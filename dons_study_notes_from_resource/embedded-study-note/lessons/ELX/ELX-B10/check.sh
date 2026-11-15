#!/bin/bash
# ENV: linux
# ELX-B10: check.sh <starter|solution|practice|demo>  -- run INSIDE the Lima VM, from any cwd:
#   limactl shell elinux -- bash <lesson dir>/check.sh solution
# Builds <variant>.c as a static guest binary, packs it into a private copy of the lab
# BusyBox initramfs, boots Linux 6.18.55 (/var/lab/out/Image, QEMU TCG cortex-a57 -smp 2),
# and decides from guest output only: "N/M passed" counts + kernel log grep. Never timing.
set -u
V=${1:-solution}
case "$V" in starter|solution|practice|demo) ;; *) echo "usage: $0 starter|solution|practice|demo"; exit 2;; esac
SRC=$(cd "$(dirname "$0")" && pwd)
WORK=/tmp/ELX-B10_work/$V.$$
IMAGE=/var/lab/out/Image
QPID=
rc=1
cleanup() {
	[ -n "$QPID" ] && kill "$QPID" 2>/dev/null && wait "$QPID" 2>/dev/null
	if [ $rc = 0 ]; then rm -rf "$WORK"; rmdir /tmp/ELX-B10_work 2>/dev/null
	else rm -rf "$WORK/r" "$WORK/i.gz"; fi	# keep guest.log + binaries for a look
	return 0
}
trap cleanup EXIT
mkdir -p "$WORK"
cp "$SRC"/*.c "$SRC"/guest.sh "$WORK"/

prog=$V; [ "$V" = demo ] && prog=solution
CFLAGS="-static -std=c11 -Wall -Wextra -Werror -O2"
if ! gcc $CFLAGS "$WORK/$prog.c" -o "$WORK/b10_test" ||
   ! gcc $CFLAGS "$WORK/demo_burst.c" -o "$WORK/demo_burst"; then
	echo "BUILD FAILED"; echo "RESULT: FAIL ($V: build)"; exit 1
fi
R=$WORK/r
mkdir -p "$R"
cp -a /var/lab/build/rootfs/. "$R"/
cp "$WORK/b10_test" "$WORK/demo_burst" "$WORK/guest.sh" "$R"/
chmod +x "$R/guest.sh"
(cd "$R" && find . | cpio -o -H newc --quiet | gzip -1) > "$WORK/i.gz"

mode=check; [ "$V" = demo ] && mode=demo
LOG=$WORK/guest.log
timeout 170 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 512M -nographic \
	-no-reboot -kernel "$IMAGE" -initrd "$WORK/i.gz" \
	-append "console=ttyAMA0 loglevel=4 LAB_CMD=/guest.sh~$mode" </dev/null > "$LOG" 2>&1 &
QPID=$!
wait "$QPID"
QPID=
tr -d '\r' < "$LOG" | sed -n '/GUEST_START/,/GUEST_END/p'
if [ "$V" = demo ]; then rc=0; exit 0; fi

echo "--- check.sh verdict ---"
ok=1
grep -q GUEST_END "$LOG" || { echo "FAIL guest did not finish (timeout or crash)"; ok=0; }
line=$(tr -d '\r' < "$LOG" | grep -aE '^[0-9]+/[0-9]+ passed$' | tail -1)
pass=${line%%/*}; tot=${line#*/}; tot=${tot%% *}
if [ -n "$line" ] && [ "$pass" = "$tot" ]; then echo "PASS guest tests $line"
else echo "FAIL guest tests ${line:-no 'N/M passed' line}"; ok=0; fi
if sed -n '/GUEST_START/,$p' "$LOG" | grep -aE 'BUG:|WARNING:|Oops|Call trace|refcount_t'; then
	echo "FAIL kernel log has BUG/WARNING/Oops lines"; ok=0
else echo "PASS kernel log clean"; fi
if [ $ok = 1 ]; then rc=0; echo "RESULT: PASS ($V)"; exit 0; fi
echo "RESULT: FAIL ($V: see $LOG)"; exit 1
