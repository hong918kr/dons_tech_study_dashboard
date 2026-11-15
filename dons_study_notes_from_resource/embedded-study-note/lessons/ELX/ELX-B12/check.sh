#!/usr/bin/env bash
# ENV: linux
# ELX-B12: libclosure tests -> dynamic BusyBox -> hand-made rootfs (closure staged by <variant>.py)
#          -> root-owned ext4 via fakeroot + mkfs.ext4 -d -> QEMU virt, root=/dev/vda -> verdict
#   check.sh <starter|solution|practice>   PASS/FAIL lines, N/M passed, RESULT line, exit 0/1
#   check.sh demo <ok|nolib|nointerp>      solution rootfs as is (ok) or minus one file; prints the console log
#                                          (KEEP=1 keeps the work dir: rootfs/, rootfs.ext4, console.log)
# Runs inside the Lima VM. Nothing is installed into the VM; the guest is a QEMU TCG cortex-a57.
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
IMAGE=/var/lab/out/Image
BBC=/var/tmp/ELX-B12_cache          # dynamic BusyBox build, reused across runs (rm -rf it to rebuild)
STRICT="-std=c11 -Wall -Wextra -Werror -O2"

ARG=${1:-}
SCEN=${2:-}
case "$ARG" in
starter|solution|practice) V=$ARG; TMO=150 ;;
demo) V=solution; TMO=90
	case "$SCEN" in ok|nolib|nointerp) ;; *) echo "usage: check.sh demo <ok|nolib|nointerp>"; exit 2 ;; esac ;;
*) echo "usage: check.sh <starter|solution|practice> | demo <ok|nolib|nointerp>"; exit 2 ;;
esac

W=/var/tmp/ELX-B12_work/$ARG${SCEN:+_$SCEN}.$$
R=$W/root        # the rootfs being built (becomes the ext4 image)
ST=$W/staging    # "staging": where the sensor package was installed (libs come from here or from / = sysroot)
mkdir -p "$W" "$ST/usr/lib/sensor"
cp "$SRC"/{starter.py,solution.py,practice.py,libsensor.c,sensor_app.c,inittab,fstab,rcS,b12-test.sh,hostname} "$W/"
fail() { echo "FAIL: $1"; echo "RESULT: FAIL ($ARG, $1) dir: $W"; exit 1; }

# ---- 1. libclosure unit tests (fixtures built with gcc, compared with the real ldd) ----
PY_P=0 PY_F=0
if [ "$ARG" != demo ]; then
	python3 -I "$W/$V.py" test >"$W/pytest.log" 2>&1
	grep -E '^(PASS|FAIL):' "$W/pytest.log"
	PY_P=$(grep -c '^PASS:' "$W/pytest.log")
	PY_F=$(grep -c '^FAIL:' "$W/pytest.log")
	if grep -q '^Traceback' "$W/pytest.log"; then
		echo "FAIL: python test crashed:"; tail -3 "$W/pytest.log"; PY_F=$((PY_F + 1))
	fi
fi

# ---- 2. dynamic BusyBox 1.37.0 from a private copy of /var/lab/src (built once, flock'd) ----
mkdir -p "$BBC"
(
	flock 9
	if [ ! -x "$BBC/install/bin/busybox" ]; then
		echo "busybox: building dynamic BusyBox in $BBC (first run only)"
		rm -rf "$BBC/src" "$BBC/bb" "$BBC/install"
		cp -a /var/lab/src/busybox-1.37.0 "$BBC/src" && mkdir -p "$BBC/bb" || exit 1
		make -s -C "$BBC/src" O="$BBC/bb" defconfig >/dev/null || exit 1
		sed -i 's/^CONFIG_TC=y/# CONFIG_TC is not set/; s/^CONFIG_SHA1_HWACCEL=y/# CONFIG_SHA1_HWACCEL is not set/; s/^CONFIG_SHA256_HWACCEL=y/# CONFIG_SHA256_HWACCEL is not set/' "$BBC/bb/.config"
		make -s -C "$BBC/bb" oldconfig </dev/null >/dev/null || exit 1
		make -s -C "$BBC/bb" -j3 EXTRA_CFLAGS=-std=gnu11 >"$BBC/build.log" 2>&1 || exit 1
		make -s -C "$BBC/bb" EXTRA_CFLAGS=-std=gnu11 CONFIG_PREFIX="$BBC/install" install >>"$BBC/build.log" 2>&1 || exit 1
	fi
) 9>"$BBC/.lock" || fail "busybox build (see $BBC/build.log)"

# ---- 3. rootfs skeleton: FHS dirs + BusyBox links + /etc ----
mkdir -p "$R" && cp -a "$BBC/install/." "$R/" && rm -f "$R/linuxrc"
mkdir -p "$R"/{dev,proc,sys,tmp,run,root,mnt,etc/init.d,var/log,usr/lib}
chmod 1777 "$R/tmp"
install -m 644 "$W/inittab" "$W/fstab" "$W/hostname" "$R/etc/"
install -m 755 "$W/rcS" "$R/etc/init.d/rcS"
install -m 755 "$W/b12-test.sh" "$R/etc/b12-test.sh"
printf 'root:x:0:0:root:/root:/bin/sh\n' >"$R/etc/passwd"
printf 'root:x:0:\ntty:x:5:\n' >"$R/etc/group"

# ---- 4. Part C package: libsensor into staging (with SONAME symlinks), app into the rootfs ----
(
	cd "$ST/usr/lib/sensor" || exit 1
	gcc $STRICT -fPIC -shared -Wl,-soname,libsensor.so.1 "$W/libsensor.c" -o libsensor.so.1.0.0 || exit 1
	ln -sf libsensor.so.1.0.0 libsensor.so.1 && ln -sf libsensor.so.1 libsensor.so
	gcc $STRICT "$W/sensor_app.c" -L. -lsensor -Wl,-rpath,'$ORIGIN/../lib/sensor' -o "$R/usr/bin/sensor_app"
) || fail "sensor package build"

# ---- 5. the lesson: stage the closure of the binaries with <variant>.py ----
python3 -I "$W/$V.py" stage "$R" --from "$ST" --from / /bin/busybox /usr/bin/sensor_app >"$W/stage.log" 2>&1
STAGE_RC=$?
cat "$W/stage.log"
case "$SCEN" in
nolib) rm -f "$R/lib/aarch64-linux-gnu/libm.so.6"; echo "demo: removed /lib/aarch64-linux-gnu/libm.so.6" ;;
nointerp) rm -f "$R/lib/ld-linux-aarch64.so.1"; echo "demo: removed /lib/ld-linux-aarch64.so.1" ;;
esac

# ---- 6. ext4 image without root: fakeroot makes chown/mknod 'work' for mkfs.ext4 -d ----
IMG=$W/rootfs.ext4
truncate -s 32M "$IMG"
fakeroot -- sh -c "chown -R 0:0 '$R' && mknod -m 600 '$R/dev/console' c 5 1 && mknod -m 666 '$R/dev/null' c 1 3 \
	&& mkfs.ext4 -q -F -L b12root -d '$R' '$IMG'" || fail "mkfs.ext4 -d"

# ---- 7. boot: kernel mounts /dev/vda as /, runs /sbin/init (BusyBox), inittab does the rest ----
LOG=$W/console.log
timeout "$TMO" qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 1 -m 512M -nographic -no-reboot \
	-kernel "$IMAGE" -drive file="$IMG",if=virtio,format=raw \
	-append "console=ttyAMA0 root=/dev/vda rw panic=-1" </dev/null 2>&1 | tr -d '\r' >"$LOG"
QRC=${PIPESTATUS[0]}

if [ "$ARG" = demo ]; then
	grep -vE '^\[ *[0-9.]+\] +(CPU|Hardware name|pstate|pc :|lr :|sp :|x[0-9]+|Call trace|Workqueue|SMP:|Kernel Offset|PHYS_OFFSET|Features|Memory Limit)' "$LOG" | tail -40
	if [ "${KEEP:-0}" = 1 ]; then echo "=== qemu exit $QRC; kept: $W ==="; else echo "=== qemu exit $QRC ==="; rm -rf "$W"; fi
	exit 0
fi

# ---- 8. verdict: counts and markers only (never timing) ----
grep -E '^(PASS|FAIL): guest' "$LOG"
G_P=$(grep -c '^PASS: guest' "$LOG")
G_F=$(grep -c '^FAIL: guest' "$LOG")
X_P=0 X_F=0
mark() { if grep -qE "$2" "$LOG"; then echo "PASS: boot $1"; X_P=$((X_P + 1)); else echo "FAIL: boot $1"; X_F=$((X_F + 1)); fi; }
if [ "$STAGE_RC" -eq 0 ] && grep -q '^  staged /lib/ld-linux-aarch64.so.1$' "$W/stage.log"; then
	echo "PASS: stage: no missing libs, interpreter staged"; X_P=$((X_P + 1))
else
	echo "FAIL: stage (exit $STAGE_RC, interpreter staged? see $W/stage.log)"; X_F=$((X_F + 1))
fi
mark "kernel mounted ext4 root from vda" 'VFS: Mounted root \(ext4 filesystem\)'
mark "kernel ran /sbin/init" 'Run /sbin/init as init process'
mark "inittab sysinit #1 ran" '^B12: inittab sysinit 1 of 2'
mark "rcS mount -a rc=0" '^B12: rcS mount -a rc=0'
mark "guest finished its checks" '^GUEST_DONE'
mark "inittab shutdown action ran on poweroff" '^B12: inittab shutdown action'
mark "clean power-off (reboot: Power down)" 'reboot: Power down'
if grep -qE 'Kernel panic|BUG:|Oops|Call trace' "$LOG"; then
	echo "FAIL: boot no panic/BUG/Oops"; X_F=$((X_F + 1))
	grep -E 'Kernel panic|error while loading|Failed to execute|BUG:|Oops' "$LOG" | head -5
else
	echo "PASS: boot no panic/BUG/Oops"; X_P=$((X_P + 1))
fi

P=$((PY_P + G_P + X_P))
F=$((PY_F + G_F + X_F))
echo "$P/$((P + F)) passed (python $PY_P/$((PY_P + PY_F)), guest $G_P/$((G_P + G_F)), boot $X_P/$((X_P + X_F)); qemu exit $QRC)"
if [ "$F" -eq 0 ] && [ "$G_P" -gt 0 ]; then
	echo "RESULT: PASS ($ARG)"
	rm -rf "$W"
	exit 0
fi
echo "RESULT: FAIL ($ARG) log: $LOG"
exit 1
