#!/bin/bash
# ELX-M05: check.sh <starter|solution|practice>   -- run INSIDE the Lima VM, from any directory.
# Builds the module + helpers in /tmp/ELX-M05_work, boots Image-dbg (QEMU TCG cortex-a57 -smp 4),
# decides from guest output (counts + dmesg). Exit 0 = PASS, 1 = FAIL. Timing never decides.
set -u
V=${1:-solution}
case "$V" in starter|solution|practice) ;; *) echo "usage: $0 starter|solution|practice"; exit 2;; esac
SRC=$(cd "$(dirname "$0")" && pwd)
. "$SRC/lab_common.sh"

prepare "$SRC" "$V" "$V" || { echo "BUILD FAILED"; echo "VERDICT $V: FAIL"; exit 1; }
LOG=$WORK/guest.log
boot "/guest.sh~check~$V" 110 | tee "$LOG"
echo "--- check.sh verdict ---"
rc=0
if ! grep -q 'GUEST_END' "$LOG"; then
	echo "guest did not finish (timeout or crash)"; rc=1
fi
line=$(grep -a 'TESTS passed' "$LOG" | tail -1)
echo "${line:-no TESTS line}"
pass=$(echo "$line" | sed -n 's/^TESTS passed \([0-9]*\)\/\([0-9]*\).*/\1/p')
tot=$(echo "$line" | sed -n 's/^TESTS passed \([0-9]*\)\/\([0-9]*\).*/\2/p')
if [ -z "$pass" ] || [ "$pass" != "$tot" ]; then rc=1; fi
if grep -aE 'BUG:|WARNING:|lockdep|possible .* deadlock|inconsistent|Oops|sleeping function' "$LOG" | grep -av "lockdep is turned off"; then
	echo "kernel log has BUG/WARNING/lockdep lines"; rc=1
fi
if [ $rc = 0 ]; then echo "VERDICT $V: PASS"; exit 0; fi
echo "VERDICT $V: FAIL"; exit 1
