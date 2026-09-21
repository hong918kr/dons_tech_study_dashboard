/* 02. Lock-free SPSC ring buffer (ISR ↔ task)
 * ------------------------------------------------------------------
 * UART RX ISR(생산자 1)이 바이트를 넣고, 파서 태스크(소비자 1)가 꺼낸다.
 * ISR 안에서는 mutex를 쓸 수 없다 → 락 없이, atomic head/tail로만 동기화.
 *
 * 핵심:
 *  - 생산자는 tail만 store, 소비자는 head만 store (각자 상대 인덱스는 load만)
 *  - release store / acquire load 짝: 데이터 write가 인덱스 publish보다 먼저 보이게
 *  - cap은 2의 거듭제곱 → % 대신 & (mask). 한 칸을 비워 full/empty 구분
 *  - volatile은 동기화 도구가 아니다. 순서 보장이 없다 → _Atomic 사용
 * 빌드: cc -std=c11 -Wall -Wextra -pthread 02_spsc_ring.c
 */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define RING_CAP 64u                       /* power of two */

typedef struct {
    uint8_t              buf[RING_CAP];
    _Atomic unsigned     head;             /* 소비자만 store */
    _Atomic unsigned     tail;             /* 생산자만 store */
} Ring;

void ring_init(Ring *r);
bool ring_push(Ring *r, uint8_t b);        /* 생산자(ISR) 전용 */
bool ring_pop(Ring *r, uint8_t *out);      /* 소비자(task) 전용 */
unsigned ring_count(const Ring *r);

/*@impl-begin*/
void ring_init(Ring *r)
{
    atomic_init(&r->head, 0u);
    atomic_init(&r->tail, 0u);
}

bool ring_push(Ring *r, uint8_t b)
{
    unsigned t = atomic_load_explicit(&r->tail, memory_order_relaxed); /* 내가 쓴 값 */
    unsigned next = (t + 1u) & (RING_CAP - 1u);
    /* 소비자가 얼마나 비웠는지 본다 → acquire */
    if (next == atomic_load_explicit(&r->head, memory_order_acquire))
        return false;                      /* full (한 칸은 항상 비워둔다) */

    r->buf[t] = b;                         /* 데이터 먼저 */
    /* release: 위의 데이터 write가 tail publish 이전에 보이도록 보장 */
    atomic_store_explicit(&r->tail, next, memory_order_release);
    return true;
}

bool ring_pop(Ring *r, uint8_t *out)
{
    unsigned h = atomic_load_explicit(&r->head, memory_order_relaxed);
    /* acquire: tail을 읽은 뒤 buf를 읽어야 생산자의 데이터가 보인다 */
    if (h == atomic_load_explicit(&r->tail, memory_order_acquire))
        return false;                      /* empty */

    *out = r->buf[h];
    atomic_store_explicit(&r->head, (h + 1u) & (RING_CAP - 1u), memory_order_release);
    return true;
}

unsigned ring_count(const Ring *r)
{
    unsigned t = atomic_load_explicit(&r->tail, memory_order_acquire);
    unsigned h = atomic_load_explicit(&r->head, memory_order_acquire);
    return (t - h) & (RING_CAP - 1u);
}
/*@impl-end*/

/* ------------------------------- self test ------------------------------- */
#define NBYTES 200000

static Ring g_ring;
static unsigned long g_sum_tx, g_sum_rx;
static unsigned long g_dropped;

static void *producer(void *arg)          /* ISR 역할 */
{
    (void)arg;
    for (int i = 0; i < NBYTES; i++) {
        uint8_t b = (uint8_t)(i & 0xFF);
        while (!ring_push(&g_ring, b)) {  /* 실제 ISR은 drop + 카운트만 한다 */
            g_dropped++;                  /* 여기선 버리지 않고 재시도 */
        }
        g_sum_tx += b;
    }
    return NULL;
}

static void *consumer(void *arg)
{
    (void)arg;
    int got = 0;
    uint8_t b;
    int expect = 0;
    while (got < NBYTES) {
        if (ring_pop(&g_ring, &b)) {
            assert(b == (uint8_t)(expect & 0xFF)); /* 순서 보존 확인 */
            expect++;
            g_sum_rx += b;
            got++;
        }
    }
    return NULL;
}

int main(void)
{
    pthread_t p, c;
    ring_init(&g_ring);
    assert(ring_count(&g_ring) == 0);

    pthread_create(&c, NULL, consumer, NULL);
    pthread_create(&p, NULL, producer, NULL);
    pthread_join(p, NULL);
    pthread_join(c, NULL);

    printf("tx_sum=%lu rx_sum=%lu retries=%lu final_count=%u\n",
           g_sum_tx, g_sum_rx, g_dropped, ring_count(&g_ring));
    assert(g_sum_tx == g_sum_rx);
    assert(ring_count(&g_ring) == 0);
    puts("02 PASS");
    return 0;
}
