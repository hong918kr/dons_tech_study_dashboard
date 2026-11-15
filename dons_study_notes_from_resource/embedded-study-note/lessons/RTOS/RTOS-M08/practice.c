/* RTOS-M08 추가 연습 — 한 번에 검증 (solution.c 의 파서·DFS·프레임 함수를 그대로 복사해 자급자족)
 *
 *   연습 1  재귀 깊이 상한이 알려진 자기 재귀: fmt_u32_rec 를 깊이 10 으로 → M4 160 B / arm64 320 B (= 실측 320 B)
 *   연습 2  함수 포인터(간접 호출): *INDIRECT* 간선을 후보 목록으로 펼치기 — 명령 디스패처
 *   연습 3  MSP(인터럽트 스택) 크기: 첫 프레임은 PSP 로, 나머지 중첩 프레임과 핸들러는 MSP 로
 *   연습 4  여유율의 근거: 같은 소스의 -O0 (clang) / arm-none-eabi-gcc 14.2 .su 로 최악 경로 비교
 *   연습 5  HWM 판정: 실측(HWM)과 정적 예산을 맞대 보고 OK / 정적 분석 누락 / 수상함 / 여유 부족 판정
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================= solution.c 에서 복사한 도구 (Part A/B/C 일부) ======================= */
#define NAMELEN 64
#define MAXF    256          /* 커널 전체 .su(112줄)도 담는다 */
#define MAXE    1024
#define MAXPATH 64

typedef enum { SU_STATIC, SU_DYNAMIC, SU_BOUNDED } su_qual_t;   /* "static" / "dynamic" / "dynamic,bounded" */
typedef struct { char name[NAMELEN]; int bytes; su_qual_t qual; } su_entry_t;
typedef struct { su_entry_t e[MAXF]; int n; int dups; } su_table_t;

typedef struct { const char *from, *to; } edge_t;
typedef struct { const char *name; int bytes; } assume_t;       /* .su 가 없는 함수(라이브러리 등): 사람이 확인한 값 */
typedef struct {
    const su_table_t *su;
    const edge_t *e; int ne;
    const assume_t *as; int nas;
} graph_t;

enum { WCS_OK = 0, WCS_RECURSION, WCS_UNKNOWN, WCS_DYNAMIC };
typedef struct {
    int bytes;                       /* root 부터 가장 깊은 경로의 프레임 합 */
    int err;                         /* WCS_* */
    char culprit[NAMELEN];           /* 에러를 만든 함수 이름 */
    int depth;                       /* path[] 길이 */
    const char *path[MAXPATH];       /* root → … → 잎 */
} wcs_t;

/* (solution.c TODO 1 그대로) .su 한 줄 → 이름 · 바이트 · 한정자. 성공 0, 잘못된 줄 -1.
 *   "fw_graph.c:34:spi_burst_read\t20\tstatic"          (clang: file:line:name)
 *   "fw_graph.c:34:15:spi_burst_read\t4\tstatic"        (gcc:   file:line:col:name)
 *   "drv.cpp:12:7:int Spi::xfer(uint8_t*, int)\t48\tdynamic,bounded"   (C++: 이름에 ':' 와 공백이 있다)
 *   line 은 '\0' 또는 '\n' 으로 끝난다 (끝의 '\r' 허용). 이름이 비었거나 NAMELEN-1 보다 길면 -1. */
static int su_parse_line(const char *line, su_entry_t *out) {
    const char *end = line;
    while (*end && *end != '\n') end++;
    if (end > line && end[-1] == '\r') end--;
    const char *tab1 = memchr(line, '\t', (size_t)(end - line));
    if (!tab1) return -1;
    const char *p = memchr(line, ':', (size_t)(tab1 - line));          /* 파일 이름 끝 */
    if (!p) return -1;
    p++;
    for (int field = 0; field < 2; field++) {                         /* line:, (gcc) col: */
        const char *q = p;
        while (q < tab1 && *q >= '0' && *q <= '9') q++;
        if (q == p || q >= tab1 || *q != ':') { if (field == 0) return -1; break; }
        p = q + 1;
    }
    size_t nlen = (size_t)(tab1 - p);
    if (nlen == 0 || nlen >= NAMELEN) return -1;
    const char *b = tab1 + 1;
    long v = 0;
    const char *q = b;
    while (q < end && *q >= '0' && *q <= '9') { v = v * 10 + (*q - '0'); if (v > 100000000) return -1; q++; }
    if (q == b || q >= end || *q != '\t') return -1;
    q++;
    size_t qlen = (size_t)(end - q);
    su_qual_t qual;
    if (qlen == 6 && memcmp(q, "static", 6) == 0) qual = SU_STATIC;
    else if (qlen == 7 && memcmp(q, "dynamic", 7) == 0) qual = SU_DYNAMIC;
    else if (qlen == 15 && memcmp(q, "dynamic,bounded", 15) == 0) qual = SU_BOUNDED;
    else return -1;
    memcpy(out->name, p, nlen);
    out->name[nlen] = '\0';
    out->bytes = (int)v;
    out->qual = qual;
    return 0;
}

/* (주어짐) 표에 넣기: 같은 이름이 또 오면(서로 다른 파일의 static 함수 등) 큰 값을 남기고 dups++ — 보수적 */
static int su_add(su_table_t *t, const su_entry_t *x) {
    for (int i = 0; i < t->n; i++)
        if (strcmp(t->e[i].name, x->name) == 0) {
            t->dups++;
            if (x->bytes > t->e[i].bytes) t->e[i] = *x;
            return 0;
        }
    if (t->n >= MAXF) return -1;
    t->e[t->n++] = *x;
    return 0;
}

/* (주어짐) 여러 줄 텍스트 → 표. 빈 줄은 건너뜀. 성공 시 항목 수, 실패 시 -(줄 번호) */
static int su_parse(const char *text, su_table_t *t) {
    int lineno = 0;
    for (const char *s = text; *s; ) {
        lineno++;
        const char *nl = strchr(s, '\n');
        if (s[0] != '\n' && s[0] != '\0') {
            su_entry_t x;
            if (su_parse_line(s, &x) != 0 || su_add(t, &x) != 0) return -lineno;
        }
        if (!nl) break;
        s = nl + 1;
    }
    return t->n;
}

/* (주어짐) 이름 → 노드 번호: .su 표 [0, n), assume 표 [n, n+nas). 없으면 -1 */
static int node_id(const graph_t *g, const char *name) {
    for (int i = 0; i < g->su->n; i++) if (strcmp(g->su->e[i].name, name) == 0) return i;
    for (int i = 0; i < g->nas; i++) if (strcmp(g->as[i].name, name) == 0) return g->su->n + i;
    return -1;
}
static const char *node_name(const graph_t *g, int id) {
    return id < g->su->n ? g->su->e[id].name : g->as[id - g->su->n].name;
}
static int node_self(const graph_t *g, int id, su_qual_t *q) {
    if (id < g->su->n) { *q = g->su->e[id].qual; return g->su->e[id].bytes; }
    *q = SU_STATIC;
    return g->as[id - g->su->n].bytes;
}

#define MAXN (MAXF + 32)
enum { WHITE, GRAY, BLACK };
typedef struct { unsigned char color[MAXN]; int best[MAXN]; int next[MAXN]; } dfs_t;

static void set_err(wcs_t *o, int err, const char *who) {
    o->err = err;
    snprintf(o->culprit, sizeof o->culprit, "%s", who);
}

/* (solution.c TODO 2 그대로) DFS. best[u] = self(u) + max(best[v] for 간선 u→v), next[u] = 그 v (동점이면 간선 순서상 먼저).
 *   GRAY 를 다시 만나면 재귀(사이클) → WCS_RECURSION, culprit = 다시 만난 함수.
 *   callee 가 .su 에도 assume 에도 없으면 → WCS_UNKNOWN, culprit = callee 이름.
 *   qualifier 가 dynamic(상한 없음) → WCS_DYNAMIC, culprit = 그 함수. dynamic,bounded 는 값 그대로 쓴다.
 *   성공 0, 에러 -1 (o->err 설정). */
static int dfs(const graph_t *g, int u, dfs_t *c, wcs_t *o) {
    if (c->color[u] == BLACK) return 0;
    if (c->color[u] == GRAY) { set_err(o, WCS_RECURSION, node_name(g, u)); return -1; }
    c->color[u] = GRAY;
    su_qual_t q;
    int self = node_self(g, u, &q);
    if (q == SU_DYNAMIC) { set_err(o, WCS_DYNAMIC, node_name(g, u)); return -1; }
    int best_child = 0, nx = -1;
    const char *me = node_name(g, u);
    for (int i = 0; i < g->ne; i++) {
        if (strcmp(g->e[i].from, me) != 0) continue;
        int v = node_id(g, g->e[i].to);
        if (v < 0) { set_err(o, WCS_UNKNOWN, g->e[i].to); return -1; }
        if (dfs(g, v, c, o) != 0) return -1;
        if (nx < 0 || c->best[v] > best_child) { best_child = c->best[v]; nx = v; }
    }
    c->best[u] = self + best_child;
    c->next[u] = nx;
    c->color[u] = BLACK;
    return 0;
}

/* (주어짐) 루트에서 시작해 dfs 를 돌리고 next[] 를 따라 경로를 채운다 */
static int worst_stack(const graph_t *g, const char *root, wcs_t *o) {
    static dfs_t c;                                     /* MAXN 개 배열 3개: 호출마다 0 으로 */
    memset(&c, 0, sizeof c);
    memset(o, 0, sizeof *o);
    int r = node_id(g, root);
    if (r < 0) { set_err(o, WCS_UNKNOWN, root); return -1; }
    if (dfs(g, r, &c, o) != 0) return -1;
    o->bytes = c.best[r];
    for (int u = r; u >= 0 && o->depth < MAXPATH; u = c.next[u]) o->path[o->depth++] = node_name(g, u);
    return 0;
}

/* ---- 프레임 (solution.c TODO 3 + Part B 타입) ---- */
static int exc_frame_bytes(int fpu_ctx, int align_pad) {
    return (fpu_ctx ? 26 : 8) * 4 + (align_pad ? 4 : 0);
}

typedef struct { const char *name; int nvic_prio; int path; int fpu; } isr_t;   /* nvic_prio: 작을수록 급함 */
/* ---- 여유 (solution.c TODO 6) ---- */
typedef struct { int pct; int min_bytes; int align; } margin_t;   /* +pct %, 최소 +min_bytes, align 배수로 올림 */
static int apply_margin(int raw, const margin_t *m) {
    int add = (raw * m->pct + 99) / 100;
    if (add < m->min_bytes) add = m->min_bytes;
    int t = raw + add;
    return (t + m->align - 1) / m->align * m->align;
}

static int edges_parse(char *text, edge_t *e, int max) {
    int n = 0;
    for (char *s = strtok(text, "\n"); s && n < max; s = strtok(NULL, "\n")) {
        char *sp = strchr(s, ' ');
        if (!sp) continue;
        *sp = '\0';
        e[n].from = s; e[n].to = sp + 1; n++;
    }
    return n;
}


/* ======================= DATA (solution.c 와 같은 출처 + -O0 / gcc) =======================
 * SU_FW_M4_O0 / EDGES_FW_M4_O0 : measure_su.sh 의 m4o0 빌드 (clang 21, -O0) — -O0 에선 __aeabi_memcpy 호출이 없다
 * SU_FW_M4_GCC / EDGES_FW_M4_GCC: Lima VM 의 arm-none-eabi-gcc 14.2.1 -O2 -fstack-usage -fcallgraph-info=su
 *   (.su 는 file:line:col:name 형식, 간선은 .ci 의 edge 줄에서) — md 9. 추가 연습 연습 4 */
static const char SU_FW_M4[] =
    "fw_graph.c:31:spi_burst_read\t20\tstatic\n"
    "fw_graph.c:36:imu_read\t32\tstatic\n"
    "fw_graph.c:46:filter_update\t8\tstatic\n"
    "fw_graph.c:56:pid_step\t0\tstatic\n"
    "fw_graph.c:67:fmt_u32\t44\tstatic\n"
    "fw_graph.c:75:log_vformat\t56\tstatic\n"
    "fw_graph.c:96:uart_write\t20\tstatic\n"
    "fw_graph.c:101:log_printf\t120\tstatic\n"
    "fw_graph.c:110:control_step\t56\tstatic\n"
    "fw_graph.c:126:crc16_ccitt\t16\tstatic\n"
    "fw_graph.c:135:cobs_encode\t32\tstatic\n"
    "fw_graph.c:147:comms_send_telemetry\t120\tstatic\n"
    "fw_graph.c:164:rx_parse_once\t56\tstatic\n"
    "fw_graph.c:180:isr_uart_rx\t0\tstatic\n"
    "fw_graph.c:184:adc_average\t88\tstatic\n"
    "fw_graph.c:197:isr_adc_dma\t8\tstatic\n"
    "fw_graph.c:201:isr_tick\t0\tstatic\n"
    "fw_graph.c:206:fmt_u32_rec\t16\tstatic\n"
    "fw_graph.c:214:ctrl_task_once\t0\tstatic\n"
    "fw_graph.c:219:comms_task_once\t0\tstatic\n";

static const char EDGES_FW_M4[] =
    "comms_send_telemetry cobs_encode\ncomms_send_telemetry crc16_ccitt\ncomms_send_telemetry uart_write\n"
    "comms_task_once comms_send_telemetry\ncontrol_step filter_update\ncontrol_step imu_read\n"
    "control_step log_printf\ncontrol_step pid_step\nctrl_task_once control_step\n"
    "fmt_u32_rec fmt_u32_rec\nimu_read spi_burst_read\nisr_adc_dma adc_average\n"
    "log_printf log_vformat\nlog_printf uart_write\nlog_vformat __aeabi_memcpy\n"
    "log_vformat fmt_u32\nrx_parse_once crc16_ccitt\n";

static const char SU_FW_A64[] =
    "fw_graph.c:31:spi_burst_read\t0\tstatic\n"
    "fw_graph.c:36:imu_read\t64\tstatic\n"
    "fw_graph.c:46:filter_update\t0\tstatic\n"
    "fw_graph.c:56:pid_step\t0\tstatic\n"
    "fw_graph.c:67:fmt_u32\t48\tstatic\n"
    "fw_graph.c:75:log_vformat\t112\tstatic\n"
    "fw_graph.c:96:uart_write\t0\tstatic\n"
    "fw_graph.c:101:log_printf\t128\tstatic\n"
    "fw_graph.c:110:control_step\t112\tstatic\n"
    "fw_graph.c:126:crc16_ccitt\t0\tstatic\n"
    "fw_graph.c:135:cobs_encode\t0\tstatic\n"
    "fw_graph.c:147:comms_send_telemetry\t128\tstatic\n"
    "fw_graph.c:164:rx_parse_once\t80\tstatic\n"
    "fw_graph.c:180:isr_uart_rx\t0\tstatic\n"
    "fw_graph.c:184:adc_average\t64\tstatic\n"
    "fw_graph.c:197:isr_adc_dma\t16\tstatic\n"
    "fw_graph.c:201:isr_tick\t0\tstatic\n"
    "fw_graph.c:206:fmt_u32_rec\t32\tstatic\n"
    "fw_graph.c:214:ctrl_task_once\t0\tstatic\n"
    "fw_graph.c:219:comms_task_once\t0\tstatic\n";

static const char EDGES_FW_A64[] =
    "adc_average __stack_chk_fail\ncomms_send_telemetry __stack_chk_fail\ncomms_send_telemetry cobs_encode\n"
    "comms_send_telemetry crc16_ccitt\ncomms_send_telemetry uart_write\ncomms_task_once comms_send_telemetry\n"
    "control_step filter_update\ncontrol_step imu_read\ncontrol_step log_printf\n"
    "control_step pid_step\nctrl_task_once control_step\nfmt_u32 __stack_chk_fail\n"
    "fmt_u32_rec fmt_u32_rec\nimu_read __stack_chk_fail\nimu_read spi_burst_read\n"
    "isr_adc_dma adc_average\nlog_printf __stack_chk_fail\nlog_printf log_vformat\n"
    "log_printf uart_write\nlog_vformat __stack_chk_fail\nlog_vformat fmt_u32\n"
    "log_vformat memcpy\nrx_parse_once __stack_chk_fail\nrx_parse_once crc16_ccitt\n";

static const char SU_FW_M4_O0[] =
    "fw_graph.c:31:spi_burst_read\t16\tstatic\n"
    "fw_graph.c:36:imu_read\t48\tstatic\n"
    "fw_graph.c:46:filter_update\t24\tstatic\n"
    "fw_graph.c:56:pid_step\t28\tstatic\n"
    "fw_graph.c:67:fmt_u32\t40\tstatic\n"
    "fw_graph.c:75:log_vformat\t80\tstatic\n"
    "fw_graph.c:96:uart_write\t12\tstatic\n"
    "fw_graph.c:101:log_printf\t136\tstatic\n"
    "fw_graph.c:110:control_step\t56\tstatic\n"
    "fw_graph.c:126:crc16_ccitt\t24\tstatic\n"
    "fw_graph.c:135:cobs_encode\t28\tstatic\n"
    "fw_graph.c:147:comms_send_telemetry\t160\tstatic\n"
    "fw_graph.c:164:rx_parse_once\t72\tstatic\n"
    "fw_graph.c:180:isr_uart_rx\t0\tstatic\n"
    "fw_graph.c:184:adc_average\t64\tstatic\n"
    "fw_graph.c:197:isr_adc_dma\t8\tstatic\n"
    "fw_graph.c:201:isr_tick\t0\tstatic\n"
    "fw_graph.c:206:fmt_u32_rec\t24\tstatic\n"
    "fw_graph.c:214:ctrl_task_once\t16\tstatic\n"
    "fw_graph.c:219:comms_task_once\t16\tstatic\n";

static const char EDGES_FW_M4_O0[] =
    "comms_send_telemetry cobs_encode\ncomms_send_telemetry crc16_ccitt\ncomms_send_telemetry uart_write\n"
    "comms_task_once comms_send_telemetry\ncontrol_step filter_update\ncontrol_step imu_read\n"
    "control_step log_printf\ncontrol_step pid_step\nctrl_task_once control_step\n"
    "fmt_u32_rec fmt_u32_rec\nimu_read spi_burst_read\nisr_adc_dma adc_average\n"
    "log_printf log_vformat\nlog_printf uart_write\nlog_vformat fmt_u32\n"
    "rx_parse_once crc16_ccitt\n";

static const char SU_FW_M4_GCC[] =
    "fw_graph.c:31:15:spi_burst_read\t4\tstatic\n"
    "fw_graph.c:36:14:imu_read\t40\tstatic\n"
    "fw_graph.c:46:15:filter_update\t0\tstatic\n"
    "fw_graph.c:56:16:pid_step\t0\tstatic\n"
    "fw_graph.c:67:17:fmt_u32\t32\tstatic\n"
    "fw_graph.c:75:17:log_vformat\t56\tstatic\n"
    "fw_graph.c:96:15:uart_write\t0\tstatic\n"
    "fw_graph.c:101:15:log_printf\t112\tstatic\n"
    "fw_graph.c:110:15:control_step\t32\tstatic\n"
    "fw_graph.c:126:19:crc16_ccitt\t4\tstatic\n"
    "fw_graph.c:135:17:cobs_encode\t20\tstatic\n"
    "fw_graph.c:147:15:comms_send_telemetry\t112\tstatic\n"
    "fw_graph.c:164:14:rx_parse_once\t48\tstatic\n"
    "fw_graph.c:180:15:isr_uart_rx\t0\tstatic\n"
    "fw_graph.c:184:19:adc_average\t56\tstatic\n"
    "fw_graph.c:197:15:isr_adc_dma\t8\tstatic\n"
    "fw_graph.c:201:15:isr_tick\t0\tstatic\n"
    "fw_graph.c:206:17:fmt_u32_rec\t16\tstatic\n"
    "fw_graph.c:214:15:ctrl_task_once\t0\tstatic\n"
    "fw_graph.c:219:15:comms_task_once\t0\tstatic\n";

static const char EDGES_FW_M4_GCC[] =
    "comms_send_telemetry cobs_encode\ncomms_send_telemetry crc16_ccitt\ncomms_send_telemetry uart_write\n"
    "comms_task_once comms_send_telemetry\ncontrol_step filter_update\ncontrol_step imu_read\n"
    "control_step log_printf\ncontrol_step pid_step\nctrl_task_once control_step\n"
    "fmt_u32_rec fmt_u32_rec\nimu_read spi_burst_read\nisr_adc_dma adc_average\n"
    "log_printf log_vformat\nlog_printf uart_write\nlog_vformat fmt_u32\n"
    "log_vformat memcpy\nrx_parse_once crc16_ccitt\n";

/* ======================= 연습 1 — 깊이 상한이 있는 자기 재귀 ======================= */
/* rec_fn 의 자기 간선을 빼고, rec_fn 의 프레임을 max_depth 배로 본다 (가장 깊은 호출에서 자식까지 부른다고 가정).
 * 상호 재귀(a→b→a)는 여전히 WCS_RECURSION. 원래 표는 건드리지 않는다. */
static su_table_t g_rec_su;
static edge_t g_rec_e[MAXE];
static int worst_stack_rec(const graph_t *g, const char *root, const char *rec_fn, int max_depth, wcs_t *o) {
    g_rec_su = *g->su;
    int ne = 0;
    for (int i = 0; i < g_rec_su.n; i++)
        if (strcmp(g_rec_su.e[i].name, rec_fn) == 0) g_rec_su.e[i].bytes *= max_depth;
    for (int i = 0; i < g->ne; i++)
        if (!(strcmp(g->e[i].from, rec_fn) == 0 && strcmp(g->e[i].to, rec_fn) == 0)) g_rec_e[ne++] = g->e[i];
    graph_t h = { &g_rec_su, g_rec_e, ne, g->as, g->nas };
    return worst_stack(&h, root, o);
}

/* ======================= 연습 2 — 간접 호출 펼치기 ======================= */
/* caller → *INDIRECT* 간선을 caller → cands[i] 로 바꾼 간선 배열을 out 에. 반환 = 간선 수 */
static int expand_indirect(const edge_t *in, int n, const char *caller, const char *const *cands, int nc,
                           edge_t *out, int max) {
    int k = 0;
    for (int i = 0; i < n && k < max; i++) {
        if (strcmp(in[i].from, caller) == 0 && strcmp(in[i].to, "*INDIRECT*") == 0) {
            for (int c = 0; c < nc && k < max; c++) out[k++] = (edge_t){ caller, cands[c] };
        } else out[k++] = in[i];
    }
    return k;
}

/* ======================= 연습 3 — MSP 크기 ======================= */
/* isr_nest_bytes 와 같은 DP 인데 레벨 1 의 프레임은 세지 않는다 (태스크를 선점한 첫 예외의 프레임은 PSP 로 간다).
 * 레벨 2 부터의 프레임은 "아래 레벨 ISR 이 FP 를 썼나" 에 따라 32/104 (+4). */
static int msp_bytes(const isr_t *v, int n, int align_pad) {
    if (n == 0) return 0;
    long best[2] = { 0, -1 };                      /* 레벨 1 아래 = 태스크: 프레임이 PSP 라 상태 무관 → f=0 에서 시작 */
    int done_prio = 1 << 30, first = 1;
    for (;;) {
        int lvl = -1;
        for (int i = 0; i < n; i++)
            if (v[i].nvic_prio < done_prio && v[i].nvic_prio > lvl) lvl = v[i].nvic_prio;
        if (lvl < 0) break;
        long nb[2] = { -1, -1 };
        for (int i = 0; i < n; i++) {
            if (v[i].nvic_prio != lvl) continue;
            for (int f = 0; f < 2; f++) {
                if (best[f] < 0) continue;
                long t = best[f] + (first ? 0 : exc_frame_bytes(f, align_pad)) + v[i].path;
                int nf = v[i].fpu ? 1 : 0;
                if (t > nb[nf]) nb[nf] = t;
            }
        }
        best[0] = nb[0]; best[1] = nb[1];
        done_prio = lvl; first = 0;
    }
    return (int)(best[0] > best[1] ? best[0] : best[1]);
}

/* ======================= 연습 5 — HWM 판정 ======================= */
typedef enum { V_OK, V_STATIC_MISSED, V_SUSPICIOUS, V_LOW_HEADROOM } verdict_t;
/* used = (depth − hwm) × word_bytes.
 *   used > static_raw           → V_STATIC_MISSED (정적 분석이 뭔가 놓쳤다: 간접 호출, 라이브러리, asm, 인라인 변화)
 *   used × 4 < static_raw       → V_SUSPICIOUS (시험이 최악 경로를 안 밟았거나 HWM 자체가 무의미 — POSIX 포트)
 *   hwm 비율 < min_headroom_pct → V_LOW_HEADROOM
 *   그 밖                        → V_OK */
static verdict_t hwm_verdict(int depth_words, int word_bytes, int hwm_words, int static_raw, int min_headroom_pct) {
    int used = (depth_words - hwm_words) * word_bytes;
    if (used > static_raw) return V_STATIC_MISSED;
    if (used * 4 < static_raw) return V_SUSPICIOUS;
    if (hwm_words * 100 < min_headroom_pct * depth_words) return V_LOW_HEADROOM;
    return V_OK;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static su_table_t g_m4, g_a64, g_o0, g_gcc, g_cmd;
static edge_t e_m4[MAXE], e_a64[MAXE], e_o0[MAXE], e_gcc[MAXE], e_cmd[64], e_cmd2[64];
static char eb[6][4096];
static int load(const char *su, su_table_t *t, const char *ed, char *buf, edge_t *e) {
    if (su_parse(su, t) < 0) return -1;
    snprintf(buf, 4096, "%s", ed);
    return edges_parse(buf, e, MAXE);
}

int main(void) {
    static const assume_t AS_M4[] = { { "__aeabi_memcpy", 0 } };
    static const assume_t AS_A64[] = { { "memcpy", 0 }, { "__stack_chk_fail", 0 } };
    static const assume_t AS_GCC[] = { { "memcpy", 0 } };
    int n_m4 = load(SU_FW_M4, &g_m4, EDGES_FW_M4, eb[0], e_m4);
    int n_a64 = load(SU_FW_A64, &g_a64, EDGES_FW_A64, eb[1], e_a64);
    int n_o0 = load(SU_FW_M4_O0, &g_o0, EDGES_FW_M4_O0, eb[2], e_o0);
    int n_gcc = load(SU_FW_M4_GCC, &g_gcc, EDGES_FW_M4_GCC, eb[3], e_gcc);
    CHECK(n_m4 == 17 && n_a64 == 24 && n_o0 == 16 && n_gcc == 17 && g_gcc.n == 20);
    graph_t gm = { &g_m4, e_m4, n_m4, AS_M4, 1 }, ga = { &g_a64, e_a64, n_a64, AS_A64, 2 };
    graph_t go = { &g_o0, e_o0, n_o0, NULL, 0 }, gg = { &g_gcc, e_gcc, n_gcc, AS_GCC, 1 };
    wcs_t w;

    /* 연습 1 */
    CHECK(worst_stack(&gm, "fmt_u32_rec", &w) == -1 && w.err == WCS_RECURSION);
    CHECK(worst_stack_rec(&gm, "fmt_u32_rec", "fmt_u32_rec", 10, &w) == 0 && w.bytes == 160);
    CHECK(worst_stack_rec(&ga, "fmt_u32_rec", "fmt_u32_rec", 10, &w) == 0 && w.bytes == 320);  /* demo 실측 320 B */
    CHECK(worst_stack_rec(&gm, "ctrl_task_once", "fmt_u32_rec", 10, &w) == 0 && w.bytes == 276); /* 안 닿으면 무관 */
    CHECK(g_m4.e[17].bytes == 16);                                    /* 원래 표는 그대로 (fmt_u32_rec) */

    /* 연습 2 — 가상의 명령 디스패처 .su (컴파일러 출력 아님, 숫자 연습용) */
    CHECK(su_parse("cmd.c:10:cmd_dispatch\t24\tstatic\ncmd.c:20:cmd_led\t8\tstatic\ncmd.c:30:cmd_cfg_save\t96\tstatic\n"
                   "cmd.c:40:eeprom_write\t40\tstatic\ncmd.c:50:cmd_status\t16\tstatic\n", &g_cmd) == 5);
    snprintf(eb[4], 4096, "cmd_dispatch *INDIRECT*\ncmd_cfg_save eeprom_write\n");
    int nc = edges_parse(eb[4], e_cmd, 64);
    graph_t gc = { &g_cmd, e_cmd, nc, NULL, 0 };
    CHECK(worst_stack(&gc, "cmd_dispatch", &w) == -1 && w.err == WCS_UNKNOWN && strcmp(w.culprit, "*INDIRECT*") == 0);
    static const char *const cands[] = { "cmd_led", "cmd_cfg_save", "cmd_status" };
    int nc2 = expand_indirect(e_cmd, nc, "cmd_dispatch", cands, 3, e_cmd2, 64);
    graph_t gc2 = { &g_cmd, e_cmd2, nc2, NULL, 0 };
    CHECK(nc2 == 4);
    CHECK(worst_stack(&gc2, "cmd_dispatch", &w) == 0 && w.bytes == 24 + 96 + 40 && strcmp(w.path[1], "cmd_cfg_save") == 0);
    nc2 = expand_indirect(e_cmd, nc, "cmd_dispatch", cands, 1, e_cmd2, 64);   /* 후보를 빠뜨리면 과소평가 */
    gc2.ne = nc2;
    CHECK(worst_stack(&gc2, "cmd_dispatch", &w) == 0 && w.bytes == 32);

    /* 연습 3 — Part C 의 ISR 경로 (USART2 40 / ADC DMA 96 / SysTick 44 / PendSV 0) */
    isr_t isrs[] = { { "USART2_IRQHandler", 5, 40, 0 }, { "isr_adc_dma", 6, 96, 0 },
                     { "xPortSysTickHandler", 15, 44, 0 }, { "xPortPendSVHandler", 15, 0, 0 } };
    CHECK(msp_bytes(isrs, 4, 1) == 44 + (36 + 96) + (36 + 40));
    CHECK(msp_bytes(isrs, 4, 0) == 44 + (32 + 96) + (32 + 40));
    isrs[1].fpu = 1;                                                  /* ADC ISR 에서 float 필터를 돌리면 */
    CHECK(msp_bytes(isrs, 4, 1) == 44 + (36 + 96) + (108 + 40));
    CHECK(msp_bytes(isrs, 1, 1) == 40 && msp_bytes(NULL, 0, 1) == 0);

    /* 연습 4 — 여유율 근거: 같은 소스, 다른 빌드 */
    CHECK(worst_stack(&go, "ctrl_task_once", &w) == 0 && w.bytes == 328);     /* -O0: memcpy 호출 자체가 없다 */
    int o2 = 276, o0 = w.bytes;
    CHECK(worst_stack(&gg, "ctrl_task_once", &w) == 0 && w.bytes == 232);     /* gcc 14.2 -O2 */
    int gcc = w.bytes;
    CHECK(o0 * 100 / o2 == 118 && gcc * 100 / o2 == 84);
    margin_t m = { 25, 64, 8 };
    CHECK(apply_margin(o2, &m) >= o0 && apply_margin(o2, &m) >= gcc);       /* 352 ≥ 328, 232 */
    CHECK(worst_stack(&gg, "comms_task_once", &w) == 0 && w.bytes == 132);
    CHECK(worst_stack(&go, "comms_task_once", &w) == 0 && w.bytes == 16 + 160 + 28);

    /* 연습 5 — HWM 판정 */
    CHECK(hwm_verdict(256, 8, 251, 400, 20) == V_SUSPICIOUS);        /* POSIX 포트 실측: depth−HWM = 5 워드 */
    CHECK(hwm_verdict(154, 4, 40, 492, 20) == V_OK);                 /* 보드에서 읽었다고 가정한 값: 456 B 사용, 26 % 여유 */
    CHECK(hwm_verdict(154, 4, 20, 492, 20) == V_STATIC_MISSED);      /* 536 B > 정적 492 B → 분석이 놓친 경로 */
    CHECK(hwm_verdict(130, 4, 20, 492, 20) == V_LOW_HEADROOM);       /* 440 B 사용, 여유 15 % */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
