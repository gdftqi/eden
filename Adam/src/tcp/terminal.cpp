#include "core/proto/pid_terminal_bind.hpp"
#include "core/proto/pid_terminal_kick.hpp"
#include "core/proto/pid_terminal_unbind.hpp"
#include "tcp/config.hpp"
#include "tcp/reactor.hpp"


// 踢除本终端: 非属主线程则投消息给属主; 属主线程直接组包发出
// 注意这里不查 Reactor 的终端表 -- 被业务拒绝的终端尚未落户, 也必须能收到通知
void
adam::tcp::Terminal::kick(uint32_t code, Reactor* cur) noexcept {
    auto* owner = sess_->reactor();
    if (owner != cur) {
        auto* m = new Message(Message::Type::TerminalKick);
        m->arg1.v = uid_;
        m->arg2.v = code;
        owner->notify(m);
        return;
    }

    // 踢人是单向指令(网关无需回执), 用 NTF 而非 REQ
    core::TerminalKickNotify ntf;
    ntf.uid  = uid_;
    ntf.code = code;

    uint8_t buf[core::TerminalKickNotify::LEN];
    ntf.encode(buf, sizeof(buf));

    send(PID_TER_KIC_NTF, buf, sizeof(buf), sess_->id());
}


void
adam::tcp::Terminal::bind() noexcept {
    core::TerminalBindNotify ntf;
    ntf.uid = uid_;

    uint8_t buf[core::TerminalBindNotify::LEN];
    ntf.encode(buf, sizeof(buf));

    send(PID_TER_BIND_NTF, buf, sizeof(buf), sess_->id());
}


void
adam::tcp::Terminal::unbind() noexcept {
    core::TerminalUnbindNotify ntf;
    ntf.uid = uid_;

    uint8_t buf[core::TerminalUnbindNotify::LEN];
    ntf.encode(buf, sizeof(buf));
    send(PID_TER_UNBD_NTF, buf, sizeof(buf), sess_->id());
}


int
adam::tcp::Terminal::send(uint16_t pid, const uint8_t* payload, uint32_t len, uint32_t dst) noexcept {
    if (len > (uint32_t)(core::PKG_MAX_LEN - core::PKG_HDR_LEN)) {
        return xERR;
    }

    alignas(core::Package) static thread_local uint8_t buf[sizeof(core::Package) + (core::PKG_MAX_LEN - core::PKG_HDR_LEN)];
    auto* pk = (core::Package*)buf;

    pk->meta.len      = core::PKG_HDR_LEN + len;
    pk->meta.conv     = conv_;
    pk->meta.src_addr = 0;
    pk->data.pid      = pid;
    pk->data.src_id   = Conf::instance()->server()->id;
    pk->data.dst_id   = dst == 0 ? uid_ : dst;
    ::memcpy(pk->data.payload, payload, len);

    int rc = (int)sess_->send(*pk);
    if (rc < 0) {
        xERROR("PID {} 发送失败: uid = {}, rc = {}", pid, uid_, rc);
    }

    return rc;
}
