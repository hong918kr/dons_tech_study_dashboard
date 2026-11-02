#!/bin/sh
# Build & run all N05G model answers (macOS: default SDK link broken -> explicit -isysroot)
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}"
for f in G*_*.c; do
  id="${f%%_*}"
  cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined "$f" -o "$OUT/n05g_$id"
  "$OUT/n05g_$id"
done
