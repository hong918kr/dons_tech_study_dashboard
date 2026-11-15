/* ARM-B04 shared_bugs.S — 일부러 틀린 버전 2개 (테스트가 잡아내는지 확인용, 모든 빌드에 링크됨) */
#ifdef __APPLE__
#define SYM(n) _##n
#define TYPE(n)
#define SIZE(n)
#else
#define SYM(n) n
#define TYPE(n) .type n, %function
#define SIZE(n) .size n, .-n
#endif

    .text

// BUG 1: int64_t sum_s32_bug(const int32_t *a, size_t n)
//   LDRSW 대신 32비트 LDR -> 값이 zero-extend 되어 -1 이 4294967295 로 더해진다
    .globl  SYM(sum_s32_bug)
    TYPE(sum_s32_bug)
    .p2align 2
SYM(sum_s32_bug):
    mov     x2, #0
    cbz     x1, 2f
1:  ldr     w3, [x0], #4            // BUG: w3 쓰기 -> x3 상위 32비트 = 0 (부호 사라짐)
    add     x2, x2, x3
    subs    x1, x1, #1
    b.ne    1b
2:  mov     x0, x2
    ret
    SIZE(sum_s32_bug)

// BUG 2: void copy_ldp_stp_notail(uint64_t *dst, const uint64_t *src, size_t n)
//   2개씩 복사만 하고 홀수 꼬리를 잊었다 -> n 이 홀수면 마지막 원소가 안 복사됨
    .globl  SYM(copy_ldp_stp_notail)
    TYPE(copy_ldp_stp_notail)
    .p2align 2
SYM(copy_ldp_stp_notail):
    lsr     x3, x2, #1              // pairs = n / 2
    cbz     x3, 2f
1:  ldp     x4, x5, [x1], #16
    stp     x4, x5, [x0], #16
    subs    x3, x3, #1
    b.ne    1b
2:  ret                             // BUG: n & 1 꼬리 처리 없음
    SIZE(copy_ldp_stp_notail)
