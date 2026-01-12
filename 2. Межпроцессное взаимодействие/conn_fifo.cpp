#include "conn_fifo.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <poll.h>
#include <cerrno>
#include <cstdio>

ConnFifo::ConnFifo(const std::string &p2c_path, const std::string &c2p_path, bool roleHost)
    : rd_(-1), wr_(-1), roleHost_(roleHost)
{
    if (roleHost_) {
        wr_ = open(p2c_path.c_str(), O_WRONLY);
        rd_ = open(c2p_path.c_str(), O_RDONLY);
    } else {
        rd_ = open(p2c_path.c_str(), O_RDONLY);
        wr_ = open(c2p_path.c_str(), O_WRONLY);
    }
    if (rd_ < 0 || wr_ < 0) {
        perror("ConnFifo open");
    }
}

ConnFifo::~ConnFifo() {
    if (rd_ >= 0) close(rd_);
    if (wr_ >= 0) close(wr_);
}

bool ConnFifo::waitFd(int fd, short events, int timeout_ms) {
    struct pollfd p; p.fd = fd; p.events = events; p.revents = 0;
    int r = poll(&p, 1, timeout_ms);
    return (r > 0) && (p.revents & events);
}

bool ConnFifo::readAll(void *buf, size_t count, int timeout_ms) {
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

bool ConnFifo::writeAll(const void *buf, size_t count, int timeout_ms) {
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