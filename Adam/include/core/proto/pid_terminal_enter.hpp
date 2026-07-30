#ifndef __ADAM_CORE_PROTO_PID_TER_ENT_HPP__
#define __ADAM_CORE_PROTO_PID_TER_ENT_HPP__


#include <cinttypes>
#include <cstddef>


namespace adam::core {


/**
 * @brief 终端进入 Backend 请求. 
 *        由客户端或网关发起.
 */
struct TerminalEnterReq {
    static constexpr int LEN = 16;


    void
    encode(uint8_t* buf, size_t len) noexcept;


    int
    decode(const uint8_t* buf, size_t len) noexcept;


    uint32_t uid;   // terminal uid
    uint32_t conv;  // terminal conv
    uint32_t ip;    // terminal ip
    uint32_t port;  // terminal port
    uint32_t type;  // terminal 类型


    TerminalEnterReq() = default;
    ~TerminalEnterReq() = default;
    TerminalEnterReq(const TerminalEnterReq&) = delete;
    TerminalEnterReq& operator=(const TerminalEnterReq&) = delete;
    TerminalEnterReq(TerminalEnterReq&&) = delete;
    TerminalEnterReq& operator=(TerminalEnterReq&&) = delete;
}; // struct TerminalEnterReq;


/**
 * @brief 终端进入 backend 响应
 */
struct TerminalEnterRsp {
    static constexpr int LEN = 8;


    void
    encode(uint8_t* buf, size_t len) noexcept;


    int
    decode(const uint8_t* buf, size_t len) noexcept;


    uint32_t code; // 响应码, 0: 成功.
    uint32_t uid;  // 成功进入的 terminal uid.


    TerminalEnterRsp() = default;
    ~TerminalEnterRsp() = default;
    TerminalEnterRsp(const TerminalEnterRsp&) = delete;
    TerminalEnterRsp& operator=(const TerminalEnterRsp&) = delete;
    TerminalEnterRsp(TerminalEnterRsp&&) = delete;
    TerminalEnterRsp& operator=(TerminalEnterRsp&&) = delete;
}; // struct TerminalEnterRsp;

    
} // namespace adam::core


#endif // __ADAM_CORE_PROTO_PID_TER_ENT_HPP__
