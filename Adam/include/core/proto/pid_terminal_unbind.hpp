#ifndef __ADAM_CORE_PROTO_PID_TER_UNBD_HPP__
#define __ADAM_CORE_PROTO_PID_TER_UNBD_HPP__


#include <cinttypes>
#include <cstddef>


namespace adam::core {


struct TerminalUnbindNotify {
    static constexpr int LEN = sizeof(uint32_t);


    void
    encode(uint8_t* buf, size_t len) noexcept;


    int
    decode(const uint8_t* buf, size_t len) noexcept;


    uint32_t uid;


    TerminalUnbindNotify() = default;
    ~TerminalUnbindNotify() = default;
    TerminalUnbindNotify(const TerminalUnbindNotify&) = delete;
    TerminalUnbindNotify& operator=(const TerminalUnbindNotify&) = delete;
    TerminalUnbindNotify(TerminalUnbindNotify&&) = delete;
    TerminalUnbindNotify& operator=(TerminalUnbindNotify&&) = delete;
}; // struct TerminalUnbindNotify;

    
} // namespace adam::core


#endif // __ADAM_CORE_PROTO_PID_TER_UNBD_HPP__
