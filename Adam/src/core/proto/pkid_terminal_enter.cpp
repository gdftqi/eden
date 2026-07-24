#include "core/proto/pkid_terminal_enter.hpp"
#include "core/adam.in.hpp"
#include "core/error.hpp"



void
adam::core::TerminalInfo::encode(uint8_t* buf) noexcept {
    uint32_t v32;
    uint16_t v16;

    v32 = u32_to_le(uid);
    ::memcpy(buf +  0, &v32, sizeof(v32));

    v32 = u32_to_le(conv);
    ::memcpy(buf +  4, &v32, sizeof(v32));

    v32 = u32_to_le(ip);
    ::memcpy(buf +  8, &v32, sizeof(v32));

    v16 = u16_to_le((uint16_t)port);
    ::memcpy(buf + 12, &v16, sizeof(v16));

    v16 = u16_to_le((uint16_t)type);
    ::memcpy(buf + 14, &v16, sizeof(v16));
}


int
adam::core::TerminalInfo::decode(const uint8_t* buf, size_t len) noexcept {
    if (len < (size_t)LEN) {
        return xERR;
    }

    // 布局(小端): uid@0 conv@4 ip@8 port@12 type@14
    uint32_t v32;
    uint16_t v16;

    ::memcpy(&v32, buf +  0, sizeof(v32));
    uid = u32_to_le(v32);

    ::memcpy(&v32, buf +  4, sizeof(v32));
    conv = u32_to_le(v32);

    ::memcpy(&v32, buf + 8, sizeof(v32));
    ip = u32_to_le(v32);

    ::memcpy(&v16, buf + 12, sizeof(v16));
    port = u16_to_le(v16);

    ::memcpy(&v16, buf + 14, sizeof(v16));
    type = u16_to_le(v16);

    return xOK;
}
