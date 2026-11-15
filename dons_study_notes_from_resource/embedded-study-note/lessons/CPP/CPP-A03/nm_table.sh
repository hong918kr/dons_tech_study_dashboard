#!/bin/bash
# CPP-A03 nm_table.sh — "구문 → 미정의 런타임 심볼" 표를 실제로 만든다. 레슨 폴더에서 실행.
#   bash nm_table.sh          맥 clang, --target=thumbv7em-none-eabi -mcpu=cortex-m4 -ffreestanding (+ arm-none-eabi 헤더)
#   bash nm_table.sh host     맥 호스트 arm64 Mach-O (이름 앞 '_' 하나 더)
#   bash nm_table.sh gcc      Lima VM elinux 안에서 (arm-none-eabi-g++ 14.2):
#       tar -cf - demo_constructs.cpp nm_table.sh | limactl shell elinux -- bash -c 'mkdir -p /tmp/CPP-A03 && tar -xf - -C /tmp/CPP-A03 && bash /tmp/CPP-A03/nm_table.sh gcc'
#   bash nm_table.sh obj      link_vm.sh 3c 단계용 clang LEGACY 오브젝트 → /tmp/cppa03/app_legacy_clang.o
# 맥 clang 은 thumb 코드는 만들지만 bare-metal C++ 헤더가 없다 → VM 의 arm-none-eabi 헤더를 한 번 복사해 쓴다 (CPP-A04 asm_targets.sh 와 같은 방식).
cd "$(dirname "$0")"
MODE=${1:-clang}
OUT=${TMPDIR:-/tmp}/cppa03; mkdir -p "$OUT"
if [ "$MODE" != gcc ]; then
    : "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
    : "${ARM_INC:=/tmp/arm-none-eabi-include}"
    if [ "$MODE" != host ] && [ ! -d "$ARM_INC/c++/14.2.1" ]; then
        echo "fetching arm-none-eabi headers from Lima VM 'elinux' → $ARM_INC" >&2
        mkdir -p "$ARM_INC"
        limactl shell elinux -- bash -lc 'cd /usr/lib/arm-none-eabi/include && tar -chf - .' | tar -xf - -C "$ARM_INC"
    fi
fi
cxx() {
    case $MODE in
    gcc)  arm-none-eabi-g++ -mcpu=cortex-m4 -mthumb -std=c++20 -O2 -ffreestanding "$@" ;;
    host) c++ -std=c++20 -O2 "$@" ;;
    *)    clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -std=c++20 -O2 -ffreestanding -nostdinc++ \
              -isystem "$ARM_INC/c++/14.2.1" -isystem "$ARM_INC/c++/14.2.1/arm-none-eabi/thumb/v7e-m/nofp" -isystem "$ARM_INC" "$@" ;;
    esac
}
nmu() { if [ "$MODE" = gcc ]; then arm-none-eabi-nm "$1" | awk '$1=="U"||$1=="w"{print ($1=="w"?"w:":"")$2}'; else xcrun llvm-nm -u "$1" | awk '{print $NF}'; fi; }
row() {
    local r=$1; shift
    printf '%-3s %-34s ' "$r" "$*"
    if ! cxx -DROW="$r" "$@" -c demo_constructs.cpp -o "$OUT/r.o" 2>"$OUT/err.txt"; then
        echo "컴파일 에러: $(grep -m1 'error:' "$OUT/err.txt" | sed 's/.*error: //')"; return
    fi
    nmu "$OUT/r.o" | tr '\n' ' '; echo
}
if [ "$MODE" = obj ]; then
    cxx -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -DLEGACY -c fw_app.cpp -o "$OUT/app_legacy_clang.o" && xcrun llvm-nm -u "$OUT/app_legacy_clang.o"
    exit $?
fi
B="-fno-exceptions -fno-rtti"
row 1 $B; row 1 $B -fno-threadsafe-statics; row 1 -fexceptions -fno-rtti
row 2 $B
row 3 $B; row 3 $B -fno-use-cxa-atexit; [ "$MODE" = gcc ] || row 3 $B -fno-c++-static-destructors
row 4 $B; row 12 $B
row 5 $B; row 5 $B -fno-sized-deallocation
row 6 $B
row 7 -fno-exceptions -frtti; row 7 $B
row 8 -fexceptions -fno-rtti; row 8 $B
row 9 $B; row 10 $B; row 10 $B -fno-builtin; row 11 $B
