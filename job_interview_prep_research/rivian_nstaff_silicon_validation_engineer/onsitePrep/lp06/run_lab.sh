#!/bin/bash
# Runs in the Lima VM (elinux). Copies sources to /var/tmp/LP06, builds, packs initramfs, boots QEMU virt + edu.
# usage (from macOS):  limactl shell elinux -- bash /Users/donh/.../onsitePrep/lp06/run_lab.sh [extra qemu -device args]
set -euo pipefail
SRC=$(cd "$(dirname "$0")" && pwd)
W=/var/tmp/LP06
# 2026-10-31: VM root fs was emergency-RO -> fall back to tmpfs /tmp
mkdir -p $W 2>/dev/null || W=/tmp/LP06
mkdir -p $W && cp $SRC/edu_lp06.c $SRC/edu_mmap.c $SRC/Makefile $SRC/guest_lab.sh $SRC/guest_lab2.sh $W/
cd $W
make -s all 2>&1 | grep -v "^$" || true
rm -rf r && mkdir r && cp -a /var/lab/build/rootfs/. r/
cp edu_lp06.ko edu_mmap guest_lab.sh guest_lab2.sh r/ && chmod +x r/guest_lab*.sh
GUEST=${GUEST:-guest_lab.sh}
(cd r && find . | cpio -o -H newc --quiet | gzip) > i.gz
EDU=${EDU:-"-device edu,dma_mask=0xffffffffff"}
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd i.gz $EDU \
  -append "console=ttyAMA0 loglevel=4 LAB_CMD=sh~/$GUEST" 2>&1 | tee $W/run_${GUEST%.sh}.log
