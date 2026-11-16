/* ENV: qemu */
/* ARM-A03 starter.c / solution.c — AArch64 MMU 켜기 (QEMU virt, cortex-a53, EL1): descriptor 빌더 · 변환 테이블 · 소프트웨어
 * page walk · TCR 계산 · AF 지연 설정 · break-before-make 재매핑 + 펌웨어 테스트.
 * 빌드: run_qemu.sh [starter|solution]  →  <v>.c + <v>.S + shared_start.S + shared_rt.c (+ arm_a03.ld)
 * 종료 코드: 0 = 전부 통과, 1 = 실패, 3 = 예외 폭주, 4 = 예상 밖 예외.
 */
#include <stddef.h>
#include <stdint.h>

/* ---- shared_rt.c / shared_start.S ---- */
void sh_puts(const char *s);
void sh_hex(uint64_t v);
void sh_dec(uint64_t v);
void sh_exit(uint32_t code) __attribute__((noreturn));
void check(int ok, const char *expr, int line);
int  finish(void);
extern volatile uint64_t g_boot_el;
#define CHECK(e) check((e) ? 1 : 0, #e, __LINE__)

/* ---- <v>.S ---- */
uint64_t get_current_el(void);
uint64_t read_sctlr(void), read_tcr(void), read_mair(void), read_ttbr0(void), read_mmfr0(void), read_mmfr1(void);
void     set_vbar_el1(const void *table);
void     mmu_enable(uint64_t ttbr0, uint64_t tcr, uint64_t mair);
uint64_t at_s1e1r(uint64_t va);
uint64_t at_s1e1w(uint64_t va);
void     tlbi_va(uint64_t va);
void     tlbi_all(void);
void     dsb_ishst(void);
void     isb_sync(void);
void     sync_code(void *p);
uint64_t load64(uint64_t va);
void     store64(uint64_t va, uint64_t v);
uint32_t load32(uint64_t va);
void     store32(uint64_t va, uint32_t v);
extern const uint32_t vectors_el1[], load64_insn[], store64_insn[], load32_insn[];
extern const char _start[], __image_end[];

#define ADDR(p) ((uint64_t)(uintptr_t)(p))

/* ================= 변환 테이블 형식 (4 KB 그래뉼, stage 1, EL1&0) ================= */
#define DESC_VALID   1ull                       /* bit 0 */
#define DESC_TABLE   3ull                       /* [1:0] = 11: L0–L2 테이블 / L3 페이지 */
#define DESC_BLOCK   1ull                       /* [1:0] = 01: L1 (1 GB) / L2 (2 MB) 블록 */
#define OA_MASK      0x0000FFFFFFFFF000ull      /* 출력 주소 [47:12] */
#define D_AF         (1ull << 10)
#define D_PXN        (1ull << 53)
#define D_UXN        (1ull << 54)

#define ATTR_NORMAL  0u                         /* MAIR Attr0 = 0xFF: Normal, Inner/Outer WB, RW-allocate */
#define ATTR_DEVICE  1u                         /* MAIR Attr1 = 0x04: Device-nGnRE */
#define ATTR_NC      2u                         /* MAIR Attr2 = 0x44: Normal Non-cacheable */
#define ATTR_SO      3u                         /* MAIR Attr3 = 0x00: Device-nGnRnE */
#define MAIR_VALUE   0x00000000004404FFull

#define AP_RW_EL1    0u                         /* AP[2:1] = 00: EL1 RW, EL0 없음 */
#define AP_RW_ALL    1u                         /* 01: EL1/EL0 RW (EL1 은 자동 PXN!) */
#define AP_RO_EL1    2u                         /* 10: EL1 RO, EL0 없음 */
#define AP_RO_ALL    3u                         /* 11: EL1/EL0 RO */
#define SH_NONE      0u
#define SH_OUTER     2u
#define SH_INNER     3u

#define FSC_TRANS(l) (0x04u + (unsigned)(l))    /* translation fault, level l */
#define FSC_AF(l)    (0x08u + (unsigned)(l))    /* access flag fault */
#define FSC_PERM(l)  (0x0Cu + (unsigned)(l))    /* permission fault */
#define FSC_EXTABT   0x10u                      /* synchronous external abort */
#define FSC_ALIGN    0x21u                      /* alignment fault */

/* ================= 이 레슨의 메모리 맵 (VA → PA) ================= */
#define MB2          0x200000ull
#define GB1          0x40000000ull
#define VA_GIC       0x08000000ull              /* Device-nGnRE, XN (GICD 0x08000000 / GICC 0x08010000) */
#define VA_UART      0x09000000ull              /* PL011, Device-nGnRE, XN */
#define VA_LOWHOLE   0x0A000000ull              /* L2_low 에 없음 → L2 translation fault */
#define RAM_BASE     0x40000000ull              /* 128 MB = L2_ram[0..63] 아이덴티티 Normal WB */
#define RAM_BLOCKS   64u
#define VA_XN        0x46000000ull              /* 실행 금지 (UXN|PXN) */
#define VA_RO        0x46200000ull              /* AP = RO, XN */
#define VA_NOAF      0x46400000ull              /* AF = 0 → 첫 접근에 access flag fault */
#define VA_HOLE      0x46600000ull              /* RAM 가운데 구멍 (invalid) */
#define PA_SHARED    0x46800000ull              /* 별칭들이 가리키는 공유 버퍼 */
#define PA_SPARE     0x46A00000ull              /* 재매핑 목적지 · RAM 코드 실행 실험 */
#define VA_ALIAS     0x48000000ull              /* → PA_SHARED, Normal, XN */
#define VA_DEVALIAS  0x48200000ull              /* → PA_SHARED, Device-nGnRE, XN */
#define VA_REMAP     0x48400000ull              /* → PA_SHARED (Part C 에서 PA_SPARE 로) */
#define VA_GB        0x80000000ull              /* L1 1 GB 블록 → PA 0x40000000 (RAM 128 MB 뒤는 빈 PA) */
#define VA_L1HOLE    0xC0000000ull              /* L1[3] invalid → L1 translation fault */
#define T0SZ         25u                        /* VA 39 비트 → 시작 레벨 1 */

static uint64_t l1[512]     __attribute__((aligned(4096)));
static uint64_t l2_low[512] __attribute__((aligned(4096)));
static uint64_t l2_ram[512] __attribute__((aligned(4096)));

/* ---- Part A TODO 1: descriptor 빌더 ---- */
uint64_t mk_block(uint64_t pa, unsigned attr_idx, unsigned ap, unsigned sh, unsigned xn, unsigned af) {
    /* TODO(Part A): OA | AttrIndx[4:2] | AP[7:6] | SH[9:8] | AF[10] | PXN[53] | UXN[54] | 블록 표시 [1:0] = 01 */
    (void)pa; (void)attr_idx; (void)ap; (void)sh; (void)xn; (void)af;
    return 0;
}

uint64_t mk_table(const void *next) {
    /* TODO(Part A): 다음 레벨 테이블 주소 | [1:0] = 11 */
    (void)next;
    return 0;
}

/* ---- Part A TODO 2: TCR_EL1 값 ---- */
uint64_t make_tcr(unsigned t0sz, uint64_t mmfr0) {
    /* TODO(Part A): T0SZ, IRGN0/ORGN0 = WB, SH0 = Inner, TG0 = 4 KB, EPD1 = 1, TG1 = 4 KB, IPS = PARange (최대 5) */
    (void)t0sz; (void)mmfr0;
    return 0;
}

/* ---- Part A TODO 3: 테이블 채우기 (위 메모리 맵 그대로) ---- */
void build_tables(void) {
    /* TODO(Part A): l1 / l2_low / l2_ram 을 위 메모리 맵대로 채운다 (구멍은 0 으로 남긴다) */
    (void)l2_low; (void)l2_ram;
}

/* ---- Part A TODO 4: 소프트웨어 page walk (하드웨어 walker 의 모델) ---- */
struct walk_res {
    uint64_t pa;
    unsigned level, attr_idx, ap, sh, af, pxn, uxn;
};

/* 반환: 0 = 변환 성공, 그 밖 = DFSC 와 같은 폴트 코드 (translation 0x04+L / access flag 0x08+L) */
unsigned walk(const uint64_t *root, unsigned t0sz, uint64_t va, struct walk_res *r) {
    /* TODO(Part A): 범위 검사 → 시작 레벨 → 레벨마다 index = (va >> shift) & 511 → 테이블이면 내려가고, 블록이면 PA·속성 */
    (void)root; (void)t0sz; (void)va; (void)r;
    return FSC_TRANS(0);
}

/* EL1 권한 검사 (주어짐): acc = 'r' / 'w' / 'x' → 0 또는 permission fault 코드 */
unsigned el1_access(const struct walk_res *r, char acc) {
    if (acc == 'w' && (r->ap & 2u)) return FSC_PERM(r->level);  /* AP[2] = 1 → 읽기 전용 */
    if (acc == 'x' && (r->pxn || r->ap == AP_RW_ALL)) return FSC_PERM(r->level);
    return 0u;
}

/* 주어짐: va 의 마지막(leaf) descriptor 주소. 없으면 NULL. walk 와 같은 순서로 내려간다 */
static uint64_t *leaf_desc(uint64_t *root, unsigned t0sz, uint64_t va) {
    if (va >> (64u - t0sz)) return NULL;
    unsigned level = t0sz <= 24u ? 0u : t0sz <= 33u ? 1u : 2u;
    uint64_t *t = root;
    for (;; level++) {
        uint64_t *p = &t[(va >> (39u - 9u * level)) & 511u];
        if (!(*p & DESC_VALID)) return NULL;
        if (level < 3u && (*p & 3u) == DESC_TABLE) { t = (uint64_t *)(uintptr_t)(*p & OA_MASK); continue; }
        return p;
    }
}

/* ---- Part C TODO 5: AF 지연 설정 (리눅스의 "young" 비트처럼 소프트웨어가 AF 를 켠다) ---- */
static volatile unsigned g_lazy_af, g_af_fixups;
int af_fixup(uint64_t far) {
    /* TODO(Part C): far 의 leaf descriptor 에 AF 를 켜고 dsb → 1 (재시도). 해당 없으면 0 */
    (void)far;
    return 0;
}

/* ---- Part C TODO 6: break-before-make 재매핑 ---- */
void remap_block(uint64_t va, uint64_t new_desc) {
    /* TODO(Part C): break-before-make — invalid 로 → TLBI → 새 descriptor → dsb/isb */
    (void)va; (void)new_desc;
}

/* ================= 예외 처리 (주어짐; ARM-M13 축약판) ================= */
struct exc_frame {
    uint64_t x[19];
    uint64_t fp, lr;
    uint64_t elr, spsr, esr, far;
    uint64_t kind;
};
_Static_assert(sizeof(struct exc_frame) == 208, "FRAME = 208 in .S");
_Static_assert(offsetof(struct exc_frame, elr) == 168 && offsetof(struct exc_frame, kind) == 200, "frame layout");

#define EC_IABT_CUR 0x21u
#define EC_DABT_CUR 0x25u
struct exc_rec { uint64_t ec, fsc, wnr, elr, far, kind; };
static volatile struct exc_rec g_last;
static volatile unsigned g_exc_count;

int exc_handler(struct exc_frame *f) {
    if (++g_exc_count > 256u) {
        sh_puts("exception storm: ELR="); sh_hex(f->elr); sh_puts(" ESR="); sh_hex(f->esr); sh_puts("\n");
        sh_exit(3);
    }
    uint32_t esr = (uint32_t)f->esr;
    g_last.ec = esr >> 26;
    g_last.fsc = esr & 0x3Fu;                   /* DFSC / IFSC */
    g_last.wnr = (esr >> 6) & 1u;
    g_last.elr = f->elr; g_last.far = f->far; g_last.kind = f->kind;
    if (g_last.ec == EC_DABT_CUR) {
        unsigned fsc = esr & 0x3Fu;
        if (g_lazy_af && fsc >= 0x08u && fsc <= 0x0Bu && af_fixup(f->far))
            return 0;                           /* AF 를 켜고 같은 명령 재시도 */
        f->elr += 4u;                           /* 그 밖: 접근을 건너뛰고 "읽은 값" = 0xBAD */
        f->x[0] = 0xBADu;
        return 0;
    }
    if (g_last.ec == EC_IABT_CUR) {             /* 실행 금지 영역으로 뛰었다: 호출한 곳으로 "return" */
        f->elr = f->lr;
        f->x[0] = 0xDEADu;
        return 0;
    }
    sh_puts("unexpected exception: ESR="); sh_hex(f->esr); sh_puts(" ELR="); sh_hex(f->elr);
    sh_puts(" FAR="); sh_hex(f->far); sh_puts("\n");
    sh_exit(4);
}

/* ================= 테스트 ================= */
static void dump(const char *name, uint64_t v) { sh_puts(name); sh_hex(v); sh_puts("\n"); }

/* 기대값 = "명세" (build_tables 와 독립적으로 쓴 표) */
struct expect { unsigned fault; uint64_t pa; unsigned attr, ap, xn, level; };
static struct expect spec(uint64_t va) {
    struct expect e = { 0, 0, ATTR_NORMAL, AP_RW_EL1, 0, 2 };
    if (va >> 39) { e.fault = FSC_TRANS(0); return e; }
    uint64_t blk = va & ~(MB2 - 1u), off = va & (MB2 - 1u);
    if (va >= 0xC0000000ull)             { e.fault = FSC_TRANS(1); return e; }
    if (va >= VA_GB) { e.level = 1; e.xn = 1; e.pa = RAM_BASE + (va - VA_GB); return e; }
    if (blk == VA_GIC || blk == VA_UART) { e.attr = ATTR_DEVICE; e.xn = 1; e.pa = va; return e; }
    if (va < RAM_BASE)                   { e.fault = FSC_TRANS(2); return e; }
    if (blk == VA_ALIAS)    { e.xn = 1; e.pa = PA_SHARED + off; return e; }
    if (blk == VA_DEVALIAS) { e.xn = 1; e.attr = ATTR_DEVICE; e.pa = PA_SHARED + off; return e; }
    if (blk == VA_REMAP)    { e.xn = 1; e.pa = PA_SHARED + off; return e; }
    if (va >= RAM_BASE + RAM_BLOCKS * MB2 || blk == VA_HOLE) { e.fault = FSC_TRANS(2); return e; }
    e.pa = va;
    if (blk == VA_XN) e.xn = 1;
    if (blk == VA_RO) { e.ap = AP_RO_EL1; e.xn = 1; }
    if (blk == VA_NOAF) e.fault = FSC_AF(2);
    return e;
}

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;
static uint64_t xorshift64(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }

/* 테스트할 VA 들: 0..4 GB 의 모든 2 MB 블록 시작 + 블록당 무작위 오프셋 2 개 + 범위 밖 몇 개 */
#define N_SPECIAL 6
static const uint64_t special_va[N_SPECIAL] = {
    1ull << 39, (1ull << 39) + 0x1234, 0x0000FFFFFFFFF000ull, 0xFFFFFF8000000000ull, 0xFFFFFFFFFFFFF000ull,
    (1ull << 39) - 8u };
static uint64_t test_va(unsigned i) {           /* i < 2048*3 + N_SPECIAL */
    if (i >= 2048u * 3u) return special_va[i - 2048u * 3u];
    uint64_t base = (uint64_t)(i / 3u) * MB2;
    return (i % 3u == 0u) ? base : base + (xorshift64() & (MB2 - 1u));
}
#define N_TEST_VA (2048u * 3u + N_SPECIAL)

/* PAR_EL1 해석: F = bit 0. 성공이면 PA[47:12] + ATTR[63:56] + SH[8:7], 실패면 FST[6:1] */
static unsigned par_fault(uint64_t par) { return (par & 1u) ? (unsigned)(par >> 1) & 0x3Fu : 0u; }
static uint64_t par_pa(uint64_t par) { return par & OA_MASK; }
static unsigned par_attr(uint64_t par) { return (unsigned)(par >> 56); }

int main(void) {
    sh_puts("ARM-A03 AArch64 MMU on (QEMU virt, cortex-a53)\n");
    uint64_t mmfr0 = read_mmfr0(), mmfr1 = read_mmfr1();
    dump("  CurrentEL        = ", get_current_el());
    dump("  ID_AA64MMFR0_EL1 = ", mmfr0);
    dump("  ID_AA64MMFR1_EL1 = ", mmfr1);
    dump("  SCTLR_EL1 (reset)= ", read_sctlr());
    dump("  image            = ", ADDR(_start));
    dump("  image end        = ", ADDR(__image_end));
    CHECK(get_current_el() == 1u);
    CHECK((mmfr0 & 0xFu) == 2u);                               /* PARange = 40 비트 (QEMU cortex-a53 실측) */
    CHECK(((mmfr0 >> 28) & 0xFu) == 0u);                       /* TGran4 = 0: 4 KB 그래뉼 지원 */
    CHECK((mmfr1 & 0xFu) == 0u);                               /* HAFDBS = 0: AF 를 하드웨어가 안 켠다 */
    CHECK((ADDR(__image_end) - RAM_BASE) <= MB2);              /* 이미지 전체가 RAM 블록 0 안 */

    /* ---------- Part A-1: descriptor / TCR 골든 값 ---------- */
    CHECK(mk_block(0x40000000u, ATTR_NORMAL, AP_RW_EL1, SH_INNER, 0, 1) == 0x40000701ull);
    CHECK(mk_block(0x09000000u, ATTR_DEVICE, AP_RW_EL1, SH_NONE, 1, 1) == 0x0060000009000405ull);
    CHECK(mk_block(0x46200000u, ATTR_NORMAL, AP_RO_EL1, SH_INNER, 1, 1) == 0x0060000046200781ull);
    CHECK(mk_block(0x46400000u, ATTR_NORMAL, AP_RW_EL1, SH_INNER, 0, 0) == 0x0000000046400301ull);
    CHECK(mk_table((const void *)(uintptr_t)0x40091000u) == 0x40091003ull);
    uint64_t tcr = make_tcr(T0SZ, mmfr0);
    dump("  TCR_EL1 value    = ", tcr);
    CHECK(tcr == 0x0000000280803519ull);
    CHECK(make_tcr(16, 0x5u) == 0x0000000580803510ull);       /* 48 비트 VA / PA */
    CHECK(make_tcr(32, 0x6u) >> 32 == 5u);                     /* 52 비트 PARange → IPS 48 로 제한 */

    /* ---------- Part A-2: 테이블 + 소프트웨어 walk vs 명세 ---------- */
    build_tables();
    dump("  l1 @             = ", ADDR(l1));
    dump("  l1[0..2]         = ", l1[0]); dump("                     ", l1[1]); dump("                     ", l1[2]);
    unsigned bad = 0, n_ok = 0;
    for (unsigned i = 0; i < N_TEST_VA; i++) {
        uint64_t va = test_va(i);
        struct walk_res w = { 0 };
        struct expect e = spec(va);
        unsigned f = walk(l1, T0SZ, va, &w);
        if (f != e.fault) { bad++; continue; }
        if (f == 0u || f == FSC_AF(2)) {
            n_ok += (f == 0u);
            if (w.pa != e.pa || w.attr_idx != e.attr || w.ap != e.ap || w.pxn != e.xn || w.uxn != e.xn ||
                w.level != e.level) bad++;
        }
    }
    sh_puts("  walk vs spec: VAs = "); sh_dec(N_TEST_VA); sh_puts(", mapped = "); sh_dec(n_ok);
    sh_puts(", mismatches = "); sh_dec(bad); sh_puts("\n");
    CHECK(bad == 0u);
    struct walk_res w;
    CHECK(walk(l1, T0SZ, 0x40080123u, &w) == 0u && w.pa == 0x40080123u && w.level == 2u);
    CHECK(walk(l1, T0SZ, VA_GB + 0x6812345u, &w) == 0u && w.pa == 0x46812345u && w.level == 1u);
    CHECK(walk(l1, T0SZ, VA_HOLE + 8u, &w) == FSC_TRANS(2));
    CHECK(walk(l1, T0SZ, VA_L1HOLE, &w) == FSC_TRANS(1));
    CHECK(walk(l1, T0SZ, 1ull << 39, &w) == FSC_TRANS(0));
    CHECK(walk(l1, T0SZ, VA_NOAF, &w) == FSC_AF(2));
    CHECK(walk(l1, T0SZ, VA_RO, &w) == 0u && el1_access(&w, 'r') == 0u && el1_access(&w, 'w') == FSC_PERM(2));
    CHECK(walk(l1, T0SZ, VA_XN, &w) == 0u && el1_access(&w, 'x') == FSC_PERM(2));
    CHECK(walk(l1, 16, 0x40080000u, &w) == FSC_TRANS(1));      /* T0SZ 를 틀리면: L1 테이블을 L0 로 읽는다 */

    /* ---------- MMU 켜기 전 준비: 벡터, 데이터 심기, 프리플라이트 ---------- */
    set_vbar_el1(vectors_el1);
    store64(VA_RO + 0x10u, 0x5A5A5A5A12345678ull);            /* MMU off 상태라 RO 영역에도 쓸 수 있다 */
    store64(VA_NOAF + 0x40u, 0xAF0000000000AF01ull);
    store64(PA_SPARE + 0x100u, 0x5BA4E00000000001ull);
    int ready = 1;
    const uint64_t must_exec[] = { ADDR(_start), ADDR(main), ADDR(vectors_el1), ADDR(exc_handler) };
    const uint64_t must_rw[] = { ADDR(&g_rng), ADDR(&ready), ADDR(__image_end) - 16u, ADDR(l1), VA_UART };
    for (unsigned i = 0; i < sizeof must_exec / sizeof must_exec[0]; i++)
        ready &= walk(l1, T0SZ, must_exec[i], &w) == 0u && w.pa == must_exec[i] && el1_access(&w, 'x') == 0u;
    for (unsigned i = 0; i < sizeof must_rw / sizeof must_rw[0]; i++)
        ready &= walk(l1, T0SZ, must_rw[i], &w) == 0u && w.pa == must_rw[i] && el1_access(&w, 'w') == 0u;
#if defined(BUG_NO_AF) || defined(BUG_T0SZ16)
    sh_puts("  (bug demo: preflight ignored)\n");
    ready = 1;
#endif
    CHECK(ready);
    if (!ready) {
        sh_puts("  preflight failed: identity map would not cover code/stack/vectors -> MMU stays off\n");
        return finish();
    }

    /* ---------- MMU on ---------- */
#ifdef BUG_T0SZ16
    tcr = make_tcr(16, mmfr0);                                  /* 7. 실수 4 데모: 테이블은 L1 부터인데 T0SZ = 16 */
#endif
    mmu_enable(ADDR(l1), tcr, MAIR_VALUE);
    uint64_t sctlr = read_sctlr();
    dump("  SCTLR_EL1 (on)   = ", sctlr);
    dump("  TCR_EL1          = ", read_tcr());
    dump("  MAIR_EL1         = ", read_mair());
    dump("  TTBR0_EL1        = ", read_ttbr0());
    CHECK((sctlr & 1u) && (sctlr & 4u) && (sctlr & (1u << 12)));
    CHECK(read_tcr() == tcr && read_mair() == MAIR_VALUE && read_ttbr0() == ADDR(l1));
    if (!(sctlr & 1u)) { sh_puts("  MMU is off: remaining tests skipped\n"); return finish(); }
    sh_puts("  MMU on: still running (identity map)\n");

    /* ---------- Part A-3: 하드웨어 walker(AT) vs 소프트웨어 walk ---------- */
    uint64_t par;
    g_rng = 0x9E3779B97F4A7C15ull;
    unsigned at_bad = 0, at_faults = 0, at_af_seen = 0, at_af_ok = 0;
    for (unsigned i = 0; i < N_TEST_VA; i++) {
        uint64_t va = test_va(i);
        unsigned f = walk(l1, T0SZ, va, &w);
        uint64_t pr = at_s1e1r(va), pw = at_s1e1w(va);
        if (f == FSC_AF(2)) {                   /* AF = 0: AT 가 AF fault 를 보고하는지는 구현마다 (5. 풀이) */
            at_af_seen++;
            if (par_fault(pr) == 0u && par_pa(pr) == (w.pa & OA_MASK)) at_af_ok++;   /* QEMU 10.2: 성공으로 보고 */
            else if (par_fault(pr) == FSC_AF(2) && par_fault(pw) == FSC_AF(2)) at_af_ok++;
            continue;
        }
        unsigned exp_w = f ? f : el1_access(&w, 'w');
        at_faults += (par_fault(pr) != 0u);
        if (par_fault(pr) != f || par_fault(pw) != exp_w) { at_bad++; continue; }
        if (f == 0u && (par_pa(pr) != (w.pa & OA_MASK) ||
                        par_attr(pr) != ((MAIR_VALUE >> (8u * w.attr_idx)) & 0xFFu))) at_bad++;
    }
    sh_puts("  AT vs walk: faults = "); sh_dec(at_faults); sh_puts(", mismatches = "); sh_dec(at_bad);
    sh_puts(", AF=0 VAs = "); sh_dec(at_af_seen); sh_puts("\n");
    par = at_s1e1r(VA_NOAF);
    dump("  PAR_EL1(AF=0, r) = ", par);
    CHECK(at_af_seen == 3u && at_af_ok == at_af_seen);
    CHECK(at_bad == 0u);
    par = at_s1e1r(VA_UART);
    dump("  PAR_EL1(UART)    = ", par);
    CHECK(par_attr(par) == 0x04u && par_pa(par) == VA_UART);    /* Device-nGnRE 가 보여야 한다 */
    par = at_s1e1r(VA_ALIAS + 0x123u);
    dump("  PAR_EL1(ALIAS)   = ", par);
    CHECK(par_attr(par) == 0xFFu && par_pa(par) == PA_SHARED);
    par = at_s1e1w(VA_RO);
    dump("  PAR_EL1(RO, w)   = ", par);
    CHECK(par_fault(par) == FSC_PERM(2));

    /* ---------- Part A-4: 별칭 일관성 + UART (Device) ---------- */
    store64(PA_SHARED + 0x100u, 0x1111222233334444ull);
    CHECK(load64(VA_ALIAS + 0x100u) == 0x1111222233334444ull);
    store64(VA_ALIAS + 0x108u, 0xA1A5A1A5A1A5A1A5ull);
    CHECK(load64(PA_SHARED + 0x108u) == 0xA1A5A1A5A1A5A1A5ull);
    CHECK(load64(VA_GB + (PA_SHARED - RAM_BASE) + 0x108u) == 0xA1A5A1A5A1A5A1A5ull);   /* 1 GB 블록 경로 */
    CHECK(load64(VA_DEVALIAS + 0x100u) == 0x1111222233334444ull);
    uint32_t cid = load32(VA_UART + 0xFF0u) | load32(VA_UART + 0xFF4u) << 8 |
                   load32(VA_UART + 0xFF8u) << 16 | load32(VA_UART + 0xFFCu) << 24;
    uint32_t pid = load32(VA_UART + 0xFE0u) | load32(VA_UART + 0xFE4u) << 8 |
                   load32(VA_UART + 0xFE8u) << 16 | load32(VA_UART + 0xFECu) << 24;
    dump("  PL011 PrimeCell ID = ", cid);
    dump("  PL011 Periph ID    = ", pid);
    CHECK(cid == 0xB105F00Du);                                 /* PrimeCell 공통 ID */
    CHECK((pid & 0xFFFu) == 0x011u);                           /* part number 0x011 = PL011 */
    for (const char *s = "ARM-A03: hello from PL011 through a Device-nGnRE mapping\n"; *s; s++)
        store32(VA_UART, (uint8_t)*s);                         /* UARTDR (+0x000) */
    dump("  GICD_IIDR          = ", load32(VA_GIC + 0x008u));

    /* ---------- Part A-5: 폴트 ---------- */
    unsigned n0 = g_exc_count;
    uint64_t r = load64(VA_HOLE + 8u);
    dump("  hole   ESR.DFSC  = ", g_last.fsc);
    CHECK(r == 0xBADu && g_last.ec == EC_DABT_CUR && g_last.fsc == FSC_TRANS(2));
    CHECK(g_last.far == VA_HOLE + 8u && g_last.elr == ADDR(load64_insn) && g_last.wnr == 0u);
    r = load64(VA_L1HOLE + 0x10u);
    dump("  L1hole ESR.DFSC  = ", g_last.fsc);
    CHECK(r == 0xBADu && g_last.fsc == FSC_TRANS(1) && g_last.far == VA_L1HOLE + 0x10u);
    r = load64(1ull << 39);
    dump("  >39bit ESR.DFSC  = ", g_last.fsc);
    CHECK(r == 0xBADu && g_last.fsc == FSC_TRANS(0) && g_last.far == (1ull << 39));
    r = load64(0x0000FFFFFFFFF000ull);
    CHECK(r == 0xBADu && g_last.fsc == FSC_TRANS(0));
    r = load64(VA_LOWHOLE);
    CHECK(r == 0xBADu && g_last.fsc == FSC_TRANS(2));

    CHECK(load64(VA_RO + 0x10u) == 0x5A5A5A5A12345678ull);     /* 읽기는 된다 */
    store64(VA_RO + 0x10u, 0);
    dump("  RO wr  ESR.DFSC  = ", g_last.fsc);
    CHECK(g_last.ec == EC_DABT_CUR && g_last.fsc == FSC_PERM(2) && g_last.wnr == 1u);
    CHECK(g_last.far == VA_RO + 0x10u && g_last.elr == ADDR(store64_insn));
    CHECK(load64(VA_RO + 0x10u) == 0x5A5A5A5A12345678ull);     /* 값은 그대로 */

    r = load64(VA_NOAF + 0x40u);
    dump("  AF=0   ESR.DFSC  = ", g_last.fsc);
    CHECK(r == 0xBADu && g_last.fsc == FSC_AF(2) && g_last.far == VA_NOAF + 0x40u);

    r = load64(VA_GB + 0x10000000u);                            /* PA 0x50000000: 변환은 되지만 PA 에 아무것도 없다 */
    dump("  noPA   ESR.DFSC  = ", g_last.fsc);
    CHECK(r == 0xBADu && g_last.fsc == FSC_EXTABT && g_last.far == VA_GB + 0x10000000u);

    /* 실행 금지: 같은 코드 바이트를 XN 블록과 실행 가능 블록에 두고 불러 본다 */
    const uint64_t ret42 = 0xD65F03C0D2800540ull;               /* mov x0, #42 (d2800540) ; ret (d65f03c0) */
    store64(VA_XN, ret42);
    store64(PA_SPARE, ret42);
    sync_code((void *)(uintptr_t)VA_XN);
    sync_code((void *)(uintptr_t)PA_SPARE);
    uint64_t (*fn)(void) = (uint64_t (*)(void))(uintptr_t)PA_SPARE;
    CHECK(fn() == 42u);                                        /* 실행 가능한 블록: 정상 */
    unsigned before = g_exc_count;
    fn = (uint64_t (*)(void))(uintptr_t)VA_XN;
    r = fn();
    dump("  XN exec ESR.EC   = ", g_last.ec);
    dump("  XN exec ESR.IFSC = ", g_last.fsc);
    dump("  XN exec ELR=FAR  = ", g_last.elr);
    CHECK(r == 0xDEADu && g_exc_count == before + 1u);
    CHECK(g_last.ec == EC_IABT_CUR && g_last.fsc == FSC_PERM(2));
    CHECK(g_last.elr == VA_XN && g_last.far == VA_XN);

    /* 비정렬: Normal 은 허용 (SCTLR.A = 0), Device 는 항상 정렬 폴트 */
    store64(PA_SHARED + 0x200u, 0x8877665544332211ull);
    store64(PA_SHARED + 0x208u, 0x00FFEEDDCCBBAA99ull);
    before = g_exc_count;
    CHECK(load64(PA_SHARED + 0x201u) == 0x9988776655443322ull && g_exc_count == before);
    r = load64(VA_DEVALIAS + 0x201u);
    dump("  Dev unaligned DFSC = ", g_last.fsc);
    CHECK(r == 0xBADu && g_last.fsc == FSC_ALIGN && g_last.far == VA_DEVALIAS + 0x201u);
    CHECK(load64(VA_DEVALIAS + 0x200u) == 0x8877665544332211ull);
    sh_puts("  faults so far      = "); sh_dec(g_exc_count - n0); sh_puts("\n");

    /* ---------- Part C-1: AF 지연 설정 → 재시도 ---------- */
    g_lazy_af = 1;
    before = g_exc_count;
    r = load64(VA_NOAF + 0x40u);
    dump("  lazy AF: value     = ", r);
    CHECK(r == 0xAF0000000000AF01ull);                         /* 핸들러가 AF 를 켜고 같은 ldr 을 다시 */
    CHECK(g_af_fixups == 1u && g_exc_count == before + 1u);
    CHECK(load64(VA_NOAF + 0x48u) == 0u && g_exc_count == before + 1u);   /* 두 번째부터는 폴트 없음 */
    CHECK(walk(l1, T0SZ, VA_NOAF, &w) == 0u && par_fault(at_s1e1r(VA_NOAF)) == 0u);
    g_lazy_af = 0;

    /* ---------- Part C-2: break-before-make 재매핑 ---------- */
    store64(PA_SHARED + 0x300u, 0x00000000000000AAull);
    store64(PA_SPARE + 0x300u, 0x00000000000000BBull);
    CHECK(load64(VA_REMAP + 0x300u) == 0xAAu);                 /* TLB 에 VA_REMAP → PA_SHARED 가 들어간다 */
    remap_block(VA_REMAP, mk_block(PA_SPARE, ATTR_NORMAL, AP_RW_EL1, SH_INNER, 1, 1));
    r = load64(VA_REMAP + 0x300u);
    dump("  remap (BBM) read   = ", r);
    CHECK(r == 0xBBu);
    CHECK(walk(l1, T0SZ, VA_REMAP + 0x300u, &w) == 0u && w.pa == PA_SPARE + 0x300u);
    CHECK(par_pa(at_s1e1r(VA_REMAP)) == PA_SPARE);

    /* 관찰: TLBI 없이 descriptor 만 바꾸면? (QEMU 의 소프트 TLB 가 옛 변환을 들고 있는지) */
    uint64_t *p = leaf_desc(l1, T0SZ, VA_REMAP);
    *p = mk_block(PA_SHARED, ATTR_NORMAL, AP_RW_EL1, SH_INNER, 1, 1);
    dsb_ishst();
    isb_sync();
    uint64_t stale = load64(VA_REMAP + 0x300u);
    uint64_t at_now = par_pa(at_s1e1r(VA_REMAP));
    dump("  no-TLBI read       = ", stale);
    dump("  no-TLBI AT PA      = ", at_now);
    tlbi_va(VA_REMAP);
    r = load64(VA_REMAP + 0x300u);
    dump("  after TLBI read    = ", r);
    CHECK(r == 0xAAu);                                         /* TLBI 뒤에는 반드시 새 변환 */

    sh_puts("  exceptions taken   = "); sh_dec(g_exc_count); sh_puts("\n");
    return finish();
}
