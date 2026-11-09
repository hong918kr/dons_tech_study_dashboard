/* ENV: linux */
/* LPERF-M07 추가 연습 — 한 번에 검증 (solution.c 의 맵을 #include 해서 쓴다)
 *   연습 1  traced_lock: bpftrace 없이 코드 안에서 경합·대기·보유 시간을 잰다 (시계는 주입)
 *   연습 2  stripe_skew: 스트라이프별 대기 횟수에서 "한쪽으로 쏠렸나" 판정 (실측 @wait_cnt 고정 데이터)
 *   연습 3  smap_get2: 두 키를 락 없이 "같은 순간의 쌍"으로 읽기 (두 seq 검증, 강제 스케줄)
 *   연습 4  RCU 스타일 설정 테이블: 복사-후-교체 + QSBR 유예 기간 (옛 버전을 언제 해제해도 되나)
 *
 * build (Lima VM elinux): gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define LPERF_M07_NO_MAIN
#pragma GCC diagnostic ignored "-Wunused-function"
#include "solution.c"

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

static void on_alarm(int sig) {
    (void)sig;
    static const char msg[] = "FAIL  timeout\n";
    if (write(1, msg, sizeof msg - 1) < 0) _exit(2);
    _exit(1);
}

/* ===== 연습 1: traced_lock ===== */
static uint64_t g_fake_now;
static uint64_t fake_clock(void) { return g_fake_now; }

typedef struct {
    pthread_mutex_t m;
    uint64_t (*clock)(void);
    uint64_t lock_t0;
    /* 아래는 락을 쥔 동안만 갱신한다 — 별도 동기화가 필요 없다. */
    uint64_t acquires, contended, wait_ns, hold_ns, hold_max_ns;
} traced_lock_t;

static void tl_init(traced_lock_t *t, uint64_t (*clk)(void)) {
    memset(t, 0, sizeof *t);
    pthread_mutex_init(&t->m, NULL);
    t->clock = clk;
}
static atomic_int g_tl_contended_flag;      /* 테스트 핸드셰이크 전용 */

static void tl_lock(traced_lock_t *t) {
    uint64_t t0 = t->clock();
    bool c = pthread_mutex_trylock(&t->m) != 0;
    if (c) {
        atomic_store(&g_tl_contended_flag, 1);
        pthread_mutex_lock(&t->m);
    }
    /* 이제 락을 쥐었다: 통계를 고쳐도 안전하다 */
    uint64_t t1 = t->clock();
    t->acquires++;
    if (c) t->contended++;
    t->wait_ns += t1 - t0;
    t->lock_t0 = t1;
}
static void tl_unlock(traced_lock_t *t) {
    uint64_t h = t->clock() - t->lock_t0;
    t->hold_ns += h;
    if (h > t->hold_max_ns) t->hold_max_ns = h;
    pthread_mutex_unlock(&t->m);
}

typedef struct { traced_lock_t *t; atomic_int *locked; } tl_arg_t;
static void *tl_holder(void *ap) {
    tl_arg_t *a = ap;
    pthread_mutex_lock(&a->t->m);
    atomic_store(a->locked, 1);
    while (!atomic_load(&g_tl_contended_flag)) sched_yield();   /* main 이 trylock 실패를 기록할 때까지 쥐고 있다 */
    pthread_mutex_unlock(&a->t->m);
    return NULL;
}

static void test_traced(void) {
    printf("== 연습 1: traced_lock ==\n");
    traced_lock_t t;
    tl_init(&t, fake_clock);
    g_fake_now = 100;
    tl_lock(&t);
    g_fake_now = 350;
    tl_unlock(&t);
    CHECK(t.acquires == 1 && t.contended == 0);
    CHECK(t.wait_ns == 0 && t.hold_ns == 250 && t.hold_max_ns == 250);

    atomic_int locked = 0;
    tl_arg_t a = { &t, &locked };
    atomic_store(&g_tl_contended_flag, 0);
    pthread_t th;
    pthread_create(&th, NULL, tl_holder, &a);
    while (!atomic_load(&locked)) sched_yield();
    g_fake_now = 1000;
    tl_lock(&t);                                /* 다른 스레드가 쥐고 있다 → trylock 실패 */
    g_fake_now = 1100;
    tl_unlock(&t);
    pthread_join(th, NULL);
    CHECK(t.acquires == 2 && t.contended == 1);
    CHECK(t.hold_ns == 250 + 100);
}

/* ===== 연습 2: stripe_skew ===== */
/* 평균 대비 최댓값 비율을 permille 로 (1000 = 완전 균등). 전부 0 이면 0. *hot 에 최댓값 칸 번호(앞쪽 우선). */
static unsigned stripe_skew(const uint64_t *c, unsigned n, unsigned *hot) {
    uint64_t sum = 0, mx = 0;
    unsigned idx = 0;
    for (unsigned i = 0; i < n; i++) { sum += c[i]; if (c[i] > mx) { mx = c[i]; idx = i; } }
    *hot = idx;
    if (sum == 0) return 0;
    return (unsigned)(mx * n * 1000 / sum);
}

static void test_skew(void) {
    printf("== 연습 2: stripe_skew ==\n");
    /* 실측(striped 모드 6 s, @wait_cnt)에서 고른 스트라이프 10 개 */
    const uint64_t real[] = { 184, 190, 193, 194, 200, 206, 382, 343, 314, 301 };
    unsigned hot = 99;
    unsigned s = stripe_skew(real, 10, &hot);
    CHECK(hot == 6);
    CHECK(s == 382ull * 10 * 1000 / 2507);
    CHECK(s < 2000);                           /* 2 배 미만: 키 분포는 균등에 가깝다 */
    const uint64_t hotkey[] = { 5, 5, 5, 5000, 5, 5, 5, 5 };
    CHECK(stripe_skew(hotkey, 8, &hot) > 7000);
    CHECK(hot == 3);
    const uint64_t zero[4] = { 0, 0, 0, 0 };
    CHECK(stripe_skew(zero, 4, &hot) == 0);
}

/* ===== 연습 3: smap_get2 ===== */
static bool smap_get2(smap_t *m, uint64_t k1, val_t *o1, uint64_t k2, val_t *o2, unsigned *retries) {
    uint32_t a = stripe_of(k1, m->n), b = stripe_of(k2, m->n);
    stripe_t *sa = &m->s[a], *sb = &m->s[b];
    unsigned fails = 0;
    while (fails < SEQ_MAX_RETRY) {
        uint32_t s1 = atomic_load_explicit(&sa->seq, memory_order_acquire);
        uint32_t s2 = atomic_load_explicit(&sb->seq, memory_order_acquire);
        if ((s1 | s2) & 1) { fails++; continue; }
        val_t t1, t2;
        slot_t *e1 = table_find(&sa->t, k1, false), *e2 = table_find(&sb->t, k2, false);
        if (e1) for (int i = 0; i < VAL_WORDS; i++) { t1.w[i] = rd(&e1->w[i]); if (g_read_hook) g_read_hook(i); }
        if (e2) for (int i = 0; i < VAL_WORDS; i++) { t2.w[i] = rd(&e2->w[i]); if (g_read_hook) g_read_hook(i); }
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(&sa->seq, memory_order_relaxed) == s1 &&
            atomic_load_explicit(&sb->seq, memory_order_relaxed) == s2) {
            if (retries) *retries = fails;
            if (e1) *o1 = t1;
            if (e2) *o2 = t2;
            return e1 && e2;
        }
        fails++;
    }
    /* 상한 도달: 작은 번호부터 두 락을 잡고 읽는다 (smap_put2 와 같은 순서) */
    if (retries) *retries = fails;
    uint32_t lo = a < b ? a : b, hi = a < b ? b : a;
    pthread_mutex_lock(&m->s[lo].lock);
    if (hi != lo) pthread_mutex_lock(&m->s[hi].lock);
    slot_t *e1 = table_find(&sa->t, k1, false), *e2 = table_find(&sb->t, k2, false);
    if (e1) slot_read(e1, o1);
    if (e2) slot_read(e2, o2);
    if (hi != lo) pthread_mutex_unlock(&m->s[hi].lock);
    pthread_mutex_unlock(&m->s[lo].lock);
    return e1 && e2;
}

static val_t mkval(uint64_t b) { val_t v; for (int i = 0; i < VAL_WORDS; i++) v.w[i] = b + (uint64_t)i; return v; }

static smap_t *g_h_map;
static int g_h_fired;
static void hook_put2_midway(int word) {
    if (word != 2 || g_h_fired) return;
    g_h_fired = 1;
    val_t v = mkval(9000);
    (void)smap_put2(g_h_map, 1, &v, 2, &v);    /* 첫 번째 키를 읽는 도중 두 키 모두 쓰기 */
}

typedef struct { smap_t *m; atomic_int *stop; uint64_t bad, reads; } g2_arg_t;
static void *g2_reader(void *ap) {
    g2_arg_t *a = ap;
    while (!atomic_load_explicit(a->stop, memory_order_relaxed)) {
        val_t x, y;
        unsigned r;
        memset(&x, 0, sizeof x); memset(&y, 0, sizeof y);
        if (smap_get2(a->m, 1, &x, 2, &y, &r)) {
            a->reads++;
            if (x.w[0] != y.w[0]) a->bad++;     /* smap_put2 는 항상 같은 값을 쓴다 */
        }
    }
    return NULL;
}
static void *g2_writer(void *ap) {
    smap_t *m = ap;
    for (uint64_t i = 1; i <= 60000; i++) {
        val_t v = mkval(i);
        (void)smap_put2(m, 1, &v, 2, &v);
    }
    return NULL;
}

static void test_get2(void) {
    printf("== 연습 3: smap_get2 ==\n");
    smap_t m;
    CHECK(smap_init(&m, 16, 8));
    val_t v = mkval(100);
    CHECK(smap_put2(&m, 1, &v, 2, &v));
    val_t a, b;
    unsigned r = 9;
    memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    CHECK(smap_get2(&m, 1, &a, 2, &b, &r) && a.w[0] == 100 && b.w[0] == 100 && r == 0);
    CHECK(!smap_get2(&m, 1, &a, 3, &b, &r));    /* 없는 키 */

    g_h_map = &m; g_h_fired = 0;
    g_read_hook = hook_put2_midway;
    memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    bool got = smap_get2(&m, 1, &a, 2, &b, &r);
    g_read_hook = NULL;
    CHECK(g_h_fired == 1);
    CHECK(got && a.w[0] == 9000 && b.w[0] == 9000 && r == 1);

    atomic_int stop = 0;
    pthread_t rt[3], wt;
    g2_arg_t ra[3];
    for (int i = 0; i < 3; i++) { ra[i] = (g2_arg_t){ &m, &stop, 0, 0 }; pthread_create(&rt[i], NULL, g2_reader, &ra[i]); }
    pthread_create(&wt, NULL, g2_writer, &m);
    pthread_join(wt, NULL);
    atomic_store(&stop, 1);
    uint64_t bad = 0, reads = 0;
    for (int i = 0; i < 3; i++) { pthread_join(rt[i], NULL); bad += ra[i].bad; reads += ra[i].reads; }
    CHECK(bad == 0);
    CHECK(reads > 0);
    smap_destroy(&m);
}

/* ===== 연습 4: RCU 스타일 설정 테이블 (QSBR) ===== */
#define CFG_READERS 4
typedef struct cfg {
    uint64_t version;
    uint64_t limits[8];                 /* 불변식: limits[i] == version * (i+1) */
    uint64_t retired_at;
    struct cfg *next;
} cfg_t;

typedef struct {
    _Alignas(128) _Atomic uint64_t seen;
} reader_slot_t;

typedef struct {
    _Atomic(cfg_t *) cur;
    _Atomic uint64_t gen;
    reader_slot_t rd[CFG_READERS];
    int nreaders;
    cfg_t *retired;                     /* writer 전용 (단일 writer 가정) */
    uint64_t freed;
} rcu_cfg_t;

static cfg_t *cfg_new(uint64_t ver) {
    cfg_t *c = calloc(1, sizeof *c);
    if (!c) abort();
    c->version = ver;
    for (int i = 0; i < 8; i++) c->limits[i] = ver * (uint64_t)(i + 1);
    return c;
}

static void rcfg_init(rcu_cfg_t *r, int nreaders) {
    memset(r, 0, sizeof *r);
    r->nreaders = nreaders;
    atomic_store(&r->cur, cfg_new(1));
    atomic_store(&r->gen, 1);
}

/* 읽기 구간 시작: 잠금 없음, 공유 메모리에 쓰지 않음 */
static cfg_t *rcfg_read(rcu_cfg_t *r) { return atomic_load_explicit(&r->cur, memory_order_acquire); }

/* 읽기 구간 밖에서만 호출: "내가 이전에 받은 포인터는 이제 안 쓴다" */
static void rcfg_quiescent(rcu_cfg_t *r, int id) {
    atomic_store_explicit(&r->rd[id].seen, atomic_load(&r->gen), memory_order_release);
}

/* 단일 writer: 새 버전을 게시하고 옛 버전을 retired 에 넣는다 */
static void rcfg_publish(rcu_cfg_t *r, cfg_t *nu) {
    cfg_t *old = atomic_exchange(&r->cur, nu);
    old->retired_at = atomic_fetch_add(&r->gen, 1) + 1;
    old->next = r->retired;
    r->retired = old;
}

/* 모든 reader 가 retired_at 이후 quiescent 를 지난 항목만 해제한다. 반환 = 해제 개수 */
static int rcfg_reclaim(rcu_cfg_t *r) {
    uint64_t min_seen = UINT64_MAX;
    for (int i = 0; i < r->nreaders; i++) {
        uint64_t s = atomic_load_explicit(&r->rd[i].seen, memory_order_acquire);
        if (s < min_seen) min_seen = s;
    }
    int n = 0;
    cfg_t **pp = &r->retired;
    while (*pp) {
        cfg_t *c = *pp;
        if (c->retired_at <= min_seen) { *pp = c->next; free(c); r->freed++; n++; }
        else pp = &c->next;
    }
    return n;
}

typedef struct { rcu_cfg_t *r; int id; atomic_int *stop; uint64_t bad, reads; } cfg_arg_t;
static void *cfg_reader(void *ap) {
    cfg_arg_t *a = ap;
    while (!atomic_load_explicit(a->stop, memory_order_relaxed)) {
        cfg_t *c = rcfg_read(a->r);
        for (int i = 0; i < 8; i++) if (c->limits[i] != c->version * (uint64_t)(i + 1)) a->bad++;
        a->reads++;
        rcfg_quiescent(a->r, a->id);            /* 이 줄 이후 c 를 쓰면 use-after-free */
    }
    return NULL;
}

static void test_rcu(void) {
    printf("== 연습 4: RCU 스타일 설정 테이블 ==\n");
    rcu_cfg_t r;
    rcfg_init(&r, 2);
    cfg_t *held = rcfg_read(&r);                /* reader 0 이 옛 버전을 들고 있다 */
    rcfg_quiescent(&r, 1);
    rcfg_publish(&r, cfg_new(2));
    CHECK(atomic_load(&r.cur)->version == 2);
    CHECK(held->version == 1);                  /* 아직 살아 있다 */
    CHECK(rcfg_reclaim(&r) == 0);               /* 아무도 게시 후 quiescent 를 안 지났다 */
    rcfg_quiescent(&r, 1);
    CHECK(rcfg_reclaim(&r) == 0);               /* reader 0 이 아직 들고 있다 */
    rcfg_quiescent(&r, 0);                      /* reader 0 이 놓는다 */
    CHECK(rcfg_reclaim(&r) == 1);
    CHECK(r.freed == 1 && r.retired == NULL);

    rcu_cfg_t r2;
    rcfg_init(&r2, 3);
    atomic_int stop = 0;
    pthread_t th[3];
    cfg_arg_t ar[3];
    for (int i = 0; i < 3; i++) { ar[i] = (cfg_arg_t){ &r2, i, &stop, 0, 0 }; pthread_create(&th[i], NULL, cfg_reader, &ar[i]); }
    for (uint64_t v = 2; v <= 3000; v++) { rcfg_publish(&r2, cfg_new(v)); (void)rcfg_reclaim(&r2); }
    atomic_store(&stop, 1);
    uint64_t bad = 0, reads = 0;
    for (int i = 0; i < 3; i++) { pthread_join(th[i], NULL); bad += ar[i].bad; reads += ar[i].reads; }
    CHECK(bad == 0);
    CHECK(reads > 0);
    for (int i = 0; i < 3; i++) rcfg_quiescent(&r2, i);
    (void)rcfg_reclaim(&r2);
    CHECK(r2.retired == NULL);
    CHECK(r2.freed == 3000 - 1);
    CHECK(atomic_load(&r2.cur)->version == 3000);
    free(atomic_load(&r2.cur));
    free(atomic_load(&r.cur));
}

int main(void) {
    signal(SIGALRM, on_alarm);
    alarm(60);
    test_traced();
    test_skew();
    test_get2();
    test_rcu();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
