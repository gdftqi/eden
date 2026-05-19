#ifndef __TYPHON_UTILS_SYS_HPP__
#define __TYPHON_UTILS_SYS_HPP__


#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <string>


namespace typhon::utils {
    

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
    int n = ::write(fd, s.c_str(), s.size());
    if (n != (ssize_t)s.size()) {
        ::close(fd);
        return false;
    }

    return true;
}


} // namespace typhon::utils



#endif // __TYPHON_UTILS_SYS_HPP__