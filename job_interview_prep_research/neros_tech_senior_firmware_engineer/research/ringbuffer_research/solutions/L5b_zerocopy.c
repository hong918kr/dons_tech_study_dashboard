// L5b_zerocopy.c  —  SOLUTION (정답 + 해설)
// 레벨 5b — zero-copy 인터페이스 (DMA 에 링 메모리를 직접 넘기기)  —  Q1~Q6
// ---------------------------------------------------------------------------
// 빌드/실행:  make sol N=L5b_zerocopy
//   또는:     cc -std=c11 -Wall -Wextra -O0 -g solutions/L5b_zerocopy.c -o build/L5b_zerocopy_sol && ./build/L5b_zerocopy_sol
//
// 이 레벨에서 배우는 것:
//   - acquire/commit **2단계 API**: "링의 내부 메모리를 직접 빌려 쓰고, 다 쓴 만큼만 확정한다".
//   - 랩(wrap) 지점에서 구간이 **둘로 쪼개지는** 문제와 그 해법(bip-buffer, iovec).
//   - DMA 특유의 함정: 캐시 일관성, 메모리 영역 제약, 진행 중 버퍼 수정 금지.
//
// ★ 왜 zero-copy 인가
//   1KB 프레임을 UART 로 보낸다고 하자. 지금까지의 API(ring_write / ring_read)는
//   "사용자 버퍼 -> 링" 한 번, "링 -> DMA 용 버퍼" 한 번, 총 두 번 memcpy 한다.
//   Cortex-M4 에서 1KB memcpy 는 대략 수백~수천 사이클이고(정렬·버스 대기에 따라),
//   그동안 D-cache 는 전송 데이터로 오염되어 정작 제어 루프가 쓸 캐시 라인을 밀어낸다.
//   DMA 는 **주소와 길이만** 있으면 되므로, 링 내부 메모리를 그대로 가리키게 하면
//   CPU 사이클은 0 이다. 400Hz 제어 루프를 도는 비행 컨트롤러에서 이 차이는 마진이다.
//
// ★ 왜 2단계(acquire -> commit)인가
//   DMA 가 도는 동안 링은 그 구간을 "예약됨(in-flight)"으로 알고 있어야 한다.
//   commit 전에는 head 가 움직이지 않으므로 **소비자에게는 그 데이터가 보이지 않는다**
//   = 미완성/절반만 채워진 데이터 노출 방지. 전송이 끝난 뒤(TC 인터럽트) 실제 전송량만큼
//   commit 하면 그때 비로소 원자적으로 "공개"된다. 한 줄로 요약하면:
//     acquire = "여기에 쓸게, 아무도 건드리지 마" / commit = "여기까지 유효해, 이제 봐도 돼".
//
// 레벨 로드맵에서의 위치:
//   L4(lock-free SPSC) -> L5a(overwrite) -> **L5b(여기, zero-copy + DMA)**.
//   L5b 는 "링을 얼마나 빨리 돌리느냐"가 아니라 "복사를 아예 없애느냐"의 문제다.
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
// head/tail 은 L2 이후와 같은 free-running 인덱스. 새로 생긴 건 두 개의 "예약" 필드다.
//
//   wr_pending : write_acquire 로 잡아둔(=DMA 가 채우는 중인) 구간 길이
//   rd_pending : read_acquire  로 잡아둔(=DMA 가 내보내는 중인) 구간 길이
//
// ★ 진행 중 수정 금지 (DMA 함정 3번)
//   commit 전 구간은 **오직 그 DMA 채널만** 건드릴 수 있다. 다른 코드가 같은 링에
//   또 write_acquire 를 하고 써버리면 전송 중인 데이터가 발밑에서 바뀐다.
//   wr_pending/rd_pending 은 그 계약을 코드로 표현한 것이다 — 실제 드라이버에서는
//   여기에 assert 나 상태 플래그(IDLE/BUSY)를 붙여 "acquire 중복 호출"을 잡아낸다.
//
// ★ 캐시라인 정렬 (DMA 함정 1번의 예고)
//   실제 타깃에서는 buf 를 __attribute__((aligned(32))) 로 잡고 size 도 32의 배수로
//   둔다. 이유는 아래 "DMA 함정" 주석 참고.
// ===========================================================================
typedef struct {
    uint8_t *buf;
    uint32_t size;        /* 2의 거듭제곱 */
    uint32_t mask;        /* size - 1 */
    uint32_t head;        /* 생산자 인덱스 (free-running) */
    uint32_t tail;        /* 소비자 인덱스 (free-running) */
    uint32_t wr_pending;  /* acquire 로 잡아둔 쓰기 구간 길이 (아직 commit 안 됨) */
    uint32_t rd_pending;  /* acquire 로 잡아둔 읽기 구간 길이 */
} zc_t;

/* ---------------------------------------------------------------------------
 * Q1.  zc_init / zc_used / zc_free
 *   KO: size 가 2의 거듭제곱이 아니면 false. used = head - tail, free = size - used.
 *       pending 필드도 0 으로 초기화한다(예약 없음 상태).
 *   EN: Init plus the two occupancy helpers for the zero-copy ring.
 *   ex: size 16 으로 init -> used 0, free 16
 *   hint: 인덱스는 마스킹하지 않고 계속 증가시킨다. 접근할 때만 & mask.
 * ------------------------------------------------------------------------- */
bool zc_init(zc_t *q, uint8_t *buf, uint32_t size) {
    if (!q || !buf) return false;
    if (size < 2 || (size & (size - 1)) != 0) return false;
    q->buf        = buf;
    q->size       = size;
    q->mask       = size - 1;
    q->head       = 0;
    q->tail       = 0;
    q->wr_pending = 0;
    q->rd_pending = 0;
    return true;
}

uint32_t zc_used(const zc_t *q) {
    if (!q) return 0;
    return q->head - q->tail;          /* 부호 없는 뺄셈 — 32비트 랩에도 안전 */
}

uint32_t zc_free(const zc_t *q) {
    if (!q) return 0;
    return q->size - (q->head - q->tail);
}

/* ---------------------------------------------------------------------------
 * Q2/Q3/Q4.  zc_write_acquire / zc_write_commit  (생산자 측 zero-copy)
 *   KO: acquire 는 "지금 **연속으로** 쓸 수 있는 최대 바이트 수"를 리턴하고 *ptr 에
 *       시작 주소를 준다. 랩 지점에서는 버퍼 끝까지만 준다(= free 보다 작을 수 있다).
 *       commit(n) 은 실제로 채운 n(<= acquire 결과)만큼만 head 를 전진시킨다.
 *   EN: Reserve a contiguous writable span inside the ring, then publish only the
 *       bytes actually filled.
 *   ex: size 16, head=12, 링이 비었을 때 acquire -> 4 리턴 (free 는 16 인데도!)
 *   hint: idx = head & mask, to_end = size - idx, n = min(free, to_end).
 *         꽉 찼으면 0 을 리턴하고 *ptr 은 NULL 로 둔다(호출자가 실수로 못 쓰게).
 * ------------------------------------------------------------------------- */
uint32_t zc_write_acquire(zc_t *q, uint8_t **ptr) {
    if (!q || !ptr) return 0;
    uint32_t freeb = q->size - (q->head - q->tail);
    if (freeb == 0) {                       /* 가득 참 -> 빌려줄 메모리가 없다 */
        *ptr = NULL;
        q->wr_pending = 0;
        return 0;
    }
    uint32_t idx    = q->head & q->mask;
    uint32_t to_end = q->size - idx;        /* 물리적으로 연속인 구간의 끝까지 */
    uint32_t n      = (freeb < to_end) ? freeb : to_end;
    *ptr = &q->buf[idx];
    q->wr_pending = n;                      /* "여기 n 바이트 예약했다" */
    return n;
    /* ★ 왜 free 전부가 아니라 min(free, to_end) 인가
       한 번의 DMA 전송은 **연속 메모리만** 다룰 수 있기 때문이다(scatter-gather 가
       없는 흔한 주변장치 기준). 링의 논리적 free 공간이 12바이트라도 버퍼 끝까지가
       4바이트뿐이면, DMA 에게 12를 주는 순간 버퍼 밖을 밟는다. 그래서 "끝까지만"
       주고, 나머지는 다음 acquire 에서 앞쪽부터 다시 준다. */
}

void zc_write_commit(zc_t *q, uint32_t n) {
    if (!q) return;
    if (n > q->wr_pending) n = q->wr_pending;
    /* ↑ 방어적 클램프. 계약상 n <= 직전 acquire 결과여야 하고, 실제 드라이버라면
       여기서 assert 로 죽이는 편이 낫다(계약 위반은 버그지 런타임 조건이 아니다).
       학습용이므로 조용히 잘라내고, acquire 없이 부른 commit 은 no-op 이 된다. */
    q->head += n;                /* ★ 이 한 줄이 "공개" 시점. 이전엔 소비자에게 안 보인다 */
    q->wr_pending = 0;           /* 예약 해제 */
    /* 남은 (acquire - n) 바이트는 그냥 버려진다. 데이터가 사라지는 게 아니라
       "아직 안 쓴 공간"으로 되돌아갈 뿐이고, 다음 acquire 가 같은 자리를 다시 준다.
       UART RX 에서 idle line 인터럽트로 DMA 를 조기 종료했을 때가 정확히 이 경우다:
       256바이트를 걸어놨는데 프레임이 37바이트에서 끝났다 -> commit(37). */
}

/* ---------------------------------------------------------------------------
 * Q5.  zc_read_acquire / zc_read_commit  (소비자 측 zero-copy)
 *   KO: 읽을 수 있는 **연속 구간**의 시작 주소와 길이를 준다. TX DMA 에 그대로 넘기면
 *       된다. acquire 만으로는 used 가 줄지 않는다(전송 중). 전송 완료(TC) 인터럽트에서
 *       실제 보낸 만큼 commit 하면 그때 tail 이 전진한다.
 *   EN: Lend a contiguous readable span to a TX DMA; tail only moves on commit.
 *   ex: 10바이트가 있을 때 acquire -> 10, 6바이트만 보내고 commit(6) -> used 4
 *   hint: idx = tail & mask, n = min(used, size - idx). 비었으면 0 + *ptr = NULL.
 * ------------------------------------------------------------------------- */
uint32_t zc_read_acquire(zc_t *q, const uint8_t **ptr) {
    if (!q || !ptr) return 0;
    uint32_t used = q->head - q->tail;
    if (used == 0) {
        *ptr = NULL;
        q->rd_pending = 0;
        return 0;
    }
    uint32_t idx    = q->tail & q->mask;
    uint32_t to_end = q->size - idx;
    uint32_t n      = (used < to_end) ? used : to_end;
    *ptr = &q->buf[idx];
    q->rd_pending = n;
    return n;
    /* const uint8_t ** 인 이유: 소비자는 링 메모리를 **읽기만** 한다. 타입으로 못 박아
       두면 "TX 버퍼를 재활용한다고 그 자리에 다른 걸 써버리는" 사고를 컴파일러가 막는다. */
}

void zc_read_commit(zc_t *q, uint32_t n) {
    if (!q) return;
    if (n > q->rd_pending) n = q->rd_pending;
    q->tail += n;                /* 여기서야 공간이 생산자에게 반환된다 */
    q->rd_pending = 0;
}

/* ---------------------------------------------------------------------------
 * Q6.  zc_peek_iov — 읽을 데이터를 최대 2조각(iovec)으로
 *   KO: 랩 때문에 데이터가 두 토막이 날 수 있다. 그 둘을 한 번에 돌려주고 총 바이트
 *       수를 리턴한다. 랩이 없으면 n1 = 0, 비었으면 0 + 둘 다 0.
 *       ★비파괴 — tail 은 움직이지 않는다.
 *   EN: Expose readable data as up to two iovec segments (writev / SG-DMA ready).
 *   ex: size 8, tail=6, used=6 -> (buf+6, 2) + (buf+0, 4), 리턴 6
 *   hint: 첫 조각 = min(used, size - (tail & mask)), 둘째 조각 = used - 첫 조각.
 * ------------------------------------------------------------------------- */
uint32_t zc_peek_iov(const zc_t *q, const uint8_t **p0, uint32_t *n0,
                     const uint8_t **p1, uint32_t *n1) {
    if (!q || !p0 || !n0 || !p1 || !n1) return 0;
    *p0 = NULL; *n0 = 0;
    *p1 = NULL; *n1 = 0;
    uint32_t used = q->head - q->tail;
    if (used == 0) return 0;

    uint32_t idx   = q->tail & q->mask;
    uint32_t first = q->size - idx;
    if (first > used) first = used;

    *p0 = &q->buf[idx];
    *n0 = first;
    if (used > first) {                 /* 랩 -> 나머지는 버퍼 앞쪽에 있다 */
        *p1 = &q->buf[0];
        *n1 = used - first;
    }
    return used;
    /* 이 모양 그대로 POSIX writev(fd, iov, 2) 나 scatter-gather DMA 디스크립터
       2개에 꽂으면 된다. 커널/DMA 엔진이 두 조각을 한 번의 호출로 처리하므로
       "랩 때문에 두 번 호출" 하는 오버헤드조차 없앨 수 있다.
       SG 가 없는 주변장치라면 조각 하나씩 두 번 전송하면 된다 — 중요한 건
       **중간 버퍼로 합치려고 memcpy 하지 않는 것**이다. */
}

// ===========================================================================
// ★ 랩 문제와 bip-buffer
// ---------------------------------------------------------------------------
//   위 write_acquire 는 랩 지점에서 "끝까지만" 준다. 그래서 1KB 프레임을 한 방에
//   DMA 로 받고 싶은데 버퍼 끝이 300바이트밖에 안 남았으면 300/724 로 쪼개야 한다.
//   **bip-buffer**(bipartite buffer, Simon Cooke)는 이 쪼개짐을 없앤다:
//     - 쓰기 요청이 버퍼 끝에 안 들어가면, 끝의 자투리를 그냥 포기하고
//       **watermark(유효 데이터의 실제 끝 위치)를 기록한 뒤 앞으로 되감아** 0번지부터 쓴다.
//     - 읽는 쪽은 tail 이 watermark 에 도달하면 "여기서 끝났구나" 하고 0번지로 점프한다.
//     - 즉 링을 항상 한 덩어리(A 영역) 또는 두 덩어리(A + 앞쪽 B 영역)로만 유지하고,
//       빌려주는 구간은 **언제나 연속**임을 보장한다.
//     - 대가: 자투리 낭비 + 상태(watermark) 하나 추가. 얻는 것: "요청한 길이를
//       연속으로 받거나, 아니면 실패" 라는 단순한 계약 -> DMA 드라이버가 깔끔해진다.
//   가변 길이 프레임을 DMA 로 통째로 주고받는 코드라면 bip-buffer 가 정답에 가깝다.
//
// ★ DMA "half-transfer" 인터럽트로 더블버퍼처럼 쓰기
//   circular DMA 모드로 링 전체를 한 바퀴 돌게 걸어두고, HT(half-transfer)와
//   TC(transfer-complete) 인터럽트를 둘 다 켠다. HT 에서는 앞 절반을, TC 에서는
//   뒤 절반을 소비한다 — DMA 는 멈추지 않고 계속 돌고, CPU 는 항상 "지금 DMA 가
//   쓰고 있지 않은 절반"만 만진다. 재시작 지연(gap)이 0 이라 고속 ADC/UART RX 에서
//   표준 패턴이다. 잔량까지 정확히 알고 싶으면 NDTR(남은 전송 수) 레지스터를 읽어
//   현재 write 위치를 계산하고, UART 라면 idle-line 인터럽트를 함께 쓴다.
//
// ★ DMA 함정 3가지
//   1) **캐시 일관성 (D-cache)**
//      STM32F7/H7, Cortex-A 처럼 D-cache 가 있는 코어에서 DMA 는 캐시를 우회해
//      물리 메모리를 직접 읽고 쓴다. 그래서
//        - DMA 가 채운 메모리를 CPU 가 읽기 전  -> **invalidate** (낡은 캐시 라인 버리기)
//        - CPU 가 채운 메모리를 DMA 가 읽기 전  -> **clean/writeback** (캐시 내용을 RAM 으로)
//      을 안 하면 조용히 낡은 데이터를 쓴다. 게다가 invalidate 는 **캐시 라인 단위(보통 32B)**
//      라서, 같은 라인에 걸친 이웃 변수까지 함께 날아간다. 그래서 DMA 버퍼는
//      **32바이트 정렬 + 32바이트 배수 크기**로 잡는다(링 size 를 pow2 로 두면 32의
//      배수는 자동). 또는 MPU 로 그 영역만 non-cacheable 로 지정하는 방법도 흔하다.
//   2) **MPU / 메모리 영역 제약**
//      모든 DMA 컨트롤러가 모든 RAM 에 닿는 건 아니다. 대표적으로 STM32H7 의 DTCM
//      (0x2000_0000)은 기본 링크 스크립트상 .bss 가 얹히는 곳인데 DMA1/2 가 접근하지
//      못한다 -> 전송이 아예 시작되지 않거나 버스 폴트가 난다. 링 버퍼를 SRAM1/2/4 같은
//      접근 가능한 영역에 명시적으로 배치(__attribute__((section(".dma_ram")))) 해야 한다.
//      MPU 설정, 버스 매트릭스 권한, 캐시 속성까지 같이 봐야 하는 이유다.
//   3) **진행 중 수정 금지**
//      commit 전 구간(wr_pending/rd_pending)을 다른 코드가 건드리면 안 된다. DMA 가
//      읽는 중에 내용을 바꾸면 전송된 프레임이 중간부터 달라지고, 쓰는 중에 CPU 가
//      같은 자리를 만지면 둘이 서로를 덮어쓴다. 코드 리뷰에서 "이 포인터의 수명이
//      DMA 완료까지인가?"를 반드시 묻는다.
// ===========================================================================

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
// ===========================================================================
int main(void) {
    zc_t    q;
    uint8_t sto[16];

    memset(&q, 0, sizeof q);
    memset(sto, 0, sizeof sto);

    // -------- Q1 --------
    printf("\n[Q1] init / free / 빈 버퍼에서의 write_acquire\n");
    T("size 12 (2의 거듭제곱 아님) init 거부", !zc_init(&q, sto, 12));
    T("size 16 init 성공", zc_init(&q, sto, 16));
    T("빈 링: used == 0, free == 16", zc_used(&q) == 0 && zc_free(&q) == 16);
    {
        uint8_t *wp = NULL;
        uint32_t n  = zc_write_acquire(&q, &wp);
        T("빈 링에서 acquire 는 16바이트 전부", n == 16);
        T("acquire 포인터는 버퍼 시작 주소 (링 내부 메모리를 그대로 빌려준다)",
          wp == sto);
        T("acquire 는 pending 만 잡을 뿐 used 는 그대로 0", zc_used(&q) == 0);
    }

    // -------- Q2 --------
    printf("\n[Q2] ★acquire -> 그 메모리에 직접 쓰기 -> commit (memcpy 0회)\n");
    {
        zc_init(&q, sto, 16);
        uint8_t *p = NULL;
        uint32_t cap = zc_write_acquire(&q, &p);
        bool can = (cap >= 5) && (p != NULL);
        T("acquire 가 5바이트 이상 + 유효 포인터를 준다", can);
        if (can) {
            /* ★ 여기가 zero-copy 의 전부다: 중간 버퍼 없이 링 메모리에 직접 쓴다.
               실제 코드라면 HAL_UART_Receive_DMA(&huart, p, cap) 로 DMA 에 넘긴다. */
            for (uint32_t i = 0; i < 5; ++i) p[i] = (uint8_t)(0x41 + i);  /* 'A'..'E' */
        }
        T("★commit 전에는 소비자에게 안 보인다 (used == 0)", zc_used(&q) == 0);

        zc_write_commit(&q, 5);
        T("commit(5) 후 used == 5, free == 11",
          zc_used(&q) == 5 && zc_free(&q) == 11);

        const uint8_t *rp = NULL;
        uint32_t rn = zc_read_acquire(&q, &rp);
        bool content = (rn == 5) && (rp != NULL);
        if (content)
            for (uint32_t i = 0; i < 5; ++i)
                if (rp[i] != (uint8_t)(0x41 + i)) { content = false; break; }
        T("memcpy 한 번 없이 쓴 내용이 그대로 읽힌다 ('A'..'E')", content);
        zc_read_commit(&q, rn);
        T("read_commit 후 링이 빈다", zc_used(&q) == 0 && zc_free(&q) == 16);
    }

    // -------- Q3 --------
    printf("\n[Q3] ★랩 지점: acquire 가 free 보다 작은 값(끝까지)만 준다\n");
    {
        zc_init(&q, sto, 16);
        uint8_t *p = NULL;
        uint32_t n = zc_write_acquire(&q, &p);
        if (p && n >= 12) for (uint32_t i = 0; i < 12; ++i) p[i] = (uint8_t)i;
        zc_write_commit(&q, 12);                 /* head = 12 */
        const uint8_t *rp = NULL;
        uint32_t rn = zc_read_acquire(&q, &rp);
        zc_read_commit(&q, rn);                  /* tail = 12 -> 링은 비었다 */
        T("준비: 링은 비었고 free 는 16 (head = tail = 12)",
          zc_used(&q) == 0 && zc_free(&q) == 16);

        p = NULL;
        n = zc_write_acquire(&q, &p);
        T("★free 는 16 인데 acquire 는 4 (버퍼 끝까지만 연속)", n == 4);
        T("그 4바이트는 buf+12 에서 시작", p == sto + 12);
        if (p && n == 4) for (uint32_t i = 0; i < 4; ++i) p[i] = (uint8_t)(0x90 + i);
        zc_write_commit(&q, 4);                  /* head = 16 -> 인덱스가 랩 */

        p = NULL;
        n = zc_write_acquire(&q, &p);
        T("두 번째 acquire 는 버퍼 앞쪽 12바이트", n == 12 && p == sto);
        if (p && n == 12) for (uint32_t i = 0; i < 12; ++i) p[i] = (uint8_t)(0xC0 + i);
        zc_write_commit(&q, 12);
        T("acquire/commit 두 번으로 링이 가득 찬다",
          zc_used(&q) == 16 && zc_free(&q) == 0);

        uint8_t *p2 = NULL;
        T("가득 찬 링에서 acquire 는 0 + NULL 포인터",
          zc_write_acquire(&q, &p2) == 0 && p2 == NULL);
    }

    // -------- Q4 --------
    printf("\n[Q4] commit 을 acquire 보다 작게 (DMA 조기 종료 = UART idle line)\n");
    {
        zc_init(&q, sto, 16);
        uint8_t *p = NULL;
        uint32_t cap = zc_write_acquire(&q, &p);
        T("acquire 는 16바이트를 빌려준다 (DMA 에 최대치를 걸어둔 상태)", cap == 16);
        if (p && cap >= 8) for (uint32_t i = 0; i < 8; ++i) p[i] = (uint8_t)(0x70 + i);

        /* 프레임이 3바이트에서 끝났다(idle line). DMA 를 멈추고 실제 수신량만 확정한다. */
        zc_write_commit(&q, 3);
        T("★acquire 16 / commit 3 -> head 는 3만 전진 (used 3, free 13)",
          zc_used(&q) == 3 && zc_free(&q) == 13);

        const uint8_t *rp = NULL;
        uint32_t rn = zc_read_acquire(&q, &rp);
        bool ok = (rn == 3) && (rp != NULL);
        if (ok) ok = (rp[0] == 0x70 && rp[1] == 0x71 && rp[2] == 0x72);
        T("commit 한 3바이트만 소비자에게 보인다 (나머지는 없던 일)", ok);
        zc_read_commit(&q, rn);

        uint8_t *p2 = NULL;
        uint32_t cap2 = zc_write_acquire(&q, &p2);
        T("다음 acquire 는 commit 지점(buf+3)부터 이어서 13바이트",
          p2 == sto + 3 && cap2 == 13);
        zc_write_commit(&q, 0);
        T("commit(0) 은 아무것도 공개하지 않는다 (used 그대로 0)", zc_used(&q) == 0);
    }

    // -------- Q5 --------
    printf("\n[Q5] read_acquire / read_commit — 'TX 완료 인터럽트에서 commit' 패턴\n");
    {
        zc_init(&q, sto, 16);
        uint8_t *p = NULL;
        uint32_t c = zc_write_acquire(&q, &p);
        if (p && c >= 10) for (uint32_t i = 0; i < 10; ++i) p[i] = (uint8_t)(0xE0 + i);
        zc_write_commit(&q, 10);

        const uint8_t *tx = NULL;
        uint32_t txn = zc_read_acquire(&q, &tx);
        T("TX DMA 에 그대로 넘길 연속 구간 10바이트 (buf 시작)",
          txn == 10 && tx == sto);
        T("★read_acquire 만으로는 used 가 줄지 않는다 (아직 전송 중)",
          zc_used(&q) == 10);

        /* DMA 가 6바이트를 내보내고 abort 됐다고 하자 (링크 끊김/우선순위 선점 등). */
        uint8_t  sink[16];
        uint32_t sent = 0;
        memset(sink, 0, sizeof sink);
        if (tx && txn >= 6) { for (uint32_t i = 0; i < 6; ++i) sink[i] = tx[i]; sent = 6; }
        T("DMA 가 실제로 가져간 바이트는 0xE0..0xE5",
          sent == 6 && sink[0] == 0xE0 && sink[5] == 0xE5);

        zc_read_commit(&q, sent);   /* TC/abort 인터럽트에서 '보낸 만큼만' 확정 */
        T("보낸 만큼만 tail 전진 -> used 4", zc_used(&q) == 4);

        const uint8_t *tx2 = NULL;
        uint32_t txn2 = zc_read_acquire(&q, &tx2);
        bool rest = (txn2 == 4) && (tx2 == sto + 6);
        if (rest)
            for (uint32_t i = 0; i < 4; ++i)
                if (tx2[i] != (uint8_t)(0xE6 + i)) { rest = false; break; }
        T("남은 4바이트가 끊김 없이 이어서 나온다 (0xE6..0xE9)", rest);
        zc_read_commit(&q, txn2);
        T("전부 소비하면 read_acquire 는 0 + NULL",
          zc_read_acquire(&q, &tx2) == 0 && tx2 == NULL && zc_used(&q) == 0);
    }

    // -------- Q6 --------
    printf("\n[Q6] zc_peek_iov — 랩 없으면 1조각, 랩되면 2조각, 비면 0조각\n");
    {
        uint8_t  sto8[8];
        zc_t     q8;
        const uint8_t *p0 = (const uint8_t *)1;   /* 일부러 쓰레기값 */
        const uint8_t *p1 = (const uint8_t *)1;
        uint32_t n0 = 99, n1 = 99;

        memset(&q8, 0, sizeof q8);
        memset(sto8, 0, sizeof sto8);
        zc_init(&q8, sto8, 8);

        T("빈 링: 총 0바이트, 두 조각 모두 0",
          zc_peek_iov(&q8, &p0, &n0, &p1, &n1) == 0 && n0 == 0 && n1 == 0);

        uint8_t *w = NULL;
        uint32_t c = zc_write_acquire(&q8, &w);
        if (w && c >= 8) for (uint32_t i = 0; i < 8; ++i) w[i] = (uint8_t)(0x21 + i);
        zc_write_commit(&q8, 8);

        uint32_t tot = zc_peek_iov(&q8, &p0, &n0, &p1, &n1);
        bool one = (tot == 8) && (n0 == 8) && (n1 == 0) && (p0 == sto8) && (p1 == NULL);
        if (one)
            for (uint32_t i = 0; i < 8; ++i)
                if (p0[i] != (uint8_t)(0x21 + i)) { one = false; break; }
        T("랩 안 된 경우: 1조각 (n1 == 0)", one);

        /* 6바이트 소비 -> tail=6, 그다음 4바이트 추가 -> head=12 : 랩 발생 */
        const uint8_t *r = NULL;
        zc_read_acquire(&q8, &r);
        zc_read_commit(&q8, 6);
        w = NULL;
        c = zc_write_acquire(&q8, &w);
        if (w && c >= 4) for (uint32_t i = 0; i < 4; ++i) w[i] = (uint8_t)(0x31 + i);
        zc_write_commit(&q8, 4);

        tot = zc_peek_iov(&q8, &p0, &n0, &p1, &n1);
        bool two = (tot == 6) && (n0 == 2) && (n1 == 4)
                && (p0 == sto8 + 6) && (p1 == sto8);
        if (two) two = (p0[0] == 0x27 && p0[1] == 0x28
                     && p1[0] == 0x31 && p1[3] == 0x34);
        T("랩 된 경우: 2조각이고 n0 + n1 == used(6)", two);
        T("iov 는 비파괴 — used 는 여전히 6", zc_used(&q8) == 6);
        T("한 조각짜리 read_acquire 는 첫 조각(2바이트)만 준다",
          zc_read_acquire(&q8, &r) == 2 && r == sto8 + 6);
    }

    printf("\n== 레벨 5b — zero-copy 인터페이스 (DMA 에 링 메모리를 직접 넘기기) ==  PASS %d / FAIL %d\n",
           g_pass, g_fail);
    return g_fail ? 1 : 0;
}
