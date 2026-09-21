// L5a_overwrite.c  —  PRACTICE STUB (직접 채워넣기)
// 레벨 5a — 덮어쓰기 모드 로그 버퍼 (블랙박스 링)  —  Q1~Q6
// ---------------------------------------------------------------------------
// 빌드/실행:  make prob N=L5a_overwrite
//   또는:     cc -std=c11 -Wall -Wextra -O0 -g problems/L5a_overwrite.c -o build/L5a_overwrite_prob && ./build/L5a_overwrite_prob
//
// 각 함수의 '// TODO' 를 구현하고 다시 실행 -> [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일/실행은 되며 대부분 FAIL 로 뜬다.)
//
// 이 레벨에서 배우는 것:
//   - 지금까지의 링버퍼는 "가득 차면 실패(drop-new)"였다. 여기서는 정책을 뒤집는다:
//     **가득 차면 가장 오래된 것을 버린다(drop-old / overwrite)**.
//   - 왜 뒤집는가: 크래시 직전 N바이트가 가장 중요한 로그이기 때문이다. 링이 가득 찼다고
//     새 로그를 버리면 정작 사고 순간의 로그가 통째로 사라진다. 비행기 블랙박스와 같은
//     구조 — 마지막 30분만 남기고 계속 덮어쓴다.
//   - 바이트 단위로 덮어쓰면 **가장 오래된 레코드의 뒷부분만 남아** 파서가 깨진다.
//     그래서 실제 로그 링은 **레코드 단위로** 버린다 (5a-2).
//   - 그리고 결정적으로: **덮어쓰기는 레벨 4의 SPSC 무락(lock-free) 논증을 깨뜨린다.**
//     생산자가 tail 을 건드리기 시작하기 때문이다. 그 이유와 해법 3가지는 solution 참고.
//
// 레벨 로드맵에서의 위치:
//   L4(lock-free SPSC, drop-new) -> **L5a(여기, drop-old/overwrite)** -> L5b(zero-copy).
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
// 5a-1. 바이트 단위 덮어쓰기 링
// ---------------------------------------------------------------------------
// 인덱스는 L2 이후와 동일하게 free-running(마스킹하지 않고 계속 증가)이다.
//   used = head - tail   (부호 없는 뺄셈이라 32비트 랩이 일어나도 정확)
//   접근은 buf[idx & mask]
// 달라진 건 딱 하나: full 일 때 push 가 실패하는 대신 **tail 을 한 칸 밀어버린다**.
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
 *         used 가 size 를 넘지 않는다. push 의 리턴 타입이 void 인 것 자체가
 *         "이 함수는 실패할 수 없다"는 계약이다.
 * ------------------------------------------------------------------------- */
bool ovr_init(ovr_t *q, uint8_t *buf, uint32_t size) {
    (void)q; (void)buf; (void)size;
    // TODO: implement (pow2 검사, mask = size-1, head/tail/overwritten = 0)
    return false;  // placeholder
}

uint32_t ovr_used(const ovr_t *q) {
    (void)q;
    // TODO: implement (head - tail, 부호 없는 뺄셈)
    return 0;  // placeholder
}

void ovr_push(ovr_t *q, uint8_t v) {
    (void)q; (void)v;
    // TODO: implement (가득이면 tail++ / overwritten++ 후, buf[head & mask] = v; head++)
}

bool ovr_pop(ovr_t *q, uint8_t *out) {
    (void)q; (void)out;
    // TODO: implement (empty 면 out 을 건드리지 말고 false)
    return false;  // placeholder
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
 *         n == 0 이거나 src 가 NULL 이면 아무 일도 하지 않는다.
 * ------------------------------------------------------------------------- */
void ovr_write(ovr_t *q, const uint8_t *src, uint32_t n) {
    (void)q; (void)src; (void)n;
    // TODO: implement (ovr_push 를 n 번 — n > size 여도 자연스럽게 동작한다)
}

/* ---------------------------------------------------------------------------
 * Q4.  ovr_snapshot — 비파괴 덤프 (크래시 덤프용)
 *   KO: 가장 최근 n 바이트를 **시간순(오래된 것 먼저)** 으로 dst 에 복사하고 복사한
 *       개수를 리턴한다. 요청 n 이 used 보다 크면 used 만큼만. ★호출 후에도 링의
 *       내용과 used 는 전혀 변하지 않는다(pop 이 아니다).
 *   EN: Non-destructive dump of the most recent n bytes, oldest-first.
 *   ex: 링에 34..3B 가 있을 때 snapshot(dst, 4) -> 38,39,3A,3B 리턴값 4, used 그대로
 *   hint: m = min(n, used), 시작 인덱스는 head - m. 거기서부터 마스킹하며 m 번 복사.
 *         인자가 const ovr_t * 인 이유를 생각해 볼 것 — 덤프가 증거를 지우면 안 된다.
 * ------------------------------------------------------------------------- */
uint32_t ovr_snapshot(const ovr_t *q, uint8_t *dst, uint32_t n) {
    (void)q; (void)dst; (void)n;
    // TODO: implement (m = min(n, used), start = head - m, dst[i] = buf[(start+i) & mask])
    return 0;  // placeholder
}

// ===========================================================================
// 5a-2. 레코드 단위 덮어쓰기 (프레임이 반쪽 나면 안 되는 경우)
// ---------------------------------------------------------------------------
// 바이트 단위 덮어쓰기의 치명적 결함:
//   [len=5][A B C D E][len=3][X Y Z] 가 들어 있는 링에 1바이트가 덮어써지면
//   남는 건 **[B][C D E][3][X Y Z]** 다. tail 이 레코드 중간을 가리키게 되고,
//   파서는 B(=0x42=66) 를 길이로 읽어 66바이트를 소비하려 든다 -> 그 뒤의 멀쩡한
//   레코드까지 전부 오파싱된다. 한 바이트 손실이 로그 전체를 못 읽게 만드는 것이다.
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

/* ---------------------------------------------------------------------------
 * Q5.  rlog_init / rlog_push / rlog_pop / rlog_dropped_records
 *   KO: push 는 공간이 부족하면 **가장 오래된 레코드를 통째로** 버리기를 반복한 뒤
 *       넣는다. len 이 0 이거나 len+1 > size 면 false(그건 정책이 아니라 오용이다).
 *       pop 은 가장 오래된 레코드를 꺼내 길이를 리턴, 없으면 0.
 *   EN: Record-granular overwriting log: evict whole oldest records to make room.
 *   ex: size 64 링에 3바이트, 5바이트 레코드를 넣으면 pop 이 3, 5 순서로 나온다
 *   hint: 필요한 공간은 len + 1 (헤더 1바이트). free = size - used.
 *         "가장 오래된 레코드 버리기" = tail 위치의 len 을 읽어 tail += 1 + len.
 *         cap 이 레코드 길이보다 작으면 소비하지 말고 0 을 리턴해 데이터를 지키자.
 * ------------------------------------------------------------------------- */
bool rlog_init(rlog_t *q, uint8_t *buf, uint32_t size) {
    (void)q; (void)buf; (void)size;
    // TODO: implement (dropped = 0, 나머지는 ovr_init 재사용)
    return false;  // placeholder
}

bool rlog_push(rlog_t *q, const uint8_t *rec, uint8_t len) {
    (void)q; (void)rec; (void)len;
    // TODO: implement (len==0 / len+1>size 거부, free < len+1 인 동안 가장 오래된
    //       레코드를 통째로 버리기, 그다음 [len][payload] 기록)
    return false;  // placeholder
}

uint32_t rlog_pop(rlog_t *q, uint8_t *dst, uint32_t cap) {
    (void)q; (void)dst; (void)cap;
    // TODO: implement (비었으면 0, len > cap 이면 소비하지 않고 0,
    //       아니면 payload 를 dst 로 복사하고 tail += 1 + len, len 리턴)
    return 0;  // placeholder
}

uint32_t rlog_dropped_records(const rlog_t *q) {
    (void)q;
    // TODO: implement
    return 0;  // placeholder
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
// main : 모든 케이스 PASS/FAIL (건드리지 말 것)
// ===========================================================================
#define NREC 12

int main(void) {
    ovr_t   q;
    uint8_t sto[8];
    uint8_t v = 0;

    /* init 이 정말 초기화하는지 보려고 일부러 0 으로 밀어둔다 (스택은 원래 쓰레기다). */
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
