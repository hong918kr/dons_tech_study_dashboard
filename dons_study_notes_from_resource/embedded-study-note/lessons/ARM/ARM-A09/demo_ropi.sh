#!/usr/bin/env bash
# ARM-A09 demo_ropi.sh (맥) — bash demo_ropi.sh > demo_ropi.txt
set -u
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
SRC=$(cd "$(dirname "$0")" && pwd); W=/tmp/ARM-A09_work/ropi; mkdir -p "$W" && cd "$W" || exit 2
for f in "" "-fropi -frwpi"; do
  echo "--- clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -Os -ffreestanding $f"
  # shellcheck disable=SC2086
  clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -Os -ffreestanding $f -c "$SRC/demo_ropi.c" -o r.o &&
    /usr/bin/objdump -d -r r.o | sed -n '/<bump>:/,$p'
done
