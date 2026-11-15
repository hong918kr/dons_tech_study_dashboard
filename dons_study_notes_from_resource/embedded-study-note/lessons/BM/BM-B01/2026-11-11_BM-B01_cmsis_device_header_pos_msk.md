---
id: BM-B01
time: 50분
updated: 2026-11-11
---
# BM-B01 벤더 헤더 읽기: CMSIS 디바이스 헤더와 _Pos/_Msk 관례

## 학습 목표

- CMSIS 디바이스 헤더(device header)의 다섯 부분을 구분해 읽는다: 접근 한정자 `__I/__O/__IO`, `xxx_TypeDef` 구조체와 `RESERVEDn` 패딩, `PERIPH_BASE → AHB1PERIPH_BASE → GPIOA_BASE → GPIOA` 주소 체인, `_Pos/_Msk` 비트 매크로, `SET_BIT/MODIFY_REG` 접근 매크로.
- `GPIOC->BSRR`, `RCC->APB1ENR` 같은 식을 보고 **실제 버스 주소**와 **마스크 값**을 손으로 계산한다.
- 레퍼런스 매뉴얼(RM, reference manual)의 오프셋 표를 `_Static_assert(offsetof(...))` 로 코드에 고정해, 패딩 한 칸 실수를 **컴파일 에러**로 잡는다.
- `_VAL2FLD / _FLD2VAL / MODIFY_REG` 를 직접 쓰고, 매크로만으로 GPIO·USART 설정 함수를 작성해 RAM 구조체로 바꿔치기한 테스트 빌드에서 레지스터 값을 검증한다.
- 고전 버그 다섯 가지(`_Pos` 를 마스크로, clear 없는 set, 별칭을 값으로, RMW 대신 덮어쓰기, 패딩 누락)를 값으로 설명한다.

## 1. 문제

새 보드를 받으면 제일 먼저 여는 파일이 `stm32f4xx.h` 같은 벤더 헤더이고, 면접에서도 "이 헤더 조각을 보고 PA5 를 출력으로 만들어 보세요"가 단골 첫 문제다 — HAL 없이 레지스터를 만질 줄 아는지가 여기서 바로 드러난다.

Don 이 SSD 컨트롤러를 bring-up 할 때 쓰던 레지스터 헤더도, RF 모듈 보드의 STM32/nRF 헤더도 같은 모양이다. Arm 의 CMSIS(Common Microcontroller Software Interface Standard)가 그 관례를 정했다: 주변장치(peripheral) 하나 = C 구조체 하나, 구조체 멤버 순서 = 레지스터 오프셋, 구조체 포인터 = 베이스 주소 캐스트. 그래서 `GPIOA->ODR = x;` 는 "주소 `0x4002_0000 + 0x14` 에 32비트 쓰기 한 번" 으로 번역된다.

```svg
<svg viewBox="0 0 740 328" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="bmb01-a1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">GPIOA-&gt;ODR 는 어떤 주소인가 — 베이스 체인 + 구조체 오프셋</text>
  <g stroke="currentColor">
    <rect x="10" y="36" width="380" height="36" rx="6" class="f-mut"/>
    <rect x="10" y="88" width="380" height="36" rx="6" class="f-mut"/>
    <rect x="10" y="140" width="380" height="36" rx="6" class="f-mut"/>
    <rect x="10" y="192" width="380" height="36" rx="6" class="f-mut"/>
    <rect x="10" y="244" width="380" height="36" rx="6" class="f-acc"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="59">PERIPH_BASE                    0x4000_0000</text>
    <text x="20" y="111">AHB1PERIPH_BASE = +0x0002_0000 0x4002_0000</text>
    <text x="20" y="163">GPIOA_BASE = AHB1 + 0x0000     0x4002_0000</text>
    <text x="20" y="215">GPIOA = ((GPIO_TypeDef *) GPIOA_BASE)</text>
    <text x="20" y="267">&amp;GPIOA-&gt;ODR = 0x4002_0000 + 0x14 = 0x4002_0014</text>
  </g>
  <g stroke="currentColor" marker-end="url(#bmb01-a1)">
    <line x1="200" y1="72" x2="200" y2="86"/><line x1="200" y1="124" x2="200" y2="138"/>
    <line x1="200" y1="176" x2="200" y2="190"/><line x1="200" y1="228" x2="200" y2="242"/>
  </g>
  <text x="430" y="36" fill="currentColor" font-weight="bold">GPIO_TypeDef (0x28 바이트)</text>
  <g stroke="currentColor" fill="none">
    <rect x="430" y="44" width="300" height="22"/><rect x="430" y="66" width="300" height="22"/>
    <rect x="430" y="88" width="300" height="22"/><rect x="430" y="110" width="300" height="22"/>
    <rect x="430" y="132" width="300" height="22"/>
    <rect x="430" y="176" width="300" height="22"/><rect x="430" y="198" width="300" height="22"/>
    <rect x="430" y="220" width="300" height="22"/><rect x="430" y="242" width="300" height="22"/>
  </g>
  <rect x="430" y="154" width="300" height="22" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="438" y="60">0x00 __IO MODER</text><text x="438" y="82">0x04 __IO OTYPER</text>
    <text x="438" y="104">0x08 __IO OSPEEDR</text><text x="438" y="126">0x0C __IO PUPDR</text>
    <text x="438" y="148">0x10 __IO IDR</text><text x="438" y="170">0x14 __IO ODR   ← 여기</text>
    <text x="438" y="192">0x18 __IO BSRR</text><text x="438" y="214">0x1C __IO LCKR</text>
    <text x="438" y="236">0x20 __IO AFR[0] (AFRL)</text><text x="438" y="258">0x24 __IO AFR[1] (AFRH)</text>
  </g>
  <text x="10" y="300" fill="currentColor" font-size="12">같은 체인: USART2 = APB1 + 0x4400 = 0x4000_4400, RCC = AHB1 + 0x3800 = 0x4002_3800,</text>
  <text x="10" y="318" fill="currentColor" font-size="12">GPIOB/GPIOC = AHB1 + 0x400/0x800 (RM0090 기준 대표값 — RM 확인 권장)</text>
</svg>
```

이 레슨의 헤더는 ST 파일을 복사한 것이 아니라 **같은 관례로 직접 쓴 짧은 발췌** `device_excerpt.h` 다 (STM32F4 / RM0090 모양). 주소·오프셋·비트·리셋값은 RM0090 기준 대표값이니 실제 칩 작업 전에는 RM 확인 권장. 테스트의 정답은 이 헤더 자체의 정의다. 기술서(SVD, JSON)에서 헤더를 **생성**하는 쪽은 SIV-B01 에서 했다 — 여기서는 반대로 **읽고 쓰는** 쪽이다.

### Part A — 헤더 읽기: 주소·마스크 계산과 RM 오프셋 고정

- 퀴즈 8개: `&GPIOA->ODR`, `&GPIOC->BSRR`, `&USART2->BRR`, `&RCC->APB1ENR` 의 주소, `GPIO_MODER_MODER5_Msk`, `GPIO_AFRL_AFSEL3_Msk`, `USART_CR1_UE_Msk`, `sizeof(GPIO_TypeDef)` 를 헤더만 보고 손으로 계산해 `ANS_*` 상수에 채운다.
- RM0090 오프셋 표(아래 5. 풀이)를 `RM_OFF(T, field, off)` → `_Static_assert(offsetof(T, field) == off, ...)` 로 22줄 고정한다 (시작 코드에 이미 있다 — 통과해야 컴파일된다). `-DDEMO_BAD_RESERVED` 로 패딩 버그를 넣으면 **진짜 컴파일 에러**가 나는지 본다.

### Part B — CMSIS 필드 매크로와 버그 갤러리

- `VAL2FLD(F, v)`, `FLD2VAL(F, r)`, `MODIFY_REG(R, clear, set)` 를 CMSIS `_VAL2FLD/_FLD2VAL`, ST `MODIFY_REG` 와 같은 의미로 쓴다. 토큰 붙이기(`field ## _Pos`)로 필드 이름 하나에서 `_Pos` 와 `_Msk` 를 둘 다 꺼낸다.
- 무작위 (pos, width, value, reg) 2000 개로 정의·왕복·"필드 밖 불변" 속성을 검사한다.
- 주어진 버그 함수 5개의 결과값을 테스트가 그대로 보여 준다 — 왜 그 값이 나오는지 설명할 수 있어야 한다.

### Part C — 매크로만으로 쓴 설정 함수 (RAM 구조체로 검증)

- `led_pa5_init()` / `led_pa5_set(on)` — Nucleo 보드의 LD2 처럼 PA5 에 LED. 클럭 → 출력, push-pull, low speed, 풀 없음. 켜고 끄기는 BSRR.
- `usart2_pins_init()` — PA2 = USART2_TX, PA3 = USART2_RX, 둘 다 AF7 (STM32F407 데이터시트 AF 표 기준 — 확인 권장). TX 는 medium speed, RX 는 풀업.
- `usart2_init(pclk1_hz, baud)` — 8N1, OVER8=0, TE+RE, 마지막에 UE. BRR = round(pclk1/baud). 잘못된 인자는 -1 이고 레지스터를 건드리지 않는다.
- 테스트 빌드(`UNIT_TEST`)는 `#undef GPIOA` → `#define GPIOA (&ram_gpioa)` 로 바꿔치기한다. RM0090 리셋값에서 시작하는 고정 테스트 + 레지스터를 무작위 값으로 채운 500회 테스트로 "대상 필드 밖은 한 비트도 안 바뀐다" 를 확인한다.

## 2. Problem (English)

> **Bring up an LED and a UART using only the vendor's CMSIS device header.**
>
> You are given `device_excerpt.h` (STM32F4-style): `GPIO_TypeDef`, `USART_TypeDef`, `RCC_TypeDef`, base-address macros (`GPIOA`, `USART2`, `RCC`), and `<PERIPH>_<REG>_<FIELD>_Pos/_Msk` bit macros. No HAL.
>
> 1. Without compiling, give the bus address of `&USART2->BRR` and `&RCC->APB1ENR`, and the value of `GPIO_MODER_MODER5_Msk`.
> 2. Lock the header's struct layout against the reference-manual offset table at compile time.
> 3. Implement `VAL2FLD(field, v)`, `FLD2VAL(field, r)` and `MODIFY_REG(reg, clearmask, setmask)` (one read, one write; bits outside `clearmask` untouched; values wider than the field are truncated).
> 4. Using only those macros, write:
>    - `void led_pa5_init(void)` — enable GPIOA clock, PA5 = general-purpose output, push-pull, low speed, no pull. Example: from reset (`MODER = 0xA8000000`), `MODER` becomes `0xA8000400`: bits [11:10] = `01`, PA13/PA14 (SWD) untouched.
>    - `void led_pa5_set(bool on)` — via `BSRR` (on → `0x00000020`, off → `0x00200000`).
>    - `void usart2_pins_init(void)` — PA2/PA3 = AF7; from reset `AFRL = 0x00007700`, `MODER = 0xA80000A0`, `PUPDR = 0x64000040`.
>    - `int usart2_init(uint32_t pclk1_hz, uint32_t baud)` — 8N1, 16x oversampling, TE|RE, then UE. `usart2_init(42000000, 115200)` → `BRR = 0x16D`, `CR1 = 0x200C`. Return -1 and touch nothing if `baud == 0` or the divider is outside `[16, 0xFFFF]`.
> 5. Show how the same driver source is unit-tested on a host.
>
> Follow-ups: why `volatile`? What do `__I`/`__O` actually enforce? Why not `__attribute__((packed))`? Is `MODIFY_REG` atomic? Why configure AFR before MODER?

## 3. 시작 코드

헤더 발췌 (starter / solution / practice / demo 가 모두 include 한다 — 레슨 폴더에서 빌드):

<!-- code: device_excerpt.h -->

<!-- code: starter.c -->

```bash
cd lessons/BM/BM-B01        # device_excerpt.h 를 include 하므로 레슨 폴더에서
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "14/55 passed" (버그 갤러리·잘못된 인자 테스트는 시작 코드로도 통과) — TODO 를 채워 55/55
```

TODO 는 세 군데다: ① `ANS_*` 퀴즈 상수 8개 ② `VAL2FLD / FLD2VAL / MODIFY_REG` 매크로 3개 ③ Part C 함수 4개. `RM_OFF` 줄과 버그 갤러리는 고치지 않는다.

## 4. 힌트

<details>
<summary>힌트 1 — 주소 = 베이스 체인 + offsetof</summary>

`GPIOC_BASE` 를 끝까지 풀어 쓴다: `PERIPH_BASE(0x4000_0000) + 0x2_0000 + 0x0800 = 0x4002_0800`. 거기에 구조체에서 BSRR 의 오프셋을 더한다. 오프셋은 멤버를 위에서부터 4바이트씩 세면 된다 (`uint32_t` 만 있고 패딩은 `RESERVEDn` 으로 명시되어 있으니까). `sizeof(GPIO_TypeDef)` 는 마지막 멤버 `AFR[1]` 의 오프셋 + 4.

</details>

<details>
<summary>힌트 2 — _Msk 는 "필드 폭만큼 1" 을 _Pos 만큼 민 것</summary>

`GPIO_MODER_MODER5_Msk = 0x3UL << 10`. MODER 는 핀당 2비트라 핀 n 의 `_Pos` 는 2n. AFRL 은 핀당 4비트라 AFSEL3 의 `_Pos` 는 12, `_Msk` 는 `0xF << 12`. `_Pos` 는 "몇 칸 미나" 라는 **숫자**이고 `_Msk` 는 **비트 패턴**이다 — 이 둘을 바꿔 쓰는 것이 버그 갤러리 1·2번이다.

</details>

<details>
<summary>힌트 3 — 토큰 붙이기로 _Pos 와 _Msk 를 한 번에</summary>

`#define VAL2FLD(field, value) (((uint32_t)(value) << field ## _Pos) & field ## _Msk)`. `VAL2FLD(GPIO_MODER_MODER5, 1)` 은 전처리기가 `GPIO_MODER_MODER5_Pos` 와 `GPIO_MODER_MODER5_Msk` 로 이어 붙인 뒤 다시 펼친다. 끝의 `& _Msk` 가 넘친 비트를 잘라 준다. `MODIFY_REG` 는 C-B01 의 `field_set` 과 같은 "지우고 → 넣기" 를 한 번 읽고 한 번 쓰는 식으로.

</details>

<details>
<summary>힌트 4 — 설정 함수의 순서</summary>

① 클럭 켜기 (`RCC->AHB1ENR` / `APB1ENR`, 다른 비트 보존 = `SET_BIT`) ② 핀이라면 AF 번호·출력 타입·속도·풀을 먼저, **MODER 는 마지막** (모드가 바뀌는 순간 이미 맞는 설정이어야 글리치가 없다) ③ USART 라면 `UE=0` → 형식 비트 → BRR → `UE=1`. BRR 은 OVER8=0 일 때 `mantissa<<4 | fraction` 이 곧 `USARTDIV × 16` 이므로 `round(pclk/baud)` 하나로 끝난다 — 범위 검사를 먼저 하고, 실패하면 아무 레지스터도 쓰지 않는다.

</details>

## 5. 풀이

### Part A — 헤더의 다섯 부분

| 부분 | 이 레슨 헤더의 예 | 실제 위치 (CMSIS / ST) | 읽을 때 볼 것 |
|---|---|---|---|
| ① 접근 한정자 | `__I` = `volatile const`, `__O` = `volatile`, `__IO` = `volatile` (+ 멤버용 `__IM/__OM/__IOM`) | `core_cm4.h` (CMSIS-Core) | `__O` 는 컴파일러가 아무것도 막지 않는다 — 문서용 |
| ② 구조체 | `GPIO_TypeDef`, `USART_TypeDef`, `RCC_TypeDef` + `RESERVEDn` | `stm32f407xx.h` (디바이스 헤더) | 멤버 순서 = 오프셋, 빈칸은 `RESERVEDn` 으로 채움 |
| ③ 주소 체인 | `PERIPH_BASE → AHB1PERIPH_BASE → GPIOA_BASE → GPIOA` | 디바이스 헤더 | 버스(APB1/APB2/AHB1)가 어디인지 = 어느 RCC 레지스터로 클럭을 켜는지 |
| ④ 비트 매크로 | `GPIO_MODER_MODER5_Pos (10U)`, `_Msk (0x3UL << 10)`, 별칭 `GPIO_MODER_MODER5`, 값 `_0/_1` | 디바이스 헤더 | 접미사 없는 이름 = `_Msk` 별칭 (값이 아니다) |
| ⑤ 접근 매크로 | `SET_BIT / CLEAR_BIT / READ_BIT / WRITE_REG / READ_REG / MODIFY_REG` | `stm32f4xx.h` | `MODIFY_REG` 는 RMW — 원자적이지 않다 |
| (+) 필드 매크로 | `_VAL2FLD / _FLD2VAL` | `core_cm4.h` (CMSIS-Core) | 이 레슨에선 밑줄 없이 직접 구현 |

**퀴즈 풀이** (손 계산 → 테스트가 헤더와 대조):

| 식 | 계산 | 답 |
|---|---|---|
| `&GPIOA->ODR` | `0x4000_0000 + 0x2_0000 + 0x0000 + 0x14` | `0x4002_0014` |
| `&GPIOC->BSRR` | `0x4002_0000 + 0x0800 + 0x18` | `0x4002_0818` |
| `&USART2->BRR` | `0x4000_0000 (APB1) + 0x4400 + 0x08` | `0x4000_4408` |
| `&RCC->APB1ENR` | `0x4002_0000 + 0x3800 + 0x40` | `0x4002_3840` |
| `GPIO_MODER_MODER5_Msk` | `0x3 << 10` | `0x0000_0C00` |
| `GPIO_AFRL_AFSEL3_Msk` | `0xF << 12` | `0x0000_F000` |
| `USART_CR1_UE_Msk` | `1 << 13` | `0x0000_2000` |
| `sizeof(GPIO_TypeDef)` | `AFR[1]` 오프셋 0x24 + 4 | `0x28` = 40 |

**RM0090 오프셋 표** (RM0090 기준 대표값 — RM 확인 권장). 이 표가 `RM_OFF(...)` 22줄의 근거다:

| 주변장치 | 오프셋 → 레지스터 |
|---|---|
| GPIOx | 0x00 MODER, 0x04 OTYPER, 0x08 OSPEEDR, 0x0C PUPDR, 0x10 IDR, 0x14 ODR, 0x18 BSRR, 0x1C LCKR, 0x20 AFRL, 0x24 AFRH |
| USARTx | 0x00 SR, 0x04 DR, 0x08 BRR, 0x0C CR1, 0x10 CR2, 0x14 CR3, (0x18 GTPR — 발췌에서 생략) |
| RCC | 0x10 AHB1RSTR, 0x14 AHB2RSTR, 0x18 AHB3RSTR, **0x1C 예약**, 0x20 APB1RSTR, 0x24 APB2RSTR, **0x28–0x2C 예약**, 0x30 AHB1ENR, 0x34 AHB2ENR, 0x38 AHB3ENR, **0x3C 예약**, 0x40 APB1ENR, 0x44 APB2ENR |

구조체는 "멤버를 순서대로, 각 멤버는 자기 정렬에 맞춰" 배치된다 (C11 6.7.2.1). `uint32_t` 만 있으면 AAPCS(타깃)와 맥 호스트 모두 사이에 숨은 패딩이 없다 — 그래서 RM 표의 빈칸은 **사람이** `RESERVEDn` 으로 채워야 한다. 그 한 칸을 빠뜨리면 그 뒤 모든 레지스터가 4바이트씩 당겨진다:

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">RCC_TypeDef: RESERVED1[2] 를 RESERVED1 (1 워드) 로 쓰면</text>
  <text x="70" y="44" fill="currentColor" font-weight="bold">RM0090 / 올바른 헤더</text>
  <text x="420" y="44" fill="currentColor" font-weight="bold">틀린 헤더 (-DDEMO_BAD_RESERVED)</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="10" y="69">0x18</text><text x="10" y="91">0x1C</text><text x="10" y="113">0x20</text><text x="10" y="135">0x24</text>
    <text x="10" y="157">0x28</text><text x="10" y="179">0x2C</text><text x="10" y="201">0x30</text><text x="10" y="223">0x34</text>
    <text x="10" y="245">0x38</text><text x="10" y="267">0x3C</text><text x="10" y="289">0x40</text><text x="10" y="311">0x44</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="60" y="54" width="270" height="22"/><rect x="60" y="76" width="270" height="22" class="f-mut"/>
    <rect x="60" y="98" width="270" height="22"/><rect x="60" y="120" width="270" height="22"/>
    <rect x="60" y="142" width="270" height="22" class="f-mut"/><rect x="60" y="164" width="270" height="22" class="f-mut"/>
    <rect x="60" y="186" width="270" height="22" class="f-ok"/><rect x="60" y="208" width="270" height="22"/>
    <rect x="60" y="230" width="270" height="22"/><rect x="60" y="252" width="270" height="22" class="f-mut"/>
    <rect x="60" y="274" width="270" height="22" class="f-ok"/><rect x="60" y="296" width="270" height="22"/>
    <rect x="410" y="54" width="270" height="22"/><rect x="410" y="76" width="270" height="22" class="f-mut"/>
    <rect x="410" y="98" width="270" height="22"/><rect x="410" y="120" width="270" height="22"/>
    <rect x="410" y="142" width="270" height="22" class="f-mut"/><rect x="410" y="164" width="270" height="22" class="f-warn"/>
    <rect x="410" y="186" width="270" height="22"/><rect x="410" y="208" width="270" height="22"/>
    <rect x="410" y="230" width="270" height="22" class="f-mut"/><rect x="410" y="252" width="270" height="22" class="f-warn"/>
    <rect x="410" y="274" width="270" height="22" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="70" y="69">AHB3RSTR</text><text x="70" y="91">RESERVED0</text><text x="70" y="113">APB1RSTR</text>
    <text x="70" y="135">APB2RSTR</text><text x="70" y="157">RESERVED1[0]</text><text x="70" y="179">RESERVED1[1]</text>
    <text x="70" y="201">AHB1ENR  (GPIOAEN = bit0)</text><text x="70" y="223">AHB2ENR</text><text x="70" y="245">AHB3ENR</text>
    <text x="70" y="267">RESERVED2</text><text x="70" y="289">APB1ENR  (USART2EN = bit17)</text><text x="70" y="311">APB2ENR</text>
    <text x="420" y="69">AHB3RSTR</text><text x="420" y="91">RESERVED0</text><text x="420" y="113">APB1RSTR</text>
    <text x="420" y="135">APB2RSTR</text><text x="420" y="157">RESERVED1</text><text x="420" y="179">AHB1ENR → 실제론 예약 칸</text>
    <text x="420" y="201">AHB2ENR → 실제 AHB1ENR</text><text x="420" y="223">AHB3ENR → 실제 AHB2ENR</text>
    <text x="420" y="245">RESERVED2</text><text x="420" y="267">APB1ENR → 실제론 예약 칸</text>
    <text x="420" y="289">APB2ENR → 실제 APB1ENR</text>
  </g>
  <text x="690" y="179" fill="currentColor" font-size="12">-4</text>
  <text x="690" y="267" fill="currentColor" font-size="12">-4</text>
</svg>
```

틀린 헤더로 `SET_BIT(RCC->AHB1ENR, RCC_AHB1ENR_GPIOAEN)` 를 하면 쓰기가 `0x4002_382C`(예약 칸)로 간다. 칩은 대개 조용히 무시하고 GPIOA 클럭은 꺼진 채라, 그 뒤 모든 GPIOA 쓰기도 무시된다 (BM-B03 의 클럭 게이팅 모델). 증상은 "LED 가 안 켜진다" 하나뿐이고 디버거로 `RCC->AHB1ENR` 을 보면 **헤더가 틀렸으니 디버거도 틀린 주소를 보여 준다**. 그래서 컴파일 시점에 막는다 — `-DDEMO_BAD_RESERVED` 로 빌드한 실제 출력 (Apple clang 21):

```text
$ cc -std=c11 -Wall -Wextra -Werror -O2 -DDEMO_BAD_RESERVED -fsyntax-only solution.c 2>&1 | grep -E "error|evaluates|generated"
solution.c:33:38: error: static assertion failed due to requirement '__builtin_offsetof(RCC_TypeDef, AHB1ENR) == (48)': RCC_TypeDef.AHB1ENR is not at RM offset 0x30
solution.c:33:38: note: expression evaluates to '44 == 48'
solution.c:34:1: error: static assertion failed due to requirement '__builtin_offsetof(RCC_TypeDef, APB1ENR) == (64)': RCC_TypeDef.APB1ENR is not at RM offset 0x40
solution.c:34:1: note: expression evaluates to '60 == 64'
solution.c:34:38: error: static assertion failed due to requirement '__builtin_offsetof(RCC_TypeDef, APB2ENR) == (68)': RCC_TypeDef.APB2ENR is not at RM offset 0x44
solution.c:34:38: note: expression evaluates to '64 == 68'
3 errors generated.
```

메시지에 `#T "." #f " is not at RM offset " #off` 로 만든 문자열이 그대로 나오고, clang 은 `44 == 48` 처럼 실제 값까지 보여 준다. 예약 칸 앞의 `APB1RSTR/APB2RSTR` 는 통과하고 **빈칸 바로 뒤부터** 실패한다 — 어느 빈칸이 틀렸는지 위치가 바로 나온다. `RESERVEDn` 자체에는 assert 를 걸 필요가 없다: 그 뒤 실제 레지스터 하나만 고정하면 된다.

### Part B — 필드 매크로: 토큰 붙이기 한 줄

```c
#define VAL2FLD(field, value)   (((uint32_t)(value) << field ## _Pos) & field ## _Msk)
#define FLD2VAL(field, value)   (((uint32_t)(value) & field ## _Msk) >> field ## _Pos)
#define MODIFY_REG(REG, CLEARMASK, SETMASK) WRITE_REG((REG), (READ_REG(REG) & ~(CLEARMASK)) | (SETMASK))
```

- `VAL2FLD` 의 마지막 `& _Msk` 가 **잘라내기**다: `VAL2FLD(GPIO_MODER_MODER5, 7)` 은 `0x1C00 & 0xC00 = 0xC00` (11 = 아날로그). 에러가 아니라 조용히 잘리므로 값 범위 검사는 호출하는 쪽 몫이다 (`usart2_init` 의 BRR 범위 검사).
- `(uint32_t)(value)` 캐스트: 음수 `int` 를 시프트하는 UB 를 막는다 (C-B01, C-B02).
- `MODIFY_REG` 는 `REG` 를 두 번 쓴다 (읽기 1, 쓰기 1). 인자에 부작용이 있으면 안 된다 (`MODIFY_REG(*p++, ...)` 금지). volatile 접근은 정확히 읽기 1번 + 쓰기 1번 — `demo_target.c` 의 asm 에서 `ldr` 하나, `str` 하나로 확인된다.
- 무작위 속성 테스트는 필드를 런타임 변수로 만드는 요령을 쓴다: `#define TFLD_Pos t_pos`, `#define TFLD_Msk t_msk` 로 두면 `VAL2FLD(TFLD, v)` → `TFLD_Pos` → `t_pos` 로 다시 펼쳐진다. 그래서 32가지 폭 × 모든 위치를 같은 매크로로 검사한다.

**버그 갤러리** — 하네스가 실제로 확인하는 값 (리셋값 `MODER = 0xA800_0000`, `CR1 = 0` 에서 시작):

| # | 코드 | 결과 | 무슨 일이 일어났나 |
|---|---|---|---|
| 1 | `GPIOA->MODER \|= GPIO_MODER_MODER5_Pos;` | `MODER = 0xA800_000A` | `_Pos` = 10 = `0b1010` → MODER0 = `10`, MODER1 = `10`. PA0·PA1 이 AF 로 바뀌고 PA5 는 그대로 입력 |
| 2 | `SET_BIT(USART2->CR1, USART_CR1_UE_Pos);` | `CR1 = 0x0000_000D` | 13 = `0b1101` → SBK(0)·RE(2)·TE(3). UE 는 꺼진 채, break 송신 요청 비트가 서 있다 |
| 3 | `GPIOA->MODER \|= GPIO_MODER_MODER5_0;` (PA5 가 AF 였던 상태) | MODER5 = `11` | OR 은 1을 켜기만 한다: `10 \| 01 = 11` = 아날로그. 출력이 안 나온다 |
| 4 | `... \| GPIO_MODER_MODER5;` (접미사 없는 이름) | MODER5 = `11` | 접미사 없는 이름은 `_Msk` 의 **별칭** = `0xC00`. 값처럼 보이지만 필드 전체가 1 |
| 5 | `GPIOA->MODER = GPIO_MODER_MODER5_0;` | `MODER = 0x0000_0400` | PA13(SWDIO)·PA14(SWCLK)·PA15 가 입력으로 → **디버거 연결이 끊긴다**. 다음 부팅부터는 "connect under reset" 으로만 붙는다 |

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">GPIOA_MODER — 핀당 2비트, 핀 n = 비트 [2n+1:2n] (리셋값 0xA800_0000, RM0090 대표값)</text>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="42" y="46">15</text><text x="86" y="46">14</text><text x="130" y="46">13</text><text x="174" y="46">12</text>
    <text x="218" y="46">11</text><text x="262" y="46">10</text><text x="306" y="46">9</text><text x="350" y="46">8</text>
    <text x="394" y="46">7</text><text x="438" y="46">6</text><text x="482" y="46">5</text><text x="526" y="46">4</text>
    <text x="570" y="46">3</text><text x="614" y="46">2</text><text x="658" y="46">1</text><text x="702" y="46">0</text>
  </g>
  <text x="10" y="46" fill="currentColor" font-size="12">핀</text>
  <g stroke="currentColor" fill="none">
    <rect x="152" y="54" width="44" height="28"/><rect x="196" y="54" width="44" height="28"/>
    <rect x="240" y="54" width="44" height="28"/><rect x="284" y="54" width="44" height="28"/>
    <rect x="328" y="54" width="44" height="28"/><rect x="372" y="54" width="44" height="28"/>
    <rect x="416" y="54" width="44" height="28"/><rect x="504" y="54" width="44" height="28"/>
    <rect x="548" y="54" width="44" height="28"/><rect x="592" y="54" width="44" height="28"/>
    <rect x="636" y="54" width="44" height="28"/><rect x="680" y="54" width="44" height="28"/>
  </g>
  <rect x="20" y="54" width="44" height="28" class="f-warn" stroke="currentColor"/>
  <rect x="64" y="54" width="44" height="28" class="f-warn" stroke="currentColor"/>
  <rect x="108" y="54" width="44" height="28" class="f-warn" stroke="currentColor"/>
  <rect x="460" y="54" width="44" height="28" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="42" y="73">10</text><text x="86" y="73">10</text><text x="130" y="73">10</text><text x="174" y="73">00</text>
    <text x="218" y="73">00</text><text x="262" y="73">00</text><text x="306" y="73">00</text><text x="350" y="73">00</text>
    <text x="394" y="73">00</text><text x="438" y="73">00</text><text x="482" y="73">00</text><text x="526" y="73">00</text>
    <text x="570" y="73">00</text><text x="614" y="73">00</text><text x="658" y="73">00</text><text x="702" y="73">00</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="100">PA15/14/13 = AF (JTDI/SWCLK/SWDIO)</text>
    <text x="420" y="100">MODER5: _Pos = 10, _Msk = 0x0000_0C00</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="136">정답  MODIFY_REG(MODER, MODER5_Msk, VAL2FLD(MODER5, 01))  → 0xA800_0400  핀5 = 01</text>
    <text x="20" y="162">버그1 MODER |= MODER5_Pos   (10 = 0b1010)              → 0xA800_000A  핀1,0 = 10 10</text>
    <text x="20" y="188">버그3 핀5 가 10 일 때 MODER |= MODER5_0                  → 핀5 = 10|01 = 11</text>
    <text x="20" y="214">버그4 (MODER &amp; ~Msk) | MODER5  (별칭 = _Msk)            → 핀5 = 11</text>
    <text x="20" y="240">버그5 MODER = MODER5_0                                  → 0x0000_0400  핀15..13 = 00</text>
  </g>
  <text x="20" y="276" fill="currentColor" font-size="12">버그5 는 SWD 핀을 입력으로 바꾼다 — 실행 직후 디버거가 끊기는 가장 흔한 bring-up 사고.</text>
</svg>
```

### Part C — 설정 함수: 무엇을 어떤 순서로

**레지스터 규칙** (RM0090 기준 대표값 — 확인 권장):

| 레지스터 | 필드 | 값 | 의미 |
|---|---|---|---|
| RCC_AHB1ENR | GPIOAEN, bit 0 | 1 | GPIOA 클럭 (리셋값 `0x0010_0000` — bit 20 CCMDATARAMEN 만 1) |
| RCC_APB1ENR | USART2EN, bit 17 | 1 | USART2 는 APB1 버스 |
| GPIOx_MODER | [2n+1:2n] | 00 입력, 01 출력, 10 AF, 11 아날로그 | |
| GPIOx_OTYPER | [n] | 0 push-pull, 1 open-drain | |
| GPIOx_OSPEEDR | [2n+1:2n] | 00 low, 01 medium, 10 fast, 11 high | 슬루율(slew rate) — 빠를수록 EMI ↑ |
| GPIOx_PUPDR | [2n+1:2n] | 00 없음, 01 풀업, 10 풀다운 | |
| GPIOx_AFRL / AFRH | [4n+3:4n] | AF0–AF15 | 핀 0–7 → `AFR[0]`, 8–15 → `AFR[1]` |
| USART_CR1 | UE 13, M 12, PCE 10, PS 9, TE 3, RE 2, OVER8 15 | | |
| USART_CR2 | STOP [13:12] | 00 = 1 stop | |
| USART_BRR | Mantissa [15:4], Fraction [3:0] | OVER8=0 → BRR = USARTDIV × 16 | |

**`usart2_pins_init()` 단계별 레지스터 값** (리셋값에서 시작, 스크래치 프로그램으로 한 줄씩 찍은 실제 값):

| 단계 | AHB1ENR | MODER | OSPEEDR | PUPDR | AFRL |
|---|---|---|---|---|---|
| 리셋 | `00100000` | `A8000000` | `0C000000` | `64000000` | `00000000` |
| `SET_BIT(AHB1ENR, GPIOAEN)` | `00100001` | | | | |
| AFRL AFSEL2/3 = 7 | | | | | `00007700` |
| OTYPER OT2 = 0 | | (OTYPER `00000000`) | | | |
| OSPEEDR2 = 01 | | | `0C000010` | | |
| PUPDR2 = 00, PUPDR3 = 01 | | | | `64000040` | |
| MODER2/3 = 10 (마지막) | | `A80000A0` | | | |

빈 칸은 "바뀌지 않음". PA13–15 의 리셋 설정(`MODER` 상위 `A8`, `PUPDR` 상위 `64`, `OSPEEDR` 의 PA13 `0C`)이 끝까지 그대로 남는 것이 핵심이다.

**왜 MODER 가 마지막인가** — MODER 를 먼저 AF 로 바꾸면, AFRL 을 쓰기 전 몇 사이클 동안 PA2 는 AF0(시스템 기능)에 연결된다. 출력 핀도 같다: `led_pa5_init` 처럼 출력으로 바꾸기 전에 원하는 초기 레벨을 BSRR 로 먼저 정해 두면 켜지는 순간의 글리치가 없다 (리셋 직후 ODR = 0 이라 이 레슨에선 생략). HAL 의 `HAL_GPIO_Init()` 도 AF 를 MODER 보다 먼저 쓴다 (STM32CubeF4 소스 기준, 확인 권장).

**`usart2_init(16 MHz, 115200)` — 더러운 상태에서 시작** (테스트의 두 번째 경우, 실제 값):

| 단계 | APB1ENR | CR1 | CR2 | BRR |
|---|---|---|---|---|
| 시작 (OVER8·M·PCE·PS·RXNEIE 켜짐, STOP=11) | `10000001` | `00009620` | `00003040` | `00000000` |
| 범위 검사: (16 000 000 + 57 600) / 115 200 = 139 → 16 ≤ 139 ≤ 0xFFFF | | | | |
| `SET_BIT(APB1ENR, USART2EN)` | `10020001` | | | |
| `CLEAR_BIT(CR1, UE)` (이미 0) | | `00009620` | | |
| `MODIFY_REG(CR1, M\|PCE\|PS\|OVER8\|TE\|RE, TE\|RE)` | | `0000002C` | | |
| `MODIFY_REG(CR2, STOP, 0)` | | | `00000040` | |
| BRR = `VAL2FLD(Mantissa, 8) \| VAL2FLD(Fraction, 11)` | | | | `0000008B` |
| `SET_BIT(CR1, UE)` | | `0000202C` | | |

- `RXNEIE`(bit 5)는 이 함수가 소유하지 않는 비트라 남긴다 — "내가 정하는 비트만 clear 마스크에 넣는다" 가 MODIFY_REG 설계의 전부다. 다른 정책(전부 `WRITE_REG(CR1, 0)` 후 조립)도 있다: 초기화 함수가 그 레지스터를 **완전히 소유**하면 그 편이 더 결정적이다. 어느 쪽이든 문서로 정한다.
- BRR: OVER8=0 이면 USARTDIV = mantissa + fraction/16 이고 BRR = `mantissa<<4 | fraction` = USARTDIV × 16. 그러니 `round(pclk/baud)` 하나를 그대로 쪼개 넣으면 fraction 이 16 으로 반올림돼 mantissa 로 올라가는 경우도 저절로 맞다. 42 MHz → 365 = `0x16D` (실제 115 068 baud, −0.11 %). 반올림 올림수(carry)·OVER8=1 의 3비트 fraction 규칙·오차 분석은 BM-B12, 허용 오차 이론은 COMM-B03.
- 실패 경로: baud 0, BRR < 16 (OVER8=0 에선 최대 baud = f/16, 16 MHz 에서 2 Mbaud 는 BRR 8), BRR > 0xFFFF (42 MHz 에서 300 baud 는 140 000) → `-1`, 레지스터는 그대로 (테스트가 `BRR = 0xDEAD` 센티널로 확인).
- 클럭을 켠 직후의 `(void)READ_BIT(RCC->AHB1ENR, ...)` 되읽기는 "클럭 활성화 직후 주변장치 접근 지연" 대응이다 — 왜 필요한지는 BM-B03.

**같은 소스, 두 빌드.** 테스트 빌드는 헤더를 include 한 뒤 캐스트 매크로만 바꾼다:

```c
#ifdef UNIT_TEST
static GPIO_TypeDef ram_gpioa;
#undef GPIOA
#define GPIOA (&ram_gpioa)        /* 드라이버 코드는 한 글자도 안 바뀐다 */
#endif
```

`UNIT_TEST` 없이 Cortex-M4 로 컴파일하면 (`demo_target.c`, 실제 출력에서 지시어 줄만 걸러 냄):

<!-- code: demo_target.c -->

```text
$ clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -ffreestanding -S demo_target.c -o - | (지시어·빈 줄 제외)
led_pa5_init:                           @ @led_pa5_init
	movw	r0, #14384
	movt	r0, #16386
	ldr	r1, [r0]
	movs	r2, #1
	orr	r1, r1, #1
	str	r1, [r0]
	ldr	r0, [r0]
	movs	r0, #0
	movt	r0, #16386
	ldr	r1, [r0]
	bfi	r1, r2, #10, #2
	str	r1, [r0]
	bx	lr
led_pa5_set:                            @ @led_pa5_set
	movs	r1, #24
	cmp	r0, #0
	mov.w	r0, #2097152
	movt	r1, #16386
	it	ne
	movne	r0, #32
	str	r0, [r1]
	bx	lr
msk_size:
	.long	4                               @ 0x4
```

| 명령 | 10진 → 16진 | 뜻 |
|---|---|---|
| `movw r0,#14384` + `movt r0,#16386` | 0x3830, 0x4002 | r0 = `0x4002_3830` = `&RCC->AHB1ENR` |
| `ldr` / `orr #1` / `str` | | `SET_BIT(AHB1ENR, GPIOAEN)` — RMW |
| `ldr r0,[r0]` (결과 버림) | | 되읽기 — volatile 이라 지워지지 않는다 |
| `movs r0,#0` + `movt r0,#16386` | 0x4002_0000 | `&GPIOA->MODER` |
| `ldr` / `bfi r1,r2,#10,#2` / `str` | r2 = 1 | `MODIFY_REG(MODER, MODER5_Msk, 01<<10)` — clear+set 가 BFI 한 명령 |
| `movs r1,#24` + `movt r1,#16386` | 0x4002_0018 | `&GPIOA->BSRR` |
| `mov.w r0,#2097152` / `movne r0,#32` | 0x0020_0000 / 0x20 | BR5 / BS5 를 고르는 `it ne` 조건 실행, 쓰기는 `str` 하나 |
| `.long 4` | | `sizeof(0x3UL << 10)` — 타깃 4바이트 (맥 호스트에서는 8) |


볼 것: ① 주소는 `movw/movt` 즉시값으로 들어가 런타임 비용이 없다 ② `MODIFY_REG` 는 `ldr` → `bfi` → `str` — clear+set 두 연산을 컴파일러가 **BFI 한 명령**으로 합쳤다 ③ volatile 이라 되읽기 `ldr` 이 남는다 ④ `0x3UL` 의 `UL` 은 타깃에서 32비트, 맥(LP64)에서 64비트 — 호스트 테스트에서 `~Msk` 가 64비트가 되는 이유 (Q7).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 55/55 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_san && ./sol_san   # 55/55, 보고 없음
cc -std=c11 -Wall -Wextra -Werror -O2 -DDEMO_BAD_RESERVED -fsyntax-only solution.c   # 패딩 버그 → static assert 3개 실패
cc -std=c11 -Wall -Wextra -Werror -O2 -c demo_target.c -o /tmp/dt.o                  # 호스트 컴파일만 (실행 X)
cc -std=c11 -Wall -Wextra -Werror -DDEMO_WRITE_RO -fsyntax-only demo_target.c        # __IM 레지스터 쓰기 → 에러
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `REG \|= X_Pos;` | `_Pos` 는 시프트 양(숫자). 10 = `0b1010` 이 엉뚱한 비트를 켠다 | `REG \|= X_Msk;` 또는 `VAL2FLD(X, v)` |
| `REG \|= VAL2FLD(F, v);` | clear 없이 OR → 이전 값과 섞인다 (`10 \| 01 = 11`) | `MODIFY_REG(REG, F_Msk, VAL2FLD(F, v));` |
| `... \| GPIO_MODER_MODER5` 를 "출력 값" 으로 | 접미사 없는 이름 = `_Msk` 별칭 (필드 전체 1) | `VAL2FLD(GPIO_MODER_MODER5, MODE_OUT)` 또는 `GPIO_MODER_MODER5_0` |
| `GPIOA->MODER = ...;` 덮어쓰기 | 다른 핀, 특히 PA13/PA14 SWD 가 입력으로 → 디버거 끊김 | RMW (`MODIFY_REG`) — GPIOA/GPIOB 리셋값은 0 이 아니다 |
| 클럭을 켜기 전에 GPIO 설정 | 클럭 꺼진 주변장치 쓰기는 무시된다 | `SET_BIT(RCC->AHB1ENR, ...GPIOAEN)` 먼저 (BM-B03) |
| 구조체를 손으로 옮기다 `RESERVEDn` 누락 | 그 뒤 레지스터가 전부 4바이트 당겨짐, 디버거도 같이 속음 | `_Static_assert(offsetof(T, f) == RM)` 을 빈칸 **바로 뒤** 레지스터마다 |
| `__attribute__((packed))` 로 "패딩 방지" | 패딩은 원래 없다. packed 는 정렬 보장을 없애 바이트 접근 코드가 나올 수 있다 | `uint32_t` + 명시적 `RESERVEDn`, `_Static_assert` |
| `MODIFY_REG(USARTx->SR, ...)` 로 플래그 지우기 | RMW 가 다른 플래그까지 건드린다 (rc_w0 / W1C) | 상태 레지스터는 `WRITE_REG` 로 해당 비트만 (BM-B04) |
| AF 핀: MODER 먼저, AFR 나중 | 잠깐 AF0 에 연결돼 글리치 | AFR·OTYPER·OSPEEDR·PUPDR → MODER 마지막 |
| `USART_CR1_UE` 를 켠 채 BRR·M 변경 | 전송 중 형식 변경 — RM 이 금지하는 비트가 있다 (확인 권장) | `CLEAR_BIT(CR1, UE)` → 설정 → `SET_BIT(CR1, UE)` |
| 내 코드에 `_VAL2FLD` 같은 이름 정의 | 밑줄+대문자 이름은 구현 예약 (C11 7.1.3) | CMSIS 것을 쓰거나 밑줄 없는 이름으로 |

## 8. 면접 Q&A

<details>
<summary>Q1. 왜 레지스터를 구조체 포인터 캐스트로 접근하나요? `#define GPIOA_MODER (*(volatile uint32_t*)0x40020000)` 과 뭐가 다르죠?</summary>

결과 명령은 같다 (주소에 대한 `ldr/str`). 차이는 ① **인스턴스 재사용** — `GPIO_TypeDef *` 하나로 GPIOA…GPIOK 를 같은 함수가 다룬다 (`gpio_pin_mode(GPIOC, ...)`), 레지스터별 `#define` 은 포트 × 레지스터 개수만큼 필요하다 ② **베이스+오프셋 주소 지정** — 컴파일러가 베이스를 레지스터 하나에 두고 `ldr r1, [r0, #0x14]` 처럼 오프셋으로 접근한다 ③ **테스트 이음새(seam)** — 이 레슨처럼 `GPIOA` 하나만 RAM 구조체로 바꾸면 드라이버 전체가 호스트에서 돈다. 꼬리: "volatile 은 왜?" → 각 접근이 실제 버스 트랜잭션이어야 하기 때문이다. 없으면 컴파일러가 폴링 루프의 읽기를 한 번으로 줄이거나, 연속 쓰기를 마지막 하나만 남기거나, 되읽기를 지운다. volatile 은 "volatile 접근끼리의 순서와 횟수" 만 보장하고 CPU/버스 수준 순서(장벽)는 별개다 (`__DSB()`, ARCH-M12).

</details>

<details>
<summary>Q2. `__I`, `__O`, `__IO` 는 실제로 무엇을 강제하나요?</summary>

CMSIS-Core(`core_cm4.h`)의 정의: C 에서 `__I` = `volatile const`, `__O` = `volatile`, `__IO` = `volatile` (구조체 멤버용 `__IM/__OM/__IOM` 도 같은 쌍). 그러니 **읽기 전용만 컴파일러가 막는다** — `demo_target.c` 의 `-DDEMO_WRITE_RO` 실제 출력: `error: cannot assign to non-static data member 'IN' with const-qualified type 'const volatile uint32_t'`. 쓰기 전용 `__O` 는 문서일 뿐, `x = GPIOA->BSRR;` 는 컴파일되고 하드웨어는 0 을 돌려준다 (RM0090: BSRR 읽기 = 0). C++ 에서는 `__I` 가 `volatile` 만이다 — const 멤버가 있으면 C++ 에서 구조체 기본 생성·대입이 막히는 문제를 피하려는 것으로 보인다. ST F4 헤더는 IDR 까지 대부분 `__IO` 로 선언하고, nRF MDK 헤더는 `IN` 같은 입력 레지스터에 `__IM` 을 쓴다 (헤더 버전별 확인 권장). 꼬리: "읽기 전용 레지스터에 const 를 쓰면 테스트가 불편한 점?" → RAM 구조체 바꿔치기에서 그 멤버를 쓸 수 없다 (const 객체 수정은 UB, C11 6.7.3p6). 그래서 그런 레지스터는 BM-B02 의 훅(hook) mock 으로 값을 공급한다.

</details>

<details>
<summary>Q3. `_Pos` 와 `_Msk` 를 헷갈리면 정확히 무슨 일이 일어나죠? 예를 들어 주세요.</summary>

`_Pos` 는 숫자, `_Msk` 는 비트 패턴이다. `SET_BIT(USART2->CR1, USART_CR1_UE_Pos)` 는 13 = `0b1101` 을 OR 해서 SBK(bit 0)·RE(2)·TE(3) 를 켠다 (테스트: `CR1 = 0x0000000D`). UE 는 꺼진 채라 UART 는 죽어 있고, 나중에 누가 UE 를 켜면 대기 중이던 break 요청이 나갈 수 있다 (SBK 의 정확한 동작은 RM 확인 권장). MODER 에 `MODER5_Pos`(10) 를 OR 하면 `0b1010` → PA0·PA1 이 AF 로 바뀐다 (`0xA800_000A`). 두 경우 모두 컴파일러 경고가 없다 — 둘 다 그냥 정수다. 방어: 직접 OR 하지 말고 `VAL2FLD(F, v)` / `MODIFY_REG(R, F_Msk, ...)` 처럼 **필드 이름만 받는** 매크로를 쓰면 `_Pos` 와 `_Msk` 를 손으로 고를 일이 없다.

</details>

<details>
<summary>Q4. 구조체 레이아웃이 RM 과 같다는 걸 어떻게 보장하나요? `packed` 를 붙이면 되지 않나요?</summary>

`_Static_assert(offsetof(T, f) == RM_offset, "...")` 를 RM 표의 빈칸 **바로 뒤** 레지스터마다, 그리고 `sizeof` 에 건다. 실패는 컴파일 에러라 비용이 0 이고, clang 은 `44 == 48` 처럼 실제 값까지 보여 준다. `uint32_t` 만 있는 구조체에는 원래 숨은 패딩이 없다 (C11 6.7.2.1, AAPCS 정렬 = 크기) — 빈칸은 `RESERVEDn` 으로 명시한다. `packed` 는 오히려 해롭다: 구조체 정렬이 1 이 되어, 컴파일러가 주소 정렬을 모른다고 보고 바이트 단위 접근을 만들 수 있다 (`-mno-unaligned-access` 나 Cortex-M0 처럼 비정렬 접근이 없는 코어). 32비트 접근만 허용하는 주변장치 레지스터에 바이트 쓰기 4번은 버스 폴트이거나 잘못된 쓰기다. 꼬리: "헤더 자체를 어떻게 믿나?" → 벤더 헤더는 SVD 에서 생성되고 SVD 도 틀린다. 실칩에서 리셋 직후 레지스터를 덤프해 RM 의 리셋값 표와 비교하는 것이 첫 스모크 테스트다 (SIV-B01 의 생성 + lint 와 짝).

</details>

<details>
<summary>Q5. `MODIFY_REG` 는 원자적인가요? ISR 이 같은 레지스터를 만지면?</summary>

아니다. `ldr` → `bfi`(또는 `bic/orr`) → `str` 세 명령이고, 그 사이에 ISR 이 같은 레지스터를 고치면 main 의 `str` 이 ISR 의 변경을 덮어쓴다 (lost update). 고치는 법: ① 하드웨어가 원자적 set/reset 레지스터를 주면 그것을 쓴다 — GPIO 출력은 BSRR 한 번 쓰기 (`led_pa5_set`) ② 아니면 임계 구역 (`__disable_irq()`/PRIMASK 저장·복원, BASEPRI — C-M14, ARM-M10) ③ Cortex-M3/M4 라면 비트밴딩(bit-banding) 별칭 주소로 한 비트 쓰기 (BM-M/A) 또는 LDREX/STREX (ARM-M11 — 단, 주변장치 영역에서의 독점 접근 지원은 구현에 따라 다르다). 그리고 상태 레지스터의 W1C/rc_w0 비트에는 RMW 자체가 버그다 — BM-B04 에서 재현한다.

</details>

<details>
<summary>Q6. 이 드라이버를 호스트에서 어떻게 단위 테스트하죠? 한계는?</summary>

이 레슨의 방법은 **매크로 이음새**: `UNIT_TEST` 빌드에서 `#undef GPIOA; #define GPIOA (&ram_gpioa)`. 드라이버 소스는 그대로이고, 테스트는 함수 호출 뒤 RAM 구조체의 값을 검사한다. 리셋값 고정 테스트 + 무작위 선채움 500회로 "필드 밖 불변" 을 증명했다. 한계: RAM 은 레지스터의 **부작용**을 모른다 — 읽으면 지워지는 비트(rc_r), W1C, 쓰기 전용(읽으면 0), 클럭이 꺼지면 무시되는 쓰기, TXE 가 시간이 지나야 1 이 되는 동작, 접근 **순서**. 그런 것은 읽기/쓰기 훅과 접근 로그를 가진 mock 이 필요하다 (BM-B02 → B03 클럭 게이팅, B12 UART). 다른 이음새: 링크 시점 치환 (CPP-A08), 함수 포인터 HAL. 마지막 단계는 같은 소스를 타깃으로 빌드해 asm 을 보는 것 (`demo_target.c`) 과 QEMU/실보드 (BM-B08).

</details>

<details>
<summary>Q7. 호스트 테스트에서 `0x3UL << 10` 이 왜 문제가 될 수 있죠?</summary>

ST 헤더는 마스크를 `UL` 로 쓴다. Cortex-M(AAPCS, ILP32)에서 `unsigned long` 은 32비트지만 맥/리눅스 x86-64·arm64(LP64)에서는 64비트다 — `demo_target.c` 의 `msk_size` 가 타깃 asm 에서 `.long 4`, 호스트에서 `sizeof` 는 8. 그러면 `~GPIO_MODER_MODER5_Msk` 가 호스트에서 `0xFFFF_FFFF_FFFF_F3FF` 가 된다. `uint32_t` 레지스터에 다시 대입할 때 잘리므로 값은 맞지만 ① `printf("%08x", Msk)` 는 형식 경고 ② `-Wconversion` 에서 경고 폭탄 ③ `(reg & ~Msk) == 0xFFFFF3FFu` 같은 비교에서 64비트 쪽으로 승격된 값과 비교해 놀랄 수 있다. 그래서 `VAL2FLD` 결과나 비교 대상은 `(uint32_t)` 로 캐스트한다 (이 레슨의 속성 테스트가 그렇게 한다).

</details>

<details>
<summary>Q8. 같은 STM32 인데 헤더마다 이름이 다른 건 왜죠? 어떻게 대처하나요?</summary>

헤더가 SVD(System View Description, CMSIS-SVD XML)에서 생성되고, 벤더가 계열 간 이름을 맞추면서 바꿨기 때문이다. F4 만 해도 `GPIO_MODER_MODER5`(옛) / `GPIO_MODER_MODE5`(새), `GPIO_OSPEEDER_OSPEEDR5`(오타가 들어간 옛 이름) / `GPIO_OSPEEDR_OSPEED5`, `GPIO_AFRL_AFRL2` / `GPIO_AFRL_AFSEL2` 가 공존하거나 호환 별칭으로 남아 있다 (헤더 버전별 확인 권장). 더 옛날 StdPeriph 헤더는 BSRR 을 `uint16_t BSRRL, BSRRH` 두 개로 선언했다 (연습 4). 대처: 프로젝트가 쓰는 헤더 버전을 고정하고, 내 드라이버는 필드 이름을 한 곳(작은 래퍼)에서만 쓰고, 오프셋은 `_Static_assert` 로 고정한다. 레지스터 맵을 사내에서 정의한다면 SIV-B01 처럼 단일 소스에서 생성한다.

</details>

## 9. 추가 연습

### 연습 1 — 핀 번호를 인자로 받는 모드 설정

핀별 매크로(`MODER5_Msk`)는 핀이 상수일 때만 쓸 수 있다. `int gpio_pin_mode(GPIO_TypeDef *g, unsigned pin, uint32_t mode)` 를 헤더의 `GPIO_MODER_MODER0_*` 만으로 써라. 핀 > 15 나 mode > 3 이면 -1 이고 아무것도 쓰지 않는다. 16 핀 × 4 모드 × 무작위 선채움으로 검사.

<details>
<summary>답 보기</summary>

```c
MODIFY_REG(g->MODER, GPIO_MODER_MODER0_Msk << (pin * 2u),
           VAL2FLD(GPIO_MODER_MODER0, mode) << (pin * 2u));
```

핀 0 필드를 `2·pin` 만큼 옮긴다 — 핀 15 는 비트 [31:30] 이고 `0x3UL << 30` 은 32비트 안이라 안전하다. 검사를 쓰기 전에 하는 것이 요점: `mode = 4` 를 그냥 넘기면 `VAL2FLD` 가 조용히 잘라 0(입력)이 된다. 테스트: 1920회 무작위 + `MODER 0xA800_0000` 에서 핀 15 를 출력으로 → `0x6800_0000`. 이 일반화된 드라이버를 클럭·풀 모델이 있는 mock 위에서 완성하는 것이 BM-B11 이다.

</details>

### 연습 2 — AFR[2]: AFRL 과 AFRH

`int gpio_pin_af(GPIO_TypeDef *g, unsigned pin, uint32_t af)`. 헤더는 AFRL/AFRH 를 `AFR[2]` 배열로 선언한다. 핀 7 과 8 의 경계, 핀 15, 잘못된 인자를 확인하라.

<details>
<summary>답 보기</summary>

`idx = pin >> 3` (0 = AFRL @0x20, 1 = AFRH @0x24), `sh = (pin & 7) * 4`, `MODIFY_REG(g->AFR[idx], GPIO_AFRL_AFSEL0_Msk << sh, VAL2FLD(GPIO_AFRL_AFSEL0, af) << sh)`. 핀 7, AF7 → `AFR[0] = 0x7000_0000` / 핀 8, AF5 → `AFR[1] = 0x0000_0005` (AFR[0] 그대로). 흔한 버그는 핀 8 이상에서 `pin * 4` 로 밀어 32 이상 시프트(UB)하거나 AFR[0] 에 써 버리는 것. ST 헤더는 AFRH 용 `GPIO_AFRH_AFSELn` 매크로도 따로 두지만 필드 모양이 같으니 AFRL 의 0번 필드를 옮겨 써도 된다.

</details>

### 연습 3 — 레지스터 되읽기: 지금 실제 baud 는?

디버거로 `USART2->BRR` 과 `CR1` 을 읽었다. `uint32_t usart_actual_baud(uint32_t pclk_hz, const USART_TypeDef *u)` 를 `FLD2VAL` 로 써라. OVER8=1 도 처리한다.

<details>
<summary>답 보기</summary>

OVER8=0: baud = f / (16·(M + F/16)) = f / (16M + F) → BRR 값 그대로가 분모. OVER8=1: USARTDIV = M + F/8 (fraction 3비트, BRR bit 3 은 0 으로 둔다), baud = f / (8M + F). 결과: `BRR 0x16D` @42 MHz → 115 068, OVER8=1 `BRR 0x2D5` (M=45, F=5 → 365) → 같은 115 068, `BRR 0x08B` @16 MHz → 115 108. BRR = 0 이면 0 을 돌려 0 나누기를 피한다. 현장 용도: 보드가 쓰레기 문자를 낼 때 BRR 과 실제 PCLK1(RCC CFGR 의 PPRE1 로 계산)을 읽어 이 식에 넣는 것이 첫 점검이다 (BM-B10 클럭 트리, BM-B12).

</details>

### 연습 4 — 옛 헤더의 BSRRL / BSRRH

STM32F4 StdPeriph 시절 헤더(확인 권장)는 BSRR 을 `__IO uint16_t BSRRL;` (0x18) + `__IO uint16_t BSRRH;` (0x1A) 로 선언했다. 이 구조체의 오프셋을 `_Static_assert` 로 고정하고, `BSRRH = 1 << 5` 가 32비트 `BSRR = GPIO_BSRR_BR5` 와 같은 바이트를 만든다는 것을 보여라.

<details>
<summary>답 보기</summary>

`offsetof(BSRRL) == 0x18`, `offsetof(BSRRH) == 0x1A`, `LCKR == 0x1C`, `sizeof` 는 32비트 판과 같다. 리틀 엔디언(Cortex-M 기본, 맥 arm64 도 같음)에서 0x1A 의 하프워드 `0x0020` 은 0x18 부터 읽은 32비트 워드의 상위 절반 → `0x0020_0000` = BR5. 테스트는 `memcpy` 로 0x18 의 4바이트를 꺼내 비교한다 (UB 없는 타입 변환). 실칩에서 이 방식은 GPIO 가 하프워드 쓰기를 받아 주기 때문에 동작한다 (RM0090 의 GPIO 레지스터 접근 폭 설명 확인 권장) — 모든 주변장치가 그런 것은 아니다. 지금 헤더는 32비트 `BSRR` 하나로 바꿨고, 옛 코드를 옮길 때 `BSRRH` 컴파일 에러가 나는 이유가 이것이다.

</details>

### 연습 5 — 패딩 버그를 런타임 표로 찾기

`_Static_assert` 를 쓸 수 없는 상황(예: 헤더를 검사 도구가 런타임에 로드)을 가정하고, `{이름, RM 오프셋, offsetof}` 표를 비교해 처음 어긋나는 레지스터를 돌려주는 `first_layout_mismatch()` 를 써라. `RESERVED1` 을 1 워드로 쓴 `RCC_Bad` 에서 GPIOA 클럭 켜기 쓰기는 어느 주소로 가나?

<details>
<summary>답 보기</summary>

표 행은 `#define ROW(T, f, off) { #f, (off), offsetof(T, f) }` 로 만든다. 올바른 `RCC_TypeDef` → -1, `RCC_Bad` → 인덱스 3 = `AHB1ENR` (실제 0x2C). 쓰기 주소는 `RCC_BASE + 0x2C = 0x4002_382C` (예약 칸), APB1ENR 은 `0x4002_383C` (역시 예약 칸). 즉 GPIOA 클럭도 USART2 클럭도 켜지지 않는다. 같은 표를 실칩에서 돌리면(리셋 직후 각 주소를 읽어 RM 리셋값과 비교) 헤더·칩·RM 세 가지가 서로 맞는지 보는 스모크 테스트가 된다 (SIV 트랙의 레지스터 리셋값 테스트).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 18/18 passed (레슨 폴더에서)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o pr_san && ./pr_san   # 18/18
```

## 10. 핵심 정리

- CMSIS 헤더 = 한정자(`__I` 만 const, `__O` 는 문서용) + `xxx_TypeDef`(멤버 순서 = 오프셋, 빈칸 = `RESERVEDn`) + 베이스 체인(`PERIPH_BASE → 버스 → 주변장치 → 캐스트`) + `_Pos/_Msk` + 접근 매크로.
- 주소 = 베이스 체인 + `offsetof`: `&USART2->BRR = 0x4000_4408`, `&RCC->APB1ENR = 0x4002_3840`, `&GPIOC->BSRR = 0x4002_0818` (RM0090 대표값).
- `_Pos` 는 숫자(시프트 양), `_Msk` 는 비트 패턴, 접미사 없는 이름은 `_Msk` 별칭. 값은 `VAL2FLD(F, v) = ((uint32_t)v << F_Pos) & F_Msk`.
- 필드 쓰기는 `MODIFY_REG(R, F_Msk, VAL2FLD(F, v))` — clear 없는 `|=`, 덮어쓰기 `=` 는 버그 (SWD 핀이 죽는다). MODIFY_REG 는 원자적이지 않다 → BSRR / 임계 구역 (BM-B04).
- RM 오프셋 표는 `_Static_assert(offsetof(T, f) == off)` 로 빈칸 바로 뒤 레지스터마다 고정. `packed` 는 쓰지 않는다.
- 설정 순서: 클럭 켜기(+되읽기, BM-B03) → AF·타입·속도·풀 → MODER 마지막 / USART 는 UE=0 → 형식 → BRR(OVER8=0 이면 `round(f/baud)`) → UE=1. 범위 검사 실패면 아무것도 쓰지 않는다.
- 호스트 테스트: `#undef GPIOA` + RAM 구조체 → 값은 검증되지만 부작용·순서는 안 보인다 (BM-B02 mock). 타깃 asm 에선 MODIFY_REG 가 `ldr/bfi/str`, 주소는 `movw/movt`.

## 참고자료

- STMicroelectronics RM0090 Reference Manual — STM32F405/415, F407/417, F427/437, F429/439: 2.3 메모리 맵(주변장치 베이스 주소), RCC 레지스터 맵(AHB1ENR 0x30, APB1ENR 0x40), GPIO 레지스터 맵·리셋값, USART 레지스터(BRR, CR1, OVER8) — <https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf> (이 레슨의 수치는 기억에 의존한 대표값 — 원문 확인 권장)
- STM32F407xx 데이터시트 — Alternate function mapping 표 (PA2/PA3 = USART2 AF7) — <https://www.st.com/resource/en/datasheet/stm32f407vg.pdf>
- ST CMSIS device F4 (`stm32f4xx.h`, `stm32f407xx.h`: `GPIO_TypeDef`, `_Pos/_Msk`, `SET_BIT/MODIFY_REG`) — <https://github.com/STMicroelectronics/cmsis-device-f4>
- CMSIS-Core(M) 문서: Device Header File 관례, `__I/__O/__IO/__IM/__OM/__IOM`, `_VAL2FLD/_FLD2VAL` — <https://arm-software.github.io/CMSIS_6/latest/Core/index.html>
- CMSIS-SVD 사양 (헤더 생성의 원천) — <https://open-cmsis-pack.github.io/svd-spec/main/index.html>
- ISO/IEC 9899:2011 — 6.7.2.1 (구조체 멤버 배치), 6.7.3 (type qualifiers, const 객체 수정 UB), 6.7.10 (`_Static_assert`), 6.10.3.3 (`##` 연산자), 7.1.3 (예약 식별자), 7.19 (`offsetof`)
- Procedure Call Standard for the Arm Architecture (AAPCS) — 기본 타입 크기·정렬 (`unsigned long` 32비트) — <https://github.com/ARM-software/abi-aa>
- 관련 레슨: C-B01 (비트 set/clear/field, BSRR), C-B02 (정수 승격), SIV-B01 (레지스터 기술서 → 헤더 생성), BM-B02 (mock 프레임워크·훅), BM-B03 (RCC 클럭 게이팅·되읽기), BM-B04 (RMW 함정·W1C·BSRR), BM-B10 (클럭 트리), BM-B11 (GPIO 드라이버), BM-B12 (폴링 UART·BRR 반올림), COMM-B03 (baud 오차 허용), C-M14 / ARM-M10 (임계 구역), ARM-M11 (LDREX/STREX), CPP-A08 (링크 이음새)
