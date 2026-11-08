/* C-B05 포인터 기초와 포인터 산술
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ---------------- Part A: 포인터 타입 = 산술 단위 ---------------- */
/* p 에서 q 까지 몇 "바이트" 인가 (같은 객체 안의 두 주소). void* 끼리는 뺄 수 없다! */
static ptrdiff_t byte_diff(const void *p, const void *q) {
    const unsigned char *a = p, *b = q;
    return b - a;                         /* 단위 = 1바이트 */
}
/* p 를 n 바이트 전진. `p + n` (void* 산술)은 GNU 확장 — 표준 C로 쓴다 */
static const void *byte_advance(const void *p, size_t n) {
    const unsigned char *c = p;
    return c + n;                         /* char* 는 1씩 움직인다 */
}
/* 32비트 워드 배열을 바이트 배열로 보고 byte_idx 번째 바이트를 읽는다 */
static uint8_t byte_at(const uint32_t *words, size_t byte_idx) {
    const uint8_t *b = (const uint8_t *)words;   /* 문자 타입으로 보는 건 합법 */
    return b[byte_idx];                   /* == *(b + byte_idx) */
}

/* ---------------- Part B: 이중 포인터와 swap ---------------- */
struct node { int val; struct node *next; };

/* n 을 맨 앞에 끼운다. head 변수 자체가 바뀌므로 struct node ** 를 받는다 */
static void list_push_front(struct node **head, struct node *n) {
    n->next = *head; *head = n;          /* 호출자의 head 변수를 고친다 */
}
/* val 과 같은 첫 노드를 떼어내 돌려준다 (떼어낸 노드의 next 는 NULL 로). 없으면 NULL.
 * "포인터를 가리키는 포인터"로 head 특수 처리(if) 없이 쓴다 */
static struct node *list_remove(struct node **head, int val) {
    for (struct node **pp = head; *pp; pp = &(*pp)->next) {
        if ((*pp)->val == val) {
            struct node *victim = *pp;
            *pp = victim->next;           /* head든 중간이든 같은 한 줄 */
            victim->next = NULL;
            return victim;
        }
    }
    return NULL;
}
/* 두 int 값을 바꾼다 */
static void swap_int(int *a, int *b) {
    int t = *a; *a = *b; *b = t;          /* a == b 여도 안전 */
}
/* 두 포인터 변수 자체를 바꾼다 (가리키는 값은 그대로) */
static void swap_ptr(int **a, int **b) {
    int *t = *a; *a = *b; *b = t;        /* 바꾸는 대상이 int* 라서 int** 를 받는다 */
}

/* ---------------- Part C: 레지스터 블록과 버퍼 걷기 ---------------- */
typedef struct {
    volatile uint32_t CR;   /* 0x00 control  */
    volatile uint32_t SR;   /* 0x04 status   */
    volatile uint32_t DR;   /* 0x08 data     */
    volatile uint32_t BRR;  /* 0x0C baudrate */
} UART_TypeDef;
_Static_assert(offsetof(UART_TypeDef, BRR) == 0x0C, "register map");
enum { UART_CR = 0x00, UART_SR = 0x04, UART_DR = 0x08, UART_BRR = 0x0C };

static UART_TypeDef fake_uart;   /* 실제 칩이라면 base = 0x40011000 (STM32F4 USART1) */

/* base + "바이트" offset 에 있는 32비트 레지스터의 주소 */
static volatile uint32_t *reg_at(volatile void *base, uint32_t off) {
    volatile uint8_t *b = base;           /* 바이트 단위로 offset 을 더한 뒤 */
    return (volatile uint32_t *)(b + off);  /* 32비트 레지스터 포인터로 */
}
/* 바이트 단위로 걸으며 XOR. *iters 에 루프 횟수 기록 */
static uint8_t xor8(const uint8_t *p, size_t len, size_t *iters) {
    const uint8_t *end = p + len;         /* one-past-the-end: 만들기만 OK */
    uint8_t x = 0; *iters = 0;
    while (p != end) { x ^= *p++; (*iters)++; }
    return x;
}
/* 워드 단위로 걸으며 XOR (같은 데이터, 루프 1/4) */
static uint32_t xor32(const uint32_t *p, size_t nwords, size_t *iters) {
    const uint32_t *end = p + nwords;     /* +nwords = 4*nwords 바이트 */
    uint32_t x = 0; *iters = 0;
    while (p != end) { x ^= *p++; (*iters)++; }
    return x;
}
/* 32비트 XOR 결과를 8비트로 접는다 (xor8 과 비교용, 이미 완성) */
static uint8_t fold32to8(uint32_t x) { return (uint8_t)(x ^ (x >> 8) ^ (x >> 16) ^ (x >> 24)); }

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static size_t list_dump(const struct node *h, int *out, size_t max) {
    size_t n = 0;
    for (; h && n < max; h = h->next) out[n++] = h->val;
    return n;
}

int main(void) {
    /* _Alignas(8): uint64_t* 로 변환하려면 8바이트 정렬이 필요 (6.3.2.3p7) */
    static _Alignas(8) const uint32_t buf[4] = { 0x11223344u, 0x55667788u, 0x99AABBCCu, 0xDDEEFF00u };
    const uint8_t  *p8  = (const uint8_t *)buf;
    const uint16_t *p16 = (const uint16_t *)buf;
    const uint32_t *p32 = buf;
    const uint64_t *p64 = (const uint64_t *)buf;
    const void     *pv  = buf;

    /* 같은 버퍼, 다른 포인터 타입 → p+1 의 주소 증가량 (uintptr_t 로 측정) */
    printf("%-22s %-14s %-14s %s\n", "pointer", "p", "p+1", "step");
#define ROW(name, p, p1) printf("%-22s %#-14jx %#-14jx %ju\n", name, \
        (uintmax_t)(uintptr_t)(p), (uintmax_t)(uintptr_t)(p1), \
        (uintmax_t)((uintptr_t)(p1) - (uintptr_t)(p)))
    ROW("const uint8_t *",  (const void *)p8,  (const void *)(p8 + 1));
    ROW("const uint16_t *", (const void *)p16, (const void *)(p16 + 1));
    ROW("const uint32_t *", (const void *)p32, (const void *)(p32 + 1));
    ROW("const uint64_t *", (const void *)p64, (const void *)(p64 + 1));
    ROW("void * (via char *)", pv, (const void *)((const unsigned char *)pv + 1));
    puts("");

    /* Part A */
    CHECK(byte_diff(p8, p8 + 1) == 1);
    CHECK(byte_diff(p32, p32 + 1) == 4);
    CHECK(byte_diff(p32 + 3, p32) == -12);
    CHECK(byte_advance(pv, 4) == (const void *)(p32 + 1));
    CHECK(byte_advance(pv, 0) == pv);
    CHECK(byte_at(buf, 0) == 0x44);     /* 리틀 엔디언 (arm64, Cortex-M 기본) — C-B13 */
    CHECK(byte_at(buf, 5) == 0x77);
    CHECK(byte_at(buf, 15) == 0xDD);

    /* Part B — 리스트 (힙 없이 정적 노드 풀) */
    static struct node pool[3] = { { 1, NULL }, { 2, NULL }, { 3, NULL } };
    struct node *head = NULL;
    int out[4];
    list_push_front(&head, &pool[2]);
    list_push_front(&head, &pool[1]);
    list_push_front(&head, &pool[0]);
    CHECK(head == &pool[0] && list_dump(head, out, 4) == 3 && out[0] == 1 && out[2] == 3);
    CHECK(list_remove(&head, 1) == &pool[0] && head == &pool[1]);       /* head 제거 */
    CHECK(pool[0].next == NULL);
    CHECK(list_remove(&head, 3) == &pool[2]);                           /* tail 제거 */
    CHECK(list_dump(head, out, 4) == 1 && out[0] == 2);
    CHECK(list_remove(&head, 9) == NULL && head == &pool[1]);           /* 없는 값 */
    CHECK(list_remove(&head, 2) == &pool[1] && head == NULL);           /* 마지막 노드 */
    CHECK(list_remove(&head, 2) == NULL && head == NULL);               /* 빈 리스트 */

    int x = 1, y = 2;
    swap_int(&x, &y);
    CHECK(x == 2 && y == 1);
    swap_int(&x, &x);                                                   /* 같은 주소 */
    CHECK(x == 2);
    int *pa = &x, *pb = &y;
    swap_ptr(&pa, &pb);
    CHECK(pa == &y && pb == &x);
    CHECK(x == 2 && y == 1);                                            /* 값은 그대로 */

    /* Part C — 레지스터 블록 base + offset */
    volatile void *base = &fake_uart;
    CHECK(reg_at(base, UART_CR) == &fake_uart.CR);
    CHECK(reg_at(base, UART_DR) == &fake_uart.DR);
    CHECK(reg_at(base, UART_BRR) == &fake_uart.BRR);
    volatile uint32_t *brr = reg_at(base, UART_BRR);
    if (brr) *brr = 0x683u;                                             /* 16MHz / 9600 */
    CHECK(fake_uart.BRR == 0x683u);

    /* Part C — 같은 DMA 버퍼를 바이트로 / 워드로 걷기 */
    static const uint32_t pkt[4] = { 0xDEADBEEFu, 0x01020304u, 0xA5A5A5A5u, 0x0000FFFFu };
    size_t n8 = 99, n32 = 99;
    uint8_t  c8  = xor8((const uint8_t *)pkt, sizeof pkt, &n8);
    uint32_t c32 = xor32(pkt, sizeof pkt / sizeof pkt[0], &n32);
    CHECK(c8 == 0x26 && n8 == 16);
    CHECK(c32 == 0x7A0AE7B1u && n32 == 4);
    CHECK(fold32to8(c32) == c8);
    CHECK(xor8((const uint8_t *)pkt + 1, sizeof pkt - 1, &n8) == 0xC9 && n8 == 15);  /* 비정렬 시작 */
    CHECK(xor8((const uint8_t *)pkt, 0, &n8) == 0 && n8 == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
