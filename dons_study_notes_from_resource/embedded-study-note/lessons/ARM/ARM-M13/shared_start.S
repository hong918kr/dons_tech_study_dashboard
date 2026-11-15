/* ENV: qemu */
/* ARM-M13 shared_start.S — 부트 코드 + 세미호스팅 호출 (주어짐, TODO 없음).
 * qemu-system-aarch64 -M virt -cpu cortex-a53, MMU off, 코어 1개.
 * _start: SP 설정 → .bss 0 → 부트 EL 기록 → (EL3/EL2 면 아래 EL 로 내려가기) → main() → SYS_EXIT(main 반환값)
 */
    .section .text.boot, "ax"
    .global _start
    .type   _start, %function
_start:
    adrp    x0, __stack_top
    add     x0, x0, :lo12:__stack_top
    mov     sp, x0                      // 지금 EL 의 SP (EL1 부팅이면 SP_EL1, EL2 면 SP_EL2)
    adrp    x0, __bss_start
    add     x0, x0, :lo12:__bss_start
    adrp    x1, __bss_end
    add     x1, x1, :lo12:__bss_end
1:  cmp     x0, x1                      // .bss 를 8 바이트씩 0 으로 (링커 스크립트가 16 정렬 보장)
    b.hs    2f
    str     xzr, [x0], #8
    b       1b
2:  mrs     x0, CurrentEL               // [3:2] = EL
    ubfx    x0, x0, #2, #2
    adrp    x1, g_boot_el
    str     x0, [x1, :lo12:g_boot_el]   // 부팅한 EL 을 기록 (하네스가 출력·검사)
3:  mrs     x0, CurrentEL               // 내려가기 루프: EL3 → (EL2) → EL1
    ubfx    x0, x0, #2, #2
    cmp     x0, #3
    b.ne    4f
    bl      el3_to_lower                // 연습 4 (practice.S). 기본(weak)은 그냥 ret → EL3 에 머문다
    mrs     x0, CurrentEL
    ubfx    x0, x0, #2, #2
    cmp     x0, #3
    b.eq    5f                          // 못 내려갔으면 그대로 main (하네스가 FAIL 로 보고)
    b       3b
4:  cmp     x0, #2
    b.ne    5f
    bl      el2_to_el1                  // Part C (starter/solution.S). 이 "함수" 는 EL1 에서 돌아온다
5:  bl      main                        // w0 = main 의 반환값 = 종료 코드
    b       sh_exit
    .size   _start, . - _start

// 기본 el3_to_lower: 아무것도 안 함 (practice.S 가 강한 심볼로 덮어쓴다)
    .weak   el3_to_lower
    .type   el3_to_lower, %function
el3_to_lower:
    ret
    .size   el3_to_lower, . - el3_to_lower

    .text
// uint64_t sh_call(uint32_t op, const void *param) — Arm 세미호스팅 (AArch64: HLT #0xF000, w0 = op, x1 = param)
    .global sh_call
    .type   sh_call, %function
sh_call:
    hlt     #0xf000
    ret
    .size   sh_call, . - sh_call

// void sh_exit(uint32_t code) — SYS_EXIT(0x18). AArch64 는 x1 = {reason, subcode} 2 워드 블록의 주소
    .global sh_exit
    .type   sh_exit, %function
sh_exit:
    mov     w3, w0                      // w 쓰기 = 위 32 비트 0 (code 를 64 비트로)
    sub     sp, sp, #16
    mov     x2, #0x0026
    movk    x2, #0x2, lsl #16           // ADP_Stopped_ApplicationExit = 0x20026
    stp     x2, x3, [sp]                // [sp] = reason, [sp+8] = exit code
    mov     x1, sp
    mov     w0, #0x18
    hlt     #0xf000                     // QEMU 가 code 를 프로세스 종료 코드로 쓴다
6:  wfi                                 // (세미호스팅이 꺼져 있으면 여기 멈춘다 → run_qemu.sh 의 timeout)
    b       6b
    .size   sh_exit, . - sh_exit
