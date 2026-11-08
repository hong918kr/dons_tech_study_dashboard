/* SIV-B09 Poll-with-Timeout 헬퍼와 에러 처리
 *
 * 검증 코드의 절반은 "하드웨어가 어떤 상태가 될 때까지 기다리기"다: PLL lock, PCIe link up, DMA done...
 * while (!(STATUS & READY)) ; 는 칩이 응답하지 않으면 테스트 러너 전체를 멈춘다(hang).
 * 이 레슨의 헬퍼는 (1) 반드시 끝나고 (2) 끝났을 때 "왜"를 남긴다 (마지막 읽은 값, 경과시간, 호출 위치).
 *
 * 결정성(determinism): 실제 시간을 쓰지 않는다. bus ops 의 now_us()/delay_us() 를 가짜 시계(sim.now)에
 * 연결하고, mock 은 읽기 1회마다 시계를 read_cost_us 만큼 전진시킨다 → 타임아웃 테스트가 0 ms 만에,
 * 매번 같은 poll 횟수로 끝난다. (clock_gettime 실시간 버전은 demo_realtime.c)
 * SIV-B02 의 mock MMIO 를 "읽기 + 스크립트된 상태 변화 + 결함 주입" 만 남기고 축약해 넣었다.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------- 버스 ops: 실보드에선 MMIO load + 하드웨어 타이머 ---------------- */
typedef struct {
    uint32_t (*read32)(void *ctx, uint32_t off);
    uint32_t (*now_us)(void *ctx);           /* 단조 증가 µs 카운터. uint32_t → 약 71.6분마다 wrap */
    void     (*delay_us)(void *ctx, uint32_t us);
    void     *ctx;
} bus_ops_t;

typedef struct { const char *name; uint32_t off; } reg_t;

typedef enum { WAIT_OK, WAIT_TIMEOUT, WAIT_HW_ERROR, WAIT_BUS_DEAD, WAIT_BAD_ARG, WAIT_PENDING } wait_status_t;
static const char *const wait_status_name[] = { "OK", "TIMEOUT", "HW_ERROR", "BUS_DEAD", "BAD_ARG", "PENDING" };

typedef struct {
    wait_status_t status;
    const char *reg_name; uint32_t off;            /* 어디를                                   */
    uint32_t mask, value, err_mask, timeout_us;    /* 무엇을 기다렸나                           */
    uint32_t last;                                  /* 마지막으로 읽은 원시값 (RC 비트도 여기 남음) */
    uint32_t polls;                                 /* 읽은 횟수                                 */
    uint32_t elapsed_us;                            /* 시작 ~ 마지막 읽기 직후                    */
    const char *file; int line; const char *func;   /* 누가 기다렸나 (호출 위치)                  */
} wait_result_t;

static void res_init(wait_result_t *res, const reg_t *r, uint32_t mask, uint32_t value, uint32_t err_mask,
                     uint32_t timeout_us, const char *file, int line, const char *func) {
    *res = (wait_result_t){ .status = WAIT_PENDING, .reg_name = r->name, .off = r->off, .mask = mask,
                            .value = value, .err_mask = err_mask, .timeout_us = timeout_us,
                            .file = file, .line = line, .func = func };
}

/* ---------------- Part A: 기본 wait_for_field ---------------- */
/* (read & mask) == value 가 될 때까지 busy-poll. timeout_us 가 지나면 WAIT_TIMEOUT.
 * 어떤 경우든 res 에 마지막 읽은 값 / poll 횟수 / 경과시간 / 호출 위치를 채운다. */
static wait_status_t wait_for_field_at(const bus_ops_t *b, const reg_t *r, uint32_t mask, uint32_t value,
                                       uint32_t timeout_us, wait_result_t *res,
                                       const char *file, int line, const char *func) {
    (void)b; (void)r; (void)mask; (void)value; (void)timeout_us; (void)res; (void)file; (void)line; (void)func; (void)res_init;
    /* TODO: res_init → start = now_us() → loop { read, polls++, last, elapsed(unsigned 뺄셈),
     *       조건 만족이면 OK, elapsed >= timeout 이면 TIMEOUT } — res->status 도 같이 채운다 */
    return WAIT_BAD_ARG;
}
#define WAIT_FOR_FIELD(b, r, mask, value, timeout_us, res) \
    wait_for_field_at((b), (r), (mask), (value), (timeout_us), (res), __FILE__, __LINE__, __func__)

/* ---------------- Part B: 에러 비트 · 마감 후 재확인 · 간격 · 인자 검사 ---------------- */
typedef struct {
    uint32_t mask, value;     /* 완료 조건: (v & mask) == value                         */
    uint32_t err_mask;        /* 이 중 한 비트라도 1 → 즉시 WAIT_HW_ERROR (0 = 없음)      */
    uint32_t timeout_us;
    uint32_t interval_us;     /* poll 사이 쉬는 시간 (0 = busy-poll)                     */
    bool     allones_dead;    /* 0xFFFFFFFF 읽힘 → WAIT_BUS_DEAD (PCIe 무응답, 전원/클럭 off) */
} wait_spec_t;

/* 값 하나를 분류. 우선순위: BUS_DEAD > HW_ERROR > OK > PENDING */
static wait_status_t wait_classify(const wait_spec_t *s, uint32_t v) {
    (void)s; (void)v;
    return WAIT_PENDING; /* TODO: all-ones(옵션) → err 비트 → 완료 조건 순서로 판정 */
}
/* 절대 만족할 수 없는(=반드시 타임아웃 나는) 조건을 미리 거른다 */
static bool wait_spec_valid(const wait_spec_t *s) {
    (void)s;
    return true; /* TODO: mask==0, mask 밖의 value 비트, err_mask 와 겹치는 value 를 거부 */
}
static wait_status_t wait_for_field_ex_at(const bus_ops_t *b, const reg_t *r, const wait_spec_t *s,
                                          wait_result_t *res, const char *file, int line, const char *func) {
    (void)b; (void)r; (void)s; (void)res; (void)file; (void)line; (void)func; (void)wait_classify; (void)wait_spec_valid;
    /* TODO: res_init → 인자 검사(BAD_ARG, 버스 접근 0회) → loop { read, 기록, classify,
     *       마감이 지났으면 "한 번 더" 읽고 판정, 아니면 min(interval, 남은 시간) 만큼 delay } */
    return WAIT_BAD_ARG;
}
#define WAIT_EX(b, r, spec, res) wait_for_field_ex_at((b), (r), (spec), (res), __FILE__, __LINE__, __func__)

/* ---------------- Part C: PCIe 링크 + DMA 엔진 (mock 레지스터 맵) ---------------- */
#define PCIE_LTSSM_OFF  0x104u      /* [5:0] LTSSM 상태, [31:6] reserved = 0 → 0xFFFFFFFF 는 정상값이 아니다 */
#define LTSSM_MASK      0x3Fu
enum { LTSSM_DETECT_QUIET = 0x00, LTSSM_DETECT_ACT = 0x01, LTSSM_POLL_ACTIVE = 0x02,
       LTSSM_POLL_COMPLIANCE = 0x03, LTSSM_POLL_CONFIG = 0x04, LTSSM_CFG_LINKWD_START = 0x07, LTSSM_L0 = 0x11 };
#define DMA_STATUS_OFF  0x204u
#define DMA_DONE        (1u << 0)   /* 전송 완료              */
#define DMA_ERR         (1u << 1)   /* 에러 (코드는 [11:8])   */
#define DMA_BUSY        (1u << 2)
#define DMA_CODE_SHIFT  8u
#define DMA_CODE_MASK   (0xFu << DMA_CODE_SHIFT)
enum { DMA_CODE_NONE = 0, DMA_CODE_SLVERR = 1, DMA_CODE_DECERR = 2, DMA_CODE_BADDESC = 3 };

#define LINK_TIMEOUT_US 100000u     /* 100 ms */
#define LINK_POLL_US    100u
#define DMA_TIMEOUT_US  5000u
#define DMA_POLL_US     10u

static const reg_t PCIE_LTSSM = { "PCIE_LTSSM", PCIE_LTSSM_OFF };
static const reg_t DMA_STATUS = { "DMA_STATUS", DMA_STATUS_OFF };

static const char *ltssm_name(uint32_t v) {                 /* 주어진 디코더 */
    switch (v & LTSSM_MASK) {
    case LTSSM_DETECT_QUIET: return "DETECT_QUIET";   case LTSSM_DETECT_ACT: return "DETECT_ACT";
    case LTSSM_POLL_ACTIVE: return "POLL_ACTIVE";     case LTSSM_POLL_COMPLIANCE: return "POLL_COMPLIANCE";
    case LTSSM_POLL_CONFIG: return "POLL_CONFIG";     case LTSSM_CFG_LINKWD_START: return "CFG_LINKWD_START";
    case LTSSM_L0: return "L0";                       default: return "LTSSM_?";
    }
}
static const char *dma_decode(uint32_t v) {                 /* 주어진 디코더 */
    static const char *const codes[] = { "NONE", "SLVERR", "DECERR", "BADDESC" };
    static char buf[64];
    uint32_t c = (v & DMA_CODE_MASK) >> DMA_CODE_SHIFT;
    snprintf(buf, sizeof buf, "busy=%u done=%u err=%u code=%s", (unsigned)((v & DMA_BUSY) != 0),
             (unsigned)(v & DMA_DONE), (unsigned)((v & DMA_ERR) != 0), c < 4 ? codes[c] : "?");
    return buf;
}

/* LTSSM 필드가 L0 가 될 때까지: 100 ms, 100 µs 간격, all-ones = 버스 사망 */
static wait_status_t pcie_wait_l0(const bus_ops_t *b, wait_result_t *res) {
    (void)b; (void)res; (void)PCIE_LTSSM;
    return WAIT_BAD_ARG; /* TODO: wait_spec_t 를 채우고 WAIT_EX 로 호출 (호출 위치가 이 함수로 기록되게) */
}
/* DONE 이 설 때까지: ERR 은 즉시 실패, 5 ms, 10 µs 간격 */
static wait_status_t dma_wait_done(const bus_ops_t *b, wait_result_t *res) {
    (void)b; (void)res; (void)DMA_STATUS;
    return WAIT_BAD_ARG; /* TODO */
}
/* 디버그 엔지니어가 바로 행동할 수 있는 한 줄 리포트. 반환값 ≥ n 이면 잘림 (snprintf 처럼 항상 NUL 종료) */
static int wait_report(char *buf, size_t n, const wait_result_t *r, const char *(*decode)(uint32_t)) {
    (void)r; (void)decode; (void)wait_status_name;
    if (n) buf[0] = '\0';
    return 0; /* TODO: "STATUS reg=.. off=0x%03X mask=.. exp=.. last=.. [decode] err=.. polls=.. elapsed=..us/..us at file:line func()" */
}

/* ---------------- mock (주어진 코드): 레지스터 저장소 + 가짜 시계 + 결함 주입 ---------------- */
#define REG_WORDS 0x100u                                  /* 오프셋 0x000..0x3FC */
typedef struct { uint32_t at_read; uint32_t val; } step_t; /* 그 레지스터의 at_read 번째 읽기 직전에 HW가 val 로 바꿈 */
typedef struct {
    uint32_t regs[REG_WORDS];
    uint32_t now;              /* 가짜 시계 (µs) */
    uint32_t read_cost_us;     /* 읽기 1회가 소비하는 가짜 시간 */
    uint32_t reads, delayed_us;
    uint32_t script_off, script_reads; const step_t *steps; size_t nsteps;
    uint32_t rc_mask;          /* 결함/특성: 스크립트 레지스터의 이 비트들은 읽으면 지워짐 (RC) */
    uint32_t jump_after_read;  /* 결함: 전체 읽기 수가 이 값이 된 직후 시계가 jump_us 만큼 튐 (선점/긴 ISR) */
    uint32_t jump_us;
    bool     dead;             /* 결함: 전원/클럭 off → 모든 읽기 0xFFFFFFFF */
} sim_t;

static sim_t g_sim;
static uint32_t sim_read32(void *ctx, uint32_t off) {
    sim_t *s = ctx;
    uint32_t v;
    s->reads++;
    s->now += s->read_cost_us;
    if (s->dead) {
        v = 0xFFFFFFFFu;
    } else if (off / 4u >= REG_WORDS) {
        v = 0xDEADBEEFu;
    } else {
        bool scripted = s->steps && off == s->script_off;
        if (scripted) {
            s->script_reads++;
            for (size_t i = 0; i < s->nsteps; i++)
                if (s->steps[i].at_read == s->script_reads) s->regs[off / 4u] = s->steps[i].val;
        }
        v = s->regs[off / 4u];
        if (scripted) s->regs[off / 4u] &= ~s->rc_mask;
    }
    if (s->jump_after_read && s->reads == s->jump_after_read) s->now += s->jump_us;
    return v;
}
static uint32_t sim_now(void *ctx) { return ((sim_t *)ctx)->now; }
static void sim_delay(void *ctx, uint32_t us) { sim_t *s = ctx; s->now += us; s->delayed_us += us; }
static const bus_ops_t g_bus = { sim_read32, sim_now, sim_delay, &g_sim };

static void sim_reset(void) { memset(&g_sim, 0, sizeof g_sim); g_sim.read_cost_us = 1; }
static void sim_script(uint32_t off, const step_t *steps, size_t n) {
    g_sim.script_off = off; g_sim.steps = steps; g_sim.nsteps = n;
}
#define NSTEPS(a) (sizeof(a) / sizeof((a)[0]))

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

#define STATUS_OFF 0x004u
#define ST_READY   (1u << 0)
#define ST_ERR     (1u << 1)
static const reg_t STATUS = { "STATUS", STATUS_OFF };

int main(void) {
    wait_result_t res = {0};
    char line[256] = "";

    /* ===== Part A ===== */
    /* A1: 5번째 읽기에서 READY */
    static const step_t a1[] = { { 5, ST_READY } };
    sim_reset(); sim_script(STATUS_OFF, a1, NSTEPS(a1));
    CHECK(WAIT_FOR_FIELD(&g_bus, &STATUS, ST_READY, ST_READY, 1000, &res) == WAIT_OK);
    CHECK(res.polls == 5 && res.last == ST_READY && res.elapsed_us == 5);

    /* A2: 영원히 안 됨 → TIMEOUT + 진단 정보 */
    static const step_t a2[] = { { 1, 0x40u } };            /* 다른 비트만 서 있고 READY 는 0 */
    sim_reset(); sim_script(STATUS_OFF, a2, NSTEPS(a2));
    int ln = __LINE__; wait_status_t st = WAIT_FOR_FIELD(&g_bus, &STATUS, ST_READY, ST_READY, 1000, &res);
    CHECK(st == WAIT_TIMEOUT && res.status == WAIT_TIMEOUT);
    CHECK(res.last == 0x40u && res.polls == 1000 && res.elapsed_us == 1000);
    CHECK(res.line == ln && res.file && strcmp(res.file, __FILE__) == 0 && res.func && strcmp(res.func, "main") == 0);
    CHECK(res.reg_name && strcmp(res.reg_name, "STATUS") == 0 && res.off == STATUS_OFF && res.timeout_us == 1000);

    /* A3: 여러 비트 필드 [7:4] == 0x5 (중간값 0x1, 0x3 을 지나감) */
    static const step_t a3[] = { { 1, 0x10u }, { 3, 0x30u }, { 4, 0x5Cu } };
    sim_reset(); sim_script(STATUS_OFF, a3, NSTEPS(a3));
    CHECK(WAIT_FOR_FIELD(&g_bus, &STATUS, 0xF0u, 0x50u, 1000, &res) == WAIT_OK);
    CHECK(res.polls == 4 && res.last == 0x5Cu);

    /* A4: 이미 준비됨 → 1회 / A5: timeout 0 → 딱 한 번 보고 판단 */
    static const step_t a4[] = { { 1, ST_READY } };
    sim_reset(); sim_script(STATUS_OFF, a4, NSTEPS(a4));
    CHECK(WAIT_FOR_FIELD(&g_bus, &STATUS, ST_READY, ST_READY, 1000, &res) == WAIT_OK && res.polls == 1);
    sim_reset();
    CHECK(WAIT_FOR_FIELD(&g_bus, &STATUS, ST_READY, ST_READY, 0, &res) == WAIT_TIMEOUT && res.polls == 1);

    /* ===== Part B ===== */
    /* B1: 4번째 읽기에서 ERR → 타임아웃까지 기다리지 않고 즉시 HW_ERROR */
    static const step_t b1[] = { { 4, ST_ERR | 0x500u } };
    sim_reset(); sim_script(STATUS_OFF, b1, NSTEPS(b1));
    wait_spec_t sp = { .mask = ST_READY, .value = ST_READY, .err_mask = ST_ERR, .timeout_us = 1000, .interval_us = 10 };
    CHECK(WAIT_EX(&g_bus, &STATUS, &sp, &res) == WAIT_HW_ERROR);
    CHECK(res.polls == 4 && res.last == (ST_ERR | 0x500u) && res.elapsed_us < 100);

    /* B2: DONE 과 ERR 이 같은 읽기에 함께 섬 → 에러가 이긴다 */
    static const step_t b2[] = { { 2, ST_READY | ST_ERR } };
    sim_reset(); sim_script(STATUS_OFF, b2, NSTEPS(b2));
    CHECK(WAIT_EX(&g_bus, &STATUS, &sp, &res) == WAIT_HW_ERROR && res.polls == 2);

    /* B3: 버스 사망(0xFFFFFFFF). allones_dead 가 없으면 ERR 비트로 오진된다 */
    sim_reset(); g_sim.dead = true;
    sp.allones_dead = true;
    CHECK(WAIT_EX(&g_bus, &STATUS, &sp, &res) == WAIT_BUS_DEAD && res.polls == 1 && res.last == 0xFFFFFFFFu);
    sp.allones_dead = false;
    sim_reset(); g_sim.dead = true;
    CHECK(WAIT_EX(&g_bus, &STATUS, &sp, &res) == WAIT_HW_ERROR);      /* 오진: 이게 왜 위험한지 md 참고 */

    /* B4: 선점(preemption) 주입 — 2번째 읽기 직후 시계가 5000us 튐, 장치는 3번째 읽기에서 READY */
    static const step_t b4[] = { { 3, ST_READY } };
    wait_spec_t sp4 = { .mask = ST_READY, .value = ST_READY, .timeout_us = 1000 };
    sim_reset(); sim_script(STATUS_OFF, b4, NSTEPS(b4)); g_sim.jump_after_read = 2; g_sim.jump_us = 5000;
    CHECK(WAIT_EX(&g_bus, &STATUS, &sp4, &res) == WAIT_OK && res.polls == 3);   /* 마감 후 재확인 덕분 */
    sim_reset(); sim_script(STATUS_OFF, b4, NSTEPS(b4)); g_sim.jump_after_read = 2; g_sim.jump_us = 5000;
    CHECK(WAIT_FOR_FIELD(&g_bus, &STATUS, ST_READY, ST_READY, 1000, &res) == WAIT_TIMEOUT && res.polls == 2);
    /* ↑ Part A 버전은 거짓 타임아웃(false timeout) — 주입한 결함이 실제로 "물린다"는 증거 */

    /* B5: 시계 wrap — 0xFFFFFF00 에서 시작 */
    sim_reset(); g_sim.now = 0xFFFFFF00u;
    CHECK(WAIT_EX(&g_bus, &STATUS, &sp4, &res) == WAIT_TIMEOUT);
    CHECK(res.polls == 1001 && res.elapsed_us == 1001);
    static const step_t b5[] = { { 300, ST_READY } };       /* 256번째 읽기 근처에서 시계가 0 을 지남 */
    sim_reset(); g_sim.now = 0xFFFFFF00u; sim_script(STATUS_OFF, b5, NSTEPS(b5));
    CHECK(WAIT_EX(&g_bus, &STATUS, &sp4, &res) == WAIT_OK && res.polls == 300);

    /* B6: 간격 300us, 마지막 sleep 은 남은 시간으로 잘린다 */
    wait_spec_t sp6 = { .mask = ST_READY, .value = ST_READY, .timeout_us = 1000, .interval_us = 300 };
    sim_reset();
    CHECK(WAIT_EX(&g_bus, &STATUS, &sp6, &res) == WAIT_TIMEOUT);
    CHECK(res.polls == 6 && g_sim.delayed_us == 996 && res.elapsed_us == 1002);

    /* B7: 만족 불가능한 조건은 버스를 건드리기 전에 거부 */
    wait_spec_t bad1 = { .mask = 0, .value = 0, .timeout_us = 1000 };
    wait_spec_t bad2 = { .mask = 0x0Fu, .value = 0x10u, .timeout_us = 1000 };
    wait_spec_t bad3 = { .mask = 0x03u, .value = 0x02u, .err_mask = 0x02u, .timeout_us = 1000 };
    sim_reset();
    CHECK(WAIT_EX(&g_bus, &STATUS, &bad1, &res) == WAIT_BAD_ARG && res.polls == 0);
    CHECK(WAIT_EX(&g_bus, &STATUS, &bad2, &res) == WAIT_BAD_ARG);
    CHECK(WAIT_EX(&g_bus, &STATUS, &bad3, &res) == WAIT_BAD_ARG && g_sim.reads == 0);

    /* B8: READY 가 RC(읽으면 지워짐) → 결과는 res.last 에만 남는다 */
    static const step_t b8[] = { { 2, ST_READY | 0x80u } };
    sim_reset(); sim_script(STATUS_OFF, b8, NSTEPS(b8)); g_sim.rc_mask = ST_READY;
    CHECK(WAIT_EX(&g_bus, &STATUS, &sp4, &res) == WAIT_OK && res.last == (ST_READY | 0x80u));
    CHECK(sim_read32(&g_sim, STATUS_OFF) == 0x80u);        /* 다시 읽으면 READY 는 이미 없다 */

    /* ===== Part C ===== */
    printf("-- Part C reports --\n");
    /* C1: 정상 링크 트레이닝 Detect → Polling → Config → L0 */
    static const step_t link_ok[] = { { 1, LTSSM_DETECT_QUIET }, { 3, LTSSM_DETECT_ACT }, { 6, LTSSM_POLL_ACTIVE },
                                      { 10, LTSSM_POLL_CONFIG }, { 14, LTSSM_CFG_LINKWD_START }, { 20, LTSSM_L0 } };
    sim_reset(); sim_script(PCIE_LTSSM_OFF, link_ok, NSTEPS(link_ok));
    CHECK(pcie_wait_l0(&g_bus, &res) == WAIT_OK && res.polls == 20);
    CHECK(wait_report(line, sizeof line, &res, ltssm_name) > 0 && strncmp(line, "OK reg=PCIE_LTSSM", 17) == 0);
    printf("%s\n", line);

    /* C2: Polling.Compliance 에 갇힘 → 100 ms 타임아웃, 리포트에 상태 이름 */
    static const step_t link_stuck[] = { { 1, LTSSM_DETECT_QUIET }, { 3, LTSSM_DETECT_ACT }, { 6, LTSSM_POLL_ACTIVE },
                                         { 9, LTSSM_POLL_COMPLIANCE } };
    sim_reset(); sim_script(PCIE_LTSSM_OFF, link_stuck, NSTEPS(link_stuck));
    CHECK(pcie_wait_l0(&g_bus, &res) == WAIT_TIMEOUT && res.last == LTSSM_POLL_COMPLIANCE);
    CHECK(res.elapsed_us >= LINK_TIMEOUT_US && res.func && strcmp(res.func, "pcie_wait_l0") == 0 && res.line > 0);
    wait_report(line, sizeof line, &res, ltssm_name);
    CHECK(strstr(line, "TIMEOUT reg=PCIE_LTSSM off=0x104") == line && strstr(line, "[POLL_COMPLIANCE]") != NULL);
    CHECK(strstr(line, "exp=0x00000011 last=0x00000003") != NULL && strstr(line, "pcie_wait_l0()") != NULL);
    printf("%s\n", line);

    /* C3: EP 전원/클럭 off → 첫 읽기에서 BUS_DEAD (100 ms 낭비 없음) */
    sim_reset(); g_sim.dead = true;
    CHECK(pcie_wait_l0(&g_bus, &res) == WAIT_BUS_DEAD && res.polls == 1);
    wait_report(line, sizeof line, &res, ltssm_name);
    CHECK(strncmp(line, "BUS_DEAD", 8) == 0 && strstr(line, "last=0xFFFFFFFF") != NULL && strchr(line, '[') == NULL);
    printf("%s\n", line);

    /* C4: DMA 정상 완료 */
    static const step_t dma_ok[] = { { 1, DMA_BUSY }, { 6, DMA_DONE } };
    sim_reset(); sim_script(DMA_STATUS_OFF, dma_ok, NSTEPS(dma_ok));
    CHECK(dma_wait_done(&g_bus, &res) == WAIT_OK && res.polls == 6);
    wait_report(line, sizeof line, &res, dma_decode);
    printf("%s\n", line);

    /* C5: DMA 가 3번째 읽기에서 DECERR → 즉시 HW_ERROR, 에러 코드가 리포트에 */
    static const step_t dma_err[] = { { 1, DMA_BUSY }, { 3, DMA_ERR | (DMA_CODE_DECERR << DMA_CODE_SHIFT) } };
    sim_reset(); sim_script(DMA_STATUS_OFF, dma_err, NSTEPS(dma_err));
    CHECK(dma_wait_done(&g_bus, &res) == WAIT_HW_ERROR && res.polls == 3 && res.elapsed_us < DMA_TIMEOUT_US / 10);
    wait_report(line, sizeof line, &res, dma_decode);
    CHECK(strstr(line, "err=0x00000002") != NULL && strstr(line, "code=DECERR") != NULL);
    printf("%s\n", line);

    /* C6: 잘린 버퍼에서도 넘치지 않고 NUL 종료 */
    char tiny[24];
    CHECK(wait_report(tiny, sizeof tiny, &res, dma_decode) > (int)sizeof tiny && strlen(tiny) == sizeof tiny - 1);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
