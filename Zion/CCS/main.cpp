#include "ccs.hpp"
#include "db/scylla.hpp"
#include "proto/ccs.pb.h"
#include "utils/snow_flake.hpp"


using adam::tcp::Directory;
using adam::tcp::Message;
using adam::tcp::Server;
using adam::tcp::Terminal;


#define PID_SINGLE_CHAT_REQ (PID_CUSTOM + 1)
#define PID_SINGLE_CHAT_RSP (PID_CUSTOM + 2)
#define PID_SINGLE_CHAT_NTF (PID_CUSTOM + 3)


constexpr uint16_t MID_SINGLE_CHAT_DB   = 1;
constexpr uint16_t MID_SINGLE_CHAT_PUSH = 2;
constexpr uint16_t MID_SINGLE_CHAT_ACK  = 3;

// 分桶宽度: bucket = seq / 10000
constexpr int64_t SEQ_BUCKET_WIDTH = 10000;

// 号段步长: 32 起步, 每批发一次翻倍, 封顶 4096(必须小于桶宽)
constexpr int64_t SEQ_STEP_MIN  = 32;
constexpr int64_t SEQ_STEP_MAX  = 4096;
constexpr int     SEQ_CAS_RETRY = 8;


// 本线程持有的号段 [next, limit), 用完向 chat_seq 再批发一段
struct SeqRange {
    int64_t next  { 0 };
    int64_t limit { 0 };
    int64_t step  { SEQ_STEP_MIN };
};

// 落库失败: 连接还在, 这一条没存下, 客户端标红可重发
#define CERR_CHAT_DB_FAILED (PERR_REQ_CUSTOM + 1)


static std::unique_ptr<adam::utils::Snowflake> snow_flake;


static inline uint64_t
make_chat_id(uint32_t a, uint32_t b) noexcept {
    return (uint64_t)std::min(a, b) << 32 | std::max(a, b);
}


/**
 * @brief 会话内单调发号: 向 chat_seq 批发一段号码, 用完再批发
 *
 * chat_seq.allocated 记的是"已承诺(批发)到哪", 不是"已用到哪" --
 * 进程崩掉那一段就整段作废, 只会让 seq 出现空洞, 绝不会重号.
 * 只准 LWT 碰这张表: 普通写与 Paxos 混在同一分区会破坏线性化保证.
 *
 * @return 新号; 失败返回 -1
 */
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


/**
 * @brief 把 RSP 发给 ctx.terminal(必须在其属主 reactor 线程上)
 */
static void
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


static void
single_chat_ack(Message* m) noexcept {
    auto* rsp = (ccs::SingleChatRsp*)m->arg3.ptr;
    auto t = m->reactor->get_terminal((uint32_t)m->arg2.v);
    if (t != nullptr) {
        Server::Context ctx(m->reactor, t.get());
        single_chat_ack(ctx, rsp);
    }

    delete rsp;
}


/**
 * @brief 把 NTF 推给 ctx.terminal(必须在其属主 reactor 线程上)
 */
static void
single_chat_notify(Server::Context& ctx, const ccs::SingleChatNtf* ntf) noexcept {
    // content 可以很大, 用 PKG 级缓冲
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


static void
single_chat_notify(Message* m) noexcept {
    auto* ntf = (ccs::SingleChatNtf*)m->arg3.ptr;

    auto t = m->reactor->get_terminal((uint32_t)m->arg2.v);
    if (t != nullptr) {
        Server::Context ctx(m->reactor, t.get());
        single_chat_notify(ctx, ntf);
    }

    delete ntf;
}


static void
single_chat_db(Server::Context& ctx, const ccs::SingleChatReq* req) noexcept {
    using adam::db::Scylla;

    uint32_t from_id   = ctx.terminal->uid();
    uint32_t to_id     = (uint32_t)req->to_id();
    uint64_t chat_id   = make_chat_id(from_id, to_id);
    int64_t seq        = next_seq(chat_id);
    int64_t msg_id     = snow_flake->next();
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


static void
single_chat_db(Message* m) noexcept {
    auto* tp  = (Terminal::Ptr*)m->arg2.ptr;
    auto* req = (ccs::SingleChatReq*)m->arg3.ptr;

    Server::Context ctx(m->reactor, tp->get());
    single_chat_db(ctx, req);

    delete tp;
    delete req;
}


/**
 * @brief 单聊
 */
static void
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


TCP_SERVER_MAIN(
    CCS, "config.yml",

    ASSERT(adam::db::Scylla::instance()->init_from_file("config.yml") == 0, "初始化 scylla 失败");
    ASSERT(adam::db::Scylla::instance()->connect("eva") == 0, "连接 eva 空间失败");
    snow_flake = std::make_unique<adam::utils::Snowflake>(adam::tcp::Conf::instance()->server()->id & 0x3FFU);

    TCP_PK_HANDLE(PID_SINGLE_CHAT_REQ, single_chat)
    TCP_MSG_HANDLE(MID_SINGLE_CHAT_DB, single_chat_db)
    TCP_MSG_HANDLE(MID_SINGLE_CHAT_PUSH, single_chat_notify)
    TCP_MSG_HANDLE(MID_SINGLE_CHAT_ACK, single_chat_ack)
)
