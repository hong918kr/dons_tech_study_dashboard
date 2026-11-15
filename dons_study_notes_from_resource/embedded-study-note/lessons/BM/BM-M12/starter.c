/* BM-M12 RTC와 저전력 타임키핑: 알람 웨이크업·백업 도메인
 *
 * Part A — BCD 헬퍼, TR/DR 패킹, 32.768 kHz 프리스케일러 계산(PREDIV_A 최대 우선), 쓰기 보호(WPR) 해제 +
 *          INIT/INITF 핸드셰이크로 rtc_set, 섀도 레지스터 순서(SSR→TR→DR)로 rtc_get + 서브초(ms).
 * Part B — 결함 주입: DBP·WPR·INITF 를 빼먹은 쓰기는 무시됨, 자정 찢어짐(torn read)을 "모든 버스 접근 사이에
 *          RTC 클럭 2사이클" 을 끼워 넣어 재현 → 섀도 순서 / 두 번 읽기(BYPSHAD=1)로 고침, 시간만 읽어 DR 이
 *          잠긴 채 남는 버그, 설정 직후·Stop 복귀 직후 RSF 재동기화, ALRAE=1 상태의 ALRMAR 쓰기 무시,
 *          알람 ISR 의 플래그 지우기 함정 3종 (INIT 덮어쓰기 / EXTI_PR 안 지움 / ALRAF 안 지움).
 * Part C — epoch 변환(days_from_civil), 요일, "다음 07:30" 일일 알람 + Stop 슬립 시뮬레이션, 15분 주기 웨이크
 *          (윤일 자정 넘김), 웜 리셋(백업 도메인 유지) vs 콜드(VBAT 소실) 부트.
 *
 * 레지스터 주소·비트는 RM0090(STM32F4) 기준 대표값 — RM 확인 권장. 테스트의 정답은 이 mock 의 정의다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/BM-M12_so && /tmp/BM-M12_so   (-v: rtc_set 접근 로그)
 */
#define UNIT_TEST 1
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= 레지스터 접근 seam (BM-B02 mock 축약판) ======================= */
#ifdef UNIT_TEST                      /* 호스트 빌드: 모든 접근이 mock 훅을 지난다 */
uint32_t mock_read32(uint32_t addr);
void     mock_write32(uint32_t addr, uint32_t val);
void     sim_cpu_wait(void);
#define REG_READ(addr)        mock_read32(addr)
#define REG_WRITE(addr, val)  mock_write32((addr), (val))
#define BUSY_WAIT_STEP()      sim_cpu_wait()      /* 폴링 한 바퀴 = RTCCLK 1 사이클이 흐른다 (모델 정의) */
#else                                 /* 타깃 빌드: 진짜 MMIO */
#define REG_READ(addr)        (*(volatile uint32_t *)(uintptr_t)(addr))
#define REG_WRITE(addr, val)  (*(volatile uint32_t *)(uintptr_t)(addr) = (val))
#define BUSY_WAIT_STEP()      ((void)0)
#endif

/* ---- RM0090 기준 대표값 (RM 확인 권장) ---- */
#define PWR_CR            0x40007000u
#define PWR_CR_DBP        (1u << 8)          /* 백업 도메인 쓰기 허용 */
#define RCC_BDCR          0x40023870u        /* 백업 도메인 안: DBP 필요 */
#define BDCR_LSEON        (1u << 0)
#define BDCR_LSERDY       (1u << 1)
#define BDCR_RTCSEL_Msk   (3u << 8)
#define BDCR_RTCSEL_LSE   (1u << 8)
#define BDCR_RTCEN        (1u << 15)
#define RTC_BASE          0x40002800u
#define RTC_TR            (RTC_BASE + 0x00u)
#define RTC_DR            (RTC_BASE + 0x04u)
#define RTC_CR            (RTC_BASE + 0x08u)
#define RTC_ISR           (RTC_BASE + 0x0Cu)
#define RTC_PRER          (RTC_BASE + 0x10u)
#define RTC_ALRMAR        (RTC_BASE + 0x1Cu)
#define RTC_WPR           (RTC_BASE + 0x24u)
#define RTC_SSR           (RTC_BASE + 0x28u)
#define RTC_BKP0R         (RTC_BASE + 0x50u)
#define CR_BYPSHAD        (1u << 5)
#define CR_FMT            (1u << 6)          /* 0 = 24시간 */
#define CR_ALRAE          (1u << 8)
#define CR_ALRAIE         (1u << 12)
#define ISR_ALRAWF        (1u << 0)          /* r: ALRMAR 써도 됨 */
#define ISR_INITS         (1u << 4)          /* r: 연도 != 0 (한 번이라도 설정됨) */
#define ISR_RSF           (1u << 5)          /* rc_w0: 섀도 동기 완료 */
#define ISR_INITF         (1u << 6)          /* r: 초기화 모드 진입 완료 */
#define ISR_INIT          (1u << 7)          /* rw: 초기화 모드 요청 */
#define ISR_ALRAF         (1u << 8)          /* rc_w0: 알람 A */
#define ALRM_MSK1         (1u << 7)          /* 초 무시 */
#define ALRM_MSK2         (1u << 15)         /* 분 무시 */
#define ALRM_MSK3         (1u << 23)         /* 시 무시 */
#define ALRM_WDSEL        (1u << 30)         /* DU = 요일 */
#define ALRM_MSK4         (1u << 31)         /* 날짜/요일 무시 */
#define EXTI_IMR          0x40013C00u
#define EXTI_RTSR         0x40013C08u
#define EXTI_PR           0x40013C14u        /* rc_w1 (W1C) */
#define EXTI_LINE_ALARM   (1u << 17)         /* RTC Alarm → EXTI 17 (F4) */
#define RTC_ALARM_IRQn    41
#define WPR_KEY1          0xCAu
#define WPR_KEY2          0x53u
#define BKP_MAGIC         0x32F2u            /* "이 RTC 는 우리가 설정했다" 표식 */
#define RTC_POLL_MAX      64                 /* 모든 폴링 루프의 상한 (스텁이 멈추지 않게) */
#define RTC_EPOCH_MIN     946684800u         /* 2000-01-01 00:00:00 */
#define RTC_EPOCH_MAX     4102444799u        /* 2099-12-31 23:59:59 (2자리 연도 범위) */

typedef struct { uint16_t year; uint8_t mon, day, wday, hour, min, sec; uint16_t ms; } rtc_time_t;  /* wday 1=월..7=일 */
enum { RTC_OK = 0, RTC_EINVAL = -1, RTC_ETIMEOUT = -2, RTC_EINCONSISTENT = -3 };

/* ---- mock core: 레지스터 표 + 훅 + 접근 로그 + "n 번째 접근 직후 끼어들기" ---- */
typedef uint32_t (*mock_on_read)(uint32_t addr, uint32_t stored, void *ctx);
typedef uint32_t (*mock_on_write)(uint32_t addr, uint32_t old, uint32_t val, void *ctx);
typedef void (*mock_fn)(void);
typedef struct { uint32_t addr, stored; mock_on_read r; mock_on_write w; void *ctx; } mock_reg_t;
typedef struct { char op; uint32_t addr, val; } mock_log_t;
#define MOCK_MAX_REGS 16
#define MOCK_LOG_CAP  64
static mock_reg_t g_reg[MOCK_MAX_REGS];
static int        g_nreg, g_nlog, g_unexpected;
static mock_log_t g_log[MOCK_LOG_CAP];
static uint32_t   g_nacc;
static struct { uint32_t at; mock_fn fn; } g_inj;

void mock_reset(void) {
    memset(g_reg, 0, sizeof g_reg); g_nreg = 0; g_nlog = 0; g_nacc = 0; g_unexpected = 0;
    memset(&g_inj, 0, sizeof g_inj);
}
int mock_add_reg(uint32_t addr, uint32_t reset_val, mock_on_read r, mock_on_write w, void *ctx) {
    if (g_nreg >= MOCK_MAX_REGS) return -1;
    g_reg[g_nreg] = (mock_reg_t){ addr, reset_val, r, w, ctx };
    return g_nreg++;
}
static mock_reg_t *mock_find(uint32_t addr) {
    for (int i = 0; i < g_nreg; i++) if (g_reg[i].addr == addr) return &g_reg[i];
    return NULL;
}
static void mock_log_add(char op, uint32_t addr, uint32_t val) {
    if (g_nlog < MOCK_LOG_CAP) g_log[g_nlog++] = (mock_log_t){ op, addr, val };
}
static void mock_after_access(void) {
    if (g_inj.fn != NULL && g_nacc == g_inj.at) { mock_fn fn = g_inj.fn; g_inj.fn = NULL; fn(); }
}
uint32_t mock_read32(uint32_t addr) {
    mock_reg_t *r = mock_find(addr);
    uint32_t v = 0;
    if (r == NULL) g_unexpected++;
    else v = r->r ? r->r(addr, r->stored, r->ctx) : r->stored;
    mock_log_add('R', addr, v); g_nacc++; mock_after_access();
    return v;
}
void mock_write32(uint32_t addr, uint32_t val) {
    mock_reg_t *r = mock_find(addr);
    if (r == NULL) g_unexpected++;
    else r->stored = r->w ? r->w(addr, r->stored, val, r->ctx) : val;
    mock_log_add('W', addr, val); g_nacc++; mock_after_access();
}
static uint32_t mock_peek(uint32_t addr) { mock_reg_t *r = mock_find(addr); return r ? r->stored : 0u; }
static void mock_poke(uint32_t addr, uint32_t v) { mock_reg_t *r = mock_find(addr); if (r) r->stored = v; }
/* 지금부터 n 번째 CPU 접근 직후 fn 실행 (n = 0 이면 바로) — BM-B04 의 끼어들기 훅 */
static void mock_inject_at(uint32_t n, mock_fn fn) {
    if (n == 0) { fn(); return; }
    g_inj.at = g_nacc + n; g_inj.fn = fn;
}

/* ---- NVIC-lite: 주변장치 모델이 라인을 올리고, sim 이 enable+pending 이면 핸들러를 동기 호출 ---- */
typedef void (*isr_fn)(void);
#define NIRQ 96
#define DISPATCH_CAP 50                      /* 같은 IRQ 연속 재진입 상한 = "ISR 폭주" 판정 */
static bool     g_nvic_en[NIRQ], g_line[NIRQ];
static isr_fn   g_vec[NIRQ];
static uint32_t g_isr_entries;
static bool     g_storm;
static void irq_line_set(int irq, bool level) { g_line[irq] = level; }
static void nvic_enable_irq(int irq) { g_nvic_en[irq] = true; }
static bool irq_any_pending(void) {
    for (int i = 0; i < NIRQ; i++) if (g_nvic_en[i] && g_line[i]) return true;
    return false;
}
static void sim_dispatch(void) {
    for (int n = 0; n < DISPATCH_CAP; n++) {
        int hit = -1;
        for (int i = 0; i < NIRQ && hit < 0; i++) if (g_nvic_en[i] && g_line[i] && g_vec[i]) hit = i;
        if (hit < 0) return;
        g_isr_entries++;
        g_vec[hit]();                         /* 핸들러가 라인을 내리지 않으면 다시 들어온다 (tail-chain) */
    }
    g_storm = true;
}

/* ======================= RTC + 백업 도메인 모델 (하드웨어 쪽 — TODO 아님) ======================= */
typedef struct {
    uint32_t bdcr; int lse_wait;
    uint32_t tr, dr, cr, prer, alrmar, bkp0;          /* 살아 있는 캘린더 카운터(BCD) / 레지스터 */
    bool     init, initf, rsf, alraf, alrawf;
    int      initf_cnt, alrawf_cnt, restart_cnt;
    uint32_t async_cnt, ss;                           /* 프리스케일러 다운카운터 (ss = SSR 실체) */
    uint32_t sh_tr, sh_dr, sh_ss; bool lk_tr, lk_dr;  /* 섀도 레지스터 + 잠금 */
    int      wpr;                                     /* 0 잠김, 1 0xCA 받음, 2 풀림 */
    bool     apb_on;                                  /* Stop 중 false → 섀도 복사 멈춤 */
    bool     pwr_clock_off;                           /* 결함 주입: PWR 클럭 꺼짐 → DBP 쓰기 무시 */
    uint64_t cyc; uint32_t seconds, alarms;
    uint32_t ign_dbp, ign_wpr, ign_init, ign_alrm;    /* 무시된 쓰기 (원인별) */
} rtc_model_t;
static rtc_model_t R;
#define M_PA ((R.prer >> 16) & 0x7Fu)
#define M_PS (R.prer & 0x7FFFu)

static uint32_t m_b(uint32_t bcd) { return (bcd >> 4) * 10u + (bcd & 0xFu); }
static uint32_t m_bcd(uint32_t v) { return ((v / 10u) << 4) | (v % 10u); }
static uint32_t m_mdays(uint32_t mon, uint32_t yy) {
    static const uint8_t d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (mon < 1u || mon > 12u) return 31u;
    return (mon == 2u && yy % 4u == 0u) ? 29u : d[mon - 1u];    /* 2자리 연도: 4의 배수 = 윤년 (2000–2099 에서 맞다) */
}
static bool m_running(void) {
    return (R.bdcr & BDCR_RTCEN) && (R.bdcr & BDCR_RTCSEL_Msk) == BDCR_RTCSEL_LSE && (R.bdcr & BDCR_LSERDY);
}
static void m_exti_update(void) {
    irq_line_set(RTC_ALARM_IRQn, (mock_peek(EXTI_PR) & mock_peek(EXTI_IMR) & EXTI_LINE_ALARM) != 0u);
}
static void m_exti_edge(uint32_t line) {          /* 상승 에지: RTSR·IMR 이 켜져 있으면 PR 세움 (모델 정의) */
    if ((mock_peek(EXTI_RTSR) & line) && (mock_peek(EXTI_IMR) & line)) mock_poke(EXTI_PR, mock_peek(EXTI_PR) | line);
    m_exti_update();
}
static void m_alarm_check(void) {                 /* 캘린더가 1초 증가할 때마다 비교 (MASKSS=0 동작) */
    if (!(R.cr & CR_ALRAE)) return;
    uint32_t a = R.alrmar;
    bool ok = true;
    if (!(a & ALRM_MSK1) && (a & 0x7Fu) != (R.tr & 0x7Fu)) ok = false;
    if (!(a & ALRM_MSK2) && ((a >> 8) & 0x7Fu) != ((R.tr >> 8) & 0x7Fu)) ok = false;
    if (!(a & ALRM_MSK3) && ((a >> 16) & 0x7Fu) != ((R.tr >> 16) & 0x7Fu)) ok = false;
    if (!(a & ALRM_MSK4)) {
        if (a & ALRM_WDSEL) { if (((a >> 24) & 0xFu) != ((R.dr >> 13) & 7u)) ok = false; }
        else if (((a >> 24) & 0x3Fu) != (R.dr & 0x3Fu)) ok = false;
    }
    if (!ok) return;
    R.alarms++;
    if (!R.alraf) { R.alraf = true; m_exti_edge(EXTI_LINE_ALARM); }   /* ALRAF 0→1 일 때만 에지 */
}
static void m_cal_inc_second(void) {
    uint32_t s = m_b(R.tr & 0x7Fu), mi = m_b((R.tr >> 8) & 0x7Fu), h = m_b((R.tr >> 16) & 0x3Fu);
    uint32_t d = m_b(R.dr & 0x3Fu), mo = m_b((R.dr >> 8) & 0x1Fu), wd = (R.dr >> 13) & 7u, y = m_b((R.dr >> 16) & 0xFFu);
    if (++s == 60u) { s = 0; if (++mi == 60u) { mi = 0; if (++h == 24u) { h = 0; wd = wd % 7u + 1u;
        if (++d > m_mdays(mo, y)) { d = 1; if (++mo > 12u) { mo = 1; y = (y + 1u) % 100u; } } } } }
    R.tr = (m_bcd(h) << 16) | (m_bcd(mi) << 8) | m_bcd(s);
    R.dr = (m_bcd(y) << 16) | (wd << 13) | (m_bcd(mo) << 8) | m_bcd(d);
    R.seconds++;
    m_alarm_check();
}
static void m_prescale(void) {                    /* ck_apre = RTCCLK/(A+1), ck_spre = ck_apre/(S+1) */
    if (R.async_cnt) { R.async_cnt--; return; }
    R.async_cnt = M_PA;
    if (R.ss) { R.ss--; return; }
    R.ss = M_PS;
    m_cal_inc_second();
}
static void m_shadow_copy(void) {                 /* 2 RTCCLK 마다: 잠기지 않은 섀도만 복사, RSF=1 */
    if (R.cr & CR_BYPSHAD) return;
    R.sh_ss = R.ss;
    if (!R.lk_tr) R.sh_tr = R.tr;
    if (!R.lk_dr) R.sh_dr = R.dr;
    R.rsf = true;
}
static void rtc_clk1(void) {                      /* RTCCLK 한 사이클 */
    R.cyc++;
    if (R.lse_wait > 0 && --R.lse_wait == 0) R.bdcr |= BDCR_LSERDY;
    if (!m_running()) return;
    if (R.init) { if (!R.initf && --R.initf_cnt <= 0) R.initf = true; return; }   /* 캘린더 멈춤 */
    if (!R.alrawf && !(R.cr & CR_ALRAE) && --R.alrawf_cnt <= 0) R.alrawf = true;
    if (R.restart_cnt > 0) R.restart_cnt--; else m_prescale();                    /* INIT 해제 후 4 사이클 */
    if (R.apb_on && (R.cyc & 1u) == 0u) m_shadow_copy();
}
static void rtc_clk(uint32_t n) { while (n--) rtc_clk1(); }
static void rtc_ff_second(void) {                 /* 다음 ck_spre 경계까지 한 번에 (긴 슬립용) */
    if (!m_running() || R.init) { R.cyc += 32768u; return; }
    R.cyc += (uint64_t)R.restart_cnt + R.async_cnt + 1u + (uint64_t)R.ss * (M_PA + 1u);
    R.restart_cnt = 0; R.async_cnt = M_PA; R.ss = M_PS;
    if (!R.alrawf && !(R.cr & CR_ALRAE)) R.alrawf = true;
    m_cal_inc_second();
    if (R.apb_on) m_shadow_copy();
}
static uint32_t m_isr_value(void) {
    return (R.alrawf ? ISR_ALRAWF : 0u) | ((R.dr & 0xFF0000u) ? ISR_INITS : 0u) | (R.rsf ? ISR_RSF : 0u) |
           (R.initf ? ISR_INITF : 0u) | (R.init ? ISR_INIT : 0u) | (R.alraf ? ISR_ALRAF : 0u);
}
static uint32_t rtc_on_read(uint32_t addr, uint32_t stored, void *ctx) {
    (void)stored; (void)ctx;
    bool byp = (R.cr & CR_BYPSHAD) != 0;
    switch (addr) {
    case RTC_TR:  if (byp) return R.tr; R.lk_dr = true; return R.sh_tr;             /* TR 읽기 → DR 잠금 */
    case RTC_DR:  if (byp) return R.dr; R.lk_tr = R.lk_dr = false; return R.sh_dr;  /* DR 읽기 → 잠금 해제 */
    case RTC_SSR: if (byp) return R.ss; R.lk_tr = R.lk_dr = true; return R.sh_ss;   /* SSR 읽기 → TR·DR 잠금 */
    case RTC_CR:     return R.cr;
    case RTC_ISR:    return m_isr_value();
    case RTC_PRER:   return R.prer;
    case RTC_ALRMAR: return R.alrmar;
    case RTC_BKP0R:  return R.bkp0;
    case RCC_BDCR:   return R.bdcr;
    default:         return 0u;                                                     /* WPR 은 읽으면 0 */
    }
}
static void m_isr_write(uint32_t v) {
    if (!(v & ISR_ALRAF)) R.alraf = false;                     /* [13:8] rc_w0, WPR 보호 밖 */
    bool want_init = (v & ISR_INIT) != 0, clr_rsf = !(v & ISR_RSF);
    if (want_init == R.init && !(clr_rsf && R.rsf)) return;    /* 보호 비트는 변화 없음 */
    if (R.wpr != 2) { R.ign_wpr++; return; }
    if (clr_rsf) R.rsf = false;
    if (want_init && !R.init) {
        R.init = true; R.initf = false; R.initf_cnt = 2; R.rsf = false;      /* INITF 는 ~2 RTCCLK 뒤 */
        R.async_cnt = M_PA; R.ss = M_PS;                                     /* 프리스케일러 재시작 (모델 정의) */
    } else if (!want_init && R.init) {
        R.init = false; R.initf = false; R.restart_cnt = 4;                  /* 4 RTCCLK 뒤 다시 센다 */
    }
}
static void m_cr_write(uint32_t v) {
    bool was = (R.cr & CR_ALRAE) != 0, now = (v & CR_ALRAE) != 0;
    R.cr = v & (CR_BYPSHAD | CR_FMT | CR_ALRAE | CR_ALRAIE);
    if (now) R.alrawf = false;
    else if (was) { R.alrawf = false; R.alrawf_cnt = 2; }                    /* ALRAWF 는 ~2 RTCCLK 뒤 */
    if (R.cr & CR_BYPSHAD) R.rsf = false;
}
static void m_bdcr_write(uint32_t v) {
    bool rdy = (R.bdcr & BDCR_LSERDY) && (v & BDCR_LSEON);
    if ((v & BDCR_LSEON) && !(R.bdcr & BDCR_LSEON)) R.lse_wait = 5;       /* LSE 기동 시간 (모델: 5 사이클) */
    uint32_t sel = R.bdcr & BDCR_RTCSEL_Msk;
    if (sel == 0u) sel = v & BDCR_RTCSEL_Msk;                              /* RTCSEL 은 BDRST 전까지 한 번만 */
    R.bdcr = (rdy ? BDCR_LSERDY : 0u) | (v & (BDCR_LSEON | BDCR_RTCEN)) | sel;
}
static uint32_t rtc_on_write(uint32_t addr, uint32_t old, uint32_t v, void *ctx) {
    (void)old; (void)ctx;
    if (!(mock_peek(PWR_CR) & PWR_CR_DBP)) { R.ign_dbp++; return 0u; }    /* 백업 도메인 쓰기 보호 */
    switch (addr) {
    case RCC_BDCR:  m_bdcr_write(v); return 0u;
    case RTC_BKP0R: R.bkp0 = v; return 0u;                                 /* BKPxR: DBP 만, WPR 대상 아님 */
    case RTC_WPR:   R.wpr = (v == WPR_KEY1) ? 1 : (v == WPR_KEY2 && R.wpr == 1) ? 2 : 0; return 0u;
    case RTC_ISR:   m_isr_write(v); return 0u;
    default: break;
    }
    if (R.wpr != 2) { R.ign_wpr++; return 0u; }
    switch (addr) {
    case RTC_TR: case RTC_DR: case RTC_PRER:
        if (!R.initf) { R.ign_init++; return 0u; }                         /* 초기화 모드에서만 */
        if (addr == RTC_TR) R.tr = v & 0x007F7F7Fu;
        else if (addr == RTC_DR) R.dr = v & 0x00FFFF3Fu;
        else { R.prer = v & 0x007F7FFFu; R.async_cnt = M_PA; R.ss = M_PS; }
        return 0u;
    case RTC_CR: m_cr_write(v); return 0u;
    case RTC_ALRMAR:
        if (!(R.alrawf || R.initf)) { R.ign_alrm++; return 0u; }           /* ALRAWF=1 또는 INIT 중에만 */
        R.alrmar = v; return 0u;
    default: return 0u;
    }
}
static uint32_t pwr_on_write(uint32_t addr, uint32_t old, uint32_t v, void *ctx) {
    (void)addr; (void)ctx;
    return R.pwr_clock_off ? old : v;
}
static uint32_t exti_imr_write(uint32_t addr, uint32_t old, uint32_t v, void *ctx) {
    (void)addr; (void)old; (void)ctx;
    irq_line_set(RTC_ALARM_IRQn, (mock_peek(EXTI_PR) & v & EXTI_LINE_ALARM) != 0u);
    return v;
}
static uint32_t exti_pr_write(uint32_t addr, uint32_t old, uint32_t v, void *ctx) {    /* W1C */
    (void)addr; (void)ctx;
    uint32_t n = old & ~v;
    irq_line_set(RTC_ALARM_IRQn, (n & mock_peek(EXTI_IMR) & EXTI_LINE_ALARM) != 0u);
    return n;
}
static void model_backup_domain_reset(void) {      /* VBAT 까지 끊김 = 백업 도메인 리셋 */
    memset(&R, 0, sizeof R);
    R.dr = 0x00002101u; R.prer = 0x007F00FFu; R.alrawf = true;            /* RM0090 리셋값 (확인 권장) */
    R.async_cnt = 0x7Fu; R.ss = 0xFFu; R.sh_dr = R.dr;
}

/* 호스트 시뮬레이션 API */
void sim_cpu_wait(void) { rtc_clk1(); }
static void sim_step(void) { rtc_clk1(); sim_dispatch(); }
static void sim_run_cycles(uint32_t n) { while (n--) sim_step(); }
/* Stop 모드: APB 꺼짐(섀도 복사 없음), RTC 는 LSE 로 계속. enable 된 IRQ 라인이 서면 깬다 → 핸들러 실행 */
static uint32_t sim_stop_until_irq(uint32_t max_s) {
    uint32_t n = 0;
    R.apb_on = false;
    while (n < max_s && !irq_any_pending()) { rtc_ff_second(); n++; }
    R.apb_on = true;
    sim_dispatch();
    return n;
}

/* ======================= 드라이버 (이 아래 TODO 를 채운다) ======================= */
static uint32_t      g_prer = (127u << 16) | 255u;   /* rtc_init 이 계산해 둔다 */
static volatile bool g_alarm_flag;
static volatile uint32_t g_alarm_count;

/* ---- Part A: BCD ---- */
bool bcd_ok(uint8_t b) {                 /* 두 니블 모두 0..9 */
    (void)b; return true;                /* TODO */
}
uint8_t bin2bcd(uint8_t v) {             /* 0..99 → 0x00..0x99 */
    return v;                            /* TODO */
}
uint8_t bcd2bin(uint8_t b) {
    return b;                            /* TODO */
}

/* ---- Part A: TR/DR 패킹. TR: HT/HU[21:16] MNT/MNU[14:8] ST/SU[6:0] (PM[22]=0, 24h)
 *      DR: YT/YU[23:16] WDU[15:13] MT/MU[12:8] DT/DU[5:0]. 서브초 ms = (PREDIV_S − SS)·1000/(PREDIV_S+1) 내림 ---- */
void rtc_pack(const rtc_time_t *t, uint32_t *tr, uint32_t *dr) {
    (void)t; *tr = 0; *dr = 0;           /* TODO */
}
void rtc_unpack(uint32_t ss, uint32_t tr, uint32_t dr, uint32_t prediv_s, rtc_time_t *t) {
    (void)ss; (void)tr; (void)dr; (void)prediv_s;
    memset(t, 0, sizeof *t);             /* TODO */
}

/* ---- Part A: 프리스케일러. (A+1)(S+1) 이 f 에 가장 가깝게 (상대오차 최소), 같으면 A 큰 쪽 (전력),
 *      A ≤ 127, S ≤ 32767. 반환 0 = 정확, 1 = 근사 (*ppm = ck_spre 오차, + 면 빠름, 반올림), -1 = f==0 ---- */
int rtc_prescaler(uint32_t f, uint32_t *pa, uint32_t *ps, int32_t *ppm) {
    (void)f; *pa = 0; *ps = 0; *ppm = 0;
    return RTC_EINVAL;                   /* TODO */
}

/* ---- Part A: 쓰기 보호 / 초기화 모드 / 섀도 재동기화 ---- */
void rtc_unlock(void) {                  /* WPR 에 0xCA, 0x53 를 연속으로 */
    /* TODO */
}
void rtc_lock(void) {                    /* 틀린 키 하나 → 다시 잠김 */
    /* TODO */
}
int rtc_enter_init(void) {               /* INIT=1 (rc_w0 플래그는 건드리지 않게) → INITF 폴링 (상한 RTC_POLL_MAX) */
    return RTC_ETIMEOUT;                 /* TODO */
}
void rtc_exit_init(void) {               /* INIT=0, 플래그는 그대로 */
    /* TODO */
}
int rtc_wait_sync(void) {                /* (WPR 풀린 상태) RSF 를 0 으로 지우고 다시 1 이 될 때까지 */
    return RTC_OK;                       /* TODO: 지금은 기다리지 않는다 */
}
int rtc_resync_after_wake(void) {        /* (주어진 코드) Stop/Standby 복귀 직후 첫 읽기 전에 */
    rtc_unlock();
    int rc = rtc_wait_sync();
    rtc_lock();
    return rc;
}

/* ---- Part C 함수 원형 (rtc_set 이 요일 계산에 쓴다) ---- */
int32_t days_from_civil(int32_t y, uint32_t m, uint32_t d);
int weekday_from_days(int32_t z);

bool rtc_time_valid(const rtc_time_t *t) {     /* (주어진 코드) 2000..2099 범위 + 달력 */
    static const uint8_t md[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (t->year < 2000u || t->year > 2099u || t->mon < 1u || t->mon > 12u || t->day < 1u) return false;
    uint32_t dim = md[t->mon - 1u] + ((t->mon == 2u && t->year % 4u == 0u) ? 1u : 0u);
    return t->day <= dim && t->hour < 24u && t->min < 60u && t->sec < 60u;
}

/* ---- Part A: rtc_set — DBP → unlock → INIT/INITF → PRER(두 번) → TR → DR → FMT=0 → INIT 해제
 *      → (BYPSHAD=0 이면) RSF 재동기화 → lock. 요일은 t->wday 를 믿지 말고 날짜로 계산 ---- */
int rtc_set(const rtc_time_t *t) {
    (void)t;
    return RTC_EINVAL;                   /* TODO */
}

/* ---- Part A: rtc_get (BYPSHAD=0) — 섀도 잠금 규칙에 맞는 순서로 세 레지스터 ---- */
int rtc_get(rtc_time_t *t) {
    memset(t, 0, sizeof *t);
    return RTC_EINVAL;                   /* TODO */
}

/* ---- Part B: rtc_get_bypass (BYPSHAD=1, 섀도 없음) — 두 번 읽어 같을 때만 (최대 3회) ---- */
int rtc_get_bypass(rtc_time_t *t) {
    memset(t, 0, sizeof *t);
    return RTC_EINCONSISTENT;            /* TODO */
}

/* ---- Part B: 알람 A 쓰기 — ALRAE 끄기 → ALRAWF 대기 → ALRMAR → 묵은 ALRAF 지우기 → ALRAE|ALRAIE ---- */
int rtc_alarm_write(uint32_t alrmar) {
    (void)alrmar;
    return RTC_ETIMEOUT;                 /* TODO */
}

/* ---- Part B/C: 알람 ISR — ALRAF 지우기(INIT 보존, rc_w0) + EXTI_PR 17 지우기(W1C) + 플래그 ---- */
void RTC_Alarm_IRQHandler(void) {
    g_alarm_flag = true;                 /* TODO: 플래그를 하나도 안 지운다 → 폭주 */
    g_alarm_count++;
}

/* ---- Part C: 달력 ↔ 일수 (1970-01-01 = 0). H. Hinnant, "chrono-Compatible Low-Level Date Algorithms" ---- */
int32_t days_from_civil(int32_t y, uint32_t m, uint32_t d) {
    (void)y; (void)m; (void)d;
    return 0;                            /* TODO */
}
void civil_from_days(int32_t z, int32_t *y, uint32_t *m, uint32_t *d) {
    (void)z; *y = 1970; *m = 1; *d = 1;  /* TODO */
}
int weekday_from_days(int32_t z) {       /* 1=월..7=일, 1970-01-01 은 목요일(4) */
    (void)z; return 1;                   /* TODO */
}
int rtc_to_epoch(const rtc_time_t *t, uint32_t *e) {
    (void)t; *e = 0;
    return RTC_EINVAL;                   /* TODO */
}
int epoch_to_rtc(uint32_t e, rtc_time_t *t) {      /* RTC 범위(2000–2099) 밖이면 RTC_EINVAL */
    (void)e; memset(t, 0, sizeof *t);
    return RTC_EINVAL;                   /* TODO */
}

/* ---- Part C: 알람 값 만들기 + 다음 발생 시각 ---- */
uint32_t alarm_daily(uint8_t h, uint8_t m, uint8_t s) {   /* 매일 h:m:s — 날짜 무시(MSK4) */
    (void)h; (void)m; (void)s; return 0; /* TODO */
}
uint32_t alarm_at(const rtc_time_t *t) {                  /* 그 날짜(일)·시·분·초 모두 비교 (28일 이내 미래용) */
    (void)t; return 0;                   /* TODO */
}
uint32_t next_daily_epoch(uint32_t now, uint8_t h, uint8_t m, uint8_t s) {   /* now 보다 엄격히 뒤의 첫 h:m:s */
    (void)h; (void)m; (void)s; return now;   /* TODO */
}

/* ======================= 주어진 펌웨어 코드 (TODO 아님) ======================= */
/* 백업 도메인 클럭: DBP → LSEON → LSERDY 대기 → RTCSEL=LSE(한 번만) → RTCEN. 웜 부트면 LSE 는 이미 켜져 있다 */
static int rtc_clock_init(void) {
    REG_WRITE(PWR_CR, REG_READ(PWR_CR) | PWR_CR_DBP);
    uint32_t b = REG_READ(RCC_BDCR);
    if (!(b & BDCR_LSERDY)) {
        REG_WRITE(RCC_BDCR, b | BDCR_LSEON);
        for (int n = 0; !(REG_READ(RCC_BDCR) & BDCR_LSERDY); n++) {
            if (n >= RTC_POLL_MAX) return RTC_ETIMEOUT;
            BUSY_WAIT_STEP();
        }
    }
    b = REG_READ(RCC_BDCR);
    if ((b & BDCR_RTCSEL_Msk) == 0u) b |= BDCR_RTCSEL_LSE;
    REG_WRITE(RCC_BDCR, b | BDCR_LSEON | BDCR_RTCEN);
    return RTC_OK;
}
static int rtc_init(uint32_t rtcclk_hz) {
    uint32_t a, s; int32_t ppm;
    if (rtc_prescaler(rtcclk_hz, &a, &s, &ppm) < 0) return RTC_EINVAL;
    g_prer = (a << 16) | s;
    return rtc_clock_init();
}
static void rtc_alarm_irq_init(void) {    /* EXTI17 상승 에지 + 마스크 해제 + NVIC (RTC_Alarm_IRQn 41) */
    REG_WRITE(EXTI_RTSR, REG_READ(EXTI_RTSR) | EXTI_LINE_ALARM);
    REG_WRITE(EXTI_IMR, REG_READ(EXTI_IMR) | EXTI_LINE_ALARM);
    g_vec[RTC_ALARM_IRQn] = RTC_Alarm_IRQHandler;
    nvic_enable_irq(RTC_ALARM_IRQn);
}
/* 부트 정책: INITS(연도≠0) + BKP0R 매직이 있으면 VBAT 가 시간을 지켜 준 것 → 건드리지 않는다 */
static int app_boot(const rtc_time_t *fallback, bool *time_was_set) {
    int rc = rtc_init(32768u);
    *time_was_set = false;
    if (rc != RTC_OK) return rc;
    if (!((REG_READ(RTC_ISR) & ISR_INITS) && REG_READ(RTC_BKP0R) == BKP_MAGIC)) {
        rc = rtc_set(fallback);
        REG_WRITE(RTC_BKP0R, BKP_MAGIC);
        *time_was_set = true;
    } else {
        rc = rtc_resync_after_wake();     /* 리셋 직후에도 섀도가 최신인지 확인하고 읽는다 */
    }
    rtc_alarm_irq_init();
    return rc;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total, g_gfail;
static bool g_verbose;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } else if (++g_gfail <= 3) \
    printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static void group(const char *name, void (*fn)(void)) {
    int t0 = g_total; g_gfail = 0; fn();
    printf("%s %s (%d checks)\n", g_gfail ? "FAIL" : "PASS", name, g_total - t0);
}
static uint32_t g_rng = 2463534242u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

/* 테스트 쪽 참값: 느리지만 뻔한 달력 (TODO 와 독립) */
static bool ref_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static int ref_mdays(int y, int m) {
    static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return (m == 2 && ref_leap(y)) ? 29 : d[m - 1];
}
static int32_t ref_days(int y, int m, int d) {
    int32_t n = 0;
    for (int yy = 1970; yy < y; yy++) n += ref_leap(yy) ? 366 : 365;
    for (int mm = 1; mm < m; mm++) n += ref_mdays(y, mm);
    return n + d - 1;
}
static int ref_wday(int32_t z) { return (int)((z + 3) % 7) + 1; }
static int64_t ref_ms(const rtc_time_t *t) {
    return ((int64_t)ref_days(t->year, t->mon, t->day) * 86400 + t->hour * 3600 + t->min * 60 + t->sec) * 1000 + t->ms;
}
static rtc_time_t T(int y, int mo, int d, int h, int mi, int s) {
    rtc_time_t t = { (uint16_t)y, (uint8_t)mo, (uint8_t)d, 0, (uint8_t)h, (uint8_t)mi, (uint8_t)s, 0 };
    return t;
}
static bool same_ymdhms(const rtc_time_t *a, const rtc_time_t *b) {
    return a->year == b->year && a->mon == b->mon && a->day == b->day &&
           a->hour == b->hour && a->min == b->min && a->sec == b->sec;
}
static const char *WD[8] = { "???", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
static void fmt_time(char *buf, size_t n, const rtc_time_t *t) {
    snprintf(buf, n, "%04u-%02u-%02u %02u:%02u:%02u.%03u %s", t->year, t->mon, t->day, t->hour, t->min, t->sec,
             t->ms, WD[t->wday & 7u]);
}
static const char *reg_name(uint32_t a) {
    switch (a) {
    case PWR_CR: return "PWR_CR"; case RCC_BDCR: return "RCC_BDCR"; case RTC_TR: return "RTC_TR";
    case RTC_DR: return "RTC_DR"; case RTC_CR: return "RTC_CR"; case RTC_ISR: return "RTC_ISR";
    case RTC_PRER: return "RTC_PRER"; case RTC_ALRMAR: return "RTC_ALRMAR"; case RTC_WPR: return "RTC_WPR";
    case RTC_SSR: return "RTC_SSR"; case RTC_BKP0R: return "RTC_BKP0R"; case EXTI_PR: return "EXTI_PR";
    default: return "?";
    }
}
static int log_find(char op, uint32_t addr, uint32_t val, bool match_val, int from) {
    for (int i = from; i < g_nlog; i++)
        if (g_log[i].op == op && g_log[i].addr == addr && (!match_val || g_log[i].val == val)) return i;
    return -1;
}

/* 보드: mock 레지스터 표를 다시 만든다. backup_lost = VBAT 까지 끊긴 콜드 부트 */
static void board_setup(bool backup_lost) {
    mock_reset();
    mock_add_reg(PWR_CR, 0u, NULL, pwr_on_write, NULL);
    const uint32_t rtc_regs[] = { RCC_BDCR, RTC_TR, RTC_DR, RTC_CR, RTC_ISR, RTC_PRER, RTC_ALRMAR, RTC_WPR, RTC_SSR, RTC_BKP0R };
    for (size_t i = 0; i < sizeof rtc_regs / sizeof rtc_regs[0]; i++) mock_add_reg(rtc_regs[i], 0u, rtc_on_read, rtc_on_write, NULL);
    mock_add_reg(EXTI_IMR, 0u, NULL, exti_imr_write, NULL);
    mock_add_reg(EXTI_RTSR, 0u, NULL, NULL, NULL);
    mock_add_reg(EXTI_PR, 0u, NULL, exti_pr_write, NULL);
    memset(g_nvic_en, 0, sizeof g_nvic_en); memset(g_line, 0, sizeof g_line); memset(g_vec, 0, sizeof g_vec);
    g_isr_entries = 0; g_storm = false;
    g_prer = (127u << 16) | 255u; g_alarm_flag = false; g_alarm_count = 0;
    if (backup_lost) model_backup_domain_reset();
    R.apb_on = true; R.pwr_clock_off = false; R.wpr = 0;
}
static void bringup(const rtc_time_t *t) {
    board_setup(true);
    CHECK(rtc_init(32768u) == RTC_OK);
    CHECK(rtc_set(t) == RTC_OK);
}
/* 모델 뒷문: 살아 있는 캘린더 + 섀도를 t 로 맞추고 프리스케일러 위상을 정한다 (짝수 사이클에서 시작) */
static void model_poke(const rtc_time_t *t, uint32_t async_cnt, uint32_t ss) {
    int wd = ref_wday(ref_days(t->year, t->mon, t->day));
    R.tr = (m_bcd(t->hour) << 16) | (m_bcd(t->min) << 8) | m_bcd(t->sec);
    R.dr = (m_bcd(t->year - 2000u) << 16) | ((uint32_t)wd << 13) | (m_bcd(t->mon) << 8) | m_bcd(t->day);
    R.async_cnt = async_cnt; R.ss = ss; R.restart_cnt = 0;
    R.sh_tr = R.tr; R.sh_dr = R.dr; R.sh_ss = ss; R.lk_tr = R.lk_dr = false; R.rsf = true;
    if (R.cyc & 1u) R.cyc++;
}
static void inj_tick2(void) { rtc_clk(2); }       /* "이 두 읽기 사이에 RTCCLK 2 사이클이 흘렀다" */

/* ---------------- Part A ---------------- */
static void test_bcd(void) {
    int nvalid = 0;
    for (int b = 0; b < 256; b++) nvalid += bcd_ok((uint8_t)b) ? 1 : 0;
    CHECK(nvalid == 100);
    CHECK(!bcd_ok(0x1A) && !bcd_ok(0xA0) && bcd_ok(0x99) && bcd_ok(0x00));
    for (int v = 0; v < 100; v++) {
        uint8_t b = bin2bcd((uint8_t)v);
        CHECK(bcd_ok(b) && (b >> 4) == v / 10 && (b & 0xF) == v % 10 && bcd2bin(b) == v);
    }
    CHECK(bin2bcd(59) == 0x59 && bin2bcd(7) == 0x07 && bcd2bin(0x23) == 23);
}
static void test_pack(void) {
    rtc_time_t t = T(2026, 11, 11, 13, 45, 30), u;
    t.wday = 3;
    uint32_t tr, dr;
    rtc_pack(&t, &tr, &dr);
    CHECK(tr == 0x00134530u);
    CHECK(dr == 0x00267111u);
    rtc_unpack(0xFFu, tr, dr, 0xFFu, &u);
    CHECK(same_ymdhms(&t, &u) && u.wday == 3 && u.ms == 0);
    int bad = 0;
    for (uint32_t s = 0; s < 86400u; s++) {                          /* 하루의 모든 초 */
        rtc_time_t a = T(2026, 1, 1, (int)(s / 3600u), (int)(s / 60u % 60u), (int)(s % 60u)), b;
        rtc_pack(&a, &tr, &dr); rtc_unpack(0, tr, dr, 255, &b);
        if (!same_ymdhms(&a, &b) || (tr & ~0x003F7F7Fu)) bad++;
    }
    CHECK(bad == 0);
    bad = 0;
    for (int y = 2000; y <= 2099; y++)                               /* 2000–2099 모든 날짜 */
        for (int m = 1; m <= 12; m++)
            for (int d = 1; d <= ref_mdays(y, m); d++) {
                rtc_time_t a = T(y, m, d, 0, 0, 0), b;
                a.wday = (uint8_t)ref_wday(ref_days(y, m, d));
                rtc_pack(&a, &tr, &dr); rtc_unpack(0, tr, dr, 255, &b);
                if (!same_ymdhms(&a, &b) || b.wday != a.wday) bad++;
            }
    CHECK(bad == 0);
    for (uint32_t k = 0; k < 256u; k++) {                            /* 서브초: SS = 255−k */
        rtc_unpack(255u - k, 0, 0x2101u, 255u, &u);
        CHECK(u.ms == k * 1000u / 256u);
    }
}
static void brute_prescaler(uint32_t f, uint32_t *pa, uint32_t *ps) {   /* 4.2M 조합 전부 */
    uint64_t bn = 0, be = 0;
    for (uint32_t a = 128; a >= 1; a--)
        for (uint32_t s = 1; s <= 32768u; s++) {
            uint64_t n = (uint64_t)a * s, e = f > n ? f - n : n - f;
            if (bn == 0 || e * bn < be * n) { bn = n; be = e; *pa = a - 1; *ps = s - 1; }
        }
}
static void test_prescaler(void) {
    static const struct { uint32_t f, a, s; int exact; } named[] = {
        { 32768u, 127, 255, 0 }, { 32000u, 127, 249, 0 }, { 40000u, 124, 319, 0 }, { 1000000u, 124, 7999, 0 },
        { 4194304u, 127, 32767, 0 }, { 1u, 0, 0, 0 },
    };
    for (size_t i = 0; i < sizeof named / sizeof named[0]; i++) {
        uint32_t a = 9, s = 9; int32_t ppm = 9;
        int rc = rtc_prescaler(named[i].f, &a, &s, &ppm);
        CHECK(rc == named[i].exact && a == named[i].a && s == named[i].s && ppm == 0);
    }
    uint32_t a, s; int32_t ppm;
    CHECK(rtc_prescaler(0, &a, &s, &ppm) == RTC_EINVAL);
    static const uint32_t bf[] = { 32768u, 32000u, 37000u, 17000u, 47000u, 32771u, 65537u, 4194305u, 129u, 300007u };
    for (size_t i = 0; i < sizeof bf / sizeof bf[0]; i++) {
        uint32_t ba = 0, bs = 0;
        brute_prescaler(bf[i], &ba, &bs);
        int rc = rtc_prescaler(bf[i], &a, &s, &ppm);
        uint64_t n = (uint64_t)(a + 1) * (s + 1);
        CHECK(rc >= 0 && a == ba && s == bs && (rc == 0) == (n == bf[i]));
    }
    CHECK(rtc_prescaler(4194305u, &a, &s, &ppm) == 1 && ppm == 0);      /* 0.24 ppm → 반올림 0 */
    CHECK(rtc_prescaler(65537u, &a, &s, &ppm) == 1 && a == 98 && s == 661 && ppm == -15);
}
static void test_set_get(void) {
    board_setup(true);
    CHECK((REG_READ(RTC_ISR) & ISR_INITS) == 0u);                    /* 백업 도메인 리셋 직후: 연도 00 */
    CHECK(rtc_init(32768u) == RTC_OK && g_prer == 0x007F00FFu);
    rtc_time_t t = T(2026, 11, 11, 13, 45, 30), g;                   /* wday=0 (틀림) → 드라이버가 3(수) 계산 */
    g_nlog = 0;
    CHECK(rtc_set(&t) == RTC_OK);
    if (g_verbose) {
        printf("  rtc_set access log (%d entries):\n", g_nlog);
        for (int i = 0; i < g_nlog; i++) printf("    %c %-10s 0x%08X\n", g_log[i].op, reg_name(g_log[i].addr), g_log[i].val);
    }
    int k1 = log_find('W', RTC_WPR, WPR_KEY1, true, 0);
    CHECK(k1 >= 0 && k1 + 1 < g_nlog && g_log[k1 + 1].op == 'W' && g_log[k1 + 1].addr == RTC_WPR && g_log[k1 + 1].val == WPR_KEY2);
    int wtr = log_find('W', RTC_TR, 0, false, 0), winit = log_find('W', RTC_ISR, 0xFFFFFFFFu, true, 0);
    int rinitf = -1;
    for (int i = winit + 1; winit >= 0 && i < wtr && rinitf < 0; i++)
        if (g_log[i].op == 'R' && g_log[i].addr == RTC_ISR && (g_log[i].val & ISR_INITF)) rinitf = i;
    CHECK(winit >= 0 && rinitf > winit && wtr > rinitf);              /* INIT → INITF 확인 → TR */
    CHECK(wtr >= 0 && g_log[wtr].val == 0x00134530u);
    int wdr = log_find('W', RTC_DR, 0, false, 0);
    CHECK(wdr > wtr && g_log[wdr].val == 0x00267111u);                /* 요일 3 이 들어갔다 */
    CHECK(g_nlog > 0 && g_log[g_nlog - 1].op == 'W' && g_log[g_nlog - 1].addr == RTC_WPR && g_log[g_nlog - 1].val != WPR_KEY2);
    CHECK(R.ign_dbp == 0 && R.ign_wpr == 0 && R.ign_init == 0 && R.wpr != 2 && !(R.cr & CR_FMT));
    CHECK(R.prer == 0x007F00FFu && (REG_READ(RTC_ISR) & ISR_INITS));
    CHECK(rtc_get(&g) == RTC_OK && same_ymdhms(&g, &t) && g.wday == 3);
    sim_run_cycles(3u * 32768u + 8u);
    CHECK(rtc_get(&g) == RTC_OK && g.sec == 33 && g.min == 45 && g.ms < 50);
    CHECK(g_unexpected == 0);
}
static void test_subsecond(void) {
    rtc_time_t t = T(2026, 11, 11, 8, 0, 0), g;
    bringup(&t);
    model_poke(&t, 127u, 255u);                                      /* 막 1초 경계를 지난 위상 */
    int bad = 0;
    for (uint32_t k = 0; k < 256u; k++) {
        if (rtc_get(&g) != RTC_OK || g.sec != 0 || g.ms != k * 1000u / 256u) bad++;
        sim_run_cycles(128u);                                        /* ck_apre 한 틱 = 128 RTCCLK */
    }
    CHECK(bad == 0);
    CHECK(rtc_get(&g) == RTC_OK && g.sec == 1 && g.ms == 0);
}

/* ---------------- Part B ---------------- */
static void bad_set_no_initf_wait(const rtc_time_t *t) {   /* 버그: INIT=1 직후 바로 TR/DR 쓰기 */
    uint32_t tr, dr;
    rtc_pack(t, &tr, &dr);
    rtc_unlock();
    REG_WRITE(RTC_ISR, 0xFFFFFFFFu);
    REG_WRITE(RTC_TR, tr);
    REG_WRITE(RTC_DR, dr);
    REG_WRITE(RTC_ISR, ~ISR_INIT);
    rtc_lock();
}
static void test_protection(void) {
    rtc_time_t t0 = T(2026, 11, 11, 9, 0, 0), t1 = T(2030, 1, 2, 3, 4, 5), g;
    /* (1) PWR 클럭이 꺼져 DBP 가 안 켜짐 → 모든 RTC 쓰기 무시 → INITF 안 옴 → 상한 안에서 타임아웃 */
    bringup(&t0);
    REG_WRITE(PWR_CR, 0u); R.pwr_clock_off = true; R.ign_dbp = 0;
    CHECK(rtc_set(&t1) == RTC_ETIMEOUT && R.ign_dbp > 0);
    CHECK(rtc_get(&g) == RTC_OK && g.year == 2026);
    /* (2) WPR 키 순서 (모델 정의: 틀린 키 하나면 다시 잠김) */
    static const struct { int n; uint32_t k[3]; bool unlocked; } seq[] = {
        { 2, { 0xCA, 0x53 }, true }, { 2, { 0x53, 0xCA }, false }, { 3, { 0xCA, 0x00, 0x53 }, false },
        { 3, { 0xCA, 0x53, 0xFF }, false }, { 3, { 0xCA, 0xCA, 0x53 }, true },
    };
    for (size_t i = 0; i < sizeof seq / sizeof seq[0]; i++) {
        bringup(&t0);
        uint32_t ign = R.ign_wpr;
        for (int j = 0; j < seq[i].n; j++) REG_WRITE(RTC_WPR, seq[i].k[j]);
        REG_WRITE(RTC_CR, CR_ALRAIE);
        CHECK(((R.cr & CR_ALRAIE) != 0) == seq[i].unlocked && (R.ign_wpr > ign) == !seq[i].unlocked);
    }
    bringup(&t0);
    g_nlog = 0; rtc_unlock();
    CHECK(g_nlog == 2 && g_log[0].val == WPR_KEY1 && g_log[1].val == WPR_KEY2 && R.wpr == 2);
    rtc_lock();
    CHECK(R.wpr == 0);
    /* (3) INITF 를 안 기다림 → TR/DR 쓰기는 무시 (아직 초기화 모드 아님) */
    bringup(&t0);
    R.ign_init = 0;
    bad_set_no_initf_wait(&t1);
    CHECK(R.ign_init == 2);
    sim_run_cycles(8);
    CHECK(rtc_get(&g) == RTC_OK && g.year == 2026 && g.hour == 9);
    /* (4) RTCEN 을 안 켬 → RTCCLK 없음 → INITF 영원히 안 옴 → 타임아웃 (멈추지 않는다) */
    board_setup(true);
    REG_WRITE(PWR_CR, PWR_CR_DBP);
    REG_WRITE(RCC_BDCR, BDCR_LSEON | BDCR_RTCSEL_LSE);
    CHECK(rtc_set(&t1) == RTC_ETIMEOUT);
}
static void test_stale_after_set(void) {
    rtc_time_t t0 = T(2026, 11, 11, 9, 0, 0), t1 = T(2026, 11, 11, 17, 30, 0), g;
    bringup(&t0);
    sim_run_cycles(100);
    uint32_t tr, dr;                     /* 버그 재현: rtc_set 의 마지막 RSF 대기만 뺀 시퀀스 */
    t1.wday = 3; rtc_pack(&t1, &tr, &dr);
    rtc_unlock();
    CHECK(rtc_enter_init() == RTC_OK);
    REG_WRITE(RTC_TR, tr); REG_WRITE(RTC_DR, dr);
    rtc_exit_init(); rtc_lock();
    CHECK(rtc_get(&g) == RTC_OK && g.hour == 9);                     /* 섀도는 아직 옛 시각 */
    CHECK(rtc_resync_after_wake() == RTC_OK);
    CHECK(rtc_get(&g) == RTC_OK && g.hour == 17 && g.min == 30);
}
typedef int (*reader_fn)(rtc_time_t *);
static int bad_get_dr_first(rtc_time_t *t) {     /* 버그: DR → TR → SSR (섀도 모드) */
    uint32_t dr = REG_READ(RTC_DR), tr = REG_READ(RTC_TR), ss = REG_READ(RTC_SSR);
    rtc_unpack(ss, tr, dr, g_prer & 0x7FFFu, t);
    return RTC_OK;
}
/* 2026-12-31 23:59:59.996, 다음 RTCCLK 에 해가 바뀌는 위상. k 번째 접근 직후 RTCCLK 2 사이클 주입 */
static uint32_t tear_sweep(const char *name, reader_fn rd, bool bypass, uint32_t *naccess) {
    rtc_time_t t = T(2026, 12, 31, 23, 59, 59), g;
    bringup(&t);
    t.ms = 996;
    int64_t before = ref_ms(&t), after = before + 4;                  /* 2027-01-01 00:00:00.000 */
    model_poke(&t, 0u, 0u);
    if (bypass) R.cr |= CR_BYPSHAD;
    uint32_t a0 = g_nacc;
    rd(&g);
    uint32_t n = g_nacc - a0, mask = 0;
    char line[256]; int len = snprintf(line, sizeof line, "  %-34s", name);
    for (uint32_t k = 0; k <= n; k++) {
        rtc_time_t t0 = T(2026, 12, 31, 23, 59, 59);
        bringup(&t0);
        model_poke(&t0, 0u, 0u);
        if (bypass) R.cr |= CR_BYPSHAD;
        mock_inject_at(k, inj_tick2);
        int rc = rd(&g);
        int64_t got = ref_ms(&g);
        bool ok = rc == RTC_OK && (got == before || got == after);
        if (!ok) mask |= 1u << k;
        if (len < 200) len += snprintf(line + len, sizeof line - (size_t)len, " k%u:%s%lld", k,
                                       ok ? "" : "*", (long long)(got - before));
    }
    printf("%s\n", line);
    *naccess = n;
    return mask;
}
static void test_tear(void) {
    uint32_t n, m;
    printf("  tear sweep: 값 = 결과 − 23:59:59.996 (ms), * = 찢어짐\n");
    m = tear_sweep("BYPSHAD=1, SSR→TR→DR (rtc_get)", rtc_get, true, &n);
    CHECK(n == 3 && m == 0x6u);                                       /* k=1 (+1000), k=2 (+1일) */
    m = tear_sweep("BYPSHAD=0, DR→TR→SSR (bad order)", bad_get_dr_first, false, &n);
    CHECK(n == 3 && m == 0x6u);                                       /* k=1 (−1일), k=2 (−996) */
    m = tear_sweep("BYPSHAD=0, SSR→TR→DR (rtc_get)", rtc_get, false, &n);
    CHECK(n == 3 && m == 0u);
    m = tear_sweep("BYPSHAD=1, read twice (bypass)", rtc_get_bypass, true, &n);
    CHECK(n == 6 && m == 0u);
}
static int bad_get_time_only(rtc_time_t *t) {    /* 버그: 시간만 읽고 DR 은 안 읽음 (HAL GetTime 만 호출) */
    uint32_t ss = REG_READ(RTC_SSR), tr = REG_READ(RTC_TR);
    rtc_unpack(ss, tr, 0x2101u, g_prer & 0x7FFFu, t);
    return RTC_OK;
}
static void test_time_only_lock(void) {
    rtc_time_t t = T(2026, 12, 31, 23, 59, 58), g;
    bringup(&t);
    model_poke(&t, 127u, 255u);
    bad_get_time_only(&g);
    CHECK(g.hour == 23 && g.sec == 58 && R.lk_tr && R.lk_dr);        /* TR·DR 이 잠긴 채 남았다 */
    sim_run_cycles(3u * 32768u);
    bad_get_time_only(&g);
    CHECK(g.hour == 23 && g.sec == 58);                              /* 3 s 지났는데 시계가 멈춰 보인다 */
    CHECK(rtc_get(&g) == RTC_OK && g.day == 31 && g.sec == 58);      /* 첫 정상 읽기도 묵은 값 → 잠금 해제 */
    sim_run_cycles(2);
    CHECK(rtc_get(&g) == RTC_OK && g.year == 2027 && g.mon == 1 && g.day == 1 && g.sec == 1 && g.wday == 5);
}
static void test_stop_wake_stale(void) {
    rtc_time_t t = T(2026, 11, 11, 22, 0, 0), g;
    bringup(&t);
    model_poke(&t, 127u, 255u);
    CHECK(sim_stop_until_irq(10) == 10);                             /* 깨울 IRQ 없음 → 10 s 꽉 채움 */
    CHECK(rtc_get(&g) == RTC_OK && g.hour == 22 && g.sec == 0);      /* 섀도: 자기 전 값 (10 s 뒤처짐) */
    CHECK(rtc_resync_after_wake() == RTC_OK);
    CHECK(rtc_get(&g) == RTC_OK && g.sec == 10);
}
static void test_alarm_write(void) {
    rtc_time_t t = T(2026, 11, 11, 22, 0, 0);
    bringup(&t);
    CHECK(rtc_alarm_write(alarm_daily(7, 30, 0)) == RTC_OK && R.alrmar == 0x80073000u);
    CHECK((R.cr & (CR_ALRAE | CR_ALRAIE)) == (CR_ALRAE | CR_ALRAIE));
    rtc_unlock(); R.ign_alrm = 0;
    REG_WRITE(RTC_ALRMAR, alarm_daily(8, 0, 0));                     /* 버그: ALRAE=1 인 채로 쓰기 */
    rtc_lock();
    CHECK(R.ign_alrm == 1 && R.alrmar == 0x80073000u);
    g_nlog = 0;
    CHECK(rtc_alarm_write(alarm_daily(8, 0, 0)) == RTC_OK && R.alrmar == 0x80080000u);
    int wcr = log_find('W', RTC_CR, 0, false, 0), wal = log_find('W', RTC_ALRMAR, 0, false, 0), raw = -1;
    for (int i = wcr + 1; wcr >= 0 && i < wal && raw < 0; i++)
        if (g_log[i].op == 'R' && g_log[i].addr == RTC_ISR && (g_log[i].val & ISR_ALRAWF)) raw = i;
    CHECK(wcr >= 0 && !(g_log[wcr].val & CR_ALRAE) && raw > wcr && wal > raw);
}
/* 알람 ISR 함정 3종 */
static void bad_isr_overwrites_init(void) { REG_WRITE(RTC_ISR, ~ISR_ALRAF); REG_WRITE(EXTI_PR, EXTI_LINE_ALARM); g_alarm_count++; }
static void bad_isr_no_exti_clear(void) { REG_WRITE(RTC_ISR, ~(ISR_ALRAF | ISR_INIT)); g_alarm_count++; }
static void bad_isr_no_alraf_clear(void) { REG_WRITE(EXTI_PR, EXTI_LINE_ALARM); g_alarm_flag = true; g_alarm_count++; }
static void alarm_scenario(isr_fn h, bool leave_unlocked) {
    rtc_time_t t = T(2026, 11, 11, 7, 29, 58);
    bringup(&t);
    model_poke(&t, 127u, 255u);
    rtc_alarm_irq_init();
    g_vec[RTC_ALARM_IRQn] = h;
    CHECK(rtc_alarm_write(alarm_daily(7, 30, 0)) == RTC_OK);
    if (leave_unlocked) rtc_unlock();                                /* "편하려고" WPR 을 풀어 둔 펌웨어 */
}
static void test_isr_traps(void) {
    /* (a) ISR = ~ALRAF : WPR 이 풀려 있으면 INIT 비트에 1 을 써서 초기화 모드 → 캘린더 정지 */
    alarm_scenario(bad_isr_overwrites_init, true);
    sim_run_cycles(3u * 32768u);
    uint32_t s0 = R.seconds;
    sim_run_cycles(5u * 32768u);
    CHECK(g_alarm_count == 1 && R.init && R.seconds == s0);
    alarm_scenario(RTC_Alarm_IRQHandler, true);
    sim_run_cycles(3u * 32768u);
    s0 = R.seconds;
    sim_run_cycles(5u * 32768u);
    CHECK(g_alarm_count == 1 && !R.init && R.seconds == s0 + 5u);
    /* (b) EXTI_PR 을 안 지움 → 라인이 계속 high → 같은 ISR 무한 재진입 */
    alarm_scenario(bad_isr_no_exti_clear, false);
    sim_run_cycles(3u * 32768u);
    CHECK(g_storm && g_isr_entries >= DISPATCH_CAP);
    alarm_scenario(RTC_Alarm_IRQHandler, false);
    sim_run_cycles(3u * 32768u);
    CHECK(!g_storm && g_isr_entries == 1 && !R.alraf);
    /* (c) ALRAF 를 안 지움 → 다음 날 매치 때 0→1 에지가 없음 → Stop 에서 영영 못 깸 */
    alarm_scenario(bad_isr_no_alraf_clear, false);
    CHECK(sim_stop_until_irq(2u * 86400u) == 2u);                    /* 07:29:58 → 07:30:00 깸 */
    CHECK(g_alarm_count == 1 && R.alraf);
    CHECK(sim_stop_until_irq(2u * 86400u) == 2u * 86400u);           /* 이틀 꽉: 못 깸 */
    CHECK(g_alarm_count == 1 && R.alarms == 3);                      /* 비교기는 매일 맞았다 */
    alarm_scenario(RTC_Alarm_IRQHandler, false);
    CHECK(sim_stop_until_irq(2u * 86400u) == 2u);
    CHECK(sim_stop_until_irq(2u * 86400u) == 86400u && g_alarm_count == 2);
}

/* ---------------- Part C ---------------- */
static void test_civil(void) {
    int y = 1970, m = 1, d = 1, wd = 4, bad_df = 0, bad_cf = 0, bad_wd = 0;
    int32_t last = ref_days(2100, 12, 31);
    for (int32_t z = 0; z <= last; z++) {                            /* 1970-01-01 … 2100-12-31 매일 */
        int32_t yy; uint32_t mm, dd;
        if (days_from_civil(y, (uint32_t)m, (uint32_t)d) != z) bad_df++;
        civil_from_days(z, &yy, &mm, &dd);
        if (yy != y || mm != (uint32_t)m || dd != (uint32_t)d) bad_cf++;
        if (weekday_from_days(z) != wd) bad_wd++;
        wd = wd % 7 + 1;
        if (++d > ref_mdays(y, m)) { d = 1; if (++m > 12) { m = 1; y++; } }
    }
    CHECK(bad_df == 0 && bad_cf == 0 && bad_wd == 0);
    CHECK(days_from_civil(2000, 1, 1) == 10957 && days_from_civil(2026, 11, 11) == 20768);
    int32_t yy; uint32_t mm, dd;
    civil_from_days(days_from_civil(2000, 2, 28) + 1, &yy, &mm, &dd);
    CHECK(yy == 2000 && mm == 2 && dd == 29);                        /* 2000: 400 의 배수 → 윤년 */
    civil_from_days(days_from_civil(2100, 2, 28) + 1, &yy, &mm, &dd);
    CHECK(yy == 2100 && mm == 3 && dd == 1);                         /* 2100: 100 의 배수 → 평년 */
    CHECK(days_from_civil(2100, 3, 1) == 47541 && weekday_from_days(20768) == 3);
}
static void test_epoch(void) {
    rtc_time_t t, u;
    uint32_t e;
    t = T(2000, 1, 1, 0, 0, 0);  CHECK(rtc_to_epoch(&t, &e) == RTC_OK && e == RTC_EPOCH_MIN);
    t = T(2099, 12, 31, 23, 59, 59); CHECK(rtc_to_epoch(&t, &e) == RTC_OK && e == RTC_EPOCH_MAX);
    CHECK(epoch_to_rtc(RTC_EPOCH_MIN - 1u, &u) == RTC_EINVAL && epoch_to_rtc(RTC_EPOCH_MAX + 1u, &u) == RTC_EINVAL);
    t = T(2026, 2, 29, 0, 0, 0); CHECK(rtc_to_epoch(&t, &e) == RTC_EINVAL);
    CHECK(epoch_to_rtc(1794355200u, &u) == RTC_OK && u.year == 2026 && u.mon == 11 && u.day == 11 && u.wday == 3 && u.hour == 0);
    int bad = 0;
    for (int i = 0; i < 5000; i++) {
        uint32_t x = RTC_EPOCH_MIN + xs32() % (RTC_EPOCH_MAX - RTC_EPOCH_MIN + 1u), back = 0;
        if (epoch_to_rtc(x, &u) != RTC_OK || rtc_to_epoch(&u, &back) != RTC_OK || back != x) bad++;
        else if (ref_ms(&u) != (int64_t)x * 1000 || u.wday != ref_wday(ref_days(u.year, u.mon, u.day))) bad++;
    }
    CHECK(bad == 0);
}
static void test_hw_calendar_vs_epoch(void) {   /* RTC 하드웨어 달력(모델)이 매일 자정을 epoch 산술과 똑같이 넘기나 */
    rtc_time_t t = T(2000, 1, 1, 0, 0, 0), g, w;
    bringup(&t);
    int bad = 0, days = 0;
    for (uint32_t e = RTC_EPOCH_MIN + 86399u; e < RTC_EPOCH_MAX; e += 86400u, days++) {
        epoch_to_rtc(e, &w);                                         /* 그날 23:59:59 */
        model_poke(&w, 0u, 0u);
        rtc_ff_second();
        rtc_unpack(R.ss, R.tr, R.dr, 255u, &g);
        epoch_to_rtc(e + 1u, &w);
        if (!same_ymdhms(&g, &w) || g.wday != w.wday) bad++;
    }
    CHECK(bad == 0 && days == 36524);
    epoch_to_rtc(RTC_EPOCH_MAX, &w);                                 /* 2099-12-31 23:59:59 → ? */
    model_poke(&w, 0u, 0u);
    rtc_ff_second();
    rtc_unpack(R.ss, R.tr, R.dr, 255u, &g);
    CHECK(g.year == 2000 && g.mon == 1 && g.day == 1 && g.wday == 5);   /* 2자리 연도: 00 → "2000" (실제 2100 금) */
}
static void test_alarm_masks(void) {
    rtc_time_t t = T(2026, 11, 11, 12, 0, 0);
    rtc_time_t a = T(2028, 2, 29, 0, 10, 0);
    CHECK(alarm_daily(7, 30, 0) == 0x80073000u && alarm_at(&a) == 0x29001000u);
    static const struct { uint32_t alrm, secs, expect; } cases[] = {
        { ALRM_MSK4 | ALRM_MSK3 | ALRM_MSK2 | ALRM_MSK1, 3600u, 3600u },   /* 전부 무시 → 매초 */
        { ALRM_MSK4 | ALRM_MSK3 | ALRM_MSK2 | 0x30u,     3600u, 60u },     /* 매분 :30 초 */
        { ALRM_MSK4 | ALRM_MSK3 | 0x1500u,               86400u, 24u },    /* 매시 15:00 */
        { ALRM_MSK4 | 0x073000u,                         2u * 86400u, 2u },/* 매일 07:30:00 */
        { ALRM_WDSEL | (1u << 24) | 0x090000u,           14u * 86400u, 2u },/* 매주 월 09:00:00 */
        { (0x13u << 24) | 0x000000u,                     20u * 86400u, 1u },/* 13일 00:00:00 */
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        bringup(&t);
        model_poke(&t, 127u, 255u);
        CHECK(rtc_alarm_write(cases[i].alrm) == RTC_OK);
        uint32_t a0 = R.alarms;
        for (uint32_t s = 0; s < cases[i].secs; s++) rtc_ff_second();
        CHECK(R.alarms - a0 == cases[i].expect);
    }
}
static uint32_t now_epoch(void) {
    rtc_time_t g; uint32_t e = 0;
    rtc_get(&g); rtc_to_epoch(&g, &e);
    return e;
}
static void log_wake(const char *tag, int n, uint32_t slept) {
    rtc_time_t g; char buf[48];
    rtc_get(&g); fmt_time(buf, sizeof buf, &g);
    printf("  [%s] %s wake #%d  slept %6u s  isr=%u alarms=%u\n", buf, tag, n, slept, g_isr_entries, (unsigned)g_alarm_count);
}
static void test_daily_node(void) {
    rtc_time_t fb = T(2026, 11, 11, 22, 15, 0);
    bool set;
    board_setup(true);
    CHECK(app_boot(&fb, &set) == RTC_OK && set);
    CHECK(rtc_alarm_write(alarm_daily(7, 30, 0)) == RTC_OK);
    for (int n = 1; n <= 3; n++) {
        uint32_t now = now_epoch(), want = next_daily_epoch(now, 7, 30, 0);
        g_alarm_flag = false;
        uint32_t slept = sim_stop_until_irq(3u * 86400u);
        CHECK(g_alarm_flag);
        CHECK(rtc_resync_after_wake() == RTC_OK);
        CHECK(now_epoch() == want);
        log_wake("daily ", n, slept);
    }
    CHECK(g_isr_entries == 3 && !g_storm && g_alarm_count == 3);
}
static void test_periodic_node(void) {
    rtc_time_t fb = T(2028, 2, 29, 23, 20, 0), w, g;
    bool set;
    board_setup(true);
    CHECK(app_boot(&fb, &set) == RTC_OK && set);
    uint32_t next = now_epoch();
    int crossed = 0;
    for (int n = 1; n <= 6; n++) {                                   /* 15분마다: 윤일 자정 → 3월 1일 */
        next += 900u;
        epoch_to_rtc(next, &w);
        CHECK(rtc_alarm_write(alarm_at(&w)) == RTC_OK);
        uint32_t slept = sim_stop_until_irq(86400u);
        CHECK(rtc_resync_after_wake() == RTC_OK && now_epoch() == next);
        rtc_get(&g);
        crossed += (g.mon == 3) ? 1 : 0;
        log_wake("15min ", n, slept);
    }
    CHECK(crossed == 4 && g_alarm_count == 6 && !g_storm);
    /* 웜 리셋: MCU 만 리셋 (DBP=0, EXTI/NVIC 리셋), 백업 도메인(RTC·BKP0R)은 그대로 */
    uint32_t before = now_epoch();
    board_setup(false);
    sim_run_cycles(5u * 32768u);
    CHECK(app_boot(&fb, &set) == RTC_OK && !set && now_epoch() == before + 5u);
    rtc_get(&g); char buf[48]; fmt_time(buf, sizeof buf, &g);
    printf("  [%s] warm reset: INITS=%u, BKP0R=0x%04X → time kept\n", buf,
           (REG_READ(RTC_ISR) & ISR_INITS) ? 1u : 0u, (unsigned)REG_READ(RTC_BKP0R));
    /* 콜드: VBAT 까지 끊김 → INITS=0 → 기본 시각으로 다시 설정 */
    board_setup(true);
    CHECK(app_boot(&fb, &set) == RTC_OK && set && now_epoch() == 1835479200u);
    rtc_get(&g); fmt_time(buf, sizeof buf, &g);
    printf("  [%s] cold boot : backup domain lost → time set to fallback\n", buf);
}

int main(int argc, char **argv) {
    g_verbose = argc > 1 && strcmp(argv[1], "-v") == 0;
    printf("== Part A: BCD, packing, prescaler, set/get ==\n");
    group("A1 BCD helpers (256 bytes, 0..99 round trip)", test_bcd);
    group("A2 TR/DR pack/unpack (86400 s, 36525 days, 256 SSR)", test_pack);
    group("A3 prescaler vs brute force", test_prescaler);
    group("A4 rtc_set sequence + rtc_get", test_set_get);
    group("A5 subsecond (256 ck_apre ticks)", test_subsecond);
    printf("== Part B: faults ==\n");
    group("B1 DBP / WPR / INITF / RTCEN", test_protection);
    group("B2 read right after set needs RSF", test_stale_after_set);
    group("B3 midnight tear sweep", test_tear);
    group("B4 time-only read leaves DR locked", test_time_only_lock);
    group("B5 stale shadow after Stop wake", test_stop_wake_stale);
    group("B6 ALRMAR write needs ALRAE=0 + ALRAWF", test_alarm_write);
    group("B7 alarm ISR flag-clear traps", test_isr_traps);
    printf("== Part C: epoch, alarms, sensor node ==\n");
    group("C1 days_from_civil vs day counter 1970-2100", test_civil);
    group("C2 epoch <-> rtc (5000 random + bounds)", test_epoch);
    group("C3 HW calendar rollover vs epoch (36524 days)", test_hw_calendar_vs_epoch);
    group("C4 alarm masks", test_alarm_masks);
    group("C5 daily 07:30 wake from Stop", test_daily_node);
    group("C6 15-min wake across leap day + warm/cold boot", test_periodic_node);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
