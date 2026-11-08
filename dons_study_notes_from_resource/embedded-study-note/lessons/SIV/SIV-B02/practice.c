/* SIV-B02 추가 연습 — 한 번에 검증
 *
 * 본문 solution.c 의 mock / 접근 계층 / test_field / 리포트 한 줄을 복사해 두고(자기완결 — raw MMIO·trace
 * 백엔드와 run_suite 는 뺐다), mock 에 FK_WO_DROP 결함과 gated(클럭 꺼짐) 플래그를 더했다.
 *  연습 1: backdoor peek 으로 WO 쓰기가 실제로 들어갔는지 확인 (RAZ 만으로는 못 잡는 FK_WO_DROP)
 *  연습 2: 증거를 지우지 않는 레지스터 덤프 (RC 는 건너뛴다)
 *  연습 3: 결함 모델 × access type 테스트 검출 행렬 자동 생성
 *  연습 4: 버스 에러 폭주 시 스위트 중단 (클럭 꺼진 블록)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 레지스터 기술 (SIV-B01 이 JSON 에서 생성하는 표의 축약판) ---------------- */
typedef enum { AT_RW, AT_RO, AT_WO, AT_W1C, AT_W1S, AT_RC } access_t;
static const char *const AT_NAME[] = { "RW", "RO", "WO", "W1C", "W1S", "RC" };

typedef struct { const char *name; uint8_t lsb, width; access_t acc; } field_desc_t;
typedef struct {
    const char *name; uint32_t off; uint32_t reset;
    const field_desc_t *fields; int nfields;
} reg_desc_t;

/* 비트 → access class. 어느 마스크에도 없는 비트 = reserved (읽으면 0, 쓰기 무시) */
typedef struct { uint32_t rw, ro, wo, w1c, w1s, rc; } reg_masks_t;

static uint32_t field_mask(const field_desc_t *f) {
    uint32_t ones = f->width >= 32 ? 0xFFFFFFFFu : ((1u << f->width) - 1u);
    return ones << f->lsb;
}

/* ---------------- 버스 계층 ---------------- */
enum { BUS_OK = 0, BUS_DECERR = -1, BUS_SLVERR = -2 };  /* AXI RRESP/BRESP 이름을 빌림 */
#define BUS_DEAD 0xDEADBEEFu                            /* 에러 응답 때 돌려주는 읽기 데이터 */

typedef struct {
    const char *name;
    void *ctx;
    int  (*read32)(void *ctx, uint32_t off, uint32_t *val);  /* 0 = OK, 음수 = 버스 에러 */
    int  (*write32)(void *ctx, uint32_t off, uint32_t val);
    void (*hw_set)(void *ctx, uint32_t off, uint32_t mask);  /* 테스트 트리거(HW 이벤트). 없으면 NULL */
    void (*reset)(void *ctx);                                /* 블록 리셋. 없으면 NULL */
} bus_ops_t;

/* ---------------- mock 레지스터 파일 + 결함 주입 ---------------- */
typedef enum {
    FK_NONE, FK_W1C_AS_RW, FK_RO_WRITABLE, FK_RC_NO_CLEAR, FK_WO_READBACK, FK_W1S_AS_RW, FK_SLVERR,
    FK_WO_DROP      /* 연습 1: WO 쓰기가 플립플롭에 안 들어감 */
} fault_kind_t;
typedef struct { fault_kind_t kind; uint32_t off; uint32_t mask; } fault_t;

#define MAX_REGS   16
#define MAX_FAULTS 4
typedef struct {
    const reg_desc_t *regs; int nregs;
    reg_masks_t masks[MAX_REGS];
    uint32_t    store[MAX_REGS];          /* 플립플롭. 테스트는 이걸 직접 보지 않는다 (backdoor) */
    fault_t     faults[MAX_FAULTS]; int nfaults;
    bool        gated;               /* 연습 4: 블록 클럭 꺼짐 → 모든 접근 SLVERR */
} mock_t;

/* ---- Part A1: 필드 목록 → access 마스크. 폭 0, 32비트 초과, 필드 겹침이면 false ---- */
static bool masks_from_fields(const reg_desc_t *r, reg_masks_t *m) {
    memset(m, 0, sizeof *m);
    uint32_t all = 0;
    for (int i = 0; i < r->nfields; i++) {
        const field_desc_t *f = &r->fields[i];
        if (f->width == 0 || f->lsb + f->width > 32) return false;
        uint32_t fm = field_mask(f);
        if (all & fm) return false;
        all |= fm;
        switch (f->acc) {
        case AT_RW:  m->rw  |= fm; break;
        case AT_RO:  m->ro  |= fm; break;
        case AT_WO:  m->wo  |= fm; break;
        case AT_W1C: m->w1c |= fm; break;
        case AT_W1S: m->w1s |= fm; break;
        case AT_RC:  m->rc  |= fm; break;
        }
    }
    return true;
}

static int mock_find(const mock_t *m, uint32_t off) {
    for (int i = 0; i < m->nregs; i++)
        if (m->regs[i].off == off) return i;
    return -1;                                    /* 구멍(hole) 또는 비정렬 주소 */
}
/* 결함 주입: 지정 비트를 다른 access class 로 "잘못 구현" 한다 (주어진 코드) */
static reg_masks_t eff_masks(const mock_t *m, int i) {
    reg_masks_t k = m->masks[i];
    for (int j = 0; j < m->nfaults; j++) {
        const fault_t *f = &m->faults[j];
        uint32_t b;
        if (f->off != m->regs[i].off) continue;
        switch (f->kind) {
        case FK_W1C_AS_RW:   b = k.w1c & f->mask; k.w1c &= ~b; k.rw |= b; break;
        case FK_RO_WRITABLE: b = k.ro  & f->mask; k.ro  &= ~b; k.rw |= b; break;
        case FK_RC_NO_CLEAR: b = k.rc  & f->mask; k.rc  &= ~b; k.ro |= b; break;
        case FK_WO_READBACK: b = k.wo  & f->mask; k.wo  &= ~b; k.rw |= b; break;
        case FK_W1S_AS_RW:   b = k.w1s & f->mask; k.w1s &= ~b; k.rw |= b; break;
        case FK_WO_DROP:     b = k.wo  & f->mask; k.wo  &= ~b;             break; /* reserved 처럼 */
        default: break;
        }
    }
    return k;
}
static bool reg_slverr(const mock_t *m, uint32_t off) {   /* 예: 클럭 꺼진 서브블록 */
    for (int j = 0; j < m->nfaults; j++)
        if (m->faults[j].kind == FK_SLVERR && m->faults[j].off == off) return true;
    return false;
}

/* ---- Part A2: 읽기 경로. 구멍 → DECERR, 결함 SLVERR → SLVERR (둘 다 *val = BUS_DEAD).
 *      WO 비트는 0 으로 읽히고(RAZ), 읽은 뒤 RC 비트는 지워진다. ---- */
static int mock_read32(void *ctx, uint32_t off, uint32_t *val) {
    mock_t *m = ctx;
    int i = mock_find(m, off);
    if (i < 0)               { *val = BUS_DEAD; return BUS_DECERR; }
    if (m->gated || reg_slverr(m, off)) { *val = BUS_DEAD; return BUS_SLVERR; }
    reg_masks_t k = eff_masks(m, i);
    *val = m->store[i] & ~k.wo;
    m->store[i] &= ~k.rc;
    return BUS_OK;
}
/* ---- Part A3: 쓰기 경로. RW/WO 교체, W1C 1 쓴 비트 지움, W1S 1 쓴 비트 켬, RO/RC/reserved 무시 ---- */
static int mock_write32(void *ctx, uint32_t off, uint32_t v) {
    mock_t *m = ctx;
    int i = mock_find(m, off);
    if (i < 0)               return BUS_DECERR;
    if (m->gated || reg_slverr(m, off)) return BUS_SLVERR;
    reg_masks_t k = eff_masks(m, i);
    uint32_t s = m->store[i], wr = k.rw | k.wo;
    s = (s & ~wr) | (v & wr);
    s &= ~(v & k.w1c);
    s |= v & k.w1s;
    m->store[i] = s;
    return BUS_OK;
}
static void mock_hw_set(void *ctx, uint32_t off, uint32_t mask) {   /* HW 가 상태 비트를 세움 */
    mock_t *m = ctx;
    int i = mock_find(m, off);
    if (i >= 0) m->store[i] |= mask;
}
static void mock_reset(void *ctx) {
    mock_t *m = ctx;
    for (int i = 0; i < m->nregs; i++) m->store[i] = m->regs[i].reset;
}
static bool mock_init(mock_t *m, const reg_desc_t *regs, int n) {
    memset(m, 0, sizeof *m);
    if (n > MAX_REGS) return false;
    m->regs = regs; m->nregs = n;
    for (int i = 0; i < n; i++)
        if (!masks_from_fields(&regs[i], &m->masks[i])) return false;
    mock_reset(m);
    return true;
}
static bool mock_add_fault(mock_t *m, fault_kind_t k, uint32_t off, uint32_t mask) {
    if (m->nfaults >= MAX_FAULTS) return false;
    m->faults[m->nfaults++] = (fault_t){ k, off, mask };
    return true;
}
static bus_ops_t mock_bus(mock_t *m) {
    return (bus_ops_t){ "mock", m, mock_read32, mock_write32, mock_hw_set, mock_reset };
}

/* ---------------- Part A4: 접근 계층 — 테스트 코드는 이 두 함수만 쓴다 ---------------- */
static const bus_ops_t *g_bus;
static struct {
    unsigned reads, writes, errors;
    int      sticky_err;      /* 마지막으로 꺼낸 뒤 "처음" 난 버스 에러 (first error wins) */
    uint32_t err_off;         /* 그 에러의 오프셋 */
} g_acc;

static uint32_t reg_read32(uint32_t off) {
    uint32_t v = BUS_DEAD;
    int e = g_bus->read32(g_bus->ctx, off, &v);
    g_acc.reads++;
    if (e) {
        g_acc.errors++;
        if (!g_acc.sticky_err) { g_acc.sticky_err = e; g_acc.err_off = off; }
    }
    return v;
}
static void reg_write32(uint32_t off, uint32_t v) {
    int e = g_bus->write32(g_bus->ctx, off, v);
    g_acc.writes++;
    if (e) {
        g_acc.errors++;
        if (!g_acc.sticky_err) { g_acc.sticky_err = e; g_acc.err_off = off; }
    }
}
static int bus_err_take(void) { int e = g_acc.sticky_err; g_acc.sticky_err = 0; return e; }
static const char *bus_err_name(int e) {
    return e == BUS_DECERR ? "DECERR" : e == BUS_SLVERR ? "SLVERR" : "OK";
}

/* ---------------- Part B: access type 별 단위 테스트 ---------------- */
enum { T_PASS = 0, T_FAIL = 1, T_SKIP = 2 };
typedef struct {
    const char *reg, *field; uint32_t off; access_t acc; const char *step;
    uint32_t exp, act;        /* 필드 비트만 남긴 값 */
    int bus_err;              /* 0 이 아니면 exp/act 대신 버스 에러 */
} fail_t;

static void fail_fill(fail_t *o, const reg_desc_t *r, const field_desc_t *f, const char *step) {
    memset(o, 0, sizeof *o);
    o->reg = r->name; o->field = f->name; o->off = r->off; o->acc = f->acc; o->step = step;
}
/* 읽어서 필드 비트가 exp 인지 본다. 다르거나 버스 에러면 *o 를 채우고 true (= 실패) */
static bool step_expect(const reg_desc_t *r, const field_desc_t *f, const char *step,
                        uint32_t exp, fail_t *o) {
    uint32_t fm = field_mask(f), v = reg_read32(r->off);
    int e = bus_err_take();
    if (!e && (v & fm) == (exp & fm)) return false;
    fail_fill(o, r, f, step);
    o->bus_err = e; o->exp = exp & fm; o->act = e ? v : (v & fm);
    return true;
}
/* 쓰기 한 번. 버스 에러면 *o 를 채우고 true */
static bool step_write(const reg_desc_t *r, const field_desc_t *f, const char *step,
                       uint32_t val, fail_t *o) {
    reg_write32(r->off, val);
    int e = bus_err_take();
    if (!e) return false;
    fail_fill(o, r, f, step);
    o->bus_err = e; o->act = BUS_DEAD;
    return true;
}

/* ---- Part B1: 필드 하나를 그 access type 의 정의대로 검사 ----
 *  RW : w(fm)→r=fm "w1-read",  w(0)→r=0 "w0-read"
 *  RO : v0=r "read",  w(~v0)→r=v0 "write-ignored"
 *  WO : w(fm)→r=0 "raz"
 *  W1C: hw_set(fm)→r=fm "set",  w(0)→r=fm "w0-keeps",  w(fm)→r=0 "w1-clears"   (hw_set 없으면 SKIP)
 *  W1S: v0=r "read",  w(0)→r=v0 "w0-noop",  w(fm)→r=fm "w1-sets",  w(0)→r=fm "w0-keeps"
 *  RC : hw_set(fm)→r=fm "read1",  r=0 "read2-clears"                             (hw_set 없으면 SKIP)
 */
static int test_field(const reg_desc_t *r, const field_desc_t *f, fail_t *o) {
    uint32_t fm = field_mask(f), v0;
    bus_err_take();
    switch (f->acc) {
    case AT_RW:
        if (step_write(r, f, "w1", fm, o) || step_expect(r, f, "w1-read", fm, o)) return T_FAIL;
        if (step_write(r, f, "w0", 0, o)  || step_expect(r, f, "w0-read", 0, o))  return T_FAIL;
        return T_PASS;
    case AT_RO:
        v0 = reg_read32(r->off);
        if (step_expect(r, f, "read", v0, o)) return T_FAIL;   /* 버스 에러 + 안정성 */
        if (step_write(r, f, "write", ~v0 & fm, o) || step_expect(r, f, "write-ignored", v0, o))
            return T_FAIL;
        return T_PASS;
    case AT_WO:
        if (step_write(r, f, "write", fm, o) || step_expect(r, f, "raz", 0, o)) return T_FAIL;
        return T_PASS;
    case AT_W1C:
        if (!g_bus->hw_set) return T_SKIP;
        g_bus->hw_set(g_bus->ctx, r->off, fm);
        if (step_expect(r, f, "set", fm, o)) return T_FAIL;
        if (step_write(r, f, "w0", 0, o)  || step_expect(r, f, "w0-keeps", fm, o)) return T_FAIL;
        if (step_write(r, f, "w1", fm, o) || step_expect(r, f, "w1-clears", 0, o)) return T_FAIL;
        return T_PASS;
    case AT_W1S:
        v0 = reg_read32(r->off);
        if (step_expect(r, f, "read", v0, o)) return T_FAIL;
        if (step_write(r, f, "w0", 0, o)  || step_expect(r, f, "w0-noop", v0, o))  return T_FAIL;
        if (step_write(r, f, "w1", fm, o) || step_expect(r, f, "w1-sets", fm, o))  return T_FAIL;
        if (step_write(r, f, "w0", 0, o)  || step_expect(r, f, "w0-keeps", fm, o)) return T_FAIL;
        return T_PASS;
    case AT_RC:
        if (!g_bus->hw_set) return T_SKIP;
        g_bus->hw_set(g_bus->ctx, r->off, fm);
        if (step_expect(r, f, "read1", fm, o))       return T_FAIL;
        if (step_expect(r, f, "read2-clears", 0, o)) return T_FAIL;
        return T_PASS;
    }
    return T_SKIP;
}

/* ---------------- Part C: 리포트, 스위트, 실제 MMIO 백엔드 ---------------- */
/* C1: 1 인 비트를 높은 쪽부터 "31,9:8,3:0" 처럼 (연속 구간은 hi:lo) */
static void fmt_bits(uint32_t d, char *buf, size_t n) {
    size_t len = 0;
    buf[0] = '\0';
    for (int b = 31; b >= 0; b--) {
        if (!(d >> b & 1u)) continue;
        int lo = b;
        while (lo > 0 && (d >> (lo - 1) & 1u)) lo--;
        int w = lo == b ? snprintf(buf + len, n - len, "%s%d", len ? "," : "", b)
                        : snprintf(buf + len, n - len, "%s%d:%d", len ? "," : "", b, lo);
        if (w < 0 || (size_t)w >= n - len) return;
        len += (size_t)w;
        b = lo;
    }
}
/* C2: 디버그 엔지니어가 바로 읽는 한 줄 */
static void fmt_fail(const fail_t *f, char *buf, size_t n) {
    if (f->bus_err) {
        snprintf(buf, n, "FAIL reg=%s.%s off=0x%03X acc=%s step=%s bus=%s rdata=0x%08X",
                 f->reg, f->field, (unsigned)f->off, AT_NAME[f->acc], f->step,
                 bus_err_name(f->bus_err), (unsigned)f->act);
        return;
    }
    char bits[96];
    fmt_bits(f->exp ^ f->act, bits, sizeof bits);
    snprintf(buf, n, "FAIL reg=%s.%s off=0x%03X acc=%s step=%s exp=0x%08X act=0x%08X diff=0x%08X bits=[%s]",
             f->reg, f->field, (unsigned)f->off, AT_NAME[f->acc], f->step,
             (unsigned)f->exp, (unsigned)f->act, (unsigned)(f->exp ^ f->act), bits);
}
/* ---------------- 가상 IP: SPI0 레지스터 맵 ---------------- */
static const field_desc_t F_ID[]   = { { "REV", 0, 8, AT_RO }, { "PART", 16, 16, AT_RO } };
static const field_desc_t F_CTRL[] = { { "EN", 0, 1, AT_RW }, { "MODE", 1, 2, AT_RW }, { "CLKDIV", 8, 8, AT_RW } };
static const field_desc_t F_STAT[] = { { "BUSY", 0, 1, AT_RO }, { "TXFULL", 1, 1, AT_RO }, { "RXCNT", 4, 4, AT_RO } };
static const field_desc_t F_IST[]  = { { "DONE", 0, 1, AT_W1C }, { "OVR", 1, 1, AT_W1C }, { "UDR", 2, 1, AT_W1C } };
static const field_desc_t F_IEN[]  = { { "DONE_EN", 0, 1, AT_W1S }, { "OVR_EN", 1, 1, AT_W1S } };
static const field_desc_t F_TX[]   = { { "DATA", 0, 8, AT_WO } };
static const field_desc_t F_ERR[]  = { { "CNT", 0, 8, AT_RC } };
#define NF(a) ((int)(sizeof(a) / sizeof((a)[0])))
static const reg_desc_t SPI0[] = {
    { "ID",       0x000, 0x53500002u, F_ID,   NF(F_ID)   },
    { "CTRL",     0x004, 0x00000400u, F_CTRL, NF(F_CTRL) },
    { "STATUS",   0x008, 0x00000000u, F_STAT, NF(F_STAT) },
    { "INT_STAT", 0x00C, 0x00000000u, F_IST,  NF(F_IST)  },
    { "INT_EN",   0x010, 0x00000000u, F_IEN,  NF(F_IEN)  },
    { "TXDATA",   0x014, 0x00000000u, F_TX,   NF(F_TX)   },
    { "ERR_CNT",  0x018, 0x00000000u, F_ERR,  NF(F_ERR)  },
};
#define SPI0_N NF(SPI0)
enum { R_ID, R_CTRL, R_STATUS, R_INT_STAT, R_INT_EN, R_TXDATA, R_ERR_CNT };


/* ================= 연습 1: backdoor peek 으로 WO 래치 확인 ================= */
/* 실칩에서는 backdoor 가 없다 → NULL. mock 에서는 플립플롭을 직접 본다 (UVM RAL 의 peek 과 같은 발상) */
static uint32_t (*g_peek)(uint32_t off);
static mock_t   g_mock;
static uint32_t mock_peek(uint32_t off) {
    int i = mock_find(&g_mock, off);
    return i < 0 ? 0 : g_mock.store[i];
}
/* WO 필드: 두 패턴을 써서 매번 RAZ 이고, peek 이 가능하면 쓴 값이 래치됐는지도 본다 */
static int test_wo_latch(const reg_desc_t *r, const field_desc_t *f, fail_t *o) {
    static const uint32_t pat[] = { 0xA5A5A5A5u, 0x5A5A5A5Au };
    uint32_t fm = field_mask(f);
    for (int i = 0; i < 2; i++) {
        if (step_write(r, f, "write", pat[i], o) || step_expect(r, f, "raz", 0, o)) return T_FAIL;
        if (!g_peek) continue;
        uint32_t got = g_peek(r->off) & fm;
        if (got != (pat[i] & fm)) {
            fail_fill(o, r, f, "latched");
            o->exp = pat[i] & fm; o->act = got;
            return T_FAIL;
        }
    }
    return g_peek ? T_PASS : T_SKIP;   /* peek 없으면 RAZ 만 확인했다 = 절반만 본 것 */
}

/* ================= 연습 2: 부작용 없는 덤프 ================= */
/* RC 필드가 하나라도 있는 레지스터는 읽지 않는다. 읽은 개수를 돌려주고 out[i] 에 값(건너뛰면 0) */
static int safe_dump(const reg_desc_t *regs, int n, uint32_t *out) {
    int nread = 0;
    for (int i = 0; i < n; i++) {
        reg_masks_t k;
        out[i] = 0;
        if (!masks_from_fields(&regs[i], &k) || k.rc) continue;
        out[i] = reg_read32(regs[i].off);
        nread++;
    }
    return nread;
}

/* ================= 연습 3: 검출 행렬 ================= */
/* 결함 하나를 넣고 스위트를 돌려, 실패한 필드의 access type 별로 센다: mat[acc] */
static void detect_row(const reg_desc_t *regs, int n, int *mat) {
    memset(mat, 0, 6 * sizeof *mat);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < regs[i].nfields; j++) {
            fail_t fl;
            if (g_bus->reset) g_bus->reset(g_bus->ctx);
            if (test_field(&regs[i], &regs[i].fields[j], &fl) == T_FAIL) mat[regs[i].fields[j].acc]++;
        }
}

/* ================= 연습 4: 버스 에러 폭주 시 중단 ================= */
/* 연속 max_bus 개 필드가 버스 에러로 실패하면 멈추고 aborted=1. 첫 실패 줄만 report 에 */
typedef struct { int pass, fail, skip, aborted; } summary2_t;
static summary2_t run_suite_abort(const reg_desc_t *regs, int n, int max_bus, char *first, size_t cap) {
    summary2_t s = { 0, 0, 0, 0 };
    int consec = 0;
    first[0] = '\0';
    for (int i = 0; i < n; i++)
        for (int j = 0; j < regs[i].nfields; j++) {
            fail_t fl;
            if (g_bus->reset) g_bus->reset(g_bus->ctx);
            int rc = test_field(&regs[i], &regs[i].fields[j], &fl);
            if (rc == T_PASS) { s.pass++; consec = 0; continue; }
            if (rc == T_SKIP) { s.skip++; continue; }
            s.fail++;
            if (!first[0]) fmt_fail(&fl, first, cap);
            consec = fl.bus_err ? consec + 1 : 0;
            if (consec >= max_bus) { s.aborted = 1; return s; }
        }
    return s;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static bus_ops_t g_mock_bus;
static void use_mock(void) {
    CHECK(mock_init(&g_mock, SPI0, SPI0_N));
    g_mock_bus = mock_bus(&g_mock);
    g_bus = &g_mock_bus;
    memset(&g_acc, 0, sizeof g_acc);
}

int main(void) {
    fail_t o;
    const reg_desc_t *tx = &SPI0[R_TXDATA];
    char line[200];

    /* 연습 1 */
    use_mock(); g_peek = mock_peek;
    CHECK(test_wo_latch(tx, &tx->fields[0], &o) == T_PASS);
    use_mock(); mock_add_fault(&g_mock, FK_WO_DROP, 0x014, 0xF0u);
    g_peek = NULL;
    CHECK(test_wo_latch(tx, &tx->fields[0], &o) == T_SKIP);            /* RAZ 는 통과 — 결함을 못 봄 */
    CHECK(test_field(tx, &tx->fields[0], &o) == T_PASS);               /* 본문 테스트도 못 봄 */
    g_peek = mock_peek;
    CHECK(test_wo_latch(tx, &tx->fields[0], &o) == T_FAIL);
    fmt_fail(&o, line, sizeof line);
    printf("  ex1: %s\n", line);
    CHECK(strcmp(line, "FAIL reg=TXDATA.DATA off=0x014 acc=WO step=latched "
                       "exp=0x000000A5 act=0x00000005 diff=0x000000A0 bits=[7,5]") == 0);
    g_peek = NULL;

    /* 연습 2 */
    uint32_t dump[SPI0_N];
    use_mock();
    g_bus->hw_set(g_bus->ctx, 0x018, 0x2Au);                           /* 에러 카운터에 증거 */
    g_bus->hw_set(g_bus->ctx, 0x00C, 0x2u);
    CHECK(safe_dump(SPI0, SPI0_N, dump) == SPI0_N - 1);
    CHECK(dump[R_ID] == 0x53500002u && dump[R_INT_STAT] == 0x2u && dump[R_ERR_CNT] == 0);
    CHECK(reg_read32(0x018) == 0x2Au);                                 /* 증거 보존 */
    CHECK(reg_read32(0x00C) == 0x2u);                                  /* W1C 는 읽어도 안전 */
    g_bus->hw_set(g_bus->ctx, 0x018, 0x2Au);
    for (int i = 0; i < SPI0_N; i++) dump[i] = reg_read32(SPI0[i].off);  /* 순진한 덤프 */
    CHECK(dump[R_ERR_CNT] == 0x2Au && reg_read32(0x018) == 0);         /* 증거 소멸 */

    /* 연습 3 */
    static const struct { fault_kind_t k; uint32_t off, mask; const char *name; } fm[] = {
        { FK_W1C_AS_RW, 0x00C, 0x7u, "W1C_AS_RW" }, { FK_RO_WRITABLE, 0x008, 0xF3u, "RO_WRITABLE" },
        { FK_RC_NO_CLEAR, 0x018, 0xFFu, "RC_NO_CLEAR" }, { FK_WO_READBACK, 0x014, 0xFFu, "WO_READBACK" },
        { FK_W1S_AS_RW, 0x010, 0x3u, "W1S_AS_RW" }, { FK_WO_DROP, 0x014, 0xFFu, "WO_DROP" },
    };
    static const int want[6][6] = {          /* 열: RW RO WO W1C W1S RC */
        { 0, 0, 0, 3, 0, 0 }, { 0, 3, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 1 },
        { 0, 0, 1, 0, 0, 0 }, { 0, 0, 0, 0, 2, 0 }, { 0, 0, 0, 0, 0, 0 },
    };
    printf("  ex3: fault         RW RO WO W1C W1S RC\n");
    for (int i = 0; i < 6; i++) {
        int mat[6];
        use_mock(); mock_add_fault(&g_mock, fm[i].k, fm[i].off, fm[i].mask);
        detect_row(SPI0, SPI0_N, mat);
        printf("       %-12s %2d %2d %2d %3d %3d %2d\n", fm[i].name, mat[0], mat[1], mat[2], mat[3], mat[4], mat[5]);
        CHECK(memcmp(mat, want[i], sizeof mat) == 0);
    }

    /* 연습 4 */
    char first[200];
    use_mock(); g_mock.gated = true;
    summary2_t s2 = run_suite_abort(SPI0, SPI0_N, 3, first, sizeof first);
    printf("  ex4: pass=%d fail=%d skip=%d aborted=%d errors=%u\n", s2.pass, s2.fail, s2.skip, s2.aborted, g_acc.errors);
    CHECK(s2.aborted == 1 && s2.fail == 3 && s2.pass == 0);
    CHECK(strcmp(first, "FAIL reg=ID.REV off=0x000 acc=RO step=read bus=SLVERR rdata=0xDEADBEEF") == 0);
    use_mock(); mock_add_fault(&g_mock, FK_SLVERR, 0x008, 0);            /* 레지스터 하나만 에러 */
    s2 = run_suite_abort(SPI0, SPI0_N, 4, first, sizeof first);
    CHECK(s2.aborted == 0 && s2.fail == 3 && s2.pass == 12);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
