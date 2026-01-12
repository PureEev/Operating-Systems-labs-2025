#pragma once
#include "conn.h"
#include <string>

class ConnFifo : public Conn {
public:
    ConnFifo(const std::string &p2c_path, const std::string &c2p_path, bool roleHost);
    ~ConnFifo() override;
    bool readAll(void *buf, size_t count, int timeout_ms) override;
    bool writeAll(const void *buf, size_t count, int timeout_ms) override;
private:
    int rd_;
    int wr_;
    bool roleHost_;
    bool waitFd(int fd, short events, int timeout_ms);
};
