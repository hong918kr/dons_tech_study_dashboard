// L2_mask.c  —  PRACTICE STUB (직접 채워넣기)
// 레벨 2 — 마스킹과 free-running 인덱스  —  Q1~Q6
// ---------------------------------------------------------------------------
// 빌드/실행:  make prob N=L2_mask
//   또는:     cc -std=c11 -Wall -Wextra -O0 -g problems/L2_mask.c -o build/L2_mask_prob && ./build/L2_mask_prob
//
// 각 함수의 '// TODO' 를 구현하고 다시 실행 -> [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일/실행은 되며 대부분 FAIL 로 뜬다.)
//
// 이 레벨에서 배우는 것:
//   - `idx % size` 를 `idx & (size-1)` 로 바꾸는 법 (size 가 2의 거듭제곱일 때만 성립).
//     Cortex-M0/M0+ 에는 하드웨어 나눗셈기가 없어 `%` 는 수십 사이클, `& mask` 는 1사이클.
//   - count 필드를 없애고 `used = head - tail` 로 계산하는 법.
//     push 와 pop 이 공유하는 변수가 사라진다 -> lock-free SPSC 로 가는 발판.
//   - head/tail 을 마스킹해서 저장하지 않고 계속 증가(free-running)시키면,
//     **부호 없는 정수 오버플로가 랩어라운드를 자동으로 해결**한다.
//     empty = (head==tail), full = (head-tail==size) 로 갈리므로 레벨 0 처럼
//     "한 칸을 비워 두는" 낭비가 없다. 용량 = size 전부.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

// ===========================================================================
// 테스트 하네스 (건드리지 말 것)
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
// head/tail 은 "지금까지 몇 바이트를 넣었/뺐는가"를 세는 누적 카운터다.
// 절대 마스킹해서 저장하지 않는다 — 마스킹은 buf 를 첨자로 만질 때만 한다.
// ===========================================================================
typedef struct {
    uint8_t *buf;
    uint32_t size;    /* 반드시 2의 거듭제곱 */
    uint32_t mask;    /* size - 1 */
    uint32_t head;    /* free-running: 마스킹하지 않고 계속 증가 */
    uint32_t tail;
} rb2_t;

/* ---------------------------------------------------------------------------
 * Q1.  2의 거듭제곱 판정 + init 에서의 거부
 *   KO: n 이 2의 거듭제곱이면 true. 0 은 false (2^k 중에 0 은 없다).
 *       rb2_init 은 buf/q 가 NULL 이거나 size 가 2의 거듭제곱이 아니면 false 를
 *       리턴하고 구조체를 건드리지 않는다. 성공 시 mask=size-1, head=tail=0.
 *   EN: Power-of-two test and an init that refuses any non-pow2 (or zero) size.
 *   ex: 8 -> true, 12 -> false, 0x80000000 -> true, 0 -> false
 *   hint: 2의 거듭제곱은 비트가 정확히 하나. n & (n-1) 은 최하위 1비트를 지운다.
 *         n=0 도 그 식을 통과해 버리니 0 검사를 따로 붙일 것.
 * ------------------------------------------------------------------------- */
bool rb2_is_pow2(uint32_t n) {
    (void)n;
    // TODO: implement (n != 0 && (n & (n-1)) == 0)
    return false;  // placeholder
}

bool rb2_init(rb2_t *q, uint8_t *buf, uint32_t size) {
    (void)q; (void)buf; (void)size;
    // TODO: implement (NULL 검사, pow2 검사, mask=size-1, head/tail=0)
    return false;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q2.  1바이트 push / pop  —  용량 = size 전부
 *   KO: full 은 (head - tail) == size, empty 는 head == tail.
 *       push 는 buf[head & mask] 에 쓰고 head++ (마스킹하지 않은 채로 증가),
 *       pop 은 buf[tail & mask] 를 읽고 tail++.
 *   EN: Single-byte push/pop; capacity is the full size (no wasted slot).
 *   ex: size 8 -> 8개 모두 push 성공, 9번째 push 는 false
 *   hint: 인덱스 자체는 절대 마스킹하지 않는다. 버퍼 첨자에만 & mask.
 *         q->head = (q->head+1) & q->mask 로 쓰면 이 레벨의 구조가 무너진다.
 * ------------------------------------------------------------------------- */
bool rb2_push(rb2_t *q, uint8_t v) {
    (void)q; (void)v;
    // TODO: implement (full 이면 false, buf[head & mask] = v, head++)
    return false;  // placeholder
}

bool rb2_pop(rb2_t *q, uint8_t *out) {
    (void)q; (void)out;
    // TODO: implement (empty 면 *out 을 건드리지 말고 false, 아니면 tail++)
    return false;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q3.  used / free  —  count 필드 없이 계산
 *   KO: used = head - tail (부호 없는 뺄셈), free = size - used.
 *       head 가 tail 보다 "숫자상" 작아도(오버플로 후) mod 2^32 뺄셈이라 정확하다.
 *   EN: Derive occupancy from the two free-running indices; no count field.
 *   ex: head=10, tail=4 -> used 6, size 8 이면 free 2
 *   hint: 절대 (int)로 캐스팅하지 말 것. signed 오버플로는 UB 다.
 * ------------------------------------------------------------------------- */
uint32_t rb2_used(const rb2_t *q) {
    (void)q;
    // TODO: implement (head - tail)
    return 0;  // placeholder
}

uint32_t rb2_free(const rb2_t *q) {
    (void)q;
    // TODO: implement (size - used)
    return 0;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q4.  벌크 write / read  —  memcpy 2회로 랩 구간 분할
 *   KO: 들어가는 만큼만 쓰고(부분 성공 허용) 실제 처리한 바이트 수를 리턴한다.
 *       랩 지점을 가로지르면 [head..size) 와 [0..) 두 조각이므로 memcpy 를 2번.
 *       n=0 이거나 공간이 0 이면 0 을 리턴한다.
 *   EN: Bulk copy in at most two memcpy calls (tail chunk + head chunk).
 *   ex: size 8, head=5 에 5바이트 write -> 3바이트 + 2바이트 두 조각
 *   hint: off = head & mask, first = min(n, size - off).
 *         바이트 루프 말고 memcpy 2회로 — 워드 단위 복사라 훨씬 빠르다.
 * ------------------------------------------------------------------------- */
uint32_t rb2_write(rb2_t *q, const uint8_t *src, uint32_t n) {
    (void)q; (void)src; (void)n;
    // TODO: implement (n = min(n, free), 첫 조각 memcpy, 남으면 두 번째 memcpy, head += n)
    return 0;  // placeholder
}

uint32_t rb2_read(rb2_t *q, uint8_t *dst, uint32_t n) {
    (void)q; (void)dst; (void)n;
    // TODO: implement (n = min(n, used), memcpy 2회, tail += n)
    return 0;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q5.  ★ 인덱스 오버플로 안전성 (free-running 의 핵심 장점)
 *   KO: head 가 0xFFFFFFFF 를 넘어 0 으로 랩해도 used = head - tail 은 여전히
 *       정확하다. 새로 구현할 함수는 없고, Q2~Q4 의 코드가 그대로 견뎌야 한다.
 *       테스트는 head=tail=0xFFFFFFFC 로 강제 세팅한 뒤 12바이트를 왕복시킨다.
 *   EN: Unsigned index wrap is harmless: head-tail is exact modulo 2^32.
 *   ex: 0xFFFFFFFC + 12 = 0x00000008, tail=0xFFFFFFFC -> used == 12
 *   hint: C11 6.2.5p9 — unsigned 연산은 2^N 으로 나눈 나머지로 "정의"된다.
 *         Q2~Q4 에서 인덱스를 int 로 캐스팅했다면 여기서 깨진다.
 * ------------------------------------------------------------------------- */
// (구현할 함수 없음 — Q2~Q4 가 오버플로를 견디는지 main 에서 검증한다)

/* ---------------------------------------------------------------------------
 * Q6.  peek_contig  —  지금 읽을 수 있는 "연속 구간"
 *   KO: 복사 없이 바로 읽을 수 있는 연속 영역의 길이를 리턴하고, *ptr 에 그
 *       시작 주소를 준다. 랩 지점에서 잘리므로 used 보다 작을 수 있다.
 *       비어 있으면 0 을 리턴하고 *ptr 은 NULL.  (레벨 5b zero-copy 의 예고편)
 *   EN: Return the length of the contiguous readable region and its address,
 *       so a DMA/UART driver can consume it without an intermediate copy.
 *   ex: size 8, tail=4, used=6 -> 4 리턴 (buf+4 부터 버퍼 끝까지)
 *   hint: contig = min(used, size - (tail & mask))
 * ------------------------------------------------------------------------- */
uint32_t rb2_peek_contig(rb2_t *q, const uint8_t **ptr) {
    (void)q; (void)ptr;
    // TODO: implement (empty 면 *ptr=NULL 후 0, 아니면 min(used, size - off))
    return 0;  // placeholder
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL (건드리지 말 것)
// ===========================================================================
int main(void) {
    uint8_t v = 0;

    // -------- Q1 : pow2 판정 + init 거부 --------
    printf("\n[Q1] rb2_is_pow2 / init 거부\n");
    bool p2_ok = (rb2_is_pow2(0) == false)
              && (rb2_is_pow2(1) == true)
              && (rb2_is_pow2(2) == true)
              && (rb2_is_pow2(3) == false)
              && (rb2_is_pow2(8) == true)
              && (rb2_is_pow2(12) == false);
    T("Q1 is_pow2: 0/3/12 는 false, 1/2/8 은 true", p2_ok);
    T("Q1 is_pow2: 0x80000000 (최상위 비트 하나) 은 true",
      rb2_is_pow2(0x80000000u) == true);
    T("Q1 is_pow2: 0xFFFFFFFF 는 false", rb2_is_pow2(0xFFFFFFFFu) == false);

    uint8_t sto[8]  = {0};
    rb2_t   q       = {0};
    bool init_ok = (rb2_init(&q, sto, 0) == false)    // size 0 거부
                && (rb2_init(&q, sto, 12) == false)   // 2의 거듭제곱 아님
                && (rb2_init(&q, NULL, 8) == false)   // buf NULL 거부
                && (rb2_init(&q, sto, 8) == true);
    T("Q1 init: size 0 / 12 / buf NULL 거부, size 8 수용", init_ok);
    T("Q1 init: mask == size-1, head==tail==0",
      q.mask == 7u && q.head == 0u && q.tail == 0u);

    // -------- Q2 : push/pop, 용량 = size 전부 --------
    printf("\n[Q2] push/pop — 8칸 링에 8개 저장 (레벨 0 대비 +1)\n");
    bool fill_ok = true;
    for (uint8_t i = 0; i < 8; ++i) fill_ok &= rb2_push(&q, (uint8_t)(0xA0 + i));
    T("Q2 size 전부(8개) push 성공 — 한 칸도 버리지 않는다", fill_ok);
    T("Q2 가득 찬 뒤 push 는 실패", !rb2_push(&q, 0xFF));

    bool fifo_ok = true;
    for (uint8_t i = 0; i < 8; ++i)
        if (!rb2_pop(&q, &v) || v != (uint8_t)(0xA0 + i)) fifo_ok = false;
    T("Q2 pop 은 넣은 순서(FIFO)대로", fifo_ok);
    T("Q2 빈 링에서 pop 은 실패", !rb2_pop(&q, &v));

    // -------- Q3 : used/free + 랩어라운드 FIFO --------
    printf("\n[Q3] used/free, 랩어라운드 FIFO\n");
    rb2_init(&q, sto, 8);
    T("Q3 빈 링: used 0, free 8", rb2_used(&q) == 0u && rb2_free(&q) == 8u);

    for (uint8_t i = 0; i < 5; ++i) rb2_push(&q, (uint8_t)(10 + i));
    T("Q3 5개 push 후: used 5, free 3", rb2_used(&q) == 5u && rb2_free(&q) == 3u);

    bool wrap_ok = true;
    for (uint8_t i = 0; i < 3; ++i)                       // tail 3 전진
        if (!rb2_pop(&q, &v) || v != (uint8_t)(10 + i)) wrap_ok = false;
    for (uint8_t i = 0; i < 6; ++i)                       // head 가 8 을 넘어 랩
        if (!rb2_push(&q, (uint8_t)(20 + i))) wrap_ok = false;
    T("Q3 랩 이후에도 used 8 / free 0 (가득)",
      rb2_used(&q) == 8u && rb2_free(&q) == 0u);

    uint8_t exp3[8] = {13, 14, 20, 21, 22, 23, 24, 25};
    for (int i = 0; i < 8; ++i)
        if (!rb2_pop(&q, &v) || v != exp3[i]) wrap_ok = false;
    T("Q3 랩 구간을 건너도 FIFO 순서 보존", wrap_ok);

    // -------- Q4 : 벌크 write/read --------
    printf("\n[Q4] rb2_write / rb2_read (memcpy 2회, 랩 구간 분할)\n");
    uint8_t bsto[8] = {0};
    rb2_t   bq      = {0};
    rb2_init(&bq, bsto, 8);

    const uint8_t src5[5] = {1, 2, 3, 4, 5};
    uint8_t dst[16] = {0};
    bool blk_ok = (rb2_write(&bq, src5, 5) == 5u) && (rb2_used(&bq) == 5u);
    blk_ok &= (rb2_read(&bq, dst, 3) == 3u);
    blk_ok &= (dst[0] == 1 && dst[1] == 2 && dst[2] == 3);
    T("Q4 write 5 / read 3: 내용과 개수 일치", blk_ok);

    // head=5, tail=3 -> 여기서 5바이트를 쓰면 [5..8) 3바이트 + [0..2) 2바이트로 쪼개진다
    const uint8_t src5b[5] = {6, 7, 8, 9, 10};
    bool split_ok = (rb2_write(&bq, src5b, 5) == 5u);
    split_ok &= (rb2_used(&bq) == 7u);
    split_ok &= (rb2_read(&bq, dst, 7) == 7u);
    uint8_t exp4[7] = {4, 5, 6, 7, 8, 9, 10};
    for (int i = 0; i < 7; ++i) if (dst[i] != exp4[i]) split_ok = false;
    T("Q4 랩 구간을 가로지르는 write/read 가 정확", split_ok);

    // 부분 성공: 8칸에 6개가 차 있을 때 5바이트 write -> 2바이트만
    rb2_init(&bq, bsto, 8);
    const uint8_t src6[6] = {1, 2, 3, 4, 5, 6};
    rb2_write(&bq, src6, 6);
    T("Q4 공간이 모자라면 들어가는 만큼만 (6/8 찬 뒤 5 요청 -> 2)",
      rb2_write(&bq, src5, 5) == 2u && rb2_free(&bq) == 0u);
    T("Q4 가득 찬 뒤 write 는 0", rb2_write(&bq, src5, 5) == 0u);

    T("Q4 n=0 은 write/read 모두 0 (인덱스 변화 없음)",
      rb2_write(&bq, src5, 0) == 0u && rb2_read(&bq, dst, 0) == 0u
      && rb2_used(&bq) == 8u);
    T("Q4 read 요청이 used 보다 크면 있는 만큼만",
      rb2_read(&bq, dst, 100) == 8u && rb2_used(&bq) == 0u);

    // -------- Q5 : 인덱스 오버플로 안전성 --------
    printf("\n[Q5] ★ 인덱스 오버플로 — head 가 0xFFFFFFFF 를 넘어간다\n");
    uint8_t osto[16] = {0};
    rb2_t   oq       = {0};
    rb2_init(&oq, osto, 16);
    oq.head = oq.tail = 0xFFFFFFFCu;   // 랩까지 4 남은 지점으로 강제 세팅

    uint8_t src12[12] = {0};
    for (int i = 0; i < 12; ++i) src12[i] = (uint8_t)(0x30 + i);
    bool ov_ok = (rb2_write(&oq, src12, 12) == 12u);
    ov_ok &= (oq.head == 0x00000008u);             // 0xFFFFFFFC + 12 = 8 (mod 2^32)
    T("Q5 head 가 0xFFFFFFFC -> 0x00000008 로 랩", ov_ok);
    T("Q5 랩 이후에도 used == 12 (head-tail 부호없는 뺄셈)",
      rb2_used(&oq) == 12u && rb2_free(&oq) == 4u);

    uint8_t odst[12] = {0};
    bool data_ok = (rb2_read(&oq, odst, 12) == 12u);
    for (int i = 0; i < 12; ++i) if (odst[i] != (uint8_t)(0x30 + i)) data_ok = false;
    T("Q5 랩을 가로지른 12바이트 데이터가 그대로 복원", data_ok);
    T("Q5 소비 후 empty (head==tail, 둘 다 0x00000008)",
      rb2_used(&oq) == 0u && oq.head == oq.tail);

    // 1바이트씩 왕복시켜도 동일 — push/pop 경로도 오버플로 안전한가
    oq.head = oq.tail = 0xFFFFFFFFu;
    bool ov1_ok = rb2_push(&oq, 0x5A) && (oq.head == 0u) && (rb2_used(&oq) == 1u);
    ov1_ok &= rb2_pop(&oq, &v) && (v == 0x5A) && (rb2_used(&oq) == 0u);
    T("Q5 push/pop 경로도 0xFFFFFFFF -> 0 랩에서 정확", ov1_ok);

    // -------- Q6 : peek_contig --------
    printf("\n[Q6] rb2_peek_contig — 연속 구간 (zero-copy 예고편)\n");
    uint8_t psto[8] = {0};
    rb2_t   pq      = {0};
    rb2_init(&pq, psto, 8);
    const uint8_t *p = NULL;

    T("Q6 빈 링: 0 리턴 + *ptr 은 NULL",
      rb2_peek_contig(&pq, &p) == 0u && p == NULL);

    const uint8_t src6b[6] = {1, 2, 3, 4, 5, 6};
    rb2_write(&pq, src6b, 6);
    rb2_read(&pq, dst, 4);                  // tail=4
    const uint8_t src4[4] = {7, 8, 9, 10};
    rb2_write(&pq, src4, 4);                // head=10 -> 랩 구간에 걸친다

    uint32_t c1 = rb2_peek_contig(&pq, &p);
    // (&& 로 이어 붙여 단락 평가시킨다 — 미구현 스텁에서 p 가 NULL 이어도 안전)
    bool pk_ok = (rb2_used(&pq) == 6u) && (c1 == 4u) && (p == psto + 4)
              && (p[0] == 5 && p[1] == 6 && p[2] == 7 && p[3] == 8);
    T("Q6 랩 지점에서 잘려서 리턴 (used 6 이지만 연속은 4)", pk_ok);

    rb2_read(&pq, dst, 4);                  // tail=8 -> off 0
    uint32_t c2 = rb2_peek_contig(&pq, &p);
    bool pk2_ok = (c2 == 2u) && (p == psto + 0) && (p[0] == 9 && p[1] == 10);
    T("Q6 랩 이후 남은 2바이트는 버퍼 앞쪽에서 연속", pk2_ok);

    rb2_read(&pq, dst, 2);
    T("Q6 전부 소비하면 다시 0", rb2_peek_contig(&pq, &p) == 0u);

    // -------- 결과 --------
    printf("\n== 레벨 2 — 마스킹과 free-running 인덱스 ==  PASS %d / FAIL %d\n",
           g_pass, g_fail);
    return g_fail ? 1 : 0;
}
