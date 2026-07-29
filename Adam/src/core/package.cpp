#include "core/package.hpp"
#include "utils/log.hpp"
#include <cstring>


const char*
adam::core::str_terminal_code(int code) noexcept {
    switch (code) {
        case TER_CODE_LEAVE:
            return "terminal activate leave";

        case TER_CODE_DISCONNECTED:
            return "terminal disconnected";

        case TER_CODE_KICKED:
            return "terminal has kicked";

        case TER_CODE_PROTO_ERR:
            return "invalid protocol data";

        case TER_CODE_REJECTED:
            return "router rejected";

        case TER_CODE_GW_LOST:
            return "gateway has losted";

        case TER_CODE_TAKEOVER:
            return "account logged in on another device";

        case TER_CODE_BANNED:
            return "account banned";

        case TER_CODE_ADMIN_KICK:
            return "kicked by administrator";

        default:
            return "unknown terminal code";
    }
}


int
adam::core::data_encode(uint8_t* buf, const Package* pk) noexcept {
    uint8_t* p = buf;
    uint16_t v16;
    uint32_t v32;

    v16 = u16_to_le((uint16_t)pk->data.pid);
    ::memcpy(p, &v16, sizeof(v16));
    p += sizeof(v16);

    v32 = u32_to_le(pk->data.src_id);
    ::memcpy(p, &v32, sizeof(v32));
    p += sizeof(v32);

    v32 = u32_to_le(pk->data.dst_id);
    ::memcpy(p, &v32, sizeof(v32));
    p += sizeof(v32);

    v32 = u32_to_le(pk->data.seq);
    ::memcpy(p, &v32, sizeof(v32));
    p += sizeof(v32);

    size_t plen = pk->payload_length();
    ::memcpy(p, pk->data.payload, plen);
    p += plen;

    return (int)(p - buf);
}


int
adam::core::data_decode(adam::core::Package* pk, const uint8_t* buf, size_t buflen) noexcept {
    ASSERT(pk != nullptr, "无效的入参 pk");

    if (buflen < (size_t)PKG_DATA_LEN) {
        return -1;
    }

    const size_t plen = buflen - PKG_DATA_LEN;

    pk->meta.len      = PKG_HDR_LEN + (uint32_t)plen;
    pk->meta.conv     = 0;
    pk->meta.src_addr = 0;

    const uint8_t* p = buf;
    uint16_t v16;
    uint32_t v32;

    ::memcpy(&v16, p, sizeof(v16));
    pk->data.pid = u16_to_le(v16);
    p += sizeof(v16);

    ::memcpy(&v32, p, sizeof(v32));
    pk->data.src_id = u32_to_le(v32);
    p += sizeof(v32);

    ::memcpy(&v32, p, sizeof(v32));
    pk->data.dst_id = u32_to_le(v32);
    p += sizeof(v32);

    ::memcpy(&v32, p, sizeof(v32));
    pk->data.seq = u32_to_le(v32);
    p += sizeof(v32);

    ::memcpy(pk->data.payload, p, plen);
    return 0;
}


int
adam::core::frame_encode(uint8_t* buf, const Package* pk) noexcept {
    uint8_t* p = buf;
    uint16_t v16;
    uint32_t v32;

    v16 = u16_to_le((uint16_t)(PKG_HDR_LEN + pk->payload_length()));
    ::memcpy(p, &v16, sizeof(v16));
    p += sizeof(v16);

    v32 = u32_to_le(pk->meta.conv);
    ::memcpy(p, &v32, sizeof(v32));
    p += sizeof(v32);

    v32 = u32_to_le(pk->meta.src_addr);
    ::memcpy(p, &v32, sizeof(v32));
    p += sizeof(v32);

    // data 段复用 data_encode
    p += data_encode(p, pk);

    return (int)(p - buf);
}


int
adam::core::frame_decode(Package* pk, const uint8_t* buf, size_t avail) noexcept {
    if (avail < (size_t)PKG_META_LEN) {
        // 连 len 都读不出 → 半包
        return 0;
    }

    // 帧首 2 字节小端 = 整帧长
    uint16_t flen;
    ::memcpy(&flen, buf, sizeof(flen));
    flen = u16_to_le(flen);

    if (flen < PKG_HDR_LEN) {
        return -1;  // 帧长非法
    }
    if (avail < flen) {
        return 0;   // 半包
    }

    // meta: len(2, 已用) conv(4) src_addr(4)
    const uint8_t* p = buf + sizeof(uint16_t);
    uint32_t conv;
    uint32_t src_addr;

    ::memcpy(&conv, p, sizeof(conv));
    p += sizeof(conv);

    ::memcpy(&src_addr, p, sizeof(src_addr));
    p += sizeof(src_addr);

    if (data_decode(pk, p, (size_t)flen - PKG_META_LEN) < 0) {
        return -1;
    }

    pk->meta.conv     = u32_to_le(conv);
    pk->meta.src_addr = u32_to_le(src_addr);

    // 消费的字节数
    return flen;
}
