/* ARCH-A01 추가 연습 — 한 번에 검증
 *
 * solution.c 의 MMU 모델을 그대로 가져와(main 만 이름을 바꿔 포함) 4 개 연습의 답을 검사한다.
 *   연습 1  페이지 테이블 장수 공식 (페이지만 / 2 MB 블록만) → 3 설정 x 결정적 랜덤 범위에서 시뮬레이터와 일치
 *   연습 2  mmu_protect: 권한 변경 + 필요한 만큼만 split (Linux set_memory_ro / linear alias 를 RO 로)
 *   연습 3  ptdump: 트리를 VA 순서로 훑어 범위로 합쳐 출력 + W^X / U&G 감사
 *   연습 4  mmu_collapse: L3 테이블 512 장이 "한 블록으로 표현 가능" 하면 2 MB 블록으로 접기 (khugepaged 느낌)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr   (solution.c 와 같은 폴더에서)
 */
#define main solution_main
#include "solution.c"
#undef main

static int p_pass, p_total;
#define PCHECK(expr) do { p_total++; if (expr) { p_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------------- 연습 1: 테이블 장수 공식 ----------------
 * 빈 트리에 [va, va+size) 를 leaf 레벨 Lf 로만 매핑하면, 루트 아래 레벨 lv 의 테이블은
 * "레벨 lv-1 엔트리 하나가 덮는 정렬 구간(level_size(lv-1))" 중 범위가 걸치는 것마다 1 장. */
static uint64_t span(uint64_t va, uint64_t size, uint64_t s) { return (va + size - 1) / s - va / s + 1; }
static uint64_t tables_formula(int levels, uint64_t va, uint64_t size, int leaf_lv) {
    uint64_t t = 1;                                             /* 루트 */
    for (int lv = 4 - levels + 1; lv <= leaf_lv; lv++) t += span(va, size, level_size(lv - 1));
    return t;
}
static void ex1(void) {
    printf("== 연습 1: 테이블 장수 공식 vs 시뮬레이터 ==\n");
    g_rng = 0xA01A01A01ull;
    int cases = 0, ok = 0;
    for (int L = 2; L <= 4; L++) {
        for (int k = 0; k < 60; k++) {
            mmu_t m;
            if (mmu_init(&m, L, 2048) != MMU_OK) continue;
            uint64_t lim = 1ull << m.va_bits, va, size, pa;
            int leaf;
            if (k % 2 == 0) {                                   /* 페이지만 */
                size = (1 + rndn(1500)) * PAGE_SIZE;
                va = rndn((lim - size) / PAGE_SIZE) * PAGE_SIZE;
                pa = 0x1000; leaf = 3;
                if (mmu_map(&m, va, pa, size, D_R, MAP_PAGES) != MMU_OK) { mmu_destroy(&m); continue; }
            } else {                                            /* 2 MB 블록만 (pa 를 1 GB 미정렬로 → 1 GB 블록 금지) */
                size = (1 + rndn(700)) * SZ_2M;
                if (size > lim) size = lim;
                va = rndn((lim - size) / SZ_2M + 1) * SZ_2M;
                pa = SZ_2M; leaf = 2;
                if (mmu_map(&m, va, pa, size, D_R, 0) != MMU_OK) { mmu_destroy(&m); continue; }
            }
            cases++;
            if (m.tables == tables_formula(L, va, size, leaf)) ok++;
            else printf("  mismatch L=%d va=0x%llx size=0x%llx sim=%u formula=%llu\n", L, ULL(va), ULL(size),
                        m.tables, ULL(tables_formula(L, va, size, leaf)));
            mmu_destroy(&m);
        }
    }
    printf("  %d/%d random ranges match\n", ok, cases);
    PCHECK(cases == 180 && ok == cases);
    /* 손계산 예: 4 단계, [0x7FFFF000, 0x80001000) 페이지 2 장 = 1 GB 경계를 가로지름 */
    uint64_t t = tables_formula(4, 0x7FFFF000ull, 2 * PAGE_SIZE, 3);
    printf("  4-level, 2 pages across the 2 GB line (0x7FFFF000..0x80001000): tables = %llu\n", ULL(t));
    PCHECK(t == 1 + 1 + 2 + 2);
}

/* ---------------- 연습 2: mmu_protect ---------------- */
#define MMU_ENOENT (-7)
static bool range_mapped(const mmu_t *m, uint64_t table, int lv, uint64_t va, uint64_t size) {
    while (size) {
        uint64_t esz = level_size(lv), chunk = esz - (va & (esz - 1));
        if (chunk > size) chunk = size;
        uint64_t d = peek(m, table + 8u * va_index(m, va, lv));
        if (!(d & D_V)) return false;
        if (lv < 3 && (d & D_T) && !range_mapped(m, d & OA_MASK, lv + 1, va, chunk)) return false;
        va += chunk; size -= chunk;
    }
    return true;
}
static int protect_level(mmu_t *m, uint64_t table, int lv, uint64_t va, uint64_t size, uint64_t perms) {
    while (size) {
        uint64_t esz = level_size(lv), chunk = esz - (va & (esz - 1));
        if (chunk > size) chunk = size;
        uint64_t ea = table + 8u * va_index(m, va, lv);
        uint64_t d = rd_desc(m, ea);
        bool leaf = (lv == 3) || !(d & D_T);
        if (leaf && chunk == esz) {
            wr_desc(m, ea, (d & ~PERM_MASK) | perms);          /* 같은 leaf 의 권한만 바꿈: 실제 칩은 TLBI 필요 */
        } else {
            if (leaf) {                                         /* 일부만 → split (실제 칩: break-before-make) */
                int rc = split_block(m, ea, lv);
                if (rc) return rc;
                d = rd_desc(m, ea);
            }
            int rc = protect_level(m, d & OA_MASK, lv + 1, va, chunk, perms);
            if (rc) return rc;
        }
        va += chunk; size -= chunk;
    }
    return MMU_OK;
}
static int mmu_protect(mmu_t *m, uint64_t va, uint64_t size, uint64_t perms) {
    int rc = check_range(m, va, size);
    if (rc) return rc;
    if ((perms & ~PERM_MASK) || ((perms & D_W) && !(perms & D_R))) return MMU_EPERM;
    if (!range_mapped(m, m->ttbr, start_level(m), va, size)) return MMU_ENOENT;   /* 바꾸기 전에 전부 확인 */
    return protect_level(m, m->ttbr, start_level(m), va, size, perms);
}
static void ex2(void) {
    printf("== 연습 2: mmu_protect — linear map 의 커널 text 별칭을 RO 로 ==\n");
    mmu_t m;
    char b[96];
    PCHECK(mmu_init(&m, 3, 256) == MMU_OK && build_layout(&m) == 0);
    uint32_t t0 = m.tables, s0 = m.splits;
    PCHECK(mmu_protect(&m, 0x6000000000ull, SZ_2M, D_R | D_G) == MMU_OK);          /* 1 GB 블록 → 2 MB 블록 512 개 */
    xlate_t r = xl(&m, 0x6000001234ull, ACC_W, false);
    fmt_x(&r, b, sizeof b);
    printf("  protect [0x6000000000, +2M) R: tables %u -> %u, splits +%u, K:W 0x6000001234 -> %s\n", t0, m.tables, m.splits - s0, b);
    PCHECK(m.tables == t0 + 1 && m.splits == s0 + 1 && is_fault(r, F_PERMISSION, 2, 2));
    PCHECK(is_ok(xl(&m, 0x6000200000ull, ACC_W, false), 0x40200000ull, 2, 2));       /* 이웃 2 MB 는 RW 그대로 */
    PCHECK(is_ok(xl(&m, 0x6012345678ull, ACC_R, false), 0x52345678ull, 2, 2));      /* 1 GB 블록이 사라져 읽기 +1 */
    PCHECK(mmu_protect(&m, 0x6000201000ull, PAGE_SIZE, D_R | D_G) == MMU_OK);        /* 4 KB 하나 → 또 split */
    PCHECK(m.tables == t0 + 2 && is_fault(xl(&m, 0x6000201000ull, ACC_W, false), F_PERMISSION, 3, 3));
    PCHECK(is_ok(xl(&m, 0x6000202000ull, ACC_W, false), 0x40202000ull, 3, 3));
    uint32_t t1 = m.tables;
    PCHECK(mmu_protect(&m, 0x0000400000ull, 0x11000, D_R | D_U) == MMU_ENOENT && m.tables == t1);   /* 끝 한 장이 비어 있음 */
    PCHECK(is_ok(xl(&m, 0x0000400100ull, ACC_X, true), 0x41000100ull, 3, 3));        /* 아무것도 안 바뀌었다 */
    PCHECK(mmu_protect(&m, 0x0001000000ull, PAGE_SIZE, D_W | D_U) == MMU_EPERM);
    PCHECK(pt_audit(&m) == 0);
    mmu_destroy(&m);
}

/* ---------------- 연습 3: ptdump ---------------- */
typedef struct { uint64_t va, end, pa, perms, leaf; } prange_t;
typedef struct { prange_t r[64]; int n; } dump_t;
static void dump_level(const mmu_t *m, uint64_t t, int lv, uint64_t vbase, dump_t *out) {
    for (unsigned i = 0; i < ENTRIES; i++) {
        uint64_t d = peek(m, t + 8u * i), va = vbase + (uint64_t)i * level_size(lv);
        if (!(d & D_V)) continue;
        if (lv < 3 && (d & D_T)) { dump_level(m, d & OA_MASK, lv + 1, va, out); continue; }
        uint64_t sz = level_size(lv), pa = d & OA_MASK, perms = d & PERM_MASK;
        prange_t *last = out->n ? &out->r[out->n - 1] : NULL;
        if (last && last->end == va && last->perms == perms && last->pa + (last->end - last->va) == pa && last->leaf == sz)
            last->end += sz;                                   /* 연속 VA · 연속 PA · 같은 권한 · 같은 leaf 크기 → 합침 */
        else if (out->n < 64)
            out->r[out->n++] = (prange_t){ va, va + sz, pa, perms, sz };
    }
}
static int ptdump(const mmu_t *m, dump_t *out, bool print) {
    out->n = 0;
    dump_level(m, m->ttbr, start_level(m), 0, out);
    int wx = 0;
    for (int i = 0; i < out->n; i++) {
        prange_t *p = &out->r[i];
        char ps[6];
        perm_str(p->perms, ps);
        bool bad = (p->perms & D_W) && (p->perms & D_X);
        bool ug = (p->perms & D_U) && (p->perms & D_G);
        wx += bad + ug;
        if (print)
            printf("  0x%010llx-0x%010llx %7lluK %s %s -> pa 0x%llx%s%s\n", ULL(p->va), ULL(p->end),
                   ULL((p->end - p->va) >> 10), ps, size_str(p->leaf), ULL(p->pa), bad ? "  !! W+X" : "", ug ? "  !! user+global" : "");
    }
    return wx;
}
static void ex3(void) {
    printf("== 연습 3: ptdump + W^X / user+global 감사 ==\n");
    mmu_t m;
    dump_t d;
    PCHECK(mmu_init(&m, 3, 256) == MMU_OK && build_layout(&m) == 0);
    int bad = ptdump(&m, &d, true);
    PCHECK(d.n == (int)NLAYOUT && bad == 0);                    /* 영역 10 개 = 범위 10 개, 위반 0 */
    int same = 0;
    for (size_t i = 0; i < NLAYOUT; i++)
        for (int j = 0; j < d.n; j++)
            if (d.r[j].va == LAYOUT[i].va && d.r[j].end == LAYOUT[i].va + LAYOUT[i].size && d.r[j].pa == LAYOUT[i].pa &&
                d.r[j].perms == LAYOUT[i].perms) same++;
    PCHECK(same == (int)NLAYOUT);
    PCHECK(mmu_map(&m, 0x0002000000ull, 0x41300000ull, 0x2000, D_R | D_W | D_X | D_U | D_G, 0) == MMU_OK);  /* JIT 실수 */
    bad = ptdump(&m, &d, false);
    for (int j = 0; j < d.n; j++)
        if (d.r[j].va == 0x0002000000ull) {
            char ps[6];
            perm_str(d.r[j].perms, ps);
            printf("  + 0x%010llx-0x%010llx %s  -> audit flags = %d (W+X, user+global)\n", ULL(d.r[j].va), ULL(d.r[j].end), ps, bad);
        }
    PCHECK(bad == 2 && d.n == (int)NLAYOUT + 1);
    mmu_destroy(&m);
}

/* ---------------- 연습 4: collapse ---------------- */
#define COL_NOT_TABLE (-10)
#define COL_HOLE      (-11)
#define COL_ATTR      (-12)
#define COL_PA        (-13)
static int mmu_collapse(mmu_t *m, uint64_t va) {               /* va = 2 MB 정렬, L2 엔트리 아래 L3 테이블을 블록으로 */
    if ((va & (SZ_2M - 1)) || check_range(m, va, SZ_2M) || m->levels < 2) return MMU_EALIGN;
    uint64_t table = m->ttbr, ea = 0, d = 0;
    for (int lv = start_level(m); lv <= 2; lv++) {              /* L2 엔트리까지 내려간다 */
        ea = table + 8u * va_index(m, va, lv);
        d = rd_desc(m, ea);
        if (!(d & D_V) || !(d & D_T)) return COL_NOT_TABLE;
        table = d & OA_MASK;
    }
    uint64_t l3 = table, first = rd_desc(m, l3), base = first & OA_MASK;
    if (base & (SZ_2M - 1)) return COL_PA;
    for (unsigned i = 0; i < ENTRIES; i++) {
        uint64_t e = rd_desc(m, l3 + 8u * i);
        if (!(e & D_V) || !(e & D_T)) return COL_HOLE;
        if ((e & LEAF_ATTR) != (first & LEAF_ATTR)) return COL_ATTR;
        if ((e & OA_MASK) != base + (uint64_t)i * PAGE_SIZE) return COL_PA;
    }
    wr_desc(m, ea, D_V | (first & LEAF_ATTR) | base);           /* 실제 칩: break-before-make + TLBI */
    free_table(m, l3);
    return MMU_OK;
}
static void ex4(void) {
    printf("== 연습 4: collapse — 페이지 512 장을 2 MB 블록으로 ==\n");
    mmu_t m;
    PCHECK(mmu_init(&m, 3, 64) == MMU_OK);
    PCHECK(mmu_map(&m, 0x800000, 0x40800000, SZ_2M, D_R | D_W, MAP_PAGES) == MMU_OK && m.tables == 3);
    PCHECK(is_ok(xl(&m, 0x812345, ACC_R, false), 0x40812345, 3, 3));
    PCHECK(mmu_collapse(&m, 0x800000) == MMU_OK && m.tables == 2);
    xlate_t r = xl(&m, 0x812345, ACC_R, false);
    printf("  collapse 0x800000: tables 3 -> %u, reads %d, leaf %s\n", m.tables, r.walk_reads, size_str(r.size));
    PCHECK(is_ok(r, 0x40812345, 2, 2) && pt_audit(&m) == 0);
    PCHECK(mmu_collapse(&m, 0x800000) == COL_NOT_TABLE);         /* 이미 블록 */
    PCHECK(mmu_collapse(&m, 0x801000) == MMU_EALIGN);
    /* 접을 수 없는 세 경우 */
    PCHECK(mmu_map(&m, 0xA00000, 0x40A00000, SZ_2M, D_R | D_W, MAP_PAGES) == MMU_OK);
    PCHECK(mmu_unmap(&m, 0xA05000, PAGE_SIZE) == MMU_OK && mmu_collapse(&m, 0xA00000) == COL_HOLE);
    PCHECK(mmu_map(&m, 0xA05000, 0x40A05000, PAGE_SIZE, D_R, 0) == MMU_OK && mmu_collapse(&m, 0xA00000) == COL_ATTR);
    PCHECK(mmu_map(&m, 0xC00000, 0x40C01000, SZ_2M, D_R | D_W, 0) == MMU_OK && mmu_collapse(&m, 0xC00000) == COL_PA);
    printf("  hole -> %d (COL_HOLE), different perms -> %d (COL_ATTR), PA not 2M-aligned -> %d (COL_PA)\n",
           COL_HOLE, COL_ATTR, COL_PA);
    PCHECK(pt_audit(&m) == 0);
    mmu_destroy(&m);
}

int main(void) {
    ex1();
    ex2();
    ex3();
    ex4();
    printf("%d/%d passed\n", p_pass, p_total);
    return p_pass == p_total ? 0 : 1;
}
