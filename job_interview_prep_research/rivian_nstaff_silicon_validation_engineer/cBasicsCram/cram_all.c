/* C basics cram — reference solutions + tests for the Rivian HM screen (2026-10-30).
 * Build & run:
 *   cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined cram_all.c && ./a.out
 * Every snippet in 2026-10-29_c_basics_cram.md is copied from this file.
 */
#include <ctype.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* ============================== C1. string copy family ============================== */
char *my_strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++) != '\0')
        ;                       /* copies the NUL too */
    return dst;
}

/* Bounded copy (strlcpy semantics): always NUL-terminates when size > 0,
 * returns strlen(src) so the caller can detect truncation (ret >= size). */
size_t my_strlcpy(char *dst, const char *src, size_t size)
{
    size_t i = 0;
    if (size > 0) {
        for (; i + 1 < size && src[i] != '\0'; i++)
            dst[i] = src[i];
        dst[i] = '\0';
    }
    while (src[i] != '\0')
        i++;
    return i;
}

size_t my_strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (size_t)(p - s);
}

int my_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

void *my_memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

/* Overlap-safe: copy backwards when dst is above src. */
void *my_memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d == s || n == 0)
        return dst;
    if ((uintptr_t)d < (uintptr_t)s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

/* ============================== C2. reverse ============================== */
static void rev_range(char *l, char *r)
{
    while (l < r) {
        char t = *l;
        *l++ = *r;
        *r-- = t;
    }
}

void revstr(char *s)
{
    if (s == NULL)
        return;
    size_t n = strlen(s);
    if (n < 2)
        return;                 /* also avoids s + n - 1 underflow on "" */
    rev_range(s, s + n - 1);
}

/* "hello big world" -> "world big hello" (single spaces) */
void rev_words(char *s)
{
    size_t n = strlen(s);
    if (n == 0)
        return;
    rev_range(s, s + n - 1);    /* 1) reverse whole string */
    char *w = s;
    for (char *p = s;; p++) {   /* 2) reverse each word back */
        if (*p == ' ' || *p == '\0') {
            if (p > w)
                rev_range(w, p - 1);
            if (*p == '\0')
                break;
            w = p + 1;
        }
    }
}

/* ============================== C3. atoi / itoa ============================== */
/* Skip leading whitespace, optional sign, digits until first non-digit,
 * clamp to INT_MAX / INT_MIN on overflow. */
int my_atoi(const char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    bool neg = false;
    if (*s == '+' || *s == '-')
        neg = (*s++ == '-');

    unsigned limit = neg ? (unsigned)INT_MAX + 1u : (unsigned)INT_MAX;
    unsigned acc = 0;
    while (*s >= '0' && *s <= '9') {
        unsigned d = (unsigned)(*s++ - '0');
        if (acc > (limit - d) / 10u)          /* acc*10 + d would exceed limit */
            return neg ? INT_MIN : INT_MAX;
        acc = acc * 10u + d;
    }
    if (neg)
        return acc == limit ? INT_MIN : -(int)acc;
    return (int)acc;
}

/* buf must hold 12 bytes ("-2147483648" + NUL). Handles INT_MIN. */
char *my_itoa(int v, char *buf)
{
    char tmp[11];
    int i = 0;
    unsigned u = v < 0 ? 0u - (unsigned)v : (unsigned)v;
    do {
        tmp[i++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u != 0);
    char *p = buf;
    if (v < 0)
        *p++ = '-';
    while (i > 0)
        *p++ = tmp[--i];
    *p = '\0';
    return buf;
}

/* "0x1F" / "1f" -> 31. Returns false on empty / bad char / > 8 hex digits. */
bool parse_hex_u32(const char *s, uint32_t *out)
{
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    uint32_t v = 0;
    int nd = 0;
    for (; *s; s++, nd++) {
        unsigned d;
        if (*s >= '0' && *s <= '9')      d = (unsigned)(*s - '0');
        else if (*s >= 'a' && *s <= 'f') d = (unsigned)(*s - 'a' + 10);
        else if (*s >= 'A' && *s <= 'F') d = (unsigned)(*s - 'A' + 10);
        else return false;
        if (nd == 8)
            return false;                  /* would overflow 32 bits */
        v = (v << 4) | d;
    }
    if (nd == 0)
        return false;
    *out = v;
    return true;
}

/* ============================== C4. 64-bit timer from two 32-bit regs ============================== */
/* Real code reads MMIO:  hi = REG32(TMR_HI); lo = REG32(TMR_LO); ...
 * Here the registers are callbacks so the test can make the counter tick between reads. */
typedef uint32_t (*rd32_fn)(void);

uint64_t timer_read64(rd32_fn rd_hi, rd32_fn rd_lo)
{
    uint32_t hi, lo, hi2;
    do {
        hi  = rd_hi();
        lo  = rd_lo();
        hi2 = rd_hi();
    } while (hi != hi2);        /* lo wrapped between the two hi reads -> retry */
    return ((uint64_t)hi << 32) | lo;
}

/* WRONG on purpose: shows the torn read. */
uint64_t timer_read64_naive(rd32_fn rd_hi, rd32_fn rd_lo)
{
    uint32_t hi = rd_hi();
    uint32_t lo = rd_lo();
    return ((uint64_t)hi << 32) | lo;
}

/* No HW high word: extend a free-running 32-bit counter in SW.
 * Must be called at least once per wrap period (2^32 ticks). Not ISR-safe as is. */
typedef struct { uint32_t last; uint64_t high; } ext64_t;

uint64_t ext64_update(ext64_t *e, uint32_t now)
{
    if (now < e->last)
        e->high += 1ull << 32;
    e->last = now;
    return e->high | now;
}

/* Wrap-safe compare: true if a is after b (valid while |a-b| < 2^31). */
static inline bool time_after32(uint32_t a, uint32_t b) { return (int32_t)(b - a) < 0; }

/* --- timer simulation for tests --- */
static uint64_t sim_t;
static uint32_t sim_hi(void) { uint32_t v = (uint32_t)(sim_t >> 32); sim_t++; return v; }
static uint32_t sim_lo(void) { uint32_t v = (uint32_t)sim_t;         sim_t++; return v; }

/* ============================== C5. ring buffer ============================== */
#define RB_SIZE 8u                      /* power of 2 -> & instead of % */
_Static_assert((RB_SIZE & (RB_SIZE - 1)) == 0, "RB_SIZE must be a power of 2");

typedef struct {
    uint8_t buf[RB_SIZE];
    volatile uint32_t head;             /* written only by producer (e.g. RX ISR) */
    volatile uint32_t tail;             /* written only by consumer (main loop)   */
} ring_t;                               /* free-running indices: count = head - tail */

static inline uint32_t rb_count(const ring_t *r) { return r->head - r->tail; }

bool rb_put(ring_t *r, uint8_t v)
{
    if (rb_count(r) == RB_SIZE)
        return false;                   /* full: drop newest, caller counts overflow */
    r->buf[r->head & (RB_SIZE - 1)] = v;
    r->head++;                          /* publish AFTER the data write */
    return true;
}

bool rb_get(ring_t *r, uint8_t *out)
{
    if (r->head == r->tail)
        return false;                   /* empty */
    *out = r->buf[r->tail & (RB_SIZE - 1)];
    r->tail++;
    return true;
}

/* Multi-core SPSC variant: C11 atomics give the ordering volatile does not. */
typedef struct {
    uint8_t buf[RB_SIZE];
    _Atomic uint32_t head, tail;
} aring_t;

bool arb_put(aring_t *r, uint8_t v)
{
    uint32_t h = atomic_load_explicit(&r->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (h - t == RB_SIZE)
        return false;
    r->buf[h & (RB_SIZE - 1)] = v;
    atomic_store_explicit(&r->head, h + 1, memory_order_release);   /* data visible before head */
    return true;
}

bool arb_get(aring_t *r, uint8_t *out)
{
    uint32_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&r->head, memory_order_acquire);
    if (h == t)
        return false;
    *out = r->buf[t & (RB_SIZE - 1)];
    atomic_store_explicit(&r->tail, t + 1, memory_order_release);   /* slot free after read */
    return true;
}

/* ============================== C6. bit / field masks ============================== */
#define BIT(n)            (1u << (n))
#define GENMASK(h, l)     (((~0u) >> (31u - (h))) & ((~0u) << (l)))
#define FIELD_GET(m, v)   (((uint32_t)(v) & (m)) >> __builtin_ctz(m))
#define FIELD_PREP(m, x)  (((uint32_t)(x) << __builtin_ctz(m)) & (m))

/* Portable (no builtin) shift/width form. width == 32 handled: 1u << 32 is UB. */
static inline uint32_t field_mask(unsigned shift, unsigned width)
{
    uint32_t m = (width >= 32u) ? ~0u : ((1u << width) - 1u);
    return m << shift;
}
static inline uint32_t get_field(uint32_t reg, unsigned shift, unsigned width)
{
    return (reg & field_mask(shift, width)) >> shift;
}
static inline uint32_t set_field(uint32_t reg, unsigned shift, unsigned width, uint32_t val)
{
    uint32_t m = field_mask(shift, width);
    return (reg & ~m) | ((val << shift) & m);
}

/* Read-modify-write a register field. Not atomic: an ISR touching the same reg needs a lock/irq-off. */
static inline void reg_update(volatile uint32_t *reg, uint32_t mask, uint32_t val)
{
    uint32_t v = *reg;                  /* exactly one read  */
    v = (v & ~mask) | (val & mask);
    *reg = v;                           /* exactly one write */
}

/* W1C status register: write ONLY the bit to clear. Never |= (that clears every pending bit). */
static inline void w1c_clear(volatile uint32_t *status, uint32_t bits) { *status = bits; }

/* ============================== C7. bit tricks ============================== */
unsigned popcount32(uint32_t x)
{
    unsigned n = 0;
    while (x) {
        x &= x - 1;                     /* clear lowest set bit */
        n++;
    }
    return n;
}

static inline bool is_pow2(uint32_t x)      { return x != 0 && (x & (x - 1)) == 0; }
static inline uint32_t lowest_bit(uint32_t x) { return x & (~x + 1u); }        /* x & -x */
static inline uint32_t align_up(uint32_t x, uint32_t a) { return (x + a - 1u) & ~(a - 1u); }  /* a = pow2 */

uint32_t next_pow2(uint32_t x)          /* 0 -> 1, 5 -> 8, 8 -> 8 (x <= 2^31) */
{
    if (x <= 1)
        return 1;
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return x + 1;
}

uint32_t bswap32(uint32_t x)
{
    return (x >> 24) | ((x >> 8) & 0x0000FF00u) | ((x << 8) & 0x00FF0000u) | (x << 24);
}

uint32_t reverse_bits32(uint32_t x)
{
    x = ((x >> 1) & 0x55555555u) | ((x & 0x55555555u) << 1);
    x = ((x >> 2) & 0x33333333u) | ((x & 0x33333333u) << 2);
    x = ((x >> 4) & 0x0F0F0F0Fu) | ((x & 0x0F0F0F0Fu) << 4);
    return bswap32(x);
}

/* ============================== C8. endianness / byte packing ============================== */
static inline bool is_little_endian(void)
{
    const uint16_t one = 1;
    return *(const uint8_t *)&one == 1;
}

/* Portable, alignment-safe, independent of host endianness. */
static inline uint32_t get_le32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static inline uint32_t get_be32(const uint8_t *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}
static inline void put_le32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}

/* ============================== C9. linked list reverse ============================== */
typedef struct node { int val; struct node *next; } node_t;

node_t *list_reverse(node_t *head)
{
    node_t *prev = NULL;
    while (head) {
        node_t *next = head->next;
        head->next = prev;
        prev = head;
        head = next;
    }
    return prev;
}

/* ============================== C10. aligned malloc ============================== */
/* align must be a power of 2. Stores the original pointer just below the returned block. */
void *aligned_malloc(size_t size, size_t align)
{
    void *raw = malloc(size + align - 1 + sizeof(void *));
    if (raw == NULL)
        return NULL;
    uintptr_t p = ((uintptr_t)raw + sizeof(void *) + align - 1) & ~(uintptr_t)(align - 1);
    ((void **)p)[-1] = raw;
    return (void *)p;
}

void aligned_free(void *p)
{
    if (p)
        free(((void **)p)[-1]);
}

/* ============================== tests ============================== */
int main(void)
{
    /* C1 */
    char b[16];
    CHECK(strcmp(my_strcpy(b, "rivian"), "rivian") == 0);
    CHECK(my_strcpy(b, "")[0] == '\0');
    char s4[4];
    CHECK(my_strlcpy(s4, "silicon", sizeof s4) == 7 && strcmp(s4, "sil") == 0);   /* truncated */
    CHECK(my_strlcpy(s4, "ab", sizeof s4) == 2 && strcmp(s4, "ab") == 0);
    CHECK(my_strlcpy(s4, "xyz", 0) == 3);
    CHECK(my_strlen("") == 0 && my_strlen("rap1") == 4);
    CHECK(my_strcmp("abc", "abc") == 0 && my_strcmp("abc", "abd") < 0 && my_strcmp("b", "a") > 0);
    CHECK(my_strcmp("ab", "abc") < 0 && my_strcmp("\xff", "a") > 0);          /* unsigned compare */
    char m[] = "123456789";
    my_memmove(m + 2, m, 5);            /* overlap, dst > src */
    CHECK(strcmp(m, "121234589") == 0);
    char m2[] = "123456789";
    my_memmove(m2, m2 + 2, 5);          /* overlap, dst < src */
    CHECK(strcmp(m2, "345676789") == 0);
    uint8_t mc[4];
    my_memcpy(mc, "\x01\x02\x03\x04", 4);
    CHECK(mc[0] == 1 && mc[3] == 4);

    /* C2 */
    char r1[] = "hello";  revstr(r1);  CHECK(strcmp(r1, "olleh") == 0);
    char r2[] = "ab";     revstr(r2);  CHECK(strcmp(r2, "ba") == 0);
    char r3[] = "";       revstr(r3);  CHECK(r3[0] == '\0');
    char r4[] = "x";      revstr(r4);  CHECK(strcmp(r4, "x") == 0);
    revstr(NULL);
    char w1[] = "hello big world";  rev_words(w1);  CHECK(strcmp(w1, "world big hello") == 0);
    char w2[] = "one";              rev_words(w2);  CHECK(strcmp(w2, "one") == 0);

    /* C3 */
    CHECK(my_atoi("42") == 42);
    CHECK(my_atoi("   -42") == -42);
    CHECK(my_atoi("+7") == 7);
    CHECK(my_atoi("4193 with words") == 4193);
    CHECK(my_atoi("words 987") == 0);
    CHECK(my_atoi("") == 0 && my_atoi("-") == 0);
    CHECK(my_atoi("2147483647") == INT_MAX);
    CHECK(my_atoi("2147483648") == INT_MAX);
    CHECK(my_atoi("-2147483648") == INT_MIN);
    CHECK(my_atoi("-91283472332") == INT_MIN);
    char ib[12];
    CHECK(strcmp(my_itoa(0, ib), "0") == 0);
    CHECK(strcmp(my_itoa(-305, ib), "-305") == 0);
    CHECK(strcmp(my_itoa(INT_MIN, ib), "-2147483648") == 0);
    CHECK(strcmp(my_itoa(INT_MAX, ib), "2147483647") == 0);
    uint32_t hv = 0;
    CHECK(parse_hex_u32("0x1F", &hv) && hv == 31u);
    CHECK(parse_hex_u32("DEADbeef", &hv) && hv == 0xDEADBEEFu);
    CHECK(!parse_hex_u32("0x", &hv) && !parse_hex_u32("12G", &hv) && !parse_hex_u32("123456789", &hv));

    /* C4: counter at 0x1_FFFFFFFF, ticks once per register read */
    sim_t = 0x1FFFFFFFFull;
    uint64_t bad = timer_read64_naive(sim_hi, sim_lo);      /* hi=1 then lo wraps to 0 */
    CHECK(bad == 0x100000000ull);                           /* ~4.29e9 ticks in the past! */
    sim_t = 0x1FFFFFFFFull;
    uint64_t t0 = sim_t;
    uint64_t good = timer_read64(sim_hi, sim_lo);
    CHECK(good == 0x200000003ull);                          /* retry: hi=2 lo=3 hi2=2 */
    CHECK(good >= t0 && good <= sim_t);
    ext64_t e = { 0, 0 };
    CHECK(ext64_update(&e, 0xFFFFFFF0u) == 0xFFFFFFF0ull);
    CHECK(ext64_update(&e, 0x00000010u) == 0x100000010ull); /* wrapped once */
    CHECK(time_after32(0x00000005u, 0xFFFFFFF0u));          /* after, across wrap */
    CHECK(!time_after32(0xFFFFFFF0u, 0x00000005u));
    CHECK((uint32_t)(0x00000005u - 0xFFFFFFF0u) == 21u);    /* elapsed across wrap */

    /* C5 */
    ring_t rb = { .head = 0, .tail = 0 };
    uint8_t out = 0;
    CHECK(!rb_get(&rb, &out));
    for (uint8_t i = 0; i < RB_SIZE; i++)
        CHECK(rb_put(&rb, i));
    CHECK(!rb_put(&rb, 99));                                /* full */
    for (uint8_t i = 0; i < RB_SIZE; i++)
        CHECK(rb_get(&rb, &out) && out == i);               /* FIFO order */
    CHECK(!rb_get(&rb, &out));
    rb.head = rb.tail = 0xFFFFFFFCu;                        /* index wrap at 2^32 */
    for (uint8_t i = 0; i < 6; i++)
        CHECK(rb_put(&rb, (uint8_t)(10 + i)));
    CHECK(rb_count(&rb) == 6 && rb.head == 2u);
    for (uint8_t i = 0; i < 6; i++)
        CHECK(rb_get(&rb, &out) && out == 10 + i);
    aring_t ar;
    atomic_init(&ar.head, 0);
    atomic_init(&ar.tail, 0);
    for (uint8_t i = 0; i < RB_SIZE; i++)
        CHECK(arb_put(&ar, i));
    CHECK(!arb_put(&ar, 1));
    CHECK(arb_get(&ar, &out) && out == 0);

    /* C6 */
    CHECK(GENMASK(7, 4) == 0xF0u && GENMASK(31, 0) == 0xFFFFFFFFu && GENMASK(0, 0) == 1u);
    CHECK(FIELD_GET(GENMASK(7, 4), 0xA5u) == 0xAu);
    CHECK(FIELD_PREP(GENMASK(7, 4), 0x3u) == 0x30u);
    CHECK(FIELD_PREP(GENMASK(7, 4), 0x13u) == 0x30u);       /* value too wide -> masked */
    CHECK(get_field(0x12345678u, 8, 8) == 0x56u);
    CHECK(set_field(0x12345678u, 8, 8, 0xABu) == 0x1234AB78u);
    CHECK(get_field(0xCAFEF00Du, 0, 32) == 0xCAFEF00Du);    /* width 32 no UB */
    volatile uint32_t ctrl = 0xFFFF0000u;
    reg_update(&ctrl, GENMASK(3, 1), FIELD_PREP(GENMASK(3, 1), 5u));
    CHECK(ctrl == 0xFFFF000Au);
    uint32_t x = 0;
    x |= BIT(3);   CHECK(x == 8u);
    x ^= BIT(0);   CHECK(x == 9u);
    x &= ~BIT(3);  CHECK(x == 1u);
    CHECK(!!(x & BIT(0)) == 1);
    volatile uint32_t st = 0;
    w1c_clear(&st, BIT(2));
    CHECK(st == BIT(2));                                    /* only bit 2 written */

    /* C7 */
    CHECK(popcount32(0) == 0 && popcount32(0xF0F0u) == 8 && popcount32(0xFFFFFFFFu) == 32);
    CHECK(is_pow2(1) && is_pow2(64) && !is_pow2(0) && !is_pow2(96));
    CHECK(lowest_bit(0x68u) == 0x8u);
    CHECK(align_up(13, 8) == 16 && align_up(16, 8) == 16 && align_up(0, 4) == 0);
    CHECK(next_pow2(0) == 1 && next_pow2(5) == 8 && next_pow2(8) == 8 && next_pow2(1000) == 1024);
    CHECK(bswap32(0x12345678u) == 0x78563412u);
    CHECK(reverse_bits32(0x00000001u) == 0x80000000u && reverse_bits32(0x0000000Fu) == 0xF0000000u);

    /* C8 */
    const uint8_t raw[4] = { 0x78, 0x56, 0x34, 0x12 };
    CHECK(get_le32(raw) == 0x12345678u && get_be32(raw) == 0x78563412u);
    uint8_t pk[4];
    put_le32(pk, 0xAABBCCDDu);
    CHECK(pk[0] == 0xDD && pk[3] == 0xAA);
    printf("host is %s-endian\n", is_little_endian() ? "little" : "big");

    /* C9 */
    node_t n3 = { 3, NULL }, n2 = { 2, &n3 }, n1 = { 1, &n2 };
    node_t *h = list_reverse(&n1);
    CHECK(h == &n3 && h->next == &n2 && h->next->next == &n1 && n1.next == NULL);
    CHECK(list_reverse(NULL) == NULL);

    /* C10 */
    void *ap = aligned_malloc(100, 64);
    CHECK(ap != NULL && ((uintptr_t)ap & 63u) == 0);
    memset(ap, 0xA5, 100);
    aligned_free(ap);

    if (fails == 0)
        printf("ALL PASS\n");
    else
        printf("%d FAIL\n", fails);
    return fails != 0;
}
