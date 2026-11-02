#!/bin/sh
# pcie_snapshot.sh -- LP15 "first 5 minutes" evidence capture for a PCIe issue.
# Read-only. Run as root for full lspci capability decode and dmesg.
#   usage: sudo ./pcie_snapshot.sh [BDF]        e.g. sudo ./pcie_snapshot.sh 0000:01:00.0
# Without BDF it dumps every PCI function. Output goes to stdout; redirect to a
# file named after the failure (board, boot #, temperature) so runs can be diffed.

BDF="$1"
hr() { printf '\n===== %s =====\n' "$*"; }

hr "kernel / time"
uname -r; date -u +%Y-%m-%dT%H:%M:%SZ

hr "topology (lspci -tv)"
lspci -tv 2>/dev/null || echo "lspci not available"

hr "ids + drivers (lspci -nnk)"
lspci -nnk 2>/dev/null

for d in /sys/bus/pci/devices/*; do
  b=$(basename "$d")
  [ -n "$BDF" ] && [ "$b" != "$BDF" ] && continue
  hr "sysfs $b"
  for f in vendor device class current_link_speed current_link_width \
           max_link_speed max_link_width enable power_state d3cold_allowed \
           aer_dev_correctable aer_dev_nonfatal aer_dev_fatal; do
    if [ -r "$d/$f" ]; then
      # multi-line counters (AER) are folded onto one line
      printf '%-22s %s\n' "$f:" "$(tr '\n' ' ' < "$d/$f" 2>/dev/null)"
    fi
  done
  [ -L "$d/driver" ] && printf '%-22s %s\n' "driver:" "$(basename "$(readlink "$d/driver")")"
  [ -r "$d/irq" ] && printf '%-22s %s\n' "irq:" "$(cat "$d/irq")"
  [ -d "$d/msi_irqs" ] && printf '%-22s %s\n' "msi_irqs:" "$(ls "$d/msi_irqs" | tr '\n' ' ')"
  hr "lspci -vvv $b (link / error / interrupt lines)"
  lspci -s "$b" -vvv 2>/dev/null | grep -E 'Control:|Status:|Region|Capabilities: \[|LnkCap|LnkCtl|LnkSta|DevCtl|DevSta|MaxPayload|MSI|UESta|CESta|Kernel driver'
done

hr "dmesg (pci / pcie / aer / dpc / link / iommu / smmu / nvme)"
PAT='pci|aer|dpc|link (up|down)|ltssm|smmu|iommu|nvme|bar [0-9]|no space'
# ring buffer may have rotated on a long-running system -> fall back to the journal
{ dmesg 2>/dev/null; journalctl -k -b --no-pager 2>/dev/null; } | grep -iE "$PAT" | awk '!seen[$0]++' | tail -n 80

hr "/proc/interrupts (PCI-MSI / ITS / pcie lines)"
grep -iE 'msi|its|pci|virtio|nvme' /proc/interrupts 2>/dev/null | head -n 40

hr "/proc/iomem (PCI windows)"
grep -iE 'pci|pcie' /proc/iomem 2>/dev/null | head -n 40
