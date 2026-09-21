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

/*@impl-begin*/
Config *config_new(uint32_t version, uint32_t period_ms, const char *apn)
{
    Config *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    atomic_init(&c->refs, 1);                   /* store가 들고 있는 참조 1 */
    c->version = version;
    c->upload_period_ms = period_ms;
    snprintf(c->apn, sizeof c->apn, "%s", apn);
    return c;
}

void cfg_store_init(ConfigStore *s, Config *initial)
{
    atomic_init(&s->cur, initial);
    atomic_init(&s->frees, 0ul);
    pthread_mutex_init(&s->writer_m, NULL);
}

static void cfg_drop(ConfigStore *s, Config *c)
{
    /* release: 내가 이 객체에 한 모든 접근이 감소보다 먼저 보이게 */
    if (atomic_fetch_sub_explicit(&c->refs, 1, memory_order_release) == 1) {
        /* 마지막 참조였다 → 다른 스레드의 접근을 모두 본 뒤 해제 */
        atomic_thread_fence(memory_order_acquire);
        atomic_fetch_add(&s->frees, 1ul);
        free(c);
    }
}

Config *cfg_acquire(ConfigStore *s)
{
    /* ★ 함정: 아래를 락 없이
     *      Config *c = atomic_load(&s->cur);
     *      atomic_fetch_add(&c->refs, 1);
     *   로 쓰면 안 된다. 두 줄 사이에 publisher가 교체 + 마지막 참조를 해제하면
     *   이미 free된 메모리의 refs를 건드린다(use-after-free).
     *   이 창을 닫는 방법은 셋뿐이다:
     *     (1) 아주 짧은 락으로 '포인터 읽기 + refcount 증가'를 원자화 (여기서 채택)
     *     (2) hazard pointer  (3) RCU / epoch-based reclamation
     *   락 구간은 몇 나노초고, 실제 설정 사용은 락 밖에서 이뤄진다. */
    pthread_mutex_lock(&s->writer_m);
    Config *c = atomic_load_explicit(&s->cur, memory_order_acquire);
    atomic_fetch_add_explicit(&c->refs, 1, memory_order_acquire);
    pthread_mutex_unlock(&s->writer_m);
    return c;
}

void cfg_release(ConfigStore *s, Config *c)
{
    cfg_drop(s, c);                             /* 해제는 락 밖에서 (refcount만) */
}

void cfg_publish(ConfigStore *s, Config *fresh)
{
    pthread_mutex_lock(&s->writer_m);           /* acquire와 같은 락 → 경쟁 창 제거 */
    Config *old = atomic_load_explicit(&s->cur, memory_order_relaxed);
    /* release: fresh의 필드 초기화가 포인터 publish보다 먼저 보이게 */
    atomic_store_explicit(&s->cur, fresh, memory_order_release);
    pthread_mutex_unlock(&s->writer_m);

    /* 이 시점부터 old는 store에서 도달 불가 → 남은 참조가 0이 되면 안전하게 free */
    cfg_drop(s, old);
}

void cfg_store_destroy(ConfigStore *s)
{
    Config *c = atomic_load(&s->cur);
    if (c) cfg_drop(s, c);
    pthread_mutex_destroy(&s->writer_m);
}
/*@impl-end*/

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
