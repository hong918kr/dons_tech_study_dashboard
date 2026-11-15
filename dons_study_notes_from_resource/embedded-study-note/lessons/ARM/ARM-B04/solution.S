/* ARM-B04 로드/스토어와 주소 지정 모드 — AArch64 (Apple Mach-O + Linux ELF 겸용)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 harness.c <starter|solution>.S shared_bugs.S
 * 규칙: 각 함수는 leaf (스택 안 씀), 인자 x0..x3 / 반환 x0, 임시로 x0..x6 만 사용 (전부 caller-saved).
 */
#ifdef __APPLE__
#define SYM(n) _##n
#define TYPE(n)
#define SIZE(n)
#else
#define SYM(n) n
#define TYPE(n) .type n, %function
#define SIZE(n) .size n, .-n
#endif

/* rec_t 레이아웃 — harness.c 의 _Static_assert(offsetof ...) 와 같은 값 */
#define REC_ID     0
#define REC_TEMP   4
#define REC_TS     8
#define REC_SIZE  16

    .text

/* ===================== Part A: 모드별 배열 합 ===================== */

// uint64_t sum_imm(const uint64_t *a, size_t n)    모드: [xn, #imm] (base 안 바뀜)
    .globl  SYM(sum_imm)
    TYPE(sum_imm)
    .p2align 2
SYM(sum_imm):
    mov     x2, #0                  // sum = 0
1:  cmp     x1, #2
    b.lo    2f                      // 남은 개수 < 2 -> 꼬리 처리
    ldr     x3, [x0]                // a[0]  주소 = x0 + 0
    ldr     x4, [x0, #8]            // a[1]  주소 = x0 + 8 (x0 은 그대로)
    add     x2, x2, x3
    add     x2, x2, x4
    add     x0, x0, #16             // base 는 별도 add 로 전진
    sub     x1, x1, #2
    b       1b
2:  cbz     x1, 3f                  // 꼬리 0개면 끝
    ldr     x3, [x0]                // 마지막 1개
    add     x2, x2, x3
3:  mov     x0, x2                  // 반환
    ret
    SIZE(sum_imm)

// uint64_t sum_post(const uint64_t *a, size_t n)   모드: [xn], #imm (post-index)
    .globl  SYM(sum_post)
    TYPE(sum_post)
    .p2align 2
SYM(sum_post):
    mov     x2, #0                  // sum = 0
    cbz     x1, 2f                  // n == 0 -> 메모리 안 건드림
1:  ldr     x3, [x0], #8            // x3 = *x0, 그 다음 x0 += 8
    add     x2, x2, x3
    subs    x1, x1, #1              // n--, 0 이면 Z=1
    b.ne    1b
2:  mov     x0, x2
    ret
    SIZE(sum_post)

// uint64_t sum_pre(const uint64_t *a, size_t n)    모드: [xn, #imm]! (pre-index)
    .globl  SYM(sum_pre)
    TYPE(sum_pre)
    .p2align 2
SYM(sum_pre):
    mov     x2, #0                  // sum = 0
    cbz     x1, 2f
    sub     x0, x0, #8              // a - 1 (C 에선 UB 포인터, ISA 에선 그냥 정수)
1:  ldr     x3, [x0, #8]!           // 먼저 x0 += 8, 그 주소에서 로드
    add     x2, x2, x3
    subs    x1, x1, #1
    b.ne    1b
2:  mov     x0, x2
    ret
    SIZE(sum_pre)

// uint64_t sum_reg(const uint64_t *a, size_t n)    모드: [xn, xm, lsl #3] (레지스터 오프셋)
    .globl  SYM(sum_reg)
    TYPE(sum_reg)
    .p2align 2
SYM(sum_reg):
    mov     x2, #0                  // sum = 0
    mov     x3, #0                  // i = 0
1:  cmp     x3, x1
    b.hs    2f                      // i >= n (unsigned) -> 끝
    ldr     x4, [x0, x3, lsl #3]    // a[i]: 주소 = x0 + (i << 3), x0 x3 그대로
    add     x2, x2, x4
    add     x3, x3, #1
    b       1b
2:  mov     x0, x2
    ret
    SIZE(sum_reg)

// int64_t sum_gather_s32(const int32_t *base, const int32_t *idx, size_t n)
//   sum of base[idx[i]], idx 는 음수 가능   모드: [xn, wm, sxtw #2]
    .globl  SYM(sum_gather_s32)
    TYPE(sum_gather_s32)
    .p2align 2
SYM(sum_gather_s32):
    mov     x3, #0                  // sum = 0
    mov     x4, #0                  // i = 0
1:  cmp     x4, x2
    b.hs    2f
    ldr     w5, [x1, x4, lsl #2]    // k = idx[i] (w5 에 32비트, x5 상위는 0)
    ldrsw   x6, [x0, w5, sxtw #2]   // base[k]: 주소 = x0 + (sext(w5) << 2), 값도 부호 확장
    add     x3, x3, x6
    add     x4, x4, #1
    b       1b
2:  mov     x0, x3
    ret
    SIZE(sum_gather_s32)

// void copy_ldr_str(uint64_t *dst, const uint64_t *src, size_t n)   1개씩
    .globl  SYM(copy_ldr_str)
    TYPE(copy_ldr_str)
    .p2align 2
SYM(copy_ldr_str):
    cbz     x2, 2f
1:  ldr     x3, [x1], #8            // x3 = *src++
    str     x3, [x0], #8            // *dst++ = x3
    subs    x2, x2, #1
    b.ne    1b
2:  ret
    SIZE(copy_ldr_str)

// void copy_ldp_stp(uint64_t *dst, const uint64_t *src, size_t n)   2개씩 + 홀수 꼬리
    .globl  SYM(copy_ldp_stp)
    TYPE(copy_ldp_stp)
    .p2align 2
SYM(copy_ldp_stp):
    lsr     x3, x2, #1              // pairs = n / 2
    cbz     x3, 2f
1:  ldp     x4, x5, [x1], #16       // x4 = src[0], x5 = src[1], src += 2
    stp     x4, x5, [x0], #16       // dst[0] = x4, dst[1] = x5, dst += 2
    subs    x3, x3, #1
    b.ne    1b
2:  tbz     x2, #0, 3f              // n 의 bit0 == 0 (짝수) -> 끝
    ldr     x4, [x1]                // 홀수 꼬리 1개
    str     x4, [x0]
3:  ret
    SIZE(copy_ldp_stp)

/* ===================== Part B: 폭과 부호 확장 ===================== */

// uint64_t sum_u8(const uint8_t *a, size_t n)      LDRB (zero-extend)
    .globl  SYM(sum_u8)
    TYPE(sum_u8)
    .p2align 2
SYM(sum_u8):
    mov     x2, #0
    cbz     x1, 2f
1:  ldrb    w3, [x0], #1            // w3 = 0..255, x3 상위 비트도 0
    add     x2, x2, x3
    subs    x1, x1, #1
    b.ne    1b
2:  mov     x0, x2
    ret
    SIZE(sum_u8)

// int64_t sum_s16(const int16_t *a, size_t n)      LDRSH (sign-extend to 64)
    .globl  SYM(sum_s16)
    TYPE(sum_s16)
    .p2align 2
SYM(sum_s16):
    mov     x2, #0
    cbz     x1, 2f
1:  ldrsh   x3, [x0], #2            // x3 = (int64_t)(int16_t)*a, a += 2 bytes
    add     x2, x2, x3
    subs    x1, x1, #1
    b.ne    1b
2:  mov     x0, x2
    ret
    SIZE(sum_s16)

// int64_t sum_s32(const int32_t *a, size_t n)      LDRSW (sign-extend to 64)
    .globl  SYM(sum_s32)
    TYPE(sum_s32)
    .p2align 2
SYM(sum_s32):
    mov     x2, #0
    cbz     x1, 2f
1:  ldrsw   x3, [x0], #4            // x3 = (int64_t)*a, a += 4 bytes
    add     x2, x2, x3
    subs    x1, x1, #1
    b.ne    1b
2:  mov     x0, x2
    ret
    SIZE(sum_s32)

/* ===================== Part C: 센서 로그 구조체 배열 ===================== */

// int64_t rec_sum_temp(const rec_t *r, size_t n)   필드 오프셋 + stride
    .globl  SYM(rec_sum_temp)
    TYPE(rec_sum_temp)
    .p2align 2
SYM(rec_sum_temp):
    mov     x2, #0
    cbz     x1, 2f
1:  ldrsw   x3, [x0, #REC_TEMP]     // r->temp (int32 -> int64)
    add     x2, x2, x3
    add     x0, x0, #REC_SIZE       // r++ (stride = sizeof(rec_t))
    subs    x1, x1, #1
    b.ne    1b
2:  mov     x0, x2
    ret
    SIZE(rec_sum_temp)

// uint64_t rec_sum_ts_for_id(const rec_t *r, size_t n, uint32_t id)
//   레코드 하나 = LDP 한 번 (16바이트 통째로)
    .globl  SYM(rec_sum_ts_for_id)
    TYPE(rec_sum_ts_for_id)
    .p2align 2
SYM(rec_sum_ts_for_id):
    mov     x3, #0
    cbz     x1, 2f
1:  ldp     x4, x5, [x0], #REC_SIZE // x4 = id | temp << 32, x5 = ts, r++
    cmp     w4, w2                  // 하위 32비트(= id)만 비교
    b.ne    3f
    add     x3, x3, x5              // 같으면 ts 누적
3:  subs    x1, x1, #1
    b.ne    1b
2:  mov     x0, x3
    ret
    SIZE(rec_sum_ts_for_id)

// rec_t *rec_log_push(rec_t *slot, uint32_t id, int32_t temp, uint64_t ts)
//   레코드를 쓰고 다음 슬롯 주소를 반환 (writeback 이 곧 반환값)
    .globl  SYM(rec_log_push)
    TYPE(rec_log_push)
    .p2align 2
SYM(rec_log_push):
    stp     w1, w2, [x0], #8        // slot->id = w1, slot->temp = w2, x0 += 8
    str     x3, [x0], #8            // slot->ts = x3, x0 += 8 -> 다음 슬롯
    ret
    SIZE(rec_log_push)
