#!/bin/sh
# Build & run every N05I problem (macOS: default SDK link is broken -> -isysroot).
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}"
for f in I*_*.c; do
  b="$OUT/n05i_${f%.c}"
  cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined "$f" -o "$b"
  "$b"
done
