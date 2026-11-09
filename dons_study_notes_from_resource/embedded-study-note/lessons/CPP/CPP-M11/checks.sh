#!/bin/bash
# CPP-M11 checks.sh — 맥에서 실행.
#   bash checks.sh nodiscard   demo_nodiscard.cpp 의 CASE 1..12 를 컴파일해 "경고 O/X" 표 (맥 clang)
#   bash checks.sh try         demo_try.cpp 실행 + -pedantic 경고 + SHOW_ERROR 1..4
#   bash checks.sh size        demo_size_chain.cpp VARIANT 1..5 를 thumbv7em 로 (clang, -Os/-O2) — 함수별 크기
#   bash checks.sh gcc         같은 소스를 Lima VM elinux 의 arm-none-eabi-g++ 로 (.text/링크 크기) + nodiscard CASE 11
#   bash checks.sh asm 2       VARIANT n 의 read_centi / app_tick 디스어셈블
# ARM_INC 를 지정하지 않으면 Lima VM 에서 헤더를 복사해 온다 (CPP-A04 의 asm_targets.sh 와 같은 방식).
set -e
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
: "${ARM_INC:=/tmp/arm-none-eabi-include}"
: "${OUT:=/tmp/cpp_m11}"; mkdir -p "$OUT"
cd "$(dirname "$0")"
OBJDUMP="xcrun llvm-objdump"; NM="xcrun llvm-nm"; SIZE="xcrun llvm-size"

need_arm_inc() {
    if [ ! -d "$ARM_INC/c++/14.2.1" ]; then
        echo "fetching arm-none-eabi headers from Lima VM 'elinux' → $ARM_INC" >&2
        mkdir -p "$ARM_INC"
        limactl shell elinux -- bash -lc 'cd /usr/lib/arm-none-eabi/include && tar -chf - .' | tar -xf - -C "$ARM_INC"
    fi
}
arm_cxx() {  # arm_cxx <std> args...  thumbv7em + arm-none-eabi libstdc++ 헤더
    local std=$1; shift
    clang++ --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -std=$std -fno-exceptions -fno-rtti \
        -ffreestanding -nostdinc++ -isystem "$ARM_INC/c++/14.2.1" \
        -isystem "$ARM_INC/c++/14.2.1/arm-none-eabi/thumb/v7e-m+fp/hard" -isystem "$ARM_INC" "$@"
}
std_of() { case $1 in 3|5) echo c++23 ;; *) echo c++20 ;; esac; }
name_of() { case $1 in 1) echo "C code + out-param" ;; 2) echo "Result<i16,E16>" ;; 3) echo "expected<i16,E16>" ;; 4) echo "Result<i16,EWide>" ;; 5) echo "expected<i16,EWide>" ;; esac; }

case "${1:-}" in
nodiscard)
    printf '%-5s %-9s %s\n' CASE diag "메시지 (첫 줄)"
    for c in 1 2 3 4 5 6 7 8 9 10 11 12; do
        msg=$(c++ -std=c++20 -Wall -Wextra -DCASE=$c -fsyntax-only demo_nodiscard.cpp 2>&1 | grep -E 'warning:|error:' | head -1 | sed -E 's/.*(warning|error): //' || true)
        if [ -n "$msg" ]; then printf '%-5s %-9s %s\n' "$c" "경고" "$msg"; else printf '%-5s %-9s %s\n' "$c" "없음" "-"; fi
    done
    ;;
try)
    c++ -std=c++20 -Wall -Wextra -O2 -fno-exceptions -fno-rtti demo_try.cpp -o "$OUT/demo_try" && "$OUT/demo_try"
    echo "--- -pedantic"
    c++ -std=c++20 -pedantic -fsyntax-only demo_try.cpp 2>&1 | grep -E 'warning:' | sort | uniq -c | head -3 || true
    for n in 1 2 3 4; do
        echo "--- SHOW_ERROR=$n"
        c++ -std=c++20 -DSHOW_ERROR=$n -fsyntax-only demo_try.cpp 2>&1 | grep -E 'error:' | head -3 || true
    done
    ;;
size)
    need_arm_inc
    for opt in -Os -O2; do
        echo "--- clang++ thumbv7em-none-eabihf $opt, demo_size_chain.cpp (.text bytes / symbol bytes)"
        printf '%-3s %-22s %6s  %s\n' V variant text "read_reg  probe  read_centi  app_tick"
        for v in 1 2 3 4 5; do
            arm_cxx "$(std_of $v)" $opt -DVARIANT=$v -c demo_size_chain.cpp -o "$OUT/v$v$opt.o"
            text=$($SIZE "$OUT/v$v$opt.o" | awk 'NR==2{print $1}')
            syms=""
            while read -r _addr sz _ty name _rest; do
                d=$((16#$sz))
                case "$name" in
                    read_reg*|probe*|read_centi*|app_tick*) syms="$syms ${name%%(*}=$d" ;;
                esac
            done < <($NM -S -C --defined-only "$OUT/v$v$opt.o")
            printf '%-3s %-22s %6s  %s\n' "$v" "$(name_of $v)" "$text" "$syms"
        done
    done
    ;;
asm)
    need_arm_inc
    v=${2:-2}
    arm_cxx "$(std_of $v)" -Os -DVARIANT=$v -c demo_size_chain.cpp -o "$OUT/asm$v.o"
    $OBJDUMP -d --no-show-raw-insn "$OUT/asm$v.o" | c++filt
    ;;
gcc)
    limactl shell elinux -- bash -c "rm -rf /tmp/CPP-M11 && mkdir -p /tmp/CPP-M11 && cp '$PWD'/*.cpp /tmp/CPP-M11/ && cd /tmp/CPP-M11 && bash -s" <<'VM'
set -e
CF="-Os -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -ffunction-sections -fdata-sections"
echo "--- arm-none-eabi-g++ -Os .o sizes (text)"
for v in 1 2 3 4 5; do
    case $v in 3|5) STD=c++23 ;; *) STD=c++20 ;; esac
    arm-none-eabi-g++ -std=$STD $CF -DVARIANT=$v -c demo_size_chain.cpp -o v$v.o
    arm-none-eabi-size v$v.o | tail -1 | awk -v v=$v '{printf "V%s text=%s\n", v, $1}'
    while read -r _addr sz ty name _rest; do
        case "$ty" in [Tt]) echo "    $((16#$sz)) ${name%%(*}" ;; esac
    done < <(arm-none-eabi-nm -C --size-sort -S v$v.o)
done
echo "--- linked ELF (arm-none-eabi-g++ -Wl,--gc-sections, _start, stub i2c_xfer)"
for v in 1 2 3 4 5; do
    case $v in 3|5) STD=c++23 ;; *) STD=c++20 ;; esac
    arm-none-eabi-g++ -std=$STD $CF -DVARIANT=$v -DWITH_START -nostartfiles --specs=nosys.specs -Wl,--gc-sections demo_size_chain.cpp -o v$v.elf 2>/dev/null
    arm-none-eabi-size v$v.elf | tail -1 | awk -v v=$v '{printf "V%s text=%s data=%s bss=%s\n", v, $1, $2, $3}'
done
echo "--- std::expected with .value() (VARIANT=3 -DUSE_VALUE): does the throw path pull anything?"
arm-none-eabi-g++ -std=c++23 $CF -DVARIANT=3 -DUSE_VALUE -DWITH_START -nostartfiles --specs=nosys.specs -Wl,--gc-sections demo_size_chain.cpp -o v3v.elf 2>/dev/null
arm-none-eabi-size v3v.elf | tail -1 | awk '{printf "V3+value() text=%s data=%s bss=%s\n", $1, $2, $3}'
arm-none-eabi-nm -C v3v.elf | grep -c -iE 'bad_expected_access|__cxa|abort' || true
arm-none-eabi-nm -C v3v.elf | grep -iE 'bad_expected_access|__cxa_|abort' | head
echo "--- CASE 11 ((void) on GNU warn_unused_result) with GCC, native g++ and arm-none-eabi-g++"
g++ -std=c++20 -Wall -Wextra -DCASE=11 -c -o /dev/null demo_nodiscard.cpp 2>&1 | grep -E 'warning' | head -2 || echo "(no warning)"
arm-none-eabi-g++ -std=c++20 -Wall -Wextra -DCASE=11 -c -o /dev/null demo_nodiscard.cpp 2>&1 | grep -E 'warning' | head -2 || echo "(no warning)"
echo "--- GCC: demo_try (temporary lifetime/sibling evaluation)"
g++ -std=c++20 -O2 -fno-exceptions -fno-rtti demo_try.cpp -o demo_try_g && ./demo_try_g
g++ -std=c++20 -pedantic -fsyntax-only demo_try.cpp 2>&1 | grep -E 'warning|error' | head -3
VM
    ;;
*) echo "usage: $0 nodiscard|try|size|asm <v>|gcc"; exit 2 ;;
esac
