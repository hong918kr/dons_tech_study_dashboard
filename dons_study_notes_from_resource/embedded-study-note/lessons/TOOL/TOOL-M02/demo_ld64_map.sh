#!/usr/bin/env bash
# TOOL-M02 demo (not graded): the same C modules linked for the Mac host with Apple ld
# (-Wl,-map) to show that ld64's map is a different format from GNU ld -Map.
#   bash demo_ld64_map.sh      -> prints excerpts (captured in ld64_map_sample.txt)
set -u
cd "$(dirname "$0")" || exit 2
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
FW=$(pwd)/fw
W=/tmp/TOOL-M02_work/ld64
rm -rf "$W"
mkdir -p "$W"
cd "$W" || exit 1
cat >host_main.c <<'C'
#include "fw.h"
uint8_t g_rx_buf[RX_BUF_SIZE];
int main(void)
{
	char s[12];
	fmt_u32(s, (uint32_t)sensor_filter_update(42));
	return crc16_ccitt((const uint8_t *)s, 2) == 0;
}
C
for f in "$FW/sensor.c" "$FW/util/crc16.c" "$FW/util/fmt.c" host_main.c; do
	cc -std=c11 -Os -I"$FW" -c "$f" -o "$(basename "${f%.c}").o" || exit 1
done
cc sensor.o crc16.o fmt.o host_main.o -Wl,-dead_strip -Wl,-map,mac.map -o host_demo || exit 1
ld -v 2>&1 | head -1
echo "--- mac.map: $(wc -l <mac.map | tr -d ' ') lines; section headers:"
grep -n '^# ' mac.map
echo "--- # Object files: (index -> path)"
sed -n '/^# Object files:/,/^# Sections:/p' mac.map | sed 's#/[^ ]*/TOOL-M02/fw/#.../fw/#; s#/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk#$SDK#'
echo "--- # Sections:"
sed -n '/^# Sections:/,/^# Symbols:/p' mac.map | head -14
echo "--- # Symbols: (first 14)"
sed -n '/^# Symbols:/,$p' mac.map | head -16
echo "--- # Dead Stripped Symbols:"
sed -n '/^# Dead Stripped Symbols:/,$p' mac.map | head -8
