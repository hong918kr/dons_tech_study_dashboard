#!/bin/bash
# ARM-M14 make_dumps.sh — 크래시 데모 빌드 + lldb 배치 세션 → crash_*.txt (레슨 디렉터리에서 실행)
#   bash make_dumps.sh
# 크래시마다 ~/Library/Logs/DiagnosticReports 에 .ips 리포트가 남을 수 있다 (lldb 밑에서는 대개 안 남음).
set -u
cd "$(dirname "$0")"
export SDKROOT=${SDKROOT:-/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}
CF="-std=c11 -O1 -g -fno-stack-protector"
cc $CF demo_crash_null.c  -o /tmp/ARM-M14_null
cc $CF demo_crash_stack.c -o /tmp/ARM-M14_stack
cc $CF -arch arm64e demo_crash_stack.c -o /tmp/ARM-M14_stack_e
cc $CF demo_crash_uaf.c   -o /tmp/ARM-M14_uaf
# 잡음 정리: DWARF 섹션 줄 · "Executing commands" 줄을 지우고 레슨 디렉터리 절대 경로를 뗀다 (나머지는 lldb 출력 그대로)
trim() { grep -v -e '__DWARF' -e '^Executing commands in' -e '^(lldb) command source' | sed 's#/Users/[^ :]*/ARM-M14/##g'; }
lldb -b -s demo_crash_null.lldb          /tmp/ARM-M14_null    2>&1 | trim > crash_null.txt
lldb -b -s demo_crash_stack.lldb         /tmp/ARM-M14_stack   2>&1 | trim > crash_stack.txt
lldb -b -s demo_crash_pattern.lldb       /tmp/ARM-M14_stack   2>&1 | trim > crash_pattern.txt
lldb -b -s demo_crash_stack_arm64e.lldb  /tmp/ARM-M14_stack_e 2>&1 | trim > crash_stack_arm64e.txt
lldb -b -s demo_crash_uaf.lldb           /tmp/ARM-M14_uaf     2>&1 | trim > crash_uaf.txt
lldb -b -s demo_crash_uaf_zero.lldb      /tmp/ARM-M14_uaf     2>&1 | trim > crash_uaf_zero.txt
lldb -b -o 'image dump line-table demo_crash_null.c' /tmp/ARM-M14_null 2>&1 | trim > linetable_null.txt
lldb -b -o 'image dump line-table demo_crash_uaf.c'  /tmp/ARM-M14_uaf  2>&1 | trim > linetable_uaf.txt
wc -l crash_*.txt linetable_*.txt
