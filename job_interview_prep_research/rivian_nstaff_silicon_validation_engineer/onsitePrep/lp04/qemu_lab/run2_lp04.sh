#!/bin/bash
cd /tmp/LP03
APPEND="console=ttyAMA0 LAB_CMD=sh~/lab.sh $1"
timeout 150 qemu-system-aarch64 -machine virt,highmem=off -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd i_lp04.gz -append "$APPEND" \
  -object memory-backend-ram,id=hm,size=1G \
  -device pcie-root-port,id=rp1,chassis=1,slot=1 \
  -device ivshmem-plain,memdev=hm,bus=rp1 \
  -device pcie-root-port,id=rp2,chassis=2,slot=2,x-speed=16,x-width=4 \
  -device virtio-net-pci,disable-legacy=on,bus=rp2 \
  -nic none 2>&1 | tr -d '\r'
