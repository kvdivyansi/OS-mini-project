/* limits.c - FR7: demonstrate setrlimit(). Each demo runs in a CHILD process so
 * the limit never affects the parent.
 *   nofile : RLIMIT_NOFILE lowered -> open() eventually fails with EMFILE
 *   cpu    : RLIMIT_CPU lowered    -> kernel kills the busy child with SIGXCPU
 *   mem    : RLIMIT_AS lowered     -> a large malloc() fails (ENOMEM)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include "common.h"

/* _exit() does not flush stdio, so flush explicitly before leaving the child */
static void leave(int code) { fflush(stdout); _exit(code); }

static void child_nofile(void)
{
    struct rlimit rl = { 10, 10 };
    if (setrlimit(RLIMIT_NOFILE, &rl) != 0) { perror("setrlimit"); leave(1); }
    getrlimit(RLIMIT_NOFILE, &rl);
    printf("  [child] RLIMIT_NOFILE now soft=%lu hard=%lu\n", (unsigned long)rl.rlim_cur, (unsigned long)rl.rlim_max);
    int opened = 0, fds[64];
    for (;;) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0) {
            printf("  [child] open() #%d FAILED: errno=%d (%s)\n", opened + 1, errno, strerror(errno));
            break;
        }
        fds[opened++] = fd;
        if (opened >= 64) { printf("  [child] limit not enforced?!\n"); break; }
    }
    printf("  [child] successfully opened %d extra files (stdin/stdout/stderr already use 3 of the 10 slots)\n", opened);
    for (int i = 0; i < opened; i++) close(fds[i]);
    leave(errno == EMFILE ? 0 : 1);
}

static void child_cpu(void)
{
    struct rlimit rl = { 1, 2 };                    /* soft 1 s -> SIGXCPU, hard 2 s -> SIGKILL */
    if (setrlimit(RLIMIT_CPU, &rl) != 0) { perror("setrlimit"); leave(1); }
    printf("  [child] RLIMIT_CPU set to soft=1s hard=2s; spinning in a busy loop...\n");
    fflush(stdout);
    volatile unsigned long x = 0;
    for (;;) x++;
}

static void child_mem(void)
{
    struct rlimit rl = { 64UL << 20, 64UL << 20 };  /* 64 MiB of address space */
    if (setrlimit(RLIMIT_AS, &rl) != 0) { perror("setrlimit"); leave(1); }
    printf("  [child] RLIMIT_AS set to 64 MiB; requesting 256 MiB with malloc...\n");
    void *p = malloc(256UL << 20);
    if (p == NULL) { printf("  [child] malloc(256 MiB) returned NULL: errno=%d (%s)\n", errno, strerror(errno)); leave(0); }
    printf("  [child] malloc unexpectedly succeeded\n"); free(p); leave(1);
}

static int run_demo(const char *name, void (*child)(void), int expect_signal)
{
    printf("\n--- limit demo: %s ---\n", name);
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }
    if (pid == 0) { child(); leave(0); }
    int st;
    if (waitpid(pid, &st, 0) < 0) { perror("waitpid"); return 1; }
    int ok;
    if (WIFSIGNALED(st)) {
        printf("  [parent] child %d was terminated by signal %d (%s)\n", pid, WTERMSIG(st), strsignal(WTERMSIG(st)));
        ok = expect_signal && WTERMSIG(st) == SIGXCPU;
    } else {
        printf("  [parent] child %d exited with status %d\n", pid, WEXITSTATUS(st));
        ok = !expect_signal && WEXITSTATUS(st) == 0;
    }
    printf("  [parent] limit effect %s; parent process itself is unaffected\n", ok ? "OBSERVED as expected" : "NOT as expected");
    return ok ? 0 : 1;
}

int cmd_limits(int argc, char **argv)
{
    const char *mode = argc >= 2 ? argv[1] : "nofile";
    if (argc > 2) { fprintf(stderr, "error: usage: limits [nofile|cpu|mem|all]\n"); return 1; }
    printf("=== Resource Limits (setrlimit) ===\n");
    int rc = 0, ran = 0;
    if (!strcmp(mode, "nofile") || !strcmp(mode, "all")) { rc |= run_demo("RLIMIT_NOFILE (open files)", child_nofile, 0); ran = 1; }
    if (!strcmp(mode, "mem")    || !strcmp(mode, "all")) { rc |= run_demo("RLIMIT_AS (address space)", child_mem, 0); ran = 1; }
    if (!strcmp(mode, "cpu")    || !strcmp(mode, "all")) { rc |= run_demo("RLIMIT_CPU (CPU seconds)", child_cpu, 1); ran = 1; }
    if (!ran) { fprintf(stderr, "error: unknown limit '%s' (use nofile, cpu, mem or all)\n", mode); return 1; }
    printf("\nRESULT: %s\n", rc == 0 ? "PASS" : "FAIL");
    return rc;
}
