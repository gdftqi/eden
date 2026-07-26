#include "core/proto/pid_regist_terminal.hpp"
#include "core/adam.in.hpp"
#include "core/error.hpp"


int
adam::core::RegistTerminalReq::decode(const uint8_t* buf, size_t len) noexcept {
    if (len < (size_t)LEN) {
        return xERR;
    }

    // RA token.go 布局(小端): expire@0 conv@8 user_id@12 ip@16 cli_pk@20 sign@52
    uint64_t v64;
    uint32_t v32;

    ::memcpy(&v64, buf +  0, sizeof(v64));
    expire = u64_to_le(v64);

    ::memcpy(&v32, buf +  8, sizeof(v32));
    conv = u32_to_le(v32);

    ::memcpy(&v32, buf + 12, sizeof(v32));
    uid = u32_to_le(v32);

    ::memcpy(&v32, buf + 16, sizeof(v32));
    ip = u32_to_le(v32);
    
    ::memcpy(cli_pk, buf + 20, sizeof(cli_pk));
    ::memcpy(sign,   buf + 52, sizeof(sign));

    return xOK;
}


void
adam::core::RegistTerminalRsp::encode(uint8_t* buf, size_t len) noexcept {
    ASSERT(len >= (size_t)LEN, "RegistTerminalRsp::encode 缓冲区不足");

    ::memcpy(buf + 0, PK, sizeof(PK));
}


int
adam::core::RegistTerminalRsp::decode(const uint8_t* buf, size_t len) noexcept {
    if (len < (size_t)LEN) {
        return xERR;
    }

    ::memcpy(PK, buf + 0, sizeof(PK));

    return xOK;
}
