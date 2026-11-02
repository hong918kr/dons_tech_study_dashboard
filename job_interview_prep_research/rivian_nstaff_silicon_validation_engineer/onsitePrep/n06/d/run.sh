#!/bin/sh
# N06D code: build + run everything here (macOS: default SDK link is broken -> explicit -isysroot)
set -e
cd "$(dirname "$0")"
SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
OUT=${TMPDIR:-/tmp}
for p in dma_matrix secded_inject; do
  if [ -d "$SDK" ]; then
    cc -isysroot "$SDK" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $p.c -o "$OUT/$p"
  else
    cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $p.c -o "$OUT/$p"
  fi
  echo "### $p"
  "$OUT/$p" | tee $p.out
done
