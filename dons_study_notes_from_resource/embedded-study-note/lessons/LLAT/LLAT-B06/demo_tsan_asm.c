/* LLAT-B06 demo_tsan_asm.c — TSan 은 인라인 asm 안의 메모리 접근을 보지 못한다
 *   MODE=0: 스레드 A 가 asm ldadd 로 g 를 증가, 스레드 B 가 g 를 "그냥" 읽는다 → TSan 조용함 (못 봄)
 *   MODE=1: 스레드 A 가 g++ (C 코드), 스레드 B 가 g 를 그냥 읽는다             → TSan data race 보고
 * build: cc -std=c11 -g -O1 -fsanitize=thread -pthread -DMODE=0 demo_tsan_asm.c -o t0 && ./t0
 *        cc -std=c11 -g -O1 -fsanitize=thread -pthread -DMODE=1 demo_tsan_asm.c -o t1 && ./t1
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#ifndef MODE
#define MODE 0
#endif
static uint64_t g;
static void *writer(void *arg) {
    (void)arg;
    for (int i = 0; i < 100000; i++) {
#if MODE == 0
        uint64_t old;
        __asm__ volatile("ldadd %x2, %x0, [%1]" : "=r"(old) : "r"(&g), "r"((uint64_t)1) : "memory");
        (void)old;
#else
        g++;
#endif
    }
    return NULL;
}
static void *reader(void *arg) {
    uint64_t *out = arg, s = 0;
    for (int i = 0; i < 100000; i++) s += *(volatile uint64_t *)&g;   /* 원자 아닌 읽기 */
    *out = s;
    return NULL;
}
int main(void) {
    pthread_t a, b; uint64_t s = 0;
    pthread_create(&a, NULL, writer, NULL);
    pthread_create(&b, NULL, reader, &s);
    pthread_join(a, NULL); pthread_join(b, NULL);
    printf("MODE=%d g=%llu (reader sum %llu)\n", MODE, (unsigned long long)g, (unsigned long long)s);
    return 0;
}
