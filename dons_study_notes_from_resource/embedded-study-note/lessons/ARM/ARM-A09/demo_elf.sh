#!/usr/bin/env bash
# ARM-A09 demo_elf.sh — Lima VM(elinux, aarch64 Linux) 에서 ELF 쪽 관찰 전부.
#   맥에서:  limactl shell elinux -- bash -lc 'bash <lesson>/demo_elf.sh'
#   bash demo_elf.sh full  → check_reloc.sh 용 덤프(.got 전체, pie_lld_norelax 의 .rela.dyn)를 끝에 더 찍는다
# 맥 홈은 VM 에서 읽기 전용 → /tmp/ARM-A09_work/elf 로 복사해서 빌드한다.
set -eu
SRC=$(cd "$(dirname "$0")" && pwd)
W=/tmp/ARM-A09_work/elf
mkdir -p "$W" && cd "$W"
cp "$SRC"/demo_elf.S "$SRC"/demo_elf_ext.c "$SRC"/demo_elf_main.c "$SRC"/demo_range.S .
gcc -c demo_elf.S -o demo_elf.o
gcc -O2 -c demo_elf_ext.c -o ext.o
gcc -O2 -c demo_elf_main.c -o main.o
gcc main.o demo_elf.o ext.o -o pie_bfd                          # 기본 = PIE, GNU ld.bfd
gcc -fuse-ld=lld main.o demo_elf.o ext.o -o pie_lld             # LLD (기본 relax 켜짐)
gcc -fuse-ld=lld -Wl,--no-relax main.o demo_elf.o ext.o -o pie_lld_norelax
gcc -static main.o demo_elf.o ext.o -o static_bfd                # 정적, non-PIE
for b in pie_bfd pie_lld pie_lld_norelax static_bfd; do echo "== run $b: $(./$b)"; done
echo "== readelf -rW demo_elf.o"; readelf -rW demo_elf.o
echo "== objdump -d demo_elf.o"; objdump -d demo_elf.o | sed -n '/<use_all>:/,$p'
for b in pie_bfd pie_lld pie_lld_norelax static_bfd; do
  echo "== nm $b"; nm "$b" | grep -E ' (use_all|g_val|g_tab|g_half|g_ptr|ext_val|ext_func|ext_tail)$'
  echo "== objdump -d $b use_all"; objdump -d --disassemble=use_all "$b" | sed -n '/<use_all>:/,/^$/p'
done
echo "== readelf -rW pie_bfd (.rela.dyn)"; readelf -rW pie_bfd | sed -n '/.rela.dyn/,/^$/p'
echo "== readelf -rW pie_lld (.rela.dyn)"; readelf -rW pie_lld | sed -n '/.rela.dyn/,/^$/p'
echo "== objdump -s -j .got static_bfd (head)"; objdump -s -j .got static_bfd | sed -n '1,8p'
echo "== shared object with non-PIC asm (expected to FAIL)"
gcc -shared demo_elf.o ext.o -o libbad.so 2>&1 | sed -n '1,3p' || true
gcc -fuse-ld=lld -shared demo_elf.o ext.o -o libbad.so 2>&1 | sed -n '1,3p' || true
echo "== range limits (demo_range.S + generated linker script)"
as demo_range.S -o range.o
mk() { printf ' . = 0x10000;\n .text.near : { *(.text.near) }\n . = %s;\n .text.far : { *(.text.far) }\n . = %s;\n .data.far : { *(.data.far) }\n' "$1" "$2" | sed '1s/^/SECTIONS {\n/;$s/$/\n}/' > r.ld; }
for d in 0x10000F000 0x100010000; do
  echo "-- far_data at $d (adrp at 0x10000), ld.bfd"; mk 0x20000 "$d"
  if ld -static -T r.ld -o r.elf range.o 2>&1; then objdump -d r.elf | sed -n '/<_start>:/,/^$/p' | sed -n '2p'; fi
done
for f in 0x8000FFC 0x8010008; do for L in ld.bfd ld.lld; do
  echo "-- far_func at $f (bl at 0x10008), $L"; mk "$f" 0x30000
  $L -static -T r.ld -o r.elf range.o 2>&1 && objdump -d r.elf | grep -E '^ +[0-9a-f]+:|>:'
done; done
echo "== misaligned :lo12: for an 8-byte LDR (demo_misalign.S)"
cp "$SRC"/demo_misalign.S . && as demo_misalign.S -o mis.o
for L in ld.bfd ld.lld; do echo "-- $L"; $L -static -o mis.elf mis.o 2>&1 | sed -n '1,3p' || true; done
if [ "${1:-}" = full ]; then
  echo "== readelf -rW pie_lld_norelax (.rela.dyn)"; readelf -rW pie_lld_norelax | sed -n '/.rela.dyn/,/^$/p'
  echo "== objdump -s -j .got static_bfd"; objdump -s -j .got static_bfd | sed -n '/Contents/,$p'
fi
