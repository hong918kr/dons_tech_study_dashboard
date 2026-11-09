/* OS-M11 demo_tsan.c — mini-lockdep 없이 "순수 pthread" ABBA 를 순차로 돌려 TSan 의 lock-order 검사와 비교한다.
 * build: cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_tsan.c -o demo_tsan
 * run:   TSAN_OPTIONS=detect_deadlocks=1:second_deadlock_stack=1 ./demo_tsan
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>

static pthread_mutex_t uart_lock = PTHREAD_MUTEX_INITIALIZER, dma_lock = PTHREAD_MUTEX_INITIALIZER;

__attribute__((noinline)) void uart_start_dma_tx(void) {
    pthread_mutex_lock(&uart_lock); pthread_mutex_lock(&dma_lock);
    pthread_mutex_unlock(&dma_lock); pthread_mutex_unlock(&uart_lock);
}
__attribute__((noinline)) void dma_done_callback(void) {
    pthread_mutex_lock(&dma_lock); pthread_mutex_lock(&uart_lock);
    pthread_mutex_unlock(&uart_lock); pthread_mutex_unlock(&dma_lock);
}
static void *t1(void *a) { (void)a; uart_start_dma_tx(); return NULL; }
static void *t2(void *a) { (void)a; dma_done_callback(); return NULL; }

int main(void) {
    pthread_t a, b;
    pthread_create(&a, NULL, t1, NULL); pthread_join(a, NULL);   /* 차례로: 실제 교착은 없다 */
    pthread_create(&b, NULL, t2, NULL); pthread_join(b, NULL);
    puts("both threads finished");
    return 0;
}
