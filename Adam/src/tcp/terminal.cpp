#include "tcp/reactor.hpp"


void
adam::tcp::Terminal::kick(uint32_t code, Reactor* cur) noexcept {
    auto* owner = sess_->reactor();
    if (owner == cur) {
        cur->kick_terminal(uid_, code);
        return;
    }

    owner->notify(new Message(Message::Type::TerminalKick, uid_, code));
}
