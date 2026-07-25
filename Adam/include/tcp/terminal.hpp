#ifndef __ADAM_TCP_TERMINAL_HPP__
#define __ADAM_TCP_TERMINAL_HPP__


#include "tcp/session.hpp"


namespace adam::tcp {


class Terminal {
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;
    Terminal(Terminal&&) = delete;
    Terminal& operator=(Terminal&&) = delete;


public:
    enum class Type {
        PC,     // 电脑
        Mobile, // 手机
        Pad,    // 平板
    };


    typedef std::shared_ptr<Terminal>          Ptr;
    typedef absl::flat_hash_map<uint32_t, Ptr> Map;


    uint32_t
    uid() const noexcept {
        return uid_;
    }


    uint32_t
    conv() const noexcept {
        return conv_;
    }


    const Session::Ptr&
    sess() const noexcept {
        return sess_;
    }


    static Ptr
    create(uint32_t uid, uint32_t conv, uint32_t ip, uint16_t port, Session::Ptr s) noexcept {
        return Ptr(new Terminal(uid, conv, ip, port, std::move(s)));
    }


private:
    explicit
    Terminal(uint32_t uid, uint32_t conv, uint32_t ip, uint16_t port, Session::Ptr s) noexcept
        : uid_(uid)
        , conv_(conv)
        , sess_(std::move(s)) {
        core::u32_to_sockaddr((sockaddr_in*)&addr_, ip, port);
    }


    uint32_t           uid_  { 0 };
    uint32_t           conv_ { 0 };
    ::sockaddr_storage addr_ {};
    Session::Ptr       sess_;
}; // class Terminal;


} // namespace adam::tcp


#endif // __ADAM_TCP_TERMINAL_HPP__
