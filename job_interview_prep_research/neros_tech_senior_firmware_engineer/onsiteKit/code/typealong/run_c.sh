#!/bin/sh
# build each tN_*.c with warnings as errors + sanitizers, then run it
cd "$(dirname "$0")" || exit 1
mkdir -p .bin
fail=0
for f in t*.c; do
    cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -g "$f" -o ".bin/${f%.c}" -lm \
        && ".bin/${f%.c}" || { echo "FAIL $f"; fail=1; }
done
exit $fail
