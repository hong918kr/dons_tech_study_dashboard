/* ARM-M11 추가 연습 — 한 번에 검증 (solution.c 의 모니터 모델 + 세 프리미티브를 그대로 포함)
 *   연습 1  A-profile 식 컨텍스트 스위치: 예외가 모니터를 안 지우면 OS 가 CLREX 해야 한다
 *   연습 2  atomic_or — 이벤트 플래그 (ISR 과 main 이 서로 다른 비트를 세운다)
 *   연습 3  재시도 상한 + PRIMASK 임계 구역 폴백 (인터럽트 폭주 / ARMv6-M)
 *   연습 4  sem_try_take — 0 이면 실패하는 감소 (FreeRTOS xSemaphoreTakeFromISR 의 핵심)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
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
    uint32_t v = *p;
    c->excl = true;
    c->tag = p;
    c->tag_val = v;
    c->n_ldrex++;
    return v;
}

/* STREX: 성공 조건 = Exclusive 이고 표시 주소 == p (MON_QEMU 는 추가로 *p == tag_val).
 * 성공이면 *p = v 하고 0, 실패면 쓰지 않고 1. 어느 쪽이든 끝나면 모니터는 Open.
 * n_strex_ok / n_strex_fail 을 센다. */
static uint32_t mon_strex(cpu_t *c, uint32_t *p, uint32_t v) {
    bool ok = c->excl && c->tag == p;
    if (ok && c->policy == MON_QEMU) ok = (*p == c->tag_val);
    if (ok) *p = v;
    c->excl = false;
    if (ok) c->n_strex_ok++; else c->n_strex_fail++;
    return ok ? 0u : 1u;
}

/* CLREX: 모니터를 Open 으로. n_clrex 를 센다. */
static void mon_clrex(cpu_t *c) {
    c->excl = false;
    c->n_clrex++;
}

/* 예외 진입 / 복귀: MON_V7M 과 MON_QEMU 는 로컬 모니터를 Open 으로 (ARMv7-M A3.4.4, QEMU m_helper.c).
 * MON_NOCLEAR 는 아무것도 하지 않는다. */
static void exc_entry(cpu_t *c) {
    if (c->policy != MON_NOCLEAR) c->excl = false;
}
static void exc_exit(cpu_t *c) {
    if (c->policy != MON_NOCLEAR) c->excl = false;
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
/* PRIMASK: CPSID 뒤로는 주입된 예외가 pend 만 되고, CPSIE 에서 (한 번) 처리된다. 지연(명령 수)을 기록. */
static void take_exception(cpu_t *c);
static MODEL_INSN void CPSID(cpu_t *c) { tick(c, 'I'); c->primask = true; }
static MODEL_INSN void CPSIE(cpu_t *c) {
    tick(c, 'E');
    c->primask = false;
    if (c->pending) {
        uint32_t lat = c->tstep - c->pend_step;
        if (lat > c->max_latency) c->max_latency = lat;
        c->pending = false;
        take_exception(c);
    }
}

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
    for (;;) {
        uint32_t v = LDREX(c, p) + 1u;
        OP(c);
        if (STREX(c, p, v) == 0u) return v;
    }
}

/* strong CAS: *p == expected 면 desired 를 쓴다. 읽은 옛 값을 반환. 순서: LDREX, OP(cmp),
 * (같으면) STREX — 실패하면 LDREX 부터 다시 / (다르면) CLREX 하고 반환. */
static uint32_t cas(cpu_t *c, uint32_t *p, uint32_t expected, uint32_t desired) {
    for (;;) {
        uint32_t old = LDREX(c, p);
        OP(c);
        if (old != expected) { CLREX(c); return old; }
        if (STREX(c, p, desired) == 0u) return old;
    }
}

/* 기다리지 않는 잠금: 0 이면 1 로 바꾸고 true. 순서: LDREX, OP(cmp), (0 이 아니면) CLREX → false /
 * (0 이면) STREX — 실패면 LDREX 부터, 성공이면 DMB(acquire) → true. */
static bool try_lock(cpu_t *c, uint32_t *lock) {
    for (;;) {
        uint32_t v = LDREX(c, lock);
        OP(c);
        if (v != 0u) { CLREX(c); return false; }
        if (STREX(c, lock, 1u) == 0u) { DMB(c); return true; }
    }
}
/* 해제: DMB(release) 다음 STR 0. */
static void unlock(cpu_t *c, uint32_t *lock) {
    DMB(c);
    STR(c, lock, 0u);
}

/* ======================= 연습 1 — 컨텍스트 스위치와 CLREX ======================= */
/* 스케줄러 예외(PendSV/IRQ) 하나로 태스크를 바꾼다. 정책이 MON_NOCLEAR 면 예외가 모니터를 안 지우므로
 * do_clrex 일 때 OS 가 CLREX 를 실행한다 (Linux arm 의 예외 복귀 경로, ARMv7-A RTOS 포트). */
static void os_switch(cpu_t *c, bool do_clrex) {
    exc_entry(c);
    if (do_clrex) CLREX(c);
    exc_exit(c);
}
/* 대본: B 가 LDREX 후 선점 → A 가 atomic_inc 를 끝내고 두 번째 LDREX 후 선점 → B 의 STREX → A 의 STREX.
 * 증가 3 번이므로 정답은 5+3 = 8. */
static uint32_t ctx_script(mon_policy_t pol, bool do_clrex) {
    cpu_t c;
    cpu_reset(&c, pol, NULL);
    uint32_t x = 5u;
    uint32_t vb = LDREX(&c, &x);                        /* task B */
    os_switch(&c, do_clrex);
    (void)atomic_inc(&c, &x);                           /* task A: 한 번 완료 */
    uint32_t va = LDREX(&c, &x);                        /* task A: 두 번째 시작 */
    os_switch(&c, do_clrex);
    while (STREX(&c, &x, vb + 1u) != 0u) vb = LDREX(&c, &x);   /* task B 재개 */
    os_switch(&c, do_clrex);
    while (STREX(&c, &x, va + 1u) != 0u) va = LDREX(&c, &x);   /* task A 재개 */
    return x;
}

/* ======================= 연습 2 — atomic_or ======================= */
/* *p |= bits, 옛 값 반환. 순서: LDREX, OP(orr), STREX (실패면 다시). */
static uint32_t atomic_or(cpu_t *c, uint32_t *p, uint32_t bits) {
    for (;;) {
        uint32_t old = LDREX(c, p);
        OP(c);
        if (STREX(c, p, old | bits) == 0u) return old;
    }
}
static uint32_t g_flags;
static void isr_set_bit1(cpu_t *c) { (void)atomic_or(c, &g_flags, 1u << 1); }

/* ======================= 연습 3 — 재시도 상한 + PRIMASK 폴백 ======================= */
/* 최대 max_tries 번 시도. 성공하면 *out = 새 값, true. 모두 실패하면 false (값은 그대로). */
static bool atomic_inc_bounded(cpu_t *c, uint32_t *p, unsigned max_tries, uint32_t *out) {
    for (unsigned t = 0; t < max_tries; t++) {
        uint32_t v = LDREX(c, p) + 1u;
        OP(c);
        if (STREX(c, p, v) == 0u) { *out = v; return true; }
    }
    return false;
}
/* ARMv6-M 식: CPSID, LDR, OP, STR, CPSIE. (원래 PRIMASK 를 저장·복원하는 판은 demo_m0_primask.c) */
static uint32_t crit_inc(cpu_t *c, uint32_t *p) {
    CPSID(c);
    uint32_t v = LDR(c, p) + 1u;
    OP(c);
    STR(c, p, v);
    CPSIE(c);
    return v;
}

/* ======================= 연습 4 — sem_try_take ======================= */
/* *p > 0 이면 1 감소하고 true. 0 이면 CLREX 하고 false. 순서: LDREX, OP(cmp), (0) CLREX / STREX. */
static bool sem_try_take(cpu_t *c, uint32_t *p) {
    for (;;) {
        uint32_t v = LDREX(c, p);
        OP(c);
        if (v == 0u) { CLREX(c); return false; }
        if (STREX(c, p, v - 1u) == 0u) return true;
    }
}
static uint32_t g_sem;
static void isr_sem_give(cpu_t *c) { (void)atomic_inc(c, &g_sem); }

/* ======================= 테스트 ======================= */
static unsigned g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) g_pass++; \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)
static uint32_t g_cnt;
static void isr_atomic_inc(cpu_t *c) { (void)atomic_inc(c, &g_cnt); }

int main(void) {
    /* 본문 프리미티브 회귀 (같은 모델을 쓰므로) */
    {
        cpu_t c;
        uint32_t lk = 0u, v = 3u;
        cpu_reset(&c, MON_V7M, NULL);
        CHECK(try_lock(&c, &lk) && !try_lock(&c, &lk));
        unlock(&c, &lk);
        CHECK(lk == 0u && cas(&c, &v, 3u, 4u) == 3u && v == 4u);
    }
    /* 연습 1 */
    CHECK(ctx_script(MON_NOCLEAR, false) == 7u);   /* CLREX 없는 A-profile 식 스위치 → 증가 하나 유실 */
    CHECK(ctx_script(MON_NOCLEAR, true) == 8u);    /* 스위치에서 CLREX → 정답 */
    CHECK(ctx_script(MON_V7M, false) == 8u);       /* M-profile: 예외 진입/복귀가 자동으로 클리어 */

    /* 연습 2: ISR 이 k = 0..5 어디에 와도 두 비트 모두 남는다 */
    bool or_ok = true;
    unsigned or_retries = 0;
    for (uint32_t k = 0; k < 6u; k++) {
        cpu_t c;
        cpu_reset(&c, MON_V7M, isr_set_bit1);
        g_flags = 0u;
        add_inj(&c, k, INJ_ISR, NULL, 0u);
        (void)atomic_or(&c, &g_flags, 1u << 0);
        finish(&c);
        or_ok = or_ok && g_flags == 3u;
        or_retries += c.n_strex_fail;
    }
    CHECK(or_ok);
    CHECK(or_retries == 2u);                        /* k=1,2 에서만 재시도 */

    /* 연습 3: 매 STREX 직전(2,5,8,11)에 ISR → 4 번 모두 실패 → 폴백 crit_inc (13 에서 온 ISR 은 pend) */
    {
        cpu_t c;
        uint32_t out = 0u;
        cpu_reset(&c, MON_V7M, isr_atomic_inc);
        g_cnt = 0u;
        const uint32_t storm[5] = { 2u, 5u, 8u, 11u, 13u };
        for (unsigned i = 0; i < 5u; i++) add_inj(&c, storm[i], INJ_ISR, NULL, 0u);
        bool got = atomic_inc_bounded(&c, &g_cnt, 4u, &out);
        CHECK(!got && c.n_strex_fail == 4u && g_cnt == 4u && c.tstep == 12u);
        uint32_t v = crit_inc(&c, &g_cnt);          /* 12=CPSID 13=LDR(여기서 pend) 14=OP 15=STR 16=CPSIE */
        finish(&c);
        CHECK(v == 5u && g_cnt == 6u && c.n_exc == 5u && c.max_latency == 4u && !c.pending);   /* 13~16 의 4 명령만큼 늦게 */
    }
    /* 연습 3b: crit_inc 는 k 가 어디든 정확 */
    bool crit_ok = true;
    for (uint32_t k = 0; k < 7u; k++) {
        cpu_t c;
        cpu_reset(&c, MON_V7M, isr_atomic_inc);
        g_cnt = 0u;
        add_inj(&c, k, INJ_ISR, NULL, 0u);
        (void)crit_inc(&c, &g_cnt);
        finish(&c);
        crit_ok = crit_ok && g_cnt == 2u && c.n_exc == 1u;
    }
    CHECK(crit_ok);

    /* 연습 4: 세마포어 1 개, main 이 두 번 take, ISR 이 k 에서 한 번 give */
    bool sem_ok = true;
    for (uint32_t k = 0; k < 10u; k++) {
        cpu_t c;
        cpu_reset(&c, MON_V7M, isr_sem_give);
        g_sem = 1u;
        add_inj(&c, k, INJ_ISR, NULL, 0u);
        unsigned took = 0;
        took += sem_try_take(&c, &g_sem);
        took += sem_try_take(&c, &g_sem);
        finish(&c);
        sem_ok = sem_ok && (g_sem + took == 2u) && took >= 1u;   /* 보존: 1 + 1(give) == 남은 + 가져간 */
    }
    CHECK(sem_ok);
    {
        cpu_t c;
        cpu_reset(&c, MON_V7M, NULL);
        g_sem = 0u;
        CHECK(!sem_try_take(&c, &g_sem) && g_sem == 0u && c.n_clrex == 1u && !c.excl);
    }

    printf("%u/%u passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
