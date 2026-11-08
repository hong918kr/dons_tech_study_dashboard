/* OS-A03 RCU 개념 구현: epoch 기반 메모리 회수
 *
 * 읽기 다수(read-mostly) 설정 테이블. 리더는 락 없이 포인터 하나를 읽어 테이블 전체를 본다.
 * writer 는 새 버전을 만들어 포인터를 바꾸고(atomic_exchange), 옛 버전은 "아무 리더도 안 볼 때" 해제한다.
 *   Part A  EBR(epoch-based reclamation): 리더별 슬롯 {active, local epoch} (128 B 패딩), 전역 epoch,
 *           retire 목록(limbo). 해제 조건 = retire epoch + 2 <= 전역 epoch  (고전 3-epoch 방식).
 *   Part B  어려운 부분: epoch 1 칸 전진으로는 부족한 이유(강제 스케줄), 멈춘 리더 = 메모리 무한 증가 →
 *           limbo 상한 + 역압(backpressure), 막고 있는 리더 찾기, synchronize(상한 있는 대기).
 *   Part C  펌웨어: 제어 루프 태스크들이 읽는 보정 테이블. MMU·malloc 없는 정적 버퍼 3 개 풀 +
 *           QSBR(quiescent-state-based reclamation): "모든 태스크가 루프 맨 위를 지났다" = grace period.
 *
 * 체크 테스트는 올바른 구현이면 매 실행 항상 성립하는 것만 본다 (해제된 버전 번호 목록, limbo 개수,
 * 체크섬 깨짐 0, 버전 역행 0, 할당 수 == 해제 수 + 1). 각 Part 의 스트레스는 단위 테스트가 다 통과해야 돈다.
 * use-after-free "없음" 의 증거 = ASan 빌드로 이 파일을 돌려 보고 없음 (cfg_free 는 해제 전에 독도 칠한다).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(ATOMIC_LLONG_LOCK_FREE == 2 && ATOMIC_POINTER_LOCK_FREE == 2, "need lock-free 64-bit/pointer atomics");

#define CL 128                 /* Apple M 계열 캐시 라인 (sysctl hw.cachelinesize = 128). Cortex-A 는 보통 64 */
#ifndef CFG_N
#define CFG_N 32               /* 보정 계수 개수 (데모는 -DCFG_N=1024 로 키운다) */
#endif
#ifndef LIMBO_CAP
#define LIMBO_CAP 32           /* 해제 대기 버전 상한 (데모는 -DLIMBO_CAP=4096) */
#endif
#define MAX_READERS 16

/* ======================= 공통: 설정 테이블 (한 번 게시되면 불변) ======================= */
typedef struct cfg {
    uint64_t version;
    int32_t gain[CFG_N];
    uint32_t sum;              /* version + gain[] 체크섬: 찢어진/독 칠한 테이블을 잡는다 */
} cfg_t;

static uint32_t cfg_sum(const cfg_t *c) {
    uint32_t h = 2166136261u ^ (uint32_t)c->version;
    for (int i = 0; i < CFG_N; i++) h = (h ^ (uint32_t)c->gain[i]) * 16777619u;
    return h;
}
static void cfg_fill(cfg_t *c, uint64_t v) {
    c->version = v;
    for (int i = 0; i < CFG_N; i++) c->gain[i] = (int32_t)((v * 2654435761u) ^ (uint64_t)(i * 40503));
    c->sum = cfg_sum(c);
}
static bool cfg_ok(const cfg_t *c) { return c->sum == cfg_sum(c); }

/* 힙 버전 할당/해제 (테스트용 기록 포함). 해제 기록은 포인터가 아니라 버전 번호로 남긴다:
 * free 뒤의 포인터 값은 C11 6.2.4p2 에 따라 "불확정" 이라 비교하면 안 된다. */
#define FLOG 64
static atomic_long g_live;                 /* 할당 - 해제 */
static uint64_t g_freed_ver[FLOG];         /* 해제된 버전 번호 (writer 만 씀, 앞의 FLOG 개) */
static long g_nfreed;                      /* 해제 횟수 (writer 만 씀) */
static cfg_t *cfg_new(uint64_t v) {
    cfg_t *c = malloc(sizeof *c);
    if (!c) { perror("malloc"); exit(2); }
    cfg_fill(c, v);
    atomic_fetch_add_explicit(&g_live, 1, memory_order_relaxed);
    return c;
}
static void cfg_free(void *p) {
    cfg_t *c = p;
    if (g_nfreed < FLOG) g_freed_ver[g_nfreed] = c->version;
    g_nfreed++;
    memset(c, 0xA5, sizeof *c);            /* 독 칠하기: 늦은 리더가 있으면 cfg_ok 가 깨지고 TSan 이 레이스로 본다 */
    atomic_fetch_sub_explicit(&g_live, 1, memory_order_relaxed);
    free(c);
}
static bool was_freed(uint64_t v) {
    for (long i = 0; i < g_nfreed && i < FLOG; i++) if (g_freed_ver[i] == v) return true;
    return false;
}

/* ======================= Part A — EBR ======================= */
/* 리더 슬롯 한 워드: 0 = 읽기 구간 밖, (e << 1) | 1 = "epoch e 에 들어와 있음". 슬롯마다 캐시 라인 하나. */
typedef struct { _Alignas(CL) _Atomic uint64_t word; } ebr_slot_t;
typedef struct { void *p; uint64_t epoch; } ebr_limbo_t;
typedef struct {
    _Alignas(CL) _Atomic uint64_t epoch;   /* 전역 epoch: 단조 증가 (64 비트라 넘침 걱정 없음) */
    ebr_slot_t slot[MAX_READERS];
    int nslots;
    /* 아래는 writer 쪽만 (cfgdb_t.wlock 을 쥔 스레드) */
    _Alignas(CL) ebr_limbo_t limbo[LIMBO_CAP];
    int nlimbo;
    void (*free_fn)(void *);
} ebr_t;

static void ebr_init(ebr_t *d, int nslots, void (*free_fn)(void *)) {
    atomic_init(&d->epoch, 0);
    for (int i = 0; i < MAX_READERS; i++) atomic_init(&d->slot[i].word, 0);
    d->nslots = nslots;
    d->nlimbo = 0;
    d->free_fn = free_fn;
}

/* 리더 r 의 읽기 구간 시작/끝 (rcu_read_lock / rcu_read_unlock 에 해당). 기다리지 않는다. */
static void ebr_enter(ebr_t *d, int r) {
    (void)d; (void)r;
    /* TODO A1 */
}
static void ebr_exit(ebr_t *d, int r) {
    (void)d; (void)r;
    /* TODO A1 */
}

/* 활동 중인 리더가 모두 현재 epoch 에 있으면 epoch 을 한 칸 올린다. 올렸으면 true. */
static bool ebr_try_advance(ebr_t *d) {
    (void)d;
    return false; /* TODO A2 */
}

/* 옛 버전 p 를 limbo 에 넣는다 (호출자가 nlimbo < LIMBO_CAP 을 보장). 기록 epoch = 지금 전역 epoch. */
static void ebr_retire(ebr_t *d, void *p) {
    (void)d; (void)p;
    /* TODO A3 */
}
/* retire epoch + 2 <= 전역 epoch 인 것을 모두 해제. 해제한 개수 반환. */
static int ebr_reclaim(ebr_t *d) {
    (void)d;
    return 0; /* TODO A3 */
}

/* --- 설정 DB: 포인터 하나 + EBR + writer 뮤텍스 (writer 끼리만 직렬화, 리더는 락 없음) --- */
typedef struct {
    _Alignas(CL) _Atomic(cfg_t *) cur;
    ebr_t ebr;
    pthread_mutex_t wlock;
} cfgdb_t;

static void cfgdb_init(cfgdb_t *db, int nreaders) {
    atomic_init(&db->cur, cfg_new(1));
    ebr_init(&db->ebr, nreaders, cfg_free);
    pthread_mutex_init(&db->wlock, NULL);
}
static void cfgdb_destroy(cfgdb_t *db) {   /* 모든 리더가 끝난 뒤에만 */
    for (int i = 0; i < db->ebr.nlimbo; i++) cfg_free(db->ebr.limbo[i].p);
    db->ebr.nlimbo = 0;
    cfg_free(atomic_load(&db->cur));
    pthread_mutex_destroy(&db->wlock);
}
/* 리더 경로 (주어짐). seq_cst load: ebr_enter 의 seq_cst store 와 짝 — acquire(ldapr) 면 SB 재배치 가능 */
static const cfg_t *cfg_read_begin(cfgdb_t *db, int r) {
    ebr_enter(&db->ebr, r);
    return atomic_load_explicit(&db->cur, memory_order_seq_cst);
}
static void cfg_read_end(cfgdb_t *db, int r) { ebr_exit(&db->ebr, r); }

/* 새 버전 version 을 게시. 0 = 성공, -1 = limbo 가 꽉 차서 거절 (교체하지 않음 — 역압). */
static int cfg_update(cfgdb_t *db, uint64_t version) {
    (void)db; (void)version;
    return -1; /* TODO A4 */
}

/* ======================= Part B — grace period 기다리기 · 막는 리더 찾기 ======================= */
/* synchronize_rcu 흉내: 호출 시점 epoch 에서 2 칸 전진할 때까지 (최대 max_spins 번 시도) 기다린다.
 * 성공하면 true — 그 전에 retire 한 것은 모두 해제 가능. 상한 = 읽기 구간 안에서 부르는 실수의 안전망. */
static bool ebr_synchronize(ebr_t *d, unsigned max_spins) {
    (void)d; (void)max_spins;
    return false; /* TODO B1 */
}
/* grace period 를 막고 있는 (옛 epoch 에 머문) 리더 번호, 없으면 -1. 워치독/진단용. */
static int ebr_blocker(ebr_t *d) {
    (void)d;
    return -1; /* TODO B2 */
}
static bool cfg_synchronize(cfgdb_t *db, unsigned max_spins) {   /* 주어짐: 기다린 뒤 회수 */
    pthread_mutex_lock(&db->wlock);
    bool ok = ebr_synchronize(&db->ebr, max_spins);
    ebr_reclaim(&db->ebr);
    pthread_mutex_unlock(&db->wlock);
    return ok;
}

/* ======================= Part C — 펌웨어: QSBR + 정적 버퍼 풀 ======================= */
/* 제어 루프 태스크는 읽기 구간 표시를 하지 않는다. 대신 루프 맨 위에서 "지금 아무 테이블도 안 쥠" 을
 * 알린다 (quiescent state). writer 는 버퍼를 바꾼 뒤 gp 를 올리고, 모든 태스크의 seen >= 그 gp 가 되면
 * 옛 버퍼를 다시 쓴다. malloc 없음: 버퍼 3 개 (LIVE 1 + RETIRED 최대 2). */
#ifndef NTASK
#define NTASK 4                /* 태스크 슬롯 수 (벤치는 -DNTASK=12) */
#endif
#define NPOOL 3
enum { BUF_FREE, BUF_LIVE, BUF_RETIRED };
typedef struct { _Alignas(CL) _Atomic uint64_t seen; } qtask_t;
typedef struct {
    cfg_t buf[NPOOL];
    int state[NPOOL];               /* writer 만 */
    uint64_t retired_gp[NPOOL];     /* writer 만 */
    _Alignas(CL) _Atomic(const cfg_t *) cur;
    _Alignas(CL) _Atomic uint64_t gp;
    qtask_t task[NTASK];
    int ntask;
} qsbr_t;

static void qsbr_init(qsbr_t *q, int ntask) {
    for (int i = 0; i < NPOOL; i++) { q->state[i] = BUF_FREE; q->retired_gp[i] = 0; }
    cfg_fill(&q->buf[0], 1);
    q->state[0] = BUF_LIVE;
    atomic_init(&q->cur, &q->buf[0]);
    atomic_init(&q->gp, 1);
    for (int t = 0; t < NTASK; t++) atomic_init(&q->task[t].seen, 1);
    q->ntask = ntask;
}
/* 태스크 쪽 (주어짐): 루프 안에서 몇 번이든 부른다. 장벽 없음 수준의 비용 (acquire load 1 번). */
static const cfg_t *qsbr_get(qsbr_t *q) { return atomic_load_explicit(&q->cur, memory_order_acquire); }

/* 태스크 t 의 루프 맨 위: "이전 반복에서 쥔 테이블은 이제 안 쓴다". */
static void qsbr_quiescent(qsbr_t *q, int t) {
    (void)q; (void)t;
    /* TODO C1 */
}
/* RETIRED 버퍼 중 모든 태스크가 retired_gp 이후 루프 맨 위를 지난 것을 FREE 로. 개수 반환. */
static int qsbr_poll(qsbr_t *q) {
    (void)q;
    return 0; /* TODO C2 */
}
/* 새 버전 게시. 0 = 성공, -1 = FREE 버퍼 없음 (늦은 태스크가 있음 — 다음 주기에 다시). */
static int qsbr_publish(qsbr_t *q, uint64_t version) {
    (void)q; (void)version;
    return -1; /* TODO C3 */
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define FAILS() (g_total - g_pass)
static void skip(int n, const char *what) {        /* 단위 테스트가 실패하면 스트레스는 건너뛴다(=실패로 셈) */
    g_total += n;
    printf("SKIP %d stress checks (%s): unit checks failed\n", n, what);
}

/* --- Part A 스트레스: 리더 6 + writer 1 --- */
#define A_READERS 6
#define A_UPDATES 20000u
#define A_MIN_READS 40000u
static cfgdb_t g_db;
static atomic_int g_a_done;
static atomic_ulong g_a_bad, g_a_backwards, g_a_reads;
static void *a_reader(void *arg) {
    int r = (int)(intptr_t)arg;
    unsigned long bad = 0, back = 0, n = 0;
    uint64_t last = 0;
    while (n < A_MIN_READS || !atomic_load_explicit(&g_a_done, memory_order_acquire)) {
        const cfg_t *c = cfg_read_begin(&g_db, r);
        if (!cfg_ok(c)) bad++;                 /* 해제·독 칠한 테이블을 읽으면 여기서 걸린다 */
        if (c->version < last) back++;         /* 같은 리더가 본 버전은 역행하지 않는다 (read-read coherence) */
        last = c->version;
        cfg_read_end(&g_db, r);
        n++;
    }
    atomic_fetch_add(&g_a_bad, bad); atomic_fetch_add(&g_a_backwards, back); atomic_fetch_add(&g_a_reads, n);
    return NULL;
}
static atomic_ulong g_a_refused;
static void *a_writer(void *arg) {
    (void)arg;
    unsigned long refused = 0;
    for (uint64_t v = 2; v < 2 + A_UPDATES; v++) {
        while (cfg_update(&g_db, v) != 0) {    /* 역압: 꽉 차면 양보하고 다시 */
            if (++refused > 20000000ul) goto out;
            sched_yield();
        }
    }
out:
    atomic_store(&g_a_refused, refused);
    atomic_store_explicit(&g_a_done, 1, memory_order_release);
    return NULL;
}

/* --- Part C 스트레스: 제어 루프 태스크 4 + writer 1 --- */
#define C_PUBLISH 20000u
#define C_MIN_LOOPS 40000u
static qsbr_t g_q;
static atomic_int g_c_done;
static atomic_ulong g_c_bad, g_c_backwards;
static void *c_task(void *arg) {
    int t = (int)(intptr_t)arg;
    unsigned long bad = 0, back = 0, n = 0;
    uint64_t last = 0;
    int64_t out = 0;
    while (n < C_MIN_LOOPS || !atomic_load_explicit(&g_c_done, memory_order_acquire)) {
        qsbr_quiescent(&g_q, t);               /* 루프 맨 위 */
        const cfg_t *c = qsbr_get(&g_q);
        out += c->gain[n % CFG_N];             /* "제어 계산" */
        if (!cfg_ok(c)) bad++;
        if (c->version < last) back++;
        last = c->version;
        n++;
    }
    (void)out;
    atomic_fetch_add(&g_c_bad, bad); atomic_fetch_add(&g_c_backwards, back);
    return NULL;
}
static atomic_ulong g_c_published;
static void *c_writer(void *arg) {
    (void)arg;
    unsigned long ok = 0, spins = 0;
    for (uint64_t v = 2; v < 2 + C_PUBLISH; v++) {
        while (qsbr_publish(&g_q, v) != 0) {
            if (++spins > 20000000ul) goto out;
            sched_yield();
        }
        ok++;
    }
out:
    atomic_store(&g_c_published, ok);
    atomic_store_explicit(&g_c_done, 1, memory_order_release);
    return NULL;
}

int main(void) {
    /* ---------- Part A 단위: 단일 스레드, 리더 "핸들" 로 순서를 강제 ---------- */
    ebr_t d1;                                                 /* 프리미티브만: retire → 2 칸 전진 → 해제 */
    ebr_init(&d1, 1, cfg_free);
    ebr_retire(&d1, cfg_new(50));
    CHECK(d1.nlimbo == 1 && d1.limbo[0].epoch == 0 && ebr_reclaim(&d1) == 0);
    CHECK(ebr_try_advance(&d1) && ebr_reclaim(&d1) == 0);    /* epoch 1: 아직 */
    CHECK(ebr_try_advance(&d1) && ebr_reclaim(&d1) == 1 && was_freed(50) && d1.nlimbo == 0);   /* epoch 2: 해제 */
    cfgdb_t db;
    cfgdb_init(&db, 2);
    const cfg_t *p = cfg_read_begin(&db, 0);
    CHECK(p->version == 1 && cfg_ok(p));
    CHECK(atomic_load(&db.ebr.slot[0].word) == 1u);          /* epoch 0 에 들어와 있음: (0<<1)|1 */
    cfg_read_end(&db, 0);
    CHECK(atomic_load(&db.ebr.slot[0].word) == 0u);
    CHECK(ebr_try_advance(&db.ebr) && atomic_load(&db.ebr.epoch) == 1u);   /* 아무도 없으면 전진 */
    CHECK(cfg_update(&db, 2) == 0 && db.ebr.nlimbo == 1);   /* 리더 없음: v1 retire(1), 전진 → 2, 아직 2 칸 아님 */
    CHECK(cfg_update(&db, 3) == 0 && was_freed(1) && !was_freed(2));      /* 다음 갱신 때 v1 해제 */
    CHECK(atomic_load(&g_live) == 1 + db.ebr.nlimbo);       /* 살아 있는 = 현재 + limbo */
    int unit_a = FAILS();

    /* ---------- Part B 단위: 강제 스케줄 ---------- */
    /* 리더 0 이 v3 을 쥔 채로 멈춰 있다 */
    cfg_synchronize(&db, 1000);
    long before = g_nfreed;
    const cfg_t *r0 = cfg_read_begin(&db, 0);
    uint64_t e0 = atomic_load(&db.ebr.epoch);
    CHECK(r0->version == 3);
    CHECK(cfg_update(&db, 4) == 0);                          /* v3 retire(e0), 리더 0 은 e0 이라 전진 1 번은 됨 */
    CHECK(atomic_load(&db.ebr.epoch) == e0 + 1 && !was_freed(3));   /* ★ 1 칸 전진은 "안전" 이 아니다 */
    CHECK(!ebr_try_advance(&db.ebr) && ebr_blocker(&db.ebr) == 0);   /* 두 번째 전진은 리더 0 이 막는다 */
    CHECK(cfg_ok(r0) && r0->version == 3);                   /* 여전히 읽을 수 있다 (ASan 빌드로 확인) */
    const cfg_t *r1 = cfg_read_begin(&db, 1);                /* 리더 1 은 새 epoch 에서 v4 를 본다 */
    CHECK(r1->version == 4 && ebr_blocker(&db.ebr) == 0);
    CHECK(cfg_update(&db, 5) == 0 && db.ebr.nlimbo == 2 && g_nfreed == before);   /* 아무것도 못 푼다 */
    CHECK(!cfg_synchronize(&db, 50));                        /* 리더가 있으면 상한까지 기다리고 포기 */
    cfg_read_end(&db, 0);                                    /* 멈춘 리더가 나간다 */
    CHECK(ebr_try_advance(&db.ebr) && ebr_reclaim(&db.ebr) == 1 && was_freed(3) && !was_freed(4));
    CHECK(ebr_blocker(&db.ebr) == 1);                        /* 이제 리더 1(옛 epoch)이 막는다 */
    cfg_read_end(&db, 1);
    CHECK(ebr_blocker(&db.ebr) == -1);
    CHECK(cfg_synchronize(&db, 1000) && db.ebr.nlimbo == 0 && was_freed(4));
    /* 멈춘 리더 + 갱신 폭주 → limbo 상한에서 역압 */
    const cfg_t *st = cfg_read_begin(&db, 0);
    int okn = 0, refused = 0;
    for (uint64_t v = 100; v < 100 + 2 * LIMBO_CAP; v++) { if (cfg_update(&db, v) == 0) okn++; else refused++; }
    CHECK(okn == LIMBO_CAP && refused == LIMBO_CAP && db.ebr.nlimbo == LIMBO_CAP);
    CHECK(atomic_load(&db.cur)->version == 100 + LIMBO_CAP - 1 && cfg_ok(st) && st->version == 5);
    CHECK(atomic_load(&g_live) == 1 + LIMBO_CAP);            /* 메모리 = 상한에서 멈춘다 */
    cfg_read_end(&db, 0);
    CHECK(cfg_update(&db, 1000) == 0 && cfg_synchronize(&db, 1000) && db.ebr.nlimbo == 0);
    CHECK(atomic_load(&g_live) == 1);
    cfgdb_destroy(&db);
    int unit_b = FAILS() - unit_a;

    /* ---------- Part A/B 스트레스 ---------- */
    if (unit_a == 0 && unit_b == 0) {
        long live0 = atomic_load(&g_live), freed0 = g_nfreed;
        cfgdb_init(&g_db, A_READERS);
        pthread_t th[A_READERS + 1];
        for (int i = 0; i < A_READERS; i++) pthread_create(&th[i], NULL, a_reader, (void *)(intptr_t)i);
        pthread_create(&th[A_READERS], NULL, a_writer, NULL);
        for (int i = 0; i <= A_READERS; i++) pthread_join(th[i], NULL);
        CHECK(atomic_load(&g_a_bad) == 0 && atomic_load(&g_a_backwards) == 0);   /* 깨진 테이블 0, 역행 0 */
        CHECK(atomic_load(&g_db.cur)->version == 1 + A_UPDATES);               /* 갱신 전부 반영 */
        CHECK(cfg_synchronize(&g_db, 1000) && g_db.ebr.nlimbo == 0);
        CHECK(g_nfreed - freed0 == (long)A_UPDATES && atomic_load(&g_live) - live0 == 1);   /* 새 것 + 해제 정확히 */
        printf("  (A stress: %lu reads, %lu refused updates)\n", atomic_load(&g_a_reads), atomic_load(&g_a_refused));
        cfgdb_destroy(&g_db);
    } else skip(4, "Part A/B");

    /* ---------- Part C 단위: 정적 풀 + QSBR, 강제 스케줄 ---------- */
    int fails_before_c = FAILS();
    qsbr_t q;
    qsbr_init(&q, 3);
    const cfg_t *t0 = qsbr_get(&q);                          /* 태스크 0 이 v1 을 쥐고 계산 중 */
    CHECK(qsbr_publish(&q, 2) == 0 && qsbr_get(&q)->version == 2 && atomic_load(&q.gp) == 2);
    CHECK(qsbr_poll(&q) == 0);                               /* 아무도 루프 맨 위를 안 지났다 */
    CHECK(qsbr_publish(&q, 3) == 0 && qsbr_publish(&q, 4) == -1);   /* 버퍼 3 개: LIVE 1 + RETIRED 2 → 꽉 참 */
    CHECK(cfg_ok(t0) && t0->version == 1);                   /* 쥔 버퍼는 덮어쓰지 않았다 */
    qsbr_quiescent(&q, 0); qsbr_quiescent(&q, 1);
    CHECK(qsbr_poll(&q) == 0);                               /* 태스크 2 가 아직 */
    qsbr_quiescent(&q, 2);
    CHECK(qsbr_poll(&q) == 2);                               /* 셋 다 gp=3 이후를 지남 → v1, v2 버퍼 반환 */
    CHECK(qsbr_publish(&q, 4) == 0 && qsbr_get(&q)->version == 4 && cfg_ok(qsbr_get(&q)));
    qsbr_quiescent(&q, 0); qsbr_quiescent(&q, 1);            /* 태스크 2 는 v3 시절에 멈춤 */
    CHECK(qsbr_publish(&q, 5) == 0 && qsbr_publish(&q, 6) == -1);   /* v3 버퍼는 태스크 2 때문에 묶임 */
    int unit_c = FAILS() - fails_before_c;

    if (unit_c == 0) {
        qsbr_init(&g_q, NTASK);
        pthread_t th[NTASK + 1];
        for (int t = 0; t < NTASK; t++) pthread_create(&th[t], NULL, c_task, (void *)(intptr_t)t);
        pthread_create(&th[NTASK], NULL, c_writer, NULL);
        for (int t = 0; t <= NTASK; t++) pthread_join(th[t], NULL);
        CHECK(atomic_load(&g_c_published) == C_PUBLISH && qsbr_get(&g_q)->version == 1 + C_PUBLISH);
        CHECK(atomic_load(&g_c_bad) == 0);                   /* 재사용 중인 버퍼를 읽은 적 없음 */
        CHECK(atomic_load(&g_c_backwards) == 0);
    } else skip(3, "Part C");

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
