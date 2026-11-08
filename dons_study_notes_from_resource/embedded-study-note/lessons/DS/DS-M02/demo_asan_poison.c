/* DS-M02 데모: 풀 + ASan 수동 poisoning
 * 빌드 (ASan 켬):  cc -std=c11 -g -O1 -fsanitize=address demo_asan_poison.c -o demo_asan
 * 빌드 (ASan 끔):  cc -std=c11 -g -O1 demo_asan_poison.c -o demo_plain
 * 실행: ./demo_asan ok | write | read
 *
 * 풀 메모리는 정적 배열이라 ASan 은 원래 그 안을 전부 "유효"로 본다 → free 된 블록을 건드려도 조용함.
 * 풀이 free 할 때 블록을 poison, alloc 할 때 unpoison 하면 ASan 이 블록 단위 use-after-free 를 잡는다.
 * ASan 없이 빌드하면 매크로가 ((void)(addr), (void)(size)) 로 바뀌어 비용 0.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sanitizer/asan_interface.h>

#define STRIDE 32u                       /* 8 의 배수: ASan 섀도 1바이트 = 앱 8바이트 */
#define NBLK   8u
static _Alignas(16) uint8_t g_mem[STRIDE * NBLK];
static void *g_head;

static void *blk_next(const void *b) { void *n; memcpy(&n, b, sizeof n); return n; }
static void blk_set_next(void *b, void *n) { memcpy(b, &n, sizeof n); }
static int owns(const void *b) {
    uintptr_t a = (uintptr_t)b, b0 = (uintptr_t)g_mem;
    return a >= b0 && a - b0 < sizeof g_mem && (a - b0) % STRIDE == 0;
}

static void pool_init(void) {
    for (unsigned i = NBLK; i-- > 0;) {
        uint8_t *b = g_mem + i * STRIDE;
        blk_set_next(b, g_head);
        g_head = b;
    }
    ASAN_POISON_MEMORY_REGION(g_mem, sizeof g_mem);   /* 처음엔 전부 free = 전부 poison */
}
static void *pool_alloc(void) {
    uint8_t *b = g_head;
    if (b == NULL) return NULL;
    ASAN_UNPOISON_MEMORY_REGION(b, STRIDE);         /* next 를 읽기 전에 먼저 풀어야 한다 */
    void *nx = blk_next(b);
    if (nx != NULL && !owns(nx)) {                  /* 디버그 검사: 오염된 free list */
        printf("  pool_alloc: free list corrupted, next=%p (not a pool block)\n", nx);
        g_head = NULL;
        return NULL;
    }
    g_head = nx;
    return b;
}
static void pool_free(void *b) {
    blk_set_next(b, g_head);                        /* 아직 unpoison 상태에서 next 기록 */
    g_head = b;
    ASAN_POISON_MEMORY_REGION(b, STRIDE);           /* 이후 사용자 접근은 ASan 이 잡는다 */
}

typedef struct { uint32_t id; uint32_t value; uint8_t pad[24]; } msg_t;   /* 32 B */

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "ok";
    pool_init();
    msg_t *m = pool_alloc();
    m->id = 7; m->value = 1234;
    printf("alloc m=%p id=%u value=%u\n", (void *)m, m->id, m->value);
    pool_free(m);
    if (strcmp(mode, "write") == 0) {
        memset(m, 0x41, 16);                        /* 버그: free 후 쓰기 — next 포인터를 덮는다 */
        void *x = pool_alloc(), *y = pool_alloc();
        printf("after UAF write: x=%p y=%p\n", x, y);
    } else if (strcmp(mode, "read") == 0) {
        printf("after free: id=%u value=%u\n", m->id, m->value);   /* 버그: free 후 읽기 */
    } else {
        msg_t *n = pool_alloc();
        printf("ok: realloc n=%p (same block: %s)\n", (void *)n, n == m ? "yes" : "no");
    }
    return 0;
}
