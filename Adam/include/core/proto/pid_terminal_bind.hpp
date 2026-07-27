#ifndef __ADAM_CORE_PROTO_PID_TER_BIND_HPP__
#define __ADAM_CORE_PROTO_PID_TER_BIND_HPP__


#include <cinttypes>
#include <cstddef>


namespace adam::core {


struct TerminalBindNotify {
    static constexpr int LEN = sizeof(uint32_t);


    void
    encode(uint8_t* buf, size_t len) noexcept;


    int
    decode(const uint8_t* buf, size_t len) noexcept;


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
