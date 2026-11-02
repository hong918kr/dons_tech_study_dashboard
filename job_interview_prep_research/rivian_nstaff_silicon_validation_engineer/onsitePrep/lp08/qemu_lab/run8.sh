#!/bin/sh
echo "=== edu before"; od -A x -t x1 -N 8 -j 4 /sys/bus/pci/devices/0000:02:00.0/config; readlink /sys/bus/pci/devices/0000:02:00.0/iommu_group; cat /sys/kernel/iommu_groups/4/type
/edu_dma_fault 0000:02:00.0 0x1000
sleep 1
echo "=== dmesg smmu"; dmesg | grep -i -A12 "smmu" | grep -v "allocated\|ias 44" | tail -60
grep -i smmu /proc/interrupts
