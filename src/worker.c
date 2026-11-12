/* worker.c - FR6: fork() workers, collect status with waitpid(), classify result.
 * Failures here are SIMULATED (deliberate exit code / deliberate signal);
 * they are NOT real operating-system faults.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include "common.h"

typedef enum { W_NORMAL, W_EXIT_FAIL, W_SIGNAL_FAIL } Kind;

static const char *kind_name(Kind k)
{
    return k == W_NORMAL ? "normal" : k == W_EXIT_FAIL ? "simulated failure (exit code 42)"
                                                        : "simulated failure (terminated by SIGUSR1)";
}

static void worker_body(int idx, Kind k)           /* runs in the child; never returns */
{
    printf("  [worker %d pid=%d] started: %s\n", idx, getpid(), kind_name(k));
    fflush(stdout);
    volatile long s = 0;
    for (long i = 0; i < 30000000L; i++) s += i;    /* a little CPU work */
    if (k == W_NORMAL) {
        printf("  [worker %d pid=%d] work finished, exiting with status 0\n", idx, getpid());
        fflush(stdout); _exit(0);
    }
    if (k == W_EXIT_FAIL) {
        printf("  [worker %d pid=%d] *** SIMULATED FAILURE (intentional, not a real OS fault): exit(42) ***\n", idx, getpid());
        fflush(stdout); _exit(42);
    }
    printf("  [worker %d pid=%d] *** SIMULATED FAILURE (intentional, not a real OS fault): raising SIGUSR1 ***\n", idx, getpid());
    fflush(stdout);
    raise(SIGUSR1);                                 /* default action = terminate */
    _exit(99);                                      /* only reached if signal were ignored */
}

/* Decode a wait status; returns 1 if the outcome matches what the worker was told to do */
static int report(int idx, pid_t pid, Kind k, int st)
{
    int expected_ok;
    printf("  [parent] worker %d (pid %d) -> ", idx, pid);
    if (WIFEXITED(st)) {
        int c = WEXITSTATUS(st);
        if (c == 0) printf("COMPLETED NORMALLY (exit status 0)");
        else        printf("FAILURE DETECTED: exited with non-zero status %d", c);
        expected_ok = (k == W_NORMAL && c == 0) || (k == W_EXIT_FAIL && c == 42);
    } else if (WIFSIGNALED(st)) {
        printf("FAILURE DETECTED: killed by signal %d (%s)", WTERMSIG(st), strsignal(WTERMSIG(st)));
        expected_ok = (k == W_SIGNAL_FAIL && WTERMSIG(st) == SIGUSR1);
    } else { printf("unexpected wait status 0x%x", st); expected_ok = 0; }
    if (k != W_NORMAL) printf("  [this was a SIMULATED failure]");
    printf("\n");
    return expected_ok;
}

int cmd_worker(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "error: usage: worker normal|fail|signal|all\n"); return 1; }
    Kind kinds[3]; int n = 0;
    if (!strcmp(argv[1], "normal"))      kinds[n++] = W_NORMAL;
    else if (!strcmp(argv[1], "fail"))   kinds[n++] = W_EXIT_FAIL;
    else if (!strcmp(argv[1], "signal")) kinds[n++] = W_SIGNAL_FAIL;
    else if (!strcmp(argv[1], "all"))  { kinds[0] = W_NORMAL; kinds[1] = W_EXIT_FAIL; kinds[2] = W_SIGNAL_FAIL; n = 3; }
    else { fprintf(stderr, "error: unknown worker mode '%s' (use normal, fail, signal or all)\n", argv[1]); return 1; }

    printf("=== Worker Failure Detection: parent pid=%d launching %d worker(s) ===\n", getpid(), n);
    pid_t pids[3];
    for (int i = 0; i < n; i++) {
        fflush(stdout);                              /* avoid duplicated buffered output in the child */
        pids[i] = fork();
        if (pids[i] < 0) { perror("fork"); return 1; }
        if (pids[i] == 0) worker_body(i + 1, kinds[i]);
        printf("  [parent] created worker %d with pid %d\n", i + 1, pids[i]);
    }
    int good = 0;
    for (int i = 0; i < n; i++) {
        int st;
        pid_t r;
        while ((r = waitpid(pids[i], &st, 0)) < 0 && errno == EINTR) ;
        if (r < 0) { perror("waitpid"); continue; }
        good += report(i + 1, pids[i], kinds[i], st);
    }
    printf("RESULT: %d/%d workers ended exactly as expected - %s\n", good, n, good == n ? "PASS" : "FAIL");
    return good == n ? 0 : 1;
}
