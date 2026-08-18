#ifndef __CCS_DELETE_CHAT_HPP__
#define __CCS_DELETE_CHAT_HPP__


#include "common.hpp"


void
delete_chat_ack(Server::Context& ctx, const ccs::DeleteChatCursorRsp* rsp) noexcept;


void
delete_chat_ack(Message* m) noexcept;


void
delete_chat_notify(Server::Context& ctx, const ccs::DeleteChatCursorNtf* ntf) noexcept;


void
delete_chat_notify(Message* m) noexcept;


void
delete_chat_db(Server::Context& ctx, const ccs::DeleteChatCursorReq* req) noexcept;


void
delete_chat_db(Message* m) noexcept;


void
delete_chat(Server::Context& ctx, adam::core::Package* pk) noexcept;


#endif // __CCS_DELETE_CHAT_HPP__
