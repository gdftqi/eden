#ifndef __TYPHON_IN_H__
#define __TYPHON_IN_H__


#include <cassert>
#include <cstdint>
#include <fcntl.h>


namespace typhon {


constexpr int UDP_MTU = 1232;

typedef int SOCKET;
constexpr SOCKET INVALID_SOCKET = -1;


enum class State: uint8_t {
    Stopped,
    Stopping,
    Starting,
    Running,
};


inline int
set_nonblocking(int fd) noexcept {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }

    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
        return -1;
    }

    return 0;
}


} // namespace typhon;


#endif // __TYPHON_IN_H__