/* DS-B09 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 본문의 세 해시 (그대로) */
static uint32_t fnv1a32(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}
static uint32_t djb2(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 5381u;
    for (size_t i = 0; i < len; i++) h = (h << 5) + h + p[i];
    return h;
}
static uint32_t sum32(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 0;
    for (size_t i = 0; i < len; i++) h += p[i];
    return h;
}

/* 연습 1: 정수 키의 "항등 해시" (lba % 64) vs Knuth 곱셈 해시
 * 4KiB 정렬 LBA (0, 8, 16, ...) 는 하위 3비트가 항상 0 → lba & 63 은 8개 버킷만 쓴다. */
static unsigned identity_bucket(uint32_t lba) { return lba & 63u; }
static unsigned knuth_bucket(uint32_t lba)    { return (lba * 2654435761u) >> (32 - 6); } /* 상위 6비트 */

static unsigned used_buckets(unsigned (*fn)(uint32_t), uint32_t stride) {
    bool used[64] = {false};
    unsigned cnt = 0;
    for (uint32_t i = 0; i < 256; i++) {
        unsigned b = fn(i * stride);
        if (!used[b]) { used[b] = true; cnt++; }
    }
    return cnt;
}

/* 연습 2: 애벌랜치(avalanche) — 입력 1비트를 뒤집으면 출력 몇 비트가 바뀌나? 이상적으론 16 */
static unsigned popcount32(uint32_t x) { unsigned c = 0; while (x) { x &= x - 1u; c++; } return c; }

static double avalanche(uint32_t (*fn)(const void *, size_t)) {
    uint32_t total = 0, trials = 0;
    for (uint32_t k = 0; k < 256; k++) {
        uint32_t key = k * 2654435761u;              /* 다양한 4바이트 키 */
        uint8_t b[4] = {(uint8_t)key, (uint8_t)(key >> 8), (uint8_t)(key >> 16), (uint8_t)(key >> 24)};
        uint32_t h0 = fn(b, 4);
        for (unsigned bit = 0; bit < 32; bit++) {
            b[bit / 8] ^= (uint8_t)(1u << (bit % 8));
            total += popcount32(h0 ^ fn(b, 4));
            b[bit / 8] ^= (uint8_t)(1u << (bit % 8));
            trials++;
        }
    }
    return (double)total / trials;
}

/* 연습 3: 마무리 섞기(finalizer) — murmur3 fmix32. 분포는 고칠 수 있어도 충돌은 못 고친다 */
static uint32_t fmix32(uint32_t h) {
    h ^= h >> 16; h *= 0x85EBCA6Bu;
    h ^= h >> 13; h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}
static uint32_t sum_fmix(const void *d, size_t n) { return fmix32(sum32(d, n)); }

static char g_keys[256][16];
static void build_sensor_keys(void) {
    static const char *dev[8] = {"imu", "baro", "mag", "temp", "adc", "motor", "batt", "gps"};
    static const char *fld[8] = {"x", "y", "z", "raw", "min", "max", "avg", "err"};
    unsigned n = 0;
    for (unsigned d = 0; d < 8; d++)
        for (unsigned i = 0; i < 4; i++)
            for (unsigned f = 0; f < 8; f++)
                snprintf(g_keys[n++], sizeof g_keys[0], "%s%u_%s", dev[d], i, fld[f]);
}
static unsigned hash_collisions(uint32_t (*fn)(const void *, size_t)) {
    static uint32_t hv[256];
    unsigned dup = 0;
    for (unsigned i = 0; i < 256; i++) {
        hv[i] = fn(g_keys[i], strlen(g_keys[i]));
        for (unsigned j = 0; j < i; j++)
            if (hv[j] == hv[i]) { dup++; break; }   /* 앞에 같은 값이 있으면 충돌 1 */
    }
    return dup;
}
static double chi64(uint32_t (*fn)(const void *, size_t)) {
    uint32_t cnt[64] = {0};
    for (unsigned i = 0; i < 256; i++) cnt[fn(g_keys[i], strlen(g_keys[i])) & 63u]++;
    double chi = 0;
    for (unsigned b = 0; b < 64; b++) chi += (cnt[b] - 4.0) * (cnt[b] - 4.0) / 4.0;
    return chi;
}

/* 연습 4: 스트리밍 FNV-1a — UART로 조각조각 들어오는 프레임을 버퍼링 없이 해시 */
static uint32_t fnv1a32_update(uint32_t h, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

/* 연습 5: 시드 있는 FNV — offset basis 자리에 비밀 시드를 섞는다 (해시 플러딩 완화) */
static uint32_t fnv1a32_seeded(uint32_t seed, const char *s) {
    return fnv1a32_update(2166136261u ^ seed, s, strlen(s));
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1 */
    unsigned id_used = used_buckets(identity_bucket, 8), kn_used = used_buckets(knuth_bucket, 8);
    printf("stride8 LBA, 64 buckets: identity uses %u, knuth uses %u\n", id_used, kn_used);
    CHECK(id_used == 8);
    CHECK(kn_used == 64);
    CHECK(used_buckets(identity_bucket, 1) == 64);   /* 순차 키엔 항등 해시도 완벽 */

    /* 연습 2 */
    double av_f = avalanche(fnv1a32), av_d = avalanche(djb2), av_s = avalanche(sum32);
    printf("avalanche (ideal 16.0): fnv1a %.2f  djb2 %.2f  sum %.2f\n", av_f, av_d, av_s);
    CHECK(av_s < 3.0);
    CHECK(av_f > av_d);
    CHECK(av_f > 8.0);

    /* 연습 3 */
    build_sensor_keys();
    unsigned c_sum = hash_collisions(sum32), c_mix = hash_collisions(sum_fmix);
    printf("sensor keys: sum h-coll %u, fmix(sum) h-coll %u; chi64 sum %.1f, fmix(sum) %.1f\n",
           c_sum, c_mix, chi64(sum32), chi64(sum_fmix));
    CHECK(c_sum == 127 && c_mix == 127);            /* 잃어버린 정보는 되돌릴 수 없다 */
    CHECK(hash_collisions(fnv1a32) == 0);

    /* 연습 4 */
    const char frame[] = "$GPGGA,123519,4807.038,N";
    uint32_t h = 2166136261u;
    h = fnv1a32_update(h, frame, 7);
    h = fnv1a32_update(h, frame + 7, 10);
    h = fnv1a32_update(h, frame + 17, sizeof frame - 1 - 17);
    CHECK(h == fnv1a32(frame, sizeof frame - 1));

    /* 연습 5 */
    CHECK(fnv1a32_seeded(0, "imu0_x") == fnv1a32("imu0_x", 6));
    unsigned same_lo = 0, same_hi = 0;
    for (unsigned i = 0; i < 256; i++) {
        uint32_t b0 = fnv1a32_seeded(0, g_keys[i]) & 63u;
        if ((fnv1a32_seeded(0x0000002Eu, g_keys[i]) & 63u) == b0) same_lo++;  /* 하위 비트가 다른 시드 */
        if ((fnv1a32_seeded(0x12340000u, g_keys[i]) & 63u) == b0) same_hi++;  /* 상위 비트만 다른 시드 */
    }
    printf("seed change: low-bit seed %u/256 same bucket, high-bit seed %u/256 same bucket\n",
           same_lo, same_hi);
    CHECK(same_lo == 0);      /* 하위 6비트 상태가 다르면 끝까지 다르다 (곱셈은 아래로 전파 안 됨) */
    CHECK(same_hi == 256);    /* 상위 비트 시드는 h & 63 에 아무 영향이 없다! */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
