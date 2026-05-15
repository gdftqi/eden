#ifndef __TYPHON_UDP_SERVER_HPP__
#define __TYPHON_UDP_SERVER_HPP__


#include "kcp.hpp"
#include "package.hpp"


namespace typhon {


constexpr int MAX_RECV = 128;
constexpr int MAX_SEND = MAX_RECV;


class KcpServer {
    KcpServer(const KcpServer&) = delete;
    KcpServer& operator=(const KcpServer&) = delete;
    KcpServer(KcpServer&&) = delete;
    KcpServer& operator=(KcpServer&&) = delete;


    struct SendBuf {
        typedef std::unique_ptr<SendBuf> Ptr;


        static Ptr
        create(Kcp::Ptr kcp, const char* buf, uint32_t len) noexcept {
            return Ptr(new SendBuf(kcp, buf, len));
        }


        ~SendBuf() noexcept {
            ::mi_free(buf);
        }


        Kcp::Ptr kcp;
        uint32_t len;
        char* buf;

    private:
        SendBuf(const SendBuf&) = delete;
        SendBuf& operator=(const SendBuf&) = delete;
        SendBuf(SendBuf&&) = delete;
        SendBuf& operator=(SendBuf&&) = delete;


        SendBuf(Kcp::Ptr k, const char* b, uint32_t l) noexcept
            : kcp(k), len(l) {
            buf = (char*)::mi_malloc(len);
            ::memcpy(buf, b, len);
        }
    }; // class SendBuf;


public:
    typedef std::unique_ptr<KcpServer> Ptr;
    typedef std::unordered_map<uint32_t, Kcp::Ptr> SessionMap;
    typedef std::vector<SendBuf::Ptr> SendBufQue;

    
    class IEvent {
    public:
        virtual int 
        on_init(KcpServer*) noexcept { 
            return 0; 
        }


        virtual void 
        on_stopped(KcpServer*) noexcept 
        {}


        virtual int 
        on_connected(Kcp::Ptr) noexcept = 0;


        virtual void 
        on_disconnected(Kcp::Ptr) noexcept = 0;


        virtual int 
        on_data(Kcp::Ptr, const Package*) noexcept = 0;
    }; // class IEvent;


    explicit 
    KcpServer(const char* host, IEvent* ev) noexcept;


    ~KcpServer() noexcept {
        // ctor 里 bind 了 sockfd_,run() 没跑(或还没跑完)的话 release 在这里兜底
        release();
        for (int i = 0; i < MAX_RECV; ++i) {
            ::mi_free(riovecs_[i].iov_base);
        }
    }


    int
    fd() const noexcept {
        return sockfd_;
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
        return state_.load(std::memory_order_relaxed) == State::Running;
    }


    int
    run() noexcept;


    int
    stop() noexcept {
        State expected = State::Running;
        if (state_.compare_exchange_strong(expected, State::Stopping)) {
            static constexpr uint64_t event = 1;
            auto n = ::write(evfd_, &event, sizeof(event));
            if (n != sizeof(event)) {
                return -1;
            }
        }

        return 0;
    }


private:
    static int
    output(const char *buf, int len, struct IKCPCB*, void *user) noexcept;


    int
    init() noexcept;


    void
    release() noexcept;


    Kcp::Ptr
    get_session(uint32_t conv) noexcept {
        auto itr = sessions_.find(conv);
        return itr == sessions_.end() ? nullptr : itr->second;
    }


    void
    add_session(uint32_t conv, Kcp::Ptr kcp) noexcept {
        kcp->set_output(output);
        sessions_.emplace(conv, kcp);
        if (event_->on_connected(kcp)) {
            remove_session(kcp->conv());
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
    on_event_handle(const ::epoll_event& ev) noexcept;


    int
    on_udp_handle(const ::epoll_event& ev) noexcept;


    void
    update() noexcept;


    SOCKET              sockfd_             { INVALID_SOCKET };
    SOCKET              epfd_               { INVALID_SOCKET };
    SOCKET              evfd_               { INVALID_SOCKET };
    IEvent*             event_              { nullptr };
    uint32_t            tnow_               { 0 };
    std::atomic<State>  state_              { State::Stopped };
    std::string         host_;
    SessionMap          sessions_;
    ::mmsghdr           rmsgs_[MAX_RECV]    {};
    ::iovec             riovecs_[MAX_RECV]  {};
    ::sockaddr_storage  raddrs_[MAX_RECV]   {};
    SendBufQue          sque_;
};


} // namespace typhon;


#endif // __TYPHON_UDP_SERVER_HPP__