#ifndef __TYPHON_PACKAGE_HPP__
#define __TYPHON_PACKAGE_HPP__


#include "core/typhon.in.hpp"
#include <type_traits>


namespace typhon::core {


constexpr int PKG_MAX_LEN  = 65535; // 最大支持的消息长度
constexpr int PKG_META_LEN = 10;
constexpr int PKG_DATA_LEN = 14;
constexpr int PKG_HDR_LEN  = PKG_META_LEN + PKG_DATA_LEN;


#define PKID_PING       (100) ///< 心跳 PING
#define PKID_PONG       (101) ///< 心跳 PONG
#define PKID_REGIST_REQ (102) ///< 注册请求
#define PKID_REGIST_RSP (103) ///< 注册应答
#define PKID_KICK_REQ   (104) ///< 踢人请求
#define PKID_KICK_RSP   (105) ///< 踢人应答
#define PKID_KICK_NTF   (106) ///< 踢人通知
#define PKID_CUSTOM     (200) ///< 自定义消息ID


/**
 * @brief 客户端 / 网关 之间的应用层消息头
 */
struct Package {
    struct {
        uint32_t len       { 0 };
        uint32_t conv      { 0 };
        uint32_t src_addr  { 0 };
    } meta;
    

    struct {
        uint32_t id         { 0 };
        uint32_t src_id     { 0 };
        uint32_t dst_id     { 0 };
        uint32_t seq        { 0 };
        uint8_t  payload[];
    } data;


    uint32_t
    payload_length() const noexcept {
        return meta.len - PKG_HDR_LEN;
    }
}; // struct Package;


// RA token.go 线上明文布局(全小端): expire@0 conv@8 user_id@12 ip@16 cli_pk@20 sign@52
constexpr int ACCESS_TOKEN_LEN        = 116;   // 明文总长(线上; 与含对齐 padding 的 sizeof(AccessToken) 无关)
constexpr int ACCESS_TOKEN_SIGNED_LEN = 52;    // 被 ed25519 签名的前段(expire..cli_pk)

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
Package*
data_decode(const uint8_t* buf, size_t buflen) noexcept;


/**
 * @brief tcp 包只需要装 Package 全部
 */
int
frame_encode(uint8_t* buf, const Package* pk) noexcept;


/**
 * @brief tcp 包只需要解 Package 全部
 */
int
frame_decode(const uint8_t* buf, size_t avail, Package** pk) noexcept;


/**
 * @brief 从 RA 密封解出的 ACCESS_TOKEN_LEN(116)字节明文里, 显式逐字段小端解出 AccessToken。
 *        buf 至少 ACCESS_TOKEN_LEN 字节; 签名校验请对 buf 的前 ACCESS_TOKEN_SIGNED_LEN 字节做。
 */
void
token_decode(const uint8_t* buf, AccessToken* out) noexcept;


} // namespace typhon::core


#endif // __TYPHON_PACKAGE_HPP__