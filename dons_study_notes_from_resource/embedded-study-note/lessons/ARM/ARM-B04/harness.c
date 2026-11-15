/* ARM-B04 로드/스토어와 주소 지정 모드 — 테스트 하네스 (starter.S / solution.S 공용)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S shared_bugs.S -o /tmp/ARM-B04_s
 * 모든 asm 결과를 C 참조 구현과 비교한다 (고정 시드 xorshift64 랜덤 + 이름 붙은 경계값).
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------- 센서 로그 레코드 (Part C) — asm 의 REC_* 상수와 반드시 일치 ---------- */
typedef struct {
    uint32_t id;    /* 센서 번호 */
    int32_t  temp;  /* 0.01 도 단위, 음수 가능 */
    uint64_t ts;    /* 타임스탬프 (us) */
} rec_t;
_Static_assert(offsetof(rec_t, id)   == 0,  "REC_ID");
_Static_assert(offsetof(rec_t, temp) == 4,  "REC_TEMP");
_Static_assert(offsetof(rec_t, ts)   == 8,  "REC_TS");
_Static_assert(sizeof(rec_t)         == 16, "REC_SIZE");

/* ---------- asm 함수 (starter.S / solution.S) ---------- */
uint64_t sum_imm(const uint64_t *a, size_t n);   /* [xn, #imm]          */
uint64_t sum_post(const uint64_t *a, size_t n);  /* [xn], #imm          */
uint64_t sum_pre(const uint64_t *a, size_t n);   /* [xn, #imm]!         */
uint64_t sum_reg(const uint64_t *a, size_t n);   /* [xn, xm, lsl #3]    */
int64_t  sum_gather_s32(const int32_t *base, const int32_t *idx, size_t n); /* [xn, wm, sxtw #2] */
void     copy_ldr_str(uint64_t *dst, const uint64_t *src, size_t n);
void     copy_ldp_stp(uint64_t *dst, const uint64_t *src, size_t n);
uint64_t sum_u8(const uint8_t *a, size_t n);     /* LDRB  */
int64_t  sum_s16(const int16_t *a, size_t n);    /* LDRSH */
int64_t  sum_s32(const int32_t *a, size_t n);    /* LDRSW */
int64_t  rec_sum_temp(const rec_t *r, size_t n);
uint64_t rec_sum_ts_for_id(const rec_t *r, size_t n, uint32_t id);
rec_t   *rec_log_push(rec_t *slot, uint32_t id, int32_t temp, uint64_t ts);
/* shared_bugs.S: 일부러 틀린 버전 */
int64_t  sum_s32_bug(const int32_t *a, size_t n);
void     copy_ldp_stp_notail(uint64_t *dst, const uint64_t *src, size_t n);

/* ---------- C 참조 구현 (wrap 은 unsigned 로 계산 → UB 없음) ---------- */
static uint64_t ref_sum_u64(const uint64_t *a, size_t n) {
    uint64_t s = 0;
    for (size_t i = 0; i < n; i++) s += a[i];
    return s;
}
static int64_t ref_gather(const int32_t *base, const int32_t *idx, size_t n) {
    int64_t s = 0;                       /* |합| <= n * 2^31 → n < 2^32 이면 넘치지 않음 */
    for (size_t i = 0; i < n; i++) s += base[idx[i]];
    return s;
}
static uint64_t ref_sum_u8(const uint8_t *a, size_t n) {
    uint64_t s = 0;
    for (size_t i = 0; i < n; i++) s += a[i];
    return s;
}
static int64_t ref_sum_s16(const int16_t *a, size_t n) {
    int64_t s = 0;
    for (size_t i = 0; i < n; i++) s += a[i];
    return s;
}
static int64_t ref_sum_s32(const int32_t *a, size_t n) {
    int64_t s = 0;
    for (size_t i = 0; i < n; i++) s += a[i];
    return s;
}
static int64_t ref_rec_sum_temp(const rec_t *r, size_t n) {
    int64_t s = 0;
    for (size_t i = 0; i < n; i++) s += r[i].temp;
    return s;
}
static uint64_t ref_rec_sum_ts(const rec_t *r, size_t n, uint32_t id) {
    uint64_t s = 0;
    for (size_t i = 0; i < n; i++) if (r[i].id == id) s += r[i].ts;
    return s;
}

/* ---------- 결정적 난수 ---------- */
static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t xs64(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

/* ---------- 하네스 ---------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

#define MAXN 40
typedef uint64_t (*sum_fn)(const uint64_t *, size_t);
typedef void (*copy_fn)(uint64_t *, const uint64_t *, size_t);

/* n = 0..MAXN 전부, 매번 새 랜덤 데이터 → 하나라도 다르면 0 */
static int sum_ok_all_n(sum_fn f) {
    uint64_t a[MAXN];
    for (size_t n = 0; n <= MAXN; n++) {
        for (size_t i = 0; i < n; i++) a[i] = xs64();
        if (f(a, n) != ref_sum_u64(a, n)) return 0;
    }
    return 1;
}

#define GUARD 0xDEADBEEFCAFEF00Dull
/* dst 앞뒤에 가드 4개씩. dst 시작을 0/1 원소 밀어서 16바이트 정렬/비정렬 둘 다 */
static int copy_ok_all_n(copy_fn f) {
    _Alignas(16) uint64_t buf[4 + 1 + MAXN + 4];
    uint64_t src[MAXN];
    for (size_t shift = 0; shift <= 1; shift++) {
        for (size_t n = 0; n <= MAXN; n++) {
            for (size_t i = 0; i < sizeof buf / sizeof buf[0]; i++) buf[i] = GUARD;
            for (size_t i = 0; i < n; i++) src[i] = xs64();
            uint64_t *dst = buf + 4 + shift;
            f(dst, src, n);
            if (n && memcmp(dst, src, n * sizeof src[0]) != 0) return 0;
            for (size_t i = 0; i < 4 + shift; i++) if (buf[i] != GUARD) return 0;       /* 앞 가드 */
            for (uint64_t *p = dst + n; p < buf + sizeof buf / sizeof buf[0]; p++)    /* 뒤 가드 */
                if (*p != GUARD) return 0;
        }
    }
    return 1;
}

int main(void) {
    /* ===== Part A: 모드별 합 · gather · 복사 ===== */
    sum_fn sums[4] = { sum_imm, sum_post, sum_pre, sum_reg };
    const char *names[4] = { "sum_imm", "sum_post", "sum_pre", "sum_reg" };
    const uint64_t ex[5] = { 1, 2, 3, 4, 5 };
    const uint64_t wrap[2] = { UINT64_MAX, 2 };          /* 2^64 - 1 + 2 → 1 (mod 2^64) */
    for (int k = 0; k < 4; k++) {
        int ok = sums[k](NULL, 0) == 0                    /* n = 0 이면 메모리를 건드리면 안 됨 */
              && sums[k](ex, 5) == 15 && sums[k](ex, 1) == 1 && sums[k](ex, 4) == 10
              && sums[k](wrap, 2) == 1;
        if (!ok) printf("  (%s: named cases)\n", names[k]);
        CHECK(ok);
        CHECK(sum_ok_all_n(sums[k]));
    }

    /* gather: base 가 배열 가운데를 가리키고, idx 는 -32..31 (음수 = base 앞쪽) */
    int32_t pool[64];
    for (int i = 0; i < 64; i++) pool[i] = (int32_t)(xs64() & 0xFFFFFFFFu);
    pool[0] = INT32_MIN; pool[63] = INT32_MAX; pool[31] = -1;
    const int32_t *base = pool + 32;
    const int32_t named_idx[4] = { -32, 31, -1, 0 };      /* INT32_MIN, INT32_MAX, -1, pool[32] */
    CHECK(sum_gather_s32(base, named_idx, 4) == (int64_t)INT32_MIN + INT32_MAX - 1 + pool[32]);
    CHECK(sum_gather_s32(base, named_idx, 0) == 0);
    {
        int ok = 1;
        int32_t idx[MAXN];
        for (size_t n = 0; n <= MAXN; n++) {
            for (size_t i = 0; i < n; i++) idx[i] = (int32_t)(xs64() % 64) - 32;
            if (sum_gather_s32(base, idx, n) != ref_gather(base, idx, n)) ok = 0;
        }
        CHECK(ok);
    }
    CHECK(copy_ok_all_n(copy_ldr_str));
    CHECK(copy_ok_all_n(copy_ldp_stp));

    /* ===== Part B: 폭 · 부호 확장 · 심어 둔 버그 ===== */
    {
        uint8_t all[256 + 16];
        for (int i = 0; i < 256; i++) all[i] = (uint8_t)i;
        CHECK(sum_u8(all, 256) == 32640);                   /* 0+1+...+255, 255 가 -1 이 되면 안 됨 */
        int ok = 1;                                          /* 시작 주소 0..15 바이트 어긋남 × 길이 0..40 */
        for (size_t off = 0; off < 16; off++)
            for (size_t n = 0; n <= MAXN; n++) {
                for (size_t i = 0; i < n; i++) all[off + i] = (uint8_t)xs64();
                if (sum_u8(all + off, n) != ref_sum_u8(all + off, n)) ok = 0;
            }
        CHECK(ok);
    }
    {
        const int16_t h[4] = { INT16_MIN, INT16_MAX, -1, 0 };
        CHECK(sum_s16(h, 4) == -2);
        CHECK(sum_s16(h, 1) == -32768);
        int16_t r[MAXN]; int ok = 1;
        for (size_t n = 0; n <= MAXN; n++) {
            for (size_t i = 0; i < n; i++) r[i] = (int16_t)(uint16_t)xs64();
            if (sum_s16(r, n) != ref_sum_s16(r, n)) ok = 0;
        }
        CHECK(ok);
    }
    {
        const int32_t w[4] = { INT32_MIN, INT32_MAX, -1, 0 };
        CHECK(sum_s32(w, 4) == -2);
        CHECK(sum_s32(w, 1) == INT32_MIN);
        int32_t r[MAXN]; int ok = 1;
        for (size_t n = 0; n <= MAXN; n++) {
            for (size_t i = 0; i < n; i++) r[i] = (int32_t)(uint32_t)xs64();
            if (sum_s32(r, n) != ref_sum_s32(r, n)) ok = 0;
        }
        CHECK(ok);
        /* 심어 둔 버그: 양수만 넣는 테스트로는 못 잡고, 음수 하나면 잡힌다 */
        const int32_t pos[3] = { 1, 2, 3 }, neg[3] = { 1, -1, 3 };
        CHECK(sum_s32_bug(pos, 3) == ref_sum_s32(pos, 3));
        CHECK(sum_s32_bug(neg, 3) != ref_sum_s32(neg, 3));
        CHECK(sum_s32_bug(neg, 3) == 4294967299ll);          /* 1 + 0xFFFFFFFF + 3 */
        /* 심어 둔 버그 2: 짝수 길이 테스트만 있으면 통과, 홀수 하나면 잡힌다 */
        uint64_t d[6] = { 0 }; const uint64_t s6[5] = { 11, 22, 33, 44, 55 };
        copy_ldp_stp_notail(d, s6, 4);
        CHECK(memcmp(d, s6, 4 * sizeof s6[0]) == 0);
        copy_ldp_stp_notail(d, s6, 5);
        CHECK(d[4] == 0);                                    /* 꼬리 누락 확인 */
        CHECK(copy_ok_all_n(copy_ldp_stp_notail) == 0);      /* 랜덤 테스트가 잡는다 */
    }

    /* ===== Part C: 센서 로그 레코드 ===== */
    {
        const rec_t log3[3] = { { 1, -250, 1000 }, { 2, 3150, 2000 }, { 1, INT32_MIN, 3000 } };
        CHECK(rec_sum_temp(log3, 3) == -250 + 3150 + (int64_t)INT32_MIN);
        CHECK(rec_sum_ts_for_id(log3, 3, 1) == 4000);
        CHECK(rec_sum_ts_for_id(log3, 3, 7) == 0);
        rec_t r[MAXN]; int ok = 1;
        for (size_t n = 0; n <= MAXN; n++) {
            for (size_t i = 0; i < n; i++) {
                r[i].id = (uint32_t)(xs64() % 4);            /* id 0..3 */
                r[i].temp = (int32_t)(uint32_t)xs64();
                r[i].ts = xs64();
            }
            uint32_t want = (uint32_t)(xs64() % 4);
            if (rec_sum_temp(r, n) != ref_rec_sum_temp(r, n)) ok = 0;
            if (rec_sum_ts_for_id(r, n, want) != ref_rec_sum_ts(r, n, want)) ok = 0;
        }
        CHECK(ok);
        /* id 비교는 하위 32비트만: temp(상위 32비트)가 같은 비트를 가져도 영향 없어야 */
        const rec_t trick[2] = { { 5, 5, 10 }, { 6, 5, 20 } };
        CHECK(rec_sum_ts_for_id(trick, 2, 5) == 10);

        rec_t ring[4];
        memset(ring, 0xA5, sizeof ring);
        rec_t *p = rec_log_push(ring, 7, -40, 123456789ull);
        CHECK(p == ring + 1);
        CHECK(ring[0].id == 7 && ring[0].temp == -40 && ring[0].ts == 123456789ull);
        p = rec_log_push(p, 0xFFFFFFFFu, INT32_MAX, UINT64_MAX);
        CHECK(p == ring + 2 && ring[1].id == 0xFFFFFFFFu && ring[1].temp == INT32_MAX
              && ring[1].ts == UINT64_MAX);
        CHECK(ring[2].id == 0xA5A5A5A5u && ring[3].ts == 0xA5A5A5A5A5A5A5A5ull); /* 옆 슬롯 무사 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
