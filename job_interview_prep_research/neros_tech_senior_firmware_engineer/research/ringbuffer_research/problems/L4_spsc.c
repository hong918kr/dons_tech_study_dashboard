// L4_spsc.c  —  PRACTICE STUB (직접 채워넣기)
// 레벨 4 — SPSC lock-free 링 (인터럽트 ↔ 메인 루프)  —  Q1~Q7
// ---------------------------------------------------------------------------
// 빌드/실행:  make prob N=L4_spsc
//   또는:     cc -std=c11 -Wall -Wextra -O0 -g problems/L4_spsc.c -o build/L4_spsc_prob && ./build/L4_spsc_prob
//
// 각 함수의 '// TODO' 를 구현하고 다시 실행 -> [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일/실행은 되며 대부분 FAIL 로 뜬다.)
//
// rb4_push_broken() 과 isr_tick() 은 **이미 완성되어 있다** — 구현 과제가 아니라
// Q5/Q6 의 재료다. 건드리지 말고 무엇을 보여주는지만 읽어라.
//
// 이 레벨에서 배우는 것:
//   - 왜 ISR 안에서는 mutex 를 잡을 수 없는가 (우선순위 역전 / 시스템 정지 / RTOS 규약)
//   - acquire / release 가 "정확히" 무엇을 보장하는가 (한 쪽의 쓰기가 다른 쪽에 보이는 순간)
//   - `volatile` 이 왜 답이 아닌가 (컴파일러는 막지만 CPU 재배치는 못 막는다)
//   - all-or-nothing 쓰기와 drop 카운터 — 반쪽짜리 레코드를 만들지 않는 규율
//   - 보존법칙(conservation law)으로 링버퍼를 검증하는 법
//
// 참고: 같은 워크스페이스의 practice/solutions/01_ring_logging.c 는 이 자료구조를
//       "로깅 시스템"에 응용한 것이다. 이 파일은 그 앞단계 — 자료구조 자체가 아니라
//       **메모리 오더링**이 주제다. 그래서 함수 접두사도 rb4_ 로 따로 간다.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>

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
// 생각해볼 문제 1 — 왜 ISR 에서 mutex 를 못 잡는가
// ---------------------------------------------------------------------------
// 코드를 짜기 전에 스스로 답해보라 (정답은 solutions/L4_spsc.c 의 '해설 1'):
//   - ISR 이 mutex_lock() 을 불렀는데 그 락을 낮은 우선순위 태스크가 쥐고 있다.
//     무슨 일이 일어나는가? 그 태스크는 언제 깨어날 수 있는가?
//   - mutex_lock 의 본질은 "못 잡으면 블록"이다. ISR 에는 무엇이 없어서 블록할 수 없나?
//   - FreeRTOS 는 왜 xQueueSend 말고 xQueueSendFromISR 을 따로 두는가?
//   - 락을 쓰면 인터럽트 지연(latency)은 어떻게 되는가?
//
// 이 큐가 락 없이 성립하는 구조적 이유 한 줄:
//   생산자만 head 를 store, 소비자만 tail 을 store -> write-write 경합이 **없다**.
//   남는 문제는 오직 "순서(ordering)" 뿐이고, 그게 이 파일의 주제다.
// ===========================================================================

// ===========================================================================
// 생각해볼 문제 2 — `volatile head` 로 바꾸면 무엇이 깨지는가
// ---------------------------------------------------------------------------
//       q->buf[i] = v;                 // 평범한 store
//       vol_head  = h + 1;             // volatile store
//   - 컴파일러는 이 두 줄의 순서를 바꿔도 되는가? (volatile 은 무엇들 사이의
//     순서만 지켜주는가?)
//   - CPU 의 store buffer 가 두 store 의 커밋 순서를 바꾸면? volatile 이
//     DMB 같은 배리어 명령을 만들어 주는가?
//   - 그런데 왜 현업의 수많은 Cortex-M 코드가 volatile 만으로 잘 도는가?
//     그 코드를 Cortex-A/멀티코어로 옮기면?
// 정답은 solutions/L4_spsc.c 의 '해설 2'.
// ===========================================================================

// ===========================================================================
// 생각해볼 문제 3 — stdatomic 이 없던 시절엔 어떻게 했나
// ---------------------------------------------------------------------------
//   (1) volatile + __DMB()  (2) __disable_irq() 크리티컬 섹션
//   - (1) 은 아래에서 구현할 어떤 memory_order 를 손으로 짠 것인가?
//   - (2) 로 데이터 복사 구간까지 감싸면 무엇이 나빠지는가? (힌트: 1KB 복사)
// 정답은 solutions/L4_spsc.c 의 '해설 3'.
// ===========================================================================

// ===========================================================================
// 해설 4 — count 필드를 두지 않고 free-running head/tail 만 쓰는 이유 (읽고 시작할 것)
// ---------------------------------------------------------------------------
//   - count(또는 used) 를 구조체에 저장하면 push 는 count++, pop 은 count--,
//     즉 **양쪽이 같은 변수를 쓴다** -> write-write 경합 -> read-modify-write 를
//     원자적으로 해야 하고(CAS/LDREX-STREX), SPSC 무락의 전제가 무너진다.
//   - head/tail 을 마스킹하지 않고 계속 증가시키면(free-running)
//       used = (uint32_t)(head - tail)
//     이 부호없는 뺄셈이라 32비트 랩어라운드가 일어나도 정확하다.
//     (head 가 0xFFFFFFFF 를 넘어 0 이 되어도 head - tail 은 여전히 맞다.)
//   - 그래서 "full 과 empty 를 구분하려고 한 칸을 비워두는" 고전적 트릭이
//     필요 없다. 용량 = size 전부.
//         empty: head == tail        full: (head - tail) == size
//   - 전제: size 가 2의 거듭제곱이어야 buf[idx & mask] 가 성립한다.
//     (2의 거듭제곱이 아니면 랩 지점에서 인덱스가 어긋난다.)
// ===========================================================================

// ===========================================================================
// 해설 5 — false sharing (멀티코어에서만 의미 있음 / 읽을거리)
// ---------------------------------------------------------------------------
// head 와 tail 이 같은 캐시라인(보통 64B)에 있으면, 생산자 코어가 head 를 쓸
// 때마다 그 라인 전체가 무효화되어 소비자 코어의 tail 읽기가 캐시 미스가 된다.
// 서로 다른 변수인데 "라인이 같아서" 핑퐁이 일어나는 것 = false sharing.
// 처리량이 수 배 떨어질 수 있다.
//
// 해법 — 두 인덱스를 각자의 캐시라인으로 밀어낸다 (C11 <stdalign.h>):
//
//     #include <stdalign.h>
//     #define CACHELINE 64
//     typedef struct {
//         uint8_t  *buf;
//         uint32_t  size, mask;
//         alignas(CACHELINE) atomic_uint_fast32_t head;   // 생산자 전용 라인
//         uint32_t  dropped;                              // 생산자만 만지니 같은 라인 OK
//         alignas(CACHELINE) atomic_uint_fast32_t tail;   // 소비자 전용 라인
//         char      _pad[CACHELINE];                      // 뒤 객체와의 공유도 차단
//     } rb4_padded_t;
//
// 주의: MCU(Cortex-M0/M3/M4) 에는 데이터 캐시가 없으므로 이 패딩은 **RAM 낭비**일
//       뿐 이득이 없다. 반대로 Linux/Cortex-A/x86 타깃의 고빈도 큐에서는 필수다.
//       => "같은 코드가 두 타깃에서 돈다"면 이 패딩은 빌드 옵션으로 켜고 끄는 게 맞다.
// 아래 rb4_t 는 학습용이라 패딩 없이 간다.
// ===========================================================================

typedef struct {
    uint8_t              *buf;
    uint32_t              size;      /* 2의 거듭제곱 */
    uint32_t              mask;      /* size - 1 */
    atomic_uint_fast32_t  head;      /* 생산자(ISR)만 store */
    atomic_uint_fast32_t  tail;      /* 소비자(main)만 store */
    uint32_t              dropped;   /* 생산자만 갱신 → 원자화 불필요 */
} rb4_t;

/* ---------------------------------------------------------------------------
 * Q1.  init / used / free  — free-running 인덱스의 기본 산수
 *   KO: size 가 2의 거듭제곱(그리고 >= 2)이 아니면 init 은 false.
 *       used = (uint32_t)(head - tail), free = size - used.
 *       한 칸도 버리지 않으므로 빈 링의 free 는 size 와 정확히 같다.
 *   EN: Init with a power-of-two size; used/free derived from free-running indices.
 *   ex: size 8 -> free 8, 3바이트 넣으면 used 3 / free 5
 *   hint: (size & (size-1)) == 0  <=>  size 는 2의 거듭제곱
 * ------------------------------------------------------------------------- */
// 원자 객체는 atomic_init() 으로 초기화한다 (C11 규약).
// init 은 큐가 다른 컨텍스트에 공유되기 **전에** 단독으로 끝나야 한다.
bool rb4_init(rb4_t *q, uint8_t *buf, uint32_t size) {
    (void)q; (void)buf; (void)size;
    // TODO: NULL 검사 -> pow2 && size>=2 검사 -> buf/size/mask/dropped 설정
    //       -> atomic_init 으로 head/tail 을 0 으로
    return false;  // placeholder
}

// used/free 는 생산자·소비자 **양쪽**에서 불릴 수 있다. 그래서 어느 한쪽 기준으로
// relaxed 를 쓸 수 없고, 두 인덱스 모두 acquire 로 읽는다.
// (그 결과가 호출자에 따라 '보수적으로' 틀리는 이유는 Q7 에서 다룬다.)
uint32_t rb4_used(const rb4_t *q) {
    (void)q;
    // TODO: head/tail 을 acquire load -> (uint32_t)(h - t)
    return 0;  // placeholder
}

uint32_t rb4_free(const rb4_t *q) {
    (void)q;
    // TODO: size - used
    return 0;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q2.  push / pop  — acquire/release 한 쌍의 최소 형태
 *   KO: 생산자는 데이터를 먼저 쓰고 head 를 release store 로 "공개"한다.
 *       소비자는 head 를 acquire load 로 본 뒤에만 그 데이터를 읽는다.
 *       이 한 쌍이 "데이터 쓰기 → 인덱스 공개" 순서를 컴파일러와 CPU 양쪽에 강제한다.
 *   EN: Publish payload before advancing the index (release), observe the index
 *       before reading the payload (acquire).
 *   ex: 8칸 링에 8개 push 성공, 9번째는 false / 랩어라운드 후에도 FIFO 유지
 * ------------------------------------------------------------------------- */

// [생산자 전용 — ISR 컨텍스트에서 호출된다고 가정]
//
// ★ 오더링 규칙 (외울 것):
//     내 인덱스   = relaxed load   (내가 유일한 writer라 최신값을 이미 안다)
//     상대 인덱스 = acquire load   (상대가 release 로 공개한 것을 보기 위해)
//     데이터 복사 -> 그 다음 내 인덱스 = release store  (순서가 정확성 조건이다)
//
// acquire/release 가 정확히 보장하는 것 한 줄:
//   release store(head) "이전"의 모든 메모리 쓰기는, 그 head 값을 acquire load 로
//   관측한 쪽에게 반드시 보인다. 그 이상도 이하도 아니다.
bool rb4_push(rb4_t *q, uint8_t v) {
    (void)q; (void)v;
    // TODO: head = relaxed load, tail = acquire load
    //       full((h-t)==size) 이면 false (덮어쓰기 금지)
    //       buf[h & mask] = v  -> 그 다음 head 를 h+1 로 release store
    //       각 줄에 '왜 이 오더인지' 자기 말로 주석을 달아볼 것.
    return false;  // placeholder
}

// [소비자 전용 — 메인 루프/저우선순위 태스크]
// 생산자와 정확히 대칭이다: 내 인덱스(tail)는 relaxed, 상대 인덱스(head)는 acquire.
bool rb4_pop(rb4_t *q, uint8_t *out) {
    (void)q; (void)out;
    // TODO: tail = relaxed load, head = acquire load
    //       h == t 이면 empty -> out 을 건드리지 말고 false
    //       *out = buf[t & mask] -> 그 다음 tail 을 t+1 로 release store
    return false;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q3.  rb4_write_all  — all-or-nothing 벌크 쓰기
 *   KO: n 바이트가 전부 들어갈 때만 쓴다. 공간이 모자라면 **한 바이트도 쓰지 않고**
 *       dropped++ 후 false. head 는 마지막에 n 만큼 한 번에 전진시킨다.
 *   EN: Copy all n bytes or none; on overflow bump dropped and return false.
 *   ex: 8칸 링에 6바이트가 들어있을 때 write_all(5) -> false, used 는 그대로 6
 *   hint: 여러 번의 rb4_push 로 구현하면 안 된다 — 중간에 실패하면 반쪽이 남는다.
 * ------------------------------------------------------------------------- */
// 왜 부분 쓰기를 금지하는가 (프레임 기반 로깅/텔레메트리의 핵심 규율):
//   링에는 보통 "레코드"가 들어간다: [len][type][payload...][crc]
//   16바이트 레코드 중 9바이트만 들어가고 잘리면, 소비자(파서)는 그 9바이트를
//   다음 레코드의 헤더로 오독한다 -> 한 번 어긋나면 그 뒤 전부가 쓰레기가 된다.
//   "로그 한 줄을 통째로 잃는 것"은 복구 가능하지만 "스트림이 깨지는 것"은 아니다.
//   그래서 정책은 drop-new: 새 레코드를 통째로 버리고, 몇 개 버렸는지 숫자로 남긴다.
//
// 또한 head 를 **마지막에 단 한 번** 전진시키는 것이 원자성의 전부다:
//   소비자 입장에서 레코드는 "보이지 않거나(head 전진 전) 전부 보이거나(후)" 둘뿐.
bool rb4_write_all(rb4_t *q, const uint8_t *src, uint32_t n) {
    (void)q; (void)src; (void)n;
    // TODO: n==0 은 true (아무 일도 안 함)
    //       avail = size - (h - t)
    //       n > avail 이면: dropped++ 후 false  (버퍼는 한 바이트도 건드리지 않는다)
    //       아니면: n 바이트 전부 복사 -> head 를 h+n 으로 **한 번에** release store
    // 주의: rb4_push 를 n 번 호출하는 식으로 짜면 안 된다 — 중간에 실패하면
    //       반쪽 레코드가 링에 남는다.
    return false;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q3b. rb4_read  — 있는 만큼 꺼내기 (소비자 측 드레인)
 *   KO: 최대 n 바이트를 dst 로 옮기고 실제로 옮긴 개수를 반환. 비면 0.
 *       tail 도 마지막에 한 번만 전진시킨다 (생산자에게 공간을 한꺼번에 돌려줌).
 *   EN: Drain up to n bytes; returns how many were actually copied.
 *   ex: 10바이트 들어있는 링에 rb4_read(dst, 4) -> 4, 남은 6은 링에 유지
 * ------------------------------------------------------------------------- */
// write_all 과 달리 읽기는 "있는 만큼"이 맞다: 소비자는 언제나 진행할 수 있어야
// 하고(그래야 생산자가 막히지 않는다), 레코드 경계 복원은 파서의 책임이다.
uint32_t rb4_read(rb4_t *q, uint8_t *dst, uint32_t n) {
    (void)q; (void)dst; (void)n;
    // TODO: tail=relaxed, head=acquire -> used=(h-t), cnt=min(n,used)
    //       cnt 바이트 복사 (dst[i] = buf[(t+i) & mask])
    //       -> tail 을 t+cnt 로 **한 번에** release store -> cnt 반환
    return 0;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q4.  rb4_dropped  — 버린 레코드 수
 *   KO: write_all 이 실패한 횟수를 그대로 돌려준다. 성공한 호출은 올리지 않는다.
 *   EN: Number of records rejected because they did not fit.
 *   ex: 공간 부족으로 3번 실패 -> 3
 * ------------------------------------------------------------------------- */
// 왜 dropped 가 atomic 이 아닌가: 이 필드는 **생산자만** 읽고 쓴다. 소비자/디버거가
// 읽는 건 통계 목적이라 한 틱 늦게 보여도 무방하다. 굳이 원자화하면 ISR 안에서
// read-modify-write 비용만 늘어난다. (엄밀히는 소비자 스레드가 읽을 때 data race
// 지만, 진단용 카운터라 실무에서는 relaxed atomic 또는 그냥 uint32_t 로 둔다.)
uint32_t rb4_dropped(const rb4_t *q) {
    (void)q;
    // TODO: q->dropped 를 그대로 반환 (NULL 이면 0)
    return 0;  // placeholder
}

// ===========================================================================
// Q6 재료 — 일부러 틀린 push (구현 과제 아님. 완성된 채로 제공)
// ---------------------------------------------------------------------------
// 무엇이 틀렸나: **head 를 먼저 올리고 데이터를 나중에 쓴다.**
//   atomic_store(head, h+1);      <- "n번 칸에 데이터 있음"이라고 선언
//   q->buf[h & mask] = v;         <- 그런데 실제로 쓰는 건 이 다음
//
// 단일 스레드에서는 절대 안 잡힌다:
//   pop 은 push 가 완전히 끝난 뒤에야 호출되므로 두 store 의 순서가 무의미하다.
//   그래서 유닛 테스트는 100% PASS 한다. 이게 이 버그가 무서운 이유다 —
//   "테스트가 통과했으니 맞다"는 추론이 여기서 무너진다.
//
// 실제로 깨지는 시나리오 (생산자=ISR, 소비자=메인 루프, 단일 코어 Cortex-M):
//   1) ISR: head 를 h+1 로 store   <- 여기서 ISR 이 끝나거나 선점당한다
//      (혹은 단순히 이 명령 직후에 더 높은 우선순위 IRQ 가 끼어든다)
//   2) main: rb4_pop -> head != tail 이므로 "데이터 있음"으로 판단
//   3) main: buf[t & mask] 를 읽는다  <- **아직 아무도 안 쓴 칸. 쓰레기 값.**
//   4) ISR 재개: 이제서야 buf 에 진짜 값을 쓴다  <- 이미 늦었다. 값 하나 유실 + 오염.
//
//   멀티코어/store buffer 가 있는 코어라면 인터럽트가 없어도 같은 일이 난다:
//   CPU 가 두 store 의 커밋 순서를 바꿔버리면 끝이다. release store 는 바로
//   그 재배치를 금지하기 위해 존재한다.
//
// 요약: 정상 push 의 "데이터 먼저 → 인덱스 나중" 은 취향이 아니라 **정확성 조건**이다.
//       그리고 그 조건은 단일 스레드 테스트로는 검증할 수 없다.
//       검증 수단은 코드 리뷰, TSan/모델 체커, 그리고 실기 스트레스 테스트뿐이다.
// ===========================================================================
bool rb4_push_broken(rb4_t *q, uint8_t v) {
    if (!q || !q->buf) return false;
    uint_fast32_t h = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint_fast32_t t = atomic_load_explicit(&q->tail, memory_order_acquire);
    if ((uint32_t)(h - t) == q->size) return false;

    atomic_store_explicit(&q->head, h + 1, memory_order_release);  /* ★ 순서 뒤바뀜! */
    q->buf[h & q->mask] = v;                                       /* ★ 너무 늦다 */
    return true;
}

// ===========================================================================
// Q5 재료 — 인터럽트 시뮬레이션 (테스트 인프라. 완성된 채로 제공)
// ---------------------------------------------------------------------------
// 진짜 스레드/인터럽트 없이 "ISR 이 메인 루프 사이사이에 끼어든다"를 흉내낸다.
// isr_tick() 을 드레인 루프 중간중간에 호출하는 것으로 선점 지점을 표현한다.
// (함수 호출은 원자적이므로 여기서 잡히는 건 오더링 버그가 아니라 **회계 버그**다.
//  그래도 충분히 가치 있다: 인덱스 산수/랩/용량/드롭 경로의 오류는 전부 잡힌다.)
// ===========================================================================
#define ISR_SAMPLE_BYTES 4u

static uint32_t g_seq            = 0;   /* 센서 샘플 일련번호 */
static uint32_t g_produced_bytes = 0;   /* write_all 을 "시도"한 총 바이트 */

/* 센서 ISR 한 틱: 4바이트 샘플을 all-or-nothing 으로 밀어넣는다. */
static void isr_tick(rb4_t *q) {
    uint8_t s[ISR_SAMPLE_BYTES];
    s[0] = (uint8_t)(g_seq & 0xFF);
    s[1] = (uint8_t)((g_seq >> 8) & 0xFF);
    s[2] = 0xA5;                                  /* 마커 */
    s[3] = (uint8_t)(0x100u - (uint8_t)(s[0] + s[1] + s[2]));  /* 체크섬 */
    g_seq++;
    g_produced_bytes += ISR_SAMPLE_BYTES;         /* 시도한 양은 무조건 집계 */
    (void)rb4_write_all(q, s, ISR_SAMPLE_BYTES);  /* 실패하면 dropped 가 올라간다 */
}

/* Q7 데모용: 특정 시점의 (head, tail) 스냅샷으로 used 를 계산하는 순수 함수.
 * rb4_used() 내부의 산수와 동일하다 — "누가 언제 본 인덱스인가"만 달리 준다. */
static uint32_t used_from(uint_fast32_t h, uint_fast32_t t) {
    return (uint32_t)(h - t);
}

// ===========================================================================
// main : Q1~Q7
// ===========================================================================
int main(void) {   /* 이 아래 테스트 코드는 건드리지 말 것 */
    uint8_t  sto[8];
    uint8_t  tmp[64] = {0};
    rb4_t    q = {0};      /* stub 상태에서도 쓰레기 값을 안 읽도록 0 초기화 */
    uint8_t  v = 0;

    // ---------------------------------------------------------------------
    printf("\n[Q1] init (pow2 검사) / used / free / size\n");
    // ---------------------------------------------------------------------
    T("Q1 non-pow2(6) 은 init 거부", rb4_init(&q, sto, 6) == false);
    T("Q1 size<2(1) 도 거부",        rb4_init(&q, sto, 1) == false);
    T("Q1 buf==NULL 도 거부",        rb4_init(&q, NULL, 8) == false);
    T("Q1 pow2(8) 는 init 성공",     rb4_init(&q, sto, 8) == true);
    T("Q1 초기 상태: used 0 / free 8 (한 칸도 안 버린다)",
      rb4_used(&q) == 0 && rb4_free(&q) == 8 && q.size == 8 && q.mask == 7);

    bool p3 = rb4_push(&q, 1) && rb4_push(&q, 2) && rb4_push(&q, 3);
    T("Q1 3바이트 push 후 used 3 / free 5",
      p3 && rb4_used(&q) == 3 && rb4_free(&q) == 5);
    T("Q1 used + free == size 는 항상 성립",
      rb4_used(&q) + rb4_free(&q) == q.size);

    // ---------------------------------------------------------------------
    printf("\n[Q2] push / pop — FIFO 와 랩어라운드\n");
    // ---------------------------------------------------------------------
    rb4_init(&q, sto, 8);
    bool fill_ok = true;
    for (uint8_t i = 0; i < 8; ++i) fill_ok &= rb4_push(&q, (uint8_t)(0xA0 + i));
    T("Q2 용량 == size : 8개 전부 push 성공", fill_ok && rb4_free(&q) == 0);
    T("Q2 가득 찬 뒤 push 는 실패 (덮어쓰기 금지)", !rb4_push(&q, 0xFF));

    bool fifo_ok = true;
    for (uint8_t i = 0; i < 5; ++i)
        if (!rb4_pop(&q, &v) || v != (uint8_t)(0xA0 + i)) fifo_ok = false;
    T("Q2 pop 은 넣은 순서대로 (FIFO)", fifo_ok);

    /* 5칸 비운 자리에 5개 더 -> head/tail 이 size(8) 경계를 넘어 랩된다 */
    for (uint8_t i = 0; i < 5; ++i)
        if (!rb4_push(&q, (uint8_t)(0xB0 + i))) fifo_ok = false;
    const uint8_t exp_wrap[8] = {0xA5, 0xA6, 0xA7, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4};
    bool wrap_ok = fifo_ok;
    for (int i = 0; i < 8; ++i)
        if (!rb4_pop(&q, &v) || v != exp_wrap[i]) wrap_ok = false;
    T("Q2 wrap-around 후에도 FIFO 순서 보존", wrap_ok);
    T("Q2 빈 링에서 pop 은 false (out 은 건드리지 않음)",
      !rb4_pop(&q, &v) && rb4_used(&q) == 0);

    // ---------------------------------------------------------------------
    printf("\n[Q3] rb4_write_all — all-or-nothing (부분 쓰기 금지)\n");
    // ---------------------------------------------------------------------
    rb4_init(&q, sto, 8);
    const uint8_t rec6[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    const uint8_t rec5[5] = {0xDE, 0xAD, 0xBE, 0xEF, 0x99};
    T("Q3 6바이트 레코드는 빈 8칸 링에 들어간다",
      rb4_write_all(&q, rec6, 6) && rb4_used(&q) == 6 && rb4_free(&q) == 2);

    uint32_t used_before = rb4_used(&q);
    T("Q3 2칸 남은 링에 5바이트 -> false (부분 쓰기 없음)",
      rb4_write_all(&q, rec5, 5) == false);
    T("Q3 실패 후 used 가 그대로 (head 전진 안 함)",
      rb4_used(&q) == used_before && rb4_free(&q) == 2);

    /* 버퍼 내용 자체도 오염되지 않았는지 끝까지 확인 — 이게 이 Q 의 본론 */
    uint32_t got = rb4_read(&q, tmp, sizeof tmp);
    bool intact = (got == 6) && (memcmp(tmp, rec6, 6) == 0);
    T("Q3 실패한 write_all 은 버퍼 내용을 한 바이트도 바꾸지 않는다", intact);
    T("Q3 n==0 은 성공이고 아무 일도 일어나지 않는다",
      rb4_write_all(&q, rec5, 0) && rb4_used(&q) == 0);
    /* 왜 중요한가: 프레임 기반 로깅에서 반쪽짜리 레코드가 들어가면
     * 파서가 그 바이트들을 다음 레코드의 헤더로 오독해 스트림 전체가 깨진다. */

    // ---------------------------------------------------------------------
    printf("\n[Q4] dropped 카운터 누적\n");
    // ---------------------------------------------------------------------
    rb4_init(&q, sto, 8);
    T("Q4 초기 dropped == 0", rb4_dropped(&q) == 0);
    T("Q4 성공한 write_all 은 dropped 를 올리지 않는다",
      rb4_write_all(&q, rec6, 6) && rb4_dropped(&q) == 0);

    uint32_t d0 = rb4_dropped(&q);
    rb4_write_all(&q, rec5, 5);            /* 실패 1 */
    rb4_write_all(&q, rec5, 5);            /* 실패 2 */
    rb4_write_all(&q, rec5, 3);            /* 실패 3 (2칸뿐인데 3바이트) */
    T("Q4 실패 3번 -> dropped 가 정확히 3 증가", rb4_dropped(&q) == d0 + 3);
    T("Q4 드롭이 나도 기존 데이터는 살아있다 (drop-new 정책)",
      rb4_used(&q) == 6 && rb4_read(&q, tmp, 6) == 6 && memcmp(tmp, rec6, 6) == 0);
    T("Q4 dropped 는 드레인해도 리셋되지 않는다 (누적 진단값)",
      rb4_dropped(&q) == d0 + 3 && rb4_used(&q) == 0);

    // ---------------------------------------------------------------------
    printf("\n[Q5] ★ 인터럽트 시뮬레이션 + 보존법칙(conservation law)\n");
    // ---------------------------------------------------------------------
    // 시나리오: 센서 ISR 이 메인 루프 사이사이에 끼어들어 4바이트씩 밀어넣고,
    //           메인 루프는 가끔, 그것도 조금씩만 드레인한다 -> 반드시 넘친다.
    // 검증할 불변식:
    //     생산 시도 총량 == 소비한 양 + 링에 남은 양 + 드롭된 양
    // 한 바이트도 복제되거나 증발하지 않았음을 이 한 줄이 증명한다.
    // ---------------------------------------------------------------------
    uint8_t  big[16];
    rb4_t    isrq = {0};
    uint32_t consumed = 0;

    rb4_init(&isrq, big, 16);
    g_seq = 0;
    g_produced_bytes = 0;

    for (uint32_t round = 0; round < 200; ++round) {
        /* 한 라운드에 ISR 이 1~3번 끼어든다 (선점 횟수를 흔든다) */
        uint32_t nisr = 1u + (round % 4u);
        for (uint32_t k = 0; k < nisr; ++k) isr_tick(&isrq);

        /* 메인 루프 드레인: 매번이 아니라 가끔, 크기도 들쭉날쭉.
         * 단, 요청량은 항상 레코드 크기(4)의 배수다 — 생산자가 레코드 단위로 써도
         * 소비자가 아무 바이트 수나 읽어버리면 남은 데이터가 레코드 중간에서
         * 시작하게 되어 경계가 깨진다. 경계 유지는 양쪽의 공동 책임이다.
         * (일반 스트림이라면 [len][payload] 프레이밍 헤더로 재동기화한다.) */
        if (round % 4u != 3u) {
            uint32_t want = (1u + (round % 3u)) * ISR_SAMPLE_BYTES;  /* 4, 8, 12 */
            if (want > sizeof tmp) want = (uint32_t)sizeof tmp;
            consumed += rb4_read(&isrq, tmp, want);
        }

        /* 드레인 도중에 또 인터럽트가 걸리는 상황 */
        if (round % 5u == 0u) {
            isr_tick(&isrq);
            consumed += rb4_read(&isrq, tmp, 2u * ISR_SAMPLE_BYTES);
            isr_tick(&isrq);
        }
    }

    uint32_t remaining     = rb4_used(&isrq);
    uint32_t dropped_bytes = rb4_dropped(&isrq) * ISR_SAMPLE_BYTES;

    printf("      produced=%u  consumed=%u  remaining=%u  dropped=%u (records=%u)\n",
           g_produced_bytes, consumed, remaining, dropped_bytes, rb4_dropped(&isrq));

    T("Q5 ★ 보존법칙: 생산 == 소비 + 잔여 + 드롭 (한 바이트도 새지 않는다)",
      g_produced_bytes == consumed + remaining + dropped_bytes);
    T("Q5 실제로 넘쳤다 (드롭이 0 이면 테스트가 무의미)", rb4_dropped(&isrq) > 0);
    T("Q5 드롭은 항상 레코드 단위 (4의 배수) — 반쪽 샘플이 없다",
      dropped_bytes % ISR_SAMPLE_BYTES == 0);
    T("Q5 잔여량은 절대 용량을 넘지 않는다", remaining <= isrq.size);

    /* 링에 남은 바이트도 4바이트 경계에서 시작하는 온전한 샘플이어야 한다:
     * all-or-nothing 덕분에 레코드가 잘린 채 들어간 적이 없기 때문이다. */
    uint32_t left = rb4_read(&isrq, tmp, sizeof tmp);
    bool frames_ok = (left % ISR_SAMPLE_BYTES == 0);
    for (uint32_t i = 0; i + 3 < left; i += ISR_SAMPLE_BYTES) {
        uint8_t sum = (uint8_t)(tmp[i] + tmp[i + 1] + tmp[i + 2] + tmp[i + 3]);
        if (tmp[i + 2] != 0xA5 || sum != 0) frames_ok = false;   /* 마커 + 체크섬 */
    }
    T("Q5 남은 바이트는 전부 온전한 4바이트 샘플 (마커/체크섬 검증)", frames_ok);

    // ---------------------------------------------------------------------
    printf("\n[Q6] ★ tearing / 순서 뒤집힘 사고 실험 (rb4_push_broken)\n");
    // ---------------------------------------------------------------------
    // rb4_push_broken 은 head 를 먼저 올리고 데이터를 나중에 쓴다 — 명백한 버그다.
    // 그런데 단일 스레드에서는 push 가 끝난 뒤에야 pop 이 불리므로 아무 문제가 없다.
    // 아래 테스트가 전부 PASS 하는 것 자체가 이 Q 의 결론이다.
    // ---------------------------------------------------------------------
    rb4_init(&q, sto, 8);
    bool broken_ok = true;
    for (uint8_t i = 0; i < 8; ++i) broken_ok &= rb4_push_broken(&q, (uint8_t)(0x70 + i));
    for (uint8_t i = 0; i < 8; ++i)
        if (!rb4_pop(&q, &v) || v != (uint8_t)(0x70 + i)) broken_ok = false;
    T("단일 스레드에서는 버그가 안 잡힌다 — 그래서 무섭다", broken_ok);
    T("Q6 broken 판도 full 판정은 정상 (버그는 '순서'에만 있다)",
      rb4_init(&q, sto, 2) && rb4_push_broken(&q, 1) && rb4_push_broken(&q, 2)
      && !rb4_push_broken(&q, 3));
    T("Q6 broken 판으로 쓴 값도 단일 스레드라면 그대로 읽힌다",
      rb4_pop(&q, &v) && v == 1 && rb4_pop(&q, &v) && v == 2);
    /* 실기에서 깨지는 지점:
     *   ISR 이 "head 전진" 직후에 선점/종료되면, 메인 루프는 아직 쓰이지 않은 칸을
     *   유효 데이터로 읽는다. 멀티코어에서는 인터럽트가 없어도 store 재배치만으로
     *   같은 일이 난다. 이 버그는 유닛 테스트로 못 잡는다 — 리뷰와 TSan 의 영역. */

    // ---------------------------------------------------------------------
    printf("\n[Q7] rb4_used() 는 왜 '보수적'인가 — 안전한 방향으로 틀린다\n");
    // ---------------------------------------------------------------------
    // 핵심: 동시 실행 중에는 "지금 정확한 used" 같은 건 존재하지 않는다.
    //       각자가 보는 값은 이미 과거다. 중요한 건 **틀리는 방향**이다.
    //
    //   생산자가 본 used  >=  실제 used
    //       head 는 자기 것이라 정확하고, tail 은 스냅샷 이후 소비자가 더 전진시켰을
    //       수 있다(오래된 = 더 작은 tail). used = head - tail 이 과대평가된다.
    //       => free 는 과소평가된다 => "공간이 없다"고 잘못 판단할 뿐, 절대
    //          없는 공간에 쓰지 않는다. 최악의 결과는 불필요한 드롭 하나.
    //
    //   소비자가 본 used  <=  실제 used
    //       tail 은 자기 것이라 정확하고, head 는 스냅샷 이후 생산자가 더 전진시켰을
    //       수 있다(오래된 = 더 작은 head). used 가 과소평가된다.
    //       => "비었다"고 잘못 판단할 뿐, 절대 없는 데이터를 읽지 않는다.
    //          최악의 결과는 다음 루프까지의 약간의 지연.
    //
    // 두 오차가 모두 "보수적(안전)" 쪽이므로 락 없이도 정확성이 유지된다.
    // ---------------------------------------------------------------------
    rb4_init(&q, sto, 8);
    rb4_write_all(&q, rec6, 6);                       /* used = 6 */

    /* --- 생산자 시점: tail 을 스냅샷한 직후 소비자가 4바이트를 빼갔다 --- */
    uint_fast32_t t_snap = atomic_load_explicit(&q.tail, memory_order_relaxed);
    rb4_read(&q, tmp, 4);                             /* 소비자 진행: used 6 -> 2 */
    uint_fast32_t h_now  = atomic_load_explicit(&q.head, memory_order_relaxed);
    uint32_t producer_view = used_from(h_now, t_snap); /* 오래된 tail 로 계산 = 6 */
    uint32_t actual1       = rb4_used(&q);            /* 진짜 = 2 */
    T("Q7 생산자가 본 used(6) >= 실제 used(2) — 과대평가",
      producer_view == 6 && actual1 == 2 && producer_view >= actual1);
    T("Q7 따라서 생산자가 본 free 는 실제보다 작다 — 없는 공간에 쓰지 않는다",
      (q.size - producer_view) <= rb4_free(&q));

    /* --- 소비자 시점: head 를 스냅샷한 직후 생산자가 3바이트를 더 넣었다 --- */
    uint_fast32_t h_snap = atomic_load_explicit(&q.head, memory_order_relaxed);
    rb4_write_all(&q, rec5, 3);                       /* 생산자 진행: used 2 -> 5 */
    uint_fast32_t t_now  = atomic_load_explicit(&q.tail, memory_order_relaxed);
    uint32_t consumer_view = used_from(h_snap, t_now); /* 오래된 head 로 계산 = 2 */
    uint32_t actual2       = rb4_used(&q);            /* 진짜 = 5 */
    T("Q7 소비자가 본 used(2) <= 실제 used(5) — 과소평가",
      consumer_view == 2 && actual2 == 5 && consumer_view <= actual2);
    T("Q7 소비자는 자기가 본 만큼만 읽는다 -> 없는 데이터를 읽는 일이 없다",
      rb4_read(&q, tmp, consumer_view) == consumer_view);
    T("Q7 덜 읽었을 뿐 데이터는 남아있다 (다음 루프에서 처리)", rb4_used(&q) == 3);

    // ---------------------------------------------------------------------
    printf("\n== 레벨 4 — SPSC lock-free 링 (인터럽트 ↔ 메인 루프) ==  PASS %d / FAIL %d\n",
           g_pass, g_fail);
    return g_fail ? 1 : 0;
}
