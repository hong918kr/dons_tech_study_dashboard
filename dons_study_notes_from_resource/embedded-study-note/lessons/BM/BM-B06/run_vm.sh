#!/bin/bash
# ENV: qemu — BM-B06 Lima VM `elinux` 안에서 실행 (arm-none-eabi-gcc 14.2.1 / binutils ld 2.45.50). 실행은 안 한다 (점프 = BM-A03).
# Mac 홈은 VM 에서 읽기 전용 → 소스를 /tmp/BM-B06_work 로 복사해 빌드한다.
#   limactl shell elinux -- bash -lc 'bash <ROOT>/lessons/BM/BM-B06/run_vm.sh [build|bad|capture]'
#   capture 후 Mac 에서: limactl copy 'elinux:/tmp/BM-B06_work/captured/*' <ROOT>/lessons/BM/BM-B06/
set -u
SRC="$(cd "$(dirname "$0")" && pwd)"
W=/tmp/BM-B06_work
rm -rf "$W"; mkdir -p "$W"
cp "$SRC"/layout.ld "$SRC"/boot.ld "$SRC"/app.ld "$SRC"/shared.h "$SRC"/boot.c "$SRC"/app.c "$W"/
cd "$W" || exit 1
CF="-mcpu=cortex-m4 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib -fno-tree-loop-distribute-patterns"
SECTS="-j .img_hdr -j .isr_vector -j .text -j .rodata -j .data -j .bss -j .noinit"

build() {
    arm-none-eabi-gcc $CF -T boot.ld -Wl,-Map=boot.map boot.c -o boot.elf || exit 1
    arm-none-eabi-gcc $CF -T app.ld  -Wl,-Map=app.map  app.c  -o app.elf  || exit 1
}
memcfg() { sed -n '/^Memory Configuration/,/^Linker script and memory map/p' "$1" | sed '$d'; }

# 고장 난 변형: $1 = 디렉터리 이름. 원본을 복사해 둔다 (원본 빌드 디렉터리는 건드리지 않음)
variant() { rm -rf "$1"; mkdir -p "$1"; cp layout.ld boot.ld app.ld shared.h boot.c app.c "$1"/; }
noassert() { sed -i '/^ASSERT/d' layout.ld boot.ld app.ld; }      # "ASSERT 가 없었다면" 을 재현
tagfile() { printf '#include <stdint.h>\n__attribute__((section(".img_hdr"), used)) const uint32_t g_build_tag = 0x7A67u;\n' > tag.c; }

bad() {   # 각 실수를 ld 가 어떻게 막는지 (실제 메시지)
    echo "### (1) SLOT_B 를 0x08060000 으로 옮김 (SLOT_A 와 128 KB 겹침)"
    variant b1; ( cd b1 && sed -i 's/SLOT_B  (r)   : ORIGIN = 0x08080000/SLOT_B  (r)   : ORIGIN = 0x08060000/' layout.ld &&
                  arm-none-eabi-gcc $CF -T app.ld app.c -o x.elf ) 2>&1 | sed 's|^/usr/.*/bin/ld: |ld: |'
    echo "### (2) 부트로더에 40 KB const 테이블 (BOOT = 32 KB)"
    variant b2; ( cd b2 && arm-none-eabi-gcc $CF -DBLOAT -T boot.ld boot.c -o x.elf ) 2>&1 | sed 's|^/usr/.*/bin/ld: |ld: |'
    echo "### (3) 다른 파일(tag.c)의 데이터가 .img_hdr 맨 앞에 링크됨"
    variant b3; ( cd b3 && tagfile && arm-none-eabi-gcc $CF -T app.ld tag.c app.c -o x.elf ) 2>&1 | sed 's|^/usr/.*/bin/ld: |ld: |'
    echo "### (4) 앱의 섹션 이름 오타 .noinit_shared (orphan 섹션)"
    variant b4; ( cd b4 && arm-none-eabi-gcc $CF -DTYPO_NOINIT -T app.ld app.c -o x.elf ) 2>&1 | sed 's|^/usr/.*/bin/ld: |ld: |'
    echo "### (5) (4) 를 ASSERT 없이 + --orphan-handling=warn"
    variant b5; ( cd b5 && noassert && arm-none-eabi-gcc $CF -DTYPO_NOINIT -T app.ld app.c -o x.elf \
                  -Wl,--orphan-handling=warn 2>&1 | grep -e noinit -e error ; echo "(.glue_7 / .iplt 등 linker stub orphan 경고와 .debug_* 경고는 grep 으로 뺐다)" ) | sed 's|^/usr/.*/bin/ld: |ld: |'
    echo "### (6) MEMORY 영역 두 개가 겹쳐도 ld 자체는 조용한가? (ASSERT 제거, SLOT_B=0x08060000)"
    variant b6; ( cd b6 && noassert && sed -i 's/SLOT_B  (r)   : ORIGIN = 0x08080000/SLOT_B  (r)   : ORIGIN = 0x08060000/' layout.ld &&
                  arm-none-eabi-gcc $CF -T app.ld app.c -o x.elf && echo "link OK (exit 0) — 경고 없음" ) 2>&1 | sed 's|^/usr/.*/bin/ld: |ld: |'
    echo "### (7) 한 링크 안에서 섹션끼리 겹치면? (.rodata 를 0x08020300 에 강제 — .text 와 겹침)"
    variant b7; ( cd b7 && sed -i 's|    .rodata : {|    .rodata 0x08020300 : {|' app.ld &&
                  arm-none-eabi-gcc $CF -T app.ld app.c -o x.elf ) 2>&1 | sed 's|^/usr/.*/bin/ld: |ld: |'
    echo "### (8) 공유 변수 섹션을 .bootshare (GCC 가 PROGBITS 로 냄) 로 바꾸고 NOLOAD 를 빼면 → objcopy -O binary 크기"
    variant b8; ( cd b8 && sed -i 's/\.noinit\.shared/.bootshare/' shared.h &&
                  sed -i 's|    .noinit (NOLOAD) : { KEEP(\*(.noinit.shared)) }|    .noinit : { KEEP(*(.bootshare)) }|' app.ld &&
                  arm-none-eabi-gcc $CF -T app.ld app.c -o x.elf && arm-none-eabi-objdump -h -j .noinit x.elf | tail -2 &&
                  arm-none-eabi-objcopy -O binary x.elf x.bin && echo "app.bin = $(stat -c %s x.bin) bytes"; rm -f x.bin ) 2>&1
    echo "### (9) .img_hdr 규칙에서 KEEP 를 빼고 -Wl,--gc-sections"
    variant b9; ( cd b9 && sed -i 's|KEEP(\*(.img_hdr))|*(.img_hdr)|' app.ld &&
                  arm-none-eabi-gcc $CF -T app.ld -Wl,--gc-sections app.c -o x.elf ) 2>&1 | sed 's|^/usr/.*/bin/ld: |ld: |'
}

case "${1:-build}" in
build)
    build
    arm-none-eabi-objdump -h $SECTS boot.elf
    arm-none-eabi-objdump -h $SECTS app.elf
    arm-none-eabi-objdump -s -j .img_hdr app.elf | sed -n '/Contents/,$p'
    echo "--- app.map: Memory Configuration (boot.map 과 같은지: $(cmp -s <(memcfg boot.map) <(memcfg app.map) && echo 같음 || echo 다름)) ---"
    memcfg app.map
    echo "--- app.map: 공유 변수 / 헤더 / 크기 심볼 ---"
    grep -E '^ *0x[0-9a-f]+ +(g_img_hdr|g_boot_share|__app_image_size|__app_image_end|__slot_a_vectors|_estack)( =|$)' app.map
    echo "ASSERT 줄 $(grep -c 'ASSERT (' app.map) 개, 모두 값 0x00000001(참)인가: $(grep 'ASSERT (' app.map | grep -vc '0x00000001') 개가 아님"
    echo "--- boot.map ---"
    grep -E '^ *0x[0-9a-f]+ +(g_boot_share|__boot_end|_estack)( =|$)' boot.map
    arm-none-eabi-size boot.elf app.elf
    echo "build exit=0" ;;
bad) bad ;;
capture)   # Mac 쪽 검사기(starter/solution/practice.c)가 읽는 파일들
    build
    mkdir -p captured
    arm-none-eabi-objdump -h boot.elf > captured/cap_boot.txt
    { arm-none-eabi-objdump -h app.elf; arm-none-eabi-objdump -s -j .img_hdr app.elf; } > captured/cap_app.txt
    memcfg app.map > captured/cap_memcfg.txt
    variant c1; ( cd c1 && noassert && sed -i 's/SLOT_B  (r)   : ORIGIN = 0x08080000/SLOT_B  (r)   : ORIGIN = 0x08060000/' layout.ld &&
                  arm-none-eabi-gcc $CF -T app.ld -Wl,-Map=x.map app.c -o x.elf ) && memcfg c1/x.map > captured/cap_bad_overlap_memcfg.txt
    variant c2; ( cd c2 && noassert && arm-none-eabi-gcc $CF -DTYPO_NOINIT -T app.ld app.c -o x.elf ) &&
        { arm-none-eabi-objdump -h c2/x.elf; arm-none-eabi-objdump -s -j .img_hdr c2/x.elf; } > captured/cap_bad_orphan.txt
    variant c3; ( cd c3 && noassert && tagfile && arm-none-eabi-gcc $CF -T app.ld tag.c app.c -o x.elf ) &&
        { arm-none-eabi-objdump -h c3/x.elf; arm-none-eabi-objdump -s -j .img_hdr c3/x.elf; } > captured/cap_bad_hdr.txt
    bad > captured/ld_errors.txt 2>&1
    ls -l captured; echo "capture exit=0" ;;
*) echo "usage: run_vm.sh [build|bad|capture]"; exit 2 ;;
esac
