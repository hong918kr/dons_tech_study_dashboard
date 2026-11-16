# ARM-A02 demo_irq.gdb — run_qemu.sh gdb 가 쓴다. 첫 IRQ 에서 SAVE_ALL 이 끝나고 C 를 부르기 직전(bl exc_c)에 멈춘다.
set pagination off
set confirm off
target remote :1234
# 0x280 IRQ 엔트리의 29 번째 명령 = bl exc_c (run_qemu.sh objdump 참고)
break *((char *)vectors_el1 + 0x2ec)
continue
printf "--- (1) hardware state at the bl ---\n"
printf "pc=%#lx cpsr=%#lx EL=%ld sp=%#lx x0=%#lx x1=%ld\n", $pc, $cpsr, ($cpsr >> 2) & 3, $sp, $x0, $x1
printf "ELR_EL1=%#lx SPSR_EL1=%#lx ESR_EL1=%#lx\n", $ELR_EL1, $SPSR_EL1, $ESR_EL1
printf "--- (2) trap frame at sp (SAVE_ALL) ---\n"
printf "elr=%#lx spsr=%#lx esr=%#lx sp_el0=%#lx\n", ((struct trap_frame *)$sp)->elr, ((struct trap_frame *)$sp)->spsr, ((struct trap_frame *)$sp)->esr, ((struct trap_frame *)$sp)->sp_el0
printf "x[30]=%#lx  x[29]=%#lx  sizeof=%ld\n", ((struct trap_frame *)$sp)->x[30], ((struct trap_frame *)$sp)->x[29], sizeof(struct trap_frame)
info symbol ((struct trap_frame *)$sp)->elr
info symbol ((struct trap_frame *)$sp)->x[30]
x/4gx $sp + 248
printf "--- (3) GIC before IAR is read ---\n"
printf "GICC_HPPIR=%#x GICD_ISPENDR0=%#x GICC_RPR=%#x\n", *(unsigned *)0x08010018, *(unsigned *)0x08000200, *(unsigned *)0x08010014
kill
quit
