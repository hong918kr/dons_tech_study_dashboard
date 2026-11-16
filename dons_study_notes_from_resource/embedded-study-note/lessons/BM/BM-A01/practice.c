/* BM-A01 추가 연습 — 호스트 C (Mac: cc, VM: run_qemu.sh practice)
 *   연습 1  cpacr_set_fp(): CP10/CP11 필드 인코딩 (다른 비트 보존, CP10 == CP11)
 *   연습 2  vtor_align_bytes() / vtor_ok() + "덧셈 vs OR 결합" 벡터 주소 모델 — 정렬 규칙의 이유
 *   연습 3  boot_order_ok(): Reset_Handler 단계 순서 검사기 (전체 순열 전수)
 *   연습 4  exc_frame_bytes(): 기본 / 확장(FP) 예외 프레임 크기와 정렬 패딩
 *   연습 5  clk_transition(): 빨라질 땐 LATENCY 먼저, 느려질 땐 SW 먼저 — 모든 주파수 쌍 전수
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-A01_pr && /tmp/BM-A01_pr
 */
#include <stdint.h>
#include <stdio.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------------- 연습 1: CPACR ---------------- */
enum { CP_DENY = 0, CP_PRIV = 1, CP_FULL = 3 };
static uint32_t cpacr_set_fp(uint32_t old, uint32_t mode) {    /* mode 2 는 예약 → 거부: 원래 값 */
    if (mode == 2u || mode > 3u) return old;
    return (old & ~(0xFu << 20)) | (mode << 20) | (mode << 22);
}
static void ex1(void) {
    CHECK(cpacr_set_fp(0u, CP_FULL) == 0x00F00000u);
    CHECK(cpacr_set_fp(0u, CP_PRIV) == 0x00500000u);
    CHECK(cpacr_set_fp(0x00F00000u, CP_DENY) == 0u);
    CHECK(cpacr_set_fp(0x0000000Fu, CP_FULL) == 0x00F0000Fu);       /* 다른 코프로세서 칸 보존 */
    CHECK(cpacr_set_fp(0x00500000u, 2u) == 0x00500000u);
    uint32_t x = 0x12345678u, bad = 0;                               /* xorshift32: 4000 개 임의 값 */
    for (int i = 0; i < 4000; ++i) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        for (uint32_t m = 0; m < 4u; ++m) {
            if (m == 2u) continue;
            uint32_t v = cpacr_set_fp(x, m);
            if ((v & ~(0xFu << 20)) != (x & ~(0xFu << 20)) || ((v >> 20) & 3u) != m || ((v >> 22) & 3u) != m) bad++;
        }
    }
    CHECK(bad == 0u);
    printf("ex1 CPACR: FULL=0x%08X PRIV=0x%08X, 12000 random RMW cases bad=%u\n",
           cpacr_set_fp(0u, CP_FULL), cpacr_set_fp(0u, CP_PRIV), bad);
}

/* ---------------- 연습 2: VTOR 정렬 ---------------- */
static uint32_t vtor_align_bytes(uint32_t n_irq) {   /* 2 의 거듭제곱 ≥ (16+n)·4, 최소 128 */
    uint32_t need = (16u + n_irq) * 4u, a = 128u;
    while (a < need) a <<= 1;
    return a;
}
static int vtor_ok(uint32_t addr, uint32_t n_irq) { return (addr & (vtor_align_bytes(n_irq) - 1u)) == 0u; }
static uint32_t vec_add(uint32_t vtor, uint32_t exc) { return vtor + exc * 4u; }               /* QEMU 방식 */
static uint32_t vec_or(uint32_t vtor, uint32_t exc) {        /* 결합(OR) 방식 모델: 덧셈기 없이 비트를 붙인다 */
    return vtor | (exc * 4u);
}
static void ex2(void) {
    CHECK(vtor_align_bytes(0u) == 128u && vtor_align_bytes(16u) == 128u && vtor_align_bytes(17u) == 256u);
    CHECK(vtor_align_bytes(32u) == 256u);       /* mps2-an386 (QEMU num-irq 32) */
    CHECK(vtor_align_bytes(82u) == 512u);       /* STM32F407: 82 IRQ (RM0090 — 확인 권장) */
    CHECK(vtor_align_bytes(240u) == 1024u);     /* Cortex-M4 최대 240 */
    CHECK(vtor_ok(0x20000600u, 32u) && !vtor_ok(0x20000680u, 32u) && vtor_ok(0x20000680u, 16u));
    /* 1 KB 창 안의 128 B 정렬 시작점 8 개 × 예외 0..47: 덧셈과 결합이 다른 칸 수 */
    uint32_t tot_mis = 0;
    printf("ex2 base   mismatching vectors (of 48) if HW concatenates instead of adds\n");
    for (uint32_t base = 0x20000400u; base < 0x20000800u; base += 128u) {
        uint32_t mis = 0;
        for (uint32_t e = 0; e < 48u; ++e) if (vec_add(base, e) != vec_or(base, e)) mis++;
        printf("    0x%08X %2u %s\n", base, mis, vtor_ok(base, 32u) ? "(aligned)" : "");
        if (vtor_ok(base, 32u)) CHECK(mis == 0u); else CHECK(mis > 0u);
        tot_mis += mis;
    }
    CHECK(tot_mis == 4u * 16u);   /* 정렬 안 된 4 개 시작점 × 예외 32..47 (오프셋 0x80..0xBC 가 bit7 과 겹침) */
    CHECK(vec_or(0x20000380u, 47u) == 0x200003BCu && (0x200003BCu - 0x20000380u) / 4u == 15u);   /* B5: 칸 15 */
    CHECK((0x20000340u & ~0x7Fu) == 0x20000300u);                  /* QEMU VTOR 쓰기 마스크 (B4 실측) */
}

/* ---------------- 연습 3: 부팅 순서 검사기 ---------------- */
enum { S_FPU, S_DATA, S_BSS, S_CTORS, S_VTOR, S_CLOCK, S_MAIN, S_N };
static const char *const k_sn[S_N] = { "FPU", "DATA", "BSS", "CTORS", "VTOR", "CLOCK", "MAIN" };
/* 규칙 (이 레슨의 펌웨어 기준): 생성자는 FP·.data·.bss 를 쓴다 / RAM 벡터 테이블은 .bss 안에 있다 /
 * 클럭 코드는 .data 의 SystemCoreClock 을 쓴다 (먼저 쓰면 .data 복사가 덮는다) / main 은 마지막. */
static const uint8_t k_rule[][2] = {
    { S_FPU, S_CTORS }, { S_DATA, S_CTORS }, { S_BSS, S_CTORS }, { S_BSS, S_VTOR }, { S_DATA, S_CLOCK },
    { S_FPU, S_MAIN }, { S_DATA, S_MAIN }, { S_BSS, S_MAIN }, { S_CTORS, S_MAIN }, { S_VTOR, S_MAIN }, { S_CLOCK, S_MAIN },
};
static int boot_order_ok(const uint8_t *ord, int n, int *bad_rule) {
    int pos[S_N];
    if (n != S_N) return 0;
    for (int i = 0; i < S_N; ++i) pos[i] = -1;
    for (int i = 0; i < n; ++i) { if (ord[i] >= S_N || pos[ord[i]] >= 0) return 0; pos[ord[i]] = i; }
    for (int r = 0; r < (int)(sizeof k_rule / sizeof k_rule[0]); ++r)
        if (pos[k_rule[r][0]] > pos[k_rule[r][1]]) { if (bad_rule) *bad_rule = r; return 0; }
    return 1;
}
static uint32_t g_perm_ok;
static void permute(uint8_t *a, int k) {
    if (k == S_N) { if (boot_order_ok(a, S_N, NULL)) g_perm_ok++; return; }
    for (int i = k; i < S_N; ++i) {
        uint8_t t = a[k]; a[k] = a[i]; a[i] = t;
        permute(a, k + 1);
        t = a[k]; a[k] = a[i]; a[i] = t;
    }
}
static void ex3(void) {
    const uint8_t mine[S_N]  = { S_FPU, S_DATA, S_BSS, S_CTORS, S_VTOR, S_CLOCK, S_MAIN };   /* 이 레슨 */
    const uint8_t late[S_N]  = { S_DATA, S_BSS, S_CTORS, S_FPU, S_VTOR, S_CLOCK, S_MAIN };   /* O1 */
    const uint8_t early[S_N] = { S_FPU, S_VTOR, S_DATA, S_BSS, S_CTORS, S_CLOCK, S_MAIN };   /* O2 */
    const uint8_t clk0[S_N]  = { S_CLOCK, S_FPU, S_DATA, S_BSS, S_CTORS, S_VTOR, S_MAIN };   /* SystemCoreClock 덮임 */
    int r = -1;
    CHECK(boot_order_ok(mine, S_N, NULL));
    CHECK(!boot_order_ok(late, S_N, &r) && k_rule[r][0] == S_FPU && k_rule[r][1] == S_CTORS);
    CHECK(!boot_order_ok(early, S_N, &r) && k_rule[r][0] == S_BSS && k_rule[r][1] == S_VTOR);
    CHECK(!boot_order_ok(clk0, S_N, &r) && k_rule[r][0] == S_DATA && k_rule[r][1] == S_CLOCK);
    CHECK(!boot_order_ok(mine, S_N - 1, NULL));
    const uint8_t dup[S_N] = { S_FPU, S_FPU, S_BSS, S_CTORS, S_VTOR, S_CLOCK, S_MAIN };
    CHECK(!boot_order_ok(dup, S_N, NULL));
    uint8_t a[S_N];
    for (int i = 0; i < S_N; ++i) a[i] = (uint8_t)i;
    g_perm_ok = 0;
    permute(a, 0);
    printf("ex3 boot orders: %u of 5040 permutations satisfy the %zu rules\n", g_perm_ok, sizeof k_rule / sizeof k_rule[0]);
    CHECK(g_perm_ok == 66u);
    (void)k_sn;
}

/* ---------------- 연습 4: 예외 프레임 ---------------- */
/* 기본 8 워드 (r0-r3, r12, lr, pc, xpsr) = 32 B; FPCA=1 → +S0-S15, FPSCR, 예약 1 = 26 워드 = 104 B.
 * STKALIGN=1 (M4 기본): 진입 시 SP 가 8 의 배수가 아니면 4 B 패딩 (xPSR bit9 에 기록). */
static uint32_t exc_frame_bytes(int fpca, uint32_t sp, uint32_t *new_sp) {
    uint32_t sz = fpca ? 104u : 32u, pad = (sp & 4u) ? 4u : 0u;
    *new_sp = sp - sz - pad;
    return sz + pad;
}
static void ex4(void) {
    uint32_t nsp;
    CHECK(exc_frame_bytes(0, 0x20010000u, &nsp) == 32u && nsp == 0x2000FFE0u);
    CHECK(exc_frame_bytes(1, 0x20010000u, &nsp) == 104u && nsp == 0x2000FF98u);
    CHECK(exc_frame_bytes(0, 0x2000FFFCu, &nsp) == 36u && nsp == 0x2000FFD8u && (nsp & 7u) == 0u);
    CHECK(exc_frame_bytes(1, 0x2000FFFCu, &nsp) == 108u && (nsp & 7u) == 0u);
    /* demo_probe P6b: FPCAR=0x2000ff88 = 프레임 시작 + 0x20 → 프레임 시작 0x2000ff68 */
    CHECK(0x2000FF88u - 0x20u == 0x2000FF68u);
    uint32_t bad = 0;
    for (uint32_t sp = 0x20000100u; sp < 0x20000200u; sp += 4u)
        for (int f = 0; f < 2; ++f) { exc_frame_bytes(f, sp, &nsp); if (nsp & 7u) bad++; }
    CHECK(bad == 0u);
    printf("ex4 frame: basic 32 B, extended 104 B (+4 pad when SP%%8==4), 128 SPs all 8-aligned after push\n");
}

/* ---------------- 연습 5: 클럭 전환 순서 ---------------- */
#define WS_STEP 30u      /* MHz per wait state (2.7–3.6 V, RM0090 — 확인 권장) */
static uint32_t ws_for(uint32_t mhz) { return mhz ? (mhz - 1u) / WS_STEP : 0u; }
typedef enum { LAT_FIRST, SW_FIRST } order_t;
static order_t clk_transition(uint32_t from_mhz, uint32_t to_mhz) { return to_mhz > from_mhz ? LAT_FIRST : SW_FIRST; }
/* 두 단계 (LATENCY 쓰기, SW 쓰기) 를 order 대로 할 때 중간 상태에서 HCLK ≤ (lat+1)·30 인지 */
static int transition_safe(uint32_t f0, uint32_t f1, order_t o) {
    uint32_t lat = ws_for(f0), hz = f0;
    for (int step = 0; step < 2; ++step) {
        int do_lat = (o == LAT_FIRST) ? step == 0 : step == 1;
        if (do_lat) lat = ws_for(f1); else hz = f1;
        if (hz > (lat + 1u) * WS_STEP) return 0;
    }
    return 1;
}
static void ex5(void) {
    static const uint32_t f[] = { 16, 24, 30, 31, 48, 60, 61, 84, 90, 100, 120, 144, 150, 168 };
    const int n = (int)(sizeof f / sizeof f[0]);
    uint32_t pairs = 0, ok = 0, wrong_unsafe = 0, ws_change = 0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            if (i == j) continue;
            pairs++;
            order_t o = clk_transition(f[i], f[j]);
            if (transition_safe(f[i], f[j], o)) ok++;
            if (ws_for(f[i]) != ws_for(f[j])) {
                ws_change++;
                if (!transition_safe(f[i], f[j], o == LAT_FIRST ? SW_FIRST : LAT_FIRST)) wrong_unsafe++;
            }
        }
    printf("ex5 transitions: %u pairs, rule-safe %u; WS changes in %u, reversed order unsafe in %u\n",
           pairs, ok, ws_change, wrong_unsafe);
    CHECK(ok == pairs);
    CHECK(wrong_unsafe == ws_change);         /* WS 가 바뀌는 전환은 순서를 뒤집으면 반드시 위반 */
    CHECK(ws_for(168) == 5u && ws_for(30) == 0u && ws_for(31) == 1u && ws_for(16) == 0u);
    CHECK(clk_transition(16, 168) == LAT_FIRST && clk_transition(168, 16) == SW_FIRST);
}

int main(void) {
    ex1(); ex2(); ex3(); ex4(); ex5();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
