#!/usr/bin/env bash
# TOOL-M03 demo (Mac, Mach-O / ld64): 등록 항목의 생존 조건, 오타, ASan 레드존, 세그먼트 선택, 여러 .c 파일.
#   bash demo_macho.sh > demo_macho.txt 2>&1
set -u
L=$(cd "$(dirname "$0")" && pwd)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-M03_work/macho; rm -rf "$W"; mkdir -p "$W"; cd "$W" || exit 1
strip_tmp() { sed -E 's#/(private/)?var/folders/[^ ]*/T/##; s#'"$L"'/##g'; }

echo "##### 1. 누가 살아남나 (demo_survive.c)"
for o in "-O0" "-O2" "-O2 -Wl,-dead_strip"; do
	printf '%-22s ' "cc $o:"
	# shellcheck disable=SC2086
	cc -std=c11 -Wall -Wextra -Werror $o "$L/demo_survive.c" -o s && ./s
done
echo "== -DPLAIN (static, 속성 없음) + -Werror"
cc -std=c11 -Wall -Wextra -Werror -O2 -DPLAIN "$L/demo_survive.c" -o s 2>&1 | strip_tmp | head -4
echo "== otool -l: 진짜 섹션과 오타 섹션 (링커가 크기 0 섹션을 만들어 준다)"
cc -std=c11 -O2 "$L/demo_survive.c" -o s
otool -l s | grep -A4 -E 'sectname __shel' | grep -E 'sectname|segname|addr|size'

echo; echo "##### 2. nm -m: 항목은 non-external, used 는 [no dead strip]"
cc -std=c11 -O2 -c "$L/solution.c" -o sol.o
nm -m sol.o | grep -E '__shellcmds|section\$' | head -9

echo; echo "##### 3. ASan 레드존 (demo_asan.c)"
for x in "-fsanitize=address" "-fsanitize=address -DFIX" ""; do
	echo "== cc -O1 $x"
	# shellcheck disable=SC2086
	cc -O1 $x "$L/demo_asan.c" -o a && ./a
done
echo "== solution.c 에서 SHELL_NO_ASAN 을 비우고 ASan 빌드"
sed 's/^#  define SHELL_NO_ASAN __attribute__((no_sanitize("address")))/#  define SHELL_NO_ASAN/' "$L/solution.c" > sol_noasanfix.c
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined sol_noasanfix.c -o sna
./sna 2>&1 | grep -E 'ERROR|READ of|located|SUMMARY' | sed -E 's/0x[0-9a-f]+/0x…/g; s/==[0-9]+==/==PID==/'
echo "rc=${PIPESTATUS[0]}"

echo; echo "##### 4. 어느 세그먼트에? (포인터가 든 const 표)"
for seg in __TEXT __DATA_CONST __DATA; do
	sed "s/__DATA/$seg/g" "$L/demo_survive.c" > seg.c
	echo "== $seg,__shellcmds"
	if cc -std=c11 -O2 seg.c -o seg 2>err.txt; then ./seg; otool -l seg | grep -A2 'sectname __shellcmds' | grep segname
	else strip_tmp < err.txt | head -3; fi
done

echo; echo "##### 5. 여러 .c 파일 (multi/): 링크 순서 · 중복 · 정적 라이브러리 · dead_strip"
for f in main cmd_sys cmd_pwr cmd_led; do cc -std=c11 -Wall -Wextra -Werror -O2 -I"$L/multi" -c "$L/multi/$f.c" -o $f.o; done
run() { echo "== cc $*"; # shellcheck disable=SC2068
	cc $@ -o m 2>&1 | strip_tmp; ./m | sed -E 's/table 0x[0-9a-f]+\.\.0x[0-9a-f]+ = //'; echo "rc=${PIPESTATUS[0]}"; }
run main.o cmd_sys.o cmd_pwr.o cmd_led.o
run main.o cmd_led.o cmd_pwr.o cmd_sys.o
ar rcs libcmds.a cmd_led.o 2>&1
run main.o cmd_sys.o -L. -lcmds
run main.o cmd_sys.o -Wl,-force_load,libcmds.a
run main.o cmd_sys.o cmd_led.o -Wl,-dead_strip
for f in cmd_sys cmd_led; do cc -std=c11 -O2 -DNO_USED -I"$L/multi" -c "$L/multi/$f.c" -o nu_$f.o 2>&1 | strip_tmp | head -2; done
run main.o nu_cmd_sys.o nu_cmd_led.o
echo "== -DSHELL_GLOBAL (항목을 외부 링키지로): 같은 이름 두 번 = 링크 에러"
for f in cmd_sys cmd_pwr; do cc -std=c11 -Wall -Wextra -Werror -O2 -DSHELL_GLOBAL -I"$L/multi" -c "$L/multi/$f.c" -o g_$f.o; done
cc main.o g_cmd_sys.o g_cmd_pwr.o -o m 2>&1 | sed -E 's#/private/tmp/TOOL-M03_work/macho/##'
