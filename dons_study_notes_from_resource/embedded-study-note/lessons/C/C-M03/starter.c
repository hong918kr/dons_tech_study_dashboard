/* C-M03 비트필드 vs 마스크/시프트: 이식성 분석
 *
 * 같은 32비트 레지스터 ADC_CFG 를 두 가지로 정의한다.
 *   (1) C 비트필드 구조체 adc_cfg_bits_t  — 비트 위치를 "컴파일러(ABI)"가 정한다 (C11 6.7.2.1p11 구현 정의)
 *   (2) CMSIS 스타일 _Pos / _Msk 마스크   — 비트 위치를 "소스 코드"가 정한다 (정수 연산만, 어디서나 같음)
 * Part A: 둘이 이 호스트(arm64 macOS)에서 같은 비트를 만드는지 표·퍼즈 테스트로 확인
 * Part B: 구현 정의 지점 — 부호 있는 필드, 필드 위치 탐침(probe), 저장 단위 경계(straddle) 규칙
 * Part C: 레지스터 드라이버는 마스크로 (읽기 1번 + 쓰기 1번). 비트필드 필드별 대입은 필드마다 RMW.
 * 크로스 타깃(Cortex-M LE/BE, MSVC) 레이아웃과 실제 asm 은 demo_layout.c / demo_rmw.c 참고.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 레지스터 맵: ADC_CFG (ADC 블록 offset 0x04) ----------------
 * bit  0      EN      1 = ADC on
 * bits 2:1    MODE    0 single, 1 continuous, 2 scan, 3 = reserved(쓰면 안 됨)
 * bits 4:3    RES     0=12b 1=10b 2=8b 3=6b
 * bits 12:5   SMPT    샘플링 시간(클럭 수) 8비트 — byte 0 / byte 1 경계를 걸친다
 * bits 19:13  OFFSET  부호 있는 7비트 보정값 (-64..63, 2의 보수)
 * bits 23:20  reserved (읽은 값 그대로 다시 쓴다)
 * bits 28:24  CH      채널 0..18
 * bit  29     START   1을 쓰면 변환 시작, 읽으면 항상 0 (self-clearing)
 * bits 31:30  reserved
 */
#define ADC_CFG_EN_Pos      0u
#define ADC_CFG_EN_Msk      (0x1u  << ADC_CFG_EN_Pos)
#define ADC_CFG_MODE_Pos    1u
#define ADC_CFG_MODE_Msk    (0x3u  << ADC_CFG_MODE_Pos)
#define ADC_CFG_RES_Pos     3u
#define ADC_CFG_RES_Msk     (0x3u  << ADC_CFG_RES_Pos)
#define ADC_CFG_SMPT_Pos    5u
#define ADC_CFG_SMPT_Msk    (0xFFu << ADC_CFG_SMPT_Pos)
#define ADC_CFG_OFFSET_Pos  13u
#define ADC_CFG_OFFSET_Msk  (0x7Fu << ADC_CFG_OFFSET_Pos)
#define ADC_CFG_CH_Pos      24u
#define ADC_CFG_CH_Msk      (0x1Fu << ADC_CFG_CH_Pos)
#define ADC_CFG_START_Pos   29u
#define ADC_CFG_START_Msk   (0x1u  << ADC_CFG_START_Pos)
#define ADC_CFG_RSVD_Msk    0xC0F00000u                 /* bits 31:30, 23:20 */

/* 마스크는 정수 상수식 → 레지스터 맵 자체를 컴파일타임에 검증할 수 있다 (비트필드는 불가, 풀이 참고) */
#define ADC_CFG_ALL_FIELDS  (ADC_CFG_EN_Msk | ADC_CFG_MODE_Msk | ADC_CFG_RES_Msk | ADC_CFG_SMPT_Msk | \
                             ADC_CFG_OFFSET_Msk | ADC_CFG_CH_Msk | ADC_CFG_START_Msk)
_Static_assert((ADC_CFG_ALL_FIELDS & ADC_CFG_RSVD_Msk) == 0u, "field overlaps reserved bits");
_Static_assert((ADC_CFG_ALL_FIELDS | ADC_CFG_RSVD_Msk) == 0xFFFFFFFFu, "gap in register map");
_Static_assert((ADC_CFG_SMPT_Msk & ADC_CFG_OFFSET_Msk) == 0u && (ADC_CFG_OFFSET_Msk & ADC_CFG_CH_Msk) == 0u,
               "fields overlap");

/* ---------------- 같은 레지스터의 비트필드 정의 ----------------
 * 선언 순서 = "LSB 부터 채운다" 고 가정한 순서. 이 가정이 맞는지는 ABI 가 정한다. */
typedef struct {
    uint32_t   en     : 1;
    uint32_t   mode   : 2;
    uint32_t   res    : 2;
    uint32_t   smpt   : 8;
    signed int offset : 7;   /* 그냥 int 라고 쓰면 부호 여부부터 구현 정의 (C11 6.7.2p5) */
    uint32_t          : 4;   /* reserved — 이름 없는 비트필드 */
    uint32_t   ch     : 5;
    uint32_t   start  : 1;
    uint32_t          : 2;   /* reserved */
} adc_cfg_bits_t;
_Static_assert(sizeof(adc_cfg_bits_t) == 4, "bitfield struct is not 32 bits on this ABI");

/* 필드 값 묶음 (레이아웃과 무관한 보통 구조체) */
typedef struct {
    unsigned en, mode, res, smpt;
    int      offset;
    unsigned ch, start;
} adc_cfg_t;

/* ---------------- Part A: 두 정의로 pack / unpack ---------------- */
/* 마스크/시프트로 레지스터 값 만들기. 필드보다 큰 값은 잘린다(넘친 비트 버림). */
static uint32_t cfg_pack_mask(const adc_cfg_t *c) {
    (void)c;
    return 0u; /* TODO: 필드마다 (값 << _Pos) & _Msk 를 OR. OFFSET 은 음수일 수 있다 */
}

/* 비트필드 구조체로 레지스터 값 만들기 (전제: 모든 값이 필드 범위 안).
 * 구조체를 0으로 채운 뒤 필드에 대입하고, 4바이트를 그대로 uint32_t 로 복사한다. */
static uint32_t cfg_pack_bf(const adc_cfg_t *c) {
    (void)c;
    return 0u; /* TODO: adc_cfg_bits_t 를 0으로 채우고 필드 대입 → 4바이트를 uint32_t 로 복사 */
}

/* 비트필드 구조체로 레지스터 값 풀기. signed int 필드는 읽을 때 부호 확장이 "공짜"다. */
static void cfg_unpack_bf(uint32_t raw, adc_cfg_t *c) {
    (void)raw;
    memset(c, 0, sizeof *c); /* TODO: raw 를 adc_cfg_bits_t 로 복사한 뒤 필드를 읽는다 */
}

/* ---------------- Part B: 구현 정의 지점을 손으로 다루기 ---------------- */
/* v 의 하위 width 비트(1..32)를 2의 보수 부호 있는 값으로 확장한다. width 위쪽 비트는 무시.
 * 구현 정의 동작 금지: 음수의 >> (6.5.7p5), 범위 밖 unsigned → signed 변환 (6.3.1.3p3) 둘 다 쓰지 않는다. */
static int32_t sext(uint32_t v, unsigned width) {
    (void)v; (void)width;
    return 0; /* TODO */
}

/* 마스크로 풀기: OFFSET 의 부호 확장은 직접 해야 한다 */
static void cfg_unpack_mask(uint32_t raw, adc_cfg_t *c) {
    (void)raw;
    memset(c, 0, sizeof *c); /* TODO: (raw & _Msk) >> _Pos, OFFSET 은 sext 로 */
}

/* 필드 위치 탐침(probe): 0으로 채운 비트필드 구조체에서 한 필드만 전부 1로 만들고
 * 레지스터 워드(uint32_t)로 읽었을 때 어느 비트가 1인지 = "이 ABI 에서 그 필드의 마스크". */
enum { F_EN, F_MODE, F_RES, F_SMPT, F_OFFSET, F_CH, F_START, F_COUNT };
static uint32_t bf_probe_mask(int field) {
    (void)field;
    return 0u; /* TODO: 알 수 없는 field 는 0 */
}

/* 같은 선언 타입(저장 단위 unit_bits)의 비트필드 n개가 놓이는 비트 오프셋을 예측한다.
 * 규칙 (SysV x86-64 / AAPCS / Apple arm64 공통, 같은 타입일 때):
 *   - 다음 필드는 바로 이어서 놓되, 저장 단위 경계를 넘으면(straddle) 다음 단위 시작으로 민다
 *   - 폭 0 필드(:0)는 다음 단위 시작으로 민다 (6.7.2.1p12), 자기 오프셋은 민 위치
 * 반환: 구조체 크기(바이트) = 사용한 비트를 저장 단위 배수로 올림. */
static unsigned abi_place(const unsigned *w, int n, unsigned unit_bits, unsigned *off) {
    (void)w; (void)unit_bits;
    for (int i = 0; i < n; i++) off[i] = 0u;
    return 0u; /* TODO */
}

/* ---------------- Part C: 드라이버 — 레지스터 접근은 마스크로 ---------------- */
typedef struct {
    volatile uint32_t SR;     /* 0x00 */
    volatile uint32_t CFG;    /* 0x04 */
    volatile uint32_t DR;     /* 0x08 */
} ADC_TypeDef;

static ADC_TypeDef fake_adc;          /* 실제 칩이라면 (ADC_TypeDef *)0x40012000 */
#define ADC1 (&fake_adc)

/* (시뮬레이터 — 고치지 않는다) 모든 레지스터 접근은 reg_read / reg_write 로. 실제 칩에선 *r, *r = v. */
static char     g_trace[256];
static unsigned g_conv_count, g_conv_ch;
static void trace(const char *s) {
    size_t n = strlen(g_trace);
    snprintf(g_trace + n, sizeof g_trace - n, "%s%s", n ? " " : "", s);
}
static const char *name_of(const volatile uint32_t *r) {
    return r == &ADC1->SR ? "SR" : r == &ADC1->CFG ? "CFG" : "DR";
}
static uint32_t reg_read(const volatile uint32_t *r) {
    char buf[16];
    snprintf(buf, sizeof buf, "r%s", name_of(r));
    trace(buf);
    return *r;
}
static void reg_write(volatile uint32_t *r, uint32_t v) {
    char buf[24];
    snprintf(buf, sizeof buf, "w%s=%08X", name_of(r), (unsigned)v);
    trace(buf);
    if (r == &ADC1->CFG && (v & ADC_CFG_START_Msk)) {    /* 하드웨어: START 를 쓰는 순간의 CH 로 변환 시작 */
        g_conv_count++;
        g_conv_ch = (v & ADC_CFG_CH_Msk) >> ADC_CFG_CH_Pos;
        v &= ~ADC_CFG_START_Msk;                          /* START 는 읽으면 0 */
    }
    *r = v;
}
static void sim_reset(uint32_t cfg) {
    fake_adc.SR = 0; fake_adc.CFG = cfg; fake_adc.DR = 0;
    g_trace[0] = '\0'; g_conv_count = 0; g_conv_ch = 0xFFu;
}

/* (주어진 코드) "ADC1_BF->CFG_b.ch = ch; ADC1_BF->CFG_b.start = 1;" 처럼 volatile 비트필드 레지스터에
 * 필드를 하나씩 대입하면 컴파일러가 만드는 접근과 같은 모양: 필드마다 ldr / bfi(orr) / str (demo_rmw.c) */
static void adc_start_fieldwise(ADC_TypeDef *a, unsigned ch) {
    adc_cfg_bits_t b;
    uint32_t v;
    v = reg_read(&a->CFG); memcpy(&b, &v, sizeof b); b.ch = ch;     memcpy(&v, &b, sizeof v); reg_write(&a->CFG, v);
    v = reg_read(&a->CFG); memcpy(&b, &v, sizeof b); b.start = 1u;  memcpy(&v, &b, sizeof v); reg_write(&a->CFG, v);
}

/* 설정 검증 후 CFG 를 "읽기 1번 + 쓰기 1번" 으로 바꾼다. 예약 비트는 읽은 값 보존, START 는 항상 0으로 쓴다.
 * 범위 밖 값(MODE 3 포함)이 하나라도 있으면 레지스터를 건드리지 않고 false. */
static bool adc_configure(ADC_TypeDef *a, const adc_cfg_t *c) {
    (void)a; (void)c;
    return false; /* TODO */
}

/* 채널 ch 로 변환 시작: CH 와 START 를 "같은 한 번의 쓰기" 에 담는다. ch > 18 이면 false, 접근 없음. */
static bool adc_start(ADC_TypeDef *a, unsigned ch) {
    (void)a; (void)ch;
    return false; /* TODO */
}

/* 현재 보정값(부호 있음)을 읽기 1번으로 */
static int adc_read_offset(ADC_TypeDef *a) {
    (void)a;
    return 0; /* TODO */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define TRACE_IS(s) (strcmp(g_trace, (s)) == 0)

static uint32_t rng_state = 0x2545F491u;                  /* xorshift32 — 고정 시드, 재현 가능 */
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}
static bool cfg_eq(const adc_cfg_t *a, const adc_cfg_t *b) {
    return a->en == b->en && a->mode == b->mode && a->res == b->res && a->smpt == b->smpt &&
           a->offset == b->offset && a->ch == b->ch && a->start == b->start;
}
static adc_cfg_t random_cfg(void) {                       /* 모든 필드가 범위 안인 무작위 설정 */
    adc_cfg_t c;
    uint32_t r = xorshift32();
    c.en = r & 1u; c.mode = (r >> 1) & 3u; c.res = (r >> 3) & 3u; c.smpt = (r >> 5) & 0xFFu;
    c.offset = (int)((r >> 13) & 0x7Fu) - 64; c.ch = (r >> 20) & 0x1Fu; c.start = (r >> 25) & 1u;
    return c;
}
/* LE 호스트에서 "몇 번째 비트(바이트*8+비트)가 처음 1인가" — 비트필드 오프셋 탐침용 */
static unsigned lowest_bit(const void *p, size_t n) {
    const unsigned char *b = p;
    for (size_t i = 0; i < n * 8u; i++)
        if ((b[i / 8u] >> (i % 8u)) & 1u) return (unsigned)i;
    return 999u;
}
#define PROBE_OFF(T, m, out) do { T t_; memset(&t_, 0, sizeof t_); t_.m = 1u; \
                                  (out) = lowest_bit(&t_, sizeof t_); } while (0)

/* Part B 에서 abi_place 예측과 대조할 실제 구조체들 (호스트 컴파일러가 배치) */
typedef struct { unsigned a : 30; unsigned b : 4; } s30_4;            /* b 가 32비트 경계를 넘나? */
typedef struct { uint8_t  a : 6;  uint8_t  b : 4; } u8_6_4;            /* 선언 타입이 저장 단위를 바꾼다 */
typedef struct { unsigned a : 6;  unsigned b : 4; } u32_6_4;
typedef struct { unsigned a : 5;  unsigned : 0; unsigned c : 3; } zero_w;
typedef struct { uint16_t a : 10; uint16_t b : 10; uint16_t c : 10; } u16_10x3;

int main(void) {
    /* ---- Part A: 마스크 vs 비트필드 pack/unpack ---- */
    const adc_cfg_t mix = { 1u, 2u, 1u, 0x5Au, -3, 7u, 0u };
    const adc_cfg_t max = { 1u, 3u, 3u, 0xFFu, -1, 31u, 1u };
    const adc_cfg_t zero = { 0u, 0u, 0u, 0u, 0, 0u, 0u };
    CHECK(cfg_pack_mask(&mix) == 0x070FAB4Du);
    CHECK(cfg_pack_mask(&max) == 0x3F0FFFFFu);                /* = ~RSVD: 예약 비트는 절대 안 켜진다 */
    CHECK(cfg_pack_mask(&zero) == 0u);
    const adc_cfg_t lo = { 0u, 0u, 0u, 0u, -64, 0u, 0u }, hi = { 0u, 0u, 0u, 0u, 63, 0u, 0u };
    CHECK(cfg_pack_mask(&lo) == 0x00080000u && cfg_pack_mask(&hi) == 0x0007E000u);
    const adc_cfg_t wide = { 3u, 7u, 0u, 0x1FFu, 0, 0x3Fu, 0u };   /* 넘친 비트는 잘려 이웃을 침범 않음 */
    CHECK(cfg_pack_mask(&wide) == (ADC_CFG_EN_Msk | ADC_CFG_MODE_Msk | ADC_CFG_SMPT_Msk | ADC_CFG_CH_Msk));
    CHECK(cfg_pack_bf(&mix) == 0x070FAB4Du);
    CHECK(cfg_pack_bf(&max) == 0x3F0FFFFFu);
    CHECK(cfg_pack_bf(&zero) == 0u);
    adc_cfg_t got;
    cfg_unpack_bf(0x070FAB4Du, &got);
    CHECK(cfg_eq(&got, &mix));                                 /* offset -3 이 부호까지 복원 */
    cfg_unpack_bf(0xC0F00000u | 0x070FAB4Du, &got);
    CHECK(cfg_eq(&got, &mix));                                 /* 예약 비트는 어떤 필드에도 안 섞인다 */
    int agree = 0, roundtrip = 0;
    for (int i = 0; i < 2000; i++) {                           /* 퍼즈: 2000 개 무작위 설정 */
        adc_cfg_t c = random_cfg(), back;
        uint32_t m = cfg_pack_mask(&c);
        agree += (m == cfg_pack_bf(&c));
        cfg_unpack_bf(m, &back);
        roundtrip += cfg_eq(&back, &c);
    }
    CHECK(agree == 2000);
    CHECK(roundtrip == 2000);

    /* ---- Part B: 부호 확장 · 탐침 · 배치 규칙 ---- */
    CHECK(sext(0x3Fu, 7) == 63 && sext(0x40u, 7) == -64 && sext(0x7Fu, 7) == -1);
    CHECK(sext(0x1u, 1) == -1 && sext(0x0u, 1) == 0);
    CHECK(sext(0xFFFFFF85u, 7) == 5);                          /* width 위쪽 비트는 무시 */
    CHECK(sext(0x80000000u, 32) == INT32_MIN && sext(0xFFFFFFFFu, 32) == -1 && sext(0x7FFFFFFFu, 32) == INT32_MAX);
    CHECK(sext(0x8000u, 16) == -32768 && sext(0x7FFFu, 16) == 32767);
    int sext_ok = 0;
    for (int v = -64; v <= 63; v++) {                          /* 7비트 전수: 비트필드 읽기와 대조 */
        adc_cfg_bits_t b; uint32_t raw;
        memset(&b, 0, sizeof b); b.offset = v; memcpy(&raw, &b, sizeof raw);
        sext_ok += (sext(raw >> ADC_CFG_OFFSET_Pos, 7) == b.offset) && (b.offset == v);
    }
    CHECK(sext_ok == 128);
    cfg_unpack_mask(0x070FAB4Du, &got);
    CHECK(cfg_eq(&got, &mix));
    cfg_unpack_mask(0xFFFFFFFFu, &got);
    CHECK(cfg_eq(&got, &max));
    int unpack_ok = 0;
    for (int i = 0; i < 2000; i++) {
        uint32_t r = xorshift32();
        adc_cfg_t x, y;
        cfg_unpack_mask(r, &x); cfg_unpack_bf(r, &y);
        unpack_ok += cfg_eq(&x, &y);
    }
    CHECK(unpack_ok == 2000);
    const uint32_t want[F_COUNT] = { ADC_CFG_EN_Msk, ADC_CFG_MODE_Msk, ADC_CFG_RES_Msk, ADC_CFG_SMPT_Msk,
                                     ADC_CFG_OFFSET_Msk, ADC_CFG_CH_Msk, ADC_CFG_START_Msk };
    int probe_ok = 0;
    for (int f = 0; f < F_COUNT; f++) probe_ok += (bf_probe_mask(f) == want[f]);
    CHECK(probe_ok == F_COUNT);                                /* 이 ABI 에선 비트필드 = 데이터시트 */
    CHECK(bf_probe_mask(F_EN) == 0x00000001u);                 /* LSB 부터 채운다 (big-endian ARM 이면 0x80000000) */
    CHECK(bf_probe_mask(F_COUNT) == 0u);

    unsigned off[9], real;
    const unsigned w_reg[9] = { 1, 2, 2, 8, 7, 4, 5, 1, 2 };   /* adc_cfg_bits_t 의 폭 목록 */
    CHECK(abi_place(w_reg, 9, 32, off) == 4u && off[3] == 5u && off[4] == 13u && off[6] == 24u && off[8] == 30u);
    const unsigned w1[2] = { 30, 4 };
    CHECK(abi_place(w1, 2, 32, off) == sizeof(s30_4));
    PROBE_OFF(s30_4, b, real);   CHECK(off[1] == real && real == 32u);   /* 넘지 않고 다음 단위로 */
    const unsigned w2[2] = { 6, 4 };
    CHECK(abi_place(w2, 2, 8, off) == sizeof(u8_6_4));
    PROBE_OFF(u8_6_4, b, real);  CHECK(off[1] == real && real == 8u);
    CHECK(abi_place(w2, 2, 32, off) == sizeof(u32_6_4));
    PROBE_OFF(u32_6_4, b, real); CHECK(off[1] == real && real == 6u);   /* 같은 폭인데 타입만 다르면 위치가 다름 */
    const unsigned w3[3] = { 5, 0, 3 };
    CHECK(abi_place(w3, 3, 32, off) == sizeof(zero_w));
    PROBE_OFF(zero_w, c, real);  CHECK(off[2] == real && real == 32u);
    const unsigned w4[3] = { 10, 10, 10 };
    CHECK(abi_place(w4, 3, 16, off) == sizeof(u16_10x3));
    PROBE_OFF(u16_10x3, b, real); CHECK(off[1] == real);
    PROBE_OFF(u16_10x3, c, real); CHECK(off[2] == real && real == 32u);

    /* ---- Part C: 드라이버 ---- */
    sim_reset(0x40500000u);                                    /* 예약 비트 30, 22, 20 이 1로 읽히는 칩 */
    const adc_cfg_t cfg = { 1u, 1u, 2u, 0x30u, -5, 3u, 1u };   /* start=1 이어도 configure 는 시작 안 함 */
    CHECK(adc_configure(ADC1, &cfg) == true);
    CHECK(TRACE_IS("rCFG wCFG=435F6613"));                     /* 읽기 1 + 쓰기 1, 예약 비트 보존 */
    CHECK(fake_adc.CFG == 0x435F6613u && g_conv_count == 0u);
    const adc_cfg_t bad[] = {
        { 1u, 3u, 0u, 0u, 0, 0u, 0u },      /* MODE 3 = reserved */
        { 2u, 0u, 0u, 0u, 0, 0u, 0u },      /* EN 은 1비트 */
        { 0u, 0u, 4u, 0u, 0, 0u, 0u },      /* RES */
        { 0u, 0u, 0u, 256u, 0, 0u, 0u },    /* SMPT */
        { 0u, 0u, 0u, 0u, -65, 0u, 0u },    /* OFFSET 아래 */
        { 0u, 0u, 0u, 0u, 64, 0u, 0u },     /* OFFSET 위 */
        { 0u, 0u, 0u, 0u, 0, 19u, 0u },     /* CH 0..18 */
    };
    int bad_ok = 0;
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        sim_reset(0x40500000u);
        bad_ok += !adc_configure(ADC1, &bad[i]) && TRACE_IS("") && fake_adc.CFG == 0x40500000u;
    }
    CHECK(bad_ok == 7);                                        /* 거부 + 접근 0회 + 값 그대로 */

    sim_reset(0x435F6613u);
    CHECK(adc_start(ADC1, 5u) == true);
    CHECK(TRACE_IS("rCFG wCFG=655F6613"));                     /* CH 와 START 를 한 번에 */
    CHECK(g_conv_count == 1u && g_conv_ch == 5u && fake_adc.CFG == 0x455F6613u);
    sim_reset(0x435F6613u);
    CHECK(adc_start(ADC1, 19u) == false && TRACE_IS("") && g_conv_count == 0u);
    sim_reset(0x435F6613u);
    adc_start_fieldwise(ADC1, 5u);                             /* 비트필드 필드별 대입 = 필드마다 RMW */
    CHECK(TRACE_IS("rCFG wCFG=455F6613 rCFG wCFG=655F6613"));  /* 버스 접근 4번, 중간 상태가 버스에 보임 */
    CHECK(g_conv_count == 1u && fake_adc.CFG == 0x455F6613u);
    sim_reset(0x435F6613u);
    CHECK(adc_read_offset(ADC1) == -5 && TRACE_IS("rCFG"));
    sim_reset(0x00080000u);
    CHECK(adc_read_offset(ADC1) == -64);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
