#ifndef __CCS_CONFIRM_CHAT_HPP__
#define __CCS_CONFIRM_CHAT_HPP__


#include "common.hpp"


void
confirm_chat_ack(Server::Context& ctx, const ccs::ConfirmChatRsp* rsp) noexcept;


void
confirm_chat_ack(Message* m) noexcept;


void
confirm_chat_notify(Server::Context& ctx, const ccs::ConfirmChatNtf* ntf) noexcept;


void
confirm_chat_notify(Message* m) noexcept;


void
confirm_chat_db(Server::Context& ctx, const ccs::ConfirmChatReq* req) noexcept;


void
confirm_chat_db(Message* m) noexcept;


void
confirm_chat(Server::Context& ctx, adam::core::Package* pk) noexcept;


#endif // __CCS_CONFIRM_CHAT_HPP__
