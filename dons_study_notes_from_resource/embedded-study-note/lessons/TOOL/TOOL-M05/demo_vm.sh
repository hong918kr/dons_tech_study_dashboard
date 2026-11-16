#!/usr/bin/env bash
# demo_vm.sh — the same source tree in the Lima VM: cmake 4.2 + arm-none-eabi-gcc 14.2.1 WITH newlib(-nano).
# Run on the Mac:  bash demo_vm.sh > demo_vm_out.txt 2>&1
# (Mac home is mounted read-only in the VM -> everything is copied to VM /tmp/TOOL-M05_work/vm first.)
set -u
cd "$(dirname "$0")" || exit 2
LESSON=$(pwd)
limactl shell elinux -- bash -s "$LESSON" <<'VMEOF' | sed -E 's|/build/newlib-[^:]*/\.\./|<newlib-src>/|'
set -u
L=$1
W=/tmp/TOOL-M05_work/vm
rm -rf "$W" && mkdir -p "$W" && cd "$W" || exit 2
mk() { mkdir -p "$1" && cp -R "$L/proj/." "$1/" && cp -R "$L/solution/." "$1/"; }
mk src
echo "== $(cmake --version | head -1), $(arm-none-eabi-gcc --version | head -1)"
T=src/cmake/arm-none-eabi.cmake

echo; echo "### A. starter-style toolchain (no CMAKE_TRY_COMPILE_TARGET_TYPE), newlib present, no specs"
sed '/CMAKE_TRY_COMPILE_TARGET_TYPE/d' $T > tc_a.cmake
cmake -S src -B a -G Ninja -DCMAKE_TOOLCHAIN_FILE=$W/tc_a.cmake 2>&1 \
	| grep -E 'C compiler|undefined reference|Configuring (done|incomplete)' | sed 's/^ *//' | head -8

echo; echo "### B. same, but --specs=nano.specs --specs=nosys.specs in CMAKE_EXE_LINKER_FLAGS_INIT"
sed -e '/CMAKE_TRY_COMPILE_TARGET_TYPE/d' \
    -e 's/^set(CMAKE_EXE_LINKER_FLAGS_INIT "")/set(CMAKE_EXE_LINKER_FLAGS_INIT "--specs=nano.specs --specs=nosys.specs")/' $T > tc_b.cmake
cmake -S src -B b -G Ninja -DCMAKE_TOOLCHAIN_FILE=$W/tc_b.cmake 2>&1 | grep -E 'C compiler|Configuring (done|incomplete)'
grep -m3 -E 'warning: _[a-z]+ is not implemented' b/CMakeFiles/CMakeConfigureLog.yaml | sed 's/^ *//'

echo; echo "### C. solution tree unchanged (-nostdlib, own memset/memcpy), VM gcc 14"
cmake -S src -B c -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake >/dev/null 2>&1
ninja -C c 2>&1 | grep -A1 -E '^ *text'
arm-none-eabi-readelf -h c/fw.elf | grep -E 'Class|Machine|Entry'

echo; echo "### D. newlib-nano instead of -nostdlib: fw calls snprintf (specs in the toolchain file)"
mk d
sed -i 's/^set(CMAKE_EXE_LINKER_FLAGS_INIT "")/set(CMAKE_EXE_LINKER_FLAGS_INIT "--specs=nano.specs --specs=nosys.specs")/' d/cmake/arm-none-eabi.cmake
sed -i -e 's| fw/libc_min.c)| fw/report.c)|' -e 's/-nostdlib -T/-T/' d/CMakeLists.txt
cat > d/fw/report.c <<'EOF'
#include <stdint.h>
#include <stdio.h>
extern volatile uint16_t g_crc;
extern volatile int32_t g_avg;
char g_line[32];
void fw_report(void);
void fw_report(void) { snprintf(g_line, sizeof g_line, "crc=%04x avg=%ld", g_crc, (long)g_avg); }
EOF
sed -i -e 's/^#include "dsp.h"/#include "dsp.h"\nvoid fw_report(void);/' \
       -e 's/^    g_boot_count += fw_board_rev;.*/&\n    fw_report();/' d/fw/main.c
cmake -S d -B bd -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake >/dev/null 2>&1
echo "[D1 given stm32f4.ld: no 'end' symbol for the heap]"
ninja -C bd 2>&1 | grep -E 'undefined reference|collect2' | sed 's|.*/arm-none-eabi/lib/|<newlib>/|' | head -3
echo "[D2 add PROVIDE(end = _ebss) to the script copy]"
sed -i 's/^ENTRY(Reset_Handler)/ENTRY(Reset_Handler)\nPROVIDE(end = _ebss);/' d/fw/stm32f4.ld
ninja -C bd >bd.log 2>&1; echo "exit=$?"
grep -E 'warning: _[a-z]+ is not implemented' bd.log | sort -u
grep -A1 -E '^ *text' bd.log
echo "libc_nano members pulled in (first 8 from the map):"
grep -E '^ *[^ ]*libc_nano\.a\(' bd/fw.map | sed 's|.*/\(libc_nano\.a(\)|\1|; s|).*|)|' | sort -u | head -8
echo "total libc_nano members: $(grep -oE 'libc_nano\.a\([^)]*\)' bd/fw.map | sort -u | wc -l)"
VMEOF
