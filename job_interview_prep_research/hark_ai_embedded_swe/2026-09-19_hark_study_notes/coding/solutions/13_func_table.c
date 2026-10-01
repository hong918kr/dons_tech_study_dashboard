/* 13_func_table.c — 함수 포인터 디스패치 테이블과 명령 핸들러 (모범답안)
 *
 *   cc -std=c11 -Wall -Wextra -O2 -g solutions/13_func_table.c -o sol_13 && ./sol_13
 *
 * 핵심 아이디어
 *   - switch 문은 코드다. 테이블은 데이터다. 명령이 늘어날 때 switch는 함수를
 *     고쳐야 하고, 테이블은 행 하나를 추가하면 끝난다. 명령이 통신 프로토콜에서
 *     오는 펌웨어는 거의 항상 테이블을 쓴다.
 *   - 테이블은 `const`로 두어 .rodata(flash)에 올린다. RAM을 한 바이트도 쓰지
 *     않고, 런타임에 덮어써질 위험도 사라진다.
 *   - 핸들러는 전역 상태를 보지 않는다. 대신 `void *ctx`를 받는다. 같은 테이블로
 *     서로 다른 장치 인스턴스를 구동할 수 있어야 한다 — 이게 콜백 설계의 핵심이다.
 *   - 함수 포인터 테이블은 잘못되면 "아무 주소로 점프"가 된다. 그래서 검증
 *     함수가 따라붙는다. NULL 핸들러, opcode 중복, 인자 개수 범위를 부팅 때
 *     한 번 확인한다.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 1. 계약: 핸들러 시그니처와 반환 코드                                 */
/* ------------------------------------------------------------------ */

/* 명령 핸들러. 인자는 원시 바이트열과 그 길이, 그리고 caller가 준 문맥이다.
 * 반환: 0 이상은 핸들러가 정한 성공 값, 음수는 오류. */
typedef int (*cmd_fn)(const uint8_t *args, size_t nargs, void *ctx);

/* 디스패처가 내는 오류. 핸들러가 내는 오류와 섞이지 않게 값을 떼어 둔다.
 * 핸들러는 -1 ~ -15를, 디스패처는 -16 이하를 쓴다는 규약이다. */
#define CMD_ERR_UNKNOWN   (-16)   /* 그 opcode가 테이블에 없다              */
#define CMD_ERR_NARGS     (-17)   /* 인자 개수가 min..max 밖이다             */
#define CMD_ERR_NULL_FN   (-18)   /* 테이블 행의 핸들러가 NULL이다           */
#define CMD_ERR_BADTABLE  (-19)   /* 테이블/인자 자체가 잘못됐다             */

/* 핸들러가 쓰는 오류 */
#define APP_ERR_RANGE     (-1)
#define APP_ERR_STATE     (-2)

/* 테이블 한 행. 전부 const로 두어 flash에 놓는다. */
typedef struct {
    uint8_t     opcode;      /* 와이어에 실려 오는 1바이트 명령 코드        */
    const char *name;        /* 로그·디버그용. 이름이 없으면 추적이 지옥이다 */
    cmd_fn      fn;          /* 핸들러                                     */
    uint8_t     min_args;    /* 허용 인자 바이트 수 하한 (포함)             */
    uint8_t     max_args;    /* 허용 인자 바이트 수 상한 (포함)             */
} cmd_entry_t;

/* ------------------------------------------------------------------ */
/* 2. 문맥 — 핸들러가 만지는 "그 장치"                                  */
/* ------------------------------------------------------------------ */

/* 전역 변수를 쓰지 않는 이유: 마이크가 두 개면 이 구조체가 두 개다.
 * 핸들러 코드는 한 벌이고, ctx만 갈아 끼워 두 장치를 구동한다. */
typedef struct {
    uint8_t  gain;           /* 0~64                                       */
    uint8_t  muted;          /* 0/1                                        */
    uint16_t sample_rate_hz10; /* 10Hz 단위. 16000Hz → 1600                */
    uint32_t reset_count;
    uint32_t cmd_count;      /* 이 장치가 처리한 명령 수                    */
} mic_ctx_t;

/* ------------------------------------------------------------------ */
/* 3. 핸들러들 — 작고, ctx만 본다                                      */
/* ------------------------------------------------------------------ */

/* PING: 인자 없음. 0을 돌려준다. */
static int h_ping(const uint8_t *args, size_t nargs, void *ctx)
{
    (void)args;
    (void)nargs;
    mic_ctx_t *m = (mic_ctx_t *)ctx;
    if (m == NULL) {
        return APP_ERR_STATE;
    }
    m->cmd_count++;
    return 0;
}

/* SET_GAIN: 인자 1바이트. 0~64만 허용. */
static int h_set_gain(const uint8_t *args, size_t nargs, void *ctx)
{
    mic_ctx_t *m = (mic_ctx_t *)ctx;
    if (m == NULL || args == NULL || nargs != 1u) {
        return APP_ERR_STATE;
    }
    if (args[0] > 64u) {
        return APP_ERR_RANGE;            /* 범위 검사는 핸들러의 책임 */
    }
    m->gain = args[0];
    m->cmd_count++;
    return 0;
}

/* GET_GAIN: 인자 없음. 현재 gain을 성공 값으로 돌려준다(0~64이므로 음수와 겹치지 않는다). */
static int h_get_gain(const uint8_t *args, size_t nargs, void *ctx)
{
    (void)args;
    (void)nargs;
    mic_ctx_t *m = (mic_ctx_t *)ctx;
    if (m == NULL) {
        return APP_ERR_STATE;
    }
    m->cmd_count++;
    return (int)m->gain;
}

/* SET_RATE: 인자 2바이트, big-endian, 10Hz 단위. 800~4800(8k~48kHz)만 허용. */
static int h_set_rate(const uint8_t *args, size_t nargs, void *ctx)
{
    mic_ctx_t *m = (mic_ctx_t *)ctx;
    if (m == NULL || args == NULL || nargs != 2u) {
        return APP_ERR_STATE;
    }
    uint16_t v = (uint16_t)(((uint16_t)args[0] << 8) | (uint16_t)args[1]);
    if (v < 800u || v > 4800u) {
        return APP_ERR_RANGE;
    }
    m->sample_rate_hz10 = v;
    m->cmd_count++;
    return 0;
}

/* MUTE: 인자 1바이트(0/1). 그 외 값은 범위 오류. */
static int h_mute(const uint8_t *args, size_t nargs, void *ctx)
{
    mic_ctx_t *m = (mic_ctx_t *)ctx;
    if (m == NULL || args == NULL || nargs != 1u) {
        return APP_ERR_STATE;
    }
    if (args[0] > 1u) {
        return APP_ERR_RANGE;
    }
    m->muted = args[0];
    m->cmd_count++;
    return 0;
}

/* RESET: 인자 0~1바이트(옵션 플래그). 상태를 기본값으로 되돌린다. */
static int h_reset(const uint8_t *args, size_t nargs, void *ctx)
{
    mic_ctx_t *m = (mic_ctx_t *)ctx;
    if (m == NULL) {
        return APP_ERR_STATE;
    }
    uint32_t keep = m->reset_count;
    uint32_t cmds = m->cmd_count;

    memset(m, 0, sizeof *m);
    m->gain             = 16u;
    m->sample_rate_hz10 = 1600u;         /* 16 kHz */
    m->reset_count      = keep + 1u;
    m->cmd_count        = cmds + 1u;

    /* 인자 1바이트가 오고 bit0이 서 있으면 카운터까지 지운다 */
    if (args != NULL && nargs == 1u && (args[0] & 1u) != 0u) {
        m->reset_count = 0u;
        m->cmd_count   = 0u;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 4. 테이블                                                           */
/* ------------------------------------------------------------------ */

#define OP_PING     0x01u
#define OP_SET_GAIN 0x10u
#define OP_GET_GAIN 0x11u
#define OP_SET_RATE 0x20u
#define OP_MUTE     0x30u
#define OP_RESET    0xF0u

/* const 배열이므로 .rodata에 놓인다. 행이 여섯이면 이게 프로토콜 전부다.
 * 새 명령을 추가하는 일은 행 하나를 더 쓰는 일이고, 디스패처는 안 고친다. */
static const cmd_entry_t g_cmd_table[] = {
    {OP_PING,     "PING",     h_ping,     0u, 0u},
    {OP_SET_GAIN, "SET_GAIN", h_set_gain, 1u, 1u},
    {OP_GET_GAIN, "GET_GAIN", h_get_gain, 0u, 0u},
    {OP_SET_RATE, "SET_RATE", h_set_rate, 2u, 2u},
    {OP_MUTE,     "MUTE",     h_mute,     1u, 1u},
    {OP_RESET,    "RESET",    h_reset,    0u, 1u},
};

#define CMD_TABLE_N (sizeof g_cmd_table / sizeof g_cmd_table[0])

/* ------------------------------------------------------------------ */
/* 5. 테이블 검증 — 부팅 때 한 번                                       */
/* ------------------------------------------------------------------ */

/* 테이블이 쓸 수 있는 상태인지 확인한다. 반환: 0 정상, 음수 오류.
 *
 * 왜 이게 필요한가. 함수 포인터가 NULL인 행 하나를 디스패처가 호출하면
 * 주소 0으로 점프한다. Cortex-M에서는 그 순간 HardFault가 나고, 스택에는
 * "왜 여기 왔는지" 알려주는 정보가 거의 없다. 그 버그를 필드에서 잡는 것보다
 * 부팅 직후 assert로 잡는 것이 만 배 싸다.
 *
 * 검사 항목
 *   - fn이 NULL인 행
 *   - name이 NULL인 행 (로그가 죽는다)
 *   - min_args > max_args 인 행
 *   - 같은 opcode가 두 번 나오는 것 (앞의 것만 먹혀 조용히 오동작한다) */
int cmd_table_validate(const cmd_entry_t *tbl, size_t n)
{
    if (tbl == NULL || n == 0u) {
        return CMD_ERR_BADTABLE;
    }

    for (size_t i = 0u; i < n; i++) {
        if (tbl[i].fn == NULL) {
            return CMD_ERR_NULL_FN;
        }
        if (tbl[i].name == NULL) {
            return CMD_ERR_BADTABLE;
        }
        if (tbl[i].min_args > tbl[i].max_args) {
            return CMD_ERR_BADTABLE;
        }
        for (size_t j = i + 1u; j < n; j++) {
            if (tbl[i].opcode == tbl[j].opcode) {
                return CMD_ERR_BADTABLE;  /* 중복 opcode */
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* 6. 조회와 디스패치                                                  */
/* ------------------------------------------------------------------ */

/* opcode로 행을 찾는다. 없으면 NULL. O(n) 선형 탐색.
 * 명령이 여섯 개면 이게 가장 빠르고 가장 작다. 캐시도 분기 예측도 필요 없다. */
const cmd_entry_t *cmd_find(const cmd_entry_t *tbl, size_t n, uint8_t opcode)
{
    if (tbl == NULL) {
        return NULL;
    }
    for (size_t i = 0u; i < n; i++) {
        if (tbl[i].opcode == opcode) {
            return &tbl[i];
        }
    }
    return NULL;
}

/* 한 프레임을 처리한다.
 * 반환: 핸들러의 반환값, 또는 CMD_ERR_* 중 하나.
 *
 * 순서가 중요하다. 찾기 → NULL 검사 → 인자 개수 검사 → 호출.
 * 호출 직전까지 어떤 부수효과도 만들지 않는다. 거부된 명령은 ctx를 건드리지
 * 않아야 재전송·복구 로직을 단순하게 만들 수 있다. */
int cmd_dispatch(const cmd_entry_t *tbl, size_t n, uint8_t opcode,
                 const uint8_t *args, size_t nargs, void *ctx)
{
    const cmd_entry_t *e = cmd_find(tbl, n, opcode);
    if (e == NULL) {
        return CMD_ERR_UNKNOWN;
    }
    if (e->fn == NULL) {
        return CMD_ERR_NULL_FN;           /* 검증을 통과했어도 한 번 더 본다 */
    }
    if (nargs < (size_t)e->min_args || nargs > (size_t)e->max_args) {
        return CMD_ERR_NARGS;
    }
    return e->fn(args, nargs, ctx);
}

/* ------------------------------------------------------------------ */
/* 7. O(1) 인덱스 — 명령이 많아지면                                     */
/* ------------------------------------------------------------------ */

/* opcode(0~255)에서 테이블 행 번호로 가는 역인덱스. -1은 "없음".
 * 256바이트를 쓰는 대신 조회가 상수 시간이 된다. 명령이 수십 개로 늘고
 * 디스패치가 ISR이나 오디오 프레임 경로에 있으면 이 교환이 이득이다. */
#define CMD_INDEX_SIZE 256u
#define CMD_INDEX_NONE ((int16_t)-1)

/* 인덱스를 만든다. 반환: 0 정상, 음수 오류.
 * 중복 opcode가 있으면 실패로 처리한다 — 조용히 하나를 덮는 것이 더 나쁘다.
 * 테이블 행 수가 인덱스에 담을 수 있는 범위를 넘는지도 본다. */
int cmd_index_build(const cmd_entry_t *tbl, size_t n, int16_t *index)
{
    if (tbl == NULL || index == NULL || n == 0u || n > 32767u) {
        return CMD_ERR_BADTABLE;
    }

    for (size_t i = 0u; i < CMD_INDEX_SIZE; i++) {
        index[i] = CMD_INDEX_NONE;
    }
    for (size_t i = 0u; i < n; i++) {
        uint8_t op = tbl[i].opcode;
        if (index[op] != CMD_INDEX_NONE) {
            return CMD_ERR_BADTABLE;      /* 중복 */
        }
        index[op] = (int16_t)i;
    }
    return 0;
}

/* 인덱스를 통한 디스패치. 배열 한 번 읽고 끝이므로 O(1).
 * opcode가 uint8_t라 인덱스 범위를 벗어날 수 없다는 점이 중요하다.
 * 만약 opcode가 uint16_t였다면 여기에 범위 검사가 반드시 있어야 한다. */
int cmd_dispatch_indexed(const cmd_entry_t *tbl, size_t n,
                         const int16_t *index, uint8_t opcode,
                         const uint8_t *args, size_t nargs, void *ctx)
{
    if (tbl == NULL || index == NULL || n == 0u) {
        return CMD_ERR_BADTABLE;
    }

    int16_t slot = index[opcode];
    if (slot == CMD_INDEX_NONE) {
        return CMD_ERR_UNKNOWN;
    }
    if ((size_t)slot >= n) {
        return CMD_ERR_BADTABLE;          /* 인덱스가 테이블과 어긋났다 */
    }

    const cmd_entry_t *e = &tbl[slot];
    if (e->fn == NULL) {
        return CMD_ERR_NULL_FN;
    }
    if (nargs < (size_t)e->min_args || nargs > (size_t)e->max_args) {
        return CMD_ERR_NARGS;
    }
    return e->fn(args, nargs, ctx);
}

/* ================================================================== */
/* 테스트                                                              */
/* ================================================================== */

static mic_ctx_t make_ctx(void)
{
    mic_ctx_t m;
    memset(&m, 0, sizeof m);
    m.gain             = 16u;
    m.sample_rate_hz10 = 1600u;
    return m;
}

static void test_table_validates(void)
{
    assert(cmd_table_validate(g_cmd_table, CMD_TABLE_N) == 0);

    /* NULL 테이블 / 길이 0 */
    assert(cmd_table_validate(NULL, CMD_TABLE_N) == CMD_ERR_BADTABLE);
    assert(cmd_table_validate(g_cmd_table, 0u) == CMD_ERR_BADTABLE);

    /* NULL 핸들러가 섞인 테이블 */
    static const cmd_entry_t bad_null[] = {
        {0x01u, "A", h_ping, 0u, 0u},
        {0x02u, "B", NULL,   0u, 0u},
    };
    assert(cmd_table_validate(bad_null, 2u) == CMD_ERR_NULL_FN);

    /* 이름이 없는 행 */
    static const cmd_entry_t bad_name[] = {
        {0x01u, NULL, h_ping, 0u, 0u},
    };
    assert(cmd_table_validate(bad_name, 1u) == CMD_ERR_BADTABLE);

    /* min > max */
    static const cmd_entry_t bad_range[] = {
        {0x01u, "A", h_ping, 3u, 1u},
    };
    assert(cmd_table_validate(bad_range, 1u) == CMD_ERR_BADTABLE);

    /* opcode 중복 — 조용히 오동작하는 유형이라 반드시 잡아야 한다 */
    static const cmd_entry_t bad_dup[] = {
        {0x10u, "A", h_ping,     0u, 0u},
        {0x20u, "B", h_get_gain, 0u, 0u},
        {0x10u, "C", h_ping,     0u, 0u},
    };
    assert(cmd_table_validate(bad_dup, 3u) == CMD_ERR_BADTABLE);

    puts("ok  1: 테이블 검증이 NULL 핸들러·이름 누락·역범위·중복 opcode를 잡는다");
}

static void test_find(void)
{
    const cmd_entry_t *e = cmd_find(g_cmd_table, CMD_TABLE_N, OP_SET_GAIN);
    assert(e != NULL);
    assert(e->opcode == OP_SET_GAIN);
    assert(strcmp(e->name, "SET_GAIN") == 0);
    assert(e->fn == h_set_gain);

    /* 첫 행과 마지막 행 (경계값) */
    assert(cmd_find(g_cmd_table, CMD_TABLE_N, OP_PING) == &g_cmd_table[0]);
    assert(cmd_find(g_cmd_table, CMD_TABLE_N, OP_RESET)
           == &g_cmd_table[CMD_TABLE_N - 1u]);

    /* 없는 opcode: 0x00, 0xFF, 그 사이 빈 값 */
    assert(cmd_find(g_cmd_table, CMD_TABLE_N, 0x00u) == NULL);
    assert(cmd_find(g_cmd_table, CMD_TABLE_N, 0xFFu) == NULL);
    assert(cmd_find(g_cmd_table, CMD_TABLE_N, 0x12u) == NULL);

    /* NULL 테이블 / 길이 0 */
    assert(cmd_find(NULL, CMD_TABLE_N, OP_PING) == NULL);
    assert(cmd_find(g_cmd_table, 0u, OP_PING) == NULL);

    puts("ok  2: 조회가 첫 행·마지막 행·없는 opcode·NULL 테이블을 구분한다");
}

static void test_dispatch_happy_path(void)
{
    mic_ctx_t m = make_ctx();

    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_PING, NULL, 0u, &m) == 0);
    assert(m.cmd_count == 1u);

    uint8_t gain = 40u;
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_SET_GAIN,
                        &gain, 1u, &m) == 0);
    assert(m.gain == 40u);

    /* GET_GAIN은 값을 반환값으로 돌려준다 */
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_GET_GAIN,
                        NULL, 0u, &m) == 40);

    uint8_t rate[2] = {0x0Cu, 0x80u};    /* 0x0C80 = 3200 → 32 kHz */
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_SET_RATE,
                        rate, 2u, &m) == 0);
    assert(m.sample_rate_hz10 == 3200u);

    uint8_t on = 1u;
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_MUTE, &on, 1u, &m) == 0);
    assert(m.muted == 1u);

    assert(m.cmd_count == 5u);
    puts("ok  3: 여섯 명령이 테이블을 통해 정확한 핸들러로 간다");
}

static void test_unknown_and_nargs(void)
{
    mic_ctx_t m = make_ctx();
    uint32_t  before = m.cmd_count;

    /* 없는 opcode */
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, 0x99u, NULL, 0u, &m)
           == CMD_ERR_UNKNOWN);

    /* 인자 개수 위반: 너무 적음 / 너무 많음 */
    uint8_t buf[4] = {1u, 2u, 3u, 4u};
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_SET_GAIN, buf, 0u, &m)
           == CMD_ERR_NARGS);
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_SET_GAIN, buf, 2u, &m)
           == CMD_ERR_NARGS);
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_SET_RATE, buf, 1u, &m)
           == CMD_ERR_NARGS);
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_PING, buf, 1u, &m)
           == CMD_ERR_NARGS);

    /* 거부된 명령은 ctx를 전혀 건드리지 않았다 */
    assert(m.cmd_count == before);
    assert(m.gain == 16u);

    /* min..max가 구간인 경우: RESET은 0과 1 둘 다 허용, 2는 거부 */
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_RESET, NULL, 0u, &m) == 0);
    uint8_t flag = 0u;
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_RESET, &flag, 1u, &m) == 0);
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_RESET, buf, 2u, &m)
           == CMD_ERR_NARGS);

    puts("ok  4: 없는 opcode와 인자 개수 위반을 핸들러 호출 전에 거부한다");
}

static void test_handler_errors_separate(void)
{
    mic_ctx_t m = make_ctx();

    /* 핸들러가 내는 오류와 디스패처가 내는 오류는 값이 겹치지 않는다 */
    uint8_t too_big = 65u;
    int rc = cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_SET_GAIN,
                          &too_big, 1u, &m);
    assert(rc == APP_ERR_RANGE);
    assert(rc > CMD_ERR_UNKNOWN);        /* 디스패처 코드 영역과 분리됨 */
    assert(m.gain == 16u);               /* 거부됐으니 값은 그대로 */

    uint8_t bad_rate[2] = {0x00u, 0x10u};  /* 16 → 160Hz, 하한 미달 */
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_SET_RATE,
                        bad_rate, 2u, &m) == APP_ERR_RANGE);
    assert(m.sample_rate_hz10 == 1600u);

    uint8_t bad_mute = 2u;
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_MUTE,
                        &bad_mute, 1u, &m) == APP_ERR_RANGE);

    /* ctx가 NULL이면 핸들러가 스스로 막는다 (디스패처는 ctx를 해석하지 않는다) */
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_PING, NULL, 0u, NULL)
           == APP_ERR_STATE);

    /* 경계값: gain 0과 64는 허용, 65는 거부 */
    uint8_t g0 = 0u;
    uint8_t g64 = 64u;
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_SET_GAIN, &g0, 1u, &m) == 0);
    assert(m.gain == 0u);
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_SET_GAIN, &g64, 1u, &m) == 0);
    assert(m.gain == 64u);

    /* 경계값: 샘플레이트 하한 800, 상한 4800 */
    uint8_t lo[2] = {0x03u, 0x20u};      /* 800 */
    uint8_t hi[2] = {0x12u, 0xC0u};      /* 4800 */
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_SET_RATE, lo, 2u, &m) == 0);
    assert(m.sample_rate_hz10 == 800u);
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_SET_RATE, hi, 2u, &m) == 0);
    assert(m.sample_rate_hz10 == 4800u);

    puts("ok  5: 핸들러 오류와 디스패처 오류가 값으로 구분되고 경계값이 정확하다");
}

static void test_context_isolation(void)
{
    /* 같은 테이블, 두 장치. 핸들러 코드는 한 벌이다. */
    mic_ctx_t left  = make_ctx();
    mic_ctx_t right = make_ctx();

    uint8_t g_left  = 10u;
    uint8_t g_right = 50u;

    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_SET_GAIN,
                        &g_left, 1u, &left) == 0);
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_SET_GAIN,
                        &g_right, 1u, &right) == 0);

    assert(left.gain == 10u);
    assert(right.gain == 50u);
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_GET_GAIN,
                        NULL, 0u, &left) == 10);
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_GET_GAIN,
                        NULL, 0u, &right) == 50);

    /* 한쪽을 리셋해도 다른 쪽은 그대로다 */
    assert(cmd_dispatch(g_cmd_table, CMD_TABLE_N, OP_RESET,
                        NULL, 0u, &left) == 0);
    assert(left.gain == 16u);
    assert(left.reset_count == 1u);
    assert(right.gain == 50u);
    assert(right.reset_count == 0u);

    puts("ok  6: ctx만 갈아 끼우면 같은 테이블이 두 장치를 독립적으로 구동한다");
}

static void test_index_dispatch(void)
{
    static int16_t index[CMD_INDEX_SIZE];
    assert(cmd_index_build(g_cmd_table, CMD_TABLE_N, index) == 0);

    /* 테이블에 있는 opcode는 행 번호를, 없는 것은 NONE을 가리킨다 */
    assert(index[OP_PING] == 0);
    assert(index[OP_RESET] == (int16_t)(CMD_TABLE_N - 1u));
    assert(index[0x00u] == CMD_INDEX_NONE);
    assert(index[0xFFu] == CMD_INDEX_NONE);

    mic_ctx_t m = make_ctx();
    uint8_t   gain = 33u;

    assert(cmd_dispatch_indexed(g_cmd_table, CMD_TABLE_N, index,
                                OP_SET_GAIN, &gain, 1u, &m) == 0);
    assert(m.gain == 33u);
    assert(cmd_dispatch_indexed(g_cmd_table, CMD_TABLE_N, index,
                                OP_GET_GAIN, NULL, 0u, &m) == 33);
    assert(cmd_dispatch_indexed(g_cmd_table, CMD_TABLE_N, index,
                                0x99u, NULL, 0u, &m) == CMD_ERR_UNKNOWN);
    assert(cmd_dispatch_indexed(g_cmd_table, CMD_TABLE_N, index,
                                OP_SET_GAIN, NULL, 0u, &m) == CMD_ERR_NARGS);

    /* 인덱스 만들기 실패 조건 */
    static const cmd_entry_t dup[] = {
        {0x10u, "A", h_ping, 0u, 0u},
        {0x10u, "B", h_ping, 0u, 0u},
    };
    assert(cmd_index_build(dup, 2u, index) == CMD_ERR_BADTABLE);
    assert(cmd_index_build(g_cmd_table, CMD_TABLE_N, NULL) == CMD_ERR_BADTABLE);
    assert(cmd_index_build(NULL, CMD_TABLE_N, index) == CMD_ERR_BADTABLE);
    assert(cmd_index_build(g_cmd_table, 0u, index) == CMD_ERR_BADTABLE);

    puts("ok  7: O(1) 인덱스 디스패치가 선형 탐색과 같은 결과를 낸다");
}

static void test_both_dispatchers_agree(void)
{
    /* 0x00 ~ 0xFF 전 opcode, 인자 길이 0~3까지 두 디스패처의 결과가 같아야 한다.
     * 이 비교가 성립하면 인덱스를 쓰는 최적화가 동작을 바꾸지 않았다는 뜻이다. */
    static int16_t index[CMD_INDEX_SIZE];
    assert(cmd_index_build(g_cmd_table, CMD_TABLE_N, index) == 0);

    uint8_t args[3] = {1u, 0u, 0u};

    for (unsigned op = 0u; op <= 0xFFu; op++) {
        for (size_t nargs = 0u; nargs <= 3u; nargs++) {
            mic_ctx_t a = make_ctx();
            mic_ctx_t b = make_ctx();

            int ra = cmd_dispatch(g_cmd_table, CMD_TABLE_N, (uint8_t)op,
                                  args, nargs, &a);
            int rb = cmd_dispatch_indexed(g_cmd_table, CMD_TABLE_N, index,
                                          (uint8_t)op, args, nargs, &b);
            assert(ra == rb);
            assert(memcmp(&a, &b, sizeof a) == 0);   /* 상태 변화까지 동일 */
        }
    }
    puts("ok  8: opcode 256개 x 인자 0~3 전 조합에서 두 디스패처가 완전히 일치한다");
}

static void test_null_fn_row_is_refused(void)
{
    /* 검증을 건너뛴 테이블이 들어와도 디스패처가 NULL 점프를 하지 않는다.
     * 이 방어가 없으면 주소 0으로 점프해 HardFault가 난다. */
    static const cmd_entry_t tbl[] = {
        {0x01u, "OK",  h_ping, 0u, 0u},
        {0x02u, "BAD", NULL,   0u, 0u},
    };
    mic_ctx_t m = make_ctx();

    assert(cmd_table_validate(tbl, 2u) == CMD_ERR_NULL_FN);   /* 부팅 때 걸린다 */
    assert(cmd_dispatch(tbl, 2u, 0x01u, NULL, 0u, &m) == 0);
    assert(cmd_dispatch(tbl, 2u, 0x02u, NULL, 0u, &m) == CMD_ERR_NULL_FN);

    /* 인덱스 경로도 같다 */
    static int16_t index[CMD_INDEX_SIZE];
    assert(cmd_index_build(tbl, 2u, index) == 0);
    assert(cmd_dispatch_indexed(tbl, 2u, index, 0x02u, NULL, 0u, &m)
           == CMD_ERR_NULL_FN);

    /* 인덱스가 테이블보다 길다고 주장하는 상황 — 범위를 넘는 slot을 거부한다 */
    assert(cmd_dispatch_indexed(tbl, 1u, index, 0x02u, NULL, 0u, &m)
           == CMD_ERR_BADTABLE);

    puts("ok  9: NULL 핸들러 행과 어긋난 인덱스를 호출 대신 오류로 돌려준다");
}

static void test_frame_driver(void)
{
    /* 실전 형태: [opcode][len][payload...] 프레임 스트림을 순서대로 처리한다 */
    static const uint8_t stream[] = {
        OP_PING,     0u,
        OP_SET_GAIN, 1u, 20u,
        OP_SET_RATE, 2u, 0x06u, 0x40u,   /* 1600 → 16 kHz */
        OP_MUTE,     1u, 1u,
        0x77u,       0u,                 /* 모르는 명령 — 무시하고 계속 */
        OP_SET_GAIN, 2u, 1u, 2u,         /* 인자 개수 위반 — 무시하고 계속 */
        OP_GET_GAIN, 0u,
    };
    mic_ctx_t m = make_ctx();

    size_t i = 0u;
    int    ok = 0;
    int    rejected = 0;
    int    last = 0;

    while (i + 2u <= sizeof stream) {
        uint8_t op  = stream[i];
        uint8_t len = stream[i + 1u];
        if (i + 2u + len > sizeof stream) {
            break;                       /* 잘린 프레임 — 다음 바이트를 기다린다 */
        }
        int rc = cmd_dispatch(g_cmd_table, CMD_TABLE_N, op,
                              &stream[i + 2u], len, &m);
        if (rc < 0) {
            rejected++;
        } else {
            ok++;
            last = rc;
        }
        i += 2u + (size_t)len;
    }

    assert(ok == 5);                     /* PING, SET_GAIN, SET_RATE, MUTE, GET_GAIN */
    assert(rejected == 2);               /* 모르는 명령 + 인자 개수 위반 */
    assert(last == 20);                  /* GET_GAIN이 마지막에 20을 돌려줬다 */
    assert(m.gain == 20u);
    assert(m.sample_rate_hz10 == 1600u);
    assert(m.muted == 1u);
    assert(m.cmd_count == 5u);

    puts("ok 10: 프레임 스트림을 돌리며 나쁜 프레임만 건너뛰고 계속 진행한다");
}

int main(void)
{
    printf("table: %zu commands, entry=%zu bytes, table=%zu bytes (.rodata)\n",
           CMD_TABLE_N, sizeof(cmd_entry_t), sizeof g_cmd_table);

    test_table_validates();
    test_find();
    test_dispatch_happy_path();
    test_unknown_and_nargs();
    test_handler_errors_separate();
    test_context_isolation();
    test_index_dispatch();
    test_both_dispatchers_agree();
    test_null_fn_row_is_refused();
    test_frame_driver();

    puts("ALL TESTS PASSED");
    return 0;
}
