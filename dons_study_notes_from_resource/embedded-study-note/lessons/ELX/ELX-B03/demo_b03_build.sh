#!/usr/bin/env bash
# ENV: linux  (VM; needs the one-time 18-minute build below, so it is NOT part of check.sh)
# ELX-B03 demo: inspect a real build of defconfig + req_good.config (GPIO_SIM=m, DEBUG_INFO_NONE,
# IKCONFIG=m, LOCALVERSION=-b03) and boot it in QEMU.
#   one-time build (done for the lesson, 18:02 wall, -j4 nice 15, shared VM):
#     cd /var/lab/src/linux-6.18.55 && make O=$B defconfig && cat $B/.config req_good.config > $B/.config.new \
#       && mv $B/.config.new $B/.config && make O=$B olddefconfig && make -j4 O=$B Image modules
#   usage: demo_b03_build.sh [BUILD_DIR]      (default /var/tmp/ELX-B03_work/build)
set -u
B=${1:-/var/tmp/ELX-B03_work/build}
KSRC=/var/lab/src/linux-6.18.55
LAB=/var/lab/out/linux-build
W=/tmp/ELX-B03_demo.$$
mkdir -p "$W"
trap 'rm -rf "$W"' EXIT
REL=$(cat "$B/include/config/kernel.release")

echo "### artifacts (release $REL)"
ls -l "$B/vmlinux" "$B/arch/arm64/boot/Image" "$LAB/vmlinux" /var/lab/out/Image | awk '{print $5, $9}'
file "$B/vmlinux" | sed 's/BuildID[^,]*, //'
echo "debug sections b03: $(readelf -S "$B/vmlinux" | grep -c '\.debug_')  lab: $(readelf -S "$LAB/vmlinux" | grep -c '\.debug_')"
echo "modules.order: $(wc -l <"$B/modules.order")  modules.builtin: $(wc -l <"$B/modules.builtin")"
grep -nE 'gpio-sim|dev-sync-probe|kernel/configs' "$B/modules.order" "$B/modules.builtin"

echo "### modinfo"
for k in drivers/gpio/gpio-sim.ko drivers/gpio/dev-sync-probe.ko kernel/configs.ko; do
	modinfo "$B/$k" | grep -E '^(filename|description|depends|intree|name|vermagic):' | sed "s#$B/##"
done

echo "### extract-ikconfig"
"$KSRC/scripts/extract-ikconfig" "$B/arch/arm64/boot/Image" >/dev/null 2>"$W/e1"
echo "Image: rc=$? $(cat "$W/e1")"
echo "configs.ko: $("$KSRC/scripts/extract-ikconfig" "$B/kernel/configs.ko" | grep -E '^CONFIG_(LOCALVERSION|IKCONFIG|GPIO_SIM)=')" | tr '\n' ' '
echo

echo "### guest 1: b03 Image + its own modules"
R=$W/r1
cp -a /var/lab/build/rootfs/. "$R/"
make -s -C "$B" INSTALL_MOD_PATH="$R" INSTALL_MOD_STRIP=1 modules_install >/dev/null 2>&1 \
	|| echo "modules_install failed"
du -sh "$R/lib/modules/$REL" | cut -f1 | sed 's/^/installed modules: /'
cp "$B/drivers/gpio/gpio-sim.ko" "$R/gpio-sim-raw.ko"
cat >"$R/test.sh" <<'G'
#!/bin/sh
echo "uname -r: $(uname -r)"
ls /proc/config.gz 2>&1
modprobe configs && echo "after modprobe configs:" && ls -l /proc/config.gz | cat && zcat /proc/config.gz | grep -E '^CONFIG_(LOCALVERSION|GPIO_SIM|IKCONFIG)='
insmod /gpio-sim-raw.ko; echo "insmod gpio-sim (no deps) rc=$?"
dmesg | grep -E 'gpio_sim|Unknown symbol' | tail -3
modprobe gpio-sim; echo "modprobe gpio-sim rc=$?"
grep -E '^(gpio_sim|dev_sync_probe|configs) ' /proc/modules | cut -d' ' -f1-4
G
chmod +x "$R/test.sh"
(cd "$R" && find . | cpio -o -H newc --quiet | gzip -1) >"$W/r1.gz"
timeout 150 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 1 -m 512M -nographic \
	-kernel "$B/arch/arm64/boot/Image" -initrd "$W/r1.gz" \
	-append "console=ttyAMA0 loglevel=3 LAB_CMD=/test.sh" </dev/null 2>&1 | tr -d '\r' |
	grep -vE '^\[ *[0-9.]+\] (reboot|Freeing|Run /init)' | sed -n '/^lab initramfs/,$p'

echo "### guest 2: lab Image (6.18.55) + the b03 dev-sync-probe.ko"
R=$W/r2
cp -a /var/lab/build/rootfs/. "$R/"
cp "$B/drivers/gpio/dev-sync-probe.ko" "$R/"
cat >"$R/test.sh" <<'G'
#!/bin/sh
insmod /dev-sync-probe.ko; echo "insmod rc=$?"
dmesg | tail -2
G
chmod +x "$R/test.sh"
(cd "$R" && find . | cpio -o -H newc --quiet | gzip -1) >"$W/r2.gz"
timeout 150 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 1 -m 512M -nographic \
	-kernel /var/lab/out/Image -initrd "$W/r2.gz" \
	-append "console=ttyAMA0 loglevel=3 LAB_CMD=/test.sh" </dev/null 2>&1 | tr -d '\r' |
	grep -vE '^\[ *[0-9.]+\] (reboot|Freeing|Run /init)' | sed -n '/^lab initramfs/,$p'

echo "### guest 3: lab Image-sub (6.18.55-sub, same struct module) + lab gpio-aggregator.ko (6.18.55)"
R=$W/r3
cp -a /var/lab/build/rootfs/. "$R/"
cp "$LAB/drivers/gpio/gpio-aggregator.ko" "$R/"
cat >"$R/test.sh" <<'G'
#!/bin/sh
insmod /gpio-aggregator.ko; echo "insmod rc=$?"
dmesg | tail -1
G
chmod +x "$R/test.sh"
(cd "$R" && find . | cpio -o -H newc --quiet | gzip -1) >"$W/r3.gz"
timeout 150 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 1 -m 512M -nographic \
	-kernel /var/lab/out/Image-sub -initrd "$W/r3.gz" \
	-append "console=ttyAMA0 loglevel=3 LAB_CMD=/test.sh" </dev/null 2>&1 | tr -d '\r' |
	grep -vE '^\[ *[0-9.]+\] (reboot|Freeing|Run /init)' | sed -n '/^lab initramfs/,$p'
