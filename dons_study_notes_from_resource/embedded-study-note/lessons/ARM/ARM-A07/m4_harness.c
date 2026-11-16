/* ENV: qemu */
/* ARM-A07 M4 레인 — Cortex-M4 (QEMU mps2-an386) 에서 같은 명세의 Q15 FIR 을 Thumb-2 DSP 명령으로.
 * 빌드·실행: run_qemu.sh m4  (m4_harness.c + m4_fir.S + m4_startup.c + m4.ld, -mcpu=cortex-m4 -mthumb)
 * 구현 3개를 명세(오라클, solution.c 축약판)와 비트 단위로 비교한다:
 *   fir_m4_q31   (m4_fir.S)  SMULBB + QDADD, 마지막 QADD 0x8000 + ASR      — 항상 명세와 같아야 한다
 *   fir_m4_smlad (여기, ACLE) SMLAD 로 두 탭씩, 포화 없는 32비트 합 + SSAT  — Σ|h| <= 32767 일 때만 같다
 *   기본 연산    ACLE __qadd/__qdbl/__qadd16/__ssat/__smlabb/__smlad  + m4_fir.S 의 m4_lmult/m4_mac/m4_round
 * Q 플래그(APSR.Q, sticky)는 q_get/q_clear (m4_fir.S) 로 읽고 지운다.
 */
#include <arm_acle.h>
#include <stdint.h>
#include <stddef.h>

/* ---- m4_startup.c ---- */
void sh_puts(const char *s);
void sh_hex(uint32_t v);
void sh_dec(int32_t v);
void check(int ok, const char *expr, int line);
extern unsigned g_pass, g_total;
#define CHECK(e) check((e) ? 1 : 0, #e, __LINE__)

/* ---- m4_fir.S ---- */
int32_t  m4_lmult(int32_t a, int32_t b);
int32_t  m4_mac(int32_t acc, int32_t a, int32_t b);
int32_t  m4_round(int32_t L);
int32_t  m4_ssat16_asr15(int32_t v);
int32_t  m4_smlabb(int32_t a, int32_t b, int32_t acc);
void     fir_m4_q31(const int16_t *x, size_t n, const int16_t *h, size_t T, int16_t *y);
uint32_t q_get(void);
void     q_clear(void);

#define FIR_MAXT 64
typedef void (*fir_fn)(const int16_t *, size_t, const int16_t *, size_t, int16_t *);

static uint32_t g_s = 0x12345678u;                                  /* xorshift32 (M4 에선 64비트가 비싸다) */
static uint32_t xs32(void) { g_s ^= g_s << 13; g_s ^= g_s >> 17; g_s ^= g_s << 5; return g_s; }
static int16_t r16(void) { return (int16_t)(uint16_t)xs32(); }

/* ---- 명세 오라클 (solution.c 축약판): int64 로 넓게 계산 후 포화 ---- */
static int64_t fdiv(int64_t v, int64_t d) { return v >= 0 ? v / d : -((-v + d - 1) / d); }
static int32_t o_sat32(int64_t v) { return (int32_t)(v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : v); }
static int16_t o_sat16(int64_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
static int32_t o_lmult(int16_t a, int16_t b) { return o_sat32(2 * (int64_t)a * b); }
static int32_t o_mac(int32_t acc, int16_t a, int16_t b) { return o_sat32((int64_t)acc + o_lmult(a, b)); }
static int16_t o_round(int32_t L) { return o_sat16(fdiv((int64_t)L + 0x8000, 65536)); }
static int16_t o_add16(int16_t a, int16_t b) { return o_sat16((int64_t)a + b); }
static void o_fir(const int16_t *x, size_t n, const int16_t *h, size_t T, int16_t *y) {
    for (size_t i = 0; i < n; i++) {
        int32_t acc = 0;
        for (size_t k = 0; k < T; k++) acc = o_mac(acc, h[k], x[i + T - 1 - k]);
        y[i] = o_round(acc);
    }
}

/* ---- 빠른 경로: SMLAD (두 탭/명령, 포화 없음) ----
 * 계수를 시간 역순으로 저장 (hr[j] = h[T-1-j]) → y[i] = Σ_j hr[j]·x[i+j]: x 와 hr 을 같은 방향으로 2개씩 읽는다.
 * T 가 홀수면 hr[T] = 0 으로 패딩 → x 를 하나 더 읽는다 (x 버퍼 끝 여유 1개, 곱해지는 계수는 0).
 * 출력 = sat16((S + 2^14) >> 15) = 명세의 round(2S) — 단 부분합 2S 가 한 번도 Q31 을 넘지 않을 때만. */
static int16_t g_hr[FIR_MAXT + 2];
static size_t  g_tp;
static void smlad_prepare(const int16_t *h, size_t T) {
    for (size_t j = 0; j < T; j++) g_hr[j] = h[T - 1 - j];
    g_tp = T;
    if (g_tp & 1) g_hr[g_tp++] = 0;
}
static uint32_t ld2(const int16_t *p) {                             /* 2×Q15 → 한 워드 (비정렬 가능: M4 LDR 은 허용) */
    return (uint32_t)(uint16_t)p[0] | (uint32_t)(uint16_t)p[1] << 16;
}
void fir_m4_smlad(const int16_t *x, size_t n, const int16_t *h, size_t T, int16_t *y) {
    smlad_prepare(h, T);
    const size_t tp = g_tp;                                         /* 지역 복사: 루프마다 다시 읽지 않게 */
    for (size_t i = 0; i < n; i++) {
        int32_t acc = 0;
        for (size_t j = 0; j < tp; j += 2)
            acc = __smlad((int16x2_t)ld2(x + i + j), (int16x2_t)ld2(g_hr + j), acc);   /* acc + lo·lo + hi·hi */
        y[i] = (int16_t)__ssat((acc + 0x4000) >> 15, 16);                             /* SSAT #16 */
    }
}

/* ---- 교차 ISA 골든 (solution.c 와 똑같은 코드) ---- */
#define GOLDEN_FNV 0xc93a6433u
static uint32_t golden_hash(fir_fn f) {
    static int16_t gx[40 + FIR_MAXT], gh[FIR_MAXT], gy[40];
    uint32_t s = 2463534242u, hash = 2166136261u;
    for (int set = 0; set < 24; set++) {
        size_t T = (size_t)(1 + (set * 13) % FIR_MAXT);
        for (size_t k = 0; k < T; k++) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            int16_t v = (int16_t)(uint16_t)s;
            gh[k] = set % 3 == 0 ? (int16_t)(v / (int32_t)T) : set % 3 == 1 ? v : (int16_t)((v & 1) ? -32768 : 32767);
        }
        for (size_t j = 0; j < 40 + T - 1; j++) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            gx[j] = (set & 4) ? (int16_t)((s & 1) ? -32768 : 32767) : (int16_t)(uint16_t)(s >> 7);
        }
        f(gx, 40, gh, T, gy);
        for (int i = 0; i < 40; i++) hash = (hash ^ (uint16_t)gy[i]) * 16777619u;
    }
    return hash;
}

static const int16_t E16[] = { -32768, -32767, -16384, -181, -2, -1, 0, 1, 2, 181, 16384, 32766, 32767 };
#define NE16 (sizeof E16 / sizeof E16[0])
static const int32_t E32[] = { INT32_MIN, INT32_MIN + 1, -0x40000000, -0x8001, -0x8000, -1, 0, 1, 0x7FFF,
                               0x8000, 0x3FFFFFFF, 0x7FFF7FFF, 0x7FFF8000, INT32_MAX };
#define NE32 (sizeof E32 / sizeof E32[0])

enum { FILT_SAFE, FILT_HOT, FILT_MIN, FILT_ALT, NFILT };
enum { SIG_RAND, SIG_SQUARE, SIG_MATCHED, SIG_MIN, NSIG };
static void make_filter(int16_t *h, size_t T, int kind) {
    for (size_t k = 0; k < T; k++) {
        if (kind == FILT_SAFE) { int32_t m = (int32_t)(32767 / T); h[k] = (int16_t)((int32_t)(xs32() % (uint32_t)(2 * m + 1)) - m); }
        else if (kind == FILT_HOT) h[k] = r16();
        else if (kind == FILT_MIN) h[k] = -32768;
        else h[k] = (k & 1) ? -32768 : 32767;
    }
}
static void make_signal(int16_t *x, size_t len, const int16_t *h, size_t T, int kind) {
    size_t period = 2 + xs32() % 19;
    for (size_t j = 0; j < len; j++) {
        if (kind == SIG_RAND) x[j] = r16();
        else if (kind == SIG_SQUARE) x[j] = ((j / (period / 2 + 1)) & 1) ? -32768 : 32767;
        else if (kind == SIG_MATCHED) x[j] = h[(T - 1) - (j % T)] >= 0 ? 32767 : -32768;
        else x[j] = -32768;
    }
}
/* filters: 0 = 안전한 필터만 (smlad 전제), 1 = 4종 */
static int suite_fir(fir_fn f, int all_filters) {
    static const size_t TS[] = { 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 64 };
    static const size_t NS[] = { 0, 1, 2, 3, 5, 8, 17, 33 };
    static int16_t x[33 + FIR_MAXT + 1], y[33 + 1], want[33];
    int16_t h[FIR_MAXT];
    int fails = 0;
    for (size_t ti = 0; ti < sizeof TS / sizeof TS[0]; ti++)
    for (size_t ni = 0; ni < sizeof NS / sizeof NS[0]; ni++)
    for (int fk = 0; fk < (all_filters ? NFILT : 1); fk++)
    for (int sk = 0; sk < NSIG; sk++) {
        size_t T = TS[ti], n = NS[ni];
        make_filter(h, T, fk);
        make_signal(x, n + T, h, T, sk);                            /* +1 여유 (smlad 패딩) */
        y[n] = 0x5A5A;
        o_fir(x, n, h, T, want);
        f(x, n, h, T, y);
        for (size_t i = 0; i < n; i++) fails += y[i] != want[i];
        fails += y[n] != 0x5A5A;
    }
    return fails;
}

int main(void) {
    sh_puts("ARM-A07 M4 lane: Cortex-M4 DSP (SMULBB/QDADD/QADD/SSAT/SMLAD) vs spec\n");
    /* ---------- 기본 연산: 명령 = 명세 ---------- */
    {
        int fails = 0;
        for (int32_t a = -32768; a <= 32767; a++)                   /* 모든 a × 경계 b */
            for (size_t j = 0; j < NE16; j++) {
                int16_t b = E16[j];
                fails += m4_lmult(a, b) != o_lmult((int16_t)a, b);
                fails += __qdbl(__smlabb(a, b, 0)) != o_lmult((int16_t)a, b);          /* ACLE 로 같은 것 */
                fails += (int16_t)__qadd16((uint16_t)a | (uint32_t)(uint16_t)b << 16,
                                           (uint16_t)b | (uint32_t)(uint16_t)a << 16) != o_add16((int16_t)a, b);
            }
        CHECK(fails == 0);
    }
    {
        int fails = 0;
        for (size_t i = 0; i < NE32; i++) for (size_t j = 0; j < NE16; j++) for (size_t k = 0; k < NE16; k++)
            fails += m4_mac(E32[i], E16[j], E16[k]) != o_mac(E32[i], E16[j], E16[k]);
        for (size_t i = 0; i < NE32; i++) fails += (int16_t)m4_round(E32[i]) != o_round(E32[i]);
        for (int t = 0; t < 20000; t++) {
            int32_t L = (int32_t)xs32(); int16_t a = r16(), b = r16();
            fails += m4_mac(L, a, b) != o_mac(L, a, b);
            fails += (int16_t)m4_round(L) != o_round(L);
            fails += __ssat(L, 16) != o_sat16(L);
            fails += m4_ssat16_asr15(L) != o_sat16(fdiv(L, 32768));
        }
        CHECK(fails == 0);
    }
    CHECK(m4_lmult(-32768, -32768) == INT32_MAX && m4_mac(INT32_MIN, -32768, -32768) == -1);
    CHECK(m4_round(0x7FFF8000) == 32767 && m4_round(-0x8001) == -1 && __ssat(40000, 16) == 32767 && __ssat(-40000, 16) == -32768);

    /* ---------- Q 플래그: 포화 명령이 세우고, 지울 때까지 남는다 ---------- */
    {
        volatile int32_t big = INT32_MAX, one = 1, two = 2;          /* 상수 접기 방지 */
        q_clear();
        int32_t r0 = __qadd(one, two);       uint32_t q0 = q_get();   /* 포화 없음 → Q=0 */
        int32_t r1 = __qadd(big, one);       uint32_t q1 = q_get();   /* 포화 → Q=1 */
        int32_t r2 = __qadd(one, one);       uint32_t q2 = q_get();   /* 포화 없음, 그래도 Q=1 (sticky) */
        q_clear();                           uint32_t q3 = q_get();
        int32_t r4 = __ssat(big, 16);        uint32_t q4 = q_get();   /* SSAT 가 자르면 Q=1 */
        q_clear();
        int32_t r5 = __ssat(one, 16);        uint32_t q5 = q_get();   /* 안 자르면 Q=0 */
        int32_t r6 = m4_smlabb(0x7FFF, 0x7FFF, big); uint32_t q6 = q_get();  /* SMLABB: 누적이 넘치면 감싸고 Q=1 */
        q_clear();
        int32_t r7 = __smlad((int16x2_t)0x80008000u, (int16x2_t)0x80008000u, 0); uint32_t q7 = q_get();   /* 2·2^30 = 2^31 */
        q_clear();
        int32_t r8 = m4_lmult(-32768, -32768); uint32_t q8 = q_get(); /* SMULBB 는 안 넘치고, QADD(p,p) 가 포화 */
        sh_puts("Q flag: qadd ok/sat/ok "); sh_dec((int32_t)q0); sh_dec((int32_t)q1); sh_dec((int32_t)q2);
        sh_puts(", clear "); sh_dec((int32_t)q3); sh_puts(", ssat sat/ok "); sh_dec((int32_t)q4); sh_dec((int32_t)q5);
        sh_puts(", smlabb ovf "); sh_dec((int32_t)q6); sh_puts(" r="); sh_hex((uint32_t)r6);
        sh_puts(", smlad 2^31 "); sh_dec((int32_t)q7); sh_puts(" r="); sh_hex((uint32_t)r7);
        sh_puts(", lmult(-1,-1) "); sh_dec((int32_t)q8); sh_puts("\n");
        CHECK(r0 == 3 && q0 == 0 && r1 == INT32_MAX && q1 == 1 && r2 == 2 && q2 == 1 && q3 == 0);
        CHECK(r4 == 32767 && q4 == 1 && r5 == 1 && q5 == 0);
        CHECK(q6 == 1 && r6 == (int32_t)(0x7FFFFFFFu + 0x3FFF0001u));    /* 감싼 값 = 0xBFFF0000 */
        CHECK(q7 == 1 && r7 == INT32_MIN);                               /* 2^31 이 감싸서 INT32_MIN */
        CHECK(r8 == INT32_MAX && q8 == 1);
    }

    /* ---------- FIR: 명세와 비트 단위 ---------- */
    CHECK(suite_fir(fir_m4_q31, 1) == 0);                            /* 필터 4종 × 신호 4종 */
    CHECK(suite_fir(fir_m4_smlad, 0) == 0);                          /* 전제(Σ|h| <= 32767)를 지키면 같다 */
    {
        static const int16_t H[4] = { 32767, -32768, -32768, 16384 }, X[4] = { 3, -32768, 32767, -32768 };
        int16_t y[1] = { 0 };
        fir_m4_q31(X, 1, H, 4, y);
        CHECK(y[0] == 1);                                            /* 맥 레인과 같은 손 계산 (포화 두 번) */
    }
    uint32_t gq = golden_hash(fir_m4_q31);
    sh_puts("golden FNV-1a (fir_m4_q31) = "); sh_hex(gq); sh_puts("\n");
    CHECK(gq == GOLDEN_FNV && golden_hash(o_fir) == GOLDEN_FNV);     /* 맥 NEON/asm 과 같은 해시 */

    /* ---------- 전제를 어기면: SMLAD 경로는 다른 필터가 된다 ---------- */
    {
        static int16_t x[16 + 8 + 1], ya[16], yb[16], h[8];
        for (int k = 0; k < 8; k++) h[k] = 16384;                    /* Σ|h| = 131072 (= 4.0) */
        for (int j = 0; j < 25; j++) x[j] = 32767;
        q_clear();
        fir_m4_smlad(x, 16, h, 8, ya);
        uint32_t q_ovf = q_get();
        o_fir(x, 16, h, 8, yb);
        sh_puts("hot DC: smlad y="); sh_dec(ya[0]); sh_puts(" spec y="); sh_dec(yb[0]); sh_puts(" Q="); sh_dec((int32_t)q_ovf); sh_puts("\n");
        CHECK(ya[0] != yb[0] && q_ovf == 1);                         /* 32비트가 넘쳐 감쌌다 → Q 가 알려 준다 */

        static const int16_t h2[4] = { 32767, 32767, -32767, -32767 };   /* Σ|h| ≈ 4.0, 그러나 합은 0 */
        for (int j = 0; j < 25; j++) x[j] = 32767;
        q_clear();
        fir_m4_smlad(x, 4, h2, 4, ya);
        uint32_t q_mid = q_get();
        o_fir(x, 4, h2, 4, yb);
        sh_puts("cancel: smlad y="); sh_dec(ya[0]); sh_puts(" spec y="); sh_dec(yb[0]); sh_puts(" Q="); sh_dec((int32_t)q_mid); sh_puts("\n");
        CHECK(ya[0] == 0 && yb[0] != 0 && q_mid == 0);               /* Q=0 인데도 명세와 다르다! */
    }

    sh_dec((int32_t)g_pass); sh_puts("/"); sh_dec((int32_t)g_total); sh_puts(" passed\n");
    return g_pass == g_total ? 0 : 1;
}
