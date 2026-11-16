#!/usr/bin/env bash
# TOOL-M05 checker — bash check.sh <starter|solution|practice>
# Copies proj/ (shared sources) + <variant>/ (CMakeLists.txt, cmake/arm-none-eabi.cmake, ...)
# to /tmp/TOOL-M05_work/<variant>/src, then builds the SAME tree twice with real tools:
#   build-host   : Apple clang  -> lib + unit tests, run by ctest
#   build-target : toolchain file -> arm-none-eabi-gcc 16 (Mac, no newlib) -> fw.elf/.bin/.map/.size
# and asserts on cmake/ninja/ctest/readelf/nm/gdb output. Mac only, ~10 s.
set -u
cd "$(dirname "$0")" || exit 2
LESSON=$(pwd)
V=${1:-}
case "$V" in starter|solution|practice) ;; *) echo "usage: bash check.sh <starter|solution|practice>"; exit 2 ;; esac
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
export PATH=/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin               # never /usr/local/bin (old LLVM 3.2)
unset CFLAGS CXXFLAGS LDFLAGS CMAKE_GENERATOR CMAKE_TOOLCHAIN_FILE CMAKE_BUILD_TYPE
W=/tmp/TOOL-M05_work/$V
rm -rf "$W" && mkdir -p "$W/src" || exit 2
cp -R proj/. "$W/src/" && cp -R "$V/." "$W/src/" || exit 2
cd "$W" || exit 2

TC=cmake/arm-none-eabi.cmake          # relative to the source dir, like a real repo
pass=0; total=0
ok()  { total=$((total + 1)); pass=$((pass + 1)); echo "PASS $1"; }
bad() { total=$((total + 1)); echo "FAIL $1"; [ -n "${2:-}" ] && echo "$2" | sed 's/^/     /'; return 0; }
sysvar() { sed -n "s/^set($2 \"\(.*\)\")/\1/p" "$1"/CMakeFiles/*/CMakeSystem.cmake 2>/dev/null | head -1; }
board_rev() { arm-none-eabi-gdb -batch -ex 'x/wx &fw_board_rev' "$1" 2>/dev/null | awk '{print $NF}'; }
mcpu_count() {  # how many times -mcpu= appears in the compile command of fw/main.c
	python3 -I -c 'import json,sys
d=json.load(open(sys.argv[1]))
c=[x["command"] for x in d if x["file"].endswith("fw/main.c")]
print(c[0].count("-mcpu=") if c else -1)' "$1/compile_commands.json" 2>/dev/null || echo -1; }
symaddr() { arm-none-eabi-nm "$1" | awk -v s="$2" '$3==s {print $1}'; }

echo "== $V: $(cmake --version | head -1), ninja $(ninja --version), $(arm-none-eabi-gcc --version | head -1) =="

# ------------------------------------------------------------------ host build
echo "-- host (Apple clang) --"
if cmake -S src -B build-host -G Ninja >host_cfg.log 2>&1 && [ "$(sysvar build-host CMAKE_CROSSCOMPILING)" = FALSE ]
then ok "H1 host configure: CMAKE_SYSTEM_PROCESSOR=$(sysvar build-host CMAKE_SYSTEM_PROCESSOR), CMAKE_CROSSCOMPILING=FALSE"
else bad "H1 host configure" "$(grep -m3 -E 'Error|error' host_cfg.log)"; fi

ninja -C build-host >host_build.log 2>&1
ctest --test-dir build-host --output-on-failure >ctest.log 2>&1
if grep -q '100% tests passed' ctest.log && grep -q 'out of 3$' ctest.log
then ok "H2 host unit tests: $(grep -m1 '100% tests passed' ctest.log)"
else bad "H2 host unit tests (want 3 tests, 100% passed)" "$(grep -m1 -E 'No tests were found|tests passed|Failed|error' ctest.log)"; fi

if [ -f build-host/compile_commands.json ] && ! grep -qE -- '-mcpu=|-mthumb|arm-none-eabi|-T[^ ]*\.ld' build-host/compile_commands.json \
   && file build-host/libdsp.a build-host/CMakeFiles/dsp.dir/lib/src/crc.c.o 2>/dev/null | grep -q 'Mach-O 64-bit object arm64'
then ok "H3 host tree clean: no -mcpu/-mthumb/arm-none-eabi in compile_commands, lib objects are Mach-O arm64"
else bad "H3 target flags leaked into host build" "$(grep -m1 -oE -- '-mcpu=[^ ]*|arm-none-eabi[^ ]*' build-host/compile_commands.json 2>/dev/null)"; fi

# ------------------------------------------------------------------ target build
echo "-- target (toolchain file $TC) --"
XTRA=""
if cmake -S src -B build-target -G Ninja -DCMAKE_TOOLCHAIN_FILE=$TC >tgt_cfg.log 2>&1; then
	ok "T1 target configure with toolchain file (no workaround needed)"
else
	bad "T1 target configure with toolchain file" "$(grep -m1 'is not able to compile' tgt_cfg.log)
$(grep -m2 'cannot find' tgt_cfg.log | sed 's/^ *//')"
	XTRA=-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
	echo "     (checker retries with $XTRA so the remaining checks still grade)"
	rm -rf build-target
	cmake -S src -B build-target -G Ninja -DCMAKE_TOOLCHAIN_FILE=$TC $XTRA >tgt_cfg.log 2>&1
fi

cc_path=$(sed -n 's/^set(CMAKE_C_COMPILER "\(.*\)")/\1/p' build-target/CMakeFiles/*/CMakeCCompiler.cmake 2>/dev/null | head -1)
if [ "$(sysvar build-target CMAKE_SYSTEM_NAME)" = Generic ] && [ "$(sysvar build-target CMAKE_CROSSCOMPILING)" = TRUE ] \
   && [ "$(basename "$cc_path")" = arm-none-eabi-gcc ]
then ok "T2 CMakeSystem: SYSTEM_NAME=Generic PROCESSOR=$(sysvar build-target CMAKE_SYSTEM_PROCESSOR) CROSSCOMPILING=TRUE, CC=$(basename "$cc_path")"
else bad "T2 target system/compiler" "name=$(sysvar build-target CMAKE_SYSTEM_NAME) cross=$(sysvar build-target CMAKE_CROSSCOMPILING) cc=$cc_path"; fi

ELF=build-target/fw.elf
ninja -C build-target >tgt_build.log 2>&1
hdr=$(arm-none-eabi-readelf -h $ELF 2>/dev/null)
entry=$(echo "$hdr" | awk '/Entry point/ {print $NF}')
reset=$(symaddr $ELF Reset_Handler)
if echo "$hdr" | grep -q 'Class:.*ELF32' && echo "$hdr" | grep -q 'Machine:.*ARM' && echo "$hdr" | grep -q 'hard-float' \
   && [ -n "$reset" ] && [ $((entry)) -eq $((0x$reset | 1)) ]
then ok "T3 fw.elf = ELF32 ARM hard-float, entry $entry = Reset_Handler|1 (Thumb)"
else bad "T3 fw.elf header" "$(tail -3 tgt_build.log)"; fi

sidata=$(symaddr $ELF _sidata); sdata=$(symaddr $ELF _sdata); edata=$(symaddr $ELF _edata)
want_bin=-1; [ -n "$sidata" ] && want_bin=$(( 0x$sidata - 0x08000000 + 0x$edata - 0x$sdata ))
got_bin=$(stat -f %z build-target/fw.bin 2>/dev/null || echo 0)
if [ "$got_bin" = "$want_bin" ] && grep -q '^ *text' build-target/fw.size.txt 2>/dev/null \
   && grep -q '^Memory Configuration' build-target/fw.map 2>/dev/null
then ok "T4 post-build: fw.bin $got_bin B (= .data LMA end - 0x08000000), fw.map, fw.size.txt"
else bad "T4 post-build artifacts" "fw.bin=$got_bin want=$want_bin; $(ls build-target | tr '\n' ' ')"; fi

ntests=$(ctest --test-dir build-target -N 2>/dev/null | sed -n 's/^Total Tests: //p')
if ! ls build-target/test_dsp* >/dev/null 2>&1 && [ "${ntests:-0}" = 0 ]
then ok "T5 target tree has no test executable, ctest -N: Total Tests: ${ntests:-0}"
else bad "T5 tests built in target tree" "$(ls build-target | grep -i test)"; fi

n=$(mcpu_count build-target)
if [ "$n" = 1 ]; then ok "T6 -mcpu= appears exactly once in the fw/main.c compile command"
else bad "T6 -mcpu= count in fw/main.c compile command: $n (want 1)"; fi

# a board engineer adds a define the standard way: CFLAGS env on the first configure
CFLAGS=-DBOARD_REV=3 cmake -S src -B build-rev3 -G Ninja -DCMAKE_TOOLCHAIN_FILE=$TC $XTRA >rev3_cfg.log 2>&1
ninja -C build-rev3 >rev3_build.log 2>&1
rev=$(board_rev build-rev3/fw.elf); n3=$(mcpu_count build-rev3)
if [ "$rev" = 0x00000003 ] && [ "$n3" = 1 ]
then ok "T7 CFLAGS=-DBOARD_REV=3 reaches the compiler: fw_board_rev=$rev, -mcpu once"
else bad "T7 CFLAGS=-DBOARD_REV=3 ignored or flags duplicated" "fw_board_rev=${rev:-?} (want 0x00000003), -mcpu count=$n3
CMAKE_C_FLAGS cache: $(sed -n 's/^CMAKE_C_FLAGS:STRING=//p' build-rev3/CMakeCache.txt)"; fi

if ninja -C build-target 2>&1 | grep -q 'no work to do'; then ok "T8 second ninja: no work to do"
else bad "T8 second ninja rebuilt something"; fi

# a 16 KB bootloader now sits at 0x08000000: the app moves to 0x08004000 (edit the script only)
sed -i '' 's/ORIGIN = 0x08000000, LENGTH = 512K/ORIGIN = 0x08004000, LENGTH = 496K/' src/fw/stm32f4.ld
ninja -C build-target >relink.log 2>&1
vaddr=$(arm-none-eabi-readelf -S $ELF | awk '{for (i = 1; i < NF; i++) if ($i == ".isr_vector") print $(i + 2)}')
if [ "$vaddr" = 08004000 ] && grep -q 'Linking C executable fw.elf' relink.log
then ok "T9 linker script edit -> relink: .isr_vector now at 0x$vaddr"
else bad "T9 linker script edit did not relink (.isr_vector still at 0x$vaddr)" "$(grep -v '^ninja: Entering' relink.log | head -2)"; fi
cp "$LESSON/proj/fw/stm32f4.ld" src/fw/stm32f4.ld && ninja -C build-target >/dev/null 2>&1

# ------------------------------------------------------------------ practice (9. 추가 연습)
if [ "$V" = practice ]; then
	echo "-- practice --"
	# P1 presets: host configure/build/test + target with BOARD_REV=2 from the preset
	( cd src && cmake --preset host && cmake --build --preset host && ctest --preset host ) >p1_host.log 2>&1
	if grep -q '100% tests passed' p1_host.log && grep -q 'out of 3$' p1_host.log
	then ok "P1 cmake --preset host / --build / ctest --preset host: 3 tests passed"
	else bad "P1 host preset" "$(tail -3 p1_host.log)"; fi
	( cd src && cmake --preset target && cmake --build --preset target ) >p1_tgt.log 2>&1
	prev=$(board_rev src/build/target/fw.elf)
	if [ "$prev" = 0x00000002 ] && [ ! -e src/build/target/test_dsp ]
	then ok "P1 cmake --preset target: fw_board_rev=$prev from the preset's BOARD_REV cache variable"
	else bad "P1 target preset" "fw_board_rev=${prev:-?}; $(grep -m2 -iE 'error' p1_tgt.log)"; fi

	# P2 second MCU from the same toolchain file: Cortex-M0+ (ARMv6-M: no FPU, no divide instruction)
	cmake -S src -B build-m0 -G Ninja -DCMAKE_TOOLCHAIN_FILE=$TC -DFW_MCU=cortex-m0plus >m0_cfg.log 2>&1
	ninja -C build-m0 >m0_build.log 2>&1
	attrs=$(arm-none-eabi-readelf -A build-m0/fw.elf 2>/dev/null)
	if echo "$attrs" | grep -q 'Tag_CPU_arch: v6S-M' && ! echo "$attrs" | grep -q 'Tag_ABI_VFP_args' \
	   && arm-none-eabi-nm build-m0/fw.elf | grep -q '__aeabi_idiv'
	then ok "P2 -DFW_MCU=cortex-m0plus: Tag_CPU_arch v6S-M, soft-float, __aeabi_idiv linked from libgcc"
	else bad "P2 cortex-m0plus build" "$(grep -m2 -E 'undefined reference|error' m0_build.log)"; fi
	if ! cmake -S src -B build-m7 -G Ninja -DCMAKE_TOOLCHAIN_FILE=$TC -DFW_MCU=cortex-m7 >m7_cfg.log 2>&1 \
	   && grep -q "FW_MCU must be cortex-m4 or cortex-m0plus, got 'cortex-m7'" m7_cfg.log
	then ok "P2 unknown -DFW_MCU=cortex-m7 stops configure with a clear message"
	else bad "P2 unknown FW_MCU accepted" "$(grep -m2 -E 'Error|error' m7_cfg.log)"; fi

	# P3 Flash budget gate in POST_BUILD (cmake -P script) + --print-memory-usage
	if grep -q 'FLASH:' tgt_build.log && grep -qE 'fw size: flash [0-9]+ / 8192 B' tgt_build.log
	then ok "P3 default build: $(grep -m1 -oE 'fw size: flash [0-9]+ / 8192 B.*' tgt_build.log)"
	else bad "P3 budget report missing" "$(tail -3 tgt_build.log)"; fi
	cmake -S src -B build-tight -G Ninja -DCMAKE_TOOLCHAIN_FILE=$TC -DFW_FLASH_BUDGET=256 >tight_cfg.log 2>&1
	if ! ninja -C build-tight >tight_build.log 2>&1 && grep -q 'over budget' tight_build.log && [ ! -e build-tight/fw.bin ]
	then ok "P3 -DFW_FLASH_BUDGET=256: build fails, no fw.bin ($(grep -m1 -o 'fw size: flash.*' tight_build.log))"
	else bad "P3 budget gate did not fail the build" "$(tail -3 tight_build.log)"; fi
fi

echo "$pass/$total passed"
[ "$pass" = "$total" ] && exit 0 || exit 1
