#ifndef __TYPHON_TCP_WORKER_HPP__
#define __TYPHON_TCP_WORKER_HPP__


#include "typhon.in.hpp"
#include "utils/log.hpp"
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


    static Ptr
    create() noexcept {
        return Ptr(new TcpWorker);
    }

    
    ~TcpWorker() noexcept {
        release();
    }


    bool
    running() const noexcept {
        return state_.load(std::memory_order_relaxed) == State::Running;
    }


    void
    run() noexcept;


    void
    stop() noexcept {
        State expected = State::Running;
        if (state_.compare_exchange_strong(expected, State::Stopping)) {
            constexpr uint64_t event = 1;
            auto n = ::write(que_evfd_, &event, sizeof(event));
            if (n != sizeof(event)) {
                xWARN("write failed: errno = {}, errstr = {}", errno, ::strerror(errno));
            }
        }
    }


    void
    push(RcvBuf* rbuf) noexcept {
        rque_.enqueue(std::move(rbuf));
        bool expected = false;
        if (sending_.compare_exchange_strong(expected, true)) {
            constexpr uint64_t event = 1;
            if (::write(que_evfd_, &event, sizeof(event)) != sizeof(event)) {
                xERROR("write failed: errno = {}, errstr = {}", errno, ::strerror(errno));
            }
        }
    }


private:
    explicit
    TcpWorker() noexcept
    {}


    void
    init() noexcept;


    void
    release() noexcept;


    int
    on_stop_handle(const ::epoll_event& ev) noexcept;


    int
    on_que_handle(const ::epoll_event& ev) noexcept;


    SOCKET               epfd_      { INVALID_SOCKET };
    SOCKET               que_evfd_    { INVALID_SOCKET }; // 队列事件
    SOCKET               stop_evfd_    { INVALID_SOCKET }; // 停止事件
    std::atomic<State>   state_     { State::Stopped };
    std::atomic<bool>    sending_   { false };
    utils::SPSC<RcvBuf*> rque_;
}; // class TcpWorker;

    
} // namespace typhon



#endif // __TYPHON_TCP_WORKER_HPP__