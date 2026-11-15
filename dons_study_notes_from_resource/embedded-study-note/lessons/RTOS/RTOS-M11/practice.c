/* RTOS-M11 Rate Monotonic 분석: Liu-Layland 경계와 RTA — 추가 연습 (practice.c)
 *
 * 본문 solution.c 의 도구(파서, bignum, assign_prio, ll/hyperbolic, rta, sim_fp, blocking_ipcp)를 그대로 두고
 * 추가 연습 4개의 답을 붙였다:
 *  1. max_c()            : 민감도 분석 — 한 태스크의 C 를 최대 얼마까지 늘릴 수 있나 (이분 탐색)
 *  2. max_switch_cost()  : 문맥 전환 비용 cs 를 C + 2cs 로 반영했을 때 견디는 최대 cs
 *  3. is_harmonic()      : 조화 주기면 RM 의 이용률 경계가 1 — RTA 통과 ⇔ U <= 1 을 무작위로 확인
 *  4. opa()              : Audsley 최적 우선순위 할당 — 지터가 있으면 DM 이 최적이 아님을 보인다
 */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXN    16
#define NAMELEN 12
#define T_MAX   10000000u            /* 주기 상한 10 s (µs) — bignum 크기 계산의 전제 */

typedef struct {
    char name[NAMELEN];
    uint32_t C, T, D, J, B;          /* 입력 (B 는 직접 주거나 blocking_ipcp 가 채운다) */
    int prio;                        /* assign_prio 결과: 클수록 높음, 0 = 가장 낮음 */
} task_t;
typedef struct { task_t t[MAXN]; int n; } taskset_t;

/* ---------------- 주어진 코드: 파서 ---------------- */
enum { PARSE_OK = 0, PARSE_SYNTAX = -1, PARSE_RANGE = -2, PARSE_TOO_MANY = -3 };

static int read_u32(const char **pp, uint32_t *out) {
    const char *p = *pp; uint64_t v = 0; int nd = 0;
    while (*p >= '0' && *p <= '9') {
        v = v * 10 + (uint64_t)(*p - '0');
        if (v > 0xFFFFFFFFu) return 0;
        p++; nd++;
    }
    if (nd == 0) return 0;
    *out = (uint32_t)v; *pp = p;
    return 1;
}
static int is_eol(char c) { return c == '\0' || c == '\n' || c == '\r' || c == ';' || c == '#'; }

static int ts_parse(taskset_t *ts, const char *s) {
    memset(ts, 0, sizeof *ts);
    const char *p = s;
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ';') p++;
        if (*p == '\0') break;
        if (*p == '#') { while (*p && *p != '\n') p++; continue; }
        if (ts->n == MAXN) return PARSE_TOO_MANY;
        task_t *t = &ts->t[ts->n];
        memset(t, 0, sizeof *t);
        int k = 0;
        while (isalnum((unsigned char)*p) || *p == '_') { if (k < NAMELEN - 1) t->name[k++] = *p; p++; }
        if (k == 0) return PARSE_SYNTAX;
        uint32_t v[3]; int nv = 0;
        for (;;) {
            while (*p == ' ' || *p == '\t') p++;
            if (*p >= '0' && *p <= '9') {
                if (nv == 3 || !read_u32(&p, &v[nv])) return PARSE_SYNTAX;
                nv++;
            } else if ((*p == 'J' || *p == 'B') && p[1] == '=') {
                char key = *p; uint32_t x; p += 2;
                if (!read_u32(&p, &x)) return PARSE_SYNTAX;
                if (key == 'J') t->J = x; else t->B = x;
            } else break;
        }
        if (!is_eol(*p) || nv < 2) return PARSE_SYNTAX;
        t->C = v[0]; t->T = v[1]; t->D = (nv == 3) ? v[2] : v[1];
        if (t->C == 0 || t->C > t->D || t->D > t->T || t->T > T_MAX || t->J >= t->T) return PARSE_RANGE;
        ts->n++;
    }
    return ts->n > 0 ? PARSE_OK : PARSE_SYNTAX;
}

/* ---------------- 주어진 코드: 작은 bignum (32-bit limb, little-endian) ----------------
 * 크기 근거: n <= 16, T <= 2^24 → Q = prod T <= 2^384, (P + nQ) <= 2nQ <= 2^389, 16 제곱 <= 2^6224 < 256*32. */
#define BIGL 256
typedef struct { int n; uint32_t w[BIGL]; } big_t;         /* n = 쓰는 limb 수, 맨 위 limb != 0 (0 이면 n = 0) */

static void big_set(big_t *a, uint32_t v) { a->n = v ? 1 : 0; a->w[0] = v; }
static int big_mul_u32(big_t *a, uint32_t m) {
    if (m == 0) { a->n = 0; return 0; }
    uint64_t carry = 0;
    for (int i = 0; i < a->n; i++) {
        uint64_t x = (uint64_t)a->w[i] * m + carry;
        a->w[i] = (uint32_t)x; carry = x >> 32;
    }
    if (carry) { if (a->n == BIGL) return -1; a->w[a->n++] = (uint32_t)carry; }
    return 0;
}
static int big_add(big_t *a, const big_t *b) {
    int n = a->n > b->n ? a->n : b->n;
    uint64_t carry = 0;
    for (int i = 0; i < n; i++) {
        uint64_t x = carry + (i < a->n ? a->w[i] : 0) + (i < b->n ? b->w[i] : 0);
        a->w[i] = (uint32_t)x; carry = x >> 32;
    }
    a->n = n;
    if (carry) { if (n == BIGL) return -1; a->w[a->n++] = (uint32_t)carry; }
    return 0;
}
static int big_mul(big_t *r, const big_t *a, const big_t *b) {   /* r 는 a, b 와 겹치면 안 된다 */
    if (a->n == 0 || b->n == 0) { r->n = 0; return 0; }
    if (a->n + b->n > BIGL) return -1;
    memset(r->w, 0, sizeof(uint32_t) * (size_t)(a->n + b->n));
    for (int i = 0; i < a->n; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < b->n; j++) {
            uint64_t x = (uint64_t)r->w[i + j] + (uint64_t)a->w[i] * b->w[j] + carry;
            r->w[i + j] = (uint32_t)x; carry = x >> 32;
        }
        r->w[i + b->n] = (uint32_t)carry;
    }
    r->n = a->n + b->n;
    while (r->n > 0 && r->w[r->n - 1] == 0) r->n--;
    return 0;
}
static int big_pow(big_t *r, const big_t *a, int e) {
    big_t tmp;
    big_set(r, 1);
    for (int i = 0; i < e; i++) {
        if (big_mul(&tmp, r, a)) return -1;
        *r = tmp;
    }
    return 0;
}
static int big_cmp(const big_t *a, const big_t *b) {
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (int i = a->n - 1; i >= 0; i--)
        if (a->w[i] != b->w[i]) return a->w[i] < b->w[i] ? -1 : 1;
    return 0;
}

/* U = P / Q 를 정확히: Q = prod T_j, P = sum_i C_i * prod_{j != i} T_j. 실패(overflow)면 -1. */
static int util_frac(const taskset_t *ts, big_t *P, big_t *Q) {
    int err = 0;
    big_set(Q, 1); big_set(P, 0);
    for (int i = 0; i < ts->n; i++) err |= big_mul_u32(Q, ts->t[i].T);
    for (int i = 0; i < ts->n; i++) {
        big_t term; big_set(&term, ts->t[i].C);
        for (int j = 0; j < ts->n; j++) if (j != i) err |= big_mul_u32(&term, ts->t[j].T);
        err |= big_add(P, &term);
    }
    return err ? -1 : 0;
}
/* 필요조건 U <= 1 (정확, 정수). */
static int u_le_one(const taskset_t *ts) {
    big_t P, Q;
    if (util_frac(ts, &P, &Q)) return -1;
    return big_cmp(&P, &Q) <= 0;
}
/* 표시용 (판정에 쓰지 않음): U 를 ppm 으로, 항마다 버림 */
static uint32_t u_ppm(const taskset_t *ts) {
    uint64_t s = 0;
    for (int i = 0; i < ts->n; i++) s += (uint64_t)ts->t[i].C * 1000000u / ts->t[i].T;
    return (uint32_t)s;
}
/* 표시용: floor(n(2^(1/n)-1) * 1e6), 오프라인 계산 (python) */
static const uint32_t LL_PPM[MAXN + 1] = { 0, 1000000, 828427, 779763, 756828, 743491, 734772, 728626,
    724061, 720537, 717734, 715451, 713557, 711958, 710592, 709411, 708380 };

static uint64_t ceil_div(uint64_t a, uint64_t b) { return (a + b - 1) / b; }
static uint64_t gcd64(uint64_t a, uint64_t b) { while (b) { uint64_t r = a % b; a = b; b = r; } return a; }
/* 하이퍼피리어드 = lcm(T). limit 를 넘으면 0 (오버플로·긴 시뮬레이션 방지). */
static uint64_t hyperperiod(const taskset_t *ts, uint64_t limit) {
    uint64_t L = 1;
    for (int i = 0; i < ts->n; i++) {
        L = L / gcd64(L, ts->t[i].T) * ts->t[i].T;          /* L <= limit <= 2^32, T <= 2^24 → 곱이 64비트 안 */
        if (L > limit) return 0;
    }
    return L;
}

/* ---------------- (본문 TODO) 1: RM / DM 우선순위 할당 ---------------- */
typedef enum { POL_RM, POL_DM } policy_t;
/* RM: T 가 짧을수록, DM: D 가 짧을수록 높은 우선순위. 같으면 인덱스가 작은 쪽이 높다.
 * prio = n - 1 - (나보다 앞서는 태스크 수)  → 0..n-1, 모두 다르다. */
static void assign_prio(taskset_t *ts, policy_t pol) {
    for (int i = 0; i < ts->n; i++) {
        uint32_t ki = pol == POL_RM ? ts->t[i].T : ts->t[i].D;
        int rank = 0;
        for (int j = 0; j < ts->n; j++) {
            uint32_t kj = pol == POL_RM ? ts->t[j].T : ts->t[j].D;
            if (kj < ki || (kj == ki && j < i)) rank++;
        }
        ts->t[i].prio = ts->n - 1 - rank;
    }
}

/* ---------------- (본문 TODO) 2: Liu–Layland 이용률 경계 (정수 판정) ---------------- */
typedef enum { UT_FAIL = 0, UT_PASS = 1, UT_NA = 2, UT_ERR = 3 } ut_t;
/* U <= n(2^(1/n) - 1)  <=>  (U/n + 1)^n <= 2  <=>  (P + nQ)^n <= 2 (nQ)^n   (U = P/Q, 양변 양수).
 * D < T 인 태스크가 하나라도 있으면 이 경계는 성립하지 않는다 → UT_NA. bignum 실패 → UT_ERR. */
static ut_t ll_test(const taskset_t *ts) {
    for (int i = 0; i < ts->n; i++) if (ts->t[i].D != ts->t[i].T) return UT_NA;
    big_t P, Q, nQ, base, lhs, rhs;
    if (util_frac(ts, &P, &Q)) return UT_ERR;
    int err = 0;
    nQ = Q; err |= big_mul_u32(&nQ, (uint32_t)ts->n);
    base = P; err |= big_add(&base, &nQ);
    err |= big_pow(&lhs, &base, ts->n);
    err |= big_pow(&rhs, &nQ, ts->n);
    err |= big_mul_u32(&rhs, 2);
    if (err) return UT_ERR;
    return big_cmp(&lhs, &rhs) <= 0 ? UT_PASS : UT_FAIL;
}

/* ---------------- (본문 TODO) 3: 쌍곡선 경계 (hyperbolic bound) ---------------- */
/* prod (U_i + 1) <= 2  <=>  prod (C_i + T_i) <= 2 prod T_i.  D < T 가 있으면 UT_NA. */
static ut_t hyperbolic_test(const taskset_t *ts) {
    for (int i = 0; i < ts->n; i++) if (ts->t[i].D != ts->t[i].T) return UT_NA;
    big_t lhs, rhs; int err = 0;
    big_set(&lhs, 1); big_set(&rhs, 2);
    for (int i = 0; i < ts->n; i++) {
        err |= big_mul_u32(&lhs, ts->t[i].C + ts->t[i].T);   /* C <= T <= 2^24 → 합이 32비트 안 */
        err |= big_mul_u32(&rhs, ts->t[i].T);
    }
    if (err) return UT_ERR;
    return big_cmp(&lhs, &rhs) <= 0 ? UT_PASS : UT_FAIL;
}

/* ---------------- (본문 TODO) 4: 응답 시간 분석 (RTA) ---------------- */
typedef struct { uint64_t R; int ok; int iters; } rta_t;
/* hp(i) = i 가 아닌 태스크 중 prio >= prio(i)  (같은 우선순위는 보수적으로 간섭에 넣는다).
 * w0 = C_i + B_i,  w_{k+1} = C_i + B_i + sum_{j in hp(i)} ceil((w_k + J_j) / T_j) * C_j
 * w_{k+1} + J_i > D_i → 멈추고 ok=0 (R = w_{k+1} + J_i, 진짜 응답 시간이 아니라 "D 를 넘은 지점").
 * w_{k+1} == w_k   → 수렴, ok=1, R = w + J_i.   iters = 계산한 w_{k+1} 개수. 정수 ceil 만 쓴다. */
static rta_t rta(const taskset_t *ts, int i) {
    const task_t *ti = &ts->t[i];
    rta_t r = { 0, 0, 0 };
    uint64_t w = (uint64_t)ti->C + ti->B;
    for (;;) {
        uint64_t nw = (uint64_t)ti->C + ti->B;
        for (int j = 0; j < ts->n; j++) {
            const task_t *tj = &ts->t[j];
            if (j == i || tj->prio < ti->prio) continue;
            nw += ceil_div(w + tj->J, tj->T) * tj->C;
        }
        r.iters++;
        if (nw + ti->J > ti->D) { r.R = nw + ti->J; r.ok = 0; return r; }
        if (nw == w)            { r.R = w + ti->J;  r.ok = 1; return r; }
        w = nw;
    }
}

/* ---------------- (본문 TODO) 5: 임계 순간 시뮬레이션 (고정 우선순위, 선점) ---------------- */
#define SIM_TRACE 40
typedef struct {
    uint64_t max_resp[MAXN], first_resp[MAXN];   /* 출시(release) → 완료, 단위 = tick */
    uint32_t jobs[MAXN], misses[MAXN];           /* 완료한 작업 수, 데드라인 넘긴 작업 수 */
    uint32_t preempt;                            /* 일을 남긴 채 더 높은 태스크에게 밀린 횟수 */
    int8_t who[SIM_TRACE];                       /* 처음 SIM_TRACE tick: 실행한 태스크, -1 idle, -2 blocker */
    uint16_t pend[SIM_TRACE];                    /* 처음 SIM_TRACE tick: 미완료 작업이 있는 태스크 비트마스크 */
} sim_t;
/* k 번째 작업의 출시 시각. jit=0: k*T.  jit=1 (지터 최악 패턴): 0, T-J, 2T-J, ... (첫 작업만 J 만큼 늦게 출시된 셈) */
static uint64_t rel_time(const task_t *t, uint64_t k, int jit) {
    return k == 0 ? 0 : k * t->T - (jit ? t->J : 0);
}
/* [0, horizon) 를 1 tick 씩. 매 tick: (1) 출시 시각 <= t 인 작업을 출시, (2) t < blocker 면 CPU 는
 * "임계 구역 안의 낮은 태스크" (선점 불가, IPCP 블로킹 흉내) 가 쓴다, (3) 아니면 미완료 작업이 있는 태스크 중
 * prio 최고가 1 tick 실행. 작업은 출시 순서대로 처리 (데드라인을 넘겨도 계속 실행). */
static void sim_fp(const taskset_t *ts, uint64_t horizon, uint32_t blocker, int jit, sim_t *s) {
    memset(s, 0, sizeof *s);
    uint64_t rel[MAXN] = { 0 }, done[MAXN] = { 0 };
    uint32_t rem[MAXN];
    for (int i = 0; i < ts->n; i++) rem[i] = ts->t[i].C;
    int last = -1;
    for (uint64_t t = 0; t < horizon; t++) {
        uint16_t pm = 0;
        for (int i = 0; i < ts->n; i++) {
            while (rel_time(&ts->t[i], rel[i], jit) <= t) rel[i]++;
            if (rel[i] > done[i]) pm |= (uint16_t)(1u << i);
        }
        if (t < SIM_TRACE) { s->pend[t] = pm; s->who[t] = -1; }
        if (t < blocker) { if (t < SIM_TRACE) s->who[t] = -2; last = -1; continue; }
        int run = -1;
        for (int i = 0; i < ts->n; i++)
            if (rel[i] > done[i] && (run < 0 || ts->t[i].prio > ts->t[run].prio)) run = i;
        if (run < 0) { last = -1; continue; }
        if (t < SIM_TRACE) s->who[t] = (int8_t)run;
        if (last >= 0 && last != run && rel[last] > done[last] && rem[last] < ts->t[last].C) s->preempt++;
        last = run;
        if (--rem[run] == 0) {
            uint64_t resp = t + 1 - rel_time(&ts->t[run], done[run], jit);
            if (s->jobs[run] == 0) s->first_resp[run] = resp;
            if (resp > s->max_resp[run]) s->max_resp[run] = resp;
            if (resp > ts->t[run].D) s->misses[run]++;
            s->jobs[run]++; done[run]++;
            rem[run] = ts->t[run].C;
        }
    }
}

/* ---------------- (본문 TODO) 6: IPCP 블로킹 항 B_i ---------------- */
typedef struct { int task; int res; uint32_t cs; } use_t;   /* task 가 res 를 최대 cs 동안 쥔다 */
/* ceiling(r) = r 을 쓰는 태스크 prio 의 최댓값.
 * B_i = max{ cs : prio(task) < prio(i) 이고 ceiling(res) >= prio(i) }  (없으면 0). prio 는 이미 할당돼 있다. */
static void blocking_ipcp(taskset_t *ts, const use_t *u, int nu) {
    int ceil_[MAXN];
    for (int r = 0; r < MAXN; r++) ceil_[r] = -1;
    for (int k = 0; k < nu; k++)
        if (ts->t[u[k].task].prio > ceil_[u[k].res]) ceil_[u[k].res] = ts->t[u[k].task].prio;
    for (int i = 0; i < ts->n; i++) {
        uint32_t b = 0;
        for (int k = 0; k < nu; k++)
            if (ts->t[u[k].task].prio < ts->t[i].prio && ceil_[u[k].res] >= ts->t[i].prio && u[k].cs > b) b = u[k].cs;
        ts->t[i].B = b;
    }
}

/* ======================= 추가 연습 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static int all_ok(const taskset_t *ts) {
    for (int i = 0; i < ts->n; i++) if (!rta(ts, i).ok) return 0;
    return 1;
}
static taskset_t load(const char *spec, policy_t pol) {
    taskset_t ts;
    if (ts_parse(&ts, spec) != PARSE_OK) { printf("parse error: %s\n", spec); ts.n = 0; }
    assign_prio(&ts, pol);
    return ts;
}
static uint32_t g_rng = 0x2026111Bu;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

/* 연습 1 — 민감도 분석: 태스크 i 의 C 를 최대 얼마까지 늘려도 세트 전체가 통과하나?
 * C_i 가 커지면 모든 R 이 단조 증가 → [C_i, D_i] 에서 이분 탐색. 원래 세트가 실패면 0. */
static uint32_t max_c(taskset_t *ts, int i) {
    uint32_t save = ts->t[i].C;
    if (!all_ok(ts)) return 0;
    uint32_t lo = save, hi = ts->t[i].D;               /* lo 는 통과, hi 는 모름 */
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo + 1) / 2;
        ts->t[i].C = mid;
        if (all_ok(ts)) lo = mid; else hi = mid - 1;
    }
    ts->t[i].C = save;
    return lo;
}

/* 연습 2 — 문맥 전환 오버헤드: 작업마다 전환 2번(들어올 때, 나갈 때) → C' = C + 2*cs.
 * 세트가 통과하는 최대 cs (µs) 를 선형 탐색으로 (0 부터, 실패 직전 값). 처음부터 실패면 -1. */
static int max_switch_cost(const taskset_t *ts0) {
    int best = -1;
    for (uint32_t cs = 0; cs < 1000; cs++) {
        taskset_t ts = *ts0;
        int valid = 1;
        for (int i = 0; i < ts.n; i++) {
            ts.t[i].C += 2 * cs;
            if (ts.t[i].C > ts.t[i].D) valid = 0;
        }
        if (!valid || !all_ok(&ts)) break;
        best = (int)cs;
    }
    return best;
}

/* 연습 3 — 조화 주기 (harmonic): 모든 주기 쌍이 서로 나누어떨어지면 RM 은 U <= 1 까지 통과 (D = T). */
static int is_harmonic(const taskset_t *ts) {
    for (int i = 0; i < ts->n; i++)
        for (int j = 0; j < ts->n; j++) {
            uint32_t a = ts->t[i].T, b = ts->t[j].T;
            if (a <= b && b % a != 0) return 0;
        }
    return 1;
}

/* 연습 4 — Audsley 최적 우선순위 할당 (OPA): 가장 낮은 레벨부터, "나머지 전부가 나보다 높다" 고 두고
 * RTA 가 통과하는 태스크를 하나 골라 그 레벨에 고정한다. 못 고르면 어떤 고정 우선순위로도 불가능. */
static int opa(taskset_t *ts) {
    int assigned[MAXN] = { 0 };
    for (int lvl = 0; lvl < ts->n; lvl++) {
        int found = -1;
        for (int c = 0; c < ts->n && found < 0; c++) {
            if (assigned[c]) continue;
            int hi = ts->n - 1;
            for (int j = 0; j < ts->n; j++) if (!assigned[j]) ts->t[j].prio = (j == c) ? lvl : hi--;
            if (rta(ts, c).ok) found = c;
        }
        if (found < 0) return 0;
        assigned[found] = 1;
        ts->t[found].prio = lvl;
    }
    return 1;
}

int main(int argc, char **argv) {
    int verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    taskset_t ts;

    /* 연습 1: 본문 Part C (C2, 고친 뒤) 의 log 는 C 를 얼마까지? comms 는? */
    enum { MOTOR, IMU, COMMS, LOG };
    enum { SPI, SETPT };
    use_t uses[] = { { IMU, SPI, 80 }, { LOG, SPI, 200 }, { MOTOR, SETPT, 20 }, { COMMS, SETPT, 50 } };
    ts = load("motor 300 1000; imu 400 2000 J=100; comms 1500 10000 4000 J=300; log 20000 100000", POL_DM);
    blocking_ipcp(&ts, uses, 4);
    uint32_t mlog = max_c(&ts, LOG), mcom = max_c(&ts, COMMS), mimu = max_c(&ts, IMU);
    printf("ex1 max C: log %u (now 20000), comms %u (now 1500), imu %u (now 400)\n", mlog, mcom, mimu);
    ts.t[LOG].C = mlog;
    printf("ex1 with log C=%u: U~%u.%04u (LL bound for n=4 ~%u.%04u, not applicable: D < T)\n", mlog,
           u_ppm(&ts) / 1000000, (u_ppm(&ts) % 1000000) / 100, LL_PPM[4] / 1000000, (LL_PPM[4] % 1000000) / 100);
    ts.t[LOG].C = 20000;
    CHECK(mlog == 34700 && mcom == 1500 && mimu == 400);
    ts.t[LOG].C = mlog;     CHECK(all_ok(&ts));
    ts.t[LOG].C = mlog + 1; CHECK(!all_ok(&ts));
    ts.t[LOG].C = 20000;

    /* 연습 2: 본문 A3 세트 (B, J 없음) 가 견디는 문맥 전환 비용 */
    ts = load("motor 300 1000; imu 400 2000; comms 1500 10000; log 20000 100000", POL_RM);
    int mcs = max_switch_cost(&ts);
    printf("ex2 max context-switch cost: %d us per switch (2 per job)\n", mcs);
    CHECK(mcs == 46);
    ts = load("t1 2 5; t2 4 7", POL_RM);
    CHECK(max_switch_cost(&ts) == -1);                  /* 원래도 실패 */

    /* 연습 3: 조화 주기 무작위 300 세트 — RTA 통과 ⇔ U <= 1, 그리고 LL 은 그중 일부만 통과 */
    {
        static const uint32_t hmenu[] = { 1000, 2000, 4000, 8000, 16000 };
        int agree = 1, n_u1 = 0, n_ll = 0, sets = 300;
        for (int k = 0; k < sets; k++) {
            int n = 2 + (int)(xs32() % 4);
            ts.n = n;
            for (int i = 0; i < n; i++) {
                task_t *t = &ts.t[i];
                memset(t, 0, sizeof *t);
                snprintf(t->name, sizeof t->name, "h%d", i);
                t->T = t->D = hmenu[xs32() % 5];
                t->C = 1 + xs32() % (2 * t->T / (uint32_t)n);
                if (t->C > t->T) t->C = t->T;
            }
            assign_prio(&ts, POL_RM);
            int u1 = u_le_one(&ts) == 1;
            n_u1 += u1; n_ll += ll_test(&ts) == UT_PASS;
            if (!is_harmonic(&ts) || all_ok(&ts) != u1) agree = 0;
        }
        printf("ex3 harmonic %d sets: U<=1 %d, RTA pass == U<=1 for all: %s, LL pass only %d\n", sets, n_u1,
               agree ? "yes" : "NO", n_ll);
        CHECK(agree && n_ll < n_u1);
        ts = load("a 1 4; b 1 6", POL_RM);
        CHECK(!is_harmonic(&ts));
        /* 정확히 U = 1 인 조화 세트: double 로 더하면 1.0000000000000002 가 나오는 그 세트 (demo_float_trap.c) */
        ts = load("a 200 1000; b 400 1000; c 300 1000; d 100 1000", POL_RM);
        CHECK(is_harmonic(&ts) && u_le_one(&ts) == 1 && all_ok(&ts) && rta(&ts, 3).R == 1000);
    }

    /* 연습 4: 지터가 있으면 DM 은 최적이 아니다 — OPA 는 찾는다 */
    ts = load("A 1 17 11 J=10; B 5 6 6", POL_DM);
    int dm_ok = all_ok(&ts);
    rta_t ra = rta(&ts, 0);
    int o = opa(&ts);
    printf("ex4 DM: A R=%llu (D=11) %s | OPA: %s, prio A=%d B=%d, R_A=%llu R_B=%llu\n", (unsigned long long)ra.R,
           dm_ok ? "ok" : "MISS", o ? "found" : "none", ts.t[0].prio, ts.t[1].prio,
           (unsigned long long)rta(&ts, 0).R, (unsigned long long)rta(&ts, 1).R);
    CHECK(!dm_ok && ra.R == 16);
    CHECK(o && ts.t[0].prio == 1 && rta(&ts, 0).R == 11 && rta(&ts, 1).R == 6 && all_ok(&ts));
    /* 지터가 없고 D <= T 면 DM 이 최적 → 무작위 세트에서 OPA 성공 ⇔ DM 성공 */
    {
        int agree = 1, n_ok = 0, sets = 500;
        for (int k = 0; k < sets; k++) {
            ts.n = 2 + (int)(xs32() % 4);
            for (int i = 0; i < ts.n; i++) {
                task_t *t = &ts.t[i];
                memset(t, 0, sizeof *t);
                snprintf(t->name, sizeof t->name, "d%d", i);
                t->T = 4 + xs32() % 37; t->D = 1 + xs32() % t->T; t->C = 1 + xs32() % t->D;
            }
            assign_prio(&ts, POL_DM);
            int dm = all_ok(&ts);
            taskset_t c = ts;
            int op = opa(&c);
            if (op && !all_ok(&c)) agree = 0;
            if (dm != op) agree = 0;
            n_ok += dm;
        }
        printf("ex4 no jitter, %d random D<=T sets: DM ok %d, OPA agrees with DM: %s\n", sets, n_ok, agree ? "yes" : "NO");
        CHECK(agree && n_ok > 0);
    }
    /* 시뮬레이션으로 OPA 결과를 한 번 더 확인 (지터 최악 패턴, 첫 작업) */
    {
        ts = load("A 1 17 11 J=10; B 5 6 6", POL_DM);
        opa(&ts);
        sim_t s;
        uint64_t L = hyperperiod(&ts, 1000000);
        CHECK(L == 102);
        sim_fp(&ts, L, 0, 1, &s);
        CHECK(s.first_resp[1] == 6 && s.first_resp[0] + ts.t[0].J == 11);
        if (verbose) printf("ex4 sim (jitter pattern): A first %llu (+J=10), B first %llu\n",
                            (unsigned long long)s.first_resp[0], (unsigned long long)s.first_resp[1]);
    }
    CHECK(hyperbolic_test(&ts) == UT_NA);                /* D < T 이므로 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
