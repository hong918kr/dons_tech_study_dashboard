/* RTOS-A10 추가 연습 — 한 번에 검증
 *
 * 본문(solution.c)의 분석 코드(TODO 1~4 정답)를 그대로 복사해 두고, 설계를 바꿔 보는 연습 5개를 푼다.
 *   연습 1: 전류 루프를 ISR 대신 최상위 태스크로 돌리면 — 허용 가능한 문맥 전환 비용과 늘어나는 지터
 *   연습 2: 윈도우 워치독(WWDG) — 모니터 우선순위가 킥 간격의 흔들림을 정한다
 *   연습 3: CAN 비트레이트별 큐 깊이
 *   연습 4: 로그 링 크기 — 지우기 시간 typ / max, 지우기 일시정지(erase suspend), 지속 가능한 로그 비율
 *   연습 5: 민감도 — 속도 루프 C 를 얼마까지 늘릴 수 있나 (RTA 기준 / 큐 깊이 3 유지 기준)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice [-v]
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total, g_verbose;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ===================== 설계 상수 (µs) ===================== */
#define T_CUR        100u   /* 10 kHz 전류 루프 */
#define C_CUR         25u   /* FOC 1회 (Clarke/Park, PI 2개, SVPWM) — 추정 WCET */
#define D_CUR         60u   /* PWM 섀도 레지스터 로드 전에 듀티를 써야 한다 */
#define A_CAN        111u   /* 1 Mbit/s 표준 ID DLC 8 최단 프레임: 44 + 64 + 3(IFS) = 111 비트 (COMM-M04) */
#define C_CANISR       4u
#define HWFIFO         3u   /* bxCAN RX FIFO 깊이 */
#define D_CANISR     (HWFIFO * A_CAN)   /* 하드웨어 FIFO 가 넘치기 전에 하나는 꺼내야 한다 */
#define C_BRIDGE       2u
#define T_TICK      1000u   /* configTICK_RATE_HZ = 1000 */
#define C_TICK         3u
#define T_SPEED     1000u
#define C_SPEED      150u   /* 속도 PI + 관측기 + 로그 기록 (임계 구역 2 µs 포함) */
#define C_MON         10u
#define MON_TICKS     20u   /* 모니터 주기 20 ms */
#define WDG_US    100000u   /* IWDG 타임아웃 100 ms */
#define C_FRAME       30u   /* CAN 명령 1개 처리 */
#define CAN_TMO_TICKS 10u   /* 명령이 없어도 10 ms 마다 깨어 체크인 */
#define N_BURST       16u   /* 요구사항: 호스트는 최대 16 프레임을 연달아 보낸다 */
#define T_BURST    50000u   /* 버스트 사이 최소 50 ms */
#define LOG_DECIM     10u   /* 속도 루프 10번에 1번 로그 = 100 Hz */
#define REC_BYTES     16u
#define REC_PER_PAGE  16u   /* 256 B 페이지 */
#define PAGES_PER_SECTOR 16u /* 4 KB 섹터 */
#define CHUNK          4u   /* flush 가 임계 구역 한 번에 꺼내는 레코드 수 */
#define C_CHUNK        4u
#define C_LOGPUSH      2u
#define ERASE_MAX 400000u   /* SPI NOR 4 KB 섹터 지우기 최대 (데이터시트 대표값, 부품별 확인 필요) */
#define PROG_MAX    3000u   /* 페이지 쓰기 최대 */
#define NVIC_SYSCALL   5    /* configMAX_SYSCALL_INTERRUPT_PRIORITY = 5 << (8 - 4) */
#define CAN_QDEPTH     3u   /* can_backlog() 결과 (Part A) */
#define LOG_RING     224u   /* log_ring_needed() 218 → 32 의 배수로 올림 */
#define QMAX          64u
#define RING_MAX    1024u

/* ===================== Part A: 분석 ===================== */
#define MAXE 12
typedef struct {
    const char *name;
    int isr;              /* 1 = ISR, 0 = 태스크 */
    int nvic;             /* ISR: NVIC 우선순위 (작을수록 급함) */
    int prio;             /* 태스크: FreeRTOS 우선순위 (클수록 급함) */
    uint32_t C, T, D, J;  /* D = 0 → 데드라인 없음 (배경 / 별도 분석) */
    uint32_t crit;        /* 이 개체 안의 가장 긴 커널 임계 구역 (BASEPRI 올림) */
} ent_t;
typedef struct { ent_t e[MAXE]; int n; } sys_t;
typedef struct { int ok; uint32_t R, B, iters; } rta_t;

/* 하나의 축으로 줄 세우기: ISR 은 모든 태스크보다 급하다 */
static int level(const ent_t *e) { return e->isr ? 1000 - e->nvic : e->prio; }
static int maskable(const ent_t *e) { return e->isr && e->nvic >= NVIC_SYSCALL; }
static uint32_t ceil_div(uint64_t a, uint32_t b) { return (uint32_t)((a + b - 1) / b); }

/* (본문 TODO 1 정답) B_i — i 가 준비됐는데 덜 급한 누군가의 커널 임계 구역 때문에 못 도는 최대 시간.
 *   - configMAX_SYSCALL 위의 ISR (BASEPRI 에 안 가려짐) → 0
 *   - BASEPRI 에 가려지는 ISR → 모든 태스크 + 덜 급한(nvic 큰) ISR 의 crit 중 최댓값
 *     (FromISR API 의 임계 구역도 BASEPRI 를 configMAX_SYSCALL 까지 올린다)
 *   - 태스크 → 우선순위가 더 낮은 태스크의 crit 중 최댓값 (임계 구역 안에서는 PendSV 도 가려진다)
 *   하나만 기다린다(최댓값, 합 아님): 단일 코어에서 임계 구역은 중첩돼도 한 번에 하나만 열려 있다. */
static uint32_t blocking_term(const sys_t *s, int i) {
    const ent_t *ei = &s->e[i];
    if (ei->isr && !maskable(ei)) return 0;
    uint32_t b = 0;
    for (int j = 0; j < s->n; j++) {
        if (j == i) continue;
        const ent_t *ej = &s->e[j];
        int can_block = ei->isr ? (!ej->isr || ej->nvic > ei->nvic)
                                : (!ej->isr && ej->prio < ei->prio);
        if (can_block && ej->crit > b) b = ej->crit;
    }
    return b;
}

/* (본문 TODO 2 정답) 확장 RTA (RTOS-A04 축약). hp(i) = level 이 i 이상인 다른 개체 (같은 level 도 보수적으로 포함).
 *   w0 = C + B, w_{k+1} = C + B + Σ ceil((w_k + J_j)/T_j)·C_j, 수렴하면 R = w + J_i.
 *   w + J_i > D 가 되는 순간 멈추고 ok = 0. D = 0 이면 한도 10 s. iters = 반복 횟수. */
static rta_t rta(const sys_t *s, int i) {
    rta_t r = {0, 0, 0, 0};
    const ent_t *ei = &s->e[i];
    uint64_t lim = ei->D ? ei->D : 10000000u;
    r.B = blocking_term(s, i);
    uint64_t w = (uint64_t)ei->C + r.B;
    for (;;) {
        r.iters++;
        uint64_t nw = (uint64_t)ei->C + r.B;
        for (int j = 0; j < s->n; j++) {
            if (j == i || level(&s->e[j]) < level(ei)) continue;
            nw += (uint64_t)ceil_div(w + s->e[j].J, s->e[j].T) * s->e[j].C;
        }
        if (nw + ei->J > lim) { r.R = (uint32_t)(nw + ei->J); return r; }
        if (nw == w) { r.ok = 1; r.R = (uint32_t)(w + ei->J); return r; }
        w = nw;
    }
}

/* (본문 TODO 3 정답) CAN 큐 깊이와 버스트 지연 (네트워크 캘큘러스식 상한).
 *   도착: 프레임 k (0 ≤ k < n) 가 k·a 에 도착 → A(t) = min(n, t/a + 1).
 *   서비스 하한 β(Δ) = served_lb(): 소비자 i 가 계속 일감이 있을 때 길이 Δ 창에서 끝내는 프레임 수
 *     floor(max(0, Δ − B_i − I(Δ)) / c),  I(Δ) = Σ_{hp(i)} ceil((Δ + J_j)/T_j)·C_j
 *   처리 완료 수 하한 D(t) = min( β(t), min_{k=1..n} [ k + β(t − s_k) ], n )
 *     s_k = A 가 k 로 머무는 마지막 시각 = min(t, k·a − 1)  (k = n 이면 t). (k−1)·a > t 인 k 는 아직 없다.
 *   depth = max_t (A(t) − D(t)),  latency = D(t) ≥ n 이 되는 가장 작은 t. limit 안에 없으면 ok = 0. */
typedef struct { int ok; uint32_t depth, latency; } backlog_t;
/* 주어진 도우미: β(Δ) */
static uint32_t served_lb(const sys_t *s, int i, uint32_t w, uint32_t B, uint32_t c) {
    uint64_t I = 0;
    for (int j = 0; j < s->n; j++) {
        if (j == i || level(&s->e[j]) < level(&s->e[i])) continue;
        I += (uint64_t)ceil_div((uint64_t)w + s->e[j].J, s->e[j].T) * s->e[j].C;
    }
    if ((uint64_t)w <= I + B) return 0;
    return (uint32_t)(((uint64_t)w - I - B) / c);
}
static backlog_t can_backlog(const sys_t *s, int i, uint32_t n, uint32_t a, uint32_t c, uint32_t limit) {
    backlog_t r = {0, 0, 0};
    uint32_t B = blocking_term(s, i);
    for (uint32_t t = 0; t <= limit; t++) {
        uint32_t A = t / a + 1 < n ? t / a + 1 : n;
        uint32_t D = served_lb(s, i, t, B, c);
        for (uint32_t k = 1; k <= n && (k - 1) * a <= t; k++) {
            uint32_t sk = (k < n && k * a - 1 < t) ? k * a - 1 : t;
            uint32_t term = k + served_lb(s, i, t - sk, B, c);
            if (term < D) D = term;
        }
        if (D > n) D = n;
        if (A > D && A - D > r.depth) r.depth = A - D;
        if (D >= n) { r.ok = 1; r.latency = t; break; }
    }
    return r;
}

/* (본문 TODO 4 정답) 로그 링 크기 (레코드 수).
 *   생산자 k 는 최소 간격 T_k 마다 burst_k 개. 길이 w 창에 최대 (ceil(w/T_k) + 1)·burst_k 개 (창 양 끝 걸침).
 *   flush 는 링에 페이지 1장(rec_per_page)이 차야 시작하고, 그 뒤 stall_us 동안 아무것도 못 꺼낸다:
 *     needed = rec_per_page + Σ_k arrivals_k(stall_us)
 *   지속 가능성: 섹터 한 바퀴(cycle_us = 지우기 + 페이지들 쓰기) 동안 도착이 섹터 용량(sector_recs) 이하여야
 *   한다. 아니면 링을 아무리 키워도 결국 넘친다 → 0 을 돌려준다. */
typedef struct { uint32_t T, burst; } producer_t;
/* 주어진 도우미: 길이 w 창의 최대 도착 수 */
static uint32_t arrivals(const producer_t *p, int np, uint32_t w) {
    uint32_t a = 0;
    for (int k = 0; k < np; k++) a += (ceil_div(w, p[k].T) + 1) * p[k].burst;
    return a;
}
static uint32_t log_ring_needed(const producer_t *p, int np, uint32_t stall_us, uint32_t rec_per_page,
                                uint32_t cycle_us, uint32_t sector_recs) {
    if (arrivals(p, np, cycle_us) > sector_recs) return 0;
    return rec_per_page + arrivals(p, np, stall_us);
}

/* ---------------- 주어진 코드: 설계 표 ---------------- */
enum { E_CUR, E_CAN, E_BRIDGE, E_TICK, E_WDG, E_SPEED, E_CANCMD, E_LOG, NE };
static void build_design(sys_t *s) {
    static const ent_t d[NE] = {
        /* name        isr nvic prio  C        T        D          J  crit */
        {"cur_isr",     1,  1,  -1, C_CUR,    T_CUR,   D_CUR,      0, 0},
        {"can_isr",     1,  5,  -1, C_CANISR, A_CAN,   D_CANISR,   0, 1},
        {"bridge_swi",  1,  6,  -1, C_BRIDGE, T_SPEED, T_SPEED,    0, 1},
        {"systick",     1, 15,  -1, C_TICK,   T_TICK,  T_TICK,     0, 2},
        {"wdg_mon",     0, -1,   5, C_MON,    MON_TICKS * T_TICK, MON_TICKS * T_TICK, 0, 2},
        {"speed",       0, -1,   4, C_SPEED,  T_SPEED, T_SPEED,    0, C_LOGPUSH},
        {"can_cmd",     0, -1,   3, C_FRAME,  A_CAN,   0,          0, C_LOGPUSH},
        {"log_flush",   0, -1,   1, C_CHUNK,  1000,    0,          0, C_CHUNK},
    };
    memset(s, 0, sizeof *s);
    for (int i = 0; i < NE; i++) s->e[i] = d[i];
    s->n = NE;
}
/* 지터 전파: 브리지는 전류 ISR 이 끝나야 펜딩된다 → J_bridge = R_cur.
 * 속도 태스크는 브리지가 알림을 줘야 READY → J_speed = R_bridge (명목 출시 = 1 ms 경계). */
static void propagate_jitter(sys_t *s) {
    rta_t rc = rta(s, E_CUR);
    s->e[E_BRIDGE].J = rc.R;
    rta_t rb = rta(s, E_BRIDGE);
    s->e[E_SPEED].J = rb.R;
}


/* ===================== 연습 1: 전류 루프를 태스크로 ===================== */
/* ADC 완료 ISR(NVIC 5, 2 µs)이 최상위 태스크(prio 6)에 알림. 태스크는 매 주기 문맥 전환 2번(들어가고 나오고)을
 * 더 낸다 → C = C_CUR + 2·cs. 브리지 SWI 는 필요 없다 (태스크가 10번째마다 속도 태스크에 직접 알림). */
static void build_cur_task(sys_t *s, uint32_t cs) {
    build_design(s);
    s->e[E_CUR].isr = 0; s->e[E_CUR].nvic = -1; s->e[E_CUR].prio = 6;
    s->e[E_CUR].C = C_CUR + 2 * cs; s->e[E_CUR].crit = 0;
    s->e[E_BRIDGE].name = "adc_isr"; s->e[E_BRIDGE].nvic = 5;     /* 브리지 자리를 ADC ISR 로 */
    s->e[E_BRIDGE].C = 2; s->e[E_BRIDGE].T = T_CUR; s->e[E_BRIDGE].D = T_CUR; s->e[E_BRIDGE].J = 0;
    s->e[E_CUR].J = rta(s, E_BRIDGE).R;                          /* 태스크 출시 지터 = ADC ISR 응답 */
}
static uint32_t max_cs_for_cur_task(void) {
    uint32_t best = 0;
    for (uint32_t cs = 0; cs <= 20; cs++) {
        sys_t s; build_cur_task(&s, cs);
        if (rta(&s, E_CUR).ok) best = cs; else break;
    }
    return best;
}

/* ===================== 연습 2: 윈도우 워치독 ===================== */
/* 모니터는 xTaskDelayUntil 로 P 마다 출시, 킥 시각 = 출시 + 응답(R_min..R_max).
 * 연속 킥 간격 ∈ [P − (R_max − R_min), P + (R_max − R_min)]. WWDG: 창이 열리기(w_open) 전 킥 = 리셋,
 * w_close 넘기면 리셋. 둘 다 피하려면 w_open < 최소 간격 && 최대 간격 < w_close. */
static int wwdg_ok(uint32_t P, uint32_t rmin, uint32_t rmax, uint32_t w_open, uint32_t w_close) {
    uint32_t spread = rmax - rmin;
    return w_open < P - spread && P + spread < w_close;
}
static uint32_t monitor_rmax(int prio) {
    sys_t s; build_design(&s); propagate_jitter(&s);
    s.e[E_CANCMD].C = N_BURST * C_FRAME; s.e[E_CANCMD].T = T_BURST;   /* 버스트 하나를 작업 하나로 (근사) */
    s.e[E_WDG].prio = prio;
    return rta(&s, E_WDG).R;
}

/* ===================== 연습 3: CAN 비트레이트 ===================== */
static uint32_t depth_at_bitrate(uint32_t kbps) {
    sys_t s; build_design(&s);
    uint32_t a = 111u * 1000u / kbps;                  /* DLC 8 최단 프레임 111 비트 */
    s.e[E_CAN].T = a; s.e[E_CAN].D = HWFIFO * a;
    propagate_jitter(&s);
    return can_backlog(&s, E_CANCMD, N_BURST, a, C_FRAME, 200000).depth;
}

/* ===================== 연습 4: 로그 링 ===================== */
static uint32_t ring_for(uint32_t decim, uint32_t erase_us, uint32_t stall_us) {
    producer_t p[2] = {{T_SPEED * decim, 1}, {T_BURST, N_BURST}};
    uint32_t cycle = erase_us + PAGES_PER_SECTOR * (PROG_MAX + T_TICK);
    return log_ring_needed(p, 2, stall_us, REC_PER_PAGE, cycle, REC_PER_PAGE * PAGES_PER_SECTOR);
}
static uint32_t min_decim_sustainable(uint32_t erase_us) {
    for (uint32_t d = 1; d <= 100; d++)
        if (ring_for(d, erase_us, erase_us + PROG_MAX + 2 * T_TICK)) return d;
    return 0;
}

/* ===================== 연습 5: 민감도 ===================== */
static int design_ok_with_cspeed(uint32_t c, int need_depth3) {
    sys_t s; build_design(&s);
    s.e[E_SPEED].C = c;
    propagate_jitter(&s);
    for (int i = 0; i < s.n; i++) if (s.e[i].D && !rta(&s, i).ok) return 0;
    if (need_depth3) {
        backlog_t b = can_backlog(&s, E_CANCMD, N_BURST, A_CAN, C_FRAME, 200000);
        if (!b.ok || b.depth > CAN_QDEPTH) return 0;
    }
    return 1;
}
static uint32_t max_cspeed(int need_depth3) {
    uint32_t lo = C_SPEED, hi = T_SPEED;               /* lo 는 통과, hi 는 실패 */
    while (hi - lo > 1) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (design_ok_with_cspeed(mid, need_depth3)) lo = mid; else hi = mid;
    }
    return lo;
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "-v")) g_verbose = 1;

    /* 연습 1 */
    sys_t s; build_cur_task(&s, 0);
    rta_t r0 = rta(&s, E_CUR);
    CHECK(s.e[E_CUR].J == 10 && r0.ok && r0.R == 48);           /* ISR 판: 항상 25 */
    CHECK(r0.B == 4);                                            /* 이제 임계 구역에 막힌다 */
    uint32_t mcs = max_cs_for_cur_task();
    CHECK(mcs == 6);
    { sys_t t; build_cur_task(&t, mcs + 1); CHECK(!rta(&t, E_CUR).ok); }
    if (g_verbose) printf("ex1: cur loop as task: J %u R %u (C 25) -> jitter up to %u us; max context switch %u us\n",
                          s.e[E_CUR].J, r0.R, r0.R - C_CUR, mcs);

    /* 연습 2 */
    uint32_t r5 = monitor_rmax(5), r2 = monitor_rmax(2);
    CHECK(r5 == 48 && r2 > 500);
    CHECK(wwdg_ok(20000, C_MON, r5, 19000, 21000) && wwdg_ok(20000, C_MON, r2, 19000, 21000));
    CHECK(wwdg_ok(20000, C_MON, r5, 19900, 20100) && !wwdg_ok(20000, C_MON, r2, 19900, 20100));
    if (g_verbose) printf("ex2: monitor R_max prio5 %u us, prio2 %u us\n", r5, r2);

    /* 연습 3 */
    uint32_t d125 = depth_at_bitrate(125), d250 = depth_at_bitrate(250), d500 = depth_at_bitrate(500),
             d1000 = depth_at_bitrate(1000);
    CHECK(d1000 == 3 && d500 <= d1000 && d250 <= d500 && d125 <= d250 && d125 >= 1);
    if (g_verbose) printf("ex3: depth 125k %u 250k %u 500k %u 1M %u\n", d125, d250, d500, d1000);

    /* 연습 4 */
    uint32_t typ = ring_for(LOG_DECIM, 45000, 45000 + PROG_MAX + 2 * T_TICK);
    uint32_t max = ring_for(LOG_DECIM, ERASE_MAX, ERASE_MAX + PROG_MAX + 2 * T_TICK);
    uint32_t susp = ring_for(LOG_DECIM, ERASE_MAX, PROG_MAX + 2 * T_TICK);   /* 지우기 일시정지로 쓰기 끼워 넣기 */
    CHECK(max == 218 && typ < max && susp < typ);
    CHECK(min_decim_sustainable(ERASE_MAX) > 1 && min_decim_sustainable(45000) == 1);
    if (g_verbose) printf("ex4: ring typ %u max %u suspend %u records; min decimation @400ms %u, @45ms %u\n",
                          typ, max, susp, min_decim_sustainable(ERASE_MAX), min_decim_sustainable(45000));

    /* 연습 5 */
    uint32_t c_rta = max_cspeed(0), c_q = max_cspeed(1);
    CHECK(c_rta > 400 && c_q < c_rta && c_q >= C_SPEED);
    if (g_verbose) printf("ex5: max C_speed by RTA %u us, keeping CAN depth <= %u: %u us\n", c_rta, CAN_QDEPTH, c_q);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
