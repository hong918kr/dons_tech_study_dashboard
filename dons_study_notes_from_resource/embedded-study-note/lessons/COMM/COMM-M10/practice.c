/* COMM-M10 추가 연습 — 한 번에 검증
 *
 * 파서 본체는 solution.c 사본 (+ 연습 1 의 헤더 표 훅 hdr_ok).
 * 연습 1: CLASS/ID 별 허용 길이 표로 헤더 단계에서 거르기 → 가짜 sync 의 거짓 수락 차단
 * 연습 2: Fletcher-8x2 의 2비트 미검출 쌍 개수 예측식 vs 전수 탐색
 * 연습 3: CFG 전송 후 UBX-ACK-ACK / ACK-NAK 대기 (다른 메시지·쓰레기 섞인 스트림, 타임아웃)
 * 연습 4: 통계 카운터 차분으로 링크 진단 (침묵 / sync 없음 / 노이즈 / overflow / 정상)
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define UBX_SYNC1        0xB5u
#define UBX_SYNC2        0x62u
#define UBX_MAX_PAYLOAD  256u                      /* 길이 가드 (우리 펌웨어의 정의; UBX 자체는 65535 까지) */
#define UBX_OVERHEAD     8u                        /* sync 2 + CLASS ID LEN×2 + CK×2 */
#define UBX_RAW_MAX      (UBX_MAX_PAYLOAD + 6u)    /* sync 뒤에 먹는 최대 바이트: 4 + 256 + 2 = 262 */

enum { UBX_ERR_LEN = -1, UBX_ERR_CAP = -2 };

/* ---------------- Part A: 체크섬과 인코더 ---------------- */

/* 8비트 Fletcher (UBX 방식, mod 256). p[0..n) 은 CLASS 부터 PAYLOAD 끝까지. */
static void ubx_checksum(const uint8_t *p, size_t n, uint8_t *ck_a, uint8_t *ck_b) {
    uint8_t a = 0, b = 0;
    for (size_t i = 0; i < n; i++) {
        a = (uint8_t)(a + p[i]);
        b = (uint8_t)(b + a);
    }
    *ck_a = a;
    *ck_b = b;
}

/* 프레임 하나를 out 에 쓴다. 반환: 쓴 바이트 수(len + 8) 또는 UBX_ERR_LEN / UBX_ERR_CAP (out 은 건드리지 않음). */
static int ubx_encode(uint8_t cls, uint8_t id, const uint8_t *pl, size_t len, uint8_t *out, size_t cap) {
    if (len > UBX_MAX_PAYLOAD) return UBX_ERR_LEN;
    if (out == NULL || cap < len + UBX_OVERHEAD) return UBX_ERR_CAP;
    out[0] = UBX_SYNC1;
    out[1] = UBX_SYNC2;
    out[2] = cls;
    out[3] = id;
    out[4] = (uint8_t)len;
    out[5] = (uint8_t)(len >> 8);
    if (len) memcpy(out + 6, pl, len);
    ubx_checksum(out + 2, len + 4, &out[6 + len], &out[7 + len]);
    return (int)(len + UBX_OVERHEAD);
}

/* ---------------- 파서 ---------------- */

typedef enum { ST_SYNC1, ST_SYNC2, ST_CLASS, ST_ID, ST_LEN1, ST_LEN2, ST_PAYLOAD, ST_CK_A, ST_CK_B } ubx_state;
typedef enum { EV_NONE, EV_FRAME, EV_ERR_LEN, EV_ERR_CK } ubx_event;

typedef void (*ubx_cb)(void *ctx, uint8_t cls, uint8_t id, const uint8_t *pl, uint16_t len);

typedef struct {
    uint32_t frames_ok;   /* 체크섬 통과해 전달한 프레임 */
    uint32_t sync_skip;   /* sync 를 찾느라 건너뛴 바이트 (rescan 으로 다시 본 바이트도 다시 센다) */
    uint32_t bad_len;     /* LEN > UBX_MAX_PAYLOAD */
    uint32_t ck_fail;     /* 체크섬 불일치 */
    uint32_t timeout;     /* 바이트 간격 > T 로 버린 미완성 프레임 */
    uint32_t overflow;    /* RX 링 버퍼가 가득 차 버린 바이트 (Part C, 링 계층이 채움) */
    uint32_t rescans;     /* in-buffer rescan 횟수 */
} ubx_stats;

typedef struct {
    ubx_state st;
    uint8_t   raw[UBX_RAW_MAX];  /* sync 뒤 바이트 그대로: [0]=CLASS [1]=ID [2..3]=LEN [4..]=PAYLOAD, 끝 CK 2개 */
    uint16_t  raw_n, len;
    uint8_t   ck_a, ck_b;        /* CLASS 부터 누적하는 Fletcher */
    uint8_t   q[UBX_RAW_MAX];    /* rescan 작업 목록 (스택 대신 구조체 안 — ISR/작은 스택 대비) */
    bool      rescan;            /* false = naive: 오류 나면 먹은 바이트를 버리고 다음 바이트부터 sync 찾기 */
    uint32_t  timeout_us;        /* 0 = 타임아웃 끔 */
    uint32_t  last_t;            /* 마지막 바이트 타임스탬프 (µs, 32비트 랩 허용) */
    bool      have_t;
    ubx_stats stats;
    ubx_cb    cb;
    void     *ctx;
    bool    (*hdr_ok)(uint8_t cls, uint8_t id, uint16_t len);   /* 연습 1: NULL 이면 길이 가드만 */
    uint32_t  steps, max_steps;  /* ubx_feed/ubx_tick 한 번에 돈 ubx_step 수 (최악 비용 측정) */
    uint64_t  steps_total;
} ubx_parser;

static void ubx_init(ubx_parser *p, bool rescan, uint32_t timeout_us, ubx_cb cb, void *ctx) {
    memset(p, 0, sizeof *p);
    p->st = ST_SYNC1;
    p->rescan = rescan;
    p->timeout_us = timeout_us;
    p->cb = cb;
    p->ctx = ctx;
}

/* 바이트 하나로 상태를 한 칸. sync 뒤 바이트는 전부 raw[] 에 남긴다 (rescan 재료).
 * 오류면 카운터를 올리고 ST_SYNC1 로 돌아간 뒤 EV_ERR_* 를 돌려준다. */
static ubx_event ubx_step(ubx_parser *p, uint8_t b) {
    p->steps++;
    switch (p->st) {
    case ST_SYNC1:
        if (b == UBX_SYNC1) p->st = ST_SYNC2;
        else p->stats.sync_skip++;
        return EV_NONE;
    case ST_SYNC2:
        if (b == UBX_SYNC2) {
            p->st = ST_CLASS;
            p->raw_n = 0;
            p->ck_a = p->ck_b = 0;
        } else if (b == UBX_SYNC1) {
            p->stats.sync_skip++;              /* 앞의 B5 만 버리고, 이 B5 가 새 sync 후보 */
        } else {
            p->stats.sync_skip += 2;           /* B5 와 이 바이트 */
            p->st = ST_SYNC1;
        }
        return EV_NONE;
    default:
        break;
    }
    p->raw[p->raw_n++] = b;                    /* 최대 4 + 256 + 2 = 262 = UBX_RAW_MAX */
    switch (p->st) {
    case ST_CLASS: case ST_ID: case ST_LEN1:
        p->ck_a = (uint8_t)(p->ck_a + b);
        p->ck_b = (uint8_t)(p->ck_b + p->ck_a);
        p->st = (p->st == ST_CLASS) ? ST_ID : (p->st == ST_ID) ? ST_LEN1 : ST_LEN2;
        return EV_NONE;
    case ST_LEN2:
        p->len = (uint16_t)(p->raw[2] | (b << 8));
        if (p->len > UBX_MAX_PAYLOAD || (p->hdr_ok && !p->hdr_ok(p->raw[0], p->raw[1], p->len))) {
            p->stats.bad_len++; p->st = ST_SYNC1; return EV_ERR_LEN;    /* 연습 1: 헤더 표 위반도 여기서 */
        }
        p->ck_a = (uint8_t)(p->ck_a + b);
        p->ck_b = (uint8_t)(p->ck_b + p->ck_a);
        p->st = p->len ? ST_PAYLOAD : ST_CK_A;
        return EV_NONE;
    case ST_PAYLOAD:
        p->ck_a = (uint8_t)(p->ck_a + b);
        p->ck_b = (uint8_t)(p->ck_b + p->ck_a);
        if (p->raw_n == 4u + p->len) p->st = ST_CK_A;
        return EV_NONE;
    case ST_CK_A:
        p->st = ST_CK_B;
        return EV_NONE;
    case ST_CK_B:
        p->st = ST_SYNC1;
        if (p->raw[p->raw_n - 2] != p->ck_a || b != p->ck_b) { p->stats.ck_fail++; return EV_ERR_CK; }
        p->stats.frames_ok++;
        return EV_FRAME;
    default:
        p->st = ST_SYNC1;
        return EV_NONE;
    }
}

static void ubx_deliver(ubx_parser *p) {
    if (p->cb) p->cb(p->ctx, p->raw[0], p->raw[1], p->raw + 4, p->len);
}

/* p->q[0..qn) 를 차례로 먹인다. rescan 모드에서 오류가 나면 작업 목록을
 * (가짜 sync 뒤에 먹은 바이트 raw[]) + (목록의 아직 안 본 부분) 으로 바꿔 처음부터 다시 훑는다. */
static int ubx_run(ubx_parser *p, size_t qn) {
    int frames = 0;
    size_t i = 0;
    while (i < qn) {
        ubx_event ev = ubx_step(p, p->q[i++]);
        if (ev == EV_FRAME) {
            ubx_deliver(p);
            frames++;
        } else if (ev != EV_NONE && p->rescan) {
            size_t rest = qn - i;
            memmove(p->q + p->raw_n, p->q + i, rest);
            memcpy(p->q, p->raw, p->raw_n);
            qn = p->raw_n + rest;
            i = 0;
            p->stats.rescans++;
        }
    }
    return frames;
}

/* 타임아웃: 진행 중이던 (미완성) 프레임을 버린다. rescan 모드면 그 안의 바이트를 다시 훑고,
 * 다시 훑다가 또 미완성으로 끝나면 그것도 버리고 다시 — 매번 짧아지므로 끝난다. 반환: 전달한 프레임 수. */
static int ubx_flush(ubx_parser *p) {
    int frames = 0;
    if (p->st == ST_SYNC2) { p->st = ST_SYNC1; p->stats.sync_skip++; }
    while (p->st != ST_SYNC1) {
        p->stats.timeout++;
        size_t n = p->raw_n;
        p->st = ST_SYNC1;
        if (!p->rescan) break;
        memcpy(p->q, p->raw, n);
        frames += ubx_run(p, n);
        if (p->st == ST_SYNC2) { p->st = ST_SYNC1; p->stats.sync_skip++; }
    }
    return frames;
}

/* 수신 바이트 하나 (t_us = 수신 시각, µs). 반환: 이번 호출에서 전달한 프레임 수. */
static int ubx_feed(ubx_parser *p, uint8_t b, uint32_t t_us) {
    int frames = 0;
    p->steps = 0;
    if (p->timeout_us && p->have_t && p->st != ST_SYNC1 && (uint32_t)(t_us - p->last_t) > p->timeout_us)
        frames += ubx_flush(p);
    p->last_t = t_us;
    p->have_t = true;
    p->q[0] = b;
    frames += ubx_run(p, 1);
    if (p->steps > p->max_steps) p->max_steps = p->steps;
    p->steps_total += p->steps;
    return frames;
}


/* ======================= 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t g_rng = 1;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }
static uint32_t rnd(uint32_t n) { return xs32() % n; }
static void count_cb(void *ctx, uint8_t cls, uint8_t id, const uint8_t *pl, uint16_t len) {
    (void)cls; (void)id; (void)pl; (void)len; (*(uint32_t *)ctx)++;
}
#define BYTE_US 87u

/* ---------- 연습 1: 헤더 표 ---------- */
typedef struct { uint8_t cls, id; uint16_t min_len, max_len; } msg_rule;
/* 이 펌웨어가 쓰는 메시지만 (길이는 u-blox M8 프로토콜 문서 기준 대표값 — 확인 권장, 여기선 우리 정의) */
static const msg_rule g_rules[] = {
    { 0x01, 0x07, 92, 92 },     /* NAV-PVT */
    { 0x01, 0x02, 28, 28 },     /* NAV-POSLLH */
    { 0x05, 0x01, 2, 2 },       /* ACK-ACK */
    { 0x05, 0x00, 2, 2 },       /* ACK-NAK */
    { 0x0A, 0x04, 40, 250 },    /* MON-VER (40 + 30·N) — poll(0) 은 우리가 보내기만 함 */
};
static bool hdr_table_ok(uint8_t cls, uint8_t id, uint16_t len) {
    for (size_t i = 0; i < sizeof g_rules / sizeof g_rules[0]; i++)
        if (g_rules[i].cls == cls && g_rules[i].id == id) return len >= g_rules[i].min_len && len <= g_rules[i].max_len;
    return false;
}

/* ---------- 연습 2: 2비트 미검출 예측 ---------- */
/* 바이트 i, j (i<j) 의 같은 비트 k 를 뒤집을 때: ΔA = ±2^k ± 2^k, ΔB = 가중치(n-i, n-j) 차이 × 2^k.
 *  k = 7: +128 ≡ −128 (mod 256) 이라 ΔA 는 항상 0, ΔB = 128·(j−i) → (j−i) 짝수면 미검출.
 *  k < 7: ΔA = 0 이려면 두 비트 값이 달라야(하나는 0→1, 하나는 1→0), ΔB = 2^k·(j−i) ≡ 0 → (j−i) ≡ 0 mod 2^(8−k).
 *  서로 다른 비트 위치끼리는 ΔA ≠ 0 (2^k ± 2^m 은 256 의 배수가 아님). */
static uint32_t predict_undetected(const uint8_t *r, size_t n) {
    uint32_t c = 0;
    for (size_t i = 0; i < n; i++) for (size_t j = i + 1; j < n; j++) {
        size_t d = j - i;
        if (d % 2 == 0) c++;                                       /* bit 7 */
        for (int k = 0; k < 7; k++)
            if (d % (1u << (8 - k)) == 0 && (((r[i] ^ r[j]) >> k) & 1u)) c++;
    }
    return c;
}
static uint32_t brute_undetected(uint8_t *r, size_t n) {
    uint8_t a0, b0, a, b; uint32_t c = 0;
    ubx_checksum(r, n, &a0, &b0);
    for (size_t x = 0; x < n * 8; x++) for (size_t y = x + 1; y < n * 8; y++) {
        r[x / 8] ^= (uint8_t)(1u << (x % 8)); r[y / 8] ^= (uint8_t)(1u << (y % 8));
        ubx_checksum(r, n, &a, &b);
        c += (a == a0 && b == b0);
        r[x / 8] ^= (uint8_t)(1u << (x % 8)); r[y / 8] ^= (uint8_t)(1u << (y % 8));
    }
    return c;
}

/* ---------- 연습 3: ACK 대기 ---------- */
typedef enum { CFG_PENDING, CFG_ACK, CFG_NAK, CFG_TIMEOUT } cfg_result;
typedef struct { uint8_t want_cls, want_id; cfg_result res; uint32_t others; } ack_wait;
static void ack_cb(void *ctx, uint8_t cls, uint8_t id, const uint8_t *pl, uint16_t len) {
    ack_wait *w = ctx;
    if (cls == 0x05 && (id == 0x01 || id == 0x00) && len == 2 && pl[0] == w->want_cls && pl[1] == w->want_id) {
        if (w->res == CFG_PENDING) w->res = (id == 0x01) ? CFG_ACK : CFG_NAK;
    } else {
        w->others++;                                               /* NAV-PVT 등은 다른 소비자에게 */
    }
}
/* 스크립트된 수신 스트림(bytes, times)을 먹이며 deadline 까지 기다린다. 실제로는 RX 링에서 꺼내며 같은 일을 한다. */
static cfg_result cfg_wait_ack(ubx_parser *p, ack_wait *w, const uint8_t *b, const uint32_t *t, size_t n,
                               uint32_t t_sent, uint32_t timeout_us) {
    w->res = CFG_PENDING;
    for (size_t i = 0; i < n && w->res == CFG_PENDING; i++) {
        if ((uint32_t)(t[i] - t_sent) > timeout_us) break;         /* deadline 지난 바이트는 이번 대기 몫이 아님 */
        ubx_feed(p, b[i], t[i]);
    }
    return w->res == CFG_PENDING ? CFG_TIMEOUT : w->res;
}
static uint8_t  s3_b[2048];
static uint32_t s3_t[2048];
static size_t   s3_n;
static uint32_t s3_now;
static void s3_put(const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n && s3_n < sizeof s3_b; i++) { s3_now += BYTE_US; s3_b[s3_n] = b[i]; s3_t[s3_n] = s3_now; s3_n++; }
}
static void s3_frame(uint8_t cls, uint8_t id, const uint8_t *pl, uint16_t len) {
    uint8_t f[UBX_MAX_PAYLOAD + UBX_OVERHEAD];
    int n = ubx_encode(cls, id, pl, len, f, sizeof f);
    if (n > 0) s3_put(f, (size_t)n);
}

/* ---------- 연습 4: 링크 진단 ---------- */
typedef enum { DIAG_OK, DIAG_SILENT, DIAG_NO_SYNC, DIAG_NOISY, DIAG_OVERRUN } link_diag_t;
static const char *diag_name(link_diag_t d) {
    static const char *n[] = { "OK", "SILENT", "NO_SYNC", "NOISY", "OVERRUN" };
    return n[d];
}
/* 1초마다 부른다: prev/cur 통계 차분 + 그 사이 받은 바이트 수. 우선순위 = 위에서부터 */
static link_diag_t link_diag(const ubx_stats *prev, const ubx_stats *cur, uint32_t bytes) {
    uint32_t ok = cur->frames_ok - prev->frames_ok;
    uint32_t err = (cur->ck_fail - prev->ck_fail) + (cur->bad_len - prev->bad_len) + (cur->timeout - prev->timeout);
    if (cur->overflow != prev->overflow) return DIAG_OVERRUN;      /* 바이트를 이미 잃었다: 먼저 고칠 것 */
    if (bytes == 0) return DIAG_SILENT;                            /* 선, 전원, TX/RX 뒤바뀜, 포트 출력 설정 */
    if (ok == 0) return DIAG_NO_SYNC;                              /* baud 불일치, NMEA 만 나옴, 다른 프로토콜 */
    if (err * 100u > (ok + err)) return DIAG_NOISY;                /* 오류 > 1%: 노이즈, baud 오차, 접지 */
    return DIAG_OK;
}

int main(void) {
    static ubx_parser P;

    /* 연습 1 */
    {
        uint32_t acc_plain = 0, acc_tab = 0;
        static ubx_parser A, B;
        ubx_init(&A, true, 0, count_cb, &acc_plain);
        ubx_init(&B, true, 0, count_cb, &acc_tab);
        B.hdr_ok = hdr_table_ok;
        g_rng = 4242;
        uint8_t f[48];
        const uint32_t N = 400000;
        for (uint32_t i = 0; i < N; i++) {                         /* sync + 랜덤 CLASS/ID + 유효 LEN 0..32 + 랜덤 나머지 */
            uint16_t len = (uint16_t)rnd(33);
            f[0] = UBX_SYNC1; f[1] = UBX_SYNC2;
            for (int k = 2; k < 8 + len; k++) f[k] = (uint8_t)xs32();
            f[4] = (uint8_t)len; f[5] = 0;
            for (int k = 0; k < 8 + len; k++) { ubx_feed(&A, f[k], 0); ubx_feed(&B, f[k], 0); }
        }
        printf("ex1 forced-sync candidates %u: accepted plain=%u (expect ~%.1f), table=%u; table header rejects=%u\n",
               N, acc_plain, N / 65536.0, acc_tab, B.stats.bad_len);
        CHECK(acc_plain > 0 && acc_tab == 0);
        CHECK(B.stats.bad_len >= N - N / 1000);                    /* 99.9% 이상을 4바이트 헤더에서 거름 */
        /* 진짜 메시지는 통과, 길이가 틀린 NAV-PVT 는 거름 */
        uint32_t got = 0;
        ubx_init(&B, true, 0, count_cb, &got);
        B.hdr_ok = hdr_table_ok;
        uint8_t pl[92] = { 0 }, out[128];
        int n = ubx_encode(0x01, 0x07, pl, 92, out, sizeof out);
        for (int k = 0; k < n; k++) ubx_feed(&B, out[k], 0);
        n = ubx_encode(0x01, 0x07, pl, 84, out, sizeof out);
        for (int k = 0; k < n; k++) ubx_feed(&B, out[k], 0);
        n = ubx_encode(0x05, 0x01, pl, 2, out, sizeof out);
        for (int k = 0; k < n; k++) ubx_feed(&B, out[k], 0);
        CHECK(got == 2 && B.stats.bad_len == 1);
    }

    /* 연습 2 */
    {
        g_rng = 2;
        int mismatch = 0;
        uint32_t total = 0;
        uint8_t r[24];
        for (int fr = 0; fr < 60; fr++) {
            size_t n = 8 + rnd(17);
            for (size_t i = 0; i < n; i++) r[i] = (uint8_t)xs32();
            uint32_t pr = predict_undetected(r, n), bf = brute_undetected(r, n);
            total += bf;
            if (pr != bf) mismatch++;
        }
        uint8_t z[20] = { 0 };
        printf("ex2 60 random regions (8..24 B): undetected 2-bit pairs total %u, predictor mismatches %d; all-zero 20 B: %u\n",
               total, mismatch, brute_undetected(z, 20));
        CHECK(mismatch == 0 && total > 0);
        CHECK(brute_undetected(z, 20) == predict_undetected(z, 20) && predict_undetected(z, 20) == 90);  /* bit7 쌍만 */
    }

    /* 연습 3: CFG-RATE(0x06 0x08) 를 보냈다고 치고 ACK 를 기다린다 */
    {
        ack_wait w = { 0x06, 0x08, CFG_PENDING, 0 };
        uint8_t pvt[92] = { 0 }, a_other[2] = { 0x06, 0x01 }, a_ours[2] = { 0x06, 0x08 };
        const uint8_t junk[7] = { 0xB5, 0x62, 0x05, 0x01, 0x40, 0x00, 0x13 };   /* 가짜 sync: LEN 64 */
        for (int sc = 0; sc < 4; sc++) {
            s3_n = 0; s3_now = 1000;
            uint32_t t_sent = s3_now;
            s3_frame(0x01, 0x07, pvt, 92);                          /* 주기 메시지 */
            s3_put(junk, 7);
            s3_now += 3000;                                         /* 가짜 프레임 → 타임아웃으로 정리 */
            s3_frame(0x05, 0x01, a_other, 2);                       /* 다른 메시지에 대한 ACK — 무시해야 */
            if (sc == 0) s3_frame(0x05, 0x01, a_ours, 2);
            if (sc == 1) s3_frame(0x05, 0x00, a_ours, 2);
            if (sc == 3) { s3_now += 600000; s3_frame(0x05, 0x01, a_ours, 2); }   /* 너무 늦은 ACK */
            s3_frame(0x01, 0x07, pvt, 92);
            ubx_init(&P, true, 1000, ack_cb, &w);
            w.others = 0;
            cfg_result r = cfg_wait_ack(&P, &w, s3_b, s3_t, s3_n, t_sent, 500000);
            const cfg_result want[4] = { CFG_ACK, CFG_NAK, CFG_TIMEOUT, CFG_TIMEOUT };
            printf("ex3 scenario %d: result=%d others=%u timeout=%u\n", sc, (int)r, w.others, P.stats.timeout);
            CHECK(r == want[sc] && w.others >= 2);
        }
    }

    /* 연습 4 */
    {
        ubx_stats prev, cur;
        uint32_t frames = 0;
        uint8_t pl[28], out[64];
        /* (a) 정상: NAV-POSLLH 10개 */
        ubx_init(&P, true, 1000, count_cb, &frames);
        prev = P.stats; uint32_t bytes = 0, t = 0;
        for (int i = 0; i < 10; i++) {
            for (int k = 0; k < 28; k++) pl[k] = (uint8_t)(i + k);
            int n = ubx_encode(0x01, 0x02, pl, 28, out, sizeof out);
            for (int k = 0; k < n; k++) { t += BYTE_US; ubx_feed(&P, out[k], t); bytes++; }
        }
        cur = P.stats;
        link_diag_t d_ok = link_diag(&prev, &cur, bytes);
        /* (b) 노이즈: 같은 10개 중 2개에 비트 오류 */
        prev = P.stats; bytes = 0;
        for (int i = 0; i < 10; i++) {
            int n = ubx_encode(0x01, 0x02, pl, 28, out, sizeof out);
            if (i % 5 == 0) out[10] ^= 0x04;
            for (int k = 0; k < n; k++) { t += BYTE_US; ubx_feed(&P, out[k], t); bytes++; }
        }
        cur = P.stats;
        link_diag_t d_noisy = link_diag(&prev, &cur, bytes);
        /* (c) NMEA 문장만 (수신기 출력이 NMEA 로 설정됨) */
        const char *nmea = "$GNGGA,092725.00,4717.11399,N,00833.91590,E,1,08,1.01,499.6,M,48.0,M,,*5B\r\n";
        prev = P.stats; bytes = 0;
        for (int rep = 0; rep < 5; rep++)
            for (const char *c = nmea; *c; c++) { t += BYTE_US; ubx_feed(&P, (uint8_t)*c, t); bytes++; }
        cur = P.stats;
        link_diag_t d_nmea = link_diag(&prev, &cur, bytes);
        /* (d) 아무것도 안 옴, (e) 링 overflow 가 늘었음 */
        prev = P.stats; cur = P.stats;
        link_diag_t d_silent = link_diag(&prev, &cur, 0);
        cur.overflow += 3;
        link_diag_t d_ovf = link_diag(&prev, &cur, 500);
        printf("ex4 diag: ok=%s noisy=%s nmea=%s silent=%s overflow=%s (nmea sync_skip so far=%u)\n",
               diag_name(d_ok), diag_name(d_noisy), diag_name(d_nmea), diag_name(d_silent), diag_name(d_ovf),
               (unsigned)P.stats.sync_skip);
        CHECK(d_ok == DIAG_OK && d_noisy == DIAG_NOISY && d_nmea == DIAG_NO_SYNC);
        CHECK(d_silent == DIAG_SILENT && d_ovf == DIAG_OVERRUN);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
