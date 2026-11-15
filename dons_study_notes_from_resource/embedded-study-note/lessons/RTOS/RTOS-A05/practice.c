/* RTOS-A05 추가 연습 — practice.c (본문 solution.c 의 시뮬레이터를 그대로 복사하고 연습용 스위치를 더했다)
 *
 *  연습 1: SS 보충 규칙의 함정 — chunk 도중 보충이 와도 chunk 를 안 닫으면 (시각을 처음 활성화로 잡으면) 놓친다
 *  연습 2: 보충 대기열 크기 제한 (POSIX sched_ss_max_repl 류) — 꽉 차면 "더 늦게" 합치기
 *  연습 3: 서버 없이 바로 처리 (최고 우선순위 태스크 / ISR 에서 다 처리) — 빠르지만 주기 태스크가 놓친다
 *  연습 4: EDF 쪽 서버 — Total Bandwidth Server 데드라인 계산
 *  연습 5: 서버 설계 — Ts 를 바꿔 가며 최대 Cs 와 평균 응답 (설계 공간 탐색)
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice [-v]
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXP   6        /* 주기 태스크 최대 수 */
#define MAXAQ  4096     /* 한 실행의 비주기 요청 최대 수 */
#define MAXJ   8        /* 태스크당 밀린 job 최대 수 */
#define MAXREPL 64      /* SS 보충 대기열 */
#define MAXTL  80       /* 타임라인으로 기록하는 tick 수 */
#define MAXLOG 32       /* SS 보충 기록 (테스트용) */

typedef struct { const char *name; uint32_t C, T, O; } ptask_t;
typedef struct { uint32_t arr, C; } areq_t;
typedef enum { SRV_BG, SRV_PS, SRV_DS, SRV_SS, SRV_DI } srv_kind_t;   /* DI = 연습 3: 바로 처리 */
typedef struct { srv_kind_t kind; uint32_t Cs, Ts; } srv_cfg_t;
typedef struct { uint32_t due, amt; } repl_t;
typedef struct { uint32_t start, due, amt; } repl_log_t;

typedef struct {
    /* 설정 */
    const ptask_t *ts; int n; srv_cfg_t sc; const areq_t *aq; int na;
    int rank[MAXP + 1];                  /* 0 = 가장 높음. 인덱스 n = 서버 */
    uint32_t now;
    /* 주기 태스크 상태: 밀린 job 들의 릴리즈 시각 링 + 맨 앞 job 의 남은 양 */
    uint32_t jrel[MAXP][MAXJ]; int jh[MAXP], jn[MAXP]; uint32_t jrem[MAXP];
    /* 비주기 큐: [qhead, narr) 가 도착했지만 안 끝난 요청, arem = 맨 앞 요청의 남은 양 */
    int qhead, narr; uint32_t arem;
    /* 서버 상태 */
    uint32_t budget;
    int in_chunk; uint32_t chunk_start, chunk_used;      /* SS */
    repl_t repl[MAXREPL]; int rh, rn;                      /* SS 보충 대기열 (due 오름차순) */
    repl_log_t rlog[MAXLOG]; int nrlog;
    /* 통계 */
    uint32_t pmiss[MAXP], pmaxresp[MAXP], misses, jovf;
    uint32_t a_done, a_maxresp, srv_ticks, inv_bad;
    uint64_t a_sum;
    int last;                                              /* 직전 tick 에 돈 것 (-1 idle, n = 서버) */
    int naive;                                             /* 연습 1: 도중 보충이 와도 chunk 를 안 닫는다 (틀린 규칙) */
    int max_repl;                                          /* 연습 2: 대기 보충 최대 개수 (0 = 무제한) */
    char row[MAXP + 1][MAXTL + 1], brow[MAXTL + 1];
} sim_t;

static int q_nonempty(const sim_t *s) { return s->qhead < s->narr; }
static int srv_ready(const sim_t *s) {
    if (s->sc.kind == SRV_DI) return q_nonempty(s);
    return s->sc.kind != SRV_BG && s->budget > 0 && q_nonempty(s);
}

/* RM 순위: (주기, 서버 먼저, 인덱스) 사전식. 주어진 코드. */
static void assign_rm(sim_t *s) {
    int n = s->n;
    for (int i = 0; i <= n; i++) {
        uint32_t Ti = (i == n) ? s->sc.Ts : s->ts[i].T;
        int r = 0;
        for (int j = 0; j <= n; j++) {
            if (j == i) continue;
            uint32_t Tj = (j == n) ? s->sc.Ts : s->ts[j].T;
            int jfirst = Tj < Ti || (Tj == Ti && (j == n || (i != n && j < i)));
            if (s->sc.kind == SRV_BG && j == n) jfirst = 0;    /* BG: 서버는 맨 아래 */
            if (s->sc.kind == SRV_BG && i == n) jfirst = 1;
            if (s->sc.kind == SRV_DI && j == n) jfirst = 1;    /* DI: 서버는 맨 위 */
            if (s->sc.kind == SRV_DI && i == n) jfirst = 0;
            r += jfirst;
        }
        s->rank[i] = r;
    }
}

/* ---------------- 1: 고정 우선순위 선택 ---------------- */
/* 반환: -1 = idle, 0..n-1 = 주기 태스크, n = 서버(또는 BG 처리).
 * 주기 태스크는 밀린 job 이 있으면 준비. 서버는 srv_ready() 일 때만 rank[n] 으로 경쟁.
 * BG 는 주기 태스크가 하나도 준비되지 않았고 큐에 일이 있을 때만. */
static int sched_pick(const sim_t *s) {
    int best = -1;
    for (int i = 0; i < s->n; i++)
        if (s->jn[i] > 0 && (best < 0 || s->rank[i] < s->rank[best])) best = i;
    if (s->sc.kind == SRV_DI) {
        if (q_nonempty(s)) best = s->n;                  /* 연습 3: 예산 없이 맨 위 */
    } else if (s->sc.kind == SRV_BG) {
        if (best < 0 && q_nonempty(s)) best = s->n;
    } else if (srv_ready(s) && (best < 0 || s->rank[s->n] < s->rank[best])) {
        best = s->n;
    }
    return best;
}

/* ---------------- 2~4: 서버 규칙 ---------------- */
static void ss_close_chunk(sim_t *s) {
    /* SS: 열린 chunk 를 닫는다. 쓴 양이 있으면 (chunk_start + Ts, chunk_used) 보충을 예약. */
    if (!s->in_chunk) return;
    if (s->chunk_used > 0 && s->max_repl > 0 && s->rn >= s->max_repl) {
        /* 연습 2: 꽉 찼으면 마지막 보충에 합치고 시각은 더 늦은 새 시각으로 (늦추는 쪽 = 안전) */
        repl_t *last = &s->repl[(s->rh + s->rn - 1) % MAXREPL];
        last->amt += s->chunk_used;
        last->due = s->chunk_start + s->sc.Ts;
    } else if (s->chunk_used > 0 && s->rn < MAXREPL) {
        int k = (s->rh + s->rn) % MAXREPL;
        s->repl[k].due = s->chunk_start + s->sc.Ts;
        s->repl[k].amt = s->chunk_used;
        s->rn++;
        if (s->nrlog < MAXLOG) {
            s->rlog[s->nrlog].start = s->chunk_start; s->rlog[s->nrlog].due = s->chunk_start + s->sc.Ts;
            s->rlog[s->nrlog].amt = s->chunk_used; s->nrlog++;
        }
    }
    s->in_chunk = 0; s->chunk_used = 0;
}

/* (4) tick 시작: 활성화 / 보충 / chunk 열기 */
static void srv_tick_begin(sim_t *s) {
    uint32_t t = s->now;
    switch (s->sc.kind) {
    case SRV_PS:   /* 2a: k*Ts 에 큐가 비어 있지 않으면 Cs, 비어 있으면 0 (그 주기 예산을 잃는다) */
        if (t % s->sc.Ts == 0) s->budget = q_nonempty(s) ? s->sc.Cs : 0;
        break;
    case SRV_DS:   /* 4: k*Ts 에 Cs 로 채운다 (쌓이지 않음) */
        if (t % s->sc.Ts == 0) s->budget = s->sc.Cs;
        break;
    case SRV_SS: { /* 3a: due == t 인 보충을 더하고, chunk 도중 보충이 오면 닫았다가 다시 연다 */
        int got = 0;
        while (s->rn > 0 && s->repl[s->rh].due == t) {
            s->budget += s->repl[s->rh].amt;
            s->rh = (s->rh + 1) % MAXREPL; s->rn--; got = 1;
        }
        if (got && s->in_chunk && !s->naive) ss_close_chunk(s);
        /* 3b: 준비 상태가 되는 순간 = 활성화. 그 시각이 보충 기준 */
        if (!s->in_chunk && s->budget > 0 && q_nonempty(s)) {
            s->in_chunk = 1; s->chunk_start = t; s->chunk_used = 0;
        }
        break;
    }
    case SRV_BG: case SRV_DI: break;
    }
}

/* (6) 서버가 1 tick 실행했다 */
static void srv_consume(sim_t *s) {
    if (s->sc.kind == SRV_BG || s->sc.kind == SRV_DI) return;
    s->budget--;                                  /* 주어진 코드: 예산 1 감소 */
    if (s->sc.kind == SRV_SS) s->chunk_used++;    /* 3c: chunk 에서 쓴 양 */
}

/* (7) tick 끝 */
static void srv_tick_end(sim_t *s) {
    switch (s->sc.kind) {
    case SRV_PS:   /* 2b: 큐가 비면 남은 예산은 버린다 (서버는 다음 주기까지 쉰다) */
        if (!q_nonempty(s)) s->budget = 0;
        break;
    case SRV_SS:   /* 3d: 예산 소진 또는 큐 비움 → chunk 닫기 */
        if (s->in_chunk && (s->budget == 0 || !q_nonempty(s))) ss_close_chunk(s);
        break;
    default: break;
    }
}

/* ---------------- 시뮬레이터 본체 (주어진 코드) ---------------- */
static void sim_init(sim_t *s, const ptask_t *ts, int n, srv_cfg_t sc, const areq_t *aq, int na) {
    memset(s, 0, sizeof *s);
    s->ts = ts; s->n = n; s->sc = sc; s->aq = aq; s->na = na; s->last = -1;
    if (sc.kind == SRV_DS || sc.kind == SRV_SS) s->budget = sc.Cs;   /* 시작할 때 가득 */
    assign_rm(s);
    for (int i = 0; i <= n; i++) { memset(s->row[i], ' ', MAXTL); s->row[i][MAXTL] = 0; }
    memset(s->brow, ' ', MAXTL); s->brow[MAXTL] = 0;
}

static void sim_step(sim_t *s) {
    uint32_t t = s->now; int n = s->n;
    /* (1) 데드라인 검사: 데드라인 == t 인데 남아 있는 job (job 은 계속 실행한다) */
    for (int i = 0; i < n; i++)
        for (int k = 0; k < s->jn[i]; k++)
            if (s->jrel[i][(s->jh[i] + k) % MAXJ] + s->ts[i].T == t) { s->pmiss[i]++; s->misses++; }
    /* (2) 주기 릴리즈 */
    for (int i = 0; i < n; i++) {
        const ptask_t *p = &s->ts[i];
        if (t >= p->O && (t - p->O) % p->T == 0) {
            if (s->jn[i] == MAXJ) { s->jovf++; continue; }
            if (s->jn[i] == 0) s->jrem[i] = p->C;
            s->jrel[i][(s->jh[i] + s->jn[i]) % MAXJ] = t; s->jn[i]++;
        }
    }
    /* (3) 비주기 도착 */
    while (s->narr < s->na && s->aq[s->narr].arr == t) {
        if (s->qhead == s->narr) s->arem = s->aq[s->narr].C;
        s->narr++;
    }
    /* (4) 서버 활성화·보충 */
    srv_tick_begin(s);
    uint32_t b_show = s->budget;
    /* (5) 선택 */
    int who = sched_pick(s);
    /* 타임라인 */
    if (t < MAXTL) {
        for (int i = 0; i < n; i++) s->row[i][t] = (who == i) ? 'R' : (s->jn[i] > 0 ? 'r' : '.');
        char c = '.';
        if (who == n) c = 'R';
        else if (q_nonempty(s)) c = (s->sc.kind == SRV_BG || srv_ready(s)) ? (srv_ready(s) ? 'r' : 'w') : 'w';
        s->row[n][t] = c;
        s->brow[t] = s->sc.kind == SRV_BG ? '-' : (char)(b_show < 10 ? '0' + b_show : '+');
    }
    /* (6) 1 tick 실행 */
    if (who >= 0 && who < n) {
        if (--s->jrem[who] == 0) {
            uint32_t rel = s->jrel[who][s->jh[who]];
            uint32_t resp = t + 1 - rel;
            if (resp > s->pmaxresp[who]) s->pmaxresp[who] = resp;
            s->jh[who] = (s->jh[who] + 1) % MAXJ; s->jn[who]--;
            if (s->jn[who] > 0) s->jrem[who] = s->ts[who].C;
        }
    } else if (who == n) {
        s->srv_ticks++;
        srv_consume(s);
        if (--s->arem == 0) {
            uint32_t resp = t + 1 - s->aq[s->qhead].arr;
            s->a_sum += resp; s->a_done++;
            if (resp > s->a_maxresp) s->a_maxresp = resp;
            s->qhead++;
            if (s->qhead < s->narr) s->arem = s->aq[s->qhead].C;
        }
    }
    s->last = who;
    /* (7) */
    srv_tick_end(s);
    /* 불변식 (SS): 예산 + 대기 중 보충 + 열린 chunk 사용량 == Cs */
    if (s->sc.kind == SRV_SS) {
        uint32_t tot = s->budget + (s->in_chunk ? s->chunk_used : 0);
        for (int k = 0; k < s->rn; k++) tot += s->repl[(s->rh + k) % MAXREPL].amt;
        if (tot != s->sc.Cs) s->inv_bad++;
    }
    if (s->budget > s->sc.Cs && s->sc.kind != SRV_BG && s->sc.kind != SRV_DI) s->inv_bad++;
    s->now++;
}

static int g_naive, g_max_repl;    /* 연습 1·2 스위치: sim_run 이 sim_init 직후 복사 */
static void sim_run(sim_t *s, const ptask_t *ts, int n, srv_cfg_t sc, const areq_t *aq, int na, uint32_t H) {
    sim_init(s, ts, n, sc, aq, na);
    s->naive = g_naive; s->max_repl = g_max_repl;
    for (uint32_t k = 0; k < H; k++) sim_step(s);
}

/* ---------------- RTA (RTOS-M11 식 + 지터, 주어진 코드) ---------------- */
typedef struct { uint32_t C, T, J; } ent_t;
/* e[0..k-1] 는 우선순위 내림차순. e[i] 의 최악 응답 R = w + J_i, w = C_i + Σ_{j<i} ⌈(w + J_j)/T_j⌉ C_j.
 * R > T_i (D = T) 면 그 값을 그대로 돌려준다 (= 실패). */
static uint32_t rta_resp(const ent_t *e, int i) {
    uint64_t w = e[i].C, prev = 0;
    while (w != prev) {
        prev = w; w = e[i].C;
        for (int j = 0; j < i; j++) w += (prev + e[j].J + e[j].T - 1) / e[j].T * e[j].C;
        if (w + e[i].J > e[i].T) break;
    }
    return (uint32_t)(w + e[i].J);
}
/* 주기 태스크 + 서버(Cs, Ts, J) 를 RM 순서로 세운 뒤, 주기 태스크 i 의 R 을 out[i] 에. 모두 R ≤ T 면 1. */
static int rta_with_server(const ptask_t *ts, int n, uint32_t Cs, uint32_t Ts, uint32_t Js, uint32_t *out) {
    ent_t e[MAXP + 1]; int id[MAXP + 1], k = 0, ok = 1;
    sim_t tmp; tmp.ts = ts; tmp.n = n; tmp.sc.kind = SRV_PS; tmp.sc.Ts = Ts; tmp.sc.Cs = Cs;
    assign_rm(&tmp);
    for (int r = 0; r <= n; r++)
        for (int i = 0; i <= n; i++)
            if (tmp.rank[i] == r) {
                if (i == n) { if (Cs == 0) continue; e[k].C = Cs; e[k].T = Ts; e[k].J = Js; }
                else { e[k].C = ts[i].C; e[k].T = ts[i].T; e[k].J = 0; }
                id[k++] = i;
            }
    for (int x = 0; x < k; x++) {
        if (id[x] == n) continue;
        uint32_t R = rta_resp(e, x);
        if (out) out[id[x]] = R;
        if (R > e[x].T) ok = 0;
    }
    return ok;
}

/* ---------------- 5: RM 을 깨지 않는 최대 서버 예산 ---------------- */
/* Cs = Ts, Ts-1, ..., 1 중 주기 태스크가 모두 RTA 를 통과하는 가장 큰 값. 없으면 0.
 * ds_jitter = 1 이면 서버를 지터 J = Ts - Cs 인 주기 태스크로 본다 (DS 의 back-to-back 실행). */
static uint32_t server_max_budget(const ptask_t *ts, int n, uint32_t Ts, int ds_jitter) {
    for (uint32_t Cs = Ts; Cs >= 1; Cs--)
        if (rta_with_server(ts, n, Cs, Ts, ds_jitter ? Ts - Cs : 0, NULL)) return Cs;
    return 0;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng;
static uint32_t xorshift32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static uint32_t rnd(uint32_t lo, uint32_t hi) { return lo + xorshift32() % (hi - lo + 1); }
static int gen_aperiodic(areq_t *aq, uint32_t seed, uint32_t gap, uint32_t cmin, uint32_t cmax, uint32_t H) {
    g_rng = seed ? seed : 1u; int k = 0; uint32_t t = rnd(0, gap);
    while (t < H && k < MAXAQ) { aq[k].arr = t; aq[k].C = rnd(cmin, cmax); k++; t += rnd(1, 2 * gap - 1); }
    return k;
}
static const char *KN[] = { "BG", "PS", "DS", "SS", "DI" };
static void print_sim(const sim_t *s, const char *title, uint32_t upto) {
    printf("-- %s  (Cs=%u Ts=%u)\n", title, s->sc.Cs, s->sc.Ts);
    printf("tick   : "); for (uint32_t t = 0; t < upto; t++) putchar('0' + (char)(t % 10)); putchar('\n');
    for (int i = 0; i < s->n; i++) printf("%-6s : %.*s\n", s->ts[i].name, (int)upto, s->row[i]);
    printf("%-6s : %.*s\n", KN[s->sc.kind], (int)upto, s->row[s->n]);
    printf("budget : %.*s\n", (int)upto, s->brow);
}

/* ---- 연습 4: Total Bandwidth Server (Spuri & Buttazzo) ----
 * 서버 대역폭 Us = num/den. k 번째 요청 (도착 r, 실행 C) 의 EDF 데드라인
 *   d_k = max(r_k, d_{k-1}) + ceil(C_k / Us) = max(r_k, d_{k-1}) + ceil(C_k * den / num)
 * 주기 태스크 U_p 와 함께 U_p + Us <= 1 이면 EDF 에서 모두 지킨다. */
static uint32_t tbs_deadline(uint32_t prev_d, uint32_t r, uint32_t C, uint32_t num, uint32_t den) {
    uint32_t base = r > prev_d ? r : prev_d;
    return base + (uint32_t)(((uint64_t)C * den + num - 1) / num);
}

typedef struct { uint64_t sum; uint32_t cnt, mx, miss, over, bad; } agg_t;
static agg_t run_batch(const ptask_t *ts, int n, srv_cfg_t c, uint32_t gap, int seeds, const uint32_t *bound) {
    static sim_t s; static areq_t aq[MAXAQ]; agg_t a; memset(&a, 0, sizeof a);
    for (int sd = 1; sd <= seeds; sd++) {
        int na = gen_aperiodic(aq, 0x2026A05u + (uint32_t)sd * 7919u, gap, 1, 4, 19600);
        sim_run(&s, ts, n, c, aq, na, 20000);
        a.sum += s.a_sum; a.cnt += s.a_done; a.miss += s.misses; a.bad += s.inv_bad;
        if (s.a_maxresp > a.mx) a.mx = s.a_maxresp;
        if (bound) for (int i = 0; i < n; i++) if (s.pmaxresp[i] > bound[i]) a.over++;
    }
    return a;
}
static double avg_of(agg_t a) { return a.cnt ? (double)a.sum / a.cnt : 0.0; }

int main(int argc, char **argv) {
    int verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    static sim_t s;
    static areq_t aq[MAXAQ];
    const ptask_t FW[] = { { "speed", 2, 10, 0 }, { "imu", 4, 20, 0 }, { "log", 30, 100, 9 } };
    uint32_t Rb[MAXP];
    CHECK(rta_with_server(FW, 3, 3, 10, 0, Rb) == 1);

    /* ===== 연습 1: 보충 시각의 함정 ===== */
    {
        const ptask_t TR[] = { { "hi", 2, 7, 5 }, { "lo", 2, 20, 6 } };
        const areq_t TQ[] = { { 2, 4 }, { 5, 10 }, { 25, 4 }, { 27, 13 } };
        srv_cfg_t c = { SRV_SS, 6, 10 };
        uint32_t R[MAXP];
        CHECK(rta_with_server(TR, 2, 6, 10, 0, R) == 1 && R[1] == 20);
        g_naive = 1; sim_run(&s, TR, 2, c, TQ, 4, 80);
        if (verbose) print_sim(&s, "ex1 naive replenishment", 60);
        printf("ex1 naive: lo misses=%u maxresp=%u (RTA %u) inv_bad=%u\n", s.pmiss[1], s.pmaxresp[1], R[1], s.inv_bad);
        CHECK(s.pmiss[1] == 1 && s.pmaxresp[1] > R[1]);
        CHECK(s.inv_bad == 0);                       /* 예산 보존 불변식은 멀쩡하다 — 시각만 틀렸다 */
        g_naive = 0; sim_run(&s, TR, 2, c, TQ, 4, 80);
        if (verbose) print_sim(&s, "ex1 correct replenishment", 60);
        printf("ex1 fixed: lo misses=%u maxresp=%u\n", s.pmiss[1], s.pmaxresp[1]);
        CHECK(s.misses == 0 && s.pmaxresp[1] <= R[1]);

        /* 무작위 소형 시스템: 서버를 RM 가능한 최대 Cs 로 두고, 관측 응답 > RTA 인 시스템 수를 센다 */
        uint32_t seed = 12345, sys = 0, bad_naive = 0, bad_fixed = 0, miss_naive = 0, miss_fixed = 0;
        for (int trial = 0; trial < 30000; trial++) {
            g_rng = seed; seed = xorshift32();
            int n = 2 + (int)rnd(0, 1);
            ptask_t ts[3]; uint32_t Ts = rnd(4, 12);
            for (int i = 0; i < n; i++) { ts[i].name = "t"; ts[i].T = rnd(3, 30); ts[i].C = rnd(1, ts[i].T / 2); ts[i].O = rnd(0, ts[i].T - 1); }
            uint32_t cs = server_max_budget(ts, n, Ts, 0); if (!cs) continue;
            uint32_t Rx[MAXP]; rta_with_server(ts, n, cs, Ts, 0, Rx);
            int na = 0; uint32_t t = rnd(0, 5);
            while (t < 300 && na < 200) { aq[na].arr = t; aq[na].C = rnd(1, 2 * cs); na++; t += rnd(1, Ts * 2); }
            srv_cfg_t cc = { SRV_SS, cs, Ts }; sys++;
            for (int mode = 0; mode < 2; mode++) {
                g_naive = mode; sim_run(&s, ts, n, cc, aq, na, 400);
                int over = 0;
                for (int i = 0; i < n; i++) over |= s.pmaxresp[i] > Rx[i];
                if (mode) { bad_naive += (uint32_t)over; miss_naive += s.misses ? 1u : 0u; }
                else      { bad_fixed += (uint32_t)over; miss_fixed += s.misses ? 1u : 0u; }
            }
        }
        g_naive = 0;
        printf("ex1 random: %u systems  naive: over-RTA %u, with misses %u | fixed: over-RTA %u, with misses %u\n",
               sys, bad_naive, miss_naive, bad_fixed, miss_fixed);
        CHECK(sys > 5000);
        CHECK(bad_naive > 0 && bad_fixed == 0 && miss_fixed == 0);
    }

    /* ===== 연습 2: 보충 대기열 크기 제한 ===== */
    {
        srv_cfg_t c = { SRV_SS, 3, 10 };
        const int K[4] = { 1, 2, 4, 0 };
        agg_t a[4];
        printf("ex2 burst workload, SS(3,10), max_repl K:\n");
        for (int k = 0; k < 4; k++) {
            g_max_repl = K[k];
            a[k] = run_batch(FW, 3, c, 10, 20, Rb);
            printf("    K=%-3s avg=%6.2f max=%4u done=%u misses=%u over=%u inv_bad=%u\n",
                   K[k] ? (K[k] == 1 ? "1" : K[k] == 2 ? "2" : "4") : "inf", avg_of(a[k]), a[k].mx, a[k].cnt,
                   a[k].miss, a[k].over, a[k].bad);
            CHECK(a[k].miss == 0 && a[k].over == 0 && a[k].bad == 0);
        }
        g_max_repl = 0;
        CHECK(a[0].sum * a[3].cnt >= a[3].sum * a[0].cnt);    /* K=1 은 무제한보다 느리거나 같다 */
        /* 작은 예: K=1 이면 두 번째 보충이 첫 번째에 합쳐져 시각이 뒤로 */
        const ptask_t A[] = { { "hi", 1, 5, 0 }, { "lo", 2, 10, 0 } };
        const areq_t AQ[] = { { 1, 1 }, { 2, 1 } };
        srv_cfg_t c2 = { SRV_SS, 2, 4 };
        sim_run(&s, A, 2, c2, AQ, 2, 20);
        CHECK(strncmp(s.brow, "2210012", 7) == 0);   /* 무제한: t=5 에 1, t=6 에 1 */
        g_max_repl = 1; sim_run(&s, A, 2, c2, AQ, 2, 20); g_max_repl = 0;
        CHECK(s.rn == 0 && s.inv_bad == 0);
        CHECK(s.nrlog == 1 && s.rlog[0].due == 5);   /* 두 번째는 새 항목이 아니라 합치기 */
        CHECK(strncmp(s.brow, "2210002", 7) == 0);   /* K=1: t=5 에는 아무것도, t=6 에 2 가 한꺼번에 */
    }

    /* ===== 연습 3: 서버 없이 바로 처리 ===== */
    {
        srv_cfg_t di = { SRV_DI, 0, 10 }, ss = { SRV_SS, 3, 10 };
        agg_t dn = run_batch(FW, 3, di, 20, 20, NULL), db = run_batch(FW, 3, di, 10, 20, NULL);
        agg_t sn = run_batch(FW, 3, ss, 20, 20, NULL), sb = run_batch(FW, 3, ss, 10, 20, NULL);
        printf("ex3 direct: normal avg=%.2f misses=%u | burst avg=%.2f misses=%u\n", avg_of(dn), dn.miss, avg_of(db), db.miss);
        printf("ex3 SS    : normal avg=%.2f misses=%u | burst avg=%.2f misses=%u\n", avg_of(sn), sn.miss, avg_of(sb), sb.miss);
        CHECK(dn.sum * sn.cnt < sn.sum * dn.cnt);      /* 응답은 DI 가 가장 빠르다 */
        CHECK(db.miss > sb.miss && sb.miss == 0);      /* 대신 폭주 때 주기 태스크가 놓친다 */
        CHECK(db.miss > 0);
    }

    /* ===== 연습 4: TBS 데드라인 ===== */
    {
        /* Us = 1/4 */
        uint32_t d1 = tbs_deadline(0, 0, 1, 1, 4);
        uint32_t d2 = tbs_deadline(d1, 2, 2, 1, 4);
        uint32_t d3 = tbs_deadline(d2, 20, 1, 1, 4);
        uint32_t d4 = tbs_deadline(d3, 21, 3, 3, 10);   /* Us = 0.3: ceil(3 / 0.3) = 10 */
        printf("ex4 TBS deadlines: %u %u %u %u\n", d1, d2, d3, d4);
        CHECK(d1 == 4 && d2 == 12 && d3 == 24 && d4 == 34);
        CHECK(tbs_deadline(0, 0, 1, 3, 10) == 4);       /* ceil(1 / 0.3) = ceil(3.33) = 4 */
    }

    /* ===== 연습 5: 서버 설계 공간 ===== */
    {
        uint32_t bestTs = 0; double bestAvg = 1e18;
        printf("ex5 design sweep (normal workload, 10 seeds):  Ts  Cs  Cs/Ts  SS_avg  SS_max  PS_avg\n");
        for (uint32_t Ts = 5; Ts <= 25; Ts++) {
            uint32_t cs = server_max_budget(FW, 3, Ts, 0);
            if (!cs) { printf("    %2u  --\n", Ts); continue; }
            uint32_t Rx[MAXP]; rta_with_server(FW, 3, cs, Ts, 0, Rx);
            srv_cfg_t c = { SRV_SS, cs, Ts }, p = { SRV_PS, cs, Ts };
            agg_t a = run_batch(FW, 3, c, 20, 10, Rx), b = run_batch(FW, 3, p, 20, 10, Rx);
            printf("    %2u  %2u  %.3f  %6.2f  %6u  %6.2f\n", Ts, cs, (double)cs / Ts, avg_of(a), a.mx, avg_of(b));
            CHECK(a.miss == 0 && a.over == 0 && b.miss == 0 && b.over == 0);
            if (avg_of(a) < bestAvg) { bestAvg = avg_of(a); bestTs = Ts; }
        }
        printf("ex5 best Ts for SS avg = %u (avg %.2f)\n", bestTs, bestAvg);
        CHECK(bestTs != 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
