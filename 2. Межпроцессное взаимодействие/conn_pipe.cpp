#include "conn_pipe.h"
#include <unistd.h>
#include <poll.h>
#include <cerrno>

ConnPipe::ConnPipe(int read_fd, int write_fd, bool closeOnDestroy)
    : rd_(read_fd), wr_(write_fd), closeOnDestroy_(closeOnDestroy) {}

ConnPipe::~ConnPipe() {
    if (closeOnDestroy_) {
        if (rd_ >= 0) close(rd_);
        if (wr_ >= 0) close(wr_);
    }
}

bool ConnPipe::waitFd(int fd, short events, int timeout_ms) {
    struct pollfd p;
    p.fd = fd; p.events = events; p.revents = 0;
    int r = poll(&p, 1, timeout_ms);
    return (r > 0) && (p.revents & events);
}

bool ConnPipe::readAll(void *buf, size_t count, int timeout_ms) {
    if (rd_ < 0) return false;
    char *p = static_cast<char*>(buf);
    size_t rem = count;
    while (rem) {
        if (!waitFd(rd_, POLLIN, timeout_ms)) return false;
        ssize_t r = read(rd_, p, rem);
        if (r < 0) { if (errno == EINTR) continue; return false; }
        if (r == 0) return false;
        rem -= r; p += r;
    }
    return true;
}

bool ConnPipe::writeAll(const void *buf, size_t count, int timeout_ms) {
    if (wr_ < 0) return false;
    const char *p = static_cast<const char*>(buf);
    size_t rem = count;
    while (rem) {
        if (!waitFd(wr_, POLLOUT, timeout_ms)) return false;
        ssize_t w = write(wr_, p, rem);
        if (w < 0) { if (errno == EINTR) continue; return false; }
        rem -= w; p += w;
    }
    return true;
}