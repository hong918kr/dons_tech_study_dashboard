/* SIV-M14 ECC 검증: 오류 주입과 CE/UE 보고 확인
 *
 * 64비트 데이터 + 8비트 sideband ECC(SECDED(72,64))를 가진 가상의 DDR 메모리 컨트롤러 DMC0 의 mock 위에서
 * error-inject 레지스터로 1비트(데이터/체크)·2비트 오류를 심고 → RD_BYPASS 읽기로 "주입이 실제로 들어갔는지"
 * 확인하고 → 일반 읽기의 STATUS · 버스 응답 · 정정 데이터 · CE/UE 카운터 · 오류 주소 · 신드롬 · 인터럽트 라인을
 * 기대값과 비교하는 테스트 매트릭스를 만든다. 검증 대상은 ECC 코덱이 아니라 "ECC 가 스펙대로 보고하는가" 다.
 * 코덱은 MEM-B07 의 SECDED(72,64)를 축약해 넣었고, mock 레지스터 접근은 SIV-B02 를 축약했다.
 * mock 에는 fault_t 로 결함(정정 안 함, UE 를 CE 로 오정정, 주소 비트 누락 ...)을 주입한다.
 * 테스트는 "좋은 mock 은 통과, 결함 mock 은 정확한 단계·레지스터·비트로 실패" 를 둘 다 보여야 한다.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ================= SECDED(72,64) 코덱 (MEM-B07 축약, 주어진 코드) =================
 * 코드워드 위치 1..71: 2의 거듭제곱 위치(1,2,4,..,64)에 체크비트 c0..c6, 나머지 64 위치에 데이터 d0..d63.
 * c7 = 72비트 전체 짝수 패리티 (위치 0 취급). 신드롬 바이트 = s | p<<7
 *   s = 1 인 비트들의 위치 XOR (7비트), p = 72비트 전체 패리티
 *   s=0,p=0 정상 / p=1 → 단일 오류 (위치 s, s=0 이면 c7) / p=0,s≠0 → 2비트 = UE / p=1,s>71 → UE
 * 비트 번호(bitnum): 데이터 0..63, 체크 c0..c7 = 64..71 */
static uint8_t POS[64];      /* 데이터 비트 i → 코드워드 위치 */
static int8_t  DBIT[128];    /* 코드워드 위치 → 데이터 비트 (-1 = 데이터 위치 아님) */
static void codec_init(void) {
    int i = 0;
    for (int p = 0; p < 128; p++) DBIT[p] = -1;
    for (int p = 3; p <= 71; p++)
        if (p & (p - 1)) { POS[i] = (uint8_t)p; DBIT[p] = (int8_t)i; i++; }
}
static unsigned parity64(uint64_t x) {
    x ^= x >> 32; x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1;
    return (unsigned)(x & 1);
}
static unsigned popcount64(uint64_t x) { unsigned n = 0; for (; x; x &= x - 1) n++; return n; }
static unsigned ctz64(uint64_t x) { unsigned n = 0; while (x && !(x & 1)) { x >>= 1; n++; } return n; }
static uint8_t hsyn64(uint64_t d) {                 /* 1 인 데이터 비트들의 위치 XOR */
    uint8_t s = 0;
    for (int i = 0; i < 64; i++) if (d >> i & 1) s ^= POS[i];
    return s;
}
static uint8_t secded_encode(uint64_t d) {
    uint8_t c = hsyn64(d);                          /* c0..c6: 위치 XOR 이 0 이 되도록 */
    return (uint8_t)(c | (parity64(d) ^ parity64(c)) << 7);
}
static uint8_t secded_syndrome(uint64_t d, uint8_t c) {
    uint8_t s = (uint8_t)(hsyn64(d) ^ (c & 0x7F));
    return (uint8_t)(s | (parity64(d) ^ parity64(c)) << 7);
}
enum { ECC_OK, ECC_CE, ECC_UE, ECC_UNSPEC = -1 };
static int secded_classify(uint8_t syn, unsigned *bitnum) {
    unsigned s = syn & 0x7Fu, p = syn >> 7;
    if (!p) return s ? ECC_UE : ECC_OK;
    if (s == 0) { *bitnum = 71; return ECC_CE; }
    if ((s & (s - 1)) == 0) { *bitnum = 64 + ctz64(s); return ECC_CE; }
    if (DBIT[s] >= 0) { *bitnum = (unsigned)DBIT[s]; return ECC_CE; }
    return ECC_UE;                                  /* s > 71: 홀수 개 다중 오류 */
}

/* ================= DMC0 레지스터 맵 ================= */
#define IP_NAME      "DMC0"
#define ECC_CTRL     0x000u  /* RW : bit0 ECC_EN, bit1 RD_BYPASS, bit2 CE_IRQ_EN, bit3 UE_IRQ_EN          */
#define ECC_STATUS   0x004u  /* W1C: bit0 CE, bit1 UE, bit2 CE_OVF, bit3 UE_OVF (sticky)                   */
#define ECC_CNT      0x008u  /* RO : [15:0] CE 횟수, [31:16] UE 횟수 — 0xFFFF 에서 포화                     */
#define ECC_CLR      0x00Cu  /* WO : bit0 CE 카운터 0, bit1 UE 카운터 0. 읽으면 0                           */
#define CE_ADDR      0x010u  /* RO : STATUS.CE 가 0→1 될 때의 주소 (첫 오류 캡처, CE W1C 로 재무장)          */
#define CE_SYND      0x014u  /* RO : [7:0] 신드롬 바이트, [15:8] 정정한 비트 번호                           */
#define UE_ADDR      0x018u  /* RO : STATUS.UE 가 0→1 될 때의 주소                                          */
#define UE_SYND      0x01Cu  /* RO : [7:0] 신드롬 바이트                                                    */
#define INJ_CTRL     0x020u  /* RW : bit0 INJ_EN — INJ_ADDR 에 대한 다음 쓰기 1회에 마스크 XOR, 그 뒤 자동 0 */
#define INJ_ADDR     0x024u
#define INJ_MASK_LO  0x028u  /* 데이터 [31:0] 중 뒤집을 비트  */
#define INJ_MASK_HI  0x02Cu  /* 데이터 [63:32]                */
#define INJ_MASK_CHK 0x030u  /* 체크 [7:0]                    */
#define BYP_CHK      0x034u  /* RO : RD_BYPASS 읽기 때 저장돼 있던 체크 바이트 */
#define INTC_RAW     0x800u  /* RO : INTC 입력 라인 레벨 (입력 n = bit n) */
#define OFF_RDATA    0xF00u  /* 리포트용 의사 오프셋: 읽기 데이터 */
#define OFF_RESP     0xF04u  /* 리포트용 의사 오프셋: 버스 응답   */

#define CTRL_ECC_EN  (1u << 0)
#define CTRL_BYPASS  (1u << 1)
#define CTRL_CE_IE   (1u << 2)
#define CTRL_UE_IE   (1u << 3)
#define CTRL_RUN     (CTRL_ECC_EN | CTRL_CE_IE | CTRL_UE_IE)
#define ST_CE        (1u << 0)
#define ST_UE        (1u << 1)
#define ST_CE_OVF    (1u << 2)
#define ST_UE_OVF    (1u << 3)
#define CLR_CE       (1u << 0)
#define CLR_UE       (1u << 1)
#define INJ_EN       (1u << 0)
#define CE_IRQ       20u
#define UE_IRQ       21u
#define CE_LINE      (1u << CE_IRQ)
#define UE_LINE      (1u << UE_IRQ)
#define MEM_WORDS    64u     /* 주소 0x000..0x1F8, 8바이트 워드 */
enum { RESP_OKAY = 0, RESP_EXOKAY = 1, RESP_SLVERR = 2, RESP_DECERR = 3 };   /* AXI RRESP 인코딩 */
static const char *const RESP_NAME[] = {"OKAY", "EXOKAY", "SLVERR", "DECERR"};

/* ================= mock (주어진 코드 — 고치지 않는다) ================= */
typedef struct {
    bool     no_correct;      /* CE 를 검출·보고하지만 읽기 데이터는 정정하지 않음          */
    bool     ignore_parity;   /* 디코더가 c7(전체 패리티)를 무시 → 2비트 오류를 단일로 오정정 */
    bool     chk_as_ue;       /* 체크비트 단일 오류를 UE 로 보고                             */
    bool     ce_cnt_dead;     /* CE 카운터가 안 올라감                                       */
    bool     cnt_wrap;        /* CE 카운터가 0xFFFF 에서 포화하지 않고 0 으로 wrap           */
    uint32_t addr_drop;       /* 오류 주소 캡처에서 빠지는 주소 비트 (예: 0x100)             */
    bool     ce_capture_last; /* CE_ADDR/CE_SYND 가 첫 오류가 아니라 마지막 오류로 덮어써짐  */
    bool     ue_irq_dead;     /* UE 인터럽트 라인 미연결                                     */
    bool     ue_no_buserr;    /* UE 읽기에 SLVERR 대신 OKAY → 오염 데이터가 조용히 소비됨     */
    bool     inj_chk_dead;    /* INJ_MASK_CHK 가 무시됨 (체크비트 주입 불가)                 */
} fault_t;

static struct {
    uint64_t data[MEM_WORDS]; uint8_t chk[MEM_WORDS];
    uint32_t ctrl, status, ce_cnt, ue_cnt, ce_addr, ce_synd, ue_addr, ue_synd;
    uint32_t inj_ctrl, inj_addr, inj_lo, inj_hi, inj_chk, byp_chk;
    fault_t f; unsigned nacc;
} M;

static void mock_init(const fault_t *f) {
    memset(&M, 0, sizeof M);                        /* 리셋: ECC_EN=0, 메모리 0 (0 의 체크 바이트도 0) */
    if (f) M.f = *f;
}
static uint32_t intc_now(void) {
    uint32_t v = 0;
    if ((M.status & ST_CE) && (M.ctrl & CTRL_CE_IE)) v |= CE_LINE;
    if ((M.status & ST_UE) && (M.ctrl & CTRL_UE_IE) && !M.f.ue_irq_dead) v |= UE_LINE;
    return v;
}
static uint32_t reg_read32(uint32_t off) {
    M.nacc++;
    switch (off) {
    case ECC_CTRL:     return M.ctrl;
    case ECC_STATUS:   return M.status;
    case ECC_CNT:      return M.ue_cnt << 16 | M.ce_cnt;
    case ECC_CLR:      return 0;
    case CE_ADDR:      return M.ce_addr;
    case CE_SYND:      return M.ce_synd;
    case UE_ADDR:      return M.ue_addr;
    case UE_SYND:      return M.ue_synd;
    case INJ_CTRL:     return M.inj_ctrl;
    case INJ_ADDR:     return M.inj_addr;
    case INJ_MASK_LO:  return M.inj_lo;
    case INJ_MASK_HI:  return M.inj_hi;
    case INJ_MASK_CHK: return M.inj_chk;
    case BYP_CHK:      return M.byp_chk;
    case INTC_RAW:     return intc_now();
    default:           return 0xDEADBEEFu;
    }
}
static void reg_write32(uint32_t off, uint32_t v) {
    M.nacc++;
    switch (off) {
    case ECC_CTRL:     M.ctrl = v & 0xFu; break;
    case ECC_STATUS:   M.status &= ~(v & 0xFu); break;
    case ECC_CLR:      if (v & CLR_CE) M.ce_cnt = 0; if (v & CLR_UE) M.ue_cnt = 0; break;
    case INJ_CTRL:     M.inj_ctrl = v & INJ_EN; break;
    case INJ_ADDR:     M.inj_addr = v; break;
    case INJ_MASK_LO:  M.inj_lo = v; break;
    case INJ_MASK_HI:  M.inj_hi = v; break;
    case INJ_MASK_CHK: M.inj_chk = v & 0xFFu; break;
    default:           break;                       /* RO / 미디코드 */
    }
}
static bool word_index(uint32_t addr, unsigned *i) {
    if ((addr & 7u) || addr >= MEM_WORDS * 8u) return false;
    *i = addr >> 3;
    return true;
}
static int mem_write64(uint32_t addr, uint64_t d) {
    unsigned i;
    M.nacc++;
    if (!word_index(addr, &i)) return RESP_DECERR;
    uint8_t c = secded_encode(d);                   /* 체크 비트는 ECC_EN 과 무관하게 항상 생성 */
    if ((M.inj_ctrl & INJ_EN) && addr == M.inj_addr) {
        d ^= (uint64_t)M.inj_hi << 32 | M.inj_lo;
        if (!M.f.inj_chk_dead) c ^= (uint8_t)M.inj_chk;
        M.inj_ctrl &= ~INJ_EN;                      /* one-shot */
    }
    M.data[i] = d; M.chk[i] = c;
    return RESP_OKAY;
}
static void log_err(int cls, uint32_t addr, uint32_t synd) {
    uint32_t a = addr & ~M.f.addr_drop;
    if (cls == ECC_CE) {
        if (M.status & ST_CE) {
            M.status |= ST_CE_OVF;
            if (M.f.ce_capture_last) { M.ce_addr = a; M.ce_synd = synd; }
        } else { M.status |= ST_CE; M.ce_addr = a; M.ce_synd = synd; }
        if (!M.f.ce_cnt_dead) {
            if (M.ce_cnt < 0xFFFFu) M.ce_cnt++;
            else if (M.f.cnt_wrap) M.ce_cnt = 0;
        }
    } else {
        if (M.status & ST_UE) M.status |= ST_UE_OVF;
        else { M.status |= ST_UE; M.ue_addr = a; M.ue_synd = synd; }
        if (M.ue_cnt < 0xFFFFu) M.ue_cnt++;
    }
}
static int mem_read64(uint32_t addr, uint64_t *out) {
    unsigned i, bit = 0;
    M.nacc++;
    if (!word_index(addr, &i)) { *out = 0; return RESP_DECERR; }
    uint64_t d = M.data[i];
    uint8_t c = M.chk[i];
    if (M.ctrl & CTRL_BYPASS) { M.byp_chk = c; *out = d; return RESP_OKAY; }   /* 원시값, 검사·보고 없음 */
    if (!(M.ctrl & CTRL_ECC_EN)) { *out = d; return RESP_OKAY; }
    uint8_t syn = secded_syndrome(d, c);
    if (M.f.ignore_parity) syn = (syn & 0x7F) ? (uint8_t)(syn | 0x80) : 0;
    int cls = secded_classify(syn, &bit);
    if (cls == ECC_CE && bit >= 64 && M.f.chk_as_ue) cls = ECC_UE;
    if (cls == ECC_CE) {
        if (bit < 64 && !M.f.no_correct) d ^= 1ull << bit;
        log_err(ECC_CE, addr, syn | bit << 8);
    } else if (cls == ECC_UE) {
        log_err(ECC_UE, addr, syn);
        *out = d;
        return M.f.ue_no_buserr ? RESP_OKAY : RESP_SLVERR;
    }
    *out = d;
    return RESP_OKAY;
}

/* ================= 테스트 공통 (주어진 코드) ================= */
enum { T_NONE, T_D1, T_C1, T_D2, T_DC2, T_C2, T_NTYPES };
static const char *const TYPE_NAME[] = {"NONE", "D1", "C1", "D2", "DC2", "C2"};
typedef struct { int type; uint32_t addr; uint64_t data, mdata; uint8_t mchk; } ecc_case_t;
typedef struct { int cls; uint32_t status, cnt, synd, line; int resp; } expect_t;

enum { S_PRE, S_INJ_ARM, S_LAND_DATA, S_LAND_CHK, S_BYP_QUIET, S_STATUS, S_RESP, S_DATA, S_CNT, S_ADDR,
       S_SYND, S_LINE, S_CLR_STATUS, S_CLR_LINE, S_CLR_CNT, S_SCRUB, S_SAT, S_FIRST, S_REARM };
static const char *const STEP_NAME[] = {"PRE", "INJ_ARM", "LAND_DATA", "LAND_CHK", "BYP_QUIET", "STATUS", "RESP",
                                        "DATA", "CNT", "ADDR", "SYND", "LINE", "CLR_STATUS", "CLR_LINE",
                                        "CLR_CNT", "SCRUB", "SAT", "FIRST_CAP", "REARM"};
typedef struct {
    const char *test; int type; uint32_t addr; uint64_t mdata; uint8_t mchk;   /* 무엇을 주입했나 */
    int step; uint32_t off; uint64_t exp, act;                                  /* 어디서 틀렸나   */
} fail_t;

static bool expect_val(fail_t *f, int step, uint32_t off, uint64_t exp, uint64_t act) {
    if (act == exp) return true;
    f->step = step; f->off = off; f->exp = exp; f->act = act;
    return false;
}
static bool expect_reg(fail_t *f, int step, uint32_t off, uint64_t exp) {
    return expect_val(f, step, off, exp, reg_read32(off));
}
/* INJ 레지스터를 프로그램하고 arm 한 뒤 그 주소에 data 를 쓴다 → 저장된 코드워드 = encode(data) ^ 마스크 */
static void inject_write(uint32_t addr, uint64_t data, uint64_t mdata, uint8_t mchk) {
    reg_write32(INJ_ADDR, addr);
    reg_write32(INJ_MASK_LO, (uint32_t)mdata);
    reg_write32(INJ_MASK_HI, (uint32_t)(mdata >> 32));
    reg_write32(INJ_MASK_CHK, mchk);
    reg_write32(INJ_CTRL, INJ_EN);
    (void)mem_write64(addr, data);
}
/* 테스트 격리: 주입 해제 → ECC 설정 복구 → 깨끗한 값 재기록 → STATUS/카운터 지움 */
static void mc_cleanup(uint32_t addr) {
    reg_write32(INJ_CTRL, 0);
    reg_write32(ECC_CTRL, CTRL_RUN);
    (void)mem_write64(addr, 0);
    reg_write32(ECC_STATUS, 0xFu);
    reg_write32(ECC_CLR, CLR_CE | CLR_UE);
}
static uint64_t xs64(uint64_t *s) { uint64_t x = *s; x ^= x << 13; x ^= x >> 7; x ^= x << 17; return *s = x; }

/* ================= Part A: 기대값 · 매트릭스 · 케이스 실행 ================= */
/* 주입 마스크만 보고 기대 결과를 계산한다. 선형 부호이므로 신드롬 = 오류 패턴(마스크)의 신드롬 — 데이터와 무관.
 * 1비트 → CE(STATUS=CE, CNT=1, LINE=CE, SYND = s|p<<7|bitnum<<8, OKAY)
 * 2비트 → UE(STATUS=UE, CNT=1<<16, LINE=UE, SYND = s|p<<7, SLVERR) / 0비트 → OK / 3비트 이상 → ECC_UNSPEC */
static void ecc_expect(const ecc_case_t *c, expect_t *e) {
    memset(e, 0, sizeof *e);
    unsigned n = popcount64(c->mdata) + popcount64(c->mchk);
    uint8_t s = (uint8_t)(hsyn64(c->mdata) ^ (c->mchk & 0x7Fu));
    e->synd = (uint32_t)s | (n & 1u) << 7;
    e->resp = RESP_OKAY;
    if (n == 0) {
        e->cls = ECC_OK;
    } else if (n == 1) {
        unsigned bit = c->mdata ? ctz64(c->mdata) : 64 + ctz64(c->mchk);
        e->cls = ECC_CE; e->status = ST_CE; e->cnt = 1; e->line = CE_LINE; e->synd |= bit << 8;
    } else if (n == 2) {
        e->cls = ECC_UE; e->status = ST_UE; e->cnt = 1u << 16; e->line = UE_LINE; e->resp = RESP_SLVERR;
    } else {
        e->cls = ECC_UNSPEC;                        /* SECDED 가 보장하지 않음 → 매트릭스에 넣지 않는다 */
    }
}

/* 순서 고정 (테스트가 인덱스로 확인한다). A = addrs, n = naddr, rng = *seed 로 xs64.
 *   NONE : 주소마다 1개 (i=0..n-1, 주소 A[i])
 *   D1   : 데이터 비트 b=0..63,                주소 A[b % n]
 *   C1   : 체크 비트 k=0..7,                    주소 A[k % n]
 *   D2   : i=0..31, 쌍 = i==0 ? {0,63} : i==1 ? {31,32} : rng%64 두 개(서로 다를 때까지 다시 뽑음), A[i % n]
 *   DC2  : k=0..7, 데이터 비트 rng%64 + 체크 비트 k, A[k % n]
 *   C2   : k=0..7, 체크 비트 k 와 (k+1)%8,       A[k % n]
 * 각 케이스의 data 는 비트를 고른 뒤 xs64 한 번. 총 n+120 개, max 를 넘으면 -1. */
static int build_matrix(const uint32_t *addrs, int naddr, uint64_t seed, ecc_case_t *out, int max) {
    int total = naddr + 120, k = 0;
    if (naddr <= 0 || total > max) return -1;
    uint64_t rng = seed;
#define ADD(T, I, MD, MC) do { out[k].type = (T); out[k].addr = addrs[(I) % naddr]; out[k].mdata = (MD); \
        out[k].mchk = (uint8_t)(MC); out[k].data = xs64(&rng); k++; } while (0)
    for (int i = 0; i < naddr; i++) ADD(T_NONE, i, 0, 0);
    for (int b = 0; b < 64; b++) ADD(T_D1, b, 1ull << b, 0);
    for (int c = 0; c < 8; c++) ADD(T_C1, c, 0, 1u << c);
    for (int i = 0; i < 32; i++) {
        unsigned a = 0, b = 63;
        if (i == 1) { a = 31; b = 32; }
        else if (i > 1) { a = (unsigned)(xs64(&rng) % 64); do b = (unsigned)(xs64(&rng) % 64); while (b == a); }
        ADD(T_D2, i, 1ull << a | 1ull << b, 0);
    }
    for (int c = 0; c < 8; c++) { unsigned a = (unsigned)(xs64(&rng) % 64); ADD(T_DC2, c, 1ull << a, 1u << c); }
    for (int c = 0; c < 8; c++) ADD(T_C2, c, 0, 1u << c | 1u << ((c + 1) % 8));
#undef ADD
    return k;
}

/* 케이스 하나: PRE → 주입(INJ_EN 소모 확인) → bypass 읽기로 착지 확인(데이터·체크·조용함) →
 * 일반 읽기 → STATUS, RESP, DATA(UE 제외), CNT, ADDR/SYND(OK 제외), LINE → W1C·CLR → 0 확인.
 * 첫 실패에서 멈추고 f 를 채운다. 끝나면 항상 mc_cleanup 으로 격리. */
static bool ecc_run_case(const ecc_case_t *c, fail_t *f) {
    expect_t e;
    ecc_expect(c, &e);
    f->test = "inj"; f->type = c->type; f->addr = c->addr; f->mdata = c->mdata; f->mchk = c->mchk;
    bool ok = expect_reg(f, S_PRE, ECC_STATUS, 0) && expect_reg(f, S_PRE, ECC_CNT, 0);
    if (ok) {                                                       /* 1. 주입 */
        inject_write(c->addr, c->data, c->mdata, c->mchk);
        ok = expect_reg(f, S_INJ_ARM, INJ_CTRL, 0);
    }
    if (ok) {                                                       /* 2. 착지 확인: bypass 읽기 */
        uint64_t raw;
        reg_write32(ECC_CTRL, CTRL_RUN | CTRL_BYPASS);
        (void)mem_read64(c->addr, &raw);
        reg_write32(ECC_CTRL, CTRL_RUN);
        ok = expect_val(f, S_LAND_DATA, OFF_RDATA, c->data ^ c->mdata, raw)
          && expect_reg(f, S_LAND_CHK, BYP_CHK, (uint8_t)(secded_encode(c->data) ^ c->mchk))
          && expect_reg(f, S_BYP_QUIET, ECC_STATUS, 0);
    }
    if (ok) {                                                       /* 3. 일반 읽기 → 판정·보고 */
        uint64_t d;
        int resp = mem_read64(c->addr, &d);
        ok = expect_reg(f, S_STATUS, ECC_STATUS, e.status)
          && expect_val(f, S_RESP, OFF_RESP, (uint64_t)e.resp, (uint64_t)resp)
          && (e.cls == ECC_UE || expect_val(f, S_DATA, OFF_RDATA, c->data, d))
          && expect_reg(f, S_CNT, ECC_CNT, e.cnt)
          && (e.cls == ECC_OK || expect_reg(f, S_ADDR, e.cls == ECC_CE ? CE_ADDR : UE_ADDR, c->addr))
          && (e.cls == ECC_OK || expect_reg(f, S_SYND, e.cls == ECC_CE ? CE_SYND : UE_SYND, e.synd))
          && expect_reg(f, S_LINE, INTC_RAW, e.line);
    }
    if (ok) {                                                       /* 4. clear */
        reg_write32(ECC_STATUS, e.status);
        reg_write32(ECC_CLR, CLR_CE | CLR_UE);
        ok = expect_reg(f, S_CLR_STATUS, ECC_STATUS, 0) && expect_reg(f, S_CLR_LINE, INTC_RAW, 0)
          && expect_reg(f, S_CLR_CNT, ECC_CNT, 0);
    }
    mc_cleanup(c->addr);
    return ok;
}

/* ================= Part B: 첫 오류 캡처 · 반복 읽기와 scrub · 카운터 포화 ================= */
#define CAP_A1 0x018u
#define CAP_A2 0x0F0u
#define CAP_A3 0x0A8u
/* clear 없이 CE@A1(d5) → CE@A2(d40) → UE@A3(d1+d2) 를 읽는다.
 *   STATUS == CE|CE_OVF|UE → RESP(A3)==SLVERR → DATA(A1) 정정 → CNT == 1<<16|2
 *   → FIRST_CAP: CE_ADDR == A1, CE_SYND == A1 의 신드롬 → ADDR: UE_ADDR == A3 → LINE == CE|UE
 *   → CE|CE_OVF 만 W1C: CLR_STATUS == UE, CLR_LINE == UE → UE W1C: 둘 다 0
 *   → A2 다시 읽기: REARM: CE_ADDR == A2. 끝나면 세 주소 모두 mc_cleanup */
static bool capture_test(fail_t *f) {
    const uint64_t D1 = 0x0123456789ABCDEFull, D2 = 0xFEDCBA9876543210ull, D3 = 0x5555AAAA3333CCCCull;
    ecc_case_t c1 = {T_D1, CAP_A1, D1, 1ull << 5, 0};
    expect_t e1;
    ecc_expect(&c1, &e1);
    f->test = "capture"; f->type = T_D1; f->addr = CAP_A1; f->mdata = c1.mdata; f->mchk = 0;
    bool ok = expect_reg(f, S_PRE, ECC_STATUS, 0);
    if (ok) {
        uint64_t g1, g2, g3;
        inject_write(CAP_A1, D1, 1ull << 5, 0);
        inject_write(CAP_A2, D2, 1ull << 40, 0);
        inject_write(CAP_A3, D3, 3ull << 1, 0);
        (void)mem_read64(CAP_A1, &g1);
        (void)mem_read64(CAP_A2, &g2);
        int r3 = mem_read64(CAP_A3, &g3);
        ok = expect_reg(f, S_STATUS, ECC_STATUS, ST_CE | ST_CE_OVF | ST_UE)
          && expect_val(f, S_RESP, OFF_RESP, RESP_SLVERR, (uint64_t)r3)
          && expect_val(f, S_DATA, OFF_RDATA, D1, g1)
          && expect_reg(f, S_CNT, ECC_CNT, 1u << 16 | 2u)
          && expect_reg(f, S_FIRST, CE_ADDR, CAP_A1) && expect_reg(f, S_FIRST, CE_SYND, e1.synd)
          && expect_reg(f, S_ADDR, UE_ADDR, CAP_A3)
          && expect_reg(f, S_LINE, INTC_RAW, CE_LINE | UE_LINE);
    }
    if (ok) {
        reg_write32(ECC_STATUS, ST_CE | ST_CE_OVF);
        ok = expect_reg(f, S_CLR_STATUS, ECC_STATUS, ST_UE) && expect_reg(f, S_CLR_LINE, INTC_RAW, UE_LINE);
    }
    if (ok) {
        reg_write32(ECC_STATUS, ST_UE);
        ok = expect_reg(f, S_CLR_STATUS, ECC_STATUS, 0) && expect_reg(f, S_CLR_LINE, INTC_RAW, 0);
    }
    if (ok) {
        uint64_t g2;
        (void)mem_read64(CAP_A2, &g2);
        ok = expect_reg(f, S_REARM, CE_ADDR, CAP_A2);
    }
    mc_cleanup(CAP_A1); mc_cleanup(CAP_A2); mc_cleanup(CAP_A3);
    return ok;
}

/* addr 의 데이터 비트 bit 에 CE 를 심고 n 번 읽는다: 매번 정정 데이터(DATA), CNT == n, STATUS == CE (+CE_OVF if n>1).
 * 정정 읽기는 배열을 고치지 않는다 — scrub(정정값 재기록) 후 한 번 더 읽으면 DATA 정상, SCRUB: CNT 여전히 n. */
static bool reread_scrub_test(uint32_t addr, unsigned bit, unsigned n, fail_t *f) {
    const uint64_t D = 0xA5A5A5A5C3C3C3C3ull;
    f->test = "reread"; f->type = T_D1; f->addr = addr; f->mdata = 1ull << bit; f->mchk = 0;
    bool ok = expect_reg(f, S_PRE, ECC_STATUS, 0) && expect_reg(f, S_PRE, ECC_CNT, 0);
    if (ok) {
        inject_write(addr, D, 1ull << bit, 0);
        for (unsigned i = 0; i < n && ok; i++) {
            uint64_t d;
            (void)mem_read64(addr, &d);
            ok = expect_val(f, S_DATA, OFF_RDATA, D, d);
        }
    }
    if (ok) ok = expect_reg(f, S_CNT, ECC_CNT, n)
              && expect_reg(f, S_STATUS, ECC_STATUS, n > 1 ? ST_CE | ST_CE_OVF : ST_CE);
    if (ok) {
        uint64_t d;
        (void)mem_write64(addr, D);                                 /* scrub: 정정값을 다시 쓴다 */
        (void)mem_read64(addr, &d);
        ok = expect_val(f, S_DATA, OFF_RDATA, D, d) && expect_reg(f, S_SCRUB, ECC_CNT, n);
    }
    mc_cleanup(addr);
    return ok;
}

#define SAT_A     0x040u
#define SAT_READS 0x10001u   /* 65537 회: 포화점 0xFFFF 를 2 회 넘긴다 */
/* CE 하나를 심고 읽기 1회: CNT == 1 → 총 SAT_READS 회까지 읽기: SAT: CNT == 0xFFFF (wrap 금지),
 * STATUS == CE|CE_OVF → ECC_CLR(CE): CLR_CNT: CNT == 0 */
static bool cnt_sat_test(fail_t *f) {
    f->test = "sat"; f->type = T_D1; f->addr = SAT_A; f->mdata = 1; f->mchk = 0;
    bool ok = expect_reg(f, S_PRE, ECC_CNT, 0);
    uint64_t d;
    if (ok) {
        inject_write(SAT_A, 0x1122334455667788ull, 1, 0);
        (void)mem_read64(SAT_A, &d);
        ok = expect_reg(f, S_CNT, ECC_CNT, 1);
    }
    if (ok) {
        for (unsigned i = 1; i < SAT_READS; i++) (void)mem_read64(SAT_A, &d);
        ok = expect_reg(f, S_SAT, ECC_CNT, 0xFFFFu) && expect_reg(f, S_STATUS, ECC_STATUS, ST_CE | ST_CE_OVF);
    }
    if (ok) {
        reg_write32(ECC_CLR, CLR_CE);
        ok = expect_reg(f, S_CLR_CNT, ECC_CNT, 0);
    }
    mc_cleanup(SAT_A);
    return ok;
}

/* ================= Part C: 실패 → 원인 분류 ================= */
enum { C_NONE, C_STALE, C_INJ_ARM, C_INJECTOR, C_BYPASS_LEAK, C_MISSED, C_FALSE_ERR, C_MISCORRECT,
       C_CHKBIT_AS_UE, C_CE_AS_UE, C_OVF_FLAG, C_NO_BUSERR, C_FALSE_BUSERR, C_NO_CORRECTION, C_COUNTER,
       C_CNT_SAT, C_ADDR_CAPTURE, C_CAPTURE_ORDER, C_SYNDROME, C_IRQ_DEAD, C_IRQ_SPURIOUS, C_CLEAR, C_SCRUB,
       C_REARM, C_COUNT };
static const char *const CAUSE_NAME[] = {
    "NONE", "STALE_STATE", "INJ_NOT_CONSUMED", "INJECTOR", "BYPASS_LEAK", "ERROR_MISSED", "FALSE_ERROR",
    "MISCORRECT_SDC", "CHKBIT_AS_UE", "CE_AS_UE", "OVF_FLAG", "NO_BUSERR", "FALSE_BUSERR", "NO_CORRECTION",
    "COUNTER", "CNT_SATURATION", "ADDR_CAPTURE", "CAPTURE_ORDER", "SYNDROME", "IRQ_DEAD", "IRQ_SPURIOUS",
    "CLEAR", "SCRUB_FAIL", "CAPTURE_REARM"};

/* 실패 기록 하나(step + exp/act + 주입 정보)로 가장 그럴듯한 원인을 고른다 */
static int classify(const fail_t *f) {
    switch (f->step) {
    case S_PRE:       return C_STALE;
    case S_INJ_ARM:   return C_INJ_ARM;
    case S_LAND_DATA:
    case S_LAND_CHK:  return C_INJECTOR;
    case S_BYP_QUIET: return C_BYPASS_LEAK;
    case S_STATUS: {
        uint64_t e = f->exp & (ST_CE | ST_UE), a = f->act & (ST_CE | ST_UE);
        if (e == a) return C_OVF_FLAG;
        if (!a) return C_MISSED;
        if (!e) return C_FALSE_ERR;
        if ((e & ST_UE) && !(a & ST_UE)) return C_MISCORRECT;
        if ((a & ST_UE) && !(e & ST_UE)) return f->type == T_C1 ? C_CHKBIT_AS_UE : C_CE_AS_UE;
        return C_MISSED;
    }
    case S_RESP:      return f->exp == RESP_SLVERR ? C_NO_BUSERR : C_FALSE_BUSERR;
    case S_DATA:      return (f->exp ^ f->act) == f->mdata ? C_NO_CORRECTION : C_MISCORRECT;
    case S_CNT:       return C_COUNTER;
    case S_SAT:       return C_CNT_SAT;
    case S_ADDR:      return C_ADDR_CAPTURE;
    case S_FIRST:     return C_CAPTURE_ORDER;
    case S_SYND:      return C_SYNDROME;
    case S_LINE:      return (f->exp & ~f->act) ? C_IRQ_DEAD : C_IRQ_SPURIOUS;
    case S_CLR_STATUS:
    case S_CLR_LINE:
    case S_CLR_CNT:   return C_CLEAR;
    case S_SCRUB:     return C_SCRUB;
    case S_REARM:     return C_REARM;
    default:          return C_NONE;
    }
}

/* ================= 스위트 실행과 리포트 (주어진 코드) ================= */
static const uint32_t ADDRS[8] = {0x000, 0x008, 0x010, 0x020, 0x040, 0x080, 0x100, 0x1F8};  /* walking 주소 비트 */
#define SEED   0x5EED00000000E14Cull
#define MAXC   160
#define MAXLOG 160
typedef struct {
    fail_t log[MAXLOG]; int n, runs; unsigned nacc;
    int tcases[T_NTYPES], tfail[T_NTYPES], tfirst[T_NTYPES]; char b_res[3];
} suite_t;

static void log_result(suite_t *S, bool ok, const fail_t *f) {
    S->runs++;
    if (!ok && S->n < MAXLOG) S->log[S->n++] = *f;
}
static void run_suite(const fault_t *flt, suite_t *S) {
    static ecc_case_t cs[MAXC];
    memset(S, 0, sizeof *S);
    for (int t = 0; t < T_NTYPES; t++) S->tfirst[t] = -1;
    mock_init(flt);
    reg_write32(ECC_CTRL, CTRL_RUN);
    int n = build_matrix(ADDRS, 8, SEED, cs, MAXC);
    for (int i = 0; i < n; i++) {
        fail_t f = {0};
        bool ok = ecc_run_case(&cs[i], &f);
        S->tcases[cs[i].type]++;
        if (!ok) { S->tfail[cs[i].type]++; if (S->tfirst[cs[i].type] < 0) S->tfirst[cs[i].type] = f.step; }
        log_result(S, ok, &f);
    }
    fail_t f = {0};
    bool ok = capture_test(&f);                     S->b_res[0] = ok ? 'P' : 'F'; log_result(S, ok, &f);
    memset(&f, 0, sizeof f); ok = reread_scrub_test(0x0C0, 17, 3, &f); S->b_res[1] = ok ? 'P' : 'F'; log_result(S, ok, &f);
    memset(&f, 0, sizeof f); ok = cnt_sat_test(&f); S->b_res[2] = ok ? 'P' : 'F'; log_result(S, ok, &f);
    S->nacc = M.nacc;
}
static const char *reg_name(uint32_t off) {
    switch (off) {
    case ECC_CTRL: return "ECC_CTRL"; case ECC_STATUS: return "ECC_STATUS"; case ECC_CNT: return "ECC_CNT";
    case CE_ADDR: return "CE_ADDR"; case CE_SYND: return "CE_SYND"; case UE_ADDR: return "UE_ADDR";
    case UE_SYND: return "UE_SYND"; case INJ_CTRL: return "INJ_CTRL"; case BYP_CHK: return "BYP_CHK";
    case INTC_RAW: return "INTC_RAW"; case OFF_RDATA: return "RDATA"; case OFF_RESP: return "RRESP";
    default: return "?";
    }
}
static void bitlist(uint64_t x, char *buf, size_t n) {
    size_t k = 0;
    buf[0] = 0;
    for (unsigned b = 0; b < 64 && k + 4 < n; b++)
        if (x >> b & 1) k += (size_t)snprintf(buf + k, n - k, "%s%u", k ? "," : "", b);
}
static void injlist(uint64_t md, uint8_t mc, char *buf, size_t n) {
    size_t k = 0;
    buf[0] = 0;
    for (unsigned b = 0; b < 64 && k + 5 < n; b++)
        if (md >> b & 1) k += (size_t)snprintf(buf + k, n - k, "%sd%u", k ? "+" : "", b);
    for (unsigned b = 0; b < 8 && k + 4 < n; b++)
        if (mc >> b & 1) k += (size_t)snprintf(buf + k, n - k, "%sc%u", k ? "+" : "", b);
}
static void print_fail(const fail_t *f) {
    char bits[200], inj[64];
    uint64_t diff = f->exp ^ f->act;
    bitlist(diff, bits, sizeof bits);
    injlist(f->mdata, f->mchk, inj, sizeof inj);
    printf("FAIL %s %s type=%s addr=0x%03X inj=%s step=%s reg=%s ", IP_NAME, f->test ? f->test : "?",
           TYPE_NAME[f->type], (unsigned)f->addr, inj[0] ? inj : "-", STEP_NAME[f->step], reg_name(f->off));
    if (f->off == OFF_RESP)
        printf("exp=%s act=%s", RESP_NAME[f->exp & 3], RESP_NAME[f->act & 3]);
    else if (f->off == OFF_RDATA)
        printf("exp=0x%016llX act=0x%016llX diff=0x%016llX bits=[%s]", (unsigned long long)f->exp,
               (unsigned long long)f->act, (unsigned long long)diff, bits);
    else
        printf("exp=0x%08llX act=0x%08llX diff=0x%08llX bits=[%s]", (unsigned long long)f->exp,
               (unsigned long long)f->act, (unsigned long long)diff, bits);
    printf(" cause=%s\n", CAUSE_NAME[classify(f)]);
}
static void print_summary(const suite_t *S) {
    printf("  %-5s %5s %5s  %s\n", "type", "cases", "fail", "first-fail-step");
    for (int t = 0; t < T_NTYPES; t++)
        printf("  %-5s %5d %5d  %s\n", TYPE_NAME[t], S->tcases[t], S->tfail[t],
               S->tfirst[t] < 0 ? "-" : STEP_NAME[S->tfirst[t]]);
    printf("  capture=%c reread=%c sat=%c\n", S->b_res[0], S->b_res[1], S->b_res[2]);
    int hist[C_COUNT] = {0};
    for (int i = 0; i < S->n; i++) hist[classify(&S->log[i])]++;
    printf("  causes:");
    for (int c = 0; c < C_COUNT; c++) if (hist[c]) printf(" %s x%d", CAUSE_NAME[c], hist[c]);
    printf("\n");
}

/* ================= 테스트 하네스 ================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static bool run_one(const fault_t *flt, const ecc_case_t *c, fail_t *f) {
    mock_init(flt);
    reg_write32(ECC_CTRL, CTRL_RUN);
    memset(f, 0, sizeof *f);
    return ecc_run_case(c, f);
}
static int count_cause(const suite_t *S, int cause) {
    int n = 0;
    for (int i = 0; i < S->n; i++) n += classify(&S->log[i]) == cause;
    return n;
}

int main(void) {
    static ecc_case_t cs[MAXC];
    static suite_t S;
    fail_t f;
    expect_t e;
    codec_init();

    /* ---- 주어진 코덱·mock sanity ---- */
    int cok = 0;
    for (unsigned b = 0; b < 72; b++) {           /* 72 위치 단일 오류 → 전부 CE, 비트 번호 일치 */
        uint64_t d = 0x0F1E2D3C4B5A6978ull;
        uint8_t c = secded_encode(d);
        if (b < 64) d ^= 1ull << b; else c ^= (uint8_t)(1u << (b - 64));
        unsigned bn = 99;
        cok += secded_classify(secded_syndrome(d, c), &bn) == ECC_CE && bn == b;
    }
    CHECK(cok == 72);
    mock_init(NULL);
    CHECK(reg_read32(ECC_CTRL) == 0 && reg_read32(ECC_STATUS) == 0 && reg_read32(0x7FC) == 0xDEADBEEFu);
    reg_write32(ECC_CTRL, CTRL_RUN);
    inject_write(0x008, 42, 1ull << 3, 0);
    uint64_t d;
    CHECK(reg_read32(INJ_CTRL) == 0 && mem_read64(0x008, &d) == RESP_OKAY && d == 42);   /* one-shot, 정정 */
    CHECK(mem_write64(0x004, 1) == RESP_DECERR && mem_read64(0x200, &d) == RESP_DECERR);

    /* ---- Part A: 기대값 (마스크만으로) ---- */
    struct { ecc_case_t c; int cls; uint32_t synd; } et[] = {
        {{T_NONE, 0, 1, 0, 0}, ECC_OK, 0x000},
        {{T_D1, 0, 1, 1ull << 5, 0}, ECC_CE, 0x58A},           /* POS[5]=10, p=1, bitnum 5 */
        {{T_D1, 0, 7, 1ull << 63, 0}, ECC_CE, 0x3FC7},         /* POS[63]=71 → 0x47|0x80, bitnum 63 */
        {{T_C1, 0, 1, 0, 0x01}, ECC_CE, 0x4081},               /* c0: s=1, bitnum 64 */
        {{T_C1, 0, 1, 0, 0x80}, ECC_CE, 0x4780},               /* c7: s=0,p=1, bitnum 71 */
        {{T_D2, 0, 1, 1ull | 1ull << 63, 0}, ECC_UE, 0x044},   /* 3^71 = 68, p=0 */
        {{T_DC2, 0, 1, 1ull << 5, 0x80}, ECC_UE, 0x00A},       /* d5 + c7: s=10, p=0 */
        {{T_C2, 0, 1, 0, 0x03}, ECC_UE, 0x003},
        {{T_D2, 0, 1, 7, 0}, ECC_UNSPEC, 0x80},                /* 3비트: 3^5^6=0, p=1 → 스펙 밖 */
    };
    int eok = 0;
    for (size_t i = 0; i < sizeof et / sizeof et[0]; i++) {
        ecc_expect(&et[i].c, &e);
        eok += e.cls == et[i].cls && e.synd == et[i].synd;
    }
    CHECK(eok == (int)(sizeof et / sizeof et[0]));
    ecc_expect(&et[5].c, &e);
    CHECK(e.status == ST_UE && e.cnt == 0x10000u && e.line == UE_LINE && e.resp == RESP_SLVERR);

    /* ---- Part A: 매트릭스 구조 ---- */
    int n = build_matrix(ADDRS, 8, SEED, cs, MAXC);
    CHECK(n == 128);
    CHECK(cs[0].type == T_NONE && cs[7].addr == 0x1F8 && cs[7].mdata == 0 && cs[7].mchk == 0);
    CHECK(cs[8].type == T_D1 && cs[8].mdata == 1 && cs[8].addr == 0x000);
    CHECK(cs[8 + 63].mdata == 1ull << 63 && cs[8 + 63].addr == 0x1F8 && cs[8 + 14].addr == 0x100);
    CHECK(cs[72].type == T_C1 && cs[72].mchk == 0x01 && cs[79].mchk == 0x80);
    CHECK(cs[80].type == T_D2 && cs[80].mdata == (1ull | 1ull << 63) && cs[81].mdata == (3ull << 31));
    int pc = 0;
    for (int i = 80; i < 112; i++) pc += popcount64(cs[i].mdata) == 2 && cs[i].mchk == 0;
    CHECK(pc == 32);
    CHECK(cs[112].type == T_DC2 && popcount64(cs[112].mdata) == 1 && cs[112].mchk == 0x01);
    CHECK(cs[127].type == T_C2 && cs[127].mchk == 0x81 && cs[127].mdata == 0);
    static ecc_case_t cs2[MAXC];
    CHECK(build_matrix(ADDRS, 8, SEED, cs2, MAXC) == 128 && memcmp(cs, cs2, sizeof(ecc_case_t) * 128) == 0);
    CHECK(build_matrix(ADDRS, 8, SEED, cs2, 100) == -1);

    /* ---- Part A: 좋은 mock 은 128 케이스 모두 통과 ---- */
    mock_init(NULL);
    reg_write32(ECC_CTRL, CTRL_RUN);
    int pass = 0;
    for (int i = 0; i < n; i++) { memset(&f, 0, sizeof f); pass += ecc_run_case(&cs[i], &f); }
    CHECK(n == 128 && pass == 128);
    CHECK(M.status == 0 && M.ce_cnt == 0 && M.ue_cnt == 0 && M.inj_ctrl == 0);

    /* ---- Part A: task 가 요구한 "정정하지 않는" 결함 ---- */
    fault_t fnc = {.no_correct = true};
    CHECK(!run_one(&fnc, &cs[8 + 17], &f));
    CHECK(f.step == S_DATA && f.off == OFF_RDATA && (f.exp ^ f.act) == 1ull << 17 && classify(&f) == C_NO_CORRECTION);
    CHECK(run_one(&fnc, &cs[72 + 3], &f));               /* 체크비트 오류는 데이터에 안 보인다 → 통과 */
    CHECK(run_one(&fnc, &cs[80], &f));                   /* UE 는 데이터 비교를 안 한다 */
    /* 주입기 결함은 디코더 결함과 구별돼야 한다 */
    fault_t fij = {.inj_chk_dead = true};
    CHECK(!run_one(&fij, &cs[72 + 2], &f) && f.step == S_LAND_CHK && classify(&f) == C_INJECTOR);
    /* UE 를 CE 로 오정정 (c7 무시) */
    fault_t fip = {.ignore_parity = true};
    CHECK(!run_one(&fip, &cs[80], &f) && f.step == S_STATUS && f.exp == ST_UE && f.act == ST_CE);
    CHECK(!run_one(&fip, &cs[79], &f) && f.step == S_STATUS && f.act == 0 && classify(&f) == C_MISSED);

    /* ---- Part B: 좋은 mock 은 통과 ---- */
    mock_init(NULL);
    reg_write32(ECC_CTRL, CTRL_RUN);
    memset(&f, 0, sizeof f); CHECK(capture_test(&f));
    memset(&f, 0, sizeof f); CHECK(reread_scrub_test(0x0C0, 17, 3, &f));
    memset(&f, 0, sizeof f); CHECK(reread_scrub_test(0x1F8, 0, 1, &f));
    memset(&f, 0, sizeof f); CHECK(cnt_sat_test(&f));
    CHECK(M.status == 0 && M.ce_cnt == 0);

    /* 첫 오류 대신 마지막 오류를 캡처: 매트릭스(케이스마다 clear)는 128 개 모두 통과 → capture 만 잡는다 */
    fault_t fcl = {.ce_capture_last = true};
    mock_init(&fcl);
    reg_write32(ECC_CTRL, CTRL_RUN);
    pass = 0;
    for (int i = 0; i < n; i++) { memset(&f, 0, sizeof f); pass += ecc_run_case(&cs[i], &f); }
    CHECK(pass == 128);
    memset(&f, 0, sizeof f);
    CHECK(!capture_test(&f) && f.step == S_FIRST && f.exp == CAP_A1 && f.act == CAP_A2);
    /* 카운터 wrap: 포화 시험만 잡는다 */
    fault_t fw = {.cnt_wrap = true};
    mock_init(&fw);
    reg_write32(ECC_CTRL, CTRL_RUN);
    memset(&f, 0, sizeof f);
    CHECK(!cnt_sat_test(&f) && f.step == S_SAT && f.exp == 0xFFFF && f.act == 1);
    /* 정정 안 함: 반복 읽기의 첫 읽기에서 */
    mock_init(&fnc);
    reg_write32(ECC_CTRL, CTRL_RUN);
    memset(&f, 0, sizeof f);
    CHECK(!reread_scrub_test(0x0C0, 17, 3, &f) && f.step == S_DATA && (f.exp ^ f.act) == 1ull << 17);

    /* ---- Part C: classify 단위 테스트 ---- */
    struct { fail_t f; int cause; } ct[] = {
        {{"inj", T_D1, 0, 1, 0, S_PRE, ECC_STATUS, 0, 1}, C_STALE},
        {{"inj", T_C1, 0, 0, 4, S_LAND_CHK, BYP_CHK, 0x13, 0x17}, C_INJECTOR},
        {{"inj", T_D1, 0, 1, 0, S_BYP_QUIET, ECC_STATUS, 0, ST_CE}, C_BYPASS_LEAK},
        {{"inj", T_D2, 0, 3, 0, S_STATUS, ECC_STATUS, ST_UE, ST_CE}, C_MISCORRECT},
        {{"inj", T_C1, 0, 0, 1, S_STATUS, ECC_STATUS, ST_CE, ST_UE}, C_CHKBIT_AS_UE},
        {{"inj", T_D1, 0, 1, 0, S_STATUS, ECC_STATUS, ST_CE, ST_UE}, C_CE_AS_UE},
        {{"inj", T_C1, 0, 0, 0x80, S_STATUS, ECC_STATUS, ST_CE, 0}, C_MISSED},
        {{"inj", T_NONE, 0, 0, 0, S_STATUS, ECC_STATUS, 0, ST_CE}, C_FALSE_ERR},
        {{"inj", T_D2, 0, 3, 0, S_RESP, OFF_RESP, RESP_SLVERR, RESP_OKAY}, C_NO_BUSERR},
        {{"inj", T_D1, 0, 8, 0, S_DATA, OFF_RDATA, 0x10, 0x18}, C_NO_CORRECTION},
        {{"inj", T_D1, 0, 8, 0, S_DATA, OFF_RDATA, 0x10, 0x11}, C_MISCORRECT},
        {{"inj", T_D1, 0x100, 1, 0, S_ADDR, CE_ADDR, 0x100, 0x000}, C_ADDR_CAPTURE},
        {{"capture", T_D1, CAP_A1, 32, 0, S_FIRST, CE_ADDR, CAP_A1, CAP_A2}, C_CAPTURE_ORDER},
        {{"inj", T_D2, 0, 3, 0, S_LINE, INTC_RAW, UE_LINE, 0}, C_IRQ_DEAD},
        {{"inj", T_NONE, 0, 0, 0, S_LINE, INTC_RAW, 0, CE_LINE}, C_IRQ_SPURIOUS},
        {{"sat", T_D1, SAT_A, 1, 0, S_SAT, ECC_CNT, 0xFFFF, 1}, C_CNT_SAT},
    };
    cok = 0;
    for (size_t i = 0; i < sizeof ct / sizeof ct[0]; i++) cok += classify(&ct[i].f) == ct[i].cause;
    CHECK(cok == (int)(sizeof ct / sizeof ct[0]));

    /* ---- Part C: 야간 회귀 — 결함 주입 캠페인 ---- */
    run_suite(NULL, &S);
    CHECK(S.runs == 131 && S.n == 0);
    printf("[good] runs=%d failures=%d bus_accesses=%u\n", S.runs, S.n, S.nacc);

    struct { const char *name; fault_t f; int nfail; const char *test; int type; int step; int cause; } camp[] = {
        {"no_correct",        {.no_correct = true},      66, "inj", T_D1, S_DATA, C_NO_CORRECTION},
        {"ignore_parity",     {.ignore_parity = true},   50, "inj", T_C1, S_STATUS, C_MISSED},
        {"chk_as_ue",         {.chk_as_ue = true},        8, "inj", T_C1, S_STATUS, C_CHKBIT_AS_UE},
        {"ce_cnt_dead",       {.ce_cnt_dead = true},     75, "inj", T_D1, S_CNT, C_COUNTER},
        {"cnt_wrap",          {.cnt_wrap = true},         1, "sat", T_D1, S_SAT, C_CNT_SAT},
        {"addr_drop=0x100",   {.addr_drop = 0x100},      30, "inj", T_D1, S_ADDR, C_ADDR_CAPTURE},
        {"ce_capture_last",   {.ce_capture_last = true},  1, "capture", T_D1, S_FIRST, C_CAPTURE_ORDER},
        {"ue_irq_dead",       {.ue_irq_dead = true},     49, "inj", T_D2, S_LINE, C_IRQ_DEAD},
        {"ue_no_buserr",      {.ue_no_buserr = true},    49, "inj", T_D2, S_RESP, C_NO_BUSERR},
        {"inj_chk_dead",      {.inj_chk_dead = true},    24, "inj", T_C1, S_LAND_CHK, C_INJECTOR},
    };
    for (size_t i = 0; i < sizeof camp / sizeof camp[0]; i++) {
        run_suite(&camp[i].f, &S);
        printf("[%s] failures=%d/%d first: ", camp[i].name, S.n, S.runs);
        if (S.n) print_fail(&S.log[0]); else printf("(none)\n");
        CHECK(S.n == camp[i].nfail);
        CHECK(S.n > 0 && strcmp(S.log[0].test, camp[i].test) == 0 && S.log[0].type == camp[i].type &&
              S.log[0].step == camp[i].step && classify(&S.log[0]) == camp[i].cause);
    }
    /* ignore_parity: 놓친 c7 CE 1 개보다 무서운 것 — UE 를 CE 로 보고(조용한 데이터 오염) */
    run_suite(&camp[1].f, &S);
    CHECK(count_cause(&S, C_MISCORRECT) == 41 && count_cause(&S, C_MISSED) == 1 &&
          count_cause(&S, C_SYNDROME) == 8);
    /* addr_drop: 주소 비트 8 이 1 인 주소(0x100, 0x1F8)에서만 */
    run_suite(&camp[5].f, &S);
    int a8 = 0;
    for (int i = 0; i < S.n; i++) a8 += (S.log[i].addr & 0x100) && S.log[i].exp == S.log[i].addr &&
                                       S.log[i].act == (S.log[i].addr & ~0x100u);
    CHECK(a8 == 30);

    /* 리포트: task 가 요구한 "정정하지 않는" 결함 */
    run_suite(&camp[0].f, &S);
    CHECK(count_cause(&S, C_NO_CORRECTION) == 66 && S.tfail[T_D1] == 64 && S.tfail[T_C1] == 0);
    printf("---- report: %s fault=%s ----\n", IP_NAME, camp[0].name);
    for (int i = 0; i < 3 && i < S.n; i++) print_fail(&S.log[i]);
    printf("  ... (%d more)\n", S.n > 3 ? S.n - 3 : 0);
    print_summary(&S);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
