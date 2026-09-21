/* 06. Worker pool + graceful shutdown
 * ------------------------------------------------------------------
 * 게이트웨이의 백그라운드 작업(로그 업로드, 설정 동기화, 디바이스 헬스체크)을
 * N개의 워커가 처리한다. 종료 시 두 가지 모드를 지원해야 한다:
 *   SHUTDOWN_DRAIN : 큐에 남은 작업을 모두 처리한 뒤 종료 (정상 재부팅)
 *   SHUTDOWN_NOW   : 진행 중 작업만 끝내고 큐는 버림 (watchdog/긴급)
 *
 * 인터뷰에서 자주 파는 edge case:
 *   - 워커가 cond_wait에 잠들어 있을 때 종료 → broadcast로 전부 깨워야 함
 *   - 종료 후 submit → 거부해야 함 (조용히 무시하면 버그 추적 불가)
 *   - join을 빼먹으면? → 스레드가 자원 해제 전에 free된 큐를 만진다(UAF)
 *   - 작업 함수가 blocking이면 종료가 지연된다 → 작업에 cancel 신호를 전달
 * 빌드: cc -std=c11 -Wall -Wextra -pthread 06_thread_pool.c
 */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*TaskFn)(void *arg);
typedef enum { POOL_RUNNING, POOL_DRAIN, POOL_STOP } PoolState;

typedef struct { TaskFn fn; void *arg; } Task;

typedef struct {
    pthread_t      *workers;
    size_t          nworkers;
    Task           *q;
    size_t          cap, head, count;
    PoolState       state;
    unsigned long   executed, rejected, discarded;
    pthread_mutex_t m;
    pthread_cond_t  work, slot;
} Pool;

int  pool_init(Pool *p, size_t nworkers, size_t qcap);
bool pool_submit(Pool *p, TaskFn fn, void *arg);   /* false = 종료 중이라 거부 */
void pool_shutdown(Pool *p, bool drain);           /* join까지 끝내고 반환 */

/* ------------------------------------------------------------------
 * 여기부터 직접 구현한다. 위의 선언부와 아래 self-test는 그대로 두고,
 * 아래 함수 목록을 채운 뒤 `make run N=06` 로 검증한다.
 *
 * 구현할 함수:
 *   - pool_take()
 *   - worker_main()
 *   - pool_init()
 *   - pool_submit()
 *   - pool_shutdown()
 *
 * 막히면 ../solutions/06_thread_pool.c 를 열되, 먼저 15분은 스스로 해볼 것.
 * ------------------------------------------------------------------ */

/* ------------------------------- self test ------------------------------- */
static _Atomic unsigned long g_counter;

static void inc_task(void *arg)
{
    (void)arg;
    atomic_fetch_add(&g_counter, 1u);
}

int main(void)
{
    /* case 1: drain 종료 → 제출한 모든 작업이 실행돼야 한다 */
    Pool p;
    g_counter = 0;
    assert(pool_init(&p, 4, 16) == 0);
    for (int i = 0; i < 5000; i++)
        assert(pool_submit(&p, inc_task, NULL));
    unsigned long executed_before = 0;
    pool_shutdown(&p, true);
    executed_before = p.executed;
    printf("drain : submitted=5000 executed=%lu counter=%lu discarded=%lu\n",
           executed_before, (unsigned long)g_counter, p.discarded);
    assert(g_counter == 5000);
    assert(p.discarded == 0);

    /* case 2: 즉시 종료 → 실행 + 폐기 합이 제출 수와 같아야 한다 */
    Pool p2;
    g_counter = 0;
    assert(pool_init(&p2, 2, 64) == 0);
    unsigned long submitted = 0;
    for (int i = 0; i < 2000; i++)
        if (pool_submit(&p2, inc_task, NULL)) submitted++;
    pool_shutdown(&p2, false);
    printf("stop  : submitted=%lu executed=%lu discarded=%lu\n",
           submitted, p2.executed, p2.discarded);
    assert(p2.executed + p2.discarded == submitted);
    assert(g_counter == p2.executed);

    /* case 3: 종료 후 submit은 거부 */
    Pool p3;
    assert(pool_init(&p3, 1, 4) == 0);
    pool_shutdown(&p3, true);
    /* shutdown이 자원을 해제했으므로 여기서 submit하면 UAF —
     * 그래서 실무 API는 shutdown과 destroy를 분리한다(아래 follow-up 참고) */
    puts("06 PASS");
    return 0;
}
