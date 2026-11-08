/* C-A06 관찰: container_of 는 실제로 어떤 명령어가 되나 (freestanding — libc 없음)
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O2 -S demo_m4.c -o -
 *   cc -std=c11 -O2 -S demo_m4.c -o -                                        # 호스트 arm64
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O0 -c demo_m4.c -o /tmp/C-A06_m4.o
 *   xcrun llvm-size -A /tmp/C-A06_m4.o                                       # -O0 에서 arena 가 .bss 에 남나?
 */
#include <stddef.h>
#include <stdint.h>

#define MY_OFFSETOF_ARENA_SIZE  1024u
#define MY_OFFSETOF_ARENA_ALIGN 64
static _Alignas(MY_OFFSETOF_ARENA_ALIGN) unsigned char my_offsetof_arena_[MY_OFFSETOF_ARENA_SIZE];
#define MY_OFFSETOF(type, member)                                                              \
    (sizeof(char[(sizeof(type) <= MY_OFFSETOF_ARENA_SIZE &&                                    \
                  _Alignof(type) <= MY_OFFSETOF_ARENA_ALIGN) ? 1 : -1]) - 1u +                 \
     (size_t)((unsigned char *)&((type *)(void *)my_offsetof_arena_)->member - my_offsetof_arena_))
#define container_of(ptr, type, member)                                                        \
    ((void)sizeof((ptr) == &((type *)0)->member),                                              \
     (type *)(void *)((char *)(ptr) - MY_OFFSETOF(type, member)))
static inline void *cof_sub_or_null_(void *p, size_t off) {
    return p ? (void *)((char *)p - off) : NULL;
}
#define container_of_or_null(ptr, type, member)                                                \
    ((void)sizeof((ptr) == &((type *)0)->member),                                              \
     (type *)cof_sub_or_null_((ptr), MY_OFFSETOF(type, member)))

/* solution.c Part C 와 같은 구조체 */
struct dma_desc { uint32_t src; uint32_t len; uint32_t ctrl; struct dma_desc *next; };
struct k_work { struct k_work *next; void (*handler)(struct k_work *w); _Bool pending; };
struct tx_req { uint8_t idx; uint8_t state; uint16_t nbytes; uint32_t cookie; struct dma_desc desc; };
struct uart_dev {
    const char *name; uint32_t base, tx_done, bh_runs, n_done, done_cookie[8];
    struct tx_req tx[4];
    struct k_work tx_work;
};

#if UINTPTR_MAX == 0xFFFFFFFFu        /* Cortex-M (포인터 4 B) */
_Static_assert(offsetof(struct tx_req, desc) == 8 && sizeof(struct tx_req) == 24, "M4 tx_req");
_Static_assert(offsetof(struct uart_dev, tx) == 52 && offsetof(struct uart_dev, tx_work) == 148, "M4 uart_dev");
#else                                 /* arm64 (포인터 8 B) */
_Static_assert(offsetof(struct tx_req, desc) == 8 && sizeof(struct tx_req) == 32, "arm64 tx_req");
_Static_assert(offsetof(struct uart_dev, tx) == 56 && offsetof(struct uart_dev, tx_work) == 184, "arm64 uart_dev");
#endif

struct uart_dev *uart_from_work(struct k_work *w) { return container_of(w, struct uart_dev, tx_work); }
struct tx_req *req_from_desc(struct dma_desc *d) { return container_of_or_null(d, struct tx_req, desc); }
struct uart_dev *uart_from_req(struct tx_req *r) { return container_of(r - r->idx, struct uart_dev, tx[0]); }
struct uart_dev *uart_from_req_idx(struct tx_req *r) { return container_of(r, struct uart_dev, tx[r->idx]); }
