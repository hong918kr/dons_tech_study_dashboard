#!/usr/bin/env bash
# TOOL-B04 demo (Mac): build anatomy.c as Mach-O and ELF, dump everything the lesson talks about.
#   bash demo_mac.sh            -> all dumps
#   bash demo_mac.sh relocs     -> only the part whose title contains "relocs"
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
OBJDUMP=/usr/bin/objdump
LLVM_NM=/Library/Developer/CommandLineTools/usr/bin/llvm-nm
W=/tmp/TOOL-B04_work/demo
rm -rf "$W" && mkdir -p "$W" || exit 2
cc -O2 -c anatomy.c -o "$W/anatomy_macho.o" || exit 1
clang --target=aarch64-linux-gnu -O2 -c anatomy.c -o "$W/anatomy_elf.o" || exit 1
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -c anatomy.c -o "$W/anatomy_m4.o" || exit 1
cd "$W" || exit 2
ONLY=${1:-}
part() { # part <title> <command...>
	case "$1" in *"$ONLY"*) ;; *) return ;; esac
	echo "### $1"; echo "\$ ${*:2}"; "${@:2}"; echo
}
part "file types"            file anatomy_macho.o anatomy_elf.o anatomy_m4.o
part "macho header"          otool -hv anatomy_macho.o
part "macho load commands"   otool -l anatomy_macho.o
part "macho nm"              nm anatomy_macho.o
part "macho nm -m"           nm -m anatomy_macho.o
part "macho sections"        "$OBJDUMP" -h anatomy_macho.o
part "macho symtab"          "$OBJDUMP" -t anatomy_macho.o
part "macho relocs"          "$OBJDUMP" -r anatomy_macho.o
part "macho disasm+relocs"   "$OBJDUMP" -d -r --no-show-raw-insn anatomy_macho.o
part "elf private headers"   "$OBJDUMP" --private-headers anatomy_elf.o
part "elf nm"                "$LLVM_NM" anatomy_elf.o
part "elf sections"          "$OBJDUMP" -h anatomy_elf.o
part "elf symtab"            "$OBJDUMP" -t anatomy_elf.o
part "elf relocs"            "$OBJDUMP" -r anatomy_elf.o
part "elf disasm+relocs"     "$OBJDUMP" -d -r anatomy_elf.o
part "elf rodata.str bytes"  "$OBJDUMP" -s -j .rodata.str1.1 -j .data anatomy_elf.o
part "m4 nm"                 "$LLVM_NM" anatomy_m4.o
part "m4 relocs"             "$OBJDUMP" -r anatomy_m4.o
part "m4 disasm+relocs"      "$OBJDUMP" -d -r anatomy_m4.o
