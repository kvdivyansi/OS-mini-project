# CampusCompute - Intelligent Job Scheduling and Resource Management
OS Mini-Project (UE25CS242A, Level 3). One C program, one sub-command per feature.

## Prerequisites (Ubuntu or WSL2 Ubuntu)
```
sudo apt update
sudo apt install build-essential        # gcc + make + libc headers (pthreads/rt are part of glibc)
sudo apt install valgrind               # OPTIONAL, only for memory checks
```
Nothing else is required. No root access is needed to run the program.

## Build / clean
```
make            # builds ./campuscompute
make clean      # removes objects, binary and logs/campus.log
make test       # automated self-check of every feature (16 checks)
make tsan       # optional: ThreadSanitizer build ./campuscompute_tsan
```
Manual build (without make):
`gcc -std=gnu11 -Wall -Wextra -Iinclude src/*.c -o campuscompute -pthread -lrt`

## Commands
| Command | Feature |
|---|---|
| `./campuscompute help` | usage (FR8) |
| `./campuscompute sched fcfs tests/jobs.txt` | FCFS (FR1/FR2) |
| `./campuscompute sched rr tests/jobs.txt 3` | Round Robin, quantum 3 |
| `./campuscompute sched prio tests/jobs.txt` | Non-preemptive priority |
| `./campuscompute sched compare tests/jobs.txt 3` | Comparison of all policies |
| `./campuscompute logger 2 2 8` | Producer-consumer logger, 2 producers, 2 consumers, 8 items each (FR3) |
| `./campuscompute ipc` | Shared memory + named semaphores (FR4) |
| `./campuscompute ipc keep` | Same, but leaves IPC objects so `cleanup` can be shown |
| `./campuscompute banker tests/safe.txt` | Banker's - safe (exit 0) (FR5) |
| `./campuscompute banker tests/unsafe.txt` | Banker's - unsafe (exit 2) |
| `./campuscompute worker normal` / `fail` / `signal` / `all` | fork + waitpid (FR6) |
| `./campuscompute limits nofile` / `cpu` / `mem` / `all` | setrlimit (FR7) |
| `./campuscompute demo` | Integrated run + summary |
| `./campuscompute cleanup [--logs]` | Remove only this program's IPC objects (and optionally the log) |

Invalid input examples (S19): `./campuscompute frobnicate`, `./campuscompute sched rr tests/jobs.txt abc`,
`./campuscompute sched fcfs tests/jobs_invalid.txt`, `./campuscompute banker tests/banker_invalid.txt`.

## Design rules (stated by the program as well)
* FCFS: sort by arrival, tie -> lower job ID.
* Priority (non-preemptive): LOWEST number = HIGHEST priority; tie -> earlier arrival, then lower ID.
* Round Robin: default quantum 3; jobs arriving when a quantum expires are queued BEFORE the pre-empted job.
* CPU idle gaps are shown in the Gantt chart. TAT = Completion - Arrival; WT = TAT - Burst.
* Logger: circular buffer (5 slots), 1 mutex + 2 condition variables (`not_full`, `not_empty`), `while` loops
  around waits, shutdown via `done` flag + broadcast, then `pthread_join`.
* IPC objects created (and the ONLY ones ever removed): shm `/cc_shm_campus`, semaphores `/cc_sem_mutex`, `/cc_sem_ready`
  (visible as `/dev/shm/cc_shm_campus`, `/dev/shm/sem.cc_sem_*`).
* Worker failures are SIMULATED (exit code 42 / SIGUSR1) and labelled as such - not real OS faults.
* Resource-limit demos run in a child process so the parent is unaffected.

## Test data (tests/)
`jobs.txt` (5 jobs, has a priority tie), `jobs_idle.txt` (CPU idle gap), `jobs_invalid.txt`, `safe.txt`, `unsafe.txt`, `banker_invalid.txt`.
Banker's file format: `n m`, Available, Maximum (n rows), Allocation (n rows); `#` begins a comment.

## Screenshot guide
| # | Command(s) |
|---|---|
| S01 | `pwd; cat /etc/os-release \| head -3` |
| S02 | `gcc --version \| head -1; make --version \| head -1` |
| S03 | `ls -R` (or `ls -l . src include tests`) |
| S04 | `make clean && make` |
| S05 | `./campuscompute help` |
| S06 | `./campuscompute sched fcfs tests/jobs.txt` |
| S07 | `./campuscompute sched rr tests/jobs.txt 3` |
| S08 | `./campuscompute sched prio tests/jobs.txt` |
| S09 | `./campuscompute sched compare tests/jobs.txt 3` |
| S10 | `./campuscompute logger 2 2 6` (top part: producer/consumer activity) |
| S11 | same run, bottom part: producers joined, consumers joined, summary; optional `make tsan && ./campuscompute_tsan logger 3 2 5` |
| S12 | `./campuscompute ipc` (parent/child exchange and checksum) |
| S13 | `./campuscompute ipc keep; ls /dev/shm \| grep cc_; ./campuscompute cleanup; ls /dev/shm \| grep cc_` |
| S14 | `./campuscompute banker tests/safe.txt` |
| S15 | `./campuscompute banker tests/unsafe.txt; echo "exit=$?"` |
| S16 | `./campuscompute worker normal` |
| S17 | `./campuscompute worker fail` (and/or `worker signal`) |
| S18 | `./campuscompute limits all` |
| S19 | `./campuscompute frobnicate; echo "exit=$?"` and a bad-input run |
| S20 | `ps aux \| grep [c]ampuscompute; ls /dev/shm \| grep cc_` after the runs |
| S21 | `./campuscompute demo` (summary at the end) |
| S22 | `ls -l logs; head logs/campus.log` |
| S23 | `ls -l . src include tests` (shows executable) |
| S24 | `zip -r PES1UGXXXXXXX_src.zip campuscompute/ -x '*.o'; unzip -l ...; unzip -t ...` |

Optional memory check: `valgrind --leak-check=full ./campuscompute sched fcfs tests/jobs.txt`

## Contribution

| Team member | Contribution |
|---|---|
| K V Divyansi (PES1UG25CS237) | Environment setup and build verification, CLI usage and error handling, CPU scheduling (FCFS, Round Robin, Priority) with policy comparison, Banker's safety algorithm (safe and unsafe states) |
| Johlee M Efroshini (PES1UG25CS231) | Producer-consumer logger with threads, shared-memory IPC with named semaphores, worker failure detection, resource limits, cleanup verification |
| Both | Integrated demo, final file listing and submission ZIP |
