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
        // PACK_MAX_LEN 已包含 PackageTail 上限,直接用即可
        static constexpr size_t PKG_BUF_SIZE = PACK_MAX_LEN;


        uint32_t rpos { 0 };
        uint32_t wpos { 0 };
        uint8_t  buf[PKG_BUF_SIZE];


        size_t
        readable() const noexcept {
            return wpos - rpos;
        }


        size_t
        writable() const noexcept {
            return PKG_BUF_SIZE - wpos;
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
            if (readable() < PKG_HEADER_LEN) {
                return false;
            }

            auto* p = (Package*)(buf + rpos);
            uint16_t pklen = ::ntohs(p->pk_len);
            size_t total = pklen + PKG_TAIL_LEN;
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


    SOCKET
    sockfd() const noexcept {
        return sockfd_;
    }


    std::string
    remote_addr() const {
        return sockaddr_to_string((const sockaddr*)&addr_);
    }


    bool
    input(const uint8_t* buf, size_t len) noexcept {
        return pbuf_.append(buf, len);
    }


    bool
    recv(Package** pkg, PackageTail** tail, uint32_t tnow) noexcept {
        if (pbuf_.decode(pkg, tail)) {
            last_recv_ms_ = tnow;
            return true;
        }

        return false;
    }


private:
    SOCKET              sockfd_         { INVALID_SOCKET };
    sockaddr_storage    addr_           {};
    socklen_t           addrlen_        { 0 };
    void*               user_data_      { nullptr };
    PkgBuf              pbuf_           {};
    uint32_t            last_recv_ms_   { 0 };
}; // class TcpSession;

    
} // namespace typhon


#endif // __TYPHON_TCP_SESSION_HPP__