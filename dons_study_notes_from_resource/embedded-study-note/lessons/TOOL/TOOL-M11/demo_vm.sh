#!/usr/bin/env bash
# TOOL-M11 demo (Lima VM elinux, aarch64 Ubuntu): the same crash.c -> ELF core -> gdb.
#   bash demo_vm.sh > demo_vm.txt 2>&1
# Changes kernel.core_pattern ONLY inside the VM and restores it on exit (trap).
set -u
L=$(cd "$(dirname "$0")" && pwd)
limactl shell elinux -- bash -s "$L" <<'VM'
set -u
L=$1
W=/tmp/TOOL-M11_work/vm
rm -rf "$W" && mkdir -p "$W" && cp "$L/crash.c" "$L/demo_vm.gdb" "$W/" && cd "$W" || exit 1
OLD=$(cat /proc/sys/kernel/core_pattern)
restore() { sudo sysctl -q -w "kernel.core_pattern=$OLD"; echo "== restored core_pattern: $(cat /proc/sys/kernel/core_pattern | cut -c1-40)..."; }
trap restore EXIT

echo "== uname: $(uname -sm) | gcc: $(gcc --version | head -1) | $(gdb-multiarch --version | head -1)"
gcc -std=c11 -g -O0 -Wall -Wextra -Werror -pthread crash.c -o crash || exit 1
echo "== ulimit -c (soft/hard): $(ulimit -c) / $(ulimit -Hc)"
echo "== core_pattern (default): $OLD"
echo "== coredump_filter: $(cat /proc/self/coredump_filter)"

echo; echo "== 1) default: apport pipe"
( ulimit -c unlimited; ./crash; echo "rc=$?" )
ls -l /var/lib/apport/coredump/ | grep TOOL-M11 | sed 's/[0-9a-f-]\{36\}/<uuid>/'
sudo rm -f /var/lib/apport/coredump/core._tmp_TOOL-M11_work_vm_crash.*

echo; echo "== 2) core_pattern -> file in the work dir"
sudo sysctl -q -w kernel.core_pattern="$W/core.%e.%p"
echo "core_pattern now: $(cat /proc/sys/kernel/core_pattern)"
( ulimit -c 0; ./crash; echo "ulimit -c 0 -> rc=$?, cores: $(ls core.* 2>/dev/null | wc -l)" )
( ulimit -c unlimited; ./crash; echo "rc=$?" )
CORE=$(ls core.* | head -1); echo "core file name: $CORE   (%e = comm of the CRASHING thread)"
ls -l "$CORE" | awk '{print $5, $9}'
du -k "$CORE" | awk '{print "du (allocated KiB):", $1}'
file "$CORE" | sed 's/, from.*//'
echo; echo "== readelf -n (notes) summary"
readelf -n "$CORE" | grep -E '^ +(CORE|LINUX)' | awk '{print $1, $3, $4}' | sort | uniq -c
echo; echo "== readelf -l: PT_LOAD count / filesz sum"
readelf -lW "$CORE" | awk '$1=="LOAD"{n++; s+=strtonum($5)} END{print n" PT_LOAD, filesz sum " s " bytes"}'

echo; echo "== 3) gdb-multiarch on the core"
gdb-multiarch -q -batch -iex "set debuginfod enabled off" -x demo_vm.gdb ./crash "$CORE" 2>&1 | grep -v '^\[New LWP'
echo; echo "== 4) native gdb on the same core"
gdb -q -batch -iex "set debuginfod enabled off" -ex 'info threads' ./crash "$CORE" 2>&1 | tail -6
rm -f "$CORE"
VM
