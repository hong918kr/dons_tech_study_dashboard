/* ARM-M04 demo_tsan.c — ThreadSanitizer 는 inline asm 을 보지 못한다 (두 방향 모두).
 *
 *   (1) 올바른 asm 스핀락 + 일반 카운터  → TSan 은 락을 모르니 "data race" 를 보고 (거짓 양성)
 *   (2) 같은 코드 + -DANNOTATE            → __tsan_acquire/__tsan_release 로 알려 주면 조용
 *   (3) -DBROKEN: STLXR 상태를 무시한 asm 카운터 → 증가를 잃는데도 TSan 은 조용 (거짓 음성)
 *
 * build: cc -std=c11 -g -O1 -fsanitize=thread -pthread [-DANNOTATE | -DBROKEN] demo_tsan.c -o dt && ./dt
 */
#define _DARWIN_C_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sanitizer/tsan_interface.h>

#define NT 4
#define ITERS 100000u

uint32_t g_lock, g_asm_cnt;          /* (static 아님: -D 에 따라 안 쓰이는 쪽이 경고 안 나게) */
uint64_t g_plain;            /* 락 안에서만 만지는 일반 변수 */

void lock(uint32_t *l) {
    uint32_t old, st;
    __asm__ volatile(
        "   sevl\n"
        "1: wfe\n"
        "2: ldaxr  %w[old], %[mem]\n"
        "   cbnz   %w[old], 1b\n"
        "   stxr   %w[st], %w[one], %[mem]\n"
        "   cbnz   %w[st], 2b\n"
        : [old] "=&r"(old), [st] "=&r"(st), [mem] "+Q"(*l) : [one] "r"(1u) : "memory");
#ifdef ANNOTATE
    __tsan_acquire(l);
#endif
}
void unlock(uint32_t *l) {
#ifdef ANNOTATE
    __tsan_release(l);
#endif
    __asm__ volatile("stlr wzr, %[mem]" : [mem] "=Q"(*l) : : "memory");
}
void add_ignore_status(uint32_t *p) {     /* 틀린 RMW: 실패한 STLXR 를 무시 */
    uint32_t old, sum, st;
    __asm__ volatile(
        "   ldaxr  %w[old], %[mem]\n"
        "   add    %w[sum], %w[old], #1\n"
        "   stlxr  %w[st], %w[sum], %[mem]\n"
        : [old] "=&r"(old), [sum] "=&r"(sum), [st] "=&r"(st), [mem] "+Q"(*p) : : "memory");
}

static void *worker(void *arg) {
    (void)arg;
    for (uint32_t i = 0; i < ITERS; i++) {
#ifdef BROKEN
        add_ignore_status(&g_asm_cnt);
#else
        lock(&g_lock);
        g_plain++;
        unlock(&g_lock);
#endif
    }
    return NULL;
}

int main(void) {
    pthread_t th[NT];
    for (int i = 0; i < NT; i++) pthread_create(&th[i], NULL, worker, NULL);
    for (int i = 0; i < NT; i++) pthread_join(th[i], NULL);
#ifdef BROKEN
    printf("asm counter=%u (expect %u)\n", g_asm_cnt, NT * ITERS);
#else
    printf("plain counter=%llu (expect %u)\n", (unsigned long long)g_plain, NT * ITERS);
#endif
    return 0;
}
