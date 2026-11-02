#!/bin/sh
export PATH=/usr/bin:/bin:/sbin:/usr/sbin
echo "--- cmdline"; cat /proc/cmdline
echo "--- dmesg host bridge + failures"; dmesg | grep -E "host bridge|ranges|IO 0x|MEM 0x|ECAM|root bus resource|no space|failed to assign|can't assign|not assigned|can't enable|1af4|BAR 2|ivshmem" 
echo "--- lspci -tv"; lspci -tv 2>/dev/null
echo "--- ivshmem 01:00.0"; lspci -s 01:00.0 -vv 2>/dev/null | grep -E "^0|Region|Control:|Memory behind|Prefetchable"
echo "--- rp1 00:01.0"; lspci -s 00:01.0 -vv 2>/dev/null | grep -E "Region|Bus:|Memory behind|Prefetchable"
echo "--- virtio-net 02:00.0 link"; lspci -s 02:00.0 -vv 2>/dev/null | grep -E "^0|Express|LnkCap:|LnkSta:" ; lspci -s 00:02.0 -vv 2>/dev/null | grep -E "LnkCap:|LnkSta:" -A1
echo "--- iomem"; cat /proc/iomem | grep -v "reserved\|Kernel\|flash\|pl0"
echo "--- resource file ivshmem"; cat /sys/bus/pci/devices/0000:01:00.0/resource
