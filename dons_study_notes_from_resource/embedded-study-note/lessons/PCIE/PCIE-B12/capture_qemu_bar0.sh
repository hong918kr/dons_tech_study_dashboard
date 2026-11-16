#!/bin/sh
# PCIE-B12: runs INSIDE the QEMU guest (lab BusyBox initramfs, LAB_CMD=sh~/<this>.sh). qemu command line: capture_qemu_nvme_bar0.txt
echo "=== PCI devices (sysfs) ==="
for d in /sys/bus/pci/devices/*; do echo "$(basename $d) $(cat $d/vendor):$(cat $d/device) class=$(cat $d/class)"; done
D=""
for d in /sys/bus/pci/devices/*; do [ "$(cat $d/class)" = "0x010802" ] && D=$d; done
echo "NVMe function: $D"
echo 1 > $D/enable
echo "resource line0: $(head -1 $D/resource)"
B=$(head -1 $D/resource | cut -d' ' -f1)
echo "BAR0 base: $B"
echo "=== config 0x00..0x3f ==="
hexdump -C -n 64 $D/config
echo "=== BAR0 registers (devmem 32-bit reads) ==="
for off in 0 4 8 12 16 20 24 28 32 36 40 44 48 52 56 60 64 68 72 76 80 84 104 4096 4100 4104 4108; do
  v=$(devmem $((B+off)) 32)
  printf "off 0x%04x = %s\n" $off $v
done
echo "=== done ==="
