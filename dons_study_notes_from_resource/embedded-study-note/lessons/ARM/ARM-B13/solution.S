/* ENV: qemu */
// ARM-B13 Thumb-2 어셈블리 — Cortex-M3 (ARMv7-M), AAPCS: r0–r3 인자, r0 반환, r4–r11 callee-saved.
// build (VM): arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb ... harness.c solution.S shared_*.S/.c -T arm_b13.ld
// Mac 확인용: clang --target=thumbv7m-none-eabi -c solution.S
        .syntax unified                 // UAL 문법: 조건 명령은 IT 블록 안에서만, adds/add 를 구분
        .thumb                          // 이후 명령은 Thumb 인코딩 (Cortex-M 은 Thumb 만 실행)
        .text

// ---------------- Part A ----------------
// int32_t max_s32(int32_t a, int32_t b)          a = r0, b = r1
        .global max_s32
        .type   max_s32, %function     // ELF 심볼 타입 = 함수 → 주소에 Thumb 비트(bit0=1)
        .thumb_func                     // (같은 뜻을 gas 에 한 번 더: 다음 라벨은 Thumb 함수)
        .p2align 1
max_s32:
        cmp     r0, r1                  // 플래그 ← a - b
        it      lt                      // 다음 1개 명령: LT (N != V, 부호 있는 <) 일 때만 실행
        movlt   r0, r1                  // a < b 면 r0 = b
        bx      lr                      // 리턴: pc ← lr (bit0=1 → Thumb 유지)
        .size   max_s32, . - max_s32

// int32_t abs_s32(int32_t x)   abs(INT32_MIN) = INT32_MIN (ISA 의 32비트 래핑 그대로)
        .global abs_s32
        .type   abs_s32, %function
        .thumb_func
        .p2align 1
abs_s32:
        cmp     r0, #0                  // 플래그 ← x - 0
        it      lt                      // x < 0 (부호 있는) 일 때만
        rsblt   r0, r0, #0              // r0 = 0 - x  (IT 안이라 16비트, 플래그 안 바꿈)
        bx      lr
        .size   abs_s32, . - abs_s32

// int32_t sum_array_s32(const int32_t *a, uint32_t n)   합은 mod 2^32 로 래핑
        .global sum_array_s32
        .type   sum_array_s32, %function
        .thumb_func
        .p2align 1
sum_array_s32:
        movs    r2, #0                  // r2 = sum = 0
        cbz     r1, 2f                  // n == 0 → 메모리를 읽지 않고 끝 (cbz: 앞으로만, low reg 만)
1:      ldr     r3, [r0], #4            // r3 = *a; a += 4   (post-index, 32비트 ldr.w)
        adds    r2, r2, r3              // sum += r3 (16비트 adds)
        subs    r1, r1, #1              // n--, n == 0 이면 Z = 1
        bne     1b                      // 남았으면 반복
2:      mov     r0, r2                  // 반환값 = sum
        bx      lr
        .size   sum_array_s32, . - sum_array_s32

// ---------------- Part B ----------------
// uint32_t max_u32(uint32_t a, uint32_t b)      부호 없는 비교 → LO / HI 계열 조건
        .global max_u32
        .type   max_u32, %function
        .thumb_func
        .p2align 1
max_u32:
        cmp     r0, r1                  // 플래그 ← a - b  (C = 1 이면 a >= b, 부호 없음)
        it      lo                      // LO = CC (C == 0): a < b (부호 없음)
        movlo   r0, r1
        bx      lr
        .size   max_u32, . - max_u32

// ---------------- Part C ----------------
// int32_t clamp_s32(int32_t x, int32_t lo, int32_t hi)   전제: lo <= hi.  x = r0, lo = r1, hi = r2
        .global clamp_s32
        .type   clamp_s32, %function
        .thumb_func
        .p2align 1
clamp_s32:
        cmp     r0, r1                  // 플래그 ← x - lo
        ite     lt                      // T: LT,  E: GE  (Then/Else 두 칸)
        movlt   r0, r1                  //   x < lo  → r0 = lo   (끝: lo <= hi 라 다음 검사는 GT 거짓)
        cmpge   r0, r2                  //   x >= lo → 플래그 ← x - hi  (IT 안의 cmp 도 플래그를 쓴다)
        it      gt
        movgt   r0, r2                  // x > hi → r0 = hi
        bx      lr
        .size   clamp_s32, . - clamp_s32

// int32_t sum_clamped_s32(const int32_t *a, uint32_t n, int32_t lo, int32_t hi)
//   sum of clamp_s32(a[i], lo, hi) — 다른 함수를 부르는 non-leaf: lr 과 쓰는 callee-saved 를 저장
        .global sum_clamped_s32
        .type   sum_clamped_s32, %function
        .thumb_func
        .p2align 1
sum_clamped_s32:
        push    {r4, r5, r6, r7, r8, lr} // 6 워드 = 24 B: sp 8 바이트 정렬 유지 (AAPCS)
        mov     r4, r0                  // r4 = p    (호출을 건너 살아야 하는 값 → callee-saved)
        add.w   r5, r0, r1, lsl #2      // r5 = end = a + n*4
        mov     r6, r2                  // r6 = lo
        mov     r7, r3                  // r7 = hi
        mov.w   r8, #0                  // r8 = sum (high reg 라 32비트 mov.w)
1:      cmp     r4, r5
        beq     2f                      // p == end → 끝 (n == 0 도 여기서 걸린다)
        ldr     r0, [r4], #4            // r0 = *p++   (1번째 인자)
        mov     r1, r6                  // 2번째 인자 lo
        mov     r2, r7                  // 3번째 인자 hi
        bl      clamp_s32               // lr ← 복귀 주소|1, r0–r3/r12 는 망가져도 된다
        add     r8, r8, r0              // sum += 결과 (16비트 high-reg add, 플래그 안 바꿈)
        b       1b
2:      mov     r0, r8
        pop     {r4, r5, r6, r7, r8, pc} // 복원 + 저장된 lr 을 pc 로 → 리턴
        .size   sum_clamped_s32, . - sum_clamped_s32
