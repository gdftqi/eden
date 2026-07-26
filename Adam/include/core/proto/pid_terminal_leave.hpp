#ifndef __ADAM_CORE_PROTO_PID_TER_LEA_HPP__
#define __ADAM_CORE_PROTO_PID_TER_LEA_HPP__


#include <cinttypes>
#include <cstddef>


namespace adam::core {


struct TerminalLeaveReq {
    static constexpr int LEN = 0;


    TerminalLeaveReq() = default;
    ~TerminalLeaveReq() = default;
    TerminalLeaveReq(const TerminalLeaveReq&) = delete;
    TerminalLeaveReq& operator=(const TerminalLeaveReq&) = delete;
    TerminalLeaveReq(TerminalLeaveReq&&) = delete;
    TerminalLeaveReq& operator=(TerminalLeaveReq&&) = delete;
}; // struct TerminalLeaveReq;


struct TerminalLeaveRsp {
    static constexpr int LEN = 0;


    TerminalLeaveRsp() = default;
    ~TerminalLeaveRsp() = default;
    TerminalLeaveRsp(const TerminalLeaveRsp&) = delete;
    TerminalLeaveRsp& operator=(const TerminalLeaveRsp&) = delete;
    TerminalLeaveRsp(TerminalLeaveRsp&&) = delete;
    TerminalLeaveRsp& operator=(TerminalLeaveRsp&&) = delete;
}; // struct TerminalLeaveRsp;


struct TerminalLeaveNotify {
    static constexpr int LEN = sizeof(uint32_t);


    void
    encode(uint8_t* buf, size_t len) noexcept;


    int
    decode(const uint8_t* buf, size_t len) noexcept;


    uint32_t uid;


    TerminalLeaveNotify() = default;
    ~TerminalLeaveNotify() = default;
    TerminalLeaveNotify(const TerminalLeaveNotify&) = delete;
    TerminalLeaveNotify& operator=(const TerminalLeaveNotify&) = delete;
    TerminalLeaveNotify(TerminalLeaveNotify&&) = delete;
    TerminalLeaveNotify& operator=(TerminalLeaveNotify&&) = delete;
}; // struct TerminalLeaveNotify;


} // namespace adam::core


#endif // __ADAM_CORE_PROTO_PID_TER_LEA_HPP__
