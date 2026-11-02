#!/bin/sh
echo "=== uname"; uname -r
echo "=== lspci"; lspci
ip link set eth0 up; sleep 2
for d in /sys/bus/pci/devices/*; do
  echo "== $(basename $d) ven=$(cat $d/vendor) dev=$(cat $d/device) class=$(cat $d/class) irq=$(cat $d/irq) drv=$(basename $(readlink $d/driver) 2>/dev/null)"
  echo "   msi_irqs: $(ls $d/msi_irqs 2>/dev/null | tr '\n' ' ')"
  for i in $(ls $d/msi_irqs 2>/dev/null); do echo "   $i -> $(cat $d/msi_irqs/$i) aff=$(cat /proc/irq/$i/smp_affinity_list) eff=$(cat /proc/irq/$i/effective_affinity_list)"; done
  echo "   iommu_group: $(readlink $d/iommu_group)  dma_mask_bits=$(cat $d/dma_mask_bits)"
done
echo "=== interrupts"; cat /proc/interrupts
for d in /sys/bus/pci/devices/*; do echo "=== cfg $(basename $d)"; od -A x -t x1 -v $d/config | head -80; done
echo "=== e1000e msix table (BAR3)"; B=$(sed -n 4p /sys/bus/pci/devices/0000:01:00.0/resource | cut -d" " -f1); for o in 0 4 8 12 16 20 24 28 32 36 40 44; do printf "+0x%02x: " $o; devmem $(printf "0x%x" $((B+o))) 32; done
echo "=== reserved_regions g3"; cat /sys/kernel/iommu_groups/3/reserved_regions; cat /sys/kernel/iommu_groups/3/type
echo "=== iommu groups"; find /sys/kernel/iommu_groups/ -name '*:*'
echo "=== dmesg"; dmesg | grep -i -E "smmu|its|msi|aer|pcieport|iommu|e1000e|dpc|swiotlb|pci 0000:01|pci 0000:02|edr"
echo "=== aer before"; for f in aer_dev_correctable aer_dev_nonfatal aer_dev_fatal aer_rootport_total_err_cor aer_rootport_total_err_nonfatal aer_rootport_total_err_fatal; do for d in /sys/bus/pci/devices/*; do [ -f $d/$f ] && echo "$(basename $d)/$f: $(cat $d/$f | tr '\n' ',')"; done; done
echo READY_FOR_INJECT
sleep 45
echo "=== dmesg after inject"; dmesg | grep -v -E "^\[ +0\.|^\[ +1\." | tail -80
echo "=== aer after"; for f in aer_dev_correctable aer_dev_nonfatal aer_dev_fatal aer_rootport_total_err_cor aer_rootport_total_err_nonfatal aer_rootport_total_err_fatal; do for d in /sys/bus/pci/devices/*; do [ -f $d/$f ] && echo "$(basename $d)/$f: $(cat $d/$f | tr '\n' ',')"; done; done
echo "=== interrupts after"; grep -i -E "its|msi|aer|e1000e|eth|pcie" /proc/interrupts
