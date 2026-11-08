/* C-A10 MISRA 스타일 정적 검사와 리팩터링
 *
 * legacy_driver.c (300줄 분석 대상, 따로 빌드) 의 핵심 함수 7개를 old_* 로 "그대로" 옮겨 두었다.
 * old_* 는 고치지 않는다 — 이것이 동작의 기준(characterization)이다.
 *
 * 할 일: 정적 분석(-Weverything, clang-tidy) 결과를 근거로 MISRA 스타일 new_* 를 작성한다.
 *   - 고정 폭 타입, 명시적 변환, 중괄호, switch 마다 break + default, 재귀 없음, 단일 출구,
 *     매크로 대신 함수, 하드웨어 레지스터는 정확히 한 번 읽기.
 * 하네스가 확인하는 두 가지:
 *   (1) 동등성(equivalence) — old 와 new 를 같은 입력 수만 개(xorshift32, 고정 시드)로 돌려 결과 비교
 *   (2) 의도된 차이(delta) — old 의 UB/버그 영역은 동등성 도메인에서 빼고, 새 계약을 따로 검사.
 *       차이 목록(D1..D6)은 아래 주석과 레슨 §5 표에 있다. 목록에 없는 차이는 전부 회귀(regression)다.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* old_temp_mc 는 구현 정의 동작 두 개에 기대고 있다 → 가정을 빌드 시점에 못 박는다 (C-A08 참고) */
_Static_assert(sizeof(short) == 2, "legacy assumes 16-bit short");
_Static_assert((-16 >> 4) == -1, "legacy assumes arithmetic >> of negative int (impl-defined, 6.5.7p5)");

/* ======================= 모의 버스 (주어진 코드 — 고치지 않는다) ======================= */
#define REG_FIFO_DATA 0x21u
#define FIFO_DEPTH    32u
static uint8_t  g_fifo[FIFO_DEPTH];
static uint32_t g_fifo_head, g_fifo_n, g_bus_reads;

static void fifo_reset(void) { g_fifo_head = 0u; g_fifo_n = 0u; g_bus_reads = 0u; }
static void fifo_push(uint8_t v) {
    if (g_fifo_n < FIFO_DEPTH) { g_fifo[(g_fifo_head + g_fifo_n) % FIFO_DEPTH] = v; g_fifo_n++; }
}
/* FIFO_DATA 레지스터: 읽을 때마다 한 칸 꺼낸다(pop). 비었으면 0. 다른 레지스터는 0. */
static uint8_t bus_read(uint8_t reg) {
    g_bus_reads++;
    if (reg != REG_FIFO_DATA || g_fifo_n == 0u) return 0u;
    uint8_t v = g_fifo[g_fifo_head];
    g_fifo_head = (g_fifo_head + 1u) % FIFO_DEPTH;
    g_fifo_n--;
    return v;
}

/* ======================= LEGACY (legacy_driver.c 에서 복사 — 고치지 않는다) ======================= */
#define MAX(a, b)       ((a) > (b) ? (a) : (b))
#define SCALE_MC(x)     x * 625 / 10            /* 1 LSB = 0.0625 C = 62.5 mC */

enum { CMD_RESET = 1, CMD_START, CMD_SET_RATE, CMD_STOP, CMD_CLEAR };
typedef struct { int running; int rate; int err; } ls_dev;

static int g_temp_offset;      /* calibration offset in LSB, set by factory tool */

static int old_temp_mc(unsigned short raw)
{
    short v = raw << 4;
    v = v >> 4;
    return SCALE_MC(v);
}

static int old_temp_cal_mc(unsigned short raw)
{
    short v = raw << 4;
    v = v >> 4;
    return SCALE_MC(v + g_temp_offset);
}

static short old_avg(const short *s, unsigned n)
{
    int sum = 0;
    unsigned i;
    for (i = 0; i < n; i++)
        sum += s[i];
    return sum / n;
}

static int old_frame_ok(const uint8_t *buf, uint8_t n)
{
    uint8_t sum = 0;
    int i;
    for (i = 0; i < n - 1; i++)
        sum += buf[i];
    return sum == buf[n - 1];
}

static int old_fifo_peak(int n)
{
    int peak = 0;
    while (n--)
        peak = MAX(bus_read(REG_FIFO_DATA), peak);
    return peak;
}

static int old_cmd(ls_dev *d, int cmd, int arg)
{
    switch (cmd) {
    case CMD_RESET:
        d->rate = 1;
        d->err = 0;
        /* and start again */
    case CMD_START:
        d->running = 1;
        break;
    case CMD_SET_RATE:
        if (arg > 0 && arg <= 100)
            d->rate = arg;
        else
            d->err++;
    case CMD_STOP:
        d->running = 0;
        break;
    case CMD_CLEAR:
        d->err = 0;
    }
    return d->running;
}

static int old_tlv_sum(const uint8_t *p, int len)
{
    int sum = 0;
    while (len > 0) {
        int t, l, i;
        if (len < 2)
            return -1;
        t = p[0];
        l = p[1];
        if (l > len - 2)
            return -1;
        if (t & 0x80) {
            int s = old_tlv_sum(p + 2, l);
            if (s < 0)
                return s;
            sum += s;
        } else {
            for (i = 0; i < l; i++)
                sum += p[2 + i];
        }
        p += 2 + l;
        len -= 2 + l;
    }
    return sum;
}

/* ======================= NEW — MISRA 스타일 (여기를 채운다) ======================= */
#define TEMP_MASK      0x0FFFu      /* 12비트 원시값                                  */
#define TEMP_SIGN      0x0800       /* 이 값 이상이면 음수                             */
#define TEMP_MOD       0x1000       /* 2^12                                            */
#define TEMP_MC_NUM    625          /* 62.5 mC/LSB = 625/10                            */
#define TEMP_MC_DEN    10
#define LS_RATE_MAX    100
#define LS_AVG_MAX_N   4096u        /* 32767 * 4096 < 2^31 → int32_t 합이 넘치지 않는다 */
#define TLV_HDR        2u
#define TLV_CONTAINER  0x80u
#define TLV_MAX_DEPTH  8u           /* 중첩 컨테이너 최대 깊이 (스택 대신 배열)         */
#define TLV_EMALFORMED (-1)
#define TLV_ETOODEEP   (-2)

/* Part A ---------------------------------------------------------------------------------- */
/* 12비트 2의 보수 → 부호 있는 LSB 값(-2048..2047). 상위 4비트는 무시. 구현 정의 동작 없이. */
static int32_t temp_lsb(uint16_t raw) {
    const uint16_t u = raw & TEMP_MASK;          /* 0..4095, unsigned 끼리 연산          */
    int32_t v = (int32_t)u;                      /* 값이 그대로 들어가는 변환 (6.3.1.3p1) */
    if (v >= TEMP_SIGN) {
        v -= TEMP_MOD;                           /* 0x800..0xFFF → -2048..-1             */
    }
    return v;
}
/* old_temp_mc 와 65536개 입력 전부에서 같은 값 (0 쪽으로 버림 포함) */
static int32_t new_temp_mc(uint16_t raw) {
    return (temp_lsb(raw) * TEMP_MC_NUM) / TEMP_MC_DEN;   /* C99+ 나눗셈은 0 쪽 버림 = legacy */
}
/* 평균, 0 쪽으로 버림. D1: n == 0 → 0 (old 는 0으로 나눔 = UB), n > LS_AVG_MAX_N → 0.
 *                       D2: 합이 음수일 때 old 는 unsigned 나눗셈 쓰레기, new 는 진짜 평균. */
static int16_t new_avg(const int16_t *s, uint32_t n) {
    int16_t avg = 0;
    if ((n > 0u) && (n <= LS_AVG_MAX_N)) {
        int32_t sum = 0;
        for (uint32_t i = 0u; i < n; i++) {
            sum += s[i];                         /* int16_t → int32_t: 넓히기만, 부호 유지 */
        }
        avg = (int16_t)(sum / (int32_t)n);       /* 부호 있는 / 부호 있는. 결과는 int16 범위 안 */
    }
    return avg;
}
/* 마지막 바이트 = 앞 바이트들의 8비트 합. D3: n == 0 → false (old 는 buf[-1] 읽기 = UB) */
static bool new_frame_ok(const uint8_t *buf, uint8_t n) {
    bool ok = false;
    if (n > 0u) {
        const uint8_t last = (uint8_t)(n - 1u);  /* n >= 1 이므로 감쌈(wrap) 없음 */
        uint8_t sum = 0u;
        for (uint8_t i = 0u; i < last; i++) {
            sum = (uint8_t)(sum + buf[i]);       /* 8비트 감쌈을 의도적으로, 명시적으로 */
        }
        ok = (sum == buf[last]);
    }
    return ok;
}

/* Part B ---------------------------------------------------------------------------------- */
/* FIFO_DATA 를 정확히 n 번 읽고 그 최대값. D4: old 는 MAX 매크로가 bus_read 를 두 번 평가 → 샘플 유실 */
static uint8_t new_fifo_peak(uint32_t n) {
    uint8_t peak = 0u;
    for (uint32_t i = 0u; i < n; i++) {
        const uint8_t v = bus_read(REG_FIFO_DATA);   /* 부작용 있는 읽기는 한 번, 변수에 담는다 */
        if (v > peak) {
            peak = v;
        }
    }
    return peak;
}
/* (원시값 + 오프셋) 을 mC 로. 오프셋은 |offset_lsb| <= 2048 가정.
 * D5: old 는 SCALE_MC(v + off) 가 v + off*625/10 로 펼쳐진다 (괄호 없는 매크로) → offset 0 이어도 mC 가 아님. */
static int32_t new_temp_cal_mc(uint16_t raw, int32_t offset_lsb) {
    const int32_t v = temp_lsb(raw) + offset_lsb;     /* 매크로가 아니라 값: 괄호 문제가 없다 */
    return (v * TEMP_MC_NUM) / TEMP_MC_DEN;
}
/* old_cmd 와 모든 (상태, cmd, arg) 에서 같은 동작 — fall-through 두 개의 "효과" 까지 그대로, 단 명시적으로. */
static int new_cmd(ls_dev *d, int cmd, int arg) {
    switch (cmd) {
    case CMD_RESET:
        d->rate = 1;
        d->err = 0;
        d->running = 1;                          /* legacy: RESET 은 START 로 흘러갔다 (의도) */
        break;
    case CMD_START:
        d->running = 1;
        break;
    case CMD_SET_RATE:
        if ((arg > 0) && (arg <= LS_RATE_MAX)) {
            d->rate = arg;
        } else {
            d->err += 1;
        }
        d->running = 0;                          /* legacy: SET_RATE 는 STOP 으로 흘러갔다.
                                                    호스트 툴이 이 동작에 기대므로 보존 (별도 티켓) */
        break;
    case CMD_STOP:
        d->running = 0;
        break;
    case CMD_CLEAR:
        d->err = 0;
        break;
    default:
        /* 모르는 명령은 무시 (legacy 계약) */
        break;
    }
    return d->running;
}

/* Part C ---------------------------------------------------------------------------------- */
/* 재귀 없는 TLV 합. 결과·에러 판정 순서가 old 와 같아야 한다.
 * D6: 컨테이너 중첩이 TLV_MAX_DEPTH 를 넘으면 TLV_ETOODEEP (old 는 끝없이 재귀). */
static int32_t new_tlv_sum(const uint8_t *p, size_t len) {
    size_t rem[TLV_MAX_DEPTH + 1u];              /* rem[k] = 깊이 k 에서 아직 안 읽은 바이트 */
    uint32_t depth = 0u;
    size_t pos = 0u;
    int32_t sum = 0;
    int32_t err = 0;
    rem[0] = len;
    while (err == 0) {
        if (rem[depth] == 0u) {                  /* 이 깊이 끝: 부모로 (재귀의 return) */
            if (depth == 0u) {
                break;
            }
            depth--;
        } else if (rem[depth] < TLV_HDR) {
            err = TLV_EMALFORMED;
        } else {
            const uint8_t type = p[pos];
            const size_t l = p[pos + 1u];
            if (l > (rem[depth] - TLV_HDR)) {
                err = TLV_EMALFORMED;
            } else {
                rem[depth] -= TLV_HDR + l;       /* 부모 몫에서 이 원소 전체를 먼저 뺀다 */
                pos += TLV_HDR;
                if ((type & TLV_CONTAINER) != 0u) {
                    if (depth == TLV_MAX_DEPTH) {
                        err = TLV_ETOODEEP;
                    } else {
                        depth++;                 /* 재귀 호출 대신 한 칸 깊이 */
                        rem[depth] = l;
                    }
                } else {
                    for (size_t i = 0u; i < l; i++) {
                        sum += p[pos + i];
                    }
                    pos += l;
                }
            }
        }
    }
    return (err != 0) ? err : sum;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0xC0FFEEu;
static uint32_t rnd(void) {                       /* xorshift32 — 고정 시드라 매번 같은 입력 */
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
    return g_rng;
}

/* 유효한 TLV 원소 하나를 buf 에 만든다 (테스트 코드라 재귀 허용). 컨테이너는 depth < maxd 에서만. */
static unsigned g_gen_depth;                      /* 이번 트리에서 만든 최대 중첩 깊이 */
static size_t gen_elem(uint8_t *buf, size_t cap, unsigned depth, unsigned maxd) {
    if (cap < 2u) return 0u;
    size_t maxpay = cap - 2u > 255u ? 255u : cap - 2u;
    if (depth < maxd && rnd() % 3u == 0u) {
        if (depth + 1u > g_gen_depth) g_gen_depth = depth + 1u;
        size_t budget = rnd() % (maxpay + 1u), used = 0u;
        while (used + 2u <= budget) {
            size_t w = gen_elem(buf + 2 + used, budget - used, depth + 1u, maxd);
            if (w == 0u) break;
            used += w;
        }
        buf[0] = (uint8_t)(0x80u | (rnd() & 0x7Fu));
        buf[1] = (uint8_t)used;
        return 2u + used;
    }
    size_t l = rnd() % ((maxpay < 16u ? maxpay : 16u) + 1u);
    buf[0] = (uint8_t)(rnd() & 0x7Fu);
    buf[1] = (uint8_t)l;
    for (size_t i = 0; i < l; i++) buf[2 + i] = (uint8_t)rnd();
    return 2u + l;
}
static size_t gen_tlv(uint8_t *buf, size_t cap, unsigned maxd) {
    size_t total = rnd() % (cap + 1u), used = 0u;
    g_gen_depth = 0u;
    while (used + 2u <= total) {
        size_t w = gen_elem(buf + used, total - used, 0u, maxd);
        if (w == 0u) break;
        used += w;
    }
    return used;
}
/* 컨테이너 k 개가 한 줄로 중첩되고 맨 안쪽에 leaf {7} 하나: 81 L 81 L ... 01 01 07 */
static size_t gen_chain(uint8_t *buf, unsigned k) {
    for (unsigned i = 0; i < k; i++) {
        buf[2u * i] = 0x81u;
        buf[2u * i + 1u] = (uint8_t)(2u * (k - 1u - i) + 3u);
    }
    buf[2u * k] = 0x01u; buf[2u * k + 1u] = 1u; buf[2u * k + 2u] = 7u;
    return 2u * k + 3u;
}

int main(void) {
    /* ---------- Part A: 암시적 변환 ---------- */
    unsigned bad = 0u;
    for (uint32_t r = 0u; r <= 0xFFFFu; r++)
        if (new_temp_mc((uint16_t)r) != old_temp_mc((unsigned short)r)) bad++;
    CHECK(bad == 0u);                                       /* 65536개 전수 동등 */
    CHECK(new_temp_mc(0x190u) == 25000 && new_temp_mc(0xF190u) == 25000);   /* 25 C, 상위 니블 무시 */
    CHECK(new_temp_mc(0xFFFu) == -62 && new_temp_mc(0x800u) == -128000);    /* -62.5 → -62 (0 쪽 버림) */
    CHECK(temp_lsb(0x7FFu) == 2047 && temp_lsb(0x800u) == -2048 && temp_lsb(0xFFFFu) == -1);

    int16_t s[64];
    unsigned eq = 0u, neg_ok = 0u, neg_cases = 0u;
    for (int t = 0; t < 5000; t++) {
        uint32_t n = 1u + rnd() % 64u;
        long long ref = 0;
        for (uint32_t i = 0; i < n; i++) { s[i] = (int16_t)(rnd() & 0xFFFFu); ref += s[i]; }
        if (ref >= 0) { if (new_avg(s, n) == old_avg(s, n)) eq++; else bad++; }
        else { neg_cases++; if (new_avg(s, n) == (int16_t)(ref / (long long)n)) neg_ok++; }
    }
    CHECK(eq > 2000u && eq + neg_cases == 5000u);           /* 합 >= 0 도메인: 전부 old 와 같음 */
    CHECK(neg_ok == neg_cases);                             /* D2: 합 < 0 이면 진짜 평균 */
    int16_t m3[3] = { -3, -3, -3 };
    CHECK(old_avg(m3, 3u) == 21842 && new_avg(m3, 3u) == -3);   /* D2 고정: old 는 구현 정의 쓰레기 */
    int16_t m2[2] = { -1, -2 };
    CHECK(new_avg(m2, 2u) == -1);                           /* -1.5 → -1 */
    CHECK(new_avg(m3, 0u) == 0 && new_avg(s, LS_AVG_MAX_N + 1u) == 0);    /* D1 (old 는 호출 금지) */

    uint8_t fr[256];
    bad = 0u;
    for (int t = 0; t < 5000; t++) {
        uint8_t n = (uint8_t)(1u + rnd() % 255u), sum = 0u;
        for (uint8_t i = 0; i < n; i++) fr[i] = (uint8_t)rnd();
        for (uint8_t i = 0; i + 1u < n; i++) sum = (uint8_t)(sum + fr[i]);
        if (rnd() & 1u) fr[n - 1u] = sum;                   /* 절반은 올바른 체크섬 */
        if ((int)new_frame_ok(fr, n) != old_frame_ok(fr, n)) bad++;
    }
    CHECK(bad == 0u);
    fr[0] = 0u;
    CHECK(new_frame_ok(fr, 1u) == true && old_frame_ok(fr, 1u) == 1);    /* 빈 payload, 체크섬 0 */
    CHECK(new_frame_ok(fr, 0u) == false);                   /* D3 (old 는 호출 금지: buf[-1]) */

    /* ---------- Part B: 매크로 부작용, switch ---------- */
    fifo_reset(); fifo_push(3u); fifo_push(9u); fifo_push(4u);
    CHECK(old_fifo_peak(3) == 9 && g_bus_reads == 4u);      /* old: 운 좋게 9, 하지만 4번 읽음 */
    fifo_reset(); fifo_push(1u); fifo_push(2u); fifo_push(3u); fifo_push(4u);
    CHECK(old_fifo_peak(2) == 4 && g_fifo_n == 0u);         /* D4 고정: 2개만 꺼내랬는데 4개 다 사라짐 */
    fifo_reset(); fifo_push(1u); fifo_push(2u); fifo_push(3u); fifo_push(4u);
    CHECK(new_fifo_peak(2u) == 2u && g_bus_reads == 2u && g_fifo_n == 2u);
    bad = 0u;
    for (int t = 0; t < 2000; t++) {
        fifo_reset();
        uint32_t k = rnd() % (FIFO_DEPTH + 1u), n = rnd() % (FIFO_DEPTH + 4u);
        uint8_t ref = 0u;
        for (uint32_t i = 0; i < k; i++) {
            uint8_t v = (uint8_t)rnd();
            fifo_push(v);
            if (i < n && v > ref) ref = v;
        }
        uint32_t left = k > n ? k - n : 0u;
        if (new_fifo_peak(n) != ref || g_bus_reads != n || g_fifo_n != left) bad++;
    }
    CHECK(bad == 0u);                                        /* 명세 모델과 일치, 정확히 n 번 읽기 */

    unsigned bad_old = 0u;
    bad = 0u;
    g_temp_offset = 0;
    for (uint32_t r = 0u; r <= 0xFFFFu; r++) {
        if (new_temp_cal_mc((uint16_t)r, 0) != new_temp_mc((uint16_t)r)) bad++;       /* 보정 0 = 무보정 */
        if (old_temp_cal_mc((unsigned short)r) != temp_lsb((uint16_t)r)) bad_old++;   /* old 는 단위가 LSB! */
    }
    CHECK(bad == 0u && bad_old == 0u);                       /* D5: old 와 같은 도메인이 아예 없다 */
    g_temp_offset = 16;                                      /* +1 C 보정 */
    CHECK(old_temp_cal_mc(16u) == 1016 && new_temp_cal_mc(16u, 16) == 2000);   /* D5 고정 */
    CHECK(new_temp_cal_mc(0x800u, -1) == -128062 && new_temp_cal_mc(0x7FFu, 2048) == 255937);

    ls_dev a = { 0, 1, 0 }, b = { 0, 1, 0 };
    bad = 0u;
    for (int t = 0; t < 20000; t++) {
        int cmd = (int)(rnd() % 9u) - 1;                     /* -1..7: 정의 안 된 명령 포함 */
        int arg = (int)(rnd() % 131u) - 10;                  /* -10..120: 범위 밖 포함 */
        int ra = old_cmd(&a, cmd, arg), rb = new_cmd(&b, cmd, arg);
        if (ra != rb || memcmp(&a, &b, sizeof a) != 0) bad++;
    }
    CHECK(bad == 0u);                                        /* 20000 단계 상태 동등 */
    ls_dev d = { 0, 50, 3 };
    CHECK(new_cmd(&d, CMD_RESET, 0) == 1 && d.rate == 1 && d.err == 0);    /* RESET → START 효과 */
    CHECK(new_cmd(&d, CMD_SET_RATE, 20) == 0 && d.rate == 20);             /* SET_RATE 는 멈춘다 (legacy) */
    CHECK(new_cmd(&d, CMD_SET_RATE, 0) == 0 && d.err == 1 && d.rate == 20);
    d.running = 1;
    CHECK(new_cmd(&d, 99, 5) == 1 && d.rate == 20 && d.err == 1);          /* 모르는 명령: 무시 */

    /* ---------- Part C: 재귀 제거 ---------- */
    static const uint8_t cfg[] = { 0x81, 6, 0x01, 1, 7, 0x02, 1, 9, 0x03, 2, 1, 2 };
    CHECK(old_tlv_sum(cfg, (int)sizeof cfg) == 19 && new_tlv_sum(cfg, sizeof cfg) == 19);
    uint8_t tb[600];
    bad = 0u;
    unsigned deep4 = 0u, deep8 = 0u;
    for (int t = 0; t < 3000; t++) {
        size_t n = gen_tlv(tb, 512u, TLV_MAX_DEPTH);
        int32_t o = old_tlv_sum(tb, (int)n), w = new_tlv_sum(tb, n);
        if (o != w || o < 0) bad++;
        if (g_gen_depth >= 4u) deep4++;
        if (g_gen_depth == TLV_MAX_DEPTH) deep8++;
    }
    CHECK(bad == 0u && deep4 > 100u && deep8 > 0u);         /* 유효 트리(깊이 <= 8): 전부 동등 */
    unsigned exact = 0u, toodeep = 0u, malformed = 0u;
    bad = 0u;
    for (int t = 0; t < 5000; t++) {
        size_t n = gen_tlv(tb, 300u, TLV_MAX_DEPTH);
        if (n > 0u && (t & 1)) tb[rnd() % n] = (uint8_t)rnd();    /* 홀수 회차: 1바이트 변조 */
        else if (n > 0u) n = rnd() % n;                           /* 짝수 회차: 잘라내기       */
        int32_t o = old_tlv_sum(tb, (int)n), w = new_tlv_sum(tb, n);
        if (w == o) { exact++; if (o == TLV_EMALFORMED) malformed++; }
        else if (w == TLV_ETOODEEP) toodeep++;
        else bad++;
    }
    CHECK(bad == 0u && exact + toodeep == 5000u);           /* 깨진 입력: 같거나, 너무 깊다는 에러만 */
    CHECK(malformed > 1000u && toodeep < 50u);              /* 에러 경로도 충분히 검사됐는가 */
    size_t n8 = gen_chain(tb, TLV_MAX_DEPTH), n9 = gen_chain(tb + 64, TLV_MAX_DEPTH + 1u);
    CHECK(old_tlv_sum(tb, (int)n8) == 7 && new_tlv_sum(tb, n8) == 7);      /* 깊이 8: 허용 */
    CHECK(old_tlv_sum(tb + 64, (int)n9) == 7 && new_tlv_sum(tb + 64, n9) == TLV_ETOODEEP);   /* D6 */
    static const uint8_t trunc[] = { 0x81, 4, 0x01, 5, 1 };       /* 자식 길이가 부모를 넘음 */
    CHECK(old_tlv_sum(trunc, 5) == -1 && new_tlv_sum(trunc, 5u) == TLV_EMALFORMED);
    CHECK(new_tlv_sum(cfg, 0u) == 0 && new_tlv_sum(cfg, 1u) == TLV_EMALFORMED);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
