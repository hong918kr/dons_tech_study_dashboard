/* MEM-M04 추가 연습 — 한 번에 검증 (MESI 트랜잭션 카운터 모델 위에서)
 *
 *   연습 1  모델로 본 false sharing: 스레드 t 의 카운터가 오프셋 t*stride 에 있을 때 (stride 8/64/128/256)
 *   연습 2  CAS 가 실제로 실패하는 스케줄 (lockstep: 모두 읽고 → 모두 cas) — 성공 1번에 트랜잭션 약 2k-1
 *   연습 3  스레드별 카운터 + 주기적으로 합을 읽는 텔레메트리 코어 — 합 1번 = c2c k + BusUpgr k
 *   연습 4  측정 → 모델: 핸드오프 비율 h 로 "연산당 라인 이동" 을 다시 세기 (정수 산술)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LINE 128
#define MAXT 12

/* ---- 모델 (solution.c Part B 의 mesi_read / mesi_write 와 같은 코드) ---- */
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
    if (s->st[c][l] != ST_I) { s->m.hit++; return; }
    s->m.rd++;
    int m_owner = -1, copies = 0;
    for (int o = 0; o < s->nc; o++) {
        if (o == c || s->st[o][l] == ST_I) continue;
        copies++;
        if (s->st[o][l] == ST_M) m_owner = o;
    }
    if (m_owner >= 0) { s->m.c2c++; s->m.wb++; }
    else s->m.mem++;
    for (int o = 0; o < s->nc; o++)
        if (o != c && s->st[o][l] != ST_I) s->st[o][l] = ST_S;    /* M→S, E→S, S 그대로 */
    s->st[c][l] = copies ? ST_S : ST_E;
}

/* B2. 코어 c 가 라인 l 에 쓴다 (store 또는 atomic RMW — 둘 다 독점(M)이 필요) */
static void mesi_write(mesi_t *s, int c, int l) {
    uint8_t me = s->st[c][l];
    if (me == ST_M || me == ST_E) { s->m.hit++; s->st[c][l] = ST_M; return; }
    int m_owner = -1, copies = 0;
    for (int o = 0; o < s->nc; o++) {
        if (o == c || s->st[o][l] == ST_I) continue;
        copies++;
        if (s->st[o][l] == ST_M) m_owner = o;
        s->st[o][l] = ST_I;
    }
    if (me == ST_S) s->m.upgr++;
    else {
        s->m.rdx++;
        if (m_owner >= 0) s->m.c2c++;
        else s->m.mem++;
    }
    s->m.inval += (uint64_t)copies;
    s->st[c][l] = ST_M;
}

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

static uint64_t bus_tx(const msgs_t *m) { return m->rd + m->rdx + m->upgr; }

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---- 연습 1: stride 배치의 false sharing ----
 * 스레드 t 의 카운터 주소 = t * stride (바이트). 라인 번호 = 주소 / LINE. round-robin 으로 n 라운드 write. */
static int line_of(int t, int stride) { return (int)((uint64_t)t * (uint64_t)stride / LINE); }
static bool run_stride(mesi_t *s, int k, int stride, uint64_t n) {
    mesi_init(s, k, ML);
    bool ok = true;
    for (uint64_t r = 0; r < n; r++)
        for (int t = 0; t < k; t++) {
            mesi_write(s, t, line_of(t, stride));
            ok = ok && mesi_swmr_ok(s);
        }
    return ok;
}

/* ---- 연습 2: lockstep CAS — 라운드마다 k 스레드가 모두 ldr, 그다음 모두 cas.
 * 첫 cas 만 성공 (나머지는 값이 바뀌어 실패). 실패한 cas 도 라인 독점을 얻는다고 모델링 (= write).
 * 반환 = 성공 횟수 */
static uint64_t run_cas_lockstep(mesi_t *s, int k, uint64_t rounds) {
    mesi_init(s, k, 1);
    uint64_t ok = 0;
    for (uint64_t r = 0; r < rounds; r++) {
        for (int t = 0; t < k; t++) mesi_read(s, t, 0);
        for (int t = 0; t < k; t++) { mesi_write(s, t, 0); if (t == 0) ok++; }
    }
    return ok;
}

/* ---- 연습 3: 코어 0..k-1 은 자기 라인 t 에 매 라운드 write, 코어 k (텔레메트리) 는 r % P == P-1 인 라운드 끝에
 * 라인 0..k-1 을 전부 read (합산) */
static void run_telemetry(mesi_t *s, int k, uint64_t n, uint64_t P) {
    mesi_init(s, k + 1, ML);
    for (uint64_t r = 0; r < n; r++) {
        for (int t = 0; t < k; t++) mesi_write(s, t, t);
        if (r % P == P - 1)
            for (int l = 0; l < k; l++) mesi_read(s, k, l);
    }
}

/* ---- 연습 4: 측정된 핸드오프 비율 → 모델의 "연산당 라인 이동".
 * demo_bench runs: k >= 2 에서 handoff/op ≈ 0.3 (h_permille = 300). round-robin 모델은 1000.
 * 연산당 시간 (ps) = 로컬 RMW 시간 + h × 이동 1번 시간 → 이동 1번 시간 (ps) 을 역산 */
static uint64_t transfer_ps(uint64_t ps_per_op_contended, uint64_t ps_per_op_local, uint64_t h_permille) {
    if (h_permille == 0 || ps_per_op_contended <= ps_per_op_local) return 0;
    return (ps_per_op_contended - ps_per_op_local) * 1000 / h_permille;
}

int main(void) {
    mesi_t s;
    const uint64_t n = 1000;

    printf("-- 1: stride false sharing in the model\n");
    for (int k = 2; k <= MAXT; k += 2) {
        uint64_t N = (uint64_t)k * n;
        CHECK(run_stride(&s, k, 8, n));        /* 16 칸이 라인 하나 → 패턴 a 와 똑같다 */
        CHECK(s.m.rdx == N && s.m.c2c == N - 1 && s.m.inval == N - 1);
        CHECK(run_stride(&s, k, 64, n));       /* 2 칸씩 라인 공유 (모델 LINE = 128) */
        uint64_t lines = (uint64_t)k / 2;
        CHECK(s.m.rdx == N && s.m.c2c == N - lines && s.m.mem == lines);
        CHECK(run_stride(&s, k, 128, n));      /* 칸마다 라인 → 콜드 미스만 */
        CHECK(s.m.rdx == (uint64_t)k && s.m.c2c == 0);
        if (k <= 8) {                          /* 256 B 간격이면 라인 번호 = 2t → 모델 라인 ML = 16 개 안 */
            CHECK(run_stride(&s, k, 256, n));
            CHECK(s.m.rdx == (uint64_t)k && s.m.c2c == 0);
        }
    }

    printf("-- 2: lockstep CAS (failures)\n");
    for (int k = 2; k <= MAXT; k++) {
        uint64_t R = 200, K = (uint64_t)k;
        CHECK(run_cas_lockstep(&s, k, R) == R);
        /* 라운드 0: 읽기 k 번 (메모리). 라운드 r > 0: 직전 라운드 마지막 cas 주인(t k-1)은 S 로 남아 hit,
         * t0 은 그 M 에서 c2c, 나머지 k-2 는 메모리 → 읽기 k-1 번. cas 단계: t0 BusUpgr(사본 k-1 무효화) + 나머지 BusRdX */
        CHECK(s.m.rd == (K - 1) * R + 1 && s.m.upgr == R && s.m.rdx == (K - 1) * R && s.m.inval == 2 * (K - 1) * R
              && s.m.c2c == (K - 1) * R + (R - 1) && s.m.wb == R - 1 && s.m.mem == K + (K - 2) * (R - 1));
        CHECK(bus_tx(&s.m) == (2 * K - 1) * R + 1);   /* 성공 1번에 트랜잭션 약 2k-1 */
    }
    CHECK(run_cas_lockstep(&s, 1, 200) == 200 && bus_tx(&s.m) == 1);

    printf("-- 3: per-thread counters + telemetry reader\n");
    const uint64_t Ps[] = { 10, 100, 1000 };
    for (int k = 1; k <= 8; k++)
        for (int i = 0; i < 3; i++) {
            uint64_t P = Ps[i], K = (uint64_t)k, S = n / P;
            run_telemetry(&s, k, n, P);
            CHECK(s.m.rdx == K && s.m.rd == K * S && s.m.c2c == K * S && s.m.wb == K * S
                  && s.m.upgr == K * (S - 1) && s.m.inval == K * (S - 1));
        }
    run_telemetry(&s, 8, n, 10);              /* 증가 8000번에 트랜잭션 8 + 800 + 792 = 1600 → 증가당 0.2 = 2/P */
    CHECK(bus_tx(&s.m) == 1600);

    printf("-- 4: transfer time from measured handoff ratio\n");
    CHECK(transfer_ps(5850, 1390, 300) == 14866);   /* 171 Mops/s, 1 스레드 720 Mops/s, h = 0.3 → 이동 ≈ 15 ns */
    CHECK(transfer_ps(5850, 1390, 1000) == 4460);   /* round-robin(h = 1) 이라고 믿으면 4.5 ns — LLAT-M05 의 40 ns 와 모순 */
    CHECK(transfer_ps(1000, 1390, 300) == 0 && transfer_ps(5000, 1000, 0) == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
