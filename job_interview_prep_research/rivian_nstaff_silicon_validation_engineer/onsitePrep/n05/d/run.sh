#!/bin/bash
# Build & run all N05D drills (ASan + UBSan). Usage: ./run.sh
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}"
for f in D*.c; do
  b="$OUT/n05d_${f%.c}"
  cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined "$f" -o "$b"
  "$b"
done
