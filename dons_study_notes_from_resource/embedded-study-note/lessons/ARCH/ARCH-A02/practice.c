/* ARCH-A02 추가 연습 — 한 번에 검증
 *
 * solution.c 의 MMU·TLB·할당기를 그대로 가져와(main 만 이름을 바꿔 포함) 4 개 연습의 답을 검사한다.
 *   연습 1  TLB 도달 범위(reach): 8 MB 를 4 KB 페이지 vs 2 MB 블록으로 매핑해 랜덤 접근 → 미스율과 워크 읽기
 *   연습 2  페이지 워크 캐시(PWC): 상위 단계 테이블 디스크립터를 캐시하면 4 단계 워크가 몇 번 읽기로 줄어드나
 *   연습 3  break-before-make: 4 KB 페이지들을 2 MB 블록으로 바꿀 때 무효화를 빼먹으면 한 VA 에 엔트리 2 개(멀티 히트)
 *   연습 4  ASID 롤오버 공식: RR 스케줄에서 P > 2^b − 1 이면 롤오버 = floor((S − 1) / (2^b − 1)), "257 번째 프로세스"
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr   (solution.c 와 같은 폴더에서)
 */
#define main solution_main
#include "solution.c"
#undef main

/* ---------- 연습 1: TLB reach ---------- */
static void ex1(void) {
    printf("== 연습 1: TLB reach — 8 MB 랜덤 접근, TLB 64 LRU, 3-level, 50000 접근 ==\n");
    for (int blk = 0; blk < 2; blk++) {
        mmu_t M0, *M = &M0;
        if (mmu_init(M, 3, 64, R_LRU)) return;
        proc_t p = proc_new(M, 1, 1);
        int e = mmu_map(M, p.ttbr, 0x40000000ull, 0x80000000ull, 8ull << 20, D_R | D_W | D_U, blk ? 0 : MAP_4K);
        context_switch(M, &p, SW_ASID);
        uint32_t s = 777u; long bad = 0;
        for (int i = 0; i < 50000; i++) acc_chk(M, 0x40000000ull + (xs32(&s) % (8u << 20)), ACC_R | ACC_USER, &bad);
        double mr = (double)M->st.misses / (double)M->st.accesses;
        printf("  %s: reach = 64 x %s = %s | misses=%llu (%.1f%%) walk_reads=%llu tables=%u frames\n",
               blk ? "2MB blocks" : "4KB pages ", blk ? "2 MB" : "4 KB", blk ? "128 MB" : "256 KB",
               (unsigned long long)M->st.misses, 100.0 * mr, (unsigned long long)M->st.walk_reads, M->next_frame - 2);
        CHECK(e == E_OK && bad == 0);
        if (blk) CHECK(M->st.misses == 4 && M->st.walk_reads == 8 && M->next_frame - 2 == 1);
        else CHECK(mr > 0.955 && mr < 0.985 && M->st.walk_reads == 3 * M->st.misses && M->next_frame - 2 == 5);
        mmu_free(M);
    }
}

/* ---------- 연습 2: 페이지 워크 캐시 ---------- */
#define PWC_N 16
typedef struct { bool valid; uint64_t ttbr; int level; uint64_t tag; uint64_t desc; uint64_t stamp; } pwce_t;
typedef struct { pwce_t e[PWC_N]; uint64_t clock, hits; } pwc_t;

/* 테이블 디스크립터(단계 L, L < 3)는 va >> lvl_shift(L) 로 정해진다 → 그것을 태그로 캐시.
 * 가장 깊은 단계의 PWC 히트부터 워크를 이어 간다. 결과는 walk() 와 같아야 한다. */
static xlate_t walk_pwc(const mmu_t *M, pwc_t *C, uint64_t ttbr, uint64_t va, int acc, uint64_t *reads) {
    int start = 4 - M->levels;
    if (va >> va_bits(M)) return walk(M, ttbr, va, acc, reads);
    int from = start; uint64_t table = ttbr;
    for (int L = 2; L >= start; L--) {                        /* 깊은 단계부터 찾는다 */
        for (int i = 0; i < PWC_N; i++) {
            pwce_t *e = &C->e[i];
            if (e->valid && e->ttbr == ttbr && e->level == L && e->tag == (va >> lvl_shift(L))) {
                e->stamp = ++C->clock; C->hits++; from = L + 1; table = e->desc & D_OA; L = -1; break;
            }
        }
    }
    xlate_t r; memset(&r, 0, sizeof r); r.va = va; r.acc = acc;
    for (int L = from; L <= 3; L++) {
        unsigned sh = lvl_shift(L);
        uint64_t d = rd64(M, table + ((va >> sh) & 511u) * 8u);
        (*reads)++;
        if (!(d & D_V)) { r.fault = F_TRANSLATION; r.level = L; return r; }
        if (L < 3 && (d & D_T)) {                             /* 테이블 디스크립터 → PWC 에 넣는다 (LRU) */
            int v = 0;
            for (int i = 0; i < PWC_N; i++) {
                if (!C->e[i].valid) { v = i; break; }
                if (C->e[i].stamp < C->e[v].stamp) v = i;
            }
            C->e[v] = (pwce_t){ true, ttbr, L, va >> sh, d, ++C->clock };
            table = d & D_OA; continue;
        }
        if (L == 0 || (L == 3 && !(d & D_T))) { r.fault = F_TRANSLATION; r.level = L; return r; }
        r.leaf_level = L; r.level = L; r.size = 1ull << sh; r.perms = d & D_PERM; r.global = (d & D_G) != 0;
        r.pa = (d & D_OA & ~(r.size - 1)) | (va & (r.size - 1));
        r.fault = perm_check(r.perms, acc);
        return r;
    }
    r.fault = F_TRANSLATION; return r;
}

static void ex2(void) {
    printf("== 연습 2: 페이지 워크 캐시 — 4-level, 8 MB 4KB 랜덤, TLB 64 + PWC %d ==\n", PWC_N);
    mmu_t M0, *M = &M0;
    if (mmu_init(M, 4, 64, R_LRU)) return;
    proc_t p = proc_new(M, 1, 1);
    int e = mmu_map(M, p.ttbr, 0x40000000ull, 0x80000000ull, 8ull << 20, D_R | D_U, MAP_4K);
    e |= mmu_map(M, p.ttbr, 0x7F0000000000ull, 0x90000000ull, 64 * PAGE_SIZE, D_R | D_U, MAP_4K);  /* 먼 영역 하나 더 */
    context_switch(M, &p, SW_ASID);
    static pwc_t C;
    uint64_t misses = 0, reads_plain = 0, reads_pwc = 0; long diff = 0;
    uint32_t s = 4242u;
    for (int i = 0; i < 50000; i++) {
        uint32_t r = xs32(&s);
        uint64_t va = (r & 7) ? 0x40000000ull + (xs32(&s) % (8u << 20)) : 0x7F0000000000ull + (xs32(&s) % (64u << 12));
        if (tlb_lookup(&M->tlb, va, M->cur_asid) >= 0) continue;   /* TLB 히트는 워크 없음 */
        misses++;
        xlate_t a = walk(M, M->cur_ttbr, va, ACC_R | ACC_USER, &reads_plain);
        xlate_t b = walk_pwc(M, &C, M->cur_ttbr, va, ACC_R | ACC_USER, &reads_pwc);
        if (!same_xlate(&a, &b)) diff++;
        if (a.fault == F_NONE) tlb_insert(&M->tlb, &a, M->cur_asid);
    }
    printf("  TLB misses=%llu  reads/miss: plain %.3f, with PWC %.3f (PWC hits=%llu), mismatches=%ld\n",
           (unsigned long long)misses, (double)reads_plain / (double)misses, (double)reads_pwc / (double)misses,
           (unsigned long long)C.hits, diff);
    CHECK(e == E_OK && diff == 0 && reads_plain == 4 * misses);
    CHECK(reads_pwc < misses + misses / 50);                     /* 거의 1 읽기/미스 */
    mmu_free(M);
}

/* ---------- 연습 3: break-before-make ---------- */
static int tlb_match_count(const tlb_t *T, uint64_t va, uint16_t asid) {
    int c = 0;
    for (int i = 0; i < T->n; i++) {
        const tlbe_t *e = &T->e[i];
        c += e->valid && (e->global || e->asid == asid) && (va >> e->shift) == e->vpn;
    }
    return c;
}
static void ex3(void) {
    printf("== 연습 3: 4KB 페이지 512 개 → 2MB 블록 재매핑 (TLB 64) ==\n");
    for (int bbm = 0; bbm < 2; bbm++) {
        mmu_t M0, *M = &M0;
        if (mmu_init(M, 3, 64, R_LRU)) return;
        proc_t p = proc_new(M, 1, 1);
        int e = mmu_map(M, p.ttbr, 0x200000ull, 0x40000000ull, 2ull << 20, D_R | D_U, MAP_4K);
        context_switch(M, &p, SW_ASID);
        for (int pg = 0; pg < 10; pg++) mmu_access(M, 0x200000ull + (uint64_t)pg * PAGE_SIZE, ACC_R | ACC_USER);
        e |= mmu_unmap(M, p.ttbr, 0x200000ull, 2ull << 20);            /* break */
        if (bbm) for (int pg = 0; pg < 512; pg++) tlbi_va_asid(M, 0x200000ull + (uint64_t)pg * PAGE_SIZE, 1);
        /* L2 슬롯에 이미 L3 테이블이 있어 블록을 못 놓는다 → 실제 OS 처럼 테이블 디스크립터를 지운다(여기선 직접) */
        uint64_t l1 = rd64(M, p.ttbr) & D_OA;                           /* 3-level: 루트 = L1, VA 0x200000 의 L1 인덱스 0 */
        wr64(M, l1 + ((0x200000ull >> lvl_shift(2)) & 511u) * 8u, 0);
        if (bbm) tlbi_asid(M, 1);                                       /* 테이블 제거 뒤 한 번 더 (워크 캐시까지 지우는 의미) */
        e |= mmu_map(M, p.ttbr, 0x200000ull, 0x60000000ull, 2ull << 20, D_R | D_U, 0);   /* make: 블록, 다른 PA */
        mmu_access(M, 0x200000ull + 100 * PAGE_SIZE, ACC_R | ACC_USER);  /* 블록 엔트리가 TLB 로 */
        int worst = 0;
        for (int pg = 0; pg < 512; pg++) {
            int c = tlb_match_count(&M->tlb, 0x200000ull + (uint64_t)pg * PAGE_SIZE, 1);
            if (c > worst) worst = c;
        }
        xlate_t r = mmu_access(M, 0x200000ull + 3 * PAGE_SIZE, ACC_R | ACC_USER);
        printf("  %s: max entries matching one VA = %d, page 3 → pa 0x%llx (want 0x60003000)\n",
               bbm ? "with TLBI (BBM)   " : "no TLBI (make only)", worst, (unsigned long long)r.pa);
        CHECK(e == E_OK);
        if (bbm) CHECK(worst == 1 && r.pa == 0x60003000ull);
        else CHECK(worst == 2 && r.pa == 0x40003000ull);
        mmu_free(M);
    }
}

/* ---------- 연습 4: ASID 롤오버 공식 ---------- */
static void ex4(void) {
    printf("== 연습 4: RR 롤오버 공식 floor((S-1)/(2^b-1)) vs 시뮬레이터 ==\n");
    int ok = 0, n = 0;
    for (int b = 2; b <= 8; b++) {
        int cap = (1 << b) - 1;
        const int Ps[] = {cap, cap + 1, 2 * cap, 3 * cap + 2};
        const int Ss[] = {1000, 12345};
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 2; j++) {
                uint64_t want = Ps[i] <= cap ? 0 : (uint64_t)(Ss[j] - 1) / (uint64_t)cap;
                ok += alloc_only(Ps[i], b, true, Ss[j]) == want; n++;
            }
    }
    printf("  %d/%d (b = 2..8, P in {cap, cap+1, 2cap, 3cap+2}, S in {1000, 12345}) match\n", ok, n);
    CHECK(ok == n);
    /* 8 비트: 서로 다른 프로세스를 처음으로 하나씩 돌릴 때 몇 번째에서 첫 롤오버? */
    int first = 0;
    for (int P = 250; P <= 260 && !first; P++) if (alloc_only(P, 8, true, P) == 1) first = P;
    printf("  8-bit ASID (ASID 0 reserved → 255 usable): first rollover at distinct process #%d\n", first);
    CHECK(first == 256);
}

int main(void) {
    ex1(); ex2(); ex3(); ex4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
