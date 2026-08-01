#ifndef __ADAM_CORE_PROTO_PID_TER_BIND_HPP__
#define __ADAM_CORE_PROTO_PID_TER_BIND_HPP__


#include <cinttypes>
#include <cstddef>


namespace adam::core {


/**
 * @brief 绑定通知, 当 terminal enter 某个backend 成功时,
 *        该 backend 会给 网关发送 bind 通知
 */
struct TerminalBindNotify {
    static constexpr size_t LEN = sizeof(uint32_t);


    void
    encode(uint8_t* buf, size_t len) noexcept;


    int
    decode(const uint8_t* buf, size_t len) noexcept;


    /**
     * @brief 绑定的 terminal uid
     */
    uint32_t uid;


    TerminalBindNotify() = default;
    ~TerminalBindNotify() = default;
    TerminalBindNotify(const TerminalBindNotify&) = delete;
    TerminalBindNotify& operator=(const TerminalBindNotify&) = delete;
    TerminalBindNotify(TerminalBindNotify&&) = delete;
    TerminalBindNotify& operator=(TerminalBindNotify&&) = delete;
}; // struct TerminalBindNotify;

    
} // namespace adam::core


#endif // __ADAM_CORE_PROTO_PID_TER_BIND_HPP__
