/* ARM-A10 — 분기 보호 린터(PAC-RET/BTI) + 장난감 PAC 모델
 *
 * Part A  decode()      : AArch64 명령 워드 → 종류(PACIASP/AUTIASP/BTI/RET/RETAA/BR/BLR/B/B.cond/LR 저장·복원…)
 *         btype_of()    : 간접 분기가 남기는 PSTATE.BTYPE (00/01/10/11)
 *         bti_accepts() : 착지 패드가 그 BTYPE 을 받아 주나 (VM 실측 표와 비교)
 * Part B  lint_function(): 함수 안 CFG 를 따라 "LR 서명 상태" 데이터 흐름 → 위반 비트마스크
 * Part C  pac_*()       : 장난감 PAC 모델 (QARMA 아님!) — 필드 마스크, 서명/인증, 위조·재사용 시나리오
 *         lint_image()  : 펌웨어 이미지 CI 게이트 (함수 여러 개 → 보호 안 된 함수 수)
 *
 * 테스트 데이터: (1) shared_ref.S 를 진짜 어셈블러가 만든 워드, (2) gen.sh / run_vm.sh gen 이 실제로 컴파일한
 * demo_gen.c 함수 45개의 워드(make_golden.py 로 추출), (3) VM 에서 실측한 BTI 표와 PAC 필드 비트.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c shared_ref.S -o /tmp/ARM-A10_solution && /tmp/ARM-A10_solution
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

static int g_pass, g_total, g_part_fail;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; else { g_part_fail++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); } } while (0)

/* ======================================================================== Part A */
typedef enum {
    K_OTHER = 0,
    K_PACIASP, K_PACIBSP, K_AUTIASP, K_AUTIBSP,     /* LR 서명/인증 (SP 가 modifier) */
    K_BTI, K_BTI_C, K_BTI_J, K_BTI_JC,              /* 착지 패드 */
    K_RET, K_RETAA, K_RETAB,                        /* RET Xn / 인증 후 반환 */
    K_BR, K_BLR,                                    /* 간접 분기 (인증 변형 BRAA/BLRAA… 포함, auth=1) */
    K_BL, K_B, K_BCOND,                             /* 직접 분기 (BCOND = B.cond/CBZ/CBNZ/TBZ/TBNZ) */
    K_SPILL_LR, K_RELOAD_LR,                        /* x30 을 스택에 저장 / 스택에서 복원 */
    K_NKIND
} kind_t;

typedef struct {
    kind_t k;
    int reg;        /* BR/BLR/RET 의 Rn */
    int auth;       /* BR/BLR 의 인증 변형(BRAA, BLRAAZ…)이면 1 */
    int64_t off;    /* 직접 분기의 바이트 오프셋 (PC 기준) */
} insn_t;

static int64_t sext(uint64_t v, int bits) { return (int64_t)(v << (64 - bits)) >> (64 - bits); }

insn_t decode(uint32_t w) {
    insn_t d = {K_OTHER, -1, 0, 0};
    /* TODO(A1): HINT 공간 → PAC/BTI, 분기 레지스터 그룹 → RET/RETAA/RETAB/BR/BLR(+auth),
     *           B/BL/B.cond/CBZ/TBZ 오프셋, STP/STR/LDP/LDR 의 x30 저장·복원 */
    if ((w & 0xFFFFF01Fu) == 0xD503201Fu) {                  /* HINT #imm7 */
        switch ((w >> 5) & 0x7F) {
        case 25: d.k = K_PACIASP; break;
        case 27: d.k = K_PACIBSP; break;
        case 29: d.k = K_AUTIASP; break;
        case 31: d.k = K_AUTIBSP; break;
        case 32: d.k = K_BTI;     break;
        case 34: d.k = K_BTI_C;   break;
        case 36: d.k = K_BTI_J;   break;
        case 38: d.k = K_BTI_JC;  break;
        default: break;                                      /* NOP, PACIAZ, XPACLRI, PACM … */
        }
        return d;
    }
    if ((w >> 25) == 0x6B && ((w >> 23) & 1) == 0 && ((w >> 16) & 0x1F) == 0x1F && ((w >> 12) & 0xF) == 0) {
        unsigned op = (w >> 21) & 3, z = (w >> 24) & 1, a = (w >> 11) & 1, m = (w >> 10) & 1;
        unsigned rn = (w >> 5) & 31, rm = w & 31;
        if (!a && (z || m || rm)) return d;                  /* 할당 안 된 조합 */
        if (a && !z && rm != 31) return d;
        if (op == 2) {                                       /* RET 계열 */
            if (!a) { d.k = K_RET; d.reg = (int)rn; }
            else if (!z && rn == 31) d.k = m ? K_RETAB : K_RETAA;
            return d;
        }
        if (op == 0 || op == 1) { d.k = op ? K_BLR : K_BR; d.reg = (int)rn; d.auth = (int)a; }
        return d;
    }
    if ((w & 0x7C000000u) == 0x14000000u) {                  /* B / BL imm26 */
        d.k = (w >> 31) ? K_BL : K_B;
        d.off = sext(w & 0x3FFFFFF, 26) * 4;
        return d;
    }
    if ((w & 0xFF000010u) == 0x54000000u || (w & 0x7E000000u) == 0x34000000u) {   /* B.cond / CBZ / CBNZ */
        d.k = K_BCOND;
        d.off = sext((w >> 5) & 0x7FFFF, 19) * 4;
        return d;
    }
    if ((w & 0x7E000000u) == 0x36000000u) {                  /* TBZ / TBNZ imm14 */
        d.k = K_BCOND;
        d.off = sext((w >> 5) & 0x3FFF, 14) * 4;
        return d;
    }
    if ((w & 0xFE000000u) == 0xA8000000u && ((w >> 23) & 3) != 0) {   /* STP/LDP 64-bit (post/offset/pre) */
        int load = (w >> 22) & 1;
        if ((w & 31) == 30 || ((w >> 10) & 31) == 30) d.k = load ? K_RELOAD_LR : K_SPILL_LR;
        return d;
    }
    if ((w & 0xFF800000u) == 0xF9000000u || (w & 0xFFA00400u) == 0xF8000400u) {   /* STR/LDR X 64-bit */
        int load = (w >> 22) & 1;
        if ((w & 31) == 30) d.k = load ? K_RELOAD_LR : K_SPILL_LR;
        return d;
    }
    return d;
}

/* PSTATE.BTYPE 이 간접 분기 뒤에 갖는 값. guarded = 분기 명령이 놓인 페이지가 BTI 보호(GP=1) 페이지인가 */
int btype_of(uint32_t w, int guarded) {
    /* TODO(A2): BLR 계열 → 2, BR x16/x17 → 1, 그 밖의 BR → 가드 페이지면 3 아니면 1, 나머지 0 */
    insn_t d = decode(w);
    if (d.k == K_BLR) return 2;
    if (d.k == K_BR) return (d.reg == 16 || d.reg == 17 || !guarded) ? 1 : 3;
    return 0;
}

/* 착지 패드 kind 가 btype 을 받아 주나. bt_strict = SCTLR_EL1.BT0 (Linux 는 1 로 켠다) */
int bti_accepts(kind_t pad, int btype, int bt_strict) {
    /* TODO(A3): BTYPE 00 은 항상 OK. BTI c = 01·10, BTI j = 01·11, BTI jc = 전부, BTI = 없음,
     *           PACIASP/PACIBSP = 01·10 (+ bt_strict == 0 이면 11 도) */
    if (btype == 0) return 1;
    switch (pad) {
    case K_BTI_C:  return btype == 1 || btype == 2;
    case K_BTI_J:  return btype == 1 || btype == 3;
    case K_BTI_JC: return 1;
    case K_PACIASP: case K_PACIBSP: return btype == 1 || btype == 2 || (btype == 3 && !bt_strict);
    default:       return 0;
    }
}

/* ======================================================================== Part B */
enum { POL_PAC = 1, POL_BTI = 2 };
enum {
    V_NO_PAD         = 1 << 0,   /* POL_BTI: 첫 명령이 BTI c/jc/PACIxSP 가 아니다 */
    V_SPILL_UNSIGNED = 1 << 1,   /* POL_PAC: 서명 안 된 LR 을 스택에 저장 */
    V_RET_UNAUTH     = 1 << 2,   /* 서명된 LR 로 인증 없이 RET */
    V_AUTH_UNSIGNED  = 1 << 3,   /* 서명 안 된 LR 을 AUT/RETAx — 실행하면 항상 실패 */
    V_KEY_MISMATCH   = 1 << 4,   /* A 키로 서명, B 키로 인증 (또는 반대) */
    V_DOUBLE_SIGN    = 1 << 5,   /* 이미 서명된 LR 을 또 서명 */
    V_TAIL_UNAUTH    = 1 << 6,   /* 서명된 채로 꼬리 호출(B 밖으로 / BR) */
    V_JOIN_CONFLICT  = 1 << 7,   /* 두 경로가 다른 서명 상태로 같은 명령에 도착 */
};
enum { ST_UNSEEN = 0, ST_PLAIN, ST_SIGNED_A, ST_SIGNED_B };
#define MAXW 64

typedef struct {
    unsigned viol;       /* V_* 비트 OR */
    int first_bad;       /* 첫 위반 명령 인덱스 (-1 = 없음) */
    int signs, auths;    /* 도달 가능한 서명/인증 명령 수 (RETAx 는 auth 로 센다) */
    int leaf;            /* BL/BLR 도 LR 저장도 없음 */
} lint_report_t;

static void flag(lint_report_t *r, unsigned v, int i) {
    if (!(r->viol & v) && r->first_bad < 0) r->first_bad = i;
    r->viol |= v;
}

int lint_function(const uint32_t *w, int n, unsigned policy, lint_report_t *r) {
    memset(r, 0, sizeof *r);
    r->first_bad = -1;
    if (n <= 0 || n > MAXW) return -1;
    /* TODO(B1): 착지 패드 검사, leaf 판정, 그리고 0 번 명령(ST_PLAIN)부터 워크리스트로 CFG 를 따라
     *           LR 상태(PLAIN / SIGNED_A / SIGNED_B)를 전파하며 위반을 모은다. 위반 비트 수(!=0)를 돌려준다. */
    insn_t d0 = decode(w[0]);
    if ((policy & POL_BTI) && !(d0.k == K_BTI_C || d0.k == K_BTI_JC || d0.k == K_PACIASP || d0.k == K_PACIBSP))
        flag(r, V_NO_PAD, 0);
    r->leaf = 1;
    for (int i = 0; i < n; i++) {
        kind_t k = decode(w[i]).k;
        if (k == K_BL || k == K_BLR || k == K_SPILL_LR) r->leaf = 0;
    }
    unsigned char st[MAXW] = {0};
    int work[MAXW], top = 0;
    st[0] = ST_PLAIN;
    work[top++] = 0;
    while (top > 0) {
        int i = work[--top];
        int s = st[i];
        insn_t d = decode(w[i]);
        int succ[2], ns = 0, fall = 1;
        switch (d.k) {
        case K_PACIASP: case K_PACIBSP:
            r->signs++;
            if (s != ST_PLAIN) flag(r, V_DOUBLE_SIGN, i);
            s = d.k == K_PACIASP ? ST_SIGNED_A : ST_SIGNED_B;
            break;
        case K_AUTIASP: case K_AUTIBSP: case K_RETAA: case K_RETAB: {
            int want = (d.k == K_AUTIASP || d.k == K_RETAA) ? ST_SIGNED_A : ST_SIGNED_B;
            r->auths++;
            if (s == ST_PLAIN) flag(r, V_AUTH_UNSIGNED, i);
            else if (s != want) flag(r, V_KEY_MISMATCH, i);
            s = ST_PLAIN;
            if (d.k == K_RETAA || d.k == K_RETAB) fall = 0;
            break;
        }
        case K_SPILL_LR:
            if ((policy & POL_PAC) && s == ST_PLAIN) flag(r, V_SPILL_UNSIGNED, i);
            break;
        case K_RET:
            if (s != ST_PLAIN && d.reg == 30) flag(r, V_RET_UNAUTH, i);
            fall = 0;
            break;
        case K_BR:
            if (s != ST_PLAIN) flag(r, V_TAIL_UNAUTH, i);
            fall = 0;
            break;
        case K_B: case K_BCOND: {
            int64_t t = i + d.off / 4;
            if (t >= 0 && t < n) succ[ns++] = (int)t;
            else if (s != ST_PLAIN) flag(r, V_TAIL_UNAUTH, i);   /* 함수 밖 = 꼬리 호출 */
            if (d.k == K_B) fall = 0;
            break;
        }
        default: break;
        }
        if (fall && i + 1 < n) succ[ns++] = i + 1;
        for (int j = 0; j < ns; j++) {
            int t = succ[j];
            if (st[t] == ST_UNSEEN) { st[t] = (unsigned char)s; work[top++] = t; }
            else if (st[t] != s) flag(r, V_JOIN_CONFLICT, t);
        }
    }
    return r->viol != 0;
}

/* ======================================================================== Part C */
/* 장난감 PAC 모델 — 실제 알고리즘(QARMA5/3 또는 Apple 고유 알고리즘)이 아니다.
 * "키를 모르면 맞는 PAC 을 만들 수 없다"와 "modifier 가 다르면 PAC 이 달라진다" 두 성질만 흉내 낸다. */
typedef struct { uint64_t lo, hi; } pac_key_t;
typedef struct { int va_bits; int tbi; } pac_cfg_t;     /* tbi = 그 포인터 종류에 Top Byte Ignore 가 켜져 있나 */

static uint64_t mix64(uint64_t z) {                     /* splitmix64 마무리 함수 */
    z ^= z >> 30; z *= 0xBF58476D1CE4E5B9ull;
    z ^= z >> 27; z *= 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
static uint64_t toy_pac_hash(uint64_t ptr, uint64_t modifier, pac_key_t k) {
    return mix64(mix64(ptr ^ k.lo) ^ (modifier + k.hi));
}

/* PAC 이 들어가는 비트: [54:va_bits], TBI 가 꺼져 있으면 [63:56] 도. 비트 55 는 상/하위 주소 범위 선택이라 제외 */
uint64_t pac_field_mask(pac_cfg_t c) {
    /* TODO(C1) */
    uint64_t m = ((1ull << 55) - 1) & ~((1ull << c.va_bits) - 1);
    if (!c.tbi) m |= 0xFF00000000000000ull;
    return m;
}

uint64_t pac_strip(uint64_t p, pac_cfg_t c) { return p & ~pac_field_mask(c); }   /* XPACI 흉내 (사용자 주소, bit55=0) */

uint64_t pac_sign(uint64_t ptr, uint64_t modifier, pac_key_t k, pac_cfg_t c) {
    /* TODO(C2): 필드 비트를 지운 포인터에 해시의 필드 비트를 끼운다 */
    uint64_t m = pac_field_mask(c), base = ptr & ~m;
    return base | (toy_pac_hash(base, modifier, k) & m);
}

/* 성공하면 1 과 *out = 서명 뗀 포인터. 실패하면 0 과 *out = 비정규(non-canonical) 독 포인터
 * (FEAT_PAuth 원형은 오류 코드 비트를 넣고, FEAT_FPAC 이면 그 자리에서 예외 — 모델은 비트 하나만 뒤집는다) */
int pac_auth(uint64_t signed_ptr, uint64_t modifier, pac_key_t k, pac_cfg_t c, uint64_t *out) {
    /* TODO(C3) */
    uint64_t base = pac_strip(signed_ptr, c);
    if (pac_sign(base, modifier, k, c) == signed_ptr) { *out = base; return 1; }
    *out = base | (1ull << (c.tbi ? 53 : 61));
    return 0;
}

/* 함수 에필로그 시뮬레이션: 저장 슬롯 값 slot 을 auth(SP) 하고 ret.
 * 결과: 정상 복귀 / 다른 곳으로 튐 / PAC 이 잡음 */
typedef enum { OUT_NORMAL, OUT_REDIRECT, OUT_CAUGHT } outcome_t;
outcome_t frame_return(uint64_t slot, uint64_t sp, int pac_on, uint64_t legit_ret, pac_key_t k, pac_cfg_t c) {
    /* TODO(C4) */
    uint64_t target = slot;
    if (pac_on && !pac_auth(slot, sp, k, c, &target)) return OUT_CAUGHT;
    return target == legit_ret ? OUT_NORMAL : OUT_REDIRECT;
}

typedef struct { const char *var, *fn; int n; uint32_t w[24]; } golden_t;
typedef struct { int funcs, bad, signed_funcs; unsigned viol_or; } gate_t;

/* CI 게이트: 이미지의 모든 함수를 린트. 위반 함수가 없으면 0, 있으면 1 */
int lint_image(const golden_t *const *fs, int nf, unsigned policy, gate_t *g) {
    /* TODO(C5) */
    memset(g, 0, sizeof *g);
    for (int i = 0; i < nf; i++) {
        lint_report_t r;
        lint_function(fs[i]->w, fs[i]->n, policy, &r);
        g->funcs++;
        if (r.viol) { g->bad++; g->viol_or |= r.viol; }
        if (r.signs) g->signed_funcs++;
    }
    return g->bad != 0;
}

/* ======================================================================== 테스트 데이터 */
extern const uint32_t ref_words[], ref_words_end[];   /* shared_ref.S */

/* make_golden.py 출력 그대로 (gen_mac.txt = Mac Apple clang 21, gen_vm.txt = VM gcc 15.2 / clang 21.1, 링크된 실행 파일) */
static const golden_t golden[] = {
    {"mac_none", "leaf_add", 2, {0x0b000020, 0xd65f03c0}},
    {"mac_none", "nonleaf", 6, {0xa9bf7bfd, 0x910003fd, 0x9400001f, 0x11000400, 0xa8c17bfd, 0xd65f03c0}},
    {"mac_none", "early_exit", 9, {0x37f800e0, 0xa9bf7bfd, 0x910003fd, 0x94000018, 0x531f7800, 0xa8c17bfd, 0xd65f03c0, 0x12800000, 0xd65f03c0}},
    {"mac_none", "tail_call", 2, {0x11000c00, 0x14000011}},
    {"mac_none", "call_ptr", 16, {0xa9bd57f6, 0xa9014ff4, 0xa9027bfd, 0x910083fd, 0xaa0103f3, 0xaa0003f4, 0xaa0103e0, 0xd63f0280, 0xaa0003f5, 0x11000660, 0xd63f0280, 0x0b150000, 0xa9427bfd, 0xa9414ff4, 0xa8c357f6, 0xd65f03c0}},
    {"mac_standard", "leaf_add", 3, {0xd503245f, 0x0b000020, 0xd65f03c0}},
    {"mac_standard", "nonleaf", 7, {0xd503233f, 0xa9bf7bfd, 0x910003fd, 0x94000023, 0x11000400, 0xa8c17bfd, 0xd65f0bff}},
    {"mac_standard", "early_exit", 11, {0xd503245f, 0x37f80100, 0xd503233f, 0xa9bf7bfd, 0x910003fd, 0x9400001a, 0x531f7800, 0xa8c17bfd, 0xd65f0bff, 0x12800000, 0xd65f03c0}},
    {"mac_standard", "tail_call", 3, {0xd503245f, 0x11000c00, 0x14000012}},
    {"mac_standard", "call_ptr", 17, {0xd503233f, 0xa9bd57f6, 0xa9014ff4, 0xa9027bfd, 0x910083fd, 0xaa0103f3, 0xaa0003f4, 0xaa0103e0, 0xd63f0280, 0xaa0003f5, 0x11000660, 0xd63f0280, 0x0b150000, 0xa9427bfd, 0xa9414ff4, 0xa8c357f6, 0xd65f0bff}},
    {"mac_pacret_leaf", "leaf_add", 3, {0xd503233f, 0x0b000020, 0xd65f0bff}},
    {"mac_pacret_leaf", "nonleaf", 7, {0xd503233f, 0xa9bf7bfd, 0x910003fd, 0x94000023, 0x11000400, 0xa8c17bfd, 0xd65f0bff}},
    {"mac_pacret_leaf", "early_exit", 10, {0xd503233f, 0x37f800e0, 0xa9bf7bfd, 0x910003fd, 0x9400001b, 0x531f7800, 0xa8c17bfd, 0xd65f0bff, 0x12800000, 0xd65f0bff}},
    {"mac_pacret_leaf", "tail_call", 4, {0xd503233f, 0x11000c00, 0xd50323bf, 0x14000012}},
    {"mac_pacret_leaf", "call_ptr", 17, {0xd503233f, 0xa9bd57f6, 0xa9014ff4, 0xa9027bfd, 0x910083fd, 0xaa0103f3, 0xaa0003f4, 0xaa0103e0, 0xd63f0280, 0xaa0003f5, 0x11000660, 0xd63f0280, 0x0b150000, 0xa9427bfd, 0xa9414ff4, 0xa8c357f6, 0xd65f0bff}},
    {"mac_pacret_bkey", "leaf_add", 2, {0x0b000020, 0xd65f03c0}},
    {"mac_pacret_bkey", "nonleaf", 7, {0xd503237f, 0xa9bf7bfd, 0x910003fd, 0x94000021, 0x11000400, 0xa8c17bfd, 0xd65f0fff}},
    {"mac_pacret_bkey", "early_exit", 10, {0x37f80100, 0xd503237f, 0xa9bf7bfd, 0x910003fd, 0x94000019, 0x531f7800, 0xa8c17bfd, 0xd65f0fff, 0x12800000, 0xd65f03c0}},
    {"mac_pacret_bkey", "tail_call", 2, {0x11000c00, 0x14000012}},
    {"mac_pacret_bkey", "call_ptr", 17, {0xd503237f, 0xa9bd57f6, 0xa9014ff4, 0xa9027bfd, 0x910083fd, 0xaa0103f3, 0xaa0003f4, 0xaa0103e0, 0xd63f0280, 0xaa0003f5, 0x11000660, 0xd63f0280, 0x0b150000, 0xa9427bfd, 0xa9414ff4, 0xa8c357f6, 0xd65f0fff}},
    {"mac_pacret_pc", "leaf_add", 2, {0x0b000020, 0xd65f03c0}},
    {"mac_pacret_pc", "nonleaf", 11, {0xd50324ff, 0xd503233f, 0xa9bf7bfd, 0x910003fd, 0x9400002c, 0x11000400, 0xa8c17bfd, 0x90000010, 0x910e1210, 0xd50324ff, 0xd65f0bff}},
    {"mac_pacret_pc", "early_exit", 14, {0x37f80180, 0xd50324ff, 0xd503233f, 0xa9bf7bfd, 0x910003fd, 0x94000020, 0x531f7800, 0xa8c17bfd, 0x90000010, 0x910ed210, 0xd50324ff, 0xd65f0bff, 0x12800000, 0xd65f03c0}},
    {"mac_pacret_pc", "tail_call", 2, {0x11000c00, 0x14000016}},
    {"mac_pacret_pc", "call_ptr", 21, {0xd50324ff, 0xd503233f, 0xa9bd57f6, 0xa9014ff4, 0xa9027bfd, 0x910083fd, 0xaa0103f3, 0xaa0003f4, 0xaa0103e0, 0xd63f0280, 0xaa0003f5, 0x11000660, 0xd63f0280, 0x0b150000, 0xa9427bfd, 0xa9414ff4, 0xa8c357f6, 0x90000010, 0x910fc210, 0xd50324ff, 0xd65f0bff}},
    {"mac_arm64e", "leaf_add", 2, {0x0b000020, 0xd65f03c0}},
    {"mac_arm64e", "nonleaf", 7, {0xd503237f, 0xa9bf7bfd, 0x910003fd, 0x94000022, 0x11000400, 0xa8c17bfd, 0xd65f0fff}},
    {"mac_arm64e", "early_exit", 11, {0x37f80120, 0xd503237f, 0xa9bf7bfd, 0x910003fd, 0x9400001a, 0x531f7800, 0xa8c17bfd, 0xd50323ff, 0xd65f03c0, 0x12800000, 0xd65f03c0}},
    {"mac_arm64e", "tail_call", 2, {0x11000c00, 0x14000012}},
    {"mac_arm64e", "call_ptr", 17, {0xd503237f, 0xa9bd57f6, 0xa9014ff4, 0xa9027bfd, 0x910083fd, 0xaa0103f3, 0xaa0003f4, 0xaa0103e0, 0xd63f0a9f, 0xaa0003f5, 0x11000660, 0xd63f0a9f, 0x0b150000, 0xa9427bfd, 0xa9414ff4, 0xa8c357f6, 0xd65f0fff}},
    {"vm_gcc_none", "leaf_add", 2, {0x0b010000, 0xd65f03c0}},
    {"vm_gcc_none", "nonleaf", 6, {0xa9bf7bfd, 0x910003fd, 0x94000024, 0xa8c17bfd, 0x11000400, 0xd65f03c0}},
    {"vm_gcc_none", "early_exit", 9, {0x37f800e0, 0xa9bf7bfd, 0x910003fd, 0x9400001d, 0xa8c17bfd, 0x531f7800, 0xd65f03c0, 0x12800000, 0xd65f03c0}},
    {"vm_gcc_none", "tail_call", 2, {0x11000c00, 0x14000016}},
    {"vm_gcc_none", "call_ptr", 21, {0xa9bd7bfd, 0x910003fd, 0xa90153f3, 0x2a0103f4, 0xaa0003f3, 0xf90013f5, 0x2a0103e0, 0xd63f0260, 0x2a0003f5, 0x11000680, 0xd63f0260, 0xa94153f3, 0x0b0002a0, 0xf94013f5, 0xa8c37bfd, 0xd65f03c0, 0xd503201f, 0xd503201f, 0xd503201f, 0xd503201f, 0xd503201f}},
    {"vm_gcc_standard", "leaf_add", 3, {0xd503245f, 0x0b010000, 0xd65f03c0}},
    {"vm_gcc_standard", "nonleaf", 8, {0xd503233f, 0xa9bf7bfd, 0x910003fd, 0x9400002a, 0xa8c17bfd, 0xd50323bf, 0x11000400, 0xd65f03c0}},
    {"vm_gcc_standard", "early_exit", 13, {0xd503245f, 0x37f80120, 0xd503233f, 0xa9bf7bfd, 0x910003fd, 0x94000020, 0xa8c17bfd, 0xd50323bf, 0x531f7800, 0xd65f03c0, 0x12800000, 0xd65f03c0, 0xd503201f}},
    {"vm_gcc_standard", "tail_call", 3, {0xd503245f, 0x11000c00, 0x14000016}},
    {"vm_gcc_standard", "call_ptr", 21, {0xd503233f, 0xa9bd7bfd, 0x910003fd, 0xa90153f3, 0x2a0103f4, 0xaa0003f3, 0xf90013f5, 0x2a0103e0, 0xd63f0260, 0x2a0003f5, 0x11000680, 0xd63f0260, 0xa94153f3, 0x0b0002a0, 0xf94013f5, 0xa8c37bfd, 0xd50323bf, 0xd65f03c0, 0xd503201f, 0xd503201f, 0xd503201f}},
    {"vm_clang_standard", "leaf_add", 3, {0xd503245f, 0x0b000020, 0xd65f03c0}},
    {"vm_clang_standard", "nonleaf", 8, {0xd503233f, 0xa9bf7bfd, 0x910003fd, 0x94000026, 0x11000400, 0xa8c17bfd, 0xd50323bf, 0xd65f03c0}},
    {"vm_clang_standard", "early_exit", 12, {0xd503245f, 0x37f80120, 0xd503233f, 0xa9bf7bfd, 0x910003fd, 0x9400001c, 0x531f7800, 0xa8c17bfd, 0xd50323bf, 0xd65f03c0, 0x12800000, 0xd65f03c0}},
    {"vm_clang_standard", "tail_call", 3, {0xd503245f, 0x11000c00, 0x14000013}},
    {"vm_clang_standard", "call_ptr", 18, {0xd503233f, 0xa9bd7bfd, 0xf9000bf5, 0xa9024ff4, 0x910003fd, 0xaa0003f4, 0x2a0103e0, 0x2a0103f3, 0xd63f0280, 0x2a0003f5, 0x11000660, 0xd63f0280, 0x0b150000, 0xa9424ff4, 0xf9400bf5, 0xa8c37bfd, 0xd50323bf, 0xd65f03c0}},
};
#define NG ((int)(sizeof golden / sizeof golden[0]))

/* demo_bti_pads.S 의 함수들 (손으로 쓴 asm — 컴파일러가 PAC/BTI 를 넣어 주지 않는다) */
static const golden_t asm_pads[] = {
    {"asm", "pad_none", 2, {0x52800140, 0xd65f03c0}},
    {"asm", "pad_bti", 3, {0xd503241f, 0x52800160, 0xd65f03c0}},
    {"asm", "pad_c", 3, {0xd503245f, 0x52800180, 0xd65f03c0}},
    {"asm", "pad_j", 3, {0xd503249f, 0x528001a0, 0xd65f03c0}},
    {"asm", "pad_jc", 3, {0xd50324df, 0x528001c0, 0xd65f03c0}},
    {"asm", "pad_pac", 4, {0xd503233f, 0x528001e0, 0xd50323bf, 0xd65f03c0}},
    {"asm", "via_blr", 6, {0xd503245f, 0xa9bf7bfd, 0x910003fd, 0xd63f0000, 0xa8c17bfd, 0xd65f03c0}},
};
#define NPADS ((int)(sizeof asm_pads / sizeof asm_pads[0]))

static const golden_t *find(const char *var, const char *fn) {
    for (int i = 0; i < NG; i++)
        if (!strcmp(golden[i].var, var) && !strcmp(golden[i].fn, fn)) return &golden[i];
    return NULL;
}

/* POL_PAC|POL_BTI 기대 위반 (gen_*.txt 를 눈으로 읽고 정한 값 — 5. 풀이의 표) */
static unsigned expect_full(const char *var, const char *fn) {
    int leafish = !strcmp(fn, "leaf_add") || !strcmp(fn, "tail_call");
    if (!strcmp(var, "mac_none") || !strcmp(var, "vm_gcc_none"))
        return V_NO_PAD | (leafish ? 0 : V_SPILL_UNSIGNED);
    if (!strcmp(var, "mac_pacret_bkey"))           /* BTI 없음: PACIBSP 로 시작하는 함수만 패드가 있다 */
        return (leafish || !strcmp(fn, "early_exit")) ? V_NO_PAD : 0;
    if (!strcmp(var, "mac_arm64e"))                /* arm64e 는 BTI 를 쓰지 않는다 */
        return (leafish || !strcmp(fn, "early_exit")) ? V_NO_PAD : 0;
    if (!strcmp(var, "mac_pacret_pc"))             /* 첫 명령이 PACM(hint #39) 이거나 패드 없음 */
        return V_NO_PAD;
    return 0;   /* mac_standard, mac_pacret_leaf, vm_gcc_standard, vm_clang_standard */
}

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t xorshift64(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

static void part_end(const char *name, int before_total) {
    printf("%s: %d checks, %s\n", name, g_total - before_total, g_part_fail ? "FAIL" : "ok");
    g_part_fail = 0;
}

/* ------------------------------------------------------------------------ Part A tests */
static void test_part_a(void) {
    int t0 = g_total;
    static const kind_t ek[] = {
        K_PACIASP, K_PACIBSP, K_AUTIASP, K_AUTIBSP, K_BTI, K_BTI_C, K_BTI_J, K_BTI_JC, K_OTHER /* pacm */,
        K_RETAA, K_RETAB, K_RET, K_RET, K_BR, K_BR, K_BLR, K_BR, K_BR, K_BLR, K_BLR,
        K_BL, K_B, K_BCOND, K_BCOND, K_BCOND, K_SPILL_LR, K_RELOAD_LR, K_SPILL_LR, K_RELOAD_LR, K_SPILL_LR,
        K_RELOAD_LR, K_OTHER, K_SPILL_LR, K_OTHER, K_OTHER, K_OTHER, K_OTHER, K_OTHER, K_B};
    int nref = (int)(ref_words_end - ref_words);
    CHECK(nref == (int)(sizeof ek / sizeof ek[0]));
    int bad = 0;
    for (int i = 0; i < nref && i < (int)(sizeof ek / sizeof ek[0]); i++)
        if (decode(ref_words[i]).k != ek[i] && ++bad <= 3) { printf("  ref_words[%d]=%08x kind %d != %d\n", i, ref_words[i], decode(ref_words[i]).k, ek[i]); }
    CHECK(bad == 0);
    /* 레지스터 / auth / 오프셋 */
    CHECK(decode(ref_words[12]).reg == 1);                       /* ret x1 */
    CHECK(decode(ref_words[13]).reg == 16 && !decode(ref_words[13]).auth);
    CHECK(decode(ref_words[16]).reg == 16 && decode(ref_words[16]).auth);   /* braa x16, x17 */
    CHECK(decode(ref_words[17]).reg == 9 && decode(ref_words[17]).auth);    /* braaz x9 */
    CHECK(decode(ref_words[18]).reg == 20 && decode(ref_words[18]).auth);   /* blraaz x20 */
    CHECK(decode(ref_words[20]).off == 16 && decode(ref_words[21]).off == 12);
    CHECK(decode(ref_words[22]).off == 8 && decode(ref_words[23]).off == 4);
    CHECK(decode(ref_words[24]).off == 8 && decode(ref_words[38]).off == 0);
    CHECK(decode(0x17FFFFFFu).off == -4 && decode(0x14000000u | 0x2000000u).off == -(1ll << 27));  /* B 범위 ±128 MB */
    /* HINT 공간 전수: imm7 0..127 */
    int hb = 0;
    for (unsigned imm = 0; imm < 128; imm++) {
        kind_t want = K_OTHER;
        switch (imm) { case 25: want = K_PACIASP; break; case 27: want = K_PACIBSP; break;
                       case 29: want = K_AUTIASP; break; case 31: want = K_AUTIBSP; break;
                       case 32: want = K_BTI; break; case 34: want = K_BTI_C; break;
                       case 36: want = K_BTI_J; break; case 38: want = K_BTI_JC; break; default: break; }
        if (decode(0xD503201Fu | imm << 5).k != want) hb++;
    }
    CHECK(hb == 0);
    /* 분기 레지스터 그룹 전수: BR/BLR/RET Xn (n=0..31) */
    int rb = 0;
    for (unsigned n = 0; n < 32; n++) {
        insn_t a = decode(0xD61F0000u | n << 5), b = decode(0xD63F0000u | n << 5), c = decode(0xD65F0000u | n << 5);
        if (a.k != K_BR || a.reg != (int)n || b.k != K_BLR || b.reg != (int)n || c.k != K_RET || c.reg != (int)n) rb++;
    }
    CHECK(rb == 0);
    CHECK(decode(0xD65F0BFEu).k == K_OTHER);                     /* RETAA 의 Rm 자리가 31 이 아니면 할당 안 됨 */
    /* 무작위 워드: 분류 결과가 범위 안 (크래시 없음) */
    int rr = 0;
    for (int i = 0; i < 20000; i++) { insn_t d = decode((uint32_t)xorshift64()); if (d.k < 0 || d.k >= K_NKIND) rr++; }
    CHECK(rr == 0);

    /* btype_of: 분기 명령 워드 → BTYPE */
    CHECK(btype_of(ref_words[15], 1) == 2);      /* blr x20   */
    CHECK(btype_of(ref_words[13], 1) == 1);      /* br x16    */
    CHECK(btype_of(ref_words[14], 1) == 3);      /* br x9     */
    CHECK(btype_of(ref_words[14], 0) == 1);      /* br x9, 가드 안 된 페이지에서 */
    CHECK(btype_of(ref_words[16], 1) == 1);      /* braa x16  */
    CHECK(btype_of(ref_words[17], 1) == 3);      /* braaz x9  */
    CHECK(btype_of(ref_words[18], 1) == 2);      /* blraaz    */
    CHECK(btype_of(ref_words[11], 1) == 0 && btype_of(ref_words[21], 1) == 0 && btype_of(ref_words[20], 1) == 0);
    CHECK(btype_of(0xD61F0220u, 1) == 1);        /* br x17    */

    /* bti_accepts vs VM 실측 표 (vm_pac.txt, bti_note: Linux 7.0 + 실제 BTI 하드웨어) */
    static const kind_t pads[6] = {K_OTHER, K_BTI, K_BTI_C, K_BTI_J, K_BTI_JC, K_PACIASP};
    static const char *vm_matrix[6][3] = {          /* 열: blr(10) · br x16(01) · br x9(11) */
        {"SIGILL", "SIGILL", "SIGILL"}, {"SIGILL", "SIGILL", "SIGILL"}, {"ok", "ok", "SIGILL"},
        {"SIGILL", "ok", "ok"}, {"ok", "ok", "ok"}, {"ok", "ok", "SIGILL"}};
    static const uint32_t vias[3] = {0xD63F0000u /* blr x0 */, 0xD61F0200u /* br x16 */, 0xD61F0120u /* br x9 */};
    int mb = 0;
    for (int i = 0; i < 6; i++)
        for (int j = 0; j < 3; j++) {
            int ok = bti_accepts(pads[i], btype_of(vias[j], 1), 1);
            if (ok != !strcmp(vm_matrix[i][j], "ok") && ++mb <= 3) printf("  matrix: pad %d via %d\n", i, j);
        }
    CHECK(mb == 0);
    CHECK(bti_accepts(K_PACIASP, 3, 0) == 1 && bti_accepts(K_PACIBSP, 3, 1) == 0);   /* BT0 = 0 이면 허용 */
    CHECK(bti_accepts(K_OTHER, 0, 1) == 1 && bti_accepts(K_BTI, 0, 1) == 1);         /* RET/직접 분기는 검사 없음 */
    part_end("Part A (decode, BTYPE, landing pads)", t0);
}

/* ------------------------------------------------------------------------ Part B tests */
static unsigned lint(const uint32_t *w, int n, unsigned pol) { lint_report_t r; lint_function(w, n, pol, &r); return r.viol; }

static void test_part_b(void) {
    int t0 = g_total;
    CHECK(NG == 45);
    /* (1) 실제 컴파일러 출력 45개 × 정책 2개 */
    int bad = 0;
    for (int i = 0; i < NG; i++) {
        unsigned full = expect_full(golden[i].var, golden[i].fn);
        unsigned got_full = lint(golden[i].w, golden[i].n, POL_PAC | POL_BTI);
        unsigned got_pac = lint(golden[i].w, golden[i].n, POL_PAC);
        if (got_full != full || got_pac != (full & ~(unsigned)V_NO_PAD)) {
            if (++bad <= 3)
                printf("  %s/%s: full %#x (want %#x) pac %#x\n", golden[i].var, golden[i].fn, got_full, full, got_pac);
        }
    }
    CHECK(bad == 0);
    /* (2) 보고서 필드 */
    lint_report_t r;
    lint_function(find("mac_standard", "early_exit")->w, find("mac_standard", "early_exit")->n, POL_PAC | POL_BTI, &r);
    CHECK(r.viol == 0 && r.signs == 1 && r.auths == 1 && !r.leaf && r.first_bad == -1);
    lint_function(find("mac_pacret_leaf", "tail_call")->w, 4, POL_PAC, &r);          /* paciasp; add; autiasp; b _sink */
    CHECK(r.viol == 0 && r.signs == 1 && r.auths == 1 && r.leaf);
    lint_function(find("mac_none", "call_ptr")->w, find("mac_none", "call_ptr")->n, POL_PAC | POL_BTI, &r);
    CHECK(r.viol == (V_NO_PAD | V_SPILL_UNSIGNED) && r.first_bad == 0);
    lint_function(find("mac_arm64e", "early_exit")->w, find("mac_arm64e", "early_exit")->n, POL_PAC, &r);
    CHECK(r.viol == 0 && r.signs == 1 && r.auths == 1);     /* pacibsp … autibsp; ret + 빠른 경로 ret */

    /* (3) 주입한 버그 (실제 워드를 한 개씩 바꾼 변이) */
    uint32_t m[MAXW];
    const golden_t *g;
#define MUT(var, fn) (g = find(var, fn), memcpy(m, g->w, sizeof g->w), g->n)
    int n;
    n = MUT("mac_standard", "nonleaf");   m[6] = 0xD65F03C0u;                 /* retaa → ret */
    CHECK(lint(m, n, POL_PAC | POL_BTI) == V_RET_UNAUTH);
    n = MUT("vm_gcc_standard", "nonleaf"); m[5] = 0xD503201Fu;                /* autiasp → nop */
    CHECK(lint(m, n, POL_PAC | POL_BTI) == V_RET_UNAUTH);
    n = MUT("mac_standard", "nonleaf");   m[0] = 0xD503237Fu;                 /* paciasp → pacibsp, retaa 그대로 */
    CHECK(lint(m, n, POL_PAC | POL_BTI) == V_KEY_MISMATCH);
    n = MUT("mac_arm64e", "early_exit");  m[7] = 0xD50323BFu;                 /* autibsp → autiasp */
    CHECK(lint(m, n, POL_PAC) == V_KEY_MISMATCH);
    n = MUT("vm_gcc_standard", "leaf_add"); m[0] = 0xD503201Fu;               /* bti c → nop */
    CHECK(lint(m, n, POL_PAC | POL_BTI) == V_NO_PAD && lint(m, n, POL_PAC) == 0);
    n = MUT("vm_gcc_standard", "early_exit");                                  /* tbnz 목적지를 에필로그 ldp 로 */
    { int ldp = -1; for (int i = 0; i < n; i++) if (decode(m[i]).k == K_RELOAD_LR) { ldp = i; break; }
      m[1] = (m[1] & ~(0x3FFFu << 5)) | ((uint32_t)(ldp - 1) << 5); }
    CHECK((lint(m, n, POL_PAC | POL_BTI) & V_JOIN_CONFLICT) != 0);
    n = MUT("vm_gcc_standard", "nonleaf"); m[1] = 0xD503233Fu;                /* stp → paciasp (두 번 서명) */
    CHECK((lint(m, n, POL_PAC) & V_DOUBLE_SIGN) != 0);
    n = MUT("mac_none", "nonleaf");       m[5] = 0xD65F0BFFu;                 /* 서명 없이 retaa */
    CHECK(lint(m, n, POL_PAC) == (V_SPILL_UNSIGNED | V_AUTH_UNSIGNED));
    /* 합성: 서명된 채로 꼬리 호출 / 인증 후 꼬리 호출 */
    const uint32_t tail_bad[]  = {0xD503233Fu, 0xA9BF7BFDu, 0x910003FDu, 0x94000010u, 0xA8C17BFDu, 0x14000040u};
    const uint32_t tail_good[] = {0xD503233Fu, 0xA9BF7BFDu, 0x910003FDu, 0x94000010u, 0xA8C17BFDu, 0xD50323BFu, 0x14000040u};
    const uint32_t br_bad[]    = {0xD503233Fu, 0xA9BF7BFDu, 0xA8C17BFDu, 0xD61F0200u};   /* … br x16 */
    CHECK(lint(tail_bad, 6, POL_PAC | POL_BTI) == V_TAIL_UNAUTH);
    CHECK(lint(tail_good, 7, POL_PAC | POL_BTI) == 0);
    CHECK(lint(br_bad, 4, POL_PAC) == V_TAIL_UNAUTH);
    /* 경계: 빈 함수 / 너무 긴 함수 */
    CHECK(lint_function(m, 0, POL_PAC, &r) == -1 && lint_function(m, MAXW + 1, POL_PAC, &r) == -1);
    part_end("Part B (lint_function on 45 real functions + mutants)", t0);
}

/* ------------------------------------------------------------------------ Part C tests */
static void test_part_c(void) {
    int t0 = g_total;
    /* (1) 필드 마스크 vs 실측 (demo_probe_pac.c 의 4096-modifier 측정, probe.txt) */
    CHECK(pac_field_mask((pac_cfg_t){47, 0}) == 0xFF7F800000000000ull);   /* Mac arm64e, I 키: 16비트 */
    CHECK(pac_field_mask((pac_cfg_t){47, 1}) == 0x007F800000000000ull);   /* Mac arm64e, D 키: 8비트 */
    CHECK(pac_field_mask((pac_cfg_t){48, 1}) == 0x007F000000000000ull);   /* VM Linux, I·D 키: 7비트 */
    CHECK(__builtin_popcountll(pac_field_mask((pac_cfg_t){39, 0})) == 24);
    /* 실측 서명 값이 마스크 안에만 있나 (probe.txt 의 Mac arm64e 첫 실행 pacia/pacib/pacda) */
    const uint64_t raw = 0x0000000100004000ull;
    CHECK(((0x9E1A800100004000ull ^ raw) & ~pac_field_mask((pac_cfg_t){47, 0})) == 0);   /* pacia, arm64e */
    CHECK(((0xB262800100004000ull ^ raw) & ~pac_field_mask((pac_cfg_t){47, 0})) == 0);   /* pacib */
    CHECK(((0x007D000100004000ull ^ raw) & ~pac_field_mask((pac_cfg_t){47, 1})) == 0);   /* pacda */
    CHECK(pac_strip(0x005DC1C983EA0C54ull, (pac_cfg_t){48, 1}) == 0x0000C1C983EA0C54ull);   /* VM 서명 LR (vm_pac.txt) */

    /* (2) 서명/인증 성질 (고정 시드 무작위 4000 개) */
    pac_key_t k = {0x0123456789ABCDEFull, 0x0F1E2D3C4B5A6978ull}, k2 = {k.lo ^ 1, k.hi};
    pac_cfg_t vm = {48, 1}, mac = {47, 0};
    int rt = 0, wrongmod = 0, wrongkey = 0, poison_canon = 0, notmask = 0;
    for (int i = 0; i < 4000; i++) {
        pac_cfg_t c = (i & 1) ? vm : mac;
        uint64_t p = xorshift64() & ((1ull << c.va_bits) - 1) & ~3ull, mod = xorshift64() & ~15ull, out;
        uint64_t s = pac_sign(p, mod, k, c);
        if (((s ^ p) & ~pac_field_mask(c)) != 0) notmask++;
        if (!pac_auth(s, mod, k, c, &out) || out != p) rt++;
        if (pac_auth(s, mod + 16, k, c, &out)) wrongmod++;          /* 다른 SP — 우연히 맞을 확률 2^-w */
        else if ((out & pac_field_mask(c)) == 0) poison_canon++;     /* 실패 결과는 비정규여야 */
        if (pac_auth(s, mod, k2, c, &out)) wrongkey++;
    }
    CHECK(rt == 0 && notmask == 0 && poison_canon == 0);
    printf("  toy model: accidental pass with wrong SP %d/4000, wrong key %d/4000 (7비트 VM 필드가 대부분)\n", wrongmod, wrongkey);
    CHECK(wrongmod > 0 && wrongmod < 80 && wrongkey < 80);         /* 2000×(1/128) + 2000×(1/65536) ≈ 16 */

    /* (3) 실제 데모 결과를 모델로 재현 (vm_pac.txt / mac_forge.txt 와 같은 표) */
    const uint64_t R = 0x0000AAAAAAAA0C0Cull, LAND = 0x0000AAAAAAAA0E00ull, R_B = 0x0000AAAAAAAA0C6Cull;
    const uint64_t SP1 = 0x0000FFFFF0001230ull, SP2 = SP1 - 0x40;
    for (int pac_on = 0; pac_on <= 1; pac_on++) {
        uint64_t sigR = pac_on ? pac_sign(R, SP1, k, vm) : R;          /* record_lr 이 저장한 (서명된) LR */
        outcome_t forge   = frame_return(LAND, SP1, pac_on, R_B, k, vm);          /* 생 주소로 덮음 */
        outcome_t replay  = frame_return(sigR, SP1, pac_on, R_B, k, vm);   /* 같은 SP 의 victim_replay */
        outcome_t replay2 = frame_return(sigR, SP2, pac_on, R_B, k, vm);   /* 한 프레임 깊은 victim_replay */
        if (!pac_on) CHECK(forge == OUT_REDIRECT && replay == OUT_REDIRECT && replay2 == OUT_REDIRECT);
        else         CHECK(forge == OUT_CAUGHT && replay == OUT_REDIRECT && replay2 == OUT_CAUGHT);
    }
    CHECK(frame_return(pac_sign(R, SP1, k, vm), SP1, 1, R, k, vm) == OUT_NORMAL);   /* 공격 없음 */

    /* (4) CI 게이트 */
    const golden_t *img[16]; int ni = 0; gate_t gt;
    static const char *fns[5] = {"leaf_add", "nonleaf", "early_exit", "tail_call", "call_ptr"};
    for (int i = 0; i < 5; i++) img[ni++] = find("mac_standard", fns[i]);
    CHECK(lint_image(img, ni, POL_PAC | POL_BTI, &gt) == 0 && gt.funcs == 5 && gt.bad == 0 && gt.signed_funcs == 3);
    for (int i = 0; i < NPADS; i++) img[ni++] = &asm_pads[i];        /* 손으로 쓴 .S 가 섞였다 */
    CHECK(lint_image(img, ni, POL_PAC | POL_BTI, &gt) == 1 && gt.funcs == 12 && gt.bad == 4
          && gt.viol_or == (V_NO_PAD | V_SPILL_UNSIGNED));             /* pad_none, pad_bti, pad_j, via_blr */
    ni = 0;
    for (int i = 0; i < 5; i++) img[ni++] = find("vm_gcc_none", fns[i]);
    CHECK(lint_image(img, ni, POL_PAC, &gt) == 1 && gt.bad == 3 && gt.signed_funcs == 0);
    ni = 0;
    for (int i = 0; i < 5; i++) img[ni++] = find("mac_arm64e", fns[i]);
    CHECK(lint_image(img, ni, POL_PAC, &gt) == 0 && gt.signed_funcs == 3);
    CHECK(lint_image(img, ni, POL_PAC | POL_BTI, &gt) == 1 && gt.bad == 3);   /* arm64e 에 BTI 정책을 걸면 */
    part_end("Part C (toy PAC model, CI gate)", t0);
}

int main(void) {
    test_part_a();
    test_part_b();
    test_part_c();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
