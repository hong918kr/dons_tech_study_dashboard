#!/bin/sh
# N07D code: build + run everything here, write run_output.txt
# macOS: default SDK link is broken -> explicit -isysroot
set -e
cd "$(dirname "$0")"
SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
OUT=${TMPDIR:-/tmp}
rc=0
{
  echo "### uart_baud_sim.c  (cases: divisor_cases.txt)"
  cc -isysroot "$SDK" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined uart_baud_sim.c -lm -o "$OUT/n07d_uart_baud_sim"
  "$OUT/n07d_uart_baud_sim" divisor_cases.txt || rc=1
  echo
  [ $rc -eq 0 ] && echo "ALL N07D PROGRAMS PASS" || echo "SOME N07D PROGRAM FAILED"
} 2>&1 | tee run_output.txt
grep -q "ALL N07D PROGRAMS PASS" run_output.txt
