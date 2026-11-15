/* RTOS-A06 데모 — Cortex-M4 에서 "이벤트 1개 기록" 이 몇 명령어인가 (실행하지 않고 asm 만 본다).
 * solution.c Part A 의 tr_record/tr_put 을 그대로 옮기고, 타깃식 훅을 붙였다:
 *   PRIMASK 저장 + cpsid i → DWT->CYCCNT 읽기 (타임스탬프) → tr_record → PRIMASK 복원.
 * 빌드: clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_m4_record.c -o -
 * (freestanding: stdint/stdbool 만. DWT_CYCCNT 주소 0xE0001004 는 ARMv7-M 아키텍처 매뉴얼 기준 — 여기선 실행 안 함) */
#include <stdbool.h>
#include <stdint.h>

enum { EV_DROP = 12 };
enum { ARG_NONE, ARG_NAME, ARG_NUM };
typedef struct { uint32_t seq, t_us, arg; uint8_t ev, id, argk, pad; } trace_rec_t;
typedef enum { TR_OVERWRITE, TR_STOP_WHEN_FULL } tr_mode_t;
typedef struct {
    trace_rec_t *buf; uint32_t cap, mask, wr, rd, next_seq, dropped, pend_drop, pend_seq, pend_t;
    tr_mode_t mode;
} trace_ring_t;

static inline void tr_put(trace_ring_t *r, uint32_t seq, uint32_t t, uint8_t ev, uint8_t id, uint8_t argk, uint32_t arg) {
    trace_rec_t *p = &r->buf[r->wr & r->mask];
    p->seq = seq; p->t_us = t; p->arg = arg; p->ev = ev; p->id = id; p->argk = argk; p->pad = 0;
    r->wr++;
}
static inline bool tr_record(trace_ring_t *r, uint32_t t, uint8_t ev, uint8_t id, uint8_t argk, uint32_t arg) {
    uint32_t seq = r->next_seq++;
    uint32_t used = r->wr - r->rd;
    if (r->mode == TR_OVERWRITE) {
        if (used == r->cap) { r->rd++; r->dropped++; }
        tr_put(r, seq, t, ev, id, argk, arg);
        return true;
    }
    uint32_t need = r->pend_drop ? 2u : 1u;
    if (r->cap - used < need) {
        if (r->pend_drop == 0) { r->pend_seq = seq; r->pend_t = t; }
        r->pend_drop++; r->dropped++;
        return false;
    }
    if (r->pend_drop) { tr_put(r, r->pend_seq, r->pend_t, EV_DROP, 0, ARG_NUM, r->pend_drop); r->pend_drop = 0; }
    tr_put(r, seq, t, ev, id, argk, arg);
    return true;
}

#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)
trace_ring_t g_ring;

/* traceTASK_SWITCHED_IN() 이 부를 함수: 잠금 안에서 시각을 읽어야 기록 순서 = 시각 순서 */
void trace_switch_in(uint8_t task_id) {
    uint32_t primask;
    __asm volatile("mrs %0, primask\n cpsid i" : "=r"(primask) :: "memory");
    tr_record(&g_ring, DWT_CYCCNT, 1 /* EV_SWITCH_IN */, task_id, ARG_NONE, 0);
    __asm volatile("msr primask, %0" :: "r"(primask) : "memory");
}
