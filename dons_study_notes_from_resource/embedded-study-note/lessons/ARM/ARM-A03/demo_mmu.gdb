# ARM-A03 demo_mmu.gdb — run_qemu.sh gdb 가 쓴다 (QEMU -s -S 에 gdb-multiarch 접속).
# MMU 를 켜기 전/후의 시스템 레지스터, 테이블 내용, 그리고 "gdb 의 x 명령은 VA 를 읽는다" 를 확인한다.
set pagination off
set confirm off
target remote :1234
break mmu_enable
continue
printf "(1) before   SCTLR_EL1=%#lx TCR_EL1=%#lx MAIR_EL1=%#lx TTBR0_EL1=%#lx\n", $SCTLR_EL1, $TCR_EL1, $MAIR_EL1, $TTBR0_EL1
printf "    args     x0(ttbr0)=%#lx x1(tcr)=%#lx x2(mair)=%#lx\n", $x0, $x1, $x2
printf "    l1[0..2] / l2_ram[0], l2_ram[64]:\n"
x/3gx &l1
x/gx &l2_ram[0]
x/gx &l2_ram[64]
break *(mmu_enable + 0x44)
continue
x/i $pc
printf "(2) after    SCTLR_EL1=%#lx TCR_EL1=%#lx MAIR_EL1=%#lx TTBR0_EL1=%#lx\n", $SCTLR_EL1, $TCR_EL1, $MAIR_EL1, $TTBR0_EL1
break remap_block
continue
printf "(3) at remap_block: gdb memory reads use the guest MMU (VA):\n"
x/2gx 0x46800100
x/2gx 0x48000100
x/2gx 0x86800100
printf "(4) QEMU gdbstub physical-memory mode: addresses are PAs now (0x46600000 = plain RAM)\n"
maint packet Qqemu.PhyMemMode:1
x/2gx 0x46800100
x/gx 0x46600000
maint packet Qqemu.PhyMemMode:0
monitor info tlb
delete
printf "(5) back to VA mode: unmapped VA (VA_HOLE) through gdb:\n"
x/gx 0x46600000
