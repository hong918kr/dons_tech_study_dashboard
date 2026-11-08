/* C-B12 추가 연습 — 한 번에 검증 (LP64 / ILP32 Cortex-M 공통으로 성립하는 값만 고정) */
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 연습 1: 센서 샘플 링버퍼의 SRAM 예산 — 재배열로 1 KB 절약 */
struct sample     { uint8_t ch; uint32_t ts; uint8_t flags; uint16_t val; };   /* 0,4,8,10 → 12 */
struct sample_min { uint32_t ts; uint16_t val; uint8_t ch; uint8_t flags; };   /* 0,4,6,7  → 8  */
#define RING_N 256u
_Static_assert(sizeof(struct sample) == 12, "sample");
_Static_assert(sizeof(struct sample_min) == 8, "sample_min");

/* 연습 2: 플렉서블 배열 멤버(FAM)와 꼬리 패딩 */
struct pkt { uint16_t len; uint8_t type; uint8_t data[]; };   /* data @3, sizeof 4 */
static size_t pkt_alloc_size(size_t n) {
    size_t need = offsetof(struct pkt, data) + n;               /* 정확히 필요한 바이트 */
    return need > sizeof(struct pkt) ? need : sizeof(struct pkt);
}

/* 연습 3: 캐시 라인(32 B) 정렬 DMA 디스크립터 — Cortex-M7 D-cache 유지보수 단위 */
struct dma_desc {
    uint32_t addr;
    uint16_t len;
    uint8_t  flags;
    _Alignas(32) uint8_t rx[64];   /* 이 버퍼만 캐시 라인 경계에서 시작 */
};
_Static_assert(alignof(struct dma_desc) == 32, "desc align");
_Static_assert(offsetof(struct dma_desc, rx) == 32, "rx @32");
_Static_assert(sizeof(struct dma_desc) == 96, "desc size");
static struct dma_desc ring[4];

/* 연습 4: 중첩 구조체 손 계산 (IMU 패킷) */
struct imu {
    uint8_t id;
    struct { int16_t x, y, z; } acc;   /* size 6, align 2 */
    uint32_t ts;
    struct { float x, y, z; } gyro;    /* size 12, align 4 */
};
_Static_assert(offsetof(struct imu, acc) == 2, "acc");
_Static_assert(offsetof(struct imu, ts) == 8, "ts");
_Static_assert(offsetof(struct imu, gyro) == 12, "gyro");
_Static_assert(sizeof(struct imu) == 24 && alignof(struct imu) == 4, "imu");

/* 연습 5: 패딩이 있는 구조체 비교 — memcmp 대신 필드별 비교 */
struct s1 { uint8_t a; uint32_t b; uint8_t c; };
static bool s1_equal(const struct s1 *x, const struct s1 *y) {
    return x->a == y->a && x->b == y->b && x->c == y->c;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 1 */
    CHECK(offsetof(struct sample, ts) == 4 && offsetof(struct sample, val) == 10);
    CHECK(RING_N * (sizeof(struct sample) - sizeof(struct sample_min)) == 1024u);

    /* 2 */
    CHECK(offsetof(struct pkt, data) == 3 && sizeof(struct pkt) == 4);
    CHECK(pkt_alloc_size(0) == 4);     /* sizeof 보다 작게 잡지 않는다 */
    CHECK(pkt_alloc_size(1) == 4);
    CHECK(pkt_alloc_size(10) == 13);   /* sizeof(struct pkt) + 10 = 14 는 1바이트 낭비 */

    /* 3 */
    CHECK(((uintptr_t)&ring[1].rx & 31u) == 0);
    CHECK(((uintptr_t)&ring[3] & 31u) == 0);
    CHECK((char *)&ring[1] - (char *)&ring[0] == 96);

    /* 4 */
    struct imu m = { .id = 7, .acc = { 1, -2, 3 }, .ts = 1000u, .gyro = { 0.5f, 0.f, -0.5f } };
    CHECK(m.acc.y == -2 && m.gyro.z == -0.5f);

    /* 5 */
    struct s1 p, q;
    memset(&p, 0xAA, sizeof p);        /* 패딩 바이트에 서로 다른 쓰레기 */
    memset(&q, 0x55, sizeof q);
    p.a = q.a = 1; p.b = q.b = 0x12345678u; p.c = q.c = 9;
    CHECK(s1_equal(&p, &q));
    q.b++;
    CHECK(!s1_equal(&p, &q));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
