#!/usr/bin/env bash
# ENV: linux (builds in the Lima VM, runs the module only inside a QEMU guest)
# ELX-B07: build kecho.ko + kecho_test, boot Image-dbg in QEMU, run, decide.
#   check.sh <starter|solution|practice>   -> PASS/FAIL lines, N/M passed, exit 0/1
#   check.sh demo <spin|nolock|direct|write0|noppos|leak>  -> run one broken variant, print the log
# Never insmod anything into the VM's own kernel.
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
KDIR=/var/lab/out/linux-build-dbg
IMAGE=/var/lab/out/Image-dbg
ROOTFS=/var/lab/build/rootfs
ROOT_WORK=/tmp/ELX-B07_work

ARG=${1:-}
SCEN=${2:-}
case "$ARG" in
starter|solution|practice) VARIANT=$ARG; TMO=170 ;;
demo) VARIANT=demo; TMO=90 ;;
*) echo "usage: check.sh <starter|solution|practice> | demo <scenario>"; exit 2 ;;
esac

W=$ROOT_WORK/$ARG${SCEN:+_$SCEN}.$$
QPID=
cleanup() { [ -n "$QPID" ] && kill "$QPID" 2>/dev/null; }
trap cleanup EXIT
mkdir -p "$W/build" "$W/rootfs"
cp "$SRC"/{starter.c,solution.c,practice.c,demo_bugs.c,kecho_test.c,guest_test.sh,Makefile} "$W/build/"

(
	cd "$W/build" || exit 1
	make -s KDIR=$KDIR VARIANT=$VARIANT all >build.log 2>&1 || { cat build.log; exit 1; }
	gcc -std=gnu11 -Wall -Wextra -Werror -O2 -static kecho_test.c -o kecho_test \
		>>build.log 2>&1 || { cat build.log; exit 1; }
) || { echo "FAIL: build"; echo "0/1 passed"; echo "RESULT: FAIL ($ARG, build)"; exit 1; }

cp -a "$ROOTFS"/. "$W/rootfs/"
cp "$W/build/kecho_test" "$W/build/guest_test.sh" "$W/rootfs/"
if [ "$ARG" = demo ]; then
	cp "$W/build/demo_bugs.ko" "$W/rootfs/"
	B='insmod /demo_bugs.ko bug'
	case "$SCEN" in
	spin)   P="$B=1; printf hello > /dev/kecho; cat /dev/kecho; echo" ;;
	nolock) P="$B=2; for i in 1 2 3; do /kecho_test stress 300; done" ;;
	direct) P="$B=3; /kecho_test base 2>&1 | grep -E 'FAIL|B3|B4|passed'" ;;
	write0) P="$B=4; dd if=/dev/zero of=/dev/kecho bs=4096 count=1 2>&1; echo dd 4096: rc=\$?;
		timeout 5 dd if=/dev/zero of=/dev/kecho bs=1024 count=5 2>&1; echo dd 5x1024 under timeout 5: rc=\$?" ;;
	noppos) P="$B=5; printf abc > /dev/kecho; head -c 20 /dev/kecho; echo;
		timeout 3 cat /dev/kecho | wc -c; echo cat rc=\$?" ;;
	leak)   P="$B=6; rmmod demo_bugs; ls /sys/class/ | grep kecho; $B=0; echo insmod rc=\$?" ;;
	*) echo "unknown scenario '$SCEN'"; exit 2 ;;
	esac
	printf '#!/bin/sh\n%s\necho ===DMESG===\ndmesg | sed -n "/demo bug/,\\$p" | head -70\n' \
		"$P" > "$W/rootfs/test.sh"
	CMD=/test.sh
else
	cp "$W/build/kecho.ko" "$W/rootfs/"
	MODE=base; [ "$ARG" = practice ] && MODE=practice
	CMD="sh~/guest_test.sh~$MODE"
fi
chmod +x "$W/rootfs/"*.sh
(cd "$W/rootfs" && find . | cpio -o -H newc --quiet | gzip -1) > "$W/i.gz"

LOG=$W/console.log
timeout "$TMO" qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 512M \
	-nographic -no-reboot -kernel "$IMAGE" -initrd "$W/i.gz" \
	-append "console=ttyAMA0 loglevel=4 LAB_CMD=$CMD" </dev/null >"$LOG" 2>&1 &
QPID=$!
wait "$QPID"
QEMU_RC=$?
QPID=
tr -d '\r' < "$LOG" > "$LOG.txt"

if [ "$ARG" = demo ]; then
	cat "$LOG.txt"
	echo "=== qemu exit: $QEMU_RC (124 = timeout) === log: $LOG.txt"
	exit 0
fi

# ---- verdict: counts + dmesg grep only, never timing ---------------------
grep -E '^(PASS:|FAIL:| {5}|DMESG_BAD=)' "$LOG.txt"
P=$(grep -c '^PASS:' "$LOG.txt")
F=$(grep -c '^FAIL:' "$LOG.txt")
BAD=$(sed -n 's/^DMESG_BAD=\([0-9]*\).*/\1/p' "$LOG.txt" | tail -1)
DONE=$(grep -c '^GUEST_DONE' "$LOG.txt")
BAD=${BAD:-unknown}

OK=1
[ "$F" -eq 0 ] && [ "$P" -gt 0 ] || OK=0
[ "$DONE" -eq 1 ] || { OK=0; F=$((F + 1)); echo "FAIL: guest did not finish (qemu rc=$QEMU_RC)"; }
[ "$BAD" = 0 ] || { OK=0; echo "FAIL: dmesg bad lines: $BAD"; }

echo "$P/$((P + F)) passed, dmesg bad lines: $BAD"
if [ "$OK" = 1 ]; then
	echo "RESULT: PASS ($ARG)"
	[ "${KEEP:-0}" = 1 ] && echo "kept: $LOG.txt" || rm -rf "$W"
	exit 0
fi
echo "RESULT: FAIL ($ARG) log: $LOG.txt"
exit 1
