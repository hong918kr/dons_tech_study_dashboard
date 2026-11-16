#!/usr/bin/env bash
# demo_target.sh — observation runs for the lesson (Mac: cmake 4.5, ninja, arm-none-eabi-gcc 16, no newlib).
# Output: bash demo_target.sh > demo_target_out.txt   (paths under /tmp/TOOL-M05_demo shortened to $D)
set -u
cd "$(dirname "$0")" || exit 2
LESSON=$(pwd)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
export PATH=/opt/homebrew/bin:/usr/bin:/bin
unset CFLAGS
D=/tmp/TOOL-M05_demo
rm -rf "$D" && mkdir -p "$D" && cd "$D" || exit 2
tree() { mkdir -p "$1" && cp -R "$LESSON/proj/." "$1/" && cp -R "$LESSON/$2/." "$1/"; }
short() { sed "s|$D|\$D|g; s|/opt/homebrew/Cellar/arm-none-eabi-gcc/16.2.0|<gcc16>|g"; }
tree s solution; tree st starter; tree p practice

echo "### 1. starter: configure with the toolchain file (no CMAKE_TRY_COMPILE_TARGET_TYPE)"
cmake -S st -B b1 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake 2>&1 \
	| grep -E '^-- |CMake Error|is not able|Run Build|\[[12]/2\]|cannot find|collect2|Configuring incomplete' | short
echo "exit=${PIPESTATUS[0]}"

echo; echo "### 2. same, but -nostdlib in CMAKE_EXE_LINKER_FLAGS_INIT (still EXECUTABLE try_compile)"
sed -e '/^# TODO(1)/d' -e 's/^set(CMAKE_EXE_LINKER_FLAGS_INIT "")/set(CMAKE_EXE_LINKER_FLAGS_INIT "-nostdlib")/' \
	st/cmake/arm-none-eabi.cmake > tc_nostdlib.cmake
cmake -S s -B b2 -G Ninja -DCMAKE_TOOLCHAIN_FILE=$D/tc_nostdlib.cmake 2>&1 | grep -E 'C compiler|Configuring done' | short
grep -m1 'cannot find entry symbol' b2/CMakeFiles/CMakeConfigureLog.yaml | sed 's/^ *//' | short

echo; echo "### 3. the old hack: -DCMAKE_C_COMPILER_WORKS=ON (skips the check, ABI detection still fails)"
sed '/^# TODO(1)/d' st/cmake/arm-none-eabi.cmake > tc_notry.cmake
cmake -S s -B b3 -G Ninja -DCMAKE_TOOLCHAIN_FILE=$D/tc_notry.cmake -DCMAKE_C_COMPILER_WORKS=ON 2>&1 | grep -E '^-- (Detecting C compiler ABI|Check|Configuring done)'
cmake -S s -B b3ok -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake >/dev/null 2>&1
for v in CMAKE_C_SIZEOF_DATA_PTR CMAKE_C_COMPILER_ABI CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES; do
	printf '%-38s hack: %-12s proper: %s\n' "$v" \
		"[$(sed -n "s/^set($v \"\(.*\)\")/\1/p" b3/CMakeFiles/*/CMakeCCompiler.cmake | cut -c1-30)]" \
		"[$(sed -n "s/^set($v \"\(.*\)\")/\1/p" b3ok/CMakeFiles/*/CMakeCCompiler.cmake | short | cut -c1-60)]"
done

echo; echo "### 4. CMakeSystem.cmake: host tree vs target tree"
cmake -S s -B bh -G Ninja >/dev/null 2>&1
for b in bh b3ok; do echo "[$b]"; grep -E 'set\(CMAKE_(HOST_SYSTEM_NAME|HOST_SYSTEM_PROCESSOR|SYSTEM_NAME|SYSTEM_PROCESSOR|CROSSCOMPILING) ' $b/CMakeFiles/*/CMakeSystem.cmake; done
echo "binutils CMake derived from the compiler prefix (target cache):"
grep -E '^CMAKE_(AR|OBJCOPY|OBJDUMP|NM|READELF|SIZE|STRIP|RANLIB|ADDR2LINE|LINKER):' b3ok/CMakeCache.txt
grep -E '^ARM_SIZE:' b3ok/CMakeCache.txt

echo; echo "### 5. solution target build (ninja -v, link step only)"
ninja -C b3ok -v 2>&1 | grep -E 'Linking|-o fw.elf' | sed 's/ && /\n    \&\& /g' | short
echo "--- fw.size.txt"; cat b3ok/fw.size.txt | short
echo "--- readelf -h (excerpt)"
arm-none-eabi-readelf -h b3ok/fw.elf | grep -E 'Class|Machine|Entry|Flags'
echo "--- readelf -S (alloc sections)"
arm-none-eabi-readelf -S b3ok/fw.elf | grep -E '\.isr_vector|\.text|\.rodata|\.data|\.bss'
echo "--- readelf -l (program headers)"
arm-none-eabi-readelf -l b3ok/fw.elf | grep -E 'Type|LOAD'
echo "--- fw.bin: size + first 8 bytes (initial SP, Reset_Handler|1)"
ls -l b3ok/fw.bin | awk '{print $5, $NF}' | short; xxd -l 8 b3ok/fw.bin
echo "--- fw.map (Memory Configuration + output sections)"
sed -n '/^Memory Configuration/,/^$/p' b3ok/fw.map | grep -v '^$'
grep -E '^\.(isr_vector|text|rodata|data|bss) ' b3ok/fw.map
echo "--- compile_commands.json entry for fw/main.c (command)"
python3 -I -c 'import json,sys
d=json.load(open(sys.argv[1])); print([x["command"] for x in d if x["file"].endswith("fw/main.c")][0])' b3ok/compile_commands.json | short

echo; echo "### 6. LINK_DEPENDS: the fw.elf edge in build.ninja"
grep -E '^build fw\.elf' b3ok/build.ninja | short
cmake -S st -B b6 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY >/dev/null 2>&1
ninja -C b6 >/dev/null 2>&1
echo "[starter]"; grep -E '^build fw\.elf' b6/build.ninja | short
touch s/fw/stm32f4.ld st/fw/stm32f4.ld
echo "[solution] touch fw/stm32f4.ld; ninja -n -d explain"; ninja -C b3ok -n -d explain 2>&1 | grep -v Entering | short
echo "[starter]  touch fw/stm32f4.ld; ninja -n -d explain"; ninja -C b6 -n -d explain 2>&1 | grep -v Entering | short

echo; echo "### 7. CMAKE_SYSTEM_PROCESSOR MATCHES \"arm\" on this Mac (starter TODO 4)"
cmake -S st -B b7 -G Ninja >/dev/null 2>&1
ninja -C b7 2>&1 | tail -1
ctest --test-dir b7 2>&1 | grep -E 'No tests|Total'
cmake -P /dev/stdin <<'EOF'
foreach(p arm64 aarch64 arm armv7 x86_64 AMD64)
  if(p MATCHES "arm")
    message("  ${p} MATCHES arm -> TRUE")
  else()
    message("  ${p} MATCHES arm -> FALSE")
  endif()
endforeach()
EOF

echo; echo "### 8. how often the toolchain file runs, and what a try_compile project sees (practice tc, -DFW_MCU=cortex-m0plus)"
for mode in with without; do
	if [ $mode = with ]; then cp p/cmake/arm-none-eabi.cmake tc8.cmake
	else sed '/IN_LIST CMAKE_TRY_COMPILE_PLATFORM_VARIABLES/,/^endif()/d' p/cmake/arm-none-eabi.cmake > tc8.cmake; fi
	sed -i '' "s|^if(FW_MCU STREQUAL \"cortex-m4\")|file(APPEND $D/reads_$mode.txt \"read: FW_MCU=[\${FW_MCU}] in \${CMAKE_BINARY_DIR}\\\\n\")\\
if(FW_MCU STREQUAL \"cortex-m4\")|" tc8.cmake
	cmake -S p -B b8_$mode -G Ninja -DCMAKE_TOOLCHAIN_FILE=$D/tc8.cmake -DFW_MCU=cortex-m0plus >/dev/null 2>&1
	echo "[$mode CMAKE_TRY_COMPILE_PLATFORM_VARIABLES FW_MCU]"; short < reads_$mode.txt
done
echo "try_compile build flags were still m0plus in both (CMAKE_C_FLAGS is forwarded):"
grep -h -m1 'Build flags' b8_without/CMakeFiles/CMakeConfigureLog.yaml | sed 's/^ *//'

echo; echo "### 9. practice P2: Cortex-M0+ without libgcc (-nostdlib drops it)"
sed 's/target_link_libraries(fw PRIVATE dsp gcc)/target_link_libraries(fw PRIVATE dsp)/' p/CMakeLists.txt > p/CMakeLists.nogcc
mkdir -p p9 && cp -R p/. p9/ && mv p9/CMakeLists.nogcc p9/CMakeLists.txt
cmake -S p9 -B b9 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DFW_MCU=cortex-m0plus >/dev/null 2>&1
ninja -C b9 2>&1 | grep -E 'undefined reference|collect2' | short
cmake -S p -B b9ok -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DFW_MCU=cortex-m0plus >/dev/null 2>&1
ninja -C b9ok 2>&1 | grep -E 'Memory region|FLASH:|RAM:|fw size|text|fw.elf$' | short
arm-none-eabi-readelf -A b9ok/fw.elf | grep -E 'Tag_CPU_name|Tag_CPU_arch:|Tag_FP_arch|Tag_ABI_VFP_args'
echo "libgcc members pulled in (map):"; grep -A1 -E '^[^ ]*libgcc\.a\(' b9ok/fw.map | grep -E 'libgcc' | short | head -4
echo "[cortex-m4 for comparison]"; cmake -S p -B b9m4 -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake >/dev/null 2>&1
ninja -C b9m4 2>&1 | grep -E 'fw size' | short
arm-none-eabi-readelf -A b9m4/fw.elf | grep -E 'Tag_CPU_name|Tag_CPU_arch:|Tag_FP_arch|Tag_ABI_VFP_args'

echo; echo "### 10. CMAKE_FIND_ROOT_PATH_MODE_*: ONLY protects nothing until a root path is set"
mkdir -p fr/sysroot/usr/lib fr/sysroot/usr/include && touch fr/sysroot/usr/include/cairo.h
arm-none-eabi-ar rc fr/sysroot/usr/lib/libcairo.a 2>/dev/null
cat > fr/CMakeLists.txt <<'EOF'
cmake_minimum_required(VERSION 3.20)
project(findtest LANGUAGES C)
find_library(CAIRO_LIB cairo)
find_program(PY python3)
message(STATUS "CAIRO_LIB=${CAIRO_LIB}  PY=${PY}")
EOF
ls /opt/homebrew/lib/libcairo.a
for args in "-DCMAKE_PREFIX_PATH=/opt/homebrew" \
            "-DCMAKE_PREFIX_PATH=/opt/homebrew -DCMAKE_FIND_ROOT_PATH=$D/fr/sysroot" \
            "-DCMAKE_PREFIX_PATH=/usr -DCMAKE_FIND_ROOT_PATH=$D/fr/sysroot"; do
	rm -rf bfr; echo "[$args]" | short
	cmake -S fr -B bfr -G Ninja -DCMAKE_TOOLCHAIN_FILE=$D/s/cmake/arm-none-eabi.cmake $args 2>&1 | grep CAIRO_LIB | short
done
