#!/bin/sh
# ENV: qemu
# ELX-B04 practice guest script (B04_SCRIPT=/etc/practice.sh). Same checks as selftest.sh,
# plus: PID 1 ignores KILL/STOP/INT, leaves a daemon behind, asks PID 1 to power off with SIGUSR2.
ok()  { echo "PASS: S$1 $2"; }
bad() { echo "FAIL: S$1 $2"; }
t() { n=$1; shift; d=$1; shift; if "$@"; then ok "$n" "$d"; else bad "$n" "$d"; fi; }

echo "SCRIPT: pid=$$ ppid=$PPID kernel=$(uname -r) rootfs=$(awk '$2=="/"{print $3}' /proc/mounts)"
t 1 "parent is PID 1"                  [ "$PPID" = 1 ]
t 2 "PID 1 is our C init"              [ "$(cat /proc/1/comm)" = init ]
t 3 "proc, sysfs, devtmpfs in /proc/mounts" \
	sh -c 'for m in proc sysfs devtmpfs; do grep -q " $m " /proc/mounts || exit 1; done'
t 4 "/dev/console is a char device"    [ -c /dev/console ]
t 5 "kernel env: HOME=/ TERM=linux"    [ "$HOME" = / -a "$TERM" = linux ]
t 6 "B04_SCRIPT points at me"          [ "$B04_SCRIPT" = "$0" ]
t 7 "/proc/cmdline has rdinit=/init"   grep -q 'rdinit=/init' /proc/cmdline
t 8 "devtmpfs made ttyAMA0 + null"     [ -c /dev/ttyAMA0 -a -c /dev/null ]

# P5: signals to PID 1 — KILL/STOP never reach global init, INT has SIG_DFL -> dropped
kill -KILL 1; kill -STOP 1; kill -INT 1
st=$(awk '/^State:/{print $2}' /proc/1/status)
echo "SCRIPT: after kill -KILL/-STOP/-INT 1: PID 1 state=$st"
t 9 "PID 1 survived KILL/STOP/INT (state S/R, not T)" [ "$st" = S -o "$st" = R ]

sleep 1000 &                            # P2: a daemon that never exits by itself
echo "SCRIPT: left daemon pid $! behind, sending SIGUSR2 to PID 1, exiting 3"
kill -USR2 1                            # P1: BusyBox convention: USR2 = poweroff
exit 3
