#!/bin/sh
# OS-B10: OSTEP paging-linear-translate.py 의 답(-c)과 solution.c 의 translate 를 대조한다.
#   sh demo_ostep_check.sh            # 레슨 폴더에서 실행 (다른 cwd 에서도 됨)
#   OSTEP_SIM=/path/to/paging-linear-translate.py sh demo_ostep_check.sh
# 시뮬레이터는 읽기만 한다 (python3 -I). 임시 파일은 /tmp/OS-B10_work/ 에 만들고 끝나면 지운다.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SIM=${OSTEP_SIM:-$HERE/../../../../operating-system-three-easy-pieces/.tools/ostep-homework/vm-paging/paging-linear-translate.py}
WORK=/tmp/OS-B10_work
[ -f "$SIM" ] || { echo "OSTEP simulator not found: $SIM (set OSTEP_SIM)"; exit 2; }
mkdir -p "$WORK"
cc -std=c11 -Wall -Wextra -Werror -O2 "$HERE/solution.c" -o "$WORK/sol" || exit 2

total=0; bad=0; nva=0; nvalid=0
run() {   # $@ = 시뮬레이터 인자
    out=$WORK/ostep_out.txt
    python3 -I "$SIM" "$@" -c > "$out" 2>&1
    res=$("$WORK/sol" --ostep "$out")
    rc=$?
    line=$(printf '%s\n' "$res" | tail -1)
    total=$((total + 1))
    case "$line" in *"cross-check:"*) ;; *) rc=1 ;; esac
    [ $rc -eq 0 ] || bad=$((bad + 1))
    nva=$((nva + $(printf '%s\n' "$res" | grep -c '^  VA ')))
    nvalid=$((nvalid + $(printf '%s\n' "$res" | grep -c 'mine 0x')))
    printf '%-46s %s\n' "$*" "$line"
}

for s in 0 1 2 3 4 5 6 7 8 9; do run -s $s; done
for u in 0 25 50 75 100; do run -P 1k -a 16k -p 32k -v -u $u -s 0; done      # 숙제 Q2
run -P 8 -a 32 -p 1024 -v -s 1                                               # 숙제 Q3
run -P 8k -a 32k -p 1m -v -s 2
run -P 1m -a 256m -p 512m -v -s 3
run -P 4k -a 1m -p 512m -n 50 -u 60 -s 7                                     # 256 PTE, 50 VA
run -A 0,4095,4096,16383 -s 0                                                # 페이지 경계
echo "configs: $total, failed: $bad, translations: $nva ($nvalid valid, $((nva - nvalid)) invalid)"

echo
echo "--- out-of-range VA: OSTEP vs mine ---"
python3 -I "$SIM" -s 0 -A 16384 -c 2>&1 | tail -2
echo "mine: VA 0x4000 in a 16k space -> XL_RANGE (solution.c Part B CHECK)"
rm -f "$WORK/ostep_out.txt" "$WORK/sol"
rmdir "$WORK" 2>/dev/null
[ $bad -eq 0 ]
