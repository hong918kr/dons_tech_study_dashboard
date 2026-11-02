#!/bin/bash
# LP05 lab: shrink the 32-bit MEM window in QEMU virt's DT 'ranges' (0x2eff0000 -> 0x80000 = 512 KiB)
# so edu's 1 MiB BAR0 cannot be placed -> "can't assign; no space" -> driver probe fails.
# Runs in the Lima VM; needs /tmp/LP06/edu_lp06.ko (build with ../lp06/run_lab.sh first).
set -euo pipefail
W=/tmp/LP05; mkdir -p $W; cd $W
sed "s/0x2000000 0x00 0x10000000 0x00 0x10000000 0x00 0x2eff0000/0x2000000 0x00 0x10000000 0x00 0x10000000 0x00 0x80000/" \
    /var/lab/out/virt.dts > small.dts
dtc -q -I dts -O dtb -o small.dtb small.dts
rm -rf r && mkdir r && cp -a /var/lab/build/rootfs/. r/ && cp /tmp/LP06/edu_lp06.ko r/
(cd r && find . | cpio -o -H newc --quiet | gzip) > i.gz
timeout 90 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -dtb small.dtb -initrd i.gz -device edu,dma_mask=0xffffffffff \
  -append 'console=ttyAMA0 loglevel=4 LAB_CMD=dmesg~|~grep~-E~"pci-host|pci~0000|pci_bus|no~space|can.t~assign|failed";grep~-i~pci~/proc/iomem;insmod~/edu_lp06.ko~dma_bits=40;dmesg~|~grep~edu_lp06' \
  2>&1 | tr -d '\r' | tee run_small_window.log
