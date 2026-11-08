/* C-M06 함수 포인터와 콜백 테이블
 *
 * Part A — 함수 포인터 문법: typedef, signal() 모양의 "날것" 선언, 함수 포인터를 반환하는 함수,
 *          콜백 + void *ctx
 * Part B — 디버그 셸: 문자열 명령 → 핸들러 디스패치 테이블 (const 테이블, argc/argv, 에러 처리)
 * Part C — Linux file_operations 스타일 driver_ops {init, read, write, ioctl} 로
 *          두 mock 디바이스 (링버퍼 루프백 "uart0", 32바이트 가짜 "eeprom0") 를 같은 코드로 구동
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
 */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================ Part A ============================ */
typedef void (*sig_handler_t)(int sig);             /* "int 를 받고 void 를 반환하는 함수" 를 가리키는 포인터 */
typedef int32_t (*binop_fn)(int32_t a, int32_t b);
typedef bool (*visit_fn)(uint8_t byte, void *ctx);  /* false 를 반환하면 순회를 멈춘다 */

#define NSIG_SIM 8
static void sig_err_fn(int sig) { (void)sig; }      /* 에러 표시용 센티넬 함수 — 주소만 쓴다 */
#define MY_SIG_DFL ((sig_handler_t)0)               /* 등록 안 됨 = 널 포인터 */
#define MY_SIG_ERR (&sig_err_fn)

static sig_handler_t g_vec[NSIG_SIM];               /* 벡터 테이블 흉내: 신호 번호 → 핸들러 */
static unsigned g_unhandled;                        /* 핸들러 없이 발생한 신호 수 */

/* A1. libc signal() 과 똑같은 모양의 날것 선언 (읽는 법은 md 5. 풀이).
 *     sig 가 범위 밖이면 MY_SIG_ERR. 아니면 handler 를 등록하고 "이전 핸들러" 를 반환한다. */
static void (*my_signal(int sig, void (*handler)(int)))(int) {
    (void)sig; (void)handler;
    return MY_SIG_ERR; /* TODO A1 */
}
/* A2. 신호 발생: 핸들러가 있으면 호출하고 1, 없으면(널) g_unhandled++ 후 0, 범위 밖이면 -1 */
static int my_raise(int sig) {
    (void)sig; (void)g_vec;
    return -1; /* TODO A2 */
}

static int32_t op_add(int32_t a, int32_t b) { return a + b; }
static int32_t op_sub(int32_t a, int32_t b) { return a - b; }
static int32_t op_mul(int32_t a, int32_t b) { return a * b; }
static int32_t op_div(int32_t a, int32_t b) {       /* 0 나누기 · INT32_MIN / -1 오버플로 방어 */
    return (b == 0 || (a == INT32_MIN && b == -1)) ? 0 : a / b;
}
static const char     k_binop_sym[] = "+-*/";       /* k_binop_sym[i] ↔ k_binops[i] */
static const binop_fn k_binops[]    = { op_add, op_sub, op_mul, op_div };

/* A3. 함수 포인터를 "반환하는" 함수 (typedef 없이). '+' '-' '*' '/' → 해당 함수, 그 외 → NULL */
static int32_t (*pick_op(char op))(int32_t, int32_t) {
    (void)op; (void)k_binop_sym; (void)k_binops;
    return NULL; /* TODO A3 */
}

/* A4. 콜백 + 컨텍스트: 각 바이트로 visit(byte, ctx) 를 부른다. visit 이 false 면 그 자리에서 멈춘다.
 *     반환: visit 을 부른 횟수. visit 이 NULL 이거나 buf 가 NULL 이면 0. */
static size_t for_each_u8(const uint8_t *buf, size_t n, visit_fn visit, void *ctx) {
    (void)buf; (void)n; (void)visit; (void)ctx;
    return 0; /* TODO A4 */
}

/* ============================ Part B ============================ */
#define SHELL_MAX_ARGS 6                            /* argv[0](명령 이름) 포함 최대 토큰 수 */
enum { SH_OK = 0, SH_ERR_INVAL = -1, SH_ERR_UNKNOWN = -2, SH_ERR_ARGC = -3,
       SH_ERR_TOOMANY = -4, SH_ERR_NOFN = -5, SH_ERR_BADVAL = -6 };

typedef int (*cmd_fn)(int argc, char *argv[], void *ctx);
typedef struct {
    const char *name;
    cmd_fn      fn;                                 /* NULL = 아직 구현 안 된 명령 (자리만 있음) */
    uint8_t     min_argc, max_argc;                 /* argv[0] 포함 */
    const char *help;
} shell_cmd_t;

typedef struct {                                    /* 핸들러가 void *ctx 로 받는 상태 */
    uint32_t regs[4];                               /* 가짜 레지스터 블록 */
    char     out[96];                               /* "UART 출력" 대신 여기에 쓴다 */
    unsigned calls;                                 /* 핸들러가 불린 횟수 */
} shell_ctx_t;

static void out_printf(shell_ctx_t *c, const char *fmt, ...) {
    size_t n = strlen(c->out);
    if (n >= sizeof c->out - 1) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->out + n, sizeof c->out - n, fmt, ap);
    va_end(ap);
}
static bool parse_u32(const char *s, uint32_t *out) {
    if (s == NULL || *s == '\0' || *s == '-' || *s == '+') return false;  /* strtoul 은 "-1" 도 받아 준다 */
    char *end;
    errno = 0;
    unsigned long v = strtoul(s, &end, 0);          /* 0x10, 010, 16 모두 (C-M09 참고) */
    if (errno != 0 || *end != '\0' || v > UINT32_MAX) return false;
    *out = (uint32_t)v;
    return true;
}

static int cmd_help(int argc, char *argv[], void *ctx);
static int cmd_peek(int argc, char *argv[], void *ctx) {
    shell_ctx_t *c = ctx;
    uint32_t i;
    (void)argc;
    c->calls++;
    if (!parse_u32(argv[1], &i) || i >= 4) return SH_ERR_BADVAL;
    out_printf(c, "R%u=0x%08X", (unsigned)i, (unsigned)c->regs[i]);
    return SH_OK;
}
static int cmd_poke(int argc, char *argv[], void *ctx) {
    shell_ctx_t *c = ctx;
    uint32_t i, v;
    (void)argc;
    c->calls++;
    if (!parse_u32(argv[1], &i) || i >= 4 || !parse_u32(argv[2], &v)) return SH_ERR_BADVAL;
    c->regs[i] = v;
    return SH_OK;
}
static int cmd_echo(int argc, char *argv[], void *ctx) {
    shell_ctx_t *c = ctx;
    c->calls++;
    for (int i = 1; i < argc; i++) out_printf(c, "%s%s", i > 1 ? " " : "", argv[i]);
    return SH_OK;
}

/* const 배열 + 안의 포인터도 상수 주소 → 링커가 .rodata(플래시) 에 둔다. RAM 0 바이트. */
static const shell_cmd_t k_cmds[] = {
    { "help",   cmd_help, 1, 1,              "list commands" },
    { "peek",   cmd_peek, 2, 2,              "peek <reg>" },
    { "poke",   cmd_poke, 3, 3,              "poke <reg> <val>" },
    { "echo",   cmd_echo, 1, SHELL_MAX_ARGS, "echo [words...]" },
    { "reboot", NULL,     1, 1,              "(not implemented yet)" },
};
#define N_CMDS (sizeof k_cmds / sizeof k_cmds[0])

static int cmd_help(int argc, char *argv[], void *ctx) {
    shell_ctx_t *c = ctx;
    (void)argc; (void)argv;
    c->calls++;
    for (size_t i = 0; i < N_CMDS; i++) out_printf(c, "%s%s", i ? " " : "", k_cmds[i].name);
    return SH_OK;
}

static bool is_sep(char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; }

/* B1. line 을 제자리에서(in place) 토큰으로 자른다. 구분자는 is_sep(), 연속 구분자는 하나로 본다.
 *     argv[0..argc-1] = 토큰, argv[argc] = NULL (C 의 main 과 같은 약속). 반환: argc.
 *     토큰이 max 개를 넘으면 SH_ERR_TOOMANY. argv 배열의 크기는 max + 1 이다. */
static int shell_tokenize(char *line, char *argv[], int max) {
    (void)line; (void)max; (void)is_sep;
    argv[0] = NULL;
    return 0; /* TODO B1 */
}
/* B2. 이름이 정확히 같은(대소문자 구분) 항목을 찾는다. 없으면 NULL. tbl == NULL 이어도 안전하게. */
static const shell_cmd_t *shell_find(const shell_cmd_t *tbl, size_t n, const char *name) {
    (void)tbl; (void)n; (void)name;
    return NULL; /* TODO B2 */
}
/* B3. 한 줄 실행. 순서: 인자 검사(SH_ERR_INVAL) → 토큰화(SH_ERR_TOOMANY) → 빈 줄이면 SH_OK
 *     (아무것도 부르지 않음) → 찾기(SH_ERR_UNKNOWN) → argc 범위(SH_ERR_ARGC) → fn NULL(SH_ERR_NOFN)
 *     → 핸들러 반환값을 그대로 반환. */
static int shell_exec(const shell_cmd_t *tbl, size_t n, char *line, void *ctx) {
    (void)tbl; (void)n; (void)line; (void)ctx;
    return SH_ERR_UNKNOWN; /* TODO B3 */
}

/* ============================ Part C ============================ */
enum { DEV_OK = 0, DEV_EPERM = -1, DEV_ENXIO = -6, DEV_EAGAIN = -11, DEV_ENODEV = -19,
       DEV_EINVAL = -22, DEV_ENOTTY = -25, DEV_ENOSPC = -28, DEV_ENOTSUP = -95 };  /* 값은 Linux errno */

struct device;
struct driver_ops {                                 /* Linux struct file_operations 의 축소판 */
    int (*init)(struct device *dev);
    int (*read)(struct device *dev, uint8_t *buf, size_t len);         /* 읽은 바이트 수 또는 음수 */
    int (*write)(struct device *dev, const uint8_t *buf, size_t len);  /* 쓴 바이트 수 또는 음수 */
    int (*ioctl)(struct device *dev, unsigned cmd, uintptr_t arg);     /* 선택: NULL 이면 미지원 */
};
struct device {
    const char *name;
    const struct driver_ops *ops;                   /* RAM 에는 포인터 하나, 표 자체는 플래시 */
    void *priv;                                     /* 방법 2: 드라이버 전용 데이터 (void *ctx) */
    bool ready;                                     /* dev_init 성공 후 true */
};
#define CONTAINER_OF(ptr, type, member) \
    ((type *)(void *)((char *)(ptr) - offsetof(type, member)))   /* 타입 검사 버전은 C-A06 */

/* ---- 디바이스 1: 루프백 UART (방법 1: struct device 를 품고 container_of 로 되찾기) ---- */
#define UART_RB_SIZE 8u                             /* 2 의 거듭제곱 → 인덱스는 & (SIZE - 1) */
typedef struct {
    uint8_t  rb[UART_RB_SIZE];                      /* 일부러 dev 를 첫 멤버가 아니게 */
    uint32_t head, tail;                            /* 자유 증가 인덱스: 들어 있는 양 = head - tail */
    uint32_t tx_total;
    struct device dev;                              /* 공통 부분을 품는다(embed) */
} uart_dev_t;
static uart_dev_t *to_uart(struct device *d) { return CONTAINER_OF(d, uart_dev_t, dev); }

static int uart_init(struct device *d) {
    uart_dev_t *u = to_uart(d);
    u->head = u->tail = u->tx_total = 0;
    return DEV_OK;
}
/* C2. write: 링에 넣을 수 있는 만큼 넣고 그 수를 반환. 한 바이트도 못 넣으면 DEV_EAGAIN.
 *     read : 들어 있는 만큼(최대 len) 꺼내고 그 수를 반환. 비었으면 0. */
static int uart_write(struct device *d, const uint8_t *buf, size_t len) {
    (void)d; (void)buf; (void)len;
    return DEV_ENOTSUP; /* TODO C2 */
}
static int uart_read(struct device *d, uint8_t *buf, size_t len) {
    (void)d; (void)buf; (void)len;
    return DEV_ENOTSUP; /* TODO C2 */
}
static const struct driver_ops uart_ops = {         /* .ioctl 생략 → 널 포인터 (6.7.9p19, p10) */
    .init = uart_init, .read = uart_read, .write = uart_write,
};

/* ---- 디바이스 2: 32 바이트 EEPROM (방법 2: dev->priv 에 void * 로 매달기) ---- */
#define EE_SIZE 32u
enum { EE_IOC_SEEK = 1, EE_IOC_WP = 2 };
typedef struct { uint8_t mem[EE_SIZE]; size_t pos; bool wp; } ee_priv_t;

static int ee_init(struct device *d) {
    ee_priv_t *e = d->priv;
    if (e == NULL) return DEV_ENODEV;               /* priv 를 안 달아 준 보드 설정 실수 */
    memset(e->mem, 0xFF, sizeof e->mem);            /* 지워진 EEPROM/플래시 = 0xFF */
    e->pos = 0;
    e->wp = false;
    return DEV_OK;
}
/* C3. read : pos 부터 최대 len 바이트, pos 전진. 끝(pos == EE_SIZE)이면 0 (EOF).
 *     write: wp 면 DEV_EPERM, 끝이면 DEV_ENOSPC, 아니면 남은 공간만큼 잘라 쓰고 그 수를 반환.
 *     ioctl: EE_IOC_SEEK(arg ≤ EE_SIZE, 아니면 DEV_EINVAL), EE_IOC_WP(arg != 0), 그 외 DEV_ENOTTY. */
static int ee_read(struct device *d, uint8_t *buf, size_t len) {
    (void)d; (void)buf; (void)len;
    return DEV_ENOTSUP; /* TODO C3 */
}
static int ee_write(struct device *d, const uint8_t *buf, size_t len) {
    (void)d; (void)buf; (void)len;
    return DEV_ENOTSUP; /* TODO C3 */
}
static int ee_ioctl(struct device *d, unsigned cmd, uintptr_t arg) {
    (void)d; (void)cmd; (void)arg;
    return DEV_ENOTSUP; /* TODO C3 */
}
static const struct driver_ops ee_ops = {
    .init = ee_init, .read = ee_read, .write = ee_write, .ioctl = ee_ioctl,
};

/* ---- 디바이스 3: 쓰기 전용 LED (read/ioctl 없음) ---- */
static uint8_t g_led_level;
static int led_init(struct device *d) { (void)d; g_led_level = 0; return DEV_OK; }
static int led_write(struct device *d, const uint8_t *buf, size_t len) {
    (void)d;
    g_led_level = buf[len - 1];                     /* 마지막 바이트 = 밝기 (len ≥ 1 은 상위가 보장) */
    return (int)len;
}
static const struct driver_ops led_ops = { .init = led_init, .write = led_write };

/* C1. 공통 계층 (Linux 의 vfs_read/vfs_write 자리). 검사 순서 고정:
 *     d == NULL 또는 (buf == NULL && len > 0) → DEV_EINVAL
 *     d->ops == NULL                         → DEV_ENODEV
 *     해당 op 가 NULL                         → DEV_ENOTSUP
 *     !d->ready (init 전/실패)                → DEV_ENXIO     (dev_init 은 이 검사 없음)
 *     len == 0                               → 0 (드라이버를 부르지 않음)
 *     그 외 드라이버 op 의 반환값. dev_init 은 성공(DEV_OK)일 때만 ready = true. */
static int dev_init(struct device *d) {
    (void)d;
    return DEV_ENOTSUP; /* TODO C1 */
}
static int dev_read(struct device *d, uint8_t *buf, size_t len) {
    (void)d; (void)buf; (void)len;
    return DEV_ENOTSUP; /* TODO C1 */
}
static int dev_write(struct device *d, const uint8_t *buf, size_t len) {
    (void)d; (void)buf; (void)len;
    return DEV_ENOTSUP; /* TODO C1 */
}
static int dev_ioctl(struct device *d, unsigned cmd, uintptr_t arg) {
    (void)d; (void)cmd; (void)arg;
    return DEV_ENOTSUP; /* TODO C1 */
}

/* (주어진 코드) 상위 계층: 어떤 디바이스인지 모른 채 ops 만으로 동작한다 */
static int dev_puts(struct device *d, const char *s) {
    return dev_write(d, (const uint8_t *)s, strlen(s));
}
static uart_dev_t uart0 = { .dev = { .name = "uart0", .ops = &uart_ops } };
static ee_priv_t ee0_priv;
static struct device ee0  = { .name = "eeprom0", .ops = &ee_ops, .priv = &ee0_priv };
static struct device led0 = { .name = "led0",    .ops = &led_ops };
static struct device *const k_devs[] = { &uart0.dev, &ee0, &led0 };   /* 보드 디바이스 목록 */
static struct device *dev_find(const char *name) {
    for (size_t i = 0; i < sizeof k_devs / sizeof k_devs[0]; i++)
        if (strcmp(k_devs[i]->name, name) == 0) return k_devs[i];
    return NULL;
}

/* ============================ 테스트 ============================ */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static int g_last_sig = -1, g_hits_a, g_hits_b;
static void on_sig_a(int sig) { g_last_sig = sig; g_hits_a++; }
static void on_sig_b(int sig) { g_last_sig = sig; g_hits_b++; }

static bool sum_cb(uint8_t b, void *ctx) { *(uint32_t *)ctx += b; return true; }
typedef struct { uint8_t target; size_t idx; bool found; } find_ctx_t;
static bool find_cb(uint8_t b, void *ctx) {
    find_ctx_t *f = ctx;
    if (b == f->target) { f->found = true; return false; }   /* 찾았으면 멈춘다 */
    f->idx++;
    return true;
}

static void test_part_a(void) {
    /* 두 선언은 "같은 타입" 이다 — 날것 선언과 typedef 선언을 _Generic 으로 대조 */
    CHECK(_Generic(my_signal, sig_handler_t (*)(int, sig_handler_t): 1, default: 0));
    CHECK(_Generic(pick_op, binop_fn (*)(char): 1, default: 0));
    CHECK(sizeof(sig_handler_t) == sizeof(void (*)(void)));

    CHECK(my_signal(3, on_sig_a) == MY_SIG_DFL);        /* 처음 등록 → 이전은 "없음" */
    CHECK(my_signal(3, on_sig_b) == on_sig_a);          /* 다시 등록 → 이전 핸들러가 돌아온다 */
    CHECK(my_signal(8, on_sig_a) == MY_SIG_ERR);        /* 범위 밖 */
    CHECK(my_signal(-1, on_sig_a) == MY_SIG_ERR);
    CHECK(my_raise(3) == 1 && g_hits_b == 1 && g_hits_a == 0 && g_last_sig == 3);
    CHECK(my_raise(5) == 0 && g_unhandled == 1);        /* 등록 안 된 신호: 널을 부르면 안 된다 */
    CHECK(my_raise(NSIG_SIM) == -1);
    sig_handler_t old = my_signal(3, MY_SIG_DFL);       /* 등록 해제 */
    CHECK(old == on_sig_b && my_raise(3) == 0 && g_unhandled == 2);

    CHECK(pick_op('+') == op_add && pick_op('/') == op_div);
    binop_fn add = pick_op('+'), mul = pick_op('*'), dv = pick_op('/');
    CHECK(add != NULL && add(40, 2) == 42);             /* 받은 포인터는 널 검사 후 호출 */
    CHECK(mul != NULL && (*mul)(6, 7) == 42);           /* * 를 붙여도 같다 */
    CHECK(dv != NULL && dv(7, 0) == 0 && dv(INT32_MIN, -1) == 0);
    CHECK(pick_op('%') == NULL);
    CHECK(pick_op('\0') == NULL);                       /* "+-*\/" 의 끝 '\0' 과 매칭되면 안 된다 */
    struct { char op; int32_t a, b, want; } calc[] = {
        { '+', 2, 3, 5 }, { '-', 2, 3, -1 }, { '*', -4, 5, -20 }, { '/', -9, 2, -4 },
    };
    int ok = 1;
    for (size_t i = 0; i < sizeof calc / sizeof calc[0]; i++) {
        binop_fn f = pick_op(calc[i].op);
        ok &= (f != NULL && f(calc[i].a, calc[i].b) == calc[i].want);
    }
    CHECK(ok);

    const uint8_t data[] = { 1, 2, 3, 0xAA, 5 };
    uint32_t sum = 0;
    CHECK(for_each_u8(data, sizeof data, sum_cb, &sum) == 5 && sum == 1 + 2 + 3 + 0xAA + 5);
    find_ctx_t f = { .target = 0xAA };
    CHECK(for_each_u8(data, sizeof data, find_cb, &f) == 4 && f.found && f.idx == 3);
    find_ctx_t g = { .target = 0x77 };
    CHECK(for_each_u8(data, sizeof data, find_cb, &g) == 5 && !g.found);
    CHECK(for_each_u8(data, sizeof data, NULL, NULL) == 0);    /* 콜백 없음: 널 호출 금지 */
    CHECK(for_each_u8(data, 0, sum_cb, &sum) == 0);
}

static void test_part_b(void) {
    char line[64];
    char *argv[SHELL_MAX_ARGS + 1];
    strcpy(line, "  peek \t 1  \r\n");
    CHECK(shell_tokenize(line, argv, SHELL_MAX_ARGS) == 2);
    CHECK(argv[0] == line + 2 && strcmp(argv[0], "peek") == 0);   /* 복사 없이 원래 버퍼 안을 가리킨다 */
    CHECK(argv[1] != NULL && strcmp(argv[1], "1") == 0 && argv[2] == NULL);
    strcpy(line, "a b c d e f g");
    CHECK(shell_tokenize(line, argv, SHELL_MAX_ARGS) == SH_ERR_TOOMANY);
    strcpy(line, "");
    CHECK(shell_tokenize(line, argv, SHELL_MAX_ARGS) == 0 && argv[0] == NULL);

    CHECK(shell_find(k_cmds, N_CMDS, "poke") == &k_cmds[2]);
    CHECK(shell_find(k_cmds, N_CMDS, "pok") == NULL);             /* 접두사 일치는 안 된다 */
    CHECK(shell_find(NULL, 3, "poke") == NULL);

    static const struct { const char *line; int rc; const char *out; } cases[] = {
        { "",                 SH_OK,          "" },
        { "   \t \r\n",       SH_OK,          "" },
        { "help",             SH_OK,          "help peek poke echo reboot" },
        { "poke 1 0x10",      SH_OK,          "" },
        { "peek 1",           SH_OK,          "R1=0x00000010" },
        { "  peek   1  \r\n", SH_OK,          "R1=0x00000010" },
        { "poke 3 4294967295", SH_OK,         "" },
        { "peek 3",           SH_OK,          "R3=0xFFFFFFFF" },
        { "peek",             SH_ERR_ARGC,    "" },
        { "peek 1 2",         SH_ERR_ARGC,    "" },
        { "help me",          SH_ERR_ARGC,    "" },
        { "pee 1",            SH_ERR_UNKNOWN, "" },
        { "peek1",            SH_ERR_UNKNOWN, "" },
        { "PEEK 1",           SH_ERR_UNKNOWN, "" },
        { "poke 9 1",         SH_ERR_BADVAL,  "" },
        { "poke 1 zz",        SH_ERR_BADVAL,  "" },
        { "poke 1 -1",        SH_ERR_BADVAL,  "" },
        { "poke 1 4294967296", SH_ERR_BADVAL, "" },
        { "echo",             SH_OK,          "" },
        { "echo a  b\tc",     SH_OK,          "a b c" },
        { "echo 1 2 3 4 5",   SH_OK,          "1 2 3 4 5" },
        { "echo 1 2 3 4 5 6", SH_ERR_TOOMANY, "" },
        { "reboot",           SH_ERR_NOFN,    "" },
    };
    shell_ctx_t ctx = { .regs = { 0 } };
    int bad = 0;
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char buf[64];
        strcpy(buf, cases[i].line);
        ctx.out[0] = '\0';
        int rc = shell_exec(k_cmds, N_CMDS, buf, &ctx);
        if (rc != cases[i].rc || strcmp(ctx.out, cases[i].out) != 0) {
            bad++;
            printf("  case \"%s\": rc=%d (want %d) out=\"%s\" (want \"%s\")\n",
                   cases[i].line, rc, cases[i].rc, ctx.out, cases[i].out);
        }
    }
    CHECK(bad == 0);
    CHECK(ctx.calls == 13);      /* 성공 9 + BADVAL 4. 빈 줄·ARGC·UNKNOWN·TOOMANY·NOFN 에선 안 불렸다 */
    CHECK(ctx.regs[1] == 0x10 && ctx.regs[3] == 0xFFFFFFFFu);
    char l2[] = "help";
    CHECK(shell_exec(NULL, N_CMDS, l2, &ctx) == SH_ERR_INVAL);
    CHECK(shell_exec(k_cmds, N_CMDS, NULL, &ctx) == SH_ERR_INVAL);
}

static void test_part_c(void) {
    uint8_t buf[40];
    CHECK(to_uart(&uart0.dev) == &uart0);                       /* container_of 가 원래 구조체를 되찾는다 */
    CHECK(offsetof(uart_dev_t, dev) != 0);                      /* 진짜로 빼기가 일어나는 배치 */
    CHECK(dev_find("eeprom0") == &ee0 && dev_find("spi9") == NULL);

    /* 초기화 전 */
    CHECK(dev_read(&uart0.dev, buf, 4) == DEV_ENXIO);
    CHECK(dev_write(&ee0, buf, 4) == DEV_ENXIO);
    /* 같은 코드로 모든 디바이스를 초기화 */
    int ok = 1;
    for (size_t i = 0; i < sizeof k_devs / sizeof k_devs[0]; i++) ok &= (dev_init(k_devs[i]) == DEV_OK);
    CHECK(ok && uart0.dev.ready && ee0.ready && led0.ready);
    CHECK(ee0_priv.mem[0] == 0xFF && ee0_priv.mem[EE_SIZE - 1] == 0xFF);

    /* 같은 dev_puts 가 UART 와 EEPROM 모두에 쓴다 */
    CHECK(dev_puts(&uart0.dev, "hello") == 5);
    CHECK(dev_puts(&ee0, "hello") == 5);
    memset(buf, 0, sizeof buf);
    CHECK(dev_read(&uart0.dev, buf, sizeof buf) == 5 && memcmp(buf, "hello", 5) == 0);
    CHECK(dev_read(&uart0.dev, buf, sizeof buf) == 0);           /* 비었다 */
    CHECK(dev_ioctl(&ee0, EE_IOC_SEEK, 0) == DEV_OK);
    CHECK(dev_read(&ee0, buf, 7) == 7 && memcmp(buf, "hello\xFF\xFF", 7) == 0);

    /* UART 링버퍼 경계: 8 칸 */
    CHECK(dev_write(&uart0.dev, (const uint8_t *)"0123456789", 10) == 8);   /* 넣은 만큼만 */
    CHECK(dev_write(&uart0.dev, (const uint8_t *)"x", 1) == DEV_EAGAIN);    /* 가득 참 */
    CHECK(dev_read(&uart0.dev, buf, 3) == 3 && memcmp(buf, "012", 3) == 0);
    CHECK(dev_write(&uart0.dev, (const uint8_t *)"ab", 2) == 2);            /* 감아 돌기(wrap) */
    CHECK(dev_read(&uart0.dev, buf, sizeof buf) == 7 && memcmp(buf, "34567ab", 7) == 0);
    CHECK(uart0.tx_total == 15);
    CHECK(dev_ioctl(&uart0.dev, EE_IOC_SEEK, 0) == DEV_ENOTSUP);           /* ioctl 이 NULL */

    /* EEPROM 경계 */
    CHECK(dev_ioctl(&ee0, EE_IOC_SEEK, 30) == DEV_OK);
    CHECK(dev_write(&ee0, (const uint8_t *)"WXYZ", 4) == 2);                /* 끝에서 잘린다 */
    CHECK(dev_write(&ee0, (const uint8_t *)"Q", 1) == DEV_ENOSPC);
    CHECK(dev_read(&ee0, buf, 4) == 0);                                     /* EOF */
    CHECK(dev_ioctl(&ee0, EE_IOC_SEEK, EE_SIZE + 1) == DEV_EINVAL);
    CHECK(dev_ioctl(&ee0, EE_IOC_SEEK, 28) == DEV_OK);
    CHECK(dev_read(&ee0, buf, 10) == 4 && memcmp(buf, "\xFF\xFFWX", 4) == 0);
    CHECK(dev_ioctl(&ee0, EE_IOC_WP, 1) == DEV_OK && dev_ioctl(&ee0, EE_IOC_SEEK, 0) == DEV_OK);
    CHECK(dev_write(&ee0, (const uint8_t *)"zz", 2) == DEV_EPERM);
    CHECK(dev_ioctl(&ee0, 99, 0) == DEV_ENOTTY);

    /* 쓰기 전용 LED, 널 안전성 */
    CHECK(dev_write(&led0, (const uint8_t *)"\x10\x80", 2) == 2 && g_led_level == 0x80);
    CHECK(dev_read(&led0, buf, 1) == DEV_ENOTSUP);
    CHECK(dev_write(&led0, NULL, 0) == 0 && g_led_level == 0x80);          /* len 0: 드라이버 안 부름 */
    CHECK(dev_write(&led0, NULL, 3) == DEV_EINVAL);
    CHECK(dev_read(NULL, buf, 1) == DEV_EINVAL && dev_init(NULL) == DEV_EINVAL);
    struct device orphan = { .name = "orphan" };                            /* 드라이버 없음 */
    CHECK(dev_init(&orphan) == DEV_ENODEV && dev_write(&orphan, buf, 1) == DEV_ENODEV);
    struct device ee_bad = { .name = "ee_bad", .ops = &ee_ops };            /* priv 를 안 달았다 */
    CHECK(dev_init(&ee_bad) == DEV_ENODEV && !ee_bad.ready);
    CHECK(dev_read(&ee_bad, buf, 1) == DEV_ENXIO);                          /* init 실패 → 계속 막힌다 */
}

int main(void) {
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
