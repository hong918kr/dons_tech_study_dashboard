#!/bin/bash
# Build & run all N05B drills. macOS: default SDK link is broken -> explicit -isysroot.
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}"
for f in B*.c; do
    id="${f%%_*}"
    cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined "$f" -o "$OUT/n05b_$id"
    "$OUT/n05b_$id"
done
