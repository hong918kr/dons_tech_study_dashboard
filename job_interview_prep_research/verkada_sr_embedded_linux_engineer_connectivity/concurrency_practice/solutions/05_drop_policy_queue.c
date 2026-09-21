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

/*@impl-begin*/
int tq_init(TeleQueue *q, size_t cap, DropPolicy p)
{
    if (cap == 0) return -1;
    memset(q, 0, sizeof *q);
    q->buf = calloc(cap, sizeof *q->buf);
    if (!q->buf) return -1;
    q->cap = cap;
    q->policy = p;
    pthread_mutex_init(&q->m, NULL);
    pthread_cond_init(&q->not_full, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    return 0;
}

void tq_destroy(TeleQueue *q)
{
    free(q->buf);
    pthread_mutex_destroy(&q->m);
    pthread_cond_destroy(&q->not_full);
    pthread_cond_destroy(&q->not_empty);
}

bool tq_push(TeleQueue *q, Sample s)
{
    pthread_mutex_lock(&q->m);
    if (q->closed) { pthread_mutex_unlock(&q->m); return false; }

    if (q->count == q->cap) {
        switch (q->policy) {
        case POLICY_BLOCK:
            while (q->count == q->cap && !q->closed)
                pthread_cond_wait(&q->not_full, &q->m);
            if (q->closed) { pthread_mutex_unlock(&q->m); return false; }
            break;
        case POLICY_DROP_NEWEST:
            q->dropped++;
            pthread_mutex_unlock(&q->m);
            return true;                       /* 성공으로 취급하되 카운트 */
        case POLICY_DROP_OLDEST:
            q->head = (q->head + 1) % q->cap;  /* 가장 오래된 것 폐기 */
            q->count--;
            q->dropped++;
            break;
        }
    }
    q->buf[(q->head + q->count) % q->cap] = s;
    q->count++;
    q->pushed++;
    pthread_mutex_unlock(&q->m);
    pthread_cond_signal(&q->not_empty);
    return true;
}

size_t tq_pop_batch(TeleQueue *q, Sample *out, size_t max)
{
    pthread_mutex_lock(&q->m);
    while (q->count == 0 && !q->closed)
        pthread_cond_wait(&q->not_empty, &q->m);

    size_t n = q->count < max ? q->count : max;   /* 배치로 꺼내 업로드 효율↑ */
    for (size_t i = 0; i < n; i++) {
        out[i] = q->buf[q->head];
        q->head = (q->head + 1) % q->cap;
    }
    q->count -= n;
    q->popped += n;
    pthread_mutex_unlock(&q->m);
    if (n) pthread_cond_broadcast(&q->not_full);
    return n;                                     /* 0 = closed && empty */
}

void tq_close(TeleQueue *q)
{
    pthread_mutex_lock(&q->m);
    q->closed = true;
    pthread_mutex_unlock(&q->m);
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
}
/*@impl-end*/

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
