/* ARM-A04 추가 연습 — 답 + 테스트 (한 번에 검증)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_ref.S -o /tmp/ARM-A04_practice && /tmp/ARM-A04_practice
 *
 * 연습 1  enc_bcond / enc_cbnz (+ 디코더) — practice.S 의 진짜 워드와 비교, 무작위 왕복
 * 연습 2  emit_jump: ±128 MB 안이면 B 하나, 밖이면 고정 5워드 veneer (movz/movk ×4 → x16, br x16) — JIT 에서 실제 호출
 * 연습 3  줄 크기를 잘못 알면 몇 줄을 놓치나: missed_lines(start, len, real_line, stride)
 * 연습 4  Cortex-M7 SCB 캐시 유지보수 레지스터 쓰기 순서 (가짜 레지스터 로그)
 * 연습 5  cmodx_safe(old, new): 다른 코어가 실행 중이어도 한 워드를 바꿔도 되는 명령 쌍인가
 * (자기완결: 필요한 인코더/JIT 도우미는 solution.c 축약판을 다시 넣었다)
 */
#define _DARWIN_C_SOURCE
#include <inttypes.h>
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

extern const uint32_t prac_ref[], prac_ref_end[];
extern const uint32_t ref_insns[];                       /* shared_ref.S (ref_insns[32] = br x16) */

static uint64_t g_rng = 0x2545F4914F6CDD1Du;
static uint64_t xs(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static int64_t sext(uint64_t v, unsigned bits) {
    uint64_t m = UINT64_C(1) << (bits - 1);
    return (int64_t)(v & (m - 1)) - (int64_t)(v & m);
}

/* ---------- 연습 1: B.cond / CBNZ ---------- */
/* B.cond = 0101010 0 imm19 0 cond,  CBNZ X = 1 011010 1 imm19 Rt.  둘 다 ±1 MB */
static bool imm19_ok(int64_t off) { return !(off & 3) && off >= -(INT64_C(1) << 20) && off <= (INT64_C(1) << 20) - 4; }
static uint32_t imm19(int64_t off) { return (uint32_t)(((uint64_t)off >> 2) & 0x7FFFFu); }
bool enc_bcond(uint32_t *out, unsigned cond, int64_t off) {
    if (cond > 15 || !imm19_ok(off)) return false;
    *out = 0x54000000u | imm19(off) << 5 | cond;
    return true;
}
bool enc_cbnz(uint32_t *out, unsigned rt, int64_t off) {
    if (rt > 31 || !imm19_ok(off)) return false;
    *out = 0xB5000000u | imm19(off) << 5 | rt;
    return true;
}
/* 디코드: B.cond 면 1, CBNZ 면 2, 아니면 0 */
int dec_cond_branch(uint32_t w, unsigned *cond_or_rt, int64_t *off) {
    if ((w & 0xFF000010u) == 0x54000000u) { *cond_or_rt = w & 15; *off = sext((w >> 5) & 0x7FFFF, 19) * 4; return 1; }
    if ((w & 0xFF000000u) == 0xB5000000u) { *cond_or_rt = w & 31; *off = sext((w >> 5) & 0x7FFFF, 19) * 4; return 2; }
    return 0;
}

/* ---------- 연습 2: 분기 또는 veneer ---------- */
static bool enc_b(uint32_t *out, int64_t off) {           /* (solution.c 축약판) */
    if ((off & 3) || off < -(INT64_C(1) << 27) || off > (INT64_C(1) << 27) - 4) return false;
    *out = 0x14000000u | (uint32_t)(((uint64_t)off >> 2) & 0x03FFFFFFu);
    return true;
}
static uint32_t enc_br(unsigned rn) { return 0xD61F0000u | rn << 5; }
/* site 에 놓일 코드가 target 으로 점프. 반환 워드 수: 1 (B) 또는 5 (veneer). 정렬이 안 맞으면 0.
 * veneer 는 항상 movz + movk ×3 (0 halfword 도 씀) → 크기가 고정이라 패치 슬롯을 미리 잡을 수 있다. x16(IP0) 사용. */
size_t emit_veneer(uint32_t *buf, uint64_t target) {
    buf[0] = 0xD2800000u | (uint32_t)(target & 0xFFFF) << 5 | 16;                       /* movz x16, #h0 */
    for (unsigned hw = 1; hw < 4; hw++)
        buf[hw] = 0xF2800000u | hw << 21 | (uint32_t)((target >> (16 * hw)) & 0xFFFF) << 5 | 16;   /* movk */
    buf[4] = enc_br(16);                                                                 /* br x16 */
    return 5;
}
size_t emit_jump(uint32_t *buf, uint64_t site, uint64_t target) {
    if ((site | target) & 3) return 0;
    int64_t off = (int64_t)(target - site);               /* 2의 보수 차 (구현정의: clang/gcc 는 모듈러) */
    if (enc_b(&buf[0], off)) return 1;
    return emit_veneer(buf, target);
}

/* ---------- 연습 3: 줄 크기를 잘못 알았을 때 ---------- */
/* 실제 줄 크기 real 인 캐시에서, 시작을 stride 로 내림해 stride 간격으로 ic ivau 하면 [start,start+len) 이 걸친
 * 실제 줄 중 몇 개가 무효화되지 않는가 (ic ivau 하나는 주소가 속한 실제 줄 1개만 무효화). real, stride 는 2의 거듭제곱. */
uint64_t missed_lines(uint64_t start, uint64_t len, uint64_t real, uint64_t stride) {
    if (!len) return 0;
    uint64_t first = start / real, last = (start + len - 1) / real, missed = 0;
    for (uint64_t l = first; l <= last; l++) {
        bool hit = false;                                  /* 이 실제 줄 안에 ic 주소가 하나라도 떨어지나 */
        uint64_t a0 = start & ~(stride - 1);
        uint64_t k_lo = (l * real > a0) ? (l * real - a0 + stride - 1) / stride : 0;
        uint64_t addr = a0 + k_lo * stride;
        if (addr < start + len && addr < (l + 1) * real && addr >= l * real) hit = true;
        if (!hit) missed++;
    }
    return missed;
}
static uint64_t missed_brute(uint64_t start, uint64_t len, uint64_t real, uint64_t stride) {
    if (!len) return 0;
    static bool cov[4096];
    uint64_t first = start / real, last = (start + len - 1) / real, m = 0;
    memset(cov, 0, sizeof cov);
    for (uint64_t a = start & ~(stride - 1); a < start + len; a += stride)
        if (a / real >= first) cov[a / real - first] = true;          /* 범위 앞 줄에 떨어진 ic 는 무관 */
    for (uint64_t l = first; l <= last; l++) m += !cov[l - first];
    return m;
}

/* ---------- 연습 4: Cortex-M7 SCB 캐시 유지보수 (CMSIS core_cm7.h 의 레지스터 주소, 문서 확인 권장) ---------- */
#define SCB_ICIALLU 0xE000EF50u
#define SCB_ICIMVAU 0xE000EF58u
#define SCB_DCCMVAU 0xE000EF64u
#define M7_LINE 32u                                       /* Cortex-M7 L1 줄 32 B */
enum ev { EV_WRITE, EV_DSB, EV_ISB };
struct rlog { int n; struct { enum ev k; uint32_t reg, val; } e[64]; };
static void lw(struct rlog *l, uint32_t reg, uint32_t v) { if (l->n < 64) { l->e[l->n].k = EV_WRITE; l->e[l->n].reg = reg; l->e[l->n].val = v; } l->n++; }
static void lb(struct rlog *l, enum ev k) { if (l->n < 64) { l->e[l->n].k = k; l->e[l->n].reg = 0; l->e[l->n].val = 0; } l->n++; }
/* RAM 으로 복사한 코드 [addr, addr+len) 를 실행 가능하게: DCCMVAU 줄마다 → DSB → ICIMVAU 줄마다 → DSB → ISB */
void m7_sync_icache(struct rlog *l, uint32_t addr, uint32_t len) {
    if (!len) return;
    uint32_t a0 = addr & ~(M7_LINE - 1), end = addr + len;
    for (uint32_t a = a0; a < end; a += M7_LINE) lw(l, SCB_DCCMVAU, a);
    lb(l, EV_DSB);
    for (uint32_t a = a0; a < end; a += M7_LINE) lw(l, SCB_ICIMVAU, a);
    lb(l, EV_DSB);
    lb(l, EV_ISB);
}
/* 전체 I-cache 무효화 (CMSIS SCB_InvalidateICache 순서) */
void m7_invalidate_icache_all(struct rlog *l) { lb(l, EV_DSB); lb(l, EV_ISB); lw(l, SCB_ICIALLU, 0); lb(l, EV_DSB); lb(l, EV_ISB); }

/* ---------- 연습 5: CMODX (Arm ARM 의 "동시 수정·실행" 예외 집합, 문서 확인 권장) ---------- */
static bool cmodx_member(uint32_t w) {
    return (w & 0x7C000000u) == 0x14000000u ||             /* B, BL */
           (w & 0xFFE0001Fu) == 0xD4200000u ||             /* BRK #imm */
           (w & 0xFFE0001Fu) == 0xD4000001u ||             /* SVC */
           (w & 0xFFE0001Fu) == 0xD4000002u ||             /* HVC */
           (w & 0xFFE0001Fu) == 0xD4000003u ||             /* SMC */
           w == 0xD503201Fu ||                              /* NOP */
           (w & 0xFFFFF0FFu) == 0xD50330DFu;               /* ISB (option 필드 CRm 포함) */
}
bool cmodx_safe(uint32_t old_w, uint32_t new_w) { return cmodx_member(old_w) && cmodx_member(new_w); }

/* ---------- JIT 도우미 (solution.c 축약판) ---------- */
static uint64_t c_target(uint64_t x) { return x * 3 + 1; }
static uint32_t *jit_new(void) {
    uint32_t *p = mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    if (p == MAP_FAILED) return NULL;
    pthread_jit_write_protect_np(0);
    for (int i = 0; i < 4096; i++) p[i] = 0xD65F03C0u;
    pthread_jit_write_protect_np(1);
    sys_icache_invalidate(p, 16384);
    return p;
}
static void jit_put(uint32_t *dst, const uint32_t *w, size_t n) {
    pthread_jit_write_protect_np(0);
    for (size_t i = 0; i < n; i++) __atomic_store_n(&dst[i], w[i], __ATOMIC_RELAXED);
    pthread_jit_write_protect_np(1);
    sys_icache_invalidate(dst, n * 4);
}
typedef uint64_t (*fn_t)(uint64_t);

int main(void) {
    size_t npr = (size_t)(prac_ref_end - prac_ref);
    CHECK(npr == 20);

    /* 연습 1 */
    uint32_t w = 0;
    CHECK(enc_bcond(&w, 0, 0x20) && w == prac_ref[0]);
    CHECK(enc_bcond(&w, 1, -4) && w == prac_ref[1]);
    CHECK(enc_bcond(&w, 8, 0xffffc) && w == prac_ref[2] && enc_bcond(&w, 11, -0x100000) && w == prac_ref[3]);
    CHECK(enc_cbnz(&w, 5, 0x40) && w == prac_ref[4] && enc_cbnz(&w, 30, -0x100000) && w == prac_ref[5]);
    CHECK(!enc_bcond(&w, 16, 0) && !enc_bcond(&w, 0, 0x100000) && !enc_cbnz(&w, 0, 2) && !enc_cbnz(&w, 32, 0));
    int bad = 0;
    for (int i = 0; i < 20000; i++) {
        int64_t off = (int64_t)(xs() % (UINT64_C(1) << 19)) * 4 - (INT64_C(1) << 20);
        unsigned c = (unsigned)(xs() % 16), r = (unsigned)(xs() % 32), c2 = 0, r2 = 0; int64_t o1 = 0, o2 = 0;
        uint32_t wb = 0, wc = 0;
        if (!enc_bcond(&wb, c, off) || !enc_cbnz(&wc, r, off) || dec_cond_branch(wb, &c2, &o1) != 1 ||
            dec_cond_branch(wc, &r2, &o2) != 2 || c2 != c || r2 != r || o1 != off || o2 != off) bad++;
    }
    CHECK(bad == 0);
    { unsigned c; int64_t o; CHECK(dec_cond_branch(prac_ref[14], &c, &o) == 0 && dec_cond_branch(0xB4000000u, &c, &o) == 0); }

    /* 연습 2 */
    uint32_t v[5];
    CHECK(prac_ref[6] == ref_insns[32] && enc_br(16) == prac_ref[6]);
    CHECK(emit_jump(v, 0x100000000u, 0x100000000u + 0x7fffffc) == 1 && v[0] == 0x15ffffffu);
    CHECK(emit_jump(v, 0x100000000u, 0x100000000u + 0x8000000) == 5 && v[4] == 0xD61F0200u);
    CHECK(emit_jump(v, 0x108000000u, 0x100000000u) == 1 && v[0] == 0x16000000u);       /* 정확히 −128 MB */
    CHECK(emit_jump(v, 0x108000004u, 0x100000000u) == 5);                               /* 4 B 더 멀면 veneer */
    CHECK(emit_jump(v, 0x1000, 0x1002) == 0);
    {
        uint32_t *j = jit_new(); uint64_t y1 = 0, y2 = 0;
        size_t n = j ? emit_jump(v, (uint64_t)(uintptr_t)(j + 16), (uint64_t)(uintptr_t)&c_target) : 0;
        if (n) { jit_put(j + 16, v, n); y1 = ((fn_t)(uintptr_t)(j + 16))(10); }
        printf("INFO  JIT@%p -> c_target@%p: emit_jump chose %zu word(s)\n", (void *)(j + 16), (void *)(uintptr_t)&c_target, n);
        int64_t d = (int64_t)((uint64_t)(uintptr_t)&c_target - (uint64_t)(uintptr_t)(j + 16));
        bool near = d >= -(INT64_C(1) << 27) && d <= (INT64_C(1) << 27) - 4;   /* 거리는 ASLR 에 따라 다르다 */
        CHECK(n == (near ? 1u : 5u) && y1 == 31);
        uint64_t y3 = 0;
        if (j) { emit_veneer(v, (uint64_t)(uintptr_t)&c_target); jit_put(j + 48, v, 5); y3 = ((fn_t)(uintptr_t)(j + 48))(7); }
        CHECK(y3 == 22);                                   /* 거리와 무관하게 veneer 로도 실제 호출 */
        uint32_t add1[2] = {0x91000400u, 0xD65F03C0u};     /* j+64: add x0,x0,#1 ; ret */
        jit_put(j + 64, add1, 2);
        n = emit_jump(v, (uint64_t)(uintptr_t)(j + 32), (uint64_t)(uintptr_t)(j + 64));
        if (n == 1) { jit_put(j + 32, v, 1); y2 = ((fn_t)(uintptr_t)(j + 32))(10); }
        CHECK(n == 1 && y2 == 11);
        if (j) munmap(j, 16384);
    }

    /* 연습 3 */
    CHECK(missed_lines(0x1000 + 76, 4, 64, 128) == 1);       /* demo_stale 의 stride 128 실험 */
    CHECK(missed_lines(0x1000, 4096, 64, 128) == 32 && missed_lines(0x1000, 4096, 64, 64) == 0);
    CHECK(missed_lines(0x1000 + 60, 8, 64, 64) == 0 && missed_lines(0x1000, 4096, 64, 32) == 0);
    bad = 0;
    for (int i = 0; i < 5000; i++) {
        uint64_t real = UINT64_C(16) << (xs() % 4), stride = UINT64_C(16) << (xs() % 5);
        uint64_t st = 0x10000 + xs() % 1024, len = xs() % 2048;
        if (missed_lines(st, len, real, stride) != missed_brute(st, len, real, stride)) bad++;
    }
    CHECK(bad == 0);

    /* 연습 4 */
    struct rlog l; memset(&l, 0, sizeof l);
    m7_sync_icache(&l, 0x20000010u, 100);                     /* 0x20000000..0x20000073 → 32 B 줄 4개 */
    CHECK(l.n == 4 + 1 + 4 + 1 + 1);
    CHECK(l.e[0].k == EV_WRITE && l.e[0].reg == SCB_DCCMVAU && l.e[0].val == 0x20000000u && l.e[3].val == 0x20000060u);
    CHECK(l.e[4].k == EV_DSB && l.e[5].reg == SCB_ICIMVAU && l.e[8].val == 0x20000060u && l.e[9].k == EV_DSB && l.e[10].k == EV_ISB);
    memset(&l, 0, sizeof l); m7_sync_icache(&l, 0x20000000u, 0);
    CHECK(l.n == 0);
    memset(&l, 0, sizeof l); m7_invalidate_icache_all(&l);
    CHECK(l.n == 5 && l.e[2].k == EV_WRITE && l.e[2].reg == SCB_ICIALLU && l.e[0].k == EV_DSB && l.e[4].k == EV_ISB);

    /* 연습 5: prac_ref[7..15] = brk brk svc hvc smc nop isb b bl  /  [16..19] = add movz ret dsb */
    bad = 0;
    for (int a = 7; a <= 15; a++) for (int b = 7; b <= 15; b++) if (!cmodx_safe(prac_ref[a], prac_ref[b])) bad++;
    CHECK(bad == 0);
    bad = 0;
    for (int a = 7; a <= 19; a++) for (int b = 16; b <= 19; b++) if (cmodx_safe(prac_ref[a], prac_ref[b]) || cmodx_safe(prac_ref[b], prac_ref[a])) bad++;
    CHECK(bad == 0);
    CHECK(!cmodx_safe(0x91000400u, 0x91000800u));             /* add #1 → add #2 : 안전 집합 밖 → 멈추고 바꿔야 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
