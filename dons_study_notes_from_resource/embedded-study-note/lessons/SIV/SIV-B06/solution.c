/* SIV-B06 주소 디코드·Aliasing 테스트
 *
 * 대상 IP: 가상 DMA 컨트롤러 "VDMA". 인터커넥트가 1 KB 슬롯(오프셋 0x000–0x3FC)을 주었고,
 * 레지스터는 0x000–0x1FC 에만 있다. 0x200–0x3FC 는 홀(hole) → 정상 디코더는 DECERR 응답.
 * 즉 디코더는 주소 비트 [9:2] 를 전부 봐야 한다.
 *
 * mock 은 SIV-B02 의 레지스터 파일을 축약해 넣었다: 기술서(regs[]) + 레지스터별 플롭 값(g_val[]) +
 * access type(RW/RO/W1C/RC) 시맨틱 + 결함 주입(fault_t). 결함은 "디코더" 에만 넣는다:
 *   ignore_bit  : 주소 비트 k 가 디코더 입력에서 0 으로 보임 (주소선 끊김 / 디코더가 비트를 안 봄)
 *   remap_from/to: 디코드 테이블 한 줄이 틀림 → from 주소 접근이 to 레지스터를 선택
 * 실제 칩에서 reg_read32(off) 는 *(volatile uint32_t *)(BASE + off) 이고, DECERR 는 버스 에러
 * (Cortex-A 는 external abort, Cortex-M 은 BusFault) — 테스트 코드는 폴트 핸들러가 센 횟수를 본다.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ---------------- 레지스터 기술서 (SIV-B01 생성 결과의 축약) ---------------- */
typedef enum { AT_RW, AT_RO, AT_W1C, AT_RC } acc_t;
typedef struct {
    const char *name;
    uint32_t    off;
    acc_t       acc;
    uint32_t    mask;    /* RW: 쓸 수 있는 비트 / 나머지: 의미 있는 비트 */
    uint32_t    reset;
} reg_desc_t;

#define CH(n, b)                                                   \
    {"CH" #n "_SRC",   (b) + 0x00u, AT_RW, 0xFFFFFFFFu, 0x0u},     \
    {"CH" #n "_DST",   (b) + 0x04u, AT_RW, 0xFFFFFFFFu, 0x0u},     \
    {"CH" #n "_LEN",   (b) + 0x08u, AT_RW, 0x00FFFFFFu, 0x0u},     \
    {"CH" #n "_CCTRL", (b) + 0x0Cu, AT_RW, 0x00000003u, 0x0u},     \
    {"CH" #n "_CSTAT", (b) + 0x10u, AT_RO, 0xFFFFFFFFu, 0x1u}

static const reg_desc_t regs[] = {
    {"CTRL",    0x000u, AT_RW,  0x00000107u, 0x00000000u},
    {"SCRATCH", 0x004u, AT_RW,  0xFFFFFFFFu, 0x00000000u},
    {"INTEN",   0x008u, AT_RW,  0x0000000Fu, 0x00000000u},
    {"INTSTAT", 0x00Cu, AT_W1C, 0x0000000Fu, 0x00000000u},
    {"CFG",     0x010u, AT_RW,  0x0000FFFFu, 0x00000010u},
    {"DBG_CNT", 0x014u, AT_RC,  0x0000FFFFu, 0x00000000u},
    {"ID",      0x018u, AT_RO,  0xFFFFFFFFu, 0x56444D41u},   /* "VDMA" */
    {"VERSION", 0x01Cu, AT_RO,  0xFFFFFFFFu, 0x00010002u},
    {"TIMEOUT", 0x020u, AT_RW,  0x0000FFFFu, 0x00000FFFu},
    {"PRIO",    0x040u, AT_RW,  0x000000FFu, 0x00000000u},
    CH(0, 0x080u), CH(1, 0x0C0u), CH(2, 0x100u), CH(3, 0x140u),
};
#define NREGS       (sizeof regs / sizeof regs[0])
#define SLOT_SIZE   0x400u        /* 인터커넥트 슬롯 1 KB → 주소 비트 [9:2] */
#define WALK_LO_BIT 2             /* walking-address 탐침: 0x004 .. 0x100 (구현 영역 안) */
#define WALK_HI_BIT 8
#define DECERR_VAL  0xDEADBEEFu   /* 이 mock 의 "디코드 에러" 읽기값 (+ 버스 에러 카운트) */

static int desc_find(uint32_t off) {
    for (size_t i = 0; i < NREGS; i++)
        if (regs[i].off == off) return (int)i;
    return -1;
}
static uint32_t mask_of(uint32_t off) { int i = desc_find(off); return i < 0 ? 0xFFFFFFFFu : regs[i].mask; }

/* ---------------- mock 버스 + 디코더 (고치지 않는다) ---------------- */
typedef struct { int ignore_bit; int remap_from; int remap_to; } fault_t;
static const fault_t NO_FAULT = {-1, -1, -1};
static fault_t  g_fault;
static uint32_t g_val[NREGS];
static unsigned g_bus_err, g_rc_reads, g_w1c_writes, g_ro_writes;

static int decode(uint32_t off) {                     /* 버스 주소 → 레지스터 인덱스, 없으면 -1 */
    if (off >= SLOT_SIZE || (off & 3u)) return -1;
    if (g_fault.ignore_bit >= 0) off &= ~(1u << g_fault.ignore_bit);
    if (g_fault.remap_from >= 0 && off == (uint32_t)g_fault.remap_from) off = (uint32_t)g_fault.remap_to;
    return desc_find(off);
}
static uint32_t reg_read32(uint32_t off) {
    int i = decode(off);
    if (i < 0) { g_bus_err++; return DECERR_VAL; }
    uint32_t v = g_val[i];
    if (regs[i].acc == AT_RC) { g_rc_reads++; g_val[i] = 0; }   /* 읽으면 지워진다 */
    return v;
}
static void reg_write32(uint32_t off, uint32_t v) {
    int i = decode(off);
    if (i < 0) { g_bus_err++; return; }
    switch (regs[i].acc) {
    case AT_RW:  g_val[i] = (g_val[i] & ~regs[i].mask) | (v & regs[i].mask); break;
    case AT_W1C: g_w1c_writes++; g_val[i] &= ~(v & regs[i].mask); break;
    default:     g_ro_writes++; break;                              /* RO, RC: 무시 */
    }
}
static void mock_reset(const fault_t *f) {
    g_fault = *f;
    for (size_t i = 0; i < NREGS; i++) g_val[i] = regs[i].reset;
    g_val[desc_find(0x00Cu)] = 0x5u;   /* INTSTAT: HW 가 세워 둔 pending 비트 */
    g_val[desc_find(0x014u)] = 7u;     /* DBG_CNT: 지금까지 센 값 */
    g_bus_err = g_rc_reads = g_w1c_writes = g_ro_writes = 0;
}
static unsigned bus_err_count(void) { return g_bus_err; }   /* 실칩: abort 핸들러가 센 횟수 */

/* ---------------- 실패 기록 ---------------- */
typedef struct { uint32_t off, exp, act, mask; } fail_t;
#define MAX_FAILS 64
typedef struct { int n; fail_t f[MAX_FAILS]; } fail_log_t;   /* n 은 전체 개수, 저장은 앞 64개 */

static void log_fail(fail_log_t *lg, uint32_t off, uint32_t exp, uint32_t act, uint32_t mask) {
    if (!lg) return;
    if (lg->n < MAX_FAILS) lg->f[lg->n] = (fail_t){off, exp, act, mask};
    lg->n++;
}
/* 레지스터 i 를 읽어 exp 와 (mask 비트만) 비교. 틀리면 1 */
static int check_reg(fail_log_t *lg, size_t i, uint32_t exp) {
    uint32_t act = reg_read32(regs[i].off);
    if (((act ^ exp) & regs[i].mask) == 0) return 0;
    log_fail(lg, regs[i].off, exp & regs[i].mask, act, regs[i].mask);
    return 1;
}

/* ---------------- Part A-1: 주소마다 고유한 패턴 ---------------- */
/* 같은 off → 같은 값, 다른 off → (32비트 전체로는) 반드시 다른 값, 하위 비트까지 골고루 섞일 것 */
static uint32_t addr_hash(uint32_t off) {
    uint32_t x = off ^ 0x5A5A5A5Au;          /* off=0 이 0 이 되지 않게 */
    x ^= x >> 16; x *= 0x7FEB352Du;          /* 홀수 곱 · xorshift = 전단사 */
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/* ---------------- Part A-2: 고유 패턴 덮어쓰기 검사 ---------------- */
/* 모든 RW 에 addr_hash 를 "먼저 다 쓰고" 그다음 "전부 읽는다". 2패스(원본, 보수).
 * RO 는 쓰지 않고 리셋값 그대로인지 읽어 본다. W1C/RC 는 쓰지도 읽지도 않는다. 반환: 불일치 개수 */
static int alias_unique_test(fail_log_t *lg) {
    int fails = 0;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t inv = pass ? 0xFFFFFFFFu : 0u;
        for (size_t i = 0; i < NREGS; i++)
            if (regs[i].acc == AT_RW) reg_write32(regs[i].off, addr_hash(regs[i].off) ^ inv);
        for (size_t i = 0; i < NREGS; i++) {
            if (regs[i].acc == AT_RW)      fails += check_reg(lg, i, addr_hash(regs[i].off) ^ inv);
            else if (regs[i].acc == AT_RO) fails += check_reg(lg, i, regs[i].reset);
        }
    }
    return fails;
}

/* ---------------- Part A-3: walking-address (2^k 오프셋) ---------------- */
/* 앵커 0x000 과 탐침 1<<k (k = 2..8). 반환: 의심되는 주소 비트의 마스크 (bit k → 0x1<<k) */
#define PAT  0xAAAAAAAAu
#define ANTI 0x55555555u
static uint32_t walk_addr_test(void) {
    uint32_t bad = 0, base[32] = {0};
    for (int k = WALK_LO_BIT; k <= WALK_HI_BIT; k++) reg_write32(1u << k, PAT);
    reg_write32(0u, PAT);
    for (int k = WALK_LO_BIT; k <= WALK_HI_BIT; k++) {        /* 0) 기본: 쓴 값이 남았나 */
        base[k] = reg_read32(1u << k);
        if (((base[k] ^ PAT) & mask_of(1u << k)) != 0) bad |= 1u << k;
    }
    uint32_t a0 = reg_read32(0u);
    reg_write32(0u, ANTI);                                     /* 1) 앵커만 바꾼다 */
    for (int k = WALK_LO_BIT; k <= WALK_HI_BIT; k++)
        if (reg_read32(1u << k) != base[k]) bad |= 1u << k;    /*    탐침 k 가 따라 바뀌면 A[k] 접힘 */
    reg_write32(0u, PAT);
    for (int k = WALK_LO_BIT; k <= WALK_HI_BIT; k++) {         /* 2) 탐침 k 하나만 바꾼다 */
        reg_write32(1u << k, ANTI);
        if (reg_read32(0u) != a0) bad |= 1u << k;              /*    앵커가 바뀜 → A[k] 접힘 */
        for (int j = WALK_LO_BIT; j <= WALK_HI_BIT; j++)
            if (j != k && reg_read32(1u << j) != base[j]) bad |= (1u << k) | (1u << j);   /* A[j]-A[k] short */
        reg_write32(1u << k, PAT);
        reg_write32(0u, PAT);                                  /* 마스크가 달라도 앵커 복구 */
    }
    return bad;
}

/* ---------------- Part B-1: 좁은 필드에서도 확실한 index-bit 다중 패스 ---------------- */
/* RW 레지스터에 순번 r = 1..N 을 주고, 패스 p 에서 (r>>p)&1 ? 전부 1 : 전부 0 을 쓴다.
 * 패스 수 = r 을 표현하는 비트 수. 서로 다른 두 레지스터는 어떤 패스에서 반드시 다른 값을 갖는다. */
static int alias_indexbits_test(fail_log_t *lg) {
    unsigned n_rw = 0, passes = 0;
    for (size_t i = 0; i < NREGS; i++) n_rw += regs[i].acc == AT_RW;
    while ((1u << passes) <= n_rw) passes++;                   /* N=22 → 5 패스 (1..22 < 32) */
    int fails = 0;
    for (unsigned p = 0; p < passes; p++) {
        unsigned r = 0;
        for (size_t i = 0; i < NREGS; i++)
            if (regs[i].acc == AT_RW) { r++; reg_write32(regs[i].off, ((r >> p) & 1u) ? 0xFFFFFFFFu : 0u); }
        r = 0;
        for (size_t i = 0; i < NREGS; i++)
            if (regs[i].acc == AT_RW) { r++; fails += check_reg(lg, i, ((r >> p) & 1u) ? 0xFFFFFFFFu : 0u); }
    }
    return fails;
}

/* ---------------- Part B-2: 홀(hole) 검사 ---------------- */
/* RW 에 기준값(addr_hash)을 깔고, 슬롯 안의 모든 홀 주소를 읽고(→ 버스 에러가 나야 함) 쓴 뒤,
 * 레지스터가 하나도 안 바뀌었는지 확인한다. 반환: 응답한 홀 수 + 바뀐 레지스터 수 */
static int hole_test(fail_log_t *lg) {
    int fails = 0;
    for (size_t i = 0; i < NREGS; i++)
        if (regs[i].acc == AT_RW) reg_write32(regs[i].off, addr_hash(regs[i].off));
    for (uint32_t off = 0; off < SLOT_SIZE; off += 4u) {
        if (desc_find(off) >= 0) continue;
        unsigned before = bus_err_count();
        uint32_t v = reg_read32(off);
        if (bus_err_count() == before) { log_fail(lg, off, DECERR_VAL, v, 0xFFFFFFFFu); fails++; }
        reg_write32(off, ~addr_hash(off));
    }
    for (size_t i = 0; i < NREGS; i++) {
        if (regs[i].acc == AT_RW)      fails += check_reg(lg, i, addr_hash(regs[i].off));
        else if (regs[i].acc == AT_RO) fails += check_reg(lg, i, regs[i].reset);
    }
    return fails;
}

/* ---------------- Part C: 진단 — 어느 주소 비트(또는 어느 디코드 엔트리)인가 ---------------- */
typedef enum { DX_PASS, DX_ADDR_BIT, DX_DECODE_ENTRY, DX_MIRROR, DX_UNKNOWN } dx_kind_t;
typedef struct {
    dx_kind_t kind;
    int       bit;                 /* DX_ADDR_BIT / DX_MIRROR: 원인 주소 비트 */
    uint32_t  walk_mask;
    uint32_t  victim, culprit;     /* 함께 움직인 두 주소 */
    int       fails;
    unsigned  bus_errs;
} dx_t;

/* 모든 RW 를 0 으로 깔고, culprit 후보 W 에 하나씩 0xFFFFFFFF 를 써서 victim 이 바뀌는지 본다.
 * 찾으면 1 과 *culprit, 못 찾으면(또는 victim 이 처음부터 0 이 아니면) 0 */
static int find_culprit(uint32_t victim, uint32_t *culprit) {
    for (size_t i = 0; i < NREGS; i++)
        if (regs[i].acc == AT_RW) reg_write32(regs[i].off, 0u);
    uint32_t vm = mask_of(victim);
    if (reg_read32(victim) & vm) return 0;                     /* DECERR 등 — 판단 불가 */
    for (size_t i = 0; i < NREGS; i++) {
        if (regs[i].acc != AT_RW || regs[i].off == victim) continue;
        reg_write32(regs[i].off, 0xFFFFFFFFu);
        int moved = (reg_read32(victim) & vm) != 0;
        reg_write32(regs[i].off, 0u);
        if (moved) { *culprit = regs[i].off; return 1; }
    }
    return 0;
}
/* 홀 주소에 쓴 값이 어느 RW 레지스터에 나타나는지 (주어진 코드) */
static int find_hole_alias(uint32_t hole, uint32_t *reg_off) {
    for (size_t i = 0; i < NREGS; i++)
        if (regs[i].acc == AT_RW) reg_write32(regs[i].off, 0u);
    reg_write32(hole, 0xFFFFFFFFu);
    for (size_t i = 0; i < NREGS; i++)
        if (regs[i].acc == AT_RW && (reg_read32(regs[i].off) & regs[i].mask)) { *reg_off = regs[i].off; return 1; }
    return 0;
}
static int popcount32(uint32_t x) { int n = 0; while (x) { x &= x - 1u; n++; } return n; }
static int ctz32(uint32_t x) { int n = 0; while (x && !(x & 1u)) { x >>= 1; n++; } return n; }

static dx_t diagnose(fail_log_t *lg, fail_log_t *holes) {
    dx_t d = {DX_PASS, -1, 0, 0, 0, 0, 0};
    unsigned e0 = bus_err_count();
    d.walk_mask = walk_addr_test();
    int nf = alias_unique_test(lg) + alias_indexbits_test(lg);
    unsigned e1 = bus_err_count();
    int nh = hole_test(holes);
    d.fails = nf + nh;
    d.bus_errs = e1 - e0;                                      /* 레지스터 테스트 중 버스 에러 */
    if (d.walk_mask == 0 && nf == 0 && nh == 0) return d;

    if (nf > 0) {                                              /* 첫 RW 피해자의 범인 찾기 */
        for (int i = 0; i < lg->n && i < MAX_FAILS; i++) {
            int r = desc_find(lg->f[i].off);
            if (r >= 0 && regs[r].acc == AT_RW && find_culprit(lg->f[i].off, &d.culprit)) {
                d.victim = lg->f[i].off;
                break;
            }
        }
    }
    if (popcount32(d.walk_mask) == 1) {                        /* 주소선 하나: walking 이 지목 */
        d.kind = DX_ADDR_BIT;
        d.bit = ctz32(d.walk_mask);
    } else if (d.walk_mask == 0 && nf > 0) {                   /* 주소선은 멀쩡, 특정 엔트리만 */
        d.kind = d.victim != d.culprit ? DX_DECODE_ENTRY : DX_UNKNOWN;
    } else if (d.walk_mask == 0 && nf == 0 && nh > 0 && holes->n > 0 &&
               find_hole_alias(holes->f[0].off, &d.victim)) { /* 홀만 응답: 상위 비트 미디코드 */
        d.culprit = holes->f[0].off;
        uint32_t x = d.victim ^ d.culprit;
        d.kind = popcount32(x) == 1 ? DX_MIRROR : DX_UNKNOWN;
        d.bit = ctz32(x);
    } else {
        d.kind = DX_UNKNOWN;                                   /* 여러 비트 / short 등 — 사람이 본다 */
    }
    return d;
}

/* ---------------- 리포트 (디버그 엔지니어가 바로 볼 형식) ---------------- */
static const char *name_of(uint32_t off) { int i = desc_find(off); return i < 0 ? "<hole>" : regs[i].name; }

static void print_fail(const fail_t *f) {
    if (f->exp == DECERR_VAL && desc_find(f->off) < 0) {
        printf("  FAIL reg=<hole> off=0x%03X exp=DECERR act=0x%08X (somebody answered)\n", f->off, f->act);
        return;
    }
    uint32_t diff = (f->exp ^ f->act) & f->mask;
    printf("  FAIL reg=%s off=0x%03X exp=0x%08X act=0x%08X diff=0x%08X bits=[", name_of(f->off), f->off,
           f->exp, f->act, diff);
    int first = 1, shown = 0;
    for (int b = 0; b < 32; b++)
        if (diff & (1u << b)) {
            if (shown == 8) { printf(",..."); break; }
            printf(first ? "%d" : ",%d", b); first = 0; shown++;
        }
    printf("]\n");
}
static void print_report(const char *scenario, const fail_log_t *lg, const fail_log_t *holes,
                         const dx_t *d, int max_lines) {
    printf("== %s ==\n", scenario);
    int shown = 0;
    for (int i = 0; i < lg->n && i < MAX_FAILS && shown < max_lines; i++, shown++) print_fail(&lg->f[i]);
    for (int i = 0; i < holes->n && i < MAX_FAILS && shown < max_lines; i++, shown++) print_fail(&holes->f[i]);
    if (lg->n + holes->n > shown) printf("  ... %d more\n", lg->n + holes->n - shown);
    switch (d->kind) {
    case DX_PASS:
        printf("  DIAG PASS: no aliasing, walk_mask=0x000, holes all DECERR\n"); break;
    case DX_ADDR_BIT:
        printf("  DIAG addr-bit A[%d] (0x%03X): decoder does not see it; e.g. %s(0x%03X) <-> %s(0x%03X); "
               "walk_mask=0x%03X fails=%d bus_errs=%u\n", d->bit, 1u << d->bit, name_of(d->victim), d->victim,
               name_of(d->culprit), d->culprit, d->walk_mask, d->fails, d->bus_errs); break;
    case DX_DECODE_ENTRY:
        printf("  DIAG decode-entry: %s(0x%03X) and %s(0x%03X) select one register; address lines OK "
               "(walk_mask=0x000) fails=%d\n", name_of(d->victim), d->victim, name_of(d->culprit), d->culprit,
               d->fails); break;
    case DX_MIRROR:
        printf("  DIAG partial-decode: A[%d] not decoded -> block mirrored at +0x%03X (hole 0x%03X hits %s 0x%03X)\n",
               d->bit, 1u << d->bit, d->culprit, name_of(d->victim), d->victim); break;
    default:
        printf("  DIAG unknown: walk_mask=0x%03X fails=%d — multiple bits/short, inspect manually\n",
               d->walk_mask, d->fails); break;
    }
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* RW 두 개가 마스크 공통 비트에서 같은 패턴을 받는 (구별 불가) 쌍의 수 */
static int count_indistinct(uint32_t (*pat)(uint32_t)) {
    int n = 0;
    for (size_t i = 0; i < NREGS; i++)
        for (size_t j = i + 1; j < NREGS; j++)
            if (regs[i].acc == AT_RW && regs[j].acc == AT_RW &&
                ((pat(regs[i].off) ^ pat(regs[j].off)) & regs[i].mask & regs[j].mask) == 0) n++;
    return n;
}
static uint32_t pat_raw_offset(uint32_t off) { return off; }   /* 비교용: 주소 그대로 쓰기 */
static int log_has(const fail_log_t *lg, uint32_t off) {
    for (int i = 0; i < lg->n && i < MAX_FAILS; i++) if (lg->f[i].off == off) return 1;
    return 0;
}
static int is_pair(const dx_t *d, uint32_t a, uint32_t b) {
    return (d->victim == a && d->culprit == b) || (d->victim == b && d->culprit == a);
}

int main(void) {
    fail_log_t lg, hl;
    fault_t f;

    /* ===== Part A-1: 패턴 ===== */
    CHECK(addr_hash(0x000u) != 0u && addr_hash(0x000u) != addr_hash(0x004u));
    CHECK((addr_hash(0x080u) & 3u) != (addr_hash(0x084u) & 3u));   /* 하위 비트도 주소마다 다르다 */
    CHECK(count_indistinct(addr_hash) < count_indistinct(pat_raw_offset));   /* 원시 주소보다 덜 겹침 */

    /* ===== Part A-2: 고유 패턴 테스트 ===== */
    mock_reset(&NO_FAULT); lg.n = 0;
    CHECK(alias_unique_test(&lg) == 0 && lg.n == 0);                 /* 정상 디코더: 통과 */
    f = NO_FAULT; f.ignore_bit = 6;                                  /* A[6] 무시: 0x040 → 0x000 ... */
    mock_reset(&f); lg.n = 0;
    CHECK(alias_unique_test(&lg) > 0);
    CHECK(log_has(&lg, 0x000u) && log_has(&lg, 0x080u));             /* CTRL←PRIO, CH0_SRC←CH1_SRC */
    f = NO_FAULT; f.ignore_bit = 3;                                  /* ID(0x018) 가 CFG(0x010) 로 */
    mock_reset(&f); lg.n = 0;
    CHECK(alias_unique_test(&lg) > 0 && log_has(&lg, 0x018u));       /* RO 리셋값 확인이 잡는다 */

    /* ===== Part A-3: walking-address ===== */
    mock_reset(&NO_FAULT);
    CHECK(walk_addr_test() == 0u);
    for (int k = WALK_LO_BIT; k <= WALK_HI_BIT; k++) {
        f = NO_FAULT; f.ignore_bit = k;
        mock_reset(&f);
        CHECK(walk_addr_test() == (1u << k));                        /* 정확히 그 비트만 지목 */
    }

    /* ===== Part B-1: 좁은 필드 — 해시가 놓치는 aliasing ===== */
    f = NO_FAULT; f.remap_from = 0x0CC; f.remap_to = 0x08C;          /* CH1_CCTRL 엔트리가 CH0_CCTRL 선택 */
    mock_reset(&f); lg.n = 0;
    CHECK(alias_unique_test(&lg) == 0);                              /* 2비트 필드, 해시 하위 2비트가 같아 놓침 */
    mock_reset(&f); lg.n = 0;
    CHECK(alias_indexbits_test(&lg) > 0 && (log_has(&lg, 0x08Cu) || log_has(&lg, 0x0CCu)));
    mock_reset(&NO_FAULT); lg.n = 0;
    CHECK(alias_indexbits_test(&lg) == 0);

    /* ===== Part B-2: 홀 검사 / 위험 레지스터 보호 ===== */
    mock_reset(&NO_FAULT); hl.n = 0;
    CHECK(hole_test(&hl) == 0 && bus_err_count() == 2u * (SLOT_SIZE / 4u - NREGS));   /* 홀마다 읽기·쓰기 에러 */
    f = NO_FAULT; f.ignore_bit = 9;                                  /* A[9] 미디코드 → +0x200 미러 */
    mock_reset(&f); lg.n = 0;
    CHECK(alias_unique_test(&lg) == 0 && walk_addr_test() == 0u);    /* 레지스터끼리는 멀쩡해 보인다 */
    mock_reset(&f); hl.n = 0;
    CHECK(hole_test(&hl) > 0 && log_has(&hl, 0x200u));               /* 홀 0x200 이 CTRL 로 응답 */
    mock_reset(&NO_FAULT); lg.n = 0; hl.n = 0;
    (void)walk_addr_test(); (void)alias_unique_test(&lg); (void)alias_indexbits_test(&lg); (void)hole_test(&hl);
    CHECK(g_rc_reads == 0 && g_w1c_writes == 0 && g_ro_writes == 0); /* RC 안 읽고 W1C/RO 안 씀 */
    CHECK(g_val[desc_find(0x00Cu)] == 0x5u && g_val[desc_find(0x014u)] == 7u);   /* pending·카운터 보존 */

    /* ===== Part C: VDMA 결함 주입 → 진단 ===== */
    mock_reset(&NO_FAULT); lg.n = 0; hl.n = 0;
    dx_t d = diagnose(&lg, &hl);
    print_report("VDMA good silicon", &lg, &hl, &d, 4);
    CHECK(d.kind == DX_PASS && d.fails == 0);

    for (int k = WALK_LO_BIT; k <= WALK_HI_BIT; k++) {
        char title[64];
        f = NO_FAULT; f.ignore_bit = k;
        mock_reset(&f); lg.n = 0; hl.n = 0;
        d = diagnose(&lg, &hl);
        snprintf(title, sizeof title, "VDMA fault: decoder ignores A[%d]", k);
        print_report(title, &lg, &hl, &d, k == 6 ? 5 : 0);
        CHECK(d.kind == DX_ADDR_BIT && d.bit == k && (d.victim ^ d.culprit) == (1u << k));
    }

    f = NO_FAULT; f.remap_from = 0x0CC; f.remap_to = 0x08C;
    mock_reset(&f); lg.n = 0; hl.n = 0;
    d = diagnose(&lg, &hl);
    print_report("VDMA fault: decode entry CH1_CCTRL -> CH0_CCTRL", &lg, &hl, &d, 3);
    CHECK(d.kind == DX_DECODE_ENTRY && is_pair(&d, 0x08Cu, 0x0CCu));

    f = NO_FAULT; f.ignore_bit = 9;
    mock_reset(&f); lg.n = 0; hl.n = 0;
    d = diagnose(&lg, &hl);
    print_report("VDMA fault: A[9] not decoded (mirror)", &lg, &hl, &d, 3);
    CHECK(d.kind == DX_MIRROR && d.bit == 9 && d.victim == 0x000u && d.culprit == 0x200u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
