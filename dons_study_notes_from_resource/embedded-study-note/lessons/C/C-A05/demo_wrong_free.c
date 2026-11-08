/* C-A05 demo: aligned_malloc 이 준 포인터를 free() 에 그냥 넘기면?
 *   cc -std=c11 -O0 -g demo_wrong_free.c -o dwf && ./dwf                                   # macOS libmalloc
 *   cc -std=c11 -O0 -g -fsanitize=address demo_wrong_free.c -o dwf_asan && ./dwf_asan     # ASan
 * 일부러 잘못된 free 를 하는 프로그램이다 — 크래시(abort)가 "정상" 결과.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *aligned_malloc_min(size_t size, size_t align) {      /* 원래 포인터만 저장하는 최소판 */
    void *raw = malloc(size + align - 1 + sizeof(void *));
    if (!raw) return NULL;
    uintptr_t a = ((uintptr_t)raw + sizeof(void *) + align - 1) & ~(uintptr_t)(align - 1);
    unsigned char *p = (unsigned char *)raw + (a - (uintptr_t)raw);
    memcpy(p - sizeof(void *), &raw, sizeof raw);
    return p;
}

int main(void) {
    void *p = aligned_malloc_min(100, 64);
    void *raw;
    memcpy(&raw, (unsigned char *)p - sizeof(void *), sizeof raw);
    printf("raw=%p  aligned=%p  (offset %td)\n", raw, p, (unsigned char *)p - (unsigned char *)raw);
    fflush(stdout);
    free(p);                  /* 버그: aligned_free(p) 여야 한다 */
    printf("not reached?\n");
    return 0;
}
