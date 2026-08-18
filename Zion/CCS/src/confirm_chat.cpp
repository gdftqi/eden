#include "confirm_chat.hpp"


/**
 * @brief 把 RSP 发给 ctx.terminal(必须在其属主 reactor 线程上)
 */
void
confirm_chat_ack(Server::Context& ctx, const ccs::ConfirmChatRsp* rsp) noexcept {
    uint8_t buf[64];
    int n = adam::utils::pb_serialize(buf, sizeof(buf), *rsp);
    if (n < 0) {
        xERROR("ConfirmChatRsp 序列化失败: uid = {}", ctx.terminal->uid());
        return;
    }

    if (ctx.terminal->send(PID_CONFIRM_CHAT_RSP, buf, (uint32_t)n) < 0) {
        xWARN("已读 ACK 发送失败: uid = {}", ctx.terminal->uid());
    }
}


void
confirm_chat_ack(Message* m) noexcept {
    auto* rsp = (ccs::ConfirmChatRsp*)m->arg3.ptr;

    auto t = m->reactor->get_terminal((uint32_t)m->arg2.v);
    if (t != nullptr) {
        Server::Context ctx(m->reactor, t.get());
        confirm_chat_ack(ctx, rsp);
    }

    delete rsp;
}


/**
 * @brief 把 NTF 推给 ctx.terminal(必须在其属主 reactor 线程上)
 */
void
confirm_chat_notify(Server::Context& ctx, const ccs::ConfirmChatNtf* ntf) noexcept {
    uint8_t buf[64];
    int n = adam::utils::pb_serialize(buf, sizeof(buf), *ntf);
    if (n < 0) {
        xERROR("ConfirmChatNtf 序列化失败: uid = {}", ctx.terminal->uid());
        return;
    }

    if (ctx.terminal->send(PID_CONFIRM_CHAT_NTF, buf, (uint32_t)n) < 0) {
        xWARN("已读通知发送失败: uid = {}", ctx.terminal->uid());
    }
}


void
confirm_chat_notify(Message* m) noexcept {
    auto* ntf = (ccs::ConfirmChatNtf*)m->arg3.ptr;

    auto t = m->reactor->get_terminal((uint32_t)m->arg2.v);
    if (t != nullptr) {
        Server::Context ctx(m->reactor, t.get());
        confirm_chat_notify(ctx, ntf);
    }

    delete ntf;
}


void
confirm_chat_db(Server::Context& ctx, const ccs::ConfirmChatReq* req) noexcept {
    using adam::db::Scylla;

    uint32_t from_id = ctx.terminal->uid();
    uint32_t peer_id = (uint32_t)req->peer_id();
    uint64_t chat_id = make_chat_id(from_id, peer_id);
    int64_t  seq     = req->seq();

    int rc = Scylla::instance()->exec(
        "UPDATE eva.chat_cursor SET read_seq=? WHERE user_id=? AND chat_id=?",
        [&](::CassStatement* st) {
            ::cass_statement_bind_int64(st, 0, seq);
            ::cass_statement_bind_int64(st, 1, (int64_t)from_id);
            ::cass_statement_bind_int64(st, 2, (int64_t)chat_id);
        }
    );

    if (rc == 0 && peer_id != from_id) {
        rc = Scylla::instance()->exec(
            "UPDATE eva.chat_cursor SET peer_read_seq=? WHERE user_id=? AND chat_id=?",
            [&](::CassStatement* st) {
                ::cass_statement_bind_int64(st, 0, seq);
                ::cass_statement_bind_int64(st, 1, (int64_t)peer_id);
                ::cass_statement_bind_int64(st, 2, (int64_t)chat_id);
            }
        );
    }

    // ---- ACK from_id ----
    ccs::ConfirmChatRsp rsp;
    rsp.set_code(rc == 0 ? 0 : CERR_CHAT_DB_FAILED);
    rsp.set_chat_id(chat_id);

    auto* home = ctx.terminal->sess()->reactor();
    if (home == ctx.reactor) {
        confirm_chat_ack(ctx, &rsp);
    } else {
        auto* m = new Message(Message::Type::MidHandle);
        m->arg1.v   = MID_CONFIRM_CHAT_ACK;
        m->arg2.v   = from_id;
        m->arg3.ptr = new ccs::ConfirmChatRsp(rsp);
        home->notify(m);
    }

    // 没写成就别通知对端, 免得对方把消息标成已读而水位没落库
    if (rc != 0) {
        return;
    }

    // 自己和自己的会话没有对端可通知
    if (peer_id == from_id) {
        return;
    }

    // ---- 通知 peer_id: 对方已读到 seq, 它据此把自己发的消息标已读 ----
    ccs::ConfirmChatNtf ntf;
    ntf.set_chat_id(chat_id);
    ntf.set_seq(seq);

    auto* s = ctx.reactor->server();
    uint32_t idx = s->directory()->get(peer_id);
    if (idx == Directory::NPOS) {
        // 目标不在线: 它下次同步时从自己的 chat_cursor 读对方 read_seq 补上
        return;
    }

    if (idx == ctx.reactor->index()) {
        auto t = ctx.reactor->get_terminal(peer_id);
        if (t != nullptr) {
            Server::Context to_ctx(ctx.reactor, t.get());
            confirm_chat_notify(to_ctx, &ntf);
        }
        return;
    }

    auto* m = new Message(Message::Type::MidHandle);
    m->arg1.v   = MID_CONFIRM_CHAT_PUSH;
    m->arg2.v   = peer_id;
    m->arg3.ptr = new ccs::ConfirmChatNtf(std::move(ntf));
    s->reactor(idx)->notify(m);
}


void
confirm_chat_db(Message* m) noexcept {
    auto* tp  = (Terminal::Ptr*)m->arg2.ptr;
    auto* req = (ccs::ConfirmChatReq*)m->arg3.ptr;

    Server::Context ctx(m->reactor, tp->get());
    confirm_chat_db(ctx, req);

    delete tp;
    delete req;
}


void
confirm_chat(Server::Context& ctx, adam::core::Package* pk) noexcept {
    ccs::ConfirmChatReq req;
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

    // 没读到任何东西就没什么好上报的. 不是协议违规, 静默丢掉即可
    if (req.seq() <= 0) {
        return;
    }

    auto* s = ctx.reactor->server();
    uint64_t chat_id = make_chat_id(from_id, peer_id);
    auto* reactor = s->reactor(chat_id % s->reactor_count());
    if (reactor == ctx.reactor) {
        confirm_chat_db(ctx, &req);
        return;
    }

    auto* m = new Message(Message::Type::MidHandle);
    m->arg1.v   = MID_CONFIRM_CHAT_DB;
    m->arg2.ptr = new Terminal::Ptr(ctx.reactor->get_terminal(from_id));
    m->arg3.ptr = new ccs::ConfirmChatReq(std::move(req));
    reactor->notify(m);
}
