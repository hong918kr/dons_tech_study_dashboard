#!/bin/bash
# Build & run all N05E problems (macOS: default SDK link broken -> explicit -isysroot)
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}"
for f in E*.c; do
  b="${f%.c}"
  cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -pthread "$f" -o "$OUT/n05e_$b"
  "$OUT/n05e_$b"
done
