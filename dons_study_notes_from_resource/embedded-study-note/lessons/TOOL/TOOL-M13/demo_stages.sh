#!/usr/bin/env bash
# TOOL-M13 demo: the size-diet stage table, measured for real in the Lima VM `elinux`
# (arm-none-eabi-gcc 14.2.1 WITH newlib + newlib-nano; the Mac gcc 16 has no newlib).
# Every stage image is also booted in qemu-system-arm -M mps2-an386 (VM) and its UART output is
# compared with golden_uart.txt — a smaller image that prints something else is not a diet, it is a bug.
#   bash demo_stages.sh   -> writes stages_vm.txt, mapdiff_vm.txt, float_vm.txt, gc_trap_vm.txt, strlen_vm.txt here
set -u
cd "$(dirname "$0")" || exit 2
HERE=$PWD
VW=/tmp/TOOL-M13_work/vm
# Mac home is read-only inside the VM -> copy sources to VM /tmp, build there, print results to stdout
limactl shell elinux -- bash -lc "rm -rf $VW && mkdir -p $VW && cp -r '$HERE/fw' '$HERE/newlib.ld' '$HERE/golden_uart.txt' $VW/" || exit 1

VM_SCRIPT='
set -u
cd '"$VW"'
BASE="-mcpu=cortex-m4 -mthumb -mfloat-abi=soft -std=c11 -Wall -Wextra -Werror -Ifw"
NOSYS="--specs=nosys.specs -nostartfiles"
# build <name> <kind: printf|tiny|bare> <CFLAGS> <LDFLAGS>   (each .c -> its own .o so the map names files)
build() {
    local name=$1 kind=$2 cf=$3 lf=$4 srcs="startup main crc uart sensor" libs="$NOSYS" o
    case $kind in
        printf) cf="$cf -DUSE_PRINTF" ;;
        tiny)   srcs="$srcs fmt" ;;
        bare)   srcs="$srcs fmt libc_min"; cf="$cf -ffreestanding"; libs="-nostdlib -lgcc" ;;
    esac
    rm -rf $name; mkdir -p $name
    case "$lf" in *--gc-sections*) lf="$lf -Wl,--print-gc-sections" ;; esac
    for o in $srcs; do arm-none-eabi-gcc $BASE $cf -c fw/$o.c -o $name/$o.o || { echo "COMPILE FAIL $name $o"; return 1; }; done
    arm-none-eabi-gcc $BASE $cf $lf -T newlib.ld -Wl,-Map=$name/fw.map $(for o in $srcs; do echo $name/$o.o; done) $libs \
        -o $name/fw.elf 2> $name/link.err || { echo "LINK FAIL $name"; head -5 $name/link.err; return 1; }
    timeout 10 qemu-system-arm -M mps2-an386 -nographic -monitor none -semihosting-config enable=on,target=native \
        -serial file:$name/uart.txt -kernel $name/fw.elf > $name/qemu.log 2>&1
    echo $? > $name/qemu.rc
}
row() {   # row <name> <label>
    local n=$1 t d b ok
    read -r t d b _ < <(arm-none-eabi-size $n/fw.elf | awk "NR==2")
    if [ "$(cat $n/qemu.rc)" = 0 ] && cmp -s $n/uart.txt golden_uart.txt; then ok=same; else ok="DIFF(rc=$(cat $n/qemu.rc))"; fi
    printf "%-4s %-44s %6d %5d %5d | %6d %5d | %6d | %s\n" "$n" "$2" $t $d $b $((t+d)) $((d+b)) $(stat -c %s $n/fw.elf) "$ok"
}
F="-ffunction-sections -fdata-sections"
GC="-Wl,--gc-sections"
NANO="--specs=nano.specs"
build S0 printf "-O0 -g"           ""
build S1 printf "-Os -g"           ""
build S2 printf "-Os -g $F"        "$GC"
build S3 printf "-Os -g $F -flto"  "$GC"
build S4 printf "-Os -g $F -flto"  "$GC $NANO"
build S5 tiny   "-Os -g $F -flto"  "$GC $NANO"
build S6 bare   "-Os -g $F -flto"  "$GC"
build T1 printf "-Os -g"           "$GC"
build T2 tiny   "-Os -g"           "$GC $NANO"
build T3 tiny   "-Os -g $F"        "$GC $NANO"
build F1 printf "-Os -g $F -flto -DLOG_FLOAT" "$GC $NANO"
build F2 printf "-Os -g $F -flto -DLOG_FLOAT" "$GC $NANO -u _printf_float"
build F3 printf "-Os -g $F -flto -DLOG_FLOAT" "$GC"

{
echo "# VM elinux: arm-none-eabi-gcc $(arm-none-eabi-gcc -dumpversion), $(qemu-system-arm --version | head -1 | sed "s/ (.*//")"
echo "# base: $BASE, newlib.ld (FLASH 64K @0x0 / RAM 16K @0x20000000), printf stages: $NOSYS"
echo "# Flash = text+data, RAM = data+bss (no heap/stack reservation in newlib.ld); elf = file size on disk"
printf "%-4s %-44s %6s %5s %5s | %6s %5s | %6s | %s\n" stg "what changed" text data bss Flash RAM elf "QEMU output vs golden"
row S0 "-O0 -g, printf, full newlib"
row S1 "-Os"
row S2 "+ -ffunction/-fdata-sections + --gc-sections"
row S3 "+ -flto"
row S4 "+ --specs=nano.specs (newlib-nano)"
row S5 "printf -> fmt.c tiny formatter"
row S6 "-nostdlib + libc_min.c (no newlib at all)"
echo "# traps / side rows"
row T1 "S1 + --gc-sections only (no -ffunction-sec.)"
row T2 "S5 without LTO and without -ffunction-sec."
row T3 "S5 without LTO (= Mac solution.flags)"
row F1 "S4 + LOG_FLOAT (%.1f), nano, no -u"
row F2 "S4 + LOG_FLOAT + -u _printf_float"
row F3 "S3 + LOG_FLOAT (full newlib)"
} > stages_vm.txt

{
for n in F1 F2 F3; do echo "--- $n: temp line on UART (golden: temp=2.4C)"; grep "^temp" $n/uart.txt; done
echo "--- S0 link warnings from nosys.specs (first 2 lines, paths shortened)"
head -2 S0/link.err | sed "s#^.*/bin/ld: #ld: #; s#^/build/newlib-[^/]*/#.../#"
echo "--- largest symbols, S3 (full newlib printf) vs S4 (nano)"
for n in S3 S4; do echo "[$n] arm-none-eabi-nm -S --size-sort fw.elf | grep -i \" [tT] \" | tail -6"; arm-none-eabi-nm -S --size-sort $n/fw.elf | grep -i " [tT] " | tail -6; done
} > float_vm.txt

# map-file diff: bytes per input file in .text/.rodata/.data (Flash) from the GNU ld map
# (handles the 2-line form where a long input-section name sits alone on its line; a real
#  analyzer for these maps is TOOL-M02 — this is the 15-line awk version)
mapsum() {
    awk "
    /^Linker script and memory map/ {on=1; next}
    !on {next}
    /^\\.(text|rodata|data|isr_vector|ARM\\.exidx|ARM\\.extab|init_array|fini_array)([ \\t]|\$)/ {sec=\$1}
    /^\\.(bss|comment|debug|ARM\\.attributes)/ {sec=\"\"}
    sec==\"\" {next}
    /^ \\.[^ ]+\$/ {pend=\$1; next}
    {
        if (pend != \"\") { \$0 = \" \" pend \" \" \$0; pend=\"\" }
        if (substr(\$0, 1, 1) == \" \" && \$1 ~ /^\\./ && \$2 ~ /^0x/ && \$3 ~ /^0x/ && NF >= 4) {
            f=\$4; sub(/.*\\//, \"\", f); sub(/^cc[A-Za-z0-9]+\\.ltrans[0-9]*\\.ltrans\\.o\$/, \"<lto>.o\", f)
            sum[f] += strtonum(\$3)
        }
    }
    END { for (f in sum) if (sum[f] > 0) printf \"%s %d\\n\", f, sum[f] }" "$1" | sort
}
mapdiff() {   # mapdiff A B
    join -a1 -a2 -e 0 -o 0,1.2,2.2 <(mapsum $1/fw.map) <(mapsum $2/fw.map) |
        awk "{print \$3-\$2, \$1, \$2, \$3}" | sort -n | awk "{printf \"%-38s %6d %6d %+7d\\n\", \$2, \$3, \$4, \$1}"
}
{
echo "# Flash bytes per input file (from fw.map), sorted by change. columns: file  before  after  delta"
echo "=== S1 (-Os) -> S2 (+ -ffunction-sections -fdata-sections --gc-sections)"
mapdiff S1 S2 | head -8; echo "..."; mapdiff S1 S2 | tail -3
echo "=== S3 (full newlib) -> S4 (newlib-nano), libc members only, biggest changes"
mapdiff S3 S4 | grep "lib" | head -6; echo "..."; mapdiff S3 S4 | grep "lib" | tail -6
echo "=== S2 fw.map: Discarded input sections from our own objects (first 8)"
sed -n "/^Discarded input sections/,/^Memory Configuration/p" S2/fw.map | grep -A1 -E "^ \\.(text|rodata|data|bss)\\." | grep -vE "^--" | paste - - | grep -E "S2/(crc|main|uart|sensor|startup)\\.o" | awk "{print \$1, \$3, \$4}" | sed "s#S2/##" | head -8
} > mapdiff_vm.txt

# the gc-sections trap at object level: one .text per file vs one section per function
arm-none-eabi-gcc $BASE -Os -c fw/crc.c -o crc_plain.o; arm-none-eabi-gcc $BASE -Os $F -c fw/crc.c -o crc_fs.o
{ echo "\$ arm-none-eabi-readelf -SW crc.o | grep -E \" \\.(text|rodata)\"    # -Os"
  arm-none-eabi-readelf -SW crc_plain.o | grep -E " \.(text|rodata)" | sed -E "s/^ +\[ ?[0-9]+\] //" | awk "{print \$1, \$2, \$3, \$4, \$5, \$6}"
  echo "\$ arm-none-eabi-readelf -SW crc.o | grep -E \" \\.(text|rodata)\"    # -Os -ffunction-sections -fdata-sections"
  arm-none-eabi-readelf -SW crc_fs.o | grep -E " \.(text|rodata)" | sed -E "s/^ +\[ ?[0-9]+\] //" | awk "{print \$1, \$2, \$3, \$4, \$5, \$6}"
  for n in T1 S2; do
    echo "\$ $n link: --print-gc-sections, removed .text*/.rodata*/.data*/.bss* sections by origin"
    grep "removing unused section" $n/link.err | grep -E "section .\.(text|rodata|data|bss)" |
      sed -E "s#.* in file .##; s#.\$##; s#.*/(lib[a-z_]+\.a)\(.*#\1#; s#.*/##" | sort | uniq -c | sort -rn | head -6
  done
} > gc_trap_vm.txt

# S5 -> S6: which newlib member does the tiny-formatter build still pull in, and why?
{ echo "--- S5 fw.map: input sections that come from libraries (section, size, member)"
  grep -E "^ \.(text|rodata|data)" S5/fw.map | grep -E "lib(c|g)_nano|libgcc|libnosys" | awk "{n = split(\$4, a, \"/\"); print \$1, \$3, a[n]}"
  arm-none-eabi-gcc $BASE -Os -c fw/fmt.c -o fmt_hosted.o; arm-none-eabi-gcc $BASE -Os -ffreestanding -c fw/fmt.c -o fmt_free.o
  echo "\$ arm-none-eabi-nm fmt.o    # -Os (hosted)"; arm-none-eabi-nm fmt_hosted.o
  echo "\$ arm-none-eabi-nm fmt.o    # -Os -ffreestanding"; arm-none-eabi-nm fmt_free.o
} > strlen_vm.txt

for f in stages_vm.txt float_vm.txt mapdiff_vm.txt gc_trap_vm.txt strlen_vm.txt; do echo "@@FILE $f"; cat "$f"; done
'
OUT=/tmp/TOOL-M13_work/vm_out.txt
mkdir -p /tmp/TOOL-M13_work
limactl shell elinux -- bash -lc "$VM_SCRIPT" >"$OUT" 2>&1
grep -q '^@@FILE' "$OUT" || { echo "VM part failed:"; cat "$OUT"; exit 1; }
grep -E '^(COMPILE|LINK) FAIL' -A5 "$OUT"
awk -v dir="$HERE" '/^@@FILE /{f=dir "/" $2; printf "" > f; next} f{print >> f}' "$OUT"
cat stages_vm.txt
