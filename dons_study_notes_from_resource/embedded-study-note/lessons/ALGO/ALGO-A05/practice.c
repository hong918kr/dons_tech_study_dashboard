/* ALGO-A05 추가 연습 — 한 번에 검증 (본문과 같은 LZSS 형식, 자기 완결)
 *   연습 1  lazy matching: 한 칸 미뤄 보고 더 긴 일치가 있으면 지금은 literal — 탐욕 대비 크기
 *   연습 2  최적 파싱(optimal parsing) DP: 토큰 비용이 고정(literal 9 bit, match 17 bit)이면 최단 경로 → 탐욕·lazy 와 비교,
 *           작은 입력은 모든 파싱을 다 해 보는 전수 탐색과 일치
 *   연습 3  제자리(in-place) 압축 해제: 압축본을 버퍼 끝에 두고 앞에서부터 풀 때 필요한 최소 여유(margin)
 *   연습 4  압축 폭탄 상한: 압축 c 바이트가 풀려 나올 수 있는 최대 길이 ≤ maxlen·8(c−6)/17 — 증명·확인
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ---------- 본문 형식의 최소 부품 (solution.c 와 같은 규칙) ---------- */
#define HDR 6u
typedef struct { unsigned obits, minm; } prm_t;
static size_t W_of(prm_t p)  { return (size_t)1 << p.obits; }
static size_t ML_of(prm_t p) { return p.minm + ((size_t)1 << (16 - p.obits)) - 1; }

static size_t mlen(const uint8_t *in, size_t n, size_t i, size_t j, size_t maxlen) {
    size_t lim = n - i < maxlen ? n - i : maxlen, k = 0;
    while (k < lim && in[j + k] == in[i + k]) k++;
    return k;
}
static size_t find(prm_t p, const uint8_t *in, size_t n, size_t i, size_t *dist) {   /* 최장, 같으면 가까운 쪽 */
    size_t W = W_of(p), ml = ML_of(p), best = 0, bd = 0, lo = i > W ? i - W : 0;
    for (size_t j = i; j-- > lo; ) {
        size_t L = mlen(in, n, i, j, ml);
        if (L > best) { best = L; bd = i - j; if (L == ml) break; }
    }
    *dist = bd;
    return best >= p.minm ? best : 0;
}

typedef struct { uint8_t *out; size_t o, flag_at; unsigned nbit, tokens; } tw_t;    /* 토큰 쓰개 (cap 은 lz_bound 로 보장) */
static void tw_init(tw_t *t, uint8_t *out, prm_t p, size_t n) {
    t->out = out; out[0] = (uint8_t)p.obits; out[1] = (uint8_t)p.minm;
    out[2] = (uint8_t)n; out[3] = (uint8_t)(n >> 8); out[4] = (uint8_t)(n >> 16); out[5] = (uint8_t)(n >> 24);
    t->o = HDR; t->nbit = 8; t->flag_at = 0; t->tokens = 0;
}
static void tw_flag(tw_t *t) { if (t->nbit == 8) { t->flag_at = t->o; t->out[t->o++] = 0; t->nbit = 0; } }
static void tw_lit(tw_t *t, uint8_t b) { tw_flag(t); t->out[t->o++] = b; t->nbit++; t->tokens++; }
static void tw_match(tw_t *t, prm_t p, size_t dist, size_t L) {
    tw_flag(t);
    unsigned w = (unsigned)(dist - 1) << (16 - p.obits) | (unsigned)(L - p.minm);
    t->out[t->o++] = (uint8_t)w; t->out[t->o++] = (uint8_t)(w >> 8);
    t->out[t->flag_at] |= (uint8_t)(1u << t->nbit); t->nbit++; t->tokens++;
}
static long enc_greedy(prm_t p, const uint8_t *in, size_t n, uint8_t *out) {
    tw_t t; tw_init(&t, out, p, n);
    for (size_t i = 0; i < n; ) {
        size_t d, L = find(p, in, n, i, &d);
        if (L) { tw_match(&t, p, d, L); i += L; } else tw_lit(&t, in[i++]);
    }
    return (long)t.o;
}
/* 디코더 (본문 lz_decode 와 같음) + 선택적 제자리 감시: out 이 아직 안 읽은 입력을 덮으려 하면 -100 */
static long dec(const uint8_t *in, size_t n, uint8_t *out, size_t cap, const uint8_t *inplace_base) {
    if (n < HDR) return -3;
    prm_t p = { in[0], in[1] };
    if (p.obits < 8 || p.obits > 12 || p.minm < 2 || p.minm > 4) return -2;
    size_t total = (size_t)in[2] | (size_t)in[3] << 8 | (size_t)in[4] << 16 | (size_t)in[5] << 24;
    if (total > cap) return -1;
    size_t ip = HDR, op = 0, start = inplace_base ? (size_t)(in - inplace_base) : 0;
    unsigned flags = 0, nbit = 8, lb = 16 - p.obits;
    while (op < total) {
        if (nbit == 8) { if (ip >= n) return -3; flags = in[ip++]; nbit = 0; }
        size_t len, dist = 0;
        if (flags >> nbit & 1u) {
            if (n - ip < 2) return -3;
            unsigned w = (unsigned)in[ip] | (unsigned)in[ip + 1] << 8; ip += 2;
            dist = (w >> lb) + 1; len = (w & ((1u << lb) - 1)) + p.minm;
            if (dist > op) return -4;
            if (len > total - op) return -5;
        } else { if (ip >= n) return -3; ip++; len = 1; }
        if (inplace_base && op + len > start + ip) return -100;      /* 마지막으로 쓸 칸 op+len−1 ≥ 안 읽은 첫 칸 */
        if (dist) for (size_t k = 0; k < len; k++, op++) out[op] = out[op - dist];
        else out[op++] = in[ip - 1];
        nbit++;
    }
    return ip == n ? (long)op : -6;
}
static size_t bound(size_t n) { return HDR + n + (n + 7) / 8; }
/* 유효한 스트림의 비트 비용 = 8·(본문 바이트 − 플래그 바이트) + 토큰 수 (플래그 비트 1개씩) */
static long stream_bits(const uint8_t *s, size_t c) {
    size_t ip = HDR, op = 0, total = (size_t)s[2] | (size_t)s[3] << 8 | (size_t)s[4] << 16 | (size_t)s[5] << 24;
    long bits = 0; unsigned flags = 0, nbit = 8;
    while (op < total && ip < c) {
        if (nbit == 8) { flags = s[ip++]; nbit = 0; continue; }
        if (flags >> nbit & 1u) { unsigned w = (unsigned)s[ip] | (unsigned)s[ip + 1] << 8; ip += 2; bits += 17;
            op += (w & ((1u << (16 - s[0])) - 1)) + s[1]; }
        else { ip++; op++; bits += 9; }
        nbit++;
    }
    return bits;
}

/* ---------- 연습 1: lazy matching ---------- */
static long enc_lazy(prm_t p, const uint8_t *in, size_t n, uint8_t *out) {
    tw_t t; tw_init(&t, out, p, n);
    for (size_t i = 0; i < n; ) {
        size_t d, L = find(p, in, n, i, &d);
        if (L && i + 1 < n) {
            size_t d2, L2 = find(p, in, n, i + 1, &d2);
            if (L2 > L) { tw_lit(&t, in[i++]); continue; }              /* 미룬다: 다음 칸이 더 길다 */
        }
        if (L) { tw_match(&t, p, d, L); i += L; } else tw_lit(&t, in[i++]);
    }
    return (long)t.o;
}

/* ---------- 연습 2: 최적 파싱 DP ----------
 * cost[i] = 위치 i 부터 끝까지의 최소 비트. cost[n] = 0,
 * cost[i] = min( 9 + cost[i+1],  17 + min_{L = minm..Lmax(i)} cost[i+L] )
 * Lmax(i) 가 되는 거리 d 에서 그보다 짧은 L 도 모두 일치한다 (접두사) → L 마다 거리를 따로 찾을 필요 없다. */
#define NMAX 40000
static long g_cost[NMAX + 1]; static uint32_t g_take[NMAX + 1], g_dist[NMAX + 1];
static long enc_optimal(prm_t p, const uint8_t *in, size_t n, uint8_t *out, long *bits) {
    g_cost[n] = 0;
    for (size_t i = n; i-- > 0; ) {
        size_t d, Lmax = find(p, in, n, i, &d);
        g_cost[i] = 9 + g_cost[i + 1]; g_take[i] = 1; g_dist[i] = 0;
        for (size_t L = p.minm; L <= Lmax; L++)
            if (17 + g_cost[i + L] < g_cost[i]) { g_cost[i] = 17 + g_cost[i + L]; g_take[i] = (uint32_t)L; g_dist[i] = (uint32_t)d; }
    }
    *bits = g_cost[0];
    tw_t t; tw_init(&t, out, p, n);
    for (size_t i = 0; i < n; ) {
        if (g_dist[i]) { tw_match(&t, p, g_dist[i], g_take[i]); i += g_take[i]; } else tw_lit(&t, in[i++]);
    }
    return (long)t.o;
}
/* 전수 탐색 (작은 n): 모든 위치에서 literal 또는 (창 안의 아무 j, 그 j 에서 가능한 아무 길이 L ≥ minm) */
static long brute_bits(prm_t p, const uint8_t *in, size_t n, size_t i) {
    if (i == n) return 0;
    long best = 9 + brute_bits(p, in, n, i + 1);
    size_t lo = i > W_of(p) ? i - W_of(p) : 0;
    for (size_t j = lo; j < i; j++) {
        size_t M = mlen(in, n, i, j, ML_of(p));
        for (size_t L = p.minm; L <= M; L++) { long c = 17 + brute_bits(p, in, n, i + L); if (c < best) best = c; }
    }
    return best;
}

/* ---------- 연습 3: 제자리 해제 여유 ----------
 * 버퍼 크기 n + m, 압축본(c B)을 끝 [n+m−c, n+m) 에 두고 앞에서부터 푼다. 토큰 k 를 다 읽은 뒤 입력 소비 in_k,
 * 그 토큰까지 쓴 출력 out_k 라 하면, 토큰 k 의 마지막 쓰기 (out_k − 1) 가 아직 안 읽은 첫 칸 (start + in_k) 보다 앞이어야:
 *   out_k ≤ start + in_k  ⇔  m ≥ out_k − in_k + c − n.   ∴ m* = max(0, max_k(out_k − in_k)) + c − n */
static long inplace_margin(const uint8_t *s, size_t c) {
    size_t ip = HDR, op = 0, n = (size_t)s[2] | (size_t)s[3] << 8 | (size_t)s[4] << 16 | (size_t)s[5] << 24;
    long D = 0; unsigned flags = 0, nbit = 8;
    while (op < n && ip < c) {
        if (nbit == 8) { flags = s[ip++]; nbit = 0; }
        if (flags >> nbit & 1u) { unsigned w = (unsigned)s[ip] | (unsigned)s[ip + 1] << 8; ip += 2;
            op += (w & ((1u << (16 - s[0])) - 1)) + s[1]; }
        else { ip++; op++; }
        nbit++;
        if ((long)op - (long)ip > D) D = (long)op - (long)ip;
    }
    return D + (long)c - (long)n;
}
static int try_inplace(const uint8_t *s, size_t c, const uint8_t *orig, size_t n, long m) {
    if (m < 0 || (long)n + m < (long)c) return -2;
    uint8_t *buf = malloc(n + (size_t)m + 1);
    if (!buf) return -2;
    size_t start = n + (size_t)m - c;
    memcpy(buf + start, s, c);
    long r = dec(buf + start, c, buf, n, buf);
    int ok = r == (long)n && memcmp(buf, orig, n) == 0 ? 1 : r == -100 ? -1 : 0;
    free(buf);
    return ok;                                                     /* 1 = 정상, -1 = 덮어쓰기 감지 */
}

/* ---------- 연습 4: 압축 폭탄 상한 ---------- */
static double bomb_bound(prm_t p, size_t c) { return (double)ML_of(p) * 8.0 * (double)(c - HDR) / 17.0; }

/* ---------- 데이터 ---------- */
static void make_log(uint8_t *b, size_t n, uint32_t *rng) {
    char line[96]; size_t o = 0; uint32_t t = 1000;
    while (o < n) {
        t += 7 + xs(rng) % 6;
        int len = snprintf(line, sizeof line, xs(rng) % 4 ? "[%07u.%03u] imu: ax=%d ay=%d\n" : "[%07u.%03u] bat: v=%dmV\n",
                           (unsigned)(t / 1000), (unsigned)(t % 1000), (int)(xs(rng) % 41) - 20 + 3900 * (int)(xs(rng) % 2), (int)(xs(rng) % 41) - 20);
        for (int k = 0; k < len && o < n; k++) b[o++] = (uint8_t)line[k];
    }
}
static uint8_t g_in[NMAX], g_a[NMAX + NMAX / 8 + 16], g_b[NMAX + NMAX / 8 + 16], g_c[NMAX + NMAX / 8 + 16], g_d[NMAX];

int main(void) {
    uint32_t rng = 0x5EEDu;
    printf("== 연습 1·2: 탐욕 vs lazy vs 최적 파싱 ==\n");
    {
        const char *names[3] = { "log 32K", "alpha4 32K", "random 32K" };
        prm_t p = { 10, 3 };
        int rt_ok = 1, order_ok = 1;
        for (int k = 0; k < 3; k++) {
            size_t n = 32768;
            if (k == 0) make_log(g_in, n, &rng);
            else for (size_t i = 0; i < n; i++) g_in[i] = (uint8_t)(k == 1 ? xs(&rng) % 4 : xs(&rng));
            long bo, a = enc_greedy(p, g_in, n, g_a), b = enc_lazy(p, g_in, n, g_b), c = enc_optimal(p, g_in, n, g_c, &bo);
            long ba = stream_bits(g_a, (size_t)a), bb = stream_bits(g_b, (size_t)b);
            printf("%-10s W=1024 min3: greedy %6ld B (%ld bit)  lazy %6ld B (%ld bit)  optimal %6ld B (%ld bit)\n",
                   names[k], a, ba, b, bb, c, bo);
            rt_ok &= dec(g_a, (size_t)a, g_d, n, NULL) == (long)n && memcmp(g_d, g_in, n) == 0;
            rt_ok &= dec(g_b, (size_t)b, g_d, n, NULL) == (long)n && memcmp(g_d, g_in, n) == 0;
            rt_ok &= dec(g_c, (size_t)c, g_d, n, NULL) == (long)n && memcmp(g_d, g_in, n) == 0;
            order_ok &= bo <= ba && bo <= bb && stream_bits(g_c, (size_t)c) == bo;
        }
        CHECK(rt_ok);                       /* 세 파서 모두 같은 디코더로 풀린다 */
        CHECK(order_ok);                    /* 최적 ≤ 탐욕, 최적 ≤ lazy (비트 모델), DP 값 == 실제 스트림 비트 */
        int bad = 0;
        for (int t = 0; t < 300; t++) {     /* 무작위 짧은 입력: 최적 ≤ 둘 다, 왕복 */
            size_t n = 1 + xs(&rng) % 400; prm_t q = { 8 + xs(&rng) % 5, 2 + xs(&rng) % 3 };
            for (size_t i = 0; i < n; i++) g_in[i] = (uint8_t)(xs(&rng) % (1 + t % 4));
            long bo, a = enc_greedy(q, g_in, n, g_a), b = enc_lazy(q, g_in, n, g_b), c = enc_optimal(q, g_in, n, g_c, &bo);
            if (bo > stream_bits(g_a, (size_t)a) || bo > stream_bits(g_b, (size_t)b)) bad++;
            if (dec(g_c, (size_t)c, g_d, n, NULL) != (long)n || memcmp(g_d, g_in, n) != 0) bad++;
            if (dec(g_b, (size_t)b, g_d, n, NULL) != (long)n || memcmp(g_d, g_in, n) != 0) bad++;
        }
        CHECK(bad == 0);
        int mism = 0; long cnt = 0;         /* {a,b} 위 길이 0..10 전부: DP == 전수 탐색 */
        for (unsigned mm = 2; mm <= 3; mm++)
            for (size_t len = 0; len <= 10; len++)
                for (uint32_t bits = 0; bits < (1u << len); bits++) {
                    for (size_t i = 0; i < len; i++) g_in[i] = (uint8_t)(bits >> i & 1u ? 'b' : 'a');
                    prm_t q = { 8, mm }; long bo;
                    enc_optimal(q, g_in, len, g_c, &bo);
                    mism += bo != brute_bits(q, g_in, len, 0); cnt++;
                }
        printf("optimal DP vs brute force over all parses: %ld strings, %d mismatches\n", cnt, mism);
        CHECK(mism == 0);
        /* 탐욕이 지는 작은 예: lazy 가 이기는 문자열 */
        const uint8_t *s = (const uint8_t *)"abcXbcdefYabcdefZ";
        prm_t q = { 8, 3 }; long bo;
        long a = enc_greedy(q, s, 17, g_a), b = enc_lazy(q, s, 17, g_b);
        enc_optimal(q, s, 17, g_c, &bo);
        printf("\"abcXbcdefYabcdefZ\": greedy %ld bit, lazy %ld bit, optimal %ld bit\n", stream_bits(g_a, (size_t)a),
               stream_bits(g_b, (size_t)b), bo);
        CHECK(stream_bits(g_b, (size_t)b) < stream_bits(g_a, (size_t)a));
    }
    printf("== 연습 3: 제자리 해제 여유 ==\n");
    {
        static const char *nm[3] = { "log", "zeros|random", "random" };
        int ok = 1;
        for (int k = 0; k < 3; k++) {       /* 가운데: 앞 절반은 0 (잘 압축), 뒤 절반은 난수 (안 압축) */
            size_t n = 16384; prm_t p = { 10, 3 };
            if (k == 0) make_log(g_in, n, &rng);
            else for (size_t i = 0; i < n; i++) g_in[i] = (uint8_t)(k == 1 && i < n / 2 ? 0 : xs(&rng));
            long c = enc_greedy(p, g_in, n, g_a), m = inplace_margin(g_a, (size_t)c);
            int at = try_inplace(g_a, (size_t)c, g_in, n, m), below = try_inplace(g_a, (size_t)c, g_in, n, m - 1);
            printf("%-12s n=%zu c=%5ld  n-c=%6ld  margin m*=%4ld  (m*: %s, m*-1: %s)\n", nm[k], n, c, (long)n - c, m,
                   at == 1 ? "ok" : "FAIL", below == -1 ? "overwrite detected" : below == -2 ? "buffer < stream" : "??");
            ok &= at == 1 && (below == -1 || below == -2);
        }
        CHECK(ok);
        int bad = 0, det = 0, small = 0;
        for (int t = 0; t < 200; t++) {     /* 압축 잘 되는 앞부분 + 덜 되는 뒷부분, 비율 무작위 */
            size_t n = 1 + xs(&rng) % 3000, cut = xs(&rng) % (n + 1); prm_t p = { 8 + xs(&rng) % 5, 2 + xs(&rng) % 3 };
            for (size_t i = 0; i < n; i++) g_in[i] = (uint8_t)(i < cut ? xs(&rng) % 2 : xs(&rng) % (1 + t % 256));
            long c = enc_greedy(p, g_in, n, g_a), m = inplace_margin(g_a, (size_t)c);
            if (try_inplace(g_a, (size_t)c, g_in, n, m) != 1) bad++;
            int b = try_inplace(g_a, (size_t)c, g_in, n, m - 1);
            if (b == -1) det++; else if (b == -2) small++; else bad++;
        }
        printf("random in-place: 200 streams, m* always ok; m*-1 -> overwrite detected %d, buffer < stream %d\n", det, small);
        CHECK(bad == 0 && det > 0);         /* m* 에서는 항상 성공, m*−1 이면 항상 덮어쓰기 감지(또는 공간 부족) */
    }
    printf("== 연습 4: 압축 폭탄 상한 ==\n");
    {
        prm_t p = { 8, 3 };                 /* maxlen 258 */
        /* 최대로 부푸는 스트림: literal 1개 + 거리 1, 길이 258 match 를 계속 */
        size_t k = 63, n = 1 + k * ML_of(p);
        tw_t t; tw_init(&t, g_a, p, n);
        tw_lit(&t, 'Z');
        for (size_t i = 0; i < k; i++) tw_match(&t, p, 1, ML_of(p));
        long r = dec(g_a, t.o, g_d, NMAX, NULL);
        printf("bomb: %zu B -> %ld B (x%.1f), bound %.1f B\n", t.o, r, (double)r / (double)t.o, bomb_bound(p, t.o));
        CHECK(r == (long)n && (double)r <= bomb_bound(p, t.o));
        int bad = 0;                        /* 정상 스트림 전부 상한 이하 */
        for (int i = 0; i < 300; i++) {
            size_t m = 1 + xs(&rng) % 5000; prm_t q = { 8 + xs(&rng) % 5, 2 + xs(&rng) % 3 };
            for (size_t j = 0; j < m; j++) g_in[j] = (uint8_t)(xs(&rng) % (1 + i % 3));
            long c = enc_greedy(q, g_in, m, g_a);
            if ((double)m > bomb_bound(q, (size_t)c)) bad++;
            if ((size_t)c > bound(m)) bad++;              /* 반대쪽 상한 (본문 lz_bound) */
        }
        CHECK(bad == 0);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
