#!/bin/sh
# OS-B11 — OSTEP vm-beyondphys-policy/paging-policy.py 와 solution.c 를 대조한다.
# 레슨 폴더에서:  sh demo_ostep_check.sh
# OSTEP 시뮬레이터 위치가 다르면:  OSTEP_POLICY=/path/to/paging-policy.py sh demo_ostep_check.sh
set -u
cd "$(dirname "$0")" || exit 2
PY=${OSTEP_POLICY:-../../../../operating-system-three-easy-pieces/.tools/ostep-homework/vm-beyondphys-policy/paging-policy.py}
[ -f "$PY" ] || { echo "paging-policy.py not found: $PY"; exit 2; }
BIN=$(mktemp /tmp/OS-B11_ostep.XXXXXX) || exit 2
trap 'rm -f "$BIN"' EXIT
cc -std=c11 -Wall -Wextra -Werror -O2 demo_ostep.c -o "$BIN" || exit 2

fail=0; runs=0
run() {   # run <args for paging-policy.py>
    runs=$((runs + 1))
    python3 -I "$PY" "$@" -c | "$BIN" || fail=$((fail + 1))
}
echo "== OSTEP 22장 예제 / Belady 문자열 =="
for p in FIFO LRU OPT; do run -a 0,1,2,0,1,3,0,3,1,2,1 -C 3 -p $p; done
for c in 3 4; do run -a 1,2,3,4,1,2,5,1,2,3,4,5 -C $c -p FIFO; done
echo "== 무작위: seed 0..9, 40 참조, 페이지 0..9, 캐시 3/4/5 =="
for s in 0 1 2 3 4 5 6 7 8 9; do
    for c in 3 4 5; do
        for p in FIFO LRU OPT; do run -s $s -n 40 -m 10 -C $c -p $p; done
    done
done
echo "== CLOCK (참고: OSTEP 은 무작위 스캔 + 2비트 카운터) =="
for s in 0 1 2; do run -s $s -n 40 -m 10 -C 4 -p CLOCK; done
echo "runs=$runs mismatches=$fail"
[ $fail -eq 0 ]
