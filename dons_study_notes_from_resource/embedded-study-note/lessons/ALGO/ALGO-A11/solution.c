/* ALGO-A11 Rate Limiter: 토큰 버킷과 슬라이딩 로그
 *
 * 시간 = 정수 틱 (uint32_t, 2^32 에서 wrap). 리미터 쪽에는 부동소수점·malloc·나눗셈 없는 핫 패스.
 *   Part A  토큰 버킷: 유리수 속도 r = num/den 토큰/틱, 버스트 b. 토큰을 den 배로 스케일해 나머지까지 보관
 *           → 드리프트 0 (10^9 틱 포화 테스트), wrap/과거 틱 처리, "모든 창 [t, t+T] 에서 통과량 ≤ b + r·T" 전수 검사,
 *           브루트포스 창 검사기와 결정(accept/reject)이 한 건도 다르지 않은지 대조
 *   Part B  고정 창 카운터(경계 2배 버스트) · 슬라이딩 로그(링 N칸, 정확) · 슬라이딩 카운터(가중 이전 창, 근사)
 *           → 같은 트래픽에서 "길이 W 창 최대 통과 수" 표, 카운터 과허용 상한 N + ceil(N(W−2)/W) 증명값 = 측정값
 *   Part C  GPIO 인터럽트 폭주 가드(IRQ 마스크 + 히스테리시스 재개) · 메시지 ID 별 로그 폭주 제한("+N suppressed")
 * 테스트는 모두 결정적 (고정 시드 xorshift32). 시간 측정은 demo_bench.c 에만.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
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
static uint32_t rnd(uint32_t n) { return xs32() % n; }   /* 테스트 쪽 전용: 약간의 모듈로 편향은 상관없음 */

/* ======================= Part A — 토큰 버킷 ======================= */
/* 토큰을 den 배로 스케일: level = 토큰 × den. 한 틱마다 num 단위가 들어온다 (= num/den 토큰).
 * 나머지(1 토큰 미만)도 level 에 남으므로 아무리 자주/드물게 불러도 잃는 것이 없다. */
typedef struct {
    uint32_t level;      /* 토큰 × den  (0 ≤ level ≤ cap) */
    uint32_t cap;        /* burst × den */
    uint32_t num, den;   /* r = num/den 토큰/틱 */
    uint32_t burst;      /* b (토큰) */
    uint32_t full_ticks; /* ceil(cap/num): 이만큼 지나면 무조건 가득 */
    uint32_t last;       /* 마지막으로 반영한 틱 */
} tbucket_t;

/* 조건: burst ≥ 1, num ≥ 1, den ≥ 1, burst·den ≤ 2^31−1 (→ refill 덧셈이 uint32 를 안 넘는다). 처음엔 가득. */
static bool tb_init(tbucket_t *tb, uint32_t burst, uint32_t num, uint32_t den, uint32_t now) {
    memset(tb, 0, sizeof *tb);
    if (burst == 0 || num == 0 || den == 0) return false;
    if ((uint64_t)burst * den > 0x7FFFFFFFu) return false;
    tb->burst = burst; tb->num = num; tb->den = den;
    tb->cap = burst * den;
    tb->full_ticks = tb->cap / num + (tb->cap % num != 0);   /* 나눗셈은 init 에서 한 번만 */
    tb->level = tb->cap;
    tb->last = now;
    return true;
}

/* now 까지 경과한 틱만큼 채운다 (게으른 refill). 계약: 같은 틱 소스, 호출 간격 < 2^31 틱.
 * now 가 last 보다 과거면(다른 문맥이 먼저 더 새 틱으로 갱신) 아무것도 하지 않는다. */
static void tb_refill(tbucket_t *tb, uint32_t now) {
    uint32_t dt = now - tb->last;                 /* mod 2^32 뺄셈: wrap 을 넘어도 실제 경과와 같다 */
    if (dt == 0 || dt > 0x7FFFFFFFu) return;      /* 같은 틱, 또는 "음수" 경과 = 늦게 읽은 틱 → 무시 */
    tb->last = now;
    if (dt >= tb->full_ticks) { tb->level = tb->cap; return; }
    uint32_t lv = tb->level + dt * tb->num;       /* dt ≤ full_ticks−1 → dt·num < cap ≤ 2^31−1 : 넘침 없음 */
    tb->level = lv < tb->cap ? lv : tb->cap;
}

/* cost 토큰짜리 요청 하나. 통과면 true (토큰 차감). cost > burst 는 영원히 불가 → false. */
static bool tb_try(tbucket_t *tb, uint32_t now, uint32_t cost) {
    tb_refill(tb, now);
    if (cost > tb->burst) return false;           /* cost·den 넘침도 여기서 막힌다 */
    uint32_t need = cost * tb->den;
    if (tb->level < need) return false;
    tb->level -= need;
    return true;
}

/* 지금 쓸 수 있는 정수 토큰 수 (refill 포함). */
static uint32_t tb_tokens(tbucket_t *tb, uint32_t now) { tb_refill(tb, now); return tb->level / tb->den; }

/* ======================= Part B — 창(window) 기반 리미터 ======================= */
/* 공통 계약: "길이 W 의 창 = 틱 [t−W+1, t]" 마다 최대 N 개. 요청 비용은 1. now 는 단조 증가. */

/* 고정 창 카운터 (주어짐 — 비교용 기준선). 창 = [start + kW, start + (k+1)W). */
typedef struct { uint32_t start, count, N, W; } fixwin_t;
static void fw_init(fixwin_t *f, uint32_t N, uint32_t W, uint32_t now) { f->start = now; f->count = 0; f->N = N; f->W = W; }
static bool fw_try(fixwin_t *f, uint32_t now) {
    uint32_t el = now - f->start;
    if (el >= f->W) { f->start += el - el % f->W; f->count = 0; }   /* 새 창: 경계를 W 배수에 맞춘다 */
    if (f->count >= f->N) return false;
    f->count++;
    return true;
}

/* 슬라이딩 로그: 최근 통과한 N 개의 타임스탬프 링. ts[head] = 그중 가장 오래된 것. 메모리 4N 바이트. */
#define SLOG_MAX 256
typedef struct { uint32_t ts[SLOG_MAX]; uint32_t head, n, N, W; } slog_t;
static bool sl_init(slog_t *s, uint32_t N, uint32_t W) {
    memset(s, 0, sizeof *s);
    if (N == 0 || N > SLOG_MAX || W == 0) return false;
    s->N = N; s->W = W;
    return true;
}
/* 창 [now−W+1, now] 안의 통과 수가 N 미만이면 통과. O(1). */
static bool sl_try(slog_t *s, uint32_t now) {
    if (s->n < s->N) {                                  /* 아직 N 개도 안 통과 → 무조건 통과 */
        s->ts[(s->head + s->n) % s->N] = now;
        s->n++;
        return true;
    }
    if (now - s->ts[s->head] < s->W) return false;      /* N번째로 최근 통과가 아직 창 안 → N 개가 창 안 */
    s->ts[s->head] = now;                               /* 가장 오래된 것을 덮어쓰고 head 전진 */
    s->head = (s->head + 1) % s->N;
    return true;
}

/* 슬라이딩 카운터: 이전 창 개수 prev 를 "이전 창 중 지금 창에 겹치는 비율"로 가중.
 * 추정 = prev·(W−el)/W + cur  (el = 지금 창 안 경과) — 나눗셈 없이 prev·(W−el) + cur·W < N·W 로 비교. 12 바이트. */
typedef struct { uint32_t start, prev, cur, N, W; } swc_t;
static bool swc_init(swc_t *c, uint32_t N, uint32_t W, uint32_t now) {
    memset(c, 0, sizeof *c);
    if (N == 0 || W == 0 || (uint64_t)N * W > 0x7FFFFFFFu) return false;   /* 비교식 ≤ 2NW 가 uint32 에 들어가게 */
    c->N = N; c->W = W; c->start = now;
    return true;
}
static bool swc_try(swc_t *c, uint32_t now) {
    uint32_t el = now - c->start;
    if (el >= c->W) {                                   /* 창이 1개 이상 지나감 */
        uint32_t k = el / c->W;
        c->prev = (k == 1) ? c->cur : 0;                /* 2개 이상 지났으면 이전 창은 비어 있었다 */
        c->cur = 0;
        c->start += k * c->W;
        el -= k * c->W;
    }
    if (c->prev * (c->W - el) + c->cur * c->W >= c->N * c->W) return false;
    c->cur++;
    return true;
}

/* ======================= Part C — 펌웨어 시나리오 ======================= */
/* C1. GPIO 엣지 인터럽트 폭주 가드. ISR 은 엣지마다 guard_on_edge(); 토큰이 없으면 그 엣지를 버리고
 * IRQ 를 마스크한다(이후 엣지는 ISR 진입 자체가 없다). 1 ms 하우스키핑 guard_tick() 이 토큰이 resume 개 이상
 * 모이면 마스크를 푼다 (히스테리시스: 1개만 모이자마자 풀면 폭주 중 매 10틱마다 마스크/해제를 반복). */
typedef struct {
    tbucket_t tb;
    bool masked;
    uint32_t resume;                  /* 마스크 해제 기준 토큰 수 */
    uint32_t mask_since;
    uint32_t handled, dropped, mask_events, longest_mask;
} irqguard_t;

/* ISR 안에서 엣지마다. 1 = 처리, 0 = 버림 (그리고 masked = true, 통계 갱신). */
static int guard_on_edge(irqguard_t *g, uint32_t now) {
    if (tb_try(&g->tb, now, 1)) { g->handled++; return 1; }
    g->dropped++;
    g->masked = true;                 /* 실제 칩: EXTI->IMR 비트 클리어 / NVIC_DisableIRQ */
    g->mask_since = now;
    g->mask_events++;
    return 0;
}

/* 1 ms 하우스키핑 (매 틱). 마스크 중이 아니어도 refill 해서 last 를 최신으로 유지한다 (2^31 틱 계약). */
static void guard_tick(irqguard_t *g, uint32_t now) {
    if (!g->masked) { tb_refill(&g->tb, now); return; }
    if (tb_tokens(&g->tb, now) >= g->resume) {
        uint32_t d = now - g->mask_since;
        if (d > g->longest_mask) g->longest_mask = d;
        g->masked = false;            /* 실제 칩: 대기 중 플래그(EXTI->PR) 를 먼저 지우고 마스크 해제 */
    }
}

/* C2. 메시지 ID 별 로그 폭주 제한. 슬롯 8개(ID 마다 버킷 + 억제 카운터), 슬롯이 다 차면 공용 overflow 버킷. */
#define LOGRL_SLOTS 8
typedef struct { uint16_t id; bool used; tbucket_t tb; uint32_t suppressed; } logrl_slot_t;
typedef struct {
    logrl_slot_t s[LOGRL_SLOTS];
    tbucket_t ovf; uint32_t ovf_suppressed;
    uint32_t burst, num, den;
} logrl_t;
static void logrl_init(logrl_t *L, uint32_t burst, uint32_t num, uint32_t den, uint32_t now) {
    memset(L, 0, sizeof *L);
    L->burst = burst; L->num = num; L->den = den;
    (void)tb_init(&L->ovf, burst, num, den, now);
}
/* id 메시지를 지금 출력해도 되나. 통과면 true, *supp = 직전 통과 이후 이 버킷에서 억제된 개수 (출력 줄에
 * "(+N suppressed)" 로 붙이고 0 으로 리셋). 거부면 false, 억제 카운터 +1, *supp = 0. */
static bool logrl_try(logrl_t *L, uint16_t id, uint32_t now, uint32_t *supp) {
    tbucket_t *tb = &L->ovf;
    uint32_t *sp = &L->ovf_suppressed;
    int freeslot = -1;
    for (int i = 0; i < LOGRL_SLOTS; i++) {
        if (L->s[i].used && L->s[i].id == id) { tb = &L->s[i].tb; sp = &L->s[i].suppressed; freeslot = -2; break; }
        if (!L->s[i].used && freeslot == -1) freeslot = i;
    }
    if (freeslot >= 0) {                                /* 처음 보는 ID → 새 슬롯 (가득 찬 버킷으로 시작) */
        logrl_slot_t *s = &L->s[freeslot];
        s->used = true; s->id = id; s->suppressed = 0;
        (void)tb_init(&s->tb, L->burst, L->num, L->den, now);
        tb = &s->tb; sp = &s->suppressed;
    }
    if (tb_try(tb, now, 1)) { *supp = *sp; *sp = 0; return true; }
    (*sp)++;
    *supp = 0;
    return false;
}

/* ======================= 테스트 하네스 ======================= */
/* 트래픽 = (틱, 비용) 사건 목록, 틱 오름차순. 시간은 하네스에서 64비트(무한)로 두고,
 * 리미터에는 (uint32_t)(BASE + t) 를 준다 — BASE 가 2^32 바로 아래라 테스트 초반에 wrap 을 지난다. */
#define BASE 0xFFFFF000u
#define MAXEV 16000
typedef struct { uint64_t t; uint32_t cost; } ev_t;
static ev_t g_ev[MAXEV];
static int g_nev;
static uint64_t g_at[MAXEV];   /* 통과한 요청의 틱 */
static uint32_t g_ac[MAXEV];   /* 통과한 요청의 비용 */

static void ev_add(uint64_t t, uint32_t cost, int k) { while (k-- > 0 && g_nev < MAXEV) { g_ev[g_nev].t = t; g_ev[g_nev].cost = cost; g_nev++; } }

enum { P_STEADY, P_BURSTY, P_EDGE, P_IDLE_BURST, P_RANDOM, P_NPAT };
static const char *pat_name[P_NPAT] = { "steady 1/5", "bursty 15@150", "edge 10|10", "idle->burst", "random" };
/* 계약 N=10 / W=100 틱 (평균 0.1 /틱) 기준으로 만든 트래픽, 길이 3000 틱 */
static void gen_pattern(int p, uint32_t seed) {
    g_nev = 0;
    g_rng = seed;
    const uint64_t H = 3000;
    switch (p) {
    case P_STEADY: for (uint64_t t = 0; t < H; t += 5) ev_add(t, 1, 1); break;              /* 2배 과부하, 고르게 */
    case P_BURSTY: for (uint64_t t = 0; t < H; t += 150) ev_add(t, 1, 15); break;           /* 뭉치 */
    case P_EDGE:   for (uint64_t k = 1; k * 100 < H; k += 2) { ev_add(k * 100 - 1, 1, 10); ev_add(k * 100, 1, 10); } break;
    case P_IDLE_BURST: for (uint64_t t = 1500; t < 1550; t++) ev_add(t, 1, 30);
                       for (uint64_t t = 1550; t < H; t++) ev_add(t, 1, 1); break;
    case P_RANDOM: for (uint64_t t = 0; t < H; t++) if (rnd(4) == 0) ev_add(t, 1, 1 + (int)rnd(4)); break;
    }
}

/* 통과 로그에서 길이 L 틱 창 [t−L+1, t] 의 최대 비용 합 (투 포인터, O(n)). */
static uint64_t max_in_window(const uint64_t *at, const uint32_t *ac, int na, uint64_t L) {
    uint64_t best = 0, sum = 0;
    int i = 0;
    for (int j = 0; j < na; j++) {
        sum += ac[j];
        while (at[j] - at[i] >= L) sum -= ac[i++];
        if (sum > best) best = sum;
    }
    return best;
}

/* 모든 창 [t_i, t_j] 에서 den·Σ cost ≤ den·b + num·(t_j − t_i) 인가 (O(na²) 전수). *tight = 등호에 닿은 창이 있나
 * (정수 통과량이 floor(b + r·T) 와 같은 창). */
static bool all_windows_ok(const uint64_t *at, const uint32_t *ac, int na, uint32_t b, uint32_t num, uint32_t den, bool *tight) {
    bool ok = true;
    *tight = false;
    for (int i = 0; i < na; i++) {
        uint64_t sum = 0;
        for (int j = i; j < na; j++) {
            sum += ac[j];
            uint64_t lim = (uint64_t)den * b + (uint64_t)num * (at[j] - at[i]);
            if ((uint64_t)den * sum > lim) ok = false;
            if ((uint64_t)den * (sum + 1) > lim) *tight = true;
        }
    }
    return ok;
}

/* 브루트포스 기준 (토큰 버킷): 통과 로그만 보고 "이 요청까지 통과시켜도 모든 창 [t_j, s] 가 b + r·(s−t_j) 이하인가". */
static bool ref_tb_ok(int na, uint64_t s, uint32_t cost, uint32_t b, uint32_t num, uint32_t den) {
    if (cost > b) return false;
    uint64_t sum = cost;
    for (int j = na - 1; j >= 0; j--) {
        sum += g_ac[j];
        if ((uint64_t)den * sum > (uint64_t)den * b + (uint64_t)num * (s - g_at[j])) return false;
    }
    return true;
}
/* 브루트포스 기준 (슬라이딩 로그): 창 [s−W+1, s] 의 통과 수 < N. */
static bool ref_log_ok(int na, uint64_t s, uint32_t N, uint32_t W) {
    uint32_t cnt = 0;
    for (int j = na - 1; j >= 0 && s - g_at[j] < W; j--) cnt++;
    return cnt < N;
}
/* 브루트포스 기준 (슬라이딩 카운터): 통과 로그에서 정렬된 창 [kW,(k+1)W) 의 개수를 직접 세어 같은 식 적용. */
static bool ref_swc_ok(int na, uint64_t s, uint32_t N, uint32_t W) {
    uint64_t k = s / W, el = s - k * W, cur = 0, prev = 0;
    for (int j = 0; j < na; j++) {
        if (g_at[j] / W == k) cur++;
        else if (k > 0 && g_at[j] / W == k - 1) prev++;
    }
    return prev * (W - el) + cur * W < (uint64_t)N * W;
}

enum { L_TB, L_TBSTRICT, L_FIXED, L_LOG, L_SWC, L_N };
static const char *lim_name[L_N] = { "TB b=10 r=1/10", "TB b=1 r=1/11", "fixed window", "sliding log", "sliding counter" };
typedef struct { tbucket_t tb; fixwin_t fw; slog_t sl; swc_t sc; } anylim_t;

/* 현재 g_ev 를 리미터 kind 에 흘려보내고 통과 로그를 g_at/g_ac 에 남긴다. 반환 = 통과 수.
 * *mism = 같은 시점 브루트포스 기준과 결정이 다른 횟수 (기준이 있는 리미터만). */
static int run_limiter(int kind, int *mism) {
    static anylim_t a;
    int na = 0;
    *mism = 0;
    switch (kind) {
    case L_TB:       (void)tb_init(&a.tb, 10, 1, 10, BASE); break;
    case L_TBSTRICT: (void)tb_init(&a.tb, 1, 1, 11, BASE); break;
    case L_FIXED:    fw_init(&a.fw, 10, 100, BASE); break;
    case L_LOG:      (void)sl_init(&a.sl, 10, 100); break;
    case L_SWC:      (void)swc_init(&a.sc, 10, 100, BASE); break;
    }
    for (int i = 0; i < g_nev; i++) {
        uint64_t s = g_ev[i].t;
        uint32_t now = (uint32_t)(BASE + s);
        bool ok = false, ref = ok;
        switch (kind) {
        case L_TB:       ok = tb_try(&a.tb, now, g_ev[i].cost); ref = ref_tb_ok(na, s, g_ev[i].cost, 10, 1, 10); break;
        case L_TBSTRICT: ok = tb_try(&a.tb, now, g_ev[i].cost); ref = ref_tb_ok(na, s, g_ev[i].cost, 1, 1, 11); break;
        case L_FIXED:    ok = fw_try(&a.fw, now); ref = ok; break;
        case L_LOG:      ok = sl_try(&a.sl, now); ref = ref_log_ok(na, s, 10, 100); break;
        case L_SWC:      ok = swc_try(&a.sc, now); ref = ref_swc_ok(na, s, 10, 100); break;
        }
        if (ok != ref) (*mism)++;
        if (ok) { g_at[na] = s; g_ac[na] = g_ev[i].cost; na++; }
    }
    return na;
}

/* 10^9 틱 포화 테스트: 무작위 간격으로 불러서 쓸 수 있는 토큰을 전부 소진.
 * 간격 ≤ (b−1)·den/num 이면 (소진 후 level < den 이므로) 버킷이 cap 에 닿지 않는다 → 버려지는 토큰 0.
 * 기대값 = b + floor(T·num/den) (T = 마지막 틱 − 시작 틱) — 드리프트가 있으면 여기서 어긋난다. */
static bool saturate_exact(uint32_t b, uint32_t num, uint32_t den, uint64_t Ttot, uint32_t seed, uint64_t *got, uint64_t *want) {
    tbucket_t tb;
    if (!tb_init(&tb, b, num, den, BASE)) return false;
    g_rng = seed;
    uint32_t maxstep = (uint32_t)((uint64_t)(b - 1) * den / num);
    if (maxstep == 0) maxstep = 1;
    uint64_t t = 0, acc = 0;
    for (;;) {
        uint32_t now = (uint32_t)(BASE + t);
        uint32_t k = tb_tokens(&tb, now);
        if (k > 0 && tb_try(&tb, now, k)) acc += k;
        uint64_t step = 1 + rnd(maxstep);
        if (t + step > Ttot) break;
        t += step;
    }
    *got = acc;
    *want = b + (t * num) / den;
    return acc == *want;
}

/* 슬라이딩 카운터 적대적 구성: 창 0 은 비우고, 창 1 의 마지막 틱(2W−1)에 N 개, 창 2 의 el = W−2 에 2N 개 홍수.
 * 길이 W 창 [2W−1, 3W−2] 의 통과 수 = N + ceil(N(W−2)/W) 이어야 한다 (5. 풀이의 상한과 같음). */
static uint64_t swc_adversary(uint32_t N, uint32_t W) {
    swc_t c;
    (void)swc_init(&c, N, W, BASE);
    int na = 0;
    for (uint32_t k = 0; k < N; k++) if (swc_try(&c, (uint32_t)(BASE + 2 * W - 1))) { g_at[na] = 2 * W - 1; g_ac[na] = 1; na++; }
    for (uint32_t k = 0; k < 2 * N; k++) if (swc_try(&c, (uint32_t)(BASE + 3 * W - 2))) { g_at[na] = 3 * W - 2; g_ac[na] = 1; na++; }
    return max_in_window(g_at, g_ac, na, W);
}

static void test_part_a(void) {
    printf("== Part A: token bucket ==\n");
    tbucket_t tb;
    CHECK(!tb_init(&tb, 0, 1, 1, 0));
    CHECK(!tb_init(&tb, 1, 0, 1, 0));
    CHECK(!tb_init(&tb, 1u << 16, 1, 1u << 15, 0));            /* b·den = 2^31 > 2^31−1 → 거부 */
    CHECK(tb_init(&tb, 3, 1, 4, 100) && tb.full_ticks == 12);

    /* 손 계산: b=3, r=1/4. 가득(3) → 3개 통과 → 4번째 거부 → 4틱 뒤 1개 → 7틱 뒤엔 아직 1개 (나머지 3/4 보관) */
    CHECK(tb_try(&tb, 100, 1) && tb_try(&tb, 100, 1) && tb_try(&tb, 100, 1));
    CHECK(!tb_try(&tb, 100, 1));
    CHECK(!tb_try(&tb, 103, 1));                               /* 3/4 토큰 */
    CHECK(tb_try(&tb, 104, 1));                                /* 정확히 1 */
    CHECK(tb_tokens(&tb, 111) == 1 && tb.level == 7);          /* 7/4 토큰 = level 7 (단위 1/4) */
    CHECK(!tb_try(&tb, 111, 2) && tb_try(&tb, 112, 2));         /* 2토큰짜리: 111 에선 1.75, 112 에서 2 */
    CHECK(!tb_try(&tb, 112, 4));                               /* cost > burst → 영원히 불가 */
    CHECK(tb_tokens(&tb, 112 + 12) == 3);                      /* full_ticks 경과 → 가득 */

    /* wrap: 0xFFFFFFF0 에서 비우고 0x00000010 (32틱 뒤)에 8토큰 (r = 1/4) — 단, 버스트 10 */
    CHECK(tb_init(&tb, 10, 1, 4, 0xFFFFFFF0u));
    CHECK(tb_try(&tb, 0xFFFFFFF0u, 10));
    CHECK(tb_tokens(&tb, 0x00000010u) == 8);
    /* 과거 틱: last=0x10 이후 0x0F 로 부르면 무시 (가득 채우면 안 된다 — dt 가 2^32−1 로 보이는 버그) */
    CHECK(tb_tokens(&tb, 0x0000000Fu) == 8 && tb.last == 0x10u);
    CHECK(tb_tokens(&tb, 0x00000014u) == 9);                    /* 0x10 기준으로 4틱만 */

    /* 매 틱 호출 + 1/3 속도: 반올림 손실이 있으면 영영 안 찬다 */
    CHECK(tb_init(&tb, 2, 1, 3, BASE));
    uint64_t acc = 0;
    for (uint32_t t = 0; t <= 99999; t++) { uint32_t k = tb_tokens(&tb, BASE + t); if (k && tb_try(&tb, BASE + t, k)) acc += k; }
    CHECK(acc == 2 + 99999 / 3);

    /* 10^9 틱 포화 (wrap 포함), 여러 속도 */
    static const uint32_t rates[][3] = { {1000, 1, 3}, {500, 7, 10}, {6400, 5, 2}, {2, 1, 1000}, {1000, 999, 1000} };
    for (int i = 0; i < 5; i++) {
        uint64_t got, want;
        bool ok = saturate_exact(rates[i][0], rates[i][1], rates[i][2], 1000000000ull, 77 + (uint32_t)i, &got, &want);
        printf("  saturate 1e9 ticks  b=%-4u r=%u/%-4u accepted=%llu expected b+floor(rT)=%llu\n",
               rates[i][0], rates[i][1], rates[i][2], (unsigned long long)got, (unsigned long long)want);
        CHECK(ok);
    }

    /* 브루트포스 창 검사기와 결정 대조 + 모든 창 b + r·T 전수 검사: 패턴 5개 + 무작위 비용 트래픽 */
    int mism_total = 0, bad_win = 0, tight_cnt = 0, runs = 0;
    for (int p = 0; p < P_NPAT; p++) {
        gen_pattern(p, 1000 + (uint32_t)p);
        int m; int na = run_limiter(L_TB, &m);
        bool tight; bool ok = all_windows_ok(g_at, g_ac, na, 10, 1, 10, &tight);
        mism_total += m; bad_win += !ok; tight_cnt += tight; runs++;
    }
    static const uint32_t prm[][3] = { {1, 1, 7}, {4, 3, 5}, {8, 2, 1}, {16, 1, 64}, {5, 5, 9} };
    for (int seed = 0; seed < 40; seed++) {
        const uint32_t *q = prm[seed % 5];
        g_nev = 0; g_rng = 5000 + (uint32_t)seed;
        for (uint64_t t = 0; t < 1500; t++) if (rnd(3) == 0) { int k = 1 + (int)rnd(3); while (k--) ev_add(t, 1 + rnd(q[0] + 1), 1); }
        tbucket_t b2; (void)tb_init(&b2, q[0], q[1], q[2], BASE);
        int na = 0, m = 0;
        for (int i = 0; i < g_nev; i++) {
            bool ok = tb_try(&b2, (uint32_t)(BASE + g_ev[i].t), g_ev[i].cost);
            if (ok != ref_tb_ok(na, g_ev[i].t, g_ev[i].cost, q[0], q[1], q[2])) m++;
            if (ok) { g_at[na] = g_ev[i].t; g_ac[na] = g_ev[i].cost; na++; }
        }
        bool tight; bool ok = all_windows_ok(g_at, g_ac, na, q[0], q[1], q[2], &tight);
        mism_total += m; bad_win += !ok; tight_cnt += tight; runs++;
    }
    printf("  vs brute-force window checker: %d runs, decision mismatches=%d, window violations=%d, runs with a tight window=%d\n",
           runs, mism_total, bad_win, tight_cnt);
    CHECK(mism_total == 0);
    CHECK(bad_win == 0);
    CHECK(tight_cnt == runs);          /* 탐욕적이라 모든 실행에서 어떤 창은 b + rT 에 꽉 찬다 */
}

static void test_part_b(void) {
    printf("== Part B: window limiters (contract N=10 per W=100 ticks) ==\n");
    printf("  %-14s %-16s %8s %8s %8s %8s\n", "traffic", "limiter", "offered", "passed", "max/W", "max/2tk");
    uint64_t mw[P_NPAT][L_N];
    int mism_log = 0, mism_swc = 0;
    for (int p = 0; p < P_NPAT; p++) {
        for (int k = 0; k < L_N; k++) {
            gen_pattern(p, 1000 + (uint32_t)p);
            int m; int na = run_limiter(k, &m);
            if (k == L_LOG) mism_log += m;
            if (k == L_SWC) mism_swc += m;
            mw[p][k] = max_in_window(g_at, g_ac, na, 100);
            printf("  %-14s %-16s %8d %8d %8llu %8llu\n", k == 0 ? pat_name[p] : "", lim_name[k], g_nev, na,
                   (unsigned long long)mw[p][k], (unsigned long long)max_in_window(g_at, g_ac, na, 2));
        }
    }
    /* 무작위 트래픽 여러 시드에서도 기준과 대조 */
    uint64_t worst_swc = 0, worst_fixed = 0, worst_log = 0;
    for (uint32_t seed = 0; seed < 30; seed++) {
        gen_pattern(P_RANDOM, 9000 + seed);
        int m, na;
        na = run_limiter(L_LOG, &m); mism_log += m; uint64_t x = max_in_window(g_at, g_ac, na, 100); if (x > worst_log) worst_log = x;
        na = run_limiter(L_SWC, &m); mism_swc += m; x = max_in_window(g_at, g_ac, na, 100); if (x > worst_swc) worst_swc = x;
        na = run_limiter(L_FIXED, &m); x = max_in_window(g_at, g_ac, na, 100); if (x > worst_fixed) worst_fixed = x;
    }
    for (int p = 0; p < P_NPAT; p++) {
        if (mw[p][L_LOG] > worst_log) worst_log = mw[p][L_LOG];
        if (mw[p][L_SWC] > worst_swc) worst_swc = mw[p][L_SWC];
        if (mw[p][L_FIXED] > worst_fixed) worst_fixed = mw[p][L_FIXED];
    }
    printf("  decision mismatches vs brute force: sliding log=%d, sliding counter=%d\n", mism_log, mism_swc);
    printf("  worst max/W over all runs: fixed=%llu (bound 2N=20), log=%llu (N=10), counter=%llu (bound 20)\n",
           (unsigned long long)worst_fixed, (unsigned long long)worst_log, (unsigned long long)worst_swc);
    CHECK(mism_log == 0);
    CHECK(mism_swc == 0);
    CHECK(worst_log <= 10);                               /* 슬라이딩 로그는 정확 */
    CHECK(worst_fixed <= 20 && mw[P_EDGE][L_FIXED] == 20); /* 고정 창: 경계에서 2N, 그리고 그게 최대 */
    CHECK(mw[P_EDGE][L_TBSTRICT] <= 10 && mw[P_BURSTY][L_TBSTRICT] <= 10); /* b + r(W−1) = 1 + 9 = 10 */
    CHECK(mw[P_IDLE_BURST][L_TB] == 19);                  /* b + floor(r(W−1)) = 10 + 9 */
    CHECK(worst_swc <= 20);

    /* 슬라이딩 카운터 과허용 상한 = N + ceil(N(W−2)/W): 적대적 구성이 정확히 도달 */
    static const uint32_t nw[][2] = { {10, 100}, {50, 20}, {7, 3}, {100, 1000}, {3, 2} };
    for (int i = 0; i < 5; i++) {
        uint32_t N = nw[i][0], W = nw[i][1];
        uint64_t bound = N + ((uint64_t)N * (W - 2) + W - 1) / W;
        uint64_t got = swc_adversary(N, W);
        printf("  sliding counter adversary N=%-3u W=%-4u max/W=%llu bound N+ceil(N(W-2)/W)=%llu (%.2fx N)\n",
               N, W, (unsigned long long)got, (unsigned long long)bound, (double)got / N);
        CHECK(got == bound);
    }
    /* 작은 N, W 에서 무작위 트래픽 다수: 상한을 절대 넘지 않는다 */
    int over = 0;
    for (uint32_t seed = 0; seed < 300; seed++) {
        g_rng = 40000 + seed;
        uint32_t N = 1 + rnd(6), W = 2 + rnd(9);
        uint64_t bound = N + ((uint64_t)N * (W - 2) + W - 1) / W;
        swc_t c; (void)swc_init(&c, N, W, BASE);
        int na = 0;
        for (uint64_t t = 0; t < 200; t++) { int k = (int)rnd(3) == 0 ? (int)rnd(2 * N + 1) : 0; while (k--) if (swc_try(&c, (uint32_t)(BASE + t))) { g_at[na] = t; g_ac[na] = 1; na++; } }
        if (max_in_window(g_at, g_ac, na, W) > bound) over++;
    }
    CHECK(over == 0);
    printf("  random small (N,W) x300: runs over bound = %d\n", over);
}

static void test_part_c(void) {
    printf("== Part C1: GPIO IRQ storm guard (1 ms tick, b=8, r=1/10 per tick = 100/s, resume at 4) ==\n");
    static irqguard_t g;
    memset(&g, 0, sizeof g);
    (void)tb_init(&g.tb, 8, 1, 10, BASE);
    g.resume = 4;
    uint32_t edges = 0, isr_entries = 0, lost_masked = 0, normal = 0, normal_ok = 0, normal_out = 0, normal_out_ok = 0;
    int na = 0;
    for (uint64_t t = 0; t < 10000; t++) {
        uint32_t now = (uint32_t)(BASE + t);
        int k = 0;
        bool is_normal = (t % 50 == 0);                   /* 정상 센서 엣지 20 Hz */
        if (t >= 3000 && t < 3300) k = 5;                 /* 채터링 폭주 A: 5 kHz, 300 ms */
        if (t >= 7000 && t < 7020) k = 50;                /* 폭주 B: 50 kHz, 20 ms */
        bool quiet = !((t >= 3000 && t < 3350) || (t >= 7000 && t < 7070));
        for (int e = 0; e < k + (is_normal ? 1 : 0); e++) {
            bool this_normal = is_normal && e == k;       /* 같은 틱이면 정상 엣지는 폭주 엣지 뒤에 */
            edges++;
            int r = 0;
            if (g.masked) lost_masked++;
            else { isr_entries++; r = guard_on_edge(&g, now); if (r) { g_at[na] = t; g_ac[na] = 1; na++; } }
            if (this_normal) { normal++; normal_ok += r; if (quiet) { normal_out++; normal_out_ok += r; } }
        }
        guard_tick(&g, now);
    }
    bool tight; bool win_ok = all_windows_ok(g_at, g_ac, na, 8, 1, 10, &tight);
    printf("  edges=%u  ISR entries=%u  handled=%u  dropped-in-ISR=%u  lost-while-masked=%u  mask events=%u  longest mask=%u ms\n",
           edges, isr_entries, g.handled, g.dropped, lost_masked, g.mask_events, g.longest_mask);
    printf("  normal 20 Hz edges: %u/%u handled overall, %u/%u outside storm(+50 ms)\n", normal_ok, normal, normal_out_ok, normal_out);
    CHECK(win_ok);                                        /* 처리량은 모든 창에서 8 + T/10 이하 */
    CHECK(g.handled <= 8 + 9999 / 10);
    CHECK(g.dropped == g.mask_events);                    /* 버린 엣지마다 바로 마스크 */
    CHECK(isr_entries == g.handled + g.dropped);
    CHECK(normal_out > 0 && normal_out_ok == normal_out); /* 폭주 밖의 정상 엣지는 하나도 안 잃는다 */
    CHECK(isr_entries * 10 < edges);                      /* ISR 진입이 엣지 수의 1/10 미만 = 마스크의 효과 */
    CHECK(!g.masked);                                     /* 끝에는 풀려 있다 */

    printf("== Part C2: per-ID log limiter (b=3, 1 msg/s, 8 slots + overflow) ==\n");
    static logrl_t L;
    logrl_init(&L, 3, 1, 1000, BASE);
    uint32_t off7 = 0, ok7 = 0, rep7 = 0, first_rep = 0, ok_other = 0, off_other = 0, supp;
    for (uint64_t t = 0; t < 5000; t++) {
        uint32_t now = (uint32_t)(BASE + t);
        off7++;                                           /* ID 7 (ADC overrun) 이 매 ms 폭주 */
        if (logrl_try(&L, 7, now, &supp)) { ok7++; rep7 += supp; if (supp && !first_rep) first_rep = supp; }
        if (t % 1500 == 0) { off_other += 2; ok_other += logrl_try(&L, 1, now, &supp); ok_other += logrl_try(&L, 2, now, &supp); }
    }
    uint32_t pending7 = 0;
    for (int i = 0; i < LOGRL_SLOTS; i++) if (L.s[i].used && L.s[i].id == 7) pending7 = L.s[i].suppressed;
    printf("  id7: offered=%u printed=%u reported-suppressed=%u pending=%u first report=+%u ; ids 1,2: %u/%u printed\n",
           off7, ok7, rep7, pending7, first_rep, ok_other, off_other);
    CHECK(ok7 == 3 + 4999 / 1000);                        /* b + floor(T·r) */
    CHECK(ok7 + rep7 + pending7 == off7);                 /* 보존: 출력 + 보고된 억제 + 남은 억제 = 제출 */
    CHECK(first_rep == 997);                              /* t=1000 의 줄: t=3..999 의 997개 억제 보고 */
    CHECK(ok_other == off_other);                         /* 다른 ID 는 폭주의 영향을 안 받는다 (격리) */
    /* 슬롯 고갈: 7·1·2 가 3칸 → 새 ID 9개 중 5개는 슬롯, 나머지 4개는 공용 overflow 버킷 (b=3 → 1개 거부) */
    uint32_t ok_new = 0;
    for (uint16_t id = 100; id < 109; id++) ok_new += logrl_try(&L, id, (uint32_t)(BASE + 5000), &supp);
    int used = 0;
    for (int i = 0; i < LOGRL_SLOTS; i++) used += L.s[i].used;
    CHECK(used == LOGRL_SLOTS);
    CHECK(ok_new == 5 + 3);                               /* 슬롯 5개 + overflow 버스트 3 */
    CHECK(!logrl_try(&L, 200, (uint32_t)(BASE + 5000), &supp) && L.ovf_suppressed == 2);
}

int main(void) {
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
