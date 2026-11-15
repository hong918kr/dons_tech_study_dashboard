/* ARM-B01 첫 AArch64 함수: C에서 호출하는 .S 파일 — 테스트 하네스 (starter.S / solution.S 공용)
 *
 * 빌드 (macOS arm64):
 *   cc -std=c11 -Wall -Wextra -Werror -O2 harness.c starter.S  shared_hostile.S -o /tmp/ARM-B01_starter  && /tmp/ARM-B01_starter
 *   cc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S shared_hostile.S -o /tmp/ARM-B01_solution && /tmp/ARM-B01_solution
 *
 * C 는 asm 을 "프로토타입"으로만 안다. 프로토타입이 곧 계약(contract)이다:
 * 어떤 인자가 어느 레지스터에, 몇 비트 폭으로 들어오고, 반환값을 어디에 두는가.
 */
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ---------------- asm 함수 (starter.S / solution.S) ---------------- */
/* Part A — 인자 x0..x2, 반환 x0 */
long add3(long a, long b, long c);              /* a + b + c (64비트 wrap) */
long max_long(long a, long b);                  /* 부호 있는 최댓값 */
long sum_array(const long *p, size_t n);        /* n == 0 이면 p 를 읽지 않는다 */

/* Part B — int 인자: w0/w1 만 의미 있음. x 의 윗 32비트는 쓰레기일 수 있다 */
int  max_int(int a, int b);
long add_wide(long a, int b);                   /* a + (long)b */

/* Part C — 패닉 핸들러용 레지스터 스냅숏. d 는 8번째 인자 → x7 */
struct regdump {
    uint64_t x[7];      /* x0..x6 (= a0..a6)          offset  0..48 */
    uint64_t fp;        /* x29: 호출자의 frame pointer  offset 56   */
    uint64_t lr;        /* x30: 복귀 주소               offset 64   */
    uint64_t sp;        /* 호출 순간의 sp               offset 72   */
};
_Static_assert(offsetof(struct regdump, fp) == 56, "asm 이 쓰는 오프셋과 맞아야 한다");
_Static_assert(offsetof(struct regdump, lr) == 64, "asm 이 쓰는 오프셋과 맞아야 한다");
_Static_assert(offsetof(struct regdump, sp) == 72, "asm 이 쓰는 오프셋과 맞아야 한다");
void regdump_capture(long a0, long a1, long a2, long a3, long a4, long a5, long a6,
                     struct regdump *d);

/* ---------------- shared_hostile.S: 윗 비트에 쓰레기를 넣는 호출자 ---------------- */
typedef void (*anyfn)(void);                    /* 범용 함수 포인터 (C 에서 이걸로 호출하진 않는다) */
int  call2_dirty_i(anyfn fn, uint64_t r0, uint64_t r1);
long call2_dirty_l(anyfn fn, uint64_t r0, uint64_t r1);

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS %s\n", #expr); } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;  /* 고정 시드 xorshift64 → 매번 같은 입력 */
static uint64_t rnd(void) {
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17;
    return g_rng;
}
/* int 값 v 를 x 레지스터에 넣되 윗 32비트를 hi 로 채운다 */
static uint64_t dirty(int v, uint32_t hi) { return ((uint64_t)hi << 32) | (uint32_t)v; }

/* lr - 4 의 명령이 BL 이면 그 목적지 주소, 아니면 0.  BL = 100101 | imm26 (워드 단위 PC 상대) */
static uintptr_t bl_target(uintptr_t pc) {
    uint32_t insn = *(const uint32_t *)pc;      /* 코드도 메모리다: __TEXT 는 읽기 가능 */
    if ((insn >> 26) != 0x25u) return 0;
    uint64_t imm = insn & 0x03FFFFFFu;
    if (imm & 0x02000000u) imm |= ~(uint64_t)0x03FFFFFFu;   /* imm26 부호 확장 (unsigned 로 → UB 없음) */
    return (uintptr_t)(pc + (imm << 2));
}

/* Part C 용: 프레임이 있는 noinline 함수 안에서 캡처하고, 무엇이 맞아야 하는지 같이 기록 */
static volatile long g_sink;
__attribute__((noinline)) static void take_dump(struct regdump *d, uintptr_t *my_fp, uintptr_t *my_local) {
    volatile long local = 42;                   /* 스택에 놓이는 지역 변수 → 주소가 sp 근처 */
    *my_fp = (uintptr_t)__builtin_frame_address(0);
    *my_local = (uintptr_t)&local;
    regdump_capture(10, -20, 30, -40, 50, -60, 70, d);
    g_sink = local;                             /* 호출 뒤에도 일이 남아 있어야 꼬리 호출(b)이 안 된다 */
}

int main(void) {
    /* ===== Part A: 인자 x0..x2 → 반환 x0 ===== */
    CHECK(add3(1, 2, 3) == 6);
    CHECK(add3(-5, 2, 1) == -2);
    CHECK(add3(LONG_MAX, 1, 0) == LONG_MIN);    /* C 로 계산하면 UB → 기댓값을 손으로 적는다 (ISA 는 wrap) */
    CHECK(max_long(3, 7) == 7 && max_long(7, 3) == 7);
    CHECK(max_long(-1, 1) == 1);                /* 부호 없는 조건(hi)을 쓰면 -1 이 이긴다 */
    CHECK(max_long(LONG_MIN, LONG_MAX) == LONG_MAX && max_long(LONG_MIN, -1) == -1);
    {
        long v[5] = {10, -3, 7, 100, -14};
        CHECK(sum_array(v, 5) == 100);
        CHECK(sum_array(v, 1) == 10);
        CHECK(sum_array(NULL, 0) == 0);         /* n == 0: 포인터를 절대 읽으면 안 된다 */
        long w[2] = {LONG_MAX, 1};
        CHECK(sum_array(w, 2) == LONG_MIN);     /* wrap, 손으로 쓴 기댓값 */
    }
    {   /* 무작위 1000회: 기준 모델은 unsigned 덧셈 (UB 없음, 2^64 모듈러) */
        int bad_add = 0, bad_max = 0, bad_sum = 0;
        long buf[64];
        for (int t = 0; t < 1000; t++) {
            long a = (long)rnd(), b = (long)rnd(), c = (long)rnd();
            if ((uint64_t)add3(a, b, c) != (uint64_t)a + (uint64_t)b + (uint64_t)c) bad_add++;
            if (max_long(a, b) != (a > b ? a : b)) bad_max++;
            size_t n = (size_t)(rnd() % 65);
            uint64_t ref = 0;
            for (size_t i = 0; i < n; i++) { buf[i] = (long)rnd(); ref += (uint64_t)buf[i]; }
            if ((uint64_t)sum_array(buf, n) != ref) bad_sum++;
        }
        CHECK(bad_add == 0);
        CHECK(bad_max == 0);
        CHECK(bad_sum == 0);
    }

    /* ===== Part B: int 인자는 w 레지스터 — 윗 32비트를 믿지 마라 ===== */
    CHECK(max_int(-5, 3) == 3);
    CHECK(max_int(INT_MIN, INT_MAX) == INT_MAX && max_int(-1, 0) == 0);
    CHECK(add_wide(100, -1) == 99);
    CHECK(add_wide(0, INT_MIN) == (long)INT_MIN);
    CHECK(add_wide(LONG_MIN, -1) == LONG_MAX);  /* wrap, 손으로 쓴 기댓값 */
    /* 적대적 호출자: 윗 32비트 = 0xDEADBEEF / 0xFFFFFFFF / 0 */
    CHECK(call2_dirty_i((anyfn)max_int, dirty(-5, 0xDEADBEEFu), dirty(3, 0x00000000u)) == 3);
    CHECK(call2_dirty_i((anyfn)max_int, dirty(7, 0xFFFFFFFFu), dirty(-2, 0x12345678u)) == 7);
    CHECK(call2_dirty_l((anyfn)add_wide, 1000, dirty(-1, 0xDEADBEEFu)) == 999);
    CHECK(call2_dirty_l((anyfn)add_wide, 0, dirty(-1, 0x00000000u)) == -1);   /* 0 확장된 -1 */
    {
        int bad_i = 0, bad_w = 0;
        for (int t = 0; t < 1000; t++) {
            int a = (int)(uint32_t)rnd(), b = (int)(uint32_t)rnd();
            uint32_t g1 = (uint32_t)rnd(), g2 = (uint32_t)rnd();
            if (call2_dirty_i((anyfn)max_int, dirty(a, g1), dirty(b, g2)) != (a > b ? a : b)) bad_i++;
            long w = (long)(rnd() >> 2) - (1L << 61);         /* |w| < 2^61 → 더해도 넘치지 않음 */
            if (call2_dirty_l((anyfn)add_wide, (uint64_t)w, dirty(b, g2)) != w + b) bad_w++;
        }
        CHECK(bad_i == 0);
        CHECK(bad_w == 0);
    }

    /* ===== Part C: 패닉 핸들러식 레지스터 스냅숏 ===== */
    {
        struct regdump d = {0};
        uintptr_t fp = 0, local = 0;
        take_dump(&d, &fp, &local);
        CHECK(d.x[0] == 10 && (long)d.x[1] == -20 && d.x[2] == 30 && (long)d.x[3] == -40);
        CHECK(d.x[4] == 50 && (long)d.x[5] == -60 && d.x[6] == 70);
        CHECK(d.fp == fp);                                     /* take_dump 의 frame record 주소 */
        CHECK(d.lr - (uintptr_t)take_dump < 1024);             /* 복귀 주소는 take_dump 몸체 안 */
        CHECK(d.lr - (uintptr_t)take_dump < 1024 && bl_target(d.lr - 4) == (uintptr_t)regdump_capture);
        CHECK(d.sp != 0 && d.sp % 16 == 0 && d.sp <= local && local - d.sp < 4096);
        printf("info: take_dump=%#lx  lr=%#llx (+%llu)  fp=%#llx  sp=%#llx  &local=%#lx\n",
               (unsigned long)(uintptr_t)take_dump, (unsigned long long)d.lr,
               (unsigned long long)(d.lr - (uintptr_t)take_dump), (unsigned long long)d.fp,
               (unsigned long long)d.sp, (unsigned long)local);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
