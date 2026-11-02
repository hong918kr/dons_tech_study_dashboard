#!/bin/sh
# Build & run every N05A drill. macOS: default SDK link broken -> explicit -isysroot.
set -e
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}"
for f in A*.c; do
    b="$OUT/n05a_${f%.c}"
    cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined "$f" -o "$b"
    "$b"
done
