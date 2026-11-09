#!/bin/bash
# CPP-M10 asm_target.sh — static_vector 를 Cortex-M4 로 빌드해 비교한다 (맥에서 실행)
#   bash asm_target.sh asm        demo_target.cpp 의 clang -S 출력 (지시어 제거)
#   bash asm_target.sh sections   demo_target.cpp 의 전역 초기화: -O2 / -O0 의 섹션과 심볼
#   bash asm_target.sh gcc        Lima VM elinux 의 arm-none-eabi-g++ 로 같은 코드 -S
#   bash asm_target.sh link       Lima VM 에서 링크, size / nm
# ARM_INC: arm-none-eabi 헤더 복사본. 없으면 VM 에서 /tmp/arm-none-eabi-include 로 가져온다.
set -e
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
: "${ARM_INC:=/tmp/arm-none-eabi-include}"
: "${OUT:=/tmp/CPP-M10}"
cd "$(dirname "$0")"; mkdir -p "$OUT"
if [ ! -d "$ARM_INC/c++/14.2.1" ]; then
    echo "fetching arm-none-eabi headers from Lima VM 'elinux' → $ARM_INC" >&2
    mkdir -p "$ARM_INC"
    limactl shell elinux -- bash -lc 'cd /usr/lib/arm-none-eabi/include && tar -chf - .' | tar -xf - -C "$ARM_INC"
fi
cxx() {
    clang++ --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -std=c++20 -Wall -Wextra -fno-exceptions -fno-rtti \
        -ffreestanding -nostdinc++ -isystem "$ARM_INC/c++/14.2.1" \
        -isystem "$ARM_INC/c++/14.2.1/arm-none-eabi/thumb/v7e-m+fp/hard" -isystem "$ARM_INC" "$@"
}
strip_dir() { grep -v -E '^\s*(\.[a-z_]+|@ --|@ %bb|$)|^\s*\.(LCPI|Lfunc_end)' || true; }
case "${1:-asm}" in
asm) cxx -O2 -DNDEBUG demo_target.cpp -S -o - | strip_dir ;;
sections)
    for o in -O2 -O0; do
        echo "== $o"
        cxx $o -DNDEBUG -c demo_target.cpp -o "$OUT/t$o.o"
        xcrun llvm-objdump -h "$OUT/t$o.o" | grep -E 'init_array|\.bss|\.data'
        xcrun llvm-nm -C "$OUT/t$o.o" | grep -E 'g_int|g_nt|__cxa_atexit|__aeabi_atexit|_GLOBAL__' || true
    done ;;
gcc)
    limactl shell elinux -- bash -c "mkdir -p /tmp/CPP-M10 && cp $PWD/demo_static_vector.hpp $PWD/demo_target.cpp /tmp/CPP-M10/ && cd /tmp/CPP-M10 && arm-none-eabi-g++ -std=c++20 -O2 -DNDEBUG -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -S demo_target.cpp -o -" | strip_dir ;;
link)
    limactl shell elinux -- bash -s "$PWD" <<'VM'
set -e
mkdir -p /tmp/CPP-M10 && cp "$1"/demo_static_vector.hpp "$1"/demo_link.cpp /tmp/CPP-M10/ && cd /tmp/CPP-M10
F="-std=c++20 -O2 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -nostartfiles --specs=nosys.specs"
b() { name=$1; shift; arm-none-eabi-g++ $F "$@" demo_link.cpp -o $name.elf 2>/dev/null; printf '%-34s' "$name"; arm-none-eabi-size $name.elf | tail -1; }
b kinds1_ndebug   -DKINDS=1 -DNDEBUG
b kinds3_ndebug   -DKINDS=3 -DNDEBUG
b kinds1_assert   -DKINDS=1
b kinds1_trap_ndebug -DKINDS=1 -DNDEBUG -DPOLICY=TrapOnFull
echo "--- nm kinds3_ndebug (work)"; arm-none-eabi-nm -C --size-sort kinds3_ndebug.elf | grep -E ' work' || true
echo "--- heap / assert symbols"
for e in kinds1_ndebug kinds1_assert kinds1_trap_ndebug; do
  printf '%-22s' $e; arm-none-eabi-nm -C $e.elf | grep -E ' (malloc|_malloc_r|operator new.*|__assert_func|fiprintf|_vfiprintf_r|abort)$' | awk '{print $3}' | tr '\n' ' '; echo
done
VM
    ;;
esac
