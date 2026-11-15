#!/usr/bin/env bash
# ENV: linux (runs inside the Lima VM; the I2C code runs in a QEMU guest)
# ELX-B11: build i2creg (static) + the fake-adapter fixture, boot Image-dom,
# modprobe i2c-stub chip_addr=0x48, run the tests, decide.
#   check.sh <starter|solution|practice>   -> PASS/FAIL lines, N/M passed, exit 0/1
#   check.sh demo                          -> print a guest log: i2cdetect, i2ctransfer,
#                                             tracepoints of RDWR vs SMBus (solution build)
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
KVER=6.18.55-dom
KDIR=/var/lab/out/linux-build-dom
IMAGE=/var/lab/out/Image-dom
MODS=/var/lab/out/modules-dom/lib/modules/$KVER
ROOTFS=/var/lab/build/rootfs

ARG=${1:-}
case "$ARG" in
starter|solution|practice) VARIANT=$ARG ;;
demo) VARIANT=solution ;;
*) echo "usage: check.sh <starter|solution|practice|demo>"; exit 2 ;;
esac
TMO=150

W=/tmp/ELX-B11_work/$ARG.$$
QPID=
cleanup() {
	[ -n "$QPID" ] && kill "$QPID" 2>/dev/null
	return 0
}
trap cleanup EXIT
mkdir -p "$W/build" "$W/rootfs"
cp "$SRC"/{starter.c,solution.c,practice.c,b11_fakei2c.c,Makefile} "$W/build/"

(
	cd "$W/build" || exit 1
	make -s KDIR=$KDIR all >build.log 2>&1 || { cat build.log; exit 1; }
	gcc -static -std=c11 -Wall -Wextra -Werror -O2 "$VARIANT.c" -o i2creg \
		>>build.log 2>&1 || { cat build.log; exit 1; }
) || { echo "FAIL: build"; echo "RESULT: FAIL ($ARG, build)"; exit 1; }

cp -a "$ROOTFS"/. "$W/rootfs/"
D=$W/rootfs/lib/modules/$KVER
mkdir -p "$D/kernel/drivers/i2c" "$D/kernel/drivers/spi"
cp "$MODS/kernel/drivers/i2c/i2c-stub.ko" "$D/kernel/drivers/i2c/"
cp "$MODS/kernel/drivers/spi/spidev.ko" "$D/kernel/drivers/spi/"
cp "$MODS/modules.dep" "$MODS/modules.alias" "$D/"
cp "$W/build/b11_fakei2c.ko" "$W/build/i2creg" "$W/rootfs/"

# ---- guest plan --------------------------------------------------------
{
	cat <<'G'
#!/bin/sh
modprobe i2c-stub chip_addr=0x48 || echo "FAIL: modprobe i2c-stub"
insmod /b11_fakei2c.ko || echo "FAIL: insmod b11_fakei2c"
modprobe spidev || echo "FAIL: modprobe spidev"
S=; F=
for d in /sys/bus/i2c/devices/i2c-*; do
	case "$(cat $d/name)" in
	"SMBus stub driver") S=${d##*-} ;;
	b11-fakei2c) F=${d##*-} ;;
	esac
done
echo "BUSES stub=i2c-$S fake=i2c-$F"
G
	if [ "$ARG" = demo ]; then
		cat <<'G'
set -x
i2cdetect -l
i2cdetect -F $S
i2cdetect -F $F
i2cdetect -y $S
i2ctransfer -y $S w1@0x48 0x0f r1
i2ctransfer -y $F w1@0x48 0x0f r1
/i2creg read $F 0x48 0x0f
/i2creg read $S 0x48 0x0f
/i2creg read $F 0x49 0x00
/i2creg read $S 0x49 0x00
i2cget -y $F 0x48 0x00 w
/i2creg read $F 0x48 0x00 2
i2cset -y $S 0x48 0x20 0x1234 w
i2cget -y $S 0x48 0x20 w
i2cget -y $S 0x48 0x20 b
i2cget -y $S 0x48 0x21 b
/i2creg read $S 0x48 0x20 2
grep -E 'i2c|spi' /proc/devices
ls /sys/class/spi_master /dev/spidev0.0
set +x
mount -t tracefs none /sys/kernel/tracing
T=/sys/kernel/tracing
echo 1 > $T/events/i2c/enable
echo 1 > $T/events/smbus/enable
echo > $T/trace
echo "== trace: /i2creg read FAKE 0x48 0x0f 2 (I2C_RDWR)"
/i2creg read $F 0x48 0x0f 2
grep -v '^#' $T/trace | sed 's/^ *//'
echo > $T/trace
echo "== trace: i2cget -y FAKE 0x48 0x0f (SMBus, emulated by core)"
i2cget -y $F 0x48 0x0f
grep -v '^#' $T/trace | sed 's/^ *//'
echo > $T/trace
echo "== trace: i2cget -y STUB 0x48 0x10 (SMBus, native smbus_xfer)"
i2cget -y $S 0x48 0x10
grep -v '^#' $T/trace | sed 's/^ *//'
G
	else
		echo 'dmesg -c >/dev/null'
		echo '/i2creg test $S $F; echo "PROG_RC=$?"'
	fi
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
# BusyBox ls colours names: strip ANSI escapes from the log
sed -i 's/\x1b\[[0-9;]*m//g; s/\r$//' "$LOG"

if [ "$ARG" = demo ]; then
	sed -n '/^BUSES/,/^GUEST_DONE/p' "$LOG"
	echo "=== qemu exit: $QEMU_RC, Linux $KVER, QEMU TCG cortex-a57 -smp 1 ==="
	rm -rf "$W"
	exit 0
fi

# ---- verdict: counts, exact values, dmesg grep -- never timing ----------
grep -E '^(BUSES|---|PASS:|FAIL:|PROG_RC=|DMESG_BAD=)|passed$' "$LOG"
P=$(grep -c '^PASS:' "$LOG")
F=$(grep -c '^FAIL:' "$LOG")
RC=$(sed -n 's/^PROG_RC=\([0-9]*\).*/\1/p' "$LOG" | tail -1)
BAD=$(sed -n 's/^DMESG_BAD=\([0-9]*\).*/\1/p' "$LOG" | tail -1)
DONE=$(grep -c '^GUEST_DONE' "$LOG")

OK=1
[ "$F" -eq 0 ] && [ "$P" -gt 0 ] || OK=0
[ "${RC:-x}" = 0 ] || OK=0
[ "$DONE" -eq 1 ] || { OK=0; echo "guest did not finish (qemu rc=$QEMU_RC)"; }
[ "${BAD:-x}" = 0 ] || OK=0

echo "$P/$((P + F)) passed (guest program rc=${RC:-none}, dmesg bad lines: ${BAD:-unknown})"
if [ "$OK" = 1 ]; then
	echo "RESULT: PASS ($ARG)"
	rm -rf "$W"
	exit 0
fi
rm -rf "$W/rootfs" "$W/i.gz"	# keep only the log + build dir
echo "RESULT: FAIL ($ARG) log: $LOG"
exit 1
