#!/bin/bash
# runs in Lima VM: boot QEMU virt (GICv3+ITS, SMMUv3) with root ports + e1000e + edu, then inject AER via monitor
set -u
W=/tmp/LP07
cd $W
rm -rf r && mkdir r && cp -a /var/lab/build/rootfs/. r/ && cp run.sh r/run.sh && chmod +x r/run.sh
(cd r && find . | cpio -o -H newc --quiet | gzip) > i.gz
M="-machine virt,gic-version=3,iommu=smmuv3 -cpu cortex-a57 -smp 2 -m 512M"
D="-device pcie-root-port,id=rp1,chassis=1,slot=1 -device e1000e,bus=rp1,id=nic1,netdev=n0 -netdev user,id=n0 -device pcie-root-port,id=rp2,chassis=2,slot=2 -device edu,bus=rp2,id=edu1"
qemu-system-aarch64 $M $D -machine dumpdtb=virt.dtb >/dev/null 2>&1; dtc -I dtb -O dts virt.dtb > virt.dts 2>/dev/null
rm -f mon.sock out.txt
timeout 170 qemu-system-aarch64 $M $D -display none -kernel /var/lab/out/Image -initrd i.gz \
  -monitor unix:$W/mon.sock,server,nowait -serial file:$W/out.txt \
  -append "console=ttyAMA0 LAB_CMD=sh~/run.sh" &
QP=$!
for i in $(seq 1 120); do grep -q READY_FOR_INJECT out.txt 2>/dev/null && break; sleep 1; done
mon() { python3 - "$1" <<PY
import socket,sys,time
s=socket.socket(socket.AF_UNIX); s.connect("$W/mon.sock"); time.sleep(0.3); s.recv(65536)
s.send((sys.argv[1]+"\n").encode()); time.sleep(0.8); print(s.recv(65536).decode(errors="replace")); s.close()
PY
}
mon "pcie_aer_inject_error -c rp1 0x1" > mon.txt
sleep 2
mon "pcie_aer_inject_error -c rp1 0x40" >> mon.txt
sleep 2
mon "pcie_aer_inject_error -c rp1 0x40" >> mon.txt
sleep 3
mon "pcie_aer_inject_error rp1 0x100000" >> mon.txt
sleep 8
mon "pcie_aer_inject_error rp1 0x40000" >> mon.txt
wait $QP
echo "qemu exit $?"
