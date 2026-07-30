#ifndef __ADAM_CORE_PROTO_PID_TER_LEA_HPP__
#define __ADAM_CORE_PROTO_PID_TER_LEA_HPP__


#include <cinttypes>
#include <cstddef>


namespace adam::core {


/**
 * @brief 终端离开 backend 请求
 */
struct TerminalLeaveReq {
    static constexpr int LEN = 0;


    TerminalLeaveReq() = default;
    ~TerminalLeaveReq() = default;
    TerminalLeaveReq(const TerminalLeaveReq&) = delete;
    TerminalLeaveReq& operator=(const TerminalLeaveReq&) = delete;
    TerminalLeaveReq(TerminalLeaveReq&&) = delete;
    TerminalLeaveReq& operator=(TerminalLeaveReq&&) = delete;
}; // struct TerminalLeaveReq;


/**
 * @brief 终端离开 backend 响应
 */
struct TerminalLeaveRsp {
    static constexpr int LEN = 0;


    TerminalLeaveRsp() = default;
    ~TerminalLeaveRsp() = default;
    TerminalLeaveRsp(const TerminalLeaveRsp&) = delete;
    TerminalLeaveRsp& operator=(const TerminalLeaveRsp&) = delete;
    TerminalLeaveRsp(TerminalLeaveRsp&&) = delete;
    TerminalLeaveRsp& operator=(TerminalLeaveRsp&&) = delete;
}; // struct TerminalLeaveRsp;


} // namespace adam::core


#endif // __ADAM_CORE_PROTO_PID_TER_LEA_HPP__
