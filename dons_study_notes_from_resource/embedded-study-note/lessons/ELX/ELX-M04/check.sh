#!/usr/bin/env bash
# ELX-M04: build the module and helper, boot Image-dbg in QEMU, run, decide.
#   check.sh <starter|solution|practice>     -> PASS/FAIL, exit 0/1
#   check.sh demo <scenario>                 -> run a broken scenario, print the log
# Runs inside the Lima VM. The module only ever loads in the QEMU guest.
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
KDIR=/var/lab/out/linux-build-dbg
IMAGE=/var/lab/out/Image-dbg
ROOTFS=/var/lab/build/rootfs
ROOT_WORK=/tmp/ELX-M04_work

ARG=${1:-}
SCEN=${2:-}
case "$ARG" in
starter|solution|practice) VARIANT=$ARG; TMO=300 ;;
demo) VARIANT=solution; TMO=120 ;;
*) echo "usage: check.sh <starter|solution|practice> | demo <scenario>"; exit 2 ;;
esac
[ "$ARG" = starter ] && TMO=110

W=$ROOT_WORK/$ARG${SCEN:+_$SCEN}.$$
mkdir -p "$W/build" "$W/rootfs"
cp "$SRC"/{starter.c,solution.c,practice.c,stress.c,syncdrv_uapi.h,Makefile} "$W/build/"

(
	cd "$W/build" || exit 1
	make -s KDIR=$KDIR VARIANT=$VARIANT all >build.log 2>&1 || { cat build.log; exit 1; }
	gcc -static -O2 -Wall -Wextra -Werror -pthread stress.c -o stress >>build.log 2>&1 \
		|| { cat build.log; exit 1; }
) || { echo "FAIL: build"; echo "RESULT: FAIL ($ARG, build)"; exit 1; }

cp -a "$ROOTFS"/. "$W/rootfs/"
cp "$W/build/$VARIANT.ko" "$W/build/stress" "$W/rootfs/"

# ---- guest plan --------------------------------------------------------
N=4 K=20000 B=200 T=500
{
	echo '#!/bin/sh'
	echo "insmod /$VARIANT.ko || echo FAIL: insmod"
	echo 'dmesg -c >/dev/null'
	case "$ARG" in
	starter|solution)
		cat <<G
/stress selftest
/stress run irqsave $T 1 $N $K $B check
/stress run atomic  $T 0 $N $K $B check
/stress run mutex   0  1 $N $K $B check
G
		;;
	practice)
		cat <<G
/stress selftest
/stress run raw_irqsave ${T}h 1 $N $K $B check
/stress run wq_mutex    $T    1 $N $K $B check
/stress run spin_irq    $T    1 $N $K $B check
/stress run percpu      $T    0 $N $K $B check
/stress nested
G
		;;
	demo)
		case "$SCEN" in
		nolock)      echo "/stress run none $T 1 $N $K $B"
		             echo "/stress run none $T 1 $N $K $B"
		             echo "/stress run none 0  1 $N $K $B" ;;
		mutex_irq)   echo "/stress run mutex 2000 1 2 2000 100" ;;
		spin_irq)    echo "/stress run spin 2000 1 2 2000 100" ;;
		irqsave_hard) echo "/stress run irqsave ${T}h 1 $N $K $B" ;;
		spin_deadlock) echo "/stress run spin 100 0 1 4000000 1000 pin0" ;;
		atomic_list) echo "/stress run atomic $T 1 $N $K $B"
		             echo "/stress run atomic $T 1 $N $K $B" ;;
		abba)        echo "/stress abba" ;;
		recursive)   echo "/stress recursive" ;;
		lockstat)    echo 'echo 0 > /proc/lock_stat'
		             echo 'echo 1 > /proc/sys/kernel/lock_stat'
		             echo "/stress run irqsave $T 1 $N $K $B"
		             echo 'echo 0 > /proc/sys/kernel/lock_stat'
		             echo 'echo ===LOCK_STAT===; head -3 /proc/lock_stat'
		             echo "grep -A3 'sd\\.' /proc/lock_stat" ;;
		*) echo "echo unknown scenario $SCEN" ;;
		esac
		;;
	esac
	cat <<'G'
n=$(dmesg | grep -cE 'BUG:|WARNING:|lockdep|possible .* deadlock|inconsistent')
echo "DMESG_BAD=$n"
dmesg | grep -E 'BUG:|WARNING:|lockdep|possible .* deadlock|inconsistent'
echo GUEST_DONE
G
} > "$W/rootfs/test.sh"
chmod +x "$W/rootfs/test.sh"
(cd "$W/rootfs" && find . | cpio -o -H newc --quiet | gzip -1) > "$W/i.gz"

LOG=$W/console.log
timeout "$TMO" qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 4 -m 512M \
	-nographic -kernel "$IMAGE" -initrd "$W/i.gz" \
	-append "console=ttyAMA0 loglevel=5 LAB_CMD=/test.sh" </dev/null >"$LOG" 2>&1
QEMU_RC=$?

if [ "$ARG" = demo ]; then
	cat "$LOG"
	echo "=== qemu exit: $QEMU_RC (124 = timeout; a hung guest is an expected demo outcome) ==="
	echo "=== log: $LOG ==="
	exit 0
fi

# ---- verdict: counts + dmesg grep only, never timing --------------------
grep -E '^(PASS:|FAIL:|RESULT |DMESG_BAD=)' "$LOG"
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
echo "RESULT: FAIL ($ARG) log: $LOG"
exit 1
