/* DS-M14 블룸 필터: 확률적 집합
 * m비트 비트맵(호출자 제공, DS-B08) + 키당 k개 위치 — 더블 해싱 g_i = h1 + i*h2 (mod m).
 * Part A 기본 연산 / Part B 거짓 양성률 이론식 vs 실측, 최적 k / Part C 플래시 읽기 건너뛰기.
 * malloc 없음. m 은 2의 거듭제곱 → 인덱스 = g & (m-1).
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BLOOM_K_MAX 32u

typedef struct {
    uint32_t *w;                         /* m/32 워드 비트맵 — 호출자 제공 (정적 배열) */
    uint32_t  m;                         /* 비트 수 (2의 거듭제곱, >= 32) */
    uint32_t  mask;                      /* m - 1 */
    uint32_t  k;                         /* 키당 세우는 비트 수 = "해시 함수 개수" */
    uint32_t  n;                         /* add 호출 수 (통계용) */
} bloom_t;

/* ---------------- 주어짐: 해시 재료 (DS-B09) ---------------- */
static uint64_t fnv1a64(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 0xCBF29CE484222325ull;              /* offset basis */
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 0x00000100000001B3ull; }
    return h;
}
/* MurmurHash3 fmix64: 모든 입력 비트를 모든 출력 비트로 섞는 전단사 함수 */
static uint64_t fmix64(uint64_t x) {
    x ^= x >> 33; x *= 0xFF51AFD7ED558CCDull;
    x ^= x >> 33; x *= 0xC4CEB9FE1A85EC53ull;
    x ^= x >> 33;
    return x;
}

static bool is_pow2(uint32_t x) { return x && !(x & (x - 1u)); }
static bool bit_test(const uint32_t *w, uint32_t i) { return (w[i >> 5] >> (i & 31u)) & 1u; }
static void bit_set(uint32_t *w, uint32_t i) { w[i >> 5] |= 1u << (i & 31u); }

static bool bloom_init(bloom_t *b, uint32_t *words, uint32_t m_bits, uint32_t k) {
    if (!words || m_bits < 32u || !is_pow2(m_bits) || k == 0 || k > BLOOM_K_MAX) return false;
    b->w = words; b->m = m_bits; b->mask = m_bits - 1u; b->k = k; b->n = 0;
    memset(words, 0, (m_bits / 32u) * sizeof words[0]);
    return true;
}
static uint32_t bloom_popcount(const bloom_t *b) {
    uint32_t c = 0;
    for (uint32_t i = 0; i < b->m / 32u; i++) c += (uint32_t)__builtin_popcount(b->w[i]);
    return c;
}

/* ---------------- Part A: 해시 2개 → k 위치, add / maybe_contains ---------------- */
/* 64비트 해시 하나를 반으로 갈라 h1, h2. h2 는 반드시 홀수:
 * m = 2^b 에서 홀수는 mod m 의 가역원 → g_0..g_{k-1} 이 모두 다른 비트 (k <= m) */
static void bloom_hash2(const void *key, size_t len, uint32_t *h1, uint32_t *h2) {
    (void)key; (void)len; (void)fmix64;
    /* TODO: h = fmix64(fnv1a64(key, len)); h1 = 하위 32비트, h2 = 상위 32비트 | 1 (홀수!) */
    *h1 = 0; *h2 = 0;
}

static void bloom_add(bloom_t *b, const void *key, size_t len) {
    (void)b; (void)key; (void)len; (void)bit_set;
    /* TODO: bloom_hash2 → g = h1 부터 k 번: bit_set(g & mask), g += h2. 마지막에 n++ */
}

/* false = 확실히 없음, true = 아마 있음 (거짓 양성 가능) */
static bool bloom_maybe_contains(const bloom_t *b, const void *key, size_t len) {
    (void)b; (void)key; (void)len;
    /* TODO: 같은 k 위치를 차례로 bit_test — 0 이 하나라도 있으면 false */
    return false;
}

static void bloom_add_u32(bloom_t *b, uint32_t key) { bloom_add(b, &key, sizeof key); }
static bool bloom_has_u32(const bloom_t *b, uint32_t key) { return bloom_maybe_contains(b, &key, sizeof key); }

/* ---------------- Part B: 이론식, 최적 k, 크기 계산 ---------------- */
/* 독립 근사: P(비트=0) ~ e^{-kn/m},  FP ~ (1 - e^{-kn/m})^k */
static double bloom_fp_theory(double n, double m, double k) {
    (void)n; (void)m; (void)k;
    /* TODO: (1 - e^{-kn/m})^k */
    return 0.0;
}

/* 실수 최적 k* = (m/n) ln 2. 정수는 floor/ceil 중 이론 FP 가 작은 쪽, [1, BLOOM_K_MAX] */
static uint32_t bloom_k_opt(double m, double n) {
    (void)m; (void)n;
    /* TODO: k* = (m/n) ln 2 → floor/ceil 중 bloom_fp_theory 가 작은 쪽. n<=0 이면 1, 상한 BLOOM_K_MAX */
    return 0;
}

/* 목표 FP eps 를 최적 k 에서 맞추는 최소 m = ceil(-n ln(eps) / (ln 2)^2) */
static uint32_t bloom_bits_for(uint32_t n, double eps) {
    (void)n; (void)eps;
    /* TODO: ceil(-n ln(eps) / (ln 2)^2) */
    return 0;
}

/* ---------------- Part C: 세그먼트별 필터로 플래시 읽기 건너뛰기 ---------------- */
#define SEG_CNT      16u
#define SEG_KEYS     256u
#define SEG_BITS_MAX 4096u

typedef struct { uint32_t key[SEG_KEYS], val[SEG_KEYS]; } seg_t;   /* 플래시에 있는 세그먼트 */

typedef struct {
    seg_t    flash[SEG_CNT];                         /* "플래시" — flash_read() 로만 읽는다 */
    uint32_t used[SEG_CNT];                          /* RAM 메타: 세그먼트별 엔트리 수 */
    uint32_t fbits[SEG_CNT][SEG_BITS_MAX / 32u];     /* RAM: 세그먼트별 필터 비트맵 */
    bloom_t  filt[SEG_CNT];
    uint32_t cur;                                    /* 지금 쓰는(가장 새) 세그먼트 */
    bool     use_filter;
    uint32_t flash_reads;                            /* 통계: 세그먼트 읽기 횟수 */
} kv_t;

/* seg_bits == 0 → 필터 없음 (기준선) */
static bool kv_init(kv_t *s, uint32_t seg_bits, uint32_t k) {
    memset(s->used, 0, sizeof s->used);
    s->cur = 0; s->flash_reads = 0; s->use_filter = seg_bits != 0;
    if (!s->use_filter) return true;
    if (seg_bits > SEG_BITS_MAX) return false;
    for (uint32_t i = 0; i < SEG_CNT; i++)
        if (!bloom_init(&s->filt[i], s->fbits[i], seg_bits, k)) return false;
    return true;
}
/* 플래시 페이지 읽기 (NAND tR 수십 us) — 비용을 센다 */
static const seg_t *flash_read(kv_t *s, uint32_t i) { s->flash_reads++; return &s->flash[i]; }

/* append-only: 지금 세그먼트가 차면 다음으로. 전부 차면 false (컴팩션은 DS-A10) */
static bool kv_put(kv_t *s, uint32_t key, uint32_t val) {
    (void)s; (void)key; (void)val;
    /* TODO: cur 가 가득이면 다음 세그먼트로(마지막이면 false). 엔트리 append + 필터에 add */
    return false;
}

/* 새 세그먼트 → 옛 세그먼트 순으로. 필터가 "없음"이면 그 세그먼트는 읽지 않는다 */
static bool kv_get(kv_t *s, uint32_t key, uint32_t *val) {
    (void)s; (void)key; (void)val; (void)flash_read;
    /* TODO: i = cur .. 0 (새 → 옛). 필터가 "없음"이면 건너뛰고, 아니면 flash_read 후
     *       엔트리를 뒤에서부터 찾는다. 찾으면 *val 채우고 true */
    return false;
}

/* ---------------- 검사 도구 (주어짐) ---------------- */
/* 불변식: 파라미터 일관성 + 켜진 비트 수 <= min(m, k*n) */
static bool bloom_check(const bloom_t *b) {
    if (!is_pow2(b->m) || b->mask != b->m - 1u || b->k == 0 || b->k > BLOOM_K_MAX) return false;
    uint64_t cap = (uint64_t)b->k * b->n;
    uint32_t pc = bloom_popcount(b);
    return pc <= b->m && pc <= cap;
}

/* 참조 모델: 비트당 1바이트, 위치는 64비트 산술 % m 으로 독립 계산 */
static uint8_t g_ref_bits[4096];
static void ref_add(uint32_t m, uint32_t k, uint32_t key) {
    uint32_t h1, h2;
    bloom_hash2(&key, sizeof key, &h1, &h2);
    for (uint64_t i = 0; i < k; i++) g_ref_bits[((uint64_t)h1 + i * h2) % m] = 1;
}
static bool ref_equal(const bloom_t *b) {
    for (uint32_t i = 0; i < b->m; i++) if (bit_test(b->w, i) != (g_ref_bits[i] != 0)) return false;
    return true;
}

static uint32_t rng_state = 1u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}
/* MurmurHash3 fmix32 — 전단사라 서로 다른 i 는 서로 다른 키 */
static uint32_t fmix32(uint32_t x) {
    x ^= x >> 16; x *= 0x85EBCA6Bu; x ^= x >> 13; x *= 0xC2B2AE35u; x ^= x >> 16;
    return x;
}

/* 실측: 짝수 키 n 개 넣고, 홀수 키 q 개(확실히 없음)로 거짓 양성률을 잰다 */
typedef struct { double fill, fill_th, fp, fp_th, fp_cond, sigma; uint32_t fn; } fp_row;
static uint32_t g_big[(1u << 20) / 32u];            /* 최대 m = 2^20 비트 = 128 KB */
static fp_row measure(uint32_t n, uint32_t m, uint32_t k, uint32_t q) {
    fp_row r = { 0, 0, 0, 0, 0, 0, 0 };
    bloom_t b;
    if (!bloom_init(&b, g_big, m, k)) return r;
    for (uint32_t i = 0; i < n; i++) bloom_add_u32(&b, i << 1);
    for (uint32_t i = 0; i < n; i++) r.fn += !bloom_has_u32(&b, i << 1);   /* 거짓 음성: 0 이어야 */
    uint32_t hit = 0;
    for (uint32_t j = 0; j < q; j++) hit += bloom_has_u32(&b, (j << 1) | 1u);
    r.fill = (double)bloom_popcount(&b) / m;
    r.fill_th = 1.0 - exp(-(double)k * n / m);
    r.fp = (double)hit / q;
    r.fp_th = bloom_fp_theory(n, m, k);
    r.fp_cond = pow(r.fill, k);                      /* 실제 채움률 rho 로 본 rho^k */
    r.sigma = sqrt(r.fp_cond * (1.0 - r.fp_cond) / q);   /* 질의 q 개의 이항 표준편차 */
    return r;
}

static kv_t g_kv;
static uint32_t g_val[SEG_CNT * SEG_KEYS];           /* 참조 모델: 키 번호 i → 최신 값 */

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    static uint32_t w[4096 / 32];
    bloom_t b;

    /* Part A: init 검증, 빈 필터, 키 하나 = 정확히 k 비트, 멱등성 */
    {
        CHECK(!bloom_init(&b, w, 1000, 3) && !bloom_init(&b, w, 16, 3) && !bloom_init(&b, w, 1024, 0)
              && !bloom_init(&b, w, 1024, BLOOM_K_MAX + 1u));
        CHECK(fnv1a64("", 0) == 0xCBF29CE484222325ull && fnv1a64("a", 1) == 0xAF63DC4C8601EC8Cull);
        bloom_init(&b, w, 1024, 7);
        CHECK(!bloom_maybe_contains(&b, "imu0", 4) && bloom_popcount(&b) == 0);
        bloom_add(&b, "imu0", 4);
        CHECK(bloom_maybe_contains(&b, "imu0", 4) && bloom_popcount(&b) == 7 && bloom_check(&b));
        uint32_t before[1024 / 32];
        memcpy(before, w, sizeof before);
        bloom_add(&b, "imu0", 4);                                  /* 같은 키 다시: 비트 불변 */
        CHECK(memcmp(before, w, sizeof before) == 0);
        /* h2 홀수 → m = 32, k = 32 이면 키 하나가 32비트를 전부 켠다 (i*h2 mod 32 가 순열) */
        bool all = true, odd = true;
        for (uint32_t key = 0; key < 2000 && all; key++) {
            uint32_t h1, h2;
            bloom_hash2(&key, sizeof key, &h1, &h2);
            odd = odd && (h2 & 1u);
            bloom_init(&b, w, 32, 32);
            bloom_add_u32(&b, key);
            all = bloom_popcount(&b) == 32;
        }
        CHECK(odd && all);
    }
    /* Part A: 랜덤 add 3000회 — 매번 참조 비트맵과 비트 단위 일치 + 불변식, 거짓 음성 0 */
    {
        const uint32_t m = 4096, k = 5;
        static uint32_t keys[3000];
        bloom_init(&b, w, m, k);
        memset(g_ref_bits, 0, sizeof g_ref_bits);
        rng_state = 0xB100Fu;
        bool ok = true;
        uint32_t prev_pc = 0;
        for (uint32_t i = 0; i < 3000 && ok; i++) {
            keys[i] = xorshift32();
            bloom_add_u32(&b, keys[i]);
            ref_add(m, k, keys[i]);
            uint32_t pc = bloom_popcount(&b);
            ok = ref_equal(&b) && bloom_check(&b) && pc >= prev_pc && bloom_has_u32(&b, keys[i]);
            prev_pc = pc;
            if (ok && (i & 255u) == 255u)                          /* 주기적으로 전체 재확인 */
                for (uint32_t j = 0; j <= i && ok; j++) ok = bloom_has_u32(&b, keys[j]);
        }
        for (uint32_t j = 0; j < 3000 && ok; j++) ok = bloom_has_u32(&b, keys[j]);
        CHECK(ok);
        printf("[A] random 3000 adds vs reference bitmap: %s (fill %.3f)\n",
               ok ? "match" : "MISMATCH", (double)bloom_popcount(&b) / m);
    }

    /* Part B: 공식 값, 최적 k, 크기 계산 */
    {
        CHECK(fabs(bloom_fp_theory(1000, 10000, 7) - 0.008194) < 2e-6);
        CHECK(bloom_k_opt(8.0 * 1000, 1000) == 6 && bloom_k_opt(10.0 * 1000, 1000) == 7
              && bloom_k_opt(16.0 * 1000, 1000) == 11 && bloom_k_opt(1000, 4000) == 1
              && bloom_k_opt(1e9, 1) == BLOOM_K_MAX);
        uint32_t m = bloom_bits_for(1000, 0.01);
        CHECK(m == 9586 && bloom_k_opt(m, 1000) == 7 && bloom_fp_theory(1000, m, 7) < 0.0101);
    }
    /* Part B: (n, m, k) 조합별 실측 vs 이론 */
    {
        static const uint32_t cfg[][3] = {
            { 8192, 65536, 2 }, { 8192, 65536, 4 }, { 8192, 65536, 6 }, { 8192, 65536, 10 },
            { 6554, 65536, 7 }, { 4096, 65536, 11 }, { 128, 1024, 6 }, { 104858, 1u << 20, 7 },
        };
        const uint32_t q = 200000;
        bool ok = true;
        printf("[B]      n        m   m/n   k | fill  1-e^(-kn/m) | FP measured  FP theory  fill^k   | meas/theory\n");
        for (unsigned c = 0; c < sizeof cfg / sizeof cfg[0]; c++) {
            fp_row r = measure(cfg[c][0], cfg[c][1], cfg[c][2], q);
            /* 필터 하나의 FP 는 그 필터의 채움률 rho 로 정해진다(rho^k). rho 자체도 m 이 작으면 흔들린다 */
            double s_fill = sqrt(r.fill_th * (1.0 - r.fill_th) / cfg[c][1]);
            bool row_ok = r.fn == 0 && r.fp_th > 0 && fabs(r.fill - r.fill_th) <= 4.0 * s_fill
                          && fabs(r.fp - r.fp_cond) <= 4.0 * r.sigma;
            ok = ok && row_ok;
            printf("[B] %6u %8u %5.1f %3u | %.3f  %.3f       | %.5f      %.5f    %.5f  | %.3f%s\n",
                   cfg[c][0], cfg[c][1], (double)cfg[c][1] / cfg[c][0], cfg[c][2], r.fill, r.fill_th,
                   r.fp, r.fp_th, r.fp_cond, r.fp_th > 0 ? r.fp / r.fp_th : 0.0, row_ok ? "" : "  <-- off");
        }
        CHECK(ok);
    }
    /* Part B: k 를 1..14 로 바꿔 가며 — m/n = 10 에서 최적 k 확인 */
    {
        const uint32_t n = 6554, m = 65536, q = 200000;
        double best_th = 2, best_me = 2, me_at_opt = 0;
        uint32_t k_th = 0, k_me = 0, k_opt = bloom_k_opt(m, n);
        printf("[B] k scan (n=%u, m=%u, m/n=%.2f, (m/n)ln2=%.2f, k_opt=%u)\n", n, m, (double)m / n,
               (double)m / n * log(2.0), k_opt);
        printf("[B]  k | FP theory | FP measured\n");
        for (uint32_t k = 1; k <= 14; k++) {
            fp_row r = measure(n, m, k, q);
            if (r.fp_th < best_th) { best_th = r.fp_th; k_th = k; }
            if (r.fp < best_me) { best_me = r.fp; k_me = k; }
            if (k == k_opt) me_at_opt = r.fp;
            printf("[B] %2u | %.5f   | %.5f\n", k, r.fp_th, r.fp);
        }
        CHECK(k_opt == 7 && k_th == k_opt && k_me >= 6 && k_me <= 8 && me_at_opt <= 1.1 * best_me);
    }

    /* Part C: 기본 동작 — 덮어쓰기, 없는 키, 가득 참 */
    {
        uint32_t v = 0;
        CHECK(kv_init(&g_kv, 2048, 6) && kv_put(&g_kv, 1, 100) && kv_put(&g_kv, 2, 200));
        CHECK(kv_get(&g_kv, 1, &v) && v == 100 && !kv_get(&g_kv, 3, &v));
        kv_init(&g_kv, 2048, 6);
        for (uint32_t i = 0; i < SEG_KEYS; i++) kv_put(&g_kv, i, i);  /* seg 0 가득 */
        kv_put(&g_kv, 5, 555);                                         /* seg 1 에 새 값 */
        g_kv.flash_reads = 0;
        CHECK(g_kv.cur == 1 && kv_get(&g_kv, 5, &v) && v == 555 && g_kv.flash_reads == 1);
        CHECK(kv_get(&g_kv, 6, &v) && v == 6);
        bool ok = true;
        for (uint32_t i = SEG_KEYS + 1u; i < SEG_CNT * SEG_KEYS; i++) ok = ok && kv_put(&g_kv, 1000u + i, i);
        CHECK(ok && !kv_put(&g_kv, 7, 7));                             /* 4096 개 후 거절 */
    }
    /* Part C: 필터 크기별 플래시 읽기 수 — 결과는 필터 유무와 무관해야 한다 */
    {
        static const uint32_t bits[] = { 0, 1024, 2048, 4096 };
        const uint32_t lookups = 20000;
        double abs_reads[4] = { 0 };
        bool ok = true;
        printf("[C] filter      k   RAM(16 seg) | absent: reads/lookup  theory | present: reads/lookup\n");
        for (unsigned c = 0; c < 4; c++) {
            uint32_t k = bits[c] ? bloom_k_opt(bits[c], SEG_KEYS) : 0;
            kv_init(&g_kv, bits[c], k);
            rng_state = 0x5EEDu;
            uint32_t nk = 0;                                   /* 지금까지 만든 서로 다른 키 수 */
            for (uint32_t p = 0; p < SEG_CNT * SEG_KEYS; p++) {
                uint32_t r = xorshift32();
                uint32_t i = (nk > 0 && r % 10u == 0) ? (r >> 8) % nk : nk++;   /* 10%는 갱신 */
                g_val[i] = r;
                ok = ok && kv_put(&g_kv, fmix32(i), r);
            }
            uint32_t v, rd_abs = 0, rd_pre = 0;
            for (uint32_t t = 0; t < lookups && ok; t++) {
                uint32_t r = xorshift32(), before = g_kv.flash_reads;
                if (t & 1u) {                                  /* 있는 키 */
                    uint32_t i = r % nk;
                    ok = kv_get(&g_kv, fmix32(i), &v) && v == g_val[i];
                    rd_pre += g_kv.flash_reads - before;
                } else {                                       /* 확실히 없는 키 (i >= 2^20) */
                    ok = !kv_get(&g_kv, fmix32((1u << 20) + r % (1u << 20)), &v);
                    rd_abs += g_kv.flash_reads - before;
                }
            }
            abs_reads[c] = (double)rd_abs / (lookups / 2);
            double th = bits[c] ? SEG_CNT * bloom_fp_theory(SEG_KEYS, bits[c], k) : SEG_CNT;
            printf("[C] %4u b (%2u/key) %2u  %5u B     | %8.4f            %7.4f | %8.3f\n", bits[c],
                   bits[c] / SEG_KEYS, k, bits[c] * SEG_CNT / 8u, abs_reads[c], th,
                   (double)rd_pre / (lookups / 2));
        }
        CHECK(ok);
        CHECK(abs_reads[0] == SEG_CNT && abs_reads[1] < 3.0 && abs_reads[2] < 0.6 && abs_reads[3] < 0.05);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
