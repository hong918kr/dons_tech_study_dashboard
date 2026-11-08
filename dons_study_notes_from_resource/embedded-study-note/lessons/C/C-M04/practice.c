/* C-M04 추가 연습 — packed 변형 레이아웃 / get_unaligned 헬퍼 / packed 배열 멤버 /
 *                    정렬이 맞는 오프셋 / packed 와 memcmp
 * 리틀 엔디안 호스트 기준 (엔디안 독립 버전은 C-M05).
 */
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "리틀 엔디안 호스트 기준"
#endif

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x1234ABCDu;
static uint32_t xorshift32(void) {
    uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x;
}

/* ---------- 연습 1: packed 의 변형들 ---------- */
struct __attribute__((packed)) p1 { uint8_t a; uint32_t b; };
struct __attribute__((packed, aligned(4))) p2 { uint8_t a; uint32_t b; };
#pragma pack(push, 2)
struct p3 { uint8_t a; uint32_t b; uint8_t c; };
#pragma pack(pop)
struct p4 { uint8_t a; uint32_t b __attribute__((packed)); uint8_t c; };   /* 멤버 하나만 packed */

/* ---------- 연습 2: Linux 스타일 get/put_unaligned (packed 래퍼 구조체) ---------- */
struct __attribute__((packed)) una_u16 { uint16_t x; };
struct __attribute__((packed)) una_u32 { uint32_t x; };
struct __attribute__((packed)) una_u64 { uint64_t x; };

static uint16_t get_unaligned_u16(const void *p) { return ((const struct una_u16 *)p)->x; }
static uint32_t get_unaligned_u32(const void *p) { return ((const struct una_u32 *)p)->x; }
static uint64_t get_unaligned_u64(const void *p) { return ((const struct una_u64 *)p)->x; }
static void put_unaligned_u32(void *p, uint32_t v) { ((struct una_u32 *)p)->x = v; }

/* ---------- 연습 3: packed 구조체 안의 배열 ---------- */
struct __attribute__((packed)) log_rec { uint8_t id; uint16_t s[4]; };   /* s 는 오프셋 1 */

/* 주어진 API: 정렬된 uint16_t 배열을 가정 */
static uint32_t sum_u16(const uint16_t *p, size_t n) {
    uint32_t t = 0;
    for (size_t i = 0; i < n; i++) t += p[i];
    return t;
}
static uint32_t rec_sum(const struct log_rec *r) {
    uint16_t tmp[4];
    memcpy(tmp, r->s, sizeof tmp);      /* sum_u16(r->s, 4) 는 경고 없이 비정렬 포인터를 넘긴다 */
    return sum_u16(tmp, 4);
}

/* ---------- 연습 4: 헤더(1+2+4)를 어느 주소에 두면 모든 필드가 자연 정렬인가 ---------- */
static int misaligned_fields(uintptr_t addr) {
    int m = 0;
    if ((addr + 1) % 2 != 0) m++;       /* len: 주소 addr+1, 정렬 2 */
    if ((addr + 3) % 4 != 0) m++;       /* seq: 주소 addr+3, 정렬 4 */
    return m;
}

/* ---------- 연습 5: packed 구조체는 memcmp 해도 되나 ---------- */
typedef struct { uint8_t type; uint16_t len; uint32_t seq; } hdr_t;          /* 패딩 1 B */
struct __attribute__((packed)) wire_hdr { uint8_t type; uint16_t len; uint32_t seq; };

static void hdr_to_wire(const hdr_t *h, struct wire_hdr *w) {
    w->type = h->type;
    w->len  = h->len;
    w->seq  = h->seq;
}

int main(void) {
    /* 연습 1 */
    CHECK(sizeof(struct p1) == 5 && alignof(struct p1) == 1);
    CHECK(sizeof(struct p2) == 8 && alignof(struct p2) == 4 && offsetof(struct p2, b) == 1);
    CHECK(sizeof(struct p3) == 8 && alignof(struct p3) == 2
          && offsetof(struct p3, b) == 2 && offsetof(struct p3, c) == 6);
    CHECK(sizeof(struct p4) == 6 && alignof(struct p4) == 1
          && offsetof(struct p4, b) == 1 && offsetof(struct p4, c) == 5);

    /* 연습 2: 무작위 오프셋 500회, memcpy 참조와 비교 */
    {
        alignas(8) uint8_t b[40];
        int ok = 0;
        for (int it = 0; it < 500; it++) {
            for (size_t i = 0; i < sizeof b; i++) b[i] = (uint8_t)xorshift32();
            size_t off = xorshift32() % 32;
            uint16_t r16; uint32_t r32; uint64_t r64;
            memcpy(&r16, b + off, 2); memcpy(&r32, b + off, 4); memcpy(&r64, b + off, 8);
            ok += get_unaligned_u16(b + off) == r16 && get_unaligned_u32(b + off) == r32
               && get_unaligned_u64(b + off) == r64;
        }
        CHECK(ok == 500);
        memset(b, 0, sizeof b);
        put_unaligned_u32(b + 3, 0xA1B2C3D4u);
        CHECK(b[2] == 0 && b[3] == 0xD4 && b[6] == 0xA1 && b[7] == 0);
    }

    /* 연습 3: 레코드를 홀수 오프셋에 두고 합계 */
    {
        alignas(4) uint8_t raw[16] = { 0 };
        const uint8_t img[9] = { 7, 1, 0, 2, 0, 3, 0, 0x10, 0x27 };   /* s = {1,2,3,10000} */
        memcpy(raw + 3, img, sizeof img);
        CHECK(sizeof(struct log_rec) == 9 && offsetof(struct log_rec, s) == 1);
        CHECK(rec_sum((const struct log_rec *)(raw + 3)) == 10006u);
        CHECK(((const struct log_rec *)(raw + 3))->s[3] == 10000);   /* 멤버 접근은 OK */
    }

    /* 연습 4 */
    CHECK(misaligned_fields(0x20000000u) == 2);
    CHECK(misaligned_fields(0x20000001u) == 0);       /* sync 1바이트 뒤 = 정답 */
    CHECK(misaligned_fields(0x20000002u) == 2);
    CHECK(misaligned_fields(0x20000003u) == 1);

    /* 연습 5 */
    {
        hdr_t h;
        struct wire_hdr w;
        memset(&h, 0xAA, sizeof h);                    /* 패딩까지 쓰레기로 */
        memset(&w, 0xAA, sizeof w);
        h.type = 2; h.len = 0x1234; h.seq = 0x12345678u;
        hdr_to_wire(&h, &w);
        static const uint8_t exp[7] = { 0x02, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12 };
        CHECK(sizeof w == 7 && memcmp(&w, exp, 7) == 0);   /* 모든 바이트가 필드 → 결정적 */
        CHECK(sizeof h == 8 && offsetof(hdr_t, len) == 2);  /* hdr_t 는 1 B 패딩 → memcmp 금지 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
