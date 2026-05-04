#ifndef __TYPHON_IN_HPP__
#define __TYPHON_IN_HPP__


#include <cassert>
#include <cstdint>
#include <fcntl.h>
#include <time.h>


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


inline ::time_t
systime_ms() noexcept{
    struct timespec ts{};
    if (::clock_gettime(CLOCK_MONOTONIC, &ts)) {
        return 0;
    }

    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}


} // namespace typhon;


#endif // __TYPHON_IN_HPP__