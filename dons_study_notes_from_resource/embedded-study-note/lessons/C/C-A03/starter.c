/* C-A03 C11 Atomics: 플래그·카운터·acquire/release
 *
 * 호스트 모델: pthread 하나 = 코어 하나 (듀얼 코어 MCU 의 Core0/Core1, 또는 RTOS 태스크).
 *   Part A  atomic_flag 스핀락 / fetch_add 티켓 카운터 / CAS 루프 최대값(워터마크)
 *   Part B  1칸 채널: release-store 로 게시(publish), acquire-load 로 받기 — 양방향 모두
 *   Part C  센서 드라이버: 트리플 버퍼(atomic_exchange acq_rel) + 스핀락 설정 + relaxed 통계
 *
 * 체크 테스트는 "올바른 구현이면 매 실행 항상 성립하는 것" 만 검사한다 (최종 카운트 == N*K,
 * acquire 이후 데이터가 온전함). 레이스 재현·TSan 보고는 demo_*.c 에 있다.
 * 각 Part 의 멀티스레드 스트레스는 단일 스레드 단위 테스트가 모두 통과해야 실행한다
 * (시작 코드가 멈추거나 레이스를 일으키지 않게).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o st && ./st   (solution.c 도 같은 명령)
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 이 레슨은 atomic_uint 가 "항상 lock-free" 라고 가정한다 (아니면 내부 락 → ISR 에서 교착 가능). */
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "atomic_uint must always be lock-free");

/* 스핀 대기 중 한 번 쉬기: arm64 YIELD (Cortex-M 의 __YIELD()/__WFE() 자리), x86 PAUSE */
static inline void cpu_relax(void) {
#if defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield" ::: "memory");
#elif defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pause" ::: "memory");
#endif
}

/* ======================= Part A — 원자 부품 3종 ======================= */
typedef struct { atomic_flag f; } spinlock_t;
#define SPINLOCK_INIT { ATOMIC_FLAG_INIT }    /* atomic_flag 는 반드시 이 매크로로 초기화 (7.17.8) */

/* 잠금: 이전 값이 false(풀림)였을 때까지 test_and_set. 성공한 RMW 가 acquire. */
static void spin_lock(spinlock_t *l) {
    (void)l; /* TODO */
}
/* 한 번만 시도: 잠그는 데 성공하면 true. */
static bool spin_trylock(spinlock_t *l) {
    (void)l;
    return false; /* TODO */
}
/* 해제: 임계 구역 안의 쓰기를 다음 소유자에게 게시하는 release. */
static void spin_unlock(spinlock_t *l) {
    (void)l; /* TODO */
}

/* 티켓(고유 번호) 발급: 0, 1, 2, ... 여러 스레드가 동시에 불러도 중복·누락 없음. */
static uint32_t ticket_next(atomic_uint *t) {
    (void)t;
    return 0; /* TODO */
}

/* 최대값 워터마크(최대 큐 깊이, 최악 지연 등): *p = max(*p, v). 갱신 직전에 본 값을 반환. */
static uint32_t stat_max(atomic_uint *p, uint32_t v) {
    (void)p; (void)v;
    return 0; /* TODO */
}

/* ======================= Part B — 1칸 채널 (publish / consume) ======================= */
typedef struct { uint32_t seq; uint32_t len; uint8_t data[20]; uint32_t crc; } msg_t;   /* 32 B */
typedef struct {
    atomic_uint full;      /* 0 = EMPTY: 슬롯은 생산자 소유, 1 = FULL: 소비자 소유 */
    msg_t slot;            /* 일반(비원자) 데이터 — full 의 release/acquire 로 보호 */
} chan_t;

/* 슬롯이 비어 있으면 m 을 복사하고 FULL 로 게시, true. 차 있으면 false. */
static bool chan_try_send(chan_t *c, const msg_t *m) {
    (void)c; (void)m;
    return false; /* TODO */
}
/* 슬롯이 차 있으면 out 으로 복사하고 EMPTY 로 돌려줌, true. 비어 있으면 false. */
static bool chan_try_recv(chan_t *c, msg_t *out) {
    (void)c; (void)out;
    return false; /* TODO */
}

/* ======================= Part C — 센서 드라이버 (트리플 버퍼) ======================= */
typedef struct { uint32_t seq, gain, offset; int32_t s[12]; uint32_t sum; } frame_t;   /* 64 B */

#define TB_IDX   3u        /* mid 의 비트 0-1: 버퍼 인덱스 */
#define TB_DIRTY 4u        /* mid 의 비트 2  : 아직 소비자가 안 가져간 새 프레임 */
typedef struct {
    frame_t buf[3];
    atomic_uint mid;       /* 생산자·소비자가 교환하는 유일한 공유 변수 */
    unsigned back;         /* 생산자 전용: 지금 채우는 버퍼 */
    unsigned front;        /* 소비자 전용: 지금 읽는 버퍼 */
} tribuf_t;

static void tb_init(tribuf_t *t) {
    memset(t->buf, 0, sizeof t->buf);
    t->back = 0u;
    atomic_init(&t->mid, 1u);
    t->front = 2u;
}
static frame_t *tb_back(tribuf_t *t) { return &t->buf[t->back]; }

/* 생산자: 다 채운 back 을 mid 와 바꾸고 DIRTY 표시. 안 읽힌 프레임을 덮었으면(drop) true. */
static bool tb_publish(tribuf_t *t) {
    (void)t;
    return false; /* TODO */
}
/* 소비자: 새 프레임이 있으면 front 와 mid 를 바꾸고 그 프레임을, 없으면 NULL. */
static const frame_t *tb_latest(tribuf_t *t) {
    (void)t;
    return NULL; /* TODO */
}

typedef struct { spinlock_t lock; uint32_t gain, offset; } cfg_t;   /* 불변식: offset == gain*10 */
static void cfg_set(cfg_t *c, uint32_t gain) {           /* (주어진 코드) main 이 바꾼다 */
    spin_lock(&c->lock);
    c->gain = gain;
    c->offset = gain * 10u;
    spin_unlock(&c->lock);
}
static void cfg_get(cfg_t *c, uint32_t *gain, uint32_t *offset) {   /* (주어진 코드) */
    spin_lock(&c->lock);
    *gain = c->gain;
    *offset = c->offset;
    spin_unlock(&c->lock);
}

static uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}
static void frame_fill(frame_t *f, uint32_t seq, uint32_t gain, uint32_t offset) {   /* (주어진 코드) */
    uint32_t r = seq * 2654435761u + 1u, sum = seq ^ gain;
    f->seq = seq; f->gain = gain; f->offset = offset;
    for (int i = 0; i < 12; i++) {
        f->s[i] = (int32_t)(xorshift32(&r) % 2001u) - 1000;
        sum += (uint32_t)f->s[i] * 31u + offset;
    }
    f->sum = sum;
}
static bool frame_ok(const frame_t *f) {
    uint32_t sum = f->seq ^ f->gain;
    for (int i = 0; i < 12; i++) sum += (uint32_t)f->s[i] * 31u + f->offset;
    return sum == f->sum && f->offset == f->gain * 10u;
}

typedef struct {
    tribuf_t tb;
    cfg_t cfg;
    atomic_uint produced, dropped;   /* 통계 — 다른 스레드가 언제든 읽는다 → relaxed 원자 */
} sensor_t;

/* "프레임 완료 ISR" (Core1 의 DMA 완료 처리): 현재 설정으로 back 을 채우고 게시, 통계 갱신. */
static void sensor_frame_done(sensor_t *d, uint32_t seq) {
    (void)d; (void)seq;
    (void)cfg_get; (void)frame_fill; (void)tb_back; (void)tb_publish; /* TODO */
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

#define NT 4                                   /* 스레드 수 */
static atomic_int g_go;                        /* 출발 신호: 모든 스레드가 동시에 시작 */
static void wait_go(void) { while (!atomic_load_explicit(&g_go, memory_order_acquire)) cpu_relax(); }
static void run_threads(void *(*fn)(void *), void *args, size_t argsz) {
    pthread_t th[NT];
    atomic_store(&g_go, 0);
    for (int i = 0; i < NT; i++) pthread_create(&th[i], NULL, fn, (char *)args + (size_t)i * argsz);
    atomic_store_explicit(&g_go, 1, memory_order_release);
    for (int i = 0; i < NT; i++) pthread_join(th[i], NULL);
}

/* --- Part A 스트레스 --- */
#define LOCK_ITERS 200000
static spinlock_t g_lock = SPINLOCK_INIT;
static struct { uint64_t a, b; } g_shared;     /* 일반 변수. 불변식 b == 3a — 락으로만 보호 */
static void *lock_worker(void *arg) {
    (void)arg;
    wait_go();
    for (int i = 0; i < LOCK_ITERS; i++) {
        spin_lock(&g_lock);
        g_shared.a += 1u;
        g_shared.b += 3u;
        spin_unlock(&g_lock);
    }
    return NULL;
}
#define TICKETS 100000
static atomic_uint g_ticket;
static uint8_t g_seen[NT * TICKETS];           /* 티켓마다 1바이트: 서로 다른 메모리 위치 → 레이스 아님 */
static void *ticket_worker(void *arg) {
    (void)arg;
    wait_go();
    for (int i = 0; i < TICKETS; i++) {
        uint32_t t = ticket_next(&g_ticket);
        if (t < NT * TICKETS) g_seen[t]++;
    }
    return NULL;
}
#define MAX_ITERS 100000
static atomic_uint g_max;
typedef struct { uint32_t seed; } max_arg_t;
static void *max_worker(void *arg) {
    max_arg_t *a = arg;
    wait_go();
    for (int i = 0; i < MAX_ITERS; i++) stat_max(&g_max, xorshift32(&a->seed) >> 1);
    return NULL;
}

/* --- Part B 스트레스 --- */
#define NMSG 200000u
static chan_t g_ch;
static atomic_int g_stop;
static msg_t msg_make(uint32_t seq) {
    msg_t m;
    m.seq = seq;
    m.len = 4u + seq % 17u;
    uint32_t h = 2166136261u;                  /* FNV-1a */
    for (unsigned i = 0; i < sizeof m.data; i++) {
        m.data[i] = (uint8_t)(seq * 7u + i * 13u);
        h = (h ^ m.data[i]) * 16777619u;
    }
    m.crc = h ^ seq ^ m.len;
    return m;
}
static bool msg_ok(const msg_t *m) {
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < sizeof m->data; i++) h = (h ^ m->data[i]) * 16777619u;
    return m->crc == (h ^ m->seq ^ m->len) && m->len == 4u + m->seq % 17u;
}
static void *producer(void *arg) {
    (void)arg;
    for (uint32_t s = 0; s < NMSG; s++) {
        msg_t m = msg_make(s);
        while (!chan_try_send(&g_ch, &m)) {
            if (atomic_load_explicit(&g_stop, memory_order_relaxed)) return NULL;
            cpu_relax();
        }
    }
    return NULL;
}

/* --- Part C 스트레스 --- */
#define NFRAME 200000u
static sensor_t g_dev = { .cfg = { SPINLOCK_INIT, 1u, 10u } };   /* atomic_flag 는 정적 초기화로 */
static atomic_int g_prod_done;
static void *sensor_thread(void *arg) {         /* Core1: 프레임 완료 ISR 이 NFRAME 번 */
    (void)arg;
    for (uint32_t s = 0; s < NFRAME; s++) sensor_frame_done(&g_dev, s);
    atomic_store_explicit(&g_prod_done, 1, memory_order_release);
    return NULL;
}

int main(void) {
    /* ---------- Part A: 단위 ---------- */
    int f0 = FAILS();
    spinlock_t l = SPINLOCK_INIT;
    CHECK(spin_trylock(&l));                   /* 풀린 락은 잡힌다 */
    CHECK(!spin_trylock(&l));                  /* 이미 잡힌 락은 실패 */
    spin_unlock(&l);
    CHECK(spin_trylock(&l));                   /* 풀면 다시 잡힌다 */
    spin_unlock(&l);
    spin_lock(&l);                             /* 풀린 락에서 spin_lock 은 바로 돌아온다 */
    CHECK(!spin_trylock(&l));
    spin_unlock(&l);
    atomic_uint t = 5u;
    CHECK(ticket_next(&t) == 5u && ticket_next(&t) == 6u && atomic_load(&t) == 7u);
    atomic_uint m = 0u;
    CHECK(stat_max(&m, 5u) == 0u && atomic_load(&m) == 5u);   /* 갱신: 이전 값 0 */
    CHECK(stat_max(&m, 3u) == 5u && atomic_load(&m) == 5u);   /* 작으면 그대로 */
    CHECK(stat_max(&m, 0xFFFFFFFFu) == 5u && atomic_load(&m) == 0xFFFFFFFFu);
    CHECK(atomic_is_lock_free(&m) && atomic_is_lock_free(&g_dev.tb.mid));
    /* ---------- Part A: 스트레스 (NT 스레드) ---------- */
    if (FAILS() == f0) {
        run_threads(lock_worker, NULL, 0);
        CHECK(g_shared.a == (uint64_t)NT * LOCK_ITERS);       /* 증가 하나도 안 잃음 */
        CHECK(g_shared.b == 3u * g_shared.a);                 /* 불변식 유지 */
        run_threads(ticket_worker, NULL, 0);
        int dup = 0, miss = 0;
        for (int i = 0; i < NT * TICKETS; i++) { dup += g_seen[i] > 1u; miss += g_seen[i] == 0u; }
        CHECK(dup == 0 && miss == 0);                         /* 모든 티켓이 정확히 한 번 */
        CHECK(atomic_load(&g_ticket) == (unsigned)(NT * TICKETS));
        max_arg_t ma[NT];
        uint32_t want = 0;
        for (int i = 0; i < NT; i++) {                        /* 기대값은 단일 스레드로 미리 계산 */
            uint32_t s = 0x1234u + (uint32_t)i * 7919u;
            ma[i].seed = s;
            for (int k = 0; k < MAX_ITERS; k++) { uint32_t v = xorshift32(&s) >> 1; if (v > want) want = v; }
        }
        run_threads(max_worker, ma, sizeof ma[0]);
        CHECK(atomic_load(&g_max) == want);
    } else skip(5, "Part A");

    /* ---------- Part B: 단위 ---------- */
    int f1 = FAILS();
    msg_t a = msg_make(1u), b = msg_make(2u), out;
    memset(&out, 0, sizeof out);
    CHECK(msg_ok(&a) && msg_ok(&b));
    CHECK(!chan_try_recv(&g_ch, &out));        /* 빈 채널 */
    CHECK(chan_try_send(&g_ch, &a));
    CHECK(!chan_try_send(&g_ch, &b));          /* 차 있으면 덮어쓰지 않는다 */
    CHECK(chan_try_recv(&g_ch, &out) && out.seq == 1u && msg_ok(&out));
    CHECK(!chan_try_recv(&g_ch, &out));        /* 한 번 받으면 비었다 */
    CHECK(chan_try_send(&g_ch, &b) && chan_try_recv(&g_ch, &out) && out.seq == 2u);
    /* ---------- Part B: 스트레스 (생산자 1 + 소비자 1) ---------- */
    if (FAILS() == f1) {
        pthread_t pt;
        pthread_create(&pt, NULL, producer, NULL);
        uint32_t expect = 0, bad = 0, idle = 0;
        while (expect < NMSG) {
            if (!chan_try_recv(&g_ch, &out)) {
                if (++idle > 200000000u) break;               /* 안전장치: 영원히 안 오면 포기 */
                cpu_relax();
                continue;
            }
            idle = 0;
            if (out.seq != expect || !msg_ok(&out)) bad++;
            expect++;
        }
        atomic_store(&g_stop, 1);
        pthread_join(pt, NULL);
        CHECK(expect == NMSG);                                /* 순서대로 전부 도착 */
        CHECK(bad == 0u);                                     /* acquire 뒤 데이터는 항상 온전 */
    } else skip(2, "Part B");

    /* ---------- Part C: 단위 ---------- */
    int f2 = FAILS();
    tb_init(&g_dev.tb);
    atomic_init(&g_dev.produced, 0u);
    atomic_init(&g_dev.dropped, 0u);
    CHECK(tb_latest(&g_dev.tb) == NULL);                      /* 아직 새 프레임 없음 */
    sensor_frame_done(&g_dev, 100u);
    const frame_t *fr = tb_latest(&g_dev.tb);
    CHECK(fr != NULL && fr->seq == 100u && frame_ok(fr) && fr->gain == 1u);
    CHECK(tb_latest(&g_dev.tb) == NULL);                      /* 같은 프레임을 두 번 주지 않는다 */
    sensor_frame_done(&g_dev, 101u);
    cfg_set(&g_dev.cfg, 7u);
    sensor_frame_done(&g_dev, 102u);                          /* 101 은 안 읽힌 채 덮임 → drop */
    fr = tb_latest(&g_dev.tb);
    CHECK(fr != NULL && fr->seq == 102u && fr->gain == 7u && frame_ok(fr));   /* 항상 최신 */
    CHECK(atomic_load(&g_dev.produced) == 3u && atomic_load(&g_dev.dropped) == 1u);
    unsigned mid = atomic_load(&g_dev.tb.mid) & TB_IDX;
    CHECK((1u << g_dev.tb.back | 1u << mid | 1u << g_dev.tb.front) == 7u);  /* 세 인덱스는 항상 {0,1,2} */
    /* ---------- Part C: 스트레스 (Core1 생산 + Core0 소비·설정 변경) ---------- */
    if (FAILS() == f2) {
        tb_init(&g_dev.tb);
        cfg_set(&g_dev.cfg, 1u);
        atomic_store(&g_dev.produced, 0u);
        atomic_store(&g_dev.dropped, 0u);
        pthread_t st;
        pthread_create(&st, NULL, sensor_thread, NULL);
        uint32_t consumed = 0, bad = 0, idle = 0, gain = 1u;
        int64_t last = -1;
        while (last != (int64_t)NFRAME - 1) {
            const frame_t *f = tb_latest(&g_dev.tb);
            if (!f) {
                if (atomic_load_explicit(&g_prod_done, memory_order_acquire) && ++idle > 10000000u) break;
                continue;
            }
            if (!frame_ok(f) || (int64_t)f->seq <= last) bad++;
            last = f->seq;
            if (++consumed % 64u == 0u) cfg_set(&g_dev.cfg, gain = gain % 9u + 1u);
        }
        pthread_join(st, NULL);
        unsigned prod = atomic_load(&g_dev.produced), drop = atomic_load(&g_dev.dropped);
        printf("Part C stress: produced=%u consumed=%u dropped=%u\n", prod, consumed, drop);
        CHECK(last == (int64_t)NFRAME - 1);                   /* 마지막 프레임은 반드시 받는다 */
        CHECK(bad == 0u);                                     /* 찢어진 프레임·역행 없음 */
        CHECK(prod == NFRAME && consumed + drop == NFRAME);    /* 모든 프레임은 소비 또는 drop 중 하나 */
    } else skip(3, "Part C");

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
