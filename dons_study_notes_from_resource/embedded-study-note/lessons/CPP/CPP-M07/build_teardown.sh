#!/bin/bash
# CPP-M07 build_teardown.sh — Lima VM elinux 의 arm-none-eabi-g++ 14.2.1 로 demo_teardown.cpp 를 링크해 비교한다.
#   limactl shell elinux -- bash -c 'rm -rf /tmp/CPP-M07; mkdir -p /tmp/CPP-M07 && cp <lesson dir>/* /tmp/CPP-M07/ && bash /tmp/CPP-M07/build_teardown.sh'
# (맥 홈은 VM 에서 읽기 전용이라 /tmp 로 복사해서 빌드한다)
cd "$(dirname "$0")"
F="-std=c++20 -O2 -fno-exceptions -fno-rtti -mcpu=cortex-m4 -mthumb -nostartfiles --specs=nosys.specs -Wl,--gc-sections -ffunction-sections -fdata-sections"
echo 'extern "C" { void* __dso_handle = nullptr; void _fini() {} }' > stubs.cpp
for c in 1 2 3 4; do
    echo "=== CASE=$c"
    if arm-none-eabi-g++ $F -DCASE=$c demo_teardown.cpp -o t$c.elf 2> err$c.txt; then
        echo "link without stubs: OK"
    else
        echo "link without stubs: FAILED"
        grep -E 'undefined reference' err$c.txt | sed -E "s/.*undefined reference to/  undefined:/" | sort -u
        arm-none-eabi-g++ $F -DCASE=$c demo_teardown.cpp stubs.cpp -o t$c.elf 2> err$c.txt || { grep -E 'undefined|error' err$c.txt | sed 's/.*ld: //' | sort -u | head; continue; }
        echo "link with __dso_handle + _fini stubs: OK"
    fi
    arm-none-eabi-size t$c.elf | tail -1
    arm-none-eabi-readelf -SW t$c.elf | grep -E '\.init_array|\.bss' | sed 's/^ *\[[ 0-9]*\] //'
    arm-none-eabi-nm -C t$c.elf | grep -E '__aeabi_atexit|__cxa_atexit| atexit|__register_exitproc|g_drv$|g_desc$' || true
done
