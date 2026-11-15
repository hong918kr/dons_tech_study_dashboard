#!/usr/bin/env bash
# demo_link_errors.sh — s1~s3 starter 의 링크 에러를 ld64(맥) 와 GNU ld(VM) 로 나란히 찍는다.
#   bash demo_link_errors.sh > demo_link_errors.txt 2>&1
# 작업 디렉터리: 맥 /tmp/TOOL-B05_demo, VM /tmp/TOOL-B05_demo (레슨 폴더에는 쓰지 않음)
set -u
cd "$(dirname "$0")" || exit 2
L=$(pwd)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
D=/tmp/TOOL-B05_demo
rm -rf "$D"
mkdir -p "$D"
CF="-std=c11 -Wall -Wextra -O2"
stage() { mkdir -p "$3"; cp "$L/$1/common/"* "$L/$1/$2/"* "$3/"; }
say() { printf '\n$ %s\n' "$*"; }
run() { say "$@"; eval "$@" 2>&1; echo "[rc=$?]"; }

echo "################ MAC: Apple clang 21 + ld64 ################"
echo "=== s1 starter: undefined reference 4종 ==="
stage s1_undefined starter "$D/s1"
cd "$D/s1" || exit 1
for f in $(cat SRCS); do cc $CF -c "$f"; done
run "cat SRCS"
run "nm -u main.o"
run "nm uart.o"
run "nm adc.o"
run "cc -std=c11 -O0 -c adc.c -o adc_O0.o && nm adc_O0.o   # -O0: 인라인 안 됨"
run "cc main.o sensor.o uart.o adc.o -o app"

echo
echo "=== s2 starter: 헤더에 정의한 전역 ==="
stage s2_multidef starter "$D/s2"
cd "$D/s2" || exit 1
run "cc -### -c tick.c 2>&1 | tr ' ' '\n' | grep -i common"
for f in $(cat SRCS); do cc $CF -c "$f"; done
run "nm -m tick.o | grep g_"
run "cc main.o tick.o log.o config.o -o app"
for f in $(cat SRCS); do cc $CF -fno-common -c "$f"; done
run "nm -m tick.o | grep g_"
run "cc main.o tick.o log.o config.o -o app   # -fno-common 로 컴파일한 .o"

echo
echo "=== s2 반쯤 고친 상태: g_log_level 만 extern + config.c, g_ticks/g_err_count 는 그대로 ==="
mkdir -p "$D/s2half"
cp "$D/s2/"*.c "$D/s2/SRCS" "$D/s2half/"
cd "$D/s2half" || exit 1
sed -e 's/^int g_log_level = 2;.*/extern int g_log_level;/' "$L/s2_multidef/starter/config.h" >config.h
printf '#include "config.h"\nint g_log_level = 2;\n' >config.c
run "grep -E '^(extern|unsigned|static)' config.h"
for f in $(cat SRCS); do cc $CF -c "$f"; done
run "cc main.o tick.o log.o config.o -o app && ./app"
run "nm -m main.o | grep g_err_count; nm -m log.o | grep g_err_count"
for f in $(cat SRCS); do cc $CF -fno-common -c "$f"; done
run "cc main.o tick.o log.o config.o -o app   # -fno-common"

echo
echo "=== s3 starter: C++ 가 C 함수를 부름 (extern \"C\" 없음) ==="
stage s3_mangling starter "$D/s3"
cd "$D/s3" || exit 1
cc $CF -c ring.c && c++ -std=c++17 -Wall -Wextra -O2 -c app.cpp
run "nm ring.o | grep ring_"
run "nm app.o | grep ring"
run "nm app.o | grep ring | c++filt"
run "c++filt __Z9ring_pushP4ringh"
run "c++ ring.o app.o -o app"

echo
echo "=== 링커 규칙 실험: common+정의, weak+weak(순서), weak+strong ==="
mkdir -p "$D/rules"
cd "$D/rules" || exit 1
printf '#include <stdio.h>\nextern int x;\nint f(void);\nint main(void) { printf("x=%%d f=%%d\\n", x, f()); return 0; }\n' >m.c
echo 'int x;' >common_x.c
echo 'int x = 5;' >def_x.c
echo '__attribute__((weak)) int f(void) { return 1; }' >w1.c
echo '__attribute__((weak)) int f(void) { return 2; }' >w2.c
echo 'int f(void) { return 3; }' >s3.c
for f in m common_x def_x w1 w2 s3; do cc -fcommon -c $f.c; done
run "cc m.o common_x.o def_x.o w1.o -o a && ./a      # common + 초기값 정의"
run "cc m.o def_x.o w1.o w2.o -o b && ./b             # weak w1 먼저"
run "cc m.o def_x.o w2.o w1.o -o c && ./c             # weak w2 먼저"
run "cc m.o def_x.o w1.o s3.o -o d && ./d             # weak + strong"

echo
echo "################ VM elinux: gcc 15.2 + GNU ld (bfd) ################"
limactl shell elinux -- bash -s -- "$L" <<'VM'
set -u
L=$1; D=/tmp/TOOL-B05_demo; rm -rf $D; mkdir -p $D
CF="-std=c11 -Wall -Wextra -O2"
stage() { mkdir -p "$3"; cp "$L/$1/common/"* "$L/$1/$2/"* "$3/"; }
run() { printf '\n$ %s\n' "$*"; eval "$@" 2>&1; echo "[rc=$?]"; }
gcc --version | head -1; ld --version | head -1
echo "=== s1 starter (GNU ld) ==="
stage s1_undefined starter $D/s1; cd $D/s1
for f in $(cat SRCS); do gcc $CF -c $f; done
run "gcc main.o sensor.o uart.o adc.o -o app"
run "nm adc.o | grep adc_"
run "gcc -std=c11 -O0 -c adc.c -o adc_O0.o && nm adc_O0.o | grep adc_"
echo
echo "=== s2 starter (gcc 기본 = -fno-common) ==="
stage s2_multidef starter $D/s2; cd $D/s2
run "gcc $CF -c tick.c"
for f in $(cat SRCS); do gcc $CF -c $f 2>/dev/null; done
run "nm tick.o | grep g_"
run "gcc main.o tick.o log.o config.o -o app"
echo
echo "=== s2 반쯤 고친 상태 (gcc) ==="
mkdir -p $D/s2half; cp $D/s2/*.c $D/s2/SRCS $D/s2half/; cd $D/s2half
sed -e "s/^int g_log_level = 2;.*/extern int g_log_level;/" $L/s2_multidef/starter/config.h > config.h
printf '#include "config.h"\nint g_log_level = 2;\n' > config.c
for f in $(cat SRCS); do gcc $CF -c $f 2>/dev/null; done
run "gcc main.o tick.o log.o config.o -o app"
for f in $(cat SRCS); do gcc $CF -fcommon -c $f 2>/dev/null; done
run "nm tick.o | grep g_   # -fcommon"
run "gcc main.o tick.o log.o config.o -o app && ./app   # -fcommon 으로 다시 컴파일"
echo
echo "=== s3 starter (g++ / GNU ld) ==="
stage s3_mangling starter $D/s3; cd $D/s3
gcc $CF -c ring.c && g++ -std=c++17 -Wall -Wextra -O2 -c app.cpp
run "g++ ring.o app.o -o app"
run "g++ ring.o app.o -o app -Wl,--no-demangle 2>&1 | grep -o \"reference to .*\""
run "nm app.o | grep ring"
echo
echo "=== 링커 규칙 실험 (gcc -fcommon / GNU ld) ==="
mkdir -p $D/rules; cd $D/rules
printf '#include <stdio.h>\nextern int x;\nint f(void);\nint main(void) { printf("x=%%d f=%%d\\n", x, f()); return 0; }\n' > m.c
echo 'int x;' > common_x.c; echo 'int x = 5;' > def_x.c
echo '__attribute__((weak)) int f(void) { return 1; }' > w1.c
echo '__attribute__((weak)) int f(void) { return 2; }' > w2.c
echo 'int f(void) { return 3; }' > s3.c
for f in m common_x def_x w1 w2 s3; do gcc -fcommon -c $f.c; done
run "gcc m.o common_x.o def_x.o w1.o -o a && ./a"
run "gcc m.o def_x.o w1.o w2.o -o b && ./b"
run "gcc m.o def_x.o w2.o w1.o -o c && ./c"
run "gcc m.o def_x.o w1.o s3.o -o d && ./d"
VM
