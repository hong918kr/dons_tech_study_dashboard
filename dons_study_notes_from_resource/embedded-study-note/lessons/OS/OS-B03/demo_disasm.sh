#!/bin/sh
# OS-B03 demo_disasm.sh — libSystem 안의 getpid / getppid / read / mach_absolute_time 기계어를 본다.
# macOS 11+ 에서 시스템 라이브러리는 디스크에 따로 없고 dyld shared cache 안에 있어서
# `otool -tV /usr/lib/system/libsystem_kernel.dylib` 는 안 된다. 대신 작은 프로그램을 lldb 로
# main 에서 멈춘 뒤, 메모리에 올라온 함수를 disassemble 한다 (SIP 를 끌 필요 없음, sudo 불필요).
#
# run: sh demo_disasm.sh            (lldb 는 Xcode Command Line Tools 에 포함)
set -eu
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"
export SDKROOT
work=$(mktemp -d /tmp/OS-B03_disasm.XXXXXX)
trap 'rm -rf "$work"' EXIT

cat > "$work/probe.c" <<'EOF'
#define _DARWIN_C_SOURCE
#include <mach/mach_time.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
int main(void) {
    struct timespec ts;
    char b;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    printf("%d %d %llu\n", getpid(), getppid(), (unsigned long long)mach_absolute_time());
    return (int)read(0, &b, 0);
}
EOF
cc -std=c11 -O2 -g "$work/probe.c" -o "$work/probe"

for fn in getpid getppid read mach_absolute_time mach_boottime_usec; do
    lldb -b -o 'b main' -o run -o "disassemble -n $fn" "$work/probe" </dev/null 2>&1 |
        sed -n "/disassemble -n $fn/,\$p" |
        awk '/^libsystem_kernel.dylib`/{p=1} /^dyld`|^libc\+\+/{p=0} p' |   # libsystem_kernel 판만
        sed -n '1,22p'
    echo
done
