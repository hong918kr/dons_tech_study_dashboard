/* COMM-M10 demo_recovery.c — 견고한 UBX 파서 복원율·거짓 수락 측정 (solution.c 의 파서/생성기 사본)
 *
 * 표 1: 손상 종류별 (프레임 10% 에 한 종류만) naive / naive+T / rescan / rescan+T 복원율
 * 표 2: 손상률 0..50% (6종 섞음) 복원율, 거짓 수락, 바이트당 step
 * 표 3: 체크섬 수준 — 2비트 오류 전수(Fletcher-8x2 vs CRC-16/CCITT-FALSE), 랜덤 버스트, 랜덤 쓰레기
 *
 * 빌드: cc -std=c11 -Wall -Wextra -O2 demo_recovery.c -o demo_recovery && ./demo_recovery
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
    /* TODO A1: A += b, B += A (둘 다 8비트에서 자연스럽게 넘침) */
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
    /* TODO A2: 검사 먼저 (len > MAX, cap 부족), 그다음 sync·헤더(LEN little-endian)·PAYLOAD·체크섬 */
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
    /* TODO A3: sync 찾기 (B5 B5 62 도 잡을 것), 헤더, 길이 가드, PAYLOAD, 체크섬 비교 */
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
        if (p->len > UBX_MAX_PAYLOAD) { p->stats.bad_len++; p->st = ST_SYNC1; return EV_ERR_LEN; }
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
            /* TODO B1: in-buffer rescan — 남은 부분을 뒤로 밀고 raw[] 를 앞에 복사, 처음부터 */
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
    /* TODO B2a: SYNC2(B5 하나만 본 상태)는 그냥 SYNC1 로. 프레임 중간이면 timeout++ 후 raw[] 재검색 반복 */
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
    /* TODO B2b: 프레임 중간인데 직전 바이트와의 간격 > timeout_us 면 먼저 ubx_flush (랩 안전한 unsigned 뺄셈) */
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

/* ---------------- Part C: 타이머 경로와 ISR 링 버퍼 ---------------- */

/* 주기 타이머 / UART receiver-timeout 인터럽트 뒤에 부른다: 바이트가 안 와도 갇힌 프레임을 꺼낸다. */
static int ubx_tick(ubx_parser *p, uint32_t now_us) {
    /* TODO C1: 타임아웃 켜짐 && 프레임 중간 && now - last_t > T → ubx_flush */
    if (p->timeout_us == 0 || !p->have_t || p->st == ST_SYNC1) return 0;
    if ((uint32_t)(now_us - p->last_t) <= p->timeout_us) return 0;
    p->steps = 0;
    int frames = ubx_flush(p);
    if (p->steps > p->max_steps) p->max_steps = p->steps;
    p->steps_total += p->steps;
    return frames;
}

#define RX_RING_SIZE 64u                        /* 2의 거듭제곱 */
typedef struct {
    uint8_t  b[RX_RING_SIZE];
    uint32_t t[RX_RING_SIZE];                   /* 바이트마다 수신 시각 (타이머 캡처) */
    volatile uint32_t head;                     /* ISR 만 쓴다 */
    volatile uint32_t tail;                     /* 메인 루프만 쓴다 */
    uint32_t overflow;                          /* 가득 차서 버린 바이트 (ISR 만 쓴다) */
} rx_ring;

/* UART RX ISR 이 부른다: 링에 넣기만. 가득 차면 새 바이트를 버리고 overflow++. */
static bool rx_isr_push(rx_ring *r, uint8_t b, uint32_t t_us) {
    /* TODO C2: 가득 참 = head - tail == SIZE (부호 없는 뺄셈, 인덱스는 & (SIZE-1)) */
    if ((uint32_t)(r->head - r->tail) == RX_RING_SIZE) { r->overflow++; return false; }
    r->b[r->head & (RX_RING_SIZE - 1u)] = b;
    r->t[r->head & (RX_RING_SIZE - 1u)] = t_us;
    r->head++;
    return true;
}

/* 메인 루프가 부른다: 쌓인 바이트를 전부 파서에. */
static int rx_poll(rx_ring *r, ubx_parser *p) {
    int frames = 0;
    while (r->tail != r->head) {
        uint32_t i = r->tail & (RX_RING_SIZE - 1u);
        frames += ubx_feed(p, r->b[i], r->t[i]);
        r->tail++;
    }
    p->stats.overflow = r->overflow;
    return frames;
}

/* bring-up 엔지니어가 읽는 한 줄. 반환: 필요한 글자 수 (>= cap 이면 잘림 — snprintf 와 같은 규칙). */
static int ubx_stats_line(const ubx_parser *p, char *buf, size_t cap) {
    const ubx_stats *s = &p->stats;
    return snprintf(buf, cap, "ubx ok=%u skip=%u len=%u ck=%u to=%u ovf=%u rescan=%u",
                    (unsigned)s->frames_ok, (unsigned)s->sync_skip, (unsigned)s->bad_len, (unsigned)s->ck_fail,
                    (unsigned)s->timeout, (unsigned)s->overflow, (unsigned)s->rescans);
}

#define BYTE_US 87u                             /* 115200 baud 8N1 ≈ 86.8 µs/바이트 */
static uint32_t g_rng = 1;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }
static uint32_t rnd(uint32_t n) { return xs32() % n; }

/* ---------- Part C 스트림 생성기 (demo_recovery.c 와 같은 모델) ---------- */
#define IDLE_US       20000u   /* 에포크(측위 주기) 사이 침묵 */
#define TRUNC_GAP_US  3000u    /* 송신 쪽 리셋: 잘린 프레임 뒤 침묵 */
#define T_US          1000u    /* inter-byte timeout */
#define LATE_US       5000u
enum { K_FLIP, K_DROP, K_INSERT, K_TRUNC, K_BURST, K_NOISE, K_COUNT };
#define S_CAP 8000000u
#define F_CAP 100000u
typedef struct { uint32_t off, t_end; uint16_t n; uint8_t intact, got; } sent_t;
static uint8_t  s_b[S_CAP];
static uint32_t s_t[S_CAP];
static sent_t   s_f[F_CAP];
static size_t   s_n, s_nf;
static uint32_t s_now;
static bool     s_full;

static void emit(uint8_t b) {
    if (s_n < S_CAP) { s_b[s_n] = b; s_t[s_n] = s_now; s_n++; } else s_full = true;
    s_now += BYTE_US;
}
static void emit_noise(uint32_t n) {           /* 쓰레기: 1/4 확률로 가짜 sync "B5 62" */
    for (uint32_t k = 0; k < n; k++) {
        if (k + 1 < n && rnd(4) == 0) { emit(UBX_SYNC1); emit(UBX_SYNC2); k++; }
        else emit((uint8_t)xs32());
    }
}
static void gen_stream(uint32_t seed, uint32_t nframes, uint32_t pct, unsigned mask) {
    static const uint8_t cls_tab[5] = { 0x01, 0x02, 0x0A, 0x0D, 0x05 };
    uint8_t pl[UBX_MAX_PAYLOAD], f[UBX_MAX_PAYLOAD + UBX_OVERHEAD + 1], orig[UBX_MAX_PAYLOAD + UBX_OVERHEAD];
    unsigned kinds[K_COUNT], nk = 0;
    for (unsigned k = 0; k < K_COUNT; k++) if (mask & (1u << k)) kinds[nk++] = k;
    g_rng = seed; s_n = s_nf = 0; s_now = 0; s_full = false;
    uint32_t left = 0;
    for (uint32_t k = 0; k < nframes && k < F_CAP; k++) {
        if (left == 0) { s_now += IDLE_US; left = 1 + rnd(8); }   /* 에포크마다 1..8 프레임 연달아 */
        left--;
        uint16_t len = (uint16_t)(4 + rnd(97));                   /* 4..100 */
        for (uint16_t i = 0; i < len; i++) pl[i] = (uint8_t)xs32();
        pl[0] = (uint8_t)k; pl[1] = (uint8_t)(k >> 8); pl[2] = (uint8_t)(k >> 16); pl[3] = (uint8_t)(k >> 24);
        if (len >= 10 && rnd(10) < 3) {                           /* 30%: PAYLOAD 안에 그럴듯한 가짜 헤더 */
            uint32_t o = 4 + rnd(len - 9u);
            pl[o] = UBX_SYNC1; pl[o + 1] = UBX_SYNC2; pl[o + 2] = cls_tab[rnd(5)];
            pl[o + 3] = (uint8_t)rnd(0x40); pl[o + 4] = (uint8_t)rnd(32); pl[o + 5] = 0;
        }
        int n = ubx_encode(cls_tab[rnd(5)], (uint8_t)rnd(0x40), pl, len, f, sizeof f);
        if (n <= 0) { memset(&s_f[k], 0, sizeof s_f[k]); s_nf++; continue; }   /* 하네스 안전장치 */
        int on = n;
        memcpy(orig, f, (size_t)n);
        int kind = -1;
        if (nk && rnd(100) < pct) kind = (int)kinds[rnd(nk)];
        uint32_t gap_after = 0;
        switch (kind) {
        case K_FLIP: f[rnd((uint32_t)n)] ^= (uint8_t)(1u << rnd(8)); break;
        case K_DROP: { uint32_t j = rnd((uint32_t)n); memmove(f + j, f + j + 1, (size_t)n - j - 1); n--; } break;
        case K_INSERT: { uint32_t j = 1 + rnd((uint32_t)n - 1);   /* 프레임 안쪽 (앞/뒤 삽입은 그냥 쓰레기) */ memmove(f + j + 1, f + j, (size_t)n - j); f[j] = (uint8_t)xs32(); n++; } break;
        case K_TRUNC: n = 1 + (int)rnd((uint32_t)n - 1); gap_after = TRUNC_GAP_US; break;
        case K_BURST: {
            uint32_t L = 2 + rnd(15);
            if (L > (uint32_t)n) L = (uint32_t)n;
            uint32_t j = rnd((uint32_t)n - L + 1);
            for (uint32_t m = 0; m < L; m++) {
                if (m + 1 < L && rnd(4) == 0) { f[j + m] = UBX_SYNC1; f[j + m + 1] = UBX_SYNC2; m++; }
                else f[j + m] = (uint8_t)xs32();
            }
        } break;
        case K_NOISE: emit_noise(1 + rnd(32)); break;             /* 프레임 앞 쓰레기 (프레임은 멀쩡) */
        default: break;
        }
        s_f[k].off = (uint32_t)s_n;
        s_f[k].n = (uint16_t)n;
        s_f[k].intact = (n == on && memcmp(f, orig, (size_t)n) == 0);
        s_f[k].got = 0;
        for (int j = 0; j < n; j++) emit(f[j]);
        s_f[k].t_end = s_now - BYTE_US;
        s_now += gap_after;
        s_nf++;
    }
}

typedef struct { uint32_t now, recovered, false_acc, dup, late, max_lat; } match_t;
static void match_cb(void *ctx, uint8_t cls, uint8_t id, const uint8_t *pl, uint16_t len) {
    match_t *m = ctx;
    if (len >= 4) {
        uint32_t seq = (uint32_t)pl[0] | ((uint32_t)pl[1] << 8) | ((uint32_t)pl[2] << 16) | ((uint32_t)pl[3] << 24);
        if (seq < s_nf && s_f[seq].intact) {
            sent_t *s = &s_f[seq];
            const uint8_t *f = s_b + s->off;
            if (s->n == len + UBX_OVERHEAD && f[2] == cls && f[3] == id && memcmp(f + 6, pl, len) == 0) {
                if (s->got) { m->dup++; return; }
                s->got = 1;
                m->recovered++;
                uint32_t lat = m->now - s->t_end;
                if (lat > m->max_lat) m->max_lat = lat;
                if (lat > LATE_US) m->late++;
                return;
            }
        }
    }
    m->false_acc++;                             /* 보낸 적 없는(또는 손상된) 프레임을 받아들였다 */
}
static uint32_t count_intact(void) {
    uint32_t c = 0;
    for (size_t k = 0; k < s_nf; k++) c += s_f[k].intact;
    return c;
}
static void run_cfg(bool rescan, uint32_t T, match_t *m, ubx_parser *p) {
    memset(m, 0, sizeof *m);
    for (size_t k = 0; k < s_nf; k++) s_f[k].got = 0;
    ubx_init(p, rescan, T, match_cb, m);
    for (size_t i = 0; i < s_n; i++) {
        if (T && i > 0 && s_t[i] - s_t[i - 1] > T) { m->now = s_t[i - 1] + T + 1; ubx_tick(p, m->now); }
        m->now = s_t[i];
        ubx_feed(p, s_b[i], s_t[i]);
    }
    if (T && s_n) { m->now = s_t[s_n - 1] + T + 1; ubx_tick(p, m->now); }
}



static uint16_t crc16_ccitt(const uint8_t *p, size_t n) {          /* CRC-16/CCITT-FALSE, 비교용 */
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)(p[i] << 8);
        for (int k = 0; k < 8; k++) c = (c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x1021u) : (uint16_t)(c << 1);
    }
    return c;
}
static void count_cb(void *ctx, uint8_t cls, uint8_t id, const uint8_t *pl, uint16_t len) {
    (void)cls; (void)id; (void)pl; (void)len; (*(uint32_t *)ctx)++;
}

static const char *cfg_name[4] = { "naive", "naive+T", "rescan", "rescan+T" };

static void table_row(const char *label, uint32_t nframes, uint32_t pct, unsigned mask, uint32_t seed, bool rate_table) {
    static ubx_parser P;
    gen_stream(seed, nframes, pct, mask);
    if (s_full) { printf("stream buffer full!\n"); return; }
    uint32_t intact = count_intact();
    match_t m[4]; ubx_stats st[4]; uint32_t maxs[4]; uint64_t tot[4];
    for (int c = 0; c < 4; c++) { run_cfg(c >= 2, (c & 1) ? T_US : 0, &m[c], &P); st[c] = P.stats; maxs[c] = P.max_steps; tot[c] = P.steps_total; }
    if (!rate_table)
        printf("| %s | %u | %.2f%% | %.2f%% | %.3f%% | %.3f%% | %u / %u | %u / %u | %.1f / %.1f |\n", label, intact,
               100.0 * m[0].recovered / intact, 100.0 * m[1].recovered / intact,
               100.0 * m[2].recovered / intact, 100.0 * m[3].recovered / intact,
               m[2].false_acc, m[3].false_acc, m[2].late, m[3].late, m[2].max_lat / 1000.0, m[3].max_lat / 1000.0);
    else
        printf("| %s | %u | %.2f%% | %.2f%% | %.3f%% | %.3f%% | %u/%u/%u/%u | %.2f | %u |\n", label, intact,
               100.0 * m[0].recovered / intact, 100.0 * m[1].recovered / intact,
               100.0 * m[2].recovered / intact, 100.0 * m[3].recovered / intact,
               m[0].false_acc, m[1].false_acc, m[2].false_acc, m[3].false_acc,
               (double)tot[3] / (double)s_n, maxs[3]);
    (void)st; (void)cfg_name;
}

int main(void) {
    const uint32_t NF = 50000;
    printf("모델: 115200 8N1 (87 us/byte), 에포크마다 1..8 프레임 연속 + 20 ms 침묵, PAYLOAD 4..100 B (30%% 에 가짜 헤더),\n");
    printf("      T = %u us, late = 전달 지연 > %u us, 프레임 %u 개, seed 2026\n\n", T_US, LATE_US, NF);

    printf("표 1: 손상 종류별 (프레임의 10%% 에 그 종류만)\n");
    printf("| 손상 | intact | naive | naive+T | rescan | rescan+T | 거짓수락 rescan / +T | late rescan / +T | 최대지연 ms rescan / +T |\n");
    printf("|---|---|---|---|---|---|---|---|---|\n");
    static const char *kname[K_COUNT] = { "bit flip", "byte drop", "byte insert", "truncate+3ms", "burst(가짜 sync)", "noise 사이 쓰레기" };
    for (int k = 0; k < K_COUNT; k++) table_row(kname[k], NF, 10, 1u << k, 2026, false);
    table_row("6종 섞음", NF, 10, (1u << K_COUNT) - 1, 2026, false);

    printf("\n표 2: 손상률 (6종 섞음)\n");
    printf("| 손상률 | intact | naive | naive+T | rescan | rescan+T | 거짓수락 n/nT/r/rT | step/byte (rescan+T) | 최악 step/호출 |\n");
    printf("|---|---|---|---|---|---|---|---|---|\n");
    static const uint32_t pcts[6] = { 0, 1, 5, 10, 20, 50 };
    char lab[16];
    for (int i = 0; i < 6; i++) { snprintf(lab, sizeof lab, "%u%%", pcts[i]); table_row(lab, NF, pcts[i], (1u << K_COUNT) - 1, 2026, true); }

    /* 표 3a: 2비트 오류 전수 — CLASS..PAYLOAD(16 B) = 20바이트 = 160비트, C(160,2) = 12720 쌍, 프레임 1000개 */
    {
        g_rng = 99;
        uint64_t pairs = 0, und_f = 0, und_c = 0, und_msb = 0;
        uint8_t r[20];
        for (int fr = 0; fr < 1000; fr++) {
            for (int i = 0; i < 20; i++) r[i] = (uint8_t)xs32();
            r[2] = 16; r[3] = 0;
            uint8_t a0, b0; ubx_checksum(r, 20, &a0, &b0);
            uint16_t c0 = crc16_ccitt(r, 20);
            for (int x = 0; x < 160; x++) for (int y = x + 1; y < 160; y++) {
                r[x / 8] ^= (uint8_t)(1u << (x % 8)); r[y / 8] ^= (uint8_t)(1u << (y % 8));
                uint8_t a, b; ubx_checksum(r, 20, &a, &b);
                pairs++;
                if (a == a0 && b == b0) { und_f++; if (x % 8 == 7 && y % 8 == 7) und_msb++; }
                if (crc16_ccitt(r, 20) == c0) und_c++;
                r[x / 8] ^= (uint8_t)(1u << (x % 8)); r[y / 8] ^= (uint8_t)(1u << (y % 8));
            }
        }
        printf("\n표 3a: 2비트 오류 전수 (20바이트 영역, 프레임 1000개 x 12720 쌍)\n");
        printf("  쌍 %llu: Fletcher 미검출 %llu (%.4f%%, 그중 bit7 쌍 %llu), CRC-16/CCITT-FALSE 미검출 %llu\n",
               (unsigned long long)pairs, (unsigned long long)und_f, 100.0 * und_f / pairs,
               (unsigned long long)und_msb, (unsigned long long)und_c);
    }
    /* 표 3b: 랜덤 버스트 (연속 1..16 바이트를 랜덤 값으로) — 체크섬 수준 */
    {
        g_rng = 7;
        uint64_t trials = 0, und_f = 0, und_c = 0;
        uint8_t r[68], o[68];
        for (int t = 0; t < 2000000; t++) {
            for (int i = 0; i < 68; i++) o[i] = r[i] = (uint8_t)xs32();
            uint8_t a0, b0; ubx_checksum(r, 68, &a0, &b0);
            uint16_t c0 = crc16_ccitt(r, 68);
            uint32_t L = 1 + rnd(16), j = rnd(68 - L + 1);
            for (uint32_t k = 0; k < L; k++) r[j + k] = (uint8_t)xs32();
            if (memcmp(r, o, 68) == 0) continue;
            trials++;
            uint8_t a, b; ubx_checksum(r, 68, &a, &b);
            und_f += (a == a0 && b == b0);
            und_c += (crc16_ccitt(r, 68) == c0);
        }
        printf("표 3b: 랜덤 버스트 1..16 B (68바이트 영역) %llu 회: Fletcher 미검출 %llu (%.2e), CRC-16 미검출 %llu (%.2e), 2^-16 = %.2e\n",
               (unsigned long long)trials, (unsigned long long)und_f, (double)und_f / trials,
               (unsigned long long)und_c, (double)und_c / trials, 1.0 / 65536);
    }
    /* 표 3c: 랜덤 쓰레기 → rescan 파서. 그리고 sync + 유효 LEN 을 강제한 랜덤 후보 */
    {
        static ubx_parser P;
        uint32_t acc = 0;
        ubx_init(&P, true, 0, count_cb, &acc);
        g_rng = 31337;
        const uint32_t NB = 50000000u;
        for (uint32_t i = 0; i < NB; i++) ubx_feed(&P, (uint8_t)xs32(), i * BYTE_US);
        printf("표 3c: 랜덤 바이트 %u 개 → rescan 파서: 수락 %u, bad_len %u, ck_fail %u, rescans %u, step/byte %.3f, 최악 %u\n",
               NB, acc, P.stats.bad_len, P.stats.ck_fail, P.stats.rescans, (double)P.steps_total / NB, P.max_steps);
        uint32_t acc2 = 0, N2 = 4000000u;
        ubx_init(&P, false, 0, count_cb, &acc2);
        uint8_t f[UBX_MAX_PAYLOAD + 8];
        for (uint32_t i = 0; i < N2; i++) {
            uint16_t len = (uint16_t)rnd(33);
            f[0] = UBX_SYNC1; f[1] = UBX_SYNC2;
            for (int k = 2; k < 8 + len; k++) f[k] = (uint8_t)xs32();
            f[4] = (uint8_t)len; f[5] = 0;
            for (int k = 0; k < 8 + len; k++) ubx_feed(&P, f[k], 0);
        }
        printf("      sync+유효 LEN 강제 랜덤 후보 %u 개: 수락 %u (기대 %.1f = N/65536)\n", N2, acc2, N2 / 65536.0);
    }
    return 0;
}
