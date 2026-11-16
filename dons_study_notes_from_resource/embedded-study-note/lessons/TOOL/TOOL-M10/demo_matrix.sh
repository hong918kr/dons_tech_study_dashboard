#!/usr/bin/env bash
# TOOL-M10 demo — 탐지 매트릭스 (버그 × 도구) 를 실제 실행으로 만든다 → matrix.txt
#   bash demo_matrix.sh      (맥에서 실행; VM 열은 limactl 로 elinux 에서 돈다)
# 칸 표기:
#   ERR   = 도구가 오류 리포트를 냈다 (ASan/valgrind/MSan 오류, Guard Malloc 크래시)
#   LEAK  = 누수 리포트 (leaks "N leaks", LSan "detected memory leaks", valgrind "definitely lost")
#   REACH = "아직 닿는(still reachable)" 메모리로만 보고 (valgrind 요약 줄)
#   VAL   = 리포트는 없지만 결과 값이 정답과 달라졌다 (증상만)
#   -     = 아무 신호 없음
set -u
cd "$(dirname "$0")" || exit 1
SRC=$(pwd)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
export ASAN_OPTIONS=abort_on_error=0
W=/tmp/TOOL-M10_work/matrix
rm -rf "$W" && mkdir -p "$W" || exit 1
CASES="overflow uaf leak cache uninit"

cc -std=c11 -g -O0 solution.c -o "$W/ok" && cc -std=c11 -g -O0 starter.c -o "$W/ev_g" &&
	cc -std=c11 -g -O1 -fno-inline -fno-omit-frame-pointer -fsanitize=address starter.c -o "$W/ev_asan" || exit 1
for c in $CASES; do "$W/ok" "$c" >"$W/want.$c"; done

# classify <case> <rc> <out> <err> → one cell
classify() {
	local c=$1 rc=$2 out=$3 err=$4
	if grep -qE 'ERROR: AddressSanitizer|WARNING: MemorySanitizer|Invalid (read|write)|uninitialised|GuardMalloc.*(trashed|attempted)' "$err" || [ "$rc" -ge 128 ]; then
		echo ERR
	elif grep -qE 'Process [0-9]+: [1-9][0-9]* leaks? for|ERROR: LeakSanitizer|definitely lost: [1-9]' "$err"; then
		echo LEAK
	elif grep -qE 'still reachable: [1-9]' "$err"; then
		echo REACH
	elif ! cmp -s "$out" "$W/want.$c"; then
		echo VAL
	else
		echo -
	fi
}

mac_tool() {   # mac_tool <name> <case> → cell
	local t=$1 c=$2 o=$W/$1.$2.out e=$W/$1.$2.err
	case $t in
	plain)    ("$W/ev_g" "$c" >"$o" 2>"$e"; exit $?) 2>/dev/null ;;
	leaks)    (leaks -q --atExit -- "$W/ev_g" "$c" >"$e" 2>&1; exit $?) 2>/dev/null
	          grep -E '^(overflow|uaf|leak|cache|uninit):' "$e" >"$o" ;;
	scribble) (MallocScribble=1 "$W/ev_g" "$c" >"$o" 2>"$e"; exit $?) 2>/dev/null ;;
	gmalloc)  (DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib "$W/ev_g" "$c" >"$o" 2>"$e"; exit $?) 2>/dev/null ;;
	asan)     ("$W/ev_asan" "$c" >"$o" 2>"$e"; exit $?) 2>/dev/null ;;
	esac
	local rc=$?
	[ "$t" = leaks ] && rc=0   # leaks exit 1 = "leaks found" (not a crash); the grep decides
	classify "$c" "$rc" "$o" "$e"
}

{
	echo "# TOOL-M10 detection matrix — starter.c, one process per case ($(date '+%Y-%m-%d'), macOS $(sw_vers -productVersion))"
	printf '%-9s' case; for t in plain leaks scribble gmalloc asan; do printf ' %-9s' "$t"; done
	printf ' | %-9s %-9s %-9s %-9s %-9s\n' vg_memchk vg_full lsan asan+lsan msan
	# VM side: copy sources, build with gcc/clang -g -O0, run each tool per case, classify there too
	VM="limactl shell elinux -- bash -lc"
	$VM "rm -rf /tmp/TOOL-M10_work/vmx && mkdir -p /tmp/TOOL-M10_work/vmx && cp '$SRC'/starter.c '$SRC'/solution.c /tmp/TOOL-M10_work/vmx/" || exit 1
	vmres=$($VM "cd /tmp/TOOL-M10_work/vmx && gcc -std=c11 -g -O0 solution.c -o ok && gcc -std=c11 -g -O0 starter.c -o g &&
		clang -std=c11 -g -O1 -fsanitize=leak starter.c -o l && clang -std=c11 -g -O1 -fno-omit-frame-pointer -fsanitize=address starter.c -o a &&
		clang -std=c11 -g -O1 -fsanitize=memory -fsanitize-memory-track-origins starter.c -o m &&
		for c in $CASES; do
		  ./ok \$c > want.\$c
		  row=\$c
		  for t in vg vgf l a m; do
		    case \$t in
		      vg)  valgrind ./g \$c > o 2> e ;;
		      vgf) valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes ./g \$c > o 2> e ;;
		      l)   ./l \$c > o 2> e ;;
		      a)   ./a \$c > o 2> e ;;
		      m)   ./m \$c > o 2> e ;;
		    esac
		    rc=\$?
		    if grep -qE 'ERROR: AddressSanitizer|WARNING: MemorySanitizer|Invalid (read|write)|uninitialised' e; then cell=ERR
		    elif grep -qE 'ERROR: LeakSanitizer|definitely lost: [1-9]' e; then cell=LEAK
		    elif grep -qE 'still reachable: [1-9]' e; then cell=REACH
		    elif ! cmp -s o want.\$c; then cell=VAL
		    else cell=-; fi
		    row=\"\$row \$cell\"
		  done
		  echo \"\$row\"
		done")
	for c in $CASES; do
		printf '%-9s' "$c"
		for t in plain leaks scribble gmalloc asan; do printf ' %-9s' "$(mac_tool "$t" "$c")"; done
		set -- $(echo "$vmres" | awk -v c="$c" '$1 == c { $1 = ""; print }')
		printf ' | %-9s %-9s %-9s %-9s %-9s\n' "$1" "$2" "$3" "$4" "$5"
	done
	echo
	echo "Mac: plain = 일반 malloc, leaks = leaks --atExit, scribble = MallocScribble=1, gmalloc = libgmalloc, asan = clang -fsanitize=address"
	echo "VM : vg_memchk = valgrind (기본: --leak-check=summary), vg_full = --leak-check=full --show-leak-kinds=all --track-origins=yes,"
	echo "     lsan = clang -fsanitize=leak, asan+lsan = clang -fsanitize=address (LSan 기본 on), msan = clang -fsanitize=memory"
} | tee matrix.txt
