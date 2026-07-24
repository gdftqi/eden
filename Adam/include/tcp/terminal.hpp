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
    typedef std::shared_ptr<Terminal> Ptr;
    typedef absl::flat_hash_map<uint32_t, Ptr> Map;


    uint32_t
    id() const noexcept {
        return id_;
    }


private:
    uint32_t     id_    { 0 };
    Session::Ptr sess_;
}; // class Terminal;


} // namespace adam::tcp


#endif // __ADAM_TCP_TERMINAL_HPP__