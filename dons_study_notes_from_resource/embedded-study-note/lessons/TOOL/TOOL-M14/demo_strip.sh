#!/usr/bin/env bash
# TOOL-M14: strip levels, what atos can still do without the dSYM, and why the UUID is the only safe key.
#   bash demo_strip.sh > strip.txt      (Mac; builds into /tmp/TOOL-M14_work/strip, takes ~5 s)
set -u
cd "$(dirname "$0")" || exit 2
L=$(pwd)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-M14_work/strip
rm -rf "$W" && mkdir -p "$W" && cd "$W" || exit 2
F="-std=c11 -g -O1 -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -fno-optimize-sibling-calls"
cc $F -c "$L/crashgen.c" -o crashgen.o && cc crashgen.o -o crashgen && dsymutil crashgen -o crashgen.dSYM || exit 1
for s in S x all; do
    mkdir -p "s_$s" && cp crashgen "s_$s/crashgen"
    if [ $s = all ]; then strip "s_$s/crashgen"; else strip -$s "s_$s/crashgen"; fi
done
echo "== strip levels: file size, symbols (nm), debug-map stabs (nm -a | OSO/FUN), UUID"
printf '%-22s %6s %4s %6s %6s  %s\n' file bytes nm stabs local uuid
for f in crashgen s_S/crashgen s_x/crashgen s_all/crashgen; do
    printf '%-22s %6s %4s %6s %6s  %s\n' "$f" "$(stat -f %z "$f")" "$(nm "$f" | wc -l | tr -d ' ')" \
        "$(nm -a "$f" | grep -cE ' (OSO|FUN|SO) ')" "$(nm "$f" | grep -c ' t ')" "$(dwarfdump --uuid "$f" | awk '{print $2}')"
done
echo "   (strip -S = debug stabs only, -x = also local symbols, plain strip = everything not needed by dyld)"
echo "   dSYM bundle: $(du -sk crashgen.dSYM | awk '{print $1}') KB on disk, DWARF file $(stat -f %z crashgen.dSYM/Contents/Resources/DWARF/crashgen) bytes"
echo
./s_all/crashgen null > crash.txt
LA=$(awk '$1 == "load_address:" {print $2}' crash.txt)
PC0=$(awk '$1 == "0" {print $2}' crash.txt); PC1=$(awk '$1 == "1" {print $2}' crash.txt)
PC1M=$(printf '0x%x' $((PC1 - 1)))
echo "== the same two frames (pc $PC0, return-1 $PC1M, load address $LA) looked up 4 ways"
mkdir -p alone && cp s_all/crashgen alone/
echo "-- atos -o <fully stripped exe>, no dSYM anywhere near it:"; atos -o alone/crashgen -arch arm64 -l "$LA" "$PC0" "$PC1M"
mkdir -p alone_x && cp s_x/crashgen alone_x/
echo "-- atos -o <strip -x exe> (globals only):"; atos -o alone_x/crashgen -arch arm64 -l "$LA" "$PC0" "$PC1M"
echo "-- why '+ 12' without a name: LC_FUNCTION_STARTS survives strip (dyld_info -function_starts):"
dyld_info -function_starts s_all/crashgen | grep -E '0x100000(EF8|FB8)' | sed 's/^ */   /'
echo "-- atos -o <dSYM>:"; atos -i -o crashgen.dSYM -arch arm64 -l "$LA" "$PC0" "$PC1M" | grep -v '^$'
echo "-- atos -s <slide> instead of -l (slide = $(awk '$1 == "slide:" {print $2}' crash.txt)):"
atos -o crashgen.dSYM -arch arm64 -s "$(awk '$1 == "slide:" {print $2}' crash.txt)" "$PC0"
echo "-- by hand: file address = pc - load_address + 0x100000000 = $(printf '0x%x' $((PC0 - LA + 0x100000000))), no -l:"
atos -o crashgen.dSYM -arch arm64 "$(printf '0x%x' $((PC0 - LA + 0x100000000)))"
echo
echo "== the UUID is per build, not per source: rebuild the same source (sibling dirs re1/re2, same path length), 1 s apart"
for i in 1 2; do
    sleep 1; mkdir -p "re$i" && (cd "re$i" && cc $F -c "$L/crashgen.c" -o crashgen.o && cc crashgen.o -o crashgen)
    printf '   rebuild %s: %s  OSO mtime %s  __text md5 %s\n' $i "$(dwarfdump --uuid re$i/crashgen | awk '{print $2}')" \
        "$(nm -a re$i/crashgen | awk '/ OSO / {print $1}')" "$(otool -t re$i/crashgen | tail -n +2 | md5 -q)"
done
for i in 1 2; do
    sleep 1; mkdir -p "z$i" && (cd "z$i" && ZERO_AR_DATE=1 cc $F -c "$L/crashgen.c" -o crashgen.o && ZERO_AR_DATE=1 cc crashgen.o -o crashgen)
    printf '   ZERO_AR_DATE=1 rebuild %s: %s  OSO mtime %s\n' $i "$(dwarfdump --uuid z$i/crashgen | awk '{print $2}')" \
        "$(nm -a z$i/crashgen | awk '/ OSO / {print $1}')"
done
