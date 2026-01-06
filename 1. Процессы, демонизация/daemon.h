#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <mutex>

namespace fs = std::filesystem;

class CleanerDaemon {
public:
    static CleanerDaemon& instance();

    CleanerDaemon(const CleanerDaemon&) = delete;
    CleanerDaemon& operator=(const CleanerDaemon&) = delete;

    bool init_config_path(const std::string& cfg_path_arg);

    bool load_config();

    void run_forever();

    fs::path pidfile_path() const;

    fs::path get_config_path() const;
    fs::path get_config_dir() const;

private:
    CleanerDaemon();
    ~CleanerDaemon() = default;

    void do_one_pass();

    fs::path config_path_;
    fs::path config_dir_;
    unsigned long interval_seconds_;
    std::vector<std::pair<std::string,std::string>> entries_; 
    mutable std::mutex mutex_;
};
