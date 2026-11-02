#!/bin/sh
# pcie_set_speed_retrain.sh - set Target Link Speed on a DOWNSTREAM port (root port / switch DSP),
# retrain the link, wait for Link Training to clear, and read back speed/width/DLActive. (LP03)
#
# usage: sudo ./pcie_set_speed_retrain.sh <port BDF, e.g. 00:1c.0 or 0000:00:1c.0> <gen 1..6> [--dry-run]
#
# Register offsets are relative to the PCI Express capability (setpci's CAP_EXP), from
# include/uapi/linux/pci_regs.h:
#   +0x02 PCIe Capabilities [7:4] = Device/Port Type (4 = Root Port, 6 = Switch Downstream Port)
#   +0x0c Link Capabilities  [3:0] Max Link Speed, [9:4] Max Width, bit20 DLLLA Reporting Capable
#   +0x10 Link Control       bit4 Link Disable, bit5 Retrain Link
#   +0x12 Link Status        [3:0] speed, [9:4] width, bit11 LT, bit13 DLLLA, bit14 LBMS, bit15 LABS (RW1C)
#   +0x30 Link Control 2     [3:0] Target Link Speed
#
# Status: mechanics verified in QEMU virt (QEMU does NOT model the LTSSM: it ignores the TLS write and the
# retrain, and the script reports that). NOT verified on real hardware -> [미검증] on real HW.
# On kernels >= 6.13 the bwctrl port service also owns LBMS; this script clears LBMS after retrain the same
# way pcie_retrain_link() does, which can hide a later autonomous change from bwctrl for one event.
set -u

die() { echo "ERROR: $*" >&2; exit 1; }
hex() { printf '%d' "0x$1"; }          # setpci prints hex without 0x

[ $# -ge 2 ] || die "usage: $0 <port BDF> <gen 1..6> [--dry-run]"
BDF=$1; GEN=$2; DRY=${3:-}
command -v setpci >/dev/null 2>&1 || die "setpci not found (pciutils)"
case $GEN in 1|2|3|4|5|6) ;; *) die "gen must be 1..6" ;; esac

speed_name() { case $1 in 1) echo 2.5GT/s;; 2) echo 5GT/s;; 3) echo 8GT/s;; 4) echo 16GT/s;; 5) echo 32GT/s;; 6) echo 64GT/s;; *) echo "unknown($1)";; esac; }

rd() { setpci -s "$BDF" "$1" 2>/dev/null || die "setpci read $1 failed (wrong BDF? not root?)"; }

decode_sta() {   # $1 = LnkSta hex
    v=$(hex "$1")
    printf 'speed=%s width=x%d LT=%d DLLLA=%d LBMS=%d LABS=%d' "$(speed_name $((v & 0xf)))" $(((v >> 4) & 0x3f)) \
        $(((v >> 11) & 1)) $(((v >> 13) & 1)) $(((v >> 14) & 1)) $(((v >> 15) & 1))
}

# --- 1. sanity: must be a downstream-facing port with a PCIe capability ---
FLAGS=$(rd CAP_EXP+0x02.w)
TYPE=$(( ($(hex "$FLAGS") >> 4) & 0xf ))
case $TYPE in
    4) PT="Root Port" ;;
    6) PT="Switch Downstream Port" ;;
    *) die "$BDF is port type $TYPE, not a Root Port(4)/Downstream Port(6). Retrain Link only exists on downstream ports." ;;
esac
LNKCAP=$(rd CAP_EXP+0x0c.l)
MAXS=$(( $(hex "$LNKCAP") & 0xf ))
MAXW=$(( ($(hex "$LNKCAP") >> 4) & 0x3f ))
DLLRC=$(( ($(hex "$LNKCAP") >> 20) & 1 ))
[ "$GEN" -le "$MAXS" ] || die "port max speed is Gen$MAXS ($(speed_name $MAXS)); refusing Gen$GEN"

STA0=$(rd CAP_EXP+0x12.w); CTL2_0=$(rd CAP_EXP+0x30.w)
echo "port $BDF ($PT): LnkCap Gen$MAXS x$MAXW DLLLA-reporting=$DLLRC"
echo "before : LnkSta=$STA0 [$(decode_sta "$STA0")]  LnkCtl2=$CTL2_0 (TLS=$(speed_name $(( $(hex "$CTL2_0") & 0xf ))))"
[ "$DLLRC" -eq 1 ] || echo "note   : LLActRep- -> the DLLLA bit is not meaningful on this port; judge by speed/width + endpoint config reads"

if [ "$DRY" = "--dry-run" ]; then
    echo "dry-run: would write CAP_EXP+0x30.w=$GEN:0xf then CAP_EXP+0x10.w=0x20:0x20"
    exit 0
fi

# --- 2. wait for any training in progress (PCIe r6.1 7.5.3.7 implementation note: avoid LTSSM race) ---
i=0
while [ $(( ($(hex "$(rd CAP_EXP+0x12.w)") >> 11) & 1 )) -eq 1 ]; do
    i=$((i + 1)); [ $i -le 100 ] || die "Link Training stuck at 1 before we started"
    sleep 0.01 2>/dev/null || usleep 10000
done

# --- 3. Target Link Speed (only the low nibble; keep the rest of LnkCtl2) ---
setpci -s "$BDF" CAP_EXP+0x30.w="$GEN":0xf
CTL2_1=$(rd CAP_EXP+0x30.w)
if [ $(( $(hex "$CTL2_1") & 0xf )) -ne "$GEN" ]; then
    echo "WARN   : TLS write did not stick (LnkCtl2 reads $CTL2_1). Emulated port (QEMU) or HW-locked?"
fi

# --- 4. Retrain Link (bit 5). Reads back 0 by spec; LT (LnkSta bit 11) shows progress ---
setpci -s "$BDF" CAP_EXP+0x10.w=0x20:0x20
i=0; LT_SEEN=0
while :; do
    S=$(rd CAP_EXP+0x12.w)
    [ $(( ($(hex "$S") >> 11) & 1 )) -eq 1 ] && LT_SEEN=1
    [ $(( ($(hex "$S") >> 11) & 1 )) -eq 0 ] && [ $i -ge 2 ] && break
    i=$((i + 1)); [ $i -le 100 ] || { echo "FAIL   : LT still 1 after ~1 s (PCIE_LINK_RETRAIN_TIMEOUT_MS is 1000 in the kernel)"; break; }
    sleep 0.01 2>/dev/null || usleep 10000
done
# DLLLA can lag LT; give the data link layer up to ~100 ms
if [ "$DLLRC" -eq 1 ]; then
    i=0
    while [ $(( ($(hex "$(rd CAP_EXP+0x12.w)") >> 13) & 1 )) -eq 0 ] && [ $i -lt 10 ]; do
        i=$((i + 1)); sleep 0.01 2>/dev/null || usleep 10000
    done
fi
STA1=$(rd CAP_EXP+0x12.w)
echo "after  : LnkSta=$STA1 [$(decode_sta "$STA1")]  LT-seen-during-retrain=$LT_SEEN"

# --- 5. clear LBMS (RW1C) so the next autonomous change is visible; report ---
setpci -s "$BDF" CAP_EXP+0x12.w=0x4000:0x4000
GOT=$(( $(hex "$STA1") & 0xf ))
if [ "$DLLRC" -eq 1 ] && [ $(( ($(hex "$STA1") >> 13) & 1 )) -eq 0 ]; then
    echo "RESULT : link DOWN (DLLLA=0) -> speed/width fields are not meaningful; check LTSSM debug reg / PERST# / refclk"
elif [ "$GOT" -eq "$GEN" ]; then
    echo "RESULT : link now at $(speed_name $GOT) (requested Gen$GEN)"
else
    echo "RESULT : link at $(speed_name $GOT), requested Gen$GEN -> partner cap lower? equalization failed? (check LnkSta2 / analyzer)"
fi

# --- 6. what the endpoint(s) below see (sysfs reads LnkSta live) ---
SEC=$(rd SECONDARY_BUS.b)
for d in /sys/bus/pci/devices/*:"$SEC":00.*; do
    [ -e "$d" ] || { echo "below  : no function on bus $SEC (link down => endpoint not enumerable)"; break; }
    echo "below  : ${d##*/} current_link_speed=$(cat "$d/current_link_speed" 2>/dev/null) width=x$(cat "$d/current_link_width" 2>/dev/null) vendor=$(cat "$d/vendor")"
done
