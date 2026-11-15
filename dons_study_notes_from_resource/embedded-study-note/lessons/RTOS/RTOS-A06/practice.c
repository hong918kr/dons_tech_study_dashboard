/* RTOS-A06 추가 연습 — 한 번에 검증
 *   P1 첫 SWITCH_OUT 으로 앞부분 귀속 되살리기 (backfill) — 언제 해도 되고 언제 안 되나
 *   P2 델타 타임스탬프 + LEB128 varint 인코딩: 레코드 크기 16 B → 몇 B?  (wrap 포함 왕복 일치)
 *   P3 링 크기 정하기: 버스트 도착 + 회선 속도 → 손실 없는 최소 cap (cap-1 이면 손실)
 *   P4 16비트 하드웨어 카운터 + 소프트웨어 overflow 로 32비트 타임스탬프 읽기: hi/lo 경쟁
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS  %s\n", #e); } \
                      else printf("FAIL  %s   (line %d)\n", #e, __LINE__); } while (0)

/* ======================= P1 backfill ======================= */
enum { E_IN, E_OUT, E_DROP, E_OTHER };
typedef struct { uint32_t t; int ev; int task; } pev_t;      /* task 0..3, DROP 은 -1 */
typedef struct { uint32_t run[4], unknown, backfilled; } p1_out_t;

/* 단순 디코더 (ISR 없음, wrap 없음). backfill=true 면: 주인을 모르는 상태에서 처음 보는 전환 이벤트가
 * SWITCH_OUT X 이면, "known_from" (시작 또는 DROP 뒤 첫 실제 이벤트의 시각) 부터 그 시각까지를 X 에게 준다.
 * 근거: known_from 이후로는 이벤트가 하나도 빠지지 않았고 그 사이 SWITCH_IN 이 없었으니 X 가 계속 돌았다.
 * DROP 시각 ~ known_from 사이는 여전히 모름 (그 구간에서 무슨 전환이 사라졌는지 모른다). */
static void p1_decode(const pev_t *e, int n, bool backfill, p1_out_t *o) {
    memset(o, 0, sizeof *o);
    int task = -1;
    bool fresh = true;                 /* 시작 또는 DROP 직후: 아직 실제 이벤트를 못 봄 */
    bool seen_switch = false;          /* 모르는 구간 이후 전환 이벤트를 봤나 */
    uint32_t now = e[0].t, known_from = e[0].t;
    for (int i = 0; i < n; i++) {
        uint32_t dt = e[i].t - now;
        if (task >= 0) o->run[task] += dt; else o->unknown += dt;
        now = e[i].t;
        if (e[i].ev == E_DROP) { task = -1; fresh = true; seen_switch = false; continue; }
        if (fresh) { known_from = now; fresh = false; }
        if (e[i].ev == E_IN) { task = e[i].task; seen_switch = true; }
        else if (e[i].ev == E_OUT) {
            if (task < 0 && !seen_switch && backfill) {
                uint32_t b = now - known_from;
                o->run[e[i].task] += b; o->unknown -= b; o->backfilled += b;
            }
            seen_switch = true;
        }
    }
}
static void test_p1(void) {
    printf("-- P1 backfill\n");
    /* 덮어쓰기 덤프 모양: 첫 레코드 100 은 RELEASE, 250 에 SWITCH_OUT 2 → [100,250] 은 태스크 2 */
    const pev_t a[] = { { 100, E_OTHER, 1 }, { 250, E_OUT, 2 }, { 250, E_IN, 1 }, { 400, E_OUT, 1 } };
    p1_out_t o;
    p1_decode(a, 4, false, &o);
    CHECK(o.unknown == 150 && o.run[2] == 0 && o.run[1] == 150);
    p1_decode(a, 4, true, &o);
    CHECK(o.unknown == 0 && o.run[2] == 150 && o.backfilled == 150);
    /* 중간 DROP (마커 시각 500): 첫 실제 이벤트 560 부터만 되살린다. [500,560] 은 계속 모름 */
    const pev_t b[] = { { 0, E_IN, 0 }, { 500, E_DROP, -1 }, { 560, E_OTHER, 3 }, { 700, E_OUT, 3 },
                        { 700, E_IN, 0 }, { 800, E_OUT, 0 } };
    p1_decode(b, 6, true, &o);
    CHECK(o.run[0] == 600 && o.run[3] == 140 && o.unknown == 60 && o.backfilled == 140);
    /* 함정: 모르는 구간 뒤 첫 전환이 SWITCH_IN 이면 되살릴 근거가 없다 (그 전 주인을 모름) */
    const pev_t c[] = { { 0, E_OTHER, 1 }, { 90, E_IN, 1 }, { 120, E_OUT, 1 }, { 120, E_IN, 2 }, { 130, E_OUT, 2 } };
    p1_decode(c, 5, true, &o);
    CHECK(o.unknown == 90 && o.backfilled == 0 && o.run[1] == 30 && o.run[2] == 10);
}

/* ======================= P2 delta + varint ======================= */
static int varint_put(uint8_t *p, uint32_t v) {           /* LEB128: 7비트씩, 상위 비트 = 계속 */
    int n = 0;
    do { uint8_t b = v & 0x7Fu; v >>= 7; p[n++] = (uint8_t)(b | (v ? 0x80u : 0u)); } while (v);
    return n;
}
static int varint_get(const uint8_t *p, uint32_t *v) {
    uint32_t x = 0; int n = 0, sh = 0;
    do { x |= (uint32_t)(p[n] & 0x7Fu) << sh; sh += 7; } while (p[n++] & 0x80u);
    *v = x;
    return n;
}
typedef struct { uint32_t t; uint8_t ev, id; } prec_t;
/* 패킷 = [ev][id][Δt varint]. 순번은 스트림 순서 자체 (손실은 DROP 레코드가 개수를 실어 나른다). */
static size_t enc(const prec_t *r, int n, uint32_t t_prev, uint8_t *out) {
    size_t len = 0;
    for (int i = 0; i < n; i++) {
        out[len++] = r[i].ev;
        out[len++] = r[i].id;
        len += (size_t)varint_put(out + len, r[i].t - t_prev);   /* wrap 해도 u32 뺄셈이면 맞다 */
        t_prev = r[i].t;
    }
    return len;
}
static int dec(const uint8_t *in, size_t len, uint32_t t_prev, prec_t *r) {
    int n = 0;
    for (size_t i = 0; i < len; ) {
        uint32_t d;
        r[n].ev = in[i++]; r[n].id = in[i++];
        i += (size_t)varint_get(in + i, &d);
        t_prev += d; r[n].t = t_prev; n++;
    }
    return n;
}
static void test_p2(void) {
    printf("-- P2 delta + varint\n");
    uint8_t b[8]; uint32_t v;
    CHECK(varint_put(b, 0) == 1 && varint_put(b, 127) == 1 && varint_put(b, 128) == 2 &&
          varint_put(b, 16383) == 2 && varint_put(b, 16384) == 3 && varint_put(b, 0xFFFFFFFFu) == 5);
    CHECK(varint_get(b, &v) == 5 && v == 0xFFFFFFFFu);
    /* Part C 모양의 이벤트 300개: 같은 시각 묶음(Δ0) + 10~990 µs 간격, 시작이 wrap 직전 */
    static prec_t r[300], back[300];
    static uint8_t buf[300 * 7];
    uint32_t t = 0xFFFFFF00u, x = 2463534242u;
    for (int i = 0; i < 300; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;                 /* xorshift32, 고정 씨앗 */
        uint32_t k = x % 4u;
        t += (k == 0) ? 0u : (k == 1) ? 10u * (x % 9u + 1u) : 10u * (x % 99u + 1u);
        r[i].t = t; r[i].ev = (uint8_t)(x % 13u); r[i].id = (uint8_t)(x % 6u);
    }
    size_t len = enc(r, 300, 0xFFFFFF00u, buf);
    int n = dec(buf, len, 0xFFFFFF00u, back);
    CHECK(n == 300 && memcmp(r, back, sizeof r) == 0);
    CHECK(r[299].t < r[0].t);                                    /* 실제로 wrap 을 건넜다 */
    printf("      300 events: fixed 16 B/rec = %d B, delta+varint = %zu B (%.2f B/rec)\n", 300 * 16, len,
           (double)len / 300.0);
    CHECK(len < 300u * 5u);
}

/* ======================= P3 링 크기 ======================= */
#define P3_STEPS 400
/* 걸음(10 µs)마다 도착 a[k]개, 회선은 걸음마다 link 바이트, 레코드 16 B (Part C 의 sim_drain 과 같은 규칙:
 * 도착 먼저, 그다음 전송. 보낼 게 없으면 크레딧 0). 반환 = 도착 직후 최대 적체 = 손실 없는 최소 cap. */
static uint32_t p3_min_cap(const uint8_t *a, int n, uint32_t link) {
    uint32_t q = 0, credit = 0, peak = 0;
    for (int k = 0; k < n; k++) {
        q += a[k];
        if (q > peak) peak = q;
        credit += link;
        while (credit >= 16u) { if (!q) { credit = 0; break; } q--; credit -= 16u; }
    }
    return peak;
}
static uint32_t p3_drops(const uint8_t *a, int n, uint32_t link, uint32_t cap) {
    uint32_t q = 0, credit = 0, drops = 0;
    for (int k = 0; k < n; k++) {
        for (uint32_t i = 0; i < a[k]; i++) { if (q < cap) q++; else drops++; }
        credit += link;
        while (credit >= 16u) { if (!q) { credit = 0; break; } q--; credit -= 16u; }
    }
    return drops;
}
static void test_p3(void) {
    printf("-- P3 링 크기\n");
    static uint8_t a[P3_STEPS];
    for (int k = 0; k < P3_STEPS; k++) a[k] = (k % 10 == 0) ? 1 : 0;          /* 배경: 100 µs 마다 1개 (10/ms) */
    for (int k = 100; k < 124; k += 3) a[k] += 2;                              /* CAN 버스트: 8 프레임 × (ENTER+EXIT) */
    uint32_t c3 = p3_min_cap(a, P3_STEPS, 3), c6 = p3_min_cap(a, P3_STEPS, 6);
    printf("      min cap: link 3 B/step -> %u, link 6 B/step -> %u\n", c3, c6);
    CHECK(p3_drops(a, P3_STEPS, 3, c3) == 0 && p3_drops(a, P3_STEPS, 3, c3 - 1) > 0);
    CHECK(p3_drops(a, P3_STEPS, 6, c6) == 0 && p3_drops(a, P3_STEPS, 6, c6 - 1) > 0);
    CHECK(c6 < c3);                                   /* 회선이 빠르면 버스트 중에도 빼 가니 링이 작아도 된다 */
    /* 평균이 회선보다 크면 cap 이 아무리 커도 언젠가 넘친다: 배경을 10 µs 마다 1개로 → 100 kB/s 필요, 회선 30 kB/s */
    static uint8_t h[P3_STEPS];
    for (int k = 0; k < P3_STEPS; k++) h[k] = 1;
    CHECK(p3_min_cap(h, P3_STEPS, 3) > P3_STEPS / 2);  /* 적체가 실행 길이에 비례해 자란다 */
}

/* ======================= P4 hi/lo 타임스탬프 ======================= */
/* STM32 TIM3 같은 16비트 카운터 (1 µs/카운트) + update(overflow) 인터럽트가 g_ovf++.
 * 모델: 레지스터/변수 접근 1번 = 1 µs 경과. 인터럽트가 켜져 있으면 접근과 접근 사이에 펜딩 ISR 이 돈다.
 * 꺼져 있으면(트레이스 레코더가 잠금 안에서 시각을 읽는 경우!) UIF 플래그만 서 있다. */
static uint32_t g_T;            /* 진짜 시간 */
static uint32_t g_ovf;          /* ISR 이 세는 overflow 횟수 */
static bool g_uif, g_irq_on;
static void hw_step(void) {
    if (g_irq_on && g_uif) { g_ovf++; g_uif = false; }      /* 펜딩 ISR 이 접근 사이에 돈다 */
    g_T++;
    if ((g_T & 0xFFFFu) == 0) g_uif = true;                    /* 카운터 wrap → UIF */
}
static uint32_t rd_cnt(uint32_t *at) { hw_step(); *at = g_T; return g_T & 0xFFFFu; }
static uint32_t rd_ovf(void) { hw_step(); return g_ovf; }
static bool rd_uif(void) { hw_step(); return g_uif; }

static uint32_t ts_naive(uint32_t *at) { uint32_t hi = rd_ovf(); uint32_t lo = rd_cnt(at); return hi << 16 | lo; }
static uint32_t ts_retry(uint32_t *at) {                      /* 인터럽트가 켜져 있을 때의 표준 해법 */
    uint32_t hi, lo;
    do { hi = rd_ovf(); lo = rd_cnt(at); } while (hi != rd_ovf());
    return hi << 16 | lo;
}
static uint32_t ts_masked(uint32_t *at) {                     /* 인터럽트가 꺼져 있을 때: UIF 를 직접 본다 */
    uint32_t hi = rd_ovf(); uint32_t lo = rd_cnt(at);
    if (rd_uif() && lo < 0x8000u) hi++;                       /* wrap 은 났는데 ISR 이 아직 → lo 가 작으면 새 바퀴 */
    return hi << 16 | lo;
}
typedef uint32_t (*ts_fn)(uint32_t *);
static int p4_errors(ts_fn f, bool irq_on) {                 /* overflow 근처 시작점 40개에서 틀린 횟수 */
    int bad = 0;
    for (uint32_t s = 0x1FFF0u; s < 0x20018u; s++) {
        g_T = s; g_ovf = s >> 16; g_uif = false; g_irq_on = irq_on;
        uint32_t at, v = f(&at);
        if (v != at) bad++;                                    /* 정답 = CNT 를 읽은 순간의 진짜 시간 */
    }
    return bad;
}
static void test_p4(void) {
    printf("-- P4 hi/lo 타임스탬프\n");
    int n_on = p4_errors(ts_naive, true), n_off = p4_errors(ts_naive, false);
    int r_on = p4_errors(ts_retry, true), r_off = p4_errors(ts_retry, false);
    int m_off = p4_errors(ts_masked, false), m_on = p4_errors(ts_masked, true);
    printf("      errors /40: naive on=%d off=%d | retry on=%d off=%d | masked on=%d off=%d\n",
           n_on, n_off, r_on, r_off, m_on, m_off);
    CHECK(n_on > 0 && n_off > 0);       /* 순진한 읽기는 둘 다 틀린다 */
    CHECK(r_on == 0);                   /* 다시 읽기 루프: 인터럽트 켜짐에서 정확 */
    CHECK(r_off > 0);                   /* ...하지만 잠금 안에서는 ovf 가 안 변하니 루프가 아무것도 못 잡는다 */
    CHECK(m_off == 0);                  /* UIF 확인: 잠금 안에서 정확 */
}

int main(void) {
    test_p1();
    test_p2();
    test_p3();
    test_p4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
