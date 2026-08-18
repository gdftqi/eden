#include "chat_message.hpp"


constexpr int32_t MSG_PAGE_MAX  = 50;
constexpr size_t  MSG_BYTES_MAX = 48 * 1024;


static bool
deleted_by_me(const ::CassRow* row, uint32_t uid) noexcept {
    const ::CassValue* v = ::cass_row_get_column_by_name(row, "deleted_by");
    if (v == nullptr || ::cass_value_is_null(v)) {
        return false;
    }

    ::CassIterator* it = ::cass_iterator_from_collection(v);
    if (it == nullptr) {
        return false;
    }

    bool hit = false;
    while (!hit && ::cass_iterator_next(it)) {
        int64_t who = 0;
        ::cass_value_get_int64(::cass_iterator_get_value(it), &who);
        hit = ((uint32_t)who == uid);
    }

    ::cass_iterator_free(it);
    return hit;
}


void
get_chat_message(Server::Context& ctx, adam::core::Package* pk) noexcept {
    using adam::db::Scylla;

    ccs::GetChatMessageReq req;
    if (adam::utils::pb_deserialize(&req, pk->payload(), pk->payload_length()) < 0) {
        xERROR("pb 解析失败: uid = {}", ctx.terminal->uid());
        ctx.terminate(PERR_TER_PROTO_ERR);
        return;
    }

    uint32_t uid     = ctx.terminal->uid();
    uint64_t chat_id = req.chat_id();

    if (uid != (uint32_t)(chat_id >> 32) && uid != (uint32_t)chat_id) {
        xERROR("越权拉取: uid = {}, chat_id = {}", uid, chat_id);
        ctx.terminate(PERR_TER_PROTO_ERR);
        return;
    }

    int32_t limit = req.limit();
    if (limit <= 0 || limit > MSG_PAGE_MAX) {
        limit = MSG_PAGE_MAX;
    }

    int rc     = 0;
    int64_t cursor = req.before_seq();

    if (cursor <= 0) {
        rc = Scylla::instance()->exec(
            "SELECT allocated FROM eva.chat_seq WHERE chat_id=?",
            [&](::CassStatement* st) {
                ::cass_statement_bind_int64(st, 0, (int64_t)chat_id);
            },
            [&](const ::CassResult* rs) noexcept {
                const ::CassRow* row = ::cass_result_first_row(rs);
                if (row != nullptr) {
                    cursor = col_i64(row, "allocated") + 1;
                }
            });
    }

    ccs::GetChatMessageRsp rsp;
    rsp.set_chat_id(chat_id);

    size_t bytes = 0;
    bool   more  = false;

    while (rc == 0 && !more && cursor > 0) {
        int64_t bucket = (cursor - 1) / SEQ_BUCKET_WIDTH;
        int64_t least  = cursor;
        int     got    = 0;

        rc = Scylla::instance()->exec(
            "SELECT seq,msg_id,cli_id,from_id,to_id,msg_type,content,target_id,"
            "edit_seq,edited_at,is_revoked,deleted_by,created_at "
            "FROM eva.chat_message WHERE chat_id=? AND bucket=? AND seq<? LIMIT ?",
            [&](::CassStatement* st) {
                ::cass_statement_bind_int64(st, 0, (int64_t)chat_id);
                ::cass_statement_bind_int32(st, 1, (int32_t)bucket);
                ::cass_statement_bind_int64(st, 2, cursor);
                ::cass_statement_bind_int32(st, 3, limit - rsp.messages_size() + 1);
            },
            [&](const ::CassResult* rs) noexcept {
                ::CassIterator* it = ::cass_iterator_from_result(rs);
                if (it == nullptr) {
                    return;
                }

                while (::cass_iterator_next(it)) {
                    const ::CassRow* row = ::cass_iterator_get_row(it);

                    ++got;
                    int64_t seq = col_i64(row, "seq");
                    least = seq;

                    if (rsp.messages_size() >= limit || bytes >= MSG_BYTES_MAX) {
                        more = true;
                        break;
                    }

                    if (deleted_by_me(row, uid)) {
                        continue;
                    }

                    auto content = col_text(row, "content");
                    bytes += content.size() + 64;

                    auto* m = rsp.add_messages();
                    m->set_chat_id(chat_id);
                    m->set_bucket((int32_t)bucket);
                    m->set_seq(seq);
                    m->set_msg_id(col_i64(row, "msg_id"));
                    m->set_cli_id((uint64_t)col_i64(row, "cli_id"));
                    m->set_from_id((uint32_t)col_i64(row, "from_id"));
                    m->set_to_id((uint32_t)col_i64(row, "to_id"));
                    m->set_msg_type((ccs::MessageType)col_i8(row, "msg_type"));
                    m->set_content(std::move(content));
                    m->set_target_id(col_i64(row, "target_id"));
                    m->set_edit_seq(col_i64(row, "edit_seq"));
                    m->set_edited_at(col_i64(row, "edited_at"));
                    m->set_is_revoked(col_bool(row, "is_revoked"));
                    m->set_created_at(col_i64(row, "created_at"));
                }

                ::cass_iterator_free(it);
            });

        if (rc != 0) {
            break;
        }

        cursor = (got == 0) ? bucket * SEQ_BUCKET_WIDTH : least;
    }

    if (rc != 0) {
        rsp.clear_messages();
        more = false;
    }
    rsp.set_code(rc == 0 ? 0 : CERR_CHAT_DB_FAILED);
    rsp.set_has_more(more);

    static thread_local uint8_t buf[adam::core::PKG_MAX_LEN];
    int n = adam::utils::pb_serialize(buf, sizeof(buf), rsp);
    if (n < 0) {
        xERROR("GetChatMessageRsp 序列化失败: uid = {}, 条数 = {}", uid, rsp.messages_size());
        return;
    }

    if (ctx.terminal->send(PID_GET_CHAT_MSG_RSP, buf, (uint32_t)n) < 0) {
        xWARN("聊天记录发送失败: uid = {}, chat_id = {}", uid, chat_id);
    }
}
