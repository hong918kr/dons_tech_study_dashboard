#!/bin/bash
# ARM-A06 demo: 이 맥의 시스템 zlib(crc32) 은 무엇으로 40 GB/s 를 내나 — lldb 로 디스어셈블해서 명령을 센다
# 실행: bash demo_zlib_disasm.sh      (맥 전용. 출력: crc32_z 앞부분 + 큰 버퍼 도우미 함수의 명령 통계)
export SDKROOT=${SDKROOT:-/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}
W=/tmp/ARM-A06_zdis; mkdir -p $W
printf '#include <zlib.h>\n#include <stdio.h>\nint main(void){printf("%%lx\\n",crc32(0,(const unsigned char*)"x",1));return 0;}\n' > $W/z.c
cc $W/z.c -lz -o $W/z || exit 1
timeout 60 lldb -b -o 'b main' -o run -o 'disassemble -n crc32_z' $W/z 2>/dev/null | sed -n '/crc32_z:/,$p' > $W/crc32_z.txt
helper=$(grep -m1 '\bbl ' $W/crc32_z.txt | awk '{print $4}')
echo "== crc32_z: $(grep -c '0x' $W/crc32_z.txt) instructions, first bl -> $helper"
sed -n '1,4p;10,16p;27,34p' $W/crc32_z.txt
timeout 60 lldb -b -o 'b main' -o run -o "disassemble -s $helper -c 64" $W/z 2>/dev/null | sed -n '/disassemble -s/,$p' > $W/helper.txt
echo "== helper ($helper, first 64 instructions): instruction counts"
grep -oE '(pmull2?|crc32c?[bhwx]|ld1|eor|ext)\.?[0-9a-z]*' $W/helper.txt | sed 's/\..*//' | sort | uniq -c
echo "== helper loop excerpt"
sed -n '12,28p' $W/helper.txt
