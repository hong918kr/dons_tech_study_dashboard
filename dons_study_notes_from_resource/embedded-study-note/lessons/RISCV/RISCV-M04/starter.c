/* RISCV-M04 A 확장: LR/SC 예약과 AMO로 락·CAS 구현
 *
 * 자급식(self-contained) RV32I+A 명령어 집합 시뮬레이터(ISS) — 레슨에 필요한 명령만:
 *   lui auipc jal jalr beq/bne/blt/bge/bltu/bgeu lw sw addi/slti/sltiu/xori/ori/andi/slli/srli/srai
 *   add/sub/sll/slt/sltu/xor/srl/sra/or/and fence ebreak + lr.w sc.w amoswap.w amoadd.w (.aq/.rl 비트 허용)
 * hart 2개, 스케줄러 2종(라운드로빈 quantum / 시드 고정 랜덤 burst). 한 스텝 = 한 hart 의 명령 1개 =
 * 원자적이고 즉시 모두에게 보인다(순차 일관 SC 인터리빙 모델). 그래서 aq/rl·fence 는 여기선 효과 없음
 * (약한 메모리 동작은 RISCV-A04 RVWMO 몫).
 *
 *   Part A  enc_amo (AMO 포맷 인코더) + exec_amo (LR/SC 예약 집합, SC 실패 조건, AMOSWAP/AMOADD)
 *   Part B  기계어 프로그램: AMOSWAP 스핀락(주어짐), LR/SC CAS, ticket lock(AMOADD) → 시드 수백 개 스트레스
 *   Part C  제약된 LR/SC 루프(constrained LR/SC loop) 정적 검사기 + "LR/SC 안의 load" 위반이 실제로 livelock
 *
 * 모든 인코딩은 Lima VM 의 clang 21 (--target=riscv32 -march=rv32ia) + llvm-objdump 로 교차 확인했다 (demo_enc.c).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st   (solution.c 도 같은 명령)
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
enum { F5_AMOADD = 0x00, F5_AMOSWAP = 0x01, F5_LR = 0x02, F5_SC = 0x03 };   /* inst[31:27] */
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
    (void)f5; (void)aqrl; (void)rs2; (void)rs1; (void)rd;
    return 0;                                   /* TODO A-1: 0x00000000 은 항상 illegal → 이 명령을 쓰는 hart 는 트랩 */
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
static void ADD(prog_t *p, int rd, int rs1, int rs2) { emit(p, enc_r(0, rs2, rs1, 0, rd, OP_OP)); }
static void LW(prog_t *p, int rd, int32_t off, int rs1) { emit(p, enc_i(off, rs1, 2, rd, OP_LOAD)); }
static void SW(prog_t *p, int rs2, int32_t off, int rs1) { emit(p, enc_s(off, rs2, rs1, 2, OP_STORE)); }
static void FENCE(prog_t *p, unsigned pred, unsigned succ) { emit(p, (pred & 15u) << 24 | (succ & 15u) << 20 | OP_FENCE); }
static void EBREAK(prog_t *p) { emit(p, 0x00100073u); }
static void JALR(prog_t *p, int rd, int rs1, int32_t off) { emit(p, enc_i(off, rs1, 0, rd, OP_JALR)); }
/* 분기/점프: target >= 0 이면 그 명령 인덱스로, -1 이면 자리만 잡고(오프셋 0) 나중에 fix() */
static int BR(prog_t *p, uint32_t f3, int rs1, int rs2, int target) {
    int at = here(p);
    emit(p, enc_b(target < 0 ? 0 : (target - at) * 4, rs2, rs1, f3));
    return at;
}
#define BEQ(p, a, b, t) BR(p, 0, a, b, t)
#define BNE(p, a, b, t) BR(p, 1, a, b, t)
static int JAL(prog_t *p, int rd, int target) {
    int at = here(p);
    emit(p, enc_j(target < 0 ? 0 : (target - at) * 4, rd));
    return at;
}
static void fix(prog_t *p, int at) {                       /* at 의 분기/점프가 지금 위치(here)를 가리키게 */
    uint32_t w = p->w[at];
    int32_t off = (here(p) - at) * 4;
    if ((w & 0x7Fu) == OP_JAL) p->w[at] = enc_j(off, (int)(w >> 7 & 31u));
    else p->w[at] = enc_b(off, (int)(w >> 20 & 31u), (int)(w >> 15 & 31u), w >> 12 & 7u);
}
static void LR_W(prog_t *p, int rd, int rs1, int o) { emit(p, enc_amo(F5_LR, o, ZERO, rs1, rd)); }
static void SC_W(prog_t *p, int rd, int rs2, int rs1, int o) { emit(p, enc_amo(F5_SC, o, rs2, rs1, rd)); }
static void AMOSWAP_W(prog_t *p, int rd, int rs2, int rs1, int o) { emit(p, enc_amo(F5_AMOSWAP, o, rs2, rs1, rd)); }
static void AMOADD_W(prog_t *p, int rd, int rs2, int rs1, int o) { emit(p, enc_amo(F5_AMOADD, o, rs2, rs1, rd)); }

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

/* Part A-2: A 확장 실행. 스텝 하나 = 원자적이므로 AMO 의 load-op-store 는 사이에 아무것도 끼지 않는다.
 * 순서: 주소 정렬 검사 → 범위 검사 → 실행. rs2 는 rd 를 쓰기 전에 읽는다(rd == rs2 일 수 있음). */
static void exec_amo(machine_t *m, int self, uint32_t w) {
    hart_t *h = &m->h[self];
    (void)w; (void)sc_forced_fail; (void)store32;
    /* TODO A-2: LR(예약 등록) / SC(예약 확인·무효화, rd = 0 또는 1) / AMOSWAP / AMOADD (+ 정렬·범위 트랩) */
    trap(h, CAUSE_ILLEGAL);
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

/* ======================= Part B — 기계어 프로그램 ======================= */
/* 데이터 (s2 = 0x1000 기준 오프셋). 각 변수는 서로 다른 64B 블록에 둔다. */
#define D_BASE   0x1000u
#define O_LOCK   0x000     /* 스핀락 워드 / ticket: next_ticket */
#define O_SERVE  0x040     /* ticket: now_serving */
#define O_COUNT  0x080     /* 공유 카운터 */
#define O_OCC    0x0C0     /* 임계 구역 점유 표시 (상호 배제 감시) */
#define O_VIOL   0x100     /* viol[hartid] */
#define O_RETRY  0x140     /* retry[hartid] */
#define O_CFG    0x180     /* Part C: cfg.step */
static uint32_t rd_data(const machine_t *m, uint32_t off) { return ld32(m, D_BASE + off); }

/* 공통 머리: s0 = hartid, s2 = 데이터 기준, s1 = 위반 수, s6 = 재시도 수, s4 = 반복 횟수 */
static void emit_prologue(prog_t *p, int iters) {
    ADDI(p, S0, A0, 0);
    LUI(p, S2, D_BASE >> 12);
    ADDI(p, S1, ZERO, 0);
    ADDI(p, S6, ZERO, 0);
    ADDI(p, S4, ZERO, iters);                         /* iters <= 2047 (12비트 부호 있는 즉치) */
}
/* 임계 구역: 점유 표시를 확인·설정하고, 공유 카운터를 "일반" lw/addi/sw 로 증가 (원자 아님 → 락이 지켜야 함) */
static void emit_cs(prog_t *p) {
    LW(p, T3, O_OCC, S2);
    ADD(p, S1, S1, T3);                               /* 누가 이미 안에 있으면 viol += 1 */
    ADDI(p, T3, ZERO, 1);
    SW(p, T3, O_OCC, S2);
    LW(p, T4, O_COUNT, S2);
    ADDI(p, T4, T4, 1);
    SW(p, T4, O_COUNT, S2);
    SW(p, ZERO, O_OCC, S2);
}
/* 공통 꼬리: viol[hartid] = s1, retry[hartid] = s6, ebreak */
static void emit_epilogue(prog_t *p) {
    SLLI(p, T0, S0, 2);
    ADD(p, T0, T0, S2);
    SW(p, S1, O_VIOL, T0);
    SW(p, S6, O_RETRY, T0);
    EBREAK(p);
}
/* 루프 끝: s4--, 0 이 아니면 top 으로 */
static void emit_loop_end(prog_t *p, int top) { ADDI(p, S4, S4, -1); BNE(p, S4, ZERO, top); }

/* (주어짐) AMOSWAP 스핀락 — 스펙의 test-and-test-and-set 예제 그대로
 *   again: lw t1,(a1); bnez t1,again; amoswap.w.aq t1,t0,(a1); bnez t1,again; <CS>; amoswap.w.rl x0,x0,(a1) */
static void build_spinlock(prog_t *p, int iters) {
    p->n = 0;
    emit_prologue(p, iters);
    ADDI(p, A1, S2, O_LOCK);
    ADDI(p, T0, ZERO, 1);
    int top = here(p);
    LW(p, T1, 0, A1);                                 /* test: 잡혀 있으면 읽기만 하며 대기 (버스 조용) */
    BNE(p, T1, ZERO, top);
    AMOSWAP_W(p, T1, T0, A1, AQ);                     /* test-and-set: 옛 값 0 이면 내 것. aq = 획득 */
    BNE(p, T1, ZERO, top);
    emit_cs(p);
    AMOSWAP_W(p, ZERO, ZERO, A1, RL);                 /* 해제: 0 저장. rl = 임계 구역 쓰기 먼저 */
    emit_loop_end(p, top);
    emit_epilogue(p);
}

/* (주어짐) 깨진 락: 검사(lw)와 설정(sw) 사이에 틈 → 둘 다 들어간다 */
static void build_broken_lock(prog_t *p, int iters) {
    p->n = 0;
    emit_prologue(p, iters);
    ADDI(p, T0, ZERO, 1);
    int top = here(p);
    LW(p, T1, O_LOCK, S2);
    BNE(p, T1, ZERO, top);
    SW(p, T0, O_LOCK, S2);                            /* 원자 아님! */
    emit_cs(p);
    SW(p, ZERO, O_LOCK, S2);
    emit_loop_end(p, top);
    emit_epilogue(p);
}

/* Part B-1: LR/SC CAS — 스펙 예제의 cas 서브루틴 (a0 = 주소, a1 = 기대값, a2 = 새 값 → a0 = 0 성공 / 1 실패)
 * 를 프로그램 끝에 두고, 본체는 락 없이 count 를 CAS 로 1 증가 (실패하면 s6++ 후 다시 읽기). */
static void build_cas_counter(prog_t *p, int iters) {
    p->n = 0;
    (void)iters; (void)fix; (void)JAL;
    EBREAK(p);                                  /* TODO B-1: 본체 루프 + 프로그램 끝의 cas 서브루틴 */
}

/* Part B-2: ticket lock — my = amoadd(next, 1); while (serving != my) ; fence r,rw; CS; fence rw,w; serving = my+1 */
static void build_ticket_lock(prog_t *p, int iters) {
    p->n = 0;
    (void)iters; (void)AMOADD_W; (void)FENCE;
    EBREAK(p);                                  /* TODO B-2: amoadd 로 번호표, serving 대기, fence, CS, 반납 */
}

/* (주어짐) 실수 재현: SC 결과를 확인하지 않는 fetch_add — 실패한 SC 의 증가가 사라진다 */
static void build_sc_no_retry(prog_t *p, int iters) {
    p->n = 0;
    emit_prologue(p, iters);
    ADDI(p, A1, S2, O_COUNT);
    int top = here(p);
    LR_W(p, T0, A1, 0);
    ADDI(p, T0, T0, 1);
    SC_W(p, T1, T0, A1, 0);                           /* t1 를 안 본다! */
    emit_loop_end(p, top);
    emit_epilogue(p);
}

/* ======================= Part C — 제약된 LR/SC 루프 ======================= */
/* 드라이버 통계: count += cfg.step 을 LR/SC 로. bad = LR 과 SC 사이에서 cfg.step 을 lw (규칙 위반),
 * good = lw 를 LR 앞(루프 밖)으로 올린 것. */
static void build_step_counter(prog_t *p, int iters, bool bad) {
    p->n = 0;
    emit_prologue(p, iters);
    ADDI(p, A1, S2, O_COUNT);
    int top = here(p);
    if (!bad) LW(p, T5, O_CFG, S2);                   /* 좋은 판: 루프 밖에서 미리 읽기 */
    int retry = here(p);
    LR_W(p, T0, A1, 0);
    if (bad) LW(p, T5, O_CFG, S2);                    /* 나쁜 판: LR..SC 사이의 load */
    ADD(p, T0, T0, T5);
    SC_W(p, T1, T0, A1, 0);
    BNE(p, T1, ZERO, retry);
    emit_loop_end(p, top);
    emit_epilogue(p);
}

enum { LRSC_OK = 0, LRSC_NOT_LR, LRSC_NO_SC, LRSC_BAD_INSN, LRSC_BACKWARD, LRSC_ADDR_CHANGED,
       LRSC_NO_LOOP, LRSC_TOO_LONG };
typedef struct { int code, sc_idx, loop_start, loop_end, loop_len, bad_idx; } lrsc_info_t;

static bool is_amo_f5(uint32_t w, uint32_t f5) { return (w & 0x7Fu) == OP_AMO && (w >> 27) == f5; }
/* "기본 I 중 load/store/JALR/FENCE/SYSTEM 이 아닌 것" + 분기/JAL 은 앞 방향만 */
static int lrsc_insn_class(uint32_t w) {
    uint32_t op = w & 0x7Fu, f7 = w >> 25;
    if (op == OP_LUI || op == OP_AUIPC || op == OP_IMM) return LRSC_OK;
    if (op == OP_OP) return (f7 == 0 || f7 == 0x20) ? LRSC_OK : LRSC_BAD_INSN;   /* MUL 등 M 확장은 금지 */
    if (op == OP_BRANCH) return imm_b(w) > 0 ? LRSC_OK : LRSC_BACKWARD;
    if (op == OP_JAL) return imm_j(w) > 0 ? LRSC_OK : LRSC_BACKWARD;
    return LRSC_BAD_INSN;                             /* load, store, jalr, fence, system, AMO/LR, 기타 */
}
static int writes_rd(uint32_t w) {                    /* 이 명령이 쓰는 정수 레지스터 (없으면 0) */
    uint32_t op = w & 0x7Fu;
    if (op == OP_BRANCH || op == OP_STORE || op == OP_FENCE) return 0;
    return (int)(w >> 7 & 31u);
}

/* Part C-1: 정적 검사기. code[lr_idx] 의 LR 에서 시작해:
 *   1) 다음 SC 까지(정적 순서) 모든 명령이 허용 명령인지, LR 의 rs1(주소 레지스터)을 바꾸지 않는지
 *   2) SC 의 rs1 == LR 의 rs1 (같은 주소·같은 크기 .w)
 *   3) SC 뒤 재시도 코드: target <= lr_idx 인 뒤 방향 분기/JAL 을 찾을 때까지 같은 제약
 *   4) 루프 = [그 target, 그 분기] 길이 <= 16 명령
 * out 에 sc_idx / loop_start / loop_end / loop_len / bad_idx(위반 명령 위치, 없으면 -1) 를 채운다. */
static int lrsc_check(const uint32_t *code, int n, int lr_idx, lrsc_info_t *out) {
    lrsc_info_t r = { -1, -1, -1, -1, 0, -1 };
    (void)code; (void)n; (void)lr_idx; (void)lrsc_insn_class; (void)writes_rd;
    if (out) *out = r;                          /* TODO C-1: 1)~4) 규칙 검사 */
    return r.code;
}
static int find_f5(const prog_t *p, uint32_t f5) {
    for (int i = 0; i < p->n; i++) if (is_amo_f5(p->w[i], f5)) return i;
    return -1;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

#define ITERS     200
#define NSEED     300
#define MAXSTEPS  400000L

/* 결과 요약: 2 hart 가 모두 ebreak, count == 2*ITERS, 위반 0 */
typedef struct { int st; uint32_t count, viol, retry, sc_fail; } res_t;
static res_t run_prog(const prog_t *p, int sched, uint32_t seed, int q, unsigned shift, unsigned spur, bool fu) {
    static machine_t m;
    machine_init(&m, p, shift);
    m.spurious_pct = spur; m.fail_unconstrained = fu;
    st32raw(&m, D_BASE + O_CFG, 3u);                  /* Part C: cfg.step = 3 */
    res_t r;
    r.st = run(&m, sched, seed, q, MAXSTEPS);
    r.count = rd_data(&m, O_COUNT);
    r.viol = rd_data(&m, O_VIOL) + rd_data(&m, O_VIOL + 4);
    r.retry = rd_data(&m, O_RETRY) + rd_data(&m, O_RETRY + 4);
    r.sc_fail = m.h[0].sc_fail + m.h[1].sc_fail;
    return r;
}
/* 시드 1..NSEED 랜덤 + RR quantum 1,2,3,5,8 전부에서 정확한가. 실패 시드 수를 돌려준다. */
static int stress(const prog_t *p, uint32_t want, unsigned shift, unsigned spur, uint32_t *retries, uint32_t *scf) {
    static const int qs[] = { 1, 2, 3, 5, 8 };
    int bad = 0;
    *retries = 0; *scf = 0;
    for (int k = 0; k < 5; k++) {
        res_t r = run_prog(p, SCHED_RR, 0, qs[k], shift, spur, false);
        bad += !(r.st == RUN_OK && r.count == want && r.viol == 0);
    }
    for (uint32_t s = 1; s <= NSEED; s++) {
        res_t r = run_prog(p, SCHED_RANDOM, s, 6, shift, spur, false);
        bad += !(r.st == RUN_OK && r.count == want && r.viol == 0);
        *retries += r.retry; *scf += r.sc_fail;
    }
    return bad;
}

/* 단위 테스트용: 한 명령을 hart h 에서 바로 실행 */
static void exec1(machine_t *m, int hid, uint32_t w) {
    m->h[hid].pc = 0x0F00u + 0x40u * (uint32_t)hid;
    st32raw(m, m->h[hid].pc, w);
    m->h[hid].state = RUN;
    step(m, hid);
}

static void test_part_a(void) {
    /* A-1 인코딩: 기대값은 VM 의 clang 21 + llvm-objdump 출력 (demo_enc.c / md 참고) */
    CHECK(enc_amo(F5_LR, 0, ZERO, A0, T0) == 0x100522AFu);         /* lr.w      t0, (a0) */
    CHECK(enc_amo(F5_LR, AQ, ZERO, A0, T0) == 0x140522AFu);        /* lr.w.aq   t0, (a0) */
    CHECK(enc_amo(F5_SC, 0, A2, A0, T0) == 0x18C522AFu);           /* sc.w      t0, a2, (a0) */
    CHECK(enc_amo(F5_SC, RL, A2, A0, T0) == 0x1AC522AFu);          /* sc.w.rl   t0, a2, (a0) */
    CHECK(enc_amo(F5_AMOSWAP, AQ, T0, A1, T1) == 0x0C55A32Fu);     /* amoswap.w.aq t1, t0, (a1) */
    CHECK(enc_amo(F5_AMOSWAP, RL, ZERO, A1, ZERO) == 0x0A05A02Fu); /* amoswap.w.rl zero, zero, (a1) */
    CHECK(enc_amo(F5_AMOADD, AQRL, A1, A2, A0) == 0x06B6252Fu);    /* amoadd.w.aqrl a0, a1, (a2) */

    static machine_t m;
    prog_t empty = { {0}, 0 };
    const uint32_t X = 0x1000u, Y = 0x1004u, Z = 0x1040u;          /* X,Y: 같은 64B 블록, Z: 다른 블록 */
    uint32_t lr0 = enc_amo(F5_LR, 0, ZERO, A1, T0);                /* lr.w t0,(a1) */
    uint32_t sc0 = enc_amo(F5_SC, 0, A2, A1, T1);                  /* sc.w t1,a2,(a1) */
    uint32_t sw1 = enc_s(0, A2, A3, 2, OP_STORE);                  /* sw a2,0(a3) — hart1 의 스토어 */

    /* LR → SC (같은 hart, 방해 없음) → 성공: rd = 0, 메모리 갱신 */
    machine_init(&m, &empty, 6); st32raw(&m, X, 41u);
    m.h[0].x[A1] = X; m.h[0].x[A2] = 42u;
    exec1(&m, 0, lr0); exec1(&m, 0, sc0);
    CHECK(m.h[0].x[T0] == 41u && m.h[0].x[T1] == 0u && ld32(&m, X) == 42u);
    /* 예약 없이 SC → 실패: rd != 0, 메모리 그대로 */
    machine_init(&m, &empty, 6); st32raw(&m, X, 7u);
    m.h[0].x[A1] = X; m.h[0].x[A2] = 99u;
    exec1(&m, 0, sc0);
    CHECK(m.h[0].x[T1] != 0u && ld32(&m, X) == 7u);
    /* SC 는 성공해도 예약을 끝낸다 → 두 번째 SC 실패 */
    machine_init(&m, &empty, 6);
    m.h[0].x[A1] = X; m.h[0].x[A2] = 5u;
    exec1(&m, 0, lr0); exec1(&m, 0, sc0); m.h[0].x[A2] = 6u; exec1(&m, 0, sc0);
    CHECK(m.h[0].x[T1] != 0u && ld32(&m, X) == 5u);
    /* 다른 hart 의 같은 워드 스토어 → SC 실패 (같은 값을 써도! = ABA 면역) */
    machine_init(&m, &empty, 6); st32raw(&m, X, 8u);
    m.h[0].x[A1] = X; m.h[0].x[A2] = 9u; m.h[1].x[A3] = X; m.h[1].x[A2] = 8u;
    exec1(&m, 0, lr0); exec1(&m, 1, sw1); exec1(&m, 0, sc0);
    CHECK(m.h[0].x[T1] != 0u && ld32(&m, X) == 8u);
    /* 같은 64B 블록의 이웃 워드 스토어: 64B 입도면 실패, 워드 입도면 성공 */
    machine_init(&m, &empty, 6);
    m.h[0].x[A1] = X; m.h[0].x[A2] = 1u; m.h[1].x[A3] = Y;
    exec1(&m, 0, lr0); exec1(&m, 1, sw1); exec1(&m, 0, sc0);
    CHECK(m.h[0].x[T1] != 0u);
    machine_init(&m, &empty, 2);
    m.h[0].x[A1] = X; m.h[0].x[A2] = 1u; m.h[1].x[A3] = Y;
    exec1(&m, 0, lr0); exec1(&m, 1, sw1); exec1(&m, 0, sc0);
    CHECK(m.h[0].x[T1] == 0u && ld32(&m, X) == 1u);
    /* 다른 블록 스토어, 다른 hart 의 load·LR 은 예약을 깨지 않는다 */
    machine_init(&m, &empty, 6);
    m.h[0].x[A1] = X; m.h[0].x[A2] = 2u; m.h[1].x[A3] = Z; m.h[1].x[A1] = X;
    exec1(&m, 0, lr0); exec1(&m, 1, sw1); exec1(&m, 1, enc_i(0, A1, 2, T2, OP_LOAD)); exec1(&m, 1, lr0);
    exec1(&m, 0, sc0);
    CHECK(m.h[0].x[T1] == 0u && ld32(&m, X) == 2u);
    /* 다른 hart 의 AMO / 성공한 SC 는 깬다, 실패한 SC 는 (안 썼으니) 안 깬다 */
    machine_init(&m, &empty, 6);
    m.h[0].x[A1] = X; m.h[0].x[A2] = 3u; m.h[1].x[A1] = X; m.h[1].x[A2] = 1u;
    exec1(&m, 0, lr0); exec1(&m, 1, enc_amo(F5_AMOADD, 0, A2, A1, ZERO)); exec1(&m, 0, sc0);
    CHECK(m.h[0].x[T1] != 0u && ld32(&m, X) == 1u);
    machine_init(&m, &empty, 6);
    m.h[0].x[A1] = X; m.h[0].x[A2] = 4u; m.h[1].x[A1] = X; m.h[1].x[A2] = 77u;
    exec1(&m, 0, lr0); exec1(&m, 1, sc0); exec1(&m, 0, sc0);       /* hart1: 예약 없는 SC → 실패, 안 씀 */
    CHECK(m.h[1].x[T1] != 0u && m.h[0].x[T1] == 0u && ld32(&m, X) == 4u);
    machine_init(&m, &empty, 6);
    m.h[0].x[A1] = X; m.h[0].x[A2] = 5u; m.h[1].x[A1] = X; m.h[1].x[A2] = 66u;
    exec1(&m, 0, lr0); exec1(&m, 1, lr0); exec1(&m, 1, sc0); exec1(&m, 0, sc0);   /* hart1 이 먼저 성공 */
    CHECK(m.h[1].x[T1] == 0u && m.h[0].x[T1] != 0u && ld32(&m, X) == 66u);
    /* AMOSWAP / AMOADD: 옛 값을 rd 로, rd == rs2 여도 rs2 는 실행 전 값 */
    machine_init(&m, &empty, 6); st32raw(&m, X, 10u);
    m.h[0].x[A1] = X; m.h[0].x[T0] = 3u;
    exec1(&m, 0, enc_amo(F5_AMOADD, AQRL, T0, A1, T0));           /* amoadd.w.aqrl t0, t0, (a1) */
    CHECK(m.h[0].x[T0] == 10u && ld32(&m, X) == 13u);
    exec1(&m, 0, enc_amo(F5_AMOSWAP, 0, ZERO, A1, T1));           /* amoswap.w t1, zero, (a1) */
    CHECK(m.h[0].x[T1] == 13u && ld32(&m, X) == 0u);
    /* 정렬 안 된 주소: LR = load 예외(4), AMO/SC = store/AMO 예외(6) */
    machine_init(&m, &empty, 6);
    m.h[0].x[A1] = X + 2u;
    exec1(&m, 0, lr0);
    CHECK(m.h[0].state == HALT_TRAP && m.h[0].cause == CAUSE_LMISALIGN);
    m.h[1].x[A1] = X + 2u;
    exec1(&m, 1, enc_amo(F5_AMOSWAP, 0, ZERO, A1, T1));
    CHECK(m.h[1].state == HALT_TRAP && m.h[1].cause == CAUSE_SMISALIGN);
    /* 가짜 실패 100% → 예약이 살아 있어도 SC 실패 */
    machine_init(&m, &empty, 6); m.spurious_pct = 100;
    m.h[0].x[A1] = X; m.h[0].x[A2] = 1u;
    exec1(&m, 0, lr0); exec1(&m, 0, sc0);
    CHECK(m.h[0].x[T1] != 0u && ld32(&m, X) == 0u);
}

static void test_part_b(void) {
    static prog_t p;
    uint32_t want = 2u * ITERS, retries, scf;
    int bad;

    build_spinlock(&p, ITERS);
    res_t r = run_prog(&p, SCHED_RR, 0, 1, 6, 0, false);
    CHECK(r.st == RUN_OK && r.count == want && r.viol == 0);
    bad = stress(&p, want, 6, 0, &retries, &scf);
    printf("spinlock   : %d bad / %d schedules\n", bad, NSEED + 5);
    CHECK(bad == 0);

    build_cas_counter(&p, ITERS);
    r = run_prog(&p, SCHED_RR, 0, 1, 6, 0, false);
    CHECK(r.st == RUN_OK && r.count == want);
    bad = stress(&p, want, 6, 0, &retries, &scf);
    printf("cas counter: %d bad, CAS retries %u, SC fails %u (over %d seeds)\n", bad, retries, scf, NSEED);
    CHECK(bad == 0 && retries > 0 && scf > 0);        /* 경합이 실제로 일어났고, 그래도 정확 */
    bad = stress(&p, want, 6, 20, &retries, &scf);   /* SC 가짜 실패 20% */
    printf("cas counter: %d bad with 20%% spurious SC failures (SC fails %u)\n", bad, scf);
    CHECK(bad == 0);

    build_ticket_lock(&p, ITERS);
    r = run_prog(&p, SCHED_RR, 0, 3, 6, 0, false);
    CHECK(r.st == RUN_OK && r.count == want && r.viol == 0);
    bad = stress(&p, want, 6, 0, &retries, &scf);
    printf("ticket lock: %d bad / %d schedules\n", bad, NSEED + 5);
    CHECK(bad == 0);

    /* 음성 대조군: 깨진 락·SC 미확인은 스트레스가 반드시 잡아내야 한다 (시드 고정 → 결정적) */
    build_broken_lock(&p, ITERS);
    bad = stress(&p, want, 6, 0, &retries, &scf);
    printf("broken lock: %d bad / %d schedules (expected > 0)\n", bad, NSEED + 5);
    CHECK(bad > 0);
    build_sc_no_retry(&p, ITERS);
    r = run_prog(&p, SCHED_RANDOM, 7, 6, 6, 0, false);
    printf("sc no-retry: count %u of %u\n", r.count, want);
    CHECK(r.st == RUN_OK && r.count < want);
}

static void test_part_c(void) {
    static prog_t p;
    lrsc_info_t info;

    build_cas_counter(&p, ITERS);                     /* 스펙 cas: 4 명령 루프, 앞 방향 bne 는 허용 */
    CHECK(lrsc_check(p.w, p.n, find_f5(&p, F5_LR), &info) == LRSC_OK && info.loop_len == 4);
    build_step_counter(&p, ITERS, false);
    CHECK(lrsc_check(p.w, p.n, find_f5(&p, F5_LR), &info) == LRSC_OK && info.loop_len == 4);
    build_step_counter(&p, ITERS, true);
    int lr = find_f5(&p, F5_LR);
    CHECK(lrsc_check(p.w, p.n, lr, &info) == LRSC_BAD_INSN && info.bad_idx == lr + 1);
    CHECK(lrsc_check(p.w, p.n, lr + 1, &info) == LRSC_NOT_LR);

    /* 합성 사례: 17 명령 루프 / 주소 레지스터 변경 / 뒤 방향 분기 / jalr / 재시도 분기 없음 */
    prog_t q = { {0}, 0 };
    int top = here(&q);
    LR_W(&q, T0, A1, 0);
    for (int k = 0; k < 14; k++) ADDI(&q, T0, T0, 1);
    SC_W(&q, T1, T0, A1, 0);
    BNE(&q, T1, ZERO, top);
    CHECK(lrsc_check(q.w, q.n, 0, &info) == LRSC_TOO_LONG && info.loop_len == 17);
    q.n = 0; LR_W(&q, T0, A1, 0); ADDI(&q, A1, A1, 4); SC_W(&q, T1, T0, A1, 0); BNE(&q, T1, ZERO, 0);
    CHECK(lrsc_check(q.w, q.n, 0, &info) == LRSC_ADDR_CHANGED && info.bad_idx == 1);
    q.n = 0; ADDI(&q, T2, ZERO, 0); LR_W(&q, T0, A1, 0); BNE(&q, T0, ZERO, 0); SC_W(&q, T1, T0, A1, 0);
    CHECK(lrsc_check(q.w, q.n, 1, &info) == LRSC_BACKWARD && info.bad_idx == 2);
    q.n = 0; LR_W(&q, T0, A1, 0); JALR(&q, RA, T5, 0); SC_W(&q, T1, T0, A1, 0); BNE(&q, T1, ZERO, 0);
    CHECK(lrsc_check(q.w, q.n, 0, &info) == LRSC_BAD_INSN && info.bad_idx == 1);
    q.n = 0; LR_W(&q, T0, A1, 0); SC_W(&q, T1, T0, A1, 0); EBREAK(&q);
    CHECK(lrsc_check(q.w, q.n, 0, &info) == LRSC_NO_LOOP);

    /* 실행: "제약 안 된 시퀀스는 항상 실패" 구현에서 나쁜 판은 livelock, 좋은 판은 정확히 끝난다.
     * 관대한 구현(fail_unconstrained=false)에서는 나쁜 판도 끝난다 — "어떤 구현에선 된다" 의 함정. */
    uint32_t want = 2u * ITERS * 3u;
    build_step_counter(&p, ITERS, false);
    res_t r = run_prog(&p, SCHED_RANDOM, 11, 6, 6, 0, true);
    CHECK(r.st == RUN_OK && r.count == want);
    build_step_counter(&p, ITERS, true);
    r = run_prog(&p, SCHED_RANDOM, 11, 6, 6, 0, true);
    printf("bad LR/SC loop, strict impl: %s, count %u, SC fails %u\n",
           r.st == RUN_LIMIT ? "step limit (livelock)" : r.st == RUN_TRAP ? "trap" : "finished", r.count, r.sc_fail);
    CHECK(r.st == RUN_LIMIT && r.count == 0);
    r = run_prog(&p, SCHED_RANDOM, 11, 6, 6, 0, false);
    CHECK(r.st == RUN_OK && r.count == want);
}

#ifndef RV_NO_MAIN
int main(void) {
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif
