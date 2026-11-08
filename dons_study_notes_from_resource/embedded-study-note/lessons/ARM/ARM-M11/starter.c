/* ARM-M11 Cortex-M LDREX/STREX와 Exclusive Monitor — 호스트 모델 (맥에서 결정적으로 실행)
 *
 * 진짜 Thumb-2 코드와 QEMU/gdb 실행은 demo_m3_*.{S,c} + demo_gdb_isr.gdb + run_qemu.sh (ENV: qemu).
 * 이 파일은 그 동작을 C 로 모델링한다: 모델 명령(LDR/STR/OP/LDREX/STREX/CLREX/DMB/CPSID/CPSIE)을 하나
 * 실행할 때마다 tick() 이 "Thread 모드 명령 번호" 를 세고, 주입 목록에 그 번호가 있으면 그 명령 **직전에**
 * 예외(ISR) 또는 외부 버스 마스터 쓰기(DMA/디버거)를 끼워 넣는다 → "k 번째 명령에서 인터럽트" 가 재현된다.
 *
 *   Part A  로컬 배타적 모니터(local exclusive monitor): LDREX/STREX/CLREX + 예외 진입/복귀 시 클리어
 *           정책 3가지: MON_V7M (ARMv7-M 규칙), MON_NOCLEAR (예외가 모니터를 안 건드림 = A-profile 처럼
 *           OS 가 CLREX 해야 하는 모델), MON_QEMU (QEMU TCG: 주소 + "LDREX 때 읽은 값" 비교, 예외 시 클리어)
 *   Part B  atomic_inc / cas(strong) / try_lock·unlock(+DMB) 을 모델 명령으로, 모든 k 에 ISR 주입
 *   Part C  센서 드라이버: ISR 은 try_lock 실패 시 기다리지 않고 deferred 카운트만 올린다 → main 이 처리
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum { MON_V7M, MON_NOCLEAR, MON_QEMU } mon_policy_t;
typedef enum { INJ_ISR, INJ_EXT } inj_kind_t;

typedef struct cpu cpu_t;
typedef void (*isr_fn)(cpu_t *);
typedef struct { uint32_t at; inj_kind_t kind; uint32_t *p; uint32_t delta; } inject_t;

struct cpu {
    mon_policy_t policy;
    bool excl;                  /* 로컬 모니터: true = Exclusive Access, false = Open Access */
    const uint32_t *tag;        /* Exclusive 일 때 표시된 주소 */
    uint32_t tag_val;           /* LDREX 가 읽은 값 (MON_QEMU 만 사용) */
    uint32_t tstep;             /* Thread 모드에서 실행한 모델 명령 수 */
    inject_t inj[8];
    unsigned n_inj, next;
    isr_fn isr;
    bool in_isr, primask, pending;
    uint32_t pend_step, max_latency;
    uint32_t n_ldrex, n_strex_ok, n_strex_fail, n_clrex, n_dmb, n_exc, n_ext;
    char trace[64];             /* Thread 모드 명령 기록: X=LDREX Y=STREX C=CLREX D=DMB L=LDR S=STR O=OP */
    unsigned tlen;
};

/* ======================= Part A — 로컬 배타적 모니터 ======================= */

/* LDREX: 값을 읽고 모니터를 Exclusive(p) 로. MON_QEMU 는 읽은 값도 기억한다. n_ldrex 를 센다. */
static uint32_t mon_ldrex(cpu_t *c, uint32_t *p) {
    (void)c;
    return *p; /* TODO */
}

/* STREX: 성공 조건 = Exclusive 이고 표시 주소 == p (MON_QEMU 는 추가로 *p == tag_val).
 * 성공이면 *p = v 하고 0, 실패면 쓰지 않고 1. 어느 쪽이든 끝나면 모니터는 Open.
 * n_strex_ok / n_strex_fail 을 센다. */
static uint32_t mon_strex(cpu_t *c, uint32_t *p, uint32_t v) {
    (void)c; (void)p; (void)v;
    return 1u; /* TODO */
}

/* CLREX: 모니터를 Open 으로. n_clrex 를 센다. */
static void mon_clrex(cpu_t *c) {
    (void)c; /* TODO */
}

/* 예외 진입 / 복귀: MON_V7M 과 MON_QEMU 는 로컬 모니터를 Open 으로 (ARMv7-M A3.4.4, QEMU m_helper.c).
 * MON_NOCLEAR 는 아무것도 하지 않는다. */
static void exc_entry(cpu_t *c) {
    (void)c; /* TODO */
}
static void exc_exit(cpu_t *c) {
    (void)c; /* TODO */
}

/* ---------- 모델 CPU: 주입기 + 명령 (주어진 코드) ---------- */
#if defined(__GNUC__)
#define MODEL_INSN __attribute__((unused))   /* 시작 코드에서 아직 안 쓰는 명령도 경고 없이 */
#else
#define MODEL_INSN
#endif
static void take_exception(cpu_t *c) {
    exc_entry(c);
    c->in_isr = true;
    c->n_exc++;
    if (c->isr) c->isr(c);
    c->in_isr = false;
    exc_exit(c);
}
static void fire(cpu_t *c, const inject_t *j) {
    if (j->kind == INJ_EXT) { *j->p += j->delta; c->n_ext++; return; }  /* 다른 버스 마스터: 예외 아님 */
    if (c->primask) {                                                  /* PRIMASK=1: pend 만, 나중에 처리 */
        if (!c->pending) { c->pending = true; c->pend_step = c->tstep; }
        return;
    }
    take_exception(c);
}
static void tick(cpu_t *c, char op) {
    if (c->in_isr) return;                                 /* ISR 안의 명령은 세지 않는다 (중첩 없음) */
    while (c->next < c->n_inj && c->inj[c->next].at == c->tstep) fire(c, &c->inj[c->next++]);
    if (c->tlen + 1 < sizeof c->trace) { c->trace[c->tlen++] = op; c->trace[c->tlen] = 0; }
    c->tstep++;
}
static MODEL_INSN uint32_t LDR(cpu_t *c, uint32_t *p) { tick(c, 'L'); return *p; }
static MODEL_INSN void STR(cpu_t *c, uint32_t *p, uint32_t v) { tick(c, 'S'); *p = v; }
static MODEL_INSN void OP(cpu_t *c) { tick(c, 'O'); }
static MODEL_INSN uint32_t LDREX(cpu_t *c, uint32_t *p) { tick(c, 'X'); return mon_ldrex(c, p); }
static MODEL_INSN uint32_t STREX(cpu_t *c, uint32_t *p, uint32_t v) { tick(c, 'Y'); return mon_strex(c, p, v); }
static MODEL_INSN void CLREX(cpu_t *c) { tick(c, 'C'); mon_clrex(c); }
static MODEL_INSN void DMB(cpu_t *c) { tick(c, 'D'); c->n_dmb++; }

static void cpu_reset(cpu_t *c, mon_policy_t pol, isr_fn isr) {
    memset(c, 0, sizeof *c);
    c->policy = pol;
    c->isr = isr;
}
static void add_inj(cpu_t *c, uint32_t at, inj_kind_t kind, uint32_t *p, uint32_t delta) {
    if (c->n_inj < 8u) c->inj[c->n_inj++] = (inject_t){ at, kind, p, delta };
}
/* main 이 끝난 뒤에야 오는 주입(k 가 실행 길이보다 큼)은 여기서 처리 */
static void finish(cpu_t *c) {
    while (c->next < c->n_inj) fire(c, &c->inj[c->next++]);
}

/* ======================= Part B — 세 프리미티브 ======================= */

/* *p += 1 을 원자적으로, 새 값을 반환. 모델 명령 순서 (재시도마다 반복): LDREX, OP(add), STREX.
 * STREX 가 1 이면 처음(LDREX)부터 다시. */
static uint32_t atomic_inc(cpu_t *c, uint32_t *p) {
    (void)c; (void)p;
    return 0u; /* TODO */
}

/* strong CAS: *p == expected 면 desired 를 쓴다. 읽은 옛 값을 반환. 순서: LDREX, OP(cmp),
 * (같으면) STREX — 실패하면 LDREX 부터 다시 / (다르면) CLREX 하고 반환. */
static uint32_t cas(cpu_t *c, uint32_t *p, uint32_t expected, uint32_t desired) {
    (void)c; (void)p; (void)expected; (void)desired;
    return 0u; /* TODO */
}

/* 기다리지 않는 잠금: 0 이면 1 로 바꾸고 true. 순서: LDREX, OP(cmp), (0 이 아니면) CLREX → false /
 * (0 이면) STREX — 실패면 LDREX 부터, 성공이면 DMB(acquire) → true. */
static bool try_lock(cpu_t *c, uint32_t *lock) {
    (void)c; (void)lock;
    return false; /* TODO */
}
/* 해제: DMB(release) 다음 STR 0. */
static void unlock(cpu_t *c, uint32_t *lock) {
    (void)c; (void)lock; /* TODO */
}

/* ======================= Part C — 센서 드라이버 (ISR ↔ main) ======================= */
typedef struct {
    uint32_t lock, gain, offset;   /* 설정: offset == gain*10 이어야 함 (두 필드를 함께 바꾼다) */
    uint32_t deferred;             /* 락을 못 잡은 ISR 이 남긴 "처리할 샘플 수" */
    uint32_t isr_calls, processed, torn;   /* 장부 (모델 명령 아님) */
} drv_t;
static drv_t g_drv;

static void process_sample(cpu_t *c) {   /* 설정을 읽어 샘플 하나 처리 (락을 잡은 상태에서) */
    uint32_t g = LDR(c, &g_drv.gain);
    uint32_t o = LDR(c, &g_drv.offset);
    OP(c);
    if (o != g * 10u) g_drv.torn++;
    g_drv.processed++;
}

/* 샘플 준비 ISR: try_lock 성공이면 바로 처리 후 unlock, 실패면 deferred 를 atomic_inc 하고 끝.
 * (ISR 이 main 이 잡은 락을 기다리면 영원히 끝나지 않는다 — 단일 코어) */
static void drv_isr(cpu_t *c) {
    g_drv.isr_calls++;
    (void)c; /* TODO */
}

/* main: 락 안에서 gain, offset(=gain*10) 을 쓰고 unlock. 그다음 deferred 를 cas 로 0 으로 가져와
 * 그 수만큼 (락을 잡고) process_sample. deferred 가 0 이 될 때까지 반복한다.
 * try_lock 은 최대 8 번만 시도 (단일 코어에서 main 이 실패할 일은 없지만, 무한 루프 방지). */
static void drv_set_gain(cpu_t *c, uint32_t gain) {
    (void)c; (void)gain; /* TODO */
}

/* ======================= 테스트 하네스 ======================= */
static unsigned g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) g_pass++; \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

static uint32_t g_cnt;
static void isr_atomic_inc(cpu_t *c) { (void)atomic_inc(c, &g_cnt); }
static void isr_plain_inc(cpu_t *c) { uint32_t v = LDR(c, &g_cnt); OP(c); STR(c, &g_cnt, v + 1u); }
static void isr_nothing(cpu_t *c) { OP(c); }
static void drv_isr_nolock(cpu_t *c) { g_drv.isr_calls++; process_sample(c); }   /* 틀린 ISR: 락 무시 */
static uint32_t g_lk, g_isr_got;
static void isr_trylock(cpu_t *c) { if (try_lock(c, &g_lk)) { g_isr_got++; unlock(c, &g_lk); } }

/* main 이 atomic_inc 한 번, k 번째 명령 직전에 ISR 한 번. 최종 g_cnt 와 STREX 실패 수를 돌려준다. */
static uint32_t run_inc(mon_policy_t pol, isr_fn isr, uint32_t k, uint32_t *ret, uint32_t *fails) {
    cpu_t c;
    cpu_reset(&c, pol, isr);
    g_cnt = 0u;
    add_inj(&c, k, INJ_ISR, NULL, 0u);
    uint32_t r = atomic_inc(&c, &g_cnt);
    finish(&c);
    if (ret) *ret = r;
    if (fails) *fails = c.n_strex_fail;
    return g_cnt;
}

static void test_part_a(void) {
    cpu_t c;
    uint32_t x = 5u, y = 9u;
    cpu_reset(&c, MON_V7M, NULL);
    CHECK(mon_ldrex(&c, &x) == 5u && c.excl && c.tag == &x);
    CHECK(mon_strex(&c, &x, 6u) == 0u && x == 6u && !c.excl);   /* 성공 → Open */
    CHECK(mon_strex(&c, &x, 7u) == 1u && x == 6u);               /* LDREX 없이 STREX → 실패 */
    (void)mon_ldrex(&c, &x);
    CHECK(mon_strex(&c, &y, 1u) == 1u && y == 9u && !c.excl);   /* 다른 주소 → 실패 (모델 선택) */
    (void)mon_ldrex(&c, &x);
    mon_clrex(&c);
    CHECK(!c.excl && mon_strex(&c, &x, 8u) == 1u && x == 6u && c.n_clrex == 1u);
    (void)mon_ldrex(&c, &x);
    exc_entry(&c);
    CHECK(!c.excl);                                               /* V7M: 예외 진입이 클리어 */
    cpu_reset(&c, MON_NOCLEAR, NULL);
    (void)mon_ldrex(&c, &x);
    exc_entry(&c); exc_exit(&c);
    CHECK(c.excl && mon_strex(&c, &x, 10u) == 0u && x == 10u);   /* NOCLEAR: 살아남음 */
    cpu_reset(&c, MON_QEMU, NULL);
    (void)mon_ldrex(&c, &x);
    x = 77u;                                                      /* 다른 마스터가 값을 바꿈 */
    CHECK(mon_strex(&c, &x, 11u) == 1u && x == 77u);              /* QEMU: 값 비교로 실패 */
    (void)mon_ldrex(&c, &x);
    exc_exit(&c);
    CHECK(!c.excl && c.n_ldrex == 2u && c.n_strex_ok == 0u && c.n_strex_fail == 1u);
}

static void test_part_b(void) {
    uint32_t r, f, fin[5], fails[5], rets[5];
    /* B1: ARMv7-M, ISR 도 atomic_inc. 명령 0=LDREX 1=OP 2=STREX (재시도 3..5) */
    for (uint32_t k = 0; k < 5u; k++) fin[k] = run_inc(MON_V7M, isr_atomic_inc, k, &rets[k], &fails[k]);
    CHECK(fin[0] == 2u && fin[1] == 2u && fin[2] == 2u && fin[3] == 2u && fin[4] == 2u);
    CHECK(fails[0] == 0u && fails[1] == 1u && fails[2] == 1u && fails[3] == 0u && fails[4] == 0u);
    CHECK(rets[0] == 2u && rets[1] == 2u && rets[2] == 2u && rets[3] == 1u && rets[4] == 1u);
    /* B2: 예외가 모니터를 안 지우고(NOCLEAR) ISR 은 평범한 ldr/add/str → k=1,2 에서 lost update */
    for (uint32_t k = 0; k < 5u; k++) fin[k] = run_inc(MON_NOCLEAR, isr_plain_inc, k, NULL, &fails[k]);
    CHECK(fin[0] == 2u && fin[1] == 1u && fin[2] == 1u && fin[3] == 2u && fin[4] == 2u);
    CHECK(fails[1] == 0u && fails[2] == 0u);
    /* B3: 같은 ISR 이라도 ARMv7-M 규칙이면 안전 / B4: NOCLEAR 라도 ISR 이 STREX 를 쓰면 안전 */
    bool ok3 = true, ok4 = true;
    for (uint32_t k = 0; k < 5u; k++) {
        ok3 = ok3 && run_inc(MON_V7M, isr_plain_inc, k, NULL, NULL) == 2u;
        ok4 = ok4 && run_inc(MON_NOCLEAR, isr_atomic_inc, k, NULL, NULL) == 2u;
    }
    CHECK(ok3);
    CHECK(ok4);
    /* B5: ISR 이 카운터를 안 건드려도 V7M·QEMU 는 STREX 실패 (예외 = 모니터 클리어), NOCLEAR 는 성공 */
    (void)run_inc(MON_V7M, isr_nothing, 1u, NULL, &f);     CHECK(f == 1u);
    (void)run_inc(MON_QEMU, isr_nothing, 1u, NULL, &f);    CHECK(f == 1u);
    (void)run_inc(MON_NOCLEAR, isr_nothing, 1u, NULL, &f); CHECK(f == 0u);
    /* B6: 예외가 아닌 "다른 마스터 쓰기" (DMA/디버거) +100 이 LDREX~STREX 사이에 */
    cpu_t c;
    cpu_reset(&c, MON_V7M, NULL); g_cnt = 0u; add_inj(&c, 1u, INJ_EXT, &g_cnt, 100u);
    r = atomic_inc(&c, &g_cnt);
    CHECK(r == 1u && g_cnt == 1u && c.n_strex_fail == 0u);    /* 로컬 모니터는 못 본다 → +100 유실 */
    cpu_reset(&c, MON_QEMU, NULL); g_cnt = 0u; add_inj(&c, 1u, INJ_EXT, &g_cnt, 100u);
    r = atomic_inc(&c, &g_cnt);
    CHECK(r == 101u && g_cnt == 101u && c.n_strex_fail == 1u); /* QEMU 는 값 비교로 잡는다 */
    cpu_reset(&c, MON_QEMU, NULL); g_cnt = 0u; add_inj(&c, 1u, INJ_EXT, &g_cnt, 0u);
    r = atomic_inc(&c, &g_cnt);
    CHECK(r == 1u && c.n_strex_fail == 0u);                    /* 같은 값 쓰기는 QEMU 도 통과 */
    /* B7: CAS */
    cpu_reset(&c, MON_V7M, NULL); g_cnt = 3u;
    CHECK(cas(&c, &g_cnt, 3u, 9u) == 3u && g_cnt == 9u && c.n_clrex == 0u);
    CHECK(cas(&c, &g_cnt, 3u, 1u) == 9u && g_cnt == 9u && c.n_clrex == 1u && !c.excl);
    cpu_reset(&c, MON_V7M, isr_atomic_inc); g_cnt = 0u; add_inj(&c, 1u, INJ_ISR, NULL, 0u);
    r = cas(&c, &g_cnt, 0u, 5u);                               /* ISR 이 0→1 로 바꿈 → 재시도 후 불일치 */
    CHECK(r == 1u && g_cnt == 1u && c.n_strex_fail == 1u && c.n_clrex == 1u);
    /* B8: try_lock / unlock 과 DMB 위치 */
    uint32_t lk = 0u;
    cpu_reset(&c, MON_V7M, NULL);
    CHECK(try_lock(&c, &lk) && lk == 1u && strcmp(c.trace, "XOYD") == 0);
    CHECK(!try_lock(&c, &lk) && lk == 1u && c.n_clrex == 1u && !c.excl && c.n_dmb == 1u);
    c.tlen = 0u; c.trace[0] = 0;
    unlock(&c, &lk);
    CHECK(lk == 0u && strcmp(c.trace, "DS") == 0);
    /* B9: try_lock 의 LDREX~STREX 사이(k=2)에 ISR 이 같은 락을 잡았다 놓음 → main 재시도 후 획득 */
    cpu_reset(&c, MON_V7M, isr_trylock); g_lk = 0u; g_isr_got = 0u; add_inj(&c, 2u, INJ_ISR, NULL, 0u);
    CHECK(try_lock(&c, &g_lk) && g_lk == 1u && g_isr_got == 1u && c.n_strex_fail == 1u);
}

static uint32_t g_k_torn;
static void run_drv(isr_fn isr, const uint32_t *ks, unsigned nk) {
    cpu_t c;
    cpu_reset(&c, MON_V7M, isr);
    memset(&g_drv, 0, sizeof g_drv);
    g_drv.gain = 1u; g_drv.offset = 10u;
    for (unsigned i = 0; i < nk; i++) add_inj(&c, ks[i], INJ_ISR, NULL, 0u);
    drv_set_gain(&c, 7u);
    finish(&c);
}
static bool drv_ok(unsigned expect_calls) {
    return g_drv.torn == 0u && g_drv.processed == expect_calls && g_drv.isr_calls == expect_calls &&
           g_drv.deferred == 0u && g_drv.lock == 0u && g_drv.gain == 7u && g_drv.offset == 70u;
}

static void test_part_c(void) {
    /* 단위: 주입 없음(k=99 → main 이 끝난 뒤 ISR) / main 이 락을 쥔 동안(k=5: gain 과 offset 사이) ISR → deferred 경로 */
    uint32_t k = 99u;
    run_drv(drv_isr, &k, 1u);
    CHECK(drv_ok(1u));
    k = 5u;
    run_drv(drv_isr, &k, 1u);
    CHECK(drv_ok(1u));
    bool unit_ok = (g_pass == g_total);
    if (!unit_ok) { printf("SKIP 3 sweep checks (Part C): unit checks failed\n"); g_total += 3u; return; }
    /* 스윕 1: ISR 하나를 k = 0..39 모든 위치에 */
    bool all1 = true;
    for (k = 0; k < 40u; k++) { run_drv(drv_isr, &k, 1u); all1 = all1 && drv_ok(1u); }
    CHECK(all1);
    /* 스윕 2: ISR 둘 (k1 < k2), 처리 중 다시 deferred 가 생기는 경우 포함 */
    bool all2 = true;
    for (uint32_t a = 0; a < 40u; a++)
        for (uint32_t b = a + 1u; b < 40u; b++) {
            uint32_t ks[2] = { a, b };
            run_drv(drv_isr, ks, 2u);
            all2 = all2 && drv_ok(2u);
        }
    CHECK(all2);
    /* 대조: 락을 무시하는 ISR 은 gain 과 offset 사이(k)에서 찢어진 설정을 본다 */
    g_k_torn = 0u;
    for (k = 0; k < 40u; k++) { run_drv(drv_isr_nolock, &k, 1u); if (g_drv.torn) g_k_torn++; }
    CHECK(g_k_torn == 1u);
}

int main(void) {
    test_part_a();
    unsigned a_pass = g_pass, a_total = g_total;
    if (a_pass != a_total) {
        printf("SKIP Part B/C: Part A (monitor) checks failed\n");
        g_total += 20u + 5u;
    } else {
        test_part_b();
        test_part_c();
    }
    printf("%u/%u passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
