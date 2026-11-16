#!/usr/bin/env bash
# demo_runtime.sh - what does TESTING see of the same 10 bugs?
#   bash demo_runtime.sh > runtime.txt
# selftest.c (16 run-time checks) built against fw_bugs.c instead of fw_clean.c:
#   1) plain -O0 build, 2) -O0 with ASan+UBSan.
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-M12_work/runtime
rm -rf "$W" && mkdir -p "$W"
INC="-Iinclude -Ithird_party"
echo "== 1. cc -O0 selftest.c -DFW_SRC=fw_bugs.c"
cc -std=c11 -w -O0 $INC -DFW_SRC='"fw_bugs.c"' selftest.c -o "$W/plain"
"$W/plain" >"$W/plain.txt" 2>&1
echo "exit status: $?"
cat "$W/plain.txt"
echo
echo "== 2. cc -O0 -g -fsanitize=address,undefined selftest.c -DFW_SRC=fw_bugs.c"
cc -std=c11 -w -O0 -g -fsanitize=address,undefined $INC -DFW_SRC='"fw_bugs.c"' selftest.c -o "$W/asan"
"$W/asan" >"$W/asan.txt" 2>&1
echo "exit status: $?"
grep -vE '^ +#([3-9]|[1-9][0-9]) |^Shadow|^  [A-Z]|^=+[0-9]+=+ *$|^ *0x[0-9a-f]+: |^=>' "$W/asan.txt" | head -24
echo
echo "== 3. same build, fw_clean.c"
cc -std=c11 -w -O0 -g -fsanitize=address,undefined $INC selftest.c -o "$W/asan_clean"
"$W/asan_clean" 2>&1 | tail -1
echo "exit status: ${PIPESTATUS[0]}"
