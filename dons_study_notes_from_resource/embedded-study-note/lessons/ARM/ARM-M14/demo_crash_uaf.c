/* ARM-M14 demo 3 — 해제 후 사용(use-after-free): MallocScribble 이 해제된 메모리를 0x55 로 채운다
 *   cc -std=c11 -O1 -g -fno-stack-protector demo_crash_uaf.c -o /tmp/ARM-M14_uaf
 *   MallocScribble=1 /tmp/ARM-M14_uaf          (lldb 안에서는 env 설정: demo_crash_uaf.lldb)
 *   lldb -b -s demo_crash_uaf.lldb /tmp/ARM-M14_uaf > crash_uaf.txt
 * DMA 요청 객체를 완료 콜백에서 free 했는데, 큐에 남은 포인터로 다시 complete() 를 부른다.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct dma_ops {
    void (*complete)(uint32_t status);
};

struct dma_req {                 /* offset */
    uint64_t src;                /* 0x00 */
    uint64_t dst;                /* 0x08 */
    uint32_t len;                /* 0x10 */
    uint32_t status;             /* 0x14 */
    const struct dma_ops *ops;   /* 0x18 */
    uint64_t cookie;             /* 0x20 */
};

static uint32_t g_done;
static void on_done(uint32_t st) { g_done += st; }
static const struct dma_ops g_ops = {on_done};

static struct dma_req *g_queue[4];               /* "완료 대기" 큐 — free 후에도 포인터가 남는다 */

__attribute__((noinline))
void finish_req(struct dma_req *r) {
    r->ops->complete(r->status);                  /* ← 해제된 r 이면 ops = 0x5555... */
}

int main(int argc, char **argv) {
    (void)argv;
    struct dma_req *r = malloc(sizeof *r);
    r->src = 0x20000000; r->dst = 0x20001000; r->len = 64; r->status = 1;
    r->ops = &g_ops; r->cookie = 0xC0FFEE;
    g_queue[0] = r;
    finish_req(g_queue[0]);                       /* 정상 완료 */
    free(r);                                      /* 요청 해제 — 그런데 g_queue[0] 를 안 지움 */
    if (argc > 1) finish_req(g_queue[0]);         /* 인자가 있으면 해제된 객체 재사용 */
    printf("done=%u\n", g_done);
    return 0;
}
