/* 09. 데드락 — 락 순서와 try-lock 백오프
 * ------------------------------------------------------------------
 * 두 개의 자원(예: 두 포트의 PoE 전력 예산, 두 WAN 인터페이스의 통계)을
 * 동시에 잠가야 하는 연산이 있다. 스레드 A가 (1→2) 순서로, 스레드 B가
 * (2→1) 순서로 잠그면 데드락이 발생한다.
 *
 * 해법 두 가지:
 *   (1) Lock ordering: 전역으로 정해진 순서(여기서는 주소/ID 순)로만 잠근다.
 *       — 가장 단순하고 확실. 코드 리뷰로 강제 가능.
 *   (2) try-lock + backoff: 두 번째 락을 trylock, 실패하면 첫 락도 풀고 재시도.
 *       — 순서를 정할 수 없을 때(콜백, 외부 API). livelock 방지 위해 랜덤 백오프.
 *
 * 임베디드 추가 논점: 우선순위 역전(priority inversion).
 *   낮은 우선순위 태스크가 락을 쥔 채 선점되면 높은 우선순위 태스크가 막힌다.
 *   → RTOS의 우선순위 상속(priority inheritance) mutex 사용, 또는 공유자원을
 *     단일 태스크가 소유하고 메시지 큐로만 접근하게 설계(락 자체를 없앤다).
 * 빌드: cc -std=c11 -Wall -Wextra -pthread 09_lock_order.c
 */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <sched.h>

typedef struct {
    int             id;
    long            budget_mw;
    pthread_mutex_t m;
} Port;

void port_init(Port *p, int id, long budget);
/* (1) 순서 고정 방식 */
bool transfer_ordered(Port *a, Port *b, long mw);
/* (2) trylock + backoff 방식 */
bool transfer_trylock(Port *a, Port *b, long mw);

static _Atomic unsigned long g_retries;

/*@impl-begin*/
void port_init(Port *p, int id, long budget)
{
    p->id = id;
    p->budget_mw = budget;
    pthread_mutex_init(&p->m, NULL);
}

bool transfer_ordered(Port *a, Port *b, long mw)
{
    if (a == b) return false;
    /* 전역 순서: id가 작은 쪽을 먼저 잠근다. 호출 방향과 무관하게 항상 동일 */
    Port *first  = (a->id < b->id) ? a : b;
    Port *second = (a->id < b->id) ? b : a;

    pthread_mutex_lock(&first->m);
    pthread_mutex_lock(&second->m);

    bool ok = false;
    if (a->budget_mw >= mw) {                 /* 잠근 뒤에 조건 검사 */
        a->budget_mw -= mw;
        b->budget_mw += mw;
        ok = true;
    }
    pthread_mutex_unlock(&second->m);         /* 해제 순서는 역순이 관례 */
    pthread_mutex_unlock(&first->m);
    return ok;
}

bool transfer_trylock(Port *a, Port *b, long mw)
{
    if (a == b) return false;
    for (;;) {
        pthread_mutex_lock(&a->m);
        if (pthread_mutex_trylock(&b->m) == 0) break;   /* 둘 다 확보 */

        pthread_mutex_unlock(&a->m);          /* ★ 반드시 첫 락도 놓는다 */
        atomic_fetch_add(&g_retries, 1ul);
        sched_yield();                        /* 실전: 랜덤 지수 백오프 */
    }

    bool ok = false;
    if (a->budget_mw >= mw) {
        a->budget_mw -= mw;
        b->budget_mw += mw;
        ok = true;
    }
    pthread_mutex_unlock(&b->m);
    pthread_mutex_unlock(&a->m);
    return ok;
}
/*@impl-end*/

/* ------------------------------- self test ------------------------------- */
#define NITER 50000

static Port g_p1, g_p2;
static bool g_use_trylock;

static void *ab(void *arg)
{
    (void)arg;
    for (int i = 0; i < NITER; i++)
        g_use_trylock ? transfer_trylock(&g_p1, &g_p2, 1)
                      : transfer_ordered(&g_p1, &g_p2, 1);
    return NULL;
}

static void *ba(void *arg)                    /* 반대 방향 — 순진하게 짜면 데드락 */
{
    (void)arg;
    for (int i = 0; i < NITER; i++)
        g_use_trylock ? transfer_trylock(&g_p2, &g_p1, 1)
                      : transfer_ordered(&g_p2, &g_p1, 1);
    return NULL;
}

static void run(bool trylock, const char *name)
{
    pthread_t t1, t2;
    g_use_trylock = trylock;
    g_retries = 0;
    port_init(&g_p1, 1, 30000);
    port_init(&g_p2, 2, 30000);

    pthread_create(&t1, NULL, ab, NULL);
    pthread_create(&t2, NULL, ba, NULL);
    pthread_join(t1, NULL);                   /* 데드락이면 여기서 영원히 멈춘다 */
    pthread_join(t2, NULL);

    long total = g_p1.budget_mw + g_p2.budget_mw;
    printf("  %-9s total=%ld (expect 60000) p1=%ld p2=%ld retries=%lu\n",
           name, total, g_p1.budget_mw, g_p2.budget_mw, (unsigned long)g_retries);
    assert(total == 60000);                   /* 전력 예산 총합은 보존 */
    pthread_mutex_destroy(&g_p1.m);
    pthread_mutex_destroy(&g_p2.m);
}

int main(void)
{
    run(false, "ordered");
    run(true,  "trylock");
    puts("09 PASS");
    return 0;
}
