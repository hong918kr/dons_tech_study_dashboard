/* ARM-B09 demo_asan_blind.c — ASan 은 손으로 쓴 asm 의 over-read 를 못 본다
 *
 *   cc -g -O1 -fsanitize=address demo_asan_blind.c starter.S -o ab && ./ab      → 조용히 틀린 값
 *   cc -g -O0 -fsanitize=address -DUSE_C demo_asan_blind.c -o abc && ./abc     → C 판은 heap-buffer-overflow 보고
 */
#include <stdio.h>
#include <stdlib.h>

#ifdef USE_C
/* starter.S 와 같은 버그를 C 로: 포인터를 먼저 올리고 읽는다 */
__attribute__((noinline)) static size_t my_strlen(const char *s) {
    const char *p = s;
    do { ++p; } while (*p != '\0');
    return (size_t)(p - s);
}
#else
extern size_t my_strlen(const char *s);
#endif

int main(void) {
    char *s = malloc(1);          /* "" 딱 1 바이트: s[1] 부터는 ASan redzone */
    s[0] = '\0';
    size_t n = my_strlen(s);
    printf("my_strlen(\"\") = %zu\n", n);
    free(s);
    return 0;
}
