#!/usr/bin/env bash
# TOOL-M14 Part A: how big is debug info at each -g level, where does it live, what does atos get back?
#   bash demo_glevels.sh > glevels.txt      (Mac; works in /tmp/TOOL-M14_work/glevels)
set -u
cd "$(dirname "$0")" || exit 2
L=$(pwd)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-M14_work/glevels
rm -rf "$W" && mkdir -p "$W" && cd "$W" || exit 2
SIZE=/Library/Developer/CommandLineTools/usr/bin/llvm-size
BASE="-std=c11 -O1 -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -fno-optimize-sibling-calls"
dbg_sum() {   # total bytes of __debug_* sections (Mach-O .o or dSYM DWARF file)
    "$SIZE" -A "$1" 2>/dev/null | awk '$1 ~ /^__debug_/ {s += $2} END {print s + 0}'
}
dbg_one() { "$SIZE" -A "$1" 2>/dev/null | awk -v n="$2" '$1 == n {print $2; f=1} END {if (!f) print 0}'; }

echo "== clang: $(cc --version | head -1)"
printf '%-26s %7s %7s %8s %8s %7s %6s %6s %7s\n' level o_bytes o_dbg exe_bytes dsym_dwarf info line str macro
for g in -g0 -gline-tables-only -g1 -g -g3 "-g3 -fdebug-macro -gdwarf-4"; do
    d=$W/$(echo "${g#-}" | tr -d ' ')
    mkdir -p "$d"
    cc $BASE $g -c "$L/crashgen.c" -o "$d/crashgen.o" && cc "$d/crashgen.o" -o "$d/crashgen" || exit 1
    dsymutil "$d/crashgen" -o "$d/crashgen.dSYM" 2>"$d/dsymutil.err"
    dw=$d/crashgen.dSYM/Contents/Resources/DWARF/crashgen
    dsz=0; [ -f "$dw" ] && dsz=$(stat -f %z "$dw")
    printf '%-26s %7s %7s %8s %8s %7s %6s %6s %7s\n' "$g" "$(stat -f %z "$d/crashgen.o")" "$(dbg_sum "$d/crashgen.o")" \
        "$(stat -f %z "$d/crashgen")" "$dsz" "$(dbg_one "$dw" __debug_info)" "$(dbg_one "$dw" __debug_line)" \
        "$(dbg_one "$dw" __debug_str)" "$(( $(dbg_one "$dw" __debug_macro) + $(dbg_one "$dw" __debug_macinfo) ))"
done
echo
echo "== dsymutil on the -g0 build (no DWARF in the .o):"
sed 's#^#   #' "$W/g0/dsymutil.err"; ls -la "$W/g0/crashgen.dSYM/Contents/Resources/DWARF/" 2>&1 | tail -n +2 | sed 's#^#   #'
echo
echo "== __TEXT/__DATA are identical at every -g level (debug info never ships in the code):"
for g in g0 gline-tables-only g1 g g3; do
    printf '   %-18s %s\n' "-$g" "$("$SIZE" -A "$W/$g/crashgen" | awk '$1 ~ /^__(text|cstring|const|data|bss|unwind_info)$/ {printf "%s %s  ", $1, $2}')"
done
echo
echo "== atos -i on frames 0 and 1 of a 'null' crash, per -g level (fresh ASLR run each time)"
for g in g0 gline-tables-only g1 g g3; do
    d=$W/$g
    "$d/crashgen" null > "$d/crash.txt"
    la=$(awk '/^load_address:/ {print $2}' "$d/crash.txt")
    pc0=$(awk '$1 == "0" {print $2}' "$d/crash.txt")
    pc1=$(awk '$1 == "1" {print $2}' "$d/crash.txt")
    pc1m=$(printf '0x%x' $((pc1 - 1)))
    echo "-- -$g   load_address $la"
    atos -i -o "$d/crashgen.dSYM" -arch arm64 -l "$la" "$pc0" "$pc1m" 2>&1 | grep -v '^$' | sed 's#^#   #'
done
echo
echo "== lldb: variables available? (-gline-tables-only vs -g), stopped at parse_record"
for g in gline-tables-only g; do
    echo "-- -$g"
    lldb -b -o 'b parse_record' -o 'run null' -o 'frame variable' -o 'kill' "$W/$g/crashgen" 2>&1 \
        | grep -E 'frame #0|^\(const packet|^\(unsigned int\)|error' | head -4 | sed 's#^#   #'
done
