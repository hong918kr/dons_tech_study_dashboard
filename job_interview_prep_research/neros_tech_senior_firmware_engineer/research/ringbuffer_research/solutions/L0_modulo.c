// L0_modulo.c  —  SOLUTION (정답 + 해설)
// 레벨 0 — 가장 단순한 링버퍼 (고정 크기 배열 + % 연산)  —  Q1~Q5
// ---------------------------------------------------------------------------
// 빌드/실행:  make sol N=L0_modulo
//   또는:     cc -std=c11 -Wall -Wextra -O0 -g solutions/L0_modulo.c -o build/L0_modulo_sol && ./build/L0_modulo_sol
//
// 이 레벨에서 배우는 것:
//   - head / tail 의 의미: head = "다음에 쓸 칸", tail = "다음에 읽을 칸".
//   - 빈 상태와 가득 찬 상태를 구분하는 고전적 방법 = 한 칸을 희생한다.
//   - `%` 연산의 실제 비용. Cortex-M0/M0+ 처럼 하드웨어 나눗셈기가 없는 코어에서
//     정수 나눗셈은 수십 사이클짜리 소프트웨어 루틴(__aeabi_uidivmod)이다.
//
// 레벨 로드맵에서의 위치:
//   L0(여기, % + 한 칸 희생) -> L1(count 필드) -> L2(free-running index + 마스킹)
//   -> L3(바이트/레코드 링) -> L4(lock-free SPSC).
//   각 레벨은 바로 앞 레벨의 "대가(cost)"를 하나씩 없애는 방향으로 간다.
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
// 왜 한 칸을 버리는가?
//   head == tail 이라는 하나의 상태가 "완전히 비었다"와 "완전히 찼다" 두 가지를
//   동시에 의미하게 되면 코드가 둘을 구분할 방법이 없다. 그래서 head 가 tail 을
//   따라잡기 직전(= 한 칸 남았을 때)을 full 로 선언해 버린다.
//   대가: 슬롯 8개짜리 배열이지만 실제로 저장 가능한 건 7개.
//   이 한 칸을 되찾는 방법이 레벨 1(count 필드)과 레벨 2(free-running index)다.
//
// 왜 unsigned 인가?
//   인덱스는 음수가 될 일이 없고, 아래 count 계산에서 부호 없는 산술이 필요하다.
//   int 였다면 (head - tail) 이 음수가 되어 C 에서 % 결과도 음수가 된다
//   (C99 이후 나눗셈은 0 방향 절삭 -> -3 % 8 == -3). 배열 인덱스로 쓰면 그 자리에서
//   out-of-bounds 다. `+ CAP` 를 더해 항상 양수로 만든 뒤 % 를 취하는 이유.
// ===========================================================================
#define RB0_CAP 8   /* 슬롯 8개 = 실제 저장 가능 7개 (한 칸 희생) */

typedef struct {
    int      buf[RB0_CAP];
    unsigned head;   /* 다음에 쓸 위치 */
    unsigned tail;   /* 다음에 읽을 위치 */
} rb0_t;

/* ---------------------------------------------------------------------------
 * Q1.  init / is_empty / is_full
 *   KO: init 은 head, tail 을 0 으로 맞춘다(버퍼 내용은 안 지워도 된다 — 어차피
 *       tail~head 구간 밖은 의미 없는 쓰레기값이다). empty 는 head == tail,
 *       full 은 head 를 한 칸 전진시켰을 때 tail 과 만나는 상태.
 *   EN: Init sets head/tail to 0; empty is head==tail, full is (head+1)%CAP==tail.
 *   ex: 방금 init 한 큐 -> is_empty 참, is_full 거짓
 *   hint: full 판정에 (head + 1) % RB0_CAP 을 쓴다. 한 칸을 비워두는 그 규칙이다.
 * ------------------------------------------------------------------------- */
void rb0_init(rb0_t *q) {
    if (!q) return;
    q->head = 0;
    q->tail = 0;
    /* buf 는 일부러 지우지 않는다. 링버퍼의 유효 데이터는 tail..head 구간뿐이고
       나머지는 읽히지 않으므로, MCU 부팅 시 수 KB 를 0 으로 미는 비용이 아깝다. */
}

bool rb0_is_empty(const rb0_t *q) {
    /* 두 인덱스가 같다 = 소비자가 생산자를 완전히 따라잡았다. */
    return q->head == q->tail;
}

bool rb0_is_full(const rb0_t *q) {
    /* 한 칸 앞이 tail 이면 full 로 간주한다. 이 한 칸이 empty 와 full 을
       구분해 주는 "보험"이다. 여기서 % 가 한 번 돈다는 점에 주목. */
    return ((q->head + 1u) % RB0_CAP) == q->tail;
}

/* ---------------------------------------------------------------------------
 * Q2.  push / pop
 *   KO: push 는 가득 차 있으면 아무것도 하지 않고 false. 성공하면 head 위치에 쓰고
 *       head 를 한 칸 전진(모듈러)시킨다. pop 은 비어 있으면 false 이고 이때
 *       out 은 절대 건드리지 않는다(호출자가 실패를 감지할 때 값이 오염되면 안 됨).
 *   EN: push returns false when full; pop returns false when empty and must leave
 *       *out untouched in that case.
 *   ex: init -> push(10), push(20) -> pop 은 10 을 먼저 준다 (FIFO)
 *   hint: 전진은 q->head = (q->head + 1) % RB0_CAP;
 * ------------------------------------------------------------------------- */
bool rb0_push(rb0_t *q, int v) {
    if (!q || rb0_is_full(q)) return false;   /* 덮어쓰기 정책이 아니라 거부 정책 */
    q->buf[q->head] = v;                      /* 1) 데이터 먼저 */
    q->head = (q->head + 1u) % RB0_CAP;       /* 2) 그다음 인덱스 전진 */
    return true;
}

bool rb0_pop(rb0_t *q, int *out) {
    if (!q || !out) return false;
    if (rb0_is_empty(q)) return false;        /* 실패 시 *out 은 손대지 않는다 */
    *out = q->buf[q->tail];
    q->tail = (q->tail + 1u) % RB0_CAP;
    return true;
}

/* ---------------------------------------------------------------------------
 * Q3.  count / peek
 *   KO: count 는 현재 들어있는 원소 개수. head 가 tail 보다 뒤로 랩되어 있을 수
 *       있으므로 (head - tail + CAP) % CAP 로 계산한다. peek 는 pop 과 같지만
 *       tail 을 전진시키지 않는다(다음에 읽힐 값 미리보기).
 *   EN: count = (head - tail + CAP) % CAP; peek reads the front without consuming.
 *   ex: 3개 들어있으면 count==3, peek 는 가장 오래된 값을 주고 count 는 그대로 3
 *   hint: `+ CAP` 가 없으면 head < tail 인 랩 상태에서 결과가 망가진다.
 * ------------------------------------------------------------------------- */
unsigned rb0_count(const rb0_t *q) {
    /* 왜 + RB0_CAP 인가:
         head=1, tail=6 인 랩 상태를 보자. 실제 개수는 3 (6,7,0 칸).
         (1 - 6) 을 unsigned 로 계산하면 매우 큰 수(2^32-5)가 되고, 그걸 % 8 하면
         우연히 3 이 나오긴 한다 — CAP 가 2의 거듭제곱일 때만 통하는 우연이다.
         CAP 가 2의 거듭제곱이 아니거나 타입이 int 였다면 바로 깨진다
         (int 면 -5 % 8 == -5). + CAP 를 먼저 더해 두면 어떤 CAP, 어떤 타입에서도
         피연산자가 항상 [0, 2*CAP) 범위의 양수라 안전하다. */
    return (q->head - q->tail + RB0_CAP) % RB0_CAP;
}

bool rb0_peek(const rb0_t *q, int *out) {
    if (!q || !out) return false;
    if (rb0_is_empty(q)) return false;
    *out = q->buf[q->tail];   /* tail 은 전진시키지 않는다 */
    return true;
}

// ===========================================================================
// 이 레벨의 비용에 대한 메모 (면접에서 말할 수 있어야 하는 내용)
// ---------------------------------------------------------------------------
// 1) `%` 의 값:
//    Cortex-M3/M4/M7 은 하드웨어 UDIV 가 있어 2~12 사이클이지만,
//    Cortex-M0/M0+ 에는 나눗셈 명령 자체가 없다. 컴파일러는 __aeabi_uidivmod 라는
//    소프트웨어 루틴을 부르고, 이건 보통 수십 사이클(대략 20~50) + 함수 호출 비용이다.
//    UART RX ISR 처럼 바이트마다 도는 핸들러에서 push 한 번에 % 가 두 번
//    (is_full 에서 한 번, head 전진에서 한 번) 돌면 무시할 수 없는 부담이 된다.
//
// 2) % 를 피하는 중간 기법 (레벨 0.5):
//        if (++q->head == RB0_CAP) q->head = 0;
//    분기 하나로 끝난다. CAP 가 2의 거듭제곱이 아니어도 쓸 수 있는 실전 기법이고,
//    이것만으로도 M0 에서 체감 차이가 크다. 단점은 "인덱스가 항상 [0,CAP)" 라는
//    성질에 의존하는 코드가 여기저기 생긴다는 것.
//
// 3) 진짜 해법 (레벨 2):
//    CAP 를 2의 거듭제곱으로 고정하고 `% CAP` 를 `& (CAP-1)` 로 바꾼다.
//    AND 한 번 = 1 사이클. 나아가 인덱스를 마스킹하지 않고 계속 증가시키면
//    (free-running) 한 칸 희생도 사라진다.
// ===========================================================================

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
// ===========================================================================
int main(void) {
    rb0_t q;
    int   v = 0;

    /* init 이 정말로 head/tail 을 0 으로 맞추는지 보려고 일부러 쓰레기값을 채운다.
       (스택 변수는 원래 쓰레기다 — 그 사실을 테스트로 드러낸다.) */
    memset(&q, 0xCC, sizeof q);

    // -------- Q1 --------
    printf("\n[Q1] init / is_empty / is_full\n");
    rb0_init(&q);
    T("init 직후 head==tail==0", q.head == 0u && q.tail == 0u);
    T("init 직후 is_empty 참", rb0_is_empty(&q));
    T("init 직후 is_full 거짓", !rb0_is_full(&q));
    {
        bool fill = true;
        for (int i = 0; i < 7; ++i) fill &= rb0_push(&q, i);
        T("7개 채우면 is_full 참 / is_empty 거짓",
          fill && rb0_is_full(&q) && !rb0_is_empty(&q));
    }

    // -------- Q2 --------
    printf("\n[Q2] push / pop 기본 동작\n");
    rb0_init(&q);
    T("빈 큐에서 pop 은 false", !rb0_pop(&q, &v));
    v = -12345;
    T("실패한 pop 은 out 을 건드리지 않는다", (!rb0_pop(&q, &v)) && v == -12345);
    T("push 3개 성공", rb0_push(&q, 10) && rb0_push(&q, 20) && rb0_push(&q, 30));
    {
        bool fifo = rb0_pop(&q, &v) && v == 10;
        fifo &= rb0_pop(&q, &v) && v == 20;
        fifo &= rb0_pop(&q, &v) && v == 30;
        T("FIFO 순서로 10,20,30 이 나온다", fifo);
        T("전부 꺼내면 다시 empty", rb0_is_empty(&q) && !rb0_pop(&q, &v));
    }
    {
        bool full_reject = true;
        rb0_init(&q);
        for (int i = 0; i < 7; ++i) full_reject &= rb0_push(&q, i);
        T("가득 찬 뒤 push 는 실패", full_reject && !rb0_push(&q, 9));
    }

    // -------- Q3 --------
    printf("\n[Q3] count / peek\n");
    rb0_init(&q);
    T("빈 큐의 count 는 0", rb0_count(&q) == 0u);
    rb0_push(&q, 100);
    rb0_push(&q, 200);
    rb0_push(&q, 300);
    T("3개 push 후 count==3", rb0_count(&q) == 3u);
    v = 0;
    T("peek 는 가장 오래된 값(100)을 준다", rb0_peek(&q, &v) && v == 100);
    T("peek 는 소비하지 않는다 (count 그대로 3)", rb0_count(&q) == 3u);
    rb0_pop(&q, &v);
    T("pop 하면 count 가 2 로 줄고 peek 는 200", rb0_count(&q) == 2u
        && rb0_peek(&q, &v) && v == 200);
    {
        rb0_t e;
        memset(&e, 0xCC, sizeof e);
        rb0_init(&e);
        v = -777;
        T("빈 큐 peek 는 false 이고 out 을 안 건드림", !rb0_peek(&e, &v) && v == -777);
    }

    // -------- Q4 --------
    printf("\n[Q4] wrap-around: 인덱스가 배열 끝을 넘어가도 FIFO 유지\n");
    rb0_init(&q);
    {
        bool wrap = true;
        for (int i = 1; i <= 7; ++i) wrap &= rb0_push(&q, i);      /* 1..7, 이제 full */
        for (int i = 1; i <= 4; ++i) wrap &= rb0_pop(&q, &v) && v == i;  /* 1,2,3,4 */
        T("7개 채우고 4개 빼면 count==3", wrap && rb0_count(&q) == 3u);

        /* head 는 지금 7, 여기서 4개 더 넣으면 7 -> 0 -> 1 -> 2 로 배열 끝을 넘는다 */
        for (int i = 8; i <= 11; ++i) wrap &= rb0_push(&q, i);
        T("랩 구간을 넘어 4개 더 push 성공 (head 가 배열 끝을 통과)",
          wrap && rb0_count(&q) == 7u && rb0_is_full(&q));
        T("랩 이후 head < tail 인 상태가 실제로 만들어졌다", q.head < q.tail);

        int exp[7] = {5, 6, 7, 8, 9, 10, 11};
        bool order = true;
        for (int i = 0; i < 7; ++i) order &= rb0_pop(&q, &v) && v == exp[i];
        T("랩어라운드 후에도 FIFO 순서 5,6,7,8,9,10,11 유지", order);
        T("다 꺼내면 empty, count==0", rb0_is_empty(&q) && rb0_count(&q) == 0u);
    }

    // -------- Q5 --------
    printf("\n[Q5] 용량 검증: 한 칸 희생 (8칸 배열, 7개 저장)\n");
    rb0_init(&q);
    {
        bool cap = true;
        for (int i = 0; i < 7; ++i) cap &= rb0_push(&q, i);
        T("8칸 배열에 7개까지만 — 한 칸은 full 판별용",
          cap && !rb0_push(&q, 999) && rb0_count(&q) == RB0_CAP - 1u);
        T("full 일 때 head+1 == tail (한 칸이 비어 있다)",
          ((q.head + 1u) % RB0_CAP) == q.tail);

        /* 한 칸 빼고 한 칸 넣기를 20번 반복해도 개수/순서가 어긋나지 않는지 */
        bool cyc = true;
        for (int i = 0; i < 20; ++i) {
            cyc &= rb0_pop(&q, &v);
            cyc &= rb0_push(&q, 1000 + i);
            cyc &= (rb0_count(&q) == RB0_CAP - 1u);
        }
        T("pop/push 20회 반복해도 count 는 항상 7 (인덱스 누적 오류 없음)", cyc);
        T("반복 후 가장 오래된 값은 1013 (7개 윈도우가 밀려 있다)",
          rb0_peek(&q, &v) && v == 1013);
    }

    printf("\n== 레벨 0 — 가장 단순한 링버퍼 (고정 크기 배열 + %% 연산) ==  PASS %d / FAIL %d\n",
           g_pass, g_fail);
    return g_fail ? 1 : 0;
}
