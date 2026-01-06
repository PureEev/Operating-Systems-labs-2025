#include "daemon.h"
#include "signal_handlers.h"

#include <syslog.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

CleanerDaemon& CleanerDaemon::instance() {
    static CleanerDaemon inst;
    return inst;
}

CleanerDaemon::CleanerDaemon()
    : interval_seconds_(60)
{}

static inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool CleanerDaemon::init_config_path(const std::string& cfg_path_arg) {
    try {
        fs::path p = fs::absolute(cfg_path_arg);
        config_path_ = p;
        config_dir_ = config_path_.parent_path();
    } catch (...) {
        syslog(LOG_ERR, "Failed to resolve config path '%s'", cfg_path_arg.c_str());
        return false;
    }
    syslog(LOG_INFO, "Using config file: %s", config_path_.c_str());
    return load_config();
}

bool CleanerDaemon::load_config() {
    std::ifstream in(config_path_);
    if (!in.is_open()) {
        syslog(LOG_ERR, "Cannot open config file: %s", config_path_.c_str());
        return false;
    }

    std::vector<std::pair<std::string,std::string>> new_entries;
    unsigned long new_interval = interval_seconds_;

    std::string line;
    size_t lineno = 0;
    while (std::getline(in, line)) {
        lineno++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string a, b;
        if (!(iss >> a)) continue;
        if (a == "interval") {
            if (!(iss >> b)) {
                syslog(LOG_WARNING, "Invalid interval at line %zu", lineno);
                continue;
            }
            try {
                unsigned long val = std::stoul(b);
                if (val == 0) val = 1;
                new_interval = val;
            } catch (...) {
                syslog(LOG_WARNING, "Bad interval value at line %zu", lineno);
            }
        } else {
            if (!(iss >> b)) {
                syslog(LOG_WARNING, "Invalid folder/ign entry at line %zu: '%s'", lineno, a.c_str());
                continue;
            }
            new_entries.emplace_back(a, b);
        }
    }

    {
        std::lock_guard<std::mutex> lg(mutex_);
        entries_.swap(new_entries);
        interval_seconds_ = new_interval;
    }

    syslog(LOG_INFO, "Config loaded: %zu entries, interval=%lu", entries_.size(), interval_seconds_);
    return true;
}

fs::path CleanerDaemon::get_config_dir() const { return config_dir_; }
fs::path CleanerDaemon::get_config_path() const { return config_path_; }
fs::path CleanerDaemon::pidfile_path() const { return config_dir_ / "daemon.pid"; }

void CleanerDaemon::do_one_pass() {
    std::vector<std::pair<std::string,std::string>> snapshot;
    {
        std::lock_guard<std::mutex> lg(mutex_);
        snapshot = entries_;
    }

    for (const auto &ent : snapshot) {
        const std::string &folder = ent.first;
        const std::string &ignfile = ent.second;
        try {
            fs::path folder_p(folder);
            if (!folder_p.is_absolute()) {
                folder_p = config_dir_ / folder_p;
            }
            if (!fs::exists(folder_p) || !fs::is_directory(folder_p)) {
                syslog(LOG_WARNING, "Folder does not exist or is not a directory: %s", folder_p.c_str());
                continue;
            }
            fs::path ign_p = folder_p / ignfile;
            if (fs::exists(ign_p)) {
                syslog(LOG_INFO, "Skip cleaning '%s' — ignore file '%s' present", folder_p.c_str(), ignfile.c_str());
                continue;
            }

            size_t removed_count = 0;
            for (auto &entry : fs::directory_iterator(folder_p)) {
                std::error_code ec;
                fs::remove_all(entry.path(), ec);
                if (ec) {
                    syslog(LOG_ERR, "Failed to remove '%s': %s", entry.path().c_str(), ec.message().c_str());
                } else {
                    removed_count++;
                }
            }
            syslog(LOG_INFO, "Cleaned %zu entries in '%s' (no %s)", removed_count, folder_p.c_str(), ignfile.c_str());
        } catch (const std::exception &ex) {
            syslog(LOG_ERR, "Exception while processing '%s': %s", ent.first.c_str(), ex.what());
        } catch (...) {
            syslog(LOG_ERR, "Unknown error while processing '%s'", ent.first.c_str());
        }
    }
}

void CleanerDaemon::run_forever() {
    syslog(LOG_INFO, "Daemon main loop started (pid=%d)", getpid());
    while (!sig::terminate.load()) {
        if (sig::reload.exchange(false)) {
            syslog(LOG_INFO, "SIGHUP received: reloading config");
            if (!load_config()) {
                syslog(LOG_ERR, "Failed to reload config");
            }
        }
        do_one_pass();

        for (unsigned long i = 0; i < interval_seconds_ && !sig::terminate.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    syslog(LOG_INFO, "Terminate flag set, exiting main loop");
}
