#!/usr/bin/env bash
# TOOL-B10 demo_debug.sh — 디버그 빌드 + lldb 배치 세션 8개 (Mac) + gdb 원격 세션 1개 (Lima VM)
#   bash demo_debug.sh        → 이 디렉터리의 demo_*.txt 를 실제 출력으로 다시 만든다
set -u
cd "$(dirname "$0")"
D=$(pwd)
W=/tmp/TOOL-B10_work/demo
rm -rf "$W" && mkdir -p "$W"
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cp starter.c demo_practice_bug.c demo_*.lldb "$W"/
# Part C 데모용: starter 의 버그는 그대로 두고 list_check 본문만 solution 것으로 갈아 끼운다
awk 'FNR == NR { if ($0 ~ /==== list_check begin/) f = 1; if (f) body = body $0 "\n";
                 if ($0 ~ /==== list_check end/) f = 0; next }
     $0 ~ /==== list_check begin/ { printf "%s", body; skip = 1; next }
     skip { if ($0 ~ /==== list_check end/) skip = 0; next }
     { print }' solution.c starter.c > "$W/verify.c"
# Part B 데모용 중간 상태: solution 에서 "continue;" 한 줄만 지움 = 버그 1(해제 후 next)만 고치고 버그 2(prev)는 남김
grep -v 'continue;  *[/][*] 지웠으면 prev' solution.c > "$W/midfix.c"
cd "$W"
cc -std=c11 -g -O0 starter.c -o starter_dbg
cc -std=c11 -g -O0 -DVERIFY midfix.c -o midfix_dbg
cc -std=c11 -O2 midfix.c -o midfix_o2
cc -std=c11 -g -O0 demo_practice_bug.c -o pbug
cc -std=c11 -g -O0 -DVERIFY verify.c -o verify_dbg
cc -std=c11 -O2 -DVERIFY verify.c -o verify_o2

session() {   # session <out.txt> <lldb args...>
    local out=$1; shift
    lldb -b "$@" < /dev/null > "$D/$out" 2>&1
    echo "== $out (lldb rc=$?, $(wc -l < "$D/$out") lines)"
}
session demo_fork.txt       -o run -- ./starter_dbg
session demo_crash.txt      -s demo_crash.lldb -- ./starter_dbg delete_head
session demo_debug.txt      -s demo_debug.lldb -- ./starter_dbg delete_head
session demo_delete_all.txt -s demo_delete_all.lldb -- ./starter_dbg delete_all_mixed
session demo_verify.txt     -s demo_verify.lldb -- ./verify_dbg delete_head
session demo_prev.txt       -s demo_prev.lldb -- ./midfix_dbg delete_all_mixed
session demo_practice.txt   -s demo_practice.lldb -- ./pbug nth
session demo_practice_sorted.txt -o run -k "frame variable" -k "p *cur" -k "p cur->next" -- ./pbug sorted
./verify_o2 > "$D/demo_verify_run.txt" 2>&1
echo "== demo_verify_run.txt (rc=$?)"
./midfix_o2 > "$D/demo_midfix_run.txt" 2>&1
echo "== demo_midfix_run.txt (rc=$?)"

# ---- Lima VM: 같은 세션을 gdb-multiarch + qemu-aarch64 gdbstub 로 ----
if command -v limactl > /dev/null 2>&1; then
    limactl shell elinux -- bash -lc "
        set -u
        V=/tmp/TOOL-B10_work/gdb; rm -rf \$V; mkdir -p \$V; cd \$V
        cp '$D/starter.c' '$D/demo_gdb.gdb' .
        gcc -std=c11 -g -O0 -static starter.c -o starter_g
        qemu-aarch64 -g 23410 ./starter_g delete_head > qemu_out.txt 2>&1 &
        sleep 1
        gdb-multiarch -q -batch -x demo_gdb.gdb ./starter_g 2>&1
        wait
    " > "$D/demo_gdb.txt" 2>&1
    echo "== demo_gdb.txt (rc=$?, $(wc -l < "$D/demo_gdb.txt") lines)"
fi
