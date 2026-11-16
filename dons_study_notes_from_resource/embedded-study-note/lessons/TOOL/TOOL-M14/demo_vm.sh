#!/usr/bin/env bash
# TOOL-M14: the same crashgen.c as a Linux ELF (aarch64 PIE) in the Lima VM `elinux`:
# DWARF inside the ELF, objcopy split + debuglink, build-id, addr2line / llvm-symbolizer, .build-id dir.
#   bash demo_vm.sh > vm.txt      (run on the Mac; the VM works in its own /tmp/TOOL-M14_work/vm)
set -u
cd "$(dirname "$0")" || exit 2
L=$(pwd)
limactl shell elinux -- bash -s "$L" <<'VM'
set -u
L=$1
W=/tmp/TOOL-M14_work/vm
rm -rf "$W" && mkdir -p "$W" && cd "$W" && cp "$L/crashgen.c" . || exit 2
F="-std=c11 -O1 -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -fno-optimize-sibling-calls -Wl,--build-id"
echo "== $(gcc --version | head -1), $(uname -m)"
echo "== -g level vs ELF size (ELF keeps the DWARF INSIDE the executable)"
printf '%-6s %8s %8s %8s\n' level file_bytes debug_sz text
for g in -g0 -g1 -g -g3; do
    gcc $F $g crashgen.c -o "cg$g" || exit 1
    printf '%-6s %8s %8s %8s\n' "$g" "$(stat -c %s cg$g)" \
        "$(size -A cg$g | awk '$1 ~ /^\.debug_/ {s += $2} END {print s + 0}')" "$(size cg$g | awk 'NR == 2 {print $1}')"
done
cp cg-g crashgen
objcopy --only-keep-debug crashgen crashgen.debug
objcopy --strip-debug --add-gnu-debuglink=crashgen.debug crashgen crashgen.stripped
strip --strip-all -o crashgen.stripall crashgen
echo
echo "== split files: size, build-id (readelf -n), .debug_* count"
for f in crashgen crashgen.debug crashgen.stripped crashgen.stripall; do
    printf '   %-18s %6s  %s  %s\n' $f "$(stat -c %s $f)" "$(readelf -n $f 2>/dev/null | awk '/Build ID/ {print $3}')" "$(readelf -S $f 2>/dev/null | grep -c '\.debug_')"
done
echo
./crashgen.stripped null > crash.txt; echo "== ./crashgen.stripped null  (exit $?)"; cat crash.txt
BASE=$(awk '$1 == "load_address:" {print $2}' crash.txt)
echo
echo "== addr2line wants FILE addresses: pc - load_address (PIE base); frames >= 1 also - 1"
ADDRS=""
while read -r n pc tag; do
    [ "$tag" = crashgen ] || continue
    a=$(( pc - BASE )); [ "$n" -eq 0 ] || a=$(( a - 1 ))
    ADDRS="$ADDRS $(printf '0x%x' $a)"
done < <(awk '/^backtrace:/ {f = 1; next} /^end/ {f = 0} f {print $1, $2, $3}' crash.txt)
ADDRS=${ADDRS# }; A0=${ADDRS%% *}
echo "   file addresses: $ADDRS"
echo "-- addr2line -f -i -p -s -e crashgen.debug"
addr2line -f -i -p -s -e crashgen.debug $ADDRS | sed 's/^/   /'
echo "-- addr2line on crashgen.stripped (follows .gnu_debuglink -> crashgen.debug in the same dir)"
addr2line -f -i -p -s -e crashgen.stripped $ADDRS | sed 's/^/   /'
echo "-- llvm-symbolizer --obj=crashgen.stripped (also follows the debuglink; default = inlines + functions)"
llvm-symbolizer --obj=crashgen.stripped $ADDRS | grep -v '^$' | sed "s#$W/##; s/^/   /"
echo "-- WITHOUT subtracting the PIE base (raw runtime pcs):"
addr2line -f -s -e crashgen.debug "$(awk '$1 == "0" {print $2}' crash.txt)" | tr '\n' ' ' | sed 's/^/   /'; echo
echo
echo "== the .build-id/xx/rest.debug convention (gdb, llvm-symbolizer, debuginfod)"
ID=$(readelf -n crashgen 2>/dev/null | awk '/Build ID/ {print $3}')
mkdir -p "dbg/.build-id/${ID:0:2}" && cp crashgen.debug "dbg/.build-id/${ID:0:2}/${ID:2}.debug" && mkdir -p away && mv crashgen.debug away/
echo "   dbg/.build-id/${ID:0:2}/${ID:2}.debug"
echo "-- crashgen.stripall (no debuglink), crashgen.debug moved away, --debug-file-directory=dbg:"
llvm-symbolizer --debug-file-directory="$W/dbg" --obj=crashgen.stripall "$A0" 2>&1 | grep -v '^$' | sed "s#$W/##; s/^/   /"
echo "-- same, without --debug-file-directory:"
llvm-symbolizer --obj=crashgen.stripall "$A0" 2>&1 | grep -v '^$' | sed 's/^/   /'
VM
