/* ARCH-A02 TLB 모델과 ASID·플러시 비용
 *
 * 0절  ARCH-A01 모델을 축소해 넣었다: "ARMv8-A VMSA 4KB granule 을 단순화한 교육용 포맷".
 *      4 KB 페이지, 단계당 인덱스 9 비트, 테이블 페이지 = 512 × 8 B 디스크립터, levels ∈ {2,3,4}
 *      → VA 비트 = 12 + 9·levels (30/39/48). 마지막 단계는 항상 L3 (4단계 L0→L3, 3단계 L1→L3, 2단계 L2→L3).
 *      디스크립터(자체 포맷, 실제 ARM 비트 위치 아님): bit0 V · bit1 T · bit2 R · bit3 W · bit4 X · bit5 U ·
 *      bit6 A(여기선 무시) · bit7 G · bits 12..47 출력 주소. 블록은 L1(1 GB)·L2(2 MB)만, L3 는 T=1 이어야 page.
 *      권한은 리프에서만: USER→U=0 은 PERMISSION, KERNEL 의 U=1 실행도 PERMISSION (SMEP/PXN 개념).
 *
 * Part A  완전 연관(fully associative) TLB: N 엔트리, 교체 정책 LRU / FIFO / RANDOM(고정 시드 xorshift).
 *         엔트리 = {valid, asid, 리프 크기 단위의 vpn, 리프 크기, pa base, perms, global}. G 엔트리는 모든 ASID 와 맞는다.
 *         TODO: tlb_lookup / tlb_victim / mmu_access.  검사: TLB 결과 == 새 워크 결과 (랜덤 주소 수만 개).
 * Part B  두 프로세스 교대 실행(K 접근마다 스위치): 전체 플러시 vs 비전역 플러시 vs ASID 태깅 → 미스·워크·워크 읽기 표.
 *         정확성 위험 2개를 직접 재현하고 고친다: (1) unmap 후 TLBI 누락 → 낡은 변환(stale)
 *         (2) ASID 재사용 시 플러시 누락 → 프로세스 B 가 A 의 매핑을 본다.
 *         TODO: tlbi_all / tlbi_nonglobal / tlbi_asid / tlbi_va_asid / context_switch.
 * Part C  작은 폭 ASID 할당기 + 세대(generation) 카운터. 소진되면 세대++ · 비트맵 비움 · TLB 전체 플러시
 *         (Linux arm64 방식을 단일 CPU 로 단순화). 프로세스 수별 1000 스위치당 플러시 수. TODO: asid_switch_to.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so      (starter.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======================= 0. ARCH-A01 모델 (축소판) ======================= */
#define PAGE_SHIFT 12
#define PAGE_SIZE (1ull << PAGE_SHIFT)
#define NFRAMES 1024                      /* 시뮬레이션 물리 메모리 4 MB = 테이블 페이지 전용 프레임 풀 */
#define D_V (1ull << 0)                   /* valid */
#define D_T (1ull << 1)                   /* 1 = 다음 단계 테이블(L0–L2) / page(L3) · 0 = 블록 */
#define D_R (1ull << 2)
#define D_W (1ull << 3)
#define D_X (1ull << 4)
#define D_U (1ull << 5)                   /* user 접근 허용 */
#define D_A (1ull << 6)                   /* access flag — 이 레슨은 무시 */
#define D_G (1ull << 7)                   /* global: TLB 에서 ASID 무관하게 맞는다 */
#define D_PERM (D_R | D_W | D_X | D_U | D_G)
#define D_OA 0x0000FFFFFFFFF000ull        /* bits 12..47 */

typedef enum { F_NONE, F_ADDRESS_SIZE, F_TRANSLATION, F_PERMISSION } fault_t;
static const char *const FAULT_NAME[] = {"NONE", "ADDRESS_SIZE", "TRANSLATION", "PERMISSION"};
enum { ACC_R = 0, ACC_W = 1, ACC_X = 2, ACC_TYPE = 3, ACC_USER = 4 };   /* access = 종류 | (USER ? ACC_USER : 0) */
enum { E_OK = 0, E_ALIGN = -1, E_PERM = -2, E_RANGE = -3, E_EXISTS = -4, E_NOMEM = -5, E_SPLIT = -6 };
enum { MAP_4K = 1 };                      /* map 플래그: 블록 대신 4 KB 페이지 강제 */

typedef struct {
    fault_t fault; int level; uint64_t va; int acc;            /* 폴트 리포트 {type, 검출 단계, va, access} */
    uint64_t pa; int leaf_level; uint64_t size; uint64_t perms; bool global; /* 리프 정보 (NONE·PERMISSION 일 때) */
    bool hit;                                                  /* TLB 에서 나왔나 (비교 대상 아님) */
} xlate_t;

typedef enum { R_LRU, R_FIFO, R_RANDOM } repl_t;
static const char *const REPL_NAME[] = {"LRU", "FIFO", "RANDOM"};
#define TLB_MAX 256
typedef struct {
    bool valid, global;
    uint16_t asid;
    uint8_t shift;          /* 리프 크기 log2: 12 (4 KB) / 21 (2 MB) / 30 (1 GB) */
    uint8_t leaf_level;
    uint64_t vpn;           /* va >> shift — 리프 크기 단위의 VPN */
    uint64_t pa_base, perms;
    uint64_t stamp;         /* LRU: 마지막 사용 시각 / FIFO: 삽입 시각 */
} tlbe_t;
typedef struct { tlbe_t e[TLB_MAX]; int n; repl_t pol; uint64_t clock; uint32_t rng; } tlb_t;

typedef struct {
    uint64_t accesses, hits, misses, walks, walk_reads, faults[4];
    uint64_t tlbi_all, tlbi_nonglobal, tlbi_asid, tlbi_va, inval_entries, switches;
} stats_t;

typedef struct {
    uint8_t *pmem; uint32_t next_frame; int levels;
    uint64_t cur_ttbr; uint16_t cur_asid;   /* "TTBR" 과 현재 ASID (ARM 은 TTBR 안에 ASID 필드가 있다) */
    tlb_t tlb; stats_t st;
} mmu_t;

typedef struct { uint64_t ttbr; uint16_t asid; uint64_t ctx; int pid; } proc_t;   /* ctx = (세대 << 16) | asid (Part C) */

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static uint64_t rd64(const mmu_t *M, uint64_t pa) {
    if (pa + 8 > (uint64_t)NFRAMES * PAGE_SIZE) return 0;         /* 풀 밖 = 0 = invalid 로 읽힘 */
    uint64_t v; memcpy(&v, M->pmem + pa, 8); return v;
}
static void wr64(mmu_t *M, uint64_t pa, uint64_t v) { memcpy(M->pmem + pa, &v, 8); }
static uint64_t frame_alloc(mmu_t *M) {                          /* 0 = 실패 (프레임 0 은 예약: "null") */
    if (M->next_frame >= NFRAMES) return 0;
    return (uint64_t)(M->next_frame++) * PAGE_SIZE;               /* calloc 이라 이미 0 */
}
static unsigned va_bits(const mmu_t *M) { return 12u + 9u * (unsigned)M->levels; }
static unsigned lvl_shift(int L) { return 12u + 9u * (unsigned)(3 - L); }

static fault_t perm_check(uint64_t perms, int acc) {
    int t = acc & ACC_TYPE; bool user = (acc & ACC_USER) != 0;
    uint64_t need = t == ACC_R ? D_R : t == ACC_W ? D_W : D_X;
    if (user && !(perms & D_U)) return F_PERMISSION;
    if (!user && t == ACC_X && (perms & D_U)) return F_PERMISSION;   /* 커널이 user 페이지 실행 = SMEP/PXN 개념 */
    if (!(perms & need)) return F_PERMISSION;
    return F_NONE;
}

/* 하드웨어 워커: 단계마다 디스크립터 1 개(8 B)를 시뮬레이션 메모리에서 읽고 *reads 를 센다 (NULL 이면 안 셈). */
static xlate_t walk(const mmu_t *M, uint64_t ttbr, uint64_t va, int acc, uint64_t *reads) {
    xlate_t r; memset(&r, 0, sizeof r); r.va = va; r.acc = acc;
    int start = 4 - M->levels;
    if (va >> va_bits(M)) { r.fault = F_ADDRESS_SIZE; r.level = start; return r; }   /* 워크 없음 */
    uint64_t table = ttbr;
    for (int L = start; L <= 3; L++) {
        unsigned sh = lvl_shift(L);
        uint64_t d = rd64(M, table + ((va >> sh) & 511u) * 8u);
        if (reads) (*reads)++;
        if (!(d & D_V)) { r.fault = F_TRANSLATION; r.level = L; return r; }
        if (L < 3 && (d & D_T)) { table = d & D_OA; continue; }
        if (L == 0 || (L == 3 && !(d & D_T))) { r.fault = F_TRANSLATION; r.level = L; return r; } /* 잘못된 인코딩 */
        r.leaf_level = L; r.level = L; r.size = 1ull << sh;
        r.perms = d & D_PERM; r.global = (d & D_G) != 0;
        r.pa = (d & D_OA & ~(r.size - 1)) | (va & (r.size - 1));
        r.fault = perm_check(r.perms, acc);
        return r;
    }
    r.fault = F_TRANSLATION; return r;                            /* 도달 불가 */
}

static int map_one(mmu_t *M, uint64_t ttbr, uint64_t va, uint64_t pa, int leaf, uint64_t perms) {
    uint64_t table = ttbr;
    for (int L = 4 - M->levels; L <= 3; L++) {
        uint64_t slot = table + ((va >> lvl_shift(L)) & 511u) * 8u;
        uint64_t d = rd64(M, slot);
        if (L == leaf) {
            if (d & D_V) return E_EXISTS;                         /* 페이지·블록·테이블이 이미 있음 */
            wr64(M, slot, (pa & D_OA) | perms | D_A | D_V | (L == 3 ? D_T : 0));
            return E_OK;
        }
        if (!(d & D_V)) {
            uint64_t t = frame_alloc(M);
            if (!t) return E_NOMEM;
            d = t | D_T | D_V; wr64(M, slot, d);
        } else if (!(d & D_T)) return E_EXISTS;                   /* 블록 아래로 내려갈 수 없다 */
        table = d & D_OA;
    }
    return E_EXISTS;
}

/* map: 정렬된 가장 큰 블록(1 GB → 2 MB → 4 KB)을 고른다. W 단독은 포맷상 가능하지만 API 가 거부. 실패 시 롤백 없음. */
static int mmu_map(mmu_t *M, uint64_t ttbr, uint64_t va, uint64_t pa, uint64_t size, uint64_t perms, int flags) {
    if (size == 0 || ((va | pa | size) & (PAGE_SIZE - 1))) return E_ALIGN;
    if ((perms & ~D_PERM) || ((perms & D_W) && !(perms & D_R))) return E_PERM;
    if (va + size < va || ((va + size - 1) >> va_bits(M)) || ((pa + size - 1) >> 48)) return E_RANGE;
    while (size) {
        int L = 3; uint64_t s = PAGE_SIZE;
        for (int c = 1; c <= 2 && !(flags & MAP_4K); c++) {
            uint64_t bs = 1ull << lvl_shift(c);
            if (c >= 4 - M->levels && !((va | pa) & (bs - 1)) && size >= bs) { L = c; s = bs; break; }
        }
        int e = map_one(M, ttbr, va, pa, L, perms);
        if (e) return e;
        va += s; pa += s; size -= s;
    }
    return E_OK;
}

/* unmap: 리프를 지운다. 블록 일부만 지우려 하면 E_SPLIT (분할은 ARCH-A01 Part B). 테이블 페이지는 회수하지 않는다.
 * 주의: 페이지 테이블만 바꾼다 — TLB 무효화는 호출자의 책임 (Part B 의 핵심). */
static int mmu_unmap(mmu_t *M, uint64_t ttbr, uint64_t va, uint64_t size) {
    if (size == 0 || ((va | size) & (PAGE_SIZE - 1))) return E_ALIGN;
    while (size) {
        uint64_t table = ttbr, step = PAGE_SIZE;
        for (int L = 4 - M->levels; L <= 3; L++) {
            uint64_t slot = table + ((va >> lvl_shift(L)) & 511u) * 8u, d = rd64(M, slot);
            if (!(d & D_V)) break;                                /* 미매핑: 4 KB 씩 건너뜀 */
            if (L < 3 && (d & D_T)) { table = d & D_OA; continue; }
            uint64_t bs = 1ull << lvl_shift(L);
            if ((va & (bs - 1)) || size < bs) return E_SPLIT;
            wr64(M, slot, 0); step = bs; break;
        }
        va += step; size = size > step ? size - step : 0;
    }
    return E_OK;
}

static int mmu_init(mmu_t *M, int levels, int tlb_n, repl_t pol) {
    memset(M, 0, sizeof *M);
    M->pmem = calloc(NFRAMES, PAGE_SIZE);
    if (!M->pmem) return -1;
    M->next_frame = 1; M->levels = levels;
    M->tlb.n = tlb_n; M->tlb.pol = pol; M->tlb.rng = 0x2545F491u;
    return 0;
}
static void mmu_free(mmu_t *M) { free(M->pmem); M->pmem = NULL; }
static proc_t proc_new(mmu_t *M, int pid, uint16_t asid) {
    proc_t p = { frame_alloc(M), asid, 0, pid }; return p;
}

/* TLB 엔트리 → 변환 결과 (히트 경로). 권한은 캐시된 perms 로 다시 검사한다. */
static xlate_t entry_to_xlate(const tlbe_t *e, uint64_t va, int acc) {
    xlate_t r; memset(&r, 0, sizeof r); r.va = va; r.acc = acc; r.hit = true;
    r.size = 1ull << e->shift; r.leaf_level = e->leaf_level; r.level = e->leaf_level;
    r.perms = e->perms; r.global = e->global;
    r.pa = e->pa_base | (va & (r.size - 1));
    r.fault = perm_check(e->perms, acc);
    return r;
}

/* ======================= Part A: TLB ======================= */

/* 히트면 엔트리 인덱스, 미스면 -1. 맞는 조건: valid && (global || asid 같음) && (va >> shift) == vpn.
 * LRU 면 히트 때 stamp 를 갱신한다 (FIFO 는 삽입 시각 그대로). */
static int tlb_lookup(tlb_t *T, uint64_t va, uint16_t asid) {
    /* TODO(Part A-1): n 개 엔트리를 모두 비교 (완전 연관 = 하드웨어에선 CAM 의 병렬 비교) */
    for (int i = 0; i < T->n; i++) {
        tlbe_t *e = &T->e[i];
        if (e->valid && (e->global || e->asid == asid) && (va >> e->shift) == e->vpn) {
            if (T->pol == R_LRU) e->stamp = ++T->clock;
            return i;
        }
    }
    return -1;
}

/* 새 엔트리를 넣을 자리: 빈 엔트리가 있으면 그것(가장 앞), 없으면 정책대로.
 * LRU·FIFO = stamp 최소, RANDOM = xs32(&T->rng) % n. */
static int tlb_victim(tlb_t *T) {
    /* TODO(Part A-2) */
    int v = 0;
    for (int i = 0; i < T->n; i++) if (!T->e[i].valid) return i;
    if (T->pol == R_RANDOM) return (int)(xs32(&T->rng) % (uint32_t)T->n);
    for (int i = 1; i < T->n; i++) if (T->e[i].stamp < T->e[v].stamp) v = i;
    return v;
}

static void tlb_insert(tlb_t *T, const xlate_t *r, uint16_t asid) {
    int i = tlb_victim(T);
    tlbe_t *e = &T->e[i];
    e->valid = true; e->global = r->global; e->asid = asid;
    e->shift = (uint8_t)lvl_shift(r->leaf_level); e->leaf_level = (uint8_t)r->leaf_level;
    e->vpn = r->va >> e->shift; e->pa_base = r->pa & ~(r->size - 1); e->perms = r->perms;
    e->stamp = ++T->clock;
}

/* MMU 한 번의 접근: VA 범위 검사(ADDRESS_SIZE 면 TLB·워크 없음) → TLB 조회 → 히트면 entry_to_xlate,
 * 미스면 walk(M->cur_ttbr, ..., &M->st.walk_reads) 후 리프를 찾았으면(NONE 또는 PERMISSION) tlb_insert.
 * 통계: accesses, hits / misses, walks, faults[결과]. TLB 히트 결과는 새 워크 결과와 (hit 제외) 같아야 한다. */
static xlate_t mmu_access(mmu_t *M, uint64_t va, int acc) {
    /* TODO(Part A-3) */
    xlate_t r;
    M->st.accesses++;
    if (va >> va_bits(M)) {
        r = walk(M, M->cur_ttbr, va, acc, NULL);
    } else {
        int i = tlb_lookup(&M->tlb, va, M->cur_asid);
        if (i >= 0) {
            M->st.hits++;
            r = entry_to_xlate(&M->tlb.e[i], va, acc);
        } else {
            M->st.misses++; M->st.walks++;
            r = walk(M, M->cur_ttbr, va, acc, &M->st.walk_reads);
            if (r.fault == F_NONE || r.fault == F_PERMISSION) tlb_insert(&M->tlb, &r, M->cur_asid);
        }
    }
    M->st.faults[r.fault]++;
    return r;
}

/* ======================= Part B: 무효화(TLBI)와 컨텍스트 스위치 ======================= */
/* 각 함수는 지운 엔트리 수를 st.inval_entries 에 더하고, 자기 카운터(tlbi_*)를 1 올린다. */

/* 전부 (TLBI VMALLE1 개념 — 전역 포함) */
static void tlbi_all(mmu_t *M) {
    /* TODO(Part B-1) */
    for (int i = 0; i < M->tlb.n; i++)
        if (M->tlb.e[i].valid) { M->tlb.e[i].valid = false; M->st.inval_entries++; }
    M->st.tlbi_all++;
}

/* 비전역(G=0)만 — PCID 없는 x86 의 CR3 쓰기와 같은 효과: 커널 global 페이지는 살아남는다 */
static void tlbi_nonglobal(mmu_t *M) {
    /* TODO(Part B-2) */
    for (int i = 0; i < M->tlb.n; i++)
        if (M->tlb.e[i].valid && !M->tlb.e[i].global) { M->tlb.e[i].valid = false; M->st.inval_entries++; }
    M->st.tlbi_nonglobal++;
}

/* 그 ASID 의 비전역 엔트리만 (TLBI ASIDE1 개념) — global 은 남는다 */
static void tlbi_asid(mmu_t *M, uint16_t asid) {
    /* TODO(Part B-3) */
    for (int i = 0; i < M->tlb.n; i++) {
        tlbe_t *e = &M->tlb.e[i];
        if (e->valid && !e->global && e->asid == asid) { e->valid = false; M->st.inval_entries++; }
    }
    M->st.tlbi_asid++;
}

/* va 를 덮는 엔트리 중 (그 ASID 의 것 또는 global) (TLBI VAE1 개념 — global 은 ASID 와 무관하게 지워진다).
 * "덮는다" = (va >> e.shift) == e.vpn: 2 MB 블록 엔트리도 그 안의 아무 VA 로 지울 수 있어야 한다. */
static void tlbi_va_asid(mmu_t *M, uint64_t va, uint16_t asid) {
    /* TODO(Part B-4) */
    for (int i = 0; i < M->tlb.n; i++) {
        tlbe_t *e = &M->tlb.e[i];
        if (e->valid && (e->global || e->asid == asid) && (va >> e->shift) == e->vpn) {
            e->valid = false; M->st.inval_entries++;
        }
    }
    M->st.tlbi_va++;
}

typedef enum { SW_FLUSH_ALL, SW_FLUSH_NONGLOBAL, SW_ASID } swpol_t;
static const char *const SW_NAME[] = {"FLUSH_ALL", "FLUSH_NONGLOBAL", "ASID"};

/* p 로 전환: TTBR = p->ttbr. FLUSH_* 정책은 ASID 를 안 쓴다(cur_asid = 0) 대신 해당 플러시,
 * ASID 정책은 cur_asid = p->asid 만 바꾸고 플러시하지 않는다. st.switches++. */
static void context_switch(mmu_t *M, const proc_t *p, swpol_t pol) {
    /* TODO(Part B-5) */
    M->cur_ttbr = p->ttbr;
    if (pol == SW_ASID) M->cur_asid = p->asid;
    else { M->cur_asid = 0; if (pol == SW_FLUSH_ALL) tlbi_all(M); else tlbi_nonglobal(M); }
    M->st.switches++;
}

/* ======================= Part C: ASID 할당기 (세대 카운터) ======================= */
#define CTX_GEN_SHIFT 16
typedef struct { int bits; uint64_t gen; uint8_t used[1u << 16]; uint32_t hint; uint64_t rollovers, allocs; } asid_alloc_t;
static void asid_alloc_init(asid_alloc_t *A, int bits) { memset(A, 0, sizeof *A); A->bits = bits; A->gen = 1; A->hint = 1; }
static bool g_mut_rollover_no_flush;      /* 돌연변이: 롤오버 때 플러시를 빼먹는다 (하네스가 켠다) */
static void rollover_flush(mmu_t *M) { if (!g_mut_rollover_no_flush) tlbi_all(M); }

/* p 로 전환하면서 ASID 를 보장한다. ASID 0 은 예약(할당 안 함) → 쓸 수 있는 수 = 2^bits − 1.
 *  - p->ctx 의 세대 == A->gen  → 그 ASID 를 그대로 (빠른 경로: 할당·플러시 없음)
 *  - 아니면 hint 부터 [1, 2^bits−1] 을 돌며 빈 ASID 를 찾는다. 없으면 롤오버:
 *    gen++, used[] 비움, rollovers++, rollover_flush(M) — 그 뒤 다시 찾는다.
 *    찾은 a: used[a] = 1, hint = a 다음, allocs++, p->ctx = (gen << CTX_GEN_SHIFT) | a.
 *  - 끝으로 p->asid = ctx 의 하위 16 비트, M->cur_ttbr / cur_asid 설정, st.switches++ (플러시는 롤오버 때만). */
static void asid_switch_to(asid_alloc_t *A, mmu_t *M, proc_t *p) {
    /* TODO(Part C-1) */
    uint32_t cap = (1u << A->bits) - 1u;
    if (p->ctx == 0 || (p->ctx >> CTX_GEN_SHIFT) != A->gen) {
        uint32_t a = 0;
        for (int pass = 0; pass < 2 && !a; pass++) {
            for (uint32_t k = 0; k < cap; k++) {
                uint32_t c = 1u + (A->hint - 1u + k) % cap;
                if (!A->used[c]) { a = c; break; }
            }
            if (!a) {                                      /* 소진 → 새 세대 */
                A->gen++; memset(A->used, 0, (size_t)1 << A->bits); A->rollovers++; A->hint = 1;
                rollover_flush(M);
            }
        }
        A->used[a] = 1; A->hint = a % cap + 1u; A->allocs++;
        p->ctx = (A->gen << CTX_GEN_SHIFT) | a;
    }
    p->asid = (uint16_t)(p->ctx & 0xFFFFu);
    M->cur_ttbr = p->ttbr; M->cur_asid = p->asid; M->st.switches++;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static bool same_xlate(const xlate_t *a, const xlate_t *b) {
    if (a->fault != b->fault || a->level != b->level || a->va != b->va || a->acc != b->acc) return false;
    if (a->fault != F_NONE && a->fault != F_PERMISSION) return true;
    return a->pa == b->pa && a->leaf_level == b->leaf_level && a->size == b->size &&
           a->perms == b->perms && a->global == b->global;
}
/* 접근 + 검사: TLB 경로 결과 vs 새 워크(통계 안 셈). 다르면 *bad++ */
static xlate_t acc_chk(mmu_t *M, uint64_t va, int acc, long *bad) {
    xlate_t r = mmu_access(M, va, acc), w = walk(M, M->cur_ttbr, va, acc, NULL);
    if (!same_xlate(&r, &w)) (*bad)++;
    return r;
}
static int tlb_valid_count(const tlb_t *T) { int c = 0; for (int i = 0; i < T->n; i++) c += T->e[i].valid; return c; }

/* TLB 감사: 유효 엔트리마다 주인 프로세스의 테이블을 새로 워크해 같은 리프인지 본다. 다르면 stale.
 * 주인 = global 이면 현재 TTBR, 아니면 asid 가 같은 프로세스 (gen != 0 이면 그 세대의 ctx 를 가진 프로세스만).
 * FLUSH 정책(cur_asid 0)에서는 asid 0 = 현재 프로세스. */
static int tlb_audit(const mmu_t *M, const proc_t *ps, int np, uint64_t gen) {
    int stale = 0;
    for (int i = 0; i < M->tlb.n; i++) {
        const tlbe_t *e = &M->tlb.e[i];
        if (!e->valid) continue;
        uint64_t ttbr = 0; bool found = false;
        if (e->global || e->asid == M->cur_asid) { ttbr = M->cur_ttbr; found = true; }
        for (int k = 0; k < np && !found; k++)
            if (ps[k].asid == e->asid && (gen == 0 || (ps[k].ctx >> CTX_GEN_SHIFT) == gen)) { ttbr = ps[k].ttbr; found = true; }
        if (!found) continue;                              /* 주인 없는 엔트리: 재사용 전까지는 무해 */
        xlate_t w = walk(M, ttbr, e->vpn << e->shift, ACC_R, NULL);
        bool ok = (w.fault == F_NONE || w.fault == F_PERMISSION) && w.size == (1ull << e->shift) &&
                  (w.pa & ~(w.size - 1)) == e->pa_base && w.perms == e->perms && w.global == e->global;
        stale += !ok;
    }
    return stale;
}

/* ---------------- Part A ---------------- */
#define UVA_TEXT 0x00400000ull
#define UVA_DATA 0x00600000ull
#define UVA_RO   0x00800000ull
#define KVA_BLK2M 0x40000000ull
#define KVA_BLK1G 0x4000000000ull         /* 256 GB: 39/48 비트 공간의 "커널 쪽" (TTBR1 대신 같은 테이블 상위 영역) */
#define KVA_PAGES 0x4040000000ull

static void build_layout(mmu_t *M, uint64_t ttbr, int *errs) {
    *errs += mmu_map(M, ttbr, UVA_TEXT, 0x10000000ull, 16 * PAGE_SIZE, D_R | D_X | D_U, 0) != E_OK;
    *errs += mmu_map(M, ttbr, UVA_DATA, 0x10100000ull, 32 * PAGE_SIZE, D_R | D_W | D_U, 0) != E_OK;
    *errs += mmu_map(M, ttbr, UVA_RO, 0x10200000ull, 8 * PAGE_SIZE, D_R | D_U, 0) != E_OK;
    *errs += mmu_map(M, ttbr, KVA_BLK2M, 0x100000000ull, 4ull << 20, D_R | D_W | D_G, 0) != E_OK;  /* 2 MB 블록 2 개 */
    *errs += mmu_map(M, ttbr, KVA_BLK1G, 0x200000000ull, 1ull << 30, D_R | D_X | D_G, 0) != E_OK;  /* 1 GB 블록 */
    *errs += mmu_map(M, ttbr, KVA_PAGES, 0x300000000ull, 8 * PAGE_SIZE, D_R | D_W | D_G, MAP_4K) != E_OK;
}
static uint64_t rand_va(uint32_t *s) {
    static const uint64_t base[] = {UVA_TEXT, UVA_DATA, UVA_RO, KVA_BLK2M, KVA_BLK1G, KVA_PAGES, 0x01000000ull};
    static const uint64_t span[] = {16 * PAGE_SIZE, 32 * PAGE_SIZE, 8 * PAGE_SIZE, 4ull << 20, 1ull << 30, 8 * PAGE_SIZE, 64 * PAGE_SIZE};
    uint32_t r = xs32(s);
    if (r % 50 == 0) return (1ull << 50) | (uint64_t)xs32(s);       /* VA 범위 밖 */
    unsigned k = (r >> 8) % 7;
    uint64_t extra = (r >> 16) % 4 == 0 ? 8 * PAGE_SIZE : 0;          /* 25%: 영역 끝을 넘어 구멍으로 */
    uint64_t off = (((uint64_t)xs32(s) << 32) | xs32(s)) % (span[k] + extra);
    return base[k] + off;
}

static void part_a(void) {
    printf("== Part A: TLB 히트 == 새 워크, 교체 정책, 엔트리 크기 ==\n");
    mmu_t M0, *M = &M0;
    int errs = 0; long bad = 0;

    /* A-1: 워크 읽기 수 = 리프까지의 단계 수 */
    printf("  config            first access (miss)   second access\n");
    static const struct { int levels; uint64_t size; int flags; int reads; const char *name; } WK[] = {
        {3, PAGE_SIZE, 0, 3, "3-level 4KB page "}, {4, PAGE_SIZE, 0, 4, "4-level 4KB page "},
        {2, PAGE_SIZE, 0, 2, "2-level 4KB page "}, {3, 2ull << 20, 0, 2, "3-level 2MB block"},
        {3, 1ull << 30, 0, 1, "3-level 1GB block"}, {4, 1ull << 30, 0, 2, "4-level 1GB block"},
    };
    for (unsigned i = 0; i < sizeof WK / sizeof WK[0]; i++) {
        if (mmu_init(M, WK[i].levels, 16, R_LRU)) return;
        proc_t p = proc_new(M, 1, 1);
        errs += mmu_map(M, p.ttbr, (1ull << 30) & ((1ull << va_bits(M)) - 1), 1ull << 30, WK[i].size, D_R | D_W | D_U, WK[i].flags) != E_OK;
        context_switch(M, &p, SW_ASID);
        uint64_t va = ((1ull << 30) & ((1ull << va_bits(M)) - 1)) + 0x123;
        xlate_t r1 = acc_chk(M, va, ACC_R | ACC_USER, &bad);
        uint64_t rd1 = M->st.walk_reads;
        xlate_t r2 = acc_chk(M, va + 8, ACC_W | ACC_USER, &bad);
        printf("  %s  walk_reads=%llu hit=%d     hit=%d walk_reads=%llu\n", WK[i].name,
               (unsigned long long)rd1, r1.hit, r2.hit, (unsigned long long)M->st.walk_reads);
        CHECK(rd1 == (uint64_t)WK[i].reads && !r1.hit && r1.fault == F_NONE);
        CHECK(r2.hit && M->st.walk_reads == rd1 && r2.pa == r1.pa + 8);
        mmu_free(M);
    }

    /* A-2: 블록 하나 = 엔트리 하나가 512 페이지를 덮는다 */
    for (int blk = 0; blk < 2; blk++) {
        if (mmu_init(M, 3, 64, R_LRU)) return;
        proc_t p = proc_new(M, 1, 1);
        errs += mmu_map(M, p.ttbr, 0x200000, 0x40000000, 2ull << 20, D_R | D_U, blk ? 0 : MAP_4K) != E_OK;
        context_switch(M, &p, SW_ASID);
        for (uint64_t pg = 0; pg < 512; pg++) acc_chk(M, 0x200000 + pg * PAGE_SIZE + 64, ACC_R | ACC_USER, &bad);
        printf("  2 MB 영역 512 페이지 순회, %s: misses=%llu hits=%llu walk_reads=%llu\n", blk ? "2MB 블록" : "4KB 페이지",
               (unsigned long long)M->st.misses, (unsigned long long)M->st.hits, (unsigned long long)M->st.walk_reads);
        if (blk) CHECK(M->st.misses == 1 && M->st.hits == 511 && M->st.walk_reads == 2);
        else CHECK(M->st.misses == 512 && M->st.walk_reads == 512 * 3);
        mmu_free(M);
    }

    /* A-3: 교체 정책 — N=4, 페이지 a b c d a e 다음 b? a? */
    for (int pol = 0; pol < 3; pol++) {
        if (mmu_init(M, 3, 4, (repl_t)pol)) return;
        proc_t p = proc_new(M, 1, 1);
        errs += mmu_map(M, p.ttbr, 0x100000, 0x8000000, 8 * PAGE_SIZE, D_R | D_U, 0) != E_OK;
        context_switch(M, &p, SW_ASID);
        const int seq[] = {0, 1, 2, 3, 0, 4};
        for (int i = 0; i < 6; i++) acc_chk(M, 0x100000 + (uint64_t)seq[i] * PAGE_SIZE, ACC_R | ACC_USER, &bad);
        bool b_hit = acc_chk(M, 0x100000 + 1 * PAGE_SIZE, ACC_R | ACC_USER, &bad).hit;
        bool a_hit = acc_chk(M, 0x100000 + 0 * PAGE_SIZE, ACC_R | ACC_USER, &bad).hit;
        printf("  N=4 %-6s: a b c d a e → 그 다음 b %s, a %s\n", REPL_NAME[pol], b_hit ? "hit " : "miss", a_hit ? "hit" : "miss");
        if (pol == R_LRU) CHECK(!b_hit && a_hit);          /* e 가 LRU 인 b 를 밀어냄 */
        if (pol == R_FIFO) CHECK(b_hit && !a_hit);         /* e 가 가장 먼저 들어온 a 를 밀어냄 */
        mmu_free(M);
    }
    /* N+1 페이지 순환: LRU·FIFO 는 히트 0, RANDOM 은 일부 히트 */
    uint64_t loop_hits[3];
    for (int pol = 0; pol < 3; pol++) {
        if (mmu_init(M, 3, 4, (repl_t)pol)) return;
        proc_t p = proc_new(M, 1, 1);
        errs += mmu_map(M, p.ttbr, 0x100000, 0x8000000, 8 * PAGE_SIZE, D_R | D_U, 0) != E_OK;
        context_switch(M, &p, SW_ASID);
        for (int i = 0; i < 500; i++) acc_chk(M, 0x100000 + (uint64_t)(i % 5) * PAGE_SIZE, ACC_R | ACC_USER, &bad);
        loop_hits[pol] = M->st.hits;
        mmu_free(M);
    }
    printf("  N=4, 5 페이지 순환 × 100: hits LRU=%llu FIFO=%llu RANDOM=%llu (of 500)\n",
           (unsigned long long)loop_hits[0], (unsigned long long)loop_hits[1], (unsigned long long)loop_hits[2]);
    CHECK(loop_hits[0] == 0 && loop_hits[1] == 0 && loop_hits[2] > 100);

    /* A-4: 랜덤 등가성 — 2 구성 × 3 크기 × 3 정책 × 20000 접근 */
    long total_bad = 0; uint64_t tot_hits = 0, tot_acc = 0, tot_perm_hits = 0;
    for (int lv = 3; lv <= 4; lv++)
        for (int ni = 0; ni < 3; ni++)
            for (int pol = 0; pol < 3; pol++) {
                static const int NS[] = {4, 16, 64};
                if (mmu_init(M, lv, NS[ni], (repl_t)pol)) return;
                proc_t p = proc_new(M, 1, 1);
                build_layout(M, p.ttbr, &errs);
                context_switch(M, &p, SW_ASID);
                uint32_t s = 12345u + (uint32_t)(lv * 100 + ni * 10 + pol);
                long b = 0;
                for (int i = 0; i < 20000; i++) {
                    uint64_t va = rand_va(&s);
                    int acc = (int)(xs32(&s) % 3) | ((xs32(&s) & 1) ? ACC_USER : 0);
                    xlate_t r = acc_chk(M, va, acc, &b);
                    if (r.hit && r.fault == F_PERMISSION) tot_perm_hits++;
                }
                const stats_t *t = &M->st;
                CHECK(t->hits + t->misses + t->faults[F_ADDRESS_SIZE] == t->accesses && t->walks == t->misses);
                total_bad += b; tot_hits += t->hits; tot_acc += t->accesses;
                if (lv == 3 && pol == R_LRU) {
                    printf("  random 3-level N=%-2d LRU: acc=%llu hits=%llu misses=%llu walk_reads=%llu  faults T=%llu P=%llu AS=%llu\n",
                           NS[ni], (unsigned long long)t->accesses, (unsigned long long)t->hits, (unsigned long long)t->misses,
                           (unsigned long long)t->walk_reads, (unsigned long long)t->faults[F_TRANSLATION],
                           (unsigned long long)t->faults[F_PERMISSION], (unsigned long long)t->faults[F_ADDRESS_SIZE]);
                }
                mmu_free(M);
            }
    printf("  equivalence: 18 configs, %llu accesses, %llu TLB hits (%llu permission faults from hits), mismatches=%ld\n",
           (unsigned long long)tot_acc, (unsigned long long)tot_hits, (unsigned long long)tot_perm_hits, total_bad);
    CHECK(total_bad == 0 && bad == 0);
    CHECK(tot_hits > tot_acc / 4 && tot_perm_hits > 0);
    CHECK(errs == 0);
}

/* ---------------- Part B ---------------- */
#define KERN_PAGES 8
#define KVA_KERN 0x4000000000ull
#define UVA_WS   0x00400000ull

typedef struct { stats_t st; long bad; } runres_t;

/* 두 프로세스(같은 user VA → 다른 PA) + 공유 커널 global 페이지. K 접근마다 교대. 접근 25% 는 커널. */
static runres_t run_two_proc(int levels, int tlb_n, swpol_t sw, int ws, int K, int total) {
    runres_t R; memset(&R, 0, sizeof R);
    mmu_t M0, *M = &M0;
    if (mmu_init(M, levels, tlb_n, R_LRU)) return R;
    proc_t P[2] = { proc_new(M, 0, 1), proc_new(M, 1, 2) };
    int errs = 0;
    for (int k = 0; k < 2; k++) {
        errs += mmu_map(M, P[k].ttbr, UVA_WS, 0x20000000ull + (uint64_t)k * 0x1000000ull, (uint64_t)ws * PAGE_SIZE, D_R | D_W | D_U, MAP_4K) != E_OK;
        errs += mmu_map(M, P[k].ttbr, KVA_KERN, 0x7000000ull, KERN_PAGES * PAGE_SIZE, D_R | D_W | D_G, MAP_4K) != E_OK;
    }
    uint32_t s = 0xC0FFEEu;
    for (int i = 0; i < total; i++) {
        if (i % K == 0) context_switch(M, &P[(i / K) & 1], sw);
        uint32_t r = xs32(&s);
        if (r % 4 == 0) acc_chk(M, KVA_KERN + (uint64_t)((r >> 2) % KERN_PAGES) * PAGE_SIZE + (r >> 20), ACC_R, &R.bad);
        else acc_chk(M, UVA_WS + (uint64_t)((r >> 2) % (uint32_t)ws) * PAGE_SIZE + (r >> 20), ((r >> 16) & 1) | ACC_USER, &R.bad);
    }
    R.st = M->st;
    if (errs) R.bad += 1000000;
    mmu_free(M);
    return R;
}

static void part_b(void) {
    printf("== Part B: 두 프로세스 교대 — 전체 플러시 vs 비전역 플러시 vs ASID ==\n");
    mmu_t M0, *M = &M0;
    long bad = 0; int errs = 0;

    /* B-1: 플러시 범위 — global 은 ASID 플러시에서 살아남는다 */
    if (mmu_init(M, 3, 16, R_LRU)) return;
    proc_t A = proc_new(M, 0, 1), B = proc_new(M, 1, 2);
    errs += mmu_map(M, A.ttbr, UVA_WS, 0x20000000ull, PAGE_SIZE, D_R | D_W | D_U, 0) != E_OK;
    errs += mmu_map(M, B.ttbr, UVA_WS, 0x21000000ull, PAGE_SIZE, D_R | D_W | D_U, 0) != E_OK;
    errs += mmu_map(M, A.ttbr, KVA_KERN, 0x7000000ull, PAGE_SIZE, D_R | D_W | D_G, 0) != E_OK;
    errs += mmu_map(M, B.ttbr, KVA_KERN, 0x7000000ull, PAGE_SIZE, D_R | D_W | D_G, 0) != E_OK;
    context_switch(M, &A, SW_ASID);
    acc_chk(M, UVA_WS, ACC_R | ACC_USER, &bad); acc_chk(M, KVA_KERN, ACC_R, &bad);
    context_switch(M, &B, SW_ASID);
    xlate_t bu = acc_chk(M, UVA_WS, ACC_R | ACC_USER, &bad), bk = acc_chk(M, KVA_KERN, ACC_R, &bad);
    printf("  B after A (ASID): user VA hit=%d pa=0x%llx (own), kernel VA hit=%d (global)\n", bu.hit,
           (unsigned long long)bu.pa, bk.hit);
    CHECK(!bu.hit && bu.pa == 0x21000000ull && bk.hit && tlb_valid_count(&M->tlb) == 3);
    tlbi_asid(M, 1);
    CHECK(tlb_valid_count(&M->tlb) == 2 && acc_chk(M, KVA_KERN, ACC_R, &bad).hit);   /* A 의 user 만 사라짐 */
    tlbi_va_asid(M, KVA_KERN, 7);                                                  /* 무관한 ASID 로도 global 은 지워짐 */
    CHECK(tlb_valid_count(&M->tlb) == 1 && !acc_chk(M, KVA_KERN, ACC_R, &bad).hit);
    tlbi_nonglobal(M);
    CHECK(tlb_valid_count(&M->tlb) == 1);                                          /* 남은 것 = global 커널 엔트리 */
    tlbi_all(M);
    CHECK(tlb_valid_count(&M->tlb) == 0 && M->st.inval_entries == 4);
    mmu_free(M);

    /* B-2: 위험 1 — unmap 후 TLBI 누락 = stale 변환 */
    if (mmu_init(M, 3, 16, R_LRU)) return;
    A = proc_new(M, 0, 1);
    errs += mmu_map(M, A.ttbr, UVA_WS, 0x80000000ull, PAGE_SIZE, D_R | D_W | D_U, 0) != E_OK;
    context_switch(M, &A, SW_ASID);
    xlate_t r0 = mmu_access(M, UVA_WS + 0x10, ACC_W | ACC_USER);
    errs += mmu_unmap(M, A.ttbr, UVA_WS, PAGE_SIZE) != E_OK;                    /* 페이지 0x80000000 을 해제했다고 치자 */
    xlate_t r1 = mmu_access(M, UVA_WS + 0x10, ACC_W | ACC_USER);
    xlate_t w1 = walk(M, A.ttbr, UVA_WS + 0x10, ACC_W | ACC_USER, NULL);
    int st1 = tlb_audit(M, &A, 1, 0);
    printf("  [bug] unmap, no TLBI: access hit=%d fault=%s pa=0x%llx | fresh walk: %s L%d | audit stale=%d\n",
           r1.hit, FAULT_NAME[r1.fault], (unsigned long long)r1.pa, FAULT_NAME[w1.fault], w1.level, st1);
    CHECK(r0.fault == F_NONE && r1.hit && r1.fault == F_NONE && r1.pa == 0x80000010ull);  /* 해제된 프레임에 쓰기 성공! */
    CHECK(w1.fault == F_TRANSLATION && w1.level == 3 && st1 == 1);
    tlbi_va_asid(M, UVA_WS, 1);
    xlate_t r2 = mmu_access(M, UVA_WS + 0x10, ACC_W | ACC_USER);
    printf("  [fix] + TLBI VA,ASID:   access hit=%d fault=%s L%d | audit stale=%d\n", r2.hit, FAULT_NAME[r2.fault], r2.level,
           tlb_audit(M, &A, 1, 0));
    CHECK(!r2.hit && r2.fault == F_TRANSLATION && r2.level == 3 && tlb_audit(M, &A, 1, 0) == 0);
    /* 재매핑(다른 PA)도 같다: 무효화 전에는 옛 PA */
    errs += mmu_map(M, A.ttbr, UVA_WS, 0x90000000ull, PAGE_SIZE, D_R | D_U, 0) != E_OK;
    mmu_access(M, UVA_WS, ACC_R | ACC_USER);
    errs += mmu_unmap(M, A.ttbr, UVA_WS, PAGE_SIZE) != E_OK;
    errs += mmu_map(M, A.ttbr, UVA_WS, 0xA0000000ull, PAGE_SIZE, D_R | D_U, 0) != E_OK;
    xlate_t r3 = mmu_access(M, UVA_WS, ACC_R | ACC_USER);
    tlbi_va_asid(M, UVA_WS, 1);
    xlate_t r4 = mmu_access(M, UVA_WS, ACC_R | ACC_USER);
    printf("  remap 0x90000000→0xA0000000: before TLBI pa=0x%llx, after pa=0x%llx\n", (unsigned long long)r3.pa,
           (unsigned long long)r4.pa);
    CHECK(r3.hit && r3.pa == 0x90000000ull && !r4.hit && r4.pa == 0xA0000000ull);
    mmu_free(M);

    /* B-3: 위험 2 — ASID 재사용 시 플러시 누락 → B 가 A 의 매핑을 본다 */
    if (mmu_init(M, 3, 16, R_LRU)) return;
    A = proc_new(M, 0, 3);
    errs += mmu_map(M, A.ttbr, UVA_WS, 0xA0000000ull, PAGE_SIZE, D_R | D_W | D_U, 0) != E_OK;
    errs += mmu_map(M, A.ttbr, UVA_WS + 0x10000, 0xA0010000ull, PAGE_SIZE, D_R | D_U, 0) != E_OK;  /* A 의 비밀 */
    context_switch(M, &A, SW_ASID);
    mmu_access(M, UVA_WS, ACC_R | ACC_USER); mmu_access(M, UVA_WS + 0x10000, ACC_R | ACC_USER);
    B = proc_new(M, 1, 3);                                   /* A 종료 → 순진한 할당기가 ASID 3 을 곧바로 B 에게 */
    errs += mmu_map(M, B.ttbr, UVA_WS, 0xB0000000ull, PAGE_SIZE, D_R | D_W | D_U, 0) != E_OK;
    context_switch(M, &B, SW_ASID);
    xlate_t bx = mmu_access(M, UVA_WS, ACC_R | ACC_USER), by = mmu_access(M, UVA_WS + 0x10000, ACC_R | ACC_USER);
    int st2 = tlb_audit(M, &B, 1, 0);
    printf("  [bug] ASID 3 reused, no flush: B VA 0x%llx → pa 0x%llx (want 0xb0000000), B VA 0x%llx → %s pa 0x%llx, stale=%d\n",
           (unsigned long long)UVA_WS, (unsigned long long)bx.pa, (unsigned long long)(UVA_WS + 0x10000),
           FAULT_NAME[by.fault], (unsigned long long)by.pa, st2);
    CHECK(bx.hit && bx.pa == 0xA0000000ull && by.hit && by.fault == F_NONE && by.pa == 0xA0010000ull && st2 == 2);
    tlbi_asid(M, 3);
    bx = mmu_access(M, UVA_WS, ACC_R | ACC_USER); by = mmu_access(M, UVA_WS + 0x10000, ACC_R | ACC_USER);
    printf("  [fix] TLBI ASID 3 before reuse: B → pa 0x%llx, secret VA → %s\n", (unsigned long long)bx.pa, FAULT_NAME[by.fault]);
    CHECK(bx.pa == 0xB0000000ull && by.fault == F_TRANSLATION && tlb_audit(M, &B, 1, 0) == 0);
    mmu_free(M);
    CHECK(errs == 0 && bad == 0);

    /* B-4: K 스윕 — 3-level, TLB 64 LRU, 65536 접근 */
    static const int KS[] = {1, 4, 16, 64, 256, 1024};
    static const int WS[] = {16, 48};
    int anomalies = 0;
    for (int w = 0; w < 2; w++) {
        printf("  ws=%d pages/proc + %d kernel global pages (distinct=%d), TLB 64 LRU, 3-level, 65536 accesses\n",
               WS[w], KERN_PAGES, 2 * WS[w] + KERN_PAGES);
        printf("     K | %s miss  walk_rd | %.9s miss  walk_rd | %6s miss  walk_rd | ALL/ASID\n",
               SW_NAME[SW_FLUSH_ALL], SW_NAME[SW_FLUSH_NONGLOBAL] + 6, SW_NAME[SW_ASID]);
        for (int k = 0; k < 6; k++) {
            runres_t R[3];
            for (int sw = 0; sw < 3; sw++) R[sw] = run_two_proc(3, 64, (swpol_t)sw, WS[w], KS[k], 65536);
            printf("  %4d | %9llu %9llu | %9llu %9llu | %9llu %8llu | %7.1fx\n", KS[k],
                   (unsigned long long)R[0].st.misses, (unsigned long long)R[0].st.walk_reads,
                   (unsigned long long)R[1].st.misses, (unsigned long long)R[1].st.walk_reads,
                   (unsigned long long)R[2].st.misses, (unsigned long long)R[2].st.walk_reads,
                   (double)R[0].st.misses / (double)R[2].st.misses);
            CHECK(R[0].bad == 0 && R[1].bad == 0 && R[2].bad == 0);
            CHECK(R[1].st.misses <= R[0].st.misses && R[2].st.misses <= R[0].st.misses);
            if (w == 0) CHECK(R[2].st.misses <= R[1].st.misses);   /* 다 들어갈 때만 ASID ≤ NONGLOBAL 이 보장된다 */
            else if (R[2].st.misses > R[1].st.misses) { anomalies++; printf("         ^ ASID > NONGLOBAL (by %llu): 안 들어가면 ASID 가 질 수도 있다\n",
                     (unsigned long long)(R[2].st.misses - R[1].st.misses)); }
            CHECK(R[0].st.walk_reads == 3 * R[0].st.misses && R[2].st.walk_reads == 3 * R[2].st.misses);
            if (w == 0) CHECK(R[2].st.misses == (uint64_t)(2 * WS[w] + KERN_PAGES));   /* 다 들어감 → 강제 미스만 */
            else CHECK(R[2].st.misses > (uint64_t)(2 * WS[w] + KERN_PAGES));          /* 안 들어감 → 용량 미스 */
        }
    }
    CHECK(anomalies == 1);                                      /* 실제 출력에서 확인한 1 건 (K=1024) — 결정적 */
    runres_t R4 = run_two_proc(4, 64, SW_FLUSH_ALL, 16, 16, 65536), R3 = run_two_proc(3, 64, SW_FLUSH_ALL, 16, 16, 65536);
    printf("  4-level vs 3-level, FLUSH_ALL K=16 ws=16: misses %llu = %llu, walk_reads %llu vs %llu\n",
           (unsigned long long)R4.st.misses, (unsigned long long)R3.st.misses,
           (unsigned long long)R4.st.walk_reads, (unsigned long long)R3.st.walk_reads);
    CHECK(R4.st.misses == R3.st.misses && R4.st.walk_reads == 4 * R4.st.misses && R4.bad == 0);
}

/* ---------------- Part C ---------------- */
#define CP_MAX 1024
static proc_t g_procs[CP_MAX];

/* 진짜 페이지 테이블을 가진 P 개 프로세스, 각자 user VA 8 페이지(같은 VA, 다른 PA) + 커널 global 4 페이지.
 * 스케줄(RR 또는 랜덤)로 nsw 번 스위치, 스위치마다 8 접근. 매 접근 TLB 결과 == 새 워크인지 센다. */
static runres_t run_alloc(int P, int bits, bool use_alloc, bool rr, int nsw, uint64_t *rollovers, int *stale_end) {
    runres_t R; memset(&R, 0, sizeof R);
    mmu_t M0, *M = &M0;
    static asid_alloc_t A;
    if (mmu_init(M, 3, 256, R_LRU)) return R;          /* 256 엔트리 = L2 TLB 급 (대표값) */
    asid_alloc_init(&A, bits);
    int errs = 0;
    for (int k = 0; k < P; k++) {
        g_procs[k] = proc_new(M, k, 0);
        errs += mmu_map(M, g_procs[k].ttbr, UVA_WS, 0x20000000ull + (uint64_t)k * 0x100000ull, 8 * PAGE_SIZE, D_R | D_W | D_U, MAP_4K) != E_OK;
        errs += mmu_map(M, g_procs[k].ttbr, KVA_KERN, 0x7000000ull, 4 * PAGE_SIZE, D_R | D_W | D_G, MAP_4K) != E_OK;
    }
    uint32_t s = 0xBEEF01u;
    for (int i = 0; i < nsw; i++) {
        proc_t *p = &g_procs[rr ? i % P : (int)(xs32(&s) % (uint32_t)P)];
        if (use_alloc) asid_switch_to(&A, M, p); else context_switch(M, p, SW_FLUSH_ALL);
        for (int j = 0; j < 8; j++) {
            uint32_t r = xs32(&s);
            if (r % 4 == 0) acc_chk(M, KVA_KERN + (uint64_t)((r >> 2) % 4) * PAGE_SIZE, ACC_R, &R.bad);
            else acc_chk(M, UVA_WS + (uint64_t)((r >> 2) % 8) * PAGE_SIZE + (r >> 20), ACC_R | ACC_USER, &R.bad);
        }
    }
    if (stale_end) *stale_end = tlb_audit(M, g_procs, P, use_alloc ? A.gen : 0);
    if (rollovers) *rollovers = A.rollovers;
    R.st = M->st;
    if (errs) R.bad += 1000000;
    mmu_free(M);
    return R;
}

/* 할당기만 (테이블·접근 없이) 스위치 nsw 번 → 롤오버 수 */
static uint64_t alloc_only(int P, int bits, bool rr, int nsw) {
    mmu_t M0, *M = &M0;
    static asid_alloc_t A;
    if (mmu_init(M, 2, 8, R_LRU)) return 0;
    asid_alloc_init(&A, bits);
    for (int k = 0; k < P; k++) { g_procs[k].ttbr = 0; g_procs[k].asid = 0; g_procs[k].ctx = 0; g_procs[k].pid = k; }
    uint32_t s = 0x5EED5u;
    for (int i = 0; i < nsw; i++) asid_switch_to(&A, M, &g_procs[rr ? i % P : (int)(xs32(&s) % (uint32_t)P)]);
    mmu_free(M);
    return A.rollovers;
}

static void part_c(void) {
    printf("== Part C: ASID 할당기 + 세대 카운터 ==\n");
    mmu_t M0, *M = &M0;
    static asid_alloc_t A;
    if (mmu_init(M, 3, 64, R_LRU)) return;

    /* C-1: 4 비트 → 쓸 수 있는 ASID 15 개 */
    asid_alloc_init(&A, 4);
    proc_t ps[17];
    for (int k = 0; k < 17; k++) { ps[k].ttbr = 0; ps[k].asid = 0; ps[k].ctx = 0; ps[k].pid = k; }
    bool distinct = true;
    for (int k = 0; k < 15; k++) { asid_switch_to(&A, M, &ps[k]); distinct &= ps[k].asid == (uint16_t)(k + 1); }
    CHECK(distinct && A.rollovers == 0 && A.allocs == 15 && M->st.tlbi_all == 0);
    asid_switch_to(&A, M, &ps[3]);                                       /* 같은 세대: 그대로 */
    CHECK(ps[3].asid == 4 && A.allocs == 15 && M->cur_asid == 4);
    asid_switch_to(&A, M, &ps[15]);                                      /* 16 번째 → 롤오버 */
    printf("  4-bit: 15 procs → asids 1..15, 16th proc → rollover=%llu gen=%llu asid=%u tlbi_all=%llu\n",
           (unsigned long long)A.rollovers, (unsigned long long)A.gen, ps[15].asid, (unsigned long long)M->st.tlbi_all);
    CHECK(A.rollovers == 1 && A.gen == 2 && ps[15].asid == 1 && M->st.tlbi_all == 1);
    asid_switch_to(&A, M, &ps[0]);                                       /* 옛 세대 → 새 ASID */
    CHECK(ps[0].asid == 2 && (ps[0].ctx >> CTX_GEN_SHIFT) == 2 && A.allocs == 17);
    mmu_free(M);

    /* C-2: 실제 변환으로 정확성 — P=20, 4 비트, RR/랜덤, 3000 스위치 × 8 접근 */
    for (int rr = 1; rr >= 0; rr--) {
        uint64_t ro = 0; int stale = 0;
        runres_t Ra = run_alloc(20, 4, true, rr, 3000, &ro, &stale);
        runres_t Rf = run_alloc(20, 4, false, rr, 3000, NULL, NULL);
        g_mut_rollover_no_flush = true;
        uint64_t rom = 0; int stale_m = 0;
        runres_t Rm = run_alloc(20, 4, true, rr, 3000, &rom, &stale_m);
        g_mut_rollover_no_flush = false;
        printf("  P=20 4-bit %-6s: ASID alloc misses=%llu rollovers=%llu mismatches=%ld stale=%d | FLUSH_ALL misses=%llu | mutant(no flush) mismatches=%ld stale=%d\n",
               rr ? "RR" : "random", (unsigned long long)Ra.st.misses, (unsigned long long)ro, Ra.bad, stale,
               (unsigned long long)Rf.st.misses, Rm.bad, stale_m);
        CHECK(Ra.bad == 0 && stale == 0 && ro > 0 && Ra.st.tlbi_all == ro);
        CHECK(Rf.bad == 0 && Ra.st.misses < Rf.st.misses);
        CHECK(Rm.bad > 0);
    }

    /* C-3: 1000 스위치당 롤오버(= 전체 플러시) 수 vs 프로세스 수 */
    static const int PS[] = {4, 8, 15, 16, 17, 32, 64, 255, 256, 300, 1000};
    const int NSW = 100000;
    printf("  rollovers (= full TLB flushes) per 1000 switches, %d switches (FLUSH_ALL policy = 1000)\n", NSW);
    printf("     P | 4-bit RR  4-bit rand | 8-bit RR  8-bit rand\n");
    for (unsigned i = 0; i < sizeof PS / sizeof PS[0]; i++) {
        uint64_t r4 = alloc_only(PS[i], 4, true, NSW), q4 = alloc_only(PS[i], 4, false, NSW);
        uint64_t r8 = alloc_only(PS[i], 8, true, NSW), q8 = alloc_only(PS[i], 8, false, NSW);
        printf("  %4d | %8.2f %10.2f | %8.2f %10.2f\n", PS[i], r4 * 1000.0 / NSW, q4 * 1000.0 / NSW,
               r8 * 1000.0 / NSW, q8 * 1000.0 / NSW);
        CHECK(r4 == (PS[i] <= 15 ? 0u : (uint64_t)(NSW - 1) / 15u));
        CHECK(r8 == (PS[i] <= 255 ? 0u : (uint64_t)(NSW - 1) / 255u));
        CHECK(q4 <= r4 && q8 <= r8);
    }
}

int main(void) {
    part_a();
    part_b();
    part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
