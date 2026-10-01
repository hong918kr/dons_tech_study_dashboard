# 16. CRC-8 비트 루프에서 테이블로

> **주제**: CRC 정의 · 테이블 생성 · 스트리밍 · residue · **난이도**: 중급 · **목표 시간**: 25분
> **해설은 보지 말 것**: 먼저 `starters/16_crc8_table.c`를 채워 `make run N=16`으로 통과시킨다.

## 면접관의 문장

"We hang a 1-Wire temperature sensor off this board, and every byte it gives us is protected by a CRC-8. Write me that CRC. Start with the bit-at-a-time version straight from the definition, because that is the version I can check against the datasheet. Then build the 256-entry table at run time and show me the two agree on every input. Tell me which named CRC-8 variant you are implementing and what its published check value is, and make your code produce that number — I have seen too many CRCs that were self-consistent and wrong. It also has to work on a stream: the bytes arrive a few at a time and I am not buffering the whole message."

## 요구사항

1. 구현할 변종은 **CRC-8/MAXIM-DOW**다(다른 이름: CRC-8/DOW-CRC, 1-Wire CRC, Dallas CRC). 파라미터는 다음과 같다.

| 항목 | 값 |
|---|---|
| width | 8 |
| poly | `0x31` (정방향 표기, `x^8 + x^5 + x^4 + 1`) |
| init | `0x00` |
| refin | true (바이트를 LSB부터 먹는다) |
| refout | true |
| xorout | `0x00` |
| **check** | **`0xA1`** — ASCII `"123456789"` 9바이트에 대한 값 |
| residue | `0x00` |

2. `refin`/`refout`이 true이므로 구현은 반사(reflected) 형태를 쓴다. 반사 다항식은 `0x31`을 비트 반전한 `0x8C`다. 코드에 `0x31`이 아니라 `0x8C`가 보이는 것이 정상이다.
3. `crc8_bit()`은 정의 그대로의 비트 루프다. 바이트당 8회전이고, 이것이 **참조 구현**이다.
4. `crc8_bitwise()`와 테이블 버전 둘 다 `"123456789"`에 대해 `0xA1`을 내야 한다. 이 숫자가 맞지 않으면 나머지가 다 맞아도 틀린 것이다.
5. `crc8_table_build()`는 `table[i]`를 "CRC 레지스터가 0일 때 바이트 `i`를 먹인 결과"로 채운다. 256바이트다.
6. 256개 엔트리 전부가 `crc8_bit(0x00, i)`와 같아야 한다. `table[0]`은 `0x00`이다.
7. `crc8_update()`는 진행 중인 `crc`에 버퍼를 이어 먹인다. 바이트당 배열 참조 **한 번**이다.
8. 스트리밍 성질: 같은 메시지를 어느 경계로 쪼개 넣어도 결과가 같아야 한다. 37바이트 메시지의 38개 분할점 전부에서다.
9. `n == 0`이면 CRC는 init 값(`0x00`) 그대로다. 계산이 아니라 정의다. `d`가 `NULL`이어도 안전해야 한다.
10. `crc8_check(table, d, n)`은 `d[0..n-2]`를 메시지, `d[n-1]`을 그 CRC로 보고 검사한다. `xorout`이 0인 이 변종은 residue도 0이므로, 메시지와 CRC를 이어 한 번에 돌려 `0`이 나오는지만 보면 된다. 별도 비교를 하지 않는다.
11. `n == 0`이면 `crc8_check()`는 `false`다.
12. 16바이트 메시지의 **모든 1비트 에러**(128가지)가 CRC를 바꿔야 한다.
13. 한 바이트 안에서 두 비트를 뒤집는 **모든 조합**(8바이트 메시지에서 224가지)도 전부 검출되어야 한다.
14. `crc8_table_build()`는 순수 함수다. 두 번 불러도, 다른 배열에 만들어도 같은 결과가 나와야 한다.
15. 전역 테이블을 함수 안에 숨기지 않는다. 테이블은 인자로 받는다.

## 인터페이스

```c
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CRC8_POLY_REFLECTED  0x8Cu
#define CRC8_INIT            0x00u
#define CRC8_TABLE_SIZE      256u

uint8_t crc8_bit(uint8_t crc, uint8_t b);
uint8_t crc8_bitwise(const uint8_t *d, size_t n);
void    crc8_table_build(uint8_t *table);
uint8_t crc8_update(const uint8_t *table, uint8_t crc, const uint8_t *d, size_t n);
bool    crc8_check(const uint8_t *table, const uint8_t *d, size_t n);
```

## 제약

- 동적 할당 금지. 테이블은 호출자가 준 배열에 쓴다.
- 하드웨어 CRC 주변장치 금지. 전부 소프트웨어다.
- 테이블을 하드코딩하지 않는다. 코드로 생성한다.
- C11 표준 라이브러리만. `cc -std=c11 -Wall -Wextra -O2`에서 경고 0개.
- `05_uart_parser`의 CRC-16 코드를 복사해 오지 않는다. 그쪽은 MSB-first 비반사이고 이쪽은 LSB-first 반사다. 비트 방향이 반대다.

## 예시 동작

카탈로그 check 값.

```
입력:  '1' '2' '3' '4' '5' '6' '7' '8' '9'   (0x31 0x32 ... 0x39)
출력:  0xA1
```

DS18B20 데이터시트의 scratchpad 예제. 9번째 바이트가 앞 8바이트의 CRC다.

```
바이트:  50 05 4B 46 7F FF 0C 10 | 1C
                                    ^ CRC
crc8_bitwise(첫 8바이트) = 0x1C
crc8_check(9바이트 전체) = true      <- residue 0
```

같은 poly를 MSB-first로 돌리면 다른 값이 나온다.

```
LSB-first 반사 (0x8C) : "123456789" -> 0xA1   <- 정답
MSB-first 비반사(0x31): "123456789" -> 0xA2   <- 우연히 1만 다르다. 더 위험하다
```

스트리밍 분할 불변.

```
crc8_update(T, 0x00, msg,      37)                        = X
crc8_update(T, crc8_update(T, 0x00, msg, 10), msg+10, 27) = X   (같다)
바이트 하나씩 37번 나눠 넣어도                              = X
```

`init = 0x00`의 알려진 약점.

```
crc8_bitwise({}, 0)                       = 0x00
crc8_bitwise({00}, 1)                     = 0x00
crc8_bitwise({00 00 00}, 3)               = 0x00
crc8_bitwise({11 22 33}, 3) == crc8_bitwise({00 00 00 11 22 33}, 6)
```

## 스스로 점검할 질문

1. `table[i] = f(i)`로 충분한 이유를 GF(2)의 선형성으로 설명할 수 있나? `crc_next = f(crc ^ b)`가 왜 성립하나?
2. 반사 CRC에서 바이트를 `crc`의 아래쪽에 XOR하는 이유는 무엇인가? 비반사에서는 어디에 XOR하나?
3. `0x31`과 `0x8C`는 어떤 관계인가? 손으로 비트를 뒤집어 확인해 보라.
4. CRC-16이나 CRC-32에서는 `table[(crc ^ b) & 0xFF]` 뒤에 `crc >> 8`이 붙는데 CRC-8에는 없다. 왜인가?
5. 256바이트 테이블을 16바이트 니블 테이블로 줄이면 속도와 크기가 각각 어떻게 바뀌나?
6. `init = 0x00`의 약점을 실제 프로토콜에서 어떻게 악용할 수 있나? `init = 0xFF`가 그것을 어떻게 막나?
7. CRC-8이 보장하는 검출 능력은 정확히 무엇인가? "모든 에러를 잡는다"가 왜 거짓인가?
8. 수신 측이 CRC를 별도로 계산해 비교하는 방식과 residue를 보는 방식 중 코드가 짧은 쪽은? 두 방식이 항상 같은 답을 주나?

## follow-up (면접관이 이어서 물을 것)

1. "테이블을 빌드 타임에 만들고 싶다. C에서 어떻게 하나?"
2. "Flash 256바이트가 아깝다. 어떤 선택지가 있나?"
3. "CRC-8로 부족한 경우는 언제인가? 무엇으로 바꾸나?"
4. "CRC와 해시(SHA-256)의 용도 차이를 한 문장으로 말해 달라."
5. "수신한 프레임이 CRC는 맞는데 내용이 말이 안 된다. 어디를 의심하겠나?"

---

해설: [L2 노트](../drills/L2_embedded_idioms.html) §3
