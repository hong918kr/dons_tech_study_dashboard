#!/usr/bin/env bash
# TOOL-B12 demo: what -fsanitize=undefined expands to, and C vs C++ differences.
#   bash demo_groups.sh > ubsan_groups.txt
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-B12_demo_groups; rm -rf "$W"; mkdir -p "$W"
echo "### -fsanitize=undefined expands to (clang -### , cc1 line)"
for t in "" "--target=thumbv7em-none-eabi -mcpu=cortex-m4"; do
    echo "--- target: ${t:-host arm64-apple-darwin}"
    # shellcheck disable=SC2086
    clang $t -fsanitize=undefined -### -c demo_lang.c -o "$W/x.o" 2>&1 \
        | tr ' ' '\n' | grep -E '^"-fsanitize=' | tr -d '"' | sed 's/,/, /g'
done
echo
echo "### same file as C11 / C++17 / C++20 (wire=7, n=31)"
cc -std=c11 -g -O1 -fsanitize=undefined demo_lang.c -o "$W/dl_c"
c++ -x c++ -std=c++17 -g -O1 -fsanitize=undefined demo_lang.c -o "$W/dl_17"
c++ -x c++ -std=c++20 -g -O1 -fsanitize=undefined demo_lang.c -o "$W/dl_20"
for b in dl_c dl_17 dl_20; do echo "--- $b"; "$W/$b" 2>&1 | grep -v '^SUMMARY'; done
echo "--- dl_20 with n=32"
"$W/dl_20" 7 32 2>&1 | grep -v '^SUMMARY'
echo "--- dl_17 with wire=3 (not an enumerator, but inside the 2-bit value range 0..3)"
"$W/dl_17" 3 1 2>&1 | grep -v '^SUMMARY'
echo "--- dl_17 with wire=4"
"$W/dl_17" 4 1 2>&1 | grep -v '^SUMMARY'
