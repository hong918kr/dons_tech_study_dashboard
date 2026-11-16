#!/usr/bin/env bash
# ENV: linux (Lima VM elinux) — PCIE-B06 실제 출력 수집 스크립트. 읽기만 한다 (setpci 쓰기 없음, insmod 없음).
#   capture.sh vm     → 이 VM 의 lspci/setpci/sysfs 출력           (md 의 "VM 실측" 블록 = capture_vm.txt)
#   capture.sh qemu   → QEMU 게스트(pcie-root-port + nvme) config 덤프 + VM 의 lspci -F 디코드 (capture_qemu.txt)
#   capture.sh vars   → qemu 덤프의 LnkCap/LnkSta/AER 바이트를 고친 "가상 덤프" 4종을 lspci -F 로 디코드 (capture_variants.txt)
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
W=/var/tmp/PCIE-B06_work
mkdir -p "$W"
B=${BDF:-00:06.0}
run() { echo "\$ $*"; eval "$@" 2>&1; echo; }
case "${1:-}" in
vm)
	run "lspci -tv"
	run "lspci -tvn"
	run "lspci -n"
	run "lspci -vvv -s $B            # 비root"
	run "sudo lspci -vvv -s $B       # root"
	run "lspci -xxx -s $B            # 비root: 64바이트만"
	run "sudo lspci -xxxx -s $B"
	for r in COMMAND STATUS VENDOR_ID.w DEVICE_ID.w CLASS_DEVICE.w HEADER_TYPE.b BASE_ADDRESS_0 BASE_ADDRESS_1 0x34.b CAP_MSIX+2.w CAP_EXP+0x12.w ECAP_AER+4.l; do
		echo "\$ sudo setpci -s $B $r"; sudo setpci -s $B $r 2>&1; echo "(exit $?)"
	done; echo
	D=/sys/bus/pci/devices/0000:$B
	for f in vendor device class subsystem_vendor subsystem_device revision irq enable; do echo "$D/$f: $(cat $D/$f)"; done
	echo "readlink $D/driver: $(readlink $D/driver)"
	echo "config size: user $(head -c 4096 $D/config | wc -c) B, root $(sudo head -c 4096 $D/config | wc -c) B"
	run "cat $D/resource"
	run "sudo grep -E 'ECAM|PCI Bus|$B' /proc/iomem"
	run "head -2 /proc/iomem               # 비root: 주소가 0 으로 가려진다"
	;;
qemu)
	R=$W/r_cap; rm -rf "$R"; mkdir -p "$R"; cp -a /var/lab/build/rootfs/. "$R/"
	cat > "$R/dump.sh" <<'G'
#!/bin/sh
for d in /sys/bus/pci/devices/*; do
  b=${d##*/}; drv=$(readlink $d/driver 2>/dev/null)
  echo "#DEV $b vendor=$(cat $d/vendor) device=$(cat $d/device) class=$(cat $d/class) driver=${drv##*/} cfgsize=$(wc -c < $d/config)"
done
for d in /sys/bus/pci/devices/*; do
  b=${d##*/}; echo "${b#0000:} dev"; hexdump -v -e '"%02_ax: " 16/1 "%02x " "\n"' $d/config; echo
done
echo "#IOMEM (guest /proc/iomem)"; grep -iE "pci|ecam|virtio" /proc/iomem | sed "s/^/#  /"
G
	chmod +x "$R/dump.sh"
	(cd "$R" && find . | cpio -o -H newc --quiet | gzip -1) > "$W/i_cap.gz"
	[ -f "$W/disk.img" ] || truncate -s 64M "$W/disk.img"
	timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 1 -m 512M -nographic -no-reboot \
		-kernel /var/lab/out/Image -initrd "$W/i_cap.gz" \
		-device pcie-root-port,id=rp1,chassis=1,slot=1 \
		-drive if=none,id=d0,file="$W/disk.img",format=raw -device nvme,serial=esn01,drive=d0,bus=rp1 \
		-append "console=ttyAMA0 quiet LAB_CMD=/dump.sh" </dev/null 2>&1 | tr -d '\r' > "$W/qemu_console.txt"
	rm -rf "$R"
	grep -E '^[0-9a-f]{2}:[0-9a-f]{2}\.[0-7] dev$|^[0-9a-f]{2,3}: ' "$W/qemu_console.txt" | sed '/^00:00.0 dev$/!s/^\([0-9a-f][0-9a-f]:[0-9a-f][0-9a-f]\.[0-7] dev\)$/\n\1/' > "$W/qemu_pcie_nvme.dump"
	echo "# QEMU 10.2 virt + pcie-root-port + nvme, guest Linux 6.18.55 (lab Image, nvme.ko 미적재)"
	grep -E '^#' "$W/qemu_console.txt"; echo
	run "lspci -F $W/qemu_pcie_nvme.dump -tv"
	run "lspci -F $W/qemu_pcie_nvme.dump -tvn"
	run "lspci -F $W/qemu_pcie_nvme.dump -nn"
	run "lspci -F $W/qemu_pcie_nvme.dump -vvv -s 00:02.0"
	run "lspci -F $W/qemu_pcie_nvme.dump -vvv -s 01:00.0"
	run "lspci -F $W/qemu_pcie_nvme.dump -xxx -s 00:02.0"
	run "lspci -F $W/qemu_pcie_nvme.dump -xxx -s 01:00.0"
	;;
vars)
	python3 -I "$SRC/make_variants.py" "$W/qemu_pcie_nvme.dump" "$W" || exit 1
	for v in x2 gen1; do
		run "lspci -F $W/var_$v.dump -vvv -s 00:02.0 | grep -E 'LnkCap:|LnkSta:|DLActive'"
		run "lspci -F $W/var_$v.dump -vvv -s 01:00.0 | grep -E 'LnkCap:|LnkSta:|DLActive'"
	done
	run "lspci -F $W/var_dldown.dump -vvv -s 00:02.0 | grep -E 'LnkSta:|DLActive|SltSta:'"
	run "lspci -F $W/var_over.dump -vvv -s 01:00.0 | grep -E 'LnkCap:|LnkSta:'"
	run "lspci -F $W/var_cmplto.dump -vvv -s 00:02.0 | grep -A1 'UESta:'"
	;;
*) echo "usage: capture.sh vm|qemu|vars"; exit 2 ;;
esac
