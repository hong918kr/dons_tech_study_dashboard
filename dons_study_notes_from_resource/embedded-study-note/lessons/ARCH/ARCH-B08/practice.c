/* ARCH-B08 추가 연습 — 한 번에 검증
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- 본문 ISA 의 인코더/디코더 압축판 (solution.c 와 같은 인코딩) ---- */
enum { OP_ADD = 0x0, OP_ADDI = 0x1, OP_LD = 0x2, OP_ST = 0x3, OP_BEQ = 0x4, OP_JAL = 0x5,
       OP_JALR_X = 0x6 /* 연습 4 의 확장 — 공용 ISA 아님 */, OP_HALT = 0xF };
typedef struct { int op, rd, rs, rt, imm; } Insn;
#define FIELD(w, hi, lo) (((unsigned)(w) >> (lo)) & ((1u << ((hi) - (lo) + 1)) - 1u))
static int sext(unsigned v, unsigned bits) { unsigned m = 1u << (bits - 1); return (int)(v ^ m) - (int)m; }
static int fits(long v, unsigned bits) { return v >= -(1L << (bits - 1)) && v <= (1L << (bits - 1)) - 1; }

static int decode(uint16_t w, Insn *o) {                     /* 1 = 합법 */
    unsigned op = FIELD(w, 15, 12), a = FIELD(w, 11, 9), b = FIELD(w, 8, 6);
    memset(o, 0, sizeof *o); o->op = (int)op;
    switch (op) {
    case OP_ADD:  o->rd = (int)a; o->rs = (int)b; o->rt = (int)FIELD(w, 5, 3); return FIELD(w, 2, 0) == 0;
    case OP_ADDI: case OP_LD: o->rd = (int)a; o->rs = (int)b; o->imm = sext(FIELD(w, 5, 0), 6); return 1;
    case OP_ST:   o->rt = (int)a; o->rs = (int)b; o->imm = sext(FIELD(w, 5, 0), 6); return 1;
    case OP_BEQ:  o->rs = (int)a; o->rt = (int)b; o->imm = sext(FIELD(w, 5, 0), 6); return 1;
    case OP_JAL:  o->rd = (int)a; o->imm = sext(FIELD(w, 8, 0), 9); return 1;
    case OP_HALT: return FIELD(w, 11, 0) == 0;
    default:      return 0;
    }
}
static uint16_t r3(int op, int a, int b, int c) {            /* ADD 형식 */
    return (uint16_t)((unsigned)op << 12 | (unsigned)a << 9 | (unsigned)b << 6 | (unsigned)c << 3);
}
static uint16_t i6(int op, int a, int b, int imm) {          /* ADDI/LD/ST/BEQ 형식, 범위는 호출자가 보장 */
    return (uint16_t)((unsigned)op << 12 | (unsigned)a << 9 | (unsigned)b << 6 | ((unsigned)imm & 0x3Fu));
}
static uint16_t j9(int rd, int imm) {
    return (uint16_t)(OP_JAL << 12 | (unsigned)rd << 9 | ((unsigned)imm & 0x1FFu));
}

/* ---- 연습 1: 인코딩 공간 회계 ---- */
static void count_space(int per_op[16], int *legal) {
    memset(per_op, 0, 16 * sizeof per_op[0]);
    *legal = 0;
    for (uint32_t w = 0; w <= 0xFFFF; w++) {
        Insn in;
        if (decode((uint16_t)w, &in)) { per_op[in.op]++; (*legal)++; }
    }
}

/* ---- 연습 2: 먼 BEQ 를 3워드 시퀀스로 늘이기 (branch relaxation) ----
 * pc 에 놓일 "if rs == rt goto target" 을 out 에 쓴다. 반환: 워드 수 (1 또는 3), 불가능하면 -1.
 *   pc  : BEQ rs, rt, +1     ; 같으면 pc+2 (JAL) 로
 *   pc+1: BEQ r0, r0, +1     ; 다르면 JAL 을 건너뛰어 pc+3 으로
 *   pc+2: JAL r0, target      ; imm9 로 먼 곳까지 */
static int emit_beq_far(int pc, int rs, int rt, int target, uint16_t out[3]) {
    int off = target - (pc + 1);
    if (fits(off, 6)) { out[0] = i6(OP_BEQ, rs, rt, off); return 1; }
    int joff = target - (pc + 3);
    if (!fits(joff, 9)) return -1;
    out[0] = i6(OP_BEQ, rs, rt, 1);
    out[1] = i6(OP_BEQ, 0, 0, 1);
    out[2] = j9(0, joff);
    return 3;
}
static int next_pc(uint16_t w, int pc, const uint16_t reg[8]) {   /* 제어 흐름만 보는 1스텝 */
    Insn in;
    if (!decode(w, &in)) return -1;
    if (in.op == OP_BEQ) return reg[in.rs] == reg[in.rt] ? pc + 1 + in.imm : pc + 1;
    if (in.op == OP_JAL) return pc + 1 + in.imm;
    return pc + 1;
}
/* seq 를 base 에 놓고 실행 → 시퀀스를 벗어나는 첫 PC */
static int run_seq(const uint16_t *seq, int cnt, int base, const uint16_t reg[8]) {
    int pc = base;
    for (int steps = 0; pc >= base && pc < base + cnt && steps < 8; steps++)
        pc = next_pc(seq[pc - base], pc, reg);
    return pc;
}

/* ---- 연습 3: 16비트 상수 만들기 (LUI 가 없는 ISA) ----
 * rd <- value 를 ADDI/ADD 만으로. -32..31 이면 1개, 아니면 1+5+5+5 비트 덩어리:
 *   ADDI rd, r0, 첫 덩어리 ; 이후 덩어리마다 ADD rd,rd,rd ×5 (= <<5) 후 ADDI rd, rd, 덩어리(0 이면 생략) */
static int li_seq(int rd, uint16_t value, uint16_t *out) {
    int s = (int16_t)value, n = 0;
    if (s >= -32 && s <= 31) { out[n++] = i6(OP_ADDI, rd, 0, s); return n; }
    int ch[4] = { value >> 15, (value >> 10) & 31, (value >> 5) & 31, value & 31 };
    int k = 0;
    while (ch[k] == 0) k++;                                   /* value != 0 이라 반드시 멈춘다 */
    out[n++] = i6(OP_ADDI, rd, 0, ch[k]);
    for (int j = k + 1; j < 4; j++) {
        for (int d = 0; d < 5; d++) out[n++] = r3(OP_ADD, rd, rd, rd);
        if (ch[j]) out[n++] = i6(OP_ADDI, rd, rd, ch[j]);
    }
    return n;
}
static uint16_t run_alu(const uint16_t *p, int n, int rd) {  /* ADD/ADDI 만 실행 */
    uint16_t r[8] = {0};
    for (int i = 0; i < n; i++) {
        Insn in;
        decode(p[i], &in);
        uint16_t v = in.op == OP_ADD ? (uint16_t)(r[in.rs] + r[in.rt]) : (uint16_t)(r[in.rs] + in.imm);
        if (in.rd != 0) r[in.rd] = v;
    }
    return r[rd];
}

/* ---- 연습 4: 빈 opcode 0110 에 JALR 확장 (공용 ISA 아님 — ARCH-B09 는 쓰지 않는다) ----
 *   JALR rd, rs : 0110 rd rs 000000   rd <- PC + 1; PC <- rs */
static uint16_t enc_jalr(int rd, int rs) { return (uint16_t)(OP_JALR_X << 12 | rd << 9 | rs << 6); }
static int decode_ext(uint16_t w, Insn *o) {
    if (FIELD(w, 15, 12) != OP_JALR_X) return decode(w, o);
    memset(o, 0, sizeof *o);
    o->op = OP_JALR_X; o->rd = (int)FIELD(w, 11, 9); o->rs = (int)FIELD(w, 8, 6);
    return FIELD(w, 5, 0) == 0;
}

/* ---- 연습 5: Verilog $readmemh 이미지 (한 줄에 16진 4자리) ---- */
static int to_memh(const uint16_t *w, int n, char *buf, size_t cap) {
    size_t used = 0;
    for (int i = 0; i < n; i++) {
        int k = snprintf(buf + used, cap - used, "%04x\n", (unsigned)w[i]);
        if (k < 0 || (size_t)k >= cap - used) return -1;
        used += (size_t)k;
    }
    return (int)used;
}
static int from_memh(const char *s, uint16_t *w, int max) {   /* // 주석·빈 줄 허용, 오류 -1 */
    int n = 0;
    while (*s) {
        size_t len = strcspn(s, "\n");
        const char *e = s + len;
        const char *p = s;
        while (p < e && isspace((unsigned char)*p)) p++;
        if (p < e && !(p + 1 < e && p[0] == '/' && p[1] == '/')) {
            unsigned v = 0; int nd = 0;
            while (p < e && isxdigit((unsigned char)*p)) {
                int c = tolower((unsigned char)*p++);
                v = v * 16 + (unsigned)(isdigit(c) ? c - '0' : c - 'a' + 10);
                nd++;
            }
            while (p < e && isspace((unsigned char)*p)) p++;
            if (nd == 0 || nd > 4 || p != e || n >= max) return -1;
            w[n++] = (uint16_t)v;
        }
        s = *e ? e + 1 : e;
    }
    return n;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static const uint16_t SUM_BIN[] = { 0x1210, 0x1405, 0x0600, 0x4405, 0x2840, 0x06E0,
                                    0x1241, 0x14BF, 0x403A, 0x3600, 0xF000 };

int main(void) {
    /* 연습 1 */
    {
        int per[16], legal, free_ops = 0;
        count_space(per, &legal);
        for (int op = 0; op < 16; op++) free_ops += (per[op] == 0);
        CHECK(per[OP_ADD] == 512 && per[OP_ADDI] == 4096 && per[OP_JAL] == 4096 && per[OP_HALT] == 1);
        CHECK(legal == 20993 && 65536 - legal == 44543);
        CHECK(free_ops == 9);                                   /* 0110 ~ 1110 */
        CHECK(legal * 10000L / 65536 == 3203);                  /* 32.03 % 만 쓰인다 */
    }
    /* 연습 2 */
    {
        uint16_t seq[3], eq[8] = {0, 5, 5, 0, 0, 0, 0, 0}, ne[8] = {0, 5, 6, 0, 0, 0, 0, 0};
        CHECK(emit_beq_far(10, 1, 2, 20, seq) == 1 && run_seq(seq, 1, 10, eq) == 20 && run_seq(seq, 1, 10, ne) == 11);
        CHECK(emit_beq_far(10, 1, 2, 200, seq) == 3);
        CHECK(run_seq(seq, 3, 10, eq) == 200 && run_seq(seq, 3, 10, ne) == 13);
        CHECK(emit_beq_far(250, 1, 2, 3, seq) == 3 && run_seq(seq, 3, 250, eq) == 3 && run_seq(seq, 3, 250, ne) == 253);
        CHECK(emit_beq_far(0, 1, 2, 300, seq) == -1);           /* imm9 로도 못 감 */
    }
    /* 연습 3: 65536 개 값 전부 */
    {
        uint16_t p[32];
        int bad = 0, maxn = 0;
        for (uint32_t v = 0; v <= 0xFFFF; v++) {
            int n = li_seq(3, (uint16_t)v, p);
            if (n > maxn) maxn = n;
            if (run_alu(p, n, 3) != v) bad++;
        }
        CHECK(bad == 0);
        CHECK(maxn == 19);
        CHECK(li_seq(3, 0x1234, p) == 13 && li_seq(3, 0xFFFF, p) == 1 && li_seq(3, 31, p) == 1 && li_seq(3, 32, p) == 6);
    }
    /* 연습 4 */
    {
        Insn in;
        CHECK(enc_jalr(0, 7) == 0x61C0);
        CHECK(decode_ext(0x61C0, &in) && in.op == OP_JALR_X && in.rd == 0 && in.rs == 7);
        CHECK(!decode_ext(0x61C1, &in));                        /* 하위 6비트는 0 이어야 */
        CHECK(!decode(0x61C0, &in) && decode_ext(0x0298, &in) && in.op == OP_ADD);  /* 기본 ISA 는 그대로 */
    }
    /* 연습 5 */
    {
        char buf[256];
        uint16_t back[16];
        int len = to_memh(SUM_BIN, 11, buf, sizeof buf);
        CHECK(len == 55 && strncmp(buf, "1210\n1405\n0600\n", 15) == 0);
        CHECK(from_memh(buf, back, 16) == 11 && memcmp(back, SUM_BIN, sizeof SUM_BIN) == 0);
        CHECK(from_memh("// imem image\n\nF000\n  0298  \n", back, 16) == 2 && back[0] == 0xF000 && back[1] == 0x0298);
        CHECK(from_memh("12g4\n", back, 16) == -1 && from_memh("12345\n", back, 16) == -1);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
