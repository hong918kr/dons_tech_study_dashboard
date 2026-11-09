#!/bin/bash
# check.sh <starter|solution|practice> [check|measure|splat] [K]
# Runs inside the Lima VM. Builds the module against 6.18.55-dbg, boots QEMU TCG
# (cortex-a57 -smp 4), and decides PASS/FAIL from the guest console.
# Timing never decides PASS/FAIL. exit 0 PASS, 1 FAIL.
set -u
MOD=${1:?usage: check.sh starter|solution|practice [check|measure|splat] [K]}
MODE=${2:-check}
K=${3:-40}
ID=ELX-M06
KDIR=/var/lab/out/linux-build-dbg
SRC=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
W=/tmp/${ID}_work/${MOD}_${MODE}
TMO=150
[ "$MODE" = measure ] && TMO=540
[ "$MODE" = splat ] && TMO=120

rm -rf "$W"; mkdir -p "$W/m"
cp "$SRC/$MOD.c" "$SRC/Makefile" "$W/m/"
cp "$SRC/fire.c" "$SRC/guest.sh" "$SRC/parse_trace.awk" "$W/"
cd "$W/m" && make -C "$KDIR" M="$W/m" MOD="$MOD" modules > "$W/build.log" 2>&1
if [ $? -ne 0 ] || [ ! -f "$W/m/$MOD.ko" ]; then
	echo "BUILD FAILED"; tail -20 "$W/build.log"; exit 1
fi
cd "$W" && gcc -static -std=c11 -Wall -Wextra -Werror -O2 fire.c -o fire 2>>"$W/build.log" || { echo "fire build failed"; exit 1; }
mkdir r && cp -a /var/lab/build/rootfs/. r/ && cp "m/$MOD.ko" fire guest.sh parse_trace.awk r/ && chmod +x r/guest.sh r/fire
(cd r && find . | cpio -o -H newc --quiet | gzip -1) > i.gz

LOG=quiet
[ "$MODE" = splat ] && LOG="loglevel=7 panic=1"
timeout "$TMO" qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 4 -m 512M -nographic -no-reboot \
	-kernel /var/lab/out/Image-dbg -initrd i.gz \
	-append "console=ttyAMA0 $LOG LAB_CMD=/guest.sh~$MOD~$MODE~$K" > "$W/guest.log" 2>&1
QRC=$?
if [ "$MODE" != check ]; then
	echo "qemu exit=$QRC  log: $W/guest.log"
	cat "$W/guest.log"
	exit 0
fi

grep -E '^(CHECK|  [a-z_]+: n=|  /proc/interrupts|FAILS=|GUEST-END)|^(hard|tasklet|wq_sys|wq_hi|threaded|wq_bh) .*(irqs|n)=' "$W/guest.log"
if ! grep -q GUEST-END "$W/guest.log"; then
	echo "VERDICT: FAIL (no GUEST-END, qemu rc=$QRC)"
	tail -30 "$W/guest.log"
	exit 1
fi
P=$(grep -c '^CHECK .* PASS' "$W/guest.log")
F=$(grep -c '^CHECK .* FAIL' "$W/guest.log")
if [ "$F" -eq 0 ] && [ "$P" -gt 0 ] && grep -q '^FAILS=0' "$W/guest.log"; then
	echo "VERDICT: PASS ($P/$((P + F)) checks)"
	exit 0
fi
echo "VERDICT: FAIL ($P passed, $F failed)"
exit 1
