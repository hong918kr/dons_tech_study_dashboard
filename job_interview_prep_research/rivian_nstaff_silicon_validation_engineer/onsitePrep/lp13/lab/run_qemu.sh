#!/bin/bash
# LP13 lab: boot the lab kernel (Image-trace) under QEMU -machine virt with a real
# PCIe topology and run a guest script. Run INSIDE the Lima VM (limactl shell elinux).
#   usage: [EXTRA="file1 file2"] run_qemu.sh <guest-script> [--inject-aer]   (EXTRA files land in guest /)
# Topology:
#   00:01.0 root port (x-speed=16,x-width=4) -> 01:00.0 nvme
#   00:02.0 root port (x-speed=8, x-width=1) -> 02:00.0 e1000e
#   00:03.0 root port -> 03:00.0 XIO3130 upstream -> 04:00.0 downstream -> 05:00.0 edu (conventional PCI)
#   00:04.0 root port, EMPTY (shows DLActive- / PresDet-)
# Host root FS of the Lima VM may be read-only -> everything lives in /tmp/LP13.
set -e
W=/tmp/LP13; mkdir -p $W; HERE=$(cd "$(dirname "$0")" && pwd)
cp "$HERE/../pcie_snapshot.py" $W/; cp "$HERE/build_initramfs.sh" $W/
GUEST="$1"; [ -f "$GUEST" ] || { echo "usage: $0 guest.sh [--inject-aer]"; exit 1; }
cp "$GUEST" $W/guest.sh
for f in $EXTRA; do cp "$f" $W/; done
EXTRA="$(for f in $EXTRA; do echo -n "$W/$(basename $f) "; done)" bash $W/build_initramfs.sh > /dev/null
[ -f $W/nvme.img ] || truncate -s 64M $W/nvme.img
rm -f $W/mon.sock $W/out.txt
timeout 240 qemu-system-aarch64 -machine virt,gic-version=3 -cpu cortex-a57 -smp 2 -m 1G \
  -display none -monitor unix:$W/mon.sock,server,nowait -serial file:$W/out.txt \
  -kernel /var/lab/out/Image-trace -initrd $W/i.gz -append "console=ttyAMA0 LAB_CMD=sh~/guest.sh" \
  -device pcie-root-port,id=rp1,chassis=1,slot=1,x-speed=16,x-width=4 \
  -drive file=$W/nvme.img,if=none,id=nv0,format=raw -device nvme,serial=lp13nvme,drive=nv0,bus=rp1 \
  -device pcie-root-port,id=rp2,chassis=2,slot=2,x-speed=8,x-width=1 \
  -netdev user,id=n0 -device e1000e,bus=rp2,netdev=n0 \
  -device pcie-root-port,id=rp3,chassis=3,slot=3 -device x3130-upstream,id=up1,bus=rp3 \
  -device xio3130-downstream,id=dn1,bus=up1,chassis=4,slot=0 -device edu,bus=dn1 \
  -device pcie-root-port,id=rp4,chassis=5,slot=4 &
QPID=$!
if [ "$2" = "--inject-aer" ]; then
  for i in $(seq 1 120); do grep -q READY_FOR_INJECT $W/out.txt 2>/dev/null && break; sleep 1; done
  # QEMU HMP: correctable (-c) Receiver Error (bit 0) then Bad TLP (bit 6) on root port rp1
  printf 'pcie_aer_inject_error -c rp1 0x1\npcie_aer_inject_error -c rp1 0x40\n' \
    | socat - UNIX-CONNECT:$W/mon.sock > /dev/null
fi
wait $QPID || true
tr -d '\r' < $W/out.txt
