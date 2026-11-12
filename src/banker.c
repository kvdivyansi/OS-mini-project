/* banker.c - FR5: Banker's safety algorithm.
 * Input file: "n m", Available[m], Maximum[n][m], Allocation[n][m]  ('#' starts a comment)
 * Exit code: 0 = SAFE, 2 = UNSAFE, 1 = input/usage error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "common.h"

#define MAXP 10
#define MAXR 10

static int next_int(FILE *f, int *out)           /* token reader that skips '#' comments */
{
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (c == '#') { while ((c = fgetc(f)) != EOF && c != '\n') ; }
        else if (!isspace(c)) { ungetc(c, f); return fscanf(f, "%d", out) == 1; }
    }
    return 0;
}

static void print_vec(const int *v, int m) { for (int j = 0; j < m; j++) printf("%3d", v[j]); }

int cmd_banker(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "error: usage: banker <state-file>   (see tests/safe.txt)\n"); return 1; }
    FILE *f = fopen(argv[1], "r");
    if (!f) { fprintf(stderr, "error: cannot open '%s': %s\n", argv[1], strerror(errno)); return 1; }
    int n, m, avail[MAXR], max[MAXP][MAXR], alloc[MAXP][MAXR], need[MAXP][MAXR], tmp;
    if (!next_int(f, &n) || !next_int(f, &m) || n < 1 || n > MAXP || m < 1 || m > MAXR) {
        fprintf(stderr, "error: first line must be 'n m' with 1<=n<=%d, 1<=m<=%d\n", MAXP, MAXR); fclose(f); return 1;
    }
    for (int j = 0; j < m; j++)
        if (!next_int(f, &avail[j]) || avail[j] < 0) { fprintf(stderr, "error: bad Available vector\n"); fclose(f); return 1; }
    for (int i = 0; i < n; i++) for (int j = 0; j < m; j++)
        if (!next_int(f, &max[i][j]) || max[i][j] < 0) { fprintf(stderr, "error: bad Maximum matrix (P%d)\n", i); fclose(f); return 1; }
    for (int i = 0; i < n; i++) for (int j = 0; j < m; j++)
        if (!next_int(f, &alloc[i][j]) || alloc[i][j] < 0) { fprintf(stderr, "error: bad Allocation matrix (P%d)\n", i); fclose(f); return 1; }
    if (next_int(f, &tmp)) { fprintf(stderr, "error: unexpected extra data after Allocation matrix\n"); fclose(f); return 1; }
    fclose(f);

    for (int i = 0; i < n; i++) for (int j = 0; j < m; j++) {
        need[i][j] = max[i][j] - alloc[i][j];                 /* Need = Maximum - Allocation */
        if (need[i][j] < 0) {
            fprintf(stderr, "error: Allocation exceeds Maximum for P%d, resource R%d\n", i, j); return 1;
        }
    }
    printf("=== Banker's Algorithm: %d processes, %d resource types (%s) ===\n\n", n, m, argv[1]);
    printf("%-5s %-14s %-14s %-14s\n", "Proc", "Allocation", "Maximum", "Need");
    for (int i = 0; i < n; i++) {
        printf("P%-4d", i); print_vec(alloc[i], m); printf("    "); print_vec(max[i], m); printf("    "); print_vec(need[i], m); printf("\n");
    }
    printf("\nAvailable:"); print_vec(avail, m); printf("\n\nSafety algorithm trace:\n");

    int work[MAXR], finish[MAXP] = {0}, seq[MAXP], cnt = 0;
    memcpy(work, avail, sizeof(int) * m);
    int progress = 1;
    while (cnt < n && progress) {
        progress = 0;
        for (int i = 0; i < n; i++) {
            if (finish[i]) continue;
            int ok = 1;
            for (int j = 0; j < m; j++) if (need[i][j] > work[j]) { ok = 0; break; }
            if (!ok) continue;
            printf("  P%d: Need <= Work [", i); print_vec(work, m);
            for (int j = 0; j < m; j++) work[j] += alloc[i][j];        /* it releases what it holds */
            printf(" ]  -> runs to completion, Work becomes ["); print_vec(work, m); printf(" ]\n");
            finish[i] = 1; seq[cnt++] = i; progress = 1;
        }
    }
    printf("\n");
    if (cnt == n) {
        printf("RESULT: SAFE state. Safe sequence: ");
        for (int i = 0; i < n; i++) printf("P%d%s", seq[i], i < n - 1 ? " -> " : "\n");
        return 0;
    }
    printf("RESULT: UNSAFE state. No further process can obtain its Need from Work =");
    print_vec(work, m);
    printf("\n        Processes that cannot finish:");
    for (int i = 0; i < n; i++) if (!finish[i]) printf(" P%d", i);
    printf("\n        Possible deadlock if these processes request their maximum.\n");
    return 2;
}
