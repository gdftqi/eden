#ifndef __CCS_COMMON_HPP__
#define __CCS_COMMON_HPP__


#include "adam.h"
#include "db/scylla.hpp"
#include "proto/ccs.pb.h"


#define PID_SINGLE_CHAT_REQ  (PID_CUSTOM + 1)
#define PID_SINGLE_CHAT_RSP  (PID_CUSTOM + 2)
#define PID_SINGLE_CHAT_NTF  (PID_CUSTOM + 3)
#define PID_CLEAR_CHAT_REQ   (PID_CUSTOM + 4)
#define PID_CLEAR_CHAT_RSP   (PID_CUSTOM + 5)
#define PID_CLEAR_CHAT_NTF   (PID_CUSTOM + 6)
#define PID_DELETE_CHAT_REQ  (PID_CUSTOM + 7)
#define PID_DELETE_CHAT_RSP  (PID_CUSTOM + 8)
#define PID_DELETE_CHAT_NTF  (PID_CUSTOM + 9)
#define PID_CONFIRM_CHAT_REQ (PID_CUSTOM + 10)
#define PID_CONFIRM_CHAT_RSP (PID_CUSTOM + 11)
#define PID_CONFIRM_CHAT_NTF (PID_CUSTOM + 12)


constexpr uint16_t MID_SINGLE_CHAT_DB    = 1;
constexpr uint16_t MID_SINGLE_CHAT_PUSH  = 2;
constexpr uint16_t MID_SINGLE_CHAT_ACK   = 3;
constexpr uint16_t MID_CLEAR_CHAT_DB     = 4;
constexpr uint16_t MID_CLEAR_CHAT_PUSH   = 5;
constexpr uint16_t MID_CLEAR_CHAT_ACK    = 6;
constexpr uint16_t MID_DELETE_CHAT_DB    = 7;
constexpr uint16_t MID_DELETE_CHAT_PUSH  = 8;
constexpr uint16_t MID_DELETE_CHAT_ACK   = 9;
constexpr uint16_t MID_CONFIRM_CHAT_DB   = 10;
constexpr uint16_t MID_CONFIRM_CHAT_PUSH = 11;
constexpr uint16_t MID_CONFIRM_CHAT_ACK  = 12;


// 分桶宽度: bucket = seq / 10000. 发消息算桶号, 清空按桶删分区, 两边都用
constexpr int64_t SEQ_BUCKET_WIDTH = 10000;

// 操作 DB 失败: 连接还在, 这一条没成, 客户端可重试
#define CERR_CHAT_DB_FAILED (PERR_REQ_CUSTOM + 1)


using adam::tcp::Server;
using adam::tcp::Message;
using adam::tcp::Directory;
using adam::tcp::Terminal;


inline uint64_t
make_chat_id(uint32_t a, uint32_t b) noexcept {
    return (uint64_t)std::min(a, b) << 32 | std::max(a, b);
}


#endif // __CCS_COMMON_HPP__
