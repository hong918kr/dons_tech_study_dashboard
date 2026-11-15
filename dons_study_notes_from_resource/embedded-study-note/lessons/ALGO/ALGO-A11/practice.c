/* ALGO-A11 추가 연습 — 한 번에 검증
 *   연습 1  GCRA (가상 스케줄링, 상태 = TAT 하나) 가 토큰 버킷과 결정이 한 건도 다르지 않음을 무작위 트래픽으로
 *   연습 2  Linux ___ratelimit 식 "첫 이벤트에 고정되는 창": 전수 탐색으로 최악 2틱 창 = 2b−1, (I+2)틱 창 = 2b
 *   연습 3  셰이퍼(버리지 않고 늦추기): 출발 시각을 닫힌 식으로 계산 → 틱 단위 시뮬레이션과 대조 + 출력이 (b, r) 준수
 *   연습 4  보수적 K-슬롯 창 카운터: 메모리 K+1 칸으로 "어느 창도 N 초과 없음" 보장, 처리량 손실을 K 별로 측정
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 2463534242u;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }
static uint32_t rnd(uint32_t n) { return xs32() % n; }

/* ---- 본문 토큰 버킷 (solution.c 와 같은 코드, 자급자족을 위해 복사) ---- */
typedef struct { uint32_t level, cap, num, den, burst, full_ticks, last; } tbucket_t;
static bool tb_init(tbucket_t *tb, uint32_t burst, uint32_t num, uint32_t den, uint32_t now) {
    memset(tb, 0, sizeof *tb);
    if (burst == 0 || num == 0 || den == 0 || (uint64_t)burst * den > 0x7FFFFFFFu) return false;
    tb->burst = burst; tb->num = num; tb->den = den; tb->cap = burst * den;
    tb->full_ticks = tb->cap / num + (tb->cap % num != 0);
    tb->level = tb->cap; tb->last = now;
    return true;
}
static void tb_refill(tbucket_t *tb, uint32_t now) {
    uint32_t dt = now - tb->last;
    if (dt == 0 || dt > 0x7FFFFFFFu) return;
    tb->last = now;
    if (dt >= tb->full_ticks) { tb->level = tb->cap; return; }
    uint32_t lv = tb->level + dt * tb->num;
    tb->level = lv < tb->cap ? lv : tb->cap;
}
static bool tb_try(tbucket_t *tb, uint32_t now, uint32_t cost) {
    tb_refill(tb, now);
    if (cost > tb->burst) return false;
    uint32_t need = cost * tb->den;
    if (tb->level < need) return false;
    tb->level -= need;
    return true;
}

/* 통과/출발 로그에서 모든 창 [t_i, t_j] 가 den·Σ ≤ den·b + num·(t_j − t_i) 인가 (O(n²)) */
static bool all_windows_ok(const uint64_t *at, const uint32_t *ac, int na, uint32_t b, uint32_t num, uint32_t den) {
    for (int i = 0; i < na; i++) {
        uint64_t sum = 0;
        for (int j = i; j < na; j++) {
            sum += ac[j];
            if ((uint64_t)den * sum > (uint64_t)den * b + (uint64_t)num * (at[j] - at[i])) return false;
        }
    }
    return true;
}
static uint64_t max_in_window(const uint64_t *at, int na, uint64_t L) {
    uint64_t best = 0; int i = 0;
    for (int j = 0; j < na; j++) { while (at[j] - at[i] >= L) i++; if ((uint64_t)(j - i + 1) > best) best = (uint64_t)(j - i + 1); }
    return best;
}

/* ---------- 연습 1: GCRA ----------
 * 시간을 num 배로 스케일 (1 틱 = num 단위, 토큰 1개 = den 단위). TAT = "이론적 도착 시각".
 * x = max(TAT − s·num, 0) 이 "밀린 양" — 토큰 버킷의 cap − level 과 같다.  통과 ⇔ x + cost·den ≤ b·den. */
typedef struct { uint64_t tat; uint32_t b, num, den; } gcra_t;
static bool gcra_try(gcra_t *g, uint64_t s, uint32_t cost) {
    uint64_t now = s * g->num;
    uint64_t base = g->tat > now ? g->tat : now;
    if (base - now + (uint64_t)cost * g->den > (uint64_t)g->b * g->den) return false;
    g->tat = base + (uint64_t)cost * g->den;
    return true;
}
static void ex1(void) {
    static const uint32_t prm[][3] = { {1, 1, 7}, {4, 3, 5}, {8, 2, 1}, {16, 1, 64}, {5, 5, 9}, {100, 1, 10} };
    int mism = 0, total = 0, acc = 0;
    for (uint32_t seed = 0; seed < 60; seed++) {
        const uint32_t *q = prm[seed % 6];
        g_rng = 300 + seed;
        tbucket_t tb; (void)tb_init(&tb, q[0], q[1], q[2], 0xFFFFF000u);
        gcra_t g = { 0, q[0], q[1], q[2] };
        for (uint64_t t = 0; t < 4000; t++) {
            int k = rnd(3) == 0 ? 1 + (int)rnd(4) : 0;
            while (k--) {
                uint32_t cost = 1 + rnd(q[0]);
                bool a = tb_try(&tb, (uint32_t)(0xFFFFF000u + t), cost), b = gcra_try(&g, t, cost);
                mism += (a != b); total++; acc += a;
            }
        }
    }
    printf("ex1 GCRA vs token bucket: %d decisions (%d accepted), mismatches=%d\n", total, acc, mism);
    CHECK(total > 100000 && mism == 0);
}

/* ---------- 연습 2: Linux 식 고정(첫 이벤트 고정) 창 ----------
 * ___ratelimit 모델: 처음 호출 때 begin = now, 남은 수 = b. 호출 시 now > begin + I 면 begin = now, 남은 수 = b (리셋).
 * 남은 수 > 0 이면 통과(−1). 창 하나는 [begin, begin + I] (I+1 틱). 거부된 호출도 리셋은 일으킨다. */
typedef struct { bool init; uint64_t begin; uint32_t left, b, I; } lrl_t;
static bool lrl_try(lrl_t *r, uint64_t now) {
    if (!r->init) { r->init = true; r->begin = now; r->left = r->b; }
    if (now > r->begin + r->I) { r->begin = now; r->left = r->b; }
    if (r->left > 0) { r->left--; return true; }
    return false;
}
/* 길이 H 틱, 틱마다 0..b 개 호출하는 모든 패턴을 전수 → 통과 로그의 길이 L 창 최대값 */
static void lrl_search(uint32_t b, uint32_t I, int H, uint64_t *max2, uint64_t *maxI1, uint64_t *maxI2) {
    int cnt[16];
    uint64_t at[256];
    *max2 = *maxI1 = *maxI2 = 0;
    uint64_t combos = 1;
    for (int i = 0; i < H; i++) combos *= (b + 1);
    for (uint64_t c = 0; c < combos; c++) {
        uint64_t x = c;
        for (int i = 0; i < H; i++) { cnt[i] = (int)(x % (b + 1)); x /= (b + 1); }
        lrl_t r = { false, 0, 0, b, I };
        int na = 0;
        for (int t = 0; t < H; t++) for (int k = 0; k < cnt[t]; k++) if (lrl_try(&r, (uint64_t)t)) at[na++] = (uint64_t)t;
        uint64_t m;
        if ((m = max_in_window(at, na, 2)) > *max2) *max2 = m;
        if ((m = max_in_window(at, na, I + 1)) > *maxI1) *maxI1 = m;
        if ((m = max_in_window(at, na, I + 2)) > *maxI2) *maxI2 = m;
    }
}
static void ex2(void) {
    static const uint32_t cfg[][2] = { {1, 1}, {2, 1}, {2, 2}, {3, 1}, {3, 3}, {2, 4}, {4, 2} };   /* (b, I) */
    for (int i = 0; i < 7; i++) {
        uint32_t b = cfg[i][0], I = cfg[i][1];
        int H = (int)I + 4;
        uint64_t m2, mI1, mI2;
        lrl_search(b, I, H, &m2, &mI1, &mI2);
        printf("ex2 linux-style b=%u I=%u (H=%d, %u^%d patterns): max in 2 ticks=%llu (2b-1=%u)  in I+1=%llu  in I+2=%llu (2b=%u)\n",
               b, I, H, b + 1, H, (unsigned long long)m2, 2 * b - 1, (unsigned long long)mI1, (unsigned long long)mI2, 2 * b);
        CHECK(m2 == 2 * b - 1 && mI1 == 2 * b - 1 && mI2 == 2 * b);
    }
}

/* ---------- 연습 3: 셰이퍼 ----------
 * 버킷에 cost 토큰이 모일 때까지 기다릴 틱 수 (지금 되면 0). 나눗셈 1번 — 셰이퍼는 패킷당 한 번만 부른다. */
static uint32_t tb_wait(tbucket_t *tb, uint32_t now, uint32_t cost) {
    tb_refill(tb, now);
    uint32_t need = cost * tb->den;
    if (tb->level >= need) return 0;
    return (need - tb->level + tb->num - 1) / tb->num;
}
#define NPK 3000
static uint64_t g_arr[NPK], g_dep[NPK], g_dep2[NPK];
static uint32_t g_cost[NPK];
static void ex3(void) {
    static const uint32_t prm[][3] = { {4, 1, 3}, {10, 7, 10}, {1, 1, 5}, {6, 5, 2} };
    int bad = 0, viol = 0, runs = 0;
    uint64_t maxdelay = 0;
    for (uint32_t seed = 0; seed < 20; seed++) {
        const uint32_t *q = prm[seed % 4];
        g_rng = 700 + seed;
        uint64_t t = 0;
        for (int i = 0; i < NPK; i++) { t += rnd(4) == 0 ? rnd(30) : 0; g_arr[i] = t; g_cost[i] = 1 + rnd(q[0]); }
        /* (a) 닫힌 식: 앞 패킷이 떠난 뒤 + 버킷이 찰 때까지 */
        tbucket_t tb; (void)tb_init(&tb, q[0], q[1], q[2], 0xFFFFFF00u);
        uint64_t prev = 0;
        for (int i = 0; i < NPK; i++) {
            uint64_t s = g_arr[i] > prev ? g_arr[i] : prev;
            s += tb_wait(&tb, (uint32_t)(0xFFFFFF00u + s), g_cost[i]);
            if (!tb_try(&tb, (uint32_t)(0xFFFFFF00u + s), g_cost[i])) bad++;   /* 기다린 뒤엔 반드시 통과 */
            g_dep[i] = prev = s;
            if (s - g_arr[i] > maxdelay) maxdelay = s - g_arr[i];
        }
        /* (b) 기준: 틱마다 FIFO 머리를 가능한 만큼 내보내는 시뮬레이션 */
        tbucket_t tb2; (void)tb_init(&tb2, q[0], q[1], q[2], 0xFFFFFF00u);
        int h = 0;
        for (uint64_t tick = 0; h < NPK; tick++)
            while (h < NPK && g_arr[h] <= tick && tb_try(&tb2, (uint32_t)(0xFFFFFF00u + tick), g_cost[h])) g_dep2[h++] = tick;
        for (int i = 0; i < NPK; i++) bad += (g_dep[i] != g_dep2[i]);
        viol += !all_windows_ok(g_dep, g_cost, NPK, q[0], q[1], q[2]);
        runs++;
    }
    printf("ex3 shaper: %d runs x %d packets, closed-form vs tick sim mismatches=%d, output window violations=%d, max delay=%llu ticks\n",
           runs, NPK, bad, viol, (unsigned long long)maxdelay);
    CHECK(bad == 0);
    CHECK(viol == 0);
}

/* ---------- 연습 4: 보수적 K-슬롯 창 카운터 ----------
 * 슬롯 크기 S = W/K (K | W). 지금 슬롯 + 직전 K 슬롯을 센다 → 덮는 구간 = K·S + el + 1 ≥ W + 1 틱 ⊇ [now−W+1, now].
 * 그래서 어느 길이 W 창도 N 을 넘지 못한다 (넘는 창의 마지막 통과 시점에 나머지 N 개가 이미 세어졌을 것). */
#define KMAX 64
typedef struct { uint32_t cnt[KMAX + 1]; uint32_t pos, slot_start, S, K, N; } kslot_t;
static void ks_init(kslot_t *k, uint32_t N, uint32_t W, uint32_t K, uint32_t now) {
    memset(k, 0, sizeof *k); k->N = N; k->K = K; k->S = W / K; k->slot_start = now;
}
static bool ks_try(kslot_t *k, uint32_t now) {
    uint32_t el = now - k->slot_start;
    if (el >= k->S) {
        uint32_t adv = el / k->S;
        for (uint32_t i = 0; i < adv && i <= k->K; i++) { k->pos = (k->pos + 1) % (k->K + 1); k->cnt[k->pos] = 0; }
        k->slot_start += adv * k->S;
    }
    uint32_t sum = 0;
    for (uint32_t i = 0; i <= k->K; i++) sum += k->cnt[i];
    if (sum >= k->N) return false;
    k->cnt[k->pos]++;
    return true;
}
/* 정확한 슬라이딩 로그 (비교 기준, 본문 Part B 와 같음) */
typedef struct { uint32_t ts[64]; uint32_t head, n, N, W; } slog_t;
static bool sl_try(slog_t *s, uint32_t now) {
    if (s->n < s->N) { s->ts[(s->head + s->n) % s->N] = now; s->n++; return true; }
    if (now - s->ts[s->head] < s->W) return false;
    s->ts[s->head] = now; s->head = (s->head + 1) % s->N;
    return true;
}
static uint64_t g_at4[20000];
static void ex4(void) {
    const uint32_t N = 10, W = 100, H = 10000;
    static const uint32_t Ks[] = { 1, 2, 5, 10, 25, 50 };
    slog_t sl; memset(&sl, 0, sizeof sl); sl.N = N; sl.W = W;
    int nlog = 0;
    for (uint32_t t = 0; t < H; t++) nlog += sl_try(&sl, 0xFFFFFF00u + t);
    int prev_acc = 0, over = 0;
    bool mono = true;
    for (int i = 0; i < 6; i++) {
        uint32_t K = Ks[i];
        kslot_t ks; ks_init(&ks, N, W, K, 0xFFFFFF00u);
        int na = 0;
        for (uint32_t t = 0; t < H; t++) if (ks_try(&ks, 0xFFFFFF00u + t)) g_at4[na++] = t;   /* 매 틱 요청 (포화) */
        uint64_t mw = max_in_window(g_at4, na, W);
        printf("ex4 K=%-2u slots=%-2u (%3u B): saturating 1/tick for %u ticks -> accepted %d (exact log %d, %.1f%%), max/W=%llu, theory N*H/(W+S)=%.0f\n",
               K, K + 1, (K + 1) * 4, H, na, nlog, 100.0 * na / nlog, (unsigned long long)mw, (double)N * H / (W + W / K));
        if (mw > N) over++;
        if (na < prev_acc) mono = false;
        prev_acc = na;
    }
    /* 무작위 트래픽에서도 어느 창도 N 초과 없음 */
    for (uint32_t seed = 0; seed < 100; seed++) {
        g_rng = 1234 + seed;
        uint32_t K = Ks[seed % 6];
        kslot_t ks; ks_init(&ks, N, W, K, 0xFFFFF000u);
        int na = 0;
        for (uint32_t t = 0; t < 3000; t++) { int k = rnd(5) == 0 ? (int)rnd(8) : 0; while (k--) if (ks_try(&ks, 0xFFFFF000u + t)) g_at4[na++] = t; }
        if (max_in_window(g_at4, na, W) > N) over++;
    }
    CHECK(over == 0);
    CHECK(mono && prev_acc <= nlog);
}

int main(void) {
    ex1();
    ex2();
    ex3();
    ex4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
