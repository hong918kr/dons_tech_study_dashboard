// C17_freelist_alloc.c — 원문 17.2절의 "free list 를 free 공간 안에 심는" 작은 할당기
// build: cc -Wall -Wextra -O0 code/C17_freelist_alloc.c -o .work/bin/C17_freelist_alloc
//
// 원문 그림(32-bit 가정)과 숫자를 그대로 맞추려고 헤더를 8바이트로 고정한다:
//   할당된 청크 헤더 : { uint32 size; uint32 magic; }
//   free 노드 헤더   : { uint32 size; uint32 next;  }   next = 힙 시작 기준 오프셋 (NIL = 없음)
// 주소는 "가상 주소 16KB(16384) 에 힙이 있다"고 가정해 16384 + offset 으로 출력한다.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>

#define HEAP_SIZE 4096u
#define VBASE     16384u                 // 원문 그림의 가상 주소 16KB
#define MAGIC     1234567u
#define NIL       0xFFFFFFFFu
#define HDR       8u

typedef struct { uint32_t size, magic; } header_t;
typedef struct { uint32_t size, next; } node_t;

typedef enum { FIRST, BEST, WORST } policy_t;
static const char *pname[] = {"FIRST", "BEST", "WORST"};

static uint8_t *heap;                    // mmap 으로 받은 4KB
static uint32_t head;                    // free list 머리 (오프셋)
static policy_t policy = FIRST;
static int coalesce = 0;                 // 0: 머리에 끼워넣기(원문 그림), 1: 주소순 + 병합
static int quiet = 0;                    // 1 이면 malloc/free 로그 생략 (준비 단계용)

#define NODE(off) ((node_t *)(heap + (off)))
#define VA(off)   ((off) == NIL ? 0u : VBASE + (off))

static void heap_init(void) {
    if (!heap) {
        heap = mmap(NULL, HEAP_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
        assert(heap != MAP_FAILED);
    }
    memset(heap, 0, HEAP_SIZE);
    head = 0;
    NODE(0)->size = HEAP_SIZE - sizeof(node_t);   // 4088
    NODE(0)->next = NIL;
}

static void dump(const char *tag) {
    printf("  %-26s free list:", tag);
    int n = 0;
    for (uint32_t c = head; c != NIL; c = NODE(c)->next, n++)
        printf(" [@%u size:%u next:%u]", VA(c), NODE(c)->size, VA(NODE(c)->next));
    if (!n) printf(" (empty)");
    printf("\n");
}

// 정책에 따라 청크를 고르고 분할(split)한다. 반환값: 사용자 포인터의 오프셋 (실패 NIL)
static uint32_t my_malloc(uint32_t n) {
    uint32_t need = n + HDR;                       // 원문: N 이 아니라 N + 헤더를 찾는다
    uint32_t best = NIL, best_prev = NIL, prev = NIL;
    int searched = 0;
    for (uint32_t c = head; c != NIL; prev = c, c = NODE(c)->next) {
        searched++;
        // 사용 가능 바이트 = 노드 헤더 뒤 size + 헤더 8바이트(재사용) 이므로 size >= n 이면 들어간다
        if (NODE(c)->size < n) continue;
        int take = best == NIL
            || (policy == BEST  && NODE(c)->size < NODE(best)->size)
            || (policy == WORST && NODE(c)->size > NODE(best)->size);
        if (take) { best = c; best_prev = prev; }
        if (policy == FIRST) break;                  // 첫 번째로 맞는 것에서 멈춘다
    }
    if (best == NIL) { if (!quiet) printf("  malloc(%u) -> NULL (searched %d)\n", n, searched); return NIL; }

    node_t *b = NODE(best);
    uint32_t next = b->next, granted = n;
    uint32_t replacement;                            // free list 에서 best 자리를 대신할 것
    if (b->size >= n + HDR + 1) {                    // split: 남는 쪽에 새 노드를 만들 공간이 있다
        uint32_t rest = best + need;
        NODE(rest)->size = b->size - need;
        NODE(rest)->next = next;
        replacement = rest;
    } else {                                         // 너무 작게 남으면 통째로 준다 (내부 단편화)
        granted = b->size;
        replacement = next;
    }
    if (best_prev == NIL) head = replacement; else NODE(best_prev)->next = replacement;

    header_t *h = (header_t *)(heap + best);
    h->size = granted; h->magic = MAGIC;
    if (!quiet) printf("  malloc(%u) -> %u  [%s, searched %d]\n", n, VA(best + HDR), pname[policy], searched);
    return best + HDR;
}

static void my_free(uint32_t ptr) {
    uint32_t hoff = ptr - HDR;                      // 원문: hptr = (void *)ptr - sizeof(header_t)
    header_t *h = (header_t *)(heap + hoff);
    assert(h->magic == MAGIC);                       // 무결성 검사
    uint32_t size = h->size;
    node_t *nd = NODE(hoff);
    nd->size = size;
    if (!quiet) printf("  free(%u)  (header @%u, size %u)\n", VA(ptr), VA(hoff), size);
    if (!coalesce) {                                 // 원문 그림 17.6/17.7: 그냥 머리에 넣기
        nd->next = head; head = hoff;
        return;
    }
    // 주소순 삽입
    uint32_t prev = NIL, c = head;
    while (c != NIL && c < hoff) { prev = c; c = NODE(c)->next; }
    nd->next = c;
    if (prev == NIL) head = hoff; else NODE(prev)->next = hoff;
    // 뒤 이웃과 병합: 내 끝 == 다음 노드 시작
    if (c != NIL && hoff + HDR + nd->size == c) {
        nd->size += HDR + NODE(c)->size;
        nd->next = NODE(c)->next;
    }
    // 앞 이웃과 병합
    if (prev != NIL && prev + HDR + NODE(prev)->size == hoff) {
        NODE(prev)->size += HDR + nd->size;
        NODE(prev)->next = nd->next;
    }
}

int main(void) {
    printf("[1] 원문 Figure 17.3~17.7 재현 (coalescing 없음, 머리에 삽입)\n");
    heap_init(); dump("init (Fig 17.3)");
    uint32_t a = my_malloc(100); dump("after 1 alloc (Fig 17.4)");
    uint32_t b = my_malloc(100);
    uint32_t c = my_malloc(100); dump("after 3 allocs (Fig 17.5)");
    my_free(b); dump("free middle (Fig 17.6)");
    my_free(a); my_free(c); dump("free all (Fig 17.7)");
    uint32_t big = my_malloc(300);                   // 메모리는 전부 비었는데...
    printf("  -> 300B 요청은 %s (4개 조각에 흩어져 있어도 3764 짜리가 있어 성공)\n", big == NIL ? "실패" : "성공");
    my_malloc(3800);                                 // 3764 보다 크면 실패: 단편화

    printf("\n[2] 같은 순서, 주소순 정렬 + coalescing\n");
    coalesce = 1; heap_init();
    a = my_malloc(100); b = my_malloc(100); c = my_malloc(100);
    my_free(b); dump("free middle");
    my_free(a); dump("free first (merge 1)");
    my_free(c); dump("free last (merge all)");
    my_malloc(3800); dump("3800B 요청");

    printf("\n[3] 정책 비교: free 청크 100, 300, 200 에 150B 요청 (원문 10/30/20 + 15 를 x10)\n");
    for (int p = FIRST; p <= WORST; p++) {
        policy = FIRST; coalesce = 1; quiet = 1; heap_init();
        uint32_t x1 = my_malloc(100), y1 = my_malloc(8);
        uint32_t x2 = my_malloc(300), y2 = my_malloc(8);
        uint32_t x3 = my_malloc(200);
        my_malloc(NODE(head)->size);                 // 나머지는 통째로 소진시켜 tail 을 없앤다
        my_free(x1); my_free(x2); my_free(x3);
        (void)y1; (void)y2;
        policy = (policy_t)p; quiet = 0;
        printf("  --- policy %s\n", pname[p]);
        dump("before");
        my_malloc(150);
        dump("after");
    }
    munmap(heap, HEAP_SIZE);
    return 0;
}
