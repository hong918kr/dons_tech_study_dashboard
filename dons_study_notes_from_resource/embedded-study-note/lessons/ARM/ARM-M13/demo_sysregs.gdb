# ARM-M13 demo_sysregs.gdb — run_qemu.sh gdb 가 쓴다 (QEMU -s -S 에 gdb-multiarch 접속).
# QEMU 10.2 gdbstub 은 AArch64 시스템 레지스터를 $ESR_EL1, $ELR_EL1, $SPSR_EL1, $VBAR_EL1, $FAR_EL1, $SP_EL0, $SP_EL1 로 준다.
# CurrentEL 은 목록에 없다 → $cpsr 의 M[3:2] 로 본다.
set pagination off
set confirm off
target remote :1234
printf "(1) reset    pc=%#lx cpsr=%#lx EL=%d VBAR_EL1=%#lx SCTLR_EL1=%#lx\n", $pc, $cpsr, ($cpsr >> 2) & 3, $VBAR_EL1, $SCTLR_EL1
break svc42_insn
continue
printf "(2) at svc   pc=%#lx cpsr=%#lx sp=%#lx VBAR_EL1=%#lx\n", $pc, $cpsr, $sp, $VBAR_EL1
stepi
printf "(3) stepi -> pc=%#lx (VBAR+%#lx) cpsr=%#lx sp=%#lx\n", $pc, $pc - $VBAR_EL1, $cpsr, $sp
printf "             ESR_EL1=%#lx ELR_EL1=%#lx SPSR_EL1=%#lx\n", $ESR_EL1, $ELR_EL1, $SPSR_EL1
x/4i $pc
break *(svc42_insn + 1)
continue
printf "(4) after eret pc=%#lx cpsr=%#lx x0=%ld\n", $pc, $cpsr, $x0
break el0_svc_mul10
continue
printf "(5) EL0      pc=%#lx cpsr=%#lx EL=%d sp=%#lx SP_EL0=%#lx SP_EL1=%#lx\n", $pc, $cpsr, ($cpsr >> 2) & 3, $sp, $SP_EL0, $SP_EL1
stepi
printf "(6) svc #1 -> pc=%#lx (VBAR+%#lx) EL=%d ESR_EL1=%#lx SPSR_EL1=%#lx sp=%#lx\n", $pc, $pc - $VBAR_EL1, ($cpsr >> 2) & 3, $ESR_EL1, $SPSR_EL1, $sp
delete
continue
