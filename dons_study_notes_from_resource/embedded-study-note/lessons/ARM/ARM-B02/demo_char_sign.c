/* ARM-B02 demo: plain `char` is SIGNED on Apple arm64 but UNSIGNED on AArch64 Linux (AAPCS64).
 * Same C source -> ldrsb vs ldrb -> different answer for bytes >= 0x80.
 *   cc -O2 demo_char_sign.c -o /tmp/ARM-B02_cs && /tmp/ARM-B02_cs                    (Mac default)
 *   cc -O2 -funsigned-char demo_char_sign.c -o /tmp/ARM-B02_cu && /tmp/ARM-B02_cu    (Linux-like)
 *   clang -S -O2 [-funsigned-char] -o - demo_char_sign.c | grep ldr
 */
#include <limits.h>
#include <stddef.h>
#include <stdio.h>

__attribute__((noinline)) size_t tok_len(const char *s) {
    const char *p = s;
    while (*p > ' ') p++;          /* same body as mystery4 */
    return (size_t)(p - s);
}

int main(void) {
    printf("CHAR_MIN=%d  tok_len(\"abc\\x80def\")=%zu  tok_len(\"caf\\xC3\\xA9 x\")=%zu\n",
           CHAR_MIN, tok_len("abc\x80" "def"), tok_len("caf\xC3\xA9 x"));
    return 0;
}
