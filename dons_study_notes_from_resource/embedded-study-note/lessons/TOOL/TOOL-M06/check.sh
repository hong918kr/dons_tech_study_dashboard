#!/usr/bin/env bash
# TOOL-M06 checker: shared libraries — libfoo.dylib (Mac ld64/dyld) and libfoo.so (Lima VM GNU ld/ld.so).
#   bash check.sh <starter|solution|practice>
# Sources <variant>/build.conf, builds <variant>/foo.c + src/ into a library and src/app.c into an app,
# installs both into a relocatable bundle (bin/ + lib/), moves the bundle, and checks load commands,
# exported symbols and an ABI drop-in with otool/nm (Mac) and readelf/nm/ldd (VM).
set -u
cd "$(dirname "$0")" || exit 2
LESSON=$(pwd)
V=${1:-}
case "$V" in
starter | solution | practice) ;;
*) echo "usage: bash check.sh <starter|solution|practice>"; exit 2 ;;
esac
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk

# shellcheck disable=SC1090
source "./$V/build.conf" || { echo "FAIL cannot source $V/build.conf"; exit 1; }

W=/tmp/TOOL-M06_work/$V
rm -rf "$W"
mkdir -p "$W/mac/src" "$W/mac/build" "$W/mac/build2"
OUT=$W/results.txt
: >"$OUT"
pass() { echo "PASS $*" | tee -a "$OUT"; }
fail() { echo "FAIL $*" | tee -a "$OUT"; }
check() { if [ "$1" = 1 ]; then pass "$2"; else fail "$2"; fi; }

EXP=$'libfoo 1.2.0 (app abi 1)\navg: 10 12 13 16 21 28 22\nstats: count=7 min=-5 max=40 guard=ok\napp round_div(7,2)=3'
EXP2=$'libfoo 2.0.0 (app abi 2)\navg: 10 12 13 16 21 28 22\nstats: count=7 min=-5 max=40 avg=22 guard=ok\napp round_div(7,2)=3'
API="foo_abi_check foo_close foo_get_stats foo_open foo_push foo_version"
CW="-std=c11 -Wall -Wextra -Werror"

# ===================== Mac: ld64 + dyld =====================
echo "--- Mac (Apple ld64 + dyld) ---"
cp src/* "$V"/* "$W/mac/src/"
cd "$W/mac/src" || exit 2
B=$W/mac/build
mac_lib() { # mac_lib <outdir> <extra cflags> <ldflags>
	# shellcheck disable=SC2086
	cc $CW $LIB_CFLAGS $2 -I. -c foo.c -o "$1/foo.o" &&
		cc $CW $LIB_CFLAGS $2 -I. -c foo_util.c -o "$1/foo_util.o" &&
		cc $3 "$1/foo.o" "$1/foo_util.o" -o "$1/libfoo.dylib"
}
mac_app() { # mac_app <libdir> <extra cflags> <out> [ldflags, default $MAC_APP_LDFLAGS]
	# shellcheck disable=SC2086
	cc $CW -O2 $2 -I. app.c app_util.c -L"$1" -lfoo ${4-$MAC_APP_LDFLAGS} -o "$3"
}
install_name() { otool -D "$1" | sed -n 2p; }
mac_lib "$B" "" "$MAC_LIB_LDFLAGS" >"$W/mac/lib.log" 2>&1 &&
	mac_app "$B" "" "$B/app" >"$W/mac/app.log" 2>&1 ||
	{ fail "Mac build"; cat "$W/mac/lib.log" "$W/mac/app.log" | head -5; }

IN=$(install_name "$B/libfoo.dylib" 2>/dev/null)
LEAF=$(basename "${IN:-libfoo.dylib}")
check "$(echo "$IN" | grep -qE '^@rpath/libfoo(\.[0-9]+)?\.dylib$' && echo 1)" \
	"A install name (otool -D) is @rpath-relative: '${IN:-none}'"
LOADCMD=$(otool -L "$B/app" 2>/dev/null | grep libfoo | sed 's/^[[:space:]]*//')
RPATHS=$(otool -l "$B/app" 2>/dev/null | awk '/cmd LC_RPATH/{f=1;next} f&&/path /{print $2; f=0}' | tr '\n' ' ')
ok=0
case "$LOADCMD" in "$IN "*) [ "$RPATHS" = "@loader_path/../lib " ] && ! otool -l "$B/app" | tail -n +2 | grep -q "$W" && ok=1 ;; esac
check $ok "A app: LC_LOAD_DYLIB=@rpath name, LC_RPATH='${RPATHS% }' (want @loader_path/../lib), no build path"

# install: bundle/bin/app + bundle/lib/<install-name leaf>
BUN=$W/mac/stage/foo-1.2.0
mkdir -p "$BUN/bin" "$BUN/lib"
cp "$B/app" "$BUN/bin/" 2>/dev/null
cp "$B/libfoo.dylib" "$BUN/lib/$LEAF" 2>/dev/null
got=$("$BUN/bin/app" 2>&1)
check "$([ "$got" = "$EXP" ] && echo 1)" "A installed bundle runs (build dir still present): '$(echo "$got" | head -1)'"
mkdir -p "$W/mac/moved/opt"
mv "$BUN" "$W/mac/moved/opt/" && mv "$B" "$W/mac/build.gone"
B=$W/mac/build.gone
MB=$W/mac/moved/opt/foo-1.2.0
got=$("$MB/bin/app" 2>&1)
check "$([ "$got" = "$EXP" ] && echo 1)" "A bundle moved + build dir deleted -> still runs: '$(echo "$got" | head -1)'"
check "$(echo "$LOADCMD" | grep -q 'compatibility version 1.0.0, current version 1.2.0' && echo 1)" \
	"A versions recorded in app: '$(echo "$LOADCMD" | sed 's/.*(\(.*\))/\1/')'"

EXPORTS=$(nm -gU "$B/libfoo.dylib" 2>/dev/null | awk '{print $3}' | sed 's/^_//' | sort | tr '\n' ' ')
check "$([ "$EXPORTS" = "$API " ] && echo 1)" "B exported set (nm -gU) == API only: '${EXPORTS% }'"

# Part C: libfoo 2.0.0 (struct foo_stats grew) dropped in under the SAME name, old app not rebuilt
mac_lib "$W/mac/build2" "-DFOO_ABI_V2" "$MAC_LIB_LDFLAGS" >"$W/mac/lib2.log" 2>&1 || fail "Mac build v2 lib"
mkdir -p "$W/mac/dropin"
cp "$W/mac/build2/libfoo.dylib" "$W/mac/dropin/$LEAF" 2>/dev/null
got=$(DYLD_LIBRARY_PATH=$W/mac/dropin "$B/app" 2>&1)
rc=$?
check "$([ $rc = 3 ] && echo "$got" | grep -q 'ABI mismatch' && ! echo "$got" | grep -q CLOBBERED && echo 1)" \
	"C old app + v2 lib (DYLD_LIBRARY_PATH drop-in) refused, rc=$rc: '$(echo "$got" | grep -E 'mismatch|stats' | head -1)'"
mac_app "$W/mac/build2" "-DFOO_ABI_V2" "$W/mac/build2/app2" >"$W/mac/app2.log" 2>&1
mkdir -p "$W/mac/v2run"
cp "$W/mac/build2/libfoo.dylib" "$W/mac/v2run/$LEAF" 2>/dev/null
got=$(DYLD_LIBRARY_PATH=$W/mac/v2run "$W/mac/build2/app2" 2>&1)
check "$([ "$got" = "$EXP2" ] && echo 1)" "C v2 app + v2 lib still accepted: '$(echo "$got" | sed -n 3p)'"

# ---- practice extras (Mac) ----
if declare -F fixup_legacy >/dev/null; then
	L=$W/mac/legacy
	mkdir -p "$L/build" "$L/pkg/bin" "$L/pkg/lib"
	mac_lib "$L/build" "" "-dynamiclib" >/dev/null 2>&1 && mac_app "$L/build" "" "$L/build/app" "" >/dev/null 2>&1
	cp "$L/build/app" "$L/pkg/bin/" && cp "$L/build/libfoo.dylib" "$L/pkg/lib/" && rm -rf "$L/build"
	(fixup_legacy "$L/pkg/bin/app" "$L/pkg/lib/libfoo.dylib") >"$L/fixup.log" 2>&1
	mv "$L/pkg" "$L/pkg.moved"
	got=$("$L/pkg.moved/bin/app" 2>&1)
	check "$([ "$got" = "$EXP" ] && echo 1)" "P1 prebuilt app fixed with install_name_tool, moved, runs: '$(echo "$got" | head -1)'"
	check "$(codesign -v "$L/pkg.moved/bin/app" 2>/dev/null && codesign -v "$L/pkg.moved/lib/libfoo.dylib" 2>/dev/null &&
		[ "$(install_name "$L/pkg.moved/lib/libfoo.dylib")" = "@rpath/libfoo.dylib" ] && echo 1)" \
		"P1 signatures valid (codesign -v) and library id fixed to @rpath/libfoo.dylib"
fi
if [ -n "${MAC_LIB2_LDFLAGS-}" ]; then
	S=$W/mac/sbs
	mkdir -p "$S/b2" "$S/foo/bin" "$S/foo/lib"
	mac_lib "$S/b2" "-DFOO_ABI_V2" "$MAC_LIB2_LDFLAGS" >/dev/null 2>&1 && mac_app "$S/b2" "-DFOO_ABI_V2" "$S/b2/app2" >/dev/null 2>&1
	IN2=$(install_name "$S/b2/libfoo.dylib")
	cp "$MB/bin/app" "$S/foo/bin/app" && cp "$MB/lib/$LEAF" "$S/foo/lib/"
	cp "$S/b2/app2" "$S/foo/bin/app2" && cp "$S/b2/libfoo.dylib" "$S/foo/lib/$(basename "$IN2")"
	g1=$("$S/foo/bin/app" 2>&1)
	g2=$("$S/foo/bin/app2" 2>&1)
	check "$([ "$g1" = "$EXP" ] && [ "$g2" = "$EXP2" ] && [ "$IN2" != "$IN" ] && echo 1)" \
		"P2 Mac side by side: lib/{$(ls "$S/foo/lib" | tr '\n' ' ' | sed 's/ $//')}, old app + new app both run"
fi

# ===================== Lima VM: GNU ld + glibc ld.so =====================
echo "--- Lima VM elinux (gcc 15.2 / GNU ld 2.46 / glibc ld.so) ---"
{
	declare -p LESSON V EXP EXP2 API CW
	for n in LIB_VERSION LIB_CFLAGS ELF_LIB_LDFLAGS ELF_APP_LDFLAGS ELF_LIB2_LDFLAGS LIB2_VERSION; do
		printf '%s=%q\n' "$n" "${!n-}"
	done
	cat <<'VM'
set -u
W=/tmp/TOOL-M06_work/$V/vm
rm -rf "$W"; mkdir -p "$W/src" "$W/build" "$W/build2" "$W/dropin"
cp "$LESSON"/src/* "$LESSON/$V"/* "$W/src/"; cd "$W/src" || exit 1
pass() { echo "PASS $*"; }
fail() { echo "FAIL $*"; }
check() { if [ "$1" = 1 ]; then pass "$2"; else fail "$2"; fi; }
B=$W/build
elf_lib() { # elf_lib <outdir> <extra cflags> <ldflags> <version>
	gcc $CW $LIB_CFLAGS $2 -I. -c foo.c -o "$1/foo.o" &&
	gcc $CW $LIB_CFLAGS $2 -I. -c foo_util.c -o "$1/foo_util.o" &&
	gcc $3 "$1/foo.o" "$1/foo_util.o" -o "$1/libfoo.so.$4" &&
	ln -sf "libfoo.so.$4" "$1/libfoo.so" && /usr/sbin/ldconfig -n "$1"
}
elf_app() { gcc $CW -O2 $2 -I. app.c app_util.c -L"$1" -lfoo $ELF_APP_LDFLAGS -o "$3"; }
dyn() { readelf -d "$1" | sed -n "s/.*($2).*\[\(.*\)\]/\1/p" | tr '\n' ' ' | sed 's/ $//'; }
elf_lib "$B" "" "$ELF_LIB_LDFLAGS" "$LIB_VERSION" >"$W/lib.log" 2>&1 && elf_app "$B" "" "$B/app" >"$W/app.log" 2>&1 ||
	{ fail "VM build"; head -5 "$W/lib.log" "$W/app.log"; }
REAL=$B/libfoo.so.$LIB_VERSION
SON=$(dyn "$REAL" SONAME)
check "$([ "$SON" = libfoo.so.1 ] && [ -L "$B/libfoo.so.1" ] && echo 1)" \
	"V SONAME='$SON' (want libfoo.so.1) + ldconfig -n made link: libfoo.so.1 -> $(readlink "$B/libfoo.so.1")"
NEED=$(dyn "$B/app" NEEDED | tr ' ' '\n' | grep libfoo)
RUNP=$(dyn "$B/app" RUNPATH); RP=$(dyn "$B/app" RPATH)
check "$([ "$NEED" = libfoo.so.1 ] && echo 1)" "V app NEEDED='$NEED' (want the SONAME, not the dev link libfoo.so)"
check "$([ "$RUNP" = '$ORIGIN/../lib' ] && [ -z "$RP" ] && echo 1)" "V app RUNPATH='$RUNP' RPATH='$RP' (want RUNPATH \$ORIGIN/../lib)"
# runtime package: real file + SONAME link only (no dev link), then move it and delete the build tree
P=$W/stage/foo-1.2.0; mkdir -p "$P/bin" "$P/lib"
cp "$B/app" "$P/bin/"; cp "$REAL" "$P/lib/"; [ -n "$SON" ] && [ -L "$B/$SON" ] && cp -P "$B/$SON" "$P/lib/"
mkdir -p "$W/moved/opt"; mv "$P" "$W/moved/opt/"; mv "$B" "$W/build.gone"; B=$W/build.gone
MP=$W/moved/opt/foo-1.2.0
got=$("$MP/bin/app" 2>&1); rc=$?
check "$([ $rc = 0 ] && echo "$got" | head -1 | grep -q '^libfoo 1.2.0' && echo 1)" \
	"V runtime bundle (no libfoo.so dev link) moved + build deleted -> loads: '$(echo "$got" | head -1)'"
got=$(LD_LIBRARY_PATH=$B "$B/app" 2>&1)
check "$([ "$got" = "$EXP" ] && echo 1)" "V lib's internal round_div not interposed by the app's: '$(echo "$got" | sed -n 2p)'"
EXPORTS=$(nm -D --defined-only "$B/libfoo.so.$LIB_VERSION" 2>/dev/null | awk '$2 ~ /^[TDBR]$/ {print $3}' | sed 's/@.*//' | sort | tr '\n' ' ')
check "$([ "$EXPORTS" = "$API " ] && echo 1)" "V exported set (nm -D --defined-only) == API only: '${EXPORTS% }'"
elf_lib "$W/build2" "-DFOO_ABI_V2" "$ELF_LIB_LDFLAGS" 2.0.0 >"$W/lib2.log" 2>&1 || fail "VM build v2 lib"
cp "$W/build2/libfoo.so.2.0.0" "$W/dropin/${NEED:-libfoo.so}"
got=$(LD_LIBRARY_PATH=$W/dropin "$B/app" 2>&1); rc=$?
check "$([ $rc = 3 ] && echo "$got" | grep -q 'ABI mismatch' && ! echo "$got" | grep -q CLOBBERED && echo 1)" \
	"V old app + v2 lib (same file name) refused, rc=$rc: '$(echo "$got" | grep -E 'mismatch|stats' | head -1)'"

if [ "$V" = practice ]; then
	VS=$(readelf --dyn-syms -W "$B/libfoo.so.$LIB_VERSION" | awk '$7 != "UND" && /foo_open/ {print $8}')
	VN=$(readelf -V -W "$B/app" | grep -o 'Name: FOO_1.0' | head -1)
	check "$([ "$VS" = 'foo_open@@FOO_1.0' ] && [ -n "$VN" ] && echo 1)" \
		"P3 version script: lib exports '$VS', app needs '${VN#Name: }'"
	if [ -n "$ELF_LIB2_LDFLAGS" ]; then
		S=$W/sbs; mkdir -p "$S/b2" "$S/foo/bin" "$S/foo/lib"
		elf_lib "$S/b2" "-DFOO_ABI_V2" "$ELF_LIB2_LDFLAGS" "$LIB2_VERSION" >/dev/null 2>&1 &&
			elf_app "$S/b2" "-DFOO_ABI_V2" "$S/b2/app2" >/dev/null 2>&1
		S2=$(dyn "$S/b2/libfoo.so.$LIB2_VERSION" SONAME)
		cp "$MP/bin/app" "$S/foo/bin/"; cp -P "$MP/lib/"* "$S/foo/lib/"
		cp "$S/b2/app2" "$S/foo/bin/"; cp -P "$S/b2/libfoo.so.$LIB2_VERSION" "$S/b2/$S2" "$S/foo/lib/" 2>/dev/null
		g1=$("$S/foo/bin/app" 2>&1); g2=$("$S/foo/bin/app2" 2>&1)
		check "$([ "$g1" = "$EXP" ] && [ "$g2" = "$EXP2" ] && [ "$S2" = libfoo.so.2 ] && echo 1)" \
			"P2 ELF side by side: lib/{$(ls "$S/foo/lib" | tr '\n' ' ' | sed 's/ $//')}, old app + new app both run"
	fi
fi
VM
} | limactl shell elinux -- bash -s 2>&1 | tee -a "$OUT"

np=$(grep -c '^PASS' "$OUT")
nf=$(grep -c '^FAIL' "$OUT")
echo "$np/$((np + nf)) passed"
[ "$nf" = 0 ] && [ "$np" -gt 0 ]
