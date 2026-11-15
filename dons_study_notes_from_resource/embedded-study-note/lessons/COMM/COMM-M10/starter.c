/* COMM-M10 견고한 패킷 파서: 재동기화·타임아웃·통계
 *
 * 포맷: u-blox UBX 프레임 (sync 2바이트 + 헤더 4바이트 + PAYLOAD + 체크섬 2바이트)
 *   [0xB5][0x62][CLASS][ID][LEN_L][LEN_H][PAYLOAD × LEN][CK_A][CK_B]
 *   LEN  = PAYLOAD 바이트 수, little-endian 16비트. 이 파서의 길이 가드: LEN <= UBX_MAX_PAYLOAD(256) (우리 정의).
 *   CK   = 8비트 Fletcher (mod 256): CLASS..PAYLOAD 의 각 바이트 b 에 대해 A += b; B += A;  (CRC 아님!)
 *   확인값: UBX-MON-VER poll = B5 62 0A 04 00 00 0E 34 (코드 + 파이썬으로 따로 계산해 일치)
 *
 * Part A: ubx_checksum, ubx_encode (용량 검사), ubx_step (바이트 단위 상태 머신, "B5 B5 62" 처리, 길이 가드)
 * Part B: ubx_run 의 in-buffer rescan (오류 시 가짜 sync 다음 바이트부터 이미 먹은 바이트를 다시 훑기),
 *         ubx_feed 의 inter-byte timeout (타임스탬프 간격 > T 이면 미완성 프레임 정리 = ubx_flush)
 * Part C: 타이머 경로 ubx_tick, UART RX ISR 링 버퍼(바이트 + 타임스탬프, overflow 카운터), 통계 로그 줄,
 *         고정 시드 손상 스트림에서 naive / rescan × 타임아웃 유무 복원율 비교
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
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
    (void)p; (void)n;
    *ck_a = 0;
    *ck_b = 0;
}

/* 프레임 하나를 out 에 쓴다. 반환: 쓴 바이트 수(len + 8) 또는 UBX_ERR_LEN / UBX_ERR_CAP (out 은 건드리지 않음). */
static int ubx_encode(uint8_t cls, uint8_t id, const uint8_t *pl, size_t len, uint8_t *out, size_t cap) {
    /* TODO A2: 검사 먼저 (len > MAX, cap 부족), 그다음 sync·헤더(LEN little-endian)·PAYLOAD·체크섬 */
    (void)cls; (void)id; (void)pl; (void)len; (void)out; (void)cap;
    return UBX_ERR_CAP;
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
    (void)b;
    return EV_NONE;
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
        }
    }
    return frames;
}

/* 타임아웃: 진행 중이던 (미완성) 프레임을 버린다. rescan 모드면 그 안의 바이트를 다시 훑고,
 * 다시 훑다가 또 미완성으로 끝나면 그것도 버리고 다시 — 매번 짧아지므로 끝난다. 반환: 전달한 프레임 수. */
static int ubx_flush(ubx_parser *p) {
    int frames = 0;
    /* TODO B2a: SYNC2(B5 하나만 본 상태)는 그냥 SYNC1 로. 프레임 중간이면 timeout++ 후 raw[] 재검색 반복 */
    p->st = ST_SYNC1;
    return frames;
}

/* 수신 바이트 하나 (t_us = 수신 시각, µs). 반환: 이번 호출에서 전달한 프레임 수. */
static int ubx_feed(ubx_parser *p, uint8_t b, uint32_t t_us) {
    int frames = 0;
    p->steps = 0;
    /* TODO B2b: 프레임 중간인데 직전 바이트와의 간격 > timeout_us 면 먼저 ubx_flush (랩 안전한 unsigned 뺄셈) */
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
    (void)p; (void)now_us; (void)ubx_flush;   /* 힌트: ubx_flush 를 부른다 */
    return 0;
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
    (void)r; (void)b; (void)t_us;
    return false;
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

/* ======================= 테스트 하네스 ======================= */

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 1;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }
static uint32_t rnd(uint32_t n) { return xs32() % n; }

/* 단위 테스트용: 받은 프레임 기록 */
#define CAP_N 16
typedef struct { int n; uint8_t cls[CAP_N], id[CAP_N]; uint16_t len[CAP_N]; uint8_t pl[CAP_N][UBX_MAX_PAYLOAD]; } cap_t;
static void cap_cb(void *ctx, uint8_t cls, uint8_t id, const uint8_t *pl, uint16_t len) {
    cap_t *c = ctx;
    if (c->n < CAP_N) {
        c->cls[c->n] = cls; c->id[c->n] = id; c->len[c->n] = len;
        memcpy(c->pl[c->n], pl, len);
    }
    c->n++;
}
static bool cap_has(const cap_t *c, uint8_t id) {
    for (int i = 0; i < c->n && i < CAP_N; i++) if (c->id[i] == id) return true;
    return false;
}

/* 단위 테스트용 바이트 먹이기: 87 µs 간격 (115200 baud 8N1 ≈ 86.8 µs/바이트) */
#define BYTE_US 87u
static uint32_t g_now;
static int feed_bytes(ubx_parser *p, const uint8_t *b, size_t n) {
    int f = 0;
    for (size_t i = 0; i < n; i++) { g_now += BYTE_US; f += ubx_feed(p, b[i], g_now); }
    return f;
}
static int feed_zeros(ubx_parser *p, size_t n) {
    uint8_t z = 0; int f = 0;
    for (size_t i = 0; i < n; i++) f += feed_bytes(p, &z, 1);
    return f;
}
/* 길이 len, PAYLOAD = start, start+1, ... 인 프레임 */
static int mk(uint8_t cls, uint8_t id, uint16_t len, uint8_t start, uint8_t *out) {
    uint8_t pl[UBX_MAX_PAYLOAD];
    for (uint16_t i = 0; i < len; i++) pl[i] = (uint8_t)(start + i);
    int n = ubx_encode(cls, id, pl, len, out, UBX_MAX_PAYLOAD + UBX_OVERHEAD);
    return n > 0 ? n : 0;                       /* 인코더가 실패해도 하네스는 안전하게 */
}

/* 왕복 테스트: 기대 길이를 순서대로 */
typedef struct { int next, bad; } rt_t;
static uint8_t rt_byte(uint16_t len, uint16_t i) { return (i % 3 == 0) ? 0xB5 : (i % 3 == 1) ? 0x62 : (uint8_t)(len + i); }
static void rt_cb(void *ctx, uint8_t cls, uint8_t id, const uint8_t *pl, uint16_t len) {
    rt_t *r = ctx;
    if (cls != 0x01 || id != (uint8_t)r->next || len != r->next) r->bad++;
    else for (uint16_t i = 0; i < len; i++) if (pl[i] != rt_byte(len, i)) { r->bad++; break; }
    r->next++;
}

/* ---------- Part C 스트림 생성기 (demo_recovery.c 와 같은 모델) ---------- */
#define IDLE_US       20000u   /* 에포크(측위 주기) 사이 침묵 */
#define TRUNC_GAP_US  3000u    /* 송신 쪽 리셋: 잘린 프레임 뒤 침묵 */
#define T_US          1000u    /* inter-byte timeout */
#define LATE_US       5000u
enum { K_FLIP, K_DROP, K_INSERT, K_TRUNC, K_BURST, K_NOISE, K_COUNT };
#define S_CAP 400000u
#define F_CAP 4000u
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

int main(void) {
    static ubx_parser P;
    static cap_t C;
    uint8_t out[UBX_MAX_PAYLOAD + UBX_OVERHEAD + 8];

    /* ---------------- Part A ---------------- */
    static const uint8_t monver[8] = { 0xB5, 0x62, 0x0A, 0x04, 0x00, 0x00, 0x0E, 0x34 };
    static const uint8_t navpvt[8] = { 0xB5, 0x62, 0x01, 0x07, 0x00, 0x00, 0x08, 0x19 };
    static const uint8_t ex4[12]   = { 0xB5, 0x62, 0x01, 0x02, 0x04, 0x00, 0x11, 0x22, 0x33, 0x44, 0xB1, 0x82 };
    {
        uint8_t a = 1, b = 1;
        ubx_checksum(monver + 2, 4, &a, &b);
        CHECK(a == 0x0E && b == 0x34);                                  /* UBX-MON-VER poll */
        CHECK(ubx_encode(0x0A, 0x04, NULL, 0, out, sizeof out) == 8 && memcmp(out, monver, 8) == 0);
        CHECK(ubx_encode(0x01, 0x07, NULL, 0, out, sizeof out) == 8 && memcmp(out, navpvt, 8) == 0);
        const uint8_t pl4[4] = { 0x11, 0x22, 0x33, 0x44 };
        CHECK(ubx_encode(0x01, 0x02, pl4, 4, out, sizeof out) == 12 && memcmp(out, ex4, 12) == 0);
        out[0] = 0x5A;
        CHECK(ubx_encode(0x01, 0x02, pl4, 4, out, 11) == UBX_ERR_CAP && out[0] == 0x5A);   /* 용량 경계 */
        CHECK(ubx_encode(0x01, 0x02, pl4, 4, out, 12) == 12);
        CHECK(ubx_encode(0x01, 0x02, out, UBX_MAX_PAYLOAD + 1, out, sizeof out) == UBX_ERR_LEN);
    }
    {   /* MON-VER poll 을 한 바이트씩: 마지막 바이트에서만 1 */
        memset(&C, 0, sizeof C);
        ubx_init(&P, true, 0, cap_cb, &C);
        int r[8];
        for (int i = 0; i < 8; i++) r[i] = feed_bytes(&P, monver + i, 1);
        CHECK(r[0] == 0 && r[1] == 0 && r[2] == 0 && r[3] == 0 && r[4] == 0 && r[5] == 0 && r[6] == 0 && r[7] == 1);
        CHECK(C.n == 1 && C.cls[0] == 0x0A && C.id[0] == 0x04 && C.len[0] == 0);
        feed_bytes(&P, ex4, 12);
        CHECK(C.n == 2 && C.len[1] == 4 && memcmp(C.pl[1], ex4 + 6, 4) == 0);
    }
    for (int mode = 0; mode < 2; mode++) {   /* LEN 0..256 전수 왕복, PAYLOAD 에 B5 62 가득 — naive 와 rescan 둘 다 */
        rt_t rt = { 0, 0 };
        ubx_init(&P, mode == 1, T_US, rt_cb, &rt);
        uint8_t pl[UBX_MAX_PAYLOAD];
        int frames = 0;
        for (uint16_t len = 0; len <= UBX_MAX_PAYLOAD; len++) {
            for (uint16_t i = 0; i < len; i++) pl[i] = rt_byte(len, i);
            int n = ubx_encode(0x01, (uint8_t)len, pl, len, out, sizeof out);
            if (n > 0) frames += feed_bytes(&P, out, (size_t)n);
        }
        CHECK(frames == 257 && rt.next == 257 && rt.bad == 0);
        CHECK(P.stats.frames_ok == 257 && P.stats.ck_fail == 0 && P.stats.bad_len == 0 && P.stats.sync_skip == 0);
    }
    {   /* "B5 B5 62": 앞의 B5 는 쓰레기, 두 번째 B5 가 진짜 sync */
        memset(&C, 0, sizeof C);
        ubx_init(&P, false, 0, cap_cb, &C);
        const uint8_t junk[3] = { 0x00, 0xB5, 0x13 };
        feed_bytes(&P, junk, 3);                 /* 00 → skip 1, B5 13 → skip 2 */
        uint8_t b5 = 0xB5;
        feed_bytes(&P, &b5, 1);
        feed_bytes(&P, monver, 8);
        CHECK(C.n == 1 && C.id[0] == 0x04);
        CHECK(P.stats.sync_skip == 4);
    }

    /* ---------------- Part B: 오류 주입 ---------------- */
    uint8_t X[UBX_MAX_PAYLOAD + 16] = { 0 }, Y[UBX_MAX_PAYLOAD + 16] = { 0 }, Z[UBX_MAX_PAYLOAD + 16] = { 0 };
    int xn = mk(0x01, 0x11, 8, 0x40, X);        /* X: 16바이트 */
    int yn = mk(0x01, 0x22, 4, 0x50, Y);        /* Y: 12바이트 */
    int zn = mk(0x01, 0x33, 6, 0x60, Z);        /* Z: 14바이트 */
    {   /* B1: LEN > MAX → bad_len, 바로 뒤 Y 는 둘 다 받는다 (길이 가드는 4바이트 만에 끊는다) */
        const uint8_t bad[6] = { 0xB5, 0x62, 0x01, 0x02, 0x01, 0x01 };   /* LEN 0x0101 = 257 */
        for (int mode = 0; mode < 2; mode++) {
            memset(&C, 0, sizeof C);
            ubx_init(&P, mode == 1, 0, cap_cb, &C);
            feed_bytes(&P, bad, 6);
            feed_bytes(&P, Y, (size_t)yn);
            CHECK(P.stats.bad_len == 1 && C.n == 1 && C.id[0] == 0x22);
        }
    }
    {   /* B2: CLASS·ID·PAYLOAD·CK 의 모든 단일 비트 뒤집기 (12바이트 × 8 = 96) → ck_fail 1, Y 받음 */
        int bad_naive = 0, bad_rescan = 0;
        for (int pos = 2; pos < xn; pos++) {
            if (pos == 4 || pos == 5) continue;  /* LEN 은 B3 에서 */
            for (int bit = 0; bit < 8; bit++) {
                for (int mode = 0; mode < 2; mode++) {
                    uint8_t x2[32];
                    memcpy(x2, X, (size_t)xn);
                    x2[pos] ^= (uint8_t)(1u << bit);
                    memset(&C, 0, sizeof C);
                    ubx_init(&P, mode == 1, 0, cap_cb, &C);
                    feed_bytes(&P, x2, (size_t)xn);
                    feed_bytes(&P, Y, (size_t)yn);
                    bool ok = P.stats.ck_fail == 1 && C.n == 1 && C.id[0] == 0x22;
                    if (!ok) { if (mode) bad_rescan++; else bad_naive++; }
                }
            }
        }
        CHECK(bad_naive == 0 && bad_rescan == 0);
    }
    {   /* B3: LEN 바이트 16비트 뒤집기 → naive 는 Y/Z 를 잃는 경우가 있고, rescan 은 16가지 전부 Y, Z 받음 */
        int naive_lost = 0, rescan_lost = 0;
        for (int bit = 0; bit < 16; bit++) {
            for (int mode = 0; mode < 2; mode++) {
                uint8_t x2[32];
                memcpy(x2, X, (size_t)xn);
                x2[4 + bit / 8] ^= (uint8_t)(1u << (bit % 8));
                memset(&C, 0, sizeof C);
                ubx_init(&P, mode == 1, 0, cap_cb, &C);
                feed_bytes(&P, x2, (size_t)xn);
                feed_bytes(&P, Y, (size_t)yn);
                feed_bytes(&P, Z, (size_t)zn);
                feed_zeros(&P, UBX_RAW_MAX);     /* 가짜 긴 프레임을 끝내 줄 꼬리 */
                int lost = !cap_has(&C, 0x22) + !cap_has(&C, 0x33);
                if (mode) rescan_lost += lost; else naive_lost += lost;
            }
        }
        printf("B3 LEN bit flips (16): naive lost %d of 32 Y/Z frames, rescan lost %d\n", naive_lost, rescan_lost);
        CHECK(naive_lost > 0 && rescan_lost == 0);
    }
    /* B4..B7: naive 는 Y 를 잃고 rescan 은 받는 시나리오들 (sc = 앞부분, 그 뒤 Y Z + 0 꼬리) */
    for (int sc = 0; sc < 4; sc++) {
        uint8_t pre[64]; size_t pn = 0;
        if (sc == 0) { memcpy(pre, X, 9); pn = 9; }                          /* B4 잘린 X (앞 9바이트) */
        if (sc == 1 && xn == 16) {                                           /* B5 X 의 PAYLOAD 한 바이트 유실 */
            memcpy(pre, X, (size_t)xn); memmove(pre + 8, pre + 9, (size_t)xn - 9); pn = (size_t)xn - 1;
        }
        if (sc == 2) {                                                       /* B6 쓰레기 버스트: 가짜 sync + 그럴듯한 LEN 64 */
            const uint8_t g[9] = { 0x3C, 0xB5, 0x62, 0x05, 0x01, 0x40, 0x00, 0x77, 0x88 };
            memcpy(pre, g, 9); pn = 9;
        }
        if (sc == 3) {                                                       /* B7 중간 시작: 가짜 헤더가 든 PAYLOAD 꼬리부터 */
            uint8_t pl[24], w[40];
            for (int i = 0; i < 24; i++) pl[i] = (uint8_t)(0x70 + i);
            const uint8_t fake[6] = { 0xB5, 0x62, 0x01, 0x02, 0x20, 0x00 };  /* 가짜 LEN 32 */
            memcpy(pl + 8, fake, 6);
            int wn = ubx_encode(0x02, 0x44, pl, 24, w, sizeof w);
            pn = wn > 14 ? (size_t)wn - (6 + 8) : 0;                         /* 가짜 헤더 위치부터 끝까지 */
            memcpy(pre, w + 6 + 8, pn);
        }
        int got[2];
        uint32_t errs[2];
        for (int mode = 0; mode < 2; mode++) {
            memset(&C, 0, sizeof C);
            ubx_init(&P, mode == 1, 0, cap_cb, &C);
            feed_bytes(&P, pre, pn);
            feed_bytes(&P, Y, (size_t)yn);
            feed_bytes(&P, Z, (size_t)zn);
            feed_zeros(&P, UBX_RAW_MAX);
            got[mode] = cap_has(&C, 0x22) + cap_has(&C, 0x33);
            errs[mode] = P.stats.ck_fail + P.stats.bad_len;
        }
        printf("B%d %-28s naive got %d/2 (errors %u), rescan got %d/2 (errors %u)\n", 4 + sc,
               sc == 0 ? "truncated X then Y Z:" : sc == 1 ? "byte drop in X then Y Z:" :
               sc == 2 ? "garbage A5..fake sync LEN64:" : "start mid-frame (fake hdr):",
               got[0], errs[0], got[1], errs[1]);
        CHECK(got[0] < 2 && got[1] == 2);
    }
    {   /* B8: 바이트 삽입은 naive 에게도 덜 아프다: X 가 1바이트 길어지면 원래 CK_B 한 바이트만 남는다 */
        uint8_t x2[40];
        memcpy(x2, X, 8); x2[8] = 0x99; memcpy(x2 + 9, X + 8, 8);
        for (int mode = 0; mode < 2; mode++) {
            memset(&C, 0, sizeof C);
            ubx_init(&P, mode == 1, 0, cap_cb, &C);
            feed_bytes(&P, x2, (size_t)xn + 1);
            feed_bytes(&P, Y, (size_t)yn);
            CHECK(P.stats.ck_fail == 1 && C.n == 1 && C.id[0] == 0x22);
        }
    }
    {   /* B9: inter-byte timeout. 잘린 X, 3 ms 침묵, Y. 타임아웃 없으면 naive 는 Y 를 잃는다 */
        for (int cfg = 0; cfg < 2; cfg++) {
            memset(&C, 0, sizeof C);
            ubx_init(&P, false, cfg ? T_US : 0, cap_cb, &C);
            feed_bytes(&P, X, 9);
            g_now += TRUNC_GAP_US;
            feed_bytes(&P, Y, (size_t)yn);
            feed_bytes(&P, Z, (size_t)zn);
            if (cfg == 0) CHECK(!cap_has(&C, 0x22) && P.stats.timeout == 0);
            else CHECK(cap_has(&C, 0x22) && cap_has(&C, 0x33) && P.stats.timeout == 1 && P.stats.ck_fail == 0);
        }
        /* 느린 송신기: 프레임 안 바이트 간격 900 / 1000 µs 는 통과, 1001 µs 는 타임아웃 (T 는 "초과" 기준) */
        const uint32_t gaps[3] = { 900, 1000, 1001 };
        for (int k = 0; k < 3; k++) {
            memset(&C, 0, sizeof C);
            ubx_init(&P, true, T_US, cap_cb, &C);
            for (int i = 0; i < xn; i++) { g_now += gaps[k]; ubx_feed(&P, X[i], g_now); }
            if (k < 2) CHECK(C.n == 1 && P.stats.timeout == 0);
            else CHECK(C.n == 0 && P.stats.sync_skip == (uint32_t)xn);   /* B5 뒤 간격 초과 → B5 도 버림, 프레임 시작 못 함 */
        }
    }
    {   /* B10: Fletcher 는 CRC 가 아니다 — 같은 홀짝 위치 두 바이트의 bit7 뒤집기는 통과(거짓 수락) */
        uint8_t x2[32];
        for (int mode = 0; mode < 2; mode++) {
            memcpy(x2, X, (size_t)xn);
            x2[6] ^= 0x80;                       /* PAYLOAD[0] */
            x2[6 + (mode ? 1 : 2)] ^= 0x80;      /* mode 0: PAYLOAD[2] (같은 홀짝), mode 1: PAYLOAD[1] */
            memset(&C, 0, sizeof C);
            ubx_init(&P, true, 0, cap_cb, &C);
            feed_bytes(&P, x2, (size_t)xn);
            if (mode == 0) CHECK(C.n == 1 && memcmp(C.pl[0], X + 6, 8) != 0);   /* 틀린 데이터가 통과 */
            else CHECK(C.n == 0 && P.stats.ck_fail == 1);
        }
        /* 전부 0 인 헤더는 체크섬도 0: B5 62 00 00 00 00 00 00 은 "유효한" 프레임 */
        memset(&C, 0, sizeof C);
        ubx_init(&P, true, 0, cap_cb, &C);
        const uint8_t sy[2] = { 0xB5, 0x62 };
        feed_bytes(&P, sy, 2);
        feed_zeros(&P, 6);
        CHECK(C.n == 1 && C.cls[0] == 0 && C.id[0] == 0 && C.len[0] == 0);
    }

    /* ---------------- Part C ---------------- */
    {   /* C1: 타이머 경로. 가짜 헤더(LEN 200) 뒤 진짜 Y, 그리고 조용 — 바이트가 더 오지 않으면 Y 는 갇힌다 */
        const uint8_t fake[6] = { 0xB5, 0x62, 0x01, 0x02, 0xC8, 0x00 };
        memset(&C, 0, sizeof C);
        ubx_init(&P, true, T_US, cap_cb, &C);
        feed_bytes(&P, fake, 6);
        feed_bytes(&P, Y, (size_t)yn);
        uint32_t last = g_now;
        CHECK(C.n == 0);
        CHECK(ubx_tick(&P, last + T_US) == 0 && C.n == 0);           /* 정확히 T: 아직 */
        CHECK(ubx_tick(&P, last + T_US + 1) == 1 && C.n == 1 && C.id[0] == 0x22);
        CHECK(P.stats.timeout == 1 && P.st == ST_SYNC1);
        CHECK(ubx_tick(&P, last + 10 * T_US) == 0);                 /* 이미 비었으면 아무 일 없음 */
        /* 32비트 µs 타임스탬프 랩 (71.6분마다): 0xFFFFFF00 → 0x00000100 은 512 µs 간격 */
        memset(&C, 0, sizeof C);
        ubx_init(&P, true, T_US, cap_cb, &C);
        uint32_t t = 0xFFFFFF00u - 6 * BYTE_US;
        for (int i = 0; i < 6; i++) { t += BYTE_US; ubx_feed(&P, monver[i], t); }
        ubx_feed(&P, monver[6], 0x00000100u);
        ubx_feed(&P, monver[7], 0x00000100u + BYTE_US);
        CHECK(C.n == 1 && P.stats.timeout == 0);
    }
    {   /* C2: ISR → 링 버퍼 → 메인 루프. 메인 루프가 200바이트 시간 멈추면 overflow, 나머지는 전부 받는다 */
        static rx_ring R;
        memset(&R, 0, sizeof R);
        bool full_ok = true;
        for (uint32_t i = 0; i < RX_RING_SIZE; i++) full_ok &= rx_isr_push(&R, 0, i);
        CHECK(full_ok && !rx_isr_push(&R, 0, 99) && R.overflow == 1);

        memset(&R, 0, sizeof R);
        gen_stream(7, 120, 0, 0);
        static uint8_t dropped[S_CAP];
        uint32_t ndrop = 0;
        match_t m; memset(&m, 0, sizeof m);
        ubx_init(&P, true, T_US, match_cb, &m);
        for (size_t i = 0; i < s_n; i++) {
            dropped[i] = !rx_isr_push(&R, s_b[i], s_t[i]);
            ndrop += dropped[i];
            bool stall = (i >= 3008 && i < 3200);                  /* poll 6번 건너뜀: i=3007 다음 poll 이 i=3231 */
            if (!stall && (i % 32) == 31) { m.now = s_t[i]; rx_poll(&R, &P); }
        }
        if (s_n) { m.now = s_t[s_n - 1]; rx_poll(&R, &P); }
        m.now += T_US + 1; ubx_tick(&P, m.now);
        uint32_t need = 0;                                          /* 버린 바이트와 안 겹친 프레임 수 */
        for (size_t k = 0; k < s_nf; k++) {
            bool hit = false;
            for (uint32_t j = 0; j < s_f[k].n; j++) hit |= dropped[s_f[k].off + j];
            need += !hit;
        }
        char line[96];
        ubx_stats_line(&P, line, sizeof line);
        printf("C2 ring: %zu bytes, overflow=%u, frames needed %u got %u\n   %s\n",
               s_n, (unsigned)R.overflow, (unsigned)need, (unsigned)m.recovered, line);
        CHECK(R.overflow == ndrop && ndrop == (3231 - 3007) - RX_RING_SIZE && P.stats.overflow == ndrop);   /* 224 쌓임 − 64 = 160 */
        CHECK(m.recovered == need && need < s_nf && m.false_acc == 0);
        char small[16];
        int need_len = ubx_stats_line(&P, small, sizeof small);
        CHECK(need_len >= (int)sizeof small && strlen(small) == sizeof small - 1);   /* 잘림을 반환값으로 안다 */
    }
    {   /* C3: 고정 시드 손상 스트림 — 프레임 3000개, 10% 손상(6종 섞음), PAYLOAD 30% 에 가짜 헤더 */
        gen_stream(12345, 3000, 10, (1u << K_COUNT) - 1);
        CHECK(!s_full && s_nf == 3000);
        uint32_t intact = count_intact();
        match_t m[4];
        ubx_stats st[4];
        uint32_t maxs[4];
        static const char *names[4] = { "naive", "naive+T", "rescan", "rescan+T" };
        for (int c = 0; c < 4; c++) {
            run_cfg(c >= 2, (c & 1) ? T_US : 0, &m[c], &P);
            st[c] = P.stats; maxs[c] = P.max_steps;
        }
        printf("C3 stream: %zu bytes, %zu frames, intact %u\n", s_n, s_nf, (unsigned)intact);
        for (int c = 0; c < 4; c++)
            printf("   %-8s recovered %4u/%u (%.2f%%) false=%u late=%u max_lat=%uus | ok=%u skip=%u len=%u ck=%u to=%u rescan=%u max_steps=%u\n",
                   names[c], (unsigned)m[c].recovered, (unsigned)intact, intact ? 100.0 * m[c].recovered / intact : 0.0,
                   (unsigned)m[c].false_acc, (unsigned)m[c].late, (unsigned)m[c].max_lat,
                   (unsigned)st[c].frames_ok, (unsigned)st[c].sync_skip, (unsigned)st[c].bad_len,
                   (unsigned)st[c].ck_fail, (unsigned)st[c].timeout, (unsigned)st[c].rescans, (unsigned)maxs[c]);
        for (int c = 0; c < 4; c++)
            CHECK(st[c].frames_ok == m[c].recovered + m[c].false_acc + m[c].dup && m[c].dup == 0);
        CHECK(m[0].recovered < m[1].recovered && m[1].recovered < m[2].recovered);   /* T 도, rescan 도 돕는다 */
        CHECK(m[3].recovered * 1000u >= intact * 998u);                             /* rescan+T ≥ 99.8% */
        CHECK(m[3].late < m[2].late);                                              /* 타이머가 갇힌 프레임을 꺼낸다 */
        CHECK(st[3].timeout > 0 && st[0].timeout == 0 && st[2].rescans > 0);
        CHECK(maxs[3] <= 2u * (1u + (UBX_RAW_MAX / 2u) * (UBX_RAW_MAX + 2u) / 2u)); /* 최악 한계 (풀이 참고) */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
