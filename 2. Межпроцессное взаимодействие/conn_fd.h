#pragma once
#include "conn.h"

class ConnFD : public Conn {
public:
    ConnFD(int fd, bool closeOnDestroy = true);
    ~ConnFD() override;
    bool readAll(void *buf, size_t count, int timeout_ms) override;
    bool writeAll(const void *buf, size_t count, int timeout_ms) override;
private:
    int fd_;
    bool closeOnDestroy_;
    bool waitFd(int fd, short events, int timeout_ms);
};
