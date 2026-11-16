#!/usr/bin/env bash
# OS-A10: build xv6-riscv with one overlay (starter|solution|practice), boot it
# in QEMU and run the tests. Runs INSIDE the Lima VM:
#   limactl shell elinux -- bash -lc 'bash <lesson dir>/check.sh solution'
#   check.sh <starter|solution|practice>  -> PASS/FAIL lines, "N/M passed", exit 0/1
#   check.sh bug <name>                   -> solution with one classic bug injected, cowtest only
#   check.sh gdb                          -> solution kernel under gdb: break in cowfault()
# The pristine clone ~/.esn_scratch/xv6-riscv is only read (rsync source).
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
PRISTINE=$HOME/.esn_scratch/xv6-riscv
ROOT_WORK=/tmp/OS-A10_work

ARG=${1:-}
case "$ARG" in
starter) VARIANT=starter; PROGS=cowtest; EXTRA= ;;
solution) VARIANT=solution; PROGS=cowtest; EXTRA=--usertests ;;
practice) VARIANT=practice; PROGS=cowtest,cowpractice; EXTRA=--usertests ;;
bug) VARIANT=solution; PROGS=cowtest; EXTRA= ;;
gdb) VARIANT=solution ;;
*) echo "usage: check.sh <starter|solution|practice|gdb> | bug <name>"; exit 2 ;;
esac

[ -d "$PRISTINE/kernel" ] || { echo "FAIL: no xv6 clone at $PRISTINE"; exit 1; }
W=$ROOT_WORK/$ARG.$$
mkdir -p "$W"
# fresh copy every run: no stale objects, no fs.img left over from a previous run
rsync -a --delete --exclude .git "$PRISTINE"/ "$W"/xv6/
(cd "$SRC/$VARIANT" && find . -type f) | while read -r f; do
	mkdir -p "$W/xv6/$(dirname "$f")"
	cp "$SRC/$VARIANT/$f" "$W/xv6/$f"
done
if [ "$ARG" = bug ]; then
	# each bug = one exact text replacement in the solution overlay (see md §7)
	python3 -I - "$W/xv6" "${2:-}" <<'PY' || { rm -rf "$W"; exit 2; }
import sys
d, name = sys.argv[1], sys.argv[2]
BUGS = {
  "nocopyout": ("kernel/vm.c", "if ((*pte & PTE_COW) && (*pte & PTE_W) == 0) {", "if (0) {"),
  "nocowbit":  ("kernel/vm.c", "!= (PTE_V | PTE_U | PTE_COW))", "== 0)"),
  "childonly": ("kernel/vm.c", "if (*pte & PTE_W) // writable: both sides become read-only + COW\n"
                "      *pte = (*pte & ~PTE_W) | PTE_COW;\n    flags = PTE_FLAGS(*pte);",
                "flags = PTE_FLAGS(*pte);\n    if (flags & PTE_W)\n"
                "      flags = (flags & ~PTE_W) | PTE_COW; // child only"),
  "alwayscopy": ("kernel/vm.c", "if (krefcnt((void *)pa) == 1) {", "if (0) {"),
  "nomaxva":   ("kernel/vm.c", "if (va >= MAXVA) // walk() panics above MAXVA\n    return -1;\n", ""),
  "norefcount": ("kernel/kalloc.c", "  if (left > 0)\n    return; // another page table still maps it\n", "  (void)left;\n"),
}
if name not in BUGS:
    print("bugs:", " ".join(BUGS)); sys.exit(1)
f, old, new = BUGS[name]
s = open(d + "/" + f).read()
assert s.count(old) == 1, (f, old)
open(d + "/" + f, "w").write(s.replace(old, new))
print("injected bug '%s' into %s" % (name, f))
PY
fi
if ! make -C "$W/xv6" -s -j4 kernel/kernel fs.img >"$W/build.log" 2>&1; then
	tail -30 "$W/build.log"
	echo "FAIL build ($ARG)"
	echo "0/1 passed"
	rm -rf "$W"
	exit 1
fi

if [ "$ARG" = gdb ]; then
	# Stop in cowfault() at the first COW store fault: the shell forks for
	# "echo hi" and the child writes its stack. gdb needs a tty for a
	# blocking "continue" in a script, so it runs under script(1).
	PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
	cd "$W/xv6" || exit 1
	L1=$(awk '/^cowfault\(/{f=1} f&&/pa = PTE2PA\(\*pte\);/{print NR; exit}' kernel/vm.c)
	L2=$(awk '/^cowfault\(/{f=1} f&&/kfree\(\(void \*\)pa\);/{print NR; exit}' kernel/vm.c)
	mkfifo "$W/in"
	setsid qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -m 128M -smp 1 \
		-nographic -global virtio-mmio.force-legacy=false \
		-drive file=fs.img,if=none,format=raw,id=x0 \
		-device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
		-S -gdb tcp:127.0.0.1:$PORT <"$W/in" >"$W/console.log" 2>&1 &
	QPID=$!
	exec 3>"$W/in"
	# type the command only after the prompt: uartinit() clears the RX FIFO
	( for _ in $(seq 150); do grep -q '\$ ' "$W/console.log" && break; sleep 0.2; done
	  printf 'echo hi\n' >&3 ) &
	sleep 1
	cat >"$W/cow.gdb" <<G
set confirm off
set pagination off
target remote 127.0.0.1:$PORT
break vm.c:$L1
continue
printf "== cowfault() for pid %d (%s)\n", cpus[0].proc->pid, cpus[0].proc->name
bt
info registers scause stval sepc
printf "va=0x%lx  *pte=0x%lx  refcount=%d\n", va, *pte, kref.cnt[((*pte >> 10 << 12) - 0x80000000) / 4096]
delete
break vm.c:$L2
continue
printf "== copy made for pid %d: *pte=0x%lx  new pa=0x%lx  old pa=0x%lx\n", cpus[0].proc->pid, *pte, (*pte >> 10) << 12, pa
kill
quit
G
	timeout 60 script -qec "gdb-multiarch -q -nx kernel/kernel -x $W/cow.gdb" /dev/null 2>&1 |
		tr -d '\r' | grep -v '^\[Inferior'
	exec 3>&-
	kill -9 -- -"$QPID" 2>/dev/null || kill -9 "$QPID" 2>/dev/null
	wait "$QPID" 2>/dev/null
	rm -rf "$W"
	exit 0
fi

python3 -I "$SRC/run_xv6.py" "$W/xv6" "$PROGS" $EXTRA
RC=$?
if [ $RC -eq 0 ]; then
	echo "RESULT: PASS ($ARG)"
	rm -rf "$W"
else
	cp "$W/xv6/console.log" "$ROOT_WORK/$ARG.last-console.log" 2>/dev/null
	echo "RESULT: FAIL ($ARG) console log: $ROOT_WORK/$ARG.last-console.log"
	rm -rf "$W"
fi
exit $RC
