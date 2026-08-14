#include "chat_cursor.hpp"


void
get_chat_cursor(Server::Context& ctx, adam::core::Package*) noexcept {
    using adam::db::Scylla;

    uint32_t uid = ctx.terminal->uid();
    ccs::GetChatCursorRsp rsp;

    int rc = Scylla::instance()->exec(
        "SELECT chat_id,last_seq,last_time,read_seq,recv_seq,is_pinned,is_muted FROM eva.chat_cursor WHERE user_id=?",
        [&](::CassStatement* st) {
            ::cass_statement_bind_int64(st, 0, (int64_t)uid);
        },
        [&](const ::CassResult* rs) noexcept {
            ::CassIterator* it = ::cass_iterator_from_result(rs);
            if (it == nullptr) {
                return;
            }

            while (::cass_iterator_next(it)) {
                const ::CassRow* row = ::cass_iterator_get_row(it);

                auto* item = rsp.add_cursors();
                item->set_user_id(uid);
                item->set_chat_id((uint64_t)col_i64(row, "chat_id"));
                item->set_last_seq(col_i64(row, "last_seq"));
                item->set_last_time(col_i64(row, "last_time"));
                item->set_read_seq(col_i64(row, "read_seq"));
                item->set_recv_seq(col_i64(row, "recv_seq"));
                item->set_is_pinned(col_bool(row, "is_pinned"));
                item->set_is_muted(col_bool(row, "is_muted"));
            }

            ::cass_iterator_free(it);
        });

    if (rc != 0) {
        rsp.clear_cursors();
    }
    rsp.set_code(rc == 0 ? 0 : CERR_CHAT_DB_FAILED);

    static thread_local uint8_t buf[adam::core::PKG_MAX_LEN];
    int n = adam::utils::pb_serialize(buf, sizeof(buf), rsp);
    if (n < 0) {
        xERROR("GetChatCursorRsp 序列化失败: uid = {}, 会话数 = {}", uid, rsp.cursors_size());
        return;
    }

    if (ctx.terminal->send(PID_GET_CHAT_CURSOR_RSP, buf, (uint32_t)n) < 0) {
        xWARN("会话列表发送失败: uid = {}", uid);
    }
}
