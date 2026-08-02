#ifndef __ADAM_CORE_PROTO_PID_TER_ERROR_HPP__
#define __ADAM_CORE_PROTO_PID_TER_ERROR_HPP__


#include <cinttypes>
#include <cstddef>


namespace adam::core {


/**
 * @brief 请求处理失败通知. 网关 -> 客户端.
 *
 *        连接仍然有效, 只是这一条请求没能送达目标.
 *        dst_id/pid 回带原请求的寻址字段, 客户端据此定位是哪一条失败了.
 */
struct ErrorNotify {
    static constexpr size_t LEN = 12;


    void
    encode(uint8_t* buf, size_t len) noexcept;


    int
    decode(const uint8_t* buf, size_t len) noexcept;


    uint32_t code;    // PERR_REQ_*
    uint32_t dst_id;  // 原请求的目标服务 id
    uint32_t pid;     // 原请求的 PID


    ErrorNotify() = default;
    ~ErrorNotify() = default;
    ErrorNotify(const ErrorNotify&) = delete;
    ErrorNotify& operator=(const ErrorNotify&) = delete;
    ErrorNotify(ErrorNotify&&) = delete;
    ErrorNotify& operator=(ErrorNotify&&) = delete;
}; // struct ErrorNotify;


} // namespace adam::core


#endif // __ADAM_CORE_PROTO_PID_TER_ERROR_HPP__
