/* RISCV-M04 demo_enc.c — 인코더 교차 검증용 덤프
 *
 * solution.c 의 인코더/미니 어셈블러로 만든 "모든" 프로그램(+ 즉치값 경계 모음 + AMO 순서 비트 16종)을
 *   ./demo_enc S   → 우리 디스어셈블러로 만든 어셈블리 텍스트 (clang 이 다시 어셈블할 입력)
 *   ./demo_enc hex → 우리 인코더가 낸 32비트 워드 (한 줄에 하나)
 * 로 출력한다. 디스어셈블러는 ISS 와 같은 디코드 함수(imm_b, imm_j ...)를 쓰므로, 인코더·디코더가
 * "같은 방향으로" 틀리면 텍스트는 그럴듯해 보이고 clang 이 낸 워드와 달라진다 → 외부 기준으로 잡힌다.
 *
 * Mac:  cc -std=c11 -Wall -Wextra -Werror -O2 demo_enc.c -o demo_enc && ./demo_enc S > all.S && ./demo_enc hex > ours.txt
 * VM :  clang --target=riscv32 -march=rv32ia -mno-relax -c all.S -o all.o && llvm-objdump -d all.o   (md 참고)
 */
#define RV_NO_MAIN
#pragma clang diagnostic ignored "-Wunused-function"   /* solution.c 의 테스트 함수는 여기서 안 쓴다 */
#include "solution.c"

static const char *RN[32] = { "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0", "a1", "a2",
    "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6" };
static const char *ORD[4] = { "", ".rl", ".aq", ".aqrl" };

static void fence_set(char *o, uint32_t b) {
    int k = 0;
    if (b & FN_I) o[k++] = 'i';
    if (b & FN_O) o[k++] = 'o';
    if (b & FN_R) o[k++] = 'r';
    if (b & FN_W) o[k++] = 'w';
    o[k] = 0;
}

/* 프로그램 하나를 "<name>_<idx>:" 레이블을 붙여 출력 (분기 대상도 레이블로) */
static void disasm(const char *name, const prog_t *p) {
    printf("%s:\n", name);
    for (int i = 0; i < p->n; i++) {
        uint32_t w = p->w[i], op = w & 0x7Fu, f3 = w >> 12 & 7u, f7 = w >> 25;
        const char *rd = RN[w >> 7 & 31u], *r1 = RN[w >> 15 & 31u], *r2 = RN[w >> 20 & 31u];
        char t[96];
        static const char *BR3[8] = { "beq", "bne", "?", "?", "blt", "bge", "bltu", "bgeu" };
        static const char *IM3[8] = { "addi", "slli", "slti", "sltiu", "xori", "srli", "ori", "andi" };
        static const char *OP3[8] = { "add", "sll", "slt", "sltu", "xor", "srl", "or", "and" };
        switch (op) {
        case OP_LUI: snprintf(t, sizeof t, "lui %s, 0x%x", rd, w >> 12); break;
        case OP_JAL: snprintf(t, sizeof t, "jal %s, %s_%d", rd, name, i + imm_j(w) / 4); break;
        case OP_JALR: snprintf(t, sizeof t, "jalr %s, %d(%s)", rd, imm_i(w), r1); break;
        case OP_BRANCH: snprintf(t, sizeof t, "%s %s, %s, %s_%d", BR3[f3], r1, r2, name, i + imm_b(w) / 4); break;
        case OP_LOAD: snprintf(t, sizeof t, "lw %s, %d(%s)", rd, imm_i(w), r1); break;
        case OP_STORE: snprintf(t, sizeof t, "sw %s, %d(%s)", r2, imm_s(w), r1); break;
        case OP_IMM:
            if (f3 == 1 || f3 == 5)
                snprintf(t, sizeof t, "%s %s, %s, %u", f3 == 5 && f7 == 0x20 ? "srai" : IM3[f3], rd, r1, w >> 20 & 31u);
            else snprintf(t, sizeof t, "%s %s, %s, %d", IM3[f3], rd, r1, imm_i(w));
            break;
        case OP_OP:
            snprintf(t, sizeof t, "%s %s, %s, %s",
                     f7 == 0x20 ? (f3 == 0 ? "sub" : "sra") : OP3[f3], rd, r1, r2);
            break;
        case OP_FENCE: {
            char a[5], b[5];
            fence_set(a, w >> 24 & 15u); fence_set(b, w >> 20 & 15u);
            snprintf(t, sizeof t, "fence %s, %s", a, b);
            break;
        }
        case OP_SYSTEM: snprintf(t, sizeof t, "ebreak"); break;
        case OP_AMO: {
            uint32_t f5 = w >> 27;
            const char *o = ORD[w >> 25 & 3u];
            if (f5 == F5_LR) snprintf(t, sizeof t, "lr.w%s %s, (%s)", o, rd, r1);
            else snprintf(t, sizeof t, "%s%s %s, %s, (%s)",
                          f5 == F5_SC ? "sc.w" : f5 == F5_AMOSWAP ? "amoswap.w" : "amoadd.w", o, rd, r2, r1);
            break;
        }
        default: snprintf(t, sizeof t, ".word 0x%08x", w);
        }
        printf("%s_%d: %s\n", name, i, t);
    }
}

/* 즉치값 경계·뒤/앞 방향 먼 분기·모든 R/I 연산 모음 */
static void build_mix(prog_t *p) {
    p->n = 0;
    int top = here(p);
    LUI(p, T0, 0xFFFFF); LUI(p, A0, 0x1);
    ADDI(p, T1, T0, -2048); ADDI(p, T1, T1, 2047); ADDI(p, A0, ZERO, -1);
    emit(p, enc_i(-1, T0, 2, T1, OP_IMM));  emit(p, enc_i(5, T0, 3, T1, OP_IMM));     /* slti, sltiu */
    emit(p, enc_i(-256, A1, 4, A2, OP_IMM)); emit(p, enc_i(0x7F0, A1, 6, A2, OP_IMM)); /* xori, ori */
    emit(p, enc_i(-16, A1, 7, A2, OP_IMM));                                          /* andi */
    SLLI(p, T2, T2, 31); emit(p, enc_i(7, T2, 5, T2, OP_IMM)); emit(p, enc_i(0x400 | 9, T2, 5, T2, OP_IMM));
    for (uint32_t f3 = 0; f3 < 8; f3++) emit(p, enc_r(0, S3, S2, f3, S1, OP_OP));     /* add..and */
    emit(p, enc_r(0x20, S3, S2, 0, S1, OP_OP)); emit(p, enc_r(0x20, S3, S2, 5, S1, OP_OP)); /* sub, sra */
    LW(p, A0, -2048, S0); LW(p, A0, 2044, S0); SW(p, A1, -4, S0); SW(p, A1, 2040, S0); SW(p, A1, -2048, S0);
    int fwd = JAL(p, RA, -1);
    for (int k = 0; k < 40; k++) ADDI(p, ZERO, ZERO, 0);                              /* nop 40개 */
    fix(p, fwd);
    for (uint32_t f3 = 0; f3 < 8; f3++) if (f3 != 2 && f3 != 3) BR(p, f3, A0, A1, top);  /* 뒤 방향 먼 분기 */
    JAL(p, ZERO, top);
    JALR(p, RA, T0, -12);
    FENCE(p, FN_R, FN_R | FN_W); FENCE(p, FN_R | FN_W, FN_W); FENCE(p, FN_R | FN_W, FN_R | FN_W);
    FENCE(p, FN_I | FN_O | FN_R | FN_W, FN_I | FN_O | FN_R | FN_W); FENCE(p, FN_W, FN_O);
    EBREAK(p);
}
static void build_amo_orders(prog_t *p) {
    p->n = 0;
    for (int o = 0; o < 4; o++) {
        LR_W(p, T0, A0, o); SC_W(p, T1, A2, A0, o);
        AMOSWAP_W(p, A3, T0, A1, o); AMOADD_W(p, ZERO, T1, S2, o);
    }
}

int main(int argc, char **argv) {
    bool S = argc > 1 && argv[1][0] == 'S';
    static prog_t p;
    struct { const char *name; int kind; } list[] = {
        { "spinlock", 0 }, { "broken", 1 }, { "cas", 2 }, { "ticket", 3 }, { "noretry", 4 },
        { "step_good", 5 }, { "step_bad", 6 }, { "mix", 7 }, { "amo_orders", 8 } };
    int total = 0;
    if (S) printf("\t.text\n\t.option norvc\n");
    for (unsigned k = 0; k < sizeof list / sizeof list[0]; k++) {
        switch (list[k].kind) {
        case 0: build_spinlock(&p, ITERS); break;
        case 1: build_broken_lock(&p, ITERS); break;
        case 2: build_cas_counter(&p, ITERS); break;
        case 3: build_ticket_lock(&p, ITERS); break;
        case 4: build_sc_no_retry(&p, ITERS); break;
        case 5: build_step_counter(&p, ITERS, false); break;
        case 6: build_step_counter(&p, ITERS, true); break;
        case 7: build_mix(&p); break;
        default: build_amo_orders(&p); break;
        }
        total += p.n;
        if (S) disasm(list[k].name, &p);
        else for (int i = 0; i < p.n; i++) printf("%08x\n", p.w[i]);
    }
    if (S) printf("# %d instructions\n", total);
    return 0;
}
