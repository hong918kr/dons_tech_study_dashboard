#!/usr/bin/env bash
# TOOL-M04 — same solution tree in the Lima VM (aarch64 Ubuntu: cmake 4.2, gcc 15, Ninja, GNU Make 4.4.1).
#   bash demo_vm.sh > demo_vm_out.txt 2>&1
# The Mac home is mounted read-only in the VM -> everything is copied to VM /tmp/TOOL-M04_work/vm first.
set -u
L=$(cd "$(dirname "$0")" && pwd)
limactl shell elinux -- bash -s "$L" <<'VM'
set -u
L=$1; W=/tmp/TOOL-M04_work/vm
rm -rf "$W" && mkdir -p "$W/src" && cp -R "$L/proj/." "$W/src/" && cp -R "$L/solution/." "$W/src/"
mkdir -p "$W/src/probe" && cp "$L"/probe/*.c "$W/src/probe/"
cat >>"$W/src/CMakeLists.txt" <<'EOF'
add_executable(probe_private EXCLUDE_FROM_ALL probe/probe_private.c)
target_link_libraries(probe_private PRIVATE ringbuf)
EOF
cmd_of() { python3 -I -c 'import json,sys
for e in json.load(open(sys.argv[1])):
    if e["file"].endswith(sys.argv[2]): print(e["command"]); break' "$1" "$2"; }
echo "######## V1 versions"
cmake --version | head -1; echo "ninja $(ninja --version)"; gcc --version | head -1; make --version | head -1
echo; echo "######## V2 configure + build + ctest (gcc, Ninja)"
cmake -S "$W/src" -B "$W/b" -G Ninja 2>&1 | grep -E 'identification|Configuring done|Generating done'
cmake --build "$W/b" 2>&1 | tail -3
(cd "$W/b" && ctest --output-on-failure 2>&1 | grep -E 'Test +#|tests passed')
"$W/b/app"
echo; echo "######## V3 genex picked the GNU branch; PUBLIC/PRIVATE definitions (main.c vs ringbuf.c)"
cmd_of "$W/b/compile_commands.json" app/main.c | sed "s|$W|\$W|g"
cmd_of "$W/b/compile_commands.json" ringbuf/src/ringbuf.c | sed "s|$W|\$W|g"
echo; echo "######## V4 probe_private with gcc"
cmake --build "$W/b" --target probe_private 2>&1 | grep -E 'error|FAILED' | sed "s|$W|\$W|g"
echo; echo "######## V5 ELF static library symbols: FEATURE_CRC ON vs OFF (nm -g, defined only)"
cmake -S "$W/src" -B "$W/b-off" -G Ninja -DFEATURE_CRC=OFF >/dev/null 2>&1 && cmake --build "$W/b-off" >/dev/null 2>&1
echo "--- ON:";  nm -g --defined-only "$W/b/ringbuf/libringbuf.a"
echo "--- OFF:"; nm -g --defined-only "$W/b-off/ringbuf/libringbuf.a"
"$W/b-off/app"; (cd "$W/b-off" && ctest 2>&1 | grep 'tests passed')
echo; echo "######## V6 Unix Makefiles generator with GNU Make 4.4.1"
cmake -S "$W/src" -B "$W/b-mk" -G "Unix Makefiles" >/dev/null 2>&1
cmake --build "$W/b-mk" -j 4 2>&1 | grep -E 'Built target'
(cd "$W/b-mk" && ctest 2>&1 | grep 'tests passed')
echo "--- after the first build: CMakeFiles/app.dir/compiler_depend.make"
head -2 "$W/b-mk/CMakeFiles/app.dir/compiler_depend.make"
touch "$W/src/ringbuf/include/ringbuf.h"
echo "--- touch ringbuf.h; make -n | grep 'Building' (dry run):"
(cd "$W/b-mk" && make -n 2>/dev/null | grep -c 'Building C object')
echo "--- real make:"
(cd "$W/b-mk" && make 2>&1 | grep 'Building C object')
echo "--- compiler_depend.make now (merged from the compiler's .d files during the 'depend' step):"
sed -n '4,7p' "$W/b-mk/CMakeFiles/app.dir/compiler_depend.make" | sed "s|$W|\$W|g"
VM
