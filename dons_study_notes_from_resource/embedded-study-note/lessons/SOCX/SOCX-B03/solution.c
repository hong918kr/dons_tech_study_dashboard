/* SOCX-B03 AXI 응답과 배타적 접근: EXOKAY와 Global Exclusive Monitor
 *
 *   Part A  슬레이브 응답(OKAY/EXOKAY/SLVERR/DECERR) + 마스터 ID별 global exclusive monitor
 *           {주소, 크기} 예약, 그리고 마스터가 각 응답을 보고 할 일(master_action).
 *   Part B  두 마스터가 같은 락 변수를 두고 경쟁: 버스 트랜잭션 인터리빙을 전부 열거하고
 *           매 단계 불변식(P1 상호배제, P2 배타 쌍의 원자성, P3 epoch 당 EXOKAY 1번, P4 최종 상태)을 검사.
 *   Part C  고장 난 모니터(mutant) 3종 → 열거기가 찾은 반례 → golden 모델로 기대 응답을 붙인
 *           directed test 로 바꿔 재생한다 (실리콘 검증 흐름).
 *
 * 단일 스레드 모델이다. 슬레이브는 트랜잭션을 한 번에 하나씩 처리하고, 마스터는 응답을 받은 뒤에
 * 다음 트랜잭션을 낸다. 체크는 전부 결정적 (열거 결과의 정확한 개수, 손으로 유도한 값).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= 모델: 주소 맵 · 응답 · 모니터 ======================= */
/* 0x000-0x0FF RAM (배타 지원) | 0x100-0x1FF ROM (쓰기 = SLVERR) | 0x200-0x2FF 주변장치 (배타 미지원)
 * 0x300 이상 = 아무 슬레이브도 없음 → 인터커넥트의 기본 슬레이브가 DECERR */
enum { RAM_BASE = 0x000, ROM_BASE = 0x100, PER_BASE = 0x200, MAP_END = 0x300 };
typedef enum { REG_RAM, REG_ROM, REG_PER, REG_NONE } region_t;
typedef enum { OKAY = 0, EXOKAY = 1, SLVERR = 2, DECERR = 3 } resp_t;   /* xRESP[1:0] 인코딩 */
static const char *const RESP_NAME[] = { "OKAY", "EXOKAY", "SLVERR", "DECERR" };

enum { MAX_ID = 4 };
typedef struct { bool valid; uint32_t addr, size; } resv_t;          /* 한 ID 의 예약 */
typedef struct { resv_t r[MAX_ID]; } monitor_t;                        /* ID 마다 하나 */

/* 일부러 고장 낸 모니터 (Part C). MON_GOOD 이 여러분이 구현하는 정상 모니터. */
typedef enum { MON_GOOD, MON_NO_CLEAR_OTHER, MON_SHARED_RESV, MON_NO_ADDR_CHECK, MON_N } mon_kind_t;
static const char *const MON_NAME[] = { "GOOD", "NO_CLEAR_OTHER", "SHARED_RESV", "NO_ADDR_CHECK" };

typedef struct {
    uint8_t mem[MAP_END];
    uint16_t ver[MAP_END / 4];   /* 하네스 전용 golden: 4 B 워드마다 "실제로 쓰인 횟수" */
    monitor_t mon;
    mon_kind_t kind;
} sys_t;

static void sys_init(sys_t *s, mon_kind_t kind) { memset(s, 0, sizeof *s); s->kind = kind; }

/* 접근 전체가 한 영역 안에 있어야 한다 (모델 단순화: 경계를 넘으면 디코드 실패) */
static region_t region_of(uint32_t addr, uint32_t size) {
    if (size == 0 || addr >= MAP_END || size > MAP_END - addr) return REG_NONE;
    if (addr / 0x100 != (addr + size - 1) / 0x100) return REG_NONE;
    return (region_t)(addr / 0x100);
}
static bool is_pow2(uint32_t x) { return x != 0 && (x & (x - 1)) == 0; }
/* 배타 접근의 모양 규칙: 크기 2의 거듭제곱 · 128 B 이하 · 크기에 정렬 */
static bool excl_shape_ok(uint32_t addr, uint32_t size) {
    return is_pow2(size) && size <= 128 && addr % size == 0;
}
static bool overlap(uint32_t a, uint32_t as, uint32_t b, uint32_t bs) { return a < b + bs && b < a + as; }

/* 데이터 경로: 단일 beat, size 1/2/4/8, 리틀 엔디언 (버스트는 SOCX-B02 참고) */
static uint64_t mem_load(const sys_t *s, uint32_t addr, uint32_t size) {
    uint64_t v = 0;
    for (uint32_t i = 0; i < size && i < 8; i++) v |= (uint64_t)s->mem[addr + i] << (8 * i);
    return v;
}
/* 메모리에 실제로 쓰는 유일한 길. golden 버전 카운터도 여기서 올라간다. */
static void mem_commit(sys_t *s, uint32_t addr, uint32_t size, uint64_t data) {
    for (uint32_t i = 0; i < size && i < 8; i++) s->mem[addr + i] = (uint8_t)(data >> (8 * i));
    for (uint32_t w = addr / 4; w <= (addr + size - 1) / 4; w++) s->ver[w]++;
}

/* ---------------- TODO 1: global exclusive monitor (마스터 ID별 예약) ---------------- */
/* 배타 읽기: 그 ID 의 예약을 {addr, size} 로 설정 (이전 예약은 교체) */
static void mon_excl_read(monitor_t *m, int id, uint32_t addr, uint32_t size) {
    m->r[id] = (resv_t){ true, addr, size };
}
/* 배타 쓰기 성공 조건: 그 ID 의 예약이 살아 있고 주소·크기가 배타 읽기와 같다 */
static bool mon_excl_write_ok(const monitor_t *m, int id, uint32_t addr, uint32_t size) {
    const resv_t *r = &m->r[id];
    return r->valid && r->addr == addr && r->size == size;
}
/* 배타 쓰기를 한 번 처리하면 (성공이든 실패든) 그 ID 의 예약은 끝난다 */
static void mon_clear_id(monitor_t *m, int id) {
    m->r[id].valid = false;
}
/* 메모리에 실제로 쓰기가 일어나면: 그 범위와 겹치는 모든 예약을 지운다 */
static void mon_snoop_write(monitor_t *m, uint32_t addr, uint32_t size) {
    for (int i = 0; i < MAX_ID; i++)
        if (m->r[i].valid && overlap(m->r[i].addr, m->r[i].size, addr, size)) m->r[i].valid = false;
}

/* ---- 하네스가 주는 디스패치: MON_GOOD 은 위 함수 그대로, mutant 는 한 군데씩 고장 ---- */
static void mon_do_excl_read(sys_t *s, int id, uint32_t a, uint32_t sz) {
    mon_excl_read(&s->mon, s->kind == MON_SHARED_RESV ? 0 : id, a, sz);   /* ID 구분 없는 예약 1개 */
}
static bool mon_do_excl_write_ok(const sys_t *s, int id, uint32_t a, uint32_t sz) {
    if (s->kind == MON_SHARED_RESV) return mon_excl_write_ok(&s->mon, 0, a, sz);
    if (s->kind == MON_NO_ADDR_CHECK) return s->mon.r[id].valid;          /* 주소·크기 비교 안 함 */
    return mon_excl_write_ok(&s->mon, id, a, sz);
}
static void mon_do_clear_id(sys_t *s, int id) { mon_clear_id(&s->mon, s->kind == MON_SHARED_RESV ? 0 : id); }
static void mon_do_snoop_write(sys_t *s, uint32_t a, uint32_t sz) {
    if (s->kind == MON_NO_CLEAR_OTHER) return;                             /* 남의 쓰기를 못 봄 */
    mon_snoop_write(&s->mon, a, sz);
}

/* ---------------- TODO 2: 슬레이브 응답 (읽기 / 쓰기) ---------------- */
static resp_t axi_read(sys_t *s, int id, uint32_t addr, uint32_t size, bool excl, uint64_t *data) {
    region_t g = region_of(addr, size);
    *data = 0;
    if (g == REG_NONE) return DECERR;                   /* 디코드 실패: 인터커넥트가 답한다 */
    *data = mem_load(s, addr, size);
    if (!excl) return OKAY;
    if (g == REG_RAM && excl_shape_ok(addr, size)) {    /* 배타 지원 슬레이브: 예약 + EXOKAY */
        mon_do_excl_read(s, id, addr, size);
        return EXOKAY;
    }
    return OKAY;                                        /* 데이터는 유효, 감시는 안 함 = "배타 미지원" */
}
static resp_t axi_write(sys_t *s, int id, uint32_t addr, uint32_t size, bool excl, uint64_t data) {
    region_t g = region_of(addr, size);
    if (g == REG_NONE) return DECERR;
    if (g == REG_ROM) return SLVERR;                    /* 슬레이브까지 갔지만 쓸 수 없는 곳 */
    if (excl && g == REG_RAM) {
        bool ok = excl_shape_ok(addr, size) && mon_do_excl_write_ok(s, id, addr, size);
        mon_do_clear_id(s, id);
        if (!ok) return OKAY;                           /* 배타 실패: 메모리는 그대로 */
        mem_commit(s, addr, size, data);
        mon_do_snoop_write(s, addr, size);
        return EXOKAY;
    }
    mem_commit(s, addr, size, data);                    /* 일반 쓰기 / 배타 미지원 영역의 배타 쓰기 */
    mon_do_snoop_write(s, addr, size);
    return OKAY;
}

/* ---------------- TODO 3: 마스터가 응답을 보고 할 일 ---------------- */
typedef enum { OP_READ, OP_WRITE, OP_EXCL_READ, OP_EXCL_WRITE } op_t;
typedef enum { ACT_SUCCESS, ACT_RETRY, ACT_NOEXCL, ACT_BUSERR } act_t;
static const char *const OP_NAME[] = { "RD  ", "WR  ", "ExRD", "ExWR" };
static act_t master_action(op_t op, resp_t r) {
    if (r == SLVERR || r == DECERR) return ACT_BUSERR;          /* 버스 에러 → 예외(abort) 경로 */
    switch (op) {
    case OP_EXCL_READ:  return r == EXOKAY ? ACT_SUCCESS : ACT_NOEXCL;
    case OP_EXCL_WRITE: return r == EXOKAY ? ACT_SUCCESS : ACT_RETRY;
    default:            return r == OKAY ? ACT_SUCCESS : ACT_BUSERR;  /* 일반 접근에 EXOKAY = 프로토콜 위반 */
    }
}

/* ======================= 테스트 하네스: 마스터 프로그램 + 전수 열거기 ======================= */
enum { NM = 2, MAX_LOG = 48 };
enum { LOCK = 0x000, DATA = 0x008, CNT = 0x010, LOCK_B = 0x040, PER_LOCK = 0x200 };
typedef enum { PROG_TAKE, PROG_LOCK, PROG_COUNTER } prog_t;
typedef enum { P_RX, P_RXB, P_WX, P_CS_R, P_CS_W, P_REL, P_DONE, P_GAVEUP, P_ERR } pc_t;
typedef struct {
    pc_t pc;
    int tries, left, acquired;
    bool interloper;            /* S3: 첫 시도에서 락을 배타 읽기한 뒤 LOCK_B 도 배타 읽기 */
    uint32_t lock;
    uint32_t rx_val, tmp;
    uint16_t rx_ver;
} master_t;
typedef struct { uint8_t m, op, resp; uint16_t addr; uint32_t data; int16_t rv[NM]; } logent_t;
typedef struct {
    sys_t sys;
    prog_t prog;
    int max_try, in_cs, epoch_exok, nlog;
    master_t m[NM];
    logent_t log[MAX_LOG];
    const char *bad;            /* 처음 깨진 불변식 (NULL = 정상) */
} world_t;

static void bad(world_t *w, const char *why) { if (!w->bad) w->bad = why; }
static bool terminal(const master_t *p) { return p->pc >= P_DONE; }
static uint16_t ver_of(const sys_t *s, uint32_t addr) { return s->ver[addr / 4]; }

static void log_tx(world_t *w, int m, op_t op, uint32_t addr, uint32_t data, resp_t r) {
    if (w->nlog >= MAX_LOG) return;
    logent_t *e = &w->log[w->nlog++];
    e->m = (uint8_t)m; e->op = (uint8_t)op; e->resp = (uint8_t)r; e->addr = (uint16_t)addr; e->data = data;
    for (int i = 0; i < NM; i++) e->rv[i] = w->sys.mon.r[i].valid ? (int16_t)w->sys.mon.r[i].addr : -1;
}
static resp_t bus_rd(world_t *w, int m, uint32_t addr, bool excl, uint32_t *v) {
    uint64_t d;
    resp_t r = axi_read(&w->sys, m, addr, 4, excl, &d);
    *v = (uint32_t)d;
    log_tx(w, m, excl ? OP_EXCL_READ : OP_READ, addr, *v, r);
    return r;
}
static resp_t bus_wr(world_t *w, int m, uint32_t addr, bool excl, uint32_t v) {
    resp_t r = axi_write(&w->sys, m, addr, 4, excl, v);
    log_tx(w, m, excl ? OP_EXCL_WRITE : OP_WRITE, addr, v, r);
    return r;
}
static void fail_try(world_t *w, master_t *p) { p->pc = (++p->tries >= w->max_try) ? P_GAVEUP : P_RX; }
static void next_round(master_t *p) { p->tries = 0; p->pc = (--p->left > 0) ? P_RX : P_DONE; }

/* 마스터 한 걸음 = 버스 트랜잭션 정확히 하나 */
static void master_step(world_t *w, int i) {
    master_t *p = &w->m[i];
    uint32_t v;
    act_t a;
    switch (p->pc) {
    case P_RX:                                         /* 락(또는 카운터) 배타 읽기 */
        a = master_action(OP_EXCL_READ, bus_rd(w, i, p->lock, true, &v));
        if (a == ACT_NOEXCL || a == ACT_BUSERR) { p->pc = P_ERR; break; }
        if (a == ACT_RETRY) { fail_try(w, p); break; }
        p->rx_val = v;
        p->rx_ver = ver_of(&w->sys, p->lock);
        if (w->prog != PROG_COUNTER && v != 0) { fail_try(w, p); break; }   /* 누가 잡고 있음 */
        p->pc = (p->interloper && p->tries == 0) ? P_RXB : P_WX;
        break;
    case P_RXB:                                        /* 끼어든 다른 배타 읽기 (같은 ID) */
        bus_rd(w, i, LOCK_B, true, &v);
        p->pc = P_WX;
        break;
    case P_WX: {
        uint16_t before = ver_of(&w->sys, p->lock);
        a = master_action(OP_EXCL_WRITE, bus_wr(w, i, p->lock, true, w->prog == PROG_COUNTER ? p->rx_val + 1 : 1));
        if (a == ACT_RETRY) { fail_try(w, p); break; }
        if (a != ACT_SUCCESS) { p->pc = P_ERR; break; }
        p->acquired++;
        if (w->prog != PROG_COUNTER) {
            if (++w->in_cs > 1) bad(w, "P1 상호배제 위반: 두 마스터가 동시에 락 보유");
            if (++w->epoch_exok > 1) bad(w, "P3 한 락 epoch 에 EXOKAY 두 번");
        }
        if (before != p->rx_ver) bad(w, "P2 원자성: 배타 읽기 뒤 다른 쓰기가 있었는데 EXOKAY");
        if (w->prog == PROG_COUNTER) { next_round(p); break; }
        p->pc = (w->prog == PROG_TAKE) ? P_DONE : P_CS_R;
        break;
    }
    case P_CS_R: bus_rd(w, i, DATA, false, &p->tmp); p->pc = P_CS_W; break;      /* 임계 구역: data++ */
    case P_CS_W: bus_wr(w, i, DATA, false, p->tmp + 1); p->pc = P_REL; break;
    case P_REL:                                        /* 해제 = 일반 쓰기 0 */
        w->in_cs--;
        w->epoch_exok = 0;
        bus_wr(w, i, p->lock, false, 0);
        next_round(p);
        break;
    default: break;
    }
}

static void leaf_check(world_t *w) {
    int total = 0;
    for (int i = 0; i < NM; i++) total += w->m[i].acquired;
    uint32_t lockv = (uint32_t)mem_load(&w->sys, w->m[0].lock, 4);
    if (w->prog == PROG_TAKE) {
        if (lockv != 0 && total != 1) bad(w, "P4 락이 잡혔는데 보유자가 정확히 1명이 아님");
    } else if (w->prog == PROG_LOCK) {
        if (lockv != 0) bad(w, "P4 모두 끝났는데 락이 0 이 아님");
        if (mem_load(&w->sys, DATA, 4) != (uint64_t)total) bad(w, "P4 data != 임계 구역 진입 수 (lost update)");
    } else if (lockv != (uint32_t)total) {
        bad(w, "P4 카운터 != 성공한 증가 수 (lost update)");
    }
}

typedef enum { S0_TAKE, S1_LOCK, S2_COUNTER, S3_INTERLOPER, S4_NOEXCL, S_N } scen_t;
static const char *const SCEN_NAME[] = { "S0-take", "S1-lock", "S2-counter", "S3-interloper", "S4-noexcl" };

static world_t world_new(mon_kind_t kind, scen_t sc) {
    world_t w;
    memset(&w, 0, sizeof w);
    sys_init(&w.sys, kind);
    w.prog = sc == S0_TAKE ? PROG_TAKE : sc == S2_COUNTER ? PROG_COUNTER : PROG_LOCK;
    w.max_try = sc == S0_TAKE ? 1 : 3;
    for (int i = 0; i < NM; i++) {
        w.m[i].pc = P_RX;
        w.m[i].left = 1;
        w.m[i].lock = sc == S2_COUNTER ? CNT : sc == S4_NOEXCL ? PER_LOCK : LOCK;
    }
    if (sc == S2_COUNTER) w.m[0].left = 2;      /* M0 은 2번, M1 은 1번 증가 */
    if (sc == S3_INTERLOPER) w.m[1].interloper = true;
    return w;
}

typedef struct {
    long leaves, bad_paths, no_progress;
    int cex_len;                                /* 가장 짧은 반례 (DFS 순서상 처음 것) */
    logent_t cex[MAX_LOG];
    const char *cex_why;
} result_t;

static void record_bad(result_t *res, const world_t *w) {
    res->bad_paths++;
    if (res->cex_len == 0 || w->nlog < res->cex_len) {
        res->cex_len = w->nlog;
        memcpy(res->cex, w->log, sizeof w->log);
        res->cex_why = w->bad;
    }
}
static void explore(const world_t *w, result_t *res) {
    bool any = false;
    for (int i = 0; i < NM; i++) {
        if (terminal(&w->m[i])) continue;
        any = true;
        world_t n = *w;
        master_step(&n, i);
        if (n.bad) record_bad(res, &n);         /* 깨진 순간 그 경로는 멈춘다 */
        else explore(&n, res);
    }
    if (any) return;
    world_t n = *w;
    leaf_check(&n);
    if (n.bad) { record_bad(res, &n); return; }
    res->leaves++;
    if (n.m[0].acquired + n.m[1].acquired == 0) res->no_progress++;
}
static result_t run_scen(mon_kind_t kind, scen_t sc) {
    result_t res;
    memset(&res, 0, sizeof res);
    world_t w = world_new(kind, sc);
    explore(&w, &res);
    return res;
}

static void print_trace(const logent_t *t, int n) {
    for (int k = 0; k < n; k++) {
        const logent_t *e = &t[k];
        printf("    %2d. M%d %s [0x%03x]", k + 1, e->m, OP_NAME[e->op], e->addr);
        if (e->op == OP_WRITE || e->op == OP_EXCL_WRITE) printf(" = %-3u", (unsigned)e->data);
        else printf("      ");
        printf(" -> %-6s", RESP_NAME[e->resp]);
        if (e->op == OP_READ || e->op == OP_EXCL_READ) printf(" data=%-3u", (unsigned)e->data);
        else printf("         ");
        printf("  resv:");
        for (int i = 0; i < NM; i++) {
            if (e->rv[i] < 0) printf(" M%d{-}    ", i);
            else printf(" M%d{0x%03x}", i, (unsigned)e->rv[i]);
        }
        printf("%s\n", k == n - 1 ? "   <-- 여기서 깨짐" : "");
    }
}

/* ---- Part C: 반례 스케줄 → golden 모델로 기대 응답을 붙인 directed test → DUT 에서 재생 ---- */
typedef struct { int n; logent_t e[MAX_LOG]; } dtest_t;
static dtest_t make_directed(scen_t sc, const logent_t *cex, int n) {
    world_t w = world_new(MON_GOOD, sc);
    for (int k = 0; k < n && !terminal(&w.m[cex[k].m]); k++) master_step(&w, cex[k].m);
    dtest_t t;
    t.n = w.nlog;
    memcpy(t.e, w.log, sizeof w.log);
    return t;
}
static int run_directed(mon_kind_t kind, const dtest_t *t) {   /* 첫 불일치 위치, 없으면 -1 */
    sys_t s;
    sys_init(&s, kind);
    for (int k = 0; k < t->n; k++) {
        const logent_t *e = &t->e[k];
        bool excl = e->op == OP_EXCL_READ || e->op == OP_EXCL_WRITE;
        if (e->op == OP_READ || e->op == OP_EXCL_READ) {
            uint64_t d;
            resp_t r = axi_read(&s, e->m, e->addr, 4, excl, &d);
            if (r != e->resp || (uint32_t)d != e->data) return k;
        } else if (axi_write(&s, e->m, e->addr, 4, excl, e->data) != e->resp) {
            return k;
        }
    }
    return -1;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
        else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* 짧은 직접 호출용 */
static resp_t rd(sys_t *s, int id, uint32_t a, uint32_t sz, bool ex, uint64_t *d) { return axi_read(s, id, a, sz, ex, d); }
static resp_t wr(sys_t *s, int id, uint32_t a, uint32_t sz, bool ex, uint64_t d) { return axi_write(s, id, a, sz, ex, d); }

int main(void) {
    uint64_t d = 0;
    /* ---------- Part A: 응답 코드와 모니터 단위 테스트 (directed) ---------- */
    {
        sys_t s;
        sys_init(&s, MON_GOOD);
        CHECK(wr(&s, 0, 0x020, 4, false, 0x11223344u) == OKAY);
        CHECK(rd(&s, 1, 0x020, 4, false, &d) == OKAY && d == 0x11223344u);
        CHECK(rd(&s, 0, 0x300, 4, false, &d) == DECERR && d == 0);         /* 맵 밖 */
        CHECK(wr(&s, 0, 0x2FE, 4, false, 1) == DECERR);                    /* 끝을 넘음 */
        CHECK(rd(&s, 0, 0x104, 4, false, &d) == OKAY);                     /* ROM 읽기 */
        CHECK(wr(&s, 0, 0x104, 4, false, 7) == SLVERR && s.mem[0x104] == 0); /* ROM 쓰기: 안 바뀜 */
    }
    {   /* 기본 쌍: ExRD → ExWR 성공, 같은 예약으로 두 번째 ExWR 은 실패 */
        sys_t s;
        sys_init(&s, MON_GOOD);
        CHECK(rd(&s, 0, 0x040, 4, true, &d) == EXOKAY);
        CHECK(wr(&s, 0, 0x040, 4, true, 5) == EXOKAY && mem_load(&s, 0x040, 4) == 5);
        CHECK(wr(&s, 0, 0x040, 4, true, 6) == OKAY && mem_load(&s, 0x040, 4) == 5);
        CHECK(wr(&s, 1, 0x050, 4, true, 9) == OKAY && mem_load(&s, 0x050, 4) == 0);  /* ExRD 없이 */
    }
    {   /* 다른 ID 의 일반 쓰기가 예약을 지운다 / 겹침 판정은 바이트 단위 */
        sys_t s;
        sys_init(&s, MON_GOOD);
        rd(&s, 0, 0x040, 4, true, &d);
        CHECK(wr(&s, 1, 0x040, 4, false, 3) == OKAY);
        CHECK(wr(&s, 0, 0x040, 4, true, 8) == OKAY && mem_load(&s, 0x040, 4) == 3);
        rd(&s, 0, 0x040, 8, true, &d);
        wr(&s, 1, 0x044, 1, false, 0xAA);                                  /* 8 B 예약의 안쪽 1 B */
        CHECK(wr(&s, 0, 0x040, 8, true, 1) == OKAY);
        rd(&s, 0, 0x040, 8, true, &d);
        wr(&s, 1, 0x048, 4, false, 0xBB);                                  /* 바로 옆, 안 겹침 */
        CHECK(wr(&s, 0, 0x040, 8, true, 1) == EXOKAY);
    }
    {   /* 같은 ID 의 새 ExRD 는 예약을 교체 / 크기 불일치는 실패 / ID 끼리는 독립 */
        sys_t s;
        sys_init(&s, MON_GOOD);
        rd(&s, 0, 0x040, 4, true, &d);
        rd(&s, 0, 0x080, 4, true, &d);
        CHECK(wr(&s, 0, 0x040, 4, true, 1) == OKAY);
        rd(&s, 0, 0x040, 4, true, &d);
        rd(&s, 0, 0x080, 4, true, &d);
        CHECK(wr(&s, 0, 0x080, 4, true, 1) == EXOKAY);
        rd(&s, 2, 0x040, 4, true, &d);
        CHECK(wr(&s, 2, 0x040, 2, true, 1) == OKAY);                       /* 크기 다름 */
        rd(&s, 0, 0x040, 4, true, &d);
        rd(&s, 1, 0x040, 4, true, &d);
        CHECK(wr(&s, 1, 0x040, 4, true, 2) == EXOKAY);                     /* 먼저 쓴 쪽이 이김 */
        CHECK(wr(&s, 0, 0x040, 4, true, 3) == OKAY && mem_load(&s, 0x040, 4) == 2);
        rd(&s, 0, 0x060, 4, true, &d);
        rd(&s, 1, 0x070, 4, true, &d);
        CHECK(wr(&s, 1, 0x070, 4, true, 1) == EXOKAY);
        CHECK(wr(&s, 0, 0x060, 4, true, 1) == EXOKAY);                     /* 다른 주소: 영향 없음 */
    }
    {   /* 배타 미지원 영역 · 모양 규칙 위반 */
        sys_t s;
        sys_init(&s, MON_GOOD);
        wr(&s, 0, 0x210, 4, false, 42);
        CHECK(rd(&s, 0, 0x210, 4, true, &d) == OKAY && d == 42);           /* 데이터 유효, 감시 X */
        CHECK(wr(&s, 0, 0x210, 4, true, 43) == OKAY && mem_load(&s, 0x210, 4) == 43); /* 그냥 써진다 */
        CHECK(rd(&s, 0, 0x110, 4, true, &d) == OKAY);                      /* ROM 도 배타 미지원 */
        CHECK(wr(&s, 0, 0x110, 4, true, 1) == SLVERR);
        CHECK(rd(&s, 0, 0x310, 4, true, &d) == DECERR);
        CHECK(rd(&s, 0, 0x042, 4, true, &d) == OKAY);                      /* 정렬 안 됨 → 감시 안 함 */
        CHECK(wr(&s, 0, 0x042, 4, true, 0x55) == OKAY && mem_load(&s, 0x042, 4) == 0);
    }
    {   /* 마스터 쪽 응답 처리표 */
        static const act_t want[4][4] = {
            /*            OKAY        EXOKAY       SLVERR      DECERR */
            [OP_READ]       = { ACT_SUCCESS, ACT_BUSERR,  ACT_BUSERR, ACT_BUSERR },
            [OP_WRITE]      = { ACT_SUCCESS, ACT_BUSERR,  ACT_BUSERR, ACT_BUSERR },
            [OP_EXCL_READ]  = { ACT_NOEXCL,  ACT_SUCCESS, ACT_BUSERR, ACT_BUSERR },
            [OP_EXCL_WRITE] = { ACT_RETRY,   ACT_SUCCESS, ACT_BUSERR, ACT_BUSERR },
        };
        int ok = 0;
        for (int o = 0; o < 4; o++)
            for (int r = 0; r < 4; r++) ok += master_action((op_t)o, (resp_t)r) == want[o][r];
        CHECK(ok == 16);
    }

    /* ---------- Part B: 두 마스터 인터리빙 전수 열거 ---------- */
    result_t r0 = run_scen(MON_GOOD, S0_TAKE);
    CHECK(r0.leaves == 6 && r0.bad_paths == 0);            /* 손으로 센 값: 6가지, 매번 정확히 1명 */
    result_t r0b = run_scen(MON_NO_CLEAR_OTHER, S0_TAKE);
    CHECK(r0b.leaves == 2 && r0b.bad_paths == 4);          /* 둘 다 0 을 읽은 4가지에서 둘 다 EXOKAY */
    result_t rs[S_N];
    for (int sc = S1_LOCK; sc < S_N; sc++) rs[sc] = run_scen(MON_GOOD, (scen_t)sc);
    printf("info: GOOD monitor  S1-lock %ld paths, S2-counter %ld, S3-interloper %ld, S4-noexcl %ld\n",
           rs[S1_LOCK].leaves, rs[S2_COUNTER].leaves, rs[S3_INTERLOPER].leaves, rs[S4_NOEXCL].leaves);
    CHECK(rs[S1_LOCK].bad_paths == 0 && rs[S1_LOCK].leaves == 120);     /* 회귀값 (solution 실행) */
    CHECK(rs[S1_LOCK].no_progress == 0);                   /* 매 경로에서 누군가는 락을 얻는다 */
    CHECK(rs[S2_COUNTER].bad_paths == 0 && rs[S2_COUNTER].leaves == 29);
    CHECK(rs[S3_INTERLOPER].bad_paths == 0 && rs[S3_INTERLOPER].leaves == 360);
    CHECK(rs[S4_NOEXCL].leaves == 2 && rs[S4_NOEXCL].no_progress == 2); /* 배타 미지원 → 둘 다 에러 */

    /* ---------- Part C: mutant kill matrix + 반례 → directed test ---------- */
    static const bool want_kill[MON_N][3] = {    /* [모니터][S1, S2, S3] */
        [MON_GOOD]           = { false, false, false },
        [MON_NO_CLEAR_OTHER] = { true,  true,  true  },
        [MON_SHARED_RESV]    = { false, true,  false },
        [MON_NO_ADDR_CHECK]  = { false, false, true  },
    };
    int matrix_ok = 0, dt_ok = 0;
    static result_t km[MON_N][3];
    printf("info: kill matrix (위반 경로 / 전체 경로)\n");
    for (int k = 0; k < MON_N; k++) {
        printf("  %-15s", MON_NAME[k]);
        for (int c = 0; c < 3; c++) {
            km[k][c] = run_scen((mon_kind_t)k, (scen_t)(S1_LOCK + c));
            printf("  %s %3ld/%-4ld", SCEN_NAME[S1_LOCK + c], km[k][c].bad_paths, km[k][c].bad_paths + km[k][c].leaves);
            matrix_ok += (km[k][c].bad_paths > 0) == want_kill[k][c];
        }
        printf("\n");
    }
    for (int k = MON_GOOD; k < MON_N; k++) {           /* GOOD 의 반례가 나오면 = 여러분 모니터의 버그 */
        for (int c = 0; c < 3; c++) {
            const result_t *r = &km[k][c];
            if (r->bad_paths == 0) continue;
            scen_t sc = (scen_t)(S1_LOCK + c);
            printf("info: 반례 %s / %s — %s\n", MON_NAME[k], SCEN_NAME[sc], r->cex_why);
            print_trace(r->cex, r->cex_len);
            if (k == MON_GOOD) break;
            dtest_t t = make_directed(sc, r->cex, r->cex_len);
            int at = run_directed((mon_kind_t)k, &t), good = run_directed(MON_GOOD, &t);
            printf("    directed test (%d 트랜잭션, 기대 응답 = GOOD 모델): GOOD %s, %s 는 %d번째에서 불일치\n",
                   t.n, good < 0 ? "통과" : "실패", MON_NAME[k], at + 1);
            dt_ok += (at >= 0 && good < 0);
            break;                                         /* mutant 마다 첫 반례 하나만 */
        }
    }
    CHECK(matrix_ok == MON_N * 3);
    CHECK(dt_ok == 3);                                     /* mutant 3개 모두 directed test 로 잡힘 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
