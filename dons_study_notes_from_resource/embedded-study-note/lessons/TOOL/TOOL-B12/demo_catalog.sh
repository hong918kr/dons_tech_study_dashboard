#!/usr/bin/env bash
# TOOL-B12 demo: every starter case under the DEFAULT UBSan runtime (recover mode),
# then the same binary with a few UBSAN_OPTIONS. Register dumps are trimmed.
#   bash demo_catalog.sh > ubsan_starter.txt
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-B12_demo_catalog; rm -rf "$W"; mkdir -p "$W"
cc -std=c11 -g -O1 -fsanitize=undefined driver.c starter.c -o "$W/ub_recover" || exit 1
trim() { grep -v -E '^ *(x\[|x[0-9]|fp =)|Register values'; }
run() { "$@" >"$W/out" 2>&1; local rc=$?; trim <"$W/out"; echo "[exit $rc]"; }
echo "### build: cc -std=c11 -g -O1 -fsanitize=undefined driver.c starter.c   (default = recover)"
for c in $("$W/ub_recover" --list); do
    echo "=== $c"
    run "$W/ub_recover" "$c" 2>/dev/null
done
echo
echo "### UBSAN_OPTIONS=halt_on_error=1 ./ub_recover adc_mul_ovf"
UBSAN_OPTIONS=halt_on_error=1 run "$W/ub_recover" adc_mul_ovf 2>/dev/null
echo
echo "### UBSAN_OPTIONS=print_stacktrace=1 ./ub_recover cal_null"
UBSAN_OPTIONS=print_stacktrace=1 run "$W/ub_recover" cal_null 2>/dev/null
echo
echo "### UBSAN_OPTIONS=report_error_type=1 ./ub_recover mask_31"
UBSAN_OPTIONS=report_error_type=1 run "$W/ub_recover" mask_31 2>/dev/null
echo
echo "### NO sanitizer: cc -std=c11 -O2 driver.c starter.c   (what the UB actually did)"
cc -std=c11 -O2 driver.c starter.c -o "$W/plain" || exit 1
for c in adc_mul_ovf temp_hot duty_neg pkt_ack mask_32 align18 therm_glitch mode_bad cal_null; do
    echo "=== $c"
    run "$W/plain" "$c" 2>/dev/null
done
echo "--- objdump -d starter.o (-O2): reg_mask / temp_to_centi / duty_to_ccr"
cc -std=c11 -O2 -c starter.c -o "$W/starter.o"
/usr/bin/objdump -d --no-show-raw-insn --disassemble-symbols=_reg_mask,_temp_to_centi,_duty_to_ccr "$W/starter.o" | sed -n '/^0/,$p'
