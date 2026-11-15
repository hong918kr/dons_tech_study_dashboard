/* ALGO-A07 비트 트릭 고급: Gray 코드·SWAR·부분집합 순회
 *
 *   Part A  Gray 변환 g = b ^ (b>>1) · 역변환(prefix-XOR 5단계) · 부분집합 순회 sub = (sub-1) & mask
 *           · 같은 popcount 의 다음 수(Gosper's hack, 32비트 꼭대기에서 0 = 끝)
 *   Part B  SWAR 바이트 검사: haszero / 정확한 0-바이트 마스크 / 첫 0 바이트 위치(little-endian, ctz)
 *           · hasvalue · hasless · 워드 단위 strlen · 길이를 아는 버퍼의 워드 단위 바이트 찾기(과독 없음)
 *   Part C  절대 엔코더: 전이 중 오판독 시뮬레이션(바뀌는 비트의 모든 부분집합) · 멀티턴 위치 추적,
 *           CRC-8(0x07) 이 못 잡는 k비트 오류 패턴 수(Gosper 로 C(n,k) 개 전부 열거)
 *
 * 규칙: TODO 안에서는 __builtin_* 를 쓰지 않는다. ctz32() 는 주어진 도우미(B12 에서 직접 구현함).
 *       테스트 쪽 기준(reference)은 비트 하나씩 도는 느린 구현 + __builtin_popcount.
 * 가정: little-endian (이 맥 arm64, Cortex-M 기본값). big-endian 이면 컴파일 에러로 멈춘다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st   (solution.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "word-at-a-time code below assumes little-endian (byte 0 = least significant byte)"
#endif

/* 주어진 도우미: 뒤쪽 0 개수, ctz32(0) = 32. Cortex-M3/M4 = rbit + clz 2명령 (ALGO-B12) */
static unsigned ctz32(uint32_t x) { return x ? (unsigned)__builtin_ctz(x) : 32u; }

/* ======================= Part A — Gray · 부분집합 · Gosper ======================= */
/* 이진 → Gray: 이웃한 두 수의 Gray 코드는 정확히 한 비트만 다르다 */
static uint32_t gray_enc(uint32_t b) {
    (void)b;
    return 0; /* TODO: b 와 b>>1 의 XOR */
}

/* Gray → 이진: b_i = g_i ^ g_{i+1} ^ ... ^ g_31 (위쪽 전부의 XOR). 루프 31번 대신 5단계 */
static uint32_t gray_dec(uint32_t g) {
    (void)g;
    return 0; /* TODO: g ^= g >> 16, 8, 4, 2, 1 (5단계로 위쪽 전부의 XOR) */
}

/* mask 의 부분집합을 큰 것부터 0(공집합)까지 전부 방문한다. 앞의 cap 개를 out[] 에 쓰고
 * 방문한 총 개수(= 2^popcount(mask))를 돌려준다. out 이 작아도 개수는 끝까지 센다. */
static uint64_t submask_list(uint32_t mask, uint32_t *out, uint64_t cap) {
    (void)mask; (void)out; (void)cap;
    return 0; /* TODO: sub = mask 에서 시작, sub = (sub - 1) & mask, 0 을 방문한 뒤 멈춤 */
}

/* x 보다 큰 수 중 popcount 가 같은 가장 작은 수 (Gosper, HAKMEM 175).
 * x == 0 이거나 32비트 안에 다음이 없으면(1 이 맨 위에 몰려 있음) 0 을 돌려준다. */
static uint32_t next_same_popcount(uint32_t x) {
    (void)x;
    return 0; /* TODO: c = x & -x, r = x + c (r == 0 이면 끝), ((r ^ x) >> 2) / c | r */
}

/* ======================= Part B — SWAR 바이트 검사 ======================= */
#define ONES32  0x01010101u
#define HIGHS32 0x80808080u

/* 0 이 아니면 ⇔ 어떤 바이트가 0. 단 바이트별 플래그는 "가장 낮은 0 바이트" 위쪽에서 틀릴 수 있다 */
static uint32_t haszero32(uint32_t v) {
    (void)v;
    return 0; /* TODO: (v - 0x01..) & ~v & 0x80.. */
}

/* 바이트별로 정확한 플래그: 0 인 바이트 자리에만 0x80. 칸 사이 자리올림이 없다 */
static uint32_t zero_bytes_exact(uint32_t v) {
    (void)v;
    return 0; /* TODO: 각 바이트의 아래 7비트 + 0x7F 가 칸을 넘지 않게 → v·0x7F.. 와 OR → 반전 */
}

/* 메모리에서 가장 앞(= little-endian 에서 가장 낮은 자리) 0 바이트의 번호 0..3, 없으면 4 */
static unsigned first_zero_le(uint32_t v) {
    (void)v;
    return 4; /* TODO: haszero 의 가장 낮은 플래그 위치 / 8 */
}

/* 어떤 바이트가 c 와 같은가 */
static uint32_t hasvalue32(uint32_t v, uint8_t c) {
    (void)v; (void)c;
    return 0; /* TODO: c 를 네 바이트에 복제해 XOR → haszero */
}

/* 어떤 바이트가 n 보다 작은가 (0 ≤ n ≤ 128 에서만 맞다) */
static uint32_t hasless32(uint32_t v, unsigned n) {
    (void)v; (void)n;
    return 0; /* TODO: haszero 의 0x01.. 대신 0x01.. * n */
}

/* 워드 단위 strlen: 바이트로 4-정렬까지 → 정렬된 4바이트씩 → 0 이 든 워드에서 위치 계산.
 * 마지막 워드는 NUL 뒤 바이트를 최대 3개 더 읽는다 (같은 정렬 워드 안 → 페이지를 넘지 않음).
 * ISO C 에서는 객체 밖을 읽으면 UB — 테스트는 문자열을 큰 배열 안에 둔다 (5. 풀이). */
static size_t strlen_word(const char *s) {
    (void)s;
    return 0; /* TODO: 바이트로 4-정렬까지 → memcpy 로 정렬된 워드 → haszero → first_zero_le */
}

/* 길이를 아는 버퍼에서 바이트 c 의 첫 위치 (없으면 len). buf[len] 이후는 절대 읽지 않는다 */
static size_t find_byte(const uint8_t *buf, size_t len, uint8_t c) {
    (void)buf; (void)c;
    return len; /* TODO: 머리(정렬까지) 바이트 · 몸통 워드(i + 4 <= len) · 꼬리 바이트 */
}

/* ======================= Part C — 엔코더 · CRC 오류 패턴 ======================= */
static uint32_t cdist(uint32_t a, uint32_t b, uint32_t N) {   /* 원형 거리 (주어짐) */
    uint32_t d = a > b ? a - b : b - a;
    return d < N - d ? d : N - d;
}

/* nbits 비트 절대 엔코더가 p → p+1 (mod 2^nbits) 로 넘어가는 순간 읽으면, 바뀌는 비트 각각이
 * 옛 값/새 값 중 아무거나로 읽힐 수 있다 (트랙마다 정렬 오차). 모든 p 와 "바뀌는 비트의 모든
 * 부분집합" 에 대해 해독한 위치가 {p, p+1} 에서 얼마나 먼지(원형 거리)의 최댓값을 돌려준다.
 * *bad = {p, p+1} 이 아닌 판독 수, *total = 판독 경우의 총수. 1 ≤ nbits ≤ 16. */
static uint32_t encoder_misread_worst(int nbits, bool gray, uint64_t *bad, uint64_t *total) {
    (void)nbits; (void)gray;
    *bad = 0; *total = 0;
    return 0; /* TODO: p 마다 diff = code(p) ^ code(p+1), diff 의 모든 부분집합 sub 로 a ^ sub 해독 */
}

/* 멀티턴 추적: 매 샘플의 원시 코드 → 한 바퀴 안 위치 → 부호 있는 최단 변화량 [-N/2, N/2) 를 누적.
 * 샘플 사이 실제 이동이 N/2 미만이어야 방향을 알 수 있다 (나이퀴스트와 같은 이야기). */
typedef struct { uint32_t last; int64_t pos; bool init; } enc_track_t;

static int64_t enc_track(enc_track_t *t, uint32_t raw, int nbits, bool gray) {
    (void)t; (void)raw; (void)nbits; (void)gray;
    return 0; /* TODO: 해독 → (cur - last) & mask → [-N/2, N/2) 로 → 누적. 첫 샘플은 cur 그대로 */
}

/* e(x) mod (x^8 + x^2 + x + 1) — CRC-8 다항식 0x07 (ALGO-M01). 주어진 도우미. */
static uint32_t poly_rem8(uint32_t e) {
    for (int i = 31; i >= 8; i--)
        if ((e >> i) & 1u) e ^= 0x107u << (i - 8);
    return e;
}

/* 길이 nbits (≤ 32) 의 코드워드에서 무게(1 의 개수) k 인 오류 패턴을 Gosper 로 전부 열거해
 * CRC 가 못 잡는 것(e(x) 가 g(x) 로 나눠떨어짐)의 수를 돌려준다. *patterns = 열거한 수 = C(nbits,k). */
static uint64_t crc8_undetected(int nbits, int k, uint64_t *patterns) {
    (void)nbits; (void)k;
    *patterns = 0;
    return 0; /* TODO: x = 2^k - 1 부터 Gosper, nbits 칸을 넘거나 0 이 되면 끝, poly_rem8(x) == 0 을 센다 */
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t rng = 0x2545F491u;
static uint32_t xorshift32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static unsigned pc(uint32_t x) { return (unsigned)__builtin_popcount(x); }

/* 기준 구현들: 비트/바이트 하나씩 */
static uint32_t ref_gray_dec(uint32_t g) {
    uint32_t b = 0, bit = 0;
    for (int i = 31; i >= 0; i--) { bit ^= (g >> i) & 1u; b |= bit << i; }
    return b;
}
static uint32_t ref_next_same(uint32_t x) {      /* 가장 낮은 "01" 을 "10" 으로, 아래 1 들은 바닥으로 */
    if (x == 0) return 0;
    int p = 0, ones = 0;
    while (!((x >> p) & 1u)) p++;
    while (p < 32 && ((x >> p) & 1u)) { ones++; p++; }
    if (p == 32) return 0;
    uint32_t hi = p == 31 ? 0u : x & (0xFFFFFFFFu << (p + 1));
    return hi | (1u << p) | ((1u << (ones - 1)) - 1u);
}
static uint32_t ref_zero_mask(uint32_t v) {
    uint32_t m = 0;
    for (int i = 0; i < 4; i++) if (((v >> (8 * i)) & 0xFFu) == 0) m |= 0x80u << (8 * i);
    return m;
}
static unsigned ref_first_zero(uint32_t v) {
    for (unsigned i = 0; i < 4; i++) if (((v >> (8 * i)) & 0xFFu) == 0) return i;
    return 4;
}
static bool ref_hasvalue(uint32_t v, uint8_t c) {
    for (int i = 0; i < 4; i++) if (((v >> (8 * i)) & 0xFFu) == c) return true;
    return false;
}
static bool ref_hasless(uint32_t v, unsigned n) {
    for (int i = 0; i < 4; i++) if (((v >> (8 * i)) & 0xFFu) < n) return true;
    return false;
}

static uint32_t subs[1u << 16];
static uint8_t seen[1u << 16];

/* 바이트 값 후보: 0, 1 (빌림 사슬), 0x7F/0x80/0x81 (최상위 비트 경계), 0xFF 등 */
static const uint8_t SPECIAL[16] = {0x00, 0x01, 0x02, 0x03, 0x0F, 0x10, 0x20, 0x55,
                                    0x7E, 0x7F, 0x80, 0x81, 0xAA, 0xC0, 0xFE, 0xFF};
static uint32_t special_word(uint32_t i) {        /* i = 0..65535 → 바이트 4개가 SPECIAL 에서 */
    return (uint32_t)SPECIAL[i & 15] | (uint32_t)SPECIAL[(i >> 4) & 15] << 8 |
           (uint32_t)SPECIAL[(i >> 8) & 15] << 16 | (uint32_t)SPECIAL[(i >> 12) & 15] << 24;
}

static _Alignas(8) char spool[512];               /* strlen 시험장: 과독이 이 배열 안에 머문다 */
static _Alignas(8) uint8_t bpool[512];

int main(void) {
    /* 주어진 도우미 확인 */
    CHECK(ctz32(0) == 32 && ctz32(0x80u) == 7 && cdist(1, 255, 256) == 2 && cdist(0, 128, 256) == 128);

    /* ---------- Part A: Gray ---------- */
    CHECK(gray_enc(0) == 0 && gray_enc(1) == 1 && gray_enc(2) == 3 && gray_enc(3) == 2 && gray_enc(4) == 6);
    CHECK(gray_enc(0x80000000u) == 0xC0000000u && gray_enc(0xFFFFFFFFu) == 0x80000000u);
    CHECK(gray_dec(6) == 4 && gray_dec(0x80000000u) == 0xFFFFFFFFu && gray_dec(0xC0000000u) == 0x80000000u);
    {
        unsigned bad_rt = 0, bad_inv = 0, bad_adj = 0, bad_ref = 0, dup = 0, wide = 0;
        memset(seen, 0, sizeof seen);
        for (uint32_t b = 0; b < 65536u; b++) {
            uint32_t g = gray_enc(b);
            if (gray_dec(g) != b) bad_rt++;
            if (gray_enc(gray_dec(b)) != b) bad_inv++;
            if (gray_dec(b) != ref_gray_dec(b)) bad_ref++;
            if (pc(g ^ gray_enc((b + 1u) & 0xFFFFu)) != 1) bad_adj++;     /* 0xFFFF → 0 도 포함 */
            if (g > 0xFFFFu) wide++; else if (seen[g]++) dup++;
        }
        printf("gray 16-bit exhaustive: roundtrip bad=%u inverse bad=%u ref bad=%u adjacent!=1bit=%u dup=%u wide=%u\n",
               bad_rt, bad_inv, bad_ref, bad_adj, dup, wide);
        CHECK(bad_rt == 0 && bad_inv == 0 && bad_ref == 0);
        CHECK(bad_adj == 0 && dup == 0 && wide == 0);
        unsigned bad32 = 0;
        for (int i = 0; i < (1 << 18); i++) {
            uint32_t x = xorshift32();
            if (gray_dec(gray_enc(x)) != x || gray_dec(x) != ref_gray_dec(x) ||
                pc(gray_enc(x) ^ gray_enc(x + 1u)) != 1) bad32++;
        }
        CHECK(bad32 == 0);
    }

    /* ---------- Part A: 부분집합 순회 ---------- */
    {
        CHECK(submask_list(0, subs, 16) == 1 && subs[0] == 0);
        CHECK(submask_list(0x15u, subs, 16) == 8 && subs[0] == 0x15u && subs[1] == 0x14u &&
              subs[2] == 0x11u && subs[3] == 0x10u && subs[7] == 0);
        unsigned bad = 0;
        uint64_t total = 0;
        for (uint32_t mask = 0; mask < 4096u; mask++) {          /* n = 12 의 모든 마스크 */
            uint64_t cnt = submask_list(mask, subs, 4096);
            total += cnt;
            if (cnt != (1ull << pc(mask))) { bad++; continue; }
            uint64_t j = 0;                                       /* 브루트: 큰 수부터 걸러내기 */
            for (uint32_t x = mask + 1u; x-- > 0;)
                if ((x & ~mask) == 0) { if (j >= cnt || subs[j] != x) bad++; j++; }
            if (j != cnt) bad++;
        }
        printf("submask n=12: all 4096 masks vs brute, bad=%u, total visits=%llu (3^12=531441)\n",
               bad, (unsigned long long)total);
        CHECK(bad == 0 && total == 531441u);
        unsigned bad_r = 0;
        for (int i = 0; i < 2000; i++) {                          /* 32비트 위치에 흩어진 ≤ 12 비트 */
            uint32_t m = xorshift32() & xorshift32() & xorshift32();
            while (pc(m) > 12) m &= m - 1u;
            uint64_t cnt = submask_list(m, subs, 1u << 16);
            if (cnt != (1ull << pc(m)) || subs[cnt - 1] != 0) { bad_r++; continue; }
            for (uint64_t j = 0; j < cnt; j++)
                if ((subs[j] & ~m) || (j && subs[j] >= subs[j - 1])) { bad_r++; break; }
        }
        CHECK(bad_r == 0);
        int ok3 = 1;                                              /* Σ_mask 2^popcount = 3^n */
        uint64_t pow3 = 1;
        for (int n = 0; n <= 15; n++, pow3 *= 3) {
            uint64_t s = 0;
            for (uint32_t mask = 0; mask < (1u << n); mask++) s += submask_list(mask, subs, 0);
            if (s != pow3) ok3 = 0;
            if (n == 15) printf("sum over all masks of #submasks, n=15: %llu (3^15=%llu)\n",
                                (unsigned long long)s, (unsigned long long)pow3);
        }
        CHECK(ok3);
    }

    /* ---------- Part A: Gosper ---------- */
    CHECK(next_same_popcount(0x7u) == 0xBu && next_same_popcount(0xCu) == 0x11u && next_same_popcount(1) == 2);
    CHECK(next_same_popcount(0x70000000u) == 0x80000003u && next_same_popcount(0x7FFFFFFFu) == 0xBFFFFFFFu);
    CHECK(next_same_popcount(0) == 0 && next_same_popcount(0xFFFFFFFFu) == 0 &&
          next_same_popcount(0x80000000u) == 0 && next_same_popcount(0xF0000000u) == 0);
    {
        unsigned bad = 0;
        for (int n = 1; n <= 20; n++) {                           /* 한 번 훑기로 k 전부 */
            uint32_t last[33]; uint64_t cnt[33], binom[33];
            for (int k = 0; k <= 32; k++) { cnt[k] = 0; binom[k] = 0; }
            binom[0] = 1;                                         /* 파스칼 행 n */
            for (int r = 1; r <= n; r++)
                for (int k = r; k >= 1; k--) binom[k] += binom[k - 1];
            for (uint32_t x = 0; x < (1u << n); x++) {
                unsigned k = pc(x);
                if (cnt[k] && next_same_popcount(last[k]) != x) bad++;
                last[k] = x; cnt[k]++;
            }
            for (int k = 0; k <= n; k++) {
                uint32_t nx = next_same_popcount(last[k]);
                if (cnt[k] != binom[k] || (nx != 0 && nx < (1u << n))) bad++;
            }
        }
        printf("gosper exhaustive n=1..20, every k: bad=%u\n", bad);
        CHECK(bad == 0);
        unsigned bad_r = 0;
        for (int i = 0; i < (1 << 18); i++) {
            uint32_t x = xorshift32();
            if (i & 1) x |= 0xFFFFFFFFu << (xorshift32() & 31);   /* 꼭대기가 1 로 찬 값을 섞는다 */
            if (next_same_popcount(x) != ref_next_same(x)) bad_r++;
        }
        CHECK(bad_r == 0);
    }

    /* ---------- Part B: haszero 와 친구들 ---------- */
    CHECK(haszero32(0x12003456u) != 0 && haszero32(0x01010101u) == 0 && haszero32(0x80808080u) == 0);
    CHECK(haszero32(0) == 0x80808080u && haszero32(0xFFFFFFFFu) == 0);
    CHECK(haszero32(0xFFFF0100u) == 0x00008080u && zero_bytes_exact(0xFFFF0100u) == 0x00000080u); /* 0x01 위 거짓 플래그 */
    CHECK(first_zero_le(0xFFFF0100u) == 0 && first_zero_le(0x00FFFFFFu) == 3 && first_zero_le(0x01010101u) == 4);
    CHECK(hasvalue32(0x0A414243u, '\n') != 0 && hasvalue32(0x41424344u, '\n') == 0);
    {
        unsigned bad_any = 0, bad_exact = 0, bad_first = 0, bad_val = 0, flag_diff = 0;
        uint32_t nwords = 0;
        for (uint32_t i = 0; i < (1u << 16) + (1u << 19); i++) {
            uint32_t v;
            if (i < (1u << 16)) v = special_word(i);
            else if (i & 1) v = xorshift32();
            else {                                                /* 0/1 바이트를 많이 섞은 랜덤 */
                v = xorshift32();
                uint32_t r = xorshift32();
                for (int b = 0; b < 4; b++)
                    if (((r >> (2 * b)) & 3u) == 0) v = (v & ~(0xFFu << (8 * b))) | ((r >> (8 + b)) & 1u) << (8 * b);
            }
            nwords++;
            uint32_t ref = ref_zero_mask(v);
            if ((haszero32(v) != 0) != (ref != 0)) bad_any++;
            if (zero_bytes_exact(v) != ref) bad_exact++;
            if (first_zero_le(v) != ref_first_zero(v)) bad_first++;
            if (haszero32(v) != ref) flag_diff++;
            uint8_t c = (uint8_t)xorshift32();
            if ((hasvalue32(v, c) != 0) != ref_hasvalue(v, c)) bad_val++;
            if ((hasvalue32(v, (uint8_t)(v >> 16)) != 0) != true) bad_val++;
        }
        printf("swar: %u words: haszero iff bad=%u exact bad=%u first bad=%u hasvalue bad=%u | "
               "per-byte flags of haszero differ from exact in %u words\n",
               nwords, bad_any, bad_exact, bad_first, bad_val, flag_diff);
        CHECK(bad_any == 0 && bad_exact == 0 && bad_first == 0 && bad_val == 0);
        CHECK(flag_diff > 0);                       /* 바이트별 플래그는 믿으면 안 된다 — 실측 */
        unsigned bad_less = 0, ce129 = 0;
        uint32_t ce_word = 0;
        for (uint32_t i = 0; i < (1u << 16); i++) {
            uint32_t v = special_word(i);
            for (unsigned n = 0; n <= 128; n++)
                if ((hasless32(v, n) != 0) != ref_hasless(v, n)) bad_less++;
            if ((hasless32(v, 129) != 0) != ref_hasless(v, 129)) { if (!ce129++) ce_word = v; }
        }
        printf("hasless n=0..128 on 65536 special words: bad=%u | n=129 wrong on %u words (first 0x%08X)\n",
               bad_less, ce129, ce_word);
        CHECK(bad_less == 0 && ce129 > 0);
    }

    /* ---------- Part B: strlen_word · find_byte ---------- */
    {
        unsigned bad = 0, cases = 0;
        for (int round = 0; round < 400; round++) {
            for (size_t k = 0; k < sizeof spool; k++) {           /* 0 이 아닌 쓰레기, 0x01·0x80 많이 */
                uint32_t r = xorshift32();
                spool[k] = (char)((r & 3u) == 0 ? 0x01 : (r & 3u) == 1 ? 0x80 : (uint8_t)((r >> 8) | 1u));
            }
            size_t off = (size_t)round % 8u, len = round < 160 ? (size_t)round / 2 : xorshift32() % 400u;
            spool[off + len] = 0;
            if ((xorshift32() & 1u) && off + len + 1 < sizeof spool) spool[off + len + 1] = 0;
            cases++;
            if (strlen_word(spool + off) != strlen(spool + off)) bad++;
        }
        printf("strlen_word: %u cases (offset 0..7, len 0..399) bad=%u\n", cases, bad);
        CHECK(bad == 0);
        unsigned badf = 0;
        static const uint8_t targets[6] = {'\n', 0x00, 0x01, 0x80, 0xFF, 0x7F};
        for (int round = 0; round < 3000; round++) {
            for (size_t k = 0; k < sizeof bpool; k++) bpool[k] = (uint8_t)xorshift32();
            uint8_t c = targets[round % 6];
            for (size_t k = 0; k < sizeof bpool; k++) if (bpool[k] == c) bpool[k] ^= 0x40;  /* c 를 지움 */
            size_t off = (size_t)round % 4u, len = round < 300 ? (size_t)round / 4 : xorshift32() % 300u;
            size_t want = len;
            if (len && (xorshift32() % 3u)) { want = xorshift32() % len; bpool[off + want] = c; }
            if (len > want + 1 && (xorshift32() & 1u)) bpool[off + want + 1] = c;   /* 바로 위에 또 c */
            if (find_byte(bpool + off, len, c) != want) badf++;
        }
        CHECK(badf == 0);
        CHECK(find_byte(bpool, 0, 0) == 0);
    }

    /* ---------- Part C: 엔코더 ---------- */
    {
        printf("encoder misread (all transitions x all subsets of changing bits):\n");
        printf("  bits | binary worst  bad/total            | gray worst  bad/total\n");
        int ok = 1;
        for (int nb = 2; nb <= 12; nb++) {
            uint64_t bb, bt, gb, gt;
            uint32_t wb = encoder_misread_worst(nb, false, &bb, &bt);
            uint32_t wg = encoder_misread_worst(nb, true, &gb, &gt);
            if (nb == 4 || nb == 8 || nb == 10 || nb == 12)
                printf("  %4d | %12u  %8llu/%-10llu | %10u  %llu/%llu\n", nb, wb,
                       (unsigned long long)bb, (unsigned long long)bt, wg,
                       (unsigned long long)gb, (unsigned long long)gt);
            if (wb != (1u << (nb - 1)) - 1u || wg != 0 || gb != 0 || gt != 2ull << nb) ok = 0;
        }
        CHECK(ok);

        /* 멀티턴 추적: 8비트(256칸), |속도| ≤ 60 칸/샘플, 200000 샘플, 10번째마다 전이 중 판독 */
        const int NB = 8;
        const uint32_t N = 1u << NB;
        for (int g = 1; g >= 0; g--) {
            enc_track_t clean = {0, 0, false}, noisy = {0, 0, false};
            rng = 0x1234567u;
            int64_t pos = 300, maxerr = 0, clean_err = 0, off = 0;
            int v = 0;
            unsigned misreads = 0;
            for (int s = 0; s < 200000; s++) {
                int step = (int)(xorshift32() % 7u) - 3;
                v += step; if (v > 60) v = 60; if (v < -60) v = -60;
                pos += v;
                uint32_t p = (uint32_t)(((pos % (int64_t)N) + (int64_t)N) % (int64_t)N);
                uint32_t code = g ? gray_enc(p) : p;
                if (s == 0) off = pos - (int64_t)p;             /* 첫 샘플은 한 바퀴 안 값으로 시작 */
                if (enc_track(&clean, code, NB, g) + off != pos) clean_err++;
                uint32_t raw = code;
                if (s % 10 == 5 && v != 0) {                      /* 마지막 한 칸 전이 중 판독 */
                    uint32_t prev = (p + (v > 0 ? N - 1u : 1u)) & (N - 1u);
                    uint32_t a = g ? gray_enc(prev) : prev;
                    uint32_t diff = a ^ code;
                    raw = a ^ (diff & xorshift32());            /* 바뀌는 비트 중 일부만 바뀐 상태 */
                    misreads++;
                }
                int64_t e = enc_track(&noisy, raw, NB, g) + off - pos;
                if (e < 0) e = -e;
                if (e > maxerr) maxerr = e;
            }
            int64_t final_err = noisy.pos + off - pos;
            printf("  multiturn %s: %u misreads, clean mismatches=%lld, max |err|=%lld, final err=%lld (= %lld turns)\n",
                   g ? "gray  " : "binary", misreads, (long long)clean_err, (long long)maxerr,
                   (long long)final_err, (long long)(final_err / (int64_t)N));
            CHECK(clean_err == 0);
            if (g) CHECK(maxerr <= 1 && final_err == 0);
            else CHECK(final_err != 0 && final_err % (int64_t)N == 0);
        }
        enc_track_t t = {0, 0, false};                           /* 경계: N/2-1 은 앞으로, N/2 는 뒤로 */
        enc_track(&t, gray_enc(0), 8, true);
        CHECK(enc_track(&t, gray_enc(127), 8, true) == 127 && enc_track(&t, gray_enc(255), 8, true) == -1);
    }

    /* ---------- Part C: CRC-8 이 못 잡는 오류 패턴 (Gosper 열거) ---------- */
    {
        CHECK(poly_rem8(0x107u) == 0 && poly_rem8(0x1u) == 1);
        int ok = 1;
        for (int n = 9; n <= 20; n++) {                           /* 2^n 브루트와 개수 대조 */
            uint64_t bu[5] = {0}, bp[5] = {0};
            for (uint32_t e = 1; e < (1u << n); e++) {
                unsigned w = pc(e);
                if (w <= 4) { bp[w]++; if (poly_rem8(e) == 0) bu[w]++; }
            }
            for (int k = 1; k <= 4; k++) {
                uint64_t pat;
                uint64_t u = crc8_undetected(n, k, &pat);
                if (u != bu[k] || pat != bp[k]) ok = 0;
            }
        }
        CHECK(ok);
        printf("CRC-8 poly 0x07, 32-bit codeword (24 data + 8 crc): undetected / patterns\n");
        uint64_t und[5], pat[5];
        for (int k = 1; k <= 4; k++) {
            und[k] = crc8_undetected(32, k, &pat[k]);
            printf("  weight %d: %llu / %llu\n", k, (unsigned long long)und[k], (unsigned long long)pat[k]);
        }
        CHECK(pat[1] == 32 && pat[2] == 496 && pat[3] == 4960 && pat[4] == 35960);
        CHECK(und[1] == 0 && und[2] == 0 && und[3] == 0 && und[4] > 0);
        uint64_t p32;
        crc8_undetected(32, 32, &p32);
        CHECK(p32 == 1);                                          /* 0xFFFFFFFF 하나, 다음은 0 = 끝 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
