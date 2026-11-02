#!/bin/sh
# reset_loop.sh — repeatedly reset one PCI function through the kernel and check it comes back sane.
#
# Usage (root):  sh reset_loop.sh <BDF> [iterations=10] [method=default]
#   method: default  -> kernel picks the first working method in reset_method (device_specific acpi flr af_flr pm bus cxl_bus)
#           flr|af_flr|pm|bus -> temporarily write that single name into <BDF>/reset_method
#           sbr      -> write 1 to the UPSTREAM bridge's reset_subordinate (Secondary Bus Reset of the whole bus)
#
# Every iteration checks (all read from config space, not cached sysfs):
#   1. Vendor/Device ID is the same as before (not 0xffff = device not answering)
#   2. Command register and BAR0 restored (kernel pci_save_state/pci_restore_state did its job)
#   3. Upstream port Link Status DLActive (link came back to L0)  [only if port reports DLLLARC]
#   4. Driver still bound, AER correctable/fatal totals did not increase
#   5. Wall time of the reset write (FLR must wait >= 100 ms; pm ~ 2 x d3hot_delay; bus ~ 100 ms+)
# Pure POSIX sh (+od/awk) so it also runs on a BusyBox initramfs.
# WARNING: resetting a device that is in use (disk with a mounted fs, NIC carrying your ssh) will hurt.

SYS=/sys/bus/pci/devices
BDF=$1; N=${2:-10}; M=${3:-default}
[ -n "$BDF" ] || { echo "usage: $0 <BDF> [iterations] [default|flr|af_flr|pm|bus|sbr]"; exit 2; }
case $BDF in *:*:*) ;; *) BDF=0000:$BDF;; esac
D=$SYS/$BDF
[ -d "$D" ] || { echo "no device $BDF"; exit 2; }
[ "$(id -u)" -eq 0 ] || { echo "need root"; exit 2; }

cfg() {
	od -An -tu1 -j "$2" -N "$3" "$1/config" 2>/dev/null |
	awk -v n="$3" '{ for (i = 1; i <= NF; i++) b[c++] = $i }
	     END { if (c < n) { print -1; exit } v = 0; for (i = c - 1; i >= 0; i--) v = v * 256 + b[i]; print v }'
}
find_cap() {
	st=$(cfg "$1" 6 2); [ "$st" -lt 0 ] && return
	[ $((st & 0x10)) -eq 0 ] && return
	p=$(( $(cfg "$1" 52 1) & 0xfc )); n=0
	while [ "$p" -ge 64 ] && [ $n -lt 48 ]; do
		[ "$(cfg "$1" $p 1)" -eq "$2" ] && { echo $p; return; }
		p=$(( $(cfg "$1" $((p + 1)) 1) & 0xfc )); n=$((n + 1))
	done
}
now_ms() {
	t=$(date +%s%N 2>/dev/null)
	case $t in *N*|"") awk '{ printf "%d\n", $1 * 1000 }' /proc/uptime;; *) echo $((t / 1000000));; esac
}
aer_total() { # <file> <key>
	[ -r "$1" ] && awk -v k="$2" '$1 == k { print $2 }' "$1" || echo 0
}
hex() { printf "0x%04x" "$1"; }

UP=$(basename "$(dirname "$(readlink -f $D)")")      # parent dir in sysfs = upstream bridge (or pciXXXX:YY root)
case $UP in pci*) UP="";; esac
UPEXP=""; [ -n "$UP" ] && UPEXP=$(find_cap $SYS/$UP 16)
DLLLARC=0
if [ -n "$UPEXP" ]; then lc=$(cfg $SYS/$UP $((UPEXP + 12)) 4); DLLLARC=$(( (lc >> 20) & 1 )); fi

ID0=$(cfg $D 0 4); CMD0=$(cfg $D 4 2); BAR00=$(cfg $D 16 4)
DRV0=$(basename "$(readlink $D/driver 2>/dev/null)" 2>/dev/null)
COR0=$(aer_total $D/aer_dev_correctable TOTAL_ERR_COR); FAT0=$(aer_total $D/aer_dev_fatal TOTAL_ERR_FATAL)
OLDM=$(cat $D/reset_method 2>/dev/null)

echo "device  : $BDF id=$(printf 0x%08x $ID0) cmd=$(hex $CMD0) bar0=$(printf 0x%08x $BAR00) driver=${DRV0:-none}"
echo "upstream: ${UP:-<host bridge / none>} DLLLARC=$DLLLARC"
echo "reset_method (kernel order): ${OLDM:-<none: device has no usable reset>}"

case $M in
default) TARGET=$D/reset ;;
sbr)     [ -n "$UP" ] && [ -e $SYS/$UP/reset_subordinate ] || { echo "no upstream reset_subordinate"; exit 2; }
         TARGET=$SYS/$UP/reset_subordinate ;;
*)       echo "$M" > $D/reset_method 2>/dev/null || { echo "method '$M' not supported by $BDF (see dmesg)"; exit 2; }
         TARGET=$D/reset ;;
esac
[ -e "$TARGET" ] || { echo "$TARGET missing (no reset method)"; exit 2; }
echo "method  : $M -> $TARGET ; iterations: $N"

pass=0; fail=0; i=1; tmin=999999; tmax=0
while [ $i -le "$N" ]; do
	t0=$(now_ms)
	if ! echo 1 > "$TARGET" 2>/dev/null; then echo "[$i] FAIL write to $TARGET returned error"; fail=$((fail + 1)); i=$((i + 1)); continue; fi
	t1=$(now_ms); dt=$((t1 - t0))
	[ $dt -lt $tmin ] && tmin=$dt; [ $dt -gt $tmax ] && tmax=$dt
	id=$(cfg $D 0 4); cmd=$(cfg $D 4 2); bar0=$(cfg $D 16 4)
	drv=$(basename "$(readlink $D/driver 2>/dev/null)" 2>/dev/null)
	why=""
	[ "$id" -eq 4294967295 ] && why="$why id=0xffffffff(not-responding)"
	[ "$id" -ne "$ID0" ] && [ "$id" -ne 4294967295 ] && why="$why id-changed"
	[ "$cmd" -ne "$CMD0" ] && why="$why cmd $(hex $CMD0)->$(hex $cmd)"
	[ "$bar0" -ne "$BAR00" ] && why="$why bar0-not-restored"
	[ "$drv" != "$DRV0" ] && why="$why driver '$DRV0'->'$drv'"
	if [ "$DLLLARC" -eq 1 ]; then
		ls=$(cfg $SYS/$UP $((UPEXP + 18)) 2); [ $((ls & 0x2000)) -eq 0 ] && why="$why upstream-DLActive-"
	fi
	cor=$(aer_total $D/aer_dev_correctable TOTAL_ERR_COR); fat=$(aer_total $D/aer_dev_fatal TOTAL_ERR_FATAL)
	[ "${fat:-0}" -gt "${FAT0:-0}" ] && why="$why AER-fatal+$((fat - FAT0))"
	if [ -z "$why" ]; then
		pass=$((pass + 1)); echo "[$i] PASS ${dt}ms cmd=$(hex $cmd) cor=$cor"
	else
		fail=$((fail + 1)); echo "[$i] FAIL ${dt}ms:$why"
	fi
	i=$((i + 1))
done

[ "$M" != default ] && [ "$M" != sbr ] && [ -n "$OLDM" ] && echo "$OLDM" > $D/reset_method
echo "summary : method=$M pass=$pass fail=$fail time_ms min=$tmin max=$tmax AER_cor $COR0->$(aer_total $D/aer_dev_correctable TOTAL_ERR_COR)"
dmesg 2>/dev/null | grep -E "$BDF.*(not ready|giving up|ready [0-9]+ms|reset)" | tail -3
[ $fail -eq 0 ]
