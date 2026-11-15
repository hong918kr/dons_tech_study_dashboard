/* ALGO-A09 스위프 라인: 메모리 영역 겹침과 최대 동시 이벤트
 *
 *   Part A  메모리 맵 영역 [base, base+size) 의 겹침 쌍 전부 — 이벤트 정렬 + 활성 집합, O(n log n + k)
 *           + 쌍 분류 (부분 겹침 PARTIAL / 포함 CONTAIN / 동일 EQUAL)
 *   Part B  ISR 구간 로그 [entry, exit) 의 최대 동시 활성 수(가중치 = 스택 바이트도 가능)와 그 값이 처음
 *           나타나는 구간 [from, to), 직사각형 합집합 넓이: 슬랩별 y 병합 O(n^2 log n) + 세그먼트 트리 O(n log n)
 *   Part C  펌웨어: 링커 메모리 맵 · MPU 영역 표 검사, 중첩 ISR 트레이스의 최대 스택, LCD 더티 사각형 전송량
 *
 * 규약: 모든 구간은 반개구간(half-open). 맞닿음 [0,10) [10,20) 은 겹침이 아니다. 끝 좌표는 uint64_t 로
 * 계산한다 (0xFFFFF000 + 0x1000 = 2^32 는 uint32_t 에서 0 이 된다).
 * 알고리즘 쪽은 malloc 없음: 작업 공간은 MM_MAX / ISR_MAX / RECT_MAX 크기의 static 배열 (재진입 불가).
 * 테스트: 손 계산 사례 + xorshift32 고정 시드 무작위를 O(n^2) 쌍 비교 · 정수 격자 전수 기준과 대조.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef MM_MAX
#define MM_MAX 4096          /* 영역 수 상한 (id 를 16비트에 담으므로 65536 이하) */
#endif
#ifndef ISR_MAX
#define ISR_MAX 4096
#endif
#ifndef RECT_MAX
#define RECT_MAX 1024
#endif
_Static_assert(MM_MAX <= 65536 && ISR_MAX <= 65536, "event id is 16 bits");
_Static_assert(RECT_MAX <= (1 << 22), "rect event id is 23 bits");

/* ======================= 공통: 64비트 키 정렬과 이벤트 키 ======================= */
/* 상향식(bottom-up) 병합 정렬. 안정, O(n log n), 재귀 없음, tmp 는 n 칸. */
static void sort_u64(uint64_t *a, size_t n, uint64_t *tmp) {
    uint64_t *src = a, *dst = tmp;
    for (size_t w = 1; w < n; w *= 2) {
        for (size_t lo = 0; lo < n; lo += 2 * w) {
            size_t mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n;
            size_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi) dst[k++] = src[i] <= src[j] ? src[i++] : src[j++];
            while (i < mid) dst[k++] = src[i++];
            while (j < hi) dst[k++] = src[j++];
        }
        uint64_t *t = src; src = dst; dst = t;
    }
    if (src != a) memcpy(a, src, n * sizeof *a);
}

/* 이벤트 = 64비트 정수 하나: [좌표 33비트][종류 1비트][id 16비트].
 * 정수 비교 한 번이 (좌표, END<START, id) 사전순과 같다 → 동점 규칙이 키 안에 박혀 있고 결과가 결정적. */
#define EV_END   0u
#define EV_START 1u
static inline uint64_t ev_key(uint64_t coord, unsigned type, unsigned id) {
    return (coord << 17) | ((uint64_t)type << 16) | id;
}
static inline uint64_t ev_coord(uint64_t k) { return k >> 17; }
static inline unsigned ev_type(uint64_t k) { return (unsigned)(k >> 16) & 1u; }
static inline unsigned ev_id(uint64_t k) { return (unsigned)(k & 0xFFFFu); }

/* ======================= Part A — 메모리 맵 겹침 쌍 ======================= */
enum { RK_MEM = 0, RK_SEC = 1 };      /* Part C: 물리 메모리(뱅크) / 섹션·파티션 */
typedef struct { const char *name; uint32_t base, size; uint8_t kind; } region_t;
static inline uint64_t reg_end(const region_t *r) { return (uint64_t)r->base + r->size; }   /* ≤ 2^33 − 2 */

typedef enum { OV_PARTIAL = 0, OV_CONTAIN = 1, OV_EQUAL = 2 } ov_kind_t;
typedef struct { uint16_t a, b; int8_t outer; uint8_t kind; } ovpair_t;   /* a < b. outer: 0 = a ⊇ b, 1 = b ⊇ a, -1 */

/* 겹치는(교집합이 비지 않은) 두 영역을 분류. *outer = 0 (x 가 y 를 포함), 1 (y 가 x 를 포함), -1 (그 밖). */
static ov_kind_t ov_classify(const region_t *x, const region_t *y, int *outer) {
    uint64_t xe = reg_end(x), ye = reg_end(y);
    if (x->base == y->base && xe == ye) { *outer = -1; return OV_EQUAL; }
    if (x->base <= y->base && ye <= xe) { *outer = 0; return OV_CONTAIN; }
    if (y->base <= x->base && xe <= ye) { *outer = 1; return OV_CONTAIN; }
    *outer = -1;
    return OV_PARTIAL;
}

static uint64_t g_ev[2 * MM_MAX], g_evtmp[2 * MM_MAX];
static uint16_t g_act[MM_MAX], g_pos[MM_MAX];      /* 활성 집합: 배열 + 위치표 → 넣기·빼기 O(1) */

/* 겹치는 쌍의 총수 k 를 반환하고, 앞의 min(k, cap) 개를 out 에 쓴다 (스위프 순서, 결정적).
 * 크기 0 영역은 무시한다. n > MM_MAX 이면 SIZE_MAX. 시간 O(n log n + k), 추가 메모리 O(n). */
static size_t mm_overlaps(const region_t *r, size_t n, ovpair_t *out, size_t cap) {
    if (n > MM_MAX) return SIZE_MAX;
    size_t ne = 0;
    for (size_t i = 0; i < n; i++) {
        if (r[i].size == 0) continue;          /* 빈 집합: 아무것과도 안 겹친다 (END<START 동점 규칙과도 충돌) */
        g_ev[ne++] = ev_key(r[i].base, EV_START, (unsigned)i);
        g_ev[ne++] = ev_key(reg_end(&r[i]), EV_END, (unsigned)i);
    }
    sort_u64(g_ev, ne, g_evtmp);
    size_t na = 0, k = 0;
    for (size_t e = 0; e < ne; e++) {
        unsigned id = ev_id(g_ev[e]);
        if (ev_type(g_ev[e]) == EV_END) {      /* 맨 끝 원소를 빈자리로 옮긴다 (swap-remove) */
            uint16_t p = g_pos[id], last = g_act[--na];
            g_act[p] = last;
            g_pos[last] = p;
        } else {                               /* 지금 활성인 영역 전부가 id 와 겹친다 — 하나도 헛일이 없다 */
            for (size_t j = 0; j < na; j++, k++) {
                if (k >= cap) continue;
                unsigned a = g_act[j], b = id;
                if (a > b) { unsigned t = a; a = b; b = t; }
                int outer;
                ov_kind_t kd = ov_classify(&r[a], &r[b], &outer);
                out[k] = (ovpair_t){(uint16_t)a, (uint16_t)b, (int8_t)outer, (uint8_t)kd};
            }
            g_pos[id] = (uint16_t)na;
            g_act[na++] = (uint16_t)id;
        }
    }
    return k;
}

/* 정규 순서 (a, b) 오름차순: 키가 0..n-1 이므로 계수 정렬 두 번(LSD) — O(n + k), 출력 민감성을 지킨다. */
static uint32_t g_cnt[MM_MAX + 1];
static void pairs_canon(ovpair_t *p, size_t k, size_t n, ovpair_t *tmp) {
    for (int pass = 0; pass < 2; pass++) {             /* pass 0: b 로, pass 1: a 로 (안정) */
        memset(g_cnt, 0, (n + 1) * sizeof g_cnt[0]);
        for (size_t i = 0; i < k; i++) g_cnt[(pass ? p[i].a : p[i].b) + 1u]++;
        for (size_t v = 0; v < n; v++) g_cnt[v + 1] += g_cnt[v];
        for (size_t i = 0; i < k; i++) tmp[g_cnt[pass ? p[i].a : p[i].b]++] = p[i];
        memcpy(p, tmp, k * sizeof *p);
    }
}

/* ======================= Part B — ISR 로그: 최대 동시 활성 ======================= */
typedef struct { uint32_t t0, t1; uint8_t irq; } isr_iv_t;          /* [t0, t1) 틱. t1 <= t0 이면 무시 */
typedef struct {
    uint64_t peak;        /* max_t Σ w_i (w == NULL 이면 개수) */
    uint32_t from, to;    /* peak 가 처음 나타나는 극대 구간 [from, to). peak == 0 이면 0, 0 */
    uint32_t nwin;        /* peak 인 극대 구간 수 */
    uint64_t at_peak;     /* peak 상태로 있던 총 시간 */
} peak_t;

static uint64_t g_iev[2 * ISR_MAX], g_ievtmp[2 * ISR_MAX];

/* 같은 시각의 사건을 한 묶음으로 적용한 뒤에만 값을 본다 → 값 v(t) 는 [t, 다음 사건 시각) 에서 일정.
 * w: 구간별 가중치(NULL = 1). 0 = 성공, n > ISR_MAX 이면 -1. O(n log n). */
static int isr_peak(const isr_iv_t *iv, size_t n, const uint32_t *w, peak_t *out) {
    memset(out, 0, sizeof *out);
    if (n > ISR_MAX) return -1;
    size_t ne = 0;
    for (size_t i = 0; i < n; i++) {
        if (iv[i].t1 <= iv[i].t0) continue;     /* 길이 0 (또는 거꾸로 된) 구간: 활성인 순간이 없다 */
        g_iev[ne++] = ev_key(iv[i].t0, EV_START, (unsigned)i);
        g_iev[ne++] = ev_key(iv[i].t1, EV_END, (unsigned)i);
    }
    sort_u64(g_iev, ne, g_ievtmp);
    uint64_t cur = 0, best = 0;
    uint32_t wstart = 0;
    bool in = false, first_closed = false;
    for (size_t e = 0; e < ne;) {
        uint64_t t = ev_coord(g_iev[e]);
        do {                                    /* 시각 t 의 사건 전부 */
            unsigned id = ev_id(g_iev[e]);
            uint64_t d = w ? w[id] : 1u;
            if (ev_type(g_iev[e]) == EV_START) cur += d; else cur -= d;
            e++;
        } while (e < ne && ev_coord(g_iev[e]) == t);
        uint32_t tt = (uint32_t)t;
        if (cur > best) {                       /* 새 최댓값: 지금까지의 창은 무효 */
            best = cur; out->from = tt; out->nwin = 1; out->at_peak = 0;
            in = true; first_closed = false; wstart = tt;
        } else if (in && cur < best) {          /* 창이 닫힌다 */
            in = false;
            out->at_peak += tt - wstart;
            if (!first_closed) { out->to = tt; first_closed = true; }
        } else if (!in && best > 0 && cur == best) {   /* 같은 최댓값의 새 창 */
            in = true; out->nwin++; wstart = tt;
        }
    }
    out->peak = best;
    return 0;
}

/* 시각 t 에 활성인 구간 번호들 (O(n) 한 번 훑기 — 보고서용) */
static size_t isr_active_at(const isr_iv_t *iv, size_t n, uint32_t t, uint16_t *ids) {
    size_t m = 0;
    for (size_t i = 0; i < n; i++)
        if (iv[i].t0 <= t && t < iv[i].t1) ids[m++] = (uint16_t)i;
    return m;
}

/* ======================= Part B — 직사각형 합집합 넓이 ======================= */
typedef struct { int32_t x1, y1, x2, y2; } rect_t;   /* [x1,x2) × [y1,y2). 좌표 |c| <= 2^30. x1>=x2 또는 y1>=y2 = 빈 사각형 */
#define RBIAS ((int64_t)1 << 31)
static inline bool rect_empty(const rect_t *r) { return r->x1 >= r->x2 || r->y1 >= r->y2; }

static int32_t g_xs[2 * RECT_MAX], g_ys[2 * RECT_MAX];
static uint64_t g_rk[2 * RECT_MAX], g_rktmp[2 * RECT_MAX];

/* 비어 있지 않은 사각형들의 x(또는 y) 좌표를 정렬·중복 제거해 out 에 쓰고 개수를 반환 (좌표 압축) */
static size_t uniq_coords(const rect_t *r, size_t n, bool want_x, int32_t *out) {
    size_t m = 0;
    for (size_t i = 0; i < n; i++) {
        if (rect_empty(&r[i])) continue;
        g_rk[m++] = (uint64_t)((int64_t)(want_x ? r[i].x1 : r[i].y1) + RBIAS);
        g_rk[m++] = (uint64_t)((int64_t)(want_x ? r[i].x2 : r[i].y2) + RBIAS);
    }
    sort_u64(g_rk, m, g_rktmp);
    size_t u = 0;
    for (size_t i = 0; i < m; i++)
        if (i == 0 || g_rk[i] != g_rk[i - 1]) out[u++] = (int32_t)((int64_t)g_rk[i] - RBIAS);
    return u;
}

static size_t lower_idx(const int32_t *a, size_t n, int32_t v) {   /* 첫 a[i] >= v */
    size_t lo = 0, hi = n;
    while (lo < hi) { size_t mid = lo + (hi - lo) / 2; if (a[mid] < v) lo = mid + 1; else hi = mid; }
    return lo;
}

/* 슬랩법: 서로 다른 x 좌표 사이의 띠(slab)마다 그 띠를 덮는 사각형의 y 구간을 모아 정렬·병합.
 * *npieces = 병합된 (띠, y 구간) 수 = 합집합을 서로소 사각형으로 나눈 개수. O(n^2 log n), n > RECT_MAX 이면 UINT64_MAX. */
static uint64_t rect_union_slab(const rect_t *r, size_t n, size_t *npieces) {
    size_t pieces = 0;
    uint64_t area = 0;
    if (npieces) *npieces = 0;
    if (n > RECT_MAX) return UINT64_MAX;
    size_t nx = uniq_coords(r, n, true, g_xs);
    for (size_t s = 0; s + 1 < nx; s++) {
        int32_t xa = g_xs[s], xb = g_xs[s + 1];
        size_t m = 0;
        for (size_t i = 0; i < n; i++)
            if (!rect_empty(&r[i]) && r[i].x1 <= xa && xb <= r[i].x2)
                g_rk[m++] = ((uint64_t)((int64_t)r[i].y1 + RBIAS) << 32) | (uint64_t)((int64_t)r[i].y2 + RBIAS);
        sort_u64(g_rk, m, g_rktmp);           /* y1 오름차순 (같으면 y2) */
        int64_t cs = 0, ce = 0;
        bool open = false;
        uint64_t cov = 0;
        for (size_t j = 0; j < m; j++) {
            int64_t a = (int64_t)(g_rk[j] >> 32) - RBIAS, b = (int64_t)(g_rk[j] & 0xFFFFFFFFu) - RBIAS;
            if (open && a <= ce) { if (b > ce) ce = b; }          /* 겹치거나 맞닿음 → 이어 붙인다 */
            else { if (open) { cov += (uint64_t)(ce - cs); pieces++; } cs = a; ce = b; open = true; }
        }
        if (open) { cov += (uint64_t)(ce - cs); pieces++; }
        area += cov * (uint64_t)((int64_t)xb - xa);
    }
    if (npieces) *npieces = pieces;
    return area;
}

/* 세그먼트 트리: 잎 = 기본 y 구간 [ys[i], ys[i+1]). 노드마다 cnt(이 노드 구간 전체를 덮는 사각형 수, 아래로
 * 내려보내지 않음)와 len(이 노드 구간 안에서 덮인 길이). 루트 len = 지금 x 에서 덮인 y 길이. */
static int32_t g_tcnt[8 * RECT_MAX];
static uint64_t g_tlen[8 * RECT_MAX];

static void st_update(size_t node, size_t lo, size_t hi, size_t l, size_t r, int d) {   /* 노드 = 잎 [lo,hi), 갱신 [l,r) */
    if (r <= lo || hi <= l) return;
    if (l <= lo && hi <= r) {
        g_tcnt[node] += d;
    } else {
        size_t mid = lo + (hi - lo) / 2;
        st_update(2 * node, lo, mid, l, r, d);
        st_update(2 * node + 1, mid, hi, l, r, d);
    }
    if (g_tcnt[node] > 0) g_tlen[node] = (uint64_t)((int64_t)g_ys[hi] - g_ys[lo]);
    else if (hi - lo == 1) g_tlen[node] = 0;
    else g_tlen[node] = g_tlen[2 * node] + g_tlen[2 * node + 1];
}

/* x 방향 스위프 + 세그먼트 트리. O(n log n), 재귀 깊이 ⌈log2(2n)⌉+1. n > RECT_MAX 이면 UINT64_MAX. */
static uint64_t rect_union_segtree(const rect_t *r, size_t n) {
    if (n > RECT_MAX) return UINT64_MAX;
    size_t ny = uniq_coords(r, n, false, g_ys);
    if (ny < 2) return 0;
    size_t leaves = ny - 1;
    memset(g_tcnt, 0, 4 * leaves * sizeof g_tcnt[0]);
    memset(g_tlen, 0, 4 * leaves * sizeof g_tlen[0]);
    size_t ne = 0;
    for (size_t i = 0; i < n; i++) {
        if (rect_empty(&r[i])) continue;
        g_rk[ne++] = ((uint64_t)((int64_t)r[i].x1 + RBIAS) << 24) | (1ull << 23) | i;   /* 열기 */
        g_rk[ne++] = ((uint64_t)((int64_t)r[i].x2 + RBIAS) << 24) | i;                  /* 닫기 */
    }
    sort_u64(g_rk, ne, g_rktmp);
    uint64_t area = 0;
    int64_t px = (int64_t)(g_rk[0] >> 24) - RBIAS;
    for (size_t e = 0; e < ne; e++) {
        int64_t x = (int64_t)(g_rk[e] >> 24) - RBIAS;
        area += g_tlen[1] * (uint64_t)(x - px);    /* 직전 x 부터 지금까지 덮인 길이는 일정 */
        px = x;
        size_t i = (size_t)(g_rk[e] & 0x7FFFFFu);
        size_t l = lower_idx(g_ys, ny, r[i].y1), rr = lower_idx(g_ys, ny, r[i].y2);
        st_update(1, 0, leaves, l, rr, (g_rk[e] >> 23) & 1u ? +1 : -1);
    }
    return area;
}

/* ======================= Part C — 펌웨어 보고서 (주어짐) ======================= */
typedef struct { size_t pairs, sec_overlap, mem_overlap, straddle, orphan, oob, empty; } mmrep_t;
#define MM_REPORT_CAP 4096
static ovpair_t g_rp[MM_REPORT_CAP], g_rtmp[MM_REPORT_CAP];
static const char *const OV_NAME[] = {"PARTIAL", "CONTAIN", "EQUAL"};

/* 링커 맵 검사: SEC×SEC 겹침, MEM×MEM 겹침, MEM 경계를 걸친 SEC(straddle), 어느 MEM 에도 안 든 SEC(orphan),
 * 주소 공간 밖(끝 > 2^32), 빈 영역. 0 = 성공, -1 = 쌍이 너무 많음. */
static int mm_report(const region_t *r, size_t n, mmrep_t *rep, FILE *fp) {
    static bool inside[MM_MAX];
    memset(rep, 0, sizeof *rep);
    if (n > MM_MAX) return -1;
    for (size_t i = 0; i < n; i++) {
        inside[i] = false;
        if (r[i].size == 0) { rep->empty++; if (fp) fprintf(fp, "  [EMPTY]        %-9s at 0x%08" PRIx32 " (size 0: ignored)\n", r[i].name, r[i].base); }
        if (reg_end(&r[i]) > ((uint64_t)1 << 32)) {
            rep->oob++;
            if (fp) fprintf(fp, "  [OUT-OF-SPACE] %-9s ends at 0x%09" PRIx64 " > 2^32 (uint32 end would wrap to 0x%08" PRIx32 ")\n",
                            r[i].name, reg_end(&r[i]), (uint32_t)(r[i].base + r[i].size));
        }
    }
    size_t k = mm_overlaps(r, n, g_rp, MM_REPORT_CAP);
    if (k > MM_REPORT_CAP) return -1;
    pairs_canon(g_rp, k, n, g_rtmp);
    rep->pairs = k;
    for (size_t q = 0; q < k; q++) {
        const ovpair_t *p = &g_rp[q];
        const region_t *a = &r[p->a], *b = &r[p->b];
        uint64_t lo = a->base > b->base ? a->base : b->base, hi = reg_end(a) < reg_end(b) ? reg_end(a) : reg_end(b);
        const char *tag = NULL;
        if (a->kind == RK_SEC && b->kind == RK_SEC) { rep->sec_overlap++; tag = "[OVERLAP]     "; }
        else if (a->kind == RK_MEM && b->kind == RK_MEM) { rep->mem_overlap++; tag = "[MEM-OVERLAP] "; }
        else {
            bool a_mem = a->kind == RK_MEM;
            bool ok = p->kind == OV_EQUAL || (p->kind == OV_CONTAIN && p->outer == (a_mem ? 0 : 1));
            if (ok) inside[a_mem ? p->b : p->a] = true;
            else { rep->straddle++; tag = "[STRADDLE]    "; }
        }
        if (tag && fp)
            fprintf(fp, "  %s %-9s x %-9s [0x%08" PRIx64 ", 0x%08" PRIx64 ") %6" PRIu64 " B  %s\n",
                    tag, a->name, b->name, lo, hi, hi - lo, OV_NAME[p->kind]);
    }
    for (size_t i = 0; i < n; i++)
        if (r[i].kind == RK_SEC && r[i].size != 0 && !inside[i]) {
            rep->orphan++;
            if (fp) fprintf(fp, "  [ORPHAN]       %-9s not inside any memory region\n", r[i].name);
        }
    if (fp) fprintf(fp, "  summary: pairs=%zu (expected containment %zu) overlap=%zu mem-overlap=%zu straddle=%zu orphan=%zu oob=%zu empty=%zu\n",
                    rep->pairs, rep->pairs - rep->sec_overlap - rep->mem_overlap - rep->straddle,
                    rep->sec_overlap, rep->mem_overlap, rep->straddle, rep->orphan, rep->oob, rep->empty);
    return 0;
}

/* MPU 표 검사. v7(PMSAv7): 크기 2^k >= 32, base 정렬 필수, 겹침 허용(번호 큰 영역이 이김).
 * v8(PMSAv8): base/limit 32 B 단위, 겹침 금지. 반환: 오류 수. *noverlap = 겹침 쌍 수. */
static size_t mpu_check(const region_t *r, size_t n, bool v8, size_t *noverlap, FILE *fp) {
    size_t err = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t s = r[i].size;
        bool ok = v8 ? (s != 0 && s % 32 == 0 && r[i].base % 32 == 0)
                     : (s >= 32 && (s & (s - 1)) == 0 && r[i].base % s == 0);
        if (!ok) { err++; if (fp) fprintf(fp, "    region %zu %-9s base/size not legal for %s\n", i, r[i].name, v8 ? "PMSAv8" : "PMSAv7"); }
    }
    size_t k = mm_overlaps(r, n, g_rp, MM_REPORT_CAP);
    if (k > MM_REPORT_CAP) return err + 1;
    pairs_canon(g_rp, k, n, g_rtmp);
    for (size_t q = 0; q < k; q++) {
        const ovpair_t *p = &g_rp[q];
        if (v8) { err++; if (fp) fprintf(fp, "    region %u %s overlaps region %u %s: not allowed on PMSAv8\n", p->a, r[p->a].name, p->b, r[p->b].name); }
        else if (fp) fprintf(fp, "    region %u %s inside/over region %u %s: region %u wins (%s)\n",
                             p->b, r[p->b].name, p->a, r[p->a].name, p->b, OV_NAME[p->kind]);
    }
    *noverlap = k;
    return err;
}

/* 단일 코어 정적 상한: 우선순위(선점 레벨)마다 많아야 하나만 중첩되므로 Σ_level max(stack in level). */
static uint32_t isr_static_bound(const uint8_t *prio, const uint32_t *stack, size_t n) {
    uint32_t mx[256] = {0}, sum = 0;
    for (size_t i = 0; i < n; i++) if (stack[i] > mx[prio[i]]) mx[prio[i]] = stack[i];
    for (int l = 0; l < 256; l++) sum += mx[l];
    return sum;
}

/* ======================= 테스트 하네스 ======================= */
#ifndef ALGO_A09_NO_MAIN
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0xA09A09u;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

/* ---- 기준 구현 (테스트 쪽) ---- */
/* O(n^2) 모든 쌍. 분류는 교집합 [max base, min end) 를 각 영역과 비교하는 다른 공식으로 (ov_classify 와 독립). */
static size_t mm_brute(const region_t *r, size_t n, ovpair_t *out, size_t cap) {
    size_t k = 0;
    for (size_t a = 0; a < n; a++)
        for (size_t b = a + 1; b < n; b++) {
            if (r[a].size == 0 || r[b].size == 0) continue;
            uint64_t lo = r[a].base > r[b].base ? r[a].base : r[b].base;
            uint64_t hi = reg_end(&r[a]) < reg_end(&r[b]) ? reg_end(&r[a]) : reg_end(&r[b]);
            if (lo >= hi) continue;
            bool ia = lo == r[a].base && hi == reg_end(&r[a]), ib = lo == r[b].base && hi == reg_end(&r[b]);
            ovpair_t p = {(uint16_t)a, (uint16_t)b, -1, OV_PARTIAL};
            if (ia && ib) p.kind = OV_EQUAL;
            else if (ib) { p.kind = OV_CONTAIN; p.outer = 0; }      /* 교집합 = b → a ⊇ b */
            else if (ia) { p.kind = OV_CONTAIN; p.outer = 1; }
            if (k < cap) out[k] = p;
            k++;
        }
    return k;
}
static bool pairs_eq(const ovpair_t *x, const ovpair_t *y, size_t k) {
    for (size_t i = 0; i < k; i++)
        if (x[i].a != y[i].a || x[i].b != y[i].b || x[i].kind != y[i].kind || x[i].outer != y[i].outer) return false;
    return true;
}
static const ovpair_t *find_pair(const ovpair_t *p, size_t k, unsigned a, unsigned b) {
    for (size_t i = 0; i < k; i++) if (p[i].a == a && p[i].b == b) return &p[i];
    return NULL;
}

/* 정수 격자 전수: 모든 t ∈ [0, T] 에서 v(t) 를 직접 센다 (정수 끝점이면 v 는 [t, t+1) 에서 일정). */
static void isr_brute(const isr_iv_t *iv, size_t n, const uint32_t *w, uint32_t T, peak_t *o) {
    memset(o, 0, sizeof *o);
    uint64_t v[512];
    for (uint32_t t = 0; t <= T; t++) {
        v[t] = 0;
        for (size_t i = 0; i < n; i++) if (iv[i].t0 <= t && t < iv[i].t1) v[t] += w ? w[i] : 1;
        if (v[t] > o->peak) o->peak = v[t];
    }
    if (o->peak == 0) return;
    for (uint32_t t = 0; t <= T; t++) {
        if (v[t] != o->peak) continue;
        o->at_peak++;
        if (t == 0 || v[t - 1] != o->peak) { if (++o->nwin == 1) o->from = t; }
        if (o->nwin == 1 && (t == T || v[t + 1] != o->peak)) o->to = t + 1;
    }
}
static bool peak_eq(const peak_t *a, const peak_t *b) {
    return a->peak == b->peak && a->from == b->from && a->to == b->to && a->nwin == b->nwin && a->at_peak == b->at_peak;
}

static uint64_t rect_grid(const rect_t *r, size_t n, int G) {   /* 좌표 [0, G) 격자 칸을 직접 칠한다 */
    uint64_t c = 0;
    for (int x = 0; x < G; x++)
        for (int y = 0; y < G; y++)
            for (size_t i = 0; i < n; i++)
                if (r[i].x1 <= x && x < r[i].x2 && r[i].y1 <= y && y < r[i].y2) { c++; break; }
    return c;
}

static ovpair_t g_out[4096], g_ref[4096], g_tmp[4096];
static region_t g_reg[MM_MAX + 1];

int main(void) {
    /* ---------------- Part A: 분류 ---------------- */
    {
        region_t big = {"big", 0x100, 0x100, RK_SEC}, in = {"in", 0x180, 0x10, RK_SEC}, part = {"part", 0x1F0, 0x20, RK_SEC};
        region_t same = {"same", 0x100, 0x100, RK_SEC}, head = {"head", 0x100, 0x10, RK_SEC};
        int o1, o2, o3, o4, o5;
        CHECK(ov_classify(&big, &in, &o1) == OV_CONTAIN && o1 == 0);
        CHECK(ov_classify(&in, &big, &o2) == OV_CONTAIN && o2 == 1);
        CHECK(ov_classify(&big, &part, &o3) == OV_PARTIAL && o3 == -1);
        CHECK(ov_classify(&big, &same, &o4) == OV_EQUAL && o4 == -1);
        CHECK(ov_classify(&head, &big, &o5) == OV_CONTAIN && o5 == 1);          /* 시작이 같아도 포함 */
    }
    /* ---------------- Part A: 손 계산 사례 ---------------- */
    {
        region_t touch[] = {{"a", 0, 10, 1}, {"b", 10, 10, 1}, {"c", 20, 5, 1}};
        CHECK(mm_overlaps(touch, 3, g_out, 16) == 0);                            /* 맞닿음 = 겹침 아님 */
        region_t zero[] = {{"big", 0, 100, 1}, {"z", 50, 0, 1}, {"z2", 0, 0, 1}};
        CHECK(mm_overlaps(zero, 3, g_out, 16) == 0);                             /* 크기 0 = 빈 집합 */
        region_t nest[] = {{"l0", 0, 1000, 1}, {"l1", 100, 500, 1}, {"l2", 200, 50, 1}, {"far", 2000, 1, 1}};
        size_t k = mm_overlaps(nest, 4, g_out, 16);
        if (k == 3) pairs_canon(g_out, k, 4, g_tmp);
        CHECK(k == 3 && find_pair(g_out, k, 0, 1) && find_pair(g_out, k, 0, 2) && find_pair(g_out, k, 1, 2)
              && g_out[0].kind == OV_CONTAIN && g_out[2].outer == 0);
        /* 주소 공간 끝: 0xFFFFF000 + 0x1000 = 2^32 (uint32 로는 0) */
        region_t top[] = {{"sys", 0xFFFFF000u, 0x1000, 1}, {"reg", 0xFFFFFF00u, 0x100, 1}, {"low", 0, 0x10, 1},
                          {"wrap", 0xFFFFFFF0u, 0x20, 1}, {"all", 0, 0xFFFFFFFFu, 1}};
        k = mm_overlaps(top, 5, g_out, 16);
        if (k <= 16) pairs_canon(g_out, k, 5, g_tmp);
        size_t kr = mm_brute(top, 5, g_ref, 16);
        CHECK(k == kr && pairs_eq(g_out, g_ref, k));
        const ovpair_t *p01 = find_pair(g_out, k, 0, 1), *p23 = find_pair(g_out, k, 2, 3), *p34 = find_pair(g_out, k, 3, 4);
        CHECK(p01 && p01->kind == OV_CONTAIN && p01->outer == 0 && !p23           /* wrap 은 0 근처로 돌아오지 않는다 */
              && p34 && p34->kind == OV_PARTIAL);                                 /* [0,2^32-1) vs [2^32-16, 2^32+16) */
        region_t two[] = {{"x", 5, 10, 1}, {"y", 5, 10, 1}};
        CHECK(mm_overlaps(two, 2, g_out, 16) == 1 && g_out[0].a == 0 && g_out[0].b == 1 && g_out[0].kind == OV_EQUAL);
    }
    /* ---------------- Part A: 무작위 대조 ---------------- */
    {
        size_t bad = 0, total = 0, maps = 0;
        for (int it = 0; it < 3000; it++) {
            size_t n = 1 + xs32() % 60;
            bool topband = it % 4 == 3;                 /* 4번에 1번은 주소 공간 꼭대기 근처 */
            for (size_t i = 0; i < n; i++) {
                if (i > 0 && xs32() % 8 == 0) { g_reg[i] = g_reg[i - 1]; continue; }   /* 똑같은 영역 */
                uint32_t base = topband ? 0xFFFFFFFFu - xs32() % 300 : xs32() % 200;
                uint32_t size = xs32() % 5 == 0 ? 0 : xs32() % 40;
                if (topband && xs32() % 3 == 0) size = xs32() % 400;
                g_reg[i] = (region_t){"r", base, size, RK_SEC};
            }
            size_t k = mm_overlaps(g_reg, n, g_out, 4096), kr = mm_brute(g_reg, n, g_ref, 4096);
            if (k <= 4096) pairs_canon(g_out, k, n, g_tmp);
            if (k != kr || !pairs_eq(g_out, g_ref, k)) bad++;
            total += kr; maps++;
        }
        printf("random maps: %zu maps, %zu overlapping pairs total, mismatches=%zu\n", maps, total, bad);
        CHECK(bad == 0 && total > 10000);
    }
    /* ---------------- Part A: 출력 민감성 · cap · 한계 ---------------- */
    {
        size_t n = 2000;
        for (size_t i = 0; i < n; i++) g_reg[i] = (region_t){"same", 0x1000, 0x100, RK_SEC};
        CHECK(mm_overlaps(g_reg, n, g_out, 0) == n * (n - 1) / 2);     /* k = 1999000, 아무것도 안 씀 */
        for (size_t i = 0; i < 40; i++) g_reg[i] = (region_t){"r", (uint32_t)(i * 3), 10, RK_SEC};
        size_t kr = mm_brute(g_reg, 40, g_ref, 4096), k = mm_overlaps(g_reg, 40, g_out, 5);
        bool valid = true;
        for (size_t i = 0; i < 5 && i < k; i++) if (!find_pair(g_ref, kr, g_out[i].a, g_out[i].b)) valid = false;
        CHECK(k == kr && kr == 37 * 3 + 2 + 1 && valid);                 /* cap 보다 많아도 총수는 정확 */
        CHECK(mm_overlaps(g_reg, MM_MAX + 1, g_out, 16) == SIZE_MAX);
        size_t k2 = mm_overlaps(g_reg, 40, g_out, 4096);
        pairs_canon(g_out, k2, 40, g_tmp);
        bool sorted = true;
        for (size_t i = 1; i < k2; i++)
            if (g_out[i - 1].a > g_out[i].a || (g_out[i - 1].a == g_out[i].a && g_out[i - 1].b >= g_out[i].b)) sorted = false;
        CHECK(k2 == kr && sorted && pairs_eq(g_out, g_ref, k2));
    }
    /* ---------------- Part B: ISR 최대 동시 활성 ---------------- */
    {
        peak_t p;
        isr_iv_t a[] = {{0, 10, 0}, {5, 15, 1}, {10, 20, 2}, {12, 14, 3}};
        CHECK(isr_peak(a, 4, NULL, &p) == 0 && p.peak == 3 && p.from == 12 && p.to == 14 && p.nwin == 1 && p.at_peak == 2);
        isr_iv_t chain[] = {{0, 5, 0}, {5, 10, 1}, {10, 15, 2}};                    /* 맞닿은 사슬: 깊이 1 이 끊기지 않는다 */
        CHECK(isr_peak(chain, 3, NULL, &p) == 0 && p.peak == 1 && p.from == 0 && p.to == 15 && p.nwin == 1 && p.at_peak == 15);
        isr_iv_t two[] = {{0, 4, 0}, {2, 6, 1}, {10, 14, 2}, {12, 16, 3}};
        CHECK(isr_peak(two, 4, NULL, &p) == 0 && p.peak == 2 && p.from == 2 && p.to == 4 && p.nwin == 2 && p.at_peak == 4);
        isr_iv_t z[] = {{7, 7, 0}, {9, 3, 1}};
        CHECK(isr_peak(z, 2, NULL, &p) == 0 && p.peak == 0 && p.nwin == 0 && p.from == 0 && p.to == 0);
        CHECK(isr_peak(z, 0, NULL, &p) == 0 && p.peak == 0 && p.nwin == 0);
        isr_iv_t wv[] = {{0, 10, 0}, {20, 30, 1}, {22, 28, 2}, {24, 26, 3}};        /* 개수 최대 ≠ 가중치 최대 */
        uint32_t wt[] = {100, 1, 1, 1};
        peak_t pc, pw;
        CHECK(isr_peak(wv, 4, NULL, &pc) == 0 && pc.peak == 3 && pc.from == 24 && pc.to == 26);
        CHECK(isr_peak(wv, 4, wt, &pw) == 0 && pw.peak == 100 && pw.from == 0 && pw.to == 10 && pw.nwin == 1);
        isr_iv_t top[] = {{0xFFFFFFF0u, 0xFFFFFFFFu, 0}, {0xFFFFFFF8u, 0xFFFFFFFFu, 1}};
        CHECK(isr_peak(top, 2, NULL, &p) == 0 && p.peak == 2 && p.from == 0xFFFFFFF8u && p.to == 0xFFFFFFFFu && p.at_peak == 7);
        CHECK(isr_peak(a, ISR_MAX + 1, NULL, &p) == -1);
    }
    {
        static isr_iv_t iv[64];
        static uint32_t w[64];
        size_t bad = 0, cases = 0, multi = 0;
        for (int it = 0; it < 4000; it++) {
            size_t n = xs32() % 41;
            uint32_t T = 0;
            for (size_t i = 0; i < n; i++) {
                uint32_t t0 = xs32() % 3 ? (xs32() % 20) * 5 : xs32() % 100;     /* 5 의 배수에 몰아 동점을 많이 */
                uint32_t d = xs32() % 4 == 0 ? 0 : (xs32() % 2 ? (xs32() % 6) * 5 : xs32() % 30);
                iv[i] = (isr_iv_t){t0, t0 + d, (uint8_t)i};
                w[i] = xs32() % 6;
                if (t0 + d > T) T = t0 + d;
            }
            bool weighted = it % 2;
            peak_t got, want;
            isr_peak(iv, n, weighted ? w : NULL, &got);
            isr_brute(iv, n, weighted ? w : NULL, T, &want);
            if (!peak_eq(&got, &want)) bad++;
            if (want.nwin > 1) multi++;
            cases++;
        }
        printf("random ISR logs: %zu cases (%zu with >1 peak window), mismatches=%zu\n", cases, multi, bad);
        CHECK(bad == 0 && multi > 100);
    }
    /* ---------------- Part B: 직사각형 합집합 ---------------- */
    {
        size_t pc;
        rect_t one[] = {{0, 0, 3, 4}};
        CHECK(rect_union_slab(one, 1, &pc) == 12 && pc == 1 && rect_union_segtree(one, 1) == 12);
        rect_t r3[] = {{0, 0, 2, 2}, {1, 0, 2, 3}, {1, 0, 3, 1}};
        CHECK(rect_union_slab(r3, 3, NULL) == 6 && rect_union_segtree(r3, 3) == 6);
        rect_t nest[] = {{-10, -10, 10, 10}, {-5, -5, 5, 5}, {-10, -10, 10, 10}, {3, 3, 3, 9}, {4, 8, 2, 9}};   /* 동일·포함·빈 2개 */
        CHECK(rect_union_slab(nest, 5, &pc) == 400 && pc == 3 && rect_union_segtree(nest, 5) == 400);
        rect_t adj[] = {{0, 0, 2, 2}, {2, 0, 4, 2}, {0, 2, 4, 3}};                 /* 맞닿음: 넓이 합, 띠 2개 */
        CHECK(rect_union_slab(adj, 3, &pc) == 12 && pc == 2 && rect_union_segtree(adj, 3) == 12);
        CHECK(rect_union_slab(adj, 0, &pc) == 0 && pc == 0 && rect_union_segtree(adj, 0) == 0);
        int32_t L = 1 << 30;
        rect_t huge[] = {{-L, -L, L, L}, {0, 0, L, L}};
        CHECK(rect_union_slab(huge, 2, NULL) == (uint64_t)1 << 62 && rect_union_segtree(huge, 2) == (uint64_t)1 << 62);
        CHECK(rect_union_slab(huge, RECT_MAX + 1, NULL) == UINT64_MAX && rect_union_segtree(huge, RECT_MAX + 1) == UINT64_MAX);
    }
    {
        static rect_t rr[RECT_MAX];
        size_t bad = 0, bad2 = 0;
        for (int it = 0; it < 1500; it++) {
            size_t n = xs32() % 26;
            for (size_t i = 0; i < n; i++) {
                int32_t x1 = (int32_t)(xs32() % 40), y1 = (int32_t)(xs32() % 40);
                rr[i] = (rect_t){x1, y1, x1 + (int32_t)(xs32() % 15) - 2, y1 + (int32_t)(xs32() % 15) - 2};   /* 일부는 빈 사각형 */
                if (rr[i].x2 > 40) rr[i].x2 = 40;
                if (rr[i].y2 > 40) rr[i].y2 = 40;
            }
            uint64_t g = rect_grid(rr, n, 40);
            if (rect_union_slab(rr, n, NULL) != g || rect_union_segtree(rr, n) != g) bad++;
        }
        for (int it = 0; it < 300; it++) {
            size_t n = 1 + xs32() % 200;
            for (size_t i = 0; i < n; i++) {
                int32_t x1 = (int32_t)(xs32() % (1u << 31)) - (1 << 30), y1 = (int32_t)(xs32() % (1u << 31)) - (1 << 30);
                int32_t x2 = x1 + (int32_t)(xs32() % (1u << 28)), y2 = y1 + (int32_t)(xs32() % (1u << 28));
                if (x2 > (1 << 30)) x2 = 1 << 30;
                if (y2 > (1 << 30)) y2 = 1 << 30;
                rr[i] = (rect_t){x1, y1, x2, y2};
            }
            if (rect_union_slab(rr, n, NULL) != rect_union_segtree(rr, n)) bad2++;
        }
        printf("random rects: 1500 grid cases mismatches=%zu, 300 large-coordinate cases slab!=segtree=%zu\n", bad, bad2);
        CHECK(bad == 0);
        CHECK(bad2 == 0);
    }
    /* ---------------- Part C: 링커 메모리 맵 ---------------- */
    {
        static const region_t map[] = {
            {"flash",    0x08000000u, 0x00100000u, RK_MEM}, {"sram",     0x20000000u, 0x00020000u, RK_MEM},
            {"ccm",      0x10000000u, 0x00010000u, RK_MEM}, {"sysreg",   0xE0000000u, 0x20000000u, RK_MEM},
            {"boot",     0x08000000u, 0x00010400u, RK_SEC}, {"cfg",      0x08010000u, 0x00004000u, RK_SEC},
            {"app_a",    0x08020000u, 0x00060000u, RK_SEC}, {"app_b",    0x08080000u, 0x00060000u, RK_SEC},
            {"log",      0x080E0000u, 0x00020000u, RK_SEC}, {".data",    0x20000000u, 0x00000800u, RK_SEC},
            {".bss",     0x20000800u, 0x00003000u, RK_SEC}, {".noinit",  0x20003800u, 0x00000000u, RK_SEC},
            {"heap",     0x20003800u, 0x00008000u, RK_SEC}, {"dma_buf",  0x2000B000u, 0x00001000u, RK_SEC},
            {"stack",    0x2001E000u, 0x00002000u, RK_SEC}, {"fastcode", 0x1000F000u, 0x00002000u, RK_SEC},
            {"dwt_rom",  0xFFFFF000u, 0x00002000u, RK_SEC}, {"itm",      0xE0000000u, 0x00001000u, RK_SEC},
        };
        size_t n = sizeof map / sizeof map[0];
        mmrep_t rep;
        printf("== memory map check: %zu regions ==\n", n);
        int rc = mm_report(map, n, &rep, stdout);
        CHECK(rc == 0 && rep.pairs == 15 && rep.sec_overlap == 2 && rep.mem_overlap == 0 && rep.straddle == 2
              && rep.orphan == 2 && rep.oob == 1 && rep.empty == 1);
        size_t k = mm_overlaps(map, n, g_out, 4096);
        pairs_canon(g_out, k, n, g_tmp);
        const ovpair_t *bc = find_pair(g_out, k, 4, 5), *ab = find_pair(g_out, k, 6, 7), *si = find_pair(g_out, k, 3, 17);
        CHECK(bc && bc->kind == OV_PARTIAL && !ab && si && si->kind == OV_CONTAIN && si->outer == 0);
    }
    /* ---------------- Part C: MPU 영역 표 ---------------- */
    {
        static const region_t v7[] = {
            {"flash", 0x08000000u, 0x00100000u, 0}, {"sram", 0x20000000u, 0x00020000u, 0},
            {"dma_nc", 0x2000C000u, 0x00004000u, 0}, {"stk_grd", 0x2001DFE0u, 0x20u, 0},
            {"periph", 0x40000000u, 0x20000000u, 0},
        };
        static const region_t v7bad[] = {{"sram", 0x20000000u, 0x00020000u, 0}, {"dma_nc", 0x2000B000u, 0x00004000u, 0},
                                         {"periph", 0x40000000u, 0x00018000u, 0}};
        static const region_t v8[] = {
            {"flash", 0x08000000u, 0x00100000u, 0},   {"sram_lo", 0x20000000u, 0x0000C000u, 0},
            {"dma_nc", 0x2000C000u, 0x00004000u, 0},  {"sram_mid", 0x20010000u, 0x0000DFE0u, 0},
            {"stk_grd", 0x2001DFE0u, 0x20u, 0},       {"sram_hi", 0x2001E000u, 0x00002000u, 0},
            {"periph", 0x40000000u, 0x00018000u, 0},
        };
        size_t ov7, ov7b, ov8p, ov8;
        printf("== MPU: PMSAv7 table ==\n");
        size_t e7 = mpu_check(v7, 5, false, &ov7, stdout);
        printf("== MPU: PMSAv7 table with mistakes ==\n");
        size_t e7b = mpu_check(v7bad, 3, false, &ov7b, stdout);
        printf("== MPU: same v7 table ported to PMSAv8 as-is ==\n");
        size_t e8p = mpu_check(v7, 5, true, &ov8p, stdout);
        printf("== MPU: PMSAv8 table split around the holes ==\n");
        size_t e8 = mpu_check(v8, 7, true, &ov8, stdout);
        printf("   v7: errors=%zu overlaps=%zu | v7bad: errors=%zu | v8 ported: errors=%zu | v8 split: errors=%zu overlaps=%zu\n",
               e7, ov7, e7b, e8p, e8, ov8);
        CHECK(e7 == 0 && ov7 == 2 && e7b == 2 && ov7b == 1);
        CHECK(e8p == 2 && e8 == 0 && ov8 == 0);
    }
    /* ---------------- Part C: 중첩 ISR 트레이스 ---------------- */
    {
        static const char *const nm[] = {"USART1", "DMA2_S0", "SysTick", "TIM2", "EXTI0", "ADC", "SysTick", "I2C1_EV",
                                         "SysTick", "CAN1_RX0", "CAN1_TX", "SysTick"};
        static const isr_iv_t tr[] = {
            {100, 160, 0}, {120, 140, 1}, {130, 135, 2}, {300, 420, 3}, {310, 400, 4}, {320, 390, 5},
            {330, 333, 6}, {500, 560, 7}, {530, 532, 8}, {600, 640, 9}, {640, 660, 10}, {1000, 1003, 11},
        };
        static const uint32_t stk[] = {96, 200, 64, 72, 48, 40, 64, 520, 64, 120, 80, 64};   /* 바이트, 예외 프레임 32 B 포함 */
        static const uint8_t prio[] = {5, 1, 0, 5, 3, 1, 0, 6, 0, 7, 7, 0};               /* 선점 우선순위 (작을수록 급함) */
        size_t n = sizeof tr / sizeof tr[0];
        peak_t pc, ps;
        uint16_t ids[16];
        isr_peak(tr, n, NULL, &pc);
        isr_peak(tr, n, stk, &ps);
        printf("== ISR trace: %zu entries ==\n", n);
        size_t m = isr_active_at(tr, n, pc.from, ids);
        printf("  max nesting depth %" PRIu64 " in [%" PRIu32 ", %" PRIu32 ") us, windows=%" PRIu32 ", active:", pc.peak, pc.from, pc.to, pc.nwin);
        for (size_t i = 0; i < m; i++) printf(" %s", nm[ids[i]]);
        m = isr_active_at(tr, n, ps.from, ids);
        printf("\n  max ISR stack   %" PRIu64 " B in [%" PRIu32 ", %" PRIu32 ") us, active:", ps.peak, ps.from, ps.to);
        for (size_t i = 0; i < m; i++) printf(" %s", nm[ids[i]]);
        uint32_t bound = isr_static_bound(prio, stk, n);
        printf("\n  static bound (sum over priority levels of max stack) = %" PRIu32 " B\n", bound);
        CHECK(pc.peak == 4 && pc.from == 330 && pc.to == 333 && pc.nwin == 1);
        CHECK(ps.peak == 584 && ps.from == 530 && ps.to == 532 && bound == 1048);
        isr_iv_t tail[] = {tr[9], tr[10]};                                     /* tail-chaining: 맞닿음, 동시 아님 */
        CHECK(isr_peak(tail, 2, NULL, &pc) == 0 && pc.peak == 1 && pc.from == 600 && pc.to == 660);
    }
    /* ---------------- Part C: LCD 더티 사각형 ---------------- */
    {
        static const rect_t d[] = {
            {0, 0, 240, 20},     /* 상태 바 */          {180, 0, 240, 20},   /* 시계 (상태 바 안) */
            {200, 2, 236, 18},   /* 배터리 아이콘 */    {10, 40, 230, 200},  /* 그래프 */
            {100, 100, 140, 140},/* 커서 (그래프 안) */ {20, 180, 120, 230}, /* 버튼 */
            {60, 150, 180, 250}, /* 토스트 */
        };
        size_t n = sizeof d / sizeof d[0], pcs;
        uint64_t sum = 0, ua = rect_union_slab(d, n, &pcs), us = rect_union_segtree(d, n);
        int32_t bx1 = 240, by1 = 320, bx2 = 0, by2 = 0;
        for (size_t i = 0; i < n; i++) {
            sum += (uint64_t)(d[i].x2 - d[i].x1) * (uint64_t)(d[i].y2 - d[i].y1);
            if (d[i].x1 < bx1) bx1 = d[i].x1;
            if (d[i].y1 < by1) by1 = d[i].y1;
            if (d[i].x2 > bx2) bx2 = d[i].x2;
            if (d[i].y2 > by2) by2 = d[i].y2;
        }
        uint64_t bbox = (uint64_t)(bx2 - bx1) * (uint64_t)(by2 - by1);
        printf("== LCD 240x320 RGB565 dirty rects: %zu ==\n", n);
        printf("  each rect sent separately: %6" PRIu64 " px = %6" PRIu64 " B\n", sum, sum * 2);
        printf("  bounding box             : %6" PRIu64 " px = %6" PRIu64 " B\n", bbox, bbox * 2);
        printf("  exact union (slab pieces): %6" PRIu64 " px = %6" PRIu64 " B in %zu windows\n", ua, ua * 2, pcs);
        printf("  full frame               : %6d px = %6d B\n", 240 * 320, 240 * 320 * 2);
        CHECK(ua == us && ua == rect_grid(d, n, 320));
        CHECK(ua < sum && ua < bbox && pcs >= 1);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* ALGO_A09_NO_MAIN */
