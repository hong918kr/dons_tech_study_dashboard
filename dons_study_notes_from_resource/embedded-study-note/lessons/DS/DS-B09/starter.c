/* DS-B09 해시 함수 기초: FNV-1a, djb2, 분포 측정
 * 세 해시를 구현하고, 세 가지 키 집합에서 버킷 분포(카이제곱)와 충돌 수를 측정한다.
 * malloc 없음 — 키 집합과 측정 버퍼는 모두 정적 배열.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- Part A: 해시 함수 3개 ---------------- */
#define FNV32_OFFSET 2166136261u   /* 0x811C9DC5 */
#define FNV32_PRIME  16777619u     /* 0x01000193 = 2^24 + 2^8 + 0x93 */

/* FNV-1a: 바이트마다 XOR 먼저, 그다음 곱하기 */
static uint32_t fnv1a32(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    (void)p; (void)len;
    return 0; /* TODO: h = OFFSET; 바이트마다 h ^= b; h *= PRIME; */
}

/* djb2 (Bernstein): h = h * 33 + c, 시작값 5381 */
static uint32_t djb2(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    (void)p; (void)len;
    return 0; /* TODO: h = 5381; 바이트마다 h = h*33 + b; */
}

/* 단순 합: 바이트를 더하기만 한다 (나쁜 해시의 기준선) */
static uint32_t sum32(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    (void)p; (void)len;
    return 0; /* TODO: 바이트를 모두 더한다 */
}

/* ---------------- Part B: 품질 지표 ---------------- */
/* 카이제곱 = Σ (O_i - E)^2 / E,  E = n / m.  이상적인 해시면 ≈ m - 1 */
static double chi_square(const uint32_t *counts, unsigned m, unsigned n) {
    (void)counts; (void)m; (void)n;
    return 0.0; /* TODO: 버킷마다 (O - E)^2 / E 를 더한다 */
}

/* 32비트 해시 값 자체가 같은 키 수 = n - (서로 다른 해시 값 개수) */
#define MAX_KEYS 256
static uint32_t g_sorted[MAX_KEYS];
static void sort_u32(uint32_t *a, unsigned n) {          /* 삽입 정렬: n ≤ 256 이면 충분 */
    for (unsigned i = 1; i < n; i++) {
        uint32_t v = a[i];
        unsigned j = i;
        while (j > 0 && a[j - 1] > v) { a[j] = a[j - 1]; j--; }
        a[j] = v;
    }
}
static unsigned count_hash_collisions(const uint32_t *h, unsigned n) {
    (void)h; (void)n; (void)sort_u32; (void)g_sorted;
    return 0; /* TODO: g_sorted 에 복사 → 정렬 → 이웃끼리 같은 값 세기 */
}

/* ---------------- Part C: 키 집합과 측정 표 (하네스) ---------------- */
#define KEY_MAX 24
typedef struct {
    const char *name;
    unsigned n;
    uint8_t len[MAX_KEYS];
    uint8_t key[MAX_KEYS][KEY_MAX];
} KeySet;

#define NSETS 4
static KeySet g_sets[NSETS];

static void add_key(KeySet *ks, const void *k, size_t len) {
    memcpy(ks->key[ks->n], k, len);
    ks->len[ks->n] = (uint8_t)len;
    ks->n++;
}

static void build_key_sets(void) {
    static const char *dev[8]   = {"imu", "baro", "mag", "temp", "adc", "motor", "batt", "gps"};
    static const char *fld[8]   = {"x", "y", "z", "raw", "min", "max", "avg", "err"};
    static const char *verb[16] = {"get", "set", "clr", "read", "write", "start", "stop", "reset",
                                   "dump", "show", "enable", "disable", "cal", "test", "load", "save"};
    static const char *noun[16] = {"led", "pwm", "adc", "uart", "spi", "i2c", "can", "gpio",
                                   "timer", "dma", "flash", "rtc", "wdt", "clock", "power", "log"};
    char buf[KEY_MAX];

    g_sets[0].name = "sensor";               /* "imu0_x", "baro3_avg", ... 8*4*8 = 256 */
    for (unsigned d = 0; d < 8; d++)
        for (unsigned i = 0; i < 4; i++)
            for (unsigned f = 0; f < 8; f++) {
                int len = snprintf(buf, sizeof buf, "%s%u_%s", dev[d], i, fld[f]);
                add_key(&g_sets[0], buf, (size_t)len);
            }

    g_sets[1].name = "command";              /* "uart_get", "flash_save", ... 16*16 = 256 */
    for (unsigned v = 0; v < 16; v++)
        for (unsigned o = 0; o < 16; o++) {
            int len = snprintf(buf, sizeof buf, "%s_%s", noun[o], verb[v]);
            add_key(&g_sets[1], buf, (size_t)len);
        }

    g_sets[2].name = "seq-int";              /* LBA 0..255 를 4바이트 리틀 엔디언으로 */
    for (uint32_t lba = 0; lba < 256; lba++) {
        uint8_t b[4] = {(uint8_t)lba, (uint8_t)(lba >> 8), (uint8_t)(lba >> 16), (uint8_t)(lba >> 24)};
        add_key(&g_sets[2], b, 4);
    }

    g_sets[3].name = "stride8";              /* 4KiB 정렬 LBA: 0, 8, 16, ... (512B 섹터 8개 = 4KiB) */
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t lba = i * 8u;
        uint8_t b[4] = {(uint8_t)lba, (uint8_t)(lba >> 8), (uint8_t)(lba >> 16), (uint8_t)(lba >> 24)};
        add_key(&g_sets[3], b, 4);
    }
}

typedef uint32_t (*HashFn)(const void *, size_t);
typedef struct { double chi; unsigned max_load, empty, bucket_coll, hash_coll; } Stats;

#define NBUCKET 64u
static Stats measure(const KeySet *ks, HashFn fn, unsigned m) {
    static uint32_t counts[1024];
    static uint32_t hv[MAX_KEYS];
    Stats s = {0};
    memset(counts, 0, m * sizeof counts[0]);
    for (unsigned i = 0; i < ks->n; i++) {
        hv[i] = fn(ks->key[i], ks->len[i]);
        counts[hv[i] % m]++;
    }
    for (unsigned b = 0; b < m; b++) {
        if (counts[b] > s.max_load) s.max_load = counts[b];
        if (counts[b] == 0) s.empty++;
    }
    s.bucket_coll = ks->n - (m - s.empty);  /* 이미 누가 있는 버킷에 들어간 키 수 */
    s.chi = chi_square(counts, m, ks->n);
    s.hash_coll = count_hash_collisions(hv, ks->n);
    return s;
}

static const char *hash_name[3] = {"fnv1a", "djb2", "sum"};
static const HashFn hash_fn[3]  = {fnv1a32, djb2, sum32};

/* sqrt 없이(-lm 불필요) 표준편차를 계산하려고 뉴턴법 몇 번 */
static double approx_sqrt(double x) {
    double r = x > 1.0 ? x : 1.0;
    for (int i = 0; i < 30; i++) r = 0.5 * (r + x / r);
    return r;
}

static void print_table(unsigned m) {
    double sd = approx_sqrt(2.0 * (m - 1));          /* 카이제곱 분포(자유도 m-1)의 표준편차 */
    printf("\nkeys=256  buckets=%u  E=%.2f  ideal chi^2 = %u, sd = %.1f\n", m, 256.0 / m, m - 1, sd);
    printf("%-8s %-6s %8s %5s %6s %7s %7s\n", "keyset", "hash", "chi^2", "max", "empty", "b-coll", "h-coll");
    for (unsigned k = 0; k < NSETS; k++)
        for (unsigned h = 0; h < 3; h++) {
            Stats s = measure(&g_sets[k], hash_fn[h], m);
            printf("%-8s %-6s %8.1f %5u %6u %7u %7u\n", g_sets[k].name, hash_name[h],
                   s.chi, s.max_load, s.empty, s.bucket_coll, s.hash_coll);
        }
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t hs(HashFn fn, const char *s) { return fn(s, strlen(s)); }

int main(void) {
    /* Part A: 공식 참조값 (FNV 테스트 벡터, djb2 손계산) */
    CHECK(hs(fnv1a32, "") == 0x811C9DC5u);
    CHECK(hs(fnv1a32, "a") == 0xE40C292Cu);
    CHECK(hs(fnv1a32, "foobar") == 0xBF9CF968u);
    CHECK(hs(djb2, "") == 5381u);
    CHECK(hs(djb2, "a") == 177670u);          /* 5381*33 + 'a'(97) */
    CHECK(hs(djb2, "foobar") == 0xFDE460BEu);
    CHECK(hs(sum32, "abc") == 294u);
    CHECK(hs(sum32, "abc") == hs(sum32, "cba"));       /* 순서를 못 본다 */
    CHECK(hs(fnv1a32, "abc") != hs(fnv1a32, "cba"));

    /* Part B: 지표 함수 */
    uint32_t flat[4] = {4, 4, 4, 4}, skew[4] = {16, 0, 0, 0};
    CHECK(chi_square(flat, 4, 16) == 0.0);
    CHECK(chi_square(skew, 4, 16) == 48.0);   /* (12^2 + 3*4^2) / 4 */
    uint32_t hv[6] = {7, 3, 7, 9, 3, 7};
    CHECK(count_hash_collisions(hv, 6) == 3); /* 서로 다른 값 3개 → 6-3 */

    /* Part C: 실제 키 집합 측정 */
    build_key_sets();
    CHECK(g_sets[0].n == 256 && g_sets[1].n == 256 && g_sets[2].n == 256);
    Stats f_sensor = measure(&g_sets[0], fnv1a32, NBUCKET);
    Stats s_sensor = measure(&g_sets[0], sum32, NBUCKET);
    Stats s_cmd    = measure(&g_sets[1], sum32, NBUCKET);
    Stats f_seq    = measure(&g_sets[2], fnv1a32, NBUCKET);
    CHECK(f_sensor.hash_coll == 0);            /* 256개에서 32비트 값이 겹치지 않는다 */
    CHECK(f_sensor.chi < 63.0 + 3 * 11.3);     /* 무작위 기대 범위(평균 + 3sd) 안 */
    CHECK(s_sensor.hash_coll > 100);           /* 합 해시는 값 자체가 겹친다 */
    CHECK(s_cmd.hash_coll > 50);
    CHECK(f_seq.chi == 0.0);                   /* 순차 키 + 2^k 버킷: 완벽한 순열(너무 고르다) */

    print_table(NBUCKET);                      /* 2의 거듭제곱 버킷 */
    print_table(61);                           /* 소수 버킷 */
    print_table(1024);                         /* 희소 테이블: 부하율 0.25 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
