#!/bin/sh
# LP14 guest script: run link_cycle.py (sysfs backend) against the QEMU nvme at 01:00.0
s() { echo; echo "##### $*"; }
mount -t tmpfs none /tmp 2>/dev/null
insmod /lib/modules/nvme-core.ko; insmod /lib/modules/nvme.ko; sleep 1
cat /sys/bus/pci/devices/0000:01:00.0/reset_method
for m in remove-rescan flr sbr link-disable; do
  s method=$m
  python3 /link_cycle.py --bdf 0000:01:00.0 --port 0000:00:01.0 --method $m --cycles 30 \
      --expect-speed 2.5 --expect-width 1 --timeout 3 --csv /tmp/$m.csv --on-fail 'dmesg | tail -3'
  head -3 /tmp/$m.csv
done
s 'negative control: expect Gen4 x4 on a Gen1 x1 link - harness MUST fail'
python3 /link_cycle.py --bdf 0000:01:00.0 --port 0000:00:01.0 --method remove-rescan --cycles 5 \
      --expect-speed 16 --expect-width 4 --timeout 3
s 'probe: does QEMU drop the link on LnkCtl.LinkDisable?'
echo "before: LnkSta=$(setpci -s 00:01.0 CAP_EXP+12.w)"; setpci -s 00:01.0 CAP_EXP+10.w=10:10; sleep 0.1
echo "LD=1:   LnkSta=$(setpci -s 00:01.0 CAP_EXP+12.w) EP vendor=$(setpci -s 01:00.0 VENDOR_ID)"; setpci -s 00:01.0 CAP_EXP+10.w=0:10; sleep 0.1
echo "LD=0:   LnkSta=$(setpci -s 00:01.0 CAP_EXP+12.w) EP vendor=$(setpci -s 01:00.0 VENDOR_ID)"
s dmesg-tail; dmesg | tail -8
s done
