// C18_linear_translate.c — 선형 페이지 테이블 주소 변환 + 메모리 트레이스 카운터
// (OSTEP Ch.18 의 Figure 18.2/18.3 예제와 Figure 18.7 array 루프 트레이스를 C로 재현)
#include <stdio.h>
#include <stdint.h>

// ---- PTE 포맷 (교육용, 32-bit) : [31]=Valid [30]=R/W [29]=U/S [28:0]=PFN
#define PTE_VALID  (1u << 31)
#define PTE_RW     (1u << 30)
#define PTE_USER   (1u << 29)
#define PTE_PFN(x) ((x) & 0x1FFFFFFFu)

typedef struct {
    unsigned offset_bits;       // 페이지 크기 = 2^offset_bits
    unsigned vpn_bits;          // VPN 비트 수
    const uint32_t *table;      // 선형 페이지 테이블 (PTBR 가 가리키는 배열)
    uint32_t ptbr;              // 페이지 테이블의 "물리 주소" (트레이스용)
} mmu_t;

static long pt_accesses, data_accesses;   // 메모리 접근 카운터

// Figure 18.6 그대로: VPN 추출 → PTE 주소 → PTE 읽기 → 검사 → PA 조립
static int translate(const mmu_t *m, uint32_t va, uint32_t *pa, int verbose)
{
    uint32_t offset_mask = (1u << m->offset_bits) - 1;
    uint32_t vpn_mask    = ((1u << m->vpn_bits) - 1) << m->offset_bits;
    uint32_t vpn    = (va & vpn_mask) >> m->offset_bits;
    uint32_t pteaddr = m->ptbr + vpn * (uint32_t)sizeof(uint32_t);
    uint32_t pte    = m->table[vpn];
    pt_accesses++;                                   // 페이지 테이블 접근 1회
    if (!(pte & PTE_VALID)) {
        if (verbose) printf("  VA %5u -> VPN %u : INVALID -> SEGMENTATION_FAULT\n", va, vpn);
        return -1;
    }
    uint32_t off = va & offset_mask;
    *pa = (PTE_PFN(pte) << m->offset_bits) | off;
    if (verbose)
        printf("  VA %5u (0x%04x) -> VPN %u, off %2u | PTEaddr %u | PFN %u -> PA %u (0x%04x)\n",
               va, va, vpn, off, pteaddr, PTE_PFN(pte), *pa, *pa);
    return 0;
}

static void print_bits(uint32_t v, int nbits)
{
    for (int b = nbits - 1; b >= 0; b--) putchar((v >> b) & 1 ? '1' : '0');
}

int main(void)
{
    // ---------- 1) 책 예제: 64B 주소공간, 16B 페이지, 128B 물리메모리 ----------
    // VP0->PF3, VP1->PF7, VP2->PF5, VP3->PF2
    const uint32_t tiny_pt[4] = {
        PTE_VALID | PTE_RW | PTE_USER | 3, PTE_VALID | PTE_RW | PTE_USER | 7,
        PTE_VALID | PTE_RW | PTE_USER | 5, PTE_VALID | PTE_RW | PTE_USER | 2 };
    mmu_t tiny = { .offset_bits = 4, .vpn_bits = 2, .table = tiny_pt, .ptbr = 0 };

    printf("[1] 64B AS / 16B page (book Fig 18.3)\n");
    uint32_t pa;
    translate(&tiny, 21, &pa, 1);
    printf("      VA 21 = "); print_bits(21, 6);
    printf("  ->  PA %u = ", pa); print_bits(pa, 7); printf("\n");
    for (uint32_t va = 0; va < 64; va += 15) translate(&tiny, va, &pa, 1);

    // ---------- 2) 페이지 테이블 크기 계산 ----------
    printf("\n[2] linear page table size = 2^(VA bits - offset bits) * PTE size\n");
    struct { int va_bits; int page_log2; int pte_bytes; const char *name; } cfg[] = {
        {32, 12, 4, "32-bit, 4KB page, 4B PTE"},
        {32, 14, 4, "32-bit, 16KB page, 4B PTE"},
        {48, 12, 8, "48-bit, 4KB page, 8B PTE (x86-64 if linear!)"},
        {48, 14, 8, "48-bit, 16KB page, 8B PTE (arm64 Apple, if linear!)"},
    };
    for (unsigned i = 0; i < sizeof cfg / sizeof cfg[0]; i++) {
        unsigned long long entries = 1ULL << (cfg[i].va_bits - cfg[i].page_log2);
        unsigned long long bytes = entries * (unsigned long long)cfg[i].pte_bytes;
        printf("  %-52s : %llu entries x %dB = %llu bytes (%.1f MB)\n", cfg[i].name,
               entries, cfg[i].pte_bytes, bytes, bytes / (1024.0 * 1024.0));
    }

    // ---------- 3) Figure 18.7 메모리 트레이스 : 64KB AS, 1KB page ----------
    // PT 는 물리주소 1024 에 있음. 코드 VPN1->PFN4, 배열 VPN39..42 -> PFN7..10
    static uint32_t pt64[64];
    pt64[1] = PTE_VALID | PTE_USER | 4;
    for (int v = 39; v <= 42; v++) pt64[v] = PTE_VALID | PTE_RW | PTE_USER | (uint32_t)(v - 32);
    mmu_t m64 = { .offset_bits = 10, .vpn_bits = 6, .table = pt64, .ptbr = 1024 };

    printf("\n[3] array[1000]=0 loop trace (book Fig 18.7), first iteration in detail\n");
    const uint32_t code_va[4] = { 1024, 1028, 1032, 1036 };   // mov, inc, cmp, jne
    const char *name[4] = { "mov", "inc", "cmp", "jne" };
    pt_accesses = data_accesses = 0;
    for (int i = 0; i < 1000; i++) {
        int v = (i == 0);
        for (int k = 0; k < 4; k++) {
            if (v) printf("  fetch %-3s:", name[k]);
            translate(&m64, code_va[k], &pa, v);     // 명령어 fetch 의 변환
            data_accesses++;                         // 명령어 fetch 자체
            if (k == 0) {                            // mov 의 배열 store
                uint32_t ava = 40000 + 4u * (uint32_t)i;
                if (v) printf("  store a[0]:");
                translate(&m64, ava, &pa, v);
                data_accesses++;
            }
        }
    }
    printf("  total over 1000 iterations: page-table reads=%ld, real accesses=%ld, sum=%ld (%.1f per iter)\n",
           pt_accesses, data_accesses, pt_accesses + data_accesses,
           (pt_accesses + data_accesses) / 1000.0);
    return 0;
}
