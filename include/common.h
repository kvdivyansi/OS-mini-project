/* common.h - shared definitions and prototypes for CampusCompute */
#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>

#define MAX_JOBS      64
#define MAX_BURST     1000
#define LOG_DIR       "logs"
#define LOG_PATH      "logs/campus.log"

/* POSIX IPC objects created by this program (and ONLY these are ever removed) */
#define CC_SHM_NAME   "/cc_shm_campus"
#define CC_SEM_MUTEX  "/cc_sem_mutex"
#define CC_SEM_READY  "/cc_sem_ready"

/* A job as described in FR1. status: 0=NEW 1=READY 2=RUNNING 3=DONE */
typedef struct {
    int id, arrival, burst, priority;
    int remaining, start, completion, tat, wt;
    int status;
} Job;

/* main.c helpers */
int  parse_int(const char *s, int *out);   /* strict integer parse, 0 = ok */
void print_usage(FILE *out);
int  ensure_log_dir(void);

/* scheduler.c */
int load_jobs(const char *path, Job *jobs, int max);   /* returns count or -1 */
int cmd_sched(int argc, char **argv);
/* logger.c */
int cmd_logger(int argc, char **argv);
/* ipc.c */
int cmd_ipc(int argc, char **argv);
int cmd_cleanup(int argc, char **argv);
/* banker.c : returns 0 = safe, 2 = unsafe, 1 = error */
int cmd_banker(int argc, char **argv);
/* worker.c */
int cmd_worker(int argc, char **argv);
/* limits.c */
int cmd_limits(int argc, char **argv);

#endif
