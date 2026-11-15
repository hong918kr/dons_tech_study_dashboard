#!/bin/bash
# CPP-A06 target_check.sh — demo_target.cpp(= solution.cpp 코드) 를 Cortex-M4 로 컴파일해 "런타임 비용 0" 을 확인 (맥에서 실행)
#   bash target_check.sh        전부: asm, size, ppm, o0, gcc. 검사 실패 시 exit 1
#   bash target_check.sh asm|size|ppm|o0|gcc
# 맥 clang 은 thumb 코드를 만들지만 bare-metal C++ 헤더가 없어서 arm-none-eabi 헤더(VM elinux)를 한 번 복사해 쓴다 (CPP-A04 와 같음).
set -e
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
: "${ARM_INC:=/tmp/arm-none-eabi-include}"
cd "$(dirname "$0")"
if [ ! -d "$ARM_INC/c++/14.2.1" ]; then
    echo "fetching arm-none-eabi headers from Lima VM 'elinux' → $ARM_INC" >&2
    mkdir -p "$ARM_INC"
    limactl shell elinux -- bash -lc 'cd /usr/lib/arm-none-eabi/include && tar -chf - .' | tar -xf - -C "$ARM_INC"
fi
T=$(mktemp -d "${TMPDIR:-/tmp}/cpp-a06.XXXXXX"); trap 'rm -rf "$T"' EXIT
m4() {   # m4 <out.o> <extra flags...>
    local o=$1; shift
    clang++ --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -std=c++20 -Wall -Wextra -fno-exceptions -fno-rtti \
        -ffreestanding -nostdinc++ -isystem "$ARM_INC/c++/14.2.1" \
        -isystem "$ARM_INC/c++/14.2.1/arm-none-eabi/thumb/v7e-m+fp/hard" -isystem "$ARM_INC" "$@" -c demo_target.cpp -o "$o"
}
dis() { xcrun llvm-objdump -d --no-show-raw-insn --disassemble-symbols="$2" "$1" | grep -E '^\s+[0-9a-f]+:|<' ; }
fail=0
run_asm() {
    m4 "$T/o2.o" -O2
    echo "### clang -O2: clock_init_ct"; dis "$T/o2.o" clock_init_ct
    if dis "$T/o2.o" clock_init_ct | grep -qE '\sbl\s'; then echo "CHECK FAIL: clock_init_ct has a call"; fail=1; fi
    if xcrun llvm-nm -C "$T/o2.o" | grep -q pll_search; then echo "CHECK FAIL: pll_search in -O2 object"; fail=1; fi
}
run_size() {
    m4 "$T/o2.o" -O2 -ffunction-sections
    echo "### llvm-size -A (sections > 0)"; xcrun llvm-size -A "$T/o2.o" | awk '$2 > 0'
    echo "### llvm-nm -C --size-sort (defined)"; xcrun llvm-nm -C --size-sort "$T/o2.o" | grep -v ' U '
    echo "### undefined"; xcrun llvm-nm -u "$T/o2.o" || true
}
run_ppm() {   # 런타임 uart_brr 가 부르는 헬퍼 (Cortex-M4 에는 64비트 나눗셈 명령이 없다)
    for k in 1 2 3; do
        m4 "$T/ppm$k.o" -O2 -DWITH_PPM=$k
        echo "### -DWITH_PPM=$k: $(xcrun llvm-nm -C --size-sort "$T/ppm$k.o" | grep -E 'baud_' | awk '{print $3, "0x"$1}')  undefined: $(xcrun llvm-nm -u "$T/ppm$k.o" | tr -s ' \n' ' ')"
    done
}
run_o0() {   # -O0: 어떤 형태가 탐색을 런타임에 남기나 (clang + VM gcc)
    m4 "$T/o0.o" -O0 -DNOT_FORCED
    echo "### clang -O0 -DNOT_FORCED: 함수별 호출 (R_ARM_THM_CALL 대상)"
    for f in o0_const_local o0_plain_local o0_expr o0_consteval_arg o0_constexpr_var; do
        printf "%-18s %s\n" $f "$(xcrun llvm-objdump -dr --no-show-raw-insn --disassemble-symbols=$f "$T/o0.o" | grep -oE 'R_ARM_THM_CALL\s+\S+' | awk '{print $2}' | xcrun llvm-cxxfilt | tr '\n' ' ')"
    done
    if xcrun llvm-objdump -dr --disassemble-symbols=o0_constexpr_var "$T/o0.o" | grep -q R_ARM_THM_CALL; then echo "CHECK FAIL: constexpr variable path has a call at -O0"; fail=1; fi
    limactl shell elinux -- bash -c "rm -rf /tmp/CPP-A06-o0 && mkdir -p /tmp/CPP-A06-o0 && cp '$PWD/solution.cpp' '$PWD/demo_target.cpp' /tmp/CPP-A06-o0/ && cd /tmp/CPP-A06-o0 &&
      arm-none-eabi-g++ -std=c++20 -O0 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -DNOT_FORCED -c demo_target.cpp -o o0.o &&
      echo '### arm-none-eabi-g++ -O0 -DNOT_FORCED' &&
      for f in o0_const_local o0_plain_local o0_expr o0_consteval_arg o0_constexpr_var; do
        printf '%-18s ' \$f; arm-none-eabi-objdump -dr --disassemble=\$f o0.o | grep -oE 'R_ARM_THM_CALL\s+\S+' | awk '{print \$2}' | arm-none-eabi-c++filt | tr '\n' ' '; echo; done" > "$T/gcc0.txt" 2>&1
    cat "$T/gcc0.txt"
    if grep -E '^o0_constexpr_var' "$T/gcc0.txt" | grep -q '[a-z_]\+('; then echo "CHECK FAIL: gcc constexpr variable path has a call"; fail=1; fi
}
run_gcc() {
    limactl shell elinux -- bash -c "rm -rf /tmp/CPP-A06-t && mkdir -p /tmp/CPP-A06-t && cp '$PWD/solution.cpp' '$PWD/demo_target.cpp' /tmp/CPP-A06-t/ && cd /tmp/CPP-A06-t &&
      arm-none-eabi-g++ -std=c++20 -O2 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -ffunction-sections -c demo_target.cpp -o t.o &&
      echo '### arm-none-eabi-g++ 14.2.1 -O2: clock_init_ct' && arm-none-eabi-objdump -d --no-show-raw-insn --disassemble=clock_init_ct t.o | grep -E '^ +[0-9a-f]+:' &&
      echo '### size' && arm-none-eabi-size -A t.o | awk '\$2 > 0' && arm-none-eabi-nm -C --size-sort t.o | grep -v ' U ' ; echo '### undefined'; arm-none-eabi-nm -u t.o" > "$T/gcc.txt" 2>&1
    cat "$T/gcc.txt"
    if sed -n '/clock_init_ct/,/### size/p' "$T/gcc.txt" | grep -qE '\sbl\s'; then echo "CHECK FAIL: gcc clock_init_ct has a call"; fail=1; fi
}
case "${1:-all}" in
asm) run_asm ;; size) run_size ;; ppm) run_ppm ;; o0) run_o0 ;; gcc) run_gcc ;;
all) run_asm; run_size; run_ppm; run_o0; run_gcc ;;
esac
[ $fail -eq 0 ] && echo "target_check: OK" || echo "target_check: FAILED"
exit $fail
