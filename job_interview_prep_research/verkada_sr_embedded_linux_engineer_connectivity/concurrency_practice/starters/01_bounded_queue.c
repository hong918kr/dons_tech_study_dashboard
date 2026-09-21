/* 01. Bounded thread-safe queue (MPSC/MPMC)
 * ------------------------------------------------------------------
 * 여러 생산자 스레드(도어 I/O 스레드 등)가 이벤트를 넣고, 소비자 스레드가
 * 꺼내서 직렬화/업로드한다. 큐가 가득 차면 생산자는 블록, 비면 소비자는 블록.
 * close()가 호출되면 남은 항목을 모두 소비한 뒤 소비자는 깨어나 종료한다.
 *
 * 핵심: mutex 1개 + condvar 2개(not_full / not_empty), 조건 검사는 반드시 while.
 * 빌드: cc -std=c11 -Wall -Wextra -pthread 01_bounded_queue.c
 */
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int    door_id;
    int    seq;
} AccessEvent;

typedef struct {
    AccessEvent    *buf;
    size_t          cap, head, tail, count;
    bool            closed;
    pthread_mutex_t m;
    pthread_cond_t  not_full, not_empty;
} BQueue;

int  bq_init(BQueue *q, size_t cap);
void bq_destroy(BQueue *q);
bool bq_push(BQueue *q, AccessEvent ev);        /* false = 큐가 close됨 */
bool bq_pop(BQueue *q, AccessEvent *out);       /* false = close + 비어있음 */
void bq_close(BQueue *q);

/* ------------------------------------------------------------------
 * 여기부터 직접 구현한다. 위의 선언부와 아래 self-test는 그대로 두고,
 * 아래 함수 목록을 채운 뒤 `make run N=01` 로 검증한다.
 *
 * 구현할 함수:
 *   - bq_init()
 *   - bq_destroy()
 *   - bq_push()
 *   - bq_pop()
 *   - bq_close()
 *
 * 막히면 ../solutions/01_bounded_queue.c 를 열되, 먼저 15분은 스스로 해볼 것.
 * ------------------------------------------------------------------ */

/* ------------------------------- self test ------------------------------- */
#define NPROD 4
#define NPER  2000

static BQueue g_q;
static long   g_consumed_sum;
static long   g_consumed_cnt;

static void *producer(void *arg)
{
    int id = (int)(intptr_t)arg;
    for (int i = 0; i < NPER; i++) {
        AccessEvent ev = { .door_id = id, .seq = i };
        if (!bq_push(&g_q, ev)) break;
    }
    return NULL;
}

static void *consumer(void *arg)
{
    (void)arg;
    AccessEvent ev;
    int last_seq[NPROD];
    for (int i = 0; i < NPROD; i++) last_seq[i] = -1;

    while (bq_pop(&g_q, &ev)) {
        /* 단일 소비자이므로 같은 생산자 내 순서는 반드시 보존돼야 한다 */
        assert(ev.seq == last_seq[ev.door_id] + 1);
        last_seq[ev.door_id] = ev.seq;
        g_consumed_sum += ev.seq;
        g_consumed_cnt++;
    }
    return NULL;
}

int main(void)
{
    pthread_t prod[NPROD], cons;
    assert(bq_init(&g_q, 8) == 0);        /* 일부러 작은 큐 → 블로킹 경로를 강제 */

    pthread_create(&cons, NULL, consumer, NULL);
    for (int i = 0; i < NPROD; i++)
        pthread_create(&prod[i], NULL, producer, (void *)(intptr_t)i);
    for (int i = 0; i < NPROD; i++)
        pthread_join(prod[i], NULL);

    bq_close(&g_q);                       /* 생산 끝 → 소비자 드레인 후 종료 */
    pthread_join(cons, NULL);

    long expect_cnt = (long)NPROD * NPER;
    long expect_sum = (long)NPROD * ((long)NPER * (NPER - 1) / 2);
    printf("consumed=%ld (expect %ld), sum=%ld (expect %ld)\n",
           g_consumed_cnt, expect_cnt, g_consumed_sum, expect_sum);
    assert(g_consumed_cnt == expect_cnt);
    assert(g_consumed_sum == expect_sum);

    bq_destroy(&g_q);
    puts("01 PASS");
    return 0;
}
