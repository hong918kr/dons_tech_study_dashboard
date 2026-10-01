/* 10_memcpy_move.c — memcpy / memmove 직접 구현과 겹침 처리 (starter)
 *
 *   make run N=10
 *
 * 아래 TODO 다섯 군데를 채우면 된다. 테스트(main 위의 "테스트" 절)는 건드리지 말 것.
 * 구현 전에는 첫 assert에서 멈추는 것이 정상이다.
 *
 * 핵심 아이디어
 *   - memcpy는 "두 영역이 겹치지 않는다"를 caller가 보증하는 계약이다(restrict).
 *     그 보증 덕분에 구현은 방향을 고민하지 않고 가장 빠른 순서로 옮길 수 있다.
 *   - memmove는 그 보증이 없다. 겹침 방향을 보고 복사 방향을 뒤집는 것이 전부다.
 *     dst < src 면 앞에서부터, dst > src 면 뒤에서부터.
 *   - "겹침"은 두 구간 [a, a+n) 과 [b, b+n) 의 교집합이 비어 있는지의 문제다.
 *     주소를 uintptr_t로 바꿔 차이를 보면 오버플로 없이 판정할 수 있다.
 *   - 속도는 한 번에 몇 바이트를 옮기는지로 결정된다. 두 주소의 "정렬 위상"이
 *     같을 때만 word 단위로 옮길 수 있고, 다르면 바이트 복사로 떨어진다.
 */

#include <assert.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 1. 겹침 판정                                                        */
/* ------------------------------------------------------------------ */

/* 길이가 같은 두 구간 [a, a+n) 과 [b, b+n) 이 한 바이트라도 겹치는가?
 *
 * 왜 uintptr_t로 바꾸는가: C의 관계 연산자(<, >)는 같은 배열(또는 같은 객체)
 * 안의 포인터끼리만 정의되어 있다. 서로 다른 객체의 주소를 < 로 비교하는 것은
 * 표준상 unspecified다. 실제 플랫폼에서는 평평한 주소 공간이라 잘 동작하지만,
 * 의도를 분명히 하려고 정수로 바꿔서 비교한다.
 *
 * 왜 a + n 을 계산하지 않는가: 주소 + 길이는 원리상 오버플로할 수 있다.
 * 두 시작 주소의 "차이"가 n보다 작은지만 보면 덧셈이 필요 없다. */
/* TODO 1: 길이가 같은 두 구간이 겹치는지 판정한다.
 *   - a/b가 NULL이거나 n == 0 이면 0
 *   - 주소를 uintptr_t로 바꿔 두 시작 주소의 "차이"가 n보다 작은지 본다
 *     (a + n 처럼 덧셈을 쓰면 원리상 오버플로할 수 있다)
 * 반환: 1 겹친다, 0 안 겹친다 */
int regions_overlap(const void *a, const void *b, size_t n)
{
    (void)a;
    (void)b;
    (void)n;
    return 0;
}

/* ------------------------------------------------------------------ */
/* 2. 일부러 순진한 구현 — 겹침에서 무엇이 깨지는지 보이기 위한 것       */
/* ------------------------------------------------------------------ */

/* 항상 낮은 주소에서 높은 주소 방향으로 한 바이트씩 옮긴다.
 *
 * 이 함수는 "틀린 구현"이 아니다. 계약이 "항상 앞에서부터 복사한다"인
 * 함수로서는 정확하다. restrict를 붙이지 않았으므로 겹치는 인자로 불러도
 * undefined behaviour가 아니고, 결과가 예측 가능하게 "이상"해진다.
 * memmove 대신 이걸 쓰면 무엇이 어떻게 망가지는지 테스트에서 확인한다. */
/* TODO 2: 무조건 낮은 주소 → 높은 주소 방향으로 한 바이트씩 옮긴다.
 *   이건 "틀린 구현"이 아니다. 계약이 "항상 앞에서부터"인 함수로서 정확하다.
 *   memmove 대신 이걸 쓰면 무엇이 깨지는지 테스트에서 비교하기 위한 것이다.
 * 반환: dst */
void *my_copy_forward(void *dst, const void *src, size_t n)
{
    (void)src;
    (void)n;
    return dst;
}

/* ------------------------------------------------------------------ */
/* 3. memcpy — 겹치지 않는다는 계약을 받고 가장 단순하게 옮긴다          */
/* ------------------------------------------------------------------ */

/* restrict의 의미: 이 함수가 실행되는 동안 dst로 접근하는 메모리와 src로
 * 접근하는 메모리는 서로 다르다고 caller가 약속한다. 약속을 깨고 겹치는
 * 인자로 부르면 undefined behaviour이고, 컴파일러는 그 약속을 근거로
 * 로드/스토어 순서를 바꾸거나 벡터화할 수 있다.
 *
 * 즉 restrict는 "겹침을 검사하겠다"는 뜻이 아니라 "검사하지 않겠다"는 뜻이다. */
/* TODO 3: n바이트를 옮긴다. 겹치지 않는다고 caller가 약속했다(restrict).
 *   - 방향을 고민할 필요가 없다. 가장 단순한 루프면 된다
 *   - 정확히 n바이트만 쓴다. n == 0 이면 dst를 한 바이트도 건드리지 않는다
 * 반환: dst (표준 memcpy와 같은 계약) */
void *my_memcpy(void *restrict dst, const void *restrict src, size_t n)
{
    (void)src;
    (void)n;
    return dst;
}

/* ------------------------------------------------------------------ */
/* 4. memmove — 겹침을 허용한다. 방향을 뒤집는 것이 전부다               */
/* ------------------------------------------------------------------ */

/* 왜 방향만 뒤집으면 되는가.
 *
 *   dst < src 인 경우 (왼쪽으로 당긴다)
 *       src: [ a b c d e ]
 *       dst: 두 칸 앞      한 바이트를 쓸 때, 그 자리에 있던 "아직 안 읽은
 *                          원본"은 항상 이미 읽은 칸이다 → 앞에서부터 안전
 *   dst > src 인 경우 (오른쪽으로 민다)
 *       앞에서부터 쓰면 아직 읽지 않은 원본 바이트를 덮어 버린다
 *                          → 뒤에서부터 쓰면 안전
 *
 * 정확히 말하면 "겹칠 때만" 방향이 중요하다. 겹치지 않으면 어느 방향이든
 * 결과가 같다. 그래서 겹침 검사를 따로 하지 않고 주소 대소만 본다.
 * 검사를 하나 줄이는 것이 분기 하나 줄이는 것보다 싸다. */
/* TODO 4: 겹쳐도 되는 복사. 방향을 뒤집는 것이 전부다.
 *   - n == 0 이거나 dst == src 면 그냥 dst 반환
 *   - dst < src : 앞(index 0)에서부터 복사
 *   - dst > src : 뒤(index n-1)에서부터 복사
 *   - 겹침 여부를 따로 검사할 필요가 없다. 안 겹치면 어느 방향이든 같다
 * 반환: dst */
void *my_memmove(void *dst, const void *src, size_t n)
{
    (void)src;
    (void)n;
    return dst;
}

/* ------------------------------------------------------------------ */
/* 5. word 단위 최적화 복사                                            */
/* ------------------------------------------------------------------ */

/* 바이트 루프는 1바이트마다 로드 1 + 스토어 1이다. Cortex-M4에서 64바이트를
 * 옮기면 최소 128번의 메모리 접근이 된다. 4바이트씩 옮기면 32번이다.
 *
 * 조건이 하나 있다. word 로드/스토어는 주소가 4의 배수여야 한다(Cortex-M0는
 * 강제, M3/M4는 unaligned도 되지만 느리고, LDM/STM이나 DMA는 여전히 정렬을
 * 요구한다). 그래서 다음 세 단계로 나눈다.
 *
 *   [1] head: d가 4의 배수가 될 때까지 바이트로 맞춘다
 *   [2] body: 4바이트씩 옮긴다
 *   [3] tail: 남은 1~3바이트를 바이트로 옮긴다
 *
 * 그리고 전제가 하나 더 있다. head를 맞춰 d를 4의 배수로 만들었을 때 s도
 * 4의 배수가 되어야 한다. 그건 처음에 (d % 4) == (s % 4) 일 때만 성립한다.
 * 이걸 "정렬 위상(alignment phase)이 같다"고 부른다. 위상이 다르면
 * (예: d가 0x1000, s가 0x2001) 어떤 head 길이로도 둘을 동시에 맞출 수 없어
 * 바이트 복사로 떨어져야 한다.
 *
 * 주의 (strict aliasing): 아래에서 unsigned char 배열을 uint32_t 포인터로
 * 읽는다. 실제 libc가 쓰는 관용구이고 모든 실무 툴체인에서 동작하지만,
 * 표준의 strict aliasing 규칙만 보면 회색지대다. 표준적으로 안전한 대안은
 * word를 memcpy로 지역 변수에 담는 것이고(컴파일러가 결국 같은 명령으로
 * 접는다), 커널·펌웨어 빌드는 보통 -fno-strict-aliasing을 켠다. */
/* TODO 5: word 단위로 옮기는 최적화 버전. 결과는 my_memcpy와 완전히 같아야 한다.
 *   [0] 조건 확인: n >= 2*sizeof(uint32_t) 이고 (dst % 4) == (src % 4) 인가?
 *       위상이 다르면 어떤 head 길이로도 둘을 동시에 정렬할 수 없다 → 전부 바이트로
 *   [1] head: dst가 4의 배수가 될 때까지 바이트로 옮긴다 (최대 3바이트)
 *   [2] body: uint32_t 포인터로 4바이트씩 옮긴다
 *   [3] tail: 남은 1~3바이트를 바이트로 옮긴다
 * 반환: dst */
void *my_memcpy_words(void *restrict dst, const void *restrict src, size_t n)
{
    (void)src;
    (void)n;
    return dst;
}

/* ================================================================== */
/* 테스트                                                              */
/* ================================================================== */

#define CAP  64u                 /* 테스트 버퍼 본문 크기 */
#define PAD  8u                  /* 앞뒤 guard 바이트 수 */
#define GUARD 0x5Au              /* guard 패턴 */

/* 4바이트 정렬을 보장한 버퍼. alignas가 없으면 word 경로에 들어가는
 * 오프셋 조합이 플랫폼에 따라 달라져 테스트가 흔들린다. */
static alignas(uint32_t) unsigned char g_dst[PAD + CAP + PAD];
static alignas(uint32_t) unsigned char g_src[PAD + CAP + PAD];

static void fill_pattern(unsigned char *buf, size_t n, unsigned seed)
{
    for (size_t i = 0u; i < n; i++) {
        buf[i] = (unsigned char)(seed + i * 7u + (i >> 3));
    }
}

static void arm_guards(unsigned char *buf, size_t total)
{
    memset(buf, GUARD, PAD);
    memset(buf + total - PAD, GUARD, PAD);
}

static void check_guards(const unsigned char *buf, size_t total)
{
    for (size_t i = 0u; i < PAD; i++) {
        assert(buf[i] == GUARD);                 /* 앞으로 삐져나가지 않았다 */
        assert(buf[total - PAD + i] == GUARD);   /* 뒤로도 */
    }
}

/* ------------------------------------------------------------------ */

static void test_overlap_detect(void)
{
    unsigned char b[32];

    /* 길이 0은 절대 겹치지 않는다 — 같은 주소라도 */
    assert(regions_overlap(b, b, 0u) == 0);
    /* 같은 주소 + 길이 1 = 겹친다 */
    assert(regions_overlap(b, b, 1u) == 1);
    /* 딱 맞닿은 두 구간 — 겹치지 않는다 (경계값) */
    assert(regions_overlap(b, b + 8, 8u) == 0);
    assert(regions_overlap(b + 8, b, 8u) == 0);
    /* 1바이트만 겹치는 최소 겹침 (경계값) */
    assert(regions_overlap(b, b + 8, 9u) == 1);
    assert(regions_overlap(b + 8, b, 9u) == 1);
    /* 완전히 떨어진 두 구간 */
    assert(regions_overlap(b, b + 16, 4u) == 0);
    /* NULL 방어 */
    assert(regions_overlap(NULL, b, 4u) == 0);
    assert(regions_overlap(b, NULL, 4u) == 0);

    puts("ok  1: 겹침 판정이 맞닿음/1바이트 겹침/길이 0 경계를 구분한다");
}

static void test_memcpy_basic(void)
{
    arm_guards(g_dst, sizeof g_dst);
    fill_pattern(g_src + PAD, CAP, 0x11u);

    /* 길이 0: dst를 한 바이트도 건드리지 않아야 한다 */
    memset(g_dst + PAD, 0xCC, CAP);
    assert(my_memcpy(g_dst + PAD, g_src + PAD, 0u) == g_dst + PAD);
    assert(g_dst[PAD] == 0xCC);

    /* 길이 1 */
    assert(my_memcpy(g_dst + PAD, g_src + PAD, 1u) == g_dst + PAD);
    assert(g_dst[PAD] == g_src[PAD]);
    assert(g_dst[PAD + 1u] == 0xCC);     /* 1바이트만 썼다 */

    /* 전체 길이 + guard 확인 */
    assert(my_memcpy(g_dst + PAD, g_src + PAD, CAP) == g_dst + PAD);
    assert(memcmp(g_dst + PAD, g_src + PAD, CAP) == 0);
    check_guards(g_dst, sizeof g_dst);

    puts("ok  2: memcpy가 길이 0/1/전체에서 정확히 n바이트만 쓴다");
}

static void test_memcpy_all_offsets(void)
{
    /* 모든 (dst 오프셋, src 오프셋, 길이) 조합에서 libc memcpy와 일치해야 한다.
     * 오프셋을 0~7까지 돌리는 이유: 정렬 위상 조합(같음/다름)을 전부 밟는다. */
    static unsigned char ref[PAD + CAP + PAD];

    for (size_t doff = 0u; doff < 8u; doff++) {
        for (size_t soff = 0u; soff < 8u; soff++) {
            for (size_t len = 0u; len <= 40u; len++) {
                arm_guards(g_dst, sizeof g_dst);
                memset(g_dst + PAD, 0u, CAP);
                memcpy(ref, g_dst, sizeof ref);
                fill_pattern(g_src + PAD, CAP, (unsigned)(doff * 31u + soff));

                unsigned char *d = g_dst + PAD + doff;
                unsigned char *s = g_src + PAD + soff;

                memcpy(ref + PAD + doff, s, len);        /* libc 기준 결과 */
                my_memcpy(d, s, len);
                assert(memcmp(g_dst, ref, sizeof ref) == 0);
                check_guards(g_dst, sizeof g_dst);
            }
        }
    }
    puts("ok  3: memcpy가 오프셋 0~7 x 길이 0~40 전 조합에서 libc와 일치한다");
}

static void test_memcpy_words_matches(void)
{
    static unsigned char ref[PAD + CAP + PAD];

    for (size_t doff = 0u; doff < 8u; doff++) {
        for (size_t soff = 0u; soff < 8u; soff++) {
            for (size_t len = 0u; len <= 40u; len++) {
                arm_guards(g_dst, sizeof g_dst);
                memset(g_dst + PAD, 0u, CAP);
                memcpy(ref, g_dst, sizeof ref);
                fill_pattern(g_src + PAD, CAP, (unsigned)(len + doff * 5u));

                unsigned char *d = g_dst + PAD + doff;
                unsigned char *s = g_src + PAD + soff;

                memcpy(ref + PAD + doff, s, len);
                my_memcpy_words(d, s, len);
                /* 위상이 같든 다르든, 길이가 word보다 짧든 길든 결과는 동일 */
                assert(memcmp(g_dst, ref, sizeof ref) == 0);
                check_guards(g_dst, sizeof g_dst);
            }
        }
    }
    puts("ok  4: word 최적화 버전이 모든 정렬 위상에서 같은 결과를 낸다");
}

static void test_memmove_overlap_backward(void)
{
    /* dst < src : 왼쪽으로 당긴다. 앞에서부터 복사해도 안전한 방향 */
    unsigned char buf[16];
    unsigned char ref[16];

    for (unsigned char i = 0u; i < 16u; i++) {
        buf[i] = (unsigned char)(0x10u + i);
    }
    memcpy(ref, buf, sizeof ref);
    memmove(ref + 0, ref + 4, 8u);       /* libc 기준 */
    my_memmove(buf + 0, buf + 4, 8u);
    assert(memcmp(buf, ref, sizeof buf) == 0);
    assert(buf[0] == 0x14u && buf[7] == 0x1Bu);
    assert(buf[8] == 0x18u);             /* 손대지 않은 뒷부분은 그대로 */

    puts("ok  5: memmove가 dst < src 겹침(왼쪽 당기기)을 정확히 처리한다");
}

static void test_memmove_overlap_forward(void)
{
    /* dst > src : 오른쪽으로 민다. 앞에서부터 복사하면 깨지는 방향 */
    unsigned char good[16];
    unsigned char bad[16];
    unsigned char ref[16];

    for (unsigned char i = 0u; i < 16u; i++) {
        good[i] = bad[i] = (unsigned char)(0x10u + i);
    }
    memcpy(ref, good, sizeof ref);
    memmove(ref + 4, ref + 0, 8u);       /* libc 기준 */

    my_memmove(good + 4, good + 0, 8u);
    assert(memcmp(good, ref, sizeof good) == 0);
    assert(good[4] == 0x10u && good[11] == 0x17u);

    /* 같은 인자를 순진한 앞방향 복사로 주면 결과가 달라진다.
     * 첫 4바이트를 옮긴 뒤 원본 [4..7]이 이미 덮여 있어 [0..3]이 반복된다. */
    my_copy_forward(bad + 4, bad + 0, 8u);
    assert(memcmp(bad, ref, sizeof bad) != 0);
    assert(bad[8] == 0x10u);             /* 4칸 주기로 패턴이 되풀이된다 */
    assert(bad[9] == 0x11u);
    assert(bad[10] == 0x12u);
    assert(bad[11] == 0x13u);

    puts("ok  6: dst > src에서 앞방향 복사는 4칸 주기로 패턴이 번지고 memmove는 안 그렇다");
}

static void test_memmove_same_and_zero(void)
{
    unsigned char buf[8] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
    unsigned char ref[8];
    memcpy(ref, buf, sizeof ref);

    assert(my_memmove(buf, buf, sizeof buf) == buf);   /* dst == src */
    assert(memcmp(buf, ref, sizeof buf) == 0);

    assert(my_memmove(buf + 1, buf, 0u) == buf + 1);   /* 길이 0 */
    assert(memcmp(buf, ref, sizeof buf) == 0);

    /* 최소 겹침: 1바이트만 겹치는 경우도 방향 판단이 맞아야 한다 */
    unsigned char b2[8] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
    unsigned char r2[8] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
    memmove(r2 + 3, r2 + 0, 4u);
    my_memmove(b2 + 3, b2 + 0, 4u);
    assert(memcmp(b2, r2, sizeof b2) == 0);

    puts("ok  7: dst == src, 길이 0, 1바이트 겹침에서 memmove가 상태를 망치지 않는다");
}

static void test_memmove_exhaustive(void)
{
    /* 한 버퍼 안에서 가능한 모든 (dst, src, len) 조합을 libc와 비교한다.
     * 겹침의 모든 형태(완전 포함, 부분 겹침, 맞닿음, 역방향)가 여기 다 들어온다. */
    enum { N = 24 };
    static unsigned char mine[N];
    static unsigned char ref[N];

    for (size_t doff = 0u; doff < N; doff++) {
        for (size_t soff = 0u; soff < N; soff++) {
            size_t maxlen = (N - ((doff > soff) ? doff : soff));
            for (size_t len = 0u; len <= maxlen; len++) {
                for (size_t i = 0u; i < N; i++) {
                    mine[i] = ref[i] = (unsigned char)(0x20u + i);
                }
                memmove(ref + doff, ref + soff, len);
                my_memmove(mine + doff, mine + soff, len);
                assert(memcmp(mine, ref, N) == 0);
            }
        }
    }
    puts("ok  8: memmove가 24바이트 버퍼의 모든 겹침 조합에서 libc와 일치한다");
}

static void test_word_path_is_taken(void)
{
    /* 위상이 같고 길이가 충분하면 word 경로를 타야 한다. 결과로는 확인할 수
     * 없으니(양쪽 다 같은 답), 여기서는 "정렬된 큰 복사"가 정확한지만 본다.
     * 실제 word 경로 사용 여부는 make sol 후 디스어셈블로 확인한다(노트 §8). */
    assert(((uintptr_t)(g_dst + PAD) % 4u) == 0u);
    assert(((uintptr_t)(g_src + PAD) % 4u) == 0u);

    fill_pattern(g_src + PAD, CAP, 0x77u);
    arm_guards(g_dst, sizeof g_dst);
    memset(g_dst + PAD, 0u, CAP);

    my_memcpy_words(g_dst + PAD, g_src + PAD, CAP);
    assert(memcmp(g_dst + PAD, g_src + PAD, CAP) == 0);
    check_guards(g_dst, sizeof g_dst);

    /* 위상이 같지만 정렬은 안 맞는 경우: head 3바이트 → body → tail */
    memset(g_dst + PAD, 0u, CAP);
    my_memcpy_words(g_dst + PAD + 1u, g_src + PAD + 1u, CAP - 2u);
    assert(memcmp(g_dst + PAD + 1u, g_src + PAD + 1u, CAP - 2u) == 0);
    assert(g_dst[PAD] == 0u);            /* head 앞을 건드리지 않았다 */
    assert(g_dst[PAD + CAP - 1u] == 0u); /* tail 뒤도 */

    puts("ok  9: 정렬된 경로와 head/tail이 붙는 경로 모두 범위를 넘지 않는다");
}

static void test_return_values(void)
{
    unsigned char a[4] = {0u};
    unsigned char b[4] = {1u, 2u, 3u, 4u};

    /* 표준 memcpy/memmove는 dst를 돌려준다. 체이닝 관용구가 여기에 의존한다 */
    assert(my_memcpy(a, b, 4u) == a);
    assert(my_memmove(a, b, 4u) == a);
    assert(my_memcpy_words(a, b, 4u) == a);
    assert(my_copy_forward(a, b, 4u) == a);
    puts("ok 10: 네 함수 모두 dst를 그대로 돌려준다 (표준과 같은 계약)");
}

int main(void)
{
    printf("copy: word=%zu bytes, dst align=%zu, src align=%zu\n",
           sizeof(uint32_t),
           (size_t)((uintptr_t)g_dst % 4u),
           (size_t)((uintptr_t)g_src % 4u));

    puts("(구현 전에는 아래 첫 assert에서 멈추는 것이 정상이다)");

    test_overlap_detect();
    test_memcpy_basic();
    test_memcpy_all_offsets();
    test_memcpy_words_matches();
    test_memmove_overlap_backward();
    test_memmove_overlap_forward();
    test_memmove_same_and_zero();
    test_memmove_exhaustive();
    test_word_path_is_taken();
    test_return_values();

    puts("ALL TESTS PASSED");
    return 0;
}
