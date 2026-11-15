#!/usr/bin/env bash
# TOOL-B12 demo: trap mode on the Mac (arm64). No UBSan runtime at all:
# each check becomes "branch to brk #0x55NN" -> SIGTRAP (shell rc 133).
#   bash demo_trap.sh > ubsan_trap.txt
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-B12_demo_trap; rm -rf "$W"; mkdir -p "$W"
cc -std=c11 -g -O2 -fsanitize=undefined -fsanitize-trap=undefined driver.c starter.c -o "$W/trap" || exit 1
echo "### cc -std=c11 -g -O2 -fsanitize=undefined -fsanitize-trap=undefined driver.c starter.c"
for c in adc_ok adc_mul_ovf adc_div0 accum_sat mask_32 pkt_seq duty_over; do
    rc=$({ "$W/trap" "$c" >"$W/out" 2>&1; echo $?; } 2>/dev/null)
    printf '%-12s exit %-3s %s\n' "$c" "$rc" "$(head -1 "$W/out")"
done
echo
echo "### is the runtime linked?  otool -L trap | grep -c ubsan"
otool -L "$W/trap" | grep -c ubsan
echo
echo "### objdump -d: the checks and their brk codes"
/usr/bin/objdump -d --no-show-raw-insn --disassemble-symbols=_adc_scale,_accum_add,_reg_mask,_sample_mag "$W/trap" \
    | sed -n '/^0/,$p'
echo "### all distinct brk immediates in the binary"
/usr/bin/objdump -d "$W/trap" | grep -o 'brk.*#0x55[0-9a-f]*' | sort | uniq -c
echo
echo "### lldb -b -s demo_trap.lldb -- trap adc_mul_ovf"
lldb -b -s demo_trap.lldb -- "$W/trap" adc_mul_ovf 2>&1 | sed "s#$W/##g"
