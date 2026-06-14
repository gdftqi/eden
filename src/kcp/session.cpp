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
    if (res < 0) {
        // ikcp_recv: -1/-2 无完整包 → xAGAIN, -3 buf 太小 → xERR_KCP_BUFSMALL
        return core::from_ikcp_recv(res);
    }

    if (res < core::PKG_HDR_LEN || res > core::PKG_MAX_LEN) {
        // 非法包:长度越界(含 res==0 的空包)
        return xERR_PK_LEN;
    }

    *pk = core::ntoh(core::PK<core::Net>(buf, res));
    auto& p = *pk;

    if (p->id == 0 || p->id >= core::MAX_HANDLERS) {
        return xERR_PKT_ID;
    }

    if (p->seq == 0) {
        return xERR_PKT_SEQ;
    }

    if (p->dst_id == 0) {
        return xERR_PKT_DST;
    }

    if (rcv_req_ >= p->seq && p->id != PKID_REGIST_REQ) {
        // 幂等重复包, 跳过.
        // 但是, 如果是 PDID_REGIST_REQ 可以通过, 因为有可能是断线重连的
        return xDUP;
    }

    size_t payload_len = (size_t)res - core::PKG_HDR_LEN;
    if (payload_len > 0 && authed_) {
        uint8_t iv[utils::AES_BLOCK_LEN];
        make_iv(iv, conv(), p->seq, DIR_C2S);
        if (utils::aes128_ctr_decrypt(buf + core::PKG_HDR_LEN, payload_len, buf + core::PKG_HDR_LEN, rx_key_, iv)) {
            return xERR_PK_DEC;
        }
    }

    last_recv_ms_ = now;
    rcv_req_ = p->seq;
    return xOK;
}


int
typhon::kcp::Session::send(core::PK<core::Host> &pk) noexcept  {
    auto plen = pk.len() - core::PKG_HDR_LEN;
    pk->seq = next_snd_seq();

    if (plen > 0 && authed_) {
        uint8_t iv[utils::AES_BLOCK_LEN];
        make_iv(iv, conv(), pk->seq, DIR_S2C);
        ASSERT(utils::aes128_ctr_encrypt(pk->payload, plen, pk->payload, tx_key_, iv) == 0, "加密失败");
    }

    core::hton(pk);
    return core::from_ikcp_send(send(pk.raw(), pk.len()));
}