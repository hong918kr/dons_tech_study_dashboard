#!/bin/sh
# OS-B12 demo_ostep_check.sh — 내 시뮬레이터(solution.c)를 OSTEP 공식 숙제 시뮬레이터와 tick 단위로 비교한다.
#   scheduler.py (FIFO/SJF/RR, 모두 0 에 도착, I/O 없음): 작업별 Response/Turnaround/Wait + 평균 줄 비교
#   mlfq.py      (도착·I/O·부스트·-S): "Run JOB j at PRIORITY p" 매 tick 줄 + Final statistics 줄 비교
# 실행: sh demo_ostep_check.sh        (어느 디렉터리에서든. OSTEP 숙제 위치는 OSTEP_HW 로 바꿀 수 있다)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
HW=${OSTEP_HW:-$HERE/../../../../operating-system-three-easy-pieces/.tools/ostep-homework}
SCHED=$HW/cpu-sched/scheduler.py
MLFQ=$HW/cpu-sched-mlfq/mlfq.py
[ -f "$SCHED" ] && [ -f "$MLFQ" ] || { echo "OSTEP homework not found at $HW"; exit 2; }
W=$(mktemp -d /tmp/OS-B12_check.XXXXXX)
trap 'rm -rf "$W"' EXIT
cc -std=c11 -Wall -Wextra -Werror -O2 "$HERE/demo_ostep.c" -o "$W/demo_ostep"
ok=0; bad=0

cmp_out() {   # $1 = 이름, $2 = 기대(파이썬), $3 = 내 것
    if diff "$2" "$3" > "$W/diff" 2>&1; then ok=$((ok + 1)); else bad=$((bad + 1)); echo "DIFF $1"; head -5 "$W/diff"; fi
}

# ---- scheduler.py: FIFO / SJF / RR(q=1,3) × seed 0..9, 작업 5개 ----
for seed in 0 1 2 3 4 5 6 7 8 9; do
  for pq in "FIFO 1" "SJF 1" "RR 1" "RR 3"; do
    set -- $pq
    python3 -I "$SCHED" -p "$1" -q "$2" -s "$seed" -j 5 -c > "$W/py"
    lens=$(sed -n 's/^  Job [0-9]* ( length = \([0-9]*\) )$/\1/p' "$W/py" | paste -sd, -)
    grep -E '^  Job .* -- Response' "$W/py" | sort > "$W/a";  grep 'Average' "$W/py" >> "$W/a"
    "$W/demo_ostep" sched "$1" "$2" "$lens" > "$W/mine"
    grep -E '^  Job .* -- Response' "$W/mine" | sort > "$W/b"; grep 'Average' "$W/mine" >> "$W/b"
    cmp_out "scheduler.py -p $1 -q $2 -s $seed -j 5 (lens $lens)" "$W/a" "$W/b"
  done
done
echo "scheduler.py: $ok match, $bad differ"
ok1=$ok; bad1=$bad; ok=0; bad=0

# ---- mlfq.py: 랜덤 작업 (seed 0..9) × 설정 5개 ----
mlfq_case() {   # $1 = 이름, 나머지 = mlfq.py 옵션 (-l 없을 때는 랜덤 작업을 파싱해 넘긴다)
    name=$1; shift
    python3 -I "$MLFQ" "$@" -c > "$W/py"
    jl=$(sed -n 's/^  Job *[0-9]*: startTime *\([0-9]*\) - runTime *\([0-9]*\) - ioFreq *\([0-9]*\)$/\1,\2,\3/p' "$W/py" | paste -sd: -)
    # mlfq.py 옵션에서 -s/-j/-m/-M/-l 은 빼고 나머지를 그대로 넘긴다
    opts=""; skip=0
    for o in "$@"; do
        if [ $skip = 1 ]; then skip=0; continue; fi
        case $o in -s|-j|-m|-M|-l) skip=1 ;; *) opts="$opts $o" ;; esac
    done
    sed -n 's/^\(\[ time [0-9]* \] Run JOB [0-9]* at PRIORITY [0-9]*\).*/\1/p' "$W/py" > "$W/a"
    grep -E '^  (Job .* - response|Avg )' "$W/py" >> "$W/a"
    # shellcheck disable=SC2086
    "$W/demo_ostep" mlfq $opts -l "$jl" > "$W/mine"
    { grep 'Run JOB' "$W/mine"; grep -E '^  (Job .* - response|Avg )' "$W/mine"; } > "$W/b"
    cmp_out "$name" "$W/a" "$W/b"
}
for seed in 0 1 2 3 4 5 6 7 8 9; do
  mlfq_case "seed $seed default"         -s $seed -n 3 -q 10
  mlfq_case "seed $seed boost 50"        -s $seed -n 3 -q 10 -B 50
  mlfq_case "seed $seed Q 10,20,40 A 2,2,1 B 100" -s $seed -Q 10,20,40 -A 2,2,1 -B 100
  mlfq_case "seed $seed -S a 2"          -s $seed -n 3 -q 10 -a 2 -S
  mlfq_case "seed $seed n2 q5 a3 i3 j4"  -s $seed -j 4 -n 2 -q 5 -a 3 -i 3
done
# ---- mlfq.py: 레슨에 나오는 고정 작업 목록 ----
mlfq_case "lesson Part B (arrivals+io+boost)" -n 3 -q 10 -i 5 -B 100 -l 0,120,0:30,40,7:50,25,3
mlfq_case "lesson Part C starvation old"      -n 3 -q 10 -i 1 -S       -l 0,200,0:20,100,1:20,100,1
mlfq_case "lesson Part C starvation boost"    -n 3 -q 10 -i 1 -S -B 50 -l 0,200,0:20,100,1:20,100,1
mlfq_case "lesson Part C starvation new"      -n 3 -q 10 -i 1          -l 0,200,0:20,100,1:20,100,1
mlfq_case "lesson Part C gaming old"          -n 3 -q 10 -i 1 -S       -l 0,300,0:0,300,9
mlfq_case "lesson Part C gaming new"          -n 3 -q 10 -i 1          -l 0,300,0:0,300,9
mlfq_case "lesson Part C gaming old+boost"    -n 3 -q 10 -i 1 -S -B 100 -l 0,300,0:0,300,9
# -S 모서리: slice 가 끝나는 바로 그 tick 에 I/O (mlfq.py 는 "리필 → 차감" 순서라 강등되지 않는다)
mlfq_case "lesson -S corner: I/O exactly at slice end" -n 3 -q 10 -a 2 -i 5 -S -l 0,60,20:0,60,0
echo "mlfq.py:      $ok match, $bad differ"
[ $((bad + bad1)) = 0 ] && echo "ALL MATCH ($((ok + ok1)) cases)"
