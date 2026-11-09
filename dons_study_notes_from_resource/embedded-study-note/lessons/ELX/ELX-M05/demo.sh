#!/bin/bash
# ELX-M05: demo.sh <lost|nooneshot|fixoneshot|badtype|sleep_hard|getvalue|latency>
# Run inside the Lima VM. Real guest output; never insmods into the VM's own kernel.
set -u
M=${1:-lost}
SRC=$(cd "$(dirname "$0")" && pwd)
. "$SRC/lab_common.sh"
if [ "$M" = latency ]; then mod=solution; else mod=demo_bugs; fi
prepare "$SRC" "demo_$M" "$mod" || { echo "BUILD FAILED"; exit 1; }
boot "/guest.sh~demo~$M" 180 | sed -n '/GUEST_START/,$p' | grep -av '^\[ *[0-9.]*\] *\(tun\|e1000\|igb\)'
