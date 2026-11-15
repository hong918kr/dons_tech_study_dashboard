/* SIV-A11 추가 연습 — 한 번에 검증
 *
 * 연습 1  순서 제약(precedence)을 poset 으로 보고, 안전한 순서의 개수를 비트마스크 DP 로 센다 (전수 순열과 교차검증)
 * 연습 2  동기 reset 설계에서 대표 순서가 불가능해지는 이유 — 제약의 사이클, 그리고 CLK_BURST 스텝으로 고치기
 * 연습 3  retention 패턴 집합의 커버리지 판정 (skip + 비트별 stuck-at-0/1)
 * 연습 4  도메인 의존성 그래프 → 전원 올림/내림 순서 (위상 정렬, 사이클 검출)
 *
 * solution.c 와 달리 물리 모델 없이 순서 규칙만으로 푼다. 제약 쌍은 solution.c 의 Part B 가 전수 조사로 추출한 것이다.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_total, g_pass;
#define CHECK(e) do { g_total++; if (e) g_pass++; else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

/* ---- 연습 1 ---- */
#define MAXN 8
typedef struct { int n; bool before[MAXN][MAXN]; } poset_t;       /* before[a][b]: a 는 b 보다 앞이어야 함 */
static void poset_init(poset_t *p, int n) { memset(p, 0, sizeof *p); p->n = n; }
static void add(poset_t *p, int a, int b) { p->before[a][b] = true; }

/* 비트마스크 DP: dp[S] = 집합 S 를 앞에 놓는 합법 순서 수. O(2^n · n²) */
static uint64_t count_orders_dp(const poset_t *p) {
    static uint64_t dp[1u << MAXN];
    int n = p->n;
    memset(dp, 0, sizeof(uint64_t) << n);
    dp[0] = 1;
    for (unsigned S = 0; S < (1u << n); S++) {
        if (!dp[S]) continue;
        for (int v = 0; v < n; v++) {
            if (S >> v & 1) continue;
            bool ok = true;                       /* v 앞이어야 하는 원소가 전부 S 안에 있나 */
            for (int u = 0; u < n; u++) if (p->before[u][v] && !(S >> u & 1)) ok = false;
            if (ok) dp[S | 1u << v] += dp[S];
        }
    }
    return dp[(1u << n) - 1];
}
/* 전수 순열로 같은 값을 센다 */
static uint64_t count_orders_brute(const poset_t *p) {
    int a[MAXN], n = p->n;
    uint64_t cnt = 0;
    for (int i = 0; i < n; i++) a[i] = i;
    for (;;) {
        bool ok = true;
        for (int i = 0; i < n && ok; i++) for (int j = i + 1; j < n; j++)
            if (p->before[a[j]][a[i]]) { ok = false; break; }
        cnt += ok;
        int i = n - 2;
        while (i >= 0 && a[i] >= a[i + 1]) i--;
        if (i < 0) break;
        int j = n - 1;
        while (a[j] <= a[i]) j--;
        int t = a[i]; a[i] = a[j]; a[j] = t;
        for (int l = i + 1, r = n - 1; l < r; l++, r--) { t = a[l]; a[l] = a[r]; a[r] = t; }
    }
    return cnt;
}
enum { Q, CK, IS, SV, RO, PO };                        /* 내림 스텝 */
enum { PW, RS, RE, UCK, UIS, BURST };                  /* 올림 스텝 (BURST = 연습 2) */
static void down_poset(poset_t *p) {
    poset_init(p, 6);
    for (int s = CK; s <= PO; s++) add(p, Q, s);
    add(p, CK, SV); add(p, SV, RO); add(p, IS, RO); add(p, SV, PO); add(p, IS, PO);
}
static void up_poset(poset_t *p) {
    poset_init(p, 5);
    add(p, PW, RS); add(p, PW, RE); add(p, PW, UCK); add(p, PW, UIS);
    add(p, RS, RE); add(p, RE, UCK); add(p, RE, UIS);
}

/* ---- 연습 2 ---- */
static void up_poset_sync_broken(poset_t *p) {  /* 동기 reset: 클럭이 먼저 돌아야 reset 이 먹는다 */
    up_poset(p);
    add(p, UCK, RS);                            /* 추가 제약 CLK_ON < RST_OFF → 사이클 생김 */
}
static void up_poset_sync_fixed(poset_t *p) {   /* BURST(유한 클럭 펄스) 스텝을 reset 동안 넣는다 */
    poset_init(p, 6);
    add(p, PW, BURST); add(p, BURST, RS);
    add(p, PW, RS); add(p, RS, RE); add(p, PW, RE);
    add(p, RE, UCK); add(p, RE, UIS); add(p, PW, UCK); add(p, PW, UIS);
}

/* ---- 연습 3 ---- */
/* rst 값을 reset 값으로 갖는 retention 레지스터에, 패턴 집합 pats 로 쓰고 전원 사이클 → 읽기 비교 를 하면
 * (a) retention skip 이 잡히나 (어느 패턴이든 reset 값과 달라야) (b) 비트별 stuck-at-0/1 이 잡히나 (각 비트가 0 인 패턴·1 인 패턴 둘 다 필요) */
static bool pat_covers(const uint32_t *pats, int n, uint32_t rst) {
    bool skip = false;
    for (int i = 0; i < n; i++) skip |= pats[i] != rst;
    if (!skip) return false;
    for (int b = 0; b < 32; b++) {
        bool has0 = false, has1 = false;
        for (int i = 0; i < n; i++) { if (pats[i] >> b & 1) has1 = true; else has0 = true; }
        if (!(has0 && has1)) return false;
    }
    return true;
}

/* ---- 연습 4 ---- */
#define MAXD 8
/* dep[a][b] = true 면 a 의 전원이 켜지려면 b 가 먼저 켜져 있어야 한다. 위상 정렬(Kahn) — out 에 올림 순서. 사이클이면 -1 */
static int power_up_plan(int n, const bool dep[MAXD][MAXD], int *out) {
    int indeg[MAXD] = {0}, cnt = 0;
    bool placed[MAXD] = {false};
    for (int a = 0; a < n; a++) for (int b = 0; b < n; b++) if (dep[a][b]) indeg[a]++;
    while (cnt < n) {
        int pick = -1;
        for (int i = 0; i < n; i++) if (!placed[i] && indeg[i] == 0) { pick = i; break; }  /* 작은 번호 우선: 결정적 */
        if (pick < 0) return -1;
        placed[pick] = true;
        out[cnt++] = pick;
        for (int a = 0; a < n; a++) if (dep[a][pick]) indeg[a]--;
    }
    return cnt;
}
/* 내림 순서는 올림의 역순 */
static void power_down_plan(int n, const int *up, int *down) { for (int i = 0; i < n; i++) down[i] = up[n - 1 - i]; }

int main(void) {
    /* 연습 1 */
    poset_t p;
    down_poset(&p);
    CHECK(count_orders_dp(&p) == 6 && count_orders_brute(&p) == 6);
    up_poset(&p);
    CHECK(count_orders_dp(&p) == 2 && count_orders_brute(&p) == 2);
    uint64_t cu = count_orders_dp(&p);
    down_poset(&p);
    printf("ex1: down poset orders=%llu up poset orders=%llu (dp=brute)\n",
           (unsigned long long)count_orders_dp(&p), (unsigned long long)cu);
    poset_init(&p, 7);                                       /* 임의 포셋: 교차검증 */
    add(&p, 0, 2); add(&p, 1, 2); add(&p, 2, 5); add(&p, 3, 4); add(&p, 1, 6);
    CHECK(count_orders_dp(&p) == count_orders_brute(&p));
    poset_init(&p, 4);                                       /* 제약 없음 = 4! */
    CHECK(count_orders_dp(&p) == 24);

    /* 연습 2 */
    up_poset_sync_broken(&p);
    uint64_t broken = count_orders_dp(&p);
    CHECK(broken == 0);
    up_poset_sync_fixed(&p);
    uint64_t fixed = count_orders_dp(&p);
    CHECK(fixed == 2);
    printf("ex2: sync-reset without BURST: %llu orders (infeasible) / with BURST: %llu orders\n",
           (unsigned long long)broken, (unsigned long long)fixed);

    /* 연습 3 */
    uint32_t r = 0x10u, a = 0xA5A5C33Cu;
    uint32_t p1[] = {r}, p2[] = {~r}, p3[] = {r, ~r}, p4[] = {a, ~a}, p5[] = {a}, p6[] = {0, 0xFFFFFFFFu};
    CHECK(!pat_covers(p1, 1, r));          /* reset 값 하나: skip 을 못 잡는다 */
    CHECK(!pat_covers(p2, 1, r));          /* ~reset 하나: skip 은 잡지만 비트마다 극성 하나뿐 */
    CHECK(pat_covers(p3, 2, r));
    CHECK(pat_covers(p4, 2, r));
    CHECK(!pat_covers(p5, 1, r));
    CHECK(pat_covers(p6, 2, r));
    uint32_t q2[] = {0xF0F0F0F0u, 0x0F0F0F0Fu}, q3[] = {0xF0F0F0F0u, 0xF0F0F0F0u};
    CHECK(pat_covers(q2, 2, 0x00u));
    CHECK(!pat_covers(q3, 2, 0xF0F0F0F0u));   /* 패턴 == reset 이고 보완 패턴 없음 */

    /* 연습 4: NOC 없이 AON 에 달린 PERI, NOC 뒤에 ISP/NPU, NPU 는 ISP 의 ISO-CLK 공유 → 예시 */
    enum { AON, NOC, ISP, NPU, PERI, ND };
    bool dep[MAXD][MAXD] = {{0}};
    dep[NOC][AON] = dep[PERI][AON] = dep[ISP][NOC] = dep[NPU][NOC] = dep[NPU][ISP] = true;
    int up[MAXD], down[MAXD];
    int n = power_up_plan(ND, (const bool (*)[MAXD])dep, up);
    CHECK(n == ND);
    power_down_plan(ND, up, down);
    int pos[ND];
    for (int i = 0; i < ND; i++) pos[up[i]] = i;
    CHECK(pos[AON] < pos[NOC] && pos[NOC] < pos[ISP] && pos[ISP] < pos[NPU] && pos[AON] < pos[PERI]);
    CHECK(down[0] == up[ND - 1] && down[ND - 1] == AON);
    static const char *names[] = {"AON", "NOC", "ISP", "NPU", "PERI"};
    printf("ex4: up order:");
    for (int i = 0; i < ND; i++) printf(" %s", names[up[i]]);
    printf(" | down order:");
    for (int i = 0; i < ND; i++) printf(" %s", names[down[i]]);
    printf("\n");
    dep[AON][NPU] = true;                                 /* AON 이 NPU 에 의존 — 사이클 */
    CHECK(power_up_plan(ND, (const bool (*)[MAXD])dep, up) == -1);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
