/* logger.c - FR3: bounded producer-consumer activity logger (POSIX threads)
 *
 * Shared state: circular buffer of BUF_SIZE messages.
 *  - mutex `m`     protects buf/head/tail/count/done and the counters
 *  - `not_full`    producers wait on it while the buffer is full
 *  - `not_empty`   consumers wait on it while the buffer is empty (and not done)
 * Waits are always in `while` loops (spurious wake-ups are allowed by POSIX).
 * Shutdown: main joins all producers, sets done=1 under the mutex and
 * broadcasts both conditions; consumers exit once done && count==0.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include "common.h"

#define BUF_SIZE        5
#define PRODUCER_DELAY  10   /* ms - producers are faster ...            */
#define CONSUMER_DELAY  40   /* ms - ... than consumers, so buffer fills */

typedef struct { char text[96]; } Msg;

static Msg   buf[BUF_SIZE];
static int   head, tail, count, done;
static long  produced, consumed, full_waits, empty_waits;
static int   items_per_producer;
static FILE *logf;
static pthread_mutex_t m     = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t log_m = PTHREAD_MUTEX_INITIALIZER;   /* serialises file writes */
static pthread_cond_t  not_full  = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  not_empty = PTHREAD_COND_INITIALIZER;

static void sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void write_log(int cid, const char *text)
{
    struct timespec now; struct tm tmv; char ts[32];
    clock_gettime(CLOCK_REALTIME, &now);
    localtime_r(&now.tv_sec, &tmv);
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tmv);
    pthread_mutex_lock(&log_m);
    fprintf(logf, "[%s.%03ld] consumer-%d | %s\n", ts, now.tv_nsec / 1000000L, cid, text);
    fflush(logf);
    pthread_mutex_unlock(&log_m);
}

static void *producer(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int i = 1; i <= items_per_producer; i++) {
        Msg msg;
        snprintf(msg.text, sizeof msg.text, "producer-%d event %d: job J%d submitted to lab queue", id, i, id * 100 + i);
        pthread_mutex_lock(&m);
        while (count == BUF_SIZE) {
            full_waits++;
            printf("[producer-%d] buffer FULL (%d/%d) -> waiting on not_full\n", id, count, BUF_SIZE);
            pthread_cond_wait(&not_full, &m);
        }
        buf[tail] = msg; tail = (tail + 1) % BUF_SIZE; count++; produced++;
        printf("[producer-%d] enqueued event %d            (buffer %d/%d)\n", id, i, count, BUF_SIZE);
        pthread_cond_signal(&not_empty);
        pthread_mutex_unlock(&m);
        sleep_ms(PRODUCER_DELAY);
    }
    return NULL;
}

static void *consumer(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (;;) {
        pthread_mutex_lock(&m);
        while (count == 0 && !done) {
            empty_waits++;
            pthread_cond_wait(&not_empty, &m);
        }
        if (count == 0 && done) { pthread_mutex_unlock(&m); break; }   /* drained + finished */
        Msg msg = buf[head]; head = (head + 1) % BUF_SIZE; count--; consumed++;
        printf("[consumer-%d] dequeued -> log file        (buffer %d/%d)\n", id, count, BUF_SIZE);
        pthread_cond_signal(&not_full);
        pthread_mutex_unlock(&m);
        write_log(id, msg.text);
        sleep_ms(CONSUMER_DELAY);
    }
    printf("[consumer-%d] buffer drained and producers done -> exiting\n", id);
    return NULL;
}

int cmd_logger(int argc, char **argv)
{
    int np = 2, nc = 2, items = 8;
    if (argc > 4) { fprintf(stderr, "error: usage: logger [producers] [consumers] [items_per_producer]\n"); return 1; }
    if ((argc > 1 && (parse_int(argv[1], &np) || np < 1 || np > 16)) ||
        (argc > 2 && (parse_int(argv[2], &nc) || nc < 1 || nc > 16)) ||
        (argc > 3 && (parse_int(argv[3], &items) || items < 1 || items > 1000))) {
        fprintf(stderr, "error: producers/consumers must be 1..16 and items 1..1000 (integers)\n");
        return 1;
    }
    if (ensure_log_dir() != 0) return 1;
    logf = fopen(LOG_PATH, "w");
    if (!logf) { fprintf(stderr, "error: cannot open %s: %s\n", LOG_PATH, strerror(errno)); return 1; }
    fprintf(logf, "=== CampusCompute activity log: %d producers, %d consumers, %d items each, buffer=%d ===\n",
            np, nc, items, BUF_SIZE);

    head = tail = count = done = 0; produced = consumed = full_waits = empty_waits = 0;
    items_per_producer = items;

    pthread_t pt[16], ct[16];
    int pcreated = 0, ccreated = 0, err = 0;
    printf("=== Producer-Consumer Logger: %d producers, %d consumers, %d items each, buffer size %d ===\n",
           np, nc, items, BUF_SIZE);
    for (int i = 0; i < nc && !err; i++) {
        int rc = pthread_create(&ct[i], NULL, consumer, (void *)(intptr_t)(i + 1));
        if (rc) { fprintf(stderr, "error: pthread_create(consumer): %s\n", strerror(rc)); err = 1; } else ccreated++;
    }
    for (int i = 0; i < np && !err; i++) {
        int rc = pthread_create(&pt[i], NULL, producer, (void *)(intptr_t)(i + 1));
        if (rc) { fprintf(stderr, "error: pthread_create(producer): %s\n", strerror(rc)); err = 1; } else pcreated++;
    }
    for (int i = 0; i < pcreated; i++) pthread_join(pt[i], NULL);
    printf("[main] all %d producers joined; signalling shutdown\n", pcreated);

    pthread_mutex_lock(&m);
    done = 1;
    pthread_cond_broadcast(&not_empty);      /* wake every sleeping consumer */
    pthread_cond_broadcast(&not_full);
    pthread_mutex_unlock(&m);

    for (int i = 0; i < ccreated; i++) pthread_join(ct[i], NULL);
    printf("[main] all %d consumers joined\n", ccreated);
    fprintf(logf, "=== end of log: %ld events written ===\n", consumed);
    fclose(logf); logf = NULL;

    long expected = (long)np * items;
    printf("\nSummary: produced=%ld consumed=%ld expected=%ld | producer waits (buffer full)=%ld | consumer waits (empty)=%ld\n",
           produced, consumed, expected, full_waits, empty_waits);
    printf("Log file: %s\n", LOG_PATH);
    if (err || produced != expected || consumed != expected) { printf("RESULT: FAIL\n"); return 1; }
    printf("RESULT: PASS - no events lost, all threads terminated cleanly\n");
    return 0;
}
