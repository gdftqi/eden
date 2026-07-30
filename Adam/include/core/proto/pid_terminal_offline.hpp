#ifndef __ADAM_CORE_PROTO_PID_TER_OFF_HPP__
#define __ADAM_CORE_PROTO_PID_TER_OFF_HPP__


#include <cinttypes>
#include <cstddef>


namespace adam::core {


/**
 * @brief 终端离线通知
 */
struct TerminalOfflineNotify {
    static constexpr int LEN = 8;


    void
    encode(uint8_t* buf, size_t len) noexcept;


    int
    decode(const uint8_t* buf, size_t len) noexcept;


    uint32_t uid;  // 离线的 terminal
    uint32_t code; // 离开码


    TerminalOfflineNotify() = default;
    ~TerminalOfflineNotify() = default;
    TerminalOfflineNotify(const TerminalOfflineNotify&) = delete;
    TerminalOfflineNotify& operator=(const TerminalOfflineNotify&) = delete;
    TerminalOfflineNotify(TerminalOfflineNotify&&) = delete;
    TerminalOfflineNotify& operator=(TerminalOfflineNotify&&) = delete;
}; // struct TerminalLeaveNotify;

    
} // namespace adam::core


#endif // __ADAM_CORE_PROTO_PID_TER_OFF_HPP__
