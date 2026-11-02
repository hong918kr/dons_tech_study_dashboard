#!/bin/bash
# Topology:
#  rp1 (00:01.0, x-speed=16 x-width=4) -> nvme (01:00.0)
#  rp2 (00:02.0, x-speed=8 x-width=4) -> x3130 USP -> DSP0 -> e1000e ; DSP1 -> edu ; 
#  rp3 (00:03.0) empty
cd /tmp/LP03
timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd i.gz \
  -append "console=ttyAMA0 LAB_CMD=sh~/lab.sh" \
  -device pcie-root-port,id=rp1,chassis=1,slot=1,x-speed=16,x-width=4 \
  -device nvme,serial=deadbeef,bus=rp1 \
  -device pcie-root-port,id=rp2,chassis=2,slot=2,x-speed=8,x-width=4 \
  -device x3130-upstream,id=usp,bus=rp2 \
  -device xio3130-downstream,id=dsp0,bus=usp,chassis=3,slot=0 \
  -device xio3130-downstream,id=dsp1,bus=usp,chassis=4,slot=1 \
  -device e1000e,bus=dsp0 \
  -device edu,bus=dsp1 \
  -device pcie-root-port,id=rp3,chassis=5,slot=3 \
  -nic none 2>&1 | tr -d '\r'
