#include "kcp/session.hpp"
#include "kcp/server.hpp"
#include <format>


static constexpr uint8_t DIR_C2S = 0;   // client → server (上行, recv 解密用)
static constexpr uint8_t DIR_S2C = 1;   // server → client (下行, send 加密用)


static inline void
make_iv(uint8_t iv[typhon::utils::AES_BLOCK_LEN], uint32_t conv, uint32_t idem, uint8_t dir) noexcept {
    ::memset(iv, 0, typhon::utils::AES_BLOCK_LEN);
    ::memcpy(iv + 0, &conv, sizeof(conv));   // iv[0..4)
    ::memcpy(iv + 4, &idem, sizeof(idem));   // iv[4..8)
    iv[8] = dir;                              // iv[8]
    // iv[9..16) = 0, CTR block counter 区
}


typhon::kcp::Session::Session(
    uint32_t    conv,
    Server*     server,
    const void* addr,
    socklen_t   addrlen
) noexcept
    : server_(server)
    , desc_(std::format("[{}]{}", conv, core::sockaddr_to_string((sockaddr*)addr))) {
    kcp_ = ::ikcp_create(conv, this);

    ::memcpy(&addr_, addr, addrlen);
    addrlen_ = addrlen;

    auto* c = Conf::instance();
    ::ikcp_wndsize(kcp_, c->sndwnd(), c->rcvwnd());
    ::ikcp_nodelay(kcp_, c->nodelay(), c->interval(), c->resend(), c->nc());
    ::ikcp_setmtu(kcp_, core::KCP_MTU);

    last_recv_ms_ = server->tnow();
}


int
typhon::kcp::Session::recv_pk(core::PK<core::Host>* pk, uint8_t* buf, int len, uint64_t now) noexcept {
    int res = recv(buf, len);
    if (res == 0 || res == -1) {
        // 没有数据
        return -1;
    } else if (res == -2) {
        // 出错
        return -2;
    } else if (res == -3) {
        // buf 太小,装不下下一条消息;调用方应放大 buf 后重试
        return -3;
    }

    if (res < core::PKG_HDR_LEN || res > core::PKG_MAX_LEN) {
        // 无效的package
        return -4;
    }

    // 半加密: header 明文, 先 ntoh 读出来做合法性 / 重放校验,
    // 非法包 / 重放包直接丢, 不浪费 payload 解密.
    auto p = core::ntoh(core::PK<core::Net>(buf));   // net → host, 返回 host 序视图

    if (p->pk_id == 0 || p->pk_id >= core::MAX_HANDLERS) {
        return -5;
    }

    if (p->pk_idem == 0) {
        return -6;
    }

    if (p->pk_dst_id == 0) {
        return -7;
    }

    if (rcv_idem_ >= p->pk_idem) {
        // 幂等重复包, 跳过(不解密)
        return 0;
    }

    // 解密 payload (header 不动). IV = (conv, pk_idem, 上行方向),
    // 与客户端 send 端 make_iv 完全一致.
    size_t payload_len = (size_t)res - core::PKG_HDR_LEN;
    if (payload_len > 0) {
        uint8_t iv[utils::AES_BLOCK_LEN];
        make_iv(iv, conv(), p->pk_idem, DIR_C2S);
        if (utils::aes128_ctr_decrypt(Conf::instance()->shkey(), iv, buf + core::PKG_HDR_LEN, buf + core::PKG_HDR_LEN, payload_len)) {
            return -8;
        }
    }

    last_recv_ms_ = now;
    rcv_idem_ = p->pk_idem;
    *pk = p;
    return res;
}


int
typhon::kcp::Session::send_pk(uint16_t pk_id, uint32_t pk_idemp, uint32_t pk_dst_id, const uint8_t* data, uint16_t len) noexcept {
    thread_local static uint8_t buf[core::PKG_MAX_LEN];

    if (len > core::PKG_MAX_PAYLOAD) {
        return -1;
    }

    const size_t total = core::PKG_HDR_LEN + len;

    core::PK<core::Host> pkg{buf};
    pkg->pk_id = pk_id;
    pkg->pk_idem = pk_idemp;
    pkg->pk_dst_id = pk_dst_id;
    ::memcpy(pkg->pk_payload, data, len);

    if (len > 0) {
        uint8_t iv[utils::AES_BLOCK_LEN];
        make_iv(iv, conv(), pk_idemp, DIR_S2C);
        ASSERT(utils::aes128_ctr_encrypt(Conf::instance()->shkey(), iv, pkg->pk_payload, pkg->pk_payload, len) == 0, "加密失败");
    }

    core::hton(pkg);   // host → net (原地翻 header), 之后 buf 是 wire-ready
    return send(buf, total);
}
