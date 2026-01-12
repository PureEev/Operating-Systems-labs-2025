#include <bits/stdc++.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <poll.h>
#include <errno.h>

#include "conn.h"
#include "conn_fd.h"
#include "conn_pipe.h"
#include "conn_fifo.h"

using namespace std;

static void logf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    fprintf(stderr, "[%lld.%03lld] %s\n", (long long)ts.tv_sec, (long long)(ts.tv_nsec/1000000), buf);
    fflush(stderr);
}

static inline string p2c_name(int id){ return "/tmp/wolf_p2c_" + to_string(id); }
static inline string c2p_name(int id){ return "/tmp/wolf_c2p_" + to_string(id); }

int main(int argc, char** argv) {
    ios::sync_with_stdio(false);
    srand(time(nullptr) ^ getpid());

    int n = 7;
    string mode = "pipe"; 
    if (argc >= 2) n = atoi(argv[1]);
    if (argc >= 3) mode = argv[2];
    if (n <= 0) n = 7;
    if (mode != "pipe" && mode != "fifo" && mode != "sock") {
        cerr << "Usage: " << argv[0] << " [n_kids] [pipe|fifo|sock]\n";
        return 1;
    }
    logf("Host start: n=%d mode=%s", n, mode.c_str());

    sem_t *round_sem = (sem_t*)mmap(nullptr, sizeof(sem_t), PROT_READ|PROT_WRITE,
                                    MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    if (round_sem == MAP_FAILED) { perror("mmap"); return 1; }
    if (sem_init(round_sem, 1, 0) == -1) { perror("sem_init"); return 1; }

    struct EP { int parent_fd = -1; int child_fd = -1; int parent_rd = -1, parent_wr = -1, child_rd = -1, child_wr = -1; string p2c, c2p; };
    vector<EP> ep(n+1);

    if (mode == "pipe") {
        for (int i=1;i<=n;i++) {
            int p1[2], p2[2];
            if (pipe(p1) == -1 || pipe(p2) == -1) { perror("pipe"); return 1; }
            ep[i].parent_wr = p1[1]; ep[i].child_rd = p1[0];
            ep[i].child_wr = p2[1]; ep[i].parent_rd = p2[0];
        }
    } else if (mode == "sock") {
        for (int i=1;i<=n;i++) {
            int sv[2];
            if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) { perror("socketpair"); return 1; }
            ep[i].parent_fd = sv[0]; ep[i].child_fd = sv[1];
        }
    } else { 
        for (int i=1;i<=n;i++) {
            string a = p2c_name(i), b = c2p_name(i);
            ep[i].p2c = a; ep[i].c2p = b;
            unlink(a.c_str()); unlink(b.c_str());
            if (mkfifo(a.c_str(), 0666) == -1) {
                if (errno != EEXIST) perror("mkfifo p2c");
            }
            if (mkfifo(b.c_str(), 0666) == -1) {
                if (errno != EEXIST) perror("mkfifo c2p");
            }
        }
    }

    vector<int> alive(n+1,1);
    vector<pid_t> kids(n+1,0);
    vector<Conn*> hostConns(n+1, nullptr);

    for (int i=1;i<=n;i++) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return 1; }
        if (pid == 0) {
            srand(time(nullptr) ^ getpid() ^ i);
            Conn *conn = nullptr;
            if (mode == "sock") {
                if (ep[i].parent_fd >= 0) close(ep[i].parent_fd);
                conn = new ConnFD(ep[i].child_fd, true);
            } else if (mode == "pipe") {
                if (ep[i].parent_wr >= 0) close(ep[i].parent_wr);
                if (ep[i].parent_rd >= 0) close(ep[i].parent_rd);
                conn = new ConnPipe(ep[i].child_rd, ep[i].child_wr, true);
            } else { 
                conn = new ConnFifo(ep[i].p2c, ep[i].c2p, false);
            }

            logf("Child %d started pid=%d", i, getpid());
            bool alive_state = true;
            while (true) {
                struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += 5;
                if (sem_timedwait(round_sem, &ts) == -1) {
                    logf("Child %d: sem_timedwait error/timeout -> exiting", i);
                    break;
                }
                int wolf_num = 0;
                if (!conn->readAll(&wolf_num, sizeof(wolf_num), 5000)) {
                    logf("Child %d: read wolf failed -> exiting", i);
                    break;
                }
                int kid_num = alive_state ? (rand()%100 + 1) : (rand()%50 + 1);
                if (!conn->writeAll(&kid_num, sizeof(kid_num), 5000)) { logf("Child %d: write kid failed", i); break; }
                int status = 0;
                if (!conn->readAll(&status, sizeof(status), 5000)) { logf("Child %d: read status failed", i); break; }
                alive_state = (status == 1);
                logf("Child %d: wolf=%d my=%d status=%s", i, wolf_num, kid_num, alive_state ? "ALIVE" : "DEAD");
            }
            delete conn;
            _exit(0);
        } else {
            kids[i] = pid;
            if (mode == "sock") {
                if (ep[i].child_fd >= 0) close(ep[i].child_fd);
                hostConns[i] = new ConnFD(ep[i].parent_fd, true);
            } else if (mode == "pipe") {
                if (ep[i].child_rd >= 0) close(ep[i].child_rd);
                if (ep[i].child_wr >= 0) close(ep[i].child_wr);
                hostConns[i] = new ConnPipe(ep[i].parent_rd, ep[i].parent_wr, true);
            } else { 
                hostConns[i] = new ConnFifo(ep[i].p2c, ep[i].c2p, true);
            }
        }
    }

    int consecutive_all_dead = 0;
    int round = 0;
    while (true) {
        ++round;
        logf("=== ROUND %d ===", round);

        int wolf_num = -1;
        struct pollfd pfd; pfd.fd = STDIN_FILENO; pfd.events = POLLIN;
        int pr = poll(&pfd, 1, 3000);
        if (pr > 0 && (pfd.revents & POLLIN)) {
            if (scanf("%d", &wolf_num) != 1) {
                string tmp; getline(cin, tmp);
                wolf_num = -1;
            }
        }
        if (wolf_num < 1 || wolf_num > 100) {
            wolf_num = rand()%100 + 1;
            logf("No valid input -> wolf picks random %d", wolf_num);
        } else logf("Wolf picked %d", wolf_num);

        for (int i=0;i<n;i++) sem_post(round_sem);

        for (int i=1;i<=n;i++) {
            if (hostConns[i])
                hostConns[i]->writeAll(&wolf_num, sizeof(wolf_num), 5000);
        }

        vector<int> kid_num(n+1, -1);
        for (int i=1;i<=n;i++) {
            if (!hostConns[i]) continue;
            int v=0;
            if (!hostConns[i]->readAll(&v, sizeof(v), 5000)) {
                logf("Host: failed to read kid #%d", i);
                kid_num[i] = -1;
            } else kid_num[i] = v;
        }

        int live_count = 0;
        for (int i=1;i<=n;i++) {
            if (!hostConns[i]) continue;
            int status = 0;
            if (kid_num[i] == -1) {
                status = 0;
                alive[i] = 0;
            } else {
                if (alive[i]) {
                    double thr = 70.0 / n;
                    status = (abs(kid_num[i] - wolf_num) <= thr) ? 1 : 0;
                } else {
                    double thr = 20.0 / n;
                    status = (abs(kid_num[i] - wolf_num) <= thr) ? 1 : 0;
                }
                alive[i] = status;
            }
            if (alive[i]) ++live_count;
            hostConns[i]->writeAll(&status, sizeof(status), 5000);
        }

        int hidden = 0, caught = 0;
        for (int i=1;i<=n;i++) {
            if (kid_num[i] == -1) { ++caught; continue; }
            if (alive[i]) ++hidden; else ++caught;
        }
        logf("Round %d summary: wolf=%d live=%d hidden=%d caught=%d", round, wolf_num, live_count, hidden, caught);

        if (live_count == 0) ++consecutive_all_dead; else consecutive_all_dead = 0;
        if (consecutive_all_dead >= 2) { logf("All kids dead 2 rounds -> end"); break; }

        sleep(1);
    }

    for (int i=1;i<=n;i++) {
        if (kids[i] > 0) kill(kids[i], SIGTERM);
    }
    for (int i=1;i<=n;i++) {
        if (kids[i] > 0) waitpid(kids[i], nullptr, 0);
    }

    if (mode == "fifo") {
        for (int i=1;i<=n;i++) {
            unlink(p2c_name(i).c_str()); unlink(c2p_name(i).c_str());
        }
    }

    sem_destroy(round_sem);
    munmap(round_sem, sizeof(sem_t));

    for (int i=1;i<=n;i++) if (hostConns[i]) delete hostConns[i];

    logf("Host exiting");
    return 0;
}
