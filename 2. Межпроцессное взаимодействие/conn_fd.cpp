#include "conn_fd.h"
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <cstdio>

ConnFD::ConnFD(int fd, bool closeOnDestroy) : fd_(fd), closeOnDestroy_(closeOnDestroy) {}
ConnFD::~ConnFD() {
    if (closeOnDestroy_ && fd_ >= 0) close(fd_);
}

bool ConnFD::waitFd(int fd, short events, int timeout_ms) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = events;
    pfd.revents = 0;
    int r = poll(&pfd, 1, timeout_ms);
    return (r > 0) && (pfd.revents & events);
}

bool ConnFD::readAll(void *buf, size_t count, int timeout_ms) {
    if (fd_ < 0) return false;
    char *p = static_cast<char*>(buf);
    size_t rem = count;
    while (rem) {
        if (!waitFd(fd_, POLLIN, timeout_ms)) return false;
        ssize_t r = read(fd_, p, rem);
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (r == 0) return false;
        rem -= r;
        p += r;
    }
    return true;
}

bool ConnFD::writeAll(const void *buf, size_t count, int timeout_ms) {
    if (fd_ < 0) return false;
    const char *p = static_cast<const char*>(buf);
    size_t rem = count;
    while (rem) {
        if (!waitFd(fd_, POLLOUT, timeout_ms)) return false;
        ssize_t w = write(fd_, p, rem);
        if (w < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        rem -= w;
        p += w;
    }
    return true;
}