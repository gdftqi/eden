#include "core/proto/pkid_kick_terminal.hpp"
#include "core/adam.in.hpp"
#include "core/error.hpp"


void
adam::core::KickTerminalReq::encode(uint8_t* buf, size_t len) noexcept {
    ASSERT(len >= (size_t)LEN, "KickTerminalReq::encode 缓冲区不足");

    uint32_t v32;

    v32 = u32_to_le(uid);
    ::memcpy(buf + 0, &v32, sizeof(v32));

    v32 = u32_to_le(code);
    ::memcpy(buf + 4, &v32, sizeof(v32));
}


int
adam::core::KickTerminalReq::decode(const uint8_t* buf, size_t len) noexcept {
    if (len < (size_t)LEN) {
        return xERR;
    }

    uint32_t v32;

    ::memcpy(&v32, buf + 0, sizeof(v32));
    uid = u32_to_le(v32);

    ::memcpy(&v32, buf + 4, sizeof(v32));
    code = u32_to_le(v32);

    return xOK;
}


void
adam::core::KickTerminalNotify::encode(uint8_t* buf, size_t len) noexcept {
    ASSERT(len >= (size_t)LEN, "KickTerminalNotify::encode 缓冲区不足");

    uint32_t v32;

    v32 = u32_to_le(uid);
    ::memcpy(buf + 0, &v32, sizeof(v32));

    v32 = u32_to_le(code);
    ::memcpy(buf + 4, &v32, sizeof(v32));
}


int
adam::core::KickTerminalNotify::decode(const uint8_t* buf, size_t len) noexcept {
    if (len < (size_t)LEN) {
        return xERR;
    }

    uint32_t v32;

    ::memcpy(&v32, buf + 0, sizeof(v32));
    uid = u32_to_le(v32);

    ::memcpy(&v32, buf + 4, sizeof(v32));
    code = u32_to_le(v32);

    return xOK;
}
