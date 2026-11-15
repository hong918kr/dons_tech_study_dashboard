/* ARM-B12 추가 연습 — IT 블록 검사, 매핑 심볼 없는 선형 분해, A32 조건 필드, -O2/-Os/-Oz 표.
 * 본문 정답의 파서(thumb_insn_len, parse_line, saving_permille)를 그대로 재사용한다.
 * 실행: 레슨 디렉터리에서 ./a.out  (또는 ./a.out <레슨 디렉터리>)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_dir[512] = ".";
static void init_dir(int argc, char **argv) {
    const char *f = __FILE__, *sl = strrchr(f, '/');
    if (argc > 1) snprintf(g_dir, sizeof g_dir, "%s", argv[1]);
    else if (sl) snprintf(g_dir, sizeof g_dir, "%.*s", (int)(sl - f), f);
}
static FILE *open_data(const char *name) {
    char p[640];
    snprintf(p, sizeof p, "%s/%s", g_dir, name);
    FILE *fp = fopen(p, "r");
    if (!fp) fp = fopen(name, "r");                         /* 현재 디렉터리 */
    if (!fp) {                                              /* 저장소 루트에서 실행한 경우 */
        snprintf(p, sizeof p, "lessons/ARM/ARM-B12/%s", name);
        fp = fopen(p, "r");
    }
    if (!fp) printf("cannot open %s (run in the lesson dir or pass it as argv[1])\n", p);
    return fp;
}

typedef enum { ISA_THUMB, ISA_A32, ISA_A64 } isa_t;
typedef struct { const char *name; isa_t isa; } target_t;
static const target_t TARGETS[] = {
    {"thumbv6m", ISA_THUMB}, {"thumbv7em", ISA_THUMB}, {"armv7a", ISA_A32}, {"aarch64", ISA_A64},
};
enum { NT = 4, MAXF = 16 };

static int hexdig(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* ---------------- Part A: Thumb 길이 규칙 + objdump 한 줄 파서 ---------------- */
/* hw1 = Thumb 명령의 첫 halfword (메모리에서 먼저 오는 16비트). 반환: 2 또는 4 */
unsigned thumb_insn_len(uint16_t hw1) {
    unsigned top5 = hw1 >> 11;                 /* [15:11] */
    return (top5 == 0x1D || top5 == 0x1E || top5 == 0x1F) ? 4u : 2u;   /* 11101/11110/11111 */
}

typedef enum { LN_OTHER, LN_FUNC, LN_INSN, LN_DATA } lkind_t;
typedef struct {
    lkind_t  kind;
    uint64_t addr;       /* FUNC / INSN / DATA */
    unsigned nbytes;     /* INSN / DATA: 이 줄의 바이트 수 */
    unsigned ngroups;    /* 16진 묶음 수 ("f8d0 c000" → 2, "e5d02000" → 1, "02 25 0b 1e" → 4) */
    uint32_t group0;     /* 첫 묶음 값 (Thumb: hw1, A32/A64: 명령 워드, 데이터: 첫 바이트) */
    char     mnem[24];   /* "ldr.w", "ittt", ".word", "<unknown>" */
    char     name[64];   /* FUNC: 함수 이름 */
} line_t;

/* 줄 종류를 돌려주고 *o 를 채운다. 형식 (llvm-objdump -dr):
 *   "00000054 <crc8>:"                              → LN_FUNC  (0열부터 주소)
 *   "      5a: b1a9         \tcbz\tr1, 0x88 ..."      → LN_INSN
 *   "      96: 02 25 0b 1e  \t.word\t0x1e0b2502"      → LN_DATA  (니모닉이 '.' 로 시작)
 *   "\t\t\t00000124:  R_ARM_THM_CALL\t__aeabi_uidiv" → LN_OTHER (리로케이션), 그 밖의 줄도 LN_OTHER */
int parse_line(const char *s, line_t *o) {
    memset(o, 0, sizeof *o);
    o->kind = LN_OTHER;
    const char *p = s;
    uint64_t a = 0;
    if (hexdig(*p) >= 0) {                          /* 함수 머리 */
        while (hexdig(*p) >= 0) a = a * 16 + (uint64_t)hexdig(*p++);
        if (p[0] != ' ' || p[1] != '<') return LN_OTHER;
        const char *nm = p + 2, *e = strchr(nm, '>');
        if (!e || e[1] != ':' || e == nm) return LN_OTHER;
        size_t n = (size_t)(e - nm);
        if (n >= sizeof o->name) n = sizeof o->name - 1;
        memcpy(o->name, nm, n);
        o->addr = a;
        return o->kind = LN_FUNC;
    }
    while (*p == ' ' || *p == '\t') p++;
    if (p == s || hexdig(*p) < 0) return LN_OTHER;
    while (hexdig(*p) >= 0) a = a * 16 + (uint64_t)hexdig(*p++);
    if (p[0] != ':' || p[1] != ' ') return LN_OTHER;
    p++;
    const char *tab = strchr(p, '\t');
    if (!tab) return LN_OTHER;
    unsigned digits = 0, groups = 0;
    uint32_t g0 = 0;
    while (p < tab) {                               /* 바이트 칸: 공백으로 나뉜 16진 묶음 */
        if (*p == ' ') { p++; continue; }
        unsigned len = 0;
        uint32_t v = 0;
        while (p < tab && hexdig(*p) >= 0) { v = v * 16 + (uint32_t)hexdig(*p++); len++; }
        if (len != 2 && len != 4 && len != 8) return LN_OTHER;    /* "R_ARM..." 등 */
        if (p < tab && *p != ' ') return LN_OTHER;
        if (groups++ == 0) g0 = v;
        digits += len;
    }
    if (groups == 0) return LN_OTHER;
    const char *m = tab + 1;
    while (*m == ' ' || *m == '\t') m++;
    size_t n = strcspn(m, " \t\r\n");
    if (n == 0) return LN_OTHER;
    if (n >= sizeof o->mnem) n = sizeof o->mnem - 1;
    memcpy(o->mnem, m, n);
    o->addr = a;
    o->nbytes = digits / 2;
    o->ngroups = groups;
    o->group0 = g0;
    return o->kind = (o->mnem[0] == '.') ? LN_DATA : LN_INSN;
}

/* small 이 big 보다 몇 ‰ 작은가: (big-small)*1000/big, 반올림(0.5 는 0에서 먼 쪽). big==0 → 0. 음수 가능 */
int saving_permille(unsigned small, unsigned big) {
    if (big == 0) return 0;
    long long num = ((long long)big - (long long)small) * 1000;
    long long b = big;
    return (int)(num >= 0 ? (2 * num + b) / (2 * b) : (2 * num - b) / (2 * b));
}

/* ================= 연습 1: IT 블록 검사 (thumbv7em) ================= */
/* "it"/"itt"/"ite"/"ittt"/"itete"... → 블록이 덮는 명령 수(1..4), IT 가 아니면 0.
 * 패턴 문자(T/E)는 "it" 뒤에 오는 글자들: 첫 명령은 항상 T(조건 그대로). */
int it_len(const char *mnem) {
    if (strncmp(mnem, "it", 2) != 0) return 0;
    int n = 1;
    for (const char *p = mnem + 2; *p; p++, n++)
        if ((*p != 't' && *p != 'e') || n == 4) return 0;
    return n;
}

static const char *const CONDS[16] = {"eq", "ne", "hs", "lo", "mi", "pl", "vs", "vc",
                                      "hi", "ls", "ge", "lt", "gt", "le", "al", "nv"};
static int cond_index(const char *c) {
    if (strcmp(c, "cs") == 0) return 2;
    if (strcmp(c, "cc") == 0) return 3;
    for (int i = 0; i < 14; i++) if (strcmp(c, CONDS[i]) == 0) return i;
    return -1;
}
/* 니모닉 끝의 조건 접미사 (".w"/".n" 는 떼고 마지막 두 글자) → 조건 번호, 없으면 -1 */
static int mnem_cond(const char *mnem) {
    char t[24];
    snprintf(t, sizeof t, "%s", mnem);
    size_t n = strlen(t);
    if (n >= 2 && t[n - 2] == '.' && (t[n - 1] == 'w' || t[n - 1] == 'n')) t[n -= 2] = '\0';
    return n > 2 ? cond_index(t + n - 2) : -1;
}

/* 덤프의 모든 IT 블록: 덮인 명령이 T 자리면 cond, E 자리면 cond^1 (반대 조건) 으로 끝나야 한다.
 * 반환: 위반 수. *nblk = IT 블록 수, *ncov = 덮인 명령 수 */
int check_it_blocks(FILE *fp, int *nblk, int *ncov) {
    char buf[512], pat[8] = "";
    line_t L;
    int bad = 0, left = 0, slot = 0, cond = -1;
    *nblk = *ncov = 0;
    while (fgets(buf, sizeof buf, fp)) {
        buf[strcspn(buf, "\r\n")] = '\0';
        if (parse_line(buf, &L) != LN_INSN) continue;
        if (left > 0) {                                   /* IT 블록 안 */
            int want = (slot == 0 || pat[slot - 1] == 't') ? cond : (cond ^ 1);
            if (mnem_cond(L.mnem) != want) bad++;
            (*ncov)++; slot++; left--;
            continue;
        }
        int n = it_len(L.mnem);
        if (n == 0) continue;
        const char *op = strstr(buf, L.mnem) + strlen(L.mnem);   /* 니모닉 뒤 피연산자 = 조건 */
        while (*op == ' ' || *op == '\t') op++;
        char c[4] = "";
        snprintf(c, sizeof c, "%.2s", op);
        cond = cond_index(c);
        if (cond < 0) bad++;
        snprintf(pat, sizeof pat, "%s", L.mnem + 2);      /* "tt", "e", ... */
        left = n; slot = 0;
        (*nblk)++;
    }
    return bad + (left != 0);
}

/* ================= 연습 2: 매핑 심볼 없이 "눈 감고" 쪼개기 ================= */
/* 덤프의 명령·데이터 줄 바이트를 메모리 순서로 이어 붙인다 (Thumb 묶음은 halfword → LE 2바이트).
 * starts[] 에 objdump 가 찍은 명령 시작 주소를 모은다. 반환: 바이트 수 */
static size_t load_bytes(const char *tname, uint8_t *mem, size_t cap, uint32_t *starts, int *nstart) {
    char fn[64], buf[512];
    line_t L;
    size_t n = 0;
    *nstart = 0;
    snprintf(fn, sizeof fn, "dump_%s.txt", tname);
    FILE *fp = open_data(fn);
    if (!fp) return 0;
    while (fgets(buf, sizeof buf, fp)) {
        buf[strcspn(buf, "\r\n")] = '\0';
        int k = parse_line(buf, &L);
        if (k != LN_INSN && k != LN_DATA) continue;
        if (L.addr != n || n + L.nbytes > cap) { fclose(fp); return 0; }   /* 연속이어야 */
        if (k == LN_INSN) starts[(*nstart)++] = (uint32_t)L.addr;
        const char *p = strchr(buf, ':') + 1;               /* 바이트 칸 다시 읽기 */
        for (unsigned g = 0; g < L.ngroups; g++) {
            unsigned v, len;
            while (*p == ' ') p++;
            len = (unsigned)strspn(p, "0123456789abcdef");
            sscanf(p, "%x", &v);
            p += len;
            if (len == 2) mem[n++] = (uint8_t)v;
            else { mem[n++] = (uint8_t)v; mem[n++] = (uint8_t)(v >> 8); }   /* 4자리 = halfword, LE */
        }
    }
    fclose(fp);
    return n;
}

/* 첫 바이트부터 thumb_insn_len 만으로 쪼갠다 ($d 무시). out[] = 시작 주소들. 반환: 개수 */
int blind_split(const uint8_t *code, size_t n, uint32_t *out, int max) {
    int k = 0;
    for (size_t pc = 0; pc + 2 <= n && k < max; ) {
        uint16_t hw1 = (uint16_t)(code[pc] | code[pc + 1] << 8);
        out[k++] = (uint32_t)pc;
        pc += thumb_insn_len(hw1);
    }
    return k;
}

/* objdump 명령 시작 중 blind_split 이 놓친 것 수 (첫 번째 놓친 주소 → *first) */
static int missed_starts(const uint32_t *ref, int nref, const uint32_t *got, int ngot, uint32_t *first) {
    int miss = 0, j = 0;
    *first = 0xFFFFFFFFu;
    for (int i = 0; i < nref; i++) {
        while (j < ngot && got[j] < ref[i]) j++;
        if (j == ngot || got[j] != ref[i]) { if (!miss++) *first = ref[i]; }
    }
    return miss;
}

/* ================= 연습 3: A32 조건 필드 ================= */
/* A32 는 모든 명령의 [31:28] 이 조건. cond != AL(0xE) 인 명령 수를 세고, 그 니모닉이 조건 접미사로
 * 끝나는지 검사. 반환: 위반 수. *nc = 조건부 명령 수, *nbr = 그중 분기(b<cond>) 수 */
int a32_cond_scan(FILE *fp, int *nc, int *nbr) {
    char buf[512];
    line_t L;
    int bad = 0;
    *nc = *nbr = 0;
    while (fgets(buf, sizeof buf, fp)) {
        buf[strcspn(buf, "\r\n")] = '\0';
        if (parse_line(buf, &L) != LN_INSN) continue;
        unsigned c = L.group0 >> 28;
        if (c >= 0xE) continue;                  /* AL, 또는 0xF = 무조건 전용 공간 */
        (*nc)++;
        if (L.mnem[0] == 'b' && strlen(L.mnem) == 3) (*nbr)++;      /* beq/bne/bhi ... */
        if (mnem_cond(L.mnem) != (int)c) bad++;
    }
    return bad;
}

/* ================= 연습 4: -O2 / -Os / -Oz 표 ================= */
typedef struct { char name[16]; unsigned o2, os, oz; } opt_row_t;
/* sizes.txt 의 "target O2 Os Oz" 표 → rows. 반환: 행 수 */
int load_opt_table(opt_row_t *rows, int max) {
    char buf[256];
    int n = 0, in = 0;
    FILE *fp = open_data("sizes.txt");
    if (!fp) return 0;
    while (fgets(buf, sizeof buf, fp) && n < max) {
        if (strncmp(buf, "target", 6) == 0) { in = 1; continue; }
        if (in && buf[0] == '#') break;
        if (in && sscanf(buf, "%15s %u %u %u", rows[n].name, &rows[n].o2, &rows[n].os, &rows[n].oz) == 4) n++;
    }
    fclose(fp);
    return n;
}

/* ---------------- 테스트 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(int argc, char **argv) {
    init_dir(argc, argv);

    /* 연습 1 */
    CHECK(it_len("it") == 1 && it_len("ite") == 2 && it_len("ittt") == 3 && it_len("itete") == 4);
    CHECK(it_len("itttte") == 0 && it_len("ldr") == 0 && it_len("itx") == 0);
    CHECK(mnem_cond("addne.w") == 1 && mnem_cond("eormi.w") == 4 && mnem_cond("moveq") == 0);
    CHECK(mnem_cond("bx") == -1 && mnem_cond("strbne") == 1 && mnem_cond("bhs") == 2);
    int nblk = 0, ncov = 0;
    FILE *fp = open_data("dump_thumbv7em.txt");
    int itbad = fp ? check_it_blocks(fp, &nblk, &ncov) : -1;
    if (fp) fclose(fp);
    printf("IT blocks (thumbv7em): %d blocks, %d covered insns, %d violations\n", nblk, ncov, itbad);
    CHECK(itbad == 0 && nblk == 4 && ncov == 6);

    /* 연습 2 */
    static uint8_t mem[4096];
    static uint32_t ref[2048], got[2048];
    const char *tt[2] = {TARGETS[1].name, TARGETS[0].name};   /* thumbv7em, thumbv6m */
    int miss[2];
    uint32_t first[2];
    for (int k = 0; k < 2; k++) {
        int nref;
        size_t n = load_bytes(tt[k], mem, sizeof mem, ref, &nref);
        int ngot = blind_split(mem, n, got, 2048);
        miss[k] = missed_starts(ref, nref, got, ngot, &first[k]);
        printf("blind split %-9s: %zu bytes, objdump %d insns, blind %d pieces, missed %d (first 0x%x)\n",
               tt[k], n, nref, ngot, miss[k], miss[k] ? first[k] : 0);
    }
    CHECK(miss[0] == 0);                         /* v7em: tbb 표 바이트가 우연히 16비트 2개로 읽힘 */
    CHECK(miss[1] == 1 && first[1] == 0xfc);     /* v6m: 리터럴 0xffff8000 의 0xffff 가 32비트 머리 */

    /* 연습 3 */
    int nc = 0, nbr = 0;
    fp = open_data("dump_armv7a.txt");
    int cbad = fp ? a32_cond_scan(fp, &nc, &nbr) : -1;
    if (fp) fclose(fp);
    printf("A32 conditional insns: %d (branches %d, others %d), suffix violations %d\n", nc, nbr, nc - nbr, cbad);
    CHECK(cbad == 0 && nc == 17 && nbr == 9);

    /* 연습 4 */
    opt_row_t r[8];
    int nr = load_opt_table(r, 8), worse = 0;
    for (int i = 0; i < nr; i++) {
        printf("%-10s O2 %5u  Os %5u  Oz %5u  (Os saves %d.%d%% vs O2)\n", r[i].name, r[i].o2, r[i].os, r[i].oz,
               saving_permille(r[i].os, r[i].o2) / 10, saving_permille(r[i].os, r[i].o2) % 10);
        if (r[i].oz > r[i].os) { worse++; CHECK(strcmp(r[i].name, "thumbv6m") == 0); }
        CHECK(r[i].o2 > r[i].os);
    }
    CHECK(nr == 4 && worse == 1);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
