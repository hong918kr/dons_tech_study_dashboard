/* ARM-A04 I-cache 일관성: JIT 코드 생성과 캐시 유지보수 — 인코더 + JIT + 테스트
 *
 * build (Mac): cc -std=c11 -Wall -Wextra -Werror -O2 {starter|solution}.c {starter|solution}.S shared_ref.S -o /tmp/ARM-A04_v
 *
 * 층 구조
 *   Part A  인코더 enc_*  /  디코더 insn_decode  (골든 = shared_ref.S 를 clang 이 어셈블한 진짜 워드)
 *           JIT: MAP_JIT + pthread_jit_write_protect_np + 캐시 유지보수 → 함수 포인터로 호출
 *           asm cache_sync_range (DC CVAU → DSB → IC IVAU → DSB → ISB) 의 줄 개수 검증
 *   Part B  W^X 위반 → 자식 프로세스가 실제 시그널로 죽는지, CTR_EL0 디코드, 이웃 인코딩 거절
 *   Part C  "표현식 JIT": y = (a*x + b) >> s 를 mul/add/asr 워드로 컴파일, 인터프리터와 비교
 * 테스트는 결정적이다: "유지보수를 빼면 옛 코드가 실행된다" 같은 마이크로아키텍처 현상은 CHECK 하지 않는다
 * (그건 demo_stale.c 가 실측). 생성 코드는 실행 전에 jit_verify 로 검사 → 인코더 버그가 크래시 대신 FAIL 이 된다.
 */
#if !defined(__APPLE__)
#define _GNU_SOURCE                           /* Linux: MAP_ANONYMOUS 등 */
#endif
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <libkern/OSCacheControl.h>           /* sys_icache_invalidate */
#include <pthread.h>                          /* pthread_jit_write_protect_np */
#endif

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------- asm (solution.S / shared_ref.S) ---------- */
uint64_t cache_sync_range(void *start, size_t len, size_t dline, size_t iline, unsigned flags);
uint64_t read_ctr_el0(void);
extern const uint32_t ref_insns[], ref_insns_end[];
#define SKIP_DC 1u                            /* CTR_EL0.IDC = 1 */
#define SKIP_IC 2u                            /* CTR_EL0.DIC = 1 */

/* ================= Part A: 인코더 (64-bit X 레지스터 형식만) ================= */
/* 모두: 범위를 벗어나면 false 를 돌려주고 *out 을 건드리지 않는다. 레지스터 번호 31 의 의미는 명령마다 다르다
 * (ADD imm 의 Rd/Rn=31 → SP, MOVZ/MOVK 의 Rd=31 → XZR). */

/* ADD Xd|SP, Xn|SP, #imm12 {, LSL #12}   = 1 00 100010 sh imm12 Rn Rd */
bool enc_add_imm(uint32_t *out, unsigned rd, unsigned rn, uint32_t imm12, unsigned sh) {
    /* TODO: 범위 검사 (rd, rn ≤ 31, imm12 ≤ 0xFFF, sh ≤ 1) 후 0x91000000 | sh<<22 | imm12<<10 | rn<<5 | rd */
    (void)out; (void)rd; (void)rn; (void)imm12; (void)sh;
    return false;
}

/* MOVZ Xd, #imm16, LSL #(16*hw)          = 1 10 100101 hw imm16 Rd */
bool enc_movz(uint32_t *out, unsigned rd, uint32_t imm16, unsigned hw) {
    /* TODO: 범위 검사 후 0xD2800000 | hw<<21 | imm16<<5 | rd */
    (void)out; (void)rd; (void)imm16; (void)hw;
    return false;
}

/* MOVK Xd, #imm16, LSL #(16*hw)          = 1 11 100101 hw imm16 Rd  (다른 비트는 유지) */
bool enc_movk(uint32_t *out, unsigned rd, uint32_t imm16, unsigned hw) {
    /* TODO: MOVZ 와 opc 만 다르다 (0xF2800000) */
    (void)out; (void)rd; (void)imm16; (void)hw;
    return false;
}

/* RET Xn                                  = 1101011 0 0 10 11111 000000 Rn 00000 */
bool enc_ret(uint32_t *out, unsigned rn) {
    /* TODO */ (void)out; (void)rn;
    return false;

}

/* B / BL: imm26 = off/4, 범위 [-2^27, 2^27 - 4] 바이트 (±128 MB), 4 의 배수 */
static bool enc_imm26(uint32_t *out, uint32_t op, int64_t off) {
    /* TODO: 4의 배수인지, [-2^27, 2^27-4] 안인지 검사 → op | (off/4 의 하위 26비트)
     *       (음수 off 를 >> 하면 구현정의 — uint64_t 로 바꾼 뒤 시프트하고 마스크) */
    (void)out; (void)op; (void)off;
    return false;
}
bool enc_b(uint32_t *out, int64_t off)  { return enc_imm26(out, 0x14000000u, off); }
bool enc_bl(uint32_t *out, int64_t off) { return enc_imm26(out, 0x94000000u, off); }

/* CBZ Xt, label                           = 1 011010 0 imm19 Rt, 범위 [-2^20, 2^20 - 4] (±1 MB) */
bool enc_cbz(uint32_t *out, unsigned rt, int64_t off) {
    /* TODO: rt ≤ 31, 4의 배수, [-2^20, 2^20-4] → 0xB4000000 | imm19<<5 | rt */
    (void)out; (void)rt; (void)off;
    return false;

}

/* ---------- 주어진 인코더 (Part C 용, TODO 아님) ---------- */
static uint32_t enc_mul(unsigned rd, unsigned rn, unsigned rm)     /* MADD Xd, Xn, Xm, XZR */
{ return 0x9B007C00u | rm << 16 | rn << 5 | rd; }
static uint32_t enc_add_reg(unsigned rd, unsigned rn, unsigned rm) /* ADD Xd, Xn, Xm (LSL #0) */
{ return 0x8B000000u | rm << 16 | rn << 5 | rd; }
static uint32_t enc_asr(unsigned rd, unsigned rn, unsigned s)      /* SBFM Xd, Xn, #s, #63 */
{ return 0x9340FC00u | s << 16 | rn << 5 | rd; }

/* ================= Part A: 디코더 ================= */
enum ik { IK_UNKNOWN, IK_ADD_IMM, IK_MOVZ, IK_MOVK, IK_RET, IK_B, IK_BL, IK_CBZ };
struct insn { enum ik kind; unsigned rd, rn; uint32_t imm; unsigned sh; int64_t off; };

static int64_t sext(uint64_t v, unsigned bits) {          /* 하위 bits 비트를 부호 확장 (구현정의 변환 없이) */
    uint64_t m = UINT64_C(1) << (bits - 1);
    return (int64_t)(v & (m - 1)) - (int64_t)(v & m);
}

/* 위 7가지만 알아본다. 나머지(이웃 인코딩 포함)는 IK_UNKNOWN + false. */
bool insn_decode(uint32_t w, struct insn *d) {
    memset(d, 0, sizeof *d);
    /* TODO: ADD imm (bit23 = 0 까지 확인), MOVZ/MOVK (opc 로 구분, MOVN 제외), RET Xn, B/BL (bit31), 64-bit CBZ
     *       를 알아보고 필드를 채운다. 분기 off 는 바이트 단위 (sext(imm) * 4). 나머지는 IK_UNKNOWN + false. */
    (void)w;
    d->kind = IK_UNKNOWN;
    return false;












}

/* ================= Part B: CTR_EL0 디코드 ================= */
struct ctr_info { unsigned iline, dline, erg, cwg, l1ip; bool idc, dic; };  /* 크기는 바이트 */

/* 필드 = log2(워드 수) → 바이트 = 4 << 필드. ERG/CWG 가 0 이면 "정보 없음" → 아키텍처 최대 2 KB 로 가정.
 * bit31 은 RES1: 0 이면 CTR 값이 아니다(읽기 실패 등) → false. */
bool ctr_decode(uint64_t ctr, struct ctr_info *ci) {
    /* TODO: bit31(RES1) 확인, IminLine[3:0] L1Ip[15:14] DminLine[19:16] ERG[23:20] CWG[27:24] IDC[28] DIC[29] */
    (void)ctr; (void)ci;
    return false;







}

/* ================= 플랫폼 층 (주어짐) ================= */
struct cache_geo { size_t dline, iline; unsigned flags; const char *src; };
static struct cache_geo g_geo;

static void cache_geo_init(struct cache_geo *g) {
#ifdef __APPLE__
    /* macOS: EL0 에서 mrs ctr_el0 은 SIGILL (실측) → CTR 을 못 읽는다.
     * libsystem 의 sys_icache_invalidate 가 64 B 간격으로 ic ivau 하는 것(디스어셈블 실측)과
     * Lima 게스트에서 읽은 CTR_EL0 (Iminline=Dminline=64 B) 을 근거로 64 B. IDC/DIC 는 모른다고 보고 둘 다 수행. */
    g->dline = g->iline = 64; g->flags = 0; g->src = "macOS fallback (CTR_EL0 trapped)";
#else
    struct ctr_info ci;
    if (ctr_decode(read_ctr_el0(), &ci)) {
        g->dline = ci.dline; g->iline = ci.iline; g->src = "CTR_EL0";
        g->flags = (ci.idc ? SKIP_DC : 0) | (ci.dic ? SKIP_IC : 0);
    } else { g->dline = g->iline = 16; g->flags = 0; g->src = "fallback 16"; }
#endif
}

#ifdef __APPLE__
static void jit_writable(bool on) { pthread_jit_write_protect_np(on ? 0 : 1); }  /* 이 스레드만 전환 */
static void os_icache_sync(void *p, size_t n) { sys_icache_invalidate(p, n); }
#define JIT_PROT  (PROT_READ | PROT_WRITE | PROT_EXEC)
#define JIT_FLAGS (MAP_PRIVATE | MAP_ANON | MAP_JIT)
#else
static void jit_writable(bool on) { (void)on; }                                    /* Linux 연습용: RWX 매핑 */
static void os_icache_sync(void *p, size_t n) { __builtin___clear_cache((char *)p, (char *)p + n); }
#define JIT_PROT  (PROT_READ | PROT_WRITE | PROT_EXEC)
#define JIT_FLAGS (MAP_PRIVATE | MAP_ANONYMOUS)
#endif

#define RET_WORD 0xD65F03C0u
struct jit { uint32_t *code; size_t words; };

/* MAP_JIT 영역을 만들고 전부 RET 로 채운다 → 패치가 안 된 자리를 불러도 f(x) = x 로 안전하게 돌아온다 */
static bool jit_alloc(struct jit *j, size_t bytes) {
    void *p = mmap(NULL, bytes, JIT_PROT, JIT_FLAGS, -1, 0);
    if (p == MAP_FAILED) { j->code = NULL; j->words = 0; return false; }
    j->code = p; j->words = bytes / 4;
    jit_writable(true);
    for (size_t i = 0; i < j->words; i++) j->code[i] = RET_WORD;
    jit_writable(false);
    os_icache_sync(j->code, bytes);
    return true;
}
static void jit_free(struct jit *j) { if (j->code) munmap(j->code, j->words * 4); j->code = NULL; }

/* ================= Part A: JIT 패치 ================= */
enum maint { MAINT_NONE, MAINT_SYS, MAINT_MANUAL };

/* j->code[at .. at+n) 에 w[0..n) 을 쓰고, m 에 따라 캐시 유지보수. 범위 밖이면 false.
 * 각 워드는 32비트 한 번의 store (단일 복사 원자성 — 다른 스레드가 실행 중인 B 하나를 바꾸는 핫패치의 전제). */
bool jit_patch(struct jit *j, size_t at, const uint32_t *w, size_t n, enum maint m) {
    /* TODO: 범위 검사 → jit_writable(true) → 워드마다 32비트 store → jit_writable(false)
     *       → m 에 따라 os_icache_sync(dst, n*4) 또는 cache_sync_range(dst, n*4, g_geo.dline, g_geo.iline, g_geo.flags) */
    (void)j; (void)at; (void)w; (void)n; (void)m;
    return false;




}

/* ---------- 생성 코드 검사기 (주어짐): 실행해도 안전한 명령만, 앞으로만 가는 분기만 ---------- */
#define VERIFY_WORDS 256u                                 /* JIT 테스트는 code[0..256) 안에서만 */
static bool reg_ok(uint32_t r) { return r <= 17; }        /* x18(플랫폼), x19+(callee-saved), sp 금지 */
static bool jit_verify(const uint32_t *w, size_t n) {
    bool has_ret = false;
    for (size_t i = 0; i < n; i++) {
        uint32_t x = w[i], rd = x & 31, rn = (x >> 5) & 31, rm = (x >> 16) & 31;
        if ((x & 0xFF800000u) == 0x91000000u)                                   /* ADD imm */
            { if (!reg_ok(rd) || !reg_ok(rn)) return false; }
        else if ((x & 0xFF800000u) == 0xD2800000u || (x & 0xFF800000u) == 0xF2800000u)
            { if (!reg_ok(rd)) return false; }                                  /* MOVZ / MOVK */
        else if ((x & 0xFFE0FC00u) == 0x8B000000u || (x & 0xFFE0FC00u) == 0x9B007C00u)
            { if (!reg_ok(rd) || !reg_ok(rn) || !reg_ok(rm)) return false; }   /* ADD reg / MUL */
        else if ((x & 0xFFC0FC00u) == 0x9340FC00u)                              /* ASR imm */
            { if (!reg_ok(rd) || !reg_ok(rn)) return false; }
        else if (x == RET_WORD) has_ret = true;
        else if ((x & 0xFC000000u) == 0x14000000u || (x & 0xFF000000u) == 0xB4000000u) {
            int64_t off = (x >> 26) == 5 ? sext(x & 0x03FFFFFFu, 26) : sext((x >> 5) & 0x7FFFFu, 19);
            if (off <= 0 || (uint64_t)off >= n - i) return false;               /* 앞으로, 창 안에서만 */
            if ((x >> 26) != 5 && !reg_ok(rd)) return false;
        } else return false;
    }
    return has_ret;
}

typedef uint64_t (*jit_fn)(uint64_t);
/* 검사 통과 시에만 호출. 실패하면 false (테스트가 FAIL 로 센다) */
static bool jit_call(const struct jit *j, size_t at, uint64_t x, uint64_t *y) {
    if (!j->code || at >= VERIFY_WORDS || !jit_verify(j->code, VERIFY_WORDS)) return false;
    jit_fn f = (jit_fn)(uintptr_t)(j->code + at);
    *y = f(x);
    return true;
}

/* f(x) = x + imm (imm < 2^24): add #lo12 ; [add #hi12, lsl #12] ; ret  (주어짐, 인코더 사용) */
static size_t gen_add_const(uint32_t *w, uint32_t imm) {
    size_t n = 0;
    if (!enc_add_imm(&w[n++], 0, 0, imm & 0xFFFu, 0)) return 0;
    if ((imm >> 12) && !enc_add_imm(&w[n++], 0, 0, imm >> 12, 1)) return 0;
    if (!enc_ret(&w[n++], 30)) return 0;
    return n;
}

/* ================= Part C: 표현식 JIT ================= */
/* y = (a*x + b) >> s   (64비트 랩어라운드 곱·합, 산술 시프트, s ≤ 63). 반환 = 워드 수 (0 = 실패).
 * a == 1 → mul 생략, b == 0 → add 생략, b < 4096 → add 즉시값, s == 0 → asr 생략. 상수는 x9(a), x10(b). */
size_t emit_imm64(uint32_t *w, unsigned rd, uint64_t v) {
    /* TODO: movz rd, #h0 그리고 0 이 아닌 h1..h3 마다 movk. 반환 = 워드 수 (실패 0) */
    (void)w; (void)rd; (void)v;
    return 0;




}
size_t compile_affine(uint32_t *buf, size_t cap, uint64_t a, uint64_t b, unsigned s) {
    /* TODO: 위 주석의 규칙대로 t[] 에 워드를 모은 뒤 cap 검사, buf 로 복사, 워드 수 반환 */
    (void)buf; (void)cap; (void)a; (void)b; (void)s;
    return 0;














}

/* 참조 인터프리터 (주어짐): 부호 없는 산술로 UB/구현정의 없이 */
static uint64_t asr64(uint64_t u, unsigned s) {
    if (s == 0) return u;
    return (u >> s) | ((u >> 63) ? ~(UINT64_MAX >> s) : 0);
}
static uint64_t interp_affine(uint64_t x, uint64_t a, uint64_t b, unsigned s) { return asr64(a * x + b, s); }

/* ================= 테스트 도우미 ================= */
static uint64_t g_rng = 0x9E3779B97F4A7C15u;
static uint64_t xs(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }

/* 줄 개수 모델: [p, p+len) 이 걸치는 line 크기 블록 수 */
static uint64_t lines_model(uintptr_t p, size_t len, size_t line) {
    return len ? (uint64_t)((p + len - 1) / line - p / line + 1) : 0;
}

#ifdef __APPLE__
/* 자식 프로세스에서 fn 실행 → 죽인 시그널 번호 (정상 종료면 -(exit code) - 1000) */
static struct jit *g_child_jit;
static int run_child(void (*fn)(void)) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) { signal(SIGBUS, SIG_DFL); signal(SIGSEGV, SIG_DFL); signal(SIGILL, SIG_DFL); fn(); _exit(0); }
    int st = 0;
    if (pid < 0 || waitpid(pid, &st, 0) != pid) return -2000;
    return WIFSIGNALED(st) ? WTERMSIG(st) : -1000 - WEXITSTATUS(st);
}
static void child_write_while_rx(void) { jit_writable(false); ((volatile uint32_t *)g_child_jit->code)[32] = RET_WORD; }
static void child_exec_while_rw(void)  { jit_writable(true); jit_fn f = (jit_fn)(uintptr_t)(g_child_jit->code + 32); (void)f(1); }
static void child_read_ctr(void)       { uint64_t v = read_ctr_el0(); printf("INFO  child read CTR_EL0 = 0x%" PRIx64 "\n", v); fflush(stdout); }
#endif

/* 골든 표: shared_ref.S 의 ref_insns[i] 와 1:1 (word 열 = /usr/bin/objdump -d 실측) */
struct golden { uint32_t word; enum ik kind; unsigned rd, rn; uint32_t imm; unsigned sh; int64_t off; const char *txt; };
static const struct golden G[] = {
    {0x91000400u, IK_ADD_IMM,  0,  0,      1, 0, 0, "add x0, x0, #1"},
    {0x913ffc41u, IK_ADD_IMM,  1,  2, 0xFFF,  0, 0, "add x1, x2, #4095"},
    {0x91400483u, IK_ADD_IMM,  3,  4,      1, 1, 0, "add x3, x4, #1, lsl #12"},
    {0x917ffcc5u, IK_ADD_IMM,  5,  6, 0xFFF,  1, 0, "add x5, x6, #4095, lsl #12"},
    {0x910043ffu, IK_ADD_IMM, 31, 31,     16, 0, 0, "add sp, sp, #16"},
    {0x910003e0u, IK_ADD_IMM,  0, 31,      0, 0, 0, "mov x0, sp"},
    {0xd297dde9u, IK_MOVZ,     9,  0, 0xBEEF, 0, 0, "movz x9, #0xbeef"},
    {0xd2bbd5a9u, IK_MOVZ,     9,  0, 0xDEAD, 1, 0, "movz x9, #0xdead, lsl #16"},
    {0xf2e24689u, IK_MOVK,     9,  0, 0x1234, 3, 0, "movk x9, #0x1234, lsl #48"},
    {0xf2dffff1u, IK_MOVK,    17,  0, 0xFFFF, 2, 0, "movk x17, #0xffff, lsl #32"},
    {0xd65f03c0u, IK_RET,      0, 30,      0, 0, 0, "ret"},
    {0xd65f0200u, IK_RET,      0, 16,      0, 0, 0, "ret x16"},
    {0x14000010u, IK_B,        0,  0, 0, 0,        0x40, "b . + 0x40"},
    {0x17fffffeu, IK_B,        0,  0, 0, 0,          -8, "b . - 8"},
    {0x15ffffffu, IK_B,        0,  0, 0, 0,   0x7fffffc, "b . + 0x7fffffc"},
    {0x16000000u, IK_B,        0,  0, 0, 0,  -0x8000000, "b . - 0x8000000"},
    {0x94000400u, IK_BL,       0,  0, 0, 0,      0x1000, "bl . + 0x1000"},
    {0x97ffffffu, IK_BL,       0,  0, 0, 0,          -4, "bl . - 4"},
    {0xb4000103u, IK_CBZ,      3,  0, 0, 0,        0x20, "cbz x3, . + 0x20"},
    {0xb4800000u, IK_CBZ,      0,  0, 0, 0,   -0x100000, "cbz x0, . - 0x100000"},
    {0xb47ffff1u, IK_CBZ,     17,  0, 0, 0,     0xffffc, "cbz x17, . + 0xffffc"},
};
#define NG (sizeof G / sizeof G[0])
#define REF_MUL 21                                      /* shared_ref.S 줄 번호 */
#define REF_NEIGHBOURS 25                               /* 25..32: 거절해야 할 이웃 인코딩 */

static bool encode_golden(const struct golden *g, uint32_t *w) {
    switch (g->kind) {
    case IK_ADD_IMM: return enc_add_imm(w, g->rd, g->rn, g->imm, g->sh);
    case IK_MOVZ:    return enc_movz(w, g->rd, g->imm, g->sh);
    case IK_MOVK:    return enc_movk(w, g->rd, g->imm, g->sh);
    case IK_RET:     return enc_ret(w, g->rn);
    case IK_B:       return enc_b(w, g->off);
    case IK_BL:      return enc_bl(w, g->off);
    case IK_CBZ:     return enc_cbz(w, g->rd, g->off);
    default:         return false;
    }
}
static bool same_insn(const struct insn *d, const struct golden *g) {
    return d->kind == g->kind && d->rd == g->rd && d->rn == g->rn && d->imm == g->imm && d->sh == g->sh && d->off == g->off;
}

int main(void) {
    cache_geo_init(&g_geo);
    printf("INFO  cache geometry: dline=%zu iline=%zu flags=%u (%s)\n", g_geo.dline, g_geo.iline, g_geo.flags, g_geo.src);

    /* ---------- Part A-1: 골든 (진짜 어셈블러 워드) ---------- */
    size_t nref = (size_t)(ref_insns_end - ref_insns);
    CHECK(nref == 33);
    int enc_bad = 0, dec_bad = 0, tab_bad = 0;
    for (size_t i = 0; i < NG; i++) {
        uint32_t w = 0; struct insn d;
        if (ref_insns[i] != G[i].word) { tab_bad++; printf("  ref[%zu] 0x%08x != table 0x%08x\n", i, ref_insns[i], G[i].word); }
        if (!encode_golden(&G[i], &w) || w != ref_insns[i]) { if (enc_bad++ < 3) printf("  enc %-28s -> 0x%08x (want 0x%08x)\n", G[i].txt, w, ref_insns[i]); }
        if (!insn_decode(ref_insns[i], &d) || !same_insn(&d, &G[i])) { if (dec_bad++ < 3) printf("  dec %-28s failed\n", G[i].txt); }
    }
    CHECK(tab_bad == 0);
    CHECK(enc_bad == 0);
    CHECK(dec_bad == 0);
    CHECK(enc_mul(0, 0, 9) == ref_insns[REF_MUL] && enc_add_reg(0, 0, 10) == ref_insns[REF_MUL + 1]);
    CHECK(enc_asr(0, 0, 5) == ref_insns[REF_MUL + 2] && enc_asr(7, 3, 63) == ref_insns[REF_MUL + 3]);

    /* ---------- Part A-2: 왕복 (전수 + 무작위) ---------- */
    int rt_bad = 0;
    for (uint32_t imm = 0; imm < 4096; imm++)
        for (unsigned sh = 0; sh < 2; sh++) {
            uint32_t w = 0; struct insn d; unsigned rd = imm % 32, rn = (imm * 7 + sh) % 32;
            if (!enc_add_imm(&w, rd, rn, imm, sh) || !insn_decode(w, &d) || d.kind != IK_ADD_IMM ||
                d.rd != rd || d.rn != rn || d.imm != imm || d.sh != sh) rt_bad++;
        }
    CHECK(rt_bad == 0);                                             /* ADD imm: imm12 × sh 전수 */
    rt_bad = 0;
    for (uint32_t imm = 0; imm < 65536; imm++)
        for (unsigned hw = 0; hw < 4; hw++) {
            uint32_t wz = 0, wk = 0; struct insn dz, dk; unsigned rd = (imm + hw) % 32;
            if (!enc_movz(&wz, rd, imm, hw) || !enc_movk(&wk, rd, imm, hw) || !insn_decode(wz, &dz) || !insn_decode(wk, &dk) ||
                dz.kind != IK_MOVZ || dk.kind != IK_MOVK || dz.imm != imm || dk.imm != imm || dz.sh != hw || dk.rd != rd) rt_bad++;
        }
    CHECK(rt_bad == 0);                                             /* MOVZ/MOVK: imm16 × hw 전수 */
    rt_bad = 0;
    for (int i = 0; i < 30000; i++) {
        int64_t ob = (int64_t)(xs() % (UINT64_C(1) << 26)) * 4 - (INT64_C(1) << 27);   /* [-2^27, 2^27-4] */
        int64_t oc = (int64_t)(xs() % (UINT64_C(1) << 19)) * 4 - (INT64_C(1) << 20);   /* [-2^20, 2^20-4] */
        unsigned rt = (unsigned)(xs() % 32);
        uint32_t wb = 0, wl = 0, wc = 0; struct insn db, dl, dc;
        if (!enc_b(&wb, ob) || !enc_bl(&wl, ob) || !enc_cbz(&wc, rt, oc) || !insn_decode(wb, &db) || !insn_decode(wl, &dl) ||
            !insn_decode(wc, &dc) || db.kind != IK_B || dl.kind != IK_BL || dc.kind != IK_CBZ ||
            db.off != ob || dl.off != ob || dc.off != oc || dc.rd != rt) rt_bad++;
    }
    CHECK(rt_bad == 0);                                             /* B/BL/CBZ: 무작위 30000 */

    /* ---------- Part A-3: 범위 검사 ---------- */
    uint32_t w = 0xAAAAAAAAu;
    CHECK(!enc_add_imm(&w, 0, 0, 4096, 0) && !enc_add_imm(&w, 32, 0, 1, 0) && !enc_add_imm(&w, 0, 0, 1, 2));
    CHECK(!enc_movz(&w, 0, 0x10000, 0) && !enc_movk(&w, 0, 1, 4) && !enc_ret(&w, 32));
    CHECK(!enc_b(&w, INT64_C(1) << 27) && !enc_b(&w, -(INT64_C(1) << 27) - 4) && !enc_bl(&w, 2) && !enc_b(&w, -6));
    CHECK(!enc_cbz(&w, 0, INT64_C(1) << 20) && !enc_cbz(&w, 0, -(INT64_C(1) << 20) - 4) && !enc_cbz(&w, 0, 1) && !enc_cbz(&w, 32, 0));
    CHECK(w == 0xAAAAAAAAu);                                        /* 실패한 인코더는 *out 을 안 건드린다 */
    CHECK(enc_b(&w, (INT64_C(1) << 27) - 4) && w == 0x15ffffffu && enc_cbz(&w, 0, -(INT64_C(1) << 20)) && w == 0xb4800000u);

    /* ---------- Part A-4: asm cache_sync_range 줄 개수 (전수: 시작 오프셋 × 길이 × 줄 크기 × flags) ---------- */
    static uint8_t buf[1024] __attribute__((aligned(256)));
    int cnt_bad = 0;
    const size_t lines[] = {16, 64};
    for (size_t li = 0; li < 2; li++)
        for (size_t off = 0; off < 128; off++)
            for (size_t len = 0; len <= 260; len += (len < 140 ? 1 : 7)) {
                size_t L = lines[li], L2 = L == 16 ? 64 : 16;   /* D, I 줄 크기를 다르게 줘서 섞임도 검사 */
                uint64_t r = cache_sync_range(buf + off, len, L, L2, 0);
                uint64_t want = lines_model((uintptr_t)(buf + off), len, L) << 32 | lines_model((uintptr_t)(buf + off), len, L2);
                if (r != want) { if (cnt_bad < 3) printf("  off=%zu len=%zu dl=%zu il=%zu: got 0x%" PRIx64 " want 0x%" PRIx64 "\n", off, len, L, L2, r, want); cnt_bad++; }
            }
    CHECK(cnt_bad == 0);
    CHECK(cache_sync_range(buf + 60, 8, 64, 64, 0) == (UINT64_C(2) << 32 | 2));     /* 4 B + 4 B 가 두 줄에 걸침 */
    CHECK(cache_sync_range(buf + 1, 64, 64, 64, SKIP_DC) == 2);                     /* IDC=1: DC 생략, IC 만 */
    CHECK(cache_sync_range(buf, 256, 64, 64, SKIP_IC) == (UINT64_C(4) << 32));      /* DIC=1: IC 생략 */
    CHECK(cache_sync_range(buf, 256, 64, 64, SKIP_DC | SKIP_IC) == 0 && cache_sync_range(buf, 0, 64, 64, 0) == 0);

    /* ---------- Part A-5: JIT — 같은 자리를 계속 다시 패치 (유지보수 2가지) ---------- */
    struct jit j;
    CHECK(jit_alloc(&j, 16384));
    { uint64_t y = 0; CHECK(jit_call(&j, 16, 41, &y) && y == 41); }   /* RET 로 채워짐 → f(x) = x */
    for (int mode = MAINT_SYS; mode <= MAINT_MANUAL; mode++) {
        int bad = 0, ran = 0;
        for (int i = 0; i < 3000; i++) {
            uint32_t imm = i < 8 ? (uint32_t[]){0, 1, 4095, 4096, 4097, 0xFFF000, 0xFFFFFF, 12345}[i] : (uint32_t)(xs() & 0xFFFFFF);
            uint32_t code[4]; size_t n = gen_add_const(code, imm); uint64_t x = xs() >> 8, y = 0;
            if (!n || !jit_patch(&j, 16, code, n, (enum maint)mode) || !jit_call(&j, 16, x, &y) || y != x + imm) bad++;
            else ran++;
        }
        printf("INFO  %s: %d patch+call ok\n", mode == MAINT_SYS ? "sys_icache_invalidate" : "manual DC/IC/ISB", ran);
        CHECK(bad == 0);
    }

    /* cbz 를 실행: f(x) = x ? x + 5 : 0x77 */
    {
        uint32_t c[5]; bool ok = enc_cbz(&c[0], 0, 12) && enc_add_imm(&c[1], 0, 0, 5, 0) && enc_ret(&c[2], 30) &&
                                 enc_movz(&c[3], 0, 0x77, 0) && enc_ret(&c[4], 30);
        uint64_t y0 = 0, y3 = 0;
        CHECK(ok && jit_patch(&j, 32, c, 5, MAINT_SYS) && jit_call(&j, 32, 0, &y0) && jit_call(&j, 32, 3, &y3) && y0 == 0x77 && y3 == 8);
    }
    /* 핫패치: 입구(word 48)의 B 하나만 바꿔 구현을 갈아끼운다 (impl1 @64: +1, impl2 @96: +2) */
    {
        uint32_t i1[2], i2[2], e = 0; uint64_t y1 = 0, y2 = 0;
        bool ok = enc_add_imm(&i1[0], 0, 0, 1, 0) && enc_ret(&i1[1], 30) && enc_add_imm(&i2[0], 0, 0, 2, 0) && enc_ret(&i2[1], 30) &&
                  jit_patch(&j, 64, i1, 2, MAINT_SYS) && jit_patch(&j, 96, i2, 2, MAINT_SYS) &&
                  enc_b(&e, (64 - 48) * 4) && jit_patch(&j, 48, &e, 1, MAINT_SYS) && jit_call(&j, 48, 10, &y1) &&
                  enc_b(&e, (96 - 48) * 4) && jit_patch(&j, 48, &e, 1, MAINT_MANUAL) && jit_call(&j, 48, 10, &y2);
        CHECK(ok && y1 == 11 && y2 == 12);
    }
    CHECK(!jit_patch(&j, j.words - 1, (uint32_t[]){RET_WORD, RET_WORD}, 2, MAINT_SYS));   /* 범위 밖 */

    /* ---------- Part B-1: W^X 위반 → 진짜 시그널 (fork 한 자식) ---------- */
#ifdef __APPLE__
    g_child_jit = &j;
    int s1 = run_child(child_write_while_rx), s2 = run_child(child_exec_while_rw);
    printf("INFO  write while RX -> signal %d (%s), exec while RW -> signal %d (%s)\n",
           s1, s1 > 0 ? strsignal(s1) : "none", s2, s2 > 0 ? strsignal(s2) : "none");
    CHECK(s1 == SIGBUS || s1 == SIGSEGV);
    CHECK(s2 == SIGBUS || s2 == SIGSEGV);
    int s3 = run_child(child_read_ctr);
    printf("INFO  mrs ctr_el0 at EL0 -> %s %d\n", s3 > 0 ? "signal" : "exit", s3 > 0 ? s3 : -(s3 + 1000));
#endif

    /* ---------- Part B-2: CTR_EL0 디코드 (QEMU CPU 모델 / Lima 게스트에서 실제로 읽은 값) ---------- */
    struct ctr_info ci;
    CHECK(ctr_decode(UINT64_C(0x84448004), &ci) && ci.iline == 64 && ci.dline == 64 && ci.l1ip == 2 && !ci.idc && !ci.dic &&
          ci.erg == 64 && ci.cwg == 64);                            /* qemu -cpu cortex-a53 */
    CHECK(ctr_decode(UINT64_C(0x8444c004), &ci) && ci.l1ip == 3 && !ci.idc && !ci.dic);   /* cortex-a72 / a76 (PIPT) */
    CHECK(ctr_decode(UINT64_C(0x9444c004), &ci) && ci.idc && !ci.dic && ci.iline == 64);  /* neoverse-v1: IDC=1 */
    CHECK(ctr_decode(UINT64_C(0x86668006), &ci) && ci.iline == 256 && ci.dline == 256 && ci.erg == 256 && ci.cwg == 256); /* a64fx */
    CHECK(ctr_decode(UINT64_C(0x80038003), &ci) && ci.iline == 32 && ci.dline == 32 && ci.erg == 2048 && ci.cwg == 2048); /* qemu-user max */
    CHECK(ctr_decode(UINT64_C(0x99444c004), &ci) && ci.idc && !ci.dic && ci.iline == 64 && ci.dline == 64);  /* Lima 게스트 (M6) */
    CHECK(ctr_decode(UINT64_C(0xB0000000), &ci) && ci.idc && ci.dic && ci.iline == 4);    /* 합성: IDC=DIC=1 */
    CHECK(!ctr_decode(0, &ci) && !ctr_decode(UINT64_C(0x0444c004), &ci));                /* RES1 비트 없음 → 거절 */

    /* ---------- Part B-3: 이웃 인코딩은 "모름" ---------- */
    int nb_bad = 0;
    for (size_t i = REF_NEIGHBOURS; i < nref; i++) { struct insn d; if (insn_decode(ref_insns[i], &d) || d.kind != IK_UNKNOWN) nb_bad++; }
    CHECK(nb_bad == 0);                                             /* b.ne cbnz movn addg sub add-w retaa br */
    { struct insn d; CHECK(!insn_decode(0, &d) && !insn_decode(0xFFFFFFFFu, &d) && !insn_decode(0xD503201Fu, &d)); }  /* udf, ?, nop */

    /* ---------- Part C: 표현식 JIT y = (a*x + b) >> s ---------- */
    {
        uint32_t code[16]; int bad = 0, ran = 0;
        CHECK(compile_affine(code, 16, 1, 0, 0) == 1 && code[0] == RET_WORD);          /* 항등: ret 하나 */
        CHECK(compile_affine(code, 16, 3, 7, 2) == 5);                                  /* movz x9 · mul · add #7 · asr · ret */
        CHECK(compile_affine(code, 16, UINT64_C(0xFFFFFFFFFFFFFFFD), UINT64_C(0x123456789), 63) == 4 + 1 + 3 + 1 + 1 + 1);
        CHECK(compile_affine(code, 16, 2, 0, 64) == 0 && compile_affine(code, 2, 3, 7, 2) == 0);
        for (int i = 0; i < 4000; i++) {
            uint64_t a, b; unsigned s = (unsigned)(xs() % 64);
            switch (i % 4) {                                            /* 상수 모양을 섞는다 */
            case 0:  a = xs(); b = xs(); break;
            case 1:  a = xs() & 0xFFFF; b = xs() & 0xFFF; break;
            case 2:  a = (uint64_t)-(int64_t)(xs() & 0xFF); b = 0; break;   /* 음수 계수 */
            default: a = 1; b = xs() >> (xs() % 64); s = i % 3 ? s : 0; break;
            }
            size_t n = compile_affine(code, 16, a, b, s);
            if (!n || !jit_patch(&j, 128, code, n, MAINT_SYS)) { bad++; continue; }
            for (int t = 0; t < 4; t++) {
                uint64_t x = t == 0 ? 0 : t == 1 ? UINT64_MAX : xs(), y = 0;
                if (!jit_call(&j, 128, x, &y) || y != interp_affine(x, a, b, s)) bad++; else ran++;
            }
        }
        printf("INFO  affine JIT: %d calls matched the interpreter\n", ran);
        CHECK(bad == 0);
        /* 펌웨어 예: ADC 보정 y = (gain_Q16 * raw + offset) >> 16, gain 1.25, offset -100 LSB (Q16) */
        uint64_t gain = 81920, off = (uint64_t)(-100 * 65536); uint64_t y = 0;
        size_t n = compile_affine(code, 16, gain, off, 16);
        CHECK(n && jit_patch(&j, 128, code, n, MAINT_MANUAL) && jit_call(&j, 128, 2000, &y) && y == 2400);
        CHECK(jit_call(&j, 128, 40, &y) && y == (uint64_t)-50);         /* 1.25·40 − 100 = −50 */
    }
    jit_free(&j);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
