/* RTOS-A09 추가 연습 — 한 번에 검증
 *
 * 손으로 쓴 작은 트레이스(RTOS-A06 포맷)로 분석기를 넓힌다. 본문(solution.c)과 독립된 축약 파서/타임라인을 쓴다.
 *  연습 1. infer_prio_order()   : "# task ... prio=" 메타 없이 선점 관계만으로 우선순위 순서 복원 (위상 정렬)
 *  연습 2. job_responses()      : 작업별 응답 시간과 여유(slack) → 미스 "전에" 경고 (near miss)
 *  연습 3. rule_overrun()       : 미스한 작업이 평소(중앙값)보다 1.5배 넘게 CPU 를 썼다 = 실행 시간 초과 (WCET 위반)
 *  연습 4. rule_event_hog()     : 주기 없는(이벤트 구동) 상위 태스크가 창의 50 % 이상 = 본문 S2 "틀린 수정"의 UNKNOWN 을 잡는 규칙
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------------- 축약 파서 + 타임라인 ---------------- */
enum { EV_RELEASE, EV_SWITCH_IN, EV_SWITCH_OUT, EV_BLOCK, EV_UNBLOCK, EV_ISR_ENTER, EV_ISR_EXIT, EV_MUTEX_TAKE,
       EV_MUTEX_GIVE, EV_Q_SEND_BLOCK, EV_Q_RECV_BLOCK, EV_DEADLINE_MISS, EV_DROP, EV_COUNT };
static const char *const k_ev[EV_COUNT] = { "RELEASE", "SWITCH_IN", "SWITCH_OUT", "BLOCK", "UNBLOCK", "ISR_ENTER",
    "ISR_EXIT", "MUTEX_TAKE", "MUTEX_GIVE", "Q_SEND_BLOCK", "Q_RECV_BLOCK", "DEADLINE_MISS", "DROP" };
#define MAXN 16
#define MAXE 512
typedef struct { char s[16]; int prio; uint64_t period; int isr; } nm_t;
typedef struct { uint64_t t; int ev, who; char arg[16]; uint32_t num; } ev_t;
typedef struct { int who; uint64_t a, b; } seg_t;
typedef struct {
    int nn, ne, ns;
    nm_t nm[MAXN];
    ev_t e[MAXE];
    seg_t seg[MAXE];
} tr_t;

static int nid(tr_t *t, const char *s) {
    for (int i = 0; i < t->nn; i++) if (!strcmp(t->nm[i].s, s)) return i;
    snprintf(t->nm[t->nn].s, sizeof t->nm[0].s, "%s", s);
    t->nm[t->nn].prio = -1;
    return t->nn++;
}
static void parse(const char *txt, tr_t *t) {
    memset(t, 0, sizeof *t);
    char line[128], a[32], b[32], c[32];
    uint64_t prev = 0;
    int cur = -1, isr = -1;
    while (*txt) {
        size_t n = strcspn(txt, "\n");
        snprintf(line, sizeof line, "%.*s", (int)n, txt);
        txt += n + (txt[n] == '\n');
        if (line[0] == '#') {
            if (sscanf(line, "# task %31s", a) == 1) {
                int i = nid(t, a);
                char *p = strstr(line, "prio="), *q = strstr(line, "period=");
                if (p) t->nm[i].prio = atoi(p + 5);
                if (q) t->nm[i].period = strtoull(q + 7, NULL, 10);
            }
            continue;
        }
        unsigned long ts;
        c[0] = 0;
        if (sscanf(line, "%lu %31s %31s %31s", &ts, a, b, c) < 3) continue;
        ev_t *e = &t->e[t->ne];
        e->t = t->ne ? prev + (uint32_t)((uint32_t)ts - (uint32_t)prev) : ts;
        prev = e->t;
        e->ev = -1;
        for (int k = 0; k < EV_COUNT; k++) if (!strcmp(a, k_ev[k])) e->ev = k;
        e->who = nid(t, b);
        snprintf(e->arg, sizeof e->arg, "%s", c);
        e->num = (uint32_t)strtoul(c, NULL, 10);
        /* 실행 구간: ISR 이 있으면 ISR 몫 */
        if (t->ne && e->t > t->e[t->ne - 1].t) {
            int who = isr >= 0 ? isr : cur;
            if (who >= 0) t->seg[t->ns++] = (seg_t){ who, t->e[t->ne - 1].t, e->t };
        }
        if (e->ev == EV_SWITCH_IN) cur = e->who;
        else if (e->ev == EV_SWITCH_OUT && cur == e->who) cur = -1;
        else if (e->ev == EV_ISR_ENTER) { isr = e->who; t->nm[e->who].isr = 1; }
        else if (e->ev == EV_ISR_EXIT) isr = -1;
        t->ne++;
    }
}
static uint64_t run_in(const tr_t *t, int who, uint64_t a, uint64_t b) {
    uint64_t s = 0;
    for (int i = 0; i < t->ns; i++) {
        if (t->seg[i].who != who) continue;
        uint64_t lo = t->seg[i].a > a ? t->seg[i].a : a, hi = t->seg[i].b < b ? t->seg[i].b : b;
        if (hi > lo) s += hi - lo;
    }
    return s;
}
static int find(const tr_t *t, const char *s) { for (int i = 0; i < t->nn; i++) if (!strcmp(t->nm[i].s, s)) return i; return -1; }

/* ---------------- 연습 1: 선점 관계 → 우선순위 순서 ---------------- */
/* 같은 시각에 SWITCH_OUT A → SWITCH_IN B 인데 A 가 그 시각에 블록(BLOCK/Q_*_BLOCK)하지 않았다면 A 는 선점당했다 → B > A.
 * 간선으로 위상 정렬(Kahn). order[] 에 높은 것부터. 반환: 정렬된 개수, 사이클이면 -1. *unique = 매 단계 후보가 하나였나. */
static int infer_prio_order(const tr_t *t, int order[], int *unique) {
    int edge[MAXN][MAXN] = { { 0 } }, indeg[MAXN] = { 0 }, used[MAXN] = { 0 }, is_task[MAXN] = { 0 }, nt = 0;
    uint64_t blocked_at[MAXN];
    for (int i = 0; i < MAXN; i++) blocked_at[i] = UINT64_MAX;
    for (int k = 0; k < t->ne; k++) {
        const ev_t *e = &t->e[k];
        if (e->ev == EV_SWITCH_IN || e->ev == EV_RELEASE) is_task[e->who] = 1;
        if (e->ev == EV_BLOCK || e->ev == EV_Q_SEND_BLOCK || e->ev == EV_Q_RECV_BLOCK) blocked_at[e->who] = e->t;
        if (e->ev == EV_SWITCH_OUT && k + 1 < t->ne && t->e[k + 1].ev == EV_SWITCH_IN && t->e[k + 1].t == e->t &&
            blocked_at[e->who] != e->t && strcmp(t->nm[e->who].s, "IDLE") && strcmp(t->nm[t->e[k + 1].who].s, "IDLE")) {
            int hi = t->e[k + 1].who, lo = e->who;
            if (!edge[hi][lo]) { edge[hi][lo] = 1; indeg[lo]++; }
        }
    }
    for (int i = 0; i < t->nn; i++) if (is_task[i] && strcmp(t->nm[i].s, "IDLE")) nt++;
    *unique = 1;
    for (int n = 0; n < nt; n++) {
        int pick = -1, cand = 0;
        for (int i = 0; i < t->nn; i++)
            if (is_task[i] && !used[i] && indeg[i] == 0 && strcmp(t->nm[i].s, "IDLE")) { cand++; if (pick < 0) pick = i; }
        if (pick < 0) return -1;                       /* 남았는데 진입 차수 0 이 없다 = 사이클 */
        if (cand > 1) *unique = 0;
        used[pick] = 1; order[n] = pick;
        for (int j = 0; j < t->nn; j++) if (edge[pick][j]) indeg[j]--;
    }
    return nt;
}

/* ---------------- 연습 2: 응답 시간과 여유 ---------------- */
/* 각 RELEASE(r, 인자 = 마감 d) 다음의 "BLOCK X delay" 가 작업 끝 f. resp = f − r, slack = d − f (음수 = 미스).
 * 반환: 작업 수. *max_resp, *near = slack < 주기의 10 % 인 작업 수 (미스 포함) */
static int job_responses(const tr_t *t, int X, uint64_t resp[], int max, uint64_t *max_resp, int *near) {
    int n = 0;
    *max_resp = 0; *near = 0;
    for (int k = 0; k < t->ne && n < max; k++) {
        const ev_t *e = &t->e[k];
        if (e->ev != EV_RELEASE || e->who != X) continue;
        uint64_t r = e->t, d = r + (uint32_t)(e->num - (uint32_t)r);
        for (int j = k + 1; j < t->ne; j++)
            if (t->e[j].ev == EV_BLOCK && t->e[j].who == X && (!strcmp(t->e[j].arg, "delay") || !strcmp(t->e[j].arg, "delay_until"))) {
                uint64_t f = t->e[j].t;
                resp[n++] = f - r;
                if (f - r > *max_resp) *max_resp = f - r;
                if (f > d || (d - f) * 10 < d - r) (*near)++;
                break;
            }
    }
    return n;
}

/* ---------------- 연습 3: 실행 시간 초과 ---------------- */
/* 미스 창 [r,d] 안 X 의 CPU 시간 > 1.5 × (같은 태스크의 "미스 안 한" 작업 CPU 중앙값) → 1. */
static int rule_overrun(const tr_t *t, int X, uint64_t r, uint64_t d, uint64_t *cpu, uint64_t *median) {
    uint64_t v[64]; int n = 0;
    for (int k = 0; k < t->ne && n < 64; k++) {
        const ev_t *e = &t->e[k];
        if (e->ev != EV_RELEASE || e->who != X || e->t == r) continue;
        uint64_t rr = e->t, dd = rr + (uint32_t)(e->num - (uint32_t)rr);
        v[n++] = run_in(t, X, rr, dd);
    }
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && v[j] < v[j - 1]; j--) { uint64_t x = v[j]; v[j] = v[j - 1]; v[j - 1] = x; }
    *cpu = run_in(t, X, r, d);
    *median = n ? v[n / 2] : 0;
    return n > 0 && *cpu * 2 > *median * 3;
}

/* ---------------- 연습 4: 이벤트 구동 상위 태스크의 간섭 ---------------- */
/* period == 0 (메타) 이고 prio > prio(X) 인 태스크가 창의 50 % 이상 → 그 태스크 번호, 없으면 -1 */
static int rule_event_hog(const tr_t *t, int X, uint64_t r, uint64_t d, uint64_t *us) {
    int best = -1; *us = 0;
    for (int i = 0; i < t->nn; i++) {
        if (i == X || t->nm[i].isr || t->nm[i].period != 0 || t->nm[i].prio <= t->nm[X].prio) continue;
        uint64_t u = run_in(t, i, r, d);
        if (u > *us) { *us = u; best = i; }
    }
    return best >= 0 && *us * 2 >= d - r ? best : -1;
}

/* ---------------- 손으로 쓴 트레이스 ---------------- */
static const char *k_s1 =        /* 메타에 prio 없음. CTRL>COMM>LOG 이 선점으로만 드러난다 */
    "# rtos-trace v1 unit=us\n"
    "0 RELEASE LOG 10000\n0 SWITCH_IN LOG\n"
    "200 RELEASE CTRL 1200\n200 SWITCH_OUT LOG\n200 SWITCH_IN CTRL\n"
    "300 BLOCK CTRL delay\n300 SWITCH_OUT CTRL\n300 SWITCH_IN LOG\n"
    "400 RELEASE COMM 5400\n400 SWITCH_OUT LOG\n400 SWITCH_IN COMM\n"
    "1200 RELEASE CTRL 2200\n1200 SWITCH_OUT COMM\n1200 SWITCH_IN CTRL\n"
    "1300 BLOCK CTRL delay\n1300 SWITCH_OUT CTRL\n1300 SWITCH_IN COMM\n"
    "1900 BLOCK COMM delay\n1900 SWITCH_OUT COMM\n1900 SWITCH_IN LOG\n"
    "2200 RELEASE CTRL 3200\n2200 SWITCH_OUT LOG\n2200 SWITCH_IN CTRL\n"
    "2250 BLOCK CTRL delay\n2250 SWITCH_OUT CTRL\n2250 SWITCH_IN IDLE\n";
static const char *k_cycle =     /* 같은 우선순위 time slicing: A↔B 가 서로 "선점" → 순서 없음 */
    "# rtos-trace v1 unit=us\n"
    "0 SWITCH_IN A\n1000 SWITCH_OUT A\n1000 SWITCH_IN B\n2000 SWITCH_OUT B\n2000 SWITCH_IN A\n";
static const char *k_resp =      /* CTRL T=1000: 응답 100, 950(여유 50 < 100 → 경고), 300 */
    "# rtos-trace v1 unit=us\n# task CTRL prio=3 period=1000\n# task BG prio=1 period=0\n"
    "0 SWITCH_IN BG\n"
    "1000 RELEASE CTRL 2000\n1000 SWITCH_OUT BG\n1000 SWITCH_IN CTRL\n1100 BLOCK CTRL delay\n1100 SWITCH_OUT CTRL\n1100 SWITCH_IN BG\n"
    "2000 RELEASE CTRL 3000\n2000 SWITCH_OUT BG\n2000 SWITCH_IN CTRL\n2950 BLOCK CTRL delay\n2950 SWITCH_OUT CTRL\n2950 SWITCH_IN BG\n"
    "3000 RELEASE CTRL 4000\n3000 SWITCH_OUT BG\n3000 SWITCH_IN CTRL\n3300 BLOCK CTRL delay\n3300 SWITCH_OUT CTRL\n3300 SWITCH_IN BG\n";
static const char *k_over =      /* 4번째 작업이 1200 us 를 쓴다 (평소 100) → 미스. 다른 태스크는 끼어들지 않았다 */
    "# rtos-trace v1 unit=us\n# task CTRL prio=3 period=1000\n# task LOG prio=1 period=10000\n"
    "0 RELEASE LOG 10000\n0 SWITCH_IN LOG\n"
    "100 RELEASE CTRL 1100\n100 SWITCH_OUT LOG\n100 SWITCH_IN CTRL\n200 BLOCK CTRL delay\n200 SWITCH_OUT CTRL\n200 SWITCH_IN LOG\n"
    "1100 RELEASE CTRL 2100\n1100 SWITCH_OUT LOG\n1100 SWITCH_IN CTRL\n1190 BLOCK CTRL delay\n1190 SWITCH_OUT CTRL\n1190 SWITCH_IN LOG\n"
    "2100 RELEASE CTRL 3100\n2100 SWITCH_OUT LOG\n2100 SWITCH_IN CTRL\n2210 BLOCK CTRL delay\n2210 SWITCH_OUT CTRL\n2210 SWITCH_IN LOG\n"
    "3100 RELEASE CTRL 4100\n3100 SWITCH_OUT LOG\n3100 SWITCH_IN CTRL\n4100 DEADLINE_MISS CTRL 4100\n4300 BLOCK CTRL delay\n4300 SWITCH_OUT CTRL\n4300 SWITCH_IN LOG\n";
static const char *k_hog =       /* 본문 S2 "틀린 수정": PARSE(p4, 주기 없음) 가 CTRL(p3) 창의 60 % */
    "# rtos-trace v1 unit=us\n# task CTRL prio=3 period=1000\n# task PARSE prio=4 period=0\n# task TELEM prio=1 period=10000\n"
    "2000 RELEASE CTRL 3000\n2000 ISR_ENTER UART_RX\n2010 ISR_EXIT UART_RX\n2010 SWITCH_IN PARSE\n2040 Q_RECV_BLOCK PARSE rxq\n"
    "2040 SWITCH_OUT PARSE\n2040 SWITCH_IN CTRL\n2050 ISR_ENTER UART_RX\n2060 ISR_EXIT UART_RX\n2060 SWITCH_OUT CTRL\n2060 SWITCH_IN PARSE\n"
    "2660 Q_RECV_BLOCK PARSE rxq\n2660 SWITCH_OUT PARSE\n2660 SWITCH_IN CTRL\n3000 DEADLINE_MISS CTRL 3000\n";

static tr_t g_t;

int main(void) {
    int order[MAXN], uniq;
    /* 연습 1 */
    parse(k_s1, &g_t);
    int n = infer_prio_order(&g_t, order, &uniq);
    CHECK(n == 3 && uniq);
    CHECK(n == 3 && !strcmp(g_t.nm[order[0]].s, "CTRL") && !strcmp(g_t.nm[order[1]].s, "COMM") && !strcmp(g_t.nm[order[2]].s, "LOG"));
    parse(k_cycle, &g_t);
    CHECK(infer_prio_order(&g_t, order, &uniq) == -1);
    /* 연습 2 */
    uint64_t resp[16], mx; int nearc;
    parse(k_resp, &g_t);
    n = job_responses(&g_t, find(&g_t, "CTRL"), resp, 16, &mx, &nearc);
    CHECK(n == 3 && resp[0] == 100 && resp[1] == 950 && resp[2] == 300);
    CHECK(mx == 950 && nearc == 1);
    /* 연습 3 */
    uint64_t cpu, med;
    parse(k_over, &g_t);
    int ctrl = find(&g_t, "CTRL");
    CHECK(rule_overrun(&g_t, ctrl, 3100, 4100, &cpu, &med) == 1 && cpu == 1000 && med == 100);
    CHECK(rule_overrun(&g_t, ctrl, 1100, 2100, &cpu, &med) == 0);          /* 평범한 작업은 안 걸린다 */
    CHECK(run_in(&g_t, find(&g_t, "LOG"), 3100, 4100) == 0);                /* 끼어든 것 없음 → 원인은 자기 자신 */
    /* 연습 4 */
    uint64_t us;
    parse(k_hog, &g_t);
    int x = find(&g_t, "CTRL"), h = rule_event_hog(&g_t, x, 2000, 3000, &us);
    CHECK(h == find(&g_t, "PARSE") && us == 630);
    CHECK(run_in(&g_t, find(&g_t, "UART_RX"), 2000, 3000) == 20);           /* ISR 비율 2 % — ISR 규칙으론 안 잡힌다 */
    parse(k_over, &g_t);
    CHECK(rule_event_hog(&g_t, find(&g_t, "CTRL"), 3100, 4100, &us) == -1);  /* 이벤트 구동 태스크 없음 */
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
