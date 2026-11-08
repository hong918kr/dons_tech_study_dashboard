/* TOOL-M09 ThreadSanitizer로 데이터 레이스 찾기
 *
 * demo_*.c 로 TSan 리포트를 먼저 읽고(버그 3종), 여기서는 "고친 버전" 을 만든다.
 *   Part A  공유 카운터: 뮤텍스로 묶은 통계 쌍 + atomic 카운터(증가 / 읽고-비우기)
 *   Part B  락 없이 읽던 플래그 → atomic_bool release/acquire 메일박스 (타임아웃 있는 대기)
 *           락 순서 역전 → rank 순서로 두 락 잡기 (lock_pair / unlock_pair)
 *   Part C  텔레메트리 허브: 설정 락(rank 1) → 로그 락(rank 2) 중첩, 채널별 atomic 통계, 정지 플래그
 *
 * 체크는 "올바른 구현이면 매 실행 항상 성립" 하는 것만 본다. 교착 위험이 있는 스트레스(Part B 락,
 * Part C)는 단일 스레드 단위 테스트가 모두 통과해야 실행한다 → 시작 코드는 멈추지 않고 FAIL 한다.
 * 이 하네스의 rlock_t 는 "rank 가 증가하는 순서로만 잠근다" 규칙을 어기면 세는 미니 검사기다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so
 * TSan : cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_t && ./so_t   (보고 0개)
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* ============ 하네스: rank 붙은 뮤텍스 (락 순서 규칙 검사기, TODO 아님) ============ */
typedef struct { pthread_mutex_t m; unsigned rank; } rlock_t;
#define RLOCK_INIT(r) { PTHREAD_MUTEX_INITIALIZER, (r) }
#define MAX_HELD 8
static _Thread_local const rlock_t *t_held[MAX_HELD];   /* 이 스레드가 지금 쥔 락들 */
static _Thread_local int t_nheld;
static atomic_uint g_order_violations;   /* rank 가 같거나 높은 락을 쥔 채 잠금 = 순서 규칙 위반 */
static atomic_uint g_relock;             /* 이미 쥔 락을 또 잠그려 함 (그대로 두면 자기 교착) */
static atomic_uint g_bad_unlock;         /* 쥐지 않은 락을 해제하려 함 */

static bool rl_held(const rlock_t *l) {
    for (int i = 0; i < t_nheld; i++) if (t_held[i] == l) return true;
    return false;
}
static void rl_lock(rlock_t *l) {
    if (rl_held(l)) { atomic_fetch_add(&g_relock, 1u); return; }   /* 멈추는 대신 세고 돌아간다 */
    for (int i = 0; i < t_nheld; i++)
        if (t_held[i]->rank >= l->rank) { atomic_fetch_add(&g_order_violations, 1u); break; }
    pthread_mutex_lock(&l->m);
    if (t_nheld < MAX_HELD) t_held[t_nheld++] = l;
}
static void rl_unlock(rlock_t *l) {
    for (int i = 0; i < t_nheld; i++)
        if (t_held[i] == l) {
            t_held[i] = t_held[--t_nheld];
            pthread_mutex_unlock(&l->m);
            return;
        }
    atomic_fetch_add(&g_bad_unlock, 1u);
}

/* ======================= Part A — 공유 카운터 ======================= */
#define SAMPLE_BYTES 64u
typedef struct {
    pthread_mutex_t lock;
    uint64_t samples;            /* 불변식: bytes == samples * SAMPLE_BYTES (둘이 항상 같이 움직인다) */
    uint64_t bytes;
} stats_t;

/* 샘플 하나 기록: samples += 1, bytes += SAMPLE_BYTES 를 "한 덩어리로". */
static void stats_record(stats_t *s) {
    pthread_mutex_lock(&s->lock);
    s->samples++;
    s->bytes += SAMPLE_BYTES;
    pthread_mutex_unlock(&s->lock);
}
/* 두 값을 일관된 쌍으로 읽는다 (다른 스레드가 기록 중이어도 찢어지지 않게). */
static void stats_snapshot(stats_t *s, uint64_t *samples, uint64_t *bytes) {
    pthread_mutex_lock(&s->lock);
    *samples = s->samples;
    *bytes = s->bytes;
    pthread_mutex_unlock(&s->lock);
}
/* 단독 카운터 +1 (다른 데이터를 넘기지 않는 순수 통계). */
static void hits_inc(atomic_ullong *c) {
    atomic_fetch_add_explicit(c, 1u, memory_order_relaxed);
}
/* 텔레메트리: 지금까지 쌓인 값을 가져가고 0 으로 되돌린다. 그 사이 증가를 하나도 잃으면 안 된다. */
static uint64_t hits_take(atomic_ullong *c) {
    return atomic_exchange_explicit(c, 0u, memory_order_relaxed);
}

/* ======================= Part B — 플래그 메일박스 / 락 순서 ======================= */
typedef struct { int32_t gain, offset; uint32_t crc; } calib_t;
typedef struct {
    calib_t data;                /* 일반 데이터 — ready 의 release/acquire 로 보호 */
    atomic_bool ready;
} mailbox_t;

static uint32_t calib_crc(const calib_t *c) { return (uint32_t)c->gain * 2654435761u ^ (uint32_t)c->offset * 40503u ^ 0x5A5Au; }

/* 보정값을 쓰고 ready 를 세운다 (한 번만 쓰는 메일박스). */
static void mb_post(mailbox_t *mb, const calib_t *c) {
    mb->data = *c;                                                     /* 1) 데이터 */
    atomic_store_explicit(&mb->ready, true, memory_order_release);     /* 2) 게시 */
}
/* ready 가 설 때까지 기다렸다가 *out 으로 복사, true. timeout_ms 가 지나면 false. */
static bool mb_wait(mailbox_t *mb, calib_t *out, unsigned timeout_ms) {
    uint64_t deadline = now_ms() + timeout_ms;
    while (!atomic_load_explicit(&mb->ready, memory_order_acquire)) {
        if (now_ms() > deadline) return false;
        sched_yield();
    }
    *out = mb->data;
    return true;
}

typedef struct { rlock_t lk; long credits; } pool_t;

/* 두 락을 교착 없이 잡는다: rank 가 작은 쪽 먼저. a == b 면 한 번만. */
static void lock_pair(rlock_t *a, rlock_t *b) {
    if (a == b) { rl_lock(a); return; }
    if (a->rank > b->rank) { rlock_t *t = a; a = b; b = t; }
    rl_lock(a);
    rl_lock(b);
}
/* lock_pair 로 잡은 두 락을 푼다. a == b 면 한 번만. */
static void unlock_pair(rlock_t *a, rlock_t *b) {
    if (a == b) { rl_unlock(a); return; }
    rl_unlock(a);
    rl_unlock(b);
}

static void pool_move(pool_t *from, pool_t *to, long n) {     /* 하네스: lock_pair 사용자 */
    lock_pair(&from->lk, &to->lk);
    from->credits -= n;
    to->credits += n;
    unlock_pair(&from->lk, &to->lk);
}

/* ======================= Part C — 텔레메트리 허브 ======================= */
#define NCH     4
#define LOG_CAP 70000u
enum { REC_SAMPLE = 0, REC_CFG = 1 };
typedef struct { uint32_t version; int32_t gain, offset; } cfg_t;
typedef struct { uint32_t version, kind_ch; int32_t raw, value; } rec_t;   /* CFG 레코드: raw=gain, value=offset */
typedef struct {
    rlock_t cfg_lk;              /* rank 1: cfg 보호 */
    cfg_t cfg;
    rlock_t log_lk;              /* rank 2: log, log_n 보호 */
    rec_t log[LOG_CAP];
    uint32_t log_n, log_drop;
    atomic_ullong per_ch[NCH];   /* 채널별 샘플 수 (relaxed 통계) */
    atomic_bool stop;            /* 정지 요청 */
} hub_t;

/* 하네스 제공: log_lk 를 쥔 상태에서만 부른다 (unused: 시작 코드에선 아직 아무도 안 부름) */
__attribute__((unused)) static void log_append(hub_t *h, rec_t r) {
    if (h->log_n < LOG_CAP) h->log[h->log_n++] = r;
    else h->log_drop++;
}

/* 샘플 하나: 채널 통계 +1, 현재 설정으로 value = raw*gain + offset 을 계산해 로그에 남긴다.
 * 요구사항: 로그 안에서 버전이 절대 뒤로 가지 않는다 → 설정을 읽은 채로 로그에 써야 한다. */
static void hub_sample(hub_t *h, unsigned ch, int32_t raw) {
    atomic_fetch_add_explicit(&h->per_ch[ch], 1u, memory_order_relaxed);
    rl_lock(&h->cfg_lk);                                       /* rank 1 */
    rec_t r = { h->cfg.version, (uint32_t)REC_SAMPLE << 8 | ch, raw, raw * h->cfg.gain + h->cfg.offset };
    rl_lock(&h->log_lk);                                       /* rank 2 (1 을 쥔 채로) */
    log_append(h, r);
    rl_unlock(&h->log_lk);
    rl_unlock(&h->cfg_lk);
}
/* 설정 변경: version+1, gain/offset 교체, 그 경계에 CFG 레코드를 로그에 남긴다 (원자적으로). */
static void hub_reconfigure(hub_t *h, int32_t gain, int32_t offset) {
    lock_pair(&h->cfg_lk, &h->log_lk);                         /* 레거시 코드는 log → cfg 순서였다 */
    h->cfg.version++;
    h->cfg.gain = gain;
    h->cfg.offset = offset;
    log_append(h, (rec_t){ h->cfg.version, (uint32_t)REC_CFG << 8, gain, offset });
    unlock_pair(&h->cfg_lk, &h->log_lk);
}
static void hub_request_stop(hub_t *h) {
    atomic_store_explicit(&h->stop, true, memory_order_release);
}
static bool hub_should_stop(hub_t *h) {
    return atomic_load_explicit(&h->stop, memory_order_acquire);
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

#define NT 4
static void run_threads(void *(*fn)(void *), void *args, size_t stride, int n) {
    pthread_t t[8];
    for (int i = 0; i < n; i++) pthread_create(&t[i], NULL, fn, args ? (char *)args + (size_t)i * stride : NULL);
    for (int i = 0; i < n; i++) pthread_join(t[i], NULL);
}

/* --- Part A 스트레스 --- */
#define A_ITERS 200000
static stats_t g_stats = { PTHREAD_MUTEX_INITIALIZER, 0u, 0u };
static atomic_ullong g_hits;
static atomic_int g_writers_left;
static atomic_bool g_torn;
static void *stats_writer(void *arg) {
    (void)arg;
    for (int i = 0; i < A_ITERS; i++) { stats_record(&g_stats); hits_inc(&g_hits); }
    atomic_fetch_sub(&g_writers_left, 1);
    return NULL;
}
typedef struct { uint64_t taken; unsigned snaps; } reader_out_t;
static void *stats_reader(void *arg) {               /* 텔레메트리 스레드: 쓰는 중에 계속 읽고 비운다 */
    reader_out_t *o = arg;
    for (;;) {
        bool last = atomic_load(&g_writers_left) == 0;   /* 이걸 먼저 읽어야 마지막 take 가 전부를 본다 */
        uint64_t s = 0u, b = 0u;
        stats_snapshot(&g_stats, &s, &b);
        if (b != s * SAMPLE_BYTES) atomic_store(&g_torn, true);
        o->snaps++;
        o->taken += hits_take(&g_hits);
        if (last) break;
        sched_yield();
    }
    return NULL;
}

/* --- Part B 스트레스 --- */
#define B_ROUNDS 300
typedef struct { mailbox_t *mb; calib_t c; } post_arg_t;
static void *poster(void *arg) {
    post_arg_t *p = arg;
    for (volatile int spin = 0; spin < (int)(p->c.gain % 64) * 50; spin++) { }   /* 조금씩 다른 타이밍 */
    mb_post(p->mb, &p->c);
    return NULL;
}
#define B_MOVES 100000
static pool_t g_rx = { RLOCK_INIT(1u), 1000 };
static pool_t g_tx = { RLOCK_INIT(2u), 1000 };
static void *rx_to_tx(void *a) { (void)a; for (int i = 0; i < B_MOVES; i++) pool_move(&g_rx, &g_tx, 1); return NULL; }
static void *tx_to_rx(void *a) { (void)a; for (int i = 0; i < B_MOVES; i++) pool_move(&g_tx, &g_rx, 1); return NULL; }

/* --- Part C 스트레스 --- */
#define C_SAMPLERS 3
#define C_PER      20000
#define C_RECONF   150
static hub_t g_hub = { .cfg_lk = RLOCK_INIT(1u), .cfg = { 0u, 1, 0 }, .log_lk = RLOCK_INIT(2u) };
static void hub_reset(hub_t *h) {
    h->cfg = (cfg_t){ 0u, 1, 0 };
    h->log_n = h->log_drop = 0u;
    for (int i = 0; i < NCH; i++) atomic_store(&h->per_ch[i], 0u);
    atomic_store(&h->stop, false);
}
static void *sampler(void *arg) {
    unsigned id = (unsigned)(uintptr_t)arg;
    for (int k = 0; k < C_PER; k++) hub_sample(&g_hub, (unsigned)k % NCH, (int32_t)((k * 7 + (int)id) & 1023));
    return NULL;
}
static void *heartbeat(void *arg) {                  /* 정지 플래그만 보고 도는 스레드 */
    bool *stopped_by_flag = arg;
    uint64_t deadline = now_ms() + 10000u;
    while (!hub_should_stop(&g_hub)) {
        if (now_ms() > deadline) return NULL;          /* 플래그를 끝내 못 보면 false 로 남는다 */
        sched_yield();
    }
    *stopped_by_flag = true;
    return NULL;
}

int main(void) {
    /* ---------- 하네스 자체 점검: rank 검사기가 역순 잠금을 센다 ---------- */
    {
        rlock_t x = RLOCK_INIT(1u), y = RLOCK_INIT(2u);
        rl_lock(&y); rl_lock(&x);                             /* 일부러 rank 2 → 1 */
        rl_unlock(&x); rl_unlock(&y);
        CHECK(atomic_exchange(&g_order_violations, 0u) == 1u);
    }
    /* ---------- Part A: 단위 ---------- */
    stats_t s1 = { PTHREAD_MUTEX_INITIALIZER, 0u, 0u };
    uint64_t sn = 99u, by = 99u;
    stats_record(&s1); stats_record(&s1); stats_record(&s1);
    stats_snapshot(&s1, &sn, &by);
    CHECK(sn == 3u && by == 3u * SAMPLE_BYTES);
    atomic_ullong h1 = 0u;
    hits_inc(&h1); hits_inc(&h1);
    CHECK(atomic_load(&h1) == 2u);
    CHECK(hits_take(&h1) == 2u && atomic_load(&h1) == 0u);    /* 가져가면 0 */
    CHECK(hits_take(&h1) == 0u);
    /* ---------- Part A: 스트레스 (writer NT + reader 1) ---------- */
    {
        reader_out_t ro = { 0u, 0u };
        pthread_t rd;
        atomic_store(&g_writers_left, NT);
        pthread_create(&rd, NULL, stats_reader, &ro);
        run_threads(stats_writer, NULL, 0, NT);
        pthread_join(rd, NULL);
        uint64_t s = 0u, b = 0u;
        stats_snapshot(&g_stats, &s, &b);
        CHECK(s == (uint64_t)NT * A_ITERS && b == s * SAMPLE_BYTES);   /* 증가를 하나도 잃지 않음 */
        CHECK(!atomic_load(&g_torn));                                  /* 읽는 중에도 쌍이 찢어지지 않음 */
        CHECK(ro.taken == (uint64_t)NT * A_ITERS);                     /* 읽고-비우기 사이에 잃은 증가 없음 */
        printf("  [A] reader snapshots during run: %u\n", ro.snaps);
    }

    /* ---------- Part B: 메일박스 단위 ---------- */
    mailbox_t mb0 = { { 0, 0, 0u }, false };
    calib_t got = { 0, 0, 0u };
    CHECK(!mb_wait(&mb0, &got, 20u));                        /* 아무도 안 쓰면 타임아웃 */
    calib_t c0 = { 7, -3, 0u };
    c0.crc = calib_crc(&c0);
    mb_post(&mb0, &c0);
    CHECK(mb_wait(&mb0, &got, 20u) && got.gain == 7 && got.offset == -3 && got.crc == calib_crc(&got));
    /* ---------- Part B: 메일박스 스트레스 (매 라운드 새 스레드가 게시) ---------- */
    {
        int ok = 0;
        for (int r = 0; r < B_ROUNDS; r++) {
            mailbox_t mb = { { 0, 0, 0u }, false };
            post_arg_t pa = { &mb, { r, -r, 0u } };
            pa.c.crc = calib_crc(&pa.c);
            pthread_t t;
            pthread_create(&t, NULL, poster, &pa);
            calib_t out = { 0, 0, 0u };
            if (mb_wait(&mb, &out, 5000u) && out.gain == r && out.offset == -r && out.crc == calib_crc(&out)) ok++;
            pthread_join(t, NULL);
        }
        CHECK(ok == B_ROUNDS);                                /* 매번 온전한 보정값을 받음 */
    }
    /* ---------- Part B: lock_pair 단위 ---------- */
    int f1 = FAILS();
    rlock_t lo = RLOCK_INIT(1u), hi = RLOCK_INIT(2u);
    unsigned v0 = atomic_load(&g_order_violations);
    lock_pair(&hi, &lo);                                      /* 인자 순서와 상관없이 rank 순서로 */
    CHECK(rl_held(&lo) && rl_held(&hi));
    CHECK(atomic_load(&g_order_violations) == v0);
    unlock_pair(&hi, &lo);
    CHECK(!rl_held(&lo) && !rl_held(&hi));
    lock_pair(&lo, &lo);                                      /* 같은 락 두 번 → 한 번만 */
    CHECK(rl_held(&lo) && atomic_load(&g_relock) == 0u);
    unlock_pair(&lo, &lo);
    CHECK(!rl_held(&lo) && atomic_load(&g_bad_unlock) == 0u);
    /* ---------- Part B: 반대 방향 이동 스트레스 ---------- */
    if (FAILS() == f1) {
        pthread_t a, b;
        pthread_create(&a, NULL, rx_to_tx, NULL);
        pthread_create(&b, NULL, tx_to_rx, NULL);
        pthread_join(a, NULL);
        pthread_join(b, NULL);
        CHECK(g_rx.credits + g_tx.credits == 2000 && g_rx.credits == 1000);   /* 같은 수만큼 오갔다 */
        CHECK(atomic_load(&g_order_violations) == 0u);
    } else skip(2, "Part B lock order");

    /* ---------- Part C: 단위 ---------- */
    int f2 = FAILS();
    hub_reset(&g_hub);
    hub_sample(&g_hub, 2u, 10);
    CHECK(g_hub.log_n == 1u && g_hub.log[0].version == 0u && g_hub.log[0].kind_ch == 2u && g_hub.log[0].value == 10);
    hub_reconfigure(&g_hub, 3, -5);
    CHECK(g_hub.cfg.version == 1u && g_hub.log_n == 2u && g_hub.log[1].kind_ch == (uint32_t)REC_CFG << 8);
    hub_sample(&g_hub, 1u, 4);
    CHECK(g_hub.log_n == 3u && g_hub.log[2].version == 1u && g_hub.log[2].value == 7);
    CHECK(atomic_load(&g_hub.per_ch[1]) == 1u && atomic_load(&g_hub.per_ch[2]) == 1u);
    CHECK(atomic_load(&g_order_violations) == 0u && atomic_load(&g_relock) == 0u);
    CHECK(!hub_should_stop(&g_hub));
    hub_request_stop(&g_hub);
    CHECK(hub_should_stop(&g_hub));
    /* ---------- Part C: 스트레스 (샘플러 3 + heartbeat 1 + main 이 재설정) ---------- */
    if (FAILS() == f2) {
        hub_reset(&g_hub);
        bool stopped_by_flag = false;
        pthread_t smp[C_SAMPLERS], hb;
        pthread_create(&hb, NULL, heartbeat, &stopped_by_flag);
        for (int i = 0; i < C_SAMPLERS; i++) pthread_create(&smp[i], NULL, sampler, (void *)(uintptr_t)i);
        for (int r = 1; r <= C_RECONF; r++) { hub_reconfigure(&g_hub, r % 50 + 1, r - 75); sched_yield(); }
        for (int i = 0; i < C_SAMPLERS; i++) pthread_join(smp[i], NULL);
        hub_request_stop(&g_hub);
        pthread_join(hb, NULL);

        unsigned long long sum = 0;
        for (int i = 0; i < NCH; i++) sum += atomic_load(&g_hub.per_ch[i]);
        CHECK(sum == (unsigned long long)C_SAMPLERS * C_PER && atomic_load(&g_hub.per_ch[0]) == C_SAMPLERS * C_PER / NCH);
        CHECK(g_hub.log_n == C_SAMPLERS * C_PER + C_RECONF && g_hub.log_drop == 0u);
        uint32_t ver = 0u, bad = 0u, markers = 0u;            /* 로그를 처음부터 다시 계산해 본다 */
        int32_t gain = 1, off = 0;
        for (uint32_t i = 0; i < g_hub.log_n; i++) {
            const rec_t *r = &g_hub.log[i];
            if (r->kind_ch >> 8 == REC_CFG) {
                bad += r->version != ver + 1u;
                ver = r->version; gain = r->raw; off = r->value; markers++;
            } else {
                bad += r->version != ver || r->value != r->raw * gain + off;
            }
        }
        CHECK(bad == 0u && markers == C_RECONF);              /* 버전 역행 없음, 모든 값이 그 버전의 설정으로 계산됨 */
        CHECK(atomic_load(&g_order_violations) == 0u && atomic_load(&g_relock) == 0u && atomic_load(&g_bad_unlock) == 0u);
        CHECK(stopped_by_flag);                               /* 정지 플래그가 다른 스레드에 보였다 */
    } else skip(5, "Part C");

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
