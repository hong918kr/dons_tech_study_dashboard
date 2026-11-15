---
id: BM-B11
time: 60분
updated: 2026-11-11
---
# BM-B11 GPIO 드라이버 (레지스터 직접): 모드·풀업·출력·입력

## 학습 목표

- 핀당 2비트(MODER·OSPEEDR·PUPDR), 1비트(OTYPER), 4비트(AFRL/AFRH) 필드가 섞인 GPIO 레지스터에서 **그 핀의 필드만** 바꾸는 `gpio_init` 을 쓰고, 전 핀 × 전 설정을 랜덤으로 미리 채운 레지스터 위에서 exhaustive 로 증명한다.
- `gpio_write` = BSRR 쓰기 1번, `gpio_read` = IDR 읽기 1번, `gpio_toggle` = ODR 읽기 + BSRR 쓰기 — 각각 왜 그 레지스터인지 (그리고 왜 `ODR ^=` 나 IDR 기준 토글이 아닌지) 접근 로그로 설명할 수 있다.
- RCC 클럭 enable 이 왜 먼저인지 (클럭이 꺼진 주변장치는 쓰기를 무시하고 읽으면 0) 를 mock 으로 재현하고, 드라이버가 접근 순서로 지키는지 테스트한다.
- 입력의 세 가지 경우 — 외부 구동, 내부 풀업/풀다운, 떠 있음(floating) — 를 IDR 모델로 구분하고, open-drain 에서 IDR ≠ ODR 이 되는 이유를 말할 수 있다.
- 보드 초기화에서 "CS 핀이 부팅 중 한순간 0 이 되는" 글리치(glitch)를 재현하고, 초기값을 먼저 래치에 넣는 순서로 없앤다.

## 1. 문제

보드를 처음 살릴 때(bring-up) 가장 먼저 하는 일은 "LED 하나 켜기" 이고, 그게 안 되면 나머지는 하나도 안 된다. 그래서 펌웨어 면접의 단골 첫 문제가 **"STM32 GPIO 드라이버를 레지스터로 직접 짜 보세요"** 다. 쉬워 보이지만 채점 포인트가 많다: 2비트 필드를 지우고 넣었나, 다른 핀(특히 디버거가 붙은 PA13/PA14)을 건드리지 않았나, 클럭을 먼저 켰나, BSRR 을 쓰나, 토글을 무엇으로 읽나, `1 << 31` 을 썼나.

C-B01 에서는 가짜 `GPIO_TypeDef` 구조체에 `gpio_set_mode` / `gpio_write` 를 써 봤다. 이번에는 **동작하는 mock 주변장치** 위에서 실제 드라이버 하나를 완성한다. Don 이 SSD 컨트롤러나 RF 모듈 보드에서 겪었을 "레지스터는 맞게 썼는데 핀이 안 움직인다(클럭 꺼짐)", "초기화 중에 리셋 라인이 한 번 튀었다(글리치)", "디버거가 갑자기 안 붙는다(SWD 핀 덮어씀)" 를 모두 mock 이 잡아낸다.

레지스터 정의는 STM32F4 (RM0090) 기준 대표값이다 (RM0090 기준 — RM 확인 권장). 테스트의 정답은 이 레슨 mock 의 정의다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">GPIOA 필드 — PA5 OUTPUT 뒤 MODER 0xA8000400, board_init 뒤 AFRL 0x00007700</text>
  <g fill="currentColor"><text x="10" y="54">MODER</text><text x="10" y="68" font-size="12">+0x00</text></g>
  <g stroke="currentColor" fill="none">
    <rect x="80" y="40" width="640" height="30"/>
    <path d="M120 40v30M160 40v30M200 40v30M240 40v30M280 40v30M320 40v30M360 40v30M400 40v30M440 40v30M480 40v30M520 40v30M560 40v30M600 40v30M640 40v30M680 40v30"/>
  </g>
  <rect x="80" y="40" width="120" height="30" class="f-warn"/>
  <rect x="480" y="40" width="40" height="30" class="f-acc"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="100" y="60">10</text><text x="140" y="60">10</text><text x="180" y="60">10</text><text x="220" y="60">00</text>
    <text x="260" y="60">00</text><text x="300" y="60">00</text><text x="340" y="60">00</text><text x="380" y="60">00</text>
    <text x="420" y="60">00</text><text x="460" y="60">00</text><text x="500" y="60">01</text><text x="540" y="60">00</text>
    <text x="580" y="60">00</text><text x="620" y="60">00</text><text x="660" y="60">00</text><text x="700" y="60">00</text>
    <text x="100" y="34">15</text><text x="140" y="34">14</text><text x="180" y="34">13</text><text x="500" y="34">5</text><text x="700" y="34">0</text>
    <text x="500" y="86">[11:10]</text><text x="140" y="86">SWD/JTAG = AF</text>
  </g>
  <g fill="currentColor"><text x="10" y="122">OTYPER</text><text x="10" y="136">+0x04</text></g>
  <g stroke="currentColor" fill="none">
    <rect x="80" y="108" width="320" height="30" stroke-dasharray="4 3"/>
    <rect x="400" y="108" width="320" height="30"/>
    <path d="M420 108v30M440 108v30M460 108v30M480 108v30M500 108v30M520 108v30M540 108v30M560 108v30M580 108v30M600 108v30M620 108v30M640 108v30M660 108v30M680 108v30M700 108v30"/>
  </g>
  <rect x="600" y="108" width="20" height="30" class="f-acc"/>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="240" y="128">[31:16] reserved</text><text x="410" y="104">15</text><text x="610" y="104">5</text><text x="710" y="104">0</text>
    <text x="610" y="154">bit 5 (1비트 필드)</text>
  </g>
  <g fill="currentColor"><text x="10" y="190">AFRH +0x24</text><text x="10" y="204">AFRL +0x20</text></g>
  <g stroke="currentColor" fill="none">
    <rect x="80" y="176" width="320" height="30"/><rect x="400" y="176" width="320" height="30"/>
    <path d="M120 176v30M160 176v30M200 176v30M240 176v30M280 176v30M320 176v30M360 176v30M440 176v30M480 176v30M520 176v30M560 176v30M600 176v30M640 176v30M680 176v30"/>
  </g>
  <rect x="560" y="176" width="80" height="30" class="f-ok"/>
  <path d="M400 168v46" stroke="currentColor" stroke-width="3"/>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="100" y="172">15</text><text x="380" y="172">8</text><text x="420" y="172">7</text><text x="700" y="172">0</text>
    <text x="580" y="196">7</text><text x="620" y="196">7</text>
    <text x="100" y="196">0</text><text x="140" y="196">0</text><text x="180" y="196">0</text><text x="220" y="196">0</text>
    <text x="260" y="196">0</text><text x="300" y="196">0</text><text x="340" y="196">0</text><text x="380" y="196">0</text>
    <text x="420" y="196">0</text><text x="460" y="196">0</text><text x="500" y="196">0</text><text x="540" y="196">0</text>
    <text x="660" y="196">0</text><text x="700" y="196">0</text>
    <text x="400" y="228">핀 8 경계: AFR[pin &gt;&gt; 3], 시프트 4·(pin &amp; 7)</text>
  </g>
  <g fill="currentColor"><text x="10" y="270">BSRR</text><text x="10" y="284">+0x18 (w)</text></g>
  <g stroke="currentColor" fill="none"><rect x="80" y="256" width="320" height="30"/><rect x="400" y="256" width="320" height="30"/></g>
  <rect x="280" y="256" width="20" height="30" class="f-warn"/>
  <rect x="600" y="256" width="20" height="30" class="f-ok"/>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="180" y="276">BR15..BR0 [31:16]</text><text x="500" y="276">BS15..BS0 [15:0]</text>
    <text x="290" y="304">BR5 = bit 21</text><text x="610" y="304">BS5 = bit 5</text>
  </g>
  <text x="80" y="324" fill="currentColor">write(PA5,1) → 0x00000020 · write(PA5,0) → 0x00200000 · 둘 다 1 이면 set 우선</text>
</svg>
```

### Part A — 드라이버 핵심 4개

`uint32_t port` 는 포트 베이스 주소 (`GPIOA` = 0x40020000, +0x400 씩). mock 은 GPIOA~D 4개 포트와 `RCC_AHB1ENR` 만 정의한다.

- `int gpio_clock_enable(port)` — `RCC_AHB1ENR` 의 GPIOxEN 비트(A=0 … D=3)를 RMW 로 켜고 read-back.
- `int gpio_init(port, pin, mode, pull, speed, otype, af)` — 클럭 → OTYPER → OSPEEDR → PUPDR → (AF 면) AFRL/AFRH → **MODER 마지막**. 그 핀의 필드만 바꾼다. 아이템의 5인자 시그니처는 `gpio_init5()` (push-pull, AF0) 로 감쌌다.
- `int gpio_write(port, pin, level)` — BSRR 쓰기 1번. `int gpio_read(port, pin)` — IDR 읽기 1번. `int gpio_toggle(port, pin)` — ODR 읽기 → BSRR 쓰기.

### Part B — 다른 핀을 건드리지 않나, 경계, 잘못된 인자, 클럭

- 4 포트 × 16 핀 × (mode, pull, speed, otype, AF 일 때 af 0..15) = **28160 설정** 전부를, 모든 레지스터를 랜덤으로 채운 상태에서 실행하고 "대상 필드만 바뀌었나 / 다른 포트는 그대로인가 / ENR 다른 비트 보존 / 클럭 쓰기가 포트 접근보다 먼저인가 / 마지막 쓰기가 MODER 인가" 를 검사한다.
- AFRL ↔ AFRH 경계 (핀 7 | 8), 핀 0 과 핀 15 (BSRR bit 31), 잘못된 인자 12종 → `GPIO_EINVAL` 이고 **레지스터 접근 0번**.
- 클럭 선행: 클럭 없이 쓰면 mock 이 무시하고 읽으면 0 (BM-B03 규칙 축약판). 드라이버는 처음 보는 포트도 스스로 클럭을 켠다.
- 떠 있는 입력은 읽을 때마다 값이 흔들리고, analog 모드는 항상 0, open-drain 핀은 IDR 과 ODR 이 다를 수 있다.

### Part C — 보드 코드

- `gpio_init_output(port, pin, initial, speed, otype)` — 초기값을 BSRR 로 출력 래치에 **먼저** 넣고 MODER 를 바꾼다 (글리치 없음).
- `board_init()` — LED PA5 (출력 0), 버튼 PC13 (입력 풀업, 누르면 0), USART2 TX/RX = PA2/PA3 (AF7, RX 풀업), SPI 플래시 CS = PB6 (출력 1). 결과 레지스터 값을 손으로 계산한 상수와 비교하고, PA13/14/15 가 AF 그대로(SWD 생존)인지 본다.
- `app_step()` — 메인 루프 한 바퀴: 버튼 눌림 엣지마다 LED 토글. 그리고 "잘못된 핀을 출력으로" 설정하면 mock 이 충돌(contention)로 잡는 것을 본다.

## 2. Problem (English)

> **Register-level GPIO driver (STM32F4-style) on a behavioural mock.**
>
> Registers per port (offsets): MODER 0x00 (2 bits/pin: 00 in, 01 out, 10 AF, 11 analog), OTYPER 0x04 (1 bit/pin: PP/OD), OSPEEDR 0x08 (2 bits/pin), PUPDR 0x0C (2 bits/pin: none/up/down/reserved), IDR 0x10 (r), ODR 0x14 (rw), BSRR 0x18 (w: [15:0] set, [31:16] reset), AFRL 0x20 / AFRH 0x24 (4 bits/pin, pins 0–7 / 8–15). Port clock enable: `RCC_AHB1ENR` (0x40023830) bit = port index; with the bit clear the port ignores writes and reads as 0. All access goes through `REG_READ(addr)` / `REG_WRITE(addr, val)`.
>
> Implement:
> ```c
> int gpio_clock_enable(uint32_t port);
> int gpio_init(uint32_t port, unsigned pin, gpio_mode_t mode, gpio_pull_t pull,
>               gpio_speed_t speed, gpio_otype_t otype, unsigned af);
> int gpio_write(uint32_t port, unsigned pin, int level);   /* exactly one BSRR write  */
> int gpio_read(uint32_t port, unsigned pin);               /* exactly one IDR read    */
> int gpio_toggle(uint32_t port, unsigned pin);             /* never writes ODR        */
> ```
> Constraints: only the target pin's fields may change; other ENR bits must be preserved; reject invalid arguments (unknown port, pin > 15, enum out of range, af > 15, analog with pull) with `GPIO_EINVAL` **before any register access**; no `1 << 31` on `int`.
>
> Example: from reset, `gpio_init(GPIOA, 5, MODE_OUTPUT, PULL_NONE, SPEED_LOW, OTYPE_PP, 0)` → `RCC_AHB1ENR` 0x00100000 → 0x00100001, MODER 0xA8000000 → 0xA8000400 (bits [11:10] = 01, PA13–15 unchanged). `gpio_write(GPIOA, 5, 0)` → single write `BSRR = 0x00200000`. `gpio_write(GPIOD, 15, 0)` → `BSRR = 0x80000000`.
>
> Follow-ups: Why BSRR? Why read ODR rather than IDR when toggling? Why the dummy read after enabling the clock? Your init drives a chip-select low for a moment at boot — fix it. Someone wrote `GPIOA->MODER = 0x400` and the debugger no longer connects — why?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/BM-B11_st && /tmp/BM-B11_st   # 13/73 passed, exit 1
```

시작 코드에는 mock 코어(BM-B02 축약판), RCC+GPIO 모델, 테스트 하네스가 다 들어 있다. TODO 는 드라이버 함수 8개 (`gpio_clock_enable`, `gpio_init`, `gpio_write`, `gpio_read`, `gpio_toggle`, `gpio_init_output`, `board_init`, `app_step`). 스텁은 `GPIO_EINVAL` 을 돌려주므로 잘못된 인자 테스트와 "모델 자체" 테스트 몇 개만 통과한다.

관찰용 파일:

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bugs.c -o /tmp/BM-B11_bugs && /tmp/BM-B11_bugs     # 레슨 폴더에서 (solution.c 를 include)
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_target.c -o -          # 타깃 asm
cc -std=c11 -Wall -Wextra -Werror -O1 -fsanitize=undefined -fno-sanitize-recover=undefined demo_shift_ub.c -o /tmp/BM-B11_ub && /tmp/BM-B11_ub
```

## 4. 힌트

<details>
<summary>힌트 1 — 필드 하나 바꾸기는 언제나 "지우고 → 넣기"</summary>

핀 `pin` 의 필드는 폭 w 에 따라 시프트가 다르다: 2비트 레지스터는 `2*pin`, OTYPER 는 `pin`, AFR 은 `4*(pin & 7)` 이고 레지스터는 `pin < 8 ? AFRL : AFRH`. 새 값 = `(old & ~(mask << s)) | ((val & mask) << s)` — 주어진 `field_put()` 이 바로 이것이다 (C-B01). `|=` 만 하면 01 → 10 으로 바꿀 때 11(analog) 이 된다.

</details>

<details>
<summary>힌트 2 — 인자 검사는 레지스터를 만지기 전에, 클럭은 그 다음</summary>

`port_index(port) < 0`, `pin > 15`, enum 은 `(unsigned)mode > 3` 처럼 unsigned 로 바꿔 비교 (음수도 한 번에 걸린다). analog + 풀은 거부 정책. 검사를 통과하면 `gpio_clock_enable()` 부터: `en = READ(ENR); WRITE(ENR, en | bit); (void)READ(ENR);`. 테스트는 접근 로그에서 ENR 쓰기 이전에 GPIO 주소 접근이 하나라도 있으면 실패로 본다.

</details>

<details>
<summary>힌트 3 — write/read/toggle 은 접근 횟수까지 채점된다</summary>

write: `REG_WRITE(port + BSRR, level ? (1u << pin) : (1u << (pin + 16u)))` — 읽기 없음. `1u` 의 `u` 를 빼면 핀 15 에서 `1 << 31` (signed int 오버플로, UB). read: IDR 을 읽고 `>> pin & 1`. toggle: **ODR** 을 읽는다 (내가 내보내려던 값). IDR 은 핀의 실제 레벨이라 open-drain 이나 외부 충돌 때 다르다. 쓰기는 BSRR 로 그 핀만.

</details>

<details>
<summary>힌트 4 — Part C: 글리치는 "MODER 를 바꾸는 순간 출력 래치에 뭐가 있었나" 의 문제</summary>

입력 모드에서도 ODR(출력 래치)은 쓸 수 있다. 그러니 `gpio_write(port, pin, initial)` 을 먼저 (클럭부터 켜고!) 하고 그다음 `gpio_init(OUTPUT)` 을 하면, MODER 가 01 이 되는 첫 순간부터 핀은 initial 을 낸다. 클럭을 안 켜고 BSRR 을 쓰면 mock 이 무시하므로 글리치가 그대로 남는다.

</details>

## 5. 풀이

### Part A — 레지스터 규칙과 mock

먼저 규칙을 표로 고정한다 (RM0090 기준 — RM 확인 권장; 리셋 값은 GPIOA/GPIOB 만 0 이 아니다).

| 레지스터 | 오프셋 | 접근 | 핀당 폭 | 리셋 GPIOA | 리셋 GPIOB | 메모 |
|---|---|---|---|---|---|---|
| MODER | 0x00 | rw | 2 | 0xA8000000 | 0x00000280 | PA13/14/15, PB3/4 = AF (SWD/JTAG) |
| OTYPER | 0x04 | rw | 1 | 0 | 0 | [31:16] reserved |
| OSPEEDR | 0x08 | rw | 2 | 0x0C000000 | 0x000000C0 | PA13, PB3 = 11 |
| PUPDR | 0x0C | rw | 2 | 0x64000000 | 0x00000100 | PA15 up, PA14 down, PA13 up, PB4 up |
| IDR | 0x10 | r | 1 | — | — | 핀 레벨 (analog 모드면 0) |
| ODR | 0x14 | rw | 1 | 0 | 0 | 출력 래치 |
| BSRR | 0x18 | w | 1+1 | 0 | 0 | 읽으면 0, set 이 reset 보다 우선 |
| LCKR | 0x1C | rw | 1 | 0 | 0 | 잠금 (연습 3) |
| AFRL / AFRH | 0x20 / 0x24 | rw | 4 | 0 | 0 | 핀 0–7 / 8–15 |

리셋 값의 비트를 직접 풀어 보면 맞는지 확인할 수 있다: MODER `0xA8000000` = `10 10 10 00…` → PA15·PA14·PA13 = AF. PUPDR `0x64000000` = `01 10 01 00…` → PA15 풀업, PA14 풀다운, PA13 풀업. 즉 **SWDIO(PA13) / SWCLK(PA14) 는 리셋 직후부터 디버거용 AF 로 살아 있다**. 드라이버가 MODER 를 통째로 덮으면 이게 죽는다.

mock 의 구조:

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">mock 구조 — 드라이버는 REG_READ / REG_WRITE 두 매크로로만 레지스터를 본다</text>
  <rect x="10" y="34" width="200" height="66" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="54" fill="currentColor" font-weight="bold">GPIO 드라이버</text>
  <text x="20" y="72" fill="currentColor" font-family="monospace">gpio_init / write</text>
  <text x="20" y="88" fill="currentColor" font-family="monospace">read / toggle</text>
  <rect x="270" y="34" width="200" height="66" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="280" y="54" fill="currentColor" font-weight="bold">REG_READ / REG_WRITE</text>
  <text x="280" y="72" fill="currentColor">UNIT_TEST: mock_read32()</text>
  <text x="280" y="88" fill="currentColor">타깃: volatile ldr / str</text>
  <rect x="530" y="34" width="200" height="66" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="540" y="54" fill="currentColor" font-weight="bold">mock 코어</text>
  <text x="540" y="72" fill="currentColor">레지스터 표 41개 (64 칸)</text>
  <text x="540" y="88" fill="currentColor">접근 로그 {R/W, addr, val}</text>
  <path d="M210 67h56M470 67h56" stroke="currentColor" fill="none"/>
  <path d="M260 62l6 5-6 5M520 62l6 5-6 5" stroke="currentColor" fill="none"/>
  <rect x="270" y="130" width="460" height="72" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="280" y="150" fill="currentColor" font-weight="bold">훅 gpio_on_read / gpio_on_write (주소별)</text>
  <text x="280" y="168" fill="currentColor">클럭 꺼짐(AHB1ENR 비트 0) → 쓰기 무시, 읽기 0 · IDR 쓰기 무시</text>
  <text x="280" y="186" fill="currentColor">BSRR → ODR = (ODR &amp; ~BR) | BS, BSRR 저장값 0 · IDR 읽기 ← 핀 모델</text>
  <path d="M630 100v26" stroke="currentColor" fill="none"/><path d="M625 120l5 6 5-6" stroke="currentColor" fill="none"/>
  <rect x="270" y="222" width="460" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="280" y="242" fill="currentColor" font-weight="bold">핀 모델 (포트당 16핀)</text>
  <text x="280" y="260" fill="currentColor">ext[pin] ∈ {FLOAT, LOW, HIGH} + MODER/OTYPER/PUPDR/ODR → 레벨 0/1/Z/X</text>
  <text x="280" y="278" fill="currentColor">레벨이 바뀌면 edge 로그 (글리치), X 가 되면 contention++</text>
  <path d="M500 202v16" stroke="currentColor" fill="none"/><path d="M495 212l5 6 5-6" stroke="currentColor" fill="none"/>
  <rect x="10" y="130" width="220" height="162" rx="6" stroke="currentColor" fill="none" stroke-dasharray="5 3"/>
  <text x="20" y="150" fill="currentColor" font-weight="bold">테스트 하네스</text>
  <text x="20" y="170" fill="currentColor">mock_poke / mock_peek</text>
  <text x="20" y="186" fill="currentColor">(백도어: 훅·로그 없음)</text>
  <text x="20" y="206" fill="currentColor">ext_set(): 버튼·다른 칩</text>
  <text x="20" y="226" fill="currentColor">g_log: 접근 순서 검사</text>
  <text x="20" y="246" fill="currentColor">pin_history(): "01", "1"</text>
  <text x="20" y="266" fill="currentColor">g_unexpected: 정의 안 된</text>
  <text x="20" y="282" fill="currentColor">주소 접근 = 실패</text>
</svg>
```

훅 시그니처는 BM-B02 와 같다: `on_read(addr, stored, ctx)` 는 CPU 가 보는 값을, `on_write(addr, old, val, ctx)` 는 새 저장 값을 돌려준다. 그래서 "쓰기 전용 BSRR 이 ODR 을 바꾸고 자기는 0 으로 남는다", "IDR 은 저장값이 아니라 핀에서 계산된다" 같은 하드웨어 동작을 레지스터 하나당 몇 줄로 표현할 수 있다.

**IDR 모델** (핀 하나의 레벨 결정, `model_pin_level`):

| MODER | 출력 구동 | 외부 ext | 풀 | 레벨 | IDR 비트 |
|---|---|---|---|---|---|
| 01 output, PP | ODR | FLOAT | — | ODR | ODR |
| 01 output, PP | ODR | 반대 값 | — | X (충돌) | 잡음 |
| 01 output, OD, ODR=0 | 0 | FLOAT/LOW | — | 0 | 0 |
| 01 output, OD, ODR=1 | 놓음 (Z) | LOW | up | 0 (외부가 이김) | 0 |
| 00 input / 10 AF | 없음 | LOW/HIGH | 무관 | ext | ext |
| 00 input / 10 AF | 없음 | FLOAT | up / down | 1 / 0 | 1 / 0 |
| 00 input / 10 AF | 없음 | FLOAT | none | Z | **잡음** (xorshift) |
| 11 analog | 없음 | 무엇이든 | 끊김 | ext 또는 Z | **항상 0** |

"외부 구동이 약한 풀 저항(F4 데이터시트 기준 typ. 40 kΩ 대 — 확인 권장)을 이긴다" 와 "analog 모드는 슈미트 트리거(Schmitt trigger) 입력이 꺼져 IDR 이 0" 은 RM0090 의 I/O 구조 설명을 모델로 옮긴 것이다. AF 모드에서 출력을 내는 주변장치는 모델링하지 않았다 (입력처럼 취급).

**`gpio_init` 단계별 추적** — 리셋 직후 `gpio_init(GPIOA, 5, OUTPUT, NONE, LOW, PP, 0)` 의 실제 접근 로그 (`./solution -v`):

```text
  -- gpio_init(GPIOA, 5, OUTPUT, NONE, LOW, PP, 0) from reset (11 accesses)
   0  R  RCC_AHB1ENR    0x00100000
   1  W  RCC_AHB1ENR    0x00100001
   2  R  RCC_AHB1ENR    0x00100001
   3  R  GPIOA_OTYPER   0x00000000
   4  W  GPIOA_OTYPER   0x00000000
   5  R  GPIOA_OSPEEDR  0x0C000000
   6  W  GPIOA_OSPEEDR  0x0C000000
   7  R  GPIOA_PUPDR    0x64000000
   8  W  GPIOA_PUPDR    0x64000000
   9  R  GPIOA_MODER    0xA8000000
  10  W  GPIOA_MODER    0xA8000400
```

| 단계 | 계산 | 결과 |
|---|---|---|
| 0–2 | ENR 리셋 0x00100000 (bit20 CCMDATARAMEN, RM0090 기준) `\|` bit0 → read-back | 0x00100001, bit20 보존 |
| 3–4 | OTYPER: `field_put(0, 5, 0x1, 0)` | 0 (값은 같아도 쓴다 — 결정적 설정) |
| 5–6 | OSPEEDR: s=10, `(0x0C000000 & ~(3<<10)) \| (0<<10)` | 0x0C000000 (PA13 의 11 보존) |
| 7–8 | PUPDR: s=10, 풀 none | 0x64000000 (SWD 풀 보존) |
| 9–10 | MODER: `(0xA8000000 & ~0xC00) \| (1<<10)` | **0xA8000400** |

MODER 를 마지막에 쓰는 이유: 핀이 "출력/AF 로 구동되기 시작하는 순간" 은 MODER 가 바뀌는 순간이다. 그 전에 출력 타입·속도·풀·AF 번호가 이미 맞아 있어야 한 순간이라도 엉뚱한 상태(예: AF 로 바꿨는데 AFR 은 아직 AF0 = 시스템 기능)로 핀을 구동하지 않는다. STM32CubeF4 의 `HAL_GPIO_Init()` 도 대체로 속도/타입 → 풀 → AFR → MODER 순서다 (버전에 따라 다를 수 있음 — 소스 확인 권장).

**write / read / toggle** 은 접근 로그가 곧 명세다:

| 호출 | 접근 로그 | 이유 |
|---|---|---|
| `gpio_write(GPIOA, 5, 1)` | `W BSRR 0x00000020` | set 절반. 읽기 없음 → ISR 이 다른 핀을 바꿔도 안전 |
| `gpio_write(GPIOA, 5, 0)` | `W BSRR 0x00200000` | reset 절반 = bit 5+16 = 21 |
| `gpio_read(GPIOA, 5)` | `R IDR` | 출력 모드에서도 IDR 은 핀을 샘플한다 |
| `gpio_toggle(GPIOA, 5)` (ODR=0x20) | `R ODR 0x20`, `W BSRR 0x00200000` | ODR 기준, 그 핀만 쓰기 |

`ODR ^= 1u << pin` 도 결과는 같지만 **ODR 16비트 전체를 다시 쓴다**. 읽기와 쓰기 사이에 ISR 이 같은 포트의 다른 핀을 바꾸면 그 변경이 사라진다 (lost update — 재현과 고침은 BM-B04). BSRR 토글은 다른 핀의 비트를 쓰지 않으므로 그 경쟁이 없다. 남는 경쟁은 "같은 핀을 ISR 도 토글하는" 경우뿐이고, 그건 설계 문제다.

### Part B — exhaustive 증명과 경계

**무엇을 몇 개 돌리나.** 핀 하나당 설정 수 = input 3·4·2 + output 3·4·2 + AF 3·4·2·16 + analog 1·4·2 = 24 + 24 + 384 + 8 = 440 (analog + 풀 2개는 거부 테스트로 따로). 4 포트 × 16 핀 × 440 = **28160**. 매 설정마다:

1. `model_reset()` 후 4 포트의 레지스터 10개와 `RCC_AHB1ENR` 를 xorshift32 로 랜덤 채움 (ENR 이 랜덤이니 대상 포트 클럭이 꺼져 있는 경우가 절반 — 드라이버가 클럭을 안 켜면 여기서 걸린다).
2. 하네스가 **자기 함수 `t_field()`** 로 기대값을 계산: MODER/OSPEEDR/PUPDR 은 `2*pin`, OTYPER `pin`, AF 면 `AFR[pin>>3]` 의 `4*(pin%8)`.
3. 대상 포트 10개 레지스터 = 기대값, 다른 3 포트 = 스냅샷 그대로, ENR = `old | bit`.
4. 접근 로그: 첫 ENR 쓰기 이전에 GPIO 주소 접근이 없을 것, 마지막 쓰기 = 그 포트 MODER, IDR/ODR/BSRR/LCKR 에는 쓰지 않을 것.

실행 결과: `exhaustive gpio_init: 28160 configs` 와 6개 CHECK 모두 PASS, 0.13 s.

흔한 버그 5종을 같은 mock 에 넣고 돌린 결과 (`demo_bugs.c`, GPIOA, 16 핀 × 4 모드 × 64 시드, ENR·MODER·AFR 랜덤, AF=7 — 실제 출력):

```text
variant (GPIOA, pins 0-15 x 4 modes x 64 seeds)  configs target wrong   others hit
reference (field_put, 2*pin)               4096            0            0
MODER |= mode << 2*pin   (no clear)        4096         1860            0
shift by pin, not 2*pin                    4096         2937         2837
MODER = mode << 2*pin    (overwrite)       4096            0         4096
AF always into AFRL                        4096          487          477
no RCC clock enable                        4096         1728            0
```

읽는 법: "지우지 않음" 은 다른 핀은 안 건드리지만 대상 필드가 45 % 틀린다 (예전 값과 OR). "덮어쓰기" 는 대상은 항상 맞는데 **다른 핀이 100 % 망가진다** — 실보드라면 PA13/PA14 가 input 이 되어 디버거가 끊긴다. "2*pin 대신 pin" 은 양쪽 다 망친다. 이 표가 랜덤 프리필 + exhaustive 테스트가 필요한 이유다: 리셋 MODER 가 0 인 포트(GPIOC/D)에서 핀 하나만 테스트하면 "덮어쓰기" 는 레지스터 전체 값을 비교해도 통과하고, "지우지 않음" 도 00 → 01 처럼 처음 설정할 때는 통과한다.

**경계 테스트** (전부 PASS):

| 경우 | 기대 | 왜 |
|---|---|---|
| PB7 AF4 → PB8 AF4 | AFRL 0x40000000, AFRH 0x00000004, OTYPER 0x0180 | 핀 8 에서 레지스터가 바뀌고 시프트가 0 으로 돌아온다 (I2C1 SCL/SDA 배치 흉내) |
| PD15 AF15 풀다운 OD | MODER 0x80000000, AFRH 0xF0000000, PUPDR 0x80000000, OTYPER 0x8000 | 최상위 필드 — 시프트 30, 28 |
| PD0 analog | MODER 0x80000003 | 최하위 필드 |
| `gpio_write(GPIOD, 15, 0)` | `W BSRR 0x80000000` | `1u << 31`. `1 << 31` 은 UB (아래 demo) |

`demo_shift_ub.c` 에서 `(uint32_t)(1 << (pin + 16))` 을 pin = 15 로 돌리면 UBSan 이 실제로 이렇게 멈춘다:

```text
demo_shift_ub.c:7:69: runtime error: left shift of 1 by 31 places cannot be represented in type 'int'
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior demo_shift_ub.c:7:69
```

(새니타이저 없이 -O2 로는 0x80000000 이 "우연히" 나온다 — 그래서 더 위험하다. C11 6.5.7p4.)

**잘못된 인자 정책.** 12종 (모르는 포트 0x12345678, pin 16, mode 4, pull 3(reserved), speed 4, otype 2, af 16, analog + 풀업, write pin 16, read pin 99, toggle GPIOE(이 mock 에 없음), clock_enable(0)) 모두 `GPIO_EINVAL` 이고 접근 로그 0줄. "검사 먼저, 접근 나중" 은 반쯤 설정된 핀을 남기지 않기 위해서다. analog + 풀을 거부하는 것은 이 레슨의 정책이다 — RM0090 은 analog 모드에서 풀 저항이 끊긴다고 설명하므로 그 조합은 설정 실수일 가능성이 높다.

**클럭 선행.** 모델이 먼저 맞는지 확인한다: 리셋 상태(GPIOC 클럭 꺼짐)에서 `REG_WRITE(GPIOC+MODER, …)` → 저장값 그대로 0, 외부가 PC13 을 HIGH 로 구동해도 `gpio_read` → 0. 그다음 ENR = 0x00100002 (GPIOB 만 켜짐) 에서 `gpio_init(GPIOC, 13, …)` 의 로그 앞 세 줄이 `R ENR 0x00100002 / W ENR 0x00100006 / R ENR 0x00100006` 인지 본다. read-back 의 의미: 실칩에서 RCC 쓰기가 버스에서 끝나기 전에 GPIO 접근이 나가면 첫 접근이 무시될 수 있다 (STM32 errata 의 "delay after an RCC peripheral clock enabling" — ES0182 류, 확인 권장). 같은 RCC 레지스터를 다시 읽으면 앞선 쓰기가 끝난 뒤에야 읽기가 완료되므로 자연스러운 지연이 된다. 이 레슨의 mock 은 클럭이 즉시 켜지는 단순 모델이고, 지연까지 모델링한 것은 BM-B03 이다.

**입력 세 경우.** PA1 input no-pull, 외부 FLOAT 로 64번 읽기 → 실제 출력 `floating PA1, 64 reads -> 33 ones`. 실보드에서도 떠 있는 입력은 손을 갖다 대거나 옆 선이 토글할 때마다 바뀌고, 입력 버퍼가 중간 전압에 걸려 관통 전류(shoot-through)까지 흘린다 — 쓰지 않는 핀은 analog 로 두는 것이 저전력 기본기다 (BM-M11). 풀업을 켜면 1, 외부 LOW 가 오면 0, 풀다운이면 0, 외부 HIGH 면 1. analog 로 바꾼 PA0 은 외부 HIGH 에서도 0.

**open-drain 과 IDR ≠ ODR.** PB9 을 open-drain + 풀업, ODR=1(놓음)으로 둔 뒤 다른 장치가 라인을 0 으로 누른다 (I2C SDA 상황). IDR=0, ODR bit9=1. `demo_bugs.c` 의 토글 3종 실제 로그:

```text
toggle via BSRR from ODR (correct) -> ODR bit9 1 -> 0
   0  R  GPIOB_ODR      0x00000200
   1  W  GPIOB_BSRR     0x02000000
toggle via ODR ^= (RMW whole ODR) -> ODR bit9 1 -> 0
   0  R  GPIOB_ODR      0x00000200
   1  W  GPIOB_ODR      0x00000000
toggle via BSRR from IDR (wrong source) -> ODR bit9 1 -> 1
   0  R  GPIOB_IDR      0x00000836
   1  W  GPIOB_BSRR     0x00000200
```

IDR 기준 토글은 "0 이네 → 1 로" 를 쓰므로 아무것도 안 바뀐다. IDR 의 다른 비트(0x836)가 떠 있는 핀들의 잡음이라는 것도 보인다. 실칩에서는 IDR 이 입력 동기화기(synchronizer)를 거쳐 몇 클럭 늦게 갱신된다는 점도 IDR 기준 토글을 피하는 이유다 (출력 직후 바로 읽으면 옛 값).

### Part C — 보드 초기화와 글리치

**board_init 손 계산** (리셋 값에서 출발):

| 레지스터 | 리셋 | 바뀌는 필드 | 결과 (`./solution -v` 실제 출력과 일치) |
|---|---|---|---|
| GPIOA MODER | 0xA8000000 | PA2 AF `10<<4`=0x20, PA3 AF `10<<6`=0x80, PA5 out `01<<10`=0x400 | **0xA80004A0** |
| GPIOA OSPEEDR | 0x0C000000 | PA2/PA3 fast `10` → 0x20 + 0x80 | 0x0C0000A0 |
| GPIOA PUPDR | 0x64000000 | PA3 up `01<<6` = 0x40 | 0x64000040 |
| GPIOA AFRL | 0 | PA2 `7<<8`, PA3 `7<<12` | 0x00007700 |
| GPIOB MODER / OSPEEDR / ODR | 0x280 / 0xC0 / 0 | PB6 out `01<<12`, medium `01<<12`, ODR bit6 | 0x1280 / 0x10C0 / 0x0040 |
| GPIOC PUPDR | 0 | PC13 up `01<<26` | 0x04000000 |
| RCC_AHB1ENR | 0x00100000 | GPIOA/B/C EN | 0x00100007 |

```text
  GPIOA MODER=A80004A0 OTYPER=0000 OSPEEDR=0C0000A0 PUPDR=64000040 ODR=0000 AFRL=00007700 AFRH=00000000
  GPIOB MODER=00001280 OTYPER=0000 OSPEEDR=000010C0 PUPDR=00000100 ODR=0040 AFRL=00000000 AFRH=00000000
  GPIOC MODER=00000000 OTYPER=0000 OSPEEDR=00000000 PUPDR=04000000 ODR=0000 AFRL=00000000 AFRH=00000000
  RCC_AHB1ENR=00100007
```

`MODER >> 26 == 0x2A` (= `10 10 10`) 검사가 "SWD 가 살아 있다" 는 뜻이다. LED PA5 / 버튼 PC13 / USART2 PA2·PA3 AF7 은 Nucleo-F4 보드 배치를 흉내 낸 것이다 (보드 매뉴얼·데이터시트 AF 표 확인 권장; Nucleo 의 버튼은 외부 풀업이 있지만 여기선 내부 풀업으로 모델링).

**CS 글리치.** SPI 플래시의 CS 는 active-low 다. 리셋 직후 PB6 은 입력(Z) — 보드에 외부 풀업이 있다면 1 로 잡혀 있다. 흔한 순서 "출력으로 바꾸고 → 1 쓰기" 는 그 사이 ODR 리셋 값 0 이 그대로 나간다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">PB6 (플래시 CS, active-low) 레벨 — mock 의 pin_history 와 같은 순서</text>
  <g fill="currentColor"><text x="10" y="70">흔한 순서</text><text x="10" y="86">history "01"</text></g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="170" y="40">리셋</text><text x="260" y="40">OTYPER</text><text x="350" y="40">OSPEEDR</text>
    <text x="440" y="40">PUPDR</text><text x="530" y="40">MODER=01</text><text x="640" y="40">BSRR BS6</text>
  </g>
  <path d="M120 75H485" stroke="currentColor" stroke-dasharray="5 4" fill="none"/>
  <path d="M485 75V100H595V54H730" stroke="currentColor" stroke-width="2" fill="none"/>
  <rect x="485" y="56" width="110" height="44" class="f-warn" opacity="0.6"/>
  <text x="490" y="118" fill="currentColor">CS=0 → 플래시가 선택됨 (글리치)</text>
  <g fill="currentColor"><text x="10" y="180">정답 순서</text><text x="10" y="196">history "1"</text></g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="170" y="150">리셋</text><text x="260" y="150">BSRR BS6</text><text x="350" y="150">OTYPER</text>
    <text x="440" y="150">OSPEEDR</text><text x="530" y="150">PUPDR</text><text x="640" y="150">MODER=01</text>
  </g>
  <path d="M120 185H595" stroke="currentColor" stroke-dasharray="5 4" fill="none"/>
  <path d="M595 185V164H730" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="200" y="210" fill="currentColor">ODR bit6 = 1 은 입력 모드에서 이미 래치됨 (핀은 아직 Z)</text>
  <text x="10" y="238" fill="currentColor">점선 = Z (구동 안 함, 외부 풀업이 있으면 1). 굵은 선 = MCU 가 구동.</text>
  <text x="10" y="258" fill="currentColor">0 구간 길이: 실칩에서 수 버스 클럭, 그 사이 IRQ 가 끼면 ISR 길이만큼 늘어난다.</text>
</svg>
```

실제 출력: `naive CS init, PB6 history: "01"` / board_init 의 PB6 history `"1"`. 글리치가 짧아도 문제인 이유: SPI 플래시는 CS 하강 엣지에서 명령 상태 머신을 리셋하고, 리셋 라인·전원 enable·모터 드라이버 enable 같은 핀은 수백 ns 펄스에도 반응한다. Don 의 SSD/RF 보드라면 "부팅 때마다 PA 가 한 번 켜졌다 꺼진다" 류의 증상이다. 보드 쪽 해법(외부 풀업/풀다운)과 펌웨어 쪽 해법(래치 먼저)을 둘 다 말할 수 있어야 한다.

**버튼 → LED 루프.** `app_step()` 은 PC13 을 읽어 "안 눌림 → 눌림" 엣지에서만 LED 를 토글한다. 외부 시퀀스 FLOAT FLOAT LOW LOW FLOAT LOW LOW LOW FLOAT FLOAT 를 넣으면 presses = 2, PA5 history = `"10"`. 실제 버튼은 바운스(bounce)가 있어 이 코드로는 여러 번 토글된다 — 디바운스와 EXTI 인터럽트는 BM-M03 의 몫이다.

**충돌(contention).** 버튼이 눌려 외부가 PC13 을 0 으로 잡고 있는데 실수로 PC13 을 push-pull 1 로 설정하면 MCU 출력과 외부가 싸운다. mock 은 `history "0X"`, `g_contention == 1` 로 잡는다. 실보드에서는 버튼이 GND 로 직결이면 출력 단의 단락 전류가 흐른다 (데이터시트의 핀당 최대 전류 초과 가능). 핀 번호 하나 틀린 board_init 이 하드웨어를 상하게 할 수 있는 이유이고, 연습 4 의 "중복 핀 검사" 가 필요한 이유다.

**타깃 빌드.** 드라이버 코드는 `REG_READ/REG_WRITE` 만 쓰므로 `UNIT_TEST` 를 빼고 드라이버 부분만 별도 파일로 빌드하면 그대로 MMIO 코드가 된다. `demo_target.c` 를 Cortex-M4 로 컴파일한 실제 asm (Apple clang 21, 지시어·레이블 줄 제외):

```text
led_on:                                 @ @led_on
	movs	r0, #24
	movt	r0, #16386
	movs	r1, #32
	str	r1, [r0]
	bx	lr
led_toggle_bsrr:                        @ @led_toggle_bsrr
	movs	r0, #20
	movt	r0, #16386
	ldr	r1, [r0]
	lsls	r1, r1, #26
	mov.w	r1, #2097152
	it	pl
	movpl	r1, #32
	str	r1, [r0, #4]
	bx	lr
led_toggle_xor:                         @ @led_toggle_xor
	movs	r0, #20
	movt	r0, #16386
	ldr	r1, [r0]
	eor	r1, r1, #32
	str	r1, [r0]
	bx	lr
gpio_write_rt:                          @ @gpio_write_rt
	cmp	r2, #0
	it	eq
	addeq	r1, #16
	movs	r2, #1
	lsl.w	r1, r2, r1
	str	r1, [r0, #24]
	bx	lr
gpio_read_rt:                           @ @gpio_read_rt
	ldr	r0, [r0, #16]
	lsrs	r0, r1
	and	r0, r0, #1
	bx	lr
```

`movt r0, #16386` = 0x4002 → r0 = 0x40020018 (GPIOA_BSRR). `led_on` 은 `str` 하나 — "원자적" 의 실체다. 토글은 r0 = ODR 주소(0x40020014)에서 읽고, bit5 를 `lsls #26` 으로 부호 비트에 올려 `it pl` 로 set/reset 값을 고른 뒤 BSRR = `[r0, #4]` 에 쓴다. 런타임 핀 `gpio_write_rt` 도 분기 없이 `it eq` + `lsl.w` + `str` 한 번이다. `ODR ^=` 버전은 `ldr / eor / str` — 이 세 명령 사이가 BM-B04 의 경쟁 창이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/BM-B11_so && /tmp/BM-B11_so      # 73/73 passed
/tmp/BM-B11_so -v                                                                         # 접근 로그 + board_init 레지스터
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/BM-B11_as && /tmp/BM-B11_as   # 73/73, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `MODER \|= mode << (2*pin)` | 옛 값을 안 지움 → 01 위에 10 = 11(analog). demo: 대상 오류 1860/4096 | `field_put(MODER, 2*pin, 0x3, mode)` |
| `MODER = mode << (2*pin)` | 다른 15핀이 input 이 됨 — PA13/PA14 면 SWD 끊김. demo: 4096/4096 | 읽고-지우고-넣기 (RMW) |
| 시프트를 `pin` 으로 (`_Pos` 를 핀 번호로 착각) | 2비트 필드는 `2*pin`. 대상·이웃 둘 다 망침 | `s2 = 2u * pin` |
| AF 를 항상 AFRL 에 / `AFRL` 에 `4*pin` 시프트 | 핀 8–15 는 AFRH. `4*15 = 60` 시프트는 UB | `afr = pin < 8 ? AFRL : AFRH`, `4u * (pin & 7u)` |
| `1 << (pin + 16)` | pin 15 에서 `1 << 31` = signed 오버플로 (UBSan 이 잡음) | `1u << (pin + 16u)` |
| `ODR ^= 1u << pin`, `ODR \|= …` | ODR 전체 RMW — ISR 과 경쟁 (BM-B04) | BSRR 쓰기 |
| IDR 을 읽어 토글 | open-drain·충돌·동기화 지연 때 IDR ≠ ODR → 토글 안 됨 | ODR 을 읽고 BSRR 로 |
| 클럭 enable 전에 GPIO 설정 | 쓰기 무시, 읽기 0 — "레지스터 값이 안 바뀌어요" | `gpio_clock_enable()` 먼저 (+ read-back) |
| ENR 에 `= bit` 대입 | 다른 포트·CCM RAM 클럭이 꺼짐 | `ENR \|= bit` (RMW) |
| MODER 먼저, 값은 나중 | 출력으로 바뀌는 순간 ODR 리셋 값 0 이 나감 (CS 글리치) | BSRR 로 초기값 → MODER |
| 인자 검사 없이 바로 쓰기 / 쓰다가 중간에 검사 | 반쯤 설정된 핀이 남음 | 검사 전부 → 그다음 접근 |
| 쓰지 않는 핀을 input no-pull 로 방치 | 떠 있는 입력 = 잡음 + 관통 전류 | analog 모드 (또는 풀 지정) |

## 8. 면접 Q&A

<details>
<summary>Q1. REG_READ/REG_WRITE 는 왜 volatile 포인터인가요? volatile 이면 충분한가요?</summary>

컴파일러는 일반 메모리라고 생각하면 같은 주소 두 번 읽기를 한 번으로 합치고, 결과를 안 쓰는 읽기(RCC read-back, 상태 레지스터 dummy read)를 지우고, 같은 주소 두 번 쓰기 중 앞의 것을 지운다 (LCKR 키 시퀀스 1→0→1 이 마지막 쓰기 하나로 줄어든다!). `volatile` 은 "이 접근은 관찰 가능한 동작이니 프로그램 순서대로, 생략 없이" 를 강제한다. 하지만 **CPU·버스 수준 순서**까지 보장하지는 않는다. Cortex-M3/M4 는 Device 메모리 접근 순서를 지키지만, 쓰기 버퍼 때문에 "쓰기가 주변장치에 도착했다" 는 보장은 없다 — 그래서 RCC enable 뒤 read-back 이나 `__DSB()` 를 쓴다. 꼬리: "구조체 `GPIO_TypeDef` 방식과 차이는?" → 같은 volatile 접근이다. CMSIS 는 `__IO` = `volatile` 매크로로 필드에 붙인다 (BM-B01).

</details>

<details>
<summary>Q2. BSRR 은 왜 있나요? ODR 에 쓰면 안 되나요?</summary>

ODR 로 핀 하나를 바꾸려면 읽고-바꾸고-쓰기(`ldr/orr/str`) 세 명령이 필요하고, 그 사이 ISR 이 같은 포트의 다른 핀을 바꾸면 메인의 `str` 이 그 변경을 덮는다. BSRR 은 쓰기 전용 레지스터로 "1 을 쓴 비트만 set(하위 16)/reset(상위 16), 0 인 비트는 무관" 이라 `str` 한 번이 그 자체로 원자적이다. 인터럽트를 끌 필요가 없다. 같은 비트에 set 과 reset 이 둘 다 1 이면 set 이 우선한다 (RM0090 기준). 꼬리: "F1/F0/L4 의 BRR 은?" → reset 전용 16비트 레지스터 (F4 에는 없음; 옛 F4 SPL 헤더는 BSRR 을 `BSRRL`/`BSRRH` 16비트 둘로 나눠 정의했다 — 확인 권장). "Cortex-M3/M4 의 다른 방법?" → 비트밴딩(bit-banding) 별칭 주소 쓰기도 원자적이다 (BM-M 레인).

</details>

<details>
<summary>Q3. 토글할 때 IDR 이 아니라 ODR 을 읽는 이유는? ODR ^= 는 왜 안 되죠?</summary>

ODR 은 "내가 내보내라고 한 값", IDR 은 "핀에서 실제로 읽힌 값" 이다. 둘은 다를 수 있다: open-drain 에서 1(놓음)을 냈는데 다른 장치가 0 으로 누를 때, 외부와 충돌할 때, 출력 직후 IDR 이 동기화기를 거쳐 아직 갱신되지 않았을 때. IDR 기준 토글은 이 레슨 demo 에서 "1 → 1" (안 바뀜) 이었다. `ODR ^=` 는 값은 맞지만 16비트 전체를 다시 쓰는 RMW 라 Q2 의 경쟁이 그대로 있다. 정답 = `ODR` 읽기 + `BSRR` 쓰기 (쓰는 쪽은 그 핀 비트만). 꼬리: "그것도 RMW 아닌가?" → 읽기와 쓰기 사이에 다른 핀이 바뀌어도 영향 없음. 같은 핀을 ISR 과 메인이 둘 다 토글한다면 그건 소유권 설계 문제다.

</details>

<details>
<summary>Q4. 클럭 enable 을 안 하면 어떻게 되나요? enable 직후 바로 쓰면? 왜 dummy read 를 하죠?</summary>

STM32 주변장치는 RCC 의 클럭 게이트가 닫혀 있으면 레지스터 쓰기가 무시되고 읽으면 0 이 나온다 (이 레슨 mock 이 그 규칙). "MODER 에 썼는데 디버거로 보면 0 이에요" 의 1번 원인이다. enable 직후 바로 접근하면, RCC 쓰기가 아직 버스에서 진행 중일 때 GPIO 접근이 나가 첫 접근이 무시될 수 있다 — ST errata 에 "delay after an RCC peripheral clock enabling" 으로 나오는 항목이다 (ES0182 등, 확인 권장). 해법은 RCC 레지스터를 한 번 다시 읽는 것: 같은 버스의 읽기는 앞선 쓰기가 끝난 뒤 완료되므로 필요한 지연이 생긴다. CubeHAL 의 `__HAL_RCC_GPIOA_CLK_ENABLE()` 매크로 안에도 `tmpreg = READ_BIT(...)` 와 `UNUSED(tmpreg)` 가 들어 있다. 지연을 모델링한 mock 은 BM-B03.

</details>

<details>
<summary>Q5. 누가 GPIOA->MODER = 0x400; 을 넣었더니 디버거가 안 붙습니다. 왜고, 어떻게 복구하나요?</summary>

GPIOA 리셋 값 MODER = 0xA8000000 은 PA13(SWDIO)/PA14(SWCLK)/PA15 를 AF 로 두어 SWD/JTAG 가 리셋 직후부터 동작하게 한다. 대입 한 줄이 이 필드를 00(input)으로 만들어 main 이 도는 순간 SWD 가 끊긴다. 복구: ① 디버거의 "connect under reset" (NRST 를 잡은 채 연결 → 펌웨어가 핀을 바꾸기 전에 코어를 정지) ② BOOT0 핀으로 시스템 메모리 부트로더로 부팅해 플래시 지우기. 예방: RMW 필드 쓰기 + 이 레슨처럼 "PA13/14 는 AF 그대로" 를 테스트로 박아 두기. 실제 리셋 값은 RM0090 GPIO 레지스터 장 확인 권장.

</details>

<details>
<summary>Q6. 풀업/풀다운, push-pull/open-drain, 속도(speed) 설정은 각각 언제 무엇을 고르나요?</summary>

풀: 입력이 떠 있지 않게 기본값을 정한다. 버튼(GND 로 누름) → 풀업, active-high 신호의 기본 0 → 풀다운. 내부 풀은 약하다(수십 kΩ) — I2C 처럼 속도가 필요한 버스는 외부 풀업(수 kΩ, HWLAB-B03 rise time). Open-drain: 여러 장치가 한 선을 공유(I2C, wired-OR 인터럽트 라인), 또는 외부 풀업으로 다른 전압 레벨에 맞출 때 (5V-tolerant 핀인지 데이터시트 확인). Push-pull: 그 외 일반 출력 — 양쪽으로 세게 구동해 엣지가 빠르다. Speed(OSPEEDR): 출력 드라이버의 슬루율(slew rate)이다. 빠를수록 엣지가 가팔라 EMI·링잉·그라운드 바운스가 커지므로 **타이밍을 만족하는 가장 낮은 값** 을 고른다 (LED 는 low, SPI 수십 MHz 는 high). 입력 전용 핀에는 의미가 없다. 꼬리: "속도 단계 이름?" → RM0090 개정판마다 low/medium/fast/high 또는 low/medium/high/very high 로 표기가 다르다 — 숫자(00–11)로 말하는 게 안전.

</details>

<details>
<summary>Q7. 부팅 중 CS 핀이 한 번 0 으로 튀는 걸 스코프에서 봤습니다. 원인과 해결은?</summary>

출력으로 바꾸는(MODER = 01) 순간 ODR 의 리셋 값 0 이 나가고, 그다음 줄에서 1 을 쓰기 때문이다. 해결: ① 펌웨어 — 입력 모드인 동안 BSRR 로 ODR 에 1 을 먼저 래치하고 MODER 를 마지막에 (이 레슨 `gpio_init_output`). ② 하드웨어 — CS 에 외부 풀업을 달아 리셋 ~ 초기화 사이 Z 구간에도 1 로 유지. 둘 다 해야 리셋 직후 부트로더 구간까지 안전하다. 꼬리: "AF 핀도?" → AF 로 바꾸기 전에 AFR 번호를 먼저 써야 엉뚱한 AF0 기능이 순간 구동하지 않는다. 주변장치(SPI/UART) 자체는 핀을 AF 로 넘기기 전에 설정·enable 해 두는 편이 idle 레벨이 안정적이다.

</details>

<details>
<summary>Q8. 실보드에서 GPIO 드라이버를 어떻게 검증하나요? mock 테스트로 충분한가요?</summary>

mock 은 "레지스터에 무엇을 어떤 순서로 썼나" 를 증명한다 (이 레슨: 28160 설정 exhaustive, 접근 순서). 실보드는 "그 레지스터 값이 전기적으로 맞는 동작을 하나" 를 본다: ① 디버거로 레지스터 덤프를 RM 리셋 값·기대값과 비교 (연습 2 의 `gpio_get_config` 같은 셸 명령) ② 루프백 — 출력 핀을 점퍼로 다른 입력 핀에 연결해 write/read 를 자동 확인 ③ 스코프/로직 분석기로 엣지·글리치·슬루율 확인 ④ 풀업 측정: 핀을 떠 있게 두고 IDR, 외부 저항 연결 후 전압. HIL 랙에서는 ②를 매 빌드 돌린다. mock 이 잡지 못하는 것: 실제 전기 특성(구동 전류, 슬루율), errata, 다른 주변장치와 AF 충돌.

</details>

## 9. 추가 연습

### 연습 1 — 여러 핀을 한 번에: `gpio_write_masked(port, mask, value)`

8비트 병렬 버스나 7세그먼트처럼 여러 핀이 **같은 순간에** 바뀌어야 한다. `mask` 의 핀들을 `value` 의 해당 비트로 만드는 함수를 BSRR 쓰기 한 번으로 구현하라. `mask = 0x00F0, value = 0x0050` 이면 BSRR 에 무엇을 쓰나?

<details>
<summary>답 보기</summary>

set = `value & mask` = 0x0050, reset = `~value & mask` = 0x00A0 → BSRR = `(0x00A0 << 16) | 0x0050` = **0x00A00050**. ODR 0x00F0 → 0x0050. 랜덤 2000건을 `ODR = (odr & ~m) | (v & m)` 기준으로 대조. `mask == 0` 이면 접근하지 않는 정책. `ODR = (ODR & ~m) | v` 로 하면 RMW 이고, 핀을 하나씩 쓰면 버스 값이 중간 상태를 거친다 (예: 0x0F → 0x10 사이에 0x1F 가 잠깐 보임). `~value` 는 int 로 승격되므로 `& 0xFFFF` 로 자른다 (C-B02).

</details>

### 연습 2 — 역으로 읽기: `gpio_get_config(port, pin, &cfg)`

디버그 셸의 "pin dump" 명령처럼 레지스터에서 핀 설정을 해석하라. `gpio_init` 과의 왕복(round trip)을 한 포트의 전 설정(16 × 440 = 7040)에 대해 확인하고, 리셋 상태의 PA13/PA14 를 해석하라.

<details>
<summary>답 보기</summary>

각 필드를 같은 시프트로 꺼낸다: mode = `MODER >> 2*pin & 3`, otype = `OTYPER >> pin & 1`, speed / pull 도 2비트, af = `AFR[pin>>3] >> 4*(pin&7) & 0xF`. 7040 건 왕복 일치. 리셋 해석: PA13 = AF, AF0 (= SWDIO), 풀업, speed 11; PA14 = AF, AF0 (= SWCLK), 풀다운, speed 00. 디버그 포트는 리셋 직후부터 쓸 수 있어야 하므로 이렇게 정해져 있다. 읽기는 클럭이 켜져 있어야 하므로 먼저 `gpio_clock_enable()`.

</details>

### 연습 3 — 설정 잠금: `gpio_lock(port, mask)` (LCKR)

안전 관련 핀(모터 enable 등)은 초기화 뒤 런어웨이 코드가 설정을 못 바꾸게 잠근다. 잠금 키 시퀀스를 구현하고, 잠긴 핀에 `gpio_init` 이 오면 `GPIO_ELOCKED` 를 돌려주게 하라. mock 에는 LCKR 상태 머신을 추가한다.

<details>
<summary>답 보기</summary>

시퀀스 (RM0090 기준 — 확인 권장): `W LCKR = LCKK(bit16) | mask` → `W mask` → `W LCKK | mask` → `R` → `R` (LCKK=1 확인). 세 번의 쓰기 동안 [15:0] 이 같아야 하고 순서가 1→0→1 이어야 한다 — 런어웨이 코드가 우연히 잠금/해제하지 못하게 하는 "키" 다. 잠긴 뒤에는 MODER/OTYPER/OSPEEDR/PUPDR/AFRL/AFRH 의 그 핀 필드가 **다음 리셋까지** 고정되고 LCKR 자체도 고정된다. ODR/BSRR(데이터)은 잠기지 않는다. 테스트: 잠금 후 `gpio_init(PA5, INPUT)` → `GPIO_ELOCKED`, MODER 에 0 을 직접 써도 PA5 필드 01 유지, PA6 은 설정 가능, `gpio_write(PA5)` 정상, 다른 마스크로 다시 잠그기 → `GPIO_ELOCKED`, (1,1,1) 이나 중간에 마스크가 바뀐 시퀀스는 잠기지 않음, 리셋 후 해제. 이 키 시퀀스는 volatile 이 없으면 컴파일러가 앞 두 쓰기를 지울 수 있다 (Q1).

</details>

### 연습 4 — 표로 보드 초기화: `board_apply(table, n, &bad)`

실제 펌웨어는 핀 설정을 `const pin_cfg_t board_pins[]` 표로 둔다 (Zephyr pinctrl, CubeMX 생성 코드와 같은 발상). 표 전체를 **먼저 검사**(범위 + 같은 핀 중복)하고 모두 맞을 때만 적용하라. 출력 핀은 글리치 없이.

<details>
<summary>답 보기</summary>

1단계: 각 항목 `pin_cfg_valid()` + 포트별 16비트 `used` 비트맵으로 중복 검사 → 실패하면 `*bad = i`, 레지스터 접근 0번. 2단계: 출력이면 클럭 → BSRR 초기값 → `gpio_init`. 같은 표로 본문 board_init 과 똑같은 레지스터 값 (MODER A 0xA80004A0, AFRL 0x7700, …) 과 PB6 history `"1"`. 테스트: 4번째 항목 pin 16 → bad = 3, 로그 0줄. CS 를 LED 핀(PA5)에 또 배정 → bad = 4, 로그 0줄. 중복 검사는 Part C 의 contention(잘못된 핀을 출력으로) 사고를 표 단계에서 막는다. `const` 표는 Flash(.rodata)에 들어가 RAM 을 쓰지 않는다 (BM-B05).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-B11_pr && /tmp/BM-B11_pr      # 31/31 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o /tmp/BM-B11_pas && /tmp/BM-B11_pas   # 31/31, 보고 없음
```

## 10. 핵심 정리

- 필드 쓰기 = `(old & ~(mask << s)) | ((v & mask) << s)`. 시프트: 2비트 레지스터 `2*pin`, OTYPER `pin`, AFR `4*(pin&7)` + 레지스터 `pin < 8 ? AFRL : AFRH`.
- `gpio_init` 순서: 인자 검사(실패 시 접근 0) → RCC 클럭 RMW + read-back → OTYPER → OSPEEDR → PUPDR → AFR → **MODER 마지막**.
- write = BSRR 쓰기 1번 (`1u << pin` / `1u << (pin+16)`), read = IDR 읽기 1번, toggle = **ODR** 읽기 + BSRR 쓰기. `ODR ^=` 는 RMW 경쟁(BM-B04), IDR 기준은 open-drain 에서 틀린다.
- 클럭이 꺼진 주변장치: 쓰기 무시, 읽기 0. enable 직후엔 read-back 으로 지연 (errata, BM-B03).
- GPIOA 리셋 MODER 0xA8000000 = PA13/14/15 AF (SWD). MODER 를 대입으로 덮으면 디버거가 끊긴다 → 복구는 connect-under-reset / BOOT0.
- 입력: 외부 구동 > 풀 > 떠 있음(잡음). analog 는 IDR 0, 쓰지 않는 핀은 analog 로.
- 글리치 없는 출력: 입력 모드에서 BSRR 로 초기값을 래치 → MODER. 외부 풀업도 함께.
- 테스트는 랜덤 프리필 + exhaustive (28160 설정) + 접근 로그 순서 검사. 핀 하나짜리 테스트는 "MODER 덮어쓰기" 버그를 통과시킨다.

## 참고자료

- ST RM0090 *STM32F405/415, 407/417, 427/437, 429/439 Reference Manual* — 8장 GPIO (I/O 구조, 레지스터 맵·리셋 값, BSRR 우선순위, LCKR 잠금 시퀀스, analog 모드 IDR), 6/7장 RCC (AHB1ENR) — <https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf>
- ST ES0182 *STM32F405/407xx and STM32F415/417xx errata sheet* — "Delay after an RCC peripheral clock enabling" (항목 이름·번호 확인 권장) — <https://www.st.com>
- STM32F401/F411 데이터시트 alternate function 표 (USART2 = AF7, I2C1 = AF4), I/O 특성 (풀 저항 값) / Nucleo-F401RE 사용자 매뉴얼 UM1724 (LD2 = PA5, B1 = PC13)
- STM32CubeF4 `stm32f4xx_hal_gpio.c` (`HAL_GPIO_Init` 필드 쓰기 순서, `HAL_GPIO_TogglePin`), `stm32f4xx_hal_rcc.h` (`__HAL_RCC_GPIOA_CLK_ENABLE` 의 read-back) — <https://github.com/STMicroelectronics/STM32CubeF4>
- CMSIS-Core 디바이스 헤더 관례 (`GPIO_TypeDef`, `__IO`) — <https://arm-software.github.io/CMSIS_6/latest/Core/index.html>
- ISO/IEC 9899:2011 §6.5.7 (비트 시프트: `1 << 31` 이 UB 인 근거), §6.7.3 (volatile)
- 관련 레슨: C-B01 (비트 필드 set/clear, 가짜 GPIO 구조체), C-B02 (정수 승격 — `~value`), C-M14 (ISR↔메인 공유 데이터), BM-B01 (CMSIS 헤더 `_Pos/_Msk`), BM-B02 (mock 프레임워크 원본), BM-B03 (RCC 클럭 게이팅·enable 지연 모델), BM-B04 (ODR RMW 경쟁과 BSRR), BM-B05 (const 표 → .rodata), BM-M03 (EXTI·디바운스), BM-M11 (미사용 핀 analog·저전력), HWLAB-B03 (I2C 풀업·rise time), SIV-B01 (레지스터 헤더 생성)
