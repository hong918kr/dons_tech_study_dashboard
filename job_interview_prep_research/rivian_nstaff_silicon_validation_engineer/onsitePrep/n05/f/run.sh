#!/bin/bash
# Build & run every N05F problem with the exact spec command (ASan + UBSan, -Werror).
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}"
for src in F*.c; do
  id="${src%%_*}"
  cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined "$src" -o "$OUT/n05f_$id"
  "$OUT/n05f_$id"
done
