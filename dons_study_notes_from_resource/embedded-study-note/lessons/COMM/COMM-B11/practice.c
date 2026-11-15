/* COMM-B11 추가 연습 — 한 번에 검증
 *
 * 연습 1: slip_encoded_len() — 인코딩하지 않고 정확한 크기 / slip_max_payload() — 고정 TX 버퍼에 최악으로도 들어가는 최대 n
 * 연습 2: slip_decode_inplace() — 같은 버퍼 안에서 디코딩 (쓰기 위치 ≤ 읽기 위치)
 * 연습 3: slip_dec_feed() — DMA 청크 단위 입력. 어디서 잘라 넣어도 바이트 단위와 같은 결과 (ESC 와 ESC_END 사이 분할 포함)
 * 연습 4: slip_tx_next() — UART TXE 인터럽트용 바이트 단위 인코더 (2n+2 버퍼 없이)
 * 연습 5: rfc_recv() — RFC 1055 예제 recv_packet 을 그대로 옮겨 "조용한 잘림"과 "ESC END 병합"을 재현, CRC 가 잡는지
 *
 * 자체 완결: 본문(solution.c)의 인코더/디코더/CRC 를 축약해 다시 담았다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define SLIP_END     0xC0u
#define SLIP_ESC     0xDBu
#define SLIP_ESC_END 0xDCu
#define SLIP_ESC_ESC 0xDDu

/* ----- 본문 축약판: 인코더, STRICT 디코더, CRC-16/CCITT-FALSE ----- */
static int slip_encode(const uint8_t *in, size_t n, uint8_t *out, size_t cap, bool lead, size_t *out_len) {
    size_t k = 0;
    *out_len = 0;
    if (lead) { if (k == cap) return -3; out[k++] = SLIP_END; }
    for (size_t i = 0; i < n; i++) {
        bool sp = in[i] == SLIP_END || in[i] == SLIP_ESC;
        if (cap - k < (sp ? 2u : 1u)) return -3;
        if (sp) { out[k++] = SLIP_ESC; out[k++] = in[i] == SLIP_END ? SLIP_ESC_END : SLIP_ESC_ESC; }
        else out[k++] = in[i];
    }
    if (k == cap) return -3;
    out[k++] = SLIP_END;
    *out_len = k;
    return 0;
}

enum { SLIP_MORE = 0, SLIP_FRAME = 1, SLIP_ERR_ESC = -1, SLIP_ERR_LONG = -2 };
enum { ST_NORMAL, ST_ESC, ST_DISCARD };
typedef struct { uint8_t *buf; size_t cap, len, frame_len; uint8_t state; int err; } slip_dec;   /* STRICT 만 */

static void dec_init(slip_dec *d, uint8_t *buf, size_t cap) { memset(d, 0, sizeof *d); d->buf = buf; d->cap = cap; }

static int dec_push(slip_dec *d, uint8_t b) {
    bool delim = (b == SLIP_END);
    if (d->state == ST_ESC) {
        d->state = ST_NORMAL;
        if (b == SLIP_ESC_END) b = SLIP_END;
        else if (b == SLIP_ESC_ESC) b = SLIP_ESC;
        else { d->state = ST_DISCARD; d->err = SLIP_ERR_ESC; if (!delim) return SLIP_MORE; }
    } else if (b == SLIP_ESC && d->state == ST_NORMAL) { d->state = ST_ESC; return SLIP_MORE; }
    if (delim) {
        if (d->state == ST_DISCARD) { d->state = ST_NORMAL; d->len = 0; return d->err; }
        if (d->len == 0) return SLIP_MORE;
        d->frame_len = d->len; d->len = 0; return SLIP_FRAME;
    }
    if (d->state == ST_DISCARD) return SLIP_MORE;
    if (d->len == d->cap) { d->state = ST_DISCARD; d->err = SLIP_ERR_LONG; return SLIP_MORE; }
    d->buf[d->len++] = b;
    return SLIP_MORE;
}

static uint16_t crc16(const uint8_t *p, size_t n) {
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)(p[i] << 8);
        for (int k = 0; k < 8; k++) c = (c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x1021u) : (uint16_t)(c << 1);
    }
    return c;
}

/* ----- 연습 1: 크기 계산 ----- */
static size_t slip_encoded_len(const uint8_t *in, size_t n, bool lead) {
    size_t k = 1 + (lead ? 1u : 0u);
    for (size_t i = 0; i < n; i++) k += (in[i] == SLIP_END || in[i] == SLIP_ESC) ? 2u : 1u;
    return k;
}
/* cap 바이트 버퍼에 "어떤 데이터든" 들어가는 최대 페이로드. 빈 프레임조차 안 들어가면 -1 */
static long slip_max_payload(size_t cap, bool lead) {
    size_t fixed = 1 + (lead ? 1u : 0u);
    return cap < fixed ? -1 : (long)((cap - fixed) / 2);
}

/* ----- 연습 2: 제자리 디코딩 (buf 안에 END 없이 프레임 본문만). 반환 = 디코딩 길이, 오류 -1 ----- */
static long slip_decode_inplace(uint8_t *buf, size_t len) {
    size_t w = 0;
    for (size_t r = 0; r < len; r++) {          /* 불변식: w <= r → 아직 안 읽은 바이트를 덮지 않는다 */
        uint8_t b = buf[r];
        if (b == SLIP_END) return -1;
        if (b == SLIP_ESC) {
            if (++r == len) return -1;          /* 끝에 홀로 남은 ESC */
            if (buf[r] == SLIP_ESC_END) b = SLIP_END;
            else if (buf[r] == SLIP_ESC_ESC) b = SLIP_ESC;
            else return -1;
        }
        buf[w++] = b;
    }
    return (long)w;
}

/* ----- 연습 3: 청크 입력 + 콜백 ----- */
typedef void (*slip_cb)(void *ctx, int ev, const uint8_t *p, size_t n);
static void slip_dec_feed(slip_dec *d, const uint8_t *chunk, size_t n, slip_cb cb, void *ctx) {
    for (size_t i = 0; i < n; i++) {
        int r = dec_push(d, chunk[i]);
        if (r == SLIP_FRAME) cb(ctx, r, d->buf, d->frame_len);
        else if (r != SLIP_MORE) cb(ctx, r, NULL, 0);
    }
}
typedef struct { uint32_t h; int nev; } ev_hash;   /* 사건 순서 + 내용을 FNV-1a 로 요약 */
static void hash_cb(void *ctx, int ev, const uint8_t *p, size_t n) {
    ev_hash *e = ctx;
    e->h = (e->h ^ (uint32_t)(ev + 7)) * 16777619u;
    for (size_t i = 0; i < n; i++) e->h = (e->h ^ p[i]) * 16777619u;
    e->h = (e->h ^ (uint32_t)n) * 16777619u;
    e->nev++;
}

/* ----- 연습 4: TX 인터럽트용 바이트 단위 인코더 ----- */
typedef struct { const uint8_t *p; size_t n, i; uint8_t stage, pend; } slip_tx;   /* stage: 0 lead, 1 본문, 2 이스케이프 둘째, 3 끝 END, 4 끝 */
static void slip_tx_start(slip_tx *t, const uint8_t *p, size_t n, bool lead) {
    t->p = p; t->n = n; t->i = 0; t->pend = 0; t->stage = lead ? 0 : 1;
}
static int slip_tx_next(slip_tx *t) {             /* 다음 와이어 바이트, 끝이면 -1. 호출당 O(1) */
    switch (t->stage) {
    case 0: t->stage = 1; return SLIP_END;
    case 1:
        if (t->i == t->n) { t->stage = 4; return SLIP_END; }
        {
            uint8_t b = t->p[t->i++];
            if (b == SLIP_END) { t->pend = SLIP_ESC_END; t->stage = 2; return SLIP_ESC; }
            if (b == SLIP_ESC) { t->pend = SLIP_ESC_ESC; t->stage = 2; return SLIP_ESC; }
            return b;
        }
    case 2: t->stage = 1; return t->pend;
    default: return -1;
    }
}

/* ----- 연습 5: RFC 1055 예제 recv_packet 을 그대로 (스트림 s 에서 *pos 부터 읽음, 스트림 끝이면 -1) ----- */
static long rfc_recv(const uint8_t *s, size_t sn, size_t *pos, uint8_t *p, size_t len) {
    size_t received = 0;
    while (*pos < sn) {
        uint8_t c = s[(*pos)++];
        switch (c) {
        case SLIP_END:
            if (received) return (long)received;
            break;
        case SLIP_ESC:
            if (*pos == sn) return -1;
            c = s[(*pos)++];
            switch (c) {
            case SLIP_ESC_END: c = SLIP_END; break;
            case SLIP_ESC_ESC: c = SLIP_ESC; break;
            }
            /* fall through — RFC 주석: 다른 값이면 "그냥 넣는다" */
        default:
            if (received < len) p[received++] = c;   /* 넘치면 조용히 버리고 계속 */
        }
    }
    return -1;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static uint8_t biased(uint32_t *s) {
    static const uint8_t sp[4] = { 0xC0, 0xDB, 0xDC, 0xDD };
    uint32_t r = xs32(s);
    return (r & 3u) == 0 ? sp[(r >> 2) & 3u] : (uint8_t)(r >> 8);
}

int main(void) {
    uint32_t seed = 0x5117u;
    static uint8_t in[400], enc[1000], tmp[1000], dbuf[400];
    size_t m;

    /* 연습 1 */
    int bad = 0;
    for (int t = 0; t < 3000; t++) {
        size_t n = xs32(&seed) % 301u; bool lead = t & 1;
        for (size_t i = 0; i < n; i++) in[i] = biased(&seed);
        if (slip_encode(in, n, enc, sizeof enc, lead, &m) != 0 || m != slip_encoded_len(in, n, lead)) bad++;
    }
    CHECK(bad == 0);
    CHECK(slip_max_payload(128, true) == 63 && slip_max_payload(129, true) == 63 && slip_max_payload(130, true) == 64);
    CHECK(slip_max_payload(1, false) == 0 && slip_max_payload(1, true) == -1 && slip_max_payload(0, false) == -1);
    memset(in, 0xC0, 64);   /* 최대값이 정말 최악에서도 들어가고, +1 이면 안 들어가는지 */
    CHECK(slip_encode(in, 63, enc, 128, true, &m) == 0 && m == 128);
    CHECK(slip_encode(in, 64, enc, 128, true, &m) == -3);

    /* 연습 2 */
    bad = 0;
    for (int t = 0; t < 3000; t++) {
        size_t n = xs32(&seed) % 301u;
        for (size_t i = 0; i < n; i++) in[i] = biased(&seed);
        slip_encode(in, n, enc, sizeof enc, false, &m);          /* 마지막 END 를 떼고 본문만 */
        long r = slip_decode_inplace(enc, m - 1);
        if (r != (long)n || memcmp(enc, in, n) != 0) bad++;
    }
    CHECK(bad == 0);
    static uint8_t lone[3] = { 0x01, 0x02, 0xDB }, badesc[3] = { 0x01, 0xDB, 0x41 }, hasend[2] = { 0x01, 0xC0 };
    CHECK(slip_decode_inplace(lone, 3) == -1 && slip_decode_inplace(badesc, 3) == -1 && slip_decode_inplace(hasend, 2) == -1);

    /* 연습 3: 30 프레임 스트림 + 잘못된 이스케이프 프레임 하나. 바이트 단위 vs 모든 2분할 vs 랜덤 청크 */
    size_t sl = 0;
    for (int f = 0; f < 30; f++) {
        size_t n = 1 + xs32(&seed) % 20u;
        for (size_t i = 0; i < n; i++) in[i] = biased(&seed);
        slip_encode(in, n, tmp + sl, sizeof tmp - sl, true, &m); sl += m;
        if (f == 10) { tmp[sl++] = 0x33; tmp[sl++] = SLIP_ESC; tmp[sl++] = 0x41; tmp[sl++] = SLIP_END; }
    }
    slip_dec d;
    ev_hash ref = { 2166136261u, 0 };
    dec_init(&d, dbuf, 64);
    for (size_t i = 0; i < sl; i++) slip_dec_feed(&d, tmp + i, 1, hash_cb, &ref);
    CHECK(ref.nev == 31);                                         /* 프레임 30 + ERR_ESC 1 */
    bad = 0;
    size_t esc_splits = 0;
    for (size_t cut = 0; cut <= sl; cut++) {
        ev_hash e = { 2166136261u, 0 };
        dec_init(&d, dbuf, 64);
        slip_dec_feed(&d, tmp, cut, hash_cb, &e);
        slip_dec_feed(&d, tmp + cut, sl - cut, hash_cb, &e);
        if (e.h != ref.h || e.nev != ref.nev) bad++;
        if (cut > 0 && tmp[cut - 1] == SLIP_ESC) esc_splits++;
    }
    for (int t = 0; t < 2000; t++) {
        ev_hash e = { 2166136261u, 0 };
        dec_init(&d, dbuf, 64);
        for (size_t i = 0; i < sl;) {
            size_t c = 1 + xs32(&seed) % 64u;
            if (c > sl - i) c = sl - i;
            slip_dec_feed(&d, tmp + i, c, hash_cb, &e);
            i += c;
        }
        if (e.h != ref.h) bad++;
    }
    printf("ex3: stream %zu bytes, %zu two-way cuts (%zu right after ESC) + 2000 random chunkings, mismatches=%d\n",
           sl, sl + 1, esc_splits, bad);
    CHECK(bad == 0 && esc_splits > 0);

    /* 연습 4 */
    bad = 0;
    for (int t = 0; t < 3000; t++) {
        size_t n = xs32(&seed) % 301u; bool lead = t & 1;
        for (size_t i = 0; i < n; i++) in[i] = biased(&seed);
        slip_encode(in, n, enc, sizeof enc, lead, &m);
        slip_tx tx; slip_tx_start(&tx, in, n, lead);
        size_t k = 0; int b;
        while ((b = slip_tx_next(&tx)) >= 0 && k < sizeof tmp) tmp[k++] = (uint8_t)b;
        if (k != m || memcmp(tmp, enc, m) != 0 || slip_tx_next(&tx) != -1) bad++;
    }
    CHECK(bad == 0);
    printf("ex4: sizeof(slip_tx)=%zu bytes of state vs a %u-byte encode buffer for n=1006\n", sizeof(slip_tx), 2 * 1006 + 2);

    /* 연습 5: RFC 예제 수신기의 두 함정 */
    {
        size_t pos = 0; long r;
        uint8_t pl[20], frame[22], rp[16];
        for (int i = 0; i < 20; i++) pl[i] = (uint8_t)(0x40 + i);
        uint16_t c = crc16(pl, 20);
        memcpy(frame, pl, 20); frame[20] = (uint8_t)(c >> 8); frame[21] = (uint8_t)c;
        slip_encode(frame, 22, enc, sizeof enc, true, &m);
        r = rfc_recv(enc, m, &pos, rp, sizeof rp);               /* 22 바이트 프레임, 버퍼 16 */
        CHECK(r == 16);                                           /* 오류 없이 16 바이트를 "정상" 프레임으로 돌려준다 */
        CHECK(crc16(rp, 14) != (uint16_t)(rp[14] << 8 | rp[15])); /* 그 위에 CRC 가 있으면 잡는다 */
        dec_init(&d, dbuf, 16);                                   /* 본문 디코더는 명시적으로 ERR_LONG */
        int ev = SLIP_MORE;
        for (size_t i = 0; i < m && ev == SLIP_MORE; i++) ev = dec_push(&d, enc[i]);
        CHECK(ev == SLIP_ERR_LONG);

        static const uint8_t merge[7] = { 0xC0, 0x05, 0xDB, 0xC0, 0x07, 0x08, 0xC0 };
        uint8_t big[16];
        pos = 0;
        r = rfc_recv(merge, 7, &pos, big, sizeof big);
        CHECK(r == 4 && big[1] == 0xC0 && big[2] == 0x07);        /* ESC END → 0xC0 데이터, 두 프레임이 하나로 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
