#!/usr/bin/env bash
# OS-A09 demo: stop the solution kernel in syscall() and sys_trace() with gdb.
# Runs INSIDE the Lima VM, after `check.sh solution` (uses its build tree):
#   limactl shell elinux -- bash -lc 'bash <lesson dir>/demo_gdb.sh'
# Same as the two-window recipe `make qemu-gdb` + `gdb-multiarch`, scripted:
# a random GDB port (not the uid-derived default, other sessions use QEMU too),
# console input typed 6 s after start, everything killed at the end.
set -u
W=/tmp/OS-A09_work/solution
cd "$W" 2>/dev/null || { echo "run check.sh solution first"; exit 1; }
PORT=$((26000 + RANDOM % 3000))
rm -f .gdbinit
make .gdbinit GDBPORT=$PORT >/dev/null || exit 1

# QEMU starts halted (-S) and waits for gdb; stdin = the xv6 console.
(sleep 6; printf 'tracetest basic\n'; sleep 20) |
	setsid make qemu-gdb CPUS=1 GDBPORT=$PORT >qemu_gdb.log 2>&1 &
sleep 1

timeout 60 gdb-multiarch -q -batch -nx -x .gdbinit \
	-ex 'break syscall' -ex 'continue' -ex 'bt' \
	-ex 'p cpus[0].proc->name' -ex 'p cpus[0].proc->trapframe->a7' \
	-ex 'p/x cpus[0].proc->trapframe->epc' -ex 'p/x $scause' \
	-ex 'delete' -ex 'break sys_trace' -ex 'continue' -ex 'bt' \
	-ex 'p cpus[0].proc->pid' -ex 'p/x cpus[0].proc->trapframe->a0' \
	-ex 'finish' -ex 'p/x cpus[0].proc->tracemask' 2>&1 |
	grep -v '^\[Inferior\|^warning: No executable'

pkill -KILL -f "qemu-system-riscv64 .*tcp::$PORT"
wait 2>/dev/null
echo "--- console (qemu_gdb.log) ---"
tail -n 8 qemu_gdb.log
