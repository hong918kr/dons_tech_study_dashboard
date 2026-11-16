/* ENV: qemu */
/* ARM-A03 shared_start.S — 부트 코드 + 세미호스팅 호출 (주어짐, TODO 없음). (ARM-M13 shared_start 축약판)
 * qemu-system-aarch64 -M virt -cpu cortex-a53 → EL1 로 시작, MMU off, 코어 1개.
 * _start: SP 설정 → .bss 0 → 부팅 EL 기록 → main() → SYS_EXIT(main 반환값)
 * (EL2/EL3 에서 내려오는 코드는 ARM-M13 에 있다. 이 레슨은 -M virt = EL1 부팅만 쓴다.)
 */
    .section .text.boot, "ax"
    .global _start
    .type   _start, %function
_start:
    adrp    x0, __stack_top
    add     x0, x0, :lo12:__stack_top
    mov     sp, x0                      // SP_EL1 = 부트 스택 꼭대기
    adrp    x0, __bss_start
    add     x0, x0, :lo12:__bss_start
    adrp    x1, __bss_end
    add     x1, x1, :lo12:__bss_end
1:  cmp     x0, x1                      // .bss 0 채우기 (변환 테이블도 .bss 에 있다 → 처음엔 전부 invalid)
    b.hs    2f
    str     xzr, [x0], #8
    b       1b
2:  mrs     x0, CurrentEL               // [3:2] = EL
    ubfx    x0, x0, #2, #2
    adrp    x1, g_boot_el
    str     x0, [x1, :lo12:g_boot_el]
    bl      main                        // w0 = main 의 반환값 = 종료 코드
    b       sh_exit
    .size   _start, . - _start

    .text
// uint64_t sh_call(uint32_t op, const void *param) — Arm 세미호스팅 (AArch64: HLT #0xF000, w0 = op, x1 = param)
    .global sh_call
    .type   sh_call, %function
sh_call:
    hlt     #0xf000
    ret
    .size   sh_call, . - sh_call

// void sh_exit(uint32_t code) — SYS_EXIT(0x18). x1 = {ADP_Stopped_ApplicationExit 0x20026, code} 블록의 주소
    .global sh_exit
    .type   sh_exit, %function
sh_exit:
    mov     w3, w0
    sub     sp, sp, #16
    mov     x2, #0x0026
    movk    x2, #0x2, lsl #16
    stp     x2, x3, [sp]
    mov     x1, sp
    mov     w0, #0x18
    hlt     #0xf000                     // QEMU 가 code 를 프로세스 종료 코드로 쓴다
3:  wfi
    b       3b
    .size   sh_exit, . - sh_exit
