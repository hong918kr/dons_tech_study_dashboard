#!/usr/bin/env bash
# ENV: qemu
# ELX-B08: build kecho.ko (VARIANT) + ktest (aarch64) + ktest32 (armhf), boot the lab
# kernel (Linux 6.18.55, QEMU TCG cortex-a57 -smp 1) with an initramfs, run both tests, decide.
#   check.sh <starter|solution|practice>   -> PASS/FAIL lines, N/M passed, RESULT, exit 0/1
#   check.sh demo nocheck                  -> solution with the cmd check removed (dispatch by nr
#                                             only); prints the guest log, always exit 0
#   KEEP_LOG=/tmp/x.txt check.sh ...       -> also copy the full guest console log there
# Runs inside the Lima VM. The module only ever loads in the QEMU guest.
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
KDIR=/var/lab/out/linux-build
IMAGE=/var/lab/out/Image
ROOTFS=/var/lab/build/rootfs
ROOT_WORK=/tmp/ELX-B08_work

ARG=${1:-}
SCEN=${2:-}
case "$ARG" in
starter|solution) VARIANT=$ARG; MODE=base ;;
practice) VARIANT=practice; MODE=practice ;;
demo) [ "$SCEN" = nocheck ] || { echo "usage: check.sh demo nocheck"; exit 2; }
      VARIANT=demo_nocheck; MODE=base ;;
*) echo "usage: check.sh <starter|solution|practice> | demo nocheck"; exit 2 ;;
esac

W=$ROOT_WORK/$ARG${SCEN:+_$SCEN}.$$
QPID=
cleanup() { [ -n "$QPID" ] && kill "$QPID" 2>/dev/null; }
trap cleanup EXIT
mkdir -p "$W/build" "$W/rootfs"
cp "$SRC"/{starter.c,solution.c,practice.c,ktest.c,kecho_ioctl.h,Makefile} "$W/build/"
if [ "$VARIANT" = demo_nocheck ]; then
	# 일부러 틀린 드라이버: 번호 검사를 빼고 _IOC_NR 만 보고 분기한다
	sed 's/^\tret = kecho_check_cmd(cmd);$/\tret = 0; (void)kecho_check_cmd;/' \
		"$W/build/solution.c" > "$W/build/demo_nocheck.c"
	grep -q 'ret = 0; (void)kecho_check_cmd;' "$W/build/demo_nocheck.c" || { echo "demo sed failed"; exit 1; }
fi

(
	cd "$W/build" || exit 1
	make -s KDIR=$KDIR VARIANT=$VARIANT all >build.log 2>&1 || { cat build.log; exit 1; }
	CF="-std=c11 -Wall -Wextra -Werror -O2 -static"
	gcc $CF ktest.c -o ktest >>build.log 2>&1 || { cat build.log; exit 1; }
	arm-linux-gnueabihf-gcc $CF ktest.c -o ktest32 >>build.log 2>&1 || { cat build.log; exit 1; }
) || { echo "FAIL: build"; echo "RESULT: FAIL ($ARG, build)"; exit 1; }

cp -a "$ROOTFS"/. "$W/rootfs/"
cp "$W/build/kecho.ko" "$W/build/ktest" "$W/build/ktest32" "$W/rootfs/"
cat > "$W/rootfs/test.sh" <<G
#!/bin/sh
insmod /kecho.ko || echo "FAIL: insmod"
ls -l /dev/kecho | cat
grep kecho /proc/devices
dmesg | grep 'kecho:'
/ktest $MODE; echo "KTEST64_RC=\$?"
/ktest32 $MODE; echo "KTEST32_RC=\$?"
rmmod kecho || echo "FAIL: rmmod"
n=\$(dmesg | grep -cE 'BUG:|WARNING:|Oops|Call trace|refcount_t')
echo "DMESG_BAD=\$n"
dmesg | grep -E 'BUG:|WARNING:|Oops|Call trace|refcount_t'
echo GUEST_DONE
G
chmod +x "$W/rootfs/test.sh"
(cd "$W/rootfs" && find . | cpio -o -H newc --quiet | gzip -1) > "$W/i.gz"

LOG=$W/console.log
timeout 170 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 1 -m 512M \
	-nographic -kernel "$IMAGE" -initrd "$W/i.gz" \
	-append "console=ttyAMA0 loglevel=5 LAB_CMD=/test.sh" </dev/null >"$LOG" 2>&1 &
QPID=$!
wait "$QPID"
QEMU_RC=$?
QPID=

if [ "$ARG" = demo ]; then
	tr -d '\r' < "$LOG"
	echo "=== qemu exit: $QEMU_RC  log: $LOG ==="
	exit 0
fi

# ---- verdict: counts + exact values + dmesg grep, never timing --------
tr -d '\r' < "$LOG" > "$LOG.txt"
[ -n "${KEEP_LOG:-}" ] && cp "$LOG.txt" "$KEEP_LOG"	# 전체 게스트 로그를 남기고 싶을 때
grep -E '^(PASS:|FAIL:|\[a(64|32)\] [0-9]+/[0-9]+ passed|KTEST(64|32)_RC=|DMESG_BAD=)' "$LOG.txt"
P=$(grep -c '^PASS:' "$LOG.txt")
F=$(grep -c '^FAIL:' "$LOG.txt")
S64=$(grep -c '^\[a64\] [0-9]*/[0-9]* passed' "$LOG.txt")
S32=$(grep -c '^\[a32\] [0-9]*/[0-9]* passed' "$LOG.txt")
BAD=$(sed -n 's/^DMESG_BAD=\([0-9]*\).*/\1/p' "$LOG.txt" | tail -1)
DONE=$(grep -c '^GUEST_DONE' "$LOG.txt")
BAD=${BAD:-unknown}

OK=1
[ "$F" -eq 0 ] && [ "$P" -gt 0 ] || OK=0
[ "$S64" -eq 1 ] && [ "$S32" -eq 1 ] || { OK=0; echo "a test binary did not finish (a64=$S64 a32=$S32)"; }
[ "$DONE" -eq 1 ] || { OK=0; echo "guest did not finish (qemu rc=$QEMU_RC)"; }
[ "$BAD" = 0 ] || OK=0

echo "$P/$((P + F)) passed, dmesg bad lines: $BAD"
if [ "$OK" = 1 ]; then
	echo "RESULT: PASS ($ARG)"
	rm -rf "$W"
	exit 0
fi
echo "RESULT: FAIL ($ARG) log: $LOG.txt"
exit 1
