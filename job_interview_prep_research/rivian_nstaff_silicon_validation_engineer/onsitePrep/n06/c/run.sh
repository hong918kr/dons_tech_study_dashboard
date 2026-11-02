#!/bin/sh
# N06C code: build + run everything here (macOS: default SDK link is broken -> explicit -isysroot)
set -e
cd "$(dirname "$0")"
SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
OUT=${TMPDIR:-/tmp}
rc=0
for src in i2c_timing_check.c i2c_ctrl_fault_model.c gic_model.c; do
  bin="$OUT/n06c_${src%.c}"
  echo "### $src"
  cc -isysroot "$SDK" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined "$src" -lm -o "$bin"
  "$bin" || rc=1
  echo
done
[ $rc -eq 0 ] && echo "ALL N06C PROGRAMS PASS" || echo "SOME N06C PROGRAM FAILED"
exit $rc
