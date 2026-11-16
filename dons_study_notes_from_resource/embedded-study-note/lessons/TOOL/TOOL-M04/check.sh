#!/usr/bin/env bash
# TOOL-M04 checker — bash check.sh <starter|solution|practice>
# Lays proj/ + <variant>/ (CMakeLists.txt files) into /tmp/TOOL-M04_work/<variant>/src, then asserts
# with the real tools (cmake 4.5 + Ninja + AppleClang + ctest + nm) that the target-based CMake is right.
# practice = solution + practice/CMakeLists.txt laid on top (ringbuf/CMakeLists.txt is the solution's).
set -u
cd "$(dirname "$0")" || exit 2
V=${1:-}
case "$V" in starter|solution|practice) ;; *) echo "usage: bash check.sh <starter|solution|practice>"; exit 2 ;; esac
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK link is broken
export PATH=/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin
LESSON=$(pwd)
W=/tmp/TOOL-M04_work/$V
S=$W/src            # source tree (read-mostly)
B=$W/build          # build tree, FEATURE_CRC default (ON)
rm -rf "$W" && mkdir -p "$S" || exit 2
cp -R proj/. "$S/" || exit 2
[ "$V" = practice ] && { cp -R solution/. "$S/" || exit 2; }
cp -R "$V/." "$S/" || exit 2

pass=0; total=0
ok()  { total=$((total + 1)); pass=$((pass + 1)); echo "PASS $1"; }
bad() { total=$((total + 1)); echo "FAIL $1"; [ -n "${2:-}" ] && echo "     $2"; return 0; }
# compile command of the entry whose "file" ends with $2, from compile_commands.json $1
cc_cmd() { python3 -I -c 'import json,sys
for e in json.load(open(sys.argv[1])):
    if e["file"].endswith(sys.argv[2]): print(e["command"]); break' "$1" "$2" 2>/dev/null; }
# basenames of the C files ninja WOULD compile (sorted, space separated)
would_compile() { ninja -C "$1" -n 2>/dev/null | sed -n 's/.*Building C object .*\/\([A-Za-z0-9_]*\)\.c\.o$/\1/p' | sort | tr '\n' ' ' | sed 's/ $//'; }

if [ "$V" = practice ]; then NTESTS=4; VER=1.4.2; else NTESTS=3; VER=dev; fi
EXPECT_ON="[app $VER] samples=64 sum=4499 crc=0x39C4"
EXPECT_OFF="[app $VER] samples=64 sum=4499 crc=off"

# probes: two "new app files" added to the project the way a teammate would (same top-level file)
mkdir -p "$S/probe" && cp probe/*.c "$S/probe/"
cat >>"$S/CMakeLists.txt" <<'EOF'

# ---- appended by check.sh: probe consumers (EXCLUDE_FROM_ALL = built only on request) ----
add_executable(probe_private EXCLUDE_FROM_ALL probe/probe_private.c)
target_link_libraries(probe_private PRIVATE ringbuf)
add_executable(probe_public EXCLUDE_FROM_ALL probe/probe_public.c)
target_link_libraries(probe_public PRIVATE ringbuf)
EOF

echo "== $V: $(cmake --version | head -1), ninja $(ninja --version), $(cc --version | head -1) =="
# 1-2 configure + build + run
if cmake -S "$S" -B "$B" -G Ninja >"$W/configure.log" 2>&1; then ok "configure -G Ninja (out-of-source: $B)"
else bad "configure" "$(grep -m2 -E 'Error|error' "$W/configure.log")"; fi
if cmake --build "$B" >"$W/build.log" 2>&1 && [ "$("$B/app")" = "$EXPECT_ON" ]; then ok "build all + app prints '$EXPECT_ON'"
else bad "build all / app output" "$(grep -m2 -E 'error|FAILED' "$W/build.log")"; fi

# 3-4 CTest
(cd "$B" && ctest --output-on-failure >"$W/ctest.log" 2>&1); rc=$?
if grep -q "100% tests passed.* out of $NTESTS\$" "$W/ctest.log"; then ok "ctest: 100% tests passed out of $NTESTS"
else bad "ctest: $NTESTS tests registered and passing" "$(grep -m1 -E 'No tests|tests passed|failed' "$W/ctest.log")  (ctest rc=$rc)"; fi
(cd "$B" && ctest -L unit >"$W/ctest_unit.log" 2>&1)
if grep -q "100% tests passed.* out of 2\$" "$W/ctest_unit.log"; then ok "ctest -L unit: exactly 2 tests (test_ringbuf, test_crc)"
else bad "ctest -L unit (labels)" "$(grep -m1 -E 'No tests|tests passed' "$W/ctest_unit.log")"; fi

# 5-6 compile_commands.json: warnings on every target; PUBLIC define reaches the app, PRIVATE one doesn't
CJ=$B/compile_commands.json
c_lib=$(cc_cmd "$CJ" /ringbuf/src/ringbuf.c); c_app=$(cc_cmd "$CJ" /app/main.c)
if [ -n "$c_lib" ] && [ -n "$c_app" ] && echo "$c_lib" | grep -q -- '-Werror' && echo "$c_app" | grep -q -- '-Wall .*-Werror'
then ok "compile_commands.json exported; -Wall..-Werror on ringbuf.c and main.c"
else bad "compile_commands.json / warning flags" "$( [ -f "$CJ" ] || echo 'no compile_commands.json')"; fi
if echo "$c_app" | grep -q -- '-DRB_HAVE_CRC=1' && ! echo "$c_app" | grep -q 'RB_ASSERTS'
then ok "main.c gets PUBLIC -DRB_HAVE_CRC=1, not the library's PRIVATE RB_ASSERTS"
else bad "definition scope on main.c" "$(echo "$c_app" | grep -o -- '-D[A-Z_]*=[0-9]*' | tr '\n' ' ')"; fi

# 7-8 try-builds: private header must be invisible to the app, public API must just work
cmake --build "$B" --target probe_private >"$W/probe_private.log" 2>&1; rc=$?
if [ $rc -ne 0 ] && grep -q "'rb_internal.h' file not found" "$W/probe_private.log"
then ok "probe_private: #include \"rb_internal.h\" fails ('rb_internal.h' file not found)"
else bad "probe_private: app code can include the library's PRIVATE header" "build rc=$rc"; fi
if cmake --build "$B" --target probe_public >"$W/probe_public.log" 2>&1 && "$B/probe_public"
then ok "probe_public: only target_link_libraries(... ringbuf) needed"
else bad "probe_public" "$(grep -m1 error "$W/probe_public.log")"; fi

# 9-10 incremental: Ninja + compiler depfiles (TOOL-B08's -MMD, for free)
if cmake --build "$B" 2>&1 | grep -q 'ninja: no work to do'; then ok "second build: ninja: no work to do."
else bad "second build is not a no-op"; fi
touch "$S/ringbuf/src/rb_internal.h"
got=$(would_compile "$B")
if [ "$got" = "rb_crc ringbuf test_ringbuf" ]; then ok "touch rb_internal.h -> ninja -n compiles {$got}"
else bad "touch rb_internal.h" "want {rb_crc ringbuf test_ringbuf}, got {$got}"; fi

# 11 reuse: another project (bootloader) consumes ringbuf/ alone
cp -R consumer "$W/consumer"
if cmake -S "$W/consumer" -B "$W/boot" -G Ninja -DRINGBUF_DIR="$S/ringbuf" >"$W/boot.log" 2>&1 \
   && cmake --build "$W/boot" -- -k 0 >>"$W/boot.log" 2>&1 && out=$("$W/boot/boot") \
   && [ "$out" = "[boot] accepted=16 last=P sizeof(rb_t)=28 lib=28" ]
then ok "consumer/ (bootloader) builds with add_subdirectory(ringbuf) alone: $out"
else bad "consumer/ (bootloader) reuse of ringbuf" "$(grep -o 'error: .*' "$W/boot.log" | sort | uniq -c | sed 's/^ *//' | paste -sd ';' -)"; fi

# 12-13 option(FEATURE_CRC) OFF changes the symbol set, the struct layout and the test list
BO=$W/build-off
cmake -S "$S" -B "$BO" -G Ninja -DFEATURE_CRC=OFF >"$W/off.log" 2>&1 && cmake --build "$BO" >>"$W/off.log" 2>&1
syms_on=$(nm -g "$B/ringbuf/libringbuf.a" 2>/dev/null | grep -c ' T _rb_')
syms_off=$(nm -g "$BO/ringbuf/libringbuf.a" 2>/dev/null | grep -c ' T _rb_')
if ! nm -g "$BO/ringbuf/libringbuf.a" 2>/dev/null | grep -q '_rb_crc16' && [ "$("$BO/app" 2>/dev/null)" = "$EXPECT_OFF" ] \
   && grep -q 'FEATURE_CRC:BOOL=OFF' "$BO/CMakeCache.txt"
then ok "FEATURE_CRC=OFF: rb_crc16 gone (T _rb_* $syms_on -> $syms_off), app prints crc=off"
else bad "FEATURE_CRC=OFF has no effect" "T _rb_* symbols ON=$syms_on OFF=$syms_off; app: $("$BO/app" 2>/dev/null)"; fi
(cd "$BO" && ctest --output-on-failure >"$W/ctest_off.log" 2>&1)
if grep -q "100% tests passed.* out of $((NTESTS - 1))\$" "$W/ctest_off.log"; then ok "FEATURE_CRC=OFF: test_crc not built, $((NTESTS - 1)) tests pass"
else bad "FEATURE_CRC=OFF test list" "$(grep -m1 -E 'No tests|tests passed|failed' "$W/ctest_off.log")"; fi

# 14 the user's / CI's CMAKE_C_FLAGS must survive (configure only: compile_commands.json is written at generate)
BA=$W/build-asan
cmake -S "$S" -B "$BA" -G Ninja "-DCMAKE_C_FLAGS=-fsanitize=address" >"$W/asan.log" 2>&1
if cc_cmd "$BA/compile_commands.json" /ringbuf/src/ringbuf.c | grep -q -- '-fsanitize=address'
then ok "-DCMAKE_C_FLAGS=-fsanitize=address reaches ringbuf.c (CI ASan job really is ASan)"
else bad "-DCMAKE_C_FLAGS=-fsanitize=address was dropped" "cache: $(grep '^CMAKE_C_FLAGS:' "$BA/CMakeCache.txt") | ringbuf.c gets: $(cc_cmd "$BA/compile_commands.json" /ringbuf/src/ringbuf.c | grep -oE -- ' -(std|W|f)[^ ]*' | tr -d '\n')"; fi

if [ "$V" = practice ]; then
	echo "-- practice --"
	# P1 alias: a namespaced name links; a typo is a configure-time error
	cat >>"$S/CMakeLists.txt" <<'EOF'
add_executable(probe_alias EXCLUDE_FROM_ALL probe/probe_public.c)
target_link_libraries(probe_alias PRIVATE fw::ringbuf)
file(GENERATE OUTPUT ${PROJECT_BINARY_DIR}/probe_props.txt CONTENT
"app_links=$<TARGET_PROPERTY:app,LINK_LIBRARIES>
warn_opts=$<TARGET_PROPERTY:fw_warnings,INTERFACE_COMPILE_OPTIONS>
")
EOF
	if cmake -S "$S" -B "$B" >/dev/null 2>&1 && cmake --build "$B" --target probe_alias >/dev/null 2>&1 && "$B/probe_alias"
	then ok "P1 fw::ringbuf alias links"; else bad "P1 fw::ringbuf alias"; fi
	cp "$S/CMakeLists.txt" "$W/CMakeLists.save"
	echo 'target_link_libraries(probe_alias PRIVATE fw::ringbuff)' >>"$S/CMakeLists.txt"
	cmake -S "$S" -B "$B" >"$W/typo.log" 2>&1; rc=$?
	if [ $rc -ne 0 ] && grep -q 'fw::ringbuff' "$W/typo.log" && grep -q 'target was not found' "$W/typo.log"
	then ok "P1 typo fw::ringbuff -> configure error (rc=$rc): target was not found"
	else bad "P1 typo fw::ringbuff not caught at configure" "rc=$rc"; fi
	cp "$W/CMakeLists.save" "$S/CMakeLists.txt"; cmake -S "$S" -B "$B" >/dev/null 2>&1
	# P2 INTERFACE warnings target
	if grep -q '^app_links=.*fw::warnings' "$B/probe_props.txt" && grep -q 'Wconversion' "$B/probe_props.txt" \
	   && cc_cmd "$CJ" /app/main.c | grep -q -- '-Wconversion' && ! cc_cmd "$CJ" /ringbuf/src/ringbuf.c | grep -q -- '-Wconversion'
	then ok "P2 app links fw::warnings; -Wconversion on main.c only (ringbuf keeps its own set)"
	else bad "P2 fw::warnings INTERFACE target" "$(cat "$B/probe_props.txt" 2>/dev/null | tr '\n' ' ')"; fi
	# P3 configure_file into the build tree
	if [ -f "$B/generated/build_info.h" ] && ! find "$S" -name build_info.h | grep -q . \
	   && grep -q '#define FW_VERSION_STR "1.4.2"' "$B/generated/build_info.h" && grep -q '#define FEATURE_CRC 1' "$B/generated/build_info.h" \
	   && grep -q '#define FEATURE_CRC 0' "$BO/generated/build_info.h"
	then ok "P3 build_info.h generated in <build>/generated (CRC 1 / OFF tree 0), source tree untouched"
	else bad "P3 configure_file"; fi
	# P4 negative test + timeouts
	(cd "$B" && ctest -L negative >"$W/neg.log" 2>&1)
	(cd "$B" && ctest --show-only=json-v1 >"$W/tests.json" 2>/dev/null)
	nto=$(python3 -I -c 'import json,sys
d=json.load(open(sys.argv[1]))
print(sum(1 for t in d["tests"] for p in t.get("properties",[]) if p["name"]=="TIMEOUT" and float(p["value"])<=10))' "$W/tests.json" 2>/dev/null)
	if grep -q "100% tests passed.* out of 1\$" "$W/neg.log" && [ "$nto" = 4 ]
	then ok "P4 ctest -L negative: app_rejects_args passes (WILL_FAIL); TIMEOUT<=10 on all 4 tests"
	else bad "P4 negative test / timeouts" "timeouts=$nto; $(grep -m1 -E 'No tests|passed' "$W/neg.log")"; fi
fi

echo "$pass/$total passed"
[ "$pass" = "$total" ] && exit 0 || exit 1
