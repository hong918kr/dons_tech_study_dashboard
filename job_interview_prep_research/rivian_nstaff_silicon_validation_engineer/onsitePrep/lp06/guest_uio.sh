#!/bin/sh
# Runs INSIDE the QEMU guest (Image-dom kernel). Started by run_uio.sh
BDF=$(for d in /sys/bus/pci/devices/*; do [ "$(cat $d/device)" = "0x11e8" ] && basename $d; done)
D=/sys/bus/pci/devices/$BDF
echo "edu BDF=$BDF"
insmod /uio_pci_generic.ko
echo "1234 11e8" > /sys/bus/pci/drivers/uio_pci_generic/new_id
dmesg | grep -iE "uio|$BDF" | tail -5
ls -l $D/driver
ls /sys/class/uio/
cat /sys/class/uio/uio0/name /sys/class/uio/uio0/maps/map0/addr /sys/class/uio/uio0/maps/map0/size
echo "irq=$(cat $D/irq)"
/uio_edu $BDF /dev/uio0; echo "uio_edu exit=$?"
grep -E "CPU|uio" /proc/interrupts
