/* C-M08 데모: 컴파일러가 "내 복사/채우기 루프" 를 memcpy/memset 호출로 바꾼다 (loop idiom recognition)
 * libc 를 직접 쓰는 사람(newlib, 부트로더, 펌웨어 런타임)에게는 이게 무한 재귀가 된다.
 * <stddef.h>/<stdint.h> 만 쓰므로 Cortex-M 타깃으로도 그대로 컴파일된다. main 없음 — -S 로 asm 만 본다.
 *
 *   cc -std=c11 -O2 -S demo_idiom.c -o -                          | grep -E '^_|b(l)?\s+_'
 *   cc -std=c11 -O2 -fno-builtin -S demo_idiom.c -o -             | grep -E '^_|b(l)?\s+_'
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_idiom.c -o -               | grep -E '^[a-z_]+:|bl?\s'
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -ffreestanding -S demo_idiom.c -o - | grep -E '^[a-z_]+:|bl?\s'
 */
#include <stddef.h>
#include <stdint.h>

typedef uint32_t __attribute__((may_alias)) word_t;

/* ① libc 안의 진짜 memset — 이름이 바로 memset (호스트에서는 -S 로만 본다: 링크하면 libc 와 충돌) */
void *memset(void *dst, int c, size_t n) {
    unsigned char *d = dst;
    while (n--) *d++ = (unsigned char)c;
    return dst;
}

/* ② libc 안의 memmove 가 쓰는 내부 헬퍼 — 이름이 memcpy 가 아니다 */
void copy_fwd_bytes(unsigned char *restrict d, const unsigned char *restrict s, size_t n) {
    while (n--) *d++ = *s++;
}
void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = dst;
    const unsigned char *s = src;
    if ((uintptr_t)d - (uintptr_t)s >= n) {
        copy_fwd_bytes(d, s, n);                 /* ← 이 헬퍼가 memcpy 호출로 바뀌면? */
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

/* ③ 링커 심볼로 받은 .bss 0 채우기 (워드 루프) — 스타트업 코드 흉내 */
void zero_bss(word_t *p, word_t *end) {
    while (p < end) *p++ = 0;
}
