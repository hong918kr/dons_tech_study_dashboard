/* DS-M10 추가 연습 — 한 번에 검증
 * 연습 1: 패딩(skip-record) 방식 로거를 만들어 split 방식과 보존 레코드 수·낭비 바이트 비교
 * 연습 2: 시퀀스 번호 리더 커서 — 덮어쓰기로 놓친 레코드 수(lost) 보고
 * 연습 3: 마지막 n개 레코드 (tail -n) — 앞으로만 걸을 수 있는 로그에서
 * 연습 4: 레코드 CRC-8 — 단일 비트 오류를 구조 검사만으로 몇 개 잡나
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LOG_SIZE        256u
#define LOG_MASK        (LOG_SIZE - 1u)
#define LOG_HDR         7u
#define LOG_MAX_PAYLOAD (LOG_SIZE - LOG_HDR)
_Static_assert((LOG_SIZE & LOG_MASK) == 0u, "LOG_SIZE must be a power of two");

typedef struct { uint16_t len; uint8_t type; uint32_t ts; } rec_hdr_t;

static void put_le16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint16_t get_le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get_le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void hdr_encode(uint8_t out[LOG_HDR], const rec_hdr_t *h) { put_le16(out, h->len); out[2] = h->type; put_le32(out + 3, h->ts); }
static void hdr_decode(const uint8_t in[LOG_HDR], rec_hdr_t *h) { h->len = get_le16(in); h->type = in[2]; h->ts = get_le32(in + 3); }

static void ring_put(uint8_t *data, uint32_t pos, const uint8_t *src, uint32_t n) {
    if (n == 0) return;
    uint32_t i = pos & LOG_MASK, first = LOG_SIZE - i;
    if (first > n) first = n;
    memcpy(data + i, src, first);
    memcpy(data, src + first, n - first);
}
static void ring_get(const uint8_t *data, uint32_t pos, uint8_t *dst, uint32_t n) {
    if (n == 0) return;
    uint32_t i = pos & LOG_MASK, first = LOG_SIZE - i;
    if (first > n) first = n;
    memcpy(dst, data + i, first);
    memcpy(dst + first, data, n - first);
}

/* ---- 본문(split) 로거 + appended 카운터 (연습 2 용) ---- */
typedef struct {
    uint32_t head, tail, nrec, dropped;
    uint32_t appended;                     /* 지금까지 append 한 레코드 수 = 다음 레코드의 seq */
    uint8_t  data[LOG_SIZE];
} tlog_t;

static void log_init(tlog_t *l, uint32_t start) { memset(l, 0, sizeof *l); l->head = l->tail = start; }
static uint32_t log_used(const tlog_t *l) { return l->head - l->tail; }
static uint16_t len_at(const uint8_t *data, uint32_t pos) {
    uint8_t b[2]; ring_get(data, pos, b, 2); return get_le16(b);
}
static bool log_append(tlog_t *l, uint8_t type, uint32_t ts, const uint8_t *payload, uint16_t len) {
    if (len > LOG_MAX_PAYLOAD || (len != 0 && payload == NULL)) return false;
    uint32_t need = LOG_HDR + len;
    while (l->nrec != 0 && LOG_SIZE - log_used(l) < need) {
        l->tail += LOG_HDR + len_at(l->data, l->tail); l->nrec--; l->dropped++;
    }
    uint8_t hb[LOG_HDR];
    rec_hdr_t h = { len, type, ts };
    hdr_encode(hb, &h);
    ring_put(l->data, l->head, hb, LOG_HDR);
    ring_put(l->data, l->head + LOG_HDR, payload, len);
    l->head += need; l->nrec++; l->appended++;
    return true;
}
typedef void (*rec_cb)(void *ctx, const rec_hdr_t *h, const uint8_t *payload);
static uint32_t log_walk(const uint8_t *data, uint32_t head, uint32_t tail, rec_cb cb, void *ctx, bool *ok) {
    uint32_t pos = tail, n = 0;
    *ok = false;
    if (head - tail > LOG_SIZE) return 0;
    while (pos != head) {
        uint32_t remain = head - pos;
        if (remain < LOG_HDR) return n;
        uint8_t hb[LOG_HDR]; rec_hdr_t h;
        ring_get(data, pos, hb, LOG_HDR); hdr_decode(hb, &h);
        if (h.len > remain - LOG_HDR) return n;
        if (cb) { uint8_t tmp[LOG_MAX_PAYLOAD]; ring_get(data, pos + LOG_HDR, tmp, h.len); cb(ctx, &h, tmp); }
        pos += LOG_HDR + h.len; n++;
    }
    *ok = true;
    return n;
}

static uint32_t rng_state = 1u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return rng_state = x;
}

/* ================= 연습 1: 패딩(skip-record) 방식 ================= */
/* 레코드는 절대 쪼개지지 않는다. 끝까지 남은 칸(to_end)이 레코드보다 작으면:
 *   to_end >= HDR → PAD 레코드(type 0xFF, len = to_end - HDR)로 채우고 처음으로
 *   to_end <  HDR → 아무것도 안 쓰고 건너뜀 (읽는 쪽도 같은 규칙으로 건너뜀 = 암묵적 패딩) */
#define TR_PAD 0xFFu
typedef struct { uint32_t head, tail, nrec, dropped; uint8_t data[LOG_SIZE]; } plog_t;

static void pl_init(plog_t *l) { memset(l, 0, sizeof *l); }
/* tail 에서 한 단위(암묵 간격 / PAD / 레코드)를 버린다 */
static void pl_step_tail(plog_t *l) {
    uint32_t to_end = LOG_SIZE - (l->tail & LOG_MASK);
    if (to_end < LOG_HDR) { l->tail += to_end; return; }
    const uint8_t *p = l->data + (l->tail & LOG_MASK);       /* 패딩 방식: 헤더는 항상 연속 */
    l->tail += LOG_HDR + get_le16(p);
    if (p[2] != TR_PAD) { l->nrec--; l->dropped++; }
}
static void pl_free_until(plog_t *l, uint32_t need) {
    while (l->tail != l->head && LOG_SIZE - (l->head - l->tail) < need) pl_step_tail(l);
}
static bool pl_append(plog_t *l, uint8_t type, uint32_t ts, const uint8_t *payload, uint16_t len) {
    if (len > LOG_MAX_PAYLOAD || type == TR_PAD) return false;
    uint32_t need = LOG_HDR + len, to_end = LOG_SIZE - (l->head & LOG_MASK);
    if (to_end < need) {                                     /* 연속으로 안 들어감 → 끝을 버리고 처음으로 */
        pl_free_until(l, to_end + need);
        if (l->tail == l->head) l->tail = l->head = l->head + to_end;   /* 비었으면 간격도 공짜 */
        else {
            if (to_end >= LOG_HDR) {
                rec_hdr_t ph = { (uint16_t)(to_end - LOG_HDR), TR_PAD, 0 };
                hdr_encode(l->data + (l->head & LOG_MASK), &ph);
            }
            l->head += to_end;
        }
    }
    pl_free_until(l, need);
    uint8_t *dst = l->data + (l->head & LOG_MASK);           /* 이제 [dst, dst+need) 는 연속 */
    rec_hdr_t h = { len, type, ts };
    hdr_encode(dst, &h);
    if (len) memcpy(dst + LOG_HDR, payload, len);
    l->head += need; l->nrec++;
    return true;
}
/* 레코드만 순서대로 (PAD/간격 건너뜀). 반환 = 레코드 수, *live = 레코드가 차지한 바이트 */
static uint32_t pl_walk(const plog_t *l, uint32_t *ts_out, uint32_t *live) {
    uint32_t pos = l->tail, n = 0;
    *live = 0;
    while (pos != l->head) {
        uint32_t to_end = LOG_SIZE - (pos & LOG_MASK);
        if (to_end < LOG_HDR) { pos += to_end; continue; }
        rec_hdr_t h;
        hdr_decode(l->data + (pos & LOG_MASK), &h);
        if (LOG_HDR + h.len > to_end) return UINT32_MAX;     /* 패딩 방식에선 레코드가 쪼개질 수 없다 */
        if (h.type != TR_PAD) { if (ts_out) ts_out[n] = h.ts; n++; *live += LOG_HDR + h.len; }
        pos += LOG_HDR + h.len;
    }
    return n;
}

/* ================= 연습 2: 시퀀스 번호 리더 커서 ================= */
typedef struct { uint32_t seq, pos; } cursor_t;
static void cur_init(const tlog_t *l, cursor_t *c) { c->seq = l->appended - l->nrec; c->pos = l->tail; }
/* 다음 레코드를 읽는다 (소비하지 않음). -1 = 더 없음. 덮어써져 놓친 레코드 수를 *lost 에 더한다 */
static int32_t cur_read(const tlog_t *l, cursor_t *c, rec_hdr_t *h, uint8_t *payload, uint32_t *lost) {
    uint32_t first = l->appended - l->nrec;                  /* tail 에 있는 레코드의 seq */
    if ((int32_t)(c->seq - first) < 0) {                     /* 내가 읽을 레코드가 이미 덮어써짐 */
        *lost += first - c->seq;
        c->seq = first; c->pos = l->tail;
    }
    if (c->seq == l->appended) return -1;
    uint8_t hb[LOG_HDR];
    ring_get(l->data, c->pos, hb, LOG_HDR); hdr_decode(hb, h);
    ring_get(l->data, c->pos + LOG_HDR, payload, h->len);
    c->pos += LOG_HDR + h->len; c->seq++;
    return h->len;
}

/* ================= 연습 3: 마지막 n개 (tail -n) ================= */
/* 뒤로는 못 걷는다(길이가 헤더 앞에만 있음) → 앞으로 걸으며 최근 n개 위치를 작은 링에 기억 */
#define LASTN_MAX 8u
static uint32_t log_last_n(const tlog_t *l, uint32_t n, uint32_t pos_out[LASTN_MAX]) {
    uint32_t ring[LASTN_MAX], seen = 0, pos = l->tail;
    if (n > LASTN_MAX) n = LASTN_MAX;
    if (n == 0) return 0;
    while (pos != l->head) {
        ring[seen % n] = pos;
        seen++;
        pos += LOG_HDR + len_at(l->data, pos);
    }
    uint32_t k = seen < n ? seen : n;
    for (uint32_t i = 0; i < k; i++) pos_out[i] = ring[(seen - k + i) % n];
    return k;
}

/* ================= 연습 4: 레코드 CRC-8 ================= */
/* CRC-8 (poly 0x07, init 0x00 — "CRC-8/SMBUS"). payload 마지막 바이트 = 헤더 7 + payload[0..len-2] 의 CRC */
static uint8_t crc8(uint8_t crc, const uint8_t *p, uint32_t n) {
    while (n--) {
        crc ^= *p++;
        for (int b = 0; b < 8; b++) crc = (uint8_t)((crc & 0x80u) ? (crc << 1) ^ 0x07u : crc << 1);
    }
    return crc;
}
static bool crc_append(tlog_t *l, uint8_t type, uint32_t ts, const uint8_t *body, uint16_t blen) {
    uint8_t rec[LOG_MAX_PAYLOAD], hb[LOG_HDR];
    if (blen + 1u > LOG_MAX_PAYLOAD) return false;
    rec_hdr_t h = { (uint16_t)(blen + 1u), type, ts };
    hdr_encode(hb, &h);
    memcpy(rec, body, blen);
    rec[blen] = crc8(crc8(0, hb, LOG_HDR), body, blen);
    return log_append(l, type, ts, rec, (uint16_t)(blen + 1u));
}
typedef struct { uint32_t bad; } crcctx_t;
static void crc_cb(void *ctx, const rec_hdr_t *h, const uint8_t *p) {
    uint8_t hb[LOG_HDR];
    hdr_encode(hb, h);
    if (h->len == 0 || crc8(crc8(0, hb, LOG_HDR), p, h->len - 1u) != p[h->len - 1u]) ((crcctx_t *)ctx)->bad++;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static tlog_t g_l;
static plog_t g_pl;

int main(void) {
    uint8_t pay[LOG_MAX_PAYLOAD];
    for (uint32_t i = 0; i < LOG_MAX_PAYLOAD; i++) pay[i] = (uint8_t)(i ^ 0x5Au);

    /* ---- 연습 1: 패딩 방식 정해진 경우 ---- */
    {
        plog_t *p = &g_pl;
        uint32_t live = 0, ts[64];
        pl_init(p);
        CHECK(pl_append(p, 1, 0, pay, 243) && p->head == 250);            /* off 250 → 끝까지 6 (< HDR) */
        CHECK(pl_append(p, 1, 1, pay, 3) && p->head == 266 && p->tail == 256);  /* 비었다 → 간격 공짜, 0 번 칸부터 */
        CHECK(pl_walk(p, ts, &live) == 1 && ts[0] == 1 && live == 10);
        pl_init(p);
        CHECK(pl_append(p, 1, 0, pay, 100) && pl_append(p, 1, 1, pay, 100) && p->head == 214);  /* off 214, to_end 42 */
        CHECK(pl_append(p, 1, 2, pay, 50) && p->dropped == 1 && p->data[214 + 2] == TR_PAD && get_le16(p->data + 214) == 35);
        CHECK(pl_walk(p, ts, &live) == 2 && ts[0] == 1 && ts[1] == 2 && live == 107u + 57u && p->head - p->tail == 107u + 42u + 57u);
        pl_init(p);
        CHECK(pl_append(p, 1, 0, pay, 10) && pl_append(p, 1, 1, pay, (uint16_t)LOG_MAX_PAYLOAD) && p->nrec == 1);  /* 링 전체 크기도 들어간다 */
        CHECK(pl_walk(p, ts, &live) == 1 && live == LOG_SIZE && !pl_append(p, TR_PAD, 0, pay, 1));
    }
    /* ---- 연습 1: 랜덤 — 패딩 방식은 항상 "최신 레코드들의 연속 suffix" 를 보존 + split 과 비교 ---- */
    {
        static const char *wname[3] = { "small 0..39", "mixed", "large 60..180" };
        bool ok = true;
        printf("[1] workload       | split avg recs | pad avg recs | split util | pad util | pad avg waste B | pad max waste B\n");
        for (int w = 0; w < 3 && ok; w++) {
            tlog_t *s = &g_l; plog_t *p = &g_pl;
            log_init(s, 0); pl_init(p);
            rng_state = 0x1234u + (uint32_t)w;
            uint64_t sum_s = 0, sum_p = 0, live_s = 0, live_p = 0, waste = 0; uint32_t maxw = 0, samples = 0;
            for (uint32_t i = 0; i < 20000 && ok; i++) {
                uint32_t r = xorshift32();
                uint16_t len = (uint16_t)(w == 0 ? r % 40u : w == 1 ? (r % 5u == 0 ? r % 200u : r % 40u) : 60u + r % 121u);
                ok = log_append(s, 1, i, pay, len) && pl_append(p, 1, i, pay, len);
                uint32_t ts[64], lv = 0, n = pl_walk(p, ts, &lv);
                ok = ok && n == p->nrec && n >= 1 && ts[n - 1] == i;
                for (uint32_t k = 1; ok && k < n; k++) ok = ts[k] == ts[k - 1] + 1u;   /* 빈틈 없는 최신 suffix */
                ok = ok && p->head - p->tail <= LOG_SIZE && lv <= p->head - p->tail;
                if (i >= 1000) {                                                       /* 안정 상태만 */
                    uint32_t wst = (p->head - p->tail) - lv;
                    sum_s += s->nrec; sum_p += p->nrec; live_s += log_used(s); live_p += lv;
                    waste += wst; if (wst > maxw) maxw = wst; samples++;
                }
            }
            printf("[1] %-15s | %14.2f | %12.2f | %9.1f%% | %7.1f%% | %15.1f | %15u\n", wname[w],
                   (double)sum_s / samples, (double)sum_p / samples, 100.0 * (double)live_s / samples / LOG_SIZE,
                   100.0 * (double)live_p / samples / LOG_SIZE, (double)waste / samples, maxw);
        }
        CHECK(ok);
    }

    /* ---- 연습 2: 리더 커서 + lost ---- */
    {
        tlog_t *l = &g_l;
        rec_hdr_t h; uint8_t buf[LOG_MAX_PAYLOAD];
        cursor_t c; uint32_t lost = 0;
        log_init(l, 0xFFFFFF00u);
        cur_init(l, &c);
        for (uint32_t i = 0; i < 5; i++) log_append(l, 2, i, pay, 9);       /* 16 B × 5 */
        CHECK(cur_read(l, &c, &h, buf, &lost) == 9 && h.ts == 0 && lost == 0);
        for (uint32_t i = 5; i < 40; i++) log_append(l, 2, i, pay, 9);      /* 링에는 최근 16개만 */
        CHECK(l->nrec == 16 && cur_read(l, &c, &h, buf, &lost) == 9 && h.ts == 24 && lost == 23);
        while (cur_read(l, &c, &h, buf, &lost) >= 0) {}
        CHECK(h.ts == 39 && c.seq == 40 && lost == 23 && cur_read(l, &c, &h, buf, &lost) == -1);
        /* 랜덤: 읽은 것 + 놓친 것 == 쓴 것, 읽은 ts 는 항상 증가 */
        log_init(l, 0); cur_init(l, &c); lost = 0;
        rng_state = 777u;
        uint32_t got = 0, last = UINT32_MAX; bool ok = true;
        for (uint32_t i = 0; i < 30000 && ok; i++) {
            uint32_t r = xorshift32();
            if (r % 3u) ok = log_append(l, 2, i, pay, (uint16_t)(r % 30u));
            else for (uint32_t k = r % 6u; k-- > 0 && cur_read(l, &c, &h, buf, &lost) >= 0;) {
                ok = ok && (last == UINT32_MAX || h.ts > last) && memcmp(buf, pay, h.len) == 0;
                last = h.ts; got++;
            }
        }
        while (ok && cur_read(l, &c, &h, buf, &lost) >= 0) got++;
        CHECK(ok && got + lost == l->appended);
        printf("[2] reader: appended %u, read %u, lost %u (overwritten before read)\n", l->appended, got, lost);
    }

    /* ---- 연습 3: 마지막 n개 ---- */
    {
        tlog_t *l = &g_l;
        uint32_t pos[LASTN_MAX];
        log_init(l, 0xFFFFFFF0u);
        CHECK(log_last_n(l, 3, pos) == 0);
        for (uint32_t i = 0; i < 2; i++) log_append(l, 3, 100 + i, pay, 5);
        CHECK(log_last_n(l, 3, pos) == 2 && pos[0] == l->tail);
        bool ok = true;
        for (uint32_t i = 2; i < 50; i++) {
            log_append(l, 3, 100 + i, pay, (uint16_t)(i % 13u));
            uint32_t k = log_last_n(l, 4, pos);
            for (uint32_t j = 0; ok && j < k; j++) {
                uint8_t hb[LOG_HDR]; rec_hdr_t h;
                ring_get(l->data, pos[j], hb, LOG_HDR); hdr_decode(hb, &h);
                ok = h.ts == 100u + i - (k - 1u - j);
            }
            ok = ok && k == (l->nrec < 4u ? l->nrec : 4u);
        }
        CHECK(ok);
    }

    /* ---- 연습 4: 단일 비트 오류 주입 — 구조 검사 vs 구조 + CRC-8 ---- */
    {
        tlog_t *l = &g_l;
        log_init(l, 0);
        rng_state = 4242u;
        for (uint32_t i = 0; i < 60; i++) crc_append(l, (uint8_t)(1u + i % 3u), 1000u + 7u * i, pay, (uint16_t)(xorshift32() % 20u));
        bool ok0 = false; crcctx_t cc = { 0 };
        CHECK(log_walk(l->data, l->head, l->tail, crc_cb, &cc, &ok0) == l->nrec && ok0 && cc.bad == 0);
        /* 각 바이트가 어느 필드인지 표시 */
        uint8_t field[LOG_SIZE];                                  /* 0 len, 1 type, 2 ts, 3 payload(+crc) */
        for (uint32_t pos = l->tail; pos != l->head;) {
            uint32_t len = len_at(l->data, pos);
            for (uint32_t k = 0; k < LOG_HDR + len; k++) field[(pos + k) & LOG_MASK] = k < 2 ? 0 : k == 2 ? 1 : k < 7 ? 2 : 3;
            pos += LOG_HDR + len;
        }
        static const char *fname[4] = { "len", "type", "ts", "payload+crc" };
        uint32_t flips[4] = { 0 }, det_s[4] = { 0 }, det_c[4] = { 0 };
        for (uint32_t pos = l->tail; pos != l->head; pos++) {
            uint32_t i = pos & LOG_MASK, f = field[i];
            for (int b = 0; b < 8; b++) {
                l->data[i] ^= (uint8_t)(1u << b);
                bool okw = false; crcctx_t c2 = { 0 };
                log_walk(l->data, l->head, l->tail, crc_cb, &c2, &okw);
                flips[f]++;
                if (!okw) { det_s[f]++; det_c[f]++; }
                else if (c2.bad) det_c[f]++;
                l->data[i] ^= (uint8_t)(1u << b);
            }
        }
        printf("[4] %u records, %u live bytes; single-bit flips:\n", l->nrec, log_used(l));
        printf("[4] field        | flips | structure-only | structure+CRC-8\n");
        uint32_t ts_ = 0, tc = 0, tf = 0;
        for (int f = 0; f < 4; f++) {
            printf("[4] %-12s | %5u | %14u | %15u\n", fname[f], flips[f], det_s[f], det_c[f]);
            tf += flips[f]; ts_ += det_s[f]; tc += det_c[f];
        }
        printf("[4] total        | %5u | %14u | %15u\n", tf, ts_, tc);
        CHECK(det_c[1] == flips[1] && det_c[2] == flips[2] && det_c[3] == flips[3]);   /* 길이가 그대로면 CRC 가 다 잡는다 */
        CHECK(det_s[1] == 0 && det_s[2] == 0 && det_s[3] == 0);                         /* 구조 검사는 len 만 본다 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
