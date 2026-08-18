#ifndef __CCS_COMMON_HPP__
#define __CCS_COMMON_HPP__


#include "adam.h"
#include "db/scylla.hpp"
#include "proto/ccs.pb.h"


#define PID_SINGLE_CHAT_REQ     (PID_CUSTOM + 1)
#define PID_SINGLE_CHAT_RSP     (PID_CUSTOM + 2)
#define PID_SINGLE_CHAT_NTF     (PID_CUSTOM + 3)
#define PID_CLEAR_CHAT_REQ      (PID_CUSTOM + 4)
#define PID_CLEAR_CHAT_RSP      (PID_CUSTOM + 5)
#define PID_CLEAR_CHAT_NTF      (PID_CUSTOM + 6)
#define PID_DELETE_CHAT_REQ     (PID_CUSTOM + 7)
#define PID_DELETE_CHAT_RSP     (PID_CUSTOM + 8)
#define PID_DELETE_CHAT_NTF     (PID_CUSTOM + 9)
#define PID_CONFIRM_CHAT_REQ    (PID_CUSTOM + 10)
#define PID_CONFIRM_CHAT_RSP    (PID_CUSTOM + 11)
#define PID_CONFIRM_CHAT_NTF    (PID_CUSTOM + 12)
#define PID_GET_CHAT_CURSOR_REQ (PID_CUSTOM + 13)
#define PID_GET_CHAT_CURSOR_RSP (PID_CUSTOM + 14)
#define PID_GET_CHAT_MSG_REQ    (PID_CUSTOM + 15)
#define PID_GET_CHAT_MSG_RSP    (PID_CUSTOM + 16)


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

// 内容超长: 重试也没用, 客户端应该自己拦住(输入框 MaxLength=2048)
#define CERR_CHAT_TOO_LONG (PERR_REQ_CUSTOM + 2)

// content 的字节上限. 客户端限的是 2048 个字符
constexpr size_t CONTENT_MAX_BYTES = 8 * 1024;


using adam::tcp::Server;
using adam::tcp::Message;
using adam::tcp::Directory;
using adam::tcp::Terminal;


inline uint64_t
make_chat_id(uint32_t a, uint32_t b) noexcept {
    return (uint64_t)std::min(a, b) << 32 | std::max(a, b);
}


// 取列的小工具: 列不存在或是 NULL 都给零值.
inline int64_t
col_i64(const ::CassRow* row, const char* name) noexcept {
    int64_t v = 0;
    const ::CassValue* c = ::cass_row_get_column_by_name(row, name);
    if (c != nullptr) {
        ::cass_value_get_int64(c, &v);
    }
    return v;
}


inline int8_t
col_i8(const ::CassRow* row, const char* name) noexcept {
    int8_t v = 0;
    const ::CassValue* c = ::cass_row_get_column_by_name(row, name);
    if (c != nullptr) {
        ::cass_value_get_int8(c, &v);
    }
    return v;
}


inline bool
col_bool(const ::CassRow* row, const char* name) noexcept {
    cass_bool_t v = cass_false;
    const ::CassValue* c = ::cass_row_get_column_by_name(row, name);
    if (c != nullptr) {
        ::cass_value_get_bool(c, &v);
    }
    return v == cass_true;
}


inline std::string
col_text(const ::CassRow* row, const char* name) noexcept {
    const char* s = nullptr;
    size_t      n = 0;
    const ::CassValue* c = ::cass_row_get_column_by_name(row, name);
    if (c != nullptr && ::cass_value_get_string(c, &s, &n) == CASS_OK) {
        return std::string(s, n);
    }

    return std::string();
}


#endif // __CCS_COMMON_HPP__
