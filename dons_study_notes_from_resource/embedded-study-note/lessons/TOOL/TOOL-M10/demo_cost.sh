#!/usr/bin/env bash
# TOOL-M10 demo — 도구 비용 실측 → cost.txt
#   bash demo_cost.sh       (맥 + VM elinux; 설정마다 3번 돌려 중앙값)
# loop_ms = 프로그램이 스스로 잰 루프 시간 (도구 시작/분석 시간 제외)
# real_s / maxRSS = /usr/bin/time (맥 -l, VM -f) — 도구 시작·종료 분석까지 포함한 프로세스 전체
set -u
cd "$(dirname "$0")" || exit 1
SRC=$(pwd)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-M10_work/cost
rm -rf "$W" && mkdir -p "$W" || exit 1
N=200000
cc -std=c11 -g -O1 demo_cost.c -o "$W/cost" && cc -std=c11 -g -O1 -fsanitize=address demo_cost.c -o "$W/cost_asan" || exit 1

med() { sort -n | awk '{ a[NR] = $1 } END { print a[int((NR + 1) / 2)] }'; }

# mac_run <label> <env...> -- <cmd...>
mac_run() {
	local label=$1; shift
	local envs=()
	while [ "$1" != -- ]; do envs+=("$1"); shift; done; shift
	: >"$W/ms" ; : >"$W/rs" ; : >"$W/rss"
	for i in 1 2 3; do
		env ${envs[@]+"${envs[@]}"} /usr/bin/time -l "$@" >"$W/o" 2>"$W/t"
		grep -o 'loop_ms=[0-9.]*' "$W/o" "$W/t" | head -1 | cut -d= -f2 >>"$W/ms"
		awk '/ real /{ print $1 }' "$W/t" >>"$W/rs"
		awk '/maximum resident set size/{ printf "%.1f\n", $1 / 1048576 }' "$W/t" >>"$W/rss"
	done
	printf '%s|%s|%s|%s\n' "$label" "$(med <"$W/ms")" "$(med <"$W/rs")" "$(med <"$W/rss")"
}

{
	mac_run "mac plain malloc"                      -- "$W/cost" $N
	mac_run "mac MallocStackLogging=lite"           MallocStackLogging=lite -- "$W/cost" $N
	mac_run "mac MallocStackLogging=full"           MallocStackLogging=full -- "$W/cost" $N
	mac_run "mac MallocScribble=1"                  MallocScribble=1 -- "$W/cost" $N
	mac_run "mac Guard Malloc (libgmalloc)"         DYLD_INSERT_LIBRARIES=/usr/lib/libgmalloc.dylib -- "$W/cost" $N
	mac_run "mac ASan"                              ASAN_OPTIONS=abort_on_error=0 -- "$W/cost_asan" $N
	mac_run "mac leaks --atExit (lite, +분석)"       -- leaks -q --atExit -- "$W/cost" $N
} >"$W/mac.rows" 2>/dev/null

limactl shell elinux -- bash -lc "rm -rf /tmp/TOOL-M10_work/vcost && mkdir -p /tmp/TOOL-M10_work/vcost && cp '$SRC'/demo_cost.c /tmp/TOOL-M10_work/vcost/ &&
  cd /tmp/TOOL-M10_work/vcost &&
  gcc -std=c11 -g -O1 demo_cost.c -o cost && clang -std=c11 -g -O1 -fsanitize=address demo_cost.c -o cost_asan &&
  clang -std=c11 -g -O1 -fsanitize=leak demo_cost.c -o cost_lsan && clang -std=c11 -g -O1 -fsanitize=memory demo_cost.c -o cost_msan &&
  med() { sort -n | awk '{ a[NR] = \$1 } END { print a[int((NR + 1) / 2)] }'; }
  vm_run() { label=\$1; shift; : > ms; : > rs; : > rss
    for i in 1 2 3; do /usr/bin/time -f 'TIME %e %M' \"\$@\" > o 2> t
      grep -ho 'loop_ms=[0-9.]*' o t | head -1 | cut -d= -f2 >> ms
      awk '/^TIME/{ print \$2 }' t >> rs; awk '/^TIME/{ printf \"%.1f\n\", \$3 / 1024 }' t >> rss; done
    printf '%s|%s|%s|%s\n' \"\$label\" \"\$(med < ms)\" \"\$(med < rs)\" \"\$(med < rss)\"; }
  vm_run 'vm plain malloc (gcc)' ./cost $N
  vm_run 'vm valgrind memcheck (기본)' valgrind -q ./cost $N
  vm_run 'vm valgrind --leak-check=full --track-origins=yes' valgrind -q --leak-check=full --track-origins=yes ./cost $N
  vm_run 'vm ASan (clang, LSan on)' ./cost_asan $N
  vm_run 'vm LSan only (clang -fsanitize=leak)' ./cost_lsan $N
  vm_run 'vm MSan (clang -fsanitize=memory)' ./cost_msan $N" >"$W/vm.rows" 2>/dev/null

{
	echo "# TOOL-M10 cost — demo_cost.c N=$N, median of 3 ($(date '+%Y-%m-%d'); Mac = Apple M-series macOS $(sw_vers -productVersion), VM = elinux aarch64 6 vCPU)"
	echo "# 기준(×1.0)은 같은 머신의 plain malloc. 다른 작업과 함께 도는 머신이라 ±30% 정도는 잡음."
	printf '%-52s %9s %7s %8s %9s %7s\n' config loop_ms slow real_s maxRSS_MB mem
	for f in mac vm; do
		awk -F'|' 'NR == 1 { b = $2; r = $4 } { printf "%-52s %9.1f %6.1fx %8.2f %9.1f %6.1fx\n", $1, $2, $2 / b, $3, $4, $4 / r }' "$W/$f.rows"
	done
} | tee cost.txt
