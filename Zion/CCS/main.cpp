
#include "proto/ccs.pb.h"
#include "ccs.hpp"
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


static std::unique_ptr<adam::utils::Snowflake> snow_flake;


static inline uint64_t
make_chat_id(uint32_t a, uint32_t b) noexcept {
    return (uint64_t)std::min(a, b) << 32 | std::max(a, b);
}


static int64_t
next_seq(uint64_t chat_id) noexcept {
    static thread_local absl::flat_hash_map<uint64_t, int64_t> seqs;
    return ++seqs[chat_id];
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
single_chat_push(Server::Context& ctx, const ccs::SingleChatNtf* ntf) noexcept {
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
single_chat_push(Message* m) noexcept {
    auto* ntf = (ccs::SingleChatNtf*)m->arg3.ptr;

    auto t = m->reactor->get_terminal((uint32_t)m->arg2.v);
    if (t != nullptr) {
        Server::Context ctx(m->reactor, t.get());
        single_chat_push(ctx, ntf);
    }

    delete ntf;
}


static void
single_chat_db(Server::Context& ctx, const ccs::SingleChatReq* req) noexcept {
    uint32_t from_id = ctx.terminal->uid();
    uint32_t to_id   = (uint32_t)req->to_id();
    uint64_t chat_id = make_chat_id(from_id, to_id);

    // TODO: 接入 Scylla 后, 发号走号段、消息落 chat_message, 以下三个值由写库结果给出
    int64_t seq        = next_seq(chat_id);
    int64_t msg_id     = snow_flake->next();
    int64_t created_at = (int64_t)adam::utils::systime_ms();

    // ---- ACK from_id ----
    ccs::SingleChatRsp rsp;
    rsp.set_cli_id(req->cli_id());
    rsp.set_code(0);
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
            single_chat_push(to_ctx, &ntf);
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

    snow_flake = std::make_unique<adam::utils::Snowflake>(adam::tcp::Conf::instance()->server()->id & 0x3FFU);

    TCP_PK_HANDLE(PID_SINGLE_CHAT_REQ, single_chat)
    TCP_MSG_HANDLE(MID_SINGLE_CHAT_DB, single_chat_db)
    TCP_MSG_HANDLE(MID_SINGLE_CHAT_PUSH, single_chat_push)
    TCP_MSG_HANDLE(MID_SINGLE_CHAT_ACK, single_chat_ack)
)
