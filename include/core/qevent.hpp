#ifndef __TYPHON_CORE_QEVENT_HPP__
#define __TYPHON_CORE_QEVENT_HPP__


#include "core/typhon.in.hpp"


namespace typhon::core {
    

/**
 * @brief 队列事件
 * 
 * 与工作线程中的 event fd 联合使用 
 */
struct QEvent {
    enum Type: uint8_t {
        Stop,       ///< 停止      data => nullptr
        AddServ,    ///< 后端服务  data => AddServArg
        RmvServ,    ///< 移除后端  data => serv_id
        TcpRecv,    ///< 收到数据  data => TcpRecvArg
        TcpSend,    ///< 发送数据  data => sock fd
        AddSess,    ///< 添加会话  data => sock fd
        RmvSess,    ///< 移除会话  data => sock fd
        KcpSend,    ///< 发送数据  data => KcpSendArg
    };


    explicit
    QEvent(Type t, void* ptr = nullptr) 
        : qe_type(t) {
        qe_data.ptr = ptr;
    }


    explicit
    QEvent(Type t, SOCKET fd)
        : qe_type(t) {
        qe_data.fd = fd;
    }


    Type qe_type; ///< 事件类型


    union {
        SOCKET fd;
        void*  ptr;
    } qe_data;


private:
    QEvent(const QEvent&) = delete;
    QEvent& operator=(const QEvent&) = delete;
    QEvent(QEvent&&) = delete;
    QEvent& operator=(QEvent&&) = delete;
}; // struct QEvent;


/**
 * @brief QEvent::Type::Recv 协带参数
 */
struct TcpRecvArg {
    core::SOCKET fd;
    uint32_t     len;
    uint8_t      data[];
};


/**
 * @brief QEvent::Type::NewServArg 协带参数
 */
struct AddServArg {
    uint32_t id       { 0 };
    char     host[32] { 0 };
};


struct KcpSendArg {
    uint8_t* raw  { nullptr };
    size_t   len  { 0 };
};


} // namespace typhon::core;


#endif // __TYPHON_CORE_QEVENT_HPP__