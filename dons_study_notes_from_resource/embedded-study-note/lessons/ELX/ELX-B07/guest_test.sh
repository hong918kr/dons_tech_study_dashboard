#!/bin/sh
# ENV: qemu
# ELX-B07 guest script: BusyBox sh inside the QEMU guest (check.sh runs it as LAB_CMD).
#   guest_test.sh <base|practice>
# Prints PASS:/FAIL: lines, DMESG_BAD=<n>, GUEST_DONE. check.sh counts them.
MODE=${1:-base}
STEPS="1 2 3 4"; [ "$MODE" = practice ] && STEPS="1 2 3 4 5"

ok() { if [ "$1" = 0 ]; then echo "PASS: $2"; else echo "FAIL: $2"; fi; }
gone() {	# nothing of kecho left anywhere
	[ ! -e /sys/class/kecho ] && ! grep -qw kecho /proc/devices &&
	[ ! -e /dev/kecho ] && [ ! -e /dev/kecho1 ] && [ ! -e /dev/kecho_misc ] &&
	! grep -qw kecho_misc /proc/misc && ! grep -q '^kecho ' /proc/modules
}

insmod /kecho.ko; ok $? "S0 insmod kecho.ko"
dmesg | grep 'kecho:'

# ---- Part A/B/C from C (CHECK harness) ------------------------------------
/kecho_test "$MODE"

# ---- Part C from the shell: the node is only a number ---------------------
MAJ=$(awk '$2=="kecho" {print $1; exit}' /proc/devices)
echo "     /proc/devices: $MAJ kecho"
ls -l --color=never /dev/kecho* /sys/class/kecho/ 2>&1
[ -n "$MAJ" ] && [ "$(ls -l /dev/kecho | awk '{print $5}')" = "$MAJ," ]
ok $? "S1 ls -l /dev/kecho major == /proc/devices"
if [ -n "$MAJ" ] && [ -c /dev/kecho ]; then	# never let dd create a plain file
	mknod /tmp/kalias c "$MAJ" 0; ok $? "S2 mknod /tmp/kalias c $MAJ 0"
	printf 'alias' > /tmp/kalias
	[ "$(head -c 5 /dev/kecho)" = alias ]
	ok $? "S2 write via /tmp/kalias, read same bytes via /dev/kecho"
	dd if=/dev/zero of=/dev/kecho bs=1024 count=5 2>/tmp/dd.err; R=$?
	cat /tmp/dd.err
	[ $R != 0 ] && grep -q 'No space left on device' /tmp/dd.err &&
		grep -q '^4+0 records out' /tmp/dd.err
	ok $? "S3 dd 5 x 1 KiB: 4 records, then ENOSPC"
	[ "$(cat /dev/kecho | wc -c)" = 4096 ]; ok $? "S3 cat /dev/kecho | wc -c == 4096"
else
	echo "FAIL: S2/S3 no /dev/kecho char device to test"
fi

echo "     refcnt with no opener: $(cat /sys/module/kecho/refcnt 2>&1)"
sleep 30 < /dev/kecho &
HOLD=$!
sleep 1
echo "     /proc/modules: $(grep '^kecho ' /proc/modules)"
rmmod kecho 2>/tmp/rm.err
echo "     rmmod rc=$? ($(cat /tmp/rm.err))"	# BusyBox rmmod: rc is not reliable
grep -q '^kecho ' /proc/modules; ok $? "S4 rmmod refused while /dev/kecho is open"
kill $HOLD; wait $HOLD 2>/dev/null
rmmod kecho; ! grep -q '^kecho ' /proc/modules; ok $? "S4 rmmod after the holder closed"
gone; ok $? "S4 nothing left after rmmod (class, /proc/devices, /dev)"
cat /tmp/kalias 2>/tmp/cat.err; cat /tmp/cat.err
grep -q 'No such device or address' /tmp/cat.err; ok $? "S5 stale mknod node -> ENXIO"

# ---- Part C: every init failure must unwind completely --------------------
for n in $STEPS; do
	insmod /kecho.ko fail_at=$n 2>/tmp/ins.err; R=$?
	[ "$n" = 1 ] && cat /tmp/ins.err
	[ $R != 0 ] && dmesg | grep -q "injected failure at step $n" && gone
	ok $? "S6 fail_at=$n: insmod fails, nothing leaked"
done
insmod /kecho.ko && [ -c /dev/kecho ] && [ "$(cat /dev/kecho | wc -c)" = 0 ]
ok $? "S7 clean insmod after the failed ones (fresh, empty buffer)"
rmmod kecho; gone; ok $? "S7 final rmmod, nothing left"

n=$(dmesg | grep -cE 'BUG:|WARNING:|Oops|Call trace|refcount_t')
echo "DMESG_BAD=$n"
dmesg | grep -E 'BUG:|WARNING:|Oops|Call trace|refcount_t' | head -5
echo GUEST_DONE
