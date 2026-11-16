#!/usr/bin/env bash
# TOOL-M04 demos on the Mac (cmake 4.5, Ninja, AppleClang, GNU Make 3.81). Output: demo_cmake_out.txt
#   bash demo_cmake.sh > demo_cmake_out.txt 2>&1
# Works only in /tmp/TOOL-M04_work/demo. Each section is independent of the checker.
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
export PATH=/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin
L=$(pwd)
D=/tmp/TOOL-M04_work/demo
rm -rf "$D" && mkdir -p "$D" || exit 2
mk() {  # mk <name> <variant...>: source tree = proj + variants laid in order
	local n=$1; shift; mkdir -p "$D/$n"; cp -R "$L/proj/." "$D/$n/"
	for v in "$@"; do cp -R "$L/$v/." "$D/$n/"; done
}
cmd_of() { python3 -I -c 'import json,sys
for e in json.load(open(sys.argv[1])):
    if e["file"].endswith(sys.argv[2]): print(e["command"]); break' "$1" "$2"; }
sec() { printf '\n######## %s\n' "$*"; }

sec "S1 configure -> generate -> build (solution, Ninja)"
mk sol solution
(cd "$D/sol" && find . -type f | sort) > "$D/src_before.txt"
cmake -S "$D/sol" -B "$D/sol-b" -G Ninja 2>&1 | sed "s|$D|\$D|g"
echo "--- build tree after configure+generate (nothing compiled yet):"
(cd "$D/sol-b" && ls -1F)
echo "--- build:"
cmake --build "$D/sol-b" 2>&1 | sed "s|$D|\$D|g"
echo "--- source tree after configure+build: files added/removed (diff of find lists):"
(cd "$D/sol" && find . -type f | sort) | diff "$D/src_before.txt" - && echo "(none)"

sec "S2 cache variables: option() is sticky"
cmake -N -LH "$D/sol-b" 2>/dev/null | grep -B1 '^FEATURE_CRC'
cmake -DFEATURE_CRC=OFF "$D/sol-b" >/dev/null 2>&1
echo "after -DFEATURE_CRC=OFF : $(grep '^FEATURE_CRC' "$D/sol-b/CMakeCache.txt")"
cmake "$D/sol-b" >/dev/null 2>&1
echo "after plain 'cmake <b>' : $(grep '^FEATURE_CRC' "$D/sol-b/CMakeCache.txt")   <- still OFF"
cmake --fresh -S "$D/sol" -B "$D/sol-b" -G Ninja >/dev/null 2>&1   # --fresh forgets the generator too
echo "after --fresh           : $(grep '^FEATURE_CRC' "$D/sol-b/CMakeCache.txt")"
cmake --build "$D/sol-b" >/dev/null 2>&1

sec "S3 starter: set(CMAKE_C_FLAGS ...) shadows the cache entry"
mk st starter
cmake -S "$D/st" -B "$D/st-b" -G Ninja "-DCMAKE_C_FLAGS=-fsanitize=address" >/dev/null 2>&1
echo "cache  : $(grep '^CMAKE_C_FLAGS:' "$D/st-b/CMakeCache.txt")"
echo "command: $(cmd_of "$D/st-b/compile_commands.json" ringbuf/src/ringbuf.c | sed "s|$D|\$D|g")"
cmake -S "$D/sol" -B "$D/sol-asan" -G Ninja "-DCMAKE_C_FLAGS=-fsanitize=address" >/dev/null 2>&1
echo "solution command: $(cmd_of "$D/sol-asan/compile_commands.json" ringbuf/src/ringbuf.c | sed "s|$D|\$D|g")"

sec "S4 Ninja keeps the header deps (compiler -MD depfile -> .ninja_deps)"
(cd "$D/sol-b" && ninja -t deps CMakeFiles/app.dir/app/main.c.o | grep -v '/Library/' | sed "s|$D|\$D|g")
touch "$D/sol/ringbuf/include/ringbuf.h"
echo "--- touch ringbuf.h; ninja -n -d explain (first lines):"
(cd "$D/sol-b" && ninja -n -d explain 2>&1 | grep -E 'explain: .*(older|dirty)|Building' | head -8 | sed "s|$D|\$D|g")
cmake --build "$D/sol-b" >/dev/null 2>&1

sec "S5 which -std= flag does CMake add? (AppleClang default: C_STANDARD_COMPUTED_DEFAULT 17, EXTENSIONS ON)"
echo 'int main(void){return 0;}' > "$D/m.c"
for v in "3.20:features+EXT_OFF" "3.22:features+EXT_OFF" "3.20:STANDARD11" "3.20:STANDARD11+EXT_OFF"; do
	mv=${v%%:*}; k=${v#*:}; p="$D/std_${mv}_$k"; mkdir -p "$p"; cp "$D/m.c" "$p/"
	{ echo "cmake_minimum_required(VERSION $mv)"; echo 'project(p C)'; echo 'set(CMAKE_EXPORT_COMPILE_COMMANDS ON)'
	  case $k in
	  features+EXT_OFF)     echo 'set(CMAKE_C_EXTENSIONS OFF)'; echo 'add_executable(m m.c)'; echo 'target_compile_features(m PUBLIC c_std_11)' ;;
	  STANDARD11)           echo 'set(CMAKE_C_STANDARD 11)'; echo 'add_executable(m m.c)' ;;
	  STANDARD11+EXT_OFF)   echo 'set(CMAKE_C_STANDARD 11)'; echo 'set(CMAKE_C_EXTENSIONS OFF)'; echo 'add_executable(m m.c)' ;;
	  esac; } > "$p/CMakeLists.txt"
	cmake -S "$p" -B "$p/b" -G Ninja >/dev/null 2>&1
	printf '%-5s %-22s -> %s\n' "$mv" "$k" "$(cmd_of "$p/b/compile_commands.json" m.c | grep -o -- '-std=[a-z0-9]*' || echo '(no -std flag)')"
done

sec "S6 typo in a plain library name vs a namespaced (::) name"
mk typo solution
sed -i '' 's/target_link_libraries(app PRIVATE ringbuf)/target_link_libraries(app PRIVATE ringbuff)/' "$D/typo/CMakeLists.txt"
cmake -S "$D/typo" -B "$D/typo-b" -G Ninja >/dev/null 2>&1; echo "configure rc=$? (plain name accepted)"
cmake --build "$D/typo-b" -- -k 0 2>&1 | grep -E "ld:|error:|FAILED|not found" | sed "s|$D|\$D|g" | sort -u
sed -i '' 's/PRIVATE ringbuff)/PRIVATE fw::ringbuff)/' "$D/typo/CMakeLists.txt"
cmake -S "$D/typo" -B "$D/typo-b2" -G Ninja 2>&1 | grep -v '^--' | sed "s|$D|\$D|g" | head -12

sec "S7 generator-expression quoting"
mkdir -p "$D/gx"; cp "$D/m.c" "$D/gx/"
for form in 'UNQUOTED $<$<C_COMPILER_ID:AppleClang>:-Wall -Wextra>' 'QUOTED_SPACE "$<$<C_COMPILER_ID:AppleClang>:-Wall -Wextra>"' 'QUOTED_SEMI "$<$<C_COMPILER_ID:AppleClang>:-Wall;-Wextra>"'; do
	name=${form%% *}; arg=${form#* }
	printf 'cmake_minimum_required(VERSION 3.20)\nproject(p C)\nadd_executable(m m.c)\ntarget_compile_options(m PRIVATE %s)\n' "$arg" > "$D/gx/CMakeLists.txt"
	rm -rf "$D/gx/b"; cmake -S "$D/gx" -B "$D/gx/b" -G Ninja >/dev/null 2>&1
	echo "$name: $arg"
	echo "   -> $(cd "$D/gx/b" && ninja -t commands | head -1 | sed 's/.*-isysroot [^ ]* //; s/ -MD .*//')"
	cmake --build "$D/gx/b" 2>&1 | grep -m1 'warning:' | sed 's/^/   /'
done

sec "S8 RB_HAVE_CRC as PRIVATE: library and app disagree on sizeof(rb_t)"
mk abi solution
sed -i '' 's/PUBLIC RB_HAVE_CRC=/PRIVATE RB_HAVE_CRC=/; s/ PRIVATE RB_ASSERTS=1)/ RB_ASSERTS=1)/' "$D/abi/ringbuf/CMakeLists.txt"
grep -n 'target_compile_definitions' "$D/abi/ringbuf/CMakeLists.txt"
echo "--- (a) with -Wundef -Werror on the app (solution's FW_WARNINGS):"
cmake -S "$D/abi" -B "$D/abi-b" -G Ninja >/dev/null 2>&1
cmake --build "$D/abi-b" --target app 2>&1 | grep -E 'error' | head -3 | sed "s|$D|\$D|g"
echo "--- (b) same, but app built without -Wundef:"
sed -i '' 's/;-Wundef>" "\$<\$<C_COMPILER_ID:MSVC>/>" "$<$<C_COMPILER_ID:MSVC>/' "$D/abi/CMakeLists.txt"
cmake -S "$D/abi" -B "$D/abi-b" >/dev/null 2>&1
cmake --build "$D/abi-b" --target app >/dev/null 2>&1 && echo "build ok"
"$D/abi-b/app"; echo "app rc=$?"
echo "--- (b2) same, app compiled with -fno-stack-protector:"
cmake -S "$D/abi" -B "$D/abi-nsp" -G Ninja "-DCMAKE_C_FLAGS=-fno-stack-protector" >/dev/null 2>&1
cmake --build "$D/abi-nsp" --target app >/dev/null 2>&1
"$D/abi-nsp/app"; echo "app rc=$?"
echo "--- (c) same with -fsanitize=address:"
cmake -S "$D/abi" -B "$D/abi-asan" -G Ninja "-DCMAKE_C_FLAGS=-fsanitize=address -g" "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address" >/dev/null 2>&1
cmake --build "$D/abi-asan" --target app >/dev/null 2>&1
"$D/abi-asan/app" 2>&1 | grep -E 'ERROR|WRITE|READ|#[0-3] |is located|Address 0x' | head -8 | sed 's/0x[0-9a-f]\{8,\}/0x…/g; s/(\/tmp[^)]*\/\([^/)]*\))/(\1)/'

sec "S9 same project, Unix Makefiles generator (GNU Make $(make --version | head -1 | awk '{print $3}')) vs Ninja"
cmake -S "$D/sol" -B "$D/sol-mk" -G "Unix Makefiles" >/dev/null 2>&1
cmake --build "$D/sol-mk" -- -j8 2>&1 | grep -E '^\[|Built target' | tail -5
(cd "$D/sol-mk" && ctest 2>&1 | grep 'tests passed')
echo "--- clean build time, 3 runs each (noisy: other jobs share this Mac)"
for g in Ninja "Unix Makefiles"; do
	ts=""
	for i in 1 2 3; do
		rm -rf "$D/t"; cmake -S "$D/sol" -B "$D/t" -G "$g" >/dev/null 2>&1
		s=$(python3 -I -c 'import time;print(time.time())'); cmake --build "$D/t" -j 8 >/dev/null 2>&1
		e=$(python3 -I -c 'import time;print(time.time())'); ts="$ts $(python3 -I -c "print(f'{$e-$s:.2f}')")"
	done
	echo "$g:$ts s"
	echo "  no-op rebuild: $( s=$(python3 -I -c 'import time;print(time.time())'); cmake --build "$D/t" >/dev/null 2>&1; e=$(python3 -I -c 'import time;print(time.time())'); python3 -I -c "print(f'{$e-$s:.2f}')") s"
done

sec "S10 target graph (cmake --graphviz)"
(cd "$D/sol-b" && cmake --graphviz=deps.dot . >/dev/null 2>&1 && grep -E 'label|->' deps.dot | grep -v -E 'legend|^ *subgraph' | head -20)

sec "S11 what ctest sees (ctest -N, labels)"
(cd "$D/sol-b" && ctest -N && ctest -N -L unit | tail -3)
