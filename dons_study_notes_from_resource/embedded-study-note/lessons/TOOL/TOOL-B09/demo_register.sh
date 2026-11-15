#!/usr/bin/env bash
# TOOL-B09 demo (practice): self-registering tests in a .a are silently dropped.
#   bash demo_register.sh > register.txt 2>&1
set -u
LESSON=$(cd "$(dirname "$0")" && pwd)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
D=/tmp/TOOL-B09_demo_register; rm -rf "$D"; mkdir -p "$D"; cd "$D" || exit 1
for f in "$LESSON"/src/p/*.c; do cc -O2 -c "$f"; done
echo '##### Mac: ar rcs libtests.a (members have no GLOBAL symbols)'
ar rcs libtests.a test_uart.o test_crc.o test_ring.o
nm test_uart.o
for line in "main_p.o -L. -ltests" "main_p.o -Wl,-force_load,./libtests.a" "main_p.o -Wl,-all_load -L. -ltests" "main_p.o -L. -Wl,-u,_reg_uart -ltests"; do
	echo "== cc $line"
	# shellcheck disable=SC2086
	cc $line -o app 2>&1 && ./app; echo "rc=$?"; rm -f app
done
echo '##### Lima VM'
limactl shell elinux -- bash -s <<VM
set -u
D=/tmp/TOOL-B09_demo_register; rm -rf \$D; mkdir -p \$D; cd \$D
for f in $LESSON/src/p/*.c; do gcc -O2 -c \$f; done
ar rcs libtests.a test_uart.o test_crc.o test_ring.o
nm -s libtests.a | sed -n '1,3p'
for ld in bfd lld; do
	for line in "main_p.o -L. -ltests" "main_p.o -L. -Wl,--whole-archive -ltests -Wl,--no-whole-archive" "main_p.o -L. -Wl,--whole-archive -ltests"; do
		echo "== [\$ld] gcc \$line"
		gcc -fuse-ld=\$ld \$line -o app 2>&1 | head -3; [ -x app ] && ./app; echo "rc=\$?"; rm -f app
	done
done
VM
