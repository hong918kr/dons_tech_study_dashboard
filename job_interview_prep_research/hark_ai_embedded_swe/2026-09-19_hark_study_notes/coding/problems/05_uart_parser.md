# 05. UART 패킷 파서 FSM + 체크섬

> **주제**: 통신 프레이밍 · 상태기계 · 에러 복구 · **난이도**: 중급 · **목표 시간**: 35분
> **해설은 보지 말 것**: 먼저 `starters/05_uart_parser.c`를 채워 `make run N=05`로 통과시킨다.

## 면접관의 문장

"Our SoC talks to the MCU over a UART at 115200 baud. The RX interrupt pushes one byte at a time into a ring buffer, and a low-priority task drains it. I want you to write the parser that sits on the other end of that ring buffer. The protocol is framed: a start byte, a length, a command, the payload, and a two-byte CRC. Write it as a state machine that takes one byte per call, with no dynamic allocation and no blocking, because it also has to be callable straight from the ISR if we decide to move it there. Assume the line is noisy: you will see garbage, you will see frames that get cut off halfway when the SoC reboots, and you will see a corrupted length field. Tell me what your parser does in each of those cases, and make sure the field can tell them apart from the logs."

## 요구사항

1. 프레임 형식은 `| SOF | LEN | CMD | PAYLOAD[LEN] | CRC_H | CRC_L |`이다. `SOF`는 `0xA5`다.
2. `LEN`은 payload 바이트 수이고 `0` 이상 `64` 이하다. `64`는 허용, `65` 이상은 즉시 폐기한다.
3. CRC는 CRC-16/CCITT-FALSE(poly `0x1021`, init `0xFFFF`, 입출력 반사 없음, xorout `0`)이며 `LEN`, `CMD`, `PAYLOAD`를 덮는다. `SOF`와 CRC 자신은 포함하지 않는다. 전송은 big-endian(`CRC_H` 먼저)이다.
4. `parser_feed()`는 바이트 하나를 받아, 그 바이트로 프레임이 완성되고 CRC까지 맞으면 `true`를 반환한다. 그 외에는 항상 `false`다.
5. `true`를 반환한 직후에만 `p->pkt`가 유효하다. 호출자가 곧바로 쓰지 않으면 다음 프레임이 덮어쓴다.
6. `LEN`이 한계를 넘으면 `payload` 배열에 단 한 바이트도 쓰지 않은 채 프레임을 버리고 `err_len`을 1 올린다.
7. CRC가 맞지 않으면 패킷을 버리고 `err_crc`를 1 올린 뒤 SOF 사냥 상태로 돌아간다.
8. 프레임 도중(SOF 사냥 상태가 아닐 때) 직전 바이트와의 간격이 `PKT_TIMEOUT_MS`(50 ms)를 **넘으면** 반쯤 받은 프레임을 버리고 `err_timeout`을 1 올린다. 간격이 정확히 50 ms면 타임아웃이 아니다.
9. 프레임 밖(SOF 사냥 상태)에서는 아무리 오래 침묵해도 타임아웃으로 세지 않는다. idle 링크에서 카운터가 증가하면 안 된다.
10. `LEN` 자리에 들어온 바이트가 한계를 넘으면서 동시에 `SOF`와 같다면, 앞의 `SOF`가 noise였고 이 바이트가 진짜 프레임 시작일 수 있다. 그 자리에서 재동기화한다(그래도 `err_len`은 1 올린다).
11. payload 안에 `0xA5`가 들어 있어도 정상 파싱되어야 한다. 프레이밍은 길이 기반이다.
12. `LEN == 0`인 프레임(명령만 있는 프레임)도 정상 프레임이다.
13. 에러 카운터 세 개는 서로 독립적이어야 한다. 한 번의 실패가 두 카운터를 동시에 올리지 않는다.
14. tick은 free-running `uint32_t`다. `2^32`에서 wrap할 때 가짜 타임아웃이 생기면 안 된다.

## 인터페이스

```c
#include <stdbool.h>
#include <stdint.h>

#define PKT_SOF          0xA5u
#define PKT_MAX_PAYLOAD  64u
#define PKT_TIMEOUT_MS   50u

typedef enum {
    ST_SOF, ST_LEN, ST_CMD, ST_PAYLOAD, ST_CRC_H, ST_CRC_L
} pstate_t;

typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t payload[PKT_MAX_PAYLOAD];
} packet_t;

typedef struct {
    pstate_t st;
    packet_t pkt;
    uint8_t  pos;
    uint16_t crc;
    uint16_t rx_crc;
    uint32_t last_ms;
    uint32_t err_crc, err_len, err_timeout;
} parser_t;

void parser_init(parser_t *p);
bool parser_feed(parser_t *p, uint8_t b, uint32_t now_ms);
```

CRC 헬퍼는 starter에 이미 주어져 있다. 이 문제의 초점은 상태기계다.

```c
static uint16_t crc16_byte(uint16_t crc, uint8_t b);   /* 주어짐 */
```

## 제약

- 동적 할당 금지(`malloc`, `calloc`, VLA 모두). 모든 상태는 `parser_t` 안에 있다.
- 블로킹 금지. 바이트당 작업은 상수 시간이어야 한다.
- 재진입 가능할 필요는 없지만, 하나의 `parser_t`는 한 문맥에서만 호출한다고 가정한다.
- 하드웨어·RTOS 헤더 금지. tick은 호출자가 인자로 넘긴다.
- C11 표준 라이브러리만. `cc -std=c11 -Wall -Wextra -O2`에서 경고 0개.
- `parser_t` 하나가 약 90바이트다. 이보다 크게 불리지 않는다.

## 예시 동작

정상 프레임 하나. `CMD = 0x10`, payload `01 02 03`.

```
바이트:  A5   03   10   01   02   03   85   48
상태:   SOF  LEN  CMD  PAY  PAY  PAY  CRCH CRCL
반환:    F    F    F    F    F    F    F    T   <- 마지막 바이트에서만 true
```

CRC 불일치. 마지막 바이트만 뒤집었다.

```
바이트:  A5   03   10   01   02   03   85   B7
반환:    F    F    F    F    F    F    F    F
결과:   err_crc = 1, 상태는 다시 SOF 사냥
```

LEN 초과. `0x41 = 65`는 한계 바로 위다.

```
바이트:  A5   41   00   00 ...
반환:    F    F    F    F
결과:   err_len = 1, payload에는 아무것도 쓰지 않는다
```

잘린 프레임 다음에 정상 프레임. 사이에 200 ms 침묵이 있다.

```
t=0..3 ms:   A5 03 10 01          <- SoC가 리부팅해 끊김
t=203 ms:    A5 03 10 01 02 03 85 48
결과:        err_timeout = 1, 그리고 두 번째 프레임은 정상 수신(true)
```

같은 바이트 열인데 침묵이 없다면(간격 1 ms) 파서는 두 프레임을 하나로 붙여 읽고 CRC에서 실패한다. `err_crc = 1`이 되고 패킷은 하나도 못 받는다. 타임아웃이 왜 필요한지 여기서 드러난다.

## 스스로 점검할 질문

1. `LEN` 검증을 `payload[pos++] = b` 뒤로 옮기면 어떤 입력에서 무슨 일이 일어나나? 몇 바이트를 넘어 쓰게 되나?
2. CRC를 `0xFFFF`로 리셋하는 지점이 `ST_SOF`인 이유는 무엇인가? `parser_init()`에서 한 번만 하면 왜 안 되나?
3. 타임아웃 비교를 `now_ms - p->last_ms`가 아니라 `now_ms > p->last_ms + PKT_TIMEOUT_MS`로 쓰면 tick wrap에서 무슨 일이 생기나?
4. 타임아웃 검사를 `ST_SOF`에서도 하면 어떤 카운터가 어떻게 오염되나?
5. payload에 `0xA5`가 들어와도 안전한 이유는 무엇이며, 그 대가로 무엇을 포기했나?
6. `p->pkt`를 호출자가 바로 복사하지 않고 포인터만 들고 있다가 나중에 읽으면 어떤 버그가 나나?
7. 이 함수를 그대로 UART RX ISR 안에서 부르면 무엇이 문제이고, 무엇을 바꿔야 하나?
8. 에러 카운터 세 개가 공장 라인에서 각각 어떤 원인을 가리키나?

## follow-up (면접관이 이어서 물을 것)

1. "payload에 SOF가 나오는 걸 아예 막으려면?"
2. "이걸 ISR에서 직접 부르고 싶다. 무엇을 바꾸겠나?"
3. "UART RX DMA를 쓰면 이 파서는 어떻게 달라지나?"
4. "프레임 손실을 상위 계층에서 복구하려면 프로토콜에 무엇을 더해야 하나?"
5. "CRC-16 대신 CRC-32나 서명을 써야 하는 경우는 언제인가?"
