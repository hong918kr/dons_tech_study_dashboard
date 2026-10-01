/* 08_endian_align.c — byte swap, 런타임 엔디언 판별, unaligned 접근, 정렬 올림 (모범답안)
 *
 * 빌드: cc -std=c11 -Wall -Wextra -O2 -g 08_endian_align.c -o sol && ./sol
 *       (또는 coding/ 에서 make sol N=08)
 *
 * 레벨 L0. 이 파일의 핵심 주장은 하나다:
 *
 *   바이트 스트림(패킷, 플래시 레코드, 오디오 프레임)에서 정수를 꺼낼 때는
 *   포인터 캐스트를 쓰지 말고 바이트를 직접 조립한다.
 *
 * 포인터 캐스트는 두 가지를 동시에 어긴다.
 *   1. 정렬: uint32_t 정렬이 아닌 주소를 uint32_t* 로 읽는 것은 UB 다.
 *      x86 은 조용히 동작하고 Cortex-M0/M3 의 LDM 이나 일부 DSP 는 HardFault 를 낸다.
 *   2. strict aliasing: unsigned char 배열을 uint32_t* 로 읽으면 컴파일러가
 *      "이 두 포인터는 절대 같은 곳을 가리키지 않는다"고 가정해 -O2 에서 재배치한다.
 *
 * 반면 memcpy 와 바이트 조립은 둘 다 위반하지 않고, -O2 에서 보통 한 명령으로 접힌다.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    ENDIAN_LITTLE,
    ENDIAN_BIG
} endian_t;

/* ------------------------------------------------------------------ */
/* 1. byte swap                                                        */
/* ------------------------------------------------------------------ */

/* uint16_t 는 산술 연산에서 int 로 승격된다(integer promotion).
 * 그래서 중간 계산은 int 로 일어나고, 마지막에 명시적으로 좁혀 준다.
 * 캐스트를 빼면 결과는 같지만 -Wconversion 계열에서 경고가 난다. */
static uint16_t bswap16(uint16_t v)
{
    return (uint16_t)(((uint32_t)v >> 8) | ((uint32_t)v << 8));
}

/* 0xAABBCCDD -> 0xDDCCBBAA. 바이트 4개를 서로 맞바꾼다. */
static uint32_t bswap32(uint32_t v)
{
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) <<  8) |
           ((v & 0x00FF0000u) >>  8) |
           ((v & 0xFF000000u) >> 24);
}

/* 64비트는 32비트 두 번으로 나눠 처리하면 상수도 짧고 읽기 쉽다.
 * uint64_t 상수에는 반드시 UL/ULL 이 아니라 캐스트나 UINT64_C 를 쓴다. */
static uint64_t bswap64(uint64_t v)
{
    uint32_t hi = (uint32_t)(v >> 32);
    uint32_t lo = (uint32_t)(v & 0xFFFFFFFFu);
    return ((uint64_t)bswap32(lo) << 32) | (uint64_t)bswap32(hi);
}

/* ------------------------------------------------------------------ */
/* 2. 런타임 엔디언 판별                                                 */
/* ------------------------------------------------------------------ */

/* 0x0102 를 바이트로 꺼내 첫 바이트가 무엇인지 본다.
 * union 이나 (unsigned char*) 캐스트도 동작하지만 memcpy 가 가장 안전하고,
 * -O2 에서는 상수 한 개로 접힌다(런타임 비용 0).
 *
 * 실전에서는 컴파일 타임 매크로(__BYTE_ORDER__ 등)를 쓰는 편이 낫다.
 * 런타임 판별의 용도는 "이식성 테스트"와 "면접에서 보여주기"다. */
static endian_t host_endian(void)
{
    uint16_t probe = 0x0102u;
    unsigned char b[sizeof probe];
    memcpy(b, &probe, sizeof b);
    /* little endian: 메모리 = [0x02, 0x01]   (낮은 주소에 낮은 바이트)
     * big endian:    메모리 = [0x01, 0x02] */
    return (b[0] == 0x02u) ? ENDIAN_LITTLE : ENDIAN_BIG;
}

/* ------------------------------------------------------------------ */
/* 3. 바이트 스트림 <-> 정수 (엔디언 명시, unaligned 안전)                */
/* ------------------------------------------------------------------ */
/* 프로토콜 필드를 읽는 정석. 주소 정렬을 전혀 요구하지 않고,
 * host 엔디언과 무관하게 항상 같은 결과를 준다. */

static uint16_t load_be16(const void *p)
{
    const unsigned char *b = (const unsigned char *)p;
    return (uint16_t)(((uint32_t)b[0] << 8) | (uint32_t)b[1]);
}

static uint32_t load_be32(const void *p)
{
    const unsigned char *b = (const unsigned char *)p;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] <<  8) | ((uint32_t)b[3]);
}

static void store_be32(void *p, uint32_t v)
{
    unsigned char *b = (unsigned char *)p;
    b[0] = (unsigned char)(v >> 24);
    b[1] = (unsigned char)(v >> 16);
    b[2] = (unsigned char)(v >>  8);
    b[3] = (unsigned char)(v);
}

static uint32_t load_le32(const void *p)
{
    const unsigned char *b = (const unsigned char *)p;
    return ((uint32_t)b[3] << 24) | ((uint32_t)b[2] << 16) |
           ((uint32_t)b[1] <<  8) | ((uint32_t)b[0]);
}

static void store_le32(void *p, uint32_t v)
{
    unsigned char *b = (unsigned char *)p;
    b[0] = (unsigned char)(v);
    b[1] = (unsigned char)(v >>  8);
    b[2] = (unsigned char)(v >> 16);
    b[3] = (unsigned char)(v >> 24);
}

/* host 바이트 순서 그대로, 정렬만 신경 쓰지 않고 읽고 쓴다.
 * 구조체를 바이트 버퍼에 통째로 담을 때 쓴다(엔디언 변환은 없다). */
static uint32_t load32_native(const void *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof v);          /* 정렬 요구 없음, aliasing 위반 없음 */
    return v;
}

static void store32_native(void *p, uint32_t v)
{
    memcpy(p, &v, sizeof v);
}

/* ------------------------------------------------------------------ */
/* 4. 정렬                                                             */
/* ------------------------------------------------------------------ */

/* a 는 2의 거듭제곱이어야 한다. 그래야 a - 1 이 하위 마스크가 된다. */
static bool is_pow2_size(size_t a)
{
    return a != 0u && (a & (a - 1u)) == 0u;
}

/* v 를 a 의 배수로 올린다. 이미 배수면 그대로.
 *
 *   v = 13, a = 8:  13 + 7 = 20 = 0b10100,  ~(8-1) = ...11000
 *                   20 & ~7 = 16
 *
 * 나눗셈 없는 이유: (v + a - 1) / a * a 는 나눗셈 두 번이고,
 * Cortex-M0 에는 하드웨어 나눗셈이 없어 수십 사이클이 든다. */
static size_t align_up(size_t v, size_t a)
{
    assert(is_pow2_size(a));
    assert(v <= SIZE_MAX - (a - 1u));       /* 올림 자체가 넘치면 안 된다 */
    return (v + (a - 1u)) & ~(a - 1u);
}

static size_t align_down(size_t v, size_t a)
{
    assert(is_pow2_size(a));
    return v & ~(a - 1u);
}

static bool is_aligned(size_t v, size_t a)
{
    assert(is_pow2_size(a));
    return (v & (a - 1u)) == 0u;
}

/* 포인터 정렬 확인. uintptr_t 를 거치는 것이 표준이 인정하는 유일한 방법이다.
 * (주소를 정수로 보는 것은 구현 정의지만 uintptr_t 왕복은 보장된다) */
static bool ptr_is_aligned(const void *p, size_t a)
{
    assert(is_pow2_size(a));
    return ((uintptr_t)p & (uintptr_t)(a - 1u)) == 0u;
}

/* 포인터를 a 경계로 올린다. 풀 할당자에서 블록 시작 주소를 맞출 때 쓴다.
 * 주의: 원래 객체 범위를 넘어선 포인터를 만들면 UB 다.
 * 그래서 호출자가 버퍼 끝을 넘지 않는지 확인해야 한다(아래 pool 예시 참고). */
static void *ptr_align_up(void *p, size_t a)
{
    uintptr_t u = (uintptr_t)p;
    assert(is_pow2_size(a));
    u = (u + (uintptr_t)(a - 1u)) & ~(uintptr_t)(a - 1u);
    return (void *)u;
}

/* ------------------------------------------------------------------ */
/* 5. 테스트                                                            */
/* ------------------------------------------------------------------ */
#define OK(msg) printf("  ok  %s\n", (msg))

static void test_bswap(void)
{
    assert(bswap16(0x0000u) == 0x0000u);
    assert(bswap16(0x00FFu) == 0xFF00u);
    assert(bswap16(0xFF00u) == 0x00FFu);
    assert(bswap16(0x1234u) == 0x3412u);
    assert(bswap16(0xFFFFu) == 0xFFFFu);
    assert(bswap16(0x8000u) == 0x0080u);

    assert(bswap32(0x00000000u) == 0x00000000u);
    assert(bswap32(0xFFFFFFFFu) == 0xFFFFFFFFu);
    assert(bswap32(0x000000FFu) == 0xFF000000u);
    assert(bswap32(0x12345678u) == 0x78563412u);
    assert(bswap32(0xDEADBEEFu) == 0xEFBEADDEu);
    assert(bswap32(0x80000000u) == 0x00000080u);

    assert(bswap64((uint64_t)0) == (uint64_t)0);
    assert(bswap64(0xFFFFFFFFFFFFFFFFu) == 0xFFFFFFFFFFFFFFFFu);
    assert(bswap64(0x0123456789ABCDEFu) == 0xEFCDAB8967452301u);
    assert(bswap64(0x00000000000000FFu) == 0xFF00000000000000u);

    /* 두 번 스왑하면 원본 (involution) */
    uint32_t s = 0x13579BDFu;
    for (int i = 0; i < 1000; i++) {
        s = (s * 1664525u) + 1013904223u;
        assert(bswap32(bswap32(s)) == s);
        assert(bswap16(bswap16((uint16_t)s)) == (uint16_t)s);
        uint64_t w = ((uint64_t)s << 32) | (uint64_t)~s;
        assert(bswap64(bswap64(w)) == w);
    }
    OK("bswap16/32/64: 0, 전체 1, 최상위 바이트, 왕복 1000회");
}

static void test_endian_detect(void)
{
    endian_t e = host_endian();
    assert(e == ENDIAN_LITTLE || e == ENDIAN_BIG);

    /* 판별 결과와 실제 메모리 배치가 일치하는지 교차 확인 */
    uint32_t v = 0x04030201u;
    unsigned char b[4];
    memcpy(b, &v, sizeof b);
    if (e == ENDIAN_LITTLE) {
        assert(b[0] == 0x01u && b[3] == 0x04u);
        assert(load_le32(b) == v);            /* LE host: LE 로 읽으면 원본 */
        assert(load_be32(b) == bswap32(v));
    } else {
        assert(b[0] == 0x04u && b[3] == 0x01u);
        assert(load_be32(b) == v);
        assert(load_le32(b) == bswap32(v));
    }
    printf("      host = %s endian\n", (e == ENDIAN_LITTLE) ? "little" : "big");
    OK("host_endian: 판별 결과와 실제 바이트 배치가 일치");
}

static void test_byte_order_accessors(void)
{
    unsigned char b[8] = {0xDEu, 0xADu, 0xBEu, 0xEFu, 0x00u, 0x00u, 0x00u, 0x00u};

    /* 같은 4바이트를 해석만 바꿔 읽는다 */
    assert(load_be32(b) == 0xDEADBEEFu);
    assert(load_le32(b) == 0xEFBEADDEu);
    assert(load_be16(b) == 0xDEADu);
    assert(load_be32(b) == bswap32(load_le32(b)));

    /* store -> load 왕복 */
    store_be32(b + 4, 0x01020304u);
    assert(b[4] == 0x01u && b[5] == 0x02u && b[6] == 0x03u && b[7] == 0x04u);
    assert(load_be32(b + 4) == 0x01020304u);

    store_le32(b + 4, 0x01020304u);
    assert(b[4] == 0x04u && b[5] == 0x03u && b[6] == 0x02u && b[7] == 0x01u);
    assert(load_le32(b + 4) == 0x01020304u);

    /* 경계값 */
    store_be32(b, 0x00000000u);
    assert(load_be32(b) == 0u);
    store_be32(b, 0xFFFFFFFFu);
    assert(load_be32(b) == 0xFFFFFFFFu);
    store_le32(b, 0x80000000u);
    assert(load_le32(b) == 0x80000000u);
    assert(b[3] == 0x80u && b[0] == 0x00u);
    OK("load/store be32·le32·be16: 해석 교차 확인과 왕복");
}

/* 정렬이 어긋난 주소에서의 접근.
 * _Alignas(8) 로 시작 주소를 확실히 8의 배수로 잡았으니
 * buf + 1, buf + 2, buf + 3 은 4바이트 정렬이 아님이 보장된다. */
static void test_unaligned_access(void)
{
    _Alignas(8) unsigned char buf[32];
    memset(buf, 0, sizeof buf);

    assert(ptr_is_aligned(buf, 8));
    assert(ptr_is_aligned(buf, 4));
    assert(!ptr_is_aligned(buf + 1, 4));      /* 정렬 어긋남 */
    assert(!ptr_is_aligned(buf + 2, 4));
    assert(!ptr_is_aligned(buf + 3, 4));
    assert(ptr_is_aligned(buf + 4, 4));
    assert(ptr_is_aligned(buf + 2, 2));
    assert(!ptr_is_aligned(buf + 1, 2));
    assert(ptr_is_aligned(buf + 1, 1));       /* 1바이트 정렬은 항상 참 */

    /* 어긋난 주소에서도 바이트 조립 접근은 전부 정상 동작한다 */
    for (size_t off = 0; off < 5u; off++) {
        store_be32(buf + off, 0xA1B2C3D4u);
        assert(load_be32(buf + off) == 0xA1B2C3D4u);
        assert(buf[off] == 0xA1u && buf[off + 3] == 0xD4u);

        store_le32(buf + off, 0xA1B2C3D4u);
        assert(load_le32(buf + off) == 0xA1B2C3D4u);

        store32_native(buf + off, 0xCAFEF00Du);
        assert(load32_native(buf + off) == 0xCAFEF00Du);
    }

    /* memcpy 기반 접근은 host 엔디언을 따른다 = 스트림 포맷에 쓰면 안 된다 */
    store32_native(buf + 1, 0x11223344u);
    if (host_endian() == ENDIAN_LITTLE) {
        assert(load_le32(buf + 1) == 0x11223344u);
        assert(load_be32(buf + 1) == 0x44332211u);
    } else {
        assert(load_be32(buf + 1) == 0x11223344u);
    }

    /* 이웃 바이트를 덮지 않는지 확인 */
    memset(buf, 0x5Au, sizeof buf);
    store_be32(buf + 5, 0x00000000u);
    assert(buf[4] == 0x5Au);
    assert(buf[5] == 0x00u && buf[8] == 0x00u);
    assert(buf[9] == 0x5Au);
    OK("unaligned: offset 0~4 전부 정상, 이웃 바이트 무손상");
}

static void test_align_math(void)
{
    assert(align_up(0, 4) == 0u);
    assert(align_up(1, 4) == 4u);
    assert(align_up(3, 4) == 4u);
    assert(align_up(4, 4) == 4u);             /* 이미 배수면 그대로 */
    assert(align_up(5, 4) == 8u);
    assert(align_up(13, 8) == 16u);
    assert(align_up(16, 16) == 16u);
    assert(align_up(17, 16) == 32u);
    assert(align_up(123, 1) == 123u);         /* a == 1 은 항상 항등 */

    assert(align_down(0, 4) == 0u);
    assert(align_down(3, 4) == 0u);
    assert(align_down(4, 4) == 4u);
    assert(align_down(7, 4) == 4u);
    assert(align_down(4095, 4096) == 0u);
    assert(align_down(4096, 4096) == 4096u);

    assert(is_aligned(0, 8));
    assert(!is_aligned(4, 8));
    assert(is_aligned(8, 8));
    assert(is_aligned(7, 1));

    /* 불변식: align_down <= v <= align_up,  둘 다 a 의 배수,
     *         align_up - v < a */
    for (size_t v = 0; v < 200u; v++) {
        for (size_t a = 1u; a <= 64u; a <<= 1) {
            size_t up = align_up(v, a);
            size_t dn = align_down(v, a);
            assert(dn <= v && v <= up);
            assert(is_aligned(up, a) && is_aligned(dn, a));
            assert(up - v < a);
            assert(v - dn < a);
            assert(is_aligned(v, a) ? (up == v && dn == v) : (up == dn + a));
        }
    }

    /* SIZE_MAX 근처 경계: 이미 정렬된 최대값은 그대로 통과해야 한다 */
    assert(align_down(SIZE_MAX, 8) == (SIZE_MAX & ~(size_t)7u));
    assert(align_up(SIZE_MAX & ~(size_t)7u, 8) == (SIZE_MAX & ~(size_t)7u));
    OK("align_up / align_down / is_aligned: 0, 배수, 경계, a=1, SIZE_MAX 근처");
}

/* 실전 모양 (a): 정렬된 블록을 잘라 주는 아주 작은 bump 할당자.
 * ptr_align_up 을 쓰되, 버퍼 끝을 넘는 포인터는 만들지 않는다. */
typedef struct {
    unsigned char *base;
    size_t size;
    size_t used;
} bump_t;

static void *bump_alloc(bump_t *b, size_t n, size_t a)
{
    size_t start = align_up(b->used, a);
    if (start > b->size || n > b->size - start) {
        return NULL;                          /* 오프셋으로 검사 = 포인터 UB 없음 */
    }
    b->used = start + n;
    return b->base + start;
}

static void test_bump_allocator(void)
{
    _Alignas(8) unsigned char arena[64];
    bump_t b = {arena, sizeof arena, 0};

    void *p1 = bump_alloc(&b, 1, 1);
    assert(p1 == arena);

    void *p4 = bump_alloc(&b, 4, 4);          /* used=1 -> 4 로 올림 */
    assert(p4 == arena + 4);
    assert(ptr_is_aligned(p4, 4));
    assert(b.used == 8u);

    void *p8 = bump_alloc(&b, 8, 8);
    assert(p8 == arena + 8);
    assert(ptr_is_aligned(p8, 8));

    /* 용량 초과는 NULL. 포인터 연산으로 넘겨보지 않는다 */
    assert(bump_alloc(&b, 64, 8) == NULL);
    assert(b.used == 16u);                    /* 실패해도 상태는 그대로 */

    void *rest = bump_alloc(&b, 48, 16);
    assert(rest == arena + 16);
    assert(b.used == 64u);
    assert(bump_alloc(&b, 1, 1) == NULL);     /* 딱 맞게 소진 */

    /* ptr_align_up 자체 확인 */
    assert(ptr_align_up(arena, 8) == arena);
    assert(ptr_align_up(arena + 1, 8) == arena + 8);
    assert(ptr_align_up(arena + 8, 8) == arena + 8);
    assert(ptr_align_up(arena + 9, 4) == arena + 12);
    OK("bump 할당자: align_up 으로 정렬 보장, 용량 초과는 NULL");
}

/* 실전 모양 (b): big-endian 와이어 포맷 헤더를 정렬 가정 없이 파싱한다.
 *
 *  offset 0   1      3         7        9
 *        +----+------+---------+--------+-----------------+
 *        | ver| len  | seq(BE) | crc(BE)|   payload ...   |
 *        +----+------+---------+--------+-----------------+
 *          u8   u16BE   u32BE    u16BE
 *
 * seq 는 offset 3 에 있다 = 절대로 4바이트 정렬이 아니다.
 * 그래서 (uint32_t*)(p + 3) 캐스트는 타깃에서 HardFault 를 낼 수 있다. */
typedef struct {
    uint8_t  ver;
    uint16_t len;
    uint32_t seq;
    uint16_t crc;
} wire_hdr_t;

static bool wire_parse(const unsigned char *p, size_t n, wire_hdr_t *out)
{
    if (n < 9u) {
        return false;
    }
    out->ver = p[0];
    out->len = load_be16(p + 1);
    out->seq = load_be32(p + 3);       /* offset 3: unaligned 하지만 안전 */
    out->crc = load_be16(p + 7);
    return true;
}

static void test_wire_format(void)
{
    /* 버퍼 자체를 1바이트 밀어 넣어 "구조체 정렬"에 기대지 못하게 한다 */
    _Alignas(8) unsigned char raw[24];
    /* raw + 3 에서 시작하므로 pkt 는 홀수 주소이고, pkt + 3 (seq 필드)은
     * raw + 6 = 4의 배수가 아니다. 즉 seq 는 확실히 unaligned 다. */
    unsigned char *pkt = raw + 3;
    memset(raw, 0, sizeof raw);

    pkt[0] = 0x02u;
    pkt[1] = 0x01u; pkt[2] = 0x00u;                       /* len = 0x0100 */
    store_be32(pkt + 3, 0xFFFFFFFFu);                     /* seq = 최대값 */
    pkt[7] = 0xABu; pkt[8] = 0xCDu;                       /* crc */

    wire_hdr_t h;
    assert(!ptr_is_aligned(pkt, 4));
    assert(!ptr_is_aligned(pkt, 2));
    assert(!ptr_is_aligned(pkt + 3, 4));      /* seq 필드가 정렬 어긋남 */
    assert(wire_parse(pkt, 9, &h));
    assert(h.ver == 0x02u);
    assert(h.len == 0x0100u);
    assert(h.seq == 0xFFFFFFFFu);
    assert(h.crc == 0xABCDu);

    /* seq = 0 과 seq = 1 경계도 확인 */
    store_be32(pkt + 3, 0u);
    assert(wire_parse(pkt, 9, &h) && h.seq == 0u);
    store_be32(pkt + 3, 1u);
    assert(wire_parse(pkt, 9, &h) && h.seq == 1u);

    /* 너무 짧은 버퍼는 거부 */
    assert(!wire_parse(pkt, 8, &h));
    assert(!wire_parse(pkt, 0, &h));

    /* 같은 헤더를 다시 직렬화하면 바이트가 원본과 동일해야 한다 */
    unsigned char out[9];
    out[0] = h.ver;
    out[1] = (unsigned char)(h.len >> 8);
    out[2] = (unsigned char)(h.len);
    store_be32(out + 3, h.seq);
    out[7] = (unsigned char)(h.crc >> 8);
    out[8] = (unsigned char)(h.crc);
    assert(memcmp(out, pkt, 9) == 0);

    /* 구조체 크기는 패딩 때문에 9 가 아니다 = 스트림 길이로 쓰면 안 된다 */
    assert(sizeof(wire_hdr_t) >= 9u);
    OK("와이어 포맷: 홀수 주소 헤더 파싱, 재직렬화 바이트 일치, 길이 검증");
}

int main(void)
{
    printf("08_endian_align (solution)\n");
    test_bswap();
    test_endian_detect();
    test_byte_order_accessors();
    test_unaligned_access();
    test_align_math();
    test_bump_allocator();
    test_wire_format();
    printf("ALL TESTS PASSED\n");
    return 0;
}
