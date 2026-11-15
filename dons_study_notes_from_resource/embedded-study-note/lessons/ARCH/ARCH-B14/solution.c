/* ARCH-B14 리셋 종류와 리셋 원인 디코딩
 *
 * 하드웨어 모델 2종 (비트 위치는 "e.g." — 실제 칩은 레퍼런스 매뉴얼 확인)
 *   - ST 스타일  : RCC_CSR 처럼 플래그는 읽기 전용, RMVF 비트에 1을 쓰면 "전부" 클리어
 *   - nRF 스타일 : POWER->RESETREAS 처럼 비트마다 write-1-to-clear, 파워 리셋이면 0
 * → 벤더 중립 원인 마스크(RC_*)로 디코딩 → 우선순위로 주 원인 결정
 * → 부팅 코드: 읽기 · 로그 · 클리어 · 결정(cold / warm / safe mode) → 시나리오 시퀀스 테스트
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 리셋 이벤트 (하드웨어 쪽 입력) ---------------- */
typedef enum {
    EV_POR, EV_BOR, EV_PIN, EV_IWDG, EV_WWDG, EV_SW, EV_LOCKUP, EV_LPWR, EV_WAKEOFF, EV__N
} event_t;
static const char *const EV_NAME[EV__N] = {
    "POR", "BOR", "PIN", "IWDG", "WWDG", "SW", "LOCKUP", "LPWR", "WAKEOFF"
};

/* ---------------- 벤더 1: ST 스타일 (RCC_CSR 유사) ---------------- */
#define ST_LSION    (1u << 0)    /* 설정 비트 (RW). 플래그와 같은 레지스터에 산다            */
#define ST_RMVF     (1u << 24)   /* W: 1을 쓰면 모든 리셋 플래그 클리어. 읽으면 항상 0        */
#define ST_BORRSTF  (1u << 25)
#define ST_PINRSTF  (1u << 26)
#define ST_PORRSTF  (1u << 27)
#define ST_SFTRSTF  (1u << 28)
#define ST_IWDGRSTF (1u << 29)
#define ST_WWDGRSTF (1u << 30)
#define ST_LPWRRSTF (1u << 31)
#define ST_FLAGS    0xFE000000u  /* bit31..25: 리셋 플래그 (읽기 전용, sticky)               */

typedef struct {
    uint32_t csr;
    bool pin_echo;   /* 내부 리셋이 NRST 핀으로 펄스를 내보내 PINRSTF 도 같이 켜지는 칩이면 true */
} st_rcc_t;

/* ---------------- 벤더 2: nRF 스타일 (RESETREAS 유사) ---------------- */
#define NRF_RESETPIN (1u << 0)
#define NRF_DOG      (1u << 1)
#define NRF_SREQ     (1u << 2)   /* 소프트웨어 리셋 (SYSRESETREQ) */
#define NRF_LOCKUP   (1u << 3)
#define NRF_OFF      (1u << 16)  /* System OFF 에서 GPIO 로 깨어남 (깨어나기 = 리셋) */
#define NRF_MASK     (NRF_RESETPIN | NRF_DOG | NRF_SREQ | NRF_LOCKUP | NRF_OFF)

typedef struct { uint32_t resetreas; } nrf_power_t;

/* ---------------- 벤더 중립 원인 마스크 / 주 원인 ---------------- */
/* 비트 번호 = 우선순위 (0 이 가장 높다). 주 원인 = "가장 낮은 1 비트" (ARCH-B05 우선순위 인코더) */
enum {
    RC_POR = 1u << 0, RC_BOR = 1u << 1, RC_POWER = 1u << 2, /* POWER: POR 또는 BOR, 구분 불가 (nRF) */
    RC_IWDG = 1u << 3, RC_WWDG = 1u << 4, RC_LOCKUP = 1u << 5, RC_SW = 1u << 6,
    RC_LPWR = 1u << 7, RC_WAKEOFF = 1u << 8, RC_PIN = 1u << 9, RC_ALL = 0x3FFu
};
typedef enum {
    CAUSE_NONE, CAUSE_POR, CAUSE_BOR, CAUSE_POWER, CAUSE_IWDG, CAUSE_WWDG, CAUSE_LOCKUP,
    CAUSE_SW, CAUSE_LPWR, CAUSE_WAKEOFF, CAUSE_PIN, CAUSE__N
} cause_t;   /* CAUSE_x == (RC 비트 번호 + 1) */
static const char *const CAUSE_NAME[CAUSE__N] = {
    "NONE", "POR", "BOR", "POWER", "IWDG", "WWDG", "LOCKUP", "SW", "LPWR", "WAKEOFF", "PIN"
};

/* ================= Part A: 리셋 원인 레지스터 모델 (하드웨어 쪽) ================= */

/* ST: 리셋 이벤트 하나를 적용. 지원 안 하는 이벤트면 false.
 * - 파워 리셋(POR, BOR)은 플래그 영역까지 초기화한 뒤 자기 플래그를 켠다. POR 은 POR|BOR 둘 다.
 * - 시스템 리셋(PIN, IWDG, WWDG, SW, LPWR)은 기존 플래그를 보존(sticky)하고 자기 플래그를 OR.
 * - 어떤 리셋이든 설정 비트(LSION)는 0 으로. pin_echo 면 PIN 이외 이벤트에도 PINRSTF. */
static bool st_hw_reset(st_rcc_t *r, event_t ev) {
    uint32_t f;
    switch (ev) {
    case EV_POR:  f = ST_PORRSTF | ST_BORRSTF; break;
    case EV_BOR:  f = ST_BORRSTF;  break;
    case EV_PIN:  f = ST_PINRSTF;  break;
    case EV_IWDG: f = ST_IWDGRSTF; break;
    case EV_WWDG: f = ST_WWDGRSTF; break;
    case EV_SW:   f = ST_SFTRSTF;  break;
    case EV_LPWR: f = ST_LPWRRSTF; break;
    default: return false;
    }
    if (r->pin_echo) f |= ST_PINRSTF;
    uint32_t keep = (ev == EV_POR || ev == EV_BOR) ? 0u : (r->csr & ST_FLAGS);
    r->csr = keep | f;
    return true;
}

/* ST: 소프트웨어가 CSR 에 v 를 쓸 때. 플래그는 읽기 전용(쓴 값 무시), LSION 만 RW,
 * RMVF=1 이면 플래그 전부 클리어, RMVF 자체는 저장되지 않는다. */
static void st_csr_write(st_rcc_t *r, uint32_t v) {
    uint32_t flags = (v & ST_RMVF) ? 0u : (r->csr & ST_FLAGS);
    r->csr = flags | (v & ST_LSION);
}

/* nRF: 파워 리셋(POR, BOR)이면 0 (= 온칩 리셋 생성기). 나머지는 자기 비트를 OR (sticky).
 * WWDG / LPWR 는 없음 → false. */
static bool nrf_hw_reset(nrf_power_t *p, event_t ev) {
    switch (ev) {
    case EV_POR: case EV_BOR: p->resetreas = 0; return true;
    case EV_PIN:     p->resetreas |= NRF_RESETPIN; return true;
    case EV_IWDG:    p->resetreas |= NRF_DOG;      return true;
    case EV_SW:      p->resetreas |= NRF_SREQ;     return true;
    case EV_LOCKUP:  p->resetreas |= NRF_LOCKUP;   return true;
    case EV_WAKEOFF: p->resetreas |= NRF_OFF;      return true;
    default: return false;
    }
}

/* nRF: write-1-to-clear. 1 을 쓴 (구현된) 비트만 0 이 된다. */
static void nrf_resetreas_write(nrf_power_t *p, uint32_t v) {
    p->resetreas &= ~(v & NRF_MASK);
}

/* ================= Part B: 디코딩과 우선순위 ================= */

/* ST CSR → 공통 마스크 */
static uint32_t st_to_rc(uint32_t csr) {
    uint32_t rc = 0;
    if (csr & ST_PORRSTF)  rc |= RC_POR;
    if (csr & ST_BORRSTF)  rc |= RC_BOR;
    if (csr & ST_PINRSTF)  rc |= RC_PIN;
    if (csr & ST_IWDGRSTF) rc |= RC_IWDG;
    if (csr & ST_WWDGRSTF) rc |= RC_WWDG;
    if (csr & ST_SFTRSTF)  rc |= RC_SW;
    if (csr & ST_LPWRRSTF) rc |= RC_LPWR;
    return rc;
}

/* nRF RESETREAS → 공통 마스크. 알려진 비트가 하나도 없으면 RC_POWER (POR/BOR 구분 불가). */
static uint32_t nrf_to_rc(uint32_t rr) {
    uint32_t rc = 0;
    if (rr & NRF_RESETPIN) rc |= RC_PIN;
    if (rr & NRF_DOG)      rc |= RC_IWDG;
    if (rr & NRF_SREQ)     rc |= RC_SW;
    if (rr & NRF_LOCKUP)   rc |= RC_LOCKUP;
    if (rr & NRF_OFF)      rc |= RC_WAKEOFF;
    return rc ? rc : RC_POWER;
}

/* 주 원인 = 가장 낮은 번호의 1 비트 (+1). rc == 0 → CAUSE_NONE. 내장 함수 금지. */
static cause_t primary_cause(uint32_t rc) {
    rc &= RC_ALL;
    for (unsigned i = 0; i < 10; i++)
        if (rc & (1u << i)) return (cause_t)(i + 1);
    return CAUSE_NONE;
}

/* ================= Part C: 부팅 코드 ================= */
#define LOG_N       8
#define RET_MAGIC   0x52535431u  /* "RST1" */
#define SAFE_STREAK 3            /* 연속 비정상 리셋 3 번이면 safe mode */

typedef struct { uint32_t seq; uint32_t raw; uint32_t cause; } log_ent_t;
typedef struct {                 /* .noinit RAM: 웜 리셋에선 살아남고, 파워 리셋 후엔 쓰레기 */
    uint32_t magic;
    uint32_t boot_seq;           /* 마지막 콜드 부트 이후 몇 번째 부팅인가 (콜드 부트 = 1)  */
    uint32_t streak;             /* 연속 비정상(IWDG/WWDG/LOCKUP) 리셋 수                 */
    uint32_t head;               /* 다음에 쓸 log 칸                                       */
    log_ent_t log[LOG_N];
    uint32_t check;              /* ret_check() */
} retained_t;

typedef enum { VEND_ST, VEND_NRF } vendor_t;
typedef struct {
    vendor_t v;
    st_rcc_t st;
    nrf_power_t nrf;
    retained_t ret;
    uint32_t rng;                /* 파워업 SRAM 쓰레기 값을 만드는 PRNG 상태 */
} chip_t;

typedef enum { BOOT_COLD, BOOT_WARM, BOOT_SAFE } boot_mode_t;
static const char *const MODE_NAME[] = { "COLD", "WARM", "SAFE" };
typedef struct {
    uint32_t raw;                /* 읽은 레지스터 값 그대로 */
    uint32_t rc;                 /* 공통 마스크 */
    cause_t cause;               /* 주 원인 */
    boot_mode_t mode;
    uint32_t streak, seq;
    bool ret_valid;              /* 들어올 때 retained 영역이 유효했나 */
} boot_info_t;

static uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x;
}

/* retained 영역 검사값 (실제로는 CRC32 를 쓴다 — 여기선 짧게) */
static uint32_t ret_check(const retained_t *r) {
    uint32_t h = 0xA5A5A5A5u ^ r->magic;
    h = h * 31u + r->boot_seq; h = h * 31u + r->streak; h = h * 31u + r->head;
    for (int i = 0; i < LOG_N; i++)
        h = ((h * 31u + r->log[i].seq) * 31u + r->log[i].raw) * 31u + r->log[i].cause;
    return h;
}

/* 하드웨어 시뮬레이션: 리셋 이벤트 적용. 파워 리셋이면 retained RAM 이 쓰레기가 된다. */
static bool chip_reset(chip_t *c, event_t ev) {
    bool ok = (c->v == VEND_ST) ? st_hw_reset(&c->st, ev) : nrf_hw_reset(&c->nrf, ev);
    if (ok && (ev == EV_POR || ev == EV_BOR)) {
        uint32_t *w = (uint32_t *)&c->ret;
        for (size_t i = 0; i < sizeof c->ret / sizeof *w; i++) w[i] = xorshift32(&c->rng);
    }
    return ok;
}

/* 펌웨어가 보는 레지스터 읽기 */
static uint32_t rsr_read(const chip_t *c) { return c->v == VEND_ST ? c->st.csr : c->nrf.resetreas; }

static bool is_power(cause_t k)    { return k == CAUSE_POR || k == CAUSE_BOR || k == CAUSE_POWER; }
static bool is_abnormal(cause_t k) { return k == CAUSE_IWDG || k == CAUSE_WWDG || k == CAUSE_LOCKUP; }

/* 부팅 시 리셋 처리. 순서: ① 읽기 ② 디코드 ③ retained 검사(파워 리셋/무효면 초기화)
 * ④ boot_seq++, streak 갱신 (비정상 +1, 파워·PIN 0, 나머지 유지) ⑤ log 기록 + check 갱신
 * ⑥ 레지스터 클리어 (ST: LSION 보존하며 RMVF, nRF: 읽은 비트만 W1C) ⑦ 결정 */
static boot_info_t boot_reset_handler(chip_t *c) {
    boot_info_t b;
    memset(&b, 0, sizeof b);
    retained_t *r = &c->ret;
    b.raw = rsr_read(c);
    b.rc = (c->v == VEND_ST) ? st_to_rc(b.raw) : nrf_to_rc(b.raw);
    b.cause = primary_cause(b.rc);
    b.ret_valid = r->magic == RET_MAGIC && r->head < LOG_N && r->check == ret_check(r);
    bool cold = is_power(b.cause) || !b.ret_valid;
    if (cold) { memset(r, 0, sizeof *r); r->magic = RET_MAGIC; }
    r->boot_seq++;
    if (is_abnormal(b.cause)) r->streak++;
    else if (is_power(b.cause) || b.cause == CAUSE_PIN) r->streak = 0;
    r->log[r->head] = (log_ent_t){ r->boot_seq, b.raw, (uint32_t)b.cause };
    r->head = (r->head + 1) % LOG_N;
    r->check = ret_check(r);
    if (c->v == VEND_ST) st_csr_write(&c->st, c->st.csr | ST_RMVF);
    else                 nrf_resetreas_write(&c->nrf, b.raw);
    b.mode = r->streak >= SAFE_STREAK ? BOOT_SAFE : cold ? BOOT_COLD : BOOT_WARM;
    b.streak = r->streak;
    b.seq = r->boot_seq;
    return b;
}

/* 앱이 "정상 동작 확인"(예: 30초 무사 + 통신 OK)을 표시 → streak 0 */
static void app_mark_healthy(chip_t *c) {
    c->ret.streak = 0;
    c->ret.check = ret_check(&c->ret);
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

#define OP_HEALTHY EV__N   /* 시나리오에서 "앱이 healthy 표시" */
typedef struct { int op; cause_t cause; boot_mode_t mode; uint32_t streak; } step_t;

static void chip_init(chip_t *c, vendor_t v, uint32_t seed) {
    memset(c, 0, sizeof *c);
    c->v = v; c->st.pin_echo = true; c->rng = seed;
}

/* 시나리오 한 줄 실행: 리셋 → (ST 면 부트로더가 LSI 켬) → 부팅 코드. 불일치 수를 돌려준다. */
static int run_steps(chip_t *c, const step_t *s, int n, bool trace) {
    int bad = 0;
    for (int i = 0; i < n; i++) {
        if (s[i].op == OP_HEALTHY) {
            app_mark_healthy(c);
            if (trace) printf("   -- app_mark_healthy()\n");
            continue;
        }
        chip_reset(c, (event_t)s[i].op);
        if (c->v == VEND_ST) st_csr_write(&c->st, c->st.csr | ST_LSION);
        boot_info_t b = boot_reset_handler(c);
        bool cleared = (c->v == VEND_ST) ? (c->st.csr == ST_LSION) : (c->nrf.resetreas == 0);
        bool ok = b.cause == s[i].cause && b.mode == s[i].mode && b.streak == s[i].streak && cleared;
        if (trace)
            printf("   %-7s raw=0x%08X  cause=%-7s mode=%s streak=%u seq=%u  after=0x%08X %s\n",
                   EV_NAME[s[i].op], (unsigned)b.raw,
                   (unsigned)b.cause < CAUSE__N ? CAUSE_NAME[b.cause] : "?",
                   (unsigned)b.mode <= BOOT_SAFE ? MODE_NAME[b.mode] : "?",
                   (unsigned)b.streak, (unsigned)b.seq, (unsigned)rsr_read(c), ok ? "" : "<-- 기대와 다름");
        bad += !ok;
    }
    return bad;
}

/* 무작위 이벤트 시퀀스 + 독립 오라클 */
static int fuzz(vendor_t v, uint32_t seed, int steps) {
    static const event_t ST_EV[]  = { EV_POR, EV_BOR, EV_PIN, EV_IWDG, EV_WWDG, EV_SW, EV_LPWR };
    static const event_t NRF_EV[] = { EV_POR, EV_BOR, EV_PIN, EV_IWDG, EV_SW, EV_LOCKUP, EV_WAKEOFF };
    static const cause_t ST_C[EV__N]  = { CAUSE_POR, CAUSE_BOR, CAUSE_PIN, CAUSE_IWDG, CAUSE_WWDG,
                                          CAUSE_SW, CAUSE_NONE, CAUSE_LPWR, CAUSE_NONE };
    static const cause_t NRF_C[EV__N] = { CAUSE_POWER, CAUSE_POWER, CAUSE_PIN, CAUSE_IWDG, CAUSE_NONE,
                                          CAUSE_SW, CAUSE_LOCKUP, CAUSE_NONE, CAUSE_WAKEOFF };
    chip_t c; chip_init(&c, v, seed ^ 0x9E3779B9u);
    uint32_t s = seed, streak = 0, seq = 0;
    int bad = 0;
    chip_reset(&c, EV_POR); boot_reset_handler(&c); seq = 1;      /* 첫 전원 인가 */
    for (int i = 0; i < steps; i++) {
        uint32_t r = xorshift32(&s);
        if (r % 10 == 0) { app_mark_healthy(&c); streak = 0; continue; }
        event_t ev = (v == VEND_ST) ? ST_EV[(r >> 8) % 7] : NRF_EV[(r >> 8) % 7];
        cause_t want = (v == VEND_ST) ? ST_C[ev] : NRF_C[ev];
        bool power = ev == EV_POR || ev == EV_BOR;
        if (power || ev == EV_PIN) streak = 0;
        else if (ev == EV_IWDG || ev == EV_WWDG || ev == EV_LOCKUP) streak++;
        seq = power ? 1 : seq + 1;
        boot_mode_t mode = streak >= SAFE_STREAK ? BOOT_SAFE : power ? BOOT_COLD : BOOT_WARM;
        chip_reset(&c, ev);
        boot_info_t b = boot_reset_handler(&c);
        const log_ent_t *last = &c.ret.log[(c.ret.head + LOG_N - 1) % LOG_N];
        bad += b.cause != want || b.mode != mode || b.streak != streak || b.seq != seq
             || rsr_read(&c) != 0 || last->seq != seq || last->raw != b.raw || last->cause != (uint32_t)want;
    }
    return bad;
}

int main(void) {
    /* ---- Part A: ST 스타일 레지스터 ---- */
    st_rcc_t st = { 0, true };
    CHECK(st_hw_reset(&st, EV_POR) && st.csr == 0x0E000000u);            /* POR|BOR|PIN */
    st_rcc_t st2 = { 0, false };
    st_hw_reset(&st2, EV_POR);
    CHECK(st2.csr == (ST_PORRSTF | ST_BORRSTF));                         /* 핀 에코 없는 칩 */
    st_hw_reset(&st, EV_SW);                                             /* 안 지우고 또 리셋 */
    CHECK(st.csr == (ST_PORRSTF | ST_BORRSTF | ST_PINRSTF | ST_SFTRSTF)); /* sticky 누적 */
    st_csr_write(&st, ST_LSION);                                         /* 설정 비트만 씀 */
    CHECK(st.csr == (ST_PORRSTF | ST_BORRSTF | ST_PINRSTF | ST_SFTRSTF | ST_LSION));
    st_csr_write(&st, st.csr | ST_RMVF);                                 /* 올바른 클리어 */
    CHECK(st.csr == ST_LSION);                                           /* 플래그 0, LSION 보존, RMVF 읽으면 0 */
    st_csr_write(&st, ST_LSION | ST_IWDGRSTF);                           /* 플래그에 1 써도 무시 */
    CHECK(st.csr == ST_LSION);
    st_hw_reset(&st, EV_IWDG);
    CHECK(st.csr == (ST_IWDGRSTF | ST_PINRSTF));                         /* LSION 은 시스템 리셋에 0 */
    st_hw_reset(&st, EV_BOR);
    CHECK(st.csr == (ST_BORRSTF | ST_PINRSTF));                          /* 파워 리셋이 옛 플래그를 지움 */
    CHECK(st_hw_reset(&st, EV_LOCKUP) == false && st_hw_reset(&st, EV_WAKEOFF) == false);

    /* ---- Part A: nRF 스타일 레지스터 ---- */
    nrf_power_t np = { 0xFFFFFFFFu };
    CHECK(nrf_hw_reset(&np, EV_POR) && np.resetreas == 0);
    nrf_hw_reset(&np, EV_IWDG);
    nrf_hw_reset(&np, EV_SW);
    CHECK(np.resetreas == (NRF_DOG | NRF_SREQ));                         /* sticky 누적 */
    nrf_resetreas_write(&np, NRF_DOG);
    CHECK(np.resetreas == NRF_SREQ);                                     /* W1C: DOG 만 지움 */
    nrf_resetreas_write(&np, 0);
    CHECK(np.resetreas == NRF_SREQ);                                     /* 0 쓰기는 무변화 */
    nrf_hw_reset(&np, EV_WAKEOFF);
    CHECK(np.resetreas == (NRF_SREQ | NRF_OFF));
    nrf_hw_reset(&np, EV_BOR);
    CHECK(np.resetreas == 0);                                            /* BOR 도 0 */
    nrf_hw_reset(&np, EV_LOCKUP);
    nrf_resetreas_write(&np, 0xFFFFFFFFu);
    CHECK(np.resetreas == 0);
    CHECK(nrf_hw_reset(&np, EV_WWDG) == false && nrf_hw_reset(&np, EV_LPWR) == false);

    /* ---- Part B: 디코딩과 우선순위 ---- */
    CHECK(st_to_rc(0x0E000000u) == (RC_POR | RC_BOR | RC_PIN));
    CHECK(primary_cause(st_to_rc(0x0E000000u)) == CAUSE_POR);            /* POR 이 BOR·PIN 을 이긴다 */
    CHECK(primary_cause(st_to_rc(ST_IWDGRSTF | ST_PINRSTF)) == CAUSE_IWDG); /* PIN 은 에코 */
    CHECK(primary_cause(st_to_rc(ST_PINRSTF)) == CAUSE_PIN);
    CHECK(primary_cause(st_to_rc(ST_LPWRRSTF | ST_PINRSTF)) == CAUSE_LPWR);
    CHECK(primary_cause(st_to_rc(ST_LSION)) == CAUSE_NONE);              /* 플래그 없음 */
    CHECK(nrf_to_rc(0) == RC_POWER && primary_cause(nrf_to_rc(0)) == CAUSE_POWER);
    CHECK(nrf_to_rc(NRF_DOG | NRF_RESETPIN) == (RC_IWDG | RC_PIN));
    CHECK(primary_cause(nrf_to_rc(NRF_OFF)) == CAUSE_WAKEOFF);
    CHECK(primary_cause(nrf_to_rc(NRF_LOCKUP | NRF_SREQ)) == CAUSE_LOCKUP);
    {   /* 우선순위 인코더 == ctz+1 (전수 1024 마스크, 테스트에서만 내장 함수) */
        int bad = 0;
        for (uint32_t m = 0; m < 1024u; m++)
            bad += primary_cause(m) != (m ? (cause_t)(__builtin_ctz(m) + 1) : CAUSE_NONE);
        CHECK(bad == 0);
    }
    {   /* 함정 1: 부팅 때 안 지우면 sticky 가 쌓여 "최근 원인" 을 잃는다 */
        st_rcc_t s = { 0, true };
        st_hw_reset(&s, EV_POR); st_hw_reset(&s, EV_IWDG);              /* 실제 마지막 = IWDG */
        CHECK(primary_cause(st_to_rc(s.csr)) == CAUSE_POR);              /* 디코드는 POR 라고 함 */
        st_hw_reset(&s, EV_POR); st_csr_write(&s, s.csr | ST_RMVF); st_hw_reset(&s, EV_IWDG);
        CHECK(primary_cause(st_to_rc(s.csr)) == CAUSE_IWDG);             /* 매 부팅 클리어하면 정답 */
        nrf_power_t q = { 0 };
        nrf_hw_reset(&q, EV_IWDG); nrf_hw_reset(&q, EV_PIN);              /* 마지막 = PIN */
        CHECK(primary_cause(nrf_to_rc(q.resetreas)) == CAUSE_IWDG);      /* 그런데 IWDG 로 보임 */
    }
    {   /* 함정 2: 부트로더가 먼저 지우면 앱은 아무것도 못 본다 */
        st_rcc_t s = { 0, true };
        st_hw_reset(&s, EV_IWDG); st_csr_write(&s, s.csr | ST_RMVF);
        CHECK(primary_cause(st_to_rc(s.csr)) == CAUSE_NONE);
        nrf_power_t q = { 0 };
        nrf_hw_reset(&q, EV_IWDG); nrf_resetreas_write(&q, q.resetreas);
        CHECK(primary_cause(nrf_to_rc(q.resetreas)) == CAUSE_POWER);     /* nRF: 전원 인가로 오판! */
    }
    {   /* 함정 3: W1C 에 |= 를 쓰면 다 지워진다 / RO 플래그에 &= ~ 는 아무 일도 안 한다 */
        nrf_power_t q = { NRF_DOG | NRF_RESETPIN };
        nrf_resetreas_write(&q, q.resetreas | NRF_DOG);                  /* RESETREAS |= DOG */
        CHECK(q.resetreas == 0);                                         /* RESETPIN 까지 사라짐 */
        st_rcc_t s = { ST_IWDGRSTF | ST_PINRSTF, true };
        st_csr_write(&s, s.csr & ~ST_IWDGRSTF);                          /* CSR &= ~IWDGRSTF */
        CHECK(s.csr == (ST_IWDGRSTF | ST_PINRSTF));                      /* 그대로 */
    }

    /* ---- Part C: 부팅 코드 시나리오 ---- */
    {
        printf("[S1] ST 스타일 (pin_echo): 전원 → 워치독 3연속 → safe mode → 핀 리셋\n");
        static const step_t s1[] = {
            { EV_POR,  CAUSE_POR,  BOOT_COLD, 0 },
            { EV_IWDG, CAUSE_IWDG, BOOT_WARM, 1 },
            { EV_IWDG, CAUSE_IWDG, BOOT_WARM, 2 },
            { EV_IWDG, CAUSE_IWDG, BOOT_SAFE, 3 },
            { EV_PIN,  CAUSE_PIN,  BOOT_WARM, 0 },
            { EV_SW,   CAUSE_SW,   BOOT_WARM, 0 },
        };
        chip_t c; chip_init(&c, VEND_ST, 1);
        CHECK(run_steps(&c, s1, 6, true) == 0);
        CHECK(c.ret.boot_seq == 6 && c.ret.head == 6);
        CHECK(c.ret.log[3].cause == CAUSE_IWDG && c.ret.log[3].raw == (ST_IWDGRSTF | ST_PINRSTF | ST_LSION));
        CHECK(c.ret.log[0].raw == (0x0E000000u | ST_LSION));
    }
    {
        printf("[S2] ST: healthy 표시가 streak 를 끊는다 / BOR 은 콜드\n");
        static const step_t s2[] = {
            { EV_POR,  CAUSE_POR,  BOOT_COLD, 0 },
            { EV_IWDG, CAUSE_IWDG, BOOT_WARM, 1 },
            { EV_WWDG, CAUSE_WWDG, BOOT_WARM, 2 },
            { OP_HEALTHY, CAUSE_NONE, BOOT_WARM, 0 },
            { EV_IWDG, CAUSE_IWDG, BOOT_WARM, 1 },
            { EV_SW,   CAUSE_SW,   BOOT_WARM, 1 },                       /* SW 는 streak 유지 */
            { EV_IWDG, CAUSE_IWDG, BOOT_WARM, 2 },
            { EV_BOR,  CAUSE_BOR,  BOOT_COLD, 0 },
        };
        chip_t c; chip_init(&c, VEND_ST, 2);
        CHECK(run_steps(&c, s2, 8, true) == 0);
        CHECK(c.ret.boot_seq == 1);                                      /* BOR 로 다시 1 */
    }
    {
        printf("[S3] nRF 스타일: 0 = 전원, DOG 3연속 safe, LOCKUP, System OFF 깨어남\n");
        static const step_t s3[] = {
            { EV_POR,     CAUSE_POWER,   BOOT_COLD, 0 },
            { EV_IWDG,    CAUSE_IWDG,    BOOT_WARM, 1 },
            { EV_LOCKUP,  CAUSE_LOCKUP,  BOOT_WARM, 2 },
            { EV_IWDG,    CAUSE_IWDG,    BOOT_SAFE, 3 },
            { EV_IWDG,    CAUSE_IWDG,    BOOT_SAFE, 4 },
            { EV_PIN,     CAUSE_PIN,     BOOT_WARM, 0 },
            { EV_WAKEOFF, CAUSE_WAKEOFF, BOOT_WARM, 0 },
            { EV_BOR,     CAUSE_POWER,   BOOT_COLD, 0 },
        };
        chip_t c; chip_init(&c, VEND_NRF, 3);
        CHECK(run_steps(&c, s3, 8, true) == 0);
    }
    {   /* retained RAM 이 웜 리셋 중에 깨지면 → 콜드로 재초기화 (streak 도 잃는다) */
        chip_t c; chip_init(&c, VEND_ST, 4);
        chip_reset(&c, EV_POR); boot_reset_handler(&c);
        chip_reset(&c, EV_IWDG); boot_reset_handler(&c);
        c.ret.log[2].raw ^= 0x40u;                                       /* 떠돌이 쓰기 */
        chip_reset(&c, EV_IWDG);
        boot_info_t b = boot_reset_handler(&c);
        CHECK(!b.ret_valid && b.mode == BOOT_COLD && b.cause == CAUSE_IWDG && b.streak == 1 && b.seq == 1);
    }
    CHECK(fuzz(VEND_ST, 0xC0FFEEu, 20000) == 0);
    CHECK(fuzz(VEND_NRF, 0xBADF00Du, 20000) == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
