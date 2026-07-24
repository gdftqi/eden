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


    typedef std::shared_ptr<Terminal> Ptr;
    typedef absl::flat_hash_map<uint32_t, Ptr> Map;


    uint32_t
    uid() const noexcept {
        return uid_;
    }


private:
    uint32_t           uid_  { 0 };
    uint32_t           conv_ { 0 };
    ::sockaddr_storage addr_ {};
    Session::Ptr       sess_;
}; // class Terminal;


} // namespace adam::tcp


#endif // __ADAM_TCP_TERMINAL_HPP__