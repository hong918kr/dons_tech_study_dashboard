#!/usr/bin/env bash
# COMM-M09 checker — runs INSIDE the Lima VM (the Mac home is mounted read-only at the same path):
#   limactl shell elinux -- bash <ROOT>/lessons/COMM/COMM-M09/check.sh
# 1) vcan0 (mtu 72, CAN FD) + vcan1 (mtu 16, classic only), idempotent
# 2) copy sources to /tmp/COMM-M09_work, build starter/solution/practice strict (-Werror)
# 3) starter must FAIL (exit 1), solution/practice must PASS (exit 0), then ASan/UBSan builds of solution/practice
# Don't run demo.sh at the same time (both use vcan0).
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
W=/tmp/COMM-M09_work
CFLAGS="-std=c11 -Wall -Wextra -Werror -O2"
SANFLAGS="-std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
fail=0

vcan_up() {   # $1 = name, $2 = mtu. MTU can only change while the link is down (EBUSY otherwise).
	sudo ip link add dev "$1" type vcan 2>/dev/null
	cur=$(cat "/sys/class/net/$1/mtu" 2>/dev/null || echo 0)
	if [ "$cur" != "$2" ]; then
		sudo ip link set "$1" down && sudo ip link set "$1" mtu "$2" || { echo "FAIL: cannot set $1 mtu $2"; exit 1; }
	fi
	sudo ip link set "$1" up || { echo "FAIL: cannot bring $1 up"; exit 1; }
}

echo "== kernel $(uname -r), $(gcc --version | head -1)"
sudo modprobe vcan && sudo modprobe can-raw || { echo "FAIL: vcan/can-raw modules"; exit 1; }
vcan_up vcan0 72
vcan_up vcan1 16
for i in vcan0 vcan1; do ip -br link show "$i"; done

rm -rf "$W" && mkdir -p "$W"
cp "$SRC"/starter.c "$SRC"/solution.c "$SRC"/practice.c "$W"/
cd "$W" || exit 1

run() {   # $1 = name, $2 = expected exit (0/1)
	local t0 t1 rc
	t0=$(date +%s.%N)
	timeout 20 "./$1" vcan0 vcan1 >"$1.out" 2>&1
	rc=$?
	t1=$(date +%s.%N)
	grep -E "FAIL|passed|ERROR|runtime error" "$1.out" | tail -n 8
	printf '   -> %s exit=%d (want %d), %.2f s\n' "$1" "$rc" "$2" "$(echo "$t1 - $t0" | bc)"
	[ "$rc" = "$2" ] || { echo "FAIL: $1 exit $rc"; fail=1; }
}

for f in starter solution practice; do
	echo "== build $f (strict)"
	gcc $CFLAGS "$f.c" -o "$f" || { echo "FAIL: build $f"; fail=1; continue; }
done
echo "== run starter (must FAIL)";  [ -x starter ]  && run starter 1
echo "== run solution (must PASS)"; [ -x solution ] && run solution 0
echo "== run practice (must PASS)"; [ -x practice ] && run practice 0

for f in solution practice; do
	echo "== ASan/UBSan $f"
	if gcc $SANFLAGS "$f.c" -o "${f}_san"; then
		run "${f}_san" 0
	else
		echo "FAIL: sanitizer build $f"; fail=1
	fi
done

if [ "$fail" = 0 ]; then echo "RESULT: PASS (starter fails as intended, solution/practice pass, sanitizers clean)"; exit 0; fi
echo "RESULT: FAIL"; exit 1
