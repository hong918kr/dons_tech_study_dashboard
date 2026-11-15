/* RTOS-M11 Rate Monotonic 분석: Liu-Layland 경계와 RTA
 *
 * 스케줄 가능성(schedulability) 분석 도구. 입력은 텍스트 태스크 세트:
 *     "name C T [D] [J=jitter] [B=blocking]"   (한 줄 또는 ';' 로 구분, '#' 주석, 단위 = µs 또는 tick)
 *   C = 최악 실행 시간(WCET), T = 주기(최소 도착 간격), D = 상대 데드라인 (생략 시 D = T), C <= D <= T.
 * 모든 판정은 정수로만 한다 (float 비교 없음). 우선순위는 클수록 높다 (FreeRTOS 와 같은 방향).
 *
 * 이 레슨에서 채우는 것 (TODO 1~6):
 *  1. assign_prio()     : RM (T 짧을수록 높음) / DM (D 짧을수록 높음). 동점은 입력 순서가 앞선 쪽이 높다.
 *  2. ll_test()         : U <= n(2^(1/n) - 1)  (Liu & Layland 1973) — 무리수 경계를 n 제곱해서 정수로 판정.
 *  3. hyperbolic_test() : prod(U_i + 1) <= 2   (Bini, Buttazzo, Buttazzo 2003) — 역시 정수로.
 *  4. rta()             : w = C + B + sum_{j in hp(i)} ceil((w + J_j) / T_j) * C_j  고정점 반복, R = w + J_i.
 *                         R > D 가 되는 순간 멈춘다 (발산하는 세트에서도 끝난다).
 *  5. sim_fp()          : 임계 순간(critical instant, 모두 t=0 동시 출시)부터 고정 우선순위 선점 시뮬레이션.
 *                         RTA 가 맞는지 교차 확인용 — 최소 기능만 (RM vs EDF 전체 시뮬레이터는 RTOS-M12).
 *  6. blocking_ipcp()   : 자원 사용 표 → B_i = IPCP 에서 한 번 막힐 수 있는 가장 긴 임계 구역 (RTOS-M05).
 * 파서, 큰 정수(bignum), 하이퍼피리어드, 출력 함수는 주어진 코드다.
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

/* ---------------- TODO 1: RM / DM 우선순위 할당 ---------------- */
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

/* ---------------- TODO 2: Liu–Layland 이용률 경계 (정수 판정) ---------------- */
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

/* ---------------- TODO 3: 쌍곡선 경계 (hyperbolic bound) ---------------- */
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

/* ---------------- TODO 4: 응답 시간 분석 (RTA) ---------------- */
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

/* ---------------- TODO 5: 임계 순간 시뮬레이션 (고정 우선순위, 선점) ---------------- */
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

/* ---------------- TODO 6: IPCP 블로킹 항 B_i ---------------- */
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

/* ---------------- 주어진 코드: 보고서 ---------------- */
static const char *ut_str(ut_t u) { return u == UT_PASS ? "PASS" : u == UT_FAIL ? "FAIL" : u == UT_NA ? "N/A" : "ERR"; }
static void report(const char *title, taskset_t *ts) {
    printf("== %s  (n=%d, U~%u.%04u, LL bound~%u.%04u)\n", title, ts->n, u_ppm(ts) / 1000000,
           (u_ppm(ts) % 1000000) / 100, LL_PPM[ts->n] / 1000000, (LL_PPM[ts->n] % 1000000) / 100);
    printf("   LL %s | hyperbolic %s | U<=1 %s\n", ut_str(ll_test(ts)), ut_str(hyperbolic_test(ts)),
           u_le_one(ts) == 1 ? "yes" : "no");
    printf("   %-8s %6s %7s %7s %5s %5s %4s | %7s %5s %s\n", "task", "C", "T", "D", "J", "B", "prio", "R", "iter", "");
    for (int i = 0; i < ts->n; i++) {
        const task_t *t = &ts->t[i];
        rta_t r = rta(ts, i);
        printf("   %-8s %6u %7u %7u %5u %5u %4d | %7llu %5d %s\n", t->name, t->C, t->T, t->D, t->J, t->B, t->prio,
               (unsigned long long)r.R, r.iters, r.ok ? "ok" : "MISS (stopped at R > D)");
    }
}

/* 하우스 타임라인 형식: R = 실행, r = 출시됐는데 못 돎, . = 할 일 없음.  blk 줄 = blocker (B 구간) */
static void print_sim_timeline(const taskset_t *ts, const sim_t *s, int ticks, uint32_t blocker, const char *title) {
    if (ticks > SIM_TRACE) ticks = SIM_TRACE;
    printf("-- %s\n tick  :", title);
    for (int t = 0; t < ticks; t++) printf(" %d", t % 10);
    printf("\n");
    if (blocker) {
        printf(" %-5s :", "blk");
        for (int t = 0; t < ticks; t++) printf(" %c", s->who[t] == -2 ? 'R' : '.');
        printf("\n");
    }
    for (int i = 0; i < ts->n; i++) {
        printf(" %-5.5s :", ts->t[i].name);
        for (int t = 0; t < ticks; t++)
            printf(" %c", s->who[t] == i ? 'R' : (s->pend[t] >> i & 1u) ? 'r' : '.');
        printf("\n");
    }
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static int all_ok(const taskset_t *ts) {
    for (int i = 0; i < ts->n; i++) if (!rta(ts, i).ok) return 0;
    return 1;
}
static taskset_t load(const char *spec, policy_t pol) {
    taskset_t ts;
    int rc = ts_parse(&ts, spec);
    if (rc != PARSE_OK) { printf("parse error %d: %s\n", rc, spec); ts.n = 0; }
    assign_prio(&ts, pol);
    return ts;
}
/* 하이퍼피리어드 동안 시뮬레이션: RTA ok 인 태스크는 최악 관측 응답 == R, 미스 0. RTA 실패 태스크는 첫 작업이 놓친다. */
static int sim_agrees(const taskset_t *ts, sim_t *s) {
    uint64_t L = hyperperiod(ts, 2000000);
    if (L == 0) return 0;
    sim_fp(ts, L, 0, 0, s);
    for (int i = 0; i < ts->n; i++) {
        rta_t r = rta(ts, i);
        if (r.ok && (s->max_resp[i] != r.R || s->first_resp[i] != r.R || s->misses[i] != 0)) return 0;
        if (!r.ok && !(s->jobs[i] == 0 || s->first_resp[i] > ts->t[i].D)) return 0;
    }
    return 1;
}
static uint32_t g_rng = 0x2026111Bu;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

int main(int argc, char **argv) {
    int verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    taskset_t ts; sim_t s;

    /* ---- Part A: 도구의 핵심 — 할당, 두 경계, RTA, 시뮬레이션 교차 확인 ---- */
    /* A1. 교과서 세트: LL 통과 → RTA 도 통과 */
    ts = load("t1 20 100; t2 40 150; t3 100 350", POL_RM);
    report("A1 textbook (LL passes)", &ts);
    CHECK(ts.n == 3 && ts.t[0].prio == 2 && ts.t[1].prio == 1 && ts.t[2].prio == 0);
    CHECK(ll_test(&ts) == UT_PASS && hyperbolic_test(&ts) == UT_PASS);
    CHECK(rta(&ts, 0).R == 20 && rta(&ts, 1).R == 60 && rta(&ts, 2).R == 240 && all_ok(&ts));
    CHECK(rta(&ts, 2).iters == 4);                                 /* 160 → 220 → 240 → 240 */
    CHECK(sim_agrees(&ts, &s));

    /* A2. 순서를 섞어 넣어도 RM 은 주기로 정한다 */
    ts = load("log 20000 100000; imu 400 2000; motor 300 1000; comms 1500 10000", POL_RM);
    CHECK(ts.t[2].prio == 3 && ts.t[1].prio == 2 && ts.t[3].prio == 1 && ts.t[0].prio == 0);

    /* A3. 펌웨어 세트 (D = T): U = 0.85 > LL 0.7568, 쌍곡선도 실패 — 그러나 RTA 는 통과 */
    ts = load("motor 300 1000; imu 400 2000; comms 1500 10000; log 20000 100000", POL_RM);
    report("A3 firmware set, D = T (LL fails, RTA passes)", &ts);
    CHECK(ll_test(&ts) == UT_FAIL && hyperbolic_test(&ts) == UT_FAIL && u_le_one(&ts) == 1);
    CHECK(rta(&ts, 0).R == 300 && rta(&ts, 1).R == 700 && rta(&ts, 2).R == 3500);
    CHECK(rta(&ts, 3).ok && rta(&ts, 3).R == 58000);
    CHECK(sim_agrees(&ts, &s));
    if (verbose) printf("   sim over hyperperiod %llu: max resp %llu %llu %llu %llu, preemptions %u\n",
                        (unsigned long long)hyperperiod(&ts, 2000000), (unsigned long long)s.max_resp[0],
                        (unsigned long long)s.max_resp[1], (unsigned long long)s.max_resp[2],
                        (unsigned long long)s.max_resp[3], s.preempt);

    /* A4. 조화(harmonic) 주기 U = 1: 두 경계 모두 실패, RTA 통과 (R3 = 80 = D 딱 맞음) */
    ts = load("c 5 20; b 10 40; a 40 80", POL_RM);
    report("A4 harmonic, U = 1", &ts);
    CHECK(ll_test(&ts) == UT_FAIL && hyperbolic_test(&ts) == UT_FAIL && u_le_one(&ts) == 1);
    CHECK(rta(&ts, 0).R == 5 && rta(&ts, 1).R == 15 && rta(&ts, 2).R == 80 && all_ok(&ts));
    CHECK(sim_agrees(&ts, &s));

    /* A5. U <= 1 인데 RTA 실패: (2,5) (4,7), U = 0.971 */
    ts = load("t1 2 5; t2 4 7", POL_RM);
    report("A5 U <= 1 but RM misses", &ts);
    CHECK(u_le_one(&ts) == 1 && ll_test(&ts) == UT_FAIL);
    CHECK(rta(&ts, 0).ok && !rta(&ts, 1).ok && rta(&ts, 1).R == 8);
    CHECK(sim_agrees(&ts, &s) && s.first_resp[1] == 8 && s.misses[1] > 0 && s.misses[0] == 0);
    print_sim_timeline(&ts, &s, 15, 0, "A5 critical instant, RM (t2 first job misses D=7 at t7, finishes t8)");
    CHECK(s.who[0] == 0 && s.who[2] == 1 && s.who[5] == 0 && s.who[7] == 1 && s.who[8] == 1);

    /* ---- Part B: 면접관이 파고드는 경계 사례 ---- */
    /* B1. 경계에 딱 붙은 값: 정수 판정이라 정확하다 */
    ts = load("a 1 1", POL_RM);                                    /* n=1, U=1 = 경계 1 */
    CHECK(ll_test(&ts) == UT_PASS && hyperbolic_test(&ts) == UT_PASS);
    ts = load("a 4142 10000; b 4142 10000", POL_RM);               /* U=0.8284 <= 0.828427 */
    CHECK(ll_test(&ts) == UT_PASS && hyperbolic_test(&ts) == UT_PASS);
    ts = load("a 4143 10000; b 4143 10000", POL_RM);               /* U=0.8286 >  0.828427 */
    CHECK(ll_test(&ts) == UT_FAIL && hyperbolic_test(&ts) == UT_FAIL);
    /* B2. 쌍곡선 = 정확히 2: (1+1/6)(1+5/7) = 2 → PASS. LL 은 실패 (U=0.881). double 이면 2.0000000000000004 */
    ts = load("a 1 6; b 5 7", POL_RM);
    report("B2 hyperbolic product exactly 2", &ts);
    CHECK(ll_test(&ts) == UT_FAIL && hyperbolic_test(&ts) == UT_PASS && all_ok(&ts) && rta(&ts, 1).R == 6);
    /* B3. 높은 태스크 이용률 합이 1 → 낮은 태스크의 w 가 끝없이 커진다. R > D 에서 멈춰야 한다 */
    ts = load("h1 5 10; h2 5 10; low 1 100", POL_RM);
    {
        rta_t r = rta(&ts, 2);
        CHECK(!r.ok && r.R > 100 && r.iters <= 12);
        if (verbose) printf("== B3 hp utilization = 1: low stops after %d iterations at R=%llu\n", r.iters,
                            (unsigned long long)r.R);
    }
    /* B4. D < T: LL/쌍곡선은 적용 불가(N/A). RM 은 실패, DM 은 통과 */
    ts = load("A 1 4 4; B 2 6 2", POL_RM);
    report("B4 D < T under RM", &ts);
    CHECK(ll_test(&ts) == UT_NA && hyperbolic_test(&ts) == UT_NA);
    CHECK(ts.t[0].prio == 1 && !rta(&ts, 1).ok && rta(&ts, 1).R == 3);
    assign_prio(&ts, POL_DM);
    report("B4 D < T under DM", &ts);
    CHECK(ts.t[1].prio == 1 && rta(&ts, 1).R == 2 && rta(&ts, 0).R == 3 && all_ok(&ts));
    CHECK(sim_agrees(&ts, &s));
    /* B5. 같은 주기 → 입력 순서가 동점 규칙. 결정적이어야 한다 */
    ts = load("x 1 10; y 2 10; z 3 10", POL_RM);
    CHECK(ts.t[0].prio == 2 && ts.t[1].prio == 1 && ts.t[2].prio == 0 && rta(&ts, 2).R == 6);
    /* B6. 파서: 범위·문법 오류 */
    CHECK(ts_parse(&ts, "a 5 4") == PARSE_RANGE);                /* C > T */
    CHECK(ts_parse(&ts, "a 2 10 12") == PARSE_RANGE);            /* D > T */
    CHECK(ts_parse(&ts, "a 0 10") == PARSE_RANGE);               /* C = 0 */
    CHECK(ts_parse(&ts, "a 1 10 J=10") == PARSE_RANGE);          /* J >= T */
    CHECK(ts_parse(&ts, "a 1") == PARSE_SYNTAX);
    CHECK(ts_parse(&ts, "a 1 10 x") == PARSE_SYNTAX);
    CHECK(ts_parse(&ts, "# only comment\n") == PARSE_SYNTAX);
    CHECK(ts_parse(&ts, "a 1 10 # ok\n b 2 20 15 J=3 B=4") == PARSE_OK && ts.n == 2 && ts.t[1].D == 15 &&
          ts.t[1].J == 3 && ts.t[1].B == 4);
    {
        char big[MAXN * 12 + 16] = ""; char line[16];
        for (int i = 0; i <= MAXN; i++) { snprintf(line, sizeof line, "t%d 1 99;", i); strcat(big, line); }
        CHECK(ts_parse(&ts, big) == PARSE_TOO_MANY);
    }
    /* B7. 하이퍼피리어드 오버플로 가드 (서로소 큰 주기) */
    ts = load("p 1 999983; q 1 999979; r 1 999961", POL_RM);
    CHECK(hyperperiod(&ts, 2000000) == 0 && ll_test(&ts) == UT_PASS);
    /* B8. 16 개 태스크, 큰 주기에서도 bignum 이 넘치지 않는다 */
    {
        char big[MAXN * 32] = ""; char line[32];
        for (int i = 0; i < MAXN; i++) { snprintf(line, sizeof line, "t%d %u %u;", i, 400000u + i, 9999991u - 2u * i); strcat(big, line); }
        ts = load(big, POL_RM);
        CHECK(ts.n == MAXN && ll_test(&ts) == UT_PASS && hyperbolic_test(&ts) == UT_PASS);
    }
    /* B9. 무작위 세트 400개: LL PASS ⇒ 쌍곡선 PASS ⇒ RTA PASS ⇒ U <= 1, 그리고 시뮬레이션 == RTA */
    {
        static const uint32_t menu[] = { 4, 5, 6, 8, 10, 12, 15, 20, 24, 30, 40, 60 };
        int n_ll = 0, n_hb = 0, n_rta = 0, n_u1 = 0, imp_bad = 0, sim_bad = 0, sets = 400;
        for (int k = 0; k < sets; k++) {
            int n = 2 + (int)(xs32() % 4);
            ts.n = n;
            for (int i = 0; i < n; i++) {
                task_t *t = &ts.t[i];
                memset(t, 0, sizeof *t);
                snprintf(t->name, sizeof t->name, "r%d", i);
                t->T = t->D = menu[xs32() % 12];
                uint32_t cmax = (2 * t->T) / (uint32_t)n; if (cmax < 1) cmax = 1;
                t->C = 1 + xs32() % cmax; if (t->C > t->T) t->C = t->T;
            }
            assign_prio(&ts, POL_RM);
            int ll = ll_test(&ts) == UT_PASS, hb = hyperbolic_test(&ts) == UT_PASS, ok = all_ok(&ts), u1 = u_le_one(&ts) == 1;
            n_ll += ll; n_hb += hb; n_rta += ok; n_u1 += u1;
            if ((ll && !hb) || (hb && !ok) || (ok && !u1)) imp_bad++;
            if (!sim_agrees(&ts, &s)) sim_bad++;
        }
        printf("random %d sets: LL pass %d, hyperbolic pass %d, RTA pass %d, U<=1 %d | implication violations %d, sim mismatches %d\n",
               sets, n_ll, n_hb, n_rta, n_u1, imp_bad, sim_bad);
        CHECK(imp_bad == 0 && sim_bad == 0);
        CHECK(n_ll < n_hb && n_hb < n_rta && n_rta < n_u1);             /* 경계가 점점 덜 비관적 */
    }

    /* ---- Part C: 펌웨어 시나리오 — 블로킹 B (IPCP) + 릴리즈 지터 J ---- */
    /* motor 1 kHz, IMU 500 Hz (DRDY ISR 로 깨움, J=100), comms (CAN RX, D=4 ms, J=300), log (SPI 플래시)
     * 자원: SPI 버스 (imu, log 공유), setpoint 뮤텍스 (motor, comms 공유) */
    enum { MOTOR, IMU, COMMS, LOG };
    enum { SPI, SETPT };
    const char *fw = "motor 300 1000; imu 400 2000 J=100; comms 1500 10000 4000 J=300; log 20000 100000";
    use_t uses[] = { { IMU, SPI, 80 }, { LOG, SPI, 1200 }, { MOTOR, SETPT, 20 }, { COMMS, SETPT, 50 } };
    ts = load(fw, POL_DM);
    blocking_ipcp(&ts, uses, 4);
    report("C1 firmware + IPCP blocking + jitter (flash page write holds SPI 1200 us)", &ts);
    CHECK(ts.t[MOTOR].B == 50 && ts.t[IMU].B == 1200 && ts.t[COMMS].B == 1200 && ts.t[LOG].B == 0);
    CHECK(ll_test(&ts) == UT_NA);
    CHECK(rta(&ts, MOTOR).ok && rta(&ts, MOTOR).R == 350);
    CHECK(!rta(&ts, IMU).ok && !rta(&ts, COMMS).ok);
    /* 시뮬레이션 (블로커 = B_i, 지터 최악 패턴) 의 첫 작업 응답 + J_i 도 D 를 넘는다 */
    sim_fp(&ts, 100000, ts.t[IMU].B, 1, &s);
    CHECK(s.first_resp[IMU] == 2500 && s.first_resp[IMU] + ts.t[IMU].J > ts.t[IMU].D);
    if (verbose) printf("   sim (blocker 1200, jitter pattern): imu first response %llu (+J=100 → %llu > D=2000)\n",
                        (unsigned long long)s.first_resp[IMU], (unsigned long long)(s.first_resp[IMU] + ts.t[IMU].J));
    /* 고침: 플래시 쓰기를 256 B 조각으로 나눠 SPI 를 200 us 씩만 쥔다 */
    uses[1].cs = 200;
    blocking_ipcp(&ts, uses, 4);
    report("C2 fix: flash write split into 200 us chunks", &ts);
    CHECK(ts.t[IMU].B == 200 && ts.t[COMMS].B == 200);
    CHECK(rta(&ts, IMU).ok && rta(&ts, IMU).R == 1000);
    CHECK(rta(&ts, COMMS).ok && rta(&ts, COMMS).R == 4000);          /* D = 4000 에 딱 맞음 */
    CHECK(all_ok(&ts) && rta(&ts, LOG).R == 58700);
    /* 교차 확인: 태스크마다 블로커 = B_i, 지터 최악 패턴으로 시뮬레이션 → 첫 작업 응답 == R_i - J_i */
    {
        int agree = 1;
        for (int i = 0; i < ts.n; i++) {
            rta_t r = rta(&ts, i);
            sim_fp(&ts, 200000, ts.t[i].B, 1, &s);
            if (s.first_resp[i] != r.R - ts.t[i].J) agree = 0;
            if (verbose) printf("   sim check %-6s blocker=%4u: first response %6llu, RTA w=%6llu\n", ts.t[i].name,
                                ts.t[i].B, (unsigned long long)s.first_resp[i], (unsigned long long)(r.R - ts.t[i].J));
        }
        CHECK(agree);
    }
    /* B, J 를 빼면 같은 세트가 얼마나 여유 있어 보이는지 (분석에서 빠뜨리면 생기는 착시) */
    for (int i = 0; i < ts.n; i++) { ts.t[i].B = 0; ts.t[i].J = 0; }
    CHECK(rta(&ts, IMU).R == 700 && rta(&ts, COMMS).R == 3500);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
