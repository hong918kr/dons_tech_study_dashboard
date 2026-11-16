#!/usr/bin/env bash
# TOOL-M03 demo (Lima VM elinux, aarch64 Ubuntu: gcc 15.2, clang 21.1.8, GNU ld 2.46, ld.lld 21.1.8).
#   bash demo_elf.sh > demo_elf.txt 2>&1          (Mac 에서 실행 → VM 안에서 돈다)
set -u
L=$(cd "$(dirname "$0")" && pwd)
limactl shell elinux -- bash -s <<VM
set -u
W=/tmp/TOOL-M03_work/elf; rm -rf \$W; mkdir -p \$W; cd \$W
cp -r "$L"/multi "$L"/demo_dotname.c "$L"/solution.c "$L"/practice.c "$L"/demo_asan.c "$L"/demo_initcall.c "$L"/initcall_host.ld .
nice() { sed -E 's/table 0x[0-9a-f]+\.\.0x[0-9a-f]+ = //; s#/usr/bin/aarch64-linux-gnu-ld.bfd#ld.bfd#; s#/tmp/cc[A-Za-z0-9]+\.o#cc.o#'; }

echo "##### 1. 같은 solution.c / practice.c 를 ELF 로 (이식성 블록의 __ELF__ 분기)"
for cc in gcc clang; do
	echo "== \$cc -O2"; \$cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o s && ./s | tail -2
	\$cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o p && ./p | tail -2
	echo "== \$cc ASan+UBSan"; \$cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sa && ./sa | tail -1
done
nm s | grep -E '__start_shellcmds|__stop_shellcmds'
readelf -SW s | grep -E ' shellcmds'

echo "== 점으로 시작하는 섹션 이름 (.shellcmds) 에 __start_shellcmds 를 기대하면 (demo_dotname.c)"
gcc -std=c11 -O2 demo_dotname.c -o dn 2>&1 | head -2 | nice

echo; echo "##### 2. 한 TU 안의 순서: gcc 와 clang 이 다르다 (multi/cmd_sys.c: uptime 다음 reboot 정의)"
cd multi
for cc in gcc clang; do
	for f in main cmd_sys cmd_pwr cmd_led; do \$cc -std=c11 -Wall -Wextra -Werror -O2 -c \$f.c -o \$f.o; done
	echo "== \$cc"; \$cc main.o cmd_sys.o cmd_pwr.o cmd_led.o -o a && ./a | nice; echo "rc=\${PIPESTATUS[0]}"
done
echo "== readelf -SW cmd_led.o (used 만 / used+retain)"
gcc -std=c11 -O2 -c cmd_led.c -o u.o && readelf -SW u.o | grep -E ' shellcmds'
gcc -std=c11 -O2 -DSHELL_RETAIN -c cmd_led.c -o r.o && readelf -SW r.o | grep -E ' shellcmds'

echo; echo "##### 3. --gc-sections 행렬 (gcc -O2, 링크 순서 main sys pwr led)"
for ld in bfd lld; do
	for z in "" "-Wl,-z,start-stop-gc" "-Wl,-z,nostart-stop-gc"; do
		for att in used retain; do
			X=""; [ \$att = retain ] && X="-DSHELL_RETAIN"
			for f in main cmd_sys cmd_pwr cmd_led; do gcc -std=c11 -O2 \$X -c \$f.c -o \$f.o; done
			printf '%-4s gc %-24s %-7s: ' \$ld "\$z" \$att
			rm -f a; gcc -fuse-ld=\$ld -Wl,--gc-sections \$z main.o cmd_sys.o cmd_pwr.o cmd_led.o -o a 2>&1 | head -1 | nice
			[ -x a ] && ./a | head -1 | nice
		done
	done
done
echo "== bfd -z start-stop-gc: 어떤 입력 섹션이 남나 (순서 led pwr sys)"
for f in main cmd_sys cmd_pwr cmd_led; do gcc -std=c11 -O2 -c \$f.c -o \$f.o; done
gcc -Wl,--gc-sections -Wl,-z,start-stop-gc -Wl,--print-gc-sections main.o cmd_led.o cmd_pwr.o cmd_sys.o -o a 2>&1 | grep shellcmds | nice
./a | head -1 | nice

echo; echo "##### 4. 섹션 이름 오타 (main 만 SECT_ID=shellcmd)"
gcc -std=c11 -O2 -DSECT_ID=shellcmd -c main.c -o main_typo.o
gcc main_typo.o cmd_sys.o cmd_pwr.o cmd_led.o -o a 2>&1 | head -2 | nice
gcc -fuse-ld=lld main_typo.o cmd_sys.o cmd_pwr.o cmd_led.o -o a 2>&1 | head -1
echo "== 경계 심볼을 weak 로 선언하면? (오타가 조용히 빈 표가 된다 — Mach-O 와 같은 증상)"
gcc -std=c11 -O2 -DSECT_ID=shellcmd -DSHELL_WEAK -c main.c -o main_tw.o
rm -f a; gcc main_tw.o cmd_sys.o cmd_pwr.o cmd_led.o -o a && ./a; echo "rc=\$?"
echo "== -DSHELL_GLOBAL: 같은 이름 두 번 = 링크 에러 (bfd)"
for f in cmd_sys cmd_pwr; do gcc -std=c11 -O2 -DSHELL_GLOBAL -c \$f.c -o g_\$f.o; done
gcc main.o g_cmd_sys.o g_cmd_pwr.o -o a 2>&1 | head -2 | nice
echo "== 정적 라이브러리 안의 등록 파일 (B09 와 같은 함정)"
ar rcs libcmds.a cmd_led.o
gcc main.o cmd_sys.o -L. -lcmds -o a && ./a | nice
gcc main.o cmd_sys.o -L. -Wl,--whole-archive -lcmds -Wl,--no-whole-archive -o a && ./a | nice
cd ..

echo; echo "##### 5. ASan 레드존: ELF 는? (demo_asan.c)"
for cc in gcc clang; do
	echo "== \$cc -O1 -fsanitize=address"; \$cc -O1 -fsanitize=address demo_asan.c -o da && ./da
done

echo; echo "##### 6. initcall 레벨: SORT 가 있는 스크립트 조각 vs 없는 것"
sed 's/KEEP(\*(SORT(.initcall.\*)))/KEEP(*(.initcall.*))/' initcall_host.ld > nosort.ld
grep -n 'KEEP' nosort.ld
for cc in gcc clang; do
	\$cc -std=c11 -O2 -c demo_initcall.c -o ic_\$cc.o
	echo "== \$cc: .o 안의 섹션 순서"; readelf -SW ic_\$cc.o | grep -E ' \.initcall\.' | sed -E 's/^ *\[ *[0-9]+\] +([^ ]+).*/    \1/' | tr '\n' ' '; echo
	for ld in initcall_host.ld nosort.ld; do
		echo "== \$cc + \$ld"; gcc ic_\$cc.o -Wl,-T,\$ld -o ic && ./ic | tr '\n' ' '; echo
	done
done
readelf -SW ic | grep -E ' \.initcalls'
echo "== 기본 링커 스크립트도 같은 방법을 쓴다 (ld --verbose 발췌)"
ld --verbose | grep -E 'SORT_BY_INIT_PRIORITY' | head -2
VM
