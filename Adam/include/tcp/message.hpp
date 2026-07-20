#ifndef __ADAM_TCP_MESSAGE_HPP__
#define __ADAM_TCP_MESSAGE_HPP__


#include "core/adam.in.hpp"


namespace adam::tcp {


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
         * @brief 连接成功
         */
        SessionConnected,

        /**
         * @brief Session 连接断开
         */
        SessionDisconnected,

        /**
         * @brief Session 收到消息
         */
        SessionInput,

        /**
         * @brief Session 可以发送消息
         */
        SessionOutput
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
 * @brief QEvent::Type::Recv 协带参数
 */
struct SessionInputArg {
    SOCKET   fd;
    uint32_t len;
    uint8_t  data[];
};

    
} // namespace adam::tcp



#endif // __ADAM_TCP_MESSAGE_HPP__