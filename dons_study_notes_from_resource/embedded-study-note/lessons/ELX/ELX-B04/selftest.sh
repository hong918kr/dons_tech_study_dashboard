#!/bin/sh
# ENV: qemu
# ELX-B04 guest script. /init (PID 1) runs it as "/bin/sh /etc/selftest.sh" because the
# kernel command line has B04_SCRIPT=/etc/selftest.sh. Non-interactive: no prompt, no tty.
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

# leave one orphan behind: the subshell outlives me -> re-parented to PID 1 (exit 5)
( sleep 1; exit 5 ) &
echo "SCRIPT: left orphan pid $! behind, exiting 3"
exit 3
