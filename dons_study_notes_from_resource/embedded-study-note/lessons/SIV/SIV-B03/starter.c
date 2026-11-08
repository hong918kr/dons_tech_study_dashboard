/* SIV-B03 리셋값 테스트 (Reset Value Test)
 *
 * 레지스터 기술서(SIV-B01 의 JSON 을 C 표로 축약)에서 레지스터마다 (기대 리셋값 exp, 비교 마스크 mask)를
 * 계산하고, mock MMIO(SIV-B02 의 mock 을 축약)를 bus ops 로 "딱 한 번씩" 읽어 비교한다.
 *   - 비교에서 빼는 비트 : VOLATILE 필드(HW/외부 핀이 바꾸는 값), UNDEF 필드(리셋 안 되는 플롭/RAM), WO 필드
 *   - 예약(reserved) 비트: 어느 필드에도 속하지 않으면 "0으로 읽힌다(RAZ)" 로 보고 비교에 포함
 *   - 아예 읽지 않는 레지스터: RF_NOREAD(읽기 자체에 부작용: FIFO pop) / 비교할 필드가 하나도 없는 레지스터
 * 실패 리포트 한 줄: FAIL reg=CTRL off=0x004 exp=0x00000000 act=0x00000001 diff=0x00000001 bits=[0] fields=[EN]
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 레지스터 기술서 (SIV-B01 형식 축약) ---------------- */
enum { AC_RW, AC_RO, AC_WO, AC_W1C, AC_W1S, AC_RC };
#define FF_VOLATILE 0x1u /* HW·외부 핀이 바꾸는 값: 리셋 직후에도 결정적이지 않다 */
#define FF_UNDEF    0x2u /* 리셋되지 않는 플롭/RAM: 전원 켤 때마다 다른 값      */
#define RF_NOREAD   0x1u /* 읽기 자체에 부작용(FIFO pop 등): 리셋 테스트에서 읽지 않는다 */

typedef struct {
    const char *name;
    uint8_t lsb, width, access, flags;
    uint32_t reset;
} field_desc_t;

typedef struct {
    const char *name;
    uint32_t off;
    uint8_t rflags, nfields;
    const field_desc_t *fields;
} reg_desc_t;

#define F(n, lsb, w, ac, rst, fl) { n, lsb, w, ac, fl, rst }
#define R(n, off, rf, fa) { n, off, rf, (uint8_t)(sizeof fa / sizeof fa[0]), fa }

/* 가상의 UART0 IP — B01 regmap.json 이 생성했다고 가정한 표 */
enum { OFF_ID = 0x000, OFF_CTRL = 0x004, OFF_BAUD = 0x008, OFF_TXDATA = 0x00C, OFF_RXDATA = 0x010,
       OFF_FIFOCFG = 0x014, OFF_STATUS = 0x018, OFF_INT_RAW = 0x01C, OFF_INT_EN = 0x020,
       OFF_ERRCNT = 0x024, OFF_TIMER = 0x028, OFF_DBG = 0x02C };

static const field_desc_t f_id[]   = { F("REV", 8, 8, AC_RO, 0x01, 0), F("PART", 16, 16, AC_RO, 0x5541, 0) };
static const field_desc_t f_ctrl[] = { F("EN", 0, 1, AC_RW, 0, 0), F("TXEN", 1, 1, AC_RW, 0, 0),
                                       F("RXEN", 2, 1, AC_RW, 0, 0), F("PAR", 3, 2, AC_RW, 0, 0),
                                       F("STOP2", 5, 1, AC_RW, 0, 0) };
static const field_desc_t f_baud[] = { F("DIV", 0, 16, AC_RW, 0x001A, 0) };
static const field_desc_t f_txd[]  = { F("DATA", 0, 8, AC_WO, 0, 0) };
static const field_desc_t f_rxd[]  = { F("DATA", 0, 8, AC_RO, 0, FF_UNDEF) };
static const field_desc_t f_fcfg[] = { F("TXTH", 0, 4, AC_RW, 0x4, 0), F("RXTH", 8, 4, AC_RW, 0x8, 0) };
static const field_desc_t f_stat[] = { F("TXEMPTY", 0, 1, AC_RO, 1, 0), F("TXFULL", 1, 1, AC_RO, 0, 0),
                                       F("RXEMPTY", 2, 1, AC_RO, 1, 0), F("BUSY", 3, 1, AC_RO, 0, 0),
                                       F("CTS", 4, 1, AC_RO, 0, FF_VOLATILE) };
static const field_desc_t f_iraw[] = { F("TXDONE", 0, 1, AC_W1C, 0, 0), F("RXDONE", 1, 1, AC_W1C, 0, 0),
                                       F("RXUF", 2, 1, AC_W1C, 0, 0) };
static const field_desc_t f_ien[]  = { F("TXDONE", 0, 1, AC_RW, 0, 0), F("RXDONE", 1, 1, AC_RW, 0, 0),
                                       F("RXUF", 2, 1, AC_RW, 0, 0) };
static const field_desc_t f_ecnt[] = { F("CNT", 0, 8, AC_RC, 0, 0) };
static const field_desc_t f_tmr[]  = { F("COUNT", 0, 32, AC_RO, 0, FF_VOLATILE) };
static const field_desc_t f_dbg[]  = { F("SEL", 0, 4, AC_RW, 0, 0), F("TAP", 24, 8, AC_RO, 0, FF_UNDEF) };

static const reg_desc_t uart0[] = {
    R("ID", OFF_ID, 0, f_id),          R("CTRL", OFF_CTRL, 0, f_ctrl),     R("BAUD", OFF_BAUD, 0, f_baud),
    R("TXDATA", OFF_TXDATA, 0, f_txd), R("RXDATA", OFF_RXDATA, RF_NOREAD, f_rxd),
    R("FIFOCFG", OFF_FIFOCFG, 0, f_fcfg), R("STATUS", OFF_STATUS, 0, f_stat),
    R("INT_RAW", OFF_INT_RAW, 0, f_iraw), R("INT_EN", OFF_INT_EN, 0, f_ien),
    R("ERRCNT", OFF_ERRCNT, 0, f_ecnt), R("TIMER", OFF_TIMER, 0, f_tmr), R("DBG", OFF_DBG, 0, f_dbg),
};
#define NREG (sizeof uart0 / sizeof uart0[0])

static uint32_t field_mask(unsigned width) { return width >= 32 ? 0xFFFFFFFFu : (1u << width) - 1u; }

/* ---------------- bus ops (SIV-B02): 같은 테스트가 mock 에도 실보드에도 붙는다 ---------------- */
typedef struct {
    uint32_t (*read32)(void *ctx, uint32_t off);
    void *ctx;
} bus_t;
/* 실보드라면: static uint32_t hw_read32(void *base, uint32_t off)
 *              { return *(volatile uint32_t *)((uintptr_t)base + off); }                       */

/* ---------------- mock UART0 (주어진 코드 — 고치지 않는다) ---------------- */
typedef struct { uint32_t off, xor_bits; } fault_t; /* 리셋 이미지의 비트를 뒤집는 결함 주입 */

typedef struct {
    uint32_t val[NREG];
    unsigned reads[NREG];
    uint32_t dead; /* != 0 이면 모든 읽기가 이 값: 클럭 꺼짐/리셋 유지/디코드 안 됨 */
    uint32_t rng;
} mock_t;

static uint32_t xorshift32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 전원 인가 직후의 "실리콘" 상태: 기술서 리셋값 + UNDEF 쓰레기 + VOLATILE 살아있는 값 + 주입 결함 */
static void mock_power_on(mock_t *m, const fault_t *faults, size_t nf, uint32_t seed) {
    memset(m, 0, sizeof *m);
    m->rng = seed ? seed : 1u;
    for (size_t i = 0; i < NREG; i++) {
        uint32_t v = 0;
        for (unsigned k = 0; k < uart0[i].nfields; k++) {
            const field_desc_t *f = &uart0[i].fields[k];
            uint32_t fm = field_mask(f->width) << f->lsb;
            if (f->flags & FF_UNDEF)         v |= xorshift32(&m->rng) & fm;
            else if (f->flags & FF_VOLATILE) v |= fm;                     /* CTS pull-up, 카운터 동작 중 */
            else                             v |= (f->reset << f->lsb) & fm;
        }
        m->val[i] = v;
    }
    for (size_t k = 0; k < nf; k++) m->val[faults[k].off >> 2] ^= faults[k].xor_bits;
}

static uint32_t mock_read32(void *ctx, uint32_t off) {
    mock_t *m = ctx;
    if (m->dead) return m->dead;
    size_t i = off >> 2;
    if ((off & 3u) || i >= NREG) return 0xBADADD00u;
    m->reads[i]++;
    uint32_t v = m->val[i];
    for (unsigned k = 0; k < uart0[i].nfields; k++)           /* RC: 읽으면 지워진다 */
        if (uart0[i].fields[k].access == AC_RC)
            m->val[i] &= ~(field_mask(uart0[i].fields[k].width) << uart0[i].fields[k].lsb);
    if (off == OFF_RXDATA) {                                  /* 빈 FIFO pop → underflow 부작용 */
        m->val[OFF_INT_RAW >> 2] |= 1u << 2;                  /* INT_RAW.RXUF */
        m->val[OFF_ERRCNT >> 2] += 1u;                        /* ERRCNT.CNT++ */
    }
    if (off == OFF_TIMER) m->val[i] += 13u;                   /* free-running counter */
    return v;
}

/* ---------------- 공용 출력 헬퍼 ---------------- */
__attribute__((format(printf, 3, 4)))
static void bprintf(char *buf, size_t cap, const char *fmt, ...) {
    size_t len = strlen(buf);
    if (len + 1 >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + len, cap - len, fmt, ap);
    va_end(ap);
}

/* ---------------- Part A: 기대값·마스크 계산과 레지스터 1개 검사 ---------------- */
typedef enum { RES_PASS, RES_FAIL, RES_SKIP } res_t;
typedef struct { uint32_t exp, mask, act, diff; } reset_res_t;

/* exp = 검사 대상 필드 리셋값의 OR, mask = 검사 대상 필드 비트 | 예약 비트.
 * 검사 대상이 아닌 필드: VOLATILE, UNDEF, WO. 검사 대상 필드가 하나도 없으면 exp = mask = 0. */
static void reset_expect(const reg_desc_t *r, uint32_t *exp, uint32_t *mask) {
    (void)r;
    *exp = 0; *mask = 0; /* TODO: 필드를 돌며 covered/chk/exp 를 모으고, chk==0 이면 0/0, 아니면 mask = chk | ~covered */
}

/* 레지스터 하나를 정확히 한 번 읽어 비교. NOREAD 이거나 mask == 0 이면 읽지 않고 SKIP. */
static res_t reset_check_one(const bus_t *b, const reg_desc_t *r, reset_res_t *out) {
    (void)b; (void)r;
    memset(out, 0, sizeof *out);
    return RES_SKIP; /* TODO: NOREAD/mask==0 은 SKIP, 아니면 한 번 읽어 diff = (act ^ exp) & mask */
}

/* ---------------- Part B: 비트·필드 단위로 불일치를 말하기 ---------------- */
/* diff 의 1 비트를 MSB 부터 연속 구간으로 묶는다: 0xF1 → "[7:4,0]", 0 → "[]" */
static void bits_to_str(uint32_t diff, char *buf, size_t n) {
    (void)diff;
    buf[0] = '\0';
    bprintf(buf, n, "[]"); /* TODO: MSB 부터 연속된 1 구간을 "hi:lo" 또는 "b" 로, 쉼표로 이어 붙인다 */
}

/* diff 와 겹치는 필드 이름을 표 순서대로, 예약 비트가 섞였으면 마지막에 "rsvd": "[EN,PAR]", "[rsvd]" */
static void fields_of(const reg_desc_t *r, uint32_t diff, char *buf, size_t n) {
    (void)r; (void)diff; (void)n;
    buf[0] = '\0'; /* TODO: diff 와 겹치는 필드 이름들 + 예약 비트면 "rsvd" */
}

/* ---------------- Part C: 블록 전체 실행 + 리포트 + 생성 ---------------- */
typedef struct { int pass, fail, skip; } tally_t;

/* 표 순서대로 전부 검사. rep 에 FAIL/SKIP 줄, SUMMARY 줄, (블록 전체가 같은 값이면) HINT 줄을 쓴다. */
static tally_t reset_test_run(const bus_t *b, const reg_desc_t *regs, size_t n, char *rep, size_t cap) {
    tally_t t = { 0, 0, 0 };
    (void)b; (void)regs; (void)n; (void)cap;
    rep[0] = '\0'; /* TODO: 레지스터마다 reset_check_one → SKIP/FAIL 줄, SUMMARY 줄, 조건 맞으면 HINT 줄 */
    return t;
}

/* 기술서 → C 테스트 벡터 표 생성 (CI 에서 regmap 이 바뀔 때마다 다시 만든다) */
static void emit_reset_vectors(const char *blk, const reg_desc_t *regs, size_t n, char *buf, size_t cap) {
    (void)blk; (void)regs; (void)n; (void)cap;
    buf[0] = '\0'; /* TODO: 머리 주석 + 배열 머리 + 레지스터별 { "NAME", off, exp, mask } 또는 skip 주석 + "};" */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static const reg_desc_t *reg_by_off(uint32_t off) { return &uart0[off >> 2]; }

int main(void) {
    mock_t m;
    bus_t bus = { mock_read32, &m };
    reset_res_t res;
    uint32_t exp, mask;
    char s[2048];

    /* Part A — 기대값·마스크 */
    reset_expect(reg_by_off(OFF_CTRL), &exp, &mask);
    CHECK(exp == 0x00000000u && mask == 0xFFFFFFFFu);
    reset_expect(reg_by_off(OFF_ID), &exp, &mask);
    CHECK(exp == 0x55410100u && mask == 0xFFFFFFFFu);
    reset_expect(reg_by_off(OFF_FIFOCFG), &exp, &mask);
    CHECK(exp == 0x00000804u);
    reset_expect(reg_by_off(OFF_STATUS), &exp, &mask);
    CHECK(exp == 0x00000005u && mask == 0xFFFFFFEFu);         /* CTS(bit4) 제외 */
    reset_expect(reg_by_off(OFF_DBG), &exp, &mask);
    CHECK(exp == 0 && mask == 0x00FFFFFFu);                   /* TAP[31:24] 제외 */
    reset_expect(reg_by_off(OFF_TXDATA), &exp, &mask);
    CHECK(mask == 0);                                         /* WO 만 있음 */
    reset_expect(reg_by_off(OFF_TIMER), &exp, &mask);
    CHECK(mask == 0);                                         /* 전부 VOLATILE */

    /* Part A — 레지스터 1개 검사: 정상 mock 은 PASS, 결함은 FAIL */
    mock_power_on(&m, NULL, 0, 0x2545F491u);
    CHECK(reset_check_one(&bus, reg_by_off(OFF_CTRL), &res) == RES_PASS);
    CHECK(reset_check_one(&bus, reg_by_off(OFF_BAUD), &res) == RES_PASS && res.act == 0x1Au);
    const fault_t tie_off[] = { { OFF_CTRL, 0x1u } };         /* EN 이 1 로 묶여 나옴 */
    mock_power_on(&m, tie_off, 1, 0x2545F491u);
    CHECK(reset_check_one(&bus, reg_by_off(OFF_CTRL), &res) == RES_FAIL);
    CHECK(res.act == 0x1u && res.diff == 0x1u);
    CHECK(m.reads[OFF_CTRL >> 2] == 1);                       /* 정확히 한 번 읽음 */

    /* Part B — 비트·필드 이름 */
    bits_to_str(0x1u, s, sizeof s);        CHECK(strcmp(s, "[0]") == 0);
    bits_to_str(0xF1u, s, sizeof s);       CHECK(strcmp(s, "[7:4,0]") == 0);
    bits_to_str(0x80000001u, s, sizeof s); CHECK(strcmp(s, "[31,0]") == 0);
    bits_to_str(0x1Au, s, sizeof s);       CHECK(strcmp(s, "[4:3,1]") == 0);
    bits_to_str(0xFFFFFFFFu, s, sizeof s); CHECK(strcmp(s, "[31:0]") == 0);
    bits_to_str(0, s, sizeof s);           CHECK(strcmp(s, "[]") == 0);
    fields_of(reg_by_off(OFF_CTRL), 0x19u, s, sizeof s); CHECK(strcmp(s, "[EN,PAR]") == 0);
    fields_of(reg_by_off(OFF_CTRL), 0x41u, s, sizeof s); CHECK(strcmp(s, "[EN,rsvd]") == 0);
    fields_of(reg_by_off(OFF_ID), 0x80u, s, sizeof s);   CHECK(strcmp(s, "[rsvd]") == 0);

    /* Part B — 제외 비트는 아무리 틀려도 보고하지 않는다 (false positive 금지) */
    const fault_t decoys[] = { { OFF_STATUS, 0x10u }, { OFF_DBG, 0xFF000000u } };
    mock_power_on(&m, decoys, 2, 0x2545F491u);
    CHECK(reset_check_one(&bus, reg_by_off(OFF_STATUS), &res) == RES_PASS);
    CHECK(reset_check_one(&bus, reg_by_off(OFF_DBG), &res) == RES_PASS);

    /* Part B — RC: 첫 읽기 값이 증거. 두 번째 읽기는 이미 지워진 값 */
    const fault_t rc_glitch[] = { { OFF_ERRCNT, 0x03u } };
    mock_power_on(&m, rc_glitch, 1, 0x2545F491u);
    CHECK(reset_check_one(&bus, reg_by_off(OFF_ERRCNT), &res) == RES_FAIL && res.act == 0x03u);
    CHECK(m.reads[OFF_ERRCNT >> 2] == 1 && m.val[OFF_ERRCNT >> 2] == 0);

    /* Part B — NOREAD / 검사할 비트 없음: 읽지도 않는다 */
    mock_power_on(&m, NULL, 0, 0x2545F491u);
    CHECK(reset_check_one(&bus, reg_by_off(OFF_RXDATA), &res) == RES_SKIP && m.reads[OFF_RXDATA >> 2] == 0);
    CHECK(reset_check_one(&bus, reg_by_off(OFF_TXDATA), &res) == RES_SKIP && m.reads[OFF_TXDATA >> 2] == 0);
    (void)mock_read32(&m, OFF_RXDATA);                        /* 만약 읽었다면? → INT_RAW.RXUF 오탐 */
    CHECK(reset_check_one(&bus, reg_by_off(OFF_INT_RAW), &res) == RES_FAIL && res.diff == 0x4u);

    /* Part C — 정상 블록 */
    mock_power_on(&m, NULL, 0, 0x2545F491u);
    tally_t t = reset_test_run(&bus, uart0, NREG, s, sizeof s);
    CHECK(t.pass == 9 && t.fail == 0 && t.skip == 3);
    CHECK(strstr(s, "SUMMARY reset_value: pass=9 fail=0 skip=3") != NULL);

    /* Part C — 결함 2개(진짜) + 미끼 2개(제외 비트) 주입 */
    const fault_t inj[] = { { OFF_CTRL, 0x1u }, { OFF_BAUD, 0x1Au }, { OFF_STATUS, 0x10u }, { OFF_DBG, 0xFF000000u } };
    mock_power_on(&m, inj, 4, 0x2545F491u);
    t = reset_test_run(&bus, uart0, NREG, s, sizeof s);
    printf("---- UART0 reset test, 4 faults injected (2 real, 2 in excluded bits) ----\n%s", s);
    CHECK(t.fail == 2 && t.pass == 7);
    CHECK(strstr(s, "FAIL reg=CTRL off=0x004 exp=0x00000000 act=0x00000001 diff=0x00000001 bits=[0] fields=[EN]\n") != NULL);
    CHECK(strstr(s, "FAIL reg=BAUD off=0x008 exp=0x0000001A act=0x00000000 diff=0x0000001A bits=[4:3,1] fields=[DIV]\n") != NULL);
    CHECK(strstr(s, "reg=STATUS") == NULL && strstr(s, "FAIL reg=DBG") == NULL);
    CHECK(strstr(s, "SKIP reg=RXDATA off=0x010 why=read-side-effect") != NULL);
    CHECK(m.reads[OFF_RXDATA >> 2] == 0 && m.reads[OFF_ERRCNT >> 2] == 1);

    /* Part C — 블록이 죽어 있으면(클럭 게이트) 레지스터 버그가 아니라 HINT */
    mock_power_on(&m, NULL, 0, 0x2545F491u);
    m.dead = 0xDEADBEEFu;
    t = reset_test_run(&bus, uart0, NREG, s, sizeof s);
    printf("---- UART0 reset test, block clock-gated ----\n%s", s);
    CHECK(t.fail == 9 && strstr(s, "HINT all 9 checked regs read 0xDEADBEEF") != NULL);

    /* Part C — 테스트 벡터 생성 */
    emit_reset_vectors("UART0", uart0, NREG, s, sizeof s);
    printf("---- generated ----\n%s", s);
    CHECK(strstr(s, "    { \"CTRL\", 0x004u, 0x00000000u, 0xFFFFFFFFu },\n") != NULL);
    CHECK(strstr(s, "    { \"STATUS\", 0x018u, 0x00000005u, 0xFFFFFFEFu },\n") != NULL);
    CHECK(strstr(s, "    /* skip RXDATA: read side effect */\n") != NULL);
    CHECK(strstr(s, "    /* skip TIMER: no checkable bits */\n") != NULL);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
