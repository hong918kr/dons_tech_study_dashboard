/* SIV-B02 Mock MMIO 레지스터 파일과 접근 계층
 *
 * 실리콘이 오기 전(또는 보드가 한 대뿐일 때) 검증 코드를 먼저 짜 두려면, 테스트가 레지스터에 닿는
 * 길을 한 곳(reg_read32 / reg_write32)으로 모으고 그 아래를 함수 포인터 bus ops 로 갈아 끼울 수
 * 있어야 한다.
 *
 *   test_field()  ──>  reg_read32 / reg_write32  ──>  bus_ops_t { read32, write32, hw_set, reset }
 *                                                      ├─ mock 레지스터 파일 (access type + 결함 주입)
 *                                                      ├─ trace 데코레이터 (모든 버스 접근 기록)
 *                                                      └─ raw MMIO (volatile 포인터, 실보드에서는 이것)
 *
 * Part A: 필드 기술 → access 마스크, mock 의 read/write 경로(RW/RO/WO/W1C/W1S/RC), 접근 계층
 * Part B: access type 별 단위 테스트 test_field() — 좋은 mock 은 PASS, 결함 mock 은 FAIL
 * Part C: SPI0 IP 전체 스위트, 결함 주입 캠페인, 실패 리포트 한 줄, raw MMIO 백엔드로 교체
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
    FK_NONE, FK_W1C_AS_RW, FK_RO_WRITABLE, FK_RC_NO_CLEAR, FK_WO_READBACK, FK_W1S_AS_RW, FK_SLVERR
} fault_kind_t;
typedef struct { fault_kind_t kind; uint32_t off; uint32_t mask; } fault_t;

#define MAX_REGS   16
#define MAX_FAULTS 4
typedef struct {
    const reg_desc_t *regs; int nregs;
    reg_masks_t masks[MAX_REGS];
    uint32_t    store[MAX_REGS];          /* 플립플롭. 테스트는 이걸 직접 보지 않는다 (backdoor) */
    fault_t     faults[MAX_FAULTS]; int nfaults;
} mock_t;

/* ---- Part A1: 필드 목록 → access 마스크. 폭 0, 32비트 초과, 필드 겹침이면 false ---- */
static bool masks_from_fields(const reg_desc_t *r, reg_masks_t *m) {
    memset(m, 0, sizeof *m);
    (void)r;
    return true; /* TODO: 필드마다 폭·범위·겹침 검사 후 access class 마스크에 OR */
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
    (void)ctx; (void)off; (void)eff_masks; (void)reg_slverr;
    *val = 0;
    return BUS_OK; /* TODO: mock_find → 구멍/SLVERR 처리 → WO 는 0, 읽은 뒤 RC 지움 */
}
/* ---- Part A3: 쓰기 경로. RW/WO 교체, W1C 1 쓴 비트 지움, W1S 1 쓴 비트 켬, RO/RC/reserved 무시 ---- */
static int mock_write32(void *ctx, uint32_t off, uint32_t v) {
    (void)ctx; (void)off; (void)v;
    return BUS_OK; /* TODO: eff_masks 로 RW/WO 교체, W1C 지움, W1S 켬 */
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
    (void)off;
    return 0; /* TODO: g_bus->read32 호출, 횟수·에러 집계, 첫 에러는 sticky_err/err_off 에 */
}
static void reg_write32(uint32_t off, uint32_t v) {
    (void)off; (void)v; /* TODO: g_bus->write32 호출, 횟수·에러 집계 */
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
    (void)step_write; (void)step_expect;
    fail_fill(o, r, f, "TODO");
    return T_FAIL; /* TODO: f->acc 별 시퀀스 (위 주석 표) */
}

/* ---------------- Part C: 리포트, 스위트, 실제 MMIO 백엔드 ---------------- */
/* C1: 1 인 비트를 높은 쪽부터 "31,9:8,3:0" 처럼 (연속 구간은 hi:lo) */
static void fmt_bits(uint32_t d, char *buf, size_t n) {
    (void)d; (void)n;
    buf[0] = '\0'; /* TODO */
}
/* C2: 디버그 엔지니어가 바로 읽는 한 줄 */
static void fmt_fail(const fail_t *f, char *buf, size_t n) {
    (void)f; (void)AT_NAME; (void)bus_err_name;
    snprintf(buf, n, "TODO"); /* TODO: bus_err 면 bus=/rdata=, 아니면 exp/act/diff/bits */
}
typedef struct { int pass, fail, skip; } summary_t;

/* C3: 모든 레지스터의 모든 필드를 검사. 필드마다 블록 리셋(있으면). 실패 줄은 report 에 '\n' 로 이어 붙인다 */
static summary_t run_suite(const reg_desc_t *regs, int n, char *report, size_t cap) {
    summary_t s = { 0, 0, 0 };
    (void)regs; (void)n; (void)cap;
    report[0] = '\0';
    return s; /* TODO: 필드마다 reset → test_field → 집계, 실패면 fmt_fail 한 줄 + '\n' */
}

/* C4: 실보드 백엔드 — 레지스터 창을 volatile 포인터로 그대로 접근한다.
 *     (실칩: base = (volatile uint32_t *)0x40013000u, 또는 Linux 에서 /dev/mem mmap)
 *     호스트에서는 정적 배열을 창으로 쓰고, 창 밖·비정렬은 배열 보호용으로 DECERR. */
typedef struct { volatile uint32_t *base; uint32_t size; const reg_desc_t *regs; int nregs; } mmio_ctx_t;
static int mmio_read32(void *ctx, uint32_t off, uint32_t *val) {
    (void)ctx; (void)off;
    *val = 0;
    return BUS_OK; /* TODO: 비정렬·창 밖이면 DECERR, 아니면 volatile 창에서 읽기 */
}
static int mmio_write32(void *ctx, uint32_t off, uint32_t val) {
    (void)ctx; (void)off; (void)val;
    return BUS_OK; /* TODO */
}
static void mmio_reset(void *ctx) {          /* 실칩이면 리셋 컨트롤러. 여기선 리셋값을 써 넣는 흉내 */
    mmio_ctx_t *c = ctx;
    for (int i = 0; i < c->nregs; i++)
        if (c->regs[i].off < c->size) c->base[c->regs[i].off / 4u] = c->regs[i].reset;
}

/* 데코레이터: 아무 bus 나 감싸서 모든 접근을 문자열로 남긴다 (JTAG 로그 / 로직 분석기 화면 흉내) */
typedef struct { const bus_ops_t *inner; char buf[512]; } trace_ctx_t;
static void tlog(trace_ctx_t *t, const char *fmt, uint32_t off, uint32_t v) {
    size_t n = strlen(t->buf);
    if (n + 1 < sizeof t->buf) {
        if (n) t->buf[n++] = ' ';
        snprintf(t->buf + n, sizeof t->buf - n, fmt, (unsigned)off, (unsigned)v);
    }
}
static int tr_read32(void *ctx, uint32_t off, uint32_t *val) {
    trace_ctx_t *t = ctx;
    int e = t->inner->read32(t->inner->ctx, off, val);
    tlog(t, e ? "R %03X!%08X" : "R %03X=%08X", off, *val);
    return e;
}
static int tr_write32(void *ctx, uint32_t off, uint32_t val) {
    trace_ctx_t *t = ctx;
    tlog(t, "W %03X=%08X", off, val);
    return t->inner->write32(t->inner->ctx, off, val);
}
static void tr_hw_set(void *ctx, uint32_t off, uint32_t mask) {
    trace_ctx_t *t = ctx;
    tlog(t, "H %03X|=%08X", off, mask);
    t->inner->hw_set(t->inner->ctx, off, mask);
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

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static mock_t    g_mock;
static bus_ops_t g_mock_bus;
static void use_mock(void) {               /* 깨끗한 mock + 접근 통계 0 으로 시작 */
    CHECK(mock_init(&g_mock, SPI0, SPI0_N));
    g_mock_bus = mock_bus(&g_mock);
    g_bus = &g_mock_bus;
    memset(&g_acc, 0, sizeof g_acc);
}
static void print_report(const char *r) {     /* 리포트 줄은 하네스 FAIL 과 구별되게 들여 쓴다 */
    while (*r) {
        const char *nl = strchr(r, '\n');
        int len = nl ? (int)(nl - r) : (int)strlen(r);
        printf("    | %.*s\n", len, r);
        r += len + (nl ? 1 : 0);
    }
}
static int one_field(int reg, int field, fail_t *o) {
    if (g_bus->reset) g_bus->reset(g_bus->ctx);
    return test_field(&SPI0[reg], &SPI0[reg].fields[field], o);
}

int main(void) {
    reg_masks_t m;
    printf("== Part A: access 마스크 / mock 읽기·쓰기 / 접근 계층 ==\n");
    CHECK(masks_from_fields(&SPI0[R_CTRL], &m) && m.rw == 0x0000FF07u && m.ro == 0);
    CHECK(masks_from_fields(&SPI0[R_ID], &m) && m.ro == 0xFFFF00FFu && m.rw == 0);
    CHECK(masks_from_fields(&SPI0[R_STATUS], &m) && m.ro == 0x000000F3u);
    CHECK(masks_from_fields(&SPI0[R_INT_STAT], &m) && m.w1c == 0x7u && m.w1s == 0);
    CHECK(masks_from_fields(&SPI0[R_TXDATA], &m) && m.wo == 0xFFu);
    {
        static const field_desc_t ov[]   = { { "A", 0, 4, AT_RW }, { "B", 3, 2, AT_W1C } };   /* bit3 겹침 */
        static const field_desc_t wide[] = { { "A", 28, 8, AT_RW } };                         /* 32비트 초과 */
        static const field_desc_t zero[] = { { "A", 4, 0, AT_RW } };
        static const field_desc_t full[] = { { "ALL", 0, 32, AT_RW } };
        reg_desc_t r = { "BAD", 0, 0, ov, 2 };
        CHECK(!masks_from_fields(&r, &m));
        r.fields = wide; r.nfields = 1; CHECK(!masks_from_fields(&r, &m));
        r.fields = zero;                 CHECK(!masks_from_fields(&r, &m));
        r.fields = full;                 CHECK(masks_from_fields(&r, &m) && m.rw == 0xFFFFFFFFu);
    }
    use_mock();
    CHECK(reg_read32(0x000) == 0x53500002u);                    /* 리셋값 */
    CHECK(reg_read32(0x004) == 0x00000400u);
    reg_write32(0x004, 0xFFFFFFFFu);
    CHECK(reg_read32(0x004) == 0x0000FF07u);                    /* RW 비트만 남고 reserved 는 0 */
    reg_write32(0x000, 0xFFFFFFFFu);
    CHECK(reg_read32(0x000) == 0x53500002u);                    /* RO: 쓰기 무시 */
    reg_write32(0x014, 0xA5u);
    CHECK(reg_read32(0x014) == 0u);                             /* WO: 0 으로 읽힘 (RAZ) */
    CHECK(g_mock.store[R_TXDATA] == 0xA5u);                     /* ...하지만 플립플롭엔 들어갔다 */
    g_bus->hw_set(g_bus->ctx, 0x00C, 0x7u);                     /* DONE|OVR|UDR 발생 */
    reg_write32(0x00C, 0x2u);
    CHECK(reg_read32(0x00C) == 0x5u);                           /* W1C: OVR 만 지워짐 */
    reg_write32(0x010, 0x1u); reg_write32(0x010, 0x2u);
    CHECK(reg_read32(0x010) == 0x3u);                           /* W1S: 누적 */
    reg_write32(0x010, 0x0u);
    CHECK(reg_read32(0x010) == 0x3u);                           /* W1S: 0 쓰기는 무시 */
    g_bus->hw_set(g_bus->ctx, 0x018, 0x2Au);
    CHECK(reg_read32(0x018) == 0x2Au);
    CHECK(reg_read32(0x018) == 0u);                             /* RC: 두 번째 읽기는 0 */
    reg_write32(0x018, 0xFFu);
    CHECK(reg_read32(0x018) == 0u);                             /* RC: 쓰기 무시 */
    CHECK(bus_err_take() == BUS_OK && g_acc.errors == 0);
    CHECK(reg_read32(0x01C) == BUS_DEAD);                       /* 구멍 */
    CHECK(g_acc.err_off == 0x01Cu && bus_err_take() == BUS_DECERR);
    CHECK(reg_read32(0x006) == BUS_DEAD && bus_err_take() == BUS_DECERR);  /* 비정렬 */
    reg_write32(0x400, 1u); reg_write32(0x404, 1u);
    CHECK(g_acc.err_off == 0x400u && bus_err_take() == BUS_DECERR);        /* 첫 에러가 남는다 */
    CHECK(g_acc.errors == 4 && g_acc.reads == 13 && g_acc.writes == 10);

    printf("== Part B: access type 별 단위 테스트 ==\n");
    fail_t o;
    use_mock();
    CHECK(one_field(R_CTRL, 2, &o) == T_PASS);                  /* RW  CLKDIV */
    CHECK(one_field(R_ID, 0, &o) == T_PASS);                    /* RO  REV */
    CHECK(one_field(R_TXDATA, 0, &o) == T_PASS);                /* WO  DATA */
    CHECK(one_field(R_INT_STAT, 1, &o) == T_PASS);              /* W1C OVR */
    CHECK(one_field(R_INT_EN, 0, &o) == T_PASS);                /* W1S DONE_EN */
    CHECK(one_field(R_ERR_CNT, 0, &o) == T_PASS);               /* RC  CNT */
    CHECK(g_acc.errors == 0);

    use_mock(); mock_add_fault(&g_mock, FK_W1C_AS_RW, 0x00C, 0x2u);
    CHECK(one_field(R_INT_STAT, 1, &o) == T_FAIL);
    CHECK(strcmp(o.step, "w0-keeps") == 0 && o.exp == 0x2u && o.act == 0u);
    CHECK(one_field(R_INT_STAT, 0, &o) == T_PASS);              /* 같은 레지스터의 DONE 은 멀쩡 */
    use_mock(); mock_add_fault(&g_mock, FK_RO_WRITABLE, 0x000, 0xFu);
    CHECK(one_field(R_ID, 0, &o) == T_FAIL);
    CHECK(strcmp(o.step, "write-ignored") == 0 && o.exp == 0x02u && o.act == 0x0Du);
    use_mock(); mock_add_fault(&g_mock, FK_WO_READBACK, 0x014, 0xFFu);
    CHECK(one_field(R_TXDATA, 0, &o) == T_FAIL && strcmp(o.step, "raz") == 0 && o.act == 0xFFu);
    use_mock(); mock_add_fault(&g_mock, FK_W1S_AS_RW, 0x010, 0x1u);
    CHECK(one_field(R_INT_EN, 0, &o) == T_FAIL && strcmp(o.step, "w0-keeps") == 0);
    CHECK(one_field(R_INT_EN, 1, &o) == T_PASS);
    use_mock(); mock_add_fault(&g_mock, FK_RC_NO_CLEAR, 0x018, 0xFFu);
    CHECK(one_field(R_ERR_CNT, 0, &o) == T_FAIL && strcmp(o.step, "read2-clears") == 0 && o.act == 0xFFu);
    use_mock(); mock_add_fault(&g_mock, FK_SLVERR, 0x004, 0);
    CHECK(one_field(R_CTRL, 0, &o) == T_FAIL && o.bus_err == BUS_SLVERR && o.act == BUS_DEAD);
    CHECK(strcmp(o.step, "w1") == 0);                           /* 첫 접근(쓰기)에서 잡힘 */
    {
        bus_ops_t no_trig = mock_bus(&g_mock);                  /* 트리거 못 거는 백엔드 */
        no_trig.hw_set = NULL;
        use_mock(); g_bus = &no_trig;
        CHECK(one_field(R_INT_STAT, 0, &o) == T_SKIP);
        CHECK(one_field(R_ERR_CNT, 0, &o) == T_SKIP);
        CHECK(one_field(R_CTRL, 1, &o) == T_PASS);              /* RW 는 트리거 없이도 된다 */
    }

    printf("== Part C: SPI0 스위트, 결함 주입 캠페인, 백엔드 교체 ==\n");
    char bits[96], line[200], report[2048];
    fmt_bits(0x0000000Fu, bits, sizeof bits); CHECK(strcmp(bits, "3:0") == 0);
    fmt_bits(0x00000002u, bits, sizeof bits); CHECK(strcmp(bits, "1") == 0);
    fmt_bits(0x8000030Fu, bits, sizeof bits); CHECK(strcmp(bits, "31,9:8,3:0") == 0);
    fmt_bits(0xFFFFFFFFu, bits, sizeof bits); CHECK(strcmp(bits, "31:0") == 0);
    fmt_bits(0x00000005u, bits, sizeof bits); CHECK(strcmp(bits, "2,0") == 0);
    {
        fail_t f = { "CTRL", "EN", 0x004, AT_RW, "w1-read", 0x1u, 0x0u, 0 };
        fmt_fail(&f, line, sizeof line);
        CHECK(strcmp(line, "FAIL reg=CTRL.EN off=0x004 acc=RW step=w1-read "
                           "exp=0x00000001 act=0x00000000 diff=0x00000001 bits=[0]") == 0);
        fail_t b = { "STATUS", "BUSY", 0x008, AT_RO, "read", 0, BUS_DEAD, BUS_SLVERR };
        fmt_fail(&b, line, sizeof line);
        CHECK(strcmp(line, "FAIL reg=STATUS.BUSY off=0x008 acc=RO step=read bus=SLVERR rdata=0xDEADBEEF") == 0);
    }

    use_mock();
    summary_t s = run_suite(SPI0, SPI0_N, report, sizeof report);
    printf("  [mock good] pass=%d fail=%d skip=%d\n", s.pass, s.fail, s.skip);
    CHECK(s.pass == 15 && s.fail == 0 && s.skip == 0 && report[0] == '\0');

    static const struct { fault_kind_t k; uint32_t off, mask; int nfail; const char *first; } camp[] = {
        { FK_W1C_AS_RW,   0x00C, 0x2u,  1, "FAIL reg=INT_STAT.OVR off=0x00C acc=W1C step=w0-keeps exp=0x00000002 act=0x00000000 diff=0x00000002 bits=[1]" },
        { FK_RO_WRITABLE, 0x000, 0xFu,  1, "FAIL reg=ID.REV off=0x000 acc=RO step=write-ignored exp=0x00000002 act=0x0000000D diff=0x0000000F bits=[3:0]" },
        { FK_RC_NO_CLEAR, 0x018, 0xFFu, 1, "FAIL reg=ERR_CNT.CNT off=0x018 acc=RC step=read2-clears exp=0x00000000 act=0x000000FF diff=0x000000FF bits=[7:0]" },
        { FK_WO_READBACK, 0x014, 0xFFu, 1, "FAIL reg=TXDATA.DATA off=0x014 acc=WO step=raz exp=0x00000000 act=0x000000FF diff=0x000000FF bits=[7:0]" },
        { FK_W1S_AS_RW,   0x010, 0x1u,  1, "FAIL reg=INT_EN.DONE_EN off=0x010 acc=W1S step=w0-keeps exp=0x00000001 act=0x00000000 diff=0x00000001 bits=[0]" },
        { FK_SLVERR,      0x008, 0,     3, "FAIL reg=STATUS.BUSY off=0x008 acc=RO step=read bus=SLVERR rdata=0xDEADBEEF" },
    };
    for (int i = 0; i < NF(camp); i++) {
        use_mock();
        mock_add_fault(&g_mock, camp[i].k, camp[i].off, camp[i].mask);
        s = run_suite(SPI0, SPI0_N, report, sizeof report);
        printf("  [fault %d] pass=%d fail=%d skip=%d\n", i, s.pass, s.fail, s.skip);
        print_report(report);
        char *nl = strchr(report, '\n');
        size_t first_len = nl ? (size_t)(nl - report) : strlen(report);
        CHECK(s.fail == camp[i].nfail && s.pass + s.fail == 15);
        CHECK(first_len == strlen(camp[i].first) && strncmp(report, camp[i].first, first_len) == 0);
    }

    /* 같은 스위트를 raw MMIO 백엔드로 — 여기서는 창 뒤가 그냥 RAM 이다 (예: 베이스 주소를 잘못 줌) */
    static uint32_t window[8];
    mmio_ctx_t mc = { window, sizeof window, SPI0, SPI0_N };
    bus_ops_t raw = { "raw-mmio", &mc, mmio_read32, mmio_write32, NULL, mmio_reset };
    g_bus = &raw; memset(&g_acc, 0, sizeof g_acc);
    s = run_suite(SPI0, SPI0_N, report, sizeof report);
    printf("  [raw RAM] pass=%d fail=%d skip=%d\n", s.pass, s.fail, s.skip);
    print_report(report);
    CHECK(s.pass == 3 && s.fail == 8 && s.skip == 4);           /* RAM 은 레지스터 파일이 아니다 */
    {
        const char *want = "FAIL reg=ID.REV off=0x000 acc=RO step=write-ignored exp=0x00000002 act=0x000000FD";
        CHECK(strncmp(report, want, strlen(want)) == 0);
    }
    CHECK(reg_read32(0x020) == BUS_DEAD && bus_err_take() == BUS_DECERR);  /* 창 밖 */

    /* trace 데코레이터: W1C 테스트가 버스에서 어떻게 보이는지 */
    use_mock();
    trace_ctx_t tc = { &g_mock_bus, "" };
    bus_ops_t traced = { "trace", &tc, tr_read32, tr_write32, tr_hw_set, NULL };
    g_bus = &traced;
    CHECK(test_field(&SPI0[R_INT_STAT], &SPI0[R_INT_STAT].fields[0], &o) == T_PASS);
    printf("  trace: %s\n", tc.buf);
    CHECK(strcmp(tc.buf, "H 00C|=00000001 R 00C=00000001 W 00C=00000000 R 00C=00000001 "
                         "W 00C=00000001 R 00C=00000000") == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
