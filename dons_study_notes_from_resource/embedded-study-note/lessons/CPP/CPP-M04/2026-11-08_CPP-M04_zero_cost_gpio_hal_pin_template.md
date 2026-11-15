---
id: CPP-M04
time: 75분
updated: 2026-11-08
---
# CPP-M04 제로 코스트 GPIO HAL: Pin<Port, N> 템플릿

## 학습 목표

- 핀을 **값이 아니라 타입**으로 표현한 `Pin<Port, N>` 을 구현하고, mock 포트와 실제 MMIO 포트가 같은 `Pin` 코드를 쓰게 한다.
- `set()` / `clear()` 가 **BSRR 쓰기 전용 레지스터에 store 1번**(읽기 0번)임을 접근 로그로 테스트하고, 왜 그것이 인터럽트에 안전한지 설명한다.
- `toggle()` 을 ODR read-modify-write(RMW)로 구현한 뒤, 인터럽트가 LDR 과 STR 사이에 끼어들면 **다른 핀의 변경이 사라지는 것**을 mock 에서 재현한다. BSRR 기반 toggle 과 critical section 이 각각 무엇을 막고 무엇을 못 막는지 구분한다.
- `--target=thumbv7em-none-eabi -O2` 오브젝트를 디스어셈블해 손으로 쓴 C 와 **명령 수와 명령 텍스트를 직접 비교**하고, "제로 코스트"가 성립하지 않는 조건(-O0, 인라이닝 실패)까지 말할 수 있다.
- 같은 포트의 핀들을 묶는 `PinBus<...>` 로 여러 핀을 **store 1번에 동시에** 갱신하고, 포트가 섞이면 컴파일 에러가 나게 한다.

## 1. 문제

GPIO 는 펌웨어에서 가장 자주 쓰는 주변장치이고, 면접에서는 "C 매크로만큼 빠른 타입 안전 HAL 을 C++ 로 짤 수 있나? 그 근거를 어셈블리로 보여 줄 수 있나? ISR 과 공유되면 안전한가?"를 묻는다. 이 세 질문에 한꺼번에 답하는 문제다.

C 로는 보통 이렇게 쓴다.

```c
#define GPIOA_BSRR (*(volatile uint32_t *)0x40020018u)
#define LED_PIN    (1u << 5)
GPIOA_BSRR = LED_PIN;          /* 켜기 */
GPIOA_BSRR = LED_PIN << 16;    /* 끄기 */
```

빠르지만 아무것도 막아 주지 않는다. `GPIOB_BSRR = LED_PIN` 처럼 포트를 틀려도, 핀 번호가 16 이어도 조용히 컴파일된다. 반대로 Arduino 의 `digitalWrite(pin, HIGH)` 는 편하지만 핀 번호를 런타임에 테이블에서 찾는다. 이 레슨의 목표는 **두 장점을 동시에 갖는 것**이다.

```cpp
using Led = Pin<PortA, 5>;
Led::set();                    // C 매크로와 같은 명령 — 아래에서 실제로 비교한다
```

STM32 GPIO 에는 출력을 바꾸는 길이 두 개 있고, 이 차이가 레슨의 핵심이다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">BSRR: 쓰기 전용, 1 을 쓴 비트만 하드웨어가 처리한다 (PA5 예)</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="340" height="34" class="f-warn"/>
    <rect x="360" y="40" width="340" height="34" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="30" y="62">[31:16] BR15 .. BR0  (1 = clear)</text>
    <text x="370" y="62">[15:0]  BS15 .. BS0  (1 = set)</text>
    <text x="20" y="92">bit31</text><text x="330" y="92">bit16</text>
    <text x="365" y="92">bit15</text><text x="672" y="92">bit0</text>
    <text x="30" y="122">Led::clear() → BSRR = 1u &lt;&lt; 21</text>
    <text x="370" y="122">Led::set()   → BSRR = 1u &lt;&lt; 5</text>
  </g>
  <path d="M190 132 L190 170 M530 132 L530 170" stroke="currentColor"/>
  <path d="M186 164 L190 172 L194 164 M526 164 L530 172 L534 164" stroke="currentColor" fill="none"/>
  <rect x="120" y="174" width="500" height="36" class="f-acc" stroke="currentColor"/>
  <text x="140" y="197" fill="currentColor" font-family="monospace" font-size="12">ODR: 하드웨어가 지정 비트만 바꾼다. 나머지 비트는 CPU 가 몰라도 된다</text>
  <text x="20" y="238" fill="currentColor">핵심: 값을 읽지 않으므로 "읽고 → 수정하고 → 쓰는" 틈이 없다.</text>
  <text x="20" y="258" fill="currentColor">ODR 에 직접 쓰는 길(ODR ^= mask)은 CPU 가 읽고 쓰므로, 그 사이에 인터럽트가 들어올 수 있다.</text>
  <text x="20" y="278" fill="currentColor">같은 store 에 BSx 와 BRx 가 모두 1 이면 BSx(set)가 우선한다 (RM0090, mock 도 동일).</text>
</svg>
```

호스트에서는 `0x40020018` 에 쓸 수 없으므로 `MockPort<Id>` 가 레지스터를 배열로 흉내 내고 모든 접근을 `mock::log` 에 기록한다. 레지스터 맵을 타입에 담는 일반론과 버스 교체 설계는 CPP-M01, 필드 쓰기는 CPP-M02 의 범위이고, 여기서는 **핀 하나의 단일 store** 에 집중한다.

### Part A — `Pin<Port, N>`: set / clear / write / read / mode

- `template <GpioPort P, unsigned N> requires (N < 16) struct Pin` 을 채운다. 모든 멤버는 `static` 이고 데이터 멤버는 없다 (`std::is_empty_v` 가 참).
- `set()` / `clear()` / `write(bool)` 은 **BSRR 에 store 1번**. 로그에 읽기가 있으면 실패한다.
- `read()` 는 IDR(핀의 실제 입력 레벨), `output()` 은 ODR(내보내라고 한 값). 둘은 다른 레지스터다.
- `mode(Mode)` 는 MODER 의 2비트 필드 RMW. 옆 핀의 필드를 보존하고, 초기화 때만 쓴다.
- 핀 번호 0, 1, 5, 7, 8, 14, 15 로 표 구동 테스트. 특히 `N=15` 의 clear 는 비트 31 이다.

### Part B — toggle 과 인터럽트

- `toggle()` 은 `ODR ^= mask`. mock 의 `arm_isr()` 는 "다음 ODR read 직후 ISR 실행"을 결정적으로 만들어, ISR 이 다른 핀을 set 한 것이 **사라지는** 버그를 테스트로 고정한다.
- `toggle_bsrr()` 은 ODR 을 읽되 쓰기는 BSRR 로 해서 이 핀의 비트만 바꾼다. 같은 끼어들기에서 PA3 이 살아남아야 한다.
- 그러나 ISR 도 **같은 핀**을 토글하면 토글 1회가 사라진다. 이를 테스트로 확인하고, `toggle_critical<Lock>()` 으로 해결한다.
- 고정 시드 xorshift32 로 2001회 무작위 끼어들기: RMW 유실 > 0, BSRR 유실 = 0.

### Part C — 극성 어댑터, 단일 store 버스, 드라이버

- `ActiveLow<P>`: `on()` 이 `P::clear()` 가 되는 빈 어댑터.
- `PinBus<First, Rest...>`: 같은 포트 핀들을 묶어 `write(v)` 를 **BSRR store 1번**으로. 포트가 섞이거나 핀이 겹치면 컴파일 에러(concept).
- `Lcd4<Rs, En, Data>`: HD44780 호환 LCD 의 4비트 쓰기 순서를 mock 로그로 검증.

## 2. Problem (English)

> **Zero-cost GPIO pin abstraction.**
>
> 1. Implement `template <GpioPort P, unsigned N> requires (N < 16) struct Pin` with static members `set()`, `clear()`, `write(bool)`, `read()`, `output()`, `mode(Mode)`, over a port policy offering `read(Reg)` / `write(Reg, uint32_t)`. `set`/`clear`/`write` must perform exactly one store to the write-only BSRR and zero reads. `Pin<P, 16>` must not compile.
> 2. Implement `toggle()` as an ODR read-modify-write. Using a mock that fires an interrupt between the read and the write, show that a concurrent change to a *different* pin is lost. Implement `toggle_bsrr()` which does not lose it, explain why it still loses a concurrent toggle of the *same* pin, and fix that with `toggle_critical<Lock>()`.
> 3. Implement `PinBus<First, Rest...>`: `write(v)` maps bit *i* of `v` to the *i*-th pin and updates all pins with one BSRR store. Pins from different ports, or a repeated pin, must not compile.
> 4. Compile for `--target=thumbv7em-none-eabi -O2`, disassemble, and compare instruction counts against hand-written C with direct register writes.
>
> Example: `Pin<PortA,5>::clear()` stores `1u << 21`; `Pin<PortA,15>::clear()` stores `0x80000000`; `PinBus<Pin<PortA,0>,Pin<PortA,3>,Pin<PortA,7>,Pin<PortA,12>>::write(0b1010)` stores `0x00811008`.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 "23/82 passed" — TODO 를 채워 82/82 를 만든다
```

`CHECK` 는 가변 인자 매크로다. `CHECK(can_pin<PortA, 0>)` 안의 쉼표를 전처리기가 인자 구분자로 읽기 때문이다.

## 4. 힌트

<details>
<summary>힌트 1 — set/clear 는 정말 한 줄이다</summary>

`mask` 와 `reset_mask` 는 이미 있다. `P::write(Reg::BSRR, mask)`. `write(bool)` 도 분기로 store 를 두 벌 만들지 말고 `high ? mask : reset_mask` 값을 **한 번** 쓴다.

</details>

<details>
<summary>힌트 2 — toggle_bsrr 의 값 계산</summary>

ODR 에서 이 핀이 1 이면 reset 비트(`mask << 16`)를, 0 이면 set 비트(`mask`)를 쓴다. 분기 없이 `((odr & mask) << 16) | (~odr & mask)` 로 쓸 수 있다.

</details>

<details>
<summary>힌트 3 — Lock 은 수명으로 critical section 을 만든다</summary>

`Lock guard;` 한 줄이 구간을 연다. 함수를 나갈 때 소멸자가 닫는다 (RAII 는 CPP-B06 참고).

</details>

<details>
<summary>힌트 4 — 팩의 i번째를 세는 fold</summary>

첫 핀은 따로 처리한 뒤 `unsigned i = 1; ((out |= ((v >> i) & 1u) << Rest::number, ++i), ...);` 처럼 **콤마 연산자 fold** 로 순서대로 실행한다. 콤마 fold 는 왼쪽부터 평가된다.

</details>

<details>
<summary>힌트 5 — 핀 중복 검사는 popcount 하나로</summary>

모든 mask 를 OR 한 결과의 1 비트 수가 핀 개수와 같으면 겹침이 없다. `std::popcount` (C++20, `<bit>`).

</details>

## 5. 풀이

### Part A — 타입으로 핀을 고정한다

**왜 `Pin<PortA, 5>` 가 타입이어야 하나.** `struct PinCfg { Gpio* port; uint32_t mask; };` 를 값으로 들고 다니면 컴파일러는 그 값을 레지스터에서 꺼내야 한다 (상수 전파가 일어나는 경우를 빼면). 타입이면 `N` 과 `Port` 가 **컴파일 시점에 이미 상수**라서, 함수 본문이 인라인되면 레지스터 주소와 마스크가 즉시값(immediate)이 된다. 그리고 `Pin<PortA,5>` 와 `Pin<PortB,5>` 는 서로 다른 타입이라 함수 인자나 템플릿 인자로 쓰면 **포트 착오가 컴파일 에러**가 된다.

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Pin 은 Port 가 무엇인지 모른다 — read(Reg) / write(Reg, v) 두 함수만 쓴다</text>
  <rect x="250" y="38" width="240" height="56" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="264" y="60" fill="currentColor" font-family="monospace" font-size="12">Pin&lt;Port, 5&gt;::set()</text>
  <text x="264" y="80" fill="currentColor" font-family="monospace" font-size="12">Port::write(Reg::BSRR, 1u&lt;&lt;5)</text>
  <path d="M310 94 L170 134 M430 94 L570 134" stroke="currentColor" fill="none"/>
  <rect x="20" y="136" width="310" height="76" rx="6" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="32" y="158">MmioPort&lt;0x40020000u&gt; (실제 칩)</text>
    <text x="32" y="178">*(volatile u32*)(Base + off) = v;</text>
    <text x="32" y="198">→ thumbv7em: str r1, [r0]</text>
  </g>
  <rect x="410" y="136" width="310" height="76" rx="6" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="422" y="158">MockPort&lt;0&gt; (테스트)</text>
    <text x="422" y="178">regs[] 갱신 + mock::log 기록</text>
    <text x="422" y="198">→ 로그로 "store 1번"을 검증</text>
  </g>
</svg>
```

`requires (N < 16)` 는 잘못된 핀을 `Pin` 이라는 이름을 쓰는 순간 거부한다. `static_assert` 를 본문에 넣는 방식과의 차이(requires-expression 으로 검사 가능 여부)는 CPP-B12 에서 다뤘다. 하네스의 `can_pin` 이 바로 그 기법이고, 템플릿 안에서 물어야 false 가 된다는 점도 같다.

| 동작 | 레지스터 | 접근 | 비고 |
|---|---|---|---|
| `set()` | BSRR 아래 16비트 | write 1 | 읽기 0 |
| `clear()` | BSRR 위 16비트 | write 1 | N=15 → 비트 31 |
| `write(b)` | BSRR | write 1 | 값만 고르고 store 는 1번 |
| `read()` | IDR | read 1 | 핀의 **실제** 레벨 |
| `output()` | ODR | read 1 | 우리가 **명령한** 값 |
| `mode(m)` | MODER | read 1 + write 1 | 비원자적 RMW, 초기화 전용 |

**Part A 의 함정: `1u` 의 u.** 16비트 마스크를 `uint16_t` 로 두고 `pin << 16` 을 쓰면 정수 승격(integral promotion)으로 `int` 가 된다. `0x8000 << 16` 은 C 에서 UB 다 (C11 6.5.7: 결과가 `int` 로 표현 불가). C++14/17 은 "대응 unsigned 타입으로 표현 가능하면" 허용, C++20 부터는 모듈러 연산으로 정의됐다. 어느 쪽이든 unsigned 로 시작하면 고민할 필요가 없다.

**`read()` 와 `output()` 은 다른 질문이다.** 출력 모드 핀이라도 외부에서 눌리면(오픈 드레인, 단락) IDR 은 ODR 과 달라진다. 하네스가 두 레지스터를 일부러 다르게 poke 해서 확인한다.

#### 어셈블리로 비교: 정말 C 와 같은가

`demo_asm_pin.cpp` (헤더 없는 Pin + `MmioPort<0x40020000u>`) 와 `demo_asm_c.c` (손으로 쓴 매크로)를 같은 플래그로 컴파일한다. `asm_compare.sh` 가 함수별 명령 수를 세고, 주소와 정렬용 `nop` 을 뺀 **명령 텍스트까지 비교**한다.

```bash
bash asm_compare.sh                  # 맥 clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 (lesson 폴더에 _asm/ 생성)
TOOL=gcc bash asm_compare.sh         # Lima VM 의 arm-none-eabi-g++ 14.2.1
```

실제 출력 (clang, -O2):

```text
op             Pin(C++)        C  same text?
set                   5        5  yes
clear                 5        5  yes
clear15               5        5  yes
write                 8        8  yes
read                  5        5  yes
toggle                6        6  yes
toggle_bsrr           8        8  yes
pulse                 7        7  yes
two_pins              7        7  yes
bus_const             6        6  yes
bus_var              14       14  no
toggle_crit           9
rt_set                -        4
tbl_set               -        7
call_rt_set           -        4
```

실제 출력 (arm-none-eabi-g++ 14.2.1, -O2):

```text
op             Pin(C++)        C  same text?
set                   4        4  yes
clear                 4        4  yes
clear15               4        4  yes
write                 7        7  yes
read                  4        4  yes
toggle                5        5  yes
toggle_bsrr           9        9  yes
pulse                 6        6  yes
two_pins              6        6  yes
bus_const             4        4  yes
bus_var              17       17  no
toggle_crit           8
```

`same text?` 는 즉시값까지 포함해 명령 텍스트가 같은지 본다 (PC 상대 리터럴 오프셋만 무시). 한 컴파일러 안에서 **`Pin` 과 C 의 명령 수는 전부 같고**, `bus_var` 하나만 텍스트가 다르다 (명령 수는 같고 레지스터 배정과 순서만 다르다 — Part C 참고). `nm` 으로 봐도 -O2 오브젝트에 `Pin<...>` 심볼이 하나도 없다 (`cpp_*` 12개만 있다).

`set` 의 실제 디스어셈블리 (`cpp_set` 과 `c_set` 이 글자까지 같다):

```text
clang -O2                              arm-none-eabi-g++ -O2
  movs r0, #0x18                         ldr  r3, [pc, #4]
  movt r0, #0x4002                       movs r2, #32
  movs r1, #0x20                         str  r2, [r3, #24]
  str  r1, [r0]                          bx   lr
  bx   lr                                .word 0x40020000
```

컴파일러끼리는 **다르다**. clang 은 주소를 `movs`+`movt` 로 만들고(5명령, 리터럴 없음), GCC 는 리터럴 풀(literal pool)에서 읽는다(4명령 + 데이터 워드 4바이트). 코드 크기는 둘 다 12바이트로 같았다 (함수 시작 주소가 0x0, 다음 함수 0xC). 어느 쪽이든 핀 상태를 바꾸는 **`str` 은 정확히 1개**다. 사이클 수는 이 환경에서 측정하지 못했다 (Mac 에 DWT 없음) — 명령 수만 비교했고, 리터럴 읽기와 `movt` 의 사이클 차이는 **미확인**이다.

**-O2 조건을 벗어나면.** "제로 코스트"는 인라이닝이 된다는 전제다. 같은 스크립트로 실측:

| 조건 | set 명령 수 | 비고 |
|---|---|---|
| clang -O2, Pin | 5 | C 와 동일 |
| clang -O0, Pin | `cpp_set` 3 + `Pin::set` 5 + `MmioPort::write` 10 = **18** (호출 체인) | C 는 5. 오브젝트 text 758B vs C 460B |
| clang -O0, Pin + `[[gnu::always_inline]]` (`-DFORCE_INLINE`) | **12** (호출은 없고 스택 왕복) | text 824B — 더 커졌다 |
| gcc -Og, Pin | 단일 핀 연산은 C 와 글자까지 동일 | `PinBus::write` 는 **인라인 안 됨** (`bl`) — `cpp_bus_const` 가 C 의 `ldr/ldr/str/bx` 대신 `push/movs/bl/pop` |

`-O0` 에서는 `nm -C` 로 `Pin<MmioPort<...>,5u>::set()` 같은 `Pin`/`PinBus` 약한 심볼(weak)이 11개 생겼고 (`MmioPort`, `IrqLock` 까지 합치면 15개), 호출하는 핀과 함수 조합에 비례해 코드가 늘어난다. 디버그 빌드에서 플래시가 넘치거나 핀 토글 속도가 느려지는 이유다. 비트뱅이 타이밍을 맞춰야 하는 코드는 디버그 빌드에서 동작이 달라질 수 있다.

**런타임 방식과 비교.** `rt_set(Gpio* p, u32 n)` 은 본체만 4명령이지만 **호출 측이 인자를 준비**해야 한다. `call_rt_set` 은 clang 에서 `movs r0,#0 / movt r0,#0x4002 / movs r1,#5 / b.w` 의 4명령(꼬리 호출)이라 호출부까지 합치면 8명령이다. 테이블 방식 `tbl_set` 은 인덱스로 포트 포인터와 마스크를 **메모리에서 읽어** 7명령이고, 테이블이 플래시를 차지한다. `Pin` 은 호출부 자리에 `movs/movt/movs/str` 네 개만 남는다 (`bx lr` 은 데모 함수 자체의 복귀라 호출부 비용이 아니다).

### Part B — 왜 toggle 은 원자적이지 않은가

`toggle()` 의 clang 출력은 다음과 같다.

```text
cpp_toggle:
  movs r0, #0x14
  movt r0, #0x4002
  ldr  r1, [r0]          ← ODR 읽기
  eor  r1, r1, #0x20     ← 여기와
  str  r1, [r0]          ← 여기 사이에 인터럽트가 들어올 수 있다
  bx   lr
```

Cortex-M 은 명령 경계에서 인터럽트를 받는다. `LDR` 뒤 `STR` 앞에 들어온 ISR 이 ODR 을 바꾸면, main 은 **옛날 값을 기준으로 계산한 값**을 STR 로 덮어쓴다. 이 현상을 갱신 유실(lost update)이라 한다.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">ODR RMW 사이에 ISR — 실제 mock 로그와 같은 순서</text>
  <text x="10" y="58" fill="currentColor">main</text>
  <text x="10" y="168" fill="currentColor">ISR</text>
  <path d="M60 70 L730 70" stroke="currentColor" fill="none"/>
  <path d="M60 180 L730 180" stroke="currentColor" fill="none"/>
  <g stroke="currentColor">
    <rect x="70" y="42" width="150" height="26" class="f-ok"/>
    <rect x="540" y="42" width="180" height="26" class="f-warn"/>
    <rect x="280" y="152" width="200" height="26" class="f-acc"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="80" y="60">LDR ODR → 0x0000</text>
    <text x="548" y="60">STR ODR ← 0x0020</text>
    <text x="290" y="170">BSRR ← 0x0008 (PA3 set)</text>
    <text x="80" y="96">ODR = 0x0000</text>
    <text x="290" y="204">ODR = 0x0008</text>
    <text x="548" y="96">ODR = 0x0020</text>
    <text x="548" y="114">PA3 사라짐!</text>
  </g>
  <path d="M220 56 L280 156 M480 156 L540 56" stroke="currentColor" fill="none"/>
  <text x="10" y="234" fill="currentColor">main 이 쓴 0x0020 은 ISR 이 끼어들기 전의 ODR 에서 계산한 값이다.</text>
  <text x="10" y="252" fill="currentColor">BSRR 은 ISR 쪽 쓰기를 정확히 처리했는데, main 이 그 결과를 덮었다.</text>
  <text x="10" y="270" fill="currentColor">주의: 하드웨어는 잘못이 없다. 비원자적인 것은 CPU 의 읽기-쓰기 시퀀스다.</text>
</svg>
```

`demo_isr_race.cpp` 실제 출력:

```text
--- toggle() = ODR RMW, ISR 이 그 사이 PA3 set
  [0] read  ODR  0x00000000
  [1] write BSRR 0x00000008
  [2] write ODR  0x00000020
  최종 ODR = 0x0020  (PA5=1, PA3=0)
--- toggle_bsrr(), 같은 끼어들기
  [0] read  ODR  0x00000000
  [1] write BSRR 0x00000008
  [2] write BSRR 0x00000020
  최종 ODR = 0x0028  (PA5=1, PA3=1)
--- toggle_bsrr(), ISR 도 같은 핀 PA5 를 toggle
  [0] read  ODR  0x00000000
  [1] read  ODR  0x00000000
  [2] write ODR  0x00000020
  [3] write BSRR 0x00000020
  최종 ODR = 0x0020  (PA5=1, PA3=0)
--- toggle_critical(), ISR 은 unmask 때까지 pending
  [0] read  ODR  0x00000000
  [1] write ODR  0x00000020
  [2] read  ODR  0x00000020
  [3] write ODR  0x00000000
  최종 ODR = 0x0000  (PA5=0, PA3=0)

고정 시드 2001회: ISR 발생 1010회, RMW 유실 484회, BSRR 유실 0회
```

세 구현이 막는 것을 정리하면 다음과 같다.

| 구현 | 다른 핀 변경 유실 | 같은 핀 토글 유실 | 인터럽트 지연 추가 | clang -O2 명령 수 |
|---|---|---|---|---|
| `toggle()` (`ODR ^= m`) | **발생** | 발생 | 없음 | 6 |
| `toggle_bsrr()` (read ODR + BSRR) | 없음 | **발생** | 없음 | 8 |
| `toggle_critical<Lock>()` | 없음 | 없음 | **있음** (`cpsid i` ~ `msr primask` 구간) | 9 |

왜 `toggle_bsrr` 로도 부족한가. 계산의 입력인 "현재 ODR" 은 여전히 읽은 값이다. 같은 핀을 ISR 도 토글하면, main 은 0을 읽은 뒤 ISR 이 1 로 바꿔 놓은 핀에 "set" 을 또 쓴다. 결과는 1 이고, 직렬화됐다면 0 이어야 한다. **한 핀의 토글을 main 과 ISR 이 공유한다면** 설계를 바꾸거나 critical section 이 필요하다. 보통은 애초에 한 컨텍스트만 그 핀을 소유하게 만든다.

BSRR 토글의 clang 출력은 `ldr r1, [r0]` / `and` / `orr.w r1, r1, r1, lsl #16` / `eor r1, r1, #0x20` / `str r1, [r0, #0x4]` 로, 쓰기가 **단일 `str`** 임을 확인할 수 있다. ODR 주소(0x14)를 베이스로 두고 +4 로 BSRR(0x18)에 쓴다.

`toggle_critical` 은 `mrs r0, primask` / `cpsid i` 로 시작해 `msr primask, r0` 로 끝난다 (9명령, `demo_asm_pin.cpp` 의 `IrqLock`). 이전 PRIMASK 를 저장했다가 복원하는 이유는, 이미 인터럽트가 꺼진 구간에서 호출돼도 잘못 켜지 않기 위해서다. mock 의 `IrqGuard` 도 `prev` 를 저장한다.

**"ISR-safe" 의 근거를 정확히.** `set()` 이 안전한 이유는 `volatile` 이 아니라 **하드웨어 동작**이다. (1) 정렬된 32비트 `STR` 은 ARMv7-M 에서 단일 복사 원자성(single-copy atomicity)을 가져 인터럽트로 쪼개지지 않고, (2) BSRR 은 1 을 쓴 비트만 바꾸며 값을 읽지 않기 때문이다. `volatile` 은 store 를 지우거나 합치거나 재배치하지 못하게 할 뿐이고 ([intro.abstract], CPP-B05 참고), 표준은 "정확히 명령 1개"까지 보장하지 않는다 ("volatile 접근이 무엇인지"는 구현 정의, [dcl.type.cv]). 그래서 위 디스어셈블리로 실제 `str` 1개를 확인한 것이다.

**같은 함수의 연속 store 는 합쳐지지 않는다.** `cpp_pulse` 는 `str` 2개, `cpp_two_pins` 도 `str` 2개였다 (volatile 이라 정상). 그러나 "두 핀을 동시에"가 필요할 때 이 2개는 **동시가 아니다** — Part C 의 이유다.

**다른 칩이라면.** 하드웨어가 toggle 을 지원하면 끼어들 틈이 아예 없다 — RP2040 SIO 의 `GPIO_OUT_XOR` 처럼. nRF52 는 `OUTSET`/`OUTCLR` 이 분리되어 있고 XOR 레지스터는 없다. Cortex-M3/M4 는 bit-band alias 로 한 비트를 store 1번에 바꿀 수 있고 (`bitband_alias`, 추가 연습 5), Cortex-M7 에는 bit-band 가 없다. 이런 포트 차이는 Port 정책에 넣어 `if constexpr` 로 고른다 (추가 연습 3, 4). DMA 가 BSRR 에 쓰는 파형 출력도 같은 원리다.

### Part C — 여러 핀을 한 번에

`ActiveLow<P>` 는 데이터가 없는 어댑터다. `Status::on()` 이 `Pin::clear()` 로 인라인되면 극성 변환은 코드에 남지 않는다. 호출 측은 "켜기/끄기"만 말하고, 회로의 극성은 타입 한 곳에 숨는다.

**왜 PinBus 인가.** 4비트 병렬 버스를 `D0::write(...); D1::write(...);` 로 쓰면 store 4번이고, 그 사이 버스는 **중간값(glitch)** 을 지난다. 같은 포트라면 BSRR 한 번에 켤 비트(아래 절반)와 끌 비트(위 절반)를 함께 실어 **모든 핀을 store 1번에** 바꿀 수 있다.

| 입력 | 계산 | 값 |
|---|---|---|
| `Nibble::mask` | PA0, PA3, PA7, PA12 의 OR | `0x1089` |
| `write(0b1010)` → `on` | bit1→PA3, bit3→PA12 | `0x1008` |
| `off` | `mask & ~on` | `0x0081` |
| BSRR 값 | `on \| (off << 16)` | `0x00811008` |

`0b1010` 을 상수로 쓴 `cpp_bus_const` 의 clang 출력 (C 와 텍스트가 같다):

```text
cpp_bus_const:
  movs r0, #0x18
  movw r1, #0x1008
  movt r0, #0x4002
  movt r1, #0x81
  str  r1, [r0]
  bx   lr
```

**컴파일 타임 계산이 전부 사라졌다** — `0x00811008` 이 즉시값으로 들어갔다. 변수 입력인 `cpp_bus_var` 는 핀이 흩어져 있어서 비트를 하나씩 옮기는 산술이 필요하고, 명령 수는 C 와 같지만(14개) 레지스터 배정과 순서가 다르다 (컴파일러가 같은 식을 다른 순서로 스케줄링). 어느 쪽이든 마지막 `str` 은 1개다.

**fold expression** ([expr.prim.fold], C++17) 이 팩을 순서대로 펼친다. 콤마 fold 는 왼쪽부터 평가되므로 `i` 가 올바른 순서로 증가한다. `mask` 는 `(First::mask | ... | Rest::mask)` 의 이항(binary) 왼쪽 fold 다.

**BusPins concept.** 두 조건을 && 로 묶는다. 포트가 같은지(`std::same_as` fold)와 핀이 겹치지 않는지(popcount). 이 concept 이 있어야 "PinBus 는 store 1번"이 **타입이 보장하는 성질**이 된다. 포트가 섞이면 원리적으로 store 1번이 불가능하기 때문이다. 포트가 다른 핀이 꼭 필요하면 동시 갱신이 안 된다는 사실을 호출부에서 드러내야 한다.

실제 컴파일 에러 (`demo_errors.cpp`, 경로만 줄임):

```text
$ c++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR=1 demo_errors.cpp
demo_errors.cpp:13:5: error: constraints not satisfied for class template 'Pin' [with P = MockPort<0>, N = 16]
./solution.cpp:87:14: note: because '16U < 16' (16 < 16) evaluated to false

$ ... -DSHOW_ERROR=2
demo_errors.cpp:15:5: error: constraints not satisfied for class template 'PinBus' [with First = Pin<MockPort<0>, 0>, Rest = <Pin<MockPort<1>, 1>>]
./solution.cpp:136:14: note: because 'BusPins<Pin<MockPort<0>, 0>, Pin<MockPort<1>, 1>>' evaluated to false
./solution.cpp:132:20: note: because 'std::same_as<typename Pin<MockPort<0>, 0>::port, typename Pin<MockPort<1>, 1>::port>' evaluated to false

$ ... -DSHOW_ERROR=3
demo_errors.cpp:17:5: error: constraints not satisfied for class template 'PinBus' [with First = Pin<MockPort<0>, 0>, Rest = <Pin<MockPort<0>, 0>>]
./solution.cpp:133:20: note: because 'std::popcount(Pin<MockPort<0>, 0>::mask | Pin<MockPort<0>, 0>::mask) == 1 + static_cast<int>(sizeof...(Rest))' (1 == 2) evaluated to false

$ ... -DSHOW_ERROR=4
demo_errors.cpp:19:5: error: no matching function for call to 'spi_select'
demo_errors.cpp:8:13: note: candidate function not viable: no known conversion from 'Pin<MockPort<1>, [...]>' to 'Pin<MockPort<0>, [...]>' for 1st argument

$ ... -DSHOW_ERROR=5
demo_errors.cpp:21:16: error: non-type template argument evaluates to -1, which cannot be narrowed to type 'unsigned int' [-Wc++11-narrowing]
```

E5 는 템플릿 인자의 **변환된 상수 식(converted constant expression)** 규칙 덕분이다 ([temp.arg.nontype]): 축소 변환(narrowing)은 허용되지 않아 `-1` 이 `unsigned` 로 몰래 바뀌지 않는다. 대체 기능 테이블(AF) 검증처럼 핀과 기능의 조합을 static_assert 로 검사하는 것은 CPP-M14 범위다.

**`Lcd4` 드라이버** 는 핀을 전부 타입 인자로 받는다. `Data` 자리에 `PinBus` 든 다른 것이든 `write` 만 있으면 들어간다. 테스트는 로그로 RS → 데이터 → E 상승 → E 하강 순서를 7개 store 까지 확인한다. 실제 LCD 는 각 단계 사이에 데이터시트의 최소 시간(setup/hold, E 펄스 폭)을 지켜야 하는데 mock 에서는 생략했다.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution
# 82/82 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer solution.cpp -o sa && ./sa
# 82/82 passed, sanitizer 출력 없음

c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti demo_isr_race.cpp -o demo_isr_race && ./demo_isr_race
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti demo_errors.cpp   -o demo_errors   && ./demo_errors
c++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR=1 demo_errors.cpp      # SHOW_ERROR=1..5
bash asm_compare.sh                                                                        # thumbv7em (맥 clang)
TOOL=gcc bash asm_compare.sh                                                               # arm-none-eabi-g++ (Lima VM)
OPT=-O0 bash asm_compare.sh; OPT=-O0 EXTRA=-DFORCE_INLINE bash asm_compare.sh              # 인라이닝 조건 비교
```

`demo_isr_race.cpp` / `demo_errors.cpp` 는 이 레슨의 `solution.cpp` 를 `#define main solution_main` 뒤에 include 해서 같은 `Pin` 을 그대로 쓴다.

## 7. 자주 하는 실수

**1. `ODR |= mask` 로 set 하기.**

```cpp
GPIOA->ODR |= (1u << 5);       // ✗ RMW — 다른 핀을 다루는 ISR 과 경쟁한다
GPIOA->BSRR = (1u << 5);       // ✓ store 1번
```

**2. "`volatile` 이니까 ISR-safe" 라고 말하기.** `volatile` 은 읽고 쓰는 동작을 보존할 뿐, RMW 를 원자적으로 만들지 않는다. `ODR ^= mask` 가 `volatile` 이어도 LDR 과 STR 사이의 틈은 그대로다. 안전의 근거는 BSRR 의 하드웨어 의미다.

**3. BSRR 을 읽어서 수정하기.** BSRR 은 write-only 로 항상 0 을 읽는다. `P::read(BSRR) | mask` 는 의미 없는 읽기만 추가한다 — `P::write(BSRR, mask)` 한 줄이면 된다.

**4. clear 시프트를 잘못 계산.** `mask << 16` 은 `uint32_t` 에서 해야 한다. `uint16_t` 변수로 두면 `int` 로 승격되어 N=15 에서 부호 비트를 건드린다 (Part A 함정). `1 << (N + 16)` 도 `1` 이 `int` 라서 같은 문제다.

**5. `read()` 를 ODR 에서 읽기.** 버튼/센서 핀의 실제 입력은 IDR 이다. `Pin::read` 가 ODR 을 보면 입력 핀은 항상 0으로 보인다.

**6. toggle_bsrr 를 "완전한 해결"이라고 하기.** 다른 핀은 안전하지만 같은 핀을 main 과 ISR 이 함께 토글하면 1회가 사라진다 (테스트로 고정). `toggle()` 이 필요한 핀은 소유 컨텍스트를 하나로 하거나 `toggle_critical` 을 쓴다.

**7. 모드 변경의 순서.** 출력 모드로 바꾼 뒤 값을 쓰면 잠깐 ODR 의 옛 값이 핀에 나간다. 값을 먼저 쓰고 나서 `mode(Mode::Output)` 을 한다.

```cpp
Led::write(false);             // ✓ 먼저 원하는 레벨을 ODR 에
Led::mode(Mode::Output);       //   그다음 출력 전환 — 핀에 글리치 없음
```

**8. 포트가 다른 핀을 하나의 "동시 출력"으로 쓰기.** BSRR 은 포트마다 따로라서 store 2번, 동시가 아니다. `PinBus` 의 concept 이 막는 것이 이것이다.

## 8. 면접 Q&A

<details>
<summary>Q1. BSRR 을 쓰면 왜 ISR 과 함께 써도 안전한가? volatile 덕분인가?</summary>

아니다. 안전한 이유는 두 가지다. 정렬된 32비트 store 는 Cortex-M 에서 인터럽트로 쪼개지지 않는다 (단일 복사 원자성, ARMv7-M ARM A3.5). 그리고 BSRR 은 1 을 쓴 비트만 하드웨어가 바꾸므로 다른 핀을 건드릴 일이 없다. `volatile` 은 store 가 지워지지 않게 할 뿐이다. 실제 `-O2` 출력에서 `str` 이 1개임을 확인할 수 있다.

꼬리질문 — 그럼 ODR 에 `str` 한 번 쓰는 건? 그것도 store 는 원자적이지만 **어떤 값을 쓰느냐**가 문제다. 포트 전체를 덮어쓰므로, 값을 만들 때 읽은 ODR 이 낡아 있으면 다른 핀의 변화가 사라진다.

</details>

<details>
<summary>Q2. ODR ^= mask 의 경쟁 창은 정확히 어디인가? 얼마나 자주 터지나?</summary>

clang 출력 기준으로 `ldr` 와 `str` 사이, 즉 `ldr` / `eor` / `str` 의 명령 경계들이다. 인터럽트가 그 구간에 닿아야 하므로 드물고, 그래서 재현이 어렵고 필드에서만 터진다. 빈도는 인터럽트 주기와 toggle 호출 빈도에 달렸다 (이 레슨은 빈도를 측정한 것이 아니라 mock 으로 경로를 결정적으로 재현했다).

꼬리질문 — 고치는 방법 세 가지와 각각의 비용? BSRR 쓰기 (같은 핀 경쟁은 남음), PRIMASK critical section (9명령, 지연 증가), bit-band alias 또는 하드웨어 XOR 레지스터 (칩이 지원할 때).

</details>

<details>
<summary>Q3. "제로 코스트"라는 주장을 어떻게 증명하나? 언제 깨지나?</summary>

`--target=thumbv7em-none-eabi -O2` 로 오브젝트를 만들어 손으로 쓴 C 와 비교한다. 이 레슨의 실측에서 단일 핀 연산은 clang, arm-none-eabi-g++ 모두 명령 텍스트까지 같았다. 깨지는 조건은 인라이닝이 안 될 때다. clang -O0 에서는 set 이 호출 체인 18명령(C 는 5), `always_inline` 을 강제해도 12명령이었다. gcc -Og 는 단일 핀은 같지만 `PinBus::write` 는 `bl` 로 남았다. 그리고 "제로 코스트"는 명령 수의 주장이지 사이클 측정은 아니다 — 여기서는 사이클을 재지 않았다.

</details>

<details>
<summary>Q4. 핀 번호가 부팅 후에야 결정되는 보드 변형(HW rev)은 어떻게 하나?</summary>

템플릿 인자는 컴파일 타임 상수라서 런타임 선택을 할 수 없다. 방법은 둘이다. 보드마다 빌드를 나누어 `using Led = Pin<...>` 만 바꾸거나 (대부분의 펌웨어), 런타임이 꼭 필요하면 `PinCfg{port, mask}` 테이블을 쓰고 그 비용을 받아들인다 (`tbl_set` 은 7명령 + 플래시 테이블, 인덱스 범위 검사도 직접 해야 한다). 템플릿 HAL 과 런타임 테이블을 무리하게 한 타입으로 합치지 않고, 경계를 두는 편이 낫다.

</details>

<details>
<summary>Q5. ODR 을 std::atomic&lt;uint32_t&gt; 로 만들면 안 되나?</summary>

안 된다. `std::atomic` 은 C++ 객체에 대한 것이고, ODR 은 주소에 있는 하드웨어 레지스터라 C++ 객체가 아니다. `reinterpret_cast<std::atomic<uint32_t>*>(addr)` 은 표준 밖이다. 또 `fetch_xor` 는 LDREX/STREX 루프가 될 텐데, 주변장치 영역에서 exclusive 접근이 유효한지는 구현 정의로 알고 있다 — 이 레슨에서 실제 칩으로 확인하지는 못했다 (**미확인**). 게다가 Cortex-M0 에는 LDREX 자체가 없다. std::atomic 자체의 이야기는 CPP-A04 를 참고.

</details>

<details>
<summary>Q6. PinBus 에서 포트가 다른 핀을 왜 금지했나? 글리치는 뭔가?</summary>

"store 1번으로 모든 핀을 동시에 갱신한다"가 PinBus 의 존재 이유인데 BSRR 은 포트별 레지스터라 포트가 다르면 store 2번이고 그 사이에 버스는 중간값을 지난다. 수신 측이 그 순간을 샘플링하면 틀린 값을 읽는다 (글리치). concept 으로 막으면 이 불변식이 타입에 드러난다. 정말 여러 포트가 필요하면 포트별 store 로 순서를 호출부에서 드러내고, 스트로브(E, latch) 신호로 데이터 유효 시점을 분리한다.

</details>

<details>
<summary>Q7. 템플릿 HAL 의 코드 부풀림(bloat)은? 핀이 100개면?</summary>

-O2 에서 인라인되면 호출 지점마다 기계어가 생기고 함수 인스턴스는 남지 않는다 (`nm` 에 `Pin` 심볼 0개 확인). 그래서 "템플릿 수"가 아니라 "호출 지점 수"의 비용이고, 각각은 C 매크로와 같은 크기다. 문제가 되는 것은 인라인이 안 되는 큰 함수를 핀 타입마다 찍어낼 때다. 이때는 본체를 `(GPIO*, mask)` 인자를 받는 비템플릿 함수로 빼고 템플릿은 얇은 껍데기만 남긴다.

</details>

## 9. 추가 연습

### 연습 1 — PinBus 읽기

`PinBus::gather(idr)`: `scatter` 의 역연산. 흩어진 핀의 값을 모아 `bit i` 로 돌려준다. `read()` 는 IDR 을 **한 번만** 읽는다 — 핀 8개를 8번 읽으면 그 사이 값이 바뀔 수 있기 때문이다.

<details>
<summary>답</summary>

```cpp
static constexpr std::uint32_t gather(std::uint32_t idr) {
    std::uint32_t out = (idr >> First::number) & 1u;
    unsigned i = 1;
    ((out |= ((idr >> Rest::number) & 1u) << i, ++i), ...);
    return out;
}
```

</details>

### 연습 2 — chip select RAII

`ScopedLow<Pin>`: 생성 시 clear, 소멸 시 set. 복사 금지. (RAII 일반론은 CPP-B06 참고.)

<details>
<summary>답</summary>

```cpp
template <typename Pin> struct ScopedLow {
    ScopedLow() { Pin::clear(); }
    ~ScopedLow() { Pin::set(); }
    ScopedLow(const ScopedLow&) = delete;
    ScopedLow& operator=(const ScopedLow&) = delete;
};
```

로그에 CS 내림 → 데이터 → CS 올림 순서가 남는지 확인한다.

</details>

### 연습 3 — set/clear 가 분리된 포트 (nRF52 스타일)

`SplitPort` 는 `set_bits` / `clr_bits` 만 있다 (BSRR 없음). 같은 `Pin` API 를 유지하면서 `write_mask(set, clr)` 가 BSRR 포트에서는 store 1번, 분리 포트에서는 store 2번이 되게 `requires` 로 고른다. 이 포트에서는 다중 비트 **동시 갱신이 불가능**하다는 점을 설명한다.

<details>
<summary>답</summary>

```cpp
template <typename P>
static void write_mask(std::uint32_t set, std::uint32_t clr) {
    if constexpr (requires { P::set_clear(set, clr); }) {
        P::set_clear(set, clr);
    } else {
        if (clr != 0) P::clr_bits(clr);
        if (set != 0) P::set_bits(set);
    }
}
```

</details>

### 연습 4 — 하드웨어 XOR 를 골라 쓰는 toggle (RP2040 스타일)

`XorPort` 는 `xor_bits` 가 있다. `toggle()` 이 `HasXor<P>` 면 store 1번(read 0), 없으면 read 후 set/clear store 1번을 고른다. XOR 레지스터가 왜 같은 핀 경쟁까지 없애는지 설명한다.

<details>
<summary>답</summary>

```cpp
static void toggle() {
    if constexpr (HasXor<P>) P::xor_bits(mask);          // read-modify-write 를 하드웨어가 한다
    else if (P::read_out() & mask) P::clr_bits(mask);    // 다른 핀은 안전, 같은 핀 경쟁은 남음
    else P::set_bits(mask);
}
```

CPU 가 값을 읽지 않으므로 "낡은 값" 이 없다. ISR 과 main 이 같은 핀을 토글해도 토글이 2번 일어난다.

</details>

### 연습 5 — bit-band alias 주소 (Cortex-M3/M4)

공식 `alias = 0x42000000 + (addr - 0x40000000) * 32 + bit * 4` 를 `constexpr` 함수로 쓰고 `static_assert` 로 GPIOA ODR(`0x40020014`) 의 bit 5 가 `0x42400294` 인지 확인한다. Cortex-M7 에는 bit-band 가 없다.

<details>
<summary>답</summary>

```cpp
constexpr std::uint32_t bitband_alias(std::uint32_t reg_addr, unsigned bit) {
    return 0x4200'0000u + (reg_addr - 0x4000'0000u) * 32u + bit * 4u;
}
static_assert(bitband_alias(0x4002'0014u, 5) == 0x4240'0294u);
```

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 19/19 passed
```

## 10. 핵심 정리

- `Pin<Port, N>` = 핀을 타입으로. Port 와 N 이 컴파일 상수라서 인라인되면 주소·마스크가 즉시값이 된다. 다른 핀은 다른 타입, 틀린 조합은 컴파일 에러.
- `set/clear` = BSRR store 1번 + 읽기 0번. BS 는 아래 16비트, BR 은 위 16비트, N=15 clear 는 비트 31. 안전의 근거는 `volatile` 이 아니라 원자적 store + 1 인 비트만 바꾸는 하드웨어.
- `ODR ^= mask` 는 LDR 과 STR 사이에 틈이 있어 ISR 이 다른 핀에 한 일까지 지운다.
- `toggle_bsrr` 은 다른 핀을 지키지만 같은 핀 경쟁은 남는다 → 소유자를 하나로 하거나 PRIMASK critical section, 또는 하드웨어 XOR.
- `PinBus` = 같은 포트 핀을 BSRR 1번에 동시 갱신. 포트 혼합·중복 핀은 concept 으로 거부. 포트가 다르면 동시 갱신 자체가 불가능하다.
- 실측 (thumbv7em -O2): `Pin` 과 C 의 명령 수는 전부 동일 (clang, GCC 각각). 컴파일러끼리는 다르다 (clang `movs/movt`, GCC 리터럴 풀). 명령 텍스트가 달랐던 것은 `bus_var` 뿐 (순서·레지스터 차이).
- 제로 코스트는 **인라이닝 전제**: clang -O0 set = 호출 체인 18명령, `always_inline` 강제 12명령, gcc -Og 는 `PinBus::write` 를 `bl` 로 남긴다.

## 참고자료

- STM32F4 Reference Manual RM0090 — §8.4 GPIO registers (BSRR, ODR, IDR, MODER; BSx 우선 규칙)
- ARMv7-M Architecture Reference Manual (DDI 0403) — A3.5.3 Atomicity in the ARM architecture, PRIMASK / CPS
- Arm Cortex-M4 Technical Reference Manual / Cortex-M3 Generic User Guide — bit-banding 영역과 alias 공식
- RP2040 Datasheet — SIO `GPIO_OUT_XOR` · nRF52 Product Specification — GPIO `OUTSET`/`OUTCLR`
- [cppreference — Fold expressions](https://en.cppreference.com/w/cpp/language/fold) · [Constraints and concepts](https://en.cppreference.com/w/cpp/language/constraints) · [Requires expression](https://en.cppreference.com/w/cpp/language/requires) · [std::popcount](https://en.cppreference.com/w/cpp/numeric/popcount) · [Template non-type arguments](https://en.cppreference.com/w/cpp/language/template_parameters)
- C++20 표준: [expr.prim.fold], [temp.constr], [expr.prim.req.type], [temp.arg.nontype], [expr.shift], [dcl.type.cv], [intro.abstract]
- C11 6.5.7 (Bitwise shift operators) · CWG 1457 (C++14 의 부호 있는 왼쪽 시프트)
- 이 트랙: CPP-M01 (레지스터 타입, MMIO/mock Port 설계), CPP-M02 (필드 RMW), CPP-M14 (AF 표 검증), CPP-B04, CPP-B05, CPP-B06, CPP-B08, CPP-B12, CPP-A04
- STM32 HAL 의 `HAL_GPIO_TogglePin` 도 BSRR 기반 토글을 쓴다고 알고 있으나, HAL 소스를 이 환경에서 직접 확인하지 못했다 (**미확인**). 또한 이 레슨의 명령 수 비교는 사이클 측정이 아니다.
