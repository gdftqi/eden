#include "kcp/session.hpp"
#include "kcp/server.hpp"
#include <format>


static constexpr uint8_t DIR_C2S = 0;   // client → server (上行, recv 解密用)
static constexpr uint8_t DIR_S2C = 1;   // server → client (下行, send 加密用)


static inline void
make_iv(uint8_t iv[typhon::utils::AES_BLOCK_LEN], uint32_t conv, uint32_t idem, uint8_t dir) noexcept {
    ::memset(iv, 0, typhon::utils::AES_BLOCK_LEN);
    ::memcpy(iv + 0, &conv, sizeof(conv));
    ::memcpy(iv + 4, &idem, sizeof(idem));
    iv[8] = dir;
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
typhon::kcp::Session::recv(core::PK<core::Host>* pk, uint8_t* buf, int len, uint64_t now) noexcept {
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

    auto p = core::ntoh(core::PK<core::Net>(buf, res));

    if (p->p_id == 0 || p->p_id >= core::MAX_HANDLERS) {
        return -5;
    }

    if (p->p_idem == 0) {
        return -6;
    }

    if (p->p_dst_id == 0) {
        return -7;
    }

    if (rcv_idem_ >= p->p_idem) {
        // 幂等重复包, 跳过(不解密)
        return 0;
    }

    size_t payload_len = (size_t)res - core::PKG_HDR_LEN;
    if (payload_len > 0) {
        uint8_t iv[utils::AES_BLOCK_LEN];
        make_iv(iv, conv(), p->p_idem, DIR_C2S);
        if (utils::aes128_ctr_decrypt(Conf::instance()->siphash(), iv, buf + core::PKG_HDR_LEN, buf + core::PKG_HDR_LEN, payload_len)) {
            return -8;
        }
    }

    last_recv_ms_ = now;
    rcv_idem_ = p->p_idem;
    *pk = p;
    return res;
}


int
typhon::kcp::Session::send(core::PK<core::Host> &pk) noexcept  {
    auto plen = pk.len() - core::PKG_HDR_LEN;
    pk->p_idem = next_snd_idem();

    if (plen > 0) {
        uint8_t iv[utils::AES_BLOCK_LEN];
        make_iv(iv, conv(), pk->p_idem, DIR_S2C);
        ASSERT(utils::aes128_ctr_encrypt(Conf::instance()->siphash(), iv, pk->p_payload, pk->p_payload, plen) == 0, "加密失败");
    }

    core::hton(pk);
    return send(pk.raw(), pk.len());
}