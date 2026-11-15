#!/bin/bash
# TOOL-B14 demo_cross.sh — 같은 target.c 를 여러 타깃으로 크로스 컴파일하고 결과를 txt 로 남긴다.
#   bash demo_cross.sh        (Mac 에서 실행. 2부는 Lima VM 'elinux' 를 limactl 로 부른다)
# 결과:  objs/*.o  (작은 오브젝트, 각 < 4 KB)
#        cross_mac.txt        Mac: 빌드 명령 · file(1) · llvm-nm · riscv32 실패
#        cross_disasm.txt     scale()/checksum() 디스어셈블리 (Mac objdump + VM llvm-objdump)
#        cross_readelf_h.txt  VM readelf -h (모든 ELF 오브젝트 + 링크된 Cortex-M4 ELF 2개)
#        cross_arm_attr.txt   VM readelf -A (.ARM.attributes)
#        cross_link.txt       링크 실패/성공 실험 (Mac ld64, VM GNU ld / ld.lld)
#        cross_hdr64.txt      각 파일의 첫 64 바이트 (starter.c 테스트 배열의 원본)
set -u
cd "$(dirname "$0")"
L=$(pwd)
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
OBJDUMP=/usr/bin/objdump                                  # Apple LLVM 21 (/usr/local/bin 의 것은 LLVM 3.2 — 쓰지 말 것)
NM=/Library/Developer/CommandLineTools/usr/bin/llvm-nm
W=/tmp/TOOL-B14_work/demo; rm -rf "$W"; mkdir -p "$W"
rm -rf objs; mkdir objs
VMW=/tmp/TOOL-B14_work/vm
CF="-ffreestanding -Os -c"

run() { echo "\$ $*"; "$@" 2>&1; echo "[rc=$?]"; }

########## 1부: Mac (Apple clang 21) ##########
{
echo "### Mac: $(clang --version | head -1)"
echo "### host triple: $(clang -print-target-triple)"
run clang --target=aarch64-linux-gnu $CF target.c -o objs/aarch64_linux.o
run clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 $CF target.c -o objs/m4_hard.o
run clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mfloat-abi=soft $CF target.c -o objs/m4_soft.o
run clang --target=aarch64_be-linux-gnu $CF target.c -o objs/aarch64_be.o
run clang --target=x86_64-linux-gnu $CF target.c -o objs/x86_64_linux.o
run clang -arch x86_64 $CF target.c -o objs/x86_64_macho.o
run clang $CF target.c -o objs/arm64_macho.o
echo "### RISC-V 는 Mac clang 에 없다:"
run clang --target=riscv32-unknown-elf -march=rv32imac -mabi=ilp32 $CF target.c -o "$W/rv.o"
echo "### file(1)"
(cd objs && file *.o)
echo "### llvm-nm (soft-float 은 라이브러리 함수를 부른다)"
for f in m4_hard m4_soft x86_64_macho; do echo "-- $f.o"; $NM objs/$f.o; done
} > cross_mac.txt

{
for f in aarch64_linux m4_hard m4_soft x86_64_macho; do
  echo "################ $f.o  (Mac /usr/bin/objdump)"
  $OBJDUMP -d --no-show-raw-insn objs/$f.o 2>&1 | grep -v '^$'
done
} > cross_disasm.txt

{
echo "### 1) aarch64-linux-gnu: 컴파일은 되지만 링크는 Mac ld64 로 간다"
run clang --target=aarch64-linux-gnu -Os main.c target.c -o "$W/a64.elf"
echo "### -v 로 본 링크 명령 (마지막 줄)"
clang --target=aarch64-linux-gnu -Os main.c target.c -o "$W/a64.elf" -v 2>&1 | grep '/usr/bin/ld"' | sed "s#$TMPDIR#\$TMPDIR/#g; s#/var/folders/[^ ]*/T/#\$TMPDIR/#g"
echo "### 2) libc 헤더도 없다 (sysroot 없음)"
printf '#include <stdio.h>\nint main(void){puts("hi");return 0;}\n' > "$W/hello.c"
run clang --target=aarch64-linux-gnu -c "$W/hello.c" -o "$W/hello.o"
echo "### 3) thumbv7em-none-eabi: bare-metal 드라이버도 ld64 를 부른다"
run clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -Os main.c target.c -o "$W/m4.elf"
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -Os main.c target.c -o "$W/m4.elf" -v 2>&1 | grep '/usr/bin/ld"' | sed "s#/var/folders/[^ ]*/T/#\$TMPDIR/#g"
echo "### 4) ELF 링커(lld)를 달라고 해도 없다"
run clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -fuse-ld=lld -Os main.c target.c -o "$W/m4.elf"
echo "### 5) 런타임 라이브러리 디렉터리"
run ls /Library/Developer/CommandLineTools/usr/lib/clang/21/lib/
} > cross_link.txt

########## 2부: Lima VM elinux (Ubuntu aarch64) ##########
# Mac 홈은 VM 에서 읽기 전용 → 소스를 VM /tmp 로 복사, 결과 .o 는 base64 로 다시 가져온다.
limactl shell elinux -- bash -lc "rm -rf $VMW; mkdir -p $VMW; cp '$L/target.c' '$L/main.c' $VMW/; cp '$L'/objs/*.o $VMW/"
limactl shell elinux -- bash -lc "
set -u; cd $VMW; CF='$CF'
{
echo '### VM: '\$(clang --version | head -1)' / '\$(arm-none-eabi-gcc --version | head -1)
clang --target=riscv32-unknown-elf -march=rv32imac  -mabi=ilp32  \$CF target.c -o rv32imac.o
clang --target=riscv32-unknown-elf -march=rv32imafc -mabi=ilp32f \$CF target.c -o rv32imafc.o
clang --target=riscv32-unknown-elf -march=rv32ima   -mabi=ilp32  \$CF target.c -o rv32ima.o
clang --target=riscv64-unknown-elf -march=rv64gc    -mabi=lp64d  \$CF target.c -o rv64gc.o
clang --target=riscv32-unknown-elf -march=rv32ec    -mabi=ilp32e \$CF target.c -o rv32ec.o
clang --target=riscv32-unknown-elf -march=rv32imac_ztso -mabi=ilp32 \$CF target.c -o rv32imac_tso.o
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 \$CF target.c -o gcc_m4_hard.o
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=soft \$CF target.c -o gcc_m4_soft.o
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=softfp -mfpu=fpv4-sp-d16 \$CF target.c -o gcc_m4_softfp.o
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -Os -c main.c -o main_m4_soft.o
LK='--specs=nano.specs --specs=nosys.specs'
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -Os \$LK main.c target.c -o m4_hard.elf 2>/dev/null
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -Os \$LK main.c target.c -o m4_soft.elf 2>/dev/null
for f in aarch64_linux.o aarch64_be.o x86_64_linux.o m4_hard.o m4_soft.o gcc_m4_hard.o gcc_m4_soft.o rv32imac.o rv32imafc.o rv32ima.o rv64gc.o rv32ec.o rv32imac_tso.o m4_hard.elf m4_soft.elf; do
  echo \"=== \$f\"; readelf -h \$f | grep -E 'Class|Data|OS/ABI|Type|Machine|Entry|Flags|Size of this header'
done
} > readelf_h.txt
{
for f in m4_hard.o m4_soft.o gcc_m4_hard.o gcc_m4_softfp.o; do echo \"=== \$f\"; readelf -A \$f | grep -E 'Tag_CPU_name|Tag_CPU_arch|Tag_FP_arch|Tag_ABI_VFP_args|Tag_ABI_HardFP_use'; done
} > arm_attr.txt
{
for f in rv32imac rv32imafc; do echo \"################ \$f.o  (VM llvm-objdump 21)\"; llvm-objdump -d --no-show-raw-insn \$f.o | grep -v '^\$'; done
echo '################ llvm-nm rv32imac.o'; llvm-nm rv32imac.o
} > disasm_rv.txt
run() { echo \"\\\$ \$*\"; \"\$@\" 2>&1; echo \"[rc=\$?]\"; }
{
echo '### 6) VM: clang riscv32-unknown-elf 링크 — 호스트 GNU ld 는 RISC-V 를 모른다'
run clang --target=riscv32-unknown-elf -march=rv32imac -mabi=ilp32 -Os main.c target.c -o rv32.elf
echo '### 7) ld.lld 는 RISC-V 를 알지만 crt0/libc/libgcc 가 없다'
run clang --target=riscv32-unknown-elf -march=rv32imac -mabi=ilp32 -fuse-ld=lld -Os main.c target.c -o rv32.elf
echo '### 8) -nostdlib: soft-float 도우미 함수가 빈다'
run clang --target=riscv32-unknown-elf -march=rv32imac -mabi=ilp32 -fuse-ld=lld -nostdlib -Os main.c target.c -o rv32.elf
echo '### 9) arm-none-eabi-gcc (newlib) — specs 없이: 시스템 콜 스텁이 없다 (undefined reference 줄, 앞 5개)'
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -Os main.c target.c -o x.elf 2>&1 | grep -E 'undefined reference|collect2' | sed 's#/build/newlib-[^ ]*/newlib/libc/#<newlib>/libc/#' | head -5
echo '### 10) --specs=nano.specs --specs=nosys.specs: 링크 성공 (경고는 nosys 스텁)'
run arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -Os --specs=nano.specs --specs=nosys.specs main.c target.c -o y.elf
run arm-none-eabi-size m4_hard.elf m4_soft.elf
echo '### 11) 툴체인이 고르는 multilib 경로 (sysroot 대신 prefix 기준)'
run arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -print-multi-directory
run arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -print-file-name=crt0.o
run arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -print-file-name=libc_nano.a
echo '### 12) float ABI 가 다른 오브젝트 섞기'
run arm-none-eabi-ld main_m4_soft.o gcc_m4_hard.o -e main -o mix.elf
run ld.lld main_m4_soft.o gcc_m4_hard.o -e main -o mix_lld.elf
clang --target=riscv32-unknown-elf -march=rv32imafc -mabi=ilp32f -Os -c main.c -o main_rv_f.o
run riscv64-linux-gnu-ld -m elf32lriscv -r main_rv_f.o rv32imac.o -o mixrv.o
run ld.lld -r main_rv_f.o rv32imac.o -o mixrv2.o
echo '### 13) 완전한 Linux 크로스 툴체인 (sysroot 있음): riscv64-linux-gnu-gcc + qemu-user'
run riscv64-linux-gnu-gcc -Os main.c target.c -o rv64.elf
run riscv64-linux-gnu-gcc -print-sysroot
run qemu-riscv64 -L /usr/riscv64-linux-gnu ./rv64.elf
echo '### 14) clang 도 --target 만 주면 설치된 gcc-cross 를 찾아 쓴다 (crt/라이브러리 경로 = -### 에서 발췌)'
run clang --target=riscv64-linux-gnu -Os main.c target.c -o rv64c.elf
clang --target=riscv64-linux-gnu -Os main.c target.c -o rv64c.elf -### 2>&1 | tail -1 | tr ' ' '\n' | grep -E 'riscv64-linux-gnu-ld|Scrt1|crti|^.-L/usr/lib/gcc-cross/riscv64-linux-gnu/15.$' | tr '\n' ' '; echo
run qemu-riscv64 -L /usr/riscv64-linux-gnu ./rv64c.elf
} > link_vm.txt
for f in aarch64_linux.o m4_hard.o m4_soft.o gcc_m4_hard.o rv32imac.o rv32imafc.o rv32ima.o rv64gc.o rv32ec.o rv32imac_tso.o aarch64_be.o x86_64_linux.o m4_hard.elf m4_soft.elf; do
  echo \"=== \$f  (readelf: \$(readelf -h \$f | grep -E 'Machine|Flags' | sed 's/  */ /g' | tr '\n' ' '))\"; od -An -tx1 -N64 -v \$f
done > hdr_vm.txt
"
# VM 결과를 Mac 으로 가져온다 (텍스트는 cat, 오브젝트는 base64)
for f in rv32imac rv32imafc rv32ima rv64gc rv32ec rv32imac_tso gcc_m4_hard gcc_m4_soft; do
  limactl shell elinux -- base64 $VMW/$f.o | base64 -D > objs/$f.o
done
limactl shell elinux -- cat $VMW/disasm_rv.txt >> cross_disasm.txt
{ echo "################ Mac /usr/bin/objdump -d rv32imac.o (Mac LLVM 에 RISC-V 백엔드 없음)"
  $OBJDUMP -d objs/rv32imac.o 2>&1 | grep -v '^$'; } >> cross_disasm.txt
limactl shell elinux -- cat $VMW/readelf_h.txt > cross_readelf_h.txt
limactl shell elinux -- cat $VMW/arm_attr.txt > cross_arm_attr.txt
limactl shell elinux -- cat $VMW/link_vm.txt >> cross_link.txt
{
  limactl shell elinux -- cat $VMW/hdr_vm.txt
  for f in arm64_macho x86_64_macho; do echo "=== $f.o  (Mach-O)"; od -An -tx1 -N32 -v objs/$f.o | tr -s " "; done
} > cross_hdr64.txt

echo "objs/:"; ls -l objs | awk 'NR>1 {printf "  %-20s %5d B\n", $9, $5}'
echo "txt: $(ls cross_*.txt | tr '\n' ' ')"
