#ifndef __TYPHON_KCP_SERVER_HPP__
#define __TYPHON_KCP_SERVER_HPP__


#include <deque>

#include "kcp/session.hpp"
#include "core/package.hpp"


namespace typhon::kcp {


constexpr int MAX_RECV = 128;
constexpr int MAX_SEND = MAX_RECV;


class Server {
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;


    struct SendBuf {
        typedef std::unique_ptr<SendBuf> Ptr;


        static Ptr
        create(Session::Ptr kcp, const char* buf, uint32_t len) noexcept {
            return std::make_unique<SendBuf>(kcp, buf, len);
        }


        ~SendBuf() noexcept {
            ::mi_free(buf);
        }


        Session::Ptr kcp;
        uint32_t len;
        uint32_t time;
        char* buf;

    private:
        SendBuf(const SendBuf&) = delete;
        SendBuf& operator=(const SendBuf&) = delete;
        SendBuf(SendBuf&&) = delete;
        SendBuf& operator=(SendBuf&&) = delete;


        SendBuf(Session::Ptr k, const char* b, uint32_t l) noexcept
            : kcp(k), len(l) {
            buf = (char*)::mi_malloc(len);
            ASSERT(buf != nullptr, "failed to allocate memory for SendBuf");
            ::memcpy(buf, b, len);
            time = kcp->server()->tnow();
        }
    }; // class SendBuf;


public:
    typedef std::unique_ptr<Server> Ptr;
    typedef std::unordered_map<uint32_t, Session::Ptr> SessionMap;
    typedef std::deque<SendBuf::Ptr> SendBufQue;

    
    class IEvent {
    public:
        virtual
        ~IEvent() noexcept
        {}


        virtual int 
        on_init(Server*) noexcept { 
            return 0; 
        }


        virtual void 
        on_stopped(Server*) noexcept 
        {}


        virtual int 
        on_connected(Session::Ptr) noexcept {
            return 0;
        }


        virtual void 
        on_disconnected(Session::Ptr) noexcept
        {}


        virtual int 
        on_data(Session::Ptr, const core::Package*) noexcept {
            return 0;
        }
    }; // class IEvent;


    explicit 
    Server(const char* host, IEvent* ev) noexcept;


    ~Server() noexcept {
        // ctor 里 bind 了 sockfd_,run() 没跑(或还没跑完)的话 release 在这里兜底
        release();
        for (int i = 0; i < MAX_RECV; ++i) {
            ::mi_free(riovecs_[i].iov_base);
        }
    }


    int
    fd() const noexcept {
        return ufd_;
    }


    uint32_t
    tnow() const noexcept {
        return tnow_;
    }


    std::string
    host() const noexcept {
        return host_;
    }


    bool
    running() const noexcept {
        return state_.load(std::memory_order_relaxed) == core::State::Running;
    }


    void
    run() noexcept;


    void
    stop() noexcept {
        if (running()) {
            core::State expected = core::State::Running;
            if (state_.compare_exchange_strong(expected, core::State::Stopping)) {
                static constexpr uint64_t event = 1;
                ASSERT(::write(stop_evfd_, &event, sizeof(event)) == sizeof(event), "errno = {}, errstr = {}", errno, ::strerror(errno));
            }
        }
    }


private:
    static int
    output(const char *buf, int len, struct IKCPCB*, void *user) noexcept;


    void
    init() noexcept;


    void
    release() noexcept;


    Session::Ptr
    get_session(uint32_t conv) noexcept {
        auto itr = sessions_.find(conv);
        return itr == sessions_.end() ? nullptr : itr->second;
    }


    void
    add_session(uint32_t conv, Session::Ptr kcp) noexcept {
        kcp->set_output(output);
        sessions_.emplace(conv, kcp);
        if (event_->on_connected(kcp)) {
            sessions_.erase(conv);
        }
    }


    void
    remove_session(uint32_t conv) noexcept {
        auto itr = sessions_.find(conv);
        if (itr != sessions_.end()) {
            auto kcp = itr->second;
            sessions_.erase(itr);
            event_->on_disconnected(kcp);
        }
    }


    int
    on_stop_handle(const ::epoll_event& ev) noexcept;


    int
    on_udp_handle(const ::epoll_event& ev) noexcept;


    void
    update() noexcept;


    core::SOCKET             ufd_               { core::INVALID_SOCKET };
    core::SOCKET             epfd_              { core::INVALID_SOCKET };
    core::SOCKET             stop_evfd_         { core::INVALID_SOCKET };
    IEvent*                  event_             { nullptr };
    uint32_t                 tnow_              { 0 };
    std::atomic<core::State> state_             { core::State::Stopped };
    std::string              host_;
    SessionMap               sessions_;
    ::mmsghdr                rmsgs_[MAX_RECV]   {};
    ::iovec                  riovecs_[MAX_RECV] {};
    ::sockaddr_storage       raddrs_[MAX_RECV]  {};
    SendBufQue               sque_;
};


} // namespace typhon::kcp;


#endif // __TYPHON_KCP_SERVER_HPP__