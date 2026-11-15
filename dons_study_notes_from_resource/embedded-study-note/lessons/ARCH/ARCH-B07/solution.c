/* ARCH-B07 하드웨어 FSM: Moore vs Mealy 시퀀스 검출기 (1011, 겹침 허용)
 *
 *   Part A  상태 전이표 4개 (moore_next/out, mealy_next/out) + 두 단계(two-phase) 클럭 시뮬레이터
 *           → 사이클별 텍스트 파형, "Mealy 출력이 Moore 보다 정확히 1사이클 먼저" 를 전수 테스트로 확인
 *   Part B  겹침/비겹침, 동기 리셋, Mealy 출력을 FF 로 한 번 더 잡은 registered Mealy == Moore,
 *           반 사이클 모델에서 Mealy 출력 글리치
 *   Part C  임의 패턴(≤16비트) Mealy 표 자동 생성 (KMP 오토마톤), DMA 버퍼 경계를 넘는 sync word 검출,
 *           바이트 단위 표(256 x 상태)로 8비트씩 처리
 *
 * 모델의 시간 정의 (md §1 그림 참고):
 *   edge k   = k 번째 클럭 상승 에지. 사이클 k = edge k 와 edge k+1 사이.
 *   in[k]    = 사이클 k 동안 안정된 입력. edge k+1 에서 플립플롭이 샘플한다.
 *   S[k]     = 사이클 k 동안의 상태 레지스터 값 (edge k 에서 커밋됨). S[0] = 리셋 상태 0.
 *   Moore    out[k] = f(S[k])            — 상태만 본다 → 레지스터 출력
 *   Mealy    out[k] = g(S[k], in[k])     — 같은 사이클의 입력을 조합논리로 바로 본다
 *   registered Mealy q[k] = Mealy out[k-1], q[0] = 0 — 출력 FF 하나 추가
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXC 64            /* 시뮬레이션 최대 사이클 수 */
enum { MOORE = 0, MEALY = 1 };
enum { MOORE_NSTATES = 5, MEALY_NSTATES = 4 };

/* ======================= Part A — 상태 전이표 ======================= */
/* Moore 상태: 0="" 1="1" 2="10" 3="101" 4="1011"(출력 1)
 * Mealy 상태: 0="" 1="1" 2="10" 3="101"  (S3 에서 입력 1 이 들어오는 그 사이클에 출력 1)
 * overlap=true  : 1011011 에서 2번 검출 (끝의 1 이 다음 패턴의 첫 1 로 재사용)
 * overlap=false : 검출 직후 처음부터 다시 (1011011 → 1번)                              */
static uint8_t moore_next(uint8_t s, uint8_t in, bool overlap) {
    static const uint8_t T[MOORE_NSTATES][2] = {
        /* S0 */ {0, 1}, /* S1 */ {2, 1}, /* S2 */ {0, 3}, /* S3 */ {2, 4}, /* S4 */ {2, 1},
    };
    if (s == 4 && !overlap) return in ? 1 : 0;   /* 이미 쓴 비트는 재사용하지 않는다 */
    return T[s][in & 1u];
}
static uint8_t moore_out(uint8_t s) {
    return s == 4;
}
static uint8_t mealy_next(uint8_t s, uint8_t in, bool overlap) {
    static const uint8_t T[MEALY_NSTATES][2] = {
        /* S0 */ {0, 1}, /* S1 */ {2, 1}, /* S2 */ {0, 3}, /* S3 */ {2, 1},
    };
    if (s == 3 && in && !overlap) return 0;
    return T[s][in & 1u];
}
static uint8_t mealy_out(uint8_t s, uint8_t in) {
    return s == 3 && in;
}

/* ======================= Part A+B — 클럭 시뮬레이터 ======================= */
typedef struct {
    int     n;
    uint8_t in[MAXC], rst[MAXC];
    uint8_t state[MAXC];   /* S[k]                                   */
    uint8_t out[MAXC];     /* 사이클 k 의 출력 (Moore 또는 Mealy)    */
    uint8_t q[MAXC];       /* registered 출력: out 을 FF 하나 더 통과 */
} trace_t;

/* rst == NULL 이면 리셋 없음. rst[k]=1 인 사이클은 동기 리셋: edge k+1 에서 S ← 0, 그 사이클의 입력은 버리고
 * Mealy 조합 출력도 0 으로 가린다. 반드시 두 단계: ① 현재 값으로 출력·다음 상태 계산 ② edge 에서 한꺼번에 커밋. */
static void simulate(int kind, const uint8_t *in, const uint8_t *rst, int n, bool overlap, trace_t *tr) {
    uint8_t S = 0, Q = 0;                       /* 리셋 직후 상태 레지스터, 출력 레지스터 */
    tr->n = n;
    for (int k = 0; k < n; k++) {
        uint8_t r = rst ? rst[k] : 0;
        tr->in[k] = in[k]; tr->rst[k] = r;
        /* phase 1: 조합 논리 — 현재 레지스터 값(S, Q)과 입력만 본다 */
        uint8_t out  = (kind == MOORE) ? moore_out(S) : (uint8_t)(!r && mealy_out(S, in[k]));
        uint8_t next = r ? 0 : (kind == MOORE ? moore_next(S, in[k], overlap) : mealy_next(S, in[k], overlap));
        tr->state[k] = S; tr->out[k] = out; tr->q[k] = Q;
        /* phase 2: 클럭 에지 — 모든 FF 가 동시에 커밋 */
        S = next;
        Q = out;
    }
}

/* ======================= Part B — 반 사이클 모델: Mealy 글리치 ======================= */
/* 사이클 k 의 앞 반에는 입력이 a[k], 뒤 반(에지 직전, setup 을 만족하는 값)에는 b[k].
 * FF 는 b[k] 만 샘플한다. Mealy 조합 출력은 반 사이클마다 바뀐다 → half[2k], half[2k+1].
 * 글리치 = 앞 반에서 1 이었다가 뒤 반에서 0 이 되는 가짜 펄스. 그 수를 돌려준다. */
static int mealy_half_cycle(const uint8_t *a, const uint8_t *b, int n, uint8_t *half) {
    uint8_t S = 0;
    int glitches = 0;
    for (int k = 0; k < n; k++) {
        half[2 * k]     = mealy_out(S, a[k]);
        half[2 * k + 1] = mealy_out(S, b[k]);
        if (half[2 * k] && !half[2 * k + 1]) glitches++;
        S = mealy_next(S, b[k], true);
    }
    return glitches;
}

/* ======================= Part C — 임의 패턴 Mealy 표 (KMP 오토마톤) ======================= */
#define MAXPAT 16
typedef struct {
    int     len;                      /* 패턴 길이 1..16 = 상태 수 */
    uint8_t next[MAXPAT][2];
    uint8_t out[MAXPAT][2];
} fsm_tab_t;

static int pat_bit(uint32_t pat, int len, int i) { return (int)((pat >> (len - 1 - i)) & 1u); }   /* MSB 먼저 */

/* 상태 s = "패턴의 앞 s비트까지 맞았다". 입력 c 를 붙인 문자열 t(길이 s+1)의 접미사 중
 * 패턴 접두사와 같은 가장 긴 것(단, 길이 < len)이 다음 상태. t 전체가 패턴이면 출력 1 (겹침 허용). */
static void kmp_build(uint32_t pat, int len, fsm_tab_t *t) {
    t->len = len;
    for (int s = 0; s < len; s++) {
        for (int c = 0; c < 2; c++) {
            int tb[MAXPAT + 1];
            for (int i = 0; i < s; i++) tb[i] = pat_bit(pat, len, i);
            tb[s] = c;
            int tl = s + 1;
            t->out[s][c] = (uint8_t)(tl == len && tb[s] == pat_bit(pat, len, s));
            int k = tl < len ? tl : len - 1;
            for (; k > 0; k--) {
                bool ok = true;
                for (int i = 0; i < k && ok; i++) ok = (tb[tl - k + i] == pat_bit(pat, len, i));
                if (ok) break;
            }
            t->next[s][c] = (uint8_t)k;
        }
    }
}

/* 바이트 버퍼를 MSB 먼저 1비트씩 먹인다. *state 는 호출 사이에 이어진다 (버퍼 경계를 넘는 패턴). 검출 수 반환. */
static int scan_bits(const fsm_tab_t *t, const uint8_t *buf, size_t n, uint8_t *state) {
    int cnt = 0;
    uint8_t s = *state;
    for (size_t i = 0; i < n; i++) {
        for (int b = 7; b >= 0; b--) {
            int c = (buf[i] >> b) & 1;
            cnt += t->out[s][c];
            s = t->next[s][c];
        }
    }
    *state = s;
    return cnt;
}

/* 8비트 묶음 표: (상태, 바이트) → (다음 상태, 그 바이트 안의 검출 수). 펌웨어에서 비트 루프를 없앤다. */
typedef struct { uint8_t next[MAXPAT][256]; uint8_t cnt[MAXPAT][256]; } byte_tab_t;

static void byte_tab_build(const fsm_tab_t *t, byte_tab_t *bt) {
    for (int s = 0; s < t->len; s++) {
        for (int v = 0; v < 256; v++) {
            uint8_t st = (uint8_t)s, byte = (uint8_t)v;
            bt->cnt[s][v]  = (uint8_t)scan_bits(t, &byte, 1, &st);
            bt->next[s][v] = st;
        }
    }
}

/* 표가 채워졌다는 가정 아래의 빠른 경로 (TODO 아님) */
static int scan_bytes_fast(const fsm_tab_t *t, const byte_tab_t *bt, const uint8_t *buf, size_t n, uint8_t *state) {
    int cnt = 0;
    uint8_t s = *state;
    (void)t;
    for (size_t i = 0; i < n; i++) { cnt += bt->cnt[s][buf[i]]; s = bt->next[s][buf[i]]; }
    *state = s;
    return cnt;
}

/* ======================= 파형 출력 (주어짐) ======================= */
static void row_levels(const char *name, const uint8_t *v, int n) {
    printf("%-9s", name);
    for (int k = 0; k < n; k++) printf("%s", v[k] ? "‾‾‾‾" : "____");
    printf("\n");
}
static void print_waveform(const trace_t *mo, const trace_t *me) {
    int n = mo->n;
    printf("%-9s", "cycle");
    for (int k = 0; k < n; k++) printf("%-4d", k);
    printf("\n%-9s", "clk");
    for (int k = 0; k < n; k++) printf("/‾\\_");
    printf("\n");
    row_levels("in", mo->in, n);
    printf("%-9s", "in(bit)");
    for (int k = 0; k < n; k++) printf("%-4d", mo->in[k]);
    printf("\n%-9s", "Moore S");
    for (int k = 0; k < n; k++) printf("S%-3d", mo->state[k]);
    printf("\n");
    row_levels("Moore o", mo->out, n);
    printf("%-9s", "Mealy S");
    for (int k = 0; k < n; k++) printf("S%-3d", me->state[k]);
    printf("\n");
    row_levels("Mealy o", me->out, n);
    row_levels("Mealy q", me->q, n);
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

static uint32_t rng = 0x2545F491u;                    /* xorshift32, 고정 시드 → 매번 같은 결과 */
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* 참조 검출기: in[k-3..k] == 1011 이면 k 에서 검출 (겹침). 리셋이 있으면 그 4비트 안에 rst 가 없어야 한다. */
static int ref_detect(const uint8_t *in, const uint8_t *rst, int k) {
    if (k < 3) return 0;
    for (int i = k - 3; i <= k; i++) if (rst && rst[i]) return 0;
    return in[k - 3] == 1 && in[k - 2] == 0 && in[k - 1] == 1 && in[k] == 1;
}
/* 비겹침 참조: 왼쪽부터 탐욕적으로, 검출된 4비트는 다시 쓰지 않는다 */
static void ref_nonoverlap(const uint8_t *in, int n, uint8_t *det) {
    int last = -1;
    for (int k = 0; k < n; k++) {
        det[k] = 0;
        if (ref_detect(in, NULL, k) && k - 3 > last) { det[k] = 1; last = k; }
    }
}
/* 비트 버퍼에서 패턴 출현 수 (겹침, MSB 먼저) */
static int ref_count(uint32_t pat, int len, const uint8_t *buf, size_t n) {
    int cnt = 0;
    uint32_t win = 0, mask = (len == 32) ? 0xFFFFFFFFu : ((1u << len) - 1u);
    for (size_t i = 0; i < n * 8; i++) {
        win = (win << 1) | ((buf[i / 8] >> (7 - i % 8)) & 1u);
        if (i + 1 >= (size_t)len && (win & mask) == pat) cnt++;
    }
    return cnt;
}

static void bits_from_str(const char *s, uint8_t *v, int *n) {
    *n = 0;
    for (; *s; s++) if (*s == '0' || *s == '1') v[(*n)++] = (uint8_t)(*s - '0');
}
static void positions(const trace_t *t, char *buf, size_t cap) {   /* 출력이 1 인 사이클 번호 "3,6,10," */
    buf[0] = 0;
    for (int k = 0; k < t->n; k++) if (t->out[k]) snprintf(buf + strlen(buf), cap - strlen(buf), "%d,", k);
}

int main(void) {
    trace_t mo, me;
    uint8_t in[MAXC], rst[MAXC];
    int n;
    char pos[128];

    /* ---------------- Part A ---------------- */
    bits_from_str("1011 0111 0110 0", in, &n);          /* 13 사이클, 마지막 0 은 Moore 출력을 보려는 여유 */
    simulate(MOORE, in, NULL, n, true, &mo);
    simulate(MEALY, in, NULL, n, true, &me);
    print_waveform(&mo, &me);
    positions(&me, pos, sizeof pos); CHECK(strcmp(pos, "3,6,10,") == 0);    /* Mealy: 마지막 1 이 들어오는 그 사이클 */
    positions(&mo, pos, sizeof pos); CHECK(strcmp(pos, "4,7,11,") == 0);    /* Moore: 다음 사이클 */
    CHECK(mo.state[3] == 3 && mo.state[4] == 4 && mo.state[5] == 2);   /* 1011 → S4, 이어서 0 → "10" = S2 */
    CHECK(me.state[3] == 3 && me.state[4] == 1);                       /* Mealy 는 S3 --1/1--> S1 (겹침) */

    /* 전수: 길이 1..14 의 모든 비트열 (32766개) */
    long bad_mealy = 0, bad_moore = 0, bad_shift = 0, streams = 0;
    for (int len = 1; len <= 14; len++) {
        for (uint32_t v = 0; v < (1u << len); v++) {
            for (int k = 0; k < len; k++) in[k] = (uint8_t)((v >> k) & 1u);
            simulate(MOORE, in, NULL, len, true, &mo);
            simulate(MEALY, in, NULL, len, true, &me);
            streams++;
            for (int k = 0; k < len; k++) {
                if (me.out[k] != ref_detect(in, NULL, k)) bad_mealy++;
                if (mo.out[k] != (k > 0 && ref_detect(in, NULL, k - 1))) bad_moore++;
                if (k + 1 < len && mo.out[k + 1] != me.out[k]) bad_shift++;   /* 핵심: 정확히 1사이클 차이 */
            }
        }
    }
    printf("exhaustive: %ld streams, mismatches mealy=%ld moore=%ld shift=%ld\n", streams, bad_mealy, bad_moore, bad_shift);
    CHECK(streams == 32766);
    CHECK(bad_mealy == 0);
    CHECK(bad_moore == 0);
    CHECK(bad_shift == 0);

    /* ---------------- Part B ---------------- */
    bits_from_str("1011011", in, &n);
    simulate(MEALY, in, NULL, n, true, &me);  positions(&me, pos, sizeof pos); CHECK(strcmp(pos, "3,6,") == 0);
    simulate(MEALY, in, NULL, n, false, &me); positions(&me, pos, sizeof pos); CHECK(strcmp(pos, "3,") == 0);
    in[n++] = 0;
    simulate(MOORE, in, NULL, n, false, &mo); positions(&mo, pos, sizeof pos); CHECK(strcmp(pos, "4,") == 0);

    /* 비겹침 전수 (길이 12) + registered Mealy == Moore (겹침/비겹침 모두) */
    long bad_nov = 0, bad_q = 0;
    for (uint32_t v = 0; v < (1u << 12); v++) {
        uint8_t det[MAXC];
        for (int k = 0; k < 12; k++) in[k] = (uint8_t)((v >> k) & 1u);
        ref_nonoverlap(in, 12, det);
        for (int ov = 0; ov < 2; ov++) {
            simulate(MOORE, in, NULL, 12, ov, &mo);
            simulate(MEALY, in, NULL, 12, ov, &me);
            for (int k = 0; k < 12; k++) {
                if (!ov && me.out[k] != det[k]) bad_nov++;
                if (!ov && mo.out[k] != (k > 0 && det[k - 1])) bad_nov++;
                if (me.q[k] != mo.out[k]) bad_q++;
            }
        }
    }
    CHECK(bad_nov == 0);
    CHECK(bad_q == 0);

    /* 동기 리셋: 1011 의 중간(사이클 1)에 리셋 → 검출 없음. 무작위 리셋 2000 회도 참조와 같아야 함 */
    bits_from_str("1011 0", in, &n);
    memset(rst, 0, sizeof rst); rst[1] = 1;
    simulate(MEALY, in, rst, n, true, &me); positions(&me, pos, sizeof pos); CHECK(pos[0] == 0);
    simulate(MOORE, in, rst, n, true, &mo); positions(&mo, pos, sizeof pos); CHECK(pos[0] == 0);
    long bad_rst = 0;
    for (int it = 0; it < 2000; it++) {
        n = 40;
        for (int k = 0; k < n; k++) { in[k] = (uint8_t)(xs32() & 1u); rst[k] = (xs32() % 16u) == 0; }
        simulate(MOORE, in, rst, n, true, &mo);
        simulate(MEALY, in, rst, n, true, &me);
        for (int k = 0; k < n; k++) {
            if (me.out[k] != ref_detect(in, rst, k)) bad_rst++;
            if (mo.out[k] != (k > 0 && ref_detect(in, rst, k - 1))) bad_rst++;
            if (me.q[k] != mo.out[k]) bad_rst++;
        }
    }
    CHECK(bad_rst == 0);

    /* 반 사이클 글리치: 101 다음 사이클에 입력이 앞 반 1 → 뒤 반 0 으로 바뀌면 Mealy 출력에 가짜 펄스 */
    {
        uint8_t a[6] = {1, 0, 1, 1, 0, 0}, b[6] = {1, 0, 1, 0, 1, 1}, half[12];
        int g = mealy_half_cycle(a, b, 6, half);
        printf("half-cycle   a: 1 0 1 1 0 0   b: 1 0 1 0 1 1\nMealy half ");
        for (int i = 0; i < 12; i++) printf("%s", half[i] ? "‾‾" : "__");
        printf("   glitches=%d\n", g);
        CHECK(g == 1);
        CHECK(half[6] == 1 && half[7] == 0);           /* 사이클 3 앞 반: 1, 뒤 반: 0 */
        uint8_t c[4] = {1, 0, 1, 1};
        CHECK(mealy_half_cycle(c, c, 4, half) == 0);   /* 입력이 사이클 내내 안정 → 글리치 없음 */
    }

    /* ---------------- Part C ---------------- */
    fsm_tab_t t;
    kmp_build(0xBu, 4, &t);
    int same = 1;
    for (int s = 0; s < 4; s++)
        for (int c = 0; c < 2; c++)
            if (t.next[s][c] != mealy_next((uint8_t)s, (uint8_t)c, true) || t.out[s][c] != mealy_out((uint8_t)s, (uint8_t)c)) same = 0;
    CHECK(same && t.out[3][1] == 1 && t.next[3][1] == 1);   /* 자동 생성한 1011 표 == 손으로 그린 Mealy 표 */

    long bad_rand = 0;
    for (int it = 0; it < 400; it++) {
        int len = 1 + (int)(xs32() % 16u);
        uint32_t pat = xs32() & ((1u << len) - 1u);
        if (it % 4 == 0) pat = (len >= 2) ? ((1u << len) - 1u) ^ 1u : 1u;   /* 1110 류: 자기겹침 큰 패턴 */
        uint8_t buf[64];
        for (int i = 0; i < 64; i++) buf[i] = (uint8_t)xs32();
        if (it % 2) for (int i = 0; i < 64; i++) buf[i] = (i & 1) ? 0xFF : (uint8_t)(xs32() | 0xF0u);  /* 1 이 많은 버퍼 */
        kmp_build(pat, len, &t);
        uint8_t st = 0;
        if (scan_bits(&t, buf, 64, &st) != ref_count(pat, len, buf, 64)) bad_rand++;
    }
    CHECK(bad_rand == 0);

    /* sync word 0xD391 을 비트 오프셋 37 에 심은 64바이트 버퍼를 DMA 처럼 두 조각으로 나눠 받는다 (모든 분할점) */
    {
        uint8_t buf[64] = {0};
        for (int i = 0; i < 64; i++) buf[i] = (uint8_t)(0x55u ^ (uint8_t)i);
        for (int i = 0; i < 16; i++) {
            int bitpos = 37 + i, v = (0xD391u >> (15 - i)) & 1u;
            buf[bitpos / 8] = (uint8_t)((buf[bitpos / 8] & ~(0x80u >> (bitpos % 8))) | (v ? (0x80u >> (bitpos % 8)) : 0u));
        }
        kmp_build(0xD391u, 16, &t);
        int whole = ref_count(0xD391u, 16, buf, 64);
        long bad_split = 0, bad_reset_state = 0;
        for (int cut = 0; cut <= 64; cut++) {
            uint8_t st = 0;
            int c1 = scan_bits(&t, buf, (size_t)cut, &st);
            int c2 = scan_bits(&t, buf + cut, (size_t)(64 - cut), &st);
            if (c1 + c2 != whole) bad_split++;
            uint8_t s1 = 0, s2 = 0;                                   /* 흔한 버그: 조각마다 상태를 0 으로 */
            int d = scan_bits(&t, buf, (size_t)cut, &s1) + scan_bits(&t, buf + cut, (size_t)(64 - cut), &s2);
            if (d != whole) bad_reset_state++;
        }
        printf("sync 0xD391: whole=%d, split mismatches=%ld, (state-reset bug would miss at %ld cut points)\n",
               whole, bad_split, bad_reset_state);
        CHECK(whole == 1);
        CHECK(bad_split == 0);
    }

    /* 바이트 표 == 비트 루프 */
    {
        static byte_tab_t bt;
        long bad_bt = 0;
        for (int it = 0; it < 50; it++) {
            int len = 1 + (int)(xs32() % 16u);
            uint32_t pat = xs32() & ((1u << len) - 1u);
            uint8_t buf[128];
            for (int i = 0; i < 128; i++) buf[i] = (uint8_t)xs32();
            kmp_build(pat, len, &t);
            byte_tab_build(&t, &bt);
            uint8_t s1 = 0, s2 = 0;
            int a = scan_bits(&t, buf, 128, &s1), b = scan_bytes_fast(&t, &bt, buf, 128, &s2);
            if (a != b || s1 != s2 || a != ref_count(pat, len, buf, 128)) bad_bt++;
        }
        CHECK(bad_bt == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
