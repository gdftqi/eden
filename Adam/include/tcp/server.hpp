#ifndef __ADAM_TCP_SERVER_HPP__
#define __ADAM_TCP_SERVER_HPP__


#include "tcp/reactor.hpp"
#include "tcp/config.hpp"


namespace adam::tcp {


/**
 * @brief TcpServer TCP 服务端
 */
class Server {
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    Server(Server&&) = delete;
    Server& operator=(Server&&) = delete;


public:
    /**
     * @brief TCP server 钩子
     */
    class IHook {
    public:
        virtual
        ~IHook() noexcept
        {}


        virtual void
        on_init(Server*) noexcept
        {}


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


        virtual void
        on_terminal_enter(Session::Ptr, core::Package*) noexcept;


        virtual void
        on_terminal_leave(Session::Ptr, core::Package*) noexcept;
    }; // class IHook;


    static constexpr int MAX_CONN = 2048; ///< 最大连接数


    typedef void (*PackageHandler)(Session::Ptr, core::Package*) noexcept;
    typedef absl::flat_hash_map<uint16_t, PackageHandler> PackageHandlers;


    explicit
    Server(const char* host, IHook* hook) noexcept
        : hook_(hook)
        , host_(host) {
        host_ = host_.substr(host_.find(':'));
        ASSERT(hook_ != nullptr, "hook handler cannot be null");
        ASSERT(::sodium_init() == 0, "libsodium 初始化失败");
    }


    ~Server() noexcept {
        release();
    }


    std::string
    host() const noexcept {
        return host_;
    }


    // reactor 线程通过它触发 on_connected / on_disconnected 业务钩子
    IHook*
    hook() noexcept {
        return hook_;
    }


    int
    worker_size() const noexcept {
        return (int)reactors_.size();
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
                constexpr uint64_t event = 1;
                if (::write(evfd_, &event, sizeof(event)) != sizeof(event)) {
                    xWARN("write failed: errno = {}, errstr = {}", errno, ::strerror(errno));
                }
            }
        }
    }


    PackageHandler
    get_handler(uint16_t pkid) const noexcept {
        auto itr = handlers.find(pkid);
        return itr == handlers.end() ? nullptr : itr->second;
    }


    void
    regist_handler(uint16_t pkid, PackageHandler handler) noexcept {
        if (handlers[pkid] != nullptr) {
            xWARN("handler for pk_id {} already exists, will be overwritten", pkid);
        }

        handlers[pkid] = handler;
    }


private:
    void
    init() noexcept;


    void
    release() noexcept;


    void
    on_stop_handle(const ::epoll_event& ev) noexcept;


    void
    on_listen_handle(const ::epoll_event& ev) noexcept;


    void
    update_serv() noexcept;


    SOCKET                    lfd_       { INVALID_SOCKET };
    SOCKET                    evfd_      { INVALID_SOCKET };
    SOCKET                    epfd_      { INVALID_SOCKET };
    uint64_t                  tnow_      { 0 };
    uint32_t                  acc_next_  { 0 };
    IHook*                    hook_      { nullptr };
    std::atomic<core::State>  state_     { core::State::Stopped };
    std::string               host_;   
    std::vector<Reactor::Ptr> reactors_;   
    std::vector<std::thread>  threads_;
    PackageHandlers           handlers;
}; // class Server;

    
} // namespace adam::tcp


#endif // __ADAM_TCP_SERVER_HPP__