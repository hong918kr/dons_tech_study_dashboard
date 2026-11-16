#!/usr/bin/env bash
# TOOL-M14: make 3 field crash logs from the STRIPPED binary, symbolicate them with solution.sh,
# then re-run with exactly ONE starter bug switched on at a time to see each bug's symptom alone.
#   bash demo_crashes.sh > crashes.txt     (logs also kept in /tmp/TOOL-M14_work/demo/crash_*.txt)
set -u
cd "$(dirname "$0")" || exit 2
L=$(pwd)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-M14_work/demo
rm -rf "$W" && mkdir -p "$W/build" "$W/ship" "$W/syms" "$W/v2" && cd "$W" || exit 2
F="-std=c11 -g -O1 -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -fno-optimize-sibling-calls"
( cd build && cc $F -c "$L/crashgen.c" -o crashgen.o && cc crashgen.o -o crashgen ) || exit 1
dsymutil build/crashgen -o syms/crashgen.dSYM && cp build/crashgen ship/ && strip ship/crashgen
( cd v2 && cc $F -DFW_V2 -c "$L/crashgen.c" -o crashgen.o && cc crashgen.o -o crashgen && dsymutil crashgen ) || exit 1
. "$L/solution.sh"
S=ship/crashgen D=syms/crashgen.dSYM

for m in null panic isr; do
    ./ship/crashgen $m > crash_$m.txt; rc=$?
    echo "================ ./ship/crashgen $m   (exit $rc)"
    cat crash_$m.txt
    echo "---- symbolicate (solution.sh)"
    symbolicate crash_$m.txt $S $D
done

echo
echo "================ one bug at a time (everything else = solution.sh)"
echo "---- BUG 1 only: load address = text_vmaddr (ASLR slide ignored)   [crash_null]"
( crash_load_address() { crash_field "$1" text_vmaddr; }; symbolicate crash_null.txt $S $D )
echo "---- BUG 2 only: no '- 1' for return addresses                      [crash_panic]"
( lookup_address() { printf '0x%x\n' $(( $2 )); }; symbolicate crash_panic.txt $S $D )
echo "---- BUG 2b: '- 1' applied to frame 0 too                            [crash_null]"
( lookup_address() { printf '0x%x\n' $(( $2 - 1 )); }; symbolicate crash_null.txt $S $D )
echo "---- BUG 3 only: dSYM UUID never compared, v2 dSYM used              [crash_panic]"
( uuid_check() { return 0; }; symbolicate crash_panic.txt $S v2/crashgen.dSYM )
echo "---- BUG 4 only: atos without -i                                     [crash_isr]"
( atos_frame() { atos -o "$1" -arch arm64 -l "$2" "$3"; }; symbolicate crash_isr.txt $S $D )
echo "---- fixed: wrong dSYM is refused"
symbolicate crash_panic.txt $S v2/crashgen.dSYM 2>&1; echo "exit $?"
echo
echo "== UUIDs:"; dwarfdump --uuid build/crashgen $S $D v2/crashgen v2/crashgen.dSYM | sed "s#$W/##"
