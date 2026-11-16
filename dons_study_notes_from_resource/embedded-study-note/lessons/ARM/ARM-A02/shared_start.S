/* ENV: qemu */
/* ARM-A02 shared_start.S — 부트 코드 + 세미호스팅 (ARM-M13 shared_start 축약판, TODO 없음).
 * qemu-system-aarch64 -M virt,gic-version=2 -cpu cortex-a53, MMU off, 코어 1개, EL1 부팅.
 * _start: SP 설정 → .bss 0 → 부팅 EL 기록 → main() → SYS_EXIT(main 반환값)
 */
    .section .text.boot, "ax"
    .global _start
    .type   _start, %function
_start:
    adrp    x0, __stack_top
    add     x0, x0, :lo12:__stack_top
    mov     sp, x0                      // SP_EL1 (EL1h 로 부팅: cpsr = 0x3c5)
    adrp    x0, __bss_start
    add     x0, x0, :lo12:__bss_start
    adrp    x1, __bss_end
    add     x1, x1, :lo12:__bss_end
1:  cmp     x0, x1                      // .bss 를 8 바이트씩 0 으로
    b.hs    2f
    str     xzr, [x0], #8
    b       1b
2:  mrs     x0, CurrentEL
    ubfx    x0, x0, #2, #2
    adrp    x1, g_boot_el
    str     x0, [x1, :lo12:g_boot_el]   // 하네스가 EL1 인지 검사한다
    bl      main                        // w0 = 종료 코드
    b       sh_exit
    .size   _start, . - _start

    .text
// uint64_t sh_call(uint32_t op, const void *param) — Arm 세미호스팅 (AArch64: HLT #0xF000)
    .global sh_call
    .type   sh_call, %function
sh_call:
    hlt     #0xf000
    ret
    .size   sh_call, . - sh_call

// void sh_exit(uint32_t code) — SYS_EXIT(0x18), x1 = {ADP_Stopped_ApplicationExit, code}
    .global sh_exit
    .type   sh_exit, %function
sh_exit:
    msr     daifset, #0xf               // 끝내는 중에 IRQ 가 끼어들지 않게
    mov     w3, w0
    sub     sp, sp, #16
    mov     x2, #0x0026
    movk    x2, #0x2, lsl #16           // 0x20026
    stp     x2, x3, [sp]
    mov     x1, sp
    mov     w0, #0x18
    hlt     #0xf000                     // QEMU 가 code 를 프로세스 종료 코드로 쓴다
3:  wfi
    b       3b
    .size   sh_exit, . - sh_exit
