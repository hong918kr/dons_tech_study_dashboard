/* ARCH-M14 추가 연습 — 한 번에 검증
 *
 *   연습 1  긴 전송을 합법 INCR 버스트로 쪼개기 (≤ 256 beat, 4 KB 안 넘기) — DMA 드라이버 / 인터커넥트의 일
 *   연습 2  같은 ID 순서 규칙이 허용하는 응답 순서의 수 (열거 = 다항계수)
 *   연습 3  VALID/READY 의존 그래프에서 사이클 찾기 → "이 매니저 × 이 서브" 조합이 데드락 가능한가
 *   연습 4  Little 의 법칙: 목표 대역폭 · 지연 · 버스트 크기 → 필요한 outstanding 수 (정수)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ================= 연습 1: 버스트 쪼개기 ================= */
typedef struct { uint32_t addr; unsigned len; } burst_t;     /* len = AxLEN (beats − 1) */
/* addr 는 2^size 정렬, bytes 는 2^size 의 배수라고 가정. 탐욕적으로: 이번 버스트 beat 수 =
 * min(남은 beat, 256, 4 KB 페이지 끝까지 남은 beat). 반환 = 버스트 수. */
static int split_bursts(uint32_t addr, uint32_t bytes, unsigned size, burst_t *out, int cap) {
    int n = 0;
    uint32_t nb = 1u << size, left = bytes >> size;
    while (left > 0 && n < cap) {
        uint32_t to_page = (0x1000u - (addr & 0xFFFu)) >> size;
        uint32_t beats = left < 256u ? left : 256u;
        if (beats > to_page) beats = to_page;
        out[n++] = (burst_t){ addr, beats - 1u };
        addr += beats * nb; left -= beats;
    }
    return n;
}
/* 검산기: 연속 · 각 버스트 합법 · 버스트 수가 하한(페이지 수와 256-beat 제한)과 같은지 */
static bool split_ok(uint32_t addr, uint32_t bytes, unsigned size, const burst_t *b, int n) {
    uint32_t nb = 1u << size, a = addr, total = 0;
    for (int i = 0; i < n; i++) {
        if (b[i].addr != a || b[i].len > 255u) return false;
        uint32_t last = a + (b[i].len + 1u) * nb - 1u;
        if ((a >> 12) != (last >> 12)) return false;
        a += (b[i].len + 1u) * nb; total += (b[i].len + 1u) * nb;
    }
    if (total != bytes) return false;
    /* 하한: 페이지마다 ceil(그 페이지 안의 beat / 256) 개는 꼭 필요 */
    int need = 0;
    for (uint32_t p = addr; p < addr + bytes;) {
        uint32_t pend = (p | 0xFFFu) + 1u, end = addr + bytes < pend ? addr + bytes : pend;
        uint32_t beats = (end - p) >> size;
        need += (int)((beats + 255u) / 256u);
        p = end;
    }
    return n == need;
}

/* ================= 연습 2: 허용되는 응답 순서 ================= */
/* ids[0..n-1] = 낸 순서의 ID. 응답 순서 = 0..n-1 의 순열. 같은 ID 끼리는 낸 순서대로 와야 합법. */
static bool order_legal(const uint8_t *ids, const int *order, int n) {
    int last_seen[256];
    for (int i = 0; i < 256; i++) last_seen[i] = -1;
    for (int k = 0; k < n; k++) {
        int t = order[k];
        if (t <= last_seen[ids[t]]) return false;
        last_seen[ids[t]] = t;
    }
    return true;
}
static long count_legal_enum(const uint8_t *ids, int n) {          /* 모든 순열을 돌려 센다 (n ≤ 8) */
    int a[8], c[8] = { 0 }; long cnt = 0;
    for (int i = 0; i < n; i++) a[i] = i;
    cnt += order_legal(ids, a, n);
    for (int i = 0; i < n;) {                                      /* Heap 알고리즘 */
        if (c[i] < i) {
            int j = (i % 2 == 0) ? 0 : c[i], tmp = a[j]; a[j] = a[i]; a[i] = tmp;
            cnt += order_legal(ids, a, n);
            c[i]++; i = 0;
        } else { c[i] = 0; i++; }
    }
    return cnt;
}
static long count_legal_formula(const uint8_t *ids, int n) {        /* n! / Π(ID별 개수)! */
    int c[256] = { 0 }; long num = 1, den = 1;
    for (int i = 0; i < n; i++) c[ids[i]]++;
    for (int i = 2; i <= n; i++) num *= i;
    for (int k = 0; k < 256; k++) for (int i = 2; i <= c[k]; i++) den *= i;
    return num / den;
}

/* ================= 연습 3: 의존 그래프와 데드락 ================= */
enum { AWV, AWR, WV, WR, NSIG };
typedef struct { bool e[NSIG][NSIG]; } dep_t;     /* e[a][b] = 신호 a 를 올리려면 b 가 먼저 올라와야 한다 */
static bool dfs(const dep_t *g, int u, int *st) {
    st[u] = 1;
    for (int v = 0; v < NSIG; v++) {
        if (!g->e[u][v]) continue;
        if (st[v] == 1 || (st[v] == 0 && dfs(g, v, st))) return true;
    }
    st[u] = 2;
    return false;
}
static bool deadlock_possible(const dep_t *g) {
    int st[NSIG] = { 0 };
    for (int u = 0; u < NSIG; u++) if (st[u] == 0 && dfs(g, u, st)) return true;
    return false;
}
/* 스펙이 서브(slave)에게 허용하는 대기: xREADY 는 AWVALID 나 WVALID (또는 둘 다)를 기다려도 된다 */
static bool sub_edge_legal(int a, int b) { return (a == AWR || a == WR) && (b == AWV || b == WV); }
/* 매니저에게 허용되는 대기: VALID 가 READY 를 기다리면 안 된다 (어떤 READY 든) */
static bool mgr_edge_legal(int a, int b) { return !((a == AWV || a == WV) && (b == AWR || b == WR)); }

/* ================= 연습 4: Little 의 법칙 ================= */
/* 대역폭 bw_mbps (MB/s, 10^6 B/s) 를 왕복 지연 lat_ns 에서 내려면 동시에 날아다닐 바이트 = bw × lat.
 * 버스트 하나가 burst_bytes 이면 outstanding = ceil(bw × lat / burst). 정수로: (MB/s × ns) / 1000 = 바이트 */
static unsigned ot_for_bw(uint64_t bw_mbps, uint64_t lat_ns, uint64_t burst_bytes) {
    uint64_t inflight = (bw_mbps * lat_ns + 999u) / 1000u;
    return (unsigned)((inflight + burst_bytes - 1u) / burst_bytes);
}

int main(void) {
    /* ---------- 연습 1 ---------- */
    burst_t b[256];
    int n = split_bursts(0x0FF0, 64, 2, b, 256);                    /* 페이지 끝 16 B + 다음 페이지 48 B */
    CHECK(n == 2 && b[0].addr == 0x0FF0 && b[0].len == 3 && b[1].addr == 0x1000 && b[1].len == 11);
    n = split_bursts(0x2000, 8192, 4, b, 256);                      /* 16 B beat: 4 KB = 256 beat 딱 하나씩 */
    CHECK(n == 2 && b[0].len == 255 && b[1].addr == 0x3000);
    n = split_bursts(0x2000, 4096, 0, b, 256);                      /* 1 B beat: 4096 beat → 256 제한으로 16 개 */
    CHECK(n == 16 && b[15].addr == 0x2F00 && b[15].len == 255);
    {
        uint32_t r = 0xB5u; int ok = 1;
        for (int i = 0; i < 5000; i++) {
            unsigned size = xs32(&r) % 5u; uint32_t nb = 1u << size;
            uint32_t addr = (xs32(&r) & 0xFFFFFu) & ~(nb - 1u), bytes = (xs32(&r) % 20000u + 1u) * nb;
            n = split_bursts(addr, bytes, size, b, 256);
            if (!split_ok(addr, bytes, size, b, n)) ok = 0;
        }
        CHECK(ok);
    }

    /* ---------- 연습 2 ---------- */
    {
        const uint8_t ids[] = { 0, 0, 1 };
        int good[] = { 0, 2, 1 }, bad[] = { 1, 0, 2 };
        CHECK(order_legal(ids, good, 3) && !order_legal(ids, bad, 3));
        CHECK(count_legal_enum(ids, 3) == 3 && count_legal_formula(ids, 3) == 3);
        const uint8_t one[] = { 5, 5, 5, 5, 5, 5 }, all[] = { 0, 1, 2, 3, 4, 5 }, mix[] = { 0, 1, 0, 2, 1, 0, 3, 2 };
        CHECK(count_legal_enum(one, 6) == 1 && count_legal_enum(all, 6) == 720);
        CHECK(count_legal_enum(mix, 8) == count_legal_formula(mix, 8) && count_legal_formula(mix, 8) == 1680);
    }

    /* ---------- 연습 3 ---------- */
    {
        dep_t g = { 0 };
        g.e[AWR][AWV] = g.e[WR][WV] = true;                           /* 게으른(합법) 서브 */
        CHECK(!deadlock_possible(&g));
        g.e[WV][AWR] = true;                                          /* 버그 매니저: 주소가 받아들여져야 데이터를 냄 */
        CHECK(!deadlock_possible(&g));                                /* 이 서브와는 우연히 동작 — 버그가 숨는다 */
        g.e[AWR][WV] = true;                                          /* 다른 합법 서브: 데이터를 봐야 주소를 받음 */
        CHECK(deadlock_possible(&g));                                 /* AWR → WV → AWR */
        CHECK(sub_edge_legal(AWR, WV) && !mgr_edge_legal(WV, AWR));   /* 사이클의 책임은 매니저 쪽 */
        dep_t h = { 0 };                                              /* 합법 간선만 전부 넣어도 사이클 없음 */
        for (int a = 0; a < NSIG; a++) for (int c = 0; c < NSIG; c++) if (a != c && sub_edge_legal(a, c)) h.e[a][c] = true;
        CHECK(!deadlock_possible(&h));
    }

    /* ---------- 연습 4 ---------- */
    CHECK(ot_for_bw(25600, 100, 64) == 40);       /* 25.6 GB/s, 100 ns, 64 B → 2560 B 비행 중 → 40 개 */
    CHECK(ot_for_bw(1600, 100, 64) == 3);         /* 1.6 GB/s 면 160 B → 3 개 (2.5 올림) */
    CHECK(ot_for_bw(400, 50, 32) == 1);           /* 20 B < 한 버스트 */
    CHECK(ot_for_bw(51200, 120, 64) == 96);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
