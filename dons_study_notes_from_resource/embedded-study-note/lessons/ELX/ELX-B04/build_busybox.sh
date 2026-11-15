#!/usr/bin/env bash
# ENV: linux
# ELX-B04: build a static BusyBox 1.37.0 from a private copy of the lab source (runs inside the Lima VM).
#   bash build_busybox.sh            -> /var/tmp/ELX-B04_work/bb/busybox
# check.sh uses this binary when it exists (BB=... overrides), else the lab's identical build.
set -eu
SRC=/var/lab/src/busybox-1.37.0
W=/var/tmp/ELX-B04_work
J=${J:-2}                       # VM shared by other sessions: keep -j small
mkdir -p "$W"
rm -rf "$W/bb-src" "$W/bb"
cp -a "$SRC" "$W/bb-src"        # lab source is read-only -> private copy
mkdir -p "$W/bb"                # O= dir must exist
cd "$W/bb-src"
make -s O="$W/bb" defconfig >"$W/bb-defconfig.log" 2>&1
cd "$W/bb"
# STATIC=y : initramfs has no libc.so / ld-linux -> must be static
# SHA*_HWACCEL off : x86-only SHA-NI code; on arm64 =y fails: 'sha1_process_block64_shaNI' undeclared
# TC off   : tc.c does not compile against the VM's new kernel headers
sed -i 's/^# CONFIG_STATIC is not set/CONFIG_STATIC=y/;
        s/^CONFIG_TC=y/# CONFIG_TC is not set/;
        s/^CONFIG_SHA1_HWACCEL=y/# CONFIG_SHA1_HWACCEL is not set/;
        s/^CONFIG_SHA256_HWACCEL=y/# CONFIG_SHA256_HWACCEL is not set/' .config
make -s oldconfig </dev/null >/dev/null
grep -E '^CONFIG_STATIC=|HWACCEL|^# CONFIG_TC is' .config
start=$(date +%s)
make -s -j"$J" EXTRA_CFLAGS="-std=gnu11" >build.log 2>&1 || { tail -30 build.log; exit 1; }
echo "build time: $(( $(date +%s) - start )) s (-j$J, shared VM)"
file busybox
ls -l busybox
echo "applets: $(./busybox --list | wc -l)"
