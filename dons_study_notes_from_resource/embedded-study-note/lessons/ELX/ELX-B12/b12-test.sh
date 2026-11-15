#!/bin/sh
# ENV: qemu
# ELX-B12 guest checks, started by inittab ::once: after sysinit finished. Ends with poweroff.
ok() { if eval "$2" >/dev/null 2>&1; then echo "PASS: guest $1"; else echo "FAIL: guest $1"; fi; }
ok "PID 1 is BusyBox init (/proc/1/exe)" '[ "$(readlink /proc/1/exe)" = /bin/busybox ]'
ok "root = /dev/root ext4 rw" 'grep -q "^/dev/root / ext4 rw" /proc/mounts'
ok "inittab sysinit ran rcS" '[ -f /run/b12-rcS.done ]'
ok "proc mounted by rcS before mount -a" 'grep -q "^proc /proc proc " /proc/mounts'
for m in "sysfs /sys sysfs" "tmpfs /tmp tmpfs" "tmpfs /run tmpfs" "devpts /dev/pts devpts"; do
	ok "fstab via mount -a: $m" "grep -q '^$m ' /proc/mounts"
done
ok "devtmpfs on /dev (kernel DEVTMPFS_MOUNT)" 'grep -q "^devtmpfs /dev devtmpfs" /proc/mounts'
ok "hostname from /etc/hostname" '[ "$(hostname)" = b12-board ]'
ok "every busybox lib resolves (ld.so --list)" '/lib/ld-linux-aarch64.so.1 --list /bin/busybox >/tmp/l && ! grep -q "not found" /tmp/l'
ok "sensor_app finds libsensor via RUNPATH" '[ "$(/usr/bin/sensor_app)" = "sensor: libsensor 1.0 reading=42" ]'
ok "files are root-owned (fakeroot)" '[ "$(stat -c %u:%g /bin/busybox /etc/inittab | sort -u)" = 0:0 ]'
sed 's/^/LIST: /' /tmp/l
echo "MOUNTS: $(cut -d' ' -f1-3 /proc/mounts | tr '\n' ';')"
echo "SIZE: $(du -sk /bin /lib /usr | tr '\n\t' '; ')"
echo GUEST_DONE
poweroff
