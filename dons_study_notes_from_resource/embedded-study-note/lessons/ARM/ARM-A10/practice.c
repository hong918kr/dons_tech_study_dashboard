/* ARM-A10 practice.c — 추가 연습 4개를 한 번에 검증
 *  연습 1  GNU property 노트 파서 + 링커 AND 규칙 (VM 의 실제 .note.gnu.property 바이트)
 *  연습 2  PAC 필드 추정: 실측 서명값 몇 개 → va_bits, TBI
 *  연습 3  무차별 대입(brute force) 수학 + 장난감 모델 실험, fork 와 키
 *  연습 4  Cortex-M85 PACBTI 린터 (Thumb 반워드, gen_mac.txt 의 실제 clang 출력)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c shared_ref.S -o /tmp/ARM-A10_practice && /tmp/ARM-A10_practice
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; else printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ======================================================================== 연습 1 */
enum { GNU_PROPERTY_AARCH64_FEATURE_1_AND = 0xC0000000u, FEAT_BTI = 1, FEAT_PAC = 2, FEAT_GCS = 4 };

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* .note.gnu.property 섹션 바이트(리틀엔디언 ELF64) → AArch64 feature 비트. 찾으면 1, 없거나 깨졌으면 0 */
int parse_gnu_property(const uint8_t *sec, size_t len, uint32_t *feat) {
    size_t off = 0;
    while (off + 12 <= len) {
        uint32_t namesz = rd32(sec + off), descsz = rd32(sec + off + 4), type = rd32(sec + off + 8);
        size_t name_off = off + 12, desc_off = name_off + ((namesz + 3u) & ~3u);
        if (desc_off > len || descsz > len - desc_off) return 0;               /* 잘림 */
        if (type == 5 && namesz == 4 && !memcmp(sec + name_off, "GNU", 4)) {  /* NT_GNU_PROPERTY_TYPE_0 */
            size_t p = desc_off, end = desc_off + descsz;
            while (p + 8 <= end) {
                uint32_t pr_type = rd32(sec + p), pr_size = rd32(sec + p + 4);
                if (pr_size > end - p - 8) return 0;
                if (pr_type == GNU_PROPERTY_AARCH64_FEATURE_1_AND) {
                    if (pr_size != 4) return 0;
                    *feat = rd32(sec + p + 8);
                    return 1;
                }
                p += 8 + ((pr_size + 7u) & ~7u);                               /* ELF64: 8바이트 정렬 */
            }
        }
        off = desc_off + ((descsz + 3u) & ~3u);
    }
    return 0;
}

/* 링커: 노트가 없는 오브젝트는 feature = 0 으로 친다 → 전체 AND. force_bti 면 결과에 BTI 를 강제로 OR */
uint32_t link_features(const uint32_t *feat, const int *has_note, int n, int force_bti) {
    uint32_t f = 0xFFFFFFFFu;
    for (int i = 0; i < n; i++) f &= has_note[i] ? feat[i] : 0;
    if (n == 0) f = 0;
    return force_bti ? (f | FEAT_BTI) : f;
}

static void test_ex1(void) {
    /* VM: readelf -x .note.gnu.property forge_std / bti_note (vm 출력 그대로 바이트로) */
    static const uint8_t forge_std[32] = {
        0x04,0,0,0, 0x10,0,0,0, 0x05,0,0,0, 'G','N','U',0,
        0x00,0x00,0x00,0xc0, 0x04,0,0,0, 0x07,0,0,0, 0,0,0,0};
    static const uint8_t bti_note[32] = {
        0x04,0,0,0, 0x10,0,0,0, 0x05,0,0,0, 'G','N','U',0,
        0x00,0x00,0x00,0xc0, 0x04,0,0,0, 0x03,0,0,0, 0,0,0,0};
    uint32_t f = 0;
    CHECK(parse_gnu_property(forge_std, 32, &f) == 1 && f == (FEAT_BTI | FEAT_PAC | FEAT_GCS));   /* "BTI, PAC, GCS" */
    CHECK(parse_gnu_property(bti_note, 32, &f) == 1 && f == (FEAT_BTI | FEAT_PAC));              /* "BTI, PAC" */
    CHECK(parse_gnu_property(forge_std, 20, &f) == 0);                     /* 잘린 섹션 */
    uint8_t bad[32]; memcpy(bad, forge_std, 32); bad[12] = 'X';
    CHECK(parse_gnu_property(bad, 32, &f) == 0);                           /* 이름이 GNU 아님 */
    memcpy(bad, forge_std, 32); bad[20] = 8;                               /* pr_datasz 가 desc 밖으로 */
    CHECK(parse_gnu_property(bad, 32, &f) == 0);
    /* 링크 실험 (run_vm.sh pac 의 실제 결과와 같은지): demo_bti.o = 7 (gcc 가 넣음), demo_bti_pads.o = 3 또는 노트 없음 */
    uint32_t fe[2] = {7, 3}; int has[2] = {1, 1};
    CHECK(link_features(fe, has, 2, 0) == (FEAT_BTI | FEAT_PAC));          /* bti_note   → "BTI, PAC" */
    has[1] = 0;
    CHECK(link_features(fe, has, 2, 0) == 0);                              /* bti_nonote → 노트 없음 */
    CHECK(link_features(fe, has, 2, 1) == FEAT_BTI);                       /* -z force-bti → "BTI" (+ 경고) */
}

/* ======================================================================== 연습 2 */
/* 같은 원본 포인터를 여러 modifier 로 서명한 값들 → PAC 필드 OR → (va_bits, tbi) 추정 */
int infer_pac_field(uint64_t raw, const uint64_t *sig, int n, int *va_bits, int *tbi) {
    uint64_t m = 0;
    for (int i = 0; i < n; i++) m |= sig[i] ^ raw;
    if (m == 0) return 0;                           /* 키가 꺼진 프로세스 (Mac arm64: PACIA 가 NOP) */
    *va_bits = __builtin_ctzll(m);
    *tbi = (m >> 56) == 0;
    return 1;
}

static void test_ex2(void) {
    const uint64_t raw = 0x0000000100004000ull;
    /* probe.txt: Mac arm64e 세 번 실행의 pacia/pacib (I 키) — 키는 실행마다 다르지만 필드 위치는 같다 */
    const uint64_t mac_i[] = {0x9e1a800100004000ull, 0xd532000100004000ull, 0xb262800100004000ull, 0x6176000100004000ull,
                              0xbd28800100004000ull, 0x0501000100004000ull, 0x276e800100004000ull, 0x9e29000100004000ull,
                              0xaa20800100004000ull};
    const uint64_t mac_d[] = {0x007d000100004000ull, 0x0066000100004000ull, 0x0033000100004000ull};
    const uint64_t vm_i[]  = {0x007e000100004000ull, 0x0005000100004000ull, 0x0071000100004000ull, 0x007f000100004000ull,
                              0x0037000100004000ull, 0x003e000100004000ull};
    const uint64_t mac_off[] = {0x0000000100004000ull, 0x0000000100004000ull};   /* Mac arm64: 키 꺼짐 */
    int va = 0, tbi = -1;
    CHECK(infer_pac_field(raw, mac_i, 9, &va, &tbi) && va == 47 && tbi == 0);
    CHECK(infer_pac_field(raw, vm_i, 6, &va, &tbi) && va == 48 && tbi == 1);
    CHECK(!infer_pac_field(raw, mac_off, 2, &va, &tbi));
    /* 표본이 적으면 과소추정할 수 있다: mac_d 세 개는 비트 47 이 모두 0 → 48 로 잘못 나온다 */
    CHECK(infer_pac_field(raw, mac_d, 3, &va, &tbi) && tbi == 1 && va == 48);
    printf("  ex2: Mac D-key 3 samples → va_bits %d (실제 47: 4096-modifier 측정은 0x007f8… = 비트 54:47)\n", va);
}

/* ======================================================================== 연습 3 */
/* w 비트 PAC 을 무작위로 맞힐 때: 성공 확률 p 에 필요한 시도 수 = ceil(log(1-p)/log(1-2^-w)) */
long tries_for(int w, double p) { return (long)ceil(log1p(-p) / log1p(-ldexp(1.0, -w))); }

static uint64_t rng = 0xC0FFEE1234567ull;
static uint64_t xs(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

/* 같은 키로 계속 다시 시도할 수 있는(fork 서버) 공격자의 평균 시도 수 — 장난감: 정답 PAC 을 고정하고 무작위 추측 */
double mean_tries_same_key(int w, int experiments) {
    uint64_t mask = (1ull << w) - 1, sum = 0;
    for (int e = 0; e < experiments; e++) {
        uint64_t secret = xs() & mask, t = 1;
        while ((xs() & mask) != secret) t++;
        sum += t;
    }
    return (double)sum / experiments;
}

static void test_ex3(void) {
    long t7 = tries_for(7, 0.5), t16 = tries_for(16, 0.5), t24 = tries_for(24, 0.5);
    printf("  ex3: 50%% 성공까지 시도 수: 7비트 %ld, 16비트 %ld, 24비트 %ld\n", t7, t16, t24);
    CHECK(t7 == 89 && t16 == 45426);
    /* 교차 검증: 직접 곱해서 */
    double q = 1.0; long k = 0;
    while (q > 0.5) { q *= 1.0 - 1.0 / 128; k++; }
    CHECK(k == t7);
    double mt = mean_tries_same_key(7, 4000);
    printf("  ex3: 7비트, 같은 키 재시도 평균 %.1f 회 (기대 128)\n", mt);
    CHECK(mt > 115 && mt < 141);
    /* fork_keys.txt: VM 은 fork 자식이 부모와 같은 pacia/pacib, exec 후엔 둘 다 다름 */
    const uint64_t vm_parent_ia = 0x007a000100004000ull, vm_fork_ia = 0x007a000100004000ull, vm_exec_ia = 0x0020000100004000ull;
    CHECK(vm_parent_ia == vm_fork_ia && vm_fork_ia != vm_exec_ia);
    /* Mac arm64e: fork 는 같음, exec 후 IB 만 바뀌고 IA 는 그대로였다 (관찰) */
    const uint64_t mac_ia[3] = {0x7a06800100004000ull, 0x7a06800100004000ull, 0x7a06800100004000ull};
    const uint64_t mac_ib[3] = {0x560a800100004000ull, 0x560a800100004000ull, 0x8d4c000100004000ull};
    CHECK(mac_ia[0] == mac_ia[2] && mac_ib[0] == mac_ib[1] && mac_ib[1] != mac_ib[2]);
}

/* ======================================================================== 연습 4 */
/* Armv8.1-M PACBTI (Cortex-M85): 32비트 Thumb 명령은 첫 반워드 상위 5비트가 11101/11110/11111 (ARM-B12) */
static int thumb_len(uint16_t hw1) { unsigned t = hw1 >> 11; return (t == 0x1D || t == 0x1E || t == 0x1F) ? 2 : 1; }

enum { MV_NO_PAD = 1, MV_UNAUTH_RET = 2, MV_UNSIGNED_AUT = 4 };
/* 선형 검사: 첫 명령이 BTI/PACBTI 인가, PACBTI 로 시작했으면 모든 bx lr 앞에 AUT 가 있었나 */
unsigned m_lint(const uint16_t *h, int n) {
    unsigned v = 0;
    int signed_ = 0, first = 1;
    for (int i = 0; i < n; ) {
        int len = thumb_len(h[i]);
        uint32_t insn = len == 2 && i + 1 < n ? (uint32_t)h[i] << 16 | h[i + 1] : h[i];
        if (first) {
            if (insn != 0xF3AF800Fu && insn != 0xF3AF800Du) v |= MV_NO_PAD;
            first = 0;
        }
        if (insn == 0xF3AF800Du) signed_ = 1;                                    /* pacbti r12, lr, sp */
        else if (insn == 0xF3AF802Du) { if (!signed_) v |= MV_UNSIGNED_AUT; signed_ = 0; }  /* aut r12, lr, sp */
        else if (insn == 0x4770u && signed_) v |= MV_UNAUTH_RET;               /* bx lr */
        i += len;
    }
    return v;
}

static void test_ex4(void) {
    /* gen_mac.txt variant=cortex_m85 (clang --target=thumbv8.1m.main-none-eabi -mcpu=cortex-m85 -mbranch-protection=standard) */
    static const uint16_t leaf_add[] = {0xf3af, 0x800f, 0x4408, 0x4770};
    static const uint16_t nonleaf[]  = {0xf3af, 0x800d, 0xe92d, 0x5080, 0xb081, 0xf7ff, 0xfffe, 0x3001, 0xb001, 0xe8bd,
                                        0x5080, 0xf3af, 0x802d, 0x4770, 0xbf00, 0xbf00};
    static const uint16_t early[]    = {0xf3af, 0x800d, 0xe92d, 0x5080, 0xb081, 0x2800, 0xd403, 0xf7ff, 0xfffe, 0x0040,
                                        0xe001, 0xf04f, 0x30ff, 0xb001, 0xe8bd, 0x5080, 0xf3af, 0x802d, 0x4770, 0xbf00};
    static const uint16_t tail[]     = {0xf3af, 0x800f, 0x3003, 0xf7ff, 0xbffe, 0xbf00, 0xbf00, 0xbf00};
    static const uint16_t call_ptr[] = {0xf3af, 0x800d, 0xe92d, 0x5070, 0xb081, 0x4605, 0x4608, 0x460c, 0x47a8, 0x4606,
                                        0x1c60, 0x47a8, 0x4430, 0xb001, 0xe8bd, 0x5070, 0xf3af, 0x802d, 0x4770};
    CHECK(m_lint(leaf_add, 4) == 0 && m_lint(nonleaf, 16) == 0 && m_lint(early, 20) == 0);
    CHECK(m_lint(tail, 8) == 0 && m_lint(call_ptr, 19) == 0);
    uint16_t m[20];
    memcpy(m, nonleaf, sizeof nonleaf); m[11] = 0xbf00; m[12] = 0xbf00;      /* aut → nop nop */
    CHECK(m_lint(m, 16) == MV_UNAUTH_RET);
    memcpy(m, leaf_add, sizeof leaf_add); m[0] = 0xbf00; m[1] = 0xbf00;     /* bti → nop nop */
    CHECK(m_lint(m, 4) == MV_NO_PAD);
    memcpy(m, nonleaf, sizeof nonleaf); m[1] = 0x800f;                       /* pacbti → bti: aut 가 서명 없이 */
    CHECK(m_lint(m, 16) == MV_UNSIGNED_AUT);
    CHECK(thumb_len(0xf3af) == 2 && thumb_len(0xe92d) == 2 && thumb_len(0x4770) == 1 && thumb_len(0xbf00) == 1);
}

int main(void) {
    test_ex1(); test_ex2(); test_ex3(); test_ex4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
