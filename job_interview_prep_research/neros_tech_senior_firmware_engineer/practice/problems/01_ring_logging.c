// 01_ring_logging.c  —  PRACTICE STUB (직접 채워넣기)
// ISR-safe 로깅 프런트엔드 (SPSC ring + deferred formatting)  —  Q1~Q6
// ---------------------------------------------------------------------------
// 빌드/실행:  make prob N=01_ring_logging
//   또는:     cc -std=c11 -Wall -Wextra -O0 -g problems/01_ring_logging.c -o /tmp/n01p && /tmp/n01p
//
// 각 함수의 '// TODO' 를 구현하고 다시 실행 -> [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일/실행은 되며 대부분 FAIL 로 뜬다.)
//
// 왜 이 토픽인가 (Neros "Senior Firmware Engineer, Platform"):
//   - 모든 펌웨어 팀이 공유하는 공통 런타임(logging/telemetry/IPC/config)의 1번 부품이
//     "ISR 에서도 안전한 로그 큐"다. 여기가 흔들리면 팀 전체가 디버깅을 못 한다.
//   - MCU 와 Linux 타깃에서 같은 API 로 돌아야 하므로 lock-free SPSC 바이트 링이 기본형.
//   - compute/memory/bandwidth 트레이드오프가 그대로 드러난다: 포맷팅은 호스트로 미루고
//     (fmt_id + 인자만 전송), RAM 은 고정 크기 링, 넘치면 조용히 죽지 말고 drop 카운터.
//   - 디버깅 숙련도 = "로그가 언제, 몇 개 유실됐는지"를 숫자로 말할 수 있는가.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdatomic.h>

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
// Q1 : SPSC 바이트 링 (power-of-two + free-running index)
// ---------------------------------------------------------------------------
// 생산자(로그를 남기는 쪽 = ISR/태스크)만 head 를 store 하고, 소비자(flush 스레드)
// 만 tail 을 store 한다.  서로 상대 인덱스는 읽기만 하므로 write-write 경합이
// 구조적으로 없다 -> 락 불필요.  ISR 안에서 mutex 를 잡으면 우선순위 역전/데드락
// 이므로 이 성질이 결정적이다.
//
// head/tail 은 마스킹하지 않고 계속 증가(free-running)시키고, 버퍼 접근 시에만
// & mask 로 감싼다.  used = head - tail (부호없는 뺄셈이라 랩되어도 정확) 이므로
// 한 칸을 희생할 필요가 없다 -> 용량 = size 전부.
// ===========================================================================
typedef struct {
    uint8_t              *buf;
    uint32_t              size;     // 반드시 2의 거듭제곱
    uint32_t              mask;     // size - 1
    atomic_uint_fast32_t  head;     // 생산자만 store
    atomic_uint_fast32_t  tail;     // 소비자만 store
    uint32_t              dropped;  // 생산자만 갱신 -> 원자화 불필요
} spsc_t;

/* ---------------------------------------------------------------------------
 * Q1.  SPSC 링 초기화 / 1바이트 push / 1바이트 pop
 *   KO: size 가 2의 거듭제곱이 아니면 init 은 false. push 는 데이터를 먼저 쓰고
 *       head 를 release store 로 공개, pop 은 head 를 acquire load 로 본 뒤 읽고
 *       tail 을 release store. full = (head-tail)==size, empty = head==tail.
 *   EN: Init/push/pop for a lock-free single-producer/single-consumer byte ring
 *       with free-running indices; publish data before advancing the index.
 *   ex: size 8 -> 8바이트 전부 저장 가능, 9번째 push 는 false
 *   hint: (size & (size-1)) == 0  <=> size 는 2의 거듭제곱
 *         버퍼 접근은 buf[idx & mask], 인덱스 자체는 마스킹하지 않는다.
 * ------------------------------------------------------------------------- */
bool spsc_init(spsc_t *q, uint8_t *buf, uint32_t size) {
    (void)q; (void)buf; (void)size;
    // TODO: implement (pow2 검사, mask=size-1, head/tail/dropped 0 으로 atomic_init)
    return false;  // placeholder
}

// 생산자 컨텍스트(ISR 포함)에서만 호출.
bool spsc_push(spsc_t *q, uint8_t data) {
    (void)q; (void)data;
    // TODO: implement (relaxed load head, acquire load tail, 데이터 먼저, release store head)
    return false;  // placeholder
}

// 소비자 컨텍스트에서만 호출.
bool spsc_pop(spsc_t *q, uint8_t *out) {
    (void)q; (void)out;
    // TODO: implement (relaxed load tail, acquire load head, 데이터 먼저, release store tail)
    return false;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q2.  bulk write (all-or-nothing) + 남은 공간
 *   KO: 로그 레코드는 쪼개지면 못 읽는다. n 바이트가 전부 들어갈 때만 쓰고,
 *       안 되면 한 바이트도 쓰지 않고 dropped++ (drop-new 정책) 후 0 반환.
 *   EN: Atomic bulk write: copy all n bytes or none. On overflow drop the new
 *       record and bump the dropped counter; also report free space.
 *   ex: 8칸 링에 6바이트가 차 있을 때 5바이트 write -> 0 반환, dropped 1 증가
 *   hint: free = size - (head - tail).  head 는 마지막에 n 만큼 한 번에 전진.
 * ------------------------------------------------------------------------- */
uint32_t spsc_free_space(const spsc_t *q) {
    (void)q;
    // TODO: implement (size - (head - tail))
    return 0;  // placeholder
}

uint32_t spsc_dropped(const spsc_t *q) {
    (void)q;
    // TODO: implement
    return 0;  // placeholder
}

size_t spsc_write(spsc_t *q, const uint8_t *src, size_t n) {
    (void)q; (void)src; (void)n;
    // TODO: implement (안 들어가면 dropped++ 후 0, 들어가면 전부 복사 후 n)
    return 0;  // placeholder
}

// ===========================================================================
// Q3 : 레벨 필터 (런타임 모듈 임계값 + 컴파일타임 하한)
// ===========================================================================
#define LOG_TRACE 0
#define LOG_DEBUG 1
#define LOG_INFO  2
#define LOG_WARN  3
#define LOG_ERROR 4

// 컴파일타임 하한: 이보다 낮은 레벨의 로그는 코드/문자열째로 빌드에서 빠진다.
// (매크로 안에서 if (level >= LOG_LEVEL_MIN) 로 감싸면 컴파일러가 통째로 제거)
#ifndef LOG_LEVEL_MIN
#define LOG_LEVEL_MIN LOG_DEBUG
#endif

/* ---------------------------------------------------------------------------
 * Q3.  로그를 실제로 내보낼지 판단
 *   KO: level 이 모듈별 런타임 임계값(module_level) 이상이고, 동시에 컴파일타임
 *       하한 LOG_LEVEL_MIN 이상일 때만 true. 두 관문을 모두 통과해야 한다.
 *   EN: Emit only when level >= module threshold AND level >= LOG_LEVEL_MIN.
 *   ex: module_level=INFO 이면 ERROR 는 emit, DEBUG 는 차단
 * ------------------------------------------------------------------------- */
bool log_should_emit(uint8_t level, uint8_t module_level) {
    (void)level; (void)module_level;
    // TODO: implement (컴파일타임 하한 먼저, 그다음 런타임 임계값)
    return false;  // placeholder
}

// ===========================================================================
// Q4/Q5 : deferred formatting 로그 레코드
// ---------------------------------------------------------------------------
// 왜 MCU 가 포맷 문자열 대신 fmt_id 를 보내는가?
//   1) flash: "imu: gyro=%d dt=%u us\n" 같은 문자열 수백 개가 ROM 을 먹는다.
//      ID 만 남기면 문자열 테이블은 빌드 산출물(호스트)로 빠진다.
//   2) bandwidth: 40바이트 문자열 대신 16바이트 바이너리 -> 링크 예산 절약.
//   3) compute: MCU 에서 snprintf 는 수천 사이클 + 스택 수백 바이트. ISR 금지 사유.
//      포맷은 호스트 PC 가 한다 (deferred / host-side formatting).
//   4) 구조화: 레벨/타임스탬프/인자가 고정 필드라 파싱·필터링이 기계적으로 가능.
// 레이아웃 (little-endian):
//   [u16 fmt_id][u8 level][u8 nargs][u32 ts_ms][u32 arg0]...[u32 arg(n-1)]
//   총 길이 = 8 + 4*nargs
// ===========================================================================
#define LOG_MAX_ARGS 8

typedef struct {
    uint16_t fmt_id;
    uint8_t  level;
    uint8_t  nargs;
    uint32_t ts_ms;
    uint32_t args[LOG_MAX_ARGS];
} log_rec_t;

/* ---------------------------------------------------------------------------
 * Q4.  로그 레코드 인코딩 (little-endian, 수동 직렬화)
 *   KO: out 에 헤더 8바이트 + 인자 4바이트씩을 LE 로 쓰고 쓴 바이트 수를 반환.
 *       cap 에 안 들어가거나 nargs 가 LOG_MAX_ARGS 초과면 한 바이트도 안 쓰고 0.
 *   EN: Serialize a log record little-endian byte by byte; return bytes written,
 *       0 (and write nothing) if it does not fit.
 *   ex: fmt_id=0x1234, nargs=2 -> 16바이트, out[0]=0x34, out[1]=0x12
 *   note: memcpy(struct) 가 아니라 바이트 단위로 쓴다 -> 패딩/엔디안/정렬 무관.
 * ------------------------------------------------------------------------- */
size_t log_encode(uint8_t *out, size_t cap, uint16_t fmt_id, uint8_t level,
                  uint32_t ts_ms, const uint32_t *args, uint8_t nargs) {
    (void)out; (void)cap; (void)fmt_id; (void)level;
    (void)ts_ms; (void)args; (void)nargs;
    // TODO: implement (need = 8 + 4*nargs, 검사 먼저 -> 그다음 바이트 단위 LE 기록)
    return 0;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q5.  로그 레코드 디코딩 (Q4 의 역함수, 길이 검증 포함)
 *   KO: len 이 최소 헤더(8) 이상인지, nargs 가 범위 안인지, 그리고 len 이 정확히
 *       8 + 4*nargs 인지 검사한다. 하나라도 어긋나면 false (잘린 입력 거부).
 *   EN: Inverse of log_encode. Reject truncated or length-inconsistent input:
 *       len must be exactly 8 + 4*nargs and nargs <= LOG_MAX_ARGS.
 *   ex: 16바이트짜리 레코드를 15바이트만 주면 false
 *   note: 링크에서 온 바이트는 전부 적대적 입력으로 취급한다 — 길이 먼저 검증.
 * ------------------------------------------------------------------------- */
bool log_decode(const uint8_t *in, size_t len, log_rec_t *out) {
    (void)in; (void)len; (void)out;
    // TODO: implement (len<8 거부, nargs>LOG_MAX_ARGS 거부, len != 8+4*nargs 거부)
    return false;  // placeholder
}

/* ---------------------------------------------------------------------------
 * Q6.  drain — 소비자 측 flush 경로 (UART/파일로 내보내기)
 *   KO: 링에서 최대 cap 바이트를 꺼내 out 에 담고 실제로 꺼낸 개수를 반환한다.
 *       비어 있으면 0. 이 함수는 저우선순위 태스크/스레드에서만 돈다.
 *   EN: Consumer side: pop up to cap bytes into out, return how many were popped.
 *   ex: 10바이트 들어있는 링에 cap=4 -> 4 반환, 6바이트는 링에 남는다
 * ------------------------------------------------------------------------- */
size_t log_drain(spsc_t *rb, uint8_t *out, size_t cap) {
    (void)rb; (void)out; (void)cap;
    // TODO: implement (spsc_pop 을 cap 번까지 반복, 실패하면 중단)
    return 0;  // placeholder
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL (건드리지 말 것)
// ===========================================================================
int main(void) {
    // -------- Q1 : SPSC 바이트 링 --------
    printf("== Q1. SPSC byte ring (init / push / pop) ==\n");
    uint8_t sto[8];
    spsc_t rb;
    bool init_ok = (spsc_init(&rb, sto, 6) == false)    // 6 은 2의 거듭제곱 아님
                && (spsc_init(&rb, sto, 8) == true);
    T("Q1 init: non-pow2(6) 거부 / pow2(8) 수용", init_ok);

    bool fill_ok = true;
    for (uint8_t i = 0; i < 8; ++i) fill_ok &= spsc_push(&rb, (uint8_t)(0xA0 + i));
    T("Q1 capacity == size : 8개 모두 push 성공 (free-running index)", fill_ok);
    T("Q1 push on full -> false (덮어쓰기 안 함)", spsc_push(&rb, 0xFF) == false);

    uint8_t v = 0;
    bool fifo_ok = true;
    for (uint8_t i = 0; i < 5; ++i)
        if (!spsc_pop(&rb, &v) || v != (uint8_t)(0xA0 + i)) fifo_ok = false;
    // 5칸 비운 자리에 5개 더 -> head/tail 이 size(8) 를 넘어 랩된다
    for (uint8_t i = 0; i < 5; ++i)
        if (!spsc_push(&rb, (uint8_t)(0xB0 + i))) fifo_ok = false;
    uint8_t exp1[8] = {0xA5, 0xA6, 0xA7, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4};
    for (int i = 0; i < 8; ++i)
        if (!spsc_pop(&rb, &v) || v != exp1[i]) fifo_ok = false;
    T("Q1 wrap-around 후에도 FIFO 순서 보존", fifo_ok);
    T("Q1 pop on empty -> false", spsc_pop(&rb, &v) == false);

    // -------- Q2 : bulk write (all-or-nothing) --------
    printf("== Q2. bulk write (all-or-nothing) + free space ==\n");
    uint8_t wsto[8];
    spsc_t wq;
    spsc_init(&wq, wsto, 8);
    const uint8_t msg3[3] = {1, 2, 3};
    bool fs_ok = (spsc_free_space(&wq) == 8);
    fs_ok &= (spsc_write(&wq, msg3, 3) == 3);
    fs_ok &= (spsc_free_space(&wq) == 5);
    T("Q2 free_space: 빈 링=8, 3바이트 쓴 뒤=5", fs_ok);

    bool content_ok = true;
    for (int i = 0; i < 3; ++i)
        if (!spsc_pop(&wq, &v) || v != (uint8_t)(i + 1)) content_ok = false;
    T("Q2 spsc_write 내용이 순서대로 저장됨", content_ok);

    const uint8_t six[6]  = {10, 11, 12, 13, 14, 15};
    const uint8_t five[5] = {20, 21, 22, 23, 24};
    spsc_write(&wq, six, 6);                       // free 8 -> 2
    uint32_t d0 = spsc_dropped(&wq);
    bool drop_ok = (spsc_write(&wq, five, 5) == 0); // 2칸에 5바이트 -> 전부 거부
    drop_ok &= (spsc_free_space(&wq) == 2);         // 한 바이트도 안 들어갔다
    drop_ok &= (spsc_dropped(&wq) == d0 + 1);
    T("Q2 drop-new: 0바이트 기록 + dropped 1 증가", drop_ok);

    // -------- Q3 : 레벨 필터 --------
    printf("== Q3. level filter (runtime + compile-time) ==\n");
    bool lv_ok = log_should_emit(LOG_ERROR, LOG_INFO)
              && log_should_emit(LOG_INFO,  LOG_INFO)
              && !log_should_emit(LOG_DEBUG, LOG_INFO);
    T("Q3 module threshold: level >= module_level 일 때만 emit", lv_ok);
    bool min_ok = !log_should_emit(LOG_TRACE, LOG_TRACE)   // 컴파일타임 하한에 걸림
                && log_should_emit(LOG_DEBUG, LOG_TRACE);
    T("Q3 LOG_LEVEL_MIN(DEBUG) 하한이 TRACE 를 차단", min_ok);

    // -------- Q4 : 레코드 인코딩 --------
    printf("== Q4. deferred-formatting record encode ==\n");
    uint8_t enc[32];
    const uint32_t args2[2] = {0xDEADBEEFu, 42u};
    size_t n4 = log_encode(enc, sizeof enc, 0x1234, LOG_WARN, 0x01020304u, args2, 2);
    bool enc_ok = (n4 == 16);
    enc_ok &= (enc[0] == 0x34 && enc[1] == 0x12);                       // fmt_id LE
    enc_ok &= (enc[2] == LOG_WARN && enc[3] == 2);                      // level, nargs
    enc_ok &= (enc[4] == 0x04 && enc[5] == 0x03 && enc[6] == 0x02 && enc[7] == 0x01);
    enc_ok &= (enc[8] == 0xEF && enc[9] == 0xBE && enc[10] == 0xAD && enc[11] == 0xDE);
    enc_ok &= (enc[12] == 42 && enc[13] == 0 && enc[14] == 0 && enc[15] == 0);
    T("Q4 encode: 8+4*nargs = 16바이트, little-endian 필드 배치", enc_ok);

    bool cap_ok = (log_encode(enc, 15, 0x1234, LOG_WARN, 1, args2, 2) == 0);
    cap_ok &= (log_encode(enc, sizeof enc, 0x1234, LOG_WARN, 1, args2, LOG_MAX_ARGS + 1) == 0);
    T("Q4 encode: cap 부족 / nargs 초과 -> 0", cap_ok);

    // -------- Q5 : 레코드 디코딩 --------
    printf("== Q5. record decode (length validation) ==\n");
    log_rec_t r;
    memset(&r, 0, sizeof r);
    bool dec_ok = log_decode(enc, n4, &r);
    dec_ok &= (r.fmt_id == 0x1234 && r.level == LOG_WARN && r.nargs == 2);
    dec_ok &= (r.ts_ms == 0x01020304u);
    dec_ok &= (r.args[0] == 0xDEADBEEFu && r.args[1] == 42u);
    T("Q5 decode roundtrip: 모든 필드 복원", dec_ok);

    bool trunc_ok = log_decode(enc, 16, &r);   // 정상 길이는 통과해야 한다
    trunc_ok &= !log_decode(enc, 15, &r);      // 1바이트 잘림 -> 거부
    trunc_ok &= !log_decode(enc,  7, &r);      // 헤더(8)보다 짧음 -> 거부
    trunc_ok &= !log_decode(enc, 20, &r);      // len != 8+4*nargs -> 거부
    T("Q5 decode: len != 8+4*nargs (truncated/초과) 거부", trunc_ok);

    // -------- Q6 : drain (flush 경로) --------
    printf("== Q6. drain to UART/file ==\n");
    uint8_t dsto[16];
    spsc_t dq;
    spsc_init(&dq, dsto, 16);
    uint8_t payload[10];
    for (int i = 0; i < 10; ++i) payload[i] = (uint8_t)(0x50 + i);
    spsc_write(&dq, payload, sizeof payload);

    uint8_t out[16];
    bool drain_ok = (log_drain(&dq, out, 4) == 4);          // cap 만큼만
    for (int i = 0; i < 4; ++i) if (out[i] != (uint8_t)(0x50 + i)) drain_ok = false;
    drain_ok &= (log_drain(&dq, out, sizeof out) == 6);     // 남은 6바이트
    for (int i = 0; i < 6; ++i) if (out[i] != (uint8_t)(0x54 + i)) drain_ok = false;
    T("Q6 drain: cap 만큼만 pop, 나머지는 링에 남음", drain_ok);
    T("Q6 drain on empty -> 0 (링은 완전히 비었다)",
      log_drain(&dq, out, sizeof out) == 0 && spsc_free_space(&dq) == 16);

    // -------- 결과 --------
    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
