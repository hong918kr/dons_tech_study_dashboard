#!/bin/sh
# sriov_iommu_probe.sh — show IOMMU groups, ACS on bridges/ports, SR-IOV PFs/VFs; optionally enable VFs
# and bind one VF to vfio-pci.
#
# Usage (root):
#   sh sriov_iommu_probe.sh                      # report only
#   sh sriov_iommu_probe.sh enable <PF-BDF> <n>  # echo n > sriov_numvfs, then report
#   sh sriov_iommu_probe.sh vfio <BDF>           # driver_override=vfio-pci, unbind, drivers_probe
# Pure POSIX sh (+od/awk): runs on BusyBox. ACS bits: include/uapi/linux/pci_regs.h PCI_ACS_*.

SYS=/sys/bus/pci/devices
cfg() {
	od -An -tu1 -j "$2" -N "$3" "$1/config" 2>/dev/null |
	awk -v n="$3" '{ for (i = 1; i <= NF; i++) b[c++] = $i }
	     END { if (c < n) { print -1; exit } v = 0; for (i = c - 1; i >= 0; i--) v = v * 256 + b[i]; print v }'
}
find_ecap() {
	p=256; n=0
	while [ $n -lt 96 ]; do
		h=$(cfg "$1" $p 4); [ "$h" -le 0 ] && return
		[ $((h & 0xffff)) -eq "$2" ] && { echo $p; return; }
		p=$(( (h >> 20) & 0xffc )); n=$((n + 1)); [ "$p" -lt 256 ] && return
	done
}
acs_str() { # value
	s=""; for kv in 1:SV 2:TB 4:RR 8:CR 16:UF 32:EC 64:DT; do
		b=${kv%%:*}; nm=${kv#*:}; [ $(($1 & b)) -ne 0 ] && s="$s$nm+ " || s="$s$nm- "
	done; echo "$s"
}
drv() { basename "$(readlink $1/driver 2>/dev/null)" 2>/dev/null; }

case $1 in
enable)
	[ -n "$2" ] && [ -n "$3" ] || { echo "usage: $0 enable <PF> <n>"; exit 2; }
	P=$SYS/$2; echo "sriov_totalvfs=$(cat $P/sriov_totalvfs)"
	echo 0 > $P/sriov_numvfs; echo "$3" > $P/sriov_numvfs && echo "enabled $3 VFs on $2" ;;
vfio)
	[ -n "$2" ] && [ -d $SYS/$2 ] || { echo "usage: $0 vfio <BDF>"; exit 2; }
	B=$SYS/$2; echo "before: driver=$(drv $B) iommu_group=$(basename "$(readlink $B/iommu_group)" 2>/dev/null)"
	echo vfio-pci > $B/driver_override
	[ -e $B/driver ] && echo "$2" > $B/driver/unbind
	echo "$2" > /sys/bus/pci/drivers_probe
	echo "after : driver=$(drv $B)  /dev/vfio: $(ls /dev/vfio 2>/dev/null | tr '\n' ' ')" ;;
esac

echo "== IOMMU groups (/sys/kernel/iommu_groups)"
if [ -z "$(ls /sys/kernel/iommu_groups 2>/dev/null)" ]; then
	echo "   (none: no IOMMU driver bound to the PCI host -> no groups, VFIO needs noiommu mode)"
else
	for g in $(ls /sys/kernel/iommu_groups | sort -n); do
		echo "   group $g ($(cat /sys/kernel/iommu_groups/$g/type 2>/dev/null)): $(ls /sys/kernel/iommu_groups/$g/devices | tr '\n' ' ')"
	done
fi

echo "== ACS on bridges/ports (ext cap 0x0D)"
for d in $SYS/*; do
	a=$(find_ecap $d 13); [ -n "$a" ] || continue
	echo "   $(basename $d) ACSCap: $(acs_str $(cfg $d $((a + 4)) 2))"
	echo "   $(basename $d) ACSCtl: $(acs_str $(cfg $d $((a + 6)) 2))"
done

echo "== SR-IOV"
for d in $SYS/*; do
	b=$(basename $d)
	if [ -e $d/sriov_totalvfs ]; then
		s=$(find_ecap $d 16)                    # PCI_EXT_CAP_ID_SRIOV 0x10
		echo "   PF $b driver=$(drv $d) totalvfs=$(cat $d/sriov_totalvfs) numvfs=$(cat $d/sriov_numvfs) offset=$(cat $d/sriov_offset) stride=$(cat $d/sriov_stride) vf_device=$(cat $d/sriov_vf_device)"
		[ -n "$s" ] && echo "      SR-IOV ecap@$(printf 0x%x $s): Ctrl=$(printf 0x%04x $(cfg $d $((s + 8)) 2)) (bit0 VF Enable, bit3 VF MSE) TotalVFs=$(cfg $d $((s + 14)) 2) NumVFs=$(cfg $d $((s + 16)) 2) FirstVFOffset=$(cfg $d $((s + 20)) 2) VFStride=$(cfg $d $((s + 22)) 2) VFDeviceID=$(printf 0x%04x $(cfg $d $((s + 26)) 2))"
		for v in $d/virtfn*; do [ -e "$v" ] && echo "      $(basename $v) -> $(basename "$(readlink $v)") driver=$(drv $v)"; done
	fi
	[ -e $d/physfn ] && echo "   VF $b physfn=$(basename "$(readlink $d/physfn)") driver=$(drv $d)"
done
exit 0
