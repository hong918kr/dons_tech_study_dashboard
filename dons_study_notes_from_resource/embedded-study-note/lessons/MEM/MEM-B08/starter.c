/* MEM-B08 메모리 테스트 패턴: walking 1/0, address-in-address
 *
 * Barr 스타일 bring-up 메모리 테스트 3종을 결함 주입 RAM 모델 위에서 돌린다.
 *   Part A  결함 주입 RAM 모델 (32-bit x 4096 word): 데이터 라인 / 주소 라인 / 셀 결함
 *   Part B  mt_data_bus (walking 1) · mt_address_bus (2^k 오프셋) · mt_device (증가 패턴 + 반전)
 *   Part C  bring-up 순서 + 원인 판정(데이터 선 / 주소 선 / 셀) + 탐지 매트릭스 출력
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/MEM-B08_so && /tmp/MEM-B08_so
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 결함 주입 RAM 모델 (주어짐: 구조체·읽기/쓰기 틀) ---------------- */
#define RAM_ABITS 12u
#define RAM_WORDS (1u << RAM_ABITS)          /* 4096 word x 32 bit = 16 KB, 주소선 A0..A11 (word 단위) */

typedef enum {
    F_NONE,
    F_DATA_SA0, F_DATA_SA1,                  /* 데이터 선 a 가 0/1 에 붙음                 */
    F_DATA_AND, F_DATA_OR,                   /* 데이터 선 a,b 단락 (0 이 이김 / 1 이 이김)  */
    F_ADDR_SA0, F_ADDR_SA1,                  /* 주소 선 a 가 low/high 에 붙음              */
    F_ADDR_AND, F_ADDR_OR,                   /* 주소 선 a,b 단락                           */
    F_ADDR_OPEN,                             /* 주소 선 a 저항성 open: RAM 은 직전 접근 주소의 비트 a 를 본다 */
    F_CELL_SA0, F_CELL_SA1,                  /* word `cell` 의 비트 a 하나가 0/1 로 죽음   */
    F_KIND_COUNT
} fault_kind;

static const char *const KIND_NAME[F_KIND_COUNT] = {
    "none", "data SA0", "data SA1", "data short AND", "data short OR",
    "addr SA0 (low)", "addr SA1 (high)", "addr short AND", "addr short OR",
    "addr open (hold)", "cell SA0", "cell SA1",
};

typedef struct { fault_kind kind; unsigned a, b; uint32_t cell; } fault_t;

typedef struct {
    uint32_t mem[RAM_WORDS];
    uint32_t nwords;                         /* 2의 거듭제곱, <= RAM_WORDS */
    fault_t  f;
    uint32_t last_addr;                      /* 직전 접근의 논리 주소 (F_ADDR_OPEN 용) */
    uint64_t nops;                           /* 읽기 + 쓰기 횟수 */
} ram_t;

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static uint32_t g_garbage[RAM_WORDS];        /* 전원 투입 직후의 "쓰레기 값" (고정 시드) */

static void ram_init(ram_t *r, uint32_t nwords, fault_t f) {
    r->nwords = nwords; r->f = f; r->last_addr = 0; r->nops = 0;
    memcpy(r->mem, g_garbage, sizeof r->mem);
}

/* ---------------- Part A: 결함 → 핀에서 보이는 값 ---------------- */
/* 논리 주소(CPU 가 낸 word offset) → RAM 이 실제로 보는 물리 주소. 주소 선 결함만 반영. */
static uint32_t ram_phys_addr(const ram_t *r, uint32_t la) {
    uint32_t p = la & (r->nwords - 1u);
    /* TODO: r->f.kind 에 따라 주소 선 a(,b) 결함을 p 에 반영
     *   ADDR_SA0/SA1: 비트 a 를 0/1 로,  ADDR_AND/OR: 비트 a,b 를 (a&b)/(a|b) 로,
     *   ADDR_OPEN: 비트 a 를 r->last_addr 의 비트 a 로 */
    return p;
}

/* 데이터 버스를 지나는 값 v 가 데이터 선 결함으로 어떻게 바뀌나 (쓰기·읽기 양방향 동일). */
static uint32_t data_xform(const fault_t *f, uint32_t v) {
    (void)f;
    /* TODO: DATA_SA0/SA1 → 비트 a 를 0/1,  DATA_AND/OR → 비트 a,b 를 둘의 AND/OR 로 */
    return v;
}

/* 주어짐: 버스 접근 = 주소 변환 → 셀 접근 → 데이터 변환. 셀 결함은 읽을 때 적용. */
static void ram_write(ram_t *r, uint32_t la, uint32_t v) {
    uint32_t p = ram_phys_addr(r, la);
    r->mem[p] = data_xform(&r->f, v);
    r->last_addr = la & (r->nwords - 1u);
    r->nops++;
}
static uint32_t ram_read(ram_t *r, uint32_t la) {
    uint32_t p = ram_phys_addr(r, la), v = r->mem[p];
    if (p == r->f.cell && r->f.kind == F_CELL_SA0) v &= ~(1u << r->f.a);
    if (p == r->f.cell && r->f.kind == F_CELL_SA1) v |= 1u << r->f.a;
    r->last_addr = la & (r->nwords - 1u);
    r->nops++;
    return data_xform(&r->f, v);
}

/* ---------------- 실패 리포트 (주어짐) ---------------- */
typedef enum { PH_PASS, PH_DATA, PH_ADDR_HI, PH_ADDR_LO, PH_DEV_FILL, PH_DEV_INV } phase_t;
static const char *const PH_NAME[] = { "pass", "data-bus", "addr-stuck-high", "addr-stuck-low/short",
                                       "device-fill", "device-inverse" };
typedef struct {
    phase_t  phase;
    uint32_t addr;                           /* 틀린 값을 읽은 word offset                     */
    uint32_t trig;                           /* 그 실패를 드러낸 쓰기 오프셋 (Barr 반환값)      */
    uint32_t exp, act;
} mt_report;

static void mt_print(const mt_report *m) {
    uint32_t d = m->exp ^ m->act;
    printf("FAIL addr=0x%03X exp=0x%08X act=0x%08X diff=0x%08X bits=[", (unsigned)m->addr,
           (unsigned)m->exp, (unsigned)m->act, (unsigned)d);
    for (int i = 31, first = 1; i >= 0; i--) {              /* 연속 구간은 31-0 처럼 줄여 쓴다 */
        if (!(d >> i & 1u)) continue;
        int j = i;
        while (j > 0 && (d >> (j - 1) & 1u)) j--;
        printf(first ? "" : ","); first = 0;
        if (j == i) printf("%d", i); else printf("%d-%d", i, j);
        i = j;
    }
    printf("] phase=%s trig=0x%03X\n", PH_NAME[m->phase], (unsigned)m->trig);
}

static void rep_fail(mt_report *rep, phase_t ph, uint32_t addr, uint32_t trig, uint32_t exp, uint32_t act) {
    rep->phase = ph; rep->addr = addr; rep->trig = trig; rep->exp = exp; rep->act = act;
}

/* ---------------- Part B: Barr 스타일 테스트 3종 ---------------- */
/* walking 1 을 한 주소에 쓰고 읽는다. 0 = PASS, 1 = FAIL(rep 채움). */
static int mt_data_bus(ram_t *r, uint32_t addr, mt_report *rep) {
    memset(rep, 0, sizeof *rep);
    (void)r; (void)addr; (void)rep_fail;
    /* TODO: pattern = 1,2,4,..,0x80000000 를 addr 에 쓰고 바로 읽어 비교. 틀리면 PH_DATA 리포트, return 1 */
    return 0;
}

/* 2^k 오프셋에 pattern, 0 번지/2^k 하나에 antipattern → alias 를 양방향으로 찾는다. */
static int mt_address_bus(ram_t *r, mt_report *rep) {
    memset(rep, 0, sizeof *rep);
    (void)r;
    /* TODO: (0) 2^k 오프셋 전부에 0xAAAAAAAA
     *       (1) 0 번지에 0x55555555 → 2^k 가 하나라도 바뀌면 PH_ADDR_HI (addr=trig=off)
     *       (2) 0 번지 복구 후, 2^t 마다 0x55555555 를 쓰고 0 번지와 다른 2^k 를 확인 → PH_ADDR_LO
     *           (0 번지가 틀리면 addr=0, 아니면 addr=off; trig=2^t), 2^t 복구 */
    return 0;
}

/* 증가 패턴(1,2,3,..)으로 채우고, 확인하며 반전, 반전값 확인. */
static int mt_device(ram_t *r, mt_report *rep) {
    memset(rep, 0, sizeof *rep);
    (void)r;
    /* TODO: pass1 증가 패턴(1,2,3,..) 채우기 → pass2 확인(PH_DEV_FILL) 하며 ~pattern 으로 반전
     *       → pass3 ~pattern 확인(PH_DEV_INV) */
    return 0;
}

/* ---------------- Part C: bring-up 순서와 원인 판정 ---------------- */
typedef enum { V_OK, V_DATA_LINE, V_ADDR_LINE, V_ADDR_OPEN, V_CELL } verdict_t;
static const char *const V_NAME[] = { "OK", "DATA_LINE", "ADDR_LINE", "ADDR_OPEN", "CELL" };
typedef struct { int stage; verdict_t verdict; uint32_t lines; mt_report rep; } bringup_t;

/* walking 1 을 끝까지 돌며 (쓴 값 ^ 읽은 값) 을 모두 OR → 의심 데이터 선 마스크. */
static uint32_t data_line_suspects(ram_t *r, uint32_t addr) {
    (void)r; (void)addr;
    /* TODO: walking 1 을 첫 실패에서 멈추지 말고 끝까지 돌며 (읽은 값 ^ 쓴 값) 을 OR */
    return 0;
}

/* alias 탐침: 0 vs 2^k, 2^j vs 2^k. 다른 주소에 쓴 값(~P)이 "그대로" 읽히면 alias. */
static uint32_t addr_line_suspects(ram_t *r) {
    (void)r;
    /* TODO: 0 vs 2^k, 2^j vs 2^k 쌍마다 P / ~P 를 쓰고, 앞 주소에서 ~P 가 "그대로" 읽히면 alias */
    return 0;
}

/* 이력(history) 탐침: 같은 주소 0 을 읽는데 "직전 접근"만 0 / 2^k 로 바꾼다.
 * 값이 달라지면 주소 선 k 가 이번 주소가 아니라 지난 주소를 따라간다 = 저항성 open. */
static uint32_t addr_line_history(ram_t *r) {
    (void)r;
    /* TODO: 선 k 마다 0 에 P, 2^k 에 ~P (각각 직전 접근을 자기 자신으로 맞추고) 쓴 뒤,
     *       "직전=0" 에서 읽은 0 번지와 "직전=2^k" 에서 읽은 0 번지가 다르면 선 k 의심 */
    return 0;
}

/* data bus → address bus → device. 처음 실패한 단계에서 멈추고 원인을 판정한다. */
static int bringup_run(ram_t *r, bringup_t *out) {
    memset(out, 0, sizeof *out);
    (void)r; (void)data_line_suspects; (void)addr_line_suspects; (void)addr_line_history;
    /* TODO: data bus(@0) → address bus → device. 처음 실패한 단계 번호(1/2/3)를 stage 와 반환값에.
     *   1: lines = suspects(@0) & suspects(@nwords-1), 0 이 아니면 DATA_LINE 아니면 CELL
     *   2: history 탐침이 0 이 아니면 ADDR_OPEN, 아니면 alias 탐침 → ADDR_LINE / (0 이면) CELL
     *   3: CELL */
    return 0;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static ram_t R;                               /* 16 KB — 스택 말고 static */

/* 결함 종류 k 의 인스턴스 i 를 만든다. 반환 = 인스턴스 개수 (i 무시하고 개수만 물을 때도 사용). */
static unsigned fault_instance(fault_kind k, unsigned i, fault_t *f) {
    unsigned n = 0;
    memset(f, 0, sizeof *f);
    f->kind = k; f->cell = UINT32_MAX;
    switch (k) {
    case F_NONE: n = 1; break;
    case F_DATA_SA0: case F_DATA_SA1: n = 32; f->a = i % 32; break;
    case F_ADDR_SA0: case F_ADDR_SA1: case F_ADDR_OPEN: n = RAM_ABITS; f->a = i % RAM_ABITS; break;
    case F_DATA_AND: case F_DATA_OR: case F_ADDR_AND: case F_ADDR_OR: {
        unsigned w = (k == F_DATA_AND || k == F_DATA_OR) ? 32 : RAM_ABITS, c = 0;
        n = w * (w - 1) / 2;
        for (unsigned a = 0; a < w; a++)
            for (unsigned b = a + 1; b < w; b++, c++)
                if (c == i) { f->a = a; f->b = b; }
        break;
    }
    case F_CELL_SA0: case F_CELL_SA1:                    /* 모든 word × 비트 (word*7+3)%32 */
        n = RAM_WORDS; f->cell = i % RAM_WORDS; f->a = (f->cell * 7u + 3u) % 32u; break;
    default: break;
    }
    return n;
}

/* 기대 매트릭스: detected[kind][test]  (test 0 = data bus @0, 1 = address bus, 2 = device) */
static const unsigned EXPECT[F_KIND_COUNT][3] = {
    [F_NONE]      = { 0, 0, 0 },
    [F_DATA_SA0]  = { 32, 16, 32 },  [F_DATA_SA1]  = { 32, 16, 32 },
    [F_DATA_AND]  = { 496, 256, 325 }, [F_DATA_OR] = { 496, 256, 325 },
    [F_ADDR_SA0]  = { 0, 12, 12 },   [F_ADDR_SA1]  = { 0, 12, 12 },
    [F_ADDR_AND]  = { 0, 66, 66 },   [F_ADDR_OR]   = { 0, 66, 66 },
    [F_ADDR_OPEN] = { 0, 12, 12 },
    [F_CELL_SA0]  = { 1, 12, 4096 }, [F_CELL_SA1]  = { 1, 1, 4096 },
};

int main(void) {
    uint32_t s = 0x8B08u;
    for (unsigned i = 0; i < RAM_WORDS; i++) g_garbage[i] = xs32(&s);
    fault_t f;
    mt_report rep;

    /* ---- Part A: 모델 동작 ---- */
    fault_instance(F_NONE, 0, &f); ram_init(&R, RAM_WORDS, f);
    ram_write(&R, 0x123, 0xCAFEF00Du);
    CHECK(ram_read(&R, 0x123) == 0xCAFEF00Du);
    CHECK(ram_phys_addr(&R, 0x5A5) == 0x5A5u);

    f = (fault_t){ F_DATA_SA0, 4, 0, UINT32_MAX };   CHECK(data_xform(&f, 0xFFu) == 0xEFu);
    f = (fault_t){ F_DATA_SA1, 31, 0, UINT32_MAX };  CHECK(data_xform(&f, 0x0u) == 0x80000000u);
    f = (fault_t){ F_DATA_AND, 0, 1, UINT32_MAX };
    CHECK(data_xform(&f, 0x1u) == 0x0u && data_xform(&f, 0x2u) == 0x0u && data_xform(&f, 0x3u) == 0x3u);
    f = (fault_t){ F_DATA_OR, 3, 7, UINT32_MAX };
    CHECK(data_xform(&f, 0x08u) == 0x88u && data_xform(&f, 0x80u) == 0x88u && data_xform(&f, 0x11u) == 0x11u);

    f = (fault_t){ F_ADDR_SA1, 3, 0, UINT32_MAX }; ram_init(&R, RAM_WORDS, f);
    CHECK(ram_phys_addr(&R, 0x000) == 0x008u);
    ram_write(&R, 0x000, 0x11111111u);                       /* 0 에 쓴 값이 8 에서 읽힌다 = alias */
    CHECK(ram_read(&R, 0x008) == 0x11111111u);
    f = (fault_t){ F_ADDR_SA0, 11, 0, UINT32_MAX }; ram_init(&R, RAM_WORDS, f);
    CHECK(ram_phys_addr(&R, 0x800) == 0x000u && ram_phys_addr(&R, 0xFFF) == 0x7FFu);
    f = (fault_t){ F_ADDR_AND, 2, 5, UINT32_MAX }; ram_init(&R, RAM_WORDS, f);
    CHECK(ram_phys_addr(&R, 0x004) == 0x000u && ram_phys_addr(&R, 0x020) == 0x000u && ram_phys_addr(&R, 0x024) == 0x024u);
    f = (fault_t){ F_ADDR_OR, 2, 5, UINT32_MAX }; ram_init(&R, RAM_WORDS, f);
    CHECK(ram_phys_addr(&R, 0x004) == 0x024u && ram_phys_addr(&R, 0x020) == 0x024u && ram_phys_addr(&R, 0x003) == 0x003u);
    f = (fault_t){ F_ADDR_OPEN, 4, 0, UINT32_MAX }; ram_init(&R, RAM_WORDS, f);
    (void)ram_read(&R, 0x010);                               /* 직전 주소의 A4 = 0 이었으므로 0x000 으로 감 */
    CHECK(ram_phys_addr(&R, 0x001) == 0x011u);               /* 이번엔 직전(0x010)의 A4=1 이 남아 있음 */
    f = (fault_t){ F_CELL_SA1, 9, 0, 0x2A0 }; ram_init(&R, RAM_WORDS, f);
    ram_write(&R, 0x2A0, 0); ram_write(&R, 0x2A1, 0);
    CHECK(ram_read(&R, 0x2A0) == 0x200u && ram_read(&R, 0x2A1) == 0u);

    /* ---- Part B: 테스트 3종, 손으로 추적 가능한 사례 ---- */
    fault_instance(F_NONE, 0, &f); ram_init(&R, RAM_WORDS, f);
    CHECK(mt_data_bus(&R, 0, &rep) == 0 && rep.phase == PH_PASS);
    CHECK(R.nops == 64);                                     /* 32 쓰기 + 32 읽기 */
    R.nops = 0; CHECK(mt_address_bus(&R, &rep) == 0);
    CHECK(R.nops == 12 + 1 + 12 + 1 + 12 * (1 + 1 + 12 + 1)); /* L=12 → 206 접근 */
    R.nops = 0; CHECK(mt_device(&R, &rep) == 0 && R.nops == 4u * RAM_WORDS);

    f = (fault_t){ F_DATA_SA0, 5, 0, UINT32_MAX }; ram_init(&R, RAM_WORDS, f);
    CHECK(mt_data_bus(&R, 0, &rep) == 1 && rep.exp == 0x20u && rep.act == 0x0u);
    f = (fault_t){ F_DATA_SA1, 5, 0, UINT32_MAX }; ram_init(&R, RAM_WORDS, f);
    CHECK(mt_data_bus(&R, 0, &rep) == 1 && rep.exp == 0x1u && rep.act == 0x21u);   /* 첫 패턴에서 잡힘 */
    printf("data SA1 D5      : "); mt_print(&rep);

    f = (fault_t){ F_ADDR_SA1, 3, 0, UINT32_MAX }; ram_init(&R, RAM_WORDS, f);
    CHECK(mt_data_bus(&R, 0, &rep) == 0);                    /* 같은 주소 쓰고 읽기 → 주소 결함은 안 보임 */
    CHECK(mt_address_bus(&R, &rep) == 1 && rep.phase == PH_ADDR_HI && rep.addr == 0x008u && rep.act == 0x55555555u);
    printf("addr SA1 A3      : "); mt_print(&rep);
    f = (fault_t){ F_ADDR_SA0, 3, 0, UINT32_MAX }; ram_init(&R, RAM_WORDS, f);
    CHECK(mt_address_bus(&R, &rep) == 1 && rep.phase == PH_ADDR_HI && rep.addr == 0x008u);  /* SA0 도 1단계에서 */
    f = (fault_t){ F_ADDR_OR, 2, 9, UINT32_MAX }; ram_init(&R, RAM_WORDS, f);
    CHECK(mt_address_bus(&R, &rep) == 1 && rep.phase == PH_ADDR_LO && rep.trig == 0x004u && rep.addr == 0x200u);
    printf("addr OR A2-A9    : "); mt_print(&rep);
    f = (fault_t){ F_CELL_SA0, 17, 0, 0x6B5 }; ram_init(&R, RAM_WORDS, f);
    CHECK(mt_data_bus(&R, 0, &rep) == 0 && mt_address_bus(&R, &rep) == 0);
    CHECK(mt_device(&R, &rep) == 1 && rep.addr == 0x6B5u && rep.phase == PH_DEV_INV && rep.exp == ~0x6B6u
          && (rep.exp ^ rep.act) == 1u << 17);                   /* 0x6B6 의 b17 = 0 → 반전 패스에서만 */
    printf("cell SA0 @6B5 b17: "); mt_print(&rep);

    /* ---- Part C-1: 탐지 매트릭스 (모든 인스턴스 × 테스트 3종, 테스트마다 새 RAM) ---- */
    unsigned det[F_KIND_COUNT][3] = { { 0 } }, cnt[F_KIND_COUNT] = { 0 };
    unsigned diag_ok[F_KIND_COUNT] = { 0 }, stage_hist[F_KIND_COUNT][4] = { { 0 } };
    for (unsigned k = 0; k < F_KIND_COUNT; k++) {
        cnt[k] = fault_instance((fault_kind)k, 0, &f);
        for (unsigned i = 0; i < cnt[k]; i++) {
            fault_instance((fault_kind)k, i, &f);
            ram_init(&R, RAM_WORDS, f); det[k][0] += (unsigned)mt_data_bus(&R, 0, &rep);
            ram_init(&R, RAM_WORDS, f); det[k][1] += (unsigned)mt_address_bus(&R, &rep);
            ram_init(&R, RAM_WORDS, f); det[k][2] += (unsigned)mt_device(&R, &rep);
            bringup_t b;
            ram_init(&R, RAM_WORDS, f); bringup_run(&R, &b);
            stage_hist[k][b.stage]++;
            int ok = 0;
            switch ((fault_kind)k) {
            case F_NONE:     ok = b.stage == 0 && b.verdict == V_OK; break;
            case F_DATA_SA0: case F_DATA_SA1:
                ok = b.verdict == V_DATA_LINE && b.lines == 1u << f.a; break;
            case F_DATA_AND: case F_DATA_OR:
                ok = b.verdict == V_DATA_LINE && b.lines == ((1u << f.a) | (1u << f.b)); break;
            case F_ADDR_SA0: case F_ADDR_SA1:
                ok = b.verdict == V_ADDR_LINE && b.lines == 1u << f.a; break;
            case F_ADDR_AND: case F_ADDR_OR:
                ok = b.verdict == V_ADDR_LINE && b.lines == ((1u << f.a) | (1u << f.b)); break;
            case F_ADDR_OPEN:
                ok = b.verdict == V_ADDR_OPEN && b.lines == 1u << f.a; break;
            case F_CELL_SA0: case F_CELL_SA1:
                ok = b.verdict == V_CELL && b.rep.addr == f.cell; break;
            default: break;
            }
            diag_ok[k] += (unsigned)ok;
        }
    }
    printf("\nDetection matrix: 32-bit x %u words, every instance of each fault (detected/instances)\n", RAM_WORDS);
    printf("%-18s %6s %12s %12s %12s   %-22s %s\n", "fault", "inst", "data-bus@0", "address-bus", "device",
           "bring-up stage 1/2/3", "diagnosis ok");
    for (unsigned k = 0; k < F_KIND_COUNT; k++) {
        printf("%-18s %6u", KIND_NAME[k], cnt[k]);
        for (unsigned t = 0; t < 3; t++) {
            char buf[24];
            snprintf(buf, sizeof buf, "%u/%u", det[k][t], cnt[k]);
            printf(" %12s", buf);
        }
        printf("   %5u/%5u/%5u       %u/%u\n", stage_hist[k][1], stage_hist[k][2], stage_hist[k][3],
               diag_ok[k], cnt[k]);
    }
    for (unsigned k = 0; k < F_KIND_COUNT; k++) {
        CHECK(det[k][0] == EXPECT[k][0] && det[k][1] == EXPECT[k][1] && det[k][2] == EXPECT[k][2]);
        CHECK(diag_ok[k] == cnt[k]);
    }

    /* ---- Part C-2: 판정 예시 (리포트 한 줄씩) ---- */
    bringup_t b;
    f = (fault_t){ F_DATA_OR, 3, 12, UINT32_MAX }; ram_init(&R, RAM_WORDS, f);
    CHECK(bringup_run(&R, &b) == 1 && b.verdict == V_DATA_LINE && b.lines == 0x1008u);
    printf("\nbring-up D3-D12 OR : stage=%d verdict=%s lines=0x%08X  ", b.stage, V_NAME[b.verdict], (unsigned)b.lines);
    mt_print(&b.rep);
    f = (fault_t){ F_ADDR_AND, 1, 10, UINT32_MAX }; ram_init(&R, RAM_WORDS, f);
    CHECK(bringup_run(&R, &b) == 2 && b.verdict == V_ADDR_LINE && b.lines == 0x402u);
    printf("bring-up A1-A10 AND: stage=%d verdict=%s lines=0x%03X       ", b.stage, V_NAME[b.verdict], (unsigned)b.lines);
    mt_print(&b.rep);
    f = (fault_t){ F_ADDR_OPEN, 6, 0, UINT32_MAX }; ram_init(&R, RAM_WORDS, f);
    CHECK(bringup_run(&R, &b) == 2 && b.verdict == V_ADDR_OPEN && b.lines == 0x040u);
    printf("bring-up A6 open   : stage=%d verdict=%s lines=0x%03X       ", b.stage, V_NAME[b.verdict], (unsigned)b.lines);
    mt_print(&b.rep);
    f = (fault_t){ F_CELL_SA1, 0, 0, 0x000 }; ram_init(&R, RAM_WORDS, f);
    CHECK(bringup_run(&R, &b) == 1 && b.verdict == V_CELL && b.rep.addr == 0);   /* 데이터 버스 테스트가 걸렸지만 셀 */
    printf("bring-up cell@0 b0 : stage=%d verdict=%s lines=0x%08X  ", b.stage, V_NAME[b.verdict], (unsigned)b.lines);
    mt_print(&b.rep);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
