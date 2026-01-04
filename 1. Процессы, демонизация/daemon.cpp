#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>
#include <mutex>

namespace fs = std::filesystem;

static std::atomic<bool> g_reload_config(false);
static std::atomic<bool> g_terminate(false);

void handle_sighup(int) {
    g_reload_config.store(true);
}

void handle_sigterm(int) {
    g_terminate.store(true);
}


static inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

class CleanerDaemon {
public:

    static CleanerDaemon& instance() {
        static CleanerDaemon inst;
        return inst;
    }

    CleanerDaemon(const CleanerDaemon&) = delete;
    CleanerDaemon& operator=(const CleanerDaemon&) = delete;

    bool init_config_path(const std::string& cfg_path_arg) {
        try {
            fs::path p = fs::absolute(cfg_path_arg);
            config_path = p;
            config_dir = config_path.parent_path();
        }
        catch (...) {
            syslog(LOG_ERR, "Failed to resolve config path '%s'", cfg_path_arg.c_str());
            return false;
        }
        syslog(LOG_INFO, "Using config file: %s", config_path.c_str());
        return load_config(); 
    }

    bool load_config() {
        std::ifstream in(config_path);
        if (!in.is_open()) {
            syslog(LOG_ERR, "Cannot open config file: %s", config_path.c_str());
            return false;
        }

        std::vector<std::pair<std::string, std::string>> new_entries;
        unsigned long new_interval = interval_seconds; // keep old if not specified

        std::string line;
        size_t lineno = 0;
        while (std::getline(in, line)) {
            lineno++;
            line = trim(line);
            if (line.empty()) continue;
            if (line[0] == '#') continue;
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
                }
                catch (...) {
                    syslog(LOG_WARNING, "Bad interval value at line %zu", lineno);
                }
            }
            else {

                if (!(iss >> b)) {
                    syslog(LOG_WARNING, "Invalid entry at line %zu: '%s'", lineno, a.c_str());
                    continue;
                }
                new_entries.emplace_back(a, b);
            }
        }

        {
            std::lock_guard<std::mutex> lg(mutex);
            entries = std::move(new_entries);
            interval_seconds = new_interval;
        }
        syslog(LOG_INFO, "Config reloaded: %zu entries, interval=%lu", entries.size(), interval_seconds);
        return true;
    }

    void run_forever() {
        syslog(LOG_INFO, "Daemon main loop started (pid=%d)", getpid());
        while (!g_terminate.load()) {
            if (g_reload_config.exchange(false)) {
                syslog(LOG_INFO, "SIGHUP received: reloading config");
                if (!load_config()) {
                    syslog(LOG_ERR, "Failed to reload config");
                }
            }
            do_one_pass();

            for (unsigned long i = 0; i < interval_seconds && !g_terminate.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        syslog(LOG_INFO, "Termination requested, exiting main loop");
    }

    fs::path pidfile_path() const {
        return config_dir / "daemon.pid";
    }

    fs::path get_config_path() const { return config_path; }
    fs::path get_config_dir() const { return config_dir; }

private:
    CleanerDaemon() : interval_seconds(20) {} 

    void do_one_pass() {
        std::vector<std::pair<std::string, std::string>> snapshot;
        {
            std::lock_guard<std::mutex> lg(mutex);
            snapshot = entries;
        }

        for (auto& ent : snapshot) {
            const std::string& folder = ent.first;
            const std::string& ignfile = ent.second;
            try {
                fs::path folder_p(folder);
                if (!folder_p.is_absolute()) {

                    folder_p = config_dir / folder_p;
                }
                if (!fs::exists(folder_p) || !fs::is_directory(folder_p)) {
                    syslog(LOG_WARNING, "Folder does not exist or is not a directory: %s", folder_p.c_str());
                    continue;
                }
                fs::path ign_p = folder_p / ignfile;
                if (fs::exists(ign_p)) {
                    syslog(LOG_INFO, "Skip cleaning '%s'  ignore file '%s' present", folder_p.c_str(), ignfile.c_str());
                    continue;
                }

                size_t removed_count = 0;
                for (auto& entry : fs::directory_iterator(folder_p)) {
                    std::error_code ec;
                    fs::remove_all(entry.path(), ec);
                    if (ec) {
                        syslog(LOG_ERR, "Failed to remove '%s': %s", entry.path().c_str(), ec.message().c_str());
                    }
                    else {
                        removed_count++;
                    }
                }
                syslog(LOG_INFO, "Cleaned %zu entries in '%s' (no %s)", removed_count, folder_p.c_str(), ignfile.c_str());
            }
            catch (const std::exception& ex) {
                syslog(LOG_ERR, "Exception while processing '%s': %s", ent.first.c_str(), ex.what());
            }
            catch (...) {
                syslog(LOG_ERR, "Unknown error while processing '%s'", ent.first.c_str());
            }
        }
    }

    fs::path config_path;
    fs::path config_dir;
    std::vector<std::pair<std::string, std::string>> entries;
    unsigned long interval_seconds;
    mutable std::mutex mutex;
};

static void write_pidfile(const fs::path& pidfile) {
    std::ofstream out(pidfile);
    if (!out.is_open()) {
        syslog(LOG_ERR, "Unable to write pidfile %s", pidfile.c_str());
        return;
    }
    out << getpid() << "\n";
    out.close();

    chmod(pidfile.c_str(), S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
}

static bool check_and_kill_existing(const fs::path& pidfile) {
    if (!fs::exists(pidfile)) return true;
    std::ifstream in(pidfile);
    if (!in.is_open()) {
        syslog(LOG_WARNING, "Cannot open existing pidfile %s", pidfile.c_str());
        return true;
    }
    pid_t oldpid = 0;
    in >> oldpid;
    in.close();
    if (oldpid <= 0) return true;


    fs::path procdir = fs::path("/proc") / std::to_string(oldpid);
    if (fs::exists(procdir)) {
        syslog(LOG_INFO, "Found existing process pid=%d, sending SIGTERM", oldpid);
        if (kill(oldpid, SIGTERM) != 0) {
            syslog(LOG_ERR, "Failed to send SIGTERM to pid %d: %s", oldpid, strerror(errno));

        }
        else {

            for (int i = 0; i < 5; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                if (!fs::exists(procdir)) break;
            }
            if (fs::exists(procdir)) {
                syslog(LOG_WARNING, "Process %d still exists after SIGTERM", oldpid);

            }
            else {
                syslog(LOG_INFO, "Previous process %d terminated", oldpid);
            }
        }
    }
    else {
        syslog(LOG_INFO, "Pidfile %s refers to non-existing pid %d; will overwrite", pidfile.c_str(), oldpid);
    }
    return true;
}

static void daemonize_and_write_pid(const fs::path& pidfile) {
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "First fork failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {

        exit(EXIT_SUCCESS);
    }
    
    if (setsid() < 0) {
        syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Second fork failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    umask(0);
    if (chdir("/") != 0) {
        syslog(LOG_WARNING, "chdir('/') failed: %s", strerror(errno));
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    int fd0 = open("/dev/null", O_RDONLY);
    int fd1 = open("/dev/null", O_WRONLY);
    int fd2 = open("/dev/null", O_RDWR);
    (void)fd0; (void)fd1; (void)fd2;

    write_pidfile(pidfile);
}

int main(int argc, char* argv[]) {
    openlog("cleaner_daemon", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "Starting cleaner daemon (initializing)");

    std::string cfg_arg = "daemon.conf";
    if (argc >= 2) cfg_arg = argv[1];

    CleanerDaemon& daemon = CleanerDaemon::instance();
    if (!daemon.init_config_path(cfg_arg)) {
        syslog(LOG_ERR, "Initial config load failed, exiting");
        closelog();
        return EXIT_FAILURE;
    }

    fs::path pidfile = daemon.pidfile_path();


    check_and_kill_existing(pidfile);

    struct sigaction sa;
    sa.sa_handler = handle_sighup;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGHUP, &sa, nullptr);

    sa.sa_handler = handle_sigterm;
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);

    daemonize_and_write_pid(pidfile);

    struct sigaction sa_hup;
    sa_hup.sa_handler = handle_sighup;
    sigemptyset(&sa_hup.sa_mask);
    sa_hup.sa_flags = 0;
    sigaction(SIGHUP, &sa_hup, nullptr);

    struct sigaction sa_term;
    sa_term.sa_handler = handle_sigterm;
    sigemptyset(&sa_term.sa_mask);
    sa_term.sa_flags = 0;
    sigaction(SIGTERM, &sa_term, nullptr);
    sigaction(SIGINT, &sa_term, nullptr);

    syslog(LOG_INFO, "Daemonization complete, running");

    daemon.run_forever();

    fs::path pidfile_now = daemon.pidfile_path();
    try {
        if (fs::exists(pidfile_now)) {

            std::ifstream in(pidfile_now);
            pid_t pid_in_file = 0;
            if (in.is_open()) {
                in >> pid_in_file;
                in.close();
            }
            if (pid_in_file == getpid()) {
                fs::remove(pidfile_now);
            }
            else {
                syslog(LOG_INFO, "Not removing pidfile (pid in file %d != mypid %d)", pid_in_file, getpid());
            }
        }
    }
    catch (...) {
        syslog(LOG_WARNING, "Failed to remove pidfile");
    }

    syslog(LOG_INFO, "Daemon exiting");
    closelog();
    return EXIT_SUCCESS;
}
