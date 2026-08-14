#include "delete_chat.hpp"


/**
 * @brief 把 RSP 发给 ctx.terminal(必须在其属主 reactor 线程上)
 */
void
delete_chat_ack(Server::Context& ctx, const ccs::DeleteChatCursorRsp* rsp) noexcept {
    uint8_t buf[64];
    int n = adam::utils::pb_serialize(buf, sizeof(buf), *rsp);
    if (n < 0) {
        xERROR("DeleteChatCursorRsp 序列化失败: uid = {}", ctx.terminal->uid());
        return;
    }

    if (ctx.terminal->send(PID_DELETE_CHAT_RSP, buf, (uint32_t)n) < 0) {
        xWARN("删除会话 ACK 发送失败: uid = {}", ctx.terminal->uid());
    }
}


void
delete_chat_ack(Message* m) noexcept {
    auto* rsp = (ccs::DeleteChatCursorRsp*)m->arg3.ptr;

    auto t = m->reactor->get_terminal((uint32_t)m->arg2.v);
    if (t != nullptr) {
        Server::Context ctx(m->reactor, t.get());
        delete_chat_ack(ctx, rsp);
    }

    delete rsp;
}


/**
 * @brief 把 NTF 推给 ctx.terminal(必须在其属主 reactor 线程上)
 */
void
delete_chat_notify(Server::Context& ctx, const ccs::DeleteChatCursorNtf* ntf) noexcept {
    uint8_t buf[64];
    int n = adam::utils::pb_serialize(buf, sizeof(buf), *ntf);
    if (n < 0) {
        xERROR("DeleteChatCursorNtf 序列化失败: uid = {}", ctx.terminal->uid());
        return;
    }

    if (ctx.terminal->send(PID_DELETE_CHAT_NTF, buf, (uint32_t)n) < 0) {
        xWARN("删除会话通知发送失败: uid = {}", ctx.terminal->uid());
    }
}


void
delete_chat_notify(Message* m) noexcept {
    auto* ntf = (ccs::DeleteChatCursorNtf*)m->arg3.ptr;

    auto t = m->reactor->get_terminal((uint32_t)m->arg2.v);
    if (t != nullptr) {
        Server::Context ctx(m->reactor, t.get());
        delete_chat_notify(ctx, ntf);
    }

    delete ntf;
}


void
delete_chat_db(Server::Context& ctx, const ccs::DeleteChatCursorReq* req) noexcept {
    using adam::db::Scylla;

    uint32_t from_id = ctx.terminal->uid();
    uint32_t peer_id = (uint32_t)req->peer_id();
    uint64_t chat_id = make_chat_id(from_id, peer_id);

    int64_t allocated = 0;
    int rc = Scylla::instance()->exec(
        "SELECT allocated FROM eva.chat_seq WHERE chat_id=?",
        [&](::CassStatement* st) {
            ::cass_statement_bind_int64(st, 0, (int64_t)chat_id);
        },
        [&](const ::CassResult* rs) noexcept {
            const ::CassRow* row = ::cass_result_first_row(rs);
            if (row == nullptr) {
                return;
            }

            const ::CassValue* v = ::cass_row_get_column_by_name(row, "allocated");
            if (v != nullptr) {
                ::cass_value_get_int64(v, &allocated);
            }
        }
    );

    for (int64_t b = 0; rc == 0 && b <= allocated / SEQ_BUCKET_WIDTH; ++b) {
        rc = Scylla::instance()->exec(
            "DELETE FROM eva.chat_message WHERE chat_id=? AND bucket=?",
            [&](::CassStatement* st) {
                ::cass_statement_bind_int64(st, 0, (int64_t)chat_id);
                ::cass_statement_bind_int32(st, 1, (int32_t)b);
            });
    }

    for (uint32_t uid: { from_id, peer_id }) {
        if (rc != 0) {
            break;
        }

        rc = Scylla::instance()->exec(
            "DELETE FROM eva.chat_cursor WHERE user_id=? AND chat_id=?",
            [&](::CassStatement* st) {
                ::cass_statement_bind_int64(st, 0, (int64_t)uid);
                ::cass_statement_bind_int64(st, 1, (int64_t)chat_id);
            }
        );

        if (peer_id == from_id) {
            break;
        }
    }

    // ---- ACK from_id ----
    ccs::DeleteChatCursorRsp rsp;
    rsp.set_code(rc == 0 ? 0 : CERR_CHAT_DB_FAILED);
    rsp.set_chat_id(chat_id);

    auto* home = ctx.terminal->sess()->reactor();
    if (home == ctx.reactor) {
        delete_chat_ack(ctx, &rsp);
    } else {
        auto* m = new Message(Message::Type::MidHandle);
        m->arg1.v   = MID_DELETE_CHAT_ACK;
        m->arg2.v   = from_id;
        m->arg3.ptr = new ccs::DeleteChatCursorRsp(rsp);
        home->notify(m);
    }

    if (rc != 0) {
        return;
    }

    if (peer_id == from_id) {
        return;
    }

    // ---- 通知 peer_id 也删掉本地会话 ----
    ccs::DeleteChatCursorNtf ntf;
    ntf.set_chat_id(chat_id);

    auto* s = ctx.reactor->server();
    uint32_t idx = s->directory()->get(peer_id);
    if (idx == Directory::NPOS) {
        return;
    }

    if (idx == ctx.reactor->index()) {
        auto t = ctx.reactor->get_terminal(peer_id);
        if (t != nullptr) {
            Server::Context to_ctx(ctx.reactor, t.get());
            delete_chat_notify(to_ctx, &ntf);
        }
        return;
    }

    auto* m = new Message(Message::Type::MidHandle);
    m->arg1.v   = MID_DELETE_CHAT_PUSH;
    m->arg2.v   = peer_id;
    m->arg3.ptr = new ccs::DeleteChatCursorNtf(std::move(ntf));
    s->reactor(idx)->notify(m);
}


void
delete_chat_db(Message* m) noexcept {
    auto* tp  = (Terminal::Ptr*)m->arg2.ptr;
    auto* req = (ccs::DeleteChatCursorReq*)m->arg3.ptr;

    Server::Context ctx(m->reactor, tp->get());
    delete_chat_db(ctx, req);

    delete tp;
    delete req;
}


void
delete_chat(Server::Context& ctx, adam::core::Package* pk) noexcept {
    ccs::DeleteChatCursorReq req;
    if (adam::utils::pb_deserialize(&req, pk->payload(), pk->payload_length()) < 0) {
        xERROR("pb 解析失败: uid = {}", ctx.terminal->uid());
        ctx.terminate(PERR_TER_PROTO_ERR);
        return;
    }

    uint32_t from_id = ctx.terminal->uid();
    uint32_t peer_id = (uint32_t)req.peer_id();
    if (peer_id == 0) {
        xERROR("无效的 peer_id: from = {}", from_id);
        ctx.terminate(PERR_TER_PROTO_ERR);
        return;
    }

    auto* s = ctx.reactor->server();
    uint64_t chat_id = make_chat_id(from_id, peer_id);
    auto* reactor = s->reactor(chat_id % s->reactor_count());
    if (reactor == ctx.reactor) {
        delete_chat_db(ctx, &req);
        return;
    }

    auto* m = new Message(Message::Type::MidHandle);
    m->arg1.v   = MID_DELETE_CHAT_DB;
    m->arg2.ptr = new Terminal::Ptr(ctx.reactor->get_terminal(from_id));
    m->arg3.ptr = new ccs::DeleteChatCursorReq(std::move(req));
    reactor->notify(m);
}
