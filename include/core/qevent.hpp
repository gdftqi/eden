#ifndef __TYPHON_CORE_QEVENT_HPP__
#define __TYPHON_CORE_QEVENT_HPP__


namespace typhon::core {
    

struct QEvent {
    enum Type: __UINT8_TYPE__ {
        Stop,       ///< 停止
        NewBnd,     ///< 新的后端服务
        Recv,       ///< 收到数据
        Send,       ///< 发送数据
        AddSess,    ///< 添加会话
        RmvSess,    ///< 移除会话
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


} // namespace typhon::core;


#endif // __TYPHON_CORE_QEVENT_HPP__