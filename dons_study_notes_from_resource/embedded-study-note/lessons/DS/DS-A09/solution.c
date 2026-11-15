/* DS-A09 FTL 매핑 테이블과 GC 대상 선정 구조
 *
 * 페이지 매핑 FTL의 "자료구조"만 떼어 낸 모델이다. NAND는 배열로 흉내 낸다 (순차 프로그램, 블록 단위 erase 규칙은 지킨다).
 *   Part A  L2P 배열, 블록별 valid 비트맵 + count, P2L(= OOB 등가), free 풀, write/read/trim
 *   Part B  indexed heap 으로 GC victim 선정: greedy / cost-benefit (시간에 따라 변하는 키 → 얼린 시계 + 재빌드), 페이지 재배치
 *   Part C  GC 트리거 워터마크, host 쓰기당 GC 페이싱, GC 복사 중 host 덮어쓰기(조건부 커밋), 예약 블록(교착 방지), WAF
 *
 * 시간 단위: 시계 = "host 가 쓴 페이지 수". 벽시계 시간은 쓰지 않아 결과가 결정적이다.
 * cost-benefit (Rosenblum & Ousterhout 1992 의 LFS 식):
 *     score = (1 - u) * age / (1 + u),  u = valid / PG_PER_BLK
 *     정수 구현: score = (age + 1) * (PG - v) * CB_SCALE / (PG + v)   (age = 얼린 시계 - stamp, 음수면 0)
 *
 * 메모리: 전부 정적 배열 (malloc 없음). 블록/페이지 크기는 매크로로 바꿀 수 있다 (demo_waf.c 참고).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef PG_PER_BLK
#define PG_PER_BLK 128u           /* 블록당 페이지 수 (이 모델에서 페이지 = 매핑 단위) */
#endif
#ifndef MAX_BLK
#define MAX_BLK 256u
#endif
#define MAX_PG    (MAX_BLK * PG_PER_BLK)
#define BM_WORDS  ((PG_PER_BLK + 63u) / 64u)
#define NONE      UINT32_MAX
#define GC_RESERVE 2u             /* host 가 건드리지 못하는 free 블록 수 */
#define CB_SCALE  1024u           /* 정수 나눗셈으로 점수가 0 이 되지 않게: CB_SCALE >= 2*PG_PER_BLK */
_Static_assert(PG_PER_BLK >= 2u && CB_SCALE >= 2u * PG_PER_BLK, "score must stay > 0 when v < PG_PER_BLK");

/* 스텁 상태(starter)와 demo 의 FTL_NO_MAIN 빌드에서 -Wunused-function 을 막는다 */
#define MAYBE_UNUSED __attribute__((unused))

typedef enum { B_FREE, B_HOST, B_GC, B_CLOSED, B_VICTIM } bstate_t;
typedef enum { POL_GREEDY, POL_CB, POL_FIFO } policy_t;
/* age 의 기준 시각(stamp)을 언제 찍을 것인가. 레슨 기본값은 AGE_CLOSE (이유는 demo_waf 표). */
typedef enum { AGE_CLOSE, AGE_CARRY, AGE_LAST_INV } age_mode_t;

typedef struct {
    uint32_t nblk, nlpn;
    policy_t policy;
    age_mode_t age_mode;
    uint32_t streams;              /* 2 = host/GC 프런티어 분리, 1 = 하나를 같이 씀 */
    /* 매핑 */
    uint32_t l2p[MAX_PG];          /* lpn → ppa (NONE = 매핑 없음) */
    uint32_t p2l[MAX_PG];          /* ppa → lpn: 페이지와 함께 프로그램되는 OOB 등가. erase 전까지 불변 */
    uint32_t data[MAX_PG];         /* 페이지 내용 (시뮬레이션) */
    /* 블록별 */
    uint64_t bitmap[MAX_BLK][BM_WORDS];
    uint16_t vcnt[MAX_BLK];        /* valid 페이지 수 == popcount(bitmap) */
    uint16_t wp[MAX_BLK];          /* 다음에 프로그램할 페이지 번호 */
    uint8_t  state[MAX_BLK];
    uint32_t erase_cnt[MAX_BLK];
    uint64_t stamp[MAX_BLK];       /* age 의 기준 시각 */
    uint64_t close_seq[MAX_BLK];   /* 닫힌 순서 (FIFO 정책, greedy 동점 처리) */
    uint64_t next_close;
    /* free 풀: FIFO 링 */
    uint32_t free_q[MAX_BLK], free_head, free_n;
    /* indexed min-heap (DS-M11): 닫힌 블록만 들어 있다 */
    uint32_t heap[MAX_BLK], pos[MAX_BLK], heap_n;
    uint64_t key[MAX_BLK];
    uint64_t epoch_clk;            /* 얼린 시계: 모든 cost-benefit 키는 이 시각으로 계산한다 */
    uint32_t epoch_len, picks_since_rebuild;
    /* 프런티어와 진행 중 GC */
    uint32_t host_blk, gc_blk, victim, cursor, gc_quota;
    uint64_t gc_src_stamp;
    bool gc_active;
    uint32_t wm_start, wm_stop;    /* 워터마크 (free 블록 수) */
    /* 계측 */
    uint64_t host_clk, gc_wr, erases, dead_copies, nand_violations;
    uint64_t heap_swaps, key_evals, rebuilds, invalidates;
    uint32_t gc_max_per_host, refresh_swaps_max;
} ftl_t;

static ftl_t F;

MAYBE_UNUSED static inline uint32_t ppa_make(uint32_t b, uint32_t p) { return b * PG_PER_BLK + p; }
MAYBE_UNUSED static inline uint32_t ppa_blk(uint32_t ppa) { return ppa / PG_PER_BLK; }
MAYBE_UNUSED static inline uint32_t ppa_pg(uint32_t ppa)  { return ppa % PG_PER_BLK; }

/* GC 복사의 "NAND 프로그램이 끝나기 전" 창. 테스트가 여기서 host 쓰기를 끼워 넣는다 (DS-A04 의 POP_WINDOW 와 같은 기법). */
static void (*g_copy_window)(uint32_t lpn, uint32_t old_ppa);

/* ---------- NAND 층 (주어짐): 순차 프로그램, erase 전 재프로그램 금지 ---------- */
MAYBE_UNUSED static uint32_t nand_program(uint32_t b, uint32_t lpn, uint32_t val) {
    uint32_t p = F.wp[b];
    if (p >= PG_PER_BLK || (F.state[b] != B_HOST && F.state[b] != B_GC)) { F.nand_violations++; return NONE; }
    uint32_t ppa = ppa_make(b, p);
    F.p2l[ppa] = lpn;
    F.data[ppa] = val;
    F.wp[b] = (uint16_t)(p + 1u);
    return ppa;
}

MAYBE_UNUSED static void nand_erase(uint32_t b) {
    if (F.vcnt[b] != 0) F.nand_violations++;            /* live 데이터를 지우면 위반 */
    memset(&F.p2l[ppa_make(b, 0)], 0xFF, PG_PER_BLK * sizeof F.p2l[0]);
    memset(&F.data[ppa_make(b, 0)], 0xFF, PG_PER_BLK * sizeof F.data[0]);
    F.wp[b] = 0;
    F.erase_cnt[b]++;
    F.erases++;
}

/* ---------- indexed heap 기계 부분 (주어짐, DS-M11 복습) ---------- */
static uint64_t gc_key(uint32_t b);

static inline bool heap_lt(uint32_t a, uint32_t b) {
    return F.key[a] < F.key[b] || (F.key[a] == F.key[b] && a < b);
}
static void heap_swap(uint32_t i, uint32_t j) {
    uint32_t a = F.heap[i], b = F.heap[j];
    F.heap[i] = b; F.heap[j] = a;
    F.pos[b] = i;  F.pos[a] = j;
    F.heap_swaps++;
}
static void sift_up(uint32_t i) {
    while (i > 0) {
        uint32_t p = (i - 1u) / 2u;
        if (!heap_lt(F.heap[i], F.heap[p])) break;
        heap_swap(i, p);
        i = p;
    }
}
static void sift_down(uint32_t i) {
    for (;;) {
        uint32_t l = 2u * i + 1u, r = l + 1u, m = i;
        if (l < F.heap_n && heap_lt(F.heap[l], F.heap[m])) m = l;
        if (r < F.heap_n && heap_lt(F.heap[r], F.heap[m])) m = r;
        if (m == i) return;
        heap_swap(i, m);
        i = m;
    }
}
MAYBE_UNUSED static void heap_insert(uint32_t b) {
    F.key[b] = gc_key(b);
    F.key_evals++;
    F.heap[F.heap_n] = b;
    F.pos[b] = F.heap_n;
    F.heap_n++;
    sift_up(F.heap_n - 1u);
}
MAYBE_UNUSED static void heap_remove(uint32_t b) {
    uint32_t i = F.pos[b], last = F.heap_n - 1u;
    F.pos[b] = NONE;
    F.heap_n = last;
    if (i == last) return;
    uint32_t m = F.heap[last];
    F.heap[i] = m;
    F.pos[m] = i;
    sift_up(i);
    sift_down(F.pos[m]);
}
MAYBE_UNUSED static void heap_refresh(uint32_t b) {
    uint64_t before = F.heap_swaps;
    F.key[b] = gc_key(b);
    F.key_evals++;
    sift_up(F.pos[b]);
    sift_down(F.pos[b]);
    if (F.heap_swaps - before > F.refresh_swaps_max) F.refresh_swaps_max = (uint32_t)(F.heap_swaps - before);
}

/* ======================= Part A — 매핑·비트맵·free 풀·host 경로 ======================= */

MAYBE_UNUSED static inline bool valid_test(uint32_t ppa) {
    uint32_t b = ppa_blk(ppa), p = ppa_pg(ppa);
    return (F.bitmap[b][p / 64u] >> (p % 64u)) & 1u;
}

/* ppa 의 valid 비트를 켜고 그 블록의 vcnt 를 +1. 이미 켜져 있으면 nand_violations++ (조용한 이중 계산 금지). */
MAYBE_UNUSED static void valid_set(uint32_t ppa) {
    /* TODO */
    uint32_t b = ppa_blk(ppa), p = ppa_pg(ppa);
    if (valid_test(ppa)) { F.nand_violations++; return; }
    F.bitmap[b][p / 64u] |= 1ull << (p % 64u);
    F.vcnt[b]++;
}

/* ppa 의 valid 비트를 끄고 vcnt 를 -1. 꺼져 있었으면 위반.
 * 그 블록이 B_CLOSED 면 힙에 있으므로 키가 바뀐다: AGE_LAST_INV 면 stamp = host_clk 로 갱신 후 heap_refresh(b).
 * F.invalidates++ 도 여기서. */
MAYBE_UNUSED static void valid_clear(uint32_t ppa) {
    /* TODO */
    uint32_t b = ppa_blk(ppa), p = ppa_pg(ppa);
    if (!valid_test(ppa)) { F.nand_violations++; return; }
    F.bitmap[b][p / 64u] &= ~(1ull << (p % 64u));
    F.vcnt[b]--;
    F.invalidates++;
    if (F.state[b] == B_CLOSED) {
        if (F.age_mode == AGE_LAST_INV) F.stamp[b] = F.host_clk;
        heap_refresh(b);
    }
}

/* 블록 b 에서 from 이상의 첫 valid 페이지 번호. 없으면 PG_PER_BLK. 64비트 워드 경계를 넘을 수 있다. __builtin_ctzll 사용 (인자 0 금지). */
MAYBE_UNUSED static uint32_t bm_next_set(uint32_t b, uint32_t from) {
    /* TODO */
    while (from < PG_PER_BLK) {
        uint32_t w = from / 64u;
        uint64_t bits = F.bitmap[b][w] >> (from % 64u);
        if (bits != 0) return from + (uint32_t)__builtin_ctzll(bits);
        from = (w + 1u) * 64u;
    }
    return PG_PER_BLK;
}

/* free 풀: FIFO 링 (먼저 지워진 블록을 먼저 재사용 → erase 횟수가 고르게 돈다). 용량 = nblk */
MAYBE_UNUSED static void free_push(uint32_t b) {
    /* TODO: F.free_q 의 (free_head + free_n) % nblk 자리에 넣고 free_n++ */
    F.free_q[(F.free_head + F.free_n) % F.nblk] = b;
    F.free_n++;
}

MAYBE_UNUSED static uint32_t free_pop(void) {
    /* TODO: 비었으면 NONE. 아니면 free_head 자리의 블록을 꺼내고 head 전진, free_n-- */
    if (F.free_n == 0) return NONE;
    uint32_t b = F.free_q[F.free_head];
    F.free_head = (F.free_head + 1u) % F.nblk;
    F.free_n--;
    return b;
}

/* 프런티어 블록을 준비한다. 있으면 그대로 true. 없으면 free 풀에서 하나 연다.
 * host 는 free_n > GC_RESERVE 일 때만, GC 는 free_n > 0 이면 가져갈 수 있다 (→ Part C 교착 방지).
 * 스트림: is_gc && streams == 2 → gc_blk (상태 B_GC), 그 외 → host_blk (상태 B_HOST). */
MAYBE_UNUSED static uint32_t *frontier_slot(bool is_gc) { return (is_gc && F.streams == 2u) ? &F.gc_blk : &F.host_blk; }

MAYBE_UNUSED static bool frontier_ready(bool is_gc) {
    /* TODO */
    uint32_t *slot = frontier_slot(is_gc);
    if (*slot != NONE) return true;
    if (!is_gc && F.free_n <= GC_RESERVE) return false;
    uint32_t b = free_pop();
    if (b == NONE) return false;
    F.state[b] = (uint8_t)((is_gc && F.streams == 2u) ? B_GC : B_HOST);
    *slot = b;
    return true;
}

/* 꽉 찬 프런티어를 닫는다: 상태 B_CLOSED, close_seq = ++next_close, stamp 기록, 힙에 넣고, 슬롯을 NONE 으로.
 *   stamp: AGE_CARRY 이고 GC 블록이면 gc_src_stamp(복사해 온 데이터 중 가장 젊은 stamp)을 쓰고 gc_src_stamp = 0,
 *          그 외에는 host_clk. */
MAYBE_UNUSED static void close_frontier(uint32_t *slot, bool is_gc) {
    /* TODO */
    uint32_t b = *slot;
    F.state[b] = B_CLOSED;
    F.close_seq[b] = ++F.next_close;
    if (F.age_mode == AGE_CARRY && is_gc && F.streams == 2u) { F.stamp[b] = F.gc_src_stamp; F.gc_src_stamp = 0; }
    else F.stamp[b] = F.host_clk;
    *slot = NONE;
    heap_insert(b);
}

/* host 쓰기 코어 (GC 없음): 프런티어 준비 → nand_program → L2P 갱신 + valid_set → 옛 ppa valid_clear → host_clk++ →
 * 꽉 차면 close. 범위 밖이거나 프런티어를 못 열면 false (상태 불변). */
MAYBE_UNUSED static bool host_program(uint32_t lpn, uint32_t val) {
    /* TODO */
    if (lpn >= F.nlpn) return false;
    if (!frontier_ready(false)) return false;
    uint32_t old = F.l2p[lpn], b = F.host_blk;
    uint32_t ppa = nand_program(b, lpn, val);
    if (ppa == NONE) return false;
    F.l2p[lpn] = ppa;
    valid_set(ppa);
    if (old != NONE) valid_clear(old);
    F.host_clk++;
    if (F.wp[b] == PG_PER_BLK) close_frontier(&F.host_blk, false);
    return true;
}

MAYBE_UNUSED static bool ftl_read(uint32_t lpn, uint32_t *out) {
    /* TODO: 범위 밖 / 매핑 없음이면 false. 아니면 *out = data[l2p[lpn]] */
    if (lpn >= F.nlpn || F.l2p[lpn] == NONE) return false;
    *out = F.data[F.l2p[lpn]];
    return true;
}

MAYBE_UNUSED static bool ftl_trim(uint32_t lpn) {
    /* TODO: 매핑이 있을 때만 L2P 를 NONE 으로, 옛 ppa valid_clear. 없으면 false */
    if (lpn >= F.nlpn || F.l2p[lpn] == NONE) return false;
    uint32_t old = F.l2p[lpn];
    F.l2p[lpn] = NONE;
    valid_clear(old);
    return true;
}

/* ======================= Part B — GC victim 선정과 재배치 ======================= */

/* 점수 = (age + 1) * (PG - v) * CB_SCALE / (PG + v)  — 64비트 부호 없는 정수, 나눗셈은 맨 마지막.
 * age + 1: 방금 닫힌 블록(age 0)이라도 비울 게 있으면 점수 > 0 → v == PG 인 블록보다 항상 앞선다. */
MAYBE_UNUSED static uint64_t cb_score(uint32_t v, uint64_t age) {
    /* TODO */
    return (age + 1u) * (uint64_t)(PG_PER_BLK - v) * CB_SCALE / (PG_PER_BLK + v);
}

/* 힙은 min-heap 이므로 "먼저 골라야 할수록 작은 키":
 *   POL_GREEDY: (vcnt << 48) | close_seq    — valid 적은 순, 같으면 먼저 닫힌 순
 *   POL_FIFO  : close_seq
 *   POL_CB    : v == 0 이면 0 (복사 없는 공짜 블록은 무조건 먼저), 아니면 UINT64_MAX - cb_score(v, age)
 *               age = epoch_clk - stamp[b] (epoch_clk < stamp 이면 0). 여기서 host_clk 를 쓰면 안 된다! */
static uint64_t gc_key(uint32_t b) {
    /* TODO */
    uint32_t v = F.vcnt[b];
    if (F.policy == POL_GREEDY) return ((uint64_t)v << 48) | F.close_seq[b];
    if (F.policy == POL_FIFO) return F.close_seq[b];
    if (v == 0) return 0;
    uint64_t age = F.epoch_clk > F.stamp[b] ? F.epoch_clk - F.stamp[b] : 0;
    return UINT64_MAX - cb_score(v, age);
}

/* 시계를 다시 얼린다: epoch_clk = host_clk, 힙 안 모든 블록의 키를 다시 계산, Floyd bottom-up heapify (O(n)),
 * picks_since_rebuild = 0, rebuilds++ */
MAYBE_UNUSED static void heap_rebuild(void) {
    /* TODO */
    F.epoch_clk = F.host_clk;
    for (uint32_t i = 0; i < F.heap_n; i++) { F.key[F.heap[i]] = gc_key(F.heap[i]); F.key_evals++; }
    for (uint32_t i = F.heap_n / 2u; i-- > 0; ) sift_down(i);
    F.picks_since_rebuild = 0;
    F.rebuilds++;
}

/* 다음 victim 을 고른다 (cursor = 0, gc_quota 계산, picks_since_rebuild++, 상태 B_VICTIM, 힙에서 제거).
 *   POL_CB 는 picks_since_rebuild >= epoch_len 이면 먼저 heap_rebuild().
 *   힙이 비었으면 false.  루트가 v == PG 이면 (FIFO 제외) 비울 게 없으니 false.
 *   gc_quota = ceil(v / (PG - v)), v == PG 이면 PG (host 쓰기 한 번당 복사할 페이지 수 — Part C). */
MAYBE_UNUSED static bool gc_pick(void) {
    /* TODO */
    if (F.policy == POL_CB && F.picks_since_rebuild >= F.epoch_len) heap_rebuild();
    if (F.heap_n == 0) return false;
    uint32_t b = F.heap[0], v = F.vcnt[b];
    if (v == PG_PER_BLK && F.policy != POL_FIFO) return false;
    heap_remove(b);
    F.state[b] = B_VICTIM;
    F.victim = b;
    F.cursor = 0;
    F.picks_since_rebuild++;
    F.gc_quota = (v == PG_PER_BLK) ? PG_PER_BLK : (v + (PG_PER_BLK - v) - 1u) / (PG_PER_BLK - v);
    return true;
}

/* victim b 의 페이지 p 를 GC 프런티어로 옮긴다.
 *   1) old = ppa_make(b,p); lpn = p2l[old]; val = data[old]
 *   2) frontier_ready(true) 실패면 false  (reserve 가 지켜지면 일어나지 않는다)
 *   3) nand_program → gc_wr++
 *   4) g_copy_window 가 있으면 호출 (lpn, old)  ← 프로그램과 L2P 커밋 사이의 창
 *   5) L2P 가 여전히 old 를 가리킬 때만 커밋: l2p[lpn] = new, valid_set(new), valid_clear(old).
 *      아니면 그 사이 host 가 덮어쓴 것 → dead_copies++ (새 페이지는 처음부터 invalid)
 *   6) AGE_CARRY 면 gc_src_stamp = max(gc_src_stamp, stamp[b])
 *   7) 프런티어가 꽉 찼으면 close_frontier */
MAYBE_UNUSED static bool gc_relocate(uint32_t b, uint32_t p) {
    /* TODO */
    uint32_t old = ppa_make(b, p), lpn = F.p2l[old], val = F.data[old];
    if (!frontier_ready(true)) return false;
    uint32_t *slot = frontier_slot(true);
    uint32_t nb = *slot;
    uint32_t nppa = nand_program(nb, lpn, val);
    if (nppa == NONE) return false;
    F.gc_wr++;
    if (g_copy_window) g_copy_window(lpn, old);
    if (F.l2p[lpn] == old) {
        F.l2p[lpn] = nppa;
        valid_set(nppa);
        valid_clear(old);
    } else {
        F.dead_copies++;
    }
    if (F.age_mode == AGE_CARRY && F.stamp[b] > F.gc_src_stamp) F.gc_src_stamp = F.stamp[b];
    if (F.wp[nb] == PG_PER_BLK) close_frontier(slot, true);
    return true;
}

/* GC 한 걸음: victim 이 없으면 고르고, 다음 valid 페이지를 하나 옮긴다. 더 없으면 erase 해서 free 풀로
 * (상태 B_FREE, victim = NONE). 진행이 불가능하면 false. */
MAYBE_UNUSED static bool gc_step(void) {
    /* TODO */
    if (F.victim == NONE && !gc_pick()) return false;
    uint32_t b = F.victim;
    uint32_t p = bm_next_set(b, F.cursor);
    if (p == PG_PER_BLK) {
        nand_erase(b);
        F.state[b] = B_FREE;
        free_push(b);
        F.victim = NONE;
        return true;
    }
    if (!gc_relocate(b, p)) return false;
    F.cursor = p + 1u;
    return true;
}

/* 전경(foreground) 쓰기 (주어짐): 프런티어를 못 열 때만 GC 를 돌린다. 한 번 시작한 victim 은 host 쓰기 전에 끝낸다. */
MAYBE_UNUSED static bool ftl_write_fg(uint32_t lpn, uint32_t val) {
    uint64_t before = F.gc_wr;
    while (F.victim != NONE || (F.host_blk == NONE && F.free_n <= GC_RESERVE)) {
        if (!gc_step()) return false;
    }
    uint32_t n = (uint32_t)(F.gc_wr - before);
    if (n > F.gc_max_per_host) F.gc_max_per_host = n;
    return host_program(lpn, val);
}

/* ======================= Part C — 워터마크, 페이싱, 하드 플로어 ======================= */

/* host 쓰기 한 번마다 호출한다.
 *   gc_active == false: free_n <= wm_start 이면 true 로.  여전히 false 면 아무것도 안 한다.
 *   gc_active == true : victim 이 없고 free_n >= wm_stop 이면 gc_active = false 로 끝.
 *      아니면 복사한 페이지 수(gc_wr 증가분)가 할당량에 닿을 때까지 gc_step() 반복.
 *      할당량: victim 이 있으면 gc_quota, 없으면 1.  gc_step 실패면 gc_active = false.
 *   v == 0 victim 은 복사가 없으니 할당량을 쓰지 않고 지나간다. */
MAYBE_UNUSED static void gc_tick(void) {
    /* TODO */
    if (!F.gc_active && F.free_n <= F.wm_start) F.gc_active = true;
    if (!F.gc_active) return;
    uint32_t done = 0;
    for (;;) {
        if (F.victim == NONE && F.free_n >= F.wm_stop) { F.gc_active = false; return; }
        if (done >= (F.victim == NONE ? 1u : F.gc_quota)) return;
        uint64_t before = F.gc_wr;
        if (!gc_step()) { F.gc_active = false; return; }
        done += (uint32_t)(F.gc_wr - before);
    }
}

/* 페이싱 쓰기: gc_tick() → (하드 플로어) 프런티어를 못 열면 열 수 있을 때까지 gc_step() → host_program.
 * 이 호출에서 복사한 페이지 수의 최댓값을 gc_max_per_host 에 기록. streams == 1 이면 ftl_write_fg 로 넘긴다. */
MAYBE_UNUSED static bool ftl_write(uint32_t lpn, uint32_t val) {
    /* TODO */
    if (F.streams == 1u) return ftl_write_fg(lpn, val);
    uint64_t before = F.gc_wr;
    gc_tick();
    while (F.host_blk == NONE && F.free_n <= GC_RESERVE) {
        if (!gc_step()) return false;
    }
    uint32_t n = (uint32_t)(F.gc_wr - before);
    if (n > F.gc_max_per_host) F.gc_max_per_host = n;
    return host_program(lpn, val);
}

/* ---------- 초기화 (주어짐) ----------
 * 교착 방지 조건 (5장 증명): 닫힌 블록 전체 용량 > 사용자 페이지 수 → 어떤 닫힌 블록에는 반드시 invalid 페이지가 있다. */
MAYBE_UNUSED static bool ftl_init(uint32_t nblk, uint32_t nlpn, policy_t policy) {
    if (nblk > MAX_BLK || nblk < GC_RESERVE + 2u) return false;
    if ((uint64_t)nlpn >= (uint64_t)(nblk - GC_RESERVE - 1u) * PG_PER_BLK) return false;
    memset(&F, 0, sizeof F);
    F.nblk = nblk; F.nlpn = nlpn; F.policy = policy; F.age_mode = AGE_CLOSE;
    F.streams = 2u;
    F.epoch_len = 32u;
    F.picks_since_rebuild = UINT32_MAX / 2u;   /* 첫 pick 은 무조건 재빌드 (epoch_clk 가 아직 의미 없다) */
    F.wm_start = GC_RESERVE + 2u;
    F.wm_stop = GC_RESERVE + 4u;
    F.host_blk = F.gc_blk = F.victim = NONE;
    memset(F.l2p, 0xFF, sizeof F.l2p);
    memset(F.p2l, 0xFF, sizeof F.p2l);
    memset(F.data, 0xFF, sizeof F.data);
    memset(F.pos, 0xFF, sizeof F.pos);
    for (uint32_t b = 0; b < nblk; b++) {
        F.free_q[b] = b;
        F.state[b] = B_FREE;
    }
    F.free_n = nblk;
    return true;
}

/* ---------- 전체 불변식 검사기 (주어짐) ----------
 * L2P ↔ P2L ↔ 비트맵 ↔ count ↔ 힙 ↔ free 풀을 서로 대조한다. 실패 이유는 g_why 에. */
static const char *g_why;
#define BAD(msg) do { g_why = (msg); return false; } while (0)

MAYBE_UNUSED static bool ftl_check(void) {
    uint64_t mapped = 0, valid_sum = 0;
    for (uint32_t lpn = 0; lpn < F.nlpn; lpn++) {
        uint32_t ppa = F.l2p[lpn];
        if (ppa == NONE) continue;
        mapped++;
        if (ppa >= F.nblk * PG_PER_BLK) BAD("L2P out of range");
        uint32_t b = ppa_blk(ppa);
        if (F.state[b] == B_FREE) BAD("L2P points into a free block");
        if (ppa_pg(ppa) >= F.wp[b]) BAD("L2P points past the write pointer");
        if (F.p2l[ppa] != lpn) BAD("P2L disagrees with L2P");
        if (!valid_test(ppa)) BAD("mapped page is not marked valid");
    }
    static uint8_t seen[MAX_BLK];
    memset(seen, 0, sizeof seen);
    uint32_t free_states = 0, closed = 0;
    for (uint32_t b = 0; b < F.nblk; b++) {
        uint32_t pop = 0;
        for (uint32_t w = 0; w < BM_WORDS; w++) pop += (uint32_t)__builtin_popcountll(F.bitmap[b][w]);
        if (pop != F.vcnt[b]) BAD("vcnt != popcount(bitmap)");
        valid_sum += pop;
        for (uint32_t p = 0; p < PG_PER_BLK; p++) {
            uint32_t ppa = ppa_make(b, p);
            if (valid_test(ppa)) {
                if (p >= F.wp[b]) BAD("valid bit past the write pointer");
                uint32_t lpn = F.p2l[ppa];
                if (lpn >= F.nlpn || F.l2p[lpn] != ppa) BAD("valid bit but L2P[P2L[ppa]] != ppa");
            }
        }
        if (F.state[b] == B_FREE) {
            free_states++;
            if (F.wp[b] != 0 || F.vcnt[b] != 0) BAD("free block is not empty");
        }
        if (F.state[b] == B_CLOSED) {
            closed++;
            if (F.wp[b] != PG_PER_BLK) BAD("closed block is not full");
            if (F.pos[b] == NONE || F.pos[b] >= F.heap_n || F.heap[F.pos[b]] != b) BAD("closed block not in heap");
            if (F.key[b] != gc_key(b)) BAD("heap key is stale");
        } else if (F.pos[b] != NONE) {
            BAD("non-closed block in heap");
        }
    }
    if (valid_sum != mapped) BAD("sum of vcnt != number of mapped LPNs");
    if (free_states != F.free_n) BAD("free count mismatch");
    if (closed != F.heap_n) BAD("heap size != closed blocks");
    for (uint32_t i = 0; i < F.free_n; i++) {
        uint32_t b = F.free_q[(F.free_head + i) % F.nblk];
        if (b >= F.nblk || F.state[b] != B_FREE || seen[b]) BAD("free ring corrupt");
        seen[b] = 1;
    }
    for (uint32_t i = 0; i < F.heap_n; i++) {
        if (F.pos[F.heap[i]] != i) BAD("heap/pos not inverse");
        if (i > 0 && heap_lt(F.heap[i], F.heap[(i - 1u) / 2u])) BAD("heap order broken");
    }
    if (F.host_blk != NONE && (F.state[F.host_blk] != B_HOST || F.wp[F.host_blk] >= PG_PER_BLK)) BAD("host frontier state");
    if (F.gc_blk != NONE && (F.state[F.gc_blk] != B_GC || F.wp[F.gc_blk] >= PG_PER_BLK)) BAD("gc frontier state");
    if (F.victim != NONE && F.state[F.victim] != B_VICTIM) BAD("victim state");
    if (F.nand_violations != 0) BAD("NAND rule violated");
    return true;
}

#ifndef FTL_NO_MAIN
/* ======================= 하네스 (여기부터는 starter 와 같다) ======================= */
static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s  [%s]\n", __FILE__, __LINE__, #e, g_why ? g_why : ""); } while (0)

/* 참조 모델: lpn 별 마지막으로 쓴 값 (0 = 매핑 없음). 쓴 값은 모두 다르다. */
static uint32_t g_ref[MAX_PG], g_ver;
static uint64_t rng_state;
static void rng_seed(uint64_t s) { rng_state = s; }
static uint32_t rng32(void) {        /* splitmix64 */
    uint64_t z = (rng_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return (uint32_t)((z ^ (z >> 31)) >> 32);
}
static uint32_t rng_lpn(uint32_t hot_pct, uint32_t space_pct) {
    uint32_t hot = (uint32_t)((uint64_t)F.nlpn * space_pct / 100u);
    if (hot == 0 || hot >= F.nlpn) return rng32() % F.nlpn;
    if (rng32() % 100u < hot_pct) return rng32() % hot;
    return hot + rng32() % (F.nlpn - hot);
}

static void ref_reset(void) { memset(g_ref, 0, sizeof g_ref); g_ver = 0; }
static bool data_ok(void) {
    for (uint32_t lpn = 0; lpn < F.nlpn; lpn++) {
        uint32_t v = 0;
        bool hit = ftl_read(lpn, &v);
        if (g_ref[lpn] == 0) { if (hit) BAD("read hit on an unmapped LPN"); }
        else if (!hit || v != g_ref[lpn]) BAD("data mismatch against the reference");
    }
    return true;
}
static bool do_write(uint32_t lpn, bool paced) {
    uint32_t v = ++g_ver;
    bool ok = paced ? ftl_write(lpn, v) : ftl_write_fg(lpn, v);
    if (ok) g_ref[lpn] = v;
    return ok;
}
static uint32_t brute_best(void) {       /* 힙 없이 선형 탐색으로 고른 최솟값 블록 */
    uint32_t best = NONE;
    for (uint32_t b = 0; b < F.nblk; b++)
        if (F.state[b] == B_CLOSED && (best == NONE || gc_key(b) < gc_key(best) || (gc_key(b) == gc_key(best) && b < best))) best = b;
    return best;
}
static bool fill_sequential(uint32_t n) {  /* lpn 0..n-1 을 차례로: host_program 만 (GC 없음) */
    for (uint32_t i = 0; i < n; i++) {
        uint32_t v = ++g_ver;
        if (!host_program(i, v)) return false;
        g_ref[i] = v;
    }
    return true;
}
static bool overwrite_range(uint32_t first, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        uint32_t v = ++g_ver;
        if (!host_program(first + i, v)) return false;
        g_ref[first + i] = v;
    }
    return true;
}
static void report_waf(const char *name, uint64_t h, uint64_t g) {
    printf("[C] %-34s host %8" PRIu64 " gc %9" PRIu64 "  WAF %.3f\n", name, h, g, (double)(h + g) / (double)h);
}

static void test_part_a(void) {
    puts("== Part A: mapping, bitmap, free pool");
    CHECK(!ftl_init(MAX_BLK + 1u, 10u, POL_GREEDY));
    CHECK(!ftl_init(16u, (16u - GC_RESERVE - 1u) * PG_PER_BLK, POL_GREEDY));   /* 여유 0 → 거절 */
    CHECK(ftl_init(16u, 1000u, POL_GREEDY));
    ref_reset(); g_why = NULL;
    uint32_t v = 0;
    CHECK(!ftl_read(7u, &v));
    CHECK(do_write(7u, false) && ftl_read(7u, &v) && v == g_ref[7]);
    uint32_t p0 = F.l2p[7];
    CHECK(p0 == ppa_make(0, 0) && F.vcnt[0] == 1u && valid_test(p0) && F.p2l[p0] == 7u);
    if (p0 >= F.nblk * PG_PER_BLK) { puts("SKIP rest of Part A: host write stub"); return; }
    CHECK(do_write(7u, false) && ftl_read(7u, &v) && v == g_ref[7]);
    CHECK(!valid_test(p0) && F.vcnt[0] == 1u && F.wp[0] == 2u && F.l2p[7] == ppa_make(0, 1));
    CHECK(F.p2l[p0] == 7u);                                  /* P2L 은 stale 이어도 erase 전까지 그대로 */
    CHECK(ftl_check());
    CHECK(ftl_trim(7u) && !ftl_read(7u, &v) && F.vcnt[0] == 0u);
    CHECK(!ftl_trim(7u) && !ftl_trim(1000u) && !do_write(1000u, false));
    CHECK(ftl_check());

    /* 비트맵 워드 경계 (페이지 63/64) */
    CHECK(ftl_init(16u, 1000u, POL_GREEDY));
    ref_reset();
    CHECK(fill_sequential(PG_PER_BLK));
    CHECK(F.vcnt[0] == PG_PER_BLK && F.state[0] == B_CLOSED && F.host_blk == NONE);
    CHECK(do_write(63u, false) && do_write(64u, false));
    CHECK(!valid_test(ppa_make(0, 63)) && !valid_test(ppa_make(0, 64)) && valid_test(ppa_make(0, 62)) && valid_test(ppa_make(0, 65)));
    CHECK(F.vcnt[0] == PG_PER_BLK - 2u);
    CHECK(bm_next_set(0, 62) == 62u && bm_next_set(0, 63) == 65u && bm_next_set(0, 64) == 65u && bm_next_set(0, PG_PER_BLK - 1u) == PG_PER_BLK - 1u);
    CHECK(bm_next_set(7, 0) == PG_PER_BLK);                  /* 빈 블록 */
    CHECK(ftl_check() && data_ok());

    /* free 풀 FIFO + 예약: host 는 GC_RESERVE 아래로 못 내려간다 */
    CHECK(ftl_init(8u, 300u, POL_GREEDY));
    ref_reset();
    uint32_t first[3] = { free_pop(), free_pop(), free_pop() };
    CHECK(first[0] == 0u && first[1] == 1u && first[2] == 2u);
    free_push(first[1]); free_push(first[0]); free_push(first[2]);
    CHECK(F.free_n == 8u && free_pop() == 3u);
    CHECK(ftl_init(8u, 300u, POL_GREEDY));
    ref_reset();
    uint32_t n = 0;
    while (n < 8u * PG_PER_BLK * 2u && host_program(n % 300u, ++g_ver)) { g_ref[n % 300u] = g_ver; n++; }
    CHECK(n == (8u - GC_RESERVE) * PG_PER_BLK);              /* 가져갈 수 있는 블록을 정확히 다 쓰고 멈춘다 */
    CHECK(F.free_n == GC_RESERVE && F.host_blk == NONE);
    CHECK(!host_program(0u, ++g_ver));                       /* 거절돼도 상태 불변 */
    CHECK(ftl_check() && data_ok());
    CHECK(F.host_clk == n && F.nand_violations == 0u);
}

/* Part B 스크립트: 3개 블록을 만들고 원하는 valid 수로 깎는다. b0: v=10, b1: v=PG, b2: v=50 */
static bool build_three(policy_t pol) {
    if (!ftl_init(8u, 3u * PG_PER_BLK, pol)) return false;
    ref_reset();
    if (!fill_sequential(3u * PG_PER_BLK)) return false;           /* b0,b1,b2 가득 */
    if (!overwrite_range(10u, PG_PER_BLK - 10u)) return false;     /* b0 → v=10, 새 b3 에 PG-10 */
    if (!overwrite_range(2u * PG_PER_BLK + 50u, PG_PER_BLK - 50u)) return false;  /* b2 → v=50 */
    return true;
}

static void test_part_b_unit(void) {
    puts("== Part B: keys, heap, relocation");
    CHECK(cb_score(0, 0) == CB_SCALE);
    CHECK(cb_score(PG_PER_BLK / 2u, 99u) == (100ull * (PG_PER_BLK / 2u) * CB_SCALE) / (PG_PER_BLK + PG_PER_BLK / 2u));
    CHECK(cb_score(PG_PER_BLK, 1000000u) == 0u);
    CHECK(cb_score(PG_PER_BLK - 1u, 0u) >= 1u);              /* 비울 게 있으면 0 이 아니다 */

    /* 키가 시간에 따라 뒤집히는 예: young/empty(A) vs old/full(B) */
    {
        uint32_t va = 32u, vb = 100u;
        uint64_t sa = 1000u, sb = 0u;
        bool t1_b_wins = cb_score(va, 1100u - sa) < cb_score(vb, 1100u - sb);
        bool t2_a_wins = cb_score(va, 2000u - sa) > cb_score(vb, 2000u - sb);
        CHECK(t1_b_wins && t2_a_wins);
    }

    CHECK(build_three(POL_GREEDY));
    CHECK(F.vcnt[0] == 10u && F.vcnt[1] == PG_PER_BLK && F.vcnt[2] == 50u);
    CHECK(ftl_check() && F.heap_n == 4u);
    CHECK(F.heap[0] == brute_best());
    CHECK(gc_pick() && F.victim == 0u && F.state[0] == B_VICTIM && F.heap_n == 3u);   /* 가장 적은 valid */
    CHECK(F.gc_quota == 1u);                                 /* ceil(10/118) */
    uint32_t steps = 0;
    while (F.victim != NONE && steps < PG_PER_BLK * 2u) { if (!gc_step()) break; steps++; }
    CHECK(F.victim == NONE && F.state[0] == B_FREE && F.gc_wr == 10u && F.erases == 1u && F.erase_cnt[0] == 1u);
    CHECK(steps == 11u);                                     /* 복사 10 + erase 1 */
    CHECK(ftl_check() && data_ok());
    CHECK(F.gc_blk != NONE && F.vcnt[F.gc_blk] == 10u);

    /* 전부 v == PG: 비울 게 없으니 greedy/CB 는 고르지 않는다 (FIFO 만 그래도 고른다) */
    for (int p = 0; p < 3; p++) {
        policy_t pol = p == 0 ? POL_GREEDY : p == 1 ? POL_CB : POL_FIFO;
        CHECK(ftl_init(8u, 3u * PG_PER_BLK, pol));
        ref_reset();
        CHECK(fill_sequential(2u * PG_PER_BLK));
        CHECK(F.heap_n == 2u && F.vcnt[0] == PG_PER_BLK && F.vcnt[1] == PG_PER_BLK);
        CHECK(gc_pick() == (pol == POL_FIFO));
        CHECK(F.victim == (pol == POL_FIFO ? 0u : NONE));
    }

    /* v == 0 블록: greedy 는 복사 없이 바로 */
    CHECK(ftl_init(8u, 3u * PG_PER_BLK, POL_GREEDY));
    ref_reset();
    CHECK(fill_sequential(PG_PER_BLK * 2u));
    CHECK(overwrite_range(0u, PG_PER_BLK));
    CHECK(gc_pick() && F.victim == 0u && F.vcnt[0] == 0u);
    CHECK(gc_step() && F.victim == NONE && F.gc_wr == 0u && F.state[0] == B_FREE);
    CHECK(ftl_check() && data_ok());

    /* cost-benefit: 키는 얼린 시계로만 바뀐다 */
    CHECK(build_three(POL_CB));
    CHECK(F.epoch_clk == 0u);
    F.epoch_len = 1000000u;                                 /* 이 스크립트에서는 자동 재빌드 금지 */
    heap_rebuild();
    CHECK(F.epoch_clk == F.host_clk && F.rebuilds == 1u);
    uint64_t k2 = F.key[2];
    CHECK(do_write(3u * PG_PER_BLK - 1u, false));            /* 시계 진행, b2 는 안 건드림 */
    CHECK(F.key[2] == k2 && F.key[2] == gc_key(2));          /* 재빌드 전에는 키가 안 떠내려간다 */
    CHECK(F.heap[0] == brute_best() && ftl_check());
    heap_rebuild();
    CHECK(F.key[2] < k2);                                    /* 시계를 새로 얼리면 age 가 늘어 점수가 오른다 (키는 내려간다) */
    CHECK(F.key[1] == UINT64_MAX);                           /* v == PG 는 점수 0 → 맨 뒤 */
    CHECK(F.key[0] != UINT64_MAX && F.key[0] < F.key[2]);    /* b0(v=10) 이 b2(v=50) 보다 앞선다 */
    CHECK(F.heap[0] == 0u && ftl_check());
    CHECK(gc_pick() && F.victim == 0u);                      /* 가장 높은 점수 */

    /* epoch_len = 1 이면 pick 할 때마다 재빌드 = 정확한 cost-benefit */
    CHECK(build_three(POL_CB));
    F.epoch_len = 1u;
    CHECK(gc_pick() && F.rebuilds == 1u && F.epoch_clk == F.host_clk);
    CHECK(F.victim == 0u);
}

/* 작은 지오메트리: 매 연산 후 전체 검사 */
static void random_small(policy_t pol, const char *name, uint64_t seed) {
    CHECK(ftl_init(24u, 20u * PG_PER_BLK / 2u, pol));
    ref_reset();
    F.epoch_len = 8u;
    rng_seed(seed);
    uint32_t ops = 8000u, bad = 0;
    bool root_ok = true, keep = true;
    for (uint32_t i = 0; i < ops && keep; i++) {
        uint32_t lpn = rng32() % F.nlpn, r = rng32() % 100u;
        if (r < 8u) { if (ftl_trim(lpn)) g_ref[lpn] = 0; }
        else if (r < 96u) { if (!do_write(lpn, false)) { bad++; keep = false; } }
        else if (!gc_step() && F.heap_n > 0 && F.vcnt[F.heap[0]] != PG_PER_BLK) { bad++; keep = false; }
        if (!ftl_check()) { bad++; keep = false; }
        if (F.heap_n > 0 && F.heap[0] != brute_best()) root_ok = false;
    }
    CHECK(bad == 0u && root_ok && data_ok());
    CHECK(F.host_clk > 0u && F.erases > 0u);
    printf("[B] %-8s small: %u ops, host %" PRIu64 " gc %" PRIu64 " erase %" PRIu64 "\n", name, ops, F.host_clk, F.gc_wr, F.erases);
}

/* 큰 지오메트리: K 연산마다 전체 검사 + 데이터 대조 (SAN 빌드는 횟수를 줄인다) */
static void random_large(policy_t pol, const char *name, uint64_t seed, uint32_t hot, uint32_t space) {
    uint32_t nblk = MAX_BLK;
    uint32_t nlpn = (uint32_t)((nblk - GC_RESERVE - 1u) * PG_PER_BLK * 82ull / 100u);
    CHECK(ftl_init(nblk, nlpn, pol));
    ref_reset();
    rng_seed(seed);
    uint64_t ops = 1100000u;
    uint64_t K = 8192u, bad = 0;
    for (uint64_t i = 0; i < ops && !bad; i++) {
        uint32_t lpn = rng_lpn(hot, space);
        if (i % 40u == 0u) { if (ftl_trim(lpn)) g_ref[lpn] = 0; continue; }
        if (!do_write(lpn, true)) bad++;
        if ((i & (K - 1u)) == 0u && (!ftl_check() || !data_ok())) bad++;
    }
    CHECK(bad == 0u && ftl_check() && data_ok());
    printf("[B] %-8s large %u/%u %" PRIu64 " ops: WAF %.3f, erases %" PRIu64 ", dead %" PRIu64 ", heap swaps/invalidate %.2f\n",
           name, hot, space, ops, (double)(F.host_clk + F.gc_wr) / (double)F.host_clk, F.erases, F.dead_copies,
           (double)F.heap_swaps / (double)F.invalidates);
}

/* ---- Part C: 강제 스케줄 훅 ---- */
static uint32_t g_hook_lpn, g_hook_fired;
static void hook_overwrite_same(uint32_t lpn, uint32_t old_ppa) {
    (void)old_ppa;
    if (g_hook_lpn == lpn && g_hook_fired == 0u) {
        g_hook_fired = 1u;
        uint32_t v = ++g_ver;
        if (host_program(lpn, v)) g_ref[lpn] = v;
    }
}
static void hook_noop(uint32_t lpn, uint32_t old_ppa) { (void)lpn; (void)old_ppa; g_hook_fired++; }

static void test_part_c(void) {
    puts("== Part C: pacing, watermarks, races, WAF");
    /* 1) 진행 중 victim 의 남은 페이지를 host 가 덮어쓰면 복사하지 않는다 */
    CHECK(build_three(POL_GREEDY));
    CHECK(gc_pick() && F.victim == 0u && F.gc_wr == 0u);
    CHECK(gc_step() && F.gc_wr == 1u && F.cursor == 1u);     /* 페이지 0 복사 */
    CHECK(host_program(5u, ++g_ver));                        /* victim 의 5번 페이지가 무효화 (전경 경로가 아니라 host 코어 직접) */
    g_ref[5] = g_ver;
    CHECK(F.state[0] == B_VICTIM && F.vcnt[0] == 8u);        /* v 10 → 9(복사) → 8(host) */
    uint32_t guard = 0;
    while (F.victim != NONE && guard++ < 64u) if (!gc_step()) break;
    CHECK(F.victim == NONE && F.gc_wr == 9u && F.state[0] == B_FREE);
    CHECK(ftl_check() && data_ok());

    /* 2) 복사 창: 프로그램과 L2P 커밋 사이에 host 가 같은 lpn 을 덮어쓴다 */
    CHECK(build_three(POL_GREEDY));
    CHECK(gc_pick() && F.victim == 0u);
    g_hook_lpn = 3u; g_hook_fired = 0u; g_copy_window = hook_overwrite_same;
    guard = 0;
    while (F.victim != NONE && guard++ < 64u) if (!gc_step()) break;
    g_copy_window = NULL;
    CHECK(g_hook_fired == 1u && F.dead_copies == 1u && F.gc_wr == 10u);
    CHECK(F.victim == NONE && ftl_check());
    uint32_t v = 0;
    CHECK(ftl_read(3u, &v) && v == g_ref[3] && data_ok());   /* 복사본이 host 의 새 데이터를 덮지 않았다 */

    /* 훅이 없을 때도 창은 지나간다 (no-op 훅으로 호출 횟수 확인) */
    CHECK(build_three(POL_GREEDY));
    CHECK(gc_pick());
    g_hook_fired = 0u; g_copy_window = hook_noop;
    guard = 0;
    while (F.victim != NONE && guard++ < 64u) if (!gc_step()) break;
    g_copy_window = NULL;
    CHECK(g_hook_fired == 10u && F.dead_copies == 0u && F.gc_wr == 10u);

    /* 3) 워터마크: wm_start 전까지 GC 0, 켜지면 wm_stop 까지 돌고 꺼진다 */
    CHECK(ftl_init(40u, 30u * PG_PER_BLK * 3u / 4u, POL_GREEDY));
    ref_reset();
    rng_seed(77u);
    uint32_t activations = 0, early_on = 0, early_off = 0, off_with_victim = 0, min_free = F.free_n;
    bool prev = false, ok = true;
    while (F.host_clk < 200000u && ok) {
        uint32_t free_before = F.free_n;
        ok = do_write(rng32() % F.nlpn, true);
        if (!prev && F.gc_active) { activations++; if (free_before > F.wm_start) early_on++; }
        /* 꺼질 때는 tick 시점에 free >= wm_stop 이었고, 그 뒤 host 가 블록 하나를 열었을 수 있다 */
        if (prev && !F.gc_active && F.free_n + 1u < F.wm_stop) early_off++;
        if (prev && !F.gc_active && F.victim != NONE) off_with_victim++;   /* 복사 중인 victim 을 버려두고 멈추면 안 된다 */
        prev = F.gc_active;
        if (F.free_n < min_free) min_free = F.free_n;
    }
    CHECK(ok && activations > 0u && early_on == 0u && early_off == 0u && off_with_victim == 0u);
    CHECK(min_free >= 1u);
    CHECK(ftl_check() && data_ok());
    printf("[C] watermarks: %u activations in %" PRIu64 " host writes, min free %u\n", activations, F.host_clk, min_free);

    /* wm_start 위에서는 GC 복사가 전혀 없다 */
    CHECK(ftl_init(40u, 25u * PG_PER_BLK, POL_GREEDY));
    ref_reset();
    uint32_t cold = 25u * PG_PER_BLK;
    for (uint32_t i = 0; i < cold; i++) ok = do_write(i, true) && ok;
    CHECK(ok && F.gc_wr == 0u && F.free_n > F.wm_start);

    /* 4) 페이싱 vs 전경 버스트: host 쓰기 1회당 최대 복사 페이지 수 (플랫폼 무관 작업량) */
    uint32_t nlpn = (MAX_BLK - GC_RESERVE - 1u) * PG_PER_BLK * 70u / 100u;
    uint32_t burst_max = 0;
    for (int mode = 0; mode < 2; mode++) {
        CHECK(ftl_init(MAX_BLK, nlpn, POL_GREEDY));
        ref_reset();
        rng_seed(1234u);
        ok = true;
        uint64_t n = (uint64_t)nlpn * 8u;
        for (uint64_t i = 0; i < n && ok; i++) ok = do_write(rng32() % nlpn, mode == 1);
        CHECK(ok && ftl_check() && data_ok());
        printf("[C] %-9s: max GC copies per host write = %u, WAF %.3f\n", mode ? "paced" : "foreground",
               F.gc_max_per_host, (double)(F.host_clk + F.gc_wr) / (double)F.host_clk);
        if (mode == 0) burst_max = F.gc_max_per_host; else CHECK(F.gc_max_per_host * 8u <= burst_max);
    }

    /* 5) 가장 빠듯한 합법 구성: 여유 1 페이지여도 교착 없이 진행 */
    {
        uint32_t nb = 14u, nl = (nb - GC_RESERVE - 1u) * PG_PER_BLK - 1u;
        CHECK(ftl_init(nb, nl, POL_GREEDY));
        ref_reset();
        rng_seed(5u);
        ok = true;
        for (uint32_t i = 0; i < nl && ok; i++) ok = do_write(i, false);
        uint32_t writes = 12000u;
        for (uint32_t i = 0; i < writes && ok; i++) ok = do_write(rng32() % nl, false);
        CHECK(ok && ftl_check() && data_ok());
        printf("[C] tightest legal config (%u blocks, spare %u page(s) beyond reserve): %" PRIu64 " host, WAF %.2f\n",
               nb, (nb - GC_RESERVE - 1u) * PG_PER_BLK - nl, F.host_clk, (double)(F.host_clk + F.gc_wr) / (double)F.host_clk);
    }

    /* 6) WAF: OP 가 늘면 줄어든다. 90/10 에서 cost-benefit < greedy */
    {
        double waf[3][2];
        static const double ops[3] = { 0.07, 0.28, 0.50 };
        for (int i = 0; i < 3; i++) for (int p = 0; p < 2; p++) {
            uint32_t pgs = (MAX_BLK - GC_RESERVE - 1u) * PG_PER_BLK;
            uint32_t nl = (uint32_t)((double)MAX_BLK * PG_PER_BLK / (1.0 + ops[i]));
            if (nl >= pgs) nl = pgs - 1u;
            CHECK(ftl_init(MAX_BLK, nl, p ? POL_CB : POL_GREEDY));
            F.epoch_len = 16u;
            ref_reset();
            rng_seed(99u);
            ok = true;
            for (uint32_t k = 0; k < nl && ok; k++) ok = do_write(k, false);
            uint64_t dw = 20u, n0 = (uint64_t)nl * dw, n1 = (uint64_t)nl * dw;
            for (uint64_t k = 0; k < n0 && ok; k++) ok = do_write(rng_lpn(90u, 10u), false);
            uint64_t h0 = F.host_clk, g0 = F.gc_wr;
            for (uint64_t k = 0; k < n1 && ok; k++) ok = do_write(rng_lpn(90u, 10u), false);
            CHECK(ok && ftl_check() && data_ok());
            uint64_t dh = F.host_clk - h0, dg = F.gc_wr - g0;
            waf[i][p] = (double)(dh + dg) / (double)dh;
            char name[40];
            snprintf(name, sizeof name, "90/10 op=%.0f%% %s", ops[i] * 100.0, p ? "cost-benefit" : "greedy");
            report_waf(name, dh, dg);
        }
        CHECK(waf[0][0] > waf[1][0] && waf[1][0] > waf[2][0] && waf[2][0] >= 1.0);
        CHECK(waf[0][1] > waf[1][1] && waf[1][1] > waf[2][1]);
        CHECK(waf[0][1] < waf[0][0] && waf[1][1] < waf[1][0] && waf[2][1] < waf[2][0]);
    }
}

int main(void) {
    test_part_a();
    bool a_ok = g_pass == g_total;
    if (!a_ok) puts("SKIP Part B/C: Part A failed (stubs would make the workload meaningless)");
    if (a_ok) {
        test_part_b_unit();
        int before = g_total - g_pass;
        if (before == 0) {
            puts("== Part B: random vs reference");
            random_small(POL_GREEDY, "greedy", 1u);
            random_small(POL_CB, "cb", 2u);
            random_small(POL_FIFO, "fifo", 3u);
            random_large(POL_GREEDY, "greedy", 4u, 100u, 100u);
            random_large(POL_CB, "cb", 5u, 90u, 10u);
            int after = g_total - g_pass;
            if (after == 0) test_part_c();
            else puts("SKIP Part C: Part B failed");
        } else {
            puts("SKIP random/Part C: Part B unit tests failed");
        }
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* FTL_NO_MAIN */
