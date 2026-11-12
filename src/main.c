/* main.c - FR8: command-line interface, dispatch, error handling, integrated demo */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "common.h"

void print_usage(FILE *out)
{
    fprintf(out,
        "CampusCompute - intelligent job scheduling and resource management prototype\n\n"
        "Usage: ./campuscompute <command> [arguments]\n\n"
        "Commands:\n"
        "  help                                 show this message\n"
        "  sched fcfs|rr|prio <jobs.txt> [q]    run one CPU scheduling policy (q = RR quantum, default 3)\n"
        "  sched compare <jobs.txt> [q]         compare all three policies on the same job set\n"
        "  logger [producers] [consumers] [n]   pthread producer-consumer logger (defaults 2 2 8)\n"
        "  ipc [keep]                           shared memory + named semaphore demo ('keep' skips cleanup)\n"
        "  banker <state-file>                  Banker's safety check (exit 0 safe, 2 unsafe)\n"
        "  worker normal|fail|signal|all        fork()/waitpid() worker status detection\n"
        "  limits [nofile|cpu|mem|all]          setrlimit() demonstrations (default nofile)\n"
        "  demo                                 integrated run of every feature + summary\n"
        "  cleanup [--logs]                     remove ONLY the IPC objects (and optionally the log) made by this program\n\n"
        "Job file format (one job per line, '#' = comment):  id arrival burst priority\n");
}

int parse_int(const char *s, int *out)
{
    char *end; errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || end == s || *end != '\0' || v < -2147483647L || v > 2147483647L) return -1;
    *out = (int)v; return 0;
}

int ensure_log_dir(void)
{
    if (mkdir(LOG_DIR, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "error: cannot create '%s': %s\n", LOG_DIR, strerror(errno)); return -1;
    }
    return 0;
}

/* ---- integrated demo ---------------------------------------------------- */
typedef struct { const char *name; int rc, expected; } Step;

static void banner(const char *t) { printf("\n################ %s ################\n", t); fflush(stdout); }

static int demo(void)
{
    Step steps[12]; int n = 0;
    #define STEP(label, expect, call) do { banner(label); steps[n].name = label; steps[n].expected = expect; \
        steps[n].rc = (call); n++; } while (0)
    char *s1[] = { "sched", "fcfs", "tests/jobs.txt" };
    char *s2[] = { "sched", "rr", "tests/jobs.txt", "3" };
    char *s3[] = { "sched", "prio", "tests/jobs.txt" };
    char *s4[] = { "sched", "compare", "tests/jobs.txt", "3" };
    char *lg[] = { "logger", "2", "2", "6" };
    char *ip[] = { "ipc" };
    char *b1[] = { "banker", "tests/safe.txt" };
    char *b2[] = { "banker", "tests/unsafe.txt" };
    char *w[]  = { "worker", "all" };
    char *lm[] = { "limits", "all" };
    STEP("1. FCFS scheduling", 0, cmd_sched(3, s1));
    STEP("2. Round Robin scheduling", 0, cmd_sched(4, s2));
    STEP("3. Priority scheduling", 0, cmd_sched(3, s3));
    STEP("4. Scheduler comparison", 0, cmd_sched(4, s4));
    STEP("5. Producer-consumer logger", 0, cmd_logger(4, lg));
    STEP("6. Shared memory + semaphore IPC", 0, cmd_ipc(1, ip));
    STEP("7. Banker's algorithm - safe state", 0, cmd_banker(2, b1));
    STEP("8. Banker's algorithm - unsafe state", 2, cmd_banker(2, b2));
    STEP("9. Worker processes (normal + SIMULATED failures)", 0, cmd_worker(2, w));
    STEP("10. Resource limits", 0, cmd_limits(2, lm));
    char *cl[] = { "cleanup" };
    STEP("11. Cleanup of IPC objects", 0, cmd_cleanup(1, cl));
    #undef STEP

    printf("\n================ INTEGRATED DEMO SUMMARY ================\n");
    int bad = 0;
    for (int i = 0; i < n; i++) {
        int ok = steps[i].rc == steps[i].expected;
        printf("%-55s : %s%s\n", steps[i].name, ok ? "PASS" : "FAIL",
               steps[i].expected == 2 && ok ? "  (unsafe state correctly detected)" : "");
        bad += !ok;
    }
    printf("Log file written to %s\n", LOG_PATH);
    printf("OVERALL: %d/%d steps passed\n", n - bad, n);
    return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "error: no command given\n\n"); print_usage(stderr); return 1; }
    const char *c = argv[1];
    int ac = argc - 1; char **av = argv + 1;           /* av[0] is the sub-command name */
    if (!strcmp(c, "help") || !strcmp(c, "--help") || !strcmp(c, "-h")) { print_usage(stdout); return 0; }
    if (!strcmp(c, "sched"))   return cmd_sched(ac, av);
    if (!strcmp(c, "logger"))  return cmd_logger(ac, av);
    if (!strcmp(c, "ipc"))     return cmd_ipc(ac, av);
    if (!strcmp(c, "banker"))  return cmd_banker(ac, av);
    if (!strcmp(c, "worker"))  return cmd_worker(ac, av);
    if (!strcmp(c, "limits"))  return cmd_limits(ac, av);
    if (!strcmp(c, "cleanup")) return cmd_cleanup(ac, av);
    if (!strcmp(c, "demo")) {
        if (ac != 1) { fprintf(stderr, "error: 'demo' takes no arguments\n"); return 1; }
        return demo();
    }
    fprintf(stderr, "error: unknown command '%s'\n\n", c);
    print_usage(stderr);
    return 1;
}
