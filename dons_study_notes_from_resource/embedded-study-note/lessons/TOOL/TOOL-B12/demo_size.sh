#!/usr/bin/env bash
# TOOL-B12 demo: code-size cost of UBSan modes on a Cortex-M4 object (Mac clang, no link).
#   bash demo_size.sh > size_m4.txt
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
SIZE=/Library/Developer/CommandLineTools/usr/bin/llvm-size
W=/tmp/TOOL-B12_demo_size; rm -rf "$W"; mkdir -p "$W"
M4="--target=thumbv7em-none-eabi -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -ffreestanding -Os -std=c11"
TRAP="-fsanitize=undefined -fsanitize-trap=undefined"
modes=(
  "none|"
  "trap|$TRAP"
  "trap-nofunc|$TRAP -fno-sanitize=function"
  "trap-nomerge|$TRAP -fno-sanitize-merge"
  "trap-subset|-fsanitize=signed-integer-overflow,shift,integer-divide-by-zero,bounds,float-cast-overflow -fsanitize-trap=all"
  "minimal-rt|-fsanitize=undefined -fsanitize-minimal-runtime"
  "full-rt|-fsanitize=undefined"
)
for src in solution starter; do
    echo "### $src.c  ($M4)"
    printf '%-13s %6s %6s %6s %6s  %s\n' mode text data bss total "udf(trap) / handler calls"
    for m in "${modes[@]}"; do
        name=${m%%|*}; flags=${m#*|}
        # shellcheck disable=SC2086
        clang $M4 $flags -c "$src.c" -o "$W/$src-$name.o" 2>"$W/$src-$name.log" || { echo "$name: build failed"; continue; }
        read -r t d b dec _ < <("$SIZE" "$W/$src-$name.o" | awk 'NR==2')
        traps=$(/usr/bin/objdump -d "$W/$src-$name.o" | grep -c -E '^ +[0-9a-f]+:.*[[:space:]](trap|udf)([[:space:]]|$)')
        calls=$(/usr/bin/objdump -dr "$W/$src-$name.o" | grep -c -E 'R_ARM_THM_(CALL|JUMP24).*__ubsan_handle')
        printf '%-13s %6s %6s %6s %6s  %s / %s\n' "$name" "$t" "$d" "$b" "$dec" "$traps" "$calls"
    done
    echo
done
echo "### warnings printed by clang for minimal-rt:"
sort -u "$W/solution-minimal-rt.log"
echo
echo "### llvm-size -A solution-full-rt.o (where the runtime-mode bytes go)"
"$SIZE" -A "$W/solution-full-rt.o" | grep -v -E '^\.(ARM\.attributes|comment|note|debug)'
echo "### undefined handler symbols in solution-full-rt.o / solution-minimal-rt.o"
/Library/Developer/CommandLineTools/usr/bin/llvm-nm -u "$W/solution-full-rt.o" | sort -u
/Library/Developer/CommandLineTools/usr/bin/llvm-nm -u "$W/solution-minimal-rt.o" | sort -u
echo
echo "### thumb disassembly: starter adc_scale, trap mode"
/usr/bin/objdump -d --disassemble-symbols=adc_scale "$W/starter-trap.o" | sed -n '/<adc_scale>/,$p'
echo "### same, -fno-sanitize-merge"
/usr/bin/objdump -d --disassemble-symbols=adc_scale "$W/starter-trap-nomerge.o" | sed -n '/<adc_scale>/,$p'
