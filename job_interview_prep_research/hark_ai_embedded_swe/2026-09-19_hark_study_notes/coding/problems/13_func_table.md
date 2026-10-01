# 13. 함수 포인터 디스패치 테이블 — 명령 핸들러와 콜백 문맥

> **주제**: 함수 포인터 · 디스패치 테이블 · 콜백 + `void *ctx` · 테이블 검증 · **난이도**: 기초 · **목표 시간**: 25분
> **레벨**: L1 메모리·포인터 · **해설은 보지 말 것**: 먼저 `starters/13_func_table.c`를 채워 `make run N=13`으로 통과시킨다.

## 면접관의 문장

"Our device takes commands over BLE. One opcode byte, one length byte, then the payload. Right now the handler is a 400-line `switch` and every new feature adds a case. I want to replace it with a table.

Write me the dispatcher. It has to reject an unknown opcode, reject a wrong argument count, and never call a null function pointer — if the table has a bad row I'd rather find out at boot than jump to address zero in the field. The handlers can't use globals, because we're about to ship a two-microphone version and the same handler has to drive either one.

Then convince me the table version is actually better than the switch. Code size, speed, both, neither?"

## 요구사항

1. 핸들러 시그니처는 `int (*)(const uint8_t *args, size_t nargs, void *ctx)`다. 반환값은 `0` 이상이 성공, 음수가 오류다.
2. 테이블 한 행 `cmd_entry_t`는 `opcode`, `name`, `fn`, `min_args`, `max_args`를 갖고, 테이블은 `const`로 두어 `.rodata`(flash)에 올린다.
3. 오류 코드 영역을 나눈다. 핸들러는 `-1 ~ -15`(`APP_ERR_*`), 디스패처는 `-16` 이하(`CMD_ERR_*`). 호출자가 반환값만 보고 "누가 거절했는지" 구분할 수 있어야 한다.
4. 핸들러 여섯 개를 구현한다. `h_ping`(인자 0), `h_set_gain`(1바이트, 0~64), `h_get_gain`(인자 0, gain을 성공 값으로 반환), `h_set_rate`(2바이트 big-endian, 800~4800), `h_mute`(1바이트, 0/1), `h_reset`(0~1바이트).
5. 핸들러는 전역 변수를 읽거나 쓰지 않는다. 상태는 전부 `ctx`(`mic_ctx_t`)에 있고, 같은 테이블로 두 인스턴스를 독립적으로 구동할 수 있어야 한다. 핸들러가 `ctx == NULL`을 스스로 막고 `APP_ERR_STATE`를 돌려준다. 디스패처는 `ctx`를 해석하지 않는다.
6. 값 범위 검사는 핸들러의 책임, 개수 검사는 디스패처의 책임이다. 이 경계를 섞지 않는다.
7. `cmd_table_validate(tbl, n)`은 다음을 잡는다. `tbl == NULL` 또는 `n == 0`(`CMD_ERR_BADTABLE`), `fn == NULL`인 행(`CMD_ERR_NULL_FN`), `name == NULL`인 행, `min_args > max_args`인 행, 같은 `opcode`가 두 번 나오는 것.
8. `cmd_find(tbl, n, opcode)`는 행 포인터 또는 `NULL`을 돌려준다. 부수효과가 없다.
9. `cmd_dispatch`의 검사 순서는 정확히 이렇다. 찾기 → 없으면 `CMD_ERR_UNKNOWN` → `fn == NULL`이면 `CMD_ERR_NULL_FN` → `nargs`가 `min_args..max_args` 밖이면 `CMD_ERR_NARGS` → 그제서야 핸들러 호출.
10. 거부된 명령은 `ctx`를 **한 바이트도** 바꾸지 않는다. 핸들러 호출 직전까지 부수효과가 없어야 재전송 로직이 단순해진다.
11. `cmd_index_build(tbl, n, index)`는 `opcode`(0~255)에서 행 번호로 가는 역인덱스를 만든다. 없는 opcode는 `CMD_INDEX_NONE`(-1)이고, 중복 opcode를 만나면 실패로 처리한다.
12. `cmd_dispatch_indexed`는 배열 조회 한 번으로 행을 찾는다. `slot >= n`이면 `CMD_ERR_BADTABLE`이다(인덱스와 테이블이 어긋난 상황).
13. `cmd_dispatch`와 `cmd_dispatch_indexed`는 모든 입력에서 **같은 반환값과 같은 상태 변화**를 만든다. 최적화가 동작을 바꾸지 않았음을 테스트로 보인다.

## 인터페이스

```c
#include <stddef.h>
#include <stdint.h>

typedef int (*cmd_fn)(const uint8_t *args, size_t nargs, void *ctx);

typedef struct {
    uint8_t     opcode;
    const char *name;             /* 로그용. NULL이면 검증에서 걸린다 */
    cmd_fn      fn;
    uint8_t     min_args, max_args;  /* 둘 다 포함 */
} cmd_entry_t;

#define CMD_ERR_UNKNOWN (-16)   /* 디스패처 오류: -16 이하 */
#define CMD_ERR_NARGS   (-17)
#define CMD_ERR_NULL_FN (-18)
#define CMD_ERR_BADTABLE (-19)
#define APP_ERR_RANGE (-1)      /* 핸들러 오류: -1 ~ -15 */
#define APP_ERR_STATE (-2)

typedef struct {
    uint8_t  gain;              /* 0~64 */
    uint8_t  muted;             /* 0/1  */
    uint16_t sample_rate_hz10;  /* 10Hz 단위. 1600 = 16kHz */
    uint32_t reset_count, cmd_count;
} mic_ctx_t;

int cmd_table_validate(const cmd_entry_t *tbl, size_t n);
const cmd_entry_t *cmd_find(const cmd_entry_t *tbl, size_t n, uint8_t op);
int cmd_dispatch(const cmd_entry_t *tbl, size_t n, uint8_t opcode,
                 const uint8_t *args, size_t nargs, void *ctx);

#define CMD_INDEX_SIZE 256u
#define CMD_INDEX_NONE ((int16_t)-1)
int cmd_index_build(const cmd_entry_t *tbl, size_t n, int16_t *index);
int cmd_dispatch_indexed(const cmd_entry_t *tbl, size_t n,
                         const int16_t *index, uint8_t opcode,
                         const uint8_t *args, size_t nargs, void *ctx);
```

## 제약

- 동적 할당 금지. 테이블은 `static const`, 인덱스는 `static` 배열이다. 핸들러 안에서 전역 변수 접근 금지 — 상태는 `ctx`로만 들어온다.
- `switch` 문으로 opcode를 분기하는 풀이는 인정하지 않는다. 테이블이 이 문제의 요지다. 핸들러 코드를 서로 복사해 붙이지 않고, 각 핸들러는 10줄 안쪽이어야 한다.
- 함수 포인터를 `void *`로 캐스팅하지 않는다. C에서 함수 포인터와 객체 포인터의 변환은 표준이 보장하지 않는다.
- 하드웨어 헤더 금지. C11 표준 라이브러리만.
- `cc -std=c11 -Wall -Wextra -O2 -g`에서 경고 0개.

## 예시 동작

테이블은 이렇게 생겼다. 행 여섯 개가 프로토콜 전부다.
```
  opcode  name      fn          min max │ opcode  name      fn          min max
   0x01   PING      h_ping       0   0  │  0x20   SET_RATE  h_set_rate   2   2
   0x10   SET_GAIN  h_set_gain   1   1  │  0x30   MUTE      h_mute       1   1
   0x11   GET_GAIN  h_get_gain   0   0  │  0xF0   RESET     h_reset      0   1
```

디스패치가 거치는 관문은 셋이다.
```
   [opcode][len][payload...]
   cmd_find(opcode)  --없음--->  CMD_ERR_UNKNOWN (-16)
   fn == NULL ?      --그렇다->  CMD_ERR_NULL_FN (-18)
   min<=nargs<=max ? --아니다->  CMD_ERR_NARGS   (-17)
   fn(args, nargs, ctx)  ----->  0 이상 성공 / APP_ERR_* (-1, -2)
                                 ^ 여기까지 와야 ctx가 바뀐다
```

프레임 스트림을 돌리면 이렇게 된다. `ctx`는 `gain=16, rate=1600, muted=0`으로 시작한다.

```
  01 00          PING        -> 0     cmd_count 1
  10 01 14       SET_GAIN 20 -> 0     gain 20
  20 02 06 40    SET_RATE    -> 0     rate 1600 (16kHz)
  30 01 01       MUTE 1      -> 0     muted 1
  77 00          (unknown)   -> -16   상태 변화 없음
  10 02 01 02    SET_GAIN    -> -17   nargs=2, 상태 변화 없음
  11 00          GET_GAIN    -> 20    반환값이 곧 gain, cmd_count 5
```

같은 테이블, 다른 `ctx`. 핸들러 코드는 한 벌이다.
```
  cmd_dispatch(tbl, n, SET_GAIN, "\x0A", 1, &left)   -> left.gain  = 10
  cmd_dispatch(tbl, n, SET_GAIN, "\x32", 1, &right)  -> right.gain = 50
  cmd_dispatch(tbl, n, RESET,    NULL,   0, &left)   -> left.gain  = 16
                                                        right.gain = 50 그대로
```

검증이 잡아야 하는 나쁜 테이블 네 종류다.
```
  {0x02, "B",  NULL,   0, 0}  -> CMD_ERR_NULL_FN   주소 0으로 점프할 행
  {0x01, NULL, h_ping, 0, 0}  -> CMD_ERR_BADTABLE  로그가 죽는다
  {0x01, "A",  h_ping, 3, 1}  -> CMD_ERR_BADTABLE  min > max, 영원히 거부
  {0x10, ...}, {0x10, ...}    -> CMD_ERR_BADTABLE  중복. 뒤의 행이 죽은 코드
```

## 스스로 점검할 질문

1. `switch`와 테이블의 차이를 code size, 속도, 유지보수 세 축으로 각각 말하라. 컴파일러가 `switch`를 점프 테이블로 바꿔 주면 속도 차이는 어떻게 되는가?
2. 테이블을 `const`로 두면 무엇이 달라지는가? 어느 섹션에 놓이고, RAM을 얼마나 쓰는가?
3. `void *ctx` 대신 전역 변수를 쓰면 마이크가 두 개가 될 때 정확히 어디가 깨지는가?
4. 함수 포인터가 NULL인 행을 호출하면 Cortex-M에서 무슨 일이 생기는가? 스택 프레임에 어떤 단서가 남는가?
5. 중복 opcode는 왜 특히 나쁜 종류의 버그인가? 컴파일러나 링커가 잡아 줄 수 있는가?
6. 역인덱스 256바이트를 쓰는 대신 선형 탐색을 유지하는 판단 기준은 무엇인가? 명령이 몇 개일 때 갈리는가?
7. 핸들러가 오래 걸리면 어떻게 되는가? 디스패처가 ISR 문맥에서 돌고 있다면 설계를 어떻게 바꾸는가?
8. `cmd_dispatch`가 인자 개수만 검사하고 값 범위는 핸들러에 맡기는 이유는 무엇인가? 반대로 하면 무엇이 나빠지는가?
9. 이 테이블을 펌웨어 업데이트로 확장할 수 있게 만들려면 무엇이 필요한가? 함수 포인터를 flash에서 읽는 것의 위험은?

## follow-up (면접관이 이어서 물을 것)

1. "Some handlers need to send a reply. How does that change the signature?"
2. "One opcode is privileged — it needs an unlock first. Where does that check live?"
3. "How do you unit-test a handler without the dispatcher?"
4. "Two opcodes share 90% of their handler. How do you factor that without a `switch` reappearing inside?"
5. "This table is in flash. A bit flips. What do you do about it?"

---

**해설**: [notes/L1_memory_pointers.md](../drills/L1_memory_pointers.html) §10 · **답안**: `solutions/13_func_table.c`
