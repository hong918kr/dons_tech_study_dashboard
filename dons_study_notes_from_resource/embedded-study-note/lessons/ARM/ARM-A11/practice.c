/* ARM-A11 추가 연습 — 답 + 테스트 (벡터화 판정은 bash check_vec.sh practice)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/ARM-A11_practice && /tmp/ARM-A11_practice
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define KERNEL __attribute__((noinline))
#define NOVEC _Pragma("clang loop vectorize(disable) interleave(disable)")

/* 연습 1: 폭·인터리브를 pragma 로 고정 → 리마크가 (4, 1) 로 바뀐다. 결과는 dot_i16 과 같다
 * -O1(ASan 빌드)에서는 벡터라이저가 돌지 않아 "-Wpass-failed" 경고가 난다(실측) → 이 함수에서만 끈다 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpass-failed"
KERNEL int32_t dot_i16_w4(const int16_t *a, const int16_t *b, size_t n) {
    int32_t s = 0;
#pragma clang loop vectorize_width(4) interleave_count(1)
    for (size_t i = 0; i < n; i++) s += a[i] * b[i];
    return s;
}
#pragma clang diagnostic pop

/* 연습 2: AoS(구조체 배열)에서 한 필드만 합 → 보폭 4 의 interleaved group → ld4.8h */
struct imu { int16_t ax, ay, az, gx; };
KERNEL int32_t sum_ax(const struct imu *s, size_t n) {
    int32_t t = 0;
    for (size_t i = 0; i < n; i++) t += s[i].ax;
    return t;
}

/* 연습 3: 실행 시간 보폭 → 컴파일러가 "st == 1" 버전을 따로 만든다 (stride versioning) */
KERNEL int32_t sum_stride(const int32_t *a, size_t n, size_t st) {
    int32_t t = 0;
    for (size_t i = 0; i < n; i++) t += a[i * st];
    return t;
}

/* 연습 4: 제자리 누적합 — 반복 i 가 반복 i-1 의 결과를 읽는다 (loop-carried dependence) → 벡터화 불가 */
KERNEL void prefix_sum(int32_t *a, size_t n) {
    for (size_t i = 1; i < n; i++) a[i] += a[i - 1];
}

/* 연습 5: 펌웨어용 hist4 — uint16 카운터로 스택 2 KB. 카운터가 넘치기 전에 32비트 h 로 비운다 */
#define CHUNK (4u * 65532u)   /* 262128: 한 청크에서 c[k][b] <= 65532, c[0] 은 꼬리 3개까지 더해 65535 = UINT16_MAX */
KERNEL void hist4_u16(uint32_t *h, const uint8_t *p, size_t n) {
    uint16_t c[4][256];
    while (n > 0) {
        size_t m = n < CHUNK ? n : CHUNK, m4 = m & ~(size_t)3, i;
        memset(c, 0, sizeof c);
        for (i = 0; i < m4; i += 4) {
            c[0][p[i]]++;
            c[1][p[i + 1]]++;
            c[2][p[i + 2]]++;
            c[3][p[i + 3]]++;
        }
        for (; i < m; i++) c[0][p[i]]++;
        for (int b = 0; b < 256; b++) h[b] += (uint32_t)c[0][b] + c[1][b] + c[2][b] + c[3][b];
        p += m;
        n -= m;
    }
}

/* ---------------- 테스트 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t xs(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }

static int64_t ref_dot(const int16_t *a, const int16_t *b, size_t n) {
    int64_t s = 0;
    NOVEC for (size_t i = 0; i < n; i++) s += (int64_t)a[i] * b[i];
    return s;
}

#define N 4096
static int16_t sa[N], sb[N];
static struct imu imu[N];
static int32_t ia[8 * N], ib[8 * N];
static uint8_t px[300000];

int main(void) {
    /* 연습 1 */
    {
        int bad = 0;
        for (size_t n = 0; n <= 200; n++) {
            for (size_t i = 0; i < n; i++) { sa[i] = (int16_t)((int)(xs() % 4001) - 2000); sb[i] = (int16_t)((int)(xs() % 4001) - 2000); }
            bad += dot_i16_w4(sa, sb, n) != ref_dot(sa, sb, n);
        }
        CHECK(bad == 0);
    }
    /* 연습 2 */
    {
        int bad = 0;
        for (size_t n = 0; n <= 300; n++) {
            int64_t r = 0;
            for (size_t i = 0; i < n; i++) {
                imu[i] = (struct imu){ (int16_t)xs(), (int16_t)xs(), (int16_t)xs(), (int16_t)xs() };
                r += imu[i].ax;
            }
            bad += sum_ax(imu, n) != r;   /* |r| <= 300·32768 < 2^31 */
        }
        CHECK(bad == 0);
        for (size_t i = 0; i < N; i++) imu[i] = (struct imu){ INT16_MIN, 1, 2, 3 };
        CHECK(sum_ax(imu, N) == -32768 * N);
    }
    /* 연습 3: 보폭 0(같은 원소 반복), 1(버전 경로), 2, 3, 7 */
    {
        int bad = 0;
        for (size_t i = 0; i < 8 * N; i++) ia[i] = (int32_t)(xs() % 20001) - 10000;
        size_t strides[] = { 0, 1, 2, 3, 7 };
        for (size_t k = 0; k < 5; k++)
            for (size_t n = 0; n <= N; n += (n < 40 ? 1 : 97)) {
                if (n * strides[k] >= 8 * N) continue;
                int64_t r = 0;
                for (size_t i = 0; i < n; i++) r += ia[i * strides[k]];
                bad += sum_stride(ia, n, strides[k]) != r;
            }
        CHECK(bad == 0);
    }
    /* 연습 4 */
    {
        int bad = 0;
        for (size_t n = 0; n <= 100; n++) {
            for (size_t i = 0; i < n; i++) ia[i] = ib[i] = (int32_t)(xs() % 2001) - 1000;
            prefix_sum(ia, n);
            NOVEC for (size_t i = 1; i < n; i++) ib[i] += ib[i - 1];
            bad += memcmp(ia, ib, n * sizeof(int32_t)) != 0;
        }
        CHECK(bad == 0);
        int32_t one[6] = { 1, 1, 1, 1, 1, 1 };
        prefix_sum(one, 6);
        CHECK(one[0] == 1 && one[5] == 6);   /* 벡터로 "옛 값"을 읽었다면 one[5] == 2 */
    }
    /* 연습 5: 같은 값만 들어오면 칸 하나에 몰린다 → 청크 경계 근처 길이에서 uint16 넘침이 없는지 */
    {
        uint32_t h[256], r[256];
        memset(px, 0x5A, sizeof px);
        int bad = 0;
        size_t same[] = { 262127, 262128, 262129, 262131, 262139, 300000 };
        for (size_t k = 0; k < sizeof same / sizeof same[0]; k++) {
            memset(h, 0, sizeof h);
            hist4_u16(h, px, same[k]);
            bad += h[0x5A] != same[k] || h[0x59] != 0;
        }
        CHECK(bad == 0);
        size_t lens[] = { 0, 1, 2, 3, 4, 5, 65531, 65533, 262127, 262129, 262133, 299999 };
        for (size_t k = 0; k < sizeof lens / sizeof lens[0]; k++) {
            for (size_t i = 0; i < lens[k]; i++) px[i] = (uint8_t)(xs() >> ((k & 1) ? 62 : 56));   /* 4값 / 256값 */
            for (int b = 0; b < 256; b++) h[b] = r[b] = (uint32_t)b;
            hist4_u16(h, px, lens[k]);
            NOVEC for (size_t i = 0; i < lens[k]; i++) r[px[i]]++;
            bad += memcmp(h, r, sizeof h) != 0;
        }
        CHECK(bad == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
