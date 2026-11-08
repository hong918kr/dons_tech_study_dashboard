/* C-B04 비트 필드 추출·삽입: Mask와 Shift로 GET/SET_FIELD
 * 마스크 하나만 들고 필드를 읽고(FIELD_GET) 만들고(FIELD_PREP) 바꾼다(FIELD_REPLACE).
 * shift 는 따로 받지 않고 마스크의 "가장 낮은 1비트"에서 유도한다 — Linux <linux/bitfield.h> 스타일.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* ---------------- Part A: GENMASK 와 마스크 전용 FIELD 매크로 ---------------- */
/* 비트 h..l (양끝 포함) 이 1인 32비트 마스크. 전제: 0 <= l <= h <= 31 */
#define GENMASK(h, l)         ((0xFFFFFFFFu << (l)) & (0xFFFFFFFFu >> (31u - (h))))

/* 마스크의 가장 낮은 1비트만 남긴 값 (= 1u << shift). 예: 0x70 -> 0x10 */
#define FIELD_LSB(mask)       ((uint32_t)(mask) & (~(uint32_t)(mask) + 1u))

/* 필드 값 꺼내기: (reg & mask) >> shift. 2의 거듭제곱으로 나누기 == 오른쪽 시프트 */
#define FIELD_GET(mask, reg)  (((uint32_t)(reg) & (uint32_t)(mask)) / FIELD_LSB(mask))

/* 필드 자리에 놓을 값 만들기: (val << shift) & mask. 넘친 비트는 & mask 로 잘린다 */
#define FIELD_PREP(mask, val) (((uint32_t)(val) * FIELD_LSB(mask)) & (uint32_t)(mask))

/* reg 의 그 필드만 val 로 바꾼 새 값 (clear → set, C-B01 field_set 의 마스크 버전) */
#define FIELD_REPLACE(mask, reg, val) (((uint32_t)(reg) & ~(uint32_t)(mask)) | FIELD_PREP(mask, val))

/* ---------------- Part B: 검사용 매크로 (런타임 + 컴파일타임) ---------------- */
/* 필드가 담을 수 있는 최댓값. 예: 0x70 -> 0x7 */
#define FIELD_MAX(mask)       ((uint32_t)(mask) / FIELD_LSB(mask))
/* 마스크가 0이 아니고 1이 끊김 없이 이어지는가? (연속이면 mask + LSB 가 필드 위로 올림되어 mask 와 안 겹침) */
#define FIELD_MASK_OK(mask)   ((uint32_t)(mask) != 0u && \
                               (((uint32_t)(mask) + FIELD_LSB(mask)) & (uint32_t)(mask)) == 0u)
/* val 이 필드 폭에 들어가는가? */
#define FIELD_FIT(mask, val)  ((uint32_t)(val) <= FIELD_MAX(mask))

/* 상수 전용 FIELD_PREP: mask 와 val 이 상수식이면 컴파일타임에 검사한다.
 * _Static_assert 는 "선언"이라 식 안에 바로 못 쓴다 → 익명 struct 멤버 자리에 넣고 sizeof 로 감싼다
 * (C11 6.7.2.1: struct-declaration 에 static_assert-declaration 허용). sizeof 결과에 0을 곱해 값엔 영향 없음. */
#define FIELD_PREP_CONST(mask, val) ((uint32_t)(FIELD_PREP(mask, val) + 0u * sizeof(struct { \
    _Static_assert(FIELD_MASK_OK(mask), "FIELD_PREP_CONST: mask is zero or not contiguous");  \
    _Static_assert(!FIELD_MASK_OK(mask) || FIELD_FIT(mask, val),                              \
                   "FIELD_PREP_CONST: value too large for field");                            \
    char dummy; })))

/* ---------------- Part C: 가짜 타이머 CTRL 레지스터 ---------------- */
typedef struct {
    volatile uint32_t CTRL;
    volatile uint32_t CNT;
} TIM_TypeDef;

static TIM_TypeDef fake_tim;                 /* 실제 칩이라면 (TIM_TypeDef *)0x40000000 */
#define TIM2 (&fake_tim)

/* CMSIS 스타일: <주변장치>_<레지스터>_<필드>_Pos / _Msk */
#define TIM_CTRL_EN_Pos     0u
#define TIM_CTRL_EN_Msk     (0x1u  << TIM_CTRL_EN_Pos)     /* bit 0       카운터 enable   */
#define TIM_CTRL_MODE_Pos   1u
#define TIM_CTRL_MODE_Msk   (0x7u  << TIM_CTRL_MODE_Pos)   /* bits 3:1    동작 모드       */
#define TIM_CTRL_PRESC_Pos  4u
#define TIM_CTRL_PRESC_Msk  (0xFu  << TIM_CTRL_PRESC_Pos)  /* bits 7:4    분주 2^PRESC    */
#define TIM_CTRL_IRQEN_Pos  8u
#define TIM_CTRL_IRQEN_Msk  (0x1u  << TIM_CTRL_IRQEN_Pos)  /* bit 8       인터럽트 enable */
#define TIM_CTRL_DTIME_Pos  16u
#define TIM_CTRL_DTIME_Msk  (0xFFu << TIM_CTRL_DTIME_Pos)  /* bits 23:16  데드타임        */
#define TIM_CTRL_RSVD_Msk   0xFF00FE00u                    /* 15:9, 31:24 예약 — 읽은 값 그대로 써야 함 */

enum { TIM_MODE_ONESHOT = 0, TIM_MODE_PERIODIC = 1, TIM_MODE_PWM = 2, TIM_MODE_CAPTURE = 3 }; /* 4..7 예약 */

#define STATIC_CHECKS 1   /* TODO(Part B): 매크로를 다 채운 뒤 1 로 바꿔 컴파일타임 검사를 켠다 */
#if STATIC_CHECKS
_Static_assert(TIM_CTRL_MODE_Msk  == GENMASK(3, 1),   "MODE must be bits 3:1");
_Static_assert(TIM_CTRL_PRESC_Msk == GENMASK(7, 4),   "PRESC must be bits 7:4");
_Static_assert(TIM_CTRL_DTIME_Msk == GENMASK(23, 16), "DTIME must be bits 23:16");
_Static_assert(FIELD_MASK_OK(TIM_CTRL_MODE_Msk) && FIELD_MASK_OK(TIM_CTRL_PRESC_Msk) &&
               FIELD_MASK_OK(TIM_CTRL_DTIME_Msk), "field mask not contiguous");
_Static_assert(FIELD_MAX(TIM_CTRL_MODE_Msk) >= TIM_MODE_CAPTURE, "MODE field too narrow");
/* 겹침 없음: 합(64비트로 더해 랩어라운드 방지) == OR.  빈틈 없음: OR == 0xFFFFFFFF */
#define TIM_CTRL_SUM ((uint64_t)TIM_CTRL_EN_Msk + TIM_CTRL_MODE_Msk + TIM_CTRL_PRESC_Msk + \
                      TIM_CTRL_IRQEN_Msk + TIM_CTRL_DTIME_Msk + TIM_CTRL_RSVD_Msk)
#define TIM_CTRL_OR  (TIM_CTRL_EN_Msk | TIM_CTRL_MODE_Msk | TIM_CTRL_PRESC_Msk | \
                      TIM_CTRL_IRQEN_Msk | TIM_CTRL_DTIME_Msk | TIM_CTRL_RSVD_Msk)
_Static_assert(TIM_CTRL_SUM == TIM_CTRL_OR, "CTRL fields overlap");
_Static_assert(TIM_CTRL_OR == 0xFFFFFFFFu,  "CTRL map has a gap");
#endif

/* 리셋 후 기본 설정 (periodic, presc=3) — 상수식이라 컴파일타임에 검사된다 */
#define TIM_CTRL_DEFAULT (FIELD_PREP_CONST(TIM_CTRL_MODE_Msk, TIM_MODE_PERIODIC) | \
                          FIELD_PREP_CONST(TIM_CTRL_PRESC_Msk, 3u))

static bool tim_set_mode(TIM_TypeDef *t, uint32_t mode) {
    if (mode > TIM_MODE_CAPTURE) return false;                 /* 예약 모드 거부 */
    t->CTRL = FIELD_REPLACE(TIM_CTRL_MODE_Msk, t->CTRL, mode);
    return true;
}
static bool tim_set_presc(TIM_TypeDef *t, uint32_t presc) {
    if (!FIELD_FIT(TIM_CTRL_PRESC_Msk, presc)) return false;  /* 잘라서 쓰지 말고 거부 */
    t->CTRL = FIELD_REPLACE(TIM_CTRL_PRESC_Msk, t->CTRL, presc);
    return true;
}
static uint32_t tim_get_presc(const TIM_TypeDef *t) {
    return FIELD_GET(TIM_CTRL_PRESC_Msk, t->CTRL);
}
/* 여러 필드를 "읽기 1번 + 쓰기 1번" 으로 바꾼다. 인자가 하나라도 틀리면 아무것도 안 쓴다. */
static bool tim_configure(TIM_TypeDef *t, uint32_t mode, uint32_t presc, uint32_t dtime, bool irq) {
    if (mode > TIM_MODE_CAPTURE || !FIELD_FIT(TIM_CTRL_PRESC_Msk, presc) ||
        !FIELD_FIT(TIM_CTRL_DTIME_Msk, dtime)) return false;
    const uint32_t m = TIM_CTRL_MODE_Msk | TIM_CTRL_PRESC_Msk | TIM_CTRL_DTIME_Msk | TIM_CTRL_IRQEN_Msk;
    uint32_t v = t->CTRL & ~m;                                 /* 읽기 1번 + 대상 필드 clear */
    v |= FIELD_PREP(TIM_CTRL_MODE_Msk, mode) | FIELD_PREP(TIM_CTRL_PRESC_Msk, presc) |
         FIELD_PREP(TIM_CTRL_DTIME_Msk, dtime) | FIELD_PREP(TIM_CTRL_IRQEN_Msk, irq);
    t->CTRL = v;                                               /* 쓰기 1번 */
    return true;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* 연속 마스크 528개 전부: GENMASK/GET/PREP 를 shift 기반 참조 구현(__builtin_ctz)과 대조 */
static bool exhaustive_ok(void) {
    static const uint32_t pat[] = { 0u, 0xFFFFFFFFu, 0xA5A5A5A5u, 0x12345678u };
    for (unsigned h = 0; h < 32; h++) {
        for (unsigned l = 0; l <= h; l++) {
            uint32_t ref = 0;
            for (unsigned b = l; b <= h; b++) ref |= 1u << b;
            if (GENMASK(h, l) != ref) return false;
            unsigned s = (unsigned)__builtin_ctz(ref);
            for (unsigned i = 0; i < 4; i++) {
                if (FIELD_GET(ref, pat[i]) != ((pat[i] & ref) >> s)) return false;
                if (FIELD_PREP(ref, pat[i]) != ((pat[i] << s) & ref)) return false;
            }
        }
    }
    return true;
}

/* 표의 한 줄: 여러 시작값에서 필드를 바꿔도 (1) 그 필드는 val 이 되고 (2) 다른 비트는 그대로 */
static bool row_ok(uint32_t mask, uint32_t val) {
    static const uint32_t starts[] = { 0x00000000u, 0xFFFFFFFFu, 0xA5A5A5A5u, 0x5A5A5A5Au, 0x12345678u };
    for (unsigned i = 0; i < sizeof starts / sizeof starts[0]; i++) {
        uint32_t n = FIELD_REPLACE(mask, starts[i], val);
        if (FIELD_GET(mask, n) != val || ((n ^ starts[i]) & ~mask) != 0u) {
            printf("  mask=0x%08X val=0x%X start=0x%08X -> 0x%08X\n",
                   (unsigned)mask, (unsigned)val, (unsigned)starts[i], (unsigned)n);
            return false;
        }
    }
    return true;
}

int main(void) {
    /* Part A */
    CHECK(GENMASK(3, 1) == 0x0000000Eu);
    CHECK(GENMASK(7, 4) == 0x000000F0u);
    CHECK(GENMASK(31, 28) == 0xF0000000u);
    CHECK(GENMASK(31, 0) == 0xFFFFFFFFu);
    CHECK(GENMASK(0, 0) == 0x00000001u);
    CHECK(FIELD_LSB(0x70u) == 0x10u);
    CHECK(FIELD_LSB(0x80000000u) == 0x80000000u);
    CHECK(FIELD_GET(0x0000FF00u, 0xABCD1234u) == 0x12u);
    CHECK(FIELD_GET(0xF0000000u, 0xABCD1234u) == 0xAu);
    CHECK(FIELD_GET(0xFFFFFFFFu, 0xABCD1234u) == 0xABCD1234u);
    CHECK(FIELD_PREP(0x000000F0u, 0xAu) == 0x000000A0u);
    CHECK(FIELD_PREP(0x000000F0u, 0x1Fu) == 0x000000F0u);      /* 넘친 값은 잘린다 */
    CHECK(FIELD_PREP(0x80000000u, 1u) == 0x80000000u);
    CHECK(FIELD_REPLACE(0x000000F0u, 0x12345678u, 0xAu) == 0x123456A8u);
    CHECK(exhaustive_ok());

    /* Part B */
    CHECK(FIELD_MAX(0x70u) == 7u);
    CHECK(FIELD_MAX(0xFFFFFFFFu) == 0xFFFFFFFFu);
    CHECK(FIELD_MASK_OK(0x70u));
    CHECK(FIELD_MASK_OK(0x80000000u));
    CHECK(FIELD_MASK_OK(0xF0000000u));
    CHECK(FIELD_MASK_OK(0xFFFFFFFFu));
    CHECK(!FIELD_MASK_OK(0x50u));                               /* 0101_0000: 구멍 */
    CHECK(!FIELD_MASK_OK(0x80000001u));
    CHECK(!FIELD_MASK_OK(0u));
    CHECK(FIELD_FIT(0x70u, 7u));
    CHECK(!FIELD_FIT(0x70u, 8u));
    CHECK(FIELD_FIT(0xFFFFFFFFu, 0xFFFFFFFFu));
    CHECK(TIM_CTRL_DEFAULT == 0x00000032u);

    /* Part C — 표 테스트: 필드 하나 바꿔도 나머지 비트는 그대로 */
    static const struct { uint32_t mask, val; } rows[] = {
        { TIM_CTRL_EN_Msk, 1u },      { TIM_CTRL_EN_Msk, 0u },
        { TIM_CTRL_MODE_Msk, 2u },    { TIM_CTRL_MODE_Msk, 7u },    { TIM_CTRL_MODE_Msk, 0u },
        { TIM_CTRL_PRESC_Msk, 0xFu }, { TIM_CTRL_PRESC_Msk, 0x9u },
        { TIM_CTRL_IRQEN_Msk, 1u },
        { TIM_CTRL_DTIME_Msk, 0xA5u }, { TIM_CTRL_DTIME_Msk, 0x00u },
    };
    for (unsigned i = 0; i < sizeof rows / sizeof rows[0]; i++)
        CHECK(row_ok(rows[i].mask, rows[i].val));

    /* Part C — 드라이버 */
    TIM2->CTRL = TIM_CTRL_RSVD_Msk;                             /* 예약 비트만 1인 리셋값이라고 치자 */
    CHECK(tim_configure(TIM2, TIM_MODE_PWM, 9u, 0x40u, true) == true);
    CHECK(TIM2->CTRL == 0xFF40FF94u);
    CHECK(tim_get_presc(TIM2) == 9u);
    uint32_t before = TIM2->CTRL;
    CHECK(tim_configure(TIM2, 5u, 1u, 0u, false) == false);     /* MODE 5 = 예약 → 거부 */
    CHECK(tim_set_presc(TIM2, 16u) == false);                   /* 4비트 필드에 16 → 거부 */
    CHECK(TIM2->CTRL == before);                                /* 거부했으면 레지스터는 그대로 */
    CHECK(tim_set_mode(TIM2, TIM_MODE_CAPTURE) == true);
    CHECK(TIM2->CTRL == ((before & ~TIM_CTRL_MODE_Msk) | (3u << TIM_CTRL_MODE_Pos)));
    CHECK(tim_set_presc(TIM2, 0u) == true && tim_get_presc(TIM2) == 0u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
