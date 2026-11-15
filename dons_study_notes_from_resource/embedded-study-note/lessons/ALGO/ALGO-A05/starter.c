/* ALGO-A05 LZ77 압축: 슬라이딩 윈도 인코더/디코더
 *
 * LZSS (LZ77 의 Storer–Szymanski 변형). 이 레슨이 정한 바이트 형식:
 *   헤더 6 B : [0] obits (8..12, 창 W = 2^obits)  [1] min_match (2..4)  [2..5] 원본 길이 n (uint32 LE)
 *   본문     : 플래그 바이트 1개 + 토큰 최대 8개 를 반복. 플래그 bit k (LSB 먼저) = 토큰 k 의 종류
 *              0 = literal 1 B (원본 바이트 그대로)
 *              1 = match 2 B : w = b0 | b1<<8,  dist = (w >> lbits) + 1  (1..W),
 *                              len = (w & (2^lbits − 1)) + min_match,  lbits = 16 − obits
 *   Part A  lz_find_naive()  창 전체를 훑는 최장 일치 / lz_encode() 탐욕(greedy) 인코더 / lz_decode() 평면 디코더
 *   Part B  lz_find_hash()   해시 체인 탐색 (max_chain = 0 이면 naive 와 같은 바이트열) + 손상 입력 안전성
 *   Part C  lzs_push()       바이트 하나씩 먹는 스트리밍 디코더: W 바이트 링 + 상태 → 256 B 플래시 페이지 쓰기
 *
 * 알고리즘 안에는 malloc 없음 (해시 표·링은 호출자가 준다). 테스트 하네스만 malloc 을 쓴다:
 * 퍼즈 입력/출력을 "정확한 크기" 힙 버퍼에 담아 ASan 이 1바이트 넘침도 잡게 하려고.
 * 모든 테스트는 결정적 (xorshift32 고정 시드).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================= 형식과 파라미터 (주어진 코드) ======================= */
#define LZ_HDR       6u
#define LZ_OBITS_MIN 8u
#define LZ_OBITS_MAX 12u
#define LZ_WMAX      (1u << LZ_OBITS_MAX)   /* 4096 */
#define LZ_HBITS     12u                    /* 해시 표 4096 칸 */

enum { LZ_OK = 0, LZ_E_CAP = -1, LZ_E_HDR = -2, LZ_E_TRUNC = -3, LZ_E_DIST = -4,
       LZ_E_OVERRUN = -5, LZ_E_TRAIL = -6, LZ_E_ARG = -7 };

typedef struct { unsigned obits, min_match, max_chain; } lz_params;   /* max_chain 0 = 무제한 (해시 방식만 씀) */
typedef enum { LZ_NAIVE, LZ_HASH } lz_method;

static unsigned lz_lbits(const lz_params *p)  { return 16u - p->obits; }
static size_t   lz_window(const lz_params *p) { return (size_t)1 << p->obits; }
static size_t   lz_maxlen(const lz_params *p) { return p->min_match + ((size_t)1 << lz_lbits(p)) - 1; }
static int lz_params_ok(const lz_params *p) {
    return p->obits >= LZ_OBITS_MIN && p->obits <= LZ_OBITS_MAX && p->min_match >= 2 && p->min_match <= 4;
}
/* 최악 출력 크기: 토큰마다 "덮는 원본 바이트 수 ≥ 본문 바이트 수" 이고 플래그는 토큰 8개당 1 B → n + ⌈n/8⌉ + 헤더 */
static size_t lz_bound(size_t n) { return LZ_HDR + n + (n + 7) / 8; }

static uint32_t rd32(const uint8_t *b) {
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
static void wr32(uint8_t *b, uint32_t v) {
    b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24);
}
static int lz_read_header(const uint8_t *in, size_t n, lz_params *p, size_t *total) {
    if (n < LZ_HDR) return LZ_E_TRUNC;
    p->obits = in[0]; p->min_match = in[1]; p->max_chain = 0;
    if (!lz_params_ok(p)) return LZ_E_HDR;
    *total = rd32(in + 2);
    return LZ_OK;
}

/* in[i..] 와 in[j..] (j < i) 가 몇 바이트 같은가 (최대 maxlen, 입력 끝까지).
 * j + k 가 i 를 넘어가도 된다 (겹치는 일치, 예: "aaaa…" 는 dist 1) — 디코더가 바이트 단위로 복사하니까. */
static size_t lz_match_len(const uint8_t *in, size_t n, size_t i, size_t j, size_t maxlen) {
    size_t lim = n - i < maxlen ? n - i : maxlen, k = 0;
    while (k < lim && in[j + k] == in[i + k]) k++;
    return k;
}

/* ======================= Part A — naive 탐색 ======================= */
/* 창 [i−W, i−1] 의 모든 시작점 j 를 "가까운 것부터" 본다. 가장 긴 일치, 길이가 같으면 가장 가까운 것(dist 최소).
 * 반환 = 일치 길이 (min_match 미만이면 0 = literal 로 보내라), *dist = i − j. */
static size_t lz_find_naive(const lz_params *p, const uint8_t *in, size_t n, size_t i, size_t *dist) {
    (void)p; (void)in; (void)n; (void)i; (void)lz_match_len; (void)lz_maxlen;
    *dist = 0;
    return 0; /* TODO: j = i−1, i−2, …, max(0, i−W) 를 가까운 것부터 lz_match_len 으로 재고,
               * 엄격히 길 때만 갱신 (같은 길이면 가까운 쪽), min_match 미만이면 0 */
}

/* ======================= Part B — 해시 체인 ======================= */
/* head[h] = 해시 h 를 가진 가장 최근 위치, prev[pos mod W] = 같은 해시의 바로 앞 위치 (-1 = 없음).
 * 메모리 = 4·4096 + 4·4096 = 32 KiB (호스트/로거용. MCU 인코더라면 uint16 상대 위치로 반으로 — Q&A). */
typedef struct {
    int32_t head[1u << LZ_HBITS];
    int32_t prev[LZ_WMAX];
} lz_hash_t;

static uint32_t lz_hash(const uint8_t *s, unsigned m) {          /* 앞 m(2..4) 바이트의 곱셈 해시 */
    uint32_t v = (uint32_t)s[0] | (uint32_t)s[1] << 8;
    if (m >= 3) v |= (uint32_t)s[2] << 16;
    if (m >= 4) v |= (uint32_t)s[3] << 24;
    return (v * 2654435761u) >> (32u - LZ_HBITS);
}
static void lz_hash_reset(lz_hash_t *h) {
    for (size_t k = 0; k < (1u << LZ_HBITS); k++) h->head[k] = -1;
}
/* 위치 i 를 자기 해시 체인 맨 앞에 넣는다. 뒤에 min_match 바이트가 안 남은 위치는 해시를 못 만들어 건너뛴다. */
static void lz_hash_insert(lz_hash_t *h, const lz_params *p, const uint8_t *in, size_t n, size_t i) {
    if (i + p->min_match > n) return;
    uint32_t k = lz_hash(in + i, p->min_match);
    h->prev[i & (lz_window(p) - 1)] = h->head[k];
    h->head[k] = (int32_t)i;
}
/* 전제: 위치 0..i−1 중 해시 가능한 것은 모두 넣었고 i 는 아직 안 넣었다.
 * 체인을 가까운 것부터 따라가며 naive 와 같은 규칙(최장, 같으면 최근)으로 고른다. 창 밖(i − c > W)에 닿으면 멈춘다.
 * p->max_chain > 0 이면 후보를 그 개수까지만 본다 (속도 ↔ 압축률). */
static size_t lz_find_hash(const lz_hash_t *h, const lz_params *p, const uint8_t *in, size_t n, size_t i,
                           size_t *dist) {
    (void)h; (void)p; (void)in; (void)n; (void)i;
    *dist = 0;
    return 0; /* TODO: head[hash(in+i)] 에서 시작해 prev[c mod W] 로 따라간다. i − c > W 면 멈춤, max_chain 예산,
               * 규칙은 naive 와 같게 (엄격히 길 때만 갱신, maxlen 이면 멈춤) */
}

/* ======================= Part A — 인코더 ======================= */
/* 탐욕 파싱: 위치 i 에서 가장 긴 일치가 min_match 이상이면 match, 아니면 literal. 출력 바이트 수 또는 음수 오류.
 * m == LZ_HASH 이면 h(호출자 소유, 32 KiB)를 쓴다. cap 이 lz_bound(n) 이상이면 절대 LZ_E_CAP 가 안 난다. */
static long lz_encode(const lz_params *p, lz_method m, lz_hash_t *h, const uint8_t *in, size_t n,
                      uint8_t *out, size_t cap) {
    if (!lz_params_ok(p) || n > (size_t)INT32_MAX || (m == LZ_HASH && h == NULL)) return LZ_E_ARG;
    if (cap < LZ_HDR) return LZ_E_CAP;
    out[0] = (uint8_t)p->obits; out[1] = (uint8_t)p->min_match; wr32(out + 2, (uint32_t)n);
    (void)lz_hash_reset; (void)lz_hash_insert; (void)lz_find_naive; (void)lz_find_hash; (void)in;
    return (long)LZ_HDR; /* TODO: 탐욕 루프 — 토큰 8개마다 플래그 바이트 자리를 먼저 잡고, match 2 B (dist−1, len−min) /
                          * literal 1 B. 해시 모드면 시작 전 reset, 덮은 위치 i..i+L−1 전부 insert */
}

/* ======================= Part A — 평면 디코더 ======================= */
/* 출력 버퍼 전체가 곧 창이다 (디코더 RAM = 출력 버퍼). 손상 입력에도 in[0..n) 밖을 읽지 않고 out[0..cap) 밖에
 * 쓰지 않는다. 반환 = 원본 길이 또는 음수 오류 (TRUNC/HDR/CAP/DIST/OVERRUN/TRAIL). */
static long lz_decode(const uint8_t *in, size_t n, uint8_t *out, size_t cap) {
    lz_params p; size_t total;
    int r = lz_read_header(in, n, &p, &total);
    if (r) return r;
    if (total > cap) return LZ_E_CAP;
    (void)out;
    return LZ_E_TRUNC; /* TODO: 플래그 바이트 → 토큰 8개. 읽기 전마다 남은 입력 확인(TRUNC), match 는 dist ≤ op(DIST),
                        * len ≤ total − op(OVERRUN) 확인 후 out[op] = out[op − dist] 를 바이트 단위로. 끝나면 ip == n(TRAIL) */
}

/* ======================= Part C — 스트리밍 디코더 ======================= */
/* UART/BLE/OTA 로 압축 스트림이 한 바이트씩 들어오고, 풀린 바이트는 put() 으로 바로 나간다 (플래시 페이지 버퍼 등).
 * RAM = sizeof(lzs_t) + 링 W 바이트. 링은 지난 W 바이트를 기억하는 창이다 — 출력 전체를 RAM 에 둘 필요가 없다. */
enum { S_HDR, S_FLAG, S_TOK, S_M2, S_DONE, S_ERR };
typedef struct {
    uint8_t  *ring;                    /* 호출자가 준 창 버퍼 (ring_size B, 2의 거듭제곱) */
    uint32_t  ring_size, limit;        /* limit = 받아들일 최대 원본 길이 (예: 플래시 파티션 크기) */
    void    (*put)(void *ctx, uint8_t b);
    void     *ctx;
    uint32_t  total, produced, mask;
    uint8_t   hdr[LZ_HDR];
    uint8_t   hdr_got, state, flags, nbit, b0, lbits, minm;
    int       err;
} lzs_t;

static void lzs_init(lzs_t *s, uint8_t *ring, uint32_t ring_size, uint32_t limit,
                     void (*put)(void *, uint8_t), void *ctx) {
    memset(s, 0, sizeof *s);
    s->ring = ring; s->ring_size = ring_size; s->limit = limit; s->put = put; s->ctx = ctx;
    s->state = S_HDR;
}
static int lzs_fail(lzs_t *s, int e) { s->state = S_ERR; s->err = e; return e; }
static void lzs_emit(lzs_t *s, uint8_t c) {            /* 링에 기억하고 내보낸다 */
    s->ring[s->produced & s->mask] = c;
    s->put(s->ctx, c);
    s->produced++;
}
static int lzs_header(lzs_t *s, uint8_t b) {
    s->hdr[s->hdr_got++] = b;
    if (s->hdr_got < LZ_HDR) return LZ_OK;
    lz_params p; size_t total;
    int r = lz_read_header(s->hdr, LZ_HDR, &p, &total);
    if (r) return lzs_fail(s, r);
    if (lz_window(&p) > s->ring_size) return lzs_fail(s, LZ_E_HDR);   /* 이 장치의 링보다 큰 창 → 못 푼다 */
    if (total > s->limit) return lzs_fail(s, LZ_E_CAP);
    s->total = (uint32_t)total; s->mask = (uint32_t)lz_window(&p) - 1u;
    s->lbits = (uint8_t)lz_lbits(&p); s->minm = (uint8_t)p.min_match;
    s->state = total == 0 ? S_DONE : S_FLAG;
    return LZ_OK;
}
/* 바이트 하나를 먹는다. LZ_OK 또는 음수 오류(이후 계속 같은 오류 — sticky). O(1) 상태 + match 마다 len 번 put. */
static int lzs_push(lzs_t *s, uint8_t b) {
    if (s->state == S_ERR) return s->err;
    if (s->state == S_HDR) return lzs_header(s, b);
    (void)lzs_emit;
    return lzs_fail(s, LZ_E_TRUNC); /* TODO: S_FLAG → 플래그 저장 / S_TOK → literal 은 lzs_emit, match 는 첫 바이트 b0 저장 →
                                     * S_M2 → dist ≤ produced, len ≤ total − produced 확인 후 링에서 바이트 단위 복사 /
                                     * 토큰이 끝나면 flags>>=1, nbit++, 상태 = DONE · FLAG · TOK / S_DONE 뒤 바이트 → TRAIL */
}
static long lzs_finish(const lzs_t *s) {
    if (s->state == S_ERR) return s->err;
    if (s->state != S_DONE) return LZ_E_TRUNC;
    return (long)s->produced;
}

/* ============================== 테스트 하네스 ============================== */
#ifndef ALGO_A05_NO_MAIN
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

#define MAXN (128u * 1024u)
static uint8_t g_in[MAXN], g_enc[LZ_HDR + MAXN + MAXN / 8 + 8], g_enc2[LZ_HDR + MAXN + MAXN / 8 + 8];
static uint8_t g_dec[MAXN], g_dec2[MAXN], g_ring[LZ_WMAX];
static lz_hash_t g_hash;

/* 토큰 통계 (유효한 스트림만 넣는다) */
typedef struct { size_t lit, match, match_bytes, dist_eq_w, max_dist; } lz_stats;
static void lz_scan(const uint8_t *in, size_t n, lz_stats *st) {
    memset(st, 0, sizeof *st);
    lz_params p; size_t total;
    if (lz_read_header(in, n, &p, &total)) return;
    size_t ip = LZ_HDR, op = 0; unsigned flags = 0, nbit = 8, lb = lz_lbits(&p);
    while (op < total && ip < n) {
        if (nbit == 8) { flags = in[ip++]; nbit = 0; continue; }
        if (flags >> nbit & 1u) {
            if (ip + 2 > n) return;
            unsigned w = (unsigned)in[ip] | (unsigned)in[ip + 1] << 8; ip += 2;
            size_t d = (w >> lb) + 1, L = (w & ((1u << lb) - 1)) + p.min_match;
            st->match++; st->match_bytes += L; op += L;
            if (d == lz_window(&p)) st->dist_eq_w++;
            if (d > st->max_dist) st->max_dist = d;
        } else { st->lit++; ip++; op++; }
        nbit++;
    }
}

/* 스트리밍 디코더를 메모리 싱크로 — 덩어리 크기를 무작위로 (UART/OTA 패킷 흉내) */
typedef struct { uint8_t *buf; size_t cap, len; int overflow; } memsink_t;
static void memsink_put(void *ctx, uint8_t b) {
    memsink_t *m = ctx;
    if (m->len < m->cap) m->buf[m->len++] = b; else m->overflow = 1;
}
static long stream_decode(const uint8_t *in, size_t n, uint8_t *out, size_t cap, uint32_t *rng, int *overflow) {
    lzs_t s; memsink_t ms = { out, cap, 0, 0 };
    lzs_init(&s, g_ring, LZ_WMAX, (uint32_t)(cap > UINT32_MAX ? UINT32_MAX : cap), memsink_put, &ms);
    size_t i = 0;
    while (i < n) {
        size_t chunk = 1 + xs(rng) % 64;
        for (size_t k = 0; k < chunk && i < n; k++, i++) if (lzs_push(&s, in[i]) < 0) break;
        if (s.state == S_ERR) break;
    }
    *overflow = ms.overflow;
    return lzs_finish(&s);
}

/* 왕복 한 건: naive 인코딩 == 해시(무제한) 인코딩 (바이트까지), ≤ lz_bound, 평면·스트리밍 디코드 == 원본,
 * 해시(체인 4) 인코딩도 왕복. 실패 종류를 비트로 반환 (0 = 모두 OK). */
enum { RT_ENC = 1, RT_BOUND = 2, RT_SAME = 4, RT_FLAT = 8, RT_STREAM = 16, RT_SHORT = 32 };
static uint32_t g_rt_rng = 0xC0FFEEu;
static int roundtrip(unsigned obits, unsigned minm, const uint8_t *in, size_t n, long *csize) {
    int bad = 0, ovf = 0;
    lz_params p = { obits, minm, 0 };
    long a = lz_encode(&p, LZ_NAIVE, NULL, in, n, g_enc, lz_bound(n));
    long b = lz_encode(&p, LZ_HASH, &g_hash, in, n, g_enc2, lz_bound(n));
    if (csize) *csize = a;
    if (a < 0 || b < 0) return RT_ENC;
    if ((size_t)a > lz_bound(n)) bad |= RT_BOUND;
    if (a != b || memcmp(g_enc, g_enc2, (size_t)a) != 0) bad |= RT_SAME;
    long d = lz_decode(g_enc, (size_t)a, g_dec, n);
    if (d != (long)n || (n && memcmp(g_dec, in, n) != 0)) bad |= RT_FLAT;
    long e = stream_decode(g_enc, (size_t)a, g_dec2, n, &g_rt_rng, &ovf);
    if (e != (long)n || ovf || (n && memcmp(g_dec2, in, n) != 0)) bad |= RT_STREAM;
    lz_params q = { obits, minm, 4 };
    long c = lz_encode(&q, LZ_HASH, &g_hash, in, n, g_enc2, lz_bound(n));
    if (c < 0 || lz_decode(g_enc2, (size_t)c, g_dec, n) != (long)n || (n && memcmp(g_dec, in, n) != 0)) bad |= RT_SHORT;
    return bad;
}

static void fill_random(uint8_t *b, size_t n, unsigned alpha, uint32_t *rng) {
    for (size_t i = 0; i < n; i++) b[i] = (uint8_t)(alpha >= 256 ? xs(rng) : xs(rng) % alpha);
}

/* 가짜 Cortex-M 펌웨어 이미지: 벡터 표(같은 핸들러 주소 반복) + "코드"(스니펫 32개를 무작위로 잇고 가끔 즉시값
 * 1 B 를 바꿈 — 컴파일된 코드의 반복성 흉내) + 문자열 표 + 지워진 플래시 0xFF 패딩. 실제 바이너리는 demo_ratio.c */
static void make_fw(uint8_t *b, size_t n, uint32_t *rng) {
    static const char *words[] = { "imu", "baro", "gps", "init", "failed", "timeout", "ok", "spi", "i2c", "dma",
        "error: ", "warn: ", "%d", "%s\n", "flash", "erase", "write", "crc mismatch", "boot", "update" };
    uint8_t snip[32][16]; size_t slen[32];
    size_t o = 0;
    for (int s = 0; s < 32; s++) { slen[s] = 4 + 2 * (xs(rng) % 7); for (int k = 0; k < 16; k++) snip[s][k] = (uint8_t)xs(rng); }
    for (int k = 0; k < 64 && o + 4 <= n; k++, o += 4)
        wr32(b + o, k == 0 ? 0x20020000u : (k < 16 && k % 3) ? 0x08000201u + 8u * (uint32_t)k : 0x080001F1u);
    size_t code_end = n * 6 / 10, str_end = n * 8 / 10;
    while (o < code_end) {
        unsigned s = xs(rng) % 32;
        for (size_t k = 0; k < slen[s] && o < code_end; k++) b[o++] = snip[s][k];
        if (xs(rng) % 4 == 0) b[o - 1] = (uint8_t)xs(rng);
    }
    while (o < str_end) {
        const char *w = words[xs(rng) % 20];
        for (size_t k = 0; w[k] && o < str_end; k++) b[o++] = (uint8_t)w[k];
        if (o < str_end) b[o++] = xs(rng) % 3 ? ' ' : 0;
    }
    while (o < n) b[o++] = 0xFF;
}

/* 합성 센서 로그 텍스트 (UART 로그 흉내): 시각 + 서브시스템 + 값. 정확히 n 바이트. */
static void make_log(uint8_t *b, size_t n, uint32_t *rng) {
    char line[96]; size_t o = 0; uint32_t t = 1000;
    while (o < n) {
        t += 7 + xs(rng) % 6;
        int len, k = (int)(xs(rng) % 10);
        if (k < 6) len = snprintf(line, sizeof line, "[%07u.%03u] imu: ax=%d ay=%d az=%d\n", (unsigned)(t / 1000),
                                  (unsigned)(t % 1000), (int)(xs(rng) % 41) - 20, (int)(xs(rng) % 41) - 20, 990 + (int)(xs(rng) % 21));
        else if (k < 8) len = snprintf(line, sizeof line, "[%07u.%03u] bat: v=%umV t=%dC\n", (unsigned)(t / 1000),
                                       (unsigned)(t % 1000), 3900u + xs(rng) % 40, 23 + (int)(xs(rng) % 3));
        else if (k < 9) len = snprintf(line, sizeof line, "[%07u.%03u] gps: fix=%u sats=%u\n", (unsigned)(t / 1000),
                                       (unsigned)(t % 1000), 3u, 7u + xs(rng) % 5);
        else len = snprintf(line, sizeof line, "[%07u.%03u] W: spi1 timeout, retry %u\n", (unsigned)(t / 1000),
                            (unsigned)(t % 1000), 1u + xs(rng) % 3);
        for (int k2 = 0; k2 < len && o < n; k2++) b[o++] = (uint8_t)line[k2];
    }
}

/* Part C: 256 B 페이지 단위로만 쓸 수 있는 플래시 흉내. 지워진(0xFF) 곳에만, 주소 순서대로 써야 한다. */
#define PAGE 256u
typedef struct { uint8_t *flash; size_t size, addr, fill; uint8_t page[PAGE]; int pages, bad; } flash_t;
static void flash_program(flash_t *f) {
    if (f->fill == 0) return;
    if (f->addr + f->fill > f->size) { f->bad = 1; f->fill = 0; return; }
    for (size_t k = 0; k < f->fill; k++) if (f->flash[f->addr + k] != 0xFF) f->bad = 1;   /* 안 지운 곳에 쓰기 */
    memcpy(f->flash + f->addr, f->page, f->fill);
    f->addr += f->fill; f->fill = 0; f->pages++;
}
static void flash_put(void *ctx, uint8_t b) {
    flash_t *f = ctx;
    f->page[f->fill++] = b;
    if (f->fill == PAGE) flash_program(f);
}

/* 손상 입력: 평면·스트리밍 디코더를 정확한 크기의 힙 버퍼로 돌리고 결과(코드·출력)가 같은지 */
static int g_hist[9], g_silent;                         /* g_silent = "성공" 했는데 원본과 다른 출력 */
static int fuzz_one(const uint8_t *src, size_t n, size_t cap, const uint8_t *orig, size_t on, uint32_t *rng) {
    uint8_t *in = malloc(n ? n : 1), *o1 = malloc(cap ? cap : 1), *o2 = malloc(cap ? cap : 1);
    if (!in || !o1 || !o2) { free(in); free(o1); free(o2); return 0; }
    memcpy(in, src, n);
    int ovf = 0;
    long a = lz_decode(in, n, o1, cap);
    long b = stream_decode(in, n, o2, cap, rng, &ovf);
    int ok = a == b && !ovf && (a <= 0 || memcmp(o1, o2, (size_t)a) == 0) && a <= (long)cap;
    g_hist[a > 0 ? 0 : a == 0 ? 8 : -a]++;
    if (a >= 0 && (a != (long)on || memcmp(o1, orig, on) != 0)) g_silent++;
    free(in); free(o1); free(o2);
    return ok;
}

int main(void) {
    printf("== Part A: 형식 고정 + 왕복 ==\n");
    {   /* 손 계산 골든 벡터: "abcabcabcabc", W=256, min 3 → literal a b c + match(dist 3, len 9) */
        static const uint8_t want[12] = { 0x08, 0x03, 0x0C, 0, 0, 0, 0x08, 'a', 'b', 'c', 0x06, 0x02 };
        lz_params p = { 8, 3, 0 };
        long r = lz_encode(&p, LZ_NAIVE, NULL, (const uint8_t *)"abcabcabcabc", 12, g_enc, sizeof g_enc);
        CHECK(r == 12 && memcmp(g_enc, want, 12) == 0);
        long d = lz_decode(want, 12, g_dec, 12);
        CHECK(d == 12 && memcmp(g_dec, "abcabcabcabc", 12) == 0);       /* 겹치는 복사: dist 3 < len 9 */
        uint8_t mm[12] = { 'a', 'b', 'c' };
        memmove(mm + 3, mm, 9);                                          /* memmove 는 "복사 전 원본" 을 옮긴다 */
        CHECK(memcmp(mm, "abcabcabcabc", 12) != 0 && memcmp(mm, "abcabc\0\0\0\0\0\0", 12) == 0);
    }
    {   /* 이름 붙은 경계 사례 × 파라미터 15 조합 (obits 8..12 × min 2..4) */
        int bad_named = 0, bad_period = 0, bad_wedge = 0, cases = 0;
        uint32_t rng = 12345u;
        for (unsigned ob = 8; ob <= 12; ob++)
            for (unsigned mm = 2; mm <= 4; mm++) {
                size_t W = (size_t)1 << ob;
                bad_named |= roundtrip(ob, mm, g_in, 0, NULL); cases++;            /* 빈 입력 */
                g_in[0] = 0x5A; bad_named |= roundtrip(ob, mm, g_in, 1, NULL); cases++;
                g_in[1] = 0x5A; bad_named |= roundtrip(ob, mm, g_in, 2, NULL); cases++;
                memset(g_in, 'A', 1000); bad_named |= roundtrip(ob, mm, g_in, 1000, NULL); cases++;   /* 전부 같음 */
                fill_random(g_in, 3000, 256, &rng); bad_named |= roundtrip(ob, mm, g_in, 3000, NULL); cases++;
                for (size_t k = 1; k <= 16; k++) {                                 /* 주기 k 데이터 */
                    fill_random(g_in, k, 256, &rng);
                    for (size_t i = k; i < 777; i++) g_in[i] = g_in[i - k];
                    bad_period |= roundtrip(ob, mm, g_in, 777, NULL); cases++;
                }
                size_t sizes[] = { W - 1, W, W + 1, 2 * W - 1, 2 * W, 2 * W + 3 };  /* 창 경계 크기 */
                for (size_t s = 0; s < 6; s++) {
                    fill_random(g_in, sizes[s], 4, &rng);
                    bad_wedge |= roundtrip(ob, mm, g_in, sizes[s], NULL); cases++;
                }
            }
        printf("named/period/window-edge: %d cases, fail bits %d/%d/%d\n", cases, bad_named, bad_period, bad_wedge);
        CHECK(bad_named == 0);
        CHECK(bad_period == 0);
        CHECK(bad_wedge == 0);
    }
    {   /* 전수: {a,b} 위의 길이 0..12 문자열 전부 (8191개) × (W=256, min 2) (W=256, min 3) */
        int bad = 0; long cnt = 0;
        for (unsigned mm = 2; mm <= 3; mm++)
            for (size_t len = 0; len <= 12; len++)
                for (uint32_t bits = 0; bits < (1u << len); bits++) {
                    for (size_t i = 0; i < len; i++) g_in[i] = (uint8_t)(bits >> i & 1u ? 'b' : 'a');
                    bad |= roundtrip(8, mm, g_in, len, NULL); cnt++;
                }
        printf("exhaustive binary strings: %ld round trips, fail bits %d\n", cnt, bad);
        CHECK(bad == 0);
    }
    {   /* 무작위: 길이 0..1500, 알파벳 1/2/4/16/256, 파라미터 무작위 */
        static const unsigned alphas[5] = { 1, 2, 4, 16, 256 };
        uint32_t rng = 0xA05A05u; int bad = 0;
        for (int t = 0; t < 400; t++) {
            size_t n = xs(&rng) % 1501;
            fill_random(g_in, n, alphas[xs(&rng) % 5], &rng);
            bad |= roundtrip(8 + xs(&rng) % 5, 2 + xs(&rng) % 3, g_in, n, NULL);
        }
        printf("random: 400 inputs, fail bits %d\n", bad);
        CHECK((bad & RT_SAME) == 0);                     /* naive == 해시(무제한) 바이트까지 */
        CHECK((bad & (RT_ENC | RT_BOUND | RT_FLAT)) == 0);
        CHECK((bad & RT_STREAM) == 0);
        CHECK((bad & RT_SHORT) == 0);                    /* 체인 4 로 잘라도 풀린다 */
    }

    printf("== Part B: 창 경계, 최악 크기, 손상 입력 ==\n");
    {   /* 주기 W 데이터는 거리 정확히 W 의 match 로, 주기 W+1 은 창 밖이라 literal 로 */
        uint32_t rng = 777u;
        for (unsigned ob = 8; ob <= 12; ob += 4) {
            size_t W = (size_t)1 << ob; long cw, cw1; lz_stats sw, sw1;
            fill_random(g_in, W + 1, 256, &rng);
            memcpy(g_in + W, g_in, W);                                     /* R R  (주기 W) */
            CHECK(roundtrip(ob, 3, g_in, 2 * W, &cw) == 0);
            lz_scan(g_enc, (size_t)cw, &sw);
            fill_random(g_in, W + 1, 256, &rng);
            memcpy(g_in + W + 1, g_in, W + 1);                             /* R' R' (주기 W+1) */
            CHECK(roundtrip(ob, 3, g_in, 2 * W + 2, &cw1) == 0);
            lz_scan(g_enc, (size_t)cw1, &sw1);
            printf("W=%4zu: period W   -> %5ld B (%zu matches, %zu with dist==W)\n", W, cw, sw.match, sw.dist_eq_w);
            printf("        period W+1 -> %5ld B (%zu matches, max dist %zu), bound %zu\n", cw1, sw1.match, sw1.max_dist,
                   lz_bound(2 * W + 2));
            CHECK(sw.dist_eq_w > 0 && (size_t)cw < W + W / 2);
            CHECK((size_t)cw1 > 2 * W);
        }
    }
    {   /* 비압축 데이터: 출력 ≤ lz_bound 이고 cap = lz_bound − 1 이면 (literal 뿐일 때) 실패해야 */
        uint32_t rng = 4242u; long c2, c3;
        fill_random(g_in, 10000, 256, &rng);
        CHECK(roundtrip(12, 3, g_in, 10000, &c3) == 0 && (size_t)c3 <= lz_bound(10000));
        CHECK(roundtrip(12, 2, g_in, 10000, &c2) == 0);
        lz_stats s3; lz_scan(g_enc, (size_t)c2, &s3);
        printf("random 10000 B: min3 -> %ld B, min2 -> %ld B (%zu two-byte matches), bound %zu\n", c3, c2, s3.match,
               lz_bound(10000));
        lz_params p = { 8, 3, 0 };
        fill_random(g_in, 64, 256, &rng);
        CHECK(lz_encode(&p, LZ_NAIVE, NULL, g_in, 64, g_enc, lz_bound(64)) == (long)lz_bound(64));
        CHECK(lz_encode(&p, LZ_NAIVE, NULL, g_in, 64, g_enc, lz_bound(64) - 1) == LZ_E_CAP);
    }
    {   /* 손으로 만든 잘못된 스트림 → 정해진 오류 코드 (평면 == 스트리밍) */
        struct { const char *name; uint8_t b[16]; size_t n, cap; long want; } bad[] = {
            { "short header",    { 8, 3, 1, 0, 0 },                            5, 16, LZ_E_TRUNC },
            { "obits 7",         { 7, 3, 1, 0, 0, 0, 0, 'x' },                 8, 16, LZ_E_HDR },
            { "obits 13",        { 13, 3, 1, 0, 0, 0, 0, 'x' },                8, 16, LZ_E_HDR },
            { "min 1",           { 8, 1, 1, 0, 0, 0, 0, 'x' },                 8, 16, LZ_E_HDR },
            { "len > cap",       { 8, 3, 17, 0, 0, 0, 0, 'x' },                8, 16, LZ_E_CAP },
            { "dist > produced", { 8, 3, 5, 0, 0, 0, 0x01, 0x00, 0x00 },       9, 16, LZ_E_DIST },
            { "len > total",     { 8, 3, 4, 0, 0, 0, 0x02, 'a', 0x05, 0x00 }, 10, 16, LZ_E_OVERRUN },
            { "cut in match",    { 8, 3, 4, 0, 0, 0, 0x02, 'a', 0x00 },        9, 16, LZ_E_TRUNC },
            { "no flag byte",    { 8, 3, 1, 0, 0, 0 },                         6, 16, LZ_E_TRUNC },
            { "trailing byte",   { 8, 3, 1, 0, 0, 0, 0x00, 'a', 0x00 },        9, 16, LZ_E_TRAIL },
            { "empty + trailing",{ 8, 3, 0, 0, 0, 0, 0x00 },                   7, 16, LZ_E_TRAIL },
        };
        int okc = 0, nb = (int)(sizeof bad / sizeof bad[0]);
        uint32_t rng = 9u;
        for (int k = 0; k < nb; k++) {
            int ovf = 0;
            long a = lz_decode(bad[k].b, bad[k].n, g_dec, bad[k].cap);
            long b = stream_decode(bad[k].b, bad[k].n, g_dec2, bad[k].cap, &rng, &ovf);
            if (a == bad[k].want && b == bad[k].want) okc++;
            else printf("  %-16s flat=%ld stream=%ld want=%ld\n", bad[k].name, a, b, bad[k].want);
        }
        printf("hand-made malformed streams: %d/%d give the expected error in both decoders\n", okc, nb);
        CHECK(okc == nb);
    }
    {   /* 결정적 퍼즈: 정상 스트림을 비트 뒤집기 / 자르기 / 본문 무작위화 / 길이 필드 변조 → 두 디코더 일치 + ASan */
        uint32_t rng = 0xF00Du; int agree = 0, iters = 0;
        static uint8_t base[LZ_HDR + 4096 + 600];
        size_t blen = 0, on = 0;
        for (int t = 0; t < 30000; t++) {
            if (t % 500 == 0) {                              /* 기준 스트림 갈아 끼우기 (60개) */
                size_t n = 200 + xs(&rng) % 3000; on = n;
                if (t % 1000 == 0) make_log(g_in, n, &rng); else fill_random(g_in, n, 1 + xs(&rng) % 8, &rng);
                lz_params p = { 8 + xs(&rng) % 5, 2 + xs(&rng) % 3, 0 };
                blen = (size_t)lz_encode(&p, LZ_HASH, &g_hash, g_in, n, base, sizeof base);   /* ≤ lz_bound → 성공 */
            }
            size_t n = blen;
            memcpy(g_dec2, base, blen);                       /* g_dec2 = 변조할 작업 사본 */
            switch (xs(&rng) % 4) {
            case 0: for (int k = 1 + (int)(xs(&rng) % 4); k > 0; k--) g_dec2[xs(&rng) % blen] ^= (uint8_t)(1u << (xs(&rng) % 8)); break;
            case 1: n = xs(&rng) % blen; break;
            case 2: for (size_t k = LZ_HDR; k < blen; k++) if (xs(&rng) % 8 == 0) g_dec2[k] = (uint8_t)xs(&rng); break;
            default: wr32(g_dec2 + 2, rd32(g_dec2 + 2) + (xs(&rng) % 64) - 32); break;
            }
            agree += fuzz_one(g_dec2, n, 4096, g_in, on, &rng); iters++;
        }
        printf("fuzz: %d mutated streams, decoders agree on %d; results ok=%d trunc=%d hdr=%d cap=%d dist=%d overrun=%d trail=%d\n",
               iters, agree, g_hist[0] + g_hist[8], g_hist[3], g_hist[2], g_hist[1], g_hist[4], g_hist[5], g_hist[6]);
        printf("      accepted but output != original: %d  (LZ 는 무결성 검사가 없다 -> CRC 필요)\n", g_silent);
        CHECK(agree == iters);
        CHECK(g_hist[4] > 0 && g_hist[5] > 0 && g_hist[3] > 0 && g_hist[6] > 0);   /* 오류 경로가 실제로 다 지나갔다 */
    }

    printf("== Part C: OTA 스트리밍 디코드 → 플래시, 로그 압축 ==\n");
    {
        static uint8_t flash[MAXN];
        uint32_t rng = 0x0FA7u;
        size_t n = 96 * 1024;
        make_fw(g_in, n, &rng);
        lz_params p = { 10, 3, 0 };                                  /* W = 1 KiB */
        long c = lz_encode(&p, LZ_HASH, &g_hash, g_in, n, g_enc, sizeof g_enc);
        CHECK(c > 0 && (size_t)c < n);
        memset(flash, 0xFF, sizeof flash);
        flash_t f = { flash, sizeof flash, 0, 0, { 0 }, 0, 0 };
        lzs_t s; uint8_t ring[1024];
        lzs_init(&s, ring, sizeof ring, (uint32_t)sizeof flash, flash_put, &f);
        size_t i = 0; int rc = 0;
        while (i < (size_t)c && rc == 0) {                           /* 1..300 B 패킷으로 도착 */
            size_t pk = 1 + xs(&rng) % 300;
            for (size_t k = 0; k < pk && i < (size_t)c && rc == 0; k++) rc = lzs_push(&s, g_enc[i++]);
        }
        long got = lzs_finish(&s);
        flash_program(&f);                                           /* 마지막 덜 찬 페이지 */
        printf("fw image %zu B -> %ld B (%.1f%%), streamed into %d flash pages, rc=%ld\n", n, c, 100.0 * (double)c / (double)n,
               f.pages, got);
        CHECK(got == (long)n && f.bad == 0 && f.pages == (int)((n + PAGE - 1) / PAGE) && memcmp(flash, g_in, n) == 0);
        printf("decoder RAM: sizeof(lzs_t)=%zu + ring W=%zu + page buffer %u = %zu B (host, 64-bit pointers)\n",
               sizeof(lzs_t), sizeof ring, PAGE, sizeof(lzs_t) + sizeof ring + PAGE);
        CHECK(sizeof(lzs_t) <= 64);
        /* 링이 창보다 작으면 거부, 길이가 파티션보다 크면 거부 */
        lz_params p4 = { 12, 3, 0 };
        long c4 = lz_encode(&p4, LZ_HASH, &g_hash, g_in, 5000, g_enc2, sizeof g_enc2);
        lzs_init(&s, ring, sizeof ring, (uint32_t)sizeof flash, flash_put, &f);
        for (size_t k = 0; k < (size_t)c4; k++) lzs_push(&s, g_enc2[k]);
        CHECK(lzs_finish(&s) == LZ_E_HDR);
        lzs_init(&s, ring, sizeof ring, 4999u, flash_put, &f);
        for (size_t k = 0; k < LZ_HDR; k++) lzs_push(&s, g_enc[k]);
        CHECK(lzs_finish(&s) == LZ_E_CAP);
    }
    {   /* 합성 UART 로그: 창 크기별 압축률 (max_len 은 W 가 커질수록 줄어든다: 258 → 18) */
        uint32_t rng = 0x106u; size_t n = 64 * 1024; long best = LONG_MAX, worst = 0;
        make_log(g_in, n, &rng);
        printf("log %zu B, min 3:", n);
        for (unsigned ob = 8; ob <= 12; ob++) {
            long c;
            CHECK(roundtrip(ob, 3, g_in, n, &c) == 0);
            printf("  W=%u %.1f%%", 1u << ob, 100.0 * (double)c / (double)n);
            if (c < best) best = c;
            if (c > worst) worst = c;
        }
        printf("\n");
        CHECK(best < (long)(n / 2));
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* ALGO_A05_NO_MAIN */
