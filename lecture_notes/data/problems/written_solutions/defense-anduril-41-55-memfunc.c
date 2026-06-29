#include <stddef.h>
#include <stdio.h>

/*
41. void* my_memset(void *s, int c, size_t n)
    Example: my_memset(buf, 0xAA, 4) -> buf = {0xAA, 0xAA, 0xAA, 0xAA}
*/
void* my_memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    unsigned char v = (unsigned char)c;
    for (size_t i = 0; i < n; ++i) {
        p[i] = v;
    }
    return s;
}

/*
42. void* my_memcpy(void *dest, const void *src, size_t n)
    Example: my_memcpy(dest, src, 4) -> dest = src[0..3]
*/
void* my_memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dest;
}

/*
43. void* my_memmove(void *dest, const void *src, size_t n) (safe for overlap)
    Example: my_memmove(dest, src, 4) -> dest = src[0..3]
*/
void* my_memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) {
        return dest;
    }
    if (d < s) {
        /* forward copy is safe */
        for (size_t i = 0; i < n; ++i) {
            d[i] = s[i];
        }
    } else {
        /* dest after src: copy backward to avoid clobbering */
        for (size_t i = n; i > 0; --i) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

/*
44. int my_memcmp(const void *s1, const void *s2, size_t n)
    Example: my_memcmp("abc", "abd", 3) -> negative value
*/
int my_memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *a = (const unsigned char *)s1;
    const unsigned char *b = (const unsigned char *)s2;
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            return (int)a[i] - (int)b[i];
        }
    }
    return 0;
}

/*
45. void* my_memchr(const void *s, int c, size_t n)
    Example: my_memchr("abc", 'b', 3) -> pointer to 'b'
*/
void* my_memchr(const void *s, int c, size_t n) {
    const unsigned char *p = (const unsigned char *)s;
    unsigned char v = (unsigned char)c;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] == v) {
            return (void *)(p + i);
        }
    }
    return NULL;
}

/*
46. Swap two integers using pointers
    Example: swap_int(&a, &b) -> a, b values swapped
*/
void swap_int(int *a, int *b) {
    int tmp = *a;
    *a = *b;
    *b = tmp;
}

/*
47. Reverse a string in place using pointers
    Example: reverse_string("hello") -> "olleh"
*/
void reverse_string(char *s) {
    if (s == NULL) {
        return;
    }
    size_t len = 0;
    while (s[len] != '\0') {
        ++len;
    }
    if (len == 0) {
        return;
    }
    char *left = s;
    char *right = s + len - 1;
    while (left < right) {
        char tmp = *left;
        *left = *right;
        *right = tmp;
        ++left;
        --right;
    }
}

/*
48. const int *p, int * const p, const int * const p (CONCEPT)
    const int *p        : value pointed to is read-only; pointer may be reassigned.
    int * const p       : pointer is fixed; pointed-to value is writable.
    const int * const p : both pointer and pointed-to value are read-only.
*/

/*
49. volatile keyword (CONCEPT)
    Tells the compiler the variable may change unexpectedly (HW registers, ISRs,
    multithreaded access) so it must not optimize away reads/writes.
*/

/*
50. Dispatch table using an array of function pointers
    Example: dispatch_table_example() calls functions by index
*/
typedef void (*func_ptr_t)(void);

static void dispatch_op0(void) { printf("op0\n"); }
static void dispatch_op1(void) { printf("op1\n"); }
static void dispatch_op2(void) { printf("op2\n"); }

void dispatch_table_example(void) {
    func_ptr_t table[] = { dispatch_op0, dispatch_op1, dispatch_op2 };
    size_t count = sizeof(table) / sizeof(table[0]);
    printf("dispatch_table_example(): ");
    for (size_t i = 0; i < count; ++i) {
        table[i]();
    }
}

/*
51. Simple malloc/free over a static memory pool
    Example: static_malloc(16), static_free(ptr)

    Minimal bump-style allocator with a free list of fixed-size blocks.
    Each block carries a header so static_free can return it to the pool.
*/
#define STATIC_POOL_SIZE 1024

typedef struct block_header {
    size_t size;            /* usable payload size */
    int    in_use;          /* 1 if allocated */
    struct block_header *next;
} block_header_t;

static unsigned char static_pool[STATIC_POOL_SIZE];
static block_header_t *static_free_list = NULL;
static int static_pool_initialized = 0;

static void static_pool_init(void) {
    static_free_list = (block_header_t *)static_pool;
    static_free_list->size = STATIC_POOL_SIZE - sizeof(block_header_t);
    static_free_list->in_use = 0;
    static_free_list->next = NULL;
    static_pool_initialized = 1;
}

void* static_malloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    if (!static_pool_initialized) {
        static_pool_init();
    }
    /* align request to pointer size */
    size_t align = sizeof(void *);
    size = (size + align - 1) & ~(align - 1);

    block_header_t *cur = static_free_list;
    while (cur != NULL) {
        if (!cur->in_use && cur->size >= size) {
            /* split if there is room for another header + minimal payload */
            if (cur->size >= size + sizeof(block_header_t) + align) {
                block_header_t *rest =
                    (block_header_t *)((unsigned char *)(cur + 1) + size);
                rest->size = cur->size - size - sizeof(block_header_t);
                rest->in_use = 0;
                rest->next = cur->next;
                cur->next = rest;
                cur->size = size;
            }
            cur->in_use = 1;
            return (void *)(cur + 1);
        }
        cur = cur->next;
    }
    return NULL;
}

void static_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    block_header_t *hdr = ((block_header_t *)ptr) - 1;
    hdr->in_use = 0;
    /* coalesce adjacent free blocks */
    block_header_t *cur = static_free_list;
    while (cur != NULL && cur->next != NULL) {
        if (!cur->in_use && !cur->next->in_use) {
            cur->size += sizeof(block_header_t) + cur->next->size;
            cur->next = cur->next->next;
        } else {
            cur = cur->next;
        }
    }
}

/*
52. Dangling pointer (CONCEPT)
    A pointer that references memory already freed or no longer valid, e.g.
    using a pointer after free() or returning the address of a local variable.
*/

/*
53. void* pointer usage (CONCEPT)
    A generic pointer that can hold the address of any object type. It must be
    cast back to the correct type before dereferencing; type info is not retained.
*/

/*
54. Struct padding and alignment (CONCEPT)
    Padding bytes are inserted so each member sits on its required alignment
    boundary, satisfying hardware access requirements and improving efficiency.
*/

/*
55. Array of pointers vs pointer to array (CONCEPT)
    Array of pointers: int *arr[10]   -> 10 pointers.
    Pointer to array : int (*arr)[10] -> one pointer to an array of 10 ints.
*/
