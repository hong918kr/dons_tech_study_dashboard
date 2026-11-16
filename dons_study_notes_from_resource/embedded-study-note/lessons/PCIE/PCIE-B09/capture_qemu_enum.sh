#!/bin/bash
# PCIE-B09 real capture recipe (run INSIDE the Lima VM: limactl shell elinux -- bash <this file>)
# Builds the same topology as the lesson's Topology A in QEMU, lets the real Linux 6.18.55 kernel
# enumerate it (ECAM pci-host-generic on the virt machine), dumps every function's config space
# from /sys/bus/pci/devices/*/config, then renders lspci -tv from that dump with the VM's pciutils.
# Output was saved to capture_qemu_enum.txt (2026-11-15). All devices are QEMU-emulated.
set -e
W=/tmp/PCIE-B09_work
mkdir -p $W && cd $W
cat > dump.sh <<'EOS'
#!/bin/sh
echo "=== DMESG-PCI ==="
dmesg | grep -iE "pci|bus " | grep -viE "virtio-mmio" | head -120
echo "=== DUMP ==="
for d in /sys/bus/pci/devices/*; do
  b=${d##*/}; b=${b#0000:}
  echo "$b dev"
  hexdump -v -n 256 -e '"%02_ax: " 16/1 "%02x " "\n"' $d/config
  echo
done
echo "=== END ==="
EOS
rm -rf r && mkdir r && cp -a /var/lab/build/rootfs/. r/ && cp dump.sh r/dump.sh && chmod +x r/dump.sh
(cd r && find . | cpio -o -H newc --quiet | gzip) > i.gz
truncate -s 16M disk.img
#  00:01.0 root port -> 01:00.0 switch USP -> 02:00.0 DSP -> 03:00.0 NVMe
#                                          -> 02:01.0 DSP -> 04:00.0/.1/.3 multi-function NIC (fn2 absent)
#                                          -> 02:02.0 DSP -> (empty slot)
#  00:02.0 root port -> 06:00.0 edu
timeout 150 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 1 -m 512M -nographic -nic none \
  -kernel /var/lab/out/Image -initrd i.gz \
  -device pcie-root-port,id=rp1,chassis=1,slot=1,bus=pcie.0,addr=1.0 \
  -device x3130-upstream,id=usp,bus=rp1 \
  -device xio3130-downstream,id=dsp0,bus=usp,chassis=2,slot=0,addr=0.0 \
  -device xio3130-downstream,id=dsp1,bus=usp,chassis=3,slot=1,addr=1.0 \
  -device xio3130-downstream,id=dsp2,bus=usp,chassis=4,slot=2,addr=2.0 \
  -drive if=none,id=d0,file=$W/disk.img,format=raw -device nvme,serial=esn01,drive=d0,bus=dsp0 \
  -device e1000e,bus=dsp1,addr=0.0,multifunction=on \
  -device e1000e,bus=dsp1,addr=0.1 \
  -device e1000e,bus=dsp1,addr=0.3 \
  -device pcie-root-port,id=rp2,chassis=5,slot=5,bus=pcie.0,addr=2.0 \
  -device edu,bus=rp2 \
  -append "console=ttyAMA0 LAB_CMD=sh~/dump.sh" > qemu_out.txt 2>&1
sed -n '/=== DUMP/,/=== END/p' qemu_out.txt | sed '1d;$d' | tr -d '\r' > dump_lspci.txt
lspci -F dump_lspci.txt -tv
