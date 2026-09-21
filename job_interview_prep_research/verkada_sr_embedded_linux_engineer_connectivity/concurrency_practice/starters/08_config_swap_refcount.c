/* 08. 무중단 설정 교체 (atomic pointer swap + refcount)
 * ------------------------------------------------------------------
 * 클라우드에서 새 설정(APN, WAN 우선순위, 업로드 주기)이 내려온다. 여러 워커
 * 스레드가 설정을 읽는 중에 교체해야 하는데:
 *   - 읽기는 아주 빈번하고, 쓰기는 아주 드물다 → 읽기에 mutex는 낭비
 *   - 읽는 중인 설정을 free하면 use-after-free
 * 해법: 설정을 '불변(immutable) 객체'로 만들고 포인터만 원자적으로 교체.
 *       사용 중인 쪽이 refcount를 올리고, 0이 되는 순간 해제(= RCU의 축소판).
 *
 * 인터뷰 포인트:
 *   - acquire/release로 "새 객체의 내용"이 "포인터 publish"보다 먼저 보이게
 *   - '포인터 읽기'와 'refcount 증가' 사이의 창 → 이 창을 닫지 않으면 use-after-free.
 *     여기서는 아주 짧은 mutex로 두 동작을 원자화한다(대안: hazard pointer, RCU/epoch).
 *   - 마지막 감소에만 release→acquire 펜스를 걸어야 free가 안전
 * 빌드: cc -std=c11 -Wall -Wextra -pthread 08_config_swap_refcount.c
 */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    _Atomic int refs;         /* 이 객체를 붙잡고 있는 스레드 수 */
    uint32_t    version;
    uint32_t    upload_period_ms;
    char        apn[32];
} Config;

typedef struct {
    _Atomic(Config *) cur;
    /* 이름은 writer_m 이지만 reader 의 cfg_acquire 도 같은 락을 쓴다 —
     * '포인터 읽기 + refcount 증가'를 publish 와 원자적으로 만들기 위해서다.
     * 락 구간은 몇 나노초뿐이고, 설정을 실제로 쓰는 구간은 락 밖이다. */
    pthread_mutex_t   writer_m;
    _Atomic unsigned long frees;
} ConfigStore;

Config *config_new(uint32_t version, uint32_t period_ms, const char *apn);
void    cfg_store_init(ConfigStore *s, Config *initial);
Config *cfg_acquire(ConfigStore *s);            /* 읽기 시작 (refcount++) */
void    cfg_release(ConfigStore *s, Config *c); /* 읽기 종료 (refcount--) */
void    cfg_publish(ConfigStore *s, Config *fresh);  /* 새 설정 게시 */
void    cfg_store_destroy(ConfigStore *s);

/* ------------------------------------------------------------------
 * 여기부터 직접 구현한다. 위의 선언부와 아래 self-test는 그대로 두고,
 * 아래 함수 목록을 채운 뒤 `make run N=08` 로 검증한다.
 *
 * 구현할 함수:
 *   - config_new()
 *   - cfg_store_init()
 *   - cfg_drop()
 *   - cfg_acquire()
 *   - cfg_release()
 *   - cfg_publish()
 *   - cfg_store_destroy()
 *
 * 막히면 ../solutions/08_config_swap_refcount.c 를 열되, 먼저 15분은 스스로 해볼 것.
 * ------------------------------------------------------------------ */

/* ------------------------------- self test ------------------------------- */
#define NREADERS 4
#define NPUBLISH 2000

static ConfigStore g_store;
static _Atomic bool g_stop;
static _Atomic unsigned long g_reads, g_bad;

static void *reader(void *arg)
{
    (void)arg;
    while (!g_stop) {
        Config *c = cfg_acquire(&g_store);
        /* 불변식: period_ms == version * 10 (같은 객체에서 나온 값이어야 함) */
        if (c->upload_period_ms != c->version * 10u)
            atomic_fetch_add(&g_bad, 1ul);
        if (c->apn[0] != 'a')
            atomic_fetch_add(&g_bad, 1ul);
        atomic_fetch_add(&g_reads, 1ul);
        cfg_release(&g_store, c);
    }
    return NULL;
}

int main(void)
{
    pthread_t r[NREADERS];
    cfg_store_init(&g_store, config_new(1, 10, "apn.one"));

    for (int i = 0; i < NREADERS; i++) pthread_create(&r[i], NULL, reader, NULL);
    for (uint32_t v = 2; v <= NPUBLISH; v++)
        cfg_publish(&g_store, config_new(v, v * 10u, "apn.two"));
    g_stop = true;
    for (int i = 0; i < NREADERS; i++) pthread_join(r[i], NULL);

    Config *last = atomic_load(&g_store.cur);
    printf("publishes=%d reads=%lu inconsistent=%lu freed=%lu last_ver=%u refs=%d\n",
           NPUBLISH, (unsigned long)g_reads, (unsigned long)g_bad,
           (unsigned long)g_store.frees, last->version, atomic_load(&last->refs));
    assert(g_bad == 0);
    assert(last->version == NPUBLISH);
    assert(atomic_load(&last->refs) == 1);        /* store만 붙잡고 있어야 */
    assert(g_store.frees == NPUBLISH - 1);        /* 교체된 설정은 모두 해제 */
    cfg_store_destroy(&g_store);
    assert(g_store.frees == NPUBLISH);
    puts("08 PASS");
    return 0;
}
