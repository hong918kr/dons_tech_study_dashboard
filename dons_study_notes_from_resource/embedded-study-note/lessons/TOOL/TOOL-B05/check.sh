#!/usr/bin/env bash
# TOOL-B05 링크 에러 체커
#   bash check.sh starter    → 4개 시나리오의 깨진 빌드 (FAIL 이 나와야 정상, exit 1)
#   bash check.sh solution   → 고친 빌드: 링크 성공 + 실행 출력 + nm 근거 (exit 0)
#   bash check.sh practice   → 9. 추가 연습 답 (exit 0)
# Part 1 = 이 맥 (Apple clang + ld64, Mach-O).  Part 2 = Lima VM elinux (gcc + GNU ld, arm-none-eabi-gcc, ELF).
# VM 이 없거나 꺼져 있으면 Part 2 는 SKIP (점수에 안 넣음).
set -u
cd "$(dirname "$0")" || exit 2
L=$(pwd)
V=${1:-}
case "$V" in starter|solution|practice) ;; *) echo "usage: check.sh <starter|solution|practice>"; exit 2 ;; esac

export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 회피
CFLAGS="-std=c11 -Wall -Wextra -Werror -O2"
CXXFLAGS="-std=c++17 -Wall -Wextra -Werror -O2"
W=/tmp/TOOL-B05_work/$V
rm -rf "$W"
mkdir -p "$W"

P=0
T=0
pass() { T=$((T + 1)); P=$((P + 1)); echo "PASS  $1"; }
fail() {
	T=$((T + 1)); echo "FAIL  $1"
	if [ -n "${2:-}" ] && [ -s "$2" ]; then head -5 "$2" | sed 's/^/        | /'; fi
}
# chk "이름" [로그파일] -- 명령...   : 명령이 성공하면 PASS
chk() {
	local name=$1 log=$2
	shift 3
	if "$@" >/dev/null 2>&1; then pass "$name"; else fail "$name" "$log"; fi
}

# stage <scenario> <variant> <dir> : common/ 위에 variant/ 를 덮어 작업 디렉터리를 만든다
stage() {
	mkdir -p "$3"
	cp "$L/$1/common/"* "$3/"
	cp "$L/$1/$2/"* "$3/"
}
# build <dir> [추가 플래그] : SRCS 의 파일을 하나씩 컴파일 → 링크. compile.log / link.log 남김
build() {
	local d=$1 x=${2:-}
	(
		cd "$d" || exit 9
		: >compile.log
		objs="" drv=cc
		for f in $(cat SRCS); do
			o=${f%.*}.o
			case $f in
			*.cpp) drv=c++; c++ $CXXFLAGS $x -c "$f" -o "$o" ;;
			*) cc $CFLAGS $x -c "$f" -o "$o" ;;
			esac >>compile.log 2>&1 || { cp compile.log link.log; exit 3; }
			objs="$objs $o"
		done
		$drv $objs -o app >link.log 2>&1
	)
}
# out_is <dir> <기대 출력> : ./app 의 stdout 이 정확히 같은가
out_is() {
	[ -x "$1/app" ] || return 1
	"$1/app" >"$1/out.txt" 2>&1
	[ "$(cat "$1/out.txt")" = "$2" ]
}
# defined_once <dir> <sym> : 전역(대문자) 정의가 .o 전체에서 정확히 1번, 지역(소문자) 정의 0번
defined_once() {
	local g l
	g=$(cd "$1" && nm *.o 2>/dev/null | grep -cE " [BDSC] _$2\$")
	l=$(cd "$1" && nm *.o 2>/dev/null | grep -cE " [bdsc] _$2\$")
	[ "$g" -eq 1 ] && [ "$l" -eq 0 ]
}
# strong_in <exe> <sym> : 최종 Mach-O 에서 그 이름이 weak 가 아닌 external 정의인가
strong_in() { [ -f "$1" ] && nm -m "$1" | grep -Eq "\) external _$2\$"; }

vm_up() {
	command -v limactl >/dev/null 2>&1 &&
		[ "$(limactl list --format '{{.Status}}' elinux 2>/dev/null)" = Running ]
}

# VM 쪽 스크립트: 인자 = 변형 이름, 레슨 경로(맥 홈은 VM 에 읽기 전용으로 마운트됨)
VM_SCRIPT='
set -u
V=$1; L=$2; X=/tmp/TOOL-B05_work/vm_$V
rm -rf "$X"; mkdir -p "$X"
ok()  { echo "PASS  $1"; }
no()  { echo "FAIL  $1"; [ -s "${2:-/nonexistent}" ] && head -5 "$2" | sed "s/^/        | /"; }
stage() { mkdir -p "$3"; cp "$L/$1/common/"* "$3/"; cp "$L/$1/$2/"* "$3/"; }
CF="-std=c11 -Wall -Wextra -Werror -O2"

# s1~s3: 네이티브 gcc/g++ (aarch64 Linux, GNU ld.bfd, -fno-common 기본)
for s in s1_undefined s2_multidef; do
  d=$X/$s; stage $s $V $d; cd $d
  ( for f in $(cat SRCS); do gcc $CF -c $f || exit 1; done; gcc $(sed "s/\.c/.o/g" SRCS) -o app ) >log 2>&1
  case $s in
  s1_undefined) exp="selftest=0
crc8=0xF4
adc_raw=2048 adc_mv=1650
UART OK" ;;
  s2_multidef) exp="E[5] sensor timeout
E[5] crc mismatch
ticks=5 errs=2 level=2" ;;
  esac
  if [ -x app ] && [ "$(./app)" = "$exp" ]; then ok "[VM gcc/GNU ld] $s links + output"; else no "[VM gcc/GNU ld] $s links + output" log; fi
done
d=$X/s3; stage s3_mangling $V $d; cd $d
( gcc $CF -c ring.c && g++ -std=c++17 -Wall -Wextra -Werror -O2 -c app.cpp && g++ ring.o app.o -o app ) >log 2>&1
if [ -x app ] && [ "$(./app)" = "sum=10 dropped=2" ]; then ok "[VM g++/GNU ld] s3_mangling links + output"; else no "[VM g++/GNU ld] s3_mangling links + output" log; fi

# s4: arm-none-eabi-gcc (Cortex-M4, alias 로 만든 weak 기본 핸들러)
d=$X/s4; stage s4_weak $V $d; cd $d
SRC=$(sed "s/test_main.c//" SRCS)
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -Os $CF -ffreestanding -nostdlib -Wl,-e,Reset_Handler $SRC -o fw.elf >log 2>&1
if [ -f fw.elf ]; then ok "[VM arm-none-eabi] s4_weak links (weak 기본값 덕분에 늘 성공)"; else no "[VM arm-none-eabi] s4_weak links" log; fi
arm-none-eabi-nm fw.elf >nm.txt 2>&1
dh=$(awk "\$3==\"Default_Handler\"{print \$1}" nm.txt)
for h in USART1_IRQHandler TIM2_IRQHandler; do
  line=$(awk -v h=$h "\$3==h" nm.txt)
  if echo "$line" | grep -q " T " && [ "${line%% *}" != "$dh" ]; then ok "[VM arm-none-eabi-nm] $h = T, Default_Handler($dh) 와 다른 주소"
  else no "[VM arm-none-eabi-nm] $h 이 아직 weak alias: ${line:-없음}"; fi
done
'

echo "== TOOL-B05 check: $V   (work dir $W)"

if [ "$V" != practice ]; then
	echo "-- Part 1: 이 맥 (Apple clang 21 + ld64, Mach-O) --"
	# ---------------- s1: undefined reference 4종 ----------------
	d=$W/s1
	stage s1_undefined "$V" "$d"
	build "$d"
	chk "s1 links (undefined symbol 0개)" "$d/link.log" -- test -x "$d/app"
	chk "s1 output" "$d/out.txt" -- out_is "$d" "selftest=0
crc8=0xF4
adc_raw=2048 adc_mv=1650
UART OK"
	for s in sensor_selftest crc8 uart_putc adc_read_raw; do
		chk "s1 nm: app 에 'T _$s'" "" -- sh -c "nm '$d/app' 2>/dev/null | grep -q ' T _$s\$'"
	done
	chk "s1 nm: adc.o 의 adc_read_raw 가 전역(T, 소문자 t 아님)" "" -- sh -c "nm '$d/adc.o' | grep -q ' T _adc_read_raw\$'"

	# ---------------- s2: 헤더에 정의한 변수 ----------------
	d=$W/s2
	stage s2_multidef "$V" "$d"
	build "$d"
	chk "s2 links (맥 기본: Apple clang 은 -fcommon)" "$d/link.log" -- test -x "$d/app"
	d2=$W/s2_nocommon
	stage s2_multidef "$V" "$d2"
	build "$d2" -fno-common
	chk "s2 links with -fno-common (GCC 10+/Clang 11+ 기본과 같은 규칙)" "$d2/link.log" -- test -x "$d2/app"
	chk "s2 output (세 파일이 같은 변수를 봄)" "$d2/out.txt" -- out_is "$d2" "E[5] sensor timeout
E[5] crc mismatch
ticks=5 errs=2 level=2"
	for s in g_log_level g_ticks g_err_count; do
		chk "s2 nm: _$s 정의가 .o 전체에서 딱 1개 (static 사본 0개)" "" -- defined_once "$d2" "$s"
	done

	# ---------------- s3: C/C++ 이름 맹글링 ----------------
	d=$W/s3
	stage s3_mangling "$V" "$d"
	build "$d"
	chk "s3 links (C++ main + C ring.c)" "$d/link.log" -- test -x "$d/app"
	chk "s3 output" "$d/out.txt" -- out_is "$d" "sum=10 dropped=2"
	chk "s3 nm: app.o 가 C 이름 '_ring_push' 를 찾음 (U)" "" -- sh -c "nm '$d/app.o' | grep -q ' U _ring_push\$'"
	chk "s3 nm: app.o 에 맹글링된 ring 심볼(__Z*ring*) 0개" "" -- sh -c "! nm '$d/app.o' | grep -q '__Z.*ring'"

	# ---------------- s4: weak 기본 핸들러 ----------------
	d=$W/s4
	stage s4_weak "$V" "$d"
	build "$d"
	chk "s4 links (weak 기본값 덕분에 버그가 있어도 성공한다)" "$d/link.log" -- test -x "$d/app"
	chk "s4 output (기본 핸들러로 샌 인터럽트 0개)" "$d/out.txt" -- out_is "$d" "uart_rx=1 tim2=2 default_hits=0 last_unhandled=-1"
	for h in USART1_IRQHandler TIM2_IRQHandler; do
		chk "s4 nm -m: _$h 가 'weak external' 이 아닌 'external'" "" -- strong_in "$d/app" "$h"
	done
else
	echo "-- Part 1: 9. 추가 연습 (이 맥) --"
	# 연습 1: C++ 파일의 IRQ 핸들러
	d=$W/p1
	stage s4_weak solution "$d"
	rm -f "$d/timer.c"
	cp "$L/practice/p1_cpp_irq/"* "$d/"
	build "$d"
	chk "p1 C++ 핸들러 빌드 링크" "$d/link.log" -- test -x "$d/app"
	chk "p1 output (tim2=2, default_hits=0)" "$d/out.txt" -- out_is "$d" "uart_rx=1 tim2=2 default_hits=0 last_unhandled=-1"
	chk "p1 nm -m: _TIM2_IRQHandler strong" "" -- strong_in "$d/app" TIM2_IRQHandler
	chk "p1 nm: timer.o 에 __Z*TIM2_IRQHandler* 없음" "" -- sh -c "! nm '$d/timer.o' | grep -q '__Z.*TIM2_IRQHandler'"

	# 연습 2: require_handlers 가드 — s4 starter/solution 빌드에 돌려 본다
	for v in starter solution; do
		stage s4_weak $v "$W/p2_$v"
		build "$W/p2_$v"
	done
	# shellcheck disable=SC1091
	. "$L/practice/p2_lint/require_handlers.sh"
	o=$(require_handlers "$W/p2_starter/app" USART1_IRQHandler TIM2_IRQHandler NMI_Handler 2>&1); rc=$?
	exp="UNBOUND USART1_IRQHandler
UNBOUND TIM2_IRQHandler
UNBOUND NMI_Handler"
	if [ $rc -eq 1 ] && [ "$o" = "$exp" ]; then pass "p2 require_handlers: s4 starter(Mach-O) → 3개 UNBOUND, rc 1"
	else fail "p2 require_handlers: s4 starter → got rc=$rc '$o'"; fi
	o=$(require_handlers "$W/p2_solution/app" USART1_IRQHandler TIM2_IRQHandler 2>&1); rc=$?
	if [ $rc -eq 0 ] && [ -z "$o" ]; then pass "p2 require_handlers: s4 solution(Mach-O) → 출력 없음, rc 0"
	else fail "p2 require_handlers: s4 solution → got rc=$rc '$o'"; fi

	# 연습 3: 링크 줄 예측표
	d=$W/p3
	mkdir -p "$d"
	cp "$L/practice/p3_predict/"*.c "$d/"
	(
		cd "$d" || exit 1
		for f in main main2 a_name b_name; do cc $CFLAGS -c $f.c; done
		ar rcs liba.a a_name.o && ar rcs libb.a b_name.o
	) >"$d/build.log" 2>&1
	actual() { # id → "link run"
		local args
		case $1 in
		c1) args="main.o liba.a libb.a" ;;
		c2) args="main.o libb.a liba.a" ;;
		c3) args="main2.o liba.a libb.a" ;;
		c4) args="main2.o libb.a liba.a" ;;
		c5) args="main.o a_name.o b_name.o" ;;
		c6) args="main.o -Wl,-undefined,dynamic_lookup" ;;
		esac
		(
			cd "$d" || exit 1
			rm -f "t_$1"
			if cc $args -o "t_$1" >"link_$1.log" 2>&1; then
				out=$("./t_$1" 2>/dev/null); rc=$?
				if [ $rc -ge 128 ]; then echo "ok crash"; else echo "ok $out"; fi
			else
				echo "fail -"
			fi
		)
	}
	grep -vE '^[[:space:]]*(#|$)' "$L/practice/p3_predict/predict.txt" | while read -r id pred; do
		got=$(actual "$id")
		if [ "$got" = "$pred" ]; then echo "PASS  p3 $id 예측 '$pred'"; else echo "FAIL  p3 $id 예측 '$pred' / 실제 '$got'"; fi
	done >"$W/p3.res"
	cat "$W/p3.res"
	T=$((T + $(grep -c . "$W/p3.res")))
	P=$((P + $(grep -c '^PASS' "$W/p3.res")))

	# 연습 4: 컴파일 단계 가드 플래그
	F=$(grep -v '^#' "$L/practice/p4_guard/guard.flags" | tr '\n' ' ' | sed 's/ *$//')
	d=$W/p4
	stage s4_weak starter "$d"
	chk "p4 가드 플래그 [$F] 로 starter uart.c(오타) 컴파일이 실패" "" -- sh -c "! cc $CFLAGS $F -c '$d/uart.c' -o '$d/u.o'"
	d=$W/p4s
	stage s4_weak solution "$d"
	build "$d" "$F"
	chk "p4 같은 플래그로 solution 전체가 경고 0개로 빌드" "$d/link.log" -- test -x "$d/app"
fi

echo "-- Part 2: Lima VM elinux (GNU 툴체인, ELF) --"
if vm_up; then
	if [ "$V" != practice ]; then
		limactl shell elinux -- bash -s -- "$V" "$L" <<<"$VM_SCRIPT" >"$W/vm.res" 2>&1
		grep -E '^(PASS|FAIL|        \|)' "$W/vm.res"
		T=$((T + $(grep -cE '^(PASS|FAIL)' "$W/vm.res")))
		P=$((P + $(grep -c '^PASS' "$W/vm.res")))
	else
		# 연습 2 를 ELF 에도: VM 에서 starter/solution 펌웨어를 링크해 stdout 으로 받아온다
		for v in starter solution; do
			limactl shell elinux -- bash -s -- "$v" "$L" <<<"$VM_SCRIPT" >/dev/null 2>&1
			limactl shell elinux -- cat "/tmp/TOOL-B05_work/vm_$v/s4/fw.elf" >"$W/fw_$v.elf" 2>/dev/null
		done
		o=$(require_handlers "$W/fw_starter.elf" USART1_IRQHandler TIM2_IRQHandler 2>&1); rc=$?
		if [ $rc -eq 1 ] && [ "$o" = "UNBOUND USART1_IRQHandler
UNBOUND TIM2_IRQHandler" ]; then pass "p2 require_handlers: starter fw.elf(ELF) → 2개 UNBOUND"
		else fail "p2 require_handlers: starter fw.elf → got rc=$rc '$o'"; fi
		o=$(require_handlers "$W/fw_solution.elf" USART1_IRQHandler TIM2_IRQHandler 2>&1); rc=$?
		if [ $rc -eq 0 ] && [ -z "$o" ]; then pass "p2 require_handlers: solution fw.elf(ELF) → rc 0"
		else fail "p2 require_handlers: solution fw.elf → got rc=$rc '$o'"; fi
	fi
else
	echo "SKIP  VM elinux 가 꺼져 있음 → Part 2 생략 (점수에 포함 안 함)"
fi

echo "$P/$T passed"
[ "$P" -eq "$T" ] && [ "$T" -gt 0 ]
