/* ARM-M01 AAPCS64 심화: 많은 인자·구조체 반환·callee-saved — 테스트 하네스 (starter.S / solution.S 공용)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S shared_violators.S -o t && ./t
 * Apple arm64 (Mach-O) 와 Linux aarch64 (ELF) 둘 다에서 돈다. 스택 인자 배치가 다른 곳만 #ifdef __APPLE__.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- 레슨의 타입들 ---- */
typedef struct { int64_t q, r; } qr16;                     /* 16 B → x0, x1 로 반환 */
typedef struct {                                            /* 32 B → x8 간접 결과 */
    uint64_t ts;                                            /* +0  타임스탬프 */
    int32_t  ax, ay, az;                                    /* +8 +12 +16 가속도 raw */
    uint32_t flags;                                         /* +20 bit k = 축 k 가 음수 */
    uint64_t sum;                                           /* +24 체크섬 = ts + ax + ay + az */
} imu_sample;
typedef struct { float w, x, y, z; } quat;                  /* HFA (float 4개) → s0–s3 */
typedef struct { float v[5]; } f5;                          /* float 5개: HFA 아님(>4) + 20 B > 16 → 메모리 */
_Static_assert(sizeof(imu_sample) == 32 && offsetof(imu_sample, flags) == 20, "imu layout");
_Static_assert(sizeof(quat) == 16 && sizeof(f5) == 20, "hfa sizes");

/* ---- 검사기 인터페이스 ---- */
typedef struct {
    uint64_t x[8];      /* +0   → x0..x7 */
    uint64_t x8;        /* +64  → x8 (간접 결과 주소) */
    uint64_t stk[2];    /* +72  → 호출 순간 [sp], [sp+8] (스택 인자 16 B) */
    uint64_t d[8];      /* +88  → d0..d7 (비트 그대로; float 는 하위 32비트 = s 뷰) */
} abi_args;
typedef struct { uint64_t x0, x1; uint64_t d[4]; } abi_ret; /* 호출 뒤 x0, x1, d0..d3 */
_Static_assert(offsetof(abi_args, x8) == 64 && offsetof(abi_args, stk) == 72 && offsetof(abi_args, d) == 88, "args");
_Static_assert(offsetof(abi_ret, d) == 16 && sizeof(abi_ret) == 48, "ret");

#define ABI_X(n) (1u << ((n) - 19))        /* bit 0..9   = x19..x28 바뀜 */
#define ABI_D(n) (1u << ((n) - 8 + 10))    /* bit 10..17 = d8..d15 (하위 64비트) 바뀜 */
#define ABI_SP   (1u << 18)                /* 돌아왔을 때 sp 가 다름 */
#define ABI_FP   (1u << 19)                /* 돌아왔을 때 x29 가 다름 */

/* ---- starter.S / solution.S (TODO) ---- */
long  sum10(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7, long a8, long a9);
long  mixed_stack(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7,
                  signed char c, short s, int i, long l);
qr16  divmod_pair(int64_t a, int64_t b);
imu_sample make_imu(uint64_t ts, int32_t ax, int32_t ay, int32_t az);
quat  quat_mul(quat a, quat b);
float f5_sum(f5 v);
uint32_t abi_call_check(const void *fn, const abi_args *a, abi_ret *r);

/* ---- shared_violators.S (주어진 시험 대상, TODO 아님) ---- */
void vio_good_leaf(void);   void vio_v8_upper(void);  void vio_good_saver(void);
void vio_x19(void);         void vio_d9(void);        void vio_s12(void);
void vio_x28_d15(void);     void vio_swap_restore(void);
void vio_sp(void);          void vio_fp(void);
long vio_mixed_aapcs(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7,
                     signed char c, short s, int i, long l);

/* ---- clang 이 만든 함수도 시험 대상: 값이 많이 살아 있는 채로 불투명 호출 → x19+, d8+ 사용 ---- */
static double (*volatile g_opaque)(double) = 0;
static double twice(double v) { return v * 2.0; }
__attribute__((noinline)) static long c_pressure(long a, long b, long c, long d) {
    double f = (double)a, g = (double)b;
    long t0 = a * 3, t1 = b * 5, t2 = c * 7, t3 = d * 11, t4 = a ^ d, t5 = b + c;
    double h = g_opaque(f) + g_opaque(g);           /* 호출 너머로 t0..t5, f, g 가 살아야 한다 */
    return t0 + t1 + t2 + t3 + t4 + t5 + (long)(h + f * g);
}

/* ---- C 기준 모델 ---- */
static uint64_t xs = 0x9E3779B97F4A7C15u;
static uint64_t rnd(void) { xs ^= xs << 13; xs ^= xs >> 7; xs ^= xs << 17; return xs; }
static quat qmul_ref(quat a, quat b) {
    quat r = { a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
               a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
               a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
               a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w };
    return r;
}
static int qeq(quat a, quat b) { return a.w == b.w && a.x == b.x && a.y == b.y && a.z == b.z; }
static float smallf(void) { return (float)((int)(rnd() % 17) - 8); }     /* -8..8: 곱·합이 정확 */
static uint64_t fbits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float bitsf(uint64_t b) { uint32_t u = (uint32_t)b; float f; memcpy(&f, &u, 4); return f; }
#ifdef __APPLE__
/* Apple: 스택 인자를 자연 크기·정렬로 채운다 → char@sp+0, short@sp+2, int@sp+4, long@sp+8 */
static void pack_mixed(abi_args *a, signed char c, short s, int i, long l) {
    a->stk[0] = (uint64_t)(uint8_t)c | (uint64_t)(uint16_t)s << 16 | (uint64_t)(uint32_t)i << 32;
    a->stk[1] = (uint64_t)l;
}
#endif

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

/* 감사(audit) 표 한 줄: 이름, 기대 마스크, 실제 마스크 */
static int g_audit_bad;
static void audit(const char *name, uint32_t expect, uint32_t got) {
    printf("  %-18s mask=0x%05x expect=0x%05x %s\n", name, got, expect, got == expect ? "ok" : "MISMATCH");
    g_audit_bad += (got != expect);
}

int main(void) {
    g_opaque = twice;
    int ok;
    /* ===== Part A: 스택 인자 · 작은 구조체 반환 ===== */
    CHECK(sum10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) == 55);
    CHECK(sum10(0, 0, 0, 0, 0, 0, 0, 0, 1L << 40, 0) == 1L << 40);              /* a8 = [sp] */
    CHECK(sum10(0, 0, 0, 0, 0, 0, 0, 0, 0, -7) == -7);                         /* a9 = [sp+8] */
    ok = 1;
    for (int k = 0; k < 2000; k++) {
        uint64_t v[10], ref = 0;
        for (int j = 0; j < 10; j++) { v[j] = rnd() >> 4; ref += v[j]; }      /* 합이 int64 안 */
        ok &= ((uint64_t)sum10((long)v[0], (long)v[1], (long)v[2], (long)v[3], (long)v[4], (long)v[5],
                               (long)v[6], (long)v[7], (long)v[8], (long)v[9]) == ref);
    }
    CHECK(ok);
    CHECK(mixed_stack(1, 2, 3, 4, 5, 6, 7, 8, -3, -1000, -100000, 0x123456789L) == 8 - 3 - 1000 - 100000 + 0x123456789L);
    CHECK(mixed_stack(0, 0, 0, 0, 0, 0, 0, 7, -128, -32768, INT32_MIN, 1L << 40)
          == 7 - 128 - 32768 + (long)INT32_MIN + (1L << 40));                   /* 부호 확장 필수 */
    CHECK(mixed_stack(0, 0, 0, 0, 0, 0, 0, 0, 127, 32767, INT32_MAX, 0) == 127 + 32767 + (long)INT32_MAX);
    ok = 1;
    for (int k = 0; k < 2000; k++) {
        signed char c = (signed char)(rnd() & 0xff); short s = (short)(rnd() & 0xffff);
        int i = (int)(int32_t)(uint32_t)rnd(); long l = (long)(rnd() >> 2), a7 = (long)(rnd() >> 2);
        ok &= (mixed_stack(1, 2, 3, 4, 5, 6, 7, a7, c, s, i, l) == a7 + c + s + (long)i + l);
    }
    CHECK(ok);
    qr16 p = divmod_pair(17, 5);
    CHECK(p.q == 3 && p.r == 2);
    p = divmod_pair(-17, 5);
    CHECK(p.q == -3 && p.r == -2);                                              /* C 처럼 0 쪽으로 자름 */
    ok = 1;
    for (int k = 0; k < 2000; k++) {
        int64_t a = (int64_t)(rnd() >> 1);
        a -= (int64_t)(rnd() >> 1);
        unsigned sh = 1u + (unsigned)(rnd() % 60);
        int64_t b = (int64_t)(rnd() >> sh);                                    /* 크기가 다양한 제수 */
        if (b == 0) b = 1;
        if (rnd() & 1) b = -b;
        qr16 t = divmod_pair(a, b);
        ok &= (t.q == a / b && t.r == a % b);
    }
    CHECK(ok);
    p = divmod_pair(INT64_MIN, -1);                                             /* C 에선 UB, ISA 는 정의됨 */
    CHECK(p.q == INT64_MIN && p.r == 0);
    p = divmod_pair(5, 0);                                                      /* SDIV x/0 = 0 → r = a */
    CHECK(p.q == 0 && p.r == 5);

    /* ===== Part A: x8 간접 결과 · HFA · 큰 구조체 인자 ===== */
    imu_sample m = make_imu(1000000007u, -5, 300, -16384);
    CHECK(m.ts == 1000000007u && m.ax == -5 && m.ay == 300 && m.az == -16384);
    CHECK(m.flags == 0x5u && m.sum == 1000000007u - 5 + 300 - 16384);
    m = make_imu(UINT64_MAX, INT32_MIN, INT32_MAX, 0);                          /* 64비트 wrap 체크섬 */
    CHECK(m.flags == 0x1u && m.sum == UINT64_MAX + (uint64_t)(int64_t)INT32_MIN + (uint64_t)INT32_MAX);
    quat qi = {0, 1, 0, 0}, qj = {0, 0, 1, 0}, qk = {0, 0, 0, 1}, one = {1, 0, 0, 0};
    CHECK(qeq(quat_mul(qi, qj), qk) && qeq(quat_mul(qj, qi), (quat){0, 0, 0, -1}));   /* ij = k, ji = -k */
    CHECK(qeq(quat_mul(qk, qk), (quat){-1, 0, 0, 0}) && qeq(quat_mul(one, qj), qj));
    ok = 1;
    for (int k = 0; k < 2000; k++) {
        quat a = {smallf(), smallf(), smallf(), smallf()}, b = {smallf(), smallf(), smallf(), smallf()};
        ok &= qeq(quat_mul(a, b), qmul_ref(a, b));
    }
    CHECK(ok);
    f5 v5 = {{1, 2, 3, 4, 5}};
    CHECK(f5_sum(v5) == 15.0f);
    v5 = (f5){{0.5f, -0.25f, 1e6f, -1e6f, 0.125f}};
    CHECK(f5_sum(v5) == (((0.5f + -0.25f) + 1e6f) + -1e6f) + 0.125f);           /* 왼쪽부터 순서대로 */

    /* ===== Part B: ABI 적합성 검사기 ===== */
    abi_args A; abi_ret R;
    memset(&A, 0, sizeof A); memset(&R, 0, sizeof R);
    A.x[0] = 40; A.x[1] = 2;
    CHECK(abi_call_check((const void *)vio_good_leaf, &A, &R) == 0 && R.x0 == 42);   /* caller-saved 는 다 망가뜨려도 OK */
    CHECK(abi_call_check((const void *)vio_v8_upper, &A, &R) == 0);              /* v8 상위 64비트만 → 위반 아님 */
    memset(&R, 0, sizeof R);
    CHECK(abi_call_check((const void *)vio_good_saver, &A, &R) == 0 && R.x0 == 40 * 19 + 2);
    CHECK(abi_call_check((const void *)vio_x19, &A, &R) == ABI_X(19));
    CHECK(abi_call_check((const void *)vio_d9, &A, &R) == ABI_D(9));
    CHECK(abi_call_check((const void *)vio_s12, &A, &R) == ABI_D(12));        /* s12 쓰기 = d12 상위 32비트도 0 */
    CHECK(abi_call_check((const void *)vio_x28_d15, &A, &R) == (ABI_X(28) | ABI_D(15)));
    CHECK(abi_call_check((const void *)vio_swap_restore, &A, &R) == (ABI_X(19) | ABI_X(20)));
    CHECK(abi_call_check((const void *)vio_sp, &A, &R) == ABI_SP);
    CHECK(abi_call_check((const void *)vio_fp, &A, &R) == ABI_FP);
    memset(&A, 0, sizeof A); memset(&R, 0, sizeof R);
    A.x[0] = 3; A.x[1] = 4; A.x[2] = 5; A.x[3] = 6;
    long cp = c_pressure(3, 4, 5, 6);
    CHECK(abi_call_check((const void *)c_pressure, &A, &R) == 0 && (long)R.x0 == cp);   /* 컴파일러는 규약을 지킨다 */
    abi_args inner; abi_ret iret;                                               /* 검사기로 검사기를 검사 */
    memset(&inner, 0, sizeof inner); memset(&iret, 0, sizeof iret);
    memset(&A, 0, sizeof A); memset(&R, 0, sizeof R);
    A.x[0] = (uint64_t)(uintptr_t)vio_x19; A.x[1] = (uint64_t)(uintptr_t)&inner; A.x[2] = (uint64_t)(uintptr_t)&iret;
    CHECK(abi_call_check((const void *)abi_call_check, &A, &R) == 0 && R.x0 == ABI_X(19));

    /* ===== Part C: 모든 시그니처를 검사기로 — 브링업 감사 표 ===== */
    printf("ABI audit (bit0-9 x19-x28, bit10-17 d8-d15, bit18 sp, bit19 fp):\n");
    g_audit_bad = 0;
    memset(&A, 0, sizeof A); memset(&R, 0, sizeof R);                         /* sum10: 9·10번째 인자는 스택 */
    for (int j = 0; j < 8; j++) A.x[j] = (uint64_t)j + 1;
    A.stk[0] = 9; A.stk[1] = 10;
    uint32_t mk = abi_call_check((const void *)sum10, &A, &R);
    audit("sum10", 0, mk);
    CHECK(mk == 0 && R.x0 == 55);
#ifdef __APPLE__
    memset(&R, 0, sizeof R);                                                    /* mixed_stack: Apple 압축 배치 */
    for (int j = 0; j < 8; j++) A.x[j] = (uint64_t)j + 1;
    pack_mixed(&A, -3, -1000, -100000, 0x123456789L);
    mk = abi_call_check((const void *)mixed_stack, &A, &R);
    audit("mixed_stack", 0, mk);
    CHECK(mk == 0 && (long)R.x0 == mixed_stack(1, 2, 3, 4, 5, 6, 7, 8, -3, -1000, -100000, 0x123456789L));
    long good = mixed_stack(1, 2, 3, 4, 5, 6, 7, 8, -3, -1000, -100000, 0x123456789L);
    long bad = vio_mixed_aapcs(1, 2, 3, 4, 5, 6, 7, 8, -3, -1000, -100000, 0x123456789L);
    printf("  info: mixed_stack=%ld  vio_mixed_aapcs (8 B 슬롯 가정)=%ld\n", good, bad);
    CHECK(good != bad);                                                          /* 표준 배치로 짠 asm 은 Apple 에서 틀린다 */
#endif
    memset(&A, 0, sizeof A); memset(&R, 0, sizeof R);
    A.x[0] = (uint64_t)-17; A.x[1] = 5;
    mk = abi_call_check((const void *)divmod_pair, &A, &R);
    audit("divmod_pair", 0, mk);
    CHECK(mk == 0 && (int64_t)R.x0 == -3 && (int64_t)R.x1 == -2);
    imu_sample buf; memset(&buf, 0xEE, sizeof buf);
    memset(&A, 0, sizeof A); memset(&R, 0, sizeof R);
    A.x[0] = 77; A.x[1] = (uint64_t)(uint32_t)-1; A.x[2] = 2; A.x[3] = (uint64_t)(uint32_t)-3;
    A.x8 = (uint64_t)(uintptr_t)&buf;                                          /* 호출자가 결과 버퍼 주소를 준다 */
    mk = abi_call_check((const void *)make_imu, &A, &R);
    audit("make_imu (x8)", 0, mk);
    CHECK(mk == 0 && buf.ts == 77 && buf.ax == -1 && buf.ay == 2 && buf.az == -3 && buf.flags == 0x5u && buf.sum == 75);
    memset(&A, 0, sizeof A); memset(&R, 0, sizeof R);
    quat qa = {1, 2, 3, 4}, qb = {5, 6, 7, 8};
    A.d[0] = fbits(qa.w); A.d[1] = fbits(qa.x); A.d[2] = fbits(qa.y); A.d[3] = fbits(qa.z);
    A.d[4] = fbits(qb.w); A.d[5] = fbits(qb.x); A.d[6] = fbits(qb.y); A.d[7] = fbits(qb.z);
    mk = abi_call_check((const void *)quat_mul, &A, &R);
    audit("quat_mul (HFA)", 0, mk);
    quat qr = {bitsf(R.d[0]), bitsf(R.d[1]), bitsf(R.d[2]), bitsf(R.d[3])};
    CHECK(mk == 0 && qeq(qr, qmul_ref(qa, qb)) && qr.w == -60.0f);
    f5 copy = {{1, 2, 3, 4, 5.5f}};
    memset(&A, 0, sizeof A); memset(&R, 0, sizeof R);
    A.x[0] = (uint64_t)(uintptr_t)&copy;                                       /* 20 B 비-HFA → 포인터 */
    mk = abi_call_check((const void *)f5_sum, &A, &R);
    audit("f5_sum (ptr)", 0, mk);
    CHECK(mk == 0 && bitsf(R.d[0]) == 15.5f);
    static const struct { const char *name; void (*fn)(void); uint32_t expect; } vt[] = {
        {"vio_good_leaf", vio_good_leaf, 0}, {"vio_v8_upper", vio_v8_upper, 0},
        {"vio_good_saver", vio_good_saver, 0}, {"vio_x19", vio_x19, ABI_X(19)},
        {"vio_d9", vio_d9, ABI_D(9)}, {"vio_s12", vio_s12, ABI_D(12)},
        {"vio_x28_d15", vio_x28_d15, ABI_X(28) | ABI_D(15)},
        {"vio_swap_restore", vio_swap_restore, ABI_X(19) | ABI_X(20)},
        {"vio_sp", vio_sp, ABI_SP}, {"vio_fp", vio_fp, ABI_FP},
    };
    memset(&A, 0, sizeof A);
    for (size_t j = 0; j < sizeof vt / sizeof vt[0]; j++)
        audit(vt[j].name, vt[j].expect, abi_call_check((const void *)vt[j].fn, &A, &R));
    CHECK(g_audit_bad == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
