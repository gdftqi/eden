#ifndef __ADAM_KCP_MESSAGE_HPP__
#define __ADAM_KCP_MESSAGE_HPP__


#include "core/adam.in.hpp"
#include "tcp/connector.hpp"


namespace adam::kcp {


struct Message {
    Message(const Message&) = delete;
    Message& operator=(const Message&) = delete;
    Message(Message&&) = delete;
    Message& operator=(Message&&) = delete;


    enum class Type {
        /**
         * @brief 停止
         */
        Stop,

        /**
         * @brief 
         */
        EnsureBackend,

        /**
         * @brief 向 Session 转发
         */
        ForwardToSession
    };


    explicit
    Message(Type t, void* ptr = nullptr) 
        : type(t) {
        arg.ptr = ptr;
    }


    explicit
    Message(Type t, SOCKET fd)
        : type(t) {
        arg.fd = fd;
    }


    Type type;
    union {
        SOCKET fd;
        void*  ptr;
    } arg;
}; // struct Message;



/**
 * @brief Message::Type::EnsureBackendArg 协带参数
 */
struct EnsureBackendArg {
    EnsureBackendArg(const EnsureBackendArg&) = delete;
    EnsureBackendArg(EnsureBackendArg&&) = delete;
    EnsureBackendArg& operator=(const EnsureBackendArg&) = delete;
    EnsureBackendArg& operator=(EnsureBackendArg&&) = delete;


    explicit
    EnsureBackendArg(uint32_t id, const char* host, const tcp::Connector::PidSet& pids, bool router)
        : id(id)
        , router(router)
        , pids(pids) {
        ::memcpy(this->host, host, sizeof(this->host));
    }


    uint32_t                id       { 0 };
    bool                    router   { false };   // 是否终端路由服务
    char                    host[32] { 0 };
    tcp::Connector::PidSet  pids;   // 后端声明的可受理 PID(随发现结果一路带到 worker)
}; // struct EnsureBackendArg;


/**
 * @brief Message::Type::ForwardToSession 协带参数
 */
struct ForwardToSessionArg {
    ForwardToSessionArg(const ForwardToSessionArg&) = delete;
    ForwardToSessionArg(ForwardToSessionArg&&) = delete;
    ForwardToSessionArg& operator=(const ForwardToSessionArg&) = delete;
    ForwardToSessionArg& operator=(ForwardToSessionArg&&) = delete;


    explicit
    ForwardToSessionArg(uint8_t* raw, size_t len) noexcept
        : raw(raw)
        , len(len) {
        this->raw = (uint8_t*)::mi_malloc(len);
        ::memcpy(this->raw, raw, len);
    }


    ~ForwardToSessionArg() {
        if (raw) {
            ::mi_free(raw);
        }
    }


    uint8_t* raw  { nullptr };
    size_t   len  { 0 };
}; // struct ForwardToSession;

    
} // namespace adam::kcp


#endif // __ADAM_KCP_MESSAGE_HPP__
