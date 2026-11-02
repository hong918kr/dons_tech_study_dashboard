#!/bin/sh
# LP13 guest script #1: tour of every observation tool (runs inside the QEMU guest, BusyBox + copied pciutils/python3)
s() { echo; echo "##### $*"; }
mount -t tmpfs none /tmp 2>/dev/null
insmod /lib/modules/nvme-core.ko; insmod /lib/modules/nvme.ko; sleep 1
s lspci; lspci
s lspci -tv; lspci -tv
s lspci -nnk; lspci -nnk
s lspci -D -nn -s 01:00.0; lspci -D -nn -s 01:00.0
s lspci -d 1b36:; lspci -d 1b36:
s lspci -d ::0604; lspci -d ::0604
s lspci -PP; lspci -PP
s lspci -b -s 01:00.0 -v; lspci -b -s 01:00.0 -v | head -12
s lspci -vmm -s 01:00.0; lspci -vmm -s 01:00.0
s lspci -mm; lspci -mm
s FULL lspci -vvv; lspci -vvv
s lspci -x -s 01:00.0; lspci -x -s 01:00.0
s lspci -xxxx -s 01:00.0; lspci -xxxx -s 01:00.0
s setpci
echo "VENDOR_ID=$(setpci -s 01:00.0 VENDOR_ID) DEVICE_ID=$(setpci -s 01:00.0 DEVICE_ID) COMMAND=$(setpci -s 01:00.0 COMMAND)"
echo "CAP_EXP@01:00.0 flags=$(setpci -s 01:00.0 CAP_EXP+2.w) LnkCap=$(setpci -s 01:00.0 CAP_EXP+0c.l) LnkSta=$(setpci -s 01:00.0 CAP_EXP+12.w)"
echo "RP 00:01.0 LnkCap=$(setpci -s 00:01.0 CAP_EXP+0c.l) LnkCtl=$(setpci -s 00:01.0 CAP_EXP+10.w) LnkSta=$(setpci -s 00:01.0 CAP_EXP+12.w) LnkCtl2=$(setpci -s 00:01.0 CAP_EXP+30.w) LnkSta2=$(setpci -s 00:01.0 CAP_EXP+32.w)"
echo "RP 00:04.0 (empty) LnkSta=$(setpci -s 00:04.0 CAP_EXP+12.w) SltSta=$(setpci -s 00:04.0 CAP_EXP+1a.w)"
echo "ECAP_AER first dword @01:00.0: $(setpci -s 01:00.0 ECAP_AER.l 2>&1)"
echo "ECAP_AER on RP 00:01.0: $(setpci -s 00:01.0 ECAP_AER.l 2>&1)"
echo "bus numbers RP 00:03.0: PRIMARY=$(setpci -s 00:03.0 PRIMARY_BUS) SECONDARY=$(setpci -s 00:03.0 SECONDARY_BUS) SUBORDINATE=$(setpci -s 00:03.0 SUBORDINATE_BUS)"
s setpci -vD demo write; setpci -vD -s 00:01.0 CAP_EXP+10.w=20:20
s retrain RP 00:01.0; setpci -s 00:01.0 CAP_EXP+10.w=20:20; sleep 0.2; echo "LnkSta after retrain=$(setpci -s 00:01.0 CAP_EXP+12.w)"
s Target speed 2.5GT/s on RP 00:01.0 then retrain; setpci -s 00:01.0 CAP_EXP+30.w=1:f; setpci -s 00:01.0 CAP_EXP+10.w=20:20; sleep 0.2; echo "LnkCtl2=$(setpci -s 00:01.0 CAP_EXP+30.w) LnkSta=$(setpci -s 00:01.0 CAP_EXP+12.w) sysfs=$(cat /sys/bus/pci/devices/0000:00:01.0/current_link_speed)"; setpci -s 00:01.0 CAP_EXP+30.w=4:f
s sysfs; D=/sys/bus/pci/devices/0000:01:00.0; ls $D
for f in vendor device subsystem_vendor subsystem_device class revision irq numa_node local_cpulist enable power_state current_link_speed current_link_width max_link_speed max_link_width reset_method driver_override dma_mask_bits consistent_dma_mask_bits modalias ari_enabled msi_bus d3cold_allowed; do printf "%-26s %s\n" $f "$(cat $D/$f 2>&1)"; done
echo "driver -> $(readlink $D/driver)"; echo "msi_irqs: $(ls $D/msi_irqs | tr "\n" " ")"
echo "resource:"; cat $D/resource | head -6
echo "config size: $(wc -c < $D/config)"
for f in aer_dev_correctable aer_dev_nonfatal aer_dev_fatal; do echo "== $f"; cat $D/$f; done
R=/sys/bus/pci/devices/0000:00:01.0; echo "RP secondary_bus_number=$(cat $R/secondary_bus_number) subordinate_bus_number=$(cat $R/subordinate_bus_number)"
for f in aer_rootport_total_err_cor aer_rootport_total_err_fatal aer_rootport_total_err_nonfatal; do echo "RP $f=$(cat $R/$f 2>&1)"; done
echo "RP link/: $(ls $R/link 2>&1 | tr "\n" " ")"; echo "EP link/: $(ls $D/link 2>&1 | tr "\n" " ")"
echo "power/: $(ls $D/power | tr "\n" " ")"; echo "runtime_status=$(cat $D/power/runtime_status) control=$(cat $D/power/control)"
echo "E 00:04.0 empty RP: speed=$(cat /sys/bus/pci/devices/0000:00:04.0/current_link_speed) width=$(cat /sys/bus/pci/devices/0000:00:04.0/current_link_width)"
s config hexdump; hexdump -C $D/config | head -8
s proc iomem; cat /proc/iomem | grep -v -i "system ram\|reserved\|kernel" | head -40
s proc interrupts; cat /proc/interrupts | grep -i -E "CPU|nvme|eth|pcie|aer|MSI|ITS" | head -20
s proc bus pci; cat /proc/bus/pci/devices | cut -c1-60
s dmesg pci; dmesg | grep -i -E "pci|nvme|e1000e|aer|ecam|link" | head -90
s devmem NVMe VS; BAR=$(cut -d" " -f1 $D/resource | head -1); echo BAR0=$BAR; devmem $((BAR+8)) 32; devmem $BAR 64 2>&1
s python mmap resource0; python3 -c "
import mmap,struct,os
fd=os.open(\"$D/resource0\",os.O_RDONLY|os.O_SYNC); m=mmap.mmap(fd,4096,mmap.MAP_SHARED,mmap.PROT_READ)
cap,=struct.unpack_from(\"<Q\",m,0); vs,=struct.unpack_from(\"<I\",m,8)
print(\"NVMe CAP=%#x MQES=%d  VS=%#x -> NVMe %d.%d\" % (cap,(cap&0xffff)+1,vs,vs>>16,(vs>>8)&0xff))"
s snapshot; python3 /pcie_snapshot.py --out /tmp/snap.json
s dyndbg + ftrace + remove/rescan; mount -t tracefs none /sys/kernel/tracing
echo "file drivers/pci/probe.c +p" > /sys/kernel/debug/dynamic_debug/control
T=/sys/kernel/tracing; echo "pci_scan_single_device pci_setup_device pci_bus_read_dev_vendor_id pci_device_add nvme_probe pci_enable_device" > $T/set_ftrace_filter; echo function > $T/current_tracer; echo 1 > $T/tracing_on
dmesg -c > /dev/null
echo 1 > $D/remove; sleep 1; echo "after remove:"; lspci -s 01:00.0; ls /sys/bus/pci/devices | tr "\n" " "; echo; echo "RP LnkSta=$(setpci -s 00:01.0 CAP_EXP+12.w)"
echo 1 > /sys/bus/pci/rescan; sleep 2; echo "after rescan:"; lspci -s 01:00.0
echo 0 > $T/tracing_on; echo "--- dmesg"; dmesg | head -40
echo "--- trace"; grep -v "^#" $T/trace | head -25
echo nop > $T/current_tracer
s reset; echo 1 > $D/reset && echo "reset OK"; dmesg | tail -5
s done
