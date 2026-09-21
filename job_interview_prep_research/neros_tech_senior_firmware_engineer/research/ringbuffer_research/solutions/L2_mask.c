// L2_mask.c  —  SOLUTION (정답 + 해설)
// 레벨 2 — 마스킹과 free-running 인덱스  —  Q1~Q6
// ---------------------------------------------------------------------------
// 빌드/실행:  make sol N=L2_mask
//   또는:     cc -std=c11 -Wall -Wextra -O0 -g solutions/L2_mask.c -o build/L2_mask_sol && ./build/L2_mask_sol
//
// 이 레벨에서 배우는 것:
//   - `idx % size` 를 `idx & (size-1)` 로 바꾸는 법 (size 가 2의 거듭제곱일 때만 성립).
//     Cortex-M0/M0+ 에는 하드웨어 나눗셈기가 없다 -> `%` 는 __aeabi_uidivmod 호출로
//     수십 사이클. `& mask` 는 1사이클 AND. ISR 안에서 이 차이는 그대로 지연이 된다.
//   - count 필드를 없애고 `used = head - tail` 로 계산하는 법.
//     레벨 0/1 에서 push/pop 마다 갱신하던 세 번째 상태(count)가 사라진다 ->
//     나중에 SPSC lock-free 로 갈 때 "생산자는 head 만, 소비자는 tail 만 쓴다"는
//     구조가 성립한다. count 가 남아 있으면 둘 다 쓰는 변수가 생겨 락이 필요해진다.
//   - head/tail 을 마스킹해서 저장하지 않고 계속 증가(free-running)시키면,
//     **부호 없는 정수 오버플로가 랩어라운드를 자동으로 해결**한다.
//     empty = (head==tail), full = (head-tail==size) 로 구분되므로 레벨 0 처럼
//     "한 칸을 비워 두는" 낭비가 없다. 용량 = size 전부.
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
// head/tail 은 "지금까지 몇 바이트를 넣었/뺐는가"를 세는 누적 카운터다.
// 절대 마스킹해서 저장하지 않는다 (마스킹은 buf 를 만질 때만).
//   - 저장할 때 마스킹하면 head 와 tail 이 둘 다 0..size-1 안에 갇혀서
//     "비었다"와 "가득 찼다"가 똑같이 head==tail 로 보인다 -> 구별하려면 한 칸을
//     버리거나 count 를 따로 들고 있어야 한다. (그게 레벨 0/1)
//   - 마스킹하지 않으면 used = head - tail 이 0..size 를 온전히 표현하므로
//     둘을 구별할 수 있고 용량도 size 전부를 쓴다.
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
 * ------------------------------------------------------------------------- */
// n=8 (0b1000), n-1=7 (0b0111) -> AND = 0. 비트가 2개 이상이면 AND 가 0 이 아니다.
// n=0 은 0 & 0xFFFFFFFF == 0 이라 통과해 버리므로 n != 0 을 따로 검사해야 한다.
// (이 한 줄을 빼먹으면 size=0 인 링이 만들어지고 mask=0xFFFFFFFF 로 버퍼 밖을 친다)
bool rb2_is_pow2(uint32_t n) {
    return n != 0u && (n & (n - 1u)) == 0u;
}

// init 은 "잘못된 설정을 런타임 진입 전에 막는 관문"이다. 펌웨어에서 size 는 보통
// 컴파일타임 상수이므로, 여기에 더해 static_assert 로 빌드 시점에 막는 것이 정석.
bool rb2_init(rb2_t *q, uint8_t *buf, uint32_t size) {
    if (!q || !buf) return false;
    if (!rb2_is_pow2(size)) return false;   // 0 과 비(非)2의거듭제곱을 한 번에 거부
    q->buf  = buf;
    q->size = size;
    q->mask = size - 1u;                    // 8 -> 0b0111
    q->head = 0u;
    q->tail = 0u;
    return true;
}

/* ---------------------------------------------------------------------------
 * Q2.  1바이트 push / pop  —  용량 = size 전부
 *   KO: full 은 (head - tail) == size, empty 는 head == tail.
 *       push 는 buf[head & mask] 에 쓰고 head++ (마스킹하지 않은 채로 증가),
 *       pop 은 buf[tail & mask] 를 읽고 tail++.
 *   EN: Single-byte push/pop; capacity is the full size (no wasted slot).
 *   ex: size 8 -> 8개 모두 push 성공, 9번째 push 는 false
 *   hint: 인덱스 자체는 절대 마스킹하지 않는다. 버퍼 첨자에만 & mask.
 * ------------------------------------------------------------------------- */
// used 가 size 와 같으면 가득 찬 것. 레벨 0 은 여기서 size-1 이 한계였다.
// 주의: q->head++ 를 q->head = (q->head+1) & q->mask 로 바꾸면 이 구조가 무너진다.
bool rb2_push(rb2_t *q, uint8_t v) {
    if (!q || !q->buf) return false;
    if ((uint32_t)(q->head - q->tail) == q->size) return false;  // full
    q->buf[q->head & q->mask] = v;   // 접근할 때만 마스킹
    q->head++;                       // free-running (오버플로해도 OK, 아래 Q5 참고)
    return true;
}

// out 을 건드리기 전에 empty 검사부터. 실패 경로에서 *out 을 쓰면 호출자의 변수를
// 오염시키고, 스텁 단계에서는 NULL 역참조로 크래시한다.
bool rb2_pop(rb2_t *q, uint8_t *out) {
    if (!q || !q->buf || !out) return false;
    if (q->head == q->tail) return false;      // empty
    *out = q->buf[q->tail & q->mask];
    q->tail++;
    return true;
}

/* ---------------------------------------------------------------------------
 * Q3.  used / free  —  count 필드 없이 계산
 *   KO: used = head - tail (부호 없는 뺄셈), free = size - used.
 *       head 가 tail 보다 "숫자상" 작아도(오버플로 후) mod 2^32 뺄셈이라 정확하다.
 *   EN: Derive occupancy from the two free-running indices; no count field.
 *   ex: head=10, tail=4 -> used 6, size 8 이면 free 2
 *   hint: 절대 (int)로 캐스팅하지 말 것. signed 오버플로는 UB 다.
 * ------------------------------------------------------------------------- */
// count 필드를 없애는 것이 핵심. count 는 push 와 pop 이 **둘 다** 쓰는 변수라서
// 멀티컨텍스트(ISR + 태스크)에서는 원자적 갱신이 필요하다. head-tail 로 유도하면
// 생산자는 head 만, 소비자는 tail 만 쓰므로 write-write 경합이 구조적으로 없어진다.
uint32_t rb2_used(const rb2_t *q) {
    if (!q) return 0u;
    return q->head - q->tail;   // uint32_t 뺄셈 = mod 2^32 (C11 6.2.5p9: UB 아님)
}

uint32_t rb2_free(const rb2_t *q) {
    if (!q) return 0u;
    return q->size - (q->head - q->tail);
}

/* ---------------------------------------------------------------------------
 * Q4.  벌크 write / read  —  memcpy 2회로 랩 구간 분할
 *   KO: 들어가는 만큼만 쓰고(부분 성공 허용) 실제 처리한 바이트 수를 리턴한다.
 *       랩 지점을 가로지르면 [head..size) 와 [0..) 두 조각이므로 memcpy 를 2번.
 *       n=0 이거나 공간이 0 이면 0 을 리턴한다.
 *   EN: Bulk copy in at most two memcpy calls (tail chunk + head chunk).
 *   ex: size 8, head=5 에 5바이트 write -> 3바이트 + 2바이트 두 조각
 *   hint: first = min(n, size - (head & mask)) 가 첫 조각의 길이.
 * ------------------------------------------------------------------------- */
// 왜 바이트 루프(`for i: buf[(head+i)&mask] = src[i]`) 대신 memcpy 2회인가?
//   - 바이트 루프는 원소마다 AND + 스토어. memcpy 는 워드 단위로 복사하고
//     컴파일러/libc 가 정렬·언롤까지 해 준다. 64바이트 이상에서 3~10배 차이.
//   - "조각이 최대 2개"라는 건 링버퍼의 불변식이다: 한 번 랩하면 그 뒤는 tail 을
//     따라잡기 전에 멈추므로, 쓸 수 있는 영역은 절대 3조각이 되지 않는다.
uint32_t rb2_write(rb2_t *q, const uint8_t *src, uint32_t n) {
    if (!q || !q->buf || !src || n == 0u) return 0u;

    uint32_t space = rb2_free(q);
    if (n > space) n = space;          // 부분 성공: 들어가는 만큼만
    if (n == 0u) return 0u;

    uint32_t off   = q->head & q->mask;      // 물리적 쓰기 시작 위치
    uint32_t first = q->size - off;          // 버퍼 끝까지 남은 연속 길이
    if (first > n) first = n;

    memcpy(q->buf + off, src, first);                    // 1) 끝까지
    if (n > first) memcpy(q->buf, src + first, n - first); // 2) 처음으로 돌아와서

    q->head += n;                     // 마스킹 없이 그대로 전진
    return n;
}

uint32_t rb2_read(rb2_t *q, uint8_t *dst, uint32_t n) {
    if (!q || !q->buf || !dst || n == 0u) return 0u;

    uint32_t used = rb2_used(q);
    if (n > used) n = used;
    if (n == 0u) return 0u;

    uint32_t off   = q->tail & q->mask;
    uint32_t first = q->size - off;
    if (first > n) first = n;

    memcpy(dst, q->buf + off, first);
    if (n > first) memcpy(dst + first, q->buf, n - first);

    q->tail += n;
    return n;
}

/* ---------------------------------------------------------------------------
 * Q5.  ★ 인덱스 오버플로 안전성 (free-running 의 핵심 장점)
 *   KO: head 가 0xFFFFFFFF 를 넘어 0 으로 랩해도 used = head - tail 은 여전히
 *       정확하다. 별도의 구현 함수는 없고, Q2~Q4 의 코드가 그대로 견뎌야 한다.
 *       테스트에서 head=tail=0xFFFFFFFC 로 강제 세팅한 뒤 12바이트를 왕복시킨다.
 *   EN: Unsigned index wrap is harmless: head-tail is exact modulo 2^32.
 *   ex: head=0xFFFFFFFC + 12 = 0x00000008, tail=0xFFFFFFFC -> used == 12
 *   hint: C11 6.2.5p9 — unsigned 연산은 "2^N 으로 나눈 나머지"로 정의된다.
 *         signed 였다면 오버플로가 UB 라서 컴파일러가 마음대로 최적화해 버린다.
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
// 실무 용도: `HAL_UART_Transmit_DMA(ptr, len)` 처럼 하드웨어에 버퍼 주소를 바로
// 넘길 때 쓴다. 중간 복사 버퍼가 사라지고 RAM 과 CPU 를 동시에 아낀다.
// 소비가 끝나면 tail 을 len 만큼 전진시켜 주는 짝 함수(rb2_consume)가 따라붙는다.
uint32_t rb2_peek_contig(rb2_t *q, const uint8_t **ptr) {
    if (!q || !q->buf || !ptr) return 0u;
    uint32_t used = rb2_used(q);
    if (used == 0u) { *ptr = NULL; return 0u; }

    uint32_t off   = q->tail & q->mask;
    uint32_t contig = q->size - off;     // 버퍼 끝까지
    if (contig > used) contig = used;    // 데이터가 더 적으면 거기까지

    *ptr = q->buf + off;
    return contig;
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
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

// ===========================================================================
// 정리 — 레벨 2 의 트레이드오프 (면접에서 한 문장씩 말할 수 있어야 한다)
// ---------------------------------------------------------------------------
// 1) `& (size-1)` vs `%`
//    - 2의 거듭제곱일 때만 둘이 같다. Cortex-M3/M4 는 UDIV 가 2~12사이클,
//      M0/M0+ 는 하드웨어 나눗셈기가 아예 없어 라이브러리 호출(수십 사이클).
//      AND 는 어느 코어든 1사이클. UART RX ISR 처럼 바이트마다 도는 코드에서는
//      이 차이가 최대 인터럽트 지연(latency)에 그대로 더해진다.
//    - 컴파일러가 `% size` 를 자동으로 AND 로 바꿔주는 건 size 가 **컴파일타임
//      상수**일 때뿐이다. 구조체 필드(q->size)는 런타임 값이라 못 바꾼다.
//
// 2) 인덱스를 마스킹해서 "저장"하지 않고 버퍼 접근 시에만 마스킹하는 이유
//    - 저장까지 마스킹하면 head,tail ∈ [0,size) 라서 empty 와 full 이 둘 다
//      head==tail 로 보인다. 그래서 레벨 0 은 한 칸을 비워 두거나(용량 size-1)
//      count 필드를 따로 들었다.
//    - free-running 이면 head==tail 이 empty, head-tail==size 가 full 로
//      명확히 갈린다 -> **한 칸을 버릴 필요가 없다.** 1KB 링에서 1바이트가
//      아깝냐가 아니라, count 라는 공유 변수를 없애는 게 진짜 이득이다
//      (생산자는 head 만, 소비자는 tail 만 쓴다 -> lock-free SPSC 로 직행).
//
// 3) 왜 오버플로가 안전한가
//    - C11 6.2.5p9: 부호 없는 정수 연산은 "결과를 2^N 으로 나눈 나머지"로
//      **정의되어** 있다. 즉 0xFFFFFFFC + 12 == 8 은 UB 가 아니라 규정된 동작.
//      따라서 head - tail 은 랩 여부와 무관하게 항상 실제 사용량과 같다
//      (단, used <= size <= 2^31 이어야 한다. 그렇지 않을 만큼 오래 벌어질 수는 없다).
//    - **signed 로 하면 UB.** int head; 로 두면 오버플로가 정의되지 않아
//      컴파일러가 "head 는 절대 음수가 안 된다"고 가정하고 검사를 지워버린다.
//      -fwrapv 를 켜야 겨우 동작하는, 이식성 없는 코드가 된다.
//      인덱스는 언제나 uint32_t / size_t 로.
//
// 4) 대가
//    - size 가 2의 거듭제곱으로 제한된다. 1000바이트가 필요하면 1024 를 잡아야
//      하고 (24바이트 낭비), 반대로 600바이트 예산이면 512 로 줄여야 한다.
//      MCU 에서 RAM 이 빠듯할 때 이 제약이 실제로 아프다 -> 그때는 레벨 0/1 의
//      `%` 또는 "size 넘으면 0 으로 되돌리는 if" 방식을 쓴다
//      (`if (++i == size) i = 0;` 은 나눗셈 없이 되지만 free-running 은 포기).
// ===========================================================================
