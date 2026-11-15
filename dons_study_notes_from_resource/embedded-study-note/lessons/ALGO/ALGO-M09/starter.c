/* ALGO-M09 그리디: 구간 스케줄링과 최소 자원 수
 *
 * 모든 구간은 반개구간 [s, e), s < e, uint32_t 시각(예: µs 틱). [1,3) 과 [3,5) 는 겹치지 않는다(맞닿음).
 *   Part A  최대 비겹침 선택: 끝 시각이 가장 이른 것부터 (교환 논증)  +  구간 병합 (맞닿으면 합친다)
 *   Part B  최대 겹침 깊이: 시작/끝 각각 정렬 + 두 포인터 스위프, 같은 시각이면 끝을 먼저 — 자원 수의 하한
 *   Part C  DMA 채널 배정: 시작 순서 + (끝 시각, 채널) 최소 힙 → 채널 수 = 깊이 (최적), 하드웨어 채널 초과 검출
 *
 * 주어진 도구(TODO 아님): 인덱스 힙 정렬 sort_idx, sort_u32 / 최소 힙 slot_push, slot_pop (DS-B12에서 자세히).
 * 테스트: 격자 위 모든 작은 인스턴스(전수) + xorshift32 무작위(n <= 12)를 브루트포스(부분집합 전수,
 *         셀 비트맵, 백트래킹 최소 색칠)와 대조, n = 1000 무작위를 O(n^2) 기준 구현과 대조(배율·오프셋
 *         변환으로 UINT32_MAX 근처까지), 잘못된 그리디(이른 시작, 짧은 것 먼저, 충돌 적은 것 먼저)의 실패율 출력.
 * 알고리즘 쪽은 malloc 없음, 재귀 없음. 정렬 작업 공간은 호출자가 준다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint32_t s, e; } ival_t;   /* 반개구간 [s, e), s < e */

#ifndef MAX_IV
#define MAX_IV 1024        /* 인덱스는 uint16_t (최대 65535) */
#endif
#ifndef MAX_CH
#define MAX_CH 16          /* dma_assign 의 힙 크기 = 다룰 수 있는 최대 채널 수 (스택 16 x 8 B) */
#endif

/* ======================= 주어진 도구: 정렬과 힙 (TODO 아님) ======================= */
enum { BY_END = 0, BY_START = 1 };

/* BY_END: (e, s, 인덱스) 사전순,  BY_START: (s, e, 인덱스) 사전순 — 전순서라 결과가 결정적이다 */
static int idx_less(const ival_t *iv, uint16_t a, uint16_t b, int key) {
    uint32_t a1 = key == BY_END ? iv[a].e : iv[a].s, b1 = key == BY_END ? iv[b].e : iv[b].s;
    uint32_t a2 = key == BY_END ? iv[a].s : iv[a].e, b2 = key == BY_END ? iv[b].s : iv[b].e;
    if (a1 != b1) return a1 < b1;
    if (a2 != b2) return a2 < b2;
    return a < b;
}
static void idx_sift(const ival_t *iv, uint16_t *h, size_t i, size_t n, int key) {
    for (;;) {
        size_t c = 2 * i + 1;
        if (c >= n) return;
        if (c + 1 < n && idx_less(iv, h[c], h[c + 1], key)) c++;
        if (!idx_less(iv, h[i], h[c], key)) return;
        uint16_t t = h[i]; h[i] = h[c]; h[c] = t; i = c;
    }
}
/* idx[0..n) = 0..n-1 을 key 순서로 정렬 (힙 정렬: O(n log n), 추가 메모리 O(1), 재귀 없음) */
void sort_idx(const ival_t *iv, size_t n, uint16_t *idx, int key) {
    for (size_t i = 0; i < n; i++) idx[i] = (uint16_t)i;
    for (size_t i = n / 2; i-- > 0;) idx_sift(iv, idx, i, n, key);
    for (size_t m = n; m > 1; m--) {
        uint16_t t = idx[0]; idx[0] = idx[m - 1]; idx[m - 1] = t;
        idx_sift(iv, idx, 0, m - 1, key);
    }
}
static void u32_sift(uint32_t *h, size_t i, size_t n) {
    for (;;) {
        size_t c = 2 * i + 1;
        if (c >= n) return;
        if (c + 1 < n && h[c] < h[c + 1]) c++;
        if (h[i] >= h[c]) return;
        uint32_t t = h[i]; h[i] = h[c]; h[c] = t; i = c;
    }
}
void sort_u32(uint32_t *a, size_t n) {
    for (size_t i = n / 2; i-- > 0;) u32_sift(a, i, n);
    for (size_t m = n; m > 1; m--) { uint32_t t = a[0]; a[0] = a[m - 1]; a[m - 1] = t; u32_sift(a, 0, m - 1); }
}

/* 채널 하나 = (그 채널의 마지막 전송이 끝나는 시각, 채널 번호). (end, ch) 사전순 최소 힙 */
typedef struct { uint32_t end; uint8_t ch; } slot_t;
static int slot_less(slot_t a, slot_t b) { return a.end != b.end ? a.end < b.end : a.ch < b.ch; }
void slot_push(slot_t *h, unsigned *hn, slot_t v) {
    unsigned i = (*hn)++;
    while (i > 0) {
        unsigned p = (i - 1) / 2;
        if (!slot_less(v, h[p])) break;
        h[i] = h[p]; i = p;
    }
    h[i] = v;
}
slot_t slot_pop(slot_t *h, unsigned *hn) {
    slot_t top = h[0], last = h[--(*hn)];
    unsigned i = 0, n = *hn;
    for (;;) {
        unsigned c = 2 * i + 1;
        if (c >= n) break;
        if (c + 1 < n && slot_less(h[c + 1], h[c])) c++;
        if (!slot_less(h[c], last)) break;
        h[i] = h[c]; i = c;
    }
    if (n > 0) h[i] = last;
    return top;
}

/* ======================= Part A: 최대 비겹침 선택 / 병합 ======================= */
/* 서로 겹치지 않는 구간을 최대 개수 고른다. pick[] 에 원래 인덱스를 시간 순으로 쓰고 개수를 돌려준다.
 * work 는 n 칸 작업 공간. 불변식: last_end = 지금까지 고른 구간 중 마지막 것의 끝 (처음엔 0). */
size_t max_nonoverlap(const ival_t *iv, size_t n, uint16_t *work, uint16_t *pick) {
    (void)iv; (void)n; (void)work; (void)pick;
    return 0; /* TODO: work 를 BY_END 로 정렬, last_end = 0, s >= last_end 면 고른다 */
}

/* 겹치거나 맞닿는 구간을 합쳐 out[] 에 시작 순으로 쓴다 (out[k].e < out[k+1].s 가 성립). 개수를 돌려준다.
 * out 은 iv 와 겹치면 안 된다. 불변식: out[0..m] 은 지금까지 본 구간들의 합집합, out[m] 이 열린 마지막 덩어리. */
size_t merge_intervals(const ival_t *iv, size_t n, uint16_t *work, ival_t *out) {
    (void)iv; (void)n; (void)work; (void)out;
    return 0; /* TODO: BY_START 로 정렬, c.s <= out[m].e 면 늘리고 아니면 새 덩어리 */
}

/* ======================= Part B: 최대 겹침 깊이 (스위프) ======================= */
/* 어떤 시각에 동시에 활성인 구간 수의 최댓값. *at = 그 최댓값에 처음 도달한 시각 (n == 0 이면 0).
 * starts/ends 는 n 칸 작업 공간. 같은 시각에 끝과 시작이 겹치면 끝을 먼저 처리한다 ([s,e) 는 e 를 포함 안 함). */
unsigned max_depth(const ival_t *iv, size_t n, uint32_t *starts, uint32_t *ends, uint32_t *at) {
    (void)iv; (void)n; (void)starts; (void)ends;
    *at = 0;
    return 0; /* TODO: starts/ends 를 따로 정렬, 두 포인터, starts[i] < ends[j] 면 +1 아니면 -1 */
}

/* ======================= Part C: DMA 채널 배정 ======================= */
/* 전송 x[i] 를 채널 chan[i] (0..k-1) 에 배정하고 쓴 채널 수 k 를 돌려준다. 한 채널의 전송은 서로 겹치지 않는다.
 * k 는 최소(= max_depth). hw_ch 개로 부족하면 -1. 힙은 채널마다 슬롯 하나 = (마지막 끝 시각, 채널). */
int dma_assign(const ival_t *x, size_t n, unsigned hw_ch, uint16_t *work, uint8_t *chan) {
    (void)x; (void)n; (void)hw_ch; (void)work; (void)chan;
    return 0; /* TODO: BY_START 순서로, 힙 top 의 end <= s 면 그 채널 재사용, 아니면 새 채널 (hw_ch 초과면 -1) */
}

#ifndef ALGO_M09_NO_MAIN
/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2545F491u;
static uint32_t xorshift32(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}

/* ---- 브루트포스 / 기준 구현 (테스트 쪽) ---- */
static int ov(ival_t a, ival_t b) { return a.s < b.e && b.s < a.e; }

/* 부분집합 전수: 서로 안 겹치는 최대 개수 (n <= 16) */
static unsigned brute_max_nonoverlap(const ival_t *iv, size_t n) {
    uint32_t conf[16] = { 0 };
    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < n; j++)
            if (i != j && ov(iv[i], iv[j])) conf[i] |= 1u << j;
    unsigned best = 0;
    for (uint32_t mask = 0; mask < (1u << n); mask++) {
        int ok = 1;
        unsigned cnt = 0;
        for (size_t i = 0; i < n && ok; i++)
            if (mask >> i & 1) { cnt++; if (conf[i] & mask) ok = 0; }
        if (ok && cnt > best) best = cnt;
    }
    return best;
}
/* pick[] 가 유효한 선택인가: 인덱스 범위 + 서로 안 겹침 (같은 인덱스 두 번이면 자기 자신과 겹쳐서 걸린다) */
static int picks_valid(const ival_t *iv, size_t n, const uint16_t *pick, size_t k) {
    for (size_t a = 0; a < k; a++) {
        if (pick[a] >= n) return 0;
        for (size_t b = a + 1; b < k; b++)
            if (pick[b] >= n || ov(iv[pick[a]], iv[pick[b]])) return 0;
    }
    return 1;
}
/* 단위 가중치 DP (끝 순서): dp[i] = max(dp[i-1], 1 + dp[p(i)]), p(i) = e <= s_i 인 앞쪽 개수. O(n^2), 그리디와 무관 */
static unsigned ref_dp_max(const ival_t *iv, size_t n) {
    static uint16_t ord[MAX_IV];
    static unsigned dp[MAX_IV + 1];
    for (size_t i = 0; i < n; i++) ord[i] = (uint16_t)i;
    for (size_t i = 1; i < n; i++) {           /* 삽입 정렬 (끝 기준) — 테스트 쪽이라 단순하게 */
        uint16_t v = ord[i]; size_t j = i;
        while (j > 0 && iv[ord[j - 1]].e > iv[v].e) { ord[j] = ord[j - 1]; j--; }
        ord[j] = v;
    }
    dp[0] = 0;
    for (size_t i = 1; i <= n; i++) {
        ival_t c = iv[ord[i - 1]];
        size_t p = 0;
        while (p < i - 1 && iv[ord[p]].e <= c.s) p++;
        unsigned take = 1 + dp[p];
        dp[i] = take > dp[i - 1] ? take : dp[i - 1];
    }
    return dp[n];
}
static unsigned depth_at(const ival_t *iv, size_t n, uint32_t t) {
    unsigned c = 0;
    for (size_t i = 0; i < n; i++) c += (iv[i].s <= t && t < iv[i].e);
    return c;
}
/* 격자 [0, G) 의 모든 정수 시각에서 깊이를 센다. 반개구간·정수 끝점이면 정수 시각만 보면 충분하다 */
static unsigned brute_depth_grid(const ival_t *iv, size_t n, uint32_t G, uint32_t *first_at) {
    unsigned best = 0;
    *first_at = 0;
    for (uint32_t t = 0; t < G; t++) {
        unsigned d = depth_at(iv, n, t);
        if (d > best) { best = d; *first_at = t; }
    }
    return best;
}
/* 셀 비트맵: [t, t+1) 칸을 칠하고 연속 구간(run)을 읽는다 = 맞닿음까지 합친 합집합 */
#define GRID_MAX 4096
static size_t brute_merge_grid(const ival_t *iv, size_t n, uint32_t G, ival_t *out) {
    static unsigned char cell[GRID_MAX];
    memset(cell, 0, G);
    for (size_t i = 0; i < n; i++)
        for (uint32_t t = iv[i].s; t < iv[i].e; t++) cell[t] = 1;
    size_t m = 0;
    for (uint32_t t = 0; t < G;) {
        if (!cell[t]) { t++; continue; }
        uint32_t s = t;
        while (t < G && cell[t]) t++;
        out[m++] = (ival_t){ s, t };
    }
    return m;
}
static int same_ivals(const ival_t *a, size_t na, const ival_t *b, size_t nb) {
    if (na != nb) return 0;
    for (size_t i = 0; i < na; i++) if (a[i].s != b[i].s || a[i].e != b[i].e) return 0;
    return 1;
}
/* 최소 색칠(채널 수) 백트래킹 — 정리(채널 수 = 깊이)를 쓰지 않는 독립 기준 */
static int color_bt(const ival_t *iv, size_t n, size_t i, unsigned k, unsigned used, uint8_t *col) {
    if (i == n) return 1;
    for (unsigned c = 0; c < k && c <= used; c++) {   /* 대칭 제거: 새 색은 used 번째만 */
        int ok = 1;
        for (size_t j = 0; j < i && ok; j++) if (col[j] == c && ov(iv[i], iv[j])) ok = 0;
        if (!ok) continue;
        col[i] = (uint8_t)c;
        if (color_bt(iv, n, i + 1, k, c == used ? used + 1 : used, col)) return 1;
    }
    return 0;
}
static unsigned brute_chromatic(const ival_t *iv, size_t n) {
    uint8_t col[16];
    for (unsigned k = 0;; k++) if (color_bt(iv, n, 0, k, 0, col)) return k;
}
/* 채널 배정 검증: 모든 chan[i] < k, 같은 채널의 두 전송은 안 겹침 */
static int chan_valid(const ival_t *x, size_t n, const uint8_t *chan, unsigned k) {
    for (size_t i = 0; i < n; i++) {
        if (chan[i] >= k) return 0;
        for (size_t j = i + 1; j < n; j++) if (chan[i] == chan[j] && ov(x[i], x[j])) return 0;
    }
    return 1;
}

/* 잘못된(또는 옳은) 그리디 규칙: 남은 것 중 규칙상 최선 하나를 고르고, 그것과 겹치는 것을 모두 지운다 (n <= 16) */
enum { R_EARLIEST_END, R_EARLIEST_START, R_SHORTEST, R_FEWEST_CONFLICTS, R_LATEST_START, R_COUNT };
static const char *const rule_name[R_COUNT] = {
    "earliest end", "earliest start", "shortest first", "fewest conflicts", "latest start" };
static unsigned greedy_rule(const ival_t *iv, size_t n, int rule) {
    uint32_t alive = (n == 32) ? 0xFFFFFFFFu : ((1u << n) - 1u);
    unsigned cnt = 0;
    while (alive) {
        int best = -1;
        uint64_t bkey = 0;
        for (size_t i = 0; i < n; i++) {
            if (!(alive >> i & 1)) continue;
            uint64_t key;
            switch (rule) {
            case R_EARLIEST_END:   key = (uint64_t)iv[i].e; break;
            case R_EARLIEST_START: key = (uint64_t)iv[i].s; break;
            case R_SHORTEST:       key = (uint64_t)(iv[i].e - iv[i].s); break;
            case R_LATEST_START:   key = (uint64_t)(UINT32_MAX - iv[i].s); break;
            default: {             /* 충돌 수가 적은 것, 같으면 끝이 이른 것 (유리한 쪽으로 깬다) */
                unsigned c = 0;
                for (size_t j = 0; j < n; j++) if (j != i && (alive >> j & 1) && ov(iv[i], iv[j])) c++;
                key = ((uint64_t)c << 32) | iv[i].e;
            } }
            if (best < 0 || key < bkey) { best = (int)i; bkey = key; }
        }
        cnt++;
        for (size_t j = 0; j < n; j++) if (ov(iv[best], iv[j])) alive &= ~(1u << j);
    }
    return cnt;
}

static ival_t g_iv[MAX_IV], g_tmp[MAX_IV], g_out[MAX_IV], g_ref[GRID_MAX];
static uint16_t g_work[MAX_IV], g_pick[MAX_IV];
static uint32_t g_s[MAX_IV], g_e[MAX_IV];
static uint8_t g_chan[MAX_IV];

/* 한 인스턴스(좌표 < G)를 네 함수 모두로 돌려 브루트포스와 대조. bad[] 에 함수별 불일치를 더한다 */
enum { F_SEL, F_MERGE, F_DEPTH, F_DMA, F_COUNT };
static void check_small(const ival_t *iv, size_t n, uint32_t G, unsigned long *bad) {
    size_t k = max_nonoverlap(iv, n, g_work, g_pick);
    if (k != brute_max_nonoverlap(iv, n) || !picks_valid(iv, n, g_pick, k)) bad[F_SEL]++;

    size_t m = merge_intervals(iv, n, g_work, g_out);
    size_t mr = brute_merge_grid(iv, n, G, g_ref);
    if (!same_ivals(g_out, m, g_ref, mr)) bad[F_MERGE]++;

    uint32_t at = 12345, at_ref;
    unsigned d = max_depth(iv, n, g_s, g_e, &at);
    unsigned dr = brute_depth_grid(iv, n, G, &at_ref);
    if (d != dr || (n > 0 && at != at_ref)) bad[F_DEPTH]++;

    memset(g_chan, 0xFF, n);
    int used = dma_assign(iv, n, MAX_CH, g_work, g_chan);
    unsigned chrom = brute_chromatic(iv, n);
    if (used < 0 || (unsigned)used != chrom || !chan_valid(iv, n, g_chan, (unsigned)used) || chrom != dr) bad[F_DMA]++;
}

int main(void) {
    /* ---- Part A: 이름 붙은 예제 ---- */
    {   /* CLRS 16.1 활동 선택 예제 (11개, 반개구간 [s, f)) → 최대 4개 {a1, a4, a8, a11} */
        static const uint32_t s[11] = { 1, 3, 0, 5, 3, 5, 6, 8, 8, 2, 12 };
        static const uint32_t f[11] = { 4, 5, 6, 7, 9, 9, 10, 11, 12, 14, 16 };
        ival_t a[11];
        for (int i = 0; i < 11; i++) a[i] = (ival_t){ s[i], f[i] };
        size_t k = max_nonoverlap(a, 11, g_work, g_pick);
        CHECK(k == 4 && g_pick[0] == 0 && g_pick[1] == 3 && g_pick[2] == 7 && g_pick[3] == 10);
    }
    {   /* LeetCode 435 예제: 제거 수 = n - 최대 비겹침 */
        ival_t e1[] = { {1,2}, {2,3}, {3,4}, {1,3} }, e2[] = { {1,2}, {1,2}, {1,2} }, e3[] = { {1,2}, {2,3} };
        CHECK(4 - max_nonoverlap(e1, 4, g_work, g_pick) == 1);
        CHECK(3 - max_nonoverlap(e2, 3, g_work, g_pick) == 2);
        CHECK(2 - max_nonoverlap(e3, 2, g_work, g_pick) == 0);
    }
    {   /* LeetCode 56 예제 (맞닿음도 합친다) */
        ival_t a[] = { {1,3}, {2,6}, {8,10}, {15,18} }, w1[] = { {1,6}, {8,10}, {15,18} };
        ival_t b[] = { {4,5}, {1,4} },                  w2[] = { {1,5} };
        ival_t c[] = { {1,10}, {2,3}, {4,5} },          w3[] = { {1,10} };   /* 포함된 구간: 끝을 줄이면 안 됨 */
        size_t m;
        m = merge_intervals(a, 4, g_work, g_out); CHECK(same_ivals(g_out, m, w1, 3));
        m = merge_intervals(b, 2, g_work, g_out); CHECK(same_ivals(g_out, m, w2, 1));
        m = merge_intervals(c, 3, g_work, g_out); CHECK(same_ivals(g_out, m, w3, 1));
    }
    {   /* 빈 입력, 원소 하나, UINT32_MAX 끝점 */
        uint32_t at = 99;
        CHECK(max_nonoverlap(NULL, 0, g_work, g_pick) == 0);
        CHECK(merge_intervals(NULL, 0, g_work, g_out) == 0);
        CHECK(max_depth(NULL, 0, g_s, g_e, &at) == 0 && at == 0);
        CHECK(dma_assign(NULL, 0, MAX_CH, g_work, g_chan) == 0);
        ival_t one[] = { {7, 8} };
        CHECK(max_nonoverlap(one, 1, g_work, g_pick) == 1 && g_pick[0] == 0);
        ival_t big[] = { {0, UINT32_MAX}, {UINT32_MAX - 1, UINT32_MAX}, {5, 6} };
        size_t m = merge_intervals(big, 3, g_work, g_out);
        CHECK(max_nonoverlap(big, 3, g_work, g_pick) == 2);
        CHECK(m == 1 && g_out[0].s == 0 && g_out[0].e == UINT32_MAX);
        CHECK(max_depth(big, 3, g_s, g_e, &at) == 2 && at == 5);
    }

    /* ---- Part B: 깊이 예제와 잘못된 그리디 반례 ---- */
    {
        uint32_t at = 0;
        ival_t r1[] = { {0,30}, {5,10}, {15,20} }, r2[] = { {7,10}, {2,4} };   /* LeetCode 253 예제 → 2, 1 */
        ival_t chain[] = { {2,3}, {0,1}, {1,2} };                              /* 맞닿음 사슬 → 1 */
        ival_t same[] = { {3,9}, {3,9}, {3,9}, {3,9}, {3,9} };
        CHECK(max_depth(r1, 3, g_s, g_e, &at) == 2 && at == 5);
        CHECK(max_depth(r2, 2, g_s, g_e, &at) == 1 && at == 2);
        CHECK(max_depth(chain, 3, g_s, g_e, &at) == 1 && at == 0);
        CHECK(max_depth(same, 5, g_s, g_e, &at) == 5 && at == 3);
        CHECK(dma_assign(chain, 3, MAX_CH, g_work, g_chan) == 1);
        ival_t es[] = { {0,10}, {1,2}, {3,4} };     /* 이른 시작: [0,10) 하나만 → 1, 최적 2 */
        ival_t sh[] = { {0,5}, {4,7}, {6,11} };     /* 짧은 것 먼저: [4,7) 이 양쪽을 막음 → 1, 최적 2 */
        CHECK(greedy_rule(es, 3, R_EARLIEST_START) == 1 && max_nonoverlap(es, 3, g_work, g_pick) == 2);
        CHECK(greedy_rule(sh, 3, R_SHORTEST) == 1 && max_nonoverlap(sh, 3, g_work, g_pick) == 2);
        /* 충돌 적은 것 먼저: n <= 7 전수에선 반례가 없었고, demo_counterexamples.c 무작위 탐색이 찾은 n = 11 반례 */
        ival_t fc[] = { {6,10}, {1,4}, {9,13}, {14,17}, {1,5}, {19,23}, {3,7}, {11,15}, {1,5}, {12,16}, {2,3} };
        CHECK(greedy_rule(fc, 11, R_FEWEST_CONFLICTS) == 4 && max_nonoverlap(fc, 11, g_work, g_pick) == 5);
    }

    /* ---- 전수: 격자 위 모든 작은 인스턴스 ---- */
    {
        unsigned long bad[F_COUNT] = { 0 }, cases = 0;
        ival_t cand[15];
        int nc;
        uint32_t G = 5;                      /* 0 <= s < e <= 5 : 15 가지, n = 0..3 의 모든 순서쌍 */
        nc = 0;
        for (uint32_t s = 0; s < G; s++) for (uint32_t e = s + 1; e <= G; e++) cand[nc++] = (ival_t){ s, e };
        for (size_t n = 0; n <= 3; n++) {
            size_t total = 1;
            for (size_t r = 0; r < n; r++) total *= (size_t)nc;
            for (size_t code = 0; code < total; code++) {
                size_t c = code;
                for (size_t r = 0; r < n; r++) { g_iv[r] = cand[c % (size_t)nc]; c /= (size_t)nc; }
                check_small(g_iv, n, G, bad); cases++;
            }
        }
        G = 4;                               /* 0 <= s < e <= 4 : 10 가지, n = 4 의 모든 순서쌍 10^4 */
        nc = 0;
        for (uint32_t s = 0; s < G; s++) for (uint32_t e = s + 1; e <= G; e++) cand[nc++] = (ival_t){ s, e };
        for (size_t code = 0; code < 10000; code++) {
            size_t c = code;
            for (size_t r = 0; r < 4; r++) { g_iv[r] = cand[c % 10]; c /= 10; }
            check_small(g_iv, 4, G, bad); cases++;
        }
        printf("exhaustive grid: %lu instances, mismatches select=%lu merge=%lu depth=%lu dma=%lu\n",
               cases, bad[F_SEL], bad[F_MERGE], bad[F_DEPTH], bad[F_DMA]);
        CHECK(bad[F_SEL] == 0);
        CHECK(bad[F_MERGE] == 0);
        CHECK(bad[F_DEPTH] == 0);
        CHECK(bad[F_DMA] == 0);
    }

    /* ---- 무작위 n <= 12 (좌표 < 52): 브루트포스 대조 + 그리디 규칙별 실패율 ---- */
    {
        unsigned long bad[F_COUNT] = { 0 }, cases = 0, worse[R_COUNT] = { 0 };
        unsigned gap[R_COUNT] = { 0 };
        for (int it = 0; it < 4000; it++) {
            size_t n = xorshift32() % 13;
            for (size_t i = 0; i < n; i++) {
                uint32_t s = xorshift32() % 40, len = 1 + xorshift32() % 12;
                g_iv[i] = (ival_t){ s, s + len };
            }
            check_small(g_iv, n, 52, bad); cases++;
            unsigned opt = brute_max_nonoverlap(g_iv, n);
            for (int r = 0; r < R_COUNT; r++) {
                unsigned got = greedy_rule(g_iv, n, r);
                if (got < opt) { worse[r]++; if (opt - got > gap[r]) gap[r] = opt - got; }
            }
        }
        printf("random n<=12: %lu instances, mismatches select=%lu merge=%lu depth=%lu dma=%lu\n",
               cases, bad[F_SEL], bad[F_MERGE], bad[F_DEPTH], bad[F_DMA]);
        for (int r = 0; r < R_COUNT; r++)
            printf("  rule %-17s suboptimal in %4lu / %lu (max shortfall %u)\n", rule_name[r], worse[r], cases, gap[r]);
        CHECK(bad[F_SEL] == 0);
        CHECK(bad[F_MERGE] == 0);
        CHECK(bad[F_DEPTH] == 0);
        CHECK(bad[F_DMA] == 0);
        CHECK(worse[R_EARLIEST_END] == 0 && worse[R_LATEST_START] == 0);   /* 옳은 규칙 두 개 (서로 거울상) */
        CHECK(worse[R_EARLIEST_START] > 0 && worse[R_SHORTEST] > 0);       /* 틀린 규칙은 실제로 틀린다 */
    }

    /* ---- 큰 무작위 n = 1000: O(n^2) 기준과 대조, 배율·오프셋 변환으로 큰 좌표까지 ---- */
    {
        unsigned long bad[F_COUNT] = { 0 }, cases = 0, over = 0;
        unsigned maxd = 0;
        for (int it = 0; it < 12; it++) {
            size_t n = 1000;
            uint32_t maxlen = 16 + 4 * (uint32_t)it;                /* 밀도를 바꿔 깊이가 MAX_CH 를 넘는 경우도 만든다 */
            for (size_t i = 0; i < n; i++) {
                uint32_t s = xorshift32() % 3900, len = 1 + xorshift32() % maxlen;
                g_iv[i] = (ival_t){ s, s + len };                 /* 좌표 < 4096 */
            }
            unsigned ref_sel = ref_dp_max(g_iv, n);
            uint32_t at_ref;
            unsigned ref_d = brute_depth_grid(g_iv, n, GRID_MAX, &at_ref);
            size_t ref_m = brute_merge_grid(g_iv, n, GRID_MAX, g_ref);
            if (ref_d > maxd) maxd = ref_d;
            for (int tr = 0; tr < 3; tr++) {                      /* 0: 그대로, 1: x 1e6, 2: + (UINT32_MAX - 4095) */
                uint32_t mul = tr == 1 ? 1000000u : 1u, add = tr == 2 ? UINT32_MAX - 4095u : 0u;
                for (size_t i = 0; i < n; i++) g_tmp[i] = (ival_t){ g_iv[i].s * mul + add, g_iv[i].e * mul + add };
                size_t k = max_nonoverlap(g_tmp, n, g_work, g_pick);
                if (k != ref_sel || !picks_valid(g_tmp, n, g_pick, k)) bad[F_SEL]++;
                size_t m = merge_intervals(g_tmp, n, g_work, g_out);
                int mok = (m == ref_m);
                for (size_t q = 0; mok && q < m; q++)
                    mok = g_out[q].s == g_ref[q].s * mul + add && g_out[q].e == g_ref[q].e * mul + add;
                if (!mok) bad[F_MERGE]++;
                uint32_t at;
                unsigned d = max_depth(g_tmp, n, g_s, g_e, &at);
                if (d != ref_d || at != at_ref * mul + add) bad[F_DEPTH]++;
                int used = dma_assign(g_tmp, n, MAX_CH, g_work, g_chan);
                if (ref_d > MAX_CH) { over++; if (used != -1) bad[F_DMA]++; }   /* 채널 부족 → -1 이어야 함 */
                else if (used != (int)ref_d || !chan_valid(g_tmp, n, g_chan, (unsigned)used)) bad[F_DMA]++;
                cases++;
            }
        }
        printf("random n=1000: %lu runs (3 coordinate maps), depth up to %u (%lu runs > MAX_CH=%d), mismatches select=%lu merge=%lu depth=%lu dma=%lu\n",
               cases, maxd, over, MAX_CH, bad[F_SEL], bad[F_MERGE], bad[F_DEPTH], bad[F_DMA]);
        CHECK(bad[F_SEL] == 0);
        CHECK(bad[F_MERGE] == 0);
        CHECK(bad[F_DEPTH] == 0);
        CHECK(bad[F_DMA] == 0);
        CHECK(over > 0 && over < cases);                        /* 두 경로(-1 / 배정) 모두 시험됨 */
    }

    /* ---- Part C: 1 ms 프레임 안의 DMA 전송 (µs) ---- */
    {
        static const char *const name[] = {
            "ADC1 scan->RAM", "SPI1 IMU RX", "SPI1 IMU TX", "UART2 GPS RX", "I2S audio TX",
            "SPI2 flash rd", "UART1 log TX", "I2C1 baro RX", "memcpy frame", "SPI2 flash wr" };
        static const ival_t xf[10] = {
            {   0, 400 }, {  50, 120 }, {  50, 120 }, {  0, 1000 }, { 100, 600 },
            { 130, 380 }, { 400, 900 }, { 380, 450 }, { 600, 800 }, { 450, 700 } };
        size_t n = 10;
        memset(g_chan, 0xFF, n);
        int used = dma_assign(xf, n, 8, g_work, g_chan);
        uint32_t at;
        unsigned d = max_depth(xf, n, g_s, g_e, &at);
        printf("Part C frame: %zu transfers, channels used=%d, depth=%u at t=%u us, brute chromatic=%u\n",
               n, used, d, at, brute_chromatic(xf, n));
        for (size_t i = 0; i < n; i++)
            printf("  ch%u  [%4u, %4u)  %s\n", (unsigned)g_chan[i], xf[i].s, xf[i].e, name[i]);
        CHECK(used == (int)d && (unsigned)used == brute_chromatic(xf, n));
        CHECK(used > 0 && chan_valid(xf, n, g_chan, (unsigned)used));
        CHECK(dma_assign(xf, n, d - 1, g_work, g_chan) == -1);   /* 채널이 하나 모자라면 거절 */
        CHECK(dma_assign(xf, n, d, g_work, g_chan) == (int)d);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* ALGO_M09_NO_MAIN */
