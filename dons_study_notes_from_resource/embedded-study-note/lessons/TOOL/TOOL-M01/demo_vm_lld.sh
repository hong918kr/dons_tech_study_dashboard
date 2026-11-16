#!/usr/bin/env bash
# TOOL-M01 VM 비교: 같은 fw.o + 같은 solution.ld 를 GNU ld(bfd) 와 ld.lld 로 링크해 결과를 비교한다.
#   bash demo_vm_lld.sh > demo_vm_lld.txt        (맥에서 실행 → Lima VM elinux 안에서 돈다)
# 맥 홈은 VM 에 읽기 전용 → VM 의 /tmp/TOOL-M01_work/vm 으로 복사해서 작업.
set -u
cd "$(dirname "$0")" || exit 2
L=$(pwd)
limactl shell elinux -- bash -lc "rm -rf /tmp/TOOL-M01_work/vm && mkdir -p /tmp/TOOL-M01_work/vm &&
  cp '$L/fw.c' '$L/solution.ld' '$L/practice.ld' /tmp/TOOL-M01_work/vm/" || exit 2

limactl shell elinux -- bash -s <<'VM'
set -u
cd /tmp/TOOL-M01_work/vm || exit 2
CF="-mcpu=cortex-m4 -mthumb -Os -g -std=c11 -Wall -Wextra -Werror -ffreestanding -nostdlib -ffunction-sections -fdata-sections"
hdr() { echo; echo "===== $* ====="; }

hdr "0. 버전 (VM)"
arm-none-eabi-gcc --version | head -1
arm-none-eabi-ld --version | head -1
ld.lld --version

arm-none-eabi-gcc $CF -c fw.c -o fw.o

hdr "1. GNU ld (bfd) — arm-none-eabi-gcc -T solution.ld"
arm-none-eabi-gcc $CF -Wl,--gc-sections -Wl,--print-gc-sections -Wl,--print-memory-usage -T solution.ld fw.o -o bfd.elf 2>&1 |
	sed 's#^.*arm-none-eabi-ld: ##'
echo "rc=${PIPESTATUS[0]}"

hdr "2. ld.lld — ld.lld -T solution.ld (같은 fw.o)"
ld.lld --gc-sections --print-gc-sections -T solution.ld fw.o -o lld.elf 2>&1
echo "rc=$?"
echo "-- --print-memory-usage 는 lld 에도 있다:"
ld.lld --gc-sections --print-memory-usage -T solution.ld fw.o -o /dev/null 2>&1

for e in bfd lld; do
	hdr "3.$e readelf -S (alloc 섹션)"
	arm-none-eabi-readelf -S -W $e.elf | grep -E '\] \.(isr_vector|text|rodata|data|bss|ARM)'
	hdr "4.$e readelf -l"
	arm-none-eabi-readelf -l -W $e.elf | sed -n '/Entry point/p;/Program Headers/,/^$/p;/Segment Sections/,/^$/p'
	hdr "5.$e 링커 스크립트 심볼"
	arm-none-eabi-readelf -s -W $e.elf | awk '$8 ~ /^_(sidata|sdata|edata|sbss|ebss|estack|etext)$/ {print $2, $4, $7, $8}' | sort -k4
	arm-none-eabi-objcopy -O binary $e.elf $e.bin
	echo "$e.bin = $(stat -c %s $e.bin) bytes  sha256=$(sha256sum $e.bin | cut -c1-16)"
done

hdr "6. 두 .bin 비교"
cmp bfd.bin lld.bin && echo "bfd.bin == lld.bin (바이트 단위로 같다)"
cmp -l bfd.bin lld.bin | awk '{printf "offset %d: bfd=%s lld=%s (8진수)\n", $1-1, $2, $3}'
echo "-- 그 자리 (0x0800006e) 는 Default_Handler(2 바이트) 뒤, main 의 4-정렬 빈칸:"
arm-none-eabi-objdump -d lld.elf | sed -n '/<Default_Handler>:/,/<main>:/p'
echo "-- 출력 섹션 채움 값(fill)을 =0x00000000 으로 주면:"
sed 's/^    } > FLASH$/    } > FLASH =0x00000000/' solution.ld >fill.ld
ld.lld --gc-sections -T fill.ld fw.o -o lldfill.elf && arm-none-eabi-objcopy -O binary lldfill.elf lldfill.bin &&
	cmp bfd.bin lldfill.bin && echo "bfd.bin == lldfill.bin"

hdr "7. 파일 크기 / 섹션 수"
ls -l bfd.elf lld.elf | awk '{print $5, $9}'
for e in bfd lld; do echo "$e: $(arm-none-eabi-readelf -S -W $e.elf | grep -c '^ *\[') section headers"; done

hdr "8. practice.ld 도 lld 로 (bootloader offset + .noinit + CCMRAM)"
arm-none-eabi-gcc $CF -DPRACTICE -c fw.c -o fwp.o
ld.lld --gc-sections -T practice.ld fwp.o -o lldp.elf 2>&1; echo "rc=$?"
arm-none-eabi-readelf -S -W lldp.elf | grep -E '\] \.(isr_vector|data|bss|noinit|ccmram)'
arm-none-eabi-readelf -l -W lldp.elf | grep LOAD
arm-none-eabi-objcopy -O binary lldp.elf lldp.bin
echo "lldp.bin = $(stat -c %s lldp.bin) bytes (objcopy 는 섹션 기준 → 0x08000000 의 헤더 세그먼트는 .bin 에 없음)"
echo "-- 0x08000000 세그먼트의 첫 4 바이트 (파일 offset 0):"
od -An -c -N4 lldp.elf

hdr "9. PHDRS 로 세그먼트를 직접 정하면 헤더 세그먼트가 사라진다 (깊은 내용은 TOOL-A07)"
python3 - <<'EOF'
s = open("practice.ld").read()
s = s.replace("SECTIONS\n{", "PHDRS\n{\n    text PT_LOAD;\n    data PT_LOAD;\n    bss  PT_LOAD;\n    ccm  PT_LOAD;\n}\n\nSECTIONS\n{", 1)
s = s.replace("    } > FLASH\n", "    } > FLASH :text\n")
s = s.replace("} > RAM AT> FLASH", "} > RAM AT> FLASH :data")
s = s.replace("        _ebss = .;\n    } > RAM", "        _ebss = .;\n    } > RAM :bss")
s = s.replace("        _enoinit = .;\n    } > RAM", "        _enoinit = .;\n    } > RAM :bss")
s = s.replace("} > CCMRAM", "} > CCMRAM :ccm")
open("phdrs.ld", "w").write(s)
EOF
grep -A6 '^PHDRS' phdrs.ld
ld.lld --gc-sections -T phdrs.ld fwp.o -o lldph.elf; echo "rc=$?"
arm-none-eabi-readelf -l -W lldph.elf | grep LOAD
VM
