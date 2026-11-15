/* ARCH-A01 MMU 페이지 테이블 워커 시뮬레이터
 *
 * 포맷 = "ARMv8-A VMSA 4KB granule 을 단순화한 교육용 포맷". 실제 ARM 디스크립터의 비트 위치가 아니다.
 *   4 KB 페이지, 레벨당 인덱스 9 비트, 테이블 1 장 = 512 x 8 B 디스크립터 = 4 KB.
 *   levels ∈ {2,3,4} → VA 비트 = 12 + 9·levels (30 / 39 / 48). 그 위 VA 비트가 1 이면 ADDRESS_SIZE (walk 없음).
 *   마지막 레벨은 항상 L3:  4 단계 L0→L1→L2→L3,  3 단계 L1→L2→L3,  2 단계 L2→L3.
 *   디스크립터 (64 bit):
 *     bit0 V(valid)  bit1 T(1 = 다음 레벨 테이블, L3 에선 "페이지" / 0 = 블록)  bit2 R  bit3 W  bit4 X
 *     bit5 U(user)  bit6 A(access flag, 저장만)  bit7 G(global, A02 TLB 용, 저장만)  bits 12..47 출력 주소(OA)
 *     나머지 비트(8..11, 48..63)는 예약 = 0.
 *   블록(leaf at non-last level)은 L1 (1 GB) · L2 (2 MB) 만. L0 의 V=1,T=0 과 L3 의 V=1,T=0 은 잘못된 인코딩
 *   → TRANSLATION 폴트 (ARMv8 의 "L3 에서 블록 인코딩은 invalid" 규칙을 흉내 냄).
 *   권한은 leaf 에서만 검사 (계층적 APTable 없음). USER 가 U=0 → PERMISSION. KERNEL 은 U=1 데이터 R/W 가능
 *   (PAN 없음), KERNEL 이 U=1 페이지 실행 → PERMISSION (SMEP/PXN 개념). W 없는 R 은 포맷상 가능하지만
 *   W 만 있고 R 없는 조합은 map API 가 거부.
 *   폴트 보고 = {종류 NONE/ADDRESS_SIZE/TRANSLATION/PERMISSION, 감지 레벨, va, 접근},
 *   성공 = {pa, leaf 레벨, 페이지/블록 크기, 권한}, 그리고 walk_reads (읽은 디스크립터 수).
 *
 *   Part A  va_index · perm_ok · mmu_translate(워커) · map_level(매핑 설치)  — 4 단계 / 3 단계 설정
 *   Part B  split_block · unmap_level, 블록 경계(정렬·블록 vs 테이블 충돌·부분 unmap = split·2 단계 설정),
 *           폴트 4 종의 감지 레벨, walk_reads 표, 랜덤 스트레스(그림자 모델과 대조, 고정 시드 xorshift64)
 *   Part C  펌웨어/OS 레이아웃 (커널 RX / 데이터 RW-XN / 유저 text·heap·stack+가드 / MMIO / 부팅 identity map)
 *           접근 표와 기대 폴트, 1 GB 매핑의 페이지 테이블 메모리: 4 KB vs 2 MB vs 1 GB
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so    (starter.c 도 같은 명령)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_SHIFT 12
#define PAGE_SIZE  (1ull << PAGE_SHIFT)
#define IDX_BITS   9
#define ENTRIES    512u
#define PA_BITS    48
#define PT_POOL_PA 0x48000000ull   /* 테이블 페이지 풀의 물리 주소 (DRAM 안이라고 가정) */
#define SZ_2M      (1ull << 21)
#define SZ_1G      (1ull << 30)

#define D_V (1ull << 0)
#define D_T (1ull << 1)
#define D_R (1ull << 2)
#define D_W (1ull << 3)
#define D_X (1ull << 4)
#define D_U (1ull << 5)
#define D_A (1ull << 6)
#define D_G (1ull << 7)
#define OA_MASK   0x0000FFFFFFFFF000ull
#define PERM_MASK (D_R | D_W | D_X | D_U | D_G)      /* map() 이 받는 권한 비트 */
#define LEAF_ATTR (PERM_MASK | D_A)                  /* leaf 에 복사되는 속성 (split 때 그대로 물려줌) */
#define RSVD_MASK (~(OA_MASK | 0xFFull))

typedef enum { F_NONE, F_ADDRESS_SIZE, F_TRANSLATION, F_PERMISSION } fault_t;
static const char *const FAULT_NAME[4] = {"NONE", "ADDRESS_SIZE", "TRANSLATION", "PERMISSION"};
typedef enum { ACC_R, ACC_W, ACC_X } acc_kind_t;
typedef struct { acc_kind_t kind; bool user; } access_t;

typedef struct {
    fault_t  fault;       /* F_NONE = 성공 */
    int      level;       /* 폴트: 감지한 레벨 / 성공: leaf 레벨 */
    uint64_t va;
    access_t acc;
    uint64_t pa;          /* 성공일 때만 */
    uint64_t size;        /* leaf 를 찾았으면 4K / 2M / 1G (PERMISSION 에도 채움) */
    uint64_t perms;       /* leaf 의 R|W|X|U|G (PERMISSION 에도 채움) */
    int      walk_reads;  /* 이번 변환에서 읽은 디스크립터 수 */
} xlate_t;

enum { MMU_OK = 0, MMU_EALIGN = -1, MMU_ERANGE = -2, MMU_EPERM = -3, MMU_EEXIST = -4, MMU_ENOMEM = -5, MMU_ELEVEL = -6 };
#define MAP_PAGES 1u                                 /* 블록 금지, 4 KB 페이지만 */

typedef struct {
    int      levels, va_bits;
    uint64_t ttbr;                                   /* 루트 테이블 PA */
    uint8_t *pmem;  uint32_t nframes;                /* 테이블 풀 = 시뮬레이션 물리 메모리 */
    uint32_t *freestk; uint32_t nfree;               /* 프레임 할당기 (스택) */
    uint32_t tables, tables_peak, splits;
    uint64_t desc_reads, desc_writes, bad_reads;
    int      trace, ntr; uint64_t tr_pa[8], tr_val[8];
} mmu_t;

static int start_level(const mmu_t *m) { return 4 - m->levels; }
static unsigned level_shift(int lv) { return PAGE_SHIFT + IDX_BITS * (unsigned)(3 - lv); }
static uint64_t level_size(int lv) { return 1ull << level_shift(lv); }
static bool block_allowed(int lv) { return lv == 1 || lv == 2; }

/* ---------- 시뮬레이션 물리 메모리 + 프레임 할당기 (제공) ---------- */
static bool pool_ok(const mmu_t *m, uint64_t pa) {
    return pa >= PT_POOL_PA && pa < PT_POOL_PA + (uint64_t)m->nframes * PAGE_SIZE && (pa & 7) == 0;
}
static uint64_t peek(const mmu_t *m, uint64_t pa) {          /* 통계에 안 잡히는 읽기 (검사·덤프용) */
    uint64_t d = 0;
    if (pool_ok(m, pa)) memcpy(&d, m->pmem + (pa - PT_POOL_PA), sizeof d);
    return d;
}
static uint64_t rd_desc(mmu_t *m, uint64_t pa) {             /* 워커·매퍼의 디스크립터 읽기 (센다) */
    m->desc_reads++;
    if (!pool_ok(m, pa)) { m->bad_reads++; return 0; }
    uint64_t d = peek(m, pa);
    if (m->trace && m->ntr < 8) { m->tr_pa[m->ntr] = pa; m->tr_val[m->ntr] = d; m->ntr++; }
    return d;
}
static void wr_desc(mmu_t *m, uint64_t pa, uint64_t d) {
    m->desc_writes++;
    if (pool_ok(m, pa)) memcpy(m->pmem + (pa - PT_POOL_PA), &d, sizeof d);
}
static uint64_t alloc_table(mmu_t *m) {                      /* 0 = 메모리 없음 (풀은 0 이 아닌 PA 에 있다) */
    if (m->nfree == 0) return 0;
    uint32_t f = m->freestk[--m->nfree];
    memset(m->pmem + (size_t)f * PAGE_SIZE, 0, PAGE_SIZE);
    if (++m->tables > m->tables_peak) m->tables_peak = m->tables;
    return PT_POOL_PA + (uint64_t)f * PAGE_SIZE;
}
static void free_table(mmu_t *m, uint64_t pa) {
    m->freestk[m->nfree++] = (uint32_t)((pa - PT_POOL_PA) / PAGE_SIZE);
    m->tables--;
}
static bool table_empty(const mmu_t *m, uint64_t table) {
    for (unsigned i = 0; i < ENTRIES; i++) if (peek(m, table + 8u * i) & D_V) return false;
    return true;
}
static int mmu_init(mmu_t *m, int levels, uint32_t nframes) {
    memset(m, 0, sizeof *m);
    if (levels < 2 || levels > 4 || nframes == 0) return MMU_ELEVEL;
    m->pmem = calloc(nframes, PAGE_SIZE);
    m->freestk = malloc(nframes * sizeof *m->freestk);
    if (!m->pmem || !m->freestk) { free(m->pmem); free(m->freestk); return MMU_ENOMEM; }
    m->levels = levels; m->va_bits = PAGE_SHIFT + IDX_BITS * levels; m->nframes = nframes;
    for (uint32_t i = 0; i < nframes; i++) m->freestk[i] = nframes - 1 - i;   /* 프레임 0 부터 나간다 */
    m->nfree = nframes;
    m->ttbr = alloc_table(m);
    return MMU_OK;
}
static void mmu_destroy(mmu_t *m) { free(m->pmem); free(m->freestk); memset(m, 0, sizeof *m); }

/* ======================= Part A: 워커 ======================= */

/* 레벨 lv 의 테이블 인덱스 (9 비트): L0 = VA[47:39], L1 = VA[38:30], L2 = VA[29:21], L3 = VA[20:12]. */
static unsigned va_index(const mmu_t *m, uint64_t va, int lv) {
    /* TODO(A-1): level_shift(lv) 만큼 오른쪽으로 밀고 하위 IDX_BITS 비트만 남긴다.
     * (레벨 번호가 절대값이라 설정(levels)과 무관 — 2 단계의 루트 L2 도 VA[29:21] 이다.) */
    (void)m; (void)va; (void)lv;
    return 0;
}

/* leaf 디스크립터 d 가 접근 a 를 허용하나? (테이블 디스크립터의 비트는 보지 않는다 = APTable 없음)
 *   user   접근: U=1 이어야 하고, 종류별로 R / W / X 비트.
 *   kernel 접근: U 와 무관하게 R / W 는 비트대로 (PAN 없음). 단 U=1 페이지의 실행은 금지 (SMEP/PXN 개념). */
static bool perm_ok(uint64_t d, access_t a) {
    /* TODO(A-2): 위 규칙. X 는 R 과 독립이다 (이 모델에선 실행에 R 이 필요 없다). */
    (void)d; (void)a;
    return false;
}

/* VA → PA. 디스크립터는 반드시 rd_desc() 로 읽는다 (하네스가 walk_reads 가 정직한지 센다). */
static xlate_t mmu_translate(mmu_t *m, uint64_t va, access_t acc) {
    xlate_t r = { F_NONE, start_level(m), va, acc, 0, 0, 0, 0 };
    /* TODO(A-3): 워커.
     *   1) va >> va_bits 가 0 이 아니면 ADDRESS_SIZE (level = 시작 레벨, walk_reads = 0).
     *   2) table = TTBR. lv = 시작 레벨 .. 3: d = rd_desc(table + 8·va_index), walk_reads++, level = lv.
     *        V=0                   → TRANSLATION
     *        lv<3 이고 T=1         → table = d & OA_MASK, 다음 레벨
     *        L0 블록 / L3 의 T=0   → TRANSLATION (잘못된 인코딩)
     *        그 외 = leaf (L1·L2 블록, L3 페이지): size = level_size(lv), perms = d & PERM_MASK;
     *        perm_ok 가 거부하면 PERMISSION, 아니면 pa = (OA 를 size 경계로 내림) | (va 의 하위 비트). */
    (void)m;
    return r;
}

/* ======================= Part A: 매핑 설치 ======================= */

/* [va, va+size) → [pa, ...) 를 레벨 lv 테이블(table = 그 PA) 아래에 설치한다. attrs = 권한|A.
 * 범위가 비어 있다는 건 mmu_map 이 미리 확인했다. 테이블 할당 실패 → MMU_ENOMEM (mmu_map 이 롤백). */
static int map_level(mmu_t *m, uint64_t table, int lv, uint64_t va, uint64_t pa, uint64_t size,
                     uint64_t attrs, unsigned flags) {
    /* TODO(A-4): size 가 0 이 될 때까지 엔트리 단위로:
     *   esz = level_size(lv), chunk = min(size, 이 엔트리 끝까지), ea = table + 8·va_index(va, lv), d = rd_desc(ea)
     *   L3                                                  → 페이지 V|T|attrs|pa
     *   블록 허용 레벨, !MAP_PAGES, chunk == esz, va·pa 가 esz 정렬, d 무효 → 블록 V|attrs|pa (T=0)
     *   그 외 → d 무효면 alloc_table + 테이블 디스크립터 V|T|t 기록, d 가 블록이면 MMU_EEXIST,
     *          map_level(다음 테이블, lv+1, va, pa, chunk) 재귀
     *   va += chunk, pa += chunk, size -= chunk */
    (void)m; (void)table; (void)lv; (void)va; (void)pa; (void)size; (void)attrs; (void)flags;
    return MMU_OK;
}

/* ======================= Part B: split 과 unmap ======================= */

/* ea 에 있는 레벨 lv 블록 디스크립터를 "같은 매핑을 512 개로 나눈 다음 레벨 테이블" 로 바꾼다.
 * 각 하위 엔트리 = 같은 속성(LEAF_ATTR), OA = 블록 base + i·level_size(lv+1). 하위가 L3 면 T=1 (페이지). */
static int split_block(mmu_t *m, uint64_t ea, int lv) {
    m->splits++;
    /* TODO(B-1): 새 테이블 할당(실패 → MMU_ENOMEM) → 512 엔트리 채우기 → ea 를 테이블 디스크립터로 교체.
     * (실제 ARMv8 에서는 블록 → 테이블 교체에 break-before-make 가 필요하다: 무효화 → TLBI → DSB → 새 값.) */
    (void)ea; (void)lv;
    return MMU_ENOMEM;
}

/* [va, va+size) 의 매핑을 지운다. 매핑 안 된 부분은 그냥 건너뛴다 (munmap 처럼 멱등).
 * 블록을 일부만 덮으면 split 한 뒤 내려간다. 하위 테이블이 비면 그 프레임을 반납하고 엔트리를 0 으로. */
static int unmap_level(mmu_t *m, uint64_t table, int lv, uint64_t va, uint64_t size) {
    /* TODO(B-2): map_level 과 같은 chunk 순회.
     *   d 무효 → 건너뜀
     *   leaf (L3, 또는 T=0 블록) 이고 chunk == esz → 엔트리 0
     *   leaf 인데 일부만 → split_block 후 (d 다시 읽고) 테이블처럼 처리
     *   테이블 → unmap_level(하위, lv+1, va, chunk) 후 하위가 비었으면 엔트리 0 + free_table */
    (void)m; (void)table; (void)lv; (void)va; (void)size;
    return MMU_OK;
}

/* ======================= 매핑 API (제공) ======================= */

static bool range_free(const mmu_t *m, uint64_t table, int lv, uint64_t va, uint64_t size) {
    while (size) {
        uint64_t esz = level_size(lv), chunk = esz - (va & (esz - 1));
        if (chunk > size) chunk = size;
        uint64_t d = peek(m, table + 8u * va_index(m, va, lv));
        if (d & D_V) {
            if (lv == 3 || !(d & D_T)) return false;
            if (!range_free(m, d & OA_MASK, lv + 1, va, chunk)) return false;
        }
        va += chunk; size -= chunk;
    }
    return true;
}
static int check_range(const mmu_t *m, uint64_t va, uint64_t size) {
    if (size == 0 || ((va | size) & (PAGE_SIZE - 1))) return MMU_EALIGN;
    uint64_t lim = 1ull << m->va_bits;
    if (va >= lim || size > lim - va) return MMU_ERANGE;
    return MMU_OK;
}
static int mmu_unmap(mmu_t *m, uint64_t va, uint64_t size) {
    int rc = check_range(m, va, size);
    return rc ? rc : unmap_level(m, m->ttbr, start_level(m), va, size);
}
/* 가장 큰 정렬된 블록을 자동으로 고른다 (flags & MAP_PAGES 면 4 KB 만). 범위가 일부라도 매핑돼 있으면 EEXIST.
 * W 는 R 과 함께만 (W-only 거부). perms = 0 (접근 불가 매핑, PROT_NONE) 은 허용. */
static int mmu_map(mmu_t *m, uint64_t va, uint64_t pa, uint64_t size, uint64_t perms, unsigned flags) {
    int rc = check_range(m, va, size);
    if (rc) return rc;
    if (pa & (PAGE_SIZE - 1)) return MMU_EALIGN;
    if (pa >= (1ull << PA_BITS) || size > (1ull << PA_BITS) - pa) return MMU_ERANGE;
    if ((perms & ~PERM_MASK) || ((perms & D_W) && !(perms & D_R))) return MMU_EPERM;
    if (!range_free(m, m->ttbr, start_level(m), va, size)) return MMU_EEXIST;
    rc = map_level(m, m->ttbr, start_level(m), va, pa, size, perms | D_A, flags);
    if (rc) (void)mmu_unmap(m, va, size);        /* 롤백: 원래 비어 있던 범위라 지우면 원상복구 */
    return rc;
}
/* 블록 하나를 명시적으로: 레벨이 블록 불가거나 이 설정에 없으면 ELEVEL, va/pa 가 블록 크기로 정렬 안 되면 EALIGN. */
static int mmu_map_block(mmu_t *m, uint64_t va, uint64_t pa, int lv, uint64_t perms) {
    if (!block_allowed(lv) || lv < start_level(m)) return MMU_ELEVEL;
    uint64_t sz = level_size(lv);
    if ((va | pa) & (sz - 1)) return MMU_EALIGN;
    return mmu_map(m, va, pa, sz, perms, 0);
}

/* 트리 전체 검사: 테이블 수 == 카운터, 빈 비루트 테이블 없음, L0 블록·L3 T=0·미정렬 블록·W-only·예약 비트 없음. */
static char g_audit_msg[128];
static int audit_level(const mmu_t *m, uint64_t t, int lv, uint32_t *ntab) {
    int bad = 0; unsigned valid = 0;
    (*ntab)++;
    for (unsigned i = 0; i < ENTRIES; i++) {
        uint64_t d = peek(m, t + 8u * i);
        if (!(d & D_V)) continue;
        valid++;
        const char *why = NULL;
        if (d & RSVD_MASK) why = "reserved bits set";
        else if (lv < 3 && (d & D_T)) {
            if (!pool_ok(m, d & OA_MASK)) why = "table pointer outside pool";
            else bad += audit_level(m, d & OA_MASK, lv + 1, ntab);
        }
        else if (lv == 0) why = "block at L0";
        else if (lv == 3 && !(d & D_T)) why = "L3 entry with T=0";
        else if (d & OA_MASK & (level_size(lv) - 1)) why = "misaligned block OA";
        else if ((d & D_W) && !(d & D_R)) why = "W without R";
        if (why) { bad++; snprintf(g_audit_msg, sizeof g_audit_msg, "L%d[%u]: %s", lv, i, why); }
    }
    if (valid == 0 && lv != start_level(m)) { bad++; snprintf(g_audit_msg, sizeof g_audit_msg, "empty L%d table", lv); }
    return bad;
}
static int pt_audit(const mmu_t *m) {
    uint32_t n = 0;
    int bad = audit_level(m, m->ttbr, start_level(m), &n);
    if (n != m->tables) { bad++; snprintf(g_audit_msg, sizeof g_audit_msg, "tables %u != counter %u", n, m->tables); }
    return bad;
}
static void count_leaves(const mmu_t *m, uint64_t t, int lv, uint64_t cnt[4]) {
    for (unsigned i = 0; i < ENTRIES; i++) {
        uint64_t d = peek(m, t + 8u * i);
        if (!(d & D_V)) continue;
        if (lv < 3 && (d & D_T)) count_leaves(m, d & OA_MASK, lv + 1, cnt);
        else cnt[lv]++;
    }
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define ULL(x) ((unsigned long long)(x))

static long g_dishonest;                             /* walk_reads != 실제로 rd_desc 한 횟수 */
static xlate_t xl(mmu_t *m, uint64_t va, acc_kind_t k, bool user) {
    access_t a = { k, user };
    uint64_t before = m->desc_reads;
    xlate_t r = mmu_translate(m, va, a);
    if (r.walk_reads < 0 || (uint64_t)r.walk_reads != m->desc_reads - before) g_dishonest++;
    return r;
}
static bool is_ok(xlate_t r, uint64_t pa, int lv, int reads) {
    return r.fault == F_NONE && r.pa == pa && r.level == lv && r.walk_reads == reads && r.size == level_size(lv);
}
static bool is_fault(xlate_t r, fault_t f, int lv, int reads) {
    return r.fault == f && r.level == lv && r.walk_reads == reads;
}
/* 한글은 UTF-8 3 바이트 · 화면 폭 2 칸 → printf %-Ns 대신 폭을 세어 채운다 */
static void put_padded(const char *s, int width) {
    int w = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p < 0x80) w++;
        else if (*p >= 0xE0 && *p < 0xF0) w += 2;          /* 3 바이트 문자의 첫 바이트 (한글) */
    }
    fputs(s, stdout);
    for (; w < width; w++) putchar(' ');
}
static const char *size_str(uint64_t s) {
    return s == SZ_1G ? "1G" : s == SZ_2M ? "2M" : s == PAGE_SIZE ? "4K" : "?";
}
static void perm_str(uint64_t p, char b[6]) {
    b[0] = (p & D_R) ? 'r' : '-'; b[1] = (p & D_W) ? 'w' : '-'; b[2] = (p & D_X) ? 'x' : '-';
    b[3] = (p & D_U) ? 'u' : '-'; b[4] = (p & D_G) ? 'g' : '-'; b[5] = '\0';
}
static const char *acc_str(access_t a) {
    static const char *const t[2][3] = { {"K:R", "K:W", "K:X"}, {"U:R", "U:W", "U:X"} };
    return t[a.user ? 1 : 0][(unsigned)a.kind % 3];
}
static void fmt_x(const xlate_t *r, char *buf, size_t n) {
    char p[6];
    if (r->fault == F_NONE) {
        perm_str(r->perms, p);
        snprintf(buf, n, "ok  pa=0x%llx L%d %s %s reads=%d", ULL(r->pa), r->level, size_str(r->size), p, r->walk_reads);
    } else {
        snprintf(buf, n, "%s@L%d reads=%d", FAULT_NAME[(unsigned)r->fault & 3], r->level, r->walk_reads);
    }
}
static const char *desc_kind(uint64_t d, int lv, char *buf, size_t n) {
    if (!(d & D_V)) snprintf(buf, n, "invalid");
    else if (lv < 3 && (d & D_T)) snprintf(buf, n, "table -> 0x%llx", ULL(d & OA_MASK));
    else if (lv == 0 || (lv == 3 && !(d & D_T))) snprintf(buf, n, "INVALID ENCODING");
    else { char p[6]; perm_str(d, p); snprintf(buf, n, "%s %s oa=0x%llx", lv == 3 ? "page" : "block", p, ULL(d & OA_MASK)); }
    return buf;
}
static void print_walk(mmu_t *m, uint64_t va, acc_kind_t k, bool user) {
    m->trace = 1; m->ntr = 0;
    xlate_t r = xl(m, va, k, user);
    m->trace = 0;
    char b[96], kb[64];
    printf("  walk %d-level va=0x%llx %s\n", m->levels, ULL(va), acc_str(r.acc));
    for (int i = 0; i < m->ntr; i++) {
        uint64_t tb = m->tr_pa[i] & ~(PAGE_SIZE - 1);
        printf("    L%d  table 0x%llx [%3u] = 0x%016llx  %s\n", start_level(m) + i, ULL(tb),
               (unsigned)((m->tr_pa[i] - tb) / 8), ULL(m->tr_val[i]), desc_kind(m->tr_val[i], start_level(m) + i, kb, sizeof kb));
    }
    fmt_x(&r, b, sizeof b);
    printf("    => %s\n", b);
}
/* 권한 오라클: perm_ok 와 다른 모양으로 쓴 같은 규칙 ("필요 비트" 와 "금지 비트") */
static bool oracle_allows(uint64_t p, access_t a) {
    uint64_t need = a.kind == ACC_R ? D_R : a.kind == ACC_W ? D_W : D_X;
    uint64_t forbid = 0;
    if (a.user) need |= D_U; else if (a.kind == ACC_X) forbid = D_U;
    return (p & need) == need && (p & forbid) == 0;
}

/* ------------------------------ Part A ------------------------------ */
static void part_a(void) {
    printf("== Part A: 워커 + 매핑 API (4 단계 / 3 단계) ==\n");
    mmu_t m4, m3;
    CHECK(mmu_init(&m4, 4, 1024) == MMU_OK && m4.va_bits == 48 && m4.tables == 1);
    CHECK(mmu_init(&m3, 3, 1024) == MMU_OK && m3.va_bits == 39 && m3.tables == 1);

    /* A-0 하네스 자체 점검: 프레임 할당기 + 디스크립터 읽기/쓰기 */
    uint64_t t = alloc_table(&m4);
    wr_desc(&m4, t + 8, D_V | 0x1234000);
    CHECK(t == PT_POOL_PA + PAGE_SIZE && rd_desc(&m4, t + 8) == (D_V | 0x1234000) && !table_empty(&m4, t));
    wr_desc(&m4, t + 8, 0);
    CHECK(table_empty(&m4, t));
    free_table(&m4, t);
    CHECK(m4.tables == 1 && m4.nfree == 1023);

    /* A-1 인덱스 분해 */
    const uint64_t VA1 = 0x123456789ABCull;
    printf("  VA 0x%llx -> L0 %u  L1 %u  L2 %u  L3 %u  offset 0x%llx\n", ULL(VA1), va_index(&m4, VA1, 0),
           va_index(&m4, VA1, 1), va_index(&m4, VA1, 2), va_index(&m4, VA1, 3), ULL(VA1 & 0xFFF));
    int badidx = 0;
    for (int lv = 0; lv <= 3; lv++) if (va_index(&m4, VA1, lv) != (VA1 / level_size(lv)) % ENTRIES) badidx++;
    CHECK(badidx == 0);
    CHECK(va_index(&m4, VA1, 0) == 36 && va_index(&m4, VA1, 1) == 209 && va_index(&m4, VA1, 2) == 179 &&
          va_index(&m4, VA1, 3) == 393);

    /* A-2 4 단계: 페이지 1 장 */
    const uint64_t PVA = 0x123456789000ull, PPA = 0x800005000ull;
    CHECK(mmu_map(&m4, PVA, PPA, PAGE_SIZE, D_R | D_W | D_U, 0) == MMU_OK);
    CHECK(m4.tables == 4);                                     /* 루트 + L1 + L2 + L3 */
    print_walk(&m4, PVA + 0xABC, ACC_R, true);
    xlate_t r = xl(&m4, PVA + 0xABC, ACC_R, true);
    CHECK(is_ok(r, PPA + 0xABC, 3, 4) && r.perms == (D_R | D_W | D_U));
    CHECK(is_ok(xl(&m4, PVA + 0xFF8, ACC_W, true), PPA + 0xFF8, 3, 4));
    CHECK(is_ok(xl(&m4, PVA, ACC_R, false), PPA, 3, 4));         /* 커널은 U=1 데이터를 읽을 수 있다 (PAN 없음) */
    CHECK(is_fault(xl(&m4, PVA, ACC_X, true), F_PERMISSION, 3, 4));

    /* A-3 블록: 1 GB (L1 블록) 와 2 MB (L2 블록) */
    CHECK(mmu_map(&m4, 0x8000000000ull, 0x100000000ull, SZ_1G, D_R | D_X, 0) == MMU_OK);   /* L0[1] */
    CHECK(m4.tables == 5);                                     /* L1 테이블 1 장만 늘었다 */
    print_walk(&m4, 0x8012345678ull, ACC_X, false);
    CHECK(is_ok(xl(&m4, 0x8012345678ull, ACC_X, false), 0x112345678ull, 1, 2));
    CHECK(mmu_map(&m4, 0x40200000ull, 0x80200000ull, SZ_2M, D_R, 0) == MMU_OK);
    CHECK(m4.tables == 7);                                     /* L0[0] 아래 L1 + L2 */
    CHECK(is_ok(xl(&m4, 0x403FFFFFull, ACC_R, false), 0x803FFFFFull, 2, 3));
    uint64_t c4[4] = {0};
    count_leaves(&m4, m4.ttbr, 0, c4);
    printf("  4-level: tables=%u  leaves L1(1G)=%llu L2(2M)=%llu L3(4K)=%llu\n", m4.tables, ULL(c4[1]), ULL(c4[2]), ULL(c4[3]));
    CHECK(c4[1] == 1 && c4[2] == 1 && c4[3] == 1);

    /* A-4 자동 블록 선택: [1 MB, 7 MB) = 1 MB 페이지 + 2 MB 블록 2 개 + 1 MB 페이지 (3 단계) */
    CHECK(mmu_map(&m3, 0x100000ull, 0x40100000ull, 6ull << 20, D_R | D_W, 0) == MMU_OK);
    uint64_t c3[4] = {0};
    count_leaves(&m3, m3.ttbr, 1, c3);
    printf("  3-level map [0x100000, 0x700000): tables=%u  2M blocks=%llu  4K pages=%llu\n", m3.tables, ULL(c3[2]), ULL(c3[3]));
    CHECK(m3.tables == 4 && c3[2] == 2 && c3[3] == 512);       /* 루트 + L2 + L3 두 장 */
    int badpg = 0;
    for (uint64_t va = 0x100000; va < 0x700000; va += PAGE_SIZE) {
        uint64_t off = (va >> 9) & 0xFF8, v = va + off;
        int lv = (va >= 0x200000 && va < 0x600000) ? 2 : 3;
        if (!is_ok(xl(&m3, v, ACC_W, false), v - 0x100000 + 0x40100000, lv, lv)) badpg++;
    }
    CHECK(badpg == 0);
    print_walk(&m3, 0x1FFFF8ull, ACC_R, false);
    print_walk(&m3, 0x200008ull, ACC_R, false);
    /* 3 단계의 1 GB 블록 = 루트(L1) 엔트리 자체 → 읽기 1 번 */
    CHECK(mmu_map(&m3, 0x4000000000ull, 0xC0000000ull, SZ_1G, D_R | D_W | D_G, 0) == MMU_OK);
    CHECK(m3.tables == 4);
    CHECK(is_ok(xl(&m3, 0x4000000010ull, ACC_W, false), 0xC0000010ull, 1, 1));

    /* A-5 perm_ok 전수: R W X U 16 조합 x {R,W,X} x {kernel,user} = 96 경우 */
    int badperm = 0;
    printf("  perm_ok (o = allowed)    K:R K:W K:X  U:R U:W U:X\n");
    for (unsigned p = 0; p < 16; p++) {
        uint64_t d = D_V | D_T | ((uint64_t)p << 2);
        char ps[6], row[32]; int k = 0;
        perm_str(d, ps);
        for (int u = 0; u < 2; u++) {
            for (int kind = 0; kind < 3; kind++) {
                access_t a = { (acc_kind_t)kind, u == 1 };
                bool got = perm_ok(d, a);
                if (got != oracle_allows(d, a)) badperm++;
                row[k++] = ' '; row[k++] = ' '; row[k++] = ' '; row[k++] = got ? 'o' : '.';
            }
            row[k++] = ' ';
        }
        row[k] = '\0';
        if ((p & 3) != 2) printf("    %.4s                %s\n", ps, row);   /* W-only(-w--) 행은 map 이 거부하므로 생략 */
    }
    CHECK(badperm == 0);
    CHECK(g_dishonest == 0);
    mmu_destroy(&m4);
    mmu_destroy(&m3);
}

/* ------------------------------ Part B ------------------------------ */
typedef struct { long ops, maps, eexist, unmaps, probes, perm, trans, mism, audit_bad; long hit[4]; uint32_t peak, splits, final_tables; } stress_t;
static uint64_t g_rng;
static uint64_t rnd(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static uint64_t rndn(uint64_t n) { return rnd() % n; }

static void stress(int levels, uint64_t base, uint64_t wsize, int nops, uint64_t seed, stress_t *st) {
    static const uint64_t PSET[6] = { 0, D_R, D_R | D_W, D_R | D_X, D_X, D_R | D_W | D_X };
    mmu_t m;
    memset(st, 0, sizeof *st);
    if (mmu_init(&m, levels, 4096) != MMU_OK) { st->mism = 1; return; }
    size_t np = (size_t)(wsize >> PAGE_SHIFT);
    uint64_t *spa = malloc(np * sizeof *spa);
    uint8_t *sp = malloc(np);                              /* 0xFF = 매핑 없음, 아니면 perms >> 2 */
    if (!spa || !sp) { free(spa); free(sp); mmu_destroy(&m); st->mism = 1; return; }
    memset(sp, 0xFF, np);
    g_rng = seed;
    const int sl = start_level(&m);
    uint64_t lva = base, lsz = PAGE_SIZE;
    for (int op = 0; op < nops; op++) {
        uint64_t r = rndn(100);
        if (r < 3) {                                           /* 가끔 창 전체를 비운다 → 1 GB 매핑이 들어갈 자리 */
            if (mmu_unmap(&m, base, wsize) != MMU_OK || m.tables != 1) st->mism++;
            memset(sp, 0xFF, np);
            st->unmaps++;
            lva = base; lsz = wsize;
        } else if (r < 48) {                                   /* map */
            uint64_t g = rndn(10), gran = g < 6 ? PAGE_SIZE : g < 9 ? SZ_2M : SZ_1G;
            uint64_t n = gran == PAGE_SIZE ? 1 + rndn(96) : gran == SZ_2M ? 1 + rndn(3) : 1;
            uint64_t size = gran * n; if (size > wsize) size = wsize;
            uint64_t va = base + rndn((wsize - size) / gran + 1) * gran;
            uint64_t pa = rndn(1u << 16) * gran, mis = rndn(8);
            if (mis == 0) pa += PAGE_SIZE; else if (mis == 1 && gran == SZ_1G) pa += SZ_2M;
            uint64_t perms = PSET[rndn(6)] | (rndn(2) ? D_U : 0) | (rndn(4) == 0 ? D_G : 0);
            unsigned flags = (size <= (8ull << 20) && rndn(6) == 0) ? MAP_PAGES : 0;
            size_t i0 = (size_t)((va - base) >> PAGE_SHIFT), cnt = (size_t)(size >> PAGE_SHIFT);
            bool fr = true;
            for (size_t i = 0; i < cnt && fr; i++) if (sp[i0 + i] != 0xFF) fr = false;
            int rc = mmu_map(&m, va, pa, size, perms, flags);
            if (rc != (fr ? MMU_OK : MMU_EEXIST)) st->mism++;
            if (rc == MMU_OK) {
                st->maps++;
                for (size_t i = 0; i < cnt; i++) { spa[i0 + i] = pa + ((uint64_t)i << PAGE_SHIFT); sp[i0 + i] = (uint8_t)(perms >> 2); }
            } else if (rc == MMU_EEXIST) st->eexist++;
            lva = va; lsz = size;
        } else if (r < 80) {                                   /* unmap (블록 일부도) */
            uint64_t va, size;
            if (rndn(10) < 7) { va = base + rndn(np) * PAGE_SIZE; size = (1 + rndn(1024)) * PAGE_SIZE; }
            else { va = base + rndn(wsize / SZ_2M) * SZ_2M; size = (1 + rndn(4)) * SZ_2M; }
            if (size > base + wsize - va) size = base + wsize - va;
            if (mmu_unmap(&m, va, size) != MMU_OK) st->mism++;
            st->unmaps++;
            memset(sp + ((va - base) >> PAGE_SHIFT), 0xFF, (size_t)(size >> PAGE_SHIFT));
            lva = va; lsz = size;
        }
        st->ops++;
        for (int k = 0; k < 24; k++) {                         /* 프로브: 절반은 방금 건드린 범위 */
            uint64_t va = rndn(2) ? lva + rndn(lsz) : base + rndn(wsize);
            access_t a = { (acc_kind_t)rndn(3), rndn(2) == 1 };
            xlate_t x = xl(&m, va, a.kind, a.user);
            size_t i = (size_t)((va - base) >> PAGE_SHIFT);
            bool lvok = x.level >= sl && x.level <= 3 && x.walk_reads == x.level - sl + 1;
            bool good;
            st->probes++;
            if (sp[i] == 0xFF) {
                good = x.fault == F_TRANSLATION && lvok;
                st->trans++;
            } else {
                uint64_t perms = (uint64_t)sp[i] << 2;
                bool allow = oracle_allows(perms, a);
                good = lvok && x.fault == (allow ? F_NONE : F_PERMISSION) && x.perms == perms &&
                       x.size == level_size(x.level) && (!allow || x.pa == spa[i] + (va & (PAGE_SIZE - 1)));
                if (good && allow) st->hit[x.level]++;
                if (!allow) st->perm++;
            }
            if (!good) st->mism++;
        }
        if (op % 50 == 49 && pt_audit(&m)) st->audit_bad++;
    }
    st->peak = m.tables_peak;
    if (mmu_unmap(&m, base, wsize) != MMU_OK) st->mism++;      /* 전부 지우면 루트만 남아야 한다 */
    if (pt_audit(&m)) st->audit_bad++;
    st->final_tables = m.tables;
    st->splits = m.splits;
    free(spa); free(sp);
    mmu_destroy(&m);
}

static void part_b(void) {
    printf("== Part B: 폴트 4 종 · 블록 경계 · split · 랜덤 스트레스 ==\n");
    mmu_t m;
    char b[96];

    /* B-1 ADDRESS_SIZE: VA 비트 위가 0 이 아니면 walk 없이 */
    for (int L = 4; L >= 2; L--) {
        CHECK(mmu_init(&m, L, 64) == MMU_OK);
        uint64_t lim = 1ull << m.va_bits;
        xlate_t r1 = xl(&m, lim, ACC_R, false), r2 = xl(&m, lim - 8, ACC_R, false);
        xlate_t r3 = xl(&m, 0xFFFF800000000000ull, ACC_R, false);
        CHECK(is_fault(r1, F_ADDRESS_SIZE, 4 - L, 0) && is_fault(r3, F_ADDRESS_SIZE, 4 - L, 0));
        CHECK(is_fault(r2, F_TRANSLATION, 4 - L, 1));
        fmt_x(&r1, b, sizeof b);
        printf("  %d-level (VA %d bits): va=0x%llx -> %s\n", L, m.va_bits, ULL(lim), b);
        mmu_destroy(&m);
    }

    /* B-2 폴트 레벨 사다리 (4 단계, 페이지 1 장 R|U) */
    CHECK(mmu_init(&m, 4, 64) == MMU_OK);
    const uint64_t P = 0x123456789000ull;
    CHECK(mmu_map(&m, P, 0x9000000ull, PAGE_SIZE, D_R | D_U, 0) == MMU_OK);
    struct { const char *what; uint64_t va; acc_kind_t k; fault_t f; int lv, reads; } lad[] = {
        { "다른 L0 엔트리 (VA^bit39)", P ^ (1ull << 39), ACC_R, F_TRANSLATION, 0, 1 },
        { "같은 L0, 다른 L1 (^bit30)", P ^ (1ull << 30), ACC_R, F_TRANSLATION, 1, 2 },
        { "같은 L1, 다른 L2 (^bit21)", P ^ (1ull << 21), ACC_R, F_TRANSLATION, 2, 3 },
        { "같은 L2, 다른 L3 (^bit12)", P ^ (1ull << 12), ACC_R, F_TRANSLATION, 3, 4 },
        { "매핑된 페이지에 쓰기",      P + 8,            ACC_W, F_PERMISSION,  3, 4 },
        { "매핑된 페이지 읽기",        P + 8,            ACC_R, F_NONE,        3, 4 },
        { "VA[48] = 1",               P | (1ull << 48), ACC_R, F_ADDRESS_SIZE, 0, 0 },
    };
    for (size_t i = 0; i < sizeof lad / sizeof lad[0]; i++) {
        xlate_t r = xl(&m, lad[i].va, lad[i].k, true);
        fmt_x(&r, b, sizeof b);
        printf("  "); put_padded(lad[i].what, 28);
        printf(" U:%c 0x%013llx -> %s\n", "RWX"[lad[i].k], ULL(lad[i].va), b);
        CHECK(is_fault(r, lad[i].f, lad[i].lv, lad[i].reads));
    }
    mmu_destroy(&m);

    /* B-3 블록 API 경계 · 파라미터 오류 · 충돌 · ENOMEM 롤백 (4 단계) */
    CHECK(mmu_init(&m, 4, 64) == MMU_OK);
    CHECK(mmu_map_block(&m, 0x200000, 0x40201000, 2, D_R) == MMU_EALIGN);   /* pa 가 2 MB 정렬 아님 */
    CHECK(mmu_map_block(&m, 0x201000, 0x40200000, 2, D_R) == MMU_EALIGN);   /* va 가 2 MB 정렬 아님 */
    CHECK(mmu_map_block(&m, 0, 0, 0, D_R) == MMU_ELEVEL);                   /* L0 블록 없음 */
    CHECK(mmu_map_block(&m, 0, 0, 3, D_R) == MMU_ELEVEL);                   /* L3 는 블록이 아니라 페이지 */
    CHECK(mmu_map(&m, 0x200000, 0x40201000, SZ_2M, D_R, 0) == MMU_OK);      /* 자동 선택: pa 미정렬 → 페이지 512 장 */
    uint64_t c[4] = {0};
    count_leaves(&m, m.ttbr, 0, c);
    CHECK(c[2] == 0 && c[3] == 512 && m.tables == 4);
    CHECK(is_ok(xl(&m, 0x200000, ACC_R, false), 0x40201000, 3, 4));
    CHECK(mmu_unmap(&m, 0x200000, SZ_2M) == MMU_OK && m.tables == 1);       /* 빈 테이블 반납 */
    CHECK(mmu_map(&m, 0x1001, 0, PAGE_SIZE, D_R, 0) == MMU_EALIGN);
    CHECK(mmu_map(&m, 0x1000, 0, 0, D_R, 0) == MMU_EALIGN);
    CHECK(mmu_map(&m, (1ull << 48) - PAGE_SIZE, 0, 2 * PAGE_SIZE, D_R, 0) == MMU_ERANGE);
    CHECK(mmu_map(&m, 0x1000, 1ull << 48, PAGE_SIZE, D_R, 0) == MMU_ERANGE);
    CHECK(mmu_map(&m, 0x1000, 0, PAGE_SIZE, D_W, 0) == MMU_EPERM);          /* W-only 거부 */
    CHECK(mmu_map(&m, 0x1000, 0, PAGE_SIZE, D_R | D_V, 0) == MMU_EPERM);    /* 권한 아닌 비트 */
    /* 블록 vs 테이블 충돌 */
    CHECK(mmu_map(&m, 0x401000, 0x80401000, PAGE_SIZE, D_R, 0) == MMU_OK);
    uint32_t t0 = m.tables;
    CHECK(mmu_map_block(&m, 0x400000, 0x80400000, 2, D_R) == MMU_EEXIST && m.tables == t0);
    CHECK(mmu_map(&m, 0x400000, 0x80400000, SZ_2M, D_R, 0) == MMU_EEXIST && m.tables == t0);
    CHECK(mmu_unmap(&m, 0x401000, PAGE_SIZE) == MMU_OK && m.tables == 1);
    CHECK(mmu_map_block(&m, 0x400000, 0x80400000, 2, D_R) == MMU_OK && m.tables == 3);
    CHECK(mmu_map(&m, 0x401000, 0x1000, PAGE_SIZE, D_R, 0) == MMU_EEXIST);     /* 블록 위에 페이지 */
    CHECK(mmu_map_block(&m, 0, 0, 1, D_R) == MMU_EEXIST);                       /* 1 GB 안에 2 MB 블록 */
    CHECK(mmu_map_block(&m, 0x400000, 0x80400000, 2, D_R) == MMU_EEXIST);       /* 같은 것을 또 */
    CHECK(mmu_unmap(&m, 0x3000000000ull, SZ_1G) == MMU_OK && m.tables == 3);    /* 빈 범위 unmap = no-op */
    CHECK(pt_audit(&m) == 0);
    mmu_destroy(&m);
    CHECK(mmu_init(&m, 4, 3) == MMU_OK);                                     /* 프레임 3 장: 루트 + 2 */
    CHECK(mmu_map(&m, 0x123000, 0x5000, PAGE_SIZE, D_R, 0) == MMU_ENOMEM);
    CHECK(m.tables == 1 && m.nfree == 2 && pt_audit(&m) == 0);
    CHECK(is_fault(xl(&m, 0x123000, ACC_R, false), F_TRANSLATION, 0, 1));
    printf("  ENOMEM rollback: tables=%u free frames=%u\n", m.tables, m.nfree);
    mmu_destroy(&m);

    /* B-4 split: 2 MB 블록에서 4 KB 한 장 빼기 (4 단계) */
    CHECK(mmu_init(&m, 4, 64) == MMU_OK);
    CHECK(mmu_map_block(&m, 0x200000, 0x40200000, 2, D_R | D_W) == MMU_OK && m.tables == 3);
    CHECK(mmu_unmap(&m, 0x205000, PAGE_SIZE) == MMU_OK);
    memset(c, 0, sizeof c); count_leaves(&m, m.ttbr, 0, c);
    printf("  2M block - 1 page: splits=%u tables=%u pages=%llu blocks=%llu\n", m.splits, m.tables, ULL(c[3]), ULL(c[2]));
    CHECK(m.splits == 1 && m.tables == 4 && c[3] == 511 && c[2] == 0);
    xlate_t rs = xl(&m, 0x204FF8, ACC_W, false);
    CHECK(is_ok(rs, 0x40204FF8, 3, 4) && rs.perms == (D_R | D_W));
    CHECK(is_fault(xl(&m, 0x205000, ACC_R, false), F_TRANSLATION, 3, 4));
    CHECK(is_ok(xl(&m, 0x3FFFF8, ACC_R, false), 0x403FFFF8, 3, 4));
    CHECK(mmu_unmap(&m, 0x200000, SZ_2M) == MMU_OK && m.tables == 1 && pt_audit(&m) == 0);
    mmu_destroy(&m);
    /* 1 GB 블록에서 4 KB: split 2 번 (L1 → L2 테이블 → L3 테이블), 3 단계 */
    CHECK(mmu_init(&m, 3, 64) == MMU_OK);
    CHECK(mmu_map(&m, 0x40000000, 0x80000000, SZ_1G, D_R | D_W, 0) == MMU_OK && m.tables == 1);
    CHECK(mmu_unmap(&m, 0x40123000, PAGE_SIZE) == MMU_OK);
    printf("  1G block - 1 page: splits=%u tables=%u\n", m.splits, m.tables);
    CHECK(m.splits == 2 && m.tables == 3);
    print_walk(&m, 0x40123008, ACC_R, false);
    CHECK(is_ok(xl(&m, 0x40200000, ACC_R, false), 0x80200000, 2, 2));        /* 이웃 2 MB 는 L2 블록 */
    CHECK(is_ok(xl(&m, 0x40100000, ACC_R, false), 0x80100000, 3, 3));
    CHECK(is_fault(xl(&m, 0x40123008, ACC_R, false), F_TRANSLATION, 3, 3));
    CHECK(is_ok(xl(&m, 0x7FFFFFF8, ACC_W, false), 0xBFFFFFF8, 2, 2));
    CHECK(mmu_unmap(&m, 0x40400000, SZ_2M) == MMU_OK && m.splits == 2 && m.tables == 3);  /* 정렬된 2 MB: split 없음 */
    CHECK(is_fault(xl(&m, 0x40400000, ACC_R, false), F_TRANSLATION, 2, 2));
    /* 블록 두 개에 걸친 부분 unmap → 양쪽 다 split */
    CHECK(mmu_map(&m, 0x1000000, 0x91000000, 4ull << 20, D_R, 0) == MMU_OK);
    uint32_t sp0 = m.splits, tb0 = m.tables;
    CHECK(mmu_unmap(&m, 0x11FF000, 2 * PAGE_SIZE) == MMU_OK && m.splits == sp0 + 2 && m.tables == tb0 + 2);
    CHECK(is_fault(xl(&m, 0x11FF000, ACC_R, false), F_TRANSLATION, 3, 3) &&
          is_fault(xl(&m, 0x1200000, ACC_R, false), F_TRANSLATION, 3, 3) &&
          is_ok(xl(&m, 0x1201000, ACC_R, false), 0x91201000, 3, 3));
    CHECK(mmu_unmap(&m, 0, 1ull << 39) == MMU_OK && m.tables == 1 && pt_audit(&m) == 0);
    mmu_destroy(&m);

    /* B-5 2 단계 설정: 루트가 L2, 블록은 2 MB 뿐 */
    CHECK(mmu_init(&m, 2, 64) == MMU_OK && m.va_bits == 30);
    CHECK(mmu_map(&m, 0, 0x40000000, SZ_1G, D_R | D_W, 0) == MMU_OK);
    memset(c, 0, sizeof c); count_leaves(&m, m.ttbr, 2, c);
    printf("  2-level map 1G: tables=%u 2M blocks=%llu\n", m.tables, ULL(c[2]));
    CHECK(m.tables == 1 && c[2] == 512);
    CHECK(is_ok(xl(&m, 0x3FFFFFF8, ACC_R, false), 0x7FFFFFF8, 2, 1));
    CHECK(mmu_map_block(&m, 0, 0, 1, D_R) == MMU_ELEVEL);                    /* 이 설정엔 L1 이 없다 */
    CHECK(is_fault(xl(&m, SZ_1G, ACC_R, false), F_ADDRESS_SIZE, 2, 0));
    CHECK(mmu_unmap(&m, 0x12345000, PAGE_SIZE) == MMU_OK && m.splits == 1 && m.tables == 2);
    CHECK(is_fault(xl(&m, 0x12345000, ACC_R, false), F_TRANSLATION, 3, 2));
    CHECK(is_ok(xl(&m, 0x12346000, ACC_R, false), 0x52346000, 3, 2));
    CHECK(is_ok(xl(&m, 0x12400000, ACC_R, false), 0x52400000, 2, 1));
    mmu_destroy(&m);
    /* split_block 단독: 2 단계 루트의 2 MB 블록을 쪼갠 뒤 512 페이지가 모두 같은 변환 · 같은 권한인지 */
    CHECK(mmu_init(&m, 2, 8) == MMU_OK);
    CHECK(mmu_map_block(&m, 0x600000, 0x7A600000, 2, D_R | D_X | D_U) == MMU_OK);
    CHECK(split_block(&m, m.ttbr + 8u * va_index(&m, 0x600000, 2), 2) == MMU_OK && m.tables == 2);
    int bads = 0;
    for (uint64_t off = 0; off < SZ_2M; off += PAGE_SIZE) {
        xlate_t x = xl(&m, 0x600000 + off + 0x18, ACC_X, true);
        if (!is_ok(x, 0x7A600018 + off, 3, 2) || x.perms != (D_R | D_X | D_U)) bads++;
    }
    CHECK(bads == 0 && pt_audit(&m) == 0);
    mmu_destroy(&m);

    /* B-6 손으로 쓴 잘못된 인코딩: L0 블록, L3 의 T=0 */
    CHECK(mmu_init(&m, 4, 64) == MMU_OK);
    const uint64_t VB = 0x0000100000000000ull, VC = 0x0000200000403000ull;
    wr_desc(&m, m.ttbr + 8u * va_index(&m, VB, 0), D_V | D_R | 0x40000000);            /* L0 에 블록 */
    uint64_t t1 = alloc_table(&m), t2 = alloc_table(&m), t3 = alloc_table(&m);
    wr_desc(&m, m.ttbr + 8u * va_index(&m, VC, 0), D_V | D_T | t1);
    wr_desc(&m, t1 + 8u * va_index(&m, VC, 1), D_V | D_T | t2);
    wr_desc(&m, t2 + 8u * va_index(&m, VC, 2), D_V | D_T | t3);
    wr_desc(&m, t3 + 8u * va_index(&m, VC, 3), D_V | D_R | 0x5000);                     /* L3, T=0 */
    CHECK(is_fault(xl(&m, VB + 0x10, ACC_R, false), F_TRANSLATION, 0, 1));
    CHECK(is_fault(xl(&m, VC + 0x10, ACC_R, false), F_TRANSLATION, 3, 4));
    int ab = pt_audit(&m);
    printf("  hand-written bad encodings: audit problems=%d (last: %s)\n", ab, g_audit_msg);
    CHECK(ab == 2);
    wr_desc(&m, t3 + 8u * va_index(&m, VC, 3), D_V | D_T | D_R | 0x5000);                /* T=1 로 고침 = 페이지 */
    wr_desc(&m, m.ttbr + 8u * va_index(&m, VB, 0), 0);
    CHECK(is_ok(xl(&m, VC + 0x10, ACC_R, false), 0x5010, 3, 4) && pt_audit(&m) == 0);
    mmu_destroy(&m);

    /* B-7 walk_reads 표 */
    printf("  walk_reads  levels VAbits  4K-page 2M-block 1G-block empty-root\n");
    for (int L = 4; L >= 2; L--) {
        CHECK(mmu_init(&m, L, 64) == MMU_OK);
        CHECK(mmu_map(&m, 0x1000, 0x1000, PAGE_SIZE, D_R, 0) == MMU_OK);
        CHECK(mmu_map_block(&m, SZ_2M, SZ_2M, 2, D_R) == MMU_OK);
        int rp = xl(&m, 0x1000, ACC_R, false).walk_reads, rb = xl(&m, SZ_2M, ACC_R, false).walk_reads, rg = -1;
        if (L >= 3) { CHECK(mmu_map_block(&m, 4 * SZ_1G, 4 * SZ_1G, 1, D_R) == MMU_OK); rg = xl(&m, 4 * SZ_1G, ACC_R, false).walk_reads; }
        int re = xl(&m, (1ull << m.va_bits) - SZ_2M, ACC_R, false).walk_reads;   /* 루트의 마지막 엔트리 (비어 있음) */
        char gs[4]; if (rg < 0) snprintf(gs, sizeof gs, "-"); else snprintf(gs, sizeof gs, "%d", rg);
        printf("              %d      %2d     %d        %d        %-2s       %d\n", L, m.va_bits, rp, rb, gs, re);
        CHECK(rp == L && rb == L - 1 && rg == (L >= 3 ? L - 2 : -1) && re == 1);
        mmu_destroy(&m);
    }

    /* B-8 랜덤 스트레스: map/unmap 섞기 + 프로브를 그림자 모델(4 KB 단위 배열)과 대조 */
    struct { int L; uint64_t base, size; } cfg[3] = {
        { 2, 0, SZ_1G }, { 3, 0, 2 * SZ_1G }, { 4, 0x7FC0000000ull, 2 * SZ_1G } };   /* 4 단계는 L0 경계(512 GB)를 가로지름 */
    for (int i = 0; i < 3; i++) {
        stress_t s;
        stress(cfg[i].L, cfg[i].base, cfg[i].size, 1500, 0x9E3779B97F4A7C15ull + (uint64_t)i, &s);
        printf("  stress %d-level: ops=%ld maps=%ld eexist=%ld unmaps=%ld splits=%u peak_tables=%u probes=%ld "
               "hits L1/L2/L3=%ld/%ld/%ld perm=%ld trans=%ld mismatches=%ld audit_bad=%ld final_tables=%u\n",
               cfg[i].L, s.ops, s.maps, s.eexist, s.unmaps, s.splits, s.peak, s.probes, s.hit[1], s.hit[2], s.hit[3],
               s.perm, s.trans, s.mism, s.audit_bad, s.final_tables);
        CHECK(s.mism == 0 && s.audit_bad == 0 && s.final_tables == 1);
        CHECK(s.splits > 0 && s.eexist > 0 && s.perm > 0 && s.trans > 0 && s.hit[2] > 0 && s.hit[3] > 0 &&
              (cfg[i].L == 2 || s.hit[1] > 0));                                         /* 커버리지 */
    }
    CHECK(g_dishonest == 0);
}

/* ------------------------------ Part C ------------------------------ */
typedef struct { const char *name; uint64_t va, pa, size, perms; } region_t;
static const region_t LAYOUT[] = {
    { "user .text",           0x0000400000ull, 0x41000000ull, 0x10000,  D_R | D_X | D_U },
    { "user heap",            0x0001000000ull, 0x41100000ull, 0x40000,  D_R | D_W | D_U },
    { "user stack",           0x003FFF0000ull, 0x41200000ull, 0x10000,  D_R | D_W | D_U },   /* 가드 0x3FFEF000 은 비움 */
    { "boot identity map",    0x0040000000ull, 0x40000000ull, SZ_2M,    D_R | D_X },
    { "kernel .text",         0x4000000000ull, 0x40000000ull, SZ_2M,    D_R | D_X | D_G },
    { "kernel .rodata",       0x4000200000ull, 0x40200000ull, 0x100000, D_R | D_G },
    { "kernel .data/.bss",    0x4000300000ull, 0x40300000ull, 0x80000,  D_R | D_W | D_G },
    { "linear map (DRAM 1G)", 0x6000000000ull, 0x40000000ull, SZ_1G,    D_R | D_W | D_G },
    { "MMIO UART",            0x7FC0000000ull, 0x09000000ull, 0x1000,   D_R | D_W | D_G },
    { "MMIO GIC",             0x7FC0010000ull, 0x08000000ull, 0x10000,  D_R | D_W | D_G },
};
#define NLAYOUT (sizeof LAYOUT / sizeof LAYOUT[0])
static int build_layout(mmu_t *m) {
    int bad = 0;
    for (size_t i = 0; i < NLAYOUT; i++)
        if (mmu_map(m, LAYOUT[i].va, LAYOUT[i].pa, LAYOUT[i].size, LAYOUT[i].perms, 0) != MMU_OK) bad++;
    return bad;
}
typedef struct { const char *what; uint64_t va; acc_kind_t k; bool user; fault_t f; int lv; uint64_t pa; } probe_t;
static const probe_t PROBES[] = {
    { "커널 코드 실행",               0x4000001234ull, ACC_X, false, F_NONE,         2, 0x40001234ull },
    { "커널 코드에 쓰기 (W^X)",       0x4000001234ull, ACC_W, false, F_PERMISSION,   2, 0 },
    { "rodata 에 쓰기",               0x4000200010ull, ACC_W, false, F_PERMISSION,   3, 0 },
    { "data 실행 (XN)",               0x4000300100ull, ACC_X, false, F_PERMISSION,   3, 0 },
    { "data 쓰기",                    0x4000300100ull, ACC_W, false, F_NONE,         3, 0x40300100ull },
    { "linear map 읽기",              0x6012345678ull, ACC_R, false, F_NONE,         1, 0x52345678ull },
    { "linear map 실행",              0x6012345678ull, ACC_X, false, F_PERMISSION,   1, 0 },
    { "linear map 으로 커널 text 쓰기", 0x6000001234ull, ACC_W, false, F_NONE,       1, 0x40001234ull },
    { "user 가 커널 data 읽기",       0x4000300100ull, ACC_R, true,  F_PERMISSION,   3, 0 },
    { "user text 실행",               0x0000400100ull, ACC_X, true,  F_NONE,         3, 0x41000100ull },
    { "user text 에 쓰기",            0x0000400100ull, ACC_W, true,  F_PERMISSION,   3, 0 },
    { "user heap 쓰기",               0x0001000040ull, ACC_W, true,  F_NONE,         3, 0x41100040ull },
    { "user heap 실행",               0x0001000040ull, ACC_X, true,  F_PERMISSION,   3, 0 },
    { "stack 맨 위 쓰기",             0x003FFFFFF8ull, ACC_W, true,  F_NONE,         3, 0x4120FFF8ull },
    { "stack 가드 페이지 쓰기",       0x003FFEFFF8ull, ACC_W, true,  F_TRANSLATION,  3, 0 },
    { "NULL 읽기",                    0x0000000000ull, ACC_R, true,  F_TRANSLATION,  2, 0 },
    { "커널이 user text 실행 (PXN)",  0x0000400100ull, ACC_X, false, F_PERMISSION,   3, 0 },
    { "커널이 user heap 읽기 (PAN 없음)", 0x0001000040ull, ACC_R, false, F_NONE,     3, 0x41100040ull },
    { "user 가 UART 접근",            0x7FC0000000ull, ACC_R, true,  F_PERMISSION,   3, 0 },
    { "커널 UART 쓰기",               0x7FC0000004ull, ACC_W, false, F_NONE,         3, 0x09000004ull },
    { "빈 1 GB 슬롯 (L1[128])",       0x2000000000ull, ACC_R, false, F_TRANSLATION,  1, 0 },
    { "VA bit39 = 1",                 0x8000000000ull, ACC_R, false, F_ADDRESS_SIZE, 1, 0 },
    { "boot identity 실행",           0x0040000100ull, ACC_X, false, F_NONE,         2, 0x40000100ull },
};
#define NPROBES (sizeof PROBES / sizeof PROBES[0])

static bool probe_match(const probe_t *p, xlate_t r, int sl) {
    int reads = p->f == F_ADDRESS_SIZE ? 0 : p->lv - sl + 1;
    if (p->f != r.fault || p->lv != r.level || r.walk_reads != reads) return false;
    return p->f != F_NONE || p->pa == 0 || p->pa == r.pa;
}

static void part_c(void) {
    printf("== Part C: 펌웨어/OS 레이아웃 (3 단계, VA 39 bit) ==\n");
    mmu_t m;
    char b[96], ps[6];
    CHECK(mmu_init(&m, 3, 256) == MMU_OK);
    CHECK(build_layout(&m) == 0);
    for (size_t i = 0; i < NLAYOUT; i++) {
        perm_str(LAYOUT[i].perms, ps);
        printf("  %-22s va=0x%010llx pa=0x%08llx size=0x%08llx %s\n", LAYOUT[i].name, ULL(LAYOUT[i].va),
               ULL(LAYOUT[i].pa), ULL(LAYOUT[i].size), ps);
    }
    uint64_t c[4] = {0};
    count_leaves(&m, m.ttbr, 1, c);
    printf("  page tables: %u (= %u KB)  leaves 1G=%llu 2M=%llu 4K=%llu\n", m.tables, m.tables * 4, ULL(c[1]), ULL(c[2]), ULL(c[3]));
    CHECK(m.tables == 10 && c[1] == 1 && c[2] == 2 && c[3] == 16 + 64 + 16 + 256 + 128 + 1 + 16);
    int bad = 0;
    for (size_t i = 0; i < NPROBES; i++) {
        xlate_t r = xl(&m, PROBES[i].va, PROBES[i].k, PROBES[i].user);
        bool ok = probe_match(&PROBES[i], r, 1);
        if (!ok) bad++;
        fmt_x(&r, b, sizeof b);
        printf("  %2zu ", i + 1); put_padded(PROBES[i].what, 32);
        printf(" %s 0x%010llx -> %s%s\n", acc_str(r.acc), ULL(PROBES[i].va), b, ok ? "" : "   <-- 기대와 다름");
    }
    CHECK(bad == 0);
    CHECK(pt_audit(&m) == 0);
    /* 부팅 끝: identity map 제거 → 그 1 GB 슬롯의 L2 테이블이 비어 반납 → 폴트가 L1 에서 */
    uint32_t tb = m.tables;
    CHECK(mmu_unmap(&m, 0x40000000ull, SZ_2M) == MMU_OK);
    xlate_t r = xl(&m, 0x40000100ull, ACC_X, false);
    fmt_x(&r, b, sizeof b);
    printf("  after boot: unmap identity -> tables %u -> %u, kernel X 0x40000100 -> %s\n", tb, m.tables, b);
    CHECK(m.tables == tb - 1 && is_fault(r, F_TRANSLATION, 1, 1));
    mmu_destroy(&m);

    /* 1 GB 를 매핑하는 페이지 테이블 메모리: 4 KB 페이지 vs 2 MB 블록 vs 1 GB 블록 */
    printf("  map 1 GB      levels  tables  table-bytes  %%of-1GB  reads/access\n");
    struct { const char *mode; uint64_t pa; unsigned flags; uint32_t want[3]; } md[3] = {
        { "4K pages ", 0x100000000ull, MAP_PAGES, { 515, 514, 513 } },
        { "2M blocks", 0x100200000ull, 0,         { 3, 2, 1 } },
        { "1G block ", 0x100000000ull, 0,         { 2, 1, 0 } },
    };
    for (int k = 0; k < 3; k++) {
        for (int L = 4; L >= 2; L--) {
            if (k == 2 && L == 2) { printf("  %s       2       -  (2 단계엔 L1 이 없음)\n", md[k].mode); continue; }
            CHECK(mmu_init(&m, L, 1024) == MMU_OK);
            uint64_t va = L == 2 ? 0 : SZ_1G, pa = md[k].pa;
            if (k == 1 && L == 2) pa = 0x40000000ull;                 /* 2 단계: 1G 정렬 PA 도 2 MB 블록으로 */
            int rc = mmu_map(&m, va, pa, SZ_1G, D_R | D_W, md[k].flags);
            int rd = xl(&m, va + 0x12345678, ACC_R, false).walk_reads;
            printf("  %s       %d     %4u  %8u KB   %6.3f%%   %d\n", md[k].mode, L, m.tables, m.tables * 4,
                   100.0 * m.tables * 4096.0 / (double)SZ_1G, rd);
            CHECK(rc == MMU_OK && m.tables == md[k].want[4 - L]);
            mmu_destroy(&m);
        }
    }
    CHECK(g_dishonest == 0);
}

int main(void) {
    part_a();
    part_b();
    part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
