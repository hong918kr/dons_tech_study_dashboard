/* 06_bit_ops.c — 비트 set/clear/toggle/test, 마스크 생성, 비트 범위 추출·삽입 (연습용 뼈대)
 *
 * 빌드: make run N=06
 *
 * TODO 가 붙은 함수만 채운다. 타입 정의와 테스트는 그대로 둔다.
 * 지금 상태로도 경고 0개로 컴파일되지만, 자리표시자가 0 을 돌려주므로
 * 첫 assert 에서 멈춘다. 그게 정상이다.
 *
 * 레벨 L0. 03_reg_bitfield 는 "MMIO 레지스터 헤더에 넣을 매크로"였고,
 * 이 문제는 그 아래 단계인 "값 하나를 비트 단위로 다루는 함수"다.
 * 레지스터도 volatile 도 나오지 않는다. 순수 함수만 다룬다.
 *
 * 이 파일이 끝까지 지키는 규칙 세 가지:
 *   1. 시프트 카운트는 항상 0..31 안에 있다 (n >= 32 는 UB).
 *   2. 왼쪽으로 미는 대상은 항상 unsigned 다 (1 << 31 은 signed overflow = UB).
 *   3. 32비트 전체 마스크는 시프트가 아니라 0xFFFFFFFFu 를 깎아서 만든다.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define U32_BITS 32u

/* bits_apply 가 수행할 연산. 실제 MCU 의 GPIO 도 BSRR/BSET/BCLR/BTOG 처럼
 * "무슨 연산을 할지"를 레지스터 주소로 고르므로, 연산을 인자로 받는 모양이
 * 임베디드 코드에서 낯설지 않다. */
typedef enum {
    BITS_SET,      /* mask 비트를 1 로 */
    BITS_CLEAR,    /* mask 비트를 0 으로 */
    BITS_TOGGLE    /* mask 비트를 반전 */
} bits_op_t;

/* bits_test 의 판정 방식 */
typedef enum {
    BITS_ALL,      /* mask 의 모든 비트가 1 인가 */
    BITS_ANY,      /* mask 중 하나라도 1 인가 */
    BITS_NONE      /* mask 중 1 이 하나도 없는가 */
} bits_match_t;

/* ------------------------------------------------------------------ */
/* 1. 마스크 만들기                                                     */
/* ------------------------------------------------------------------ */

/* bit n 하나만 1 인 32비트 마스크.
 *
 *   n = 0  -> 0x00000001
 *   n = 31 -> 0x80000000
 *
 * (uint32_t)1u 로 시작하는 것이 핵심이다. `1 << 31` 은 int 를 미는 것이라
 * signed overflow(UB)이고, `1UL << n` 은 host(x86-64, long=64bit)에서 64비트가
 * 되어 타깃(ARM32, long=32bit)과 결과 타입이 달라진다. */
static uint32_t bit_mask(unsigned n)
{
    /* TODO: bit n 하나만 1인 32비트 마스크. n >= 32 는 시프트 자체가 UB 다.
     *       1 << n 이 아니라 (uint32_t)1u << n 이어야 하는 이유를 말할 수 있어야 한다. */
    (void)n;
    return 0u;
}

/* bit hi..lo 가 연속으로 1 인 마스크. 0 <= lo <= hi <= 31.
 *
 *   mask_range(7, 4)  -> 0x000000F0
 *   mask_range(31, 0) -> 0xFFFFFFFF   <- 시프트 32 를 쓰지 않고 만든다
 *
 * 폭으로 만들고 싶으면 mask_range(shift + width - 1, shift) 를 쓴다.
 * "폭 width 마스크 = (1u << width) - 1" 은 width == 32 에서 무너진다. */
static uint32_t mask_range(unsigned hi, unsigned lo)
{
    /* TODO: bit hi..lo 가 1인 마스크. mask_range(31, 0) 이 0xFFFFFFFF 여야 한다.
     *       시프트 카운트가 32 가 되는 식은 쓰지 않는다. */
    (void)hi; (void)lo;
    return 0u;
}

/* ------------------------------------------------------------------ */
/* 2. 여러 비트 동시 조작                                                */
/* ------------------------------------------------------------------ */

/* mask 에 든 비트들에 op 를 적용한 새 값을 돌려준다. mask 밖은 그대로.
 * mask 는 여러 비트를 한 번에 담을 수 있다(bit_mask(1) | bit_mask(4) | ...). */
static uint32_t bits_apply(uint32_t v, uint32_t mask, bits_op_t op)
{
    /* TODO: mask 비트에만 op 를 적용한 새 값. mask 밖은 그대로 둔다. */
    (void)mask; (void)op;
    return v;
}

/* mask 비트를 on 에 따라 세우거나 내린다. 분기 없이 한 식으로 쓰는 관용구.
 * -(uint32_t)on 은 on 이 1 이면 0xFFFFFFFF, 0 이면 0x00000000 이다.
 * (unsigned 의 단항 마이너스는 정의되어 있다: 2^32 - x) */
static uint32_t bits_assign(uint32_t v, uint32_t mask, bool on)
{
    /* TODO: mask 비트를 on 에 따라 세우거나 내린다. if 없이 한 식으로 써 본다. */
    (void)mask; (void)on;
    return v;
}

/* ------------------------------------------------------------------ */
/* 3. 판정                                                             */
/* ------------------------------------------------------------------ */

/* mask == 0 일 때: ALL 은 true(공집합은 모두 만족), ANY 는 false, NONE 은 true.
 * 경계를 이렇게 못박아 두면 호출부에서 조건 조합이 꼬이지 않는다. */
static bool bits_test(uint32_t v, uint32_t mask, bits_match_t how)
{
    /* TODO: ALL / ANY / NONE 판정. mask == 0 일 때의 답을 먼저 정하고 시작한다. */
    (void)v; (void)mask; (void)how;
    return false;
}

/* ------------------------------------------------------------------ */
/* 4. 비트 범위 추출·삽입                                                */
/* ------------------------------------------------------------------ */

/* v 의 bit hi..lo 를 0 부터 시작하는 정수로 꺼낸다.
 *
 *   v = 0xDEADBEEF, hi = 15, lo = 8
 *   0xDEADBEEF & 0x0000FF00 = 0x0000BE00
 *   >> 8                    = 0x000000BE
 */
static uint32_t bits_extract(uint32_t v, unsigned hi, unsigned lo)
{
    /* TODO: v 의 bit hi..lo 를 0 부터 시작하는 정수로 꺼낸다. */
    (void)v; (void)hi; (void)lo;
    return 0u;
}

/* v 의 bit hi..lo 를 field 로 교체한 새 값. 폭을 넘는 상위 비트는 잘라낸다.
 * & m 을 빼면 field 의 넘친 비트가 이웃 필드를 오염시킨다. */
static uint32_t bits_insert(uint32_t v, unsigned hi, unsigned lo, uint32_t field)
{
    /* TODO: v 의 bit hi..lo 를 field 로 교체. 폭을 넘는 상위 비트는 잘라낸다. */
    (void)hi; (void)lo; (void)field;
    return v;
}

/* field 가 hi..lo 폭에 잘림 없이 들어가는가. 파라미터 검증용. */
static bool bits_fits(unsigned hi, unsigned lo, uint32_t field)
{
    /* TODO: field 가 hi..lo 폭에 잘림 없이 들어가는가. */
    (void)hi; (void)lo; (void)field;
    return true;
}

/* ------------------------------------------------------------------ */
/* 5. 테스트                                                            */
/* ------------------------------------------------------------------ */
#define OK(msg) printf("  ok  %s\n", (msg))

static void test_bit_mask(void)
{
    assert(bit_mask(0)  == 0x00000001u);
    assert(bit_mask(1)  == 0x00000002u);
    assert(bit_mask(7)  == 0x00000080u);
    assert(bit_mask(30) == 0x40000000u);
    assert(bit_mask(31) == 0x80000000u);   /* 1u 로 시작했으니 UB 없이 최상위 비트 */

    /* 모든 비트를 하나씩 세워 OR 하면 전체 마스크가 된다 */
    uint32_t all = 0u;
    for (unsigned n = 0; n < U32_BITS; n++) {
        all |= bit_mask(n);
    }
    assert(all == 0xFFFFFFFFu);
    OK("bit_mask: 0, 1, 31 경계 + 32개 OR = 0xFFFFFFFF");
}

static void test_mask_range(void)
{
    assert(mask_range(0, 0)   == 0x00000001u);
    assert(mask_range(31, 31) == 0x80000000u);
    assert(mask_range(7, 4)   == 0x000000F0u);
    assert(mask_range(19, 8)  == 0x000FFF00u);
    assert(mask_range(31, 28) == 0xF0000000u);
    assert(mask_range(31, 0)  == 0xFFFFFFFFu);   /* 시프트 32 없이 만든 전체 마스크 */
    assert(mask_range(31, 1)  == 0xFFFFFFFEu);
    assert(mask_range(30, 0)  == 0x7FFFFFFFu);

    /* 한 비트짜리 범위는 bit_mask 와 같아야 한다 */
    for (unsigned n = 0; n < U32_BITS; n++) {
        assert(mask_range(n, n) == bit_mask(n));
    }

    /* 폭 w 마스크는 mask_range(w-1, 0). w == 32 에서도 깨지지 않는다.
     * 같은 것을 (1u << w) - 1 로 만들면 w == 32 에서 UB 가 된다. */
    for (unsigned w = 1; w <= U32_BITS; w++) {
        uint32_t m = mask_range(w - 1u, 0u);
        assert(m != 0u);
        assert((m & (m + 1u)) == 0u);            /* 아래쪽이 연속으로 1 */
    }
    OK("mask_range: 단일 비트 / 임의 구간 / 폭 32 전체 마스크");
}

static void test_apply(void)
{
    uint32_t v = 0u;

    v = bits_apply(v, bit_mask(0), BITS_SET);
    assert(v == 0x00000001u);

    /* 여러 비트 동시 set */
    v = bits_apply(v, bit_mask(4) | bit_mask(8) | bit_mask(31), BITS_SET);
    assert(v == 0x80000111u);

    /* 멱등: 이미 1 인 비트를 다시 세워도 변화 없음 */
    uint32_t before = v;
    v = bits_apply(v, bit_mask(4), BITS_SET);
    assert(v == before);

    /* 여러 비트 동시 clear. mask 밖은 그대로 */
    v = bits_apply(v, bit_mask(4) | bit_mask(8), BITS_CLEAR);
    assert(v == 0x80000001u);

    /* 없는 비트를 clear 해도 변화 없음 */
    before = v;
    v = bits_apply(v, bit_mask(20), BITS_CLEAR);
    assert(v == before);

    /* toggle 두 번은 항등 */
    uint32_t m = bit_mask(3) | bit_mask(17);
    assert(bits_apply(bits_apply(v, m, BITS_TOGGLE), m, BITS_TOGGLE) == v);

    /* 전체 마스크 경계 */
    assert(bits_apply(0u, 0xFFFFFFFFu, BITS_SET) == 0xFFFFFFFFu);
    assert(bits_apply(0xFFFFFFFFu, 0xFFFFFFFFu, BITS_CLEAR) == 0u);
    assert(bits_apply(0xFFFFFFFFu, 0xFFFFFFFFu, BITS_TOGGLE) == 0u);
    assert(bits_apply(0x12345678u, 0u, BITS_SET) == 0x12345678u);   /* mask 0 = no-op */
    OK("bits_apply: set/clear/toggle, 다중 비트, 멱등, mask=0 과 전체 마스크");
}

static void test_assign(void)
{
    uint32_t m = bit_mask(2) | bit_mask(5);

    assert(bits_assign(0x00000000u, m, true)  == 0x00000024u);
    assert(bits_assign(0xFFFFFFFFu, m, false) == 0xFFFFFFDBu);

    /* bits_assign 은 set/clear 와 같은 결과여야 한다 */
    for (uint32_t v = 0; v < 64u; v++) {
        assert(bits_assign(v, m, true)  == bits_apply(v, m, BITS_SET));
        assert(bits_assign(v, m, false) == bits_apply(v, m, BITS_CLEAR));
    }
    assert(bits_assign(0x5A5A5A5Au, 0xFFFFFFFFu, true)  == 0xFFFFFFFFu);
    assert(bits_assign(0x5A5A5A5Au, 0xFFFFFFFFu, false) == 0x00000000u);
    OK("bits_assign: 분기 없는 조건 set/clear, set/clear 와 결과 일치");
}

static void test_test(void)
{
    uint32_t v = bit_mask(1) | bit_mask(4);
    uint32_t both = bit_mask(1) | bit_mask(4);
    uint32_t one_missing = bit_mask(1) | bit_mask(5);

    assert(bits_test(v, both, BITS_ALL));
    assert(!bits_test(v, one_missing, BITS_ALL));
    assert(bits_test(v, one_missing, BITS_ANY));
    assert(!bits_test(v, bit_mask(5) | bit_mask(6), BITS_ANY));
    assert(bits_test(v, bit_mask(5) | bit_mask(6), BITS_NONE));
    assert(!bits_test(v, both, BITS_NONE));

    /* mask == 0 경계: ALL/NONE 은 true, ANY 는 false */
    assert(bits_test(v, 0u, BITS_ALL));
    assert(!bits_test(v, 0u, BITS_ANY));
    assert(bits_test(v, 0u, BITS_NONE));

    /* 0 과 전체 마스크 경계 */
    assert(bits_test(0xFFFFFFFFu, 0xFFFFFFFFu, BITS_ALL));
    assert(bits_test(0x00000000u, 0xFFFFFFFFu, BITS_NONE));
    assert(bits_test(0x80000000u, bit_mask(31), BITS_ALL));
    OK("bits_test: ALL/ANY/NONE, mask=0 과 최상위 비트 경계");
}

static void test_extract_insert(void)
{
    assert(bits_extract(0xDEADBEEFu, 15, 8)  == 0xBEu);
    assert(bits_extract(0xDEADBEEFu, 31, 28) == 0xDu);
    assert(bits_extract(0xDEADBEEFu, 3, 0)   == 0xFu);
    assert(bits_extract(0xDEADBEEFu, 31, 0)  == 0xDEADBEEFu);   /* 전체 폭 */
    assert(bits_extract(0x80000000u, 31, 31) == 1u);

    /* 삽입은 다른 비트를 건드리지 않는다 */
    uint32_t v = 0xAAAA5555u;
    uint32_t w = bits_insert(v, 15, 8, 0x3Cu);
    assert(bits_extract(w, 15, 8) == 0x3Cu);
    assert((w & ~mask_range(15, 8)) == (v & ~mask_range(15, 8)));  /* 나머지 보존 */
    assert(w == 0xAAAA3C55u);

    /* 추출 -> 삽입 왕복 */
    for (uint32_t f = 0; f < 256u; f++) {
        uint32_t t = bits_insert(0x12345678u, 23, 16, f);
        assert(bits_extract(t, 23, 16) == f);
    }

    /* 폭을 넘는 값은 잘린다. 이웃 필드는 오염되지 않는다 */
    uint32_t base = 0x00000000u;
    uint32_t t = bits_insert(base, 11, 8, 0x1FFu);      /* 4비트 자리에 9비트 값 */
    assert(bits_extract(t, 11, 8) == 0xFu);
    assert(t == 0x00000F00u);                            /* bit 12 이상 깨끗 */
    assert(!bits_fits(11, 8, 0x1FFu));
    assert(bits_fits(11, 8, 0xFu));

    /* 최상위 경계: bit 31 한 칸, 그리고 상위 끝에 붙은 필드 */
    assert(bits_insert(0u, 31, 31, 1u) == 0x80000000u);
    assert(bits_insert(0u, 31, 31, 3u) == 0x80000000u);  /* 잘림 */
    assert(bits_insert(0xFFFFFFFFu, 31, 24, 0x00u) == 0x00FFFFFFu);
    assert(bits_insert(0u, 31, 0, 0xFFFFFFFFu) == 0xFFFFFFFFu);
    OK("bits_extract / bits_insert: 왕복, 이웃 보존, 절단, 31비트 경계");
}

/* 실전 모양: 한 워드에 여러 필드를 패킹했다가 다시 꺼낸다.
 * (오디오 스트림 디스크립터를 흉내 낸 예시 레이아웃이다)
 *
 *  31    28 27      16 15   12 11        4  3  2  1  0
 * +--------+----------+-------+-----------+--+--+--+--+
 * | CHAN   |  RATE_ID | BITS  |  LEN      |ER|EN|MU|ST|
 * +--------+----------+-------+-----------+--+--+--+--+
 */
#define DESC_ST      0u
#define DESC_MUTE    1u
#define DESC_EN      2u
#define DESC_ERR     3u

static void test_packed_word(void)
{
    uint32_t d = 0u;

    d = bits_insert(d, 11, 4, 0xC0u);        /* LEN   = 192 */
    d = bits_insert(d, 15, 12, 4u);          /* BITS  = 4 (=24bit 코드) */
    d = bits_insert(d, 27, 16, 0x123u);      /* RATE  = 0x123 */
    d = bits_insert(d, 31, 28, 2u);          /* CHAN  = 2 */
    d = bits_apply(d, bit_mask(DESC_EN) | bit_mask(DESC_ST), BITS_SET);

    assert(d == 0x21234C05u);
    assert(bits_extract(d, 11, 4)  == 0xC0u);
    assert(bits_extract(d, 15, 12) == 4u);
    assert(bits_extract(d, 27, 16) == 0x123u);
    assert(bits_extract(d, 31, 28) == 2u);
    assert(bits_test(d, bit_mask(DESC_EN), BITS_ALL));
    assert(bits_test(d, bit_mask(DESC_ERR) | bit_mask(DESC_MUTE), BITS_NONE));

    /* LEN 만 바꿔도 다른 필드/플래그는 전부 살아 있다 */
    uint32_t d_len1 = bits_insert(d, 11, 4, 0x01u);
    assert(d_len1 == 0x21234015u);
    assert(bits_extract(d_len1, 11, 4) == 0x01u);
    assert(bits_extract(d_len1, 15, 12) == 4u);
    assert(bits_extract(d_len1, 27, 16) == 0x123u);
    assert(bits_extract(d_len1, 31, 28) == 2u);
    assert((d_len1 & 0x0Fu) == (d & 0x0Fu));

    /* MUTE 플래그만 조건부로 켜고 끄면 나머지는 원래 값으로 돌아온다 */
    uint32_t d2 = bits_assign(d_len1, bit_mask(DESC_MUTE), true);
    assert(bits_test(d2, bit_mask(DESC_MUTE), BITS_ALL));
    assert(bits_extract(d2, 11, 4) == 0x01u);
    d2 = bits_assign(d2, bit_mask(DESC_MUTE), false);
    assert(bits_test(d2, bit_mask(DESC_MUTE), BITS_NONE));
    assert(d2 == d_len1);
    OK("패킹된 워드: 필드 4개 + 플래그 4개를 서로 오염 없이 다룬다");
}

int main(void)
{
    printf("06_bit_ops (starter)\n");
    test_bit_mask();
    test_mask_range();
    test_apply();
    test_assign();
    test_test();
    test_extract_insert();
    test_packed_word();
    printf("ALL TESTS PASSED\n");
    return 0;
}
