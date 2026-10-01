// C20_multilevel.c — 2단계 페이지 테이블 워크 (OSTEP Fig 20.5 예제) + 레벨 수 / 테이블 크기 계산
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// ---------------- 1) 책 예제: 16KB 주소공간, 64B 페이지, 4B PTE ----------------
// VA 14bit = VPN 8bit (PDIndex 4 | PTIndex 4) + offset 6bit
#define OFF_BITS 6
#define PAGE     (1u << OFF_BITS)          // 64 B
#define NFRAMES  128
static uint8_t  phys[NFRAMES * PAGE];      // "물리 메모리" 8KB
static int      mem_reads;                 // 페이지 테이블 워크 중 메모리 읽기 횟수

typedef struct { uint32_t pfn : 24, valid : 1, prot : 3; } entry_t;   // PDE/PTE 공통 (4B)

static entry_t *frame_entries(uint32_t pfn) { return (entry_t *)&phys[pfn * PAGE]; }

static entry_t read_entry(uint32_t addr)    // 물리 주소에서 4B 엔트리 읽기
{
    entry_t e; memcpy(&e, &phys[addr], sizeof e); mem_reads++; return e;
}

static int walk(uint32_t pdbr_pfn, uint32_t va)
{
    uint32_t vpn = va >> OFF_BITS, off = va & (PAGE - 1);
    uint32_t pdi = (vpn >> 4) & 0xF, pti = vpn & 0xF;
    mem_reads = 0;
    uint32_t pde_addr = (pdbr_pfn << OFF_BITS) + pdi * (uint32_t)sizeof(entry_t);
    entry_t pde = read_entry(pde_addr);
    printf("VA 0x%04x : VPN %3u (PDI %2u, PTI %2u) off %2u | PDEaddr 0x%04x valid %u",
           va, vpn, pdi, pti, off, pde_addr, pde.valid);
    if (!pde.valid) { printf(" -> FAULT (PDE invalid)  [reads %d]\n", mem_reads); return -1; }
    uint32_t pte_addr = ((uint32_t)pde.pfn << OFF_BITS) + pti * (uint32_t)sizeof(entry_t);
    entry_t pte = read_entry(pte_addr);
    printf(" -> PT@PFN %u, PTEaddr 0x%04x valid %u", (unsigned)pde.pfn, pte_addr, pte.valid);
    if (!pte.valid) { printf(" -> FAULT (PTE invalid)  [reads %d]\n", mem_reads); return -1; }
    uint32_t pa = ((uint32_t)pte.pfn << OFF_BITS) | off;
    printf(" -> PFN %u -> PA 0x%04x  [reads %d]\n", (unsigned)pte.pfn, pa, mem_reads);
    return (int)pa;
}

static void build_book_example(void)
{
    // 페이지 디렉터리: PFN 99 (아무 빈 프레임), PDE[0] -> PFN 100, PDE[15] -> PFN 101
    entry_t *pd = frame_entries(99);
    pd[0]  = (entry_t){ .pfn = 100, .valid = 1 };
    pd[15] = (entry_t){ .pfn = 101, .valid = 1 };
    entry_t *pt0 = frame_entries(100);       // VPN 0..15
    pt0[0] = (entry_t){ 10, 1, 5 };  pt0[1] = (entry_t){ 23, 1, 5 };   // code r-x
    pt0[4] = (entry_t){ 80, 1, 6 };  pt0[5] = (entry_t){ 59, 1, 6 };   // heap rw-
    entry_t *pt15 = frame_entries(101);      // VPN 240..255
    pt15[14] = (entry_t){ 55, 1, 6 }; pt15[15] = (entry_t){ 45, 1, 6 }; // stack rw-
}

// ---------------- 2) 레벨 수 계산 ----------------
static void levels(const char *name, int va_bits, int page_log2, int pte_bytes)
{
    int per_level = page_log2 - (pte_bytes == 8 ? 3 : 2);   // 한 페이지에 들어가는 엔트리 수의 log2
    int vpn_bits = va_bits - page_log2;
    int n = (vpn_bits + per_level - 1) / per_level;
    int top = vpn_bits - per_level * (n - 1);
    printf("  %-34s VPN %2d bits, %2d bits/level -> %d levels (top level uses %d bits = %d entries)\n",
           name, vpn_bits, per_level, n, top, 1 << top);
}

// ---------------- 3) 희소 주소공간에서 선형 vs 2단계 메모리 ----------------
static void sparse_compare(void)
{
    // 32-bit, 4KB 페이지, 4B PTE: 1024 PTE/page, PD 1024 엔트리
    // 사용 영역: code 0x00400000 4MB, heap 0x10000000 64MB, stack 0xBF800000 8MB
    struct { uint32_t base, size; } r[3] = {
        { 0x00400000u, 4u << 20 }, { 0x10000000u, 64u << 20 }, { 0xBF800000u, 8u << 20 } };
    static uint8_t pt_page_used[1024];
    long used_pages = 0;
    for (int i = 0; i < 3; i++)
        for (uint64_t va = r[i].base; va < (uint64_t)r[i].base + r[i].size; va += 4096) {
            pt_page_used[va >> 22] = 1; used_pages++;
        }
    int n = 0; for (int i = 0; i < 1024; i++) n += pt_page_used[i];
    printf("  used virtual pages: %ld (%.0f MB of 4096 MB)\n", used_pages, used_pages * 4.0 / 1024);
    printf("  linear table : 1024 pages of PTEs = %d KB\n", 1024 * 4);
    printf("  two-level    : 1 PD page + %d PT pages = %d KB  (%.1f%% of linear)\n",
           n, (1 + n) * 4, 100.0 * (1 + n) / 1024);
}

int main(void)
{
    printf("[1] two-level walk, book example (PDBR = PFN 99)\n");
    build_book_example();
    uint32_t tests[] = { 0x3F80, 0x3FFF, 0x0000, 0x0145, 0x0080, 0x2000 };
    for (unsigned i = 0; i < sizeof tests / sizeof tests[0]; i++) walk(99, tests[i]);

    printf("\n[2] how many levels so that every table fits in one page?\n");
    levels("book: 30-bit VA, 512B page, 4B PTE", 30, 9, 4);
    levels("x86 32-bit, 4KB, 4B PTE", 32, 12, 4);
    levels("x86-64 48-bit, 4KB, 8B PTE", 48, 12, 8);
    levels("x86-64 57-bit (LA57), 4KB, 8B", 57, 12, 8);
    levels("arm64 48-bit, 16KB granule, 8B", 48, 14, 8);
    levels("arm64 47-bit, 16KB granule, 8B", 47, 14, 8);
    levels("arm64 39-bit, 4KB granule, 8B", 39, 12, 8);

    printf("\n[3] sparse 32-bit address space: linear vs two-level\n");
    sparse_compare();
    return 0;
}
