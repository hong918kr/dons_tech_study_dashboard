/* SIV-A11 클럭·전원 도메인 검증
 *
 * 가상의 SoC "SENS-A" 의 도메인(ISP = 전원 스위치 + retention, NPU = 전원 스위치만, PERI = 클럭 게이트만) mock 위에서
 *   - 클럭이 꺼진/전원이 꺼진 도메인 접근이 정해진 버스 에러로 끝나는가
 *   - 전원 복귀 뒤 non-retention 은 reset 값, retention 레지스터는 저장값인가
 *   - isolation 이 켜진 동안 AON 쪽 포트가 clamp 값이고, 풀린 뒤에는 정상 값인가
 *   - 클럭 enable 직후 N cycle 동안의 첫 접근이 안전한가
 * 를 검사하고, 시퀀스 스텝의 순열 전체를 물리 모델 + safety oracle 로 돌려 안전한 순서를 전수 조사한다.
 * 시퀀스(대표값): 내림 = QUIESCE → CLK_OFF → ISO_ON → SAVE → RST_ON → PWR_OFF
 *                 올림 = PWR_ON → RST_OFF → RESTORE → CLK_ON(+settle) → ISO_OFF
 * 실제 순서는 SoC 전원 스펙 / UPF(IEEE 1801) power intent 가 정한다. 설계마다 다르다.
 * mock 레지스터 접근은 SIV-B02 를 축약, poll-with-timeout 은 SIV-B09 를 축약해 넣었다 (가짜 시계, 실제 sleep 없음).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ================= 도메인 / 레지스터 정의 ================= */
enum { D_AON, D_ISP, D_NPU, D_PERI, NDOM };
typedef struct { const char *name; bool has_pwr; bool has_ret; uint32_t id; } dom_cfg_t;
static const dom_cfg_t DOM[NDOM] = {
    {"AON",  false, false, 0x10000000u},
    {"ISP",  true,  true,  0x15000011u},
    {"NPU",  true,  false, 0x1A000021u},
    {"PERI", false, false, 0x1C000031u},
};
enum { R_ID, R_CTX, R_CFG, R_TUNE, NREG };
typedef struct { const char *name; uint32_t off, rst; bool ret; } reg_cfg_t;
static const reg_cfg_t REG[NREG] = {          /* ret = retention 플롭 (ISP 에서만 의미) */
    {"ID",   0x00u, 0u,          false},      /* RO, reset 값 = 도메인 id */
    {"CTX",  0x04u, 0xA5A50000u, false},      /* RW, non-retention */
    {"CFG",  0x08u, 0x00000010u, true},       /* RW, retention. [7:0] 이 AON 으로 가는 출력 포트 */
    {"TUNE", 0x0Cu, 0x000000FFu, true},       /* RW, retention */
};
#define BAD_DATA  0xDEADBEEFu
#define CLAMP_VAL 0x00u
#define T_SETTLE  4          /* 대표값: CLK_ON 뒤 첫 접근까지 필요한 cycle */
#define T_RAMP    8          /* 대표값: PWR_ON 뒤 rail good 까지 cycle */
#define TIMEOUT   64

typedef enum { RESP_OK, RESP_SLVERR, RESP_DECERR, RESP_HANG } resp_t;
static const char *const RESP_NAME[] = {"OKAY", "SLVERR", "DECERR", "HANG"};

typedef enum {
    S_QUIESCE, S_CLK_OFF, S_ISO_ON, S_SAVE, S_RST_ON, S_PWR_OFF,   /* 내림 */
    S_PWR_ON, S_RST_OFF, S_RESTORE, S_CLK_ON, S_ISO_OFF, S__N      /* 올림 */
} step_t;
static const char *const STEP_NAME[S__N] = {"QUIESCE", "CLK_OFF", "ISO_ON", "SAVE", "RST_ON", "PWR_OFF",
                                            "PWR_ON", "RST_OFF", "RESTORE", "CLK_ON", "ISO_OFF"};

/* mock 에 넣는 하드웨어 결함 */
typedef struct {
    bool gate_ok, gate_hang, gate_wr_leak, off_slverr;
    bool ret_skip;
    uint32_t ret_mask; bool ret_pol;     /* SAVE 때 CFG 의 mask 비트가 ret_pol 로 고정 */
    bool ctx_retained, rst_skip;
    bool clamp_ff, iso_no_release, iso_early_hw;
    bool settle_no_err, ack_early;
    bool cross_gate;                     /* ISP 의 CLK_OFF 가 NPU 클럭도 끈다 */
} fault_t;

/* 위반 종류 (물리 모델의 ground truth) */
enum { V_UNQUIESCED, V_X_CROSS, V_ISO_GLITCH, V_SAVE_RACE, V_RECOVERY, V_RESTORE_RACE, V_POWERUP_X,
       V_RET_LOST, V_RST_BAD, V_PORT_BAD, V__N };
static const char *const V_NAME[V__N] = {"UNQUIESCED", "X_CROSS", "ISO_GLITCH", "SAVE_RACE", "RECOVERY",
                                         "RESTORE_RACE", "POWERUP_X", "RET_LOST", "RST_BAD", "PORT_BAD"};

typedef struct {
    bool pwr, pwr_req, iso, clk, rst;
    int pending;                         /* 진행 중 트랜잭션 */
    uint32_t reg[NREG], shadow[NREG];
    unsigned xmask;                      /* 값이 정의되지 않은 레지스터 비트맵 */
    uint64_t clk_on_at, pwr_on_at;
} dom_t;
typedef struct { uint32_t viol; uint8_t port_last; bool port_valid; } mon_t;
static struct { dom_t dom[NDOM]; mon_t mon[NDOM]; uint64_t now; uint32_t rng; uint8_t port_mask; } g;
static fault_t g_f;

static uint32_t xr32(void) { uint32_t x = g.rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g.rng = x; }
static void viol(int d, int v) { g.mon[d].viol |= 1u << v; }

static void soc_init(uint32_t seed) {
    memset(&g, 0, sizeof g);
    g.rng = seed ? seed : 1u; g.now = 1000; g.port_mask = 0xFFu;
    for (int d = 0; d < NDOM; d++) {
        dom_t *D = &g.dom[d];
        D->pwr = D->pwr_req = D->clk = true;
        for (int r = 0; r < NREG; r++) D->reg[r] = D->shadow[r] = r == R_ID ? DOM[d].id : REG[r].rst;
    }
}
static void soc_tick(int n) {
    while (n-- > 0) {
        g.now++;
        for (int d = 0; d < NDOM; d++) {
            dom_t *D = &g.dom[d];
            if (D->pwr_req && !D->pwr && g.now >= D->pwr_on_at + T_RAMP) {       /* rail good */
                D->pwr = true;
                for (int r = 0; r < NREG; r++) {
                    if (r == R_CTX && g_f.ctx_retained) continue;
                    if (D->rst) { D->reg[r] = r == R_ID ? DOM[d].id : REG[r].rst; D->xmask &= ~(1u << r); }
                    else if (r == R_ID) D->reg[r] = DOM[d].id;
                    else { D->reg[r] = xr32(); D->xmask |= 1u << r; viol(d, V_POWERUP_X); }
                }
            }
        }
    }
}
static void need_idle(int d) { if (g.dom[d].pending) viol(d, V_UNQUIESCED); }

/* AON 쪽에서 보이는 포트 값 */
static void port_eval(int d, uint8_t *v, bool *x) {
    const dom_t *D = &g.dom[d];
    *x = false;
    if (D->iso) { *v = g_f.clamp_ff ? 0xFFu : CLAMP_VAL; return; }
    if (!D->pwr || (D->xmask & (1u << R_CFG))) { *x = true; *v = (uint8_t)xr32(); return; }
    *v = (uint8_t)D->reg[R_CFG];
}
static uint8_t aon_port_read(int d) {
    uint8_t v; bool x;
    port_eval(d, &v, &x);
    return (uint8_t)(v & g.port_mask);
}
static void mon_after(int d) {
    mon_t *m = &g.mon[d];
    uint8_t v; bool x;
    if (g.dom[d].iso) { m->port_valid = false; return; }
    port_eval(d, &v, &x);
    if (x) viol(d, V_X_CROSS);
    else if (m->port_valid && v != m->port_last) viol(d, V_ISO_GLITCH);
    m->port_last = v; m->port_valid = !x;
}
static void mon_begin(int d) {
    uint8_t v; bool x;
    g.mon[d].viol = 0; g.mon[d].port_valid = false;
    port_eval(d, &v, &x);
    if (!g.dom[d].iso && !x) { g.mon[d].port_last = v; g.mon[d].port_valid = true; }
}

/* PMU: 스텝 하나를 적용한다 (대기는 pd_step 이 한다) */
static void pmu_apply(int d, step_t s) {
    dom_t *D = &g.dom[d];
    switch (s) {
    case S_QUIESCE: D->pending = 0; break;
    case S_CLK_OFF:
        need_idle(d); D->clk = false;
        if (g_f.cross_gate && d == D_ISP) g.dom[D_NPU].clk = false;
        break;
    case S_ISO_ON: need_idle(d); D->iso = true; break;
    case S_SAVE:
        need_idle(d);
        if (g_f.ret_skip || !D->pwr) break;
        if (D->clk) {                                                         /* 클럭이 도는 중: 잘못 캡처 */
            viol(d, V_SAVE_RACE);
            for (int r = 0; r < NREG; r++) if (REG[r].ret) D->shadow[r] = D->reg[r] ^ (xr32() | 1u);
        } else {
            for (int r = 0; r < NREG; r++) if (REG[r].ret) D->shadow[r] = D->reg[r];
            if (g_f.ret_mask)
                D->shadow[R_CFG] = g_f.ret_pol ? D->shadow[R_CFG] | g_f.ret_mask : D->shadow[R_CFG] & ~g_f.ret_mask;
        }
        break;
    case S_RST_ON:
        need_idle(d);
        if (g_f.rst_skip) break;
        D->rst = true;
        if (D->pwr) for (int r = 0; r < NREG; r++) {
            if (r == R_CTX && g_f.ctx_retained) continue;
            D->reg[r] = r == R_ID ? DOM[d].id : REG[r].rst;
            D->xmask &= ~(1u << r);
        }
        break;
    case S_PWR_OFF:
        need_idle(d);
        D->pwr = D->pwr_req = false;
        for (int r = 0; r < NREG; r++) {
            if (r == R_CTX && g_f.ctx_retained) continue;
            D->reg[r] = xr32(); D->xmask |= 1u << r;
        }
        break;
    case S_PWR_ON: D->pwr_req = true; D->pwr_on_at = g.now; break;
    case S_RST_OFF:
        if (D->clk) { viol(d, V_RECOVERY); D->reg[R_CTX] = xr32(); D->xmask |= 1u << R_CTX; } /* recovery/removal 위반 */
        D->rst = false;
        if (g_f.iso_early_hw) D->iso = false;
        break;
    case S_RESTORE:
        if (!D->pwr || D->rst) break;                                         /* 전원 없음 / reset 이 우선 */
        if (D->clk) {
            viol(d, V_RESTORE_RACE);
            for (int r = 0; r < NREG; r++) if (REG[r].ret) { D->reg[r] = xr32(); D->xmask |= 1u << r; }
        } else {
            for (int r = 0; r < NREG; r++) if (REG[r].ret) { D->reg[r] = D->shadow[r]; D->xmask &= ~(1u << r); }
        }
        break;
    case S_CLK_ON: D->clk = true; D->clk_on_at = g.now; break;
    case S_ISO_OFF: if (!g_f.iso_no_release) D->iso = false; break;
    default: break;
    }
    mon_after(d);
}
#define STAT_PGOOD   (1u << 0)
#define STAT_CLK_ACK (1u << 1)
#define STAT_ISO     (1u << 2)
#define STAT_RST     (1u << 3)
static uint32_t pmu_status(int d) {
    const dom_t *D = &g.dom[d];
    uint32_t s = 0;
    if (D->pwr) s |= STAT_PGOOD;
    if (D->clk && g.now - D->clk_on_at >= (uint64_t)(g_f.ack_early ? 1 : T_SETTLE)) s |= STAT_CLK_ACK;
    if (D->iso) s |= STAT_ISO;
    if (D->rst) s |= STAT_RST;
    return s;
}
static bool pmu_wait(int d, uint32_t bit, int timeout) {
    for (int i = 0; i <= timeout; i++) {
        if (pmu_status(d) & bit) return true;
        soc_tick(1);
    }
    return false;
}
/* 스텝 적용 + 해당 ack 대기 */
static bool pd_step(int d, step_t s) {
    pmu_apply(d, s);
    if (s == S_PWR_ON) return pmu_wait(d, STAT_PGOOD, TIMEOUT);
    if (s == S_CLK_ON) return pmu_wait(d, STAT_CLK_ACK, TIMEOUT);
    return true;
}

/* 버스 */
static resp_t access_state(int d, bool *window) {
    const dom_t *D = &g.dom[d];
    *window = false;
    if (!D->pwr) return g_f.off_slverr ? RESP_SLVERR : RESP_DECERR;
    if (D->rst || !D->clk) return g_f.gate_hang ? RESP_HANG : RESP_SLVERR;
    if (g.now - D->clk_on_at < (uint64_t)T_SETTLE) { *window = true; return RESP_SLVERR; }
    return RESP_OK;
}
static resp_t bus_read(int d, uint32_t off, uint32_t *v) {
    bool w; resp_t r = access_state(d, &w);
    int idx = (int)(off / 4);
    *v = BAD_DATA;
    if (r == RESP_SLVERR && !w && g_f.gate_ok) { r = RESP_OK; *v = g.dom[d].reg[idx]; }
    else if (r == RESP_SLVERR && w && g_f.settle_no_err) { r = RESP_OK; *v = g.dom[d].reg[idx] ^ 0x5A5A5A5Au; }
    else if (r == RESP_OK) *v = g.dom[d].reg[idx];
    soc_tick(1);
    return r;
}
static resp_t bus_write(int d, uint32_t off, uint32_t val) {
    bool w; resp_t r = access_state(d, &w);
    int idx = (int)(off / 4);
    dom_t *D = &g.dom[d];
    if (r == RESP_OK) { if (idx != R_ID) { D->reg[idx] = val; D->xmask &= ~(1u << idx); } }
    else if (r == RESP_SLVERR && !w && g_f.gate_wr_leak && idx != R_ID) D->reg[idx] = val;
    else if (r == RESP_SLVERR && !w && g_f.gate_ok) r = RESP_OK;
    else if (r == RESP_SLVERR && w && g_f.settle_no_err) { r = RESP_OK; if (idx != R_ID) D->reg[idx] = val ^ 0x5A5A5A5Au; }
    soc_tick(1);
    return r;
}

/* 대표 시퀀스 */
static const step_t SEQ_DOWN_RET[] = {S_QUIESCE, S_CLK_OFF, S_ISO_ON, S_SAVE, S_RST_ON, S_PWR_OFF};
static const step_t SEQ_UP_RET[]   = {S_PWR_ON, S_RST_OFF, S_RESTORE, S_CLK_ON, S_ISO_OFF};
static const step_t SEQ_DOWN_NR[]  = {S_QUIESCE, S_CLK_OFF, S_ISO_ON, S_RST_ON, S_PWR_OFF};
static const step_t SEQ_UP_NR[]    = {S_PWR_ON, S_RST_OFF, S_CLK_ON, S_ISO_OFF};
typedef struct { const step_t *down; int nd; const step_t *up; int nu; } seq_t;
#define ALEN(a) ((int)(sizeof(a) / sizeof((a)[0])))
static const seq_t SEQ_RET_REP = {SEQ_DOWN_RET, ALEN(SEQ_DOWN_RET), SEQ_UP_RET, ALEN(SEQ_UP_RET)};
static const seq_t SEQ_NR_REP  = {SEQ_DOWN_NR, ALEN(SEQ_DOWN_NR), SEQ_UP_NR, ALEN(SEQ_UP_NR)};
static const seq_t *g_seq_ret = &SEQ_RET_REP;   /* ISP 가 쓸 시퀀스 (결함 시퀀스로 교체 가능) */
static const seq_t *seq_of(int d) { return DOM[d].has_ret ? g_seq_ret : &SEQ_NR_REP; }
static bool run_steps(int d, const step_t *s, int n) {
    for (int i = 0; i < n; i++) if (!pd_step(d, s[i])) return false;
    return true;
}

/* ================= 실패 기록 ================= */
static unsigned popc32(uint32_t x) { unsigned n = 0; for (; x; x &= x - 1) n++; return n; }
typedef enum { K_NONE, K_PRE, K_ACK_TIMEOUT, K_GATED_RESP, K_GATED_DATA, K_GATED_WR_RESP, K_GATED_WR_LEAK,
               K_OFF_RESP, K_OFF_DATA, K_ID, K_CTX_RESET, K_RET, K_NORET_RESET,
               K_PORT_HOLD, K_PORT_CLAMP, K_PORT_OPEN, K_ACK, K_SETTLE_RESP, K_FIRST_RD, K_FIRST_WR, K_CROSS, K__N } chk_t;
static const char *const K_NAME[K__N] = {"NONE", "PRE", "ACK_TIMEOUT", "GATED_RESP", "GATED_DATA", "GATED_WR_RESP",
    "GATED_WR_LEAK", "OFF_RESP", "OFF_DATA", "ID", "CTX_RESET", "RET", "NORET_RESET", "PORT_HOLD", "PORT_CLAMP",
    "PORT_OPEN", "ACK", "SETTLE_RESP", "FIRST_RD", "FIRST_WR", "CROSS"};
typedef struct {
    const char *test; int dom; chk_t step; const char *reg; uint32_t off, exp, act;
    int k; bool is_resp; bool iso_on;
} fail_t;
static bool fail_(fail_t *f, chk_t step, const char *reg, uint32_t off, uint32_t exp, uint32_t act) {
    f->step = step; f->reg = reg; f->off = off; f->exp = exp; f->act = act;
    f->is_resp = false; f->iso_on = false;
    return false;
}
static bool fail_resp(fail_t *f, chk_t step, const char *reg, uint32_t off, resp_t exp, resp_t act) {
    fail_(f, step, reg, off, (uint32_t)exp, (uint32_t)act);
    f->is_resp = true;
    return false;
}
static void print_fail(const fail_t *f) {
    printf("FAIL %s %s step=%s", f->dom >= 0 ? DOM[f->dom].name : "?", f->test ? f->test : "?", K_NAME[f->step]);
    if (f->step == K_FIRST_RD || f->step == K_FIRST_WR || f->step == K_SETTLE_RESP || f->step == K_ACK) printf(" k=%d", f->k);
    if (f->reg) printf(" reg=%s off=0x%03X", f->reg, f->off);
    if (f->is_resp) printf(" exp=%s act=%s", RESP_NAME[f->exp], RESP_NAME[f->act]);
    else {
        uint32_t diff = f->exp ^ f->act;
        printf(" exp=0x%08X act=0x%08X diff=0x%08X bits=[", f->exp, f->act, diff);
        for (int b = 0, c = 0, n = 0; b < 32; b++) {
            if (!(diff >> b & 1)) continue;
            if (n < 6) printf("%s%d", c++ ? "," : "", b); else if (n == 6) printf(",+%d", (int)popc32(diff) - 6);
            n++;
        }
        printf("]");
    }
}

static int g_total, g_pass;
#define CHECK(e) do { g_total++; if (e) g_pass++; else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

/* ================= Part A ================= */
static bool check_gated_access(int d, fail_t *f) {
    const uint32_t pat = 0x1234ABCDu;
    uint32_t v;
    resp_t r = bus_write(d, REG[R_CTX].off, pat);
    if (r != RESP_OK) return fail_resp(f, K_PRE, "CTX", REG[R_CTX].off, RESP_OK, r);
    pmu_apply(d, S_CLK_OFF);
    for (int i = 0; i < NREG; i++) {                       /* 게이트된 동안 모든 읽기 = SLVERR + BAD_DATA */
        r = bus_read(d, REG[i].off, &v);
        if (r != RESP_SLVERR) return fail_resp(f, K_GATED_RESP, REG[i].name, REG[i].off, RESP_SLVERR, r);
        if (v != BAD_DATA) return fail_(f, K_GATED_DATA, REG[i].name, REG[i].off, BAD_DATA, v);
    }
    r = bus_write(d, REG[R_CTX].off, ~pat);                /* 쓰기도 에러, 그리고 값이 들어가면 안 된다 */
    if (r != RESP_SLVERR) return fail_resp(f, K_GATED_WR_RESP, "CTX", REG[R_CTX].off, RESP_SLVERR, r);
    if (!pd_step(d, S_CLK_ON)) return fail_(f, K_ACK_TIMEOUT, "PMU", 0, STAT_CLK_ACK, pmu_status(d));
    r = bus_read(d, REG[R_CTX].off, &v);
    if (r != RESP_OK) return fail_resp(f, K_PRE, "CTX", REG[R_CTX].off, RESP_OK, r);
    if (v != pat) return fail_(f, K_GATED_WR_LEAK, "CTX", REG[R_CTX].off, pat, v);
    return true;
}
static bool check_power_cycle(int d, uint32_t cfg_pat, uint32_t tune_pat, fail_t *f) {
    const uint32_t ctx_pat = 0x13572468u;
    const seq_t *q = seq_of(d);
    uint32_t v;
    resp_t r;
    uint32_t ids[][2] = {{R_CTX, ctx_pat}, {R_CFG, cfg_pat}, {R_TUNE, tune_pat}};
    for (int i = 0; i < 3; i++) {
        r = bus_write(d, REG[ids[i][0]].off, ids[i][1]);
        if (r != RESP_OK) return fail_resp(f, K_PRE, REG[ids[i][0]].name, REG[ids[i][0]].off, RESP_OK, r);
    }
    if (!run_steps(d, q->down, q->nd)) return fail_(f, K_ACK_TIMEOUT, "PMU", 0, 1, 0);
    r = bus_read(d, REG[R_ID].off, &v);                    /* 전원이 꺼진 동안: DECERR, 쓰기도 마찬가지 */
    if (r != RESP_DECERR) return fail_resp(f, K_OFF_RESP, "ID", REG[R_ID].off, RESP_DECERR, r);
    if (v != BAD_DATA) return fail_(f, K_OFF_DATA, "ID", REG[R_ID].off, BAD_DATA, v);
    r = bus_write(d, REG[R_CFG].off, 0x11111111u);
    if (r != RESP_DECERR) return fail_resp(f, K_OFF_RESP, "CFG", REG[R_CFG].off, RESP_DECERR, r);
    if (!run_steps(d, q->up, q->nu)) return fail_(f, K_ACK_TIMEOUT, "PMU", 0, 1, 0);
    for (int i = 0; i < NREG; i++) {
        uint32_t exp;
        chk_t k;
        r = bus_read(d, REG[i].off, &v);
        if (r != RESP_OK) return fail_resp(f, K_PRE, REG[i].name, REG[i].off, RESP_OK, r);
        if (i == R_ID) { exp = DOM[d].id; k = K_ID; }
        else if (i == R_CTX) { exp = REG[i].rst; k = K_CTX_RESET; }
        else if (DOM[d].has_ret) { exp = i == R_CFG ? cfg_pat : tune_pat; k = K_RET; }
        else { exp = REG[i].rst; k = K_NORET_RESET; }
        if (v != exp) return fail_(f, k, REG[i].name, REG[i].off, exp, v);
    }
    return true;
}
/* 포트 샘플: isolation 이 켜져 있으면 clamp, 꺼져 있으면 기능 값이어야 한다 */
static bool check_iso_trace(int d, uint32_t cfg_pat, fail_t *f) {
    const seq_t *q = seq_of(d);
    uint32_t v;
    resp_t r = bus_write(d, REG[R_CFG].off, cfg_pat);
    if (r != RESP_OK) return fail_resp(f, K_PRE, "CFG", REG[R_CFG].off, RESP_OK, r);
    uint8_t hold = (uint8_t)cfg_pat;
    uint8_t final = DOM[d].has_ret ? hold : (uint8_t)REG[R_CFG].rst;
    uint8_t p = aon_port_read(d);
    if (p != hold) return fail_(f, K_PORT_HOLD, "PORT", 0, hold, p);
    for (int i = 0; i < q->nd; i++) {                      /* 내림: ISO_ON 전까지 hold, 그 뒤 clamp */
        if (!pd_step(d, q->down[i])) return fail_(f, K_ACK_TIMEOUT, "PMU", 0, 1, 0);
        bool iso = pmu_status(d) & STAT_ISO;
        p = aon_port_read(d);
        uint8_t exp = iso ? CLAMP_VAL : hold;
        if (p != exp) { fail_(f, iso ? K_PORT_CLAMP : K_PORT_HOLD, "PORT", 0, exp, p); f->k = i; f->iso_on = iso; return false; }
    }
    bool released = false;
    for (int i = 0; i < q->nu; i++) {                      /* 올림: ISO_OFF 까지 clamp, 그 뒤 최종 값 */
        if (!pd_step(d, q->up[i])) return fail_(f, K_ACK_TIMEOUT, "PMU", 0, 1, 0);
        if (q->up[i] == S_ISO_OFF) released = true;
        bool iso = pmu_status(d) & STAT_ISO;
        p = aon_port_read(d);
        uint8_t exp = released ? final : CLAMP_VAL;
        if (p != exp) { fail_(f, released ? K_PORT_OPEN : K_PORT_CLAMP, "PORT", 0, exp, p); f->k = i; f->iso_on = iso; return false; }
    }
    r = bus_read(d, REG[R_ID].off, &v);
    if (r != RESP_OK) return fail_resp(f, K_PRE, "ID", REG[R_ID].off, RESP_OK, r);
    return true;
}
/* 클럭 enable 후 경과 k cycle 에서의 ack·읽기·쓰기 */
static bool check_first_access(int d, fail_t *f) {
    const uint32_t off = REG[R_CTX].off, pat = 0x600DF00Du;
    uint32_t v;
    resp_t r = bus_write(d, off, pat);
    if (r != RESP_OK) return fail_resp(f, K_PRE, "CTX", off, RESP_OK, r);
    for (int k = 0; k <= T_SETTLE + 1; k++) {
        bool settled = k >= T_SETTLE;
        f->k = k;
        pmu_apply(d, S_CLK_OFF); pmu_apply(d, S_CLK_ON);   /* 읽기 */
        soc_tick(k);
        bool ack = pmu_status(d) & STAT_CLK_ACK;
        if (ack != settled) return fail_(f, K_ACK, "PMU_STAT", 0, settled, ack);
        r = bus_read(d, off, &v);
        if (r != (settled ? RESP_OK : RESP_SLVERR))
            return fail_resp(f, K_SETTLE_RESP, "CTX", off, settled ? RESP_OK : RESP_SLVERR, r);
        if (settled ? v != pat : v != BAD_DATA) return fail_(f, K_FIRST_RD, "CTX", off, settled ? pat : BAD_DATA, v);
        pmu_apply(d, S_CLK_OFF); pmu_apply(d, S_CLK_ON);   /* 쓰기 */
        soc_tick(k);
        r = bus_write(d, off, ~pat);
        if (r != (settled ? RESP_OK : RESP_SLVERR))
            return fail_resp(f, K_SETTLE_RESP, "CTX", off, settled ? RESP_OK : RESP_SLVERR, r);
        soc_tick(T_SETTLE);
        r = bus_read(d, off, &v);
        if (r != RESP_OK) return fail_resp(f, K_FIRST_WR, "CTX", off, RESP_OK, r);
        if (v != (settled ? ~pat : pat)) return fail_(f, K_FIRST_WR, "CTX", off, settled ? ~pat : pat, v);
        if (settled && bus_write(d, off, pat) != RESP_OK) return fail_resp(f, K_FIRST_WR, "CTX", off, RESP_OK, RESP_SLVERR);
    }
    return true;
}
/* 주어진 코드: 다른 도메인의 전원 사이클이 이 도메인을 건드리지 않는지 */
static bool check_cross(int d, int other, fail_t *f) {
    uint32_t v;
    uint32_t pat = 0xC0FFEE00u ^ (uint32_t)d;
    if (bus_write(other, REG[R_CTX].off, pat) != RESP_OK) return fail_(f, K_PRE, "CTX", REG[R_CTX].off, 0, 1);
    const seq_t *q = seq_of(d);
    if (!run_steps(d, q->down, q->nd) || !run_steps(d, q->up, q->nu)) return fail_(f, K_ACK_TIMEOUT, "PMU", 0, 1, 0);
    resp_t r = bus_read(other, REG[R_CTX].off, &v);
    if (r != RESP_OK) return fail_resp(f, K_CROSS, "CTX", REG[R_CTX].off, RESP_OK, r);
    if (v != pat) return fail_(f, K_CROSS, "CTX", REG[R_CTX].off, pat, v);
    return true;
}

/* ================= Part B: 순서 전수 조사 ================= */
typedef struct { uint32_t viol; bool rb_bad; bool port_bad; } oracle_t;
/* 주어진 코드: ISP 왕복 1회. o->viol = 물리 모델 위반(ground truth), rb_bad = 최종 레지스터 읽기로 보이나, port_bad = 스텝마다
 * 포트를 샘플해 보이나 (둘 다 소프트웨어/AON 레지스터로 관측 가능한 것) */
#define ORACLE_CFG  0xA5A5C33Cu
#define ORACLE_TUNE 0x5A5A1234u
static void port_sample(int d, uint8_t func, bool *bad) {
    bool iso = pmu_status(d) & STAT_ISO;
    uint8_t exp = iso ? (uint8_t)(CLAMP_VAL & g.port_mask) : (uint8_t)(func & g.port_mask);
    if (aon_port_read(d) != exp) *bad = true;
}
static void oracle_run(const step_t *down, int nd, const step_t *up, int nu, uint32_t seed, uint8_t pmask, bool wait, oracle_t *o) {
    const int d = D_ISP;
    soc_init(seed);
    g.port_mask = pmask;
    (void)bus_write(d, REG[R_CFG].off, ORACLE_CFG);
    (void)bus_write(d, REG[R_TUNE].off, ORACLE_TUNE);
    (void)bus_write(d, REG[R_CTX].off, 0x13572468u);
    g.dom[d].pending = 1;
    mon_begin(d);
    o->port_bad = false;
    uint8_t func = (uint8_t)ORACLE_CFG;
    for (int i = 0; i < nd; i++) { if (wait) (void)pd_step(d, down[i]); else pmu_apply(d, down[i]); port_sample(d, func, &o->port_bad); }
    for (int i = 0; i < nu; i++) { if (wait) (void)pd_step(d, up[i]);   else pmu_apply(d, up[i]);   port_sample(d, func, &o->port_bad); }
    if (!wait) soc_tick(T_RAMP + T_SETTLE + 2);                       /* 시퀀스가 끝난 뒤 rail/clock 이 안정될 시간은 준다 */
    dom_t *D = &g.dom[d];
    uint32_t v;
    bool rb = false;
    if (bus_read(d, REG[R_ID].off, &v) != RESP_OK || v != DOM[d].id) rb = true;
    if (bus_read(d, REG[R_CTX].off, &v) != RESP_OK || v != REG[R_CTX].rst) rb = true;
    if (bus_read(d, REG[R_CFG].off, &v) != RESP_OK || v != ORACLE_CFG) rb = true;
    if (bus_read(d, REG[R_TUNE].off, &v) != RESP_OK || v != ORACLE_TUNE) rb = true;
    o->rb_bad = rb;
    if ((D->xmask & ((1u << R_CFG) | (1u << R_TUNE))) || D->reg[R_CFG] != ORACLE_CFG || D->reg[R_TUNE] != ORACLE_TUNE)
        viol(d, V_RET_LOST);
    if ((D->xmask & (1u << R_CTX)) || D->reg[R_CTX] != REG[R_CTX].rst) viol(d, V_RST_BAD);
    uint8_t pv; bool px;
    port_eval(d, &pv, &px);
    if (px || pv != (uint8_t)ORACLE_CFG) viol(d, V_PORT_BAD);
    o->viol = g.mon[d].viol;
}
static void oracle_roundtrip(const step_t *down, int nd, const step_t *up, int nu, uint32_t seed, uint8_t pmask, oracle_t *o) {
    oracle_run(down, nd, up, nu, seed, pmask, true, o);
}

#define MAX_STEPS 6
#define MAX_SAFE 32
typedef struct {
    int total, safe, seen_rb, seen_port, seen_either, only_monitor;  /* seen_* 는 위반 순서 중 관측되는 개수 */
    int hist[V__N];
    step_t safe_list[MAX_SAFE][MAX_STEPS];
    bool prec[MAX_STEPS][MAX_STEPS];     /* prec[i][j]: 모든 안전 순서에서 base[i] 가 base[j] 앞 */
} enum_t;

/* 사전식 다음 순열. 더 없으면 false */
static bool next_perm(int *a, int n) {
    int i = n - 2;
    while (i >= 0 && a[i] >= a[i + 1]) i--;
    if (i < 0) return false;
    int j = n - 1;
    while (a[j] <= a[i]) j--;
    int t = a[i]; a[i] = a[j]; a[j] = t;
    for (int l = i + 1, r = n - 1; l < r; l++, r--) { t = a[l]; a[l] = a[r]; a[r] = t; }
    return true;
}
/* base 의 모든 순열을 (is_down 이면 내림, 아니면 올림 자리에) 넣고, 다른 쪽은 대표 시퀀스로 왕복한다 */
static void enumerate_orders(const step_t *base, int n, bool is_down, uint8_t pmask, enum_t *e) {
    int idx[MAX_STEPS];
    memset(e, 0, sizeof *e);
    for (int i = 0; i < n; i++) idx[i] = i;
    for (int a = 0; a < n; a++) for (int b = 0; b < n; b++) e->prec[a][b] = true;
    do {
        step_t s[MAX_STEPS];
        oracle_t o;
        for (int i = 0; i < n; i++) s[i] = base[idx[i]];
        if (is_down) oracle_roundtrip(s, n, SEQ_RET_REP.up, SEQ_RET_REP.nu, 0x1234567u, pmask, &o);
        else oracle_roundtrip(SEQ_RET_REP.down, SEQ_RET_REP.nd, s, n, 0x1234567u, pmask, &o);
        e->total++;
        for (int v = 0; v < V__N; v++) if (o.viol >> v & 1) e->hist[v]++;
        if (o.viol == 0) {
            if (e->safe < MAX_SAFE) memcpy(e->safe_list[e->safe], s, sizeof s);
            e->safe++;
            for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) e->prec[idx[j]][idx[i]] = false;
        } else {
            e->seen_rb += o.rb_bad;
            e->seen_port += o.port_bad;
            e->seen_either += o.rb_bad || o.port_bad;
            e->only_monitor += !o.rb_bad && !o.port_bad;
        }
    } while (next_perm(idx, n));
}

/* ================= Part C: 캠페인 ================= */
enum { C_NONE, C_GATE_NO_ERR, C_GATE_HANG, C_GATE_WR_LEAK, C_OFF_RESP, C_ID, C_RST_KEPT, C_RST_CORRUPT, C_RET_LOST,
       C_RET_BIT, C_RET_CORRUPT, C_ISO_LATE, C_ISO_EARLY, C_ISO_STUCK, C_CLAMP_VALUE, C_ACK_EARLY,
       C_SETTLE_UNSAFE, C_CROSS_DOMAIN, C_ACK_TIMEOUT, C_PRECOND, C_OTHER, C__N };
static const char *const C_NAME[C__N] = {"NONE", "GATE_NO_ERR", "GATE_HANG", "GATE_WR_LEAK", "OFF_RESP", "ID", "RST_KEPT",
    "RST_CORRUPT", "RET_LOST", "RET_BIT", "RET_CORRUPT", "ISO_LATE", "ISO_EARLY", "ISO_STUCK", "CLAMP_VALUE",
    "ACK_EARLY", "SETTLE_UNSAFE", "CROSS_DOMAIN", "ACK_TIMEOUT", "PRECOND", "OTHER"};

static int classify(const fail_t *f) {
    switch (f->step) {
    case K_PRE: return C_PRECOND;                             /* 준비 단계 실패: 앞선 실패가 원인일 수 있다 */
    case K_ACK_TIMEOUT: return C_ACK_TIMEOUT;
    case K_GATED_RESP: return f->act == RESP_HANG ? C_GATE_HANG : C_GATE_NO_ERR;
    case K_GATED_DATA: case K_GATED_WR_RESP: return C_GATE_NO_ERR;
    case K_GATED_WR_LEAK: return C_GATE_WR_LEAK;
    case K_OFF_RESP: case K_OFF_DATA: return C_OFF_RESP;
    case K_ID: return C_ID;
    case K_CTX_RESET: return f->act == 0x13572468u ? C_RST_KEPT : C_RST_CORRUPT;   /* ctx_pat 이 살아 있으면 retention 처럼 동작 */
    case K_RET: {
        uint32_t rst = strcmp(f->reg, "CFG") == 0 ? REG[R_CFG].rst : REG[R_TUNE].rst;
        if (f->act == rst) return C_RET_LOST;                 /* reset 값으로 돌아옴 = SAVE/RESTORE 가 안 일어남 */
        if (popc32(f->exp ^ f->act) == 1) return C_RET_BIT;
        return C_RET_CORRUPT;
    }
    case K_NORET_RESET: return C_RST_CORRUPT;
    case K_PORT_HOLD: return C_ISO_LATE;                      /* isolation 이 아직 안 켜졌는데 포트가 흔들림 */
    case K_PORT_CLAMP: return f->iso_on ? C_CLAMP_VALUE : C_ISO_EARLY;
    case K_PORT_OPEN: return f->iso_on ? C_ISO_STUCK : C_ISO_EARLY;
    case K_ACK: return C_ACK_EARLY;
    case K_SETTLE_RESP: case K_FIRST_RD: case K_FIRST_WR: return C_SETTLE_UNSAFE;
    case K_CROSS: return C_CROSS_DOMAIN;
    default: return C_OTHER;
    }
}

#define CFG_A 0xA5A5C33Cu
#define TUNE_A 0x5A5A1234u
#define MAX_LOG 64
typedef struct { int runs, n; fail_t log[MAX_LOG]; } suite_t;
typedef enum { T_GATED, T_FIRST, T_PWR_A, T_PWR_B, T_ISO, T_CROSS } test_t;
static void run_one(suite_t *S, const fault_t *flt, const seq_t *isp, test_t t, int d, int other, uint32_t seed) {
    fail_t f;
    bool ok = false;
    memset(&f, 0, sizeof f);
    f.dom = d;
    soc_init(seed);
    g_f = *flt;
    g_seq_ret = isp ? isp : &SEQ_RET_REP;
    switch (t) {
    case T_GATED:  f.test = "gated"; ok = check_gated_access(d, &f); break;
    case T_FIRST:  f.test = "first"; ok = check_first_access(d, &f); break;
    case T_PWR_A:  f.test = "pwrcyc"; ok = check_power_cycle(d, CFG_A, TUNE_A, &f); break;
    case T_PWR_B:  f.test = "pwrcyc"; ok = check_power_cycle(d, ~CFG_A, ~TUNE_A, &f); break;
    case T_ISO:    f.test = "iso"; ok = check_iso_trace(d, CFG_A, &f); break;
    case T_CROSS:  f.test = "cross"; f.dom = other; ok = check_cross(d, other, &f); break;
    }
    g_seq_ret = &SEQ_RET_REP;
    memset(&g_f, 0, sizeof g_f);
    S->runs++;
    if (!ok && S->n < MAX_LOG) S->log[S->n++] = f;
}
static void run_suite(const fault_t *flt, const seq_t *isp, suite_t *S) {
    static const int order[] = {D_ISP, D_NPU, D_PERI};
    memset(S, 0, sizeof *S);
    for (int i = 0; i < 3; i++) {
        int d = order[i];
        run_one(S, flt, isp, T_FIRST, d, 0, 11u + (uint32_t)i);
        run_one(S, flt, isp, T_GATED, d, 0, 7u + (uint32_t)i);
        if (DOM[d].has_pwr) {
            run_one(S, flt, isp, T_PWR_A, d, 0, 13u + (uint32_t)i);
            run_one(S, flt, isp, T_PWR_B, d, 0, 17u + (uint32_t)i);
            run_one(S, flt, isp, T_ISO, d, 0, 19u + (uint32_t)i);
        }
    }
    run_one(S, flt, isp, T_CROSS, D_ISP, D_NPU, 23u);
    run_one(S, flt, isp, T_CROSS, D_ISP, D_PERI, 29u);
    run_one(S, flt, isp, T_CROSS, D_NPU, D_ISP, 31u);
}
static void print_fail_full(const fail_t *f) {
    print_fail(f);
    printf(" cause=%s\n", C_NAME[classify(f)]);
}

/* 순서 결함 시퀀스 (좋은 mock 위에서 돌린다) */
static const step_t DN_NO_SAVE[]    = {S_QUIESCE, S_CLK_OFF, S_ISO_ON, S_RST_ON, S_PWR_OFF};
static const step_t UP_ISO_EARLY[]  = {S_PWR_ON, S_ISO_OFF, S_RST_OFF, S_RESTORE, S_CLK_ON};
static const step_t UP_CLK_FIRST[]  = {S_PWR_ON, S_CLK_ON, S_RST_OFF, S_RESTORE, S_ISO_OFF};
static const step_t UP_RESTORE_RST[] = {S_PWR_ON, S_RESTORE, S_RST_OFF, S_CLK_ON, S_ISO_OFF};
static const step_t DN_PWR_BEFORE_ISO[] = {S_QUIESCE, S_CLK_OFF, S_SAVE, S_RST_ON, S_PWR_OFF, S_ISO_ON};
static const step_t DN_SAVE_CLK[]   = {S_QUIESCE, S_SAVE, S_CLK_OFF, S_ISO_ON, S_RST_ON, S_PWR_OFF};
static const seq_t BAD_NO_SAVE     = {DN_NO_SAVE, ALEN(DN_NO_SAVE), SEQ_UP_RET, ALEN(SEQ_UP_RET)};
static const seq_t BAD_ISO_EARLY   = {SEQ_DOWN_RET, ALEN(SEQ_DOWN_RET), UP_ISO_EARLY, ALEN(UP_ISO_EARLY)};
static const seq_t BAD_CLK_FIRST   = {SEQ_DOWN_RET, ALEN(SEQ_DOWN_RET), UP_CLK_FIRST, ALEN(UP_CLK_FIRST)};
static const seq_t BAD_RESTORE_RST = {SEQ_DOWN_RET, ALEN(SEQ_DOWN_RET), UP_RESTORE_RST, ALEN(UP_RESTORE_RST)};
static const seq_t BAD_PWR_ISO     = {DN_PWR_BEFORE_ISO, ALEN(DN_PWR_BEFORE_ISO), SEQ_UP_RET, ALEN(SEQ_UP_RET)};
static const seq_t BAD_SAVE_CLK    = {DN_SAVE_CLK, ALEN(DN_SAVE_CLK), SEQ_UP_RET, ALEN(SEQ_UP_RET)};


/* 도움: step 순서를 출력 */
static void print_order(const step_t *s, int n) {
    for (int i = 0; i < n; i++) printf("%s%s", i ? " > " : "", STEP_NAME[s[i]]);
    printf("\n");
}
static bool in_list(step_t l[][MAX_STEPS], int cnt, const step_t *s, int n) {
    for (int i = 0; i < cnt; i++) if (memcmp(l[i], s, sizeof(step_t) * (size_t)n) == 0) return true;
    return false;
}
/* 스위트의 실패 중 해당 cause 의 개수 */
static int count_cause(const suite_t *S, int c) { int k = 0; for (int i = 0; i < S->n; i++) k += classify(&S->log[i]) == c; return k; }
/* 패턴 집합으로 CFG shadow 비트 고착 64 종(32비트 × 0/1) 중 몇 개를 잡나 */
static int ret_sweep(const uint32_t *pats, int n) {
    int det = 0;
    for (int b = 0; b < 32; b++) for (int pol = 0; pol < 2; pol++) {
        bool hit = false;
        for (int i = 0; i < n && !hit; i++) {
            fail_t f;
            memset(&f, 0, sizeof f);
            soc_init(3u + (uint32_t)b);
            g_f.ret_mask = 1u << b; g_f.ret_pol = pol != 0;
            bool ok = check_power_cycle(D_ISP, pats[i], TUNE_A, &f);
            hit = !ok && f.step == K_RET && strcmp(f.reg, "CFG") == 0;
            memset(&g_f, 0, sizeof g_f);
        }
        det += hit;
    }
    return det;
}

int main(void) {
    fail_t f;
    uint32_t v;
    bool ok;

    /* ---- mock sanity ---- */
    soc_init(1);
    CHECK(bus_read(D_ISP, REG[R_ID].off, &v) == RESP_OK && v == DOM[D_ISP].id);
    CHECK(bus_read(D_NPU, REG[R_CFG].off, &v) == RESP_OK && v == REG[R_CFG].rst);
    CHECK(pd_step(D_ISP, S_CLK_OFF) && !(pmu_status(D_ISP) & STAT_CLK_ACK));
    pmu_apply(D_ISP, S_CLK_ON);
    CHECK(!(pmu_status(D_ISP) & STAT_CLK_ACK));
    soc_tick(T_SETTLE);
    CHECK((pmu_status(D_ISP) & STAT_CLK_ACK) != 0);

    /* ---- Part A: 좋은 모델은 모두 통과 ---- */
    static const int doms[] = {D_ISP, D_NPU, D_PERI};
    for (int i = 0; i < 3; i++) {
        memset(&f, 0, sizeof f); soc_init(2); CHECK(check_gated_access(doms[i], &f));
        memset(&f, 0, sizeof f); soc_init(3); CHECK(check_first_access(doms[i], &f));
    }
    for (int i = 0; i < 2; i++) {
        memset(&f, 0, sizeof f); soc_init(4); CHECK(check_power_cycle(doms[i], CFG_A, TUNE_A, &f));
        memset(&f, 0, sizeof f); soc_init(5); CHECK(check_iso_trace(doms[i], CFG_A, &f));
    }
    /* Part A: 결함별 — 올바른 단계·레지스터·기대/실제로 실패해야 한다 */
    struct { const char *name; fault_t ft; int kind; int d; chk_t k; const char *reg; uint32_t exp, act; } tcase[] = {
        {"gate_ok",      {.gate_ok = true},      0, D_PERI, K_GATED_RESP, "ID", RESP_SLVERR, RESP_OK},
        {"gate_hang",    {.gate_hang = true},    0, D_ISP,  K_GATED_RESP, "ID", RESP_SLVERR, RESP_HANG},
        {"gate_wr_leak", {.gate_wr_leak = true}, 0, D_NPU,  K_GATED_WR_LEAK, "CTX", 0x1234ABCDu, ~0x1234ABCDu},
        {"off_slverr",   {.off_slverr = true},   2, D_ISP,  K_OFF_RESP, "ID", RESP_DECERR, RESP_SLVERR},
        {"ret_skip",     {.ret_skip = true},     2, D_ISP,  K_RET, "CFG", CFG_A, 0x00000010u},
        {"ctx_retained", {.ctx_retained = true}, 2, D_ISP,  K_CTX_RESET, "CTX", 0xA5A50000u, 0x13572468u},
        {"clamp_ff",     {.clamp_ff = true},     3, D_ISP,  K_PORT_CLAMP, "PORT", 0x00, 0xFF},
        {"iso_stuck",    {.iso_no_release = true}, 3, D_NPU, K_PORT_OPEN, "PORT", 0x10, 0x00},
        {"iso_early_hw", {.iso_early_hw = true}, 3, D_ISP,  K_PORT_CLAMP, "PORT", 0x00, 0x10},
        {"ack_early",    {.ack_early = true},    1, D_ISP,  K_ACK, "PMU_STAT", 0, 1},
        {"settle_no_err",{.settle_no_err = true},1, D_PERI, K_SETTLE_RESP, "CTX", RESP_SLVERR, RESP_OK},
    };
    for (size_t i = 0; i < sizeof tcase / sizeof tcase[0]; i++) {
        memset(&f, 0, sizeof f); f.dom = tcase[i].d; f.test = tcase[i].name;
        soc_init(42); g_f = tcase[i].ft;
        switch (tcase[i].kind) {
        case 0: ok = check_gated_access(tcase[i].d, &f); break;
        case 1: ok = check_first_access(tcase[i].d, &f); break;
        case 2: ok = check_power_cycle(tcase[i].d, CFG_A, TUNE_A, &f); break;
        default: ok = check_iso_trace(tcase[i].d, CFG_A, &f); break;
        }
        memset(&g_f, 0, sizeof g_f);
        CHECK(!ok && f.step == tcase[i].k && f.reg && strcmp(f.reg, tcase[i].reg) == 0 &&
              f.exp == tcase[i].exp && f.act == tcase[i].act);
    }
    /* ack_early: k=1 에서 ack 가 이미 1 / settle_no_err: k=0 읽기가 OKAY 를 돌려줌 */
    memset(&f, 0, sizeof f); soc_init(42); g_f.ack_early = true;
    CHECK(!check_first_access(D_ISP, &f) && f.k == 1);
    memset(&g_f, 0, sizeof g_f);

    /* ---- Part B: 스텝 순서 전수 조사 ---- */
    enum_t ed, eu;
    enumerate_orders(SEQ_DOWN_RET, 6, true, 0xFF, &ed);
    enumerate_orders(SEQ_UP_RET, 5, false, 0xFF, &eu);
    printf("[B] down: %d orders, safe=%d | up: %d orders, safe=%d\n", ed.total, ed.safe, eu.total, eu.safe);
    CHECK(ed.total == 720 && ed.safe == 6);
    CHECK(eu.total == 120 && eu.safe == 2);
    CHECK(in_list(ed.safe_list, ed.safe, SEQ_DOWN_RET, 6));
    CHECK(in_list(eu.safe_list, eu.safe, SEQ_UP_RET, 5));
    for (int i = 0; i < ed.safe; i++) { printf("  safe down: "); print_order(ed.safe_list[i], 6); }
    for (int i = 0; i < eu.safe; i++) { printf("  safe up:   "); print_order(eu.safe_list[i], 5); }
    printf("  violations down:"); for (int k = 0; k < V__N; k++) printf(" %s=%d", V_NAME[k], ed.hist[k]); printf("\n");
    printf("  violations up:  "); for (int k = 0; k < V__N; k++) printf(" %s=%d", V_NAME[k], eu.hist[k]); printf("\n");
    /* hist 의 해석: UNQUIESCED 는 QUIESCE 가 첫 스텝이 아닌 모든 순서 — 720 - 120 = 600 */
    CHECK(ed.hist[V_UNQUIESCED] == 600);
    /* X 가 드러난 down 순서 = ISO_ON 이 PWR_OFF 뒤인 순서 = 720/2 */
    CHECK(ed.hist[V_X_CROSS] == 360);
    /* 추출된 precedence */
    enum { Q, CK, IS, SV, RO, PO };             /* 내림 인덱스 */
    enum { UP_PW, UP_RS, UP_RE, UP_CK, UP_IS }; /* 올림 인덱스 */
    CHECK(ed.prec[Q][CK] && ed.prec[Q][PO] && ed.prec[CK][SV] && ed.prec[SV][RO] && ed.prec[IS][RO] && ed.prec[IS][PO] && ed.prec[SV][PO]);
    CHECK(!ed.prec[CK][IS] && !ed.prec[IS][CK] && !ed.prec[IS][SV] && !ed.prec[RO][PO] && !ed.prec[PO][RO]);
    CHECK(eu.prec[UP_PW][UP_RS] && eu.prec[UP_RS][UP_RE] && eu.prec[UP_RS][UP_CK] && eu.prec[UP_RE][UP_CK] && eu.prec[UP_RE][UP_IS] && eu.prec[UP_RS][UP_IS]);
    CHECK(!eu.prec[UP_CK][UP_IS] && !eu.prec[UP_IS][UP_CK]);    /* CLK_ON 과 ISO_OFF 의 상대 순서는 이 물리 모델에서 자유 */

    /* 내림 × 올림 전체 곱 (86,400) — 두 단계가 OFF 상태 한 곳으로만 이어지므로 독립이어야 한다 */
    int di[6] = {0,1,2,3,4,5}, ui[5];
    int pairs = 0, safe_pairs = 0;
    do {
        for (int i = 0; i < 5; i++) ui[i] = i;
        do {
            step_t sd[6], su[5];
            oracle_t o;
            for (int i = 0; i < 6; i++) sd[i] = SEQ_DOWN_RET[di[i]];
            for (int i = 0; i < 5; i++) su[i] = SEQ_UP_RET[ui[i]];
            oracle_roundtrip(sd, 6, su, 5, 0x1234567u, 0xFF, &o);
            pairs++; safe_pairs += o.viol == 0;
        } while (next_perm(ui, 5));
    } while (next_perm(di, 6));
    printf("  cross product: %d pairs, safe=%d (= %d x %d)\n", pairs, safe_pairs, ed.safe, eu.safe);
    CHECK(pairs == 86400 && safe_pairs == ed.safe * eu.safe && safe_pairs == 12);

    /* 한계 1 — 관측 가능성: 위반 순서 중 소프트웨어로 보이는 것 */
    printf("  observability down: unsafe=%d readback=%d port8=%d either=%d monitor_only=%d\n",
           ed.total - ed.safe, ed.seen_rb, ed.seen_port, ed.seen_either, ed.only_monitor);
    printf("  observability up:   unsafe=%d readback=%d port8=%d either=%d monitor_only=%d\n",
           eu.total - eu.safe, eu.seen_rb, eu.seen_port, eu.seen_either, eu.only_monitor);
    CHECK(ed.seen_either + ed.only_monitor == ed.total - ed.safe);
    CHECK(ed.only_monitor == 30);    /* QUIESCE 만 늦은 30 순서: 레지스터로는 안 보인다 */
    CHECK(eu.only_monitor == 0);
    /* 한계 2 — 1비트 포트(X 가 임의의 0/1 로 굳는다): port 샘플이 놓치는 순서가 생긴다 */
    enum_t ed1, eu1;
    enumerate_orders(SEQ_DOWN_RET, 6, true, 0x01, &ed1);
    enumerate_orders(SEQ_UP_RET, 5, false, 0x01, &eu1);
    printf("  1-bit port: down port=%d either=%d monitor_only=%d | up port=%d either=%d monitor_only=%d\n",
           ed1.seen_port, ed1.seen_either, ed1.only_monitor, eu1.seen_port, eu1.seen_either, eu1.only_monitor);
    CHECK(ed1.seen_port < ed.seen_port && ed1.only_monitor == 36 && eu1.only_monitor == 2);
    /* 한계 3 — 순서가 맞아도 시간이 틀리면 깨진다: 순열 열거에 안 나타난다 */
    oracle_t w, nw;
    oracle_run(SEQ_RET_REP.down, SEQ_RET_REP.nd, SEQ_RET_REP.up, SEQ_RET_REP.nu, 7u, 0xFF, true, &w);
    oracle_run(SEQ_RET_REP.down, SEQ_RET_REP.nd, SEQ_RET_REP.up, SEQ_RET_REP.nu, 7u, 0xFF, false, &nw);
    printf("  rep order, wait=yes viol=0x%X  wait=no viol=0x%X (RET_LOST=%d RST_BAD=%d X_CROSS=%d)\n",
           w.viol, nw.viol, nw.viol >> V_RET_LOST & 1, nw.viol >> V_RST_BAD & 1, nw.viol >> V_X_CROSS & 1);
    CHECK(w.viol == 0 && (nw.viol & (1u << V_RET_LOST)) && (nw.viol & (1u << V_X_CROSS)));
    /* 한계 4 — retention 패턴 마스킹: 패턴이 reset 값이면 skip 이 안 보인다 */
    memset(&f, 0, sizeof f); soc_init(50); g_f.ret_skip = true;
    CHECK(check_power_cycle(D_ISP, 0x00000010u, 0x000000FFu, &f));          /* reset 패턴: 통과 — 놓친다 */
    memset(&f, 0, sizeof f); soc_init(50); g_f.ret_skip = true;
    CHECK(!check_power_cycle(D_ISP, CFG_A, TUNE_A, &f) && f.step == K_RET); /* 비-reset 패턴: 잡는다 */
    memset(&g_f, 0, sizeof g_f);
    static const uint32_t P_RST[] = {0x00000010u}, P_A[] = {CFG_A}, P_AB[] = {CFG_A, ~CFG_A};
    int det_r = ret_sweep(P_RST, 1), det_a = ret_sweep(P_A, 1), det_ab = ret_sweep(P_AB, 2);
    printf("  retention-bit coverage (64 stuck faults): reset-pat=%d, A=%d, A+~A=%d\n", det_r, det_a, det_ab);
    CHECK(det_r == 32 && det_a == 32 && det_ab == 64);

    /* ---- Part C: 야간 회귀 — 결함 주입 캠페인 ---- */
    suite_t S;
    run_suite(&(fault_t){0}, NULL, &S);
    printf("[C good] runs=%d failures=%d\n", S.runs, S.n);
    CHECK(S.runs == 15 && S.n == 0);
    struct { const char *name; fault_t ft; const seq_t *q; int nfail; const char *test; chk_t k; int cause; } camp[] = {
        {"hw:gate_ok",        {.gate_ok = true}, NULL, 3, "gated", K_GATED_RESP, C_GATE_NO_ERR},
        {"hw:gate_hang",      {.gate_hang = true}, NULL, 3, "gated", K_GATED_RESP, C_GATE_HANG},
        {"hw:gate_wr_leak",   {.gate_wr_leak = true}, NULL, 3, "gated", K_GATED_WR_LEAK, C_GATE_WR_LEAK},
        {"hw:off_slverr",     {.off_slverr = true}, NULL, 4, "pwrcyc", K_OFF_RESP, C_OFF_RESP},
        {"hw:ret_skip",       {.ret_skip = true}, NULL, 3, "pwrcyc", K_RET, C_RET_LOST},
        {"hw:ret_bit5_0",     {.ret_mask = 1u << 5, .ret_pol = false}, NULL, 2, "pwrcyc", K_RET, C_RET_BIT},
        {"hw:ctx_retained",   {.ctx_retained = true}, NULL, 4, "pwrcyc", K_CTX_RESET, C_RST_KEPT},
        {"hw:rst_skip",       {.rst_skip = true}, NULL, 5, "pwrcyc", K_CTX_RESET, C_RST_CORRUPT},
        {"hw:clamp_ff",       {.clamp_ff = true}, NULL, 2, "iso", K_PORT_CLAMP, C_CLAMP_VALUE},
        {"hw:iso_stuck",      {.iso_no_release = true}, NULL, 2, "iso", K_PORT_OPEN, C_ISO_STUCK},
        {"hw:iso_early_hw",   {.iso_early_hw = true}, NULL, 2, "iso", K_PORT_CLAMP, C_ISO_EARLY},
        {"hw:ack_early",      {.ack_early = true}, NULL, 12, "first", K_ACK, C_ACK_EARLY},
        {"hw:settle_no_err",  {.settle_no_err = true}, NULL, 3, "first", K_SETTLE_RESP, C_SETTLE_UNSAFE},
        {"hw:cross_gate",     {.cross_gate = true}, NULL, 1, "cross", K_CROSS, C_CROSS_DOMAIN},
        {"seq:no_save",       {0}, &BAD_NO_SAVE, 3, "pwrcyc", K_RET, C_RET_LOST},
        {"seq:iso_early",     {0}, &BAD_ISO_EARLY, 1, "iso", K_PORT_OPEN, C_ISO_EARLY},
        {"seq:clk_before_rst",{0}, &BAD_CLK_FIRST, 3, "pwrcyc", K_CTX_RESET, C_RST_CORRUPT},
        {"seq:restore_in_rst",{0}, &BAD_RESTORE_RST, 3, "pwrcyc", K_RET, C_RET_LOST},
        {"seq:pwr_before_iso",{0}, &BAD_PWR_ISO, 1, "iso", K_PORT_HOLD, C_ISO_LATE},
        {"seq:save_clk_on",   {0}, &BAD_SAVE_CLK, 3, "pwrcyc", K_RET, C_RET_CORRUPT},
    };
    for (size_t i = 0; i < sizeof camp / sizeof camp[0]; i++) {
        run_suite(&camp[i].ft, camp[i].q, &S);
        printf("[%s] failures=%d/%d first: ", camp[i].name, S.n, S.runs);
        if (S.n) print_fail_full(&S.log[0]); else printf("(none)\n");
        CHECK(S.n == camp[i].nfail);
        CHECK(S.n > 0 && strcmp(S.log[0].test, camp[i].test) == 0 && S.log[0].step == camp[i].k && classify(&S.log[0]) == camp[i].cause);
    }
    /* iso 조기 해제는 레지스터 시험(pwrcyc)이 전부 통과한다 — 포트 샘플에서만 잡힌다 */
    run_suite(&(fault_t){0}, &BAD_ISO_EARLY, &S);
    CHECK(S.n == 1 && S.log[0].dom == D_ISP && strcmp(S.log[0].test, "iso") == 0);
    printf("---- report: SENS-A ISP seq=iso_early (pwr_on > iso_off > rst_off > restore > clk_on) ----\n");
    for (int i = 0; i < S.n; i++) print_fail_full(&S.log[i]);
    /* cross_gate 의 희생 도메인은 NPU */
    run_suite(&(fault_t){.cross_gate = true}, NULL, &S);
    CHECK(S.n == 1 && S.log[0].dom == D_NPU && count_cause(&S, C_CROSS_DOMAIN) == 1);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
