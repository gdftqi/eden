#include "kcp/session.hpp"
#include "kcp/server.hpp"
#include "core/proto/pid_terminal_offline.hpp"
#include "core/proto/pid_terminal_enter.hpp"
#include <format>


// client → server (上行, recv 解密用)
static constexpr uint8_t DIR_C2S = 0;

// server → client (下行, send 加密用)
static constexpr uint8_t DIR_S2C = 1;


static inline void
make_nonce(uint8_t nonce[adam::utils::XX20_NONCE_LEN], uint32_t conv, uint32_t seq, uint8_t dir) noexcept {
    // 12B nonce = conv(4) | seq(4) | dir(1) | 0(3)
    // 同一会话密钥下 (conv, seq, dir) 唯一 → nonce 绝不复用 (seq 单调递增)
    ::memset(nonce, 0, adam::utils::XX20_NONCE_LEN);
    ::memcpy(nonce + 0, &conv, sizeof(conv));
    ::memcpy(nonce + 4, &seq, sizeof(seq));
    nonce[8] = dir;
}


adam::kcp::Session::Session(
    uint32_t    conv,
    Worker*     worker,
    const void* addr,
    socklen_t   addrlen
) noexcept
    : worker_(worker)
    , json_(std::format("{{\"conv\":{},\"remote\":\"{}\"}}", conv, core::sockaddr_to_string((sockaddr*)addr))) {
    kcp_ = ::ikcp_create(conv, this);

    ::memcpy(&addr_, addr, addrlen);
    addrlen_ = addrlen;

    auto* c = Conf::instance();
    ::ikcp_wndsize(kcp_, c->sndwnd(), c->rcvwnd());
    ::ikcp_nodelay(kcp_, c->nodelay(), c->interval(), c->resend(), c->nc());
    ::ikcp_setmtu(kcp_, core::KCP_MTU);
    ::ikcp_set_siphash(kcp_, c->siphashs()[conv & (c->nsiphash() - 1)]);
    kcp_->timeout = 5000;
    set_output(Worker::output);
}


int
adam::kcp::Session::recv(adam::core::Package* pk) noexcept {
    thread_local static uint8_t rbuf[core::PKG_MAX_LEN];

    int res = ::ikcp_recv(kcp_, (char*)rbuf, sizeof(rbuf));
    if (res < 0) {
        return core::from_ikcp_recv(res);
    }

    const bool trusted = authed();
    if (res < core::PKG_DATA_LEN || res > core::PKG_MAX_LEN) {
        return trusted ? reject() : xERR_PK_LEN;
    }

    bool encrypted = trusted;
    if (encrypted && res < core::PKG_DATA_LEN + (int)utils::XX20_TAG_LEN) {
        return reject();
    }

    size_t datalen = res;
    if (encrypted) {
        uint32_t seq;
        ::memcpy(&seq, rbuf + core::PKG_DATA_LEN - sizeof(uint32_t), sizeof(seq));
        seq = core::u32_to_le(seq);

        size_t   clen   = (size_t)res - core::PKG_DATA_LEN - utils::XX20_TAG_LEN;
        uint8_t* cipher = rbuf + core::PKG_DATA_LEN;
        uint8_t* tag    = cipher + clen;

        uint8_t nonce[utils::XX20_NONCE_LEN];
        make_nonce(nonce, conv(), seq, DIR_C2S);

        // AAD 必须与发送侧一致: 14 字节头. 少传或多传都会让验签失败
        if (utils::xx20_decrypt(cipher, clen, cipher, tag, rx_key_, nonce,
                                rbuf, core::PKG_DATA_LEN)) {
            return reject();
        }

        datalen = core::PKG_DATA_LEN + clen;
    }

    if (core::data_decode(pk, rbuf, datalen) < 0) {
        return trusted ? reject() : xERR_PK_LEN;
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
    } else if (pk->data.seq == 0) {
        err = xERR_PK_SEQ;
    } else if (rcv_req_ >= pk->data.seq) {
        err = xDUP;
    }

    if (err != xOK) {
        return err;
    }

    dec_fail_ = 0;
    rcv_req_ = pk->data.seq;
    return xOK;
}


int
adam::kcp::Session::send(core::Package *pk) noexcept  {
    // 发送时最大明文 payload: 客户端线上是 data-only 帧, 整帧 = PKG_DATA_LEN 头 + payload + tag,
    // 上限 PKG_MAX_LEN。故减的是 PKG_DATA_LEN(14, meta 不上线)再减 tag, 不是 PKG_HDR_LEN
    constexpr int SND_MAX_PAYLOAD = core::PKG_MAX_LEN - core::PKG_DATA_LEN - utils::XX20_TAG_LEN;

    // 发送缓冲区
    thread_local static uint8_t sndbuf[core::PKG_MAX_LEN];

    int plen = (int)pk->payload_length();
    if (plen > SND_MAX_PAYLOAD) {
        return xERR_PK_LEN;
    }

    pk->data.seq = next_snd_seq();

    int wire;

    // 鉴权之后一律加密, 空 payload 也不例外(此时密文 0 字节, 只余 16 字节 tag)
    if (authed()) {
        core::data_encode(sndbuf, pk);

        uint8_t nonce[utils::XX20_NONCE_LEN];
        make_nonce(nonce, conv(), pk->data.seq, DIR_S2C);

        // 14 字节头作 AAD: 只认证、不加密
        ASSERT(utils::xx20_encrypt(sndbuf + core::PKG_DATA_LEN, plen,
                                   sndbuf + core::PKG_DATA_LEN,
                                   sndbuf + core::PKG_DATA_LEN + plen,
                                   tx_key_, nonce,
                                   sndbuf, core::PKG_DATA_LEN) == 0, "加密失败");

        wire = core::PKG_DATA_LEN + plen + (int)utils::XX20_TAG_LEN;
    } else {
        wire = core::data_encode(sndbuf, pk);
    }

    return core::from_ikcp_send(::ikcp_send(kcp_, (char*)sndbuf, wire));
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
    pk->data.seq      = 0;
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

    // meta.conv 不只是路由信息, 也是"哪一次会话下线了"的凭据 ——
    pk->meta.len      = core::PKG_HDR_LEN + core::TerminalOfflineNotify::LEN;
    pk->meta.conv     = conv();
    pk->meta.src_addr = remote_addr_u32();
    pk->data.pid      = PID_TER_OFF_NTF;
    pk->data.src_id   = Conf::instance()->server()->id;
    pk->data.seq      = 0;
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
