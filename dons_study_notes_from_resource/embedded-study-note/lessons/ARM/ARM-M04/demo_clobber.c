/* ARM-M04 demo_clobber.c — "memory" clobber 를 빼면 컴파일러가 임계 구역 쓰기를 락 밖으로 옮길 수 있나?
 * main 없음: 어셈블리만 본다.
 *   cc  -std=c11 -O2 -S demo_clobber.c -o -              (Apple clang 21: 이 코드에선 옮기지 않음)
 *   gcc -std=c11 -O2 -S demo_clobber.c -o -              (Lima VM gcc 15: 두 임계 구역의 +1 을 합쳐 버림)
 *   gcc -std=c11 -O2 -S -DMEMCLOB demo_clobber.c -o -    (clobber 를 넣으면 각 임계 구역 안에서 ldr/add/str)
 */
#include <stdint.h>
#ifdef MEMCLOB
#define CLOB : "memory"
#define CLOB2 : : "memory"
#else
#define CLOB
#define CLOB2
#endif
static uint64_t g_a;                 /* 락으로 보호하려는 일반 변수 */
uint32_t g_lock;
uint64_t get_a(void) { return g_a; }
static inline void lock_(uint32_t *l) {
    uint32_t old, st;
    __asm__ volatile(
        "1: ldaxr  %w[old], %[mem]\n"
        "   cbnz   %w[old], 1b\n"
        "   stxr   %w[st], %w[one], %[mem]\n"
        "   cbnz   %w[st], 1b\n"
        : [old] "=&r"(old), [st] "=&r"(st), [mem] "+Q"(*l) : [one] "r"(1u) CLOB);
}
static inline void unlock_(uint32_t *l) { __asm__ volatile("stlr wzr, %[mem]" : [mem] "=Q"(*l) CLOB2); }
void work2(void) {
    lock_(&g_lock); g_a += 1; unlock_(&g_lock);
    lock_(&g_lock); g_a += 1; unlock_(&g_lock);
}
