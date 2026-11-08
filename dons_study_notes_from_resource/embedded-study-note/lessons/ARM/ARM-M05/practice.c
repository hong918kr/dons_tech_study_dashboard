/* ARM-M05 추가 연습 — 한 번에 검증
 *
 *   연습 1  한쪽만 가볍게: writer dmb ishst / reader dmb ishld, 제어 의존(cbz) vs cbz+isb
 *   연습 2  배리어·acquire/release 명령 인코더 (objdump 값과 대조, 디코더로 왕복)
 *   연습 3  상황별 배리어 레시피 (A-profile 시스템 레지스터·TLB·코드 수정, Cortex-M MPU·CONTROL·FPU)
 *   연습 4  JIT 한 단어 패치: ldapr → ldar → ldr [x0,#4] 로 바꿔 가며 실행
 *   연습 5  판정기 확장 케이스: 도메인(osh/nsh/sy), dsb, isb 단독, dmb ld/st
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr
 */
#define _DARWIN_C_SOURCE
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#if !defined(__aarch64__) || !defined(__APPLE__)
#error "이 레슨은 Apple arm64 (macOS) 전용이다"
#endif

#define NOINLINE __attribute__((noinline))
#define MAYBE_UNUSED __attribute__((unused))
static inline void cpu_relax(void) { __asm__ __volatile__("yield" ::: "memory"); }
typedef void (*wfn)(uint32_t *data, uint32_t *flag, uint32_t v);
typedef uint64_t (*rfn)(const uint32_t *flag, const uint32_t *data);

/* 본문에서 가져온 커널 2개 */
NOINLINE static void w_stlr(uint32_t *data, uint32_t *flag, uint32_t v) {
    __asm__ __volatile__("str %w[v], [%[d]]\n\t"
                         "stlr %w[v], [%[f]]"
                         :: [d] "r"(data), [f] "r"(flag), [v] "r"(v) : "memory");
}
NOINLINE static void w_plain(uint32_t *data, uint32_t *flag, uint32_t v) {
    __asm__ __volatile__("str %w[v], [%[d]]\n\t"
                         "str %w[v], [%[f]]"
                         :: [d] "r"(data), [f] "r"(flag), [v] "r"(v) : "memory");
}

/* ===== 연습 1 — 한쪽 배리어를 필요한 만큼만 ===== */
NOINLINE static void w_dmbst(uint32_t *data, uint32_t *flag, uint32_t v) {      /* store→store 만 */
    __asm__ __volatile__("str %w[v], [%[d]]\n\t"
                         "dmb ishst\n\t"
                         "str %w[v], [%[f]]"
                         :: [d] "r"(data), [f] "r"(flag), [v] "r"(v) : "memory");
}
NOINLINE static uint64_t r_dmbld(const uint32_t *flag, const uint32_t *data) {  /* load→(load,store) */
    uint64_t f, d;
    __asm__ __volatile__("ldr %w[rf], [%[f]]\n\t"
                         "dmb ishld\n\t"
                         "ldr %w[rd], [%[d]]"
                         : [rf] "=&r"(f), [rd] "=&r"(d) : [f] "r"(flag), [d] "r"(data) : "memory");
    return f << 32 | d;
}
NOINLINE static uint64_t r_ctrl(const uint32_t *flag, const uint32_t *data) {   /* 제어 의존만: 부족 */
    uint64_t f, d;
    __asm__ __volatile__("ldr %w[rf], [%[f]]\n\t"
                         "cbz %w[rf], 1f\n"
                         "1:\n\t"
                         "ldr %w[rd], [%[d]]"
                         : [rf] "=&r"(f), [rd] "=&r"(d) : [f] "r"(flag), [d] "r"(data) : "memory");
    return f << 32 | d;
}
NOINLINE static uint64_t r_ctrlisb(const uint32_t *flag, const uint32_t *data) { /* 제어 의존 + ISB: 충분 */
    uint64_t f, d;
    __asm__ __volatile__("ldr %w[rf], [%[f]]\n\t"
                         "cbz %w[rf], 1f\n"
                         "1:\n\t"
                         "isb\n\t"
                         "ldr %w[rd], [%[d]]"
                         : [rf] "=&r"(f), [rd] "=&r"(d) : [f] "r"(flag), [d] "r"(data) : "memory");
    return f << 32 | d;
}

/* ======================= 본문 Part B 의 디코더 + 판정기 (정답판) ======================= */
enum { K_OTHER, K_LDR, K_STR, K_LDAR, K_LDAPR, K_STLR, K_DMB, K_DSB, K_ISB, K_ALU, K_CBZ, K_RET };
static const char *const KNAME[] = { "?", "LDR", "STR", "LDAR", "LDAPR", "STLR", "DMB", "DSB", "ISB", "ALU", "CBZ", "RET" };
typedef struct { uint8_t k, opt, rt, rn, rm; } op_t;   /* rt = Rt 또는 Rd, rm = 32 이면 없음, opt = DMB/DSB 의 CRm */

/* 32비트(W) 형식만 본다. 인코딩은 모두 llvm-objdump 로 확인한 값 (md 참고). */
static op_t decode(uint32_t w) {
    op_t o = { K_OTHER, 0, (uint8_t)(w & 31u), (uint8_t)(w >> 5 & 31u), 32 };
    if ((w & 0xFFC00000u) == 0xB9400000u) o.k = K_LDR;                        /* LDR  Wt,[Xn,#imm]  */
    else if ((w & 0xFFE00C00u) == 0xB8600800u) { o.k = K_LDR; o.rm = (uint8_t)(w >> 16 & 31u); } /* LDR Wt,[Xn,Xm] */
    else if ((w & 0xFFC00000u) == 0xB9000000u) o.k = K_STR;                   /* STR  Wt,[Xn,#imm]  */
    else if ((w & 0xFFFFFC00u) == 0x88DFFC00u) o.k = K_LDAR;                  /* LDAR Wt,[Xn]       */
    else if ((w & 0xFFFFFC00u) == 0xB8BFC000u) o.k = K_LDAPR;                 /* LDAPR Wt,[Xn]      */
    else if ((w & 0xFFFFFC00u) == 0x889FFC00u) o.k = K_STLR;                  /* STLR Wt,[Xn]       */
    else if ((w & 0xFFFFF0FFu) == 0xD50330BFu) { o.k = K_DMB; o.opt = (uint8_t)(w >> 8 & 15u); }
    else if ((w & 0xFFFFF0FFu) == 0xD503309Fu) { o.k = K_DSB; o.opt = (uint8_t)(w >> 8 & 15u); }
    else if (w == 0xD5033FDFu) o.k = K_ISB;                                   /* ISB (SY)           */
    else if ((w & 0x7F200000u) == 0x4A000000u || (w & 0x7F200000u) == 0x0A000000u ||
             (w & 0x7F200000u) == 0x0B000000u) { o.k = K_ALU; o.rm = (uint8_t)(w >> 16 & 31u); } /* EOR/AND/ADD reg */
    else if ((w & 0x7E000000u) == 0x34000000u) o.k = K_CBZ;                   /* CBZ/CBNZ Rt        */
    else if (w == 0xD65F03C0u) o.k = K_RET;
    return o;
}
static int decode_words(const uint32_t *w, int n, op_t *out) {
    for (int i = 0; i < n; i++) out[i] = decode(w[i]);
    return n;
}
/* 함수 기계어를 RET 까지 읽는다 (함수 포인터 → 데이터 포인터 변환은 구현 정의, macOS arm64 에서는 그대로 읽힌다) */
static int decode_fn(uintptr_t fn, op_t *out, int max) {
    const uint32_t *p = (const uint32_t *)fn;
    int n = 0;
    while (n < max) { out[n] = decode(p[n]); if (out[n++].k == K_RET) break; }
    return n;
}
static bool is_ld(int k) { return k == K_LDR || k == K_LDAR || k == K_LDAPR; }
static bool is_st(int k) { return k == K_STR || k == K_STLR; }
/* 처음 두 메모리 접근의 위치 */
static bool two_mem(const op_t *o, int n, int *a, int *b) {
    int c = 0;
    for (int i = 0; i < n && c < 2; i++)
        if (is_ld(o[i].k) || is_st(o[i].k)) { if (c++ == 0) *a = i; else *b = i; }
    return c == 2;
}
/* DMB/DSB 의 CRm: 아래 2비트 = 종류 (1 LD, 2 ST, 3 전부), 위 2비트 = 도메인 (0 OSH, 1 NSH, 2 ISH, 3 SY).
 * 다른 코어와의 순서에는 NSH(이 PE 만) 로는 부족하다. need: 1=load 뒤 순서, 2=store→store, 3=전부 */
enum { NEED_LD = 1, NEED_ST = 2, NEED_ALL = 3 };
MAYBE_UNUSED static bool bar_covers(const op_t *o, int need) {
    if (o->k != K_DMB && o->k != K_DSB) return false;
    int type = o->opt & 3, dom = o->opt >> 2;
    if (type == 0 || dom == 1) return false;
    return type == 3 || type == need;
}
/* 기계어 모양 요약: 첫 메모리 접근부터 둘째까지 (예: "STR DMB.ISH STR") */
static const char *sig(const op_t *o, int n, char *buf, size_t cap) {
    int a = 0, b = -1;
    buf[0] = '\0';
    if (!two_mem(o, n, &a, &b)) return buf;
    for (int i = a; i <= b; i++) {
        static const char *const DOM[] = { "OSH", "NSH", "ISH", "" };
        static const char *const TYP[] = { "?", "LD", "ST", "" };
        char one[24];
        if (o[i].k == K_OTHER) continue;
        if (o[i].k == K_DMB || o[i].k == K_DSB)
            snprintf(one, sizeof one, "%s.%s%s", KNAME[o[i].k], (o[i].opt >> 2) == 3 && (o[i].opt & 3) == 3 ? "SY" : DOM[o[i].opt >> 2],
                     (o[i].opt >> 2) == 3 && (o[i].opt & 3) == 3 ? "" : TYP[o[i].opt & 3]);
        else snprintf(one, sizeof one, "%s", KNAME[o[i].k]);
        if (buf[0]) strncat(buf, " ", cap - strlen(buf) - 1);
        strncat(buf, one, cap - strlen(buf) - 1);
    }
    return buf;
}

/* B1. 첫 두 접근이 store, store 일 때: 다른 코어가 둘째를 봤다면 첫째도 보이나?
 *     (둘째가 STLR 이거나, 사이에 ST 또는 전부를 순서 짓는 DMB/DSB — NSH 제외) */
static bool st_st_ordered(const op_t *o, int n) {
    int a, b;
    if (!two_mem(o, n, &a, &b) || !is_st(o[a].k) || !is_st(o[b].k)) return false;
    if (o[b].k == K_STLR) return true;
    for (int i = a + 1; i < b; i++)
        if (bar_covers(&o[i], NEED_ST)) return true;
    return false;
}
/* B2. 첫 두 접근이 load, load 일 때: 둘째가 첫째보다 "먼저" 만족될 수 없나?
 *     첫째가 LDAR/LDAPR, 사이에 LD 또는 전부를 순서 짓는 배리어, 주소 의존(첫째 결과 → EOR/AND/ADD → 둘째 주소),
 *     또는 제어 의존(첫째 결과로 CBZ) + 그 뒤 ISB. 제어 의존만으로는 부족하다. */
static bool ld_ld_ordered(const op_t *o, int n) {
    int a, b;
    if (!two_mem(o, n, &a, &b) || !is_ld(o[a].k) || !is_ld(o[b].k)) return false;
    if (o[a].k == K_LDAR || o[a].k == K_LDAPR) return true;
    uint32_t taint = 1u << o[a].rt;
    bool ctrl = false;
    for (int i = a + 1; i < b; i++) {
        if (bar_covers(&o[i], NEED_LD)) return true;
        if (o[i].k == K_ALU) {
            bool in = (taint >> o[i].rn & 1u) || (o[i].rm < 32 && (taint >> o[i].rm & 1u));
            if (in) taint |= 1u << o[i].rt; else taint &= ~(1u << o[i].rt);
        }
        if (o[i].k == K_CBZ && (taint >> o[i].rt & 1u)) ctrl = true;
        if (o[i].k == K_ISB && ctrl) return true;
    }
    return (taint >> o[b].rn & 1u) || (o[b].rm < 32 && (taint >> o[b].rm & 1u));
}
/* B3. 첫 접근 store, 둘째 load (다른 주소): load 가 store 를 앞지르지 못하나?
 *     사이에 "전부" 배리어(DMB ISH/SY, DSB), 또는 STLR 다음 LDAR (RCsc). STLR→LDAPR 은 아니다 (RCpc). */
static bool st_ld_ordered(const op_t *o, int n) {
    int a, b;
    if (!two_mem(o, n, &a, &b) || !is_st(o[a].k) || !is_ld(o[b].k)) return false;
    if (o[a].k == K_STLR && o[b].k == K_LDAR) return true;
    for (int i = a + 1; i < b; i++)
        if (bar_covers(&o[i], NEED_ALL)) return true;
    return false;
}

/* 주어짐: MP 의 (flag=1, data=0) 는 writer 와 reader 가 둘 다 순서를 지킬 때만 금지된다.
 * SB 의 (0, 0) 은 두 스레드 모두 store→load 를 지킬 때만 금지. (ARMv8 = other-multi-copy-atomic) */
static bool mp_forbidden(const op_t *w, int nw, const op_t *r, int nr) { return st_st_ordered(w, nw) && ld_ld_ordered(r, nr); }
static bool sb_forbidden(const op_t *a, int na, const op_t *b, int nb) { return st_ld_ordered(a, na) && st_ld_ordered(b, nb); }

/* ===== 연습 2 — 인코더 ===== */
enum { CRM_OSHLD = 1, CRM_OSHST = 2, CRM_OSH = 3, CRM_NSH = 7, CRM_ISHLD = 9, CRM_ISHST = 10, CRM_ISH = 11,
       CRM_LD = 13, CRM_ST = 14, CRM_SY = 15 };
static uint32_t enc_dmb(unsigned crm) { return 0xD50330BFu | (crm & 15u) << 8; }
static uint32_t enc_dsb(unsigned crm) { return 0xD503309Fu | (crm & 15u) << 8; }
static uint32_t enc_isb(void) { return 0xD5033FDFu; }
static uint32_t enc_ldar_w(unsigned rt, unsigned rn) { return 0x88DFFC00u | (rn & 31u) << 5 | (rt & 31u); }
static uint32_t enc_stlr_w(unsigned rt, unsigned rn) { return 0x889FFC00u | (rn & 31u) << 5 | (rt & 31u); }
static uint32_t enc_ldapr_w(unsigned rt, unsigned rn) { return 0xB8BFC000u | (rn & 31u) << 5 | (rt & 31u); }
/* LDR/STR Wt,[Xn,#off]: imm12 = off/4 (4의 배수, 0..16380) */
static uint32_t enc_ldr_w(unsigned rt, unsigned rn, unsigned off) { return 0xB9400000u | (off / 4u & 0xFFFu) << 10 | (rn & 31u) << 5 | (rt & 31u); }
static uint32_t enc_str_w(unsigned rt, unsigned rn, unsigned off) { return 0xB9000000u | (off / 4u & 0xFFFu) << 10 | (rn & 31u) << 5 | (rt & 31u); }
#define ENC_RET 0xD65F03C0u

/* ===== 연습 3 — 상황별 배리어 레시피 ===== */
enum sit { S_SYSREG, S_TLB, S_SMC, S_WFI, S_M_MPU, S_M_CONTROL, S_M_FPU, S_COUNT };
static const char *recipe(enum sit s) {
    switch (s) {
    case S_SYSREG:    return "MSR ; ISB";                                    /* 예: CPACR_EL1.FPEN 켠 뒤 FP 명령 */
    case S_TLB:       return "DSB ISHST ; TLBI ; DSB ISH ; ISB";             /* 페이지 테이블 수정 → 무효화 → 완료 → 재인출 */
    case S_SMC:       return "DC CVAU ; DSB ISH ; IC IVAU ; DSB ISH ; ISB";  /* 코드를 쓰고 실행 (일반형) */
    case S_WFI:       return "DSB ; WFI";                                    /* 진행 중 접근을 끝내고 잠든다 */
    case S_M_MPU:     return "DSB ; ISB";                                    /* MPU_CTRL 등 MPU 설정 뒤 */
    case S_M_CONTROL: return "MSR ; ISB";                                    /* CONTROL(SPSEL/nPRIV) 쓴 뒤 */
    case S_M_FPU:     return "DSB ; ISB";                                    /* SCB->CPACR 로 CP10/CP11 켠 뒤 FP 명령 전 */
    default:          return "";
    }
}

/* ===== 연습 4 — JIT 한 단어 패치 ===== */
typedef struct { uint32_t *page; size_t cap; } jit_t;
static int jit_open(jit_t *j) {
    void *p = mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    if (p == MAP_FAILED) return -1;
    j->page = p;
    j->cap = 16384 / 4;
    return 0;
}
static int jit_load(jit_t *j, const uint32_t *code, size_t n) {
    if (n == 0 || n > j->cap) return -1;
    pthread_jit_write_protect_np(0);
    memcpy(j->page, code, n * 4);
    pthread_jit_write_protect_np(1);
    sys_icache_invalidate(j->page, n * 4);
    return 0;
}
/* 한 단어만 바꾸고 그 4바이트만 동기화 (핫 패치: 분기 하나, 즉값 하나) */
static int jit_patch(jit_t *j, size_t idx, uint32_t word) {
    if (idx >= j->cap) return -1;
    pthread_jit_write_protect_np(0);
    j->page[idx] = word;
    pthread_jit_write_protect_np(1);
    sys_icache_invalidate(&j->page[idx], 4);
    return 0;
}
static uint32_t jit_call_p(const jit_t *j, const uint32_t *p) {
    uint32_t (*f)(const uint32_t *) = (uint32_t (*)(const uint32_t *))(uintptr_t)j->page;
    return f(p);
}

/* ===== 하네스 ===== */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define NW(a) ((int)(sizeof(a) / sizeof((a)[0])))
static op_t g_ops[2][64];
static int g_n[2];
static int dec(int slot, uintptr_t fn) { return g_n[slot] = decode_fn(fn, g_ops[slot], 64); }
static atomic_int g_go, g_ready;
static void start_line(void) {
    atomic_fetch_add(&g_ready, 1);
    while (!atomic_load_explicit(&g_go, memory_order_acquire)) cpu_relax();
}
static _Alignas(128) uint32_t g_md;
static _Alignas(128) uint32_t g_mf;
typedef struct { wfn w; rfn r; uint64_t iters, weak; } mp_job;
static void *mp_wthr(void *a) {
    mp_job *j = a;
    start_line();
    for (uint64_t i = 1; i <= j->iters; i++) j->w(&g_md, &g_mf, (uint32_t)i);
    return NULL;
}
static void *mp_rthr(void *a) {
    mp_job *j = a;
    uint64_t weak = 0;
    start_line();
    for (uint64_t i = 1; i <= j->iters; i++) {
        uint64_t fd = j->r(&g_mf, &g_md);
        weak += (uint32_t)fd < (uint32_t)(fd >> 32);
    }
    j->weak = weak;
    return NULL;
}
static uint64_t run_mp(wfn w, rfn r, uint64_t iters) {
    mp_job j = { w, r, iters, 0 };
    pthread_t a, b;
    g_md = g_mf = 0;
    atomic_store(&g_go, 0);
    atomic_store(&g_ready, 0);
    pthread_create(&a, NULL, mp_wthr, &j);
    pthread_create(&b, NULL, mp_rthr, &j);
    while (atomic_load(&g_ready) != 2) cpu_relax();
    atomic_store_explicit(&g_go, 1, memory_order_release);
    pthread_join(a, NULL);
    pthread_join(b, NULL);
    return j.weak;
}
static bool judge(int kind, const uint32_t *w, int n) {
    op_t o[8];
    decode_words(w, n, o);
    return kind == 0 ? st_st_ordered(o, n) : kind == 1 ? ld_ld_ordered(o, n) : st_ld_ordered(o, n);
}

int main(void) {
    char sb[96];
    /* ---- 연습 1 ---- */
    uint32_t d = 3u, f = 4u;
    w_dmbst(&d, &f, 9u);
    CHECK(d == 9u && f == 9u);
    CHECK(r_dmbld(&f, &d) == ((uint64_t)9 << 32 | 9u) && r_ctrl(&f, &d) == ((uint64_t)9 << 32 | 9u));
    f = 0u;                                                       /* cbz 가 "건너뛰는" 쪽도 같은 값 */
    CHECK(r_ctrlisb(&f, &d) == 9u);
    CHECK(strcmp(sig(g_ops[0], dec(0, (uintptr_t)w_dmbst), sb, sizeof sb), "STR DMB.ISHST STR") == 0);
    CHECK(strcmp(sig(g_ops[0], dec(0, (uintptr_t)r_dmbld), sb, sizeof sb), "LDR DMB.ISHLD LDR") == 0);
    CHECK(strcmp(sig(g_ops[0], dec(0, (uintptr_t)r_ctrl), sb, sizeof sb), "LDR CBZ LDR") == 0);
    CHECK(strcmp(sig(g_ops[0], dec(0, (uintptr_t)r_ctrlisb), sb, sizeof sb), "LDR CBZ ISB LDR") == 0);
    static const struct { wfn w; rfn r; bool forb; } MP[] = {
        { w_dmbst, r_dmbld, true }, { w_stlr, r_ctrl, false }, { w_stlr, r_ctrlisb, true }, { w_plain, r_dmbld, false },
    };
    for (int i = 0; i < NW(MP); i++) {
        dec(0, (uintptr_t)MP[i].w);
        dec(1, (uintptr_t)MP[i].r);
        CHECK(mp_forbidden(g_ops[0], g_n[0], g_ops[1], g_n[1]) == MP[i].forb);
    }
    uint64_t e1 = run_mp(w_dmbst, r_dmbld, 1000000u), e2 = run_mp(w_stlr, r_ctrlisb, 1000000u);
    uint64_t e3 = run_mp(w_stlr, r_ctrl, 1000000u);
    printf("ex1 MP 1M: dmbst/dmbld=%llu stlr/ctrl+isb=%llu  stlr/ctrl=%llu (info, not checked)\n",
           (unsigned long long)e1, (unsigned long long)e2, (unsigned long long)e3);
    CHECK(e1 == 0u && e2 == 0u);                                   /* 금지된 결과는 매 실행 0 */

    /* ---- 연습 2 ---- (기대값 = llvm-objdump 출력) */
    CHECK(enc_dmb(CRM_ISH) == 0xd5033bbfu && enc_dmb(CRM_ISHST) == 0xd5033abfu && enc_dmb(CRM_ISHLD) == 0xd50339bfu);
    CHECK(enc_dmb(CRM_SY) == 0xd5033fbfu && enc_dmb(CRM_OSH) == 0xd50333bfu && enc_dmb(CRM_NSH) == 0xd50337bfu);
    CHECK(enc_dmb(CRM_LD) == 0xd5033dbfu && enc_dmb(CRM_ST) == 0xd5033ebfu && enc_dmb(CRM_OSHLD) == 0xd50331bfu);
    CHECK(enc_dsb(CRM_ISH) == 0xd5033b9fu && enc_dsb(CRM_SY) == 0xd5033f9fu && enc_dsb(CRM_ISHST) == 0xd5033a9fu);
    CHECK(enc_isb() == 0xd5033fdfu);
    CHECK(enc_stlr_w(2, 1) == 0x889ffc22u && enc_ldar_w(3, 0) == 0x88dffc03u && enc_ldapr_w(3, 0) == 0xb8bfc003u);
    CHECK(enc_stlr_w(7, 9) == 0x889ffd27u && enc_ldar_w(7, 9) == 0x88dffd27u && enc_ldapr_w(7, 9) == 0xb8bfc127u);
    CHECK(enc_ldr_w(0, 0, 4) == 0xb9400400u && enc_ldr_w(7, 9, 8) == 0xb9400927u && enc_str_w(7, 9, 8) == 0xb9000927u);
    op_t o = decode(enc_dmb(CRM_ISHST));
    CHECK(o.k == K_DMB && o.opt == CRM_ISHST);
    o = decode(enc_ldapr_w(5, 6));
    CHECK(o.k == K_LDAPR && o.rt == 5 && o.rn == 6);
    o = decode(enc_stlr_w(30, 2));
    CHECK(o.k == K_STLR && o.rt == 30 && o.rn == 2);

    /* ---- 연습 3 ---- */
    CHECK(strcmp(recipe(S_SYSREG), "MSR ; ISB") == 0);
    CHECK(strcmp(recipe(S_TLB), "DSB ISHST ; TLBI ; DSB ISH ; ISB") == 0);
    CHECK(strcmp(recipe(S_SMC), "DC CVAU ; DSB ISH ; IC IVAU ; DSB ISH ; ISB") == 0);
    CHECK(strcmp(recipe(S_WFI), "DSB ; WFI") == 0);
    CHECK(strcmp(recipe(S_M_MPU), "DSB ; ISB") == 0 && strcmp(recipe(S_M_FPU), "DSB ; ISB") == 0);
    CHECK(strcmp(recipe(S_M_CONTROL), "MSR ; ISB") == 0 && recipe(S_COUNT)[0] == '\0');

    /* ---- 연습 4 ---- */
    jit_t j;
    static uint32_t cell[2] = { 0x1111u, 0x2222u };
    CHECK(jit_open(&j) == 0);
    uint32_t code[] = { enc_ldapr_w(0, 0), ENC_RET };
    CHECK(jit_load(&j, code, 2) == 0 && jit_call_p(&j, cell) == 0x1111u);    /* ldapr w0,[x0] */
    CHECK(jit_patch(&j, 0, enc_ldar_w(0, 0)) == 0 && jit_call_p(&j, cell) == 0x1111u);
    CHECK(jit_patch(&j, 0, enc_ldr_w(0, 0, 4)) == 0 && jit_call_p(&j, cell) == 0x2222u);  /* ldr w0,[x0,#4] */
    CHECK(jit_patch(&j, j.cap, ENC_RET) == -1);
    int ok = 1;
    for (unsigned k = 0; k < 1000u; k++) {
        if (jit_patch(&j, 0, enc_ldr_w(0, 0, (k & 1u) * 4u)) != 0 || jit_call_p(&j, cell) != cell[k & 1u]) { ok = 0; break; }
    }
    CHECK(ok);
    munmap(j.page, 16384);

    /* ---- 연습 5 ---- */
    static const struct { uint32_t w[4]; int n, kind; bool ordered; } T[] = {
        { { 0xb9000002, 0xd50333bf, 0xb9000022 }, 3, 0, true },   /* dmb osh: 더 넓은 도메인도 OK   */
        { { 0xb9000002, 0xd50332bf, 0xb9000022 }, 3, 0, true },   /* dmb oshst                      */
        { { 0xb9000002, 0xd5033ebf, 0xb9000022 }, 3, 0, true },   /* dmb st (= full system, ST)     */
        { { 0xb9400003, 0xd5033dbf, 0xb9400024 }, 3, 1, true },   /* dmb ld                          */
        { { 0xb9400003, 0xd50337bf, 0xb9400024 }, 3, 1, false },  /* dmb nsh: 다른 코어엔 의미 없음 */
        { { 0xb9400003, 0xd503399f, 0xb9400024 }, 3, 1, true },   /* dsb ishld                       */
        { { 0xb9000002, 0xd5033f9f, 0xb9400023 }, 3, 2, true },   /* dsb sy: st→ld 도 막는다         */
        { { 0xb9000002, 0xd5033fdf, 0xb9400023 }, 3, 2, false },  /* isb 는 메모리 배리어가 아니다   */
        { { 0xb9000002, 0xd5033dbf, 0xb9400023 }, 3, 2, false },  /* dmb ld: 앞의 store 는 안 막음   */
        { { 0x889ffc02, 0xd50339bf, 0xb8bfc023 }, 3, 2, false },  /* stlr ; dmb ishld ; ldapr        */
    };
    for (int i = 0; i < NW(T); i++) {
        bool got = judge(T[i].kind, T[i].w, T[i].n);
        if (got != T[i].ordered) printf("  ex5 case %d: got %d want %d\n", i, got, T[i].ordered);
        CHECK(got == T[i].ordered);
    }
    {   /* SB 전체: 두 스레드가 같은 모양일 때 */
        static const uint32_t full[] = { 0xb9000002, 0xd5033fbf, 0xb9400023 }, isb[] = { 0xb9000002, 0xd5033fdf, 0xb9400023 };
        op_t a[3], b[3];
        decode_words(full, 3, a);
        decode_words(isb, 3, b);
        CHECK(sb_forbidden(a, 3, a, 3) && !sb_forbidden(a, 3, b, 3) && !sb_forbidden(b, 3, b, 3));  /* dmb sy 둘 다 필요 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
