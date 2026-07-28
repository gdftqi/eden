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


#define PID_PING           (100)             // 心跳 PING
#define PID_PONG           (101)             // 心跳 PONG
#define PID_REG_BKD_REQ    (102)             // 网关注册请求
#define PID_REG_BKD_RSP    (103)             // 网关注册应答
#define PID_TER_REG_REQ    PID_REG_BKD_REQ   // 终端注册请求
#define PID_TER_REG_RSP    PID_REG_BKD_RSP   // 终端注册应答
#define PID_TER_KIC_REQ    (104)             // 踢除终端请求
#define PID_TER_KIC_RSP    (105)             // 踢除终端应答
#define PID_TER_KIC_NTF    (106)             // 踢除终端通知
#define PID_TER_ENT_REQ    (107)             // 终端进入请求
#define PID_TER_ENT_RSP    (108)             // 终端进入应答
#define PID_TER_LEA_REQ    (109)             // 终端离开请求
#define PID_TER_LEA_RSP    (110)             // 终端离开应答
#define PID_TER_OFF_NTF    (111)             // 终端离开通知
#define PID_TER_BIND_NTF   (112)             // 终端绑定通知
#define PID_TER_UNBD_NTF   (113)             // 终端解绑通知
#define PID_CUSTOM         (200)             // 自定义消息ID


// ---------------------- 终端离开码(OFF/KIC 携带, 框架 0-99, 业务 100+) ----------------------
#define TER_CODE_LEAVE        (1)   // 客户端主动自离(TER_LEA_REQ)
#define TER_CODE_DISCONNECTED (2)   // 超时 / 网络断        -> 建议给宽限期
#define TER_CODE_KICKED       (3)   // 被踢(通用)           -> 建议给宽限期(马上有新会话接管)
#define TER_CODE_PROTO_ERR    (4)   // 协议违规             -> 立即清
#define TER_CODE_REJECTED     (5)   // 登记被业务拒绝        -> 立即清
#define TER_CODE_GW_LOST      (6)   // 网关连接断(后端本地判定, 一批同时消失)
#define TER_CODE_CUSTOM       (100) // 业务自定义起点: 顶号/封禁/同设备互斥…由 Noah 或业务后端定义


const char*
str_terminal_code(int code) noexcept;


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
        uint32_t pid        { 0 };  // Package ID 16 bits
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


} // namespace adam::core


#endif // __ADAM_PACKAGE_HPP__
