#include "core/proto/pid_terminal_bind.hpp"
#include "core/adam.in.hpp"
#include "core/error.hpp"


void
adam::core::TerminalBindNotify::encode(uint8_t* buf, size_t len) noexcept {
    ASSERT(len >= TerminalBindNotify::LEN, "TerminalBindNotify::encode 缓冲区不足");

    uint32_t v32;

    v32 = u32_to_le(uid);
    ::memcpy(buf +  0, &v32, sizeof(v32));
}


int
adam::core::TerminalBindNotify::decode(const uint8_t* buf, size_t len) noexcept {
    if (len < LEN) {
        return xERR;
    }

    // 布局(小端): uid@0
    uint32_t v32;

    ::memcpy(&v32, buf +  0, sizeof(v32));
    uid = u32_to_le(v32);
    return xOK;
}