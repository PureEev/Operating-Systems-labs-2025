#pragma once

#include <filesystem>
#include <string>
#include <optional>
#include <sys/types.h>

namespace fs = std::filesystem;

class PidFile {
public:
    explicit PidFile(const fs::path& p);

    std::optional<pid_t> read_pid() const;

    bool write_pid(pid_t pid) const;

    void remove_if_owned(pid_t pid) const;

    void check_and_terminate_existing() const;

    fs::path path() const { return path_; }

private:
    fs::path path_;
};
