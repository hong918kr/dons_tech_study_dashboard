/* OS-M01 페이지 테이블 관리자: map/unmap/protect와 TLB 무효화
 *
 * 0절  ARCH-A01 모델을 축소해 넣었다 (워커·포맷·블록은 ARCH-A01 소유, 이 레슨은 OS 쪽 "관리 연산"):
 *      4 단계 L0→L1→L2→L3, 4 KB 페이지, 레벨당 인덱스 9 비트, 테이블 1 장 = 512 × 8 B = 4 KB, VA 48 비트.
 *      디스크립터(교육용 포맷, 실제 ARM 비트 위치 아님): bit0 V · bit1 T(L0–L2: 다음 테이블, L3: 페이지) ·
 *      bit2 R · bit3 W · bit4 X · bit5 U · bit7 G · bits 12..47 OA · bit55 SW(이 매핑이 프레임을 소유 → unmap 때 반납.
 *      실제 ARMv8 디스크립터도 55..58 을 소프트웨어용으로 남겨 둔다). 블록 매핑은 쓰지 않는다 (4 KB 페이지만).
 *      시뮬레이션 물리 메모리 4 MB = 1024 프레임: 테이블 페이지와 데이터 페이지가 같은 프레임 할당기(LIFO)에서 나온다.
 *      mock TLB (64 엔트리, ASID 태그, G 엔트리는 모든 ASID 와 일치) + walk cache (8 엔트리: (ASID, VA[47:21]) → L3 테이블 PA).
 *      무효화 명령 4 종을 로그에 남긴다: TLBI_VA_LEAF(그 VA 의 leaf 만, ≈ VALE1) · TLBI_VA(leaf + walk cache, ≈ VAE1) ·
 *      TLBI_ASID(그 ASID 의 비전역 전부, ≈ ASIDE1) · TLBI_ALL(전부, ≈ VMALLE1).
 *      검사기(제공): set_desc 는 "지운/바꾼 엔트리가 가리키던 프레임" 을 무효화 대기(pending) 로 표시하고, 그것을 덮는
 *      무효화가 와야 풀린다. 대기 중인 프레임을 free_frame 하면 early_free++ (= 무효화 전에 재사용될 수 있는 버그).
 *      valid → valid 로 OA 를 바꾸면 bbm_viol++ (break-before-make 위반). tlb_audit() 는 TLB · walk cache 엔트리를
 *      새 워크와 비교해 위험한 낡은(stale) 엔트리 수를 센다 (권한이 "더 적은" 낡은 엔트리는 무해 = spurious fault 감).
 *
 * Part A  walk_alloc (중간 테이블 즉석 할당) · map_range (익명 = 프레임 할당 / 물리 주소 지정 = MMIO, 실패 시 롤백) ·
 *         prune_path (빈 테이블을 아래에서 위로 회수, 테이블마다 live-entry 카운트) · unmap_range · protect_range
 * Part B  tlb_finish (mmu_gather: ≤ ceiling 페이지면 VA 별, 넘으면 ASID 전체 / 전역 페이지가 섞였으면 ALL,
 *         테이블을 풀었으면 walk cache 까지, 프레임 반납은 무효화 "뒤") · mmu_access 의 spurious fault 처리
 * Part C  teardown_level · as_destroy (프로세스 종료: fullmm → ASID 한 번) · migrate_page (break-before-make)
 *
 * 모든 체크는 결정적(단일 코어 모델, 고정 시드 xorshift32). 정책 비교 수치는 demo_flush_policy.c.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 * 데모가 모델만 가져다 쓸 때: #define OSM01_NO_MAIN 후 #include "solution.c"
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GIVEN __attribute__((unused))      /* 시작 코드가 아직 안 부르는 도우미의 -Wunused 경고 끄기 */

/* ======================= 0. ARCH-A01 모델 (축소판) + 기계 상태 (주어짐) ======================= */
#define PAGE_SHIFT 12
#define PAGE_SIZE  (1ull << PAGE_SHIFT)
#define ENTRIES    512u
#define VA_BITS    48
#define NFRAMES    1024u
#define PMEM_BASE  0x40000000ull           /* 프레임 0 의 PA */
#define PA_ANON    UINT64_MAX               /* map_range: "프레임을 새로 할당해 소유하라" */

#define D_V  (1ull << 0)
#define D_T  (1ull << 1)
#define D_R  (1ull << 2)
#define D_W  (1ull << 3)
#define D_X  (1ull << 4)
#define D_U  (1ull << 5)
#define D_G  (1ull << 7)
#define D_SW (1ull << 55)
#define D_OA   0x0000FFFFFFFFF000ull
#define D_PROT (D_R | D_W | D_X)
#define D_PERM (D_R | D_W | D_X | D_U | D_G)

enum { E_OK = 0, E_ALIGN = -1, E_RANGE = -2, E_PERM = -3, E_EXIST = -4, E_NOMEM = -5, E_NOENT = -6, E_NOSYS = -7 };
enum { MAP_GLOBAL = 1u };
typedef enum { F_NONE, F_TRANSLATION, F_PERMISSION } fault_t;
GIVEN static const char *const FAULT_NAME[] = {"NONE", "TRANSLATION", "PERMISSION"};
enum { ACC_R = 0, ACC_W = 1, ACC_X = 2, ACC_TYPE = 3, ACC_USER = 4 };   /* access = 종류 | (USER ? ACC_USER : 0) */
typedef enum { TLBI_VA_LEAF, TLBI_VA, TLBI_ASID, TLBI_ALL } tlbi_kind_t;
GIVEN static const char *const TLBI_NAME[] = {"VA_LEAF", "VA", "ASID", "ALL"};
typedef enum { BUG_NONE, BUG_SKIP_TLBI, BUG_FREE_FIRST, BUG_LEAF_ONLY } bug_t;  /* 테스트용 버그 주입 */
enum { FR_FREE, FR_TABLE, FR_DATA };
enum { TLB_N = 64, WC_N = 8, MAX_ASID = 16, LOG_MAX = 4096, GATHER_VA = 64 };

typedef struct { uint16_t asid; uint64_t root; } as_t;                    /* 주소 공간 = ASID + 루트 테이블 */
typedef struct { bool valid, global; uint16_t asid; uint64_t vpn, pa, perms; } tlbe_t;
typedef struct { bool valid; uint16_t asid; uint64_t region, table; } wce_t; /* region = va >> 21 */
typedef struct { tlbi_kind_t kind; uint16_t asid; uint64_t va; } tlbi_rec_t;
typedef struct { bool on, table, global; uint16_t asid; uint64_t va; unsigned span_shift; } pend_t;

static struct {
    uint8_t    mem[NFRAMES * PAGE_SIZE];   /* 시뮬레이션 물리 메모리 */
    uint8_t    state[NFRAMES];             /* FR_FREE / FR_TABLE / FR_DATA */
    uint16_t   live[NFRAMES];              /* 테이블 프레임의 유효 엔트리 수 — Part A 가 관리한다 */
    uint32_t   freestk[NFRAMES], nfree;    /* LIFO 프레임 할당기: 방금 반납한 프레임이 바로 다시 나간다 */
    int        budget;                     /* ≥ 0 이면 앞으로 이만큼만 할당 성공 (ENOMEM 주입), -1 = 무제한 */
    pend_t     pend[NFRAMES];              /* 무효화를 기다리는 프레임 (검사기) */
    tlbe_t     tlb[TLB_N];  unsigned tlb_next;
    wce_t      wc[WC_N];    unsigned wc_next;
    as_t      *as_by_asid[MAX_ASID];
    tlbi_rec_t log[LOG_MAX]; int nlog;     /* 무효화 기록 */
    unsigned   ceiling;                    /* ≤ ceiling 페이지면 VA 별 무효화 (Linux x86 기본 33) */
    bug_t      bug;
    uint64_t   desc_reads, tlb_hits, tlb_misses, wc_hits, spurious;
    int        early_free, bbm_viol, double_free;
} M;

static unsigned level_shift(int lv) { return PAGE_SHIFT + 9u * (unsigned)(3 - lv); }
static uint64_t level_size(int lv) { return 1ull << level_shift(lv); }
static unsigned va_index(uint64_t va, int lv) { return (unsigned)(va >> level_shift(lv)) & (ENTRIES - 1); }
static bool in_pool(uint64_t pa) { return pa >= PMEM_BASE && pa < PMEM_BASE + (uint64_t)NFRAMES * PAGE_SIZE; }
static uint32_t frame_of(uint64_t pa) { return in_pool(pa) ? (uint32_t)((pa - PMEM_BASE) >> PAGE_SHIFT) : NFRAMES; }
static uint64_t pa_of(uint32_t f) { return PMEM_BASE + ((uint64_t)f << PAGE_SHIFT); }
static int state_of(uint64_t pa) { return in_pool(pa) ? M.state[frame_of(pa)] : -1; }
static uint64_t peek(uint64_t pa) {                 /* 소프트웨어(OS)의 읽기 — 세지 않는다 */
    uint64_t v = 0;
    if (in_pool(pa) && !(pa & 7)) memcpy(&v, M.mem + (pa - PMEM_BASE), sizeof v);
    return v;
}
static void poke(uint64_t pa, uint64_t v) {
    if (in_pool(pa) && !(pa & 7)) memcpy(M.mem + (pa - PMEM_BASE), &v, sizeof v);
}
static uint64_t rd(uint64_t pa) { M.desc_reads++; return peek(pa); }   /* 하드웨어 워커의 읽기 (센다) */

static void machine_reset(void) {
    memset(&M, 0, sizeof M);
    for (uint32_t i = 0; i < NFRAMES; i++) M.freestk[i] = NFRAMES - 1 - i;   /* 프레임 0 부터 나간다 */
    M.nfree = NFRAMES; M.budget = -1; M.ceiling = 33;
}
static uint64_t alloc_frame(int kind) {             /* 0 = 메모리 없음. 내용은 0 으로 채워서 준다 */
    if (M.nfree == 0 || M.budget == 0) return 0;
    if (M.budget > 0) M.budget--;
    uint32_t f = M.freestk[--M.nfree];
    memset(M.mem + (size_t)f * PAGE_SIZE, 0, PAGE_SIZE);
    M.state[f] = (uint8_t)kind; M.live[f] = 0;
    return pa_of(f);
}
static void free_frame(uint32_t f) {
    if (f >= NFRAMES || M.state[f] == FR_FREE) { M.double_free++; return; }
    if (M.pend[f].on) { M.early_free++; M.pend[f].on = false; }   /* 아직 TLB/walk cache 가 볼 수 있는 프레임! */
    M.state[f] = FR_FREE; M.live[f] = 0;
    M.freestk[M.nfree++] = f;
}

/* 디스크립터 쓰기는 반드시 이 함수로. va = 이 엔트리가 덮는 아무 주소, lv = 엔트리가 있는 레벨 (0..3).
 * 유효했던 엔트리를 지우거나 OA 를 바꾸면, 옛 OA 프레임은 "덮는 무효화가 올 때까지" 대기 상태가 된다. */
GIVEN static void set_desc(const as_t *as, uint64_t ea, uint64_t va, int lv, uint64_t d) {
    uint64_t old = peek(ea);
    if ((old & D_V) && (d & D_V) && ((old ^ d) & D_OA)) M.bbm_viol++;
    if ((old & D_V) && (!(d & D_V) || ((old ^ d) & D_OA)) && in_pool(old & D_OA)) {
        pend_t *p = &M.pend[frame_of(old & D_OA)];
        p->on = true; p->table = lv < 3; p->global = lv == 3 && (old & D_G); p->asid = as->asid;
        p->va = va; p->span_shift = lv < 3 ? level_shift(lv) : PAGE_SHIFT;
    }
    poke(ea, d);
}

/* ---------- 무효화 명령 (하드웨어, 단일 코어) ---------- */
static bool pend_hit(const pend_t *p, tlbi_kind_t k, uint16_t asid, uint64_t va) {
    if (!p->on) return false;
    switch (k) {
    case TLBI_ALL:     return true;
    case TLBI_ASID:    return p->asid == asid && !p->global;
    case TLBI_VA_LEAF: return !p->table && (p->asid == asid || p->global) && (p->va >> PAGE_SHIFT) == (va >> PAGE_SHIFT);
    case TLBI_VA:      return (p->asid == asid || p->global) && (p->va >> p->span_shift) == (va >> p->span_shift);
    }
    return false;
}
static void tlbi(tlbi_kind_t k, uint16_t asid, uint64_t va) {
    if (M.nlog < LOG_MAX) M.log[M.nlog++] = (tlbi_rec_t){k, asid, va};
    uint64_t vpn = va >> PAGE_SHIFT;
    for (int i = 0; i < TLB_N; i++) {
        tlbe_t *e = &M.tlb[i];
        if (e->valid && (k == TLBI_ALL || (k == TLBI_ASID && e->asid == asid && !e->global) ||
                         (k <= TLBI_VA && e->vpn == vpn && (e->asid == asid || e->global))))
            e->valid = false;
    }
    for (int i = 0; i < WC_N; i++) {
        wce_t *w = &M.wc[i];
        if (w->valid && (k == TLBI_ALL || (k == TLBI_ASID && w->asid == asid) ||
                         (k == TLBI_VA && w->asid == asid && w->region == (va >> 21))))
            w->valid = false;
    }
    for (uint32_t f = 0; f < NFRAMES; f++) if (pend_hit(&M.pend[f], k, asid, va)) M.pend[f].on = false;
}
GIVEN static void tlbi_va_leaf(uint16_t asid, uint64_t va) { tlbi(TLBI_VA_LEAF, asid, va); }
GIVEN static void tlbi_va(uint16_t asid, uint64_t va) { tlbi(TLBI_VA, asid, va); }
GIVEN static void tlbi_asid(uint16_t asid) { tlbi(TLBI_ASID, asid, 0); }
GIVEN static void tlbi_all(void) { tlbi(TLBI_ALL, 0, 0); }

static tlbe_t *tlb_find(uint16_t asid, uint64_t vpn) {
    for (int i = 0; i < TLB_N; i++) {
        tlbe_t *e = &M.tlb[i];
        if (e->valid && e->vpn == vpn && (e->global || e->asid == asid)) return e;
    }
    return NULL;
}
static void tlb_fill(uint16_t asid, uint64_t vpn, uint64_t pa, uint64_t perms, bool global) {
    int slot = -1;
    for (int i = 0; i < TLB_N && slot < 0; i++) if (!M.tlb[i].valid) slot = i;
    if (slot < 0) slot = (int)(M.tlb_next++ % TLB_N);                /* 꽉 차면 라운드 로빈 */
    M.tlb[slot] = (tlbe_t){true, global, asid, vpn, pa, perms};
}
static wce_t *wc_find(uint16_t asid, uint64_t region) {
    for (int i = 0; i < WC_N; i++)
        if (M.wc[i].valid && M.wc[i].asid == asid && M.wc[i].region == region) return &M.wc[i];
    return NULL;
}
static void wc_fill(uint16_t asid, uint64_t region, uint64_t table) {
    int slot = -1;
    for (int i = 0; i < WC_N && slot < 0; i++) if (!M.wc[i].valid) slot = i;
    if (slot < 0) slot = (int)(M.wc_next++ % WC_N);
    M.wc[slot] = (wce_t){true, asid, region, table};
}

/* ARCH-A01 의 권한 규칙: user 는 U=1 필요, kernel 은 U=1 페이지 실행 금지 (SMEP/PXN 개념). X 는 R 과 독립. */
static bool perm_ok(uint64_t d, int acc) {
    bool user = (acc & ACC_USER) != 0;
    int kind = acc & ACC_TYPE;
    if (user && !(d & D_U)) return false;
    if (!user && kind == ACC_X && (d & D_U)) return false;
    return kind == ACC_R ? (d & D_R) != 0 : kind == ACC_W ? (d & D_W) != 0 : (d & D_X) != 0;
}

/* OS 쪽 워크: tbl[0] = 루트, tbl[k] = 레벨 k 테이블 PA, ea[k] = 레벨 k 에서 va 의 엔트리 주소.
 * 반환 = 멈춘 레벨. 0..2 면 ea[ret] 가 무효라 그 아래 테이블이 없다. 3 이면 ea[3] = L3 슬롯 (유효 여부는 호출자가 본다). */
static int walk_path(const as_t *as, uint64_t va, uint64_t tbl[4], uint64_t ea[4]) {
    tbl[0] = as->root;
    for (int lv = 0; lv < 3; lv++) {
        ea[lv] = tbl[lv] + 8u * va_index(va, lv);
        uint64_t d = peek(ea[lv]);
        if (!(d & D_V)) return lv;
        tbl[lv + 1] = d & D_OA;
    }
    ea[3] = tbl[3] + 8u * va_index(va, 3);
    return 3;
}
typedef struct { fault_t fault; uint64_t pa, perms; } walk_t;
static walk_t walk(const as_t *as, uint64_t va) {   /* "새 워크": TLB 를 안 쓰고 트리만 본다 (권한 검사 없음) */
    uint64_t tbl[4], ea[4];
    walk_t w = {F_TRANSLATION, 0, 0};
    if ((va >> VA_BITS) || walk_path(as, va, tbl, ea) < 3) return w;
    uint64_t d = peek(ea[3]);
    if (!(d & D_V) || !(d & D_T)) return w;
    w.fault = F_NONE; w.pa = (d & D_OA) | (va & (PAGE_SIZE - 1)); w.perms = d & D_PERM;
    return w;
}

static int as_create(as_t *as, uint16_t asid) {
    if (asid >= MAX_ASID || M.as_by_asid[asid]) return E_EXIST;
    uint64_t r = alloc_frame(FR_TABLE);
    if (!r) return E_NOMEM;
    as->asid = asid; as->root = r; M.as_by_asid[asid] = as;
    return E_OK;
}
GIVEN static int check_range(uint64_t va, uint64_t size) {
    if (size == 0 || ((va | size) & (PAGE_SIZE - 1))) return E_ALIGN;
    if ((va >> VA_BITS) || size > (1ull << VA_BITS) - va) return E_RANGE;
    return E_OK;
}
/* [va, va+size) 에서 매핑된 페이지 수. 없는 하위 트리는 그 레벨 크기만큼 통째로 건너뛴다. */
GIVEN static uint64_t count_mapped(const as_t *as, uint64_t va, uint64_t size) {
    uint64_t n = 0, end = va + size;
    while (va < end) {
        uint64_t tbl[4], ea[4];
        int lv = walk_path(as, va, tbl, ea);
        if (lv < 3) { va = (va & ~(level_size(lv) - 1)) + level_size(lv); continue; }
        if (peek(ea[3]) & D_V) n++;
        va += PAGE_SIZE;
    }
    return n;
}

/* ---------- mmu_gather (Linux 의 이름을 빌림): 이번 연산에서 무효화할 것 + 무효화 뒤에 반납할 프레임 ---------- */
typedef struct {
    const as_t *as;
    int npages;  uint64_t va[GATHER_VA];   /* 무효화가 필요한 leaf: unmap 한 페이지, 권한을 줄인 페이지 */
    int ntables; uint64_t tva[GATHER_VA];  /* 푼 테이블 (그 테이블이 덮는 아무 VA) */
    bool global, freed_tables, fullmm;
    int nframes; uint32_t frames[NFRAMES]; /* 무효화가 끝난 뒤 반납할 프레임 */
} gather_t;
GIVEN static void gather_init(gather_t *g, const as_t *as) {
    g->as = as; g->npages = g->ntables = g->nframes = 0;
    g->global = g->freed_tables = g->fullmm = false;
}
GIVEN static void gather_page(gather_t *g, uint64_t va, bool global) {
    if (g->npages < GATHER_VA) g->va[g->npages] = va;
    g->npages++; g->global |= global;
}
GIVEN static void gather_table(gather_t *g, uint32_t f, uint64_t va) {
    if (g->ntables < GATHER_VA) g->tva[g->ntables] = va;
    g->ntables++; g->freed_tables = true;
    g->frames[g->nframes++] = f;
}
GIVEN static void gather_data(gather_t *g, uint32_t f) { g->frames[g->nframes++] = f; }

static void tlb_finish(gather_t *g);
static int unmap_range(const as_t *as, uint64_t va, uint64_t size);
GIVEN static void gather_done(gather_t *g) {              /* 버그 주입은 여기서만 (테스트가 켠다) */
    if (M.bug == BUG_FREE_FIRST) {                  /* 반납 먼저, 무효화 나중 */
        for (int i = 0; i < g->nframes; i++) free_frame(g->frames[i]);
        g->nframes = 0;
    }
    if (M.bug == BUG_LEAF_ONLY)                     /* 테이블을 풀었는데도 leaf 만 무효화 */
        for (int i = 0; i < g->npages && i < GATHER_VA; i++) tlbi_va_leaf(g->as->asid, g->va[i]);
    if (M.bug == BUG_SKIP_TLBI || M.bug == BUG_LEAF_ONLY) {
        g->npages = g->ntables = 0;
        g->global = g->freed_tables = g->fullmm = false;
    }
    tlb_finish(g);
}

/* ---------- 하드웨어 쪽 접근: TLB → walk cache → 전체 워크 ---------- */
typedef struct { fault_t fault; uint64_t pa; bool tlb_hit; } xr_t;
static xr_t mmu_access(const as_t *as, uint64_t va, int acc) {
    xr_t r = {F_TRANSLATION, 0, false};
    if (va >> VA_BITS) return r;
    uint64_t off = va & (PAGE_SIZE - 1);
    tlbe_t *e = tlb_find(as->asid, va >> PAGE_SHIFT);
    if (e) {
        M.tlb_hits++; r.tlb_hit = true;
        if (perm_ok(e->perms, acc)) { r.fault = F_NONE; r.pa = e->pa | off; return r; }
        /* TODO(B-2): spurious fault. TLB 의 권한으로는 안 되지만, PTE(새 워크)는 이미 허용할 수 있다 (권한을 늘리면서
         * 무효화를 생략했기 때문). 그렇다면 M.spurious++, 이 VA 의 leaf 만 무효화하고 다시 시도한다.
         * 새 워크도 거부하면 진짜 PERMISSION 폴트. */
        r.fault = F_PERMISSION;
        return r;
    }
    M.tlb_misses++;
    uint64_t l3;
    wce_t *w = wc_find(as->asid, va >> 21);
    if (w) { M.wc_hits++; l3 = w->table; }          /* 중간 단계를 건너뛰고 캐시된 L3 테이블로 바로 */
    else {
        uint64_t t = as->root;
        for (int lv = 0; lv < 3; lv++) {
            uint64_t d = rd(t + 8u * va_index(va, lv));
            if (!(d & D_V) || !(d & D_T)) return r;
            t = d & D_OA;
        }
        l3 = t;
        wc_fill(as->asid, va >> 21, l3);            /* leaf 가 무효여도 중간 엔트리는 캐시될 수 있다 */
    }
    uint64_t d = rd(l3 + 8u * va_index(va, 3));
    if (!(d & D_V) || !(d & D_T)) return r;          /* 무효 엔트리는 TLB 에 안 들어간다 */
    if (!perm_ok(d, acc)) { r.fault = F_PERMISSION; return r; }
    tlb_fill(as->asid, va >> PAGE_SHIFT, d & D_OA, d & D_PERM, (d & D_G) != 0);
    r.fault = F_NONE; r.pa = (d & D_OA) | off;
    return r;
}

/* ======================= Part A: 매핑 관리 ======================= */

/* va 의 L3 슬롯 주소를 돌려준다. 가는 길에 없는 중간 테이블(L1·L2·L3)을 할당해 붙인다.
 * 0 = 메모리 없음 (그때까지 붙인 빈 테이블은 그대로 두고 돌아온다 — 호출자가 prune_path 로 치운다). */
GIVEN static uint64_t walk_alloc(const as_t *as, uint64_t va) {
    /* TODO(A-1): lv = 0..2 에서 엔트리가 무효면 alloc_frame(FR_TABLE) → set_desc(V|T|t) → 부모 테이블 live++.
     * 마지막에 L3 테이블 + 8·va_index(va, 3). */
    (void)as; (void)va;
    return 0;
}

/* va 경로의 가장 깊은 테이블부터 위로: live == 0 인 테이블은 부모 엔트리를 지우고(set_desc 0), 부모 live--,
 * 그 프레임을 gather 에 넣는다 (반납은 무효화 뒤). 루트는 절대 풀지 않는다. 비지 않은 테이블을 만나면 멈춘다. */
GIVEN static void prune_path(const as_t *as, uint64_t va, gather_t *g) {
    /* TODO(A-3) */
    (void)as; (void)va; (void)g;
}

/* [va, va+size) 를 매핑한다. pa == PA_ANON 이면 페이지마다 새 프레임(0 으로 채움, D_SW = 소유),
 * 아니면 pa 부터 연속 물리 주소 (MMIO 등, 소유 안 함 → unmap 때 반납 안 함).
 * perms ⊆ R|W|X|U, W 는 R 과 함께만. flags & MAP_GLOBAL → G. 범위에 이미 매핑이 있으면 E_EXIST (아무것도 안 바꿈).
 * 도중에 메모리가 모자라면 이번에 만든 것을 전부 되돌리고 E_NOMEM. 새 매핑엔 무효화가 필요 없다. */
static int map_range(const as_t *as, uint64_t va, uint64_t pa, uint64_t size, uint64_t perms, unsigned flags) {
    /* TODO(A-2): 검사(E_ALIGN/E_RANGE/E_PERM/E_EXIST) → 페이지마다 walk_alloc · (익명이면) alloc_frame(FR_DATA) ·
     * set_desc(V|T|attrs|OA[|SW]) · L3 테이블 live++. 실패하면 unmap_range(이미 한 부분) + prune_path(실패한 va). */
    (void)as; (void)va; (void)pa; (void)size; (void)perms; (void)flags;
    return E_NOSYS;
}

/* [va, va+size) 의 매핑을 지운다. 빈 곳은 건너뛴다 (멱등). 반환 = 지운 페이지 수 (≥ 0) 또는 E_ALIGN/E_RANGE.
 * 지운 leaf 는 gather 에 (무효화 대상), 소유 프레임과 비게 된 테이블도 gather 에 (반납은 무효화 뒤).
 * 끝에 gather_done → tlb_finish. */
static int unmap_range(const as_t *as, uint64_t va, uint64_t size) {
    /* TODO(A-4): count_mapped 와 같은 순회(없는 하위 트리는 건너뜀). 유효 leaf 면 set_desc 0, L3 live--,
     * gather_page(va, G?), 소유면 gather_data, prune_path. */
    (void)as; (void)va; (void)size;
    return 0;
}

/* [va, va+size) 의 R/W/X 를 prot 로 바꾼다 (U·G 는 그대로). 범위에 빈 페이지가 하나라도 있으면 E_NOENT (아무것도 안 바꿈,
 * Linux mprotect 의 ENOMEM 에 해당). 권한이 하나라도 "빠진" 페이지만 무효화 대상이다. 늘기만 했으면 무효화하지 않는다. */
static int protect_range(const as_t *as, uint64_t va, uint64_t size, uint64_t prot) {
    /* TODO(A-5) */
    (void)as; (void)va; (void)size; (void)prot;
    return E_NOSYS;
}

/* ======================= Part B: 무효화 정책 ======================= */

/* gather 를 끝낸다: ① 무효화 ② 그다음에야 프레임 반납. 정책:
 *   fullmm (주소 공간 해체)               → TLBI_ASID 한 번 (전역 페이지가 있었으면 TLBI_ALL)
 *   npages > M.ceiling                    → TLBI_ASID 한 번 (전역이 섞였으면 TLBI_ALL — ASID 무효화는 G 를 못 지운다)
 *   1 ≤ npages ≤ ceiling                  → 페이지마다: 테이블을 풀었으면 TLBI_VA (walk cache 까지), 아니면 TLBI_VA_LEAF
 *   npages == 0 이고 테이블만 풀었으면      → 푼 테이블마다 TLBI_VA (map 실패 롤백), ceiling 을 넘으면 ASID
 *   아무것도 없으면                        → 무효화 없음 */
static void tlb_finish(gather_t *g) {
    /* TODO(B-1) */
    for (int i = 0; i < g->nframes; i++) free_frame(g->frames[i]);   /* 스텁: 무효화 없이 반납만 (= 버그) */
    g->nframes = 0;
}

/* ======================= Part C: 해체와 break-before-make ======================= */

/* 레벨 lv 테이블(PA = table, 덮는 시작 VA = va) 아래를 전부 해체: 유효 엔트리마다 set_desc 0 + 그 테이블 live--,
 * 하위 테이블은 재귀 후 gather_table, L3 의 소유 프레임은 gather_data, G 가 있었으면 g->global = true. */
GIVEN static void teardown_level(const as_t *as, uint64_t table, int lv, uint64_t va, gather_t *g) {
    /* TODO(C-1) */
    (void)as; (void)table; (void)lv; (void)va; (void)g;
}

/* 프로세스 종료: 트리 전체 해체 → fullmm gather (ASID 한 번) → 프레임·루트 반납 → ASID 를 놓아준다 (무효화 뒤!). */
static void as_destroy(as_t *as) {
    /* TODO(C-2) */
    M.as_by_asid[as->asid] = NULL;                  /* 스텁: ASID 만 놓아준다 (트리·프레임은 샌다) */
    as->root = 0;
}

/* va 의 (소유) 페이지를 새 프레임으로 옮긴다 (페이지 마이그레이션·압축). break-before-make:
 *   ① 새 프레임 할당 (실패 → E_NOMEM, 아무것도 안 바뀜)  ② break: 엔트리를 0 으로 + 그 VA 무효화 (gather_done)
 *   ③ 내용 복사  ④ make: 같은 속성 + 새 OA 설치  ⑤ 옛 프레임 반납.  매핑 없음 → E_NOENT, 소유 아님 → E_PERM. */
static int migrate_page(const as_t *as, uint64_t va, uint64_t *new_pa) {
    /* TODO(C-3) */
    (void)as; (void)va; (void)new_pa;
    return E_NOSYS;
}

/* ======================= 검사 도우미 (주어짐) ======================= */
static int tlb_audit(void) {          /* 위험한 낡은 엔트리 수: 새 워크와 다른 PA, 사라진 매핑, 더 많은 권한, 사라진 ASID */
    int bad = 0;
    for (int i = 0; i < TLB_N; i++) {
        const tlbe_t *e = &M.tlb[i];
        if (!e->valid) continue;
        const as_t *as = M.as_by_asid[e->asid];
        if (!as) { bad++; continue; }
        walk_t w = walk(as, e->vpn << PAGE_SHIFT);
        if (w.fault != F_NONE || (w.pa & D_OA) != e->pa || (e->perms & ~w.perms & (D_PROT | D_U))) bad++;
    }
    for (int i = 0; i < WC_N; i++) {
        const wce_t *w = &M.wc[i];
        if (!w->valid) continue;
        const as_t *as = M.as_by_asid[w->asid];
        uint64_t tbl[4], ea[4];
        if (!as || walk_path(as, w->region << 21, tbl, ea) < 3 || tbl[3] != w->table) bad++;
    }
    return bad;
}
static int live_errors(void) {        /* 모든 테이블 프레임에서 live == 실제 유효 엔트리 수인가 */
    int bad = 0;
    for (uint32_t f = 0; f < NFRAMES; f++) {
        if (M.state[f] != FR_TABLE) continue;
        unsigned n = 0;
        for (unsigned i = 0; i < ENTRIES; i++) n += (peek(pa_of(f) + 8u * i) & D_V) != 0;
        bad += n != M.live[f];
    }
    return bad;
}
static int count_state(int kind) { int n = 0; for (uint32_t f = 0; f < NFRAMES; f++) n += M.state[f] == kind; return n; }
static int tlb_count(uint16_t asid) { int n = 0; for (int i = 0; i < TLB_N; i++) n += M.tlb[i].valid && M.tlb[i].asid == asid; return n; }
static int wc_count(uint16_t asid) { int n = 0; for (int i = 0; i < WC_N; i++) n += M.wc[i].valid && M.wc[i].asid == asid; return n; }
static uint64_t wc_table(uint16_t asid, uint64_t va) { const wce_t *w = wc_find(asid, va >> 21); return w ? w->table : 0; }
static int log_count(tlbi_kind_t k) { int n = 0; for (int i = 0; i < M.nlog; i++) n += M.log[i].kind == k; return n; }
static void log_reset(void) { M.nlog = 0; }
static void touch(const as_t *as, uint64_t va, int npages, int acc) {
    for (int i = 0; i < npages; i++) (void)mmu_access(as, va + (uint64_t)i * PAGE_SIZE, acc);
}
GIVEN static void set_ceiling(unsigned c) { M.ceiling = c > GATHER_VA ? GATHER_VA : c; }

#ifndef OSM01_NO_MAIN
/* ======================= 테스트 ======================= */
static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)
#define PG PAGE_SIZE
static const uint64_t RWU = D_R | D_W | D_U;
static const int UR = ACC_R | ACC_USER, UW = ACC_W | ACC_USER;
static const uint64_t SECRET = 0x5EC12E7DA7Aull;

static void part_a(void) {
    puts("-- Part A: tables on demand, map/unmap/protect, reclaim to baseline, ENOMEM rollback");
    machine_reset();
    as_t a;
    (void)as_create(&a, 1);
    const uint32_t base = M.nfree;
    CHECK(map_range(&a, 0x400000, PA_ANON, 3 * PG, RWU, 0) == E_OK && M.nfree == base - 6);   /* 데이터 3 + L1·L2·L3 */
    walk_t w0 = walk(&a, 0x400000), w1 = walk(&a, 0x401000), w2 = walk(&a, 0x402000);
    CHECK(w0.fault == F_NONE && w1.fault == F_NONE && w2.fault == F_NONE && w0.pa != w1.pa && w1.pa != w2.pa && w0.pa != w2.pa);
    CHECK(w0.perms == RWU && state_of(w0.pa) == FR_DATA);
    CHECK(live_errors() == 0 && M.live[frame_of(a.root)] == 1 && count_state(FR_TABLE) == 4);
    CHECK(map_range(&a, 0x400800, PA_ANON, PG, RWU, 0) == E_ALIGN && map_range(&a, 0x500000, PA_ANON, 0, RWU, 0) == E_ALIGN);
    CHECK(map_range(&a, 1ull << 48, PA_ANON, PG, RWU, 0) == E_RANGE && map_range(&a, (1ull << 48) - PG, PA_ANON, 2 * PG, RWU, 0) == E_RANGE);
    CHECK(map_range(&a, 0x500000, PA_ANON, PG, D_W, 0) == E_PERM && map_range(&a, 0x500000, PA_ANON, PG, D_R | D_G, 0) == E_PERM);
    CHECK(map_range(&a, 0x3FE000, PA_ANON, 4 * PG, RWU, 0) == E_EXIST && M.nfree == base - 6);   /* 겹침 → 아무것도 안 바뀜 */
    CHECK(map_range(&a, 0x5FF000, PA_ANON, 2 * PG, RWU, 0) == E_OK && M.nfree == base - 9);      /* 2 MB 경계: 새 L3 1 장 */
    CHECK(count_state(FR_TABLE) == 5 && M.nlog == 0);                                            /* map 은 무효화 0 번 */

    xr_t r1 = mmu_access(&a, 0x400010, UR), r2 = mmu_access(&a, 0x400018, UW);
    CHECK(r1.fault == F_NONE && !r1.tlb_hit && r2.fault == F_NONE && r2.tlb_hit && r1.pa == w0.pa + 0x10);
    CHECK(mmu_access(&a, 0x400000, ACC_X | ACC_USER).fault == F_PERMISSION && mmu_access(&a, 0x403000, UR).fault == F_TRANSLATION);

    touch(&a, 0x400000, 3, UR); touch(&a, 0x5FF000, 2, UR);
    log_reset();
    CHECK(unmap_range(&a, 0x401000, PG) == 1 && M.nfree == base - 8 && walk(&a, 0x401000).fault == F_TRANSLATION);
    CHECK(mmu_access(&a, 0x401000, UR).fault == F_TRANSLATION && tlb_audit() == 0 && M.early_free == 0);
    CHECK(unmap_range(&a, 0x400000, 0x202000) == 4 && M.nfree == base && count_state(FR_TABLE) == 1);   /* 루트만 남음 */
    CHECK(M.live[frame_of(a.root)] == 0 && live_errors() == 0 && tlb_audit() == 0 && M.early_free == 0);
    log_reset();
    CHECK(unmap_range(&a, 0x400000, 1ull << 30) == 0 && M.nlog == 0 && unmap_range(&a, 0x400800, PG) == E_ALIGN);

    M.budget = 5;                                              /* L1·L2·L3·데이터·데이터 → 세 번째 페이지에서 실패 */
    int rc = map_range(&a, 0x10000000, PA_ANON, 10 * PG, RWU, 0);
    M.budget = -1;
    CHECK(rc == E_NOMEM && M.nfree == base && walk(&a, 0x10000000).fault == F_TRANSLATION && live_errors() == 0);
    M.budget = 2;                                              /* L1·L2 는 생기고 L3 에서 실패 */
    rc = map_range(&a, 0x10000000, PA_ANON, PG, RWU, 0);
    M.budget = -1;
    CHECK(rc == E_NOMEM && M.nfree == base && count_state(FR_TABLE) == 1 && M.early_free == 0);

    CHECK(map_range(&a, 0x400000, PA_ANON, 4 * PG, RWU, 0) == E_OK);
    CHECK(protect_range(&a, 0x400000, 5 * PG, D_R) == E_NOENT && walk(&a, 0x400000).perms == RWU);   /* 구멍 → 그대로 */
    CHECK(protect_range(&a, 0x400000, PG, D_W) == E_PERM && protect_range(&a, 0x400000, PG, D_R | D_U) == E_PERM);
    CHECK(protect_range(&a, 0x400000, 4 * PG, D_R | D_X) == E_OK && walk(&a, 0x403000).perms == (D_R | D_X | D_U));

    const uint32_t before = M.nfree;                           /* MMIO: 물리 주소 지정, 프레임 소유 안 함 */
    CHECK(map_range(&a, 0x70000000, 0x09000000, 2 * PG, D_R | D_W, 0) == E_OK && walk(&a, 0x70001008).pa == 0x09001008);
    CHECK(M.nfree == before - 2 && count_state(FR_DATA) == 4);  /* 새 L2·L3 테이블만, 데이터 프레임 0 */
    CHECK(unmap_range(&a, 0x70000000, 2 * PG) == 2 && M.nfree == before && count_state(FR_DATA) == 4);
    CHECK(unmap_range(&a, 0x400000, 4 * PG) == 4 && M.nfree == base);
    CHECK(tlb_audit() == 0 && M.early_free == 0 && M.bbm_viol == 0 && M.double_free == 0);
}

/* ---------- 랜덤 스트레스: 그림자 모델과 대조 ---------- */
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
enum { SW_CHUNKS = 12, SW_CHUNK = 64, SW_PAGES = SW_CHUNKS * SW_CHUNK };
/* 청크 c = L0 idx (c&1) · L1 idx ((c>>1)&1) · L2 idx (c>>2) 에 있는 64 페이지 → 청크마다 다른 L3, L2·L1 은 일부 공유 */
static uint64_t sw_base(int c) { return ((uint64_t)(c & 1) << 39) | ((uint64_t)((c >> 1) & 1) << 30) | ((uint64_t)(c >> 2) << 21); }
static uint64_t sw_va(int i) { return sw_base(i / SW_CHUNK) + (uint64_t)(i % SW_CHUNK) * PAGE_SIZE; }
static bool s_map[SW_PAGES]; static uint64_t s_pa[SW_PAGES], s_perm[SW_PAGES];
static int distinct_prefixes(unsigned shift) {               /* 매핑이 있는 청크들의 va >> shift 종류 수 */
    uint64_t seen[SW_CHUNKS]; int n = 0;
    for (int c = 0; c < SW_CHUNKS; c++) {
        bool any = false;
        for (int k = 0; k < SW_CHUNK && !any; k++) any = s_map[c * SW_CHUNK + k];
        if (!any) continue;
        uint64_t key = sw_base(c) >> shift; bool dup = false;
        for (int j = 0; j < n; j++) dup |= seen[j] == key;
        if (!dup) seen[n++] = key;
    }
    return n;
}
static int tables_expected(void) {    /* 루트 + L1 (va>>39 종류) + L2 (va>>30) + L3 (va>>21) — 빈 테이블은 0 장이어야 한다 */
    return 1 + distinct_prefixes(39) + distinct_prefixes(30) + distinct_prefixes(21);
}
static void stress(void) {
    puts("-- Part A: random map/unmap/protect/access vs shadow model (3000 ops, 12 chunks, seed 2026)");
    machine_reset();
    as_t a;
    (void)as_create(&a, 2);
    memset(s_map, 0, sizeof s_map);
    const uint32_t base = M.nfree;
    static const uint64_t MAP_PERMS[5] = {D_R | D_U, D_R | D_W | D_U, D_R | D_X | D_U, D_U, D_R};
    static const uint64_t PROTS[5] = {0, D_R, D_R | D_W, D_R | D_X, D_R | D_W | D_X};
    uint32_t s = 2026;
    int bad = 0, nomem = 0, eexist = 0, enoent = 0, nacc = 0, unmapped = 0, last_i = 0, last_len = 1, empty_states = 0;
    for (int op = 0; op < 3000; op++) {
        uint32_t kind = xs32(&s) % 100;
        int i = (int)(xs32(&s) % SW_PAGES), cend = (i / SW_CHUNK + 1) * SW_CHUNK;
        int len = 1 + (int)(xs32(&s) % (kind >= 30 && kind < 55 ? 80 : kind < 30 ? 40 : 16));
        if (i + len > cend) len = cend - i;
        int nm = 0;
        for (int k = 0; k < len; k++) nm += s_map[i + k];
        log_reset();
        if (kind < 30) {
            uint64_t p = MAP_PERMS[xs32(&s) % 5];
            if (xs32(&s) % 8 == 0) M.budget = (int)(xs32(&s) % (uint32_t)(len + 2));   /* 가끔 ENOMEM 주입 */
            int rc = map_range(&a, sw_va(i), PA_ANON, (uint64_t)len * PG, p, 0);
            M.budget = -1;
            if (nm) { bad += rc != E_EXIST; eexist++; }
            else if (rc == E_NOMEM) nomem++;
            else if (rc != E_OK) bad++;
            else for (int k = 0; k < len; k++) { s_map[i + k] = true; s_perm[i + k] = p; s_pa[i + k] = walk(&a, sw_va(i + k)).pa; }
            if (rc == E_OK) bad += M.nlog != 0;                 /* 새 매핑은 무효화 없음 (롤백만 무효화) */
        } else if (kind < 55) {
            int rc = unmap_range(&a, sw_va(i), (uint64_t)len * PG);
            bad += rc != nm;
            unmapped += nm;
            for (int k = 0; k < len; k++) s_map[i + k] = false;
        } else if (kind < 75) {
            uint64_t p = PROTS[xs32(&s) % 5];
            int rc = protect_range(&a, sw_va(i), (uint64_t)len * PG, p);
            if (nm != len) { bad += rc != E_NOENT; enoent++; }
            else if (rc != E_OK) bad++;
            else for (int k = 0; k < len; k++) s_perm[i + k] = (s_perm[i + k] & ~D_PROT) | p;
        } else {
            for (int t = 0; t < 30; t++) {
                int j = (t & 1) ? last_i + (int)(xs32(&s) % (uint32_t)last_len) : (int)(xs32(&s) % SW_PAGES);
                int acc = (int)(xs32(&s) % 3) | ACC_USER;           /* 절반은 직전 연산의 범위 (TLB 재사용) */
                xr_t r = mmu_access(&a, sw_va(j) + 8, acc);
                fault_t want = !s_map[j] ? F_TRANSLATION : perm_ok(s_perm[j], acc) ? F_NONE : F_PERMISSION;
                bad += r.fault != want || (want == F_NONE && r.pa != s_pa[j] + 8);
                nacc++;
            }
        }
        if (kind < 75) { last_i = i; last_len = len; }
        if (tlb_audit() || M.early_free || M.bbm_viol || M.double_free || live_errors()) bad++;
        int ndata = 0;
        for (int k = 0; k < SW_PAGES; k++) {
            ndata += s_map[k];
            walk_t w = walk(&a, sw_va(k));
            if (s_map[k] ? (w.fault != F_NONE || w.pa != s_pa[k] || w.perms != s_perm[k]) : w.fault != F_TRANSLATION) bad++;
        }
        if (count_state(FR_DATA) != ndata || count_state(FR_TABLE) != tables_expected()) bad++;
        empty_states += tables_expected() < 1 + 2 + 4 + SW_CHUNKS;   /* 어떤 청크가 비어 테이블이 회수된 상태 */
    }
    printf("  ops=3000 accesses=%d unmapped_pages=%d E_EXIST=%d E_NOENT=%d E_NOMEM=%d spurious=%llu reclaimed_states=%d bad=%d\n",
           nacc, unmapped, eexist, enoent, nomem, (unsigned long long)M.spurious, empty_states, bad);
    CHECK(bad == 0 && nomem > 0 && M.spurious > 0 && empty_states > 0);
    for (int c = 0; c < SW_CHUNKS; c++) (void)unmap_range(&a, sw_base(c), SW_CHUNK * PG);
    CHECK(M.nfree == base && count_state(FR_TABLE) == 1 && tlb_audit() == 0 && M.early_free == 0);
}

/* 누락 시나리오: a 가 페이지 하나를 쓰다 unmap, b 가 a 의 반납 프레임 4 장을 데이터로 재사용하고 비밀을 쓴다. */
typedef struct { xr_t r; int early, audit; } leak_t;
static leak_t leak_scenario(bug_t bug) {
    machine_reset();
    as_t a, b;
    (void)as_create(&a, 1); (void)as_create(&b, 2);
    (void)map_range(&b, 0x10000000, PA_ANON, PG, RWU, 0);      /* b 의 테이블을 미리 만들어 둔다 */
    (void)map_range(&a, 0x400000, PA_ANON, PG, RWU, 0);
    poke(walk(&a, 0x400000).pa, 0xA1A1);
    (void)mmu_access(&a, 0x400000, UR);                         /* TLB 에 a 의 변환 */
    M.bug = bug;
    (void)unmap_range(&a, 0x400000, PG);                         /* 데이터 + L3 + L2 + L1 반납 */
    M.bug = BUG_NONE;
    leak_t L = {{F_NONE, 0, false}, M.early_free, 0};
    (void)map_range(&b, 0x10001000, PA_ANON, 4 * PG, RWU, 0);  /* LIFO: a 의 4 프레임이 b 의 데이터가 된다 */
    for (int k = 1; k <= 4; k++) poke(walk(&b, 0x10000000 + (uint64_t)k * PG).pa, SECRET);
    L.r = mmu_access(&a, 0x400000, UR);
    L.audit = tlb_audit();
    return L;
}

static void part_b(void) {
    puts("-- Part B: flush policy (ceiling 33), ASID vs ALL, spurious fault, injected bugs");
    machine_reset();
    as_t a, b;
    (void)as_create(&a, 1); (void)as_create(&b, 2);
    (void)map_range(&a, 0x400000, PA_ANON, 40 * PG, RWU, 0);
    touch(&a, 0x400000, 40, UR);
    log_reset();
    CHECK(unmap_range(&a, 0x400000, 33 * PG) == 33 && M.nlog == 33 && log_count(TLBI_VA_LEAF) == 33);   /* 테이블 남음 */
    CHECK(unmap_range(&a, 0x400000 + 33 * PG, 7 * PG) == 7 && M.nlog == 40 && log_count(TLBI_VA) == 7);  /* 테이블 반납 */
    CHECK(tlb_count(1) == 0 && wc_count(1) == 0 && tlb_audit() == 0 && M.early_free == 0);
    (void)map_range(&a, 0x400000, PA_ANON, 34 * PG, RWU, 0);
    touch(&a, 0x400000, 34, UR);
    log_reset();
    CHECK(unmap_range(&a, 0x400000, 34 * PG) == 34 && M.nlog == 1 && M.log[0].kind == TLBI_ASID && M.log[0].asid == 1);
    CHECK(tlb_audit() == 0 && M.early_free == 0);

    (void)map_range(&b, 0x10000000, PA_ANON, 4 * PG, RWU, 0);
    touch(&b, 0x10000000, 4, UR);
    (void)map_range(&a, 0x400000, PA_ANON, 40 * PG, RWU, 0);
    touch(&a, 0x400000, 40, UR);
    CHECK(tlb_count(2) == 4 && tlb_count(1) == 40);
    log_reset();
    CHECK(unmap_range(&a, 0x400000, 40 * PG) == 40 && M.nlog == 1 && tlb_count(1) == 0 && tlb_count(2) == 4);  /* 남의 ASID 는 그대로 */
    (void)map_range(&a, 0x7F0000000000ull, PA_ANON, 40 * PG, D_R | D_W, MAP_GLOBAL);
    touch(&a, 0x7F0000000000ull, 40, ACC_R);
    log_reset();
    CHECK(unmap_range(&a, 0x7F0000000000ull, 40 * PG) == 40 && M.nlog == 1 && M.log[0].kind == TLBI_ALL);  /* G 는 ASID 로 못 지움 */
    CHECK(tlb_count(2) == 0 && tlb_audit() == 0 && M.early_free == 0);   /* 대가: b 의 엔트리까지 날아감 */

    (void)map_range(&a, 0x400000, PA_ANON, 8 * PG, RWU, 0);
    touch(&a, 0x400000, 8, UR);
    log_reset();
    CHECK(protect_range(&a, 0x400000, 8 * PG, D_R) == E_OK && M.nlog == 8 && log_count(TLBI_VA_LEAF) == 8);  /* 축소 */
    CHECK(mmu_access(&a, 0x400000, UW).fault == F_PERMISSION && tlb_audit() == 0);
    xr_t r = mmu_access(&a, 0x400000, UR);                       /* TLB 에 R 권한으로 채움 */
    log_reset();
    CHECK(protect_range(&a, 0x400000, 8 * PG, D_R | D_W) == E_OK && M.nlog == 0 && tlb_audit() == 0);   /* 확대: 무효화 0 */
    const uint64_t sp = M.spurious;
    xr_t w1 = mmu_access(&a, 0x400000, UW);
    CHECK(w1.fault == F_NONE && w1.pa == r.pa && M.spurious == sp + 1 && M.nlog == 1 && M.log[0].kind == TLBI_VA_LEAF);
    xr_t w2 = mmu_access(&a, 0x400000, UW);
    CHECK(w2.fault == F_NONE && w2.tlb_hit && M.spurious == sp + 1);
    log_reset();
    CHECK(protect_range(&a, 0x400000, PG, D_R | D_X) == E_OK && M.nlog == 1);        /* RW → RX: W 가 빠짐 = 축소 */
    CHECK(protect_range(&a, 0x401000, PG, 0) == E_OK && M.nlog == 2 && mmu_access(&a, 0x401000, UR).fault == F_PERMISSION);
    CHECK(protect_range(&a, 0x402000, PG, D_R | D_W) == E_OK && M.nlog == 2);         /* 바뀐 게 없으면 아무것도 */

    leak_t bug = leak_scenario(BUG_SKIP_TLBI), ok = leak_scenario(BUG_NONE);
    printf("  skip-TLBI: fault=%s tlb_hit=%d read=0x%llx early_free=%d audit=%d\n", FAULT_NAME[bug.r.fault], bug.r.tlb_hit,
           (unsigned long long)peek(bug.r.pa & ~7ull), bug.early, bug.audit);
    CHECK(bug.early > 0 && bug.r.fault == F_NONE && bug.r.tlb_hit && peek(bug.r.pa & ~7ull) == SECRET && bug.audit > 0);
    CHECK(ok.early == 0 && ok.r.fault == F_TRANSLATION && ok.audit == 0);
    leak_t ff = leak_scenario(BUG_FREE_FIRST);
    CHECK(ff.early > 0 && ff.r.fault == F_TRANSLATION && ff.audit == 0);   /* 결과는 멀쩡해 보여도 순서 위반 */

    machine_reset();                                             /* leaf 만 무효화 → walk cache 가 반납된 테이블을 가리킴 */
    (void)as_create(&a, 1); (void)as_create(&b, 2);
    (void)map_range(&b, 0x10000000, PA_ANON, PG, RWU, 0);
    (void)map_range(&a, 0x400000, PA_ANON, PG, RWU, 0);
    (void)mmu_access(&a, 0x400000, UR);
    const uint64_t T = wc_table(1, 0x400000);
    M.bug = BUG_LEAF_ONLY;
    (void)unmap_range(&a, 0x400000, PG);
    M.bug = BUG_NONE;
    CHECK(T != 0 && tlb_count(1) == 0 && wc_count(1) == 1 && M.early_free > 0);
    (void)map_range(&b, 0x10001000, PA_ANON, 4 * PG, RWU, 0);
    const uint64_t victim = walk(&b, 0x10000000).pa;
    poke(victim, SECRET);
    poke(T + 8u * va_index(0x400000, 3), D_V | D_T | D_R | D_U | victim);   /* b 가 자기 페이지에 쓴 평범한 값 */
    xr_t lr = mmu_access(&a, 0x400000, UR);
    printf("  leaf-only: old L3 table frame is now %s, a reads pa=0x%llx (b's page) via stale walk cache\n",
           state_of(T) == FR_DATA ? "b's DATA" : "?", (unsigned long long)lr.pa);
    CHECK(state_of(T) == FR_DATA && lr.fault == F_NONE && !lr.tlb_hit && lr.pa == victim && tlb_audit() > 0);
    machine_reset();
    (void)as_create(&a, 1);
    (void)map_range(&a, 0x400000, PA_ANON, PG, RWU, 0);
    (void)mmu_access(&a, 0x400000, UR);
    log_reset();
    (void)unmap_range(&a, 0x400000, PG);
    CHECK(M.nlog == 1 && M.log[0].kind == TLBI_VA && wc_count(1) == 0 && M.early_free == 0);
}

static void part_c(void) {
    puts("-- Part C: driver ioremap/iounmap, DMA buffer handoff, page migration (BBM), process exit");
    machine_reset();
    as_t k, a, a2;
    (void)as_create(&k, 0);
    const uint32_t base = M.nfree;
    const uint64_t KV = 0x7F0000000000ull, UART = 0x09000000;
    CHECK(map_range(&k, KV, UART, 4 * PG, D_R | D_W, MAP_GLOBAL) == E_OK);
    xr_t kr = mmu_access(&k, KV + 0x18, ACC_W), ur = mmu_access(&k, KV + 0x18, UR);
    CHECK(kr.fault == F_NONE && kr.pa == UART + 0x18 && ur.fault == F_PERMISSION);
    touch(&k, KV, 4, ACC_R);
    log_reset();
    CHECK(unmap_range(&k, KV, 4 * PG) == 4 && M.nlog == 4 && log_count(TLBI_VA) == 4 && M.nfree == base && count_state(FR_DATA) == 0);
    CHECK(mmu_access(&k, KV, ACC_R).fault == F_TRANSLATION && tlb_audit() == 0);

    (void)as_create(&a, 1);
    const uint64_t BUF = 0x20000000;
    (void)map_range(&a, BUF, PA_ANON, 40 * PG, RWU, 0);
    touch(&a, BUF, 40, UW);
    log_reset();
    CHECK(protect_range(&a, BUF, 40 * PG, D_R) == E_OK && M.nlog == 1 && M.log[0].kind == TLBI_ASID);   /* 40 > 33 */
    CHECK(mmu_access(&a, BUF + 39 * PG, UW).fault == F_PERMISSION && tlb_audit() == 0);
    log_reset();
    CHECK(protect_range(&a, BUF, 40 * PG, D_R | D_W) == E_OK && M.nlog == 0);

    const uint64_t va = BUF + 5 * PG, oldpa = walk(&a, va).pa;
    uint64_t newpa = 0;
    poke(oldpa, 0xC0FFEE);
    (void)mmu_access(&a, va, UR);
    const uint32_t nf = M.nfree;
    log_reset();
    CHECK(migrate_page(&a, va, &newpa) == E_OK && newpa != oldpa && peek(newpa) == 0xC0FFEE);
    CHECK(M.bbm_viol == 0 && M.early_free == 0 && M.nlog == 1 && M.log[0].kind == TLBI_VA_LEAF && M.nfree == nf);
    xr_t mr = mmu_access(&a, va, UR);
    CHECK(mr.fault == F_NONE && mr.pa == newpa && state_of(oldpa) == FR_FREE && tlb_audit() == 0);
    CHECK(migrate_page(&a, BUF + 40 * PG, NULL) == E_NOENT);

    (void)map_range(&a, 0x8000000000ull, PA_ANON, 3 * PG, RWU, 0);
    (void)map_range(&a, 0x30000000, UART, PG, D_R | D_W | D_U, 0);
    touch(&a, 0x8000000000ull, 3, UR); touch(&a, 0x30000000, 1, UR);
    log_reset();
    as_destroy(&a);
    CHECK(M.nlog == 1 && M.log[0].kind == TLBI_ASID && M.log[0].asid == 1);
    CHECK(M.nfree == base && tlb_count(1) == 0 && wc_count(1) == 0 && tlb_audit() == 0 && M.early_free == 0);
    CHECK(as_create(&a2, 1) == E_OK && mmu_access(&a2, BUF, UR).fault == F_TRANSLATION);   /* 같은 ASID 재사용 */
    (void)map_range(&a2, KV, PA_ANON, PG, D_R | D_W, MAP_GLOBAL);
    touch(&a2, KV, 1, ACC_R);
    log_reset();
    as_destroy(&a2);
    CHECK(M.nlog == 1 && M.log[0].kind == TLBI_ALL && M.nfree == base);
    CHECK(M.bbm_viol == 0 && M.double_free == 0 && M.early_free == 0);
}

int main(void) {
    part_a();
    stress();
    part_b();
    part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif
