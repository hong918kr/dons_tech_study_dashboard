#!/bin/bash
# CPP-M06 measure.sh — Lima VM elinux 안에서 실행 (arm-none-eabi-g++ 14.2.1, 실제 ELF 링크)
#   limactl shell elinux -- bash -c 'mkdir -p /tmp/CPP-M06 && cp <lesson dir>/* /tmp/CPP-M06/ && bash /tmp/CPP-M06/measure.sh'
# 인자: 없으면 표, "dis <이름>" 은 via_*/fanout 디스어셈블, "syms <이름>" 은 큰 심볼 목록
cd "$(dirname "$0")"
O=/tmp/CPP-M06/out; mkdir -p $O
BASE="-std=c++20 -O2 -fno-exceptions -mcpu=cortex-m4 -mthumb -nostartfiles --specs=nosys.specs"
GC="-ffunction-sections -fdata-sections -Wl,--gc-sections"
SRC="demo_app.cpp demo_board.cpp demo_uart.cpp demo_spi.cpp demo_i2c.cpp demo_stubs.cpp"
b() { # b <name> flags...
    local n=$1; shift
    arm-none-eabi-g++ $BASE "$@" $SRC -o $O/$n.elf 2>$O/$n.log || { echo "$n: LINK FAILED"; grep -E 'undefined|error' $O/$n.log | head -5; return 1; }
    local s; s=$(arm-none-eabi-size $O/$n.elf | tail -1 | awk '{print $1, $2, $3}')
    local ti tb vt vb
    ti=$(arm-none-eabi-nm -C -S $O/$n.elf | grep -c -E ' (typeinfo for|typeinfo name for) ')
    tb=$(arm-none-eabi-nm -C -S $O/$n.elf | grep -E ' (typeinfo for|typeinfo name for) ' | awk '{s+=strtonum("0x"$2)} END{print s+0}')
    vb=$(arm-none-eabi-nm -C -S $O/$n.elf | grep -E ' vtable for ' | awk '{s+=strtonum("0x"$2)} END{print s+0}')
    vt=$(arm-none-eabi-nm -C $O/$n.elf | grep -c ' vtable for ')
    printf '%-22s text=%-7s data=%-5s bss=%-5s vtables=%-2s(%sB) typeinfo_syms=%-3s(%sB)\n' "$n" $s "$vt" "$vb" "$ti" "$tb"
}
case "${1:-table}" in
table)
    b nortti            -fno-rtti
    b rtti              -frtti
    b nortti_gc         -fno-rtti $GC
    b rtti_gc           -frtti $GC
    b nortti_final      -fno-rtti -DUSE_FINAL
    b nortti_lto        -fno-rtti -flto
    b nortti_final_lto  -fno-rtti -DUSE_FINAL -flto
    b nortti_lto_gc     -fno-rtti -flto $GC
    b vdtor             -fno-rtti -DVIRTUAL_DTOR
    b vdtor_gc          -fno-rtti -DVIRTUAL_DTOR $GC
    b vdtor_outline     -fno-rtti -DVIRTUAL_DTOR -DOUTLINE_DTOR
    b vdtor_outline_gc  -fno-rtti -DVIRTUAL_DTOR -DOUTLINE_DTOR $GC
    b only_uart_lto     -fno-rtti -DONLY_UART -flto
    b vdtor_gc_stubdel  -fno-rtti -DVIRTUAL_DTOR -DSTUB_DELETE $GC
    b vdtor_gc_nocxa    -fno-rtti -DVIRTUAL_DTOR -fno-use-cxa-atexit $GC
    b vdtor_gc_both     -fno-rtti -DVIRTUAL_DTOR -DSTUB_DELETE -fno-use-cxa-atexit $GC
    b rtti_gc_stubdel   -frtti -DSTUB_DELETE $GC
    ;;
dis)  arm-none-eabi-objdump -d --no-show-raw-insn -C $O/$2.elf | awk '/^[0-9a-f]+ <(via_|fanout)/{p=1} p{print} /^$/{p=0}' ;;
syms) arm-none-eabi-nm -C -S --size-sort $O/$2.elf | tail -${3:-25} ;;
gc)   # 호출 안 되는 가상/비가상 함수가 링크 결과에 남는가
    for n in nortti nortti_gc nortti_lto nortti_lto_gc; do
        printf '%-14s selftest(virtual)=%s debug_dump(non-virtual)=%s\n' $n \
            "$(arm-none-eabi-nm -C $O/$n.elf | grep -c 'UartDrv::selftest')" "$(arm-none-eabi-nm -C $O/$n.elf | grep -c 'UartDrv::debug_dump')"
    done ;;
vfe)  # clang + lld: Virtual Function Elimination (LTO 전용)
    F="--target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c++20 -O2 -fno-exceptions -fno-rtti -ffreestanding -nostdlib -fuse-ld=lld -Wl,-e,_start -ffunction-sections -fdata-sections -Wl,--gc-sections"
    clang++ $F $SRC -o $O/c0.elf
    clang++ $F -flto -fwhole-program-vtables -fvirtual-function-elimination -fvisibility=hidden -Wl,--lto-whole-program-visibility $SRC -o $O/c1.elf
    for e in c0 c1; do echo "$e: $(arm-none-eabi-size $O/$e.elf | tail -1 | cut -c1-24) selftest=$(arm-none-eabi-nm -C $O/$e.elf | grep -c selftest)"; done ;;
pure) # 순수 가상 슬롯: gcc(weak) vs clang(strong)
    arm-none-eabi-g++ -std=c++20 -O2 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -DVIRTUAL_DTOR -DOUTLINE_DTOR -c demo_board.cpp -o $O/gb.o
    clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c++20 -O2 -fno-exceptions -fno-rtti -DVIRTUAL_DTOR -DOUTLINE_DTOR -c demo_board.cpp -o $O/cb.o
    echo "gcc  : $(arm-none-eabi-nm $O/gb.o | grep pure)"; echo "clang: $(arm-none-eabi-nm $O/cb.o | grep pure)"
    a=$(arm-none-eabi-nm $O/vdtor_outline.elf | grep _ZTV7ByteBus | cut -d' ' -f1)
    arm-none-eabi-objdump -s -j .rodata --start-address=0x$a --stop-address=$((0x$a+32)) $O/vdtor_outline.elf | tail -2 ;;
esac
