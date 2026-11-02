#!/bin/sh
# N07B: regenerate mock captures, build the decoder with ASan/UBSan, run all cases -> run_output.txt
cd "$(dirname "$0")" || exit 1
SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
OUT="${TMPDIR:-/tmp}/n07b_i2c_capture_decode"
{
  echo "### python3 -I gen_captures.py"
  python3 -I gen_captures.py || exit 1
  echo "### cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined i2c_capture_decode.c && run"
  cc -isysroot $SDK -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined i2c_capture_decode.c -o "$OUT" || exit 1
  "$OUT" cap_golden.txt cap_b3_slow_rise.txt cap_b4_stop_between.txt cap_b5_glitch_arb.txt cap_b7_stretch_timeout.txt
  echo "exit=$?"
} > run_output.txt 2>&1
cat run_output.txt
