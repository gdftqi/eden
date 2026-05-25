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
    kcp_ = ::ikcp_create(conv, this);

    ::memcpy(&addr_, addr, addrlen);
    addrlen_ = addrlen;

    auto* c = Conf::instance();
    ::ikcp_wndsize(kcp_, c->sndwnd(), c->rcvwnd());
    ::ikcp_nodelay(kcp_, c->nodelay(), c->interval(), c->resend(), c->nc());
    ::ikcp_setmtu(kcp_, core::UDP_MTU);

    last_recv_ms_ = server->tnow();
}