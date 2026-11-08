/* SIV-B13 추가 연습 — 한 번에 검증
 *
 * 본문 solution.c 의 모델·백엔드·스위트·triage 를 그대로 가져오고, 그 위에 연습 1~4 를 얹는다.
 *   연습 1: 읽기 재시도 decorator 백엔드 (응답 유실되는 JTAG)
 *   연습 2: shift-left — 실제 발견 단계와 가장 이른 단계의 차이
 *   연습 3: SILICON_ONLY 실패를 여러 유닛에서 재현 → 설계 문제인가, 유닛 결함인가
 *   연습 4: 플랫폼별 기대값 (FPGA 빌드의 ID 레지스터)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ======================= Part A: 발견 단계 분류 ======================= */
enum {                      /* 버그를 "보려면" 필요한 것 = 단계가 가진 능력 */
    C_LOGIC  = 1u << 0,     /* 디지털 논리 동작 (RTL 그대로)                        */
    C_4STATE = 1u << 1,     /* X/Z 4-state 모델링 (이벤트 시뮬레이터만)             */
    C_REALIO = 1u << 2,     /* 진짜 외부 상대 장치(호스트, 링크 파트너)와 연결      */
    C_TIMING = 1u << 3,     /* 실제 ASIC 타이밍: 게이트 지연, 클럭 트리, 메타안정   */
    C_ANALOG = 1u << 4,     /* 실제 아날로그: PLL, PHY, 전원망(IR drop), 누설       */
    C_PVT    = 1u << 5,     /* 전압·온도 코너를 실제로 흔들 수 있음                 */
    C_VOLUME = 1u << 6,     /* 수천~수백만 개 유닛 (공정 산포, 경년 열화)           */
};
typedef enum { ST_NONE = -1, ST_SIM, ST_EMU, ST_FPGA, ST_SILICON, ST_PROD, ST_COUNT } stage_t;
typedef struct { const char *name; uint32_t caps; int log10_cycles; } stage_info_t;
/* log10_cycles: 테스트 하나에 현실적으로 쓸 수 있는 사이클 수의 어림값 (속도 × 하루 남짓) */
static const stage_info_t STAGES[ST_COUNT] = {
    { "SIM",     C_LOGIC | C_4STATE,                                  8 },  /* ~1 kHz  */
    { "EMU",     C_LOGIC | C_REALIO,                                 11 },  /* ~1 MHz  */
    { "FPGA",    C_LOGIC | C_REALIO,                                 12 },  /* ~10 MHz */
    { "SILICON", C_LOGIC | C_REALIO | C_TIMING | C_ANALOG | C_PVT,   14 },  /* ~1 GHz  */
    { "PROD",    C_LOGIC | C_REALIO | C_TIMING | C_ANALOG | C_PVT | C_VOLUME, 18 },
};
typedef struct { const char *name; uint32_t needs; int log10_cycles; } bug_t;
static const bug_t BUGS[20] = {
    { "FIFO full 플래그 off-by-one",            C_LOGIC,                       4 },
    { "레지스터 리셋값 오류(RTL)",              C_LOGIC,                       2 },
    { "W1C 를 RW 로 구현",                      C_LOGIC,                       2 },
    { "주소 디코더가 bit 4 무시(aliasing)",     C_LOGIC,                       3 },
    { "리셋 없는 플롭 → X 전파",                C_LOGIC | C_4STATE,            3 },
    { "아비터 starvation (부팅 트래픽)",        C_LOGIC,                      10 },
    { "Linux 부팅 중 드라이버-HW 행",           C_LOGIC,                      10 },
    { "멀티코어 캐시 일관성 deadlock",          C_LOGIC,                      11 },
    { "실제 USB 호스트와 프로토콜 interop",     C_LOGIC | C_REALIO,            9 },
    { "카운터 rollover 경쟁 (장시간 스트리밍)", C_LOGIC,                      12 },
    { "slow 코너 setup 위반 (타이밍 마진)",     C_TIMING | C_PVT,              6 },
    { "CDC 메타안정 → 드문 FIFO 오염",          C_TIMING,                     12 },
    { "저온에서 PLL lock 실패",                 C_ANALOG | C_PVT,              3 },
    { "DDR PHY 아이 마진 부족",                 C_ANALOG | C_TIMING,           9 },
    { "긴 채널에서 SerDes BER 초과",            C_ANALOG | C_REALIO,          13 },
    { "전원 시퀀스(IO 먼저) → POR 글리치",      C_ANALOG,                      1 },
    { "전 코어 동시 토글 → 전압 droop",         C_ANALOG | C_TIMING,           9 },
    { "실워크로드 10^13 사이클 후 TLB errata",  C_LOGIC,                      13 },
    { "저전압 SRAM 약한 셀 (0.1% 유닛)",        C_ANALOG | C_PVT | C_VOLUME,   3 },
    { "트랜지스터 경년 열화 (일부 유닛)",       C_ANALOG | C_VOLUME,          15 },
};

/* A1: 버그가 필요로 하는 능력을 모두 갖추고, 사이클 예산 안에 드는 가장 이른 단계. 없으면 ST_NONE */
static stage_t earliest_stage(const bug_t *b) {
    for (int s = 0; s < ST_COUNT; s++)
        if ((STAGES[s].caps & b->needs) == b->needs && b->log10_cycles <= STAGES[s].log10_cycles)
            return (stage_t)s;
    return ST_NONE;
}

/* ======================= Part B: 레지스터 기술 + 백엔드 ======================= */
typedef struct {
    const char *name;
    uint32_t off, reset;
    uint32_t rw, w1c, wo, vol;  /* 나머지 비트는 RO. vol = HW 가 바꾸는 비트 (비교 제외)          */
    int force_idx;              /* >= 0: 1 을 쓰면 regs[force_idx] 의 같은 비트를 세움 (test force) */
} reg_desc_t;
enum { R_ID, R_CTRL, R_STATUS, R_INT_STAT, R_INT_EN, R_INT_FORCE, R_SCRATCH, NREGS };
#define QSPI0_BASE 0x40010000u
static const reg_desc_t QSPI_REGS[NREGS] = {   /* 가상의 QSPI 컨트롤러 */
    { "ID",        0x00, 0x51530A01u, 0,           0,    0,    0,    -1 },
    { "CTRL",      0x04, 0x00000400u, 0x0000FF0Fu, 0,    0,    0,    -1 },  /* EN, MODE[3:1], CLKDIV[15:8] */
    { "STATUS",    0x08, 0x00000002u, 0,           0,    0,    0x1u, -1 },  /* BUSY[0] vol, TXE[1]         */
    { "INT_STAT",  0x0C, 0,           0,           0x7u, 0,    0,    -1 },  /* W1C                         */
    { "INT_EN",    0x10, 0,           0x7u,        0,    0,    0,    -1 },
    { "INT_FORCE", 0x14, 0,           0,           0,    0x7u, 0,    R_INT_STAT },
    { "SCRATCH",   0x18, 0,           0xFFFFFFFFu, 0,    0,    0,    -1 },
};

typedef enum { BUS_OK = 0, BUS_ERR_SLVERR, BUS_ERR_TIMEOUT, BUS_ERR_PROTO } bus_status_t;
typedef struct {
    const char *name;
    bus_status_t (*read32)(void *ctx, uint32_t addr, uint32_t *val);
    bus_status_t (*write32)(void *ctx, uint32_t addr, uint32_t val);
    void *ctx;
} bus_t;

/* ---- 백엔드 1: mock (SIV-B02 축약) + 결함 주입 — 주어진 코드 ---- */
typedef struct {
    uint32_t reset_xor[NREGS];  /* 리셋값 tie-off 오류 */
    uint32_t stuck1[NREGS], stuck0[NREGS];
    int w1c_as_rw;              /* 이 레지스터의 W1C 비트가 RW 처럼 동작 (-1 = 없음) */
} fault_t;
typedef struct { const reg_desc_t *regs; uint32_t base; uint32_t st[NREGS]; fault_t f; } mock_t;

static void mock_init(mock_t *m, const fault_t *f) {
    m->regs = QSPI_REGS; m->base = QSPI0_BASE;
    if (f) m->f = *f; else { memset(&m->f, 0, sizeof m->f); m->f.w1c_as_rw = -1; }
    for (int i = 0; i < NREGS; i++) m->st[i] = m->regs[i].reset ^ m->f.reset_xor[i];
}
static int mock_find(const mock_t *m, uint32_t addr) {
    for (int i = 0; i < NREGS; i++) if (m->base + m->regs[i].off == addr) return i;
    return -1;
}
static bus_status_t mock_read32(void *ctx, uint32_t addr, uint32_t *val) {
    mock_t *m = ctx; int i = mock_find(m, addr);
    if (i < 0) return BUS_ERR_SLVERR;                    /* 디코드 안 되는 주소 */
    const reg_desc_t *d = &m->regs[i];
    m->st[i] ^= d->vol;                                  /* BUSY 는 읽을 때마다 바뀐다 (HW 활동) */
    *val = ((m->st[i] | m->f.stuck1[i]) & ~m->f.stuck0[i]) & ~d->wo;
    return BUS_OK;
}
static bus_status_t mock_write32(void *ctx, uint32_t addr, uint32_t w) {
    mock_t *m = ctx; int i = mock_find(m, addr);
    if (i < 0) return BUS_ERR_SLVERR;
    const reg_desc_t *d = &m->regs[i];
    uint32_t rw = d->rw, w1c = d->w1c;
    if (m->f.w1c_as_rw == i) { rw |= w1c; w1c = 0; }
    uint32_t v = m->st[i];
    v = (v & ~rw) | (w & rw);
    v &= ~(w & w1c);
    m->st[i] = v;
    if (d->force_idx >= 0) m->st[d->force_idx] |= w & d->wo;
    return BUS_OK;
}

/* ---- 백엔드 2: JTAG/OpenOCD 식 원격 접근 ---- */
/* transport: 명령 한 줄을 보내고 응답 한 줄을 받는다. 0 = 성공, < 0 = 링크 오류 (TCP/JTAG 끊김) */
typedef int (*xfer_fn)(void *tctx, const char *cmd, char *resp, size_t cap);
typedef struct { xfer_fn xfer; void *tctx; char last_cmd[48]; unsigned ncmd; } jtag_t;

/* B2: "mdw 0x%08x" 를 보내고 "0x<addr>: <value>" 응답을 파싱. 링크 오류 → TIMEOUT,
 *     "ERR" 응답 → SLVERR, 형식이 틀리거나 주소 echo 가 다르면 → PROTO */
static bus_status_t jtag_read32(void *ctx, uint32_t addr, uint32_t *val) {
    jtag_t *j = ctx; char resp[64]; unsigned a, v; char extra;
    snprintf(j->last_cmd, sizeof j->last_cmd, "mdw 0x%08x", (unsigned)addr);
    j->ncmd++;
    if (j->xfer(j->tctx, j->last_cmd, resp, sizeof resp) != 0) return BUS_ERR_TIMEOUT;
    if (strncmp(resp, "ERR", 3) == 0) return BUS_ERR_SLVERR;
    if (sscanf(resp, "0x%x: %x %c", &a, &v, &extra) != 2 || a != addr) return BUS_ERR_PROTO;
    *val = v;
    return BUS_OK;
}
/* B2: "mww 0x%08x 0x%08x" 를 보낸다. 성공 응답은 빈 줄 */
static bus_status_t jtag_write32(void *ctx, uint32_t addr, uint32_t val) {
    jtag_t *j = ctx; char resp[64];
    snprintf(j->last_cmd, sizeof j->last_cmd, "mww 0x%08x 0x%08x", (unsigned)addr, (unsigned)val);
    j->ncmd++;
    if (j->xfer(j->tctx, j->last_cmd, resp, sizeof resp) != 0) return BUS_ERR_TIMEOUT;
    if (strncmp(resp, "ERR", 3) == 0) return BUS_ERR_SLVERR;
    if (resp[0] != '\0') return BUS_ERR_PROTO;
    return BUS_OK;
}

/* 가짜 OpenOCD + 보드 (주어진 코드): 명령을 파싱해 "실리콘" mock 에 전달한다.
 * drop_after >= 0 이면 그 횟수 이후 링크가 끊긴다 (케이블, 보드 전원, JTAG 클럭 문제 흉내). */
typedef struct { mock_t *chip; int drop_after; int nxfer; } target_t;
static int target_xfer(void *tctx, const char *cmd, char *resp, size_t cap) {
    target_t *t = tctx; unsigned a, v; uint32_t val;
    if (t->drop_after >= 0 && t->nxfer >= t->drop_after) return -1;
    t->nxfer++;
    resp[0] = '\0';
    if (sscanf(cmd, "mdw 0x%x", &a) == 1) {
        if (mock_read32(t->chip, a, &val) != BUS_OK) snprintf(resp, cap, "ERR slverr");
        else snprintf(resp, cap, "0x%08x: %08x ", a, (unsigned)val);
    } else if (sscanf(cmd, "mww 0x%x 0x%x", &a, &v) == 2) {
        if (mock_write32(t->chip, a, v) != BUS_OK) snprintf(resp, cap, "ERR slverr");
    } else {
        snprintf(resp, cap, "ERR unknown command");
    }
    return 0;
}


/* ---- 접근 계층: 테스트는 이 두 함수만 안다 ---- */
static const bus_t *g_bus;
static uint32_t g_base, g_txn, g_txn_hash;
static void bus_attach(const bus_t *b, uint32_t base) {
    g_bus = b; g_base = base; g_txn = 0; g_txn_hash = 2166136261u;
}
static void txn_log(char dir, uint32_t off, uint32_t v) {   /* FNV-1a: (방향, 오프셋, 값) 시퀀스 지문 */
    uint32_t w[3] = { (uint32_t)dir, off, v };
    for (int k = 0; k < 3; k++)
        for (int byte = 0; byte < 4; byte++) {
            g_txn_hash ^= (w[k] >> (8 * byte)) & 0xFFu;
            g_txn_hash *= 16777619u;
        }
    g_txn++;
}
/* B1: 현재 백엔드로 base+off 에 접근하고, 성공한 트랜잭션만 txn_log 에 남긴다. 상태는 그대로 돌려준다 */
static bus_status_t reg_read32(uint32_t off, uint32_t *val) {
    bus_status_t s = g_bus->read32(g_bus->ctx, g_base + off, val);
    if (s == BUS_OK) txn_log('R', off, *val);
    return s;
}
static bus_status_t reg_write32(uint32_t off, uint32_t val) {
    bus_status_t s = g_bus->write32(g_bus->ctx, g_base + off, val);
    if (s == BUS_OK) txn_log('W', off, val);
    return s;
}

/* ---- 테스트 스위트 (SIV-B03~B05 축약, 주어진 코드) — 백엔드를 모른다 ---- */
typedef struct { int fails; bool infra; char first[128]; } tres_t;
static void fmt_fail(char *buf, size_t cap, const reg_desc_t *d, uint32_t exp, uint32_t act) {
    uint32_t diff = exp ^ act;
    int n = snprintf(buf, cap, "reg=%s off=0x%03X exp=0x%08X act=0x%08X diff=0x%08X bits=[",
                     d->name, (unsigned)d->off, (unsigned)exp, (unsigned)act, (unsigned)diff);
    for (int b = 0, first = 1; b < 32 && n > 0 && (size_t)n < cap; b++)
        if (diff >> b & 1u) { n += snprintf(buf + n, cap - (size_t)n, first ? "%d" : ",%d", b); first = 0; }
    if (n > 0 && (size_t)n < cap) snprintf(buf + n, cap - (size_t)n, "]");
}
static void expect(tres_t *r, const reg_desc_t *d, uint32_t exp, uint32_t act) {
    if (exp == act) return;
    if (r->fails++ == 0) fmt_fail(r->first, sizeof r->first, d, exp, act);
}
static bool rd(tres_t *r, const reg_desc_t *d, uint32_t *v) {
    if (reg_read32(d->off, v) == BUS_OK) return true;
    r->infra = true; return false;
}
static bool wr(tres_t *r, const reg_desc_t *d, uint32_t v) {
    if (reg_write32(d->off, v) == BUS_OK) return true;
    r->infra = true; return false;
}
static void t_reset(tres_t *r) {                 /* B03: 리셋 직후 값 (WO·volatile 비트 제외) */
    for (int i = 0; i < NREGS; i++) {
        const reg_desc_t *d = &QSPI_REGS[i]; uint32_t v, care = ~(d->wo | d->vol);
        if (!rd(r, d, &v)) return;
        expect(r, d, d->reset & care, v & care);
    }
}
static void t_rw(tres_t *r) {                    /* B04: RW 비트에 4패턴, 나머지 비트엔 0(무해) */
    static const uint32_t pats[4] = { 0x00000000u, 0xFFFFFFFFu, 0x55555555u, 0xAAAAAAAAu };
    for (int i = 0; i < NREGS; i++) {
        const reg_desc_t *d = &QSPI_REGS[i]; uint32_t v, care = ~(d->wo | d->vol);
        if (!d->rw) continue;
        for (int p = 0; p < 4; p++) {
            if (!wr(r, d, pats[p] & d->rw) || !rd(r, d, &v)) return;
            expect(r, d, ((pats[p] & d->rw) | (d->reset & ~d->rw)) & care, v & care);
        }
        if (!wr(r, d, d->reset & d->rw)) return;
    }
}
static void t_walk(tres_t *r) {                  /* B05: walking 1 / walking 0 (RW 비트 안에서) */
    for (int i = 0; i < NREGS; i++) {
        const reg_desc_t *d = &QSPI_REGS[i]; uint32_t v;
        for (int b = 0; b < 32; b++) {
            uint32_t one = 1u << b;
            if (!(d->rw & one)) continue;
            if (!wr(r, d, one) || !rd(r, d, &v)) return;
            expect(r, d, one, v & d->rw);
            if (!wr(r, d, d->rw & ~one) || !rd(r, d, &v)) return;
            expect(r, d, d->rw & ~one, v & d->rw);
        }
        if (d->rw && !wr(r, d, d->reset & d->rw)) return;
    }
}
static void t_w1c(tres_t *r) {                   /* B04/B11: force 로 세우고, 1 을 써서 지운다 */
    for (int i = 0; i < NREGS; i++) {
        const reg_desc_t *d = &QSPI_REGS[i]; uint32_t v;
        if (d->force_idx < 0) continue;
        const reg_desc_t *t = &QSPI_REGS[d->force_idx];
        for (int b = 0; b < 32; b++) {
            uint32_t one = 1u << b;
            if (!(d->wo & one)) continue;
            if (!wr(r, d, one) || !rd(r, t, &v)) return;
            expect(r, t, one, v & t->w1c);
            if (!wr(r, t, one) || !rd(r, t, &v)) return;
            expect(r, t, 0, v & t->w1c);
        }
    }
}
typedef enum { R_PASS, R_FAIL, R_INFRA } result_t;
typedef struct { const char *name; void (*fn)(tres_t *); } test_t;
#define NTESTS 4
static const test_t SUITE[NTESTS] = {   /* 순서 고정: reset_value 가 맨 처음 (리셋 직후 상태 가정) */
    { "reset_value", t_reset }, { "rw_pattern", t_rw }, { "walking_1_0", t_walk }, { "w1c_clear", t_w1c },
};
/* B3: 테스트 하나 실행. 버스가 한 번이라도 실패했으면 비교 결과와 상관없이 R_INFRA */
static result_t run_test(const test_t *t, tres_t *r) {
    memset(r, 0, sizeof *r);
    t->fn(r);
    if (r->infra) return R_INFRA;
    return r->fails ? R_FAIL : R_PASS;
}

/* ======================= Part C: 같은 테스트, 두 단계 → triage ======================= */
typedef enum { TR_PASS, TR_RTL_BUG, TR_SILICON_ONLY, TR_MODEL_MISMATCH, TR_DIVERGENT, TR_INFRA, TR_COUNT } triage_t;
static const char *TR_NAME[TR_COUNT] = { "PASS", "RTL_BUG", "SILICON_ONLY", "MODEL_MISMATCH", "DIVERGENT", "INFRA" };
static const char *RES_NAME[3] = { "PASS", "FAIL", "INFRA" };

/* C1: pre(모델/RTL) 결과와 post(실리콘) 결과 → 판정. sig = 첫 실패 문자열 (같은 증상인지 비교) */
static triage_t triage(result_t pre, const char *pre_sig, result_t post, const char *post_sig) {
    if (pre == R_INFRA || post == R_INFRA) return TR_INFRA;
    if (pre == R_PASS && post == R_PASS) return TR_PASS;
    if (pre == R_PASS) return TR_SILICON_ONLY;
    if (post == R_PASS) return TR_MODEL_MISMATCH;
    return strcmp(pre_sig, post_sig) == 0 ? TR_RTL_BUG : TR_DIVERGENT;
}

typedef struct {
    result_t pre[NTESTS], post[NTESTS];
    tres_t   pre_r[NTESTS], post_r[NTESTS];
    triage_t tri[NTESTS];
    int      counts[TR_COUNT];
} campaign_t;
/* C2: 스위트 전체를 pre 백엔드에, 그다음 post 백엔드에 돌리고 테스트마다 triage, 판정별 개수 집계 */
static void run_campaign(const bus_t *pre, const bus_t *post, uint32_t base, campaign_t *c) {
    memset(c, 0, sizeof *c);
    bus_attach(pre, base);
    for (int i = 0; i < NTESTS; i++) c->pre[i] = run_test(&SUITE[i], &c->pre_r[i]);
    bus_attach(post, base);
    for (int i = 0; i < NTESTS; i++) c->post[i] = run_test(&SUITE[i], &c->post_r[i]);
    for (int i = 0; i < NTESTS; i++) {
        c->tri[i] = triage(c->pre[i], c->pre_r[i].first, c->post[i], c->post_r[i].first);
        c->counts[c->tri[i]]++;
    }
}
static void print_campaign(const char *title, const campaign_t *c) {   /* 주어진 코드 */
    printf("== %s\n", title);
    for (int i = 0; i < NTESTS; i++) {
        printf("  %-12s pre=%-5s post=%-5s -> %-14s", SUITE[i].name, RES_NAME[c->pre[i]],
               RES_NAME[c->post[i]], TR_NAME[c->tri[i]]);
        if (c->post[i] == R_FAIL) printf(" post: %s", c->post_r[i].first);
        if (c->pre[i] == R_FAIL && c->tri[i] != TR_RTL_BUG) printf(" pre: %s", c->pre_r[i].first);
        printf("\n");
    }
}

/* ======================= 하네스 ======================= */
static mock_t chip_pre, chip_post;
static target_t tgt;
static jtag_t jt;
static const bus_t MOCK_PRE  = { "mock",  mock_read32, mock_write32, &chip_pre };
static const bus_t JTAG_POST = { "jtag",  jtag_read32, jtag_write32, &jt };
static void setup(const fault_t *pre_f, const fault_t *post_f, int drop_after) {
    mock_init(&chip_pre, pre_f);
    mock_init(&chip_post, post_f);
    tgt = (target_t){ &chip_post, drop_after, 0 };
    jt = (jtag_t){ target_xfer, &tgt, "", 0 };
}
static fault_t no_fault(void) { fault_t f; memset(&f, 0, sizeof f); f.w1c_as_rw = -1; return f; }


/* ======================= 연습 1: 읽기 재시도 decorator ======================= */
/* 다른 bus_t 를 감싸는 bus_t. TIMEOUT 난 읽기만 max_retry 번까지 다시 시도한다.
 * 쓰기는 재시도하지 않는다 — 명령이 실제로 반영됐는지 모르기 때문 (W1S/W1C/FIFO push 는 두 번 먹으면 안 된다). */
typedef struct { const bus_t *inner; int max_retry; int retries; } retry_t;
static bus_status_t retry_read32(void *ctx, uint32_t addr, uint32_t *val) {
    retry_t *r = ctx;
    bus_status_t s = r->inner->read32(r->inner->ctx, addr, val);
    for (int k = 0; s == BUS_ERR_TIMEOUT && k < r->max_retry; k++) {
        r->retries++;
        s = r->inner->read32(r->inner->ctx, addr, val);
    }
    return s;
}
static bus_status_t retry_write32(void *ctx, uint32_t addr, uint32_t val) {
    retry_t *r = ctx;
    return r->inner->write32(r->inner->ctx, addr, val);
}
/* every 번째 읽기마다, 타깃이 읽기를 *실행한 뒤* 응답이 유실되는 transport */
typedef struct { target_t *t; int every; int nread; } flaky_t;
static int flaky_xfer(void *tctx, const char *cmd, char *resp, size_t cap) {
    flaky_t *f = tctx;
    int rc = target_xfer(f->t, cmd, resp, cap);
    if (rc == 0 && strncmp(cmd, "mdw", 3) == 0 && ++f->nread % f->every == 0) return -1;
    return rc;
}

/* ======================= 연습 2: shift-left 격차 ======================= */
/* 실제 발견 단계 found 가 가장 이른 가능 단계보다 몇 단계 늦었나. found 가 물리적으로 불가능한 단계면 -1 */
static int escape_levels(const bug_t *b, stage_t found) {
    stage_t e = earliest_stage(b);
    if (e == ST_NONE || found < e || found >= ST_COUNT) return -1;
    return (int)found - (int)e;
}

/* ======================= 연습 3: 여러 유닛에서 재현 ======================= */
typedef enum { UV_NOT_REPRODUCED, UV_UNIT_SPECIFIC, UV_DESIGN } unit_verdict_t;
/* INFRA 유닛은 판단에서 뺀다. 유효 유닛 전부 FAIL → 설계, 일부 → 유닛 특이, 0 → 재현 안 됨 */
static unit_verdict_t unit_verdict(const result_t *res, int n) {
    int valid = 0, fail = 0;
    for (int i = 0; i < n; i++) {
        if (res[i] == R_INFRA) continue;
        valid++;
        if (res[i] == R_FAIL) fail++;
    }
    if (fail == 0) return UV_NOT_REPRODUCED;
    return fail == valid ? UV_DESIGN : UV_UNIT_SPECIFIC;
}
/* ======================= 연습 4: 플랫폼별 기대값 ======================= */
typedef struct { int reg; uint32_t reset; } override_t;
typedef struct { const char *name; const override_t *ov; int nov; } platform_t;
static const override_t FPGA_OV[] = { { R_ID, 0x51530AF1u } };    /* FPGA 빌드는 ID 하위 바이트에 빌드 태그 */
static const platform_t PLAT_SILICON = { "silicon", NULL, 0 };
static const platform_t PLAT_FPGA    = { "fpga", FPGA_OV, 1 };
static const platform_t *g_plat = &PLAT_SILICON;
static uint32_t expected_reset(const platform_t *p, int reg) {
    for (int k = 0; k < p->nov; k++) if (p->ov[k].reg == reg) return p->ov[k].reset;
    return QSPI_REGS[reg].reset;
}
static void t_reset_plat(tres_t *r) {
    for (int i = 0; i < NREGS; i++) {
        const reg_desc_t *d = &QSPI_REGS[i]; uint32_t v, care = ~(d->wo | d->vol);
        if (!rd(r, d, &v)) return;
        expect(r, d, expected_reset(g_plat, i) & care, v & care);
    }
}

int main(void) {
    tres_t r;
    /* ---- 연습 1 ---- */
    {
        fault_t nf = no_fault();
        setup(&nf, &nf, -1);
        flaky_t fl = { &tgt, 5, 0 };
        jtag_t fj = { flaky_xfer, &fl, "", 0 };
        const bus_t FLAKY = { "jtag-flaky", jtag_read32, jtag_write32, &fj };
        bus_attach(&FLAKY, QSPI0_BASE);
        CHECK(run_test(&SUITE[0], &r) == R_INFRA);               /* 재시도 없으면 5번째 읽기에서 INFRA */

        setup(&nf, &nf, -1);
        fl = (flaky_t){ &tgt, 5, 0 };
        retry_t rt = { &FLAKY, 2, 0 };
        const bus_t RETRY = { "retry(jtag-flaky)", retry_read32, retry_write32, &rt };
        bus_attach(&RETRY, QSPI0_BASE);
        int ok = 0;
        for (int i = 0; i < NTESTS; i++) ok += run_test(&SUITE[i], &r) == R_PASS;
        CHECK(ok == NTESTS);
        CHECK(rt.retries > 0 && rt.retries == fl.nread / 5);    /* 유실된 응답 수만큼 재시도 */
        printf("ex1: retries=%d over %d mdw commands\n", rt.retries, fl.nread);
    }
    /* ---- 연습 2 ---- */
    CHECK(escape_levels(&BUGS[1], ST_SILICON) == 3);              /* 리셋값 오류가 실리콘에서 발견: 3단계 늦음 */
    CHECK(escape_levels(&BUGS[12], ST_SILICON) == 0);             /* PLL 저온: 실리콘이 최선 */
    CHECK(escape_levels(&BUGS[12], ST_SIM) == -1);                /* SIM 에서 "발견" 은 분류 오류 */
    CHECK(escape_levels(&BUGS[5], ST_SILICON) == 2);              /* 아비터 starvation: EMU 에서 잡았어야 */
    /* ---- 연습 3 ---- */
    {
        fault_t u[3] = { no_fault(), no_fault(), no_fault() };
        u[1].stuck1[R_SCRATCH] = 1u << 13;                        /* 유닛 1 만 stuck-at */
        result_t res[3]; campaign_t c; fault_t clean = no_fault();
        for (int i = 0; i < 3; i++) {                             /* 본문 캠페인을 유닛마다 */
            setup(&clean, &u[i], -1);
            run_campaign(&MOCK_PRE, &JTAG_POST, QSPI0_BASE, &c);
            res[i] = c.post[1];                                   /* rw_pattern */
            if (i == 1) print_campaign("unit 1", &c);
        }
        CHECK(res[0] == R_PASS && res[1] == R_FAIL && res[2] == R_PASS);
        CHECK(unit_verdict(res, 3) == UV_UNIT_SPECIFIC);
        for (int i = 0; i < 3; i++) {
            u[i].reset_xor[R_CTRL] = 0x00000200u;                 /* 모든 유닛의 tie-off */
            setup(&clean, &u[i], -1);
            run_campaign(&MOCK_PRE, &JTAG_POST, QSPI0_BASE, &c);
            res[i] = c.post[0];                                   /* reset_value */
        }
        CHECK(unit_verdict(res, 3) == UV_DESIGN);
        result_t mixed[3] = { R_FAIL, R_INFRA, R_FAIL };
        CHECK(unit_verdict(mixed, 3) == UV_DESIGN);
        result_t none[2] = { R_PASS, R_INFRA };
        CHECK(unit_verdict(none, 2) == UV_NOT_REPRODUCED);
    }
    /* ---- 연습 4 ---- */
    {
        const test_t TR = { "reset_value(plat)", t_reset_plat };
        fault_t fpga = no_fault(); fpga.reset_xor[R_ID] = 0x51530AF1u ^ 0x51530A01u;
        mock_t chip; mock_init(&chip, &fpga);
        const bus_t B = { "mock-fpga", mock_read32, mock_write32, &chip };
        g_plat = &PLAT_FPGA; bus_attach(&B, QSPI0_BASE);
        CHECK(run_test(&TR, &r) == R_PASS);
        mock_init(&chip, &fpga);
        g_plat = &PLAT_SILICON; bus_attach(&B, QSPI0_BASE);
        CHECK(run_test(&TR, &r) == R_FAIL &&
              strcmp(r.first, "reg=ID off=0x000 exp=0x51530A01 act=0x51530AF1 diff=0x000000F0 bits=[4,5,6,7]") == 0);
        mock_init(&chip, NULL);
        g_plat = &PLAT_FPGA; bus_attach(&B, QSPI0_BASE);
        CHECK(run_test(&TR, &r) == R_FAIL);                       /* 실리콘을 FPGA 기대값으로 → 잘못된 플랫폼 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
