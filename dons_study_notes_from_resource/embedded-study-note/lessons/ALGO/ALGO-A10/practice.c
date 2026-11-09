/* ALGO-A10 추가 연습 — 한 번에 검증
 *   연습 1  wait-for 그래프 스냅샷: "지금" 교착인가 (출력 차수 ≤ 1 → O(T) 사이클 찾기)
 *   연습 2  GoodLock 규칙: 후보 사이클이 실제로 동시에 성립할 수 있나 (스레드 서로 다름 + 게이트 서로소)
 *   연습 3  선언된 락 계층(lock level) 검사기: 첫 위반 이벤트 찾기
 *   연습 4  SCC ≠ 모든 사이클: 완전 유향 그래프 K_n 의 단순 사이클 수 폭발
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------- 연습 1: wait-for 그래프 ----------
 * owner[l] = 락 l 을 쥔 스레드 (-1 = 비었음), waiting[t] = 스레드 t 가 기다리는 락 (-1 = 안 기다림).
 * 스레드 t → owner[waiting[t]] 간선은 스레드마다 최대 1개 → "함수 그래프" 라서 SCC 없이 걸어가며 색칠하면 된다.
 * 사이클이 있으면 그 스레드들을 가장 작은 번호부터 cyc[] 에 넣고 길이 반환, 없으면 0. */
#define MAXT 32
static int wfg_cycle(int nt, const int *owner, const int *waiting, int *cyc) {
    int color[MAXT] = {0};                         /* 0 = 미방문, 1 = 이번 걸음에서 방문 중, 2 = 끝남 */
    int best = 0;
    for (int s = 0; s < nt; s++) {
        if (color[s]) continue;
        int path[MAXT], np = 0, t = s;
        while (t >= 0 && color[t] == 0) {
            color[t] = 1; path[np++] = t;
            t = waiting[t] >= 0 ? owner[waiting[t]] : -1;
        }
        if (t >= 0 && color[t] == 1 && best == 0) {  /* 이번 걸음 안으로 돌아왔다 = 사이클 */
            int i = 0;
            while (path[i] != t) i++;
            int len = np - i, m = i;
            for (int j = i; j < np; j++) if (path[j] < path[m]) m = j;
            for (int j = 0; j < len; j++) cyc[j] = path[i + (m - i + j) % len];
            best = len;
        }
        for (int j = 0; j < np; j++) color[path[j]] = 2;
    }
    return best;
}

/* ---------- 연습 2: GoodLock 규칙 ----------
 * 사이클의 간선 i 는 스레드 thread[i] 가 "게이트" 락 집합 gates[i] (비트마스크) 를 쥔 채 만들었다.
 * 교착이 실제로 성립하려면 k 개 간선이 동시에 성립해야 한다: (1) 스레드가 모두 다르고
 * (2) 어떤 두 간선도 같은 게이트를 공유하지 않는다 (공유하면 둘 중 하나는 그 게이트에서 기다리는 중). */
static bool cycle_feasible(int k, const int *thread, const uint32_t *gates) {
    for (int i = 0; i < k; i++)
        for (int j = i + 1; j < k; j++)
            if (thread[i] == thread[j] || (gates[i] & gates[j]) != 0u) return false;
    return true;
}

/* ---------- 연습 3: 락 계층 검사기 ----------
 * 이벤트 i: 스레드 ev_t[i] 가 ev_acq[i] ? 획득 : 해제 를 락 ev_l[i] 에 한다.
 * 획득은 "그 스레드가 쥔 모든 락의 레벨보다 엄격히 큰 레벨" 이어야 한다. 첫 위반 이벤트 번호, 없으면 -1. */
#define MAXL 16
static int level_check(int ne, const int *ev_t, const bool *ev_acq, const int *ev_l, const int *level) {
    int held[MAXT][MAXL], nh[MAXT] = {0};
    for (int i = 0; i < ne; i++) {
        int t = ev_t[i], l = ev_l[i];
        if (ev_acq[i]) {
            for (int j = 0; j < nh[t]; j++) if (level[held[t][j]] >= level[l]) return i;
            held[t][nh[t]++] = l;
        } else {
            for (int j = 0; j < nh[t]; j++)
                if (held[t][j] == l) { held[t][j] = held[t][--nh[t]]; break; }
        }
    }
    return -1;
}

/* ---------- 연습 4: K_n 의 단순 사이클 수 ----------
 * 각 사이클을 "가장 작은 정점에서 출발" 할 때만 센다 → 중복 없이 정확히 한 번. 길이 ≥ 2 (자기 루프 없음). */
static long long cyc_count;
static void dfs_cycles(int n, int start, int v, uint32_t used, int depth) {
    for (int w = start; w < n; w++) {
        if (w == v) continue;                      /* K_n 은 모든 u≠w 사이에 간선 */
        if (w == start) { if (depth >= 2) cyc_count++; continue; }   /* depth = 경로의 정점 수 */
        if (used >> w & 1u) continue;
        dfs_cycles(n, start, w, used | 1u << w, depth + 1);
    }
}
static long long count_cycles_kn(int n) {
    cyc_count = 0;
    for (int s = 0; s < n; s++) dfs_cycles(n, s, s, 1u << s, 1);
    return cyc_count;
}
static long long formula_kn(int n) {               /* Σ_{k=2..n} C(n,k)·(k-1)! */
    long long total = 0;
    for (int k = 2; k <= n; k++) {
        long long c = 1, f = 1;
        for (int i = 0; i < k; i++) c = c * (n - i) / (i + 1);
        for (int i = 2; i < k; i++) f *= i;
        total += c * f;
    }
    return total;
}

int main(void) {
    /* 연습 1: OS-M10 의 철학자 5명 — P_i 가 F_i 를 쥐고 F_{i+1} 을 기다림 */
    {
        int owner[5], waiting[5], cyc[MAXT];
        for (int i = 0; i < 5; i++) { owner[i] = i; waiting[i] = (i + 1) % 5; }
        int L = wfg_cycle(5, owner, waiting, cyc);
        CHECK(L == 5 && cyc[0] == 0 && cyc[1] == 1 && cyc[4] == 4);
        waiting[4] = -1;                            /* P4 가 오른쪽 포크를 안 기다리면 사슬일 뿐 */
        CHECK(wfg_cycle(5, owner, waiting, cyc) == 0);
        int own2[4] = {3, 2, -1, -1}, wait2[4] = {-1, -1, 0, 1};   /* L0 주인 T3, L1 주인 T2; T2 는 L0, T3 은 L1 대기 */
        L = wfg_cycle(4, own2, wait2, cyc);
        CHECK(L == 2 && cyc[0] == 2 && cyc[1] == 3);
    }
    /* 연습 2: GoodLock */
    {
        int th2[2] = {1, 2}, th_same[2] = {1, 1};
        uint32_t none[3] = {0, 0, 0}, gg[2] = {1u << 5, 1u << 5};
        CHECK(cycle_feasible(2, th2, none));        /* 기본 ABBA */
        CHECK(!cycle_feasible(2, th_same, none));   /* 한 스레드: 자기 자신과는 동시에 못 기다린다 */
        CHECK(!cycle_feasible(2, th2, gg));         /* 공통 게이트 G */
        int th3[3] = {1, 2, 3};
        uint32_t partial[3] = {1u << 6, 0, 1u << 6}; /* G 가 간선 1·3 에만: "모든 간선 공통" 규칙은 못 거르는 경우 */
        uint32_t disjoint[3] = {1u << 6, 1u << 7, 0};
        CHECK(!cycle_feasible(3, th3, partial));
        CHECK(cycle_feasible(3, th3, disjoint));
    }
    /* 연습 3: Part C 의 락에 레벨을 매기고 트레이스 검사 */
    {
        enum { BOARD_CFG, PWR, SPI, DMA, LOG, NL };
        int level[NL] = {10, 20, 30, 40, 90};
        int t[]  = {0, 0, 0, 0,   1, 1, 1, 1,   2, 2, 2, 2};
        bool a[] = {1, 1, 0, 0,   1, 1, 0, 0,   1, 1, 0, 0};
        int l[]  = {SPI, DMA, DMA, SPI,   DMA, PWR, PWR, DMA,   PWR, SPI, SPI, PWR};
        CHECK(level_check(12, t, a, l, level) == 5); /* dma_task: dma_ch2(40) 쥔 채 pwr_domain(20) → 위반 */
        int l_fix[] = {SPI, DMA, DMA, SPI,   DMA, DMA, PWR, PWR,   PWR, SPI, SPI, PWR};
        bool a_fix[] = {1, 1, 0, 0,   1, 0, 1, 0,   1, 1, 0, 0};  /* 고침: DMA 를 먼저 풀고 PWR 은 나중에(지연 작업) */
        CHECK(level_check(12, t, a_fix, l_fix, level) == -1);
    }
    /* 연습 4 */
    {
        int ok = 1;
        for (int n = 2; n <= 9; n++) {
            long long c = count_cycles_kn(n), f = formula_kn(n);
            printf("K_%d: V=%d E=%d  SCC=1  simple cycles=%lld (formula %lld)\n", n, n, n * (n - 1), c, f);
            if (c != f) ok = 0;
        }
        CHECK(ok);
        CHECK(formula_kn(8) == 16064);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
