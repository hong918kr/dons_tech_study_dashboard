#!/usr/bin/env bash
# TOOL-M14: what is inside the dSYM that turns an address into "func (file:line)"?
#   bash demo_dwarf.sh > dwarf.txt     (Mac; builds into /tmp/TOOL-M14_work/dwarf)
set -u
cd "$(dirname "$0")" || exit 2
L=$(pwd)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-M14_work/dwarf
rm -rf "$W" && mkdir -p "$W" && cd "$W" || exit 2
F="-std=c11 -g -O1 -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -fno-optimize-sibling-calls"
cc $F -c "$L/crashgen.c" -o crashgen.o && cc crashgen.o -o crashgen && dsymutil crashgen -o crashgen.dSYM || exit 1
short() { sed -e "s#$L/##g"; }

echo "== where does the DWARF live?  (Mach-O: in the .o + a 'debug map' in the exe; dSYM = linked copy)"
nm -a crashgen | grep -E ' (OSO|SO) | FUN _(check_header|parse_record)$' | short
echo
echo "== function addresses (unslid file addresses, __TEXT at 0x100000000)"
nm -n crashgen | grep -E ' [tT] _(check_header|parse_record|fw_panic|process_packet|main)$'
echo
echo "== disassembly: check_header ends with 'bl _fw_panic' -> the return address is parse_record's first insn"
otool -tv crashgen | awk '/^_check_header:/ {p = 1} /^_fw_panic:/ {p = 0} p' | sed 's/^/   /'
echo
echo "== dwarfdump --debug-line (the line table rows for 0x100000f84..0x100000ffc)"
dwarfdump --debug-line crashgen.dSYM | grep -E '^Address|^------' | head -2
dwarfdump --debug-line crashgen.dSYM | awk '$1 ~ /^0x0000000100000f[89a-f]/' | head -16
echo
echo "== dwarfdump --lookup 0x100000fc4  (null crash, frame 0, after subtracting the slide)"
dwarfdump --lookup 0x100000fc4 crashgen.dSYM | short | grep -E 'DW_TAG_subprogram|DW_TAG_inlined|DW_AT_name|abstract_origin|low_pc|high_pc|call_line|call_column|Line info'
echo
echo "== dwarfdump --lookup 0x100000fb7  (panic crash, frame 1: return address 0x100000fb8 minus 1)"
dwarfdump --lookup 0x100000fb7 crashgen.dSYM | short | grep -E 'DW_AT_name|low_pc|high_pc|Line info'
echo "== dwarfdump --lookup 0x100000fb8  (same frame, WITHOUT minus 1)"
dwarfdump --lookup 0x100000fb8 crashgen.dSYM | short | grep -E 'DW_AT_name|low_pc|high_pc|Line info'
echo
echo "== lldb on the unslid binary agrees (image lookup -v shows the inline chain)"
lldb -b -o 'image lookup -v -a 0x100000fc4' crashgen.dSYM/../crashgen 2>&1 | short | grep -E 'Summary|Function|Blocks|LineEntry' | head -6
