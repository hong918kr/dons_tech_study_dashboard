#!/bin/bash
# CPP-A07 target.sh — demo_target.cpp(= solution.cpp 프레임워크)를 Cortex-M4 로 컴파일해 RAM·심볼·asm 을 본다 (맥에서 실행)
#   bash target.sh        nm -S (정적 객체 크기), nm -u (외부 참조), 섹션 표, 크기
#   bash target.sh asm    run_one / Button 디스패치 썽크 asm (지시어 제거)
#   bash target.sh mfp    demo_mfp.cpp: 멤버 함수 포인터 vs 함수 포인터 vs 가상 함수 호출 asm
#   bash target.sh sizes  Cortex-M4 에서의 sizeof (Show<...> 에러 메시지로)
# arm-none-eabi 헤더는 CPP-A04 asm_targets.sh 와 같은 방식으로 VM 에서 한 번 복사해 쓴다.
set -e
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
: "${ARM_INC:=/tmp/arm-none-eabi-include}"
cd "$(dirname "$0")"
if [ ! -d "$ARM_INC/c++/14.2.1" ]; then
    echo "fetching arm-none-eabi headers from Lima VM 'elinux' → $ARM_INC" >&2
    mkdir -p "$ARM_INC"
    limactl shell elinux -- bash -lc 'cd /usr/lib/arm-none-eabi/include && tar -chf - .' | tar -xf - -C "$ARM_INC"
fi
OUT=${OUT:-$(mktemp -d)}
CXX="clang++ --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti
     -ffreestanding -nostdinc++ -isystem $ARM_INC/c++/14.2.1 -isystem $ARM_INC/c++/14.2.1/arm-none-eabi/thumb/v7e-m+fp/hard
     -isystem $ARM_INC"
strip_dir() { grep -v -E '^\s*(\.[a-z_]+|@ --|@ %bb|$)|^\s*\.(LCPI|Lfunc_end)' || true; }
case "${1:-nm}" in
nm)
    $CXX -c demo_target.cpp -o "$OUT/t.o"
    echo "== 정적 객체 크기 (llvm-nm -C -S --size-sort, 데이터만)"
    xcrun llvm-nm -C -S --size-sort "$OUT/t.o" | grep -E ' [bBdD] '
    echo "== 외부 참조 (llvm-nm -u)"; xcrun llvm-nm -u "$OUT/t.o" | sed 's/^/  /'; echo "  (끝)"
    echo "== 섹션 종류별 합계 (llvm-size -A)"
    xcrun llvm-size -A "$OUT/t.o" | awk '$1 ~ /^\.(text|rodata|data|bss|init_array|ARM.exidx)/ { k=$1; sub(/^\.text\..*/,".text",k); sub(/^\.ARM\.exidx\..*/,".ARM.exidx",k); sub(/^\.rodata\..*/,".rodata",k); t[k]+=$2 } END { for (k in t) printf "  %-12s %5d\n", k, t[k] }' | sort
    echo "  .init_array 섹션 수: $(xcrun llvm-objdump -h "$OUT/t.o" | grep -c init_array || true)" ;;
mfp)
    echo "== thumbv7em (Cortex-M4)"
    clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c++20 -O2 -fno-exceptions -fno-rtti -S demo_mfp.cpp -o - | strip_dir
    echo "== arm64 (host)"
    c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S demo_mfp.cpp -o - | grep -v -E '^\s*(\.|;|$)|^l_|^ltmp' ;;
sizes)
    $CXX -DAO_SHOW_SIZES -fsyntax-only demo_target.cpp 2>&1 | grep -o 'Show<[^>]*>' | head -1 || true ;;
asm)
    $CXX -S demo_target.cpp -o - | strip_dir | awk '/^_ZN9Framework7run_oneEv:/,/^\.Lfunc_end|^_Z.*:$/ {print} /^_ZN6ActiveI6ButtonLt4EE14dispatch_thunkER12ActiveObjectRK5Event:/,/bx\tlr|pop/ {print}' ;;
esac
