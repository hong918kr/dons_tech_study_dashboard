#!/bin/sh
# Variant: edu behind a pcie-root-port, INTx mode (force_intx=1). Started by: GUEST=guest_lab2.sh EDU="..." run_lab.sh
echo "=== [A] devices"
for d in /sys/bus/pci/devices/*; do
  echo "$(basename $d) vendor=$(cat $d/vendor) device=$(cat $d/device) class=$(cat $d/class)"
done
echo "=== [B] enumeration log (root port + bridge window)"
dmesg | grep -E "pci 0000|pcieport|bridge window" | head -30
RP=$(for d in /sys/bus/pci/devices/*; do [ "$(cat $d/class)" = "0x060400" ] && basename $d; done)
echo "root port = $RP"
echo "=== [C] root port sysfs link attrs"
for f in current_link_speed current_link_width max_link_speed max_link_width secondary_bus_number subordinate_bus_number; do
  echo "$f = $(cat /sys/bus/pci/devices/$RP/$f 2>&1)"; done
BDF=$(for d in /sys/bus/pci/devices/*; do [ "$(cat $d/device)" = "0x11e8" ] && basename $d; done)
echo "edu BDF=$BDF"
echo "--- root port config 0x00-0x3f (type 1 header) + PCIe cap"
od -A x -t x1 -N 64 /sys/bus/pci/devices/$RP/config
echo "=== [D] insmod force_intx=1 dma_bits=40"
dmesg -c >/dev/null
insmod /edu_lp06.ko force_intx=1 dma_bits=40
dmesg | grep edu_lp06 | grep -E "irq|enabling|PASS|FAIL"
echo "=== [E] /proc/interrupts (INTx = GICv2 SPI, level)"
grep -E "CPU|edu" /proc/interrupts
echo "irq file: $(cat /sys/bus/pci/devices/$BDF/irq)"
od -A x -t x2 -j 4 -N 2 /sys/bus/pci/devices/$BDF/config
rmmod edu_lp06
