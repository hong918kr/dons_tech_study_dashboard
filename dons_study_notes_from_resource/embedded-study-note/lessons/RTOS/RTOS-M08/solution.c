/* RTOS-M08 태스크 스택 예산: ISR 중첩·예외 프레임 포함 최악 스택
 *
 * "스택 예산 계산기" 를 만든다. 입력은 컴파일러가 실제로 낸 숫자다:
 *   - .su  : clang/gcc -fstack-usage 의 함수별 프레임 바이트 ("file:line[:col]:name<TAB>bytes<TAB>qualifier")
 *   - 간선 : 호출 관계 "caller callee" — clang 은 -fcallgraph-info 가 없어서 callgraph.sh 가 objdump 의 bl 로 뽑았다
 * 아래 DATA 블록의 문자열은 fw_graph.c / cm4/fw_tasks.c / FreeRTOS V11.3.2 를 clang 21 로 컴파일해 나온 출력을
 * 그대로 붙인 것이다 (measure_su.sh, cm4/kernel_su.sh — md 5. 풀이).
 *
 *   Part A  su_parse_line()  : .su 한 줄 파싱 (clang/gcc 형식, C++ 이름, dynamic/bounded, 잘못된 줄 거부)
 *           worst_stack()    : 루트에서 가장 깊은 경로 (DFS + 메모), 재귀 → 에러, 모르는 함수 → 에러
 *   Part B  exc_frame_bytes() / switch_frame_bytes() : Cortex-M4F 예외 프레임(기본 8워드 / FP 확장 26워드, +4 정렬)
 *                              과 FreeRTOS ARM_CM4F PendSV 가 소프트웨어로 쌓는 r4–r11,r14 (+ s16–s31)
 *           isr_nest_bytes() : ISR 이 "태스크 스택 위에서" 돈다면 쌓이는 중첩 ISR 몫 (우선순위 레벨별, FPU 상태 DP)
 *   Part C  apply_margin() / task_budget() : 태스크별 예산 = 경로 + 문맥 프레임 (+ ISR 몫) + 여유 → FreeRTOS 워드 수
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so          (-v: 표 출력)
 * 도구 모드: ./so --su a.su [--su b.su] --edges e.txt --root NAME [--root …] [--assume NAME=BYTES]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================= Part A — .su 파서 + 최악 경로 ======================= */
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

/* TODO 1: .su 한 줄 → 이름 · 바이트 · 한정자. 성공 0, 잘못된 줄 -1.
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

/* TODO 2: DFS. best[u] = self(u) + max(best[v] for 간선 u→v), next[u] = 그 v (동점이면 간선 순서상 먼저).
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

/* ======================= Part B — 예외 프레임 · 문맥 프레임 · ISR 중첩 ======================= */
/* TODO 3: Cortex-M4F 예외 진입 때 하드웨어가 쌓는 바이트.
 *   기본 프레임  r0–r3, r12, lr, pc, xPSR                      =  8 워드 =  32 B
 *   확장 프레임  + s0–s15, FPSCR, 예약 1                         = 26 워드 = 104 B  (선점당한 문맥이 FP 를 쓰던 중: CONTROL.FPCA=1)
 *   align_pad=1 이면 진입 SP 가 8 정렬이 아니었을 때의 패딩 4 B 를 더한다 (최악 가정). */
static int exc_frame_bytes(int fpu_ctx, int align_pad) {
    return (fpu_ctx ? 26 : 8) * 4 + (align_pad ? 4 : 0);
}

/* TODO 4: FreeRTOS ARM_CM4F 의 PendSV 가 태스크 스택(PSP)에 소프트웨어로 더 쌓는 바이트 (port.c 518–521):
 *   tst r14,#0x10 / vstmdbeq r0!,{s16-s31}  → FP 문맥이면 16 워드
 *   stmdb r0!,{r4-r11, r14}                 → 항상 9 워드 */
static int switch_frame_bytes(int task_fpu) {
    return (9 + (task_fpu ? 16 : 0)) * 4;
}

typedef struct { const char *name; int nvic_prio; int path; int fpu; } isr_t;   /* nvic_prio: 작을수록 급함 */
typedef enum { ISR_ON_MSP, ISR_ON_TASK_STACK } isr_model_t;

/* TODO 5: ISR 이 태스크 스택 위에서 돈다면(별도 인터럽트 스택이 없는 포트) 중첩으로 쌓이는 최대 바이트.
 *   - 같은 nvic_prio 끼리는 서로 선점하지 못한다 → 레벨마다 ISR 하나만 쌓인다.
 *   - 덜 급한 레벨부터 차례로 중첩: 레벨 k 의 몫 = exc_frame_bytes(바로 아래 문맥의 FP 사용, align_pad) + path_k.
 *     "바로 아래 문맥" = 레벨 1 은 태스크(task_fpu), 그 위는 아래 레벨에서 고른 ISR 의 fpu.
 *   - 레벨 안에서 path 가 가장 큰 ISR 이 늘 최악은 아니다 (FP 를 쓰는 ISR 이 다음 프레임을 키운다) →
 *     "직전 문맥 fpu = 0/1" 두 상태로 DP. n == 0 이면 0. */
static int isr_nest_bytes(const isr_t *v, int n, int task_fpu, int align_pad) {
    if (n == 0) return 0;
    long best[2] = { -1, -1 };                     /* best[f] = 지금까지 쌓인 최대 바이트, 맨 위 문맥 fpu = f */
    best[task_fpu ? 1 : 0] = 0;
    int done_prio = 1 << 30;                       /* 처리한 레벨보다 급한(작은) 다음 레벨을 찾는다 */
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
                long t = best[f] + exc_frame_bytes(f, align_pad) + v[i].path;
                int nf = v[i].fpu ? 1 : 0;
                if (t > nb[nf]) nb[nf] = t;
            }
        }
        best[0] = nb[0]; best[1] = nb[1];
        done_prio = lvl;
    }
    return (int)(best[0] > best[1] ? best[0] : best[1]);
}

/* ======================= Part C — 여유율과 태스크 예산 ======================= */
typedef struct { int pct; int min_bytes; int align; } margin_t;   /* +pct %, 최소 +min_bytes, align 배수로 올림 */
typedef struct { const char *name; const char *root; int fpu; } task_spec_t;
typedef struct { int path, ctx, isr, raw, total, words; wcs_t w; } budget_t;

/* TODO 6: total = round_up(raw + max(ceil(raw * pct / 100), min_bytes), align) */
static int apply_margin(int raw, const margin_t *m) {
    int add = (raw * m->pct + 99) / 100;
    if (add < m->min_bytes) add = m->min_bytes;
    int t = raw + add;
    return (t + m->align - 1) / m->align * m->align;
}

/* TODO 7: 태스크 하나의 예산.
 *   path = worst_stack(root) (에러면 -1 반환, b->w 에 이유)
 *   ctx  = exc_frame_bytes(fpu, 1) + switch_frame_bytes(fpu)       — 전환당해 쉬는 동안 PSP 위의 문맥
 *   isr  = ISR_ON_MSP 면 0 (첫 프레임은 ctx 에 이미 있다 — 그 위 중첩·핸들러는 MSP 로 간다)
 *          ISR_ON_TASK_STACK 면 isr_nest_bytes(isrs, n, fpu, 1)
 *   raw  = path + max(ctx, isr)       (중첩 ISR 이 다 끝나야 PendSV 가 돈다 → 둘은 동시에 쌓이지 않는다)
 *   total = apply_margin(raw), words = total / 4  (Cortex-M4 의 StackType_t = uint32_t) */
static int task_budget(const graph_t *g, const task_spec_t *t, const isr_t *isrs, int nisr,
                       isr_model_t model, const margin_t *m, budget_t *b) {
    memset(b, 0, sizeof *b);
    if (worst_stack(g, t->root, &b->w) != 0) return -1;
    b->path = b->w.bytes;
    b->ctx = exc_frame_bytes(t->fpu, 1) + switch_frame_bytes(t->fpu);
    b->isr = model == ISR_ON_MSP ? 0 : isr_nest_bytes(isrs, nisr, t->fpu, 1);
    b->raw = b->path + (b->ctx > b->isr ? b->ctx : b->isr);
    b->total = apply_margin(b->raw, m);
    b->words = b->total / 4;
    return 0;
}

/* ======================= DATA — 실제 컴파일러 출력 (md 5. 풀이에 명령과 함께) =======================
 * SU_FW_M4 / EDGES_FW_M4  : bash measure_su.sh → /tmp/RTOS-M08_su/m4/fw_graph.{su,edges}   (clang 21, thumbv7em, -O2)
 * SU_FW_A64 / EDGES_FW_A64: 같은 스크립트의 arm64 빌드 (demo_paint_measure.c 가 실측하는 바로 그 객체)
 * SU_RTOS_M4 / EDGES_RTOS_M4: bash cm4/kernel_su.sh → FreeRTOS V11.3.2 tasks/queue/list/port.c + cm4/fw_tasks.c 중
 *   태스크 3개와 ISR 들에서 닿는 함수만 (경로 접두어 ~/.esn_scratch/FreeRTOS-Kernel/ 와 portable/GCC/ARM_CM4F/ 는 줄였다) */
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

static const char SU_RTOS_M4[] =
    "tasks.c:3890:vTaskSuspendAll\t0\tstatic\n"
    "tasks.c:4064:xTaskResumeAll\t40\tstatic\n"
    "tasks.c:2414:xTaskDelayUntil\t24\tstatic\n"
    "tasks.c:4831:xTaskIncrementTick\t36\tstatic\n"
    "tasks.c:4213:xTaskGetTickCount\t0\tstatic\n"
    "tasks.c:5215:vTaskSwitchContext\t0\tstatic\n"
    "tasks.c:5402:vTaskPlaceOnEventList\t24\tstatic\n"
    "tasks.c:5500:xTaskRemoveFromEventList\t16\tstatic\n"
    "tasks.c:5681:vTaskInternalSetTimeOutState\t0\tstatic\n"
    "tasks.c:5693:xTaskCheckForTimeOut\t16\tstatic\n"
    "tasks.c:5762:vTaskMissedYield\t0\tstatic\n"
    "tasks.c:6848:xTaskPriorityDisinherit\t16\tstatic\n"
    "tasks.c:7797:ulTaskGenericNotifyTake\t32\tstatic\n"
    "tasks.c:8315:vTaskGenericNotifyGiveFromISR\t24\tstatic\n"
    "queue.c:949:xQueueGenericSend\t48\tstatic\n"
    "queue.c:2499:prvUnlockQueue\t24\tstatic\n"
    "queue.c:1509:xQueueReceive\t48\tstatic\n"
    "list.c:139:vListInsert\t8\tstatic\n"
    "list.c:217:uxListRemove\t0\tstatic\n"
    "port.c:504:xPortPendSVHandler\t0\tstatic\n"
    "port.c:475:vPortEnterCritical\t0\tstatic\n"
    "port.c:492:vPortExitCritical\t0\tstatic\n"
    "port.c:560:xPortSysTickHandler\t8\tstatic\n"
    "port.c:851:vPortValidateInterruptPriority\t0\tstatic\n"
    "fw_tasks.c:14:ctrl_task\t8\tstatic\n"
    "fw_tasks.c:24:comms_task\t48\tstatic\n"
    "fw_tasks.c:36:rx_task\t0\tstatic\n"
    "fw_tasks.c:44:USART2_IRQHandler\t16\tstatic\n";

static const char EDGES_RTOS_M4[] =
    "comms_task comms_send_telemetry\ncomms_task xQueueReceive\nctrl_task control_step\n"
    "ctrl_task xQueueGenericSend\nctrl_task xTaskDelayUntil\nctrl_task xTaskGetTickCount\n"
    "prvUnlockQueue vPortEnterCritical\nprvUnlockQueue vPortExitCritical\nprvUnlockQueue vTaskMissedYield\n"
    "prvUnlockQueue xTaskRemoveFromEventList\nrx_task rx_parse_once\nrx_task ulTaskGenericNotifyTake\n"
    "ulTaskGenericNotifyTake uxListRemove\nulTaskGenericNotifyTake vListInsert\nulTaskGenericNotifyTake vPortEnterCritical\n"
    "ulTaskGenericNotifyTake vPortExitCritical\nulTaskGenericNotifyTake xTaskResumeAll\nUSART2_IRQHandler isr_uart_rx\n"
    "USART2_IRQHandler vTaskGenericNotifyGiveFromISR\nvTaskGenericNotifyGiveFromISR vPortValidateInterruptPriority\nvTaskPlaceOnEventList uxListRemove\n"
    "vTaskPlaceOnEventList vListInsert\nxPortPendSVHandler vTaskSwitchContext\nxPortSysTickHandler xTaskIncrementTick\n"
    "xQueueGenericSend __aeabi_memcpy\nxQueueGenericSend prvUnlockQueue\nxQueueGenericSend vPortEnterCritical\n"
    "xQueueGenericSend vPortExitCritical\nxQueueGenericSend vTaskInternalSetTimeOutState\nxQueueGenericSend vTaskPlaceOnEventList\n"
    "xQueueGenericSend vTaskSuspendAll\nxQueueGenericSend xTaskCheckForTimeOut\nxQueueGenericSend xTaskPriorityDisinherit\n"
    "xQueueGenericSend xTaskRemoveFromEventList\nxQueueGenericSend xTaskResumeAll\nxQueueReceive __aeabi_memcpy\n"
    "xQueueReceive prvUnlockQueue\nxQueueReceive vPortEnterCritical\nxQueueReceive vPortExitCritical\n"
    "xQueueReceive vTaskInternalSetTimeOutState\nxQueueReceive vTaskPlaceOnEventList\nxQueueReceive vTaskSuspendAll\n"
    "xQueueReceive xTaskCheckForTimeOut\nxQueueReceive xTaskRemoveFromEventList\nxQueueReceive xTaskResumeAll\n"
    "xTaskCheckForTimeOut vPortEnterCritical\nxTaskCheckForTimeOut vPortExitCritical\nxTaskDelayUntil uxListRemove\n"
    "xTaskDelayUntil vListInsert\nxTaskDelayUntil xTaskResumeAll\nxTaskPriorityDisinherit uxListRemove\n"
    "xTaskResumeAll vPortEnterCritical\nxTaskResumeAll vPortExitCritical\nxTaskResumeAll xTaskIncrementTick\n";

/* ======================= (주어짐) 간선 텍스트 · 도구 모드 ======================= */
/* "caller callee\n" 줄들을 제자리에서 잘라 edge_t 로 (text 는 수정된다). 간선 수 반환 */
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

static const char *const g_err_name[] = { "ok", "RECURSION", "UNKNOWN", "DYNAMIC" };

static void print_wcs(const char *root, const wcs_t *w) {
    if (w->err) { printf("  %-22s ERROR %s: %s\n", root, g_err_name[w->err], w->culprit); return; }
    printf("  %-22s %5d B  ", root, w->bytes);
    for (int i = 0; i < w->depth; i++) printf("%s%s", i ? " > " : "", w->path[i]);
    printf("\n");
}

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    if (b) b[n] = '\0';
    fclose(f);
    return b;
}

/* ./so --su a.su [--su b.su] --edges e.txt --root NAME [--root …] [--assume NAME=BYTES …] */
static int tool_main(int argc, char **argv) {
    static su_table_t su;
    static edge_t e[MAXE];
    static assume_t as[16];
    static char asname[16][NAMELEN];
    int ne = 0, nas = 0, rc = 0;
    const char *roots[32]; int nr = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (strcmp(argv[i], "--su") == 0) {
            char *t = slurp(argv[i + 1]);
            if (!t) return 2;
            int r = su_parse(t, &su);
            free(t);
            if (r < 0) { fprintf(stderr, "%s: bad line %d\n", argv[i + 1], -r); return 2; }
        } else if (strcmp(argv[i], "--edges") == 0) {
            char *t = slurp(argv[i + 1]);                  /* edge_t 가 이 버퍼를 가리킨다 → 해제 안 함 */
            if (!t) return 2;
            ne += edges_parse(t, e + ne, MAXE - ne);
        } else if (strcmp(argv[i], "--root") == 0 && nr < 32) {
            roots[nr++] = argv[i + 1];
        } else if (strcmp(argv[i], "--assume") == 0 && nas < 16) {
            const char *eq = strchr(argv[i + 1], '=');
            if (!eq || eq == argv[i + 1] || eq - argv[i + 1] >= NAMELEN) return 2;
            snprintf(asname[nas], NAMELEN, "%.*s", (int)(eq - argv[i + 1]), argv[i + 1]);
            as[nas].name = asname[nas]; as[nas].bytes = atoi(eq + 1); nas++;
        } else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }
    graph_t g = { &su, e, ne, as, nas };
    printf("tool: %d functions (%d dup), %d edges, %d assumed\n", su.n, su.dups, ne, nas);
    for (int i = 0; i < nr; i++) {
        wcs_t w;
        if (worst_stack(&g, roots[i], &w) != 0) rc = 1;
        print_wcs(roots[i], &w);
    }
    return rc;
}


/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total, g_verbose;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static su_table_t g_fw, g_a64, g_all, g_tmp;
static edge_t g_efw[MAXE], g_ea64[MAXE], g_eall[MAXE], g_etmp[64];
static int g_nefw, g_nea64, g_neall;
static char g_ebuf[4][4096];

static int load_edges(const char *txt, char *buf, edge_t *e, int max) {
    snprintf(buf, 4096, "%s", txt);
    return edges_parse(buf, e, max);
}
static int path_is(const wcs_t *w, const char *const *want, int n) {
    if (w->depth != n) return 0;
    for (int i = 0; i < n; i++) if (strcmp(w->path[i], want[i]) != 0) return 0;
    return 1;
}
static const assume_t AS_M4[]  = { { "__aeabi_memcpy", 0 } };                  /* newlib v7e-m+fp memcpy: push 없음 (md) */
static const assume_t AS_A64[] = { { "memcpy", 0 }, { "__stack_chk_fail", 0 } }; /* 실패 경로는 예산에서 제외 */

/* 작은 그래프를 텍스트로 만들어 돌린다 (Part A 의 합성 케이스용) */
static int run_small(const char *su_txt, const char *edges, const assume_t *as, int nas, const char *root, wcs_t *w) {
    memset(&g_tmp, 0, sizeof g_tmp);
    if (su_parse(su_txt, &g_tmp) < 0) return -99;
    int ne = edges ? load_edges(edges, g_ebuf[3], g_etmp, 64) : 0;
    graph_t g = { &g_tmp, g_etmp, ne, as, nas };
    return worst_stack(&g, root, w);
}

static uint32_t g_rng = 0x2026111Bu;
static uint32_t xorshift32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

/* 무작위 DAG 대조: 노드 i → j (i < j) 만 → 사이클 없음. 정답 = 모든 경로를 다 펼친 최댓값 (지수 시간, n=10 이라 OK) */
static int bf_adj[10][10], bf_self[10];
static int brute(int u) {
    int best = 0;
    for (int v = 0; v < 10; v++) if (bf_adj[u][v]) { int t = brute(v); if (t > best) best = t; }
    return bf_self[u] + best;
}
static void fuzz_dag(int rounds, int *bad, int *path_bad) {
    static char names[10][8], sutxt[512], etxt[1024];
    for (int r = 0; r < rounds; r++) {
        size_t o = 0, oe = 0;
        for (int i = 0; i < 10; i++) {
            snprintf(names[i], sizeof names[i], "f%d", i);
            bf_self[i] = (int)(xorshift32() % 17) * 4;
            o += (size_t)snprintf(sutxt + o, sizeof sutxt - o, "r.c:%d:f%d\t%d\tstatic\n", i + 1, i, bf_self[i]);
        }
        for (int i = 0; i < 10; i++)
            for (int j = 0; j < 10; j++) {
                bf_adj[i][j] = (i < j) && (xorshift32() % 100 < 30);
                if (bf_adj[i][j]) oe += (size_t)snprintf(etxt + oe, sizeof etxt - oe, "f%d f%d\n", i, j);
            }
        wcs_t w;
        if (run_small(sutxt, oe ? etxt : NULL, NULL, 0, "f0", &w) != 0 || w.bytes != brute(0)) { (*bad)++; continue; }
        int sum = 0, ok = w.depth > 0 && strcmp(w.path[0], "f0") == 0;   /* 경로가 간선을 따라가고 합이 bytes 인가 */
        for (int i = 0; ok && i < w.depth; i++) {
            int u = atoi(w.path[i] + 1);
            sum += bf_self[u];
            if (i + 1 < w.depth && !bf_adj[u][atoi(w.path[i + 1] + 1)]) ok = 0;
        }
        if (!ok || sum != w.bytes) (*path_bad)++;
    }
}

/* Part C 시나리오: STM32F4 급 비행 제어기 — 태스크 3개, ISR 3 레벨 (+ PendSV 는 SysTick 과 같은 최저 레벨) */
static const task_spec_t TASKS[] = {
    { "ctrl",  "ctrl_task",  1 },     /* 1 kHz 자세 제어: pid_step 등 float → FP 문맥 */
    { "comms", "comms_task", 1 },     /* float 곱셈 하나(pid[i].prev * 1000.0f) 때문에 FP 문맥이 생긴다 */
    { "rx",    "rx_task",    0 },     /* 정수만 */
};
static isr_t ISRS[] = {               /* path 는 아래 isr_paths() 가 .su 로 채운다 */
    { "USART2_IRQHandler",   5, 0, 0 },   /* NVIC 5 = configMAX_SYSCALL_INTERRUPT_PRIORITY (FromISR API 사용 가능 최상) */
    { "isr_adc_dma",         6, 0, 0 },
    { "xPortSysTickHandler", 15, 0, 0 },  /* configKERNEL_INTERRUPT_PRIORITY = 최저 */
    { "xPortPendSVHandler",  15, 0, 0 },
};
#define NISR ((int)(sizeof ISRS / sizeof ISRS[0]))
static const margin_t MARGIN = { 25, 64, 8 };

static void isr_paths(const graph_t *g) {
    for (int i = 0; i < NISR; i++) { wcs_t w; ISRS[i].path = worst_stack(g, ISRS[i].name, &w) == 0 ? w.bytes : -1; }
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "-v") != 0) return tool_main(argc, argv);
    g_verbose = argc > 1;

    /* ---------- Part A: 파서 ---------- */
    su_entry_t x;
    CHECK(su_parse_line("fw_graph.c:31:spi_burst_read\t20\tstatic", &x) == 0 &&
          strcmp(x.name, "spi_burst_read") == 0 && x.bytes == 20 && x.qual == SU_STATIC);
    CHECK(su_parse_line("fw_graph.c:34:15:spi_burst_read\t4\tstatic\n", &x) == 0 &&
          strcmp(x.name, "spi_burst_read") == 0 && x.bytes == 4);                        /* gcc: 열 번호 */
    CHECK(su_parse_line("drv.cpp:12:7:int Spi::xfer(uint8_t*, int)\t48\tdynamic,bounded", &x) == 0 &&
          strcmp(x.name, "int Spi::xfer(uint8_t*, int)") == 0 && x.bytes == 48 && x.qual == SU_BOUNDED);
    CHECK(su_parse_line("k.c:9:vla_user\t32\tdynamic\r\n", &x) == 0 && x.qual == SU_DYNAMIC && x.bytes == 32);
    CHECK(su_parse_line("main.c:3:main\t0\tstatic", &x) == 0 && x.bytes == 0);
    CHECK(su_parse_line("no tab at all", &x) == -1);
    CHECK(su_parse_line("a.c:1:f\t\tstatic", &x) == -1);
    CHECK(su_parse_line("a.c:1:f\t12a\tstatic", &x) == -1);
    CHECK(su_parse_line("a.c:1:f\t-4\tstatic", &x) == -1);
    CHECK(su_parse_line("a.c:1:f\t12\tStatic", &x) == -1);
    CHECK(su_parse_line("a.c:1:f\t12", &x) == -1);
    CHECK(su_parse_line("a.c:1:\t12\tstatic", &x) == -1);                               /* 빈 이름 */
    CHECK(su_parse_line("a.c:x:f\t12\tstatic", &x) == -1);                              /* 줄 번호 아님 */
    CHECK(su_parse_line("a.c:1:a_function_name_that_is_far_too_long_for_the_name_table_of_this_tool\t8\tstatic", &x) == -1);
    CHECK(su_parse(SU_FW_M4, &g_fw) == 20 && g_fw.dups == 0);
    CHECK(su_parse(SU_FW_A64, &g_a64) == 20);
    memset(&g_tmp, 0, sizeof g_tmp);
    CHECK(su_parse("a.c:1:helper\t16\tstatic\nb.c:7:helper\t24\tstatic\n", &g_tmp) == 1 &&
          g_tmp.dups == 1 && g_tmp.e[0].bytes == 24);                                  /* 같은 이름 → 큰 값 */
    memset(&g_tmp, 0, sizeof g_tmp);
    CHECK(su_parse("a.c:1:f\t8\tstatic\n\na.c:3:g\tx\tstatic\n", &g_tmp) == -3);       /* 3번째 줄이 잘못 */

    /* ---------- Part A: 최악 경로 ---------- */
    g_nefw = load_edges(EDGES_FW_M4, g_ebuf[0], g_efw, MAXE);
    g_nea64 = load_edges(EDGES_FW_A64, g_ebuf[1], g_ea64, MAXE);
    CHECK(g_nefw == 17 && g_nea64 == 24);
    graph_t gm_noas = { &g_fw, g_efw, g_nefw, NULL, 0 };
    graph_t gm = { &g_fw, g_efw, g_nefw, AS_M4, 1 };
    graph_t ga = { &g_a64, g_ea64, g_nea64, AS_A64, 2 };
    wcs_t w;
    CHECK(worst_stack(&gm_noas, "ctrl_task_once", &w) == -1 && w.err == WCS_UNKNOWN &&
          strcmp(w.culprit, "__aeabi_memcpy") == 0);              /* 컴파일러가 넣은 memcpy 호출: .su 없음 */
    CHECK(worst_stack(&gm, "ctrl_task_once", &w) == 0 && w.bytes == 276);
    static const char *const P_CTRL[] = { "ctrl_task_once", "control_step", "log_printf", "log_vformat", "fmt_u32" };
    CHECK(path_is(&w, P_CTRL, 5));
    CHECK(worst_stack(&gm, "comms_task_once", &w) == 0 && w.bytes == 152 && strcmp(w.path[w.depth - 1], "cobs_encode") == 0);
    CHECK(worst_stack(&gm, "rx_parse_once", &w) == 0 && w.bytes == 72);
    CHECK(worst_stack(&gm, "isr_adc_dma", &w) == 0 && w.bytes == 96);
    CHECK(worst_stack(&gm, "imu_read", &w) == 0 && w.bytes == 52);
    CHECK(worst_stack(&gm, "isr_uart_rx", &w) == 0 && w.bytes == 0 && w.depth == 1);
    CHECK(worst_stack(&gm, "fmt_u32_rec", &w) == -1 && w.err == WCS_RECURSION && strcmp(w.culprit, "fmt_u32_rec") == 0);
    CHECK(worst_stack(&gm, "no_such_fn", &w) == -1 && w.err == WCS_UNKNOWN);
    CHECK(worst_stack(&ga, "ctrl_task_once", &w) == 0 && w.bytes == 400 && w.depth == 6 &&
          strcmp(w.path[4], "fmt_u32") == 0 && strcmp(w.path[5], "__stack_chk_fail") == 0);   /* arm64: 카나리 실패 함수(0 B 가정) */
    CHECK(worst_stack(&ga, "comms_task_once", &w) == 0 && w.bytes == 128);
    CHECK(worst_stack(&ga, "isr_adc_dma", &w) == 0 && w.bytes == 80);

    const char *SMALL = "s.c:1:a\t8\tstatic\ns.c:2:b\t16\tstatic\ns.c:3:c\t24\tstatic\ns.c:4:d\t40\tstatic\n";
    CHECK(run_small(SMALL, "a b\nb c\nc a\n", NULL, 0, "a", &w) == -1 && w.err == WCS_RECURSION &&
          strcmp(w.culprit, "a") == 0);                           /* 상호 재귀 a→b→c→a */
    CHECK(run_small(SMALL, "a b\na c\nb d\nc d\n", NULL, 0, "a", &w) == 0 && w.bytes == 8 + 24 + 40);   /* 다이아몬드 */
    CHECK(run_small(SMALL, "a b\na c\n", NULL, 0, "b", &w) == 0 && w.bytes == 16);                       /* 루트만 */
    CHECK(run_small("x.c:1:loop\t16\tstatic\nx.c:5:vla_fn\t40\tdynamic\n", "loop vla_fn\n", NULL, 0, "loop", &w) == -1 &&
          w.err == WCS_DYNAMIC && strcmp(w.culprit, "vla_fn") == 0);
    CHECK(run_small("x.c:1:loop\t16\tstatic\nx.c:5:vla_fn\t40\tdynamic,bounded\n", "loop vla_fn\n", NULL, 0, "loop", &w) == 0 &&
          w.bytes == 56);
    CHECK(run_small(SMALL, "a b\nb *INDIRECT*\n", NULL, 0, "a", &w) == -1 && w.err == WCS_UNKNOWN &&
          strcmp(w.culprit, "*INDIRECT*") == 0);                  /* 함수 포인터: 사람이 후보를 적어야 */
    int bad = 0, pbad = 0;
    fuzz_dag(300, &bad, &pbad);
    CHECK(bad == 0);
    CHECK(pbad == 0);

    /* ---------- Part B: 프레임 ---------- */
    CHECK(exc_frame_bytes(0, 0) == 32 && exc_frame_bytes(1, 0) == 104);
    CHECK(exc_frame_bytes(0, 1) == 36 && exc_frame_bytes(1, 1) == 108);
    CHECK(switch_frame_bytes(0) == 36 && switch_frame_bytes(1) == 100);
    CHECK(exc_frame_bytes(0, 1) + switch_frame_bytes(0) == 18 * 4);   /* pxPortInitialiseStack 이 만드는 첫 프레임 18 워드 */
    CHECK(isr_nest_bytes(NULL, 0, 1, 1) == 0);
    isr_t one[] = { { "u", 5, 40, 0 } };
    CHECK(isr_nest_bytes(one, 1, 0, 0) == 72 && isr_nest_bytes(one, 1, 1, 0) == 144);
    isr_t same[] = { { "u", 5, 40, 0 }, { "a", 5, 96, 0 } };
    CHECK(isr_nest_bytes(same, 2, 0, 0) == 32 + 96);                 /* 같은 우선순위는 서로 선점 못 함 */
    isr_t dp[] = { { "A", 10, 40, 0 }, { "B", 10, 0, 1 }, { "C", 5, 0, 0 } };
    CHECK(isr_nest_bytes(dp, 3, 0, 0) == 32 + 0 + 104 + 0);          /* path 큰 A(104) 보다 FP 쓰는 B(136) 가 최악 */
    isr_t dp2[] = { { "C", 5, 0, 0 }, { "B", 10, 0, 1 }, { "A", 10, 40, 0 } };
    CHECK(isr_nest_bytes(dp2, 3, 0, 0) == 136);                      /* 배열 순서 무관 */

    /* ---------- Part C: 태스크 예산 ---------- */
    g_all = g_fw;                                                    /* fw_graph.su + 커널/태스크 .su 합치기 */
    CHECK(su_parse(SU_RTOS_M4, &g_all) == 48 && g_all.dups == 0);
    g_neall = load_edges(EDGES_FW_M4, g_ebuf[2], g_eall, MAXE);
    static char ebuf_rtos[4096];
    snprintf(ebuf_rtos, sizeof ebuf_rtos, "%s", EDGES_RTOS_M4);
    g_neall += edges_parse(ebuf_rtos, g_eall + g_neall, MAXE - g_neall);
    CHECK(g_neall == 17 + 54);
    graph_t gr = { &g_all, g_eall, g_neall, AS_M4, 1 };
    isr_paths(&gr);
    CHECK(ISRS[0].path == 40 && ISRS[1].path == 96 && ISRS[2].path == 44 && ISRS[3].path == 0);
    CHECK(worst_stack(&gr, "xQueueGenericSend", &w) == 0 && w.bytes == 124 &&
          strcmp(w.path[w.depth - 1], "xTaskIncrementTick") == 0);   /* 밀린 tick 처리가 태스크 스택에서! */
    CHECK(apply_margin(492, &MARGIN) == 616 && apply_margin(100, &MARGIN) == 168 && apply_margin(0, &MARGIN) == 64);
    margin_t m10 = { 10, 0, 4 };
    CHECK(apply_margin(101, &m10) == 112);                           /* ceil(10.1) = 11 → 112 (이미 4 의 배수) */
    budget_t b[3], bt[3];
    int rc = 0;
    for (int i = 0; i < 3; i++) {
        rc |= task_budget(&gr, &TASKS[i], ISRS, NISR, ISR_ON_MSP, &MARGIN, &b[i]);
        rc |= task_budget(&gr, &TASKS[i], ISRS, NISR, ISR_ON_TASK_STACK, &MARGIN, &bt[i]);
    }
    CHECK(rc == 0);
    CHECK(b[0].path == 284 && b[0].ctx == 208 && b[0].isr == 0 && b[0].raw == 492 && b[0].total == 616 && b[0].words == 154);
    CHECK(b[1].path == 200 && b[1].raw == 408 && b[1].total == 512 && b[1].words == 128);
    CHECK(b[2].path == 108 && b[2].ctx == 72 && b[2].raw == 180 && b[2].total == 248 && b[2].words == 62);
    CHECK(bt[0].isr == 360 && bt[0].raw == 644 && bt[0].total == 808 && bt[0].words == 202);
    CHECK(bt[1].raw == 560 && bt[1].words == 176);
    CHECK(bt[2].isr == 288 && bt[2].raw == 396 && bt[2].words == 124);
    isr_t more[NISR + 1];
    memcpy(more, ISRS, sizeof ISRS);
    more[NISR] = (isr_t){ "TIM1_BRK_IRQHandler", 1, 24, 0 };        /* 모터 브레이크 폴트: 시스템콜 상한 위 */
    budget_t bm, btm;
    task_budget(&gr, &TASKS[0], more, NISR + 1, ISR_ON_MSP, &MARGIN, &bm);
    task_budget(&gr, &TASKS[0], more, NISR + 1, ISR_ON_TASK_STACK, &MARGIN, &btm);
    CHECK(bm.raw == b[0].raw);                                       /* MSP 모델: ISR 이 늘어도 태스크 스택은 그대로 */
    CHECK(btm.raw == bt[0].raw + 36 + 24);                           /* 태스크 스택 모델: 레벨 하나당 프레임 + 핸들러 */
    graph_t gr_noas = { &g_all, g_eall, g_neall, NULL, 0 };
    CHECK(task_budget(&gr_noas, &TASKS[0], ISRS, NISR, ISR_ON_MSP, &MARGIN, &bm) == -1 && bm.w.err == WCS_UNKNOWN);

    if (g_verbose) {
        printf("== Part A: worst path per root (Cortex-M4 clang -O2 | arm64 clang -O2)\n");
        const char *roots[] = { "imu_read", "log_printf", "ctrl_task_once", "comms_task_once", "rx_parse_once", "isr_adc_dma", "fmt_u32_rec" };
        for (int i = 0; i < 7; i++) {
            wcs_t a, c;
            worst_stack(&gm, roots[i], &a); worst_stack(&ga, roots[i], &c);
            printf("  %-16s M4 %4d B %-10s arm64 %4d B   ", roots[i], a.err ? -1 : a.bytes, a.err ? g_err_name[a.err] : "",
                   c.err ? -1 : c.bytes);
            for (int k = 0; k < a.depth; k++) printf("%s%s", k ? " > " : "", a.path[k]);
            printf("\n");
        }
        printf("== Part C: ISR paths (MSP) ");
        for (int i = 0; i < NISR; i++) printf(" %s(prio %d)=%d", ISRS[i].name, ISRS[i].nvic_prio, ISRS[i].path);
        printf("\n== Part C: task stack budget, margin +%d%% (min %d B), align %d\n", MARGIN.pct, MARGIN.min_bytes, MARGIN.align);
        printf("  %-6s %3s %5s %5s | %-22s | %-26s\n", "task", "fpu", "path", "ctx", "ISR on MSP: raw total w", "ISR on task stack: isr raw total w");
        for (int i = 0; i < 3; i++)
            printf("  %-6s %3d %5d %5d | %5d %5d %5d        | %5d %5d %5d %5d\n", TASKS[i].name, TASKS[i].fpu, b[i].path, b[i].ctx,
                   b[i].raw, b[i].total, b[i].words, bt[i].isr, bt[i].raw, bt[i].total, bt[i].words);
        for (int i = 0; i < 3; i++) { printf("  %-6s path: ", TASKS[i].name);
            for (int k = 0; k < b[i].w.depth; k++) printf("%s%s", k ? " > " : "", b[i].w.path[k]); printf("\n"); }
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
