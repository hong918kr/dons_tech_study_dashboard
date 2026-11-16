/* ENV: qemu */
/* ARM-A03 practice.c — 추가 연습 답. 본문 테스트(solution.c 의 main)를 먼저 돌리고 연습 1–4 를 더한다.
 * 빌드: run_qemu.sh practice → practice.c(+solution.c 포함) + practice.S(+solution.S 포함) + shared_*
 */
#define main   solution_main
#define finish solution_finish
#include "solution.c"
#undef main
#undef finish
int finish(void);
int solution_finish(void) { sh_puts("  -- (solution tests above, practice below) --\n"); return 0; }

void set_ttbr0_only(uint64_t ttbr0);
void mmu_disable(void);

/* ---------- 연습 1: 2 MB 블록 하나를 L3 4 KB 페이지로 쪼개기 (가드 페이지, RO 페이지, 4 KB 별칭) ---------- */
#define VA_PAGES  0x48600000ull                 /* l2_ram[67]: 원래 invalid → L3 테이블 */
#define PA_PAGES  0x46C00000ull                 /* 실제 RAM 2 MB */
#define PG        4096ull
static uint64_t l3[512] __attribute__((aligned(4096)));

static uint64_t mk_page(uint64_t pa, unsigned attr, unsigned ap, unsigned sh, unsigned xn, unsigned af) {
    return mk_block(pa, attr, ap, sh, xn, af) | 2u;   /* L3 에서는 [1:0] = 11 이 "페이지" (01 은 invalid) */
}
static void build_l3(void) {
    for (unsigned i = 0; i < 512u; i++) l3[i] = mk_page(PA_PAGES + i * PG, ATTR_NORMAL, AP_RW_EL1, SH_INNER, 1, 1);
    l3[5] = 0;                                                                   /* 가드 페이지 */
    l3[6] = mk_page(PA_PAGES + 6u * PG, ATTR_NORMAL, AP_RO_EL1, SH_INNER, 1, 1); /* RO 페이지 */
    l3[7] = mk_page(PA_PAGES, ATTR_NORMAL, AP_RW_EL1, SH_INNER, 1, 1);           /* 페이지 0 의 별칭 */
    dsb_ishst();
    l2_ram[(VA_PAGES - RAM_BASE) >> 21] = mk_table(l3);     /* invalid → valid: TLB 에 없던 것이라 TLBI 불필요 */
    dsb_ishst();
    isb_sync();
}

/* ---------- 연습 2: T0SZ → 시작 레벨 / 루트 엔트리 수, MAIR 속성 바이트 해석 ---------- */
static int start_level(unsigned t0sz) {         /* 4 KB 그래뉼, FEAT_LPA2·TTST 없이 */
    if (t0sz < 16u || t0sz > 39u) return -1;
    return t0sz <= 24u ? 0 : t0sz <= 33u ? 1 : 2;
}
static unsigned root_entries(unsigned t0sz) {   /* 시작 레벨 테이블에서 실제로 쓰는 엔트리 수 */
    int l = start_level(t0sz);
    unsigned shift = 39u - 9u * (unsigned)l;
    return 1u << (64u - t0sz - shift);
}
static unsigned levels_ref(unsigned t0sz) {      /* 독립 참조: 12 비트 오프셋 뒤 9 비트씩 몇 단계? */
    unsigned bits = 64u - t0sz - 12u;
    return (bits + 8u) / 9u;
}

enum attr_kind { A_RESERVED, A_DEV_nGnRnE, A_DEV_nGnRE, A_DEV_nGRE, A_DEV_GRE, A_NORMAL };
static enum attr_kind mair_kind(uint8_t b, char *outer, char *inner) {
    unsigned hi = b >> 4, lo = b & 0xFu;
    if (hi == 0u) {                              /* Device: 0000ddxx, xx 는 00 이어야 (FEAT_XS 의 01 은 제외) */
        if (lo & 3u) return A_RESERVED;
        return (enum attr_kind)(A_DEV_nGnRnE + (lo >> 2));
    }
    /* Normal: 각 니블 0100 = NC, 00RW(RW≠0) = WT-transient, 01RW = WB-transient, 10RW = WT, 11RW = WB */
    unsigned n[2] = { hi, lo };
    char c[2];
    for (int k = 0; k < 2; k++) {
        if (n[k] == 0x4u) c[k] = 'N';
        else if (n[k] == 0u) return A_RESERVED;  /* inner 0000 + outer ≠ 0: UNPREDICTABLE */
        else if (n[k] < 0x4u) c[k] = 't';        /* WT transient */
        else if (n[k] < 0x8u) c[k] = 'b';        /* WB transient */
        else if (n[k] < 0xCu) c[k] = 'T';        /* WT non-transient */
        else c[k] = 'B';                         /* WB non-transient */
    }
    *outer = c[0]; *inner = c[1];
    return A_NORMAL;
}

/* ---------- 연습 3: 주소 공간 전환 (TTBR0 바꾸기) ---------- */
static uint64_t l1b[512]     __attribute__((aligned(4096)));
static uint64_t l2_ram_b[512] __attribute__((aligned(4096)));

int main(void) {
    solution_main();
    if (!(read_sctlr() & 1u)) { CHECK(0); return finish(); }
    struct walk_res w;

    /* 연습 1 */
    build_l3();
    g_rng = 0x1234567887654321ull;
    unsigned bad = 0, at_bad = 0;
    for (unsigned i = 0; i < 512u; i++) {
        for (unsigned k = 0; k < 2u; k++) {
            uint64_t va = VA_PAGES + i * PG + (k ? (xorshift64() & (PG - 1u)) : 0u);
            unsigned f = walk(l1, T0SZ, va, &w);
            unsigned ef = (i == 5u) ? FSC_TRANS(3) : 0u;
            uint64_t epa = (i == 7u ? PA_PAGES : PA_PAGES + i * PG) + (va & (PG - 1u));
            if (f != ef || (f == 0u && (w.pa != epa || w.level != 3u))) bad++;
            uint64_t pr = at_s1e1r(va), pw = at_s1e1w(va);
            unsigned ew = f ? f : el1_access(&w, 'w');
            if (par_fault(pr) != f || par_fault(pw) != ew || (f == 0u && par_pa(pr) != (epa & OA_MASK))) at_bad++;
        }
    }
    sh_puts("  [1] L3 pages: walk mismatches = "); sh_dec(bad); sh_puts(", AT mismatches = "); sh_dec(at_bad); sh_puts("\n");
    CHECK(bad == 0u && at_bad == 0u);
    uint64_t r = load64(VA_PAGES + 5u * PG + 0x18u);
    dump("  [1] guard page DFSC = ", g_last.fsc);
    CHECK(r == 0xBADu && g_last.fsc == FSC_TRANS(3) && g_last.far == VA_PAGES + 5u * PG + 0x18u);
    store64(VA_PAGES + 6u * PG, 1u);
    dump("  [1] RO page wr DFSC = ", g_last.fsc);
    CHECK(g_last.fsc == FSC_PERM(3) && g_last.wnr == 1u);
    store64(VA_PAGES + 0x80u, 0x4B1A5ull);
    CHECK(load64(VA_PAGES + 7u * PG + 0x80u) == 0x4B1A5ull);   /* 4 KB 별칭 */
    CHECK(load64(VA_PAGES + 4u * PG + 0x80u) == 0u);

    /* 연습 2 */
    unsigned lv_ok = 0;
    for (unsigned t = 16u; t <= 39u; t++)
        lv_ok += (unsigned)(start_level(t) == 4 - (int)levels_ref(t)) &&
                 root_entries(t) == 1u << (64u - t - 12u - 9u * (levels_ref(t) - 1u));
    CHECK(lv_ok == 24u);
    CHECK(root_entries(25) == 512u && root_entries(32) == 4u && root_entries(33) == 2u && root_entries(16) == 512u);
    CHECK(start_level(15) == -1 && start_level(40) == -1);
    char o = 0, in = 0;
    CHECK(mair_kind(0xFF, &o, &in) == A_NORMAL && o == 'B' && in == 'B');
    CHECK(mair_kind(0x44, &o, &in) == A_NORMAL && o == 'N' && in == 'N');
    CHECK(mair_kind(0xBB, &o, &in) == A_NORMAL && o == 'T' && in == 'T');
    CHECK(mair_kind(0xF4, &o, &in) == A_NORMAL && o == 'B' && in == 'N');
    CHECK(mair_kind(0x04, &o, &in) == A_DEV_nGnRE && mair_kind(0x00, &o, &in) == A_DEV_nGnRnE &&
          mair_kind(0x0C, &o, &in) == A_DEV_GRE && mair_kind(0x08, &o, &in) == A_DEV_nGRE);
    CHECK(mair_kind(0xF0, &o, &in) == A_RESERVED && mair_kind(0x01, &o, &in) == A_RESERVED);
    unsigned valid = 0;
    for (unsigned b = 0; b < 256u; b++) valid += mair_kind((uint8_t)b, &o, &in) != A_RESERVED;
    sh_puts("  [2] valid MAIR attribute bytes (v8.0 rules) = "); sh_dec(valid); sh_puts("\n");
    CHECK(valid == 229u);                                       /* 4 Device + 15 × 15 Normal */

    /* 연습 3: 두 번째 주소 공간 — VA_ALIAS 만 PA_SPARE 로 다르게 */
    for (unsigned i = 0; i < 512u; i++) { l1b[i] = l1[i]; l2_ram_b[i] = l2_ram[i]; }
    l1b[1] = mk_table(l2_ram_b);
    l2_ram_b[(VA_ALIAS - RAM_BASE) >> 21] = mk_block(PA_SPARE, ATTR_NORMAL, AP_RW_EL1, SH_INNER, 1, 1);
    dsb_ishst();
    store64(PA_SHARED + 0x400u, 0xA0A0u);
    store64(PA_SPARE + 0x400u, 0xB0B0u);
    CHECK(load64(VA_ALIAS + 0x400u) == 0xA0A0u);                /* 주소 공간 A, TLB 에 들어감 */
    mmu_enable(ADDR(l1b), read_tcr(), MAIR_VALUE);              /* TTBR0 = l1b (mmu_enable 이 TLBI VMALLE1 포함) */
    r = load64(VA_ALIAS + 0x400u);
    dump("  [3] space B read (with TLBI)  = ", r);
    CHECK(r == 0xB0B0u && read_ttbr0() == ADDR(l1b));
    CHECK(load64(0x40080000u) == load64(VA_GB + 0x80000u));     /* 나머지는 같다 */
    mmu_enable(ADDR(l1), read_tcr(), MAIR_VALUE);               /* 원래 공간으로 */
    CHECK(load64(VA_ALIAS + 0x400u) == 0xA0A0u);
    set_ttbr0_only(ADDR(l1b));                                  /* 관찰: 같은 ASID 0 으로 TTBR0 만 바꾸면? */
    dump("  [3] space B read (no TLBI)    = ", load64(VA_ALIAS + 0x400u));
    tlbi_all();
    r = load64(VA_ALIAS + 0x400u);
    dump("  [3] space B read (after TLBI) = ", r);
    CHECK(r == 0xB0B0u);
    mmu_enable(ADDR(l1), read_tcr(), MAIR_VALUE);
    CHECK(load64(VA_ALIAS + 0x400u) == 0xA0A0u);

    /* 연습 4: MMU 끄고 T0SZ = 32 (4 GB VA) 로 다시 켜기 */
    mmu_disable();
    dump("  [4] SCTLR after disable = ", read_sctlr());
    CHECK((read_sctlr() & 1u) == 0u);
    uint64_t tcr32 = make_tcr(32, read_mmfr0());
    mmu_enable(ADDR(l1), tcr32, MAIR_VALUE);                    /* 같은 L1 테이블: T0SZ 25..33 은 모두 L1 시작 */
    dump("  [4] TCR_EL1 (T0SZ 32)   = ", read_tcr());
    CHECK((read_sctlr() & 1u) && (read_tcr() & 0x3Fu) == 32u);
    CHECK(load64(VA_GB + (PA_SHARED - RAM_BASE) + 0x400u) == 0xA0A0u);     /* L1[2] 1 GB 블록은 여전히 */
    r = load64(0x100000000ull);                                  /* 4 GB: 이제 범위 밖 */
    dump("  [4] VA 4GB DFSC          = ", g_last.fsc);
    CHECK(r == 0xBADu && g_last.fsc == FSC_TRANS(0));
    CHECK(walk(l1, 32, 0x100000000ull, &w) == FSC_TRANS(0) && walk(l1, 25, 0x100000000ull, &w) == FSC_TRANS(1));
    mmu_disable();
    mmu_enable(ADDR(l1), make_tcr(T0SZ, read_mmfr0()), MAIR_VALUE);
    CHECK((read_tcr() & 0x3Fu) == T0SZ);
    return finish();
}
