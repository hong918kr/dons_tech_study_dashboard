#!/bin/sh
# N07C: SPI mode-mismatch (16 combos) + flash JEDEC model + sample-delay shmoo.
# Builds with ASan/UBSan, runs, writes run_output.txt next to this script.
cd "$(dirname "$0")" || exit 1
OUT="${TMPDIR:-/tmp}"
SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
if [ -d "$SDK" ]; then
  cc -isysroot "$SDK" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined spi_mode_sim.c -o "$OUT/n07c_spi_mode_sim" || exit 1
else
  cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined spi_mode_sim.c -o "$OUT/n07c_spi_mode_sim" || exit 1
fi
"$OUT/n07c_spi_mode_sim" | tee run_output.txt
rc=$?
echo "exit=$rc"
exit $rc
