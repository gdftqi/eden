#include "clear_chat.hpp"


/**
 * @brief 把 RSP 发给 ctx.terminal(必须在其属主 reactor 线程上)
 */
void
clear_chat_ack(Server::Context& ctx, const ccs::ClearChatRsp* rsp) noexcept {
    uint8_t buf[64];
    int n = adam::utils::pb_serialize(buf, sizeof(buf), *rsp);
    if (n < 0) {
        xERROR("ClearChatRsp 序列化失败: uid = {}", ctx.terminal->uid());
        return;
    }

    if (ctx.terminal->send(PID_CLEAR_CHAT_RSP, buf, (uint32_t)n) < 0) {
        xWARN("清空 ACK 发送失败: uid = {}", ctx.terminal->uid());
    }
}


void
clear_chat_ack(Message* m) noexcept {
    auto* rsp = (ccs::ClearChatRsp*)m->arg3.ptr;

    auto t = m->reactor->get_terminal((uint32_t)m->arg2.v);
    if (t != nullptr) {
        Server::Context ctx(m->reactor, t.get());
        clear_chat_ack(ctx, rsp);
    }

    delete rsp;
}


/**
 * @brief 把 NTF 推给 ctx.terminal(必须在其属主 reactor 线程上)
 */
void
clear_chat_notify(Server::Context& ctx, const ccs::ClearChatNtf* ntf) noexcept {
    uint8_t buf[64];
    int n = adam::utils::pb_serialize(buf, sizeof(buf), *ntf);
    if (n < 0) {
        xERROR("ClearChatNtf 序列化失败: uid = {}", ctx.terminal->uid());
        return;
    }

    if (ctx.terminal->send(PID_CLEAR_CHAT_NTF, buf, (uint32_t)n) < 0) {
        xWARN("清空通知发送失败: uid = {}", ctx.terminal->uid());
    }
}


void
clear_chat_notify(Message* m) noexcept {
    auto* ntf = (ccs::ClearChatNtf*)m->arg3.ptr;

    auto t = m->reactor->get_terminal((uint32_t)m->arg2.v);
    if (t != nullptr) {
        Server::Context ctx(m->reactor, t.get());
        clear_chat_notify(ctx, ntf);
    }

    delete ntf;
}


void
clear_chat_db(Server::Context& ctx, const ccs::ClearChatReq* req) noexcept {
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
                return;   // 这个会话从没发过消息, allocated 保持 0
            }

            const ::CassValue* v = ::cass_row_get_column_by_name(row, "allocated");
            if (v != nullptr) {
                ::cass_value_get_int64(v, &allocated);
            }
        });

    for (int64_t b = 0; rc == 0 && b <= allocated / SEQ_BUCKET_WIDTH; ++b) {
        rc = Scylla::instance()->exec(
            "DELETE FROM eva.chat_message WHERE chat_id=? AND bucket=?",
            [&](::CassStatement* st) {
                ::cass_statement_bind_int64(st, 0, (int64_t)chat_id);
                ::cass_statement_bind_int32(st, 1, (int32_t)b);
            });
    }

    // ---- ACK from_id ----
    ccs::ClearChatRsp rsp;
    rsp.set_code(rc == 0 ? 0 : CERR_CHAT_DB_FAILED);
    rsp.set_chat_id(chat_id);

    auto* home = ctx.terminal->sess()->reactor();
    if (home == ctx.reactor) {
        clear_chat_ack(ctx, &rsp);
    } else {
        auto* m = new Message(Message::Type::MidHandle);
        m->arg1.v   = MID_CLEAR_CHAT_ACK;
        m->arg2.v   = from_id;
        m->arg3.ptr = new ccs::ClearChatRsp(rsp);
        home->notify(m);
    }

    // 没删成就别通知对端, 免得它把本地记录清了而服务端还留着
    if (rc != 0) {
        return;
    }

    // 自己和自己的会话没有对端可通知
    if (peer_id == from_id) {
        return;
    }

    // ---- 通知 peer_id 也清掉本地记录 ----
    ccs::ClearChatNtf ntf;
    ntf.set_chat_id(chat_id);

    auto* s = ctx.reactor->server();
    uint32_t idx = s->directory()->get(peer_id);
    if (idx == Directory::NPOS) {
        // 目标不在线: 它本地的旧记录要等拉历史时才对得上
        return;
    }

    if (idx == ctx.reactor->index()) {
        auto t = ctx.reactor->get_terminal(peer_id);
        if (t != nullptr) {
            Server::Context to_ctx(ctx.reactor, t.get());
            clear_chat_notify(to_ctx, &ntf);
        }
        return;
    }

    auto* m = new Message(Message::Type::MidHandle);
    m->arg1.v   = MID_CLEAR_CHAT_PUSH;
    m->arg2.v   = peer_id;
    m->arg3.ptr = new ccs::ClearChatNtf(std::move(ntf));
    s->reactor(idx)->notify(m);
}


void
clear_chat_db(Message* m) noexcept {
    auto* tp  = (Terminal::Ptr*)m->arg2.ptr;
    auto* req = (ccs::ClearChatReq*)m->arg3.ptr;

    Server::Context ctx(m->reactor, tp->get());
    clear_chat_db(ctx, req);

    delete tp;
    delete req;
}


void
clear_chat(Server::Context& ctx, adam::core::Package* pk) noexcept {
    ccs::ClearChatReq req;
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
        clear_chat_db(ctx, &req);
        return;
    }

    auto* m = new Message(Message::Type::MidHandle);
    m->arg1.v   = MID_CLEAR_CHAT_DB;
    m->arg2.ptr = new Terminal::Ptr(ctx.reactor->get_terminal(from_id));
    m->arg3.ptr = new ccs::ClearChatReq(std::move(req));
    reactor->notify(m);
}
