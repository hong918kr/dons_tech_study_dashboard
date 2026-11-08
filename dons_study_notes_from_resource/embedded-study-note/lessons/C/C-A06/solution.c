/* C-A06 offsetof와 container_of 구현
 *
 * Part A: MY_OFFSETOF(type, member) — <stddef.h> 의 offsetof 를 쓰지 않는다 (UB 없이, UBSan 깨끗하게)
 *         container_of(ptr, type, member) — 타입 검사 포함, 실행 비용 0
 * Part B: container_of_const (const 보존, _Generic), container_of_or_null (NULL 안전, ptr 1회 평가),
 *         배열 원소 멤버(ch[i]) 에서 바깥 구조체 찾기
 * Part C: DMA 완료 ISR + 워크큐 bottom half — 하드웨어가 준 디스크립터 주소 하나로
 *         desc → tx_req → uart_dev, 그리고 k_work → uart_dev 를 복원한다.
 *
 * 심판: 테스트는 <stddef.h> offsetof 와 실제 객체 주소(&obj)로 판정한다 — 피고 매크로에 의존하지 않는다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter   (solution.c 도 같은 방법)
 */
#include <stdbool.h>
#include <stddef.h>   /* size_t, NULL, offsetof(심판 전용 — 구현에는 쓰지 않는다) */
#include <stdint.h>
#include <stdio.h>

/* ======================= Part A: MY_OFFSETOF, container_of ======================= */

/* 멤버 오프셋(바이트). 요구사항:
 *  - offsetof 금지, 널 포인터를 역참조하는 고전 형태 ((size_t)&((type *)0)->member) 도 금지 (UBSan 이 잡는다)
 *  - member 는 멤버 지정자: b, in.y, arr[3], arr[i](런타임 i) 모두 된다
 *  - 결과 타입 size_t. 정수 상수식(ICE)일 필요는 없다 (_Static_assert 에는 표준 offsetof 를 쓴다)
 */
#define MY_OFFSETOF_ARENA_SIZE  1024u
#define MY_OFFSETOF_ARENA_ALIGN 64
/* 기준점: 정렬·크기가 충분한 "실제로 존재하는" 정적 저장소. 읽지도 쓰지도 않고 주소 계산에만 쓴다. */
static _Alignas(MY_OFFSETOF_ARENA_ALIGN) unsigned char my_offsetof_arena_[MY_OFFSETOF_ARENA_SIZE];
#define MY_OFFSETOF(type, member)                                                              \
    (sizeof(char[(sizeof(type) <= MY_OFFSETOF_ARENA_SIZE &&                                    \
                  _Alignof(type) <= MY_OFFSETOF_ARENA_ALIGN) ? 1 : -1]) - 1u +                 \
     (size_t)((unsigned char *)&((type *)(void *)my_offsetof_arena_)->member - my_offsetof_arena_))

/* ptr 이 type 객체 안 member 를 가리킬 때, 그 type 객체의 주소.
 * 타입 검사: ptr 과 &((type *)0)->member 가 다른 포인터 타입이면 컴파일 진단 (실행 코드 0, ptr 평가 1회). */
#define container_of(ptr, type, member)                                                        \
    ((void)sizeof((ptr) == &((type *)0)->member),                                              \
     (type *)(void *)((char *)(ptr) - MY_OFFSETOF(type, member)))

/* ======================= Part B: const 보존, NULL 안전, 배열 원소 ======================= */

/* p 가 const 를 가리키면 const void *, 아니면 void * 타입이 되는 식 (평가되지 않는 곳에서만 쓴다).
 * 한쪽이 void * (널 포인터 상수가 아님) 인 조건 연산자 → 결과는 양쪽 한정자를 합친 void * (6.5.15p6) */
#define QUAL_VOID_PTR_(p) (1 ? (p) : (void *)(p))

/* const 포인터를 넣으면 const type *, 아니면 type * 를 돌려준다 */
#define container_of_const(ptr, type, member)                                                  \
    _Generic(QUAL_VOID_PTR_(ptr),                                                              \
        const void *: ((void)sizeof((ptr) == &((type *)0)->member),                            \
                       (const type *)(const void *)((const char *)(ptr) - MY_OFFSETOF(type, member))), \
        default: container_of(ptr, type, member))

/* NULL 이면 NULL, 아니면 p - off. (매크로 인자 ptr 을 한 번만 평가하려고 함수로 뺐다) */
static inline void *cof_sub_or_null_(void *p, size_t off) {
    return p ? (void *)((char *)p - off) : NULL;
}
#define container_of_or_null(ptr, type, member)                                                \
    ((void)sizeof((ptr) == &((type *)0)->member),                                              \
     (type *)cof_sub_or_null_((ptr), MY_OFFSETOF(type, member)))

/* DMA 컨트롤러: 채널 구조체가 배열 멤버로 들어 있다. 채널 콜백은 struct dma_chan * 만 받는다. */
struct dma_chan { uint8_t idx; uint8_t busy; uint32_t xfers; };
struct dma_ctrl { uint32_t base; uint32_t irq_mask; struct dma_chan ch[4]; };

/* c 가 어떤 ctrl 의 ch[c->idx] 일 때 그 ctrl. 방법 1: c - c->idx == &ch[0] 으로 옮긴 뒤 ch[0] 기준 */
static struct dma_ctrl *chan_to_ctrl(struct dma_chan *c) {
    return container_of(c - c->idx, struct dma_ctrl, ch[0]);
}
/* 방법 2: 런타임 인덱스를 멤버 지정자에 그대로 (MY_OFFSETOF 는 런타임 i 도 된다) */
static struct dma_ctrl *chan_to_ctrl_idx(struct dma_chan *c) {
    return container_of(c, struct dma_ctrl, ch[c->idx]);
}

/* ======================= Part C: DMA 완료 ISR + 워크큐 ======================= */

/* 하드웨어가 읽는 DMA 디스크립터 (링크드 리스트 DMA). 완료되면 엔진이 그 주소를 CURR_DESC 에 남긴다. */
struct dma_desc { uint32_t src; uint32_t len; uint32_t ctrl; struct dma_desc *next; };
#define DESC_OWN 1u                                  /* 1 = 하드웨어 소유 */
/* 가짜 DMA 레지스터. 실제 MCU 에선 32비트 주소 레지스터(uint32_t) — 64비트 호스트라 uintptr_t */
typedef struct { volatile uintptr_t CURR_DESC; volatile uint32_t IFCR; } DMA_TypeDef;
static DMA_TypeDef g_dma;

/* Zephyr k_work 와 같은 모양의 워크 아이템: 큐는 handler(&work) 만 부른다 — 주인 타입을 모른다 */
struct k_work { struct k_work *next; void (*handler)(struct k_work *w); bool pending; };

enum { REQ_FREE, REQ_BUSY, REQ_DONE };
#define NREQ 4
struct tx_req {                                      /* 드라이버의 송신 요청 1개 */
    uint8_t  idx;                                    /* uart_dev.tx[] 안 위치 */
    uint8_t  state;
    uint16_t nbytes;
    uint32_t cookie;                                 /* 호출자가 준 식별자 */
    struct dma_desc desc;                            /* 첫 멤버가 아니다 → 캐스트로는 못 돌아간다 */
};
struct uart_dev {
    const char *name;
    uint32_t base;
    uint32_t tx_done;                                /* ISR 이 센 완료 수 */
    uint32_t bh_runs;                                /* bottom half 실행 수 */
    uint32_t n_done;                                 /* bottom half 가 회수한 요청 수 */
    uint32_t done_cookie[8];
    struct tx_req tx[NREQ];
    struct k_work tx_work;
};
static struct uart_dev g_uart[2];
static uint32_t g_spurious;                          /* 주인을 못 찾은 이벤트 */

/* 하드웨어가 준 디스크립터 주소 → 그것을 품은 tx_req. d == NULL (가짜 인터럽트) 이면 NULL */
static struct tx_req *req_from_desc(struct dma_desc *d) {
    return container_of_or_null(d, struct tx_req, desc);
}
/* tx_req → 그것을 tx[] 배열에 품은 uart_dev */
static struct uart_dev *uart_from_req(struct tx_req *r) {
    return container_of(r - r->idx, struct uart_dev, tx[0]);
}
/* 워크큐가 넘겨준 k_work → uart_dev */
static struct uart_dev *uart_from_work(struct k_work *w) {
    return container_of(w, struct uart_dev, tx_work);
}

/* ---------------- 주어진 코드: 워크큐, 드라이버, ISR, 하드웨어 시뮬레이터 ---------------- */
static struct k_work *g_wq_head, *g_wq_tail;
static void work_init(struct k_work *w, void (*h)(struct k_work *)) {
    w->next = NULL;
    w->handler = h;
    w->pending = false;
}
/* ISR 에서 부른다. 실제 칩에선 큐 조작을 임계 구역으로 감싸야 한다 (C-M14). 대기 중이면 합친다. */
static void work_submit(struct k_work *w) {
    if (w->pending) return;
    w->pending = true;
    w->next = NULL;
    if (g_wq_tail) g_wq_tail->next = w; else g_wq_head = w;
    g_wq_tail = w;
}
static int work_run_all(void) {                      /* main 루프 / 워크큐 스레드 */
    int n = 0;
    while (g_wq_head) {
        struct k_work *w = g_wq_head;
        g_wq_head = w->next;
        if (!g_wq_head) g_wq_tail = NULL;
        w->pending = false;
        w->handler(w);
        n++;
    }
    return n;
}
/* bottom half: 완료된 요청을 회수해 cookie 를 모은다 */
static void uart_tx_work_handler(struct k_work *w) {
    struct uart_dev *dev = uart_from_work(w);
    if (dev == NULL) { g_spurious++; return; }
    dev->bh_runs++;
    for (int i = 0; i < NREQ; i++) {
        struct tx_req *r = &dev->tx[i];
        if (r->state != REQ_DONE) continue;
        if (dev->n_done < 8u) dev->done_cookie[dev->n_done] = r->cookie;
        dev->n_done++;
        r->state = REQ_FREE;
    }
}
static void uart_init(struct uart_dev *dev, const char *name, uint32_t base) {
    *dev = (struct uart_dev){0};
    dev->name = name;
    dev->base = base;
    for (int i = 0; i < NREQ; i++) {
        dev->tx[i].idx = (uint8_t)i;
        dev->tx[i].desc.next = &dev->tx[(i + 1) % NREQ].desc;    /* 디스크립터 링 */
    }
    work_init(&dev->tx_work, uart_tx_work_handler);
}
static void uart_tx_start(struct uart_dev *dev, int i, uint32_t cookie, uint16_t nbytes) {
    struct tx_req *r = &dev->tx[i];
    r->state = REQ_BUSY;
    r->cookie = cookie;
    r->nbytes = nbytes;
    r->desc.len = nbytes;
    r->desc.ctrl = DESC_OWN;                         /* 하드웨어에 넘김 */
}
/* DMA 완료 ISR: 하드웨어가 알려 주는 건 "방금 끝난 디스크립터 주소" 하나뿐이다 */
static void dma_irq_handler(void) {
    struct dma_desc *d = (struct dma_desc *)g_dma.CURR_DESC;
    g_dma.IFCR = 1u;                                 /* 인터럽트 플래그 클리어 (W1C, C-M02) */
    struct tx_req *r = req_from_desc(d);
    struct uart_dev *dev = r ? uart_from_req(r) : NULL;
    if (dev == NULL) { g_spurious++; return; }
    r->state = REQ_DONE;
    dev->tx_done++;
    work_submit(&dev->tx_work);                      /* 무거운 일은 bottom half 로 미룬다 */
}
static void dma_hw_complete(struct dma_desc *d) {    /* 시뮬레이터: 엔진이 d 를 끝냈다 */
    d->ctrl &= ~DESC_OWN;
    g_dma.CURR_DESC = (uintptr_t)d;
    dma_irq_handler();
}
static void dma_hw_spurious(void) {                  /* CURR_DESC 가 0 인 채로 인터럽트 */
    g_dma.CURR_DESC = 0u;
    dma_irq_handler();
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define IS_CONST_PTR(p) _Generic(QUAL_VOID_PTR_(p), const void *: 1, default: 0)

struct list_node { struct list_node *next, *prev; };
struct layout {                                      /* MY_OFFSETOF 시험용: 패딩·중첩·배열·과정렬 */
    uint8_t  id;
    uint32_t flags;
    uint16_t len;
    struct { uint8_t x; uint64_t y; } in;
    uint32_t arr[5];
    struct list_node link;
    _Alignas(16) uint8_t tail[3];
};
struct msg { uint16_t len; uint8_t type; uint8_t data[]; };      /* 유연 배열 멤버 */
union word { uint32_t w; uint8_t b[4]; struct { uint16_t lo, hi; } h; };
struct node_host {                                   /* container_of 시험용: 여러 위치의 같은 타입 노드 */
    struct list_node first;                          /* offset 0 — 캐스트로도 맞는 유일한 경우 */
    uint8_t  tag;
    struct list_node mid;
    uint64_t stamp;
    struct { uint32_t a; struct list_node deep; } nest;
    struct list_node last;
};
static int g_eval_count;
static struct list_node *next_node(struct list_node *n) { g_eval_count++; return n; }

int main(void) {
    /* ---- Part A: MY_OFFSETOF == offsetof ---- */
    CHECK(MY_OFFSETOF(struct layout, id) == offsetof(struct layout, id));
    CHECK(MY_OFFSETOF(struct layout, flags) == offsetof(struct layout, flags));
    CHECK(MY_OFFSETOF(struct layout, len) == offsetof(struct layout, len));
    CHECK(MY_OFFSETOF(struct layout, in) == offsetof(struct layout, in));
    CHECK(MY_OFFSETOF(struct layout, in.y) == offsetof(struct layout, in.y));
    CHECK(MY_OFFSETOF(struct layout, arr[3]) == offsetof(struct layout, arr[3]));
    CHECK(MY_OFFSETOF(struct layout, link.prev) == offsetof(struct layout, link.prev));
    CHECK(MY_OFFSETOF(struct layout, tail) == offsetof(struct layout, tail));
    int ok = 1;
    for (int i = 0; i < 5; i++)                      /* 런타임 인덱스 */
        ok &= MY_OFFSETOF(struct layout, arr[i]) == offsetof(struct layout, arr) + (size_t)i * sizeof(uint32_t);
    CHECK(ok);
    CHECK(MY_OFFSETOF(struct msg, data) == offsetof(struct msg, data));
    CHECK(MY_OFFSETOF(union word, h.hi) == offsetof(union word, h.hi) && MY_OFFSETOF(union word, b[0]) == 0u);
    CHECK(_Generic(MY_OFFSETOF(struct layout, len), size_t: 1, default: 0));

    /* ---- Part A: container_of — 어느 위치의 멤버에서 출발해도 원래 객체 ---- */
    static struct node_host h, hs[4];
    CHECK(container_of(&h.first, struct node_host, first) == &h);
    CHECK(container_of(&h.mid, struct node_host, mid) == &h);
    CHECK(container_of(&h.nest.deep, struct node_host, nest.deep) == &h);
    CHECK(container_of(&h.last, struct node_host, last) == &h);
    ok = 1;
    for (int k = 0; k < 4; k++) {
        struct list_node *ns[3] = { &hs[k].mid, &hs[k].nest.deep, &hs[k].last };
        ok &= container_of(ns[0], struct node_host, mid) == &hs[k];
        ok &= container_of(ns[1], struct node_host, nest.deep) == &hs[k];
        ok &= container_of(ns[2], struct node_host, last) == &hs[k];
    }
    CHECK(ok);
    struct node_host *hp = container_of(&hs[2].last, struct node_host, last);
    if (hp == &hs[2]) hp->tag = 0x5A;                /* 복원한 포인터로 쓰기 (틀린 주소면 쓰지 않는다) */
    CHECK(hs[2].tag == 0x5A && hs[1].tag == 0 && hs[3].tag == 0);
    /* 같은 타입의 "다른 멤버 이름" — 타입 검사로 못 잡는다. 정확히 (last - mid) 바이트 어긋난 주소 */
    CHECK((char *)container_of(&h.mid, struct node_host, last) ==
          (char *)&h - (offsetof(struct node_host, last) - offsetof(struct node_host, mid)));

    /* ---- Part B: const 보존 ---- */
    const struct node_host *ch = &h;
    CHECK(IS_CONST_PTR(container_of(&ch->mid, struct node_host, mid)) == 0);  /* 평범한 판은 const 를 잃는다 */
    CHECK(IS_CONST_PTR(container_of_const(&ch->mid, struct node_host, mid)) == 1);
    CHECK(IS_CONST_PTR(container_of_const(&h.mid, struct node_host, mid)) == 0);
    CHECK(container_of_const(&ch->last, struct node_host, last) == ch);
    CHECK(container_of_const(&h.nest.deep, struct node_host, nest.deep) == &h);

    /* ---- Part B: NULL 안전, ptr 1회 평가 ---- */
    struct list_node *none = NULL;
    CHECK(container_of_or_null(none, struct node_host, last) == NULL);
    CHECK(container_of_or_null(&h.last, struct node_host, last) == &h);
    g_eval_count = 0;
    CHECK(container_of_or_null(next_node(&hs[3].mid), struct node_host, mid) == &hs[3] && g_eval_count == 1);

    /* ---- Part B: 배열 원소 멤버 ---- */
    static struct dma_ctrl dc[2];
    for (int c = 0; c < 2; c++)
        for (int i = 0; i < 4; i++) dc[c].ch[i].idx = (uint8_t)i;
    ok = 1;
    for (int c = 0; c < 2; c++)
        for (int i = 0; i < 4; i++) ok &= chan_to_ctrl(&dc[c].ch[i]) == &dc[c];
    CHECK(ok);
    ok = 1;
    for (int c = 0; c < 2; c++)
        for (int i = 0; i < 4; i++) ok &= chan_to_ctrl_idx(&dc[c].ch[i]) == &dc[c];
    CHECK(ok);

    /* ---- Part C: DMA 완료 ISR → bottom half ---- */
    uart_init(&g_uart[0], "uart0", 0x40011000u);
    uart_init(&g_uart[1], "uart1", 0x40004400u);
    CHECK(req_from_desc(&g_uart[0].tx[3].desc) == &g_uart[0].tx[3]);
    CHECK(req_from_desc(NULL) == NULL);
    CHECK(uart_from_req(&g_uart[1].tx[2]) == &g_uart[1] && uart_from_req(&g_uart[0].tx[0]) == &g_uart[0]);
    CHECK(uart_from_work(&g_uart[1].tx_work) == &g_uart[1]);
    /* 시나리오 1: uart1 의 tx[2] 완료 → ISR 은 uart1 만 센다 → bottom half 가 회수 */
    uart_tx_start(&g_uart[1], 2, 0xC0FFEE01u, 12);
    dma_hw_complete(&g_uart[1].tx[2].desc);
    CHECK(g_uart[1].tx_done == 1u && g_uart[0].tx_done == 0u && g_spurious == 0u);
    CHECK(g_uart[1].tx[2].state == REQ_DONE && g_uart[1].tx_work.pending);
    CHECK(work_run_all() == 1);
    CHECK(g_uart[1].bh_runs == 1u && g_uart[1].n_done == 1u && g_uart[1].done_cookie[0] == 0xC0FFEE01u &&
          g_uart[1].tx[2].state == REQ_FREE);
    /* 시나리오 2: uart0 에서 두 번 완료 → work 는 합쳐져 bottom half 1번에 2개 회수 */
    uart_tx_start(&g_uart[0], 3, 0xA3u, 5);
    uart_tx_start(&g_uart[0], 1, 0xA1u, 7);
    dma_hw_complete(&g_uart[0].tx[3].desc);
    dma_hw_complete(&g_uart[0].tx[1].desc);
    CHECK(g_uart[0].tx_done == 2u && work_run_all() == 1);
    CHECK(g_uart[0].bh_runs == 1u && g_uart[0].n_done == 2u &&
          g_uart[0].done_cookie[0] == 0xA1u && g_uart[0].done_cookie[1] == 0xA3u);
    /* 시나리오 3: 두 장치 교차 → 각자 자기 bottom half */
    uart_tx_start(&g_uart[0], 0, 0xB0u, 1);
    uart_tx_start(&g_uart[1], 3, 0xB3u, 1);
    dma_hw_complete(&g_uart[1].tx[3].desc);
    dma_hw_complete(&g_uart[0].tx[0].desc);
    CHECK(work_run_all() == 2);
    CHECK(g_uart[0].bh_runs == 2u && g_uart[1].bh_runs == 2u &&
          g_uart[0].done_cookie[2] == 0xB0u && g_uart[1].done_cookie[1] == 0xB3u);
    /* 시나리오 4: CURR_DESC == 0 인 가짜 인터럽트 → 크래시 없이 세고 끝 */
    dma_hw_spurious();
    CHECK(g_spurious == 1u && g_uart[0].tx_done == 3u && g_uart[1].tx_done == 2u && work_run_all() == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
