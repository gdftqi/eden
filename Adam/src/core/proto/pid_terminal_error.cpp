#include "core/proto/pid_terminal_error.hpp"
#include "core/adam.in.hpp"
#include "core/error.hpp"


void
adam::core::ErrorNotify::encode(uint8_t* buf, size_t len) noexcept {
    ASSERT(len >= ErrorNotify::LEN, "ErrorNotify::encode 缓冲区不足");

    uint32_t v32;

    v32 = u32_to_le(code);
    ::memcpy(buf + 0, &v32, sizeof(v32));

    v32 = u32_to_le(dst_id);
    ::memcpy(buf + 4, &v32, sizeof(v32));

    v32 = u32_to_le(pid);
    ::memcpy(buf + 8, &v32, sizeof(v32));
}


int
adam::core::ErrorNotify::decode(const uint8_t* buf, size_t len) noexcept {
    if (len < ErrorNotify::LEN) {
        return xERR;
    }

    uint32_t v32;

    ::memcpy(&v32, buf + 0, sizeof(v32));
    code = u32_to_le(v32);

    ::memcpy(&v32, buf + 4, sizeof(v32));
    dst_id = u32_to_le(v32);

    ::memcpy(&v32, buf + 8, sizeof(v32));
    pid = u32_to_le(v32);

    return xOK;
}
