/* ============================================================================
 * 04_memory.c  —  Memory & Pointers (메모리 & 포인터)  Q41–55   [SOLUTION]
 * Anduril Firmware Engineer C interview prep.
 *
 * 빌드:  cc -std=c11 -Wall -Wextra solutions/04_memory.c -o /tmp/andb_memory
 * 실행:  /tmp/andb_memory
 *
 * 주제: mem* 함수 직접 구현(overlap 안전성 포함), 포인터 스왑/문자열 뒤집기,
 *       const 위치, volatile(HW 레지스터/ISR), 함수 포인터 디스패치,
 *       정적 메모리 풀 malloc/free, dangling pointer, void*, 구조체 패딩,
 *       포인터 배열 vs 배열 포인터.
 * ========================================================================== */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>   /* 검증(reference)용 표준 memmove/memcmp 비교에만 사용 */

/* ---------------------------------------------------------------------------
 * 테스트 하네스: 호출/결과/PASS·FAIL(expected) 출력
 * ------------------------------------------------------------------------- */
static int g_pass = 0, g_fail = 0;

#define EXPECT_EQ(label, got, want) do {                                       \
    long long _g = (long long)(got), _w = (long long)(want);                   \
    bool _ok = (_g == _w);                                                     \
    printf("  %-46s => %lld  [%s] (expected %lld)\n",                          \
           (label), _g, _ok ? "PASS" : "FAIL", _w);                            \
    if (_ok) g_pass++; else g_fail++;                                          \
} while (0)

#define EXPECT_TRUE(label, cond) do {                                          \
    bool _ok = (cond);                                                         \
    printf("  %-46s => %s  [%s] (expected true)\n",                            \
           (label), _ok ? "true " : "false", _ok ? "PASS" : "FAIL");           \
    if (_ok) g_pass++; else g_fail++;                                          \
} while (0)

/* 버퍼 n바이트 일치 검사 */
#define EXPECT_MEM(label, buf, ref, n) do {                                    \
    bool _ok = (memcmp((buf), (ref), (n)) == 0);                               \
    printf("  %-46s => %s  [%s] (expected match)\n",                           \
           (label), _ok ? "match" : "diff ", _ok ? "PASS" : "FAIL");           \
    if (_ok) g_pass++; else g_fail++;                                          \
} while (0)

/* ===========================================================================
 * 41. my_memset — s의 앞 n바이트를 (unsigned char)c 로 채운다. s 반환.
 * ========================================================================= */
void *my_memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;          /* 바이트 단위 접근 */
    unsigned char v = (unsigned char)c;             /* 하위 1바이트만 사용 */
    for (size_t i = 0; i < n; ++i)
        p[i] = v;
    return s;                                        /* memset 계약: dest 반환 */
}

/* ===========================================================================
 * 42. my_memcpy — src의 n바이트를 dest로 복사(겹침 미보장). dest 반환.
 *   restrict: dest/src가 겹치지 않음을 컴파일러에 약속 → 최적화 허용.
 * ========================================================================= */
void *my_memcpy(void *restrict dest, const void *restrict src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; ++i)
        d[i] = s[i];
    return dest;
}

/* ===========================================================================
 * 43. my_memmove — 겹쳐도 안전하게 n바이트 복사. dest 반환.
 *   dest < src : 앞→뒤 복사 안전.  dest > src : 뒤→앞 복사해야 원본 보존.
 * ========================================================================= */
void *my_memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0)
        return dest;
    if (d < s) {                                     /* 앞에서부터 */
        for (size_t i = 0; i < n; ++i)
            d[i] = s[i];
    } else {                                         /* 뒤에서부터(겹침 안전) */
        for (size_t i = n; i > 0; --i)
            d[i - 1] = s[i - 1];
    }
    return dest;
}

/* ===========================================================================
 * 44. my_memcmp — 첫 불일치 바이트를 unsigned char로 비교. <0 / 0 / >0.
 * ========================================================================= */
int my_memcmp(const void *s1, const void *s2, size_t n)
{
    const unsigned char *a = (const unsigned char *)s1;
    const unsigned char *b = (const unsigned char *)s2;
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i])
            return (int)a[i] - (int)b[i];            /* unsigned 차이 → 부호 확정 */
    }
    return 0;
}

/* ===========================================================================
 * 45. my_memchr — 앞 n바이트에서 (unsigned char)c 첫 위치 반환. 없으면 NULL.
 * ========================================================================= */
void *my_memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned char v = (unsigned char)c;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] == v)
            return (void *)(p + i);                  /* const 벗겨 반환(계약) */
    }
    return NULL;
}

/* ===========================================================================
 * 46. swap_int — 포인터로 두 정수 교환(임시 변수 사용, XOR 트릭 지양).
 * ========================================================================= */
void swap_int(int32_t *a, int32_t *b)
{
    if (a == b)                                      /* 같은 주소면 XOR류는 0됨 */
        return;
    int32_t tmp = *a;
    *a = *b;
    *b = tmp;
}

/* ===========================================================================
 * 47. reverse_string — NUL 종료 문자열 in-place 뒤집기.
 * ========================================================================= */
void reverse_string(char *s)
{
    if (s == NULL)
        return;
    size_t len = 0;
    while (s[len] != '\0')
        ++len;
    for (size_t i = 0, j = (len == 0 ? 0 : len - 1); i < j; ++i, --j) {
        char t = s[i];
        s[i] = s[j];
        s[j] = t;
    }
}

/* ===========================================================================
 * 48. const 위치별 의미 — 런타임 실증.
 *   const int *p      : *p 읽기전용, p 재지정 가능        (pointer-to-const)
 *   int * const p     : p 재지정 불가, *p 수정 가능        (const pointer)
 *   const int * const p: 둘 다 불가.
 *   반환값으로 의미를 검증한다.
 * ========================================================================= */
int demo_pointer_const(void)
{
    int x = 10, y = 7;

    int *const cp = &x;      /* const pointer: cp는 항상 &x */
    *cp = 42;                /* OK: 가리키는 값 수정 가능 */
    /* cp = &y;  // 컴파일 에러: cp는 재지정 불가 */

    const int *pc = &x;      /* pointer-to-const: *pc 읽기전용 */
    /* *pc = 1;  // 컴파일 에러: 값 수정 불가 */
    pc = &y;                 /* OK: 다른 주소로 재지정 가능 */

    return *cp + *pc;        /* 42 + 7 = 49 */
}

/* ===========================================================================
 * 49. volatile — HW 상태 레지스터 폴링.
 *   volatile: 컴파일러가 "매 접근마다 실제 메모리를 다시 읽도록" 강제.
 *   최적화로 레지스터/ISR 공유 변수를 캐싱·제거하지 못하게 함.
 *   여기서는 memory-mapped status register 폴링을 모사한다.
 * ========================================================================= */
bool wait_ready(volatile const uint32_t *status, uint32_t mask, int max_iters)
{
    if (status == NULL)
        return false;
    for (int i = 0; i < max_iters; ++i) {
        /* volatile 이므로 매 반복 실제 레지스터를 재로드 — HW가 값 갱신 가능 */
        if ((*status & mask) != 0u)
            return true;                             /* READY 비트 감지 */
    }
    return false;                                    /* timeout */
}

/* ===========================================================================
 * 50. 함수 포인터 디스패치 테이블 — opcode로 ALU 연산 선택.
 *   레지스터 명령 디코더, 프로토콜 커맨드 핸들러 등에서 흔한 패턴.
 * ========================================================================= */
static int32_t op_add(int32_t a, int32_t b) { return a + b; }
static int32_t op_sub(int32_t a, int32_t b) { return a - b; }
static int32_t op_mul(int32_t a, int32_t b) { return a * b; }
static int32_t op_and(int32_t a, int32_t b) { return a & b; }

typedef int32_t (*alu_fn_t)(int32_t, int32_t);

int32_t dispatch(uint8_t opcode, int32_t a, int32_t b)
{
    static const alu_fn_t table[] = { op_add, op_sub, op_mul, op_and };
    const size_t n = sizeof table / sizeof table[0];
    if (opcode >= n)
        return INT32_MIN;                            /* 잘못된 opcode 방어 */
    return table[opcode](a, b);
}

/* ===========================================================================
 * 51. 정적 메모리 풀 malloc/free — 힙 없이 고정 배열 위 first-fit 할당기.
 *   블록 헤더(크기/free 플래그) + 순차 배치 + split + 인접 free 병합(coalesce).
 *   임베디드에서 힙을 못 쓰거나 결정성이 필요할 때의 전형적 패턴.
 * ========================================================================= */
#define POOL_SIZE 1024u
#define ALIGN     8u

typedef struct blk {
    size_t size;         /* payload 바이트(헤더 제외) */
    bool   used;
    struct blk *next;    /* 주소 오름차순 링크 */
} blk_t;

static _Alignas(16) uint8_t g_pool[POOL_SIZE];
static blk_t  *g_head = NULL;

static size_t align_up(size_t v, size_t a) { return (v + (a - 1)) & ~(a - 1); }

static void pool_init(void)
{
    g_head = (blk_t *)(void *)g_pool;
    g_head->size = POOL_SIZE - sizeof(blk_t);
    g_head->used = false;
    g_head->next = NULL;
}

void *static_malloc(size_t size)
{
    if (g_head == NULL)
        pool_init();
    if (size == 0)
        return NULL;
    size = align_up(size, ALIGN);

    for (blk_t *b = g_head; b != NULL; b = b->next) {
        if (b->used || b->size < size)
            continue;
        /* 남는 공간이 헤더+최소블록보다 크면 split */
        if (b->size >= size + sizeof(blk_t) + ALIGN) {
            uint8_t *raw = (uint8_t *)b + sizeof(blk_t) + size;
            blk_t *nb = (blk_t *)(void *)raw;
            nb->size = b->size - size - sizeof(blk_t);
            nb->used = false;
            nb->next = b->next;
            b->size  = size;
            b->next  = nb;
        }
        b->used = true;
        return (uint8_t *)b + sizeof(blk_t);         /* payload 포인터 */
    }
    return NULL;                                     /* 공간 부족 */
}

static void pool_coalesce(void)
{
    for (blk_t *b = g_head; b != NULL && b->next != NULL; ) {
        if (!b->used && !b->next->used) {
            b->size += sizeof(blk_t) + b->next->size;
            b->next  = b->next->next;                /* 다음 free와 병합 */
        } else {
            b = b->next;
        }
    }
}

void static_free(void *ptr)
{
    if (ptr == NULL)
        return;                                      /* free(NULL)은 no-op */
    blk_t *b = (blk_t *)(void *)((uint8_t *)ptr - sizeof(blk_t));
    b->used = false;
    pool_coalesce();                                 /* 단편화 완화 */
}

/* ===========================================================================
 * 52. Dangling pointer 방어 — free 후 포인터를 NULL로 만드는 안전 idiom.
 *   ptr-to-ptr 를 받아 실제 free + 호출자 포인터를 NULL 세팅.
 *   지역변수 주소 반환/free 후 사용/이중 free 가 대표적 dangling 원인.
 * ========================================================================= */
void safe_free(void **pp)
{
    if (pp == NULL || *pp == NULL)
        return;
    free(*pp);
    *pp = NULL;                                       /* 이후 사용/이중free 차단 */
}

/* ===========================================================================
 * 53. void* 제네릭 — 임의 타입/크기 객체 교환(바이트 단위).
 *   타입 소거(type erasure): 크기만 알면 어떤 타입이든 교환 가능.
 * ========================================================================= */
void generic_swap(void *a, void *b, size_t size)
{
    if (a == b || a == NULL || b == NULL)
        return;
    unsigned char *pa = (unsigned char *)a;
    unsigned char *pb = (unsigned char *)b;
    for (size_t i = 0; i < size; ++i) {              /* 임시버퍼 크기제약 없음 */
        unsigned char t = pa[i];
        pa[i] = pb[i];
        pb[i] = t;
    }
}

/* ===========================================================================
 * 54. 구조체 패딩/정렬 — 멤버 순서가 sizeof를 바꾼다.
 *   각 멤버는 자신의 정렬 경계에 놓이며 사이/끝에 padding 삽입.
 *   큰→작은 순서로 재배치하면 padding 최소화.
 * ========================================================================= */
struct Unpacked  { uint8_t a; uint32_t b; uint8_t c; };  /* 1+3pad+4+1+3pad = 12 */
struct Reordered { uint32_t b; uint8_t a; uint8_t c; };  /* 4+1+1+2pad     = 8  */

size_t sizeof_unpacked(void)     { return sizeof(struct Unpacked); }
size_t sizeof_reordered(void)    { return sizeof(struct Reordered); }
size_t offset_of_b_unpacked(void){ return offsetof(struct Unpacked, b); }

/* ===========================================================================
 * 55. 포인터 배열 vs 배열 포인터
 *   int *pa[4]      : 포인터 4개를 원소로 갖는 "포인터의 배열"
 *   int (*pp)[4]    : "int[4] 배열 전체"를 가리키는 하나의 포인터
 * ========================================================================= */
/* 배열 포인터를 받아 i번째 원소 반환: (*p)[i] */
int deref_array_pointer(int (*p)[4], size_t i)
{
    if (p == NULL || i >= 4)
        return -1;
    return (*p)[i];
}

/* 포인터 배열(문자열 테이블) 순회: NULL 종료 표를 세어 개수 반환 */
size_t count_strings(const char *const *arr)
{
    if (arr == NULL)
        return 0;
    size_t n = 0;
    while (arr[n] != NULL)                            /* 포인터 원소들을 순회 */
        ++n;
    return n;
}

/* ===========================================================================
 * main — PASS/FAIL 하네스
 * ========================================================================= */
int main(void)
{
    printf("=== 04 Memory & Pointers (Q41-55) ===\n");

    /* 41 my_memset */
    printf("\n[41] my_memset\n");
    {
        unsigned char buf[6];
        my_memset(buf, 0xAA, 4);
        buf[4] = 0x11; buf[5] = 0x22;                /* 경계: 4번째 이후 불변 */
        unsigned char ref[6] = {0xAA,0xAA,0xAA,0xAA,0x11,0x22};
        EXPECT_MEM("my_memset(buf,0xAA,4)", buf, ref, 6);
        EXPECT_TRUE("my_memset returns dest", my_memset(buf, 0, 0) == buf);
    }

    /* 42 my_memcpy */
    printf("\n[42] my_memcpy\n");
    {
        char dst[8] = {0};
        const char *src = "abcdef";
        void *r = my_memcpy(dst, src, 4);
        EXPECT_MEM("my_memcpy(dst,\"abcdef\",4)", dst, "abcd", 4);
        EXPECT_TRUE("my_memcpy returns dest", r == dst);
        EXPECT_TRUE("n=0 no-op", (my_memcpy(dst, src, 0) == dst) && dst[0]=='a');
    }

    /* 43 my_memmove — overlap */
    printf("\n[43] my_memmove (overlap)\n");
    {
        char a[8] = "abcdef";
        char b[8] = "abcdef";
        my_memmove(a + 2, a, 4);          /* dest > src: 뒤로 겹침 */
        memmove(b + 2, b, 4);
        EXPECT_MEM("forward-overlap vs libc", a, b, 8);

        char c[8] = "abcdef";
        char d[8] = "abcdef";
        my_memmove(c, c + 2, 4);          /* dest < src: 앞으로 겹침 */
        memmove(d, d + 2, 4);
        EXPECT_MEM("backward-overlap vs libc", c, d, 8);
    }

    /* 44 my_memcmp */
    printf("\n[44] my_memcmp\n");
    {
        EXPECT_TRUE("memcmp(abc,abd,3) < 0", my_memcmp("abc","abd",3) < 0);
        EXPECT_TRUE("memcmp(abd,abc,3) > 0", my_memcmp("abd","abc",3) > 0);
        EXPECT_EQ  ("memcmp(abc,abc,3) == 0", my_memcmp("abc","abc",3), 0);
        EXPECT_EQ  ("memcmp(x,y,0) == 0",     my_memcmp("x","y",0),     0);
        /* 부호: 0x80 vs 0x01 은 unsigned 비교여야 양수 */
        unsigned char hi[1] = {0x80}, lo[1] = {0x01};
        EXPECT_TRUE("unsigned-compare 0x80>0x01", my_memcmp(hi,lo,1) > 0);
    }

    /* 45 my_memchr */
    printf("\n[45] my_memchr\n");
    {
        const char *s = "abcabc";
        EXPECT_TRUE("find 'b'  -> s+1", my_memchr(s,'b',6) == s + 1);
        EXPECT_TRUE("find 'z'  -> NULL", my_memchr(s,'z',6) == NULL);
        EXPECT_TRUE("range限定 n=1 miss", my_memchr(s,'b',1) == NULL);
    }

    /* 46 swap_int */
    printf("\n[46] swap_int\n");
    {
        int32_t x = 1, y = 2;
        swap_int(&x, &y);
        EXPECT_EQ("after swap x", x, 2);
        EXPECT_EQ("after swap y", y, 1);
        swap_int(&x, &x);                            /* alias 안전 */
        EXPECT_EQ("swap(&x,&x) keeps x", x, 2);
    }

    /* 47 reverse_string */
    printf("\n[47] reverse_string\n");
    {
        char s1[] = "hello";  reverse_string(s1);
        EXPECT_TRUE("reverse hello -> olleh", strcmp(s1,"olleh")==0);
        char s2[] = "ab";     reverse_string(s2);
        EXPECT_TRUE("reverse ab -> ba",       strcmp(s2,"ba")==0);
        char s3[] = "";       reverse_string(s3);
        EXPECT_TRUE("reverse empty -> empty",  strcmp(s3,"")==0);
        char s4[] = "x";      reverse_string(s4);
        EXPECT_TRUE("reverse single -> single", strcmp(s4,"x")==0);
    }

    /* 48 const 위치 */
    printf("\n[48] const int* vs int* const\n");
    {
        EXPECT_EQ("demo_pointer_const()", demo_pointer_const(), 49);
    }

    /* 49 volatile / HW register polling */
    printf("\n[49] volatile (HW status register)\n");
    {
        volatile uint32_t ready   = 0x00000001u;     /* READY 비트 셋 */
        volatile uint32_t notyet  = 0x00000000u;
        EXPECT_TRUE("wait_ready sees READY",  wait_ready(&ready,  0x1, 10) == true);
        EXPECT_TRUE("wait_ready times out",   wait_ready(&notyet, 0x1, 10) == false);
        EXPECT_TRUE("NULL status -> false",   wait_ready(NULL,    0x1, 10) == false);
    }

    /* 50 dispatch table */
    printf("\n[50] function-pointer dispatch table\n");
    {
        EXPECT_EQ("dispatch ADD 3,4",  dispatch(0, 3, 4), 7);
        EXPECT_EQ("dispatch SUB 3,4",  dispatch(1, 3, 4), -1);
        EXPECT_EQ("dispatch MUL 3,4",  dispatch(2, 3, 4), 12);
        EXPECT_EQ("dispatch AND 6,3",  dispatch(3, 6, 3), 2);
        EXPECT_EQ("dispatch bad op",   dispatch(99, 1, 1), INT32_MIN);
    }

    /* 51 static pool malloc/free */
    printf("\n[51] static memory pool malloc/free\n");
    {
        void *p1 = static_malloc(100);
        void *p2 = static_malloc(200);
        EXPECT_TRUE("p1 != NULL", p1 != NULL);
        EXPECT_TRUE("p2 != NULL", p2 != NULL);
        EXPECT_TRUE("p1 != p2",   p1 != p2);
        if (p1 && p2) {
            my_memset(p1, 0x5A, 100);                /* 쓰기 가능해야 함 */
            my_memset(p2, 0xA5, 200);
        }
        EXPECT_TRUE("oversize alloc -> NULL", static_malloc(POOL_SIZE * 2) == NULL);
        EXPECT_TRUE("alloc(0) -> NULL",       static_malloc(0) == NULL);
        static_free(p1);
        static_free(p2);
        static_free(NULL);                           /* no crash */
        /* 전부 free 후 병합되어 큰 블록 재할당 성공 */
        void *big = static_malloc(POOL_SIZE - 64);
        EXPECT_TRUE("coalesce -> big alloc ok", big != NULL);
        static_free(big);
    }

    /* 52 dangling pointer 방어 */
    printf("\n[52] dangling pointer (safe_free)\n");
    {
        int *p = (int *)malloc(sizeof *p);
        *p = 5;
        safe_free((void **)&p);
        EXPECT_TRUE("safe_free NULLs pointer", p == NULL);
        safe_free((void **)&p);                      /* 이중 free 안전(no-op) */
        EXPECT_TRUE("double safe_free no-op",  p == NULL);
    }

    /* 53 void* generic swap */
    printf("\n[53] void* generic swap\n");
    {
        int ia = 11, ib = 22;
        generic_swap(&ia, &ib, sizeof ia);
        EXPECT_EQ("swap int a", ia, 22);
        EXPECT_EQ("swap int b", ib, 11);

        double da = 1.5, db = 9.5;
        generic_swap(&da, &db, sizeof da);
        EXPECT_TRUE("swap double", da == 9.5 && db == 1.5);

        struct P { int x, y; } sa = {1,2}, sb = {3,4};
        generic_swap(&sa, &sb, sizeof sa);
        EXPECT_TRUE("swap struct", sa.x==3 && sa.y==4 && sb.x==1 && sb.y==2);
    }

    /* 54 struct padding */
    printf("\n[54] struct padding / alignment\n");
    {
        EXPECT_EQ("sizeof Unpacked",  sizeof_unpacked(),  12);
        EXPECT_EQ("sizeof Reordered", sizeof_reordered(), 8);
        EXPECT_EQ("offsetof(b) unpacked", offset_of_b_unpacked(), 4);
    }

    /* 55 pointer array vs array pointer */
    printf("\n[55] pointer-array vs array-pointer\n");
    {
        int arr[4] = {10, 20, 30, 40};
        int (*pp)[4] = &arr;                          /* 배열 포인터 */
        EXPECT_EQ("deref_array_pointer i=2", deref_array_pointer(pp, 2), 30);
        EXPECT_EQ("deref out-of-range",      deref_array_pointer(pp, 9), -1);

        const char *tbl[] = {"IMU", "GPS", "BARO", NULL}; /* 포인터 배열 */
        EXPECT_EQ("count_strings table", count_strings(tbl), 3);
        EXPECT_EQ("count_strings NULL",  count_strings(NULL), 0);
    }

    /* 요약 */
    printf("\n=====================================\n");
    printf("TOTAL: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
