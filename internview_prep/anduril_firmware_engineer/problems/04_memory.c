/* ============================================================================
 * 04_memory.c  —  Memory & Pointers (메모리 & 포인터)  Q41–55   [PRACTICE STUB]
 * Anduril Firmware Engineer C interview prep.
 *
 * 빌드:  cc -std=c11 -Wall -Wextra problems/04_memory.c -o /tmp/andb_memory
 * 실행:  /tmp/andb_memory
 *
 * 사용법: 각 함수의 // TODO 를 채우고 다시 빌드/실행해 FAIL -> PASS 로 바꾼다.
 *         지금은 placeholder 라 컴파일/실행은 되지만 대부분 FAIL 이 뜬다.
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

#define EXPECT_MEM(label, buf, ref, n) do {                                    \
    bool _ok = (memcmp((buf), (ref), (n)) == 0);                               \
    printf("  %-46s => %s  [%s] (expected match)\n",                           \
           (label), _ok ? "match" : "diff ", _ok ? "PASS" : "FAIL");           \
    if (_ok) g_pass++; else g_fail++;                                          \
} while (0)

/* ===========================================================================
 * 41. my_memset
 *   KO: s의 앞 n바이트를 (unsigned char)c 값으로 채우고 s를 반환하라.
 *   EN: Fill first n bytes of s with (unsigned char)c; return s.
 *   예: my_memset(buf,0xAA,4) -> buf = {AA,AA,AA,AA}
 * ========================================================================= */
void *my_memset(void *s, int c, size_t n)
{
    (void)c; (void)n;
    /* TODO: implement */
    return s;
}

/* ===========================================================================
 * 42. my_memcpy
 *   KO: src의 n바이트를 dest로 복사(겹침 미보장)하고 dest 반환.
 *   EN: Copy n bytes src->dest (no overlap); return dest.
 *   예: my_memcpy(dst,"abcdef",4) -> dst[0..3]="abcd"
 * ========================================================================= */
void *my_memcpy(void *restrict dest, const void *restrict src, size_t n)
{
    (void)src; (void)n;
    /* TODO: implement */
    return dest;
}

/* ===========================================================================
 * 43. my_memmove  ★ overlap 안전 ★
 *   KO: 영역이 겹쳐도 올바르게 n바이트 복사. dest>src면 뒤에서부터.
 *   EN: Copy n bytes safely even if [dest,dest+n) overlaps [src,src+n).
 *   예: buf="abcdef"; my_memmove(buf+2,buf,4) -> "ababcd"
 * ========================================================================= */
void *my_memmove(void *dest, const void *src, size_t n)
{
    (void)src; (void)n;
    /* TODO: implement (dest<src: 앞→뒤, dest>src: 뒤→앞) */
    return dest;
}

/* ===========================================================================
 * 44. my_memcmp
 *   KO: 첫 불일치 바이트를 unsigned char로 비교해 <0/0/>0 반환.
 *   EN: Compare n bytes as unsigned char; return sign of first difference.
 *   예: my_memcmp("abc","abd",3) < 0
 * ========================================================================= */
int my_memcmp(const void *s1, const void *s2, size_t n)
{
    (void)s1; (void)s2; (void)n;
    /* TODO: implement */
    return 0;
}

/* ===========================================================================
 * 45. my_memchr
 *   KO: 앞 n바이트에서 (unsigned char)c 첫 위치 포인터, 없으면 NULL.
 *   EN: Return pointer to first (unsigned char)c within n bytes, else NULL.
 *   예: my_memchr("abcabc",'b',6) -> &s[1]
 * ========================================================================= */
void *my_memchr(const void *s, int c, size_t n)
{
    (void)s; (void)c; (void)n;
    /* TODO: implement */
    return NULL;
}

/* ===========================================================================
 * 46. swap_int
 *   KO: 포인터로 두 정수를 교환. 같은 주소여도 안전하게.
 *   EN: Swap two ints via pointers; alias-safe.
 * ========================================================================= */
void swap_int(int32_t *a, int32_t *b)
{
    (void)a; (void)b;
    /* TODO: implement */
}

/* ===========================================================================
 * 47. reverse_string
 *   KO: NUL 종료 문자열을 in-place로 뒤집는다.
 *   EN: Reverse a NUL-terminated string in place.
 *   예: "hello" -> "olleh"
 * ========================================================================= */
void reverse_string(char *s)
{
    (void)s;
    /* TODO: implement */
}

/* ===========================================================================
 * 48. const 위치별 의미 (개념 + 실증)
 *   KO: const int*(값 읽기전용,포인터 재지정 가능), int* const(포인터 고정,
 *       값 수정 가능), const int* const(둘 다 불가)을 코드로 실증.
 *       int x=10,y=7; int* const cp=&x; *cp=42; const int* pc=&x; pc=&y;
 *       return *cp + *pc;  // 49 가 나오도록.
 *   EN: Demonstrate the three const placements; return 49.
 * ========================================================================= */
int demo_pointer_const(void)
{
    /* TODO: implement (return 42 + 7 == 49) */
    return 0;
}

/* ===========================================================================
 * 49. volatile — HW 상태 레지스터 폴링 (개념 + 실증)
 *   KO: mask 비트가 set될 때까지 최대 max_iters회 status를 폴링.
 *       set이면 true, timeout이면 false. status는 volatile 로 매 접근 재로드.
 *   EN: Poll *status until (*status & mask); volatile forces re-read.
 * ========================================================================= */
bool wait_ready(volatile const uint32_t *status, uint32_t mask, int max_iters)
{
    (void)status; (void)mask; (void)max_iters;
    /* TODO: implement (NULL이면 false) */
    return false;
}

/* ===========================================================================
 * 50. 함수 포인터 디스패치 테이블
 *   KO: opcode(0=ADD,1=SUB,2=MUL,3=AND)로 ALU 함수를 골라 a,b에 적용.
 *       잘못된 opcode는 INT32_MIN 반환. 함수 포인터 배열을 사용할 것.
 *   EN: Dispatch a,b via a function-pointer table indexed by opcode.
 * ========================================================================= */
typedef int32_t (*alu_fn_t)(int32_t, int32_t);

int32_t dispatch(uint8_t opcode, int32_t a, int32_t b)
{
    (void)opcode; (void)a; (void)b;
    /* TODO: static const alu_fn_t table[] = {...}; 인덱스로 호출 */
    return INT32_MIN;
}

/* ===========================================================================
 * 51. 정적 메모리 풀 malloc/free
 *   KO: 힙 없이 static 배열 위에서 first-fit 할당기 구현.
 *       블록 헤더(크기/free) + split + 인접 free 병합(coalesce).
 *       size==0 또는 공간부족은 NULL. static_free(NULL)은 no-op.
 *   EN: Bump/first-fit allocator over a fixed static pool; free coalesces.
 * ========================================================================= */
#define POOL_SIZE 1024u
#define ALIGN     8u

typedef struct blk {
    size_t size;         /* payload 바이트(헤더 제외) */
    bool   used;
    struct blk *next;
} blk_t;

static _Alignas(16) uint8_t g_pool[POOL_SIZE];
static blk_t  *g_head = NULL;

void *static_malloc(size_t size)
{
    (void)size; (void)g_pool; (void)g_head;
    /* TODO: implement (pool_init -> first-fit -> split -> payload 반환) */
    return NULL;
}

void static_free(void *ptr)
{
    (void)ptr;
    /* TODO: implement (헤더 복원 -> used=false -> coalesce) */
}

/* ===========================================================================
 * 52. Dangling pointer 방어
 *   KO: **pp 를 받아 실제 free 후 *pp = NULL 로 만들어 dangling/이중free 차단.
 *   EN: free(*pp) then set *pp=NULL to prevent dangling/double-free.
 * ========================================================================= */
void safe_free(void **pp)
{
    (void)pp;
    /* TODO: implement (pp 와 *pp 의 NULL 방어) */
}

/* ===========================================================================
 * 53. void* 제네릭 swap
 *   KO: 임의 타입/크기 객체를 바이트 단위로 교환(size 바이트).
 *   EN: Swap two objects of arbitrary size byte-by-byte via void*.
 * ========================================================================= */
void generic_swap(void *a, void *b, size_t size)
{
    (void)a; (void)b; (void)size;
    /* TODO: implement */
}

/* ===========================================================================
 * 54. 구조체 패딩/정렬
 *   KO: 멤버 순서가 sizeof를 바꾼다. 아래 두 구조체의 sizeof/offsetof 반환.
 *       Unpacked{u8,u32,u8} 는 12, Reordered{u32,u8,u8} 는 8, offsetof(b)=4.
 *   EN: Return sizeof/offsetof reflecting padding rules.
 * ========================================================================= */
struct Unpacked  { uint8_t a; uint32_t b; uint8_t c; };
struct Reordered { uint32_t b; uint8_t a; uint8_t c; };

size_t sizeof_unpacked(void)      { /* TODO */ return 0; }
size_t sizeof_reordered(void)     { /* TODO */ return 0; }
size_t offset_of_b_unpacked(void) { /* TODO */ return 0; }

/* ===========================================================================
 * 55. 포인터 배열 vs 배열 포인터
 *   KO: int (*p)[4] 는 "int[4] 전체"를 가리키는 배열 포인터 -> (*p)[i] 반환.
 *       const char *const *arr 는 NULL 종료 "포인터 배열" -> 원소 개수 반환.
 *   EN: array-pointer deref vs counting a NULL-terminated pointer array.
 * ========================================================================= */
int deref_array_pointer(int (*p)[4], size_t i)
{
    (void)p; (void)i;
    /* TODO: implement (범위 밖/NULL이면 -1) */
    return -1;
}

size_t count_strings(const char *const *arr)
{
    (void)arr;
    /* TODO: implement (NULL이면 0) */
    return 0;
}

/* ===========================================================================
 * main — PASS/FAIL 하네스 (수정 금지: 함수만 채우면 됨)
 * ========================================================================= */
int main(void)
{
    printf("=== 04 Memory & Pointers (Q41-55) ===\n");

    printf("\n[41] my_memset\n");
    {
        unsigned char buf[6];
        my_memset(buf, 0xAA, 4);
        buf[4] = 0x11; buf[5] = 0x22;
        unsigned char ref[6] = {0xAA,0xAA,0xAA,0xAA,0x11,0x22};
        EXPECT_MEM("my_memset(buf,0xAA,4)", buf, ref, 6);
        EXPECT_TRUE("my_memset returns dest", my_memset(buf, 0, 0) == buf);
    }

    printf("\n[42] my_memcpy\n");
    {
        char dst[8] = {0};
        const char *src = "abcdef";
        void *r = my_memcpy(dst, src, 4);
        EXPECT_MEM("my_memcpy(dst,\"abcdef\",4)", dst, "abcd", 4);
        EXPECT_TRUE("my_memcpy returns dest", r == dst);
        EXPECT_TRUE("n=0 no-op", (my_memcpy(dst, src, 0) == dst) && dst[0]=='a');
    }

    printf("\n[43] my_memmove (overlap)\n");
    {
        char a[8] = "abcdef";
        char b[8] = "abcdef";
        my_memmove(a + 2, a, 4);
        memmove(b + 2, b, 4);
        EXPECT_MEM("forward-overlap vs libc", a, b, 8);

        char c[8] = "abcdef";
        char d[8] = "abcdef";
        my_memmove(c, c + 2, 4);
        memmove(d, d + 2, 4);
        EXPECT_MEM("backward-overlap vs libc", c, d, 8);
    }

    printf("\n[44] my_memcmp\n");
    {
        EXPECT_TRUE("memcmp(abc,abd,3) < 0", my_memcmp("abc","abd",3) < 0);
        EXPECT_TRUE("memcmp(abd,abc,3) > 0", my_memcmp("abd","abc",3) > 0);
        EXPECT_EQ  ("memcmp(abc,abc,3) == 0", my_memcmp("abc","abc",3), 0);
        EXPECT_EQ  ("memcmp(x,y,0) == 0",     my_memcmp("x","y",0),     0);
        unsigned char hi[1] = {0x80}, lo[1] = {0x01};
        EXPECT_TRUE("unsigned-compare 0x80>0x01", my_memcmp(hi,lo,1) > 0);
    }

    printf("\n[45] my_memchr\n");
    {
        const char *s = "abcabc";
        EXPECT_TRUE("find 'b'  -> s+1", my_memchr(s,'b',6) == s + 1);
        EXPECT_TRUE("find 'z'  -> NULL", my_memchr(s,'z',6) == NULL);
        EXPECT_TRUE("range限定 n=1 miss", my_memchr(s,'b',1) == NULL);
    }

    printf("\n[46] swap_int\n");
    {
        int32_t x = 1, y = 2;
        swap_int(&x, &y);
        EXPECT_EQ("after swap x", x, 2);
        EXPECT_EQ("after swap y", y, 1);
        swap_int(&x, &x);
        EXPECT_EQ("swap(&x,&x) keeps x", x, 2);
    }

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

    printf("\n[48] const int* vs int* const\n");
    {
        EXPECT_EQ("demo_pointer_const()", demo_pointer_const(), 49);
    }

    printf("\n[49] volatile (HW status register)\n");
    {
        volatile uint32_t ready   = 0x00000001u;
        volatile uint32_t notyet  = 0x00000000u;
        EXPECT_TRUE("wait_ready sees READY",  wait_ready(&ready,  0x1, 10) == true);
        EXPECT_TRUE("wait_ready times out",   wait_ready(&notyet, 0x1, 10) == false);
        EXPECT_TRUE("NULL status -> false",   wait_ready(NULL,    0x1, 10) == false);
    }

    printf("\n[50] function-pointer dispatch table\n");
    {
        EXPECT_EQ("dispatch ADD 3,4",  dispatch(0, 3, 4), 7);
        EXPECT_EQ("dispatch SUB 3,4",  dispatch(1, 3, 4), -1);
        EXPECT_EQ("dispatch MUL 3,4",  dispatch(2, 3, 4), 12);
        EXPECT_EQ("dispatch AND 6,3",  dispatch(3, 6, 3), 2);
        EXPECT_EQ("dispatch bad op",   dispatch(99, 1, 1), INT32_MIN);
    }

    printf("\n[51] static memory pool malloc/free\n");
    {
        void *p1 = static_malloc(100);
        void *p2 = static_malloc(200);
        EXPECT_TRUE("p1 != NULL", p1 != NULL);
        EXPECT_TRUE("p2 != NULL", p2 != NULL);
        EXPECT_TRUE("p1 != p2",   p1 != p2);
        if (p1 && p2) {
            my_memset(p1, 0x5A, 100);
            my_memset(p2, 0xA5, 200);
        }
        EXPECT_TRUE("oversize alloc -> NULL", static_malloc(POOL_SIZE * 2) == NULL);
        EXPECT_TRUE("alloc(0) -> NULL",       static_malloc(0) == NULL);
        static_free(p1);
        static_free(p2);
        static_free(NULL);
        void *big = static_malloc(POOL_SIZE - 64);
        EXPECT_TRUE("coalesce -> big alloc ok", big != NULL);
        static_free(big);
    }

    printf("\n[52] dangling pointer (safe_free)\n");
    {
        int *p = (int *)malloc(sizeof *p);
        *p = 5;
        safe_free((void **)&p);
        EXPECT_TRUE("safe_free NULLs pointer", p == NULL);
        safe_free((void **)&p);
        EXPECT_TRUE("double safe_free no-op",  p == NULL);
        if (p != NULL) free(p);   /* stub 미구현 시 누수 방지 */
    }

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

    printf("\n[54] struct padding / alignment\n");
    {
        EXPECT_EQ("sizeof Unpacked",  sizeof_unpacked(),  12);
        EXPECT_EQ("sizeof Reordered", sizeof_reordered(), 8);
        EXPECT_EQ("offsetof(b) unpacked", offset_of_b_unpacked(), 4);
    }

    printf("\n[55] pointer-array vs array-pointer\n");
    {
        int arr[4] = {10, 20, 30, 40};
        int (*pp)[4] = &arr;
        EXPECT_EQ("deref_array_pointer i=2", deref_array_pointer(pp, 2), 30);
        EXPECT_EQ("deref out-of-range",      deref_array_pointer(pp, 9), -1);

        const char *tbl[] = {"IMU", "GPS", "BARO", NULL};
        EXPECT_EQ("count_strings table", count_strings(tbl), 3);
        EXPECT_EQ("count_strings NULL",  count_strings(NULL), 0);
    }

    printf("\n=====================================\n");
    printf("TOTAL: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
