/* COMM-A10 로직 애널라이저 디코더: 샘플 캡처(CSV/VCD) → UART(auto-baud) · SPI · I2C 트랜잭션
 *
 * 캡처 표현: 채널마다 "시작 레벨 + 토글 시각(ps) 목록". CSV 와 VCD 를 같은 표현으로 읽는다.
 * 테스트 캡처는 이 파일 안의 작은 송신 시뮬레이터(COMM-B01 UART · COMM-B04 SPI · COMM-B06 I2C
 * 모델 축약판)가 만든 이상 파형을 LA 샘플링(올림 양자화) · 지터 · 글리치 · 클럭 오차로 망가뜨려 만든다.
 *
 * build : cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 * CLI   : ./solution FILE.vcd|FILE.csv uart ch=TX [fmt=8N1] [baud=auto|115200] [glitch=NS]
 *         ./solution FILE spi cs=CS_N sck=SCLK mosi=MOSI [miso=MISO] [mode=0] [bits=8] [lsb=1]
 *         ./solution FILE i2c [sda=SDA scl=SCL] [glitch=NS]        (sda/scl 생략 → 역할 추정)
 *         ./solution --emit-demo > demo.vcd                         (데모 캡처를 VCD 로 출력)
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ 캡처 표현 */
#define LA_MAX_CH    8
#define LA_MAX_EDGES 16384
#define LA_NAME_MAX  16
#define PS_PER_NS    1000LL
#define PS_PER_S     1000000000000LL

typedef struct {
    char     name[LA_NAME_MAX];
    uint8_t  init;                  /* 캡처 시작(t0) 레벨 */
    uint32_t n;                     /* 토글 개수 */
    int64_t  t[LA_MAX_EDGES];       /* 토글 시각(ps), 증가. t[i] 뒤의 레벨 = init ^ ((i+1)&1) */
} la_chan;

typedef struct {
    int     nch;
    int64_t t0, t_end;              /* ps */
    la_chan ch[LA_MAX_CH];
} la_capture;

enum {
    LA_OK = 0, LA_E_HDR = -1, LA_E_COLS = -2, LA_E_VAL = -3, LA_E_TIME = -4, LA_E_CAP = -5,
    LA_E_EMPTY = -6, LA_E_TSCALE = -7, LA_E_VAR = -8, LA_E_ID = -9, LA_E_XZ = -10, LA_E_INIT = -11,
    LA_E_EVCAP = -12, LA_E_ARG = -13, LA_E_NOEDGE = -14
};

/* 디코드 결과 = LA "protocol table" 한 줄 */
enum { EV_UART = 1, EV_SPI, EV_SPI_PARTIAL, EV_I2C_START, EV_I2C_RSTART, EV_I2C_ADDR, EV_I2C_DATA,
       EV_I2C_STOP, EV_I2C_MIDBYTE, EV_TRUNC };
enum { F_PERR = 1, F_FERR = 2, F_BREAK = 4, F_NACK = 8, F_CPOL = 16 };

typedef struct {
    int64_t  t0, t1;                /* ps */
    uint8_t  type, flags;
    uint16_t aux;                   /* UART: -, SPI: bits, I2C ADDR: R/W, MIDBYTE/TRUNC: 남은 비트 */
    uint32_t val, val2;             /* UART byte / SPI MOSI,MISO / I2C addr or data */
} la_event;

typedef struct { la_event *ev; int cap, n, over; } la_sink;

static void emit(la_sink *s, int64_t t0, int64_t t1, int type, int flags, int aux, uint32_t val, uint32_t val2) {
    if (s->n >= s->cap) { s->over = 1; return; }
    la_event e = { t0, t1, (uint8_t)type, (uint8_t)flags, (uint16_t)aux, val, val2 };
    s->ev[s->n++] = e;
}
static int sink_result(const la_sink *s) { return s->over ? LA_E_EVCAP : s->n; }

static int chan_now(const la_chan *c) { return c->init ^ (int)(c->n & 1u); }

/* 시각 t 에 레벨 v 로 (이미 v 면 아무것도 안 함). 같은 시각 두 번 토글 = 폭 0 펄스 → 상쇄 */
static int la_set(la_chan *c, int64_t t, int v) {
    if (v == chan_now(c)) return LA_OK;
    if (c->n > 0 && t < c->t[c->n - 1]) return LA_E_TIME;
    if (c->n > 0 && t == c->t[c->n - 1]) { c->n--; return LA_OK; }
    if (c->n >= LA_MAX_EDGES) return LA_E_CAP;
    c->t[c->n++] = t;
    return LA_OK;
}

/* ---- TODO A2: t 이하(<=)인 토글 개수 (이진 탐색) ---- */
static uint32_t la_edge_upper(const la_chan *c, int64_t t) {
    uint32_t lo = 0, hi = c->n;               /* 답은 [lo, hi] 안. t[lo-1] <= t < t[hi] */
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (c->t[mid] <= t) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}
static int la_level_at(const la_chan *c, int64_t t) { return c->init ^ (int)(la_edge_upper(c, t) & 1u); }

static int la_find(const la_capture *cp, const char *name) {
    for (int i = 0; i < cp->nch; i++) if (strcmp(cp->ch[i].name, name) == 0) return i;
    return -1;
}

/* ------------------------------------------------------------------ CSV: time_ns,ch0,ch1,... */
static bool is_sp(char ch) { return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'; }
static void trim(const char **b, const char **e) {
    while (*b < *e && is_sp(**b)) (*b)++;
    while (*e > *b && is_sp((*e)[-1])) (*e)--;
}
static bool tok_eq(const char *b, const char *e, const char *s) {
    size_t n = strlen(s);
    return (size_t)(e - b) == n && memcmp(b, s, n) == 0;
}
/* "-12.345" → unit_ps 단위 정수 ps. 정수부 ≤ 15자리, 소수 ≤ max_frac 자리, 쓰레기 금지 */
static int parse_fixed(const char *b, const char *e, int64_t unit_ps, int max_frac, int64_t *out) {
    bool neg = false;
    if (b < e && *b == '-') { neg = true; b++; }
    int64_t ip = 0, fp = 0, div = 1;
    int nd = 0, nf = 0;
    while (b < e && *b >= '0' && *b <= '9') { if (++nd > 15) return -1; ip = ip * 10 + (*b++ - '0'); }
    if (nd == 0) return -1;
    if (b < e && *b == '.') {
        b++;
        while (b < e && *b >= '0' && *b <= '9') { if (++nf > max_frac) return -1; fp = fp * 10 + (*b++ - '0'); div *= 10; }
        if (nf == 0) return -1;
    }
    if (b != e || unit_ps % div != 0 || ip > INT64_MAX / unit_ps / 2) return -1;    /* 오버플로 방지 */
    int64_t v = ip * unit_ps + fp * (unit_ps / div);
    *out = neg ? -v : v;
    return 0;
}

int la_parse_csv(const char *s, size_t len, la_capture *cp) {
    const char *p = s, *end = s + len;
    int row = 0;
    int64_t unit = 0, last_t = 0;
    memset(cp, 0, sizeof *cp);
    while (p < end) {
        const char *le = p;
        while (le < end && *le != '\n') le++;
        const char *lb = p, *lx = le;
        p = le < end ? le + 1 : end;
        trim(&lb, &lx);
        if (lb == lx) continue;                                   /* 빈 줄 */
        int col = 0;
        int64_t t = 0;
        int vals[LA_MAX_CH];
        const char *fb = lb;
        while (fb <= lx) {
            const char *fe = fb;
            while (fe < lx && *fe != ',') fe++;
            const char *b = fb, *e = fe;
            trim(&b, &e);
            if (row == 0) {                                       /* 헤더 */
                if (col == 0) {
                    if (tok_eq(b, e, "time_ns")) unit = PS_PER_NS;
                    else if (tok_eq(b, e, "time_ps")) unit = 1;
                    else return LA_E_HDR;
                } else {
                    if (col > LA_MAX_CH) return LA_E_CAP;
                    size_t n = (size_t)(e - b);
                    if (n == 0 || n >= LA_NAME_MAX) return LA_E_HDR;
                    memcpy(cp->ch[col - 1].name, b, n);
                }
            } else {
                if (col > cp->nch) return LA_E_COLS;
                if (col == 0) {
                    if (parse_fixed(b, e, unit, unit == PS_PER_NS ? 3 : 0, &t) != 0) return LA_E_VAL;
                } else {
                    if (e - b != 1 || (*b != '0' && *b != '1')) return LA_E_VAL;
                    vals[col - 1] = *b - '0';
                }
            }
            col++;
            fb = fe + 1;
        }
        if (row == 0) {
            if (col < 2) return LA_E_HDR;
            cp->nch = col - 1;
        } else {
            if (col != cp->nch + 1) return LA_E_COLS;
            if (row == 1) {
                cp->t0 = t;
                for (int c = 0; c < cp->nch; c++) cp->ch[c].init = (uint8_t)vals[c];
            } else {
                if (t <= last_t) return LA_E_TIME;
                for (int c = 0; c < cp->nch; c++) {
                    int rc = la_set(&cp->ch[c], t, vals[c]);
                    if (rc) return rc;
                }
            }
            last_t = t;
        }
        row++;
    }
    if (row < 2) return LA_E_EMPTY;
    cp->t_end = last_t;
    return LA_OK;
}

/* ------------------------------------------------------------------ VCD 부분집합 */
static bool next_tok(const char **pp, const char *e, const char **tb, const char **te) {
    const char *p = *pp;
    while (p < e && is_sp(*p)) p++;
    if (p >= e) { *pp = p; return false; }
    *tb = p;
    while (p < e && !is_sp(*p)) p++;
    *te = p;
    *pp = p;
    return true;
}
static bool skip_to_end(const char **pp, const char *e) {
    const char *b, *x;
    while (next_tok(pp, e, &b, &x)) if (tok_eq(b, x, "$end")) return true;
    return false;
}

/* ---- TODO A1: VCD 파서 ($timescale, $var wire 1 <id> <name>, $enddefinitions, #t, 0<id>/1<id>) ---- */
int la_parse_vcd(const char *s, size_t len, la_capture *cp) {
    const char *p = s, *e = s + len, *b, *x;
    char ids[LA_MAX_CH][8];
    bool init_set[LA_MAX_CH] = { false };
    bool defs_done = false, have_time = false;
    int64_t unit = 0, now = 0;
    memset(cp, 0, sizeof *cp);
    while (next_tok(&p, e, &b, &x)) {
        if (!defs_done) {
            if (tok_eq(b, x, "$timescale")) {             /* "1ns" 또는 "1 ns" */
                char ts[16];
                size_t n = 0;
                bool ok = false;
                while (next_tok(&p, e, &b, &x)) {
                    if (tok_eq(b, x, "$end")) { ok = true; break; }
                    if (n + (size_t)(x - b) >= sizeof ts) return LA_E_TSCALE;
                    memcpy(ts + n, b, (size_t)(x - b));
                    n += (size_t)(x - b);
                }
                if (!ok) return LA_E_TSCALE;
                ts[n] = 0;
                int64_t mul = 0;
                size_t k = 0;
                if (strncmp(ts, "100", 3) == 0) { mul = 100; k = 3; }
                else if (strncmp(ts, "10", 2) == 0) { mul = 10; k = 2; }
                else if (strncmp(ts, "1", 1) == 0) { mul = 1; k = 1; }
                else return LA_E_TSCALE;
                const char *u = ts + k;
                if (strcmp(u, "s") == 0) unit = mul * PS_PER_S;
                else if (strcmp(u, "ms") == 0) unit = mul * 1000000000LL;
                else if (strcmp(u, "us") == 0) unit = mul * 1000000LL;
                else if (strcmp(u, "ns") == 0) unit = mul * PS_PER_NS;
                else if (strcmp(u, "ps") == 0) unit = mul;
                else return LA_E_TSCALE;
            } else if (tok_eq(b, x, "$var")) {            /* $var wire 1 ! CS_N $end */
                const char *f[4][2];
                for (int i = 0; i < 4; i++) {
                    if (!next_tok(&p, e, &f[i][0], &f[i][1]) || tok_eq(f[i][0], f[i][1], "$end")) return LA_E_VAR;
                }
                if (!tok_eq(f[1][0], f[1][1], "1")) return LA_E_VAR;          /* 1비트 wire 만 */
                if (cp->nch >= LA_MAX_CH) return LA_E_CAP;
                size_t il = (size_t)(f[2][1] - f[2][0]), nl = (size_t)(f[3][1] - f[3][0]);
                if (il >= sizeof ids[0] || nl >= LA_NAME_MAX) return LA_E_VAR;
                memcpy(ids[cp->nch], f[2][0], il);
                ids[cp->nch][il] = 0;
                memcpy(cp->ch[cp->nch].name, f[3][0], nl);
                cp->nch++;
                if (!skip_to_end(&p, e)) return LA_E_VAR;                    /* [7:0] 같은 꼬리 무시 */
            } else if (tok_eq(b, x, "$enddefinitions")) {
                if (!skip_to_end(&p, e)) return LA_E_HDR;
                if (unit == 0) return LA_E_TSCALE;
                if (cp->nch == 0) return LA_E_EMPTY;
                defs_done = true;
            } else if (*b == '$') {
                if (!skip_to_end(&p, e)) return LA_E_HDR;                    /* $date $version $scope ... */
            } else {
                return LA_E_HDR;
            }
            continue;
        }
        if (*b == '#') {
            int64_t t;
            if (parse_fixed(b + 1, x, unit, 0, &t) != 0) return LA_E_TIME;
            if (have_time && t <= now) return LA_E_TIME;
            if (!have_time) cp->t0 = t;
            now = t;
            have_time = true;
        } else if (tok_eq(b, x, "$dumpvars") || tok_eq(b, x, "$end") || tok_eq(b, x, "$dumpall") ||
                   tok_eq(b, x, "$dumpon") || tok_eq(b, x, "$dumpoff")) {
            /* 값 블록 표시일 뿐 */
        } else if (tok_eq(b, x, "$comment")) {
            if (!skip_to_end(&p, e)) return LA_E_HDR;
        } else if (strchr("01xXzZ", *b) != NULL) {
            size_t il = (size_t)(x - b - 1);
            int c = -1;
            for (int i = 0; i < cp->nch; i++)
                if (strlen(ids[i]) == il && memcmp(ids[i], b + 1, il) == 0) { c = i; break; }
            if (c < 0) return LA_E_ID;
            if (*b != '0' && *b != '1') return LA_E_XZ;
            if (!have_time) return LA_E_INIT;
            int v = *b - '0';
            if (!init_set[c] || now == cp->t0) {
                if (now != cp->t0) return LA_E_INIT;               /* 시작 시각에 값이 없던 채널 */
                cp->ch[c].init = (uint8_t)v;
                init_set[c] = true;
            } else {
                int rc = la_set(&cp->ch[c], now, v);
                if (rc) return rc;
            }
        } else if (*b == 'b' || *b == 'B' || *b == 'r' || *b == 'R') {
            return LA_E_VAR;                                         /* 벡터/실수 값은 지원 안 함 */
        } else {
            return LA_E_HDR;
        }
    }
    if (!defs_done) return LA_E_HDR;
    if (!have_time) return LA_E_EMPTY;
    for (int i = 0; i < cp->nch; i++) if (!init_set[i]) return LA_E_INIT;
    cp->t_end = now;
    return LA_OK;
}

/* ------------------------------------------------------------------ 쓰기 (CSV / VCD) */
typedef struct { char *b; size_t cap, n; int over; } wbuf;
static void wput(wbuf *w, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void wput(wbuf *w, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    size_t room = w->n < w->cap ? w->cap - w->n : 0;
    int k = vsnprintf(room ? w->b + w->n : NULL, room, fmt, ap);
    va_end(ap);
    if (k < 0 || (size_t)k >= room) w->over = 1;
    w->n += k > 0 ? (size_t)k : 0;
}
static void fmt_ns(char *o, size_t cap, int64_t t_ps) {                  /* ps → "123" 또는 "123.456" ns */
    const char *sg = t_ps < 0 ? "-" : "";
    int64_t a = t_ps < 0 ? -t_ps : t_ps;
    if (a % PS_PER_NS == 0) snprintf(o, cap, "%s%" PRId64, sg, a / PS_PER_NS);
    else snprintf(o, cap, "%s%" PRId64 ".%03" PRId64, sg, a / PS_PER_NS, a % PS_PER_NS);
}
/* 모든 채널의 토글을 시간순으로 합치며 cb(시각, 레벨들) — 첫 호출은 t0 의 초기 상태 */
static int64_t merge_next(const la_capture *cp, const uint32_t *idx) {
    int64_t best = INT64_MAX;
    for (int c = 0; c < cp->nch; c++)
        if (idx[c] < cp->ch[c].n && cp->ch[c].t[idx[c]] < best) best = cp->ch[c].t[idx[c]];
    return best;
}
int la_write_csv(const la_capture *cp, char *buf, size_t cap) {
    wbuf w = { buf, cap, 0, 0 };
    uint32_t idx[LA_MAX_CH] = { 0 };
    int lv[LA_MAX_CH];
    char ts[32];
    wput(&w, "time_ns");
    for (int c = 0; c < cp->nch; c++) { wput(&w, ",%s", cp->ch[c].name); lv[c] = cp->ch[c].init; }
    wput(&w, "\n");
    int64_t t = cp->t0, last = cp->t0;
    for (;;) {
        fmt_ns(ts, sizeof ts, t);
        wput(&w, "%s", ts);
        for (int c = 0; c < cp->nch; c++) wput(&w, ",%d", lv[c]);
        wput(&w, "\n");
        last = t;
        t = merge_next(cp, idx);
        if (t == INT64_MAX) break;
        for (int c = 0; c < cp->nch; c++)
            if (idx[c] < cp->ch[c].n && cp->ch[c].t[idx[c]] == t) { idx[c]++; lv[c] ^= 1; }
    }
    if (cp->t_end > last) {                                            /* 끝 표시 행 */
        fmt_ns(ts, sizeof ts, cp->t_end);
        wput(&w, "%s", ts);
        for (int c = 0; c < cp->nch; c++) wput(&w, ",%d", lv[c]);
        wput(&w, "\n");
    }
    return w.over ? LA_E_CAP : (int)w.n;
}
int la_write_vcd(const la_capture *cp, char *buf, size_t cap) {
    wbuf w = { buf, cap, 0, 0 };
    uint32_t idx[LA_MAX_CH] = { 0 };
    bool ns = cp->t0 % PS_PER_NS == 0 && cp->t_end % PS_PER_NS == 0;
    for (int c = 0; c < cp->nch; c++)
        for (uint32_t i = 0; i < cp->ch[c].n; i++) if (cp->ch[c].t[i] % PS_PER_NS) ns = false;
    int64_t u = ns ? PS_PER_NS : 1;
    wput(&w, "$version COMM-A10 la_write_vcd $end\n$timescale 1 %s $end\n$scope module la $end\n", ns ? "ns" : "ps");
    for (int c = 0; c < cp->nch; c++) wput(&w, "$var wire 1 %c %s $end\n", '!' + c, cp->ch[c].name);
    wput(&w, "$upscope $end\n$enddefinitions $end\n#%" PRId64 "\n$dumpvars\n", cp->t0 / u);
    for (int c = 0; c < cp->nch; c++) wput(&w, "%d%c\n", cp->ch[c].init, '!' + c);
    wput(&w, "$end\n");
    int64_t t, last = cp->t0;
    while ((t = merge_next(cp, idx)) != INT64_MAX) {
        wput(&w, "#%" PRId64 "\n", t / u);
        for (int c = 0; c < cp->nch; c++)
            if (idx[c] < cp->ch[c].n && cp->ch[c].t[idx[c]] == t) {
                idx[c]++;
                wput(&w, "%d%c\n", cp->ch[c].init ^ (int)(idx[c] & 1u), '!' + c);
            }
        last = t;
    }
    if (cp->t_end > last) wput(&w, "#%" PRId64 "\n", cp->t_end / u);
    return w.over ? LA_E_CAP : (int)w.n;
}

/* ---- TODO B1: 글리치 필터 — min_ps 보다 짧은 펄스(토글 2개)를 제거. 반환 = 지운 펄스 수 ---- */
int la_deglitch(la_chan *c, int64_t min_ps) {
    uint32_t out = 0;
    int removed = 0;
    for (uint32_t i = 0; i < c->n; i++) {
        if (out > 0 && c->t[i] - c->t[out - 1] < min_ps) { out--; removed++; }  /* 직전 토글과 상쇄 */
        else c->t[out++] = c->t[i];
    }
    c->n = out;
    return removed;
}

/* ------------------------------------------------------------------ UART */
typedef struct { uint8_t data_bits; char parity; uint8_t stop_bits; } uart_fmt;     /* 'N' 'E' 'O' */
typedef struct {
    int64_t  bit_ps;                /* 추정 비트 시간 */
    uint32_t baud, std_baud;        /* 추정 baud, 가장 가까운 표준 baud */
    double   std_err_pct;           /* (추정 − 표준) / 표준 × 100 */
    int      used, total;           /* 정수배에 맞은 펄스 / 전체 펄스 */
} uart_est;

static const uint32_t k_std_baud[] = { 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400,
                                       460800, 921600, 1000000, 2000000, 3000000 };

static int uart_nbits(uart_fmt f) { return 1 + f.data_bits + (f.parity != 'N') + f.stop_bits; }
static int parity_of(uint32_t v, int nb, char par) {
    int ones = 0;
    for (int i = 0; i < nb; i++) ones += (int)((v >> i) & 1u);
    return par == 'E' ? (ones & 1) : !(ones & 1);
}

/* ---- TODO A3: auto-baud — 펄스 폭 = 비트 시간의 정수배. 최소 폭 군집 → low 펄스로 정수배 최소제곱 2회 ---- */
int uart_autobaud(const la_chan *c, uart_est *est) {
    memset(est, 0, sizeof *est);
    if (c->n < 3) return LA_E_NOEDGE;                     /* 완전한 펄스 2개 이상 */
    int64_t wmin = INT64_MAX;
    for (uint32_t i = 0; i + 1 < c->n; i++) {
        int64_t w = c->t[i + 1] - c->t[i];
        if (w < wmin) wmin = w;
    }
    int64_t sum = 0, cnt = 0;                             /* 1단계: 최소 폭의 1.5배 안 = "1비트" 군집 */
    for (uint32_t i = 0; i + 1 < c->n; i++) {
        int64_t w = c->t[i + 1] - c->t[i];
        if (2 * w < 3 * wmin) { sum += w; cnt++; }
    }
    int64_t T = sum / cnt;
    est->total = (int)(c->n - 1);
    /* 2단계: low 펄스(= 한 프레임 안, idle 이 섞이지 않음)만으로 w ≈ k·T 맞추기.
     * high 펄스는 stop + 프레임 사이 idle 이 붙어 정수배가 아닐 수 있다. low 가 하나도 안 맞으면 전부 사용. */
    for (int it = 0; it < 2 && T > 0; it++) {
        int64_t sw = 0, sk = 0;
        int used = 0;
        for (int pass = 0; pass < 2 && sk == 0; pass++) {
            for (uint32_t i = 0; i + 1 < c->n; i++) {
                int low = (c->init ^ (int)((i + 1) & 1u)) == 0;
                if (pass == 0 && !low) continue;
                int64_t w = c->t[i + 1] - c->t[i];
                int64_t k = (w + T / 2) / T;
                if (k < 1 || k > 11) continue;            /* 긴 idle / break 는 빼기 */
                int64_t r = w - k * T;
                if (r < 0) r = -r;
                if (4 * r > T) continue;                  /* 정수배에서 T/4 넘게 벗어난 폭 */
                sw += w; sk += k; used++;
            }
        }
        if (sk == 0) break;
        T = (sw + sk / 2) / sk;
        est->used = used;
    }
    if (T <= 0) return LA_E_NOEDGE;
    est->bit_ps = T;
    est->baud = (uint32_t)((PS_PER_S + T / 2) / T);
    double best = 1e9;
    for (size_t i = 0; i < sizeof k_std_baud / sizeof k_std_baud[0]; i++) {
        double d = ((double)est->baud - k_std_baud[i]) / k_std_baud[i] * 100.0;
        if ((d < 0 ? -d : d) < (best < 0 ? -best : best)) { best = d; est->std_baud = k_std_baud[i]; }
    }
    est->std_err_pct = best;
    return LA_OK;
}

/* ---- TODO A4: UART 디코드 — 하강 에지마다 비트 중앙(t0 + (i+½)T)에서 샘플, LSB first ---- */
int uart_decode(const la_chan *c, int64_t t_end, int64_t T, uart_fmt f, la_event *ev, int cap) {
    la_sink s = { ev, cap, 0, 0 };
    if (T <= 0 || f.data_bits < 5 || f.data_bits > 9 || f.stop_bits < 1 || f.stop_bits > 2) return LA_E_ARG;
    int nb = uart_nbits(f), np = f.parity != 'N';
    uint32_t i = 0;
    while (i < c->n) {
        if ((c->init ^ (int)((i + 1) & 1u)) != 0) { i++; continue; }       /* 하강 에지만 */
        int64_t t0 = c->t[i];
        int64_t last = t0 + (2 * (int64_t)(nb - 1) + 1) * T / 2;           /* 마지막 stop 비트 중앙 */
        if (last > t_end) { emit(&s, t0, t_end, EV_TRUNC, 0, 0, 0, 0); break; }
        if (la_level_at(c, t0 + T / 2) != 0) { i++; continue; }            /* start 중앙이 1 = 가짜 시작 */
        uint32_t v = 0;
        int all0 = 1, flags = 0;
        for (int b = 0; b < f.data_bits; b++) {
            int lv = la_level_at(c, t0 + (2 * (int64_t)(1 + b) + 1) * T / 2);
            v |= (uint32_t)lv << b;
            all0 &= !lv;
        }
        if (np) {
            int pb = la_level_at(c, t0 + (2 * (int64_t)(1 + f.data_bits) + 1) * T / 2);
            all0 &= !pb;
            if (pb != parity_of(v, f.data_bits, f.parity)) flags |= F_PERR;
        }
        for (int k = 0; k < f.stop_bits; k++) {
            int sb = la_level_at(c, t0 + (2 * (int64_t)(1 + f.data_bits + np + k) + 1) * T / 2);
            if (!sb) flags |= F_FERR;
            all0 &= !sb;
        }
        if (all0) flags = F_BREAK | F_FERR;                                  /* 프레임 전체가 0 = break */
        emit(&s, t0, t0 + nb * T, EV_UART, flags, 0, v, 0);
        i = la_edge_upper(c, last);                                         /* 마지막 샘플 뒤 첫 에지부터 */
    }
    return sink_result(&s);
}

/* ------------------------------------------------------------------ SPI */
typedef struct { uint8_t mode, bits, lsb; } spi_fmt;                  /* mode = CPOL<<1 | CPHA */

/* ---- TODO A5: SPI 디코드 — CS low 구간마다 샘플 에지(CPOL==CPHA → 상승)에서 MOSI/MISO ---- */
int spi_decode(const la_capture *cp, int cs, int sck, int mosi, int miso, spi_fmt f, la_event *ev, int cap) {
    la_sink s = { ev, cap, 0, 0 };
    if (cs < 0 || cs >= cp->nch || sck < 0 || sck >= cp->nch || mosi < 0 || mosi >= cp->nch ||
        miso >= cp->nch || f.mode > 3 || f.bits < 1 || f.bits > 32) return LA_E_ARG;
    const la_chan *C = &cp->ch[cs], *K = &cp->ch[sck], *MO = &cp->ch[mosi];
    const la_chan *MI = miso >= 0 ? &cp->ch[miso] : NULL;
    int cpol = f.mode >> 1, cpha = f.mode & 1;
    int sample_rise = cpol == cpha;
    if (C->init == 0) emit(&s, cp->t0, C->n ? C->t[0] : cp->t_end, EV_TRUNC, 0, 1, 0, 0);  /* 전송 도중 시작 */
    for (uint32_t k = 0; k < C->n; k++) {
        if ((C->init ^ (int)((k + 1) & 1u)) != 0) continue;                /* CS 하강만 */
        int64_t ta = C->t[k];
        bool trunc = k + 1 >= C->n;
        int64_t tb = trunc ? cp->t_end + 1 : C->t[k + 1];
        int flags = la_level_at(K, ta) != cpol ? F_CPOL : 0;                 /* idle 레벨이 CPOL 과 다름 */
        uint32_t mo = 0, mi = 0;
        int bits = 0;
        int64_t wt0 = ta;
        for (uint32_t j = la_edge_upper(K, ta); j < K->n && K->t[j] < tb; j++) {
            int rising = (K->init ^ (int)((j + 1) & 1u)) == 1;
            if (rising != sample_rise) continue;
            int64_t t = K->t[j];
            uint32_t bo = (uint32_t)la_level_at(MO, t), bi = MI ? (uint32_t)la_level_at(MI, t) : 0;
            if (bits == 0) wt0 = t;
            if (f.lsb) { mo |= bo << bits; mi |= bi << bits; }
            else { mo = mo << 1 | bo; mi = mi << 1 | bi; }
            if (++bits == f.bits) {
                emit(&s, wt0, t, EV_SPI, flags, bits, mo, mi);
                mo = mi = 0;
                bits = 0;
            }
        }
        if (trunc) emit(&s, ta, cp->t_end, EV_TRUNC, 0, bits, mo, mi);
        else if (bits) emit(&s, wt0, tb, EV_SPI_PARTIAL, flags, bits, mo, mi);
    }
    return sink_result(&s);
}

/* ------------------------------------------------------------------ I2C */
/* ---- TODO A6: I2C 디코드 — 두 채널 에지를 시간순 병합. 같은 시각: SCL 하강 → SDA → SCL 상승 ---- */
int i2c_decode(const la_chan *sda, const la_chan *scl, int64_t t_end, la_event *ev, int cap) {
    la_sink s = { ev, cap, 0, 0 };
    uint32_t i = 0, j = 0;
    int d = sda->init, c = scl->init, in_tx = 0, bits = 0, idx = 0, hi_sampled = 0;
    uint32_t byte = 0;
    int64_t tb = 0;
    while (i < sda->n || j < scl->n) {
        int64_t ta = i < sda->n ? sda->t[i] : INT64_MAX;
        int64_t tc = j < scl->n ? scl->t[j] : INT64_MAX;
        bool scl_first = tc < ta || (tc == ta && c == 1);                 /* 같은 시각이면 SCL 하강 먼저 */
        if (scl_first) {
            c ^= 1;
            j++;
            hi_sampled = 0;
            if (c == 1 && in_tx) {                                        /* SCL 상승 = 샘플 */
                hi_sampled = 1;
                if (bits < 8) {
                    if (bits == 0) tb = tc;
                    byte = byte << 1 | (uint32_t)d;
                    bits++;
                } else {                                                  /* 9번째 클럭 = ACK/NACK */
                    int fl = d ? F_NACK : 0;
                    if (idx == 0) emit(&s, tb, tc, EV_I2C_ADDR, fl, (int)(byte & 1u), byte >> 1, 0);
                    else emit(&s, tb, tc, EV_I2C_DATA, fl, 0, byte, 0);
                    idx++;
                    bits = 0;
                    byte = 0;
                }
            }
        } else {
            d ^= 1;
            i++;
            if (c == 1) {                                                 /* SCL high 중 SDA 변화 */
                /* 이번 SCL high 에서 방금 샘플한 비트는 데이터가 아니라 Sr/STOP 조건의 클럭이었다 → 취소 */
                int eb = bits - hi_sampled;
                if (in_tx && eb > 0) emit(&s, ta, ta, EV_I2C_MIDBYTE, 0, eb, hi_sampled ? byte >> 1 : byte, 0);
                hi_sampled = 0;
                if (d == 0) {
                    emit(&s, ta, ta, in_tx ? EV_I2C_RSTART : EV_I2C_START, 0, 0, 0, 0);
                    in_tx = 1;
                } else if (in_tx) {
                    emit(&s, ta, ta, EV_I2C_STOP, 0, 0, 0, 0);
                    in_tx = 0;
                }
                bits = 0;
                idx = 0;
                byte = 0;
            }
        }
    }
    if (in_tx) {
        int64_t tl = 0;
        if (sda->n) tl = sda->t[sda->n - 1];
        if (scl->n && scl->t[scl->n - 1] > tl) tl = scl->t[scl->n - 1];
        emit(&s, tl, t_end, EV_TRUNC, 0, bits, byte, 0);
    }
    return sink_result(&s);
}

/* ---- TODO B2: 역할 추정 — SCL 은 비트마다 토글 2번, SDA 는 많아야 1번(+START/STOP) ---- */
int i2c_guess_roles(const la_capture *cp, int a, int b, int *sda, int *scl) {
    if (a < 0 || b < 0 || a >= cp->nch || b >= cp->nch || a == b) return LA_E_ARG;
    uint32_t na = cp->ch[a].n, nb = cp->ch[b].n;
    *sda = a;
    *scl = b;
    if (na + nb == 0) return LA_E_NOEDGE;
    if (na == nb) return 1;                               /* 판단 불가 */
    if (na > nb) { *scl = a; *sda = b; }
    return 0;
}

/* ------------------------------------------------------------------ LA protocol table */
static const char *ev_name(int t) {
    switch (t) {
    case EV_UART: return "UART";      case EV_SPI: return "SPI";        case EV_SPI_PARTIAL: return "SPI-PART";
    case EV_I2C_START: return "START"; case EV_I2C_RSTART: return "Sr";  case EV_I2C_ADDR: return "ADDR";
    case EV_I2C_DATA: return "DATA";   case EV_I2C_STOP: return "STOP";  case EV_I2C_MIDBYTE: return "ERR";
    case EV_TRUNC: return "TRUNC";     default: return "?";
    }
}
/* snprintf 규칙: 반환 = 필요한 길이(잘려도), buf 는 항상 NUL 종료 */
int la_format_table(const la_event *ev, int n, char *buf, size_t cap) {
    wbuf w = { buf, cap, 0, 0 };
    if (cap) buf[0] = 0;
    for (int i = 0; i < n; i++) {
        const la_event *e = &ev[i];
        int64_t ns = e->t0 / PS_PER_NS;
        wput(&w, "%4d %8" PRId64 ".%03" PRId64 " us  %-8s ", i, ns / 1000, ns % 1000, ev_name(e->type));
        int hx = (e->aux + 3) / 4;
        switch (e->type) {
        case EV_UART:
            wput(&w, "0x%02" PRIX32 " '%c'%s%s%s", e->val, (e->val >= 0x20 && e->val < 0x7F) ? (char)e->val : '.',
                 (e->flags & F_PERR) ? " PERR" : "", (e->flags & F_FERR) && !(e->flags & F_BREAK) ? " FERR" : "",
                 (e->flags & F_BREAK) ? " BREAK" : "");
            break;
        case EV_SPI: case EV_SPI_PARTIAL:
            wput(&w, "MOSI 0x%0*" PRIX32 " MISO 0x%0*" PRIX32, hx, e->val, hx, e->val2);
            if (e->type == EV_SPI_PARTIAL) wput(&w, " (%d bits)", e->aux);
            if (e->flags & F_CPOL) wput(&w, " CPOL?");
            break;
        case EV_I2C_ADDR:
            wput(&w, "0x%02" PRIX32 " %c %s", e->val, e->aux ? 'R' : 'W', (e->flags & F_NACK) ? "NACK" : "ACK");
            break;
        case EV_I2C_DATA:
            wput(&w, "0x%02" PRIX32 " %s", e->val, (e->flags & F_NACK) ? "NACK" : "ACK");
            break;
        case EV_I2C_MIDBYTE:
            wput(&w, "SDA changed while SCL high after %d bits", e->aux);
            break;
        case EV_TRUNC:
            wput(&w, "capture ends inside a frame (%d bits pending)", e->aux);
            break;
        default:
            break;
        }
        wput(&w, "\n");
    }
    return (int)w.n;
}

/* ================================================================== 하네스 (주어짐) */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 1;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }
static int64_t rnd_range(int64_t lo, int64_t hi) {                       /* [lo, hi] */
    uint64_t r = ((uint64_t)xs32() << 32) | xs32();
    return lo + (int64_t)(r % (uint64_t)(hi - lo + 1));
}

/* ---- 이상 파형 생성기 (시간 = ps, 지터 ±jit 은 토글에만) ---- */
static void wv_set(la_chan *c, int64_t t, int v, int64_t jit) {
    if (v == chan_now(c)) return;
    if (jit) t += rnd_range(-jit, jit);
    if (c->n && t <= c->t[c->n - 1]) t = c->t[c->n - 1] + 1;
    (void)la_set(c, t, v);
}
static void cap_init(la_capture *cp, int nch, const char *const *names, const int *init) {
    memset(cp, 0, sizeof *cp);
    cp->nch = nch;
    for (int i = 0; i < nch; i++) {
        snprintf(cp->ch[i].name, LA_NAME_MAX, "%s", names[i]);
        cp->ch[i].init = (uint8_t)init[i];
    }
}
/* LA 샘플링: 샘플 k (시각 k·ts) 는 그 시각 이하의 토글을 본다 → 토글은 ceil(t/ts)·ts 로 밀린다.
 * 한 샘플 간격 안의 두 토글은 상쇄(펄스 소실). t_end 는 샘플 격자로 내림, 그 뒤 토글은 버림. */
static void la_sample(const la_capture *in, int64_t ts, la_capture *out) {
    memset(out, 0, sizeof *out);
    out->nch = in->nch;
    out->t0 = 0;
    out->t_end = in->t_end / ts * ts;
    for (int c = 0; c < in->nch; c++) {
        la_chan *o = &out->ch[c];
        memcpy(o->name, in->ch[c].name, LA_NAME_MAX);
        o->init = in->ch[c].init;
        for (uint32_t i = 0; i < in->ch[c].n; i++) {
            int64_t t = in->ch[c].t[i];
            int64_t q = (t + ts - 1) / ts * ts;
            if (q > out->t_end) break;
            if (q == 0) { o->init ^= 1; continue; }
            if (o->n && o->t[o->n - 1] == q) o->n--;
            else o->t[o->n++] = q;
        }
    }
}

enum { UF_PFLIP = 1, UF_STOP0 = 2, UF_BREAK = 4 };
/* COMM-B01 모델 축약판: 프레임 한 개를 t 부터 그린다. 반환 = 프레임(+복귀 idle) 끝 */
static int64_t gen_uart_frame(la_chan *c, int64_t t, int64_t T, uart_fmt f, uint32_t v, int fault, int64_t jit) {
    int bits[16], k = 0;
    bits[k++] = 0;
    for (int b = 0; b < f.data_bits; b++) bits[k++] = (int)((v >> b) & 1u);
    if (f.parity != 'N') bits[k++] = parity_of(v, f.data_bits, f.parity) ^ ((fault & UF_PFLIP) ? 1 : 0);
    for (int s = 0; s < f.stop_bits; s++) bits[k++] = (fault & UF_STOP0) ? 0 : 1;
    if (fault & UF_BREAK) for (int i = 0; i < k; i++) bits[i] = 0;
    for (int i = 0; i < k; i++) wv_set(c, t + i * T, bits[i], jit);
    int64_t end = t + k * T;
    if (!bits[k - 1]) { wv_set(c, end, 1, jit); end += T; }   /* 다음 start 하강이 보이도록 복귀 */
    return end;
}
static la_event ev_uart(uint32_t v, int flags) { la_event e = { 0, 0, EV_UART, (uint8_t)flags, 0, v, 0 }; return e; }

/* COMM-B04 모델 축약판: CS↓ → nw 워드 (+extra 비트) → CS↑ (cs_rise=false 면 안 올림). 반환 = 끝 */
static int64_t gen_spi_xfer(la_capture *cp, const int *chs, int64_t t, int64_t H, spi_fmt f,
                            const uint32_t *mo, const uint32_t *mi, int nw, int extra, bool cs_rise, int64_t jit) {
    la_chan *C = &cp->ch[chs[0]], *K = &cp->ch[chs[1]], *MO = &cp->ch[chs[2]];
    la_chan *MI = chs[3] >= 0 ? &cp->ch[chs[3]] : NULL;
    int cpol = f.mode >> 1, cpha = f.mode & 1;
    int64_t dly = H / 8;                                          /* 데이터 출력 지연 */
    wv_set(C, t, 0, jit);
    int64_t e = t + H;                                            /* 첫(leading) 에지 */
    int total = nw * f.bits + extra;
    for (int i = 0; i < total; i++) {
        int w = i / f.bits, bi = i % f.bits;
        int sh = f.lsb ? bi : f.bits - 1 - bi;
        int bo = (int)((mo[w] >> sh) & 1u), bm = MI ? (int)((mi[w] >> sh) & 1u) : 0;
        int64_t td = cpha == 0 ? (i == 0 ? t + dly : e - H + dly) : e + dly;
        wv_set(MO, td, bo, 0);
        if (MI) wv_set(MI, td, bm, 0);
        wv_set(K, e, !cpol, jit);
        wv_set(K, e + H, cpol, jit);
        e += 2 * H;
    }
    if (cs_rise) wv_set(C, e, 1, jit);
    return e + H;
}

/* COMM-B06 모델 축약판: 트랜잭션 스크립트 → SDA/SCL 파형 + 기대 이벤트 */
typedef struct { la_chan *sda, *scl; int64_t now, H, d, jit; la_event *exp; int nexp, in_tx; } i2c_wr;
static void i2c_expect(i2c_wr *w, int type, int flags, int aux, uint32_t val) {
    la_event e = { 0, 0, (uint8_t)type, (uint8_t)flags, (uint16_t)aux, val, 0 };
    w->exp[w->nexp++] = e;
}
static void i2c_g_start(i2c_wr *w) {
    if (w->in_tx) {                                               /* repeated START */
        wv_set(w->sda, w->now + w->d, 1, w->jit);
        wv_set(w->scl, w->now + w->H, 1, w->jit);
        wv_set(w->sda, w->now + 3 * w->H / 2, 0, w->jit);
        wv_set(w->scl, w->now + 2 * w->H, 0, w->jit);
        w->now += 2 * w->H;
        i2c_expect(w, EV_I2C_RSTART, 0, 0, 0);
    } else {
        wv_set(w->sda, w->now, 0, w->jit);
        wv_set(w->scl, w->now + w->H / 2, 0, w->jit);
        w->now += w->H / 2;
        i2c_expect(w, EV_I2C_START, 0, 0, 0);
    }
    w->in_tx = 1;
}
static void i2c_g_bit(i2c_wr *w, int v, int bad) {                /* bad: SCL high 중 SDA 를 뒤집는 버그 */
    wv_set(w->sda, w->now + w->d, v, w->jit);
    wv_set(w->scl, w->now + w->H, 1, w->jit);
    if (bad) wv_set(w->sda, w->now + 3 * w->H / 2, !v, w->jit);
    wv_set(w->scl, w->now + 2 * w->H, 0, w->jit);
    w->now += 2 * w->H;
}
/* 바이트 + 9번째 클럭. first = (Sr)START 직후 주소 바이트. nack = 수신 측이 SDA 를 놓음 */
static void i2c_g_byte(i2c_wr *w, uint8_t b, int nack, bool first) {
    for (int i = 0; i < 8; i++) i2c_g_bit(w, (b >> (7 - i)) & 1, 0);
    i2c_g_bit(w, nack, 0);
    if (first) i2c_expect(w, EV_I2C_ADDR, nack ? F_NACK : 0, b & 1, b >> 1);
    else i2c_expect(w, EV_I2C_DATA, nack ? F_NACK : 0, 0, b);
}
static void i2c_g_stop(i2c_wr *w) {
    wv_set(w->sda, w->now + w->d, 0, w->jit);
    wv_set(w->scl, w->now + w->H, 1, w->jit);
    wv_set(w->sda, w->now + 3 * w->H / 2, 1, w->jit);
    w->now += 3 * w->H / 2 + 2 * w->H;                            /* + bus free */
    w->in_tx = 0;
    i2c_expect(w, EV_I2C_STOP, 0, 0, 0);
}
/* TMP117 류 레지스터 읽기: S addr+W reg Sr addr+R d0 ACK d1 NACK P */
static void i2c_g_regread(i2c_wr *w, uint8_t addr, uint8_t reg, const uint8_t *d, int n) {
    i2c_g_start(w);
    i2c_g_byte(w, (uint8_t)(addr << 1), 0, true);
    i2c_g_byte(w, reg, 0, false);
    i2c_g_start(w);
    i2c_g_byte(w, (uint8_t)(addr << 1 | 1), 0, true);
    for (int i = 0; i < n; i++) i2c_g_byte(w, d[i], i == n - 1, false);
    i2c_g_stop(w);
}

static bool ev_same(const la_event *a, const la_event *b) {
    return a->type == b->type && a->flags == b->flags && a->aux == b->aux && a->val == b->val && a->val2 == b->val2;
}
static bool ev_list_same(const la_event *a, int na, const la_event *b, int nb) {
    if (na != nb) return false;
    for (int i = 0; i < na; i++) if (!ev_same(&a[i], &b[i])) return false;
    return true;
}

static la_capture G_IDEAL, G_CAP, G_RD;
static la_event   G_EV[8192], G_EXP[8192];
static char       G_TXT[1 << 21];

/* ================================================================== 테스트 */
static const char *const NM_UART[1] = { "TX" };
static const int INIT_HI[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };

/* UART 캡처 하나: 바이트 n 개, 결과를 G_CAP 에 (샘플링 후), 기대 이벤트를 G_EXP 에 */
static int make_uart(int64_t T, uart_fmt f, const uint32_t *v, const int *fault, int n, int64_t ts,
                     int64_t gap_max, int64_t jit, int64_t tail) {
    cap_init(&G_IDEAL, 1, NM_UART, INIT_HI);
    int64_t t = rnd_range(3 * T, 4 * T);                          /* 무작위 위상 */
    for (int i = 0; i < n; i++) {
        int fl = fault ? fault[i] : 0;
        t = gen_uart_frame(&G_IDEAL.ch[0], t, T, f, v[i], fl, jit);
        t += gap_max ? rnd_range(0, gap_max) : 0;
        int ef = (fl & UF_BREAK) ? (F_BREAK | F_FERR) : ((fl & UF_PFLIP) ? F_PERR : 0) | ((fl & UF_STOP0) ? F_FERR : 0);
        G_EXP[i] = ev_uart((fl & UF_BREAK) ? 0 : v[i] & ((1u << f.data_bits) - 1), ef);
    }
    G_IDEAL.t_end = t + tail;
    la_sample(&G_IDEAL, ts, &G_CAP);
    return n;
}

#ifndef LA_NO_MAIN   /* practice.c / demo_autobaud.c 는 여기부터(CLI · 테스트) 빼고 include */
static bool chan_same(const la_chan *a, const la_chan *b) {
    if (strcmp(a->name, b->name) || a->init != b->init || a->n != b->n) return false;
    return a->n == 0 || memcmp(a->t, b->t, a->n * sizeof a->t[0]) == 0;
}
static bool cap_same(const la_capture *a, const la_capture *b) {
    if (a->nch != b->nch || a->t0 != b->t0 || a->t_end != b->t_end) return false;
    for (int i = 0; i < a->nch; i++) if (!chan_same(&a->ch[i], &b->ch[i])) return false;
    return true;
}
/* ------------------------------------------------------------------ 데모 캡처 + CLI (주어짐) */
static void build_demo(la_capture *out) {
    static const char *const nm[3] = { "TX", "SCL", "SDA" };
    static const int in[3] = { 1, 1, 1 };
    static la_event ex[64];
    cap_init(&G_IDEAL, 3, nm, in);
    g_rng = 0xA10DE40u;
    uart_fmt f = { 8, 'N', 1 };
    int64_t T = (int64_t)(PS_PER_S / (115200.0 * 1.02));          /* TX 클럭 +2% 빠름 */
    int64_t t = 20 * PS_PER_NS * 1000;
    const char *msg = "OK\r\n";
    for (const char *p = msg; *p; p++) t = gen_uart_frame(&G_IDEAL.ch[0], t, T, f, (uint8_t)*p, 0, 30 * PS_PER_NS);
    wv_set(&G_IDEAL.ch[0], t + 50 * PS_PER_NS * 1000, 0, 0);       /* idle 중 40 ns 글리치 하나 */
    wv_set(&G_IDEAL.ch[0], t + 50 * PS_PER_NS * 1000 + 40 * PS_PER_NS, 1, 0);
    i2c_wr w = { &G_IDEAL.ch[2], &G_IDEAL.ch[1], t + 200 * PS_PER_NS * 1000, 1250 * PS_PER_NS, 625 * PS_PER_NS,
                 20 * PS_PER_NS, ex, 0, 0 };
    const uint8_t temp[2] = { 0x0C, 0x80 };                        /* TMP117 raw 0x0C80 = 25.000 °C */
    i2c_g_regread(&w, 0x48, 0x00, temp, 2);
    G_IDEAL.t_end = w.now + 20 * PS_PER_NS * 1000;
    la_sample(&G_IDEAL, 40 * PS_PER_NS, out);                      /* 25 MS/s */
}

static const char *opt_get(const char *(*opt)[2], int no, const char *name, const char *def) {
    for (int q = 0; q < no; q++) if (!strcmp(opt[q][0], name)) def = opt[q][1];
    return def;
}
static int cli(int argc, char **argv) {
    static la_capture cp;
    static char file[1 << 20];
    if (strcmp(argv[1], "--emit-demo") == 0) {
        build_demo(&cp);
        int n = la_write_vcd(&cp, G_TXT, sizeof G_TXT);
        if (n < 0) return 2;
        fwrite(G_TXT, 1, (size_t)n, stdout);
        return 0;
    }
    if (argc < 3) { fprintf(stderr, "usage: see header comment\n"); return 2; }
    FILE *fp = fopen(argv[1], "rb");
    if (!fp) { perror(argv[1]); return 2; }
    size_t len = fread(file, 1, sizeof file - 1, fp);
    bool big = !feof(fp);
    fclose(fp);
    if (big) { fprintf(stderr, "file too large\n"); return 2; }
    size_t k = 0;
    while (k < len && is_sp(file[k])) k++;
    int rc = (k < len && file[k] == '$') ? la_parse_vcd(file, len, &cp) : la_parse_csv(file, len, &cp);
    if (rc) { fprintf(stderr, "parse error %d\n", rc); return 2; }
    const char *opt[16][2];
    int no = 0;
    for (int i = 3; i < argc && no < 16; i++) {
        char *eq = strchr(argv[i], '=');
        if (!eq) { fprintf(stderr, "bad option %s\n", argv[i]); return 2; }
        *eq = 0;
        opt[no][0] = argv[i];
        opt[no][1] = eq + 1;
        no++;
    }
#define OPT(name, def) opt_get(opt, no, name, def)
    const char *gl = OPT("glitch", NULL);
    if (gl) for (int c = 0; c < cp.nch; c++) la_deglitch(&cp.ch[c], atoll(gl) * PS_PER_NS);
    int n = -1;
    printf("# %s: %d channels, %" PRId64 " .. %" PRId64 " ns\n", argv[1], cp.nch, cp.t0 / PS_PER_NS, cp.t_end / PS_PER_NS);
    if (!strcmp(argv[2], "uart")) {
        int ch = la_find(&cp, OPT("ch", "TX"));
        const char *fm = OPT("fmt", "8N1"), *bd = OPT("baud", "auto");
        if (ch < 0 || strlen(fm) != 3) { fprintf(stderr, "bad ch/fmt\n"); return 2; }
        uart_fmt f = { (uint8_t)(fm[0] - '0'), fm[1], (uint8_t)(fm[2] - '0') };
        int64_t T;
        if (!strcmp(bd, "auto")) {
            uart_est e;
            if (uart_autobaud(&cp.ch[ch], &e)) { fprintf(stderr, "auto-baud failed\n"); return 1; }
            T = e.bit_ps;
            printf("# auto-baud: bit %" PRId64 " ps = %" PRIu32 " baud (nearest %" PRIu32 ", %+.2f%%), fit on %d of %d pulses\n",
                   e.bit_ps, e.baud, e.std_baud, e.std_err_pct, e.used, e.total);
        } else {
            T = (PS_PER_S + atoll(bd) / 2) / atoll(bd);
        }
        n = uart_decode(&cp.ch[ch], cp.t_end, T, f, G_EV, 8192);
    } else if (!strcmp(argv[2], "spi")) {
        int cs = la_find(&cp, OPT("cs", "CS_N")), sck = la_find(&cp, OPT("sck", "SCLK"));
        int mo = la_find(&cp, OPT("mosi", "MOSI")), mi = la_find(&cp, OPT("miso", "MISO"));
        spi_fmt f = { (uint8_t)atoi(OPT("mode", "0")), (uint8_t)atoi(OPT("bits", "8")), (uint8_t)atoi(OPT("lsb", "0")) };
        n = spi_decode(&cp, cs, sck, mo, mi, f, G_EV, 8192);
    } else if (!strcmp(argv[2], "i2c")) {
        const char *sn = OPT("sda", NULL), *cn = OPT("scl", NULL);
        int sda, scl;
        if (sn && cn) { sda = la_find(&cp, sn); scl = la_find(&cp, cn); }
        else {
            int a = la_find(&cp, "SDA"), b = la_find(&cp, "SCL");
            if (a < 0 || b < 0) { a = 0; b = 1; }
            int g = i2c_guess_roles(&cp, a, b, &sda, &scl);
            printf("# i2c roles: SDA=%s SCL=%s (%s)\n", sda >= 0 ? cp.ch[sda].name : "?", scl >= 0 ? cp.ch[scl].name : "?",
                   g == 0 ? "by edge count" : "ambiguous");
        }
        if (sda < 0 || scl < 0) { fprintf(stderr, "bad sda/scl\n"); return 2; }
        n = i2c_decode(&cp.ch[sda], &cp.ch[scl], cp.t_end, G_EV, 8192);
    } else {
        fprintf(stderr, "unknown protocol %s\n", argv[2]);
        return 2;
    }
#undef OPT
    if (n < 0) { fprintf(stderr, "decode error %d\n", n); return 1; }
    la_format_table(G_EV, n, G_TXT, sizeof G_TXT);
    fputs(G_TXT, stdout);
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1) return cli(argc, argv);
    char tbl[4096];

    /* ================= Part A — 파서 · 레벨 조회 · 세 디코더 ================= */
    {   /* A-1 CSV */
        const char *csv = "time_ns,TX,SCL\n0,1,1\n100,0,1\n250.5,0,0\n400,1,0\n1000,1,0\n";
        CHECK(la_parse_csv(csv, strlen(csv), &G_RD) == LA_OK);
        CHECK(G_RD.nch == 2 && !strcmp(G_RD.ch[0].name, "TX") && !strcmp(G_RD.ch[1].name, "SCL"));
        CHECK(G_RD.ch[0].init == 1 && G_RD.ch[0].n == 2 && G_RD.ch[0].t[0] == 100000 && G_RD.ch[0].t[1] == 400000);
        CHECK(G_RD.ch[1].n == 1 && G_RD.ch[1].t[0] == 250500 && G_RD.t0 == 0 && G_RD.t_end == 1000000);
        const char *bad[6] = { "t,TX\n0,1\n", "time_ns,TX,SCL\n0,1\n", "time_ns,TX\n0,2\n",
                               "time_ns,TX\n0,1\n50,0\n40,1\n", "time_ns,TX\n", "time_ns,A,B,C,D,E,F,G,H,I\n0,1,1,1,1,1,1,1,1,1\n" };
        const int want[6] = { LA_E_HDR, LA_E_COLS, LA_E_VAL, LA_E_TIME, LA_E_EMPTY, LA_E_CAP };
        int ok = 0;
        for (int i = 0; i < 6; i++) ok += la_parse_csv(bad[i], strlen(bad[i]), &G_RD) == want[i];
        CHECK(ok == 6);
    }
    {   /* A-2 VCD: 10 ns timescale, 여러 글자 id, $dumpvars, 무시할 섹션 */
        const char *vcd =
            "$date today $end\n$version x $end\n$timescale 10 ns $end\n$scope module top $end\n"
            "$var wire 1 !# SDA $end\n$var wire 1 % SCL $end\n$upscope $end\n$enddefinitions $end\n"
            "#0\n$dumpvars\n1!#\n1%\n$end\n#5\n0!#\n#10\n0%\n#12\n0%\n#30\n1%\n1!#\n#40\n";
        CHECK(la_parse_vcd(vcd, strlen(vcd), &G_RD) == LA_OK);
        CHECK(G_RD.nch == 2 && !strcmp(G_RD.ch[0].name, "SDA") && !strcmp(G_RD.ch[1].name, "SCL"));
        CHECK(G_RD.ch[0].n == 2 && G_RD.ch[0].t[0] == 50000 && G_RD.ch[0].t[1] == 300000);
        CHECK(G_RD.ch[1].n == 2 && G_RD.ch[1].t[0] == 100000 && G_RD.ch[1].t[1] == 300000 && G_RD.t_end == 400000);
        const char *hdr = "$timescale 1ns $end $var wire 1 ! A $end $enddefinitions $end\n";
        char v[256];
        const char *tail[7] = { "#0 1! #5 x!", "#0 1! #5 0@", "#5 1!", "#0 1! #9 0! #8 1!", "#0 1! #5 b101 !", "", "#0 1! #3 0! #3 1!" };
        int ok = 0;
        static const int want2[7] = { LA_E_XZ, LA_E_ID, LA_OK, LA_E_TIME, LA_E_VAR, LA_E_EMPTY, LA_E_TIME };
        for (int i = 0; i < 7; i++) {
            snprintf(v, sizeof v, "%s%s\n", hdr, tail[i]);
            ok += la_parse_vcd(v, strlen(v), &G_RD) == want2[i];
        }
        CHECK(ok == 7);
        const char *b1 = "$timescale 3 ns $end $var wire 1 ! A $end $enddefinitions $end #0 1!";
        const char *b2 = "$timescale 1 ns $end $var wire 8 ! A $end $enddefinitions $end #0 1!";
        const char *b3 = "$timescale 1 ns $end $var wire 1 ! A $end $var wire 1 \" B $end $enddefinitions $end #0 1! #4 0\"";
        CHECK(la_parse_vcd(b1, strlen(b1), &G_RD) == LA_E_TSCALE && la_parse_vcd(b2, strlen(b2), &G_RD) == LA_E_VAR &&
              la_parse_vcd(b3, strlen(b3), &G_RD) == LA_E_INIT);
    }
    {   /* A-3 레벨 조회: 무작위 채널, 에지 시각 정확히 + 사이 시각 전부를 선형 기준과 비교 */
        g_rng = 0xA1003u;
        static la_chan c;
        memset(&c, 0, sizeof c);
        c.init = 1;
        int64_t t = 0;
        for (int i = 0; i < 300; i++) { t += rnd_range(1, 5); c.t[c.n++] = t; }
        int bad = 0;
        for (int64_t q = -1; q <= t + 1; q++) {
            uint32_t cnt = 0;
            while (cnt < c.n && c.t[cnt] <= q) cnt++;
            bad += la_edge_upper(&c, q) != cnt || la_level_at(&c, q) != (int)(c.init ^ (cnt & 1u));
        }
        CHECK(bad == 0);
    }
    {   /* A-4 쓰기 → 읽기 왕복: 무작위 캡처 300개를 CSV 와 VCD 로 */
        g_rng = 0xA1004u;
        int okc = 0, okv = 0;
        for (int it = 0; it < 300; it++) {
            static const char *const nm[8] = { "D0", "D1", "D2", "D3", "SCL", "SDA", "CS_N", "TX" };
            int nch = (int)rnd_range(1, 8), init[8];
            for (int c = 0; c < nch; c++) init[c] = (int)rnd_range(0, 1);
            cap_init(&G_IDEAL, nch, nm, init);
            G_IDEAL.t0 = rnd_range(0, 1000) * (it % 3 ? PS_PER_NS : 1);
            int64_t tmax = G_IDEAL.t0;
            for (int c = 0; c < nch; c++) {
                int64_t tt = G_IDEAL.t0;
                int ne = (int)rnd_range(0, 60);
                for (int k = 0; k < ne; k++) {
                    tt += rnd_range(1, 40) * (it % 3 ? PS_PER_NS : 1);  /* 1/3 은 ps 해상도 */
                    G_IDEAL.ch[c].t[G_IDEAL.ch[c].n++] = tt;
                }
                if (tt > tmax) tmax = tt;
            }
            G_IDEAL.t_end = tmax + rnd_range(1, 50) * PS_PER_NS;
            int n = la_write_csv(&G_IDEAL, G_TXT, sizeof G_TXT);
            okc += n > 0 && la_parse_csv(G_TXT, (size_t)n, &G_RD) == LA_OK && cap_same(&G_IDEAL, &G_RD);
            n = la_write_vcd(&G_IDEAL, G_TXT, sizeof G_TXT);
            okv += n > 0 && la_parse_vcd(G_TXT, (size_t)n, &G_RD) == LA_OK && cap_same(&G_IDEAL, &G_RD);
        }
        CHECK(okc == 300);
        CHECK(okv == 300);
    }
    {   /* A-5 UART: 'A' 이상 파형 + "Hi" 115200 8N1 @10 MS/s → auto-baud + 디코드 */
        uart_fmt f = { 8, 'N', 1 };
        int64_t T = (PS_PER_S + 57600) / 115200;
        cap_init(&G_IDEAL, 1, NM_UART, INIT_HI);
        gen_uart_frame(&G_IDEAL.ch[0], 1000 * T, T, f, 0x41, 0, 0);     /* 0 1000 0010 1 */
        const la_chan *a = &G_IDEAL.ch[0];
        CHECK(a->n == 6 && a->t[0] == 1000 * T && a->t[1] == 1001 * T && a->t[2] == 1002 * T &&
              a->t[3] == 1007 * T && a->t[4] == 1008 * T && a->t[5] == 1009 * T);
        g_rng = 0xA1005u;
        const uint32_t hi[2] = { 'H', 'i' };
        make_uart(T, f, hi, NULL, 2, 100 * PS_PER_NS, 0, 0, 20 * T);
        uart_est e;
        CHECK(uart_autobaud(&G_CAP.ch[0], &e) == LA_OK && e.std_baud == 115200 &&
              llabs(e.bit_ps - T) * 1000 < T * 5);                              /* 0.5% 안 */
        int n = uart_decode(&G_CAP.ch[0], G_CAP.t_end, e.bit_ps, f, G_EV, 64);
        CHECK(ev_list_same(G_EV, n, G_EXP, 2));
    }
    {   /* A-6 SPI: 4 모드 × (8, 12, 16 비트, 8 비트 LSB first) × 30 트랜잭션 @ SCK 5 MHz, 100 MS/s */
        g_rng = 0xA1006u;
        static const char *const nm[4] = { "CS_N", "SCLK", "MOSI", "MISO" };
        const int chs[4] = { 0, 1, 2, 3 };
        int combos = 0, ok = 0;
        for (int mode = 0; mode < 4; mode++)
            for (int v = 0; v < 4; v++) {
                spi_fmt f = { (uint8_t)mode, (uint8_t)(v == 1 ? 12 : v == 2 ? 16 : 8), (uint8_t)(v == 3) };
                int init[4] = { 1, mode >> 1, 0, 0 };
                cap_init(&G_IDEAL, 4, nm, init);
                int64_t t = rnd_range(100, 200) * PS_PER_NS;
                int ne = 0;
                for (int x = 0; x < 30; x++) {
                    uint32_t mo[4], mi[4];
                    int nw = (int)rnd_range(1, 4);
                    for (int w = 0; w < nw; w++) {
                        mo[w] = xs32() & ((1u << f.bits) - 1);
                        mi[w] = xs32() & ((1u << f.bits) - 1);
                        la_event ex = { 0, 0, EV_SPI, 0, f.bits, mo[w], mi[w] };
                        G_EXP[ne++] = ex;
                    }
                    t = gen_spi_xfer(&G_IDEAL, chs, t, 100 * PS_PER_NS, f, mo, mi, nw, 0, true, 3 * PS_PER_NS);
                    t += rnd_range(200, 900) * PS_PER_NS;
                }
                G_IDEAL.t_end = t;
                la_sample(&G_IDEAL, 10 * PS_PER_NS, &G_CAP);
                int n = spi_decode(&G_CAP, 0, 1, 2, 3, f, G_EV, 8192);
                combos++;
                ok += ev_list_same(G_EV, n, G_EXP, ne);
            }
        CHECK(combos == 16 && ok == 16);
    }
    {   /* A-7 I2C: TMP117 류 온도 읽기 @400 kHz, 10 MS/s, 지터 ±20 ns */
        g_rng = 0xA1007u;
        static const char *const nm[2] = { "SDA", "SCL" };
        cap_init(&G_IDEAL, 2, nm, INIT_HI);
        i2c_wr w = { &G_IDEAL.ch[0], &G_IDEAL.ch[1], 5000 * PS_PER_NS, 1250 * PS_PER_NS, 625 * PS_PER_NS,
                     20 * PS_PER_NS, G_EXP, 0, 0 };
        const uint8_t d[2] = { 0x0C, 0x80 };
        i2c_g_regread(&w, 0x48, 0x00, d, 2);
        G_IDEAL.t_end = w.now + 5000 * PS_PER_NS;
        la_sample(&G_IDEAL, 100 * PS_PER_NS, &G_CAP);
        int n = i2c_decode(&G_CAP.ch[0], &G_CAP.ch[1], G_CAP.t_end, G_EV, 64);
        CHECK(w.nexp == 8 && ev_list_same(G_EV, n, G_EXP, w.nexp));
        CHECK(n == 8 && G_EV[1].type == EV_I2C_ADDR && G_EV[1].val == 0x48 && G_EV[1].aux == 0 &&
              G_EV[4].aux == 1 && G_EV[6].val == 0x80 && (G_EV[6].flags & F_NACK));
    }
    printf("Part A: %d/%d\n", g_pass, g_total);

    /* ================= Part B — 오차 · 글리치 · 위반 · 잘림 · 역할 ================= */
    {   /* B-1 무작위 UART: baud × 샘플링률(OVS ≥ 8) × 클럭 오차 ±3% × 지터 × 형식 */
        g_rng = 0xB1001u;
        static const uint32_t bauds[8] = { 9600, 19200, 57600, 115200, 230400, 460800, 921600, 1000000 };
        static const int64_t tss[8] = { 1000, 500, 200, 100, 50, 40, 20, 10 };       /* ns: 1..100 MS/s */
        static const uart_fmt fm[6] = { { 8, 'N', 1 }, { 8, 'E', 1 }, { 8, 'O', 1 }, { 8, 'N', 2 }, { 7, 'E', 1 }, { 7, 'O', 2 } };
        int caps = 0, frames = 0, snap_ok = 0, dec_ok = 0;
        double worst = 0;
        while (caps < 300) {
            uint32_t baud = bauds[rnd_range(0, 7)];
            int64_t ts = tss[rnd_range(0, 7)] * PS_PER_NS;
            double err = (double)rnd_range(-300, 300) / 10000.0;
            int64_t T = (int64_t)((double)PS_PER_S / (baud * (1.0 + err)) + 0.5);
            if (T < 8 * ts) continue;                                                  /* OVS < 8 은 demo 에서 */
            uart_fmt f = fm[rnd_range(0, 5)];
            uint32_t v[30];
            for (int i = 0; i < 30; i++) v[i] = xs32() & 0xFF;
            make_uart(T, f, v, NULL, 30, ts, 3 * T, T / 50, 15 * T);
            uart_est e;
            int rc = uart_autobaud(&G_CAP.ch[0], &e);
            double rel = rc ? 1.0 : (double)llabs(e.bit_ps - T) / (double)T;
            if (rel > worst) worst = rel;
            snap_ok += rc == 0 && e.std_baud == baud;
            int n = rc ? -1 : uart_decode(&G_CAP.ch[0], G_CAP.t_end, e.bit_ps, f, G_EV, 64);
            dec_ok += ev_list_same(G_EV, n, G_EXP, 30);
            frames += 30;
            caps++;
        }
        printf("B-1 uart random: %d captures, %d frames, worst auto-baud error %.3f%%\n", caps, frames, worst * 100);
        CHECK(snap_ok == 300);
        CHECK(dec_ok == 300);
        CHECK(worst < 0.015);                         /* 고정 baud 디코드가 견디는 +3.9% (OVS 8) 의 절반 이하 */
    }
    {   /* B-2 결함 프레임: parity 반전, break, stop=0 — 그 뒤 프레임은 정상 */
        g_rng = 0xB1002u;
        uart_fmt f = { 8, 'E', 1 };
        int64_t T = (PS_PER_S + 57600) / 115200;
        const uint32_t v[6] = { 0x55, 0xA3, 0x00, 0x7E, 0x31, 0x0F };
        const int fault[6] = { 0, UF_PFLIP, UF_BREAK, UF_STOP0, 0, UF_PFLIP | UF_STOP0 };
        make_uart(T, f, v, fault, 6, 50 * PS_PER_NS, 2 * T, 0, 15 * T);
        int n = uart_decode(&G_CAP.ch[0], G_CAP.t_end, T, f, G_EV, 64);
        CHECK(ev_list_same(G_EV, n, G_EXP, 6));
        CHECK(n == 6 && G_EV[1].flags == F_PERR && G_EV[2].flags == (F_BREAK | F_FERR) && G_EV[3].flags == F_FERR &&
              G_EV[4].flags == 0 && G_EV[5].flags == (F_PERR | F_FERR));
    }
    {   /* B-3 글리치: 30 ns 펄스 20개(UART) / SCL 의 30 ns 하이 펄스(I2C) — 필터 없으면 틀리고, 있으면 맞다 */
        g_rng = 0xB1003u;
        uart_fmt f = { 8, 'N', 1 };
        int64_t T = (PS_PER_S + 57600) / 115200;
        uint32_t v[20];
        for (int i = 0; i < 20; i++) v[i] = xs32() & 0xFF;
        make_uart(T, f, v, NULL, 20, 10 * PS_PER_NS, 0, 0, 15 * T);
        static la_chan gl;                                     /* 이상 파형에 펄스 쌍 끼워 넣기 */
        memset(&gl, 0, sizeof gl);
        const la_chan *src = &G_IDEAL.ch[0];
        gl = *src;
        gl.n = 0;
        int64_t start = src->t[0];
        int gi = 0;
        uint32_t k = 0;
        for (int fr = 0; fr < 20; fr++) {
            int64_t off = fr % 2 ? T / 2 - 15 * PS_PER_NS : rnd_range(3 * T / 10, 7 * T / 10);   /* 절반은 비트 중앙 */
            int64_t tg = start + (int64_t)fr * 10 * T + rnd_range(1, 8) * T + off;
            while (k < src->n && src->t[k] < tg) gl.t[gl.n++] = src->t[k++];
            gl.t[gl.n++] = tg;
            gl.t[gl.n++] = tg + 30 * PS_PER_NS;
            gi++;
        }
        while (k < src->n) gl.t[gl.n++] = src->t[k++];
        G_IDEAL.ch[0] = gl;
        la_sample(&G_IDEAL, 10 * PS_PER_NS, &G_CAP);
        uart_est e;
        int rc = uart_autobaud(&G_CAP.ch[0], &e);
        CHECK(gi == 20 && rc == LA_OK && e.std_baud != 115200);              /* 글리치 폭을 비트로 착각 */
        int bad = uart_decode(&G_CAP.ch[0], G_CAP.t_end, T, f, G_EV, 64);   /* 올바른 T 로도 일부 틀림 */
        CHECK(!ev_list_same(G_EV, bad, G_EXP, 20));
        int rm = la_deglitch(&G_CAP.ch[0], 100 * PS_PER_NS);
        CHECK(rm == 20 && uart_autobaud(&G_CAP.ch[0], &e) == LA_OK && e.std_baud == 115200);
        int n = uart_decode(&G_CAP.ch[0], G_CAP.t_end, e.bit_ps, f, G_EV, 64);
        CHECK(ev_list_same(G_EV, n, G_EXP, 20));

        static const char *const nm[2] = { "SDA", "SCL" };       /* I2C: SCL low 구간 1/4 지점에 하이 펄스 */
        cap_init(&G_IDEAL, 2, nm, INIT_HI);
        i2c_wr w = { &G_IDEAL.ch[0], &G_IDEAL.ch[1], 5000 * PS_PER_NS, 1250 * PS_PER_NS, 625 * PS_PER_NS, 0, G_EXP, 0, 0 };
        const uint8_t d[2] = { 0x0C, 0x80 };
        i2c_g_regread(&w, 0x48, 0x00, d, 2);
        G_IDEAL.t_end = w.now + 5000 * PS_PER_NS;
        la_chan *scl = &G_IDEAL.ch[1];
        int64_t tg = scl->t[7] + 1250 * PS_PER_NS / 4;         /* 주소 바이트 4번째 비트 뒤 SCL low 중 */
        memmove(&scl->t[10], &scl->t[8], (scl->n - 8) * sizeof scl->t[0]);
        scl->t[8] = tg;
        scl->t[9] = tg + 30 * PS_PER_NS;
        scl->n += 2;
        la_sample(&G_IDEAL, 10 * PS_PER_NS, &G_CAP);
        n = i2c_decode(&G_CAP.ch[0], &G_CAP.ch[1], G_CAP.t_end, G_EV, 64);
        CHECK(!ev_list_same(G_EV, n, G_EXP, w.nexp));
        CHECK(la_deglitch(&G_CAP.ch[1], 100 * PS_PER_NS) == 1);
        n = i2c_decode(&G_CAP.ch[0], &G_CAP.ch[1], G_CAP.t_end, G_EV, 64);
        CHECK(ev_list_same(G_EV, n, G_EXP, w.nexp));
    }
    {   /* B-3b 링잉: 짧은 펄스 1~4개가 붙어서(간격 < 필터 폭) 와도 원래 파형으로 돌아와야 한다 */
        g_rng = 0xB1013u;
        static la_chan orig, noisy;
        int ok = 0, pulses = 0, removed = 0;
        for (int it = 0; it < 500; it++) {
            memset(&orig, 0, sizeof orig);
            orig.init = (uint8_t)rnd_range(0, 1);
            noisy = orig;
            int64_t t = 0;
            for (int k = 0; k < 40; k++) {
                int64_t prev = t;
                t += rnd_range(400, 1000);
                if (rnd_range(0, 1)) {                                /* prev 와 t 사이에 버스트 */
                    int64_t g = prev + rnd_range(60, 100);
                    int m = (int)rnd_range(1, 4);
                    for (int q = 0; q < 2 * m; q++) { noisy.t[noisy.n++] = g; g += rnd_range(1, 20); }
                    pulses += m;
                }
                orig.t[orig.n++] = t;
                noisy.t[noisy.n++] = t;
            }
            removed += la_deglitch(&noisy, 50);
            ok += chan_same(&orig, &noisy);
        }
        CHECK(ok == 500 && removed == pulses);
    }
    {   /* B-4 SCL high 중 SDA 변화(마스터 버그) → 가짜 STOP / 가짜 Sr + 유령 주소 */
        static const char *const nm[2] = { "SDA", "SCL" };
        static la_event got[2][16];
        int ng[2];
        for (int cse = 0; cse < 2; cse++) {
            cap_init(&G_IDEAL, 2, nm, INIT_HI);
            i2c_wr w = { &G_IDEAL.ch[0], &G_IDEAL.ch[1], 5000 * PS_PER_NS, 1250 * PS_PER_NS, 625 * PS_PER_NS, 0, G_EXP, 0, 0 };
            i2c_g_start(&w);
            i2c_g_byte(&w, 0x48 << 1, 0, true);
            int bad = cse == 0 ? 3 : 0;                         /* 0xA5 = 1010 0101: 비트3=0 → STOP, 비트0=1 → START */
            for (int i = 0; i < 8; i++) i2c_g_bit(&w, (0xA5 >> (7 - i)) & 1, i == bad);
            i2c_g_bit(&w, 0, 0);
            i2c_g_stop(&w);
            G_IDEAL.t_end = w.now + 5000 * PS_PER_NS;
            la_sample(&G_IDEAL, 50 * PS_PER_NS, &G_CAP);
            ng[cse] = i2c_decode(&G_CAP.ch[0], &G_CAP.ch[1], G_CAP.t_end, got[cse], 16);
        }
        const la_event e0[4] = { { 0, 0, EV_I2C_START, 0, 0, 0, 0 }, { 0, 0, EV_I2C_ADDR, 0, 0, 0x48, 0 },
                                 { 0, 0, EV_I2C_MIDBYTE, 0, 3, 0x05, 0 }, { 0, 0, EV_I2C_STOP, 0, 0, 0, 0 } };
        const la_event e1[5] = { { 0, 0, EV_I2C_START, 0, 0, 0, 0 }, { 0, 0, EV_I2C_ADDR, 0, 0, 0x48, 0 },
                                 { 0, 0, EV_I2C_RSTART, 0, 0, 0, 0 },                /* 비트0 위반 = 정상 Sr 과 구별 불가 */
                                 { 0, 0, EV_I2C_ADDR, 0, 0, 0x25, 0 }, { 0, 0, EV_I2C_STOP, 0, 0, 0, 0 } };
        CHECK(ev_list_same(got[0], ng[0], e0, 4));
        CHECK(ev_list_same(got[1], ng[1], e1, 5));
    }
    {   /* B-5 잘린 캡처: UART 프레임 중간 / SPI CS 가 안 올라감 + 부분 워드 / I2C 바이트 중간 */
        uart_fmt f = { 8, 'N', 1 };
        int64_t T = (PS_PER_S + 57600) / 115200;
        const uint32_t hi[2] = { 'H', 'i' };
        g_rng = 0xB1005u;
        make_uart(T, f, hi, NULL, 2, 100 * PS_PER_NS, 0, 0, 0);
        G_IDEAL.t_end = G_IDEAL.ch[0].t[G_IDEAL.ch[0].n - 1] - 3 * T;    /* 'i' 프레임 중간에서 끊음 */
        la_sample(&G_IDEAL, 100 * PS_PER_NS, &G_CAP);
        int n = uart_decode(&G_CAP.ch[0], G_CAP.t_end, T, f, G_EV, 64);
        CHECK(n == 2 && ev_same(&G_EV[0], &G_EXP[0]) && G_EV[1].type == EV_TRUNC);

        static const char *const sn[4] = { "CS_N", "SCLK", "MOSI", "MISO" };
        const int chs[4] = { 0, 1, 2, 3 }, init[4] = { 1, 0, 0, 0 };
        spi_fmt sf = { 0, 8, 0 };
        const uint32_t mo[3] = { 0x9F, 0x00, 0x05 }, mi[3] = { 0xFF, 0xEF, 0x02 };
        cap_init(&G_IDEAL, 4, sn, init);
        int64_t t = gen_spi_xfer(&G_IDEAL, chs, 1000 * PS_PER_NS, 100 * PS_PER_NS, sf, mo, mi, 2, 3, true, 0);
        t = gen_spi_xfer(&G_IDEAL, chs, t + 1000 * PS_PER_NS, 100 * PS_PER_NS, sf, mo, mi, 2, 3, false, 0);
        G_IDEAL.t_end = t + 500 * PS_PER_NS;
        la_sample(&G_IDEAL, 10 * PS_PER_NS, &G_CAP);
        n = spi_decode(&G_CAP, 0, 1, 2, 3, sf, G_EV, 64);
        CHECK(n == 6 && G_EV[0].val == 0x9F && G_EV[1].val2 == 0xEF && G_EV[2].type == EV_SPI_PARTIAL &&
              G_EV[2].aux == 3 && G_EV[2].val == 0 && G_EV[2].val2 == 0 && G_EV[5].type == EV_TRUNC && G_EV[5].aux == 3);
        G_CAP.ch[0].init = 0;                                         /* CS 가 이미 low 인 채로 캡처 시작 */
        n = spi_decode(&G_CAP, 0, 1, 2, 3, sf, G_EV, 64);
        CHECK(n > 0 && G_EV[0].type == EV_TRUNC && G_EV[0].aux == 1);

        static const char *const nm[2] = { "SDA", "SCL" };
        cap_init(&G_IDEAL, 2, nm, INIT_HI);
        i2c_wr w = { &G_IDEAL.ch[0], &G_IDEAL.ch[1], 5000 * PS_PER_NS, 1250 * PS_PER_NS, 625 * PS_PER_NS, 0, G_EXP, 0, 0 };
        i2c_g_start(&w);
        i2c_g_byte(&w, 0x48 << 1, 0, true);
        for (int i = 0; i < 5; i++) i2c_g_bit(&w, 1, 0);
        G_IDEAL.t_end = w.now + 1250 * PS_PER_NS;
        la_sample(&G_IDEAL, 100 * PS_PER_NS, &G_CAP);
        n = i2c_decode(&G_CAP.ch[0], &G_CAP.ch[1], G_CAP.t_end, G_EV, 64);
        CHECK(n == 3 && G_EV[1].type == EV_I2C_ADDR && G_EV[2].type == EV_TRUNC && G_EV[2].aux == 5 && G_EV[2].val == 0x1F);
    }
    {   /* B-6 채널 뒤바뀜: 무작위 I2C 캡처 300개에서 역할 추정 (두 순서 모두) */
        g_rng = 0xB1006u;
        static const char *const nm[2] = { "A", "B" };
        int ok = 0, ok_sw = 0;
        for (int it = 0; it < 300; it++) {
            cap_init(&G_IDEAL, 2, nm, INIT_HI);
            i2c_wr w = { &G_IDEAL.ch[0], &G_IDEAL.ch[1], 5000 * PS_PER_NS, 1250 * PS_PER_NS, 625 * PS_PER_NS, 0, G_EXP, 0, 0 };
            int ntx = (int)rnd_range(1, 4);
            for (int x = 0; x < ntx; x++) {
                int kind = (int)rnd_range(0, 2);
                uint8_t a = (uint8_t)rnd_range(0x08, 0x77);
                if (kind == 0) { i2c_g_start(&w); i2c_g_byte(&w, (uint8_t)(a << 1), 1, true); i2c_g_stop(&w); }  /* 스캔 */
                else if (kind == 1) {
                    i2c_g_start(&w);
                    i2c_g_byte(&w, (uint8_t)(a << 1), 0, true);
                    int nb = (int)rnd_range(1, 4);
                    for (int b = 0; b < nb; b++) i2c_g_byte(&w, (uint8_t)xs32(), 0, false);
                    i2c_g_stop(&w);
                } else {
                    uint8_t d[4] = { (uint8_t)xs32(), (uint8_t)xs32(), (uint8_t)xs32(), (uint8_t)xs32() };
                    i2c_g_regread(&w, a, (uint8_t)xs32(), d, (int)rnd_range(1, 4));
                }
                if (w.nexp > 7000) break;
            }
            G_IDEAL.t_end = w.now + 5000 * PS_PER_NS;
            la_sample(&G_IDEAL, 100 * PS_PER_NS, &G_CAP);
            int sda = -1, scl = -1;
            ok += i2c_guess_roles(&G_CAP, 0, 1, &sda, &scl) == 0 && sda == 0 && scl == 1;
            ok_sw += i2c_guess_roles(&G_CAP, 1, 0, &sda, &scl) == 0 && sda == 0 && scl == 1;
        }
        CHECK(ok == 300 && ok_sw == 300);
    }
    {   /* B-7 auto-baud 의 한계: 1비트 폭이 없는 트래픽 → 폭들의 "최대공약수" 를 비트로 착각 */
        uart_fmt f = { 8, 'N', 1 };
        int64_t T = (PS_PER_S + 57600) / 115200;
        uint32_t z[20], u[20];
        for (int i = 0; i < 20; i++) { z[i] = 0x00; u[i] = 0x55; }
        uart_est e;
        cap_init(&G_IDEAL, 1, NM_UART, INIT_HI);                      /* 0x00, 프레임 사이 2비트 idle */
        int64_t t = 10 * T;
        for (int i = 0; i < 20; i++) t = gen_uart_frame(&G_IDEAL.ch[0], t, T, f, z[i], 0, 0) + 2 * T;
        G_IDEAL.t_end = t + 10 * T;
        la_sample(&G_IDEAL, 10 * PS_PER_NS, &G_CAP);
        CHECK(uart_autobaud(&G_CAP.ch[0], &e) == LA_OK && llabs(e.bit_ps - 3 * T) * 100 < 3 * T);   /* 3T! */
        cap_init(&G_IDEAL, 1, NM_UART, INIT_HI);                      /* 같은 0x00 이라도 연속 전송이면 stop 1T */
        t = 10 * T;
        for (int i = 0; i < 20; i++) t = gen_uart_frame(&G_IDEAL.ch[0], t, T, f, z[i], 0, 0);
        G_IDEAL.t_end = t + 10 * T;
        la_sample(&G_IDEAL, 10 * PS_PER_NS, &G_CAP);
        CHECK(uart_autobaud(&G_CAP.ch[0], &e) == LA_OK && e.std_baud == 115200);
        cap_init(&G_IDEAL, 1, NM_UART, INIT_HI);                      /* 'U' = 0x55: 모든 폭이 1T (보정 문자) */
        t = 10 * T;
        for (int i = 0; i < 20; i++) t = gen_uart_frame(&G_IDEAL.ch[0], t, T, f, u[i], 0, 0) + 2 * T;
        G_IDEAL.t_end = t + 10 * T;
        la_sample(&G_IDEAL, 10 * PS_PER_NS, &G_CAP);
        CHECK(uart_autobaud(&G_CAP.ch[0], &e) == LA_OK && e.std_baud == 115200 && llabs(e.bit_ps - T) * 1000 < T);
    }
    {   /* B-8 SPI 모드 착각: mode 3 를 mode 0 으로 → 데이터는 같고 CPOL 경고 / mode 1 을 mode 0 으로 → 데이터 틀림 */
        g_rng = 0xB1008u;
        static const char *const nm[4] = { "CS_N", "SCLK", "MOSI", "MISO" };
        const int chs[4] = { 0, 1, 2, 3 };
        int res[2] = { 0, 0 };
        for (int cse = 0; cse < 2; cse++) {
            spi_fmt real = { (uint8_t)(cse == 0 ? 3 : 1), 8, 0 }, wrong = { 0, 8, 0 };
            int init[4] = { 1, real.mode >> 1, 0, 0 };
            cap_init(&G_IDEAL, 4, nm, init);
            uint32_t mo[8], mi[8];
            for (int i = 0; i < 8; i++) {
                mo[i] = xs32() & 0xFF; mi[i] = xs32() & 0xFF;
                la_event ex = { 0, 0, EV_SPI, 0, 8, mo[i], mi[i] };
                G_EXP[i] = ex;
            }
            int64_t t = gen_spi_xfer(&G_IDEAL, chs, 500 * PS_PER_NS, 100 * PS_PER_NS, real, mo, mi, 8, 0, true, 0);
            G_IDEAL.t_end = t + 500 * PS_PER_NS;
            la_sample(&G_IDEAL, 10 * PS_PER_NS, &G_CAP);
            int n = spi_decode(&G_CAP, 0, 1, 2, 3, wrong, G_EV, 64);
            int same = 0, cpolw = 0;
            for (int i = 0; i < n && i < 8; i++) {
                same += G_EV[i].val == mo[i] && G_EV[i].val2 == mi[i];
                cpolw += (G_EV[i].flags & F_CPOL) != 0;
            }
            res[cse] = n == 8 && (cse == 0 ? (same == 8 && cpolw == 8) : (same < 8 && cpolw == 0));
        }
        CHECK(res[0] && res[1]);
    }
    {   /* B-9 같은 샘플 동률: SDA 가 SCL 하강 1 ns 뒤에 바뀜 + 4 MS/s → 같은 샘플에 찍힌다 */
        g_rng = 0xB1009u;
        static const char *const nm[2] = { "SDA", "SCL" };
        cap_init(&G_IDEAL, 2, nm, INIT_HI);
        i2c_wr w = { &G_IDEAL.ch[0], &G_IDEAL.ch[1], 5100 * PS_PER_NS, 5000 * PS_PER_NS, 1 * PS_PER_NS, 0, G_EXP, 0, 0 };
        for (int x = 0; x < 10; x++) {
            uint8_t d[3] = { (uint8_t)xs32(), (uint8_t)xs32(), (uint8_t)xs32() };
            i2c_g_regread(&w, (uint8_t)rnd_range(0x08, 0x77), (uint8_t)xs32(), d, 3);
        }
        G_IDEAL.t_end = w.now + 5000 * PS_PER_NS;
        la_sample(&G_IDEAL, 250 * PS_PER_NS, &G_CAP);
        int ties = 0;
        for (uint32_t i = 0; i < G_CAP.ch[0].n; i++) {
            uint32_t k = la_edge_upper(&G_CAP.ch[1], G_CAP.ch[0].t[i]);
            ties += k > 0 && G_CAP.ch[1].t[k - 1] == G_CAP.ch[0].t[i];
        }
        int n = i2c_decode(&G_CAP.ch[0], &G_CAP.ch[1], G_CAP.t_end, G_EV, 8192);
        CHECK(ties > 100 && ev_list_same(G_EV, n, G_EXP, w.nexp));
    }
    {   /* B-10 이벤트 용량 초과 → LA_E_EVCAP, 앞 3개는 온전 */
        static const char *const nm[2] = { "SDA", "SCL" };
        cap_init(&G_IDEAL, 2, nm, INIT_HI);
        i2c_wr w = { &G_IDEAL.ch[0], &G_IDEAL.ch[1], 5000 * PS_PER_NS, 1250 * PS_PER_NS, 625 * PS_PER_NS, 0, G_EXP, 0, 0 };
        const uint8_t d[2] = { 0x0C, 0x80 };
        i2c_g_regread(&w, 0x48, 0x00, d, 2);
        G_IDEAL.t_end = w.now + 5000 * PS_PER_NS;
        la_sample(&G_IDEAL, 100 * PS_PER_NS, &G_CAP);
        int n = i2c_decode(&G_CAP.ch[0], &G_CAP.ch[1], G_CAP.t_end, G_EV, 3);
        CHECK(n == LA_E_EVCAP && ev_list_same(G_EV, 3, G_EXP, 3));
    }
    {   /* B-11 망가진 파일: 올바른 CSV/VCD 를 바이트 단위로 3000번씩 변형 → 크래시·OOB 없이 오류 코드 또는
         *      불변식(토글 시각 증가, 개수 ≤ 용량, t_end ≥ 마지막 토글)을 지키는 캡처. ASan/UBSan 빌드가 증명 */
        g_rng = 0xB1011u;
        static char base[2][4096], mut[4200];
        int blen[2], bad = 0, errs = 0, oks = 0;
        static const char *const nm[3] = { "TX", "SCL", "SDA" };
        cap_init(&G_IDEAL, 3, nm, INIT_HI);
        for (int c = 0; c < 3; c++)
            for (int k = 0; k < 12; k++) G_IDEAL.ch[c].t[G_IDEAL.ch[c].n++] = (k + 1) * (1000 + 300 * c) * PS_PER_NS;
        G_IDEAL.t_end = 20000 * PS_PER_NS;
        blen[0] = la_write_csv(&G_IDEAL, base[0], sizeof base[0]);
        blen[1] = la_write_vcd(&G_IDEAL, base[1], sizeof base[1]);
        for (int it = 0; it < 6000; it++) {
            int fmt = it & 1, n = blen[fmt];
            memcpy(mut, base[fmt], (size_t)n);
            int nm_ = (int)rnd_range(1, 4);
            for (int m = 0; m < nm_; m++) {
                int pos = (int)rnd_range(0, n - 1), op = (int)rnd_range(0, 3);
                static const char pool[] = "01xz#$,.-\n 9!\"%";
                if (op == 0) mut[pos] = (char)xs32();
                else if (op == 1) mut[pos] = pool[rnd_range(0, (int64_t)sizeof pool - 2)];
                else if (op == 2 && n > 1) { memmove(mut + pos, mut + pos + 1, (size_t)(n - pos - 1)); n--; }
                else n = pos + 1;                                       /* 잘린 파일 */
            }
            int rc = fmt ? la_parse_vcd(mut, (size_t)n, &G_RD) : la_parse_csv(mut, (size_t)n, &G_RD);
            if (rc < 0) { errs++; continue; }
            if (rc != 0 || G_RD.nch < 1 || G_RD.nch > LA_MAX_CH) { bad++; continue; }
            for (int c = 0; c < G_RD.nch; c++) {
                const la_chan *ch = &G_RD.ch[c];
                if (ch->n > LA_MAX_EDGES || ch->init > 1) bad++;
                for (uint32_t k = 0; k < ch->n; k++)
                    if (ch->t[k] <= (k ? ch->t[k - 1] : G_RD.t0) || ch->t[k] > G_RD.t_end) { bad++; break; }
            }
            oks++;
        }
        printf("B-11 mutated files: %d rejected with an error code, %d parsed, %d invariant violations\n", errs, oks, bad);
        CHECK(bad == 0 && errs > 1000);
    }
    printf("Part B: %d/%d\n", g_pass, g_total);

    /* ================= Part C — 한 캡처에 세 버스 + protocol table ================= */
    {   /* C-1 6채널 혼합 캡처 → CSV → 파싱 → 세 디코더, VCD 왕복도 같은 캡처 */
        g_rng = 0xC1001u;
        static const char *const nm[6] = { "TX", "SCL", "SDA", "CS_N", "SCLK", "MOSI" };
        const int init[6] = { 1, 1, 1, 1, 0, 0 };
        cap_init(&G_IDEAL, 6, nm, init);
        uart_fmt uf = { 8, 'N', 1 };
        int64_t T = (int64_t)(PS_PER_S / (115200 * 1.015));             /* TX 클럭 +1.5% */
        const char *msg = "AT+TEMP?\r\n";
        static la_event exu[16], exi[16], exs[16];
        int nu = 0, ns = 0;
        int64_t t = 3000 * PS_PER_NS;
        for (const char *p = msg; *p; p++) {
            t = gen_uart_frame(&G_IDEAL.ch[0], t, T, uf, (uint8_t)*p, 0, 20 * PS_PER_NS) + rnd_range(0, T);
            exu[nu++] = ev_uart((uint8_t)*p, 0);
        }
        i2c_wr w = { &G_IDEAL.ch[2], &G_IDEAL.ch[1], 20000 * PS_PER_NS, 1250 * PS_PER_NS, 625 * PS_PER_NS,
                     20 * PS_PER_NS, exi, 0, 0 };
        const uint8_t d[2] = { 0x0C, 0x80 };
        i2c_g_regread(&w, 0x48, 0x00, d, 2);
        const int chs[4] = { 3, 4, 5, -1 };
        spi_fmt sf = { 0, 8, 0 };
        const uint32_t cmd[8] = { 0x02, 0x00, 0x10, 0x00, 0x19, 0x0C, 0x80, 0xA5 };   /* page program: 로그 기록 */
        for (int i = 0; i < 8; i++) { la_event ex = { 0, 0, EV_SPI, 0, 8, cmd[i], 0 }; exs[ns++] = ex; }
        int64_t ts = gen_spi_xfer(&G_IDEAL, chs, 60000 * PS_PER_NS, 100 * PS_PER_NS, sf, cmd, cmd, 8, 0, true, 5 * PS_PER_NS);
        G_IDEAL.t_end = (t > w.now ? (t > ts ? t : ts) : (w.now > ts ? w.now : ts)) + 10000 * PS_PER_NS;
        la_sample(&G_IDEAL, 20 * PS_PER_NS, &G_CAP);                   /* 50 MS/s */
        int nt = la_write_csv(&G_CAP, G_TXT, sizeof G_TXT);
        CHECK(nt > 0 && la_parse_csv(G_TXT, (size_t)nt, &G_RD) == LA_OK && cap_same(&G_CAP, &G_RD));
        uart_est e;
        int ok = uart_autobaud(&G_RD.ch[la_find(&G_RD, "TX")], &e) == LA_OK && e.std_baud == 115200 &&
                 e.std_err_pct > 1.2 && e.std_err_pct < 1.8;          /* "TX 클럭이 +1.5% 빠르다" 를 읽어 냄 */
        int n = uart_decode(&G_RD.ch[0], G_RD.t_end, e.bit_ps, uf, G_EV, 64);
        ok &= ev_list_same(G_EV, n, exu, nu);
        int sda, scl;
        ok &= i2c_guess_roles(&G_RD, 1, 2, &sda, &scl) == 0 && sda == 2 && scl == 1;
        n = i2c_decode(&G_RD.ch[sda], &G_RD.ch[scl], G_RD.t_end, G_EV, 64);
        ok &= ev_list_same(G_EV, n, exi, w.nexp);
        n = spi_decode(&G_RD, 3, 4, 5, -1, sf, G_EV, 64);
        ok &= ev_list_same(G_EV, n, exs, ns);
        CHECK(ok);
        nt = la_write_vcd(&G_RD, G_TXT, sizeof G_TXT);
        CHECK(nt > 0 && la_parse_vcd(G_TXT, (size_t)nt, &G_IDEAL) == LA_OK && cap_same(&G_IDEAL, &G_RD));
    }
    {   /* C-2 protocol table (지터 없음, 400 kHz, 10 MS/s) — 골든 문자열 */
        static const char *const nm[2] = { "SDA", "SCL" };
        cap_init(&G_IDEAL, 2, nm, INIT_HI);
        i2c_wr w = { &G_IDEAL.ch[0], &G_IDEAL.ch[1], 10000 * PS_PER_NS, 1250 * PS_PER_NS, 625 * PS_PER_NS, 0, G_EXP, 0, 0 };
        const uint8_t d[2] = { 0x0C, 0x80 };
        i2c_g_regread(&w, 0x48, 0x00, d, 2);
        G_IDEAL.t_end = w.now + 5000 * PS_PER_NS;
        la_sample(&G_IDEAL, 100 * PS_PER_NS, &G_CAP);
        int n = i2c_decode(&G_CAP.ch[0], &G_CAP.ch[1], G_CAP.t_end, G_EV, 64);
        int len = n > 0 ? la_format_table(G_EV, n, tbl, sizeof tbl) : 0;
        const char *gold =
            "   0       10.000 us  START    \n"
            "   1       11.900 us  ADDR     0x48 W ACK\n"
            "   2       34.400 us  DATA     0x00 ACK\n"
            "   3       57.500 us  Sr       \n"
            "   4       59.400 us  ADDR     0x48 R ACK\n"
            "   5       81.900 us  DATA     0x0C ACK\n"
            "   6      104.400 us  DATA     0x80 NACK\n"
            "   7      127.500 us  STOP     \n";
        CHECK(len == (int)strlen(gold) && strcmp(tbl, gold) == 0);
        if (n > 0 && strcmp(tbl, gold) != 0) fputs(tbl, stdout);
        char small[16];
        CHECK(n > 0 && la_format_table(G_EV, n, small, sizeof small) == len && strlen(small) == 15);
    }
    printf("Part C: %d/%d\n", g_pass, g_total);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif
