// 07_modular_design.c  —  PRACTICE STUB (직접 채워넣기)
// 모듈화 · 테스트 가능한 설계 (Modular & Testable Embedded Design)  —  Q59~Q68
// ---------------------------------------------------------------------------
// 빌드/실행:  make prob N=07_modular_design
//   또는:     cc -std=c11 -Wall -Wextra -pthread 07_modular_design.c -o /tmp/vk_modular_design && /tmp/vk_modular_design
//
// 각 함수의 '// TODO' 를 구현하고 다시 실행 -> [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일/실행은 되며 대부분 FAIL 로 뜬다. 스레드가 없으므로
//  어떤 경우에도 멈추지 않는다 — 이것도 이 세트의 교훈이다.)
//
// 리크루터 메일의 네 번째 주제 "design principles for modular, testable embedded
// software"가 그대로 이 세트다. 파트 B(시스템 설계)에서 거의 확실히 나오는 질문:
// **"하드웨어 없이 이걸 어떻게 테스트하나?"**  답은 하나다 — 드라이버가 HAL을 직접
// 부르지 않게 하고(의존성 주입), 시간·I/O·로그를 전부 주입 가능한 인터페이스로 빼고,
// 정책(재시도·타임아웃·CRC·전이)은 순수 함수와 테이블로 분리한다.
//
// 이 세트 전체가 **하나의 예제**를 단계적으로 키운다: Verkada 공기질 센서(SV)의
// I2C 드라이버. 마지막 Q68은 "시작 → NACK 재시도 → 준비 → CRC 실패 → 재측정 →
// 성공 → 타임아웃"을 fake만으로 한 번에 재현한다. **스레드가 하나도 없다** —
// 이것도 교훈이다. 좋은 설계는 테스트에서 동시성을 없앤다.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

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
// 공용 타입 — 드라이버가 의존하는 "인터페이스"들 (Q59에서 설명)
// ---------------------------------------------------------------------------
// 핵심: 아래 세 구조체(I2cOps / ClockOps / LogOps)가 이 드라이버의 **전부**다.
// 드라이버 코드는 HAL_I2C_Mem_Read() 도, HAL_GetTick() 도, printf() 도 모른다.
// ===========================================================================
typedef enum { IO_OK = 0, IO_NACK = -1, IO_BUS_ERR = -2 } IoStatus;

// I2C 버스 인터페이스 — 제품에선 진짜 HAL, 테스트에선 fake 를 꽂는다.
typedef struct {
    IoStatus (*read)(void *ctx, uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);
    IoStatus (*write)(void *ctx, uint8_t addr, uint8_t reg, uint8_t val);
    void     *ctx;                 // 구현체의 상태(버스 핸들 / fake 구조체)
} I2cOps;

// 시계 인터페이스 — 시간도 의존성이다. 테스트가 시간을 '조종'한다.
typedef struct {
    uint32_t (*now_ms)(void *ctx);
    void     *ctx;
} ClockOps;

// 로그 싱크 인터페이스 — 테스트에서 로그 내용을 단정할 수 있게 한다.
typedef enum { LOG_DEBUG = 0, LOG_INFO = 1, LOG_WARN = 2, LOG_ERROR = 3 } LogLevel;
typedef struct {
    void (*write)(void *ctx, LogLevel lvl, const char *msg);
    void *ctx;
} LogOps;

// --- 센서 레지스터 맵 (가상의 공기질 센서, Sensirion 계열을 본떴다) ---------
#define REG_WHOAMI   0x00       // read 1B, 기대값 0x5A
#define REG_CMD      0x01       // write 1B
#define REG_STATUS   0x02       // read 1B, bit0 = data ready
#define REG_DATA     0x04       // read 3B = [hi, lo, crc8]
#define CMD_START    0x01
#define AQ_WHOAMI    0x5A
#define STATUS_RDY   0x01

// --- 에러는 값으로 반환한다 (Q63) -------------------------------------------
typedef enum {
    AQ_OK        = 0,
    AQ_EAGAIN    = 1,   // 일시적: 다시 부르면 된다 (아직 준비 안 됨 / NACK)
    AQ_ETIMEDOUT = 2,
    AQ_ECRC      = 3,
    AQ_EIO       = 4,   // 영구적 버스 오류
    AQ_ENODEV    = 5,
    AQ_ESTATE    = 6,
    AQ_EINVAL    = 7,
    AQ_ERR__COUNT = 8
} AqErr;

// --- 상태와 이벤트 (Q64의 전이 테이블이 쓴다) --------------------------------
typedef enum {
    AQ_S_IDLE = 0, AQ_S_MEASURING = 1, AQ_S_READY = 2, AQ_S_FAULT = 3,
    AQ_S__COUNT = 4,
    AQ_S_INVALID = 0xFF            // "그 전이는 없다"는 sentinel
} AqState;

typedef enum {
    AQ_E_START = 0,      // 측정 시작 커맨드 성공
    AQ_E_DATA_RDY = 1,   // STATUS 비트가 섰다
    AQ_E_DECODED = 2,    // 데이터 읽기 + CRC 통과
    AQ_E_RETRY = 3,      // 복구 가능한 실패 (CRC 등) → 재측정
    AQ_E_FAIL = 4,       // 복구 불가 (타임아웃 / 버스 오류)
    AQ_E_RECOVER = 5,    // 상위 레이어의 명시적 복구
    AQ_E__COUNT = 6
} AqEvent;

// --- 고정 크기 링 로그 (Q66) -------------------------------------------------
#define RLOG_CAP  8                 // 슬롯 수 (malloc 없음)
#define RLOG_MSG  48                // 한 줄 최대 길이(NUL 포함)
typedef struct {
    char     msg[RLOG_CAP][RLOG_MSG];
    LogLevel lvl[RLOG_CAP];
    size_t   head;                  // 다음에 쓸 슬롯
    size_t   count;                 // 채워진 개수 (RLOG_CAP 에서 포화)
    size_t   dropped;               // 덮어써서 사라진 줄 수
} RingLog;

// --- 설정 (Q67) --------------------------------------------------------------
typedef struct {
    uint8_t  i2c_addr;
    uint32_t period_ms;             // 샘플링 주기
    uint32_t timeout_ms;            // 한 측정의 최대 대기
    unsigned max_retries;
} AqConfig;

typedef enum {
    CFG_OK = 0, CFG_E_NULL, CFG_E_ADDR, CFG_E_PERIOD, CFG_E_TIMEOUT, CFG_E_RETRIES
} CfgErr;

// --- 드라이버 본체 -----------------------------------------------------------
typedef struct {
    I2cOps   i2c;                   // 주입된 버스
    ClockOps clk;                   // 주입된 시계
    LogOps   log;                   // 주입된 로그 싱크 (NULL 이면 no-op)
    uint8_t  addr;
    uint32_t timeout_ms;
    unsigned max_attempts;
    AqState  state;
    uint32_t op_start_ms;
    IoStatus last_io;               // 마지막 I/O 결과 (재시도 정책 입력)
    unsigned attempt;               // 현재 연속 시도 횟수
    unsigned retries;               // 통계: 누적 재시도
    unsigned crc_errors;
    unsigned io_errors;
    unsigned bad_transitions;       // 테이블이 거부한 전이 수
} AqDriver;

// ===========================================================================
// 프로토타입 — 헤더 하나에 들어갈 "공개 API". 구현 순서와 무관하게 쓸 수 있다.
// ===========================================================================
void      aq_init(AqDriver *d, I2cOps i2c, ClockOps clk, uint8_t addr, uint32_t timeout_ms); // Q59
AqErr     aq_probe(AqDriver *d);                                                             // Q59
AqErr     aq_start_measure(AqDriver *d);                                                     // Q60
bool      deadline_expired(uint32_t start_ms, uint32_t now_ms, uint32_t timeout_ms);         // Q61
AqErr     aq_wait_ready(AqDriver *d);                                                        // Q61
bool      should_retry(IoStatus st, unsigned attempt, unsigned max_attempts);                // Q62
uint32_t  next_delay_ms(unsigned attempt, uint32_t base_ms, uint32_t cap_ms);                // Q62
AqErr     io_to_err(IoStatus st);                                                            // Q63
const char *aq_strerror(AqErr e);                                                            // Q63
AqErr     aq_read_raw(AqDriver *d, uint8_t *raw3);                                           // Q63
AqState   aq_next_state(AqState s, AqEvent e);                                               // Q64
bool      aq_fsm_apply(AqDriver *d, AqEvent e);                                              // Q64
uint8_t   crc8(const uint8_t *data, size_t n);                                               // Q65
AqErr     aq_decode(const uint8_t *raw3, int16_t *out_ppb);                                  // Q65
void      rlog_init(RingLog *rl);                                                            // Q66
void      rlog_push(RingLog *rl, LogLevel lvl, const char *msg);                             // Q66
size_t    rlog_count(const RingLog *rl);                                                     // Q66
const char *rlog_at(const RingLog *rl, size_t i);                                            // Q66
void      aq_set_logger(AqDriver *d, LogOps log);                                            // Q66
void      aq_log(AqDriver *d, LogLevel lvl, const char *msg);                                // Q66
CfgErr    aq_config_validate(const AqConfig *c);                                             // Q67
AqErr     aq_service(AqDriver *d, int16_t *out_ppb);                                         // Q68
void      aq_recover(AqDriver *d);                                                           // Q68

// ===========================================================================
// 제품용(real) HAL 어댑터 — 여기가 유일하게 하드웨어를 아는 곳.
// ---------------------------------------------------------------------------
// 단위 테스트는 이 함수를 **절대** 부르지 않는다. g_real_hal_calls 가 0인지
// 단정하는 것만으로 "드라이버가 HAL을 직접 부르지 않는다"를 기계적으로 증명한다.
// (제품 빌드에서는 aq_hal_i2c_ops() 가 진짜 버스를 물고 온다.)
// ===========================================================================
static unsigned g_real_hal_calls = 0;

static IoStatus real_hal_read(void *ctx, uint8_t addr, uint8_t reg, uint8_t *buf, size_t n) {
    (void)ctx; (void)addr; (void)reg; (void)buf; (void)n;
    g_real_hal_calls++;                 // 실제로는 HAL_I2C_Mem_Read(...)
    return IO_BUS_ERR;
}
static IoStatus real_hal_write(void *ctx, uint8_t addr, uint8_t reg, uint8_t val) {
    (void)ctx; (void)addr; (void)reg; (void)val;
    g_real_hal_calls++;                 // 실제로는 HAL_I2C_Mem_Write(...)
    return IO_BUS_ERR;
}
static I2cOps aq_hal_i2c_ops(void) {
    I2cOps ops = { real_hal_read, real_hal_write, NULL };
    return ops;
}

// ===========================================================================
// Q59. ops 구조체(인터페이스) 정의와 주입
// ---------------------------------------------------------------------------
// 드라이버는 함수 포인터 구조체(I2cOps / ClockOps)만 들고 있고, 그 구현이
// 진짜 HAL인지 fake인지 모른다. C의 의존성 주입 = "vtable을 값으로 넘기기".
// aq_init 은 ops를 복사해 보관하고(포인터 수명 문제 회피) 상태를 IDLE로 리셋한다.
// aq_probe 는 주입된 read 로 WHOAMI 를 확인한다 — 첫 번째 "HAL 없는" 동작.
// ===========================================================================
/* ---------------------------------------------------------------------------
 * Q59.  ops 구조체 주입 — 드라이버 초기화
 *   KO: d 를 0 으로 밀고 주입받은 i2c/clk 를 '값으로' 복사해 보관한다.
 *       addr, timeout_ms 를 저장하고 max_attempts=3, state=AQ_S_IDLE,
 *       last_io=IO_OK 로 리셋. d 가 NULL 이면 아무것도 하지 않는다.
 *   EN: Store the injected ops by value, reset counters and set state to IDLE.
 *   ex: aq_init(&d, io, ck, 0x62, 1000) -> state==AQ_S_IDLE, retries==0
 * ------------------------------------------------------------------------- */
void aq_init(AqDriver *d, I2cOps i2c, ClockOps clk, uint8_t addr, uint32_t timeout_ms) {
    (void)d; (void)i2c; (void)clk; (void)addr; (void)timeout_ms;
    // TODO: implement
}

/* ---------------------------------------------------------------------------
 * Q59.  주입된 read 로 WHOAMI 확인
 *   KO: d->i2c.read 로 REG_WHOAMI 를 1바이트 읽어 AQ_WHOAMI(0x5A) 인지 본다.
 *       I/O 실패 → io_errors++ 후 AQ_EIO, ID 불일치 → AQ_ENODEV, 일치 → AQ_OK.
 *       d 나 d->i2c.read 가 NULL 이면 AQ_EINVAL (주입 안 된 ops 방어).
 *   EN: Read WHOAMI through the injected ops; AQ_OK / AQ_ENODEV / AQ_EIO.
 *   ex: fake.whoami=0x5A -> AQ_OK,  0x11 -> AQ_ENODEV
 * ------------------------------------------------------------------------- */
AqErr aq_probe(AqDriver *d) {
    (void)d;
    // TODO: implement
    return AQ_EIO;   // placeholder
}

// ===========================================================================
// Q60. fake I2C 로 정상 경로 테스트 (호출 횟수까지 단정)
// ---------------------------------------------------------------------------
// 측정 시작 = REG_CMD 에 CMD_START 쓰기 + 주입된 시계로 시작 시각 기록.
// "무엇을 반환했나"만 보지 말고 **어떤 트랜잭션이 몇 번 나갔는지**를 단정해야
// 진짜 회귀 테스트다 (레지스터를 잘못 쓰면 반환값은 그대로 OK 일 수 있다).
// 참고: 여기서는 전이를 직접 한다. Q64에서 같은 전이를 테이블로 일반화하고,
//       Q68의 aq_service 는 테이블(aq_fsm_apply)을 쓴다.
// ===========================================================================
/* ---------------------------------------------------------------------------
 * Q60.  측정 시작 (정상 경로 + 호출 횟수)
 *   KO: state 가 AQ_S_IDLE 이 아니면 I/O 도 내지 말고 AQ_ESTATE.
 *       REG_CMD 에 CMD_START 를 쓰고, 실패면 last_io 기록 후 io_to_err(st) 반환
 *       (버스 오류면 io_errors++). 성공이면 주입된 시계로 op_start_ms 를 찍고
 *       attempt=0, state=AQ_S_MEASURING 으로 바꾼 뒤 AQ_OK.
 *   EN: Write CMD_START to REG_CMD, stamp op_start_ms from the injected clock,
 *       move to MEASURING. Refuse (AQ_ESTATE) unless currently IDLE.
 *   ex: clock=4321 -> AQ_OK, writes==1, last_reg==REG_CMD, op_start_ms==4321
 * ------------------------------------------------------------------------- */
AqErr aq_start_measure(AqDriver *d) {
    (void)d;
    // TODO: implement
    return AQ_EIO;   // placeholder
}

// ===========================================================================
// Q61. 시계 주입 — 타임아웃을 실제 대기 없이 테스트
// ---------------------------------------------------------------------------
// deadline_expired 는 순수 함수다. unsigned 뺄셈이라 32비트 tick 랩어라운드에도
// 정확하다(now - start 는 mod 2^32 경과시간). `now >= start + timeout` 으로 쓰면
// 랩 근처에서 영원히 만료되지 않는 고전적 버그가 난다.
// aq_wait_ready 는 STATUS 를 한 번 폴링한다 — 블로킹도, sleep 도 없다.
// ===========================================================================
/* ---------------------------------------------------------------------------
 * Q61.  데드라인 판정 (랩어라운드 안전, 순수 함수)
 *   KO: 경과시간이 timeout_ms 이상이면 true. 32비트 tick 이 랩어라운드해도
 *       정확해야 한다 — unsigned 뺄셈(now - start)을 쓰고, 절대
 *       `now >= start + timeout` 로 쓰지 말 것.
 *   EN: Return true when (now - start) >= timeout, using unsigned wrap-safe math.
 *   ex: deadline_expired(0xFFFFFF00, 0x50, 300) -> true (336ms 경과)
 * ------------------------------------------------------------------------- */
bool deadline_expired(uint32_t start_ms, uint32_t now_ms, uint32_t timeout_ms) {
    (void)start_ms; (void)now_ms; (void)timeout_ms;
    // TODO: implement
    return false;    // placeholder
}

/* ---------------------------------------------------------------------------
 * Q61.  준비 상태 1회 폴링 (블로킹·sleep 금지)
 *   KO: state 가 MEASURING 이 아니면 AQ_ESTATE. REG_STATUS 를 1바이트 읽어:
 *       버스 오류 → io_errors++ 후 AQ_EIO / RDY 비트가 서면 AQ_OK /
 *       그 외(NACK·미준비)는 주입된 시계로 deadline_expired 를 확인해
 *       만료면 AQ_ETIMEDOUT, 아니면 AQ_EAGAIN.
 *   EN: Poll STATUS once; EAGAIN while pending, ETIMEDOUT past the deadline.
 *   ex: not_ready + clock 을 timeout 만큼 전진 -> AQ_ETIMEDOUT (실제 대기 0초)
 * ------------------------------------------------------------------------- */
AqErr aq_wait_ready(AqDriver *d) {
    (void)d;
    // TODO: implement
    return AQ_EIO;   // placeholder
}

// ===========================================================================
// Q62. 재시도/백오프 정책을 순수 함수로 분리
// ---------------------------------------------------------------------------
// 정책을 I/O 에서 떼어내면 버스도 시계도 없이 표로 단위 테스트할 수 있다.
//  - should_retry : 일시적 실패(NACK)만, 예산(max_attempts) 안에서만 재시도
//  - next_delay_ms: 지수 백오프 + 상한. 오버플로/무한증가를 막는다.
// ===========================================================================
/* ---------------------------------------------------------------------------
 * Q62.  재시도 정책 (순수 함수)
 *   KO: IO_NACK(일시적 실패)일 때만, 그리고 attempt < max_attempts 일 때만 true.
 *       IO_OK 는 재시도 대상이 아니고 IO_BUS_ERR 는 영구 오류라 재시도하지 않는다.
 *   EN: Retry only transient NACKs, and only while within the attempt budget.
 *   ex: should_retry(IO_NACK,1,3) -> true;  should_retry(IO_BUS_ERR,0,3) -> false
 * ------------------------------------------------------------------------- */
bool should_retry(IoStatus st, unsigned attempt, unsigned max_attempts) {
    (void)st; (void)attempt; (void)max_attempts;
    // TODO: implement
    return false;    // placeholder
}

/* ---------------------------------------------------------------------------
 * Q62.  지수 백오프 지연 (순수 함수, 오버플로 금지)
 *   KO: attempt 회차의 대기시간 = base_ms * 2^attempt, 단 cap_ms 에서 포화.
 *       base_ms 가 0 이면 0. attempt 가 커도 uint32 오버플로가 나면 안 된다.
 *   EN: Exponential backoff capped at cap_ms, overflow-safe.
 *   ex: base=10, cap=200: 10, 20, 40, 80, 160, 200, 200 ...
 * ------------------------------------------------------------------------- */
uint32_t next_delay_ms(unsigned attempt, uint32_t base_ms, uint32_t cap_ms) {
    (void)attempt; (void)base_ms; (void)cap_ms;
    // TODO: implement
    return 0;        // placeholder
}

// ===========================================================================
// Q63. 에러 전파 설계 — 에러는 값, 로그는 부가물, 삼키지 않는다
// ---------------------------------------------------------------------------
// 드라이버는 assert() 로 죽지도, 에러를 로그만 찍고 AQ_OK 로 뭉개지도 않는다.
// 정책(재시도할지, 센서를 끌지, 클라우드에 알릴지)은 **호출자**가 정한다.
// aq_strerror 는 디버깅·로그·Command 이벤트에 쓰는 코드→문자열 매핑.
// ===========================================================================
/* ---------------------------------------------------------------------------
 * Q63.  버스 상태 → 드라이버 에러 코드
 *   KO: IO_OK→AQ_OK, IO_NACK→AQ_EAGAIN(일시적, 재시도 가능),
 *       IO_BUS_ERR→AQ_EIO(영구). 일시적/영구를 같은 코드로 뭉개면
 *       상위 레이어가 재시도할지 포기할지 결정할 수 없다.
 *   EN: Map bus status to driver error, keeping transient and permanent distinct.
 *   ex: io_to_err(IO_NACK) -> AQ_EAGAIN
 * ------------------------------------------------------------------------- */
AqErr io_to_err(IoStatus st) {
    (void)st;
    // TODO: implement
    return AQ_EIO;   // placeholder
}

/* ---------------------------------------------------------------------------
 * Q63.  에러 코드 → 문자열
 *   KO: 모든 AqErr 값에 사람이 읽을 수 있는 문자열을 돌려준다.
 *       정의되지 않은 값은 "UNKNOWN". 반환 문자열은 정적 저장소여야 한다
 *       (지역 배열 주소를 돌려주면 안 된다).
 *   EN: Return a static human-readable string for each error code.
 *   ex: aq_strerror(AQ_ECRC) -> "ECRC: checksum mismatch"
 * ------------------------------------------------------------------------- */
const char *aq_strerror(AqErr e) {
    (void)e;
    // TODO: implement
    return "UNKNOWN";   // placeholder
}

/* ---------------------------------------------------------------------------
 * Q63.  데이터 3바이트 읽기 — 에러를 삼키지 않기
 *   KO: REG_DATA 에서 3바이트를 읽는다. 실패하면 io_errors++ 로 통계는 남기되
 *       에러는 io_to_err(st) 로 그대로 호출자에게 올린다(로그만 찍고 AQ_OK 반환 금지).
 *       실패 시 raw3 버퍼는 건드리지 않는다. NULL 인자는 AQ_EINVAL.
 *   EN: Read 3 data bytes; propagate failures as values, never swallow them.
 *   ex: bus_error -> AQ_EIO, raw3 unchanged, io_errors==1
 * ------------------------------------------------------------------------- */
AqErr aq_read_raw(AqDriver *d, uint8_t *raw3) {
    (void)d; (void)raw3;
    // TODO: implement
    return AQ_EIO;   // placeholder
}

// ===========================================================================
// Q64. 전이 테이블 기반 상태머신
// ---------------------------------------------------------------------------
// switch 중첩 대신 [상태][이벤트] 테이블 하나로 전이를 **선언**한다. 장점:
//  - 전체 전이를 한 화면에서 리뷰할 수 있다 (인터뷰에서 그려보이기 좋다)
//  - 빠진 칸 = 명시적으로 금지된 전이 → 잘못된 순서 호출을 조용히 삼키지 않는다
//  - 상태/이벤트 추가가 코드가 아니라 데이터 수정이 된다
// ===========================================================================
/* ---------------------------------------------------------------------------
 * Q64.  전이 테이블 조회
 *   KO: static const 2차원 테이블 TBL[AQ_S__COUNT][AQ_E__COUNT] 로 전이를 선언하고
 *       조회만 한다. 금지된 칸은 AQ_S_INVALID. s 나 e 가 범위를 벗어나면
 *       AQ_S_INVALID (테이블 밖 접근 방어).
 *       표: IDLE+START→MEASURING, MEASURING+DATA_RDY→READY, READY+DECODED→IDLE,
 *           *+RETRY→IDLE(FAULT 제외), *+FAIL→FAULT, FAULT+RECOVER→IDLE
 *   EN: Look up [state][event] in a static transition table; AQ_S_INVALID if
 *       the transition (or the index) is not allowed.
 *   ex: aq_next_state(AQ_S_FAULT, AQ_E_START) -> AQ_S_INVALID
 * ------------------------------------------------------------------------- */
AqState aq_next_state(AqState s, AqEvent e) {
    (void)s; (void)e;
    // TODO: implement  (힌트: static const uint8_t TBL[AQ_S__COUNT][AQ_E__COUNT])
    return AQ_S_INVALID;   // placeholder
}

/* ---------------------------------------------------------------------------
 * Q64.  전이 적용 — 잘못된 전이 방어
 *   KO: aq_next_state 로 다음 상태를 구한다. AQ_S_INVALID 면 상태를 바꾸지 않고
 *       bad_transitions++ 후 false. 유효하면 d->state 를 갱신하고 true.
 *   EN: Apply an event: reject illegal transitions without changing state.
 *   ex: MEASURING 에서 다시 START -> false, state 는 MEASURING 그대로
 * ------------------------------------------------------------------------- */
bool aq_fsm_apply(AqDriver *d, AqEvent e) {
    (void)d; (void)e;
    // TODO: implement
    return false;    // placeholder
}

// ===========================================================================
// Q65. CRC 검증 — 오염된 데이터가 출력 파라미터로 절대 새어나가지 않게
// ---------------------------------------------------------------------------
// crc8: Sensirion 계열 CRC-8 (poly 0x31, init 0xFF). 순수 함수 → 단독 테스트.
// aq_decode: **검증을 먼저** 하고, 실패하면 *out_ppb 를 건드리지 않고 반환한다.
// (먼저 쓰고 나중에 검사하면, 호출자가 반환값을 무시했을 때 쓰레기값이 남는다.)
// ===========================================================================
/* ---------------------------------------------------------------------------
 * Q65.  CRC-8 (poly 0x31, init 0xFF) — 순수 함수
 *   KO: Sensirion 계열 CRC-8: crc=0xFF 로 시작, 바이트마다 XOR 후 8번 시프트,
 *       MSB 가 서 있으면 <<1 후 0x31 XOR, 아니면 <<1.
 *   EN: CRC-8 with polynomial 0x31 and init 0xFF.
 *   ex: crc8({0xBE,0xEF}, 2) -> 0x92
 * ------------------------------------------------------------------------- */
uint8_t crc8(const uint8_t *data, size_t n) {
    (void)data; (void)n;
    // TODO: implement
    return 0;        // placeholder
}

/* ---------------------------------------------------------------------------
 * Q65.  프레임 디코드 — 오염 데이터 유출 차단
 *   KO: raw3 = [hi, lo, crc]. **먼저** crc8(raw3,2) 를 검증하고 불일치면
 *       *out_ppb 를 건드리지 않은 채 AQ_ECRC 를 반환한다(실패 시 out 미변경이 핵심).
 *       통과하면 big-endian 으로 조립해 int16_t 로 저장(음수 값도 정확히). NULL → AQ_EINVAL.
 *   EN: Verify the CRC first; on mismatch return AQ_ECRC and leave *out untouched.
 *   ex: {0x02,0xE6,crc} -> 742;  crc 한 비트 뒤집기 -> AQ_ECRC, out 그대로
 * ------------------------------------------------------------------------- */
AqErr aq_decode(const uint8_t *raw3, int16_t *out_ppb) {
    (void)raw3; (void)out_ppb;
    // TODO: implement
    return AQ_ECRC;  // placeholder
}

// ===========================================================================
// Q66. 고정 크기 링 로그 + 로거 인터페이스 주입
// ---------------------------------------------------------------------------
// malloc 없는 고정 슬롯 링. 꽉 차면 가장 오래된 줄을 덮어쓰고 dropped 를 센다
// (로그가 메모리를 먹고 장치를 죽이는 일은 없어야 한다).
// 드라이버는 printf 가 아니라 주입된 LogOps 로만 말한다 → 테스트가 로그를 단정.
// ===========================================================================
/* ---------------------------------------------------------------------------
 * Q66.  링 로그 초기화
 *   KO: rl 전체를 0 으로 민다(head/count/dropped=0). NULL 이면 no-op.
 *   EN: Zero the ring log.
 *   ex: rlog_init(&rl) -> rlog_count(&rl)==0
 * ------------------------------------------------------------------------- */
void rlog_init(RingLog *rl) {
    (void)rl;
    // TODO: implement
}

/* ---------------------------------------------------------------------------
 * Q66.  링 로그 한 줄 기록 (malloc 없음)
 *   KO: head 슬롯에 msg 를 최대 RLOG_MSG-1 자까지 복사하고 반드시 NUL 종료
 *       (snprintf 가 가장 안전). lvl 저장, head 전진(모듈러).
 *       가득 차 있으면 가장 오래된 줄을 덮어쓰고 dropped++, 아니면 count++.
 *       msg 가 NULL 이면 "(null)" 로 대체 — 크래시 금지.
 *   EN: Append one line; overwrite the oldest when full and count the drop.
 *   ex: CAP=8 에 10줄 -> count==8, dropped==2, at(0)=="m2"
 * ------------------------------------------------------------------------- */
void rlog_push(RingLog *rl, LogLevel lvl, const char *msg) {
    (void)rl; (void)lvl; (void)msg;
    // TODO: implement
}

/* ---------------------------------------------------------------------------
 * Q66.  링 로그에 들어 있는 줄 수
 *   KO: 현재 보관 중인 줄 수(용량에서 포화). NULL 이면 0.
 *   EN: Number of lines currently held.
 *   ex: 3줄 push -> 3
 * ------------------------------------------------------------------------- */
size_t rlog_count(const RingLog *rl) {
    (void)rl;
    // TODO: implement
    return 0;        // placeholder
}

// i = 0 이 가장 오래된 줄. 범위를 벗어나면 NULL.
/* ---------------------------------------------------------------------------
 * Q66.  오래된 순서로 i 번째 줄
 *   KO: i=0 이 가장 오래된 줄. i >= count 거나 rl 이 NULL 이면 NULL.
 *       가장 오래된 슬롯 = (head + RLOG_CAP - count) % RLOG_CAP.
 *   EN: Return the i-th oldest line, or NULL when out of range.
 *   ex: push a,b,c -> at(0)=="a", at(2)=="c", at(3)==NULL
 * ------------------------------------------------------------------------- */
const char *rlog_at(const RingLog *rl, size_t i) {
    (void)rl; (void)i;
    // TODO: implement
    return NULL;     // placeholder
}

/* ---------------------------------------------------------------------------
 * Q66.  로거 주입
 *   KO: LogOps 를 값으로 저장한다. 로그도 의존성 — 드라이버는 printf 를 모른다.
 *   EN: Inject the log sink (stored by value).
 *   ex: aq_set_logger(&d, (LogOps){rlog_sink, &ring})
 * ------------------------------------------------------------------------- */
void aq_set_logger(AqDriver *d, LogOps log) {
    (void)d; (void)log;
    // TODO: implement
}

/* ---------------------------------------------------------------------------
 * Q66.  주입된 싱크로 로그 보내기 (미주입이면 no-op)
 *   KO: d->log.write 가 NULL 이면 조용히 아무것도 하지 않는다(크래시 금지).
 *       그 외에는 d->log.write(d->log.ctx, lvl, msg) 로 넘긴다.
 *   EN: Forward to the injected sink; silently no-op when none is installed.
 *   ex: 로거 없이 aq_log(...) -> 크래시 없이 무시
 * ------------------------------------------------------------------------- */
void aq_log(AqDriver *d, LogLevel lvl, const char *msg) {
    (void)d; (void)lvl; (void)msg;
    // TODO: implement
}

// ===========================================================================
// Q67. 설정 검증 순수 함수 — 경계값 거부 + 이유 코드
// ---------------------------------------------------------------------------
// 설정은 클라우드(Command)에서 내려온다. "주기 0", "타임아웃이 주기보다 김",
// "예약된 I2C 주소" 같은 값이 런타임에 들어오면 장치가 조용히 미쳐버린다.
// 검증은 I/O 가 없는 순수 함수 → 경계값 표를 그대로 테스트로 옮길 수 있다.
// ===========================================================================
/* ---------------------------------------------------------------------------
 * Q67.  설정 검증 — 경계값 거부 + 이유 코드
 *   KO: 순서대로 검사하고 첫 위반의 이유 코드를 반환한다:
 *       NULL → CFG_E_NULL / i2c_addr 이 0x08~0x77 밖(I2C 예약 구간) → CFG_E_ADDR /
 *       period_ms 가 100 미만이거나 3,600,000 초과 → CFG_E_PERIOD /
 *       timeout_ms 가 0 이거나 period_ms 초과 → CFG_E_TIMEOUT /
 *       max_retries > 10 → CFG_E_RETRIES. 모두 통과하면 CFG_OK.
 *   EN: Validate a config and return the first violated rule as a reason code.
 *   ex: {0x62, period=1000, timeout=1001, 3} -> CFG_E_TIMEOUT
 * ------------------------------------------------------------------------- */
CfgErr aq_config_validate(const AqConfig *c) {
    (void)c;
    // TODO: implement
    return CFG_E_NULL;   // placeholder
}

// ===========================================================================
// Q68. 통합 시나리오 — 논블로킹 서비스 루프 한 틱
// ---------------------------------------------------------------------------
// 상위 루프가 주기적으로 부르는 단 하나의 함수. 한 번 호출 = I2C 트랜잭션 1회
// (블로킹 없음 → 다른 일을 하는 메인 루프/이벤트 루프에 그대로 얹을 수 있다).
// 반환: AQ_OK(값 나옴) / AQ_EAGAIN(진행 중) / 그 외 에러.
// ===========================================================================
/* ---------------------------------------------------------------------------
 * Q68.  논블로킹 서비스 한 틱 (통합)
 *   KO: state 에 따라 한 번의 I2C 트랜잭션만 수행한다.
 *       IDLE: aq_start_measure. 성공이면 AQ_EAGAIN. 실패면 attempt++ 후
 *         should_retry 면 retries++ 하고 AQ_EAGAIN(상태 유지), 아니면 로그 남기고
 *         AQ_E_FAIL 전이 후 에러 반환.
 *       MEASURING: aq_wait_ready. EAGAIN 그대로 / OK 면 AQ_E_DATA_RDY 전이 후 AQ_EAGAIN /
 *         그 외는 로그 + AQ_E_FAIL 전이 후 에러.
 *       READY: aq_read_raw + aq_decode. CRC 실패면 crc_errors++, 경고 로그,
 *         AQ_E_RETRY 전이(→IDLE, 재측정) 후 AQ_ECRC. 성공이면 AQ_E_DECODED 전이 후 AQ_OK.
 *       FAULT: 무조건 AQ_ESTATE (명시적 복구 전까지 고착). NULL 인자는 AQ_EINVAL.
 *   EN: One non-blocking tick of the driver: at most one I2C transaction per call.
 *       Returns AQ_OK with a value, AQ_EAGAIN while in progress, or an error.
 *   ex: start→NACK 재시도→준비→CRC 실패→재측정→성공→타임아웃 전체 흐름
 * ------------------------------------------------------------------------- */
AqErr aq_service(AqDriver *d, int16_t *out_ppb) {
    (void)d; (void)out_ppb;
    // TODO: implement
    return AQ_ESTATE;    // placeholder
}

/* ---------------------------------------------------------------------------
 * Q68.  FAULT 에서 명시적 복구
 *   KO: AQ_E_RECOVER 를 전이시키고, 성공했을 때만 attempt=0, last_io=IO_OK 로
 *       리셋하고 INFO 로그를 남긴다. FAULT 가 아니면 아무 일도 일어나지 않아야 한다.
 *   EN: Explicit recovery from FAULT back to IDLE.
 *   ex: FAULT 상태에서 aq_recover -> state==IDLE, 다시 측정 가능
 * ------------------------------------------------------------------------- */
void aq_recover(AqDriver *d) {
    (void)d;
    // TODO: implement
}

// ===========================================================================
// 테스트용 fake 들 — 하드웨어 0개. (그대로 주어진다. 건드리지 말 것)
// ===========================================================================
typedef struct {
    int      nack_writes;      // 남은 쓰기 NACK 횟수
    int      nack_reads;       // 남은 읽기 NACK 횟수
    int      not_ready_times;  // 남은 "아직 준비 안 됨" 횟수
    bool     bus_error;        // 모든 접근을 IO_BUS_ERR 로
    bool     corrupt_crc;      // 데이터의 CRC 바이트를 망가뜨린다
    uint8_t  whoami;
    int16_t  value;            // 센서가 보고할 값 (ppb)
    unsigned reads, writes;    // 호출 횟수 — 테스트가 이것까지 단정한다
    uint8_t  last_addr, last_reg, last_val;
} FakeI2c;

static IoStatus fake_read(void *ctx, uint8_t addr, uint8_t reg, uint8_t *buf, size_t n) {
    FakeI2c *f = (FakeI2c *)ctx;
    f->reads++;
    f->last_addr = addr;
    f->last_reg  = reg;
    if (f->bus_error) return IO_BUS_ERR;
    if (f->nack_reads > 0) { f->nack_reads--; return IO_NACK; }

    if (reg == REG_WHOAMI && n == 1) { buf[0] = f->whoami; return IO_OK; }
    if (reg == REG_STATUS && n == 1) {
        if (f->not_ready_times > 0) { f->not_ready_times--; buf[0] = 0x00; }
        else                        { buf[0] = STATUS_RDY; }
        return IO_OK;
    }
    if (reg == REG_DATA && n == 3) {
        uint16_t u = (uint16_t)f->value;
        buf[0] = (uint8_t)(u >> 8);
        buf[1] = (uint8_t)(u & 0xFF);
        buf[2] = crc8(buf, 2);
        if (f->corrupt_crc) buf[2] = (uint8_t)(buf[2] ^ 0xFF);
        return IO_OK;
    }
    return IO_BUS_ERR;                    // 모르는 레지스터 = 테스트 버그
}

static IoStatus fake_write(void *ctx, uint8_t addr, uint8_t reg, uint8_t val) {
    FakeI2c *f = (FakeI2c *)ctx;
    f->writes++;
    f->last_addr = addr;
    f->last_reg  = reg;
    f->last_val  = val;
    if (f->bus_error) return IO_BUS_ERR;
    if (f->nack_writes > 0) { f->nack_writes--; return IO_NACK; }
    return IO_OK;
}

typedef struct { uint32_t ms; } FakeClock;          // 테스트가 시간을 조종한다
static uint32_t fake_now(void *ctx) { return ((FakeClock *)ctx)->ms; }

static void rlog_sink(void *ctx, LogLevel lvl, const char *msg) {
    (void)lvl;
    rlog_push((RingLog *)ctx, lvl, msg);
}

static void setup(AqDriver *d, FakeI2c *f, FakeClock *c) {
    I2cOps   io = { fake_read, fake_write, f };
    ClockOps ck = { fake_now, c };
    aq_init(d, io, ck, 0x62, 1000);
}

// rlog_at() 이 NULL 을 돌려줘도(미구현 stub) 크래시하지 않는 비교
static bool str_eq(const char *a, const char *b) { return a && b && strcmp(a, b) == 0; }

static bool log_has(const RingLog *rl, const char *needle) {
    for (size_t i = 0; i < rlog_count(rl); i++) {
        const char *s = rlog_at(rl, i);
        if (s && strstr(s, needle)) return true;
    }
    return false;
}

// ===========================================================================
// main — 문제별 테스트 (건드리지 말 것)
// ===========================================================================
int main(void) {
    clock_t t_start = clock();

    // ----------------------------------------------------------------- Q59
    printf("== Q59. ops 구조체(인터페이스) 정의와 주입 ==\n");
    {
        AqDriver d = {0}; FakeI2c f = { .whoami = AQ_WHOAMI }; FakeClock c = { 0 };
        setup(&d, &f, &c);
        T("aq_init: 상태는 IDLE, 통계는 0", d.state == AQ_S_IDLE && d.retries == 0 && d.io_errors == 0);
        T("aq_init: ops 를 값으로 보관한다", d.i2c.ctx == &f && d.clk.ctx == &c);
        T("aq_probe: 주입된 read 로 WHOAMI 확인", aq_probe(&d) == AQ_OK);
        T("aq_probe: 올바른 주소/레지스터로 읽기 1회만 발생",
          f.last_addr == 0x62 && f.last_reg == REG_WHOAMI && f.reads == 1 && f.writes == 0);

        FakeI2c bad = { .whoami = 0x11 }; FakeClock c2 = { 0 };
        AqDriver d2 = {0}; setup(&d2, &bad, &c2);
        T("다른 칩 ID → AQ_ENODEV", aq_probe(&d2) == AQ_ENODEV);

        FakeI2c dead = { .whoami = AQ_WHOAMI, .bus_error = true }; FakeClock c3 = { 0 };
        AqDriver d3 = {0}; setup(&d3, &dead, &c3);
        T("버스 오류 → AQ_EIO + io_errors 증가", aq_probe(&d3) == AQ_EIO && d3.io_errors == 1);

        I2cOps prod = aq_hal_i2c_ops();   // 제품 빌드가 쓰는 ops (여기선 꽂지 않는다)
        (void)prod;
        T("단위 테스트는 실제 HAL 을 한 번도 부르지 않는다", g_real_hal_calls == 0);
    }

    // ----------------------------------------------------------------- Q60
    printf("\n== Q60. fake I2C 로 정상 경로 테스트 (호출 횟수까지) ==\n");
    {
        AqDriver d = {0}; FakeI2c f = { .whoami = AQ_WHOAMI }; FakeClock c = { .ms = 4321 };
        setup(&d, &f, &c);
        T("aq_start_measure 성공", aq_start_measure(&d) == AQ_OK);
        T("쓰기 정확히 1회, 읽기 0회", f.writes == 1 && f.reads == 0);
        T("REG_CMD 에 CMD_START 를 썼다", f.last_reg == REG_CMD && f.last_val == CMD_START);
        T("시작 시각은 주입된 시계에서 온다", d.op_start_ms == 4321);
        T("상태가 MEASURING 으로 전이", d.state == AQ_S_MEASURING);
        T("이미 측정 중이면 AQ_ESTATE (추가 I/O 없음)",
          aq_start_measure(&d) == AQ_ESTATE && f.writes == 1);

        AqDriver d2 = {0}; FakeI2c f2 = { .whoami = AQ_WHOAMI, .nack_writes = 1 }; FakeClock c2 = { 0 };
        setup(&d2, &f2, &c2);
        T("쓰기 NACK → AQ_EAGAIN, 상태는 IDLE 유지",
          aq_start_measure(&d2) == AQ_EAGAIN && d2.state == AQ_S_IDLE);
    }

    // ----------------------------------------------------------------- Q61
    printf("\n== Q61. 시계 주입 — 대기 없는 타임아웃 테스트 ==\n");
    {
        T("경과 < 타임아웃", deadline_expired(100, 1000, 1000) == false);
        T("경과 == 타임아웃이면 만료", deadline_expired(100, 1100, 1000) == true);
        T("tick 랩어라운드에도 정확(336ms 경과)",
          deadline_expired(0xFFFFFF00u, 0x50u, 300) == true &&
          deadline_expired(0xFFFFFF00u, 0x50u, 400) == false);

        AqDriver d = {0}; FakeI2c f = { .whoami = AQ_WHOAMI, .not_ready_times = 100000 };
        FakeClock c = { .ms = 500 };
        setup(&d, &f, &c);
        (void)aq_start_measure(&d);                     // op_start_ms = 500
        T("아직 준비 안 됨 → AQ_EAGAIN", aq_wait_ready(&d) == AQ_EAGAIN);
        c.ms = 1400;                                    // 900ms 경과 (timeout 1000)
        T("타임아웃 직전엔 여전히 EAGAIN", aq_wait_ready(&d) == AQ_EAGAIN);
        c.ms = 1500;                                    // ★ 시계를 1초 앞으로 — 실제 대기 0
        T("타임아웃 경계에서 AQ_ETIMEDOUT", aq_wait_ready(&d) == AQ_ETIMEDOUT);

        AqDriver d2 = {0}; FakeI2c f2 = { .whoami = AQ_WHOAMI }; FakeClock c2 = { 0 };
        setup(&d2, &f2, &c2);
        T("MEASURING 이 아니면 폴링 거부(AQ_ESTATE)", aq_wait_ready(&d2) == AQ_ESTATE);
        (void)aq_start_measure(&d2);
        T("RDY 비트가 서면 AQ_OK", aq_wait_ready(&d2) == AQ_OK);
    }

    // ----------------------------------------------------------------- Q62
    printf("\n== Q62. 재시도/백오프 정책 (순수 함수) ==\n");
    {
        T("성공·영구 버스 오류는 재시도 대상이 아니다",
          should_retry(IO_OK, 0, 3) == false && should_retry(IO_BUS_ERR, 0, 3) == false);
        T("NACK + 예산 남음 → 재시도", should_retry(IO_NACK, 1, 3) == true);
        T("예산 소진(또는 max=0) → 중단",
          should_retry(IO_NACK, 3, 3) == false && should_retry(IO_NACK, 0, 0) == false);

        T("백오프 1회차 = base", next_delay_ms(0, 10, 200) == 10);
        T("백오프 지수 증가 (10→20→40→80)",
          next_delay_ms(1, 10, 200) == 20 && next_delay_ms(2, 10, 200) == 40 &&
          next_delay_ms(3, 10, 200) == 80);
        T("상한에서 포화 + 오버플로 없음",
          next_delay_ms(5, 10, 200) == 200 && next_delay_ms(1000, 7, 5000) == 5000);
        T("base=0 이면 항상 0", next_delay_ms(4, 0, 200) == 0);
    }

    // ----------------------------------------------------------------- Q63
    printf("\n== Q63. 에러 전파 — 값으로 반환, 삼키지 않기 ==\n");
    {
        T("일시적(NACK)과 영구(BUS_ERR) 오류를 다른 코드로 구분",
          io_to_err(IO_OK) == AQ_OK && io_to_err(IO_NACK) == AQ_EAGAIN &&
          io_to_err(IO_BUS_ERR) == AQ_EIO);

        bool all_named = true;
        for (int e = 0; e < AQ_ERR__COUNT; e++)
            if (strcmp(aq_strerror((AqErr)e), "UNKNOWN") == 0) all_named = false;
        T("모든 에러 코드에 문자열이 있다", all_named);
        T("범위 밖 코드는 UNKNOWN", strcmp(aq_strerror((AqErr)99), "UNKNOWN") == 0);
        T("문자열에 원인이 드러난다", strstr(aq_strerror(AQ_ECRC), "checksum") != NULL);

        AqDriver d = {0}; FakeI2c f = { .whoami = AQ_WHOAMI, .bus_error = true }; FakeClock c = { 0 };
        setup(&d, &f, &c);
        uint8_t raw[3] = { 0xAA, 0xBB, 0xCC };
        T("읽기 실패는 그대로 전파(AQ_EIO)", aq_read_raw(&d, raw) == AQ_EIO);
        T("실패 시 출력 버퍼는 건드리지 않는다", raw[0] == 0xAA && raw[1] == 0xBB && raw[2] == 0xCC);
        T("실패를 삼키지 않고 카운터에 남긴다", d.io_errors == 1);
        T("NULL 인자는 크래시 대신 AQ_EINVAL", aq_read_raw(&d, NULL) == AQ_EINVAL);
    }

    // ----------------------------------------------------------------- Q64
    printf("\n== Q64. 전이 테이블 기반 상태머신 ==\n");
    {
        T("정상 경로: IDLE→MEASURING→READY→IDLE",
          aq_next_state(AQ_S_IDLE, AQ_E_START) == AQ_S_MEASURING &&
          aq_next_state(AQ_S_MEASURING, AQ_E_DATA_RDY) == AQ_S_READY &&
          aq_next_state(AQ_S_READY, AQ_E_DECODED) == AQ_S_IDLE);
        T("READY + RETRY → IDLE (CRC 실패 복구)", aq_next_state(AQ_S_READY, AQ_E_RETRY) == AQ_S_IDLE);
        T("FAULT 는 RECOVER 로만 빠져나온다",
          aq_next_state(AQ_S_FAULT, AQ_E_START) == AQ_S_INVALID &&
          aq_next_state(AQ_S_FAULT, AQ_E_RECOVER) == AQ_S_IDLE);
        T("범위 밖 이벤트는 INVALID", aq_next_state(AQ_S_IDLE, (AqEvent)99) == AQ_S_INVALID);

        AqDriver d = {0}; FakeI2c f = { .whoami = AQ_WHOAMI }; FakeClock c = { 0 };
        setup(&d, &f, &c);
        T("유효 전이는 적용된다", aq_fsm_apply(&d, AQ_E_START) && d.state == AQ_S_MEASURING);
        T("잘못된 전이는 거부 + 상태 불변",
          aq_fsm_apply(&d, AQ_E_START) == false && d.state == AQ_S_MEASURING);
        T("거부된 전이는 통계에 남는다", d.bad_transitions == 1);
        T("어떤 상태에서도 FAIL 은 FAULT 로",
          aq_fsm_apply(&d, AQ_E_FAIL) && d.state == AQ_S_FAULT);
    }

    // ----------------------------------------------------------------- Q65
    printf("\n== Q65. CRC 검증 — 오염 데이터 유출 차단 ==\n");
    {
        const uint8_t vec[2] = { 0xBE, 0xEF };
        T("crc8 알려진 벡터 {0xBE,0xEF} = 0x92", crc8(vec, 2) == 0x92);
        T("순수 함수: 반복 호출 동일 결과, 빈 입력은 init 0xFF",
          crc8(vec, 2) == crc8(vec, 2) && crc8(vec, 0) == 0xFF);

        uint8_t good[3] = { 0x02, 0xE6, 0 };            // 0x02E6 = 742
        good[2] = crc8(good, 2);
        int16_t ppb = -1;
        T("정상 프레임 디코드", aq_decode(good, &ppb) == AQ_OK && ppb == 742);

        uint8_t bad[3] = { good[0], good[1], (uint8_t)(good[2] ^ 0x01) };
        int16_t keep = -12345;
        T("CRC 불일치 → AQ_ECRC", aq_decode(bad, &keep) == AQ_ECRC);
        T("실패 시 out 파라미터 미변경", keep == -12345);

        uint8_t neg[3] = { 0xFF, 0xD8, 0 };             // -40
        neg[2] = crc8(neg, 2);
        int16_t t2 = 0;
        T("음수 값도 정확히 복원(-40)", aq_decode(neg, &t2) == AQ_OK && t2 == -40);
        T("NULL 방어", aq_decode(good, NULL) == AQ_EINVAL);
    }

    // ----------------------------------------------------------------- Q66
    printf("\n== Q66. 고정 크기 링 로그 + 로거 주입 ==\n");
    {
        RingLog rl = {0}; rlog_init(&rl);
        T("init 후 비어 있다", rlog_count(&rl) == 0 && rlog_at(&rl, 0) == NULL);
        rlog_push(&rl, LOG_INFO, "a");
        rlog_push(&rl, LOG_INFO, "b");
        rlog_push(&rl, LOG_INFO, "c");
        T("순서대로 쌓인다(0 = 가장 오래된 줄), 범위 밖은 NULL",
          rlog_count(&rl) == 3 && str_eq(rlog_at(&rl, 0), "a") &&
          str_eq(rlog_at(&rl, 2), "c") && rlog_at(&rl, 3) == NULL);

        RingLog r2 = {0}; rlog_init(&r2);
        char buf[16];
        for (int i = 0; i < (int)RLOG_CAP + 2; i++) {
            snprintf(buf, sizeof buf, "m%d", i);
            rlog_push(&r2, LOG_DEBUG, buf);
        }
        T("용량에서 포화 (malloc 없음)", rlog_count(&r2) == RLOG_CAP);
        T("가장 오래된 줄을 덮어쓴다", str_eq(rlog_at(&r2, 0), "m2"));
        T("덮어쓴 개수를 센다", r2.dropped == 2);

        RingLog r3 = {0}; rlog_init(&r3);
        char longmsg[200];
        memset(longmsg, 'x', sizeof longmsg - 1);
        longmsg[sizeof longmsg - 1] = '\0';
        rlog_push(&r3, LOG_WARN, longmsg);
        T("긴 메시지는 안전하게 잘린다",
          rlog_at(&r3, 0) != NULL && strlen(rlog_at(&r3, 0)) == RLOG_MSG - 1);

        AqDriver d = {0}; FakeI2c f = { .whoami = AQ_WHOAMI }; FakeClock c = { 0 };
        setup(&d, &f, &c);
        aq_log(&d, LOG_ERROR, "no logger yet");          // 크래시하면 안 된다
        T("로거 미주입 시 no-op", d.log.write == NULL);
        RingLog sink = {0}; rlog_init(&sink);
        LogOps lo = { rlog_sink, &sink };
        aq_set_logger(&d, lo);
        aq_log(&d, LOG_WARN, "sensor hiccup");
        T("주입된 싱크로 로그가 흘러간다",
          rlog_count(&sink) == 1 && log_has(&sink, "hiccup") && sink.lvl[0] == LOG_WARN);
    }

    // ----------------------------------------------------------------- Q67
    printf("\n== Q67. 설정 검증 (경계값 + 이유 코드) ==\n");
    {
        AqConfig ok = { .i2c_addr = 0x62, .period_ms = 1000, .timeout_ms = 500, .max_retries = 3 };
        T("정상 설정 통과", aq_config_validate(&ok) == CFG_OK);
        T("NULL → CFG_E_NULL", aq_config_validate(NULL) == CFG_E_NULL);

        AqConfig c = ok; c.i2c_addr = 0x07;
        AqConfig c2 = ok; c2.i2c_addr = 0x78;
        T("I2C 예약 주소(0x07 / 0x78) 거부",
          aq_config_validate(&c) == CFG_E_ADDR && aq_config_validate(&c2) == CFG_E_ADDR);
        c = ok; c.i2c_addr = 0x08;
        c2 = ok; c2.i2c_addr = 0x77;
        T("경계 주소 0x08 / 0x77 은 허용",
          aq_config_validate(&c) == CFG_OK && aq_config_validate(&c2) == CFG_OK);

        c = ok; c.period_ms = 0;
        c2 = ok; c2.period_ms = 99;
        T("주기 0 / 하한 미만 거부",
          aq_config_validate(&c) == CFG_E_PERIOD && aq_config_validate(&c2) == CFG_E_PERIOD);
        c = ok; c.period_ms = 100; c.timeout_ms = 100;
        T("주기 하한 경계 허용", aq_config_validate(&c) == CFG_OK);

        c = ok; c.timeout_ms = 0;
        c2 = ok; c2.timeout_ms = 1001;
        T("타임아웃 0 / 주기 초과 거부",
          aq_config_validate(&c) == CFG_E_TIMEOUT && aq_config_validate(&c2) == CFG_E_TIMEOUT);
        c = ok; c.max_retries = 11;
        T("과도한 재시도 거부", aq_config_validate(&c) == CFG_E_RETRIES);
    }

    // ----------------------------------------------------------------- Q68
    printf("\n== Q68. 통합 시나리오 (하드웨어 0개, 대기 0초) ==\n");
    {
        AqDriver d = {0};
        FakeI2c f = { .whoami = AQ_WHOAMI, .value = 742, .nack_writes = 2, .not_ready_times = 2 };
        FakeClock c = { .ms = 0 };
        RingLog sink = {0}; rlog_init(&sink);
        setup(&d, &f, &c);
        LogOps lo = { rlog_sink, &sink };
        aq_set_logger(&d, lo);

        int16_t ppb = -1;

        // ① 시작 커맨드가 두 번 NACK → 재시도로 흡수
        T("틱1: NACK → EAGAIN(상태 유지)", aq_service(&d, &ppb) == AQ_EAGAIN && d.state == AQ_S_IDLE);
        T("틱2: NACK → EAGAIN", aq_service(&d, &ppb) == AQ_EAGAIN && d.retries == 2);
        T("틱3: 쓰기 성공 → MEASURING", aq_service(&d, &ppb) == AQ_EAGAIN && d.state == AQ_S_MEASURING);
        T("재시도 동안 쓰기는 3회 나갔다", f.writes == 3);

        // ② 준비될 때까지 폴링 (시계는 테스트가 돌린다)
        c.ms = 10;  AqErr t4 = aq_service(&d, &ppb);
        c.ms = 20;  AqErr t5 = aq_service(&d, &ppb);
        T("틱4~5: 아직 준비 안 됨 → EAGAIN", t4 == AQ_EAGAIN && t5 == AQ_EAGAIN);

        // ③ 준비됨 → 그런데 CRC 가 깨졌다
        f.corrupt_crc = true;
        c.ms = 30;
        T("틱6: RDY 감지 → READY", aq_service(&d, &ppb) == AQ_EAGAIN && d.state == AQ_S_READY);
        T("틱7: CRC 실패 → AQ_ECRC, IDLE 로 되돌아 재측정",
          aq_service(&d, &ppb) == AQ_ECRC && d.state == AQ_S_IDLE && d.crc_errors == 1);
        T("CRC 실패 시 출력값은 오염되지 않았다", ppb == -1);
        T("CRC 실패가 로그에 남았다", log_has(&sink, "crc"));

        // ④ 재측정 → 성공
        f.corrupt_crc = false;
        T("틱8~9: 재측정 시작 → RDY",
          aq_service(&d, &ppb) == AQ_EAGAIN && d.state == AQ_S_MEASURING &&
          aq_service(&d, &ppb) == AQ_EAGAIN && d.state == AQ_S_READY);
        T("틱10: 값 획득 (742 ppb) 후 IDLE 복귀",
          aq_service(&d, &ppb) == AQ_OK && ppb == 742 && d.state == AQ_S_IDLE);

        // ⑤ 센서가 영영 준비되지 않는다 → 타임아웃 → FAULT 고착
        f.not_ready_times = 100000;
        AqErr t11 = aq_service(&d, &ppb);
        AqErr t12 = aq_service(&d, &ppb);
        T("틱11~12: 새 측정 시작 후 대기 중", t11 == AQ_EAGAIN && t12 == AQ_EAGAIN);
        c.ms += 1000;                                   // ★ 가짜 시계만 1초 전진
        T("틱13: AQ_ETIMEDOUT → FAULT",
          aq_service(&d, &ppb) == AQ_ETIMEDOUT && d.state == AQ_S_FAULT);
        T("타임아웃이 로그에 남았다", log_has(&sink, "timeout"));
        T("FAULT 는 명시적 복구 전까지 고착", aq_service(&d, &ppb) == AQ_ESTATE);
        T("타임아웃이 값을 덮어쓰지 않았다", ppb == 742);

        aq_recover(&d);
        T("복구 후 다시 측정 가능",
          d.state == AQ_S_IDLE && aq_service(&d, &ppb) == AQ_EAGAIN);
        T("전 시나리오에서 실제 HAL 호출 0회", g_real_hal_calls == 0);
    }

    double secs = (double)(clock() - t_start) / (double)CLOCKS_PER_SEC;
    printf("\n== 전체 스위트 소요 시간 ==\n");
    T("가짜 시계 덕분에 실제 대기는 사실상 0 (< 1s)", secs < 1.0);

    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
