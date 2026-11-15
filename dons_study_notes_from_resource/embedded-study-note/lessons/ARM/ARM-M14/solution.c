/* ARM-M14 크래시 트리아지: lldb 배치 덤프(crash_*.txt)만 보고 원인 분류 + 소스 라인 역추적
 * 입력 = make_dumps.sh 가 만든 실제 lldb 출력 (register read / disassemble -f / memory read /
 *        image dump sections). 출력 = {NULL_MEMBER, BAD_PC, SCRIBBLE, PAC_FAIL, UNKNOWN} + 근거.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c  -o /tmp/ARM-M14_starter   (solution.c 도 같은 방법)
 * 실행: 레슨 디렉터리에서 /tmp/ARM-M14_starter  (또는 인자로 레슨 디렉터리 경로)
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- 데이터 파일 (이 소스 파일 옆) ---------------- */
static char g_dir[512] = ".";
static void init_dir(int argc, char **argv) {
    const char *f = __FILE__, *sl = strrchr(f, '/');
    if (argc > 1) snprintf(g_dir, sizeof g_dir, "%s", argv[1]);
    else if (sl) snprintf(g_dir, sizeof g_dir, "%.*s", (int)(sl - f), f);
}
static char *load_file(const char *name) {               /* 못 열면 빈 문자열 */
    char p[640];
    snprintf(p, sizeof p, "%s/%s", g_dir, name);
    FILE *fp = fopen(p, "r");
    if (!fp) fp = fopen(name, "r");
    if (!fp) { snprintf(p, sizeof p, "lessons/ARM/ARM-M14/%s", name); fp = fopen(p, "r"); }
    char *buf = calloc(1, 1 << 16);
    if (!buf) exit(2);
    if (fp) { size_t n = fread(buf, 1, (1 << 16) - 1, fp); buf[n] = '\0'; fclose(fp); }
    else printf("cannot open %s (run in the lesson dir or pass it as argv[1])\n", name);
    return buf;
}

/* ---------------- 공통 타입 ---------------- */
enum { R_FP = 29, R_LR = 30, R_SP = 31, R_PC = 32, R_CPSR = 33, R_FAR = 34, R_ESR = 35, NREG = 36 };
typedef enum { CR_UNKNOWN, CR_NULL_MEMBER, CR_BAD_PC, CR_SCRIBBLE, CR_PAC_FAIL } crash_class_t;
static const char *const CLASS_NAME[] = {"UNKNOWN", "NULL_MEMBER", "BAD_PC", "SCRIBBLE", "PAC_FAIL"};

typedef struct { char exc[24]; int code; int have_addr; uint64_t addr; } stop_t;
typedef struct {
    char    mnem[12];
    int     dst, dst2;      /* 이 명령이 쓰는 레지스터 (없으면 -1). ldp 는 둘 */
    int     is_mem;         /* [ ] 메모리 피연산자가 있나 */
    int     base;           /* 베이스 레지스터 번호 (sp=31), 없으면 -1 */
    int64_t imm;            /* 즉치 오프셋 (없으면 0) */
    int     has_index;      /* [xn, xm, lsl #3] 처럼 레지스터 오프셋 */
    int     pre, post;      /* [xn, #i]!  /  [xn], #i */
} insn_t;

#define MAXDIS 64
#define MAXMEM 64
typedef struct {
    int      have_stop; stop_t stop;
    uint64_t reg[NREG]; uint64_t regmask;            /* bit i = reg[i] 를 읽었다 */
    char     pc_sym[128], lr_sym[128];               /* "img`func + 16 at file.c:36:17" */
    int      have_text; uint64_t text_lo, text_hi;   /* __TEXT.__text [lo, hi) */
    int      have_insn; uint64_t insn_addr; insn_t insn;
    int      ndis; uint64_t dis_addr[MAXDIS]; char dis_text[MAXDIS][64];
    int      nmem; uint64_t mem_addr[MAXMEM], mem_val[MAXMEM]; char mem_sym[MAXMEM][96];
} dump_t;
#define HAS(d, r) ((r) >= 0 && (r) < NREG && (((d)->regmask >> (r)) & 1u))

/* ---------------- 주어진 도우미 ---------------- */
static int hexdig(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
/* "0x" + 1..16 자리 16진수 → 소비한 글자 수, 실패 0 */
int parse_hex(const char *s, uint64_t *v) {
    if (s[0] != '0' || s[1] != 'x') return 0;
    int i = 2; uint64_t x = 0;
    while (hexdig((unsigned char)s[i]) >= 0) {
        if (i - 2 == 16) return 0;                       /* 17자리 이상 = 64비트 초과 */
        x = x * 16 + (uint64_t)hexdig((unsigned char)s[i]);
        i++;
    }
    if (i == 2) return 0;
    *v = x;
    return i;
}
/* 레지스터 이름(길이 n) → 번호. x0–x30 / w0–w30 / fp lr sp wsp pc cpsr far esr, 그 밖 -1 */
int reg_index(const char *s, size_t n) {
    static const struct { const char *nm; int id; } SPECIAL[] = {
        {"fp", R_FP}, {"lr", R_LR}, {"sp", R_SP}, {"wsp", R_SP}, {"pc", R_PC},
        {"cpsr", R_CPSR}, {"far", R_FAR}, {"esr", R_ESR}};
    for (size_t i = 0; i < sizeof SPECIAL / sizeof SPECIAL[0]; i++)
        if (strlen(SPECIAL[i].nm) == n && strncmp(s, SPECIAL[i].nm, n) == 0) return SPECIAL[i].id;
    if (n < 2 || n > 3 || (s[0] != 'x' && s[0] != 'w')) return -1;
    if (n == 3 && s[1] == '0') return -1;                /* "x05" 같은 표기는 없다 */
    int v = 0;
    for (size_t i = 1; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v <= 30 ? v : -1;
}
__attribute__((unused)) static int is_name_ch(int c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.'; }
static const char *skip_ws(const char *p) { while (*p == ' ' || *p == '\t') p++; return p; }
/* s[0..len) 을 뒤쪽 공백을 떼고 dst(크기 n) 에 복사 */
static void copy_trim(char *dst, size_t n, const char *s, size_t len) {
    while (len && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\n')) len--;
    if (len >= n) len = n - 1;
    memcpy(dst, s, len);
    dst[len] = '\0';
}
/* 해제된 메모리 채움 패턴(0x55 / 0xAA) + 작은 오프셋: 상위 6바이트가 전부 55 또는 AA */
static int is_fill(uint64_t v) { return (v >> 16) == 0x555555555555ull || (v >> 16) == 0xaaaaaaaaaaaaull; }
/* 유효 주소: post-index 는 베이스 그대로, 그 밖은 베이스 + 즉치 */
static uint64_t insn_ea(const insn_t *in, uint64_t base) { return in->post ? base : base + (uint64_t)in->imm; }

/* ================= Part A — 한 줄 파서 ================= */

/* "        x0 = 0x0000000000000000"
 * "        lr = 0x000000010000059c  ARM-M14_null`main + 64 at demo_crash_null.c:45:5"
 * "       x16 = 0x516c00018fb635e4 (0x000000018fb635e4) libsystem_kernel.dylib`__error"
 * → *idx = 레지스터 번호, *val = 첫 16진수(레지스터 원본 값), sym = 그 뒤 기호 정보(없으면 "")
 * 성공 1, 레지스터 줄이 아니면 0 */
int parse_reg_line(const char *line, int *idx, uint64_t *val, char *sym, size_t symsz) {
    /* TODO(A1) */
    const char *p = skip_ws(line), *nm = p;
    while (is_name_ch((unsigned char)*p)) p++;
    int r = reg_index(nm, (size_t)(p - nm));
    if (r < 0) return 0;
    p = skip_ws(p);
    if (*p != '=') return 0;
    p = skip_ws(p + 1);
    uint64_t v;
    int k = parse_hex(p, &v);
    if (!k || (p[k] != '\0' && p[k] != ' ' && p[k] != '\n')) return 0;
    p = skip_ws(p + k);
    if (*p == '(') {                                     /* arm64e: (서명 뗀 값) */
        const char *q = strchr(p, ')');
        p = q ? skip_ws(q + 1) : p + strlen(p);
    }
    *idx = r; *val = v;
    if (sym && symsz) copy_trim(sym, symsz, p, strlen(p));
    return 1;
}

/* "* thread #1, ..., stop reason = EXC_BAD_ACCESS (code=1, address=0x18)"
 * → exc="EXC_BAD_ACCESS", code=1, have_addr=1, addr=0x18. 주소/코드가 없으면 have_addr=0, code=-1
 * "stop reason = " 이 없으면 0 */
int parse_stop_reason(const char *line, stop_t *st) {
    /* TODO(A2) */
    const char *p = strstr(line, "stop reason = ");
    if (!p) return 0;
    memset(st, 0, sizeof *st);
    st->code = -1;
    p += strlen("stop reason = ");
    size_t n = 0;
    while (p[n] && p[n] != ' ' && p[n] != '\n' && n < sizeof st->exc - 1) n++;
    if (n == 0) return 0;
    memcpy(st->exc, p, n); st->exc[n] = '\0';
    const char *c = strstr(p, "(code=");
    if (c) {
        c += 6;
        int neg = (*c == '-'), v = 0;
        if (neg) c++;
        while (*c >= '0' && *c <= '9') v = v * 10 + (*c++ - '0');
        st->code = neg ? -v : v;
    }
    const char *a = strstr(p, "address=");
    if (a && parse_hex(a + 8, &st->addr)) st->have_addr = 1;
    return 1;
}

/* 디스어셈블리 명령 텍스트 하나 → insn_t.  예:
 *   "ldr    x8, [x0, #0x18]"   "ldrb   w2, [x1, #0x1]!"   "ldr x0, [x1], #0x8"
 *   "ldp    x29, x30, [sp, #0x20]"   "ldur x8, [x29, #-0x8]"   "ldr x8, [x0, x1, lsl #3]"
 *   "str    w0, [x19, #0x10]"   "blr    x8"   "retab"
 * 저장/비교/분기는 dst = -1. 성공 1 (니모닉이 없으면 0) */
int parse_insn(const char *text, insn_t *in) {
    /* TODO(A3) */
    memset(in, 0, sizeof *in);
    in->dst = in->dst2 = in->base = -1;
    const char *p = skip_ws(text), *m = p;
    while (is_name_ch((unsigned char)*p)) p++;
    size_t mn = (size_t)(p - m);
    if (mn == 0 || mn >= sizeof in->mnem) return 0;
    memcpy(in->mnem, m, mn); in->mnem[mn] = '\0';
    int no_dst = strncmp(in->mnem, "st", 2) == 0 || strcmp(in->mnem, "cmp") == 0 ||
                 strcmp(in->mnem, "cmn") == 0 || strcmp(in->mnem, "tst") == 0 ||
                 strncmp(in->mnem, "b.", 2) == 0 || strcmp(in->mnem, "b") == 0 ||
                 strncmp(in->mnem, "bl", 2) == 0 || strcmp(in->mnem, "br") == 0 ||
                 strncmp(in->mnem, "ret", 3) == 0 || strncmp(in->mnem, "cb", 2) == 0 ||
                 strncmp(in->mnem, "tb", 2) == 0;
    /* 피연산자: '[' 앞의 레지스터들 */
    p = skip_ws(p);
    int nreg = 0, regs[2] = {-1, -1};
    while (*p && *p != '[') {
        const char *t = p;
        while (is_name_ch((unsigned char)*p)) p++;
        int r = reg_index(t, (size_t)(p - t));
        if (r >= 0 && nreg < 2) regs[nreg++] = r;
        if (p == t) p++;                                 /* ',' ' ' '#' 등 건너뛰기 */
    }
    if (!no_dst) { in->dst = regs[0]; if (strncmp(in->mnem, "ldp", 3) == 0) in->dst2 = regs[1]; }
    if (*p != '[') return 1;
    in->is_mem = 1;
    p = skip_ws(p + 1);
    const char *t = p;
    while (is_name_ch((unsigned char)*p)) p++;
    in->base = reg_index(t, (size_t)(p - t));
    p = skip_ws(p);
    int64_t sign = 1; uint64_t v = 0; int k;
    if (*p == ',') {
        p = skip_ws(p + 1);
        if (*p == '#') {
            p++;
            if (*p == '-') { sign = -1; p++; }
            if ((k = parse_hex(p, &v)) != 0) p += k;
            else while (*p >= '0' && *p <= '9') v = v * 10 + (uint64_t)(*p++ - '0');
            in->imm = sign * (int64_t)v;
        } else {
            in->has_index = 1;
        }
    }
    const char *close = strchr(p, ']');
    if (!close) return 1;
    p = close + 1;
    if (*p == '!') { in->pre = 1; return 1; }
    p = skip_ws(p);
    if (*p == ',') {                                     /* post-index: [xn], #imm */
        p = skip_ws(p + 1);
        if (*p == '#') {
            p++;
            if (*p == '-') { sign = -1; p++; }
            v = 0;
            if ((k = parse_hex(p, &v)) == 0) while (*p >= '0' && *p <= '9') v = v * 10 + (uint64_t)(*p++ - '0');
            in->imm = sign * (int64_t)v;
            in->post = 1;
        }
    }
    return 1;
}

/* (주어짐) "->  0x100000538 <+16>: ldr    x8, [x0, #0x18]"  또는  "    0x10000054c <+4>:  ldr ..."
 * → addr, is_pc(화살표 줄이면 1), text("; 주석" 앞까지, 뒤 공백 제거). 디스어셈블 줄이 아니면 0 */
int parse_disasm_line(const char *line, uint64_t *addr, int *is_pc, char *text, size_t n) {
    const char *p = line;
    *is_pc = 0;
    if (p[0] == '-' && p[1] == '>') { *is_pc = 1; p += 2; }
    p = skip_ws(p);
    int k = parse_hex(p, addr);
    if (!k) return 0;
    p += k;
    if (p[0] != ' ' || p[1] != '<') return 0;
    const char *c = strstr(p, ">:");
    if (!c) return 0;
    p = skip_ws(c + 2);
    const char *e = strchr(p, ';');
    copy_trim(text, n, p, e ? (size_t)(e - p) : strlen(p));
    return text[0] != '\0';
}

/* (주어짐) 덤프 전체 → dump_t. 인식한 줄 수를 돌려준다. 레지스터는 처음 나온 값을 쓴다. */
int parse_dump(const char *text, dump_t *d) {
    memset(d, 0, sizeof *d);
    int recognized = 0;
    char line[512];
    for (const char *p = text; *p;) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        copy_trim(line, sizeof line, p, len);
        p += len + (e ? 1 : 0);

        stop_t st; int idx; uint64_t v, a; int is_pc; char sym[128], tx[64];
        if (!d->have_stop && parse_stop_reason(line, &st)) { d->have_stop = 1; d->stop = st; recognized++; continue; }
        if (parse_reg_line(line, &idx, &v, sym, sizeof sym)) {
            if (!HAS(d, idx)) {
                d->reg[idx] = v; d->regmask |= 1ull << idx;
                if (idx == R_PC) snprintf(d->pc_sym, sizeof d->pc_sym, "%s", sym);
                if (idx == R_LR) snprintf(d->lr_sym, sizeof d->lr_sym, "%s", sym);
            }
            recognized++; continue;
        }
        if (parse_disasm_line(line, &a, &is_pc, tx, sizeof tx)) {
            if (d->ndis < MAXDIS) { d->dis_addr[d->ndis] = a; snprintf(d->dis_text[d->ndis], 64, "%s", tx); d->ndis++; }
            if (is_pc && !d->have_insn && parse_insn(tx, &d->insn)) { d->have_insn = 1; d->insn_addr = a; }
            recognized++; continue;
        }
        const char *s = strstr(line, ".__TEXT.__text");
        const char *br = strchr(line, '[');
        if (s && br && parse_hex(br + 1, &a)) {          /* [0x...-0x...) ... img.__TEXT.__text */
            const char *dash = strchr(br, '-');
            if (dash && parse_hex(dash + 1, &v)) { d->have_text = 1; d->text_lo = a; d->text_hi = v; recognized++; }
            continue;
        }
        int k = parse_hex(line, &a);                     /* memory read: "0xADDR: 0xVAL [0xVAL] [sym]" */
        if (k && line[k] == ':' && line[k + 1] == ' ') {
            const char *q = line + k + 2;
            int any = 0;
            for (;;) {
                q = skip_ws(q);
                int kk = parse_hex(q, &v);
                if (!kk || d->nmem >= MAXMEM) break;
                d->mem_addr[d->nmem] = a; d->mem_val[d->nmem] = v; d->mem_sym[d->nmem][0] = '\0';
                q += kk; a += 8; any = 1;
                if (*q == ' ' && q[1] != '0') {          /* -f A: 값 뒤 기호 */
                    snprintf(d->mem_sym[d->nmem], sizeof d->mem_sym[0], "%s", skip_ws(q));
                    d->nmem++; break;
                }
                d->nmem++;
            }
            if (any) recognized++;
        }
    }
    return recognized;
}

/* ================= Part B — 분류 ================= */
/* 규칙 (위에서부터 처음 맞는 것):
 *  1. stop reason 이 EXC_BAD_ACCESS 가 아니거나 pc 를 모르면 UNKNOWN
 *  2. code == 261 (EXC_ARM_PAC_FAIL, mach/arm/exception.h 0x105)          → PAC_FAIL
 *  3. pc 가 4의 배수가 아님 / __TEXT.__text 밖 / 채움 패턴                   → BAD_PC
 *  4. pc 의 명령이 메모리 접근이 아니거나 베이스 값 모름                      → UNKNOWN
 *  5. 베이스 값 또는 fault 주소가 채움 패턴(0x5555…/0xAAAA…)                 → SCRIBBLE
 *  6. 베이스 == 0 이고 fault 주소 == 유효주소(=즉치) 이고 < 0x10000          → NULL_MEMBER
 *  7. 그 밖                                                              → UNKNOWN */
crash_class_t classify(const dump_t *d) {
    /* TODO(B1) */
    if (!d->have_stop || strcmp(d->stop.exc, "EXC_BAD_ACCESS") != 0 || !HAS(d, R_PC)) return CR_UNKNOWN;
    if (d->stop.code == 261) return CR_PAC_FAIL;
    uint64_t pc = d->reg[R_PC];
    if ((pc & 3) != 0 || is_fill(pc) || (d->have_text && (pc < d->text_lo || pc >= d->text_hi)))
        return CR_BAD_PC;
    if (!d->have_insn || !d->insn.is_mem || !HAS(d, d->insn.base)) return CR_UNKNOWN;
    uint64_t b = d->reg[d->insn.base];
    if (is_fill(b) || (d->stop.have_addr && is_fill(d->stop.addr))) return CR_SCRIBBLE;
    if (b == 0 && d->stop.have_addr && !d->insn.has_index &&
        d->stop.addr == insn_ea(&d->insn, b) && d->stop.addr < 0x10000)
        return CR_NULL_MEMBER;
    return CR_UNKNOWN;
}

/* ================= Part C — 역추적 ================= */
typedef struct { const char *name; uint32_t off, size; } member_t;

/* off 를 포함하는 멤버 이름 (off ∈ [m.off, m.off+m.size)), 없으면 NULL */
const char *member_at(const member_t *tbl, size_t n, uint64_t off) {
    /* TODO(C1) */
    for (size_t i = 0; i < n; i++)
        if (off >= tbl[i].off && off < (uint64_t)tbl[i].off + tbl[i].size) return tbl[i].name;
    return NULL;
}

/* "누가 이 레지스터를 썼나": 디스어셈블 목록에서 주소 < pc 인 명령을 뒤에서부터 보며
 * reg 를 쓰는(dst/dst2, 또는 pre/post 쓰기 되돌림 베이스) 첫 명령의 인덱스. 없으면 -1.
 * (직선 코드 가정 — 분기 합류는 보지 않는다) */
int find_def(const dump_t *d, int reg, uint64_t pc) {
    /* TODO(C2) */
    for (int i = d->ndis - 1; i >= 0; i--) {
        if (d->dis_addr[i] >= pc) continue;
        insn_t in;
        if (!parse_insn(d->dis_text[i], &in)) continue;
        if (in.dst == reg || in.dst2 == reg || ((in.pre || in.post) && in.base == reg)) return i;
    }
    return -1;
}

/* 죽은 스택에서 "마지막 리턴 주소" 찾기: sp 보다 낮은 주소의 메모리 워드 중
 * 값이 __TEXT.__text 안이고 4의 배수인 것 가운데 sp 에 가장 가까운(주소가 가장 큰) 것의 인덱스. 없으면 -1 */
int stale_return_scan(const dump_t *d) {
    /* TODO(C3) */
    if (!d->have_text || !HAS(d, R_SP)) return -1;
    int best = -1;
    for (int i = 0; i < d->nmem; i++) {
        uint64_t v = d->mem_val[i];
        if (d->mem_addr[i] >= d->reg[R_SP] || (v & 3) || v < d->text_lo || v >= d->text_hi) continue;
        if (best < 0 || d->mem_addr[i] > d->mem_addr[best]) best = i;
    }
    return best;
}

/* (주어짐) "img`func + 16 at file.c:36:17" → func="func", fileline="file.c:36" (열 번호 제거) */
static void sym_split(const char *sym, char *func, size_t fn, char *fl, size_t fln) {
    func[0] = fl[0] = '\0';
    const char *b = strchr(sym, '`');
    if (b) {
        const char *e = b + 1;
        while (*e && *e != ' ' && *e != '(') e++;
        copy_trim(func, fn, b + 1, (size_t)(e - b - 1));
    }
    const char *at = strstr(sym, " at ");
    if (at) {
        const char *c1 = strchr(at + 4, ':');
        const char *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
        size_t len = c2 ? (size_t)(c2 - at - 4) : strlen(at + 4);
        copy_trim(fl, fln, at + 4, len);
    }
}

typedef enum { HINT_NONE, HINT_FILL, HINT_ZEROED } obj_hint_t;
typedef struct {
    crash_class_t cls;
    char     func[48], line[48];      /* 원인 함수 · 소스 라인 */
    char     caller[48];              /* lr 기호의 함수 (BAD_PC 면 비어 있을 수 있다) */
    int      base;                    /* 죽은 명령의 베이스 레지스터 */
    int64_t  fault_off;               /* 그 명령의 즉치 = 구조체 멤버 offset 후보 */
    int      def_idx;                 /* 베이스를 쓴 명령 (dis_text 인덱스, 없으면 -1) */
    int      def_base; int64_t def_off;
    int      base_is_arg;             /* 함수 안에서 정의 없음 + x0–x7 → 호출자가 넘긴 인자 */
    obj_hint_t hint;                  /* def 가 읽은 객체 메모리 모양 */
    int      lr_eq_pc;                /* 47비트 주소 마스크로 lr == pc → 깨진 LR 로 ret */
    int      stale_idx;               /* BAD_PC: 죽은 스택의 리턴 주소 (mem 인덱스) */
} report_t;

/* (주어짐) def 명령이 읽은 객체([def_base] 부근) 메모리 모양: 전부 채움 패턴 / +0x10 이후 전부 0 */
static obj_hint_t object_hint(const dump_t *d, uint64_t obj) {
    int n = 0, fill = 0, zero_after = 0, after = 0;
    for (int i = 0; i < d->nmem; i++) {
        if (d->mem_addr[i] < obj || d->mem_addr[i] >= obj + 0x30) continue;
        n++;
        if (is_fill(d->mem_val[i])) fill++;
        if (d->mem_addr[i] >= obj + 0x10) { after++; if (d->mem_val[i] == 0) zero_after++; }
    }
    if (n >= 2 && fill == n) return HINT_FILL;
    if (after >= 2 && zero_after == after) return HINT_ZEROED;
    return HINT_NONE;
}

/* (주어짐) 분류 + 근거 모으기 */
void triage(const dump_t *d, report_t *r) {
    memset(r, 0, sizeof *r);
    r->def_idx = r->stale_idx = -1; r->base = r->def_base = -1;
    r->cls = classify(d);
    char tmp[48];
    sym_split(d->lr_sym, r->caller, sizeof r->caller, tmp, sizeof tmp);
    const uint64_t VA47 = (1ull << 47) - 1;            /* lldb: "Number of bits used in addressing (code): 47" */
    r->lr_eq_pc = HAS(d, R_LR) && HAS(d, R_PC) && ((d->reg[R_LR] ^ d->reg[R_PC]) & VA47) == 0;
    if (r->cls == CR_BAD_PC) {
        r->stale_idx = stale_return_scan(d);
        if (r->stale_idx >= 0)
            sym_split(d->mem_sym[r->stale_idx], r->func, sizeof r->func, r->line, sizeof r->line);
        return;
    }
    sym_split(d->pc_sym, r->func, sizeof r->func, r->line, sizeof r->line);
    if (r->cls != CR_NULL_MEMBER && r->cls != CR_SCRIBBLE) return;
    r->base = d->insn.base; r->fault_off = d->insn.imm;
    r->def_idx = find_def(d, r->base, d->insn_addr);
    if (r->def_idx < 0) { r->base_is_arg = r->base <= 7; return; }
    insn_t def;
    if (parse_insn(d->dis_text[r->def_idx], &def) && def.is_mem) {
        r->def_base = def.base; r->def_off = def.imm;
        if (HAS(d, def.base)) r->hint = object_hint(d, d->reg[def.base]);
    }
}

static void print_report(const char *name, const dump_t *d, const report_t *r) {
    static const char *const HN[] = {"-", "fill(0x55/0xAA)", "zeroed"};
    printf("[%s] %-11s func=%s line=%s caller=%s", name, CLASS_NAME[r->cls],
           r->func[0] ? r->func : "?", r->line[0] ? r->line : "?", r->caller[0] ? r->caller : "?");
    if (r->cls == CR_NULL_MEMBER || r->cls == CR_SCRIBBLE) {
        printf(" base=x%d off=0x%llx", r->base, (unsigned long long)r->fault_off);
        if (r->def_idx >= 0) printf(" def=\"%s\" hint=%s", d->dis_text[r->def_idx], HN[r->hint]);
        else if (r->base_is_arg) printf(" def=(argument from caller)");
    }
    if (r->cls == CR_BAD_PC) {
        printf(" lr==pc:%s", r->lr_eq_pc ? "yes" : "no");
        if (r->stale_idx >= 0) printf(" stale_ra=0x%llx@0x%llx", (unsigned long long)d->mem_val[r->stale_idx],
                                      (unsigned long long)d->mem_addr[r->stale_idx]);
    }
    printf("\n");
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static int streq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }
static void part_done(const char *name, int *p0, int *t0) {
    printf("%s: %d/%d %s\n", name, g_pass - *p0, g_total - *t0, (g_pass - *p0 == g_total - *t0) ? "PASS" : "FAIL");
    *p0 = g_pass; *t0 = g_total;
}

/* 데모 구조체의 축약 복사본 (offsetof 로 멤버 표를 만든다) */
struct sensor { uint32_t id, flags; const char *name; int32_t last_mC; uint32_t pad;
                int32_t (*read)(void); uint64_t sample_count; };
struct dma_ops { void (*complete)(uint32_t); };
struct dma_req { uint64_t src, dst; uint32_t len, status; const struct dma_ops *ops; uint64_t cookie; };
_Static_assert(offsetof(struct sensor, read) == 0x18, "sensor.read @0x18");
_Static_assert(offsetof(struct dma_req, ops) == 0x18, "dma_req.ops @0x18");
#define M(T, f) {#f, (uint32_t)offsetof(struct T, f), (uint32_t)sizeof(((struct T *)0)->f)}
static const member_t SENSOR_T[] = {M(sensor, id), M(sensor, flags), M(sensor, name), M(sensor, last_mC),
                                    M(sensor, pad), M(sensor, read), M(sensor, sample_count)};
static const member_t DMA_REQ_T[] = {M(dma_req, src), M(dma_req, dst), M(dma_req, len), M(dma_req, status),
                                     M(dma_req, ops), M(dma_req, cookie)};
static const member_t DMA_OPS_T[] = {M(dma_ops, complete)};
#define NT(t) (sizeof t / sizeof t[0])

/* 합성 덤프 (실제 덤프의 줄 모양을 그대로 따라 만든 편집본 — 경계 사례용) */
#define SEC "  0x01 code [0x00000001000004f8-0x00000001000005c8)  r-x  0x4f8 0xd0 0x80000400 a.out.__TEXT.__text\n"
#define STOP18 "* thread #1, queue = 'q', stop reason = EXC_BAD_ACCESS (code=1, address=0x18)\n"

int main(int argc, char **argv) {
    init_dir(argc, argv);
    int p0 = 0, t0 = 0;
    char sym[128]; int idx; uint64_t v; stop_t st; insn_t in;

    /* ---- Part A: 한 줄 파서 ---- */
    CHECK(parse_reg_line("        x0 = 0x0000000000000000", &idx, &v, sym, sizeof sym) == 1 && idx == 0 && v == 0 && sym[0] == 0);
    CHECK(parse_reg_line("        lr = 0x000000010000059c  ARM-M14_null`main + 64 at demo_crash_null.c:45:5",
                         &idx, &v, sym, sizeof sym) == 1 && idx == R_LR && v == 0x10000059c &&
          streq(sym, "ARM-M14_null`main + 64 at demo_crash_null.c:45:5"));
    CHECK(parse_reg_line("     x16 = 0x516c00018fb635e4 (0x000000018fb635e4) libsystem_kernel.dylib`__error",
                         &idx, &v, sym, sizeof sym) == 1 && idx == 16 && v == 0x516c00018fb635e4ull &&
          streq(sym, "libsystem_kernel.dylib`__error"));
    CHECK(parse_reg_line("     far = 0x4242424242424242", &idx, &v, sym, sizeof sym) == 1 && idx == R_FAR && v == 0x4242424242424242ull);
    CHECK(parse_reg_line("      cpsr = 0x80000000", &idx, &v, sym, sizeof sym) == 1 && idx == R_CPSR);
    CHECK(parse_reg_line("General Purpose Registers:", &idx, &v, sym, sizeof sym) == 0);
    CHECK(parse_reg_line("(lldb) register read far esr", &idx, &v, sym, sizeof sym) == 0);
    CHECK(parse_reg_line("Process 9170 exited with status = 9 (0x00000009) killed", &idx, &v, sym, sizeof sym) == 0);
    CHECK(parse_reg_line("        x0 = 0xZZ", &idx, &v, sym, sizeof sym) == 0);
    CHECK(parse_reg_line("       x31 = 0x0000000000000001", &idx, &v, sym, sizeof sym) == 0);

    CHECK(parse_stop_reason("* thread #1, queue = 'com.apple.main-thread', stop reason = EXC_BAD_ACCESS (code=1, address=0x18)", &st) == 1
          && streq(st.exc, "EXC_BAD_ACCESS") && st.code == 1 && st.have_addr && st.addr == 0x18);
    CHECK(parse_stop_reason("* thread #1, stop reason = EXC_BAD_ACCESS (code=257, address=0x4242424242424242)", &st) == 1
          && st.code == 257 && st.addr == 0x4242424242424242ull);
    CHECK(parse_stop_reason("* thread #1, stop reason = EXC_BAD_ACCESS (code=261, address=0xd65f0fff)", &st) == 1
          && st.code == 261 && st.addr == 0xd65f0fff);
    CHECK(parse_stop_reason("* thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.1", &st) == 1
          && streq(st.exc, "breakpoint") && st.code == -1 && st.have_addr == 0);
    CHECK(parse_stop_reason("    frame #0: 0x0000000100000538 ARM-M14_null`poll_sensor", &st) == 0);

    CHECK(parse_insn("ldr    x8, [x0, #0x18]", &in) && streq(in.mnem, "ldr") && in.dst == 8 && in.is_mem && in.base == 0 && in.imm == 0x18 && !in.pre && !in.post);
    CHECK(parse_insn("ldr    x1, [x8]", &in) && in.dst == 1 && in.base == 8 && in.imm == 0);
    CHECK(parse_insn("ldrb   w2, [x1, #0x1]!", &in) && in.dst == 2 && in.base == 1 && in.imm == 1 && in.pre);
    CHECK(parse_insn("ldr    x0, [x1], #0x8", &in) && in.dst == 0 && in.base == 1 && in.imm == 8 && in.post && insn_ea(&in, 0x100) == 0x100);
    CHECK(parse_insn("ldp    x29, x30, [sp, #0x20]", &in) && in.dst == 29 && in.dst2 == 30 && in.base == R_SP && in.imm == 0x20);
    CHECK(parse_insn("ldur   x8, [x29, #-0x8]", &in) && in.base == 29 && in.imm == -8);
    CHECK(parse_insn("ldr    x8, [x0, x1, lsl #3]", &in) && in.base == 0 && in.has_index && in.imm == 0);
    CHECK(parse_insn("str    w0, [x19, #0x10]", &in) && in.dst == -1 && in.base == 19 && in.imm == 0x10);
    CHECK(parse_insn("stp    x20, x19, [sp, #-0x20]!", &in) && in.dst == -1 && in.base == R_SP && in.imm == -0x20 && in.pre);
    CHECK(parse_insn("ldrsb  x8, [sp, #0x10]", &in) && streq(in.mnem, "ldrsb") && in.dst == 8 && in.base == R_SP);
    CHECK(parse_insn("blr    x8", &in) && in.dst == -1 && !in.is_mem && in.base == -1);
    CHECK(parse_insn("retab", &in) && streq(in.mnem, "retab") && in.dst == -1 && !in.is_mem);
    CHECK(parse_insn("mov    x19, x0", &in) && in.dst == 19 && !in.is_mem);
    CHECK(parse_insn("", &in) == 0);

    uint64_t a; int ispc; char tx[64];
    CHECK(parse_disasm_line("->  0x100000538 <+16>: ldr    x8, [x0, #0x18]", &a, &ispc, tx, sizeof tx) && a == 0x100000538 && ispc && streq(tx, "ldr    x8, [x0, #0x18]"));
    CHECK(parse_disasm_line("    0x1000004e4 <+28>: bl     0x1000004b0               ; copy_name at demo_crash_stack.c:18:5",
                            &a, &ispc, tx, sizeof tx) && !ispc && streq(tx, "bl     0x1000004b0"));
    CHECK(parse_disasm_line("0x16fdfea28: 0x00000001000004f8 ARM-M14_stack`set_label + 48", &a, &ispc, tx, sizeof tx) == 0);
    part_done("Part A (line parsers)", &p0, &t0);

    /* ---- Part B: 실제 덤프 파싱 + 분류, 깨진/합성 덤프 ---- */
    static const char *const FILES[] = {"crash_null.txt", "crash_stack.txt", "crash_stack_arm64e.txt",
                                        "crash_uaf.txt", "crash_uaf_zero.txt"};
    static const crash_class_t EXPECT[] = {CR_NULL_MEMBER, CR_BAD_PC, CR_PAC_FAIL, CR_SCRIBBLE, CR_NULL_MEMBER};
    static dump_t D[5];
    for (int i = 0; i < 5; i++) {
        char *t = load_file(FILES[i]);
        int n = parse_dump(t, &D[i]);
        free(t);
        CHECK(n > 5);
        CHECK(classify(&D[i]) == EXPECT[i]);
    }
    const dump_t *dn = &D[0], *ds = &D[1], *du = &D[3];
    CHECK(dn->reg[R_PC] == 0x100000538 && dn->reg[R_LR] == 0x10000059c && HAS(dn, R_FP) && (dn->reg[R_SP] & 15) == 0);
    CHECK(dn->stop.addr == 0x18 && dn->reg[R_FAR] == 0x18 && dn->reg[R_ESR] == 0x92000006);
    CHECK(dn->have_insn && dn->insn.base == 0 && dn->insn.imm == 0x18 && dn->reg[0] == 0);
    CHECK(dn->have_text && dn->text_lo == 0x1000004f8 && dn->text_hi == 0x1000005c8);
    CHECK(ds->reg[R_PC] == 0x0000424242424242ull && ds->reg[R_LR] == 0x4242424242424242ull && ds->stop.code == 257);
    CHECK(ds->have_insn == 0 && ds->nmem >= 12);         /* pc 를 디스어셈블할 수 없다 */
    CHECK(du->insn.base == 8 && du->reg[8] == 0x5555555555555555ull && du->stop.addr == 0x5555555555555555ull);

    struct { const char *txt; crash_class_t want; } SYN[] = {
        {"", CR_UNKNOWN},
        {"hello\nworld\n", CR_UNKNOWN},
        {STOP18, CR_UNKNOWN},                                                     /* 잘린 덤프: pc 없음 */
        {"* thread #1, stop reason = breakpoint 1.1\n        pc = 0x0000000100000538\n", CR_UNKNOWN},
        {STOP18 "        pc = 0x0000000100000539\n" SEC, CR_BAD_PC},              /* 4의 배수 아님 */
        {STOP18 "        pc = 0x0000000200000000\n" SEC, CR_BAD_PC},              /* __text 밖 */
        {STOP18 "        x0 = 0x0000000000001000\n        pc = 0x0000000100000538\n" SEC
                "->  0x100000538 <+16>: ldr    x8, [x0, #0x18]\n", CR_UNKNOWN},   /* 베이스가 0 아님 */
        {"* thread #1, stop reason = EXC_BAD_ACCESS (code=1, address=0x20)\n        x0 = 0x0000000000000000\n"
                "        pc = 0x0000000100000538\n->  0x100000538 <+16>: ldr    x8, [x0, #0x18]\n", CR_UNKNOWN},  /* 주소 불일치 */
        {STOP18 "        x0 = 0x0000000000000000\n        pc = 0x0000000100000538\n", CR_UNKNOWN},  /* 디스어셈블 없음 */
        {"* thread #1, stop reason = EXC_BAD_ACCESS (code=1, address=0x0)\n        x2 = 0x0000000000000000\n"
                "        pc = 0x0000000100000540\n->  0x100000540 <+0>: ldr    x9, [x2], #0x8\n", CR_NULL_MEMBER},  /* post-index */
        {"* thread #1, stop reason = EXC_BAD_ACCESS (code=1, address=0x10)\n        x2 = 0x0000000000000000\n"
                "        pc = 0x0000000100000540\n->  0x100000540 <+0>: ldr    x9, [x2, #0x10]!\n", CR_NULL_MEMBER},
        {"* thread #1, stop reason = EXC_BAD_ACCESS (code=1, address=0xaaaaaaaaaaaaaab2)\n        x3 = 0xaaaaaaaaaaaaaaaa\n"
                "        pc = 0x0000000100000540\n->  0x100000540 <+0>: ldrh   w9, [x3, #0x8]\n", CR_SCRIBBLE},     /* PreScribble */
        {"* thread #1, stop reason = EXC_BAD_ACCESS (code=1, address=0x0)\n        x0 = 0x0000000000000000\n"
                "        pc = 0x0000000100000540\n->  0x100000540 <+0>: ldr    x9, [x0, x1, lsl #3]\n", CR_UNKNOWN},  /* 인덱스 미상 */
    };
    for (size_t i = 0; i < sizeof SYN / sizeof SYN[0]; i++) {
        dump_t t;
        parse_dump(SYN[i].txt, &t);
        if (classify(&t) != SYN[i].want) printf("  synthetic #%zu -> %s\n", i, CLASS_NAME[classify(&t)]);
        CHECK(classify(&t) == SYN[i].want);
    }
    dump_t t0d;
    CHECK(parse_dump("", &t0d) == 0 && parse_dump("hello\nworld\n", &t0d) == 0);
    part_done("Part B (parse_dump + classify)", &p0, &t0);

    /* ---- Part C: 역추적 리포트 ---- */
    report_t R[5];
    for (int i = 0; i < 5; i++) triage(&D[i], &R[i]);
    CHECK(member_at(SENSOR_T, NT(SENSOR_T), 0x18) && streq(member_at(SENSOR_T, NT(SENSOR_T), 0x18), "read"));
    CHECK(streq(member_at(SENSOR_T, NT(SENSOR_T), 0x1c), "read") && streq(member_at(SENSOR_T, NT(SENSOR_T), 0x14), "pad"));
    CHECK(member_at(SENSOR_T, NT(SENSOR_T), 0x28) == NULL && member_at(DMA_OPS_T, NT(DMA_OPS_T), 8) == NULL);
    /* 1) NULL 멤버: poll_sensor 36행, s->read, x0 은 인자 → 호출자 main 이 NULL 을 넘김 */
    CHECK(R[0].cls == CR_NULL_MEMBER && streq(R[0].func, "poll_sensor") && streq(R[0].line, "demo_crash_null.c:36"));
    CHECK(streq(member_at(SENSOR_T, NT(SENSOR_T), (uint64_t)R[0].fault_off), "read") && R[0].def_idx == -1 && R[0].base_is_arg);
    CHECK(streq(R[0].caller, "main") && !R[0].lr_eq_pc);
    /* 2) 스택 오버런: pc 쓰레기, lr == pc (47비트), 죽은 스택의 마지막 리턴 주소 = set_label + 48 */
    CHECK(R[1].cls == CR_BAD_PC && R[1].lr_eq_pc && R[1].stale_idx >= 0);
    CHECK(streq(R[1].func, "set_label") && streq(R[1].line, "demo_crash_stack.c:29"));
    CHECK(R[1].stale_idx >= 0 && D[1].mem_val[R[1].stale_idx] == 0x1000004f8);
    /* 3) arm64e: retab 에서 바로 멈춤 → 함수가 곧 범인 */
    CHECK(R[2].cls == CR_PAC_FAIL && streq(R[2].func, "set_label") && streq(R[2].line, "demo_crash_stack.c:29"));
    /* 4) UAF + Scribble: x8 = r->ops 를 해제된 객체에서 읽음 */
    CHECK(R[3].cls == CR_SCRIBBLE && streq(R[3].func, "finish_req") && streq(R[3].line, "demo_crash_uaf.c:32"));
    CHECK(R[3].def_idx >= 0 && R[3].def_base == 0 && streq(member_at(DMA_REQ_T, NT(DMA_REQ_T), (uint64_t)R[3].def_off), "ops"));
    CHECK(R[3].hint == HINT_FILL && streq(R[3].caller, "main"));
    /* 5) UAF + zero-on-free: 겉보기엔 NULL 멤버(ops->complete), def 체인을 따라가면 0 으로 지워진 객체 */
    CHECK(R[4].cls == CR_NULL_MEMBER && streq(member_at(DMA_OPS_T, NT(DMA_OPS_T), (uint64_t)R[4].fault_off), "complete"));
    CHECK(R[4].def_idx >= 0 && streq(member_at(DMA_REQ_T, NT(DMA_REQ_T), (uint64_t)R[4].def_off), "ops") && R[4].hint == HINT_ZEROED);
    {   /* find_def: 목록 밖 레지스터 / pc 이후 정의는 무시 */
        CHECK(find_def(&D[0], 19, D[0].insn_addr) >= 0 && streq(D[0].dis_text[find_def(&D[0], 19, D[0].insn_addr)], "mov    x19, x0"));
        CHECK(find_def(&D[0], 8, D[0].insn_addr) == -1);       /* x8 은 pc 의 명령이 쓴다 (아직 안 씀) */
    }
    part_done("Part C (triage report)", &p0, &t0);

    for (int i = 0; i < 5; i++) print_report(FILES[i], &D[i], &R[i]);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
