#!/bin/bash
# LP06 lab 3: user-space driver through uio_pci_generic (Image-dom kernel has CONFIG_UIO=y, uio_pci_generic=m).
set -euo pipefail
SRC=$(cd "$(dirname "$0")" && pwd)
W=/var/tmp/LP06; mkdir -p $W 2>/dev/null || W=/tmp/LP06; mkdir -p $W
cp $SRC/uio_edu.c $SRC/guest_uio.sh $W/ && cd $W
gcc -static -O2 -Wall -Wextra -Werror uio_edu.c -o uio_edu
rm -rf ru && mkdir ru && cp -a /var/lab/build/rootfs/. ru/
cp uio_edu guest_uio.sh ru/ && chmod +x ru/guest_uio.sh
cp /var/lab/out/modules-dom/lib/modules/6.18.55-dom/kernel/drivers/uio/uio_pci_generic.ko ru/
(cd ru && find . | cpio -o -H newc --quiet | gzip) > iu.gz
timeout 150 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image-dom -initrd iu.gz -device edu \
  -append "console=ttyAMA0 loglevel=4 LAB_CMD=sh~/guest_uio.sh" 2>&1 | tr -d '\r' | tee $W/run_uio.log
