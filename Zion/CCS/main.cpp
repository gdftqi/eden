#include "adam.h"
#include "proto/ccs.pb.h"

#define PID_SINGLE_CHAT_REQ (PID_CUSTOM + 1)
#define PID_SINGLE_CHAT_RSP (PID_CUSTOM + 2)


class CCS: public adam::tcp::Server::IHook {
public:
    void
    on_init(adam::tcp::Server* s) noexcept override {
        xINFO("{} running on {}", s->host(), ::pthread_self());
    }


    void
    on_stopped(adam::tcp::Server* s) noexcept override {
        xINFO("{} stopped on {}", s->host(), ::pthread_self());
    }


    int
    on_sess_connected(adam::tcp::Session::Ptr sess) noexcept override {
        xINFO("{} connected", sess->remote_addr());
        return 0;
    }


    void
    on_sess_disconnected(adam::tcp::Session::Ptr sess) noexcept override {
        xINFO("{} disconnected", sess->remote_addr());
    }


    virtual void
    on_serv_registed(adam::tcp::Session::Ptr s) noexcept override {
        xINFO("{} has registed", s->id());
    }


    virtual void
    on_serv_unregisted(adam::tcp::Session::Ptr s) noexcept override {
        xINFO("{} has unregisted", s->id());
    }


    virtual int
    on_terminal_enter(adam::tcp::Terminal::Ptr t) noexcept override {
        xINFO("{} has enter", t->uid());
        return 0;
    }


    virtual void
    on_terminal_leave(adam::tcp::Terminal::Ptr t, uint32_t code) noexcept override {
        xINFO("{} has leave => code = {}", t->uid(), code);
    }
}; // class CCS;


/**
 * @brief 单聊
 */
static void
single_chat(adam::tcp::Server::Context& ctx, adam::core::Package* pk) noexcept {
    ccs::SingleChatReq req;
    if (adam::utils::pb_deserialize(&req, pk->payload(), pk->payload_length()) < 0) {
        xERROR("pb 解析失败");
        return;
    }

    // TODO
}


TCP_SERVER_MAIN(
    CCS, "config.yml",
    TCP_PK_HANDLE(PID_SINGLE_CHAT_REQ, single_chat)
)