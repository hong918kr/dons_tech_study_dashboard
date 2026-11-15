#!/usr/bin/env bash
# TOOL-B12 demo, Linux side: run from the Mac, executes inside the Lima VM `elinux`.
#   bash demo_vm.sh > ubsan_vm.txt
# Mac home is read-only in the VM -> sources are copied to VM /tmp/TOOL-B12_work/vm first.
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
limactl shell elinux -- bash -lc "SRC='$SRC' bash -s" <<'VM'
set -u
W=/tmp/TOOL-B12_work/vm; rm -rf "$W"; mkdir -p "$W"
cp "$SRC"/{fwub.h,driver.c,starter.c,solution.c,demo_minrt.c,demo_lang.c} "$W/"
cd "$W"
# q: run quietly (bash's "Segmentation fault" notice goes to /dev/null), print output + exit code
q() { local rc; rc=$({ "$@" >out.txt 2>&1; echo $?; } 2>/dev/null); grep -v '^SUMMARY' out.txt; echo "[exit $rc]"; }

echo "### 1. GCC $(gcc -dumpfullversion) -fsanitize=undefined (recover) on starter cases"
gcc -std=c11 -g -O1 -fsanitize=undefined driver.c starter.c -o ub_gcc -lm
for c in adc_mul_ovf mask_31 align18 pkt_seq pkt_ack mode_bad cal_null temp_hot duty_neg; do
    echo "=== $c"; q ./ub_gcc $c
done
echo
echo "### 2. GCC: float-cast-overflow is NOT in -fsanitize=undefined; add it explicitly"
gcc -std=c11 -g -O1 -fsanitize=undefined,float-cast-overflow driver.c starter.c -o ub_gcc_fc -lm
for c in temp_hot duty_neg; do echo "=== $c"; q ./ub_gcc_fc $c; done
echo
echo "### 3. -fno-sanitize-recover=all exit code on Linux (gcc, clang)"
gcc   -std=c11 -g -O1 -fsanitize=undefined -fno-sanitize-recover=all driver.c starter.c -o ub_gcc_h -lm
clang -std=c11 -g -O1 -fsanitize=undefined -fno-sanitize-recover=all driver.c starter.c -o ub_clang_h -lm
echo "=== gcc   accum_sat"; q ./ub_gcc_h accum_sat
echo "=== clang accum_sat"; q ./ub_clang_h accum_sat
echo
echo "### 4. demo_lang.c with gcc as C and C++ (enum load check)"
gcc -std=c11 -g -O1 -fsanitize=undefined demo_lang.c -o dl_gc
g++ -x c++ -std=c++17 -g -O1 -fsanitize=undefined demo_lang.c -o dl_gxx
echo "=== gcc C11";  q ./dl_gc
echo "=== g++ C++17"; q ./dl_gxx
echo
echo "### 5. clang $(clang -dumpversion) minimal runtime (libclang_rt.ubsan_minimal), trap mode"
clang -std=c11 -g -O1 -fsanitize=undefined -fsanitize-minimal-runtime driver.c starter.c -o ub_min -lm
for c in adc_mul_ovf mask_32 pkt_seq; do echo "=== minimal $c"; q ./ub_min $c; done
clang -std=c11 -g -O2 -fsanitize=undefined -fsanitize-trap=undefined driver.c starter.c -o ub_trap -lm
echo "=== trap adc_mul_ovf"; q ./ub_trap adc_mul_ovf
echo
echo "### 6. own minimal handlers + addr2line (no-pie so the PC is a link address)"
clang -std=c11 -g -O1 -no-pie -fno-pie -fsanitize=undefined -fsanitize-minimal-runtime \
      -fno-sanitize-link-runtime demo_minrt.c starter.c -o dm
./dm > dm.txt; echo "[exit $?]"; cat dm.txt
echo "(return address - 4 = the bl that called the handler)"
for pc in $(sed -n 's/.*pc=\(0x[0-9a-f]*\).*/\1/p' dm.txt); do
    printf '%s -> ' "$pc"; addr2line -f -i -e dm $(printf '0x%x' $((pc - 4))) | paste -sd' '
done
echo
echo "### 7. arm-none-eabi-gcc $(arm-none-eabi-gcc -dumpversion): Cortex-M4 object size, -Os"
M4="-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -ffreestanding -Os -std=c11 -c"
for src in solution starter; do
    arm-none-eabi-gcc $M4 $src.c -o $src-none.o
    arm-none-eabi-gcc $M4 -fsanitize=undefined -fsanitize-undefined-trap-on-error $src.c -o $src-trap.o
    arm-none-eabi-gcc $M4 -fsanitize=undefined -fsanitize-trap=undefined $src.c -o $src-trap2.o 2>trap2.err \
        || { echo "(-fsanitize-trap=undefined not accepted:)"; head -2 trap2.err; }
    arm-none-eabi-gcc $M4 -fsanitize=undefined $src.c -o $src-full.o
    echo "--- $src.c   (arm-none-eabi-size, Berkeley)"
    arm-none-eabi-size $src-none.o $src-trap.o $( [ -f $src-trap2.o ] && echo $src-trap2.o ) $src-full.o
    echo "    traps (udf) in $src-trap.o: $(arm-none-eabi-objdump -d $src-trap.o | grep -c -E '\sudf\s')"
done
echo "--- arm-none-eabi-objdump -d starter-trap.o, adc_scale"
arm-none-eabi-objdump -d --no-show-raw-insn starter-trap.o | sed -n '/<adc_scale>:/,/^$/p'
echo "--- undefined handler symbols in solution-full.o (need libubsan: none for bare metal)"
arm-none-eabi-nm -u solution-full.o
VM
