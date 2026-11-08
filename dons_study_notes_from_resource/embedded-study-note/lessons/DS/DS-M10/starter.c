/* DS-M10 가변 길이 레코드 로그 링 버퍼
 * [len|type|timestamp|payload] 레코드를 2^n 바이트 링에 기록하는 트레이스 로거.
 * 공간이 모자라면 가장 오래된 레코드를 "통째로" 버린다 (overwrite-oldest).
 * 설계 선택: 레코드는 링 끝에서 쪼개져 앞으로 이어진다 (split) — 패딩 없음.
 * head/tail 은 자유 증가 uint32_t 바이트 카운터 (DS-B04). malloc 없음.
 * 헤더는 바이트 단위로 직렬화한다 (구조체 캐스팅 금지 — 정렬·aliasing·엔디언).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LOG_SIZE        256u                      /* 2^n 바이트 */
#define LOG_MASK        (LOG_SIZE - 1u)
#define LOG_HDR         7u                        /* len(2) + type(1) + ts(4) — 패딩 없음 */
#define LOG_MAX_PAYLOAD (LOG_SIZE - LOG_HDR)      /* 레코드 하나 <= 링 전체 */
_Static_assert((LOG_SIZE & LOG_MASK) == 0u, "LOG_SIZE must be a power of two");
_Static_assert(LOG_MAX_PAYLOAD <= 0xFFFFu, "len field is 16-bit");

typedef struct {
    uint16_t len;                                 /* payload 바이트 수 (헤더 제외) */
    uint8_t  type;
    uint32_t ts;
} rec_hdr_t;                                      /* CPU 쪽 표현. 링 속 바이트 배치는 hdr_encode 가 정한다 */

typedef struct {
    uint32_t head;                                /* 지금까지 쓴 총 바이트 = 다음 레코드 시작 */
    uint32_t tail;                                /* 가장 오래된 레코드 시작 — 항상 레코드 경계 */
    uint32_t nrec;                                /* 들어 있는 레코드 수 */
    uint32_t dropped;                             /* 덮어써서 버린 레코드 수 (누적) */
    uint8_t  data[LOG_SIZE];
} tlog_t;

static void log_init(tlog_t *l, uint32_t start) {
    l->head = l->tail = start;                    /* start 로 2^32 경계 근처 테스트 (DS-B04) */
    l->nrec = l->dropped = 0;
    memset(l->data, 0, sizeof l->data);
}
static uint32_t log_used(const tlog_t *l) { return l->head - l->tail; }

/* ---------------- Part A: 바이트 직렬화 + 쪼개진 복사 + append/pop ---------------- */
static void put_le16(uint8_t *p, uint16_t v) {
    (void)p; (void)v;
    /* TODO: 하위 바이트 먼저 — p[0] = v 의 bit 0..7, p[1] = bit 8..15 */
}
static void put_le32(uint8_t *p, uint32_t v) {
    (void)p; (void)v;
    /* TODO: p[0..3] = v 의 바이트 0..3 (little-endian). 포인터 캐스팅 금지 */
}
static uint16_t get_le16(const uint8_t *p) {
    (void)p;
    /* TODO: p[0] | p[1] << 8 */
    return 0;
}
static uint32_t get_le32(const uint8_t *p) {
    (void)p;
    /* TODO: 각 바이트를 uint32_t 로 바꾼 "뒤에" 시프트 (p[3] << 24 를 int 로 하면 UB) */
    return 0;
}

/* 링 안 배치: [0..1] len LE, [2] type, [3..6] ts LE */
static void hdr_encode(uint8_t out[LOG_HDR], const rec_hdr_t *h) {
    (void)out; (void)h;
    /* TODO: out[0..1] = len, out[2] = type, out[3..6] = ts */
}
static void hdr_decode(const uint8_t in[LOG_HDR], rec_hdr_t *h) {
    (void)in;
    /* TODO: hdr_encode 의 거울상 */
    h->len = 0; h->type = 0; h->ts = 0;
}

/* 카운터 pos 위치부터 n 바이트 (n <= LOG_SIZE). 끝에서 잘리면 memcpy 2번 */
static void ring_put(uint8_t data[LOG_SIZE], uint32_t pos, const uint8_t *src, uint32_t n) {
    (void)data; (void)pos; (void)src; (void)n;
    /* TODO: n == 0 이면 끝. i = pos & LOG_MASK, 끝까지 first 바이트, 나머지는 data[0] 부터 */
}
static void ring_get(const uint8_t data[LOG_SIZE], uint32_t pos, uint8_t *dst, uint32_t n) {
    (void)data; (void)pos; (void)dst; (void)n;
    /* TODO: ring_put 의 거울상 */
}

static void log_make_room(tlog_t *l, uint32_t need);   /* Part B */

/* 레코드 추가. len > LOG_MAX_PAYLOAD 면 false 이고 버퍼는 그대로 */
static bool log_append(tlog_t *l, uint8_t type, uint32_t ts, const uint8_t *payload, uint16_t len) {
    (void)l; (void)type; (void)ts; (void)payload; (void)len; (void)log_make_room;
    /* TODO: 너무 크면 false (버퍼는 그대로) → log_make_room(need) → 헤더 7바이트 + payload 를
     *       head 위치에 ring_put → 마지막에 head += need, nrec++ */
    return false;
}

/* 가장 오래된 레코드를 꺼낸다. 반환: payload 길이, -1 = 비었음, -2 = cap 부족(소비 안 함) */
static int32_t log_pop(tlog_t *l, rec_hdr_t *h, uint8_t *payload, uint32_t cap) {
    (void)l; (void)h; (void)payload; (void)cap;
    /* TODO: 비었으면 -1. tail 의 헤더를 ring_get + decode, len > cap 이면 -2 (소비 안 함),
     *       아니면 payload 복사 후 tail 전진, nrec-- */
    return -1;
}

/* ---------------- Part B: 가장 오래된 레코드 통째로 버리기 ---------------- */
/* tail 의 헤더(쪼개져 있을 수 있음)를 읽어 레코드 하나만큼 tail 전진. 버린 바이트 수 반환 */
static uint32_t log_drop_oldest(tlog_t *l) {
    (void)l;
    /* TODO: tail 의 헤더(쪼개져 있을 수 있음)에서 len → tail += HDR + len, nrec--, dropped++ */
    return 0;
}

/* 빈 공간이 need 이상이 될 때까지 가장 오래된 레코드부터 버린다 (need <= LOG_SIZE) */
static void log_make_room(tlog_t *l, uint32_t need) {
    (void)l; (void)need; (void)log_drop_oldest;
    /* TODO: 빈 공간(LOG_SIZE - used) < need 인 동안 log_drop_oldest */
}

/* ---------------- Part C: 신뢰할 수 없는 메모리 걷기 · noinit 복구 · 덤프 파서 ---------------- */
typedef void (*rec_cb)(void *ctx, const rec_hdr_t *h, const uint8_t *payload);

/* data/head/tail 을 "믿지 않고" tail 부터 레코드를 걷는다. 걸은 레코드 수 반환.
 * 구조가 맞으면(head 에 정확히 도착) *ok = true. 망가졌으면 거기서 멈추고 *ok = false.
 * cb 가 있으면 검증된 레코드마다 호출 (payload 는 연속 버퍼로 복사해서 넘김). */
static uint32_t log_walk(const uint8_t data[LOG_SIZE], uint32_t head, uint32_t tail,
                         rec_cb cb, void *ctx, bool *ok) {
    (void)data; (void)head; (void)tail; (void)cb; (void)ctx;
    /* TODO: used > LOG_SIZE 면 실패. pos = tail 부터: 남은 바이트 < HDR 이거나 len > 남은 - HDR 이면
     *       거기서 멈춤(*ok = false). cb 가 있으면 payload 를 연속 버퍼로 복사해 호출. head 에 정확히
     *       도착하면 *ok = true. 반환 = 검증된 레코드 수 */
    *ok = false;
    return 0;
}

#define TRACE_MAGIC 0x54524331u                   /* "TRC1" */
typedef struct {
    uint32_t magic;
    uint32_t boot;                                /* warm boot 횟수 */
    tlog_t   log;
} trace_nv_t;
/* 실제 칩: __attribute__((section(".noinit"))) static trace_nv_t g_trace;
 * 링커 스크립트 .noinit (NOLOAD) — startup 코드가 0 으로 지우지 않는 RAM */

/* 부팅마다 호출. 살아남은 로그가 구조적으로 멀쩡하면 이어 쓰고(true = warm),
 * 아니면(전원 투입 직후의 쓰레기, 손상) 새로 초기화한다(false = cold) */
static bool trace_attach(trace_nv_t *nv) {
    (void)nv; (void)log_walk;
    /* TODO: magic 이 맞고 log_walk 가 ok 면 nrec 을 다시 센 값으로, boot++, true (warm).
     *       아니면 log_init(start 0), boot = 0, magic 은 마지막에 기록, false (cold) */
    return false;
}

/* 덤프 이미지 (호스트로 보내는 바이트열, 모든 필드 LE):
 * [0] magic [4] boot [8] head [12] tail [16] dropped [20..] data[LOG_SIZE] */
#define IMG_HDR   20u
#define IMG_BYTES (IMG_HDR + LOG_SIZE)
static void trace_save(const trace_nv_t *nv, uint8_t img[IMG_BYTES]) {
    put_le32(img + 0, nv->magic);
    put_le32(img + 4, nv->boot);
    put_le32(img + 8, nv->log.head);
    put_le32(img + 12, nv->log.tail);
    put_le32(img + 16, nv->log.dropped);
    memcpy(img + IMG_HDR, nv->log.data, LOG_SIZE);
}

/* 호스트 쪽 덤프 파서: 이미지를 검증하며 레코드마다 cb. 반환 = 검증된 레코드 수 */
static uint32_t dump_parse(const uint8_t *img, size_t n, rec_cb cb, void *ctx, bool *ok) {
    (void)img; (void)n; (void)cb; (void)ctx;
    /* TODO: 길이 < IMG_BYTES 또는 magic 불일치면 0. 아니면 get_le32 로 head/tail 을 꺼내 log_walk */
    *ok = false;
    return 0;
}

enum { TR_BOOT = 1, TR_SENSOR = 2, TR_MSG = 3, TR_FAULT = 0xEF };

/* HardFault 핸들러가 부르는 함수 (주어짐): PC/LR 을 LE 로 payload 에 담는다 */
static void trace_fault(trace_nv_t *nv, uint32_t ts, uint32_t pc, uint32_t lr) {
    uint8_t p[8];
    put_le32(p, pc);
    put_le32(p + 4, lr);
    (void)log_append(&nv->log, TR_FAULT, ts, p, sizeof p);
}

/* ---------------- 검사 도구 (주어짐) — 학생 코드와 독립적으로 바이트를 직접 읽는다 ---------------- */
typedef struct { uint16_t len; uint8_t type; uint32_t ts; uint8_t p[LOG_MAX_PAYLOAD]; } mrec_t;
#define MCAP (LOG_SIZE / LOG_HDR + 1u)            /* 링에 들어갈 수 있는 최대 레코드 수 + 1 */

static uint8_t rbyte(const uint8_t *d, uint32_t pos) { return d[pos & LOG_MASK]; }

/* 불변식: used <= SIZE, tail 부터 레코드 크기만큼 걸으면 정확히 head 에 도착, 개수 == nrec.
 * out 이 있으면 레코드를 복사해 둔다 (모델 비교용) */
static bool log_check(const tlog_t *l, mrec_t *out, uint32_t *cnt) {
    uint32_t pos = l->tail, n = 0;
    if (l->head - l->tail > LOG_SIZE) return false;
    while (pos != l->head) {
        uint32_t remain = l->head - pos;
        if (remain < LOG_HDR || n >= MCAP) return false;
        uint32_t len = (uint32_t)rbyte(l->data, pos) | (uint32_t)rbyte(l->data, pos + 1u) << 8;
        if (len > remain - LOG_HDR) return false;
        if (out) {
            out[n].len = (uint16_t)len;
            out[n].type = rbyte(l->data, pos + 2u);
            out[n].ts = 0;
            for (uint32_t k = 0; k < 4; k++) out[n].ts |= (uint32_t)rbyte(l->data, pos + 3u + k) << (8u * k);
            for (uint32_t k = 0; k < len; k++) out[n].p[k] = rbyte(l->data, pos + LOG_HDR + k);
        }
        pos += LOG_HDR + len;
        n++;
    }
    if (cnt) *cnt = n;
    return n == l->nrec;
}

static bool mrec_eq(const mrec_t *a, const mrec_t *b) {
    return a->len == b->len && a->type == b->type && a->ts == b->ts && memcmp(a->p, b->p, a->len) == 0;
}

static uint32_t rng_state = 1u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

/* 참조 모델: 레코드 단위 원형 큐. 바이트 합이 SIZE 를 넘으면 앞에서부터 버림 */
static mrec_t g_model[MCAP];
static uint32_t m_head, m_n, m_bytes, m_dropped;
static void model_reset(void) { m_head = m_n = m_bytes = m_dropped = 0; }
static void model_drop(void) { m_bytes -= LOG_HDR + g_model[m_head].len; m_head = (m_head + 1u) % MCAP; m_n--; }
static void model_append(const mrec_t *r) {
    while (m_bytes + LOG_HDR + r->len > LOG_SIZE) { model_drop(); m_dropped++; }
    g_model[(m_head + m_n) % MCAP] = *r;
    m_n++;
    m_bytes += LOG_HDR + r->len;
}

static mrec_t g_snap[MCAP], g_snap2[MCAP];
static bool log_matches_model(const tlog_t *l) {
    uint32_t n = 0;
    if (!log_check(l, g_snap, &n) || n != m_n || log_used(l) != m_bytes || l->dropped != m_dropped) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!mrec_eq(&g_snap[i], &g_model[(m_head + i) % MCAP])) return false;
    return true;
}

/* 덤프 포매터: 마지막 몇 줄을 문자열로 */
typedef struct { char lines[8][96]; uint32_t n; } dumpbuf_t;
static void fmt_cb(void *ctx, const rec_hdr_t *h, const uint8_t *p) {
    dumpbuf_t *d = ctx;
    static const char *names[256] = { [TR_BOOT] = "BOOT", [TR_SENSOR] = "SENSOR", [TR_MSG] = "MSG", [TR_FAULT] = "FAULT" };
    char *s = d->lines[d->n % 8u];
    int k = snprintf(s, 96, "t=%-5u %-6s len=%-2u:", (unsigned)h->ts, names[h->type] ? names[h->type] : "?", (unsigned)h->len);
    for (uint32_t i = 0; i < h->len && i < 16u && k < 90; i++) k += snprintf(s + k, (size_t)(96 - k), " %02x", p[i]);
    d->n++;
}
static const char *dump_line(const dumpbuf_t *d, uint32_t back) { return d->lines[(d->n - 1u - back) % 8u]; }

static tlog_t g_l;
static trace_nv_t g_nv;
static uint8_t g_img[IMG_BYTES];

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    tlog_t *l = &g_l;
    rec_hdr_t h = { 0, 0, 0 };
    uint8_t buf[LOG_SIZE] = { 0 };

    /* Part A: 바이트 직렬화 — 정확한 바이트 배치와 왕복 */
    {
        uint8_t b[LOG_HDR] = { 0 };
        rec_hdr_t in = { 0x0102, 0xAB, 0x11223344u }, out = { 0, 0, 0 };
        hdr_encode(b, &in);
        static const uint8_t want[LOG_HDR] = { 0x02, 0x01, 0xAB, 0x44, 0x33, 0x22, 0x11 };
        CHECK(memcmp(b, want, LOG_HDR) == 0);
        hdr_decode(b, &out);
        CHECK(out.len == 0x0102 && out.type == 0xAB && out.ts == 0x11223344u);
        uint8_t odd[9] = { 0 };
        put_le32(odd + 1, 0xFEDCBA98u);           /* 홀수 주소에서도 안전 (캐스팅 없음) */
        CHECK(odd[1] == 0x98 && odd[4] == 0xFE && get_le32(odd + 1) == 0xFEDCBA98u && get_le16(odd + 3) == 0xFEDC);
    }
    /* Part A: 끝에서 쪼개지는 ring_put/ring_get (카운터 2^32 경계 포함) */
    {
        uint8_t src[10], dst[10] = { 0 };
        for (int i = 0; i < 10; i++) src[i] = (uint8_t)(0xA0 + i);
        log_init(l, 0);
        ring_put(l->data, LOG_SIZE - 3u, src, 10);
        CHECK(l->data[LOG_SIZE - 3u] == 0xA0 && l->data[LOG_SIZE - 1u] == 0xA2 && l->data[0] == 0xA3 && l->data[6] == 0xA9);
        ring_get(l->data, LOG_SIZE - 3u, dst, 10);
        CHECK(memcmp(src, dst, 10) == 0);
        memset(dst, 0, sizeof dst);
        ring_put(l->data, 0xFFFFFFFEu, src, 10);  /* &MASK = 254 → 2 + 8 조각 */
        ring_get(l->data, 0xFFFFFFFEu, dst, 10);
        CHECK(memcmp(src, dst, 10) == 0 && l->data[0] == 0xA2);
    }
    /* Part A: append/pop FIFO + 헤더가 쪼개지는 모든 위치 (k = 0..7) */
    {
        uint8_t p1[3] = { 1, 2, 3 }, p2[5] = { 9, 8, 7, 6, 5 };
        log_init(l, 0);
        CHECK(log_pop(l, &h, buf, sizeof buf) == -1);
        CHECK(log_append(l, TR_SENSOR, 100, p1, 3) && log_append(l, TR_MSG, 200, p2, 5) && log_append(l, TR_BOOT, 300, NULL, 0));
        CHECK(log_used(l) == 3u * LOG_HDR + 8u && l->nrec == 3 && log_check(l, NULL, NULL));
        CHECK(log_pop(l, &h, buf, 2) == -2 && l->nrec == 3);                  /* cap 부족: 소비 안 함 */
        CHECK(log_pop(l, &h, buf, sizeof buf) == 3 && h.type == TR_SENSOR && h.ts == 100 && memcmp(buf, p1, 3) == 0);
        CHECK(log_pop(l, &h, buf, sizeof buf) == 5 && h.type == TR_MSG && h.ts == 200 && memcmp(buf, p2, 5) == 0);
        CHECK(log_pop(l, &h, buf, 0) == 0 && h.type == TR_BOOT && log_pop(l, &h, buf, sizeof buf) == -1);
        bool ok = true;
        for (uint32_t k = 0; k <= LOG_HDR; k++) {                          /* 헤더 k 바이트만 끝에 */
            log_init(l, LOG_SIZE - k);
            memset(buf, 0, sizeof buf);
            ok = ok && log_append(l, (uint8_t)(0x40 + k), 0xCAFE0000u + k, p2, 5) && log_check(l, NULL, NULL);
            ok = ok && log_pop(l, &h, buf, sizeof buf) == 5 && h.type == 0x40 + k && h.ts == 0xCAFE0000u + k
                    && memcmp(buf, p2, 5) == 0 && l->head == l->tail;
        }
        CHECK(ok);
    }

    /* Part B: 가득 차면 가장 오래된 레코드를 통째로 버린다 */
    {
        uint8_t p[LOG_MAX_PAYLOAD];
        for (uint32_t i = 0; i < LOG_MAX_PAYLOAD; i++) p[i] = (uint8_t)(i * 7u + 1u);
        log_init(l, 0xFFFFFF80u);                                        /* 중간에 2^32 를 넘는다 */
        bool ok = true;
        for (uint32_t i = 0; i < 8; i++) ok = ok && log_append(l, TR_SENSOR, i, p, 25);  /* 32 B × 8 = 256 */
        CHECK(ok && log_used(l) == LOG_SIZE && l->nrec == 8 && l->dropped == 0 && log_check(l, NULL, NULL));
        CHECK(log_append(l, TR_SENSOR, 8, p, 25) && l->nrec == 8 && l->dropped == 1 && log_check(l, NULL, NULL));
        CHECK(log_pop(l, &h, buf, sizeof buf) == 25 && h.ts == 1);       /* ts 0 이 버려졌다 */
        CHECK(log_append(l, TR_MSG, 9, p, 80) && l->dropped == 3 && l->nrec == 6 && log_check(l, NULL, NULL));
        uint32_t used = log_used(l), head = l->head, nrec = l->nrec;      /* 너무 큰 레코드: 거절 + 그대로 */
        CHECK(!log_append(l, TR_MSG, 10, p, (uint16_t)(LOG_MAX_PAYLOAD + 1u)) && log_used(l) == used && l->head == head && l->nrec == nrec);
        CHECK(log_append(l, TR_MSG, 11, p, (uint16_t)LOG_MAX_PAYLOAD) && l->nrec == 1 && log_used(l) == LOG_SIZE && l->dropped == 9);
        CHECK(log_pop(l, &h, buf, sizeof buf) == (int32_t)LOG_MAX_PAYLOAD && h.ts == 11 && memcmp(buf, p, LOG_MAX_PAYLOAD) == 0);
        log_init(l, 3);
        for (uint32_t i = 0; i < 40; i++) ok = ok && log_append(l, TR_BOOT, i, NULL, 0);
        CHECK(ok && l->nrec == LOG_SIZE / LOG_HDR && l->dropped == 40u - LOG_SIZE / LOG_HDR && log_check(l, NULL, NULL));
    }
    /* Part B: 랜덤 append/pop vs 레코드 큐 모델 — 시작 카운터 4곳, 매 연산 뒤 전체 비교 */
    {
        static const uint32_t starts[4] = { 0u, 0xFFFFFF9Cu, 0xFFFFF448u, 0x7FFFFFF0u };
        bool ok = true;
        uint32_t n_app = 0, n_rej = 0, n_pop = 0, n_split_hdr = 0, n_split_pay = 0, max_nrec = 0;
        uint8_t pay[LOG_MAX_PAYLOAD + 32u];
        for (int s = 0; s < 4 && ok; s++) {
            log_init(l, starts[s]);
            model_reset();
            rng_state = 0xD5A10u + (uint32_t)s;
            for (int op = 0; op < 20000 && ok; op++) {
                uint32_t r = xorshift32(), r2 = xorshift32();
                if (r % 10u < 6u) {
                    uint32_t len = (r2 % 100u < 80u) ? r2 % 40u
                                 : (r2 % 100u < 95u) ? r2 % (LOG_MAX_PAYLOAD + 1u)
                                 : LOG_MAX_PAYLOAD + 1u + r2 % 20u;
                    mrec_t m = { (uint16_t)len, (uint8_t)(r >> 8), (uint32_t)op * 3u, { 0 } };
                    for (uint32_t k = 0; k < len; k++) pay[k] = (uint8_t)xorshift32();
                    uint32_t off = l->head & LOG_MASK;
                    bool app = log_append(l, m.type, m.ts, pay, (uint16_t)len);
                    if (len > LOG_MAX_PAYLOAD) { ok = !app; n_rej++; }
                    else {
                        memcpy(m.p, pay, len);
                        model_append(&m);
                        ok = app; n_app++;
                        if (off > LOG_SIZE - LOG_HDR) n_split_hdr++;
                        else if (off + LOG_HDR + len > LOG_SIZE) n_split_pay++;
                    }
                } else {
                    uint32_t cap = (r2 & 3u) == 0u ? r2 % 16u : LOG_MAX_PAYLOAD;
                    int32_t got = log_pop(l, &h, buf, cap);
                    if (m_n == 0) ok = got == -1;
                    else if (g_model[m_head].len > cap) ok = got == -2;
                    else {
                        const mrec_t *f = &g_model[m_head];
                        ok = got == f->len && h.type == f->type && h.ts == f->ts && memcmp(buf, f->p, f->len) == 0;
                        model_drop();
                        n_pop++;
                    }
                }
                ok = ok && log_matches_model(l);
                if (l->nrec > max_nrec) max_nrec = l->nrec;
            }
        }
        CHECK(ok);
        printf("[B] random 4x20000 vs model: %s | append %u, reject %u, pop %u, dropped(last run) %u\n",
               ok ? "match" : "MISMATCH", n_app, n_rej, n_pop, l->dropped);
        printf("[B] records split at wrap: header %u, payload-only %u | max records held %u\n",
               n_split_hdr, n_split_pay, max_nrec);
    }

    /* Part C: cold boot — 전원 투입 직후 noinit RAM 은 쓰레기 */
    {
        bool ok = true;
        for (uint32_t seed = 1; seed <= 2000 && ok; seed++) {
            rng_state = seed * 2654435761u;
            unsigned char *raw = (unsigned char *)&g_nv;
            for (size_t i = 0; i < sizeof g_nv; i++) raw[i] = (unsigned char)xorshift32();
            ok = !trace_attach(&g_nv) && g_nv.magic == TRACE_MAGIC && g_nv.boot == 0
              && g_nv.log.nrec == 0 && log_used(&g_nv.log) == 0 && log_check(&g_nv.log, NULL, NULL);
        }
        CHECK(ok);
        /* magic 은 맞는데 head/tail/data 가 쓰레기: 받아들이면 반드시 구조가 맞아야 한다 */
        uint32_t accepted = 0, nonempty = 0;
        ok = true;
        for (uint32_t t = 0; t < 20000 && ok; t++) {
            for (uint32_t i = 0; i < LOG_SIZE; i++) g_nv.log.data[i] = (uint8_t)xorshift32();
            g_nv.magic = TRACE_MAGIC;
            g_nv.log.tail = xorshift32();
            g_nv.log.head = g_nv.log.tail + (t % 2u ? xorshift32() % (LOG_SIZE + 64u) : xorshift32() % 24u);
            g_nv.log.nrec = xorshift32();
            if (trace_attach(&g_nv)) {
                accepted++;
                if (g_nv.log.nrec) nonempty++;
                ok = log_check(&g_nv.log, NULL, NULL);
            } else {
                ok = g_nv.log.nrec == 0 && log_used(&g_nv.log) == 0;
            }
        }
        CHECK(ok);
        printf("[C] garbage with valid magic: accepted %u / 20000 (non-empty %u), accepted all valid: %s\n",
               accepted, nonempty, ok ? "yes" : "NO");
    }
    /* Part C: 로그 → HardFault → warm reset → 살아남은 로그 + BOOT 레코드 */
    {
        log_init(&g_nv.log, 0); g_nv.magic = 0;                           /* 전원 투입 흉내 */
        CHECK(!trace_attach(&g_nv) && g_nv.boot == 0);
        uint8_t rr[1] = { 0x01 };                                         /* reset reason: power-on */
        (void)log_append(&g_nv.log, TR_BOOT, 0, rr, 1);
        uint32_t tick = 10;
        for (uint32_t i = 0; i < 40; i++, tick += 10) {                   /* 링을 몇 바퀴 돈다 */
            uint8_t p[4];
            put_le16(p, (uint16_t)(1000u + i)); put_le16(p + 2, (uint16_t)(25u + (i & 3u)));
            (void)log_append(&g_nv.log, TR_SENSOR, tick, p, 4);
        }
        static const uint8_t msg[] = "i2c timeout";
        (void)log_append(&g_nv.log, TR_MSG, tick, msg, (uint16_t)(sizeof msg - 1u));
        trace_fault(&g_nv, tick + 1u, 0x08001A2Cu, 0x08001F11u);
        uint32_t n_before = 0;
        CHECK(log_check(&g_nv.log, g_snap2, &n_before) && g_nv.log.dropped > 0);
        /* --- 리셋: .bss/.data 는 다시 초기화되지만 g_nv(noinit)는 그대로 --- */
        CHECK(trace_attach(&g_nv) && g_nv.boot == 1 && g_nv.log.nrec == n_before);
        uint32_t n_after = 0;
        bool same = log_check(&g_nv.log, g_snap, &n_after) && n_after == n_before;
        for (uint32_t i = 0; same && i < n_after; i++) same = mrec_eq(&g_snap[i], &g_snap2[i]);
        CHECK(same);
        uint8_t rr2[1] = { 0x08 };                                        /* reset reason: software */
        (void)log_append(&g_nv.log, TR_BOOT, 0, rr2, 1);

        /* 호스트 덤프: 이미지 → 파서 → 마지막 레코드들 */
        trace_save(&g_nv, g_img);
        dumpbuf_t d;
        memset(&d, 0, sizeof d);
        bool ok = false;
        uint32_t n = dump_parse(g_img, sizeof g_img, fmt_cb, &d, &ok);
        CHECK(ok && n == g_nv.log.nrec && d.n == n);
        CHECK(strcmp(dump_line(&d, 1), "t=411   FAULT  len=8 : 2c 1a 00 08 11 1f 00 08") == 0);
        CHECK(strcmp(dump_line(&d, 0), "t=0     BOOT   len=1 : 08") == 0);
        printf("[C] dump: %s, boot=%u, records=%u, dropped=%u, last 4:\n", ok ? "valid" : "INVALID",
               (unsigned)get_le32(g_img + 4), (unsigned)n, (unsigned)get_le32(g_img + 16));
        for (uint32_t i = 4; i-- > 0;) printf("    %s\n", dump_line(&d, i));

        /* 손상 1: 두 번째 레코드의 len 바이트를 0xFF 로 → 파서는 첫 레코드만 살리고 멈춘다 */
        uint32_t tail = get_le32(g_img + 12);
        uint32_t len0 = g_img[IMG_HDR + (tail & LOG_MASK)];
        uint32_t second = tail + LOG_HDR + len0;
        g_img[IMG_HDR + ((second + 1u) & LOG_MASK)] = 0xFF;               /* len 상위 바이트 */
        memset(&d, 0, sizeof d);
        n = dump_parse(g_img, sizeof g_img, fmt_cb, &d, &ok);
        CHECK(!ok && n == 1 && d.n == 1);
        /* 같은 손상이 noinit 에 있으면 attach 는 cold 로 판단해 새로 시작 */
        g_nv.log.data[(second + 1u) & LOG_MASK] = 0xFF;
        CHECK(!trace_attach(&g_nv) && g_nv.log.nrec == 0 && g_nv.boot == 0);
        /* 손상 2: 잘린 이미지, 틀린 magic, head-tail > SIZE */
        trace_save(&g_nv, g_img);
        CHECK(dump_parse(g_img, IMG_BYTES - 1u, NULL, NULL, &ok) == 0 && !ok);
        g_img[0] ^= 1u;
        CHECK(dump_parse(g_img, sizeof g_img, NULL, NULL, &ok) == 0 && !ok);
        g_img[0] ^= 1u;
        put_le32(g_img + 8, get_le32(g_img + 12) + LOG_SIZE + 1u);
        CHECK(dump_parse(g_img, sizeof g_img, NULL, NULL, &ok) == 0 && !ok);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
