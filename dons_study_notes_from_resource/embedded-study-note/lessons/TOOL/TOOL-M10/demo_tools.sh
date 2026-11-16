#!/usr/bin/env bash
# TOOL-M10 demo — 맥 쪽 도구를 starter.c / practice.c(-DBUGGY) 에 직접 돌려 리포트를 캡처한다.
#   bash demo_tools.sh   → leaks_leak.txt leaks_modes.txt cache_tools.txt history_uaf.txt
#                          gmalloc.txt malloc_env.txt asan_mac.txt attach.txt plain_crash.txt  (이 디렉터리에)
# 빌드/실행은 /tmp/TOOL-M10_work/tools 에서. 긴 "Binary Images" 목록 등은 잘라낸다.
set -u
cd "$(dirname "$0")" || exit 1
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-M10_work/tools
rm -rf "$W" && mkdir -p "$W" || exit 1
L=$(pwd)
GM=/usr/lib/libgmalloc.dylib
cc -std=c11 -g -O0 starter.c -o "$W/ev_g" &&
	cc -std=c11 -g -O1 -fno-inline -fno-omit-frame-pointer -fsanitize=address starter.c -o "$W/ev_asan" &&
	cc -std=c11 -g -O0 -DBUGGY practice.c -o "$W/kvb_g" || exit 1
cd "$W" || exit 1
# 실행 한 번: 명령 줄 + 출력 + 종료 코드 (쉘의 "Segmentation fault" 줄은 숨김)
# 리다이렉션을 명령 안쪽에 둬야 쉘의 "Segmentation fault" 알림(바깥 stderr)이 출력에 섞이지 않는다
show() {
	echo "\$ $*"
	(eval "$* >\"$W/so\" 2>&1"; exit $?) 2>/dev/null
	local rc=$?
	cat "$W/so"; [ -s "$W/so" ] && [ -n "$(tail -c1 "$W/so")" ] && echo
	echo "[exit code $rc]"
}
nobin() { sed '/^Binary Images:/,$d'; }
gmq() { grep -v -E 'GuardMalloc\[.*\]: ( - |Allocations will|version )'; }   # 배너 3줄 숨김 (처음 한 번은 보여 줌)

# 1) leaks 기본 리포트 (lite 스택)
{ show "MallocStackLogging=1 leaks --atExit -- ./ev_g leak" | nobin | sed '/^Process:/,/^----$/d'; } >"$L/leaks_leak.txt"

# 2) 스택 기록 방식별 차이: 요약 줄 + 첫 ROOT LEAK 줄
{
	for e in "(env 없음)" "MallocStackLogging=0" "MallocStackLogging=1" "MallocStackLogging=lite" "MallocStackLogging=full"; do
		echo "== $e"
		envv=$e; [ "$e" = "(env 없음)" ] && envv=
		env $envv leaks --atExit -- ./ev_g leak 2>&1 |
			grep -E 'MallocStackLogging:|leaks? for|^STACK OF|ROOT LEAK' | sed -E 's/\(([0-9]+)\)/(PID)/; s/\.[0-9]+\.[0-9a-f]+\.[A-Za-z0-9]+\.index/.<pid>.index/' | head -5
	done
} >"$L/leaks_modes.txt"

# 3) 닿지만 잊힌 캐시: leaks 는 0, malloc_history / referenceTree 는 "누가 들고 있나" 를 보여 준다
{
	show "MallocStackLogging=1 leaks --atExit --outputGraph=cache.memgraph -- ./ev_g cache" | grep -v '^$'
	echo
	show "leaks -q cache.memgraph"
	echo
	echo "\$ malloc_history cache.memgraph -allBySize | grep dev_intern     # 종료 시점에 살아 있는 블록, 할당 스택별"
	malloc_history cache.memgraph -allBySize 2>&1 | grep dev_intern | sed 's/^\(.*bytes:\).*(ev_g) main /\1 … main /; s/0x[0-9a-f]* (ev_g) //g; s/ | 0x[0-9a-f]* (libsystem_malloc.dylib) .*//'
	echo
	echo "\$ leaks --referenceTree cache.memgraph | grep -A9 g_devs     # 루트(전역) 에서 내려가는 소유 사슬"
	leaks --referenceTree cache.memgraph 2>&1 | grep -A9 "'g_devs'"
} >"$L/cache_tools.txt"

# 4) full 모드 = 할당 + 해제 이력 → 주소 하나의 "누가 만들고 누가 풀었나" (ASan 의 두 스택에 해당)
{
	show "MallocStackLogging=full leaks --atExit --fullStackHistory --outputGraph=uaf.memgraph -- ./ev_g uaf" |
		grep -v -E '^$|stack logs' | sed -E 's/leaks\([0-9]+\)/leaks(PID)/'
	echo
	addr=$(malloc_history uaf.memgraph -allEvents 2>/dev/null | grep '^ALLOC' | grep 'rec_new' | sed -n 4p | sed -E 's/^ALLOC (0x[0-9a-f]+)-.*/\1/')
	echo "# 네 번째 rec_new 블록 = node-12 value 3 = purge 로 지워진 꼬리: $addr"
	echo "\$ malloc_history uaf.memgraph $addr | grep 'ev_g'"
	malloc_history uaf.memgraph "$addr" 2>&1 | grep '(ev_g)' | grep "$addr" |
		sed -E 's/0x[0-9a-f]+ \(dyld\) start \| //; s/0x[0-9a-f]+ \((ev_g|libsystem_malloc.dylib)\) //g; s/ \| _malloc_zone_malloc_instrumented_or_legacy.*//; s/ \| find_zone_and_free.*//'
} >"$L/history_uaf.txt"

# 5) Guard Malloc
{
	echo "\$ ls -l $GM"; ls -l "$GM" | awk '{ print $1, $5, $6, $7, $8, $9 }'
	echo
	show "DYLD_INSERT_LIBRARIES=$GM ./ev_g overflow"
	echo
	show "MALLOC_POINTER_SIZE=1 DYLD_INSERT_LIBRARIES=$GM ./ev_g overflow" | gmq
	echo
	show "MALLOC_PROTECT_BEFORE=1 DYLD_INSERT_LIBRARIES=$GM ./ev_g overflow" | gmq
	echo
	show "DYLD_INSERT_LIBRARIES=$GM ./ev_g uaf" | gmq
	echo
	show "DYLD_INSERT_LIBRARIES=$GM ./ev_g uninit" | gmq
	show "MALLOC_FILL_SPACE=1 DYLD_INSERT_LIBRARIES=$GM ./ev_g uninit" | gmq
	echo
	show "DYLD_INSERT_LIBRARIES=$GM ./ev_g leak" | gmq
	echo
	echo "\$ lldb -b -s demo_gm.lldb -- ./ev_g overflow"
	lldb -b -s "$L/demo_gm.lldb" -- ./ev_g overflow 2>&1 | grep -v -E '^\(lldb\) (target create|settings set|command source)|^Current executable|^Executing commands' | gmq
} >"$L/gmalloc.txt"

# 6) 일반 malloc 의 디버그 환경 변수
{
	echo "\$ MallocDebugReport=stderr MallocHelp=1 ./ev_g uninit 2>&1 | head -20"
	MallocDebugReport=stderr MallocHelp=1 ./ev_g uninit 2>&1 | sed -E 's/^ev_g\([0-9]+,0x[0-9a-f]+\)/ev_g(PID,TID)/' | head -20
	echo
	show "./ev_g uninit"
	show "MallocScribble=1 ./ev_g uninit"
	show "MallocPreScribble=1 ./ev_g uninit"
	show "MallocZeroOnFree=0 ./ev_g uninit"
	echo
	show "./ev_g uaf"
	show "MallocScribble=1 ./ev_g uaf"
	show "MallocCheckZeroOnFreeCorruption=1 ./ev_g uaf"
	show "MallocCheckHeapStart=1 MallocCheckHeapEach=1 MallocCheckHeapAbort=1 ./ev_g uaf"
	echo
	show "MallocCheckHeapStart=1 MallocCheckHeapEach=1 MallocCheckHeapAbort=1 ./ev_g overflow"
	show "MallocGuardEdges=1 ./ev_g overflow"
} >"$L/malloc_env.txt"

# 7) ASan (맥): 리포트 핵심 줄, 0xbe 채움, 누수 미지원
{
	export ASAN_OPTIONS=abort_on_error=0
	show "./ev_asan overflow" | sed '/^Shadow bytes around/,$d' | sed -E 's/^==[0-9]+==/==PID==/'
	echo "…"
	echo
	show "./ev_asan uaf" | sed '/^SUMMARY/,$d' | sed -E 's/^==[0-9]+==/==PID==/'
	echo "…"
	echo
	show "./ev_asan uninit"
	echo "# 2105376127 = 0x7D7D7D7F = (2*0xBEBEBEBE + 3) mod 2^32  ← ASan 이 새 블록을 0xbe 로 채운다"
	show "ASAN_OPTIONS=abort_on_error=0:max_malloc_fill_size=0 ./ev_asan uninit"
	echo
	show "./ev_asan leak"
	show "ASAN_OPTIONS=detect_leaks=1 ./ev_asan leak" | sed -E 's/^==[0-9]+==/==PID==/'
	show "cc -fsanitize=memory -g '$L/starter.c' -o ev_msan" | sed "s|$L/||"
	show "cc -fsanitize=leak -g '$L/starter.c' -o ev_lsan" | sed "s|$L/||"
} >"$L/asan_mac.txt" 2>&1

# 7b) 실행 중인 프로세스에 붙이기: 스택은 "시작할 때부터" 기록해야 있다 + 보수적 스캔의 미탐
cc -std=c11 -g -O0 "$L/demo_attach.c" -o attach || exit 1
{
	for m in inline scrub; do
		for e in "" "MallocStackLogging=1"; do
			echo "\$ ${e:+$e }./attach $m &   →   leaks <pid>"
			(env $e ./attach $m >addr.txt 2>&1 & pid=$!; sleep 1; leaks "$pid" 2>&1 |
				grep -E 'leaks? for|ROOT LEAK' | sed -E 's/^Process [0-9]+:/Process PID:/'; wait "$pid")
		done
	done
	echo
	echo "# inline 이 0 인 이유: 블록을 가리키는 루트를 추적"
	echo "\$ ./attach inline &   →   leaks --trace=<찍힌 주소> <pid>"
	(./attach inline >addr.txt 2>&1 & pid=$!; sleep 1; leaks --trace="$(head -1 addr.txt)" "$pid" 2>&1 | grep -v '^[[:space:]]*$' | tail -4; wait "$pid")
} >"$L/attach.txt"

# 8) 도구 없이 일반 malloc 에서 practice -DBUGGY: 크래시 위치 ≠ 버그 위치
{
	printf 'run\nbt 5\n' >crash.lldb
	for c in grow replace; do
		echo "\$ lldb -b -o run -o 'bt 5' -- ./kvb_g $c"
		lldb -b -s crash.lldb -- ./kvb_g $c 2>&1 | grep -E 'stop reason|frame #|^->|^(grow|parse|replace):' | head -9
		echo
	done
	show "DYLD_INSERT_LIBRARIES=$GM ./kvb_g replace" | gmq
} >"$L/plain_crash.txt"

cd "$L" && wc -l leaks_leak.txt leaks_modes.txt cache_tools.txt history_uaf.txt gmalloc.txt malloc_env.txt asan_mac.txt attach.txt plain_crash.txt
