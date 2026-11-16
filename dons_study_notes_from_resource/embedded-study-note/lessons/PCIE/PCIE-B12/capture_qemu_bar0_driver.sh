#!/bin/sh
# PCIE-B12: runs INSIDE the QEMU guest (lab BusyBox initramfs, LAB_CMD=sh~/<this>.sh). qemu command line: capture_qemu_nvme_bar0.txt
D=""
for d in /sys/bus/pci/devices/*; do [ "$(cat $d/class)" = "0x010802" ] && D=$d; done
B=$(head -1 $D/resource | cut -d' ' -f1)
dump() {
  echo "--- $1 ---"
  for off in 0 4 8 20 28 36 40 44 48 52 4096 4100 4104 4108; do
    printf "off 0x%04x = %s\n" $off $(devmem $((B+off)) 32)
  done
}
echo 1 > $D/enable
dump "before driver"
insmod /nvme-core.ko && insmod /nvme.ko
sleep 3
dmesg | grep -i nvme | tail -5
dump "after nvme.ko probe"
echo "=== done ==="
