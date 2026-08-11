#include "adam.h"


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
 * @brief 单聊消息入口. 占位: 目前只是 echo 回发, 用来验证链路通.
 *        真实实现按 README 的流程走: peek chat_id -> 算属主 -> 投递/就地处理.
 */
static void
single_chat(adam::tcp::Terminal::Ptr t, adam::core::Package* pk) noexcept {
    // src/dst 对调, 原样回送给对端
    uint32_t src = pk->data.src_id;
    pk->data.src_id = pk->data.dst_id;
    pk->data.dst_id = src;

    int rc = t->sess()->send(*pk);
    if (rc < 0) {
        xWARN("single_chat send failed: uid ={}, rc={}", t->uid(), rc);
    }
}


TCP_SERVER_MAIN(
    CCS, "config.yml",
    TCP_PK_HANDLE(PID_CUSTOM + 1, single_chat)
)