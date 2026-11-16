#!/usr/bin/env bash
# TOOL-M07 demo_lldb.sh — 맥 lldb 실험 전부를 다시 돌려 이 디렉터리의 demo_*.txt 를 실제 출력으로 다시 만든다
#   bash demo_lldb.sh            (약 2~3분: 비용 측정이 대부분)
#   bash demo_lldb.sh quick      (비용 측정 생략)
set -u
cd "$(dirname "$0")" || exit 2
D=$(pwd)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-M07_work/demo
rm -rf "$W" && mkdir -p "$W"
cp buggy.c demo_wp.c demo_trap.c solution.py starter.py solution.lldb starter.lldb "$W"/
cd "$W" || exit 2
cc -std=c11 -g -O0 buggy.c -o buggy
cc -std=c11 -g -O0 demo_wp.c -o demo_wp
cc -std=c11 -g -O0 demo_trap.c -o demo_trap
L() { lldb -b "$@" < /dev/null 2>&1 | grep -v '^Current executable set to\|^(lldb) target create'; }

# ---- 1. 사냥 전체 기록 (solution / starter) ----
cp solution.py ringbuf_cmd.py; L -s solution.lldb ./buggy > "$D/demo_hunt.txt"
cp starter.py ringbuf_cmd.py;  L -s starter.lldb  ./buggy > "$D/demo_hunt_starter.txt"
echo "== demo_hunt.txt / demo_hunt_starter.txt"

# ---- 2. 하드웨어 워치포인트 개수 · 크기 · run 전 설정 ----
{
    echo "### sysctl"; sysctl hw.optional.breakpoint hw.optional.watchpoint
    echo "### (a) run 전에 watchpoint"
    L -o 'watchpoint set variable g' ./demo_wp
    echo "### (b) 8바이트 워치포인트 5개"
    L -o 'b main' -o run \
      -o 'watchpoint set expression -w write -s 8 -- &g.w[0]' -o 'watchpoint set expression -w write -s 8 -- &g.w[1]' \
      -o 'watchpoint set expression -w write -s 8 -- &g.w[2]' -o 'watchpoint set expression -w write -s 8 -- &g.w[3]' \
      -o 'watchpoint set expression -w write -s 8 -- &g.w[4]' ./demo_wp | sed -n '/-s 8 -- &g.w\[3\]/,$p'
    echo "### (c) 크기/정렬 → 실제 하드웨어 자원(resources)"
    for spec in "-s 1 -- &g.big[3]" "-s 3 -- &g.big[1]" "-s 8 -- &g.big[4]" "-s 16 -- &g.big[0]" "-s 24 -- &g.big[0]" "-s 64 -- &g.big[0]"; do
        L -o 'b main' -o run -o "watchpoint set expression -w write $spec" ./demo_wp \
          | awk '/^\(lldb\) watchpoint set/{p=1} p && /watchpoint set|Watchpoint created|#[0-9]+: addr/'
    done
    echo "### (d) 16바이트 경계를 걸치는 8바이트 (&g.big[12] = ...4c..53)"
    L -o 'b main' -o run -o 'watchpoint set expression -w write -s 8 -- &g.big[12]' ./demo_wp \
      | awk '/^\(lldb\) watchpoint set/{p=1} p && /watchpoint set|Watchpoint created|#[0-9]+: addr/'
} > "$D/demo_limits.txt"
echo "== demo_limits.txt"

# ---- 3. modify vs write, expression 기본 크기, 커널 쓰기 ----
cat > cnt.py <<'PY'
import lldb
N = [0]
def cb(frame, wp, d):            # 워치포인트 콜백: 부를 때마다 세고, 절대 멈추지 않음
    N[0] += 1
    return False
def count_stops(debugger, command, exe_ctx, result, d):   # 끝날 때까지 continue 하며 정지 횟수 세기
    debugger.SetAsync(False)
    p = exe_ctx.GetProcess()
    n = 0
    while n < 5000:
        p.Continue()
        if p.GetState() != lldb.eStateStopped:
            break
        n += 1
    result.AppendMessage("stops = %d, final state = %s" % (n, lldb.SBDebugger.StateAsCString(p.GetState())))
PY
{
    for t in modify write; do
        echo "### -w $t : same = 5 (x1000) 다음 same = 6 → 사용자에게 보이는 정지 횟수 / 하드웨어 히트(콜백 호출) 횟수"
        L -o 'command script import cnt.py' -o 'command script add -f cnt.count_stops count_stops' \
          -o 'b main' -o 'run modify' -o "watchpoint set expression -w $t -s 4 -- &same" -o count_stops ./demo_wp \
          | grep -E 'type = |stops ='
        L -o 'command script import cnt.py' -o 'b main' -o 'run modify' -o "watchpoint set expression -w $t -s 4 -- &same" \
          -o 'watchpoint command add -F cnt.cb 1' -o continue -o 'script print("callback calls =", cnt.N[0])' ./demo_wp \
          | grep -E '^callback calls'
    done
    echo "### expression 워치포인트 -s 생략 → 기본 크기 = 포인터 크기 8 → 옆 필드(counter) 쓰기에도 멈춤"
    L -o 'b main' -o 'run neighbour' -o 'watchpoint set expression -- &p.state' -o continue ./demo_wp \
      | grep -E 'watchpoint set|Watchpoint created|stop reason|^-> '
    echo "### variable 워치포인트는 변수 크기(4)"
    L -o 'b main' -o 'run neighbour' -o 'watchpoint set variable p.state' -o continue ./demo_wp \
      | grep -E 'watchpoint set|Watchpoint created|stop reason|^-> '
    echo "### 커널(read 시스템 콜)이 쓴 것은 안 잡힌다"
    L -o 'b main' -o 'run kernel' -o 'watchpoint set expression -w write -s 4 -- &kbuf[0]' -o continue -o 'p/x kbuf' ./demo_wp \
      | grep -E 'kernel wrote|user wrote|stop reason|^-> |\(unsigned char\[16\]\)'
} > "$D/demo_modify.txt"
echo "== demo_modify.txt"

# ---- 4. lldb 명령줄 옵션: -o / -s / -k, 여러 줄 breakpoint command, -i ignore count ----
cat > multi.lldb <<'LL'
breakpoint set -n rb_put_escaped -c 'rb->head == 15'
breakpoint command add 1
p g_tick
p rb->count
continue
DONE
run
LL
{
    echo '### lldb -b -o run -k bt -k "frame variable" ./demo_trap   (-k = 멈춘 이유가 크래시일 때만)'
    L -o run -k 'bt' -k 'frame variable' ./demo_trap
    echo '### 같은 명령을 정상 종료 프로그램에: -k 는 실행 안 됨'
    L -o run -k 'bt' ./demo_wp
    echo '### 여러 줄 breakpoint command (소스 파일 안에서 DONE 으로 끝)'
    L -s multi.lldb ./buggy
    echo '### -i ignore count: rb_put_escaped 31번째 호출에서 멈춤'
    L -o 'breakpoint set -n rb_put_escaped -i 30' -o run -o 'p g_tick' -o 'breakpoint list 1' ./buggy \
      | grep -E 'Breakpoint 1:|stop reason|^\(uint32_t\)|hit count|ignore'
    echo '### 가설이 선 뒤의 빠른 길: 조건부 BP 로 그 순간까지 → 워치포인트 1번 → 범인 명령어 디스어셈블'
    L -o "breakpoint set -n rb_put_escaped -c 'rb->head == 15'" -o run -o 'p g_tick' \
      -o 'watchpoint set expression -w write -s 4 -- &dev.link_state' -o continue \
      -o "disassemble -s '\$pc-12' -c 4" -o 'register read x8 x9 x10 x11' ./buggy \
      | sed -n '/^(lldb) p g_tick/,$p'
} > "$D/demo_options.txt"
echo "== demo_options.txt"

# ---- 5. 고친 버전: % RB_CAP 하나 ----
sed 's/rb->buf\[rb->head + 1\] = /rb->buf[(rb->head + 1) % RB_CAP] = /' buggy.c > fixed.c
cc -std=c11 -g -O0 fixed.c -o fixed
{ diff buggy.c fixed.c; ./fixed; echo "rc=$?"; } > "$D/demo_fix.txt" 2>&1
echo "== demo_fix.txt"

[ "${1:-}" = quick ] && exit 0

# ---- 6. 비용: 조건부 BP / 워치포인트는 "멈출 때마다" 디버거 왕복 ----
cat > wpcb.py <<'PY'
import lldb
N = [0]
def cb(frame, wp, d):
    N[0] += 1
    e = lldb.SBError()
    v = frame.GetThread().GetProcess().ReadUnsignedFromMemory(wp.GetWatchAddress(), 4, e)
    return v > 3
PY
med() { sort -n | awk '{a[NR]=$1} END {print a[int((NR+1)/2)]}'; }
t() { local s e; s=$(python3 -c 'import time; print(time.time())'); "$@" > /dev/null 2>&1 < /dev/null
      e=$(python3 -c 'import time; print(time.time())'); python3 -c "print('%.2f' % ($e - $s))"; }
{
    echo "# 각 줄 = 3회 실행 중앙값 (초). 다른 작업과 같이 도는 맥이라 ±20% 정도 흔들린다."
    printf '%-62s %s\n' "case" "sec"
    r=$(for i in 1 2 3 4 5; do t ./buggy; done | med); printf '%-62s %s\n' "native ./buggy (7343 tick 끝까지)" "$r"
    r=$(for i in 1 2 3; do t lldb -b -o run ./buggy; done | med); printf '%-62s %s\n' "lldb run, BP/WP 없음" "$r"
    r=$(for i in 1 2 3; do t lldb -b -o 'br set -n link_task -i 999' -o run -o 'p g_tick' -o 'process kill' ./buggy; done | med)
    printf '%-62s %s\n' "BP link_task -i 999 (999번 멈춤, 조건 식 없음)" "$r"
    r=$(for i in 1 2 3; do t lldb -b -o "br set -n link_task -c 'g_tick == 1000'" -o run -o 'p g_tick' -o 'process kill' ./buggy; done | med)
    printf '%-62s %s\n' "BP link_task -c 'g_tick == 1000' (식 1000번 평가)" "$r"
    r=$(for i in 1 2 3; do t lldb -b -o "br set -n rb_put_escaped -c 'rb->head == 15'" -o run -o 'p g_tick' -o 'process kill' ./buggy; done | med)
    printf '%-62s %s\n' "BP rb_put_escaped -c 'rb->head == 15' (드문 함수)" "$r"
    r=$(for i in 1 2 3; do t lldb -b -o 'b main' -o run -o 'watchpoint set variable dev.link_state' \
          -o "watchpoint modify -c 'dev.link_state > 3'" -o continue -o 'process kill' ./buggy; done | med)
    printf '%-62s %s\n' "WP dev.link_state + -c 'dev.link_state > 3' (1827 히트)" "$r"
    r=$(for i in 1 2 3; do t lldb -b -o 'command script import wpcb.py' -o 'b main' -o run -o 'watchpoint set variable dev.link_state' \
          -o 'watchpoint command add -F wpcb.cb 1' -o continue -o 'process kill' ./buggy; done | med)
    printf '%-62s %s\n' "WP dev.link_state + Python 콜백 (1827 히트)" "$r"
    echo "# 히트 수 확인"
    lldb -b -o 'b main' -o run -o 'watchpoint set variable dev.link_state' -o "watchpoint modify -c 'dev.link_state > 3'" \
         -o continue -o 'script print("wp hits =", lldb.target.GetWatchpointAtIndex(0).GetHitCount())' -o 'process kill' ./buggy 2>&1 | grep 'wp hits'
    lldb -b -o "br set -n link_task -c 'g_tick == 1000'" -o run -o 'breakpoint list 1' -o 'process kill' ./buggy 2>&1 | grep -m1 'hit count'
    lldb -b -o 'br set -n link_task -i 999' -o run -o 'breakpoint list 1' -o 'process kill' ./buggy 2>&1 | grep -m1 'hit count'
} > "$D/demo_cost.txt"
echo "== demo_cost.txt"
