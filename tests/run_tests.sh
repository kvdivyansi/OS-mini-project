#!/bin/bash
# Automated self-check: builds nothing, just runs each feature and checks the result.
cd "$(dirname "$0")/.." || exit 1
B=./campuscompute; pass=0; fail=0
check() { # description, expected-exit, grep-pattern, command...
  local d="$1" e="$2" p="$3"; shift 3
  out=$("$@" 2>&1); rc=$?
  if [ "$rc" -eq "$e" ] && echo "$out" | grep -q -- "$p"; then echo "PASS  $d"; pass=$((pass+1)); else echo "FAIL  $d (rc=$rc)"; fail=$((fail+1)); fi
}
check "FCFS average waiting 7.00"      0 "Average Waiting = 7.00" $B sched fcfs tests/jobs.txt
check "Priority runs"                  0 "PRIORITY Scheduling"   $B sched prio tests/jobs.txt
check "RR quantum 3 shown"             0 "time quantum = 3"      $B sched rr tests/jobs.txt 3
check "Idle gap handled"               0 "idle 2-6"              $B sched fcfs tests/jobs_idle.txt
check "Compare table"                  0 "Round Robin"           $B sched compare tests/jobs.txt
check "Invalid job file rejected"      1 "burst"                 $B sched fcfs tests/jobs_invalid.txt
check "Bad quantum rejected"           1 "quantum"               $B sched rr tests/jobs.txt abc
check "Logger"                         0 "RESULT: PASS"          $B logger 2 2 6
check "IPC"                            0 "RESULT: PASS"          $B ipc
check "Banker safe"                    0 "SAFE state"            $B banker tests/safe.txt
check "Banker unsafe"                  2 "UNSAFE state"          $B banker tests/unsafe.txt
check "Banker invalid file"            1 "error"                 $B banker tests/banker_invalid.txt
check "Worker all"                     0 "3/3 workers"           $B worker all
check "Limits nofile"                  0 "EMFILE\|Too many open" $B limits nofile
check "Unknown command"                1 "unknown command"       $B frobnicate
check "Cleanup"                        0 "Cleanup"               $B cleanup
echo "---- $pass passed, $fail failed ----"; [ "$fail" -eq 0 ]
