/* scheduler.c - FR1/FR2: job loading + FCFS, Round Robin, non-preemptive Priority */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "common.h"

#define MAX_SLICES 100000
#define IDLE (-1)
#define DEFAULT_QUANTUM 3

typedef struct { int id, start, end; } Slice;      /* one Gantt chart entry */
typedef struct { double awt, atat, act; int makespan, dispatches; } Metrics;

static Slice timeline[MAX_SLICES];
static int   nslices;

static void add_slice(int id, int s, int e)
{
    if (e <= s || nslices >= MAX_SLICES) return;
    timeline[nslices].id = id; timeline[nslices].start = s; timeline[nslices].end = e;
    nslices++;
}

/* ---------- FR1: load + validate jobs ------------------------------------ */
int load_jobs(const char *path, Job *jobs, int max)
{
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "error: cannot open '%s': %s\n", path, strerror(errno)); return -1; }
    char line[256];
    int n = 0, lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        if (!strchr(line, '\n') && !feof(f)) {
            fprintf(stderr, "error: %s line %d: line too long\n", path, lineno); fclose(f); return -1;
        }
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == '\0' || *p == '#') continue;               /* blank / comment */
        int id, at, bt, pr, pos = 0;
        if (sscanf(p, "%d %d %d %d%n", &id, &at, &bt, &pr, &pos) != 4) {
            fprintf(stderr, "error: %s line %d: expected 'id arrival burst priority' (integers)\n", path, lineno);
            fclose(f); return -1;
        }
        char *rest = p + pos;
        while (isspace((unsigned char)*rest)) rest++;
        if (*rest != '\0' && *rest != '#') {
            fprintf(stderr, "error: %s line %d: unexpected extra text '%s'\n", path, lineno, rest);
            fclose(f); return -1;
        }
        if (id <= 0 || at < 0 || bt <= 0 || bt > MAX_BURST || pr < 0) {
            fprintf(stderr, "error: %s line %d: need id>0, arrival>=0, 0<burst<=%d, priority>=0\n", path, lineno, MAX_BURST);
            fclose(f); return -1;
        }
        for (int i = 0; i < n; i++)
            if (jobs[i].id == id) {
                fprintf(stderr, "error: %s line %d: duplicate job id %d\n", path, lineno, id);
                fclose(f); return -1;
            }
        if (n >= max) { fprintf(stderr, "error: too many jobs (max %d)\n", max); fclose(f); return -1; }
        Job *j = &jobs[n++];
        memset(j, 0, sizeof *j);
        j->id = id; j->arrival = at; j->burst = bt; j->priority = pr;
        j->remaining = bt; j->start = -1; j->completion = -1; j->status = 1;  /* READY (waiting for CPU) */
    }
    fclose(f);
    if (n == 0) { fprintf(stderr, "error: '%s' contains no jobs\n", path); return -1; }
    return n;
}

static int cmp_arrival_id(const void *a, const void *b)
{
    const Job *x = a, *y = b;
    if (x->arrival != y->arrival) return x->arrival - y->arrival;
    return x->id - y->id;
}
static int cmp_id(const void *a, const void *b) { return ((const Job *)a)->id - ((const Job *)b)->id; }

/* ---------- Algorithms ---------------------------------------------------- */
static void run_job(Job *j, int *t, int len)
{
    j->status = 2;
    if (j->start < 0) j->start = *t;
    add_slice(j->id, *t, *t + len);
    *t += len; j->remaining -= len;
    if (j->remaining == 0) { j->completion = *t; j->status = 3; } else j->status = 1;
}

/* FCFS: sort by arrival, tie -> lower job id */
static void sched_fcfs(Job *j, int n)
{
    qsort(j, n, sizeof *j, cmp_arrival_id);
    int t = 0;
    for (int i = 0; i < n; i++) {
        if (t < j[i].arrival) { add_slice(IDLE, t, j[i].arrival); t = j[i].arrival; }
        run_job(&j[i], &t, j[i].burst);
    }
}

/* Priority (non-preemptive): LOWEST number = HIGHEST priority.
   Tie -> earlier arrival, then lower job id. */
static void sched_prio(Job *j, int n)
{
    int t = 0, done = 0;
    while (done < n) {
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (j[i].status == 3 || j[i].arrival > t) continue;
            if (best < 0 || j[i].priority < j[best].priority ||
               (j[i].priority == j[best].priority && (j[i].arrival < j[best].arrival ||
               (j[i].arrival == j[best].arrival && j[i].id < j[best].id)))) best = i;
        }
        if (best < 0) {                      /* CPU idle: jump to next arrival */
            int next = -1;
            for (int i = 0; i < n; i++)
                if (j[i].status != 3 && (next < 0 || j[i].arrival < next)) next = j[i].arrival;
            add_slice(IDLE, t, next); t = next; continue;
        }
        run_job(&j[best], &t, j[best].burst);
        done++;
    }
}

/* Round Robin: jobs arriving at the same instant a quantum expires enter the
   ready queue BEFORE the pre-empted job is re-queued. */
static void sched_rr(Job *j, int n, int q)
{
    qsort(j, n, sizeof *j, cmp_arrival_id);
    int queue[MAX_JOBS + 1], head = 0, tail = 0, cnt = 0, next = 0, t = 0, done = 0;
#define PUSH(x) do { queue[tail] = (x); tail = (tail + 1) % (MAX_JOBS + 1); cnt++; } while (0)
#define ADMIT() while (next < n && j[next].arrival <= t) { PUSH(next); next++; }
    while (done < n) {
        ADMIT();
        if (cnt == 0) { add_slice(IDLE, t, j[next].arrival); t = j[next].arrival; continue; }
        int i = queue[head]; head = (head + 1) % (MAX_JOBS + 1); cnt--;
        int run = j[i].remaining < q ? j[i].remaining : q;
        run_job(&j[i], &t, run);
        ADMIT();                              /* new arrivals first ... */
        if (j[i].remaining > 0) PUSH(i); else done++;   /* ... then the pre-empted job */
    }
#undef PUSH
#undef ADMIT
}

/* ---------- Reporting ------------------------------------------------------ */
static void print_rules(const char *pol, int q)
{
    printf("Rule: ");
    if (!strcmp(pol, "FCFS"))
        printf("jobs run in order of arrival; tie-break = lower job ID.\n");
    else if (!strcmp(pol, "PRIORITY"))
        printf("non-preemptive; LOWEST priority number = HIGHEST priority; tie-break = earlier arrival, then lower job ID.\n");
    else
        printf("Round Robin, time quantum = %d; newly arrived jobs enter the queue BEFORE the pre-empted job; tie-break = lower job ID.\n", q);
}

static void run_policy(const char *pol, const Job *src, int n, int q, int verbose, Metrics *m)
{
    Job j[MAX_JOBS];
    memcpy(j, src, n * sizeof *j);            /* every policy works on identical input */
    nslices = 0;
    if (!strcmp(pol, "FCFS")) sched_fcfs(j, n);
    else if (!strcmp(pol, "PRIORITY")) sched_prio(j, n);
    else sched_rr(j, n, q);

    double swt = 0, stat = 0, sct = 0; int mk = 0;
    for (int i = 0; i < n; i++) {
        j[i].tat = j[i].completion - j[i].arrival;
        j[i].wt  = j[i].tat - j[i].burst;
        swt += j[i].wt; stat += j[i].tat; sct += j[i].completion;
        if (j[i].completion > mk) mk = j[i].completion;
    }
    m->awt = swt / n; m->atat = stat / n; m->act = sct / n; m->makespan = mk;
    m->dispatches = 0;
    for (int i = 0; i < nslices; i++) if (timeline[i].id != IDLE) m->dispatches++;
    if (!verbose) return;

    printf("\n=== %s Scheduling ===\n", pol);
    print_rules(pol, q);
    printf("\nExecution order : ");
    for (int i = 0, first = 1; i < nslices; i++) {
        if (timeline[i].id == IDLE) printf("%s[idle]", first ? "" : " -> ");
        else printf("%sJ%d", first ? "" : " -> ", timeline[i].id);
        first = 0;
    }
    printf("\nGantt timeline  :\n");
    for (int i = 0; i < nslices; i++) {
        if (timeline[i].id == IDLE) printf("| idle %d-%d ", timeline[i].start, timeline[i].end);
        else printf("| J%d %d-%d ", timeline[i].id, timeline[i].start, timeline[i].end);
        if ((i + 1) % 8 == 0 || i == nslices - 1) printf("|\n");
    }
    qsort(j, n, sizeof *j, cmp_id);
    printf("\n%-5s %-8s %-6s %-9s %-7s %-11s %-11s %-8s\n",
           "Job", "Arrival", "Burst", "Priority", "Start", "Completion", "Turnaround", "Waiting");
    for (int i = 0; i < n; i++)
        printf("J%-4d %-8d %-6d %-9d %-7d %-11d %-11d %-8d\n", j[i].id, j[i].arrival, j[i].burst,
               j[i].priority, j[i].start, j[i].completion, j[i].tat, j[i].wt);
    printf("\nTAT = Completion - Arrival ; WT = TAT - Burst\n");
    printf("Average Completion = %.2f | Average Turnaround = %.2f | Average Waiting = %.2f\n",
           m->act, m->atat, m->awt);
}

int cmd_sched(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "error: usage: sched fcfs|rr|prio|compare <jobs.txt> [quantum]\n");
        return 1;
    }
    const char *pol = argv[1];
    int q = DEFAULT_QUANTUM;
    if (argc >= 4 && (parse_int(argv[3], &q) != 0 || q <= 0 || q > MAX_BURST)) {
        fprintf(stderr, "error: quantum must be an integer between 1 and %d (got '%s')\n", MAX_BURST, argv[3]);
        return 1;
    }
    if (argc > 4) { fprintf(stderr, "error: too many arguments for 'sched'\n"); return 1; }

    Job jobs[MAX_JOBS];
    int n = load_jobs(argv[2], jobs, MAX_JOBS);
    if (n < 0) return 1;
    Metrics m;
    if (!strcmp(pol, "fcfs"))      run_policy("FCFS", jobs, n, q, 1, &m);
    else if (!strcmp(pol, "rr"))   run_policy("RR", jobs, n, q, 1, &m);
    else if (!strcmp(pol, "prio")) run_policy("PRIORITY", jobs, n, q, 1, &m);
    else if (!strcmp(pol, "compare")) {
        Metrics a, b, c;
        run_policy("FCFS", jobs, n, q, 0, &a);
        run_policy("RR", jobs, n, q, 0, &b);
        run_policy("PRIORITY", jobs, n, q, 0, &c);
        printf("\n=== Scheduling Comparison (%d jobs from %s, RR quantum = %d) ===\n", n, argv[2], q);
        printf("%-22s %-10s %-10s %-10s %-10s %-10s\n", "Policy", "Avg CT", "Avg TAT", "Avg WT", "Makespan", "Dispatches");
        printf("%-22s %-10.2f %-10.2f %-10.2f %-10d %-10d\n", "FCFS", a.act, a.atat, a.awt, a.makespan, a.dispatches);
        printf("%-22s %-10.2f %-10.2f %-10.2f %-10d %-10d\n", "Round Robin", b.act, b.atat, b.awt, b.makespan, b.dispatches);
        printf("%-22s %-10.2f %-10.2f %-10.2f %-10d %-10d\n", "Priority (non-preempt)", c.act, c.atat, c.awt, c.makespan, c.dispatches);
        const char *bn = "FCFS"; double best = a.awt;
        if (b.awt < best) { best = b.awt; bn = "Round Robin"; }
        if (c.awt < best) { best = c.awt; bn = "Priority"; }
        printf("\nLowest average waiting time: %s (%.2f)\n", bn, best);
    } else {
        fprintf(stderr, "error: unknown scheduling policy '%s' (use fcfs, rr, prio or compare)\n", pol);
        return 1;
    }
    return 0;
}
