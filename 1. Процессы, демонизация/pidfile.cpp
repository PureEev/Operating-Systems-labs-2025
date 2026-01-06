#include "pidfile.h"
#include <sys/stat.h>
#include <fstream>
#include <unistd.h>
#include <signal.h>
#include <chrono>
#include <thread>
#include <syslog.h>
#include <filesystem>
#include <cstring>
#include <errno.h>

PidFile::PidFile(const fs::path& p) : path_(p) {}

std::optional<pid_t> PidFile::read_pid() const {
    if (!fs::exists(path_)) return std::nullopt;
    std::ifstream in(path_);
    if (!in.is_open()) return std::nullopt;
    pid_t pid = 0;
    in >> pid;
    if (pid <= 0) return std::nullopt;
    return pid;
}

bool PidFile::write_pid(pid_t pid) const {
    std::ofstream out(path_, std::ios::trunc);
    if (!out.is_open()) {
        syslog(LOG_ERR, "Unable to write pidfile %s", path_.c_str());
        return false;
    }
    out << pid << "\n";
    out.close();
    chmod(path_.c_str(), S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    return true;
}

void PidFile::remove_if_owned(pid_t pid) const {
    try {
        auto maybe = read_pid();
        if (maybe && *maybe == pid) {
            fs::remove(path_);
            syslog(LOG_INFO, "Removed pidfile %s", path_.c_str());
        } else {
            syslog(LOG_INFO, "Not removing pidfile %s (contents differ)", path_.c_str());
        }
    } catch (...) {
        syslog(LOG_WARNING, "Failed to remove pidfile %s", path_.c_str());
    }
}

void PidFile::check_and_terminate_existing() const {
    auto maybe = read_pid();
    if (!maybe) return;
    pid_t oldpid = *maybe;
    fs::path procdir = fs::path("/proc") / std::to_string(oldpid);
    if (fs::exists(procdir)) {
        syslog(LOG_INFO, "Found existing process pid=%d, sending SIGTERM", oldpid);
        if (kill(oldpid, SIGTERM) != 0) {
            syslog(LOG_ERR, "Failed to send SIGTERM to pid %d: %s", oldpid, strerror(errno));
            return;
        }
        // wait up to 5 seconds for process to disappear
        for (int i = 0; i < 5; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (!fs::exists(procdir)) break;
        }
        if (fs::exists(procdir)) {
            syslog(LOG_WARNING, "Process %d still exists after SIGTERM", oldpid);
        } else {
            syslog(LOG_INFO, "Previous process %d terminated", oldpid);
        }
    } else {
        syslog(LOG_INFO, "Pidfile %s refers to non-existing pid %d; will overwrite", path_.c_str(), oldpid);
    }
}
