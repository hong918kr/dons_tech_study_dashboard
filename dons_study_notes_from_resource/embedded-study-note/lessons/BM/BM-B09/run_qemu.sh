#!/bin/bash
# ENV: qemu
# BM-B09 — Lima VM `elinux` 안에서 실행 (qemu-system-arm 10.2.1, arm-none-eabi-gcc 14.2.1 + newlib 4.6 nano/rdimon).
# Mac 홈은 VM 에서 읽기 전용 → 소스를 /tmp/BM-B09_work 로 복사해 빌드한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/BM/BM-B09/run_qemu.sh MODE'
# MODE
#   solution | starter : UART0 출력 캡처 → CR 제거 → expected_solution.txt 와 diff → PASS/FAIL (종료 코드)
#   practice           : 세미호스팅 켬/끔 두 번 실행 (UART 입력 = practice_input.txt) → expected_practice*.txt
#   semihost           : demo_semihost.c (rdimon) — printf 가 세미호스팅으로 호스트 stdout 에
#   nodebug            : 같은 이미지를 -semihosting 없이 → BKPT 0xAB → HardFault 덤프 (UART0)
#   bugs               : 고장 주입 빌드 (BUG_SBRK / BUG_FSTAT / BUG_NO_TXEN / BUG_NO_INIT_HANDLES)
#   size               : printf 경로별 코드 크기 표 (demo_size.c)
#   link               : syscall 이 없을 때의 링크 에러 / nosys.specs 의 "조용한 printf"
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/BM-B09_work
mkdir -p "$W"
cp "$SRC"/*.c "$SRC"/*.ld "$SRC"/*.txt "$W"/
cd "$W" || exit 1
CFLAGS="-mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror"
LDF="-nostartfiles -T mps2_an385.ld -ffunction-sections -fdata-sections -Wl,--gc-sections"
QEMU="timeout 10 qemu-system-arm -M mps2-an385 -nographic -monitor none -no-reboot"
SEMI="-semihosting-config enable=on,target=native"

fw() {   # fw <src> <elf> [extra flags...]  (기본 nano.specs + 우리 syscall)
    local src=$1 elf=$2; shift 2
    arm-none-eabi-gcc $CFLAGS --specs=nano.specs $LDF "$@" "$src" -o "$elf" || exit 1
}
# check <capture> <expected> : CR 개수 == LF 개수 (CRLF 변환 확인) + 내용 비교
check() {
    local out=$1 exp=$2 cr lf
    cr=$(tr -cd '\r' < "$out" | wc -c); lf=$(tr -cd '\n' < "$out" | wc -c)
    echo "--- captured $out ($(wc -c < "$out") bytes, CR=$cr LF=$lf) ---"
    tr -d '\r' < "$out"; echo
    if [ "$cr" = "$lf" ] && tr -d '\r' < "$out" | diff -u "$exp" - > "$out.diff"; then
        echo "CHECK PASS: $out == $exp"; return 0
    fi
    echo "CHECK FAIL: $out != $exp (CR=$cr LF=$lf)"; head -20 "$out.diff"; return 1
}

case "${1:-solution}" in
solution|starter)
    fw "$1.c" "$1.elf"
    arm-none-eabi-size "$1.elf"
    rm -f "$1.uart"
    $QEMU -serial file:"$1.uart" -kernel "$1.elf"; echo "qemu exit=$?"
    check "$1.uart" expected_solution.txt; exit $? ;;

practice)
    fw practice.c practice.elf
    arm-none-eabi-size practice.elf
    rm -f p_semi.uart p_nodebug.uart
    echo "=== (1) semihosting ON: stderr -> SYS_WRITE0 -> QEMU stderr"
    $QEMU $SEMI -chardev file,id=u0,path=p_semi.uart,input-path=practice_input.txt -serial chardev:u0 \
          -kernel practice.elf 2> p_semi.stderr; echo "qemu exit=$?"
    check p_semi.uart expected_practice.txt; r1=$?
    echo "--- QEMU stderr (semihosting console) ---"; cat p_semi.stderr
    diff -u expected_practice_semi.txt p_semi.stderr && echo "CHECK PASS: semihosting stderr" ; r2=$?
    echo "=== (2) semihosting OFF (no debugger): BKPT 0xAB -> HardFault -> skipped, stderr -> UART"
    $QEMU -chardev file,id=u0,path=p_nodebug.uart,input-path=practice_input.txt -serial chardev:u0 \
          -kernel practice.elf; echo "qemu exit=$?"
    check p_nodebug.uart expected_practice_nodebug.txt; r3=$?
    [ $r1 -eq 0 ] && [ $r2 -eq 0 ] && [ $r3 -eq 0 ] && { echo "practice: ALL PASS"; exit 0; }
    echo "practice: FAIL"; exit 1 ;;

semihost)
    arm-none-eabi-gcc $CFLAGS --specs=nano.specs --specs=rdimon.specs $LDF demo_semihost.c -o semi.elf || exit 1
    arm-none-eabi-size semi.elf
    $QEMU $SEMI -serial file:semi.uart -kernel semi.elf > semi.stdout 2> semi.stderr; echo "qemu exit=$?"
    echo "--- host stdout ---"; cat semi.stdout
    echo "--- host stderr ---"; cat semi.stderr
    echo "--- UART0 ($(wc -c < semi.uart) bytes) ---" ;;

nodebug)
    arm-none-eabi-gcc $CFLAGS --specs=nano.specs --specs=rdimon.specs $LDF demo_semihost.c -o semi.elf || exit 1
    $QEMU -serial file:nodebug.uart -kernel semi.elf > nodebug.stdout 2>&1; echo "qemu exit=$?"
    echo "--- host stdout+stderr ($(wc -c < nodebug.stdout) bytes) ---"; cat nodebug.stdout
    echo "--- UART0 (HardFault handler) ---"; tr -d '\r' < nodebug.uart
    pc=$(grep -o 'PC=0x[0-9A-F]*' nodebug.uart | cut -c4-)
    echo "--- addr2line $pc ---"; arm-none-eabi-addr2line -f -i -e semi.elf "$pc" | sed 's|.*/libgloss/|libgloss/|'
    echo "--- objdump around PC ---"
    arm-none-eabi-objdump -d semi.elf | grep -B3 -A1 "^ *$(printf '%x' "$pc"):" ;;

bugs)
    for b in BUG_SBRK BUG_FSTAT; do
        echo "=== solution.c -D$b"
        fw solution.c "$b.elf" -D$b; rm -f "$b.uart"
        $QEMU -serial file:"$b.uart" -kernel "$b.elf"; echo "qemu exit=$?"
        tr -d '\r' < "$b.uart" | diff expected_solution.txt - ; echo "(diff vs expected above)"
    done
    echo "=== solution.c -DBUG_NO_TXEN  (QEMU trace: 장치가 본 레지스터 쓰기)"
    fw solution.c BUG_NO_TXEN.elf -DBUG_NO_TXEN; rm -f BUG_NO_TXEN.uart
    $QEMU -serial file:BUG_NO_TXEN.uart -d trace:cmsdk_apb_uart_write,trace:cmsdk_apb_uart_tx -D notxen.log \
          -kernel BUG_NO_TXEN.elf; echo "qemu exit=$?"
    echo "UART0 captured $(wc -c < BUG_NO_TXEN.uart) bytes; trace:"; cat notxen.log
    echo "=== demo_semihost.c -DBUG_NO_INIT_HANDLES"
    arm-none-eabi-gcc $CFLAGS --specs=nano.specs --specs=rdimon.specs $LDF -DBUG_NO_INIT_HANDLES \
        demo_semihost.c -o noinit.elf || exit 1
    $QEMU $SEMI -serial file:noinit.uart -kernel noinit.elf > noinit.stdout 2> noinit.stderr; echo "qemu exit=$?"
    echo "host stdout $(wc -c < noinit.stdout) bytes, stderr $(wc -c < noinit.stderr) bytes" ;;

size)
    row() {  # row <label> <flags...>
        local label=$1; shift
        arm-none-eabi-gcc $CFLAGS $LDF "$@" demo_size.c -o sz.elf || exit 1
        arm-none-eabi-size sz.elf | awk -v l="$label" 'NR==2 {printf "%-44s %7d %6d %6d\n", l, $1, $2, $3}'
    }
    printf "%-44s %7s %6s %6s\n" "variant" "text" "data" "bss"
    row "raw uart_puts (no printf), nano"         --specs=nano.specs -DRAW
    row "nano   printf  + UART _write"            --specs=nano.specs -DPF=printf
    row "nano   iprintf + UART _write"            --specs=nano.specs -DPF=iprintf
    row "nano   printf %f, no -u _printf_float"   --specs=nano.specs -DUSE_FLOAT
    row "nano   printf %f + -u _printf_float"     --specs=nano.specs -DUSE_FLOAT -u _printf_float
    row "full   printf  + UART _write"            -DPF=printf
    row "full   iprintf + UART _write"            -DPF=iprintf
    row "full   printf %f"                        -DUSE_FLOAT
    row "nano   printf  + rdimon (semihosting)"   --specs=nano.specs --specs=rdimon.specs -DSEMIHOST
    row "full   printf  + rdimon (semihosting)"   --specs=rdimon.specs -DSEMIHOST
    echo "--- what %f prints (nano without / with -u _printf_float, full) ---"
    for v in "--specs=nano.specs" "--specs=nano.specs -u _printf_float" ""; do
        arm-none-eabi-gcc $CFLAGS $LDF $v -DUSE_FLOAT demo_size.c -o f.elf || exit 1
        rm -f f.uart; $QEMU -serial file:f.uart -kernel f.elf
        printf '%-38s -> ' "[${v:-full newlib}]"; tr -d '\r' < f.uart | tail -1
    done ;;
link)
    echo "=== nano.specs, syscall 하나도 없음 → 링크 에러"
    arm-none-eabi-gcc $CFLAGS --specs=nano.specs $LDF -DNO_SYSCALLS demo_size.c -o nosc.elf 2>&1 \
        | grep -o "undefined reference to \`[_a-z]*'" | sort | uniq -c
    echo "=== nano.specs + nosys.specs → 링크는 되지만 (경고) _write 가 늘 -1"
    arm-none-eabi-gcc $CFLAGS --specs=nano.specs --specs=nosys.specs $LDF -DNO_SYSCALLS demo_size.c -o nosys.elf 2>&1 \
        | grep -o "warning: _[a-z]* is not implemented and will always fail" | sort -u
    rm -f nosys.uart; $QEMU -serial file:nosys.uart -kernel nosys.elf; echo "qemu exit=$?, UART0 captured $(wc -c < nosys.uart) bytes" ;;
*) echo "unknown mode $1"; exit 2 ;;
esac
