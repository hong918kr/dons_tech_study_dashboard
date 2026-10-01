/*
 * C0B_toy_vmm.c
 * OSTEP 부록 B (Virtual Machine Monitors) — trap-and-emulate 장난감 시뮬레이터
 *
 * 하드웨어: software-managed TLB (MIPS 스타일, 4 엔트리), 모드 = USER / GUEST_KERNEL / VMM.
 *   - TLB 쓰기(tlbwr)는 privileged 명령 → VMM 모드가 아니면 trap.
 *   - 게스트 OS는 "자기가 커널" 이라고 믿지만 실제로는 덜 특권적인 모드에서 돈다.
 *
 * 메모리 매핑은 책 Figure B.1 그대로:
 *   게스트 OS page table : VPN0->PFN10, VPN2->PFN3, VPN3->PFN8  (VPN1은 invalid)
 *   VMM pmap (PFN->MFN)  : PFN3->MFN6,  PFN8->MFN10, PFN10->MFN5
 *
 * 실험
 *   A) native: OS가 직접 하드웨어 위에서 TLB miss 처리
 *   B) virtualized: VMM이 TLB miss를 받아 게스트 핸들러로 반사(reflect),
 *      게스트의 tlbwr(VPN->PFN)를 trap으로 가로채 VPN->MFN 으로 바꿔 설치 (Table B.5)
 *   C) B + Disco식 VMM "software TLB" (machine switch 후 TLB가 비었을 때 효과)
 *   D) hardware-walked TLB를 위한 shadow page table 합성 (VPN->MFN)
 *
 * build: cc -Wall -Wextra -O0 code/C0B_toy_vmm.c -o .work/bin/C0B_toy_vmm
 */
#include <stdio.h>
#include <string.h>

#define NVPN 4
#define NPFN 16
#define TLBN 4
#define INVALID -1

static const int guest_pt[NVPN] = { 10, INVALID, 3, 8 };  /* VPN -> PFN */
static int pmap[NPFN];                                      /* PFN -> MFN */

struct tlbe { int valid, vpn, frame; };
static struct tlbe tlb[TLBN];
static int tlb_next;

/* 소프트웨어 TLB (Disco): VMM이 본 VPN->MFN 매핑 캐시 */
static int stlb[NVPN];

/* 통계 */
static int n_tlb_miss, n_vmm_traps, n_guest_handler, n_world_switch;
static int verbose;

#define LOG(...) do { if (verbose) printf(__VA_ARGS__); } while (0)

static void tlb_flush(void)
{
    memset(tlb, 0, sizeof tlb);
    tlb_next = 0;
}

static int tlb_lookup(int vpn)
{
    for (int i = 0; i < TLBN; i++)
        if (tlb[i].valid && tlb[i].vpn == vpn) return tlb[i].frame;
    return INVALID;
}

/* 진짜 하드웨어 TLB에 쓰는 동작 (privileged) */
static void hw_tlbwr(int vpn, int frame)
{
    tlb[tlb_next] = (struct tlbe){ 1, vpn, frame };
    tlb_next = (tlb_next + 1) % TLBN;
}

/* ---------------- A) native ---------------- */
static int native_access(int vpn)
{
    int f = tlb_lookup(vpn);
    if (f != INVALID) { LOG("    VPN%d: TLB hit -> PFN%d\n", vpn, f); return f; }
    n_tlb_miss++;
    LOG("    VPN%d: TLB miss -> trap to OS handler\n", vpn);
    n_guest_handler++;
    int pfn = guest_pt[vpn];
    if (pfn == INVALID) { LOG("      OS: VPN%d invalid -> segfault\n", vpn); return INVALID; }
    hw_tlbwr(vpn, pfn);                         /* OS가 커널 모드라 바로 설치 */
    LOG("      OS: tlbwr VPN%d->PFN%d, rett; retry -> hit\n", vpn, pfn);
    return pfn;
}

/* ---------------- B/C) virtualized ---------------- */
/* 게스트 OS가 tlbwr를 실행하면: 게스트는 특권이 없으므로 trap → VMM이 대신 처리 */
static void guest_tlbwr_traps(int vpn, int pfn)
{
    n_vmm_traps++;
    int mfn = pmap[pfn];
    LOG("      [trap] guest tried tlbwr VPN%d->PFN%d; VMM installs VPN%d->MFN%d\n",
        vpn, pfn, vpn, mfn);
    hw_tlbwr(vpn, mfn);
    stlb[vpn] = mfn;                            /* 소프트웨어 TLB에 기록 */
}

static int virt_access(int vpn, int use_stlb)
{
    int f = tlb_lookup(vpn);
    if (f != INVALID) { LOG("    VPN%d: TLB hit -> MFN%d\n", vpn, f); return f; }
    n_tlb_miss++;
    n_vmm_traps++;                              /* 하드웨어 trap은 항상 VMM으로 */
    LOG("    VPN%d: TLB miss -> trap to VMM\n", vpn);

    if (use_stlb && stlb[vpn] != INVALID) {
        hw_tlbwr(vpn, stlb[vpn]);
        LOG("      VMM: software-TLB hit, install VPN%d->MFN%d directly, rett\n",
            vpn, stlb[vpn]);
        return stlb[vpn];
    }
    /* 게스트의 TLB miss 핸들러로 "반사" (권한을 낮춰서 점프) */
    n_world_switch++;
    n_guest_handler++;
    LOG("      VMM: reflect to guest OS TLB handler (reduced privilege)\n");
    int pfn = guest_pt[vpn];
    if (pfn == INVALID) {
        LOG("      guest OS: VPN%d invalid -> deliver segfault to its process\n", vpn);
        n_vmm_traps++;                          /* 게스트의 rett도 trap */
        return INVALID;
    }
    guest_tlbwr_traps(vpn, pfn);
    n_vmm_traps++;                              /* 게스트가 rett 실행 → 또 trap */
    LOG("      [trap] guest rett; VMM does real return-from-trap; retry -> hit\n");
    return pmap[pfn];
}

static void reset_stats(void)
{
    n_tlb_miss = n_vmm_traps = n_guest_handler = n_world_switch = 0;
}

static void stats(const char *name)
{
    printf("  -> %-32s TLB misses=%d, traps into VMM=%d, guest handler runs=%d\n",
           name, n_tlb_miss, n_vmm_traps, n_guest_handler);
}

int main(void)
{
    for (int i = 0; i < NPFN; i++) pmap[i] = INVALID;
    pmap[3] = 6; pmap[8] = 10; pmap[10] = 5;   /* Figure B.1 */

    const int refs[] = { 0, 2, 3, 0, 2, 3, 1 };
    const int nrefs = (int)(sizeof refs / sizeof refs[0]);

    printf("Reference string (VPN): 0 2 3 0 2 3 1   (VPN1 is invalid)\n\n");

    /* A */
    printf("== A. Native OS on bare hardware ==\n");
    verbose = 1; tlb_flush(); reset_stats();
    for (int i = 0; i < nrefs; i++) native_access(refs[i]);
    stats("native");

    /* B */
    printf("\n== B. Same OS on a VMM (trap-and-emulate, Table B.5) ==\n");
    tlb_flush(); reset_stats();
    for (int i = 0; i < NVPN; i++) stlb[i] = INVALID;
    for (int i = 0; i < nrefs; i++) virt_access(refs[i], 0);
    stats("virtualized");

    /* C: machine switch가 TLB를 비운 뒤 다시 실행 */
    printf("\n== C. After a machine switch (TLB flushed), rerun refs 0 2 3 x10 ==\n");
    verbose = 0;
    const char *names[] = { "virtualized, no software TLB", "virtualized + software TLB" };
    for (int use = 0; use <= 1; use++) {
        reset_stats();
        for (int i = 0; i < NVPN; i++) stlb[i] = INVALID;
        tlb_flush();
        for (int i = 0; i < 3; i++) virt_access(refs[i], 0);   /* 1회 워밍업: stlb 채워짐 */
        reset_stats();
        for (int round = 0; round < 10; round++) {
            tlb_flush();                        /* 다른 VM이 돌다 돌아옴 */
            for (int i = 0; i < 3; i++) virt_access(refs[i], use);
        }
        stats(names[use]);
    }

    /* D: shadow page table */
    printf("\n== D. Shadow page table for a hardware-walked TLB ==\n");
    printf("  VPN | guest PT (VPN->PFN) | VMM pmap (PFN->MFN) | shadow PT (VPN->MFN)\n");
    for (int v = 0; v < NVPN; v++) {
        int pfn = guest_pt[v];
        if (pfn == INVALID)
            printf("   %d  |       invalid       |          -          |   invalid\n", v);
        else
            printf("   %d  |       PFN %2d        |       MFN %2d        |   MFN %2d\n",
                   v, pfn, pmap[pfn], pmap[pfn]);
    }
    printf("  (HW walks only the shadow PT; VMM write-protects the guest PT so every\n"
           "   guest PTE update traps and the VMM can patch the shadow entry.)\n");
    return 0;
}
