#include "daemon.h"
#include "pidfile.h"
#include "signal_handlers.h"

#include <syslog.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include <sys/stat.h>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <cstring>
#include <errno.h>

namespace fs = std::filesystem;

static void daemonize_and_write_pid(const fs::path& pidfile) {
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "First fork failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (pid > 0) exit(EXIT_SUCCESS);

    if (setsid() < 0) {
        syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Second fork failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (pid > 0) exit(EXIT_SUCCESS);

    umask(0);
    if (chdir("/") != 0) {
        syslog(LOG_WARNING, "chdir('/') failed: %s", strerror(errno));
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_RDWR);

    PidFile pf(pidfile);
    pf.write_pid(getpid());
}

int main(int argc, char* argv[]) {
    openlog("cleaner_daemon", LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "Starting cleaner daemon (initializing)");

    std::string cfg_arg = "daemon.conf";
    if (argc >= 2) cfg_arg = argv[1];

    CleanerDaemon &daemon = CleanerDaemon::instance();
    if (!daemon.init_config_path(cfg_arg)) {
        syslog(LOG_ERR, "Initial config load failed, exiting");
        closelog();
        return EXIT_FAILURE;
    }

    fs::path pidfile = daemon.pidfile_path();
    PidFile pf(pidfile);

    pf.check_and_terminate_existing();

    sig::install_handlers();

    daemonize_and_write_pid(pidfile);

    sig::install_handlers();

    syslog(LOG_INFO, "Daemonization complete, running");

    daemon.run_forever();

    pf.remove_if_owned(getpid());

    syslog(LOG_INFO, "Daemon exiting");
    closelog();
    return EXIT_SUCCESS;
}
