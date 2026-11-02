#!/bin/sh
# Build & run all N05C problems (macOS: default SDK link broken -> explicit -isysroot).
cd "$(dirname "$0")" || exit 1
fail=0
for f in C*.c; do
  id=${f%%_*}
  cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined "$f" -o "${TMPDIR:-/tmp}/n05c_$id" && "${TMPDIR:-/tmp}/n05c_$id" || { echo "$id FAIL"; fail=1; }
done
exit $fail
