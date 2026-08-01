#include "core/proto/pid_terminal_kick.hpp"
#include "core/adam.in.hpp"
#include "core/error.hpp"


void
adam::core::TerminalKickReq::encode(uint8_t* buf, size_t len) noexcept {
    ASSERT(len >= LEN, "KickTerminalReq::encode 缓冲区不足");

    uint32_t v32;

    v32 = u32_to_le(uid);
    ::memcpy(buf + 0, &v32, sizeof(v32));

    v32 = u32_to_le(code);
    ::memcpy(buf + 4, &v32, sizeof(v32));
}


int
adam::core::TerminalKickReq::decode(const uint8_t* buf, size_t len) noexcept {
    if (len < LEN) {
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
adam::core::TerminalKickNotify::encode(uint8_t* buf, size_t len) noexcept {
    ASSERT(len >= LEN, "KickTerminalNotify::encode 缓冲区不足");

    uint32_t v32;

    v32 = u32_to_le(uid);
    ::memcpy(buf + 0, &v32, sizeof(v32));

    v32 = u32_to_le(code);
    ::memcpy(buf + 4, &v32, sizeof(v32));
}


int
adam::core::TerminalKickNotify::decode(const uint8_t* buf, size_t len) noexcept {
    if (len < LEN) {
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
adam::core::TerminalKickRsp::encode(uint8_t* buf, size_t len) noexcept {
    ASSERT(len >= LEN, "TerminalKickRsp::encode 缓冲区不足");

    // 布局(小端): code@0
    uint32_t v32 = u32_to_le(code);
    ::memcpy(buf + 0, &v32, sizeof(v32));
}


int
adam::core::TerminalKickRsp::decode(const uint8_t* buf, size_t len) noexcept {
    if (len < LEN) {
        return xERR;
    }

    uint32_t v32;
    ::memcpy(&v32, buf + 0, sizeof(v32));
    code = u32_to_le(v32);

    return xOK;
}
