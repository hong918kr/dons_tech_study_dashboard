// L5a_overwrite.c  —  SOLUTION (정답 + 해설)
// 레벨 5a — 덮어쓰기 모드 로그 버퍼 (블랙박스 링)  —  Q1~Q6
// ---------------------------------------------------------------------------
// 빌드/실행:  make sol N=L5a_overwrite
//   또는:     cc -std=c11 -Wall -Wextra -O0 -g solutions/L5a_overwrite.c -o build/L5a_overwrite_sol && ./build/L5a_overwrite_sol
//
// 이 레벨에서 배우는 것:
//   - 지금까지의 링버퍼는 "가득 차면 실패(drop-new)"였다. 여기서는 정책을 뒤집는다:
//     **가득 차면 가장 오래된 것을 버린다(drop-old / overwrite)**.
//   - 왜 뒤집는가: 크래시 직전 N바이트가 가장 중요한 로그이기 때문이다. 링이 가득 찼다고
//     새 로그를 버리면, 정작 사고 순간의 로그가 통째로 사라진다. 비행기 블랙박스와 같은
//     구조 — 마지막 30분만 남기고 계속 덮어쓴다.
//   - 바이트 단위로 덮어쓰면 **가장 오래된 레코드의 뒷부분만 남아** 파서가 깨진다.
//     그래서 실제 로그 링은 **레코드 단위로** 버린다 (5a-2).
//   - 그리고 결정적으로: **덮어쓰기는 레벨 4의 SPSC 무락(lock-free) 논증을 깨뜨린다.**
//     생산자가 tail 을 건드리기 시작하기 때문이다. 해법 3가지를 아래에 정리한다.
//
// 레벨 로드맵에서의 위치:
//   L4(lock-free SPSC, drop-new) -> **L5a(여기, drop-old/overwrite)** -> L5b(zero-copy).
//   L4 가 "무엇을 버릴지"의 기본형이라면, L5a 는 그 정책을 반대로 뒤집었을 때
//   따라오는 대가(동시성 보장 상실)를 어떻게 갚는지를 다룬다.
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
// 5a-1. 바이트 단위 덮어쓰기 링
// ---------------------------------------------------------------------------
// 인덱스는 L2 이후와 동일하게 free-running(마스킹하지 않고 계속 증가)이다.
//   used = head - tail   (부호 없는 뺄셈이라 32비트 랩이 일어나도 정확)
//   접근은 buf[idx & mask]
// 달라진 건 딱 하나: full 일 때 push 가 실패하는 대신 **tail 을 한 칸 밀어버린다**.
//
// ★ 덮어쓰기가 SPSC 무락을 깨뜨리는 이유 (이 레벨의 핵심 논점)
//   레벨 4의 무락 논증은 "생산자만 head 를 store, 소비자만 tail 을 store" 라는
//   단일 소유권(single-writer) 규칙 위에 서 있었다. write-write 경합이 구조적으로
//   없으니 락이 필요 없었던 것이다.
//   그런데 덮어쓰기 모드에서는 **생산자도 tail 을 옮긴다**. 이제 tail 에 writer 가
//   둘이다 -> write-write 경합 -> 소비자가 pop 하려고 읽은 tail 을 생산자가 동시에
//   밀어버리면, 소비자는 이미 덮어써진 쓰레기 바이트를 유효 데이터로 읽는다.
//   (CAS 루프로 억지로 막을 수는 있지만, 그러면 더 이상 "인덱스 두 개짜리 단순 링"이
//    아니고 ABA·기아 문제까지 따라온다.)
//
//   실무 해법 3가지:
//   1) **생산자 전용 링으로 쓴다 (권장, 진짜 블랙박스)**
//      소비자를 아예 두지 않는다. 런타임에는 누구도 pop 하지 않고, 하드폴트 핸들러나
//      디버거가 **CPU 를 멈춘 상태에서** 메모리를 통째로 덤프한다. writer 가 하나뿐이니
//      경합 자체가 없다. RAM 을 .noinit 섹션에 두면 워치독 리셋 후에도 내용이 살아남아
//      "리셋 직전 로그"를 그대로 읽을 수 있다. 실무에서 제일 흔한 형태.
//   2) **크리티컬 섹션으로 감싼다**
//      push 주변만 __disable_irq() / __enable_irq() (또는 BASEPRI 로 특정 우선순위
//      이하만 차단). 대가는 인터럽트 지연(latency) 몇 사이클 증가. push 가 수십 사이클
//      짜리 짧은 코드라면 보통 받아들일 만하다. 단, 이건 "무락"이 아니다.
//   3) **seqlock 스타일 스냅샷 (읽는 쪽이 검증하고 재시도)**
//      읽기 전에 tail_before 를 읽고, 데이터를 복사하고, 다시 tail_after 를 읽어서
//      "내가 읽던 구간이 그사이 덮어써졌는지"를 판정한다. 인덱스가 free-running 이라
//      (tail_after - tail_before) 가 곧 그사이 버려진 바이트 수이므로, 그 값이 0 이면
//      깨끗한 읽기, 0 보다 크면 오염이므로 재시도하거나 "유실됨"으로 보고한다.
//      락을 안 쓰지만 읽기가 **진행 보장(wait-free)이 아니다** — 생산자가 빠르면
//      영원히 재시도할 수도 있다.
// ===========================================================================
typedef struct {
    uint8_t *buf;
    uint32_t size;        /* 2의 거듭제곱 */
    uint32_t mask;        /* size - 1 */
    uint32_t head;        /* 다음에 쓸 위치 (free-running) */
    uint32_t tail;        /* 다음에 읽을 위치 (free-running) */
    uint32_t overwritten; /* 덮어써서 버린 바이트 수 (누적) */
} ovr_t;

/* ---------------------------------------------------------------------------
 * Q1.  ovr_init / ovr_used / ovr_push / ovr_pop
 *   KO: size 가 2의 거듭제곱이 아니면 init 은 false. used 는 head - tail.
 *       push 는 ★절대 실패하지 않는다★ — 가득이면 tail++ 하고 overwritten++ 한 뒤
 *       새 값을 넣는다. pop 은 비었으면 false, 아니면 가장 오래된 값 하나.
 *   EN: Init/used/push/pop for an overwriting byte ring; push never fails, it
 *       evicts the oldest byte instead and counts the eviction.
 *   ex: size 8 링에 8바이트를 채운 뒤 push -> used 는 여전히 8, overwritten == 1
 *   hint: full 판정은 (head - tail) == size. 덮어쓸 때 tail 을 먼저 밀어야
 *         used 가 size 를 넘지 않는다.
 * ------------------------------------------------------------------------- */
bool ovr_init(ovr_t *q, uint8_t *buf, uint32_t size) {
    /* pow2 를 강제하는 이유는 L2 와 같다: idx % size 대신 idx & (size-1) 을 쓰기
       위해서다. Cortex-M0/M0+ 에는 하드웨어 나눗셈기가 없어 % 는 수십 사이클짜리
       소프트웨어 루틴이지만, & 는 1 사이클이다. 로그 push 는 ISR 안에서도 도는
       초고빈도 경로라 이 차이가 그대로 인터럽트 지연이 된다. */
    if (!q || !buf) return false;
    if (size < 2 || (size & (size - 1)) != 0) return false;
    q->buf         = buf;
    q->size        = size;
    q->mask        = size - 1;
    q->head        = 0;
    q->tail        = 0;
    q->overwritten = 0;
    return true;
}

uint32_t ovr_used(const ovr_t *q) {
    /* 부호 없는 뺄셈이므로 head 가 32비트 경계를 넘어 랩해도 결과는 정확하다.
       (head=0x00000002, tail=0xFFFFFFFF -> 2 - 0xFFFFFFFF = 3) */
    if (!q) return 0;
    return q->head - q->tail;
}

/* ★ 이 레벨의 심장부. 리턴 타입이 bool 이 아니라 void 인 것 자체가 계약이다:
   "이 함수는 실패할 수 없다". 호출자(ISR)는 반환값 검사도, 재시도 로직도 필요 없다.
   대신 잃은 데이터는 overwritten 카운터로 사후에 보고된다. */
void ovr_push(ovr_t *q, uint8_t v) {
    if (!q) return;
    if ((uint32_t)(q->head - q->tail) == q->size) {
        /* 가득 찼다 -> 가장 오래된 1바이트를 버린다.
           ★ 여기서 생산자가 tail 을 건드린다. 위 주석의 무락 붕괴 지점이 바로 이 줄. */
        q->tail++;
        q->overwritten++;
    }
    q->buf[q->head & q->mask] = v;
    q->head++;
}

bool ovr_pop(ovr_t *q, uint8_t *out) {
    if (!q || !out) return false;
    if (q->head == q->tail) return false;       /* empty */
    *out = q->buf[q->tail & q->mask];
    q->tail++;
    return true;
}

/* ---------------------------------------------------------------------------
 * Q2.  (테스트 전용 — 구현할 함수 없음) 가득 찬 뒤 push 의 동작 검증
 *   KO: push 가 실패하지 않고, 가장 오래된 값이 밀려나며, overwritten 이 정확히
 *       "버린 바이트 수"를 세는지 확인한다.
 *   EN: Verify push never fails on a full ring and evicts oldest-first.
 *   ex: A0..A7 을 채운 뒤 B0 push -> pop 하면 A1 부터 나온다
 * ------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------
 * Q3.  ovr_write — n 바이트 한 번에 (n > size 여도 동작)
 *   KO: src 의 n 바이트를 차례로 넣는다. n 이 size 보다 커도 크래시 없이 동작하고,
 *       결과적으로 **마지막 size 바이트만** 링에 남는다. overwritten 은 그동안
 *       버린 바이트 수만큼 증가한다.
 *   EN: Bulk overwrite-write; if n > size only the last size bytes survive.
 *   ex: size 8 링에 0..19 (20바이트) write -> 남는 건 12..19, overwritten == 12
 *   hint: 가장 단순한 정답은 ovr_push 를 n 번 도는 것이다. 카운터 의미도 자동으로 맞는다.
 * ------------------------------------------------------------------------- */
void ovr_write(ovr_t *q, const uint8_t *src, uint32_t n) {
    if (!q || !src || n == 0) return;
    /* 굳이 "앞의 (n - size) 바이트는 어차피 덮어써지니 건너뛰자" 같은 최적화를 먼저
       하지 않는다. 그렇게 하면 overwritten 카운터를 따로 보정해야 하고, 경계 계산에서
       off-by-one 이 나기 딱 좋다. n >> size 인 경우가 실제로 잦다면
         if (n >= q->size) { src += n - q->size; q->overwritten += n - q->size; n = q->size; }
       를 앞에 덧붙이고 나머지는 그대로 두는 식으로 "의미를 보존한 채" 빠르게 만든다. */
    for (uint32_t i = 0; i < n; ++i) ovr_push(q, src[i]);
}

/* ---------------------------------------------------------------------------
 * Q4.  ovr_snapshot — 비파괴 덤프 (크래시 덤프용)
 *   KO: 가장 최근 n 바이트를 **시간순(오래된 것 먼저)** 으로 dst 에 복사하고 복사한
 *       개수를 리턴한다. 요청 n 이 used 보다 크면 used 만큼만. ★호출 후에도 링의
 *       내용과 used 는 전혀 변하지 않는다(pop 이 아니다).
 *   EN: Non-destructive dump of the most recent n bytes, oldest-first.
 *   ex: 링에 34..3B 가 있을 때 snapshot(dst, 4) -> 38,39,3A,3B 리턴값 4, used 그대로
 *   hint: 시작 인덱스는 head - m (m = min(n, used)). 거기서부터 마스킹하며 m 번 복사.
 * ------------------------------------------------------------------------- */
uint32_t ovr_snapshot(const ovr_t *q, uint8_t *dst, uint32_t n) {
    if (!q || !dst || n == 0) return 0;
    uint32_t used = q->head - q->tail;
    uint32_t m    = (n < used) ? n : used;      /* 요청이 과하면 있는 만큼만 */
    /* "가장 최근 m 바이트" = [head - m, head) 구간. free-running 인덱스라
       head - m 을 그냥 빼도 되고(부호 없는 랩이 알아서 맞는다), 접근할 때만 마스킹한다. */
    uint32_t start = q->head - m;
    for (uint32_t i = 0; i < m; ++i)
        dst[i] = q->buf[(start + i) & q->mask];  /* i 오름차순 = 시간순 */
    return m;
    /* ★ 왜 비파괴인가?
       크래시 덤프를 떴다고 로깅이 멈추면 안 되기 때문이다. 하드폴트 핸들러가 링을
       UART 로 토해내는 동안에도(또는 텔레메트리로 주기적으로 스냅샷을 보내는 동안에도)
       링은 계속 최신 로그를 받아야 한다. pop 으로 비워버리면 "덤프를 뜨는 행위 자체가
       증거를 지우는" 꼴이 된다. 같은 이유로 스냅샷은 여러 번 떠도 같은 결과여야 한다.
       그래서 인자가 const ovr_t * 다 — 타입으로 계약을 못 박는다. */
}

// ===========================================================================
// 5a-2. 레코드 단위 덮어쓰기 (프레임이 반쪽 나면 안 되는 경우)
// ---------------------------------------------------------------------------
// 바이트 단위 덮어쓰기의 치명적 결함:
//   [len=5][A B C D E][len=3][X Y Z] 가 들어 있는 링에 1바이트가 덮어써지면
//   남는 건 [5][B C D E][3][X Y Z] 가 아니라 **[B][C D E][3][X Y Z]** 다.
//   즉 tail 이 레코드 중간을 가리키게 되고, 파서는 B(=0x42=66) 를 길이로 읽어
//   66바이트를 소비하려 든다 -> 그 뒤의 멀쩡한 레코드까지 전부 오파싱된다.
//   한 바이트 손실이 **로그 전체를 못 읽게** 만드는 것이다.
//
// 그래서 실제 로그 링은 **레코드 단위로 버린다**: 공간이 모자라면 가장 오래된
// 레코드를 [len + payload] 통째로 소비해서 tail 을 다음 레코드 경계로 옮긴다.
// 이렇게 하면 tail 은 언제나 레코드 경계를 가리킨다(불변식).
//
// 레코드 포맷: [uint8_t len][payload len 바이트]   (len 은 1~255)
// ===========================================================================
typedef struct {
    ovr_t    r;        /* 바이트 저장소는 위의 링을 그대로 재사용 */
    uint32_t dropped;  /* 통째로 버린 레코드 수 */
} rlog_t;

/* 가장 오래된 레코드 하나를 통째로 버린다 (tail 을 다음 레코드 경계로).
   tail 이 항상 레코드 경계를 가리킨다는 불변식이 있으므로 len 바이트를 그냥 믿어도 된다. */
static void rlog_drop_oldest(rlog_t *q) {
    if (q->r.head == q->r.tail) return;                  /* 비었으면 버릴 게 없다 */
    uint8_t len = q->r.buf[q->r.tail & q->r.mask];       /* 레코드 헤더 = 길이 */
    q->r.tail += 1u + (uint32_t)len;                     /* 헤더 + payload 를 한 번에 */
    q->dropped++;
}

/* 공간이 확보된 상태에서만 부르는 raw 저장 (덮어쓰기 분기를 타지 않는다). */
static void rlog_put_raw(rlog_t *q, uint8_t v) {
    q->r.buf[q->r.head & q->r.mask] = v;
    q->r.head++;
}

/* ---------------------------------------------------------------------------
 * Q5.  rlog_init / rlog_push / rlog_pop / rlog_dropped_records
 *   KO: push 는 공간이 부족하면 **가장 오래된 레코드를 통째로** 버리기를 반복한 뒤
 *       넣는다. len 이 0 이거나 len+1 > size 면 false(그건 정책이 아니라 오용이다).
 *       pop 은 가장 오래된 레코드를 꺼내 길이를 리턴, 없으면 0.
 *   EN: Record-granular overwriting log: evict whole oldest records to make room.
 *   ex: size 64 링에 3바이트, 5바이트 레코드를 넣으면 pop 이 3, 5 순서로 나온다
 *   hint: 필요한 공간은 len + 1 (헤더 1바이트). free = size - used.
 *         cap 이 레코드 길이보다 작으면 소비하지 말고 0 을 리턴해 데이터를 지키자.
 * ------------------------------------------------------------------------- */
bool rlog_init(rlog_t *q, uint8_t *buf, uint32_t size) {
    if (!q) return false;
    q->dropped = 0;
    return ovr_init(&q->r, buf, size);
}

bool rlog_push(rlog_t *q, const uint8_t *rec, uint8_t len) {
    if (!q || !rec) return false;
    if (len == 0) return false;                       /* 빈 레코드는 의미가 없다 */
    uint32_t need = 1u + (uint32_t)len;               /* 헤더 1 + payload */
    if (need > q->r.size) return false;               /* 링보다 큰 레코드는 영원히 못 넣는다 */

    /* ★ drop-old 루프: 들어갈 자리가 날 때까지 가장 오래된 레코드를 통째로 버린다.
       바이트 단위로 tail 을 찔끔 미는 게 아니라 레코드 경계 단위로 점프하는 것이
       핵심 — 그래서 반쪽 레코드가 생길 수 없다. */
    while ((q->r.size - (q->r.head - q->r.tail)) < need) {
        if (q->r.head == q->r.tail) break;            /* 방어: need <= size 이므로 도달 불가 */
        rlog_drop_oldest(q);
    }

    rlog_put_raw(q, len);                             /* 헤더 */
    for (uint8_t i = 0; i < len; ++i) rlog_put_raw(q, rec[i]);
    return true;
}

uint32_t rlog_pop(rlog_t *q, uint8_t *dst, uint32_t cap) {
    if (!q || !dst) return 0;
    if (q->r.head == q->r.tail) return 0;             /* 비었다 */
    uint8_t len = q->r.buf[q->r.tail & q->r.mask];
    if ((uint32_t)len > cap) return 0;
    /* ↑ 버퍼가 모자라면 **소비하지 않고** 0 을 리턴한다. 여기서 tail 을 밀어버리면
       "출력 버퍼가 작았다"는 호출자 실수가 곧 로그 유실이 된다. 호출자는 더 큰
       버퍼로 재시도할 수 있어야 한다. (반환값 0 이 empty 와 겹치는 게 싫으면
       -1/에러코드로 분리해도 좋다 — 여기서는 API 를 단순하게 유지했다.) */
    for (uint8_t i = 0; i < len; ++i)
        dst[i] = q->r.buf[(q->r.tail + 1u + (uint32_t)i) & q->r.mask];
    q->r.tail += 1u + (uint32_t)len;
    return len;
}

uint32_t rlog_dropped_records(const rlog_t *q) {
    return q ? q->dropped : 0;
}

/* ---------------------------------------------------------------------------
 * Q6.  (테스트 전용 — 구현할 함수 없음) 작은 링에서의 레코드 무결성
 *   KO: 32바이트짜리 링에 길이가 제각각인 레코드를 계속 밀어 넣어도, 꺼낸 레코드는
 *       **항상 온전한 길이와 내용**을 가져야 한다(반쪽 레코드 금지). 그리고
 *       dropped_records + 남은 개수 == 넣은 개수 여야 한다.
 *   EN: Stress a tiny record log; every popped record must be byte-exact.
 *   ex: 12개를 넣고 2개가 남았다면 dropped_records 는 10 이고, 남은 2개는
 *       "가장 최근에 넣은 2개"와 완전히 일치한다
 * ------------------------------------------------------------------------- */

// ===========================================================================
// ★ overwritten / dropped_records 카운터는 왜 반드시 있어야 하는가
// ---------------------------------------------------------------------------
//   덮어쓰기 링의 위험은 "조용히 잃는다"는 것이다. drop-new 모드에서는 push 가
//   false 를 리턴하니 호출자가 최소한 알아챌 기회라도 있지만, drop-old 모드에서는
//   push 가 언제나 성공하므로 **아무도 손실을 모른다**.
//   그래서 카운터가 API 의 일부다. 실무 효용:
//     - 덤프를 읽는 사람이 "여기서부터 로그가 연속적이다 / 앞은 잘렸다"를 판정할 수 있다.
//     - 로그 유실률을 숫자로 보고 링 크기나 로그 레벨을 조정할 수 있다
//       ("overwritten 이 부팅 후 3MB → 링이 작거나 flush 가 느리다").
//     - 회귀 테스트가 가능해진다. "이 시나리오에서 유실 0" 을 단언할 수 있다.
//   디버깅 숙련도의 정의가 바로 이것이다: **로그가 몇 개 유실됐는지 숫자로 말할 수 있는가.**
//   덤프 헤더에 overwritten/dropped 를 같이 실어 보내는 것이 정석이다.
// ===========================================================================

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
// ===========================================================================
#define NREC 12

int main(void) {
    ovr_t   q;
    uint8_t sto[8];
    uint8_t v = 0;

    /* init 이 정말 초기화하는지 보려고 일부러 쓰레기값을 채운다 (스택은 원래 쓰레기다). */
    memset(&q, 0, sizeof q);

    // -------- Q1 --------
    printf("\n[Q1] ovr_init / ovr_used / ovr_push / ovr_pop 기본\n");
    T("size 6 (2의 거듭제곱 아님) init 거부", !ovr_init(&q, sto, 6));
    T("size 8 init 성공", ovr_init(&q, sto, 8));
    T("init 직후 used == 0", ovr_used(&q) == 0);
    ovr_push(&q, 10);
    ovr_push(&q, 20);
    ovr_push(&q, 30);
    T("3개 push 후 used == 3", ovr_used(&q) == 3);
    T("pop 은 가장 오래된 것부터 (10)", ovr_pop(&q, &v) && v == 10);
    T("이어서 20, 30 (FIFO)",
      ovr_pop(&q, &v) && v == 20 && ovr_pop(&q, &v) && v == 30);
    T("빈 상태에서 pop 은 false", !ovr_pop(&q, &v));
    T("여기까지 덮어쓰기는 없었다 (overwritten == 0)", q.overwritten == 0);

    // -------- Q2 --------
    printf("\n[Q2] ★가득 찬 뒤 push — 실패하지 않고 가장 오래된 것이 밀려난다\n");
    ovr_init(&q, sto, 8);
    for (uint8_t i = 0; i < 8; ++i) ovr_push(&q, (uint8_t)(0xA0 + i));
    T("8개 채운 상태: used == 8, overwritten == 0",
      ovr_used(&q) == 8 && q.overwritten == 0);
    ovr_push(&q, 0xB0);                 /* ★ 실패할 방법이 없다 (리턴값조차 없다) */
    T("가득 찬 뒤 push 해도 used 는 그대로 8 (버퍼가 안 자란다)", ovr_used(&q) == 8);
    T("overwritten == 1 (가장 오래된 0xA0 이 버려졌다)", q.overwritten == 1);
    T("이제 가장 오래된 값은 0xA1", ovr_pop(&q, &v) && v == 0xA1);
    /* used 7 -> push 3회: 첫 1회는 여유, 나머지 2회는 덮어쓰기 */
    ovr_push(&q, 0xB1);
    ovr_push(&q, 0xB2);
    ovr_push(&q, 0xB3);
    T("push 3회 중 2회가 덮어쓰기 -> overwritten == 3", q.overwritten == 3);
    {
        const uint8_t exp[8] = {0xA4, 0xA5, 0xA6, 0xA7, 0xB0, 0xB1, 0xB2, 0xB3};
        bool win = (ovr_used(&q) == 8);
        for (int i = 0; i < 8; ++i)
            if (!ovr_pop(&q, &v) || v != exp[i]) { win = false; break; }
        T("남은 8바이트는 '가장 최근 8개' 윈도우 (A4..A7,B0..B3)", win);
    }

    // -------- Q3 --------
    printf("\n[Q3] ovr_write — n 이 size 보다 커도 동작 (마지막 size 바이트만 남음)\n");
    {
        uint8_t src20[20];
        for (int i = 0; i < 20; ++i) src20[i] = (uint8_t)i;

        ovr_init(&q, sto, 8);
        ovr_write(&q, src20, 3);
        T("짧은 write(3바이트): used == 3, 덮어쓰기 없음",
          ovr_used(&q) == 3 && q.overwritten == 0);

        ovr_init(&q, sto, 8);
        ovr_write(&q, src20, 0);
        T("n == 0 은 아무 일도 하지 않는다", ovr_used(&q) == 0);

        ovr_init(&q, sto, 8);
        ovr_write(&q, src20, 20);       /* ★ 링(8)보다 2.5배 큰 입력 */
        T("n(20) > size(8) 여도 크래시 없이 used == 8", ovr_used(&q) == 8);
        T("버려진 바이트 수 overwritten == 12", q.overwritten == 12);
        {
            bool last8 = true;
            for (uint8_t i = 12; i < 20; ++i)
                if (!ovr_pop(&q, &v) || v != i) { last8 = false; break; }
            T("남은 내용은 마지막 8바이트(12..19) 뿐", last8);
        }
    }

    // -------- Q4 --------
    printf("\n[Q4] ovr_snapshot — 비파괴 크래시 덤프 (시간순)\n");
    {
        uint8_t dump[16];
        ovr_init(&q, sto, 8);
        for (int i = 0; i < 12; ++i) ovr_push(&q, (uint8_t)(0x30 + i));
        /* 12개를 넣었으니 남은 건 0x34..0x3B, overwritten == 4 */
        T("준비: used == 8, overwritten == 4", ovr_used(&q) == 8 && q.overwritten == 4);

        memset(dump, 0, sizeof dump);
        uint32_t n = ovr_snapshot(&q, dump, 4);
        T("snapshot(4) 는 4 를 리턴", n == 4);
        T("가장 최근 4바이트가 시간순으로 (38,39,3A,3B)",
          dump[0] == 0x38 && dump[1] == 0x39 && dump[2] == 0x3A && dump[3] == 0x3B);
        T("★비파괴: snapshot 후에도 used 는 8 그대로", ovr_used(&q) == 8);

        memset(dump, 0, sizeof dump);
        uint32_t n2 = ovr_snapshot(&q, dump, 100);
        T("요청(100) > used(8) 이면 used 만큼만 복사",
          n2 == 8 && dump[0] == 0x34 && dump[7] == 0x3B);

        memset(dump, 0, sizeof dump);
        T("몇 번을 떠도 같은 결과 (덤프가 증거를 지우지 않는다)",
          ovr_snapshot(&q, dump, 8) == 8 && dump[0] == 0x34 && dump[7] == 0x3B);
        T("스냅샷은 pop 이 아니다 — 여전히 0x34 부터 pop 된다",
          ovr_pop(&q, &v) && v == 0x34);

        ovr_init(&q, sto, 8);
        T("빈 링 snapshot 은 0", ovr_snapshot(&q, dump, 8) == 0);
    }

    // -------- Q5 --------
    printf("\n[Q5] rlog push / pop 기본 + FIFO\n");
    {
        uint8_t  rsto[64];
        rlog_t   lg;
        uint8_t  out[64];
        uint8_t  big[255];
        const uint8_t r1[3] = {1, 2, 3};
        const uint8_t r2[5] = {10, 11, 12, 13, 14};
        memset(big, 0xEE, sizeof big);
        memset(&lg, 0, sizeof lg);

        T("rlog_init 성공", rlog_init(&lg, rsto, 64));
        T("빈 로그에서 pop 은 0", rlog_pop(&lg, out, sizeof out) == 0);
        T("len == 0 은 거부 (오용)", !rlog_push(&lg, r1, 0));
        T("len + 1 > size 는 거부 (링보다 큰 레코드)", !rlog_push(&lg, big, 200));

        T("정상 레코드 2개 push 성공",
          rlog_push(&lg, r1, 3) && rlog_push(&lg, r2, 5));
        uint32_t l1 = rlog_pop(&lg, out, sizeof out);
        bool p1_ok = (l1 == 3) && (memcmp(out, r1, 3) == 0);
        uint32_t l2 = rlog_pop(&lg, out, sizeof out);
        bool p2_ok = (l2 == 5) && (memcmp(out, r2, 5) == 0);
        T("FIFO: 넣은 순서대로 길이·내용이 그대로 (3 -> 5)", p1_ok && p2_ok);
        T("다 꺼내면 다시 0", rlog_pop(&lg, out, sizeof out) == 0);

        rlog_push(&lg, r2, 5);
        T("cap 이 모자라면 0 리턴 + 레코드는 보존", rlog_pop(&lg, out, 4) == 0);
        T("더 큰 버퍼로 재시도하면 온전히 나온다",
          rlog_pop(&lg, out, sizeof out) == 5 && memcmp(out, r2, 5) == 0);
        T("여기까지 버린 레코드 없음", rlog_dropped_records(&lg) == 0);
    }

    // -------- Q6 --------
    printf("\n[Q6] ★rlog 덮어쓰기 — 작은 링(32B)에서도 반쪽 레코드는 절대 안 나온다\n");
    {
        uint8_t  sto32[32];
        rlog_t   lg2;
        uint8_t  lens[NREC];
        uint8_t  recs[NREC][16];
        uint8_t  got[16];
        uint8_t  kept_len[NREC];
        uint8_t  kept_buf[NREC][16];
        uint32_t kept = 0;

        memset(&lg2, 0, sizeof lg2);
        rlog_init(&lg2, sto32, 32);

        /* 길이가 3~11 로 제각각인 레코드 — 경계가 매번 다른 위치에서 랩된다 */
        for (uint32_t i = 0; i < NREC; ++i) {
            lens[i] = (uint8_t)(3 + (i % 9));
            for (uint32_t j = 0; j < lens[i]; ++j)
                recs[i][j] = (uint8_t)(i * 16u + j);
        }

        bool push_ok = true;
        for (uint32_t i = 0; i < NREC; ++i)
            push_ok &= rlog_push(&lg2, recs[i], lens[i]);
        T("12개 전부 push 성공 (덮어쓰기 모드는 실패하지 않는다)", push_ok);

        uint32_t L;
        while (kept < NREC && (L = rlog_pop(&lg2, got, sizeof got)) != 0) {
            kept_len[kept] = (uint8_t)L;
            memcpy(kept_buf[kept], got, L);
            kept++;
        }
        T("링 크기보다 많이 넣었으니 일부만 남아 있다 (0 < kept < 12)",
          kept > 0 && kept < NREC);

        /* ★ 핵심 검증: 남은 것들은 "가장 최근 kept 개"와 길이·내용이 완전히 일치해야 한다.
           바이트 단위로 덮어썼다면 첫 레코드의 길이 헤더가 깨져 여기서 무너진다. */
        bool intact = (kept > 0);
        for (uint32_t i = 0; i < kept; ++i) {
            uint32_t src = NREC - kept + i;
            if (kept_len[i] != lens[src]) { intact = false; break; }
            if (memcmp(kept_buf[i], recs[src], kept_len[i]) != 0) { intact = false; break; }
        }
        T("남은 레코드는 전부 온전한 길이·내용 (반쪽 레코드 없음)", intact);
        T("버린 레코드 + 남은 레코드 == 넣은 레코드 (12)",
          rlog_dropped_records(&lg2) + kept == NREC);
        T("가장 마지막에 넣은 레코드는 반드시 살아 있다",
          kept > 0 && kept_len[kept - 1] == lens[NREC - 1] &&
          memcmp(kept_buf[kept - 1], recs[NREC - 1], lens[NREC - 1]) == 0);
        T("다 꺼낸 뒤 pop 은 0", rlog_pop(&lg2, got, sizeof got) == 0);
    }

    printf("\n== 레벨 5a — 덮어쓰기 모드 로그 버퍼 (블랙박스 링) ==  PASS %d / FAIL %d\n",
           g_pass, g_fail);
    return g_fail ? 1 : 0;
}
