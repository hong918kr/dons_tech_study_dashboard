/* 11_struct_layout.c — 구조체 패딩·offsetof·바이트 스트림 직렬화 (starter)
 *
 *   make run N=11
 *
 * 아래 TODO 여섯 군데를 채우면 된다. 타입 정의, _Static_assert, 테스트는
 * 그대로 둔다. 구현 전에는 첫 assert에서 멈추는 것이 정상이다.
 *
 * 핵심 아이디어
 *   - 구조체의 크기는 멤버 크기의 합이 아니다. 각 멤버는 자기 정렬의 배수인
 *     오프셋에 놓여야 하고, 구조체 전체 크기는 가장 큰 정렬의 배수로 올라간다.
 *     그 사이에 생기는 빈칸이 padding이고, 값이 정의되지 않는다.
 *   - 그 레이아웃은 ABI가 정한다. 컴파일러·타깃·옵션이 바뀌면 바뀔 수 있다.
 *     그래서 오프셋을 숫자로 외우는 대신 offsetof로 물어보고, 코드가 의존하는
 *     성질만 _Static_assert로 컴파일 타임에 못박는다.
 *   - 통신 프레임을 구조체로 "덮어씌워" 읽으면 padding·엔디언·정렬 세 가지가
 *     동시에 발목을 잡는다. 안전한 패턴은 바이트 단위 serialize / deserialize
 *     하나뿐이다. 이 파일이 그 패턴의 표준형이다.
 */

#include <assert.h>
#include <limits.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 1. 메모리 안의 타입 — 편한 대로 만든다                               */
/* ------------------------------------------------------------------ */

/* 센서 한 샘플. 이건 "CPU가 다루기 편한" 표현이다.
 * 필드 순서는 읽기 좋은 순서일 뿐이고, 컴파일러가 padding을 끼워 넣는다. */
typedef struct {
    uint8_t  id;             /* 채널 번호 0~255                        */
    uint16_t seq;            /* 시퀀스 번호, wrap 허용                  */
    uint32_t timestamp_us;   /* 캡처 시각 (us)                          */
    int16_t  temp_c_q8;      /* 온도, Q8 고정소수점 (256 = 1.0 도)      */
    uint8_t  flags;          /* bit0 valid, bit1 saturated, ...         */
} sensor_sample_t;

/* 같은 다섯 멤버를 정렬이 큰 것부터 늘어놓은 버전. padding이 줄거나 같다. */
typedef struct {
    uint32_t timestamp_us;
    uint16_t seq;
    int16_t  temp_c_q8;
    uint8_t  id;
    uint8_t  flags;
} sensor_tight_t;

/* 같은 다섯 멤버를 최악의 순서로 늘어놓은 버전. 1바이트 멤버를 큰 멤버
 * 사이사이에 끼워 넣으면 그 뒤마다 padding이 생긴다. */
typedef struct {
    uint8_t  id;
    uint32_t timestamp_us;
    uint8_t  flags;
    uint16_t seq;
    int16_t  temp_c_q8;
} sensor_loose_t;

/* ------------------------------------------------------------------ */
/* 2. 와이어 포맷 — 프로토콜이 정한다. padding도 정렬도 없다             */
/* ------------------------------------------------------------------ */

/* 바이트 오프셋 | 크기 | 필드          | 바이트 순서
 *      0        |  1   | id            | -
 *      1        |  2   | seq           | big-endian
 *      3        |  4   | timestamp_us  | big-endian
 *      7        |  2   | temp_c_q8     | big-endian, 2의 보수
 *      9        |  1   | flags         | -
 * 합계 10바이트. sizeof(sensor_sample_t)와 같을 이유가 전혀 없다. */
#define SENSOR_WIRE_SIZE 10u

#define WIRE_OFF_ID    0u
#define WIRE_OFF_SEQ   1u
#define WIRE_OFF_TS    3u
#define WIRE_OFF_TEMP  7u
#define WIRE_OFF_FLAGS 9u

/* 와이어 크기는 프로토콜 문서가 정한 상수다. 여기서만 틀어지면 전체가 어긋나니
 * 오프셋 합이 맞는지 컴파일 타임에 확인한다. */
_Static_assert(WIRE_OFF_FLAGS + 1u == SENSOR_WIRE_SIZE,
               "wire field offsets must tile the frame exactly");

/* ------------------------------------------------------------------ */
/* 3. 레이아웃에 대한 컴파일 타임 계약                                  */
/* ------------------------------------------------------------------ */

/* 여기서 "offsetof(seq) == 2" 처럼 숫자를 박지 않는 것이 중요하다.
 * 그 값은 ABI가 정하는 것이고, 다른 타깃에서 3이 되어도 코드는 옳다.
 * 대신 코드가 실제로 의존하는 성질만 못박는다. */

/* (1) 첫 멤버의 오프셋은 표준이 0으로 보장한다. 이건 어디서나 참이다. */
_Static_assert(offsetof(sensor_sample_t, id) == 0u,
               "first member must live at offset 0");

/* (2) 각 멤버는 자기 정렬의 배수 위에 있다 — 포인터를 꺼내 쓸 때의 전제 */
_Static_assert(offsetof(sensor_sample_t, seq) % alignof(uint16_t) == 0u,
               "seq must be naturally aligned");
_Static_assert(offsetof(sensor_sample_t, timestamp_us) % alignof(uint32_t) == 0u,
               "timestamp must be naturally aligned");
_Static_assert(offsetof(sensor_sample_t, temp_c_q8) % alignof(int16_t) == 0u,
               "temp must be naturally aligned");

/* (3) 멤버는 선언 순서대로 오프셋이 증가한다 — 표준 보장. 겹치지 않는다 */
_Static_assert(offsetof(sensor_sample_t, seq) >= offsetof(sensor_sample_t, id)
                                                + sizeof(uint8_t),
               "members must not overlap");
_Static_assert(offsetof(sensor_sample_t, timestamp_us)
                   >= offsetof(sensor_sample_t, seq) + sizeof(uint16_t),
               "members must not overlap");

/* (4) 구조체 전체 크기는 자기 정렬의 배수다 — 배열로 만들 수 있으려면 필수 */
_Static_assert(sizeof(sensor_sample_t) % alignof(sensor_sample_t) == 0u,
               "struct size must be a multiple of its alignment");

/* (5) 와이어보다 메모리 표현이 작을 수는 없다. 같을 수도 있고 클 수도 있다 */
_Static_assert(sizeof(sensor_sample_t) >= SENSOR_WIRE_SIZE,
               "in-memory form cannot be smaller than the wire form");

/* (6) 정렬 큰 것부터 늘어놓으면 padding이 줄거나 같다 */
_Static_assert(sizeof(sensor_tight_t) <= sizeof(sensor_loose_t),
               "reordering by decreasing alignment must not grow the struct");

/* (7) 바이트가 8비트여야 "1바이트 = 와이어 1칸"이 성립한다. DSP 계열에는
 *     CHAR_BIT이 16인 타깃이 있고, 거기서는 이 프로토콜 코드를 다시 써야 한다. */
_Static_assert(CHAR_BIT == 8, "this wire format assumes 8-bit bytes");
_Static_assert(sizeof(uint8_t) == 1u, "uint8_t must be exactly one byte");

/* ------------------------------------------------------------------ */
/* 4. 도우미                                                           */
/* ------------------------------------------------------------------ */

/* 멤버 크기의 합. padding을 뺀 "진짜 데이터" 바이트 수다. */
#define SENSOR_MEMBER_BYTES (sizeof(uint8_t) + sizeof(uint16_t)              \
                             + sizeof(uint32_t) + sizeof(int16_t)            \
                             + sizeof(uint8_t))

/* TODO 1: 이 구조체에 들어 있는 padding 바이트 수를 돌려준다.
 *   sizeof(구조체) - 멤버 크기의 합. SENSOR_MEMBER_BYTES를 쓴다. */
size_t struct_padding_bytes(void)
{
    return 0u;
}

/* uint16_t 비트패턴을 int16_t로 바꾼다.
 *
 * 왜 (int16_t)u 로 끝내지 않는가: 범위를 벗어나는 값을 부호 있는 타입으로
 * 변환하는 결과는 C11에서 implementation-defined다(C23부터 2의 보수로 고정).
 * 대부분의 컴파일러가 우리가 원하는 대로 자르지만, 프로토콜 코드는 표준만
 * 보고도 정답이 하나여야 한다. 아래 형태는 어디서나 같은 값을 준다. */
/* TODO 2: uint16_t 비트패턴을 int16_t 값으로 바꾼다.
 *   (int16_t)u 로 끝내지 말 것 — 범위를 넘는 값의 변환 결과는 C11에서
 *   implementation-defined다. 최상위 비트가 서 있으면 65536을 빼는 형태로
 *   쓰면 어느 컴파일러에서도 같은 값이 나온다. */
int16_t u16_to_i16(uint16_t u)
{
    (void)u;
    return 0;
}

/* int16_t를 2의 보수 비트패턴으로. 음수의 uint16_t 변환은 표준이 모듈로
 * 연산으로 정의하므로 이 방향은 캐스트만으로 안전하다. */
uint16_t i16_to_u16(int16_t v)
{
    return (uint16_t)v;
}

/* 두 샘플이 필드 단위로 같은가. memcmp를 쓰면 padding까지 비교하게 된다. */
/* TODO 3: 두 샘플을 필드 단위로 비교한다. memcmp를 쓰면 padding까지 보게 되니
 *   쓰지 말 것. NULL이 섞이면 0.
 * 반환: 1 같다, 0 다르다 */
int sensor_equal(const sensor_sample_t *a, const sensor_sample_t *b)
{
    (void)a;
    (void)b;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 5. serialize — 구조체를 바이트로                                    */
/* ------------------------------------------------------------------ */

/* 한 샘플을 out에 big-endian으로 적는다.
 * 반환: 적은 바이트 수(SENSOR_WIRE_SIZE), 인자가 나쁘면 0.
 *
 * out의 정렬을 전혀 요구하지 않는다는 점이 이 함수의 핵심이다. 바이트 단위로만
 * 쓰기 때문에 out이 홀수 주소여도, 링버퍼 중간이어도 동작한다. */
/* TODO 4: 한 샘플을 out에 big-endian으로 적는다.
 *   - out/s가 NULL이거나 cap < SENSOR_WIRE_SIZE 면 0 (부분 기록 금지)
 *   - WIRE_OFF_* 오프셋에 MSB부터 차례로 시프트해 넣는다
 *   - out의 정렬을 요구하지 않는다: 바이트 단위 대입만 쓴다
 *   - 음수 온도는 i16_to_u16으로 비트패턴을 얻는다
 * 반환: 적은 바이트 수(SENSOR_WIRE_SIZE), 실패면 0 */
size_t sensor_pack(uint8_t *out, size_t cap, const sensor_sample_t *s)
{
    (void)out;
    (void)cap;
    (void)s;
    return 0u;
}

/* ------------------------------------------------------------------ */
/* 6. deserialize — 바이트를 구조체로                                  */
/* ------------------------------------------------------------------ */

/* in에서 한 샘플을 읽는다. 반환: 0 성공, -1 인자/길이 부족.
 *
 * 시프트로 조립하기 때문에 in의 정렬도, 호스트 엔디언도 상관이 없다.
 * 같은 소스가 little-endian x86과 big-endian PowerPC에서 같은 값을 만든다. */
/* TODO 5: in에서 한 샘플을 읽어 s에 채운다.
 *   - s/in이 NULL이거나 n < SENSOR_WIRE_SIZE 면 -1 이고 s를 건드리지 않는다
 *   - 시프트로 조립한다. in의 정렬도 호스트 엔디언도 상관없어야 한다
 *   - 주의: 각 바이트를 (uint32_t)로 먼저 올린 뒤 << 24 한다. uint8_t를 그냥
 *     << 24 하면 int로 승격돼 부호 비트를 건드려 undefined behaviour가 된다
 *   - 온도는 u16_to_i16으로 부호를 복원한다
 * 반환: 0 성공, -1 실패 */
int sensor_unpack(sensor_sample_t *s, const uint8_t *in, size_t n)
{
    (void)s;
    (void)in;
    (void)n;
    return -1;
}

/* ------------------------------------------------------------------ */
/* 7. 배열 단위 — 와이어 stride는 sizeof가 아니다                       */
/* ------------------------------------------------------------------ */

/* count개를 연달아 적는다. 반환: 적은 총 바이트 수, 공간 부족이면 0.
 * 프레임 사이에 빈칸이 없다. 그래서 스트림에서 k번째 샘플의 위치는
 * k * SENSOR_WIRE_SIZE 이고, k * sizeof(sensor_sample_t) 가 아니다. */
/* TODO 6a: count개를 SENSOR_WIRE_SIZE 간격으로 연달아 적는다.
 *   - 다 담을 공간이 없으면 아무것도 쓰지 않고 0
 *   - 프레임 사이에 빈칸이 없다. stride는 sizeof(구조체)가 아니라 와이어 크기다
 * 반환: 적은 총 바이트 수 */
size_t sensor_pack_array(uint8_t *out, size_t cap,
                         const sensor_sample_t *arr, size_t count)
{
    (void)out;
    (void)cap;
    (void)arr;
    (void)count;
    return 0u;
}

/* 스트림에서 온전한 프레임만 꺼낸다. 반환: 꺼낸 샘플 수.
 * n이 프레임 크기의 배수가 아니면 남는 꼬리는 건드리지 않는다(다음 호출에서
 * 이어 붙이라는 뜻이다). max보다 많으면 max까지만 꺼낸다. */
/* TODO 6b: 스트림에서 온전한 프레임만 꺼낸다.
 *   - 꺼낼 수 있는 수 = n / SENSOR_WIRE_SIZE, 단 max를 넘지 않는다
 *   - 잘린 꼬리는 건드리지 않는다 (다음 호출에서 이어 붙이라는 뜻)
 * 반환: 꺼낸 샘플 수 */
size_t sensor_unpack_array(sensor_sample_t *arr, size_t max,
                           const uint8_t *in, size_t n)
{
    (void)arr;
    (void)max;
    (void)in;
    (void)n;
    return 0u;
}

/* ================================================================== */
/* 테스트                                                              */
/* ================================================================== */

static sensor_sample_t make_sample(uint8_t id, uint16_t seq, uint32_t ts,
                                   int16_t temp, uint8_t flags)
{
    sensor_sample_t s;
    memset(&s, 0, sizeof s);             /* padding까지 0으로 — 재현성 확보 */
    s.id           = id;
    s.seq          = seq;
    s.timestamp_us = ts;
    s.temp_c_q8    = temp;
    s.flags        = flags;
    return s;
}

static void test_layout_facts(void)
{
    /* offsetof로 물어본 값들이 서로 모순이 없는지 런타임에도 확인한다 */
    size_t o_id    = offsetof(sensor_sample_t, id);
    size_t o_seq   = offsetof(sensor_sample_t, seq);
    size_t o_ts    = offsetof(sensor_sample_t, timestamp_us);
    size_t o_temp  = offsetof(sensor_sample_t, temp_c_q8);
    size_t o_flags = offsetof(sensor_sample_t, flags);

    assert(o_id == 0u);
    assert(o_seq >= o_id + sizeof(uint8_t));
    assert(o_ts >= o_seq + sizeof(uint16_t));
    assert(o_temp >= o_ts + sizeof(uint32_t));
    assert(o_flags >= o_temp + sizeof(int16_t));
    assert(o_flags + sizeof(uint8_t) <= sizeof(sensor_sample_t));

    /* 정렬 배수 */
    assert(o_seq % alignof(uint16_t) == 0u);
    assert(o_ts % alignof(uint32_t) == 0u);
    assert(o_temp % alignof(int16_t) == 0u);

    /* padding 계산이 음수가 되면 어딘가 가정이 틀린 것이다 */
    assert(sizeof(sensor_sample_t) >= SENSOR_MEMBER_BYTES);
    assert(struct_padding_bytes() == sizeof(sensor_sample_t)
                                     - SENSOR_MEMBER_BYTES);

    puts("ok  1: offsetof로 본 레이아웃이 정렬·비겹침·크기 조건을 모두 만족한다");
}

static void test_reorder_shrinks(void)
{
    /* 같은 데이터, 다른 순서. 정렬 큰 것부터 놓으면 padding이 줄거나 같다 */
    assert(sizeof(sensor_tight_t) <= sizeof(sensor_loose_t));
    assert(sizeof(sensor_tight_t) >= SENSOR_MEMBER_BYTES);
    assert(sizeof(sensor_loose_t) >= SENSOR_MEMBER_BYTES);
    puts("ok  2: 정렬 내림차순 배치가 느슨한 배치보다 크지 않다");
}

static void test_padding_is_not_data(void)
{
    /* 같은 필드 값을 가진 두 구조체를 서로 다른 초기 바이트에서 만든다.
     * 필드 비교는 같다고 해야 하고, memcmp는 padding 때문에 달라질 수 있다. */
    sensor_sample_t a;
    sensor_sample_t b;

    memset(&a, 0x00, sizeof a);
    memset(&b, 0xFF, sizeof b);

    a.id = b.id = 7u;
    a.seq = b.seq = 0x1234u;
    a.timestamp_us = b.timestamp_us = 0xDEADBEEFu;
    a.temp_c_q8 = b.temp_c_q8 = -1234;
    a.flags = b.flags = 0x03u;

    assert(sensor_equal(&a, &b) == 1);   /* 필드는 완전히 같다 */

    if (struct_padding_bytes() > 0u) {
        /* padding 바이트에는 우리가 쓴 값이 그대로 남아 있다. 구조체 비교를
         * memcmp로 하면 여기서 "다르다"가 나온다 — 이게 흔한 버그다. */
        assert(memcmp(&a, &b, sizeof a) != 0);
    }

    /* 직렬화는 padding을 절대 내보내지 않는다. 같은 필드면 같은 바이트다. */
    uint8_t wa[SENSOR_WIRE_SIZE];
    uint8_t wb[SENSOR_WIRE_SIZE];
    assert(sensor_pack(wa, sizeof wa, &a) == SENSOR_WIRE_SIZE);
    assert(sensor_pack(wb, sizeof wb, &b) == SENSOR_WIRE_SIZE);
    assert(memcmp(wa, wb, SENSOR_WIRE_SIZE) == 0);

    puts("ok  3: padding은 데이터가 아니다 — memcmp는 갈리고 와이어는 같다");
}

static void test_wire_bytes_exact(void)
{
    /* 바이트 순서를 눈으로 확인한다. 이 배열이 프로토콜 문서의 예시와 같아야 한다 */
    sensor_sample_t s = make_sample(0x2Au, 0x0102u, 0x03040506u,
                                    u16_to_i16(0x8001u), 0xC0u);
    uint8_t w[SENSOR_WIRE_SIZE + 4u];
    memset(w, 0xEE, sizeof w);

    assert(sensor_pack(w, SENSOR_WIRE_SIZE, &s) == SENSOR_WIRE_SIZE);

    static const uint8_t want[SENSOR_WIRE_SIZE] = {
        0x2A,                      /* id                  */
        0x01, 0x02,                /* seq       big-endian */
        0x03, 0x04, 0x05, 0x06,    /* timestamp big-endian */
        0x80, 0x01,                /* temp      big-endian */
        0xC0                       /* flags               */
    };
    assert(memcmp(w, want, SENSOR_WIRE_SIZE) == 0);
    /* 프레임 뒤를 넘어 쓰지 않았다 */
    assert(w[SENSOR_WIRE_SIZE] == 0xEEu);
    assert(w[SENSOR_WIRE_SIZE + 3u] == 0xEEu);

    puts("ok  4: 와이어 바이트가 프로토콜이 정한 big-endian 순서와 정확히 같다");
}

static void test_roundtrip_boundaries(void)
{
    /* 경계값: 0, 1, 각 타입의 최대/최소, 부호 경계 */
    static const struct {
        uint8_t  id;
        uint16_t seq;
        uint32_t ts;
        int16_t  temp;
        uint8_t  flags;
    } cases[] = {
        {0u,    0u,      0u,          0,         0u},
        {1u,    1u,      1u,          1,         1u},
        {255u,  65535u,  4294967295u, 32767,     255u},   /* 전부 최대  */
        {128u,  32768u,  2147483648u, -32768,    0x80u},   /* 부호 경계  */
        {7u,    0xFFFFu, 0x80000000u, -1,        0x55u},   /* -1 = 0xFFFF */
        {9u,    0x0100u, 0x0000FFFFu, 256,       0xAAu},   /* Q8로 1.0도 */
    };

    for (size_t i = 0u; i < sizeof cases / sizeof cases[0]; i++) {
        sensor_sample_t in = make_sample(cases[i].id, cases[i].seq,
                                         cases[i].ts, cases[i].temp,
                                         cases[i].flags);
        uint8_t         w[SENSOR_WIRE_SIZE];
        sensor_sample_t out;

        assert(sensor_pack(w, sizeof w, &in) == SENSOR_WIRE_SIZE);
        memset(&out, 0x5A, sizeof out);
        assert(sensor_unpack(&out, w, sizeof w) == 0);
        assert(sensor_equal(&in, &out) == 1);
    }
    puts("ok  5: 0·1·최대·부호 경계 전부 pack→unpack 왕복에서 값이 보존된다");
}

static void test_unaligned_buffer(void)
{
    /* 여기가 이 문제의 핵심이다. 와이어 버퍼를 홀수 주소에서 시작시킨다.
     * 구조체 포인터로 캐스팅해 읽었다면 Cortex-M0에서는 HardFault,
     * Cortex-M4에서는 느린 접근, 일부 ARMv7 LDM에서는 데이터 회전이 된다.
     * 바이트 단위 pack/unpack은 아무 일도 일어나지 않는다. */
    static uint8_t raw[SENSOR_WIRE_SIZE * 2u + 8u];
    sensor_sample_t in = make_sample(0x11u, 0xBEEFu, 0x01234567u, -300, 0x0Fu);

    for (size_t skew = 0u; skew < 8u; skew++) {
        uint8_t        *w = raw + skew;   /* 일부러 정렬을 어긋나게 한다 */
        sensor_sample_t out;

        memset(raw, 0u, sizeof raw);
        assert(sensor_pack(w, SENSOR_WIRE_SIZE, &in) == SENSOR_WIRE_SIZE);
        assert(sensor_unpack(&out, w, SENSOR_WIRE_SIZE) == 0);
        assert(sensor_equal(&in, &out) == 1);
    }
    puts("ok  6: 와이어 버퍼가 1~7바이트 어긋난 주소에 있어도 왕복이 성립한다");
}

static void test_reject_short_and_null(void)
{
    sensor_sample_t s = make_sample(1u, 2u, 3u, 4, 5u);
    sensor_sample_t out;
    uint8_t         w[SENSOR_WIRE_SIZE];

    /* 공간이 1바이트라도 부족하면 아무것도 쓰지 않는다 */
    memset(w, 0xEE, sizeof w);
    assert(sensor_pack(w, SENSOR_WIRE_SIZE - 1u, &s) == 0u);
    for (size_t i = 0u; i < sizeof w; i++) {
        assert(w[i] == 0xEEu);           /* 부분 기록 금지 */
    }
    assert(sensor_pack(w, SENSOR_WIRE_SIZE, &s) == SENSOR_WIRE_SIZE);

    /* 프레임이 1바이트 모자라면 unpack은 실패하고 출력을 건드리지 않는다 */
    memset(&out, 0x77, sizeof out);
    assert(sensor_unpack(&out, w, SENSOR_WIRE_SIZE - 1u) == -1);
    assert(out.id == 0x77u);

    /* NULL 방어 */
    assert(sensor_pack(NULL, sizeof w, &s) == 0u);
    assert(sensor_pack(w, sizeof w, NULL) == 0u);
    assert(sensor_unpack(NULL, w, sizeof w) == -1);
    assert(sensor_unpack(&out, NULL, sizeof w) == -1);
    assert(sensor_unpack(&out, w, 0u) == -1);

    puts("ok  7: 길이 부족·NULL을 거부하고 부분 기록을 남기지 않는다");
}

static void test_array_stride(void)
{
    sensor_sample_t in[4];
    for (uint8_t i = 0u; i < 4u; i++) {
        in[i] = make_sample((uint8_t)(i + 1u), (uint16_t)(0x1000u + i),
                            0x10000000u * (uint32_t)(i + 1u),
                            (int16_t)(-256 * (i + 1)), (uint8_t)(i * 3u));
    }

    uint8_t stream[SENSOR_WIRE_SIZE * 4u + 3u];
    memset(stream, 0xEE, sizeof stream);

    size_t wrote = sensor_pack_array(stream, sizeof stream, in, 4u);
    assert(wrote == SENSOR_WIRE_SIZE * 4u);

    /* 와이어 stride가 sizeof가 아니라 SENSOR_WIRE_SIZE임을 확인한다.
     * k번째 프레임의 첫 바이트는 그 샘플의 id다. */
    for (size_t k = 0u; k < 4u; k++) {
        assert(stream[k * SENSOR_WIRE_SIZE] == in[k].id);
    }
    /* 4프레임 뒤의 3바이트는 손대지 않았다 */
    assert(stream[wrote] == 0xEEu);

    sensor_sample_t out[4];
    assert(sensor_unpack_array(out, 4u, stream, wrote) == 4u);
    for (size_t k = 0u; k < 4u; k++) {
        assert(sensor_equal(&in[k], &out[k]) == 1);
    }

    /* 꼬리가 잘린 스트림: 온전한 프레임만 꺼낸다 */
    assert(sensor_unpack_array(out, 4u, stream, wrote - 1u) == 3u);
    assert(sensor_unpack_array(out, 4u, stream, SENSOR_WIRE_SIZE - 1u) == 0u);
    /* 목적지 배열이 작으면 거기까지만 */
    assert(sensor_unpack_array(out, 2u, stream, wrote) == 2u);

    /* 공간이 부족하면 아무것도 쓰지 않는다 */
    uint8_t small[SENSOR_WIRE_SIZE * 3u];
    memset(small, 0xEE, sizeof small);
    assert(sensor_pack_array(small, sizeof small, in, 4u) == 0u);
    assert(small[0] == 0xEEu);

    puts("ok  8: 스트림 stride가 와이어 크기이고 잘린 꼬리를 안전하게 남긴다");
}

static void test_signed_temperature(void)
{
    /* 음수 온도가 2의 보수로 나가고 그대로 돌아오는지 전 범위로 확인한다 */
    for (int32_t t = -32768; t <= 32767; t += 137) {
        sensor_sample_t in = make_sample(3u, 9u, 12345u, (int16_t)t, 1u);
        uint8_t         w[SENSOR_WIRE_SIZE];
        sensor_sample_t out;

        assert(sensor_pack(w, sizeof w, &in) == SENSOR_WIRE_SIZE);
        assert(sensor_unpack(&out, w, sizeof w) == 0);
        assert(out.temp_c_q8 == (int16_t)t);
    }
    /* 극단값 두 개는 따로 못박는다 */
    for (int32_t t = -32768; t <= 32767; t += 65535) {
        sensor_sample_t in = make_sample(3u, 9u, 0u, (int16_t)t, 0u);
        uint8_t         w[SENSOR_WIRE_SIZE];
        sensor_sample_t out;
        assert(sensor_pack(w, sizeof w, &in) == SENSOR_WIRE_SIZE);
        assert(sensor_unpack(&out, w, sizeof w) == 0);
        assert(out.temp_c_q8 == (int16_t)t);
    }
    /* -1은 0xFFFF로 나가야 한다 */
    sensor_sample_t m1 = make_sample(0u, 0u, 0u, -1, 0u);
    uint8_t         w1[SENSOR_WIRE_SIZE];
    assert(sensor_pack(w1, sizeof w1, &m1) == SENSOR_WIRE_SIZE);
    assert(w1[WIRE_OFF_TEMP] == 0xFFu && w1[WIRE_OFF_TEMP + 1u] == 0xFFu);

    puts("ok  9: 부호 있는 필드가 2의 보수로 왕복하고 -1이 0xFFFF로 나간다");
}

static void test_host_endian_independence(void)
{
    /* 호스트가 little-endian인지 확인만 하고, 와이어 결과가 그것과 무관함을
     * 보인다. 같은 값이 메모리에서는 뒤집혀 있는데 와이어에서는 안 뒤집힌다. */
    uint32_t probe = 0x01020304u;
    uint8_t  view[sizeof probe];
    memcpy(view, &probe, sizeof probe);  /* 타입 재해석은 memcpy로만 */

    int host_is_little = (view[0] == 0x04u);
    int host_is_big    = (view[0] == 0x01u);
    assert(host_is_little || host_is_big);      /* 중간 엔디언은 가정하지 않는다 */

    sensor_sample_t s = make_sample(0u, 0u, 0x01020304u, 0, 0u);
    uint8_t         w[SENSOR_WIRE_SIZE];
    assert(sensor_pack(w, sizeof w, &s) == SENSOR_WIRE_SIZE);
    assert(w[WIRE_OFF_TS + 0u] == 0x01u);      /* 항상 MSB 먼저 */
    assert(w[WIRE_OFF_TS + 3u] == 0x04u);

    printf("     host=%s, wire=big-endian 고정\n",
           host_is_little ? "little-endian" : "big-endian");
    puts("ok 10: 와이어 바이트 순서가 호스트 엔디언과 무관하게 고정된다");
}

int main(void)
{
    printf("layout: sizeof=%zu, members=%zu, padding=%zu, align=%zu, wire=%u\n",
           sizeof(sensor_sample_t), (size_t)SENSOR_MEMBER_BYTES,
           struct_padding_bytes(), alignof(sensor_sample_t),
           SENSOR_WIRE_SIZE);
    printf("offsets: id=%zu seq=%zu ts=%zu temp=%zu flags=%zu\n",
           offsetof(sensor_sample_t, id), offsetof(sensor_sample_t, seq),
           offsetof(sensor_sample_t, timestamp_us),
           offsetof(sensor_sample_t, temp_c_q8),
           offsetof(sensor_sample_t, flags));
    printf("reorder: tight=%zu loose=%zu\n",
           sizeof(sensor_tight_t), sizeof(sensor_loose_t));

    puts("(구현 전에는 아래 첫 assert에서 멈추는 것이 정상이다)");

    test_layout_facts();
    test_reorder_shrinks();
    test_padding_is_not_data();
    test_wire_bytes_exact();
    test_roundtrip_boundaries();
    test_unaligned_buffer();
    test_reject_short_and_null();
    test_array_stride();
    test_signed_temperature();
    test_host_endian_independence();

    puts("ALL TESTS PASSED");
    return 0;
}
