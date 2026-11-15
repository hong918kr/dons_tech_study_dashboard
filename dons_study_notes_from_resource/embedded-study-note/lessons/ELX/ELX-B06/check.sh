#!/usr/bin/env bash
# ENV: linux  (runs inside the Lima VM; modules only ever load in the QEMU guest)
# ELX-B06: build hello.ko + hello_user.ko, check them on the VM side (modinfo,
# Module.symvers, depmod -b, modpost GPL gate), then boot Linux 6.18.55 in QEMU
# and run guest_test.sh. Verdict = PASS/FAIL line counts, never timing.
#   check.sh <starter|solution|practice>   -> RESULT: PASS/FAIL, exit 0/1
#   check.sh demo     -> modpost errors + a license-stripped .ko in a guest (log only)
#   check.sh shell    -> solution modules staged, interactive guest (Ctrl-A x quits)
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
KDIR=/var/lab/out/linux-build
IMAGE=/var/lab/out/Image
ROOTFS=/var/lab/build/rootfs

ARG=${1:-}
case "$ARG" in
starter|solution|practice) VARIANT=$ARG ;;
demo|shell) VARIANT=solution ;;
*) echo "usage: check.sh <starter|solution|practice|demo|shell>"; exit 2 ;;
esac
KREL=$(cat $KDIR/include/config/kernel.release)		# 6.18.55
W=/tmp/ELX-B06_work/$ARG.$$
QPID=
cleanup() {
	[ -n "$QPID" ] && kill "$QPID" 2>/dev/null
	[ "$ARG" = shell ] && rm -rf "$W"
}
trap cleanup EXIT
mkdir -p "$W"/{build,sub,dbg,prop,gpl,rootfs}
HOST=$W/host.log
: >"$HOST"
h() {	# h "description" condition...   (VM-side check)
	local d=$1; shift
	if "$@"; then echo "PASS: $d"; else echo "FAIL: $d"; fi | tee -a "$HOST"
}

# ---- 1. build: the variant against the guest's tree, plus two wrong trees -----
for d in build sub dbg; do cp "$SRC"/{starter.c,solution.c,practice.c,Makefile} "$W/$d/"; done
mk() {	# mk <dir> <kdir>
	make -C "$W/$1" -s VARIANT=$VARIANT KDIR="$2" all >"$W/$1/build.log" 2>&1 \
		|| { cat "$W/$1/build.log"; echo "FAIL: build in $1"; echo "RESULT: FAIL ($ARG, build)"; exit 1; }
}
mk build $KDIR
mk sub /var/lab/out/linux-build-sub
mk dbg /var/lab/out/linux-build-dbg
B=$W/build

# proprietary consumer, built ALONE: modpost can't see hello_greet -> only a warning
cp "$SRC/demo_prop.c" "$W/prop/"; echo 'obj-m := demo_prop.o' >"$W/prop/Kbuild"
make -C $KDIR M="$W/prop" KBUILD_MODPOST_WARN=1 modules >"$W/prop/build.log" 2>&1 \
	|| { cat "$W/prop/build.log"; echo "RESULT: FAIL ($ARG, demo_prop build)"; exit 1; }

# ---- 2. VM-side checks on the artifacts ------------------------------------------
echo "=== VM side ($KREL tree) ==="
grep -h hello_greet "$B/Module.symvers"
h "H1 Module.symvers: hello_greet is EXPORT_SYMBOL_GPL" \
	grep -qP '^0x[0-9a-f]+\thello_greet\thello\tEXPORT_SYMBOL_GPL' "$B/Module.symvers"
modinfo "$B/hello_user.ko" | grep -E '^(depends|vermagic)'
h "H2 modinfo hello_user.ko: depends=hello" test "$(modinfo -F depends "$B/hello_user.ko")" = hello
modinfo -F parm "$B/hello.ko"
h "H3 modinfo hello.ko: parm count (int) and name (charp)" \
	bash -c "modinfo -F parm '$B/hello.ko' | grep -q '^count:.*(int)\$' && modinfo -F parm '$B/hello.ko' | grep -q '^name:.*(charp)\$'"
h "H4 vermagic = '$KREL SMP preempt mod_unload aarch64'" \
	test "$(modinfo -F vermagic "$B/hello.ko")" = "$KREL SMP preempt mod_unload aarch64"

# staging rootfs: modules under /lib/modules/<ver>/extra, depmod -b like Buildroot does
cp -a "$ROOTFS"/. "$W/rootfs/"
M=$W/rootfs/lib/modules/$KREL/extra
mkdir -p "$M"
cp "$B/hello.ko" "$B/hello_user.ko" "$M/"
depmod -b "$W/rootfs" "$KREL" 2>"$W/depmod.err"
echo "--- modules.dep:"; cat "$W/rootfs/lib/modules/$KREL/modules.dep"
h "H5 depmod -b: modules.dep has 'extra/hello_user.ko: extra/hello.ko'" \
	grep -qx 'extra/hello_user.ko: extra/hello.ko' "$W/rootfs/lib/modules/$KREL/modules.dep"

# modpost GPL gate: same demo_prop, but now modpost sees hello's exports
cp "$SRC/demo_prop.c" "$W/gpl/"; echo 'obj-m := demo_prop.o' >"$W/gpl/Kbuild"
make -C $KDIR M="$W/gpl" KBUILD_EXTRA_SYMBOLS="$B/Module.symvers" modules >"$W/gpl/build.log" 2>&1
GPL_RC=$?
grep -E '^(ERROR|WARNING)' "$W/gpl/build.log"
h "H6 modpost refuses Proprietary demo_prop: 'uses GPL-only symbol 'hello_greet''" \
	bash -c "[ $GPL_RC != 0 ] && grep -qF \"ERROR: modpost: GPL-incompatible module demo_prop.ko uses GPL-only symbol 'hello_greet'\" '$W/gpl/build.log'"
if [ "$VARIANT" = practice ]; then
	h "H7 modinfo hello_opt.ko: depends= empty (symbol_get, no hard link)" \
		test -z "$(modinfo -F depends "$B/hello_opt.ko")"
fi

# ---- 3. guest -------------------------------------------------------------------
R=$W/rootfs
cp "$B"/*.ko "$W/prop/demo_prop.ko" "$R/"
cp "$W/sub/hello.ko" "$R/hello_sub.ko"
cp "$W/dbg/hello.ko" "$R/hello_dbg.ko"
cp "$SRC/guest_test.sh" "$R/guest_test.sh"
chmod +x "$R/guest_test.sh"
APPEND="console=ttyAMA0 loglevel=1"
case "$ARG" in
starter|solution|practice)
	printf '#!/bin/sh\n/guest_test.sh %s\n' "$VARIANT" >"$R/test.sh" ;;
demo)
	# runtime view of a missing license: patch "license=GPL" out of a copy of
	# hello_user.ko (modpost would refuse to build it without MODULE_LICENSE)
	perl -0777 -pe 's/license=GPL\0/licenze=GPL\0/' "$B/hello_user.ko" >"$R/hello_nolic.ko"
	echo "--- modinfo of the patched copy:"; modinfo "$R/hello_nolic.ko" | grep -E '^(license|licenze|depends)' || true
	echo "--- modpost: missing MODULE_LICENSE()"
	mkdir -p "$W/nolic"; grep -v MODULE_LICENSE "$SRC/demo_prop.c" >"$W/nolic/nolic.c"
	echo 'obj-m := nolic.o' >"$W/nolic/Kbuild"
	make -C $KDIR M="$W/nolic" KBUILD_MODPOST_WARN=1 modules 2>&1 | grep -E '^(ERROR|WARNING)|Error'
	echo "--- modpost: GPL-only symbol from a Proprietary module"
	grep -E '^(ERROR|WARNING)|Error' "$W/gpl/build.log"
	cat >"$R/test.sh" <<'G'
#!/bin/sh
dmesg -c >/dev/null
insmod /hello.ko
echo "--- insmod hello_nolic.ko"; insmod /hello_nolic.ko; echo "rc=$?"
echo "tainted=$(cat /proc/sys/kernel/tainted)"
dmesg | sed 's/^\[ *[0-9.]*\] //'
echo "--- delete_module of a module with refcnt>0"; insmod /hello_user.ko; rmmod hello; echo "rmmod rc=$? (BusyBox rmmod)"
echo GUEST_DONE
G
	;;
shell)
	echo "modules: / and /lib/modules/$KREL/extra. Try: insmod /hello.ko count=3; lsmod. Quit: Ctrl-A x"
	rm -f "$R/test.sh"
	(cd "$R" && find . | cpio -o -H newc --quiet | gzip -1) >"$W/i.gz"
	qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 1 -m 512M -nographic \
		-kernel $IMAGE -initrd "$W/i.gz" -append "console=ttyAMA0"
	exit 0 ;;
esac
chmod +x "$R/test.sh"
(cd "$R" && find . | cpio -o -H newc --quiet | gzip -1) >"$W/i.gz"

LOG=$W/console.log
timeout 150 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 1 -m 512M -nographic \
	-kernel $IMAGE -initrd "$W/i.gz" -append "$APPEND LAB_CMD=/test.sh" </dev/null >"$LOG" 2>&1 &
QPID=$!
wait $QPID; QEMU_RC=$?; QPID=

echo "=== guest (Linux $KREL, QEMU TCG cortex-a57 -smp 1) ==="
sed -n '/^lab initramfs/,/^GUEST_DONE/p' "$LOG"
if [ "$ARG" = demo ]; then
	echo "=== qemu exit $QEMU_RC ==="
	rm -rf "$W"
	exit 0
fi

# ---- 4. verdict: counts only ----------------------------------------------------
P=$(cat "$HOST" "$LOG" | grep -c '^PASS:')
F=$(cat "$HOST" "$LOG" | grep -c '^FAIL:')
DONE=$(grep -c '^GUEST_DONE' "$LOG")
BAD=$(sed -n 's/^DMESG_BAD=\([0-9]*\).*/\1/p' "$LOG" | tail -1)
BAD=${BAD:-unknown}
OK=1
[ "$F" -eq 0 ] && [ "$P" -gt 0 ] || OK=0
[ "$DONE" -eq 1 ] || { OK=0; echo "guest did not finish (qemu rc=$QEMU_RC)"; }
[ "$BAD" = 0 ] || OK=0
echo "$P/$((P + F)) passed, dmesg BUG/WARNING/Oops lines: $BAD"
if [ "$OK" = 1 ]; then
	echo "RESULT: PASS ($ARG)"
	rm -rf "$W"
	exit 0
fi
# keep only the two logs of a failed run (a few KB)
find "$W" -mindepth 1 -maxdepth 1 ! -name console.log ! -name host.log -exec rm -rf {} +
echo "RESULT: FAIL ($ARG) log: $LOG"
exit 1
