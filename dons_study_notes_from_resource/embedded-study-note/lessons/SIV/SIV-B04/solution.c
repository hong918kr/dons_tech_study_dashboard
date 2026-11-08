/* SIV-B04 Read/Write 테스트와 Access Type별 기대값
 *
 * 레지스터 R/W 테스트는 "써 보고 읽어 본다" 가 전부처럼 보이지만, 필드의 access type
 * (RW/RO/WO/W1C/W1S/RC)에 따라 "읽혀야 하는 값" 이 전부 다르다. 이 파일은
 *   ① 기술서(SIV-B01을 축약한 C 표)로부터 write 후 기대 read 값과 비교 마스크(care)를 계산하고
 *   ② 0x00000000 / 0xFFFFFFFF / 0x55555555 / 0xAAAAAAAA 패턴으로 R/W 테스트를 돌리고
 *   ③ mock 에 결함(RO 가 써짐, W1C 가 안 지워짐 …)을 주입해 테스트가 레지스터·비트 단위로 잡는지 본다.
 * mock MMIO 는 SIV-B02 의 mock 을 축약해 넣었다. 실보드에서는 reg_read32/reg_write32 가
 * *(volatile uint32_t *)(BASE + off) 접근(또는 JTAG 메모리 접근)으로 바뀐다.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ================= 레지스터 기술서 (SIV-B01 의 JSON → C 표를 축약) ================= */
typedef enum { AT_RW, AT_RO, AT_WO, AT_W1C, AT_W1S, AT_RC } access_t;
static const char *const AT_NAME[] = { "RW", "RO", "WO", "W1C", "W1S", "RC" };

#define F_VOL  0x1u   /* volatile: HW 가 언제든 바꾼다 → 값 비교에서 제외               */
#define F_SIDE 0x2u   /* side effect: 1 을 쓰면 동작이 시작된다 → 패턴 쓰기 금지(항상 0) */

typedef struct { const char *name; uint8_t lsb, width; access_t acc; uint8_t flags; } field_t;
typedef struct { const char *name; uint32_t off, reset; int nf; field_t f[4]; } reg_desc_t;

/* 가상의 DMA 채널 IP "DMA0". 필드로 정의되지 않은 비트 = reserved(RES0: 0 을 쓰고, 읽은 값은 비교 안 함) */
static const reg_desc_t DMA0[] = {
    { "ID",         0x000, 0x0D3A0102u, 1, { { "ID",       0, 32, AT_RO,  0      } } },
    { "CTRL",       0x004, 0x00000000u, 4, { { "EN",       0,  1, AT_RW,  F_SIDE },
                                             { "MODE",     1,  2, AT_RW,  0      },
                                             { "PRIO",     4,  2, AT_RW,  0      },
                                             { "BURST",    8,  4, AT_RW,  0      } } },
    { "STATUS",     0x008, 0x00000000u, 3, { { "BUSY",     0,  1, AT_RO,  F_VOL  },
                                             { "FIFO_LVL", 4,  4, AT_RO,  F_VOL  },
                                             { "ERR_CODE", 8,  4, AT_RO,  0      } } },
    { "INT_STATUS", 0x00C, 0x00000000u, 3, { { "DONE",     0,  1, AT_W1C, 0      },
                                             { "ERR",      1,  1, AT_W1C, 0      },
                                             { "HALF",     2,  1, AT_W1C, 0      } } },
    { "INT_EN",     0x010, 0x00000000u, 1, { { "EN",       0,  3, AT_RW,  0      } } },
    { "SRC_ADDR",   0x014, 0x00000000u, 2, { { "ALIGN",    0,  2, AT_RO,  0      },
                                             { "ADDR",     2, 30, AT_RW,  0      } } },
    { "LEN",        0x018, 0x00000000u, 1, { { "LEN",      0, 16, AT_RW,  0      } } },
    { "CMD",        0x01C, 0x00000000u, 3, { { "START",    0,  1, AT_WO,  F_SIDE },
                                             { "ABORT",    1,  1, AT_WO,  F_SIDE },
                                             { "TAG",      8,  8, AT_WO,  0      } } },
    { "ERR_CNT",    0x020, 0x00000000u, 1, { { "CNT",      0,  8, AT_RC,  0      } } },
    { "SW_EVT",     0x024, 0x00000000u, 1, { { "EVT",      0,  8, AT_W1S, 0      } } },
    { "SCRATCH",    0x028, 0x00000000u, 1, { { "DATA",     0, 32, AT_RW,  0      } } },
};
#define NREG ((int)(sizeof DMA0 / sizeof DMA0[0]))

/* ================= Mock MMIO + 결함 주입 (SIV-B02 축약) — 주어진 코드, 고치지 않는다 ================= */
typedef enum { FLT_RO_WRITABLE, FLT_W1C_NOCLEAR, FLT_W1C_AS_RW, FLT_W1C_CLEAR_ALL,
               FLT_WO_READBACK, FLT_RC_NOCLEAR, FLT_W1S_AS_RW } fault_kind_t;
typedef struct { fault_kind_t kind; uint32_t off, mask; } fault_t;

static uint32_t g_store[NREG];      /* "플립플롭" — reserved 비트는 항상 0 */
static fault_t  g_flt[4];
static int      g_nflt;
static int      g_side_hits;        /* F_SIDE 필드에 1 이 쓰인 횟수 (DMA 가 엉뚱하게 출발한 횟수) */

static int reg_index(uint32_t off) {
    for (int i = 0; i < NREG; i++) if (DMA0[i].off == off) return i;
    return -1;
}
static uint32_t mock_fmask(const field_t *f) {            /* mock 전용 — 학생 TODO 와 독립 */
    return (uint32_t)((((uint64_t)1 << f->width) - 1u) << f->lsb);
}
static uint32_t mock_acc_mask(const reg_desc_t *r, access_t a) {
    uint32_t m = 0;
    for (int i = 0; i < r->nf; i++) if (r->f[i].acc == a) m |= mock_fmask(&r->f[i]);
    return m;
}
static uint32_t flt_mask(uint32_t off, fault_kind_t k) {
    uint32_t m = 0;
    for (int i = 0; i < g_nflt; i++) if (g_flt[i].off == off && g_flt[i].kind == k) m |= g_flt[i].mask;
    return m;
}
static void mock_reset(void) {                            /* 전원 리셋 + 결함 제거 */
    for (int i = 0; i < NREG; i++) g_store[i] = DMA0[i].reset;
    g_nflt = 0; g_side_hits = 0;
}
static void mock_inject(fault_kind_t k, uint32_t off, uint32_t mask) {
    if (g_nflt < 4) g_flt[g_nflt++] = (fault_t){ k, off, mask };
}
/* 백도어: HW 이벤트(또는 IP 의 force/test 레지스터)가 상태 비트를 세운다 */
static void mock_hw_set(uint32_t off, uint32_t bits) {
    int i = reg_index(off);
    if (i < 0) return;
    uint32_t defined = 0;
    for (int k = 0; k < DMA0[i].nf; k++) defined |= mock_fmask(&DMA0[i].f[k]);
    g_store[i] |= bits & defined;
}
static uint32_t reg_read32(uint32_t off) {
    int i = reg_index(off);
    if (i < 0) return 0xDEADBEEFu;                        /* 디코드 안 되는 주소: 버스 에러 패턴 */
    const reg_desc_t *r = &DMA0[i];
    uint32_t v   = g_store[i];
    uint32_t ret = v & ~(mock_acc_mask(r, AT_WO) & ~flt_mask(off, FLT_WO_READBACK));  /* WO = RAZ */
    v &= ~(mock_acc_mask(r, AT_RC) & ~flt_mask(off, FLT_RC_NOCLEAR));                  /* RC: 읽으면 0 */
    for (int k = 0; k < r->nf; k++) {                     /* volatile 필드: 읽을 때마다 HW 가 움직인다 */
        const field_t *f = &r->f[k];
        if (!(f->flags & F_VOL)) continue;
        uint32_t m = mock_fmask(f);
        v = (v & ~m) | (((((v & m) >> f->lsb) + 1u) << f->lsb) & m);
    }
    g_store[i] = v;
    return ret;
}
static void reg_write32(uint32_t off, uint32_t w) {
    int i = reg_index(off);
    if (i < 0) return;
    const reg_desc_t *r = &DMA0[i];
    uint32_t cur = g_store[i], nv = 0;
    for (int k = 0; k < r->nf; k++) {
        const field_t *f = &r->f[k];
        uint32_t m = mock_fmask(f);
        if ((f->flags & F_SIDE) && (w & m)) g_side_hits++;
        switch (f->acc) {
        case AT_RW: case AT_WO: nv |= w & m;          break;
        case AT_RO: case AT_RC: nv |= cur & m;        break;
        case AT_W1C:            nv |= cur & ~w & m;   break;
        case AT_W1S:            nv |= (cur | w) & m;  break;
        }
    }
    uint32_t m;
    m = flt_mask(off, FLT_RO_WRITABLE);   nv = (nv & ~m) | (w & m);    /* RO 에 write-enable 이 붙음   */
    m = flt_mask(off, FLT_W1C_NOCLEAR);   nv = (nv & ~m) | (cur & m);  /* clear 경로가 끊김           */
    m = flt_mask(off, FLT_W1C_AS_RW);     nv = (nv & ~m) | (w & m);    /* W1C 를 RW 로 구현           */
    m = flt_mask(off, FLT_W1C_CLEAR_ALL); nv &= ~m;                    /* 어떤 write 든 지움 (WC)     */
    m = flt_mask(off, FLT_W1S_AS_RW);     nv = (nv & ~m) | (w & m);    /* W1S 를 RW 로 구현           */
    g_store[i] = nv;
}
/* 실보드에선 IP 의 INT_FORCE/테스트 모드 레지스터 쓰기, 또는 실제 이벤트 유발 */
static void (*const hw_force)(uint32_t off, uint32_t bits) = mock_hw_set;

/* ================= 실패 기록 ================= */
typedef struct {
    const reg_desc_t *r;
    uint32_t pat;     /* 테스트 패턴           */
    uint32_t wr;      /* 실제로 쓴 값          */
    uint32_t exp;     /* 기대 read 값          */
    uint32_t act;     /* 실제 read 값          */
    uint32_t care;    /* 비교한 비트           */
} fail_t;

static const uint32_t PATS[4] = { 0x00000000u, 0xFFFFFFFFu, 0x55555555u, 0xAAAAAAAAu };

/* ================= Part A: 기대값 모델 ================= */
/* A1: 필드의 비트 마스크. width 는 1..32 */
static uint32_t field_mask(const field_t *f) {
    uint32_t ones = (f->width >= 32) ? 0xFFFFFFFFu : ((1u << f->width) - 1u);
    return ones << f->lsb;
}

/* A2: write 직전에 읽은 값 before, 실제로 쓴 값 wr 일 때 write 직후 read 의 기대값 *exp 와
 *     비교할 비트 *care. reserved 비트와 F_VOL 필드는 care=0, exp=0.
 *     RW: 쓴 값 / RO: before 그대로 / WO: 0 (read-as-zero) / W1C: 1 쓴 자리만 0 /
 *     W1S: 1 쓴 자리만 1 / RC: before 를 읽는 순간 이미 지워졌으므로 0 */
static void expect_after_write(const reg_desc_t *r, uint32_t before, uint32_t wr,
                               uint32_t *exp, uint32_t *care) {
    uint32_t e = 0, c = 0;
    for (int i = 0; i < r->nf; i++) {
        const field_t *f = &r->f[i];
        uint32_t m = field_mask(f);
        if (f->flags & F_VOL) continue;
        switch (f->acc) {
        case AT_RW:  e |= wr & m;              break;
        case AT_RO:  e |= before & m;          break;
        case AT_WO:                            break;  /* RAZ */
        case AT_W1C: e |= before & ~wr & m;    break;
        case AT_W1S: e |= (before | wr) & m;   break;
        case AT_RC:                            break;  /* 앞선 read 가 지웠다 */
        }
        c |= m;
    }
    *exp = e; *care = c;
}

/* ================= Part B: 안전한 패턴 쓰기와 R/W 테스트 ================= */
/* B1: 패턴 pat 를 이 레지스터에 쓸 때 실제로 쓸 값. reserved 비트와 F_SIDE 필드에는 0. */
static uint32_t safe_write_value(const reg_desc_t *r, uint32_t pat) {
    uint32_t allowed = 0;
    for (int i = 0; i < r->nf; i++)
        if (!(r->f[i].flags & F_SIDE)) allowed |= field_mask(&r->f[i]);
    return pat & allowed;
}

/* B2: 패턴마다 [prime → before=read → write(safe) → act=read → 비교].
 *     prime 이면 W1C/RC 필드(F_VOL 제외)를 hw_force 로 먼저 1 로 세운다.
 *     실패를 out[0..maxf-1] 에 기록하고, 총 실패 수를 돌려준다. */
static int rw_test_reg(const reg_desc_t *r, const uint32_t *pats, int npat, bool prime,
                       fail_t *out, int maxf) {
    uint32_t pm = 0;
    for (int i = 0; i < r->nf; i++) {
        const field_t *f = &r->f[i];
        if ((f->acc == AT_W1C || f->acc == AT_RC) && !(f->flags & F_VOL)) pm |= field_mask(f);
    }
    int nfail = 0;
    for (int p = 0; p < npat; p++) {
        if (prime && pm) hw_force(r->off, pm);
        uint32_t before = reg_read32(r->off);
        uint32_t wr = safe_write_value(r, pats[p]);
        reg_write32(r->off, wr);
        uint32_t act = reg_read32(r->off);
        uint32_t exp, care;
        expect_after_write(r, before, wr, &exp, &care);
        if ((act ^ exp) & care) {
            if (nfail < maxf) out[nfail] = (fail_t){ r, pats[p], wr, exp, act, care };
            nfail++;
        }
    }
    return nfail;
}

/* ================= Part C: 디버그 엔지니어가 바로 쓸 수 있는 리포트 ================= */
/* C1: 한 줄 리포트. 예)
 *   FAIL reg=INT_STATUS off=0x00C pat=0xFFFFFFFF wr=0x00000007 exp=0x00000000 act=0x00000002
 *        diff=0x00000002 bits=[1] field=ERR(W1C)          (실제로는 한 줄)
 *   bits: 오름차순, 연속 구간은 "hi:lo" (예: [15:8], [1,3], [2:0])
 *   field: diff 와 겹치는 필드를 기술서 순서대로 "이름(ACCESS)" 콤마 구분
 *   반환: snprintf 처럼 쓴 글자 수 */
static int format_fail(const fail_t *f, char *buf, size_t n) {
    uint32_t diff = (f->exp ^ f->act) & f->care;
    char bits[160] = "", flds[160] = "";
    size_t bl = 0, fl = 0;
    for (int b = 0; b < 32; ) {
        if (!(diff >> b & 1u)) { b++; continue; }
        int lo = b;
        while (b < 32 && (diff >> b & 1u)) b++;
        int hi = b - 1;
        bl += (size_t)snprintf(bits + bl, sizeof bits - bl, "%s", bl ? "," : "");
        if (hi == lo) bl += (size_t)snprintf(bits + bl, sizeof bits - bl, "%d", lo);
        else          bl += (size_t)snprintf(bits + bl, sizeof bits - bl, "%d:%d", hi, lo);
    }
    for (int i = 0; i < f->r->nf; i++) {
        const field_t *fd = &f->r->f[i];
        if (!(diff & field_mask(fd))) continue;
        fl += (size_t)snprintf(flds + fl, sizeof flds - fl, "%s%s(%s)", fl ? "," : "",
                               fd->name, AT_NAME[fd->acc]);
    }
    return snprintf(buf, n, "FAIL reg=%s off=0x%03X pat=0x%08X wr=0x%08X exp=0x%08X act=0x%08X "
                    "diff=0x%08X bits=[%s] field=%s",
                    f->r->name, (unsigned)f->r->off, (unsigned)f->pat, (unsigned)f->wr,
                    (unsigned)f->exp, (unsigned)f->act, (unsigned)diff, bits, flds);
}

/* C2: 가장 낮은 실패 비트의 필드 access type 과 방향(exp/act)으로 원인 추정 문장을 고른다.
 *   RO                    → "RO field took a write (write-enable on RO / tie-off wired to a flop?)"
 *   W1C  exp=0 act=1      → "W1C bit did not clear on write-1 (clear path broken?)"
 *   W1C  exp=1 act=0      → "W1C bit cleared without write-1 (implemented as RW or any-write-clears?)"
 *   W1S  exp=1 act=0      → "W1S bit lost on write-0 (implemented as RW?)"
 *   W1S  exp=0 act=1      → "W1S bit set without write-1"
 *   WO                    → "WO field not read-as-zero (write data leaks to read path)"
 *   RC                    → "RC field not cleared by read (read side effect missing)"
 *   RW   act=1 / act=0    → "RW bit reads 1 against expected 0 (stuck-at-1? see SIV-B05)" /
 *                           "RW bit reads 0 against expected 1 (stuck-at-0? see SIV-B05)"
 *   그 외(실패 없음 등)    → "no failing bit" */
static const char *diagnose(const fail_t *f) {
    uint32_t diff = (f->exp ^ f->act) & f->care;
    if (!diff) return "no failing bit";
    int b = 0;
    while (!(diff >> b & 1u)) b++;
    bool act1 = (f->act >> b) & 1u;
    for (int i = 0; i < f->r->nf; i++) {
        const field_t *fd = &f->r->f[i];
        if (!(field_mask(fd) >> b & 1u)) continue;
        switch (fd->acc) {
        case AT_RO:  return "RO field took a write (write-enable on RO / tie-off wired to a flop?)";
        case AT_W1C: return act1 ? "W1C bit did not clear on write-1 (clear path broken?)"
                                 : "W1C bit cleared without write-1 (implemented as RW or any-write-clears?)";
        case AT_W1S: return act1 ? "W1S bit set without write-1"
                                 : "W1S bit lost on write-0 (implemented as RW?)";
        case AT_WO:  return "WO field not read-as-zero (write data leaks to read path)";
        case AT_RC:  return "RC field not cleared by read (read side effect missing)";
        case AT_RW:  return act1 ? "RW bit reads 1 against expected 0 (stuck-at-1? see SIV-B05)"
                                 : "RW bit reads 0 against expected 1 (stuck-at-0? see SIV-B05)";
        }
    }
    return "no failing bit";
}

/* 블록 전체 R/W 테스트 + 리포트 (주어진 코드) */
static int rw_test_block(bool prime, char *rep, size_t n) {
    size_t len = 0; int total = 0;
    rep[0] = '\0';
    for (int i = 0; i < NREG; i++) {
        fail_t fl[8];
        int nf = rw_test_reg(&DMA0[i], PATS, 4, prime, fl, 8);
        for (int k = 0; k < nf && k < 8; k++) {
            char line[320];
            format_fail(&fl[k], line, sizeof line);
            if (len < n) len += (size_t)snprintf(rep + len, n - len, "%s\n  -> %s\n", line, diagnose(&fl[k]));
        }
        total += nf;
    }
    if (len < n) snprintf(rep + len, n - len, "DMA0 rw_test: %d regs x 4 patterns, %d failure(s)\n", NREG, total);
    return total;
}

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static const reg_desc_t *R(const char *name) {
    for (int i = 0; i < NREG; i++) if (strcmp(DMA0[i].name, name) == 0) return &DMA0[i];
    return NULL;
}
static bool exp_is(const char *reg, uint32_t before, uint32_t wr, uint32_t want_exp, uint32_t want_care) {
    uint32_t e = 0xBADu, c = 0xBADu;
    expect_after_write(R(reg), before, wr, &e, &c);
    return e == want_exp && c == want_care;
}
static bool line_is(const fail_t *f, const char *want) {
    char buf[320];
    format_fail(f, buf, sizeof buf);
    if (strcmp(buf, want) != 0) { printf("  got : %s\n  want: %s\n", buf, want); return false; }
    return true;
}
/* 결함 하나를 주입하고 패턴 하나만 (리셋 직후, prime 켜고) 돌렸을 때 잡히나 */
static bool detects(fault_kind_t k, const char *reg, uint32_t mask, uint32_t pat) {
    fail_t fl[1];
    mock_reset();
    mock_inject(k, R(reg)->off, mask);
    return rw_test_reg(R(reg), &pat, 1, true, fl, 1) > 0;
}

int main(void) {
    /* ---- Part A1: field_mask ---- */
    CHECK(field_mask(&(field_t){ "a", 0, 1, AT_RW, 0 }) == 0x00000001u);
    CHECK(field_mask(&(field_t){ "b", 8, 4, AT_RW, 0 }) == 0x00000F00u);
    CHECK(field_mask(&(field_t){ "c", 0, 32, AT_RW, 0 }) == 0xFFFFFFFFu);   /* 1u<<32 은 UB */
    CHECK(field_mask(&(field_t){ "d", 2, 30, AT_RW, 0 }) == 0xFFFFFFFCu);
    CHECK(field_mask(&(field_t){ "e", 31, 1, AT_RW, 0 }) == 0x80000000u);

    /* ---- Part A2: access type 별 기대값 ---- */
    CHECK(exp_is("CTRL",       0x00000000u, 0xFFFFFFFFu, 0x00000F37u, 0x00000F37u));  /* RW, reserved 제외 */
    CHECK(exp_is("CTRL",       0xFFFFFFFFu, 0x00000000u, 0x00000000u, 0x00000F37u));  /* before 의 reserved 가 새면 안 됨 */
    CHECK(exp_is("ID",         0x0D3A0102u, 0xFFFFFFFFu, 0x0D3A0102u, 0xFFFFFFFFu));  /* RO = before */
    CHECK(exp_is("STATUS",     0x000005F1u, 0xFFFFFFFFu, 0x00000500u, 0x00000F00u));  /* volatile 제외 */
    CHECK(exp_is("INT_STATUS", 0x00000007u, 0x00000005u, 0x00000002u, 0x00000007u));  /* W1C */
    CHECK(exp_is("INT_STATUS", 0x00000007u, 0x00000000u, 0x00000007u, 0x00000007u));  /* 0 쓰기는 무효 */
    CHECK(exp_is("CMD",        0x0000FF03u, 0x0000FF03u, 0x00000000u, 0x0000FF03u));  /* WO = RAZ */
    CHECK(exp_is("ERR_CNT",    0x0000003Cu, 0x000000FFu, 0x00000000u, 0x000000FFu));  /* RC */
    CHECK(exp_is("SW_EVT",     0x0000000Fu, 0x00000050u, 0x0000005Fu, 0x000000FFu));  /* W1S */
    CHECK(exp_is("SRC_ADDR",   0x00000000u, 0xFFFFFFFFu, 0xFFFFFFFCu, 0xFFFFFFFFu));  /* RO 정렬 비트 */

    /* ---- Part B1: 안전한 쓰기 값 ---- */
    CHECK(safe_write_value(R("CTRL"), 0xFFFFFFFFu) == 0x00000F36u);   /* EN(side) 과 reserved 빼고 */
    CHECK(safe_write_value(R("CMD"), 0xFFFFFFFFu) == 0x0000FF00u);    /* START/ABORT 는 절대 1 금지 */
    CHECK(safe_write_value(R("ID"), 0xFFFFFFFFu) == 0xFFFFFFFFu);     /* RO 에는 써 봐야 한다 */
    CHECK(safe_write_value(R("STATUS"), 0xFFFFFFFFu) == 0x00000FF1u);
    CHECK(safe_write_value(R("INT_STATUS"), 0x55555555u) == 0x00000005u);
    CHECK(safe_write_value(R("SRC_ADDR"), 0xAAAAAAAAu) == 0xAAAAAAAAu);

    /* ---- Part B2: 정상 mock 은 통과, 결함은 잡는다 ---- */
    fail_t fl[8];
    {
        mock_reset();
        int total = 0;
        for (int i = 0; i < NREG; i++) total += rw_test_reg(&DMA0[i], PATS, 4, true, fl, 8);
        CHECK(total == 0);                                     /* 정상 칩: 실패 0 */
        CHECK(g_side_hits == 0);                               /* START/ABORT/EN 에 1 을 쓴 적 없음 */
        mock_reset();
        CHECK(reg_read32(0x008) != reg_read32(0x008));         /* STATUS 는 정말 움직인다 (volatile) */
    }
    mock_reset(); mock_inject(FLT_W1C_NOCLEAR, 0x00C, 0x2u);
    CHECK(rw_test_reg(R("INT_STATUS"), PATS, 4, false, fl, 8) == 0);   /* prime 없으면 못 잡는다! */
    mock_reset(); mock_inject(FLT_W1C_NOCLEAR, 0x00C, 0x2u);
    CHECK(rw_test_reg(R("INT_STATUS"), PATS, 4, true, fl, 8) == 2);
    CHECK(fl[0].pat == 0xFFFFFFFFu && fl[0].exp == 0x0u && fl[0].act == 0x2u);  /* bit1 만 남음 */
    CHECK(fl[1].pat == 0xAAAAAAAAu);
    mock_reset(); mock_inject(FLT_RO_WRITABLE, 0x000, 0x8u);
    CHECK(rw_test_reg(R("ID"), PATS, 4, true, fl, 8) == 3);             /* F, 5(되돌림), A */
    CHECK(fl[0].pat == 0xFFFFFFFFu && fl[0].act == 0x0D3A010Au);
    CHECK(fl[1].pat == 0x55555555u && fl[1].exp == 0x0D3A010Au && fl[1].act == 0x0D3A0102u);
    mock_reset(); mock_inject(FLT_RC_NOCLEAR, 0x020, 0xFFu);
    CHECK(rw_test_reg(R("ERR_CNT"), PATS, 4, false, fl, 8) == 0);      /* RC 도 prime 필요 */
    mock_reset(); mock_inject(FLT_RC_NOCLEAR, 0x020, 0xFFu);
    CHECK(rw_test_reg(R("ERR_CNT"), PATS, 4, true, fl, 8) == 4);
    mock_reset(); mock_inject(FLT_W1S_AS_RW, 0x024, 0xFFu);
    CHECK(rw_test_reg(R("SW_EVT"), PATS, 4, true, fl, 8) == 2);        /* F 다음 5 에서야 드러남 */
    CHECK(fl[0].pat == 0x55555555u && fl[0].exp == 0xFFu && fl[0].act == 0x55u);
    mock_reset(); mock_inject(FLT_W1C_AS_RW, 0x00C, 0x7u);
    CHECK(rw_test_reg(R("INT_STATUS"), PATS, 4, true, fl, 1) == 4);    /* 총 개수는 maxf 와 무관 */

    /* ---- Part C1: 리포트 한 줄 ---- */
    CHECK(line_is(&(fail_t){ R("INT_STATUS"), 0xFFFFFFFFu, 0x7u, 0x0u, 0x2u, 0x7u },
        "FAIL reg=INT_STATUS off=0x00C pat=0xFFFFFFFF wr=0x00000007 exp=0x00000000 act=0x00000002 "
        "diff=0x00000002 bits=[1] field=ERR(W1C)"));
    CHECK(line_is(&(fail_t){ R("CMD"), 0xFFFFFFFFu, 0xFF00u, 0x0u, 0xFF00u, 0xFF03u },
        "FAIL reg=CMD off=0x01C pat=0xFFFFFFFF wr=0x0000FF00 exp=0x00000000 act=0x0000FF00 "
        "diff=0x0000FF00 bits=[15:8] field=TAG(WO)"));
    CHECK(line_is(&(fail_t){ R("INT_STATUS"), 0x0u, 0x0u, 0x7u, 0x0u, 0x7u },
        "FAIL reg=INT_STATUS off=0x00C pat=0x00000000 wr=0x00000000 exp=0x00000007 act=0x00000000 "
        "diff=0x00000007 bits=[2:0] field=DONE(W1C),ERR(W1C),HALF(W1C)"));
    CHECK(line_is(&(fail_t){ R("SCRATCH"), 0xAAAAAAAAu, 0xAAAAAAAAu, 0xAAAAAAAAu, 0xAAAAA0A0u, 0xFFFFFFFFu },
        "FAIL reg=SCRATCH off=0x028 pat=0xAAAAAAAA wr=0xAAAAAAAA exp=0xAAAAAAAA act=0xAAAAA0A0 "
        "diff=0x00000A0A bits=[1,3,9,11] field=DATA(RW)"));
    CHECK(line_is(&(fail_t){ R("STATUS"), 0xFFFFFFFFu, 0xFF1u, 0x500u, 0x5F1u, 0xF00u },   /* volatile 차이는 무시 */
        "FAIL reg=STATUS off=0x008 pat=0xFFFFFFFF wr=0x00000FF1 exp=0x00000500 act=0x000005F1 "
        "diff=0x00000000 bits=[] field="));

    /* ---- Part C2: 원인 추정 ---- */
    CHECK(strcmp(diagnose(&(fail_t){ R("ID"), 0xFFFFFFFFu, 0xFFFFFFFFu, 0x0D3A0102u, 0x0D3A010Au, ~0u }),
                 "RO field took a write (write-enable on RO / tie-off wired to a flop?)") == 0);
    CHECK(strcmp(diagnose(&(fail_t){ R("INT_STATUS"), 0xFFFFFFFFu, 0x7u, 0x0u, 0x2u, 0x7u }),
                 "W1C bit did not clear on write-1 (clear path broken?)") == 0);
    CHECK(strcmp(diagnose(&(fail_t){ R("INT_STATUS"), 0x0u, 0x0u, 0x7u, 0x0u, 0x7u }),
                 "W1C bit cleared without write-1 (implemented as RW or any-write-clears?)") == 0);
    CHECK(strcmp(diagnose(&(fail_t){ R("SW_EVT"), 0x55555555u, 0x55u, 0xFFu, 0x55u, 0xFFu }),
                 "W1S bit lost on write-0 (implemented as RW?)") == 0);
    CHECK(strcmp(diagnose(&(fail_t){ R("CMD"), 0xFFFFFFFFu, 0xFF00u, 0x0u, 0xFF00u, 0xFF03u }),
                 "WO field not read-as-zero (write data leaks to read path)") == 0);
    CHECK(strcmp(diagnose(&(fail_t){ R("ERR_CNT"), 0x0u, 0x0u, 0x0u, 0xFFu, 0xFFu }),
                 "RC field not cleared by read (read side effect missing)") == 0);
    CHECK(strcmp(diagnose(&(fail_t){ R("SCRATCH"), 0x0u, 0x0u, 0x0u, 0x10u, ~0u }),
                 "RW bit reads 1 against expected 0 (stuck-at-1? see SIV-B05)") == 0);
    CHECK(strcmp(diagnose(&(fail_t){ R("STATUS"), 0x0u, 0x0u, 0x500u, 0x5F1u, 0xF00u }),
                 "no failing bit") == 0);

    /* ---- Part C3: 결함 × 패턴 검출 매트릭스 (패턴 1개, 리셋 직후, prime) ---- */
    static const struct { fault_kind_t k; const char *reg; uint32_t mask; bool det[4]; } MX[] = {
        /*                                         0x0    0xF..  0x55.. 0xAA.. */
        { FLT_RO_WRITABLE,   "ID",         0x08u, { false, true,  false, true  } },
        { FLT_W1C_NOCLEAR,   "INT_STATUS", 0x02u, { false, true,  false, true  } },
        { FLT_W1C_AS_RW,     "INT_STATUS", 0x02u, { true,  true,  true,  true  } },
        { FLT_W1C_CLEAR_ALL, "INT_STATUS", 0x07u, { true,  false, true,  true  } },
        { FLT_WO_READBACK,   "CMD",        0xFF00u,{ false, true,  true,  true  } },
        { FLT_RC_NOCLEAR,    "ERR_CNT",    0xFFu, { true,  true,  true,  true  } },
        { FLT_W1S_AS_RW,     "SW_EVT",     0xFFu, { false, false, false, false } },  /* 단독 패턴으론 불가 */
    };
    for (size_t i = 0; i < sizeof MX / sizeof MX[0]; i++)
        for (int p = 0; p < 4; p++)
            CHECK(detects(MX[i].k, MX[i].reg, MX[i].mask, PATS[p]) == MX[i].det[p]);

    /* ---- Part C4: DMA0 블록 전체 — 과제의 두 결함 주입 ---- */
    {
        static char rep[4096];
        mock_reset();
        CHECK(rw_test_block(true, rep, sizeof rep) == 0);
        mock_reset();
        mock_inject(FLT_RO_WRITABLE, 0x000, 0x8u);        /* ID[3]: tie-off 대신 flop */
        mock_inject(FLT_W1C_NOCLEAR, 0x00C, 0x2u);        /* INT_STATUS.ERR clear 경로 끊김 */
        int nf = rw_test_block(true, rep, sizeof rep);
        printf("%s", rep);
        CHECK(nf == 5);
        CHECK(strstr(rep, "FAIL reg=ID off=0x000 pat=0xFFFFFFFF wr=0xFFFFFFFF exp=0x0D3A0102 "
                          "act=0x0D3A010A diff=0x00000008 bits=[3] field=ID(RO)") != NULL);
        CHECK(strstr(rep, "FAIL reg=INT_STATUS off=0x00C pat=0xAAAAAAAA wr=0x00000002 exp=0x00000005 "
                          "act=0x00000007 diff=0x00000002 bits=[1] field=ERR(W1C)") != NULL);
        CHECK(strstr(rep, "-> W1C bit did not clear on write-1") != NULL);
        CHECK(g_side_hits == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
