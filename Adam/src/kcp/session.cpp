#include "kcp/session.hpp"
#include "kcp/server.hpp"
#include "core/proto/pid_terminal_offline.hpp"
#include "core/proto/pid_terminal_enter.hpp"
#include "core/proto/pid_terminal_error.hpp"
#include <format>


namespace {
using namespace adam;

static_assert(core::ENVELOPE_HDR_LEN == core::ENVELOPE_CTR_OFF + 4, "信封头布局不自洽");
static_assert(core::ENVELOPE_OVERHEAD == core::ENVELOPE_HDR_LEN + utils::XX20_TAG_LEN, "ENVELOPE_OVERHEAD 与 AEAD tag 长度对不上");
static_assert(core::ENVELOPE_HDR_LEN + core::KCP_MTU + utils::XX20_TAG_LEN <= core::UDP_MTU, "封装后超过 UDP_MTU: KCP_MTU 太大或信封开销变了");
static_assert(core::ENVELOPE_MAC_LEN + core::KCP_MTU <= core::UDP_MTU, "握手期数据报超过 UDP_MTU");
static_assert(core::ENVELOPE_MAC_LEN + core::KCP_HDR_LEN >= core::ENVELOPE_MAC_LEN + 24,  "最小包长不足以让 XDP 取到 24 字节哈希输入");

} // namespace


// client → server (上行, recv 解密用)
static constexpr uint8_t DIR_C2S = 0;

// server → client (下行, send 加密用)
static constexpr uint8_t DIR_S2C = 1;


static inline void
make_nonce(uint8_t nonce[adam::utils::XX20_NONCE_LEN], uint32_t conv, uint32_t ctr, uint8_t dir) noexcept {
    // 12B nonce = conv(4) | 计数器(4) | dir(1) | 0(3)
    // 密钥本身已按会话+方向分开, 计数器每次发送递增 -- 三者任一唯一即可, 这里三重保险
    ::memset(nonce, 0, adam::utils::XX20_NONCE_LEN);
    ::memcpy(nonce + 0, &conv, sizeof(conv));
    ::memcpy(nonce + 4, &ctr, sizeof(ctr));
    nonce[8] = dir;
}


adam::kcp::Session::Session(uint32_t conv, Worker* worker, const void* addr, socklen_t addrlen) noexcept
    : worker_(worker)
    , json_(std::format("{{\"conv\":{},\"remote\":\"{}\"}}", conv, core::sockaddr_to_string((sockaddr*)addr))) {
    kcp_ = ::ikcp_create(conv, this);

    ::memcpy(&addr_, addr, addrlen);
    addrlen_ = addrlen;

    auto* c = Conf::instance();
    ::ikcp_wndsize(kcp_, c->sndwnd(), c->rcvwnd());
    ::ikcp_nodelay(kcp_, c->nodelay(), c->interval(), c->resend(), c->nc());
    ::ikcp_setmtu(kcp_, core::KCP_MTU);

    // 槽位密钥留在 Session 自己手里, 不再交给 ikcp -- 信封改由 Session 层统一封装,
    // 因为槽位 MAC 现在要盖在密文上, ikcp 拿不到密钥也看不到密文.
    // 下标算法必须与 XDP 侧的 conv & (nkeys-1) 一致.
    ::memcpy(slot_key_, c->siphashs()[conv & (c->nsiphash() - 1)], sizeof(slot_key_));

    kcp_->timeout = 5000;
    set_output(Worker::output);
}


void
adam::kcp::Session::terminal_enter() noexcept {
    auto& routers = worker_->router_ids();
    if (routers.empty()) {
        xWARN("尚未发现路由服务, {} 无法登记", to_json());
        return;
    }

    uint32_t rid = routers[uid_ % routers.size()];
    auto serv = worker_->get_serv(rid);
    if (serv == nullptr || !serv->is_connected() || !serv->authed()) {
        xWARN("路由服务 {} 不可用, {} 登记失败", rid, to_json());
        return;
    }

    core::TerminalEnterReq req;
    req.uid  = uid_;
    req.conv = conv();
    req.ip   = remote_addr_u32();
    req.port = ::ntohs(((sockaddr_in*)&addr_)->sin_port);
    // TODO: 设备类型待 Eva 的 token 带过来
    req.type = 0;

    alignas(core::Package) uint8_t buf[sizeof(core::Package) + core::TerminalEnterReq::LEN];
    auto* pk = (core::Package*)buf;

    // 信封由网关盖章: 后端会拿 payload 里的 conv/ip 与之交叉校验
    pk->meta.len      = core::PKG_HDR_LEN + core::TerminalEnterReq::LEN;
    pk->meta.conv     = conv();
    pk->meta.src_addr = req.ip;
    pk->data.pid      = PID_TER_ENT_REQ;
    pk->data.src_id   = Conf::instance()->server()->id;
    pk->data.dst_id   = rid;
    req.encode(pk->data.payload, core::TerminalEnterReq::LEN);

    if (serv->send(*pk, worker_->tnow()) < 0) {
        xERROR("{} 向路由服务 {} 登记失败", to_json(), rid);
    }
}


void
adam::kcp::Session::terminal_off(uint32_t code) noexcept {
    if (binds_.empty()) {
        return;
    }

    core::TerminalOfflineNotify ntf;
    ntf.uid  = uid_;
    ntf.code = code;

    alignas(core::Package) uint8_t buf[sizeof(core::Package) + core::TerminalOfflineNotify::LEN];
    auto* pk = (core::Package*)buf;

    // meta.conv 不只是路由信息, 也是"哪一次会话下线了"的凭据 --
    pk->meta.len      = core::PKG_HDR_LEN + core::TerminalOfflineNotify::LEN;
    pk->meta.conv     = conv();
    pk->meta.src_addr = remote_addr_u32();
    pk->data.pid      = PID_TER_OFF_NTF;
    pk->data.src_id   = Conf::instance()->server()->id;
    ntf.encode(pk->data.payload, core::TerminalOfflineNotify::LEN);

    for (auto sid : binds_) {
        auto serv = worker_->get_serv(sid);
        if (serv == nullptr || !serv->is_connected() || !serv->authed()) {
            continue;
        }

        pk->data.dst_id = sid;
        if (serv->send(*pk, worker_->tnow()) < 0) {
            xERROR("{} 向 {} 发 OFF 失败", to_json(), sid);
        }
    }
}


void
adam::kcp::Session::send_error(uint32_t code, uint32_t dst_id, uint32_t pid) noexcept {
    if (!authed()) {
        return;
    }

    core::ErrorNotify ntf;
    ntf.code   = code;
    ntf.dst_id = dst_id;
    ntf.pid    = pid;

    alignas(core::Package) uint8_t buf[sizeof(core::Package) + core::ErrorNotify::LEN];
    auto* pk = (core::Package*)buf;

    pk->meta.len    = core::PKG_HDR_LEN + core::ErrorNotify::LEN;
    pk->data.pid    = PID_TER_ERROR;
    pk->data.src_id = Conf::instance()->server()->id;
    pk->data.dst_id = uid_;
    ntf.encode(pk->data.payload, core::ErrorNotify::LEN);

    if (send(pk) < 0) {
        xERROR("{} 回 PID_TER_ERROR 失败: code = {}", to_json(), code);
    }
}


int
adam::kcp::Session::input(const uint8_t* data, size_t len, const void* addr, socklen_t addrlen) noexcept {
    thread_local static uint8_t dg[core::UDP_MTU];

    if (len < core::ENVELOPE_MAC_LEN + core::KCP_HDR_LEN) {
        return IKCP_ERR_TOOSHORT;
    }

    const uint8_t* kcp_dg = data + core::ENVELOPE_MAC_LEN;
    size_t kcp_len = len - core::ENVELOPE_MAC_LEN;

    if (authed()) {
        // 已鉴权
        int n = sealedbox_decode(data, len, dg);
        if (n > 0) {
            env_up_ = true;
            kcp_dg = dg;
            kcp_len = n;
        } else if (env_up_) {
            return reject();
        }
    }

    int res = ::ikcp_input(kcp_, kcp_dg, kcp_len);
    if (res == 0) {
        ::memcpy(&addr_, addr, addrlen);
        addrlen_ = addrlen;
    }

    // ikcp 的码直接上抛: 两位数段与框架的三位数段不重叠, 不会有歧义
    return res;
}


int
adam::kcp::Session::sealedbox_encode(const uint8_t* dg, size_t len, uint8_t* out) noexcept {
    if (!env_up_) {
        // 无信封处理
        ::memcpy(out + core::ENVELOPE_MAC_LEN, dg, len);

        uint64_t t = utils::siphash24(out + core::ENVELOPE_MAC_LEN, core::KCP_HDR_LEN, slot_key_);
        ::memcpy(out, &t, sizeof(t));
        return core::ENVELOPE_MAC_LEN + len;
    }

    if (snd_ctr_ == UINT32_MAX) {
        xERROR("{} 信封计数器耗尽", to_json());
        return xERR;
    }

    uint32_t ctr = ++snd_ctr_;

    uint32_t v = core::u32_to_le(conv());
    ::memcpy(out + core::ENVELOPE_CONV_OFF, &v, sizeof(v));

    v = core::u32_to_le(ctr);
    ::memcpy(out + core::ENVELOPE_CTR_OFF, &v, sizeof(v));

    uint8_t nonce[utils::XX20_NONCE_LEN];
    make_nonce(nonce, conv(), ctr, DIR_S2C);

    uint8_t* cipher = out + core::ENVELOPE_HDR_LEN;
    ASSERT(utils::xx20_encrypt(dg, len, cipher, cipher + len, tx_key_, nonce, out + core::ENVELOPE_CONV_OFF, core::ENVELOPE_HDR_LEN - core::ENVELOPE_CONV_OFF) == 0, "加密失败");

    uint64_t tag = utils::siphash24(out + core::ENVELOPE_MAC_LEN, core::KCP_HDR_LEN, slot_key_);
    ::memcpy(out, &tag, sizeof(tag));

    return core::ENVELOPE_HDR_LEN + len + (int)utils::XX20_TAG_LEN;
}


int
adam::kcp::Session::sealedbox_decode(const uint8_t* wire, size_t len, uint8_t* out) noexcept {
    if (len < core::ENVELOPE_OVERHEAD + core::KCP_HDR_LEN) {
        return xERR;
    }

    uint32_t* p = (uint32_t*)(wire + core::ENVELOPE_CTR_OFF); 
    uint32_t ctr = core::u32_to_le(*p);

    // 先查窗口: 重放包不值得浪费一次 AEAD
    if (!replay_ok(ctr)) {
        return xERR;
    }

    size_t cipher_len = len - core::ENVELOPE_OVERHEAD;
    const uint8_t* cipher = wire + core::ENVELOPE_HDR_LEN;

    uint8_t nonce[utils::XX20_NONCE_LEN];
    make_nonce(nonce, conv(), ctr, DIR_C2S);

    if (utils::xx20_decrypt(cipher, cipher_len, out, cipher + cipher_len, rx_key_, nonce, wire + core::ENVELOPE_CONV_OFF, core::ENVELOPE_HDR_LEN - core::ENVELOPE_CONV_OFF) != 0) {
        return xERR;
    }

    replay_commit(ctr);
    return (int)cipher_len;
}


int
adam::kcp::Session::recv(adam::core::Package* pk) noexcept {
    thread_local static uint8_t rbuf[core::PKG_MAX_LEN];

    int res = ::ikcp_recv(kcp_, rbuf, sizeof(rbuf));
    if (res < 0) {
        // EMPTY/FRAGMENT 是"这轮取完了", 不是错 -- 必须翻成 xAGAIN,
        // 否则调用方按 res < 0 判死, 每次正常收包都会踢人.
        if (res == IKCP_ERR_EMPTY || res == IKCP_ERR_FRAGMENT) {
            return xAGAIN;
        }

        return res;
    }

    size_t dglen = (size_t)res;
    if (dglen < core::PKG_DATA_LEN || dglen > core::PKG_MAX_LEN - core::PKG_META_LEN) {
        return xERR_PK_LEN;
    }

    if (core::data_decode(pk, rbuf, dglen) < 0) {
        return xERR_PK_LEN;
    }

    pk->meta.conv     = conv();
    pk->meta.src_addr = remote_addr_u32();

    int err = xOK;

    if (pk->data.pid == 0) {
        err = xERR_PK_PID;
    } else if (pk->data.src_id == 0) {
        err = xERR_PK_SRC;
    } else if (pk->data.dst_id == 0) {
        err = xERR_PK_DST;
    }

    if (err != xOK) {
        return err;
    }

    dec_fail_ = 0;
    return xOK;
}


int
adam::kcp::Session::send(core::Package *pk) noexcept  {
    constexpr size_t SND_MAX_PAYLOAD = core::PKG_MAX_LEN - core::PKG_DATA_LEN;

    // 发送缓冲区
    thread_local static uint8_t sndbuf[core::PKG_MAX_LEN];

    size_t plen = pk->payload_length();
    if (plen > SND_MAX_PAYLOAD) {
        return xERR_PK_LEN;
    }

    int wire = core::data_encode(sndbuf, pk);

    int res = ::ikcp_send(kcp_, sndbuf, wire);
    return res < 0 ? res : xOK;   // 成功时 ikcp_send 返回已入队长度, 上层只关心成败
}
