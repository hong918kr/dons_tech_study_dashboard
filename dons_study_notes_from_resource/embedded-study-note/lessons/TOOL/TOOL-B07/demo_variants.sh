#!/usr/bin/env bash
# TOOL-B07 demo: what -E / -dM show for two legal variants of the SOLUTION header.
#   bash demo_variants.sh > variants.txt
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
W=/tmp/TOOL-B07_work/variants
rm -rf "$W" && mkdir -p "$W" && cd "$W" || exit 1
cp "$SRC/solution.h" board_config.h && cp "$SRC/app.c" .
VA="-DBOARD_A -DBUILD_DEBUG"
VB="-DBOARD_B -DBUILD_RELEASE -DFEATURE_USB=1"
OURS='^#define (BOARD_NAME|BUILD_NAME|LED_PIN|UART_BAUD|LOG_LEVEL|HAS_USB|HAS_SHELL|RX_BUF_SIZE|FEATURE_USB|FEATURE_SHELL|ASSERT|BOARD_[AB]|BUILD_(DEBUG|RELEASE))[ (]'

echo "## -E -dM line counts (all macros incl. <stdio.h>, <stdint.h> and predefined ones)"
for v in "$VA" "$VB"; do
	printf '%-45s %5d\n' "$v" "$(cc -std=c11 $v -E -dM app.c | wc -l)"
done
echo
for v in "$VA" "$VB"; do
	echo "## \$ cc -std=c11 $v -E -dM app.c | grep -E '<our macros>' | sort"
	cc -std=c11 $v -E -dM app.c | grep -E "$OURS" | sort
	echo
done
echo "## -E -P sizes (lines)"
for v in "$VA" "$VB"; do
	printf '%-45s %5d\n' "$v" "$(cc -std=c11 $v -E -P app.c | wc -l)"
done
echo
echo "## tail of the two variants (from the rx_idx_t typedef on) after preprocessing: diff (A-debug  vs  B-release+USB)"
cc -std=c11 $VA -E -P app.c | sed -n '/ rx_idx_t;$/,$p' >a.i
cc -std=c11 $VB -E -P app.c | sed -n '/ rx_idx_t;$/,$p' >b.i
diff a.i b.i
echo "[diff rc=$?]"
echo
echo "## object size of the two variants on the Mac (arm64 Mach-O, -Os -c)"
for v in "$VA" "-DBOARD_A -DBUILD_RELEASE" "$VB"; do
	cc -std=c11 -Os $v -c app.c -o v.o
	printf '%-45s %s\n' "$v" "$(/Library/Developer/CommandLineTools/usr/bin/llvm-size -A v.o | grep -E '^__(text|cstring|bss) ' | awk '{printf "%s=%s ", $1, $2}')"
done
