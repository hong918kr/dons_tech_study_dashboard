/* 16_crc8_table.c — CRC-8 비트 단위 -> 테이블 (solution)
 *
 * 변종: CRC-8/MAXIM-DOW  (다른 이름: CRC-8/DOW-CRC, "1-Wire CRC", Dallas CRC)
 *   width  = 8
 *   poly   = 0x31          (정방향 표기. x^8 + x^5 + x^4 + 1)
 *   init   = 0x00
 *   refin  = true          <- 바이트를 LSB부터 먹는다
 *   refout = true
 *   xorout = 0x00
 *   check  = 0xA1          ASCII "123456789" (9바이트)에 대한 값
 *   residue= 0x00          메시지 뒤에 CRC를 붙여 다시 돌리면 0이 된다
 *
 * refin/refout이 true이므로 구현은 '반사된' 형태를 쓴다. 반사된 다항식은
 * 0x31을 비트 반전한 0x8C다. 그래서 코드에는 0x31이 아니라 0x8C가 보인다.
 * 이게 CRC 구현에서 가장 흔한 혼동 지점이다.
 *
 * 이 변종을 고른 이유: DS18B20 온도센서, 1-Wire EEPROM의 64비트 ROM ID와
 * 9바이트 scratchpad가 전부 이 CRC를 쓴다. 실제 데이터시트 예제로 검증할 수
 * 있다(테스트에 들어 있다).
 *
 * 05_uart_parser와의 차이: 05는 CRC-16/CCITT-FALSE(MSB-first, 비반사)를
 * 파서 상태기계 안에서 스트리밍한다. 여기는 반대 비트 방향(LSB-first, 반사)의
 * CRC-8을 다루고, 초점은 파싱이 아니라 '비트 루프 == 테이블'의 동등성,
 * 테이블을 코드로 생성하는 방법, 그리고 residue를 이용한 자기검증이다.
 *
 * build: cc -std=c11 -Wall -Wextra -O2 16_crc8_table.c -o sol_16
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 상수                                                                 */
/* ------------------------------------------------------------------ */

#define CRC8_POLY_REFLECTED  0x8Cu   /* 0x31을 비트 반전한 값 */
#define CRC8_INIT            0x00u
#define CRC8_TABLE_SIZE      256u

/* ------------------------------------------------------------------ */
/* 1. 비트 단위 — 정의 그대로                                            */
/* ------------------------------------------------------------------ */

/* crc에 바이트 b를 한 개 먹인다. 반사 CRC이므로:
 *   - 바이트를 crc의 '아래쪽'에 XOR한다(비반사는 위쪽에 XOR).
 *   - LSB를 보고 오른쪽으로 밀며, LSB가 1이면 반사 다항식을 XOR한다.
 * 8비트 CRC는 crc 레지스터가 8비트이므로 바이트를 crc에 바로 XOR할 수 있다. */
uint8_t crc8_bit(uint8_t crc, uint8_t b)
{
    crc = (uint8_t)(crc ^ b);
    for (int i = 0; i < 8; i++) {
        crc = (crc & 0x01u) ? (uint8_t)((crc >> 1) ^ CRC8_POLY_REFLECTED)
                            : (uint8_t)(crc >> 1);
    }
    return crc;
}

/* 버퍼 전체의 CRC. 비트 루프 버전. 바이트당 8회전 = 참조 구현. */
uint8_t crc8_bitwise(const uint8_t *d, size_t n)
{
    uint8_t crc = (uint8_t)CRC8_INIT;
    for (size_t i = 0u; i < n; i++) {
        crc = crc8_bit(crc, d[i]);
    }
    return crc;
}

/* ------------------------------------------------------------------ */
/* 2. 테이블 생성                                                       */
/* ------------------------------------------------------------------ */

/* table[i] = "CRC 레지스터가 0일 때 바이트 i를 먹인 결과".
 *
 * 왜 이게 성립하나: CRC는 GF(2) 위의 선형 연산이다. 즉
 *     crc_next = f(crc ^ b)
 * 로 쓸 수 있고, f는 8회전 시프트 루프다. 따라서 (crc ^ b) 한 바이트만
 * 미리 계산해 두면 런타임에는 배열 참조 한 번으로 끝난다.
 *
 * 테이블은 256바이트다. Flash 256바이트를 내고 바이트당 8회전을 없애는
 * 거래다. 4비트(니블) 테이블을 쓰면 16바이트로 줄고 바이트당 2회 참조다. */
void crc8_table_build(uint8_t *table)
{
    for (unsigned i = 0u; i < CRC8_TABLE_SIZE; i++) {
        uint8_t c = (uint8_t)i;
        for (int b = 0; b < 8; b++) {
            c = (c & 0x01u) ? (uint8_t)((c >> 1) ^ CRC8_POLY_REFLECTED)
                            : (uint8_t)(c >> 1);
        }
        table[i] = c;
    }
}

/* ------------------------------------------------------------------ */
/* 3. 테이블 버전 — 스트리밍                                             */
/* ------------------------------------------------------------------ */

/* 이미 진행 중인 crc에 버퍼를 이어 먹인다. 조각을 어떻게 쪼개 넣어도
 * 결과가 같다(스트리밍 성질). 시작할 때 crc에 CRC8_INIT을 넘긴다. */
uint8_t crc8_update(const uint8_t *table, uint8_t crc, const uint8_t *d, size_t n)
{
    for (size_t i = 0u; i < n; i++) {
        crc = table[crc ^ d[i]];
    }
    return crc;
}

/* ------------------------------------------------------------------ */
/* 4. 자기검증 (residue)                                                */
/* ------------------------------------------------------------------ */

/* d[0..n-2]가 메시지이고 d[n-1]이 그 CRC라고 보고 검사한다.
 * xorout이 0인 이 변종은 residue도 0이라, 메시지와 CRC를 이어서 한 번에
 * 돌리면 0이 나온다. 따로 비교할 필요가 없어 1-Wire 드라이버가 즐겨 쓴다.
 * n == 0이면 검사할 것이 없으므로 false. */
bool crc8_check(const uint8_t *table, const uint8_t *d, size_t n)
{
    if (n == 0u) { return false; }
    return crc8_update(table, (uint8_t)CRC8_INIT, d, n) == 0x00u;
}

/* ================================================================== */
/* 여기부터는 테스트 코드                                                */
/* ================================================================== */

/* 비트 방향을 틀렸을 때 무엇이 나오는지 보여주기 위한 '오답' 구현.
 * poly 0x31을 그대로 쓰면서 MSB-first로 도는 비반사 CRC-8이다. */
static uint8_t crc8_msb_first_0x31(const uint8_t *d, size_t n)
{
    uint8_t crc = 0x00u;
    for (size_t i = 0u; i < n; i++) {
        crc = (uint8_t)(crc ^ d[i]);
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x31u)
                                : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static uint32_t lcg_state = 987654321u;
static void lcg_reset(void) { lcg_state = 987654321u; }
static uint32_t lcg_next(void)
{
    lcg_state = lcg_state * 1103515245u + 12345u;
    return lcg_state >> 16;
}

static uint8_t g_table[CRC8_TABLE_SIZE];

static void test_documented_check_value(void)
{
    /* 표준 CRC 카탈로그가 정의하는 check 값: ASCII "123456789"의 CRC.
     * CRC-8/MAXIM-DOW의 check는 0xA1이다. 비트 버전과 테이블 버전이
     * 둘 다 이 값을 내야 한다. */
    const uint8_t s[9] = { '1','2','3','4','5','6','7','8','9' };

    uint8_t a = crc8_bitwise(s, sizeof s);
    uint8_t b = crc8_update(g_table, (uint8_t)CRC8_INIT, s, sizeof s);
    assert(a == 0xA1u);
    assert(b == 0xA1u);
    printf("  ok  CRC-8/MAXIM-DOW check value: \"123456789\" -> 0x%02X (bit and table)\n", a);
}

static void test_datasheet_example(void)
{
    /* DS18B20 scratchpad 9바이트. 앞 8바이트의 CRC가 9번째 바이트다.
     * (+85.0 C 리셋 상태의 전형적인 값) */
    const uint8_t sp[9] = { 0x50u, 0x05u, 0x4Bu, 0x46u, 0x7Fu,
                            0xFFu, 0x0Cu, 0x10u, 0x1Cu };
    assert(crc8_bitwise(sp, 8u) == 0x1Cu);
    assert(crc8_update(g_table, (uint8_t)CRC8_INIT, sp, 8u) == 0x1Cu);
    /* 9바이트 전체를 돌리면 residue 0 */
    assert(crc8_check(g_table, sp, 9u));
    printf("  ok  DS18B20 scratchpad example: CRC byte 0x1C matches, residue 0\n");
}

static void test_bit_order_matters(void)
{
    /* 같은 poly 0x31을 MSB-first로 돌리면 전혀 다른 값이 나온다.
     * 'poly만 맞으면 된다'는 착각을 여기서 깬다. */
    const uint8_t s[9] = { '1','2','3','4','5','6','7','8','9' };
    uint8_t wrong = crc8_msb_first_0x31(s, sizeof s);
    assert(wrong != 0xA1u);
    printf("  ok  same poly 0x31 MSB-first gives 0x%02X, not 0xA1 (refin matters)\n",
           wrong);
}

static void test_table_matches_bitwise_all_bytes(void)
{
    /* table[i]는 'crc=0에서 바이트 i를 먹인 결과'와 정의상 같아야 한다. */
    for (unsigned i = 0u; i < CRC8_TABLE_SIZE; i++) {
        assert(g_table[i] == crc8_bit(0x00u, (uint8_t)i));
    }
    assert(g_table[0] == 0x00u);          /* 0을 먹이면 0 그대로 */
    printf("  ok  all 256 table entries equal the bitwise result\n");
}

static void test_table_matches_bitwise_random(void)
{
    lcg_reset();
    for (unsigned trial = 0u; trial < 500u; trial++) {
        uint8_t buf[64];
        size_t n = (size_t)(lcg_next() % 65u);       /* 0..64, n=0 포함 */
        for (size_t i = 0u; i < n; i++) {
            buf[i] = (uint8_t)lcg_next();
        }
        uint8_t a = crc8_bitwise(buf, n);
        uint8_t b = crc8_update(g_table, (uint8_t)CRC8_INIT, buf, n);
        assert(a == b);
    }
    printf("  ok  500 random buffers (len 0..64): bitwise == table\n");
}

static void test_empty_and_one_byte(void)
{
    const uint8_t one = 0x5Au;
    /* 빈 입력의 CRC는 init 값 그대로다. 계산이 아니라 정의다. */
    assert(crc8_bitwise(NULL, 0u) == (uint8_t)CRC8_INIT);
    assert(crc8_update(g_table, (uint8_t)CRC8_INIT, NULL, 0u) == (uint8_t)CRC8_INIT);
    /* 1바이트는 테이블 한 칸을 그대로 읽는 것과 같다 */
    assert(crc8_bitwise(&one, 1u) == g_table[0x5Au]);
    /* n == 0 검사는 false */
    assert(!crc8_check(g_table, NULL, 0u));
    printf("  ok  n=0 returns init (0x00), n=1 equals one table lookup\n");
}

static void test_streaming_split_invariance(void)
{
    /* 어떤 경계로 쪼개도 결과가 같아야 한다. UART/SPI로 조각조각 들어오는
     * 데이터를 버퍼에 다 모으지 않고 계산할 수 있는 근거다. */
    uint8_t msg[37];
    lcg_reset();
    for (size_t i = 0u; i < sizeof msg; i++) { msg[i] = (uint8_t)lcg_next(); }

    uint8_t once = crc8_update(g_table, (uint8_t)CRC8_INIT, msg, sizeof msg);
    for (size_t split = 0u; split <= sizeof msg; split++) {
        uint8_t c = crc8_update(g_table, (uint8_t)CRC8_INIT, msg, split);
        c = crc8_update(g_table, c, msg + split, sizeof msg - split);
        assert(c == once);
    }
    /* 1바이트씩 38조각으로 나눠도 같다 */
    uint8_t c1 = (uint8_t)CRC8_INIT;
    for (size_t i = 0u; i < sizeof msg; i++) {
        c1 = crc8_update(g_table, c1, &msg[i], 1u);
    }
    assert(c1 == once);
    printf("  ok  streaming: every one of %zu split points gives the same CRC\n",
           sizeof msg + 1u);
}

static void test_residue_property(void)
{
    /* 메시지 + CRC를 이어 돌리면 0. xorout=0인 변종의 성질이다. */
    lcg_reset();
    for (unsigned trial = 0u; trial < 200u; trial++) {
        uint8_t buf[33];
        size_t n = 1u + (size_t)(lcg_next() % 32u);
        for (size_t i = 0u; i < n; i++) { buf[i] = (uint8_t)lcg_next(); }
        buf[n] = crc8_bitwise(buf, n);
        assert(crc8_check(g_table, buf, n + 1u));

        /* 한 비트라도 틀리면 검사에 걸려야 한다 */
        buf[n / 2u] = (uint8_t)(buf[n / 2u] ^ 0x01u);
        assert(!crc8_check(g_table, buf, n + 1u));
    }
    printf("  ok  residue: msg||crc -> 0, and a flipped bit breaks it (200 trials)\n");
}

static void test_all_single_bit_errors_detected(void)
{
    /* CRC-8은 8비트보다 짧은 버스트를 전부 잡는다. 여기서는 더 약한,
     * 그러나 확실한 성질을 전수 검사한다: 어떤 1비트 에러도 CRC를 바꾼다. */
    uint8_t msg[16];
    for (size_t i = 0u; i < sizeof msg; i++) { msg[i] = (uint8_t)(i * 17u + 3u); }
    uint8_t good = crc8_update(g_table, (uint8_t)CRC8_INIT, msg, sizeof msg);

    for (size_t i = 0u; i < sizeof msg; i++) {
        for (unsigned bit = 0u; bit < 8u; bit++) {
            uint8_t saved = msg[i];
            msg[i] = (uint8_t)(msg[i] ^ (1u << bit));
            assert(crc8_update(g_table, (uint8_t)CRC8_INIT, msg, sizeof msg) != good);
            msg[i] = saved;
        }
    }
    printf("  ok  all %u single-bit errors in a 16-byte message change the CRC\n",
           (unsigned)(sizeof msg * 8u));
}

static void test_two_bit_burst_within_8_detected(void)
{
    /* 8비트 폭 안의 어떤 에러 패턴도 잡는다(CRC의 보장 범위).
     * 같은 바이트 안에서 두 비트를 동시에 뒤집어 전수 확인한다. */
    uint8_t msg[8];
    for (size_t i = 0u; i < sizeof msg; i++) { msg[i] = (uint8_t)(0xA0u + i); }
    uint8_t good = crc8_bitwise(msg, sizeof msg);
    unsigned checked = 0u;

    for (size_t i = 0u; i < sizeof msg; i++) {
        for (unsigned b1 = 0u; b1 < 8u; b1++) {
            for (unsigned b2 = b1 + 1u; b2 < 8u; b2++) {
                uint8_t saved = msg[i];
                msg[i] = (uint8_t)(msg[i] ^ (1u << b1) ^ (1u << b2));
                assert(crc8_bitwise(msg, sizeof msg) != good);
                msg[i] = saved;
                checked++;
            }
        }
    }
    printf("  ok  all %u two-bit errors inside one byte are detected\n", checked);
}

static void test_leading_zeros_weakness(void)
{
    /* init = 0x00의 알려진 약점: 앞에 0x00이 몇 개 붙어도 CRC가 그대로다.
     * 즉 '0으로 채운 프레임'의 길이 에러를 못 잡는다.
     * 그래서 많은 변종이 init = 0xFF를 쓴다. 약점을 아는 것도 스펙의 일부다. */
    const uint8_t zeros[8] = { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u };
    for (size_t n = 0u; n <= sizeof zeros; n++) {
        assert(crc8_bitwise(zeros, n) == 0x00u);
    }
    const uint8_t a[3] = { 0x11u, 0x22u, 0x33u };
    uint8_t b[6] = { 0u, 0u, 0u, 0x11u, 0x22u, 0x33u };
    assert(crc8_bitwise(a, 3u) == crc8_bitwise(b, 6u));
    printf("  ok  init=0x00 weakness: leading 0x00 bytes do not change the CRC\n");
}

static void test_table_build_is_pure(void)
{
    /* 두 번 만들면 같은 테이블이 나오고, 다른 배열에 만들어도 같다.
     * (테이블을 런타임에 만드는 대신 코드 생성으로 박아넣을 수 있다는 근거) */
    uint8_t t2[CRC8_TABLE_SIZE];
    memset(t2, 0xFFu, sizeof t2);
    crc8_table_build(t2);
    assert(memcmp(t2, g_table, sizeof t2) == 0);
    crc8_table_build(t2);
    assert(memcmp(t2, g_table, sizeof t2) == 0);
    printf("  ok  crc8_table_build is deterministic and idempotent\n");
}

int main(void)
{
    printf("16_crc8_table (solution)\n");
    crc8_table_build(g_table);
    test_documented_check_value();
    test_datasheet_example();
    test_bit_order_matters();
    test_table_matches_bitwise_all_bytes();
    test_table_matches_bitwise_random();
    test_empty_and_one_byte();
    test_streaming_split_invariance();
    test_residue_property();
    test_all_single_bit_errors_detected();
    test_two_bit_burst_within_8_detected();
    test_leading_zeros_weakness();
    test_table_build_is_pure();
    printf("ALL TESTS PASSED\n");
    return 0;
}
