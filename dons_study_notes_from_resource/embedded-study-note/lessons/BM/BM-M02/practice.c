/* BM-M02 추가 연습 — 호스트 C (Mac 또는 VM 네이티브 cc). 펌웨어(QEMU) 에서 실측한 규칙을 모델로 옮겨 검증한다.
 *   연습 1  prio_reg_locate   : IRQn → 우선순위 레지스터 주소·바이트 (IPR / SHPR), 전수 검사
 *   연습 2  nvic_model_run    : NVIC 중재 모델이 QEMU 실측 트레이스(S1..S6, F2, F3, starter) 를 똑같이 재현하나
 *   연습 3  port_to_bits      : 4비트 설계를 3비트 파트로 옮기면 무엇이 바뀌나 + HAL PRIORITYGROUP ↔ PRIGROUP
 *   연습 4  rta_bound / sim   : 응답 시간 예산 (선점 임계값식 RTA) vs 이벤트 시뮬레이션, 나쁜 설계 2개
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-M02_pr && /tmp/BM-M02_pr
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ===================== 연습 1: 우선순위 레지스터 위치 ===================== */
typedef struct { uint32_t addr, word_addr, lane; uint8_t byte; } prio_loc_t;
/* irqn ≥ 0: NVIC_IPR (0xE000E400 + irqn), irqn < 0: 설정 가능한 시스템 예외만 SHPR (0xE000ED18 + exc - 4) */
static int prio_reg_locate(int irqn, uint32_t prio_bits, uint32_t prio, prio_loc_t *out) {
    if (prio_bits < 2u || prio_bits > 8u || prio >= (1u << prio_bits)) return -1;
    if (irqn >= 240) return -1;
    if (irqn >= 0) {
        out->addr = 0xE000E400u + (uint32_t)irqn;
    } else {
        int exc = 16 + irqn;                     /* -1 → 15 SysTick, -2 → 14 PendSV, -5 → 11 SVCall ... */
        static const int ok[] = { 4, 5, 6, 11, 12, 14, 15 }; /* MemManage BusFault UsageFault SVCall DebugMon PendSV SysTick */
        int found = 0;
        for (unsigned i = 0; i < sizeof ok / sizeof ok[0]; ++i) found |= (ok[i] == exc);
        if (!found) return -1;                   /* Reset/NMI/HardFault = 고정, 7..10·13 = 예약 */
        out->addr = 0xE000ED18u + (uint32_t)(exc - 4);
    }
    out->word_addr = out->addr & ~3u;
    out->lane = out->addr & 3u;
    out->byte = (uint8_t)(prio << (8u - prio_bits));
    return 0;
}
static void ex1(void) {
    printf("== 연습 1: prio_reg_locate ==\n");
    uint32_t bad = 0, n = 0;
    for (int irq = 0; irq < 240; ++irq)
        for (uint32_t p = 0; p < 16u; ++p) {
            prio_loc_t L;
            ++n;
            if (prio_reg_locate(irq, 4u, p, &L) != 0) { ++bad; continue; }
            /* 다른 공식: 워드 IPR[irq/4], 레인 irq%4, 워드 안 값 = byte << 8·lane */
            uint32_t word = 0xE000E400u + 4u * (uint32_t)(irq / 4);
            uint32_t word_val = (uint32_t)L.byte << (8u * L.lane);
            if (L.word_addr != word || L.lane != (uint32_t)(irq % 4) || ((word_val >> (8u * L.lane)) & 0xF0u) != p << 4) ++bad;
        }
    printf("  IRQ 0..239 x prio 0..15: %u/%u consistent\n", n - bad, n);
    CHECK(bad == 0);
    prio_loc_t L;
    CHECK(prio_reg_locate(-1, 4, 5, &L) == 0 && L.addr == 0xE000ED23u && L.byte == 0x50u);   /* SysTick = SHPR3[31:24] */
    CHECK(prio_reg_locate(-2, 4, 15, &L) == 0 && L.addr == 0xE000ED22u && L.byte == 0xF0u);  /* PendSV  = SHPR3[23:16] */
    CHECK(prio_reg_locate(-5, 4, 1, &L) == 0 && L.addr == 0xE000ED1Fu);                      /* SVCall  = SHPR2[31:24] */
    CHECK(prio_reg_locate(-12, 4, 1, &L) == 0 && L.addr == 0xE000ED18u);                     /* MemManage = SHPR1[7:0] */
    CHECK(prio_reg_locate(-13, 4, 1, &L) == -1 && prio_reg_locate(-14, 4, 1, &L) == -1);     /* HardFault, NMI: 고정 */
    CHECK(prio_reg_locate(-3, 4, 1, &L) == -1 && prio_reg_locate(16, 4, 16, &L) == -1);      /* 예약 / 범위 밖 prio */
    CHECK(prio_reg_locate(15, 4, 1, &L) == 0 && L.word_addr == 0xE000E40Cu && L.lane == 3u && L.byte == 0x10u);
    printf("  SysTick -> 0x%08X byte 0x%02X, IRQ15 -> word 0x%08X lane %u\n", 0xE000ED23u, 0x50u, L.word_addr, L.lane);
}

/* ===================== 연습 2: NVIC 중재 모델 ===================== */
enum { P, A, R, T, S, V, NS };
static const char k_let[NS] = { 'P', 'A', 'R', 'T', 'S', 'V' };
static const uint32_t k_exc[NS] = { 30, 31, 32, 33, 15, 14 };   /* 예외 번호 = 16 + IRQn (SysTick 15, PendSV 14) */
typedef struct {
    uint32_t prigroup, bits;         /* bits = 구현된 우선순위 비트 수 (하위 비트는 0 으로 읽힘) */
    uint8_t byte[NS];                /* 우선순위 바이트 (쓴 값) */
    const char *plan[NS];            /* 진입 시 pend 할 소스 글자 (1회용) */
} model_t;
typedef struct {
    model_t m;
    int pending[NS], active[NS], used[NS];
    int stack[16], sp;
    char tr[128]; int tn;
} sim_t;
static uint32_t eff(const model_t *m, int s) { return m->byte[s] & (0xFFu << (8u - m->bits)) & 0xFFu; }
static uint32_t grp(const model_t *m, uint32_t raw) { return raw & (0xFFu << (m->prigroup + 1u)) & 0xFFu; }
static int letter(char c) { for (int s = 0; s < NS; ++s) if (k_let[s] == c) return s; return -1; }
static uint32_t exec_group(const sim_t *x) {            /* 실행 우선순위 = active 중 가장 급한 그룹, 없으면 256 */
    uint32_t g = 256;
    for (int i = 0; i < x->sp; ++i) { uint32_t v = grp(&x->m, eff(&x->m, x->stack[i])); if (v < g) g = v; }
    return g;
}
static int best_pending(const sim_t *x) {               /* (raw 우선순위 = 그룹 다음 서브, 같으면 예외 번호) 최소 */
    int b = -1;
    for (int s = 0; s < NS; ++s) {
        if (!x->pending[s]) continue;
        if (b < 0 || eff(&x->m, s) < eff(&x->m, b) || (eff(&x->m, s) == eff(&x->m, b) && k_exc[s] < k_exc[b])) b = s;
    }
    return b;
}
static void take_pending(sim_t *x);
static void run_handler(sim_t *x, int s) {
    x->pending[s] = 0; x->active[s] = 1; x->stack[x->sp++] = s;
    x->tr[x->tn++] = k_let[s];
    if (!x->used[s] && x->m.plan[s]) {
        x->used[s] = 1;
        for (const char *p = x->m.plan[s]; *p; ++p) { x->pending[letter(*p)] = 1; take_pending(x); }
    }
    x->tr[x->tn++] = (char)(k_let[s] + 32);
    x->sp--; x->active[s] = 0;
}
static void take_pending(sim_t *x) {                    /* 선점 + 복귀 시 꼬리 물기 */
    for (;;) {
        int b = best_pending(x);
        if (b < 0 || x->sp >= 15 || grp(&x->m, eff(&x->m, b)) >= exec_group(x)) return;
        run_handler(x, b);
    }
}
static const char *nvic_model_run(const model_t *m, const char *start, char *out) {
    sim_t x;
    memset(&x, 0, sizeof x);
    x.m = *m;
    for (const char *p = start; *p; ++p) { x.pending[letter(*p)] = 1; take_pending(&x); }
    x.tr[x.tn++] = '.';
    x.tr[x.tn] = 0;
    strcpy(out, x.tr);
    return out;
}
static const uint8_t k_design[NS] = { 0x00, 0x20, 0x40, 0x40, 0x50, 0xF0 };   /* solution.c 설계 (QEMU 실측 바이트) */
static int model_case(const char *id, uint32_t pg, uint32_t bits, const uint8_t *bytes, const char *start,
                      const char *plan_src, const char *plan, const char *qemu) {
    model_t m;
    memset(&m, 0, sizeof m);
    m.prigroup = pg; m.bits = bits;
    memcpy(m.byte, bytes, NS);
    if (plan_src) for (int i = 0; plan_src[i]; ++i) {     /* "RA" + "A,P" 형식: 소스 i 의 plan */
        const char *q = plan; for (int k = 0; k < i; ++k) { q = strchr(q, ','); q++; }
        static char buf[NS][8];
        int n = 0; while (q[n] && q[n] != ',') { buf[letter(plan_src[i])][n] = q[n]; ++n; }
        buf[letter(plan_src[i])][n] = 0;
        m.plan[letter(plan_src[i])] = buf[letter(plan_src[i])];
    }
    char got[128];
    nvic_model_run(&m, start, got);
    int ok = strcmp(got, qemu) == 0;
    printf("  %-4s model %-16s QEMU %-16s %s\n", id, got, qemu, ok ? "same" : "DIFF");
    return ok;
}
static void ex2(void) {
    printf("== 연습 2: NVIC 중재 모델 vs QEMU 실측 트레이스 ==\n");
    static const uint8_t zero[NS] = { 0 };
    uint8_t f3[NS]; memcpy(f3, k_design, NS); f3[R] = 0x04;
    CHECK(model_case("S1", 4, 8, k_design, "R", "RA", "A,P", "RAPpar."));
    CHECK(model_case("S2", 4, 8, k_design, "P", "P", "A", "PpAa."));
    CHECK(model_case("S3", 4, 8, k_design, "S", "S", "R", "SsRr."));
    CHECK(model_case("S3b", 4, 8, k_design, "R", "R", "S", "RrSs."));
    CHECK(model_case("S4", 4, 8, k_design, "P", "P", "VSTR", "PpRrTtSsVv."));
    CHECK(model_case("S5", 4, 8, k_design, "R", "R", "VA", "RAarVv."));
    CHECK(model_case("S6", 4, 8, k_design, "T", "T", "T", "TtTt."));
    CHECK(model_case("F2", 0, 8, k_design, "S", "S", "R", "SRrs."));
    CHECK(model_case("F3", 4, 8, f3, "A", "A", "R", "ARra."));
    CHECK(model_case("st1", 0, 8, zero, "R", "RA", "A,P", "RrAaPp."));      /* starter.c 실측 */
    CHECK(model_case("st4", 0, 8, zero, "P", "P", "VSTR", "PpVvSsRrTt."));
    CHECK(model_case("st5", 0, 8, zero, "R", "R", "VA", "RrVvAa."));
}

/* ===================== 연습 3: 이식성 (8 → 4 → 3 비트) ===================== */
static void port_to_bits(const uint8_t *in, uint32_t bits, uint8_t *out) {
    for (int s = 0; s < NS; ++s) out[s] = (uint8_t)(in[s] & (0xFFu << (8u - bits)));
}
static void ex3(void) {
    printf("== 연습 3: PRIGROUP x 구현 비트 -> (선점 단계, 서브 단계) ==\n  PRIGROUP  HAL group      bits=3     bits=4     bits=8\n");
    uint32_t tbl[8][3][2];
    static const uint32_t B[3] = { 3, 4, 8 };
    for (uint32_t pg = 0; pg < 8; ++pg) {
        for (int k = 0; k < 3; ++k) {
            uint32_t gb = 7u - pg < B[k] ? 7u - pg : B[k];    /* 그룹 = 비트 [7 : pg+1] 중 구현된 것 */
            uint32_t sb = B[k] - gb;
            tbl[pg][k][0] = 1u << gb; tbl[pg][k][1] = 1u << sb;
        }
        char hal[32] = "-";
        if (pg >= 3) snprintf(hal, sizeof hal, "PRIORITYGROUP_%u", 7u - pg);
        printf("  %u         %-14s %3ux%-3u    %3ux%-3u    %3ux%-3u\n", pg, hal,
               tbl[pg][0][0], tbl[pg][0][1], tbl[pg][1][0], tbl[pg][1][1], tbl[pg][2][0], tbl[pg][2][1]);
    }
    CHECK(tbl[4][1][0] == 8 && tbl[4][1][1] == 2);      /* 설계: PRIGROUP 4, 4비트 → 8 그룹 × 2 서브 */
    CHECK(tbl[3][1][0] == 16 && tbl[3][1][1] == 1);     /* ST HAL PRIORITYGROUP_4 (FreeRTOS 권장) */
    CHECK(tbl[4][0][0] == 8 && tbl[4][0][1] == 1);      /* 3비트 파트에선 서브가 사라진다 */
    CHECK(tbl[0][2][0] == 128 && tbl[0][2][1] == 2);    /* QEMU(8비트) 리셋값 PRIGROUP 0 */

    uint8_t b3[NS];
    port_to_bits(k_design, 3, b3);
    printf("  design bytes on a 3-bit part:");
    for (int s = 0; s < NS; ++s) printf(" %c=0x%02X", k_let[s], b3[s]);
    printf("\n");
    CHECK(b3[S] == 0x40 && b3[R] == 0x40);                /* SysTick 의 서브 1 이 잘려 UART 와 동률 */
    CHECK(model_case("S4/3b", 4, 3, k_design, "P", "P", "VSTR", "PpSsRrTtVv."));   /* 동률 → 예외 번호 15 가 먼저 */
    /* 선점 관계(그룹)는 모든 쌍에서 그대로인가 */
    model_t m4 = { 4, 4, { 0 }, { 0 } }, m3 = { 4, 3, { 0 }, { 0 } };
    memcpy(m4.byte, k_design, NS); memcpy(m3.byte, k_design, NS);
    int same = 1;
    for (int a = 0; a < NS; ++a) for (int b = 0; b < NS; ++b)
        same &= ((grp(&m4, eff(&m4, a)) < grp(&m4, eff(&m4, b))) == (grp(&m3, eff(&m3, a)) < grp(&m3, eff(&m3, b))));
    CHECK(same);
}

/* ===================== 연습 4: 응답 시간 예산 ===================== */
typedef struct { const char *name; uint8_t byte; uint32_t exc; uint64_t C, T, D; } task_t;
/* ns. C 는 진입/복귀 오버헤드 포함 가정 (Cortex-M3/M4 ≈ 12 사이클씩, 168 MHz ≈ 71 ns — ARM-A08) */
static const task_t k_tasks[NS] = {
    { "MOTOR_PWM", 0x00, 30, 2000, 50000, 10000 },        /* 20 kHz, 업데이트 후 10 µs 안에 듀티 기록 */
    { "ADC", 0x20, 31, 12000, 50000, 40000 },             /* 전류 루프 계산, 다음 업데이트 전에 끝 */
    { "UART_RX", 0x40, 32, 1000, 86806, 86806 },          /* 115200 8N1: 1 문자 = 86.806 µs → ORE 전에 */
    { "UART_TX", 0x40, 33, 800, 86806, 86806 },
    { "SysTick", 0x50, 15, 1500, 1000000, 1000000 },
    { "PendSV", 0xF0, 14, 30000, 694448, 694448 },         /* 8 바이트 줄 = 8 문자 시간마다 명령 해석 */
};
static int better(const task_t *t, int a, int b) {        /* a 가 b 보다 먼저 뽑히나 (raw 바이트, 예외 번호) */
    return t[a].byte < t[b].byte || (t[a].byte == t[b].byte && t[a].exc < t[b].exc);
}
static uint64_t grpb(uint8_t byte, uint32_t pg) { return byte & (0xFFu << (pg + 1u)) & 0xFFu; }
/* 선점 임계값(preemption threshold) 식 RTA: 시작 시각 S, 끝 시각 F. D ≤ T 가정 (작업 1개). 넘으면 UINT64_MAX */
static uint64_t rta_bound(const task_t *t, uint32_t pg, int i) {
    uint64_t Bk = 0;
    for (int k = 0; k < NS; ++k)                          /* 차단: 같은 그룹, 순서가 뒤인 것 하나가 이미 실행 중 */
        if (k != i && grpb(t[k].byte, pg) == grpb(t[i].byte, pg) && better(t, i, k) && t[k].C > Bk) Bk = t[k].C;
    uint64_t S = Bk, prev;
    do {
        prev = S; S = Bk;
        for (int j = 0; j < NS; ++j) if (j != i && better(t, j, i)) S += (prev / t[j].T + 1u) * t[j].C;
        if (S > t[i].D) return UINT64_MAX;
    } while (S != prev);
    uint64_t F = S + t[i].C;
    do {
        prev = F; F = S + t[i].C;
        for (int j = 0; j < NS; ++j)
            if (grpb(t[j].byte, pg) < grpb(t[i].byte, pg)) {
                uint64_t n = (prev + t[j].T - 1u) / t[j].T, before = S / t[j].T + 1u;
                if (n > before) F += (n - before) * t[j].C;
            }
        if (F > t[i].D) return UINT64_MAX;
    } while (F != prev);
    return F;
}
/* 이벤트 시뮬레이션: NVIC 규칙 그대로 (소스당 pending 1비트 → 겹치면 lost), 오프셋에서 주기 방출 */
static uint32_t xs = 0x2545F491u;
static uint32_t xorshift32(void) { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }
static void simulate(const task_t *t, uint32_t pg, const uint64_t *off, uint64_t horizon, uint64_t *maxR, uint32_t *lost) {
    uint64_t next[NS], rel[NS] = { 0 }, rem[NS] = { 0 }, jobrel[NS] = { 0 }, now = 0;
    int pend[NS] = { 0 }, stack[NS], sp = 0;
    for (int s = 0; s < NS; ++s) next[s] = off[s];
    while (now <= horizon) {
        for (int s = 0; s < NS; ++s) while (next[s] <= now) {
            if (pend[s]) lost[s]++;                       /* pending 비트가 이미 1 → 한 건 사라짐 */
            pend[s] = 1; rel[s] = next[s]; next[s] += t[s].T;
        }
        for (;;) {                                        /* 선점 / 꼬리 물기 */
            int b = -1;
            for (int s = 0; s < NS; ++s) if (pend[s] && (b < 0 || better(t, s, b))) b = s;
            uint64_t eg = 256;
            for (int k = 0; k < sp; ++k) if (grpb(t[stack[k]].byte, pg) < eg) eg = grpb(t[stack[k]].byte, pg);
            if (b < 0 || grpb(t[b].byte, pg) >= eg) break;
            pend[b] = 0; stack[sp++] = b; rem[b] = t[b].C; jobrel[b] = rel[b];
        }
        uint64_t nr = UINT64_MAX;
        for (int s = 0; s < NS; ++s) if (next[s] < nr) nr = next[s];
        if (sp == 0) { now = nr; continue; }
        int top = stack[sp - 1];
        uint64_t end = now + rem[top];
        if (end <= nr) {
            now = end; sp--;
            if (now - jobrel[top] > maxR[top]) maxR[top] = now - jobrel[top];
        } else { rem[top] -= nr - now; now = nr; }
    }
}
static int budget(const char *title, const uint8_t *bytes, uint32_t pg, uint64_t *Rpwm_sim) {
    task_t t[NS];
    memcpy(t, k_tasks, sizeof t);
    for (int s = 0; s < NS; ++s) t[s].byte = bytes[s];
    uint64_t maxR[NS] = { 0 }, off[NS];
    uint32_t lost[NS] = { 0 };
    for (int run = 0; run < 3000; ++run) {                /* 0: 동시 방출, 1..NS: 차단자 먼저, 나머지: 무작위 */
        for (int s = 0; s < NS; ++s) off[s] = run == 0 ? 0 : (run <= NS ? (s == run - 1 ? 0 : 1) : xorshift32() % t[s].T);
        simulate(t, pg, off, 2000000u, maxR, lost);
    }
    printf("  -- %s (PRIGROUP %u)\n  source      byte    C(ns)    T(ns)    D(ns)  R_bound  R_sim_max  verdict\n", title, pg);
    int all_ok = 1, sound = 1;
    for (int i = 0; i < NS; ++i) {
        uint64_t Rb = rta_bound(t, pg, i);
        int ok = Rb != UINT64_MAX;
        all_ok &= ok;
        if (ok && maxR[i] > Rb) sound = 0;
        char rb[24];
        if (ok) snprintf(rb, sizeof rb, "%llu", (unsigned long long)Rb); else snprintf(rb, sizeof rb, "> D");
        printf("  %-10s  0x%02X %8llu %8llu %8llu %8s %10llu  %s\n", t[i].name, t[i].byte, (unsigned long long)t[i].C,
               (unsigned long long)t[i].T, (unsigned long long)t[i].D, rb, (unsigned long long)maxR[i],
               ok ? "OK" : (maxR[i] > t[i].D ? "MISS (sim too)" : "MISS (bound)"));
    }
    *Rpwm_sim = maxR[P];
    CHECK(sound);                                          /* 시뮬레이션 최악값 ≤ 해석 상한 */
    return all_ok;
}
static void ex4(void) {
    printf("== 연습 4: 응답 시간 예산 (상한식 vs 3000 회 시뮬레이션) ==\n");
    static const uint8_t zero[NS] = { 0 };
    static const uint8_t adc_g0[NS] = { 0x00, 0x10, 0x40, 0x40, 0x50, 0xF0 };   /* ADC 를 PWM 그룹(0) 의 서브 1 로 */
    uint64_t r_design, r_zero, r_adc;
    CHECK(budget("design", k_design, 4, &r_design) == 1);
    CHECK(budget("starter: all 0", zero, 0, &r_zero) == 0);
    CHECK(budget("ADC in PWM group", adc_g0, 4, &r_adc) == 0);
    CHECK(r_design <= 10000u && r_zero > 10000u && r_adc > 10000u);   /* PWM D = 10 µs: 실제로도 놓친다 */
}

int main(void) {
    ex1();
    ex2();
    ex3();
    ex4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
