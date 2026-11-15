/* ARM-B04 추가 연습 답 — AArch64 (Apple Mach-O + Linux ELF 겸용)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_bugs.S -o /tmp/ARM-B04_p
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

    .text

// 연습 1: uint64_t sum_ldp(const uint64_t *a, size_t n)   LDP post-index 로 2개씩 + 꼬리
    .globl  SYM(sum_ldp)
    TYPE(sum_ldp)
    .p2align 2
SYM(sum_ldp):
    mov     x2, #0
    lsr     x5, x1, #1              // pairs = n / 2
    cbz     x5, 2f
1:  ldp     x3, x4, [x0], #16       // a[0], a[1], a += 2
    add     x2, x2, x3
    add     x2, x2, x4
    subs    x5, x5, #1
    b.ne    1b
2:  tbz     x1, #0, 3f              // 짝수면 끝
    ldr     x3, [x0]                // 꼬리 1개
    add     x2, x2, x3
3:  mov     x0, x2
    ret
    SIZE(sum_ldp)

// 연습 2: void reverse_copy(uint64_t *dst, const uint64_t *src, size_t n)
//   dst[i] = src[n-1-i]: 읽기는 앞에서 post-index, 쓰기는 뒤에서 pre-index (음수)
    .globl  SYM(reverse_copy)
    TYPE(reverse_copy)
    .p2align 2
SYM(reverse_copy):
    cbz     x2, 2f
    add     x0, x0, x2, lsl #3      // x0 = dst + n (끝 다음)
1:  ldr     x3, [x1], #8            // x3 = *src++
    str     x3, [x0, #-8]!          // *--dst = x3  (스택 push 와 같은 모양)
    subs    x2, x2, #1
    b.ne    1b
2:  ret
    SIZE(reverse_copy)

// 연습 3: uint64_t lut_sum_u16(const uint16_t *tab, const uint8_t *keys, size_t n)
//   sum of tab[keys[i]] — 보정 테이블 조회, 모드 [xn, wm, uxtw #1]
    .globl  SYM(lut_sum_u16)
    TYPE(lut_sum_u16)
    .p2align 2
SYM(lut_sum_u16):
    mov     x3, #0
    cbz     x2, 2f
1:  ldrb    w4, [x1], #1            // k = *keys++ (0..255)
    ldrh    w5, [x0, w4, uxtw #1]   // tab[k]: 주소 = x0 + (zext(w4) << 1)
    add     x3, x3, x5
    subs    x2, x2, #1
    b.ne    1b
2:  mov     x0, x3
    ret
    SIZE(lut_sum_u16)

// 연습 4a: uint64_t *push2(uint64_t *sp, uint64_t a, uint64_t b)
//   프롤로그 stp x29, x30, [sp, #-16]! 와 같은 모양. 반환 = 새 sp
    .globl  SYM(push2)
    TYPE(push2)
    .p2align 2
SYM(push2):
    stp     x1, x2, [x0, #-16]!     // x0 -= 16; [x0] = a, [x0+8] = b
    ret                             // writeback 된 x0 이 곧 반환값
    SIZE(push2)

// 연습 4b: uint64_t *pop2(uint64_t *sp, uint64_t *a, uint64_t *b)
//   에필로그 ldp x29, x30, [sp], #16 과 같은 모양
    .globl  SYM(pop2)
    TYPE(pop2)
    .p2align 2
SYM(pop2):
    ldp     x3, x4, [x0], #16       // x3 = [x0], x4 = [x0+8]; x0 += 16
    str     x3, [x1]                // *a = x3
    str     x4, [x2]                // *b = x4
    ret
    SIZE(pop2)

// 연습 5: int64_t rec_sum_temp_asr(const rec_t *r, size_t n)
//   레코드 앞 8바이트를 x 로 한 번에 읽고 상위 32비트(temp)를 asr 로 꺼낸다 (little-endian)
    .globl  SYM(rec_sum_temp_asr)
    TYPE(rec_sum_temp_asr)
    .p2align 2
SYM(rec_sum_temp_asr):
    mov     x2, #0
    cbz     x1, 2f
1:  ldr     x3, [x0], #16           // x3 = id | temp << 32, r++ (ts 는 안 읽음)
    asr     x3, x3, #32             // 산술 시프트 = temp 부호 확장
    add     x2, x2, x3
    subs    x1, x1, #1
    b.ne    1b
2:  mov     x0, x2
    ret
    SIZE(rec_sum_temp_asr)
