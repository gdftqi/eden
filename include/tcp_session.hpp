#ifndef __TYPHON_TCP_SESSION_HPP__
#define __TYPHON_TCP_SESSION_HPP__


#include "typhon.in.hpp"
#include "package.hpp"


namespace typhon {


class TcpSession {
    TcpSession(const TcpSession&) = delete;
    TcpSession& operator=(const TcpSession&) = delete;
    TcpSession(TcpSession&&) = delete;
    TcpSession& operator=(TcpSession&&) = delete;


    struct PkgBuf {
        uint32_t rpos { 0 };
        uint32_t wpos { 0 };
        uint8_t  buf[PKG_MAX_LEN];


        size_t
        readable() const noexcept {
            return wpos - rpos;
        }


        size_t
        writable() const noexcept {
            return PKG_MAX_LEN - wpos;
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
        decode(Package** pkg, PackageTail** tail) noexcept {
            if (readable() < PKG_HEADER_LEN + PKG_TAIL_LEN) {
                return false;
            }

            auto* p = (Package*)(buf + rpos);
            uint16_t pklen = ::ntohs(p->pk_len);
            size_t total = pklen;
            if (readable() < total) {
                return false;
            }

            pk_ntoh(p);
            auto* t = (PackageTail*)(buf + rpos + pklen);
            pkt_ntoh(t);
            *pkg = p;
            *tail = t;
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
    explicit
    TcpSession(SOCKET sockfd, uint32_t tnow) noexcept
        : sockfd_(sockfd)
        , addrlen_(sizeof(addr_))
        , last_recv_ms_(tnow) {
        ASSERT(::getpeername(sockfd_, (sockaddr*)&addr_, &addrlen_) == 0, "failed to get peer name");
    }


    ~TcpSession() noexcept {
        if (sockfd_ != INVALID_SOCKET) {
            ::close(sockfd_);
        }
    }


    bool
    authed() const noexcept {
        return authed_;
    }


    SOCKET
    sockfd() const noexcept {
        return sockfd_;
    }


    uint32_t
    last_recv_ms() const noexcept {
        return last_recv_ms_;
    }


    std::string
    remote_addr() const {
        return sockaddr_to_string((const sockaddr*)&addr_);
    }


    bool
    input(const uint8_t* buf, size_t len) noexcept {
        return pbuf_.append(buf, len);
    }


    int
    recv(Package** pk, PackageTail** pkt, uint32_t tnow) noexcept {
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


private:
    bool             authed_        { false };
    SOCKET           sockfd_        { INVALID_SOCKET };
    sockaddr_storage addr_          {};
    socklen_t        addrlen_       { 0 };
    uint32_t         last_recv_ms_  { 0 };
    uint32_t         rcv_idem_      { 0 };
    void*            user_data_     { nullptr };
    PkgBuf           pbuf_          {};
}; // class TcpSession;

    
} // namespace typhon


#endif // __TYPHON_TCP_SESSION_HPP__