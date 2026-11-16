/* BM-A03 추가 연습 — 호스트 C (Mac: cc, VM: run_qemu.sh practice). 펌웨어가 아니라 "규칙" 을 검사한다.
 *   연습 1  VTOR 정렬 규칙: 벡터 수 → 필요한 정렬, 앱 베이스 후보 판정 (전수)
 *   연습 2  합친 이미지 만들기: 0xFF 패딩, 부트로더 크기 초과·용량 부족 검출 (작은 크기 전수)
 *   연습 3  정리 순서 모델: 5 개 정리 동작의 120 가지 순서 중 안전한 것 세기 (QEMU probe 실측 규칙)
 *   연습 4  앱 쪽 계약 검사: QEMU 실행(qemu_solution.txt)에서 앱이 찍은 스냅샷으로 위반 집합 재현
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-A03_pr && /tmp/BM-A03_pr
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ================= 연습 1: VTOR 정렬 =================
 * ARMv7-M 규칙 (ARM ARM B3.2.5 — 확인 권장): 벡터 테이블은 "4·벡터수 이상인 2 의 거듭제곱" 에 맞춰
 * 정렬되어야 하고 최소 128 B. (QEMU 는 VTOR 하위 7 비트만 지운다 — demo_probe P4: 0x4080 도 받아들임) */
static uint32_t vtor_align(uint32_t nvec) {
    uint32_t a = 128u;
    while (a < 4u * nvec) a <<= 1;
    return a;
}
static int vtor_base_ok(uint32_t base, uint32_t nvec) { return (base & (vtor_align(nvec) - 1u)) == 0u; }

static void practice1(void) {
    for (uint32_t n = 1; n <= 512u; ++n) {
        uint32_t a = vtor_align(n);
        CHECK((a & (a - 1u)) == 0u);                 /* 2 의 거듭제곱 */
        CHECK(a >= 128u && a >= 4u * n);             /* 충분히 크다 */
        CHECK(a == 128u || a / 2u < 4u * n);         /* 가장 작은 것 */
        for (uint32_t base = 0; base < 0x10000u; base += 0x40u)
            if (vtor_base_ok(base, n) != ((base % a) == 0u)) { CHECK(0); return; }
    }
    static const struct { const char *chip; uint32_t nvec; } t[] = {
        { "mps2-an385 (this lesson)", 16u + 32u },  { "nRF52840 (48 IRQ, hedge)", 16u + 48u },
        { "STM32F407 (82 IRQ, hedge)", 16u + 82u }, { "STM32H743 (150 IRQ, hedge)", 16u + 150u },
    };
    printf("연습 1  VTOR alignment\n  %-30s %5s %6s  0x4000 0x4080 0x4100\n", "chip", "nvec", "align");
    for (size_t i = 0; i < sizeof t / sizeof t[0]; ++i)
        printf("  %-30s %5u %6u  %-6s %-6s %-6s\n", t[i].chip, t[i].nvec, vtor_align(t[i].nvec),
               vtor_base_ok(0x4000u, t[i].nvec) ? "ok" : "BAD", vtor_base_ok(0x4080u, t[i].nvec) ? "ok" : "BAD",
               vtor_base_ok(0x4100u, t[i].nvec) ? "ok" : "BAD");
    CHECK(vtor_align(48u) == 256u && vtor_base_ok(0x4000u, 48u) && !vtor_base_ok(0x4080u, 48u));
    CHECK(vtor_align(98u) == 512u && vtor_align(166u) == 1024u);
}

/* ================= 연습 2: 합친 이미지 =================
 * out[0 .. slot) = boot + 0xFF 패딩, out[slot .. slot+alen) = app. 반환: 전체 길이, 또는 음수 오류. */
enum { MERGE_ERR_BOOT_TOO_BIG = -1, MERGE_ERR_CAP = -2 };
static long merge_image(const uint8_t *boot, uint32_t blen, const uint8_t *app, uint32_t alen, uint32_t slot,
                        uint8_t *out, uint32_t cap) {
    if (blen > slot) return MERGE_ERR_BOOT_TOO_BIG;           /* 부트로더가 앱 슬롯을 침범 */
    if ((uint64_t)slot + alen > cap) return MERGE_ERR_CAP;    /* 64 비트로 더해 넘침 방지 */
    memcpy(out, boot, blen);
    memset(out + blen, 0xFF, slot - blen);                    /* 지워진 플래시 = 0xFF (0 이 아니다) */
    memcpy(out + slot, app, alen);
    return (long)(slot + alen);
}
static void practice2(void) {
    uint8_t boot[80], app[80], out[160];
    for (uint32_t i = 0; i < 80u; ++i) { boot[i] = (uint8_t)(0x10u + i); app[i] = (uint8_t)(0x80u ^ i); }
    uint32_t cases = 0, errs = 0;
    for (uint32_t slot = 0; slot <= 64u; slot += 16u)
        for (uint32_t blen = 0; blen <= 80u; ++blen)
            for (uint32_t alen = 0; alen <= 80u; ++alen) {
                memset(out, 0xAA, sizeof out);
                long n = merge_image(boot, blen, app, alen, slot, out, 128u);
                cases++;
                if (blen > slot) { CHECK(n == MERGE_ERR_BOOT_TOO_BIG); errs++; continue; }
                if (slot + alen > 128u) { CHECK(n == MERGE_ERR_CAP); errs++; continue; }
                int ok = n == (long)(slot + alen) && memcmp(out, boot, blen) == 0 && memcmp(out + slot, app, alen) == 0;
                for (uint32_t j = blen; j < slot; ++j) ok &= out[j] == 0xFFu;
                for (uint32_t j = slot + alen; j < sizeof out; ++j) ok &= out[j] == 0xAAu;   /* 넘쳐 쓰지 않음 */
                if (!ok) { CHECK(0); return; }
            }
    CHECK(cases == 5u * 81u * 81u);
    /* 실제 숫자 (run_qemu.sh layout): boot 6968 B + 패딩 → 16384, app 1800 → merged 18184 B.
     * 패딩 없이 cat 하면 앱이 0x1b38 에 놓인다 (run_qemu.sh nopad) */
    CHECK(16384u + 1800u == 18184u && 6968u == 0x1b38u);
    printf("연습 2  merge_image: %u cases, %u rejected (boot too big / capacity)\n", cases, errs);
}

/* ================= 연습 3: 정리 순서 =================
 * 시작 상태 = 부트로더가 점프 직전에 실제로 남긴 것 (qemu_solution.txt "state before handoff"):
 *   SysTick ENABLE+TICKINT, SysTick pending, UART0 TX 인터럽트 신호 high, IRQ7 enable+pending, IRQ1 pending.
 * QEMU probe 로 확인한 규칙: (P2) SysTick 이 켜져 있으면 언제든 다시 pending 된다,
 *   (P5) 장치 신호가 high 인 동안 ICPR 은 먹히지 않는다 (pending 이 다시 선다), (P3) ICER 는 pending 을 안 지운다.
 * 동작 5 개: SYST_OFF(CTRL=0), PENDSTCLR, UART_OFF(CTRL=0 + INTCLEAR), ICER(전부), ICPR(전부). PRIMASK=1 상태. */
enum { OP_SYST_OFF, OP_PENDSTCLR, OP_UART_OFF, OP_ICER, OP_ICPR, NOPS };
static const char *const k_op[NOPS] = { "SYST_OFF", "PENDSTCLR", "UART_OFF", "ICER", "ICPR" };
typedef struct { int syst_en, syst_pend, uart_line; uint32_t en, pend; } hstate_t;
static void hw_step(hstate_t *s) {                    /* 적대적 하드웨어: 원천이 살아 있으면 지금 막 pending 이 섰다 */
    if (s->syst_en) s->syst_pend = 1;
    if (s->uart_line) s->pend |= 1u << 1;
}
static int order_safe(const int *ops, int n) {
    hstate_t s = { 1, 1, 1, 1u << 7, (1u << 7) | (1u << 1) };
    for (int i = 0; i < n; ++i) {
        switch (ops[i]) {
        case OP_SYST_OFF: s.syst_en = 0; break;
        case OP_PENDSTCLR: s.syst_pend = 0; break;
        case OP_UART_OFF: s.uart_line = 0; break;
        case OP_ICER: s.en = 0; break;
        case OP_ICPR: s.pend = 0; break;
        default: break;
        }
        hw_step(&s);
    }
    return !s.syst_en && !s.syst_pend && !s.uart_line && s.en == 0u && s.pend == 0u;
}
static int g_safe, g_perms;
static void permute(int *a, int k) {
    if (k == NOPS) {
        g_perms++;
        int pos[NOPS];
        for (int i = 0; i < NOPS; ++i) pos[a[i]] = i;
        int want = pos[OP_SYST_OFF] < pos[OP_PENDSTCLR] && pos[OP_UART_OFF] < pos[OP_ICPR];
        int got = order_safe(a, NOPS);
        g_safe += got;
        CHECK(got == want);                            /* 안전 ⇔ "원천을 먼저 끄고, pending 은 그 다음" 두 조건 */
        return;
    }
    for (int i = k; i < NOPS; ++i) {
        int t = a[k]; a[k] = a[i]; a[i] = t;
        permute(a, k + 1);
        t = a[k]; a[k] = a[i]; a[i] = t;
    }
}
static void practice3(void) {
    int a[NOPS] = { 0, 1, 2, 3, 4 };
    permute(a, 0);
    CHECK(g_perms == 120 && g_safe == 30);
    const int sol[NOPS] = { OP_SYST_OFF, OP_PENDSTCLR, OP_UART_OFF, OP_ICER, OP_ICPR };   /* solution.c 의 순서 */
    CHECK(order_safe(sol, NOPS));
    printf("연습 3  handoff order model: %d/%d orders safe; dropping one op ->", g_safe, g_perms);
    for (int drop = 0; drop < NOPS; ++drop) {         /* 하나라도 빠지면 안전하지 않다 */
        int ops[NOPS - 1], n = 0;
        for (int i = 0; i < NOPS; ++i) if (sol[i] != drop) ops[n++] = sol[i];
        int s = order_safe(ops, n);
        CHECK(!s);
        printf(" -%s:%s", k_op[drop], s ? "safe" : "UNSAFE");
    }
    printf("\n");
}

/* ================= 연습 4: 앱 쪽 계약 검사 (app.c contract_check 의 호스트 판) ================= */
enum { V_VTOR = 1u << 0, V_MSP = 1u << 1, V_PRIMASK = 1u << 2, V_CONTROL = 1u << 3, V_SYSTICK = 1u << 4,
       V_PENDST = 1u << 5, V_NVIC_EN = 1u << 6, V_NVIC_PEND = 1u << 7, V_UART = 1u << 8, V_EARLY = 1u << 9 };
typedef struct {
    const char *step;
    uint32_t vtor, msp, primask, control, syst_csr, icsr, iser0, ispr0, uart_ctrl, early_t, early_i;
    uint32_t want;            /* 앱이 QEMU 에서 찍은 "contract:" 줄 */
} snap_t;
static uint32_t contract_check(const snap_t *s, uint32_t own_vtor, uint32_t own_top) {
    uint32_t v = 0;
    if (s->vtor != own_vtor) v |= V_VTOR;
    if (s->msp != own_top) v |= V_MSP;
    if (s->primask != 0u) v |= V_PRIMASK;
    if (s->control != 0u) v |= V_CONTROL;
    if (s->syst_csr & 0x3u) v |= V_SYSTICK;
    if (s->icsr & (1u << 26)) v |= V_PENDST;
    if (s->iser0 != 0u) v |= V_NVIC_EN;
    if (s->ispr0 != 0u) v |= V_NVIC_PEND;
    if (s->uart_ctrl != 0u) v |= V_UART;
    if (s->early_t + s->early_i != 0u) v |= V_EARLY;
    return v;
}
static void practice4(void) {
    /* qemu_solution.txt 의 "[app] entry:" 두 줄에서 옮김 (QEMU 10.2.1 실측) */
    static const snap_t s[] = {
        { "NORMAL",          0x4000u, 0x2000f800u, 0, 0, 0x00000u, 0x000800u, 0, 0, 0, 0, 0, 0u },
        { "NO_VTOR",         0x0000u, 0x2000f800u, 0, 0, 0x00000u, 0x000800u, 0, 0, 0, 0, 0, V_VTOR },
        { "SYSTICK_RUNNING", 0x4000u, 0x2000f800u, 0, 0, 0x10007u, 0x000800u, 0, 0, 0, 2, 0, V_SYSTICK | V_EARLY },
        { "PENDST_LEFT",     0x4000u, 0x2000f800u, 0, 0, 0x00000u, 0x000800u, 0, 0, 0, 1, 0, V_EARLY },
        { "NVIC_LEFT",       0x4000u, 0x2000f800u, 0, 0, 0x00000u, 0x400800u, 0, 2, 5, 0, 1,
          V_NVIC_PEND | V_UART | V_EARLY },
        { "PRIMASK_SET",     0x4000u, 0x2000f800u, 1, 0, 0x00000u, 0x000800u, 0, 0, 0, 0, 0, V_PRIMASK },
    };
    printf("연습 4  contract_check on QEMU snapshots:");
    for (size_t i = 0; i < sizeof s / sizeof s[0]; ++i) {
        uint32_t v = contract_check(&s[i], 0x4000u, 0x2000f800u);
        CHECK(v == s[i].want);
        printf(" %s=0x%03x", s[i].step, v);
    }
    printf("\n");
    /* 앱이 "기대는" 리셋값 관점에서: 정상 단계의 스냅샷이 위반 0 이어야 리셋으로 바로 부팅한 것과 구분이 안 된다 */
    snap_t warm = s[0];
    warm.control = 2u;                     /* 부트로더가 PSP(CONTROL.SPSEL) 로 돌다 그대로 점프했다면 */
    CHECK(contract_check(&warm, 0x4000u, 0x2000f800u) == V_CONTROL);
}

int main(void) {
    practice1();
    practice2();
    practice3();
    practice4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
