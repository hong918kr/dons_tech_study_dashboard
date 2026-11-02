#!/bin/sh
export PATH=/usr/bin:/bin:/sbin:/usr/sbin
echo "=== DMESG-PCI"; dmesg | grep -iE "pci|ecam|bwctrl|e1000e|pcieport"
echo "=== LSPCI-TV"; lspci -tv
echo "=== LSPCI-NN"; lspci -nn
echo "=== LSPCI-VVV"; lspci -vvv
echo "=== IOMEM"; cat /proc/iomem
echo "=== IOPORTS"; cat /proc/ioports
echo "=== SYSFS-LINK"
for d in /sys/bus/pci/devices/*; do
  printf "%s cur=%s x%s max=%s x%s\n" ${d##*/} "$(cat $d/current_link_speed 2>/dev/null)" "$(cat $d/current_link_width 2>/dev/null)" "$(cat $d/max_link_speed 2>/dev/null)" "$(cat $d/max_link_width 2>/dev/null)"
done
echo "=== EXTRA"; [ -f /extra.sh ] && sh /extra.sh
echo "=== END"
