#pragma once

#include <atomic>
#include <signal.h>
#include <string.h>

namespace sig {
    inline std::atomic<bool> reload{false};
    inline std::atomic<bool> terminate{false};

    static void handle_hup(int) __attribute__((used));
    static void handle_term(int) __attribute__((used));

    static void handle_hup(int) { reload.store(true); }
    static void handle_term(int) { terminate.store(true); }

    inline void install_handlers() {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = handle_hup;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGHUP, &sa, nullptr);

        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = handle_term;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);
    }
}
