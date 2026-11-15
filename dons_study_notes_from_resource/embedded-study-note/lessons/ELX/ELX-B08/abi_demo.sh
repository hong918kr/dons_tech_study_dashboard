#!/usr/bin/env bash
# ENV: linux
# ELX-B08: 4개 ABI 로 abi_layout.c 를 컴파일 → pahole 레이아웃 + _IOR 번호 비교. VM 안에서 실행.
#   limactl shell elinux -- bash <레슨 디렉터리>/abi_demo.sh
set -eu
SRC=$(cd "$(dirname "$0")" && pwd)
W=/tmp/ELX-B08_work/abi.$$
mkdir -p "$W"
trap 'rm -rf "$W"' EXIT
cp "$SRC/abi_layout.c" "$SRC/kecho_ioctl.h" "$W/"
cd "$W"
build() {	# <name> <cc> [flags...]
	local n=$1 cc=$2; shift 2
	"$cc" "$@" -g -O0 -c abi_layout.c -o "$n.o"
}
build arm64  gcc
build armhf  arm-linux-gnueabihf-gcc
build x86_64 x86_64-linux-gnu-gcc
build i386   x86_64-linux-gnu-gcc -m32
for a in arm64 armhf x86_64 i386; do
	echo "===== $a: $(file -b $a.o | cut -d, -f1-2)"
	for s in kecho_stats kecho_stats_nopad kecho_stats_bad; do
		pahole -C "$s" "$a.o"
	done
	case $a in x86_64|i386) oc=x86_64-linux-gnu-objcopy ;; armhf) oc=arm-linux-gnueabihf-objcopy ;; *) oc=objcopy ;; esac
	"$oc" -O binary -j .rodata "$a.o" "$a.rodata"
	echo "_IOR('x',1,T) good/nopad/bad: $(od -An -tx4 "$a.rodata" | xargs)"
done
