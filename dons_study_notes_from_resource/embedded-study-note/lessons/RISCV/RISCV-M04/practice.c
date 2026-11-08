/* RISCV-M04 추가 연습 — 한 번에 검증
 *
 * solution.c 의 ISS(인코더 · 미니 어셈블러 · 2-hart 머신 · 스케줄러)를 그대로 복사하고, exec_amo 만
 * AMO 9종 전부(amoswap/add/xor/and/or/min/max/minu/maxu) + "QEMU 식 SC" 옵션으로 넓혔다.
 *   연습 1  이벤트 플래그: amoor.w 로 비트 세우기, amoand.w 로 지우기 (일반 lw/or/sw 는 비트를 잃는다)
 *   연습 2  원자 최대값: LR/SC 루프 vs amomaxu.w — 같은 결과, 명령 수는 다르다
 *   연습 3  예약 집합 입도: 이웃 워드 스토어가 SC 를 깨는가 (워드 vs 64B) — "예약 false sharing"
 *   연습 4  ABA: 스펙 SC(어떤 스토어든 실패) vs QEMU 식 SC(값만 비교) — demo_qemu_rv64.c 실측과 같은 결과
 * 새 인코딩(amoor/amoand/amomaxu)도 VM 의 clang 21 + llvm-objdump 값과 비교한다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= 공통: 레지스터 · opcode · 인코더 ======================= */
enum { ZERO = 0, RA = 1, T0 = 5, T1 = 6, T2 = 7, S0 = 8, S1 = 9, A0 = 10, A1 = 11, A2 = 12, A3 = 13,
       S2 = 18, S3 = 19, S4 = 20, S5 = 21, S6 = 22, T3 = 28, T4 = 29, T5 = 30 };
enum { OP_LOAD = 0x03, OP_FENCE = 0x0F, OP_IMM = 0x13, OP_AUIPC = 0x17, OP_STORE = 0x23, OP_AMO = 0x2F,
       OP_OP = 0x33, OP_LUI = 0x37, OP_BRANCH = 0x63, OP_JALR = 0x67, OP_JAL = 0x6F, OP_SYSTEM = 0x73 };
enum { F5_AMOADD = 0x00, F5_AMOSWAP = 0x01, F5_LR = 0x02, F5_SC = 0x03, F5_AMOXOR = 0x04, F5_AMOOR = 0x08,
       F5_AMOAND = 0x0C, F5_AMOMIN = 0x10, F5_AMOMAX = 0x14, F5_AMOMINU = 0x18, F5_AMOMAXU = 0x1C };   /* inst[31:27] */
enum { AQ = 2, RL = 1, AQRL = 3 };                                          /* inst[26]=aq, inst[25]=rl */
enum { FN_W = 1, FN_R = 2, FN_O = 4, FN_I = 8 };                            /* fence pred/succ 비트 */

static uint32_t enc_r(uint32_t f7, int rs2, int rs1, uint32_t f3, int rd, uint32_t op) {
    return f7 << 25 | (uint32_t)rs2 << 20 | (uint32_t)rs1 << 15 | f3 << 12 | (uint32_t)rd << 7 | op;
}
static uint32_t enc_i(int32_t imm, int rs1, uint32_t f3, int rd, uint32_t op) {
    return ((uint32_t)imm & 0xFFFu) << 20 | (uint32_t)rs1 << 15 | f3 << 12 | (uint32_t)rd << 7 | op;
}
static uint32_t enc_s(int32_t imm, int rs2, int rs1, uint32_t f3, uint32_t op) {
    uint32_t u = (uint32_t)imm;
    return (u >> 5 & 0x7Fu) << 25 | (uint32_t)rs2 << 20 | (uint32_t)rs1 << 15 | f3 << 12 | (u & 0x1Fu) << 7 | op;
}
static uint32_t enc_b(int32_t off, int rs2, int rs1, uint32_t f3) {          /* off: 바이트, 짝수 */
    uint32_t u = (uint32_t)off;
    return (u >> 12 & 1u) << 31 | (u >> 5 & 0x3Fu) << 25 | (uint32_t)rs2 << 20 | (uint32_t)rs1 << 15 |
           f3 << 12 | (u >> 1 & 0xFu) << 8 | (u >> 11 & 1u) << 7 | OP_BRANCH;
}
static uint32_t enc_u(uint32_t imm20, int rd, uint32_t op) { return (imm20 & 0xFFFFFu) << 12 | (uint32_t)rd << 7 | op; }
static uint32_t enc_j(int32_t off, int rd) {
    uint32_t u = (uint32_t)off;
    return (u >> 20 & 1u) << 31 | (u >> 1 & 0x3FFu) << 21 | (u >> 11 & 1u) << 20 | (u >> 12 & 0xFFu) << 12 |
           (uint32_t)rd << 7 | OP_JAL;
}

/* Part A-1: AMO 포맷 = R 포맷의 funct7 자리를 funct5 | aq | rl 로 쪼갠 것. funct3 = 010 (.w), opcode 0101111.
 *   31..27 funct5 | 26 aq | 25 rl | 24..20 rs2 | 19..15 rs1 | 14..12 funct3 | 11..7 rd | 6..0 opcode   */
static uint32_t enc_amo(uint32_t f5, int aqrl, int rs2, int rs1, int rd) {
    return f5 << 27 | ((uint32_t)aqrl & 3u) << 25 | (uint32_t)rs2 << 20 | (uint32_t)rs1 << 15 |
           2u << 12 | (uint32_t)rd << 7 | OP_AMO;
}

/* 디코드 도우미 (ISS 와 검사기가 같이 쓴다) */
static int32_t imm_i(uint32_t w) { return (int32_t)w >> 20; }
static int32_t imm_s(uint32_t w) { return ((int32_t)w >> 25) * 32 | (int32_t)(w >> 7 & 0x1Fu); }
static int32_t imm_b(uint32_t w) {
    uint32_t u = (w >> 31) << 12 | (w >> 7 & 1u) << 11 | (w >> 25 & 0x3Fu) << 5 | (w >> 8 & 0xFu) << 1;
    return (int32_t)(u << 19) >> 19;                                    /* 13비트 부호 확장 */
}
static int32_t imm_j(uint32_t w) {
    uint32_t u = (w >> 31) << 20 | (w >> 12 & 0xFFu) << 12 | (w >> 20 & 1u) << 11 | (w >> 21 & 0x3FFu) << 1;
    return (int32_t)(u << 11) >> 11;                                    /* 21비트 부호 확장 */
}

/* ======================= 미니 어셈블러 (기계어 프로그램을 C 로 조립) ======================= */
#define PROG_MAX 128
typedef struct { uint32_t w[PROG_MAX]; int n; } prog_t;
static void emit(prog_t *p, uint32_t w) { if (p->n < PROG_MAX) p->w[p->n++] = w; }
static int here(const prog_t *p) { return p->n; }

static void LUI(prog_t *p, int rd, uint32_t hi20) { emit(p, enc_u(hi20, rd, OP_LUI)); }
static void ADDI(prog_t *p, int rd, int rs1, int32_t imm) { emit(p, enc_i(imm, rs1, 0, rd, OP_IMM)); }
static void SLLI(prog_t *p, int rd, int rs1, int sh) { emit(p, enc_i(sh, rs1, 1, rd, OP_IMM)); }
static void LW(prog_t *p, int rd, int32_t off, int rs1) { emit(p, enc_i(off, rs1, 2, rd, OP_LOAD)); }
static void SW(prog_t *p, int rs2, int32_t off, int rs1) { emit(p, enc_s(off, rs2, rs1, 2, OP_STORE)); }
static void EBREAK(prog_t *p) { emit(p, 0x00100073u); }
/* 분기/점프: target >= 0 이면 그 명령 인덱스로, -1 이면 자리만 잡고(오프셋 0) 나중에 fix() */
static int BR(prog_t *p, uint32_t f3, int rs1, int rs2, int target) {
    int at = here(p);
    emit(p, enc_b(target < 0 ? 0 : (target - at) * 4, rs2, rs1, f3));
    return at;
}
#define BEQ(p, a, b, t) BR(p, 0, a, b, t)
#define BNE(p, a, b, t) BR(p, 1, a, b, t)
static void fix(prog_t *p, int at) {                       /* at 의 분기/점프가 지금 위치(here)를 가리키게 */
    uint32_t w = p->w[at];
    int32_t off = (here(p) - at) * 4;
    if ((w & 0x7Fu) == OP_JAL) p->w[at] = enc_j(off, (int)(w >> 7 & 31u));
    else p->w[at] = enc_b(off, (int)(w >> 20 & 31u), (int)(w >> 15 & 31u), w >> 12 & 7u);
}
static void LR_W(prog_t *p, int rd, int rs1, int o) { emit(p, enc_amo(F5_LR, o, ZERO, rs1, rd)); }
static void SC_W(prog_t *p, int rd, int rs2, int rs1, int o) { emit(p, enc_amo(F5_SC, o, rs2, rs1, rd)); }

/* ======================= ISS: 메모리 · hart · 머신 ======================= */
#define MEM_SIZE 0x2000u
#define NHART    2
enum { RUN = 0, HALT_EBREAK = 1, HALT_TRAP = 2 };
enum { CAUSE_IMISALIGN = 0, CAUSE_IFAULT = 1, CAUSE_ILLEGAL = 2, CAUSE_LMISALIGN = 4, CAUSE_LFAULT = 5,
       CAUSE_SMISALIGN = 6, CAUSE_SFAULT = 7 };   /* mcause 번호 (privileged spec) */

typedef struct {
    uint32_t x[32], pc;
    int state;                 /* RUN / HALT_EBREAK / HALT_TRAP (트랩 핸들러는 없다: 트랩 = 정지) */
    uint32_t cause;
    bool res_valid;            /* 예약(reservation) 유효? */
    uint32_t res_base;         /* 예약 집합 = [res_base, res_base + 2^res_shift) */
    uint32_t res_addr, res_val; /* 연습 4: QEMU 식 SC 가 쓰는 LR 주소·값 */
    bool res_own;              /* 연습 4: QEMU 의 load_res — 남의 스토어로는 안 지워지고 내 SC 만 지운다 */
    bool in_lrsc;              /* LR 이후 SC 전? (제약 루프 감시용) */
    bool lrsc_bad;             /* LR..SC 사이에 제약 위반 명령이 실행됐나 */
    int lrsc_len;
    uint32_t sc_ok, sc_fail, instret;
} hart_t;

typedef struct {
    uint8_t mem[MEM_SIZE];
    hart_t h[NHART];
    unsigned res_shift;        /* 예약 집합 크기 = 2^res_shift 바이트. 2 = 워드, 6 = 64B 캐시 라인 */
    unsigned spurious_pct;     /* SC 가짜 실패 주입 확률 (0..100) */
    bool fail_unconstrained;   /* true 면 "제약 안 된 LR/SC 는 SC 를 항상 실패" 구현 (스펙이 허용) */
    bool sc_value_compare;     /* 연습 4: QEMU TCG 처럼 SC = "LR 때 값과 같으면 cmpxchg" (예약 무효화 무시) */
    uint32_t rng;
} machine_t;

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static bool mem_ok(uint32_t a) { return a <= MEM_SIZE - 4u; }
static uint32_t ld32(const machine_t *m, uint32_t a) {
    return (uint32_t)m->mem[a] | (uint32_t)m->mem[a + 1] << 8 | (uint32_t)m->mem[a + 2] << 16 | (uint32_t)m->mem[a + 3] << 24;
}
static void st32raw(machine_t *m, uint32_t a, uint32_t v) {
    m->mem[a] = (uint8_t)v; m->mem[a + 1] = (uint8_t)(v >> 8); m->mem[a + 2] = (uint8_t)(v >> 16); m->mem[a + 3] = (uint8_t)(v >> 24);
}
static void trap(hart_t *h, uint32_t cause) { h->state = HALT_TRAP; h->cause = cause; }
static void wr(hart_t *h, uint32_t rd, uint32_t v) { if (rd) h->x[rd] = v; }
static uint32_t res_block(const machine_t *m, uint32_t a) { return a & ~((1u << m->res_shift) - 1u); }

/* 스토어 한 번(sw, AMO, 성공한 SC)의 "가시화": 다른 hart 의 예약 집합에 닿으면 그 예약을 깬다. */
static void notify_store(machine_t *m, int self, uint32_t a) {
    for (int j = 0; j < NHART; j++)
        if (j != self && m->h[j].res_valid && m->h[j].res_base == res_block(m, a)) m->h[j].res_valid = false;
}
static void store32(machine_t *m, int self, uint32_t a, uint32_t v) { st32raw(m, a, v); notify_store(m, self, a); }

/* SC 를 예약과 무관하게 실패시킬 "구현 사정": 가짜 실패 주입, 제약 안 된 시퀀스 거부 (스펙 8.3/14.3 이 허용) */
static bool sc_forced_fail(machine_t *m, const hart_t *h) {
    if (m->spurious_pct && xs32(&m->rng) % 100u < m->spurious_pct) return true;
    return m->fail_unconstrained && h->in_lrsc && h->lrsc_bad;
}

/* A 확장 실행 — solution.c 의 exec_amo + 연습 1·2 용 AMO 전부 + 연습 4 용 QEMU 식 SC */
static void exec_amo(machine_t *m, int self, uint32_t w) {
    hart_t *h = &m->h[self];
    uint32_t f5 = w >> 27, rd = w >> 7 & 31u, rs1 = w >> 15 & 31u, rs2 = w >> 20 & 31u;
    uint32_t a = h->x[rs1], src = h->x[rs2];
    if ((w >> 12 & 7u) != 2u) { trap(h, CAUSE_ILLEGAL); return; }
    if (f5 == F5_LR) {
        if (rs2 != 0) { trap(h, CAUSE_ILLEGAL); return; }
        if (a & 3u) { trap(h, CAUSE_LMISALIGN); return; }
        if (!mem_ok(a)) { trap(h, CAUSE_LFAULT); return; }
        h->res_valid = true; h->res_base = res_block(m, a);
        h->res_addr = a; h->res_val = ld32(m, a); h->res_own = true;
        h->in_lrsc = true; h->lrsc_bad = false; h->lrsc_len = 0;
        wr(h, rd, h->res_val);
        return;
    }
    if (a & 3u) { trap(h, CAUSE_SMISALIGN); return; }
    if (!mem_ok(a)) { trap(h, CAUSE_SFAULT); return; }
    if (f5 == F5_SC) {
        bool ok = h->res_valid && h->res_base == res_block(m, a);
        if (m->sc_value_compare)                                           /* QEMU: 주소 같고 값 같으면 성공 */
            ok = h->res_own && h->res_addr == a && ld32(m, a) == h->res_val;
        if (sc_forced_fail(m, h)) ok = false;
        h->res_valid = false; h->res_own = false; h->in_lrsc = false;
        if (ok) { store32(m, self, a, src); h->sc_ok++; wr(h, rd, 0); }
        else { h->sc_fail++; wr(h, rd, 1); }
        return;
    }
    uint32_t old = ld32(m, a), nv;
    switch (f5) {
    case F5_AMOADD:  nv = old + src; break;
    case F5_AMOSWAP: nv = src; break;
    case F5_AMOXOR:  nv = old ^ src; break;
    case F5_AMOOR:   nv = old | src; break;
    case F5_AMOAND:  nv = old & src; break;
    case F5_AMOMIN:  nv = (int32_t)old < (int32_t)src ? old : src; break;
    case F5_AMOMAX:  nv = (int32_t)old > (int32_t)src ? old : src; break;
    case F5_AMOMINU: nv = old < src ? old : src; break;
    case F5_AMOMAXU: nv = old > src ? old : src; break;
    default: trap(h, CAUSE_ILLEGAL); return;
    }
    store32(m, self, a, nv);
    wr(h, rd, old);
}

/* 명령 1개 실행 */
static void step(machine_t *m, int self) {
    hart_t *h = &m->h[self];
    if (h->state != RUN) return;
    uint32_t pc = h->pc;
    if (pc & 3u) { trap(h, CAUSE_IMISALIGN); return; }
    if (!mem_ok(pc)) { trap(h, CAUSE_IFAULT); return; }
    uint32_t w = ld32(m, pc), op = w & 0x7Fu, rd = w >> 7 & 31u, f3 = w >> 12 & 7u, f7 = w >> 25;
    uint32_t a = h->x[w >> 15 & 31u], b = h->x[w >> 20 & 31u], next = pc + 4u, addr;
    bool track = h->in_lrsc && !(op == OP_AMO && (w >> 27) == F5_SC);     /* LR..SC 사이 명령인가 */
    h->instret++;
    switch (op) {
    case OP_LUI:   wr(h, rd, w & 0xFFFFF000u); break;
    case OP_AUIPC: wr(h, rd, pc + (w & 0xFFFFF000u)); break;
    case OP_JAL:   wr(h, rd, pc + 4u); next = pc + (uint32_t)imm_j(w); break;
    case OP_JALR:
        if (f3) { trap(h, CAUSE_ILLEGAL); return; }
        next = (a + (uint32_t)imm_i(w)) & ~1u; wr(h, rd, pc + 4u); break;
    case OP_BRANCH: {
        bool t;
        switch (f3) {
        case 0: t = a == b; break;                       case 1: t = a != b; break;
        case 4: t = (int32_t)a < (int32_t)b; break;      case 5: t = (int32_t)a >= (int32_t)b; break;
        case 6: t = a < b; break;                        case 7: t = a >= b; break;
        default: trap(h, CAUSE_ILLEGAL); return;
        }
        if (t) next = pc + (uint32_t)imm_b(w);
        break;
    }
    case OP_LOAD:
        if (f3 != 2) { trap(h, CAUSE_ILLEGAL); return; }                   /* lw 만 */
        addr = a + (uint32_t)imm_i(w);
        if (addr & 3u) { trap(h, CAUSE_LMISALIGN); return; }
        if (!mem_ok(addr)) { trap(h, CAUSE_LFAULT); return; }
        wr(h, rd, ld32(m, addr)); break;
    case OP_STORE:
        if (f3 != 2) { trap(h, CAUSE_ILLEGAL); return; }                   /* sw 만 */
        addr = a + (uint32_t)imm_s(w);
        if (addr & 3u) { trap(h, CAUSE_SMISALIGN); return; }
        if (!mem_ok(addr)) { trap(h, CAUSE_SFAULT); return; }
        store32(m, self, addr, b); break;
    case OP_IMM: {
        uint32_t im = (uint32_t)imm_i(w), sh = w >> 20 & 31u, r;
        switch (f3) {
        case 0: r = a + im; break;                       case 2: r = (int32_t)a < (int32_t)im; break;
        case 3: r = a < im; break;                       case 4: r = a ^ im; break;
        case 6: r = a | im; break;                       case 7: r = a & im; break;
        case 1: if (f7) { trap(h, CAUSE_ILLEGAL); return; } r = a << sh; break;
        default: /* 5 */
            if (f7 == 0) r = a >> sh;
            else if (f7 == 0x20) r = (uint32_t)((int32_t)a >> sh);
            else { trap(h, CAUSE_ILLEGAL); return; }
        }
        wr(h, rd, r); break;
    }
    case OP_OP: {
        uint32_t r, sh = b & 31u;
        if (f7 == 0x20 && f3 == 0) r = a - b;
        else if (f7 == 0x20 && f3 == 5) r = (uint32_t)((int32_t)a >> sh);
        else if (f7 != 0) { trap(h, CAUSE_ILLEGAL); return; }               /* M 확장(f7=1) 없음: rv32ia */
        else switch (f3) {
            case 0: r = a + b; break;                    case 1: r = a << sh; break;
            case 2: r = (int32_t)a < (int32_t)b; break;  case 3: r = a < b; break;
            case 4: r = a ^ b; break;                    case 5: r = a >> sh; break;
            case 6: r = a | b; break;                    default: r = a & b; break;
        }
        wr(h, rd, r); break;
    }
    case OP_FENCE:
        if (f3 != 0) { trap(h, CAUSE_ILLEGAL); return; }
        break;                                           /* SC 인터리빙 모델: 이미 모든 접근이 순서대로 → no-op */
    case OP_SYSTEM:
        if (w == 0x00100073u) { h->state = HALT_EBREAK; return; }          /* ebreak = 이 hart 끝 */
        trap(h, CAUSE_ILLEGAL); return;
    case OP_AMO:
        exec_amo(m, self, w);
        if (h->state != RUN) return;
        break;
    default:
        trap(h, CAUSE_ILLEGAL); return;                  /* 0x00000000 도 여기 (스펙상 항상 illegal) */
    }
    if (track) {                                         /* 제약된 LR/SC 루프 규칙: 동적 감시 */
        h->lrsc_len++;
        if (op == OP_LOAD || op == OP_STORE || op == OP_JALR || op == OP_FENCE || op == OP_SYSTEM ||
            op == OP_AMO || next <= pc || h->lrsc_len > 16)
            h->lrsc_bad = true;
    }
    h->pc = next;
}

/* ======================= 스케줄러 ======================= */
enum { SCHED_RR = 0, SCHED_RANDOM = 1 };
enum { RUN_OK = 0, RUN_LIMIT = 1, RUN_TRAP = 2 };

static void machine_init(machine_t *m, const prog_t *p, unsigned res_shift) {
    memset(m, 0, sizeof *m);
    m->res_shift = res_shift;
    m->rng = 0x9E3779B9u;
    for (int i = 0; i < p->n; i++) st32raw(m, (uint32_t)i * 4u, p->w[i]);    /* 코드는 0x0000 부터, 두 hart 공유 */
    for (int j = 0; j < NHART; j++) m->h[j].x[A0] = (uint32_t)j;            /* a0 = hartid (OpenSBI 부팅 관례) */
}

/* RR: hart 하나가 quantum 개 실행 후 교대. RANDOM: 시드로 hart 와 burst 길이(1..quantum)를 고른다.
 * max_steps 를 넘으면 RUN_LIMIT (livelock / 미완성 starter 가 멈추지 않게) */
static int run(machine_t *m, int sched, uint32_t seed, int quantum, long max_steps) {
    uint32_t s = seed ? seed : 1u;
    int cur = 0, left = quantum;
    for (long n = 0; n < max_steps; n++) {
        bool any = false;
        for (int j = 0; j < NHART; j++) any |= m->h[j].state == RUN;
        if (!any) break;
        if (sched == SCHED_RR) {
            if (left <= 0 || m->h[cur].state != RUN) { cur = (cur + 1) % NHART; left = quantum; }
            if (m->h[cur].state != RUN) cur = (cur + 1) % NHART;
        } else if (left <= 0 || m->h[cur].state != RUN) {
            cur = (int)(xs32(&s) % NHART);
            left = 1 + (int)(xs32(&s) % (uint32_t)quantum);
            if (m->h[cur].state != RUN) cur = (cur + 1) % NHART;
        }
        left--;
        step(m, cur);
    }
    for (int j = 0; j < NHART; j++) if (m->h[j].state == HALT_TRAP) return RUN_TRAP;
    for (int j = 0; j < NHART; j++) if (m->h[j].state == RUN) return RUN_LIMIT;
    return RUN_OK;
}

/* ======================= 연습용 도우미 ======================= */
static void OPR(prog_t *p, uint32_t f7, uint32_t f3, int rd, int rs1, int rs2) { emit(p, enc_r(f7, rs2, rs1, f3, rd, OP_OP)); }
static void OPI(prog_t *p, uint32_t f3, int rd, int rs1, int32_t imm) { emit(p, enc_i(imm, rs1, f3, rd, OP_IMM)); }
static void AMO(prog_t *p, uint32_t f5, int rd, int rs2, int rs1, int o) { emit(p, enc_amo(f5, o, rs2, rs1, rd)); }
enum { F3_SLL = 1, F3_XOR = 4, F3_SRL = 5, F3_OR = 6, F3_AND = 7 };

#define D_BASE  0x1000u
#define O_FLAGS 0x000
#define O_MAX   0x040
#define O_X     0x080      /* 연습 3: X 와 Y(= X+4) 는 같은 64B 블록 */
#define PITERS  100

static machine_t g_m;
static int run_on(const prog_t *p, unsigned shift, int sched, uint32_t seed) {
    machine_init(&g_m, p, shift);
    return run(&g_m, sched, seed, 6, 200000L);
}
static uint32_t data(uint32_t off) { return ld32(&g_m, D_BASE + off); }

/* 연습 1: hart h 는 비트 16h..16h+15 를 하나씩 세우고(amoor), 끝에 자기 절반의 짝수 비트를 지운다(amoand).
 * plain = true 면 같은 일을 lw / or(and) / sw 로 (원자 아님). 기대 최종값 0xAAAAAAAA. */
static void build_flags(prog_t *p, bool plain) {
    p->n = 0;
    ADDI(p, S0, A0, 0); LUI(p, S2, D_BASE >> 12); ADDI(p, A1, S2, O_FLAGS);
    ADDI(p, T0, ZERO, 1);
    SLLI(p, T1, S0, 4);                               /* t1 = 16 * hartid */
    OPR(p, 0, F3_SLL, T0, T0, T1);                    /* t0 = 1 << t1 */
    ADDI(p, S4, ZERO, 16);
    int top = here(p);
    if (plain) { LW(p, T2, 0, A1); OPR(p, 0, F3_OR, T2, T2, T0); SW(p, T2, 0, A1); }
    else AMO(p, F5_AMOOR, ZERO, T0, A1, 0);           /* amoor.w zero, t0, (a1) — 옛 값은 버림 */
    SLLI(p, T0, T0, 1);
    ADDI(p, S4, S4, -1); BNE(p, S4, ZERO, top);
    LUI(p, T0, 0x5); ADDI(p, T0, T0, 0x555);          /* t0 = 0x5555 */
    OPR(p, 0, F3_SLL, T0, T0, T1);
    OPI(p, F3_XOR, T0, T0, -1);                       /* t0 = ~(0x5555 << 16h) */
    if (plain) { LW(p, T2, 0, A1); OPR(p, 0, F3_AND, T2, T2, T0); SW(p, T2, 0, A1); }
    else AMO(p, F5_AMOAND, ZERO, T0, A1, RL);         /* amoand.w.rl: 앞의 접근을 먼저 보이게 (관례) */
    EBREAK(p);
}

/* 연습 2: 각 hart 가 xorshift32(시드 hartid+1) 값 PITERS 개를 "최대값" 에 제출. amo = true 면 amomaxu.w,
 * 아니면 LR/SC 루프 (cur >= v 이면 앞 방향 분기로 탈출 — 제약 루프 규칙 안). */
static void build_max(prog_t *p, bool amo) {
    p->n = 0;
    ADDI(p, S0, A0, 0); LUI(p, S2, D_BASE >> 12); ADDI(p, A1, S2, O_MAX);
    ADDI(p, S5, S0, 1);
    ADDI(p, S4, ZERO, PITERS);
    int top = here(p);
    SLLI(p, T0, S5, 13); OPR(p, 0, F3_XOR, S5, S5, T0);
    OPI(p, F3_SRL, T0, S5, 17); OPR(p, 0, F3_XOR, S5, S5, T0);
    SLLI(p, T0, S5, 5); OPR(p, 0, F3_XOR, S5, S5, T0);
    if (amo) AMO(p, F5_AMOMAXU, ZERO, S5, A1, 0);
    else {
        int retry = here(p);
        LR_W(p, T0, A1, 0);
        int done = BR(p, 7, T0, S5, -1);              /* bgeu t0, s5, done */
        SC_W(p, T1, S5, A1, 0);
        BNE(p, T1, ZERO, retry);
        fix(p, done);
    }
    ADDI(p, S4, S4, -1); BNE(p, S4, ZERO, top);
    EBREAK(p);
}
static uint32_t expected_max(void) {
    uint32_t best = 0;
    for (uint32_t h = 0; h < NHART; h++) {
        uint32_t s = h + 1;
        for (int i = 0; i < PITERS; i++) { uint32_t v = xs32(&s); if (v > best) best = v; }
    }
    return best;
}

/* 연습 3: hart0 = X 를 LR/SC 로 PITERS 번 증가, hart1 = 이웃 워드 Y(X+4) 에 PITERS 번 sw */
static void build_neighbor(prog_t *p) {
    p->n = 0;
    ADDI(p, S0, A0, 0); LUI(p, S2, D_BASE >> 12); ADDI(p, A1, S2, O_X); ADDI(p, S4, ZERO, PITERS);
    int h1 = BNE(p, S0, ZERO, -1);
    int top = here(p);
    LR_W(p, T0, A1, 0); ADDI(p, T0, T0, 1); SC_W(p, T1, T0, A1, 0); BNE(p, T1, ZERO, top);
    ADDI(p, S4, S4, -1); BNE(p, S4, ZERO, top);
    EBREAK(p);
    fix(p, h1);
    int t1 = here(p);
    SW(p, S4, 4, A1);
    ADDI(p, S4, S4, -1); BNE(p, S4, ZERO, t1);
    EBREAK(p);
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static void exec1(machine_t *m, int hid, uint32_t w) {
    m->h[hid].pc = 0x0F00u + 0x40u * (uint32_t)hid;
    st32raw(m, m->h[hid].pc, w);
    m->h[hid].state = RUN;
    step(m, hid);
}

int main(void) {
    static prog_t p;
    /* 새 인코딩: VM clang 21 + llvm-objdump 기대값 */
    CHECK(enc_amo(F5_AMOOR, AQRL, A1, A2, A0) == 0x46B6252Fu);    /* amoor.w.aqrl a0, a1, (a2) */
    CHECK(enc_amo(F5_AMOOR, 0, T0, S2, T1) == 0x4059232Fu);       /* amoor.w      t1, t0, (s2) */
    CHECK(enc_amo(F5_AMOAND, RL, T0, S2, T1) == 0x6259232Fu);     /* amoand.w.rl  t1, t0, (s2) */
    CHECK(enc_amo(F5_AMOMAXU, 0, A1, A2, A0) == 0xE0B6252Fu);     /* amomaxu.w    a0, a1, (a2) */

    /* 연습 1 */
    build_flags(&p, false);
    int bad = 0;
    for (uint32_t s = 1; s <= 100; s++)
        bad += !(run_on(&p, 6, SCHED_RANDOM, s) == RUN_OK && data(O_FLAGS) == 0xAAAAAAAAu);
    CHECK(bad == 0);
    build_flags(&p, true);
    int lost = 0;
    for (uint32_t s = 1; s <= 100; s++) lost += run_on(&p, 6, SCHED_RANDOM, s) == RUN_OK && data(O_FLAGS) != 0xAAAAAAAAu;
    printf("ex1 flags: amo 0/100 wrong, plain lw/or/sw %d/100 wrong\n", lost);
    CHECK(lost > 0);

    /* 연습 2 */
    uint32_t want = expected_max();
    for (int amo = 0; amo < 2; amo++) {
        build_max(&p, amo);
        bad = 0;
        for (uint32_t s = 1; s <= 100; s++) bad += !(run_on(&p, 6, SCHED_RANDOM, s) == RUN_OK && data(O_MAX) == want);
        printf("ex2 max (%s): %d bad, %d instructions in program\n", amo ? "amomaxu.w" : "LR/SC loop", bad, p.n);
        CHECK(bad == 0);
    }

    /* 연습 3 */
    build_neighbor(&p);
    uint32_t fails[2] = { 0, 0 };
    unsigned shifts[2] = { 2, 6 };
    for (int k = 0; k < 2; k++) {
        bad = 0;
        for (uint32_t s = 1; s <= 100; s++) {
            bad += !(run_on(&p, shifts[k], SCHED_RANDOM, s) == RUN_OK && data(O_X) == PITERS && data(O_X + 4) == 1u);
            fails[k] += g_m.h[0].sc_fail;
        }
        CHECK(bad == 0);
    }
    printf("ex3 neighbor stores: SC fails word-granule %u, 64B-granule %u (100 seeds)\n", fails[0], fails[1]);
    CHECK(fails[0] == 0 && fails[1] > 0);

    /* 연습 4: x = 5; hart0 lr.w; hart1 sw 7, sw 5 (A-B-A); hart0 sc.w 99 */
    for (int qemu = 0; qemu < 2; qemu++) {
        static machine_t m;
        prog_t empty = { {0}, 0 };
        machine_init(&m, &empty, 6);
        m.sc_value_compare = qemu;
        st32raw(&m, 0x1000u, 5u);
        m.h[0].x[A1] = 0x1000u; m.h[0].x[A2] = 99u; m.h[1].x[A3] = 0x1000u;
        exec1(&m, 0, enc_amo(F5_LR, 0, ZERO, A1, T0));
        m.h[1].x[A2] = 7u; exec1(&m, 1, enc_s(0, A2, A3, 2, OP_STORE));
        m.h[1].x[A2] = 5u; exec1(&m, 1, enc_s(0, A2, A3, 2, OP_STORE));
        exec1(&m, 0, enc_amo(F5_SC, 0, A2, A1, T1));
        printf("ex4 ABA, %s SC: rd=%u x=%u\n", qemu ? "QEMU-style" : "spec", m.h[0].x[T1], ld32(&m, 0x1000u));
        if (qemu) CHECK(m.h[0].x[T1] == 0u && ld32(&m, 0x1000u) == 99u);   /* 값만 보면 "안 바뀐 것" */
        else CHECK(m.h[0].x[T1] != 0u && ld32(&m, 0x1000u) == 5u);         /* 스펙: 남의 스토어 → 실패 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
