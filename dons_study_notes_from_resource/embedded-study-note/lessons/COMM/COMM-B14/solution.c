/* COMM-B14 UART 커맨드 라인 / AT 커맨드 파서
 *
 * Part A  라인 에디터 le_feed(): 바이트 하나씩 받는다. BS(0x08)/DEL(0x7F) 지우기, CR / LF / CRLF = 줄 끝 1번,
 *         최대 길이 초과 → 줄 끝까지 버리고 에러 1번, 비인쇄 문자 버림(TAB 은 공백으로), 에코.
 * Part B  tokenize() (제자리 분할, 인자 초과는 에러), parse_i32() (10진 / 0x 16진, 부호, 넘침, 꼬리 쓰레기 —
 *         atoi/strtol 없이), 명령 테이블 디스패처 cli_exec() (이름, 최소/최대 인자 수, 핸들러).
 * Part C  같은 코어 위의 AT 명령 at_exec() (AT, ATE0/1, AT+BAUD=…, AT+BAUD?, AT+BAUD=?, AT+LED),
 *         UART RX ISR → 링 버퍼 → 메인 루프 cli_poll(), 악성 입력과 고정 시드 퍼즈 테스트.
 * malloc 없음: 모든 버퍼는 정적이고 용량(CLI_LINE_MAX, CLI_MAX_ARGS, OUT_CAP, RX_RING_SIZE)이 인터페이스다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CLI_LINE_MAX 40   /* 줄 본문 최대 글자 수 (NUL 제외) */
#define CLI_MAX_ARGS 8    /* 명령 이름 포함 토큰 최대 개수 */
#define OUT_CAP      256  /* 응답 버퍼 (실제 펌웨어라면 TX 링 버퍼) */
#define RX_RING_SIZE 64u  /* 2의 거듭제곱 (DS-B04) */

/* ---------------- 출력 싱크: 가득 차면 버리고 센다 (블로킹 금지) ---------------- */
typedef struct { char buf[OUT_CAP + 1]; size_t len; uint32_t dropped; } out_t;
static void out_putc(out_t *o, char c) {
    if (o->len < OUT_CAP) { o->buf[o->len++] = c; o->buf[o->len] = '\0'; }
    else o->dropped++;
}
static void out_puts(out_t *o, const char *s) { while (*s) out_putc(o, *s++); }
static void out_u32(out_t *o, uint32_t v) {
    char t[10]; int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) out_putc(o, t[--n]);
}
static void out_clear(out_t *o) { o->len = 0; o->buf[0] = '\0'; }

/* ---------------- Part A: 라인 에디터 ---------------- */
typedef enum { LE_NONE = 0, LE_LINE, LE_OVERFLOW } le_event_t;
typedef struct {
    char     buf[CLI_LINE_MAX + 1]; /* 항상 NUL 종료, strlen(buf) == len */
    uint8_t  len;
    bool     discarding;            /* 넘친 줄: 줄 끝까지 버리는 중 */
    bool     last_cr;               /* 직전 바이트가 CR → 바로 뒤 LF 는 삼킨다 (CRLF = 1줄) */
    bool     ready;                 /* LE_LINE 을 돌려줬다 → 다음 바이트에서 버퍼를 비운다 */
    bool     echo;
    uint32_t n_lines, n_overflow, n_ctrl_dropped;
} le_t;

static void le_init(le_t *le, bool echo) { memset(le, 0, sizeof *le); le->echo = echo; }

/* 바이트 하나를 처리한다. LE_LINE 이면 le->buf 에 줄이 있다 (다음 le_feed 호출 전까지 유효).
 * LE_OVERFLOW 는 넘친 줄의 줄 끝에서 딱 한 번. 에코는 tx 로 (echo 가 켜져 있을 때만). */
static le_event_t le_feed(le_t *le, out_t *tx, uint8_t c) {
    if (le->ready) { le->len = 0; le->buf[0] = '\0'; le->ready = false; }
    bool after_cr = le->last_cr;
    le->last_cr = (c == '\r');
    if (c == '\n' && after_cr) return LE_NONE;            /* CRLF 의 LF: 이미 줄을 끝냈다 */
    if (c == '\r' || c == '\n') {
        if (le->echo) out_puts(tx, "\r\n");
        le->ready = true;
        if (le->discarding) { le->discarding = false; le->n_overflow++; return LE_OVERFLOW; }
        le->n_lines++;
        return LE_LINE;
    }
    if (le->discarding) return LE_NONE;                    /* 줄 끝까지 전부 버린다 (BS 포함) */
    if (c == 0x08 || c == 0x7F) {                          /* 터미널마다 BS 또는 DEL 을 보낸다 */
        if (le->len) {
            le->buf[--le->len] = '\0';
            if (le->echo) out_puts(tx, "\b \b");           /* 커서 왼쪽, 공백으로 덮고, 다시 왼쪽 */
        }
        return LE_NONE;
    }
    if (c == '\t') c = ' ';
    if (c < 0x20 || c > 0x7E) { le->n_ctrl_dropped++; return LE_NONE; }   /* NUL, ESC, 0x80~ */
    if (le->len == CLI_LINE_MAX) {                         /* 넘침: 지금까지 받은 것도 버린다 */
        le->discarding = true; le->len = 0; le->buf[0] = '\0';
        if (le->echo) out_putc(tx, '\a');
        return LE_NONE;
    }
    le->buf[le->len++] = (char)c;
    le->buf[le->len] = '\0';
    if (le->echo) out_putc(tx, (char)c);
    return LE_NONE;
}

/* ---------------- Part B: 토크나이저 · 숫자 · 디스패처 ---------------- */
typedef enum { NUM_OK = 0, NUM_E_SYNTAX, NUM_E_RANGE } num_status_t;

static int digit_val(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return 99;
}

/* 문법: [+-]? ( 0[xX] hex+ | dec+ ). 문자열 전체가 숫자여야 한다. 8진수 없음("007" = 7).
 * 범위 밖(int32 넘침 포함)은 NUM_E_RANGE, 모양이 틀리면 NUM_E_SYNTAX (모양 검사가 우선).
 * 실패하면 *out 은 건드리지 않는다. */
static num_status_t parse_i32(const char *s, int32_t lo, int32_t hi, int32_t *out) {
    bool neg = false, big = false;
    uint32_t base = 10, acc = 0;
    if (*s == '+' || *s == '-') { neg = (*s == '-'); s++; }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
    if (*s == '\0') return NUM_E_SYNTAX;                   /* "", "-", "0x" */
    const uint32_t limit = neg ? 2147483648u : 2147483647u;
    for (; *s; s++) {
        uint32_t d = (uint32_t)digit_val(*s);
        if (d >= base) return NUM_E_SYNTAX;                /* "12abc", "0x1G", "1 2" */
        if (!big && acc > (limit - d) / base) big = true;  /* 넘치기 전에 잡는다 (C-M09) */
        if (!big) acc = acc * base + d;
    }
    if (big) return NUM_E_RANGE;
    int32_t v = neg ? (acc == 2147483648u ? INT32_MIN : -(int32_t)acc) : (int32_t)acc;
    if (v < lo || v > hi) return NUM_E_RANGE;
    *out = v;
    return NUM_OK;
}

/* 공백으로 제자리 분할. 토큰 수를 돌려주고, max_args 개를 넘으면 -1 (조용히 자르지 않는다). */
static int tokenize(char *s, char *argv[], int max_args) {
    int argc = 0;
    for (;;) {
        while (*s == ' ') s++;
        if (*s == '\0') return argc;
        if (argc == max_args) return -1;
        argv[argc++] = s;
        while (*s != '\0' && *s != ' ') s++;
        if (*s != '\0') *s++ = '\0';
    }
}

typedef enum {
    CLI_OK = 0, CLI_EMPTY, CLI_E_UNKNOWN, CLI_E_ARGC, CLI_E_TOO_MANY,
    CLI_E_SYNTAX, CLI_E_RANGE, CLI_E_LONG, CLI_N_STATUS
} cli_status_t;
static const char *const k_msg[CLI_N_STATUS] = {
    "OK", "", "ERR unknown command", "ERR usage: ", "ERR too many args",
    "ERR bad number", "ERR out of range", "ERR line too long",
};

typedef struct cli cli_t;
typedef cli_status_t (*cmd_fn)(cli_t *cli, int argc, char *argv[]);
typedef struct { const char *name; uint8_t min_args, max_args; cmd_fn fn; const char *usage; } cmd_t;
struct cli {
    le_t le;
    out_t out;
    const cmd_t *cmds; size_t n_cmds;
    cli_status_t (*exec)(cli_t *cli, const char *line);              /* 셸 또는 AT — 같은 코어 */
    void (*reply)(cli_t *cli, cli_status_t st, const char *usage);
    cli_status_t last;
    uint32_t n_status[CLI_N_STATUS];
};

/* 모든 명령의 끝: 상태를 세고 모드별 응답을 쓴다 */
static cli_status_t cli_finish(cli_t *cli, cli_status_t st, const char *usage) {
    cli->last = st;
    cli->n_status[st]++;
    cli->reply(cli, st, usage);
    return st;
}

static cli_status_t num_to_cli(num_status_t e) { return e == NUM_E_RANGE ? CLI_E_RANGE : CLI_E_SYNTAX; }

/* ---- 가짜 장치와 셸 명령 (주어짐) ---- */
static struct { uint8_t led; uint16_t reg[16]; uint32_t baud, baud_pending; } dev;
static void dev_reset(void) { memset(&dev, 0, sizeof dev); dev.baud = 115200; }
static void dev_tx_idle(void) {          /* USART TC: 마지막 응답 비트까지 나간 뒤에야 baud 를 바꾼다 */
    if (dev.baud_pending) { dev.baud = dev.baud_pending; dev.baud_pending = 0; }
}

static cli_status_t cmd_led(cli_t *cli, int argc, char *argv[]) {
    int32_t v; num_status_t e = parse_i32(argv[1], 0, 1, &v);
    (void)cli; (void)argc;
    if (e != NUM_OK) return num_to_cli(e);
    dev.led = (uint8_t)v;
    return CLI_OK;
}
static cli_status_t cmd_set(cli_t *cli, int argc, char *argv[]) {
    int32_t r, v; num_status_t e;
    (void)cli; (void)argc;
    if ((e = parse_i32(argv[1], 0, 15, &r)) != NUM_OK) return num_to_cli(e);
    if ((e = parse_i32(argv[2], 0, 0xFFFF, &v)) != NUM_OK) return num_to_cli(e);
    dev.reg[r] = (uint16_t)v;            /* 인자를 전부 검사한 뒤에만 쓴다 */
    return CLI_OK;
}
static cli_status_t cmd_get(cli_t *cli, int argc, char *argv[]) {
    int32_t r; num_status_t e = parse_i32(argv[1], 0, 15, &r);
    (void)argc;
    if (e != NUM_OK) return num_to_cli(e);
    out_putc(&cli->out, 'R'); out_u32(&cli->out, (uint32_t)r); out_putc(&cli->out, '=');
    out_u32(&cli->out, dev.reg[r]); out_puts(&cli->out, "\r\n");
    return CLI_OK;
}
static cli_status_t cmd_echo(cli_t *cli, int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) { if (i > 1) out_putc(&cli->out, ' '); out_puts(&cli->out, argv[i]); }
    out_puts(&cli->out, "\r\n");
    return CLI_OK;
}
static cli_status_t cmd_help(cli_t *cli, int argc, char *argv[]) {
    (void)argc; (void)argv;
    for (size_t i = 0; i < cli->n_cmds; i++) { out_puts(&cli->out, cli->cmds[i].usage); out_puts(&cli->out, "\r\n"); }
    return CLI_OK;
}
static const cmd_t k_shell_cmds[] = {
    { "led",  1, 1,                cmd_led,  "led <0|1>" },
    { "set",  2, 2,                cmd_set,  "set <reg 0..15> <val 0..0xFFFF>" },
    { "get",  1, 1,                cmd_get,  "get <reg 0..15>" },
    { "echo", 0, CLI_MAX_ARGS - 1, cmd_echo, "echo [word...]" },
    { "help", 0, 0,                cmd_help, "help" },
};

static void shell_reply(cli_t *cli, cli_status_t st, const char *usage) {
    if (st == CLI_EMPTY) return;                                  /* 빈 줄: 프롬프트만 다시 */
    out_puts(&cli->out, k_msg[st]);
    if (st == CLI_E_ARGC && usage) out_puts(&cli->out, usage);
    out_puts(&cli->out, "\r\n");
}

/* 셸 디스패처: 줄(const) → 복사 → 토큰 → 명령 찾기 → 인자 수 → 핸들러 → cli_finish */
static cli_status_t cli_exec(cli_t *cli, const char *line) {
    char work[CLI_LINE_MAX + 1];
    char *argv[CLI_MAX_ARGS];
    size_t n = 0;
    while (n <= CLI_LINE_MAX && line[n] != '\0') n++;            /* 상한 있는 strlen */
    if (n > CLI_LINE_MAX) return cli_finish(cli, CLI_E_LONG, NULL);
    memcpy(work, line, n + 1);
    int argc = tokenize(work, argv, CLI_MAX_ARGS);
    if (argc == 0) return cli_finish(cli, CLI_EMPTY, NULL);
    if (argc < 0) return cli_finish(cli, CLI_E_TOO_MANY, NULL);
    for (size_t i = 0; i < cli->n_cmds; i++) {
        const cmd_t *c = &cli->cmds[i];
        if (strcmp(argv[0], c->name) != 0) continue;
        if (argc - 1 < c->min_args || argc - 1 > c->max_args) return cli_finish(cli, CLI_E_ARGC, c->usage);
        return cli_finish(cli, c->fn(cli, argc, argv), NULL);
    }
    return cli_finish(cli, CLI_E_UNKNOWN, NULL);
}

/* ---------------- Part C: AT 명령 (같은 라인 에디터 · 같은 parse_i32) ---------------- */
typedef enum { AT_EXEC = 1, AT_READ = 2, AT_TEST = 4, AT_SET = 8 } at_op_t;
typedef cli_status_t (*at_fn)(cli_t *cli, at_op_t op, const char *arg);
typedef struct { const char *name; uint8_t ops; at_fn fn; } at_cmd_t;

static const uint32_t k_bauds[] = { 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600 };

static void at_info_u32(cli_t *cli, const char *prefix, uint32_t v) {   /* "\r\n+BAUD: 115200\r\n" */
    out_puts(&cli->out, "\r\n"); out_puts(&cli->out, prefix); out_u32(&cli->out, v); out_puts(&cli->out, "\r\n");
}
static cli_status_t at_baud(cli_t *cli, at_op_t op, const char *arg) {
    if (op == AT_READ) { at_info_u32(cli, "+BAUD: ", dev.baud); return CLI_OK; }
    if (op == AT_TEST) {
        out_puts(&cli->out, "\r\n+BAUD: (");
        for (size_t i = 0; i < sizeof k_bauds / sizeof k_bauds[0]; i++) {
            if (i) out_putc(&cli->out, ',');
            out_u32(&cli->out, k_bauds[i]);
        }
        out_puts(&cli->out, ")\r\n");
        return CLI_OK;
    }
    int32_t v; num_status_t e = parse_i32(arg, 1, 921600, &v);
    if (e != NUM_OK) return num_to_cli(e);
    for (size_t i = 0; i < sizeof k_bauds / sizeof k_bauds[0]; i++)
        if (k_bauds[i] == (uint32_t)v) { dev.baud_pending = (uint32_t)v; return CLI_OK; }  /* OK 를 보낸 뒤 적용 */
    return CLI_E_RANGE;
}
static cli_status_t at_led(cli_t *cli, at_op_t op, const char *arg) {
    if (op == AT_READ) { at_info_u32(cli, "+LED: ", dev.led); return CLI_OK; }
    if (op == AT_TEST) { out_puts(&cli->out, "\r\n+LED: (0,1)\r\n"); return CLI_OK; }
    int32_t v; num_status_t e = parse_i32(arg, 0, 1, &v);
    if (e != NUM_OK) return num_to_cli(e);
    dev.led = (uint8_t)v;
    return CLI_OK;
}
static const at_cmd_t k_at_cmds[] = {
    { "+BAUD", AT_READ | AT_TEST | AT_SET, at_baud },
    { "+LED",  AT_READ | AT_TEST | AT_SET, at_led  },
};

static char up(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c; }

static const at_cmd_t *at_find(const char *p, size_t n) {          /* 대소문자 무시, 길이까지 일치 */
    for (size_t i = 0; i < sizeof k_at_cmds / sizeof k_at_cmds[0]; i++) {
        const char *name = k_at_cmds[i].name;
        if (strlen(name) != n) continue;
        size_t j = 0;
        while (j < n && up(p[j]) == name[j]) j++;
        if (j == n) return &k_at_cmds[i];
    }
    return NULL;
}

static void at_reply(cli_t *cli, cli_status_t st, const char *usage) {   /* V.250 verbose: <CR><LF>text<CR><LF> */
    (void)usage;
    if (st == CLI_EMPTY) return;
    out_puts(&cli->out, st == CLI_OK ? "\r\nOK\r\n" : "\r\nERROR\r\n");
}

/* AT 디스패처: "AT" | "ATE0" | "ATE1" | "AT+NAME" | "AT+NAME?" | "AT+NAME=?" | "AT+NAME=<value>" */
static cli_status_t at_exec(cli_t *cli, const char *line) {
    if (line[0] == '\0') return cli_finish(cli, CLI_EMPTY, NULL);
    if (up(line[0]) != 'A' || up(line[1]) != 'T') return cli_finish(cli, CLI_E_UNKNOWN, NULL);
    const char *p = line + 2;
    if (*p == '\0') return cli_finish(cli, CLI_OK, NULL);                         /* "AT" = 살아 있나? */
    if (up(p[0]) == 'E' && (p[1] == '0' || p[1] == '1') && p[2] == '\0') {
        cli->le.echo = (p[1] == '1');
        return cli_finish(cli, CLI_OK, NULL);
    }
    if (*p != '+') return cli_finish(cli, CLI_E_UNKNOWN, NULL);
    size_t n = strcspn(p, "=?");
    const at_cmd_t *c = at_find(p, n);
    if (c == NULL) return cli_finish(cli, CLI_E_UNKNOWN, NULL);
    const char *q = p + n, *arg = NULL;
    at_op_t op;
    if (*q == '\0') op = AT_EXEC;
    else if (strcmp(q, "?") == 0) op = AT_READ;
    else if (strcmp(q, "=?") == 0) op = AT_TEST;
    else if (q[0] == '=' && q[1] != '\0') { op = AT_SET; arg = q + 1; }
    else return cli_finish(cli, CLI_E_SYNTAX, NULL);                              /* "=", "??" */
    if ((c->ops & op) == 0) return cli_finish(cli, CLI_E_UNKNOWN, NULL);
    return cli_finish(cli, c->fn(cli, op, arg), NULL);
}

typedef enum { MODE_SHELL, MODE_AT } cli_mode_t;
static void cli_init(cli_t *cli, cli_mode_t mode, bool echo) {
    memset(cli, 0, sizeof *cli);
    le_init(&cli->le, echo);
    cli->cmds = k_shell_cmds; cli->n_cmds = sizeof k_shell_cmds / sizeof k_shell_cmds[0];
    cli->exec  = (mode == MODE_AT) ? at_exec : cli_exec;
    cli->reply = (mode == MODE_AT) ? at_reply : shell_reply;
    cli->last  = CLI_N_STATUS;                                     /* "아직 실행 안 함" */
}

/* 바이트 하나 → 라인 에디터 → (줄 끝이면) 디스패처. 메인 루프 문맥에서만 부른다. */
static void cli_feed(cli_t *cli, uint8_t c) {
    le_event_t ev = le_feed(&cli->le, &cli->out, c);
    if (ev == LE_LINE) cli->exec(cli, cli->le.buf);
    else if (ev == LE_OVERFLOW) cli_finish(cli, CLI_E_LONG, NULL);
}

/* ---- UART RX ISR → 링 버퍼 → 메인 루프 ---- */
static struct { uint8_t buf[RX_RING_SIZE]; volatile uint16_t head, tail; uint32_t overrun; } rx;
static void rx_reset(void) { memset((void *)&rx, 0, sizeof rx); }

/* ISR 문맥: 생산자. head 만 쓴다. 가득 차면 새 바이트를 버리고 센다 (기다리면 안 된다). */
static void uart_rx_isr(uint8_t c) {
    uint16_t h = rx.head;
    if ((uint16_t)(h - rx.tail) == RX_RING_SIZE) { rx.overrun++; return; }
    rx.buf[h % RX_RING_SIZE] = c;
    rx.head = (uint16_t)(h + 1);   /* 데이터를 쓴 "뒤에" head 공개 (실제 MCU: 배리어/atomic, DS-A01) */
}
/* 메인 루프: 소비자. tail 만 쓴다. 쌓인 바이트를 전부 라인 에디터로. */
static void cli_poll(cli_t *cli) {
    while (rx.tail != rx.head) {
        uint8_t c = rx.buf[rx.tail % RX_RING_SIZE];
        rx.tail = (uint16_t)(rx.tail + 1);
        cli_feed(cli, c);
    }
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2026B14u;
static uint32_t xorshift32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

/* Part A 도우미: 바이트를 먹이고 나온 줄을 "줄|줄|" 로, 넘침은 "!|" 로 이어 붙인다 */
static char g_got[1024];
static const char *le_trace(le_t *le, out_t *o, const char *s, size_t n) {
    size_t g = 0;
    for (size_t i = 0; i < n; i++) {
        le_event_t ev = le_feed(le, o, (uint8_t)s[i]);
        const char *add = ev == LE_LINE ? le->buf : ev == LE_OVERFLOW ? "!" : NULL;
        if (add) { size_t k = strlen(add); memcpy(g_got + g, add, k); g += k; g_got[g++] = '|'; }
    }
    g_got[g] = '\0';
    return g_got;
}
#define LT(lit) le_trace(&le, &eo, lit, sizeof(lit) - 1)

static void feed_bytes(cli_t *c, const char *p, size_t n) { for (size_t i = 0; i < n; i++) cli_feed(c, (uint8_t)p[i]); }
/* 한 줄 실행: 응답 버퍼를 비우고, 바이트를 먹이고, CR 로 끝낸다 */
static cli_status_t run(cli_t *c, const char *s) {
    out_clear(&c->out); c->last = CLI_N_STATUS;
    feed_bytes(c, s, strlen(s)); cli_feed(c, '\r');
    return c->last;
}
#define RUN_FEED(c, lit) (out_clear(&(c)->out), (c)->last = CLI_N_STATUS, feed_bytes((c), lit, sizeof(lit) - 1), (c)->last)
static void isr_str(const char *s) { while (*s) uart_rx_isr((uint8_t)*s++); }

/* 퍼즈용 줄 생성기: 문법을 대충 따르는 줄에 쓰레기 바이트 · BS 를 섞고, 줄 끝은 CR / LF / CRLF / 없음 */
#define PICK(arr) (arr[xorshift32() % (sizeof arr / sizeof arr[0])])
static size_t append(char *b, size_t n, const char *s) { size_t k = strlen(s); memcpy(b + n, s, k); return n + k; }
static size_t mutate_and_end(char *b, size_t n) {
    if (xorshift32() % 5 == 0 && n) b[xorshift32() % n] = (char)(xorshift32() & 0xFF);  /* NUL, 0xFF, CR 포함 */
    if (xorshift32() % 8 == 0) b[n++] = (xorshift32() & 1) ? '\b' : 0x7F;
    static const char *const eol[] = { "\r", "\r", "\n", "\r\n", "\r\n", "" };
    return append(b, n, PICK(eol));
}
static size_t gen_shell_line(char *b) {
    static const char *const tok[] = { "led", "set", "get", "echo", "help", "LED", "foo", "0", "1", "15", "16",
                                       "0x1F", "-1", "99999999999", "12abc", "0x", "-", "65535", "0x10000" };
    static const char *const sep[] = { " ", " ", "  ", "\t" };
    size_t n = 0, ntok = xorshift32() % 12;
    for (size_t i = 0; i < ntok; i++) { if (i) n = append(b, n, PICK(sep)); n = append(b, n, PICK(tok)); }
    return mutate_and_end(b, n);
}
static size_t gen_at_line(char *b) {
    static const char *const pre[]  = { "AT", "AT", "at", "At", "A", "", "XY" };
    static const char *const body[] = { "", "E0", "E1", "E2", "+BAUD", "+BAUD", "+LED", "+LED", "+FOO", "+", "+baud" };
    static const char *const op[]   = { "", "?", "=?", "=", "=9600", "=115200", "=12345", "=99999999999", "=abc",
                                        "=0x1C200", "=1", "=0", "=2", "??", "=1;+LED?" };
    size_t n = append(b, 0, PICK(pre));
    n = append(b, n, PICK(body));
    n = append(b, n, PICK(op));
    if (xorshift32() % 10 == 0) { memset(b + n, 'Z', 40); n += 40; }           /* 넘치는 줄 */
    return mutate_and_end(b, n);
}

static le_t le; static out_t eo;
static cli_t sh, at;

int main(void) {
    /* ===== Part A: 라인 에디터 ===== */
    le_init(&le, false);
    CHECK(strcmp(LT("led 1\r"), "led 1|") == 0);
    CHECK(strcmp(LT("led 1\r\n"), "led 1|") == 0);                    /* CRLF = 1줄 */
    CHECK(strcmp(LT("a\rb\nc\r\nd\n\r"), "a|b|c|d||") == 0);           /* LF CR 은 2번 (두 번째는 빈 줄) */
    CHECK(strcmp(LT("x\r"), "x|") == 0 && strcmp(LT("\ny\r"), "y|") == 0);   /* CR 과 LF 가 다른 호출로 와도 1줄 */
    CHECK(strcmp(LT("lef\bd 1\r"), "led 1|") == 0);
    CHECK(strcmp(LT("lef\x7f" "d 1\r"), "led 1|") == 0);
    CHECK(strcmp(LT("\b\b\x7fok\r"), "ok|") == 0);                      /* 빈 줄에서 BS 는 무시 */
    CHECK(strcmp(LT("ab\b\b\b\bcd\r"), "cd|") == 0);
    CHECK(strcmp(LT("le\0d\x1b 1\r"), "led 1|") == 0 && le.n_ctrl_dropped == 2);   /* NUL, ESC 버림 */
    CHECK(strcmp(LT("\xc3\xa9t\xff" "a\r"), "ta|") == 0 && le.n_ctrl_dropped == 5);
    CHECK(strcmp(LT("set\t3\t7\r"), "set 3 7|") == 0);                  /* TAB → 공백 */
    {   /* 길이 경계: 40 글자는 OK, 41 글자는 넘침 */
        char s[1100]; memset(s, 'x', sizeof s);
        le_init(&le, false);
        s[CLI_LINE_MAX] = '\r';
        const char *g = le_trace(&le, &eo, s, CLI_LINE_MAX + 1);
        CHECK(strlen(g) == CLI_LINE_MAX + 1 && g[CLI_LINE_MAX] == '|');
        s[CLI_LINE_MAX] = 'x'; s[CLI_LINE_MAX + 1] = '\r';
        CHECK(strcmp(le_trace(&le, &eo, s, CLI_LINE_MAX + 2), "!|") == 0 && le.n_overflow == 1);
        CHECK(strcmp(LT("ok\r"), "ok|") == 0);                         /* 다음 줄은 정상 */
        memset(s, 'A', 1000); memcpy(s + 1000, "\r\n", 2);              /* 1000 글자 + CRLF → 에러 딱 1번 */
        CHECK(strcmp(le_trace(&le, &eo, s, 1002), "!|") == 0 && le.n_overflow == 2);
        memset(s, 'x', CLI_LINE_MAX + 1); memset(s + CLI_LINE_MAX + 1, '\b', 10); s[CLI_LINE_MAX + 11] = '\r';
        CHECK(strcmp(le_trace(&le, &eo, s, CLI_LINE_MAX + 12), "!|") == 0);   /* 버리는 중엔 BS 로 못 되살린다 */
        CHECK(le.len == 0 && le.n_lines == 2);
    }
    {   /* 에코 */
        le_init(&le, true); out_clear(&eo);
        CHECK(strcmp(LT("ab\x7f\r"), "a|") == 0 && strcmp(eo.buf, "ab\b \b\r\n") == 0);
        out_clear(&eo);
        CHECK(strcmp(LT("\bq\r\n"), "q|") == 0 && strcmp(eo.buf, "q\r\n") == 0);   /* 빈 줄 BS 는 에코도 없음 */
    }

    /* ===== Part B: 토크나이저 ===== */
    {
        char *av[CLI_MAX_ARGS];
        char t1[] = "  set   3  42 ", t2[] = "", t3[] = "      ";
        char t4[] = "a b c d e f g h", t5[] = "a b c d e f g h i", t6[] = "a b c d e f g h    ";
        CHECK(tokenize(t1, av, CLI_MAX_ARGS) == 3 && strcmp(av[0], "set") == 0 && strcmp(av[1], "3") == 0 && strcmp(av[2], "42") == 0);
        CHECK(tokenize(t2, av, CLI_MAX_ARGS) == 0 && tokenize(t3, av, CLI_MAX_ARGS) == 0);
        CHECK(tokenize(t4, av, CLI_MAX_ARGS) == 8 && strcmp(av[7], "h") == 0);
        CHECK(tokenize(t5, av, CLI_MAX_ARGS) == -1);                    /* 9개 → 에러 (자르지 않음) */
        CHECK(tokenize(t6, av, CLI_MAX_ARGS) == 8);
    }
    /* ===== Part B: 숫자 파서 표 ===== */
    {
        static const struct { const char *s; int32_t lo, hi; num_status_t st; int32_t v; } T[] = {
            { "0", INT32_MIN, INT32_MAX, NUM_OK, 0 },            { "42", INT32_MIN, INT32_MAX, NUM_OK, 42 },
            { "-42", INT32_MIN, INT32_MAX, NUM_OK, -42 },        { "+7", INT32_MIN, INT32_MAX, NUM_OK, 7 },
            { "0x1F", INT32_MIN, INT32_MAX, NUM_OK, 31 },        { "0XfF", INT32_MIN, INT32_MAX, NUM_OK, 255 },
            { "-0x10", INT32_MIN, INT32_MAX, NUM_OK, -16 },      { "007", INT32_MIN, INT32_MAX, NUM_OK, 7 },
            { "000000000000000000042", INT32_MIN, INT32_MAX, NUM_OK, 42 },
            { "2147483647", INT32_MIN, INT32_MAX, NUM_OK, INT32_MAX },
            { "-2147483648", INT32_MIN, INT32_MAX, NUM_OK, INT32_MIN },
            { "0x7FFFFFFF", INT32_MIN, INT32_MAX, NUM_OK, INT32_MAX },
            { "-0x80000000", INT32_MIN, INT32_MAX, NUM_OK, INT32_MIN },
            { "2147483648", INT32_MIN, INT32_MAX, NUM_E_RANGE, 0 }, { "-2147483649", INT32_MIN, INT32_MAX, NUM_E_RANGE, 0 },
            { "99999999999", INT32_MIN, INT32_MAX, NUM_E_RANGE, 0 }, { "0x80000000", INT32_MIN, INT32_MAX, NUM_E_RANGE, 0 },
            { "0xFFFFFFFF", INT32_MIN, INT32_MAX, NUM_E_RANGE, 0 },  { "0x100000000", INT32_MIN, INT32_MAX, NUM_E_RANGE, 0 },
            { "12abc", INT32_MIN, INT32_MAX, NUM_E_SYNTAX, 0 },  { "", INT32_MIN, INT32_MAX, NUM_E_SYNTAX, 0 },
            { "-", INT32_MIN, INT32_MAX, NUM_E_SYNTAX, 0 },      { "+", INT32_MIN, INT32_MAX, NUM_E_SYNTAX, 0 },
            { "0x", INT32_MIN, INT32_MAX, NUM_E_SYNTAX, 0 },     { "0x1G", INT32_MIN, INT32_MAX, NUM_E_SYNTAX, 0 },
            { "1 2", INT32_MIN, INT32_MAX, NUM_E_SYNTAX, 0 },    { "--1", INT32_MIN, INT32_MAX, NUM_E_SYNTAX, 0 },
            { "0x-5", INT32_MIN, INT32_MAX, NUM_E_SYNTAX, 0 },   { " 5", INT32_MIN, INT32_MAX, NUM_E_SYNTAX, 0 },
            { "99999999999x", INT32_MIN, INT32_MAX, NUM_E_SYNTAX, 0 },   /* 모양 검사가 범위보다 먼저 */
            { "1e3", INT32_MIN, INT32_MAX, NUM_E_SYNTAX, 0 },    { "0b101", INT32_MIN, INT32_MAX, NUM_E_SYNTAX, 0 },
            { "15", 0, 15, NUM_OK, 15 },  { "16", 0, 15, NUM_E_RANGE, 0 },  { "-1", 0, 15, NUM_E_RANGE, 0 },
            { "-0", 0, 15, NUM_OK, 0 },
        };
        int bad = 0;
        for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
            int32_t v = 12345;
            num_status_t st = parse_i32(T[i].s, T[i].lo, T[i].hi, &v);
            bool ok = st == T[i].st && v == (st == NUM_OK ? T[i].v : 12345);   /* 실패하면 *out 그대로 */
            if (!ok) { bad++; printf("  parse_i32(\"%s\") -> st=%d v=%d\n", T[i].s, (int)st, (int)v); }
        }
        CHECK(bad == 0);
        printf("parse_i32 table: %zu rows, bad=%d\n", sizeof T / sizeof T[0], bad);
    }
    {   /* 랜덤 왕복: snprintf 로 만든 문자열 → parse_i32 */
        int bad = 0; char s[40];
        for (int i = 0; i < 20000; i++) {
            int32_t v = (int32_t)xorshift32(), got = 0;
            snprintf(s, sizeof s, (i & 1) ? "%d" : "%+012d", (int)v);
            if (parse_i32(s, INT32_MIN, INT32_MAX, &got) != NUM_OK || got != v) bad++;
            if (v >= 0) {
                snprintf(s, sizeof s, (i & 2) ? "0x%x" : "0X%08X", (unsigned)v);
                if (parse_i32(s, INT32_MIN, INT32_MAX, &got) != NUM_OK || got != v) bad++;
            }
        }
        CHECK(bad == 0);
        int bad_r = 0, bad_s = 0;
        for (int i = 0; i < 20000; i++) {       /* int32 밖의 64비트 값 → RANGE */
            int64_t w = (int64_t)(((uint64_t)xorshift32() << 32) | xorshift32()) >> (xorshift32() % 31);
            if (w >= INT32_MIN && w <= INT32_MAX) w = (w < 0) ? w - 2147483649LL : w + 2147483648LL;
            int32_t got = 7;
            snprintf(s, sizeof s, "%lld", (long long)w);
            if (parse_i32(s, INT32_MIN, INT32_MAX, &got) != NUM_E_RANGE || got != 7) bad_r++;
            snprintf(s, sizeof s, "%d%c", (int)(int32_t)xorshift32(), "xZ.+-_ g"[xorshift32() % 8]);
            if (parse_i32(s, INT32_MIN, INT32_MAX, &got) != NUM_E_SYNTAX) bad_s++;  /* 꼬리 쓰레기 */
        }
        CHECK(bad_r == 0 && bad_s == 0);
        printf("random parse: 20000 round trips, 20000 out-of-range (bad=%d), 20000 trailing junk (bad=%d)\n", bad_r, bad_s);
    }

    /* ===== Part B: 디스패처 (셸 모드, 에코 끔) ===== */
    dev_reset();
    cli_init(&sh, MODE_SHELL, false);
    CHECK(run(&sh, "led 1") == CLI_OK && dev.led == 1 && strcmp(sh.out.buf, "OK\r\n") == 0);
    CHECK(run(&sh, "led") == CLI_E_ARGC && strcmp(sh.out.buf, "ERR usage: led <0|1>\r\n") == 0);
    CHECK(run(&sh, "led 1 2") == CLI_E_ARGC);
    CHECK(run(&sh, "led 2") == CLI_E_RANGE && dev.led == 1 && strcmp(sh.out.buf, "ERR out of range\r\n") == 0);
    CHECK(run(&sh, "led on") == CLI_E_SYNTAX && strcmp(sh.out.buf, "ERR bad number\r\n") == 0);
    CHECK(run(&sh, "LED 1") == CLI_E_UNKNOWN && strcmp(sh.out.buf, "ERR unknown command\r\n") == 0);
    CHECK(run(&sh, "set 3 0x1234") == CLI_OK && dev.reg[3] == 0x1234);
    CHECK(run(&sh, "get 3") == CLI_OK && strcmp(sh.out.buf, "R3=4660\r\nOK\r\n") == 0);
    CHECK(run(&sh, "  set   0x0F   65535  ") == CLI_OK && dev.reg[15] == 65535);
    CHECK(run(&sh, "set 16 1") == CLI_E_RANGE);
    CHECK(run(&sh, "set 3 -1") == CLI_E_RANGE && dev.reg[3] == 0x1234);
    CHECK(run(&sh, "set 3 0x10000") == CLI_E_RANGE && dev.reg[3] == 0x1234);
    CHECK(run(&sh, "set 3 12abc") == CLI_E_SYNTAX && dev.reg[3] == 0x1234);            /* 악성: 꼬리 쓰레기 */
    CHECK(run(&sh, "set 3 99999999999") == CLI_E_RANGE && dev.reg[3] == 0x1234);       /* 악성: 넘침 */
    CHECK(run(&sh, "set 1 2 3 4 5 6 7 8") == CLI_E_TOO_MANY && strcmp(sh.out.buf, "ERR too many args\r\n") == 0);
    CHECK(run(&sh, "echo a b c d e f g") == CLI_OK && strcmp(sh.out.buf, "a b c d e f g\r\nOK\r\n") == 0);
    CHECK(run(&sh, "     ") == CLI_EMPTY && sh.out.len == 0);                           /* 악성: 공백만 */
    CHECK(run(&sh, "") == CLI_EMPTY && sh.out.len == 0);
    CHECK(run(&sh, "help") == CLI_OK && strstr(sh.out.buf, "set <reg 0..15> <val 0..0xFFFF>\r\n") != NULL);
    CHECK(RUN_FEED(&sh, "set 4 1\0" "2\r") == CLI_OK && dev.reg[4] == 12);              /* 악성: NUL 은 버려지고 줄은 안 잘린다 */
    {
        char s[300]; strcpy(s, "set 3 "); memset(s + 6, '9', 250); s[256] = '\0';
        CHECK(run(&sh, s) == CLI_E_LONG && strcmp(sh.out.buf, "ERR line too long\r\n") == 0 && dev.reg[3] == 0x1234);
        CHECK(run(&sh, "get 3") == CLI_OK);                                             /* 넘침 뒤 회복 */
        CHECK(cli_exec(&sh, s) == CLI_E_LONG);                                          /* 직접 불러도 상한 검사 */
    }
    {   /* 성질 테스트: 랜덤 유효 set 은 항상 성공, 랜덤 무효 set 은 항상 실패 + 레지스터 불변 */
        int bad = 0; char s[64];
        for (int i = 0; i < 5000; i++) {
            uint32_t r = xorshift32() % 16, v = xorshift32() % 65536, f = xorshift32() % 4;
            const char *fmt = f == 0 ? "set %u %u" : f == 1 ? "set 0x%x 0x%x" : f == 2 ? " set  %03u  0X%04X " : "set\t%u\t%05u";
            snprintf(s, sizeof s, fmt, r, v);
            if (run(&sh, s) != CLI_OK || dev.reg[r] != v) bad++;
        }
        CHECK(bad == 0);
        uint16_t snap[16]; memcpy(snap, dev.reg, sizeof snap);
        bad = 0;
        for (int i = 0; i < 5000; i++) {
            uint32_t k = xorshift32() % 4;
            if (k == 0) snprintf(s, sizeof s, "set %u 1", 16 + xorshift32() % 100000u);
            else if (k == 1) snprintf(s, sizeof s, "set 1 %u", 65536 + xorshift32() % 1000000u);
            else if (k == 2) snprintf(s, sizeof s, "set 1 %u%c", xorshift32() % 65536, "g.,x-+"[xorshift32() % 6]);
            else snprintf(s, sizeof s, "set 1 -%u", 1 + xorshift32() % 65536);
            cli_status_t st = run(&sh, s);
            if (st != CLI_E_RANGE && st != CLI_E_SYNTAX) bad++;
        }
        CHECK(bad == 0 && memcmp(snap, dev.reg, sizeof snap) == 0);
    }

    /* ===== Part C: AT 모드 ===== */
    dev_reset();
    cli_init(&at, MODE_AT, false);
    CHECK(run(&at, "AT") == CLI_OK && strcmp(at.out.buf, "\r\nOK\r\n") == 0);
    CHECK(run(&at, "at") == CLI_OK);
    CHECK(run(&at, "AT+BAUD?") == CLI_OK && strcmp(at.out.buf, "\r\n+BAUD: 115200\r\n\r\nOK\r\n") == 0);
    CHECK(run(&at, "AT+BAUD=9600") == CLI_OK && dev.baud == 115200 && dev.baud_pending == 9600);  /* OK 는 옛 baud 로 */
    dev_tx_idle();
    CHECK(dev.baud == 9600 && dev.baud_pending == 0);
    CHECK(run(&at, "at+baud?") == CLI_OK && strcmp(at.out.buf, "\r\n+BAUD: 9600\r\n\r\nOK\r\n") == 0);
    CHECK(run(&at, "AT+BAUD=?") == CLI_OK &&
          strcmp(at.out.buf, "\r\n+BAUD: (9600,19200,38400,57600,115200,230400,460800,921600)\r\n\r\nOK\r\n") == 0);
    CHECK(run(&at, "AT+BAUD=12345") == CLI_E_RANGE && strcmp(at.out.buf, "\r\nERROR\r\n") == 0 && dev.baud_pending == 0);
    CHECK(run(&at, "AT+BAUD=") == CLI_E_SYNTAX);
    CHECK(run(&at, "AT+BAUD=abc") == CLI_E_SYNTAX);
    CHECK(run(&at, "AT+BAUD=99999999999") == CLI_E_RANGE && dev.baud_pending == 0);
    CHECK(run(&at, "AT+BAUD=0x1C200") == CLI_OK && dev.baud_pending == 115200);
    CHECK(run(&at, "AT+BAUD??") == CLI_E_SYNTAX);
    CHECK(run(&at, "AT+BAUD") == CLI_E_UNKNOWN);                 /* 실행형(EXEC)은 지원 안 함 */
    CHECK(run(&at, "AT+FOO?") == CLI_E_UNKNOWN && run(&at, "AT+") == CLI_E_UNKNOWN && run(&at, "AT+BAU?") == CLI_E_UNKNOWN);
    CHECK(run(&at, "XYZ") == CLI_E_UNKNOWN && run(&at, "A") == CLI_E_UNKNOWN && run(&at, "ATX") == CLI_E_UNKNOWN);
    CHECK(run(&at, "AT+LED=1") == CLI_OK && dev.led == 1);
    CHECK(run(&at, "AT+LED?") == CLI_OK && strcmp(at.out.buf, "\r\n+LED: 1\r\n\r\nOK\r\n") == 0);
    CHECK(run(&at, "AT+LED=2") == CLI_E_RANGE && dev.led == 1);
    CHECK(run(&at, "") == CLI_EMPTY && at.out.len == 0);
    {
        char s[200]; memcpy(s, "AT+BAUD=", 8); memset(s + 8, '1', 150); s[158] = '\0';
        CHECK(run(&at, s) == CLI_E_LONG && strcmp(at.out.buf, "\r\nERROR\r\n") == 0);
    }
    {   /* ATE0 / ATE1: 에코는 라인 에디터의 기능 — AT 가 켜고 끈다 */
        cli_init(&at, MODE_AT, true);
        CHECK(run(&at, "ATE0") == CLI_OK && strcmp(at.out.buf, "ATE0\r\n\r\nOK\r\n") == 0);
        CHECK(run(&at, "AT") == CLI_OK && strcmp(at.out.buf, "\r\nOK\r\n") == 0);
        CHECK(run(&at, "ATE1") == CLI_OK && strcmp(at.out.buf, "\r\nOK\r\n") == 0);
        CHECK(run(&at, "AT") == CLI_OK && strcmp(at.out.buf, "AT\r\n\r\nOK\r\n") == 0);
        CHECK(run(&at, "ATE2") == CLI_E_UNKNOWN);
    }

    /* ===== Part C: ISR → 링 버퍼 → 메인 루프 ===== */
    dev_reset(); rx_reset();
    cli_init(&at, MODE_AT, false);
    isr_str("AT+LED=1\r");
    CHECK(at.last == CLI_N_STATUS);                               /* ISR 은 파싱하지 않는다 */
    cli_poll(&at);
    CHECK(at.last == CLI_OK && dev.led == 1 && rx.tail == rx.head);
    {   /* 호스트가 OK 를 안 기다리고 11개를 몰아 보냄 (99 바이트) → 링 64 바이트 */
        uint32_t ok0 = at.n_status[CLI_OK];
        for (int i = 0; i < 11; i++) isr_str("AT+LED=0\r");
        CHECK(rx.overrun == 99 - RX_RING_SIZE);
        cli_poll(&at);
        CHECK(at.n_status[CLI_OK] - ok0 == 7);                    /* 63 바이트 = 7 명령, 64번째 'A' 는 다음 줄에 남음 */
        isr_str("AT\r"); cli_poll(&at);
        CHECK(at.last == CLI_E_UNKNOWN);                          /* "AAT" → ERROR: 잃어버린 바이트의 흔적 */
        isr_str("AT\r"); cli_poll(&at);
        CHECK(at.last == CLI_OK);                                 /* 한 줄 만에 회복 */
    }
    {   /* 고정 시드 문법 퍼즈: 그럴듯한 줄 + 쓰레기 바이트를 ISR 로 넣고, 랜덤 간격으로 poll */
        bool inv_ok = true;
        for (int mode = 0; mode < 2; mode++) {
            cli_t *c = mode ? &at : &sh;
            cli_init(c, mode ? MODE_AT : MODE_SHELL, true);
            dev_reset(); rx_reset();
            int since_poll = 0, poll_every = 1;
            char line[160];
            for (int sent = 0; sent < 150000;) {
                size_t n = mode ? gen_at_line(line) : gen_shell_line(line);
                for (size_t j = 0; j < n; j++) uart_rx_isr((uint8_t)line[j]);
                sent += (int)n; since_poll += (int)n;
                if (since_poll >= poll_every) {
                    cli_poll(c); since_poll = 0; poll_every = 1 + (int)(xorshift32() % 40);
                    if (c->le.len > CLI_LINE_MAX || c->le.buf[c->le.len] != '\0' ||
                        (!c->le.ready && strlen(c->le.buf) != c->le.len) || c->out.len > OUT_CAP) inv_ok = false;
                    if (c->out.len > OUT_CAP / 2) out_clear(&c->out);    /* TX 가 비워 줬다고 치자 */
                }
            }
            cli_poll(c);
            printf("fuzz %-5s: OK=%u EMPTY=%u UNKNOWN=%u ARGC=%u TOO_MANY=%u SYNTAX=%u RANGE=%u LONG=%u  overrun=%u ctrl_dropped=%u\n",
                   mode ? "AT" : "shell", c->n_status[CLI_OK], c->n_status[CLI_EMPTY], c->n_status[CLI_E_UNKNOWN],
                   c->n_status[CLI_E_ARGC], c->n_status[CLI_E_TOO_MANY], c->n_status[CLI_E_SYNTAX],
                   c->n_status[CLI_E_RANGE], c->n_status[CLI_E_LONG], rx.overrun, c->le.n_ctrl_dropped);
            bool cov = true;     /* 퍼즈가 모든 결과 경로를 밟았나 (AT 엔 ARGC/TOO_MANY 가 없다) */
            for (int s = 0; s < CLI_N_STATUS; s++)
                if (c->n_status[s] == 0 && !(mode && (s == CLI_E_ARGC || s == CLI_E_TOO_MANY))) cov = false;
            CHECK(cov);
            bool baud_ok = dev.baud_pending == 0;
            for (size_t i = 0; i < sizeof k_bauds / sizeof k_bauds[0]; i++) if (dev.baud_pending == k_bauds[i]) baud_ok = true;
            CHECK(dev.led <= 1 && baud_ok && dev.baud == 115200);
            c->le.echo = false;
            isr_str(mode ? "\rAT\r" : "\rled 1\r"); cli_poll(c);          /* 무엇이 들어왔든 다음 줄에서 회복 */
            CHECK(c->last == CLI_OK);
        }
        CHECK(inv_ok);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
