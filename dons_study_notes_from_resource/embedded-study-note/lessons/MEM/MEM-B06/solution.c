/* MEM-B06 패리티와 Hamming(7,4): 단일 비트 정정
 *
 * Part A: 짝수 패리티(even parity) + Hamming(7,4) 인코더 / 신드롬(syndrome) / 디코더 — 비트 연산만
 *         고전 배치: 위치 1..7 = p1 p2 d1 p4 d2 d3 d4, 신드롬 = 틀린 비트의 위치 번호
 * Part B: 전수 오류 주입 — 16 데이터 × 128 오류 패턴(무게 0..7) 결과표,
 *         단일 오류 100 % 정정 / 2비트 오류 336 건 전부 오정정(miscorrection), 최소 거리 d = 3
 * Part C: 32비트 워드 = 니블 8개 = Hamming(7,4) 8개 = 56 물리 비트. 비트 인터리빙(interleaving)으로
 *         길이 ≤ 8 버스트를 "코드워드마다 1비트" 로 쪼개 정정 — 버스트 전수 검사
 *
 * 코드워드 비트 배치 (이 레슨의 정의): uint8_t cw 의 bit i = 위치 i (i = 1..7), bit 0 = 0 (예약 — 연습 1 의 전체 패리티 자리)
 * 데이터 니블 d: bit0 = d1, bit1 = d2, bit2 = d3, bit3 = d4.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static unsigned popcount8(uint8_t x) { unsigned n = 0; while (x) { x &= (uint8_t)(x - 1u); n++; } return n; }

/* 패리티 검사 행렬 H 의 세 행 = "위치 번호의 bit k 가 1 인 위치들" (bit i = 위치 i) */
#define H74_MASK_P1 0xAAu   /* 위치 1,3,5,7  (위치 번호 bit0 = 1) */
#define H74_MASK_P2 0xCCu   /* 위치 2,3,6,7  (bit1 = 1) */
#define H74_MASK_P4 0xF0u   /* 위치 4,5,6,7  (bit2 = 1) */
#define H74_VALID   0xFEu   /* 위치 1..7 */

/* ---------------- Part A: 패리티와 Hamming(7,4) ---------------- */

/* 1 의 개수 mod 2 (XOR 접기). 0 = 짝수 개, 1 = 홀수 개 */
static unsigned parity8(uint8_t x) {
    x ^= (uint8_t)(x >> 4);
    x ^= (uint8_t)(x >> 2);
    x ^= (uint8_t)(x >> 1);
    return x & 1u;
}

/* 짝수 패리티 5비트 워드: bit0..3 = 데이터, bit4 = p (전체 1 의 개수가 짝수가 되게) */
static uint8_t ep_encode(uint8_t d) { d &= 0x0Fu; return (uint8_t)(d | parity8(d) << 4); }
static bool    ep_ok(uint8_t w5)    { return parity8(w5 & 0x1Fu) == 0; }

/* 데이터 비트를 위치 3,5,6,7 에 놓는다 / 꺼낸다 (주어짐) */
static uint8_t h74_place(uint8_t d) {
    return (uint8_t)((d & 1u) << 3 | (d & 2u) << 4 | (d & 4u) << 4 | (d & 8u) << 4);
}
static uint8_t h74_extract(uint8_t cw) {
    return (uint8_t)((cw >> 3 & 1u) | (cw >> 4 & 2u) | (cw >> 4 & 4u) | (cw >> 4 & 8u));
}

/* 신드롬 s = (s4 s2 s1): 각 행 마스크 안의 1 의 개수 mod 2. 단일 오류면 s = 그 위치 번호 */
static unsigned h74_syndrome(uint8_t cw) {
    return parity8(cw & H74_MASK_P1) | parity8(cw & H74_MASK_P2) << 1 | parity8(cw & H74_MASK_P4) << 2;
}

/* 4비트 → 7비트 코드워드 (bit i = 위치 i). 패리티 위치 1,2,4 를 신드롬이 0 이 되게 채운다 */
static uint8_t h74_encode(uint8_t d) {
    uint8_t c = h74_place(d & 0x0Fu);
    unsigned s = h74_syndrome(c);                      /* 데이터만 놓은 상태의 신드롬 */
    return (uint8_t)(c | (s & 1u) << 1 | (s & 2u) << 1 | (s & 4u) << 2);   /* s 의 bit k → 위치 2^k */
}

typedef struct { uint8_t data; uint8_t cw; unsigned syndrome; } h74_result;

/* 신드롬 ≠ 0 이면 그 위치 비트를 뒤집고 데이터를 꺼낸다. Hamming(7,4) 는 "정정 불가" 를 말할 수 없다 */
static h74_result h74_decode(uint8_t rx) {
    h74_result r;
    uint8_t cw = rx & H74_VALID;
    r.syndrome = h74_syndrome(cw);
    if (r.syndrome != 0) cw ^= (uint8_t)(1u << r.syndrome);
    r.cw = cw;
    r.data = h74_extract(cw);
    return r;
}

/* ---------------- Part B: 전수 오류 주입 ---------------- */

typedef enum { OUT_OK, OUT_CORRECTED, OUT_MISCORRECTED, OUT_UNDETECTED, OUT_COUNT } outcome_t;

/* 데이터 d 의 코드워드에 오류 e(bit 1..7) 를 XOR 하고 디코드한 결과를 분류한다.
 * OK = 오류 없음 + 데이터 맞음, CORRECTED = 오류 있음 + 데이터 맞음,
 * UNDETECTED = 신드롬 0 인데 데이터 틀림(조용한 오염), MISCORRECTED = 신드롬 ≠ 0 이라 "고쳤는데" 데이터 틀림 */
static outcome_t h74_outcome(uint8_t d, uint8_t e) {
    e &= H74_VALID;
    h74_result r = h74_decode((uint8_t)(h74_encode(d) ^ e));
    if (r.data == (d & 0x0Fu)) return e == 0 ? OUT_OK : OUT_CORRECTED;
    return r.syndrome == 0 ? OUT_UNDETECTED : OUT_MISCORRECTED;
}

/* table[w][o] = 오류 무게 w 인 (데이터, 패턴) 쌍 중 결과 o 의 개수. 16 × 128 = 2048 건 전수 */
static void h74_classify_all(unsigned table[8][OUT_COUNT]) {
    for (unsigned w = 0; w < 8; w++)
        for (unsigned o = 0; o < OUT_COUNT; o++) table[w][o] = 0;
    for (unsigned d = 0; d < 16; d++)
        for (unsigned x = 0; x < 128; x++) {
            uint8_t e = (uint8_t)(x << 1);
            table[popcount8(e)][h74_outcome((uint8_t)d, e)]++;
        }
}

/* 서로 다른 두 코드워드 사이 해밍 거리의 최솟값 (16 × 15 / 2 = 120 쌍) */
static unsigned code_min_distance(void) {
    unsigned best = 8;
    for (unsigned a = 0; a < 16; a++)
        for (unsigned b = a + 1; b < 16; b++) {
            unsigned dist = popcount8((uint8_t)(h74_encode((uint8_t)a) ^ h74_encode((uint8_t)b)));
            if (dist < best) best = dist;
        }
    return best;
}

/* ---------------- Part C: 32비트 워드 저장 + 비트 인터리빙 ---------------- */

#define BLK_CW   8u    /* 니블 8개 → 코드워드 8개 */
#define BLK_BITS 56u   /* 8 × 7 물리 비트 (uint64_t 의 bit 0..55) */

/* 코드워드 j(0..7) 의 위치 pos(1..7) 가 놓이는 물리 비트 번호 (주어짐)
 *   순차(sequential): 코드워드가 7비트씩 이어 붙음   → 7j + (pos − 1)
 *   인터리브       : 같은 위치끼리 8비트씩 모음     → 8(pos − 1) + j   (이웃 비트 = 다른 코드워드) */
static unsigned phys_index(unsigned j, unsigned pos, bool il) {
    return il ? 8u * (pos - 1u) + j : 7u * j + (pos - 1u);
}

static uint64_t blk_encode(uint32_t w, bool il) {
    uint64_t phys = 0;
    for (unsigned j = 0; j < BLK_CW; j++) {
        uint8_t cw = h74_encode((uint8_t)(w >> (4u * j) & 0xFu));
        for (unsigned pos = 1; pos <= 7; pos++)
            if (cw >> pos & 1u) phys |= 1ull << phys_index(j, pos, il);
    }
    return phys;
}

typedef struct { uint32_t data; unsigned ce; } blk_result;   /* ce = 신드롬 ≠ 0 이었던 코드워드 수 */

static blk_result blk_decode(uint64_t phys, bool il) {
    blk_result r = { 0, 0 };
    for (unsigned j = 0; j < BLK_CW; j++) {
        uint8_t cw = 0;
        for (unsigned pos = 1; pos <= 7; pos++)
            if (phys >> phys_index(j, pos, il) & 1u) cw |= (uint8_t)(1u << pos);
        h74_result h = h74_decode(cw);
        if (h.syndrome != 0) r.ce++;
        r.data |= (uint32_t)h.data << (4u * j);
    }
    return r;
}

/* 길이 L 버스트(첫·끝 비트는 반드시 뒤집고, 사이 L−2 비트는 모든 조합)를 56비트의 모든 시작 위치에 넣어
 * 디코드 데이터가 틀린 경우의 수를 센다. *ntested = 시도 수. L = 1..BLK_BITS, L ≤ 20 */
static unsigned burst_fail_count(uint32_t w, unsigned L, bool il, unsigned *ntested) {
    unsigned fail = 0, n = 0;
    if (L == 0 || L > 20 || L > BLK_BITS) { *ntested = 0; return 0; }
    uint64_t good = blk_encode(w, il);
    unsigned inner = L >= 2 ? L - 2 : 0;
    for (unsigned s = 0; s + L <= BLK_BITS; s++)
        for (uint32_t m = 0; m < (1u << inner); m++) {
            uint64_t e = 1ull << s;
            if (L >= 2) e |= 1ull << (s + L - 1) | (uint64_t)m << (s + 1);
            n++;
            if (blk_decode(good ^ e, il).data != w) fail++;
        }
    *ntested = n;
    return fail;
}

/* ---------------- 테스트 하네스 ---------------- */
int main(void) {
    /* Part A — 패리티 */
    CHECK(parity8(0x00) == 0 && parity8(0x01) == 1 && parity8(0x80) == 1);
    CHECK(parity8(0xFF) == 0 && parity8(0x7F) == 1 && parity8(0xAA) == 0);
    {
        bool all = true;
        for (unsigned x = 0; x < 256; x++) all &= parity8((uint8_t)x) == (popcount8((uint8_t)x) & 1u);
        CHECK(all);
    }
    {   /* 짝수 패리티: 16 데이터 × 31 개 0 아닌 오류(5비트) → 홀수 무게만 검출 */
        unsigned det = 0, miss = 0;
        bool rule = true;
        for (unsigned d = 0; d < 16; d++) {
            uint8_t w = ep_encode((uint8_t)d);
            rule &= ep_ok(w);
            for (unsigned e = 1; e < 32; e++) {
                bool caught = !ep_ok((uint8_t)(w ^ e));
                rule &= caught == ((popcount8((uint8_t)e) & 1u) == 1u);
                if (caught) det++; else miss++;
            }
        }
        printf("even parity (4+1): detected=%u missed=%u of %u error cases\n", det, miss, det + miss);
        CHECK(rule);
        CHECK(det == 16 * 16 && miss == 16 * 15);
    }
    /* Part A — Hamming(7,4) 알려진 값 (bit i = 위치 i) */
    CHECK(h74_encode(0x0) == 0x00);
    CHECK(h74_encode(0xB) == 0xAA);          /* d=1011b → p1 p2 d1 p4 d2 d3 d4 = 1 0 1 0 1 0 1 */
    CHECK(h74_encode(0x1) == 0x0E);          /* d1 만 1 → 위치 1,2,3 */
    CHECK(h74_encode(0xF) == 0xFE);          /* 전부 1 */
    CHECK(h74_encode(0x8) == 0x96);          /* d4(위치 7) → p1 p2 p4 모두 1: 위치 1,2,4,7 */
    CHECK(h74_syndrome(0xAA) == 0 && h74_syndrome(0x02) == 1 && h74_syndrome(0x80) == 7);
    {
        bool all = true;
        printf("codewords d->cw:");
        for (unsigned d = 0; d < 16; d++) {
            uint8_t c = h74_encode((uint8_t)d);
            printf(" %X:%02X", d, c);
            all &= (c & 1u) == 0 && h74_syndrome(c) == 0 && h74_extract(c) == d && h74_decode(c).data == d;
        }
        printf("\n");
        CHECK(all);
    }
    {   /* 16 데이터 × 7 단일 오류: 신드롬 = 위치, 100 % 정정 */
        unsigned ok = 0, synd_is_pos = 0;
        for (unsigned d = 0; d < 16; d++)
            for (unsigned pos = 1; pos <= 7; pos++) {
                h74_result r = h74_decode((uint8_t)(h74_encode((uint8_t)d) ^ (1u << pos)));
                synd_is_pos += r.syndrome == pos;
                ok += r.data == d && r.cw == h74_encode((uint8_t)d);
            }
        printf("single errors: corrected %u/112, syndrome==position %u/112\n", ok, synd_is_pos);
        CHECK(ok == 112 && synd_is_pos == 112);
    }
    CHECK(h74_decode(0x01).data == 0 && h74_decode(0x01).syndrome == 0);   /* bit 0 은 무시 (예약) */

    /* Part B — 한 건씩 분류 */
    CHECK(h74_outcome(0xB, 0x00) == OUT_OK);
    CHECK(h74_outcome(0xB, 1u << 5) == OUT_CORRECTED);
    CHECK(h74_outcome(0xB, 1u << 2 | 1u << 5) == OUT_MISCORRECTED);
    CHECK(h74_outcome(0xB, 0x0E) == OUT_UNDETECTED);      /* 오류 = 코드워드(enc(1)) → 신드롬 0 */
    /* Part B — 전수 결과표 */
    {
        unsigned t[8][OUT_COUNT];
        h74_classify_all(t);
        printf("weight  patterns  ok  corrected  miscorrected  undetected   (16 data each)\n");
        for (unsigned w = 0; w < 8; w++)
            printf("  %u      %3u    %3u   %4u       %4u          %4u\n", w,
                   t[w][0] + t[w][1] + t[w][2] + t[w][3], t[w][0], t[w][1], t[w][2], t[w][3]);
        CHECK(t[0][OUT_OK] == 16);
        CHECK(t[1][OUT_CORRECTED] == 112 && t[1][OUT_MISCORRECTED] == 0 && t[1][OUT_UNDETECTED] == 0);
        CHECK(t[2][OUT_MISCORRECTED] == 336 && t[2][OUT_CORRECTED] == 0 && t[2][OUT_UNDETECTED] == 0);
        CHECK(t[3][OUT_UNDETECTED] == 7 * 16 && t[3][OUT_MISCORRECTED] == 28 * 16);
        CHECK(t[4][OUT_UNDETECTED] == 7 * 16 && t[4][OUT_MISCORRECTED] == 28 * 16);
        CHECK(t[5][OUT_MISCORRECTED] == 21 * 16 && t[6][OUT_MISCORRECTED] == 7 * 16);
        CHECK(t[7][OUT_UNDETECTED] == 16);
        unsigned good = 0;
        for (unsigned w = 2; w < 8; w++) good += t[w][OUT_OK] + t[w][OUT_CORRECTED];
        CHECK(good == 0);                    /* 2비트 이상은 하나도 못 살린다 (완전 부호) */
    }
    {   /* 2비트 오류 336 건: 디코더는 세 번째 비트(i XOR j)를 뒤집는다 → 무게 3 오류 = 이웃 코드워드 */
        unsigned hist[5] = { 0 }, third_ok = 0, n = 0;
        for (unsigned d = 0; d < 16; d++)
            for (unsigned i = 1; i <= 7; i++)
                for (unsigned j = i + 1; j <= 7; j++) {
                    uint8_t c = h74_encode((uint8_t)d);
                    h74_result r = h74_decode((uint8_t)(c ^ (1u << i) ^ (1u << j)));
                    third_ok += r.syndrome == (i ^ j) && popcount8((uint8_t)(r.cw ^ c)) == 3;
                    hist[popcount8((uint8_t)(r.data ^ d))]++;
                    n++;
                }
        printf("double errors: %u cases, syndrome = i^j and final error weight 3: %u; wrong data bits 0..4: %u %u %u %u %u\n",
               n, third_ok, hist[0], hist[1], hist[2], hist[3], hist[4]);
        CHECK(n == 336 && third_ok == 336 && hist[0] == 0);
    }
    {   /* 예: d=0xB, 위치 2·5 오류 → 신드롬 7 → 위치 7 을 뒤집어 0x1 로 오정정 */
        h74_result r = h74_decode((uint8_t)(0xAA ^ 1u << 2 ^ 1u << 5));
        printf("trace: rx=0x%02X syndrome=%u -> cw=0x%02X data=0x%X (sent 0xB)\n",
               (unsigned)(0xAA ^ 1u << 2 ^ 1u << 5), r.syndrome, r.cw, r.data);
        CHECK(r.syndrome == 7 && r.cw == 0x0E && r.data == 0x1);
    }
    CHECK(code_min_distance() == 3);
    {   /* 완전 부호(perfect code): 128 개 7비트 워드 = 16 코드워드 × (자기 + 이웃 7) — 겹침 없음 */
        unsigned hit[16] = { 0 };
        for (unsigned x = 0; x < 128; x++) hit[h74_decode((uint8_t)(x << 1)).data]++;
        bool all8 = true;
        for (unsigned d = 0; d < 16; d++) all8 &= hit[d] == 8;
        CHECK(all8);
    }

    /* Part C — 32비트 워드 저장 */
    CHECK(phys_index(0, 1, false) == 0 && phys_index(7, 7, false) == 55);
    CHECK(phys_index(0, 1, true) == 0 && phys_index(7, 7, true) == 55 && phys_index(1, 1, true) == 1);
    {
        static const uint32_t W[] = { 0x00000000u, 0xFFFFFFFFu, 0xDEADBEEFu, 0x12345678u };
        bool rt = true, single = true, two = true;
        uint32_t rng = 0x2026B06u;
        for (unsigned k = 0; k < 4 + 60; k++) {
            uint32_t w = k < 4 ? W[k] : xs32(&rng);
            for (int il = 0; il <= 1; il++) {
                uint64_t p = blk_encode(w, il);
                blk_result r = blk_decode(p, il);
                rt &= r.data == w && r.ce == 0 && (p >> BLK_BITS) == 0;
                for (unsigned b = 0; b < BLK_BITS; b++) {          /* 56 단일 오류 전부 */
                    r = blk_decode(p ^ 1ull << b, il);
                    single &= r.data == w && r.ce == 1;
                }
                /* 서로 다른 코드워드 두 개에 1비트씩 → 둘 다 정정, CE 2 */
                r = blk_decode(p ^ 1ull << phys_index(1, 3, il) ^ 1ull << phys_index(6, 5, il), il);
                two &= r.data == w && r.ce == 2;
            }
        }
        CHECK(rt);
        CHECK(single);
        CHECK(two);
        CHECK(blk_encode(0x0000000Bu, false) == 0xAAu >> 1);   /* 코드워드 0 = 0xAA 의 위치 1..7 → 물리 0..6 */
    }
    {
        static const uint32_t W[] = { 0x00000000u, 0xFFFFFFFFu, 0xDEADBEEFu, 0x12345678u };
        unsigned fs[11] = { 0 }, fi[11] = { 0 }, nt[11] = { 0 };
        bool cnt_ok = true;
        for (unsigned L = 1; L <= 10; L++)
            for (unsigned k = 0; k < 4; k++) {
                unsigned n1, n2;
                fs[L] += burst_fail_count(W[k], L, false, &n1);
                fi[L] += burst_fail_count(W[k], L, true, &n2);
                nt[L] += n1;
                cnt_ok &= n1 == n2 && n1 == (BLK_BITS - L + 1) * (L >= 2 ? 1u << (L - 2) : 1u);
            }
        CHECK(cnt_ok);                                  /* 시도 수 = (57 − L) × 2^(L−2) */
        printf("burst L : tested(4 words)  sequential-fail  interleaved-fail\n");
        for (unsigned L = 1; L <= 10; L++)
            printf("  %2u    : %6u           %6u (%5.1f%%)   %6u (%5.1f%%)\n", L, nt[L],
                   fs[L], nt[L] ? 100.0 * fs[L] / nt[L] : 0.0, fi[L], nt[L] ? 100.0 * fi[L] / nt[L] : 0.0);
        bool il_ok = true;
        for (unsigned L = 1; L <= 8; L++) il_ok &= fi[L] == 0;
        CHECK(il_ok);                                   /* 인터리브: 길이 ≤ 8 버스트 전부 정정 */
        CHECK(fi[9] == nt[9]);                          /* L = 9: 첫·끝 비트가 같은 코드워드 → 전부 실패 */
        CHECK(fs[1] == 0);
        CHECK(fs[2] == 4 * 48);                         /* 순차: 55 시작 중 경계(b mod 7 = 6) 7 곳만 산다 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
