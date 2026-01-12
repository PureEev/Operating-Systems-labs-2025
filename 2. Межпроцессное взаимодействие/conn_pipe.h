#pragma once
#include "conn.h"

class ConnPipe : public Conn {
public:
    ConnPipe(int read_fd, int write_fd, bool closeOnDestroy = true);
    ~ConnPipe() override;
    bool readAll(void *buf, size_t count, int timeout_ms) override;
    bool writeAll(const void *buf, size_t count, int timeout_ms) override;
private:
    int rd_;
    int wr_;
    bool closeOnDestroy_;
    bool waitFd(int fd, short events, int timeout_ms);
};
