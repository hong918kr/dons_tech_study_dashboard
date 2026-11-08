/* C-A05 demo: 표준/POSIX 정렬 할당 함수가 이 맥에서 실제로 어떻게 동작하나
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_std_alloc.c -o demo_std_alloc && ./demo_std_alloc
 * (관찰용 — 체크 테스트 아님)
 */
#define _POSIX_C_SOURCE 200112L   /* posix_memalign 선언 (include 전에) */
#include <errno.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned tz(uintptr_t v) { unsigned n = 0; while (v && !(v & 1u)) { v >>= 1; n++; } return n; }

static void try_aligned_alloc(size_t al, size_t sz) {
    errno = 0;
    void *p = aligned_alloc(al, sz);
    int e = errno;
    printf("aligned_alloc(%5zu, %5zu) -> %s", al, sz, p ? "ok  " : "NULL");
    if (p) printf("  addr%%al=%zu", (size_t)((uintptr_t)p % al));
    else   printf("  errno=%d (%s)", e, e ? strerror(e) : "-");
    printf("\n");
    free(p);
}

static void try_posix_memalign(size_t al, size_t sz) {
    void *p = NULL;
    int r = posix_memalign(&p, al, sz);
    printf("posix_memalign(al=%4zu, %4zu) -> ret=%d%s\n", al, sz, r,
           r == 0 ? "" : (r == EINVAL ? " (EINVAL)" : ""));
    if (r == 0) free(p);
}

int main(void) {
    printf("alignof(max_align_t)=%zu  sizeof(void*)=%zu\n", alignof(max_align_t), sizeof(void *));

    /* malloc 이 실제로 주는 최소 정렬: 여러 크기 1000번 할당 중 가장 작은 trailing zero 수 */
    unsigned minz = 64;
    void *keep[1000];
    for (int i = 0; i < 1000; i++) {
        keep[i] = malloc((size_t)(1 + (i * 37) % 3000));
        unsigned z = tz((uintptr_t)keep[i]);
        if (z < minz) minz = z;
    }
    for (int i = 0; i < 1000; i++) free(keep[i]);
    printf("malloc: 1000 blocks (1..3000 B), min alignment seen = %u bytes\n", 1u << minz);

    try_aligned_alloc(64, 128);     /* size 가 align 의 배수 */
    try_aligned_alloc(64, 100);     /* C11 원문: UB / C17(DR 460): 허용 */
    try_aligned_alloc(4096, 4096);
    try_aligned_alloc(4096, 10);
    try_aligned_alloc(48, 96);      /* 2의 거듭제곱 아님 */
    try_aligned_alloc(4, 16);       /* sizeof(void*) 보다 작음 */
    try_posix_memalign(64, 100);
    try_posix_memalign(4, 16);      /* POSIX: sizeof(void*) 의 배수인 2의 거듭제곱이어야 */
    try_posix_memalign(48, 96);

    _Alignas(4096) static unsigned char page_buf[4096];
    printf("_Alignas(4096) static buffer: addr %% 4096 = %zu\n", (size_t)((uintptr_t)page_buf % 4096));
    return 0;
}
