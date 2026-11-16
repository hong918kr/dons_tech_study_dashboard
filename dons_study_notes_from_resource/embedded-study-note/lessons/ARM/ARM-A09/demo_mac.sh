#!/usr/bin/env bash
# ARM-A09 demo_mac.sh — 맥(Mach-O, ld-1266.8 기준) 쪽 관찰 전부. bash demo_mac.sh
# 결과물은 /tmp/ARM-A09_work/mac 에만 쓴다.
set -u
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
SRC=$(cd "$(dirname "$0")" && pwd)
W=/tmp/ARM-A09_work/mac
mkdir -p "$W" && cd "$W" || exit 2
OBJDUMP=/usr/bin/objdump
DYLD_INFO=/Library/Developer/CommandLineTools/usr/bin/dyld_info
echo "== 1. shared_pic.S: .o 의 리로케이션 (otool -rv, objdump -d -r)"
cc -c "$SRC/shared_pic.S" -o pic.o
otool -rv pic.o
$OBJDUMP -d -r --no-show-raw-insn pic.o | sed -n '/<ltmp0>:/,$p'
echo "== 2. 실행 파일로 링크한 뒤 (solution.c + shared_pic.S)"
cc -O2 "$SRC/solution.c" "$SRC/shared_pic.S" -o sol
$OBJDUMP -d --disassemble-symbols=_pic_addr_local,_pic_load_tab2,_pic_addr_local_got,_pic_addr_ext_got,_pic_call_abs sol | sed -n '/^0/,$p'
$OBJDUMP -d --section=__stubs sol | sed -n '/__stubs>:/,$p' | head -9
$OBJDUMP -h sol | sed -n '/Idx/,$p'
nm sol | grep -E ' _(pic_g|pic_tab|pic_ptr)$'
$DYLD_INFO -fixups sol
otool -s __DATA_CONST __got sol
echo "== 3. dylib + 실행 파일 (demo_sensor.c, demo_dylib_app.S)"
cc -O2 -dynamiclib "$SRC/demo_sensor.c" -o libsensor.dylib -install_name @rpath/libsensor.dylib
cc -c "$SRC/demo_dylib_app.S" -o app.o
cc -O2 "$SRC/demo_dylib_main.c" app.o -L. -lsensor -Wl,-rpath,@executable_path -o app && ./app
otool -rv app.o
$OBJDUMP -d --disassemble-symbols=_scaled app | sed -n '/<_scaled>:/,$p'
$OBJDUMP -d --section=__stubs app | sed -n '/__stubs>:/,$p'
nm app | grep -E ' _(scaled|local_bias|ptr_to_bias|ptr_to_gain)$'
$DYLD_INFO -fixups app
$DYLD_INFO -fixup_chains app | sed -n '/seg\[2\]/,$p'
$DYLD_INFO -fixup_chain_details app
otool -s __DATA_CONST __got app
otool -s __DATA __data app
echo "== 4. @PAGE 로 dylib 심볼을 직접 참조 (expected link error)"
printf '    .text\n    .globl _f3\n    .p2align 2\n_f3:\n    adrp x8, _sensor_gain@PAGE\n    ldr  x0, [x8, _sensor_gain@PAGEOFF]\n    ret\n' > bad3.S
cc -c bad3.S -o bad3.o && printf 'long f3(void); int main(void){ return (int)f3(); }\n' > m3.c
cc m3.c bad3.o -L. -lsensor -o b3 2>&1 | sed -n '1p'
echo "== 5. @PAGE 없이 adrp (expected assembler error)"
printf '    .text\n    .globl _f1\n    .p2align 2\n_f1:\n    adrp x0, _local_bias\n    ret\n' > bad1.S
cc -c bad1.S -o bad1.o 2>&1 | grep error: | sed 's/^.*error:/error:/'
echo "== 6. 정렬 안 맞는 PAGEOFF12 (demo_misalign_mac.S)"
cc -c "$SRC/demo_misalign_mac.S" -o mis.o
echo "-- default ld:"; cc "$SRC/demo_misalign_main.c" mis.o -o mis 2>&1; ./mis
$OBJDUMP -d --disassemble-symbols=_f2 mis | sed -n '/<_f2>:/,$p'
echo "-- ld -ld_classic:"; cc "$SRC/demo_misalign_main.c" mis.o -o mis_classic -Wl,-ld_classic 2>&1 | sed -n '1,2p'
echo "== 7. .loh AdrpAdd 힌트 (demo_loh.S)"
cc -c "$SRC/demo_loh.S" -o loh.o && otool -lv loh.o | grep -A3 LC_LINKER_OPTIMIZATION_HINT
printf 'long *get_bias_loh(void); int main(void){ return *get_bias_loh() != 100; }\n' > mloh.c
cc -O2 mloh.c loh.o -o loh && ./loh && $OBJDUMP -d --disassemble-symbols=_get_bias_loh loh | sed -n '/<_get_bias_loh>:/,$p'
exit 0
