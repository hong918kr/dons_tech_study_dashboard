#!/bin/sh
export PATH=/usr/bin:/bin:/sbin:/usr/sbin
echo "--- lspci -vv ivshmem"; lspci -s 01:00.0 -vv 2>/dev/null | head -12
echo "--- lspci -b -vv ivshmem"; lspci -b -s 01:00.0 -vv 2>/dev/null | grep -E "Region|Control"
echo "--- raw BAR2/BAR3"; setpci -s 01:00.0 BASE_ADDRESS_2.l BASE_ADDRESS_3.l
echo "--- enable attempt via sysfs"; echo 1 > /sys/bus/pci/devices/0000:01:00.0/enable; dmesg | tail -3
