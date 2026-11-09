/* MEM-M04 일관성 트래픽 실측: 공유 캐시라인 경합과 코어 수
 *
 *   Part A  세 패턴(+a')을 진짜 스레드로 돌리는 작업 함수 — 합계가 "정확히" 맞아야 한다
 *           (a)  공유 카운터 하나에 fetch_add relaxed (arm64 LSE → ldadd), 돌려받은 옛 값 기록
 *           (a') 같은 카운터를 CAS 루프로 (ldr → cas, 실패하면 재시도)
 *           (b)  스레드별 카운터: 칸 하나 = 128 B 라인 하나, 끝나고 합산
 *           (c)  읽기 전용 공유 테이블 (라인 4개)을 모든 스레드가 읽기만
 *   Part B  MESI 트랜잭션 카운터 모델 (시간 없음, 개수만): round-robin 고정 스케줄에서
 *           BusRd / BusRdX / BusUpgr / 무효화 / cache-to-cache 횟수 → 손으로 유도한 공식과 같아야 한다
 *   Part C  펌웨어 시나리오: 여러 코어가 읽는 설정 워드 + 가끔 갱신하는 writer 하나 (read-mostly)
 *           C1 진짜 스레드: reader 가 본 값은 절대 뒤로 가지 않는다 (같은 주소 coherence 보장)
 *           C2 모델: 갱신 한 번의 값 = BusUpgr 1 + 무효화 R + 재읽기 R
 *
 * 체크 테스트는 정확성만 본다 (합계, 옛 값 집합, 모델 개수, 단조성). 시간은 절대 CHECK 하지 않는다
 * → demo_bench.c (1~12 스레드 처리량 곡선, P/E 코어, 핸드오프 비율).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so
 */
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LINE 128   /* sysctl hw.cachelinesize = 128 (Apple M6). Cortex-A 리눅스 보드는 보통 64 */
#define MAXT 12    /* 이 맥 코어 수: Super 2 + Performance 4 + Efficiency 6 */

#if defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#define SAN_BUILD 1
#endif
#endif
#ifndef SAN_BUILD
#define SAN_BUILD 0
#endif
#define OPS (SAN_BUILD ? 20000u : 200000u)   /* 스레드당 연산 수 — 새니타이저 빌드는 1/10 */

/* ======================= Part A — 세 패턴의 데이터 ======================= */
/* (a)(a') 공유 카운터: 모든 스레드가 같은 라인 하나를 쓴다 */
static _Alignas(LINE) _Atomic uint64_t g_shared;

/* (b) 스레드별 카운터. 규칙: 칸 하나가 라인 하나를 통째로 차지 (정렬 128, 크기 128의 배수) */
typedef struct {
    _Atomic uint64_t v;   /* TODO: 칸 하나가 라인 하나를 차지하게 (지금은 8 B 칸 16개가 라인 하나에) */
} slot_t;
static slot_t g_slot[MAXT];

/* (c) 읽기 전용 공유 테이블: 라인 4개 = 64 워드. 시작 전에 한 번 채우고 그 뒤로는 읽기만 */
#define RO_LINES 4
#define RO_WORDS (RO_LINES * LINE / 8)
static _Alignas(LINE) _Atomic uint64_t g_ro[RO_WORDS];

/* ======================= Part A — 작업 함수 ======================= */
/* A1. (a) g_shared 에 fetch_add(1, relaxed) 를 n 번. i 번째 호출이 돌려준 옛 값을 olds[i] 에 (32비트로) 기록 */
static void work_shared_add(uint32_t *olds, uint64_t n) {
    (void)olds; (void)n; /* TODO */
}

/* A2. (a') g_shared 를 CAS 루프로 n 번 +1. 반환값 = CAS 실패(재시도) 횟수.
 *     읽기 → old+1 계산 → compare_exchange_weak (relaxed/relaxed). 실패하면 old 가 현재 값으로 갱신된다 */
static uint64_t work_shared_cas(uint64_t n) {
    (void)n;
    return 0; /* TODO */
}

/* A3. (b) 자기 칸 g_slot[t] 에만 fetch_add(1, relaxed) 를 n 번 — 다른 스레드와 공유하는 라인이 없다 */
static void work_private_add(int t, uint64_t n) {
    (void)t; (void)n; /* TODO */
}

/* A4. (b) 합산: 칸 0..k-1 의 합 (relaxed load). reader 쪽 비용 = 라인 k 개 읽기 */
static uint64_t sum_slots(int k) {
    (void)k;
    return 0; /* TODO */
}

/* A5. (c) 읽기 전용 테이블을 n 번 읽은 합: i 번째 읽기 = g_ro[i % RO_WORDS] (relaxed load).
 *     atomic load 라서 컴파일러가 "테이블 합 × 반복 수" 로 접어 버리지 못한다 → 매번 진짜 ldr */
static uint64_t work_readonly(uint64_t n) {
    (void)n;
    return 0; /* TODO */
}

/* ======================= Part B — MESI 트랜잭션 카운터 모델 ======================= */
/* 코어 nc 개, 각자 private 캐시 (무한 크기 — 이 패턴들은 라인 몇 개뿐이라 eviction 없음).
 * 원자적인 스누핑 버스 하나 (한 번에 트랜잭션 하나). 시간은 없다 — 개수만 센다.
 *
 *  read (load)              : M/E/S → hit.   I → BusRd 그리고
 *                             · 다른 캐시에 M 이 있으면: 그 캐시가 데이터를 준다(c2c) + 메모리도 갱신(wb), 그 캐시 M→S, 요청자 S
 *                             · M 은 없고 E/S 사본이 있으면: 메모리가 준다(mem), E 사본은 E→S, 요청자 S
 *                             · 사본이 없으면: 메모리가 준다(mem), 요청자 E
 *  write (store / atomic RMW): M → hit.   E → hit (조용히 E→M, 버스 없음)
 *                             S → BusUpgr, 다른 사본 전부 무효화 (inval += 사본 수), 요청자 M
 *                             I → BusRdX, 다른 캐시에 M 이 있으면 c2c 아니면 mem,
 *                                 다른 사본 전부 무효화 (inval += 사본 수), 요청자 M
 */
enum { ST_I = 0, ST_S, ST_E, ST_M };
#define MC 16
#define ML 16
typedef struct {
    uint64_t rd, rdx, upgr; /* 버스 트랜잭션 */
    uint64_t inval;         /* 무효화된 사본 수 (BusRdX/BusUpgr 하나가 여러 사본을 지울 수 있다) */
    uint64_t c2c;           /* M 사본을 가진 캐시가 데이터를 직접 준 횟수 (intervention / flush) */
    uint64_t mem;           /* 메모리(또는 LLC)가 데이터를 준 횟수 */
    uint64_t wb;            /* BusRd 스누프로 M→S 될 때 메모리에 써 준 횟수 */
    uint64_t hit;           /* 버스 없이 끝난 접근 */
} msgs_t;
typedef struct {
    int nc, nl;
    uint8_t st[MC][ML];
    msgs_t m;
} mesi_t;

static void mesi_init(mesi_t *s, int nc, int nl) {
    memset(s, 0, sizeof *s);
    s->nc = nc;
    s->nl = nl;
}

/* B1. 코어 c 가 라인 l 을 읽는다 (위 표대로 상태와 개수를 갱신) */
static void mesi_read(mesi_t *s, int c, int l) {
    (void)s; (void)c; (void)l; /* TODO */
}

/* B2. 코어 c 가 라인 l 에 쓴다 (store 또는 atomic RMW — 둘 다 독점(M)이 필요) */
static void mesi_write(mesi_t *s, int c, int l) {
    (void)s; (void)c; (void)l; /* TODO */
}

/* 패턴 이름 (Part A 의 진짜 스레드 패턴과 같은 이름) */
enum { PAT_ADD, PAT_CAS, PAT_PRIV, PAT_RO, PAT_RMOSTLY };

/* B3. 라운드 r 에서 스레드 t 가 하는 접근 (패턴 → 모델 접근으로 옮기기)
 *   PAT_ADD     : 라인 0 에 write (fetch_add = RMW = 독점 필요)
 *   PAT_CAS     : 라인 0 read (ldr) 다음 라인 0 write (cas) — round-robin 이라 CAS 는 실패하지 않는다
 *   PAT_PRIV    : 자기 라인 t 에 write
 *   PAT_RO      : 라인 (r % RO_LINES) read
 *   PAT_RMOSTLY : t == 0 (writer) 은 r % every == 0 일 때만 라인 0 write, 아니면 아무것도 안 함
 *                 t >= 1 (reader) 은 매 라운드 라인 0 read */
static void mesi_op(mesi_t *s, int pat, int t, uint64_t r, uint64_t every) {
    (void)s; (void)pat; (void)t; (void)r; (void)every; /* TODO: mesi_read / mesi_write 로 */
}

/* ---- 하네스 쪽 (TODO 아님): 불변식과 고정 스케줄 ---- */
/* SWMR (single writer / multiple readers): 라인마다 M/E 는 최대 1개, M/E 가 있으면 나머지는 전부 I */
static bool mesi_swmr_ok(const mesi_t *s) {
    for (int l = 0; l < s->nl; l++) {
        int excl = 0, valid = 0;
        for (int c = 0; c < s->nc; c++) {
            if (s->st[c][l] != ST_I) valid++;
            if (s->st[c][l] == ST_M || s->st[c][l] == ST_E) excl++;
        }
        if (excl > 1 || (excl == 1 && valid != 1)) return false;
    }
    return true;
}
/* round-robin: 라운드 r = 0..n-1, 라운드 안에서 스레드 0..k-1 순서. 매 접근 뒤 SWMR 검사. 반환 = 위반 없음 */
static bool mesi_run(mesi_t *s, int pat, int k, uint64_t n, uint64_t every) {
    mesi_init(s, k, ML);
    bool ok = true;
    for (uint64_t r = 0; r < n; r++)
        for (int t = 0; t < k; t++) {
            mesi_op(s, pat, t, r, every);
            if (!mesi_swmr_ok(s)) ok = false;
        }
    return ok;
}
static uint64_t bus_tx(const msgs_t *m) { return m->rd + m->rdx + m->upgr; }

/* ======================= Part C — read-mostly 설정 워드 ======================= */
static _Alignas(LINE) _Atomic uint64_t g_cfg;      /* 설정 버전. writer 하나만 쓴다 */
static _Alignas(LINE) _Atomic int g_cfg_done;      /* 하네스가 writer join 뒤 1 (release) */
typedef struct {
    uint64_t reads, backwards, maxv, lastv;
} rd_stat_t;

/* C1. writer: g_cfg 에 1, 2, ..., updates 를 차례로 relaxed store. 저장 사이에 g_cfg 를 gap 번 읽으며 쉰다 */
static void cfg_writer(uint64_t updates, unsigned gap) {
    (void)updates; (void)gap; /* TODO */
}

/* C2. reader: g_cfg_done 을 acquire 로 볼 때까지 g_cfg 를 relaxed 로 계속 읽는다.
 *     reads = 읽은 횟수, backwards = 직전에 읽은 값보다 작은 값을 본 횟수, maxv = 본 최대값.
 *     done 을 본 뒤 한 번 더 읽어 lastv 에 둔다 (writer 의 마지막 store 가 보여야 한다) */
static void cfg_reader(rd_stat_t *st) {
    memset(st, 0, sizeof *st); /* TODO: done 을 볼 때까지 읽기 + 통계 */
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_olds[MAXT][200000];          /* (a) 스레드별 옛 값 기록 (OPS 이하) */
static uint8_t g_seen[MAXT * 200000];
static _Alignas(LINE) _Atomic int g_ready;
static _Alignas(LINE) _Atomic int g_go;

typedef struct {
    int pat, t;
    uint64_t n, out;
    unsigned gap;
    rd_stat_t rs;
} targ_t;

static void *worker(void *p) {
    targ_t *a = p;
    atomic_fetch_add_explicit(&g_ready, 1, memory_order_relaxed);
    while (!atomic_load_explicit(&g_go, memory_order_acquire)) sched_yield();   /* 다 같이 출발 */
    switch (a->pat) {
    case PAT_ADD: work_shared_add(g_olds[a->t], a->n); break;
    case PAT_CAS: a->out = work_shared_cas(a->n); break;
    case PAT_PRIV: work_private_add(a->t, a->n); break;
    case PAT_RO: a->out = work_readonly(a->n); break;
    case PAT_RMOSTLY: if (a->t == 0) cfg_writer(a->n, a->gap); else cfg_reader(&a->rs); break;
    default: break;
    }
    return NULL;
}

/* k 스레드로 패턴 실행. PAT_RMOSTLY 는 스레드 0 (writer) 을 join 한 뒤 g_cfg_done 을 켜고 나머지를 join */
static bool run_pattern(int pat, int k, uint64_t n, unsigned gap, targ_t *args) {
    pthread_t th[MAXT];
    atomic_store(&g_ready, 0);
    atomic_store(&g_go, 0);
    atomic_store(&g_cfg_done, 0);
    int made = 0;
    for (int t = 0; t < k; t++) {
        args[t] = (targ_t){ .pat = pat, .t = t, .n = n, .gap = gap };
        if (pthread_create(&th[t], NULL, worker, &args[t]) != 0) break;
        made++;
    }
    while (atomic_load(&g_ready) < made) sched_yield();
    atomic_store_explicit(&g_go, 1, memory_order_release);
    int first = 0;
    if (pat == PAT_RMOSTLY && made > 0) {
        pthread_join(th[0], NULL);
        atomic_store_explicit(&g_cfg_done, 1, memory_order_release);
        first = 1;
    }
    for (int t = first; t < made; t++) pthread_join(th[t], NULL);
    atomic_store(&g_cfg_done, 1);
    return made == k;
}

static const int KS[] = { 1, 2, 4, 12 };
#define NKS ((int)(sizeof KS / sizeof KS[0]))

static void test_part_a(void) {
    printf("-- Part A: real threads, exact totals (OPS=%u per thread)\n", OPS);
    targ_t args[MAXT];

    /* 레이아웃: 칸 하나 = 라인 하나 */
    CHECK(_Alignof(slot_t) == LINE && sizeof(slot_t) % LINE == 0);
    CHECK((uintptr_t)&g_slot[1] / LINE != (uintptr_t)&g_slot[0] / LINE);
    CHECK((uintptr_t)&g_slot[MAXT - 1].v % LINE == 0);

    for (int i = 0; i < NKS; i++) {
        int k = KS[i];
        uint64_t total = (uint64_t)k * OPS;

        /* (a) fetch_add: 합계 + 돌려받은 옛 값이 0..total-1 을 정확히 한 번씩 */
        atomic_store(&g_shared, 0);
        memset(g_olds, 0xFF, sizeof g_olds);
        CHECK(run_pattern(PAT_ADD, k, OPS, 0, args));
        CHECK(atomic_load(&g_shared) == total);
        memset(g_seen, 0, sizeof g_seen);
        uint64_t dup = 0, bad = 0;
        for (int t = 0; t < k; t++)
            for (uint32_t j = 0; j < OPS; j++) {
                uint32_t v = g_olds[t][j];
                if (v >= total) bad++;
                else if (g_seen[v]++) dup++;
            }
        CHECK(dup == 0 && bad == 0);

        /* (a') CAS 루프: 합계. 실패 횟수는 실행마다 다르다 → CHECK 하지 않는다 (demo_bench.c 가 센다) */
        atomic_store(&g_shared, 0);
        CHECK(run_pattern(PAT_CAS, k, OPS, 0, args));
        CHECK(atomic_load(&g_shared) == total);

        /* (b) 스레드별 칸 */
        for (int t = 0; t < MAXT; t++) atomic_store(&g_slot[t].v, 0);
        CHECK(run_pattern(PAT_PRIV, k, OPS, 0, args));
        bool each = true;
        for (int t = 0; t < k; t++) each = each && atomic_load(&g_slot[t].v) == OPS;
        CHECK(each && sum_slots(k) == total);

        /* (c) 읽기 전용: 스레드마다 같은 합 */
        uint64_t want = 0;
        for (uint64_t j = 0; j < OPS; j++) want += 1000u + (j % RO_WORDS);
        CHECK(run_pattern(PAT_RO, k, OPS, 0, args));
        bool same = true;
        for (int t = 0; t < k; t++) same = same && args[t].out == want;
        CHECK(same);
    }
}

static void test_part_b(void) {
    printf("-- Part B: MESI transaction counts vs hand-derived formulas\n");
    mesi_t s;

    /* 단일 접근 전이 */
    mesi_init(&s, 4, 1);
    mesi_read(&s, 0, 0);
    CHECK(s.st[0][0] == ST_E && s.m.rd == 1 && s.m.mem == 1);            /* 혼자 읽으면 E */
    mesi_read(&s, 1, 0);
    CHECK(s.st[0][0] == ST_S && s.st[1][0] == ST_S && s.m.mem == 2 && s.m.c2c == 0);
    mesi_write(&s, 2, 0);                                                 /* I 에서 쓰기: BusRdX, 사본 2개 무효화 */
    CHECK(s.st[2][0] == ST_M && s.st[0][0] == ST_I && s.st[1][0] == ST_I && s.m.rdx == 1 && s.m.inval == 2);
    mesi_read(&s, 3, 0);                                                  /* M 사본에서 c2c + wb */
    CHECK(s.st[2][0] == ST_S && s.st[3][0] == ST_S && s.m.c2c == 1 && s.m.wb == 1);
    mesi_write(&s, 3, 0);                                                 /* S 에서 쓰기: BusUpgr */
    CHECK(s.st[3][0] == ST_M && s.st[2][0] == ST_I && s.m.upgr == 1 && s.m.inval == 3);
    mesi_write(&s, 3, 0);
    CHECK(s.m.hit == 1 && bus_tx(&s.m) == 5);                            /* rd 3 + rdx 1 + upgr 1 */
    mesi_init(&s, 2, 1);
    mesi_read(&s, 0, 0);
    mesi_write(&s, 0, 0);                                                 /* E→M 은 조용히 */
    CHECK(s.st[0][0] == ST_M && bus_tx(&s.m) == 1 && s.m.hit == 1);

    const uint64_t n = 1000;
    for (int k = 1; k <= MAXT; k++) {
        uint64_t N = (uint64_t)k * n;
        /* (a) 공유 fetch_add: k >= 2 면 연산마다 BusRdX 1 + c2c 1 + 무효화 1 (첫 연산만 메모리) */
        CHECK(mesi_run(&s, PAT_ADD, k, n, 0));
        if (k == 1) CHECK(s.m.rdx == 1 && s.m.mem == 1 && s.m.c2c == 0 && s.m.inval == 0 && s.m.hit == n - 1);
        else CHECK(s.m.rdx == N && s.m.c2c == N - 1 && s.m.inval == N - 1 && s.m.mem == 1 && s.m.rd == 0
                   && s.m.upgr == 0 && s.m.hit == 0);
        /* (a') CAS (ldr + cas, 실패 없음): k >= 2 면 연산마다 BusRd 1 + BusUpgr 1 → 트랜잭션 2배 */
        CHECK(mesi_run(&s, PAT_CAS, k, n, 0));
        if (k == 1) CHECK(s.m.rd == 1 && s.m.upgr == 0 && s.m.rdx == 0 && bus_tx(&s.m) == 1);
        else CHECK(s.m.rd == N && s.m.upgr == N - 1 && s.m.rdx == 0 && s.m.c2c == N - 1 && s.m.wb == N - 1
                   && s.m.inval == N - 1 && s.m.mem == 1);
        /* (b) 스레드별 라인: n 과 무관하게 콜드 미스 k 번뿐 */
        CHECK(mesi_run(&s, PAT_PRIV, k, n, 0));
        CHECK(s.m.rdx == (uint64_t)k && s.m.mem == (uint64_t)k && s.m.inval == 0 && s.m.c2c == 0
              && bus_tx(&s.m) == (uint64_t)k && s.m.hit == N - (uint64_t)k);
        /* (c) 읽기 전용 라인 L 개: 코어마다 라인마다 BusRd 1 번, 무효화 0. 끝 상태 = 모두 S (k == 1 이면 E) */
        CHECK(mesi_run(&s, PAT_RO, k, n, 0));
        CHECK(s.m.rd == (uint64_t)k * RO_LINES && s.m.mem == (uint64_t)k * RO_LINES && s.m.inval == 0
              && s.m.c2c == 0 && s.m.rdx + s.m.upgr == 0);
        CHECK(s.st[k - 1][RO_LINES - 1] == (k == 1 ? ST_E : ST_S));
    }
    /* 연산당 트랜잭션 (×1000, 정수) 의 순서: CAS > ADD >> PRIV = RO */
    mesi_run(&s, PAT_ADD, 8, n, 0);
    uint64_t add = bus_tx(&s.m) * 1000 / (8 * n);
    mesi_run(&s, PAT_CAS, 8, n, 0);
    uint64_t cas = bus_tx(&s.m) * 1000 / (8 * n);
    mesi_run(&s, PAT_PRIV, 8, n, 0);
    uint64_t prv = bus_tx(&s.m) * 1000 / (8 * n);
    CHECK(add == 1000 && cas == 1999 && prv == 1);
}

static void test_part_c(void) {
    printf("-- Part C: read-mostly config word (real threads + model)\n");
    targ_t args[MAXT];
    const uint64_t U = SAN_BUILD ? 2000 : 20000;
    for (int k = 2; k <= MAXT; k += 5) {         /* k = 2, 7, 12 : writer 1 + reader k-1 */
        atomic_store(&g_cfg, 0);
        CHECK(run_pattern(PAT_RMOSTLY, k, U, 64, args));
        bool mono = true, last = true, maxok = true;
        for (int t = 1; t < k; t++) {
            mono = mono && args[t].rs.backwards == 0;
            last = last && args[t].rs.lastv == U;
            maxok = maxok && args[t].rs.maxv <= U;
        }
        CHECK(mono);
        CHECK(last && maxok);
        CHECK(atomic_load(&g_cfg) == U);
    }

    /* 모델: writer 1 (스레드 0) + reader R = k-1, n 라운드, every 라운드마다 갱신 → 갱신 U = ceil(n / every)
     *   rdx = 1 (첫 쓰기), upgr = U - 1, inval = R (U - 1), rd = R U, c2c = U, wb = U, mem = 1 + (R - 1) U */
    mesi_t s;
    const uint64_t n = 1000;
    const uint64_t evs[] = { 1, 10, 100, 1000 };
    for (int k = 2; k <= MAXT; k++)
        for (int e = 0; e < 4; e++) {
            uint64_t every = evs[e], R = (uint64_t)k - 1, Uc = (n + every - 1) / every;
            CHECK(mesi_run(&s, PAT_RMOSTLY, k, n, every));
            CHECK(s.m.rdx == 1 && s.m.upgr == Uc - 1 && s.m.inval == R * (Uc - 1) && s.m.rd == R * Uc
                  && s.m.c2c == Uc && s.m.wb == Uc && s.m.mem == 1 + (R - 1) * Uc);
        }
    /* 갱신이 드물면 읽기 n R 번 중 버스를 쓰는 것은 R U 번 뿐: every = 100, k = 12 → 1 % */
    mesi_run(&s, PAT_RMOSTLY, 12, n, 100);
    CHECK(s.m.rd * 100 == 11 * n);
}

int main(void) {
    for (int j = 0; j < RO_WORDS; j++) atomic_store(&g_ro[j], 1000u + (uint64_t)j);
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
