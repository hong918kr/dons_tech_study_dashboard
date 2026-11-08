/* C-B12 구조체 기초: 패딩 · 정렬 · sizeof · offsetof
 *
 * 기준 ABI: LP64 (macOS arm64 / Linux aarch64). ILP32 Cortex-M 값은 md의 표 참고.
 *   Part A — 6개 구조체의 sizeof/alignof/offsetof를 손으로 계산해 표(enum)에 적는다.
 *            런타임 CHECK가 모두 PASS하면 PIN_LAYOUT 을 1로 → _Static_assert 로 못 박는다.
 *   Part B — 레이아웃 계산기(align_up / compute_layout / min_layout_size) +
 *            필드 재배열로 크기를 최소화한 *_min 구조체.
 *   Part C — 데이터시트 오프셋 표와 일치하는 레지스터 블록 구조체 (reserved 필드).
 */
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define TODO 0

/* ======================= Part A: 손 계산 → 표 → 고정 ======================= */
struct s1 { uint8_t a; uint32_t b; uint8_t c; };
struct s2 { uint32_t b; uint8_t a; uint8_t c; };
struct s3 { uint8_t a; uint16_t b; uint8_t c; uint64_t d; };
struct s4 { uint8_t a; uint64_t d; uint8_t c; };
struct s5 { uint16_t a; uint8_t b[3]; uint32_t c; uint8_t d; };
struct s6 { uint8_t tag; long n; void *p; uint16_t len; };   /* ABI 의존! (LP64 기준) */

/* 손으로 계산한 값 (LP64). 첫 필드 오프셋은 항상 0 이라 생략. */
enum {
    S1_SIZE = 12, S1_ALIGN = 4, S1_B = 4, S1_C = 8,
    S2_SIZE = 8, S2_ALIGN = 4, S2_A = 4, S2_C = 5,
    S3_SIZE = 16, S3_ALIGN = 8, S3_B = 2, S3_C = 4, S3_D = 8,
    S4_SIZE = 24, S4_ALIGN = 8, S4_D = 8, S4_C = 16,
    S5_SIZE = 16, S5_ALIGN = 4, S5_B = 2, S5_C = 8, S5_D = 12,
    S6_SIZE = 32, S6_ALIGN = 8, S6_N = 8, S6_P = 16, S6_LEN = 24,
    S1X4_SIZE  = 48,   /* sizeof(struct s1[4])                 */
    S1X4_1B    = 16,   /* (char*)&arr[1].b - (char*)arr        */
};

/* 런타임 CHECK가 전부 PASS하면 1로 바꿔라 → 이제 틀리면 컴파일이 실패한다 */
#define PIN_LAYOUT 1

/* ======================= Part B: 레이아웃 계산기 ======================= */
typedef struct { size_t size, align; } field_t;
#define F(T) { sizeof(T), alignof(T) }

/* a 는 2의 거듭제곱. x 를 a 의 배수로 올림 */
static size_t align_up(size_t x, size_t a) {
    return (x + a - 1u) & ~(a - 1u);
}

/* 필드를 선언 순서대로 배치: off[i] 에 오프셋, *align_out 에 구조체 정렬, 반환값 = sizeof */
static size_t compute_layout(const field_t *f, size_t n, size_t *off, size_t *align_out) {
    size_t pos = 0, max_al = 1;
    for (size_t i = 0; i < n; i++) {
        pos = align_up(pos, f[i].align);          /* 내부 패딩 */
        off[i] = pos;
        pos += f[i].size;
        if (f[i].align > max_al) max_al = f[i].align;
    }
    *align_out = max_al;
    return align_up(pos, max_al);                 /* 꼬리 패딩 (tail padding) */
}

/* 필드 순서를 마음대로 바꿀 수 있을 때 가능한 최소 sizeof (n <= 16) */
static size_t min_layout_size(const field_t *f, size_t n) {
    field_t t[16];
    size_t off[16], al;
    for (size_t i = 0; i < n; i++) t[i] = f[i];
    for (size_t i = 1; i < n; i++) {              /* 정렬 내림차순 삽입 정렬 */
        field_t k = t[i];
        size_t j = i;
        while (j > 0 && t[j - 1].align < k.align) { t[j] = t[j - 1]; j--; }
        t[j] = k;
    }
    return compute_layout(t, n, off, &al);
}

/* 재배열로 최소화한 버전 — 필드 집합은 원본과 같아야 한다 */
struct s1_min { uint32_t b; uint8_t a; uint8_t c; };
struct s4_min { uint64_t d; uint8_t a; uint8_t c; };
struct s5_min { uint32_t c; uint16_t a; uint8_t b[3]; uint8_t d; };
struct s6_min { long n; void *p; uint16_t len; uint8_t tag; };

/* 중첩 구조체와 _Alignas */
struct outer { uint8_t x; struct s3 in; uint8_t y; };
struct dmabuf { uint8_t tag; _Alignas(16) uint8_t buf[4]; };

/* ======================= Part C: 데이터시트 → 레지스터 구조체 =======================
 *  SPIX (가상의 SPI+DMA 컨트롤러) — 데이터시트 레지스터 맵
 *   0x000 CR        0x004 SR        0x008 DR (16비트)   0x00A–0x00B reserved
 *   0x00C BRR       0x010–0x01F reserved
 *   0x020 + 0x10*n  CH[n].CFG   (n = 0..3)
 *   0x024 + 0x10*n  CH[n].CNT
 *   0x028 + 0x10*n  CH[n].ADDR
 *   0x02C + 0x10*n  reserved
 *   0x060–0x0FB reserved        0x0FC VERSION (read-only)     블록 크기 0x100
 */
typedef struct {
    volatile uint32_t CFG;
    volatile uint32_t CNT;
    volatile uint32_t ADDR;
    uint32_t RESERVED;                 /* 채널 stride = 0x10 */
} SPIX_Chan;

typedef struct {
    volatile uint32_t CR;              /* 0x000 */
    volatile uint32_t SR;              /* 0x004 */
    volatile uint16_t DR;              /* 0x008 */
    uint16_t RESERVED0;                /* 0x00A */
    volatile uint32_t BRR;             /* 0x00C */
    uint32_t RESERVED1[4];             /* 0x010–0x01F */
    SPIX_Chan CH[4];                   /* 0x020–0x05F */
    uint32_t RESERVED2[39];            /* 0x060–0x0FB */
    volatile const uint32_t VERSION;   /* 0x0FC */
} SPIX_TypeDef;

#define SPIX_CFG_EN (1u << 0)

/* 채널 ch 에 DMA 주소/길이를 쓰고 EN 비트를 켠다 (순서: ADDR → CNT → CFG.EN) */
static void spix_ch_start(SPIX_TypeDef *p, unsigned ch, uint32_t addr, uint32_t cnt) {
    p->CH[ch].ADDR = addr;
    p->CH[ch].CNT  = cnt;
    p->CH[ch].CFG |= SPIX_CFG_EN;
}

/* ======================= 컴파일 타임 고정 ======================= */
#if PIN_LAYOUT
_Static_assert(sizeof(struct s1) == S1_SIZE && alignof(struct s1) == S1_ALIGN, "s1");
_Static_assert(offsetof(struct s1, b) == S1_B && offsetof(struct s1, c) == S1_C, "s1 offs");
_Static_assert(sizeof(struct s2) == S2_SIZE && alignof(struct s2) == S2_ALIGN, "s2");
_Static_assert(offsetof(struct s2, a) == S2_A && offsetof(struct s2, c) == S2_C, "s2 offs");
_Static_assert(sizeof(struct s3) == S3_SIZE && alignof(struct s3) == S3_ALIGN, "s3");
_Static_assert(offsetof(struct s3, b) == S3_B && offsetof(struct s3, c) == S3_C
               && offsetof(struct s3, d) == S3_D, "s3 offs");
_Static_assert(sizeof(struct s4) == S4_SIZE && alignof(struct s4) == S4_ALIGN, "s4");
_Static_assert(offsetof(struct s4, d) == S4_D && offsetof(struct s4, c) == S4_C, "s4 offs");
_Static_assert(sizeof(struct s5) == S5_SIZE && alignof(struct s5) == S5_ALIGN, "s5");
_Static_assert(offsetof(struct s5, b) == S5_B && offsetof(struct s5, c) == S5_C
               && offsetof(struct s5, d) == S5_D, "s5 offs");
_Static_assert(sizeof(struct s6) == S6_SIZE && alignof(struct s6) == S6_ALIGN, "s6 (LP64)");
_Static_assert(offsetof(struct s6, n) == S6_N && offsetof(struct s6, p) == S6_P
               && offsetof(struct s6, len) == S6_LEN, "s6 offs (LP64)");
_Static_assert(sizeof(struct s1[4]) == S1X4_SIZE, "s1[4]");

/* 레지스터 맵: 데이터시트 표 한 줄 = assert 한 줄 */
_Static_assert(offsetof(SPIX_TypeDef, DR)      == 0x008, "DR");
_Static_assert(offsetof(SPIX_TypeDef, BRR)     == 0x00C, "BRR");
_Static_assert(offsetof(SPIX_TypeDef, CH)      == 0x020, "CH[0]");
_Static_assert(sizeof(SPIX_Chan)               == 0x010, "CH stride");
_Static_assert(offsetof(SPIX_Chan, ADDR)       == 0x008, "CH.ADDR");
_Static_assert(offsetof(SPIX_TypeDef, VERSION) == 0x0FC, "VERSION");
_Static_assert(sizeof(SPIX_TypeDef)            == 0x100, "SPIX block size");
#endif

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* compute_layout 결과가 컴파일러(offsetof/sizeof/alignof)와 같은가 */
static int layout_matches(const field_t *f, size_t n, const size_t *want_off,
                          size_t want_size, size_t want_align) {
    size_t off[16] = {0}, al = 0;
    size_t sz = compute_layout(f, n, off, &al);
    if (sz != want_size || al != want_align) return 0;
    for (size_t i = 0; i < n; i++)
        if (off[i] != want_off[i]) return 0;
    return 1;
}
#define N(arr) (sizeof(arr) / sizeof((arr)[0]))

/* 레지스터 블록의 base + off 위치에 있는 32비트 레지스터를 "주소로" 읽는다 */
static uint32_t raw_read32(const volatile void *base, size_t off) {
    return *(const volatile uint32_t *)((const volatile uint8_t *)base + off);
}

static SPIX_TypeDef fake_spix;              /* 실제 칩이라면 (SPIX_TypeDef *)0x40013000 */
#define SPIX (&fake_spix)

int main(void) {
    /* ---- Part A: 손 계산 표 vs 컴파일러 ---- */
    CHECK(sizeof(struct s1) == S1_SIZE);   CHECK(alignof(struct s1) == S1_ALIGN);
    CHECK(offsetof(struct s1, b) == S1_B); CHECK(offsetof(struct s1, c) == S1_C);
    CHECK(sizeof(struct s2) == S2_SIZE);   CHECK(alignof(struct s2) == S2_ALIGN);
    CHECK(offsetof(struct s2, a) == S2_A); CHECK(offsetof(struct s2, c) == S2_C);
    CHECK(sizeof(struct s3) == S3_SIZE);   CHECK(alignof(struct s3) == S3_ALIGN);
    CHECK(offsetof(struct s3, b) == S3_B); CHECK(offsetof(struct s3, c) == S3_C);
    CHECK(offsetof(struct s3, d) == S3_D);
    CHECK(sizeof(struct s4) == S4_SIZE);   CHECK(alignof(struct s4) == S4_ALIGN);
    CHECK(offsetof(struct s4, d) == S4_D); CHECK(offsetof(struct s4, c) == S4_C);
    CHECK(sizeof(struct s5) == S5_SIZE);   CHECK(alignof(struct s5) == S5_ALIGN);
    CHECK(offsetof(struct s5, b) == S5_B); CHECK(offsetof(struct s5, c) == S5_C);
    CHECK(offsetof(struct s5, d) == S5_D);
    CHECK(sizeof(struct s6) == S6_SIZE);   CHECK(alignof(struct s6) == S6_ALIGN);
    CHECK(offsetof(struct s6, n) == S6_N); CHECK(offsetof(struct s6, p) == S6_P);
    CHECK(offsetof(struct s6, len) == S6_LEN);
    struct s1 arr[4];
    CHECK(sizeof arr == S1X4_SIZE);
    CHECK((size_t)((char *)&arr[1].b - (char *)arr) == S1X4_1B);

    /* ---- Part B: 계산기 vs 컴파일러 ---- */
    CHECK(align_up(0, 4) == 0 && align_up(1, 4) == 4 && align_up(4, 4) == 4);
    CHECK(align_up(13, 8) == 16 && align_up(17, 16) == 32 && align_up(5, 1) == 5);

    const field_t f1[] = { F(uint8_t), F(uint32_t), F(uint8_t) };
    const field_t f3[] = { F(uint8_t), F(uint16_t), F(uint8_t), F(uint64_t) };
    const field_t f4[] = { F(uint8_t), F(uint64_t), F(uint8_t) };
    const field_t f5[] = { F(uint16_t), F(uint8_t[3]), F(uint32_t), F(uint8_t) };
    const field_t f6[] = { F(uint8_t), F(long), F(void *), F(uint16_t) };
    const field_t fo[] = { F(uint8_t), F(struct s3), F(uint8_t) };
    const field_t fd[] = { F(uint8_t), { 4, 16 } };            /* _Alignas(16) uint8_t[4] */

    const size_t o1[] = { 0, offsetof(struct s1, b), offsetof(struct s1, c) };
    const size_t o3[] = { 0, offsetof(struct s3, b), offsetof(struct s3, c), offsetof(struct s3, d) };
    const size_t o4[] = { 0, offsetof(struct s4, d), offsetof(struct s4, c) };
    const size_t o5[] = { 0, offsetof(struct s5, b), offsetof(struct s5, c), offsetof(struct s5, d) };
    const size_t o6[] = { 0, offsetof(struct s6, n), offsetof(struct s6, p), offsetof(struct s6, len) };
    const size_t oo[] = { 0, offsetof(struct outer, in), offsetof(struct outer, y) };
    const size_t od[] = { 0, offsetof(struct dmabuf, buf) };

    CHECK(layout_matches(f1, N(f1), o1, sizeof(struct s1), alignof(struct s1)));
    CHECK(layout_matches(f3, N(f3), o3, sizeof(struct s3), alignof(struct s3)));
    CHECK(layout_matches(f4, N(f4), o4, sizeof(struct s4), alignof(struct s4)));
    CHECK(layout_matches(f5, N(f5), o5, sizeof(struct s5), alignof(struct s5)));
    CHECK(layout_matches(f6, N(f6), o6, sizeof(struct s6), alignof(struct s6)));
    CHECK(layout_matches(fo, N(fo), oo, sizeof(struct outer), alignof(struct outer)));
    CHECK(layout_matches(fd, N(fd), od, sizeof(struct dmabuf), alignof(struct dmabuf)));

    CHECK(min_layout_size(f1, N(f1)) == 8);
    CHECK(min_layout_size(f4, N(f4)) == 16);
    CHECK(min_layout_size(f5, N(f5)) == 12);
    CHECK(min_layout_size(f6, N(f6)) == (sizeof(long) == 8 ? 24u : 12u));
    CHECK(sizeof(struct s1_min) == min_layout_size(f1, N(f1)));
    CHECK(sizeof(struct s4_min) == min_layout_size(f4, N(f4)));
    CHECK(sizeof(struct s5_min) == min_layout_size(f5, N(f5)));
    CHECK(sizeof(struct s6_min) == min_layout_size(f6, N(f6)));

    /* ---- Part C: 레지스터 블록 ---- */
    CHECK(offsetof(SPIX_TypeDef, BRR) == 0x00C);
    CHECK(offsetof(SPIX_TypeDef, CH) == 0x020);
    CHECK(sizeof(SPIX_Chan) == 0x010);
    CHECK(offsetof(SPIX_TypeDef, VERSION) == 0x0FC);
    CHECK(sizeof(SPIX_TypeDef) == 0x100);

    SPIX->CH[2].CFG = 0x30u;                               /* 다른 비트가 이미 있음 */
    spix_ch_start(SPIX, 2, 0x20001000u, 64u);
    CHECK(raw_read32(SPIX, 0x020 + 0x10 * 2 + 0x8) == 0x20001000u);   /* CH[2].ADDR @ 0x048 */
    CHECK(raw_read32(SPIX, 0x020 + 0x10 * 2 + 0x4) == 64u);           /* CH[2].CNT  @ 0x044 */
    CHECK(raw_read32(SPIX, 0x020 + 0x10 * 2 + 0x0) == (0x30u | SPIX_CFG_EN));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
