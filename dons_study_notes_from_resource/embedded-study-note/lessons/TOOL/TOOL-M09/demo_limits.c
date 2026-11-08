/* TOOL-M09 demo_limits.c — TSan 이 "못 보는" 레이스 두 가지
 *
 *  (1) 실행되지 않은 경로: 에러 복구 경로에만 있는 레이스는 그 경로가 돌 때만 잡힌다.
 *  (2) 계측되지 않은 코드: no_sanitize("thread") 함수(= -fsanitize=thread 없이 빌드한 라이브러리,
 *      어셈블리, 벤더 바이너리)의 메모리 접근은 TSan 에게 보이지 않는다.
 *
 *   cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_limits.c -o dlim_t
 *   ./dlim_t            # 정상 경로만: 보고 없음, exit 0
 *   ./dlim_t fault      # 에러 경로까지: 보고 1개
 *   cc -std=c11 -g -O1 -fsanitize=thread -pthread -DNOINST demo_limits.c -o dlim_n
 *   ./dlim_n            # 계측 안 된 카운터: 증가를 잃는데도 보고 없음
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define ITERS 200000

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned long g_ok_frames;      /* 락으로 보호 */
static unsigned long g_crc_errors;     /* 에러 경로에서만 건드림 — 락을 깜빡함 */
static bool g_inject_fault;

#ifdef NOINST
static unsigned long g_dma_done;       /* NOINST 실험용 */
/* "계측 안 된 모듈" 흉내: 이 함수 안의 접근은 TSan 이 기록하지 않는다 */
__attribute__((no_sanitize("thread"), noinline))
static void dma_isr_bookkeeping(void) {
    g_dma_done++;                      /* 레이스 — 하지만 TSan 은 모른다 */
}
#endif

static void *rx_task(void *arg) {
    (void)arg;
    for (int i = 0; i < ITERS; i++) {
        bool crc_bad = g_inject_fault && (i % 1000 == 999);
        if (crc_bad) {
            g_crc_errors++;            /* 드문 경로의 레이스 */
        } else {
            pthread_mutex_lock(&g_lock);
            g_ok_frames++;
            pthread_mutex_unlock(&g_lock);
        }
#ifdef NOINST
        dma_isr_bookkeeping();
#endif
    }
    return NULL;
}

int main(int argc, char **argv) {
    g_inject_fault = (argc > 1 && strcmp(argv[1], "fault") == 0);
    pthread_t a, b;
    pthread_create(&a, NULL, rx_task, NULL);
    pthread_create(&b, NULL, rx_task, NULL);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    printf("ok=%lu crc_err=%lu", g_ok_frames, g_crc_errors);
#ifdef NOINST
    printf(" dma_done=%lu (expect %d)", g_dma_done, 2 * ITERS);
#endif
    printf("\n");
    return 0;
}
