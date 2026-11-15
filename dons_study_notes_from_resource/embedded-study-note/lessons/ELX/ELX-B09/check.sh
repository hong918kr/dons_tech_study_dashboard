#!/usr/bin/env bash
# ENV: linux
# ELX-B09: build the module + static helper, boot Image-dbg in QEMU, run the
# shell test in the guest, decide from PASS/FAIL counts + dmesg grep.
#   check.sh <starter|solution|practice>   -> PASS/FAIL, exit 0/1
#   check.sh demo <ret0|atomic|perm>       -> reproduce a broken driver, print the log
# Runs inside the Lima VM. The module only ever loads in the QEMU guest.
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
KDIR=/var/lab/out/linux-build-dbg
IMAGE=/var/lab/out/Image-dbg
ROOTFS=/var/lab/build/rootfs
ROOT_WORK=/tmp/ELX-B09_work

ARG=${1:-}
SCEN=${2:-}
case "$ARG" in
starter|solution|practice) VARIANT=$ARG; TMO=240 ;;
demo) VARIANT=demo_bugs; TMO=120
      case "$SCEN" in ret0|atomic|perm) ;; *) echo "usage: check.sh demo <ret0|atomic|perm>"; exit 2 ;; esac ;;
*) echo "usage: check.sh <starter|solution|practice> | demo <ret0|atomic|perm>"; exit 2 ;;
esac
LEVEL=basic
[ "$ARG" = practice ] && LEVEL=practice

W=$ROOT_WORK/$ARG${SCEN:+_$SCEN}.$$
QPID=
cleanup() {
	[ -n "$QPID" ] && kill "$QPID" 2>/dev/null
	return 0
}
trap cleanup EXIT
mkdir -p "$W/build" "$W/rootfs"
cp "$SRC"/{starter.c,solution.c,practice.c,demo_bugs.c,systest.c,sysfs_test.sh,Makefile} "$W/build/"

EXTRA=
[ "$SCEN" = perm ] && EXTRA=-DB09_DEMO_PERM
(
	cd "$W/build" || exit 1
	make -s KDIR=$KDIR VARIANT=$VARIANT EXTRA_CFLAGS_B09="$EXTRA" all >build.log 2>&1 \
		|| { cat build.log; exit 1; }
	gcc -std=c11 -Wall -Wextra -Werror -O2 -static systest.c -o systest >>build.log 2>&1 \
		|| { cat build.log; exit 1; }
) || {
	if [ "$SCEN" = perm ]; then
		echo "=== demo perm: build failed as expected (0666 attribute) ==="
		rm -rf "$W"
		exit 0
	fi
	echo "FAIL: build"; echo "RESULT: FAIL ($ARG, build)"; exit 1
}

cp -a "$ROOTFS"/. "$W/rootfs/"
cp "$W/build/$VARIANT.ko" "$W/build/systest" "$W/build/sysfs_test.sh" "$W/rootfs/"
chmod +x "$W/rootfs/sysfs_test.sh"

# ---- guest plan --------------------------------------------------------
{
	echo '#!/bin/sh'
	echo 'dmesg -n 1'
	case "$ARG" in
	demo)
		D=/sys/bus/platform/devices/thsense
		case "$SCEN" in
		ret0)   echo "insmod /demo_bugs.ko bug=1"
			echo "echo '--- echo 42 > threshold (store returns 0), killed after 3 s:'"
			echo "timeout 3 sh -c 'echo 42 > $D/threshold'; echo \"rc=\$?\""
			echo "echo \"store calls: \$(cat $D/stores)  threshold: \$(cat $D/threshold)\"" ;;
		atomic) echo "insmod /demo_bugs.ko bug=2"
			echo "echo 600 > $D/sample"
			echo "echo \"count: \$(cat $D/count)\""
			echo "dmesg | grep -A14 'BUG:'" ;;
		esac ;;
	*)
		echo "insmod /$VARIANT.ko || echo FAIL: insmod"
		echo "/sysfs_test.sh $VARIANT $LEVEL"
		;;
	esac
	cat <<'G'
n=$(dmesg | grep -cE 'BUG:|WARNING:|Oops|Call trace|refcount_t')
echo "DMESG_BAD=$n"
dmesg | grep -E 'BUG:|WARNING:|Oops|Call trace|refcount_t'
echo GUEST_DONE
G
} > "$W/rootfs/test.sh"
chmod +x "$W/rootfs/test.sh"
(cd "$W/rootfs" && find . | cpio -o -H newc --quiet | gzip -1) > "$W/i.gz"

LOG=$W/console.log
timeout "$TMO" qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 1 -m 512M \
	-nographic -kernel "$IMAGE" -initrd "$W/i.gz" \
	-append "console=ttyAMA0 loglevel=4 LAB_CMD=/test.sh" </dev/null >"$LOG" 2>&1 &
QPID=$!
wait "$QPID"
QEMU_RC=$?
QPID=

if [ "$ARG" = demo ]; then
	sed -n '/lab initramfs/,$p' "$LOG"
	echo "=== qemu exit: $QEMU_RC ==="
	rm -rf "$W"
	exit 0
fi

# ---- verdict: counts + dmesg grep only, never timing --------------------
grep -E '^(PASS:|FAIL:|  \[|sysfs_test:|poll:|DMESG_BAD=)' "$LOG"
P=$(grep -c '^PASS:' "$LOG")
F=$(grep -c '^FAIL:' "$LOG")
BAD=$(sed -n 's/^DMESG_BAD=\([0-9]*\).*/\1/p' "$LOG" | tail -1)
DONE=$(grep -c '^GUEST_DONE' "$LOG")
BAD=${BAD:-unknown}

OK=1
[ "$F" -eq 0 ] && [ "$P" -gt 0 ] || OK=0
[ "$DONE" -eq 1 ] || { OK=0; echo "guest did not finish (qemu rc=$QEMU_RC)"; }
[ "$BAD" = 0 ] || OK=0

echo "$P/$((P + F)) passed, dmesg bad lines: $BAD"
if [ "$OK" = 1 ]; then
	echo "RESULT: PASS ($ARG)"
	rm -rf "$W"
	exit 0
fi
cp "$LOG" "$ROOT_WORK/$ARG.last.log"
rm -rf "$W"
echo "RESULT: FAIL ($ARG) log: $ROOT_WORK/$ARG.last.log"
exit 1
