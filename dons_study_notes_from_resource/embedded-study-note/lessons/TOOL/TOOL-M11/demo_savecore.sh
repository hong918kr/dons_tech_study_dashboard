#!/usr/bin/env bash
# TOOL-M11 demo (Mac): cores written by lldb (`process save-core`) instead of the kernel.
#   bash demo_savecore.sh > demo_savecore.txt 2>&1
# --style full is skipped here: measured once by hand on 2026-11-15 = 7121956864 bytes, ~4 min.
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-M11_work/savecore
rm -rf "$W" && mkdir -p "$W" || exit 2
cc -std=c11 -g -O0 -Wall -Wextra -Werror crash.c -o "$W/crash" || exit 1
codesign -s - -f --entitlements ent.plist "$W/crash" 2>/dev/null

echo "== run under lldb; at the crash save two cores (-k = commands to run after a crash stop)"
lldb -b "$W/crash" -o run \
	-k "process save-core --style stack $W/stack.core" \
	-k "process save-core --style modified-memory $W/dirty.core" \
	-k kill >"$W/lldb_run.txt" 2>&1
grep -E 'stop reason|save-core|exited' "$W/lldb_run.txt"
echo "(\"Saving N bytes ...\" lines printed by lldb: $(grep -c '^Saving' "$W/lldb_run.txt"))"
echo; echo "== sizes"
for c in stack dirty; do printf '%-6s %10s bytes\n' "$c" "$(stat -f %z "$W/$c.core")"; done

for c in stack dirty; do
	echo; echo "== post-mortem on $c.core"
	lldb -b -c "$W/$c.core" "$W/crash" -o 'thread list' -o 'frame variable *s' \
		-o 'p g_trace[1]' 2>&1 | grep -v '^(lldb) target\|^Core file\|^$' | head -22
done
rm -f "$W"/*.core
