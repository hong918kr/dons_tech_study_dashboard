/* ARCH-A04 MESI 캐시 일관성 프로토콜 시뮬레이터
 *
 * 코어 4개(최대 8) + 코어별 private 캐시(direct-mapped 또는 작은 set-associative, LRU) +
 * 원자적(atomic) 스누핑 버스 1개: 한 사이클에 버스 트랜잭션 1개, 라운드로빈 중재.
 *   요청 메시지   BusRd(읽기 미스) · BusRdX(쓰기 미스 = RFO) · BusUpgr(S/O 에서 쓰기) · BusWB(dirty 축출 write-back)
 *   스누프 응답   shared 신호 · Flush(M/O 소유자가 라인을 버스에 올림 = cache-to-cache 전송, intervention)
 *
 *   Part A  상태 전이 함수 3개 (TODO): cpu_access / snoop / fill_state.
 *           교과서 트레이스의 전이·버스 메시지·데이터 출처가 기대 로그와 한 글자까지 같아야 한다.
 *   Part B  불변식 3개를 매 사이클 검사 (하네스 제공): SWMR / DATA(모든 유효 복사본과 읽은 값 == golden) /
 *           MEM(M·O 소유자가 없으면 메모리 == golden). 돌연변이(mutant) 8개를 주입해 각각 어느 불변식이
 *           몇 사이클에 잡는지 확인 + xorshift 랜덤 트레이스 스트레스(고정 시드).
 *   Part C  MSI / MESI / MOESI 비교 (E 가 아끼는 BusUpgr, O 가 아끼는 write-back) + MEM-M04 와 비교할
 *           표준 패턴 a(공유 카운터) / b(코어별 카운터) / b'(같은 라인 = false sharing) / c(읽기 전용 공유).
 *
 * 트레이스 형식 (텍스트 파일 / 코드 안 문자열, 같은 파서):
 *   c<k> R <addr>          읽기
 *   c<k> W <addr> <val>    쓰기
 *   c<k> A <addr> <inc>    원자 fetch_add (RMW → 쓰기처럼 M 이 필요)
 *   sync                   앞의 연산이 모두 끝나야 다음 연산 시작 (배리어 — 타이밍을 고정할 때)
 *   # ...                  주석
 * 주소 = 32-bit 워드 번호, LINE_WORDS 워드 = 한 캐시 라인. 코어별 연산은 프로그램 순서대로 1 개씩.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 *        ./so trace_textbook.txt mesi [mutant]     (트레이스 파일을 사이클마다 출력하며 실행)
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CORE 8
#define LINE_WORDS 4               /* 한 라인 = 4 워드 = 16 B (축소 모델. 실제 칩은 64 B / Apple M 128 B) */
#define MEM_WORDS 256
#define NLINES (MEM_WORDS / LINE_WORDS)
#define MAX_SETS 16
#define MAX_WAYS 4
#define MAXQ 4096                  /* 코어당 최대 연산 수 */
#define MAXEV 64                   /* 테스트용으로 보관하는 이벤트 로그 줄 수 */

typedef enum { ST_I, ST_S, ST_E, ST_O, ST_M, NST } mstate_t;
static const char *const ST_NAME[NST] = {"I", "S", "E", "O", "M"};
typedef enum { PR_MSI, PR_MESI, PR_MOESI } proto_t;
static const char *const PR_NAME[3] = {"MSI", "MESI", "MOESI"};
typedef enum { BUS_NONE, BUS_RD, BUS_RDX, BUS_UPGR, BUS_WB, NBUS } bus_t;
static const char *const BUS_NAME[NBUS] = {"-", "BusRd", "BusRdX", "BusUpgr", "BusWB"};
typedef enum { OP_R, OP_W, OP_A } opk_t;

typedef struct { bus_t msg; mstate_t next; } cpu_out_t;               /* msg == BUS_NONE → 히트, next = 새 상태 */
typedef struct { mstate_t next; bool flush; bool shared; } snoop_out_t; /* 스누프한 쪽의 새 상태와 응답 */

/* ======================= Part A: 상태 전이 함수 (프로토콜의 전부) ======================= */

/* 프로세서 쪽: 상태 s 인 라인에 op 를 하려면 어떤 버스 메시지가 필요한가 (BUS_NONE = 로컬 히트).
 * 히트면 next 에 새 상태 (예: E 에 쓰기 → M, 버스 없이 = silent upgrade). */
static cpu_out_t cpu_access(proto_t p, mstate_t s, opk_t op) {
    cpu_out_t o = { BUS_NONE, s };
    /* TODO(Part A-1): 상태 s 에서 op (OP_R 읽기 / OP_W·OP_A 쓰기) 를 하려면 어떤 버스 메시지가 필요한가?
     *   필요 없으면 BUS_NONE 그대로 두고 o.next 에 히트 후 상태. 다섯 상태 × 읽기/쓰기 = 10 칸을 채운다.
     *   E 에 쓰기가 왜 특별한지(E 의 존재 이유)를 생각할 것.                                     */
    (void)p; (void)op;
    return o;
}

/* 스누프 쪽: 다른 코어의 메시지 m 을 본, 상태 s 인 캐시의 반응. */
static snoop_out_t snoop(proto_t p, mstate_t s, bus_t m) {
    snoop_out_t o = { s, false, false };
    /* TODO(Part A-2): 다른 코어가 보낸 m (BusRd / BusRdX / BusUpgr) 을 본, 상태 s 인 캐시의 반응:
     *   o.next (새 상태), o.flush (내 데이터를 버스에 올리나), o.shared (나도 가졌다고 알리나).
     *   최신 데이터를 가진 쪽이 누구인지, 쓰려는 쪽이 생기면 남은 복사본이 어떻게 돼야 하는지. p 가 MOESI 면 다르다. */
    (void)p; (void)m;
    return o;
}

/* 요청한 쪽: 트랜잭션 m 이 끝난 뒤 내 라인의 새 상태. shared = 다른 캐시가 shared 신호를 냈나. */
static mstate_t fill_state(proto_t p, bus_t m, bool shared) {
    /* TODO(Part A-3): 트랜잭션 m 이 끝난 뒤 요청한 쪽의 새 상태. shared 신호가 무엇을 가르나? (MSI 에는 E 가 없다) */
    (void)p; (void)m; (void)shared;
    return ST_S;
}

/* ======================= 돌연변이 (Part B: 검사기를 검사한다) ======================= */
enum {
    MUT_NO_INV_UPGR    = 1u << 0,  /* BusUpgr 를 본 S 가 무효화 안 함 */
    MUT_NO_INV_RDX     = 1u << 1,  /* BusRdX 를 본 S 가 무효화 안 함 */
    MUT_E_WHEN_SHARED  = 1u << 2,  /* shared 신호를 무시하고 읽기 미스에 E */
    MUT_SILENT_S_WRITE = 1u << 3,  /* S 에서 버스 없이 M 으로 (E 처럼 취급) */
    MUT_NO_FLUSH_RD    = 1u << 4,  /* BusRd 를 본 M 이 데이터를 안 내놓고 S 로 */
    MUT_FLUSH_NO_MEMWB = 1u << 5,  /* M→S Flush 때 메모리 갱신 안 함 (= O 없는 MOESI) */
    MUT_EVICT_NO_WB    = 1u << 6,  /* M 라인 축출 때 write-back 없이 버림 */
    MUT_UPGR_RACE      = 1u << 7,  /* 대기 중 무효화돼도 처음 정한 BusUpgr 를 그대로 보냄 */
    MUT_O_NO_SUPPLY    = 1u << 8,  /* (MOESI) BusRd 를 본 O 가 데이터 공급 안 함 → 연습 3 */
    MUT_O_EVICT_SILENT = 1u << 9,  /* (MOESI) O 라인 축출 때 write-back 없이 버림 → 연습 3 */
};
static const struct { unsigned bit; const char *name; } MUTANTS[] = {
    {MUT_NO_INV_UPGR, "no_inv_upgr"}, {MUT_NO_INV_RDX, "no_inv_rdx"}, {MUT_E_WHEN_SHARED, "e_when_shared"},
    {MUT_SILENT_S_WRITE, "silent_s_write"}, {MUT_NO_FLUSH_RD, "no_flush_rd"}, {MUT_FLUSH_NO_MEMWB, "flush_no_memwb"},
    {MUT_EVICT_NO_WB, "evict_no_wb"}, {MUT_UPGR_RACE, "upgr_race"}, {MUT_O_NO_SUPPLY, "o_no_supply"},
    {MUT_O_EVICT_SILENT, "o_evict_silent"},
};
#define NMUT ((int)(sizeof MUTANTS / sizeof MUTANTS[0]))

/* ======================= 시뮬레이터 (하네스 — starter 와 같다) ======================= */
typedef struct { mstate_t st; bool used; uint32_t lno; uint64_t lru; uint32_t d[LINE_WORDS]; } line_t;
typedef struct { line_t way[MAX_SETS][MAX_WAYS]; } cache_t;
typedef struct { opk_t op; int phase; uint32_t addr, val; } op_t;
typedef enum { INV_OK, INV_SWMR, INV_DATA, INV_MEM, INV_HANG } inv_t;
static const char *const INV_NAME[] = {"OK", "SWMR", "DATA", "MEM", "HANG"};

typedef struct {
    long cycles, ops, hits, bus[NBUS], flush, c2c, memrd, memwb, inval, silent_up, snoop_valid, snoop_act;
} stats_t;

typedef struct {
    proto_t proto; int ncore, nsets, nways; unsigned mut; int verbose;
    cache_t c[MAX_CORE];
    uint32_t mem[MEM_WORDS], gold[MEM_WORDS];   /* gold = 전역 쓰기 순서상 최신 값 (shadow memory) */
    op_t q[MAX_CORE][MAXQ]; int qlen[MAX_CORE], qpos[MAX_CORE], phase;
    bool pend[MAX_CORE]; bus_t pend_msg[MAX_CORE];
    int rr; long cycle, nops; uint64_t tick;
    stats_t st;
    inv_t viol; long viol_cycle; char viol_msg[160];
    int nev; char ev[MAXEV][112]; char cur[200];
} sim_t;

static void sim_reset(sim_t *s, proto_t p, int ncore, int nsets, int nways, unsigned mut) {
    memset(s, 0, sizeof *s);
    s->proto = p; s->ncore = ncore; s->nsets = nsets; s->nways = nways; s->mut = mut;
    s->rr = ncore - 1;                                   /* 첫 중재는 c0 부터 */
    for (int i = 0; i < MEM_WORDS; i++) s->mem[i] = s->gold[i] = 1000u + (uint32_t)i;
}

static void violate(sim_t *s, inv_t inv, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void violate(sim_t *s, inv_t inv, const char *fmt, ...) {
    if (s->viol != INV_OK) return;                       /* 첫 위반만 기록 */
    s->viol = inv; s->viol_cycle = s->cycle;
    va_list ap; va_start(ap, fmt); vsnprintf(s->viol_msg, sizeof s->viol_msg, fmt, ap); va_end(ap);
    if (s->verbose) printf("  !! VIOLATION %s at cycle %ld: %s\n", INV_NAME[inv], s->cycle, s->viol_msg);
}
static void evf(sim_t *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void evf(sim_t *s, const char *fmt, ...) {
    size_t n = strlen(s->cur);
    va_list ap; va_start(ap, fmt); vsnprintf(s->cur + n, sizeof s->cur - n, fmt, ap); va_end(ap);
}
static void ev_commit(sim_t *s) {
    if (s->nev < MAXEV) snprintf(s->ev[s->nev++], sizeof s->ev[0], "%s", s->cur);
    if (s->verbose) printf("%4ld  %s\n", s->cycle, s->cur);
    s->cur[0] = '\0';
}

/* ---- 트레이스 적재 ---- */
static int add_op(sim_t *s, int k, opk_t op, uint32_t addr, uint32_t val) {
    if (k < 0 || k >= s->ncore || addr >= MEM_WORDS || s->qlen[k] >= MAXQ) return -1;
    s->q[k][s->qlen[k]++] = (op_t){ op, s->phase, addr, val };
    s->nops++;
    return 0;
}
static void add_sync(sim_t *s) { s->phase++; }

/* 텍스트 트레이스 → 큐. 성공 시 연산 수, 실패 시 -(줄 번호). */
static long sim_load(sim_t *s, const char *text) {
    int lineno = 0;
    for (const char *p = text; *p; ) {
        char buf[128]; size_t n = 0;
        lineno++;
        while (*p && *p != '\n') { if (n + 1 < sizeof buf) buf[n++] = *p; p++; }
        if (*p == '\n') p++;
        buf[n] = '\0';
        char *hash = strchr(buf, '#'); if (hash) *hash = '\0';
        char who[16], opc[8]; long addr = 0, val = 0;
        int got = sscanf(buf, "%15s %7s %li %li", who, opc, &addr, &val);
        if (got <= 0) continue;                                         /* 빈 줄 · 주석 */
        if (got == 1 && strcmp(who, "sync") == 0) { add_sync(s); continue; }
        int k = -1;
        if (who[0] != 'c' || sscanf(who + 1, "%d", &k) != 1 || got < 3) return -lineno;
        int op = opc[0] == 'R' ? OP_R : opc[0] == 'W' ? OP_W : opc[0] == 'A' ? OP_A : -1;
        if (op < 0 || opc[1] != '\0' || (op == OP_R) != (got == 3) || addr < 0 || val < 0) return -lineno;
        if (add_op(s, k, (opk_t)op, (uint32_t)addr, (uint32_t)val) != 0) return -lineno;
    }
    return s->nops;
}

/* ---- 캐시 조회 ---- */
static uint32_t line_of(uint32_t addr) { return addr / LINE_WORDS; }
static line_t *lookup(sim_t *s, int k, uint32_t L) {                    /* 유효(≠I) 복사본 */
    int set = (int)(L % (uint32_t)s->nsets);
    for (int w = 0; w < s->nways; w++) {
        line_t *ln = &s->c[k].way[set][w];
        if (ln->used && ln->lno == L && ln->st != ST_I) return ln;
    }
    return NULL;
}
static line_t *frame(sim_t *s, int k, uint32_t L) {                     /* L 을 담을 자리: 같은 태그 > 빈 칸 > LRU */
    int set = (int)(L % (uint32_t)s->nsets);
    line_t *best = NULL;
    for (int w = 0; w < s->nways; w++) {
        line_t *ln = &s->c[k].way[set][w];
        if (ln->used && ln->lno == L) return ln;                        /* I 라도 태그가 같으면 재사용 (옛 데이터 그대로) */
        if (!ln->used) { if (!best || best->used) best = ln; continue; }
        if (!best || (best->used && ln->lru < best->lru)) best = ln;
    }
    return best;
}

/* ---- 돌연변이를 씌운 전이 함수 ---- */
static cpu_out_t cpu_m(const sim_t *s, mstate_t st, opk_t op) {
    cpu_out_t o = cpu_access(s->proto, st, op);
    if ((s->mut & MUT_SILENT_S_WRITE) && st == ST_S && op != OP_R) { o.msg = BUS_NONE; o.next = ST_M; }
    return o;
}
static snoop_out_t snoop_m(const sim_t *s, mstate_t st, bus_t m) {
    snoop_out_t o = snoop(s->proto, st, m);
    if ((s->mut & MUT_NO_INV_UPGR) && m == BUS_UPGR && st == ST_S) o.next = ST_S;
    if ((s->mut & MUT_NO_INV_RDX) && m == BUS_RDX && st == ST_S) o.next = ST_S;
    if ((s->mut & MUT_NO_FLUSH_RD) && m == BUS_RD && st == ST_M) o.flush = false;
    if ((s->mut & MUT_O_NO_SUPPLY) && m == BUS_RD && st == ST_O) o.flush = false;
    return o;
}
static mstate_t fill_m(const sim_t *s, bus_t m, bool shared) {
    mstate_t ns = fill_state(s->proto, m, shared);
    if ((s->mut & MUT_E_WHEN_SHARED) && m == BUS_RD && s->proto != PR_MSI) ns = ST_E;
    return ns;
}

/* ---- 연산 완료: 읽은 값을 golden 과 비교 (DATA 불변식의 "읽기" 절반) ---- */
static void ev_op(sim_t *s, int k, const op_t *op) {
    if (op->op == OP_R) evf(s, "c%d R 0x%02x", k, op->addr);
    else evf(s, "c%d %c 0x%02x%s%u", k, op->op == OP_W ? 'W' : 'A', op->addr, op->op == OP_W ? "=" : "+", op->val);
}
static void do_op(sim_t *s, int k, line_t *ln, const op_t *op) {
    uint32_t w = op->addr % LINE_WORDS, v = ln->d[w];
    ln->lru = ++s->tick; s->st.ops++;
    if (op->op != OP_W && v != s->gold[op->addr])
        violate(s, INV_DATA, "c%d read 0x%02x = %u, latest write = %u", k, op->addr, v, s->gold[op->addr]);
    if (op->op == OP_W) { ln->d[w] = op->val; s->gold[op->addr] = op->val; }
    else if (op->op == OP_A) { ln->d[w] = v + op->val; s->gold[op->addr] += op->val; }
    s->qpos[k]++; s->pend[k] = false;
}
static void hit(sim_t *s, int k, line_t *ln, const op_t *op, mstate_t next) {
    ev_op(s, k, op); evf(s, " hit");
    if (next != ln->st) {
        evf(s, " c%d:%s->%s", k, ST_NAME[ln->st], ST_NAME[next]);
        if (ln->st == ST_E && next == ST_M) s->st.silent_up++;
    }
    ln->st = next; s->st.hits++;
    do_op(s, k, ln, op); ev_commit(s);
}

/* ---- 버스 트랜잭션 1 개 (원자적: 요청 → 모든 스누프 → 데이터 → 완료가 한 사이클) ---- */
static void txn(sim_t *s, int r) {
    const op_t *op = &s->q[r][s->qpos[r]];
    uint32_t L = line_of(op->addr);
    line_t *ln = lookup(s, r, L);
    cpu_out_t co = cpu_m(s, ln ? ln->st : ST_I, op->op);
    bus_t msg = (s->mut & MUT_UPGR_RACE) ? s->pend_msg[r] : co.msg;    /* 올바른 쪽: 승인 시점에 다시 계산 */
    char drop[32] = "";
    if (!ln) {
        line_t *v = frame(s, r, L);
        if (v->used && v->lno != L && v->st != ST_I) {                  /* 축출 */
            bool dirty = v->st == ST_M || v->st == ST_O;
            bool lose = (v->st == ST_M && (s->mut & MUT_EVICT_NO_WB)) || (v->st == ST_O && (s->mut & MUT_O_EVICT_SILENT));
            if (dirty && !lose) {                                       /* 이번 사이클은 write-back 이 버스를 쓴다 */
                memcpy(&s->mem[v->lno * LINE_WORDS], v->d, sizeof v->d);
                s->st.bus[BUS_WB]++; s->st.memwb++;
                evf(s, "c%d BusWB 0x%02x c%d:%s->I mem<-c%d", r, v->lno * LINE_WORDS, r, ST_NAME[v->st], r);
                v->st = ST_I; ev_commit(s);
                return;                                                 /* 연산은 다음 승인 때 */
            }
            snprintf(drop, sizeof drop, " drop(0x%02x:%s)", v->lno * LINE_WORDS, ST_NAME[v->st]);
            v->st = ST_I;
        }
        if (!v->used || v->lno != L) { v->used = true; v->lno = L; v->st = ST_I; }
        ln = v;
    }
    if (msg == BUS_NONE) { hit(s, r, ln, op, co.next); return; }      /* (올바른 프로토콜에선 안 옴) */

    ev_op(s, r, op); evf(s, " %s", BUS_NAME[msg]);
    char sn[160] = ""; size_t sl = 0;
    bool shared = false; int src = -1, memby = -1; uint32_t bus[LINE_WORDS] = {0};
    for (int k = 0; k < s->ncore; k++) {
        if (k == r) continue;
        line_t *o = lookup(s, k, L);
        if (!o) continue;
        s->st.snoop_valid++;
        snoop_out_t so = snoop_m(s, o->st, msg);
        if (so.shared) shared = true;
        if (so.flush && src < 0) {
            src = k; memcpy(bus, o->d, sizeof bus); s->st.flush++;
            if (s->proto != PR_MOESI && !(s->mut & MUT_FLUSH_NO_MEMWB)) {   /* MSI/MESI: Flush 가 메모리도 갱신 */
                memcpy(&s->mem[L * LINE_WORDS], o->d, sizeof o->d); s->st.memwb++; memby = k;
            }
        }
        if (so.next != o->st || so.flush) s->st.snoop_act++;           /* 실제로 반응해야 했던 캐시 */
        if (so.next != o->st || so.flush)
            sl += (size_t)snprintf(sn + sl, sizeof sn - sl, " c%d:%s%s%s%s", k, ST_NAME[o->st],
                                   so.next != o->st ? "->" : "", so.next != o->st ? ST_NAME[so.next] : "",
                                   so.flush ? "(Flush)" : "");
        if (o->st != ST_I && so.next == ST_I) s->st.inval++;
        o->st = so.next;
    }
    s->st.bus[msg]++;
    if (msg == BUS_RD || msg == BUS_RDX) {
        if (src >= 0) { memcpy(ln->d, bus, sizeof bus); s->st.c2c++; }
        else { memcpy(ln->d, &s->mem[L * LINE_WORDS], sizeof ln->d); s->st.memrd++; }
    }
    mstate_t ns = fill_m(s, msg, shared);
    evf(s, " c%d:%s->%s%s%s", r, ST_NAME[ln->st], ST_NAME[ns], sn, drop);
    if (msg == BUS_RD || msg == BUS_RDX) { if (src >= 0) evf(s, " src=c%d", src); else evf(s, " src=mem"); }
    if (memby >= 0) evf(s, " mem<-c%d", memby);
    ln->st = ns;
    do_op(s, r, ln, op); ev_commit(s);
}

/* ---- 불변식: 매 사이클 끝에 모든 라인 검사 ---- */
static void check_inv(sim_t *s) {
    if (s->viol != INV_OK) return;
    int own[NLINES] = {0}, excl[NLINES] = {0}, shr[NLINES] = {0}, dirty[NLINES] = {0}, stale_k = -1;
    uint32_t stale_L = 0;
    for (int k = 0; k < s->ncore; k++)
        for (int set = 0; set < s->nsets; set++)
            for (int w = 0; w < s->nways; w++) {
                const line_t *ln = &s->c[k].way[set][w];
                if (!ln->used || ln->st == ST_I) continue;
                if (ln->st == ST_S) shr[ln->lno]++; else own[ln->lno]++;
                if (ln->st == ST_M || ln->st == ST_E) excl[ln->lno]++;
                if (ln->st == ST_M || ln->st == ST_O) dirty[ln->lno]++;
                if (stale_k < 0 && memcmp(ln->d, &s->gold[ln->lno * LINE_WORDS], sizeof ln->d) != 0) {
                    stale_k = k; stale_L = ln->lno;
                }
            }
    for (uint32_t L = 0; L < NLINES; L++) {               /* SWMR: 소유자(M/E/O) ≤ 1, M/E 면 나머지 전부 I */
        if (own[L] > 1 || (excl[L] > 0 && shr[L] > 0)) {
            char who[96] = ""; size_t n = 0;
            for (int k = 0; k < s->ncore; k++) {
                const line_t *ln = lookup(s, k, L);
                if (ln) n += (size_t)snprintf(who + n, sizeof who - n, " c%d=%s", k, ST_NAME[ln->st]);
            }
            violate(s, INV_SWMR, "line 0x%02x:%s", L * LINE_WORDS, who);
            return;
        }
    }
    if (stale_k >= 0) {                                   /* DATA: 유효 복사본은 전부 최신 값 */
        violate(s, INV_DATA, "c%d holds a stale copy of line 0x%02x", stale_k, stale_L * LINE_WORDS);
        return;
    }
    for (uint32_t L = 0; L < NLINES; L++)                 /* MEM: dirty 소유자(M/O)가 없으면 메모리가 최신 */
        if (dirty[L] == 0 && memcmp(&s->mem[L * LINE_WORDS], &s->gold[L * LINE_WORDS],
                                    LINE_WORDS * sizeof(uint32_t)) != 0) {
            violate(s, INV_MEM, "line 0x%02x: no M/O owner but memory is stale", L * LINE_WORDS);
            return;
        }
}

/* ---- 한 사이클: (1) 히트는 로컬에서 끝 (2) 버스 필요한 코어 중 라운드로빈으로 1 개 승인 (3) 불변식 ---- */
static void sim_step(sim_t *s) {
    s->cycle++; s->st.cycles++;
    int ph = 1 << 30;
    for (int k = 0; k < s->ncore; k++)
        if (s->qpos[k] < s->qlen[k] && s->q[k][s->qpos[k]].phase < ph) ph = s->q[k][s->qpos[k]].phase;
    bool ready[MAX_CORE] = {false};
    for (int k = 0; k < s->ncore; k++) {
        if (s->qpos[k] >= s->qlen[k] || s->q[k][s->qpos[k]].phase != ph) continue;
        const op_t *op = &s->q[k][s->qpos[k]];
        line_t *ln = lookup(s, k, line_of(op->addr));
        cpu_out_t co = cpu_m(s, ln ? ln->st : ST_I, op->op);
        if (co.msg == BUS_NONE) {
            if (!ln) { ln = frame(s, k, line_of(op->addr)); ln->used = true; ln->lno = line_of(op->addr); }
            hit(s, k, ln, op, co.next);
        } else {
            ready[k] = true;
            if (!s->pend[k]) { s->pend[k] = true; s->pend_msg[k] = co.msg; }   /* 처음 정한 메시지를 기억 */
        }
    }
    for (int i = 1; i <= s->ncore; i++) {
        int k = (s->rr + i) % s->ncore;
        if (ready[k]) { s->rr = k; txn(s, k); break; }
    }
    check_inv(s);
}
static bool sim_done(const sim_t *s) {
    for (int k = 0; k < s->ncore; k++) if (s->qpos[k] < s->qlen[k]) return false;
    return true;
}
static inv_t sim_run(sim_t *s) {
    long limit = 4 * s->nops + 100;                      /* 연산 1 개 = 최대 2 승인(WB + 요청) × 코어 수 여유 */
    limit *= s->ncore;
    while (!sim_done(s) && s->viol == INV_OK) {
        sim_step(s);
        if (s->cycle > limit) { violate(s, INV_HANG, "no progress after %ld cycles", s->cycle); break; }
    }
    return s->viol;
}
static void sim_flush_all(sim_t *s) {                    /* 끝난 뒤 dirty 라인을 전부 메모리로 (통계 제외) */
    for (int k = 0; k < s->ncore; k++)
        for (int set = 0; set < s->nsets; set++)
            for (int w = 0; w < s->nways; w++) {
                line_t *ln = &s->c[k].way[set][w];
                if (ln->used && (ln->st == ST_M || ln->st == ST_O))
                    memcpy(&s->mem[ln->lno * LINE_WORDS], ln->d, sizeof ln->d);
            }
}
static void print_stats(const sim_t *s) {
    const stats_t *t = &s->st;
    printf("  %-5s %dc %dx%d: cyc=%ld ops=%ld hit=%ld | Rd=%ld RdX=%ld Upgr=%ld WB=%ld | flush=%ld c2c=%ld"
           " memrd=%ld memwb=%ld inval=%ld E>M=%ld\n", PR_NAME[s->proto], s->ncore, s->nsets, s->nways,
           t->cycles, t->ops, t->hits, t->bus[BUS_RD], t->bus[BUS_RDX], t->bus[BUS_UPGR], t->bus[BUS_WB],
           t->flush, t->c2c, t->memrd, t->memwb, t->inval, t->silent_up);
}

/* ---- 트레이스 생성기 ---- */
static uint32_t xs32(uint32_t *x) { uint32_t v = *x; v ^= v << 13; v ^= v >> 17; v ^= v << 5; return *x = v; }
static void gen_random(sim_t *s, uint32_t seed, int nops, int nlines) {
    uint32_t x = seed * 2654435761u + 1u;
    for (int i = 0; i < nops; i++) {
        if (xs32(&x) % 16 == 0) add_sync(s);
        int k = (int)(xs32(&x) % (uint32_t)s->ncore);
        uint32_t addr = (xs32(&x) % (uint32_t)nlines) * LINE_WORDS + xs32(&x) % LINE_WORDS;
        uint32_t r = xs32(&x) % 10;
        add_op(s, k, r < 5 ? OP_R : r < 8 ? OP_W : OP_A, addr, xs32(&x) % 1000);
    }
}
typedef enum { PAT_A, PAT_B, PAT_BFS, PAT_C } pat_t;     /* a 공유 카운터 · b 코어별 라인 · b' 같은 라인 · c 읽기 공유 */
static const char *const PAT_NAME[] = {"a shared counter", "b per-core lines", "b' same line", "c read-only shared"};
static void gen_pattern(sim_t *s, pat_t pat, int k, int rounds, bool strict) {
    for (int r = 0; r < rounds; r++) {
        for (int c = 0; c < k; c++) {
            if (pat == PAT_A) add_op(s, c, OP_A, 0, 1);
            else if (pat == PAT_B) add_op(s, c, OP_A, (uint32_t)(c * LINE_WORDS), 1);
            else if (pat == PAT_BFS) add_op(s, c, OP_A, (uint32_t)c, 1);        /* 라인 0 의 워드 c (k ≤ LINE_WORDS) */
            else add_op(s, c, OP_R, 0, 0);
            if (strict) add_sync(s);                      /* 엄격한 순환: c0, c1, ..., c(k-1), c0, ... 한 사이클에 하나 */
        }
        if (!strict) add_sync(s);                         /* 라운드 단위 배리어만: 라운드 안 순서는 중재기가 정함 */
    }
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static sim_t *S;                                          /* 큰 구조체라 힙에 하나 */

static const char *const TEXTBOOK =
    "# Part A 교과서 트레이스 (모든 연산 사이에 sync → 한 사이클에 연산 하나)\n"
    "c0 R 0\nsync\nc1 R 0\nsync\nc2 W 0 7\nsync\nc0 R 0\nsync\nc0 W 0 8\nsync\n"
    "c3 R 1\nsync\nc3 W 1 5\nsync\nc3 R 2\nsync\nc1 R 8   # 다른 라인: 혼자 읽고\nsync\nc1 W 8 9 # 혼자 쓴다\nsync\n"
    "c2 R 1\nsync\nc1 R 2\n";
static const char *const EXP_MESI[] = {
    "c0 R 0x00 BusRd c0:I->E src=mem",
    "c1 R 0x00 BusRd c1:I->S c0:E->S src=mem",
    "c2 W 0x00=7 BusRdX c2:I->M c0:S->I c1:S->I src=mem",
    "c0 R 0x00 BusRd c0:I->S c2:M->S(Flush) src=c2 mem<-c2",
    "c0 W 0x00=8 BusUpgr c0:S->M c2:S->I",
    "c3 R 0x01 BusRd c3:I->S c0:M->S(Flush) src=c0 mem<-c0",
    "c3 W 0x01=5 BusUpgr c3:S->M c0:S->I",
    "c3 R 0x02 hit",
    "c1 R 0x08 BusRd c1:I->E src=mem",
    "c1 W 0x08=9 hit c1:E->M",
    "c2 R 0x01 BusRd c2:I->S c3:M->S(Flush) src=c3 mem<-c3",
    "c1 R 0x02 BusRd c1:I->S src=mem",
};
static const char *const EXP_MSI[] = {
    "c0 R 0x00 BusRd c0:I->S src=mem",
    "c1 R 0x00 BusRd c1:I->S src=mem",
    "c2 W 0x00=7 BusRdX c2:I->M c0:S->I c1:S->I src=mem",
    "c0 R 0x00 BusRd c0:I->S c2:M->S(Flush) src=c2 mem<-c2",
    "c0 W 0x00=8 BusUpgr c0:S->M c2:S->I",
    "c3 R 0x01 BusRd c3:I->S c0:M->S(Flush) src=c0 mem<-c0",
    "c3 W 0x01=5 BusUpgr c3:S->M c0:S->I",
    "c3 R 0x02 hit",
    "c1 R 0x08 BusRd c1:I->S src=mem",
    "c1 W 0x08=9 BusUpgr c1:S->M",
    "c2 R 0x01 BusRd c2:I->S c3:M->S(Flush) src=c3 mem<-c3",
    "c1 R 0x02 BusRd c1:I->S src=mem",
};
static const char *const EXP_MOESI[] = {
    "c0 R 0x00 BusRd c0:I->E src=mem",
    "c1 R 0x00 BusRd c1:I->S c0:E->S src=mem",
    "c2 W 0x00=7 BusRdX c2:I->M c0:S->I c1:S->I src=mem",
    "c0 R 0x00 BusRd c0:I->S c2:M->O(Flush) src=c2",
    "c0 W 0x00=8 BusUpgr c0:S->M c2:O->I",
    "c3 R 0x01 BusRd c3:I->S c0:M->O(Flush) src=c0",
    "c3 W 0x01=5 BusUpgr c3:S->M c0:O->I",
    "c3 R 0x02 hit",
    "c1 R 0x08 BusRd c1:I->E src=mem",
    "c1 W 0x08=9 hit c1:E->M",
    "c2 R 0x01 BusRd c2:I->S c3:M->O(Flush) src=c3",
    "c1 R 0x02 BusRd c1:I->S c3:O(Flush) src=c3",
};

static int log_match(const sim_t *s, const char *const *exp, int n) {   /* 처음 틀린 줄 번호+1, 다 맞으면 0 */
    for (int i = 0; i < n; i++)
        if (i >= s->nev || strcmp(s->ev[i], exp[i]) != 0) {
            printf("  log[%d] got  \"%s\"\n         want \"%s\"\n", i, i < s->nev ? s->ev[i] : "(none)", exp[i]);
            return i + 1;
        }
    return s->nev == n ? 0 : n + 1;
}
static bool mem_is_gold(sim_t *s) { sim_flush_all(s); return memcmp(s->mem, s->gold, sizeof s->mem) == 0; }

static inv_t run_text(proto_t p, int nsets, int nways, unsigned mut, const char *text) {
    sim_reset(S, p, 4, nsets, nways, mut);
    if (sim_load(S, text) < 0) return INV_HANG;
    return sim_run(S);
}

static void part_a(void) {
    printf("== Part A: 교과서 트레이스 (4 코어, 4 set direct-mapped) ==\n");
    const char *const *exp[3] = {EXP_MSI, EXP_MESI, EXP_MOESI};
    for (int p = 0; p < 3; p++) {
        sim_reset(S, (proto_t)p, 4, 4, 1, 0);
        S->verbose = (p == PR_MESI);
        CHECK(sim_load(S, TEXTBOOK) == 12);
        CHECK(sim_run(S) == INV_OK);
        S->verbose = 0;
        CHECK(log_match(S, exp[p], 12) == 0);
        print_stats(S);
        const stats_t *t = &S->st;
        if (p == PR_MSI)
            CHECK(t->bus[BUS_RD] == 7 && t->bus[BUS_RDX] == 1 && t->bus[BUS_UPGR] == 3 && t->c2c == 3 &&
                  t->memrd == 5 && t->memwb == 3 && t->inval == 4 && t->silent_up == 0 && t->hits == 1);
        if (p == PR_MESI)
            CHECK(t->bus[BUS_RD] == 7 && t->bus[BUS_RDX] == 1 && t->bus[BUS_UPGR] == 2 && t->c2c == 3 &&
                  t->memrd == 5 && t->memwb == 3 && t->inval == 4 && t->silent_up == 1 && t->hits == 2);
        if (p == PR_MOESI)
            CHECK(t->bus[BUS_RD] == 7 && t->bus[BUS_RDX] == 1 && t->bus[BUS_UPGR] == 2 && t->c2c == 4 &&
                  t->memrd == 4 && t->memwb == 0 && t->inval == 4 && t->silent_up == 1 && t->hits == 2);
        if (p == PR_MOESI) CHECK(S->mem[0] == 1000u && S->gold[0] == 8u);  /* O 가 dirty 를 쥐고 메모리는 옛 값 */
        CHECK(mem_is_gold(S));
    }
    /* 축출: direct-mapped 에서 같은 set(0x00 과 0x40) → M 은 BusWB 가 먼저 버스를 쓴다 */
    static const char *const EXP_EVICT[] = {
        "c0 W 0x00=5 BusRdX c0:I->M src=mem",
        "c0 BusWB 0x00 c0:M->I mem<-c0",
        "c0 R 0x40 BusRd c0:I->E src=mem",
        "c0 R 0x01 BusRd c0:I->E drop(0x40:E) src=mem",
        "c1 R 0x00 BusRd c1:I->S c0:E->S src=mem",
    };
    const char *evict = "c0 W 0 5\nc0 R 64\nc0 R 1\nsync\nc1 R 0\n";
    CHECK(run_text(PR_MESI, 4, 1, 0, evict) == INV_OK);
    CHECK(log_match(S, EXP_EVICT, 5) == 0 && S->st.bus[BUS_WB] == 1 && S->cycle == 5);
    CHECK(run_text(PR_MESI, 4, 2, 0, evict) == INV_OK && S->st.bus[BUS_WB] == 0);  /* 2-way: 충돌 없음 */
    /* 파서: 주석·빈 줄·sync·16진수, 잘못된 줄은 -(줄 번호) */
    sim_reset(S, PR_MESI, 4, 4, 1, 0);
    CHECK(sim_load(S, "# c\n\nc0 R 0x10  # 주석\nsync\nc3 A 0x10 2\n") == 2 && S->phase == 1);
    sim_reset(S, PR_MESI, 4, 4, 1, 0); CHECK(sim_load(S, "c0 R 0\nc4 R 0\n") == -2);   /* 코어 범위 밖 */
    sim_reset(S, PR_MESI, 4, 4, 1, 0); CHECK(sim_load(S, "c0 W 5\n") == -1);          /* 값 없음 */
    sim_reset(S, PR_MESI, 4, 4, 1, 0); CHECK(sim_load(S, "c0 R 256\n") == -1);        /* 주소 범위 밖 */
    sim_reset(S, PR_MESI, 4, 4, 1, 0); CHECK(sim_load(S, "c0 X 1\n") == -1);          /* 모르는 연산 */
}

static const struct { unsigned mut; proto_t p; const char *trace; inv_t want; long cyc; } DIRECTED[] = {
    {MUT_NO_INV_UPGR,    PR_MESI, "c0 R 0\nc1 R 0\nsync\nc0 W 0 1\n",          INV_SWMR, 3},
    {MUT_NO_INV_RDX,     PR_MESI, "c0 R 0\nc1 R 0\nsync\nc2 W 0 1\n",          INV_SWMR, 3},
    {MUT_E_WHEN_SHARED,  PR_MESI, "c0 R 0\nsync\nc1 R 0\n",                    INV_SWMR, 2},
    {MUT_SILENT_S_WRITE, PR_MESI, "c0 R 0\nc1 R 0\nsync\nc0 W 0 1\n",          INV_SWMR, 3},
    {MUT_NO_FLUSH_RD,    PR_MESI, "c0 W 0 1\nsync\nc1 R 0\n",                  INV_DATA, 2},
    {MUT_FLUSH_NO_MEMWB, PR_MESI, "c0 W 0 1\nsync\nc1 R 0\n",                  INV_MEM,  2},
    {MUT_EVICT_NO_WB,    PR_MESI, "c0 W 0 1\nsync\nc0 R 64\n",                 INV_MEM,  2},
    {MUT_UPGR_RACE,      PR_MESI, "c0 R 0\nc1 R 0\nsync\nc0 W 0 1\nc1 W 1 2\n", INV_DATA, 4},
};
#define NDIR ((int)(sizeof DIRECTED / sizeof DIRECTED[0]))

static const char *mut_name(unsigned bit) {
    for (int i = 0; i < NMUT; i++) if (MUTANTS[i].bit == bit) return MUTANTS[i].name;
    return "?";
}

static void part_b(void) {
    printf("== Part B: 돌연변이 → 어느 불변식이 몇 사이클에 잡나 ==\n");
    for (int i = 0; i < NDIR; i++) {
        inv_t clean = run_text(DIRECTED[i].p, 4, 1, 0, DIRECTED[i].trace);
        CHECK(clean == INV_OK);                                          /* 올바른 프로토콜은 같은 트레이스 통과 */
        inv_t got = run_text(DIRECTED[i].p, 4, 1, DIRECTED[i].mut, DIRECTED[i].trace);
        printf("  %-15s directed: %-4s cycle %ld  %s\n", mut_name(DIRECTED[i].mut), INV_NAME[got], S->viol_cycle, S->viol_msg);
        CHECK(got == DIRECTED[i].want && S->viol_cycle == DIRECTED[i].cyc);
    }
    /* 랜덤 스트레스: 올바른 프로토콜 3 종 × 캐시 구성 4 종 × 시드 20 개 → 위반 0, 끝난 뒤 메모리 == golden */
    static const int CFG[][3] = { {4, 2, 1}, {4, 4, 2}, {4, 1, 4}, {8, 4, 2} };   /* 코어, set, way */
    long viol = 0, bad_mem = 0, ops = 0, wb = 0, c2c = 0;
    for (int p = 0; p < 3; p++)
        for (int c = 0; c < 4; c++)
            for (uint32_t seed = 1; seed <= 20; seed++) {
                sim_reset(S, (proto_t)p, CFG[c][0], CFG[c][1], CFG[c][2], 0);
                gen_random(S, seed, 2000, 6);
                if (sim_run(S) != INV_OK) { viol++; if (viol == 1) printf("  first violation: %s %s\n", INV_NAME[S->viol], S->viol_msg); }
                if (!mem_is_gold(S)) bad_mem++;
                ops += S->st.ops; wb += S->st.bus[BUS_WB]; c2c += S->st.c2c;
            }
    printf("  random: 240 runs, %ld ops, %ld write-backs, %ld c2c → violations %ld, final mismatch %ld\n", ops, wb, c2c, viol, bad_mem);
    CHECK(viol == 0 && bad_mem == 0 && ops == 240L * 2000 && wb > 0 && c2c > 0);
    /* 같은 랜덤 생성기가 돌연변이 8 개를 전부 잡는가 (시드 1..50 중 첫 검출) */
    int caught = 0;
    for (int i = 0; i < NDIR; i++) {
        uint32_t seed; inv_t got = INV_OK;
        for (seed = 1; seed <= 50 && got == INV_OK; seed++) {
            sim_reset(S, DIRECTED[i].p, 4, 2, 1, DIRECTED[i].mut);
            gen_random(S, seed, 2000, 6);
            got = sim_run(S);
        }
        printf("  %-15s random  : %-4s seed %u cycle %ld\n", mut_name(DIRECTED[i].mut), INV_NAME[got], seed - 1, S->viol_cycle);
        if (got != INV_OK) caught++;
    }
    CHECK(caught == NDIR);
}

static void part_c(void) {
    printf("== Part C: MSI vs MESI vs MOESI, 표준 패턴 a/b/b'/c ==\n");
    /* (1) 사적 데이터: 코어마다 자기 라인을 읽고 쓴다 → E 가 BusUpgr 를 없앤다 */
    long upgr[3], wbk[3];
    for (int p = 0; p < 3; p++) {
        sim_reset(S, (proto_t)p, 4, 4, 1, 0);
        for (int c = 0; c < 4; c++) { add_op(S, c, OP_R, (uint32_t)(c * LINE_WORDS), 0); add_op(S, c, OP_W, (uint32_t)(c * LINE_WORDS), 7); }
        CHECK(sim_run(S) == INV_OK);
        upgr[p] = S->st.bus[BUS_UPGR];
        printf("  private R+W x4   "); print_stats(S);
    }
    CHECK(upgr[PR_MSI] == 4 && upgr[PR_MESI] == 0 && upgr[PR_MOESI] == 0);
    /* (2) 생산자-소비자: c0 쓰고 c1 읽기를 8 번 → O 가 매번의 메모리 write-back 을 없앤다 */
    for (int p = 0; p < 3; p++) {
        sim_reset(S, (proto_t)p, 4, 4, 1, 0);
        for (int r = 0; r < 8; r++) { add_op(S, 0, OP_W, 0, (uint32_t)r); add_sync(S); add_op(S, 1, OP_R, 0, 0); add_sync(S); }
        CHECK(sim_run(S) == INV_OK && mem_is_gold(S));
        wbk[p] = S->st.memwb;
        printf("  prod-cons x8     "); print_stats(S);
        CHECK(S->st.bus[BUS_RDX] == 1 && S->st.bus[BUS_UPGR] == 7 && S->st.bus[BUS_RD] == 8 && S->st.c2c == 8);
    }
    CHECK(wbk[PR_MSI] == 8 && wbk[PR_MESI] == 8 && wbk[PR_MOESI] == 0);
    /* (3) MEM-M04 와 비교할 표준 패턴: k 코어 라운드로빈 100 라운드, 연산당 메시지 수 */
    printf("  pattern (MESI, 100 rounds)   k  ops   bus/op  c2c/op  inval/op  memwb/op\n");
    for (int pat = 0; pat < 4; pat++)
        for (int k = 1; k <= 4; k++) {
            sim_reset(S, PR_MESI, 4, 4, 1, 0);
            gen_pattern(S, (pat_t)pat, k, 100, true);
            CHECK(sim_run(S) == INV_OK);
            const stats_t *t = &S->st;
            long bus = t->bus[BUS_RD] + t->bus[BUS_RDX] + t->bus[BUS_UPGR] + t->bus[BUS_WB];
            printf("  %-27s %2d %4ld   %6.3f  %6.3f  %8.3f  %8.3f\n", PAT_NAME[pat], k, t->ops,
                   (double)bus / (double)t->ops, (double)t->c2c / (double)t->ops,
                   (double)t->inval / (double)t->ops, (double)t->memwb / (double)t->ops);
            long n = 100L * k;
            if (k == 4 && (pat == PAT_A || pat == PAT_BFS))
                CHECK(t->bus[BUS_RDX] == n && t->c2c == n - 1 && t->inval == n - 1 && t->memwb == n - 1 && t->memrd == 1);
            if (k == 4 && pat == PAT_B)
                CHECK(t->bus[BUS_RDX] == 4 && t->c2c == 0 && t->inval == 0 && t->hits == n - 4);
            if (k == 4 && pat == PAT_C)
                CHECK(t->bus[BUS_RD] == 4 && t->memrd == 4 && t->c2c == 0 && t->inval == 0 && t->hits == n - 4);
            if (pat == PAT_A) CHECK(S->gold[0] == 1000u + (uint32_t)n);   /* fetch_add 가 하나도 안 사라짐 */
        }
    sim_reset(S, PR_MOESI, 4, 4, 1, 0);                    /* MOESI 의 a: 이전 소유자 → 다음 소유자 직행, 메모리 0 번 */
    gen_pattern(S, PAT_A, 4, 100, true);
    CHECK(sim_run(S) == INV_OK && S->st.bus[BUS_RDX] == 400 && S->st.c2c == 399 && S->st.memwb == 0);
    printf("  a shared counter, MOESI    4  400   RdX=%ld c2c=%ld memwb=%ld\n", S->st.bus[BUS_RDX], S->st.c2c, S->st.memwb);
    /* 라운드 단위 sync 만 두면: 직전 라운드의 마지막 소유자가 다음 라운드 첫 사이클에 로컬 히트 → (k-1)/k */
    sim_reset(S, PR_MESI, 4, 4, 1, 0);
    gen_pattern(S, PAT_A, 4, 100, false);
    CHECK(sim_run(S) == INV_OK && S->st.bus[BUS_RDX] == 301 && S->st.c2c == 300 && S->st.hits == 99);
    printf("  a, sync per round only     4  400   RdX=%ld c2c=%ld hits=%ld  (%.3f transfers/op)\n",
           S->st.bus[BUS_RDX], S->st.c2c, S->st.hits, (double)S->st.c2c / (double)S->st.ops);
}

static int file_mode(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    static char text[1 << 20];
    size_t n = fread(text, 1, sizeof text - 1, f); fclose(f); text[n] = '\0';
    proto_t p = PR_MESI; unsigned mut = 0;
    if (argc > 2) p = strcmp(argv[2], "msi") == 0 ? PR_MSI : strcmp(argv[2], "moesi") == 0 ? PR_MOESI : PR_MESI;
    for (int i = 0; argc > 3 && i < NMUT; i++) if (strcmp(argv[3], MUTANTS[i].name) == 0) mut = MUTANTS[i].bit;
    sim_reset(S, p, 4, 4, 1, mut);
    long r = sim_load(S, text);
    if (r < 0) { fprintf(stderr, "%s: parse error at line %ld\n", argv[1], -r); return 2; }
    printf("%s, 4 cores, 4 sets x 1 way, %ld ops%s%s\n", PR_NAME[p], r, mut ? ", mutant " : "", mut ? argv[3] : "");
    S->verbose = 1;
    inv_t v = sim_run(S);
    print_stats(S);
    printf("%s\n", v == INV_OK ? "invariants OK every cycle" : "VIOLATION");
    return v == INV_OK ? 0 : 1;
}

int main(int argc, char **argv) {
    S = calloc(1, sizeof *S);
    if (!S) return 2;
    if (argc > 1) { int r = file_mode(argc, argv); free(S); return r; }
    part_a();
    part_b();
    part_c();
    free(S);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
