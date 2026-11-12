/* ipc.c - FR4 + part of FR7: POSIX shared memory + named semaphores between a
 * parent and a forked child that opens the objects BY NAME (true IPC).
 *   sem_mutex : binary semaphore guarding the shared block
 *   sem_ready : child posts it when data is written (ordering/signalling)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "common.h"

#define SHM_JOBS 6
struct shm_job   { int id, burst, priority; };
struct shm_block { int count; struct shm_job jobs[SHM_JOBS]; long checksum; };

static long compute_checksum(const struct shm_block *b)
{
    long s = b->count;
    for (int i = 0; i < b->count; i++) s += b->jobs[i].id * 31 + b->jobs[i].burst * 17 + b->jobs[i].priority * 7;
    return s;
}

static void sleep_ms(int ms) { struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L }; nanosleep(&ts, NULL); }

static int sem_wait_retry(sem_t *s)
{
    while (sem_wait(s) == -1) if (errno != EINTR) return -1;
    return 0;
}

/* Child: the "worker" process that publishes job data */
static int child_writer(void)
{
    int fd = shm_open(CC_SHM_NAME, O_RDWR, 0600);
    if (fd < 0) { perror("child: shm_open"); return 1; }
    struct shm_block *b = mmap(NULL, sizeof *b, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (b == MAP_FAILED) { perror("child: mmap"); return 1; }
    sem_t *mtx = sem_open(CC_SEM_MUTEX, 0), *rdy = sem_open(CC_SEM_READY, 0);
    if (mtx == SEM_FAILED || rdy == SEM_FAILED) { perror("child: sem_open"); munmap(b, sizeof *b); return 1; }

    printf("[child  pid=%d] opened shared memory + semaphores by name; preparing data...\n", getpid());
    fflush(stdout);
    sleep_ms(500);                                   /* parent is visibly blocked meanwhile */
    if (sem_wait_retry(mtx) != 0) { perror("child: sem_wait"); return 1; }
    b->count = SHM_JOBS;
    for (int i = 0; i < SHM_JOBS; i++) {
        b->jobs[i].id = i + 1; b->jobs[i].burst = 2 + (i * 3) % 7; b->jobs[i].priority = (i * 2) % 5;
    }
    b->checksum = compute_checksum(b);
    printf("[child  pid=%d] wrote %d jobs (checksum %ld) inside critical section\n", getpid(), b->count, b->checksum);
    fflush(stdout);
    sem_post(mtx);                                   /* leave critical section */
    sem_post(rdy);                                   /* tell parent data is ready */
    sem_close(mtx); sem_close(rdy); munmap(b, sizeof *b);
    return 0;
}

/* Try to open an object WITHOUT creating it: must fail with ENOENT after unlink */
static void verify_gone(void)
{
    int fd = shm_open(CC_SHM_NAME, O_RDWR, 0);
    printf("  verify shm  %-16s : %s\n", CC_SHM_NAME, fd < 0 && errno == ENOENT ? "gone (ENOENT)" : "STILL EXISTS");
    if (fd >= 0) close(fd);
    const char *names[2] = { CC_SEM_MUTEX, CC_SEM_READY };
    for (int i = 0; i < 2; i++) {
        sem_t *s = sem_open(names[i], 0);
        printf("  verify sem  %-16s : %s\n", names[i], s == SEM_FAILED && errno == ENOENT ? "gone (ENOENT)" : "STILL EXISTS");
        if (s != SEM_FAILED) sem_close(s);
    }
}

int cmd_cleanup(int argc, char **argv)
{
    int logs = 0;
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "--logs") != 0)) {
        fprintf(stderr, "error: usage: cleanup [--logs]\n"); return 1;
    }
    if (argc == 2) logs = 1;
    printf("=== Cleanup (only objects created by CampusCompute) ===\n");
    if (shm_unlink(CC_SHM_NAME) == 0) printf("removed shared memory %s\n", CC_SHM_NAME);
    else printf("shared memory %s: %s\n", CC_SHM_NAME, errno == ENOENT ? "not present" : strerror(errno));
    const char *names[2] = { CC_SEM_MUTEX, CC_SEM_READY };
    for (int i = 0; i < 2; i++) {
        if (sem_unlink(names[i]) == 0) printf("removed semaphore %s\n", names[i]);
        else printf("semaphore %s: %s\n", names[i], errno == ENOENT ? "not present" : strerror(errno));
    }
    if (logs) {
        if (unlink(LOG_PATH) == 0) printf("removed %s\n", LOG_PATH);
        else printf("%s: %s\n", LOG_PATH, errno == ENOENT ? "not present" : strerror(errno));
    }
    return 0;
}

int cmd_ipc(int argc, char **argv)
{
    int keep = 0;
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "keep") != 0)) {
        fprintf(stderr, "error: usage: ipc [keep]   ('keep' leaves the IPC objects for 'cleanup' to remove)\n"); return 1;
    }
    if (argc == 2) keep = 1;
    int rc = 1, shm_made = 0, sems_made = 0;
    struct shm_block *b = MAP_FAILED;
    sem_t *mtx = SEM_FAILED, *rdy = SEM_FAILED;

    /* Remove stale copies of OUR OWN names from a previous crashed run */
    shm_unlink(CC_SHM_NAME); sem_unlink(CC_SEM_MUTEX); sem_unlink(CC_SEM_READY);

    printf("=== POSIX Shared Memory + Named Semaphore IPC ===\n");
    int fd = shm_open(CC_SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) { perror("shm_open"); goto out; }
    shm_made = 1;
    if (ftruncate(fd, sizeof *b) != 0) { perror("ftruncate"); close(fd); goto out; }
    b = mmap(NULL, sizeof *b, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (b == MAP_FAILED) { perror("mmap"); goto out; }
    memset(b, 0, sizeof *b);
    mtx = sem_open(CC_SEM_MUTEX, O_CREAT | O_EXCL, 0600, 1);   /* binary semaphore */
    rdy = sem_open(CC_SEM_READY, O_CREAT | O_EXCL, 0600, 0);   /* signalling semaphore */
    if (mtx == SEM_FAILED || rdy == SEM_FAILED) { perror("sem_open"); goto out; }
    sems_made = 1;
    printf("[parent pid=%d] created shm %s (%zu bytes), sem %s (=1), sem %s (=0)\n",
           getpid(), CC_SHM_NAME, sizeof *b, CC_SEM_MUTEX, CC_SEM_READY);
    fflush(stdout);

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); goto out; }
    if (pid == 0) _exit(child_writer());

    printf("[parent pid=%d] forked child %d; blocking on sem_ready (timeout 5 s)...\n", getpid(), pid);
    fflush(stdout);
    struct timespec dl; clock_gettime(CLOCK_REALTIME, &dl); dl.tv_sec += 5;
    int w;
    while ((w = sem_timedwait(rdy, &dl)) == -1 && errno == EINTR) ;
    int ok = 0;
    if (w == -1) fprintf(stderr, "error: sem_timedwait: %s (child never signalled)\n", strerror(errno));
    else if (sem_wait_retry(mtx) == 0) {
        printf("[parent pid=%d] woke up; reading shared memory inside critical section:\n", getpid());
        printf("  %-6s %-6s %-9s\n", "JobID", "Burst", "Priority");
        for (int i = 0; i < b->count && i < SHM_JOBS; i++)
            printf("  J%-5d %-6d %-9d\n", b->jobs[i].id, b->jobs[i].burst, b->jobs[i].priority);
        long cs = compute_checksum(b);
        printf("  checksum in memory = %ld, recomputed = %ld -> %s\n", b->checksum, cs, cs == b->checksum ? "MATCH" : "MISMATCH");
        ok = (cs == b->checksum && b->count == SHM_JOBS);
        sem_post(mtx);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) < 0) perror("waitpid");
    else printf("[parent pid=%d] child %d exited with status %d\n", getpid(), pid, WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    rc = (ok && WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : 1;

out:
    if (b != MAP_FAILED) munmap(b, sizeof *b);
    if (mtx != SEM_FAILED) sem_close(mtx);
    if (rdy != SEM_FAILED) sem_close(rdy);
    if (!keep) {
        printf("\nCleaning up IPC objects created by this run...\n");
        if (shm_made && shm_unlink(CC_SHM_NAME) != 0) perror("shm_unlink");
        if (sems_made) {
            if (sem_unlink(CC_SEM_MUTEX) != 0) perror("sem_unlink mutex");
            if (sem_unlink(CC_SEM_READY) != 0) perror("sem_unlink ready");
        }
        verify_gone();
    } else if (shm_made) {
        printf("\n'keep' given: IPC objects left in place (see: ls /dev/shm | grep cc_). Remove them with: ./campuscompute cleanup\n");
    }
    printf("RESULT: %s\n", rc == 0 ? "PASS - data exchanged correctly via shared memory" : "FAIL");
    return rc;
}
