// L5b_zerocopy.c  —  PRACTICE STUB (직접 채워넣기)
// 레벨 5b — zero-copy 인터페이스 (DMA 에 링 메모리를 직접 넘기기)  —  Q1~Q6
// ---------------------------------------------------------------------------
// 빌드/실행:  make prob N=L5b_zerocopy
//   또는:     cc -std=c11 -Wall -Wextra -O0 -g problems/L5b_zerocopy.c -o build/L5b_zerocopy_prob && ./build/L5b_zerocopy_prob
//
// 각 함수의 '// TODO' 를 구현하고 다시 실행 -> [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일/실행은 되며 대부분 FAIL 로 뜬다.)
//
// 이 레벨에서 배우는 것:
//   - acquire/commit **2단계 API**: "링의 내부 메모리를 직접 빌려 쓰고, 다 쓴 만큼만 확정한다".
//   - 랩(wrap) 지점에서 구간이 **둘로 쪼개지는** 문제와 그 해법(bip-buffer, iovec).
//   - DMA 특유의 함정: 캐시 일관성, 메모리 영역 제약, 진행 중 버퍼 수정 금지.
//
// ★ 왜 zero-copy 인가
//   1KB 프레임을 UART 로 보낸다고 하자. 지금까지의 API(ring_write / ring_read)는
//   "사용자 버퍼 -> 링" 한 번, "링 -> DMA 용 버퍼" 한 번, 총 두 번 memcpy 한다.
//   Cortex-M4 에서 1KB memcpy 는 수백~수천 사이클이고 그동안 D-cache 도 오염된다.
//   DMA 는 **주소와 길이만** 있으면 되므로, 링 내부 메모리를 그대로 가리키게 하면
//   CPU 사이클은 0 이다.
//
// ★ 왜 2단계(acquire -> commit)인가
//   DMA 가 도는 동안 링은 그 구간을 "예약됨(in-flight)"으로 알고 있어야 한다.
//   commit 전에는 head 가 움직이지 않으므로 **소비자에게는 그 데이터가 보이지 않는다**
//   = 미완성 데이터 노출 방지.
//     acquire = "여기에 쓸게, 아무도 건드리지 마" / commit = "여기까지 유효해, 이제 봐도 돼".
//
// 레벨 로드맵에서의 위치:
//   L4(lock-free SPSC) -> L5a(overwrite) -> **L5b(여기, zero-copy + DMA)**.
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
// head/tail 은 L2 이후와 같은 free-running 인덱스. 새로 생긴 건 두 개의 "예약" 필드다.
//
//   wr_pending : write_acquire 로 잡아둔(=DMA 가 채우는 중인) 구간 길이
//   rd_pending : read_acquire  로 잡아둔(=DMA 가 내보내는 중인) 구간 길이
//
// commit 전 구간은 **오직 그 DMA 채널만** 건드릴 수 있다. pending 필드는 그 계약을
// 코드로 표현한 것이다.
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
    (void)q; (void)buf; (void)size;
    // TODO: implement (pow2 검사, mask = size-1, head/tail/wr_pending/rd_pending = 0)
    return false;  // placeholder
}

uint32_t zc_used(const zc_t *q) {
    (void)q;
    // TODO: implement (head - tail)
    return 0;  // placeholder
}

uint32_t zc_free(const zc_t *q) {
    (void)q;
    // TODO: implement (size - used)
    return 0;  // placeholder
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
 *         왜 free 전부를 못 주는가? 한 번의 DMA 는 **연속 메모리만** 다룰 수 있다.
 * ------------------------------------------------------------------------- */
uint32_t zc_write_acquire(zc_t *q, uint8_t **ptr) {
    (void)q;
    if (ptr) *ptr = NULL;
    // TODO: implement (free==0 이면 0 + *ptr=NULL, 아니면 *ptr=&buf[head&mask],
    //       wr_pending = min(free, size - (head&mask)) 를 기억하고 리턴)
    return 0;  // placeholder
}

void zc_write_commit(zc_t *q, uint32_t n) {
    (void)q; (void)n;
    // TODO: implement (n 을 wr_pending 으로 클램프, head += n, wr_pending = 0)
    //       ★ head 를 움직이는 이 순간이 곧 "소비자에게 공개"하는 시점이다.
}

/* ---------------------------------------------------------------------------
 * Q5.  zc_read_acquire / zc_read_commit  (소비자 측 zero-copy)
 *   KO: 읽을 수 있는 **연속 구간**의 시작 주소와 길이를 준다. TX DMA 에 그대로 넘기면
 *       된다. acquire 만으로는 used 가 줄지 않는다(전송 중). 전송 완료(TC) 인터럽트에서
 *       실제 보낸 만큼 commit 하면 그때 tail 이 전진한다.
 *   EN: Lend a contiguous readable span to a TX DMA; tail only moves on commit.
 *   ex: 10바이트가 있을 때 acquire -> 10, 6바이트만 보내고 commit(6) -> used 4
 *   hint: idx = tail & mask, n = min(used, size - idx). 비었으면 0 + *ptr = NULL.
 *         인자가 const uint8_t ** 인 이유 — 소비자는 링 메모리를 읽기만 한다.
 * ------------------------------------------------------------------------- */
uint32_t zc_read_acquire(zc_t *q, const uint8_t **ptr) {
    (void)q;
    if (ptr) *ptr = NULL;
    // TODO: implement (used==0 이면 0, 아니면 *ptr=&buf[tail&mask],
    //       rd_pending = min(used, size - (tail&mask)) 를 기억하고 리턴)
    return 0;  // placeholder
}

void zc_read_commit(zc_t *q, uint32_t n) {
    (void)q; (void)n;
    // TODO: implement (n 을 rd_pending 으로 클램프, tail += n, rd_pending = 0)
}

/* ---------------------------------------------------------------------------
 * Q6.  zc_peek_iov — 읽을 데이터를 최대 2조각(iovec)으로
 *   KO: 랩 때문에 데이터가 두 토막이 날 수 있다. 그 둘을 한 번에 돌려주고 총 바이트
 *       수를 리턴한다. 랩이 없으면 n1 = 0, 비었으면 0 + 둘 다 0.
 *       ★비파괴 — tail 은 움직이지 않는다.
 *   EN: Expose readable data as up to two iovec segments (writev / SG-DMA ready).
 *   ex: size 8, tail=6, used=6 -> (buf+6, 2) + (buf+0, 4), 리턴 6
 *   hint: 첫 조각 = min(used, size - (tail & mask)), 둘째 조각 = used - 첫 조각.
 *         이 모양 그대로 writev(fd, iov, 2) 나 scatter-gather DMA 디스크립터에 꽂힌다.
 * ------------------------------------------------------------------------- */
uint32_t zc_peek_iov(const zc_t *q, const uint8_t **p0, uint32_t *n0,
                     const uint8_t **p1, uint32_t *n1) {
    (void)q;
    if (p0) *p0 = NULL;
    if (n0) *n0 = 0;
    if (p1) *p1 = NULL;
    if (n1) *n1 = 0;
    // TODO: implement (used==0 이면 0, 첫 조각 채우고, 랩이면 둘째 조각까지, 총합 리턴)
    return 0;  // placeholder
}

// ===========================================================================
// ★ 랩 문제와 bip-buffer (개념만 — 구현 과제는 아니다)
// ---------------------------------------------------------------------------
//   위 write_acquire 는 랩 지점에서 "끝까지만" 준다. 그래서 1KB 프레임을 한 방에
//   DMA 로 받고 싶은데 버퍼 끝이 300바이트밖에 안 남았으면 300/724 로 쪼개야 한다.
//   **bip-buffer**(bipartite buffer)는 이 쪼개짐을 없앤다:
//     - 쓰기 요청이 버퍼 끝에 안 들어가면 끝의 자투리를 포기하고,
//       **watermark(유효 데이터의 실제 끝)를 기록한 뒤 앞으로 되감아** 0번지부터 쓴다.
//     - 읽는 쪽은 tail 이 watermark 에 닿으면 0번지로 점프한다.
//     - 즉 빌려주는 구간이 **언제나 연속**임을 보장한다.
//     - 대가: 자투리 낭비 + 상태 하나 추가. 얻는 것: "요청 길이를 연속으로 받거나 실패"
//       라는 단순한 계약 -> DMA 드라이버가 깔끔해진다.
//
// ★ DMA 함정 3가지 (solution 에 자세한 설명)
//   1) 캐시 일관성 — D-cache 가 있는 MCU 는 DMA 전후로 invalidate / clean 이 필요하고,
//      그래서 링을 캐시라인(보통 32B) 정렬 + 배수 크기로 잡는다.
//   2) MPU / 메모리 영역 — DMA 가 접근 못 하는 RAM(예: STM32H7 DTCM)에 링을 두면 안 된다.
//   3) 진행 중 수정 금지 — commit 전 구간을 다른 코드가 건드리면 안 된다.
// ===========================================================================

// ===========================================================================
// main : 모든 케이스 PASS/FAIL (건드리지 말 것)
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
