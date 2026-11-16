#!/usr/bin/env bash
# TOOL-M14 Part C: the firmware (ELF, Cortex-M4) side on the Mac with arm-none-eabi binutils.
#   bash demo_m4.sh > m4.txt        (builds into /tmp/TOOL-M14_work/m4)
set -u
cd "$(dirname "$0")" || exit 2
L=$(pwd)
W=/tmp/TOOL-M14_work/m4
rm -rf "$W" && mkdir -p "$W" && cd "$W" || exit 2
P=arm-none-eabi
CF="-mcpu=cortex-m4 -mthumb -O1 -ffreestanding -nostdlib -Wl,--build-id=sha1 -T $L/fw_m4.ld"
short() { sed -e "s#$L/##g"; }

echo "== $($P-gcc --version | head -1)"
echo "== -g level vs ELF file size vs what goes into Flash (.bin)"
printf '%-6s %8s %8s %8s %8s %6s\n' level elf_bytes debug_sz text bin_bytes
for g in -g0 -g1 -g -g3; do
    $P-gcc $CF $g "$L/fw_m4.c" -o "fw$g.elf" || exit 1
    $P-objcopy -O binary "fw$g.elf" "fw$g.bin"
    dbg=$($P-size -A "fw$g.elf" | awk '$1 ~ /^\.debug_/ {s += $2} END {print s + 0}')
    txt=$($P-size "fw$g.elf" | awk 'NR == 2 {print $1}')
    printf '%-6s %8s %8s %8s %8s\n' "$g" "$(stat -f %z "fw$g.elf")" "$dbg" "$txt" "$(stat -f %z "fw$g.bin")"
done
echo "   cmp fw-g0.bin fw-g3.bin: $(cmp fw-g0.bin fw-g3.bin 2>&1); differing bytes $(cmp -l fw-g0.bin fw-g3.bin | awk 'NR == 1 {f = $1} {l = $1; n++} END {print f "-" l " (" n " bytes)"}')"
echo "   build-id -g0 $($P-readelf -n fw-g0.elf | awk '/Build ID/ {print $3}')  -g3 $($P-readelf -n fw-g3.elf | awk '/Build ID/ {print $3}')"
for g in -g0 -g1 -g -g3; do $P-objcopy -O binary -j .isr_vector -j .text "fw$g.elf" "code$g.bin"; done
echo "   only .isr_vector+.text: $(cmp code-g0.bin code-g3.bin && cmp code-g0.bin code-g1.bin && cmp code-g0.bin code-g.bin && echo identical at every -g level)"
echo
cp fw-g.elf fw.elf
echo "== split the debug info (GNU way): --only-keep-debug / --strip-debug / --add-gnu-debuglink"
$P-objcopy --only-keep-debug fw.elf fw.debug
$P-objcopy --strip-debug --add-gnu-debuglink=fw.debug fw.elf fw.stripped
$P-objcopy --strip-all fw.elf fw.stripall
for f in fw.elf fw.debug fw.stripped fw.stripall; do
    printf '   %-12s %6s bytes  build-id %-42s debug sections %s\n' "$f" "$(stat -f %z $f)" \
        "$($P-readelf -n $f 2>/dev/null | awk '/Build ID/ {print $3}')" "$($P-readelf -S $f | grep -c '\.debug_')"
done
echo "-- readelf -S fw.debug (code sections become NOBITS: addresses kept, bytes dropped)"
$P-readelf -S -W fw.debug | grep -E '^ +\[ ?[0-9]+\] (\.isr|\.note|\.text|\.bss|\.debug_info|\.debug_line)' | sed 's/^ */   /'
echo "-- objdump -s -j .gnu_debuglink fw.stripped (file name, pad to 4, CRC32 of fw.debug)"
$P-objdump -s -j .gnu_debuglink fw.stripped | grep -E '^ [0-9a-f]{4} ' | sed 's/^/   /'
echo "-- build-id lives in Flash: note section at $($P-readelf -S -W fw.elf | awk '{for (i = 1; i <= NF; i++) if ($i == ".note.gnu.build-id") print "0x" $(i + 2)}'), first bytes of fw.bin after the vector table:"
$P-objcopy -O binary fw.elf fw.bin && xxd -s 64 -l 36 fw.bin | sed 's/^/   /'
echo
echo "== objdump -d -l: sensor_task (bl = 4 bytes, return address = next insn)"
$P-objdump -d -l fw.elf | awk '/^[0-9a-f]+ <sensor_task>:/ {p = 1} /^[0-9a-f]+ <Reset_Handler>:/ {p = 0} p' | short | sed 's/^/   /'
echo
LR=$($P-objdump -d fw.elf | awk '/\tbl\t/ && $NF == "<parse_frame>" {a = $1; sub(/:/, "", a); print a; exit}')
LRV=$(printf '0x%x' $(( (0x$LR + 4) | 1 )))
echo "== a stacked lr of $LRV (bl <parse_frame> at 0x$LR, +4, Thumb bit set) looked up 3 ways"
for a in $LRV $(printf '0x%x' $(( LRV & ~1 ))) $(printf '0x%x' $(( (LRV & ~1) - 1 ))); do
    printf '   addr2line %-10s -> %s\n' "$a" "$($P-addr2line -f -s -e fw.elf $a | tr '\n' ' ')"
done
PC=$($P-objdump -d -l fw.elf | awk '/^\// {c = $1; sub(/.*\//, "", c); next} /^ +[0-9a-f]+:\t/ && c == "fw_m4.c:15" {a = $1; sub(/:/, "", a); print "0x" a; exit}')
echo "== fault pc $PC (inside the inlined rd_be16): -f -i -p, on fw.debug vs fw.stripped"
echo "-- fw.debug:";    $P-addr2line -f -i -p -s -e fw.debug "$PC" | sed 's/^/   /'
echo "-- fw.stripped:"; $P-addr2line -f -i -p -s -e fw.stripped "$PC" | sed 's/^/   /'
mkdir -p elsewhere && mv fw.debug elsewhere/
echo "-- fw.stripped after moving fw.debug away (the debuglink can't be followed):"; $P-addr2line -f -i -p -s -e fw.stripped "$PC" | sed 's/^/   /'
echo "-- fw.stripall:"; $P-addr2line -f -i -p -s -e fw.stripall "$PC" | sed 's/^/   /'
