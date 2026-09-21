# 05. UART 패킷 파서 FSM + 체크섬 — 해설

> **문제**: [problems/05_uart_parser.md](../problems/05_uart_parser.html) · **답안**: `solutions/05_uart_parser.c`
> **이 노트를 다 읽으면**: (1) 바이트 스트림에서 프레임 경계를 찾는 상태기계를 그림으로 설명할 수 있다. (2) garbage, 길이 오염, 잘린 프레임을 각각 어떤 방어선이 잡는지 말할 수 있다. (3) inter-byte timeout이 없으면 정확히 어떤 패킷이 사라지는지 바이트 단위로 보여줄 수 있다.

## 0. 한 문장으로

UART는 바이트만 주고 프레임 경계는 주지 않으므로, 파서는 **SOF를 사냥하다가 → 길이를 먼저 검증하고 → payload를 세면서 CRC를 누적하고 → 마지막에 비교**하는 6-상태 기계여야 하며, 실패하면 언제나 사냥 상태로 돌아가면서 원인별 카운터를 올린다.

---

## 1. 왜 이 패턴이 필요한가 — 하드웨어에서 출발

UART가 하드웨어로 알려주는 경계는 **바이트 경계**뿐이다. "여기서 메시지가 시작한다"는 개념이 없어서 프레이밍은 100% 소프트웨어의 몫이다.

```
 SoC (Linux)                MCU (Cortex-M)
 +-------------+  TX -->  +------------------------------------------+
 | app / test  |          | UART periph -- RXNE IRQ, 바이트 1개        |
 | station     |  <-- RX  |   v                                      |
 +-------------+          | rx_isr(): ring_push(b, now_ms)  (Q01 링) |
                          |   v                                      |
                          | task: while (ring_pop(&b, &t))           |
                          |         if (parser_feed(p,b,t)) dispatch();|
                          +------------------------------------------+
```

factory test station-DUT 명령 채널, SoC-MCU IPC, BLE HCI UART transport(H4), 센서 모듈 프로토콜이 전부 이 모양이다. 요구 성질도 같다. **바이트당 O(1)**(115200 baud면 86 us마다 한 바이트), **동적 할당 없음**, **블로킹 없음**, **노이즈에서 자가 복구**.

```
        +------+------+------+-------------+-------+-------+
        | 0xA5 | LEN  | CMD  | PAYLOAD[LEN]| CRC_H | CRC_L |
        +------+------+------+-------------+-------+-------+
           ^      \________________________/          ^
        경계 후보     CRC 범위 (SOF는 제외)        big-endian
```

가장 중요한 한 가지는 **`0xA5`는 "프레임 시작일 수도 있는 바이트"이지 "프레임 시작"이 아니라는 것**이다. payload 안에도 `0xA5`는 얼마든지 나온다. 진짜 경계는 `SOF + 올바른 LEN + 맞는 CRC` 셋이 동시에 성립할 때 사후적으로 확정된다. CRC를 버퍼에 다 모은 뒤 한 번에 계산하지 않고 바이트마다 누적하는 이유도 같은 맥락이다. 64바이트 payload를 일괄 계산하면 마지막 바이트 처리에서 512회 shift 루프를 몰아서 돈다. 스트리밍은 그 비용을 고르게 분산한다. 실시간 시스템에서는 평균보다 **최악 지터**가 중요하다.

---

## 2. 먼저 그림으로 이해하기

### 2.1 전체 상태 다이어그램

```
                       T1: b != 0xA5 -> 조용히 버림, 카운터 안 올림
                              +----+
                              v    |
        +----------------+         |
   ---> |     ST_SOF     |---------+       "SOF 사냥 중"
        +----------------+
            | T2: b == 0xA5 : crc = 0xFFFF      <-- 프레임 시작 = CRC 리셋
            v
        +----------------+ <-----------------+
        |     ST_LEN     |                   | T5: b > 64 이면서 b == 0xA5
        +----------------+ ------------------+     (앞의 A5가 noise였다)
            |          |  T4: b > 64, b != 0xA5   err_len++, 제자리 재동기화
            |          +--> err_len++, ST_SOF
            | T3: b <= 64 : len = b, crc += b
            v
        +----------------+
        |     ST_CMD     |   cmd = b, crc += b, pos = 0
        +----------------+
            |          +--> T7: len == 0 --------------+
            | T6: len > 0                              |
            v                                          |
        +----------------+ <--+ T8: pos < len          |
        |   ST_PAYLOAD   |    |  payload[pos++] = b    |
        +----------------+ ---+  crc += b              |
            | T9: pos == len                           |
            v                                          |
        +----------------+ <---------------------------+
        |    ST_CRC_H    |   T10: rx_crc = b << 8
        +----------------+
            v
        +----------------+   rx_crc |= b
        |    ST_CRC_L    |   T11: rx_crc == crc --> return true, ST_SOF
        +----------------+   T12: rx_crc != crc --> err_crc++,   ST_SOF

  [T0: 모든 상태 공통 진입 가드 — switch보다 먼저 실행]
        st != ST_SOF && (now_ms - last_ms) > 50  ==>  err_timeout++, st = ST_SOF
        그 뒤 이번 바이트를 새 상태(ST_SOF)에서 다시 해석한다.
```

눈여겨볼 점 셋이다. **모든 탈출구가 `ST_SOF`로 간다** — 에러 처리 경로가 따로 없고 "모르면 처음부터"가 복구 전략의 전부다. **타임아웃은 상태가 아니라 모든 전이 앞의 가드**라 어느 상태에서 끊기든 같은 방식으로 회수된다. **`ST_LEN`만 자기 루프를 가진다** — 이게 mid-frame 재동기화다.

### 2.2 `parser_t` 내부가 변하는 모습

정상 프레임 `A5 03 10 01 02 03 85 48`을 먹일 때, payload 배열과 커서를 실제로 그린 것이다.

```
 init      st=ST_SOF   pos=0  crc=FFFF  len=0  payload [ .. .. .. .. ]
 byte A5   st=ST_LEN          crc=FFFF                 [ .. .. .. .. ]  <- 리셋
 byte 03   st=ST_CMD          crc=D193  len=3          [ .. .. .. .. ]
 byte 10   st=ST_PAY   pos=0  crc=5A6D  cmd=10         [ .. .. .. .. ]  ^pos=0
 byte 01   st=ST_PAY   pos=1  crc=869E                 [ 01 .. .. .. ]     ^pos
 byte 02   st=ST_PAY   pos=2  crc=4F0C                 [ 01 02 .. .. ]        ^pos
 byte 03   st=ST_CRC_H pos=3  crc=8548  (pos==len)     [ 01 02 03 .. ]           ^pos
 byte 85   st=ST_CRC_L        rx_crc=8500
 byte 48                      rx_crc=8548 == crc 8548  -->  return true
```

CRC 누산기만 따로 보면 `FFFF →(03) D193 →(10) 5A6D →(01) 869E →(02) 4F0C →(03) 8548`이다. `85 48`이 바로 이 값이고 SOF는 들어가지 않는다.

---

## 3. 잘못된 구현부터 보기

순진한 버전에는 셋이 없다. **LEN 검증**, **타임아웃**, **에러 카운터**.

```c
case ST_LEN:     p->pkt.len = b; p->st = ST_CMD; break;  /* 검증 없음! */
case ST_PAYLOAD: p->pkt.payload[p->pos++] = b;           /* overflow */
                 if (p->pos == p->pkt.len) p->st = ST_CRC_H;
                 break;
```

### 3.1 깨지는 장면 1 — 길이 한 바이트가 뒤집히면

노이즈로 `LEN = 0x03`이 `0xFF`로 바뀌었다고 하자.

```
 A5  FF  10  xx xx xx ...        len = 255 로 그냥 받아들임

 parser_t:  payload [ 0 .. 63 ] | cmd,len | pos, crc, rx_crc, last_ms, 카운터
            +------- 64 B -----+-- 2 B --+------------ ~20 B ------------+
                                ^ 65번째 payload 바이트부터 여기를 덮어쓴다

 pos가 오염되면 종료 조건이 영영 안 맞고 파서가 완전히 미친다.
 parser_t가 지역 변수라면 스택 리턴 주소까지 간다.
```

**교훈**: 길이는 버퍼를 건드리기 **전에** 검증한다. 성능 문제가 아니라 보안 문제다. `LEN=255` 프레임이 첫 번째 테스트 케이스여야 한다.

### 3.2 깨지는 장면 2 — 타임아웃이 없으면

SoC가 프레임 중간에 리부팅해 앞 4바이트만 왔다고 하자. 타임아웃이 없으면 파서는 `ST_PAYLOAD`에 그대로 앉아 있다가, 뒤이어 오는 **정상 프레임의 SOF와 LEN을 남은 payload로 먹어 버린다**. 그 결과 멀쩡한 프레임 하나가 통째로 사라지고 로그에는 "CRC 에러"만 남는다(바이트 단위 추적은 §6.5). 현장에서 제일 짜증나는 증상이다. **진짜 원인은 송신 측이 끊긴 것인데 로그는 CRC 에러를 가리킨다.** 원인과 증상이 어긋나므로 케이블을 의심하며 며칠을 태운다. 타임아웃은 성능 최적화가 아니라 **오진 방지 장치**이고, 덤으로 패킷 하나를 실제로 더 건진다(§6.5 참조).

### 3.3 깨지는 장면 3 — 카운터가 하나뿐이면

`err_count` 하나만 두면 "DUT 1234에서 통신 에러 17건"이라는 쓸모없는 티켓이 올라온다. 셋으로 나누면 바로 분류된다.

| 관측 | 유력한 원인 | 다음 행동 |
|---|---|---|
| `err_crc`만 증가 | 전기적 노이즈, 접지, baud 오차 | 케이블/커넥터, 클럭 트림 확인 |
| `err_len`만 증가 | 송신 측 프레이밍 버그, 엉뚱한 프로토콜 | 상대 펌웨어 버전 확인 |
| `err_timeout`만 증가 | 상대가 리셋/행, 상대 TX 고갈 | 상대 로그, 전원 glitch 확인 |
| 셋 다 증가 | baud 불일치, 링크가 통째로 깨짐 | baud, 전압 레벨(3.3 vs 1.8) 확인 |

---

## 4. 한 줄씩 만들기

### 단계 1 — 사냥 상태와 SOF

`case ST_SOF:` 는 `if (b == PKT_SOF) { p->crc = 0xFFFFu; p->st = ST_LEN; }` 한 줄이 전부다.

- `if`가 거짓일 때 **아무것도 안 하고 `break`**하는 것이 핵심이다. 이 조용한 폐기가 garbage 복구의 전부이고, 카운터도 올리지 않는다. idle 링크의 노이즈로 로그를 채우면 안 된다.
- `crc = 0xFFFF`가 여기 있는 이유: CRC는 프레임 단위다. `parser_init()`에서 한 번만 리셋하면 두 번째 프레임부터 이전 프레임의 잔재가 섞인다. **이 줄이 없으면** 첫 프레임만 성공하고 그 뒤로 전부 CRC 에러다. 전형적인 "처음엔 되는데 조금 있다 죽어요" 버그다.

### 단계 2 — 길이 검증 (가장 중요한 줄)

```c
case ST_LEN:
    if (b > PKT_MAX_PAYLOAD) {
        p->err_len++;
        p->st = (b == PKT_SOF) ? ST_LEN : ST_SOF;
        break;
    }
    p->pkt.len = b;
    p->crc     = crc16_byte(p->crc, b);
    p->st      = ST_CMD;
    break;
```

- `b > PKT_MAX_PAYLOAD`가 유일한 overflow 방어선이다. `ST_PAYLOAD`에는 범위 검사가 없고 있을 필요도 없다. 불변식 `pos < len <= 64`가 여기서 확립되기 때문이다. **이 줄이 없으면** §3.1의 메모리 파괴가 일어난다.
- `(b == PKT_SOF) ? ST_LEN : ST_SOF`가 mid-frame 재동기화다. `0xA5 = 165`는 언제나 64보다 크므로 LEN 자리에 SOF가 오면 반드시 이 분기를 탄다. 앞의 `0xA5`가 noise였고 지금 이 바이트가 진짜 시작일 가능성이 높으니, `ST_SOF`로 갔다가 이 바이트를 버리는 대신 제자리에서 `ST_LEN`을 유지해 다음 바이트를 LEN으로 읽는다.
- 이 경로에서 `crc`를 다시 리셋하지 않아도 되는 이유: `ST_LEN` 진입 시 이미 `0xFFFF`였고 이 분기는 `crc16_byte()`를 부르지 않았다.

### 단계 3 — payload 수집과 LEN 0 처리

```c
case ST_CMD:      /* cmd 저장, crc 누적, pos = 0 */
    p->st = (p->pkt.len == 0u) ? ST_CRC_H : ST_PAYLOAD;
    break;
case ST_PAYLOAD:
    p->pkt.payload[p->pos++] = b;
    p->crc = crc16_byte(p->crc, b);
    if (p->pos == p->pkt.len) { p->st = ST_CRC_H; }
    break;
```

- `len == 0` 삼항 연산자가 **없으면** `LEN=0` 프레임에서 `ST_PAYLOAD`에 들어가고, `pos == len` 비교가 쓰기 **뒤**에 있으므로 `pos=1 != 0`이 되어 영원히 못 빠져나온다. 그 프레임뿐 아니라 이후 스트림 전체가 망가진다. ping이나 reset 같은 명령 전용 프레임이 링크를 영구히 죽인다.
- `pos == len`을 `>=`가 아니라 `==`로 써도 안전하다. `pos`는 1씩 늘고 `len`을 넘는 순간이 없다.
- `pos = 0`을 `ST_CMD`에서 하는 것은 "payload 커서는 payload 직전에 리셋"이라는 읽기 쉬운 배치 때문이다.

### 단계 4 — CRC 비교와 복귀

`ST_CRC_H`는 `p->rx_crc = (uint16_t)((uint16_t)b << 8); p->st = ST_CRC_L;` 두 줄이고, 판정은 다음 바이트에서 난다.

```c
case ST_CRC_L:
    p->rx_crc = (uint16_t)(p->rx_crc | b);
    p->st     = ST_SOF;
    if (p->rx_crc == p->crc) { return true; }
    p->err_crc++;
    break;
```

- `p->st = ST_SOF;`를 비교보다 **먼저** 한다. 성공이든 실패든 같은 곳으로 가므로, 두 경로에 나눠 쓰면 한쪽을 빠뜨리기 쉽다. **성공 경로에만 두면** CRC가 한 번 틀렸을 때 파서가 `ST_CRC_L`에 갇혀 이후 모든 바이트를 CRC 하위 바이트로 해석한다.
- 캐스트를 두 번 하는 이유는 integer promotion이다. `b << 8`은 `int`가 되고 `-Wconversion` 프로젝트에서 경고가 난다.
- CRC는 big-endian으로 온다. 리틀엔디안 MCU에서 `memcpy`로 `uint16_t`에 밀어 넣으면 뒤집힌다. shift로 명시 조립하는 게 프로토콜 코드의 기본 규율이다.

### 단계 5 — inter-byte timeout 가드

```c
if (p->st != ST_SOF && (uint32_t)(now_ms - p->last_ms) > PKT_TIMEOUT_MS) {
    p->err_timeout++;
    p->st = ST_SOF;
}
p->last_ms = now_ms;
```

- `p->st != ST_SOF` 가드가 없으면 idle 링크에서 노이즈 바이트마다 `err_timeout`이 올라가 카운터가 쓸모없어진다. 프레임 밖에는 지킬 마감이 없다.
- `(uint32_t)(now_ms - p->last_ms)`는 **unsigned 뺄셈**이다. `now=0x00000002`, `last=0xFFFFFFFE`면 차이가 정확히 `4`로 나온다. `now_ms > p->last_ms + PKT_TIMEOUT_MS`로 쓰면 `last + 50`이 wrap해 비교가 뒤집힌다. 49.7일에 한 번 나는 버그는 절대 재현 못 한다.
- 비교가 `>`이므로 정확히 50 ms는 통과한다. 경계는 스펙에 못 박고 테스트로 고정한다.
- 이 블록이 switch **앞**에 있다는 게 결정적이다. 타임아웃으로 `ST_SOF`가 된 뒤 이번 바이트가 곧바로 `ST_SOF` 케이스에서 해석되므로, "타임아웃을 유발한 바로 그 바이트가 새 프레임의 SOF"인 경우를 한 번에 처리한다. switch 뒤에 뒀다면 그 SOF를 놓치고 프레임 하나를 더 잃는다.

---

## 5. 전체 코드 읽기 — 전이와 코드의 대응

`solutions/05_uart_parser.c`의 `parser_feed()`는 위 다섯 조각을 순서대로 이어 붙인 것이다. 각 전이(§2.1의 T 번호)가 어느 줄인지 표로 못 박는다.

| # | 현재 상태 | 입력 조건 | 다음 상태 | 코드에서 이 줄 | 부수 효과 |
|---|---|---|---|---|---|
| T0 | `!= ST_SOF` | `now - last > 50` | `ST_SOF` | `if (p->st != ST_SOF && ...)` (switch 앞) | `err_timeout++` |
| T1 | `ST_SOF` | `b != 0xA5` | `ST_SOF` | `if (b == PKT_SOF)`의 암묵적 else | 없음. 조용히 폐기 |
| T2 | `ST_SOF` | `b == 0xA5` | `ST_LEN` | `p->crc = 0xFFFFu; p->st = ST_LEN;` | CRC 리셋 |
| T3 | `ST_LEN` | `b <= 64` | `ST_CMD` | `p->pkt.len = b; ... p->st = ST_CMD;` | `len` 확정, CRC 누적 |
| T4 | `ST_LEN` | `b > 64`, `b != 0xA5` | `ST_SOF` | `p->st = (b == PKT_SOF) ? ST_LEN : ST_SOF;` 거짓 가지 | `err_len++` |
| T5 | `ST_LEN` | `b > 64`, `b == 0xA5` | `ST_LEN` | 같은 삼항의 참 가지 | `err_len++`, 제자리 재동기화 |
| T6 | `ST_CMD` | `len > 0` | `ST_PAYLOAD` | `p->st = (p->pkt.len == 0u) ? ST_CRC_H : ST_PAYLOAD;` 거짓 가지 | `cmd` 확정, `pos = 0` |
| T7 | `ST_CMD` | `len == 0` | `ST_CRC_H` | 같은 삼항의 참 가지 | payload 단계 생략 |
| T8 | `ST_PAYLOAD` | `pos+1 < len` | `ST_PAYLOAD` | `if (p->pos == p->pkt.len)`가 거짓 | payload 한 칸 채움 |
| T9 | `ST_PAYLOAD` | `pos+1 == len` | `ST_CRC_H` | 같은 `if`가 참 | 마지막 payload 바이트 |
| T10 | `ST_CRC_H` | 항상 | `ST_CRC_L` | `p->rx_crc = (uint16_t)(b << 8);` | 상위 바이트 저장 |
| T11 | `ST_CRC_L` | `rx_crc == crc` | `ST_SOF` | `if (p->rx_crc == p->crc) return true;` | **패킷 완성, true 반환** |
| T12 | `ST_CRC_L` | `rx_crc != crc` | `ST_SOF` | `p->err_crc++;` | 프레임 폐기 |

불변식 셋이 항상 성립한다. `p->pkt.len <= PKT_MAX_PAYLOAD`(T3에서만 `len`이 바뀌고 그 경로는 `b <= 64`를 통과했다), `p->pos <= p->pkt.len`(T6에서 0으로 시작해 T8/T9에서 1씩 늘며 `len`에서 멈춘다), 그리고 `st != ST_SOF`이면 직전 바이트로부터 50 ms 이내다(T0가 보장한다). 비용은 바이트당 분기 몇 개 + `crc16_byte()`의 8회 루프이고, 복사는 0회다(payload는 수신 즉시 최종 위치에 들어간다). `parser_t`는 32-bit ARM에서 약 92 B다. 링크가 더 빠르면 `crc16_byte()`를 256-entry 테이블(flash 512 B)로 바꿔 바이트당 lookup 한 번으로 줄이거나 MCU의 하드웨어 CRC 유닛을 쓴다.

---

## 6. 실패 모드별 바이트 트레이스

프레임 상수는 전부 실제 값이며 `frame_build()`가 만드는 것과 같다.

### 6.1 정상 경로 — 프레임 `CMD=0x10`, payload `01 02 03`

| t(ms) | byte | 진입 상태 | 조건 | 나가는 상태 | crc | 반환 |
|---|---|---|---|---|---|---|
| 1000 | `A5` | ST_SOF | SOF | ST_LEN | FFFF | false |
| 1001 | `03` | ST_LEN | 3 <= 64 (T3) | ST_CMD | D193 | false |
| 1002 | `10` | ST_CMD | len>0 (T6) | ST_PAYLOAD | 5A6D | false |
| 1003 | `01` | ST_PAYLOAD | pos 0→1 (T8) | ST_PAYLOAD | 869E | false |
| 1004 | `02` | ST_PAYLOAD | pos 1→2 (T8) | ST_PAYLOAD | 4F0C | false |
| 1005 | `03` | ST_PAYLOAD | pos 2→3 == len (T9) | ST_CRC_H | 8548 | false |
| 1006 | `85` | ST_CRC_H | — (T10) | ST_CRC_L | 8548 | false |
| 1007 | `48` | ST_CRC_L | rx=8548 == 8548 (T11) | ST_SOF | 8548 | **true** |

카운터는 전부 0이다. `test_happy_path()`가 "마지막 바이트에서만 `true`"까지 검사한다.

### 6.2 체크섬 불일치

프레임 `A5 02 31 DE AD C9 52`에서 마지막 바이트를 `52 ^ FF = AD`로 손상시켰다.

```
  byte  A5      02      31      DE      AD      C9      AD
  state SOF     LEN     CMD     PAY     PAY     CRC_H   CRC_L
  act   crc=    len=2   cmd=31  pl[0]   pl[1]   rx_crc  rx_crc=C9AD
        FFFF    C1B2    5D1F    BEEB    C952    =C900   계산 C952 -> 불일치
  ret   F       F       F       F       F       F       F + err_crc++
                                                          st = ST_SOF
```

핵심은 **CRC 실패 후에도 파서가 멀쩡하다**는 것이다. `st = ST_SOF`, 카운터만 하나 늘었고, 바로 뒤의 정상 프레임은 그대로 수신된다(`test_bad_crc()`가 확인). 다만 CRC가 틀렸을 때도 payload는 이미 버퍼에 써 놓은 상태다. 상위 계층은 `parser_feed()`가 `true`를 준 경우에만 `p->pkt`를 읽어야 한다.

### 6.3 LEN 초과

```
 입력:  A5   41   00   00        (0x41 = 65, 한계 바로 위)
  A5  -> ST_SOF: SOF 맞음, crc=FFFF, st=ST_LEN
  41  -> ST_LEN: 65 > 64 => err_len++ (=1), b != 0xA5 => ST_SOF (T4)
                 payload에는 아무것도 쓰지 않았다   <-- 핵심
  00 00 -> ST_SOF: SOF 아님, 전부 버림

 이어서:  A5 FF   (0xFF = 255, 전형적인 overflow 시도) -> err_len++ (=2), ST_SOF
 이어서:  A5 01 41 5A 3E EE  -> cmd=0x41, len=1, payload[0]=0x5A, true
          err_len은 2 그대로, err_crc = 0

 [검증 있음] payload 건드림 0, 다음 프레임 정상
 [검증 없음] payload 64칸을 넘겨 pos/crc/카운터까지 오염, 복구 불가
```

### 6.4 garbage 후 정상 프레임

**(a) SOF가 없는 쓰레기** — 공짜로 복구된다.

```
 00 FF 7E 12 34 99 | A5 02 55 AA BB 38 93
 \_______________/   전부 ST_SOF에서 "SOF 아님" -> 조용히 버림 (T1)
                     카운터 전부 0 유지 (중요: 로그를 오염시키지 않는다)
 A5 02 55 AA BB      crc=FFFF 리셋, len=2, cmd=0x55, payload={AA,BB}, crc=3893
 38 93               rx_crc=3893 == 3893  ->  true

 결과: 패킷 1개, err_crc = err_len = err_timeout = 0
```

**(b) 쓰레기 안에 `0xA5`가 있는 경우** — 한 번의 에러를 대가로 복구한다.

```
 12 A5 | A5 03 10 01 02 03 85 48
        ^^ 진짜 프레임의 SOF

 12  -> ST_SOF: 버림
 A5  -> ST_SOF: SOF로 오인! crc=FFFF, st=ST_LEN
 A5  -> ST_LEN: 165 > 64 => err_len++ (=1), b == 0xA5 => ST_LEN 유지
                (T5, 재동기화!) crc는 아직 FFFF라 손대지 않아도 된다
 03 10 01 02 03 -> len=3, cmd=10, payload={01,02,03}, crc=8548
 85 48          -> rx_crc=8548 == 8548  ->  true

 결과: 패킷 1개 살아남음, err_len=1
```

T5가 없었다면 두 번째 `A5`에서 `ST_SOF`로 돌아가고, 그 자리의 `A5`는 이미 소비된 뒤라 다음 SOF를 만날 때까지 프레임 전체를 잃는다. **한 줄의 삼항 연산자가 프레임 하나를 건진다.** `err_len`이 1 올라가는 것은 정직한 신호다. 정상적인 프레임 흐름이 아니었던 게 사실이기 때문이다. `test_resync_sof_in_len_slot()`이 이 동작을 고정한다.

### 6.5 잘린 프레임 후 정상 프레임

**(a) 침묵 없이 이어질 때** — 패킷을 잃는다.

```
 t:  0  1  2  3 | 4  5  6  7  8  9 10 11
     A5 03 10 01| A5 03 10 01 02 03 85 48

 t=0..3  ST_PAYLOAD, pos=1, payload={01}, crc=869E
 t=4  A5 -> 간격 1 ms라 타임아웃 아님. payload[1]=A5, pos=2, crc=8A01
 t=5  03 -> payload[2]=03, pos=3==len -> ST_CRC_H, crc=01A1
 t=6,7  10 01 -> rx_crc = 1001 != 01A1 -> err_crc++ (=1), st=ST_SOF
 t=8..11  02 03 85 48  전부 SOF가 아니라 버려진다

 결과: 패킷 0개, err_crc=1, err_timeout=0
```

`test_truncated_then_valid_no_timeout()`이 이 결과를 그대로 assert한다. 실패를 문서화해 둔 테스트다.

**(b) 200 ms 침묵이 있을 때** — 패킷을 건진다.

```
 t:  0  1  2  3 | ..... 200 ms 침묵 ..... | 204 205 ...  211
     A5 03 10 01|                         | A5  03  ...  48

 t=0..3   ST_PAYLOAD, pos=1
 t=204 A5 -> T0 진입 가드: 204 - 3 = 201 > 50 => err_timeout++ (=1), ST_SOF
             그리고 이 A5를 ST_SOF에서 해석 -> ST_LEN, crc=FFFF
 t=205..  03 10 01 02 03 85 48  -> 정상 파싱 -> true

 결과: 패킷 1개, err_timeout=1, err_crc=0
```

(a)와 (b)를 나란히 놓는 것이 이 문제의 하이라이트다. **입력 바이트 열은 완전히 동일하고 다른 것은 시간뿐인데**, 결과가 "패킷 0개 + CRC 에러"와 "패킷 1개 + 타임아웃"으로 갈린다. 타임아웃 훅이 하는 일이 정확히 이것이다.

### 6.6 타임아웃 경계와 idle 링크, payload 속 `0xA5`

간격이 정확히 50 ms면 `50 > 50`이 거짓이라 타임아웃이 아니고 프레임을 정상 수신한다. 51 ms면 매 바이트마다 타임아웃이 걸려 프레임을 하나도 못 받는다. 반면 `ST_SOF`에서 10초 간격으로 노이즈가 와도 T0 조건 자체가 거짓이라 `err_timeout`은 0을 유지한다. 실제 제품의 UART는 대부분의 시간 동안 idle이므로, `st != ST_SOF` 가드가 없으면 카운터가 무의미한 숫자로 가득 차 진짜 신호를 덮는다.

```
 프레임: A5 | 04 | A5 | A5 A5 00 A5 | E4 92    (CMD도 payload도 0xA5 투성이)
         SOF len  cmd   payload 4B     CRC

 파서는 len=4를 센 뒤 정확히 4바이트를 payload로 먹는다. 내부의 A5는
 "SOF일까?"를 아예 묻지 않는다. 상태가 ST_PAYLOAD이기 때문이다.
   장점: 길이 기반 프레이밍은 투명(transparent) 전송, 이스케이프 불필요.
   단점: 한 번 동기화를 잃으면 payload 속 A5가 가짜 SOF로 보인다.
         그래서 타임아웃과 CRC가 최종 방어선이다.
```

---

## 7. 동시성·메모리 관점

```
 [설계 A] ISR은 링버퍼에만 넣고 task가 파싱        <-- 이 답안의 가정
   UART IRQ: ring_push(b, tick)   task: while (ring_pop(&b,&t)) parser_feed(p,b,t);
   장점: ISR이 짧고 파서/디스패치가 preemptible
   단점: 링버퍼가 task 지연을 흡수할 만큼 커야 한다(overrun 주의)

 [설계 B] ISR 안에서 직접 parser_feed()
   장점: 버퍼 하나 절약
   단점: ISR에서 CRC 8-loop. 완성 패킷을 넘길 큐가 결국 또 필요하고,
         g_parser를 task가 읽으면 race가 생긴다
```

설계 B로 가려면 `parser_t`를 ISR 전용으로 두고, 완성된 패킷은 ISR 안에서 고정 슬롯 큐로 **복사**하며(포인터만 넘기면 다음 프레임이 덮어쓴다), 카운터 읽기는 자연 정렬된 `uint32_t` 단일 워드 접근으로 제한한다.

ISR이 `g_p.err_crc++`를 하는 동안 task가 `uint32_t e = g_p.err_crc;`를 루프에서 읽는다면, 컴파일러는 그 읽기를 루프 밖으로 끌어내 레지스터에 캐시할 수 있다. task는 영원히 갱신 안 된 값을 본다.

- 파서 내부 필드(`st`, `pos`, `crc`)는 **한 문맥에서만** 접근하므로 `volatile`이 필요 없다. 붙이면 매번 메모리 왕복이 생겨 느려지기만 한다.
- ISR과 task가 **둘 다 보는** 필드(에러 카운터, "패킷 준비됨" 플래그)만 `volatile`이 필요하고, C11이면 `_Atomic`이 더 정확한 도구다.
- `err_crc++`는 `volatile`이어도 원자적이지 않다(load-modify-store). 다만 이 파서는 **쓰기가 ISR 한 곳뿐**이고 task는 읽기만 하므로 손실이 없다. 이 비대칭이 안전성의 근거이고, 면접에서 이 문장을 정확히 말하는 게 점수다.
- tick은 **ISR이 바이트를 받는 순간의 값**을 바이트와 함께 큐에 넣어야 한다. task가 `parser_feed` 직전에 `HAL_GetTick()`을 부르면, task가 밀렸을 때 붙어 온 바이트들이 "간격이 벌어진" 것처럼 보여 가짜 타임아웃이 난다. 부하가 높을 때만 나는 버그다. 시그니처가 tick을 **인자로 받는** 이유가 이것이고, 덤으로 테스트가 시간을 주입할 수 있게 된다.

---

## 8. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `ST_LEN`에서 범위 검사 누락 | 랜덤 하드폴트, 카운터가 말도 안 되는 값 | `payload[pos++]`가 구조체 밖을 씀 | 검사를 저장보다 앞에 |
| `crc`를 `parser_init()`에서만 리셋 | 첫 프레임만 성공, 이후 전부 CRC 에러 | 이전 프레임 CRC 잔재 | `ST_SOF`에서 `0xFFFF`로 리셋 |
| `LEN == 0` 분기 누락 | ping 프레임 뒤로 링크가 영구히 멈춤 | `pos == len` 비교가 쓰기 뒤 | `ST_CMD`에서 `len==0`이면 `ST_CRC_H` |
| 타임아웃을 `now > last + 50`으로 | 49.7일마다 한 번 폭주, 재현 불가 | `last + 50`이 wrap | `(uint32_t)(now - last) > 50` |
| 타임아웃을 `ST_SOF`에서도 검사 | idle 링크에서 `err_timeout` 무한 증가 | 프레임 밖에는 마감이 없음 | `st != ST_SOF &&` 가드 |
| 타임아웃 검사를 switch 뒤에 | 침묵 후 첫 프레임을 항상 하나 잃음 | 새 SOF를 소비해 버림 | 검사를 switch 앞으로 |
| `st = ST_SOF`를 성공 경로에만 | CRC 한 번 틀리면 링크가 영영 죽음 | `ST_CRC_L`에 갇힘 | 비교 전에 `st = ST_SOF` |
| `p->pkt` 포인터를 나중에 읽음 | 가끔 엉뚱한 명령이 실행됨 | 다음 프레임이 덮어씀 | `true` 직후 복사, 또는 패킷 큐 |
| CRC를 `memcpy`로 조립 | 특정 값에서만 실패 | 엔디안 뒤집힘 | shift로 big-endian 명시 조립 |
| 에러 카운터를 하나로 합침 | 필드 이슈를 분류 못 함 | 원인별 정보 소실 | 셋으로 분리 |

---

## 9. 직접 확인하기

```
 make sol N=05     모범답안 빌드·실행 (ALL TESTS PASSED가 나와야 한다)
 make run N=05     내 구현 빌드·실행
```

출력에서 이 두 줄을 나란히 본다. 같은 바이트 열, 다른 시간 → 다른 결과. 이 문제의 핵심이다.

```
  ok  truncated+valid without timeout: packet LOST (err_crc=1)
  ok  truncated+valid with 200 ms gap: err_timeout=1, packet OK
```

- **실험 1 — 길이 검증 지우기.** `ST_LEN`의 `if (b > PKT_MAX_PAYLOAD)` 블록을 주석 처리한다. `test_oversize_length()`가 깨지고, 운이 나쁘면 assert 전에 이상한 곳에서 죽는다. `cc -std=c11 -Wall -Wextra -O1 -g -fsanitize=address solutions/05_uart_parser.c -o /tmp/asan05`로 다시 빌드하면 buffer-overflow가 몇 번째 줄인지 정확히 찍힌다.
- **실험 2 — 타임아웃 가드 지우기.** `st != ST_SOF &&`만 지운다. `test_timeout_only_inside_frame()`이 즉시 깨지고, idle 링크에서 카운터가 오염되는 것을 눈으로 확인할 수 있다.
- **실험 3 — 재동기화 지우기.** 삼항 연산자를 `p->st = ST_SOF;`로 바꾸면 `test_resync_sof_in_len_slot()`이 깨지고 §6.4(b)의 프레임이 통째로 사라진다. **실험 4 — 타임아웃 검사를 switch 뒤로 옮기기.** `test_truncated_then_valid_with_timeout()`이 깨진다. 타임아웃을 유발한 바이트가 곧 새 프레임의 SOF였다는 사실이 왜 중요한지 보인다.

---

## 10. 면접에서 말하기

1. "UART는 바이트 경계만 주고 프레임 경계는 안 줍니다. 프레이밍은 소프트웨어의 몫입니다."
2. "6-상태 기계입니다. SOF 사냥 → 길이 → 명령 → payload → CRC 상위 → CRC 하위."
3. "가장 중요한 줄은 길이 검증입니다. payload 배열을 건드리기 전에 검사해야 overflow가 없습니다."
4. "CRC는 스트리밍으로 누적합니다. 다 모은 뒤 계산하면 마지막 바이트에서 실행시간이 튑니다."
5. "실패는 세 종류이고 카운터가 각각 다릅니다. CRC 불일치는 전기적 노이즈, 길이 초과는 상대 펌웨어 버그, 타임아웃은 상대가 죽은 겁니다."
6. "타임아웃이 없으면 잘린 프레임이 다음 프레임을 먹어서, 원인은 리셋인데 로그에는 CRC 에러가 찍힙니다. 오진 방지가 타임아웃의 진짜 가치입니다."
7. "tick 비교는 unsigned 뺄셈으로 합니다. 49.7일 wrap에서 재현 불가능한 버그가 나면 안 되니까요."

그대로 쓸 영어 문장 다섯 개다.
- "A UART gives you byte boundaries but not frame boundaries, so framing is entirely a software problem, and the start byte is only a candidate, never a guarantee."
- "The length check is the single most important line: I validate it before I touch the payload buffer, which turns a corrupted length field from a memory-corruption bug into a counted error."
- "I accumulate the CRC incrementally as bytes arrive rather than over the buffer at the end, so the per-byte cost is constant and there is no jitter spike on the last byte."
- "Every failure path returns to the hunting state and bumps a specific counter, so the factory can tell a noisy cable from a peer that rebooted from a peer running the wrong firmware."
- "Without the inter-byte timeout, a truncated frame swallows the head of the next one and you get a CRC error pointing at the wrong root cause; with it, you get a timeout counter pointing at the real one."

화이트보드에 그리는 순서는 이렇다. 프레임 레이아웃 한 줄과 CRC 범위 밑줄 → 상태 6개를 가로로 놓고 정방향 화살표 → 모든 상태에서 `ST_SOF`로 돌아가는 화살표 세 개에 `err_crc`, `err_len`, `err_timeout` 라벨 → `ST_LEN` 자기 루프를 추가하며 "SOF는 항상 64보다 크다" → payload 배열 칸에 `pos`와 `len` 화살표를 찍고 overflow 설명 → 시간축 두 줄로 같은 바이트 열이 다른 결과를 내는 것을 보여준다.

---

## 11. follow-up 답안

**"payload에 SOF가 나오는 걸 아예 막으려면?"** — 바이트 스터핑을 쓴다. COBS(Consistent Overhead Byte Stuffing)는 `0x00`이 payload에 절대 나타나지 않게 인코딩하고 오버헤드가 254바이트당 1바이트로 **상한이 정해져 있다**. SLIP은 `0xC0`을 구분자로 쓰고 payload의 `0xC0`을 escape하므로 최악에 크기가 2배다. 구분자가 payload에 절대 없다는 보장이 생기면 동기화를 잃어도 다음 구분자에서 반드시 회복되고 길이 필드 자체가 필요 없어진다. 대가는 인코딩·디코딩 단계와 가변 오버헤드다. 길이 기반은 코드가 단순하고 오버헤드가 고정인 대신 복구를 타임아웃과 CRC에 의존한다.

**"이걸 ISR에서 직접 부르고 싶다. 무엇을 바꾸겠나?"** — 셋을 바꾼다. 첫째, `crc16_byte()`를 256-entry 테이블 버전으로 바꿔 바이트당 8회 루프를 lookup 한 번으로 줄인다(flash 512 B). 둘째, 완성된 패킷을 ISR 안에서 고정 크기 패킷 큐 슬롯으로 **복사**하고 task에 알린다. `p->pkt` 포인터를 넘기면 다음 프레임이 덮어쓴다. 셋째, task가 읽는 필드만 `volatile`/`_Atomic`으로 만들고 파서 내부 상태는 ISR 전용으로 유지한다. 마지막으로 최악 경로(패킷 완성 + 복사)의 ISR 실행 시간을 측정해 다음 바이트 도착 전에 끝나는지 확인한다. 115200 baud면 여유가 86 us다.

**"UART RX DMA를 쓰면 이 파서는 어떻게 달라지나?"** — 바이트마다 인터럽트가 사라진다. RX DMA를 circular 모드로 큰 버퍼에 돌리고, half-transfer/transfer-complete 인터럽트와 **idle line 인터럽트**(대부분의 벤더 UART에 있지만 이름과 설정은 제각각이다)에서 "DMA가 지금까지 쓴 위치"를 읽어 이전 위치부터 현재 위치까지를 한꺼번에 파서에 먹인다. 파서 자체는 그대로 쓴다. 바뀌는 것은 tick의 의미다. 바이트별 수신 시각을 더 이상 알 수 없으므로 inter-byte timeout은 "idle line 인터럽트 = 프레임 간 침묵"이라는 더 정확한 신호로 대체하거나 DMA 블록 단위 시각으로 근사한다. 이득은 인터럽트 수가 수천 분의 일로 줄어 CPU와 전력이 크게 준다는 것이고, 대가는 DMA 버퍼가 DMA 가능·cache-coherent 영역에 있어야 하고 오버런 처리가 더 미묘해진다는 것이다.

**"프레임 손실을 상위 계층에서 복구하려면?"** — 헤더에 **sequence 번호**를 넣는다. 수신 측은 `(seq, 결과 코드)`로 ACK를 보내고 송신 측은 타임아웃 후 재전송하며, 수신 측은 **같은 seq를 중복 실행하지 않는다**(idempotency). 이게 없으면 ACK가 유실됐을 때 "모터 10도 회전" 같은 명령이 두 번 실행된다. 재전송은 지수 백오프와 최대 시도 횟수를 두고 초과하면 링크를 down으로 표시해 상위에 알린다. 윈도우를 1로 두면(stop-and-wait) 구현이 단순하고, 처리량이 필요하면 sliding window로 간다. 이 파서의 CRC는 손상 탐지만 하므로 손실 복구는 반드시 상위 계층의 일이다.

**"CRC-16 대신 CRC-32나 서명을 써야 하는 경우는?"** — CRC-16의 미검출 확률은 대략 `2^-16`이다. 짧은 명령 프레임에는 충분하지만 수 KB 이상(OTA 이미지, 큰 calibration blob)에서는 에러 발생 확률 자체가 올라가므로 CRC-32로 간다. 더 중요한 구분은 **무결성과 진위**다. CRC는 우연한 손상만 잡고 의도적 변조는 전혀 못 막는다. 공격자는 payload를 바꾸고 CRC를 다시 계산하면 그만이다. OTA 이미지나 보안이 걸린 명령에는 SHA-256 + 서명(ECDSA P-256 등)이 필요하고 검증 키는 변경 불가능한 저장소(OTP/fuse, write-protected flash)에 둔다. 그 위에서도 CRC는 값이 싸서 전송 오류를 서명 검증 전에 빠르게 걸러내는 용도로 여전히 유용하다.

---

## 12. 요약 & 체크리스트

프레임은 `A5 | LEN | CMD | PAYLOAD | CRC_H | CRC_L`, CRC-16/CCITT-FALSE가 `LEN`부터 `PAYLOAD`까지, 6-상태 기계, 모든 실패는 `ST_SOF`로 복귀하며 원인별 카운터를 올린다.
- [ ] `ST_LEN`에서 `b > PKT_MAX_PAYLOAD`를 payload 저장보다 **먼저** 검사한다.
- [ ] `crc`를 `ST_SOF`에서(프레임마다) `0xFFFF`로 리셋한다.
- [ ] `LEN == 0` 프레임이 `ST_PAYLOAD`를 건너뛴다.
- [ ] `ST_CRC_L`에서 비교 **전에** `st = ST_SOF`로 되돌린다.
- [ ] 타임아웃 검사가 switch **앞**에 있고 `st != ST_SOF` 가드가 붙어 있다.
- [ ] 타임아웃 비교가 `(uint32_t)(now - last) > TIMEOUT` 형태다(wrap 안전).
- [ ] CRC를 shift로 big-endian 조립한다(`memcpy` 금지).
- [ ] `err_crc`, `err_len`, `err_timeout`이 분리되어 있고 한 실패가 하나만 올린다.
- [ ] 호출자가 `true` 직후에만 `p->pkt`를 읽는다(또는 즉시 복사한다).
- [ ] 동적 할당 0, 복사 0, 바이트당 O(1).
