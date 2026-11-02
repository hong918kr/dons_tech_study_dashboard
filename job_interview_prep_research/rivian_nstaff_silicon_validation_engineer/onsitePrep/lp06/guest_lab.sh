#!/bin/sh
# Runs INSIDE the QEMU guest (BusyBox initramfs). Started by run_lab.sh via LAB_CMD=sh~/guest_lab.sh
echo "=== [1] PCI devices (sysfs; BusyBox has no full lspci)"
for d in /sys/bus/pci/devices/*; do
  echo "$(basename $d) vendor=$(cat $d/vendor) device=$(cat $d/device) class=$(cat $d/class) irq=$(cat $d/irq)"
done
echo "=== [2] host bridge + enumeration log"
dmesg | grep -E "pci-host-generic|pci_bus|pci 0000|PCI: |ECAM" | head -40
echo "=== [3] /proc/iomem PCI part"
grep -i -E "pci|pcie|10000000|8000000000" /proc/iomem
BDF=$(for d in /sys/bus/pci/devices/*; do [ "$(cat $d/device)" = "0x11e8" ] && basename $d; done)
echo "edu BDF=$BDF"
D=/sys/bus/pci/devices/$BDF
echo "=== [4] edu modalias / resource / config header (first 64 B)"
cat $D/modalias
cat $D/resource | head -1
od -A x -t x1 -N 64 $D/config
echo "=== [5] insmod dma_bits=28 (real edu default) -> expect mask failure on virt (RAM at 1 GiB)"
insmod /edu_lp06.ko dma_bits=28
dmesg | grep edu_lp06 | tail -20
rmmod edu_lp06
echo "=== [6] insmod dma_bits=40 (QEMU started with dma_mask=0xffffffffff)"
dmesg -c > /dev/null
insmod /edu_lp06.ko dma_bits=40
dmesg | grep -E "edu_lp06|$BDF"
echo "=== [7] COMMAND register after probe (offset 0x04): bit1=MEM bit2=BusMaster bit10=INTxDisable"
od -A x -t x2 -j 4 -N 2 $D/config
echo "=== [8] /proc/interrupts"
grep -E "CPU|edu" /proc/interrupts
echo "=== [9] driver link + sysfs attr"
ls -l $D/driver
echo 8 > $D/fact; echo "fact(8) via sysfs = $(cat $D/fact)"
echo "=== [10] unbind -> user-space mmap of resource0 -> bind"
echo $BDF > /sys/bus/pci/drivers/edu_lp06/unbind
ls $D/driver 2>&1 | head -1
/edu_mmap $BDF; echo "edu_mmap exit=$?"
echo $BDF > /sys/bus/pci/drivers/edu_lp06/bind
dmesg | grep edu_lp06 | tail -3
echo "=== [11] driver_override demo: steer to no driver, then back"
echo $BDF > /sys/bus/pci/drivers/edu_lp06/unbind
echo none_such > $D/driver_override
echo $BDF > /sys/bus/pci/drivers_probe
ls $D/driver 2>&1 | head -1
echo > $D/driver_override
echo $BDF > /sys/bus/pci/drivers_probe
ls -l $D/driver
echo "=== [12] rmmod"
rmmod edu_lp06
dmesg | tail -2
