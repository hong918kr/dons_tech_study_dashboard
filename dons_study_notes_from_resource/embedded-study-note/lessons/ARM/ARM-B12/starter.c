/* ARM-B12 Thumb-2 인코딩 관찰: 16비트 vs 32비트 명령
 * firmware.c 를 4개 타깃(-Os)으로 컴파일한 llvm-objdump -dr 텍스트(dump_*.txt)를 파싱해
 * 함수별 바이트 / 명령 수 / 2·4바이트 분포 / 데이터(리터럴 풀·분기표)를 세고, 타깃 비교 표를 만든다.
 * 데이터 파일(dump_*.txt, syms_*.txt, sizes.txt, thumb_sweep.txt)은 make_dumps.sh 가 만든다.
 * 실행: 레슨 디렉터리에서 ./a.out  (또는 ./a.out <레슨 디렉터리>)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- 데이터 파일 열기 (이 소스 파일 옆) ---------------- */
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
    (void)hw1;
    return 2u; /* TODO: hw1[15:11] 이 0b11101 / 0b11110 / 0b11111 이면 4 */
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
    (void)s; (void)hexdig;
    return LN_OTHER; /* TODO: 함수 머리 / 명령 / 데이터 줄 구분, 바이트 칸 16진 묶음 세기, 니모닉 복사 */
}

/* ---------------- Part B: 함수별 통계 ---------------- */
typedef struct {
    char     name[64];
    uint64_t start;
    unsigned bytes;       /* 명령 + 데이터 */
    unsigned insns;
    unsigned n16, n32;    /* 2바이트 / 4바이트 명령 수 */
    unsigned data;        /* .word/.short/.byte 바이트 (리터럴 풀, 분기표) */
    unsigned dotw;        /* 4바이트 명령 중 니모닉에 ".w" 가 찍힌 것 */
} func_t;
typedef struct {
    int nfunc;
    func_t f[MAXF];
    unsigned bytes, insns, n16, n32, data, dotw;    /* 합계 */
} dump_t;

/* dump 텍스트 전체를 읽어 *d 를 채운다. 반환: 함수 개수 (함수가 MAXF 초과면 -1) */
int analyze_dump(FILE *fp, dump_t *d) {
    memset(d, 0, sizeof *d);
    (void)fp;
    return 0; /* TODO: 줄마다 parse_line → 현재 함수에 누적, 마지막에 합계 */
}

/* ---------------- Part C: 비교 표 + 런타임 헬퍼 호출 ---------------- */
/* small 이 big 보다 몇 ‰ 작은가: (big-small)*1000/big, 반올림(0.5 는 0에서 먼 쪽). big==0 → 0. 음수 가능 */
int saving_permille(unsigned small, unsigned big) {
    (void)small; (void)big;
    return 0; /* TODO: (big-small)*1000/big 을 반올림 (음수 대칭), big==0 → 0 */
}

/* "\t\t\t00000124:  R_ARM_THM_CALL\t__aeabi_uidiv" → type="R_ARM_THM_CALL", sym="__aeabi_uidiv", 반환 1.
 * 리로케이션 줄이 아니면 0. */
int parse_reloc(const char *s, char type[32], char sym[64]) {
    (void)s; (void)type; (void)sym;
    return 0; /* TODO: 공백·탭 / 16진 주소 / ':' / "R_..." 타입 / 심볼 */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static int load_dump(const char *tname, dump_t *d) {
    char fn[64];
    snprintf(fn, sizeof fn, "dump_%s.txt", tname);
    FILE *fp = open_data(fn);
    if (!fp) { memset(d, 0, sizeof *d); return -1; }
    int n = analyze_dump(fp, d);
    fclose(fp);
    return n;
}

/* syms_<t>.txt (llvm-nm -S -n) 에서 함수 크기 → 같은 이름 함수의 bytes 와 비교. 불일치 수 반환 */
static int nm_mismatches(const char *tname, const dump_t *d, int *nsyms) {
    char fn[64], buf[256], nm[64], ty;
    unsigned long long val, sz;
    int bad = 0;
    *nsyms = 0;
    snprintf(fn, sizeof fn, "syms_%s.txt", tname);
    FILE *fp = open_data(fn);
    if (!fp) return -1;
    while (fgets(buf, sizeof buf, fp)) {
        if (sscanf(buf, "%llx %llx %c %63s", &val, &sz, &ty, nm) != 4 || ty != 'T') continue;
        (*nsyms)++;
        int found = 0;
        for (int i = 0; i < d->nfunc; i++)
            if (strcmp(d->f[i].name, nm) == 0) {
                found = 1;
                if (d->f[i].bytes != sz || d->f[i].start != val) bad++;
            }
        if (!found) bad++;
    }
    fclose(fp);
    return bad;
}

/* sizes.txt 의 "fw_<t>.o  :" 블록에서 .text 크기 */
static unsigned text_size(const char *tname) {
    char want[64], buf[256], sec[64];
    unsigned sz = 0, addr, in = 0;
    snprintf(want, sizeof want, "fw_%s.o", tname);
    FILE *fp = open_data("sizes.txt");
    if (!fp) return 0;
    while (fgets(buf, sizeof buf, fp)) {
        if (strncmp(buf, want, strlen(want)) == 0 && strstr(buf, " :")) in = 1;
        else if (in && sscanf(buf, "%63s %u %u", sec, &sz, &addr) == 3 && strcmp(sec, ".text") == 0) break;
        sz = 0;
    }
    fclose(fp);
    return sz;
}

/* thumb_sweep.txt 의 구간 요약과 thumb_insn_len 을 65536 값 전부 비교. 불일치 수 반환 */
static int sweep_mismatches(const char *tname, int *covered) {
    char buf[256], t[32];
    unsigned lo, hi, len;
    int bad = 0;
    *covered = 0;
    FILE *fp = open_data("thumb_sweep.txt");
    if (!fp) return -1;
    while (fgets(buf, sizeof buf, fp)) {
        if (sscanf(buf, "%31s %x %x %u", t, &lo, &hi, &len) != 4 || strcmp(t, tname) != 0) continue;
        for (unsigned h = lo; h <= hi && h <= 0xFFFF; h++, (*covered)++)
            if (thumb_insn_len((uint16_t)h) != len) bad++;
    }
    fclose(fp);
    return bad;
}

/* dump 안 모든 INSN 줄의 길이 검사: Thumb → thumb_insn_len(hw1) == nbytes, A32/A64 → 4 */
static int len_mismatches(const target_t *t, int *ninsn) {
    char fn[64], buf[512];
    line_t L;
    int bad = 0;
    *ninsn = 0;
    snprintf(fn, sizeof fn, "dump_%s.txt", t->name);
    FILE *fp = open_data(fn);
    if (!fp) return -1;
    while (fgets(buf, sizeof buf, fp)) {
        buf[strcspn(buf, "\r\n")] = '\0';
        if (parse_line(buf, &L) != LN_INSN) continue;
        (*ninsn)++;
        if (t->isa == ISA_THUMB) {
            if (L.ngroups * 2 != L.nbytes || thumb_insn_len((uint16_t)L.group0) != L.nbytes) bad++;
        } else if (L.nbytes != 4 || L.ngroups != 1) {
            bad++;
        }
    }
    fclose(fp);
    return bad;
}

/* dump 의 CALL 리로케이션 대상 목록 (컴파일러 런타임 헬퍼: __aeabi_uidiv 등) */
static int helper_calls(const char *tname, char out[][64], int max) {
    char fn[64], buf[512], type[32], sym[64];
    int n = 0;
    snprintf(fn, sizeof fn, "dump_%s.txt", tname);
    FILE *fp = open_data(fn);
    if (!fp) return -1;
    while (fgets(buf, sizeof buf, fp))
        if (parse_reloc(buf, type, sym) && strstr(type, "CALL") && n < max)
            memcpy(out[n++], sym, 64);
    fclose(fp);
    return n;
}

static const func_t *find_func(const dump_t *d, const char *name) {
    for (int i = 0; i < d->nfunc; i++)
        if (strcmp(d->f[i].name, name) == 0) return &d->f[i];
    return NULL;
}

static void print_tables(const dump_t d[NT]) {
    printf("\n%-16s %9s %9s %9s %9s %10s\n", "function (bytes)", "thumbv6m", "thumbv7em", "armv7a",
           "aarch64", "T2 vs A32");
    for (int i = 0; i < d[2].nfunc; i++) {
        const char *nm = d[2].f[i].name;
        unsigned b[NT];
        for (int k = 0; k < NT; k++) { const func_t *f = find_func(&d[k], nm); b[k] = f ? f->bytes : 0; }
        int pm = saving_permille(b[1], b[2]);
        printf("%-16s %9u %9u %9u %9u %+9.1f%%\n", nm, b[0], b[1], b[2], b[3], -pm / 10.0);
    }
    int pm = saving_permille(d[1].bytes, d[2].bytes);
    printf("%-16s %9u %9u %9u %9u %+9.1f%%\n", "TOTAL .text", d[0].bytes, d[1].bytes, d[2].bytes,
           d[3].bytes, -pm / 10.0);
    printf("\n%-10s %6s %6s %6s %6s %6s %6s %10s\n", "target", "bytes", "insns", "2B", "4B", "data",
           ".w", "avg B/insn");
    for (int k = 0; k < NT; k++) {
        unsigned code = d[k].bytes - d[k].data;
        printf("%-10s %6u %6u %6u %6u %6u %6u %10.2f\n", TARGETS[k].name, d[k].bytes, d[k].insns,
               d[k].n16, d[k].n32, d[k].data, d[k].dotw, d[k].insns ? (double)code / d[k].insns : 0.0);
    }
}

int main(int argc, char **argv) {
    init_dir(argc, argv);
    line_t L;

    /* Part A — 길이 규칙: 대표값 + 65536 전수 (objdump 스윕) */
    CHECK(thumb_insn_len(0x4770) == 2);   /* bx lr */
    CHECK(thumb_insn_len(0xbf00) == 2);   /* nop */
    CHECK(thumb_insn_len(0xe7fe) == 2);   /* b . (16비트 무조건 분기, top5 = 11100) */
    CHECK(thumb_insn_len(0xe800) == 4);   /* top5 = 11101: 첫 32비트 */
    CHECK(thumb_insn_len(0xe8df) == 4);   /* tbb */
    CHECK(thumb_insn_len(0xf8d0) == 4);   /* ldr.w */
    CHECK(thumb_insn_len(0xfa90) == 4);   /* qadd16 */
    CHECK(thumb_insn_len(0xf7ff) == 4);   /* bl */
    for (int k = 0; k < 2; k++) {
        int cov, bad = sweep_mismatches(TARGETS[k].name, &cov);
        if (bad) printf("  %s sweep: %d/%d mismatches\n", TARGETS[k].name, bad, cov);
        CHECK(bad == 0 && cov == 65536);
    }

    /* Part A — 한 줄 파서 (덤프에서 그대로 가져온 줄들) */
    CHECK(parse_line("00000054 <crc8>:", &L) == LN_FUNC && L.addr == 0x54 && strcmp(L.name, "crc8") == 0);
    CHECK(parse_line("00000000000001b4 <small_memcpy>:", &L) == LN_FUNC && L.addr == 0x1b4);
    CHECK(parse_line("      5a: b1a9         \tcbz\tr1, 0x88 <crc8+0x34>    @ imm = #0x2a", &L) == LN_INSN
          && L.addr == 0x5a && L.nbytes == 2 && L.group0 == 0xb1a9 && strcmp(L.mnem, "cbz") == 0);
    CHECK(parse_line("      42: eb00 020c    \taddne.w\tr2, r0, r12", &L) == LN_INSN
          && L.nbytes == 4 && L.ngroups == 2 && L.group0 == 0xeb00 && strcmp(L.mnem, "addne.w") == 0);
    CHECK(parse_line("      96: 02 25 0b 1e  \t.word\t0x1e0b2502", &L) == LN_DATA
          && L.nbytes == 4 && L.ngroups == 4 && strcmp(L.mnem, ".word") == 0);
    CHECK(parse_line("     14c: 03002004     \tmovweq\tr2, #0x4", &L) == LN_INSN
          && L.nbytes == 4 && L.group0 == 0x03002004u);
    CHECK(parse_line("     194: 528fffe9     \tmov\tw9, #0x7fff             // =32767", &L) == LN_INSN
          && L.addr == 0x194 && strcmp(L.mnem, "mov") == 0);
    CHECK(parse_line("\t\t\t00000124:  R_ARM_THM_CALL\t__aeabi_uidiv", &L) == LN_OTHER);
    CHECK(parse_line("fw_thumbv7em.o:\tfile format elf32-littlearm", &L) == LN_OTHER);
    CHECK(parse_line("Disassembly of section .text:", &L) == LN_OTHER);
    CHECK(parse_line("", &L) == LN_OTHER);

    /* Part A — 덤프의 모든 명령 줄: Thumb 은 규칙과 objdump 가 같은 길이, A32/A64 는 전부 4 */
    for (int k = 0; k < NT; k++) {
        int n, bad = len_mismatches(&TARGETS[k], &n);
        if (bad) printf("  %s: %d/%d length mismatches\n", TARGETS[k].name, bad, n);
        CHECK(bad == 0 && n > 50);
    }

    /* Part B — 함수별 통계: nm 크기 / .text 크기와 교차 검증 */
    dump_t d[NT];
    for (int k = 0; k < NT; k++) {
        int nf = load_dump(TARGETS[k].name, &d[k]), nsyms;
        CHECK(nf == 10);
        CHECK(nm_mismatches(TARGETS[k].name, &d[k], &nsyms) == 0 && nsyms == 10);
        CHECK(d[k].bytes == text_size(TARGETS[k].name) && d[k].bytes > 0);
        CHECK(d[k].n16 * 2 + d[k].n32 * 4 + d[k].data == d[k].bytes);
    }
    const func_t *f;
    CHECK((f = find_func(&d[1], "fsm_feed")) && f->data == 4);        /* tbb 분기표 4 B */
    CHECK((f = find_func(&d[0], "sat_add_s16")) && f->data == 8);     /* v6m 리터럴 풀 2 워드 */
    CHECK((f = find_func(&d[2], "fsm_feed")) && f->data == 16);       /* A32 ldr pc 분기표 4 워드 */
    CHECK(d[3].n16 == 0 && d[3].data == 0 && d[2].n16 == 0);          /* 고정 길이 ISA */
    CHECK(d[1].n32 > d[1].dotw);       /* 4바이트인데 ".w" 가 안 붙은 명령도 있다 (tbb, udiv, ldrb post) */

    /* Part C — 비교 표 + 런타임 헬퍼 */
    CHECK(saving_permille(320, 504) == 365);
    CHECK(saving_permille(1, 3) == 667 && saving_permille(2, 3) == 333);
    CHECK(saving_permille(1, 2) == 500 && saving_permille(3, 2) == -500);
    CHECK(saving_permille(320, 302) == -60 && saving_permille(7, 0) == 0);
    char ty[32], sy[64];
    CHECK(parse_reloc("\t\t\t00000124:  R_ARM_THM_CALL\t__aeabi_uidiv", ty, sy) == 1
          && strcmp(ty, "R_ARM_THM_CALL") == 0 && strcmp(sy, "__aeabi_uidiv") == 0);
    CHECK(parse_reloc("\t\t\t000000d0:  R_ARM_ABS32\t.text", ty, sy) == 1 && strcmp(sy, ".text") == 0);
    CHECK(parse_reloc("      5a: b1a9         \tcbz\tr1, 0x88", ty, sy) == 0);
    char calls[NT][4][64];
    int nc[NT];
    for (int k = 0; k < NT; k++) nc[k] = helper_calls(TARGETS[k].name, calls[k], 4);
    CHECK(nc[0] == 1 && strcmp(calls[0][0], "__aeabi_uidiv") == 0);   /* Cortex-M0: 나눗셈 명령 없음 */
    CHECK(nc[1] == 0);                                                 /* M4: udiv */
    CHECK(nc[2] == 1 && strcmp(calls[2][0], "__aeabi_uidiv") == 0);   /* 기본 armv7-a: udiv 없음 */
    CHECK(nc[3] == 0);
    CHECK(d[1].bytes < d[2].bytes && d[0].bytes < d[2].bytes);

    if (d[2].nfunc == 10) print_tables(d);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
