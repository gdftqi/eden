#ifndef __ADAM_CORE_PROTO_PID_TER_ENT_HPP__
#define __ADAM_CORE_PROTO_PID_TER_ENT_HPP__


#include <cinttypes>
#include <cstddef>


namespace adam::core {


struct TerminalEnterReq {
    static constexpr int LEN = 16;


    uint32_t uid;
    uint32_t conv;
    uint32_t ip;
    uint32_t port;
    uint32_t type;


    void
    encode(uint8_t* buf, size_t len) noexcept;


    int
    decode(const uint8_t* buf, size_t len) noexcept;


    TerminalEnterReq() = default;
    ~TerminalEnterReq() = default;
    TerminalEnterReq(const TerminalEnterReq&) = delete;
    TerminalEnterReq& operator=(const TerminalEnterReq&) = delete;
    TerminalEnterReq(TerminalEnterReq&&) = delete;
    TerminalEnterReq& operator=(TerminalEnterReq&&) = delete;
}; // struct TerminalEnterReq;


struct TerminalEnterRsp {
    static constexpr int LEN = 8;


    uint32_t code;
    uint32_t uid;


    void
    encode(uint8_t* buf) noexcept;


    int
    decode(const uint8_t* buf, size_t len) noexcept;


    TerminalEnterRsp() = default;
    ~TerminalEnterRsp() = default;
    TerminalEnterRsp(const TerminalEnterRsp&) = delete;
    TerminalEnterRsp& operator=(const TerminalEnterRsp&) = delete;
    TerminalEnterRsp(TerminalEnterRsp&&) = delete;
    TerminalEnterRsp& operator=(TerminalEnterRsp&&) = delete;
}; // struct TerminalEnterRsp;

    
} // namespace adam::core


#endif // __ADAM_CORE_PROTO_PID_TER_ENT_HPP__
