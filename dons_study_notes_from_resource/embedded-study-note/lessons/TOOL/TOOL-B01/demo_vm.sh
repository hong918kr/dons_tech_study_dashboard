#!/usr/bin/env bash
# TOOL-B01 demo, Linux side (ELF + GNU toolchain). Run from the Mac:
#   limactl shell elinux -- bash <lesson dir>/demo_vm.sh
# The Mac home is mounted read-only in the VM -> everything is copied to /tmp/TOOL-B01_work/vm first.
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
W=/tmp/TOOL-B01_work/vm
rm -rf "$W" && mkdir -p "$W" && cp "$SRC"/*.c "$SRC"/*.h "$SRC"/solution.sh "$W"/ && cd "$W" || exit 1
hr() { printf '\n===== %s =====\n' "$*"; }
first() { grep -m1 -E "$1" | cut -c1-150; }

hr "0. tools"
gcc --version | head -1; clang --version | head -1; ld --version | head -1; as --version | head -1

hr "1. gcc -v -c main.c : cc1 and as are SEPARATE programs"
gcc -std=c11 -O1 -v -c main.c 2>&1 | grep -E '/cc1 |-as ' | sed 's/^ *//' | awk '{printf "%s ... (%d words)\n", substr($0,1,110), NF}'

hr "2. gcc -save-temps main.c util.c"
mkdir st && cp main.c util.c util.h config.h st/ && (cd st && gcc -std=c11 -O1 -save-temps main.c util.c -o fw && ls -1 && file fw-main.i fw-main.s fw-main.o fw | cut -c1-110 && ./fw)
echo "--- one file + -c: plain names"
(cd st && gcc -std=c11 -O1 -save-temps -c main.c && ls -1 main.*)
echo "--- first lines of gcc's main.i"
head -3 st/main.i
echo "lines: $(wc -l < st/main.i)  linemarkers: $(grep -c '^# [0-9]' st/main.i)"

hr "3. gcc -v link: collect2 -> ld"
(cd st && gcc -c util.c) && gcc -v st/main.o st/util.o -o fw_v 2>&1 | grep -E '/collect2 ' | awk '{printf "%s ... (%d words)\n", substr($0,1,140), NF}'

hr "4. GNU ld diagnostics (gcc 15 + ld.bfd)"
for t in "bad_undefined.c" "bad_dup_a.c bad_dup_b.c" "bad_no_main.c" "main.c"; do
    echo "--- gcc $t"
    gcc -std=c11 $t -o x 2>&1 | grep -v '^collect2: error: ld returned' | sed 's#/usr/lib/gcc/aarch64-linux-gnu/15/../../../aarch64-linux-gnu/#<libdir>/#' | head -4
done
echo "--- same with ld.lld (clang -fuse-ld=lld)"
clang -std=c11 -fuse-ld=lld bad_undefined.c -o x 2>&1 | head -3
clang -std=c11 -fuse-ld=lld bad_dup_a.c bad_dup_b.c -o x 2>&1 | head -3

hr "5. inline asm: gcc -S copies the text, as(1) rejects it"
gcc -std=c11 -S bad_asm_mnemonic.c -o asm.s; echo "gcc -S rc=$?"; grep -n frobnicate asm.s
gcc -c asm.s -o asm.o; echo "as rc=$?"
clang -std=c11 -S bad_asm_mnemonic.c -o asm2.s 2>clang.err; echo "clang -S rc=$?"; head -1 clang.err
clang -std=c11 -fno-integrated-as -S bad_asm_mnemonic.c -o asm3.s; echo "clang -fno-integrated-as -S rc=$?"

hr "6. C89 vs C11: where does a call to an undeclared function fail?"
gcc -std=c89 -Wall -c bad_implicit.c -o imp.o 2>&1 | first 'warning|error'; echo "gcc -std=c89 -Wall -c rc=${PIPESTATUS[0]}"
gcc imp.o -o imp 2>&1 | first 'undefined'
gcc -std=c11 -c bad_implicit.c -o imp11.o 2>&1 | first 'warning|error'; echo "gcc -std=c11 -c rc=${PIPESTATUS[0]}"

hr "7. first_failing_stage (solution.sh) with CC=gcc and CC=clang in the VM"
. ./solution.sh
export STAGE_DIR=$W/stage
printf '%-28s %-10s %-10s\n' input gcc-15 clang-21
for t in bad_error.c bad_missing_header.c bad_unterminated_if.c bad_macro_args.c bad_syntax.c bad_type.c \
         bad_macro_paren.c bad_implicit.c bad_attr_error.c bad_asm_mnemonic.c bad_asm_org.c \
         bad_undefined.c "bad_dup_a.c bad_dup_b.c" bad_no_main.c main.c ok_single.c "main.c util.c"; do
    g=$(CC=gcc first_failing_stage $t 2>/dev/null)
    c=$(CC=clang first_failing_stage $t 2>/dev/null)
    printf '%-28s %-10s %-10s\n' "$t" "$g" "$c"
done
echo "--- gcc's message for bad_asm_mnemonic.c / bad_asm_org.c / bad_attr_error.c"
CC=gcc first_failing_stage bad_asm_mnemonic.c 2>&1 >/dev/null | first 'Error|error'
CC=gcc first_failing_stage bad_asm_org.c 2>&1 >/dev/null | first 'Error|error'
CC=gcc first_failing_stage bad_attr_error.c 2>&1 >/dev/null | first 'error:'

echo "--- gcc -fsyntax-only (front end only)"
for t in bad_attr_error.c bad_asm_mnemonic.c bad_asm_org.c; do gcc -std=c11 -fsyntax-only $t 2>/dev/null; echo "gcc -fsyntax-only $t rc=$?"; done

echo "--- gcc's first error line per front-end snippet"
for t in bad_error.c bad_missing_header.c bad_unterminated_if.c bad_macro_args.c bad_syntax.c bad_type.c bad_macro_paren.c; do
    printf '%-22s %s\n' "$t" "$(gcc -std=c11 -fsyntax-only $t 2>&1 | grep -m1 'error' | sed 's/^[^ ]* //' | cut -c1-95)"
done

hr "8. upstream clang: .s is NOT preprocessed (.S is)"
cp st/main.s x.s; cp st/main.s x.S
clang -ccc-print-phases -c x.s 2>&1; echo ---; clang -ccc-print-phases -c x.S 2>&1

hr "9. the same 4 stages with the firmware cross compiler (arm-none-eabi-gcc, Cortex-M4)"
arm-none-eabi-gcc --version | head -1
mkdir m4 && cp main.c util.c util.h config.h m4/ && cd m4 || exit 1
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -Os -std=c11 -save-temps -c util.c && ls -1 util.*
file util.o | cut -c1-90
echo "--- util.s: sectors_for"
sed -n '/^sectors_for:/,/bx\tlr/p' util.s
echo "--- link util.o alone (newlib crt0 wants main)"
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb util.o -o x.elf 2>&1 | grep -m1 -o "undefined reference to .main'"
echo "--- link main.c util.c with newlib-nano + nosys stubs"
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -Os -std=c11 --specs=nano.specs --specs=nosys.specs main.c util.c -o fw.elf 2>&1 \
    | grep -o 'warning: _[a-z]* is not implemented and will always fail' | sort -u
file fw.elf | cut -c1-100
arm-none-eabi-size fw.elf
