#ifndef __ADAM_TCP_MESSAGE_HPP__
#define __ADAM_TCP_MESSAGE_HPP__


#include "core/adam.in.hpp"


namespace adam::tcp {


struct Message {
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
         * @brief 终端进入
         */
        TerminalEnter,

        /**
         * @brief 终端离开
         */
        TerminalLeave,

        /**
         * @brief 终端处理
         */
        TerminalHandle,
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


    Message(const Message&) = delete;
    Message& operator=(const Message&) = delete;
    Message(Message&&) = delete;
    Message& operator=(Message&&) = delete;
}; // struct Message;

    
} // namespace adam::tcp



#endif // __ADAM_TCP_MESSAGE_HPP__