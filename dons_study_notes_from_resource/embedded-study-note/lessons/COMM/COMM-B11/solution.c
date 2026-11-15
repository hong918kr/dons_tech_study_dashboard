/* COMM-B11 SLIP 인코더/디코더 (RFC 1055)
 *
 * Part A: slip_encode() — 바이트 스터핑(END→ESC ESC_END, ESC→ESC ESC_ESC), leading END 옵션, 출력 용량 검사
 *         slip_dec_push() — 바이트 하나씩 먹는 스트리밍 디코더 (ISR 에서 불러도 되는 O(1), 정적 버퍼)
 * Part B: END/ESC 가 섞인 페이로드, 연속 END(빈 프레임은 버림), 잘못된 이스케이프
 *         (LENIENT = RFC 1055 예제 코드처럼 그대로 저장 / STRICT = 프레임 거부), 너무 긴 프레임, 최악 오버헤드 2n+2
 * Part C: SLIP 위에 CRC-16/CCITT-FALSE 를 붙인 링크 계층 — 잡음·스트림 중간 시작·END 유실·비트 오류가 섞인
 *         스트림에서 멀쩡한 프레임은 전부, 손상된 프레임은 하나도 전달되지 않는지 검사
 *
 * 테스트: 1·2바이트 페이로드 전수, xorshift32 고정 시드 랜덤 왕복, 이름 붙인 결함 주입 케이스.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SLIP_END     0xC0u   /* RFC 1055 에는 8진수 0300 */
#define SLIP_ESC     0xDBu   /* 0333 */
#define SLIP_ESC_END 0xDCu   /* 0334 — ESC 뒤에 오면 데이터 0xC0 */
#define SLIP_ESC_ESC 0xDDu   /* 0335 — ESC 뒤에 오면 데이터 0xDB */
#define SLIP_ENC_MAX(n) (2u * (size_t)(n) + 2u)   /* 최악: 모든 바이트가 이스케이프 + END 2개 */

enum { SLIP_OK = 0, SLIP_ERR_OVERFLOW = -3, LINK_ERR_MTU = -4 };
typedef enum { SLIP_MORE = 0, SLIP_FRAME = 1, SLIP_ERR_ESC = -1, SLIP_ERR_LONG = -2 } slip_rc;
typedef enum { SLIP_LENIENT, SLIP_STRICT } slip_policy;
enum { ST_NORMAL, ST_ESC, ST_DISCARD };

typedef struct {
    uint8_t *buf;        /* 호출자가 준 정적 버퍼 */
    size_t cap;          /* 디코딩된 바이트 기준 최대 프레임 길이 */
    size_t len;          /* 지금 모으는 프레임의 길이 */
    size_t frame_len;    /* SLIP_FRAME 을 돌려줄 때 길이. buf[0..frame_len) 은 다음 push 전까지만 유효 */
    uint8_t state;       /* ST_NORMAL / ST_ESC(직전 바이트가 ESC) / ST_DISCARD(END 까지 버림) */
    slip_rc err;         /* ST_DISCARD 일 때 다음 END 에서 보고할 오류 */
    slip_policy policy;
    uint32_t frames, empty, esc_err, long_err;   /* 통계 */
} slip_dec;

static void slip_dec_init(slip_dec *d, uint8_t *buf, size_t cap, slip_policy pol) {
    memset(d, 0, sizeof *d);
    d->buf = buf; d->cap = cap; d->policy = pol;
}

/* ---------------- Part A: 인코더 ---------------- */

/* in[0..n) 을 SLIP 으로 out 에 쓴다. lead_end 면 앞에도 END.
 * 성공: SLIP_OK, *out_len = 쓴 바이트 수. 용량 부족: SLIP_ERR_OVERFLOW, *out_len = 0, out[cap..] 은 절대 안 건드림. */
static int slip_encode(const uint8_t *in, size_t n, uint8_t *out, size_t cap, bool lead_end, size_t *out_len) {
    size_t k = 0;                                   /* 불변식: k <= cap */
    *out_len = 0;
    if (lead_end) {
        if (k == cap) return SLIP_ERR_OVERFLOW;
        out[k++] = SLIP_END;                        /* 수신기에 쌓인 잡음을 먼저 털어낸다 */
    }
    for (size_t i = 0; i < n; i++) {
        uint8_t b = in[i];
        if (b == SLIP_END || b == SLIP_ESC) {
            if (cap - k < 2) return SLIP_ERR_OVERFLOW;
            out[k++] = SLIP_ESC;
            out[k++] = (b == SLIP_END) ? SLIP_ESC_END : SLIP_ESC_ESC;
        } else {
            if (k == cap) return SLIP_ERR_OVERFLOW;
            out[k++] = b;
        }
    }
    if (k == cap) return SLIP_ERR_OVERFLOW;
    out[k++] = SLIP_END;
    *out_len = k;
    return SLIP_OK;
}

/* ---------------- Part A + B: 스트리밍 디코더 ---------------- */

/* 바이트 하나를 먹는다. 반환:
 *   SLIP_MORE     — 아직 프레임 없음 (빈 프레임 END 도 여기: empty++ 하고 무시)
 *   SLIP_FRAME    — 프레임 완성: d->buf[0..d->frame_len)
 *   SLIP_ERR_ESC  — 이 END 로 끝난 프레임에 잘못된 이스케이프가 있었다 (STRICT 만). 프레임은 버림
 *   SLIP_ERR_LONG — 이 END 로 끝난 프레임이 cap 을 넘었다. 프레임은 버림
 * 오류는 END 에서 한 번만 보고한다 → END 하나에 판정 하나. */
static slip_rc slip_dec_push(slip_dec *d, uint8_t b) {
    bool delim = (b == SLIP_END);
    if (d->state == ST_ESC) {                                   /* 직전 바이트가 ESC */
        d->state = ST_NORMAL;
        if (b == SLIP_ESC_END)              { b = SLIP_END; }   /* 데이터 0xC0 (delim 은 원래 b 로 이미 false) */
        else if (b == SLIP_ESC_ESC)         { b = SLIP_ESC; }   /* 데이터 0xDB */
        else if (d->policy == SLIP_LENIENT) { delim = false; }  /* RFC 예제: 그대로 저장 — END 라도 데이터로! */
        else {                                                  /* STRICT: 규칙 위반 → 이 프레임은 버린다 */
            d->state = ST_DISCARD; d->err = SLIP_ERR_ESC;
            if (!delim) return SLIP_MORE;                       /* ESC END 면 아래에서 바로 프레임 종료 */
        }
    } else if (b == SLIP_ESC && d->state == ST_NORMAL) {
        d->state = ST_ESC;
        return SLIP_MORE;
    }
    if (delim) {
        if (d->state == ST_DISCARD) {
            slip_rc e = d->err;
            if (e == SLIP_ERR_ESC) d->esc_err++; else d->long_err++;
            d->state = ST_NORMAL; d->len = 0;
            return e;
        }
        if (d->len == 0) { d->empty++; return SLIP_MORE; }      /* END END: 빈 프레임은 버린다 */
        d->frame_len = d->len; d->len = 0; d->frames++;
        return SLIP_FRAME;
    }
    if (d->state == ST_DISCARD) return SLIP_MORE;               /* END 가 올 때까지 버림 */
    if (d->len == d->cap) { d->state = ST_DISCARD; d->err = SLIP_ERR_LONG; return SLIP_MORE; }
    d->buf[d->len++] = b;
    return SLIP_MORE;
}

/* ---------------- Part C: SLIP + CRC-16 링크 계층 ---------------- */

/* CRC-16/CCITT-FALSE (= CRC-16/IBM-3740): poly 0x1021, init 0xFFFF, 반사 없음, xorout 0. check("123456789") = 0x29B1
 * (비트 단위 구현, 원리는 ALGO-M01 / 테이블은 ALGO-M02) — 주어진 코드 */
static uint16_t crc16_ccitt_false(const uint8_t *p, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)(p[i] << 8);
        for (int k = 0; k < 8; k++) crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
    }
    return crc;
}

#define LINK_MTU 64   /* 페이로드 최대 바이트 (이 레슨의 정의) */

/* 링크 프레임 = SLIP( payload || CRC16 상위 바이트 || CRC16 하위 바이트 ), CRC 는 스터핑 전 payload 에 대해 */
typedef struct {
    slip_dec dec;
    uint8_t buf[LINK_MTU + 2];
    uint32_t ok, crc_err, short_err;   /* esc_err / long_err 는 dec 안에 */
} link_rx;

static void link_rx_init(link_rx *rx, slip_policy pol) {
    memset(rx, 0, sizeof *rx);
    slip_dec_init(&rx->dec, rx->buf, sizeof rx->buf, pol);
}

/* 성공 SLIP_OK. n > LINK_MTU 면 LINK_ERR_MTU, out 용량 부족이면 SLIP_ERR_OVERFLOW (*out_len = 0). */
static int link_send(const uint8_t *pl, size_t n, uint8_t *out, size_t cap, bool lead_end, size_t *out_len) {
    uint8_t tmp[LINK_MTU + 2];
    *out_len = 0;
    if (n > LINK_MTU) return LINK_ERR_MTU;
    memcpy(tmp, pl, n);
    uint16_t c = crc16_ccitt_false(pl, n);
    tmp[n] = (uint8_t)(c >> 8);                     /* CRC 는 big-endian (상위 바이트 먼저) */
    tmp[n + 1] = (uint8_t)c;
    return slip_encode(tmp, n + 2, out, cap, lead_end, out_len);   /* CRC 바이트도 스터핑된다 */
}

/* 수신 바이트 하나 (UART RX ISR 에서 부르는 자리). 좋은 프레임이면 1 과 *pl, *n (다음 호출 전까지 유효). */
static int link_rx_byte(link_rx *rx, uint8_t b, const uint8_t **pl, size_t *n) {
    if (slip_dec_push(&rx->dec, b) != SLIP_FRAME) return 0;    /* 오류 프레임은 dec 통계에 이미 셌다 */
    size_t len = rx->dec.frame_len;
    if (len < 2) { rx->short_err++; return 0; }                /* CRC 도 못 담은 조각 = 잡음 */
    uint16_t want = (uint16_t)(rx->buf[len - 2] << 8 | rx->buf[len - 1]);
    if (crc16_ccitt_false(rx->buf, len - 2) != want) { rx->crc_err++; return 0; }
    rx->ok++;
    *pl = rx->buf; *n = len - 2;
    return 1;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 특수 바이트(C0 DB DC DD)가 1/4 확률로 나오게 치우친 랜덤 바이트 — 이스케이프 경로를 많이 밟게 */
static uint8_t biased_byte(uint32_t *s) {
    static const uint8_t sp[4] = { 0xC0, 0xDB, 0xDC, 0xDD };
    uint32_t r = xs32(s);
    return (r & 3u) == 0 ? sp[(r >> 2) & 3u] : (uint8_t)(r >> 8);
}

/* 디코더에 바이트 열을 먹이고 결과를 기록: 프레임은 frames_out 에 이어 붙이고 길이를 lens 에, 사건은 ev 에 */
typedef struct { int ev[16]; int nev; uint8_t data[256]; size_t lens[16]; int nfr; size_t used; } feed_log;
static void feed(slip_dec *d, const uint8_t *s, size_t n, feed_log *lg) {
    memset(lg, 0, sizeof *lg);
    for (size_t i = 0; i < n; i++) {
        slip_rc r = slip_dec_push(d, s[i]);
        if (r == SLIP_MORE) continue;
        if (lg->nev < 16) lg->ev[lg->nev++] = r;
        if (r == SLIP_FRAME && lg->nfr < 16 && lg->used + d->frame_len <= sizeof lg->data) {
            memcpy(lg->data + lg->used, d->buf, d->frame_len);
            lg->used += d->frame_len; lg->lens[lg->nfr++] = d->frame_len;
        }
    }
}

/* 한 프레임 왕복: 인코딩 → 디코딩 → 정확히 프레임 1개, 내용 같음. n == 0 은 프레임 0개 (SLIP 은 빈 프레임을 못 나른다) */
static bool roundtrip(const uint8_t *in, size_t n, bool lead, slip_policy pol) {
    static uint8_t enc[SLIP_ENC_MAX(1100)], dbuf[1100];
    size_t m;
    if (slip_encode(in, n, enc, sizeof enc, lead, &m) != SLIP_OK) return false;
    slip_dec d; slip_dec_init(&d, dbuf, sizeof dbuf, pol);
    int frames = 0; bool same = true;
    for (size_t i = 0; i < m; i++) {
        slip_rc r = slip_dec_push(&d, enc[i]);
        if (r == SLIP_FRAME) { frames++; same = same && d.frame_len == n && memcmp(d.buf, in, n) == 0; }
        else if (r != SLIP_MORE) return false;
    }
    return n == 0 ? frames == 0 : (frames == 1 && same);
}

static size_t count_special(const uint8_t *p, size_t n) {
    size_t c = 0;
    for (size_t i = 0; i < n; i++) c += (p[i] == SLIP_END || p[i] == SLIP_ESC);
    return c;
}

/* 인코딩 결과의 구조 검사: END 는 맨 앞(lead)·맨 끝에만, ESC 뒤에는 반드시 ESC_END/ESC_ESC */
static bool well_formed(const uint8_t *e, size_t m, bool lead) {
    if (m < 1 || e[m - 1] != SLIP_END) return false;
    if (lead && e[0] != SLIP_END) return false;
    for (size_t i = lead ? 1 : 0; i + 1 < m; i++) {
        if (e[i] == SLIP_END) return false;
        if (e[i] == SLIP_ESC) { if (e[i + 1] != SLIP_ESC_END && e[i + 1] != SLIP_ESC_ESC) return false; i++; }
    }
    return true;
}

/* ----- Part C 스트림 실험 도구 ----- */
#define NFR 400
static uint8_t g_pl[NFR][LINK_MTU];
static size_t g_pn[NFR];
static uint8_t g_stream[NFR * (SLIP_ENC_MAX(LINK_MTU + 2) + 16) + 256];

/* 스트림을 링크 수신기에 먹이고, 전달된 프레임이 want[] 에 표시된 프레임과 순서·내용까지 같은지 */
static bool deliver_matches(link_rx *rx, const uint8_t *s, size_t n, const bool *want, int *delivered) {
    int next = 0; bool ok = true; *delivered = 0;
    for (size_t i = 0; i < n; i++) {
        const uint8_t *pl; size_t pn;
        if (!link_rx_byte(rx, s[i], &pl, &pn)) continue;
        (*delivered)++;
        while (next < NFR && !want[next]) next++;
        if (next == NFR || pn != g_pn[next] || memcmp(pl, g_pl[next], pn) != 0) ok = false;
        next++;
    }
    while (next < NFR && !want[next]) next++;
    return ok && next == NFR;
}

int main(void) {
    uint32_t seed = 0xC0DBDC0Du;
    uint8_t enc[SLIP_ENC_MAX(1100)], in[1100], dbuf[1100];
    size_t m;
    slip_dec d;
    feed_log lg;

    /* ===== Part A: 알려진 벡터 ===== */
    static const uint8_t p1[4] = { 0x01, 0xC0, 0xDB, 0x02 };
    static const uint8_t e1[8] = { 0xC0, 0x01, 0xDB, 0xDC, 0xDB, 0xDD, 0x02, 0xC0 };
    CHECK(slip_encode(p1, 4, enc, sizeof enc, true, &m) == SLIP_OK && m == 8 && memcmp(enc, e1, 8) == 0);
    CHECK(slip_encode(p1, 4, enc, sizeof enc, false, &m) == SLIP_OK && m == 7 && memcmp(enc, e1 + 1, 7) == 0);
    static const uint8_t e2[7] = { 0xC0, 'h', 'e', 'l', 'l', 'o', 0xC0 };
    CHECK(slip_encode((const uint8_t *)"hello", 5, enc, sizeof enc, true, &m) == SLIP_OK && m == 7 && memcmp(enc, e2, 7) == 0);
    CHECK(slip_encode(p1, 0, enc, sizeof enc, true, &m) == SLIP_OK && m == 2 && enc[0] == 0xC0 && enc[1] == 0xC0);
    CHECK(slip_encode(p1, 0, enc, sizeof enc, false, &m) == SLIP_OK && m == 1 && enc[0] == 0xC0);
    slip_dec_init(&d, dbuf, 64, SLIP_STRICT);
    feed(&d, e1, 8, &lg);
    CHECK(lg.nev == 1 && lg.ev[0] == SLIP_FRAME && lg.lens[0] == 4 && memcmp(lg.data, p1, 4) == 0);
    CHECK(d.frames == 1 && d.empty == 1 && d.esc_err == 0 && d.long_err == 0);

    /* 전수: 1바이트 페이로드 256개 × lead 2 × 정책 2, 2바이트 65536개 × lead 2 */
    int bad = 0;
    for (int v = 0; v < 256; v++)
        for (int lead = 0; lead < 2; lead++)
            for (int pol = 0; pol < 2; pol++) {
                in[0] = (uint8_t)v;
                if (!roundtrip(in, 1, lead, pol ? SLIP_STRICT : SLIP_LENIENT)) bad++;
            }
    CHECK(bad == 0);
    bad = 0;
    for (int v = 0; v < 65536; v++)
        for (int lead = 0; lead < 2; lead++) {
            in[0] = (uint8_t)(v >> 8); in[1] = (uint8_t)v;
            if (!roundtrip(in, 2, lead, SLIP_STRICT)) bad++;
            if (slip_encode(in, 2, enc, sizeof enc, lead, &m) != SLIP_OK || m != 2 + count_special(in, 2) + 1 + (size_t)lead) bad++;
        }
    printf("exhaustive 1-byte x4 configs + 2-byte x2 configs: bad=%d\n", bad);
    CHECK(bad == 0);

    /* 랜덤 왕복 5000개 (길이 0..300, 특수 바이트 1/4) + 길이 공식 + 구조 검사 */
    int bad_rt = 0, bad_len = 0, bad_form = 0;
    for (int t = 0; t < 5000; t++) {
        size_t n = xs32(&seed) % 301u;
        bool lead = t & 1;
        for (size_t i = 0; i < n; i++) in[i] = biased_byte(&seed);
        if (!roundtrip(in, n, lead, (t & 2) ? SLIP_STRICT : SLIP_LENIENT)) bad_rt++;
        if (slip_encode(in, n, enc, sizeof enc, lead, &m) != SLIP_OK || m != n + count_special(in, n) + 1 + lead) bad_len++;
        else if (!well_formed(enc, m, lead)) bad_form++;
    }
    printf("random 5000 frames: roundtrip bad=%d, length-formula bad=%d, structure bad=%d\n", bad_rt, bad_len, bad_form);
    CHECK(bad_rt == 0);
    CHECK(bad_len == 0);
    CHECK(bad_form == 0);

    /* 이어 붙인 스트림 200프레임 (길이 1..40): 순서대로 전부 나오고, lead END 마다 빈 프레임 1개 */
    {
        static uint8_t st[200 * (SLIP_ENC_MAX(40))], src[200][40];
        size_t srcn[200], sl = 0;
        for (int f = 0; f < 200; f++) {
            srcn[f] = 1 + xs32(&seed) % 40u;
            for (size_t i = 0; i < srcn[f]; i++) src[f][i] = biased_byte(&seed);
            if (slip_encode(src[f], srcn[f], st + sl, sizeof st - sl, true, &m) == SLIP_OK) sl += m;
        }
        slip_dec_init(&d, dbuf, 64, SLIP_STRICT);
        int f = 0, mism = 0;
        for (size_t i = 0; i < sl; i++)
            if (slip_dec_push(&d, st[i]) == SLIP_FRAME) {
                if (f >= 200 || d.frame_len != srcn[f] || memcmp(d.buf, src[f], srcn[f]) != 0) mism++;
                f++;
            }
        printf("stream of 200 frames: decoded=%d mismatches=%d empty=%u\n", f, mism, (unsigned)d.empty);
        CHECK(f == 200 && mism == 0);
        CHECK(d.empty == 200);
    }

    /* ===== Part B: 최악 오버헤드 2n+2 (전부 0xC0 / 전부 0xDB, n = 0..1006) ===== */
    int bad_worst = 0;
    for (size_t n = 0; n <= 1006; n++) {
        for (int which = 0; which < 2; which++) {
            memset(in, which ? 0xDB : 0xC0, n);
            if (slip_encode(in, n, enc, sizeof enc, true, &m) != SLIP_OK || m != 2 * n + 2) bad_worst++;
            if (slip_encode(in, n, enc, sizeof enc, false, &m) != SLIP_OK || m != 2 * n + 1) bad_worst++;
            if (!roundtrip(in, n, true, SLIP_STRICT)) bad_worst++;
        }
    }
    printf("all-0xC0 / all-0xDB n=0..1006: encoded == 2n+2 (lead) / 2n+1, bad=%d\n", bad_worst);
    CHECK(bad_worst == 0);
    CHECK(slip_encode(in, 1006, enc, sizeof enc, true, &m) == SLIP_OK && m == SLIP_ENC_MAX(1006) && m == 2014);

    /* 균일 랜덤 1006바이트 2000개: 실측 평균 오버헤드 (이론 n·2/256 + 2 = 9.86) */
    {
        uint64_t sum = 0; size_t mx = 0, mn = (size_t)-1;
        for (int t = 0; t < 2000; t++) {
            for (size_t i = 0; i < 1006; i++) in[i] = (uint8_t)xs32(&seed);
            if (slip_encode(in, 1006, enc, sizeof enc, true, &m) != SLIP_OK) m = 0;
            sum += m; if (m > mx) mx = m; if (m < mn) mn = m;
        }
        double mean = (double)sum / 2000.0;
        printf("random n=1006 x2000: encoded min=%zu mean=%.2f max=%zu (overhead mean %.2f B = %.2f%%), worst 2014\n",
               mn, mean, mx, mean - 1006, (mean - 1006) * 100.0 / 1006);
        CHECK(mean > 1006 + 2 + 6 && mean < 1006 + 2 + 10);   /* 기댓값 7.86 이스케이프, 표준편차 2.8 의 평균 → 넉넉한 창 */
        CHECK(mx < 2014);
    }

    /* 인코더 용량: 딱 맞으면 OK, 1 바이트라도 모자라면 OVERFLOW + cap 밖은 안 건드림 (모든 cap 0..need-1) */
    {
        static const uint8_t p3[6] = { 0x10, 0xC0, 0x20, 0xDB, 0xC0, 0x30 };
        size_t need = SLIP_ENC_MAX(0) + 6 + 3;   /* 2 + 6 + 특수 3 = 11 */
        uint8_t ob[32];
        int bad_cap = 0;
        for (size_t cap = 0; cap < need; cap++) {
            memset(ob, 0xAA, sizeof ob);
            if (slip_encode(p3, 6, ob, cap, true, &m) != SLIP_ERR_OVERFLOW || m != 0) bad_cap++;
            for (size_t i = cap; i < sizeof ob; i++) if (ob[i] != 0xAA) { bad_cap++; break; }
        }
        CHECK(bad_cap == 0);
        memset(ob, 0xAA, sizeof ob);
        CHECK(slip_encode(p3, 6, ob, need, true, &m) == SLIP_OK && m == need && ob[need] == 0xAA);
    }

    /* 연속 END: 빈 프레임은 전부 버리고 진짜 프레임 하나만 */
    static const uint8_t s_ends[6] = { 0xC0, 0xC0, 0xC0, 0x01, 0xC0, 0xC0 };
    slip_dec_init(&d, dbuf, 64, SLIP_STRICT);
    feed(&d, s_ends, 6, &lg);
    CHECK(lg.nev == 1 && lg.ev[0] == SLIP_FRAME && lg.lens[0] == 1 && lg.data[0] == 0x01 && d.empty == 4);

    /* 잘못된 이스케이프 ESC 0x41: STRICT 는 그 프레임 거부 후 다음 프레임 정상, LENIENT(RFC 예제)는 0x41 그대로 */
    static const uint8_t s_bad[8] = { 0xC0, 0x05, 0xDB, 0x41, 0x06, 0xC0, 0x07, 0xC0 };
    slip_dec_init(&d, dbuf, 64, SLIP_STRICT);
    feed(&d, s_bad, 8, &lg);
    CHECK(lg.nev == 2 && lg.ev[0] == SLIP_ERR_ESC && lg.ev[1] == SLIP_FRAME && lg.lens[0] == 1 && lg.data[0] == 0x07);
    CHECK(d.esc_err == 1 && d.frames == 1);
    slip_dec_init(&d, dbuf, 64, SLIP_LENIENT);
    feed(&d, s_bad, 8, &lg);
    CHECK(lg.nev == 2 && lg.lens[0] == 3 && lg.data[0] == 0x05 && lg.data[1] == 0x41 && lg.data[2] == 0x06 && lg.lens[1] == 1);

    /* ESC END: STRICT 는 오류 + END 로 재동기, LENIENT 는 0xC0 을 데이터로 삼켜 두 프레임이 합쳐진다 */
    static const uint8_t s_escend[7] = { 0xC0, 0x05, 0xDB, 0xC0, 0x07, 0x08, 0xC0 };
    slip_dec_init(&d, dbuf, 64, SLIP_STRICT);
    feed(&d, s_escend, 7, &lg);
    CHECK(lg.nev == 2 && lg.ev[0] == SLIP_ERR_ESC && lg.ev[1] == SLIP_FRAME && lg.lens[0] == 2 && lg.data[0] == 0x07);
    slip_dec_init(&d, dbuf, 64, SLIP_LENIENT);
    feed(&d, s_escend, 7, &lg);
    CHECK(lg.nev == 1 && lg.lens[0] == 4 && lg.data[0] == 0x05 && lg.data[1] == 0xC0 && lg.data[2] == 0x07);

    /* ESC ESC (흔한 인코더 버그 모양): STRICT 오류, LENIENT 는 0xDB 로 받아 버린다 */
    static const uint8_t s_escesc[5] = { 0xC0, 0xDB, 0xDB, 0x09, 0xC0 };
    slip_dec_init(&d, dbuf, 64, SLIP_STRICT);
    feed(&d, s_escesc, 5, &lg);
    CHECK(lg.nev == 1 && lg.ev[0] == SLIP_ERR_ESC);
    slip_dec_init(&d, dbuf, 64, SLIP_LENIENT);
    feed(&d, s_escesc, 5, &lg);
    CHECK(lg.nev == 1 && lg.ev[0] == SLIP_FRAME && lg.lens[0] == 2 && lg.data[0] == 0xDB && lg.data[1] == 0x09);

    /* 너무 긴 프레임 (cap 16): 16 OK, 17 → ERR_LONG 후 다음 프레임 정상. cap 은 디코딩 후 바이트 기준 */
    {
        uint8_t st[128]; size_t sl = 0;
        memset(in, 0x55, 17);
        slip_encode(in, 16, st + sl, sizeof st - sl, true, &m); sl += m;   /* 16바이트 */
        slip_encode(in, 17, st + sl, sizeof st - sl, true, &m); sl += m;   /* 17바이트 → 너무 김 */
        in[0] = 0x77;
        slip_encode(in, 1, st + sl, sizeof st - sl, true, &m); sl += m;    /* 그다음 정상 */
        slip_dec_init(&d, dbuf, 16, SLIP_STRICT);
        feed(&d, st, sl, &lg);
        CHECK(lg.nev == 3 && lg.ev[0] == SLIP_FRAME && lg.ev[1] == SLIP_ERR_LONG && lg.ev[2] == SLIP_FRAME);
        CHECK(lg.lens[0] == 16 && lg.lens[1] == 1 && lg.data[16] == 0x77 && d.long_err == 1);
        memset(in, 0xC0, 17);
        slip_encode(in, 16, st, sizeof st, true, &m);                      /* 34바이트로 인코딩되지만 디코딩 16 */
        slip_dec_init(&d, dbuf, 16, SLIP_STRICT);
        feed(&d, st, m, &lg);
        CHECK(m == 34 && lg.nev == 1 && lg.ev[0] == SLIP_FRAME && lg.lens[0] == 16 && lg.data[15] == 0xC0);
        slip_encode(in, 17, st, sizeof st, true, &m);                      /* 17번째가 ESC ESC_END 쌍에서 넘침 */
        slip_dec_init(&d, dbuf, 16, SLIP_STRICT);
        feed(&d, st, m, &lg);
        CHECK(lg.nev == 1 && lg.ev[0] == SLIP_ERR_LONG);
    }

    /* 스트림 중간에서 시작: SLIP 혼자서는 앞부분이 잘린 프레임을 "정상"으로 넘긴다 → Part C 에서 CRC 로 막는다 */
    slip_encode((const uint8_t *)"ABCDEFGH", 8, enc, sizeof enc, true, &m);
    slip_dec_init(&d, dbuf, 64, SLIP_STRICT);
    feed(&d, enc + 4, m >= 4 ? m - 4 : 0, &lg);
    CHECK(lg.nev == 1 && lg.ev[0] == SLIP_FRAME && lg.lens[0] == 5 && memcmp(lg.data, "DEFGH", 5) == 0);

    /* ===== Part C: SLIP + CRC-16 링크 ===== */
    CHECK(crc16_ccitt_false((const uint8_t *)"123456789", 9) == 0x29B1);
    {
        uint8_t ob[SLIP_ENC_MAX(LINK_MTU + 2)];
        CHECK(link_send(in, LINK_MTU + 1, ob, sizeof ob, true, &m) == LINK_ERR_MTU && m == 0);
        CHECK(link_send(in, LINK_MTU, ob, 10, true, &m) == SLIP_ERR_OVERFLOW && m == 0);
        /* "123456789" → CRC 0x29B1 이 뒤에 붙는다: C0 31..39 29 B1 C0 */
        CHECK(link_send((const uint8_t *)"123456789", 9, ob, sizeof ob, true, &m) == SLIP_OK && m == 13 &&
              ob[0] == 0xC0 && ob[1] == 0x31 && ob[10] == 0x29 && ob[11] == 0xB1 && ob[12] == 0xC0);
        /* CRC 바이트 자체가 C0/DB 인 페이로드도 왕복 (CRC 도 스터핑됨) */
        int crc_special = 0, bad_link = 0;
        link_rx rx; link_rx_init(&rx, SLIP_STRICT);
        for (int t = 0; t < 3000; t++) {
            size_t n = xs32(&seed) % (LINK_MTU + 1u);
            for (size_t i = 0; i < n; i++) in[i] = biased_byte(&seed);
            uint16_t c = crc16_ccitt_false(in, n);
            crc_special += ((c >> 8) == 0xC0 || (c >> 8) == 0xDB || (c & 0xFF) == 0xC0 || (c & 0xFF) == 0xDB);
            if (link_send(in, n, ob, sizeof ob, true, &m) != SLIP_OK) { bad_link++; continue; }
            int got = 0;
            for (size_t i = 0; i < m; i++) {
                const uint8_t *pl; size_t pn;
                if (link_rx_byte(&rx, ob[i], &pl, &pn)) got += (pn == n && memcmp(pl, in, n) == 0) ? 1 : 100;
            }
            if (got != 1) bad_link++;
        }
        printf("link roundtrip 3000 frames (len 0..64): bad=%d, frames whose CRC has a C0/DB byte=%d\n", bad_link, crc_special);
        CHECK(bad_link == 0 && crc_special > 0);
    }

    /* 끝 END 유실: lead END 가 있으면 다음 프레임의 lead 가 대신 닫아 둘 다 살고, 없으면 두 프레임이 합쳐져 둘 다 죽는다 */
    for (int lead = 0; lead < 2; lead++) {
        uint8_t st[64]; size_t sl = 0;
        link_send((const uint8_t *)"AB", 2, st, sizeof st, lead, &m); sl = m > 0 ? m - 1 : 0;   /* 끝 END 를 버림 */
        link_send((const uint8_t *)"CD", 2, st + sl, sizeof st - sl, lead, &m); sl += m;
        link_rx rx; link_rx_init(&rx, SLIP_STRICT);
        int got = 0;
        for (size_t i = 0; i < sl; i++) { const uint8_t *pl; size_t pn; got += link_rx_byte(&rx, st[i], &pl, &pn); }
        CHECK(lead ? (got == 2 && rx.crc_err == 0) : (got == 0 && rx.crc_err == 1));
    }

    /* 랜덤 페이로드 NFR 개 (길이 0..64) */
    for (int f = 0; f < NFR; f++) {
        g_pn[f] = xs32(&seed) % (LINK_MTU + 1u);
        for (size_t i = 0; i < g_pn[f]; i++) g_pl[f][i] = biased_byte(&seed);
    }

    /* 스트림 1 (STRICT, lead END): 중간 시작 + 프레임 사이 잡음(END 포함 가능, 1/3 은 ESC 로 끝남)
     *                               + 끝 END 유실 + 선 위 비트 반전 → 반전 안 된 프레임은 전부, 반전된 건 하나도 안 나와야 */
    {
        static bool want[NFR];
        size_t sl = 0; int n_noise = 0, n_drop = 0, n_flip = 0;
        uint8_t ob[SLIP_ENC_MAX(LINK_MTU + 2)];
        /* 수신기는 앞 프레임의 중간부터 듣기 시작 (ESC 바로 뒤일 수도 있다) */
        for (size_t i = 0; i < 40; i++) in[i] = biased_byte(&seed);
        link_send(in, 40, ob, sizeof ob, true, &m);
        size_t cut = m > 2 ? 1 + xs32(&seed) % (m - 2) : 0;   /* m > 2 는 스텁 대비 */
        memcpy(g_stream, ob + cut, m - cut); sl = m - cut;
        bool prev_dropped = false;
        for (int f = 0; f < NFR; f++) {
            uint32_t r = xs32(&seed);
            if (!prev_dropped && r % 4u == 0) {                  /* 잡음 1..8 바이트 */
                size_t nn = 1 + (r >> 8) % 8u;
                for (size_t i = 0; i < nn; i++) g_stream[sl++] = (uint8_t)xs32(&seed);
                if ((r >> 16) % 3u == 0) g_stream[sl - 1] = SLIP_ESC;    /* 잡음이 ESC 로 끝남 */
                n_noise++;
            }
            link_send(g_pl[f], g_pn[f], ob, sizeof ob, true, &m);
            want[f] = true;
            if (m > 2 && (r >> 20) % 25u == 0) {                          /* 선 위 비트 1개 반전 (END 두 개는 제외) */
                size_t pos = 1 + xs32(&seed) % (m - 2);
                ob[pos] ^= (uint8_t)(1u << (xs32(&seed) % 8u));
                want[f] = false; n_flip++;
            }
            prev_dropped = (m > 0 && (r >> 28) % 8u == 0);                /* 끝 END 유실 (다음 프레임 lead END 가 대신 끝냄) */
            if (prev_dropped) { m--; n_drop++; }
            memcpy(g_stream + sl, ob, m); sl += m;
        }
        g_stream[sl++] = SLIP_END;                               /* 마지막 프레임 END 가 유실됐을 때를 위해 */
        link_rx rx; link_rx_init(&rx, SLIP_STRICT);
        int deliv;
        bool okm = deliver_matches(&rx, g_stream, sl, want, &deliv);
        printf("stream1 strict+lead: %d frames, noise=%d dropEND=%d flips=%d -> delivered=%d (expect %d) "
               "crc_err=%u short=%u esc_err=%u long=%u\n", NFR, n_noise, n_drop, n_flip, deliv, NFR - n_flip,
               (unsigned)rx.crc_err, (unsigned)rx.short_err, (unsigned)rx.dec.esc_err, (unsigned)rx.dec.long_err);
        CHECK(okm);
        CHECK(deliv == NFR - n_flip && n_flip > 0 && n_noise > 0 && n_drop > 0);
    }

    /* 스트림 2 (STRICT, lead END 없음): 잡음(END 없는)이 다음 프레임 앞에 붙어 그 프레임이 죽는다 */
    {
        static bool want[NFR];
        size_t sl = 0; int n_noise = 0;
        uint8_t ob[SLIP_ENC_MAX(LINK_MTU + 2)];
        for (int f = 0; f < NFR; f++) {
            uint32_t r = xs32(&seed);
            want[f] = true;
            if (r % 4u == 0) {
                size_t nn = 1 + (r >> 8) % 8u;
                for (size_t i = 0; i < nn; i++) { uint8_t b; do b = (uint8_t)xs32(&seed); while (b == SLIP_END); g_stream[sl++] = b; }
                want[f] = false; n_noise++;
            }
            link_send(g_pl[f], g_pn[f], ob, sizeof ob, false, &m);
            memcpy(g_stream + sl, ob, m); sl += m;
        }
        link_rx rx; link_rx_init(&rx, SLIP_STRICT);
        int deliv;
        bool okm = deliver_matches(&rx, g_stream, sl, want, &deliv);
        printf("stream2 strict, no lead END: noise before %d frames -> delivered=%d, lost=%d\n", n_noise, deliv, NFR - deliv);
        CHECK(okm && deliv == NFR - n_noise);
    }

    /* 스트림 3: 같은 lead END 스트림, 잡음은 항상 ESC 로 끝남 → LENIENT 는 ESC END 로 lead END 를 삼켜 그 프레임을 잃고,
     *          STRICT 는 전부 살린다 */
    {
        static bool want_l[NFR], want_s[NFR];
        size_t sl = 0; int n_noise = 0;
        uint8_t ob[SLIP_ENC_MAX(LINK_MTU + 2)];
        for (int f = 0; f < NFR; f++) {
            uint32_t r = xs32(&seed);
            want_l[f] = want_s[f] = true;
            if (r % 4u == 0) {
                size_t nn = 1 + (r >> 8) % 4u;
                for (size_t i = 0; i < nn; i++) { uint8_t b; do b = (uint8_t)xs32(&seed); while (b == SLIP_END || b == SLIP_ESC); g_stream[sl++] = b; }
                g_stream[sl - 1] = SLIP_ESC;
                want_l[f] = false; n_noise++;
            }
            link_send(g_pl[f], g_pn[f], ob, sizeof ob, true, &m);
            memcpy(g_stream + sl, ob, m); sl += m;
        }
        link_rx rl, rs; link_rx_init(&rl, SLIP_LENIENT); link_rx_init(&rs, SLIP_STRICT);
        int dl, ds;
        bool okl = deliver_matches(&rl, g_stream, sl, want_l, &dl);
        bool oks = deliver_matches(&rs, g_stream, sl, want_s, &ds);
        printf("stream3 lead END, noise ending in ESC before %d frames: LENIENT delivered=%d (lost %d), STRICT delivered=%d (lost %d)\n",
               n_noise, dl, NFR - dl, ds, NFR - ds);
        CHECK(okl && dl == NFR - n_noise);
        CHECK(oks && ds == NFR);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
