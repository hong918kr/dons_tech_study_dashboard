#!/usr/bin/env bash
# demo_flags.sh — how a toolchain file's flag style interacts with the ways a user adds flags.
# 3 toolchain styles x 4 user actions, each a fresh (or re-)configure + build of the solution tree.
#   INIT   : set(CMAKE_C_FLAGS_INIT "${CPU_FLAGS} ...")              (solution)
#   SET    : set(CMAKE_C_FLAGS      "${CPU_FLAGS} ...")              (starter)
#   APPEND : set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${CPU_FLAGS} ...")  (common copy-paste)
# Output: bash demo_flags.sh > demo_flags_out.txt
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
export PATH=/opt/homebrew/bin:/usr/bin:/bin
unset CFLAGS
W=/tmp/TOOL-M05_flags
rm -rf "$W" && mkdir -p "$W/src" && cp -R proj/. "$W/src/" && cp -R solution/. "$W/src/" || exit 2
cd "$W" || exit 2
T=src/cmake/arm-none-eabi.cmake
cp $T tc_INIT.cmake
sed 's/^set(CMAKE_C_FLAGS_INIT   "/set(CMAKE_C_FLAGS        "/' $T > tc_SET.cmake
sed 's/^set(CMAKE_C_FLAGS_INIT   "/set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /' $T > tc_APPEND.cmake

result() {  # build dir -> "build=ok rev=0x.. mcpu=N cache='...'"
	local b=$1 rev n
	if ninja -C "$b" >"$b.log" 2>&1; then
		rev=$(arm-none-eabi-gdb -batch -ex 'x/wx &fw_board_rev' "$b/fw.elf" 2>/dev/null | awk '{print $NF}')
	else rev="BUILD-FAILED($(grep -m1 -o 'fatal error: [^ ]*' "$b.log"))"; fi
	n=$(python3 -I -c 'import json,sys
d=json.load(open(sys.argv[1])); c=[x["command"] for x in d if x["file"].endswith("fw/main.c")][0]
print(c.count("-mcpu="))' "$b/compile_commands.json")
	printf '%-7s %-37s rev=%-38s -mcpu x%s  cache CMAKE_C_FLAGS="%s"\n' "$2" "$3" "$rev" "$n" \
		"$(sed -n 's/^CMAKE_C_FLAGS:STRING=//p' "$b/CMakeCache.txt" | cut -c1-40)"
}

for s in INIT SET APPEND; do
	tc=$PWD/tc_$s.cmake
	cmake -S src -B b_${s}_none -G Ninja -DCMAKE_TOOLCHAIN_FILE=$tc >/dev/null 2>&1
	result b_${s}_none $s "(nothing)"
	CFLAGS=-DBOARD_REV=3 cmake -S src -B b_${s}_env -G Ninja -DCMAKE_TOOLCHAIN_FILE=$tc >/dev/null 2>&1
	result b_${s}_env $s "CFLAGS=-DBOARD_REV=3 (env)"
	cmake -S src -B b_${s}_D -G Ninja -DCMAKE_TOOLCHAIN_FILE=$tc -DCMAKE_C_FLAGS=-DBOARD_REV=3 >/dev/null 2>&1
	result b_${s}_D $s "-DCMAKE_C_FLAGS=-DBOARD_REV=3"
	# re-configure an existing tree, appending to what the cache already holds
	old=$(sed -n 's/^CMAKE_C_FLAGS:STRING=//p' b_${s}_none/CMakeCache.txt)
	cmake -S src -B b_${s}_none -DCMAKE_C_FLAGS="$old -DBOARD_REV=3" >/dev/null 2>&1
	result b_${s}_none $s "reconfigure: cache += -DBOARD_REV=3"
done

echo
echo "--- how often is the toolchain file read? (INIT style, file(APPEND) trace) ---"
sed 's|^set(CPU_FLAGS|file(APPEND '"$W"'/reads.txt "read in ${CMAKE_BINARY_DIR}\\n")\nset(CPU_FLAGS|' $T > tc_trace.cmake
cmake -S src -B b_trace -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/tc_trace.cmake >/dev/null 2>&1
echo "first configure:"; sed "s|$W/|  |" reads.txt; rm reads.txt
cmake -S src -B b_trace >/dev/null 2>&1
echo "re-configure of the same tree:"; sed "s|$W/|  |" reads.txt
