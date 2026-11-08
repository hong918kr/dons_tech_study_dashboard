/* SIV-B05 Walking 1s / Walking 0s 비트 테스트
 *
 * 32비트 데이터 버스와 RW 레지스터에 walking-1 / walking-0 패턴을 걸어 stuck-at-0/1, 비트 간 short
 * (wired-OR / wired-AND), 끊긴 선(open, bus-hold)을 찾고, 두 패턴의 "증상(syndrome)"으로 결함 종류와 위치를
 * 판정한다. 맥에는 실리콘이 없으므로 SIV-B02 의 mock MMIO 를 축약해 넣었다: 모든 접근은 bus_write / bus_read
 * 를 지나고, 결함은 line_fault_t 설정으로 "버스(모든 레지스터 공통)" 또는 "레지스터 하나(flop)"에 결정적으로
 * 주입한다. 실제 보드에서 bus_read/bus_write 는 *(volatile uint32_t *)(BASE + off) 접근이다
 * (JTAG/OpenOCD 의 mdw / mww 도 같은 일을 한다).
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 결함 모델 (주어진 코드) ---------------- */
typedef enum { BR_NONE = 0, BR_OR, BR_AND } bridge_t;  /* short: 두 선이 같은 값으로 묶인다 */
typedef struct {
    uint32_t sa0;        /* stuck-at-0 선                                            */
    uint32_t sa1;        /* stuck-at-1 선                                            */
    bridge_t bridge;     /* br_a <-> br_b short. OR = 1이 이김, AND = 0이 이김         */
    unsigned br_a, br_b;
    uint32_t open;       /* 끊긴 선 (버스 전용): write 는 안 닿고 read 는 bus-hold 값 */
} line_fault_t;

static uint32_t apply_lines(const line_fault_t *f, uint32_t v) {
    if (f->bridge != BR_NONE) {
        uint32_t a = (v >> f->br_a) & 1u, b = (v >> f->br_b) & 1u;
        uint32_t r = (f->bridge == BR_OR) ? (a | b) : (a & b);
        v = (v & ~((1u << f->br_a) | (1u << f->br_b))) | (r << f->br_a) | (r << f->br_b);
    }
    return (v & ~f->sa0) | f->sa1;
}

/* ---------------- 레지스터 기술 (SIV-B01 기술서의 축소판) ---------------- */
typedef struct {
    const char *name;
    uint32_t    off;
    uint32_t    rw_mask;    /* 하드웨어: 쓰기가 저장되는 비트 (나머지는 RO = reset 값 유지)  */
    uint32_t    walk_mask;  /* 테스트: 걸어도 안전한 비트 (EN/START 같은 부작용 비트 제외)   */
    uint32_t    reset;
} reg_desc_t;

/* 가상의 DMA0 IP (오프셋만, base 생략) */
enum { R_SCRATCH, R_CTRL, R_SRC, R_DST, R_LEN, R_STATUS, R_IRQ_EN, R_ID, NREG };
static const reg_desc_t dma_regs[NREG] = {
    { "SCRATCH", 0x000, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x00000000u },
    { "CTRL",    0x004, 0x0000FFFFu, 0x0000FFFCu, 0x00000000u }, /* [0]EN [1]START 은 걷지 않는다 */
    { "SRC",     0x008, 0xFFFFFFFCu, 0xFFFFFFFCu, 0x00000000u }, /* word 정렬: [1:0] RO 0       */
    { "DST",     0x00C, 0xFFFFFFFCu, 0xFFFFFFFCu, 0x00000000u },
    { "LEN",     0x010, 0x00FFFFFFu, 0x00FFFFFFu, 0x00000000u },
    { "STATUS",  0x014, 0x00000000u, 0x00000000u, 0x00000001u }, /* RO: walk 대상 아님           */
    { "IRQ_EN",  0x018, 0x0000001Fu, 0x0000001Fu, 0x00000000u },
    { "ID",      0x01C, 0x00000000u, 0x00000000u, 0xD3A00105u },
};

/* ---------------- mock SoC: 데이터 버스 + 레지스터 파일 (SIV-B02 mock 축약, 주어진 코드) ---------------- */
typedef struct {
    uint32_t     cell[NREG];      /* 레지스터 flop                                   */
    line_fault_t bus;             /* 데이터 버스 결함: 모든 레지스터 접근에 공통        */
    line_fault_t regf[NREG];      /* 레지스터 하나의 결함 (flop / write-enable 경로)   */
    uint32_t     hold;            /* bus-hold: CPU 쪽 버스에 마지막으로 실린 값        */
    uint32_t     toggled[NREG];   /* 한 번이라도 바뀐 비트 (부작용 비트 감시용)        */
} soc_t;

static void soc_reset(soc_t *s) {
    memset(s, 0, sizeof *s);
    for (int i = 0; i < NREG; i++) s->cell[i] = dma_regs[i].reset;
}
static int off_to_idx(uint32_t off) { return (off % 4u == 0 && off / 4u < NREG) ? (int)(off / 4u) : -1; }

static void bus_write(soc_t *s, uint32_t off, uint32_t v) {
    int i = off_to_idx(off);
    uint32_t w = apply_lines(&s->bus, v);              /* 선을 지나며 변형된 값    */
    s->hold = w;                                       /* CPU 쪽 선은 이 값을 기억 */
    if (i < 0) return;                                 /* 디코드 안 됨: 무시       */
    uint32_t in  = apply_lines(&s->regf[i], w);
    uint32_t en  = dma_regs[i].rw_mask & ~s->bus.open; /* 끊긴 선은 flop 까지 안 닿음 */
    uint32_t old = s->cell[i];
    s->cell[i] = (old & ~en) | (in & en);
    s->toggled[i] |= old ^ s->cell[i];
}
static uint32_t bus_read(soc_t *s, uint32_t off) {
    int i = off_to_idx(off);
    uint32_t v = (i < 0) ? 0xDEADBEEFu : apply_lines(&s->regf[i], s->cell[i]);
    v = apply_lines(&s->bus, v);
    v = (v & ~s->bus.open) | (s->hold & s->bus.open);  /* 끊긴 선 = 직전 값이 남아 있음 */
    s->hold = v;
    return v;
}

/* ---------------- Part A: walking-1 / walking-0 ---------------- */
typedef enum { WALK1, WALK0 } walk_kind_t;

typedef struct {
    int      nsteps;      /* 실행한 단계 수 = mask 의 1 개수                  */
    int      nfail;       /* 실패한 단계 수                                    */
    uint32_t fail_steps;  /* bit i = "비트 i 를 걷는 단계"가 실패               */
    uint32_t rise;        /* 0 을 기대했는데 1 로 읽힌 비트 (모든 단계 OR)      */
    uint32_t fall;        /* 1 을 기대했는데 0 으로 읽힌 비트                   */
    int      first_step;  /* 첫 실패 단계 (-1 = 없음)                           */
    uint32_t first_exp, first_act;   /* 첫 실패의 기대값 / 실제값 (mask 적용)   */
} walk_res_t;

#define NO_DECOY 0xFFFFFFFFu

/* 단계 i 에서 mask 안에 쓸 패턴 (mask 밖 비트는 0) */
static uint32_t walk_pattern(walk_kind_t k, uint32_t mask, unsigned i) {
    uint32_t b = 1u << i;
    return (k == WALK1) ? b : (mask & ~b);
}

/* off 레지스터의 mask 비트를 하나씩 걷는다. mask 밖 비트는 원래 값 유지, 끝나면 원래 값 복구.
 * decoy != NO_DECOY 면 매 단계 write 직후 decoy 에 ~패턴을 써서 버스를 반대로 흔든다 (bus-hold 대책). */
static walk_res_t walk(soc_t *s, uint32_t off, walk_kind_t k, uint32_t mask, uint32_t decoy) {
    walk_res_t r = { 0, 0, 0, 0, 0, -1, 0, 0 };
    uint32_t saved = bus_read(s, off);
    uint32_t dsave = (decoy != NO_DECOY) ? bus_read(s, decoy) : 0;
    for (unsigned i = 0; i < 32; i++) {
        if (!(mask & (1u << i))) continue;
        uint32_t exp = walk_pattern(k, mask, i);
        bus_write(s, off, (saved & ~mask) | exp);
        if (decoy != NO_DECOY) bus_write(s, decoy, ~exp);
        uint32_t act = bus_read(s, off) & mask;
        r.nsteps++;
        if (act != exp) {
            if (r.nfail == 0) { r.first_step = (int)i; r.first_exp = exp; r.first_act = act; }
            r.nfail++;
            r.fail_steps |= 1u << i;
            r.rise |= act & ~exp;
            r.fall |= exp & ~act;
        }
    }
    if (decoy != NO_DECOY) bus_write(s, decoy, dsave);
    bus_write(s, off, saved);
    return r;
}

/* ---------------- Part B: 증상으로 결함 판정 ---------------- */
typedef enum { D_PASS, D_SA0, D_SA1, D_OR_SHORT, D_AND_SHORT, D_OPEN, D_UNKNOWN } dkind_t;
static const char *const DK_NAME[] = { "PASS", "SA0", "SA1", "OR_SHORT", "AND_SHORT", "OPEN", "UNKNOWN" };
typedef struct { dkind_t kind; int a, b; } diag_t;   /* a < b: 관련 비트 (없으면 -1) */

static int popc(uint32_t x) { int c = 0; while (x) { x &= x - 1u; c++; } return c; }
static int lowbit(uint32_t x) { int i = 0; if (!x) return -1; while (!(x & 1u)) { x >>= 1; i++; } return i; }

/* 단일 결함 가정. w1 = walking-1 결과, w0 = walking-0 결과 (같은 mask, decoy 사용) */
static diag_t classify(const walk_res_t *w1, const walk_res_t *w0) {
    diag_t d = { D_UNKNOWN, -1, -1 };
    uint32_t rise = w1->rise | w0->rise, fall = w1->fall | w0->fall, bad = rise | fall;
    if (bad == 0) { d.kind = D_PASS; return d; }
    d.a = lowbit(bad);
    if (popc(bad) == 1) {
        if (!rise)      d.kind = D_SA0;                     /* 항상 0 으로만 틀림 */
        else if (!fall) d.kind = D_SA1;                     /* 항상 1 로만 틀림   */
        else if ((w1->rise & w1->fall & w0->rise & w0->fall) == bad)
                        d.kind = D_OPEN;                    /* 쓴 값의 반대(=decoy)를 따라감 */
        return d;
    }
    if (popc(bad) == 2) {
        d.b = lowbit(bad & (bad - 1u));
        if (!fall && w1->fail_steps == bad)      d.kind = D_OR_SHORT;   /* 이중 SA1 이면 walk1 전 단계 실패 */
        else if (!rise && w0->fail_steps == bad) d.kind = D_AND_SHORT;  /* 이중 SA0 이면 walk0 전 단계 실패 */
    }
    return d;
}

/* ---------------- 결함 x 패턴 매트릭스 (주어진 코드) ---------------- */
enum { PS_ZERO_ONES, PS_CHECKER, PS_WALK1, PS_WALK0, PS_W10_SHAKE, NPS };
static const char *const PS_NAME[NPS] = { "0/~0", "55/AA", "walk1", "walk0", "w1+w0+shake" };

static bool fixed_patterns_fail(soc_t *s, uint32_t off, const uint32_t *p, int n) {
    for (int i = 0; i < n; i++) { bus_write(s, off, p[i]); if (bus_read(s, off) != p[i]) return true; }
    return false;
}
/* 버스 결함 f 를 넣은 새 SoC 의 SCRATCH 에 패턴 세트 ps 를 걸어 검출하면 true */
static bool detects(int ps, const line_fault_t *f) {
    static const uint32_t zo[2] = { 0x00000000u, 0xFFFFFFFFu }, ck[2] = { 0x55555555u, 0xAAAAAAAAu };
    soc_t s; soc_reset(&s); s.bus = *f;
    switch (ps) {
    case PS_ZERO_ONES: return fixed_patterns_fail(&s, 0x000, zo, 2);
    case PS_CHECKER:   return fixed_patterns_fail(&s, 0x000, ck, 2);
    case PS_WALK1:     return walk(&s, 0x000, WALK1, 0xFFFFFFFFu, NO_DECOY).nfail > 0;
    case PS_WALK0:     return walk(&s, 0x000, WALK0, 0xFFFFFFFFu, NO_DECOY).nfail > 0;
    default:           return walk(&s, 0x000, WALK1, 0xFFFFFFFFu, 0x008).nfail > 0
                           || walk(&s, 0x000, WALK0, 0xFFFFFFFFu, 0x008).nfail > 0;
    }
}
static const struct { const char *name; line_fault_t f; unsigned expect; } FAULTS[] = {
    /* expect: bit ps = 패턴 세트 ps 가 검출해야 함 */
    { "SA0 D7",    { .sa0 = 1u << 7 },                           0x1F },
    { "SA1 D7",    { .sa1 = 1u << 7 },                           0x1F },
    { "OR  D7-D8", { .bridge = BR_OR,  .br_a = 7, .br_b = 8 },   0x1E },
    { "AND D7-D8", { .bridge = BR_AND, .br_a = 7, .br_b = 8 },   0x1E },
    { "OR  D0-D2", { .bridge = BR_OR,  .br_a = 0, .br_b = 2 },   0x1C },
    { "AND D0-D2", { .bridge = BR_AND, .br_a = 0, .br_b = 2 },   0x1C },
    { "OPEN D7",   { .open = 1u << 7 },                          0x10 },
};
#define NFAULTS ((int)(sizeof FAULTS / sizeof FAULTS[0]))
static unsigned matrix_row(int fi) {
    unsigned m = 0;
    for (int ps = 0; ps < NPS; ps++) if (detects(ps, &FAULTS[fi].f)) m |= 1u << ps;
    return m;
}

/* ---------------- Part C: IP 블록 전체 walk + 위치 추정 리포트 ---------------- */
typedef struct {
    int      fail_steps;       /* 모든 레지스터 x 패턴의 실패 단계 합                */
    int      regs_failed;
    uint32_t coverage;         /* 실제로 걸어 본 데이터 선 = walk_mask 들의 OR        */
    uint32_t bus_suspect;      /* 2개 이상 레지스터에서 나쁜 비트 -> 공통 경로(버스)   */
    uint32_t reg_suspect;      /* 한 레지스터에서만 나쁜 비트 -> 그 레지스터           */
    int      reg_suspect_idx;  /* 그 레지스터의 표 인덱스 (-1 = 없음)                */
} block_sum_t;

static void appendf(char *buf, size_t cap, const char *fmt, ...) {
    size_t n = strlen(buf);
    if (n + 1 >= cap) return;
    va_list ap; va_start(ap, fmt); vsnprintf(buf + n, cap - n, fmt, ap); va_end(ap);
}
static void bits_str(uint32_t m, char *out, size_t cap) {          /* 0x180 -> "[7,8]" */
    out[0] = '\0'; appendf(out, cap, "[");
    for (int b = 0; b < 32; b++) if (m & (1u << b)) appendf(out, cap, "%s%d", out[1] ? "," : "", b);
    appendf(out, cap, "]");
}
static void report_fail(char *rep, size_t cap, const reg_desc_t *d, const char *test, const walk_res_t *r) {
    char bs[128]; bits_str(r->first_exp ^ r->first_act, bs, sizeof bs);
    appendf(rep, cap, "FAIL reg=%s off=0x%03X test=%s step=%d exp=0x%08X act=0x%08X diff=0x%08X bits=%s",
            d->name, (unsigned)d->off, test, r->first_step, (unsigned)r->first_exp, (unsigned)r->first_act,
            (unsigned)(r->first_exp ^ r->first_act), bs);
    if (r->nfail > 1) appendf(rep, cap, " (+%d more steps)", r->nfail - 1);
    appendf(rep, cap, "\n");
}

/* t[0..n) 중 walk_mask != 0 인 레지스터마다 walk1 + walk0 (decoy 로 버스 흔들기) 실행.
 * decoy 레지스터 자신을 걸을 때는 decoy_alt 를 쓴다. 리포트(rep)에 FAIL / DIAG / SUSPECT / RESULT 줄을 쓴다. */
static block_sum_t block_walk(soc_t *s, const reg_desc_t *t, int n, uint32_t decoy, uint32_t decoy_alt,
                              char *rep, size_t cap) {
    block_sum_t sum = { 0, 0, 0, 0, 0, -1 };
    int hits[32] = { 0 };
    uint32_t bad_of[32] = { 0 };
    char bs[128];
    rep[0] = '\0';
    for (int i = 0; i < n && i < 32; i++) {
        if (!t[i].walk_mask) continue;
        uint32_t dc = (t[i].off == decoy) ? decoy_alt : decoy;
        walk_res_t w1 = walk(s, t[i].off, WALK1, t[i].walk_mask, dc);
        walk_res_t w0 = walk(s, t[i].off, WALK0, t[i].walk_mask, dc);
        sum.coverage |= t[i].walk_mask;
        if (!w1.nfail && !w0.nfail) continue;
        if (w1.nfail) report_fail(rep, cap, &t[i], "walk1", &w1);
        if (w0.nfail) report_fail(rep, cap, &t[i], "walk0", &w0);
        sum.fail_steps += w1.nfail + w0.nfail;
        sum.regs_failed++;
        bad_of[i] = w1.rise | w1.fall | w0.rise | w0.fall;
        for (int b = 0; b < 32; b++) if (bad_of[i] & (1u << b)) hits[b]++;
        diag_t d = classify(&w1, &w0);
        bits_str(bad_of[i], bs, sizeof bs);
        appendf(rep, cap, "DIAG reg=%s class=%s bits=%s\n", t[i].name, DK_NAME[d.kind], bs);
    }
    for (int b = 0; b < 32; b++) {
        if (hits[b] >= 2)      sum.bus_suspect |= 1u << b;
        else if (hits[b] == 1) sum.reg_suspect |= 1u << b;
    }
    if (sum.bus_suspect) {
        bits_str(sum.bus_suspect, bs, sizeof bs);
        appendf(rep, cap, "SUSPECT bus lines=%s (seen in >=2 registers: common data path)\n", bs);
    }
    for (int i = 0; i < n && i < 32; i++) {
        if (!(bad_of[i] & sum.reg_suspect)) continue;
        sum.reg_suspect_idx = i;
        bits_str(bad_of[i] & sum.reg_suspect, bs, sizeof bs);
        appendf(rep, cap, "SUSPECT reg=%s bits=%s (only this register: flop / write path)\n", t[i].name, bs);
    }
    appendf(rep, cap, "RESULT %s failed_regs=%d failed_steps=%d coverage=0x%08X\n",
            sum.regs_failed ? "FAIL" : "PASS", sum.regs_failed, sum.fail_steps, (unsigned)sum.coverage);
    return sum;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static walk_res_t walk_bus_fault(const line_fault_t *f, walk_kind_t k, uint32_t decoy) {
    soc_t s; soc_reset(&s); s.bus = *f;
    return walk(&s, 0x000, k, 0xFFFFFFFFu, decoy);
}
static diag_t diag_bus_fault(const line_fault_t *f) {
    walk_res_t w1 = walk_bus_fault(f, WALK1, 0x008), w0 = walk_bus_fault(f, WALK0, 0x008);
    return classify(&w1, &w0);
}
static void preset(soc_t *s) {        /* "backdoor" 로 테스트 전 값을 심는다 */
    s->cell[R_SCRATCH] = 0x12345678u; s->cell[R_CTRL] = 0x0000A5A1u; /* EN=1 */
    s->cell[R_SRC] = 0x80001000u; s->cell[R_DST] = 0x80002000u; s->cell[R_LEN] = 0x40u;
}

int main(void) {
    static char rep[4096];
    soc_t s;
    walk_res_t r, r0;
    line_fault_t none = { 0 };

    /* Part A — 패턴과 기본 walk (SCRATCH, 32비트 전체) */
    CHECK(walk_pattern(WALK1, 0xFFFFFFFFu, 0) == 0x00000001u);
    CHECK(walk_pattern(WALK1, 0xFFFFFFFFu, 31) == 0x80000000u);
    CHECK(walk_pattern(WALK0, 0xFFFFFFFFu, 0) == 0xFFFFFFFEu);
    CHECK(walk_pattern(WALK0, 0x0000FFFCu, 2) == 0x0000FFF8u);
    r = walk_bus_fault(&none, WALK1, NO_DECOY);
    CHECK(r.nsteps == 32 && r.nfail == 0 && r.first_step == -1);
    r = walk_bus_fault(&none, WALK0, NO_DECOY);
    CHECK(r.nsteps == 32 && r.nfail == 0);
    line_fault_t sa0_7 = { .sa0 = 1u << 7 }, sa1_7 = { .sa1 = 1u << 7 };
    r = walk_bus_fault(&sa0_7, WALK1, NO_DECOY);
    CHECK(r.nfail == 1 && r.first_step == 7 && r.first_exp == 0x80u && r.first_act == 0u);
    CHECK(r.fall == 0x80u && r.rise == 0u);
    r = walk_bus_fault(&sa0_7, WALK0, NO_DECOY);
    CHECK(r.nfail == 31 && r.fall == 0x80u);           /* walk0 는 SA0 를 31번 본다 */
    r = walk_bus_fault(&sa1_7, WALK1, NO_DECOY);
    CHECK(r.nfail == 31 && r.rise == 0x80u);
    r = walk_bus_fault(&sa1_7, WALK0, NO_DECOY);
    CHECK(r.nfail == 1 && r.first_step == 7 && r.first_act == 0xFFFFFFFFu);
    line_fault_t or78 = { .bridge = BR_OR, .br_a = 7, .br_b = 8 }, and78 = { .bridge = BR_AND, .br_a = 7, .br_b = 8 };
    r = walk_bus_fault(&or78, WALK1, NO_DECOY);
    CHECK(r.fail_steps == 0x180u && r.rise == 0x180u && r.first_act == 0x180u);
    r = walk_bus_fault(&and78, WALK1, NO_DECOY);
    CHECK(r.fail_steps == 0x180u && r.fall == 0x180u && r.first_act == 0u);
    soc_reset(&s); s.cell[R_SCRATCH] = 0xCAFEF00Du;
    r = walk(&s, 0x000, WALK1, 0xFFFFFFFFu, NO_DECOY);
    CHECK(s.cell[R_SCRATCH] == 0xCAFEF00Du);           /* 테스트 후 원래 값 복구 */

    /* Part B — mask, 부작용 비트, coverage 구멍, bus-hold, 판정 */
    soc_reset(&s); s.cell[R_CTRL] = 0x00000001u;       /* EN=1 인 채로 */
    r = walk(&s, 0x004, WALK1, 0x0000FFFCu, NO_DECOY);
    r0 = walk(&s, 0x004, WALK0, 0x0000FFFCu, NO_DECOY);
    CHECK(r.nsteps == 14 && r.nfail == 0 && r0.nsteps == 14 && r0.nfail == 0);
    CHECK((s.toggled[R_CTRL] & 0x3u) == 0 && s.cell[R_CTRL] == 0x00000001u);  /* EN/START 한 번도 안 바뀜 */
    line_fault_t sa1_1 = { .sa1 = 1u << 1 };
    soc_reset(&s); s.bus = sa1_1;
    CHECK(walk(&s, 0x008, WALK1, 0xFFFFFFFCu, NO_DECOY).nfail == 0);   /* SRC 는 D1 을 못 본다 */
    CHECK(walk(&s, 0x000, WALK1, 0xFFFFFFFFu, NO_DECOY).nfail == 31);  /* SCRATCH 는 본다      */
    line_fault_t open7 = { .open = 1u << 7 };
    CHECK(walk_bus_fault(&open7, WALK1, NO_DECOY).nfail == 0);         /* 끊긴 선이 통과해 버린다 */
    CHECK(walk_bus_fault(&open7, WALK0, NO_DECOY).nfail == 0);
    CHECK(walk_bus_fault(&open7, WALK1, 0x008).nfail == 32);           /* 버스를 흔들면 잡힌다    */
    diag_t d;
    d = diag_bus_fault(&none);  CHECK(d.kind == D_PASS);
    d = diag_bus_fault(&sa0_7); CHECK(d.kind == D_SA0 && d.a == 7);
    d = diag_bus_fault(&sa1_7); CHECK(d.kind == D_SA1 && d.a == 7);
    d = diag_bus_fault(&or78);  CHECK(d.kind == D_OR_SHORT && d.a == 7 && d.b == 8);
    d = diag_bus_fault(&and78); CHECK(d.kind == D_AND_SHORT && d.a == 7 && d.b == 8);
    line_fault_t or02 = { .bridge = BR_OR, .br_a = 0, .br_b = 2 };
    d = diag_bus_fault(&or02);  CHECK(d.kind == D_OR_SHORT && d.a == 0 && d.b == 2);
    line_fault_t dsa0 = { .sa0 = (1u << 7) | (1u << 8) };              /* 이중 SA0 은 AND short 가 아니다 */
    d = diag_bus_fault(&dsa0);  CHECK(d.kind == D_UNKNOWN && d.a == 7 && d.b == 8);
    d = diag_bus_fault(&open7); CHECK(d.kind == D_OPEN && d.a == 7);
    printf("fault       ");
    for (int ps = 0; ps < NPS; ps++) printf(" %-11s", PS_NAME[ps]);
    printf("\n");
    for (int fi = 0; fi < NFAULTS; fi++) {
        unsigned m = matrix_row(fi);
        printf("%-11s ", FAULTS[fi].name);
        for (int ps = 0; ps < NPS; ps++) printf(" %-11s", (m >> ps) & 1u ? "detect" : "-");
        printf("\n");
        CHECK(m == FAULTS[fi].expect);
    }

    /* Part C — DMA0 블록 전체: 결함 없음 / 버스 short / 레지스터 하나 / 끊긴 선 */
    block_sum_t sum;
    soc_t before;
    soc_reset(&s); preset(&s); before = s;
    sum = block_walk(&s, dma_regs, NREG, 0x000, 0x008, rep, sizeof rep);
    CHECK(sum.fail_steps == 0 && sum.regs_failed == 0 && sum.coverage == 0xFFFFFFFFu);
    CHECK(strstr(rep, "RESULT PASS failed_regs=0 failed_steps=0 coverage=0xFFFFFFFF") != NULL);
    CHECK(memcmp(s.cell, before.cell, sizeof s.cell) == 0);           /* 모든 레지스터 복구 */
    CHECK((s.toggled[R_CTRL] & 0x3u) == 0);                            /* EN/START 안 건드림 */

    soc_reset(&s); preset(&s); s.bus = or78;
    sum = block_walk(&s, dma_regs, NREG, 0x000, 0x008, rep, sizeof rep);
    printf("---- report: bus OR-short D7-D8 ----\n%s", rep);
    CHECK(sum.bus_suspect == 0x180u && sum.reg_suspect == 0 && sum.regs_failed == 5 && sum.fail_steps == 20);
    CHECK(strstr(rep, "FAIL reg=SCRATCH off=0x000 test=walk1 step=7 exp=0x00000080 act=0x00000180 "
                      "diff=0x00000100 bits=[8] (+1 more steps)") != NULL);
    CHECK(strstr(rep, "DIAG reg=SCRATCH class=OR_SHORT bits=[7,8]") != NULL);
    CHECK(strstr(rep, "SUSPECT bus lines=[7,8]") != NULL);

    soc_reset(&s); preset(&s); s.regf[R_CTRL].sa1 = 1u << 3;
    sum = block_walk(&s, dma_regs, NREG, 0x000, 0x008, rep, sizeof rep);
    printf("---- report: CTRL flop bit3 stuck-at-1 ----\n%s", rep);
    CHECK(sum.bus_suspect == 0 && sum.reg_suspect == 0x8u && sum.reg_suspect_idx == R_CTRL);
    CHECK(strstr(rep, "DIAG reg=CTRL class=SA1 bits=[3]") != NULL);
    CHECK(strstr(rep, "SUSPECT reg=CTRL bits=[3] (only this register") != NULL);

    line_fault_t open5 = { .open = 1u << 5 };
    soc_reset(&s); preset(&s); s.bus = open5;
    sum = block_walk(&s, dma_regs, NREG, 0x000, 0x008, rep, sizeof rep);
    printf("---- report: bus open D5 ----\n%s", rep);
    CHECK(sum.bus_suspect == 0x20u && sum.regs_failed == 5);
    CHECK(strstr(rep, "DIAG reg=LEN class=OPEN bits=[5]") != NULL);
    soc_reset(&s); preset(&s); s.bus = open5;
    sum = block_walk(&s, dma_regs, NREG, NO_DECOY, NO_DECOY, rep, sizeof rep);
    CHECK(sum.regs_failed == 0 && strstr(rep, "RESULT PASS") != NULL); /* decoy 없으면 놓친다 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
