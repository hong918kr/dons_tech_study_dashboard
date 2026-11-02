#!/bin/bash
# N07E: build the DDR failure-log decoder (ASan+UBSan) and run it on the 6 mock logs.
# Logs are checked in; regenerate only if you change the fault model: python3 -I gen_mocks.py
set -euo pipefail
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/n07e_ddr_fail_decode"
cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror \
   -fsanitize=address,undefined ddr_fail_decode.c -o "$OUT"
"$OUT" logs/M0_clean.log logs/M1_dq13_stuck.log logs/M2_lane2_intermittent.log \
       logs/M3_upper_half_alias.log logs/M4_hot_retention.log logs/M5_same_addr_ce.log | tee run_output.txt
