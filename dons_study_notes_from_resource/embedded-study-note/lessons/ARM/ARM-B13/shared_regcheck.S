/* ENV: qemu */
// ARM-B13 공용 검사기 — "callee-saved 레지스터를 지켰나?" 를 테스트로 만든다 (모든 변형에 링크).
//
// int32_t rc_call4(const void *fn, const int32_t args[4], uint32_t *bad)
//   r4–r11 에 표지값(sentinel) 0xC0DE00nn 을 넣고 fn(args[0], args[1], args[2], args[3]) 를 부른 뒤,
//   r4–r11 과 sp 가 그대로인지 본다. *bad = 망가진 레지스터 비트마스크 (bit n = rn, bit 13 = sp).
//   반환값 = fn 의 반환값 (r0). 인자를 덜 받는 함수도 OK: 남는 r1–r3 은 그냥 무시된다 (AAPCS).
        .syntax unified
        .thumb
        .text

        .global rc_call4
        .type   rc_call4, %function
        .thumb_func
rc_call4:
        push    {r2, r4-r11, lr}        // 10 워드 = 40 B → sp 8 바이트 정렬 유지, r2(=bad) 도 보관
        mov     r3, sp
        ldr     r2, =rc_sp_before       // 리터럴 풀에서 주소 로드
        str     r3, [r2]                // 호출 직전 sp 기록
        ldr     r4, =0xC0DE0004         // 표지값: 수정 즉시값(modified imm)으로 못 만드는 수 → 리터럴 풀
        ldr     r5, =0xC0DE0005
        ldr     r6, =0xC0DE0006
        ldr     r7, =0xC0DE0007
        ldr     r8, =0xC0DE0008
        ldr     r9, =0xC0DE0009
        ldr     r10, =0xC0DE000A
        ldr     r11, =0xC0DE000B
        mov     r12, r0                 // fn (bit0 = 1 이어야 Thumb 상태 유지)
        ldm     r1, {r0-r3}             // args[0..3] → r0..r3
        blx     r12                     // 호출. lr = 다음 명령 주소 | 1
        movs    r3, #0                  // r3 = bad 마스크 (r0 = fn 반환값은 건드리지 않는다)
        ldr     r2, =0xC0DE0004
        cmp     r4, r2
        it      ne
        orrne   r3, r3, #(1 << 4)
        ldr     r2, =0xC0DE0005
        cmp     r5, r2
        it      ne
        orrne   r3, r3, #(1 << 5)
        ldr     r2, =0xC0DE0006
        cmp     r6, r2
        it      ne
        orrne   r3, r3, #(1 << 6)
        ldr     r2, =0xC0DE0007
        cmp     r7, r2
        it      ne
        orrne   r3, r3, #(1 << 7)
        ldr     r2, =0xC0DE0008
        cmp     r8, r2
        it      ne
        orrne   r3, r3, #(1 << 8)
        ldr     r2, =0xC0DE0009
        cmp     r9, r2
        it      ne
        orrne   r3, r3, #(1 << 9)
        ldr     r2, =0xC0DE000A
        cmp     r10, r2
        it      ne
        orrne   r3, r3, #(1 << 10)
        ldr     r2, =0xC0DE000B
        cmp     r11, r2
        it      ne
        orrne   r3, r3, #(1 << 11)
        mov     r1, sp
        ldr     r2, =rc_sp_before
        ldr     r2, [r2]
        cmp     r1, r2
        it      ne
        orrne   r3, r3, #(1 << 13)      // sp 가 달라졌다 (push/pop 짝이 안 맞음)
        ldr     r1, [sp]                // 보관해 둔 bad 포인터
        str     r3, [r1]
        pop     {r2, r4-r11, pc}        // 복원 + pc ← lr 로 리턴 (bit0 → Thumb)
        .ltorg                          // 리터럴 풀을 여기에 (ldr r, =imm 의 상수들)
        .size   rc_call4, . - rc_call4

        .bss
        .p2align 2
rc_sp_before:
        .space  4
