/* ARM-B09 추가 연습 asm — practice.c 가 테스트한다
 *
 *   size_t span_printable(const char *s);      // 연습 1: 앞에서부터 "출력 가능(>= 0x20, 부호 없이)" 바이트 수
 *   size_t span_printable_bug(const char *s);  // 연습 1: 두 번째 심은 버그 (ldrsb + 부호 있는 비교)
 *   size_t my_strlen_post(const char *s);      // 연습 2: post-index 로 고친 strlen
 *   size_t my_strnlen(const char *s, size_t n);// 연습 3: 최대 n 바이트만 읽는다 (NUL 없어도 안전)
 */
#ifdef __APPLE__
#  define SYM(n)  _##n
#  define TYPE(n)
#  define SIZE(n)
#else
#  define SYM(n)  n
#  define TYPE(n) .type n, %function
#  define SIZE(n) .size n, . - n
#endif

    .text
// ---------------- 연습 1: 정답 — ldrb(0 확장) + 부호 없는 비교 b.hs ----------------
    .globl  SYM(span_printable)
    TYPE(span_printable)
    .p2align 2
SYM(span_printable):
    mov     x1, x0                     // p = s
1:  ldrb    w2, [x1]                   // w2 = *p, 0..255 (zero-extend)
    cmp     w2, #0x20                  // w2 - 0x20 → C = (w2 >= 0x20, 부호 없이)
    b.lo    2f                         // < 0x20 (NUL·\n·\t 포함) 이면 끝
    add     x1, x1, #1                 // p++
    b       1b
2:  sub     x0, x1, x0                 // 길이 = p - s
    ret
    SIZE(span_printable)

// ---------------- 연습 1: 버그 판 — ldrsb(부호 확장) + 부호 있는 비교 b.lt ----------------
    .globl  SYM(span_printable_bug)
    TYPE(span_printable_bug)
    .p2align 2
SYM(span_printable_bug):
    mov     x1, x0
1:  ldrsb   w2, [x1]                   // BUG: 0xC3 → w2 = 0xFFFFFFC3 (= -61)
    cmp     w2, #0x20
    b.lt    2f                         // BUG: -61 < 32 (부호 있게) → UTF-8 바이트에서 멈춤
    add     x1, x1, #1
    b       1b
2:  sub     x0, x1, x0
    ret
    SIZE(span_printable_bug)

// ---------------- 연습 2: post-index 판 strlen ----------------
    .globl  SYM(my_strlen_post)
    TYPE(my_strlen_post)
    .p2align 2
SYM(my_strlen_post):
    mov     x1, x0                     // p = s
1:  ldrb    w2, [x1], #1               // w2 = *p, 그 다음 p += 1 (post-index)
    cbnz    w2, 1b                     // 0 아니면 계속 (cbnz 는 플래그를 안 바꾼다)
    sub     x0, x1, x0                 // p 는 NUL "다음" 을 가리킨다 → p - s = len + 1
    sub     x0, x0, #1                 // 그래서 1 을 뺀다
    ret
    SIZE(my_strlen_post)

// ---------------- 연습 3: strnlen — 경계 검사를 "읽기 전에" ----------------
    .globl  SYM(my_strnlen)
    TYPE(my_strnlen)
    .p2align 2
SYM(my_strnlen):
    mov     x2, #0                     // i = 0
1:  cmp     x2, x1                     // i - n (부호 없는 비교)
    b.hs    2f                         // i >= n 이면 읽지 말고 끝 (읽기 전에 검사!)
    ldrb    w3, [x0, x2]               // w3 = s[i]  (register offset)
    cbz     w3, 2f                     // NUL 이면 끝
    add     x2, x2, #1                 // i++
    b       1b
2:  mov     x0, x2                     // return i
    ret
    SIZE(my_strnlen)

#ifndef __APPLE__
    .section .note.GNU-stack, "", %progbits
#endif
