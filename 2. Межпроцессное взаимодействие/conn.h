#pragma once
#include <cstddef>

class Conn {
public:
    virtual ~Conn() = default;
    virtual bool readAll(void *buf, size_t count, int timeout_ms) = 0;
    virtual bool writeAll(const void *buf, size_t count, int timeout_ms) = 0;
};
