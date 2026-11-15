/* ARCH-B08 미니 ISA 설계: 명령어 인코딩·디코딩
 *
 * 16비트 고정 길이 toy ISA (ARCH-B09 가 이 인코딩을 그대로 실행한다)
 *   r0..r7 (r0 = 항상 0), 16비트 워드, imem[256]/dmem[256] 워드 주소, PC = 명령어 번호
 *
 *   bits     15..12  11..9  8..6   5..3   2..0
 *   ADD      0000    rd     rs     rt     000     rd <- rs + rt
 *   ADDI     0001    rd     rs     imm6 (signed)  rd <- rs + sext(imm6)
 *   LD       0010    rd     rs     imm6           rd <- dmem[rs + sext(imm6)]
 *   ST       0011    rt     rs     imm6           dmem[rs + sext(imm6)] <- rt
 *   BEQ      0100    rs     rt     imm6           if rs == rt: PC <- PC + 1 + sext(imm6)
 *   JAL      0101    rd     imm9 (signed)         rd <- PC + 1; PC <- PC + 1 + sext(imm9)
 *   HALT     1111    000000000000                 stop
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { OP_ADD = 0x0, OP_ADDI = 0x1, OP_LD = 0x2, OP_ST = 0x3, OP_BEQ = 0x4, OP_JAL = 0x5, OP_HALT = 0xF };
enum { IMEM_WORDS = 256 };

typedef enum {
    E_OK = 0, E_ILLEGAL, E_BAD_REG, E_IMM_RANGE, E_UNKNOWN_LABEL,
    E_UNKNOWN_MNEMONIC, E_SYNTAX, E_DUP_LABEL, E_TOO_LONG, E_TARGET_RANGE
} Err;

/* 의미 기반 필드. 어떤 필드가 어느 비트 자리에 가는지는 op 마다 다르다.
 *   ADD: rd rs rt · ADDI/LD: rd rs imm · ST: rt rs imm · BEQ: rs rt imm · JAL: rd imm · HALT: -
 * 쓰지 않는 필드는 0. */
typedef struct { int op, rd, rs, rt, imm; } Insn;

/* ---------- 주어진 도구: 비트 필드, 부호 확장 (ARCH-B01 참고), 범위 검사 ---------- */
#define FIELD(w, hi, lo) (((unsigned)(w) >> (lo)) & ((1u << ((hi) - (lo) + 1)) - 1u))
#define REG_OK(r) ((r) >= 0 && (r) <= 7)
static int sext(unsigned v, unsigned bits) {          /* bits 폭 2의 보수 → int */
    unsigned m = 1u << (bits - 1);
    return (int)(v ^ m) - (int)m;                     /* 부호 비트를 뒤집고 빼면 끝 */
}
static int fits_signed(long v, unsigned bits) {
    long lo = -(1L << (bits - 1)), hi = (1L << (bits - 1)) - 1;
    return v >= lo && v <= hi;
}

/* ================= Part A: 인코더 / 디코더 / 디스어셈블러 ================= */

/* 범위를 벗어난 레지스터 → E_BAD_REG, 즉시값 → E_IMM_RANGE (잘라내지 말 것), 모르는 op → E_ILLEGAL */
static Err encode(const Insn *in, uint16_t *out) {
    /* TODO: op 별로 레지스터(0..7)·즉시값 범위를 검사하고 비트 자리에 넣는다.
     *       ST 는 rt 가, BEQ 는 rs 가 bits 11..9 에 간다. 음수 imm 은 & 0x3F / & 0x1FF */
    (void)in; (void)out; (void)fits_signed;
    return E_ILLEGAL;
}

/* 합법이면 E_OK 와 함께 *o 를 채운다 (안 쓰는 필드 0). 정의 안 된 opcode,
 * ADD 의 bits 2..0 != 0, HALT 의 bits 11..0 != 0 → E_ILLEGAL */
static Err decode(uint16_t w, Insn *o) {
    /* TODO: FIELD 로 opcode·레지스터·imm 을 꺼내고 sext 로 부호 확장. 불법 워드 걸러내기 */
    (void)w; (void)sext;
    memset(o, 0, sizeof *o);
    return E_ILLEGAL;
}

/* 정규형 텍스트: "ADD r1, r2, r3" "ADDI r1, r1, -1" "LD r1, 4(r2)" "ST r1, 4(r2)"
 * "BEQ r1, r0, -3" "JAL r7, 10" "HALT". 불법 워드는 ".word 0x6000" 을 쓰고 E_ILLEGAL. */
static Err disasm(uint16_t w, char *buf, size_t n) {
    /* TODO: decode 후 op 별 snprintf. 불법이면 ".word 0x%04X" */
    (void)w;
    if (n) buf[0] = '\0';
    return E_ILLEGAL;
}

/* ================= Part B: 어셈블러 (텍스트 → 기계어) ================= */
/* --- 주어진 도구: 레이블 표, 커서, 토큰 파서 --- */
enum { MAX_LABELS = 64, LABEL_LEN = 32 };
typedef struct { char name[LABEL_LEN]; int addr; } Label;
typedef struct { Label l[MAX_LABELS]; int n; } Labels;
typedef struct { const char *p; } Cur;
#define TRY(x) do { Err e_ = (x); if (e_ != E_OK) return e_; } while (0)

static int is_id0(int c) { return isalpha(c) || c == '_'; }
static int is_id(int c) { return isalnum(c) || c == '_'; }
static void skip_ws(Cur *c) { while (*c->p == ' ' || *c->p == '\t' || *c->p == '\r') c->p++; }

static int label_find(const Labels *L, const char *s, size_t len) {     /* → 주소, 없으면 -1 */
    for (int i = 0; i < L->n; i++)
        if (strlen(L->l[i].name) == len && memcmp(L->l[i].name, s, len) == 0) return L->l[i].addr;
    return -1;
}
static Err p_ident(Cur *c, const char **s, size_t *len) {
    skip_ws(c);
    if (!is_id0((unsigned char)*c->p)) return E_SYNTAX;
    *s = c->p;
    while (is_id((unsigned char)*c->p)) c->p++;
    *len = (size_t)(c->p - *s);
    return E_OK;
}
static Err p_char(Cur *c, char ch) {
    skip_ws(c);
    if (*c->p != ch) return E_SYNTAX;
    c->p++;
    return E_OK;
}
static Err p_end(Cur *c) { skip_ws(c); return *c->p == '\0' ? E_OK : E_SYNTAX; }
static Err p_reg(Cur *c, int *r) {                     /* r0..r7 (R 도 허용) */
    skip_ws(c);
    if ((*c->p != 'r' && *c->p != 'R') || !isdigit((unsigned char)c->p[1])) return E_BAD_REG;
    const char *q = c->p + 1;
    int v = 0;
    while (isdigit((unsigned char)*q)) { if (v < 100) v = v * 10 + (*q - '0'); q++; }
    if (is_id((unsigned char)*q) || v > 7) return E_BAD_REG;            /* r8, r12, r1x */
    *r = v; c->p = q;
    return E_OK;
}
static Err p_int(Cur *c, long *v) {                    /* [+-] 십진수 | [+-] 0x16진수, 포화 ±1e6 */
    skip_ws(c);
    int neg = 0;
    if (*c->p == '+' || *c->p == '-') { neg = (*c->p == '-'); c->p++; }
    int base = 10;
    if (c->p[0] == '0' && (c->p[1] == 'x' || c->p[1] == 'X')) { base = 16; c->p += 2; }
    long x = 0; int nd = 0;
    for (;; c->p++, nd++) {
        int ch = (unsigned char)*c->p, d;
        if (isdigit(ch)) d = ch - '0';
        else if (base == 16 && isxdigit(ch)) d = tolower(ch) - 'a' + 10;
        else break;
        if (x < 1000000) x = x * base + d;
    }
    if (nd == 0 || is_id((unsigned char)*c->p)) return E_SYNTAX;
    *v = neg ? -x : x;
    return E_OK;
}

/* 분기/점프 피연산자: 레이블이면 (주소 - (pc + 1)), 숫자면 그 값 그대로 (PC+1 기준 오프셋).
 * 모르는 레이블 → E_UNKNOWN_LABEL. 범위 검사는 encode 가 한다. */
static Err p_target(Cur *c, int pc, const Labels *L, int *off) {
    /* TODO: 첫 글자가 식별자면 label_find → addr - (pc + 1), 아니면 p_int */
    (void)c; (void)pc; (void)L; (void)off;
    return E_SYNTAX;
}

/* 니모닉 뒤의 피연산자 문법 (o->op 는 이미 채워져 있다):
 *   ADD  rd, rs, rt      ADDI rd, rs, imm      LD rd, imm(rs)     ST rt, imm(rs)
 *   BEQ  rs, rt, target  JAL  rd, target       HALT               — 끝에 남는 글자 → E_SYNTAX */
static Err parse_operands(Cur *c, int pc, const Labels *L, Insn *o) {
    /* TODO: switch (o->op) 로 형식별 문법을 TRY(p_reg / p_char / p_int / p_target) 로 읽고,
     *       마지막에 p_end 로 남은 글자가 없는지 확인 */
    (void)c; (void)pc; (void)L; (void)o; (void)p_target; (void)p_reg; (void)p_char;
    return E_SYNTAX;
}

/* --- 주어진 도구: 2-pass 드라이버 --- */
typedef struct { Err err; int line; } AsmDiag;
static const struct { const char *name; int op; } MNEMONICS[] = {
    {"ADD", OP_ADD}, {"ADDI", OP_ADDI}, {"LD", OP_LD}, {"ST", OP_ST},
    {"BEQ", OP_BEQ}, {"JAL", OP_JAL}, {"HALT", OP_HALT},
};
static int mnemonic_op(const char *s, size_t len) {          /* 대소문자 무시, 없으면 -1 */
    for (size_t i = 0; i < sizeof MNEMONICS / sizeof MNEMONICS[0]; i++) {
        const char *m = MNEMONICS[i].name;
        if (strlen(m) != len) continue;
        size_t k = 0;
        while (k < len && toupper((unsigned char)s[k]) == m[k]) k++;
        if (k == len) return MNEMONICS[i].op;
    }
    return -1;
}

/* 한 줄을 해석한다. pass 1: 레이블만 등록. pass 2: 명령어를 인코딩해 *w 에.
 * 반환: 명령어가 있으면 *has = 1. */
static Err asm_line(char *buf, int pass, int pc, Labels *L, uint16_t *w, int *has) {
    char *semi = strchr(buf, ';');
    if (semi) *semi = '\0';                                  /* ; 이후 주석 */
    Cur c = { buf };
    *has = 0;
    for (;;) {                                               /* "name:" 가 몇 개든 */
        Cur save = c;
        const char *s; size_t len;
        if (p_ident(&c, &s, &len) != E_OK) { c = save; break; }
        skip_ws(&c);
        if (*c.p != ':') { c = save; break; }
        c.p++;
        if (pass == 1) {
            if (len >= LABEL_LEN || L->n >= MAX_LABELS) return E_TOO_LONG;
            if (label_find(L, s, len) >= 0) return E_DUP_LABEL;
            memcpy(L->l[L->n].name, s, len); L->l[L->n].name[len] = '\0';
            L->l[L->n].addr = pc; L->n++;
        }
    }
    skip_ws(&c);
    if (*c.p == '\0') return E_OK;                           /* 빈 줄 / 레이블만 */
    *has = 1;
    if (pass == 1) return E_OK;
    if (strncmp(c.p, ".word", 5) == 0 && !is_id((unsigned char)c.p[5])) {   /* .word 0x6000 */
        long v; c.p += 5;
        TRY(p_int(&c, &v));
        TRY(p_end(&c));
        if (v < 0 || v > 0xFFFF) return E_IMM_RANGE;
        *w = (uint16_t)v;
        return E_OK;
    }
    const char *s; size_t len;
    if (p_ident(&c, &s, &len) != E_OK) return E_SYNTAX;
    int op = mnemonic_op(s, len);
    if (op < 0) return E_UNKNOWN_MNEMONIC;
    Insn in = { op, 0, 0, 0, 0 };
    TRY(parse_operands(&c, pc, L, &in));
    return encode(&in, w);
}

/* src 전체를 어셈블해 out[0..] 에 쓴다. 반환: 워드 수, 오류면 -1 (*d 에 원인과 1부터 센 줄 번호) */
static int assemble(const char *src, uint16_t *out, int max, AsmDiag *d) {
    Labels L;
    L.n = 0;
    d->err = E_OK; d->line = 0;
    int pc = 0;
    for (int pass = 1; pass <= 2; pass++) {
        const char *s = src;
        int line = 0;
        pc = 0;
        while (*s) {
            char buf[128];
            size_t len = strcspn(s, "\n");
            line++;
            Err e = E_OK;
            int has = 0;
            uint16_t w = 0;
            if (len >= sizeof buf) e = E_TOO_LONG;
            else {
                memcpy(buf, s, len); buf[len] = '\0';
                e = asm_line(buf, pass, pc, &L, &w, &has);
                if (e == E_OK && has && pc >= max) e = E_TOO_LONG;
            }
            if (e != E_OK) { d->err = e; d->line = line; return -1; }
            if (has) { if (pass == 2) out[pc] = w; pc++; }
            s += len;
            if (*s == '\n') s++;
        }
    }
    return pc;
}

/* ================= Part C: 펌웨어 이미지 도구 ================= */

/* 로더가 imem 에 올리기 전 검사: 모든 워드가 합법이고, 모든 BEQ/JAL 목표(PC+1+imm)가 [0, n) 안.
 * 첫 문제 워드의 인덱스를 *bad 에 (문제 없으면 -1). E_ILLEGAL / E_TARGET_RANGE / E_OK. */
static Err image_check(const uint16_t *img, int n, int *bad) {
    /* TODO: 각 워드 decode, BEQ/JAL 이면 목표 i + 1 + imm 이 [0, n) 인지 */
    (void)img; (void)n;
    *bad = -1;
    return E_OK;
}

/* 바이너리 패치: prog[pos] 자리에 w 를 끼워 넣고(뒤는 한 칸씩 밀림), 기존 BEQ/JAL 의 PC 상대
 * 오프셋을 고친다. 규칙: 목표가 pos 이하면 그대로(= pos 를 노리던 분기는 새 명령어로 간다),
 * pos 보다 크면 +1. 고친 오프셋이 imm 범위를 넘으면 E_IMM_RANGE 를 돌려주고 prog/n 은 그대로.
 * 공간 부족(*n >= max) 또는 pos 범위 밖 → E_TOO_LONG. w 자신의 오프셋은 고치지 않는다. */
static Err insert_insn(uint16_t *prog, int *n, int max, int pos, uint16_t w) {
    /* TODO: 임시 배열에 새 배치를 만들며 BEQ/JAL 오프셋을 고친다 (옛 i → 새 ni, 옛 목표 t → 새 nt).
     *       하나라도 encode 가 실패하면 prog/n 을 건드리지 말고 그 오류를 반환 */
    (void)prog; (void)n; (void)max; (void)pos; (void)w;
    return E_TOO_LONG;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2026B08u;                         /* xorshift32, 고정 시드 */
static uint32_t rng(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

static uint16_t enc(int op, int rd, int rs, int rt, int imm) {
    Insn in = { op, rd, rs, rt, imm };
    uint16_t w = 0xDEAD;
    return encode(&in, &w) == E_OK ? w : 0xDEAD;
}
static Err enc_err(int op, int rd, int rs, int rt, int imm) {
    Insn in = { op, rd, rs, rt, imm };
    uint16_t w;
    return encode(&in, &w);
}
static int dis_is(uint16_t w, const char *want) {
    char b[48];
    disasm(w, b, sizeof b);
    return strcmp(b, want) == 0;
}
static int asm1(const char *line, uint16_t *w) {             /* 한 줄 → 워드 1개면 1 */
    AsmDiag d;
    return assemble(line, w, 1, &d) == 1;
}
static int asm_err(const char *src, Err want, int line) {
    uint16_t out[IMEM_WORDS];
    AsmDiag d;
    int n = assemble(src, out, IMEM_WORDS, &d);
    return n == -1 && d.err == want && d.line == line;
}

static const char SUM_SRC[] =
    "; 배열 합: dmem[16..20] 을 더해 dmem[0] 에 저장\n"
    "        ADDI r1, r0, 16      ; base\n"
    "        ADDI r2, r0, 5       ; n\n"
    "        ADD  r3, r0, r0      ; sum = 0\n"
    "loop:   BEQ  r2, r0, done    ; n == 0 이면 끝\n"
    "        LD   r4, 0(r1)\n"
    "        ADD  r3, r3, r4\n"
    "        ADDI r1, r1, 1\n"
    "        ADDI r2, r2, -1\n"
    "        BEQ  r0, r0, loop    ; 무조건 분기\n"
    "done:   ST   r3, 0(r0)\n"
    "        HALT\n";
static const uint16_t SUM_BIN[] = { 0x1210, 0x1405, 0x0600, 0x4405, 0x2840, 0x06E0,
                                    0x1241, 0x14BF, 0x403A, 0x3600, 0xF000 };

/* SUM_SRC 의 loop: 바로 뒤에 계수기 한 줄을 넣은 소스 — 패치 결과와 비교용 */
static const char SUM_PATCHED_SRC[] =
    "        ADDI r1, r0, 16\n"
    "        ADDI r2, r0, 5\n"
    "        ADD  r3, r0, r0\n"
    "loop:   ADDI r5, r5, 1       ; ← 끼워 넣은 반복 계수기\n"
    "        BEQ  r2, r0, done\n"
    "        LD   r4, 0(r1)\n"
    "        ADD  r3, r3, r4\n"
    "        ADDI r1, r1, 1\n"
    "        ADDI r2, r2, -1\n"
    "        BEQ  r0, r0, loop\n"
    "done:   ST   r3, 0(r0)\n"
    "        HALT\n";

int main(void) {
    /* ---------- Part A: 손으로 계산한 인코딩 ---------- */
    CHECK(enc(OP_ADD, 1, 2, 3, 0) == 0x0298);                 /* 0000 001 010 011 000 */
    CHECK(enc(OP_ADDI, 1, 1, 0, -1) == 0x127F);               /* imm6 = 111111 */
    CHECK(enc(OP_LD, 1, 2, 0, 4) == 0x2284);
    CHECK(enc(OP_ST, 0, 2, 1, 4) == 0x3284);                  /* ST r1, 4(r2): rt=1 이 11..9 */
    CHECK(enc(OP_BEQ, 0, 1, 0, -3) == 0x423D);                /* BEQ r1, r0, -3 */
    CHECK(enc(OP_JAL, 7, 0, 0, 10) == 0x5E0A);
    CHECK(enc(OP_JAL, 0, 0, 0, -256) == 0x5100);
    CHECK(enc(OP_HALT, 0, 0, 0, 0) == 0xF000);
    CHECK(enc(OP_ADD, 0, 0, 0, 0) == 0x0000);                 /* ADD r0, r0, r0 = NOP */
    /* 범위 오류는 잘라내지 말고 보고 */
    CHECK(enc_err(OP_ADD, 1, 2, 8, 0) == E_BAD_REG);
    CHECK(enc_err(OP_ADDI, -1, 0, 0, 0) == E_BAD_REG);
    CHECK(enc_err(OP_ADDI, 1, 1, 0, 32) == E_IMM_RANGE);
    CHECK(enc_err(OP_LD, 1, 1, 0, -33) == E_IMM_RANGE);
    CHECK(enc_err(OP_ADDI, 1, 1, 0, -32) == E_OK && enc_err(OP_ADDI, 1, 1, 0, 31) == E_OK);
    CHECK(enc_err(OP_JAL, 1, 0, 0, 256) == E_IMM_RANGE && enc_err(OP_JAL, 1, 0, 0, 255) == E_OK);
    CHECK(enc_err(0x6, 0, 0, 0, 0) == E_ILLEGAL);
    /* 디코드: 불법 워드 */
    {
        Insn in;
        CHECK(decode(0x0001, &in) == E_ILLEGAL);              /* ADD 인데 bits 2..0 != 0 */
        CHECK(decode(0x6000, &in) == E_ILLEGAL && decode(0xE123, &in) == E_ILLEGAL);
        CHECK(decode(0xF001, &in) == E_ILLEGAL && decode(0xFFFF, &in) == E_ILLEGAL);
        CHECK(decode(0x423D, &in) == E_OK && in.op == OP_BEQ && in.rs == 1 && in.rt == 0 && in.imm == -3);
        CHECK(decode(0x5100, &in) == E_OK && in.op == OP_JAL && in.rd == 0 && in.imm == -256);
        CHECK(decode(0x3284, &in) == E_OK && in.op == OP_ST && in.rt == 1 && in.rs == 2 && in.imm == 4);
    }
    /* 디스어셈블 정규형 */
    CHECK(dis_is(0x0298, "ADD r1, r2, r3"));
    CHECK(dis_is(0x127F, "ADDI r1, r1, -1"));
    CHECK(dis_is(0x2284, "LD r1, 4(r2)"));
    CHECK(dis_is(0x3284, "ST r1, 4(r2)"));
    CHECK(dis_is(0x423D, "BEQ r1, r0, -3"));
    CHECK(dis_is(0x5E0A, "JAL r7, 10"));
    CHECK(dis_is(0xF000, "HALT"));
    CHECK(dis_is(0xFFFF, ".word 0xFFFF"));
    /* 전수: 65536 워드 모두 — decode 실패하거나, 다시 encode 하면 같은 워드 */
    {
        int legal = 0, bad = 0, per_op[16] = {0};
        for (uint32_t w = 0; w <= 0xFFFF; w++) {
            Insn in;
            uint16_t back = 0;
            if (decode((uint16_t)w, &in) != E_OK) continue;
            legal++; per_op[in.op]++;
            if (encode(&in, &back) != E_OK || back != w) bad++;
        }
        CHECK(bad == 0);
        CHECK(legal == 20993);                                 /* 512 + 4*4096 + 4096 + 1 */
        CHECK(per_op[OP_ADD] == 512 && per_op[OP_BEQ] == 4096 && per_op[OP_HALT] == 1);
    }

    /* ---------- Part B: 어셈블러 ---------- */
    {
        uint16_t out[IMEM_WORDS];
        AsmDiag d;
        int n = assemble(SUM_SRC, out, IMEM_WORDS, &d);
        CHECK(n == 11 && memcmp(out, SUM_BIN, sizeof SUM_BIN) == 0);
        /* 왕복: asm → bin → 정규형 텍스트 → bin 이 같아야 */
        char text[2048] = "";
        for (int i = 0; i < n; i++) {
            char b[48];
            disasm(out[i], b, sizeof b);
            strcat(text, b); strcat(text, "\n");
        }
        uint16_t out2[IMEM_WORDS];
        int n2 = assemble(text, out2, IMEM_WORDS, &d);
        CHECK(n2 == n && memcmp(out, out2, (size_t)n * 2) == 0);
    }
    {
        uint16_t w = 0;
        CHECK(asm1("add R1,r2 ,  r3", &w) && w == 0x0298);     /* 대소문자·공백 자유 */
        CHECK(asm1("x: y: JAL r7, x", &w) && w == 0x5FFF);     /* 자기 자신으로: 0 - 1 = -1 */
        CHECK(asm1("ADDI r1, r1, 0x1F", &w) && w == 0x125F);
        CHECK(asm1("LD r2, -32(r7)", &w) && w == 0x25E0);
        CHECK(asm1(".word 0xE123", &w) && w == 0xE123);
    }
    /* 오류: 원인 + 줄 번호 */
    CHECK(asm_err("HALT\nADD r1, r2, r8\n", E_BAD_REG, 2));
    CHECK(asm_err("ADD r1, r2, 5\n", E_BAD_REG, 1));
    CHECK(asm_err("ADDI r1, r1, 32\n", E_IMM_RANGE, 1));
    CHECK(asm_err("\n\nBEQ r1, r0, nowhere\n", E_UNKNOWN_LABEL, 3));
    CHECK(asm_err("FOO r1\n", E_UNKNOWN_MNEMONIC, 1));
    CHECK(asm_err("LD r1, 4 r2\n", E_SYNTAX, 1));
    CHECK(asm_err("ADD r1, r2\n", E_SYNTAX, 1));
    CHECK(asm_err("HALT r1\n", E_SYNTAX, 1));
    CHECK(asm_err("a: HALT\na: HALT\n", E_DUP_LABEL, 2));
    CHECK(asm_err(".word 0x10000\n", E_IMM_RANGE, 1));
    {   /* 레이블이 imm6 밖: BEQ 다음 32칸 뒤 → 오프셋 32 → 범위 오류 */
        char src[1024] = "BEQ r0, r0, far\n";
        for (int i = 0; i < 32; i++) strcat(src, "HALT\n");
        strcat(src, "far: HALT\n");
        CHECK(asm_err(src, E_IMM_RANGE, 1));
    }
    /* 전수 텍스트 왕복: 65536 워드 → disasm 텍스트 → assemble → 같은 워드 */
    {
        int bad = 0;
        for (uint32_t w = 0; w <= 0xFFFF; w++) {
            char b[48];
            uint16_t back = 0;
            disasm((uint16_t)w, b, sizeof b);
            if (!asm1(b, &back) || back != w) bad++;
        }
        CHECK(bad == 0);
    }
    /* 퍼징: SUM_SRC 를 1~3 글자 바꾼 2000개 — 죽지 말고, 성공이면 결과가 왕복되고, 실패면 줄 번호가 유효 */
    {
        static const char alpha[] = "r0123456789,()-+:;xADEHILJQSTW .\t";
        int ok = 0, err = 0, broken = 0;
        for (int it = 0; it < 2000; it++) {
            char src[sizeof SUM_SRC];
            memcpy(src, SUM_SRC, sizeof SUM_SRC);
            int k = 1 + (int)(rng() % 3);
            for (int j = 0; j < k; j++)
                src[rng() % (sizeof SUM_SRC - 1)] = alpha[rng() % (sizeof alpha - 1)];
            uint16_t out[IMEM_WORDS];
            AsmDiag d;
            int n = assemble(src, out, IMEM_WORDS, &d);
            if (n < 0) {
                err++;
                if (d.err == E_OK || d.line < 1 || d.line > 12) broken++;
            } else {
                ok++;
                for (int i = 0; i < n; i++) {
                    char b[48]; uint16_t back = 0;
                    disasm(out[i], b, sizeof b);
                    if (!asm1(b, &back) || back != out[i]) broken++;
                }
            }
        }
        CHECK(broken == 0 && ok > 0 && err > 0);
    }

    /* ---------- Part C: 이미지 검사 + 바이너리 패치 ---------- */
    {
        int bad = 99;
        uint16_t img[16];
        memcpy(img, SUM_BIN, sizeof SUM_BIN);
        CHECK(image_check(img, 11, &bad) == E_OK && bad == -1);
        img[5] = 0x7000;                                        /* 불법 opcode 로 오염 */
        CHECK(image_check(img, 11, &bad) == E_ILLEGAL && bad == 5);
        memcpy(img, SUM_BIN, sizeof SUM_BIN);
        CHECK(image_check(img, 9, &bad) == E_TARGET_RANGE && bad == 3);   /* done(9) 이 잘려 나감 */
        img[0] = enc(OP_JAL, 0, 0, 0, 10);                      /* 목표 11 == n → 끝 밖 */
        CHECK(image_check(img, 11, &bad) == E_TARGET_RANGE && bad == 0);
    }
    {
        uint16_t prog[IMEM_WORDS], want[IMEM_WORDS];
        AsmDiag d;
        int n = assemble(SUM_SRC, prog, IMEM_WORDS, &d);
        int nw = assemble(SUM_PATCHED_SRC, want, IMEM_WORDS, &d);
        CHECK(insert_insn(prog, &n, IMEM_WORDS, 3, enc(OP_ADDI, 5, 5, 0, 1)) == E_OK);
        CHECK(n == 12 && nw == 12 && memcmp(prog, want, 24) == 0);  /* 패치 == 소스 수정 후 재어셈블 */
        int bad;
        CHECK(image_check(prog, n, &bad) == E_OK);
        CHECK(insert_insn(prog, &n, 12, 0, 0xF000) == E_TOO_LONG && n == 12);  /* 공간 없음 */
    }
    {   /* 오프셋 31 짜리 분기를 가로질러 끼우면 32 가 되어 imm6 초과 → 거부, 원본 그대로 */
        uint16_t prog[IMEM_WORDS], copy[IMEM_WORDS];
        int n = 33;
        prog[0] = enc(OP_BEQ, 0, 0, 0, 31);
        for (int i = 1; i < n; i++) prog[i] = 0xF000;
        memcpy(copy, prog, sizeof prog[0] * 33);
        CHECK(insert_insn(prog, &n, IMEM_WORDS, 5, 0x0000) == E_IMM_RANGE);
        CHECK(n == 33 && memcmp(prog, copy, sizeof prog[0] * 33) == 0);
        CHECK(insert_insn(prog, &n, IMEM_WORDS, 0, 0x0000) == E_OK && n == 34 && prog[0] == 0x0000
              && prog[1] == enc(OP_BEQ, 0, 0, 0, 31));          /* 분기 앞에 넣으면 상대거리 불변 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
