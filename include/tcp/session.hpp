#ifndef __TYPHON_TCP_SESSION_HPP__
#define __TYPHON_TCP_SESSION_HPP__


#include "core/typhon.in.hpp"
#include "core/package.hpp"


namespace typhon::tcp {


class Session {
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;


    struct PkgBuf {
        uint32_t rpos { 0 };
        uint32_t wpos { 0 };
        uint8_t  buf[core::PKG_MAX_LEN];


        size_t
        readable() const noexcept {
            return wpos - rpos;
        }


        size_t
        writable() const noexcept {
            return core::PKG_MAX_LEN - wpos;
        }


        bool
        append(const uint8_t* data, uint32_t len) noexcept {
            if (writable() < len) {
                compact();
                if (writable() < len) {
                    return false;
                }
            }

            ::memcpy(buf + wpos, data, len);
            wpos += len;
            return true;
        }


        bool
        decode(core::Package** pkg, core::PackageTail** pkt) noexcept {
            if (readable() < core::PKG_HEADER_LEN + core::PKG_TAIL_LEN) {
                return false;
            }

            auto* p = (core::Package*)(buf + rpos);
            uint16_t pklen = ::ntohs(p->pk_len);
            size_t total = pklen;
            if (readable() < total) {
                return false;
            }

            pk_ntoh(p);
            auto* t = (core::PackageTail*)(buf + rpos + pklen);
            pkt_ntoh(t);
            *pkg = p;
            *pkt = t;
            rpos += total;
            return true;
        }

        
        void
        reset() noexcept {
            rpos = wpos = 0;
        }


        void
        compact() noexcept {
            if (rpos == 0) {
                return;
            }

            size_t remaining = readable();
            if (remaining > 0) {
                ::memmove(buf, buf + rpos, remaining);
            }

            rpos = 0;
            wpos = remaining;
        }
    }; // PkgBuf;


public:
    typedef std::unique_ptr<Session> Ptr;


    static Ptr
    create(core::SOCKET sockfd, uint32_t tnow) noexcept {
        return std::make_unique<Session>(sockfd, tnow);
    }


    explicit
    Session(core::SOCKET sockfd, uint32_t tnow) noexcept
        : sockfd_(sockfd)
        , addrlen_(sizeof(addr_))
        , last_recv_ms_(tnow) {
        constexpr int on = 1;
        constexpr int bufsize = 1024 * 1024 * 8;
        ASSERT(::getpeername(sockfd_, (sockaddr*)&addr_, &addrlen_) == 0, "failed to get peer name");
        ASSERT(core::set_nonblocking(sockfd_) == 0, "failed to set non-blocking");
        ASSERT(::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)) == 0, "failed to set TCP_NODELAY");
        ASSERT(::setsockopt(sockfd_, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize)) == 0, "failed to set send buffer");
        ASSERT(::setsockopt(sockfd_, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize)) == 0, "failed to set receive buffer");
    }


    ~Session() noexcept {
        if (sockfd_ != core::INVALID_SOCKET) {
            ::close(sockfd_);
        }
    }


    bool
    authed() const noexcept {
        return authed_;
    }


    core::SOCKET
    sockfd() const noexcept {
        return sockfd_;
    }


    uint32_t
    last_recv_ms() const noexcept {
        return last_recv_ms_;
    }


    std::string
    remote_addr() const {
        return core::sockaddr_to_string((const sockaddr*)&addr_);
    }


    bool
    input(const uint8_t* buf, size_t len) noexcept {
        return pbuf_.append(buf, len);
    }


    int
    recv(core::Package** pk, core::PackageTail** pkt, uint32_t tnow) noexcept {
        if (!pbuf_.decode(pk, pkt)) {
            return -1;
        }

        if ((*pk)->pk_idem <= rcv_idem_) {
            return 0;
        }

        rcv_idem_ = (*pk)->pk_idem;
        last_recv_ms_ = tnow;
        return 1;
    }


    int
    send(core::Package* pk) noexcept;


private:
    bool             authed_       { false };
    core::SOCKET     sockfd_       { core::INVALID_SOCKET };
    sockaddr_storage addr_         {};
    socklen_t        addrlen_      { 0 };
    uint32_t         last_recv_ms_ { 0 };
    uint32_t         rcv_idem_     { 0 };
    void*            user_data_    { nullptr };
    PkgBuf           pbuf_         {};
}; // class TcpSession;

    
} // namespace typhon::tcp


#endif // __TYPHON_TCP_SESSION_HPP__