#ifndef __TYPHON_TCP_WORKER_HPP__
#define __TYPHON_TCP_WORKER_HPP__


#include "typhon.in.hpp"
#include "utils/spsc.hpp"


namespace typhon {


struct RcvBuf {
    SOCKET   fd;
    uint32_t len;
    uint8_t  data[];
};


class TcpWorker {
    TcpWorker(const TcpWorker&) = delete;
    TcpWorker& operator=(const TcpWorker&) = delete;
    TcpWorker(TcpWorker&&) = delete;
    TcpWorker& operator=(TcpWorker&&) = delete;


public:
    typedef std::unique_ptr<TcpWorker> Ptr;


    explicit
    TcpWorker() noexcept;


    void
    run() noexcept;


    void
    stop() noexcept;


    void
    push(RcvBuf* rbuf) noexcept {
        rque_.enqueue(std::move(rbuf));
    }


private:
    SOCKET epfd_ { INVALID_SOCKET };
    SOCKET evfd_ { INVALID_SOCKET };
    utils::SPSC<RcvBuf*, 4096> rque_;
};

    
} // namespace typhon



#endif // __TYPHON_TCP_WORKER_HPP__