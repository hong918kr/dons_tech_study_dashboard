/* BM-M11 추가 연습 — 한 번에 검증
 *   연습 1: SLEEPONEXIT — 인터럽트 구동 앱에서 스택 push/pop 과 스레드 실행 횟수 세기
 *   연습 2: Standby 웜 부트 — SBF 플래그로 부팅 종류 판별, 백업 레지스터로 상태 복원
 *   연습 3: 배터리 수명 디레이팅 — 사용 가능 용량 + 자가 방전
 *   연습 4: Stop 복귀 후 클럭을 복원하지 않으면 UART baud 가 얼마나 틀어지나
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------------- 연습 1: SLEEPONEXIT ----------------
 * 스레드: SCR.SLEEPONEXIT=1 로 두고 for(;;){ WFI(); if (work) { work=0; main 처리; SLEEPONEXIT=1; } }
 * IRQ i 의 ISR 은 needs_main[i] 이면 work=1 로 하고 SLEEPONEXIT 를 끈다 (main 에서 할 일이 생김).
 * 규칙 (ARMv7-M B1.5.17): 스레드로 복귀하는 예외 복귀 때 SLEEPONEXIT=1 이면 복귀(unstack)하지 않고 잔다.
 * 그 상태에서 오는 다음 예외는 tail-chain 처럼 동작 → 다시 stack 하지 않는다.
 * 반환: 스레드(main 처리)가 실행된 횟수. *n_stack / *n_unstack = 스택 push/pop 횟수. */
typedef struct { int n_stack, n_unstack, n_main; char tr[160]; } soe_t;
static void soe_tr(soe_t *s, const char *t) {
    size_t n = strlen(s->tr);
    if (n && n + 1 < sizeof s->tr) s->tr[n++] = ' ';
    while (*t && n + 1 < sizeof s->tr) s->tr[n++] = *t++;
    s->tr[n] = '\0';
}
static void soe_run(soe_t *s, const bool *needs_main, int n, bool use_soe) {
    memset(s, 0, sizeof *s);
    bool scr_soe = use_soe;
    bool stacked = false;                      /* 스레드 문맥이 스택에 쌓인 채 자는 중인가 */
    for (int i = 0; i < n; i++) {
        if (!stacked) { s->n_stack++; stacked = true; }   /* 스레드 → 핸들러: 레지스터 8개 push */
        soe_tr(s, "IRQ");
        if (needs_main[i]) scr_soe = false;    /* ISR: work = 1; SCB->SCR &= ~SLEEPONEXIT */
        if (scr_soe) { soe_tr(s, "zz"); continue; }       /* 복귀하지 않고 잔다 */
        s->n_unstack++; stacked = false;       /* 스레드로 복귀 */
        if (needs_main[i]) { s->n_main++; soe_tr(s, "main"); }
        scr_soe = use_soe;                     /* main: 처리 후 다시 SLEEPONEXIT=1, 루프 → WFI */
        soe_tr(s, "WFI");
    }
}

/* ---------------- 연습 2: Standby 웜 부트 ----------------
 * PWR 플래그 모델: sbf = Standby 에서 깨어남, wuf = 웨이크업 핀/RTC 플래그. 둘 다 W1C 식 클리어 레지스터로 지운다.
 * node_boot(): 리셋 핸들러 → main 직후 호출. 콜드 부트면 sample_count = 0, 백업 레지스터 0.
 * 웜 부트(SBF=1)면 sample_count 를 백업 레지스터에서 복원하고, 플래그를 지운다.
 * 반환: 부팅 종류. SRAM(.bss) 은 리셋마다 0 이 된다고 가정한다 (startup 이 .bss 를 지우니까). */
typedef enum { BOOT_COLD, BOOT_FROM_STANDBY } boot_t;
typedef struct { bool sbf, wuf; uint32_t bkp_dr0; uint32_t sram_count; } node_t;
static void pwr_clear_flags(node_t *n) { n->sbf = false; n->wuf = false; }   /* PWR_SCR = CSBF|CWUF */
static boot_t node_boot(node_t *n) {
    n->sram_count = 0u;                         /* startup 의 .bss 제로화 */
    if (n->sbf) {
        n->sram_count = n->bkp_dr0;
        pwr_clear_flags(n);
        return BOOT_FROM_STANDBY;
    }
    n->bkp_dr0 = 0u;
    return BOOT_COLD;
}
static void node_sample_and_standby(node_t *n) {   /* 센서 한 번 읽고 상태를 백업 레지스터에 둔 뒤 Standby */
    n->sram_count++;
    n->bkp_dr0 = n->sram_count;
    n->sbf = true; n->wuf = true;               /* 하드웨어: RTC 웨이크업으로 Standby 탈출 → 리셋 */
}

/* ---------------- 연습 3: 배터리 수명 디레이팅 ----------------
 * 사용 가능 용량 = cap * usable_pct / 100. 자가 방전 sd_permille_per_year (‰/년) 를 등가 전류로:
 *   I_sd [nA] = cap[mAh] * 1e6 * sd/1000 / 8760 h.   수명[일] = 사용가능[nA·h] / (avg + I_sd) / 24. */
static uint32_t self_discharge_nA(uint32_t cap_mAh, uint32_t sd_permille) {
    return (uint32_t)((uint64_t)cap_mAh * 1000000u * sd_permille / 1000u / 8760u);
}
static uint32_t life_days_derated(uint32_t cap_mAh, uint32_t avg_nA, uint32_t usable_pct, uint32_t sd_permille) {
    uint64_t usable_nAh = (uint64_t)cap_mAh * 1000000u * usable_pct / 100u;
    uint64_t i = (uint64_t)avg_nA + self_discharge_nA(cap_mAh, sd_permille);
    return i ? (uint32_t)(usable_nAh / i / 24u) : UINT32_MAX;
}

/* ---------------- 연습 4: Stop 복귀 후 클럭 ----------------
 * USART BRR = round(pclk / baud) (오버샘플링 16). Stop 에서 깨면 SYSCLK 가 PLL 80 MHz → HSI16 16 MHz 로 바뀐다.
 * 클럭을 복원하지 않고 보내면 실제 baud = pclk_now / BRR. 오차 [%·100] 를 돌려준다 (부호 있음). */
static uint32_t usart_brr(uint32_t pclk, uint32_t baud) { return (pclk + baud / 2u) / baud; }
static uint32_t usart_actual(uint32_t pclk, uint32_t brr) { return pclk / brr; }
static int32_t baud_err_x100(uint32_t want, uint32_t got) {
    return (int32_t)(((int64_t)got - (int64_t)want) * 10000 / (int64_t)want);
}

int main(void) {
    /* 연습 1 */
    soe_t s;
    bool ev[5] = { false, false, false, true, false };
    soe_run(&s, ev, 5, false);                  /* SLEEPONEXIT 없이: 매번 stack/unstack, 스레드 왕복 */
    CHECK(s.n_stack == 5 && s.n_unstack == 5 && s.n_main == 1);
    CHECK(strcmp(s.tr, "IRQ WFI IRQ WFI IRQ WFI IRQ main WFI IRQ WFI") == 0);
    soe_run(&s, ev, 5, true);                   /* SLEEPONEXIT: 처리할 일이 생길 때만 스레드로 */
    CHECK(s.n_stack == 2 && s.n_unstack == 1 && s.n_main == 1);
    CHECK(strcmp(s.tr, "IRQ zz IRQ zz IRQ zz IRQ main WFI IRQ zz") == 0);
    bool none[3] = { false, false, false };
    soe_run(&s, none, 3, true);                 /* main 이 할 일이 없으면 스레드는 한 번도 안 돈다 */
    CHECK(s.n_stack == 1 && s.n_unstack == 0 && s.n_main == 0);

    /* 연습 2 */
    node_t n;
    memset(&n, 0, sizeof n);
    n.bkp_dr0 = 0xDEADu;                        /* 전원 투입 직후 백업 도메인은 쓰레기일 수 있다 */
    CHECK(node_boot(&n) == BOOT_COLD && n.sram_count == 0u && n.bkp_dr0 == 0u);
    for (int i = 0; i < 3; i++) {
        node_sample_and_standby(&n);
        CHECK(node_boot(&n) == BOOT_FROM_STANDBY);
    }
    CHECK(n.sram_count == 3u && !n.sbf && !n.wuf);   /* 3번째 샘플까지 이어서 센다, 플래그는 지웠다 */
    CHECK(node_boot(&n) == BOOT_COLD);          /* 플래그를 지웠으니 다음 (핀 리셋 등) 은 콜드 부트 */

    /* 연습 3: Standby 노드(857 nA)와 Stop 노드(1951 nA), CR2032 225 mAh, 사용 가능 80 %, 자가 방전 1 %/년 */
    CHECK(self_discharge_nA(225u, 10u) == 256u);
    CHECK(life_days_derated(225u, 857u, 100u, 0u) == 10939u);    /* 이상적: 약 30년 */
    CHECK(life_days_derated(225u, 857u, 80u, 10u) == 6738u);     /* 현실적: 약 18.4년 (-38 %) */
    CHECK(life_days_derated(225u, 1951u, 80u, 10u) == 3398u);
    CHECK(life_days_derated(225u, 0u, 80u, 0u) == UINT32_MAX);

    /* 연습 4 */
    uint32_t brr = usart_brr(80000000u, 115200u);
    CHECK(brr == 694u && usart_actual(80000000u, brr) == 115273u);
    CHECK(baud_err_x100(115200u, usart_actual(80000000u, brr)) == 6);         /* +0.06 % */
    CHECK(usart_actual(16000000u, brr) == 23054u);                             /* 클럭 복원 안 함 */
    CHECK(baud_err_x100(115200u, usart_actual(16000000u, brr)) == -7998);      /* -80 % → 쓰레기 문자 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
