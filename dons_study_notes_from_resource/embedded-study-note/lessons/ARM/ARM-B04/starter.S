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
    mov     x3, #0                  // TODO: a[0] 로드, [x0] 오프셋 모드
    mov     x4, #0                  // TODO: a[1] 로드, [x0, #imm] 오프셋 모드
    add     x2, x2, x3
    add     x2, x2, x4
    add     x0, x0, #16             // base 는 별도 add 로 전진
    sub     x1, x1, #2
    b       1b
2:  cbz     x1, 3f                  // 꼬리 0개면 끝
    mov     x3, #0                  // TODO: 마지막 1개 로드
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
1:  mov     x3, #0                  // TODO: 로드 + x0 += 8 을 명령 하나로 (post-index)
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
1:  mov     x3, #0                  // TODO: x0 += 8 먼저, 그 주소에서 로드 (pre-index)
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
    mov     x4, #0                  // TODO: a[i] 로드, 레지스터 오프셋 + lsl
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
    mov     w5, #0                  // TODO: k = idx[i] (32비트 로드)
    mov     x6, #0                  // TODO: base[k] — k 는 음수 가능, 값은 int32 -> int64
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
1:  mov     x3, #0                  // TODO: x3 = *src++
    nop                             // TODO: *dst++ = x3
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
1:  mov     x4, #0                  // TODO: src 에서 2개를 한 번에 + src += 16
    nop                             // TODO: dst 에 2개를 한 번에 + dst += 16
    subs    x3, x3, #1
    b.ne    1b
2:  tbz     x2, #0, 3f              // n 의 bit0 == 0 (짝수) -> 끝
    mov     x4, #0                  // TODO: 홀수 꼬리 1개 로드
    nop                             // TODO: 홀수 꼬리 1개 저장
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
1:  mov     w3, #0                  // TODO: 바이트 로드 (zero-extend) + a += 1
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
1:  mov     x3, #0                  // TODO: int16 로드 (sign-extend to 64) + a += 2
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
1:  mov     x3, #0                  // TODO: int32 로드 (sign-extend to 64) + a += 4
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
1:  mov     x3, #0                  // TODO: r->temp 로드 (오프셋 REC_TEMP, 부호 확장)
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
1:  mov     x4, #0                  // TODO: 레코드 16바이트를 한 번에 + r++
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
    nop                             // TODO: id, temp 를 한 번에 저장 + x0 += 8
    nop                             // TODO: ts 저장 + x0 += 8
    ret
    SIZE(rec_log_push)
