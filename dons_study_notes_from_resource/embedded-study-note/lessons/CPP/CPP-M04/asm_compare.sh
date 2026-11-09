#!/bin/bash
# CPP-M04 asm_compare.sh — Pin 템플릿(demo_asm_pin.cpp) vs 손으로 쓴 C(demo_asm_c.c), Cortex-M4 오브젝트 비교
#   bash asm_compare.sh [출력디렉터리]            맥 clang, 기본 -O2
#   OPT=-O0 bash asm_compare.sh                  최적화 수준 변경
#   EXTRA=-DFORCE_INLINE OPT=-O0 bash asm_compare.sh
#   TOOL=gcc bash asm_compare.sh                 Lima VM elinux 의 arm-none-eabi-g++ 로 (VM 안에서 빌드)
set -e
cd "$(dirname "$0")"
: "${OPT:=-O2}"
: "${TOOL:=clang}"
if [ "$TOOL" = gcc ]; then
    limactl shell elinux -- bash -c "set -e; rm -rf /tmp/CPP-M04-asm; mkdir -p /tmp/CPP-M04-asm; cp '$PWD'/demo_asm_pin.cpp '$PWD'/demo_asm_c.c '$PWD'/asm_compare.sh /tmp/CPP-M04-asm/; cd /tmp/CPP-M04-asm; TOOL=gcc-local OPT='$OPT' EXTRA='$EXTRA' SHOW='$SHOW' bash asm_compare.sh out"
    exit $?
fi
OUT="${1:-_asm}"; mkdir -p "$OUT"
if [ "$TOOL" = gcc-local ]; then
    C="-mcpu=cortex-m4 -mthumb $OPT -c"
    arm-none-eabi-g++ $C $EXTRA -std=c++20 -fno-exceptions -fno-rtti demo_asm_pin.cpp -o "$OUT/pin.o"
    arm-none-eabi-gcc $C -std=c11 demo_asm_c.c -o "$OUT/c.o"
    OBJDUMP=arm-none-eabi-objdump; SIZE=arm-none-eabi-size
else
    : "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
    T="--target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb $OPT -c"
    clang++ $T $EXTRA -std=c++20 -fno-exceptions -fno-rtti demo_asm_pin.cpp -o "$OUT/pin.o"
    clang   $T -std=c11 demo_asm_c.c -o "$OUT/c.o"
    OBJDUMP="xcrun llvm-objdump"; SIZE="xcrun llvm-size"
fi
body() {  # <obj> <함수이름>: 주소·정렬 nop·리터럴 .word 를 뺀 명령만
    $OBJDUMP -d --no-show-raw-insn "$1" | awk -v f="<$2>:" '
        index($0, f) { on=1; next } /^$/ { on=0 } /^[0-9a-f]+ </ { on=0 }
        on && /^ +[0-9a-f]+:/ { $1=""; print }' | grep -vE '^ *nop|\.word' | sed -E 's/[[:space:]]*[@;].*$//'
}
printf '%-14s %8s %8s  %s\n' "op" "Pin(C++)" "C" "same text?"
for f in set clear clear15 write read toggle toggle_bsrr pulse two_pins bus_const bus_var; do
    a=$(body "$OUT/pin.o" cpp_$f); b=$(body "$OUT/c.o" c_$f)
    na=$(printf '%s\n' "$a" | grep -c .); nb=$(printf '%s\n' "$b" | grep -c .)
    norm() { sed -E 's/\[pc, #[-0-9a-fx]+\]/[pc, #LIT]/; s/<[^>]*>//g'; }   # PC 상대 리터럴 오프셋만 무시 (즉값은 비교)
    if [ "$(printf '%s\n' "$a" | norm)" = "$(printf '%s\n' "$b" | norm)" ]; then s=yes; else s=no; fi
    printf '%-14s %8s %8s  %s\n' "$f" "$na" "$nb" "$s"
done
for f in toggle_crit; do printf '%-14s %8s\n' "$f" "$(body "$OUT/pin.o" cpp_$f | grep -c .)"; done
for f in rt_set tbl_set call_rt_set; do printf '%-14s %8s %8s\n' "$f" "-" "$(body "$OUT/c.o" $f | grep -c .)"; done
echo "===== size (.o)"; $SIZE "$OUT/pin.o" "$OUT/c.o"
if [ -n "$SHOW" ]; then for f in $SHOW; do echo "--- $f"; body "$OUT/pin.o" "$f" 2>/dev/null; body "$OUT/c.o" "$f" 2>/dev/null; done; fi
