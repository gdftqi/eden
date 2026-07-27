#ifndef __ADAM_CORE_PROTO_PID_TER_KIC_HPP__
#define __ADAM_CORE_PROTO_PID_TER_KIC_HPP__


#include <cinttypes>
#include <cstddef>


namespace adam::core {


struct TerminalKickReq {
    static constexpr int LEN = 8;


    void
    encode(uint8_t* buf, size_t len) noexcept;


    int
    decode(const uint8_t* buf, size_t len) noexcept;


    uint32_t uid;
    uint32_t code;


    TerminalKickReq() = default;
    ~TerminalKickReq() = default;
    TerminalKickReq(const TerminalKickReq&) = delete;
    TerminalKickReq& operator=(const TerminalKickReq&) = delete;
    TerminalKickReq(TerminalKickReq&&) = delete;
    TerminalKickReq& operator=(TerminalKickReq&&) = delete;
}; // struct KickTerminalReq;


struct TerminalKickRsp {
    static constexpr int LEN = 0;


    TerminalKickRsp() = default;
    ~TerminalKickRsp() = default;
    TerminalKickRsp(const TerminalKickRsp&) = delete;
    TerminalKickRsp& operator=(const TerminalKickRsp&) = delete;
    TerminalKickRsp(TerminalKickRsp&&) = delete;
    TerminalKickRsp& operator=(TerminalKickRsp&&) = delete;
}; // struct KickTerminalRsp;


struct TerminalKickNotify {
    static constexpr int LEN = 8;


    void
    encode(uint8_t* buf, size_t len) noexcept;


    int
    decode(const uint8_t* buf, size_t len) noexcept;


    uint32_t uid;
    uint32_t code;


    TerminalKickNotify() = default;
    ~TerminalKickNotify() = default;
    TerminalKickNotify(const TerminalKickNotify&) = delete;
    TerminalKickNotify& operator=(const TerminalKickNotify&) = delete;
    TerminalKickNotify(TerminalKickNotify&&) = delete;
    TerminalKickNotify& operator=(TerminalKickNotify&&) = delete;
}; // struct KickTerminalNotify;

    
} // namespace adam::core


#endif // __ADAM_CORE_PROTO_PID_TER_KIC_HPP__
