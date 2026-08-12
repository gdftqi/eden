#ifndef __ADAM_TCP_MESSAGE_HPP__
#define __ADAM_TCP_MESSAGE_HPP__


#include "core/adam.in.hpp"


namespace adam::tcp {


class Reactor;


struct Message {
    enum class Type {
        /**
         * @brief 停止(仅用于唤醒消息循环). 无参
         */
        Stop,

        /**
         * @brief 连接成功.  arg1.v = 新连接的 fd
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

        /**
         * @brief 踢除终端. arg1.v = uid, arg2.v = kick code
         */
        TerminalKick,

        /**
         * @brief 自定义任务.
         */
        MidHandle,
    };


    struct Arg {
        uint64_t v   { 0 };
        void*    ptr { nullptr };
    }; // struct Arg;


    explicit
    Message(Type t) noexcept
        : type(t)
    {}


    Type type;
    Arg  arg1;
    Arg  arg2;
    Arg  arg3;
    Reactor* reactor { nullptr }; // 当前 reactor 工作线程


    Message(const Message&) = delete;
    Message& operator=(const Message&) = delete;
    Message(Message&&) = delete;
    Message& operator=(Message&&) = delete;
}; // struct Message;


} // namespace adam::tcp


#endif // __ADAM_TCP_MESSAGE_HPP__
