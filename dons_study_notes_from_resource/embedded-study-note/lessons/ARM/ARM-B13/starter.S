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
        // TODO: cmp + IT 블록 + 조건부 mov  (지금은 a 를 그대로 반환)
        bx      lr                      // 리턴: pc ← lr (bit0=1 → Thumb 유지)
        .size   max_s32, . - max_s32

// int32_t abs_s32(int32_t x)   abs(INT32_MIN) = INT32_MIN (ISA 의 32비트 래핑 그대로)
        .global abs_s32
        .type   abs_s32, %function
        .thumb_func
        .p2align 1
abs_s32:
        // TODO: x < 0 이면 r0 = 0 - x  (지금은 x 를 그대로 반환)
        bx      lr
        .size   abs_s32, . - abs_s32

// int32_t sum_array_s32(const int32_t *a, uint32_t n)   합은 mod 2^32 로 래핑
        .global sum_array_s32
        .type   sum_array_s32, %function
        .thumb_func
        .p2align 1
sum_array_s32:
        // TODO: n == 0 처리 + ldr post-index 루프  (지금은 0 반환)
        movs    r0, #0
        bx      lr
        .size   sum_array_s32, . - sum_array_s32

// ---------------- Part B ----------------
// uint32_t max_u32(uint32_t a, uint32_t b)      부호 없는 비교 → LO / HI 계열 조건
        .global max_u32
        .type   max_u32, %function
        .thumb_func
        .p2align 1
max_u32:
        // TODO: 부호 없는 조건으로  (지금은 a 를 그대로 반환)
        bx      lr
        .size   max_u32, . - max_u32

// ---------------- Part C ----------------
// int32_t clamp_s32(int32_t x, int32_t lo, int32_t hi)   전제: lo <= hi.  x = r0, lo = r1, hi = r2
        .global clamp_s32
        .type   clamp_s32, %function
        .thumb_func
        .p2align 1
clamp_s32:
        // TODO: ITE 블록 하나 + IT 블록 하나  (지금은 x 를 그대로 반환)
        bx      lr
        .size   clamp_s32, . - clamp_s32

// int32_t sum_clamped_s32(const int32_t *a, uint32_t n, int32_t lo, int32_t hi)
//   sum of clamp_s32(a[i], lo, hi) — 다른 함수를 부르는 non-leaf: lr 과 쓰는 callee-saved 를 저장
        .global sum_clamped_s32
        .type   sum_clamped_s32, %function
        .thumb_func
        .p2align 1
sum_clamped_s32:
        // TODO: push {...lr}, 루프에서 bl clamp_s32, pop {..., pc}  (지금은 0 반환)
        movs    r0, #0
        bx      lr
        .size   sum_clamped_s32, . - sum_clamped_s32
