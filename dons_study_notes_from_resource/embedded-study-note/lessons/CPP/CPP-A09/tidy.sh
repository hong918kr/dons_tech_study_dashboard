#!/bin/bash
# CPP-A09 tidy.sh — reproduces every tool number quoted in the lesson (run on the Mac; exit 0 = all steps ran)
#   bash tidy.sh counts   brief check set (cppcoreguidelines/cert/hicpp/bugprone/misc/readability/modernize): legacy vs fixed
#   bash tidy.sh policy   the project policy below (noise off, NOLINT needs a check name + DEV id): legacy vs fixed
#   bash tidy.sh build    legacy + selftest (exceptions/RTTI on) · legacy with -fno-exceptions -fno-rtti · fixed with firmware flags
#   bash tidy.sh san      legacy selftest under ASan+UBSan, then the FIELD_CASES inputs
#   bash tidy.sh gaps     demo_tool_gaps.cpp: run, -Wformat with attribute, UBSan, clang-tidy
#   bash tidy.sh asm      demo_m4_asm.cpp → thumbv7em (M4F) / thumbv6m (M0+) asm + llvm-nm -u (no std headers needed)
#   bash tidy.sh vm       demo_linker_regs.cpp linked in Lima VM 'elinux' (arm-none-eabi-g++ 14.2.1): linker-placed registers
#   bash tidy.sh all      (everything except vm)
set -e
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
: "${TIDY:=$HOME/.venvs/clangtidy/bin/clang-tidy}"                 # LLVM 23.1.4 (pip wheel)
cd "$(dirname "$0")"
OUT=$(mktemp -d "${TMPDIR:-/tmp}/cpp_a09.XXXXXX")
trap 'rm -rf "$OUT"' EXIT
BRIEF='-*,cppcoreguidelines-*,cert-*,hicpp-*,bugprone-*,misc-*,readability-*,modernize-*'
# Project policy = brief set minus noise. Each removal is justified in the lesson §5 "NOLINT 정책".
POLICY="$BRIEF,-readability-identifier-length,-modernize-use-trailing-return-type,-misc-use-internal-linkage,\
-cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,-cppcoreguidelines-pro-bounds-constant-array-index,\
-readability-trailing-comma"
FW="-std=c++20 -fno-exceptions -fno-rtti"

tidy_count() {   # tidy_count <checks> <file> <extra flags...>
    local checks=$1 file=$2; shift 2
    "$TIDY" "$file" --checks="$checks" -- -std=c++20 -isysroot "$SDKROOT" "$@" >"$OUT/t.txt" 2>/dev/null || true
    local n; n=$(grep -c 'warning:' "$OUT/t.txt" || true)
    echo "== $file : $n warnings"
    grep -o '\[[a-z0-9.,-]*\]$' "$OUT/t.txt" | sort | uniq -c | sort -rn || true
}

do_counts() {
    "$TIDY" --list-checks --checks='-*,hicpp-*' 2>&1 | sed 's/^/hicpp module: /'
    tidy_count "$BRIEF" legacy_mc100.cpp
    tidy_count "$BRIEF" fixed_mc100.cpp $FW
}
do_policy() {
    tidy_count "$POLICY" legacy_mc100.cpp
    tidy_count "$POLICY" fixed_mc100.cpp $FW
    echo "== NOLINT lines in fixed_mc100.cpp"; grep -n 'NOLINT' fixed_mc100.cpp || true
}
do_build() {
    echo "== legacy, team flags (exceptions + RTTI on)"
    c++ -std=c++20 -Wall -Wextra -Werror -O2 legacy_mc100.cpp demo_legacy_main.cpp -o "$OUT/leg" && "$OUT/leg"
    echo "== legacy with firmware flags $FW (expected to fail)"
    if c++ $FW -Wall -Wextra -c legacy_mc100.cpp -o "$OUT/x.o" 2>"$OUT/e.txt"; then echo "unexpected: compiled"; exit 1; fi
    grep 'error:' "$OUT/e.txt"
    echo "== fixed, firmware flags"
    c++ $FW -Wall -Wextra -Werror -O2 -c fixed_mc100.cpp -o "$OUT/fixed.o" && echo "fixed_mc100.cpp: 0 warnings"
    echo "== legacy -Weverything (by warning group)"
    c++ -std=c++20 -Weverything -Wno-c++98-compat -c legacy_mc100.cpp -o "$OUT/x.o" 2>&1 | grep -o '\[-W[a-z0-9+-]*\]' | sort | uniq -c | sort -rn
}
do_san() {
    local SAN="-std=c++20 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer"
    echo "== selftest under ASan+UBSan"
    c++ $SAN legacy_mc100.cpp demo_legacy_main.cpp -o "$OUT/s1" && "$OUT/s1" 2>&1 | grep -E 'runtime error|ok' || true
    echo "== selftest, ASAN_OPTIONS=alloc_dealloc_mismatch=1"
    ASAN_OPTIONS=alloc_dealloc_mismatch=1 "$OUT/s1" 2>&1 | grep -E 'ERROR|SUMMARY: Addr' || true
    echo "== FIELD_CASES, plain -O2"
    c++ -std=c++20 -O2 -DFIELD_CASES legacy_mc100.cpp demo_legacy_main.cpp -o "$OUT/f0" && "$OUT/f0" | tail -6
    echo "== FIELD_CASES under ASan+UBSan"
    c++ $SAN -DFIELD_CASES legacy_mc100.cpp demo_legacy_main.cpp -o "$OUT/f1"
    "$OUT/f1" 2>&1 | grep -E 'runtime error: [^l]|runtime error: index|ERROR: Addr|SUMMARY: Addr' || true
}
do_gaps() {
    c++ -std=c++20 -Wall -Wextra -O2 demo_tool_gaps.cpp -o "$OUT/g" && "$OUT/g"
    echo "== -DSHOW_FORMAT (format attribute on log_checked)"
    c++ -std=c++20 -Wall -Wextra -O2 -DSHOW_FORMAT demo_tool_gaps.cpp -o "$OUT/g2" 2>&1 | grep warning || true
    echo "== UBSan"
    c++ -std=c++20 -g -O1 -fsanitize=undefined demo_tool_gaps.cpp -o "$OUT/g3" && "$OUT/g3" 2>&1 | grep 'runtime error' || true
    echo "== clang-tidy (brief set) minus naming/linkage/trailing-return noise"
    "$TIDY" demo_tool_gaps.cpp --checks="$BRIEF" -- -std=c++20 -isysroot "$SDKROOT" 2>/dev/null | grep 'warning:' | sed "s|$PWD/||" |
        grep -v -E 'identifier-length|trailing-return|anonymous-namespace|internal-linkage|magic-numbers|non-const-global|nodiscard|const-correctness|named-parameter' || true
}
do_asm() {
    local A="-std=c++20 -O2 -fno-exceptions -fno-rtti -ffreestanding"
    local M4="--target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfloat-abi=hard"
    local M0="--target=thumbv6m-none-eabi -mcpu=cortex-m0plus"
    strip() { grep -v -E '^\s*(\.[a-z_]+|@ %bb|@ --|$)|^\s*\.L(tmp|func_end)' || true; }
    echo "=================== cortex-m4 (thumbv7em, hard float)"; clang++ $M4 $A -S demo_m4_asm.cpp -o - | strip
    echo "=================== cortex-m0+ (thumbv6m): decode only"
    clang++ $M0 $A -S demo_m4_asm.cpp -o - | strip | awk '/^_Z10decode_(old|new)/,/bx[ \t]+lr/'
    echo "=================== undefined symbols (M4 object)"
    clang++ $M4 $A -c demo_m4_asm.cpp -o "$OUT/m4.o" && xcrun llvm-nm -u "$OUT/m4.o"
    echo "=================== vtable relocations (M4 object): HallA (virtual dtor) vs HallB (protected dtor)"
    xcrun llvm-objdump -r "$OUT/m4.o" | awk '/RECORDS FOR \[\.rodata\._ZTV5Hall/{p=1; print; next} /RECORDS FOR/{p=0} p && /R_ARM/'
}

do_vm() {
    local D=/tmp/CPP-A09
    limactl shell elinux -- bash -c "mkdir -p $D && cp '$PWD/demo_linker_regs.cpp' $D/ && cd $D &&
        arm-none-eabi-g++ -std=c++20 -O2 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -nostartfiles --specs=nosys.specs \
            -Wl,--defsym=PWM1=0x40010000 demo_linker_regs.cpp -o lr.elf 2>&1 | grep -v -E 'not implemented|will always fail' ;
        arm-none-eabi-nm lr.elf | grep -E ' PWM1|ccr0' ;
        arm-none-eabi-objdump -d --no-show-raw-insn lr.elf | awk '/<_Z11ccr0_linkerm>:|<_Z9ccr0_castm>:/{p=1} p{print} /^$/{p=0}'"
}

case "${1:-counts}" in
counts) do_counts ;;
policy) do_policy ;;
build)  do_build ;;
san)    do_san ;;
gaps)   do_gaps ;;
asm)    do_asm ;;
vm)     do_vm ;;
all)    do_counts; do_policy; do_build; do_san; do_gaps; do_asm ;;
*) echo "usage: $0 counts|policy|build|san|gaps|asm|all"; exit 2 ;;
esac
