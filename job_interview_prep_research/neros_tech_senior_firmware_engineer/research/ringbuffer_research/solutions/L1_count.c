// L1_count.c  —  SOLUTION (정답 + 해설)
// 레벨 1 — count 필드로 한 칸도 낭비하지 않기  —  Q1~Q6
// ---------------------------------------------------------------------------
// 빌드/실행:  make sol N=L1_count
//   또는:     cc -std=c11 -Wall -Wextra -O0 -g solutions/L1_count.c -o build/L1_count_sol && ./build/L1_count_sol
//
// 이 레벨에서 배우는 것:
//   - 레벨 0 이 한 칸을 버린 이유(head==tail 이 empty 와 full 둘 다를 의미)를
//     "개수를 따로 센다"로 정면 돌파한다 -> 8칸 배열에 8개를 전부 담는다.
//   - 부분 성공(partial success) API: push_n / pop_n 은 되는 만큼만 처리하고
//     실제 처리한 개수를 돌려준다. UART/센서 드라이버의 표준 모양이다.
//   - tail 은 사실 파생값이다: tail = (head - count + CAP) % CAP.
//     (head, tail, count) 셋 중 둘만 있으면 나머지는 유도된다.
//   - 그리고 이 레벨의 대가 — count 를 생산자와 소비자가 "둘 다" 갱신한다는 것.
//     이게 레벨 4(lock-free SPSC)로 못 가는 이유다. 아래 '이 레벨의 대가' 참고.
//
// 레벨 로드맵:
//   L0(% + 한 칸 희생) -> L1(여기, count 필드) -> L2(free-running index + 마스킹)
//   -> L3(바이트/레코드 링) -> L4(lock-free SPSC).
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// ===========================================================================
// 테스트 하네스 (PASS/FAIL)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// ===========================================================================
// 자료구조
// ---------------------------------------------------------------------------
// 레벨 0 과의 차이는 필드 하나(count)뿐인데, 그 하나가 두 가지를 바꾼다.
//   1) empty/full 판정이 인덱스 비교가 아니라 개수 비교가 된다
//      -> head == tail 이 모호하지 않으므로 한 칸을 비워둘 이유가 사라진다.
//      -> 실제 용량 = RB1_CAP 전부. 레벨 0 대비 12.5% (1/8) 이득.
//   2) count 조회가 O(1) 상수 시간이고 % 도 필요 없다. 레벨 0 의
//      (head - tail + CAP) % CAP 에는 나눗셈이 들어 있었다.
// ===========================================================================
#define RB1_CAP 8   /* 이제 8개 전부 저장 가능 */

typedef struct {
    int      buf[RB1_CAP];
    unsigned head;
    unsigned tail;
    unsigned count;   /* 현재 들어있는 개수 — empty/full 을 명시적으로 구분 */
} rb1_t;

/* ---------------------------------------------------------------------------
 * Q1.  init / is_empty / is_full / free
 *   KO: init 은 head, tail, count 를 모두 0 으로. empty 는 count == 0,
 *       full 은 count == RB1_CAP (한 칸 희생이 없다!), free 는 남은 빈 칸 수.
 *   EN: init zeroes head/tail/count; empty is count==0, full is count==CAP,
 *       free is CAP-count.
 *   ex: init 직후 -> is_empty 참, free == 8
 *   hint: free 와 count 는 항상 더해서 RB1_CAP 이다. 이 불변식을 기억할 것.
 * ------------------------------------------------------------------------- */
void rb1_init(rb1_t *q) {
    if (!q) return;
    q->head  = 0;
    q->tail  = 0;
    q->count = 0;
    /* buf 는 지우지 않는다 — 유효 데이터는 count 개뿐이고 나머지는 읽히지 않는다. */
}

bool rb1_is_empty(const rb1_t *q) {
    return q->count == 0u;          /* 인덱스를 볼 필요조차 없다 */
}

bool rb1_is_full(const rb1_t *q) {
    return q->count == RB1_CAP;     /* 레벨 0 의 (head+1)%CAP==tail 과 대비 */
}

unsigned rb1_count(const rb1_t *q) {
    return q->count;                /* O(1), 나눗셈 없음 */
}

unsigned rb1_free(const rb1_t *q) {
    return RB1_CAP - q->count;      /* 불변식: count + free == RB1_CAP */
}

/* ---------------------------------------------------------------------------
 * Q2.  push / pop  (이번엔 8개 전부 들어간다)
 *   KO: push 는 full 이면 false. 성공하면 buf[head] 에 쓰고 head 를 전진시키고
 *       count++. pop 은 empty 면 false(이때 *out 은 손대지 않는다), 성공하면
 *       buf[tail] 을 읽고 tail 전진 + count--.
 *   EN: push/pop with an explicit count; capacity is the full RB1_CAP.
 *   ex: 빈 큐에 8번 push -> 전부 성공, 9번째만 false
 *   hint: 인덱스 전진은 여전히 % RB1_CAP. count 갱신을 빠뜨리지 말 것.
 * ------------------------------------------------------------------------- */
bool rb1_push(rb1_t *q, int v) {
    if (!q || rb1_is_full(q)) return false;
    q->buf[q->head] = v;
    q->head = (q->head + 1u) % RB1_CAP;
    q->count++;                      /* <- 생산자가 count 를 '쓴다' (뒤의 메모 참고) */
    return true;
}

bool rb1_pop(rb1_t *q, int *out) {
    if (!q || !out) return false;
    if (rb1_is_empty(q)) return false;   /* 실패 시 *out 불변 */
    *out = q->buf[q->tail];
    q->tail = (q->tail + 1u) % RB1_CAP;
    q->count--;                      /* <- 소비자도 count 를 '쓴다' (write-write!) */
    return true;
}

/* ---------------------------------------------------------------------------
 * Q4/Q5.  push_n / pop_n  (부분 성공 API)
 *   KO: push_n 은 n 개를 넣으려 시도하되 공간이 모자라면 들어간 만큼만 넣고 그
 *       개수를 반환한다(부분 성공). pop_n 도 마찬가지로 꺼낸 개수를 반환.
 *       n == 0 이거나 공간/데이터가 없으면 0 을 반환하고 아무것도 하지 않는다.
 *   EN: Bulk push/pop with partial success: copy as many as fit and return the
 *       count actually transferred.
 *   ex: free 가 3인데 push_n(...,5) -> 3 반환, 앞의 3개만 들어간다
 *   hint: 랩 구간은 "배열 끝까지 한 덩어리 + 앞에서부터 나머지 한 덩어리"로
 *         쪼개면 memcpy 두 번이면 된다. 루프로 한 개씩 넣어도 결과는 같다.
 * ------------------------------------------------------------------------- */
unsigned rb1_push_n(rb1_t *q, const int *src, unsigned n) {
    if (!q || !src || n == 0u) return 0u;

    unsigned space = rb1_free(q);
    if (n > space) n = space;            /* 부분 성공: 되는 만큼으로 줄인다 */
    if (n == 0u) return 0u;

    /* 랩 구간을 두 덩어리로 쪼갠다:
         first  = head 부터 배열 끝까지 연속으로 쓸 수 있는 개수
         second = 나머지 (배열 앞쪽으로 넘어간 부분)
       한 개씩 % 를 돌리는 루프보다 memcpy 두 번이 훨씬 빠르다 — DMA 를 붙일 때도
       "연속 구간 두 개"라는 이 형태가 그대로 descriptor 두 개가 된다. */
    unsigned first = RB1_CAP - q->head;
    if (first > n) first = n;
    unsigned second = n - first;

    memcpy(&q->buf[q->head], src, (size_t)first * sizeof(int));
    if (second > 0u)
        memcpy(&q->buf[0], src + first, (size_t)second * sizeof(int));

    q->head   = (q->head + n) % RB1_CAP;
    q->count += n;
    return n;
}

unsigned rb1_pop_n(rb1_t *q, int *dst, unsigned n) {
    if (!q || !dst || n == 0u) return 0u;

    if (n > q->count) n = q->count;      /* 있는 만큼만 */
    if (n == 0u) return 0u;

    unsigned first = RB1_CAP - q->tail;
    if (first > n) first = n;
    unsigned second = n - first;

    memcpy(dst, &q->buf[q->tail], (size_t)first * sizeof(int));
    if (second > 0u)
        memcpy(dst + first, &q->buf[0], (size_t)second * sizeof(int));

    q->tail   = (q->tail + n) % RB1_CAP;
    q->count -= n;
    return n;
}

/* ---------------------------------------------------------------------------
 * Q6.  tail 은 사실 파생값이다
 *   KO: (head, tail, count) 는 서로 독립이 아니다. head 와 count 만 있으면
 *       tail = (head - count + CAP) % CAP 로 언제나 유도할 수 있다.
 *       즉 이 설계에서 tail 필드는 '캐시'일 뿐 필수가 아니다.
 *   EN: tail is redundant: tail == (head - count + CAP) % CAP always holds.
 *   ex: head=2, count=5, CAP=8 -> tail = (2-5+8)%8 = 5
 *   hint: + CAP 가 없으면 head < count 인 상황에서 unsigned 언더플로가 난다.
 *         (레벨 0 의 count 계산에서 + CAP 가 필요했던 것과 정확히 같은 이유)
 * ------------------------------------------------------------------------- */
unsigned rb1_tail_derived(const rb1_t *q) {
    return (q->head - q->count + RB1_CAP) % RB1_CAP;
}

// ===========================================================================
// 이 레벨의 대가 — 왜 여기서 멈추면 안 되는가 (가장 중요한 문단)
// ---------------------------------------------------------------------------
// 얻은 것: 한 칸을 되찾았고(용량 8/8), count 조회가 O(1) 이며 코드가 읽기 쉽다.
//
// 잃은 것 1) 필드가 하나 늘었다. 구조체가 4바이트 커진다. 링을 수십 개 두는
//   MCU 에서는 RAM 예산에 직접 잡히고, 캐시 라인 안에서도 자리를 차지한다.
//
// 잃은 것 2) 이게 결정적이다 — 생산자와 소비자가 '둘 다' count 를 갱신한다.
//   push 는 count++ 를, pop 은 count-- 를 한다. 즉 같은 변수에 대해 두 컨텍스트가
//   모두 write 를 한다(write-write 경합). count++ 는 원자적이지 않고
//   load -> add -> store 세 단계라서, ISR 이 그 사이에 끼어들면 갱신 하나가
//   통째로 사라진다. 그러면 count 가 실제 개수와 어긋나고, 링은 가득 찼는데
//   count 는 7 이라고 믿는 식으로 조용히 데이터를 덮어쓰기 시작한다.
//   -> 메인 루프와 ISR 이 같이 쓴다면 이 버전은 count 갱신(정확히는 인덱스+count
//      갱신 전체)을 크리티컬 섹션으로 감싸야 한다.
//      __disable_irq() / taskENTER_CRITICAL() 로 감싸는 그 코드다.
//      대가: ISR 지연(interrupt latency) 증가. 하드 실시간 경로에서는 부담이고,
//      ISR 안에서 mutex 를 잡는 건 우선순위 역전 때문에 아예 금지다.
//   -> 그래서 레벨 4 의 lock-free SPSC 에서는 이 count 설계가 치명적이다.
//      레벨 2 의 free-running 인덱스로 가면 "생산자는 head 만 쓰고 소비자는
//      tail 만 쓴다"가 되어 공유 변수에 대한 write-write 경합이 구조적으로
//      사라지고, 개수는 used = head - tail 로 그때그때 계산한다.
//      Q6 이 보여주는 "셋 중 하나는 파생값"이라는 사실이 바로 그 길을 연다.
// ===========================================================================

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
// ===========================================================================
int main(void) {
    rb1_t q;
    int   v = 0;

    /* init 이 정말로 세 필드를 0 으로 맞추는지 보려고 일부러 쓰레기값을 채운다. */
    memset(&q, 0xCC, sizeof q);

    // -------- Q1 --------
    printf("\n[Q1] init / is_empty / is_full / free\n");
    rb1_init(&q);
    T("init 직후 head==tail==count==0",
      q.head == 0u && q.tail == 0u && q.count == 0u);
    T("init 직후 is_empty 참 / is_full 거짓", rb1_is_empty(&q) && !rb1_is_full(&q));
    T("빈 큐의 free 는 8 (한 칸 희생 없음)", rb1_free(&q) == RB1_CAP);
    rb1_push(&q, 1);
    rb1_push(&q, 2);
    rb1_push(&q, 3);
    T("3개 넣으면 count==3, free==5", rb1_count(&q) == 3u && rb1_free(&q) == 5u);
    T("불변식 count + free == RB1_CAP", rb1_count(&q) + rb1_free(&q) == RB1_CAP);

    // -------- Q2 --------
    printf("\n[Q2] push / pop — 8칸 배열에 8개 전부 저장 (레벨 0 대비 개선점)\n");
    rb1_init(&q);
    T("빈 큐에서 pop 은 false", !rb1_pop(&q, &v));
    v = -12345;
    T("실패한 pop 은 out 을 건드리지 않는다", (!rb1_pop(&q, &v)) && v == -12345);
    {
        bool fill = true;
        for (int i = 0; i < RB1_CAP; ++i) fill &= rb1_push(&q, 100 + i);
        T("8개 전부 push 성공 — 레벨 0 은 7개가 한계였다",
          fill && rb1_count(&q) == RB1_CAP && rb1_free(&q) == 0u);
        T("가득 찬 뒤 push 는 실패", rb1_is_full(&q) && !rb1_push(&q, 999));
        T("full 인데 head == tail 이다 (count 가 없으면 empty 와 구분 불가)",
          q.head == q.tail);

        bool fifo = true;
        for (int i = 0; i < RB1_CAP; ++i) fifo &= rb1_pop(&q, &v) && v == 100 + i;
        T("8개가 FIFO 순서로 전부 나온다", fifo);
        T("전부 꺼내면 empty, free==8", rb1_is_empty(&q) && rb1_free(&q) == RB1_CAP);
    }

    // -------- Q3 --------
    printf("\n[Q3] wrap-around FIFO 순서\n");
    rb1_init(&q);
    {
        bool wrap = true;
        for (int i = 1; i <= 8; ++i) wrap &= rb1_push(&q, i);        /* 1..8, full */
        for (int i = 1; i <= 5; ++i) wrap &= rb1_pop(&q, &v) && v == i;
        T("8개 채우고 5개 빼면 count==3, free==5",
          wrap && rb1_count(&q) == 3u && rb1_free(&q) == 5u);

        /* head 는 지금 0 (8 에서 랩됨), tail 은 5. 여기서 4개를 더 넣으면
           쓰기 위치는 0,1,2,3 — 즉 head 가 tail 뒤쪽으로 돌아간 랩 상태가 된다. */
        for (int i = 9; i <= 12; ++i) wrap &= rb1_push(&q, i);
        T("4개 더 push -> count==7, free==1",
          wrap && rb1_count(&q) == 7u && rb1_free(&q) == 1u);
        T("head < tail 인 랩 상태가 실제로 만들어졌다", q.head < q.tail);

        int exp[7] = {6, 7, 8, 9, 10, 11, 12};
        bool order = true;
        for (int i = 0; i < 7; ++i) order &= rb1_pop(&q, &v) && v == exp[i];
        T("랩 이후에도 FIFO 순서 6..12 유지", order && rb1_is_empty(&q));
    }

    // -------- Q4 --------
    printf("\n[Q4] push_n / pop_n 부분 성공 (n=0 경계, 용량 초과)\n");
    rb1_init(&q);
    {
        const int src5[5] = {1, 2, 3, 4, 5};
        int dst[16] = {0};

        T("n=0 인 push_n 은 0 을 반환하고 아무것도 안 넣는다",
          rb1_push_n(&q, src5, 0u) == 0u && rb1_count(&q) == 0u);
        T("빈 큐에서 pop_n 은 0", rb1_pop_n(&q, dst, 4u) == 0u);
        T("n=0 인 pop_n 도 0", rb1_pop_n(&q, dst, 0u) == 0u);

        T("push_n 5개 전부 들어간다", rb1_push_n(&q, src5, 5u) == 5u
          && rb1_count(&q) == 5u);

        /* free 는 3인데 5개를 넣으려 한다 -> 앞의 3개만 (부분 성공) */
        const int src5b[5] = {6, 7, 8, 9, 10};
        T("free(3) 보다 많은 push_n 은 들어간 개수 3 을 반환",
          rb1_push_n(&q, src5b, 5u) == 3u && rb1_is_full(&q));

        unsigned got = rb1_pop_n(&q, dst, 16u);   /* 요청 16, 실제 8 */
        int exp[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        bool ok = (got == 8u);
        for (unsigned i = 0; i < got; ++i) ok &= (dst[i] == exp[i]);
        T("pop_n 은 요청보다 적어도 있는 만큼(8)만 꺼내고 내용이 맞다", ok);
        T("pop_n 후 큐는 비었다", rb1_is_empty(&q) && rb1_free(&q) == RB1_CAP);

        /* 일부만 꺼내기 */
        rb1_push_n(&q, src5, 5u);
        T("pop_n(2) 는 2개만 꺼내고 나머지 3개는 남는다",
          rb1_pop_n(&q, dst, 2u) == 2u && dst[0] == 1 && dst[1] == 2
          && rb1_count(&q) == 3u);
    }

    // -------- Q5 --------
    printf("\n[Q5] push_n 이 랩 구간을 넘어도 결과는 동일 (memcpy 2번이든 루프든)\n");
    rb1_init(&q);
    {
        const int warm[6] = {-1, -2, -3, -4, -5, -6};
        int trash[6] = {0};
        /* head/tail 을 6 으로 밀어놓는다 -> 다음 push_n 은 반드시 배열 끝을 넘는다 */
        rb1_push_n(&q, warm, 6u);
        rb1_pop_n(&q, trash, 6u);
        T("준비: head==tail==6, 큐는 비어 있음",
          q.head == 6u && q.tail == 6u && rb1_is_empty(&q));

        const int span[5] = {71, 72, 73, 74, 75};   /* 6,7 | 0,1,2 로 쪼개진다 */
        T("랩을 가로지르는 push_n 5개가 전부 들어간다",
          rb1_push_n(&q, span, 5u) == 5u && rb1_count(&q) == 5u);
        T("랩 이후 head 는 3 으로 돌아왔다 (배열 앞쪽으로 넘어감)", q.head == 3u);

        int dst[5] = {0, 0, 0, 0, 0};
        unsigned got = rb1_pop_n(&q, dst, 5u);
        bool ok = (got == 5u);
        for (unsigned i = 0; i < 5u; ++i) ok &= (dst[i] == span[i]);
        T("두 덩어리로 쪼개 넣었어도 pop_n 결과는 71..75 그대로", ok);

        /* 반대 방향도: 쓰기는 연속이고 읽기가 랩을 가로지르는 경우 */
        const int more[8] = {81, 82, 83, 84, 85, 86, 87, 88};
        rb1_push_n(&q, more, 8u);          /* head=3 에서 8개 -> 다시 랩 */
        int out8[8] = {0};
        got = rb1_pop_n(&q, out8, 8u);     /* tail=3 에서 8개 -> 읽기도 랩 */
        ok = (got == 8u);
        for (unsigned i = 0; i < 8u; ++i) ok &= (out8[i] == more[i]);
        T("pop_n 도 랩을 가로질러 81..88 을 순서대로 꺼낸다", ok);
    }

    // -------- Q6 --------
    printf("\n[Q6] tail 은 파생값: tail == (head - count + CAP) %% CAP\n");
    rb1_init(&q);
    {
        const int src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        int dst[8] = {0};
        bool ok = (rb1_tail_derived(&q) == q.tail);   /* 빈 상태 */
        T("빈 큐(head=count=0)에서도 유도식이 맞는다", ok);

        rb1_push_n(&q, src, 8u);
        T("full 상태(head==tail)에서도 유도식이 맞는다",
          rb1_is_full(&q) && rb1_tail_derived(&q) == q.tail);

        /* 랜덤에 가까운 push/pop 을 섞어 돌리며 매 단계 불변식을 확인한다 */
        bool always = true;
        unsigned seed = 12345u;
        for (int step = 0; step < 200; ++step) {
            seed = seed * 1103515245u + 12345u;       /* 결정적 LCG */
            if ((seed >> 16) & 1u) {
                unsigned n = ((seed >> 20) % 4u) + 1u;
                rb1_push_n(&q, src, n);
            } else {
                unsigned n = ((seed >> 24) % 4u) + 1u;
                rb1_pop_n(&q, dst, n);
            }
            if (rb1_tail_derived(&q) != q.tail) always = false;
            if (rb1_count(&q) + rb1_free(&q) != RB1_CAP) always = false;
        }
        T("push/pop 200회를 섞어 돌려도 유도된 tail == 실제 tail", always);
        T("같은 200회 동안 count + free == CAP 도 항상 성립",
          rb1_count(&q) + rb1_free(&q) == RB1_CAP);
        T("유도식 수치 확인: head=2, count=5 -> tail=5 이어야 한다",
          ((2u - 5u + RB1_CAP) % RB1_CAP) == 5u);
    }

    printf("\n== 레벨 1 — count 필드로 한 칸도 낭비하지 않기 ==  PASS %d / FAIL %d\n",
           g_pass, g_fail);
    return g_fail ? 1 : 0;
}
