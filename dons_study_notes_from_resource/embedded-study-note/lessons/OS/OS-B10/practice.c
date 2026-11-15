/* OS-B10 추가 연습: 페이지 테이블 크기, Sv32 2단계 분해, accessed 스캔, dirty 기록, 요구 페이징
 *
 *   cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#define _POSIX_C_SOURCE 200809L
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- 본문(solution.c)의 핵심만 다시 (자기 완결) ---- */
#define PTE_V (1u << 0)
#define PTE_R (1u << 1)
#define PTE_W (1u << 2)
#define PTE_X (1u << 3)
#define PTE_A (1u << 6)
#define PTE_D (1u << 7)
#define PTE_PFN_SHIFT 10

typedef enum { ACC_READ, ACC_WRITE, ACC_EXEC } acc_t;
typedef enum { XL_OK, XL_RANGE, XL_INVALID, XL_PROT, XL_BADPFN } xl_t;

typedef struct {
    uint64_t asize, pgsize, nframes;
    unsigned off_bits;
    uint64_t npages;
    uint32_t *pt;
} mmu_t;

static void mmu_setup(mmu_t *m, unsigned as_bits, unsigned pg_bits, unsigned frame_bits, uint32_t *pt) {
    m->asize = 1ull << as_bits; m->pgsize = 1ull << pg_bits; m->off_bits = pg_bits;
    m->npages = 1ull << (as_bits - pg_bits); m->nframes = 1ull << frame_bits; m->pt = pt;
    memset(pt, 0, (size_t)m->npages * sizeof *pt);
}
static uint32_t pte_make(uint64_t pfn, uint32_t f) { return (uint32_t)(pfn << PTE_PFN_SHIFT) | f; }
static uint64_t pte_pfn(uint32_t e) { return e >> PTE_PFN_SHIFT; }

static xl_t mmu_translate(mmu_t *m, uint64_t va, acc_t acc, uint64_t *pa) {
    if (va >= m->asize) return XL_RANGE;
    uint32_t *e = &m->pt[va >> m->off_bits];
    uint32_t need = acc == ACC_READ ? PTE_R : acc == ACC_WRITE ? PTE_W : PTE_X;
    if (!(*e & PTE_V)) return XL_INVALID;
    if (!(*e & need)) return XL_PROT;
    if (pte_pfn(*e) >= m->nframes) return XL_BADPFN;
    *e |= PTE_A | (acc == ACC_WRITE ? PTE_D : 0);
    *pa = (pte_pfn(*e) << m->off_bits) | (va & (m->pgsize - 1));
    return XL_OK;
}

/* ---- 연습 1: 선형 페이지 테이블 크기 ---- */
static uint64_t pt_bytes(unsigned as_bits, unsigned pg_bits, unsigned pte_bytes, uint64_t *entries) {
    *entries = 1ull << (as_bits - pg_bits);
    return *entries * pte_bytes;
}

/* ---- 연습 2: Sv32 2단계 분해 (10 | 10 | 12) ---- */
typedef struct { uint32_t vpn1, vpn0, off; } sv32_va_t;
static sv32_va_t sv32_split(uint32_t va) {
    sv32_va_t r = { va >> 22, (va >> 12) & 0x3FFu, va & 0xFFFu };
    return r;
}
static uint32_t sv32_join(sv32_va_t s) { return (s.vpn1 << 22) | (s.vpn0 << 12) | s.off; }

/* ---- 연습 3: accessed 스캔 — 지난 스캔 이후 안 쓰인 valid 페이지 목록, A 는 모두 지움 ---- */
static size_t scan_accessed(mmu_t *m, uint64_t *cold, size_t cap) {
    size_t n = 0;
    for (uint64_t v = 0; v < m->npages; v++) {
        uint32_t *e = &m->pt[v];
        if (!(*e & PTE_V)) continue;
        if (!(*e & PTE_A) && n < cap) cold[n++] = v;
        *e &= ~PTE_A;
    }
    return n;
}

/* ---- 연습 4: dirty 페이지 기록(writeback) — D 를 지우고 VPN 목록을 돌려준다 ---- */
static size_t writeback_dirty(mmu_t *m, uint64_t *out, size_t cap) {
    size_t n = 0;
    for (uint64_t v = 0; v < m->npages; v++) {
        uint32_t *e = &m->pt[v];
        if ((*e & (PTE_V | PTE_D)) == (PTE_V | PTE_D) && n < cap) {
            out[n++] = v;            /* 실제 OS: 여기서 프레임 내용을 디스크/플래시에 쓴다 */
            *e &= ~PTE_D;
        }
    }
    return n;
}

/* ---- 연습 5: 요구 페이징 — INVALID 이고 그 페이지가 "할당 허용 영역" 이면 프레임을 주고 재시도 ---- */
typedef struct { uint64_t next, end; } frame_pool_t;   /* [next, end) 범프 할당기 */

static xl_t access_demand(mmu_t *m, frame_pool_t *fp, const bool *may_map, uint64_t va, acc_t acc,
                          uint64_t *pa, unsigned *faults) {
    xl_t r = mmu_translate(m, va, acc, pa);
    if (r != XL_INVALID || !may_map[va >> m->off_bits]) return r;  /* 진짜 segfault */
    (*faults)++;
    if (fp->next == fp->end) return XL_INVALID;   /* 프레임 없음: 교체는 OS-B11 */
    m->pt[va >> m->off_bits] = pte_make(fp->next++, PTE_V | PTE_R | PTE_W);
    return mmu_translate(m, va, acc, pa);          /* 명령 재실행 */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1 */
    uint64_t ent = 0;
    CHECK(pt_bytes(32, 12, 4, &ent) == (4ull << 20) && ent == (1ull << 20));     /* 4 MiB */
    CHECK(pt_bytes(32, 14, 4, &ent) == (1ull << 20) && ent == (1ull << 18));     /* 16 KB 페이지: 1 MiB */
    CHECK(pt_bytes(48, 12, 8, &ent) == (512ull << 30));                         /* 512 GiB */
    CHECK(pt_bytes(39, 12, 8, &ent) == (1ull << 30));                           /* Sv39 평평하게: 1 GiB */
    uint64_t e1, e2, e3, e4, e5;                                                /* OSTEP 숙제 Q1 */
    pt_bytes(20, 10, 4, &e1); pt_bytes(21, 10, 4, &e2); pt_bytes(22, 10, 4, &e3);
    pt_bytes(20, 11, 4, &e4); pt_bytes(20, 12, 4, &e5);
    CHECK(e1 == 1024 && e2 == 2048 && e3 == 4096 && e4 == 512 && e5 == 256);

    /* 연습 2 */
    sv32_va_t s = sv32_split(0x12345678u);
    CHECK(s.vpn1 == 0x48 && s.vpn0 == 0x345 && s.off == 0x678);
    s = sv32_split(0xFFFFFFFFu);
    CHECK(s.vpn1 == 0x3FF && s.vpn0 == 0x3FF && s.off == 0xFFF);
    bool rt = true;
    for (uint32_t va = 0, k = 0; k < 100000; k++, va += 0x9E3779B1u) rt &= sv32_join(sv32_split(va)) == va;
    CHECK(rt);

    /* 공통 테이블: 16 KB 주소 공간, 4 KB 페이지, 16 프레임 */
    uint32_t pt[4];
    mmu_t m;
    uint64_t pa, list[8];
    mmu_setup(&m, 14, 12, 4, pt);
    for (int v = 0; v < 4; v++) pt[v] = pte_make((uint64_t)(8 + v), PTE_V | PTE_R | PTE_W);
    pt[2] = 0;                                            /* VPN 2 는 invalid */

    /* 연습 3 */
    mmu_translate(&m, 0x0010, ACC_READ, &pa);              /* VPN 0 */
    mmu_translate(&m, 0x3010, ACC_READ, &pa);              /* VPN 3 */
    size_t n = scan_accessed(&m, list, 8);
    CHECK(n == 1 && list[0] == 1);                        /* VPN 2 는 invalid 라 제외 */
    mmu_translate(&m, 0x1000, ACC_READ, &pa);
    n = scan_accessed(&m, list, 8);
    CHECK(n == 2 && list[0] == 0 && list[1] == 3);
    CHECK(!(pt[0] & PTE_A) && !(pt[1] & PTE_A) && !(pt[3] & PTE_A));

    /* 연습 4 */
    mmu_translate(&m, 0x0004, ACC_WRITE, &pa);
    mmu_translate(&m, 0x3FFF, ACC_WRITE, &pa);
    mmu_translate(&m, 0x1004, ACC_READ, &pa);
    n = writeback_dirty(&m, list, 8);
    CHECK(n == 2 && list[0] == 0 && list[1] == 3);
    CHECK(writeback_dirty(&m, list, 8) == 0);             /* 두 번째는 쓸 게 없다 */
    CHECK((pt[0] & PTE_A) && (pt[1] & PTE_A));            /* A 는 그대로 */

    /* 연습 5: 4 페이지 중 0..2 만 힙(할당 허용), 프레임 2개 */
    uint32_t pt2[4];
    mmu_t d;
    mmu_setup(&d, 14, 12, 4, pt2);
    bool may[4] = {true, true, true, false};
    frame_pool_t fp = {5, 7};
    unsigned faults = 0;
    CHECK(access_demand(&d, &fp, may, 0x0100, ACC_WRITE, &pa, &faults) == XL_OK && pa == 0x5100 && faults == 1);
    CHECK(access_demand(&d, &fp, may, 0x0200, ACC_READ, &pa, &faults) == XL_OK && faults == 1);  /* 이미 매핑 */
    CHECK(access_demand(&d, &fp, may, 0x1000, ACC_READ, &pa, &faults) == XL_OK && pa == 0x6000 && faults == 2);
    CHECK(access_demand(&d, &fp, may, 0x3000, ACC_READ, &pa, &faults) == XL_INVALID && faults == 2);  /* segfault */
    CHECK(access_demand(&d, &fp, may, 0x2000, ACC_READ, &pa, &faults) == XL_INVALID && faults == 3
          && !(pt2[2] & PTE_V));                                                  /* 프레임 고갈 */
    CHECK((pt2[0] & PTE_D) && !(pt2[1] & PTE_D));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
