#include "ccs.hpp"
#include "clear_chat.hpp"
#include "delete_chat.hpp"
#include "single_chat.hpp"
#include "confirm_chat.hpp"


TCP_SERVER_MAIN(
    CCS, "config.yml",

    ASSERT(adam::db::Scylla::instance()->init_from_file("config.yml") == 0, "初始化 scylla 失败");
    ASSERT(adam::db::Scylla::instance()->connect("eva") == 0, "连接 eva 空间失败");

    TCP_PK_HANDLE(PID_SINGLE_CHAT_REQ, single_chat)
    TCP_MSG_HANDLE(MID_SINGLE_CHAT_DB, single_chat_db)
    TCP_MSG_HANDLE(MID_SINGLE_CHAT_PUSH, single_chat_notify)
    TCP_MSG_HANDLE(MID_SINGLE_CHAT_ACK, single_chat_ack)

    TCP_PK_HANDLE(PID_CLEAR_CHAT_REQ, clear_chat)
    TCP_MSG_HANDLE(MID_CLEAR_CHAT_DB, clear_chat_db)
    TCP_MSG_HANDLE(MID_CLEAR_CHAT_PUSH, clear_chat_notify)
    TCP_MSG_HANDLE(MID_CLEAR_CHAT_ACK, clear_chat_ack)

    TCP_PK_HANDLE(PID_DELETE_CHAT_REQ, delete_chat)
    TCP_MSG_HANDLE(MID_DELETE_CHAT_DB, delete_chat_db)
    TCP_MSG_HANDLE(MID_DELETE_CHAT_PUSH, delete_chat_notify)
    TCP_MSG_HANDLE(MID_DELETE_CHAT_ACK, delete_chat_ack)

    TCP_PK_HANDLE(PID_CONFIRM_CHAT_REQ, confirm_chat)
    TCP_MSG_HANDLE(MID_CONFIRM_CHAT_DB, confirm_chat_db)
    TCP_MSG_HANDLE(MID_CONFIRM_CHAT_PUSH, confirm_chat_notify)
    TCP_MSG_HANDLE(MID_CONFIRM_CHAT_ACK, confirm_chat_ack)
)
