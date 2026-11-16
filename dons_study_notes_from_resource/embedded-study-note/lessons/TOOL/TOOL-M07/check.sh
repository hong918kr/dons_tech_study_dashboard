#!/usr/bin/env bash
# TOOL-M07 check.sh — bash check.sh <starter|solution|practice>
#   <variant>.py   → ringbuf_cmd.py 로 복사 (rbdump 명령 + 요약 제공자)
#   <variant>.lldb → 범인 찾기 세션 (lldb -b -s)
# 맥 전용 (lldb + 하드웨어 워치포인트). 작업 폴더 /tmp/TOOL-M07_work/<variant>, 약 15~25 초.
set -u
cd "$(dirname "$0")" || exit 2
V=${1:-}
case "$V" in
starter|solution|practice) ;;
*) echo "usage: bash check.sh <starter|solution|practice>"; exit 2 ;;
esac
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
SRC=$(pwd)
W=/tmp/TOOL-M07_work/$V
rm -rf "$W" && mkdir -p "$W"
cp buggy.c rbstates.c "$W"/
cp "$V.py" "$W/ringbuf_cmd.py"
cp "$V.lldb" "$W/hunt.lldb"
cd "$W" || exit 2

pass=0 total=0
check() {   # check "<이름>" <명령...>   (명령이 성공하면 PASS)
    local name=$1; shift
    total=$((total + 1))
    if "$@" > /dev/null 2>&1; then pass=$((pass + 1)); echo "PASS $name"; else echo "FAIL $name"; fi
}
has()   { grep -qF -- "$2" "$1"; }          # has <file> <고정 문자열>
hasre() { grep -qE -- "$2" "$1"; }          # hasre <file> <정규식>
lacks() { ! grep -qF -- "$2" "$1"; }

# ---- 0. 빌드 ------------------------------------------------------------
cc -std=c11 -Wall -Wextra -Werror -g -O0 buggy.c -o buggy 2> build.txt
cc -std=c11 -Wall -Wextra -Werror -g -O0 rbstates.c -o rbstates 2>> build.txt
check "build buggy + rbstates (-g -O0, 0 warnings)" test -x buggy -a -x rbstates -a ! -s build.txt
./buggy > run.txt 2>&1
check "symptom: ./buggy reports 'tick 7343: bad link_state 0xdd'" has run.txt "tick 7343: bad link_state 0xdd"

# ---- 1. 단위: 파이썬 순수 함수 + SBValue 읽기 (TODO 1~4 를 하나씩 따로 본다) ----
cat > t_unit.py <<'PY'
import lldb
import ringbuf_cmd as m

def safe(fn):
    try:
        return fn()
    except Exception as e:                    # starter 의 버그가 예외로 나와도 다음 항목은 계속
        return "EXC %s: %s" % (type(e).__name__, e)

def rep(name, got, exp):
    print("UNIT %s %s got=%r" % (name, "OK" if got == exp else "BAD", got))

def __lldb_init_module(debugger, internal_dict):
    t = debugger.GetSelectedTarget()
    g = t.FindFirstGlobalVariable
    RB = m.RB
    z = [0] * 16
    rep("order_wrapped", safe(lambda: m.logical_slots(RB(0, 3, 12, 7, 16, z))), [12, 13, 14, 15, 0, 1, 2])
    rep("order_linear",  safe(lambda: m.logical_slots(RB(0, 7, 2, 5, 16, z))), [2, 3, 4, 5, 6])
    rep("order_full",    safe(lambda: m.logical_slots(RB(0, 4, 4, 16, 16, z))), [(4 + i) % 16 for i in range(16)])
    rep("state_full",    safe(lambda: m.rb_state(RB(0, 4, 4, 16, 16, z))), "full")
    rep("state_empty",   safe(lambda: m.rb_state(RB(0, 9, 9, 0, 16, z))), "empty")
    rep("state_wrapped", safe(lambda: m.rb_state(RB(0, 3, 12, 7, 16, z))), "wrapped")
    rep("state_linear",  safe(lambda: m.rb_state(RB(0, 7, 2, 5, 16, z))), "linear")
    buf = g("rb_slip").GetNonSyntheticValue().GetChildMemberWithName("buf")   # 합성 자식(practice)이 있어도 원래 멤버
    rep("bytes_slip",    safe(lambda: m.read_bytes(buf)[14:16] + m.read_bytes(buf)[0:2]), [0xC0, 0xDB, 0x00, 0x7F])
    want = g("rb_wrapped").GetLoadAddress()
    rep("addr_value",    safe(lambda: m.rb_address(g("rb_wrapped"))), want)
    rep("addr_pointer",  safe(lambda: m.rb_address(g("prb"))), want)
PY
lldb -b -o 'breakpoint set -n checkpoint' -o run -o 'command script import ringbuf_cmd.py' \
     -o 'command script import t_unit.py' ./rbstates > unit.txt 2>&1
unit_ok() { for n in "$@"; do has unit.txt "UNIT $n OK" || return 1; done; }
check "TODO(1) logical order: tail → wrap → head (wrapped/linear/full)" unit_ok order_wrapped order_linear order_full
check "TODO(2) empty vs full when head == tail (count decides)"         unit_ok state_full state_empty state_wrapped state_linear
check "TODO(3) uint8_t elements read as ints (0xc0 0xdb 0x00 0x7f)"     unit_ok bytes_slip
check "TODO(4) address of struct and of struct * (same 0x…)"           unit_ok addr_value addr_pointer

# ---- 2. 통합: rbdump 출력과 요약을 그대로 비교 ----------------------------
lldb -b -o 'breakpoint set -n checkpoint' -o run -o 'command script import ringbuf_cmd.py' \
     -o 'p &rb_wrapped' -o 'rbdump rb_wrapped' -o 'rbdump prb' -o 'rbdump rb_full' -o 'rbdump rb_empty' \
     -o 'p rb_wrapped' -o 'p prb' -o 'p rb_empty' -o 'frame variable rb_slip' ./rbstates > fmt.txt 2>&1
A=$(sed -n 's/^(ringbuf \*) 0x0*\([0-9a-f]*\) .*/\1/p' fmt.txt | head -1)
awk '/^\(lldb\) rbdump rb_wrapped$/ {f=1; next} /^\(lldb\)/ {f=0} f' fmt.txt > got_wrapped.txt
cat > want_wrapped.txt <<TXT
ringbuf @ 0x$A cap=16 count=7 tail=12 head=3 wrapped
  [0] buf[12] = 0x41
  [1] buf[13] = 0x42
  [2] buf[14] = 0x43
  [3] buf[15] = 0x44
  [4] buf[0] = 0x45
  [5] buf[1] = 0x46
  [6] buf[2] = 0x47
TXT
awk '/^\(lldb\) rbdump prb$/ {f=1; next} /^\(lldb\)/ {f=0} f' fmt.txt > got_prb.txt
check "rbdump rb_wrapped: exact text"                        cmp -s got_wrapped.txt want_wrapped.txt
check "rbdump prb (pointer): same text as rb_wrapped"        cmp -s got_prb.txt want_wrapped.txt
full_ok() { awk '/^\(lldb\) rbdump rb_full$/ {f=1; next} /^\(lldb\)/ {f=0} f' fmt.txt > got_full.txt
            grep -q ' cap=16 count=16 tail=4 head=4 full$' got_full.txt && [ "$(grep -c '^  \[' got_full.txt)" = 16 ] \
            && grep -q '^  \[15\] buf\[3\] = 0x66$' got_full.txt; }
check "rbdump rb_full: 'full' + 16 elements, last = buf[3]"   full_ok
empty_ok() { awk '/^\(lldb\) rbdump rb_empty$/ {f=1; next} /^\(lldb\)/ {f=0} f' fmt.txt > got_empty.txt
             [ "$(wc -l < got_empty.txt)" -eq 1 ] && grep -qE '^ringbuf @ 0x[0-9a-f]+ cap=16 count=0 tail=9 head=9 empty$' got_empty.txt; }
check "rbdump rb_empty: one header line ending 'empty'"      empty_ok
check "summary: p rb_wrapped"   has fmt.txt "(ringbuf) count=7/16 tail=12 head=3 [41 42 43 44 45 46 47]"
check "summary: p prb (through the pointer)" hasre fmt.txt "^\(ringbuf \*\) 0x0*$A count=7/16 tail=12 head=3 \[41 42 43 44 45 46 47\]$"
sum2_ok() { has fmt.txt '(ringbuf) count=0/16 tail=9 head=9 empty' && has fmt.txt '(ringbuf) rb_slip = count=4/16 tail=14 head=2 [c0 db 00 7f]'; }
check "summary: p rb_empty / frame variable rb_slip" sum2_ok
check "no Python traceback while formatting" lacks fmt.txt "Traceback"
lldb -b -o 'breakpoint set -n checkpoint' -o run -o 'command script import ringbuf_cmd.py' \
     -o 'rbdump nosuch_var' ./rbstates > err.txt 2>&1
err_ok() { has err.txt "error: rbdump: cannot evaluate 'nosuch_var'" && lacks err.txt Traceback; }
check "rbdump on a bad expression: clean 'error:' (no traceback)" err_ok

# ---- 3. 사냥: <variant>.lldb 로 범인 줄 찾기 ------------------------------
CULPRIT=$(grep -n 'rb->buf\[rb->head + 1\]' buggy.c | cut -d: -f1)
lldb -b -s hunt.lldb ./buggy > hunt.txt 2>&1
stop2() { awk '/stop reason = /{n++; if (n == 2) {print; getline; print; exit}}' hunt.txt; }   # 1번째 = main
hunt_stop_ok() { stop2 | grep -q 'stop reason = watchpoint' && stop2 | grep -q 'rb_put_escaped'; }
check "hunt: first stop after main = the watchpoint, inside rb_put_escaped" hunt_stop_ok
check "hunt: culprit = instruction before PC → buggy.c:$CULPRIT" hasre hunt.txt "Summary: buggy\`rb_put_escaped \+ [0-9]+ at buggy\.c:$CULPRIT:"
tick_ok() { [ "$(awk '/^\(lldb\) p g_tick$/{getline; print; exit}' hunt.txt)" = "(uint32_t) 6800" ]; }
check "hunt: corruption happened at tick 6800 (first, silent), not 7343" tick_ok
check "hunt: rbdump rb at the stop" \
      hasre hunt.txt "^ringbuf @ 0x[0-9a-f]+ cap=16 count=2 tail=13 head=15 linear$"
second_ok() { grep -qx '(uint32_t) 7342' hunt.txt && has hunt.txt 'tick 7343: bad link_state 0xdd' && has hunt.txt 'exited with status = 1'; }
check "hunt: 2nd dangerous call seen at tick 7342, program then reports 7343" second_ok

# ---- 4. practice 전용 ---------------------------------------------------
if [ "$V" = practice ]; then
    lldb -b -o 'breakpoint set -n checkpoint' -o run -o 'command script import ringbuf_cmd.py' \
         -o 'rbcheck rb_wrapped' -o 'rbcheck rb_bad' -o 'frame variable -f x rb_wrapped[0] rb_wrapped[3] rb_wrapped[6]' \
         -o 'frame variable -Y1 -f x rb_full' -o 'p rb_wrapped' ./rbstates > prac.txt 2>&1
    p1_ok() { grep -qx 'rbcheck rb_wrapped: OK' prac.txt \
              && grep -qxF 'rbcheck rb_bad: CORRUPT: count 20 > cap 16; head 15 != (tail + count) % cap = 1' prac.txt; }
    check "practice ① rbcheck: OK / CORRUPT with both reasons" p1_ok
    p2_ok() { grep -qxF '(uint8_t) rb_wrapped[0] = 0x41' prac.txt && grep -qxF '(uint8_t) rb_wrapped[3] = 0x44' prac.txt \
              && grep -qxF '(uint8_t) rb_wrapped[6] = 0x47' prac.txt; }
    check "practice ② synthetic children: rb_wrapped[0]/[3]/[6] = 0x41/0x44/0x47 (logical index)" p2_ok
    check "practice ② summary still works next to synthetic children" \
          has prac.txt "(ringbuf) count=7/16 tail=12 head=3 [41 42 43 44 45 46 47]"
    check "practice ③ Python watchpoint callback stopped on the bad write" hasre hunt.txt '^\[wp\] link_state=0xdc after [0-9]+ hits$'
    check "practice ④ Python breakpoint callback stopped at the last slot" hasre hunt.txt '^\[bp\] rb_put_escaped at the last slot \(head=15\) after [0-9]+ calls$'
    check "practice: rbcheck at the corruption = OK (the damage is OUTSIDE the ring buffer)" has hunt.txt "rbcheck rb: OK"
fi

echo "$pass/$total passed"
[ "$pass" -eq "$total" ]
