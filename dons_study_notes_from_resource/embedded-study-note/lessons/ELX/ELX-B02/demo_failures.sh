#!/usr/bin/env bash
# ENV: linux
# ELX-B02 demo: 크로스 바이너리의 "고전적인 실패"를 실제로 재현하고 출력만 보여 준다 (판정 없음).
#   limactl shell elinux -- bash <레슨 디렉터리>/demo_failures.sh > demo_failures.txt
set -u
SRC=$(cd "$(dirname "$0")" && pwd)
W=/tmp/ELX-B02_work/demo.$$
trap 'cd /; rm -rf "$W"' EXIT
mkdir -p "$W" && cp "$SRC"/{sensor.c,sensor.h,app.c} "$W/" && cd "$W" || exit 1
r() { echo "\$ $*"; "$@" 2>&1; echo "[exit=$?]"; }   # 전부 몇 초 안에 끝난다 (멈출 일 없음)
for T in aarch64-linux-gnu arm-linux-gnueabihf x86_64-linux-gnu; do
	mkdir -p $T/lib
	$T-gcc -O2 -fPIC -shared -Wl,-soname,libsensor.so.1 sensor.c -o $T/lib/libsensor.so.1
	ln -s libsensor.so.1 $T/lib/libsensor.so
	$T-gcc -O2 app.c -L$T/lib -lsensor -Wl,-rpath,'$ORIGIN/lib' -o $T/app
	$T-gcc -O2 app.c -L$T/lib -lsensor -o $T/app_norpath
done

echo "=== 0. 툴체인: 이 VM(aarch64)에서 aarch64-linux-gnu-gcc 는 사실상 네이티브 gcc ==="
r readlink -f "$(command -v gcc)"
for T in aarch64-linux-gnu arm-linux-gnueabihf x86_64-linux-gnu; do
	echo "--- $T: dumpmachine=$($T-gcc -dumpmachine) sysroot='$($T-gcc -print-sysroot)'"
	echo "    libc.so.6 = $($T-gcc -print-file-name=libc.so.6)"
	echo "    ld        = $($T-gcc -print-prog-name=ld)"
done

echo; echo "=== 1. file / readelf -h -l -d (armhf app) ==="
file arm-linux-gnueabihf/app arm-linux-gnueabihf/lib/libsensor.so.1 | sed "s|$W/||"
readelf -h arm-linux-gnueabihf/app | grep -E 'Class|Data|Type|Machine|Flags'
readelf -l arm-linux-gnueabihf/app | grep -E -A1 '^  (INTERP|LOAD|DYNAMIC)'
readelf -d arm-linux-gnueabihf/app | grep -E 'NEEDED|RUNPATH|STRTAB|STRSZ|FLAGS_1'

echo; echo "=== 2. 그냥 ./ 로 실행 ==="
r ./arm-linux-gnueabihf/app                      # binfmt 없음 + Apple Silicon 은 AArch32 EL0 없음
r ./x86_64-linux-gnu/app                         # binfmt_misc 가 qemu-x86_64 를 부르지만 로더가 없다
r env QEMU_LD_PREFIX=/usr/x86_64-linux-gnu ./x86_64-linux-gnu/app
grep -E '^(enabled|interpreter|flags)' /proc/sys/fs/binfmt_misc/qemu-x86_64
ls /proc/sys/fs/binfmt_misc | grep -cx 'qemu-arm'

echo; echo "=== 3. qemu-user: -L 없음 / 맞는 -L / 틀린 -L ==="
r qemu-arm ./arm-linux-gnueabihf/app
r qemu-arm -L /usr/arm-linux-gnueabihf ./arm-linux-gnueabihf/app
r qemu-arm -L /usr/x86_64-linux-gnu ./arm-linux-gnueabihf/app
r qemu-aarch64 -L /usr/arm-linux-gnueabihf ./aarch64-linux-gnu/app   # 틀린 -L 인데 돈다: 호스트 경로로 되돌아감

echo; echo "=== 4. 파일은 있는데 'No such file or directory' (PT_INTERP 가 없는 경로) ==="
aarch64-linux-gnu-gcc -O2 app.c -Laarch64-linux-gnu/lib -lsensor -Wl,-rpath,'$ORIGIN/lib' \
	-Wl,--dynamic-linker=/lib/ld-linux-board.so.1 -o aarch64-linux-gnu/app_badinterp
ls -l aarch64-linux-gnu/app_badinterp | awk '{print $1, $5, $NF}'
readelf -l aarch64-linux-gnu/app_badinterp | grep interpreter
r ./aarch64-linux-gnu/app_badinterp
r busybox sh -c ./aarch64-linux-gnu/app_badinterp
r python3 -I -c 'import os; os.execv("./aarch64-linux-gnu/app_badinterp", ["x"])'

echo; echo "=== 5. .so 를 못 찾음 / 다른 class 의 .so ==="
r ./aarch64-linux-gnu/app_norpath
r qemu-arm -L /usr/arm-linux-gnueabihf ./arm-linux-gnueabihf/app_norpath
r qemu-arm -L /usr/arm-linux-gnueabihf -E LD_LIBRARY_PATH="$W/arm-linux-gnueabihf/lib" ./arm-linux-gnueabihf/app_norpath
mkdir -p mix/lib && cp arm-linux-gnueabihf/app mix/ && cp aarch64-linux-gnu/lib/libsensor.so.1 mix/lib/
r qemu-arm -L /usr/arm-linux-gnueabihf ./mix/app
mkdir -p stage/lib && cp /usr/arm-linux-gnueabihf/lib/ld-linux-armhf.so.3 stage/lib/
r qemu-arm -L "$W/stage" ./arm-linux-gnueabihf/app

echo; echo "=== 6. LD_DEBUG=libs: 실제 탐색 순서 (armhf, RUNPATH + LD_LIBRARY_PATH 에 64-bit lib) ==="
qemu-arm -L /usr/arm-linux-gnueabihf -E LD_DEBUG=libs -E LD_LIBRARY_PATH="$W/mix/lib" ./arm-linux-gnueabihf/app 2>&1 |
	sed -e 's/^ *[0-9]*:\t//' -e "s|$W/||g" | grep -E 'find library|search|trying|incompatible|no such' | head -16

echo; echo "=== 7. float ABI 섞기: soft-float 오브젝트를 hard-float 앱에 링크 ==="
arm-linux-gnueabihf-gcc -c -O2 -mfloat-abi=soft sensor.c -o soft.o
readelf -h soft.o | grep Flags
readelf -h arm-linux-gnueabihf/app | grep Flags
r arm-linux-gnueabihf-gcc app.c soft.o -o mixed
