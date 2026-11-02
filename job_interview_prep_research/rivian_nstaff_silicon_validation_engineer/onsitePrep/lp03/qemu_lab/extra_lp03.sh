#!/bin/sh
export PATH=/usr/bin:/bin:/sbin:/usr/sbin
echo "--- pci_express services"; ls /sys/bus/pci_express/devices/
echo "--- RP3 empty slot"; lspci -s 00:03.0 -vv | grep -E "LnkCap:|LnkSta:|SltCap|SltSta|PresDet|Slot #" 
echo "--- RAW rp1 LnkCap/LnkCtl/LnkSta/LnkCtl2/LnkSta2"
setpci -s 00:01.0 CAP_EXP+0x0c.l CAP_EXP+0x10.w CAP_EXP+0x12.w CAP_EXP+0x30.w CAP_EXP+0x32.w
echo "--- RAW nvme LnkCap/LnkSta"; setpci -s 01:00.0 CAP_EXP+0x0c.l CAP_EXP+0x12.w
echo "--- set TLS=2.5GT/s on rp1 + retrain"
setpci -s 00:01.0 CAP_EXP+0x30.w=0x0001:0x000f
setpci -s 00:01.0 CAP_EXP+0x10.w=0x0020:0x0020
sleep 1
echo "LnkCtl=$(setpci -s 00:01.0 CAP_EXP+0x10.w) LnkSta=$(setpci -s 00:01.0 CAP_EXP+0x12.w) LnkCtl2=$(setpci -s 00:01.0 CAP_EXP+0x30.w)"
lspci -s 00:01.0 -vv | grep -E "LnkSta:|LnkCtl2:" -A1
cat /sys/bus/pci/devices/0000:00:01.0/current_link_speed /sys/bus/pci/devices/0000:01:00.0/current_link_speed
echo "--- link disable / enable on rp1"
setpci -s 00:01.0 CAP_EXP+0x10.w=0x0010:0x0010; sleep 1
echo "after LD=1: LnkSta=$(setpci -s 00:01.0 CAP_EXP+0x12.w) nvme vendor=$(setpci -s 01:00.0 VENDOR_ID.w)"
setpci -s 00:01.0 CAP_EXP+0x10.w=0x0000:0x0010; sleep 1
echo "after LD=0: LnkSta=$(setpci -s 00:01.0 CAP_EXP+0x12.w) nvme vendor=$(setpci -s 01:00.0 VENDOR_ID.w)"
echo "--- BAR sizing by hand on edu 05:00.0 (32-bit BAR0)"
OLD=$(setpci -s 05:00.0 BASE_ADDRESS_0.l); CMD=$(setpci -s 05:00.0 COMMAND.w)
setpci -s 05:00.0 COMMAND.w=0000
setpci -s 05:00.0 BASE_ADDRESS_0.l=ffffffff; RB=$(setpci -s 05:00.0 BASE_ADDRESS_0.l)
setpci -s 05:00.0 BASE_ADDRESS_0.l=$OLD; setpci -s 05:00.0 COMMAND.w=$CMD
echo "edu BAR0 old=$OLD readback_after_all1s=$RB restored=$(setpci -s 05:00.0 BASE_ADDRESS_0.l) cmd=$CMD"
echo "--- BAR sizing by hand on nvme 01:00.0 (64-bit BAR0/1)"
O0=$(setpci -s 01:00.0 BASE_ADDRESS_0.l); O1=$(setpci -s 01:00.0 BASE_ADDRESS_1.l); CMD=$(setpci -s 01:00.0 COMMAND.w)
setpci -s 01:00.0 COMMAND.w=0000
setpci -s 01:00.0 BASE_ADDRESS_0.l=ffffffff BASE_ADDRESS_1.l=ffffffff
R0=$(setpci -s 01:00.0 BASE_ADDRESS_0.l); R1=$(setpci -s 01:00.0 BASE_ADDRESS_1.l)
setpci -s 01:00.0 BASE_ADDRESS_0.l=$O0 BASE_ADDRESS_1.l=$O1; setpci -s 01:00.0 COMMAND.w=$CMD
echo "nvme BAR0 old=$O0 BAR1 old=$O1 readback=$R1:$R0"
echo "--- bridge regs of rp2 (00:02.0)"; setpci -s 00:02.0 PRIMARY_BUS.b SECONDARY_BUS.b SUBORDINATE_BUS.b MEMORY_BASE.w MEMORY_LIMIT.w PREF_MEMORY_BASE.w PREF_MEMORY_LIMIT.w PREF_BASE_UPPER32.l PREF_LIMIT_UPPER32.l IO_BASE.b IO_LIMIT.b
echo "--- remove nvme + rescan"
echo 1 > /sys/bus/pci/devices/0000:01:00.0/remove; echo "after remove:"; lspci
echo 1 > /sys/bus/pci/rescan; echo "after rescan:"; lspci | grep 01:00
dmesg | tail -8
echo "--- remove whole switch (rp2 subtree) + rescan"
echo 1 > /sys/bus/pci/devices/0000:02:00.0/remove; lspci -t
echo 1 > /sys/bus/pci/rescan; lspci -t; dmesg | tail -40 | grep -E "02:00|03:0|04:00|05:00"
echo "--- raw config hexdump rp1"; hexdump -C /sys/bus/pci/devices/0000:00:01.0/config | head -8
