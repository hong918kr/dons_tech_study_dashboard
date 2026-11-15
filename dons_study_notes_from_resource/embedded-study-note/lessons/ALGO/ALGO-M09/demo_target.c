/* ALGO-M09 demo: Cortex-M4 에서 스택 사용량 — solution.c 의 알고리즘 부분만 (libc 헤더 없이) 그대로 복사.
 * build (이 맥, 링크 없이 컴파일만):
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -fstack-usage -c demo_target.c -o /tmp/ALGO-M09_t.o
 *   cat /tmp/ALGO-M09_t.su      (clang 은 .su 를 -o 이름 옆에 쓴다)
 */
#include <stddef.h>
#include <stdint.h>

typedef struct { uint32_t s, e; } ival_t;   /* 반개구간 [s, e), s < e */

#ifndef MAX_IV
#define MAX_IV 1024        /* 인덱스는 uint16_t (최대 65535) */
#endif
#ifndef MAX_CH
#define MAX_CH 16          /* dma_assign 의 힙 크기 = 다룰 수 있는 최대 채널 수 (스택 16 x 8 B) */
#endif

/* ======================= 주어진 도구: 정렬과 힙 (TODO 아님) ======================= */
enum { BY_END = 0, BY_START = 1 };

/* BY_END: (e, s, 인덱스) 사전순,  BY_START: (s, e, 인덱스) 사전순 — 전순서라 결과가 결정적이다 */
static int idx_less(const ival_t *iv, uint16_t a, uint16_t b, int key) {
    uint32_t a1 = key == BY_END ? iv[a].e : iv[a].s, b1 = key == BY_END ? iv[b].e : iv[b].s;
    uint32_t a2 = key == BY_END ? iv[a].s : iv[a].e, b2 = key == BY_END ? iv[b].s : iv[b].e;
    if (a1 != b1) return a1 < b1;
    if (a2 != b2) return a2 < b2;
    return a < b;
}
static void idx_sift(const ival_t *iv, uint16_t *h, size_t i, size_t n, int key) {
    for (;;) {
        size_t c = 2 * i + 1;
        if (c >= n) return;
        if (c + 1 < n && idx_less(iv, h[c], h[c + 1], key)) c++;
        if (!idx_less(iv, h[i], h[c], key)) return;
        uint16_t t = h[i]; h[i] = h[c]; h[c] = t; i = c;
    }
}
/* idx[0..n) = 0..n-1 을 key 순서로 정렬 (힙 정렬: O(n log n), 추가 메모리 O(1), 재귀 없음) */
void sort_idx(const ival_t *iv, size_t n, uint16_t *idx, int key) {
    for (size_t i = 0; i < n; i++) idx[i] = (uint16_t)i;
    for (size_t i = n / 2; i-- > 0;) idx_sift(iv, idx, i, n, key);
    for (size_t m = n; m > 1; m--) {
        uint16_t t = idx[0]; idx[0] = idx[m - 1]; idx[m - 1] = t;
        idx_sift(iv, idx, 0, m - 1, key);
    }
}
static void u32_sift(uint32_t *h, size_t i, size_t n) {
    for (;;) {
        size_t c = 2 * i + 1;
        if (c >= n) return;
        if (c + 1 < n && h[c] < h[c + 1]) c++;
        if (h[i] >= h[c]) return;
        uint32_t t = h[i]; h[i] = h[c]; h[c] = t; i = c;
    }
}
void sort_u32(uint32_t *a, size_t n) {
    for (size_t i = n / 2; i-- > 0;) u32_sift(a, i, n);
    for (size_t m = n; m > 1; m--) { uint32_t t = a[0]; a[0] = a[m - 1]; a[m - 1] = t; u32_sift(a, 0, m - 1); }
}

/* 채널 하나 = (그 채널의 마지막 전송이 끝나는 시각, 채널 번호). (end, ch) 사전순 최소 힙 */
typedef struct { uint32_t end; uint8_t ch; } slot_t;
static int slot_less(slot_t a, slot_t b) { return a.end != b.end ? a.end < b.end : a.ch < b.ch; }
void slot_push(slot_t *h, unsigned *hn, slot_t v) {
    unsigned i = (*hn)++;
    while (i > 0) {
        unsigned p = (i - 1) / 2;
        if (!slot_less(v, h[p])) break;
        h[i] = h[p]; i = p;
    }
    h[i] = v;
}
slot_t slot_pop(slot_t *h, unsigned *hn) {
    slot_t top = h[0], last = h[--(*hn)];
    unsigned i = 0, n = *hn;
    for (;;) {
        unsigned c = 2 * i + 1;
        if (c >= n) break;
        if (c + 1 < n && slot_less(h[c + 1], h[c])) c++;
        if (!slot_less(h[c], last)) break;
        h[i] = h[c]; i = c;
    }
    if (n > 0) h[i] = last;
    return top;
}

/* ======================= Part A: 최대 비겹침 선택 / 병합 ======================= */
/* 서로 겹치지 않는 구간을 최대 개수 고른다. pick[] 에 원래 인덱스를 시간 순으로 쓰고 개수를 돌려준다.
 * work 는 n 칸 작업 공간. 불변식: last_end = 지금까지 고른 구간 중 마지막 것의 끝 (처음엔 0). */
size_t max_nonoverlap(const ival_t *iv, size_t n, uint16_t *work, uint16_t *pick) {
    sort_idx(iv, n, work, BY_END);
    size_t k = 0;
    uint32_t last_end = 0;                     /* s >= 0 이므로 첫 구간은 항상 들어간다 */
    for (size_t t = 0; t < n; t++) {
        const ival_t *c = &iv[work[t]];
        if (c->s >= last_end) {                /* 반개구간: s == last_end 는 맞닿음 → 허용 */
            pick[k++] = work[t];
            last_end = c->e;
        }
    }
    return k;
}

/* 겹치거나 맞닿는 구간을 합쳐 out[] 에 시작 순으로 쓴다 (out[k].e < out[k+1].s 가 성립). 개수를 돌려준다.
 * out 은 iv 와 겹치면 안 된다. 불변식: out[0..m] 은 지금까지 본 구간들의 합집합, out[m] 이 열린 마지막 덩어리. */
size_t merge_intervals(const ival_t *iv, size_t n, uint16_t *work, ival_t *out) {
    if (n == 0) return 0;
    sort_idx(iv, n, work, BY_START);
    size_t m = 0;
    out[0] = iv[work[0]];
    for (size_t t = 1; t < n; t++) {
        ival_t c = iv[work[t]];
        if (c.s <= out[m].e) {                 /* 겹침 또는 맞닿음 → 늘리기 (줄이면 안 된다: 포함된 구간) */
            if (c.e > out[m].e) out[m].e = c.e;
        } else {
            out[++m] = c;
        }
    }
    return m + 1;
}

/* ======================= Part B: 최대 겹침 깊이 (스위프) ======================= */
/* 어떤 시각에 동시에 활성인 구간 수의 최댓값. *at = 그 최댓값에 처음 도달한 시각 (n == 0 이면 0).
 * starts/ends 는 n 칸 작업 공간. 같은 시각에 끝과 시작이 겹치면 끝을 먼저 처리한다 ([s,e) 는 e 를 포함 안 함). */
unsigned max_depth(const ival_t *iv, size_t n, uint32_t *starts, uint32_t *ends, uint32_t *at) {
    for (size_t i = 0; i < n; i++) { starts[i] = iv[i].s; ends[i] = iv[i].e; }
    sort_u32(starts, n);
    sort_u32(ends, n);
    unsigned d = 0, best = 0;
    size_t i = 0, j = 0;
    *at = 0;
    while (i < n) {                            /* 시작이 다 나오면 깊이는 줄기만 한다 → 끝 */
        if (starts[i] < ends[j]) {             /* 엄격한 < : 같은 시각이면 끝(else) 먼저 */
            d++;
            if (d > best) { best = d; *at = starts[i]; }
            i++;
        } else {                               /* j < i 가 보장된다 (풀이 참고) */
            d--;
            j++;
        }
    }
    return best;
}

/* ======================= Part C: DMA 채널 배정 ======================= */
/* 전송 x[i] 를 채널 chan[i] (0..k-1) 에 배정하고 쓴 채널 수 k 를 돌려준다. 한 채널의 전송은 서로 겹치지 않는다.
 * k 는 최소(= max_depth). hw_ch 개로 부족하면 -1. 힙은 채널마다 슬롯 하나 = (마지막 끝 시각, 채널). */
int dma_assign(const ival_t *x, size_t n, unsigned hw_ch, uint16_t *work, uint8_t *chan) {
    slot_t heap[MAX_CH];
    unsigned hn = 0, used = 0;
    if (hw_ch > MAX_CH) hw_ch = MAX_CH;
    sort_idx(x, n, work, BY_START);
    for (size_t t = 0; t < n; t++) {
        uint16_t i = work[t];
        if (hn > 0 && heap[0].end <= x[i].s) {         /* 가장 일찍 비는 채널이 지금 비어 있으면 재사용 */
            chan[i] = slot_pop(heap, &hn).ch;
        } else if (used < hw_ch) {                     /* 모든 채널이 바쁘다 → 새 채널 */
            chan[i] = (uint8_t)used++;
        } else {
            return -1;                                 /* 깊이 > hw_ch */
        }
        slot_push(heap, &hn, (slot_t){ x[i].e, chan[i] });
    }
    return (int)used;
}

