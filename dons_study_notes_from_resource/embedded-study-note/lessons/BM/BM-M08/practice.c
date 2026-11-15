/* BM-M08 추가 연습 — 한 번에 검증
 *
 * 연습 1 — 플래그 위치를 분기 없이: off = ((s & 2) << 3) + (s & 1) * 6, 그리고 "TC 가 선 스트림 목록"
 * 연습 2 — abort 뒤 이어 보내기: NDTR 로 보낸 양을 계산해 남은 부분만 다시 설정
 * 연습 3 — 임의 길이·정렬의 M→M 복사 계획: word/halfword/byte 조각 + NDTR 65535 한계
 * 연습 4 — 스트림 중재: PL 높은 것, 같으면 번호 낮은 것 (전수 vs 다른 방식)
 * 연습 5 — 바이트당 IRQ vs DMA HT/TC IRQ, 반 버퍼 마감 시간
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs = 0x1234567u;
static uint32_t xorshift32(void) { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }

/* ---------- 연습 1 ---------- */
#define DMA_TCIF 5u
#define DMA_GROUP_MASK 0x3Du
static unsigned flag_bit_alu(unsigned s, unsigned f) { return ((s & 2u) << 3) + (s & 1u) * 6u + f; }
static uint32_t stream_mask(unsigned s) { return DMA_GROUP_MASK << flag_bit_alu(s, 0u); }
/* LISR, HISR 값 → TC 가 선 스트림 비트마스크 (bit s = 스트림 s) */
static unsigned tc_streams(uint32_t lisr, uint32_t hisr) {
    unsigned m = 0;
    for (unsigned s = 0; s < 8u; s++) {
        uint32_t r = s < 4u ? lisr : hisr;
        if ((r >> flag_bit_alu(s, DMA_TCIF)) & 1u) m |= 1u << s;
    }
    return m;
}

/* ---------- 연습 2 ---------- */
/* n 항목 전송을 abort 했더니 NDTR = left. 이어 보낼 주소·길이. 남은 게 없으면 false */
static bool abort_resume(uint32_t m0ar, uint16_t n, uint16_t left, unsigned size_bytes,
                         uint32_t *new_m0ar, uint16_t *new_n) {
    if (left == 0u || left > n) return false;
    *new_m0ar = m0ar + (uint32_t)(n - left) * size_bytes;
    *new_n = left;
    return true;
}

/* ---------- 연습 3 ---------- */
typedef struct { uint32_t src, dst; unsigned size; uint32_t items; } chunk_t;
static unsigned plan_copy(uint32_t src, uint32_t dst, uint32_t len, chunk_t *out, unsigned cap) {
    unsigned k = 0;
    while (len > 0u) {
        unsigned s = 1u;
        if (((src | dst) & 3u) == 0u && len >= 4u) s = 4u;
        else if (((src | dst) & 1u) == 0u && len >= 2u) s = 2u;
        uint32_t items = len / s;
        if (items > 65535u) items = 65535u;                    /* NDTR 은 16비트 */
        if (k >= cap) return 0u;                               /* 용량 부족 = 실패 */
        out[k++] = (chunk_t){ src, dst, s, items };
        src += items * s; dst += items * s; len -= items * s;
    }
    return k;
}
static bool plan_ok(uint32_t src, uint32_t dst, uint32_t len, const chunk_t *c, unsigned k) {
    uint32_t a = src, b = dst, done = 0;
    for (unsigned i = 0; i < k; i++) {
        if (c[i].src != a || c[i].dst != b || c[i].items == 0u || c[i].items > 65535u) return false;
        if ((c[i].src % c[i].size) || (c[i].dst % c[i].size)) return false;
        a += c[i].items * c[i].size; b += c[i].items * c[i].size; done += c[i].items * c[i].size;
    }
    return done == len;
}

/* ---------- 연습 4 ---------- */
static int arbitrate(unsigned req, const uint8_t pl[8]) {      /* 키 = PL·8 + (7 − s) 의 최댓값 */
    int best = -1; unsigned bk = 0;
    for (unsigned s = 0; s < 8u; s++)
        if ((req >> s) & 1u) { unsigned key = pl[s] * 8u + (7u - s); if (best < 0 || key > bk) { best = (int)s; bk = key; } }
    return best;
}
static int arbitrate_ref(unsigned req, const uint8_t pl[8]) {  /* 정의 그대로: PL 3→0, 그 안에서 0→7 */
    for (int p = 3; p >= 0; p--)
        for (unsigned s = 0; s < 8u; s++)
            if (((req >> s) & 1u) && pl[s] == p) return (int)s;
    return -1;
}

/* ---------- 연습 5 ---------- */
static uint32_t chars_per_s(uint32_t baud, uint32_t frame_bits) { return baud / frame_bits; }
static uint32_t dma_irqs_per_s(uint32_t baud, uint32_t frame_bits, uint32_t n) {   /* HT+TC, 올림 */
    uint64_t num = 2ull * baud, den = (uint64_t)frame_bits * n;
    return (uint32_t)((num + den - 1u) / den);
}
static uint32_t half_deadline_us(uint32_t baud, uint32_t frame_bits, uint32_t n) { /* 내림 */
    return (uint32_t)((uint64_t)(n / 2u) * frame_bits * 1000000u / baud);
}

int main(void) {
    /* 연습 1 */
    static const unsigned T[8] = { 0, 6, 16, 22, 0, 6, 16, 22 };
    bool ok = true;
    for (unsigned s = 0; s < 8u; s++) for (unsigned f = 0; f < 6u; f++) ok &= flag_bit_alu(s, f) == T[s] + f;
    CHECK(ok);
    CHECK((stream_mask(0) | stream_mask(1) | stream_mask(2) | stream_mask(3)) == 0x0F7D0F7Du);
    CHECK(stream_mask(2) == 0x003D0000u && stream_mask(7) == 0x0F400000u);
    CHECK(tc_streams(1u << 21, 1u << 27) == ((1u << 2) | (1u << 7)));   /* S2 TC (LISR 21), S7 TC (HISR 27) */
    CHECK(tc_streams(0x0F7D0F7Du & ~0x08200820u, 0u) == 0u);           /* TC 빼고 다 서 있어도 0 */
    unsigned bad = 0;
    for (unsigned b = 0; b < 32u; b++) {                                  /* 비트 하나씩 전수 */
        unsigned m = tc_streams(1u << b, 0u), want = 0;
        for (unsigned s = 0; s < 4u; s++) if (b == T[s] + 5u) want = 1u << s;
        bad += (m != want);
    }
    CHECK(bad == 0u);

    /* 연습 2 — BM-M08 solution 의 실제 abort 숫자: 100 중 NDTR 68 남음 */
    uint32_t na; uint16_t nn;
    CHECK(abort_resume(0x20000300u, 100u, 68u, 1u, &na, &nn) && na == 0x20000320u && nn == 68u);
    CHECK(abort_resume(0x20001000u, 256u, 6u, 4u, &na, &nn) && na == 0x20001000u + 250u * 4u && nn == 6u);
    CHECK(!abort_resume(0x20000300u, 100u, 0u, 1u, &na, &nn));            /* 다 보냈다 */
    CHECK(!abort_resume(0x20000300u, 100u, 101u, 1u, &na, &nn));          /* 불가능한 NDTR = 설정 버그 */

    /* 연습 3 */
    chunk_t c[16];
    unsigned k = plan_copy(0x20000000u, 0x20010000u, 300000u, c, 16);
    CHECK(k == 2u && c[0].size == 4u && c[0].items == 65535u && c[1].items == 9465u);
    k = plan_copy(0x20000001u, 0x20000100u, 100u, c, 16);
    CHECK(k == 1u && c[0].size == 1u && c[0].items == 100u);              /* 정렬이 다르면 끝까지 byte */
    k = plan_copy(0x20000000u, 0x20000100u, 7u, c, 16);
    CHECK(k == 3u && c[0].size == 4u && c[1].size == 2u && c[2].size == 1u);
    k = plan_copy(0x20000002u, 0x20000006u, 10u, c, 16);
    CHECK(k == 1u && c[0].size == 2u && c[0].items == 5u);
    CHECK(plan_copy(0x20000000u, 0x20000100u, 0u, c, 16) == 0u);
    bad = 0;
    for (unsigned sa = 0; sa < 4u; sa++) for (unsigned da = 0; da < 4u; da++) for (uint32_t len = 1; len <= 300u; len++) {
        k = plan_copy(0x20000000u + sa, 0x20004000u + da, len, c, 16);
        bad += !(k > 0u && plan_ok(0x20000000u + sa, 0x20004000u + da, len, c, k));
    }
    for (unsigned i = 0; i < 500u; i++) {
        uint32_t len = 1u + xorshift32() % 1000000u, sa = xorshift32() & 3u, da = xorshift32() & 3u;
        k = plan_copy(0x20000000u + sa, 0x60000000u + da, len, c, 16);
        bad += !(k > 0u && plan_ok(0x20000000u + sa, 0x60000000u + da, len, c, k));
    }
    CHECK(bad == 0u);

    /* 연습 4 */
    bad = 0;
    for (unsigned t = 0; t < 2000u; t++) {
        uint8_t pl[8];
        for (unsigned s = 0; s < 8u; s++) pl[s] = (uint8_t)(xorshift32() & 3u);
        for (unsigned req = 0; req < 256u; req++) bad += arbitrate(req, pl) != arbitrate_ref(req, pl);
    }
    CHECK(bad == 0u);
    const uint8_t pl_ex[8] = { 0, 0, 2, 0, 0, 0, 0, 1 };                 /* S2 = UART RX(2), S7 = UART TX(1) */
    CHECK(arbitrate((1u << 0) | (1u << 2) | (1u << 7), pl_ex) == 2);
    CHECK(arbitrate((1u << 0) | (1u << 7), pl_ex) == 7 && arbitrate((1u << 1) | (1u << 3), pl_ex) == 1);
    CHECK(arbitrate(0u, pl_ex) == -1);

    /* 연습 5 */
    CHECK(chars_per_s(115200u, 10u) == 11520u);                           /* 바이트당 RXNE IRQ = 11520 /s */
    CHECK(dma_irqs_per_s(115200u, 10u, 64u) == 360u);                     /* 32 배 적다 */
    CHECK(half_deadline_us(115200u, 10u, 64u) == 2777u);                  /* 32 문자 × 86.8 µs */
    CHECK(dma_irqs_per_s(921600u, 10u, 256u) == 720u && half_deadline_us(921600u, 10u, 256u) == 1388u);
    CHECK(dma_irqs_per_s(3000000u, 10u, 16u) == 37500u);                  /* 3 Mbaud + 작은 버퍼 = 다시 IRQ 폭풍 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
