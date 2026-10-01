# 11. 구조체 레이아웃 — 패딩, offsetof, 그리고 와이어 포맷

> **주제**: 패딩·정렬 규칙 · `offsetof` · `_Static_assert` · 직렬화 · **난이도**: 중급 · **목표 시간**: 30분
> **레벨**: L1 메모리·포인터 · **해설은 보지 말 것**: 먼저 `starters/11_struct_layout.c`를 채워 `make run N=11`으로 통과시킨다.

## 면접관의 문장

"Here's a bug report from the field. The device firmware sends a 10-byte sensor frame over BLE. The phone app parses it fine. Then we ship a build compiled for a different core and the app starts reading garbage timestamps. Nothing in the protocol changed.

I want you to write the frame encoder and decoder so this can't happen. Assume the wire format is fixed by a spec document I can't change: big-endian, no padding, ten bytes.

While you're at it: tell me `sizeof` your in-memory struct, and tell me how you'd know that number without running the program. And somebody on the team wants to just cast the receive buffer to a struct pointer and read the fields. Talk me out of it — or talk me into it, if you think it's fine."

## 요구사항

1. 메모리 표현은 `sensor_sample_t`다. 다섯 멤버는 `uint8_t id`, `uint16_t seq`, `uint32_t timestamp_us`, `int16_t temp_c_q8`, `uint8_t flags`이고 이 순서를 바꾸지 않는다.
2. 와이어 포맷은 10바이트 고정이다. 오프셋 0에 `id`, 1~2에 `seq`, 3~6에 `timestamp_us`, 7~8에 `temp_c_q8`, 9에 `flags`. 다중 바이트 필드는 모두 big-endian이고 부호 있는 필드는 2의 보수다.
3. `struct_padding_bytes()`는 padding 바이트 수(`sizeof` 구조체 − 멤버 크기의 합)를 돌려준다.
4. 레이아웃 가정은 `_Static_assert`로 컴파일 타임에 못박되 `offsetof(...) == 2`처럼 **숫자를 박지 않는다.** 코드가 실제로 의존하는 성질만 확인한다. 첫 멤버가 오프셋 0인지, 각 멤버가 자기 정렬의 배수 위인지, 멤버가 겹치지 않는지, 구조체 크기가 자기 정렬의 배수인지, `sizeof`가 와이어 크기보다 작지 않은지, `CHAR_BIT`이 8인지.
5. `sensor_pack(out, cap, s)`는 `out`에 10바이트를 적고 적은 바이트 수를 돌려준다. `out`이나 `s`가 NULL이거나 `cap`이 10보다 작으면 `0`을 돌려주고 **한 바이트도 쓰지 않는다.** 부분 기록은 금지다.
6. `sensor_pack`은 `out`의 정렬을 전혀 요구하지 않는다. 바이트 단위 대입과 시프트만 쓴다. `out`이 홀수 주소여도 동작해야 한다.
7. `sensor_unpack(s, in, n)`은 `0`(성공) 또는 `-1`(인자/길이 부족)을 돌려준다. 실패하면 `s`를 건드리지 않는다.
8. `sensor_unpack`도 `in`의 정렬과 호스트 엔디언에 의존하지 않는다. 같은 소스가 little-endian과 big-endian 호스트에서 같은 값을 만든다.
9. `u16_to_i16(u)`는 `uint16_t` 비트패턴을 `int16_t` 값으로 바꾼다. `(int16_t)u` 캐스트 하나로 끝내지 않는다. 범위를 넘는 값의 부호 있는 변환 결과는 C11에서 implementation-defined이므로, 최상위 비트가 서 있으면 `65536`을 빼는 형태로 쓴다.
10. `sensor_pack_array` / `sensor_unpack_array`는 프레임을 10바이트 간격으로 연달아 처리한다. 스트림에서 k번째 프레임의 위치는 `k * 10`이고 `k * sizeof(sensor_sample_t)`가 아니다.
11. `sensor_unpack_array`는 온전한 프레임만 꺼낸다. `n`이 10의 배수가 아니면 남는 꼬리는 건드리지 않고, 꺼낸 개수만 돌려준다. 목적지 배열 크기 `max`를 절대 넘지 않는다.
12. `sensor_pack_array`는 `count`개를 다 담을 공간이 없으면 `0`을 돌려주고 아무것도 쓰지 않는다.
13. `sensor_equal(a, b)`는 필드 단위로 비교한다. `memcmp`로 구조체를 비교하지 않는다. 그 이유를 말할 수 있어야 한다.

## 인터페이스

```c
#include <limits.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t  id;
    uint16_t seq;
    uint32_t timestamp_us;
    int16_t  temp_c_q8;      /* Q8 고정소수점: 256 = 1.0 도 */
    uint8_t  flags;
} sensor_sample_t;

#define SENSOR_WIRE_SIZE 10u

int16_t  u16_to_i16(uint16_t u);
uint16_t i16_to_u16(int16_t v);

size_t struct_padding_bytes(void);
int    sensor_equal(const sensor_sample_t *a, const sensor_sample_t *b);

size_t sensor_pack(uint8_t *out, size_t cap, const sensor_sample_t *s);
int    sensor_unpack(sensor_sample_t *s, const uint8_t *in, size_t n);

size_t sensor_pack_array(uint8_t *out, size_t cap,
                         const sensor_sample_t *arr, size_t count);
size_t sensor_unpack_array(sensor_sample_t *arr, size_t max,
                           const uint8_t *in, size_t n);
```

## 제약

- `#pragma pack`, `__attribute__((packed))`, 컴파일러 전용 확장을 쓰지 않는다. 표준 C11만으로 푼다.
- 수신 버퍼를 `sensor_sample_t *`로 캐스팅해 필드를 읽는 풀이는 인정하지 않는다. 그게 이 문제가 금지하려는 바로 그 패턴이다.
- `union`으로 바이트 배열과 구조체를 겹쳐 놓는 풀이도 인정하지 않는다. 같은 문제를 이름만 바꿔 부르는 것이다.
- `memcpy`로 구조체 전체를 와이어 버퍼에 통째로 옮기지 않는다. 필드마다 명시적으로 쓴다.
- `offsetof`는 써도 되고 권장한다. 다만 오프셋 상수를 소스에 박지 않는다.
- 하드웨어 헤더 금지. C11 표준 라이브러리만.
- `cc -std=c11 -Wall -Wextra -O2 -g`에서 경고 0개.

## 예시 동작

`x86-64`와 `arm-none-eabi`의 기본 ABI에서는 레이아웃이 이렇게 나온다. 다른 타깃에서는 달라질 수 있고, 그래서 코드가 이 숫자에 의존하면 안 된다.

```
sensor_sample_t (sizeof = 12, alignof = 4)
  offset  0    1    2    3    4    5    6    7    8    9   10   11
         | id |PAD |   seq   |    timestamp_us   |  temp   |flg |PAD |
               ^^^^ seq를 2의 배수로 맞추다 1B 버림        전체를 4의 배수로 ^^^^
  멤버 합 = 1+2+4+2+1 = 10 · sizeof = 12 · padding = 2

wire (10 bytes, big-endian, 정렬 요구 없음)
  offset  0    1    2    3    4    5    6    7    8    9
         | id |   seq   |    timestamp_us   |  temp   |flg |
```

구체적인 값 하나를 끝까지 따라가면 이렇다.

```
s.id = 0x2A, s.seq = 0x0102, s.timestamp_us = 0x03040506,
s.temp_c_q8 = -32767 (0x8001), s.flags = 0xC0

sensor_pack -> 2A 01 02 03 04 05 06 80 01 C0
                  ^^^^^ MSB 먼저          ^^^^^ 2의 보수 그대로

sensor_unpack(같은 10바이트) -> 원래 다섯 값이 그대로 복원된다
```

필드 순서를 바꾸면 크기가 달라진다. 와이어 포맷은 그대로다.

```
loose: uint8 id, uint32 ts, uint8 flags, uint16 seq, int16 temp  -> sizeof 16
tight: uint32 ts, uint16 seq, int16 temp, uint8 id, uint8 flags  -> sizeof 12
                                                    같은 데이터, 4바이트 차이
```

padding은 데이터가 아니다. 같은 필드 값을 가진 두 구조체가 `memcmp`로는 다르게 나올 수 있다.

```
a: 전체를 0x00으로 채운 뒤 다섯 필드 대입
b: 전체를 0xFF로 채운 뒤 같은 다섯 필드 대입

sensor_equal(&a, &b)          -> 1   (필드는 같다)
memcmp(&a, &b, sizeof a)      -> != 0 (padding 2바이트가 다르다)
sensor_pack 결과 10바이트      -> 완전히 같다
```

## 스스로 점검할 질문

1. `sizeof(sensor_sample_t)`가 12인 이유를 오프셋 하나씩 짚어 설명하라. `alignof`가 4인 이유는 무엇인가?
2. 수신 버퍼를 `sensor_sample_t *`로 캐스팅해 읽으면 무엇이 깨지는가? 세 가지를 대라. 그중 x86 개발 PC에서 재현되는 것은 몇 개인가?
3. `__attribute__((packed))`를 붙이면 `sizeof`가 10이 된다. 그러면 캐스팅해도 되는가? 왜 안 되는가? packed 멤버의 주소를 포인터로 넘기면 무슨 일이 생기는가?
4. `_Static_assert(offsetof(sensor_sample_t, seq) == 2, ...)`를 쓰면 무엇이 좋고 무엇이 나쁜가? 언제 그렇게 써도 되는가?
5. `uint8_t`를 `<< 24`하면 왜 undefined behaviour가 될 수 있는가? 정수 승격이 어디서 개입하는가?
6. `memcmp`로 구조체를 비교하면 언제 거짓 음성(같은데 다르다고 함)이 나오는가? 거짓 양성도 가능한가?
7. 이 프레임에 필드를 하나 추가해야 한다. 와이어 포맷과 구조체 중 어디를 먼저 고치고, 구버전 기기와의 호환은 어떻게 처리하는가?
8. `CHAR_BIT`이 16인 DSP에서 이 코드는 어디서 처음 깨지는가?

## follow-up (면접관이 이어서 물을 것)

1. "Your decoder runs on every BLE packet. Show me the cost, and tell me when you'd care."
2. "The spec changes to little-endian. How many lines do you touch?"
3. "How would you generate this encoder instead of writing it? What do you lose?"
4. "Now the frame has a variable-length payload at the end. What does the struct look like?"
5. "Somebody reordered the struct members for RAM savings. What tells you at compile time that the wire format still matches?"

---

**해설**: [notes/L1_memory_pointers.md](../drills/L1_memory_pointers.html) §2, §6, §7 · **답안**: `solutions/11_struct_layout.c`
