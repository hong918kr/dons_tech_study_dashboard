/* ENV: qemu */
// ARM-B13 demo — 일부러 틀린 Thumb-2 함수들 (7. 자주 하는 실수). run_qemu.sh bugs 가 BUG=1..6 을 하나씩 실행.
        .syntax unified
        .thumb
        .text

// BUG 1: 부호 없는 max 에 부호 있는 조건 (lt) 을 씀 → 0xFFFFFFFF 를 -1 로 본다
        .global max_u32_lt
        .type   max_u32_lt, %function
        .thumb_func
max_u32_lt:
        cmp     r0, r1
        it      lt                      // 틀림: lo 여야 한다
        movlt   r0, r1
        bx      lr
        .size   max_u32_lt, . - max_u32_lt

// BUG 2: r4 를 쓰면서 push 목록에서 빠뜨림 → 호출자의 r4 가 망가진다
        .global sum_clamped_nosave
        .type   sum_clamped_nosave, %function
        .thumb_func
sum_clamped_nosave:
        push    {r5, r6, r7, r8, lr}    // r4 없음!
        mov     r4, r0
        add.w   r5, r0, r1, lsl #2
        mov     r6, r2
        mov     r7, r3
        mov.w   r8, #0
1:      cmp     r4, r5
        beq     2f
        ldr     r0, [r4], #4
        mov     r1, r6
        mov     r2, r7
        bl      clamp_s32
        add     r8, r8, r0
        b       1b
2:      mov     r0, r8
        pop     {r5, r6, r7, r8, pc}
        .size   sum_clamped_nosave, . - sum_clamped_nosave

// BUG 3: non-leaf 인데 lr 을 저장하지 않음 → bl 이 lr 을 덮어써서 bx lr 이 루프 안으로 돌아간다
        .global sum_clamped_nolr
        .type   sum_clamped_nolr, %function
        .thumb_func
sum_clamped_nolr:
        push    {r4, r5, r6, r7, r8}    // lr 없음!
        mov     r4, r0
        add.w   r5, r0, r1, lsl #2
        mov     r6, r2
        mov     r7, r3
        mov.w   r8, #0
1:      cmp     r4, r5
        beq     2f
        ldr     r0, [r4], #4
        mov     r1, r6
        mov     r2, r7
        bl      clamp_s32               // lr ← 아래 add 의 주소|1
        add     r8, r8, r0
        b       1b
2:      mov     r0, r8
        pop     {r4, r5, r6, r7, r8}
        bx      lr                      // lr 은 이제 "add r8, r8, r0" 를 가리킨다
        .size   sum_clamped_nolr, . - sum_clamped_nolr

// BUG 4: n == 0 검사 없이 do-while → 첫 원소를 무조건 읽고, n-- 가 0xFFFFFFFF 로 래핑
        .global sum_noguard
        .type   sum_noguard, %function
        .thumb_func
sum_noguard:
        movs    r2, #0
1:      ldr     r3, [r0], #4            // n == 0 이어도 읽는다
        adds    r2, r2, r3
        subs    r1, r1, #1
        bne     1b
        mov     r0, r2
        bx      lr
        .size   sum_noguard, . - sum_noguard

// BUG 5/6: .type %function 도 .thumb_func 도 없는 라벨 → ELF 심볼이 함수/Thumb 로 표시되지 않음
        .global max_nothumb
        .p2align 2                      // 워드 정렬 주소 (BUG 7 과 비교)
max_nothumb:
        cmp     r0, r1
        it      lt
        movlt   r0, r1
        bx      lr

// BUG 7: 같은 실수, 그러나 라벨 주소가 4 의 배수가 아님 (2 바이트 어긋남)
        nop                             // 16비트 nop 하나 → 다음 라벨은 4n+2
        .global max_nothumb2
max_nothumb2:
        cmp     r0, r1
        it      lt
        movlt   r0, r1
        bx      lr
