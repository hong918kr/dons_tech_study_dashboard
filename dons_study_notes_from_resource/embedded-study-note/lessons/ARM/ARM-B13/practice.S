/* ENV: qemu */
// ARM-B13 추가 연습 답 — Thumb-2, Cortex-M3. run_qemu.sh practice (practice.c + practice.S + shared_*)
        .syntax unified
        .thumb
        .text

// 연습 1: int32_t sign_s32(int32_t x)  → -1 / 0 / +1, ITE 하나로 (양쪽 칸을 다 쓴다)
        .global sign_s32
        .type   sign_s32, %function
        .thumb_func
sign_s32:
        cmp     r0, #0                  // 플래그 ← x - 0
        ite     gt                      // T: GT,  E: LE
        movgt   r0, #1                  //   x > 0  → 1          (IT 안: 16비트 mov)
        asrle   r0, r0, #31             //   x <= 0 → 0 은 0, 음수는 -1  (IT 안: 16비트 asr)
        bx      lr
        .size   sign_s32, . - sign_s32

// 연습 2: int32_t max_array_s32(const int32_t *a, uint32_t n)   n == 0 → INT32_MIN
        .global max_array_s32
        .type   max_array_s32, %function
        .thumb_func
max_array_s32:
        mov     r2, #0x80000000         // best = INT32_MIN (수정 즉시값: 0x80 회전 → mov.w 한 개)
        cbz     r1, 2f
1:      ldr     r3, [r0], #4            // r3 = *a++
        cmp     r3, r2
        it      gt
        movgt   r2, r3                  // 더 크면 교체
        subs    r1, r1, #1
        bne     1b
2:      mov     r0, r2
        bx      lr
        .size   max_array_s32, . - max_array_s32

// 연습 3: int32_t sat_add_s32(int32_t a, int32_t b)   오버플로면 INT32_MAX / INT32_MIN 으로 포화
//   (Cortex-M4 의 QADD 한 개가 하는 일을 M3 에서 — ARM-A07 참고)
        .global sat_add_s32
        .type   sat_add_s32, %function
        .thumb_func
sat_add_s32:
        adds    r2, r0, r1              // r2 = a + b (래핑), V = 부호 있는 오버플로
        itt     vs                      // 오버플로일 때만 두 명령
        mvnvs   r2, #0x80000000         //   r2 = 0x7FFFFFFF
        addvs   r2, r2, r0, lsr #31     //   a < 0 이면 +1 → 0x80000000 (음의 포화)
        mov     r0, r2
        bx      lr
        .size   sat_add_s32, . - sat_add_s32

// 연습 4: uint32_t count_in_range_s32(const int32_t *a, uint32_t n, int32_t lo, int32_t hi)
//   lo <= a[i] <= hi 인 개수. 조건부 비교(conditional compare) 체인 — 비교 순서가 중요하다.
        .global count_in_range_s32
        .type   count_in_range_s32, %function
        .thumb_func
count_in_range_s32:
        push    {r4, lr}                // r4 (callee-saved) 를 쓰므로 저장. 2 워드 = 8 B 정렬
        add.w   r1, r0, r1, lsl #2      // r1 = end
        mov.w   r12, #0                 // r12 = count (caller-saved 라 저장 불필요)
1:      cmp     r0, r1
        beq     2f
        ldr     r4, [r0], #4            // x = *p++
        cmp     r4, r3                  // x - hi
        it      le
        cmple   r2, r4                  //   x <= hi 일 때만: lo - x
        it      le                      // 이제 LE ⇔ (x <= hi && lo <= x)
        addle   r12, r12, #1
        b       1b
2:      mov     r0, r12
        pop     {r4, pc}
        .size   count_in_range_s32, . - count_in_range_s32
