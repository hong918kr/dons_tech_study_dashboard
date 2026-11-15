#!/usr/bin/env bash
# COMM-A07 demo_linux.sh — Linux 커널 can-isotp 를 기준 구현(독립 참조)으로 쓰는 실험
# 실행 (Mac 에서): limactl shell elinux -- bash <이 레슨 폴더>/demo_linux.sh
# VM: Lima elinux (Ubuntu, kernel 7.0, can-utils 2023.03). Mac 홈은 읽기 전용 → 작업은 VM 의 /tmp/COMM-A07_work 에서.
# 1) 정상 전송 4가지를 candump -L 로 찍는다 → solution.c 의 LINUX_K1..K4 표와 같은 바이트여야 한다.
# 2) 송신자 쪽: 손으로 만든 FC (OVFLW / FS=3 / 없음 / WAIT×3) 에 isotpsend 가 어떻게 반응하나.
# 3) 수신자 쪽: 손으로 만든 FF/CF (SN 틀림 / 마지막 CF 없음 / 새 FF / SF 끼어듦 / CF 중복) 에 isotprecv 가 어떻게 반응하나.
set -u
W=/tmp/COMM-A07_work; mkdir -p "$W"; cd "$W" || exit 1
sudo modprobe vcan; sudo modprobe can-isotp
sudo ip link add dev vcan0 type vcan 2>/dev/null || true
sudo ip link set vcan0 mtu 16 2>/dev/null || true      # Classic CAN
sudo ip link set up vcan0
echo "kernel $(uname -r), can_isotp $(lsmod | awk '$1=="can_isotp"{print "loaded"}')"

cap_start() { candump -L vcan0 > cap.log & CD=$!; sleep 0.2; }
cap_stop()  { sleep 0.3; kill "$CD" 2>/dev/null; wait "$CD" 2>/dev/null; sed 's/^/    /' cap.log; }
hexseq() { python3 -c "import sys; n,m=int(sys.argv[1]),int(sys.argv[2]); print(' '.join('%02x'%(i*m&255) for i in range(n)))" "$1" "$2"; }

echo "== 1. normal transfers (isotprecv = receiver, isotpsend = sender)"
run_ok() {  # $1 label, $2 data, $3 recv opts, $4 send opts
  echo "-- $1"; cap_start
  isotprecv -s 7e8 -d 7e0 $3 vcan0 > rx.txt & RX=$!; sleep 0.2
  echo "$2" | isotpsend -s 7e0 -d 7e8 $4 vcan0
  wait "$RX"; cap_stop; echo "    received: $(cat rx.txt)"
}
run_ok "K1 20 B, BS=2 STmin=0x0A, pad CC" "$(hexseq 20 1)" "-p CC:CC -b 2 -m 0a" "-p CC:CC"
run_ok "K2 40 B, BS=2 STmin=0xF5, pad CC" "$(hexseq 40 7)" "-p CC:CC -b 2 -m f5" "-p CC:CC"
run_ok "K3 5 B SF, pad CC"               "de ad be ef 01"  "-p CC:CC" "-p CC:CC"
run_ok "K4 10 B, no padding"             "a0 a1 a2 a3 a4 a5 a6 a7 a8 a9" "" ""

echo "== 2. sender (isotpsend -b, 20 B) vs hand-made FC from cansend"
D20=$(hexseq 20 1)
run_tx() {
  echo "-- $1"; shift; cap_start
  ( sleep 0.1; eval "$@" ) & R=$!
  local t0; t0=$(date +%s.%N)
  echo "$D20" | isotpsend -s 7e0 -d 7e8 -p CC:CC -b vcan0; local ec=$?
  local t1; t1=$(date +%s.%N)
  wait "$R"; echo "    isotpsend exit=$ec, elapsed $(python3 -c "print('%.2f' % ($t1-$t0))") s"; cap_stop
}
run_tx "FC OVFLW"                 "cansend vcan0 7E8#32000000"
run_tx "FC with invalid FS=3"     "cansend vcan0 7E8#33000000"
run_tx "no FC at all"             "true"
run_tx "WAIT x3 (0.4 s apart), then CTS" \
  "cansend vcan0 7E8#31000000; sleep 0.4; cansend vcan0 7E8#31000000; sleep 0.4; cansend vcan0 7E8#31000000; sleep 0.4; cansend vcan0 7E8#30000000"

echo "== 3. receiver (isotprecv) vs hand-made FF/CF from cansend"
run_rx() {
  echo "-- $1"; shift; cap_start
  timeout 3 isotprecv -s 7e8 -d 7e0 -p CC:CC vcan0 > rx.txt 2>&1 & RX=$!; sleep 0.2
  local t0; t0=$(date +%s.%N)
  eval "$@"
  wait "$RX"; local ec=$?; local t1; t1=$(date +%s.%N)
  echo "    isotprecv exit=$ec after $(python3 -c "print('%.2f' % ($t1-$t0))") s, output: '$(cat rx.txt)'"; cap_stop
}
FF=7E0#1014000102030405
run_rx "wrong SN (22 before 21)" "cansend vcan0 $FF; sleep 0.05; cansend vcan0 7E0#22060708090A0B0C; sleep 0.05; cansend vcan0 7E0#210D0E0F10111213"
run_rx "last CF missing (N_Cr)"  "cansend vcan0 $FF; sleep 0.05; cansend vcan0 7E0#21060708090A0B0C"
run_rx "new FF mid-reception"    "cansend vcan0 $FF; sleep 0.05; cansend vcan0 7E0#21060708090A0B0C; sleep 0.05; cansend vcan0 7E0#100AA0A1A2A3A4A5; sleep 0.05; cansend vcan0 7E0#21A6A7A8A9CCCCCC"
run_rx "SF mid-reception"        "cansend vcan0 $FF; sleep 0.05; cansend vcan0 7E0#0311223344CCCCCC"
run_rx "duplicated CF"           "cansend vcan0 $FF; sleep 0.05; cansend vcan0 7E0#21060708090A0B0C; cansend vcan0 7E0#21060708090A0B0C; sleep 0.05; cansend vcan0 7E0#220D0E0F10111213"
