/* ALGO-A05 demo — 겹치는 match 복사 (dist < len) 를 세 가지로 해 본다
 *   byte loop : out[op] = out[op − dist] 를 앞에서부터 한 바이트씩 → LZ77 의 정의 (방금 쓴 바이트를 다시 읽는다)
 *   memmove   : 겹쳐도 "복사 전의 원본" 을 옮기도록 정의된 함수 → 정의된 동작이지만 LZ 의미로는 틀림
 *   memcpy    : 겹치면 정의되지 않은 동작 (C11 7.24.2.1) → 결과는 libc 구현 마음대로, ASan 은 즉시 잡는다
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_overlap.c -o demo_overlap && ./demo_overlap
 *        cc -std=c11 -g -O1 -fsanitize=address -fno-omit-frame-pointer demo_overlap.c -o demo_overlap_asan && ./demo_overlap_asan
 */
#include <stdio.h>
#include <string.h>

static void show(const char *name, const unsigned char *b, size_t n) {
    printf("%-10s \"", name);
    for (size_t i = 0; i < n; i++) putchar(b[i] >= 0x20 && b[i] < 0x7F ? b[i] : '.');
    printf("\"\n");
}

int main(void) {
    /* "abc" 다음에 match(dist = 3, len = 33) → 기대값 "abc" × 12 */
    enum { DIST = 3, LEN = 33, N = DIST + LEN };
    unsigned char a[N] = "abc", b[N] = "abc", c[N] = "abc";
    for (size_t k = 0, op = DIST; k < LEN; k++, op++) a[op] = a[op - DIST];
    show("byte loop", a, N);
    memmove(b + DIST, b, LEN);
    show("memmove", b, N);
    memcpy(c + DIST, c, LEN);                     /* UB: 겹침. -O2 에서는 인라인 load/store 로 바뀔 수 있다 */
    show("memcpy", c, N);
    return 0;
}
