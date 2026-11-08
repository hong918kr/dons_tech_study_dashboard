/* ARM-M05 메모리 배리어 명령: DMB, DSB, ISB, LDAR/STLR
 *
 * arm64 호스트(Apple Silicon)에서 배리어 명령을 "직접" 쓰고, 그 기계어를 다시 읽어 판정한다.
 *   Part A  리트머스 커널을 GNU inline asm 으로: MP writer(str;str) / reader(ldr;ldr), SB(str;ldr)
 *           의 배리어 버전 9개 — dmb ish, stlr, ldar, ldapr, 주소 의존성
 *   Part B  ARMv8 순서 판정기: 디코더(주어짐)가 만든 명령 목록으로 st→st / ld→ld / st→ld 가
 *           "다른 코어가 보기에 순서 보장" 인지 판정 → 내가 쓴 커널의 실제 기계어에 적용
 *   Part C  펌웨어: (1) 코어 간 메일박스 (stlr 게시 / ldapr 수신 / ack)  (2) JIT: W^X + I-cache 동기화
 *
 * 체크 테스트는 매 실행 성립하는 것만 본다: 값, 기계어 모양, 판정 결과, "금지된" 결과가 0 인지,
 * 메일박스 메시지가 온전한지, JIT 결과. 재배치 "관찰 횟수" 는 demo_litmus_asm.c 가 센다.
 * 손으로 쓴 asm 접근은 TSan 이 보지 못한다 (계측되지 않음).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _DARWIN_C_SOURCE                       /* MAP_JIT, pthread_jit_write_protect_np */
#include <libkern/OSCacheControl.h>            /* sys_icache_invalidate */
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
#define MAYBE_UNUSED __attribute__((unused))   /* 시작 코드에서 아직 안 쓰는 도우미의 경고 끄기 */
static inline void cpu_relax(void) { __asm__ __volatile__("yield" ::: "memory"); }

/* ======================= Part A — 리트머스 커널 (inline asm) ======================= */
/* MP writer: *data = v 다음 *flag = v.  reader: f = *flag 다음 d = *data, 반환 (f << 32) | d.
 * SB: *mine = v 다음 r = *other, 반환 r.   "memory" 클로버 = 컴파일러도 이 asm 너머로 메모리 접근을 못 옮긴다. */
typedef void (*wfn)(uint32_t *data, uint32_t *flag, uint32_t v);
typedef uint64_t (*rfn)(const uint32_t *flag, const uint32_t *data);
typedef uint32_t (*sfn)(uint32_t *mine, const uint32_t *other, uint32_t v);

/* 예시 (주어짐): 배리어 없는 버전 */
NOINLINE static void w_plain(uint32_t *data, uint32_t *flag, uint32_t v) {
    __asm__ __volatile__("str %w[v], [%[d]]\n\t"
                         "str %w[v], [%[f]]"
                         :: [d] "r"(data), [f] "r"(flag), [v] "r"(v) : "memory");
}
NOINLINE static uint64_t r_plain(const uint32_t *flag, const uint32_t *data) {
    uint64_t f, d;
    __asm__ __volatile__("ldr %w[rf], [%[f]]\n\t"
                         "ldr %w[rd], [%[d]]"
                         : [rf] "=&r"(f), [rd] "=&r"(d) : [f] "r"(flag), [d] "r"(data) : "memory");
    return f << 32 | d;
}
NOINLINE static uint32_t s_plain(uint32_t *mine, const uint32_t *other, uint32_t v) {
    uint32_t r;
    __asm__ __volatile__("str %w[v], [%[m]]\n\t"
                         "ldr %w[r], [%[o]]"
                         : [r] "=&r"(r) : [m] "r"(mine), [o] "r"(other), [v] "r"(v) : "memory");
    return r;
}

/* A1. 두 store 사이에 dmb ish (inner shareable, 모든 접근 순서) */
NOINLINE static void w_dmb(uint32_t *data, uint32_t *flag, uint32_t v) {
    (void)data; (void)flag; (void)v; /* TODO */
}
/* A2. flag 를 store-release (stlr) 로: 앞의 모든 접근이 이 store 보다 먼저 보인다 */
NOINLINE static void w_stlr(uint32_t *data, uint32_t *flag, uint32_t v) {
    (void)data; (void)flag; (void)v; /* TODO */
}
/* A3. 두 load 사이에 dmb ish */
NOINLINE static uint64_t r_dmb(const uint32_t *flag, const uint32_t *data) {
    (void)flag; (void)data;
    return 0; /* TODO */
}
/* A4. flag 를 load-acquire (ldar, RCsc) 로 */
NOINLINE static uint64_t r_ldar(const uint32_t *flag, const uint32_t *data) {
    (void)flag; (void)data;
    return 0; /* TODO */
}
/* A5. flag 를 load-acquire RCpc (ldapr, FEAT_LRCPC) 로 */
NOINLINE static uint64_t r_ldapr(const uint32_t *flag, const uint32_t *data) {
    (void)flag; (void)data;
    return 0; /* TODO */
}
/* A6. 배리어 없이 주소 의존성: t = f ^ f (항상 0) 을 data 주소에 더해 읽는다 */
NOINLINE static uint64_t r_addr(const uint32_t *flag, const uint32_t *data) {
    (void)flag; (void)data;
    return 0; /* TODO */
}
/* A7. SB: store 와 load 사이에 dmb ish */
NOINLINE static uint32_t s_dmb(uint32_t *mine, const uint32_t *other, uint32_t v) {
    (void)mine; (void)other; (void)v;
    return 0; /* TODO */
}
/* A8. SB: stlr 다음 ldar (RCsc — 이 둘은 서로 순서가 지켜진다) */
NOINLINE static uint32_t s_rcsc(uint32_t *mine, const uint32_t *other, uint32_t v) {
    (void)mine; (void)other; (void)v;
    return 0; /* TODO */
}
/* A9. SB: stlr 다음 ldapr (RCpc — ldapr 은 앞선 stlr 를 앞지를 수 있다) */
NOINLINE static uint32_t s_rcpc(uint32_t *mine, const uint32_t *other, uint32_t v) {
    (void)mine; (void)other; (void)v;
    return 0; /* TODO */
}

/* ======================= Part B — 디코더(주어짐) + 순서 판정기 ======================= */
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
    (void)o; (void)n;
    return false; /* TODO */
}
/* B2. 첫 두 접근이 load, load 일 때: 둘째가 첫째보다 "먼저" 만족될 수 없나?
 *     첫째가 LDAR/LDAPR, 사이에 LD 또는 전부를 순서 짓는 배리어, 주소 의존(첫째 결과 → EOR/AND/ADD → 둘째 주소),
 *     또는 제어 의존(첫째 결과로 CBZ) + 그 뒤 ISB. 제어 의존만으로는 부족하다. */
static bool ld_ld_ordered(const op_t *o, int n) {
    (void)o; (void)n;
    return false; /* TODO */
}
/* B3. 첫 접근 store, 둘째 load (다른 주소): load 가 store 를 앞지르지 못하나?
 *     사이에 "전부" 배리어(DMB ISH/SY, DSB), 또는 STLR 다음 LDAR (RCsc). STLR→LDAPR 은 아니다 (RCpc). */
static bool st_ld_ordered(const op_t *o, int n) {
    (void)o; (void)n;
    return false; /* TODO */
}

/* 주어짐: MP 의 (flag=1, data=0) 는 writer 와 reader 가 둘 다 순서를 지킬 때만 금지된다.
 * SB 의 (0, 0) 은 두 스레드 모두 store→load 를 지킬 때만 금지. (ARMv8 = other-multi-copy-atomic) */
static bool mp_forbidden(const op_t *w, int nw, const op_t *r, int nr) { return st_st_ordered(w, nw) && ld_ld_ordered(r, nr); }
static bool sb_forbidden(const op_t *a, int na, const op_t *b, int nb) { return st_ld_ordered(a, na) && st_ld_ordered(b, nb); }

/* ======================= Part C — 펌웨어: 메일박스 + JIT ======================= */
/* C1. 두 코어(예: Cortex-A 앱 코어 → 실시간 코어) 공유 SRAM 의 1칸 메일박스.
 *     seq/ack 는 아래 두 헬퍼(asm)로만, payload 는 relaxed 원자(= 일반 ldr/str)로 접근한다. */
#define MB_WORDS 4
typedef struct {
    _Alignas(128) uint32_t seq;                /* 보낸 쪽이 올림: 마지막으로 게시한 메시지 번호 */
    _Alignas(128) uint32_t ack;                /* 받은 쪽이 올림: 마지막으로 다 읽은 메시지 번호 */
    _Alignas(128) _Atomic uint32_t w[MB_WORDS];
} mbox_t;
MAYBE_UNUSED static inline void st_rel(uint32_t *p, uint32_t v) { __asm__ __volatile__("stlr %w1, [%0]" :: "r"(p), "r"(v) : "memory"); }
MAYBE_UNUSED static inline uint32_t ld_acq(const uint32_t *p) {
    uint32_t v;
    __asm__ __volatile__("ldapr %w0, [%1]" : "=r"(v) : "r"(p) : "memory");
    return v;
}
/* 보내기: ack == seq-1 (이전 메시지를 다 읽음) 을 spins 번 안에 못 보면 -1.
 * 아니면 payload 를 쓰고 마지막에 seq 를 release 로 게시하고 0. */
static int mbox_send(mbox_t *m, uint32_t seq, const uint32_t *payload, uint32_t spins) {
    (void)m; (void)seq; (void)payload; (void)spins;
    return -1; /* TODO */
}
/* 받기(폴링 1회): seq == want 를 봤으면 payload 를 out 에 복사하고 ack = want 를 release 로 쓴 뒤 1, 아니면 0. */
static int mbox_poll(mbox_t *m, uint32_t want, uint32_t *out) {
    (void)m; (void)want; (void)out;
    return 0; /* TODO */
}

/* C2. JIT: MAP_JIT 페이지에 기계어를 넣고 실행. Apple 은 W^X 를 스레드별 스위치로 강제한다. */
typedef struct { uint32_t *page; size_t cap; } jit_t;   /* cap = 단어 수 */
static int jit_open(jit_t *j) {                          /* 주어짐 */
    void *p = mmap(NULL, 16384, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
    if (p == MAP_FAILED) return -1;
    j->page = p;
    j->cap = 16384 / 4;
    return 0;
}
/* code[0..n) 을 페이지 맨 앞에 넣고 "다음 호출이 새 명령을 실행" 하게 만든다. n == 0 이나 n > cap 이면 -1.
 * 순서: 쓰기 허용(이 스레드) → 복사 → 실행 허용으로 되돌림 → I-cache 동기화(해당 범위) */
static int jit_load(jit_t *j, const uint32_t *code, size_t n) {
    (void)j; (void)code; (void)n;
    return -1; /* TODO */
}
static int jit_call(const jit_t *j, int a, int b) {     /* 주어짐: AAPCS64 — a=w0, b=w1, 결과 w0 */
    int (*f)(int, int) = (int (*)(int, int))(uintptr_t)j->page;
    return f(a, b);
}
/* 인코더 (주어짐, objdump 로 확인): mov w0,#imm16 / add·sub w,w,w / ret */
static uint32_t enc_movz_w(unsigned rd, unsigned imm16) { return 0x52800000u | (imm16 & 0xFFFFu) << 5 | (rd & 31u); }
static uint32_t enc_add_w(unsigned rd, unsigned rn, unsigned rm) { return 0x0B000000u | (rm & 31u) << 16 | (rn & 31u) << 5 | (rd & 31u); }
static uint32_t enc_sub_w(unsigned rd, unsigned rn, unsigned rm) { return 0x4B000000u | (rm & 31u) << 16 | (rn & 31u) << 5 | (rd & 31u); }
#define ENC_RET 0xD65F03C0u

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define FAILS() (g_total - g_pass)
static void skip(int n, const char *what) {
    g_total += n;
    printf("SKIP %d stress checks (%s): unit checks failed\n", n, what);
}
#define NW(a) ((int)(sizeof(a) / sizeof((a)[0])))
static op_t g_ops[2][64];
static int g_n[2];
static int dec(int slot, uintptr_t fn) { return g_n[slot] = decode_fn(fn, g_ops[slot], 64); }

/* 리트머스 스트레스 (2 스레드, CPU 고정 불가 — macOS) */
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
        weak += (uint32_t)fd < (uint32_t)(fd >> 32);    /* data < flag: 옛 data 를 봤다 */
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

/* 메일박스 스트레스: 보내는 코어 = 스레드 */
#define NMSG 100000u
static mbox_t g_mb;
static uint32_t pay(uint32_t seq, int i) { return seq * 2654435761u + (uint32_t)i * 40503u; }
static atomic_int g_send_fail;
static void *sender(void *a) {
    (void)a;
    for (uint32_t s = 1; s <= NMSG; s++) {
        uint32_t p[MB_WORDS];
        for (int i = 0; i < MB_WORDS; i++) p[i] = pay(s, i);
        if (mbox_send(&g_mb, s, p, 1u << 30) != 0) { atomic_store(&g_send_fail, 1); break; }
    }
    return NULL;
}

int main(void) {
    char sb[96];
    /* ---------- Part A: 단일 스레드 값 확인 + 기계어 모양 ---------- */
    int f0 = FAILS();
    uint32_t d = 0, f = 0, x = 7, y = 9;
    w_dmb(&d, &f, 5u);  CHECK(d == 5u && f == 5u);
    w_stlr(&d, &f, 6u); CHECK(d == 6u && f == 6u);
    d = 11u; f = 22u;
    CHECK(r_dmb(&f, &d) == ((uint64_t)22 << 32 | 11u));
    CHECK(r_ldar(&f, &d) == ((uint64_t)22 << 32 | 11u));
    CHECK(r_ldapr(&f, &d) == ((uint64_t)22 << 32 | 11u));
    CHECK(r_addr(&f, &d) == ((uint64_t)22 << 32 | 11u));
    CHECK(s_dmb(&x, &y, 1u) == 9u && x == 1u);
    CHECK(s_rcsc(&x, &y, 2u) == 9u && x == 2u);
    CHECK(s_rcpc(&y, &x, 3u) == 2u && y == 3u);
    CHECK(strcmp(sig(g_ops[0], dec(0, (uintptr_t)w_plain), sb, sizeof sb), "STR STR") == 0);   /* 예시 */
    CHECK(strcmp(sig(g_ops[0], dec(0, (uintptr_t)w_dmb),   sb, sizeof sb), "STR DMB.ISH STR") == 0);
    CHECK(strcmp(sig(g_ops[0], dec(0, (uintptr_t)w_stlr),  sb, sizeof sb), "STR STLR") == 0);
    CHECK(strcmp(sig(g_ops[0], dec(0, (uintptr_t)r_dmb),   sb, sizeof sb), "LDR DMB.ISH LDR") == 0);
    CHECK(strcmp(sig(g_ops[0], dec(0, (uintptr_t)r_ldar),  sb, sizeof sb), "LDAR LDR") == 0);
    CHECK(strcmp(sig(g_ops[0], dec(0, (uintptr_t)r_ldapr), sb, sizeof sb), "LDAPR LDR") == 0);
    CHECK(strcmp(sig(g_ops[0], dec(0, (uintptr_t)r_addr),  sb, sizeof sb), "LDR ALU LDR") == 0);
    CHECK(strcmp(sig(g_ops[0], dec(0, (uintptr_t)s_dmb),   sb, sizeof sb), "STR DMB.ISH LDR") == 0);
    CHECK(strcmp(sig(g_ops[0], dec(0, (uintptr_t)s_rcsc),  sb, sizeof sb), "STLR LDAR") == 0);
    CHECK(strcmp(sig(g_ops[0], dec(0, (uintptr_t)s_rcpc),  sb, sizeof sb), "STLR LDAPR") == 0);

    bool a_ok = FAILS() == f0;

    /* ---------- Part B: 판정기 — 손으로 고른 명령열 (인코딩은 objdump 확인값) ---------- */
    static const struct { uint32_t w[4]; int n; int kind; bool ordered; } T[] = {
        /* kind 0 = st→st */
        { { 0xb9000002, 0xb9000022 }, 2, 0, false },                         /* str ; str          */
        { { 0xb9000002, 0xd5033bbf, 0xb9000022 }, 3, 0, true },              /* dmb ish            */
        { { 0xb9000002, 0xd5033abf, 0xb9000022 }, 3, 0, true },              /* dmb ishst          */
        { { 0xb9000002, 0xd50339bf, 0xb9000022 }, 3, 0, false },             /* dmb ishld: st 안 막음 */
        { { 0xb9000002, 0xd50337bf, 0xb9000022 }, 3, 0, false },             /* dmb nsh: 이 코어만 */
        { { 0xb9000002, 0xd5033b9f, 0xb9000022 }, 3, 0, true },              /* dsb ish            */
        { { 0xb9000002, 0x889ffc22 }, 2, 0, true },                          /* str ; stlr         */
        { { 0x889ffc02, 0xb9000022 }, 2, 0, false },                         /* stlr ; str (반대)  */
        /* kind 1 = ld→ld */
        { { 0xb9400003, 0xb9400024 }, 2, 1, false },                         /* ldr ; ldr          */
        { { 0xb9400003, 0xd50339bf, 0xb9400024 }, 3, 1, true },              /* dmb ishld          */
        { { 0xb9400003, 0xd5033abf, 0xb9400024 }, 3, 1, false },             /* dmb ishst: ld 안 막음 */
        { { 0x88dffc03, 0xb9400024 }, 2, 1, true },                          /* ldar ; ldr         */
        { { 0xb8bfc003, 0xb9400024 }, 2, 1, true },                          /* ldapr ; ldr        */
        { { 0xb9400003, 0x88dffc24 }, 2, 1, false },                         /* ldr ; ldar (반대)  */
        { { 0xb9400003, 0xca030065, 0xb8656824 }, 3, 1, true },              /* eor x5,x3,x3 ; ldr [x1,x5] */
        { { 0xb9400003, 0xca0600c5, 0xb8656824 }, 3, 1, false },             /* eor x5,x6,x6: 가짜 의존 */
        { { 0xb9400003, 0xca030065, 0x8b050021, 0xb9400024 }, 4, 1, true },  /* eor ; add x1,x1,x5 ; ldr [x1] */
        { { 0xb9400003, 0x34000023, 0xb9400024 }, 3, 1, false },             /* cbz 만 (제어 의존) */
        { { 0xb9400003, 0x34000023, 0xd5033fdf, 0xb9400024 }, 4, 1, true },  /* cbz ; isb          */
        { { 0xb9400003, 0xd5033fdf, 0xb9400024 }, 3, 1, false },             /* isb 만             */
        /* kind 2 = st→ld */
        { { 0xb9000002, 0xb9400023 }, 2, 2, false },                         /* str ; ldr          */
        { { 0xb9000002, 0xd5033bbf, 0xb9400023 }, 3, 2, true },              /* dmb ish            */
        { { 0xb9000002, 0xd5033abf, 0xb9400023 }, 3, 2, false },             /* dmb ishst          */
        { { 0xb9000002, 0xd50339bf, 0xb9400023 }, 3, 2, false },             /* dmb ishld          */
        { { 0x889ffc02, 0x88dffc23 }, 2, 2, true },                          /* stlr ; ldar (RCsc) */
        { { 0x889ffc02, 0xb8bfc023 }, 2, 2, false },                         /* stlr ; ldapr (RCpc)*/
        { { 0xb9000002, 0x88dffc23 }, 2, 2, false },                         /* str ; ldar         */
    };
    for (int i = 0; i < NW(T); i++) {
        op_t o[4];
        decode_words(T[i].w, T[i].n, o);
        bool got = T[i].kind == 0 ? st_st_ordered(o, T[i].n) : T[i].kind == 1 ? ld_ld_ordered(o, T[i].n) : st_ld_ordered(o, T[i].n);
        if (got != T[i].ordered) printf("  case %d (kind %d): got %d want %d\n", i, T[i].kind, got, T[i].ordered);
        CHECK(got == T[i].ordered);
    }
    /* 내가 쓴 커널의 실제 기계어로 판정 */
    static const struct { wfn w; rfn r; bool forb; } MP[] = {
        { w_plain, r_plain, false }, { w_dmb, r_plain, false }, { w_plain, r_dmb, false },
        { w_dmb, r_dmb, true }, { w_stlr, r_ldar, true }, { w_stlr, r_ldapr, true },
        { w_stlr, r_addr, true }, { w_plain, r_ldapr, false },
    };
    for (int i = 0; i < NW(MP); i++) {
        dec(0, (uintptr_t)MP[i].w);
        dec(1, (uintptr_t)MP[i].r);
        CHECK(mp_forbidden(g_ops[0], g_n[0], g_ops[1], g_n[1]) == MP[i].forb);
    }
    static const struct { sfn s; bool forb; } SB[] = { { s_plain, false }, { s_dmb, true }, { s_rcsc, true }, { s_rcpc, false } };
    for (int i = 0; i < NW(SB); i++) {
        dec(0, (uintptr_t)SB[i].s);
        CHECK(sb_forbidden(g_ops[0], g_n[0], g_ops[0], g_n[0]) == SB[i].forb);
    }

    /* ---------- Part A 스트레스: "금지" 판정 조합은 매 실행 0 이어야 한다 ---------- */
    if (a_ok) {
        uint64_t it = 2000000u;
        uint64_t w_plain_cnt = run_mp(w_plain, r_plain, it);
        uint64_t a1 = run_mp(w_dmb, r_dmb, it), a2 = run_mp(w_stlr, r_ldapr, it), a3 = run_mp(w_stlr, r_addr, it);
        printf("MP stress %llu iters: plain/plain=%llu (info, not checked)  dmb/dmb=%llu stlr/ldapr=%llu stlr/addr=%llu\n",
               (unsigned long long)it, (unsigned long long)w_plain_cnt, (unsigned long long)a1,
               (unsigned long long)a2, (unsigned long long)a3);
        CHECK(a1 == 0u);
        CHECK(a2 == 0u);
        CHECK(a3 == 0u);
    } else skip(3, "Part A MP");

    /* ---------- Part C1: 메일박스 ---------- */
    int f2 = FAILS();
    {
        mbox_t m;
        memset(&m, 0, sizeof m);
        uint32_t p[MB_WORDS] = { 0xA1u, 0xB2u, 0xC3u, 0xD4u }, q[MB_WORDS] = { 0 };
        CHECK(mbox_poll(&m, 1u, q) == 0);                                    /* 아직 없음 */
        CHECK(mbox_send(&m, 1u, p, 100u) == 0 && m.seq == 1u);
        CHECK(atomic_load(&m.w[0]) == 0xA1u && atomic_load(&m.w[3]) == 0xD4u);
        CHECK(mbox_send(&m, 2u, p, 100u) == -1 && m.seq == 1u);              /* ack 전엔 덮어쓰지 않는다 */
        CHECK(mbox_poll(&m, 1u, q) == 1 && memcmp(p, q, sizeof p) == 0 && m.ack == 1u);
        CHECK(mbox_poll(&m, 2u, q) == 0);
        p[0] = 0x55u;
        CHECK(mbox_send(&m, 2u, p, 100u) == 0 && m.seq == 2u && atomic_load(&m.w[0]) == 0x55u);
    }
    if (FAILS() == f2) {
        memset(&g_mb, 0, sizeof g_mb);
        pthread_t t;
        pthread_create(&t, NULL, sender, NULL);
        uint32_t want = 1, bad = 0, q[MB_WORDS];
        uint64_t idle = 0;
        while (want <= NMSG && idle < 400000000u) {
            if (!mbox_poll(&g_mb, want, q)) { idle++; cpu_relax(); continue; }
            for (int i = 0; i < MB_WORDS; i++) bad += q[i] != pay(want, i);
            want++;
            idle = 0;
        }
        pthread_join(t, NULL);
        printf("Part C mailbox: received=%u bad=%u\n", want - 1u, bad);
        CHECK(want - 1u == NMSG && atomic_load(&g_send_fail) == 0);
        CHECK(bad == 0u);
    } else skip(2, "Part C mailbox");

    /* ---------- Part C2: JIT ---------- */
    jit_t j;
    CHECK(jit_open(&j) == 0);
    uint32_t c1[] = { enc_movz_w(0, 42), ENC_RET }, c2[] = { enc_add_w(0, 0, 1), ENC_RET }, c3[] = { enc_sub_w(0, 0, 1), ENC_RET };
    CHECK(c1[0] == 0x52800540u && c2[0] == 0x0b010000u && c3[0] == 0x4b010000u);   /* objdump 값 */
    CHECK(jit_load(&j, c1, 0) == -1 && jit_load(&j, c1, j.cap + 1) == -1);
    CHECK(jit_load(&j, c1, 2) == 0 && jit_call(&j, 3, 4) == 42);
    CHECK(jit_load(&j, c2, 2) == 0 && jit_call(&j, 3, 4) == 7);                 /* 같은 주소, 다른 명령 */
    CHECK(jit_load(&j, c3, 2) == 0 && jit_call(&j, 3, 4) == -1);
    int ok = 1;
    for (unsigned k = 0; k < 2000u; k++) {                                       /* 바꾸고 곧바로 실행을 반복 */
        uint32_t c[] = { enc_movz_w(0, k), ENC_RET };
        if (jit_load(&j, c, 2) != 0 || jit_call(&j, 0, 0) != (int)k) { ok = 0; break; }
    }
    CHECK(ok);
    munmap(j.page, 16384);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
