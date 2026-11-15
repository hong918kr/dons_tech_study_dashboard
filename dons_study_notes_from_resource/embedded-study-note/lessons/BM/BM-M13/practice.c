/* BM-M13 추가 연습 — 호스트 C (Mac 에서도, VM 에서도). 펌웨어 없이 fault 정책의 "판단 로직" 을 검증한다.
 *   연습 1 fault_route   : 어떤 fault 가 어느 핸들러로 가나 (SHCSR/CCR/우선순위 → 승격, lockup)
 *   연습 2 cfsr_describe : 이 레슨에서 일으킨 CFSR 비트를 이름으로 (버퍼 용량 검사)
 *   연습 3 rec_seal/valid: 리셋을 건너는 기록의 봉인 — 1비트 뒤집힘 전수, 반쯤 쓴 기록
 *   연습 4 boot_policy   : 리셋 루프 가드 — 짧은 간격의 fault 리셋 3 번이면 SAFE 모드
 *   연습 5 실제 QEMU 출력 : starter/solution 요약 표(실측)를 파싱해 연습 1·2 의 예측과 대조
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-M13_pr && /tmp/BM-M13_pr
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } else { \
    printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

#define SHCSR_MEMFAULTENA (1u << 16)
#define SHCSR_BUSFAULTENA (1u << 17)
#define SHCSR_USGFAULTENA (1u << 18)
#define CCR_UNALIGN_TRP   (1u << 3)
#define CCR_DIV_0_TRP     (1u << 4)
#define CCR_RESET         0x00000200u          /* STKALIGN — QEMU mps2-an385 실측 리셋값 */

/* ============ 연습 1: fault_route ============ */
typedef enum { K_DIV0, K_UNALIGNED_LDR, K_UNALIGNED_LDRD, K_BUS_PRECISE, K_UNDEF, K_MPU, K_COUNT } fault_kind_t;
enum { EXC_NONE = 0, EXC_HARD = 3, EXC_MEM = 4, EXC_BUS = 5, EXC_USAGE = 6, EXC_LOCKUP = 99 };
#define PRIO_THREAD 256                        /* 활성 예외 없음 (스레드 모드) */
#define PRIO_HARDFAULT (-1)
typedef struct { int exc; int forced; } route_t;

/* cur_prio: 지금 실행 중인 우선순위 (스레드 256, IRQ 의 값, HardFault -1). fault_prio: 해당 fault 의 SHPR 값 */
static route_t fault_route(fault_kind_t k, uint32_t shcsr, uint32_t ccr, int cur_prio, int fault_prio) {
    route_t r = { EXC_NONE, 0 };
    int target;
    uint32_t ena;
    switch (k) {
    case K_DIV0:           if (!(ccr & CCR_DIV_0_TRP)) return r;   /* 트랩 꺼짐: 몫 0, fault 없음 */
                           target = EXC_USAGE; ena = SHCSR_USGFAULTENA; break;
    case K_UNALIGNED_LDR:  if (!(ccr & CCR_UNALIGN_TRP)) return r; /* M3/M4: 하드웨어가 비정렬을 처리 */
                           target = EXC_USAGE; ena = SHCSR_USGFAULTENA; break;
    case K_UNALIGNED_LDRD: target = EXC_USAGE; ena = SHCSR_USGFAULTENA; break;   /* LDRD/LDM 은 항상 */
    case K_BUS_PRECISE:    target = EXC_BUS; ena = SHCSR_BUSFAULTENA; break;
    case K_UNDEF:          target = EXC_USAGE; ena = SHCSR_USGFAULTENA; break;
    case K_MPU:            target = EXC_MEM; ena = SHCSR_MEMFAULTENA; break;
    default:               return r;
    }
    if (cur_prio <= PRIO_HARDFAULT) { r.exc = EXC_LOCKUP; return r; }          /* HardFault 안의 fault */
    if (!(shcsr & ena) || fault_prio >= cur_prio) { r.exc = EXC_HARD; r.forced = 1; return r; }
    r.exc = target;
    return r;
}

static void ex1(void) {
    const uint32_t all = SHCSR_MEMFAULTENA | SHCSR_BUSFAULTENA | SHCSR_USGFAULTENA;
    const uint32_t traps = CCR_RESET | CCR_DIV_0_TRP | CCR_UNALIGN_TRP;
    /* 이름 붙은 경우 */
    route_t r = fault_route(K_DIV0, all, CCR_RESET, PRIO_THREAD, 0);
    CHECK(r.exc == EXC_NONE);
    r = fault_route(K_DIV0, all, traps, PRIO_THREAD, 0);
    CHECK(r.exc == EXC_USAGE && !r.forced);
    r = fault_route(K_UNALIGNED_LDRD, 0u, CCR_RESET, PRIO_THREAD, 0);
    CHECK(r.exc == EXC_HARD && r.forced);
    r = fault_route(K_UNDEF, all, traps, 0, 0);                       /* 우선순위 0 IRQ 안 */
    CHECK(r.exc == EXC_HARD && r.forced);
    r = fault_route(K_UNDEF, all, traps, 1, 0);                       /* IRQ 를 1 로 낮추면 */
    CHECK(r.exc == EXC_USAGE && !r.forced);
    r = fault_route(K_BUS_PRECISE, all, traps, PRIO_HARDFAULT, 0);
    CHECK(r.exc == EXC_LOCKUP);
    /* 성질 검사: 모든 (kind, SHCSR 8가지, CCR 4가지, cur, fprio) 조합 */
    static const int curs[] = { PRIO_THREAD, 15, 5, 1, 0, PRIO_HARDFAULT };
    int bad = 0, n = 0;
    for (int k = 0; k < K_COUNT; ++k)
        for (uint32_t s = 0; s < 8u; ++s)
            for (uint32_t c = 0; c < 4u; ++c)
                for (size_t ci = 0; ci < sizeof curs / sizeof curs[0]; ++ci)
                    for (int fp = 0; fp < 4; ++fp) {
                        uint32_t shcsr = s << 16, ccr = CCR_RESET | (c & 1u ? CCR_UNALIGN_TRP : 0u) | (c & 2u ? CCR_DIV_0_TRP : 0u);
                        route_t a = fault_route((fault_kind_t)k, shcsr, ccr, curs[ci], fp);
                        route_t b = fault_route((fault_kind_t)k, all, ccr, curs[ci], fp);
                        route_t z = fault_route((fault_kind_t)k, 0u, ccr, curs[ci], fp);
                        ++n;
                        if (a.forced != (a.exc == EXC_HARD)) ++bad;                 /* HardFault 는 여기선 항상 FORCED */
                        if ((a.exc == EXC_NONE) != (b.exc == EXC_NONE)) ++bad;      /* 트랩 여부는 SHCSR 와 무관 */
                        if (z.exc != EXC_NONE && z.exc != EXC_HARD && z.exc != EXC_LOCKUP) ++bad;  /* 다 끄면 Hard 로만 */
                        if (a.exc == EXC_MEM || a.exc == EXC_BUS || a.exc == EXC_USAGE)
                            if (fp >= curs[ci]) ++bad;                               /* 받았다면 지금보다 높아야 */
                    }
    printf("  ex1: %d 조합 성질 검사, 위반 %d\n", n, bad);
    CHECK(n == K_COUNT * 8 * 4 * 6 * 4 && bad == 0);
}

/* ============ 연습 2: cfsr_describe ============ */
static const struct { uint32_t bit; const char *name; } k_cfsr[] = {
    { 1u << 0, "IACCVIOL" }, { 1u << 1, "DACCVIOL" }, { 1u << 7, "MMARVALID" }, { 1u << 9, "PRECISERR" },
    { 1u << 10, "IMPRECISERR" }, { 1u << 15, "BFARVALID" }, { 1u << 16, "UNDEFINSTR" }, { 1u << 17, "INVSTATE" },
    { 1u << 24, "UNALIGNED" }, { 1u << 25, "DIVBYZERO" },
};
/* "DIVBYZERO", "PRECISERR|BFARVALID", 모르는 비트는 "|+0x...", 0 이면 "-". 반환 = 길이, 용량 부족이면 -1 (buf 는 "") */
static int cfsr_describe(uint32_t cfsr, char *buf, size_t cap) {
    char tmp[160];
    size_t len = 0;
    uint32_t known = 0;
    tmp[0] = 0;
    for (size_t i = 0; i < sizeof k_cfsr / sizeof k_cfsr[0]; ++i) {
        known |= k_cfsr[i].bit;
        if (cfsr & k_cfsr[i].bit)
            len += (size_t)snprintf(tmp + len, sizeof tmp - len, "%s%s", len ? "|" : "", k_cfsr[i].name);
    }
    if (cfsr & ~known) len += (size_t)snprintf(tmp + len, sizeof tmp - len, "%s+0x%08x", len ? "|" : "", (unsigned)(cfsr & ~known));
    if (len == 0) len = (size_t)snprintf(tmp, sizeof tmp, "-");
    if (cap == 0) return -1;
    if (len + 1 > cap) { buf[0] = 0; return -1; }
    memcpy(buf, tmp, len + 1);
    return (int)len;
}
static void ex2(void) {
    char b[64];
    CHECK(cfsr_describe(0x02000000u, b, sizeof b) == 9 && strcmp(b, "DIVBYZERO") == 0);
    CHECK(cfsr_describe(0x00008200u, b, sizeof b) > 0 && strcmp(b, "PRECISERR|BFARVALID") == 0);
    CHECK(cfsr_describe(0x00000082u, b, sizeof b) > 0 && strcmp(b, "DACCVIOL|MMARVALID") == 0);
    CHECK(cfsr_describe(0x00000000u, b, sizeof b) == 1 && strcmp(b, "-") == 0);
    CHECK(cfsr_describe(0x00010000u | (1u << 19), b, sizeof b) > 0 && strcmp(b, "UNDEFINSTR|+0x00080000") == 0);
    CHECK(cfsr_describe(0x00008200u, b, 19) == -1 && b[0] == 0);      /* 정확히 19 자 + NUL = 20 필요 */
    CHECK(cfsr_describe(0x00008200u, b, 20) == 19);
    int ok = 1;                                                        /* 비트 하나씩: 이름이 정확히 하나 */
    for (size_t i = 0; i < sizeof k_cfsr / sizeof k_cfsr[0]; ++i)
        if (cfsr_describe(k_cfsr[i].bit, b, sizeof b) != (int)strlen(k_cfsr[i].name) || strcmp(b, k_cfsr[i].name)) ok = 0;
    CHECK(ok);
}

/* ============ 연습 3: 기록 봉인 ============ */
#define CRASH_MAGIC 0xFA17C0DEu
typedef struct {
    uint32_t magic, reason, cfsr, hfsr, bfar, mmfar, pc, lr, xpsr, outputs, check;
} crash_rec_t;
static uint32_t fnv1a_words(const uint32_t *w, uint32_t n) {     /* 펌웨어와 같은 함수 */
    uint32_t h = 0x811C9DC5u;
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t s = 0; s < 32u; s += 8u) { h ^= (w[i] >> s) & 0xFFu; h *= 0x01000193u; }
    return h;
}
#define PAYLOAD_WORDS 9u
static void rec_seal(crash_rec_t *r) {
    r->magic = 0u;
    r->check = fnv1a_words(&r->reason, PAYLOAD_WORDS);
    r->magic = CRASH_MAGIC;                                  /* 마지막 */
}
static int rec_valid(const crash_rec_t *r) {
    return r->magic == CRASH_MAGIC && r->check == fnv1a_words(&r->reason, PAYLOAD_WORDS);
}
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static void ex3(void) {
    crash_rec_t r = { 0, 6u, 0x02000000u, 0u, 0u, 0u, 0x2AEu, 0xE93u, 0x01000000u, 0u, 0 };
    rec_seal(&r);
    CHECK(rec_valid(&r));
    /* 1비트 뒤집힘 전수: 11 워드 × 32 비트 = 352 가지, 전부 잡혀야 */
    int missed = 0;
    for (uint32_t w = 0; w < 11u; ++w)
        for (uint32_t b = 0; b < 32u; ++b) {
            crash_rec_t c = r;
            ((uint32_t *)&c)[w] ^= 1u << b;
            if (rec_valid(&c)) ++missed;
        }
    printf("  ex3: 1비트 뒤집힘 352 가지 중 못 잡은 것 %d\n", missed);
    CHECK(missed == 0);
    /* 무작위 다중 손상 (고정 시드): 1~4 워드를 무작위 값으로 */
    uint32_t seed = 0x13579BDFu;
    int undetected = 0;
    for (int t = 0; t < 20000; ++t) {
        crash_rec_t c = r;
        int nw = 1 + (int)(xs32(&seed) % 4u);
        for (int i = 0; i < nw; ++i) ((uint32_t *)&c)[1u + xs32(&seed) % 10u] ^= xs32(&seed) | 1u;
        if (rec_valid(&c)) ++undetected;
    }
    printf("  ex3: 무작위 다중 손상 20000 건 중 통과(미검출) %d\n", undetected);
    CHECK(undetected == 0);
    /* 반쯤 쓴 기록: 내용·체크값까지 쓰고 magic 전에 리셋 → 무효여야 */
    crash_rec_t h = r;
    h.magic = 0u;
    CHECK(!rec_valid(&h));
    /* 오래된 기록 재사용 방지: 소비 후 magic = 0 */
    crash_rec_t u = r;
    u.magic = 0u;
    CHECK(!rec_valid(&u));
    /* 콜드 부팅 RAM 쓰레기 (고정 시드): magic 이 우연히 맞을 확률은 2^-32 — 10만 건에 0 기대 */
    int garbage_ok = 0;
    for (int t = 0; t < 100000; ++t) {
        crash_rec_t g;
        for (int i = 0; i < 11; ++i) ((uint32_t *)&g)[i] = xs32(&seed);
        if (rec_valid(&g)) ++garbage_ok;
    }
    CHECK(garbage_ok == 0);
}

/* ============ 연습 4: 리셋 루프 가드 ============ */
typedef enum { CAUSE_POWER_ON, CAUSE_FAULT, CAUSE_WATCHDOG, CAUSE_SOFTWARE } cause_t;
typedef enum { MODE_NORMAL, MODE_SAFE } boot_mode_t;
#define STABLE_S   60u      /* 이만큼 문제없이 돌았으면 "안정" — 연속 카운터 초기화 */
#define LOOP_LIMIT 3u       /* 짧은 간격 fault 리셋이 연속 3 번이면 SAFE */
typedef struct { uint32_t consecutive, total_faults; boot_mode_t mode; } boot_state_t;

/* 부팅마다 한 번: 이전 리셋의 원인과, 리셋 전까지 돈 시간(uptime_s) */
static boot_mode_t boot_policy(boot_state_t *st, cause_t cause, uint32_t uptime_s) {
    switch (cause) {
    case CAUSE_POWER_ON: st->consecutive = 0; st->total_faults = 0; st->mode = MODE_NORMAL; break;
    case CAUSE_SOFTWARE: st->consecutive = 0; break;               /* 의도된 리셋 (업데이트 등) */
    case CAUSE_FAULT:
    case CAUSE_WATCHDOG:
        st->total_faults++;
        st->consecutive = (uptime_s < STABLE_S) ? st->consecutive + 1u : 1u;
        break;
    }
    if (st->mode == MODE_SAFE && cause != CAUSE_POWER_ON) return st->mode;   /* SAFE 는 사람이 풀 때까지 유지 */
    st->mode = (st->consecutive >= LOOP_LIMIT) ? MODE_SAFE : MODE_NORMAL;
    return st->mode;
}
static void ex4(void) {
    boot_state_t s;
    memset(&s, 0xA5, sizeof s);                                    /* 콜드 부팅 RAM 쓰레기 */
    CHECK(boot_policy(&s, CAUSE_POWER_ON, 0) == MODE_NORMAL && s.consecutive == 0);
    CHECK(boot_policy(&s, CAUSE_FAULT, 2) == MODE_NORMAL && s.consecutive == 1);
    CHECK(boot_policy(&s, CAUSE_FAULT, 2) == MODE_NORMAL && s.consecutive == 2);
    CHECK(boot_policy(&s, CAUSE_WATCHDOG, 1) == MODE_SAFE && s.consecutive == 3);   /* WDT 도 셈 */
    CHECK(boot_policy(&s, CAUSE_SOFTWARE, 500) == MODE_SAFE);                      /* 소프트 리셋으로는 안 풀림 */
    CHECK(boot_policy(&s, CAUSE_POWER_ON, 0) == MODE_NORMAL && s.total_faults == 0);
    /* 간헐 fault: 매번 오래 돌다가 죽으면 SAFE 로 가지 않는다 (카운터가 1 로 돌아감) */
    for (int i = 0; i < 10; ++i) boot_policy(&s, CAUSE_FAULT, 3600);
    CHECK(s.mode == MODE_NORMAL && s.consecutive == 1 && s.total_faults == 10);
    /* 경계: uptime 59 는 "짧음", 60 은 "안정" */
    boot_policy(&s, CAUSE_POWER_ON, 0);
    boot_policy(&s, CAUSE_FAULT, 59); boot_policy(&s, CAUSE_FAULT, 59);
    CHECK(boot_policy(&s, CAUSE_FAULT, 60) == MODE_NORMAL && s.consecutive == 1);
    /* 이 레슨의 QEMU 실행 (7 번 연속, 각 부팅 수 ms) 에 이 가드를 넣었다면 3 번째 fault 리셋에서 SAFE */
    boot_policy(&s, CAUSE_POWER_ON, 0);
    int safe_at = -1;
    for (int i = 1; i <= 7 && safe_at < 0; ++i)
        if (boot_policy(&s, CAUSE_FAULT, 0) == MODE_SAFE) safe_at = i;
    CHECK(safe_at == 3);
}

/* ============ 연습 5: 실제 QEMU 요약 표와 대조 ============ */
/* run_qemu.sh solution / starter 의 요약 줄 (실측 그대로) */
static const char *const k_solution_rows[] = {
    "0    DIV0      UsageFault 0x00000000 0x02000000 0x00000000 0x000002ae 0x000002ae 0x00000000",
    "1    UNALIGNED UsageFault 0x00000000 0x01000000 0x00000000 0x000002b6 0x000002b6 0x00000000",
    "2    BUSFAULT  BusFault   0x00000000 0x00008200 0x30000000 0x000002ba 0x000002ba 0x00000000",
    "3    UNDEF     UsageFault 0x00000000 0x00010000 0x00000000 0x000002c0 0x000002c0 0x00000000",
    "4    MPU_GUARD MemManage  0x00000000 0x00000082 0x20000040 0x000002ec 0x000002ec 0x00000000",
    "5    ESC_OFF   HardFault  0x40000000 0x00010000 0x00000000 0x00000310 0x00000310 0x00000000",
    "6    ESC_PRIO  HardFault  0x40000000 0x00010000 0x00000000 0x00000314 0x00000314 0x00000000",
};
static const char *const k_starter_rows[] = {
    "0    DIV0      none       0x00000000 0x00000000 0x00000000 0xffffffff 0x0000028a 0x00000003",
    "1    UNALIGNED none       0x00000000 0x00000000 0x00000000 0xffffffff 0x00000292 0x00000003",
    "2    BUSFAULT  HardFault  0x40000000 0x00008200 0x00000000 0x00000296 0x00000296 0x00000003",
    "3    UNDEF     HardFault  0x40000000 0x00010000 0x00000000 0x0000029c 0x0000029c 0x00000003",
    "4    MPU_GUARD HardFault  0x40000000 0x00000082 0x00000000 0x000002c8 0x000002c8 0x00000003",
    "5    ESC_OFF   HardFault  0x40000000 0x00010000 0x00000000 0x000002ec 0x000002ec 0x00000003",
    "6    ESC_PRIO  HardFault  0x40000000 0x00010000 0x00000000 0x000002f0 0x000002f0 0x00000003",
};
typedef struct { unsigned step; char name[16], handler[16]; unsigned hfsr, cfsr, addr, pc, epc, out; } row_t;
static int parse_row(const char *s, row_t *r) {
    return sscanf(s, "%u %15s %15s %x %x %x %x %x %x", &r->step, r->name, r->handler, &r->hfsr, &r->cfsr,
                  &r->addr, &r->pc, &r->epc, &r->out) == 9;
}
static int exc_of(const char *h) {
    if (!strcmp(h, "none")) return EXC_NONE;
    if (!strcmp(h, "HardFault")) return EXC_HARD;
    if (!strcmp(h, "MemManage")) return EXC_MEM;
    if (!strcmp(h, "BusFault")) return EXC_BUS;
    if (!strcmp(h, "UsageFault")) return EXC_USAGE;
    return -1;
}
/* 각 단계가 실제로 실행된 조건 (solution.c 의 inject() 를 보고 적은 것) */
static route_t predict(unsigned step, int solution) {
    const uint32_t all = SHCSR_MEMFAULTENA | SHCSR_BUSFAULTENA | SHCSR_USGFAULTENA;
    uint32_t shcsr = solution ? all : 0u;
    uint32_t ccr = solution ? (CCR_RESET | CCR_DIV_0_TRP | CCR_UNALIGN_TRP) : CCR_RESET;
    static const fault_kind_t kind[7] = { K_DIV0, K_UNALIGNED_LDR, K_BUS_PRECISE, K_UNDEF, K_MPU, K_UNDEF, K_UNDEF };
    if (step == 5) shcsr &= ~SHCSR_USGFAULTENA;                     /* ESC_OFF */
    int cur = (step == 6) ? 0 : PRIO_THREAD;                        /* ESC_PRIO: 우선순위 0 IRQ 안 */
    return fault_route(kind[step], shcsr, ccr, cur, 0);
}
static void ex5(void) {
    static const char *const want_names[7] = { "DIVBYZERO", "UNALIGNED", "PRECISERR|BFARVALID", "UNDEFINSTR",
                                               "DACCVIOL|MMARVALID", "UNDEFINSTR", "UNDEFINSTR" };
    int rows_ok = 0;
    for (int which = 0; which < 2; ++which) {
        const char *const *rows = which ? k_solution_rows : k_starter_rows;
        for (unsigned i = 0; i < 7u; ++i) {
            row_t r;
            char names[64];
            if (!parse_row(rows[i], &r)) continue;
            route_t p = predict(r.step, which);
            int ok = r.step == i && exc_of(r.handler) == p.exc && ((r.hfsr >> 30) & 1u) == (unsigned)p.forced;
            if (which && (cfsr_describe(r.cfsr, names, sizeof names) < 0 || strcmp(names, want_names[i]))) ok = 0;
            if (which && (r.pc != r.epc || r.out != 0u)) ok = 0;          /* 정답: 정확한 PC, 출력 안전 */
            if (!which && r.out != 3u) ok = 0;                            /* 시작 코드: 출력이 켜진 채 */
            rows_ok += ok;
        }
    }
    printf("  ex5: 실측 14 줄 중 예측과 일치 %d\n", rows_ok);
    CHECK(rows_ok == 14);
}

int main(void) {
    ex1(); ex2(); ex3(); ex4(); ex5();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
