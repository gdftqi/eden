#ifndef __TYPHON_CORE_QEVENT_HPP__
#define __TYPHON_CORE_QEVENT_HPP__


#include "core/typhon.in.hpp"   // core::SOCKET / uint32_t / uint8_t 等类型定义


namespace typhon::core {
    

struct QEvent {
    enum Type: __UINT8_TYPE__ {
        Stop,       ///< 停止         data => nullptr
        NewServ,     ///< 新的后端服务 data => NewServArg
        Recv,       ///< 收到数据     data => RecvArg
        Send,       ///< 发送数据     data => sock fd
        AddSess,    ///< 添加会话     data => sock fd
        RmvSess,    ///< 移除会话     data => sock fd
    };


    explicit
    QEvent(Type t, void* data) 
        : qe_type(t), qe_data(data) 
    {}


    Type  qe_type; ///< 事件类型
    void* qe_data; ///< 事件数据


private:
    QEvent(const QEvent&) = delete;
    QEvent& operator=(const QEvent&) = delete;
    QEvent(QEvent&&) = delete;
    QEvent& operator=(QEvent&&) = delete;
}; // struct QEvent;


struct RecvArg {
    core::SOCKET fd;
    uint32_t     len;
    uint8_t      data[];
};


struct NewServArg {
    uint32_t id;
    char     host[32];
};


} // namespace typhon::core;


#endif // __TYPHON_CORE_QEVENT_HPP__