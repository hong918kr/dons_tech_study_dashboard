#!/usr/bin/env bash
# TOOL-M10 checker (Form S):  bash check.sh <starter|solution|practice>
#   starter|solution : <variant>.c (evlog 모듈 + 드라이버), 버그 하나 = 도구 하나 = 줄 하나
#       1. -Wall -Wextra -Werror -O2 빌드
#       2. 자체 테스트: 일반 빌드로 `all` → 결과 5줄이 정답과 같은가
#       3. overflow  ASan          : "ERROR: AddressSanitizer" 없음 + 결과 줄
#       4. uaf       ASan          : 같음
#       5. leak      leaks --atExit: "0 leaks for 0 total leaked bytes." (exit 0)
#       6. cache     leaks --outputGraph + malloc_history -allBySize: 종료 시점에
#                    dev_intern 이 할당한 블록이 살아 있지 않아야 함 (leaks 는 0 이라고 해도)
#       7. uninit    MallocScribble=1 (새 블록을 0xaa 로 채움) 에서도 결과 줄이 같은가
#       8. all       Guard Malloc (DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib) 에서 정상 종료
#   practice : practice.c (고친 버전) 이 모든 도구에서 깨끗하고, -DBUGGY 의 버그 3개가 각각 잡히는가
# 맥 전용 (VM 비교는 demo_vm.sh). 작업 디렉터리: /tmp/TOOL-M10_work/<variant>
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
export ASAN_OPTIONS=abort_on_error=0      # 맥 ASan 기본은 abort(rc 134 + 크래시 리포트) → rc 1 로
GM=/usr/lib/libgmalloc.dylib

V=${1:-}
case "$V" in
starter | solution | practice) ;;
*) echo "usage: bash check.sh <starter|solution|practice>"; exit 2 ;;
esac
W=/tmp/TOOL-M10_work/$V
rm -rf "$W" && mkdir -p "$W" || exit 2

PASS=0 TOTAL=0
ok()  { TOTAL=$((TOTAL + 1)); PASS=$((PASS + 1)); echo "PASS $*"; }
bad() { TOTAL=$((TOTAL + 1)); echo "FAIL $*"; }

STRICT="-std=c11 -Wall -Wextra -Werror -O2"
DBG="-std=c11 -g -O0"
ASAN="-std=c11 -g -O1 -fno-inline -fno-omit-frame-pointer -fsanitize=address"

# asan_clean <bin> <case> <expected line> [label]
asan_clean() {
	local bin=$1 c=$2 want=$3 lab=${4:-$2/asan}
	("$bin" "$c" >"$W/$c.asan.out" 2>"$W/$c.asan.err"; exit $?) 2>/dev/null
	local rc=$? got
	got=$(head -n 1 "$W/$c.asan.out")
	if grep -q "ERROR: AddressSanitizer" "$W/$c.asan.err"; then
		bad "$lab  $(grep -m1 '^SUMMARY:' "$W/$c.asan.err" | sed 's/^SUMMARY: //')"
	elif [ $rc -ne 0 ] || [ "$got" != "$want" ]; then
		bad "$lab  rc=$rc output '$got' want '$want'"
	else
		ok "$lab  ($got)"
	fi
}

# asan_caught <bin> <case> <kind> [label]
asan_caught() {
	local bin=$1 c=$2 kind=$3 lab=${4:-$2/asan}
	("$bin" "$c" >"$W/$c.bug.out" 2>"$W/$c.bug.err"; exit $?) 2>/dev/null
	local rc=$?
	if [ $rc -ne 0 ] && grep -q "ERROR: AddressSanitizer: $kind" "$W/$c.bug.err"; then
		ok "$lab  caught: $(grep -m1 '^SUMMARY:' "$W/$c.bug.err" | sed 's/^SUMMARY: //')"
	else
		bad "$lab  expected ASan '$kind' (rc=$rc)"
	fi
}

# leaks_run <bin> <case> <tag> [extra leaks options...] → $W/<tag>.leaks.txt, sets LRC / LSUM
leaks_run() {
	local bin=$1 c=$2 tag=$3; shift 3
	MallocStackLogging=1 leaks "$@" --atExit -- "$bin" "$c" >"$W/$tag.leaks.txt" 2>&1
	LRC=$?
	LSUM=$(grep -m1 -E 'Process [0-9]+: [0-9]+ leaks? for' "$W/$tag.leaks.txt" | sed -E 's/^Process [0-9]+: //')
}

# exact multi-line compare: same <file> <expected text> → prints first differing line or nothing
first_diff() {
	diff <(printf '%s\n' "$2") "$1" | grep -m1 '^[<>]' || true
}

if [ "$V" != practice ]; then
	E_overflow="overflow: lines=gw-temp=215,node-07-hum=4551,node-12=90210"
	E_uaf="uaf: purged=3 left=2 values=40,12"
	E_leak="leak: recs=3 texts=ack,rx late,rx"
	E_cache="cache: devs=5 recs=6"
	E_uninit="uninit: retries=3"
	E_all=$(printf '%s\n' "$E_overflow" "$E_uaf" "$E_leak" "$E_cache" "$E_uninit")

	if cc $STRICT "$V.c" -o "$W/ev" 2>"$W/build.log"; then
		ok "build -Werror -O2 ($V.c)"
	else
		bad "build -Werror -O2"; cat "$W/build.log"
	fi
	cc $DBG "$V.c" -o "$W/ev_g" 2>>"$W/build.log" && cc $ASAN "$V.c" -o "$W/ev_asan" 2>>"$W/build.log" ||
		{ cat "$W/build.log"; echo "FAIL debug/ASan build"; echo "$PASS/$((TOTAL + 1)) passed"; exit 1; }

	# 2. self-test, plain malloc
	"$W/ev" all >"$W/all.out" 2>&1
	d=$(first_diff "$W/all.out" "$E_all")
	if [ -z "$d" ]; then ok "selftest all (plain malloc, 5 lines match)"; else bad "selftest all  first diff: $d"; fi

	# 3-4. spatial / temporal heap errors → ASan
	asan_clean "$W/ev_asan" overflow "$E_overflow"
	asan_clean "$W/ev_asan" uaf "$E_uaf"

	# 5. lost-pointer leak → leaks --atExit (exit 1 when it finds leaks)
	leaks_run "$W/ev_g" leak leak
	if [ $LRC -eq 0 ] && grep -q ' 0 leaks for 0 total leaked bytes' "$W/leak.leaks.txt" &&
		grep -qxF "$E_leak" "$W/leak.leaks.txt"; then
		ok "leak/leaks  ($LSUM)"
	else
		bad "leak/leaks  rc=$LRC ${LSUM:-no summary, see $W/leak.leaks.txt}"
	fi

	# 6. reachable-but-forgotten cache → leaks can't call it a leak; ask "who still owns memory at exit"
	#    --outputGraph 를 주면 leaks 는 리포트 대신 memgraph 를 쓴다 → 요약은 memgraph 에서 다시 읽는다
	leaks_run "$W/ev_g" cache cache --outputGraph="$W/cache.memgraph"
	LSUM=$(leaks -q "$W/cache.memgraph" 2>&1 | grep -m1 -E 'leaks? for' | sed -E 's/^Process [0-9]+: //')
	malloc_history "$W/cache.memgraph" -allBySize >"$W/cache.history.txt" 2>&1
	live=$(awk '/dev_intern/ { c += $1; b += $4 } END { printf "%d blocks / %d bytes", c, b }' "$W/cache.history.txt")
	if [ ! -s "$W/cache.memgraph" ]; then
		bad "cache/malloc_history  no memgraph written (see $W/cache.leaks.txt)"
	elif ! grep -q 'dev_intern' "$W/cache.history.txt"; then
		ok "cache/malloc_history  (nothing from dev_intern alive at exit; leaks: $LSUM)"
	else
		bad "cache/malloc_history  $live from dev_intern still alive at exit (leaks said: $LSUM)"
	fi

	# 7. uninitialized read → MallocScribble fills new blocks with 0xaa
	got=$(MallocScribble=1 "$W/ev_g" uninit 2>/dev/null)
	if [ "$got" = "$E_uninit" ]; then ok "uninit/MallocScribble  ($got)"; else bad "uninit/MallocScribble  output '$got' want '$E_uninit'"; fi

	# 8. Guard Malloc: every block on its own page, freed pages unmapped
	(DYLD_INSERT_LIBRARIES=$GM "$W/ev_g" all >"$W/gm.out" 2>"$W/gm.err"; exit $?) 2>/dev/null
	grc=$?
	d=$(first_diff "$W/gm.out" "$E_all")
	if ! grep -q '^GuardMalloc\[' "$W/gm.err"; then
		bad "all/GuardMalloc  libgmalloc not loaded (see $W/gm.err)"
	elif [ $grc -eq 0 ] && [ -z "$d" ]; then
		ok "all/GuardMalloc  (rc=0, 5 lines match)"
	else
		bad "all/GuardMalloc  rc=$grc ${d:+first diff: $d}"
	fi
else
	P_all=$(printf '%s\n' "grow: n=5 can=500k,eth=1G,i2c=400k,spi=8M,uart=115200" \
		"parse: ok=2 bad=3" "replace: n=2 mode=set")
	if cc $STRICT practice.c -o "$W/kv" 2>"$W/build.log" && cc $STRICT -DBUGGY practice.c -o "$W/kvb" 2>>"$W/build.log"; then
		ok "build -Werror -O2 (fixed and -DBUGGY)"
	else
		bad "build -Werror"; cat "$W/build.log"
	fi
	cc $DBG practice.c -o "$W/kv_g" 2>>"$W/build.log" && cc $DBG -DBUGGY practice.c -o "$W/kvb_g" 2>>"$W/build.log" &&
		cc $ASAN practice.c -o "$W/kv_asan" 2>>"$W/build.log" && cc $ASAN -DBUGGY practice.c -o "$W/kvb_asan" 2>>"$W/build.log" ||
		{ cat "$W/build.log"; echo "FAIL debug/ASan build"; echo "$PASS/$((TOTAL + 1)) passed"; exit 1; }

	"$W/kv" all >"$W/all.out" 2>&1
	d=$(first_diff "$W/all.out" "$P_all")
	if [ -z "$d" ]; then ok "selftest all (plain malloc, 3 lines match)"; else bad "selftest all  first diff: $d"; fi

	"$W/kv_asan" all >"$W/all.asan.out" 2>"$W/all.asan.err"
	d=$(first_diff "$W/all.asan.out" "$P_all")
	if ! grep -q "ERROR: AddressSanitizer" "$W/all.asan.err" && [ -z "$d" ]; then ok "fixed all/asan  (clean)"; else bad "fixed all/asan  $(grep -m1 '^SUMMARY:' "$W/all.asan.err") $d"; fi

	leaks_run "$W/kv_g" all fixed
	if [ $LRC -eq 0 ] && grep -q ' 0 leaks for 0 total leaked bytes' "$W/fixed.leaks.txt"; then ok "fixed all/leaks  ($LSUM)"; else bad "fixed all/leaks  rc=$LRC $LSUM"; fi

	(DYLD_INSERT_LIBRARIES=$GM "$W/kv_g" all >"$W/gm.out" 2>"$W/gm.err"; exit $?) 2>/dev/null
	grc=$?
	d=$(first_diff "$W/gm.out" "$P_all")
	if [ $grc -eq 0 ] && [ -z "$d" ] && grep -q '^GuardMalloc\[' "$W/gm.err"; then ok "fixed all/GuardMalloc  (rc=0)"; else bad "fixed all/GuardMalloc  rc=$grc $d"; fi

	# the planted bugs must be visible to the right tool
	asan_caught "$W/kvb_asan" grow heap-use-after-free "BUGGY grow/asan"
	leaks_run "$W/kvb_g" parse buggy_parse
	if [ $LRC -ne 0 ] && grep -q 'kvs_parse' "$W/buggy_parse.leaks.txt"; then
		ok "BUGGY parse/leaks  caught: $LSUM"
	else
		bad "BUGGY parse/leaks  expected leaks with a kvs_parse stack (rc=$LRC $LSUM)"
	fi
	asan_caught "$W/kvb_asan" replace "attempting double-free" "BUGGY replace/asan"
	("$W/kvb_g" replace >/dev/null 2>&1; exit $?) 2>/dev/null   # 쉘의 "Trace/BPT trap" 줄 숨김
	prc=$?
	if [ $prc -ge 128 ]; then ok "BUGGY replace/plain malloc  (libmalloc kills it: rc=$prc)"; else bad "BUGGY replace/plain malloc  rc=$prc (expected a signal)"; fi
fi

echo "$PASS/$TOTAL passed"
[ "$PASS" -eq "$TOTAL" ]
