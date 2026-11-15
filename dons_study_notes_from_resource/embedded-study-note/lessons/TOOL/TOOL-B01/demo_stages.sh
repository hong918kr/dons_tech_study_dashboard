#!/usr/bin/env bash
# TOOL-B01 demo: walk main.c + util.c through -E / -S / -c / link on the Mac and look at every artifact.
#   bash demo_stages.sh            (writes only to /tmp/TOOL-B01_demo)
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-B01_demo
rm -rf "$W" && mkdir -p "$W" && cd "$W" || exit 1
cp "$SRC"/main.c "$SRC"/util.c "$SRC"/util.h "$SRC"/config.h .
CF="-std=c11 -O1"
OBJDUMP=/usr/bin/objdump
ab() { sed -e "s#$SDKROOT#<SDK>#g" -e "s#/Library/Developer/CommandLineTools#<CLT>#g"; }
hr() { printf '\n===== %s =====\n' "$*"; }

hr "1. preprocess: cc -E  (.c -> .i)"
cc $CF -E main.c -o main.i
cc $CF -E util.c -o util.i
wc -l main.c util.c main.i util.i | sed '$d'
echo "linemarker lines in main.i: $(grep -c '^# [0-9]' main.i)"
echo "--- first 3 lines of main.i"
head -3 main.i
echo "--- linemarkers for our own headers"
grep -n '^# [0-9]* "\./*\(util\|config\)\.h"\|^# [0-9]* "main\.c" 2' main.i | head -6
echo "--- tail of main.i (macros are gone, LOG() expanded)"
tail -12 main.i
echo "--- util.i: FW_VERSION and SECTORS_FOR after expansion"
grep -n 'fw_version\[\] =\|return (((' util.i
echo "--- no #define survives -E:  grep -c '#define' main.i = $(grep -c '#define' main.i)"

hr "2. compile: cc -S  (.i -> .s)"
cc $CF -S main.i -o main.s
cc $CF -S util.i -o util.s
wc -l main.s util.s
grep -n '_main:\|bl\s\|adrp\|\.globl\|\.section\|l_.str' main.s

hr "3. assemble: cc -c  (.s -> .o)"
cc -c main.s -o main.o
cc -c util.s -o util.o
file main.o util.o
echo "--- nm main.o  (U = undefined here, to be filled in by the linker)"
nm main.o
echo "--- nm util.o"
nm util.o
echo "--- relocations in main.o (the 'holes')"
$OBJDUMP -r main.o

hr "4. link: cc main.o util.o -> fw"
cc main.o util.o -o fw
file fw
nm fw | grep -v ' radr://' | grep '_main\|_sectors_for\|_checksum8\|_fw_version\|_printf\|_puts'
./fw; echo "exit=$?"

hr "5. -save-temps (all intermediates in one go)"
mkdir st && cp main.c util.c util.h config.h st/ && (cd st && cc $CF -save-temps main.c util.c -o fw2 && ls -1 && file main.bc)

hr "6. -ccc-print-phases"
cc $CF -ccc-print-phases main.c util.c -o fw3

hr "7. -### (driver prints the jobs, runs nothing)"
cc $CF -### main.c util.c -o fw4 2>&1 | grep '^ "' | while read -r line; do
    n=$(printf '%s\n' "$line" | awk -F'" "' '{print NF}')
    prog=$(printf '%s\n' "$line" | awk '{print $1}')
    case $prog in
    *clang\") act=$(printf '%s\n' "$line" | grep -o '"-emit-obj"' | head -1)
              out=$(printf '%s\n' "$line" | grep -o '"-o" "[^"]*"' | sed 's#/var/folders/[^"]*/#$TMPDIR/#')
              in=$(printf '%s\n' "$line" | awk '{print $NF}')
              echo "$prog \"-cc1\" ... $act ... $out ... $in   ($n args)" ;;
    *)        echo "$line" | ab | sed 's#/var/folders/[^"]*/#$TMPDIR/#g'; echo "   ($n args)" ;;
    esac
done | ab

hr "8. -v : include search list"
cc $CF -v -E main.c -o /dev/null 2>&1 | sed -n '/search starts here/,/End of search list/p' | ab

hr "9. -H : header tree for main.c (dots = include depth)"
cc $CF -H -fsyntax-only main.c 2>"$W/h.txt"
head -8 "$W/h.txt" | ab
echo "..."
grep -n 'util.h\|config.h\|/stdint.h$' "$W/h.txt" | ab
echo "total header opens: $(grep -c '^\.' "$W/h.txt"), distinct files: $(sed 's/^\.* //' "$W/h.txt" | sort -u | wc -l | tr -d ' ')"

hr "10. -fsyntax-only (front end only) vs -S (front end + codegen) on every snippet"
cp "$SRC"/bad_*.c "$SRC"/ok_single.c .
printf '%-24s %-14s %-6s %s\n' file fsyntax-only -S "first error at -S"
for f in bad_*.c ok_single.c; do
    cc -std=c11 -fsyntax-only "$f" 2>/dev/null; y=$?
    cc -std=c11 -S "$f" -o /dev/null 2>"$W/s.err"; s=$?
    printf '%-24s %-14s %-6s %s\n' "$f" "rc=$y" "rc=$s" "$(grep -m1 'error' "$W/s.err" | sed 's/^[^ ]* error: //' | cut -c1-60)"
done

hr "11. ld64 diagnostics (Mac linker)"
for t in "bad_undefined.c" "bad_dup_a.c bad_dup_b.c" "bad_no_main.c" "main.c"; do
    echo "--- cc $t"
    cc -std=c11 $t -o x 2>&1 | sed -E 's#(/private)?/var/folders/[^ ]*/#$TMPDIR/#g'
done

hr "12. staged (-E, -S, -c) object == one-shot (cc -c main.c) object?"
cc $CF -c main.c -o oneshot.o
cmp oneshot.o main.o && echo "byte-identical: $(wc -c < main.o | tr -d ' ') bytes"
