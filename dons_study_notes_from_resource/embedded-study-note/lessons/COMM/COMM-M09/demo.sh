#!/usr/bin/env bash
# COMM-M09 demo — candump / cansend / cangen next to our C code, real outputs for the lesson.
#   limactl shell elinux -- bash <ROOT>/lessons/COMM/COMM-M09/demo.sh
# Uses vcan0 (mtu 72) and vcan1 (mtu 16) like check.sh, plus a throw-away vcan9 to show the default MTU.
# Don't run check.sh at the same time (both use vcan0).
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
W=/tmp/COMM-M09_work/demo
rm -rf "$W" && mkdir -p "$W" && cd "$W" || exit 1
gcc -std=c11 -Wall -Wextra -O2 "$SRC/demo_tool.c" -o demo_tool || exit 1

vcan_up() {
	sudo ip link add dev "$1" type vcan 2>/dev/null
	if [ "$(cat /sys/class/net/$1/mtu)" != "$2" ]; then sudo ip link set "$1" down; sudo ip link set "$1" mtu "$2"; fi
	sudo ip link set "$1" up
}
sudo modprobe vcan; sudo modprobe can-raw
vcan_up vcan0 72
vcan_up vcan1 16

echo "### 1. interfaces (kernel $(uname -r), can-utils $(dpkg-query -W -f='${Version}' can-utils 2>/dev/null))"
sudo ip link add dev vcan9 type vcan && echo "fresh vcan9 default mtu = $(cat /sys/class/net/vcan9/mtu)" && sudo ip link del vcan9
ip -details link show vcan0 | head -3
ip -br link show vcan1
echo "try mtu change while UP:"; sudo ip link set vcan1 mtu 72 2>&1 | sed 's/^/  /'

echo; echo "### 2. our formatter vs candump -L (same frames, two independent printers)"
timeout 3 candump -L vcan0 >candump_L.txt &
sleep 0.3
./demo_tool send vcan0 >ours.txt
sleep 0.3; kill %1 2>/dev/null; wait 2>/dev/null
sed -E 's/^\([0-9.]+\) vcan0 //' candump_L.txt >candump_stripped.txt
paste -d'|' ours.txt candump_stripped.txt | awk -F'|' '{ printf "%-4s %s\n", ($1==$2?"same":"DIFF"), $1 }' | cut -c1-90
if diff -q ours.txt candump_stripped.txt >/dev/null; then echo "=> IDENTICAL ($(wc -l <ours.txt) lines)"; else echo "=> DIFFERENT"; diff ours.txt candump_stripped.txt; fi

echo; echo "### 3. cansend -> candump (human format) + filters: candump vs our receiver"
timeout 3 candump vcan0 >D_all.txt &
timeout 3 candump vcan0,123:7FF >D_123_7FF.txt &
timeout 3 candump vcan0,123:C00007FF >D_123_C00007FF.txt &
timeout 3 candump vcan0,123~7FF >D_inv.txt &
./demo_tool recv vcan0 10 123:7FF >O_123_7FF.txt &
./demo_tool recv vcan0 10 123:C00007FF >O_123_C00007FF.txt &
sleep 0.4
for f in 123#01 00000123#02 124#03 123#R 1ABCDEF0#DEADBEEF 321##1AABBCC; do cansend vcan0 "$f"; done
sleep 2.8; wait 2>/dev/null
for f in D_all D_123_7FF O_123_7FF D_123_C00007FF O_123_C00007FF D_inv; do echo "-- $f"; cat "$f.txt"; done

echo; echo "### 4. error frames: vcan never makes them; inject one, watch with an error mask"
timeout 2 candump -e vcan0,0:0,#FFFFFFFF >E.txt &
timeout 2 candump vcan0 >E_plain.txt &
sleep 0.3; ./demo_tool errinj vcan0; cansend vcan0 010#; sleep 1.9; wait 2>/dev/null
echo "-- candump -e vcan0,0:0,#FFFFFFFF"; cat E.txt
echo "-- candump vcan0 (no error mask)"; cat E_plain.txt

echo; echo "### 5. CAN FD on a classic-MTU interface"
cansend vcan1 123##1AA; echo "cansend rc=$?"
cansend vcan0 123##1AA; echo "cansend vcan0 rc=$?"

echo; echo "### 6. receive-queue overflow (nobody reads while frames pour in), SO_RXQ_OVFL"
for rb in 0 32768 -1; do ./demo_tool flood vcan0 2000 "$rb"; done
./demo_tool flood vcan0 20000 -1

echo; echo "### 7. cangen burst counted by candump, interface counters"
before=$(cat /sys/class/net/vcan0/statistics/rx_packets)
timeout 5 candump -n 500 vcan0 >G.txt &
sleep 0.3
cangen vcan0 -g 1 -n 500 -I 42A -L 8 -D i
sleep 0.5; wait 2>/dev/null
echo "candump -n 500 got $(wc -l <G.txt) lines; first/last:"; head -1 G.txt; tail -1 G.txt
after=$(cat /sys/class/net/vcan0/statistics/rx_packets)
echo "vcan0 rx_packets delta = $((after - before))"
ip -s link show vcan0 | sed -n '1,6p'
