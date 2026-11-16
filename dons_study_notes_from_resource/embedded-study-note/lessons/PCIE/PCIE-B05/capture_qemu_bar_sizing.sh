#!/bin/sh
# PCIE-B05 capture: all-1s BAR sizing via sysfs config writes (QEMU guest)
rd32() { dd if=$1 bs=4 skip=$2 count=1 2>/dev/null | od -An -tx4 | tr -d ' '; }
rd16() { dd if=$1 bs=2 skip=$2 count=1 2>/dev/null | od -An -tx2 | tr -d ' '; }
echo "== lspci =="; lspci
for D in /sys/bus/pci/devices/*; do
  B=$(basename $D); V=$(cat $D/vendor); DV=$(cat $D/device); C=$(cat $D/class)
  echo "== $B vendor=$V device=$DV class=$C"
done
D=$(grep -l 0x010802 /sys/bus/pci/devices/*/class | head -1); D=$(dirname $D)
echo "== NVMe at $(basename $D)"
echo "resource file (start end flags):"; head -6 $D/resource
CMD=$(rd16 $D/config 2); echo "COMMAND before = 0x$CMD"
dd if=$D/config bs=2 skip=2 count=1 2>/dev/null > /tmp/cmd.orig
printf '\000\000' | dd of=$D/config bs=2 seek=2 count=1 conv=notrunc 2>/dev/null
echo "COMMAND decode off = 0x$(rd16 $D/config 2)"
i=0
while [ $i -lt 6 ]; do
  N=$((4+i))
  dd if=$D/config bs=4 skip=$N count=1 2>/dev/null > /tmp/bar.orig
  O=$(rd32 $D/config $N)
  printf '\377\377\377\377' | dd of=$D/config bs=4 seek=$N count=1 conv=notrunc 2>/dev/null
  M=$(rd32 $D/config $N)
  dd if=/tmp/bar.orig of=$D/config bs=4 seek=$N count=1 conv=notrunc 2>/dev/null
  R=$(rd32 $D/config $N)
  echo "BAR$i @0x$(printf %02x $((N*4))): orig=0x$O  after_all1s=0x$M  restored=0x$R"
  i=$((i+1))
done
dd if=/tmp/cmd.orig of=$D/config bs=2 seek=2 count=1 conv=notrunc 2>/dev/null
echo "COMMAND restored = 0x$(rd16 $D/config 2)"
