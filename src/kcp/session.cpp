#include "kcp/session.hpp"
#include "kcp/server.hpp"
#include <format>


typhon::kcp::Session::Session(
    uint32_t    conv,
    Server*     server,
    const void* addr,
    socklen_t   addrlen
) noexcept
    : server_(server)
    , desc_(std::format("[{}]{}", conv, core::sockaddr_to_string((sockaddr*)addr))) {
    kcp_ = ::xkcp_create(conv, this);

    ::memcpy(&addr_, addr, addrlen);
    addrlen_ = addrlen;

    auto* c = Conf::instance();
    ::xkcp_wndsize(kcp_, c->sndwnd(), c->rcvwnd());
    ::xkcp_nodelay(kcp_, c->nodelay(), c->interval(), c->resend(), c->nc());

    last_recv_ms_ = server->tnow();
}


int
typhon::kcp::Session::recv(core::PK<core::Host>* pk, uint8_t* buf, int len, uint64_t now) noexcept {
    int res = ::xkcp_recv(kcp_, buf, len);
    if (res < 0) {
        // ikcp_recv: -1/-2 无完整包 → xAGAIN, -3 buf 太小 → xERR_KCP_BUFSMALL
        return core::from_ikcp_recv(res);
    }

    if (res < core::PKG_HDR_LEN || res > core::PKG_MAX_LEN) {
        return xERR_PK_LEN;
    }

    // 解密已在 xkcp_recv 内部完成(整条消息 AEAD), 此处 buf 已是明文 Package
    *pk = core::ntoh(core::PK<core::Net>(buf, res));
    auto& p = *pk;

    if (p->id == 0) {
        return xERR_PK_ID;
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

    last_recv_ms_ = now;
    rcv_req_ = p->seq;
    return xOK;
}


int
typhon::kcp::Session::send(core::PK<core::Host> &pk) noexcept  {
    int plen = pk.len() - core::PKG_HDR_LEN;
    if (plen > core::PKG_MAX_PAYLOAD) {
        return xERR_PK_LEN;
    }
    pk->seq  = next_snd_seq();

    core::hton(pk);
    return core::from_ikcp_send(::xkcp_send(kcp_, pk.raw(), pk.len()));
}