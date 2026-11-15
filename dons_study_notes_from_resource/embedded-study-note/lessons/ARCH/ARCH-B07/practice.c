/* ARCH-B07 추가 연습 — 한 번에 검증
 *
 *   연습 1  Moore 1011 검출기를 one-hot(FF 5개) 다음 상태 식으로 → 표 버전과 전수 비교
 *   연습 2  Mealy 1011 검출기를 이진 인코딩(FF 2개) 다음 상태 식으로 → 표 버전과 전수 비교
 *   연습 3  상승 에지 검출기: Mealy(FF 1개) vs Moore(상태 3개) — Moore 펄스가 1사이클 늦다
 *   연습 4  버튼 디바운서 (Moore, N=3 연속 샘플) — 출력은 바운스 동안 흔들리지 않는다
 *   연습 5  다른 패턴 1001 의 Mealy 표를 손으로 → 참조 검출기와 전수 비교
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

/* 본문의 표 (겹침 허용) — 비교 기준 */
static const uint8_t MOORE_T[5][2] = { {0, 1}, {2, 1}, {0, 3}, {2, 4}, {2, 1} };
static const uint8_t MEALY_T[4][2] = { {0, 1}, {2, 1}, {0, 3}, {2, 1} };

/* ---------------- 연습 1: one-hot Moore ---------------- */
/* q 의 비트 i = 상태 Si 에 있다. 리셋 = 0b00001. 각 FF 의 D 입력을 "어디서 어떤 입력으로 들어오나" 로 쓴다. */
static uint8_t onehot_next(uint8_t q, uint8_t in) {
    uint8_t q0 = q & 1u, q1 = (q >> 1) & 1u, q2 = (q >> 2) & 1u, q3 = (q >> 3) & 1u, q4 = (q >> 4) & 1u;
    uint8_t ni = (uint8_t)!in;
    uint8_t d0 = ni & (q0 | q2);             /* S0 --0-->, S2 --0--> S0          */
    uint8_t d1 = in & (q0 | q1 | q4);        /* S0,S1,S4 --1--> S1               */
    uint8_t d2 = ni & (q1 | q3 | q4);        /* S1,S3,S4 --0--> S2               */
    uint8_t d3 = in & q2;                    /* S2 --1--> S3                     */
    uint8_t d4 = in & q3;                    /* S3 --1--> S4                     */
    return (uint8_t)(d0 | d1 << 1 | d2 << 2 | d3 << 3 | d4 << 4);
}

/* ---------------- 연습 2: 이진 인코딩 Mealy (S0=00 S1=01 S2=10 S3=11) ---------------- */
static uint8_t bin_next(uint8_t s, uint8_t in, uint8_t *out) {
    uint8_t s1 = (s >> 1) & 1u, s0 = s & 1u, ni = (uint8_t)!in;
    uint8_t d1 = (uint8_t)((ni & s0) | (in & s1 & !s0));   /* 다음이 S2 또는 S3 */
    uint8_t d0 = in;                                      /* 다음이 S1 또는 S3 ⇔ 입력이 1 */
    *out = in & s1 & s0;
    return (uint8_t)(d1 << 1 | d0);
}

/* ---------------- 연습 3: 상승 에지 검출기 ---------------- */
/* Mealy: prev FF 하나. pulse[k] = in[k] & !prev */
static void edge_mealy(const uint8_t *in, int n, uint8_t *pulse) {
    uint8_t prev = 0;
    for (int k = 0; k < n; k++) { pulse[k] = in[k] & (uint8_t)!prev; prev = in[k]; }
}
/* Moore: LOW(0) / EDGE(1, 출력 1) / HIGH(2) */
static void edge_moore(const uint8_t *in, int n, uint8_t *pulse) {
    static const uint8_t T[3][2] = { {0, 1}, {0, 2}, {0, 2} };
    uint8_t s = 0;
    for (int k = 0; k < n; k++) { pulse[k] = s == 1; s = T[s][in[k]]; }
}

/* ---------------- 연습 4: 디바운서 (Moore) ---------------- */
/* 상태 = (안정 레벨 level, 다른 값이 연속으로 몇 번 왔나 cnt). 출력 = level (상태만 본다).
 * N 번 연속으로 반대 값이 샘플되면 그 에지에서 level 을 바꾼다. */
#define DEB_N 3
typedef struct { uint8_t level, cnt; } deb_t;
static uint8_t deb_step(deb_t *d, uint8_t in) {
    uint8_t out = d->level;                         /* phase 1: 현재 상태로 출력 */
    deb_t nx = *d;
    if (in == d->level) nx.cnt = 0;
    else if (++nx.cnt == DEB_N) { nx.level = in; nx.cnt = 0; }
    *d = nx;                                        /* phase 2: 커밋 */
    return out;
}

/* ---------------- 연습 5: 1001 Mealy (겹침) ---------------- */
/* 상태: 0="" 1="1" 2="10" 3="100". S3 --1/1--> S1 ("1001" 의 끝 1 = 새 접두사 "1") */
static const uint8_t P1001_T[4][2] = { {0, 1}, {2, 1}, {3, 1}, {0, 1} };
static uint8_t p1001_out(uint8_t s, uint8_t in) { return s == 3 && in; }

static int ref_pat(const uint8_t *in, int k, const char *p, int len) {
    if (k < len - 1) return 0;
    for (int i = 0; i < len; i++) if (in[k - len + 1 + i] != (uint8_t)(p[i] - '0')) return 0;
    return 1;
}

int main(void) {
    uint8_t in[16];

    /* 연습 1 + 2: 길이 1..14 전수 */
    long bad1 = 0, bad2 = 0, bad5 = 0, bad3 = 0;
    for (int len = 1; len <= 14; len++) {
        for (uint32_t v = 0; v < (1u << len); v++) {
            for (int k = 0; k < len; k++) in[k] = (uint8_t)((v >> k) & 1u);
            uint8_t s = 0, q = 1u, m = 0, b = 0, p = 0;
            for (int k = 0; k < len; k++) {
                if (q != (uint8_t)(1u << s)) bad1++;                 /* one-hot 이 표 상태와 같은 비트인가 */
                if (((q >> 4) & 1u) != (s == 4)) bad1++;             /* 출력 = q4 */
                uint8_t ob, om = (m == 3 && in[k]);
                if (b != m) bad2++;
                uint8_t nb = bin_next(b, in[k], &ob);
                if (ob != om) bad2++;
                if (p1001_out(p, in[k]) != ref_pat(in, k, "1001", 4)) bad5++;
                q = onehot_next(q, in[k]); s = MOORE_T[s][in[k]];
                b = nb;                    m = MEALY_T[m][in[k]];
                p = P1001_T[p][in[k]];
            }
            uint8_t pm[16], pr[16];
            edge_mealy(in, len, pm); edge_moore(in, len, pr);
            for (int k = 0; k < len; k++) {
                if (pm[k] != (in[k] && (k == 0 || !in[k - 1]))) bad3++;
                if (k + 1 < len && pr[k + 1] != pm[k]) bad3++;
                if (pr[0] != 0) bad3++;
            }
        }
    }
    CHECK(bad1 == 0);
    CHECK(bad2 == 0);
    CHECK(bad3 == 0);
    CHECK(bad5 == 0);

    /* 연습 4: 바운스 0 1 0 1 1 1 1 1 0 1 1 1 0 0 0 0 → 출력은 한 번만 0→1, 다시 한 번 1→0 */
    {
        const uint8_t bounce[16] = {0, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0, 0};
        uint8_t out[16];
        deb_t d = {0, 0};
        int changes = 0;
        printf("deb in : ");
        for (int k = 0; k < 16; k++) printf("%d", bounce[k]);
        printf("\ndeb out: ");
        for (int k = 0; k < 16; k++) { out[k] = deb_step(&d, bounce[k]); printf("%d", out[k]); if (k && out[k] != out[k - 1]) changes++; }
        printf("\n");
        CHECK(changes == 2);
        CHECK(out[5] == 0 && out[6] == 1);      /* 1 이 세 번(3,4,5) 연속 → edge 6 이후 출력 1 */
        CHECK(out[14] == 1 && out[15] == 0);    /* 0 이 세 번(12,13,14) 연속 → edge 15 이후 출력 0 */
        /* 성질: 출력이 바뀐 사이클 k 직전 N 개 입력은 모두 새 레벨이어야 한다 (무작위 5000 사이클) */
        uint32_t r = 0x1234567u; long bad4 = 0; deb_t e = {0, 0}; uint8_t hist[5000], o[5000];
        for (int k = 0; k < 5000; k++) {
            r ^= r << 13; r ^= r >> 17; r ^= r << 5;
            hist[k] = (r % 7u) < 3u;                /* 편향된 잡음 입력 */
            o[k] = deb_step(&e, hist[k]);
            if (k >= DEB_N && o[k] != o[k - 1])
                for (int i = 1; i <= DEB_N; i++) if (hist[k - i] != o[k]) bad4++;
        }
        CHECK(bad4 == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
