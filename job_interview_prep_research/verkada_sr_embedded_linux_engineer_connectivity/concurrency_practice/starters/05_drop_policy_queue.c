/* 05. Backpressure & drop policy — 링크가 끊겨도 죽지 않는 텔레메트리 큐
 * ------------------------------------------------------------------
 * 게이트웨이가 LTE로 텔레메트리를 올린다. 링크가 느려지거나 끊기면 업로더가
 * 막히고, 그 사이에도 센서 스레드는 계속 샘플을 만든다.
 * "메모리는 유한하다" → 큐가 차면 무엇을 할지 정책으로 정한다:
 *   BLOCK        : 생산자를 막는다 (손실 없음, 실시간 경로에는 금지)
 *   DROP_NEWEST  : 새 샘플을 버린다 (과거 이력 보존 — 로그/감사에 적합)
 *   DROP_OLDEST  : 가장 오래된 것을 버린다 (최신 상태 보존 — 상태/알람에 적합)
 * 인터뷰 포인트: 정책은 '데이터의 의미'가 결정한다. 그리고 버린 개수는 반드시
 * 카운트해서 함께 보고한다(조용한 손실이 제일 나쁘다).
 *
 * 불변식(테스트로 검증): produced == consumed + dropped + queued
 * 빌드: cc -std=c11 -Wall -Wextra -pthread 05_drop_policy_queue.c
 */
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { POLICY_BLOCK, POLICY_DROP_NEWEST, POLICY_DROP_OLDEST } DropPolicy;

typedef struct { uint32_t ts_ms; int32_t value; } Sample;

typedef struct {
    Sample         *buf;
    size_t          cap, head, count;
    DropPolicy      policy;
    bool            closed;
    unsigned long   pushed, dropped, popped;
    pthread_mutex_t m;
    pthread_cond_t  not_full, not_empty;
} TeleQueue;

int  tq_init(TeleQueue *q, size_t cap, DropPolicy p);
void tq_destroy(TeleQueue *q);
bool tq_push(TeleQueue *q, Sample s);                   /* false = closed */
size_t tq_pop_batch(TeleQueue *q, Sample *out, size_t max); /* 0 = closed&empty */
void tq_close(TeleQueue *q);

/* ------------------------------------------------------------------
 * 여기부터 직접 구현한다. 위의 선언부와 아래 self-test는 그대로 두고,
 * 아래 함수 목록을 채운 뒤 `make run N=05` 로 검증한다.
 *
 * 구현할 함수:
 *   - tq_init()
 *   - tq_destroy()
 *   - tq_push()
 *   - tq_pop_batch()
 *   - tq_close()
 *
 * 막히면 ../solutions/05_drop_policy_queue.c 를 열되, 먼저 15분은 스스로 해볼 것.
 * ------------------------------------------------------------------ */

/* ------------------------------- self test ------------------------------- */
#define NPROD   3
#define NPER    5000
#define BATCH   16

static TeleQueue g_q;
static unsigned long g_consumed;

static void *producer(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < NPER; i++) {
        Sample s = { .ts_ms = (uint32_t)i, .value = id };
        if (!tq_push(&g_q, s)) break;
    }
    return NULL;
}

static void *uploader(void *arg)
{
    (void)arg;
    Sample batch[BATCH];
    size_t n;
    while ((n = tq_pop_batch(&g_q, batch, BATCH)) > 0) {
        g_consumed += n;
        for (volatile int spin = 0; spin < 200; spin++) { } /* 느린 링크 흉내 */
    }
    return NULL;
}

static void run_case(DropPolicy p, const char *name)
{
    pthread_t prod[NPROD], up;
    g_consumed = 0;
    assert(tq_init(&g_q, 32, p) == 0);

    pthread_create(&up, NULL, uploader, NULL);
    for (int i = 0; i < NPROD; i++)
        pthread_create(&prod[i], NULL, producer, (void *)(intptr_t)i);
    for (int i = 0; i < NPROD; i++) pthread_join(prod[i], NULL);
    tq_close(&g_q);
    pthread_join(up, NULL);

    unsigned long produced = (unsigned long)NPROD * NPER;
    unsigned long leftover = (unsigned long)g_q.count;
    printf("  %-12s produced=%lu consumed=%lu dropped=%lu left=%lu\n",
           name, produced, g_consumed, g_q.dropped, leftover);
    /* 불변식: 만든 만큼은 소비되었거나 버려졌거나 큐에 남아 있다 */
    assert(g_consumed + g_q.dropped + leftover == produced);
    if (p == POLICY_BLOCK) assert(g_q.dropped == 0);   /* 블로킹은 손실 없음 */
    tq_destroy(&g_q);
}

int main(void)
{
    run_case(POLICY_BLOCK,       "BLOCK");
    run_case(POLICY_DROP_NEWEST, "DROP_NEWEST");
    run_case(POLICY_DROP_OLDEST, "DROP_OLDEST");
    puts("05 PASS");
    return 0;
}
