#include "core/proto/pkid_terminal_leave.hpp"
#include "core/adam.in.hpp"
#include "core/error.hpp"


void
adam::core::TerminalLeaveNotify::encode(uint8_t* buf, size_t len) noexcept {
    ASSERT(len >= TerminalLeaveNotify::LEN, "TerminalLeaveNotify::encode 缓冲区不足");

    uint32_t v32 = u32_to_le(uid);
    ::memcpy(buf + 0, &v32, sizeof(v32));
}


int
adam::core::TerminalLeaveNotify::decode(const uint8_t* buf, size_t len) noexcept {
    if (len < TerminalLeaveNotify::LEN) {
        return xERR;
    }

    uint32_t v32;
    ::memcpy(&v32, buf + 0, sizeof(v32));
    uid = u32_to_le(v32);

    return xOK;
}
