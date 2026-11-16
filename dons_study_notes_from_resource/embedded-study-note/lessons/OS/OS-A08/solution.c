/* OS-A08 로그 구조 FS와 FTL: 세그먼트 클리닝 시뮬레이터
 *
 * 저장소 = 세그먼트 nseg 개 × 블록 S 개. 논리 블록(LBA)은 절대 제자리에 덮어쓰지 않는다.
 *   map[lba]      → phys = seg*S + off   (LFS 의 imap, SSD 의 L2P)
 *   summary[phys] → (주인 lba, 버전)       (LFS segment summary, SSD 의 OOB/P2L)
 *   usage[seg]    → live 블록 수, 가장 최근 데이터 시각 mtime   (LFS segment usage table)
 * 쓰기는 "헤드" 세그먼트 끝에 붙인다 (out-of-place). 옛 자리는 쓰레기가 되고, free 세그먼트가 워터마크 아래로
 * 내려가면 클리너가 victim 을 골라 live 블록만 옮긴 뒤 세그먼트를 지운다 (= SSD GC 의 블록 erase).
 *
 *   Part A  block_is_live, log_append, lfs_write              — 로그에 붙이기, summary 로 liveness 판정
 *   Part B  pick_victim (greedy / cost-benefit / FIFO), clean_pass, maybe_clean, 지표(WA, 세그먼트/GiB)
 *   Part C  lfs_rebuild_map — summary 만 훑어 map 을 다시 만든다 (LFS roll-forward ≈ FTL 의 OOB 스캔)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 * 데모·연습이 엔진만 쓸 때: #define LFS_NO_MAIN 후 #include "solution.c"
 */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GIVEN   __attribute__((unused))   /* 시작 코드가 아직 안 부르는 도우미의 -Wunused 경고 끄기 */
#define TODO_FN __attribute__((unused))   /* TODO 함수 (데모·연습이 일부만 써도 경고 없게) */
#define NONE  UINT32_MAX
#define BLK_BYTES 4096u                       /* 지표 계산용 블록 크기 (4 KiB) */

enum { POL_GREEDY = 0, POL_COSTBEN = 1, POL_FIFO = 2 };
enum { SEG_FREE = 0, SEG_OPEN = 1, SEG_FULL = 2, SEG_VICTIM = 3 };
static GIVEN const char *const POL_NAME[] = { "greedy", "cost-benefit", "FIFO" };

typedef struct {
    uint32_t nseg, S, nlba;
    int      policy;
    uint32_t wm_low, wm_high;   /* nfree < wm_low 이면 청소 시작, nfree >= wm_high 가 될 때까지 */
    uint32_t reserve;           /* 호스트는 nfree > reserve 일 때만 새 세그먼트를 연다 (클리너 몫) */
    uint32_t batch;             /* 청소 패스 한 번에 고르는 victim 수 (1 이상) */
    bool     separate;          /* 클리너 출력을 호스트 헤드와 다른 헤드에 (hot/cold 분리) */
    bool     age_sort;          /* 모은 live 블록을 데이터 나이순으로 정렬해서 쓴다 (LFS 논문) */
} lfs_cfg_t;

typedef struct {
    uint64_t host_w;            /* 호스트가 쓴 블록 */
    uint64_t clean_w;           /* 클리너가 복사한 블록 */
    uint64_t segs_cleaned;      /* 지운 세그먼트 */
    uint64_t passes;            /* 청소 패스 */
    uint64_t victim_live;       /* 고를 때 victim 들의 live 합 (끝나면 clean_w 와 같아야 함) */
    uint64_t hist[10];          /* victim 사용률 u = live/S 히스토그램 (0.1 간격) */
} lfs_stats_t;

typedef struct {
    lfs_cfg_t c;
    uint32_t *map, *ver;                 /* lba 기준 */
    uint32_t *sum_lba, *sum_ver;         /* phys 기준 — segment summary */
    uint64_t *data, *btime;              /* phys 기준 — 내용 (lba<<32 | ver), 호스트가 쓴 시각(복사해도 유지) */
    uint32_t *live, *wp, *erases;        /* seg 기준 */
    uint64_t *mtime, *opened;            /* seg 기준 — 최근 데이터 시각, 열린 순번(FIFO 용) */
    uint8_t  *state;
    uint32_t *freeq, fq_head, nfree;     /* free 세그먼트 FIFO 링 */
    uint32_t hhead, chead;               /* 호스트 헤드, 클리너 헤드 (separate 일 때만 씀) */
    uint64_t now, open_seq, erase_total;
    struct gat { uint64_t bt; uint32_t phys; } *gather;   /* 청소 패스가 모은 live 블록 */
    lfs_stats_t st;
    /* 테스트 훅: 세그먼트를 지울 때마다 lfs_check 를 돌린다 */
    bool     check_on_erase;
    uint64_t erase_checks, erase_check_fail, lost_live;
} lfs_t;

static inline uint64_t stamp(uint32_t lba, uint32_t ver) { return ((uint64_t)lba << 32) | ver; }
static inline GIVEN uint32_t seg_of(const lfs_t *L, uint32_t phys) { return phys / L->c.S; }

static int lfs_check(lfs_t *L);   /* 아래 (주어짐) */

/* ============================ 초기화·세그먼트 관리 (주어짐) ============================ */
static GIVEN void lfs_free(lfs_t *L) {
    free(L->map); free(L->ver); free(L->sum_lba); free(L->sum_ver); free(L->data); free(L->btime);
    free(L->live); free(L->wp); free(L->erases); free(L->mtime); free(L->opened); free(L->state);
    free(L->freeq); free(L->gather);
    memset(L, 0, sizeof *L);
}

static GIVEN int lfs_init(lfs_t *L, const lfs_cfg_t *c) {
    memset(L, 0, sizeof *L);
    L->c = *c;
    if (L->c.batch == 0) L->c.batch = 1;
    size_t P = (size_t)c->nseg * c->S;
    L->map = malloc(c->nlba * sizeof *L->map);   L->ver = calloc(c->nlba, sizeof *L->ver);
    L->sum_lba = malloc(P * sizeof *L->sum_lba); L->sum_ver = calloc(P, sizeof *L->sum_ver);
    L->data = calloc(P, sizeof *L->data);        L->btime = calloc(P, sizeof *L->btime);
    L->live = calloc(c->nseg, sizeof *L->live);  L->wp = calloc(c->nseg, sizeof *L->wp);
    L->erases = calloc(c->nseg, sizeof *L->erases);
    L->mtime = calloc(c->nseg, sizeof *L->mtime); L->opened = calloc(c->nseg, sizeof *L->opened);
    L->state = calloc(c->nseg, 1);               L->freeq = malloc(c->nseg * sizeof *L->freeq);
    L->gather = malloc((size_t)L->c.batch * c->S * sizeof *L->gather);
    if (!L->map || !L->ver || !L->sum_lba || !L->sum_ver || !L->data || !L->btime || !L->live || !L->wp ||
        !L->erases || !L->mtime || !L->opened || !L->state || !L->freeq || !L->gather) { lfs_free(L); return -1; }
    for (uint32_t i = 0; i < c->nlba; i++) L->map[i] = NONE;
    for (size_t i = 0; i < P; i++) L->sum_lba[i] = NONE;
    for (uint32_t s = 0; s < c->nseg; s++) L->freeq[s] = s;
    L->nfree = c->nseg;
    L->hhead = L->chead = NONE;
    return 0;
}

/* free 링에서 하나 꺼낸다. 호스트는 reserve 를 남겨야 하고, 클리너는 마지막 하나까지 쓸 수 있다. */
static GIVEN uint32_t seg_take(lfs_t *L, bool for_cleaner) {
    if (L->nfree == 0 || (!for_cleaner && L->nfree <= L->c.reserve)) return NONE;
    uint32_t s = L->freeq[L->fq_head];
    L->fq_head = (L->fq_head + 1) % L->c.nseg;
    L->nfree--;
    L->state[s] = SEG_OPEN; L->wp[s] = 0; L->live[s] = 0; L->mtime[s] = 0;
    L->opened[s] = ++L->open_seq;
    return s;
}

/* 세그먼트 지우기 (= NAND 블록 erase). live 가 남아 있으면 데이터를 잃는 것이므로 lost_live 로 센다. */
static GIVEN void seg_erase(lfs_t *L, uint32_t s) {
    L->lost_live += L->live[s];
    for (uint32_t o = 0; o < L->c.S; o++) L->sum_lba[s * L->c.S + o] = NONE;
    L->state[s] = SEG_FREE; L->wp[s] = 0; L->live[s] = 0; L->mtime[s] = 0;
    L->erases[s]++; L->erase_total++;
    L->freeq[(L->fq_head + L->nfree) % L->c.nseg] = s;
    L->nfree++;
    if (L->check_on_erase) { L->erase_checks++; if (lfs_check(L) != 0) L->erase_check_fail++; }
}

/* 슬롯 하나에 기록: 내용 + summary + usage table + map. 헤드에 자리가 있다는 것은 호출자가 보장한다. */
static GIVEN uint32_t put_slot(lfs_t *L, uint32_t s, uint32_t lba, uint32_t ver, uint64_t bt) {
    uint32_t phys = s * L->c.S + L->wp[s]++;
    L->data[phys] = stamp(lba, ver);
    L->sum_lba[phys] = lba; L->sum_ver[phys] = ver; L->btime[phys] = bt;
    L->live[s]++;
    if (bt > L->mtime[s]) L->mtime[s] = bt;
    L->map[lba] = phys;
    if (L->wp[s] == L->c.S) L->state[s] = SEG_FULL;      /* 꽉 차면 닫힌다 → victim 후보 */
    return phys;
}

static GIVEN int lfs_read(const lfs_t *L, uint32_t lba, uint64_t *out) {
    if (lba >= L->c.nlba || L->map[lba] == NONE) return -1;
    *out = L->data[L->map[lba]];
    return 0;
}

/* ============================ Part A — 로그에 붙이기와 liveness ============================ */

/* A1. phys 의 블록이 아직 살아 있나? summary 의 주인 lba 를 보고, map 이 정확히 여기를 가리키면 live. */
static TODO_FN bool block_is_live(const lfs_t *L, uint32_t phys) {
    uint32_t lba = L->sum_lba[phys];
    return lba != NONE && L->map[lba] == phys;
}

/* A2. *head 세그먼트에 블록 하나를 붙인다. 헤드가 없거나 꽉 찼으면 새 세그먼트를 연다
 *     (cleaner=true 면 reserve 까지 쓸 수 있음). 열 세그먼트가 없으면 NONE. 성공하면 새 phys. */
static TODO_FN uint32_t log_append(lfs_t *L, uint32_t *head, bool cleaner, uint32_t lba, uint32_t ver, uint64_t bt) {
    if (*head == NONE || L->state[*head] != SEG_OPEN) {
        uint32_t s = seg_take(L, cleaner);
        if (s == NONE) return NONE;
        *head = s;
    }
    return put_slot(L, *head, lba, ver, bt);
}

static int maybe_clean(lfs_t *L);

/* A3. 호스트 쓰기: 헤드에 자리가 없으면 (필요하면 청소한 뒤) 새 세그먼트를 열고, 옛 자리를 죽이고, 새 자리에 붙인다.
 *     시각 now 는 호스트 쓰기마다 1 씩 (btime = ++now). 실패하면 -1 (공간 없음), 이전 데이터는 그대로. */
static TODO_FN int lfs_write(lfs_t *L, uint32_t lba) {
    if (lba >= L->c.nlba) return -1;
    if (L->hhead == NONE || L->state[L->hhead] != SEG_OPEN) {
        L->hhead = NONE;
        if (maybe_clean(L) < 0) return -1;
        if (L->hhead == NONE || L->state[L->hhead] != SEG_OPEN) {   /* separate=false 면 클리너가 열었을 수 있다 */
            uint32_t s = seg_take(L, false);
            if (s == NONE) return -1;
            L->hhead = s;
        }
    }
    uint32_t old = L->map[lba];                 /* 청소가 옮겼을 수 있으니 청소 "뒤" 에 읽는다 */
    if (old != NONE) L->live[seg_of(L, old)]--;
    uint32_t v = ++L->ver[lba];
    put_slot(L, L->hhead, lba, v, ++L->now);
    L->st.host_w++;
    return 0;
}

/* ============================ Part B — 클리너 ============================ */

/* cost-benefit 비교: (1-u)·age/(1+u) = (S-l)·age/(S+l). 곱셈으로 비교해 나눗셈·부동소수를 피한다.
 * S ≤ 4096, age < 2^38 이면 (S-l)·age·(S+l) < 2^63. */
#define AGE_CAP ((uint64_t)1 << 38)

/* B1. victim 하나 고르기. 후보 = SEG_FULL 이고 live < S. 없으면 NONE.
 *   greedy       live 최소 (같으면 먼저 열린 것)
 *   cost-benefit (S-live)·age/(S+live) 최대, age = now - mtime (같으면 live 작은 것, 그다음 먼저 열린 것)
 *   FIFO         가장 먼저 열린 것 */
static TODO_FN uint32_t pick_victim(const lfs_t *L) {
    uint32_t best = NONE, S = L->c.S;
    for (uint32_t s = 0; s < L->c.nseg; s++) {
        if (L->state[s] != SEG_FULL || L->live[s] >= S) continue;
        if (best == NONE) { best = s; continue; }
        uint32_t ls = L->live[s], lb = L->live[best];
        bool better;
        if (L->c.policy == POL_GREEDY) {
            better = ls < lb || (ls == lb && L->opened[s] < L->opened[best]);
        } else if (L->c.policy == POL_COSTBEN) {
            uint64_t as = L->now - L->mtime[s], ab = L->now - L->mtime[best];
            if (as > AGE_CAP) as = AGE_CAP;
            if (ab > AGE_CAP) ab = AGE_CAP;
            uint64_t x = (uint64_t)(S - ls) * as * (S + lb), y = (uint64_t)(S - lb) * ab * (S + ls);
            better = x > y || (x == y && (ls < lb || (ls == lb && L->opened[s] < L->opened[best])));
        } else {
            better = L->opened[s] < L->opened[best];
        }
        if (better) best = s;
    }
    return best;
}

static GIVEN int cmp_gat(const void *a, const void *b) {
    const struct gat *x = a, *y = b;
    if (x->bt != y->bt) return x->bt < y->bt ? -1 : 1;
    return x->phys < y->phys ? -1 : x->phys > y->phys;
}

/* B2. 청소 패스 한 번: victim 을 batch 개까지 고르고 (클리너가 쓸 자리가 모자라면 덜 고름), live 블록을 모아
 *     (age_sort 면 데이터 나이순 정렬) 클리너 헤드 (separate=false 면 호스트 헤드) 로 복사한 뒤 victim 을 지운다.
 *     victim 마다 hist[u*10] 와 victim_live 를 센다. 지운 세그먼트 수를 돌려주고, 하나도 못 고르면 -1. */
static TODO_FN int clean_pass(lfs_t *L) {
    uint32_t S = L->c.S, vic[64], nv = 0, ng = 0;
    uint32_t *head = L->c.separate ? &L->chead : &L->hhead;
    uint32_t room = L->nfree * S;
    if (*head != NONE && L->state[*head] == SEG_OPEN) room += S - L->wp[*head];
    uint32_t maxv = L->c.batch < 64 ? L->c.batch : 64;
    while (nv < maxv) {
        uint32_t s = pick_victim(L);
        if (s == NONE || ng + L->live[s] > room) break;
        L->state[s] = SEG_VICTIM;                         /* 다음 pick 에서 빠지도록 */
        vic[nv++] = s;
        L->st.victim_live += L->live[s];
        uint32_t b = L->live[s] * 10 / S;
        L->st.hist[b > 9 ? 9 : b]++;
        for (uint32_t o = 0; o < L->wp[s]; o++) {
            uint32_t p = s * S + o;
            if (block_is_live(L, p)) { L->gather[ng].bt = L->btime[p]; L->gather[ng].phys = p; ng++; }
        }
    }
    if (nv == 0) return -1;
    if (L->c.age_sort) qsort(L->gather, ng, sizeof *L->gather, cmp_gat);
    for (uint32_t i = 0; i < ng; i++) {
        uint32_t p = L->gather[i].phys;
        uint32_t np = log_append(L, head, true, L->sum_lba[p], L->sum_ver[p], L->btime[p]);
        if (np == NONE) return -1;                        /* room 계산상 일어나지 않는다 */
        L->live[seg_of(L, p)]--;
        L->st.clean_w++;
    }
    for (uint32_t i = 0; i < nv; i++) { seg_erase(L, vic[i]); L->st.segs_cleaned++; }
    L->st.passes++;
    return (int)nv;
}

/* B3. 워터마크: nfree < wm_low 일 때만 시작해서 nfree >= wm_high 가 될 때까지 패스를 돌린다.
 *     패스가 실패하거나 nseg 번 돌아도 안 되면 -1 (진전 없음). */
static TODO_FN int maybe_clean(lfs_t *L) {
    if (L->nfree >= L->c.wm_low) return 0;
    for (uint32_t guard = 0; L->nfree < L->c.wm_high; guard++) {
        if (guard > L->c.nseg || clean_pass(L) < 0) return L->nfree > L->c.reserve ? 0 : -1;
    }
    return 0;
}

/* B4. 지표. WA = (호스트 + 클리너 복사) / 호스트.  세그먼트/GiB = 지운 세그먼트 / (호스트 바이트 / 2^30). */
static TODO_FN double stats_wa(const lfs_stats_t *s) {
    return s->host_w ? (double)(s->host_w + s->clean_w) / (double)s->host_w : 0.0;
}
static TODO_FN double stats_segs_per_gib(const lfs_stats_t *s) {
    return s->host_w ? (double)s->segs_cleaned * 1073741824.0 / ((double)s->host_w * BLK_BYTES) : 0.0;
}

/* ============================ Part C — summary 로 map 다시 만들기 ============================ */

/* C1. FREE 가 아닌 세그먼트의 쓰인 슬롯 (off < wp) 만 훑어, lba 마다 버전이 가장 큰 슬롯을 map_out 에,
 *     세그먼트별 그런 슬롯 수를 live_out 에 넣는다. 같은 (lba, 최대 버전) 슬롯이 둘이면 -1 (모호).
 *     돌려주는 값 = 매핑된 lba 수. (전원 차단 뒤 마운트: LFS roll-forward, SSD 의 OOB/P2L 스캔) */
static TODO_FN int lfs_rebuild_map(const lfs_t *L, uint32_t *map_out, uint32_t *live_out) {
    uint32_t S = L->c.S, n = 0;
    for (uint32_t i = 0; i < L->c.nlba; i++) map_out[i] = NONE;
    for (uint32_t s = 0; s < L->c.nseg; s++) live_out[s] = 0;
    for (uint32_t s = 0; s < L->c.nseg; s++) {
        if (L->state[s] == SEG_FREE) continue;
        for (uint32_t o = 0; o < L->wp[s]; o++) {
            uint32_t p = s * S + o, lba = L->sum_lba[p];
            if (lba == NONE || lba >= L->c.nlba) continue;
            uint32_t cur = map_out[lba];
            if (cur == NONE) { map_out[lba] = p; n++; continue; }
            if (L->sum_ver[p] == L->sum_ver[cur]) return -1;
            if (L->sum_ver[p] > L->sum_ver[cur]) map_out[lba] = p;
        }
    }
    for (uint32_t i = 0; i < L->c.nlba; i++)
        if (map_out[i] != NONE) live_out[map_out[i] / S]++;
    return (int)n;
}

/* ============================ 검사기·워크로드·해석 모델 (주어짐) ============================ */

/* 불변식 (TODO 함수를 쓰지 않고 독립적으로 다시 계산):
 *  1 map[lba] 는 FREE 아닌 세그먼트의 쓰인 슬롯, summary 의 주인·버전이 맞고 내용 = stamp(lba, ver[lba])
 *  2 phys 하나를 두 lba 가 가리키지 않는다 (각 live lba 는 정확히 한 번)
 *  3 세그먼트마다 summary+map 으로 다시 센 live 수 == usage table 의 live
 *  4 mtime == 쓰인 슬롯들의 btime 최댓값,  FREE 세그먼트는 wp=0·live=0·summary 비어 있음
 *  5 nfree == FREE 세그먼트 수, free 링의 원소는 모두 FREE 이고 중복 없음
 * 위반 수를 돌려준다 (0 이면 OK). */
static GIVEN int lfs_check(lfs_t *L) {
    uint32_t S = L->c.S, P = L->c.nseg * S;
    int bad = 0;
    uint8_t *used = calloc(P, 1);
    uint32_t *cnt = calloc(L->c.nseg, sizeof *cnt);
    uint8_t *inq = calloc(L->c.nseg, 1);
    if (!used || !cnt || !inq) { free(used); free(cnt); free(inq); return 1; }
    for (uint32_t lba = 0; lba < L->c.nlba; lba++) {
        uint32_t p = L->map[lba];
        if (p == NONE) continue;
        uint32_t s = p / S;
        if (p >= P || L->state[s] == SEG_FREE || p % S >= L->wp[s] || L->sum_lba[p] != lba ||
            L->sum_ver[p] != L->ver[lba] || L->data[p] != stamp(lba, L->ver[lba])) { bad++; continue; }
        if (used[p]++) bad++;
        cnt[s]++;
    }
    uint32_t nf = 0;
    for (uint32_t s = 0; s < L->c.nseg; s++) {
        if (L->state[s] == SEG_FREE) {
            nf++;
            if (L->wp[s] || L->live[s]) bad++;
            for (uint32_t o = 0; o < S; o++) if (L->sum_lba[s * S + o] != NONE) { bad++; break; }
            continue;
        }
        if (cnt[s] != L->live[s]) bad++;
        uint64_t mt = 0;
        for (uint32_t o = 0; o < L->wp[s]; o++) if (L->btime[s * S + o] > mt) mt = L->btime[s * S + o];
        if (mt != L->mtime[s]) bad++;
        if (L->state[s] == SEG_FULL && L->wp[s] != S) bad++;
    }
    if (nf != L->nfree) bad++;
    for (uint32_t i = 0; i < L->nfree; i++) {
        uint32_t s = L->freeq[(L->fq_head + i) % L->c.nseg];
        if (L->state[s] != SEG_FREE || inq[s]++) bad++;
    }
    free(used); free(cnt); free(inq);
    return bad;
}

enum { WL_UNIFORM = 0, WL_HOTCOLD = 1, WL_SEQ = 2 };
static GIVEN const char *const WL_NAME[] = { "uniform", "hot-cold 90/10", "sequential" };

static inline uint32_t xs32(uint32_t *x) { *x ^= *x << 13; *x ^= *x >> 17; *x ^= *x << 5; return *x; }

/* hot-cold: 쓰기의 90 % 가 앞쪽 10 % LBA 로. sequential: 0,1,2,...,nlba-1,0,1,... */
static GIVEN uint32_t next_lba(int wl, uint32_t nlba, uint32_t *rng, uint64_t i) {
    if (wl == WL_SEQ) return (uint32_t)(i % nlba);
    if (wl == WL_HOTCOLD) {
        uint32_t hot = nlba / 10;
        if (xs32(rng) % 100 < 90) return xs32(rng) % hot;
        return hot + xs32(rng) % (nlba - hot);
    }
    return xs32(rng) % nlba;
}

typedef struct {
    double wa, segs_per_gib, mean_free, mean_victim_u;
    lfs_stats_t st;
    int integrity_bad;          /* 끝나고 모든 lba 를 읽어 기대 버전과 비교한 불일치 수 */
    int write_fail;             /* 실패한 쓰기가 있었으면 1 */
} run_t;

/* 순차로 한 번 채우고 (prefill), warm × nlba 번 쓰고 (정상 상태로), 지표를 0 으로 한 뒤 meas × nlba 번 측정.
 * 호스트 쪽 기대 버전 ref[] 를 따로 들고 있다가 끝에서 전부 읽어 대조한다. */
static GIVEN int run_sim(const lfs_cfg_t *cfg, int wl, uint32_t seed, double warm, double meas, run_t *r) {
    lfs_t L;
    memset(r, 0, sizeof *r);
    if (lfs_init(&L, cfg) != 0) return -1;
    uint32_t *ref = calloc(cfg->nlba, sizeof *ref), rng = seed ? seed : 1;
    if (!ref) { lfs_free(&L); return -1; }
    uint64_t nw = (uint64_t)(warm * cfg->nlba), nm = (uint64_t)(meas * cfg->nlba), i = 0;
    double free_sum = 0;
    for (uint32_t b = 0; b < cfg->nlba; b++) {
        if (lfs_write(&L, b) != 0) { r->write_fail = 1; goto done; }
        ref[b]++;
    }
    for (uint64_t k = 0; k < nw + nm; k++) {
        if (k == nw) memset(&L.st, 0, sizeof L.st);
        uint32_t b = next_lba(wl, cfg->nlba, &rng, i++);
        if (lfs_write(&L, b) != 0) { r->write_fail = 1; goto done; }
        ref[b]++;
        if (k >= nw) free_sum += L.nfree;
    }
    r->st = L.st;
    r->wa = stats_wa(&L.st);
    r->segs_per_gib = stats_segs_per_gib(&L.st);
    r->mean_free = nm ? free_sum / (double)nm : 0;
    r->mean_victim_u = L.st.segs_cleaned ? (double)L.st.victim_live / ((double)L.st.segs_cleaned * cfg->S) : 0;
done:
    for (uint32_t b = 0; b < cfg->nlba; b++) {
        uint64_t got;
        if (lfs_read(&L, b, &got) != 0 || got != stamp(b, ref[b])) r->integrity_bad++;
    }
    free(ref);
    lfs_free(&L);
    return r->write_fail ? -1 : 0;
}

/* 해석 모델 (균등 랜덤 덮어쓰기). α = T/U (물리 블록 / 논리 블록).
 *  FIFO: 세그먼트가 쓰인 뒤 로그가 한 바퀴 (T(1-u) 호스트 쓰기) 도는 동안 각 블록이 살아남을 확률 → u = e^{-α(1-u)},
 *        WA = 1/(1-u).  (Desnoyers 2012 Eq.4 와 같은 식: A = α/(α + W(-α e^{-α})))
 *  greedy: Desnoyers 2012 Eq.11–12 (DS-A09 demo_waf.c 에서 측정과 0.5 % 안으로 맞춰 본 식) */
static GIVEN double model_fifo(double alpha) {
    double lo = 0.0, hi = 1.0 - 1e-12;               /* f(u) = u - e^{-α(1-u)} 의 (0,1) 근 */
    for (int k = 0; k < 200; k++) {
        double m = 0.5 * (lo + hi);
        if (m - exp(-alpha * (1.0 - m)) < 0) lo = m; else hi = m;
    }
    return 1.0 / (1.0 - 0.5 * (lo + hi));
}
static double lambert_w0(double y) {                 /* y ∈ (-1/e, 0), 주가지 w ∈ (-1, 0), 뉴턴 */
    double w = -0.5;
    for (int i = 0; i < 100; i++) {
        double e = exp(w), f = w * e - y;
        w -= f / (e * (w + 1.0));
        if (w <= -1.0) w = -0.9999999;
    }
    return w;
}
static GIVEN double model_greedy(double alpha, double np) {
    double c = (1.0 + 1.0 / (2.0 * np)) * alpha;
    double x0 = 0.5 - (np / alpha) * lambert_w0(-c * exp(-c));
    return np / (np - (x0 - 1.0));
}
/* 측정에 쓴 실제 여유: 평균 free 세그먼트와 열린 헤드 반 개는 데이터를 담지 않으므로 T 에서 뺀다. */
static GIVEN double eff_alpha(const lfs_cfg_t *c, double mean_free) {
    double heads = c->separate ? 2.0 : 1.0;
    return ((double)c->nseg - mean_free - 0.5 * heads) * c->S / (double)c->nlba;
}

#ifndef LFS_NO_MAIN
/* ================================== 테스트 ================================== */
static int g_pass, g_fail;
#define CHECK(expr) do { if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
                         else { g_fail++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static lfs_cfg_t cfg_of(uint32_t nseg, uint32_t S, double u, int pol, bool sep, bool sort, uint32_t batch) {
    lfs_cfg_t c = { nseg, S, (uint32_t)(u * nseg * S), pol, 2, 3, 1, batch, sep, sort };
    return c;
}
static bool approx(double a, double b, double rel) { return fabs(a - b) <= rel * fabs(b); }

static void part_a(void) {
    printf("-- Part A: out-of-place append, segment summary, liveness\n");
    lfs_t L;
    lfs_cfg_t c = { 8, 4, 16, POL_GREEDY, 2, 3, 1, 1, false, false };
    uint64_t v;
    CHECK(lfs_init(&L, &c) == 0 && L.nfree == 8 && lfs_read(&L, 0, &v) == -1);
    int ok = 1;
    for (uint32_t b = 0; b < 4; b++) ok &= lfs_write(&L, b) == 0;
    CHECK(ok && L.hhead == 0 && L.map[0] == 0 && L.map[3] == 3 && L.live[0] == 4 && L.state[0] == SEG_FULL);
    CHECK(L.sum_lba[2] == 2 && lfs_read(&L, 2, &v) == 0 && v == stamp(2, 1));
    CHECK(lfs_write(&L, 4) == 0 && L.hhead == 1 && L.map[4] == 4 && L.nfree == 6);
    CHECK(lfs_write(&L, 1) == 0 && L.map[1] == 5 && L.live[0] == 3 && L.live[1] == 2);
    CHECK(!block_is_live(&L, 1) && block_is_live(&L, 5) && L.sum_lba[1] == 1);      /* 옛 summary 는 남아 있다 */
    CHECK(lfs_read(&L, 1, &v) == 0 && v == stamp(1, 2) && L.ver[1] == 2);
    CHECK(L.mtime[0] == 4 && L.mtime[1] == 6 && L.now == 6 && L.st.host_w == 6);
    CHECK(lfs_check(&L) == 0);
    lfs_free(&L);

    /* 작은 장치 (8 세그먼트 × 4 블록 = 32, lba 16 개 = u 0.5) 에서 2000 번 랜덤 쓰기 — 쓰기마다 검사기 */
    CHECK(lfs_init(&L, &c) == 0);
    L.check_on_erase = true;
    uint32_t rng = 7, ref[16] = { 0 };
    int fails = 0, viol = 0;
    for (int k = 0; k < 2000; k++) {
        uint32_t b = xs32(&rng) % 16;
        if (lfs_write(&L, b) != 0) fails++; else ref[b]++;
        viol += lfs_check(&L) != 0;
    }
    int bad = 0;
    for (uint32_t b = 0; b < 16; b++) bad += lfs_read(&L, b, &v) != 0 || v != stamp(b, ref[b]);
    printf("  small device: %llu cleans, %llu copied, erase-time checks %llu\n",
           (unsigned long long)L.st.segs_cleaned, (unsigned long long)L.st.clean_w, (unsigned long long)L.erase_checks);
    CHECK(fails == 0 && viol == 0 && bad == 0);
    CHECK(L.st.segs_cleaned > 0 && L.erase_checks > 0 && L.erase_check_fail == 0 && L.lost_live == 0);
    CHECK(L.st.clean_w == L.st.victim_live && L.nfree >= c.reserve);
    lfs_free(&L);
}

/* 손으로 만든 세그먼트 상태에서 victim 고르기 (S = 100, now = 1000) */
static uint32_t pick_on(int pol) {
    lfs_t L;
    lfs_cfg_t c = { 6, 100, 100, pol, 2, 3, 1, 1, false, false };
    if (lfs_init(&L, &c) != 0) return NONE - 1;
    static const struct { uint8_t st; uint32_t live; uint64_t mtime, opened; } H[6] = {
        { SEG_FULL, 75, 0, 3 },     /* 0: 오래된 차가운 세그먼트, u 0.75 — CB: 25·1000/175 = 142.9 */
        { SEG_FULL, 50, 900, 4 },   /* 1: 젊고 u 0.50                    — CB: 50·100/150  = 33.3  */
        { SEG_FULL, 100, 0, 1 },    /* 2: 꽉 참 → 후보 아님 (FIFO 도 건너뜀) */
        { SEG_OPEN, 10, 999, 6 },   /* 3: 헤드 → 후보 아님 */
        { SEG_FREE, 0, 0, 0 },
        { SEG_FULL, 95, 10, 2 },    /* 5: 가장 먼저 열린 후보          — CB: 5·990/195   = 25.4  */
    };
    for (int s = 0; s < 6; s++) { L.state[s] = H[s].st; L.live[s] = H[s].live; L.mtime[s] = H[s].mtime; L.opened[s] = H[s].opened; }
    L.now = 1000;
    uint32_t v = pick_victim(&L);
    lfs_free(&L);
    return v;
}

static void part_b(void) {
    printf("-- Part B: victim policies, cleaner, WA\n");
    CHECK(pick_on(POL_GREEDY) == 1);
    CHECK(pick_on(POL_COSTBEN) == 0);
    CHECK(pick_on(POL_FIFO) == 5);
    {   /* 동점: CB 점수가 같으면 live 가 작은 쪽, 후보가 모두 꽉 차 있으면 NONE */
        lfs_t L;
        lfs_cfg_t c = { 3, 10, 10, POL_COSTBEN, 2, 3, 1, 1, false, false };
        CHECK(lfs_init(&L, &c) == 0);
        L.state[0] = L.state[1] = L.state[2] = SEG_FULL;
        L.live[0] = 10; L.live[1] = 10; L.live[2] = 10;
        CHECK(pick_victim(&L) == NONE);
        L.live[0] = 4; L.live[1] = 2; L.now = 0;              /* age 0 → 점수 둘 다 0 */
        CHECK(pick_victim(&L) == 1);
        lfs_free(&L);
    }
    lfs_stats_t s = { .host_w = 1000, .clean_w = 500 };
    CHECK(approx(stats_wa(&s), 1.5, 1e-12));
    lfs_stats_t g = { .host_w = 262144, .segs_cleaned = 100 };   /* 262144 × 4 KiB = 1 GiB */
    CHECK(approx(stats_segs_per_gib(&g), 100.0, 1e-12));

    run_t r, r2;
    /* sequential: 옛 세그먼트가 통째로 죽으므로 복사 0, WA 정확히 1 */
    lfs_cfg_t c = cfg_of(64, 32, 0.80, POL_GREEDY, true, false, 1);
    CHECK(run_sim(&c, WL_SEQ, 1, 2, 4, &r) == 0 && r.st.clean_w == 0 && r.st.segs_cleaned > 0 && r.wa == 1.0);
    c.policy = POL_COSTBEN;
    CHECK(run_sim(&c, WL_SEQ, 1, 2, 4, &r) == 0 && r.st.clean_w == 0 && r.wa == 1.0);

    /* 지울 때마다 검사기: hot-cold, CB, 분리 + 나이 정렬 + batch 3 */
    {
        lfs_t L;
        lfs_cfg_t cc = cfg_of(32, 16, 0.75, POL_COSTBEN, true, true, 3);
        CHECK(lfs_init(&L, &cc) == 0);
        L.check_on_erase = true;
        uint32_t rng = 11, *ref = calloc(cc.nlba, sizeof *ref);
        int fails = 0, bad = 0;
        for (uint32_t b = 0; b < cc.nlba; b++) { fails += lfs_write(&L, b) != 0; ref[b]++; }
        for (uint64_t k = 0; k < 20u * cc.nlba; k++) {
            uint32_t b = next_lba(WL_HOTCOLD, cc.nlba, &rng, k);
            if (lfs_write(&L, b) != 0) fails++; else ref[b]++;
        }
        uint64_t v;
        for (uint32_t b = 0; b < cc.nlba; b++) bad += lfs_read(&L, b, &v) != 0 || v != stamp(b, ref[b]);
        printf("  check-on-erase run: %llu erases checked, %llu passes (batch 3)\n",
               (unsigned long long)L.erase_checks, (unsigned long long)L.st.passes);
        CHECK(fails == 0 && bad == 0 && L.erase_checks > 100 && L.erase_check_fail == 0 && L.lost_live == 0);
        CHECK(L.st.segs_cleaned > L.st.passes);              /* batch 가 실제로 여러 victim 을 골랐다 */
        free(ref);
        lfs_free(&L);
    }

    /* 균등 랜덤: 해석 모델과 비교 (256 세그먼트 × 64 블록, u = 0.80) */
    c = cfg_of(256, 64, 0.80, POL_FIFO, true, false, 1);
    CHECK(run_sim(&c, WL_UNIFORM, 3, 4, 8, &r) == 0 && r.integrity_bad == 0);
    double mf = model_fifo(eff_alpha(&c, r.mean_free));
    printf("  uniform u=0.80 FIFO  : WA %.4f  model %.4f (alpha_eff %.4f, mean free %.2f)\n", r.wa, mf,
           eff_alpha(&c, r.mean_free), r.mean_free);
    CHECK(approx(r.wa, mf, 0.03));
    c.policy = POL_GREEDY;
    CHECK(run_sim(&c, WL_UNIFORM, 3, 4, 8, &r2) == 0 && r2.integrity_bad == 0);
    double mg = model_greedy(eff_alpha(&c, r2.mean_free), c.S);
    printf("  uniform u=0.80 greedy: WA %.4f  model %.4f\n", r2.wa, mg);
    CHECK(approx(r2.wa, mg, 0.03) && r2.wa < r.wa);
    CHECK(approx(r2.wa, 1.0 / (1.0 - r2.mean_victim_u), 0.01));   /* WA ≈ 1/(1-ū) : victim 사용률 평균 */

    /* hot-cold 90/10, u = 0.80, 분리 + 나이 정렬: cost-benefit 이 greedy 보다 적게 복사해야 한다 */
    run_t gh, cb, gu;
    c = cfg_of(256, 64, 0.80, POL_GREEDY, true, true, 4);
    CHECK(run_sim(&c, WL_HOTCOLD, 5, 4, 8, &gh) == 0 && gh.integrity_bad == 0);
    c.policy = POL_COSTBEN;
    CHECK(run_sim(&c, WL_HOTCOLD, 5, 4, 8, &cb) == 0 && cb.integrity_bad == 0);
    c.policy = POL_GREEDY;
    CHECK(run_sim(&c, WL_UNIFORM, 5, 4, 8, &gu) == 0);
    printf("  hot-cold u=0.80: greedy WA %.3f (%.0f seg/GiB)  cost-benefit WA %.3f (%.0f seg/GiB); greedy uniform %.3f\n",
           gh.wa, gh.segs_per_gib, cb.wa, cb.segs_per_gib, gu.wa);
    CHECK(cb.wa < gh.wa);
    CHECK(gh.wa > gu.wa);          /* LFS 논문의 반직관: greedy 는 지역성이 있으면 오히려 나빠진다 */
}

static void part_c(void) {
    printf("-- Part C: rebuild the map from segment summaries (roll-forward / OOB scan)\n");
    lfs_t L;
    lfs_cfg_t c = cfg_of(64, 32, 0.80, POL_COSTBEN, true, true, 2);
    CHECK(lfs_init(&L, &c) == 0);
    uint32_t rng = 9;
    int fails = 0;
    for (uint32_t b = 0; b < c.nlba; b++) fails += lfs_write(&L, b) != 0;
    for (uint64_t k = 0; k < 10u * c.nlba; k++) fails += lfs_write(&L, next_lba(WL_HOTCOLD, c.nlba, &rng, k)) != 0;
    uint32_t *m = malloc(c.nlba * sizeof *m), *lv = malloc(c.nseg * sizeof *lv);
    int n = lfs_rebuild_map(&L, m, lv);
    int diff = 0, ldiff = 0, stale = 0, naive_wrong = 0;
    for (uint32_t b = 0; b < c.nlba; b++) diff += m[b] != L.map[b];
    for (uint32_t s = 0; s < c.nseg; s++) ldiff += lv[s] != L.live[s];
    /* 대조: 버전 없이 "물리 순서상 나중 슬롯이 이긴다" 로 다시 만들면? (세그먼트 재사용 때문에 물리 순서 ≠ 시간 순서) */
    for (uint32_t s = 0; s < c.nseg; s++) {
        if (L.state[s] == SEG_FREE) continue;
        for (uint32_t o = 0; o < L.wp[s]; o++) {
            uint32_t p = s * c.S + o;
            if (L.sum_lba[p] == NONE) continue;
            if (L.map[L.sum_lba[p]] != p) stale++;
        }
    }
    {
        uint32_t *nm = malloc(c.nlba * sizeof *nm);
        for (uint32_t b = 0; b < c.nlba; b++) nm[b] = NONE;
        for (uint32_t p = 0; p < c.nseg * c.S; p++)
            if (L.state[p / c.S] != SEG_FREE && p % c.S < L.wp[p / c.S] && L.sum_lba[p] != NONE) nm[L.sum_lba[p]] = p;
        for (uint32_t b = 0; b < c.nlba; b++) naive_wrong += nm[b] != L.map[b];
        free(nm);
    }
    printf("  rebuilt %d lbas, stale summary slots %d, naive physical-order rebuild wrong for %d lbas\n",
           n, stale, naive_wrong);
    CHECK(fails == 0 && n == (int)c.nlba && diff == 0 && ldiff == 0);
    CHECK(stale > 0 && naive_wrong > 0);
    /* 마모: 지운 횟수 합 == 누적 erase 수, 최소·최대 출력 */
    uint64_t sum = 0;
    uint32_t mn = UINT32_MAX, mx = 0;
    for (uint32_t s = 0; s < c.nseg; s++) { sum += L.erases[s]; if (L.erases[s] < mn) mn = L.erases[s]; if (L.erases[s] > mx) mx = L.erases[s]; }
    printf("  wear: erases min %u max %u mean %.1f\n", mn, mx, (double)sum / c.nseg);
    CHECK(sum == L.erase_total && sum > 0 && mx >= mn);
    free(m); free(lv);
    lfs_free(&L);
}

int main(void) {
    part_a();
    part_b();
    part_c();
    printf("%d/%d passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
#endif /* LFS_NO_MAIN */
