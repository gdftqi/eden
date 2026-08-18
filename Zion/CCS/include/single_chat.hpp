#ifndef __CCS_SINGLE_CHAT_HPP__
#define __CCS_SINGLE_CHAT_HPP__


#include "common.hpp"


// 号段步长: 32 起步, 每批发一次翻倍, 封顶 4096(必须小于桶宽)
constexpr int64_t SEQ_STEP_MIN  = 32;
constexpr int64_t SEQ_STEP_MAX  = 4096;
constexpr int     SEQ_CAS_RETRY = 8;


// 本线程持有的号段 [next, limit), 用完向 chat_seq 再批发一段
struct SeqRange {
    int64_t next  { 0 };
    int64_t limit { 0 };
    int64_t step  { SEQ_STEP_MIN };
}; // struct SeqRange;


void
single_chat_ack(Message* m) noexcept;


void
single_chat_ack(Server::Context& ctx, const ccs::SingleChatRsp* rsp) noexcept;


void
single_chat_notify(Server::Context& ctx, const ccs::SingleChatNtf* ntf) noexcept;


void
single_chat_notify(Message* m) noexcept;


void
single_chat_db(Server::Context& ctx, const ccs::SingleChatReq* req) noexcept;


void
single_chat_db(Message* m) noexcept;


void
single_chat(Server::Context& ctx, adam::core::Package* pk) noexcept;


#endif // __CCS_SINGLE_CHAT_HPP__
