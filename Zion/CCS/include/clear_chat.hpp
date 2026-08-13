#ifndef __CCS_CLEAR_CHAT_HPP__
#define __CCS_CLEAR_CHAT_HPP__


#include "common.hpp"


void
clear_chat_ack(Server::Context& ctx, const ccs::ClearChatRsp* rsp) noexcept;


void
clear_chat_ack(Message* m) noexcept;


void
clear_chat_notify(Server::Context& ctx, const ccs::ClearChatNtf* ntf) noexcept;


void
clear_chat_notify(Message* m) noexcept;


void
clear_chat_db(Server::Context& ctx, const ccs::ClearChatReq* req) noexcept;


void
clear_chat_db(Message* m) noexcept;


void
clear_chat(Server::Context& ctx, adam::core::Package* pk) noexcept;


#endif // __CCS_CLEAR_CHAT_HPP__
