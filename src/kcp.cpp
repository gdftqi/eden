#include "kcp.hpp"
#include "kcp_server.hpp"


typhon::Kcp::Kcp(uint32_t conv, KcpServer* server) noexcept
    : server_(server) {
    kcp_ = ::ikcp_create(conv, this);

    auto& c = conf();
    ::ikcp_wndsize(kcp_, c.sndwnd, c.rcvwnd);
    ::ikcp_nodelay(kcp_, c.nodelay, c.interval, c.resend, c.nc);
    ::ikcp_setmtu(kcp_, UDP_MTU);

    last_recv_ms_ = server->tnow();
}