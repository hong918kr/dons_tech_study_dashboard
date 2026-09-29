#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

/* ---- 1. 네 구역의 주소 ---- */
static int  g_bss;             /* 초기값 없음 -> BSS */
static int  g_data = 7;        /* 초기값 있음 -> data */

static void demo_regions(void)
{
    int local = 1;
    static int s_local = 2;
    int *heap = malloc(sizeof *heap);
    *heap = 3;
    printf("g_data(data)  %p\n", (void *)&g_data);
    printf("g_bss (bss)   %p\n", (void *)&g_bss);
    printf("s_local(data) %p\n", (void *)&s_local);
    printf("heap          %p\n", (void *)heap);
    printf("local(stack)  %p\n", (void *)&local);
    free(heap);
}

/* ---- 2. 수명 ---- */
static const char *bad_name(void)
{
    char buf[16];
    snprintf(buf, sizeof buf, "trailer-7");
    return buf;                /* 죽은 스택을 가리킨다 */
}

static const char *ok_name(void)
{
    static char buf[16];       /* 프로그램이 끝날 때까지 산다 */
    snprintf(buf, sizeof buf, "trailer-7");
    return buf;
}

/* ---- 3. sizeof / alignment / padding ---- */
struct LuxSample  { uint64_t ts; float lux; };      /* 8 + 4 + pad4 = 16 */
struct LuxPacked  { float lux; uint64_t ts; };      /* 4 + pad4 + 8 = 16 */
struct ThreeBytes { uint8_t a, b, c; };             /* 3 */
struct ApInfo     { uint8_t bssid[6]; char ssid[33]; int8_t rssi; uint8_t ch; }; /* 41 */
struct GpsFix     { int status; double lat, lon; float hdop; uint64_t timestamp; };

static void demo_sizeof(void)
{
    printf("LuxSample  size=%zu align=%zu  off(ts)=%zu off(lux)=%zu\n",
           sizeof(struct LuxSample), _Alignof(struct LuxSample),
           offsetof(struct LuxSample, ts), offsetof(struct LuxSample, lux));
    printf("LuxPacked  size=%zu align=%zu  off(lux)=%zu off(ts)=%zu\n",
           sizeof(struct LuxPacked), _Alignof(struct LuxPacked),
           offsetof(struct LuxPacked, lux), offsetof(struct LuxPacked, ts));
    printf("ThreeBytes size=%zu align=%zu\n",
           sizeof(struct ThreeBytes), _Alignof(struct ThreeBytes));
    printf("ApInfo     size=%zu align=%zu\n",
           sizeof(struct ApInfo), _Alignof(struct ApInfo));
    printf("GpsFix     size=%zu align=%zu  off(lat)=%zu off(hdop)=%zu off(ts)=%zu\n",
           sizeof(struct GpsFix), _Alignof(struct GpsFix),
           offsetof(struct GpsFix, lat), offsetof(struct GpsFix, hdop),
           offsetof(struct GpsFix, timestamp));
    printf("8192 * %zu B = %zu KiB\n",
           sizeof(struct LuxSample), 8192u * sizeof(struct LuxSample) / 1024u);
}

/* ---- 4. 비트 연산 ---- */
#define BIT(n) (1u << (n))
#define FLAG_VALID   BIT(0)
#define FLAG_STALE   BIT(1)
#define FLAG_EVICTED BIT(2)

static void demo_bits(void)
{
    uint32_t f = 0;
    f |= FLAG_VALID;                     /* set    */
    f |= FLAG_STALE;
    printf("set     f=0x%02x\n", f);
    f &= ~FLAG_STALE;                    /* clear  */
    printf("clear   f=0x%02x\n", f);
    f ^= FLAG_EVICTED;                   /* toggle */
    printf("toggle  f=0x%02x\n", f);
    printf("test VALID=%d STALE=%d\n", (f & FLAG_VALID) != 0, (f & FLAG_STALE) != 0);

    uint32_t mask3 = 0x7u;               /* 하위 3비트만 남기는 마스크 */
    printf("0xAB & 0x7 = 0x%x\n", 0xABu & mask3);
}

/* ---- 5. & (N-1) == % N ---- */
#define CAP 8192u
static void demo_mask(void)
{
    _Static_assert((CAP & (CAP - 1u)) == 0u, "CAP must be a power of two");
    for (uint32_t i = 0; i < 100000u; i++)
        assert((i & (CAP - 1u)) == (i % CAP));
    printf("head=8190..8194 -> slot %u %u %u %u %u\n",
           8190u & (CAP-1u), 8191u & (CAP-1u), 8192u & (CAP-1u),
           8193u & (CAP-1u), 8194u & (CAP-1u));
}

/* ---- 6. pack / unpack (02번 방식) ---- */
#define PUB_RSSI_BITS 16
#define PUB_RSSI_MASK 0xFFFFull

static uint64_t pack(uint64_t rel_us, int rssi_dbm)
{
    return (rel_us << PUB_RSSI_BITS) | (uint64_t)(uint16_t)(int16_t)rssi_dbm;
}
static void unpack(uint64_t word, uint64_t *rel_us, int *rssi_dbm)
{
    *rel_us   = word >> PUB_RSSI_BITS;
    *rssi_dbm = (int16_t)(uint16_t)(word & PUB_RSSI_MASK);
}

static void demo_pack(void)
{
    uint64_t w = pack(1234567ull, -97);
    uint64_t rel; int rssi;
    unpack(w, &rel, &rssi);
    printf("word=0x%016llx -> rel=%llu rssi=%d\n",
           (unsigned long long)w, (unsigned long long)rel, rssi);
    printf("48비트 rel 최대 = %llu us = %.1f 년\n",
           (unsigned long long)(UINT64_MAX >> PUB_RSSI_BITS),
           (double)(UINT64_MAX >> PUB_RSSI_BITS) / 1e6 / 86400.0 / 365.0);
}

/* ---- 7. 시프트와 부호 ---- */
static void demo_shift(void)
{
    int      neg = -8;
    unsigned u   = 0x80000000u;
    printf("-8 >> 1 = %d (산술 시프트: 부호 유지)\n", neg >> 1);
    printf("0x80000000u >> 1 = 0x%x (논리 시프트)\n", u >> 1);
    printf("1u  << 31 = 0x%x\n", 1u << 31);
    printf("(uint64_t)1 << 40 = %llu\n", (unsigned long long)((uint64_t)1 << 40));
    /* 1 << 40 은 int 시프트라 UB. 1 << 31 도 int 에서는 UB. */
}

/* ---- 8. 엔디안 ---- */
static void demo_endian(void)
{
    uint32_t v = 0x01020304u;
    unsigned char b[4];
    memcpy(b, &v, sizeof b);
    printf("메모리 바이트: %02x %02x %02x %02x -> %s\n",
           b[0], b[1], b[2], b[3], (b[0] == 0x04) ? "little-endian" : "big-endian");
}

static uint32_t be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] <<  8) | ((uint32_t)p[3]);
}

static void demo_be(void)
{
    const unsigned char wire[4] = {0x00, 0x00, 0x1f, 0x40};
    printf("be32(00 00 1f 40) = %u\n", be32(wire));
}

/* ---- 9. float 비트를 정수로 (원본 ALS Part 1) ---- */
static uint32_t float_bits(float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof bits);
    return bits;
}
static float bits_float(uint32_t bits)
{
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

/* ---- 10. volatile 맛보기 ---- */
static volatile int g_flag;

int main(void)
{
    demo_regions();
    printf("ok_name = \"%s\"\n", ok_name());
    demo_sizeof();
    demo_bits();
    demo_mask();
    demo_pack();
    demo_shift();
    demo_endian();
    demo_be();
    printf("float 123.5f bits = 0x%08x, 되돌리면 %.1f\n",
           float_bits(123.5f), (double)bits_float(float_bits(123.5f)));
    g_flag = 1;
    printf("flag=%d\n", g_flag);
    (void)bad_name;
    return 0;
}
