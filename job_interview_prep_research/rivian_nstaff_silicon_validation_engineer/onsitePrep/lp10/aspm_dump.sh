#!/bin/sh
# aspm_dump.sh — dump PCIe power-management state of every PCI function.
#
# What it prints per function (BDF):
#   - D-state (sysfs power_state + PMCSR PowerState bits), runtime PM control/status
#   - PCIe Link Capabilities: ASPM support + L0s/L1 exit latency + ClockPM
#   - Device Capabilities: L0s/L1 *acceptable* latency (what the endpoint tolerates)
#   - Link Control: ASPM enable bits, Common Clock, CLKREQ# enable
#   - L1 PM Substates capability/control (L1.1/L1.2, T_POWER_ON, Common_Mode_Restore, LTR threshold)
#   - LTR capability/enable
#   - Linux per-link sysfs knobs: /sys/bus/pci/devices/<BDF>/link/{clkpm,l0s_aspm,l1_aspm,...}
#
# Pure POSIX sh + od + awk: works on BusyBox (no lspci/setpci needed).
# Reads config space from sysfs; run as root to see extended config (offset >= 0x40/0x100).
# Usage: sh aspm_dump.sh [BDF ...]       (default: all functions)
# Register offsets/bits: include/uapi/linux/pci_regs.h (Linux 6.18).

SYS=/sys/bus/pci/devices

# cfg <dev-dir> <offset> <bytes>  -> decimal value (little endian), -1 if unreadable
cfg() {
	od -An -tu1 -j "$2" -N "$3" "$1/config" 2>/dev/null |
	awk -v n="$3" '{ for (i = 1; i <= NF; i++) b[c++] = $i }
	     END { if (c < n) { print -1; exit } v = 0; for (i = c - 1; i >= 0; i--) v = v * 256 + b[i]; print v }'
}

# find_cap <dev> <id> -> offset of standard capability, or empty
find_cap() {
	st=$(cfg "$1" 6 2); [ "$st" -lt 0 ] && return
	[ $((st & 0x10)) -eq 0 ] && return          # Status.Capabilities List
	p=$(( $(cfg "$1" 52 1) & 0xfc )); n=0       # 0x34 Capabilities Pointer
	while [ "$p" -ge 64 ] && [ $n -lt 48 ]; do
		id=$(cfg "$1" $p 1)
		[ "$id" -eq "$2" ] && { echo $p; return; }
		p=$(( $(cfg "$1" $((p + 1)) 1) & 0xfc )); n=$((n + 1))
	done
}

# find_ecap <dev> <id> -> offset of extended capability (>= 0x100), or empty
find_ecap() {
	p=256; n=0
	while [ $n -lt 96 ]; do
		h=$(cfg "$1" $p 4)
		[ "$h" -le 0 ] && return                  # -1 = not readable (non-root / conventional PCI)
		[ $((h & 0xffff)) -eq "$2" ] && { echo $p; return; }
		p=$(( (h >> 20) & 0xffc )); n=$((n + 1))
		[ "$p" -lt 256 ] && return
	done
}

lat_l0s() { case $1 in 0) echo "<64ns";; 1) echo "<128ns";; 2) echo "<256ns";; 3) echo "<512ns";;
	4) echo "<1us";; 5) echo "<2us";; 6) echo "<4us";; *) echo "unlimited";; esac; }
lat_l1()  { case $1 in 0) echo "<1us";; 1) echo "<2us";; 2) echo "<4us";; 3) echo "<8us";;
	4) echo "<16us";; 5) echo "<32us";; 6) echo "<64us";; *) echo "unlimited";; esac; }
aspm_sup() { case $1 in 0) echo "none";; 1) echo "L0s";; 2) echo "L1";; *) echo "L0s+L1";; esac; }
flag() { [ "$1" -ne 0 ] && echo "+" || echo "-"; }
rd() { [ -r "$1" ] && cat "$1" 2>/dev/null || echo "n/a"; }

echo "ASPM policy: $(rd /sys/module/pcie_aspm/parameters/policy)"
echo "cmdline   : $(grep -o 'pcie_aspm=[a-z]*' /proc/cmdline 2>/dev/null || echo '(no pcie_aspm=)')"
[ "$(id -u)" -ne 0 ] && echo "WARN: not root -> only first 64 bytes of config space are readable"

if [ $# -gt 0 ]; then LIST="$*"; else LIST=$(ls $SYS); fi
for b in $LIST; do
	case $b in *:*:*) ;; *) b=0000:$b;; esac
	d=$SYS/$b; [ -d "$d" ] || { echo "$b: no such device"; continue; }
	echo "== $b  [$(rd $d/vendor):$(rd $d/device)] class=$(rd $d/class) driver=$(basename "$(readlink $d/driver 2>/dev/null)" 2>/dev/null)"
	echo "   sysfs: power_state=$(rd $d/power_state) d3cold_allowed=$(rd $d/d3cold_allowed) runtime: control=$(rd $d/power/control) status=$(rd $d/power/runtime_status)"

	pm=$(find_cap $d 1)                          # PCI_CAP_ID_PM
	if [ -n "$pm" ]; then
		pmc=$(cfg $d $((pm + 2)) 2); csr=$(cfg $d $((pm + 4)) 2)
		echo "   PM@$(printf 0x%x $pm): D1$(flag $((pmc & 0x200))) D2$(flag $((pmc & 0x400))) PME-from(D0..D3cold)=$(printf 0x%x $(((pmc >> 11) & 0x1f)))" \
		     " PMCSR: D$((csr & 3)) NoSoftRst$(flag $((csr & 8))) PME-En$(flag $((csr & 0x100))) PME-Status$(flag $((csr & 0x8000)))"
	fi

	ex=$(find_cap $d 16)                         # PCI_CAP_ID_EXP
	if [ -z "$ex" ]; then echo "   (no PCI Express capability: conventional PCI / virtual device -> no ASPM)"; continue; fi
	flags=$(cfg $d $((ex + 2)) 2); type=$(((flags >> 4) & 0xf))
	case $type in 0) t=Endpoint;; 1) t=LegacyEP;; 4) t=RootPort;; 5) t=SwitchUp;; 6) t=SwitchDown;; 9) t=RCiEP;; *) t=type$type;; esac
	devcap=$(cfg $d $((ex + 4)) 4); lnkcap=$(cfg $d $((ex + 12)) 4)
	lnkctl=$(cfg $d $((ex + 16)) 2); lnksta=$(cfg $d $((ex + 18)) 2)
	echo "   EXP@$(printf 0x%x $ex) $t"
	if [ "$devcap" -ge 0 ]; then
		echo "   DevCap : AcceptableLat L0s $(lat_l0s $(((devcap >> 6) & 7))) L1 $(lat_l1 $(((devcap >> 9) & 7)))  FLReset$(flag $((devcap & 0x10000000)))"
	fi
	if [ "$lnkcap" -ge 0 ]; then
		echo "   LnkCap : ASPM $(aspm_sup $(((lnkcap >> 10) & 3)))  ExitLat L0s $(lat_l0s $(((lnkcap >> 12) & 7))) L1 $(lat_l1 $(((lnkcap >> 15) & 7)))  ClockPM$(flag $((lnkcap & 0x40000)))  DLLLARC$(flag $((lnkcap & 0x100000)))"
		case $((lnkctl & 3)) in 0) a=Disabled;; 1) a="L0s";; 2) a="L1";; 3) a="L0s+L1";; esac
		echo "   LnkCtl : ASPM $a  CommClk$(flag $((lnkctl & 0x40)))  ClkReq$(flag $((lnkctl & 0x100)))  LnkDisable$(flag $((lnkctl & 0x10)))"
		echo "   LnkSta : Speed code $((lnksta & 0xf)) Width x$(((lnksta >> 4) & 0x3f)) DLActive$(flag $((lnksta & 0x2000)))"
	fi
	dc2=$(cfg $d $((ex + 36)) 4); dctl2=$(cfg $d $((ex + 40)) 2)
	[ "$dc2" -ge 0 ] && echo "   LTR    : DevCap2.LTR$(flag $((dc2 & 0x800))) DevCtl2.LTR_EN$(flag $((dctl2 & 0x400)))"

	l1=$(find_ecap $d 30)                        # PCI_EXT_CAP_ID_L1SS 0x1E
	if [ -n "$l1" ]; then
		c=$(cfg $d $((l1 + 4)) 4); c1=$(cfg $d $((l1 + 8)) 4); c2=$(cfg $d $((l1 + 12)) 4)
		ps=$(((c >> 16) & 3)); pv=$(((c >> 19) & 0x1f))
		case $ps in 0) pon=$((pv * 2));; 1) pon=$((pv * 10));; 2) pon=$((pv * 100));; *) pon=err;; esac
		echo "   L1SSCap: PCI-PM_L1.2$(flag $((c & 1))) PCI-PM_L1.1$(flag $((c & 2))) ASPM_L1.2$(flag $((c & 4))) ASPM_L1.1$(flag $((c & 8))) CMRestore=$(((c >> 8) & 0xff))us PortTPowerOn=${pon}us"
		ts=$(((c1 >> 29) & 7)); tv=$(((c1 >> 16) & 0x3ff))
		echo "   L1SSCtl1: PCI-PM_L1.2$(flag $((c1 & 1))) PCI-PM_L1.1$(flag $((c1 & 2))) ASPM_L1.2$(flag $((c1 & 4))) ASPM_L1.1$(flag $((c1 & 8))) T_CommonMode=$(((c1 >> 8) & 0xff))us LTR_L1.2_Th=$((tv * (1 << (5 * ts))))ns"
		echo "   L1SSCtl2: T_PowerOn scale=$((c2 & 3)) value=$(((c2 >> 3) & 0x1f))"
	else
		echo "   L1SS   : (no L1 PM Substates ext cap, or not root)"
	fi

	if [ -d $d/link ]; then
		s="   sysfs link/:"
		for f in clkpm l0s_aspm l1_aspm l1_1_aspm l1_2_aspm l1_1_pcipm l1_2_pcipm; do
			[ -e $d/link/$f ] && s="$s $f=$(rd $d/link/$f)"
		done
		echo "$s"
	fi
done
