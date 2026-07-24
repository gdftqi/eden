#ifndef __ADAM_PACKAGE_HPP__
#define __ADAM_PACKAGE_HPP__


#include "core/adam.in.hpp"
#include <type_traits>


namespace adam::core {


/**
 * @brief Package 最大支持的消息长度
 */
constexpr int PKG_MAX_LEN  = 65535;


/**
 * @brief Package::meta 长度, 只在 TCP 传输之间需要用到这个头
 */
constexpr int PKG_META_LEN = 10;


/**
 * @brief Package::data UDP 消息头长度
 */
constexpr int PKG_DATA_LEN = 14;


/**
 * @brief Package 消息头长度, TCP 之间传输之间的消息头
 */
constexpr int PKG_HDR_LEN = PKG_META_LEN + PKG_DATA_LEN;


#define PKID_PING           (100)             // 心跳 PING
#define PKID_PONG           (101)             // 心跳 PONG
#define PKID_REG_GW_REQ     (102)             // 网关注册请求
#define PKID_REG_GW_RSP     (103)             // 网关注册应答
#define PKID_REG_TER_REQ    PKID_REG_GW_REQ   // 终端注册请求
#define PKID_REG_TER_RSP    PKID_REG_GW_RSP   // 终端注册应答
#define PKID_KIC_TER_REQ    (104)             // 踢除终端请求
#define PKID_KIC_TER_RSP    (105)             // 踢除终端应答
#define PKID_KIC_TER_NTF    (106)             // 踢除终端通知
#define PKID_ON_TER_REQ     (107)             // 终端上线请求
#define PKID_ON_TER_RSP     (108)             // 终端上线应答
#define PKID_OFF_TER_NTF    (109)             // 终端下线通知
#define PKID_BIND_TER_NTF   (110)             // 终端绑定通知
#define PKID_UNBD_TER_NTF   (111)             // 终端解绑通知
#define PKID_CUSTOM         (200)             // 自定义消息ID


/**
 * @brief 客户端 / 网关 之间的应用层消息头
 */
struct Package {
    struct {
        uint32_t len       { 0 };   // Package总长度
        uint32_t conv      { 0 };   // kcp conv
        uint32_t src_addr  { 0 };   // 源地址
    } meta;
    

    struct {
        uint32_t id         { 0 };  // 消息ID, PKID, 16 bits
        uint32_t src_id     { 0 };  // 源ID, 发送者的ID
        uint32_t dst_id     { 0 };  // 目标ID, 接收者的ID
        uint32_t seq        { 0 };  // Package sequence, 用来确保消息的唯一性
        uint8_t  payload[];         // payload
    } data;


    uint32_t
    payload_length() const noexcept {
        return meta.len - PKG_HDR_LEN;
    }
}; // struct Package;


/**
 * @brief 明文总长(线上; 与含对齐 padding 的 sizeof(AccessToken) 无关)
 */
constexpr int ACCESS_TOKEN_LEN = 116;


/**
 * @brief 被 ed25519 签名的前段(expire..cli_pk)
 */
constexpr int ACCESS_TOKEN_SIGNED_LEN = 52;

/**
 * @brief 网关鉴权 token(仅内存持有)。线上字节由 token_decode 显式逐字段小端解析,
 *        不做内存覆盖, 所以不需要 1 字节对齐(和 Package 那套 codec 统一)。
 */
struct AccessToken {
    uint64_t expire;     // 过期时间戳
    uint32_t conv;       // 会话 ID
    uint32_t user_id;    // 用户 ID
    uint32_t ip;         // 登录 IP
    uint8_t  cli_pk[32]; // 客户端 X25519 公钥
    uint8_t  sign[64];   // 登录服 Ed25519 签名
}; // struct AccessToken;


// ---------------------------- 编解码 (全小端) ----------------------------


/**
 * @brief kcp 包只需要装 Package::data 部分
 */
int
data_encode(uint8_t* buf, const Package* pk) noexcept;


/**
 * @brief kcp 包只需要解包= Package::data 部分
 */
int
data_decode(Package* pk, const uint8_t* buf, size_t buflen) noexcept;


/**
 * @brief tcp 包只需要装 Package 全部
 */
int
frame_encode(uint8_t* buf, const Package* pk) noexcept;


/**
 * @brief tcp 包只需要解 Package 全部
 */
int
frame_decode(Package* pk, const uint8_t* buf, size_t avail) noexcept;


/**
 * @brief 从 RA 密封解出的 ACCESS_TOKEN_LEN(116)字节明文里, 显式逐字段小端解出 AccessToken。
 *        buf 至少 ACCESS_TOKEN_LEN 字节; 签名校验请对 buf 的前 ACCESS_TOKEN_SIGNED_LEN 字节做。
 */
void
token_decode(const uint8_t* buf, AccessToken* out) noexcept;


} // namespace adam::core


#endif // __ADAM_PACKAGE_HPP__