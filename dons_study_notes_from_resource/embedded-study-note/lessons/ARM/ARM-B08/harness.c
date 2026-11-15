/* ARM-B08 스택 프레임과 재귀: FP/LR 저장 — 테스트 하네스 (starter.S / solution.S 공용)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 harness.c starter.S(또는 solution.S) shared_regcheck.S -o t && ./t
 * 반환값만 보지 않는다: base case 에서 부르는 C 훅이 FP 체인을 걸어 깊이 · 저장된 LR · 프레임 간격을 검사한다.
 */
#define _DARWIN_C_SOURCE 1              /* dladdr (심볼 이름 찾기) */
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- asm (starter.S / solution.S) ---- */
uint64_t fact_asm(uint64_t n);                                            /* Part A */
uint64_t fib_asm(uint64_t n);                                             /* Part A */
int64_t  local_array_sum(int32_t seed, void (*cb)(int32_t *a, size_t n)); /* Part A/C */
int      fp_backtrace(uintptr_t *out, int max);                           /* Part C */
void fact_asm_end(void);                 /* 끝 표시 라벨: 주소만 쓴다 (호출 금지) */
void fib_asm_end(void);

/* ---- shared_regcheck.S ---- */
/* fn = 함수 주소 (uintptr_t 로 넘긴다: 다른 시그니처의 함수를 asm 이 x0, x1 로 부르므로 C 는 그 호출을 안 한다) */
uint64_t call_preserve_check(uintptr_t fn, uint64_t a, uint64_t b, uint64_t *ret);

/* ---- 훅: asm 의 base case 가 bl _base_hook 으로 부른다 ---- */
#define MAXW 20100
static uintptr_t g_lo, g_hi;           /* [g_lo, g_hi) = 검사 대상 asm 함수의 코드 범위 */
static uintptr_t g_lr[MAXW], g_fp[MAXW], g_saved19[MAXW];
static int g_calls, g_depth, g_walk_n, g_inrange;
static int g_bt_on, g_bt_n;
static uintptr_t g_bt[256];

void base_hook(void);
__attribute__((noinline)) void base_hook(void) {
    g_calls++;
    if (g_bt_on) g_bt_n = fp_backtrace(g_bt, 256);
    /* 1차: 체인 끝(fp == 0)까지 걸으며 "앞에서부터 연속으로 범위 안" 인 LR 개수(d)를 센다 */
    const uintptr_t *fp = (const uintptr_t *)__builtin_frame_address(0);  /* 훅 자신의 프레임 레코드 */
    int d = 0, n = 0, in = 0, prefix = 1;
    while (fp && n < MAXW) {
        uintptr_t lr = fp[1];                          /* 이 프레임 주인의 반환 주소 */
        int inr = (lr >= g_lo && lr < g_hi);
        in += inr;
        if (prefix && inr) d++; else prefix = 0;
        n++;
        const uintptr_t *up = (const uintptr_t *)fp[0];
        if ((uintptr_t)up <= (uintptr_t)fp || ((uintptr_t)up & 15u)) break;  /* 위로, 16 정렬 */
        fp = up;
    }
    if (d < g_depth) return;                           /* 가장 깊었던 체인만 기록 */
    /* 2차: 기록. g_lr[k] = k 번째 LR, g_fp[k] = 그 LR 로 돌아갈 "호출자" 프레임 레코드 */
    g_depth = d; g_walk_n = n; g_inrange = in;
    fp = (const uintptr_t *)__builtin_frame_address(0);
    for (int k = 0; k < n; k++) {
        g_lr[k] = fp[1];
        fp = (const uintptr_t *)fp[0];
        g_fp[k] = (uintptr_t)fp;
        g_saved19[k] = (k < d) ? fp[2] : 0;            /* asm 프레임만: [fp+16] = 저장된 x19 */
    }
}

static void hook_reset(uintptr_t lo, uintptr_t hi) {
    g_lo = lo; g_hi = hi;
    g_calls = g_depth = g_walk_n = g_inrange = 0;
}

/* ---- 콜백 (local_array_sum 이 부른다) ---- */
static int32_t *g_cb_ptr; static size_t g_cb_n; static uintptr_t g_cb_caller_fp;
static int32_t g_cb_seen[10];
static void cb_record(int32_t *a, size_t n) {
    g_cb_ptr = a; g_cb_n = n;
    for (size_t i = 0; i < n && i < 10; i++) g_cb_seen[i] = a[i];
    g_cb_caller_fp = ((const uintptr_t *)__builtin_frame_address(0))[0];   /* = local_array_sum 의 x29 */
}
static void cb_patch(int32_t *a, size_t n) { if (n > 3) a[3] += 1000; }

/* ---- C 기준 모델 (부호 없는 연산만 → UB 없음) ---- */
static uint64_t fact_ref(uint64_t n) { uint64_t r = 1; for (uint64_t i = 2; i <= n; i++) r *= i; return r; }
static uint64_t fib_ref(uint64_t n) { uint64_t a = 0, b = 1; while (n--) { uint64_t t = a + b; a = b; b = t; } return a; }

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static int depth_ok(uint64_t n) { return g_depth == (n ? (int)n : 1) && g_inrange == g_depth; }

static void print_bt(void) {           /* 크래시 로그처럼: #k 심볼+오프셋 */
    for (int k = 0; k < g_bt_n && k < 7; k++) {
        Dl_info di;
        if (dladdr((const void *)g_bt[k], &di) && di.dli_sname)
            printf("  #%d %s+0x%lx\n", k, di.dli_sname, (unsigned long)(g_bt[k] - (uintptr_t)di.dli_saddr));
        else
            printf("  #%d ?\n", k);
    }
    printf("  ... (%d frames)\n", g_bt_n);
}

int main(void) {
    /* ===== Part A: 반환값 ===== */
    CHECK(fact_asm(0) == 1u);
    CHECK(fact_asm(5) == 120u);
    CHECK(fact_asm(20) == 2432902008176640000u);
    int ok = 1;
    for (uint64_t n = 0; n <= 25; n++) ok &= (fact_asm(n) == fact_ref(n));   /* 21! 부터 mod 2^64 */
    CHECK(ok);
    CHECK(fib_asm(0) == 0u && fib_asm(1) == 1u);
    CHECK(fib_asm(10) == 55u);
    ok = 1;
    for (uint64_t n = 0; n <= 25; n++) ok &= (fib_asm(n) == fib_ref(n));
    CHECK(ok);
    CHECK(local_array_sum(0, NULL) == 45);
    CHECK(local_array_sum(-5, NULL) == -5);

    /* ===== Part B: 프레임 검증 (FP 체인) ===== */
    static const uint64_t ns[] = {0, 1, 2, 5, 20};
    ok = 1;
    for (size_t i = 0; i < sizeof ns / sizeof ns[0]; i++) {
        hook_reset((uintptr_t)fact_asm, (uintptr_t)fact_asm_end);
        uint64_t r = fact_asm(ns[i]);
        ok &= (r == fact_ref(ns[i]) && g_calls == 1 && depth_ok(ns[i]));
    }
    CHECK(ok);                                        /* 깊이 == max(n,1), 범위 안 LR 이 정확히 그만큼 */
    hook_reset((uintptr_t)fact_asm, (uintptr_t)fact_asm_end);
    (void)fact_asm(5);
    ok = (g_depth == 5);
    for (int k = 2; k < g_depth; k++) ok &= (g_lr[k] == g_lr[1]);
    CHECK(ok && g_lr[0] != g_lr[1]);                  /* 재귀 복귀 주소는 하나, base 복귀 주소는 따로 */
    ok = (g_depth == 5);
    for (int k = 0; k < g_depth; k++) ok &= ((g_fp[k] & 15u) == 0);
    for (int k = 0; k + 1 < g_depth; k++) ok &= (g_fp[k + 1] - g_fp[k] == 32u);
    CHECK(ok);                                        /* 프레임 레코드 16 정렬, 간격 32 B */
    ok = (g_depth == 5);
    for (int k = 0; k + 1 < g_depth; k++) ok &= (g_saved19[k] == (uintptr_t)k + 2);
    CHECK(ok);                                        /* [fp+16] = "호출자" 의 x19 (= 호출자의 n) */

    hook_reset((uintptr_t)fact_asm, (uintptr_t)fact_asm_end);
    CHECK(fact_asm(10000) == fact_ref(10000) && depth_ok(10000));   /* 깊은 재귀: 320 KB 스택 */

    static const uint64_t fs[] = {0, 1, 2, 10, 20};
    ok = 1;
    for (size_t i = 0; i < sizeof fs / sizeof fs[0]; i++) {
        hook_reset((uintptr_t)fib_asm, (uintptr_t)fib_asm_end);
        uint64_t r = fib_asm(fs[i]);
        ok &= (r == fib_ref(fs[i]) && (uint64_t)g_calls == fib_ref(fs[i] + 1) && depth_ok(fs[i]));
    }
    CHECK(ok);                                        /* 훅 호출 수 == fib(n+1), 최대 깊이 == max(n,1) */

    uint64_t r = 0;
    CHECK(call_preserve_check((uintptr_t)fact_asm, 20, 0, &r) == 0 && r == 2432902008176640000u);
    r = 0;
    CHECK(call_preserve_check((uintptr_t)fib_asm, 15, 0, &r) == 0 && r == 610u);
    r = 0;
    CHECK(call_preserve_check((uintptr_t)local_array_sum, 100, (uint64_t)(uintptr_t)cb_patch, &r) == 0
          && r == 2045u);                             /* x19..x28 · sp 보존 */

    CHECK(local_array_sum(INT32_MIN, NULL) == -21474836435LL);   /* 합은 64비트로 (w 로 합하면 틀림) */
    CHECK(local_array_sum(INT32_MAX, NULL) == -17179869149LL);   /* a[i] 는 32비트 wrap, 손으로 쓴 기대값 */

    /* ===== Part C: 지역 배열 + 콜백, 크래시 로거용 백트레이스 ===== */
    g_cb_ptr = NULL; g_cb_n = 0;
    CHECK(local_array_sum(7, cb_record) == 115 && g_cb_n == 10);
    CHECK(g_cb_ptr != NULL && ((uintptr_t)g_cb_ptr & 15u) == 0);
    CHECK(g_cb_ptr != NULL && g_cb_caller_fp - (uintptr_t)g_cb_ptr == 48u);   /* a == x29 - 48 */
    ok = (g_cb_n == 10);
    for (int i = 0; i < 10; i++) ok &= (g_cb_seen[i] == 7 + i);
    CHECK(ok);
    CHECK(local_array_sum(100, cb_patch) == 2045);    /* 콜백이 바꾼 값을 다시 읽는다 */

    hook_reset((uintptr_t)fact_asm, (uintptr_t)fact_asm_end);
    g_bt_on = 1; g_bt_n = 0;
    (void)fact_asm(4);
    g_bt_on = 0;
    CHECK(g_bt_n == g_walk_n + 1 && g_bt_n < 256);   /* asm 백트레이스 길이 == C 로 걸은 길이 + 1 */
    CHECK(g_bt_n > 1 && memcmp(&g_bt[1], g_lr, (size_t)(g_bt_n - 1) * sizeof g_bt[0]) == 0);
    CHECK(g_bt_n > 0 && g_bt[0] > (uintptr_t)base_hook && g_bt[0] - (uintptr_t)base_hook < 4096u);
    if (g_bt_n > 0) print_bt();

    uintptr_t buf[5] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    CHECK(fp_backtrace(buf, 0) == 0 && buf[0] == 0xAA);
    CHECK(fp_backtrace(buf, 3) == 3 && buf[3] == 0xAA && buf[0] != 0xAA);   /* max 를 넘겨 쓰지 않는다 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
