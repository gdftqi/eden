#include "core/proto/pid_terminal_bind.hpp"
#include "core/proto/pid_terminal_kick.hpp"
#include "core/proto/pid_terminal_unbind.hpp"
#include "tcp/config.hpp"
#include "tcp/reactor.hpp"


// 踢除本终端: 非属主线程则投消息给属主; 属主线程直接组包发出
// 注意这里不查 Reactor 的终端表 —— 被业务拒绝的终端尚未落户, 也必须能收到通知
void
adam::tcp::Terminal::kick(uint32_t code, Reactor* cur) noexcept {
    auto* owner = sess_->reactor();
    if (owner != cur) {
        owner->notify(new Message(Message::Type::TerminalKick, uid_, code));
        return;
    }

    // 踢人是单向指令(网关无需回执), 用 NTF 而非 REQ
    core::TerminalKickNotify ntf;
    ntf.uid  = uid_;
    ntf.code = code;

    uint8_t buf[core::TerminalKickNotify::LEN];
    ntf.encode(buf, sizeof(buf));

    notify(PID_TER_KIC_NTF, buf, sizeof(buf));
}


void
adam::tcp::Terminal::bind() noexcept {
    core::TerminalBindNotify ntf;
    ntf.uid = uid_;

    uint8_t buf[core::TerminalBindNotify::LEN];
    ntf.encode(buf, sizeof(buf));

    notify(PID_TER_BIND_NTF, buf, sizeof(buf));
}


void
adam::tcp::Terminal::unbind() noexcept {
    core::TerminalUnbindNotify ntf;
    ntf.uid = uid_;

    uint8_t buf[core::TerminalUnbindNotify::LEN];
    ntf.encode(buf, sizeof(buf));
    notify(PID_TER_UNBD_NTF, buf, sizeof(buf));
}


void
adam::tcp::Terminal::notify(uint16_t pid, const uint8_t* payload, uint32_t len) noexcept {
    constexpr uint32_t MAX_NTF_LEN = 16;
    ASSERT(len <= MAX_NTF_LEN, "send_ntf payload 过长: {}", len);

    alignas(core::Package) uint8_t buf[sizeof(core::Package) + MAX_NTF_LEN];
    auto* pk = (core::Package*)buf;

    pk->meta.len      = core::PKG_HDR_LEN + len;
    pk->meta.conv     = conv_;
    pk->meta.src_addr = 0;
    pk->data.pid      = pid;
    pk->data.src_id   = Conf::instance()->server()->id;
    pk->data.dst_id   = sess_->id();
    ::memcpy(pk->data.payload, payload, len);

    if (sess_->send(*pk) < 0) {
        xERROR("PID {} 发送失败: uid = {}", pid, uid_);
    }
}
