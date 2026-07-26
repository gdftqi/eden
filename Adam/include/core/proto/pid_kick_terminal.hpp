#ifndef __ADAM_CORE_PROTO_PID_KIC_TER_HPP__
#define __ADAM_CORE_PROTO_PID_KIC_TER_HPP__


#include <cinttypes>
#include <cstddef>


namespace adam::core {


struct KickTerminalReq {
    static constexpr int LEN = 8;


    void
    encode(uint8_t* buf, size_t len) noexcept;


    int
    decode(const uint8_t* buf, size_t len) noexcept;


    uint32_t uid;
    uint32_t code;


    KickTerminalReq() = default;
    ~KickTerminalReq() = default;
    KickTerminalReq(const KickTerminalReq&) = delete;
    KickTerminalReq& operator=(const KickTerminalReq&) = delete;
    KickTerminalReq(KickTerminalReq&&) = delete;
    KickTerminalReq& operator=(KickTerminalReq&&) = delete;
}; // struct KickTerminalReq;


struct KickTerminalRsp {
    static constexpr int LEN = 0;


    KickTerminalRsp() = default;
    ~KickTerminalRsp() = default;
    KickTerminalRsp(const KickTerminalRsp&) = delete;
    KickTerminalRsp& operator=(const KickTerminalRsp&) = delete;
    KickTerminalRsp(KickTerminalRsp&&) = delete;
    KickTerminalRsp& operator=(KickTerminalRsp&&) = delete;
}; // struct KickTerminalRsp;


struct KickTerminalNotify {
    static constexpr int LEN = 8;


    void
    encode(uint8_t* buf, size_t len) noexcept;


    int
    decode(const uint8_t* buf, size_t len) noexcept;


    uint32_t uid;
    uint32_t code;


    KickTerminalNotify() = default;
    ~KickTerminalNotify() = default;
    KickTerminalNotify(const KickTerminalNotify&) = delete;
    KickTerminalNotify& operator=(const KickTerminalNotify&) = delete;
    KickTerminalNotify(KickTerminalNotify&&) = delete;
    KickTerminalNotify& operator=(KickTerminalNotify&&) = delete;
}; // struct KickTerminalNotify;

    
} // namespace adam::core


#endif // __ADAM_CORE_PROTO_PID_KIC_TER_HPP__
