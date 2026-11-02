#!/bin/bash
W=/tmp/LP08; cd $W
gcc -static -O2 -Wall -Wextra -Werror -o edu_dma_fault edu_dma_fault.c || exit 1
rm -rf r && mkdir r && cp -a /var/lab/build/rootfs/. r/ && cp run8.sh r/run.sh && cp edu_dma_fault r/ && chmod +x r/run.sh r/edu_dma_fault
(cd r && find . | cpio -o -H newc --quiet | gzip) > i.gz
timeout 120 qemu-system-aarch64 -machine virt,gic-version=3,iommu=smmuv3 -cpu cortex-a57 -smp 2 -m 512M -display none -serial file:$W/out.txt \
 -device pcie-root-port,id=rp1,chassis=1,slot=1 -device e1000e,bus=rp1,id=nic1,netdev=n0 -netdev user,id=n0 \
 -device pcie-root-port,id=rp2,chassis=2,slot=2 -device edu,bus=rp2,id=edu1 \
 -kernel /var/lab/out/Image -initrd i.gz -append "console=ttyAMA0 quiet LAB_CMD=sh~/run.sh"
tr -d '\r' < out.txt > o.txt; echo "qemu exit $?"
