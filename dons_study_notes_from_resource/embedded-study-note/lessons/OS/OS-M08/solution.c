/* OS-M08 C11 atomics 동기화 패턴: 메시지 패싱·DCLP
 *
 * 호스트 모델: pthread 하나 = 코어 하나 (듀얼 코어 MCU, Cortex-A + M 메일박스, SMP RTOS 태스크).
 *   Part A  메시지 패싱(MP): payload 4필드 + flag. release store 로 게시, acquire load 로 받기.
 *           받은 payload 를 OK / STALE(통째로 옛것) / TORN(섞임) 으로 분류하는 검사기 포함.
 *   Part B  지연 초기화: double-checked locking (acquire 빠른 경로 + 락 안 release 게시) 과 pthread_once.
 *   Part C  드라이버 1회 초기화 상태 기계 UNINIT → BUSY → READY (CAS, 락 없음):
 *           ISR 에서도 부를 수 있는 "기다리지 않는" try_open, 프로브 실패 시 재시도 허용.
 *
 * 체크 테스트는 "올바른 구현이면 매 실행 항상 성립하는 것" 만 본다 (재배치 0 건, init 정확히 1 번,
 * 프로브 횟수 정확히 fail_first + 1, 프로브 겹침 0). relaxed 로 실제로 깨지는 모습은 demo_*.c.
 * 각 Part 의 스트레스는 단일 스레드 단위 테스트가 모두 통과해야 실행한다 (시작 코드가 멈추지 않게).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

_Static_assert(ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_POINTER_LOCK_FREE == 2, "need lock-free int/pointer atomics");

static inline void cpu_relax(void) {
#if defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield" ::: "memory");
#elif defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pause" ::: "memory");
#endif
}

/* ======================= Part A — 메시지 패싱 ======================= */
/* payload 는 일반(비원자) 필드. flag 의 release/acquire 가 "payload 다 썼음" 을 함께 넘긴다. */
typedef struct { uint32_t seq, a, b, c; } payload_t;
typedef struct {
    _Alignas(64) payload_t pl;       /* payload 한 캐시 라인 */
    _Alignas(64) atomic_uint flag;   /* flag 는 다른 라인 (보통의 메일박스 레지스터/공유 SRAM 배치) */
} slot_t;

/* 라운드 값 v 로 만든 payload. v == 0 이면 전부 0 (= 아직 아무도 안 쓴 칸). */
static payload_t payload_make(uint32_t v) {
    payload_t p;
    p.seq = v;
    p.a = v * 0x9E3779B1u;
    p.b = (v << 13) | (v >> 19);
    p.c = p.seq ^ p.a ^ p.b;
    return p;
}

typedef enum { MP_OK, MP_STALE, MP_TORN } mp_result_t;
/* 받은 payload 판정: 이번 라운드 v 로 온전하면 OK, 다른 한 라운드로 온전하면 STALE, 섞였으면 TORN. */
static mp_result_t mp_classify(const payload_t *p, uint32_t v) {
    payload_t want = payload_make(p->seq);              /* seq 가 말하는 라운드로 다시 만들어 본다 */
    if (p->a != want.a || p->b != want.b || p->c != want.c) return MP_TORN;
    return p->seq == v ? MP_OK : MP_STALE;
}

/* 생산자: payload 를 다 쓴 "뒤에" flag = v 를 게시. */
static void mp_post(slot_t *s, uint32_t v) {
    s->pl = payload_make(v);                                         /* 1) 데이터 (일반 쓰기) */
    atomic_store_explicit(&s->flag, v, memory_order_release);        /* 2) 게시: 위 쓰기가 먼저 보이게 */
}
/* 소비자: flag == v 이면 payload 를 *out 에 복사하고 true. 아직이면 false (기다리지 않는다). */
static bool mp_take(slot_t *s, uint32_t v, payload_t *out) {
    if (atomic_load_explicit(&s->flag, memory_order_acquire) != v) return false;   /* 1) 확인 */
    *out = s->pl;                                                    /* 2) 그다음에 읽기 */
    return true;
}

/* ======================= Part B — 지연 초기화 (DCLP / pthread_once) ======================= */
#define TMAGIC 0xC0FFEE11u
#define NTAB 64
typedef struct { uint32_t magic; uint32_t tab[NTAB]; uint32_t sum; } table_t;
static atomic_int g_table_inits;                 /* table_build 호출 횟수 (테스트용) */
static void table_build(table_t *t) {            /* "무거운" 초기화: CRC 표, 보정 계수 표 ... */
    atomic_fetch_add_explicit(&g_table_inits, 1, memory_order_relaxed);
    uint32_t s = 0;
    for (int i = 0; i < NTAB; i++) { t->tab[i] = (uint32_t)i * 2654435761u; s += t->tab[i]; }
    t->sum = s;
    t->magic = TMAGIC;
}
static bool table_ok(const table_t *t) {
    if (!t || t->magic != TMAGIC) return false;
    uint32_t s = 0;
    for (int i = 0; i < NTAB; i++) s += t->tab[i];
    return s == t->sum;
}

typedef struct {
    _Atomic(table_t *) p;            /* NULL = 아직. 게시된 뒤에는 바뀌지 않는다 */
    pthread_mutex_t mu;              /* 느린 경로(처음 한 번)만 잡는다 */
    table_t obj;                     /* 실제 저장소 (정적 할당 — 펌웨어엔 malloc 이 없을 수 있다) */
} lazy_t;
#define LAZY_INIT { NULL, PTHREAD_MUTEX_INITIALIZER, { 0, { 0 }, 0 } }

/* double-checked locking: 빠른 경로는 락 없이 load 1 번. 처음 한 번만 락 + 초기화. */
static const table_t *lazy_get(lazy_t *z) {
    table_t *p = atomic_load_explicit(&z->p, memory_order_acquire);  /* 빠른 경로: acquire */
    if (p) return p;
    pthread_mutex_lock(&z->mu);
    p = atomic_load_explicit(&z->p, memory_order_relaxed);           /* 락 안: 락이 순서를 준다 */
    if (!p) {
        table_build(&z->obj);
        p = &z->obj;
        atomic_store_explicit(&z->p, p, memory_order_release);       /* 게시: 내용 다 쓴 뒤 */
    }
    pthread_mutex_unlock(&z->mu);
    return p;
}

/* pthread_once 버전. macOS 에는 <threads.h>(C11 call_once) 가 없다 → POSIX pthread_once.
 * 테스트가 라운드마다 "새 부팅" 을 흉내 내도록 once 객체를 여러 개 둔다 (PTHREAD_ONCE_INIT 로만 초기화 가능).
 * 0..62 = 스트레스 라운드, 63 = 단위 테스트. */
#define ONCE_SLOTS 64
#define O1 PTHREAD_ONCE_INIT
#define O4 O1, O1, O1, O1
#define O16 O4, O4, O4, O4
static pthread_once_t g_once[ONCE_SLOTS] = { O16, O16, O16, O16 };
static table_t g_once_obj[ONCE_SLOTS];
static int g_once_round;                         /* 라운드 사이(워커가 모두 쉬는 동안)에만 바뀐다 */
static void once_init(void) { table_build(&g_once_obj[g_once_round]); }
static const table_t *once_get(void) {
    pthread_once(&g_once[g_once_round], once_init);   /* 첫 호출자만 once_init, 나머지는 끝날 때까지 대기 */
    return &g_once_obj[g_once_round];
}

/* ======================= Part C — 드라이버 1회 초기화 상태 기계 ======================= */
enum { DRV_UNINIT = 0, DRV_BUSY = 1, DRV_READY = 2 };
typedef enum { DRV_OK, DRV_EBUSY, DRV_EPROBE } drv_status_t;
typedef struct {
    atomic_uint state;               /* UNINIT → BUSY → READY (실패하면 BUSY → UNINIT) */
    uint32_t chip_id, calib[4];      /* 프로브 결과 (일반 필드, state 의 release/acquire 로 보호) */
    /* 가짜 하드웨어 · 테스트 계측 */
    int fail_first;                  /* 처음 fail_first 번의 프로브는 실패 (센서가 아직 전원 안정 전) */
    atomic_int probe_calls, in_probe, overlap;
} drv_t;

/* 가짜 I2C 프로브: 실패하면 반쯤 쓴 쓰레기를 남기고 -1 (실제 드라이버도 흔히 그렇다). */
static int drv_probe(drv_t *d) {
    if (atomic_fetch_add_explicit(&d->in_probe, 1, memory_order_relaxed) != 0)
        atomic_fetch_add_explicit(&d->overlap, 1, memory_order_relaxed);        /* 두 명이 동시에 프로브? */
    int n = atomic_fetch_add_explicit(&d->probe_calls, 1, memory_order_relaxed);
    int rc;
    if (n < d->fail_first) { d->chip_id = 0xDEADu; d->calib[0] = 1u; rc = -1; }
    else {
        d->chip_id = 0x6A;                                   /* WHO_AM_I */
        for (int i = 0; i < 4; i++) d->calib[i] = 1000u + (uint32_t)i * 7u;
        rc = 0;
    }
    atomic_fetch_sub_explicit(&d->in_probe, 1, memory_order_relaxed);
    return rc;
}
static bool drv_fields_ok(const drv_t *d) {
    if (d->chip_id != 0x6Au) return false;
    for (int i = 0; i < 4; i++) if (d->calib[i] != 1000u + (uint32_t)i * 7u) return false;
    return true;
}

/* 기다리지 않는 열기 (ISR · 스핀락 보유 중 · 스케줄러 시작 전에도 호출 가능).
 *   READY  → DRV_OK
 *   UNINIT → CAS 로 BUSY 를 차지한 한 명만 프로브. 성공 → READY 게시, DRV_OK / 실패 → UNINIT 로 되돌리고 DRV_EPROBE
 *   BUSY   → DRV_EBUSY (다른 누군가 프로브 중) */
static drv_status_t drv_try_open(drv_t *d) {
    unsigned s = atomic_load_explicit(&d->state, memory_order_acquire);
    if (s == DRV_READY) return DRV_OK;
    if (s == DRV_BUSY) return DRV_EBUSY;
    if (!atomic_compare_exchange_strong_explicit(&d->state, &s, DRV_BUSY,
                                                 memory_order_acquire, memory_order_acquire))
        return s == DRV_READY ? DRV_OK : DRV_EBUSY;          /* 그 사이 누가 바꿨다: s 에 현재 값 */
    if (drv_probe(d) == 0) {
        atomic_store_explicit(&d->state, DRV_READY, memory_order_release);   /* 필드 다 쓴 뒤 게시 */
        return DRV_OK;
    }
    atomic_store_explicit(&d->state, DRV_UNINIT, memory_order_release);      /* 다음 시도자에게 넘김 */
    return DRV_EPROBE;
}
/* 스레드 문맥용: BUSY 동안은 최대 max_spins 번 쉬며 다시 시도. 프로브 실패는 그대로 돌려준다. */
static drv_status_t drv_open(drv_t *d, unsigned max_spins) {
    for (unsigned i = 0;; i++) {
        drv_status_t st = drv_try_open(d);
        if (st != DRV_EBUSY || i >= max_spins) return st;
        cpu_relax();
    }
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
static uint32_t xorshift32(uint32_t *s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }

/* --- Part A 스트레스: 생산자 1 + 소비자 1, NSLOT 칸을 ROUNDS 라운드 재사용 --- */
#define NSLOT 1024u
#define MP_ROUNDS 300u
static slot_t g_slots[NSLOT];
static atomic_uint g_mp_go, g_mp_wdone;
static void *mp_writer(void *arg) {
    (void)arg;
    for (unsigned k = 1; k <= MP_ROUNDS; k++) {
        while (atomic_load_explicit(&g_mp_go, memory_order_acquire) != k) cpu_relax();
        for (unsigned i = 0; i < NSLOT; i++) mp_post(&g_slots[i], (k - 1u) * NSLOT + i + 1u);
        atomic_store_explicit(&g_mp_wdone, k, memory_order_release);
    }
    return NULL;
}

/* --- Part B 스트레스: 워커 NT 개가 라운드마다 "처음부터" 동시에 get() --- */
#define NT 6
#define DCLP_ROUNDS 3000u
static lazy_t g_lazy = LAZY_INIT;
static atomic_uint g_round_gen, g_round_done;
static const table_t *g_got[NT];                 /* 워커 i 가 이번 라운드에 받은 포인터 (i 만 씀) */
static int g_use_once;
static void *b_worker(void *arg) {
    int id = (int)(intptr_t)arg;
    uint32_t seed = 0x9E3779B9u ^ (uint32_t)id;
    unsigned rounds = g_use_once ? ONCE_SLOTS - 1u : DCLP_ROUNDS;
    for (unsigned r = 1; r <= rounds; r++) {
        while (atomic_load(&g_round_gen) != r) cpu_relax();
        for (volatile uint32_t d = xorshift32(&seed) % 300u; d; d--) { }   /* 도착 시각 흩뜨리기 */
        g_got[id] = g_use_once ? once_get() : lazy_get(&g_lazy);
        atomic_fetch_add(&g_round_done, 1u);
    }
    return NULL;
}
static void run_b(int use_once, unsigned *bad_rounds, unsigned *bad_reads) {
    pthread_t th[NT];
    unsigned rounds = use_once ? ONCE_SLOTS - 1u : DCLP_ROUNDS;
    g_use_once = use_once;
    atomic_store(&g_round_gen, 0u);
    *bad_rounds = *bad_reads = 0;
    for (intptr_t i = 0; i < NT; i++) pthread_create(&th[i], NULL, b_worker, (void *)i);
    for (unsigned r = 1; r <= rounds; r++) {
        if (use_once) g_once_round = (int)r - 1;
        else { memset(&g_lazy.obj, 0, sizeof g_lazy.obj); atomic_store(&g_lazy.p, NULL); }   /* 새 부팅 */
        atomic_store(&g_table_inits, 0);
        atomic_store(&g_round_done, 0u);
        atomic_store(&g_round_gen, r);                          /* seq_cst: 위 초기화가 워커에게 보인다 */
        while (atomic_load(&g_round_done) != NT) cpu_relax();
        if (atomic_load(&g_table_inits) != 1) (*bad_rounds)++;
        for (int i = 0; i < NT; i++)
            if (!table_ok(g_got[i]) || g_got[i] != g_got[0]) (*bad_reads)++;
    }
    for (int i = 0; i < NT; i++) pthread_join(th[i], NULL);
}

/* --- Part C 스트레스: 스레드 NT-1 개는 drv_open, 1 개는 "ISR" 처럼 drv_try_open 만 --- */
#define DRV_ROUNDS 2000u
static drv_t g_drv;
static atomic_uint g_c_gen, g_c_done, g_c_badfields, g_c_isr_busy;
static void *c_worker(void *arg) {
    int isr = (int)(intptr_t)arg == 0;
    for (unsigned r = 1; r <= DRV_ROUNDS; r++) {
        while (atomic_load(&g_c_gen) != r) cpu_relax();
        drv_status_t st = DRV_EBUSY;
        for (int tries = 0; tries < 1000000; tries++) {           /* 안전장치: 영원히 안 열리면 포기 */
            st = isr ? drv_try_open(&g_drv) : drv_open(&g_drv, 100000u);
            if (st == DRV_OK) break;
            if (isr && st == DRV_EBUSY) atomic_fetch_add(&g_c_isr_busy, 1u);
            cpu_relax();
        }
        if (st != DRV_OK || !drv_fields_ok(&g_drv)) atomic_fetch_add(&g_c_badfields, 1u);
        atomic_fetch_add(&g_c_done, 1u);
    }
    return NULL;
}
static void drv_reset(drv_t *d, int fail_first) {
    atomic_store(&d->state, DRV_UNINIT);
    d->chip_id = 0; memset(d->calib, 0, sizeof d->calib);
    d->fail_first = fail_first;
    atomic_store(&d->probe_calls, 0); atomic_store(&d->in_probe, 0); atomic_store(&d->overlap, 0);
}

int main(void) {
    /* ---------- Part A: 단위 ---------- */
    int fa = FAILS();
    payload_t p = payload_make(7u), z = payload_make(0u), q;
    CHECK(z.seq == 0u && z.a == 0u && z.b == 0u && z.c == 0u);
    CHECK(mp_classify(&p, 7u) == MP_OK);
    CHECK(mp_classify(&p, 8u) == MP_STALE);                    /* 온전하지만 옛 라운드 */
    CHECK(mp_classify(&z, 8u) == MP_STALE);                    /* 아직 아무도 안 쓴 칸 */
    q = p; q.b = payload_make(8u).b;
    CHECK(mp_classify(&q, 8u) == MP_TORN && mp_classify(&q, 7u) == MP_TORN);   /* 필드가 섞였다 */
    q = payload_make(8u); q.seq = 7u;
    CHECK(mp_classify(&q, 8u) == MP_TORN);                     /* seq 만 옛것 */
    memset(&g_slots[0], 0, sizeof g_slots[0]);
    CHECK(!mp_take(&g_slots[0], 5u, &q));                       /* 아직 게시 전 */
    mp_post(&g_slots[0], 5u);
    CHECK(mp_take(&g_slots[0], 5u, &q) && mp_classify(&q, 5u) == MP_OK);
    CHECK(!mp_take(&g_slots[0], 6u, &q));                       /* 다른 라운드 값은 아직 */
    CHECK(atomic_load(&g_slots[0].flag) == 5u);
    /* ---------- Part A: 스트레스 ---------- */
    if (FAILS() == fa) {
        memset(g_slots, 0, sizeof g_slots);
        pthread_t wt;
        pthread_create(&wt, NULL, mp_writer, NULL);
        unsigned long ok = 0, stale = 0, torn = 0, missing = 0;
        for (unsigned k = 1; k <= MP_ROUNDS; k++) {
            atomic_store_explicit(&g_mp_go, k, memory_order_release);
            for (unsigned i = 0; i < NSLOT; i++) {
                uint32_t v = (k - 1u) * NSLOT + i + 1u;
                unsigned idle = 0;
                while (!mp_take(&g_slots[i], v, &q) && ++idle < 100000000u) cpu_relax();
                if (idle >= 100000000u) { missing++; continue; }
                mp_result_t r = mp_classify(&q, v);
                ok += r == MP_OK; stale += r == MP_STALE; torn += r == MP_TORN;
            }
            while (atomic_load_explicit(&g_mp_wdone, memory_order_acquire) != k) cpu_relax();
        }
        pthread_join(wt, NULL);
        printf("Part A stress: %u messages  ok=%lu stale=%lu torn=%lu\n", NSLOT * MP_ROUNDS, ok, stale, torn);
        CHECK(missing == 0u && ok == (unsigned long)NSLOT * MP_ROUNDS);   /* 전부 도착, 전부 온전 */
        CHECK(stale == 0u && torn == 0u);                                 /* acquire 뒤엔 옛것·섞인 것 없음 */
    } else skip(2, "Part A");

    /* ---------- Part B: 단위 ---------- */
    int fb = FAILS();
    static lazy_t lz = LAZY_INIT;
    atomic_store(&g_table_inits, 0);
    const table_t *t1 = lazy_get(&lz), *t2 = lazy_get(&lz), *t3 = lazy_get(&lz);
    CHECK(table_ok(t1) && t1 == t2 && t2 == t3 && t1 == &lz.obj);
    CHECK(atomic_load(&g_table_inits) == 1);                   /* 세 번 불러도 초기화는 한 번 */
    g_once_round = ONCE_SLOTS - 1;
    atomic_store(&g_table_inits, 0);
    t1 = once_get(); t2 = once_get();
    CHECK(table_ok(t1) && t1 == t2 && atomic_load(&g_table_inits) == 1);
    /* ---------- Part B: 스트레스 ---------- */
    if (FAILS() == fb) {
        unsigned br, bd;
        run_b(0, &br, &bd);
        printf("Part B stress (DCLP): %u rounds x %d threads  rounds_with_init!=1=%u bad_reads=%u\n",
               DCLP_ROUNDS, NT, br, bd);
        CHECK(br == 0u);                                        /* 매 라운드 초기화 정확히 1 번 */
        CHECK(bd == 0u);                                        /* 모든 스레드가 같은, 완성된 객체 */
        run_b(1, &br, &bd);
        printf("Part B stress (pthread_once): %d rounds x %d threads  rounds_with_init!=1=%u bad_reads=%u\n",
               ONCE_SLOTS - 1, NT, br, bd);
        CHECK(br == 0u && bd == 0u);                            /* pthread_once 도 같은 보장 */
    } else skip(3, "Part B");

    /* ---------- Part C: 단위 ---------- */
    int fc = FAILS();
    static drv_t d1;
    drv_reset(&d1, 0);
    CHECK(drv_try_open(&d1) == DRV_OK && drv_fields_ok(&d1));
    CHECK(drv_try_open(&d1) == DRV_OK && atomic_load(&d1.probe_calls) == 1);   /* 두 번째는 프로브 안 함 */
    drv_reset(&d1, 2);
    CHECK(drv_try_open(&d1) == DRV_EPROBE && atomic_load(&d1.state) == DRV_UNINIT);
    CHECK(drv_open(&d1, 10u) == DRV_EPROBE);                   /* 프로브 실패는 기다려도 그대로 */
    CHECK(drv_try_open(&d1) == DRV_OK && atomic_load(&d1.probe_calls) == 3 && drv_fields_ok(&d1));
    drv_reset(&d1, 0);
    atomic_store(&d1.state, DRV_BUSY);                         /* 다른 코어가 프로브 중인 순간에 ISR 이 들어왔다 */
    CHECK(drv_try_open(&d1) == DRV_EBUSY && atomic_load(&d1.probe_calls) == 0);  /* 즉시 돌아온다 */
    CHECK(drv_open(&d1, 1000u) == DRV_EBUSY);                  /* 스레드 문맥도 상한이 있으면 끝난다 */
    /* ---------- Part C: 스트레스 ---------- */
    if (FAILS() == fc) {
        pthread_t th[NT];
        for (intptr_t i = 0; i < NT; i++) pthread_create(&th[i], NULL, c_worker, (void *)i);
        unsigned bad_probe = 0, overlaps = 0;
        for (unsigned r = 1; r <= DRV_ROUNDS; r++) {
            drv_reset(&g_drv, (int)(r % 4u));                  /* 라운드마다 실패 0~3 번 */
            atomic_store(&g_c_done, 0u);
            atomic_store(&g_c_gen, r);
            while (atomic_load(&g_c_done) != NT) cpu_relax();
            if (atomic_load(&g_drv.probe_calls) != (int)(r % 4u) + 1) bad_probe++;
            overlaps += (unsigned)atomic_load(&g_drv.overlap);
        }
        for (int i = 0; i < NT; i++) pthread_join(th[i], NULL);
        printf("Part C stress: %u rounds x %d threads  bad_probe_count=%u overlaps=%u bad=%u isr_ebusy=%u\n",
               DRV_ROUNDS, NT, bad_probe, overlaps, atomic_load(&g_c_badfields), atomic_load(&g_c_isr_busy));
        CHECK(bad_probe == 0u);                                 /* 프로브 = 실패 횟수 + 성공 1 번, 정확히 */
        CHECK(overlaps == 0u);                                  /* 동시에 두 명이 프로브한 적 없음 */
        CHECK(atomic_load(&g_c_badfields) == 0u);               /* OK 를 받은 모두가 완성된 필드를 봤다 */
    } else skip(3, "Part C");

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
