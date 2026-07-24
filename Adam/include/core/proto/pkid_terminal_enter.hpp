#ifndef __ADAM_CORE_PROTO_PKID_TER_ENT_HPP__
#define __ADAM_CORE_PROTO_PKID_TER_ENT_HPP__


#include <cinttypes>
#include <cstddef>


namespace adam::core {


struct TerminalInfo {
    static constexpr int LEN = 16;


    uint32_t uid;
    uint32_t conv;
    uint32_t ip;
    uint32_t port;
    uint32_t type;


    void
    encode(uint8_t* buf) noexcept;


    int
    decode(const uint8_t* buf, size_t len) noexcept;


    TerminalInfo() = default;
    ~TerminalInfo() = default;
    TerminalInfo(const TerminalInfo&) = delete;
    TerminalInfo& operator=(const TerminalInfo&) = delete;
    TerminalInfo(TerminalInfo&&) = delete;
    TerminalInfo& operator=(TerminalInfo&&) = delete;
}; // struct TerminalInfo;

    
} // namespace adam::core


#endif // __ADAM_CORE_PROTO_PKID_TER_ENT_HPP__