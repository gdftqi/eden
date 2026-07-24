#ifndef __ADAM_CORE_USER_HPP__
#define __ADAM_CORE_USER_HPP__


#include <memory>
#include <absl/container/flat_hash_map.h>


namespace adam::core {


class User {
    User(const User&) = delete;
    User& operator=(const User&) = delete;
    User(User&&) = delete;
    User& operator=(User&&) = delete;


public:
    typedef std::shared_ptr<User> Ptr;
    typedef absl::flat_hash_map<uint32_t, Ptr> Map;


    uint32_t
    id() const noexcept {
        return id_;
    }


private:
    uint32_t id_ { 0 };
}; // class User;


} // namespace adam::core


#endif // __ADAM_CORE_USER_HPP__