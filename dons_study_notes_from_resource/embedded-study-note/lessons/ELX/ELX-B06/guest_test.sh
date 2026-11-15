#!/bin/sh
# ENV: qemu  (BusyBox ash inside the initramfs guest, Linux 6.18.55)
# ELX-B06 guest test: /test.sh <starter|solution|practice>
# Files (put there by check.sh): /hello.ko /hello_user.ko /demo_prop.ko
#   /hello_sub.ko (built against linux-build-sub) /hello_dbg.ko (linux-build-dbg)
#   /lib/modules/6.18.55/extra/hello{,_user}.ko (+ /hello_opt.ko for practice)
# Prints PASS:/FAIL: lines. check.sh counts them. No timing anywhere.
V=${1:-solution}
P=/sys/module/hello/parameters

t() {	# t "description" "shell condition"
	if eval "$2"; then echo "PASS: $1"; else echo "FAIL: $1"; fi
}
loaded() { grep -q "^$1 " /proc/modules; }
clr() { dmesg -c >>/tmp/dmesg.all; }	# keep everything for the final scan
mode() { ls -l "$1" | cut -c1-10; }	# piped -> no colour codes
show() { echo "--- $1"; dmesg | sed 's/^\[ *[0-9.]*\] //'; }	# for the md

echo "=== Part A: lifecycle, parameters, export ==="
clr
insmod /hello.ko count=3 name=don; rc=$?
show "insmod hello.ko count=3 name=don (rc=$rc)"
t "A1 insmod hello.ko count=3 name=don succeeds" "[ $rc = 0 ] && loaded hello"
t "A2 init printed exactly 3 greetings 'hello don i/3'" \
	"[ \$(dmesg | grep -c 'hello: hello don [1-3]/3') = 3 ]"
t "A3 tainted = 4096 (O: out-of-tree module)" "[ \$(cat /proc/sys/kernel/tainted) = 4096 ]"
t "A4 $P/count reads 3" "[ \"\$(cat $P/count 2>/dev/null)\" = 3 ]"
t "A5 $P/name reads don" "[ \"\$(cat $P/name 2>/dev/null)\" = don ]"
echo "--- modes: count $(mode $P/count 2>/dev/null) name $(mode $P/name 2>/dev/null)"
t "A6 count is -rw-r--r-- and name is -r--r--r--" \
	"[ \"\$(mode $P/count)\" = -rw-r--r-- ] && [ \"\$(mode $P/name)\" = -r--r--r-- ]"
echo 7 > $P/count 2>/dev/null
t "A7 root writes count: echo 7 -> reads 7" "[ \"\$(cat $P/count 2>/dev/null)\" = 7 ]"
echo "--- echo bob > name:"; echo bob > $P/name
t "A8 name (0444) refuses writes even for root" "[ \"\$(cat $P/name 2>/dev/null)\" = don ]"

clr
insmod /hello_user.ko; rc=$?
show "insmod hello_user.ko (rc=$rc)"
t "A9 insmod hello_user.ko succeeds after hello" "[ $rc = 0 ] && loaded hello_user"
t "A10 hello_greet saw the live count: 'greet #1 from hello_user (count=7)'" \
	"dmesg | grep -q 'hello: greet #1 from hello_user (count=7)'"
t "A11 hello_user logged 'hello_greet returned 1'" \
	"dmesg | grep -q 'hello_user: hello_greet returned 1'"
echo "--- lsmod:"; lsmod
echo "--- refcnt=$(cat /sys/module/hello/refcnt) holders=$(ls /sys/module/hello/holders | tr '\n' ' ')"
t "A12 hello refcnt = 1 while hello_user is loaded" "[ \"\$(cat /sys/module/hello/refcnt)\" = 1 ]"
t "A13 holders/hello_user exists and /proc/modules lists 'hello_user,'" \
	"[ -e /sys/module/hello/holders/hello_user ] && grep -q '^hello .* hello_user,' /proc/modules"
echo "--- rmmod hello (in use):"; rmmod hello
t "A14 rmmod hello refused while in use (still loaded)" "loaded hello"
clr
rmmod hello_user; rmmod hello
show "rmmod hello_user; rmmod hello"
t "A15 rmmod hello_user then hello: both gone, /sys/module/hello removed" \
	"! loaded hello_user && ! loaded hello && [ ! -d /sys/module/hello ]"
t "A16 exit printed 'bye (greets=1)'" "dmesg | grep -q 'hello: bye (greets=1)'"

echo "=== Part B: the failures you meet in bring-up ==="
clr
echo "--- insmod hello_user.ko (hello not loaded):"; insmod /hello_user.ko; rc=$?
show "(rc=$rc)"
t "B1 hello_user alone is refused" "[ $rc != 0 ] && ! loaded hello_user"
t "B2 dmesg: 'hello_user: Unknown symbol hello_greet (err -2)'" \
	"dmesg | grep -q 'hello_user: Unknown symbol hello_greet (err -2)'"

for c in 0 11; do
	clr
	echo "--- insmod hello.ko count=$c:"; insmod /hello.ko count=$c; rc=$?
	show "(rc=$rc)"
	if [ "$V" = practice ]; then
		want="hello: \`$c' invalid for parameter \`count'"
	else
		want="hello: count=$c out of range 1..10"
	fi
	t "B3 count=$c refused, nothing left loaded, dmesg '$want'" \
		"[ $rc != 0 ] && ! loaded hello && dmesg | grep -qF \"\$want\""
done
clr
echo "--- insmod hello.ko count=abc:"; insmod /hello.ko count=abc; rc=$?
show "(rc=$rc)"
t "B4 count=abc refused: \`abc' invalid for parameter \`count'" \
	"[ $rc != 0 ] && ! loaded hello && dmesg | grep -qF \"hello: \\\`abc' invalid for parameter \\\`count'\""
clr
insmod /hello.ko cnt=3; rc=$?
show "insmod hello.ko cnt=3 (rc=$rc)"
t "B5 typo cnt=3 still loads: 'unknown parameter 'cnt' ignored' + 1 greeting" \
	"[ $rc = 0 ] && dmesg | grep -q \"hello: unknown parameter 'cnt' ignored\" && [ \$(dmesg | grep -c 'hello: hello world 1/1') = 1 ]"
rmmod hello
clr
echo "--- insmod hello_sub.ko (built against linux-build-sub):"; insmod /hello_sub.ko; rc=$?
show "(rc=$rc)"
t "B6 vermagic: 'version magic '6.18.55-sub ...' should be '6.18.55 ...''" \
	"[ $rc != 0 ] && dmesg | grep -q \"hello: version magic '6.18.55-sub SMP preempt mod_unload aarch64' should be '6.18.55 SMP preempt mod_unload aarch64'\""
clr
echo "--- insmod hello_dbg.ko (built against linux-build-dbg):"; insmod /hello_dbg.ko; rc=$?
show "(rc=$rc)"
t "B7 dbg tree: struct module size differs -> this_module size check" \
	"[ $rc != 0 ] && dmesg | grep -q 'module hello: .gnu.linkonce.this_module section size must match'"

echo "=== Part C: modprobe, depmod, options ==="
clr
modprobe hello_user; rc=$?
show "modprobe hello_user (rc=$rc)"
t "C1 modprobe hello_user pulls in hello first (depends=hello)" \
	"[ $rc = 0 ] && loaded hello && loaded hello_user && dmesg | grep -q 'hello_user: hello_greet returned 1'"
modprobe -r hello_user
t "C2 modprobe -r hello_user removes both" "! loaded hello_user && ! loaded hello"
mkdir -p /etc/modules && echo "count=4 name=etc" > /etc/modules/hello
clr
modprobe hello
show "modprobe hello with /etc/modules/hello = 'count=4 name=etc'"
t "C3 BusyBox modprobe applies /etc/modules/hello options (4 greetings)" \
	"[ \$(dmesg | grep -c 'hello: hello etc [1-4]/4') = 4 ]"
rmmod hello; rm -f /etc/modules/hello
echo "--- modprobe nosuch:"; modprobe nosuch; rc=$?
t "C4 modprobe of a missing module fails" "[ $rc != 0 ]"

if [ "$V" = practice ]; then
	echo "=== practice ==="
	clr
	insmod /hello.ko count=2 ids=5,6,7
	show "insmod hello.ko count=2 ids=5,6,7"
	echo "--- echo 42 > count:"; echo 42 > $P/count
	t "P1 runtime write 42 refused by the param op, count stays 2" \
		"[ \"\$(cat $P/count)\" = 2 ]"
	echo 9 > $P/count
	t "P1 runtime write 9 accepted" "[ \"\$(cat $P/count)\" = 9 ]"
	t "P2 ids=5,6,7 -> $P/ids reads 5,6,7, init logged 'ids n=3: 5 6 7'" \
		"[ \"\$(cat $P/ids)\" = 5,6,7 ] && dmesg | grep -q 'hello: ids n=3: 5 6 7'"
	rmmod hello
	clr
	echo "--- insmod hello.ko ids=1,2,3,4,5:"; insmod /hello.ko ids=1,2,3,4,5; rc=$?
	show "(rc=$rc)"
	t "P2 five ids refused: 'ids: can only take 4 arguments'" \
		"[ $rc != 0 ] && ! loaded hello && dmesg | grep -q 'ids: can only take 4 arguments'"
	clr
	insmod /hello_opt.ko; rc=$?
	show "insmod hello_opt.ko without hello (rc=$rc)"
	t "P3 hello_opt loads without hello (optional dependency)" \
		"[ $rc = 0 ] && loaded hello_opt && dmesg | grep -q 'hello_opt: hello not loaded, running without it'"
	rmmod hello_opt
	clr
	insmod /hello.ko; insmod /hello_opt.ko
	show "insmod hello.ko; insmod hello_opt.ko"
	echo "--- refcnt=$(cat /sys/module/hello/refcnt) holders=[$(ls /sys/module/hello/holders | tr '\n' ' ')]"
	grep '^hello ' /proc/modules
	t "P3 symbol_get: greet worked, hello refcnt 1, but no holders entry" \
		"dmesg | grep -q 'hello_opt: optional greet returned 1' && [ \"\$(cat /sys/module/hello/refcnt)\" = 1 ] && [ -z \"\$(ls /sys/module/hello/holders)\" ]"
	echo "--- rmmod hello (pinned by symbol_get):"; rmmod hello
	t "P3 rmmod hello refused while hello_opt holds the symbol" "loaded hello"
	rmmod hello_opt
	t "P3 after rmmod hello_opt (symbol_put): refcnt 0" "[ \"\$(cat /sys/module/hello/refcnt)\" = 0 ]"
	rmmod hello
fi

echo "=== Part B (last: taints the kernel P for good) ==="
insmod /hello.ko
clr
echo "--- insmod demo_prop.ko (license Proprietary):"; insmod /demo_prop.ko; rc=$?
show "(rc=$rc)"
t "B8 Proprietary module cannot bind the GPL-only symbol" \
	"[ $rc != 0 ] && ! loaded demo_prop && dmesg | grep -q 'demo_prop: Unknown symbol hello_greet (err -2)'"
t "B9 the attempt alone tainted the kernel: 'license 'Proprietary' taints', tainted = 4097" \
	"dmesg | grep -q \"demo_prop: module license 'Proprietary' taints kernel.\" && [ \$(cat /proc/sys/kernel/tainted) = 4097 ]"
rmmod hello
clr
echo "DMESG_BAD=$(grep -cE 'BUG:|WARNING:|Oops|Call trace|refcount_t' /tmp/dmesg.all)"
echo GUEST_DONE
