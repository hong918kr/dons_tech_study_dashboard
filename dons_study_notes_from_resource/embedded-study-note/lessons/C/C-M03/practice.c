/* C-M03 추가 연습 — 한 번에 검증
 * 1) 1비트 int 비트필드 함정   2) 로컬 사본(shadow) 패턴   3) big-endian 배치 예측
 * 4) 선언 타입이 섞인 비트필드 크기: SysV/AAPCS vs MSVC   5) X-macro 한 곳 정의 레지스터 맵 + 덤프
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 본문과 같은 레지스터 (마스크 + 비트필드) */
#define ADC_CFG_CH_Pos      24u
#define ADC_CFG_CH_Msk      (0x1Fu << ADC_CFG_CH_Pos)
#define ADC_CFG_START_Msk   (0x1u << 29)
typedef struct {
    uint32_t   en     : 1;
    uint32_t   mode   : 2;
    uint32_t   res    : 2;
    uint32_t   smpt   : 8;
    signed int offset : 7;
    uint32_t          : 4;
    uint32_t   ch     : 5;
    uint32_t   start  : 1;
    uint32_t          : 2;
} adc_cfg_bits_t;
typedef union { adc_cfg_bits_t b; uint32_t w; } adc_cfg_reg_t;
typedef struct { unsigned en, mode, res, smpt; int offset; unsigned ch, start; } adc_cfg_t;

/* ---------- 연습 1: 1비트 int 비트필드 ---------- */
struct legacy_status { int ready : 1; int error : 1; };              /* 옛 헤더: plain int 1비트 */
struct fixed_status  { unsigned ready : 1; unsigned error : 1; };    /* 고친 헤더 */
static bool legacy_is_ready(const struct legacy_status *s) { return s->ready != 0; }  /* == 1 로 비교 금지 */

/* ---------- 연습 2: 로컬 사본 패턴 — 비트필드는 지역 변수에만, 레지스터는 워드로 1R + 1W ---------- */
static uint32_t shadow_set_ch_start(uint32_t raw, unsigned ch) {
    adc_cfg_reg_t t;
    t.w = raw;                       /* 레지스터 읽기 1번 (워드) */
    t.b.ch = ch & 0x1Fu;
    t.b.start = 1u;
    return t.w;                      /* 레지스터 쓰기 1번 (워드) — union 다른 멤버 읽기는 C11 에서 허용 (6.5.2.3 각주 95) */
}
static uint32_t mask_set_ch_start(uint32_t raw, unsigned ch) {
    return (raw & ~ADC_CFG_CH_Msk) | ((ch << ADC_CFG_CH_Pos) & ADC_CFG_CH_Msk) | ADC_CFG_START_Msk;
}

/* ---------- 연습 3: big-endian(MSB-first) 배치 예측 ----------
 * LE 에서 비트 pos..pos+w-1 을 차지하는 필드는, 같은 선언을 BE ARM 으로 컴파일하면
 * 레지스터 워드의 비트 (31-pos-w+1)..(31-pos) 로 "거울상" 이동한다. 필드 안의 비트 순서(값)는 그대로. */
static uint32_t be_field_mask(unsigned le_pos, unsigned width) {
    uint32_t m = (width >= 32u) ? 0xFFFFFFFFu : ((1u << width) - 1u);
    return m << (32u - le_pos - width);
}
static uint32_t be_put(uint32_t val, unsigned le_pos, unsigned width) {
    return (val << (32u - le_pos - width)) & be_field_mask(le_pos, width);
}
static uint32_t pack_msb_first(const adc_cfg_t *c) {
    return be_put(c->en, 0, 1) | be_put(c->mode, 1, 2) | be_put(c->res, 3, 2) | be_put(c->smpt, 5, 8)
         | be_put((uint32_t)c->offset, 13, 7) | be_put(c->ch, 24, 5) | be_put(c->start, 29, 1);
}

/* ---------- 연습 4: 선언 타입이 섞인 비트필드 구조체 크기 예측 ---------- */
typedef struct { unsigned type_bytes, width; } bf_decl;
static unsigned round_up(unsigned x, unsigned a) { return (x + a - 1u) / a * a; }
/* SysV x86-64 / AAPCS / Apple arm64: 비트 커서를 하나 두고, 필드가 "자기 선언 타입 크기로 정렬된 구간"을
 * 넘으면 그 경계로 민다. 구조체 정렬 = 이름 있는 필드 선언 타입 중 최대. */
static unsigned size_sysv(const bf_decl *f, int n) {
    unsigned pos = 0, align = 1;
    for (int i = 0; i < n; i++) {
        unsigned s = f[i].type_bytes * 8u;
        if (f[i].width == 0u || pos / s != (pos + f[i].width - 1u) / s) pos = round_up(pos, s);
        pos += f[i].width;
        if (f[i].width && f[i].type_bytes > align) align = f[i].type_bytes;
    }
    return round_up(round_up(pos, 8u) / 8u, align);
}
/* MSVC: 저장 단위를 하나씩 연다. 선언 타입 크기가 바뀌거나 남은 비트가 모자라면 새 단위(그 크기로 정렬). */
static unsigned size_ms(const bf_decl *f, int n) {
    unsigned end = 0, unit_bytes = 0, used = 0, align = 1;   /* end: 지금까지 쓴 바이트 끝 */
    for (int i = 0; i < n; i++) {
        unsigned tb = f[i].type_bytes;
        if (f[i].width == 0u) { unit_bytes = 0; continue; }
        if (unit_bytes != tb || used + f[i].width > tb * 8u) {
            end = round_up(end, tb) + tb;                        /* 새 단위 하나를 통째로 차지 */
            unit_bytes = tb; used = 0;
        }
        used += f[i].width;
        if (tb > align) align = tb;
    }
    return round_up(end, align);
}
typedef struct { uint8_t a : 4; uint32_t b : 4; } mix_8_32;
typedef struct { uint32_t a : 4; uint8_t b : 4; } mix_32_8;
typedef struct { uint8_t a : 4; uint16_t b : 4; uint8_t c : 4; } mix_8_16_8;
typedef struct { uint8_t a : 3; uint16_t b : 7; uint32_t c : 20; } mix_8_16_32;
typedef struct { uint16_t a : 10; uint16_t b : 10; uint16_t c : 10; } u16_10x3;

/* ---------- 연습 5: X-macro 로 레지스터 맵을 한 곳에 ---------- */
/*        이름     pos width signed */
#define ADC_CFG_FIELDS(X) \
    X(EN,      0,  1, 0)  \
    X(MODE,    1,  2, 0)  \
    X(RES,     3,  2, 0)  \
    X(SMPT,    5,  8, 0)  \
    X(OFFSET, 13,  7, 1)  \
    X(CH,     24,  5, 0)  \
    X(START,  29,  1, 0)
#define X_MSK(name, pos, w, sg) XCFG_##name##_Msk = (int)((0xFFFFFFFFu >> (32 - (w))) << (pos)),  /* 모두 < 2^31 → int 범위 */
enum xcfg_msk { ADC_CFG_FIELDS(X_MSK) };
#define X_OR(name, pos, w, sg)  | (uint64_t)XCFG_##name##_Msk
#define X_SUM(name, pos, w, sg) + (uint64_t)XCFG_##name##_Msk
/* 겹치는 비트가 없으면 마스크들의 합 == OR (겹치면 같은 비트가 두 번 더해져 합이 커진다) */
_Static_assert((0u ADC_CFG_FIELDS(X_SUM)) == (0u ADC_CFG_FIELDS(X_OR)), "ADC_CFG fields overlap");
_Static_assert((uint32_t)XCFG_SMPT_Msk == 0x00001FE0u, "SMPT mask");

typedef struct { const char *name; unsigned pos, width; bool is_signed; } field_desc;
#define X_DESC(name, pos, w, sg) { #name, pos, w, sg },
static const field_desc k_fields[] = { ADC_CFG_FIELDS(X_DESC) };
#define N_FIELDS ((int)(sizeof k_fields / sizeof k_fields[0]))

static int32_t field_value(uint32_t raw, const field_desc *d) {
    uint32_t v = (raw >> d->pos) & (0xFFFFFFFFu >> (32u - d->width));
    if (d->is_signed && (v >> (d->width - 1u)) & 1u)
        return (int32_t)v - (int32_t)(1u << d->width);           /* width <= 31 전제: 두 값 모두 int32 범위 */
    return (int32_t)v;
}
/* "EN=1 MODE=2 ... START=0" 형태 — UART 로그용 레지스터 덤프 */
static int cfg_format(char *buf, size_t cap, uint32_t raw) {
    size_t n = 0;
    for (int i = 0; i < N_FIELDS && n < cap; i++) {
        int k = snprintf(buf + n, cap - n, "%s%s=%ld", i ? " " : "", k_fields[i].name,
                         (long)field_value(raw, &k_fields[i]));
        if (k < 0) return -1;
        n += (size_t)k;
    }
    return (int)n;
}

/* ---------------- 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t rng_state = 0x2545F491u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

int main(int argc, char **argv) {
    (void)argv;
    /* 연습 1 */
    struct legacy_status ls = { 0, 0 };
    struct fixed_status fs = { 0u, 0u };
    int one = argc;                               /* 런타임 1 (상수 1 이면 -Wsingle-bit-bitfield-constant-conversion) */
    ls.ready = one;                               /* 범위 밖 → 구현 정의. clang/GCC 는 -1 */
    fs.ready = (unsigned)one;
    CHECK(ls.ready == -1);                        /* 이 컴파일러에서 관찰되는 값 */
    CHECK(!(ls.ready == 1));                      /* 그래서 == 1 비교는 영원히 거짓 */
    CHECK(legacy_is_ready(&ls) == true);
    CHECK(fs.ready == 1u && fs.error == 0u);

    /* 연습 2 */
    int same = 0, rsvd_kept = 0;
    for (int i = 0; i < 1000; i++) {
        uint32_t raw = xorshift32() & ~ADC_CFG_START_Msk;   /* START 는 읽으면 0 */
        unsigned ch = xorshift32() % 19u;
        uint32_t s = shadow_set_ch_start(raw, ch);
        same += (s == mask_set_ch_start(raw, ch));
        rsvd_kept += ((s & 0xC0F00000u) == (raw & 0xC0F00000u));
    }
    CHECK(same == 1000);
    CHECK(rsvd_kept == 1000);                     /* 이 ABI 에서 이름 없는 필드(예약)도 보존 */

    /* 연습 3: demo_layout.c 를 --target=thumbebv7em-none-eabi 로 컴파일해 objdump 로 본 실제 워드 */
    CHECK(be_field_mask(0, 1) == 0x80000000u);   /* k_en   80000000 */
    CHECK(be_field_mask(1, 2) == 0x60000000u);   /* k_mode 60000000 */
    CHECK(be_field_mask(5, 8) == 0x07F80000u);   /* k_smpt 07f80000 */
    CHECK(be_field_mask(13, 7) == 0x0007F000u);  /* k_off  0007f000 */
    CHECK(be_field_mask(24, 5) == 0x000000F8u);  /* k_ch   000000f8 */
    const adc_cfg_t mix = { 1u, 2u, 1u, 0x5Au, -3, 7u, 0u };
    CHECK(pack_msb_first(&mix) == 0xCAD7D038u);  /* k_mix  cad7d038 (LE 에선 0x070FAB4D) */
    CHECK(be_field_mask(0, 32) == 0xFFFFFFFFu);

    /* 연습 4: SysV 예측은 호스트 sizeof 와, MSVC 예측은 --target=x86_64-pc-windows-msvc 실측값과 대조 */
    const bf_decl d1[] = { {1, 4}, {4, 4} };
    const bf_decl d2[] = { {4, 4}, {1, 4} };
    const bf_decl d3[] = { {1, 4}, {2, 4}, {1, 4} };
    const bf_decl d4[] = { {1, 3}, {2, 7}, {4, 20} };
    const bf_decl d5[] = { {2, 10}, {2, 10}, {2, 10} };
    const bf_decl dreg[] = { {4,1}, {4,2}, {4,2}, {4,8}, {4,7}, {4,4}, {4,5}, {4,1}, {4,2} };
    CHECK(size_sysv(d1, 2) == sizeof(mix_8_32)    && size_ms(d1, 2) == 8u);
    CHECK(size_sysv(d2, 2) == sizeof(mix_32_8)    && size_ms(d2, 2) == 8u);
    CHECK(size_sysv(d3, 3) == sizeof(mix_8_16_8)  && size_ms(d3, 3) == 6u);
    CHECK(size_sysv(d4, 3) == sizeof(mix_8_16_32) && size_ms(d4, 3) == 8u);
    CHECK(size_sysv(d5, 3) == sizeof(u16_10x3)    && size_ms(d5, 3) == 6u);
    CHECK(size_sysv(dreg, 9) == sizeof(adc_cfg_bits_t) && size_ms(dreg, 9) == 4u);  /* 같은 크기 타입만 → 같음 */
    CHECK(sizeof(mix_8_32) == 4u && sizeof(mix_8_16_8) == 2u);                       /* 호스트(AAPCS64 계열) */

    /* 연습 5 */
    char buf[96];
    CHECK(cfg_format(buf, sizeof buf, 0x070FAB4Du) > 0 &&
          strcmp(buf, "EN=1 MODE=2 RES=1 SMPT=90 OFFSET=-3 CH=7 START=0") == 0);
    CHECK(cfg_format(buf, sizeof buf, 0x00080000u) > 0 &&
          strcmp(buf, "EN=0 MODE=0 RES=0 SMPT=0 OFFSET=-64 CH=0 START=0") == 0);
    CHECK((uint32_t)XCFG_OFFSET_Msk == 0x000FE000u && (uint32_t)XCFG_CH_Msk == 0x1F000000u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
