#!/usr/bin/env bash
# TOOL-B08: solution.mk under GNU Make 4.4.1 + gcc in the Lima VM, plus a Cortex-M cross build
# with the SAME Makefile.   Run from the Mac:  bash demo_vm.sh
# (Mac home is read-only inside the VM -> copy to VM /tmp/TOOL-B08_work/vm first.)
set -u
L=$(cd "$(dirname "$0")" && pwd)
limactl shell elinux -- bash -s "$L" <<'VM'
set -u
L=$1
W=/tmp/TOOL-B08_work/vm; rm -rf $W; mkdir -p $W; cp -R "$L/proj/." $W/; cp "$L/solution.mk" $W/Makefile; cd $W
echo '### make --version'; make --version | head -1; gcc --version | head -1
echo '### make -j8 --trace CC=gcc  (from clean)'
make -j8 --trace CC=gcc >trace.log 2>&1; grep -v '^gcc' trace.log | head -6
echo '...'; ./build/app
echo '### gcc -MMD -MP output (build/main.d)'; cat build/main.d
echo '### touch include/dsp.h; make --trace CC=gcc'
touch include/dsp.h; make --trace CC=gcc
echo '### second make'; make CC=gcc
echo '### make -j8 --output-sync=target CC=gcc (clean first)'
make -s clean; make -j8 --output-sync=target CC=gcc >sync.log 2>&1
echo "exit=$? lines=$(wc -l <sync.log)"; head -3 sync.log
echo '### same Makefile, Cortex-M4 cross build: override CC/CFLAGS/LDFLAGS/BUILD on the command line'
M4='-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16'
make -j8 BUILD=build-m4 CC=arm-none-eabi-gcc \
  CFLAGS="-std=c11 -Wall -Wextra -Werror -Os -Iinclude $M4" \
  LDFLAGS="$M4 --specs=nano.specs --specs=nosys.specs" >m4.log 2>&1
echo "exit=$?"
grep -m1 -- '-c src/main.c' m4.log; grep -- '-o build-m4/app' m4.log
grep -o 'warning: _[a-z]* is not implemented and will always fail' m4.log | sort -u
file build-m4/app | cut -c1-100
arm-none-eabi-size build-m4/app
ls -d build build-m4
VM
