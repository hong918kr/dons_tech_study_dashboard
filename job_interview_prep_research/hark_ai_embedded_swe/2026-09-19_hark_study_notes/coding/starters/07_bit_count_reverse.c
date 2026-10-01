/* 07_bit_count_reverse.c — popcount 3가지, 비트 반전, ffs/fls, 2의 거듭제곱 (연습용 뼈대)
 *
 * 빌드: make run N=07
 *
 * TODO 가 붙은 함수만 채운다. 테스트와 난수 헬퍼는 그대로 둔다.
 * __builtin_popcount / __builtin_clz 는 쓰지 않는다.
 *
 * 레벨 L0. 전부 컴파일러 내장 함수(__builtin_popcount, __builtin_clz)를 쓰지 않고
 * 손으로 구현한다. 면접에서 "내장 함수 쓰면 되죠"는 답이 아니고,
 * 타깃 컴파일러에 그 명령이 없을 수도 있다(Cortex-M0 는 CLZ 가 없다).
 *
 * 규칙: 시프트 카운트는 항상 0..31, 미는 대상은 항상 unsigned.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define U32_BITS 32u

/* ------------------------------------------------------------------ */
/* 1. popcount — 세 가지 구현                                           */
/* ------------------------------------------------------------------ */

/* (a) 순진한 방법: 32번 돌면서 최하위 비트를 본다.
 * 항상 정확히 32회. 입력과 무관하게 실행 시간이 일정하다(상수 시간).
 * 암호 키처럼 타이밍이 새면 안 되는 값에는 오히려 이게 맞다. */
static unsigned popcount_naive(uint32_t x)
{
    /* TODO: 32번 돌면서 최하위 비트를 센다. 입력과 무관하게 항상 32회. */
    (void)x;
    return 0u;
}

/* (b) Kernighan: x &= x - 1 은 "가장 낮은 1 비트 하나를 지운다".
 *
 *   x     = 0b0101_1000
 *   x - 1 = 0b0101_0111      <- 최하위 1이 0이 되고 그 아래가 모두 1
 *   &     = 0b0101_0000      <- 최하위 1 하나만 사라졌다
 *
 * 세워진 비트 수만큼만 돈다. 희소한 비트맵(인터럽트 pending 마스크)에 최적.
 * 반면 실행 시간이 입력에 따라 달라진다. */
static unsigned popcount_kernighan(uint32_t x)
{
    /* TODO: x &= x - 1 이 무엇을 하는지 먼저 손으로 그려 본다. */
    (void)x;
    return 0u;
}

/* (c) 테이블: 니블(4비트) 16바이트 테이블로 8번 조회.
 * 바이트 테이블(256바이트)로 4번 조회하는 변형도 있는데,
 * flash 가 아까운 MCU 에서는 16바이트가 낫다. 분기가 없어 파이프라인에 좋다. */
static unsigned popcount_table(uint32_t x)
{
    /* TODO: 니블 테이블로 8번 조회한다. 테이블은 아래 값을 그대로 쓴다. */
    static const uint8_t nibble[16] = {
        0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4
    };
    (void)nibble; (void)x;
    return 0u;
}

/* (d) 참고: SWAR(분할 병합). 루프도 테이블도 없다.
 * 2비트씩 세고, 4비트로 합치고, 8비트로 합친 뒤 곱셈 한 번으로 모아 읽는다. */
static unsigned popcount_swar(uint32_t x)
{
    /* TODO: 2 -> 4 -> 8비트 단위로 접어 올린 뒤 곱셈 한 번으로 모아 읽는다.
     *       막히면 이 함수는 마지막에 해도 된다. */
    (void)x;
    return 0u;
}

/* ------------------------------------------------------------------ */
/* 2. 비트 반전                                                         */
/* ------------------------------------------------------------------ */

/* 8비트 반전. LSB-first 로 나가는 SPI/CRC 와 MSB-first 데이터를 맞출 때 쓴다.
 * uint8_t 는 연산 중 int 로 승격되므로 uint32_t 로 받아 계산하고 다시 좁힌다. */
static uint8_t bit_reverse8(uint8_t x)
{
    /* TODO: 1 -> 2 -> 4 비트 쌍을 차례로 교환한다. */
    (void)x;
    return 0u;
}

/* 32비트 반전. bit 0 <-> bit 31, bit 1 <-> bit 30 ...
 * 1 -> 2 -> 4 -> 8 -> 16 다섯 단계로 log2(32) 번만 돈다. */
static uint32_t bit_reverse32(uint32_t x)
{
    /* TODO: 1 -> 2 -> 4 -> 8 -> 16 다섯 단계. 마스크를 직접 적어 본다. */
    (void)x;
    return 0u;
}

/* ------------------------------------------------------------------ */
/* 3. find first / last set                                            */
/* ------------------------------------------------------------------ */

/* 가장 낮은 1 비트의 인덱스(0 기반). x == 0 이면 -1.
 * 0 을 "없음"으로 표현하는 방식을 API 에 못박는 것이 중요하다.
 * POSIX ffs() 는 1 기반이고 0 을 "없음"으로 쓴다 — 헷갈리기 쉬운 지점이다.
 *
 * 절반씩 좁히는 이분 탐색이라 항상 5번만 비교한다. */
static int find_first_set(uint32_t x)
{
    /* TODO: 가장 낮은 1 비트의 인덱스(0 기반). x == 0 이면 -1. */
    (void)x;
    return -1;
}

/* 가장 높은 1 비트의 인덱스(0 기반). x == 0 이면 -1.
 * 31 - find_last_set(x) 가 곧 CLZ(leading zero count)다. */
static int find_last_set(uint32_t x)
{
    /* TODO: 가장 높은 1 비트의 인덱스(0 기반). x == 0 이면 -1. */
    (void)x;
    return -1;
}

/* 가장 낮은 1 비트만 남긴 값. x & -x 관용구.
 * -x 는 unsigned 에서 2^32 - x 로 정의되어 있으므로 UB 가 아니다. */
static uint32_t lowest_set_bit(uint32_t x)
{
    /* TODO: 가장 낮은 1 비트만 남긴 값. 두 연산자로 끝난다. */
    (void)x;
    return 0u;
}

/* ------------------------------------------------------------------ */
/* 4. 2의 거듭제곱                                                      */
/* ------------------------------------------------------------------ */

/* x 가 2의 거듭제곱인가. 0 은 아니다.
 * 2의 거듭제곱은 1 비트만 세워져 있으므로 x & (x-1) == 0.
 * x != 0 검사를 빼면 0 이 통과해 버린다 — 링버퍼 크기 검증에서 자주 나는 버그. */
static bool is_power_of_two(uint32_t x)
{
    /* TODO: 0 은 2의 거듭제곱이 아니라는 것을 빠뜨리지 않는다. */
    (void)x;
    return false;
}

/* x 이상인 가장 작은 2의 거듭제곱.
 *   0 -> 1, 1 -> 1, 2 -> 2, 3 -> 4, 0x80000000 -> 0x80000000
 *   0x80000001 이상은 32비트에 담기지 않으므로 0 을 돌려 "불가"를 알린다.
 *
 * 최상위 1 비트 아래를 전부 1 로 채우고(smear) 1 을 더한다.
 *   x-1 = 0b0100_1001
 *   |= >>1 ... >>16  -> 0b0111_1111
 *   +1               -> 0b1000_0000
 */
static uint32_t next_power_of_two(uint32_t x)
{
    /* TODO: x 이상인 가장 작은 2의 거듭제곱.
     *       0 -> 1, 이미 거듭제곱이면 그대로, 32비트에 안 담기면 0. */
    (void)x;
    return 0u;
}

/* ------------------------------------------------------------------ */
/* 5. 테스트                                                            */
/* ------------------------------------------------------------------ */
#define OK(msg) printf("  ok  %s\n", (msg))

/* 테스트용 결정론적 난수 (LCG). 표준 rand() 는 구현마다 달라 재현이 안 된다. */
static uint32_t lcg_next(uint32_t *s)
{
    *s = (*s * 1664525u) + 1013904223u;
    return *s;
}

static void test_popcount_boundaries(void)
{
    assert(popcount_naive(0x00000000u) == 0u);
    assert(popcount_naive(0x00000001u) == 1u);
    assert(popcount_naive(0x80000000u) == 1u);
    assert(popcount_naive(0xFFFFFFFFu) == 32u);
    assert(popcount_naive(0x55555555u) == 16u);
    assert(popcount_naive(0xAAAAAAAAu) == 16u);
    assert(popcount_naive(0x0000FFFFu) == 16u);
    assert(popcount_naive(0xDEADBEEFu) == 24u);
    OK("popcount 경계값: 0, 1, 최상위 비트, 전체 1, 교대 패턴");
}

static void test_popcount_agreement(void)
{
    /* 네 구현이 모든 경계값에서 일치 */
    static const uint32_t v[] = {
        0x00000000u, 0x00000001u, 0x00000002u, 0x00000003u, 0x7FFFFFFFu,
        0x80000000u, 0x80000001u, 0xFFFFFFFEu, 0xFFFFFFFFu,
        0x0F0F0F0Fu, 0xF0F0F0F0u, 0x00FF00FFu, 0x12345678u
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        unsigned a = popcount_naive(v[i]);
        assert(popcount_kernighan(v[i]) == a);
        assert(popcount_table(v[i]) == a);
        assert(popcount_swar(v[i]) == a);
    }

    /* 단일 비트 32개 전부 */
    for (unsigned n = 0; n < U32_BITS; n++) {
        uint32_t b = (uint32_t)1u << n;
        assert(popcount_naive(b) == 1u);
        assert(popcount_kernighan(b) == 1u);
        assert(popcount_table(b) == 1u);
        assert(popcount_swar(b) == 1u);
    }

    /* 난수 2000개 교차 검증 */
    uint32_t s = 0x12345678u;
    for (int i = 0; i < 2000; i++) {
        uint32_t x = lcg_next(&s);
        unsigned a = popcount_naive(x);
        assert(popcount_kernighan(x) == a);
        assert(popcount_table(x) == a);
        assert(popcount_swar(x) == a);
    }
    OK("popcount naive / Kernighan / table / SWAR 네 구현이 항상 일치");
}

static void test_reverse(void)
{
    assert(bit_reverse8(0x00u) == 0x00u);
    assert(bit_reverse8(0xFFu) == 0xFFu);
    assert(bit_reverse8(0x01u) == 0x80u);
    assert(bit_reverse8(0x80u) == 0x01u);
    assert(bit_reverse8(0x0Fu) == 0xF0u);
    assert(bit_reverse8(0xA5u) == 0xA5u);      /* 0b10100101 은 회문 */
    assert(bit_reverse8(0x13u) == 0xC8u);      /* 0b00010011 -> 0b11001000 */

    assert(bit_reverse32(0x00000000u) == 0x00000000u);
    assert(bit_reverse32(0xFFFFFFFFu) == 0xFFFFFFFFu);
    assert(bit_reverse32(0x00000001u) == 0x80000000u);
    assert(bit_reverse32(0x80000000u) == 0x00000001u);
    assert(bit_reverse32(0x12345678u) == 0x1E6A2C48u);
    assert(bit_reverse32(0x55555555u) == 0xAAAAAAAAu);

    /* 두 번 반전은 항등. 반전은 popcount 를 보존한다. */
    uint32_t s = 0xC0FFEEu;
    for (int i = 0; i < 2000; i++) {
        uint32_t x = lcg_next(&s);
        assert(bit_reverse32(bit_reverse32(x)) == x);
        assert(popcount_naive(bit_reverse32(x)) == popcount_naive(x));
        uint8_t b = (uint8_t)(x & 0xFFu);
        assert(bit_reverse8(bit_reverse8(b)) == b);
    }

    /* 비트 i 하나만 세워 반전하면 비트 31-i 하나만 세워진다 */
    for (unsigned n = 0; n < U32_BITS; n++) {
        assert(bit_reverse32((uint32_t)1u << n) == ((uint32_t)1u << (31u - n)));
    }
    OK("bit_reverse8 / bit_reverse32: 회문·왕복·단일 비트 32개·popcount 보존");
}

static void test_ffs_fls(void)
{
    assert(find_first_set(0u) == -1);
    assert(find_last_set(0u) == -1);

    assert(find_first_set(0x00000001u) == 0);
    assert(find_last_set(0x00000001u) == 0);
    assert(find_first_set(0x80000000u) == 31);
    assert(find_last_set(0x80000000u) == 31);
    assert(find_first_set(0xFFFFFFFFu) == 0);
    assert(find_last_set(0xFFFFFFFFu) == 31);
    assert(find_first_set(0x00010000u) == 16);
    assert(find_last_set(0x00010000u) == 16);
    assert(find_first_set(0xDEADBE00u) == 9);      /* 0xBE00 의 최하위 1 */
    assert(find_last_set(0x7FFFFFFFu) == 30);

    /* 단일 비트 32개: ffs == fls == n */
    for (unsigned n = 0; n < U32_BITS; n++) {
        uint32_t b = (uint32_t)1u << n;
        assert(find_first_set(b) == (int)n);
        assert(find_last_set(b) == (int)n);
        assert(lowest_set_bit(b) == b);
    }

    /* 불변식: lowest_set_bit(x) == 1u << find_first_set(x),
     *         ffs <= fls,  fls - ffs + 1 >= popcount */
    uint32_t s = 0xBEEF0001u;
    for (int i = 0; i < 2000; i++) {
        uint32_t x = lcg_next(&s);
        if (x == 0u) {
            continue;
        }
        int lo = find_first_set(x);
        int hi = find_last_set(x);
        assert(lo >= 0 && hi >= 0 && lo <= hi);
        assert(lowest_set_bit(x) == ((uint32_t)1u << lo));
        assert((x >> hi) == 1u);                                /* 위쪽은 비어 있다 */
        assert(popcount_naive(x) <= (unsigned)(hi - lo + 1));
    }

    /* Kernighan 루프와 ffs 로 세워진 비트를 순서대로 훑는 관용구 */
    uint32_t pending = 0x00001048u;             /* bit 3, 6, 12 */
    int seen[3];
    int k = 0;
    while (pending != 0u) {
        int b = find_first_set(pending);
        assert(k < 3);
        seen[k++] = b;
        pending &= pending - 1u;                 /* 그 비트만 지운다 */
    }
    assert(k == 3 && seen[0] == 3 && seen[1] == 6 && seen[2] == 12);
    OK("find_first_set / find_last_set: 0 처리, 31비트 경계, pending 마스크 순회");
}

static void test_power_of_two(void)
{
    assert(!is_power_of_two(0u));                 /* 0 은 2의 거듭제곱이 아니다 */
    assert(is_power_of_two(1u));
    assert(is_power_of_two(2u));
    assert(!is_power_of_two(3u));
    assert(is_power_of_two(0x80000000u));         /* 2^31 */
    assert(!is_power_of_two(0x80000001u));
    assert(!is_power_of_two(0xFFFFFFFFu));

    for (unsigned n = 0; n < U32_BITS; n++) {
        assert(is_power_of_two((uint32_t)1u << n));
    }
    /* popcount 로 다시 확인 */
    uint32_t s = 0x2A2A2A2Au;
    for (int i = 0; i < 2000; i++) {
        uint32_t x = lcg_next(&s);
        assert(is_power_of_two(x) == (popcount_naive(x) == 1u));
    }
    OK("is_power_of_two: 0 배제, 1, 2^31, popcount 와 일치");
}

static void test_next_power_of_two(void)
{
    assert(next_power_of_two(0u) == 1u);
    assert(next_power_of_two(1u) == 1u);
    assert(next_power_of_two(2u) == 2u);
    assert(next_power_of_two(3u) == 4u);
    assert(next_power_of_two(4u) == 4u);
    assert(next_power_of_two(5u) == 8u);
    assert(next_power_of_two(1023u) == 1024u);
    assert(next_power_of_two(1024u) == 1024u);
    assert(next_power_of_two(1025u) == 2048u);
    assert(next_power_of_two(0x7FFFFFFFu) == 0x80000000u);
    assert(next_power_of_two(0x80000000u) == 0x80000000u);  /* 이미 2의 거듭제곱 */
    assert(next_power_of_two(0x80000001u) == 0u);           /* 오버플로: 표현 불가 */
    assert(next_power_of_two(0xFFFFFFFFu) == 0u);

    /* 2^n 경계 주변을 전부 확인. n = 1 은 p - 1 == 1 이 이미 2의 거듭제곱이라 제외. */
    for (unsigned n = 2; n < 31u; n++) {
        uint32_t p = (uint32_t)1u << n;
        assert(next_power_of_two(p) == p);
        assert(next_power_of_two(p - 1u) == p);
        assert(next_power_of_two(p + 1u) == (p << 1));
    }

    /* 결과는 항상 2의 거듭제곱이고 x 이상이며, 그보다 작은 거듭제곱은 x 미만이다 */
    uint32_t s = 0x9E3779B9u;
    for (int i = 0; i < 2000; i++) {
        uint32_t x = lcg_next(&s) & 0x3FFFFFFFu;    /* 오버플로 영역은 뺀다 */
        uint32_t p = next_power_of_two(x);
        assert(is_power_of_two(p));
        assert(p >= x);
        assert(p == 1u || (p >> 1) < x);
    }

    /* fls 와의 관계: x 가 2의 거듭제곱이 아니면 결과는 1u << (fls(x)+1) */
    assert(next_power_of_two(100u) == ((uint32_t)1u << (find_last_set(100u) + 1)));
    assert(next_power_of_two(128u) == ((uint32_t)1u << find_last_set(128u)));
    OK("next_power_of_two: 0/1 경계, 2^n 전후, 오버플로 0 반환, fls 와의 관계");
}

/* 실전 모양: 인터럽트 pending 비트맵을 우선순위 순으로 처리하고,
 * 링버퍼 크기를 2의 거듭제곱으로 올려 잡는다. */
static void test_practical(void)
{
    /* (a) pending 마스크를 낮은 번호부터 처리 (낮은 번호 = 높은 우선순위 가정) */
    uint32_t pending = (1u << 2) | (1u << 9) | (1u << 31);
    unsigned handled = 0;
    int order[3];
    while (pending != 0u) {
        int irq = find_first_set(pending);
        order[handled++] = irq;
        pending &= ~((uint32_t)1u << irq);                 /* 처리한 비트만 지운다 */
    }
    assert(handled == 3);
    assert(order[0] == 2 && order[1] == 9 && order[2] == 31);

    /* (b) 큐 크기: 요청 크기를 2의 거듭제곱으로 올려 마스킹 인덱스를 쓴다 */
    uint32_t want = 300u;
    uint32_t cap = next_power_of_two(want);
    assert(cap == 512u);
    assert(is_power_of_two(cap));
    uint32_t idx_mask = cap - 1u;
    assert(idx_mask == 511u);
    assert(((cap + 7u) & idx_mask) == 7u);                 /* wrap 이 AND 한 번 */

    /* (c) 비트맵에서 빈 슬롯 찾기: ~used 의 최하위 0 비트 = used 의 빈자리 */
    uint32_t used = 0x0000000Fu;                           /* 슬롯 0..3 사용중 */
    int free_slot = find_first_set(~used);
    assert(free_slot == 4);
    used |= (uint32_t)1u << free_slot;
    assert(popcount_kernighan(used) == 5u);

    /* 전부 사용중이면 -1 */
    used = 0xFFFFFFFFu;
    assert(find_first_set(~used) == -1);
    OK("실전: pending 순회, 큐 크기 2의 거듭제곱, 비트맵 빈 슬롯 찾기");
}

int main(void)
{
    printf("07_bit_count_reverse (starter)\n");
    test_popcount_boundaries();
    test_popcount_agreement();
    test_reverse();
    test_ffs_fls();
    test_power_of_two();
    test_next_power_of_two();
    test_practical();
    printf("ALL TESTS PASSED\n");
    return 0;
}
