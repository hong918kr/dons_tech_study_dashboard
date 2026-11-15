/* RTOS-A06 RTOS 트레이싱: 훅으로 이벤트 기록 → 타임라인
 *
 * SEGGER SystemView / Percepio Tracealyzer 가 하는 일을 세 조각으로 줄여 직접 만든다.
 *   Part A  타깃 쪽 레코더: 커널 훅(traceTASK_SWITCHED_IN 등)이 부르는 tr_record() 가 고정 크기 레코드를
 *           링 버퍼에 쓴다. 단일 writer. 모드 2개 — 덮어쓰기(flight recorder: 마지막 N개만 남김) /
 *           가득 차면 멈춤(streaming: 리더가 빼 가는 동안 넘치면 버리고 DROP 마커를 남김).
 *           순번(seq)은 "시도한" 이벤트마다 1씩 → 어디서 몇 개를 잃었는지 호스트가 안다.
 *           tx_record() 는 링에서 꺼낸 레코드를 공유 텍스트 포맷으로 바꾸고, 순번 구멍을 DROP 줄로 적는다.
 *   Part B  호스트 쪽 디코더: 텍스트만 보고 태스크별 실행 구간·CPU 시간을 SWITCH_IN 으로 재구성한다.
 *           32비트 µs 타임스탬프 wrap, 첫 이벤트 없음(중간부터 시작), DROP(구멍)을 처리 — 모르는 시간은
 *           아무 태스크에도 주지 않고 unknown 으로 따로 센다. ISR 시간은 ISR 몫, 깔린 태스크는 stolen.
 *   Part C  STM32 모터 제어기 모양의 결정적 시뮬레이터(주어진 코드)가 이벤트를 만들고, 1 MB/s 급 UART
 *           대역폭으로 스트리밍하거나 flight recorder 로 덤프한다. 디코딩 결과를 시뮬레이터의 정답(누가 언제
 *           돌았나)과 µs 단위로 대조하고, 주기 태스크의 응답 시간(RELEASE → BLOCK delay)을 트레이스만으로 잰다.
 *
 * 공유 포맷 (A09/A10 도 쓴다): 첫 줄 "# rtos-trace v1 unit=us", 한 줄에 이벤트 하나
 *   <t_us> <EVENT> <name> [arg]     t_us = unsigned 32-bit (wrap), '#' 줄은 주석
 *
 * 빌드:  cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o sol && ./sol
 *        ./sol --dump (C-1 트레이스) / ./sol --dump-lossy (C-2: 링 8 + 300 kB/s, DROP 포함) → timeline.py 입력
 * demo_freertos_trace.c 가 -DA06_LIB_ONLY 로 이 파일을 #include 해 Part A/B 를 진짜 FreeRTOS 안에서 쓴다.
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================= 공유 포맷: 이벤트 종류 ======================= */
enum { EV_RELEASE, EV_SWITCH_IN, EV_SWITCH_OUT, EV_BLOCK, EV_UNBLOCK, EV_ISR_ENTER, EV_ISR_EXIT,
       EV_MUTEX_TAKE, EV_MUTEX_GIVE, EV_Q_SEND_BLOCK, EV_Q_RECV_BLOCK, EV_DEADLINE_MISS, EV_DROP, EV_COUNT };
static const char *const EV_NAME[EV_COUNT] = {
    "RELEASE", "SWITCH_IN", "SWITCH_OUT", "BLOCK", "UNBLOCK", "ISR_ENTER", "ISR_EXIT",
    "MUTEX_TAKE", "MUTEX_GIVE", "Q_SEND_BLOCK", "Q_RECV_BLOCK", "DEADLINE_MISS", "DROP" };
enum { ARG_NONE, ARG_NAME, ARG_NUM };         /* arg 가 없음 / 이름표 번호 / 숫자 */

/* ======================= Part A: 타깃 쪽 레코더 ======================= */
typedef struct {            /* 레코드 1개 = 16바이트. 진짜 도구는 델타 타임스탬프·가변 길이로 줄인다 (추가 연습 2) */
    uint32_t seq;           /* 논리 이벤트 번호: tr_record 를 "부른" 횟수. 버린 것도 번호를 먹는다 */
    uint32_t t_us;          /* 32비트 µs 타임스탬프 (71.6 분마다 wrap) */
    uint32_t arg;           /* argk 에 따라 이름표 번호 또는 숫자 (DROP 이면 잃은 개수) */
    uint8_t ev, id, argk, pad;   /* id = 태스크/ISR 이름표 번호 */
} trace_rec_t;

typedef enum { TR_OVERWRITE, TR_STOP_WHEN_FULL } tr_mode_t;
typedef struct {
    trace_rec_t *buf;
    uint32_t cap, mask;     /* cap 은 2의 거듭제곱 → index = counter & mask (u32 카운터가 wrap 해도 맞다) */
    uint32_t wr, rd;        /* 자유 증가 카운터. 들어 있는 개수 = wr - rd */
    uint32_t next_seq;      /* 다음 이벤트에 줄 순번 */
    uint32_t dropped;       /* 지금까지 잃은 이벤트 수 (덮어씀 + 거절) */
    uint32_t pend_drop;     /* STOP 모드: 마지막 저장 이후 거절했지만 아직 DROP 마커를 못 쓴 수 */
    uint32_t pend_seq, pend_t;   /* 그 첫 거절 이벤트의 순번과 시각 */
    tr_mode_t mode;
} trace_ring_t;

static bool tr_init(trace_ring_t *r, trace_rec_t *storage, uint32_t cap, tr_mode_t mode) {
    if (cap == 0 || (cap & (cap - 1u)) != 0) return false;   /* 2의 거듭제곱만 */
    memset(r, 0, sizeof *r);
    r->buf = storage;
    r->cap = cap;
    r->mask = cap - 1u;
    r->mode = mode;
    return true;
}
static void tr_put(trace_ring_t *r, uint32_t seq, uint32_t t, uint8_t ev, uint8_t id, uint8_t argk, uint32_t arg) {
    trace_rec_t *p = &r->buf[r->wr & r->mask];
    p->seq = seq; p->t_us = t; p->arg = arg; p->ev = ev; p->id = id; p->argk = argk; p->pad = 0;
    r->wr++;
}

/* TODO A1 — 이벤트 1개 기록. 저장했으면 true, 버렸으면 false.
 *  - 순번: 부를 때마다 next_seq 를 하나 쓴다 (버려도).
 *  - TR_OVERWRITE: 가득 차면 가장 오래된 것을 밀어낸다 (rd++, dropped++). 항상 true.
 *  - TR_STOP_WHEN_FULL: 새 것을 버린다 (dropped++, pend_drop++; 첫 번째면 pend_seq/pend_t 기억).
 *    다음에 자리가 나면 먼저 DROP 마커 (seq=pend_seq, t=pend_t, ev=EV_DROP, argk=ARG_NUM, arg=pend_drop) 를
 *    쓰고 이벤트를 쓴다. 마커 + 이벤트 = 2칸이 필요하다 — 1칸뿐이면 이번 것도 버린다. */
static bool tr_record(trace_ring_t *r, uint32_t t, uint8_t ev, uint8_t id, uint8_t argk, uint32_t arg) {
    (void)r; (void)t; (void)ev; (void)id; (void)argk; (void)arg;
    return false; /* TODO A1 */
}
/* 리더 1개: 가장 오래된 레코드를 꺼낸다 (STOP 모드 스트리밍 / 정지 후 덤프). */
static bool tr_pop(trace_ring_t *r, trace_rec_t *out) {
    if (r->wr == r->rd) return false;
    *out = r->buf[r->rd & r->mask];
    r->rd++;
    return true;
}

/* ---------- 레코드 → 공유 텍스트 포맷 (호스트 쪽 "수신기") ---------- */
typedef struct {
    char *out; size_t cap, len; bool overflow;
    const char *const *names; uint32_t nnames;   /* 이름표: id → "Ctrl" 등 */
    uint32_t exp_seq;       /* 다음에 와야 할 순번 */
    uint32_t last_t;        /* 마지막으로 적은 줄의 시각 */
    bool any;               /* 한 줄이라도 적었나 */
} tr_text_t;

static void tx_printf(tr_text_t *x, const char *fmt, ...) {
    if (x->overflow) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(x->out + x->len, x->cap - x->len, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= x->cap - x->len) { x->overflow = true; x->out[x->len] = '\0'; return; }
    x->len += (size_t)n;
}
static void tx_begin(tr_text_t *x, char *out, size_t cap, const char *const *names, uint32_t nnames) {
    memset(x, 0, sizeof *x);
    x->out = out; x->cap = cap; x->names = names; x->nnames = nnames;
    out[0] = '\0';
    tx_printf(x, "# rtos-trace v1 unit=us\n");
}
static const char *tx_name(const tr_text_t *x, uint32_t id) { return id < x->nnames ? x->names[id] : "UNKNOWN"; }
static void tx_line(tr_text_t *x, uint32_t t, int ev, const char *name, int argk, uint32_t arg) {
    if (argk == ARG_NAME)     tx_printf(x, "%u %s %s %s\n", (unsigned)t, EV_NAME[ev], name, tx_name(x, arg));
    else if (argk == ARG_NUM) tx_printf(x, "%u %s %s %u\n", (unsigned)t, EV_NAME[ev], name, (unsigned)arg);
    else                      tx_printf(x, "%u %s %s\n", (unsigned)t, EV_NAME[ev], name);
}

/* TODO A2 — 레코드 1개를 줄로 적는다.
 *  - EV_DROP 마커: "t DROP TRACE n" 그대로 적고, 다음 기대 순번 = 마커 seq + n.
 *  - 순번 구멍 (r->seq != exp_seq: 덮어쓰기로 앞을 잃음 / 전송 중 손실): 이벤트 줄 앞에
 *    "t DROP TRACE (r->seq - exp_seq)" 을 적는다. t 는 직전 줄 시각(last_t) — 언제 잃었는지 모르므로
 *    "마지막으로 확실한 시각" 부터 모름. 아직 한 줄도 없으면(앞부분 손실) 이 레코드의 시각.
 *  - 그다음 이벤트 줄, exp_seq = r->seq + 1, last_t 갱신. */
static void tx_record(tr_text_t *x, const trace_rec_t *r) {
    (void)x; (void)r; /* TODO A2 */
}
/* TODO A2 (계속) — 끝맺음: writer 가 준 순번(next_seq)까지 못 받은 꼬리가 있으면 DROP 한 줄.
 * 시각은 STOP 모드에서 마커를 못 쓴 채 끝났으면 pend_t (첫 손실 시각을 정확히 앎), 아니면 last_t. */
static void tx_end(tr_text_t *x, const trace_ring_t *r) {
    (void)x; (void)r; /* TODO A2 */
}

/* ======================= Part B: 호스트 쪽 디코더 ======================= */
#define DEC_MAXCTX  24
#define DEC_MAXIV   8192
#define DEC_MAXRESP 64
#define DEC_MAXNEST 8
#define DEC_NAMELEN 15
enum { DEC_OK, DEC_EHEADER, DEC_ESYNTAX, DEC_EEVENT, DEC_EBACKWARDS, DEC_EFULL, DEC_ENEST };

typedef struct {
    char name[DEC_NAMELEN + 1];
    bool is_isr;
    uint64_t run_us;        /* 태스크: 자기 코드 실행 (ISR 시간 제외) / ISR: ISR 자신 */
    uint64_t stolen_us;     /* 태스크: 이 태스크 위에서 ISR 이 쓴 시간 */
    uint32_t n_in;          /* 실제로 실행 주체가 바뀐 SWITCH_IN 수 (ISR 은 ISR_ENTER 수) */
    /* Part C: 응답 시간 (RELEASE → BLOCK delay) */
    bool job_open; uint64_t rel_t;
    uint32_t n_resp, n_resp_lost, n_overlap, max_resp;
    uint32_t resp[DEC_MAXRESP];
} dec_ctx_t;
typedef struct { int16_t ctx; uint64_t t0, t1; } dec_iv_t;   /* ctx -1 = 모름 (구멍) */

typedef struct {
    dec_ctx_t c[DEC_MAXCTX]; int nc;
    dec_iv_t iv[DEC_MAXIV]; int niv; bool iv_full;
    bool have_t; uint32_t t0_32, last32; uint64_t now; uint32_t wraps;   /* now = 첫 이벤트 기준 µs (wrap 풀림) */
    int task;                          /* 지금 CPU 의 주인 태스크 (모르면 -1) */
    int isr[DEC_MAXNEST]; int depth;   /* 그 위에 쌓인 ISR (중첩) */
    int seg_owner; uint64_t seg_t0;    /* 타임라인 구간을 만드는 중인 주인과 시작 시각 */
    uint64_t unknown_us, span_us;
    uint32_t n_events, n_switches, n_drop_lines, n_dropped, n_mismatch, n_miss;
    int err, err_line;
} dec_t;

static int dec_owner(const dec_t *d) { return d->depth > 0 ? d->isr[d->depth - 1] : d->task; }

static int dec_intern(dec_t *d, const char *name) {
    for (int i = 0; i < d->nc; i++) if (strcmp(d->c[i].name, name) == 0) return i;
    if (d->nc == DEC_MAXCTX) return -1;
    dec_ctx_t *c = &d->c[d->nc];
    memset(c, 0, sizeof *c);
    snprintf(c->name, sizeof c->name, "%s", name);
    return d->nc++;
}
static int dec_find(const dec_t *d, const char *name) {
    for (int i = 0; i < d->nc; i++) if (strcmp(d->c[i].name, name) == 0) return i;
    return -1;
}

/* TODO B1 — 32비트 시각을 첫 이벤트 기준 64비트 µs 로 푼다.
 *  첫 호출: t0_32 = last32 = t32, *now = 0.
 *  이후: delta = (uint32_t)(t32 - last32). delta >= 2^31 이면 "시간이 거꾸로" → false.
 *        (= 연속 이벤트 간격 < 35.8 분이라는 가정. 그 안이면 wrap 은 자동으로 풀린다.)
 *        t32 < last32 (수치상 작아짐) 이면 wraps++. *now = d->now + delta. */
static bool dec_time(dec_t *d, uint32_t t32, uint64_t *now) {
    (void)d; (void)t32;
    *now = 0;
    return true; /* TODO B1 */
}

/* TODO B2 — d->now 에서 new_now 까지 흐른 시간을 지금 주인에게 준다.
 *  ISR 중이면(depth>0): 맨 위 ISR 의 run_us, 깔린 태스크를 알면 그 태스크의 stolen_us 에도.
 *  아니면 태스크를 알면 그 run_us, 모르면 unknown_us. 마지막에 d->now = new_now. */
static void dec_advance(dec_t *d, uint64_t new_now) {
    (void)d; (void)new_now; /* TODO B2 */
}

/* TODO B3 — 이벤트가 "누가 CPU 를 쓰나" 를 바꾼다. ctx = 이름표 번호 (DROP 이면 -1). false = 형식 오류.
 *  SWITCH_IN X : task != X 이면 n_switches++, c[X].n_in++. task = X.  (같은 태스크 재선택은 전환 아님)
 *  SWITCH_OUT X: 주인을 바꾸지 않는다 (다음 SWITCH_IN 까지 X 몫 = 커널 전환 비용 포함).
 *                task 를 아는데 X 가 아니면 n_mismatch++.
 *  ISR_ENTER I : 스택에 push (DEC_MAXNEST 초과면 false), c[I].is_isr = true, c[I].n_in++.
 *  ISR_EXIT I  : 맨 위가 I 면 pop, 아니면 (ENTER 를 잃음) n_mismatch++.
 *  DROP n      : n_drop_lines++, n_dropped += n, task = -1, depth = 0 → 다음 SWITCH_IN 까지 모름.
 *  DEADLINE_MISS: n_miss++.  나머지는 시간 귀속과 무관. */
static bool dec_apply(dec_t *d, int ev, int ctx, uint32_t num) {
    (void)d; (void)ev; (void)ctx; (void)num;
    return true; /* TODO B3 */
}

/* TODO C1 — 주기 태스크의 응답 시간. RELEASE X: 열린 job 이 없으면 job_open, rel_t = now
 *  (이미 열려 있으면 n_overlap++ 하고 옛 job 을 유지). BLOCK X delay|delay_until: 열린 job 이 있으면
 *  resp = now - rel_t 를 resp[] 에 (DEC_MAXRESP 까지) 넣고 n_resp++, max_resp 갱신, 닫는다.
 *  DROP: 열린 job 을 전부 닫고 n_resp_lost++ — 구멍을 건너는 응답 시간은 재지 않는다. */
static void dec_job(dec_t *d, int ev, int ctx, const char *arg) {
    (void)d; (void)ev; (void)ctx; (void)arg; /* TODO C1 */
}

/* 타임라인 구간: 주인이 바뀌면 이전 구간을 닫는다 (길이 0 은 버림). */
static void dec_segment(dec_t *d, bool final) {
    int o = dec_owner(d);
    if (!final && o == d->seg_owner) return;
    if (d->now > d->seg_t0) {
        if (d->niv < DEC_MAXIV) d->iv[d->niv++] = (dec_iv_t){ (int16_t)d->seg_owner, d->seg_t0, d->now };
        else d->iv_full = true;
    }
    d->seg_owner = o;
    d->seg_t0 = d->now;
}

static bool is_name(const char *s) {
    if (!*s || strlen(s) > DEC_NAMELEN) return false;
    for (; *s; s++)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_')) return false;
    return true;
}
static bool parse_u32(const char *s, uint32_t *v) {
    if (!*s) return false;
    uint64_t x = 0;
    for (; *s; s++) { if (*s < '0' || *s > '9') return false; x = x * 10u + (uint64_t)(*s - '0'); if (x > 0xFFFFFFFFu) return false; }
    *v = (uint32_t)x;
    return true;
}

/* 텍스트 전체를 디코드. 반환 DEC_OK 또는 오류 코드 (d->err_line = 줄 번호). */
static int trace_decode(const char *text, dec_t *d) {
    memset(d, 0, sizeof *d);
    d->task = -1; d->seg_owner = -1;
    int line = 0; bool header = false;
    const char *p = text;
    while (*p) {
        char buf[128];
        size_t n = strcspn(p, "\n");
        line++;
        if (n >= sizeof buf) { d->err_line = line; return d->err = DEC_ESYNTAX; }
        memcpy(buf, p, n); buf[n] = '\0';
        p += n; if (*p == '\n') p++;
        if (n && buf[n - 1] == '\r') buf[--n] = '\0';
        if (!header) {
            if (n == 0) continue;
            if (strcmp(buf, "# rtos-trace v1 unit=us") != 0) { d->err_line = line; return d->err = DEC_EHEADER; }
            header = true;
            continue;
        }
        if (n == 0 || buf[0] == '#') continue;
        char *tok[5]; int nt = 0;
        for (char *s = strtok(buf, " \t"); s && nt < 5; s = strtok(NULL, " \t")) tok[nt++] = s;
        uint32_t t32, num = 0;
        if (nt < 3 || nt > 4 || !parse_u32(tok[0], &t32) || !is_name(tok[2]) || (nt == 4 && !is_name(tok[3])))
            { d->err_line = line; return d->err = DEC_ESYNTAX; }
        int ev = -1;
        for (int i = 0; i < EV_COUNT; i++) if (strcmp(tok[1], EV_NAME[i]) == 0) ev = i;
        if (ev < 0) { d->err_line = line; return d->err = DEC_EEVENT; }
        if (ev == EV_DROP && (nt != 4 || !parse_u32(tok[3], &num))) { d->err_line = line; return d->err = DEC_ESYNTAX; }
        uint64_t now;
        if (!dec_time(d, t32, &now)) { d->err_line = line; return d->err = DEC_EBACKWARDS; }
        dec_advance(d, now);
        int ctx = -1;
        if (ev != EV_DROP && (ctx = dec_intern(d, tok[2])) < 0) { d->err_line = line; return d->err = DEC_EFULL; }
        if (!dec_apply(d, ev, ctx, num)) { d->err_line = line; return d->err = DEC_ENEST; }
        dec_job(d, ev, ctx, nt == 4 ? tok[3] : "");
        dec_segment(d, false);
        d->n_events++;
    }
    if (!header) return d->err = DEC_EHEADER;
    dec_segment(d, true);
    d->span_us = d->now;
    return DEC_OK;
}
/* CPU 점유율 (basis point: 10000 = 100 %), 반올림 */
static uint32_t dec_cpu_bp(const dec_t *d, int ctx) {
    return d->span_us ? (uint32_t)((d->c[ctx].run_us * 10000u + d->span_us / 2u) / d->span_us) : 0u;
}
static void dec_print_table(const dec_t *d) {
    printf("      %-8s %10s %8s %8s %6s %6s %9s\n", "ctx", "run_us", "stolen", "cpu%", "in", "resp#", "max_resp");
    for (int i = 0; i < d->nc; i++) {
        uint32_t bp = dec_cpu_bp(d, i);
        printf("      %-8s %10llu %8llu %5u.%02u %6u %6u %9u%s\n", d->c[i].name, (unsigned long long)d->c[i].run_us,
               (unsigned long long)d->c[i].stolen_us, bp / 100u, bp % 100u, d->c[i].n_in, d->c[i].n_resp,
               d->c[i].max_resp, d->c[i].is_isr ? "  (ISR)" : "");
    }
    printf("      span=%llu unknown=%llu wraps=%u switches=%u drop_lines=%u dropped=%u mismatch=%u intervals=%d\n",
           (unsigned long long)d->span_us, (unsigned long long)d->unknown_us, d->wraps, d->n_switches,
           d->n_drop_lines, d->n_dropped, d->n_mismatch, d->niv);
}

#ifndef A06_LIB_ONLY
/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
static bool g_quiet;
#define CHECK(e) do { g_total++; if (e) { g_pass++; if (!g_quiet) printf("PASS  %s\n", #e); } \
                      else printf("FAIL  %s   (line %d)\n", #e, __LINE__); } while (0)

static dec_t g_d;                          /* 디코더 상태는 크다 (구간 8192개) → 정적 */
static char g_text[1 << 20];
static trace_rec_t g_store[4096];

static void test_part_a(void) {
    printf("-- Part A: 레코더 링 버퍼 + 텍스트 내보내기\n");
    static const char *const NM[] = { "IDLE", "Ctrl", "Comm", "spi" };
    trace_ring_t r; trace_rec_t o;
    CHECK(!tr_init(&r, g_store, 0, TR_OVERWRITE) && !tr_init(&r, g_store, 12, TR_OVERWRITE));
    /* A-1 덮어쓰기: cap 4 에 6개 → 가장 오래된 2개가 밀려남, 순번 2..5 가 남는다 */
    tr_init(&r, g_store, 4, TR_OVERWRITE);
    for (uint32_t i = 0; i < 6; i++) tr_record(&r, 100u * i, EV_SWITCH_IN, 1, ARG_NONE, 0);
    CHECK(r.next_seq == 6 && r.dropped == 2 && r.wr - r.rd == 4);
    uint32_t seqs = 0;
    while (tr_pop(&r, &o)) seqs = seqs * 10u + o.seq;
    CHECK(seqs == 2345u);
    /* A-2 멈춤: cap 4 에 6개 → 0..3 저장, 2개 거절. 2개를 빼 간 뒤 7번째 → [마커(4, 2개), 6] */
    tr_init(&r, g_store, 4, TR_STOP_WHEN_FULL);
    int stored = 0;
    for (uint32_t i = 0; i < 6; i++) stored += tr_record(&r, 10u * i, EV_SWITCH_IN, 1, ARG_NONE, 0);
    CHECK(stored == 4 && r.dropped == 2 && r.pend_drop == 2 && r.pend_seq == 4 && r.pend_t == 40);
    tr_pop(&r, &o); tr_pop(&r, &o);
    CHECK(tr_record(&r, 60, EV_BLOCK, 1, ARG_NAME, 3) == true && r.pend_drop == 0);
    trace_rec_t got[4] = { { 0 } }; int ng = 0;
    while (ng < 4 && tr_pop(&r, &got[ng])) ng++;
    CHECK(ng == 4 && got[0].seq == 2 && got[1].seq == 3);
    CHECK(got[2].ev == EV_DROP && got[2].seq == 4 && got[2].t_us == 40 && got[2].arg == 2);
    CHECK(got[3].ev == EV_BLOCK && got[3].seq == 6 && got[3].t_us == 60);
    /* A-3 마커는 2칸이 필요: 1칸만 비었으면 이번 것도 버린다 (pend 3) */
    tr_init(&r, g_store, 4, TR_STOP_WHEN_FULL);
    for (uint32_t i = 0; i < 6; i++) tr_record(&r, i, EV_SWITCH_IN, 1, ARG_NONE, 0);
    tr_pop(&r, &o);
    CHECK(tr_record(&r, 6, EV_SWITCH_IN, 1, ARG_NONE, 0) == false && r.pend_drop == 3 && r.dropped == 3);
    /* A-4 u32 카운터 wrap 에서도 순서 유지 (index = counter & mask) */
    tr_init(&r, g_store, 4, TR_OVERWRITE);
    r.wr = r.rd = 0xFFFFFFFEu;
    for (uint32_t i = 0; i < 5; i++) tr_record(&r, i, EV_SWITCH_IN, 1, ARG_NONE, 0);
    seqs = 0;
    while (tr_pop(&r, &o)) seqs = seqs * 10u + o.seq;
    CHECK(seqs == 1234u && r.wr == 3u);
    /* A-5 텍스트: 마커 / 전송 손실(순번 구멍) / 꼬리 손실 */
    tr_text_t x;
    tx_begin(&x, g_text, sizeof g_text, NM, 4);
    trace_rec_t s1 = { 0, 100, 0, EV_SWITCH_IN, 1, ARG_NONE, 0 };
    trace_rec_t s2 = { 1, 150, 3, EV_MUTEX_TAKE, 1, ARG_NAME, 0 };
    trace_rec_t s3 = { 2, 200, 3, EV_DROP, 0, ARG_NUM, 0 };          /* 마커: 순번 2,3,4 잃음 */
    trace_rec_t s4 = { 5, 260, 0, EV_SWITCH_IN, 2, ARG_NONE, 0 };
    trace_rec_t s5 = { 8, 300, 0, EV_SWITCH_IN, 1, ARG_NONE, 0 };    /* 6,7 은 전송 중 사라짐 */
    tx_record(&x, &s1); tx_record(&x, &s2); tx_record(&x, &s3); tx_record(&x, &s4); tx_record(&x, &s5);
    trace_ring_t w; tr_init(&w, g_store, 4, TR_STOP_WHEN_FULL); w.next_seq = 10;   /* writer 는 10개를 냈다 */
    tx_end(&x, &w);
    const char *want =
        "# rtos-trace v1 unit=us\n100 SWITCH_IN Ctrl\n150 MUTEX_TAKE Ctrl spi\n200 DROP TRACE 3\n"
        "260 SWITCH_IN Comm\n260 DROP TRACE 2\n300 SWITCH_IN Ctrl\n300 DROP TRACE 1\n";
    CHECK(strcmp(g_text, want) == 0);
    if (strcmp(g_text, want) != 0) printf("%s", g_text);
    /* A-6 덮어쓰기 덤프: 앞부분 손실이 첫 줄 DROP 이 된다 (시각 = 남은 첫 레코드) */
    tr_init(&r, g_store, 2, TR_OVERWRITE);
    tr_record(&r, 10, EV_SWITCH_IN, 1, ARG_NONE, 0);
    tr_record(&r, 20, EV_SWITCH_IN, 2, ARG_NONE, 0);
    tr_record(&r, 30, EV_SWITCH_IN, 0, ARG_NONE, 0);
    tx_begin(&x, g_text, sizeof g_text, NM, 4);
    while (tr_pop(&r, &o)) tx_record(&x, &o);
    tx_end(&x, &r);
    CHECK(strcmp(g_text, "# rtos-trace v1 unit=us\n20 DROP TRACE 1\n20 SWITCH_IN Comm\n30 SWITCH_IN IDLE\n") == 0);
}

static uint64_t run_of(const dec_t *d, const char *n) { int i = dec_find(d, n); return i < 0 ? 0 : d->c[i].run_us; }
static uint64_t stolen_of(const dec_t *d, const char *n) { int i = dec_find(d, n); return i < 0 ? 0 : d->c[i].stolen_us; }
static const dec_ctx_t *ctx_of(const dec_t *d, const char *n) {   /* 없으면 0 으로 찬 더미 */
    static const dec_ctx_t none;
    int i = dec_find(d, n);
    return i < 0 ? &none : &d->c[i];
}
static bool sum_ok(const dec_t *d) {          /* 불변식: 모든 몫 + 모름 = 전체 */
    uint64_t s = d->unknown_us;
    for (int i = 0; i < d->nc; i++) s += d->c[i].run_us;
    return s == d->span_us;
}

static void test_part_b(void) {
    printf("-- Part B: 디코더 (구간·CPU·wrap·첫 이벤트 없음·DROP)\n");
    dec_t *d = &g_d;
    /* B-1 기본: SWITCH_IN 으로 구간, ISR 은 따로, 깔린 태스크는 stolen */
    const char *t1 = "# rtos-trace v1 unit=us\n# comment\n\n100 SWITCH_IN A\n400 SWITCH_OUT A\n400 SWITCH_IN B\n"
                     "500 ISR_ENTER X\n550 ISR_EXIT X\n700 SWITCH_OUT B\n700 SWITCH_IN A\n1000 SWITCH_OUT A\n";
    CHECK(trace_decode(t1, d) == DEC_OK);
    CHECK(run_of(d, "A") == 600 && run_of(d, "B") == 250 && run_of(d, "X") == 50 && stolen_of(d, "B") == 50);
    CHECK(d->span_us == 900 && d->unknown_us == 0 && d->n_switches == 3 && sum_ok(d));
    CHECK(d->niv == 5 && d->iv[1].t0 == 300 && d->iv[1].t1 == 400 && d->iv[2].ctx == dec_find(d, "X")
          && d->iv[2].t0 == 400 && d->iv[2].t1 == 450);
    CHECK(dec_find(d, "A") >= 0 && dec_cpu_bp(d, dec_find(d, "A")) == 6667);
    /* B-2 32비트 wrap: 4294967000 → 296 은 592 µs 뒤 */
    CHECK(trace_decode("# rtos-trace v1 unit=us\n4294967000 SWITCH_IN A\n4294967200 SWITCH_IN B\n296 SWITCH_OUT B\n", d) == DEC_OK);
    CHECK(d->wraps == 1 && run_of(d, "A") == 200 && run_of(d, "B") == 392 && d->span_us == 592);
    /* B-3 시간이 거꾸로 (1000 → 900) 은 오류, 줄 번호 */
    CHECK(trace_decode("# rtos-trace v1 unit=us\n1000 SWITCH_IN A\n900 SWITCH_IN B\n", d) == DEC_EBACKWARDS && d->err_line == 3);
    /* B-4 첫 SWITCH_IN 이 없다 (중간부터): 그 전 시간은 아무에게도 주지 않는다 */
    CHECK(trace_decode("# rtos-trace v1 unit=us\n50 RELEASE C\n80 SWITCH_OUT C\n200 SWITCH_IN A\n300 SWITCH_OUT A\n", d) == DEC_OK);
    CHECK(d->unknown_us == 150 && run_of(d, "C") == 0 && run_of(d, "A") == 100 && sum_ok(d));
    /* B-5 DROP: 그 시각부터 다음 SWITCH_IN 까지 모름, 잃은 수 합산 */
    CHECK(trace_decode("# rtos-trace v1 unit=us\n0 SWITCH_IN A\n300 DROP TRACE 5\n420 ISR_EXIT X\n500 SWITCH_IN B\n600 SWITCH_OUT B\n", d) == DEC_OK);
    CHECK(run_of(d, "A") == 300 && d->unknown_us == 200 && run_of(d, "B") == 100 && d->n_dropped == 5 && d->n_drop_lines == 1);
    CHECK(d->n_mismatch == 1 && sum_ok(d) && d->niv == 3 && d->iv[1].ctx == -1);
    /* B-6 같은 태스크 재선택 (vTaskSwitchContext 는 매번 OUT/IN 을 찍는다) = 전환 아님 */
    CHECK(trace_decode("# rtos-trace v1 unit=us\n0 SWITCH_IN A\n10 SWITCH_OUT A\n10 SWITCH_IN A\n30 SWITCH_OUT A\n", d) == DEC_OK);
    CHECK(d->n_switches == 1 && ctx_of(d, "A")->n_in == 1 && d->niv == 1 && run_of(d, "A") == 30);
    /* B-7 중첩 ISR */
    CHECK(trace_decode("# rtos-trace v1 unit=us\n0 SWITCH_IN A\n10 ISR_ENTER U\n15 ISR_ENTER T\n18 ISR_EXIT T\n30 ISR_EXIT U\n40 SWITCH_OUT A\n", d) == DEC_OK);
    CHECK(run_of(d, "U") == 17 && run_of(d, "T") == 3 && stolen_of(d, "A") == 20 && run_of(d, "A") == 20 && sum_ok(d));
    /* B-8 형식 오류들 */
    CHECK(trace_decode("100 SWITCH_IN A\n", d) == DEC_EHEADER);
    CHECK(trace_decode("# rtos-trace v1 unit=us\n100 SWITCH_IN\n", d) == DEC_ESYNTAX && d->err_line == 2);
    CHECK(trace_decode("# rtos-trace v1 unit=us\n100 SWITCHED_IN A\n", d) == DEC_EEVENT);
    CHECK(trace_decode("# rtos-trace v1 unit=us\n100 SWITCH_IN A-1\n", d) == DEC_ESYNTAX);
    CHECK(trace_decode("# rtos-trace v1 unit=us\n4294967296 SWITCH_IN A\n", d) == DEC_ESYNTAX);
    CHECK(trace_decode("# rtos-trace v1 unit=us\n5 DROP TRACE\n", d) == DEC_ESYNTAX);
}

/* ======================= Part C: 모터 제어기 시뮬레이터 (주어진 코드) =======================
 * 10 µs 걸음. 태스크 Ctrl(우선 3, 1 ms, 200~300 µs) / Comm(2, 2.5 ms, 500~700) / Log(1, 10 ms, 2000~3000) /
 * IDLE. ISR: ADC (500 µs 마다 30 µs, 네 번에 한 번 70 µs), CAN (20 ms·33 ms 에 프레임 8개 버스트, 30 µs 간격,
 * 각 20 µs). ISR 끼리는 중첩 대신 차례로 (tail-chaining: 앞 ISR 이 끝나면 태스크로 안 돌아가고 바로 다음 ISR).
 * 타임스탬프 = SIM_T0 + s → 16.4 ms 에 wrap. 스위치는 ISR 이 끝난 뒤에만 (PendSV 가 가장 낮은 우선순위라서).
 * 정답 own[] = 매 걸음 누가 CPU 를 썼나. */
#define SIM_T0    0xFFFFC000u
#define SIM_END   40000u
#define SIM_STEP  10u
#define SIM_NSTEP (SIM_END / SIM_STEP)
#define REC_BYTES 16u
enum { N_IDLE, N_CTRL, N_COMM, N_LOG, N_ADC, N_CAN, N_DELAY, N_NAMES };
static const char *const SIM_NAMES[N_NAMES] = { "IDLE", "Ctrl", "Comm", "Log", "ADC", "CAN", "delay" };
typedef struct { int prio; uint32_t period, phase, cost0, costv; } sim_task_t;
static const sim_task_t SIM_TASK[4] = { { 0, 0, 0, 0, 0 }, { 3, 1000, 0, 200, 50 }, { 2, 2500, 100, 500, 100 },
                                        { 1, 10000, 300, 2000, 500 } };
typedef struct {
    uint32_t run[N_NAMES], stolen[N_NAMES], overrun, events;
    uint32_t resp[4][DEC_MAXRESP], nresp[4], maxresp[4];
    uint8_t own[SIM_NSTEP];
} sim_truth_t;
typedef struct { trace_ring_t *ring; tr_text_t *tx; uint32_t link_bytes_per_step, credit; } sim_sink_t;

static void sim_emit(sim_sink_t *k, sim_truth_t *tr, uint32_t s, int ev, int id, int argk, uint32_t arg) {
    tr->events++;
    tr_record(k->ring, SIM_T0 + s, (uint8_t)ev, (uint8_t)id, (uint8_t)argk, arg);
}
static void sim_drain(sim_sink_t *k) {        /* UART: 걸음마다 link 바이트, 레코드 1개 = 16바이트 */
    if (!k->link_bytes_per_step) return;
    k->credit += k->link_bytes_per_step;
    while (k->credit >= REC_BYTES) {
        trace_rec_t r;
        if (!tr_pop(k->ring, &r)) { k->credit = 0; break; }   /* 보낼 게 없으면 회선은 논다 */
        tx_record(k->tx, &r);
        k->credit -= REC_BYTES;
    }
}
static bool can_frame_at(uint32_t s) {
    return (s >= 20000 && s < 20000 + 8 * 30 && (s - 20000) % 30 == 0) ||
           (s >= 33000 && s < 33000 + 8 * 30 && (s - 33000) % 30 == 0);
}
static void sim_run(sim_sink_t *k, sim_truth_t *tr) {
    memset(tr, 0, sizeof *tr);
    int cur = -1, isr = -1;
    uint32_t rem[4] = { 0 }, rel[4] = { 0 }, jobs[4] = { 0 }, isr_left = 0, adc_n = 0, adc_pend = 0, can_pend = 0;
    for (uint32_t s = 0; s < SIM_END; s += SIM_STEP) {
        for (int i = 1; i <= 3; i++) {
            const sim_task_t *T = &SIM_TASK[i];
            if (s >= T->phase && (s - T->phase) % T->period == 0) {
                if (rem[i]) tr->overrun++;
                sim_emit(k, tr, s, EV_RELEASE, i, ARG_NONE, 0);
                rem[i] = T->cost0 + (jobs[i] % 3u) * T->costv;
                rel[i] = s;
                jobs[i]++;
            }
        }
        if (s >= 50 && (s - 50) % 500 == 0) adc_pend++;
        if (can_frame_at(s)) can_pend++;
        if (isr < 0 && (adc_pend || can_pend)) {               /* ADC 가 CAN 보다 우선 */
            if (adc_pend) { adc_pend--; isr = N_ADC; isr_left = (adc_n++ % 4u == 3u) ? 70u : 30u; }
            else          { can_pend--; isr = N_CAN; isr_left = 20u; }
            sim_emit(k, tr, s, EV_ISR_ENTER, isr, ARG_NONE, 0);
        }
        if (isr >= 0) {
            tr->run[isr] += SIM_STEP;
            if (cur >= 0) tr->stolen[cur] += SIM_STEP;
            tr->own[s / SIM_STEP] = (uint8_t)isr;
            if ((isr_left -= SIM_STEP) == 0) { sim_emit(k, tr, s + SIM_STEP, EV_ISR_EXIT, isr, ARG_NONE, 0); isr = -1; }
        } else {
            int nx = N_IDLE;
            for (int i = 1; i <= 3; i++) if (rem[i] && SIM_TASK[i].prio > SIM_TASK[nx].prio) nx = i;
            if (nx != cur) {
                if (cur >= 0) sim_emit(k, tr, s, EV_SWITCH_OUT, cur, ARG_NONE, 0);
                sim_emit(k, tr, s, EV_SWITCH_IN, nx, ARG_NONE, 0);
                cur = nx;
            }
            tr->run[cur] += SIM_STEP;
            tr->own[s / SIM_STEP] = (uint8_t)cur;
            if (cur != N_IDLE && (rem[cur] -= SIM_STEP) == 0) {
                sim_emit(k, tr, s + SIM_STEP, EV_BLOCK, cur, ARG_NAME, N_DELAY);
                uint32_t r = s + SIM_STEP - rel[cur];
                if (tr->nresp[cur] < DEC_MAXRESP) tr->resp[cur][tr->nresp[cur]] = r;
                tr->nresp[cur]++;
                if (r > tr->maxresp[cur]) tr->maxresp[cur] = r;
            }
        }
        sim_drain(k);
    }
    sim_emit(k, tr, SIM_END, EV_SWITCH_OUT, cur, ARG_NONE, 0);   /* 트레이스 끝 표시 */
}
/* 실행 + 남은 것 비우기 + 텍스트 + 디코드 */
static int sim_trace(trace_ring_t *ring, uint32_t cap, tr_mode_t mode, uint32_t link, sim_truth_t *tr) {
    tr_text_t x;
    tr_init(ring, g_store, cap, mode);
    tx_begin(&x, g_text, sizeof g_text, SIM_NAMES, N_NAMES);
    sim_sink_t k = { ring, &x, link, 0 };
    sim_run(&k, tr);
    trace_rec_t r;
    while (tr_pop(ring, &r)) tx_record(&x, &r);   /* 정지 후 호스트가 나머지를 읽는다 */
    tx_end(&x, ring);
    if (x.overflow) return -1;
    return trace_decode(g_text, &g_d);
}
/* 디코더의 모든 구간이 정답과 맞나: 틀린 걸음 수 (0 이어야), 덮은 걸음 수 */
static uint32_t check_intervals(const dec_t *d, const sim_truth_t *tr, uint32_t *covered) {
    uint32_t bad = 0, cov = 0, off = d->t0_32 - SIM_T0;          /* 디코더 시각 0 = 시뮬 시각 off */
    int map[DEC_MAXCTX];
    for (int i = 0; i < d->nc; i++) {
        map[i] = -1;
        for (int j = 0; j < N_NAMES; j++) if (strcmp(d->c[i].name, SIM_NAMES[j]) == 0) map[i] = j;
    }
    for (int k = 0; k < d->niv; k++) {
        if (d->iv[k].ctx < 0) continue;
        for (uint64_t t = d->iv[k].t0; t < d->iv[k].t1; t += SIM_STEP) {
            uint32_t s = off + (uint32_t)t;
            cov++;
            if (s >= SIM_END || tr->own[s / SIM_STEP] != map[d->iv[k].ctx]) bad++;
        }
    }
    *covered = cov;
    return bad;
}
static bool resp_subset(const dec_ctx_t *c, const sim_truth_t *tr, int task) {   /* 잰 응답 ⊂ 정답 (중복 포함) */
    bool used[DEC_MAXRESP] = { false };
    uint32_t n = c->n_resp < DEC_MAXRESP ? c->n_resp : DEC_MAXRESP;
    for (uint32_t i = 0; i < n; i++) {
        bool found = false;
        for (uint32_t j = 0; j < tr->nresp[task] && j < DEC_MAXRESP && !found; j++)
            if (!used[j] && tr->resp[task][j] == c->resp[i]) used[j] = found = true;
        if (!found) return false;
    }
    return true;
}

static void test_part_c(void) {
    printf("-- Part C: 모터 제어기 트레이스 (정답 대조)\n");
    static sim_truth_t tr;
    trace_ring_t ring;
    uint32_t cov;
    /* C-1 큰 링 (4096), 정지 후 덤프: 손실 0 → 디코드 = 정답, µs 단위로 */
    CHECK(sim_trace(&ring, 4096, TR_STOP_WHEN_FULL, 0, &tr) == DEC_OK);
    CHECK(tr.overrun == 0 && ring.dropped == 0 && g_d.n_drop_lines == 0 && g_d.wraps == 1);
    bool all = true;
    for (int i = 0; i < N_DELAY; i++)
        all = all && run_of(&g_d, SIM_NAMES[i]) == tr.run[i] && stolen_of(&g_d, SIM_NAMES[i]) == tr.stolen[i];
    CHECK(all);
    CHECK(check_intervals(&g_d, &tr, &cov) == 0 && cov == SIM_NSTEP && g_d.unknown_us == 0 && sum_ok(&g_d));
    all = true;
    for (int i = 1; i <= 3; i++) {
        const dec_ctx_t *c = ctx_of(&g_d, SIM_NAMES[i]);
        all = all && c->n_resp == tr.nresp[i] && c->max_resp == tr.maxresp[i] && resp_subset(c, &tr, i);
    }
    CHECK(all);
    printf("      events=%u  (C-1 표: 디코더 결과)\n", tr.events);
    dec_print_table(&g_d);
    printf("      응답 시간 정답 max: Ctrl=%u Comm=%u Log=%u\n", tr.maxresp[1], tr.maxresp[2], tr.maxresp[3]);
    /* C-2 스트리밍: 링 8, UART 3 B/10 µs (300 kB/s) → CAN 버스트에서 손실. 그래도 틀린 귀속 0 */
    CHECK(sim_trace(&ring, 8, TR_STOP_WHEN_FULL, 3, &tr) == DEC_OK);
    CHECK(ring.dropped > 0 && g_d.n_dropped == ring.dropped && g_d.n_drop_lines > 0);
    uint32_t bad = check_intervals(&g_d, &tr, &cov);
    CHECK(bad == 0 && g_d.unknown_us > 0 && sum_ok(&g_d));
    all = true;
    for (int i = 0; i < N_DELAY; i++) all = all && run_of(&g_d, SIM_NAMES[i]) <= tr.run[i];
    for (int i = 1; i <= 3; i++) {
        const dec_ctx_t *c = ctx_of(&g_d, SIM_NAMES[i]);
        all = all && resp_subset(c, &tr, i) && c->n_resp + c->n_resp_lost <= tr.nresp[i] && c->max_resp <= tr.maxresp[i];
    }
    CHECK(all);
    printf("      C-2 링 8: 잃음 %u/%u 이벤트, DROP 줄 %u, 모름 %llu µs (%.1f %%), 응답 잰 것 Ctrl %u/%u\n",
           ring.dropped, tr.events, g_d.n_drop_lines, (unsigned long long)g_d.unknown_us,
           g_d.span_us ? 100.0 * (double)g_d.unknown_us / (double)g_d.span_us : 0.0,
           ctx_of(&g_d, "Ctrl")->n_resp, tr.nresp[1]);
    /* 대조군: DROP 줄을 지우고(= 구멍을 무시하는 순진한 디코더) 다시 디코드 → 틀린 귀속이 생긴다 */
    static char naive[1 << 20];
    size_t nl = 0;
    for (const char *q = g_text; *q; ) {
        size_t n = strcspn(q, "\n") + 1;
        if (!strstr(q, " DROP ") || strstr(q, " DROP ") > q + n) { memcpy(naive + nl, q, n); nl += n; }
        q += n;
    }
    naive[nl] = '\0';
    CHECK(trace_decode(naive, &g_d) == DEC_OK);
    uint32_t bad_naive = check_intervals(&g_d, &tr, &cov);
    CHECK(bad_naive > 0);
    printf("      대조군 (DROP 무시): 틀린 귀속 %u 걸음 = %u µs, 모름 %llu µs\n", bad_naive, bad_naive * SIM_STEP,
           (unsigned long long)g_d.unknown_us);
    /* C-3 같은 회선, 링 32: 버스트를 흡수해 손실 0 → 다시 정답과 같다 */
    CHECK(sim_trace(&ring, 32, TR_STOP_WHEN_FULL, 3, &tr) == DEC_OK);
    CHECK(ring.dropped == 0 && check_intervals(&g_d, &tr, &cov) == 0 && cov == SIM_NSTEP);
    /* C-4 flight recorder: 덮어쓰기 128, 회선 없음 → 마지막 128개만. 첫 줄 = DROP, 창 안은 정확 */
    CHECK(sim_trace(&ring, 128, TR_OVERWRITE, 0, &tr) == DEC_OK);
    CHECK(strncmp(g_text, "# rtos-trace v1 unit=us\n", 24) == 0 && strstr(g_text + 24, " DROP TRACE ") == strchr(g_text + 24, ' '));
    CHECK(g_d.n_dropped == tr.events - 128u && check_intervals(&g_d, &tr, &cov) == 0 && cov > 0 && sum_ok(&g_d));
    printf("      C-4 덮어쓰기 128: 앞 %u개 잃음, 창 %llu µs = 귀속 %u + 모름 %llu µs\n", g_d.n_dropped,
           (unsigned long long)g_d.span_us, cov * SIM_STEP, (unsigned long long)g_d.unknown_us);
}

int main(int argc, char **argv) {
    (void)tr_put; (void)tx_line;                       /* 시작 코드에서 "안 쓰는 함수" 경고 방지 */
    bool lossy = argc > 1 && strcmp(argv[1], "--dump-lossy") == 0;
    if (lossy || (argc > 1 && strcmp(argv[1], "--dump") == 0)) {   /* timeline.py 입력용: C-1 / C-2 트레이스 */
        static sim_truth_t tr; trace_ring_t ring;
        g_quiet = true;
        if (sim_trace(&ring, lossy ? 8 : 4096, TR_STOP_WHEN_FULL, lossy ? 3 : 0, &tr) != DEC_OK) return 1;
        fputs(g_text, stdout);
        return 0;
    }
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* A06_LIB_ONLY */
