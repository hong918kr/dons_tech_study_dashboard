/* ARM-M14 추가 연습 — 크래시 덤프의 나머지 단서들
 *   1) ESR 디코더 (EC / IL / ISS, 데이터 abort 의 DFSC·WnR, FPAC 의 키)
 *   2) 순환 패턴으로 "버퍼 시작 → 저장된 LR" 거리 재기 (crash_pattern.txt)
 *   3) addr2line: 라인 테이블에서 pc / lr / lr-4 의 소스 줄 (linetable_*.txt)
 *   4) 포인터 정규화(47비트 VA, PAC 서명 떼기) + 레지스터 값의 ASCII 해석
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/ARM-M14_practice
 * 실행: 레슨 디렉터리에서 /tmp/ARM-M14_practice
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static char g_dir[512] = ".";
static char *load_file(const char *name) {
    char p[640];
    snprintf(p, sizeof p, "%s/%s", g_dir, name);
    FILE *fp = fopen(p, "r");
    if (!fp) fp = fopen(name, "r");
    if (!fp) { snprintf(p, sizeof p, "lessons/ARM/ARM-M14/%s", name); fp = fopen(p, "r"); }
    char *buf = calloc(1, 1 << 16);
    if (!buf) exit(2);
    if (fp) { size_t n = fread(buf, 1, (1 << 16) - 1, fp); buf[n] = '\0'; fclose(fp); }
    else printf("cannot open %s\n", name);
    return buf;
}
/* 덤프에서 "<name> = 0x..." 첫 값 (ARM-M14 parse_reg_line 의 축약판) */
static int dump_reg(const char *text, const char *name, uint64_t *v) {
    char key[16];
    snprintf(key, sizeof key, " %s = 0x", name);
    const char *p = strstr(text, key);
    if (!p) return 0;
    *v = strtoull(p + strlen(key), NULL, 16);
    return 1;
}

/* ================= 연습 1 — ESR 디코더 ================= */
typedef struct {
    unsigned ec, il; uint32_t iss;
    const char *kind;        /* EC 이름 */
    unsigned fsc;            /* abort: ISS[5:0] */
    const char *fsc_kind;    /* "translation" / "permission" / "access flag" / "address size" / "alignment" / "other" */
    int level;               /* fault 단계 0..3 (해당 없으면 -1) */
    int wnr;                 /* data abort: 1 = 쓰기 */
    int pac_data_key, pac_key_b;   /* EC 0x1C: ISS[1] (0 = 명령 키), ISS[0] (1 = B 키) */
} esr_t;

esr_t decode_esr(uint32_t esr) {
    esr_t e;
    memset(&e, 0, sizeof e);
    e.ec = esr >> 26; e.il = (esr >> 25) & 1; e.iss = esr & 0x1FFFFFF;
    e.level = -1; e.kind = "other"; e.fsc_kind = "";
    switch (e.ec) {
    case 0x15: e.kind = "SVC (AArch64)"; break;
    case 0x1C: e.kind = "PAC fail (FPAC)"; e.pac_data_key = (e.iss >> 1) & 1; e.pac_key_b = e.iss & 1; break;
    case 0x20: e.kind = "instruction abort, lower EL"; break;
    case 0x21: e.kind = "instruction abort, same EL"; break;
    case 0x22: e.kind = "PC alignment"; break;
    case 0x24: e.kind = "data abort, lower EL"; break;
    case 0x25: e.kind = "data abort, same EL"; break;
    case 0x26: e.kind = "SP alignment"; break;
    case 0x3C: e.kind = "BRK"; break;
    default: break;
    }
    if (e.ec == 0x20 || e.ec == 0x21 || e.ec == 0x24 || e.ec == 0x25) {
        e.fsc = esr & 0x3F;
        if (e.ec >= 0x24) e.wnr = (esr >> 6) & 1;
        unsigned grp = e.fsc >> 2;
        if (e.fsc == 0x21) e.fsc_kind = "alignment";
        else if (grp <= 3) {
            static const char *const G[] = {"address size", "translation", "access flag", "permission"};
            e.fsc_kind = G[grp]; e.level = (int)(e.fsc & 3);
        } else e.fsc_kind = "other";
    }
    return e;
}

/* ================= 연습 2 — 순환 패턴 ================= */
/* Metasploit pattern_create 형식: "Aa0Aa1…Aa9Ab0…" (대문자·소문자·숫자 3글자 묶음) */
void pattern_create(char *buf, size_t n) {
    size_t i = 0;
    for (char u = 'A'; u <= 'Z'; u++)
        for (char l = 'a'; l <= 'z'; l++)
            for (char d = '0'; d <= '9'; d++) {
                const char t[3] = {u, l, d};
                for (int k = 0; k < 3; k++) { if (i == n) goto done; buf[i++] = t[k]; }
            }
done:
    buf[i] = '\0';
}
/* 레지스터 값(리틀엔디언 8바이트, mask 로 알 수 있는 비트만)이 패턴의 몇 번째 바이트부터인가. 없으면 -1 */
long pattern_offset(const char *pat, uint64_t value, uint64_t mask) {
    size_t n = strlen(pat);
    for (size_t i = 0; i + 8 <= n; i++) {
        uint64_t w = 0;
        for (int k = 7; k >= 0; k--) w = (w << 8) | (uint8_t)pat[i + (size_t)k];
        if (((w ^ value) & mask) == 0) return (long)i;
    }
    return -1;
}

/* ================= 연습 3 — 라인 테이블 (addr2line) ================= */
typedef struct { uint64_t addr; int line, col; } lte_t;
/* "0x00000001000005cc: demo_crash_uaf.c:43:19" 줄들 → 표 (줄 번호 없는 항목은 line=0) */
int load_line_table(const char *text, lte_t *t, int max) {
    int n = 0;
    for (const char *p = text; *p && n < max;) {
        const char *e = strchr(p, '\n');
        if (p[0] == '0' && p[1] == 'x') {
            char *q;
            uint64_t a = strtoull(p, &q, 16);
            if (*q == ':') {
                const char *c1 = strchr(q + 1, ':');
                if (e && c1 > e) c1 = NULL;
                t[n].addr = a; t[n].line = c1 ? atoi(c1 + 1) : 0; t[n].col = 0;
                const char *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
                if (c2 && (!e || c2 < e)) t[n].col = atoi(c2 + 1);
                n++;
            }
        }
        p = e ? e + 1 : p + strlen(p);
    }
    return n;
}
/* addr 이하에서 가장 큰 주소의 항목 (표는 주소 오름차순). 없으면 NULL */
const lte_t *line_lookup(const lte_t *t, int n, uint64_t addr) {
    int lo = 0, hi = n - 1, best = -1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (t[mid].addr <= addr) { best = mid; lo = mid + 1; } else hi = mid - 1;
    }
    return best >= 0 ? &t[best] : NULL;
}

/* ================= 연습 4 — 포인터 정규화 + ASCII ================= */
static const uint64_t VA47 = (1ull << 47) - 1;   /* lldb: "Number of bits used in addressing (code): 47" */
uint64_t canon47(uint64_t v) { return v & VA47; }
int same_code_ptr(uint64_t a, uint64_t b) { return canon47(a) == canon47(b); }
/* 8바이트 전부 출력 가능한 ASCII(0x20..0x7e)면 리틀엔디언 순서 문자열로 out 에 쓰고 1 */
int ascii8(uint64_t v, char out[9]) {
    for (int k = 0; k < 8; k++) {
        unsigned c = (unsigned)(v >> (8 * k)) & 0xFF;
        if (c < 0x20 || c > 0x7e) { out[0] = '\0'; return 0; }
        out[k] = (char)c;
    }
    out[8] = '\0';
    return 1;
}

int main(int argc, char **argv) {
    const char *f = __FILE__, *sl = strrchr(f, '/');
    if (argc > 1) snprintf(g_dir, sizeof g_dir, "%s", argv[1]);
    else if (sl) snprintf(g_dir, sizeof g_dir, "%.*s", (int)(sl - f), f);

    /* ---- 연습 1: 실제 덤프의 esr ---- */
    static const char *const FILES[] = {"crash_null.txt", "crash_stack.txt", "crash_stack_arm64e.txt", "crash_uaf.txt"};
    uint64_t esr[4] = {0};
    for (int i = 0; i < 4; i++) { char *t = load_file(FILES[i]); CHECK(dump_reg(t, "esr", &esr[i])); free(t); }
    esr_t e = decode_esr((uint32_t)esr[0]);                       /* NULL+0x18 읽기 */
    CHECK(esr[0] == 0x92000006 && e.ec == 0x24 && e.il == 1 && strcmp(e.fsc_kind, "translation") == 0 && e.level == 2 && e.wnr == 0);
    e = decode_esr((uint32_t)esr[1]);                             /* ret → 0x4242…42 */
    CHECK(esr[1] == 0x8a000000 && e.ec == 0x22 && strcmp(e.kind, "PC alignment") == 0);
    e = decode_esr((uint32_t)esr[2]);                             /* arm64e retab */
    CHECK(esr[2] == 0x72000001 && e.ec == 0x1C && e.pac_data_key == 0 && e.pac_key_b == 1);
    e = decode_esr((uint32_t)esr[3]);                             /* 0x5555… 읽기 */
    CHECK(esr[3] == 0x92000004 && e.ec == 0x24 && e.level == 0 && strcmp(e.fsc_kind, "translation") == 0);
    e = decode_esr(0x92000007);                                   /* ARM-B09 가드 페이지 */
    CHECK(e.level == 3 && e.wnr == 0);
    e = decode_esr(0x92000046);                                   /* 합성: 같은 fault 의 쓰기 판 */
    CHECK(e.wnr == 1 && e.level == 2);
    e = decode_esr(0x9200000F);                                   /* 합성: permission fault L3 */
    CHECK(strcmp(e.fsc_kind, "permission") == 0 && e.level == 3);
    e = decode_esr(0x56000080);                                   /* svc #0x80 */
    CHECK(e.ec == 0x15 && e.iss == 0x80 && e.level == -1);

    /* ---- 연습 2: 순환 패턴 ---- */
    char pat[64];
    pattern_create(pat, 48);
    CHECK(strcmp(pat, "Aa0Aa1Aa2Aa3Aa4Aa5Aa6Aa7Aa8Aa9Ab0Ab1Ab2Ab3Ab4Ab5") == 0);   /* demo_crash_stack.c g_pat */
    char *tp = load_file("crash_pattern.txt");
    uint64_t lr = 0, fp = 0, pc = 0;
    CHECK(dump_reg(tp, "lr", &lr) && dump_reg(tp, "fp", &fp) && dump_reg(tp, "pc", &pc));
    free(tp);
    CHECK(lr == 0x6241396141386141ull && pattern_offset(pat, lr, ~0ull) == 24);   /* 버퍼 16 B + 저장 x29 8 B */
    CHECK(pattern_offset(pat, fp, ~0ull) == -1 && pattern_offset(pat, fp, VA47) == 16);   /* lldb 가 상위 비트를 지운 값 */
    CHECK(pattern_offset(pat, pc, VA47) == 24);
    char big[2048];
    pattern_create(big, 2000);
    CHECK(strlen(big) == 2000 && pattern_offset(big, 0x5a5a5a5a5a5a5a5aull /* "ZZZZZZZZ": 없는 값 */, ~0ull) == -1);
    {   /* 패턴 안의 모든 8바이트 창은 서로 다르다 (2000 B 까지) → 오프셋이 유일 */
        int uniq = 1;
        for (size_t i = 0; i + 8 <= 2000 && uniq; i += 7) {
            uint64_t w = 0;
            for (int k = 7; k >= 0; k--) w = (w << 8) | (uint8_t)big[i + (size_t)k];
            if (pattern_offset(big, w, ~0ull) != (long)i) uniq = 0;
        }
        CHECK(uniq);
    }

    /* ---- 연습 3: addr2line ---- */
    static lte_t T[64], U[64];
    char *tn = load_file("linetable_null.txt"), *tu = load_file("linetable_uaf.txt");
    int nn = load_line_table(tn, T, 64), nu = load_line_table(tu, U, 64);
    free(tn); free(tu);
    CHECK(nn == 24 && nu == 24);
    const lte_t *x = line_lookup(T, nn, 0x100000538);              /* NULL 크래시 pc */
    CHECK(x && x->line == 36 && x->col == 17);
    x = line_lookup(U, nu, 0x1000005d0);                            /* UAF lr: 다음 줄(printf) */
    CHECK(x && x->line == 44);
    x = line_lookup(U, nu, 0x1000005d0 - 4);                        /* lr-4 = bl finish_req: 진짜 호출 줄 */
    CHECK(x && x->line == 43 && x->col == 19);
    x = line_lookup(T, nn, 0x10000059c - 4);                        /* NULL lr-4: 45행 38열 (poll_sensor 호출) */
    CHECK(x && x->line == 45 && x->col == 38);
    x = line_lookup(U, nu, 0x10000057c);                            /* 줄 번호 없는 항목 (컴파일러 생성) */
    CHECK(x && x->line == 0);
    CHECK(line_lookup(U, nu, 0x100000000) == NULL);

    /* ---- 연습 4: 정규화 + ASCII ---- */
    CHECK(canon47(0x516c00018fb635e4ull) == 0x18fb635e4ull);       /* arm64e x16 의 lldb 표시 (0x000000018fb635e4) */
    CHECK(same_code_ptr(0x4242424242424242ull, 0x0000424242424242ull));   /* crash_stack: lr vs pc */
    CHECK(!same_code_ptr(0x10000059c, 0x100000538));
    char s8[9];
    CHECK(ascii8(0x4242424242424242ull, s8) && strcmp(s8, "BBBBBBBB") == 0);
    CHECK(ascii8(0x6241396141386141ull, s8) && strcmp(s8, "Aa8Aa9Ab") == 0);
    CHECK(ascii8(0x00000001000004f8ull, s8) == 0 && s8[0] == '\0');
    CHECK(ascii8(0x532d373131504d54ull, s8) && strcmp(s8, "TMP117-S") == 0);   /* 죽은 스택의 label 앞 8 B */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
