#!/bin/bash
# CPP-A08 size_targets.sh — 같은 펌웨어(app_target.cpp)를 세 이음매로 Cortex-M4 용으로 빌드해 크기·심볼·호출 asm 을 비교한다
#   bash size_targets.sh mac        맥: clang --target=thumbv7em (recipe b) 오브젝트 — 함수별 크기, 호출 지점 asm
#   bash size_targets.sh vm [mode]  맥: 레슨 파일을 Lima VM 'elinux' 의 /tmp/CPP-A08 로 복사하고 아래 elf 를 VM 안에서 실행
#   bash size_targets.sh elf        VM 안: arm-none-eabi-g++ 14.2.1 로 실제 ELF 링크 (gc-sections / LTO / RTTI)
#   bash size_targets.sh vm archive VM 안에서 아래 archive 실행 (GNU ld 의 .a 순서 실험)
#   bash size_targets.sh dis <v>    VM 안: elf 결과의 드라이버 함수 디스어셈블 (v = t, v, l, ...)
set -u
cd "$(dirname "$0")"
mode=${1:-mac}
case $mode in
mac)
    : "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
    : "${ARM_INC:=/tmp/arm-none-eabi-include}"
    if [ ! -d "$ARM_INC/c++/14.2.1" ]; then
        echo "fetching arm-none-eabi headers from Lima VM 'elinux' → $ARM_INC" >&2
        mkdir -p "$ARM_INC"
        limactl shell elinux -- bash -lc 'cd /usr/lib/arm-none-eabi/include && tar -chf - .' | tar -xf - -C "$ARM_INC"
    fi
    O=$(mktemp -d /tmp/cpp_a08_size.XXXXXX); trap 'rm -rf "$O"' EXIT
    T="--target=thumbv7em-none-eabihf -mcpu=cortex-m4 -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti -ffreestanding \
       -ffunction-sections -nostdinc++ -isystem $ARM_INC/c++/14.2.1 -isystem $ARM_INC/c++/14.2.1/arm-none-eabi/thumb/v7e-m+fp/hard -isystem $ARM_INC"
    for v in 1 2 3; do clang++ $T -DVARIANT=$v -c app_target.cpp -o $O/app$v.o; done
    clang++ $T -c sensor.cpp -o $O/sensor.o; clang++ $T -c i2c_hw.cpp -o $O/i2c_hw.o
    for f in app1 app2 app3 sensor i2c_hw; do
        echo "== $f.o  (text 심볼, 크기 순)"
        xcrun llvm-nm -C -S --size-sort $O/$f.o | grep -E ' [TtWwVvRr] ' | perl -ane 'printf "   %5d  %s\n", hex($F[1]), join(" ", @F[3..$#F])' \
            | sed -E 's/std::span<([^,]*), 4294967295u>/span<\1>/g'
        xcrun llvm-nm -C $O/$f.o | grep -E ' U ' | sed -E 's/std::span<([^,]*), 4294967295u>/span<\1>/g; s/^/   /'
    done
    echo "== 호출 지점: TempSensor<II2c>::read_reg16 / TempSensor<LinkBus>::read_reg16 (sensor.o)"
    xcrun llvm-objdump -d --no-show-raw-insn -C $O/sensor.o | awk -v max="${NLINES:-30}" '/^[0-9a-f]+ <TempSensor<(II2c|LinkBus)>::read_reg16/{p=1; n=0} p&&n<max{print; n++} /^$/{p=0}' \
        | sed -E 's/std::span<([^,]*), 4294967295u>/span<\1>/g'
    ;;
vm)
    L=$(pwd)
    limactl shell elinux -- bash -c "rm -rf /tmp/CPP-A08 && mkdir -p /tmp/CPP-A08 && cp $L/*.cpp $L/*.hpp $L/*.sh /tmp/CPP-A08/ && bash /tmp/CPP-A08/size_targets.sh ${2:-elf} ${3:-}"
    ;;
elf)
    O=/tmp/CPP-A08/out; mkdir -p $O
    BASE="-std=c++20 -O2 -fno-exceptions -mcpu=cortex-m4 -mthumb -nostartfiles --specs=nosys.specs"
    GC="-ffunction-sections -fdata-sections -Wl,--gc-sections"
    b() {   # b <name> <variant> <extra srcs> flags...  (링크 실패도 결과다: v_nogc 참고)
        local n=$1 v=$2 srcs=$3; shift 3
        arm-none-eabi-g++ $BASE -DVARIANT=$v "$@" app_target.cpp $srcs -o $O/$n.elf 2>$O/$n.log \
            || { echo "$n: LINK FAILED"; grep -E 'undefined|error' $O/$n.log | head -5; return 1; }
        local s vt ti ia
        s=$(arm-none-eabi-size $O/$n.elf | tail -1 | awk '{printf "text=%-5s data=%-4s bss=%-4s", $1, $2, $3}')
        vt=$(arm-none-eabi-nm -C -S $O/$n.elf | grep ' vtable for ' | perl -ane '$s+=hex($F[1]); $n++; END{printf "%d(%dB)", $n, $s}')
        ti=$(arm-none-eabi-nm -C -S $O/$n.elf | grep -E ' typeinfo (name )?for ' | perl -ane '$s+=hex($F[1]); $n++; END{printf "%d(%dB)", $n, $s}')
        ia=$(arm-none-eabi-objdump -h $O/$n.elf | grep -c init_array)
        printf '%-14s %s vtables=%-7s typeinfo=%-8s init_array=%s\n' "$n" "$s" "$vt" "$ti" "$ia"
    }
    echo "arm-none-eabi-g++ $(arm-none-eabi-g++ -dumpversion), -O2 -fno-exceptions -mcpu=cortex-m4 -mthumb"
    b t_gc      1 ""                          -fno-rtti $GC
    b v_gc      2 "sensor.cpp"                -fno-rtti $GC
    b l_gc      3 "sensor.cpp i2c_hw.cpp"     -fno-rtti $GC
    b t_nogc    1 ""                          -fno-rtti
    b v_nogc    2 "sensor.cpp"                -fno-rtti
    b l_nogc    3 "sensor.cpp i2c_hw.cpp"     -fno-rtti
    b t_lto     1 ""                          -fno-rtti -flto $GC
    b v_lto     2 "sensor.cpp"                -fno-rtti -flto $GC
    b l_lto     3 "sensor.cpp i2c_hw.cpp"     -fno-rtti -flto $GC
    b v_rtti    2 "sensor.cpp"                -frtti $GC
    b t_rtti    1 ""                          -frtti $GC
    b t_os      1 ""                          -fno-rtti -Os $GC
    b v_os      2 "sensor.cpp"                -fno-rtti -Os $GC
    b l_os      3 "sensor.cpp i2c_hw.cpp"     -fno-rtti -Os $GC
    echo "== 드라이버·버스 함수 (nm -C --size-sort, 10진 바이트)"
    for n in t_gc v_gc l_gc v_lto l_lto t_os v_os l_os; do
        echo "-- $n"
        arm-none-eabi-nm -C -S --size-sort $O/$n.elf | grep -E ' [TtWw] ' | grep -E 'TempSensor|i2c::|HwBus|hw_xfer|_start' \
            | perl -ane 'printf "   %5d  %s\n", hex($F[1]), join(" ", @F[3..$#F])' \
            | sed -E 's/std::span<([^,]*), 4294967295u>/span<\1>/g'
        arm-none-eabi-nm -C -S $O/$n.elf | grep -E ' vtable for ' | perl -ane 'printf "   %5d  %s\n", hex($F[1]), join(" ", @F[3..$#F])'
    done
    echo "== typeinfo (v_rtti)"; arm-none-eabi-nm -C -S $O/v_rtti.elf | grep -E ' typeinfo (name )?for ' | perl -ane 'printf "   %5d  %s\n", hex($F[1]), join(" ", @F[3..$#F])'
    ;;
archive)   # VM 안: GNU ld 는 명령줄 순서대로 아카이브를 뒤진다 → 링크 심 + .a 는 순서가 결과를 바꾼다
    O=/tmp/CPP-A08/out; mkdir -p $O
    F="-std=c++20 -O2 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb"
    L="-nostartfiles --specs=nosys.specs -mcpu=cortex-m4 -mthumb"
    for f in sensor i2c_fake i2c_hw; do arm-none-eabi-g++ $F -c $f.cpp -o $O/$f.o; done
    arm-none-eabi-g++ $F -DVARIANT=3 -c app_target.cpp -o $O/app3.o
    rm -f $O/libhw.a; arm-none-eabi-ar rcs $O/libhw.a $O/i2c_hw.o
    for order in "sensor.o i2c_fake.o libhw.a" "sensor.o libhw.a i2c_fake.o"; do
        echo "-- app3.o $order"
        args=$(for x in $order; do printf '%s ' "$O/$x"; done)
        if arm-none-eabi-g++ $L $O/app3.o $args -o $O/ar.elf 2>$O/ar.log; then
            echo "   link OK (경고 없음); i2c_fake.o 의 link_fake() 가 ELF 에 있다: $(arm-none-eabi-nm -C $O/ar.elf | grep -c ' T link_fake')개 → 가짜가 이겼다"
        else grep -m1 'multiple definition' $O/ar.log | sed -E 's/.*multiple definition of/   multiple definition of/; s/std::span<([^,]*), 4294967295u>/span<\1>/g'; fi
    done
    ;;
dis)
    O=/tmp/CPP-A08/out
    arm-none-eabi-objdump -d --no-show-raw-insn -C $O/${2}.elf | awk '/^[0-9a-f]+ <(TempSensor|i2c::|_start)/{p=1} p{print} /^$/{p=0}' \
        | sed -E 's/std::span<([^,]*), 4294967295u>/span<\1>/g'
    ;;
esac
