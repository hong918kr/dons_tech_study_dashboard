#!/usr/bin/env bash
# OS-A09: build xv6 with an overlay and grade it. Runs INSIDE the Lima VM:
#   limactl shell elinux -- bash -lc 'bash <lesson dir>/check.sh starter|solution|practice'
# Layers: pristine clone -> starter/ -> (solution/) -> (practice/).
#   starter  = pristine + starter/                    (plumbing only, tests FAIL)
#   solution = pristine + starter/ + solution/        (TODOs filled)
#   practice = pristine + starter/ + solution/ + practice/
# The pristine clone is only read (rsync source), never written.
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
PRISTINE=$HOME/.esn_scratch/xv6-riscv
V=${1:-}
case "$V" in
starter) LAYERS="starter" ;;
solution) LAYERS="starter solution" ;;
practice) LAYERS="starter solution practice" ;;
*) echo "usage: check.sh starter|solution|practice"; exit 2 ;;
esac
[ -f "$PRISTINE/kernel/main.c" ] || { echo "FAIL: no xv6 clone at $PRISTINE"; exit 1; }

W=/tmp/OS-A09_work/$V
mkdir -p "$W"
rsync -a --delete --exclude=.git "$PRISTINE/" "$W/"
for l in $LAYERS; do cp -r "$SRC/$l/." "$W/"; done
cd "$W" || exit 1

make clean >/dev/null 2>&1
if ! make -j4 kernel/kernel fs.img >build.log 2>&1; then
	tail -30 build.log
	echo "FAIL: build ($V)"
	echo "0/1 passed"
	exit 1
fi
echo "built $V in $W ($(riscv64-linux-gnu-gcc --version | head -1))"
# run_xv6.py kills QEMU itself; timeout is the last-resort watchdog.
timeout 600 python3 "$SRC/run_xv6.py" "$V"
