/* BM-B08 추가 연습 — 호스트(Mac) 에서 도는 "부트 이미지 린터(lint)".
 * VM/QEMU 없이도 돈다: solution.elf 에서 실제로 뽑은 벡터 테이블 48 워드와 링커 심볼 값을 아래 표로 담았다
 * (run_qemu.sh map / practice 가 같은 값을 다시 찍어 준다). 그 진짜 이미지와 일부러 망가뜨린 이미지를 검사한다.
 *   P1 vt_lint      — 벡터 테이블 규칙 검사 (SP 정렬·범위, Thumb 비트, 예약 칸, 빈 핸들러)
 *   P2 reset_sim    — 리셋 때 코어가 하는 일 (MSP/PC/T 로드) → BOOT / HardFault(INVSTATE) / LOCKUP 예측
 *   P3 weak_census  — Default_Handler 로 가는 벡터 개수 (weak alias 가 몇 개 남았나)
 *   P4 sym_lint     — 링커 심볼 순서·정렬·FLASH/RAM 예산 (size 출력과 대조)
 *   P5 ipsr_lookup  — Default_Handler 에서 본 IPSR → 벡터 오프셋·IRQ 번호·등록된 주소
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-B08_pr && /tmp/BM-B08_pr
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ---------- 실제 캡처: arm-none-eabi-objdump -s -j .isr_vector solution.elf (little-endian 워드로 변환) ---------- */
#define NVEC 48u
static const uint32_t REAL_VT[NVEC] = {
    0x20010000u, 0x0000025Du, 0x000001A1u, 0x00000799u, 0x000001A1u, 0x000001A1u, 0x000001A1u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x000001A1u, 0x000001A1u, 0x00000000u, 0x000001A1u, 0x0000071Du,
    0x000001A1u, 0x000001A1u, 0x000001A1u, 0x000001A1u, 0x000001A1u, 0x000001A1u, 0x000001A1u, 0x000001A1u,
    0x000001A1u, 0x000001A1u, 0x000001A1u, 0x000001A1u, 0x000001A1u, 0x000001A1u, 0x000001A1u, 0x000001A1u,
    0x000001A1u, 0x000001A1u, 0x000001A1u, 0x000001A1u, 0x000001A1u, 0x000001A1u, 0x000001A1u, 0x000001A1u,
    0x000001A1u, 0x000001A1u, 0x000001A1u, 0x000001A1u, 0x000001A1u, 0x000001A1u, 0x000001A1u, 0x000001A1u,
};
/* arm-none-eabi-nm -n solution.elf / arm-none-eabi-size solution.elf (text 3212, data 36, bss 76) */
typedef struct {
    uint32_t sidata, sdata, edata, sbss, ebss, estack, init_start, init_end, min_stack;
} syms_t;
static const syms_t REAL_SYMS = { 0x00000C94u, 0x20000000u, 0x2000001Cu, 0x20000020u, 0x2000006Cu,
                                  0x20010000u, 0x00000C8Cu, 0x00000C94u, 0x800u };
#define ADDR_DEFAULT_HANDLER 0x000001A0u   /* nm: T Default_Handler (Thumb 비트 없는 심볼 값) */

/* 메모리 맵: 링커 스크립트 bm_b08.ld 의 MEMORY (보드는 더 크지만 "이 칩" 은 256K/64K 라고 약속) */
typedef struct { uint32_t flash_lo, flash_hi, ram_lo, ram_hi; } memmap_t;   /* [lo, hi) */
static const memmap_t MM = { 0x00000000u, 0x00040000u, 0x20000000u, 0x20010000u };

/* ================= P1: 벡터 테이블 린트 ================= */
enum {
    E_SP_ALIGN = 1u << 0,        /* [0] 이 8 의 배수가 아님 (AAPCS: 공개 인터페이스에서 SP 8-정렬) */
    E_SP_RANGE = 1u << 1,        /* [0] 이 (ram_lo, ram_hi] 밖 — full-descending 이라 ram_hi 자체는 OK */
    E_RESET_THUMB = 1u << 2,     /* [1] bit0 = 0 */
    E_RESET_RANGE = 1u << 3,     /* [1] 이 FLASH 밖 */
    E_HANDLER_THUMB = 1u << 4,   /* 0 이 아닌 핸들러 칸의 bit0 = 0 */
    E_HANDLER_NULL = 1u << 5,    /* 예약이 아닌 칸이 0 (그 예외가 오면 주소 0 = SP 값으로 점프) */
    E_RESERVED = 1u << 6,        /* 예약 칸 7..10, 13 이 0 이 아님 */
};
static bool is_reserved(unsigned i) { return (i >= 7u && i <= 10u) || i == 13u; }

static unsigned vt_lint(const uint32_t *vt, unsigned n, const memmap_t *mm) {
    unsigned e = 0;
    if (vt[0] & 7u) e |= E_SP_ALIGN;
    if (!(vt[0] > mm->ram_lo && vt[0] <= mm->ram_hi)) e |= E_SP_RANGE;
    if (!(vt[1] & 1u)) e |= E_RESET_THUMB;
    uint32_t rpc = vt[1] & ~1u;
    if (!(rpc >= mm->flash_lo && rpc < mm->flash_hi)) e |= E_RESET_RANGE;
    for (unsigned i = 2; i < n; ++i) {
        if (is_reserved(i)) {
            if (vt[i] != 0u) e |= E_RESERVED;
        } else if (vt[i] == 0u) {
            e |= E_HANDLER_NULL;
        } else if (!(vt[i] & 1u)) {
            e |= E_HANDLER_THUMB;
        }
    }
    return e;
}

/* ================= P2: 리셋 시뮬레이션 ================= */
typedef enum { R_BOOT, R_HARDFAULT, R_LOCKUP } reset_outcome_t;
typedef struct {
    uint32_t msp, pc, cfsr, hfsr, stacked_pc;
    bool t;
    reset_outcome_t out;
} reset_t;

/* 단순화한 모델: (1) MSP = vt[0] & ~3, PC = vt[1] & ~1, EPSR.T = vt[1] & 1.
 * (2) SP 가 RAM 밖이면 첫 push 가 BusFault → HardFault 진입/핸들러의 push 도 실패 → LOCKUP.
 * (3) T = 0 이면 첫 명령에서 UsageFault(INVSTATE) — 리셋 직후엔 UsageFault 가 꺼져 있어 HardFault 로
 *     승격(FORCED). HardFault 벡터마저 T = 0 이면 핸들러 안에서 또 fault → LOCKUP. */
static reset_t reset_sim(const uint32_t *vt, const memmap_t *mm) {
    reset_t r = { 0 };
    r.msp = vt[0] & ~3u;
    r.pc = vt[1] & ~1u;
    r.t = (vt[1] & 1u) != 0u;
    bool sp_ok = r.msp > mm->ram_lo && r.msp <= mm->ram_hi;
    if (!sp_ok) { r.out = R_LOCKUP; return r; }
    if (r.t) { r.out = R_BOOT; return r; }
    r.cfsr = 1u << 17;               /* UFSR.INVSTATE (CFSR bit 17) */
    r.hfsr = 1u << 30;               /* HFSR.FORCED */
    r.stacked_pc = r.pc;
    r.out = (vt[3] & 1u) ? R_HARDFAULT : R_LOCKUP;
    return r;
}

/* ================= P3: weak alias 인구조사 ================= */
static unsigned weak_census(const uint32_t *vt, unsigned n, uint32_t default_addr) {
    unsigned c = 0;
    for (unsigned i = 2; i < n; ++i)
        if (!is_reserved(i) && (vt[i] & ~1u) == default_addr) ++c;
    return c;
}

/* ================= P4: 링커 심볼 린트 + 예산 ================= */
enum {
    S_ORDER = 1u << 0,       /* sdata ≤ edata ≤ sbss ≤ ebss 순서가 아님 */
    S_ALIGN = 1u << 1,       /* 경계가 4 의 배수가 아님 → 워드 복사 루프가 꼬리를 놓친다 */
    S_LMA = 1u << 2,         /* .data 초기값(LMA) 이 FLASH 밖 */
    S_STACK = 1u << 3,       /* ebss + min_stack > estack */
    S_INIT = 1u << 4,        /* init_array 가 FLASH 밖이거나 4-정렬 아님 */
};
typedef struct { uint32_t flash_used, ram_static, ram_with_stack, copy_words, zero_words; } budget_t;

static unsigned sym_lint(const syms_t *s, const memmap_t *mm, budget_t *b) {
    unsigned e = 0;
    if (!(s->sdata <= s->edata && s->edata <= s->sbss && s->sbss <= s->ebss)) e |= S_ORDER;
    if ((s->sdata | s->edata | s->sbss | s->ebss | s->sidata) & 3u) e |= S_ALIGN;
    uint32_t dsz = s->edata - s->sdata;
    if (!(s->sidata >= mm->flash_lo && (uint64_t)s->sidata + dsz <= mm->flash_hi)) e |= S_LMA;
    if ((uint64_t)s->ebss + s->min_stack > s->estack) e |= S_STACK;
    if (s->init_start > s->init_end || s->init_end > mm->flash_hi || ((s->init_start | s->init_end) & 3u))
        e |= S_INIT;
    b->flash_used = s->sidata + dsz;             /* .data 의 LMA 가 이미지의 마지막 → 끝 주소 = 사용량 */
    b->ram_static = s->ebss - s->sdata;          /* .data + 정렬 패딩 + .bss */
    b->ram_with_stack = b->ram_static + s->min_stack;
    b->copy_words = dsz / 4u;
    b->zero_words = (s->ebss - s->sbss) / 4u;
    return e;
}

/* ================= P5: IPSR → 벡터 ================= */
typedef struct { uint32_t offset, handler; int irq; } ipsr_info_t;
static bool ipsr_lookup(const uint32_t *vt, unsigned n, uint32_t ipsr, ipsr_info_t *o) {
    if (ipsr == 0u || ipsr >= n) return false;   /* 0 = Thread 모드, n 이상 = 이 표에 없음 */
    o->offset = ipsr * 4u;
    o->handler = vt[ipsr];
    o->irq = (int)ipsr - 16;                     /* 음수 = 시스템 예외 (CMSIS IRQn_Type 과 같은 관례) */
    return true;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static void copy_vt(uint32_t *dst, const uint32_t *src) {
    for (unsigned i = 0; i < NVEC; ++i) dst[i] = src[i];
}

int main(void) {
    uint32_t vt[NVEC];

    /* P1 — 진짜 이미지는 깨끗하고, 주입한 결함은 각각 정확히 그 비트로 잡힌다 */
    CHECK(vt_lint(REAL_VT, NVEC, &MM) == 0u);
    copy_vt(vt, REAL_VT); vt[1] = 0x00000270u;             /* faults F1: Reset_NoThumb (bit0 없음) */
    CHECK(vt_lint(vt, NVEC, &MM) == E_RESET_THUMB);
    copy_vt(vt, REAL_VT); vt[0] = 0x30000000u;             /* faults F3: 없는 주소 */
    CHECK(vt_lint(vt, NVEC, &MM) == E_SP_RANGE);
    copy_vt(vt, REAL_VT); vt[0] = 0x20800000u;             /* faults F4: QEMU 미러 때문에 "돌아 버린" 값 */
    CHECK(vt_lint(vt, NVEC, &MM) == E_SP_RANGE);
    copy_vt(vt, REAL_VT); vt[0] = 0x20000000u;             /* F2: _estack = ORIGIN(RAM) — 경계 자체 */
    CHECK(vt_lint(vt, NVEC, &MM) == E_SP_RANGE);
    copy_vt(vt, REAL_VT); vt[0] = 0x2000FFFCu;             /* 4-정렬이지만 8-정렬 아님 */
    CHECK(vt_lint(vt, NVEC, &MM) == E_SP_ALIGN);
    copy_vt(vt, REAL_VT); vt[8] = 0x000001A1u;             /* 예약 칸에 값 */
    CHECK(vt_lint(vt, NVEC, &MM) == E_RESERVED);
    copy_vt(vt, REAL_VT); vt[15] = 0u;                     /* SysTick 칸 비움 */
    CHECK(vt_lint(vt, NVEC, &MM) == E_HANDLER_NULL);
    copy_vt(vt, REAL_VT); vt[3] = 0x00000798u;             /* HardFault 칸 bit0 없음 */
    CHECK(vt_lint(vt, NVEC, &MM) == E_HANDLER_THUMB);
    copy_vt(vt, REAL_VT); vt[1] = 0x00040001u;             /* FLASH 끝 너머 */
    CHECK(vt_lint(vt, NVEC, &MM) == E_RESET_RANGE);

    /* P2 — run_qemu.sh faults 의 실제 관찰과 같은 결론이어야 한다 */
    reset_t r = reset_sim(REAL_VT, &MM);
    CHECK(r.out == R_BOOT && r.msp == 0x20010000u && r.pc == 0x0000025Cu && r.t);
    copy_vt(vt, REAL_VT); vt[1] = 0x00000270u;
    r = reset_sim(vt, &MM);                                /* 실측: CFSR=0x00020000 HFSR=0x40000000 pc=0x270 */
    CHECK(r.out == R_HARDFAULT && r.cfsr == 0x00020000u && r.hfsr == 0x40000000u && r.stacked_pc == 0x270u);
    vt[3] = 0x00000798u;                                   /* HardFault 벡터마저 bit0 없음 */
    CHECK(reset_sim(vt, &MM).out == R_LOCKUP);
    copy_vt(vt, REAL_VT); vt[0] = 0x30000000u;             /* 실측: "Lockup: can't escalate 3 to HardFault" */
    CHECK(reset_sim(vt, &MM).out == R_LOCKUP);

    /* P3 — 7 개 시스템 예외(NMI, MemManage, BusFault, UsageFault, SVC, DebugMon, PendSV) + IRQ 32 개 */
    CHECK(weak_census(REAL_VT, NVEC, ADDR_DEFAULT_HANDLER) == 7u + 32u);
    CHECK((REAL_VT[3] & ~1u) != ADDR_DEFAULT_HANDLER && (REAL_VT[15] & ~1u) != ADDR_DEFAULT_HANDLER);

    /* P4 — 실제 심볼은 깨끗, 예산은 size 출력과 일치 (text 3212 + data 36 = 3248) */
    budget_t b;
    CHECK(sym_lint(&REAL_SYMS, &MM, &b) == 0u);
    CHECK(b.flash_used == 3212u + 36u);
    CHECK(b.copy_words == 7u && b.zero_words == 19u && b.ram_static == 0x6Cu && b.ram_with_stack == 0x86Cu);
    syms_t s = REAL_SYMS; s.edata = 0x2000001Bu; s.sbss = 0x2000001Bu;   /* ALIGN(4) 빠뜨린 스크립트 */
    CHECK(sym_lint(&s, &MM, &b) == S_ALIGN);
    s = REAL_SYMS; s.estack = 0x20000000u;                               /* F2 와 같은 실수 */
    CHECK(sym_lint(&s, &MM, &b) == S_STACK);
    s = REAL_SYMS; s.sidata = 0x0003FFF0u;                               /* 초기값이 FLASH 끝을 넘김 */
    CHECK(sym_lint(&s, &MM, &b) == S_LMA);
    s = REAL_SYMS; s.sbss = 0x20000010u;                                 /* .bss 가 .data 와 겹침 */
    CHECK(sym_lint(&s, &MM, &b) == S_ORDER);

    /* P5 — Default_Handler 실측 IPSR 23 = IRQ7 */
    ipsr_info_t o;
    CHECK(ipsr_lookup(REAL_VT, NVEC, 23u, &o) && o.offset == 0x5Cu && o.irq == 7 && o.handler == 0x1A1u);
    CHECK(ipsr_lookup(REAL_VT, NVEC, 3u, &o) && o.irq == -13 && o.handler == 0x799u);   /* HardFault */
    CHECK(ipsr_lookup(REAL_VT, NVEC, 15u, &o) && o.irq == -1 && o.offset == 0x3Cu);     /* SysTick */
    CHECK(!ipsr_lookup(REAL_VT, NVEC, 0u, &o) && !ipsr_lookup(REAL_VT, NVEC, 48u, &o));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
