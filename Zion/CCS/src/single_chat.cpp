#include "../include/single_chat.hpp"


static adam::utils::Snowflake&
snowflake() noexcept {
    static adam::utils::Snowflake sf(adam::tcp::Conf::instance()->server()->id & 0x3FFU);
    return sf;
}


static int64_t
next_seq(uint64_t chat_id) noexcept {
    using adam::db::Scylla;

    static thread_local absl::flat_hash_map<uint64_t, SeqRange> ranges;

    auto& r = ranges[chat_id];
    if (r.next < r.limit) {
        return r.next++;
    }

    bool applied = false;
    int64_t cur = 0;

    auto read = [&](const ::CassResult* rs) noexcept {
        const ::CassRow* row = ::cass_result_first_row(rs);
        if (row == nullptr) {
            return;
        }

        cass_bool_t b = cass_false;
        const ::CassValue* v = ::cass_row_get_column_by_name(row, "[applied]");
        if (v != nullptr) {
            ::cass_value_get_bool(v, &b);
        }
        applied = (b == cass_true);

        if (!applied) {
            v = ::cass_row_get_column_by_name(row, "allocated");
            if (v != nullptr) {
                ::cass_value_get_int64(v, &cur);
            }
        }
    };

    int64_t allocated = -1;

    for (int i = 0; i < SEQ_CAS_RETRY; ++i) {
        applied = false;
        cur     = 0;

        int rc;
        if (allocated < 0) {
            rc = Scylla::instance()->exec(
                "INSERT INTO eva.chat_seq(chat_id,allocated) VALUES(?,?) IF NOT EXISTS",
                [&](::CassStatement* st) {
                    ::cass_statement_bind_int64(st, 0, (int64_t)chat_id);
                    ::cass_statement_bind_int64(st, 1, r.step);
                }, read);

            if (rc == 0 && applied) {
                allocated = 0;
            }
        } else {
            rc = Scylla::instance()->exec(
                "UPDATE eva.chat_seq SET allocated=? WHERE chat_id=? IF allocated=?",
                [&](::CassStatement* st) {
                    ::cass_statement_bind_int64(st, 0, allocated + r.step);
                    ::cass_statement_bind_int64(st, 1, (int64_t)chat_id);
                    ::cass_statement_bind_int64(st, 2, allocated);
                }, read);
        }

        if (rc != 0) {
            return -1;
        }

        if (applied) {
            r.next  = allocated + 1;
            r.limit = allocated + r.step + 1;
            r.step  = std::min(r.step * 2, SEQ_STEP_MAX);
            return r.next++;
        }

        allocated = cur;
    }

    xERROR("发号失败, CAS 冲突超过 {} 次: chat_id = {}", SEQ_CAS_RETRY, chat_id);
    return -1;
}


void
single_chat_ack(Server::Context& ctx, const ccs::SingleChatRsp* rsp) noexcept {
    uint8_t buf[256];
    int n = adam::utils::pb_serialize(buf, sizeof(buf), *rsp);
    if (n < 0) {
        xERROR("SingleChatRsp 序列化失败: uid = {}", ctx.terminal->uid());
        return;
    }

    if (ctx.terminal->send(PID_SINGLE_CHAT_RSP, buf, (uint32_t)n) < 0) {
        xWARN("ACK 发送失败: uid = {}", ctx.terminal->uid());
    }
}


void
single_chat_ack(Message* m) noexcept {
    auto* rsp = (ccs::SingleChatRsp*)m->arg3.ptr;
    auto t = m->reactor->get_terminal((uint32_t)m->arg2.v);
    if (t != nullptr) {
        Server::Context ctx(m->reactor, t.get());
        single_chat_ack(ctx, rsp);
    }

    delete rsp;
}


void
single_chat_notify(Server::Context& ctx, const ccs::SingleChatNtf* ntf) noexcept {
    static thread_local uint8_t buf[adam::core::PKG_MAX_LEN];
    int n = adam::utils::pb_serialize(buf, sizeof(buf), *ntf);
    if (n < 0) {
        xERROR("SingleChatNtf 序列化失败: uid = {}", ctx.terminal->uid());
        return;
    }

    if (ctx.terminal->send(PID_SINGLE_CHAT_NTF, buf, (uint32_t)n) < 0) {
        xWARN("推送发送失败: uid = {}", ctx.terminal->uid());
    }
}


void
single_chat_notify(Message* m) noexcept {
    auto* ntf = (ccs::SingleChatNtf*)m->arg3.ptr;

    auto t = m->reactor->get_terminal((uint32_t)m->arg2.v);
    if (t != nullptr) {
        Server::Context ctx(m->reactor, t.get());
        single_chat_notify(ctx, ntf);
    }

    delete ntf;
}


void
single_chat_db(Server::Context& ctx, const ccs::SingleChatReq* req) noexcept {
    using adam::db::Scylla;

    uint32_t from_id   = ctx.terminal->uid();
    uint32_t to_id     = (uint32_t)req->to_id();
    uint64_t chat_id   = make_chat_id(from_id, to_id);
    int64_t seq        = next_seq(chat_id);
    int64_t msg_id     = snowflake().next();
    int64_t created_at = (int64_t)adam::utils::systime_ms();

    int rc = seq < 0 ? -1 : Scylla::instance()->exec("INSERT INTO eva.chat_message(chat_id,bucket,seq,msg_id,cli_id,from_id,to_id,msg_type,content,target_id,edit_seq,edited_at,is_revoked,created_at)VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
    [&](::CassStatement* st){
        ::cass_statement_bind_int64_by_name(st, "chat_id", (int64_t)chat_id);
        ::cass_statement_bind_int32_by_name(st, "bucket", (int32_t)(seq / SEQ_BUCKET_WIDTH));
        ::cass_statement_bind_int64_by_name(st, "seq", seq);
        ::cass_statement_bind_int64_by_name(st, "msg_id", msg_id);
        ::cass_statement_bind_int64_by_name(st, "cli_id", (int64_t)req->cli_id());
        ::cass_statement_bind_int64_by_name(st, "from_id", (int64_t)from_id);
        ::cass_statement_bind_int64_by_name(st, "to_id", (int64_t)to_id);
        ::cass_statement_bind_int8_by_name(st, "msg_type", (int8_t)req->type());
        ::cass_statement_bind_string_by_name(st, "content", req->content().c_str());
        ::cass_statement_bind_int64_by_name(st, "target_id", req->target_id());
        ::cass_statement_bind_int64_by_name(st, "edit_seq", 0);
        ::cass_statement_bind_int64_by_name(st, "edited_at", 0);
        ::cass_statement_bind_bool_by_name(st, "is_revoked", cass_false);
        ::cass_statement_bind_int64_by_name(st, "created_at", created_at);
    });

    for (uint32_t uid: { from_id, to_id }) {
        if (rc != 0) {
            break;
        }

        rc = Scylla::instance()->exec(
            "UPDATE eva.chat_cursor SET last_seq=?,last_time=? WHERE user_id=? AND chat_id=?",
            [&](::CassStatement* st) {
                ::cass_statement_bind_int64(st, 0, seq);
                ::cass_statement_bind_int64(st, 1, created_at);
                ::cass_statement_bind_int64(st, 2, (int64_t)uid);
                ::cass_statement_bind_int64(st, 3, (int64_t)chat_id);
            });

        if (to_id == from_id) {
            break;
        }
    }

    // ---- ACK from_id ----
    ccs::SingleChatRsp rsp;
    rsp.set_cli_id(req->cli_id());
    rsp.set_code(rc == 0 ? 0 : CERR_CHAT_DB_FAILED);
    rsp.set_seq(seq);
    rsp.set_msg_id(msg_id);
    rsp.set_created_at(created_at);

    auto* home = ctx.terminal->sess()->reactor();
    if (home == ctx.reactor) {
        single_chat_ack(ctx, &rsp);
    } else {
        auto* m = new Message(Message::Type::MidHandle);
        m->arg1.v   = MID_SINGLE_CHAT_ACK;
        m->arg2.v   = from_id;
        m->arg3.ptr = new ccs::SingleChatRsp(rsp);
        home->notify(m);
    }

    if (rc != 0) {
        return;
    }

    if (to_id == from_id) {
        return;
    }

    // ---- 推送 to_id ----
    ccs::SingleChatNtf ntf;
    ntf.set_cli_id(req->cli_id());
    ntf.set_seq(seq);
    ntf.set_msg_id(msg_id);
    ntf.set_type(req->type());
    ntf.set_content(req->content());
    ntf.set_created_at(created_at);
    ntf.set_from_id(from_id);

    auto* s = ctx.reactor->server();
    uint32_t idx = s->directory()->get(to_id);
    if (idx == Directory::NPOS) {
        // 目标不在线
        return;
    }

    if (idx == ctx.reactor->index()) {
        auto t = ctx.reactor->get_terminal(to_id);
        if (t != nullptr) {
            Server::Context to_ctx(ctx.reactor, t.get());
            single_chat_notify(to_ctx, &ntf);
        }
        return;
    }

    auto* m = new Message(Message::Type::MidHandle);
    m->arg1.v   = MID_SINGLE_CHAT_PUSH;
    m->arg2.v   = to_id;
    m->arg3.ptr = new ccs::SingleChatNtf(std::move(ntf));
    s->reactor(idx)->notify(m);
}


void
single_chat_db(Message* m) noexcept {
    auto* tp  = (Terminal::Ptr*)m->arg2.ptr;
    auto* req = (ccs::SingleChatReq*)m->arg3.ptr;

    Server::Context ctx(m->reactor, tp->get());
    single_chat_db(ctx, req);

    delete tp;
    delete req;
}


void
single_chat(Server::Context& ctx, adam::core::Package* pk) noexcept {
    ccs::SingleChatReq req;
    if (adam::utils::pb_deserialize(&req, pk->payload(), pk->payload_length()) < 0) {
        xERROR("pb 解析失败: uid = {}", ctx.terminal->uid());
        ctx.terminate(PERR_TER_PROTO_ERR);
        return;
    }

    uint32_t from_id = ctx.terminal->uid();
    uint32_t to_id   = (uint32_t)req.to_id();
    if (to_id == 0) {
        xERROR("无效的 to_id: from = {}", from_id);
        ctx.terminate(PERR_TER_PROTO_ERR);
        return;
    }

    if (req.content().size() > CONTENT_MAX_BYTES) {
        xWARN("content 超长: from = {}, bytes = {}", from_id, req.content().size());

        ccs::SingleChatRsp rsp;
        rsp.set_cli_id(req.cli_id());
        rsp.set_code(CERR_CHAT_TOO_LONG);
        single_chat_ack(ctx, &rsp);
        return;
    }

    auto* s = ctx.reactor->server();
    uint64_t chat_id = make_chat_id(from_id, to_id);
    auto* reactor = s->reactor(chat_id % s->reactor_count());
    if (reactor == ctx.reactor) {
        single_chat_db(ctx, &req);
        return;
    }

    auto* m = new Message(Message::Type::MidHandle);
    m->arg1.v   = MID_SINGLE_CHAT_DB;
    m->arg2.ptr = new Terminal::Ptr(ctx.reactor->get_terminal(from_id));
    m->arg3.ptr = new ccs::SingleChatReq(std::move(req));
    reactor->notify(m);
}