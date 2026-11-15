#!/bin/bash
# ARM-B04 demo: 주소 지정 모드의 즉시값 범위를 진짜 어셈블러로 확인한다.
#   한 줄씩 어셈블 → 성공하면 실제 디스어셈블(어셈블러가 고른 명령) + 32비트 워드, 실패하면 에러 메시지.
# 실행: bash demo_range.sh      (맥: clang -arch arm64, VM: clang --target=aarch64-linux-gnu 도 동일)
W=/tmp/ARM-B04_range
mkdir -p "$W"
try() {
    printf '%s\n' "    $1" > "$W/t.s"
    if clang -c -arch arm64 -x assembler "$W/t.s" -o "$W/t.o" 2> "$W/err.txt"; then
        dis=$(objdump -d --no-show-raw-insn "$W/t.o" | awk -F'\t' '/^ *0:/{print $2" "$3}')
        word=$(objdump -d "$W/t.o" | awk '/^ *0:/{print $2}')
        printf 'OK   %-30s -> %-28s 0x%s\n' "$1" "$dis" "$word"
    else
        msg=$(grep -m1 'error:' "$W/err.txt" | sed 's/.*error: //')
        printf 'ERR  %-30s -> %s\n' "$1" "$msg"
    fi
}
echo "--- 부호 없는 오프셋 (imm12 x 크기): LDR X 는 0..32760, 8의 배수"
try "ldr x0, [x1, #32760]"
try "ldr x0, [x1, #32768]"
try "ldr x0, [x1, #4]"
try "ldr x0, [x1, #-8]"
try "ldr w0, [x1, #16380]"
try "ldrb w0, [x1, #4095]"
try "ldrb w0, [x1, #4096]"
echo "--- 비스케일 (imm9 = -256..255 바이트): LDUR"
try "ldur x0, [x1, #255]"
try "ldur x0, [x1, #256]"
try "ldur x0, [x1, #-256]"
echo "--- pre/post-index (imm9)"
try "ldr x0, [x1, #255]!"
try "ldr x0, [x1, #256]!"
try "ldr x0, [x1], #-256"
echo "--- LDP/STP (imm7 x 8 = -512..504)"
try "ldp x0, x1, [x2, #504]"
try "ldp x0, x1, [x2, #512]"
try "ldp x0, x1, [x2, #4]"
try "stp x29, x30, [sp, #-16]!"
echo "--- 레지스터 오프셋 (shift 는 0 또는 log2(크기))"
try "ldr x0, [x1, x2, lsl #3]"
try "ldr x0, [x1, x2, lsl #2]"
try "ldr w0, [x1, w2, sxtw #2]"
try "ldr w0, [x1, w2]"
try "ldrsw x0, [x1, w2, uxtw #2]"
echo "--- writeback 과 같은 레지스터 (Arm ARM: CONSTRAINED UNPREDICTABLE) / 자동 LDUR 치환"
try "ldr x0, [x0], #8"
try "str x0, [x0], #8"
try "ldp x0, x1, [x0], #16"
try "ldp x1, x1, [x0]"
try "ldrsw x0, [x1, #-4]"
try "ldr x0, [x1, #3]"
