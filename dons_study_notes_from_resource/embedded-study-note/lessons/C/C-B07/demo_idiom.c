/* C-B07 데모: 컴파일러가 "내 strlen 루프" 를 libc strlen 호출로 바꿔 버린다 (idiom recognition)
 *   cc -std=c11 -O2 -S demo_idiom.c -o - | grep -A4 '^_my_'
 *   cc -std=c11 -O2 -fno-builtin -S demo_idiom.c -o - | grep -A4 '^_my_'
 */
#include <stddef.h>

size_t my_strlen(const char *s) {
    const char *p = s;
    while (*p != '\0') p++;
    return (size_t)(p - s);
}

void *my_memset(void *dst, int c, size_t n) {
    unsigned char *d = dst;
    for (size_t i = 0; i < n; i++) d[i] = (unsigned char)c;
    return dst;
}
