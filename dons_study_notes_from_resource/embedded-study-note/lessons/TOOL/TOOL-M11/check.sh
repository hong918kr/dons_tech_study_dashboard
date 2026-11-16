#!/usr/bin/env bash
# TOOL-M11 checker — bash check.sh <starter|solution|practice>
# 1. builds crash.c (-g -O0) into /tmp/TOOL-M11_work/<variant>, signs it with get-task-allow
# 2. runs it under `ulimit -c unlimited` -> the kernel writes /cores/core.<pid> (~7 GB on macOS 27!)
# 3. runs the variant's post-mortem script on that core with `lldb -b -c`
# 4. DELETES the core (also on Ctrl-C / error), then greps the lldb transcript for the facts.
set -u
cd "$(dirname "$0")" || exit 2
V=${1:-}
case "$V" in starter|solution|practice) ;; *) echo "usage: bash check.sh <starter|solution|practice>"; exit 2 ;; esac
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
L=$(pwd)
W=/tmp/TOOL-M11_work/$V
rm -rf "$W" && mkdir -p "$W" || exit 2
pass=0; total=0
ok()  { total=$((total + 1)); pass=$((pass + 1)); echo "PASS $1"; }
bad() { total=$((total + 1)); echo "FAIL $1"; [ -n "${2:-}" ] && echo "     $2"; return 0; }
has() { grep -Eq -- "$1" "$W/lldb.txt"; }
CORE=
trap '[ -n "$CORE" ] && rm -f "$CORE"' EXIT INT TERM

echo "== $V: $(lldb --version | head -1), $(sw_vers -productVersion) =="
# ---- C1 build + sign ------------------------------------------------------
cc -std=c11 -g -O0 -Wall -Wextra -Werror crash.c -o "$W/crash" || { bad "build crash.c"; echo "$pass/$total passed"; exit 1; }
codesign -s - -f --entitlements ent.plist "$W/crash" 2>/dev/null
if codesign -d --entitlements - "$W/crash" 2>/dev/null | grep -q get-task-allow; then
	ok "C1 crash built -g -O0 and ad-hoc signed with com.apple.security.get-task-allow"
else bad "C1 entitlement" "get-task-allow missing"; fi

# ---- C2 crash with cores enabled -------------------------------------------
( cd "$W" && ulimit -c unlimited && exec ./crash ) >"$W/run.txt" 2>&1 &
pid=$!
wait "$pid" 2>/dev/null; rc=$?
CORE=/cores/core.$pid
if [ "$rc" = 139 ]; then ok "C2 crash died with SIGSEGV (rc 139), pid $pid"
else bad "C2 crash exit" "rc=$rc (want 139)"; fi

# ---- C3/C4 the core file ---------------------------------------------------
if [ -f "$CORE" ]; then
	ok "C3 kernel wrote $CORE ($(( $(stat -f %z "$CORE") / 1048576 )) MiB)"
	ft=$(otool -h "$CORE" | awk 'NR==4{print $5}')
	nth=$(otool -l "$CORE" | grep -c 'cmd LC_THREAD')
	if [ "$ft" = 4 ] && [ "$nth" = 4 ]; then ok "C4 Mach-O filetype 4 (MH_CORE) with $nth LC_THREAD (one per thread)"
	else bad "C4 core anatomy" "filetype=$ft LC_THREAD=$nth"; fi
else
	bad "C3 no core at $CORE" "ulimit -c? /cores writable? kern.coredump=$(sysctl -n kern.coredump)"
	echo "$pass/$total passed"; exit 1
fi

# ---- run the post-mortem on the core, then delete the core ---------------
if [ "$V" = practice ]; then
	lldb -b -c "$CORE" "$W/crash" -o "command script import $L/practice.py" -o pm_report >"$W/lldb.txt" 2>&1
else
	lldb -b -c "$CORE" "$W/crash" -s "$L/$V.lldb" >"$W/lldb.txt" 2>&1
fi
lrc=$?
rm -f "$CORE"; CORE=
echo "     (core deleted; lldb exit $lrc; transcript $W/lldb.txt)"

if [ "$V" != practice ]; then
	has '^ +\* frame #0: 0x[0-9a-f]+ crash`tx_flush' \
		&& ok "F1 backtrace of the CRASHING thread: frame #0 = tx_flush" \
		|| bad "F1 crashing thread backtrace" "got: $(grep -m1 -E '^ +\* frame #0' "$W/lldb.txt" | sed 's/^ *//')"
	has 'far = 0x656c75646f6d2d61' && has 'esr = 0x92000004' \
		&& ok "F2 fault registers: far = 0x656c75646f6d2d61, esr = 0x92000004" \
		|| bad "F2 fault registers" "got: $(grep -E '^ +(far|esr) =' "$W/lldb.txt" | tr -s ' ' | tr '\n' ' ')"
	has '^\(txbuf \*\) tx = 0x656c75646f6d2d61' \
		&& ok "F3 crashing frame locals: (txbuf *) tx = 0x656c75646f6d2d61" \
		|| bad "F3 crashing frame locals" "tx_flush's 'tx' not printed"
	has 'self->name = "tx_worker"' \
		&& ok "F4 crashing task name: tx_worker" \
		|| bad "F4 crashing task name" "self->name of the crashing thread not printed"
	has 'name = "front-left-camer"' \
		&& ok "F5 heap object fields: name = \"front-left-camer\" (16 bytes, no NUL)" \
		|| bad "F5 heap object fields" "*g_sess not printed"
	has '^0x[0-9a-f]+: 66 74 2d 63 61 6d 65 72 61 2d 6d 6f 64 75 6c 65  ft-camera-module' \
		&& ok "F6 heap raw bytes: ...ft-camera-module spills over name[16] into tx" \
		|| bad "F6 heap raw bytes" "got: $(grep -m2 -A2 'memory read' "$W/lldb.txt" | tail -2 | tr '\n' '|')"
	has 'self->name = "rx_parser"' && has 'last_len = 24' \
		&& ok "F7 writer's own frame: rx_parser, last_len = 24" \
		|| bad "F7 writer frame" "rx_parser_main locals not printed"
	has 'who = "rx_parser", what = "rx_name", arg = 24' \
		&& ok "F8 breadcrumb ring: rx_parser rx_name 24" \
		|| bad "F8 breadcrumb ring" "g_trace not printed"
	grep -qx 'POSTMORTEM-END' "$W/lldb.txt" \
		&& ok "F9 script ran to the end (no command failed on the core)" \
		|| bad "F9 script stopped early" "$(grep -m1 '^error:' "$W/lldb.txt")"
else
	has '^thread #3 -> tx_worker$' && has '^thread #4 -> rx_parser$' && has '^thread #2 -> watchdog$' \
		&& ok "P1 thread -> task map from struct task *self (2 watchdog, 3 tx_worker, 4 rx_parser)" \
		|| bad "P1 thread -> task map"
	has '^crash: thread #3 task=tx_worker func=tx_flush at crash.c:115$' \
		&& ok "P2 crashing thread found by stop reason: tx_worker / tx_flush / crash.c:115" \
		|| bad "P2 crashing thread"
	has 'EC=0x24 DABT_LOWER .*DFSC=0x04 translation fault, level 0, read; far=0x656c75646f6d2d61' \
		&& ok "P3 ESR decoded: EC 0x24 data abort, DFSC 0x04 level-0 translation fault, read" \
		|| bad "P3 ESR decode"
	has "magic OK, name\[16\] unterminated, tx=0x656c75646f6d2d61 == far, tx bytes as text 'a-module'" \
		&& ok "P4 heap object check: magic OK, name unterminated, tx == far == 'a-module'" \
		|| bad "P4 heap object check"
	has '^trace 0: main boot' && has '^trace 1: rx_parser rx_name 24$' && has '^trace 2: tx_worker tx_flush 1$' \
		&& ok "P5 trace ring in logical order (main boot -> rx_parser rx_name 24 -> tx_worker tx_flush)" \
		|| bad "P5 trace ring order"
	has '^writer: thread #4 task=rx_parser frame #2 rx_parser_main last_len=24 > sizeof name=16 -> 8 bytes into tx$' \
		&& ok "P6 writer located by function name, not by number: 24 > 16 -> 8 bytes into tx" \
		|| bad "P6 writer"
	has '^VERDICT: rx_parser overflowed session.name \(24 > 16\); tx_worker crashed reading s->tx$' \
		&& ok "P7 one-line verdict" || bad "P7 verdict"
	[ "$lrc" = 0 ] && ok "P8 lldb exit 0 (no command failed on the core)" || bad "P8 lldb exit $lrc"
fi

ls /cores/core.$pid >/dev/null 2>&1 && bad "core still present" || ok "core removed from /cores"
echo "$pass/$total passed"
[ "$pass" = "$total" ]
