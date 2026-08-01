#ifndef __ADAM_UTILS_SYS_HPP__
#define __ADAM_UTILS_SYS_HPP__


#include <fcntl.h>
#include <unistd.h>
#include "utils/log.hpp"


namespace adam::utils {
    

inline bool
lock_pid(const char* fname) noexcept {
    int fd = ::open(fname, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        return false;
    }

    struct flock fl{};

    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    if (::fcntl(fd, F_OFD_SETLK, &fl) < 0) {
        ::close(fd);
        return false;
    }

    auto s = std::to_string(::getpid());
    ASSERT(::ftruncate(fd, 0) == 0, "ftruncate failed: errno = {}, errstr = {}", errno, ::strerror(errno));
    int n = ::write(fd, s.c_str(), s.size());
    if (n != (ssize_t)s.size()) {
        ::close(fd);
        return false;
    }

    return true;
}


inline uint64_t
systime_ms() noexcept{
    struct timespec ts{};
    if (::clock_gettime(CLOCK_MONOTONIC, &ts)) {
        return 0;
    }

    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}


} // namespace adam::utils


#endif // __ADAM_UTILS_SYS_HPP__