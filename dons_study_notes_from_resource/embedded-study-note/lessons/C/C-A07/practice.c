/* C-A07 추가 연습 — 한 번에 검증
 *   cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 *
 * 1) 2단(nested) X-macro: 레지스터 목록 안에 필드 목록 → 마스크 enum, 겹침 검사, 필드 디코더
 * 2) CLI 명령 표: 프로토타입·디스패치 표·help 를 한 목록에서 (-DP2_MISSING: 핸들러 없는 명령 → 빌드 실패)
 * 3) 열(column) 추가에 강한 X 매크로: `...` 로 안 쓰는 열 무시, 컴파일 타임 비트마스크·개수
 * 4) 접근 속성을 지키는 reg_write_checked (RO/RC 거부, W1C 는 읽지 않고 쓰기만)
 * 5) 컴파일 타임 CSV 문자열: 호스트 도구·문서와 펌웨어가 같은 목록을 공유
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* ======================= 연습 1: 레지스터 + 필드 2단 X-macro ======================= */
/*                  레지스터 필드   pos width */
#define CR1_FIELDS(Y) Y(CR1, UE,     0, 1) Y(CR1, M,     12, 1) Y(CR1, OVER8, 15, 1)
#define CR2_FIELDS(Y) Y(CR2, STOP,  12, 2)
#define SR_FIELDS(Y)  Y(SR,  PE,     0, 1) Y(SR,  FE,     1, 1) Y(SR,  RXNE,   5, 1) \
                      Y(SR,  TC,     6, 1) Y(SR,  TXE,    7, 1)
#define NO_FIELDS(Y)
/*                 이름  오프셋  필드 목록(매크로 이름을 그대로 넘긴다) */
#define P1_REGS(X) X(CR1, 0x00u, CR1_FIELDS) X(CR2, 0x04u, CR2_FIELDS) \
                   X(SR,  0x0Cu, SR_FIELDS)  X(ID,  0x1Cu, NO_FIELDS)

#define X_P1_ID(r, ofs, F) P1_##r,
enum p1_reg { P1_REGS(X_P1_ID) P1_COUNT };

/* 안쪽 목록은 바깥 X 가 F(Y_...) 로 "호출" 해서 펼친다. 바깥·안쪽 매크로 이름이 달라야 한다. */
#define Y_MSK(r, f, pos, w) r##_##f##_Pos = (pos), r##_##f##_Msk = (int)(((1u << (w)) - 1u) << (pos)),
#define X_P1_MSK(r, ofs, F) F(Y_MSK)
enum p1_msk { P1_REGS(X_P1_MSK) P1_MSK_END_ };

#define Y_SUM(r, f, pos, w) + (uint64_t)r##_##f##_Msk
#define Y_OR(r, f, pos, w)  | (uint64_t)r##_##f##_Msk
#define X_P1_OVL(r, ofs, F) _Static_assert((0u F(Y_SUM)) == (0u F(Y_OR)), #r ": fields overlap");
P1_REGS(X_P1_OVL)

typedef struct { int reg; const char *name; unsigned pos, width; } p1_field_t;
#define Y_DESC(r, f, pos, w) { P1_##r, #f, (pos), (w) },
#define X_P1_DESC(r, ofs, F) F(Y_DESC)
static const p1_field_t k_p1_fields[] = { P1_REGS(X_P1_DESC) };

#define X_P1_NAME(r, ofs, F) #r,
static const char *const k_p1_names[] = { P1_REGS(X_P1_NAME) };

/* "SR: PE=0 FE=0 RXNE=1 TC=1 TXE=1" — 필드 없는 레지스터는 "ID: 0x00A70011" */
static int p1_decode(int reg, uint32_t v, char *buf, size_t cap) {
    size_t n = 0;
    int k = snprintf(buf, cap, "%s:", k_p1_names[reg]);
    bool any = false;
    if (k < 0 || (size_t)k >= cap) return -1;
    n = (size_t)k;
    for (size_t i = 0; i < ARRAY_LEN(k_p1_fields); i++) {
        const p1_field_t *f = &k_p1_fields[i];
        if (f->reg != reg) continue;
        any = true;
        k = snprintf(buf + n, cap - n, " %s=%lu", f->name,
                     (unsigned long)((v >> f->pos) & ((1u << f->width) - 1u)));
        if (k < 0 || (size_t)k >= cap - n) return -1;
        n += (size_t)k;
    }
    if (!any) {
        k = snprintf(buf + n, cap - n, " 0x%08lX", (unsigned long)v);
        if (k < 0 || (size_t)k >= cap - n) return -1;
        n += (size_t)k;
    }
    return (int)n;
}

/* ======================= 연습 3: 열이 많은 목록 — `...` 로 필요한 열만 ======================= */
enum sev { SEV_INFO, SEV_WARN, SEV_ERROR, SEV_FATAL };
/*         이름        값   심각도     설명                재시도 가능 */
#define P3_ERRS(X)                                                      \
    X(OK,          0, SEV_INFO,  "success",              0)            \
    X(TIMEOUT,    -1, SEV_WARN,  "operation timed out",  1)            \
    X(BUSY,       -2, SEV_WARN,  "resource busy",        1)            \
    X(INVAL,      -3, SEV_ERROR, "invalid argument",     0)            \
    X(CRC,       -10, SEV_ERROR, "CRC mismatch",         1)            \
    X(HW_FAULT, -100, SEV_FATAL, "hardware fault",       0)

#define X_P3_VAL(n, v, ...) E_##n = (v),          /* 앞의 두 열만 쓰고 나머지는 ... 가 삼킨다 */
enum p3_err { P3_ERRS(X_P3_VAL) };
#define X_P3_IDX(n, ...) I_##n,
enum p3_idx { P3_ERRS(X_P3_IDX) P3_COUNT };
#define X_P3_RETRY(n, v, sev, desc, retry) | ((uint32_t)(retry) << I_##n)
#define P3_RETRY_MASK (0u P3_ERRS(X_P3_RETRY))
#define X_P3_FATAL(n, v, sev, ...) + ((sev) == SEV_FATAL)
enum { P3_N_FATAL = 0 P3_ERRS(X_P3_FATAL) };
_Static_assert(P3_COUNT <= 32, "retry mask is uint32_t");

static int p3_index(int code) {
#define X_P3_CASE(n, v, ...) case (v): return I_##n;
    switch (code) { P3_ERRS(X_P3_CASE) default: return -1; }
#undef X_P3_CASE
}
static bool p3_is_retryable(int code) {
    int i = p3_index(code);
    return i >= 0 && ((P3_RETRY_MASK >> i) & 1u) != 0u;
}
#define X_P3_NAME(n, ...) #n,
static const char *const k_p3_names[] = { P3_ERRS(X_P3_NAME) };
#define X_P3_DESC(n, v, sev, desc, ...) desc,
static const char *const k_p3_desc[] = { P3_ERRS(X_P3_DESC) };

/* ======================= 연습 5: 컴파일 타임 CSV ======================= */
#define X_P5_CSV(n, v, sev, desc, retry) #n "," #v "," desc "," #retry "\n"
static const char k_err_csv[] = "name,code,desc,retry\n" P3_ERRS(X_P5_CSV);

/* ======================= 연습 2: CLI 명령 표 ======================= */
#ifdef P2_MISSING
#define P2_EXTRA(X) X(reboot, 0, "reset the MCU")      /* 핸들러 cmd_reboot 를 안 만들었다 */
#else
#define P2_EXTRA(X)
#endif
/*           이름  최소 인자 수  도움말 */
#define CLI_CMDS(X)                                     \
    X(help, 0, "list commands")                         \
    X(err,  1, "err <code>: explain an error code")     \
    X(peek, 1, "peek <reg>: decode a register")         \
    P2_EXTRA(X)

#define X_P2_PROTO(n, nargs, help) static int cmd_##n(int argc, char **argv);
CLI_CMDS(X_P2_PROTO)
typedef struct { const char *name; int (*fn)(int, char **); int nargs; const char *help; } cli_cmd_t;
#define X_P2_CMD(n, nargs, help) { #n, cmd_##n, (nargs), (help) },
static const cli_cmd_t k_cmds[] = { CLI_CMDS(X_P2_CMD) };

static char g_out[256];
static uint32_t g_p2_regs[P1_COUNT] = { 0x00001001u, 0x00002000u, 0x000000E0u, 0x00A70011u };

static int cmd_help(int argc, char **argv) {
    size_t n = 0;
    (void)argc; (void)argv;
    for (size_t i = 0; i < ARRAY_LEN(k_cmds); i++) {
        int k = snprintf(g_out + n, sizeof g_out - n, "%s%s", i ? " " : "", k_cmds[i].name);
        if (k < 0 || (size_t)k >= sizeof g_out - n) return E_INVAL;
        n += (size_t)k;
    }
    return E_OK;
}
static int cmd_err(int argc, char **argv) {
    char *end;
    long c = strtol(argv[1], &end, 0);                     /* C-M09 */
    int i;
    (void)argc;
    if (*end != '\0' || (i = p3_index((int)c)) < 0) return E_INVAL;
    snprintf(g_out, sizeof g_out, "%s: %s%s", k_p3_names[i], k_p3_desc[i],
             p3_is_retryable((int)c) ? " (retry)" : "");
    return E_OK;
}
static int cmd_peek(int argc, char **argv) {
    (void)argc;
    for (int r = 0; r < P1_COUNT; r++)
        if (strcmp(argv[1], k_p1_names[r]) == 0)
            return p1_decode(r, g_p2_regs[r], g_out, sizeof g_out) < 0 ? E_INVAL : E_OK;
    return E_INVAL;
}
/* 공백으로 자른 줄을 명령 표에서 찾아 실행. 줄은 수정된다 (토큰 끝에 '\0'). */
static int cli_exec(char *line) {
    char *argv[4];
    int argc = 0;
    char *p = line;
    g_out[0] = '\0';
    while (*p && argc < (int)ARRAY_LEN(argv)) {
        while (*p == ' ') *p++ = '\0';
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
    }
    if (argc == 0) return E_INVAL;
    for (size_t i = 0; i < ARRAY_LEN(k_cmds); i++)
        if (strcmp(argv[0], k_cmds[i].name) == 0)
            return argc - 1 < k_cmds[i].nargs ? E_INVAL : k_cmds[i].fn(argc, argv);
    return E_INVAL;
}

/* ======================= 연습 4: 접근 속성을 지키는 쓰기 ======================= */
enum reg_acc { ACC_RO, ACC_RW, ACC_WO, ACC_RC, ACC_W1C };
#define P4_REGS(X) X(CR1, 0x00u, ACC_RW) X(SR, 0x0Cu, ACC_W1C) X(RDR, 0x10u, ACC_RC) \
                   X(TDR, 0x14u, ACC_WO) X(ID, 0x1Cu, ACC_RO)
#define X_P4_ID(n, o, a) P4_##n,
enum p4_reg { P4_REGS(X_P4_ID) P4_COUNT };
#define X_P4_OFS(n, o, a) (o),
static const uint32_t k_p4_ofs[] = { P4_REGS(X_P4_OFS) };
#define X_P4_ACC(n, o, a) (a),
static const uint8_t k_p4_acc[] = { P4_REGS(X_P4_ACC) };

typedef struct { uint32_t mem[8]; unsigned nwrites; uint32_t last_ofs, last_val; } p4_bus_t;   /* 읽기 함수는 아예 없다 */
static void p4_bus_write(p4_bus_t *b, uint32_t ofs, uint32_t v) {   /* 하드웨어 흉내 */
    b->nwrites++; b->last_ofs = ofs; b->last_val = v;
    if (ofs == 0x0Cu) b->mem[ofs / 4u] &= ~v;                      /* SR: 1 을 쓴 비트만 지워짐 */
    else b->mem[ofs / 4u] = v;
}
static int reg_write_checked(p4_bus_t *b, int id, uint32_t v) {
    if (id < 0 || id >= P4_COUNT) return E_INVAL;
    switch ((enum reg_acc)k_p4_acc[id]) {
    case ACC_RO: case ACC_RC: return E_INVAL;                       /* 쓰면 안 되는 레지스터 */
    case ACC_RW: case ACC_WO: case ACC_W1C:
        p4_bus_write(b, k_p4_ofs[id], v);                           /* W1C 도 RMW 없이 그대로 (C-M02) */
        return E_OK;
    }
    return E_INVAL;
}

/* ================================== 하네스 ================================== */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define STREQ(a, b) (strcmp((a), (b)) == 0)

int main(void) {
    char buf[128];

    /* 연습 1 */
    CHECK(SR_RXNE_Msk == 0x20 && CR2_STOP_Msk == 0x3000 && CR1_OVER8_Pos == 15);
    CHECK(ARRAY_LEN(k_p1_fields) == 9u && P1_COUNT == 4);
    CHECK(p1_decode(P1_SR, 0xE0u, buf, sizeof buf) > 0 && STREQ(buf, "SR: PE=0 FE=0 RXNE=1 TC=1 TXE=1"));
    CHECK(p1_decode(P1_CR2, 0x2000u, buf, sizeof buf) > 0 && STREQ(buf, "CR2: STOP=2"));
    CHECK(p1_decode(P1_ID, 0x00A70011u, buf, sizeof buf) > 0 && STREQ(buf, "ID: 0x00A70011"));
    CHECK(p1_decode(P1_SR, 0xE0u, buf, 8) == -1);

    /* 연습 2 */
    {
        char l1[] = "help", l2[] = "err -11", l3[] = "  peek   SR ", l4[] = "err", l5[] = "reset",
             l6[] = "err 0x7", l7[] = "err -10";
        CHECK(cli_exec(l1) == E_OK && STREQ(g_out, "help err peek"));
        CHECK(cli_exec(l2) == E_INVAL);                       /* -11 은 P3 목록에 없음 */
        CHECK(cli_exec(l7) == E_OK && STREQ(g_out, "CRC: CRC mismatch (retry)"));
        CHECK(cli_exec(l3) == E_OK && STREQ(g_out, "SR: PE=0 FE=0 RXNE=1 TC=1 TXE=1"));
        CHECK(cli_exec(l4) == E_INVAL && cli_exec(l5) == E_INVAL && cli_exec(l6) == E_INVAL);
    }

    /* 연습 3 */
    CHECK(P3_COUNT == 6 && P3_RETRY_MASK == 0x16u && P3_N_FATAL == 1);
    CHECK(p3_is_retryable(E_TIMEOUT) && p3_is_retryable(E_CRC) && !p3_is_retryable(E_HW_FAULT)
          && !p3_is_retryable(-55));
    CHECK(ARRAY_LEN(k_p3_names) == P3_COUNT && ARRAY_LEN(k_p3_desc) == P3_COUNT);

    /* 연습 4 */
    {
        p4_bus_t b;
        memset(&b, 0, sizeof b);
        b.mem[0x0C / 4] = 0xE0u;
        CHECK(reg_write_checked(&b, P4_SR, 0x20u) == E_OK && b.mem[0x0C / 4] == 0xC0u && b.nwrites == 1u);
        CHECK(reg_write_checked(&b, P4_ID, 1u) == E_INVAL && reg_write_checked(&b, P4_RDR, 1u) == E_INVAL
              && b.nwrites == 1u);
        CHECK(reg_write_checked(&b, P4_TDR, 0x41u) == E_OK && b.last_ofs == 0x14u && b.last_val == 0x41u);
        CHECK(reg_write_checked(&b, P4_COUNT, 0u) == E_INVAL && reg_write_checked(&b, -1, 0u) == E_INVAL);
    }

    /* 연습 5 */
    CHECK(STREQ(k_err_csv, "name,code,desc,retry\n"
                           "OK,0,success,0\n"
                           "TIMEOUT,-1,operation timed out,1\n"
                           "BUSY,-2,resource busy,1\n"
                           "INVAL,-3,invalid argument,0\n"
                           "CRC,-10,CRC mismatch,1\n"
                           "HW_FAULT,-100,hardware fault,0\n"));
    CHECK(sizeof k_err_csv == strlen(k_err_csv) + 1u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
