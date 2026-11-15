/* ENV: qemu */
/* ARM-M13 starter.c — 베어메탈 AArch64 (QEMU virt, cortex-a53) 펌웨어 테스트 + ESR 디코더 + 예외 처리 정책.
 * 빌드: run_qemu.sh [starter|solution]  →  <v>.c + <v>.S + shared_start.S + shared_rt.c (+ arm_m13.ld)
 * 실행 2 번: (1) -M virt → EL1 로 부팅, (2) -M virt,virtualization=on → EL2 로 부팅해서 EL1 로 내려옴.
 * 출력: PASS/FAIL 줄, "N/M passed", 세미호스팅 SYS_EXIT 코드 (0 = 전부 통과, 1 = 실패, 3 = 예외 폭주, 4 = 예상 밖 예외).
 */
#include <stddef.h>
#include <stdint.h>

/* ---- shared_rt.c / shared_start.S ---- */
void sh_puts(const char *s);
void sh_hex(uint64_t v);
void sh_dec(uint64_t v);
void sh_exit(uint32_t code) __attribute__((noreturn));
void check(int ok, const char *expr, int line);
int  finish(void);
extern volatile uint64_t g_boot_el;
#define CHECK(e) check((e) ? 1 : 0, #e, __LINE__)

/* ---- <v>.S ---- */
uint64_t get_current_el(void);
void     set_vbar_el1(const void *table);
uint64_t read_vbar_el1(void);
uint64_t read_daif(void);
uint64_t do_svc42(uint64_t x);
uint64_t do_brk(void);
uint64_t do_udf(void);
uint64_t do_hvc(uint64_t x0);
uint64_t load64(const void *p);
void     store64(void *p, uint64_t v);
uint64_t run_el0(uint64_t (*fn)(uint64_t), uint64_t arg, void *el0_sp_top);
uint64_t el0_svc_mul10(uint64_t x);
void     el0_exit(uint64_t code) __attribute__((noreturn));
uint64_t el0_read_currentel(void);
extern const uint32_t vectors_el1[], svc42_insn[], brk_insn[], udf_insn[], hvc_insn[];
extern const uint32_t load64_insn[], store64_insn[], el0_currentel_insn[];
volatile uint64_t g_hcr_el2_before, g_hcr_el2_after;          /* el2_to_el1 이 기록 */

#define ADDR(p) ((uint64_t)(uintptr_t)(p))

/* ---- 예외 프레임: exc_common(.S) 이 스택에 만든 것과 같은 배치 ---- */
struct exc_frame {
    uint64_t x[19];          /* x0..x18 (caller-saved: C 를 부르려면 이것만 지키면 된다) */
    uint64_t fp, lr;         /* x29, x30 */
    uint64_t elr, spsr, esr, far;
    uint64_t kind;           /* 벡터 엔트리 번호 0..15 = (오프셋 / 0x80) */
    uint64_t sp_el0;
};
_Static_assert(sizeof(struct exc_frame) == 216, "frame size (FRAME = 224 in .S)");
_Static_assert(offsetof(struct exc_frame, elr) == 168 && offsetof(struct exc_frame, sp_el0) == 208, "frame layout");

/* ---- ESR_EL1 디코딩 ---- */
#define EC_UNKNOWN   0x00u
#define EC_SVC64     0x15u
#define EC_HVC64     0x16u
#define EC_SMC64     0x17u
#define EC_SYSREG    0x18u
#define EC_IABT_LOW  0x20u
#define EC_IABT_CUR  0x21u
#define EC_PC_ALIGN  0x22u
#define EC_DABT_LOW  0x24u
#define EC_DABT_CUR  0x25u
#define EC_SP_ALIGN  0x26u
#define EC_SERROR    0x2Fu
#define EC_BRK64     0x3Cu

struct esr_info {
    uint32_t ec, il, iss;    /* [31:26], [25], [24:0] */
    uint32_t imm16;          /* SVC/HVC/SMC/BRK: ISS[15:0], 그 밖 0 */
    uint32_t is_abort;       /* EC 0x20/0x21/0x24/0x25 */
    uint32_t dfsc;           /* 어보트: ISS[5:0] (DFSC/IFSC) */
    uint32_t wnr;            /* 데이터 어보트: ISS[6] 1 = 쓰기 */
    uint32_t isv;            /* 데이터 어보트: ISS[24] 1 = SAS/SRT 유효 */
    uint32_t srt;            /* isv 일 때 ISS[20:16] = 전송 레지스터 번호 */
    uint32_t sas;            /* isv 일 때 ISS[23:22] = 접근 크기 log2(바이트) */
    const char *name;
};

static const char *ec_name(uint32_t ec) {
    switch (ec) {
    case EC_UNKNOWN:  return "UNKNOWN";
    case EC_SVC64:    return "SVC64";
    case EC_HVC64:    return "HVC64";
    case EC_SMC64:    return "SMC64";
    case EC_SYSREG:   return "SYSREG_TRAP";
    case EC_IABT_LOW: return "IABT_LOWER";
    case EC_IABT_CUR: return "IABT_CUR";
    case EC_PC_ALIGN: return "PC_ALIGN";
    case EC_DABT_LOW: return "DABT_LOWER";
    case EC_DABT_CUR: return "DABT_CUR";
    case EC_SP_ALIGN: return "SP_ALIGN";
    case EC_SERROR:   return "SERROR";
    case EC_BRK64:    return "BRK64";
    default:          return "OTHER";
    }
}

/* TODO(Part A): ESR 을 필드로 나눈다. 반환값 = EC */
uint32_t decode_esr(uint64_t esr, struct esr_info *o) {
    uint32_t v = (uint32_t)esr;                 /* ESR_EL1[63:32] 는 cortex-a53 에서 RES0 */
    (void)v;
    o->ec = o->il = o->iss = 0u;                /* TODO: EC [31:26], IL [25], ISS [24:0] */
    o->imm16 = 0u;                              /* TODO: SVC/HVC/SMC/BRK 이면 ISS[15:0] */
    o->is_abort = 0u;                           /* TODO */
    o->dfsc = o->wnr = o->isv = o->srt = o->sas = 0u;   /* TODO: 어보트 ISS 필드 (5. 풀이 표) */
    o->name = ec_name(o->ec);
    return o->ec;
}

/* ---- 핸들러가 남기는 기록 ---- */
struct exc_rec { uint64_t kind, esr, elr, spsr, far, sp_el0, x0_in; };
static volatile struct exc_rec g_rec[16];
static volatile unsigned g_nrec, g_exc_count;

/* exc_common(.S) 이 부른다. 0 = ERET 으로 복귀, 1 = EL0 프로그램 끝 (x[0] = exit code) */
int exc_handler(struct exc_frame *f) {
    if (++g_exc_count > 64u) {                  /* ELR 을 안 옮겨서 같은 명령이 계속 예외 → 끝없는 반복 방지 */
        sh_puts("exception storm: ELR="); sh_hex(f->elr); sh_puts(" ESR="); sh_hex(f->esr); sh_puts("\n");
        sh_exit(3);
    }
    volatile struct exc_rec *r = &g_rec[g_nrec++ & 15u];
    r->kind = f->kind; r->esr = f->esr; r->elr = f->elr; r->spsr = f->spsr;
    r->far = f->far; r->sp_el0 = f->sp_el0; r->x0_in = f->x[0];

    struct esr_info e;
    decode_esr(f->esr, &e);
    /* TODO(Part A/B): 예외 종류별 복귀 정책 — ELR 을 옮길지, 반환값(x0)을 바꿀지 */
    switch (e.ec) {
    /* TODO: case EC_SVC64 / EC_BRK64·EC_UNKNOWN / EC_DABT_CUR — 각각 ELR 을 어떻게? x[0] 은?
     *       (EL0 의 svc #0 = exit → return 1, svc #1 = x0 *= 10, EL1 의 svc #0x42 = x0 += 1) */
    default:
        sh_puts("unexpected exception: "); sh_puts(e.name); sh_puts(" ELR="); sh_hex(f->elr); sh_puts("\n");
        sh_exit(4);
    }
}

static void dump(const char *name, uint64_t v) { sh_puts(name); sh_hex(v); sh_puts("\n"); }

static const volatile struct exc_rec *last(void) { return &g_rec[(g_nrec - 1u) & 15u]; }

/* ---- EL0 에서 돌 C 함수: 시스템 콜 두 번 + 금지된 레지스터 읽기 → exit ---- */
static uint64_t g_el0_stack[512] __attribute__((aligned(16)));   /* 4 KB */
static volatile uint64_t g_el0_mul, g_el0_el;

static uint64_t el0_main(uint64_t arg) {
    g_el0_mul = el0_svc_mul10(arg);             /* svc #1 → EL1 이 x0 *= 10 */
    g_el0_el = el0_read_currentel();            /* 금지 → EL1 이 0xDEAD 로 바꿔 돌려준다 */
    el0_exit(arg + 1000u);                      /* svc #0 → run_el0 이 이 값을 돌려준다 */
}

int main(void) {
    sh_puts("ARM-M13 AArch64 exception levels (QEMU virt, cortex-a53)\n");
    uint64_t el = get_current_el();
    dump("  boot EL     = ", g_boot_el);
    dump("  CurrentEL   = ", el);
    dump("  DAIF        = ", read_daif());

    /* ---------- Part A: EL1 확인, 벡터 설치 ---------- */
    CHECK(el == 1u);                                           /* EL1 부팅이든 EL2 → EL1 이든 main 은 EL1 */
    CHECK(g_boot_el >= 1u);                                   /* 1, 2 (virtualization=on), 3 (secure=on, 연습 4) */
    CHECK((ADDR(vectors_el1) & 0x7FFu) == 0u);                 /* 2 KB 정렬 */
    /* A: decode_esr — 손으로 만든 인코딩 표 (예외 없이 순수 C) */
    static const struct { uint64_t esr; uint32_t ec, il, imm, dfsc, wnr, isv, srt; } tv[] = {
        { 0x56000042u, 0x15, 1, 0x42, 0, 0, 0, 0 },            /* svc #0x42 */
        { 0x5600FFFFu, 0x15, 1, 0xFFFF, 0, 0, 0, 0 },          /* svc #0xffff */
        { 0xF2000007u, 0x3C, 1, 7, 0, 0, 0, 0 },               /* brk #7 */
        { 0x5A000005u, 0x16, 1, 5, 0, 0, 0, 0 },               /* hvc #5 */
        { 0x02000000u, 0x00, 1, 0, 0, 0, 0, 0 },               /* unknown */
        { 0x96000021u, 0x25, 1, 0, 0x21, 0, 0, 0 },            /* alignment fault, 읽기 */
        { 0x96000050u, 0x25, 1, 0, 0x10, 1, 0, 0 },            /* external abort, 쓰기 */
        { 0x93C10045u, 0x24, 1, 0, 0x05, 1, 1, 1 },            /* 하위 EL, ISV: SAS=3 SRT=1 쓰기, L1 translation */
        { 0x62000000u, 0x18, 1, 0, 0, 0, 0, 0 },               /* sysreg trap (ISS 0) */
        { 0x0u,        0x00, 0, 0, 0, 0, 0, 0 },
    };
    unsigned ok = 0;
    for (unsigned i = 0; i < sizeof tv / sizeof tv[0]; i++) {
        struct esr_info d;
        uint32_t ec = decode_esr(tv[i].esr, &d);
        ok += (ec == tv[i].ec && d.ec == tv[i].ec && d.il == tv[i].il && d.imm16 == tv[i].imm &&
               d.dfsc == tv[i].dfsc && d.wnr == tv[i].wnr && d.isv == tv[i].isv && d.srt == tv[i].srt);
    }
    CHECK(ok == sizeof tv / sizeof tv[0]);
    struct esr_info d7;
    decode_esr(0x93C10045u, &d7);
    CHECK(d7.sas == 3u && d7.is_abort == 1u && d7.iss == 0x1C10045u);

    set_vbar_el1(vectors_el1);
    dump("  VBAR_EL1    = ", read_vbar_el1());
    CHECK(read_vbar_el1() == ADDR(vectors_el1));
    int ready = (el == 1u && read_vbar_el1() == ADDR(vectors_el1));
    if (!ready) {                                               /* 안전장치: 벡터가 없으면 예외를 일으키지 않는다 */
        sh_puts("  (EL1 + VBAR_EL1 not ready: exception tests skipped)\n");
        CHECK(ready);
        return finish();
    }

    /* A: svc #0x42 */
    uint64_t r = do_svc42(5u);
    const volatile struct exc_rec *s = last();
    struct esr_info e;
    decode_esr(s->esr, &e);
    dump("  SVC  ESR_EL1  = ", s->esr);
    dump("  SVC  ELR_EL1  = ", s->elr);
    dump("  SVC  SPSR_EL1 = ", s->spsr);
    dump("  SVC  vector   = ", s->kind * 0x80u);
    CHECK(r == 5u + 1u + 100u);                                /* 핸들러 +1, svc 다음 add +100 */
    CHECK(e.ec == EC_SVC64 && e.il == 1u && e.imm16 == 0x42u);
    CHECK(s->esr == 0x56000042u);                              /* EC 0x15 << 26 | IL | imm16 */
    CHECK(s->elr == ADDR(svc42_insn) + 4u);                    /* SVC: ELR = 다음 명령 */
    CHECK(s->kind == 4u);                                      /* 0x200: 현재 EL, SP_ELx, Sync */
    CHECK((s->spsr & 0xFu) == 0x5u);                           /* M = EL1h */
    CHECK(((s->spsr >> 6) & 0xFu) == 0xFu);                    /* 예외 직전 DAIF = 1111 (부팅 상태) */

    /* ---------- Part B: ELR 규칙이 다른 예외들 ---------- */
    r = do_brk();
    s = last();
    dump("  BRK  ESR_EL1  = ", s->esr);
    CHECK(s->esr == 0xF2000007u);                              /* EC 0x3C, imm16 = 7 */
    CHECK(s->elr == ADDR(brk_insn));                           /* BRK: ELR = brk 자신 */
    CHECK(r == 0xDEADu);                                       /* +4 해서 ret 까지 왔다 */

    r = do_udf();
    s = last();
    dump("  UDF  ESR_EL1  = ", s->esr);
    CHECK(s->esr == 0x02000000u && s->elr == ADDR(udf_insn) && r == 0xDEADu);

    if (g_boot_el == 1u) {                                     /* EL2 가 없을 때만 (있으면 hvc 는 VBAR_EL2 로 간다) */
        unsigned n = g_exc_count;
        r = do_hvc(0x84000000u);                               /* PSCI_VERSION */
        dump("  HVC PSCI_VERSION -> x0 = ", r);
        dump("  HVC exceptions taken    = ", g_exc_count - n);
        CHECK(r == 0x10001u);                                  /* PSCI 1.1 — QEMU 가 펌웨어 역할 */
        r = do_hvc(3u);                                        /* 없는 함수 ID */
        dump("  HVC fid 3        -> x0 = ", r);
        CHECK(r == 0xFFFFFFFFFFFFFFFFu);                       /* PSCI NOT_SUPPORTED = -1 */
        CHECK(g_exc_count == n);                               /* EL1 에 예외가 안 왔다 */
    } else {
        sh_puts("  HVC skipped (boot EL != 1: EL2/EL3 present, 9. 연습 4)\n");
    }

    uint64_t bad = 0x50000000u;                                /* RAM(0x40000000 + 128 MB) 너머 = 아무것도 없음 */
    r = load64((const void *)(uintptr_t)bad);
    s = last();
    decode_esr(s->esr, &e);
    dump("  DABT ld ESR   = ", s->esr);
    dump("  DABT ld FAR   = ", s->far);
    CHECK(e.ec == EC_DABT_CUR && e.wnr == 0u && s->far == bad);
    CHECK(e.dfsc == 0x10u);                                    /* 동기 외부 어보트 (QEMU 실측) */
    CHECK(s->elr == ADDR(load64_insn) && r == 0xBADu);

    store64((void *)(uintptr_t)(bad + 8u), 1u);
    s = last();
    decode_esr(s->esr, &e);
    dump("  DABT st ESR   = ", s->esr);
    CHECK(e.ec == EC_DABT_CUR && e.wnr == 1u && s->far == bad + 8u && s->elr == ADDR(store64_insn));

    static uint64_t buf[4] __attribute__((aligned(16)));
    buf[0] = 0x1122334455667788u; buf[1] = 0x99aabbccddeeff00u;
    uint64_t mis = ADDR(buf) + 1u;                              /* MMU off → Device 메모리 → 비정렬 금지 */
    r = load64((const void *)(uintptr_t)mis);
    s = last();
    decode_esr(s->esr, &e);
    dump("  ALGN ld ESR   = ", s->esr);
    CHECK(e.ec == EC_DABT_CUR && e.dfsc == 0x21u && s->far == mis && r == 0xBADu);
    CHECK(load64(buf) == 0x1122334455667788u);                 /* 정렬된 읽기는 정상 */

    /* ---------- Part C: (EL2 →) EL1 → EL0 → svc → EL1 ---------- */
    if (g_boot_el == 2u) {
        dump("  HCR_EL2 before = ", g_hcr_el2_before);
        dump("  HCR_EL2 after  = ", g_hcr_el2_after);
        CHECK((g_hcr_el2_after >> 31) & 1u);                   /* RW = 1 이어야 EL1 이 AArch64 */
    }
    unsigned n0 = g_nrec;
    r = run_el0(el0_main, 7u, &g_el0_stack[512]);
    dump("  EL0 exit code  = ", r);
    CHECK(r == 1007u);
    CHECK(g_el0_mul == 70u);                                   /* EL0 → svc #1 → EL1 이 계산 */
    CHECK(g_el0_el == 0xDEADu);                                /* EL0 에서 CurrentEL 은 못 읽는다 */
    CHECK(g_nrec - n0 == 3u);                                  /* svc #1, mrs, svc #0 */
    const volatile struct exc_rec *a = &g_rec[n0 & 15u], *b = &g_rec[(n0 + 1u) & 15u],
                                  *c = &g_rec[(n0 + 2u) & 15u];
    dump("  EL0 svc  ESR   = ", a->esr);
    dump("  EL0 svc  SPSR  = ", a->spsr);
    dump("  EL0 svc  vector= ", a->kind * 0x80u);
    dump("  EL0 mrs  ESR   = ", b->esr);
    dump("  EL0 SP_EL0     = ", a->sp_el0);
    CHECK(a->kind == 8u && c->kind == 8u && b->kind == 8u);    /* 0x400: 하위 EL, AArch64, Sync */
    CHECK(a->esr == 0x56000001u && c->esr == 0x56000000u);
    CHECK((a->spsr & 0xFu) == 0u);                             /* M = EL0t */
    CHECK(b->esr == 0x02000000u && b->elr == ADDR(el0_currentel_insn));
    CHECK(a->sp_el0 < ADDR(&g_el0_stack[512]) && a->sp_el0 >= ADDR(g_el0_stack));
    CHECK(get_current_el() == 1u);                             /* 다시 EL1 */

    dump("  exceptions taken = ", g_exc_count);
    return finish();
}
