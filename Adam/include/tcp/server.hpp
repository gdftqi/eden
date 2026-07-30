#ifndef __ADAM_TCP_SERVER_HPP__
#define __ADAM_TCP_SERVER_HPP__


#include "tcp/reactor.hpp"
#include "tcp/directory.hpp"
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
        ~IHook() noexcept {
        }


        virtual void
        on_init(Server*) noexcept {
        }


        virtual void
        on_stopped(Server*) noexcept {
        }


        virtual int
        on_sess_connected(Session::Ptr) noexcept {
            return 0;
        }


        virtual void
        on_sess_disconnected(Session::Ptr) noexcept {
        }


        virtual void
        on_serv_registed(Session::Ptr) noexcept {
        }


        virtual void
        on_serv_unregisted(Session::Ptr) noexcept {
        }


        virtual int
        on_terminal_enter(Terminal::Ptr) noexcept {
            return 0;
        }


        /**
         * @brief 终端离开本服务(所有删档路径统一回调)
         * @param code 离开码: 框架 TER_CODE_*(0-99) 或业务自定义(100+),
         *             业务凭它决定实体是立即销毁还是留宽限期
         */
        virtual void
        on_terminal_leave(Terminal::Ptr, uint32_t /*code*/) noexcept {
        }
    }; // class IHook;


    static constexpr int MAX_CONN = 2048; ///< 最大连接数


    typedef void (*PackageHandler)(Terminal::Ptr, core::Package*) noexcept;
    typedef absl::flat_hash_map<uint16_t, PackageHandler> PackageHandlers;


    explicit
    Server(const char* host, IHook* hook) noexcept
        : hook_(hook)
        , host_(host) {
        host_ = host_.substr(host_.find(':'));
        ASSERT(hook_ != nullptr, "hook handler cannot be null");
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


    // 终端全局目录(uid → 属主 reactor), 各 reactor 线程共享
    Directory*
    directory() noexcept {
        return &dir_;
    }


    // 按 index 取 reactor(跨 reactor 投递 TerminalKick 用)
    Reactor*
    reactor(uint32_t idx) noexcept {
        ASSERT(idx < (uint32_t)reactors_.size(), "reactor index 越界: {}", idx);
        return reactors_[idx].get();
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
    get_handler(uint16_t pid) const noexcept {
        auto itr = handlers.find(pid);
        return itr == handlers.end() ? nullptr : itr->second;
    }


    void
    regist_handler(uint16_t pid, PackageHandler handler) noexcept {
        ASSERT(pid >= PID_CUSTOM, "PID: {} 无效", pid);
        ASSERT(state_.load() == core::State::Stopped, "服务启动之后不允许再次注册 PID句柄");

        if (handlers[pid] != nullptr) {
            xWARN("handler for PID {} already exists, will be overwritten", pid);
        }

        handlers[pid] = handler;
        Conf::instance()->server()->pid_set(pid);
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
    Directory                 dir_;
}; // class Server;

    
} // namespace adam::tcp


#endif // __ADAM_TCP_SERVER_HPP__
