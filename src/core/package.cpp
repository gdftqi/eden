#include "core/package.hpp"
#include "utils/log.hpp"
#include <cstring>


// 说明: 这些函数定义在 typhon::core 内, 函数体里 PKG_*/Package/u16_to_le 等按 typhon::core 就近解析。


int
typhon::core::data_encode(uint8_t* buf, const Package* pk) noexcept {
    uint8_t* p = buf;
    uint16_t v16;
    uint32_t v32;

    v16 = u16_to_le((uint16_t)pk->data.id);
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


typhon::core::Package*
typhon::core::data_decode(const uint8_t* buf, size_t buflen) noexcept {
    if (buflen < (size_t)PKG_DATA_LEN) {
        return nullptr;
    }

    const size_t plen = buflen - PKG_DATA_LEN;
    Package* pk = (Package*)::mi_malloc(sizeof(Package) + plen);
    ASSERT(pk != nullptr, "::mi_malloc 分配失败");

    pk->meta.len      = PKG_HDR_LEN + (uint32_t)plen;
    pk->meta.conv     = 0;
    pk->meta.src_addr = 0;

    const uint8_t* p = buf;
    uint16_t v16;
    uint32_t v32;

    ::memcpy(&v16, p, sizeof(v16));
    pk->data.id = u16_to_le(v16);
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
    return pk;
}


int
typhon::core::frame_encode(uint8_t* buf, const Package* pk) noexcept {
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
typhon::core::frame_decode(const uint8_t* buf, size_t avail, Package** pk) noexcept {
    if (avail < (size_t)PKG_META_LEN) {
        return 0;   // 连 len 都读不出 → 半包
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

    Package* pkg = data_decode(p, (size_t)flen - PKG_META_LEN);
    if (pkg == nullptr) {
        return -1;
    }

    pkg->meta.conv     = u32_to_le(conv);
    pkg->meta.src_addr = u32_to_le(src_addr);

    *pk = pkg;
    return flen;   // 消费的字节数
}


void
typhon::core::token_decode(const uint8_t* buf, AccessToken* out) noexcept {
    // RA token.go 布局(小端): expire@0 conv@8 user_id@12 ip@16 cli_pk@20 sign@52
    uint64_t e;
    uint32_t v;

    ::memcpy(&e, buf +  0, sizeof(e)); out->expire  = u64_to_le(e);
    ::memcpy(&v, buf +  8, sizeof(v)); out->conv    = u32_to_le(v);
    ::memcpy(&v, buf + 12, sizeof(v)); out->user_id = u32_to_le(v);
    ::memcpy(&v, buf + 16, sizeof(v)); out->ip      = u32_to_le(v);
    ::memcpy(out->cli_pk, buf + 20, sizeof(out->cli_pk));
    ::memcpy(out->sign,   buf + 52, sizeof(out->sign));
}
