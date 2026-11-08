---
id: C-M01
time: 75분
updated: 2026-11-07
---
# C-M01 메모리 맵 레지스터 접근: volatile 포인터와 레지스터 구조체

## 학습 목표

- 데이터시트의 레지스터 표를 **두 방식** — `#define` 주소 매크로(`REG32(BASE + OFS)`)와 `volatile` 구조체 오버레이(`USART1->DR`) — 으로 옮기고, 둘이 **같은 바이트**를 가리킴을 테스트로 보인다.
- `(volatile uint32_t *)0x40011000u` 가 표준 C 에서 무엇인지(정수 → 포인터 = **구현 정의**, C11 6.3.2.3p5)와, 그래서 호스트 테스트용 가짜 베이스(정렬된 정적 배열)가 "모델" 일 뿐인 이유를 설명한다.
- reserved 구간이 있는 레지스터 블록을 정의하고 `offsetof` + `_Static_assert` 로 레이아웃을 **컴파일 타임에 고정**한다 (C-B12 의 연장).
- 접근 폭(16비트 멤버 → `strh`), 바이트 오프셋 vs 워드 인덱스, 인스턴스 번호 ↔ 베이스 주소 같은 면접 꼬리질문에 답한다.
- CMSIS `__I / __O / __IO` 와 STM32 USART 레지스터 이름(SR/DR/BRR/CR1 vs ISR/RDR/TDR)을 연결하고, 오버레이 위에 `uart_init / uart_putc / uart_getc` 를 작성한다.

## 1. 문제

펌웨어의 모든 드라이버는 결국 "주소 X 에 값 Y 를 쓴다" 이고, 면접에서 *"레지스터 0x40011004 에 0x41 을 써 보세요 → 그걸 구조체로 바꿔 보세요 → 왜 volatile? → 패딩 끼면?"* 은 화이트보드 단골 연쇄 질문이다.

C 에는 "주변장치" 라는 개념이 없다. 그래서 펌웨어는 두 가지 관용구로 레지스터를 C 의 lvalue 로 만든다.

1. **`#define` 주소 방식** — 주소 정수를 `volatile` 포인터로 캐스트해 역참조: `#define USART1_DR (*(volatile uint32_t *)(0x40011000u + 0x04u))`. 레지스터마다 매크로 하나.
2. **구조체 오버레이(struct overlay)** — 레지스터 블록과 바이트 단위로 똑같이 생긴 구조체를 정의하고, 베이스 주소를 그 구조체 포인터로 캐스트: `#define USART1 ((USART_TypeDef *)0x40011000u)` → `USART1->DR = 0x41;`. CMSIS 와 모든 벤더 헤더(STM32, nRF, NXP)가 이 방식이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">VUART 레지스터 맵 하나, 접근 방식 둘 — 둘 다 BASE + 0x04 의 같은 32비트를 가리켜야 한다</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="12" y="70">0x00</text><text x="12" y="100">0x04</text><text x="12" y="130">0x08</text>
    <text x="12" y="160">0x0C</text><text x="12" y="190">0x10</text><text x="12" y="240">0x20</text>
  </g>
  <g stroke="currentColor">
    <rect x="60" y="50" width="200" height="30" class="f-acc"/>
    <rect x="60" y="80" width="200" height="30" class="f-ok"/>
    <rect x="60" y="110" width="200" height="30" class="f-acc"/>
    <rect x="60" y="140" width="200" height="30" class="f-acc"/>
    <rect x="60" y="170" width="200" height="50" class="f-warn"/>
    <rect x="60" y="220" width="200" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="160" y="70">SR  (TXE b7, RXNE b5)</text>
    <text x="160" y="100">DR  (데이터 8비트)</text>
    <text x="160" y="130">BRR (분주값)</text>
    <text x="160" y="160">CR1 (UE b13, TE b3, RE b2)</text>
    <text x="160" y="192">RESERVED0[4]</text>
    <text x="160" y="210" font-size="12">0x10–0x1F 접근 금지</text>
    <text x="160" y="240">ID  (읽기 전용)</text>
  </g>
  <rect x="400" y="50" width="330" height="54" rx="6" stroke="currentColor" fill="none"/>
  <text x="410" y="70" fill="currentColor" font-weight="bold">① #define 주소 방식</text>
  <text x="410" y="92" fill="currentColor" font-family="monospace" font-size="12">VUART1_DR = REG32(BASE + 0x04u)</text>
  <rect x="400" y="120" width="330" height="54" rx="6" stroke="currentColor" fill="none"/>
  <text x="410" y="140" fill="currentColor" font-weight="bold">② 구조체 오버레이</text>
  <text x="410" y="162" fill="currentColor" font-family="monospace" font-size="12">VUART1-&gt;DR   (offsetof == 0x04)</text>
  <path d="M398 80 L266 95" stroke="currentColor" fill="none"/>
  <path d="M266 95 l9 -6 l-1 9 z" fill="currentColor"/>
  <path d="M398 148 L266 97" stroke="currentColor" fill="none"/>
  <rect x="400" y="190" width="330" height="60" rx="6" stroke="currentColor" class="f-mut"/>
  <text x="410" y="210" fill="currentColor" font-weight="bold">③ 레이아웃 고정 (컴파일 타임)</text>
  <text x="410" y="230" fill="currentColor" font-family="monospace" font-size="12">_Static_assert(offsetof(VUART_TypeDef,</text>
  <text x="410" y="245" fill="currentColor" font-family="monospace" font-size="12">               ID) == 0x20, "...");</text>
  <text x="10" y="282" fill="currentColor">타깃: BASE = 0x40011000u — 정수 상수를 포인터로 캐스트 (구현 정의, 6.3.2.3p5)</text>
  <text x="10" y="302" fill="currentColor">호스트: BASE = (uintptr_t)periph_window — 1 KB 정렬 정적 배열. 진짜 하드웨어가 아닌 "모델"</text>
  <text x="10" y="322" fill="currentColor">VUART2 = BASE + 0x400 — 같은 구조체, 다른 베이스 (STM32F4 의 USART1 0x40011000 / USART6 0x40011400 처럼)</text>
</svg>
```

호스트(맥)에는 0x40011000 에 아무것도 없다. 그 주소를 역참조하면 바로 죽는다 (`demo_real_addr.c` 실측: exit 139 = SIGSEGV). 그래서 이번 하네스는 `static _Alignas(1024) uint32_t periph_window[512]` 를 **주변장치 창**으로 두고 `VUART1_BASE = (uintptr_t)periph_window` 로 정의한다. 드라이버 코드는 타깃과 한 글자도 다르지 않고, 베이스 매크로 두 줄만 바뀐다. 테스트는 같은 메모리를 `WIN(inst, ofs)` 로 **원시 워드**로 읽어 "데이터시트 오프셋에 정말 써졌나" 를 확인한다.

### Part A — 두 방식으로 정의하고 같은 바이트임을 보인다

1. `REG32(addr)` / `REG32_RO(addr)` 매크로를 완성한다. 시작 코드는 `volatile` 이 빠져 있다. 읽기 전용 쪽은 대입이 **컴파일 에러** 가 되게 한다. 테스트는 `_Generic` 으로 lvalue 의 한정자를 검사한다.
2. `VUART_TypeDef` 의 reserved 구간(0x10–0x1F)을 정확히 채우고 `ID` 를 읽기 전용으로 만든다.
3. `offsetof` + `_Static_assert` 로 오프셋 5개와 `sizeof == 0x24` 를 고정한다.
- 테스트: `&VUART1_DR == &VUART1->DR`, `#define` 으로 쓰고 구조체·원시 메모리로 읽기, 그 반대, VUART2 와의 독립성.

### Part B — 면접관이 파고드는 곳: 오프셋 단위, 접근 폭, 인스턴스

1. `vuart_reg_offset(u, reg)` — 레지스터 포인터에서 **데이터시트 바이트 오프셋**을 구한다 (디버그 로그 "VUART2 +0x0C 에 씀"). 시작 코드는 포인터 뺄셈이라 워드 인덱스(3)를 낸다.
2. `VUART16_TypeDef` — 같은 맵을 예전 STM32 SPL 헤더처럼 **16비트 멤버 + reserved 하프워드**로 정의한다. 16비트 쓰기가 상위 하프워드를 건드리지 않음을 확인한다.
3. `vuart_by_index(i)` / `vuart_index_of(u)` — 인스턴스 번호 ↔ 베이스 주소. 블록 중간 주소·NULL 은 -1.

### Part C — 오버레이 위의 작은 드라이버

- `uart_init(u, pclk, baud)` — `BRR = round(pclk/baud)`, 범위(16..0xFFFF) 밖이거나 `baud == 0` 이면 **레지스터를 하나도 건드리지 않고** -1. 부트로더가 UART 를 켜 둔 채 넘겨줬을 수 있으므로 끄기 → BRR → TE|RE → 마지막에 UE. 32비트 넘침 없이 반올림한다 (`4294000000 / 2000000` 케이스).
- `uart_putc(u, c, max_polls)` — TXE 를 폴링, 타임아웃이면 DR 을 건드리지 않고 -1.
- `uart_getc(u)` — RXNE 면 DR 하위 8비트, 아니면 -1.
- 테스트는 창 전체를 `0xA5A5A5A5` 로 채운 뒤 init 해서 **reserved·ID·VUART2 가 한 워드도 안 바뀌었는지** 본다.

> 쓰기 의미론(W1C, RMW 위험)은 C-M02, 비트필드로 필드를 표현하는 문제는 C-M03, volatile 의 의미 자체는 C-B09, 구조체 패딩 계산은 C-B12 에서 다룬다.

## 2. Problem (English)

> **Memory-mapped registers: `#define` addresses vs. a volatile register struct.**
>
> A UART peripheral has this map: `SR @0x00`, `DR @0x04`, `BRR @0x08`, `CR1 @0x0C`, reserved `0x10–0x1F`, read-only `ID @0x20`. Two instances live at `BASE` and `BASE + 0x400`. On the host, `BASE` is the address of a 1 KiB-aligned static array.
>
> 1. Write `REG32(addr)` and `REG32_RO(addr)` that turn an integer address into a volatile lvalue (read-only for the latter). Define `VUART_TypeDef` with explicit reserved members and a `const volatile` `ID`; pin every offset and the size with `offsetof` + `_Static_assert`. Show that `&VUART1_DR == &VUART1->DR`.
> 2. `uint32_t vuart_reg_offset(const VUART_TypeDef *u, const volatile uint32_t *reg)` returns the datasheet byte offset. Define the same map with 16-bit members (`VUART16_TypeDef`, size 0x10). Implement `vuart_by_index(i)` and `vuart_index_of(u)` (−1 for NULL, mid-block or out-of-range addresses).
> 3. `int uart_init(VUART_TypeDef *u, uint32_t pclk_hz, uint32_t baud)`: `BRR = round(pclk/baud)` must lie in `[16, 0xFFFF]`; on error return −1 without touching any register. Disable first, set BRR, then TE|RE, enable (UE) last. `uart_putc(u, c, max_polls)` and `uart_getc(u)` poll TXE / RXNE.
>
> Then explain: is `*(volatile uint32_t *)0x40011004 = 0x41;` valid ISO C? What changes if the struct members are not `volatile`?
>
> Example: `uart_init(VUART1, 16000000, 115200) == 0`, `BRR == 139`, `CR1 == 0x200C`; `uart_init(VUART1, 1000000, 115200) == -1` (divider 9 < 16).

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 줄들과 "23/48 passed" — TODO 를 채워 48/48 을 만든다
```

관찰용 파일 (하네스와 별개, 결과는 5. 풀이에 실측으로 실었다):

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_target.c -o -   # 두 방식의 Cortex-M4 asm
clang -std=c11 -fsyntax-only -DDIAG=1 demo_diag.c          # 1..4: 컴파일러가 잡아 주는 정의 실수
cc -std=c11 -Wall -Wextra -Werror -O2 demo_real_addr.c -o demo_real_addr && ./demo_real_addr   # 맥에서 0x40011000 읽기
clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -mthumb -O2 -S demo_packed.c -o -    # packed 레지스터 구조체
```

## 4. 힌트

<details>
<summary>힌트 1 — Part A: 캐스트 순서는 "정수 → volatile 포인터 → 역참조"</summary>

`REG32(a)` 는 `*( (volatile uint32_t *) (uintptr_t)(a) )` 모양이다. 바깥 `*` 까지 매크로에 넣어야 `VUART1_DR = 0x41;` 처럼 **변수처럼** 쓸 수 있고, 전체를 괄호로 감싸야 `&VUART1_DR` 같은 식에서 우선순위 사고가 안 난다 (C-B15). 읽기 전용은 포인터 대상 타입에 `const` 를 하나 더.

</details>

<details>
<summary>힌트 2 — Part A: reserved 크기는 "다음 오프셋 − 현재 오프셋"</summary>

CR1 이 0x0C 에서 끝나면(0x10) 다음 레지스터 ID 는 0x20. 비는 바이트 = 0x20 − 0x10 = 16 = `uint32_t` 4개. 숫자 4 를 직접 쓰기보다 `(0x20u - 0x10u) / 4u` 로 적으면 데이터시트와 대조하기 쉽다. 그리고 그 결과를 `_Static_assert(offsetof(...) == 0x20, ...)` 로 묶는다.

</details>

<details>
<summary>힌트 3 — Part B: 포인터 뺄셈은 "원소 개수" 를 준다</summary>

`uint32_t *` 끼리 빼면 4로 나눈 값이 나온다 (C-B05 의 ×4 함정의 역방향). 바이트가 필요하면 두 주소를 `uintptr_t` 로 바꿔 빼거나 `unsigned char *` 로 바꿔 뺀다. 16비트 구조체는 각 16비트 레지스터 뒤에 `uint16_t RESERVEDn` 을 하나씩.

</details>

<details>
<summary>힌트 4 — Part C: 검사를 모두 끝낸 다음에 첫 쓰기</summary>

에러면 "아무것도 안 건드림" 이 요구사항이므로 계산·범위 검사를 먼저 끝내고 레지스터에 쓴다. 반올림은 `(pclk + baud/2) / baud` 인데 `pclk + baud/2` 가 32비트를 넘을 수 있다 — `uint64_t` 로. CR1 은 `|=` 가 아니라 **통째로** 써야 부트로더가 남긴 비트가 사라진다.

</details>

## 5. 풀이

### Part A — 두 방식, 그리고 그 캐스트가 표준에서 뜻하는 것

```c
#define REG32(addr)    (*(volatile uint32_t *)(uintptr_t)(addr))
#define REG32_RO(addr) (*(const volatile uint32_t *)(uintptr_t)(addr))

typedef struct {
    REG_RW uint32_t SR;                              /* 0x00 */
    REG_RW uint32_t DR;                              /* 0x04 */
    REG_RW uint32_t BRR;                             /* 0x08 */
    REG_RW uint32_t CR1;                             /* 0x0C */
    uint32_t        RESERVED0[(0x20u - 0x10u) / 4u]; /* 0x10–0x1F */
    REG_RO uint32_t ID;                              /* 0x20 */
} VUART_TypeDef;
```

`REG32(VUART1_BASE + 0x04u)` 를 한 단계씩 풀면:

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="40" fill="currentColor" font-weight="bold">타깃 (Cortex-M4)</text>
  <g stroke="currentColor">
    <rect x="10" y="50" width="170" height="50" rx="6" class="f-acc"/>
    <rect x="195" y="50" width="170" height="50" rx="6" class="f-warn"/>
    <rect x="380" y="50" width="170" height="50" rx="6" class="f-acc"/>
    <rect x="565" y="50" width="170" height="50" rx="6" class="f-ok"/>
    <rect x="10" y="150" width="170" height="50" rx="6" class="f-mut"/>
    <rect x="195" y="150" width="170" height="50" rx="6" class="f-warn"/>
    <rect x="380" y="150" width="170" height="50" rx="6" class="f-warn"/>
    <rect x="565" y="150" width="170" height="50" rx="6" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="95" y="71" font-family="monospace" font-size="12">0x40011004u</text>
    <text x="95" y="90" font-size="12">정수 상수</text>
    <text x="280" y="71" font-family="monospace" font-size="12">(volatile uint32_t *)</text>
    <text x="280" y="90" font-size="12">정수→포인터: 구현 정의</text>
    <text x="465" y="71" font-family="monospace" font-size="12">*p = 0x41</text>
    <text x="465" y="90" font-size="12">volatile 접근 (6.7.3p7)</text>
    <text x="650" y="71" font-family="monospace" font-size="12">str r0, [r1, #4]</text>
    <text x="650" y="90" font-size="12">버스 쓰기 정확히 1회</text>
    <text x="95" y="171" font-family="monospace" font-size="12">periph_window</text>
    <text x="95" y="190" font-size="12">정적 배열 = 진짜 객체</text>
    <text x="280" y="171" font-family="monospace" font-size="12">(uintptr_t) + 0x04</text>
    <text x="280" y="190" font-size="12">포인터→정수: 구현 정의</text>
    <text x="465" y="171" font-family="monospace" font-size="12">(volatile uint32_t *)</text>
    <text x="465" y="190" font-size="12">정수→포인터: 구현 정의</text>
    <text x="650" y="171" font-family="monospace" font-size="12">periph_window[1]</text>
    <text x="650" y="190" font-size="12">평범한 RAM 쓰기</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M180 75 H192"/><path d="M365 75 H377"/><path d="M550 75 H562"/>
    <path d="M180 175 H192"/><path d="M365 175 H377"/><path d="M550 175 H562"/>
  </g>
  <text x="10" y="140" fill="currentColor" font-weight="bold">호스트 모델 (맥, 이 레슨의 하네스)</text>
  <text x="10" y="232" fill="currentColor">C 표준은 "그 번지" 를 약속하지 않는다 — 약속하는 것은 컴파일러/ABI 문서다.</text>
</svg>
```

표준 C 의 관점에서 정확히 말하면:

| 단계 | 표준의 분류 | 근거 | 실무에서의 의미 |
|---|---|---|---|
| 정수 → 포인터 `(T *)0x40011004u` | **구현 정의** (정렬이 틀릴 수도, 그 타입의 실체를 안 가리킬 수도, trap 표현일 수도) | 6.3.2.3p5 | GCC/clang 은 평평한 주소 공간에서 "비트 패턴 그대로" 로 문서화 |
| 포인터 → 정수 `(uintptr_t)p` | 구현 정의 (표현 못 하면 UB) | 6.3.2.3p6 | `uint32_t` 에 담으면 64비트 호스트에서 경고 (demo_diag 3) |
| `void *` ↔ `uintptr_t` 왕복 | 원래 포인터와 같다고 비교됨 — 단 `uintptr_t` 자체가 **선택적(optional)** 타입 | 7.20.1.4p1 | 정수 산술(+0x04) 뒤 되돌린 포인터는 이 보장 밖 → 다시 구현 정의 |
| volatile lvalue 로 접근 | 생략·병합 금지. 무엇이 "접근" 인지(폭·횟수)는 **구현 정의** | 6.7.3p7 | 자연 정렬된 32비트 → `ldr/str` 1개 (아래 asm) |
| 매핑 안 된 주소 역참조 | UB — 맥에선 SIGSEGV | 6.5.3.2p4 | `demo_real_addr`: `reading 0x40011000 ...` 출력 후 exit 139 |

즉 레지스터 접근은 **"표준이 금지하지는 않지만 의미는 컴파일러가 정하는"** 영역이다. 그래서 면접 답은 "합법이냐" 가 아니라 "구현 정의이고, 우리 툴체인은 이렇게 정의한다, 그리고 volatile 이 접근 횟수와 순서를 지킨다" 가 정답이다.

**호스트 모델이 성립하는 이유.** `periph_window` 는 진짜 `uint32_t` 배열 객체라서 `VUART1->BRR` 접근은 "유효 타입 `uint32_t` 인 메모리를 `volatile uint32_t` lvalue 로 읽기" 가 된다. 한정자만 다른 같은 타입이므로 strict aliasing 규칙(6.5p7) 안이다. 1 KB 정렬(`_Alignas(1024)`)은 구조체 정렬(4) 요구를 넉넉히 만족시키고, 실제 칩처럼 베이스의 하위 10비트가 0 이 되게 한다 (`(VUART1_BASE & 0x3FF) == 0` 테스트). 16비트 모델에 별도 `uint16_t` 창을 둔 것도 같은 이유다 — `uint32_t` 배열을 `uint16_t` lvalue 로 쓰면 6.5p7 밖이다. 진짜 하드웨어에서는 애초에 C 객체가 없으므로 이 규칙들 대신 컴파일러의 volatile 문서가 기준이 된다.

**`_Static_assert` 가 필요한 이유.** C 는 멤버 순서와 "첫 멤버 앞엔 패딩 없음" 만 보장한다 (6.7.2.1p15). 멤버 **사이** 패딩이 없는 것은 ABI(AAPCS, AArch64 PCS)가 `uint32_t` 를 4바이트 정렬로 정해서다. 그래서 레지스터 맵은 플랫폼마다 컴파일러에게 확인시킨다. reserved 를 빠뜨리면 이렇게 잡힌다 (`demo_diag.c -DDIAG=2`, 실제 출력):

```text
demo_diag.c:18:16: error: static assertion failed due to requirement '__builtin_offsetof(VUART_TypeDef, ID) == 32': VUART ID must be at 0x20 (datasheet)
demo_diag.c:18:44: note: expression evaluates to '16 == 32'
```

`offsetof` 가 `__builtin_offsetof` 로 펼쳐지는 것도 보인다 (C-B12 Q8).

**두 방식은 같은 기계어가 된다.** `demo_target.c` 를 `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S` 로 컴파일한 실제 출력 (지시어 생략):

```text
putc_define:                       ; while(!(USART1_SR & 0x80)){}  USART1_DR = c;
	movw	r1, #4096              ; 0x1000
	movt	r1, #16385             ; 0x4001 → r1 = 0x40011000
.LBB0_1:
	ldr	r2, [r1]               ; SR 읽기 (루프는 4번 펼쳐졌지만 매번 ldr)
	lsls	r2, r2, #24            ; bit7 → N 플래그
	bmi	.LBB0_4
	...
	bpl	.LBB0_1
.LBB0_4:
	str	r0, [r1, #4]           ; DR = 베이스 + 4
	bx	lr
putc_struct:                       ; USART1->SR / USART1->DR  — 위와 한 줄도 다르지 않다
putc_param:                        ; (USART_TypeDef *u) 인자 버전
.LBB2_1:
	ldr	r2, [r0]               ; 베이스는 r0 로 들어오고 오프셋은 즉치값
	...
	str	r1, [r0, #4]
read_id:
	movw	r0, #4128              ; 0x1020
	movt	r0, #16385
	ldr	r0, [r0]               ; ID @ 0x40011020
```

"구조체는 느리다" 는 오해다. 오프셋은 컴파일 타임 상수라서 `[베이스, #오프셋]` 주소 지정 하나로 끝난다. 차이는 코드 생성이 아니라 **설계**에 있다:

| | `#define` 주소 방식 | 구조체 오버레이 |
|---|---|---|
| 인스턴스 여러 개 | 인스턴스마다 매크로 세트, 또는 `REG32(base + OFS)` 로 base 를 넘김 | `USART_TypeDef *u` 하나로 모든 인스턴스 (`putc_param`) |
| 레이아웃 의존 | 없음 — 오프셋이 숫자로 박혀 있다 | 패딩·reserved 가 정확해야 함 → `_Static_assert` 필수 |
| 띄엄띄엄 있는 레지스터 | 자연스럽다 (오프셋만 적으면 됨) | 큰 reserved 배열이 필요 (예: 0x3FC 의 ID) |
| 디버거 | 숫자 주소만 보인다 | `p *USART1` 로 필드 이름별 표시 |
| 어셈블리(.S)·링커 스크립트와 공유 | 숫자 매크로라 `#include` 가능 | 불가 (C 타입) |
| 대표 사용처 | Linux `readl(base + OFS)`, Zephyr `sys_read32(base + OFS)`, 작은 부트롬 | CMSIS, STM32 HAL/LL, nRF MDK, NXP MCUXpresso |

**CMSIS 한정자.** `core_cm4.h` 에는 `#define __I volatile const` (C++ 에서는 `volatile` 만), `#define __O volatile`, `#define __IO volatile`, 그리고 구조체 멤버용 `__IM / __OM / __IOM` 이 있다. 쓰기 전용(`__O`)은 C 로 표현할 방법이 없어 그냥 `volatile` 이다 — 문서 역할만 한다. C++ 에서 `__I` 가 `const` 를 빼는 이유는 const 멤버가 있으면 그 구조체의 기본 생성·대입이 막히는 등 C++ 규칙과 충돌하기 때문이다. 이 레슨이 `REG_RO` 같은 다른 이름을 쓴 것은 `__I` 처럼 **밑줄 두 개로 시작하는 이름은 구현 예약어**(7.1.3)라서다 — CMSIS 는 사실상 툴체인 일부로 취급되어 그 이름을 쓴다.

`const volatile` 멤버의 부수 효과: 구조체 전체가 수정 가능한 lvalue 가 아니게 된다 (6.3.2.1p1). 레지스터 블록을 `*u = *v` 로 통째 복사하는 사고를 컴파일러가 막아 준다 (`-DDIAG=4`, 실제 출력):

```text
# -DDIAG=1:  u->ID = 0;
demo_diag.c:23:11: error: cannot assign to non-static data member 'ID' with const-qualified type 'const volatile uint32_t' (aka 'const volatile unsigned int')
# -DDIAG=4:  *u = *v;
demo_diag.c:30:8: error: cannot assign to lvalue with const-qualified data member 'ID'
```

### Part B — 오프셋 단위, 접근 폭, 인스턴스

**B-1 바이트 오프셋.** 시작 코드의 `reg - &u->SR` 는 `uint32_t` 원소 개수라서 CR1 에 대해 3 을 낸다. 정답은 정수 주소의 차:

```c
return (uint32_t)((uintptr_t)reg - (uintptr_t)u);
```

`(const volatile unsigned char *)reg - (const volatile unsigned char *)u` 도 바이트를 주지만, 포인터 뺄셈은 **같은 배열 객체 안** 일 때만 정의된다 (6.5.6p9) — 호스트 모델에선 성립하지만 진짜 레지스터에는 "배열 객체" 가 없다. 정수 차이는 그 조건이 없고(값의 의미만 구현 정의), 드라이버 디버그 로그에서 흔히 쓰는 형태다. ×4 함정의 정방향(`(uint32_t *)BASE + 0x04`)은 C-B05 에서 다뤘다.

**B-2 16비트 정의와 접근 폭.** STM32F4 의 예전 StdPeriph(SPL) 헤더는 USART 를 이렇게 정의했다:

```c
__IO uint16_t SR;  uint16_t RESERVED0;
__IO uint16_t DR;  uint16_t RESERVED1;   /* ... */
```

레지스터 안의 유효 비트가 16비트 이하라서다. 현재 CMSIS 디바이스 헤더(`stm32f4xx.h` 계열)는 `__IO uint32_t` 로 바뀌었다. 차이는 **버스 접근 폭**이다. `demo_target.c` 의 `write16` 실제 출력:

```text
write16:
	movw	r1, #4100           ; 0x40011004
	movt	r1, #16385
	strh	r0, [r1]            ; 16비트 쓰기 — 상위 하프워드는 버스에 나가지 않는다
```

STM32F4 USART 는 레퍼런스 매뉴얼에 "하프워드 또는 워드로 접근" 이라고 적혀 있어 둘 다 되지만, 주변장치에 따라 **서브워드 쓰기를 무시하거나, 바이트를 복제해 쓰거나, 버스 폴트**를 내는 것도 있다. 그래서 데이터시트의 "access width" 를 구조체 멤버 타입으로 정확히 옮겨야 한다. 테스트는 `periph16_window[3] = 0xFFFF` 를 두고 `VUART16->DR = 0x41` 뒤에도 그대로인지 본다. (volatile 비트필드의 접근 폭은 ABI 마다 다르다 → C-M03.)

`packed` 를 붙이면 더 나빠진다. `demo_packed.c` 를 Cortex-M0 로 컴파일한 실제 출력 — 포인터로 받은 packed 구조체의 32비트 레지스터 쓰기:

```text
wpp:                                ; void wpp(P *p, uint32_t v) { p->DR = v; }
	strb	r1, [r0, #4]
	lsrs	r2, r1, #24
	strb	r2, [r0, #7]
	lsrs	r2, r1, #16
	strb	r2, [r0, #6]
	lsrs	r1, r1, #8
	strb	r1, [r0, #5]                ; 바이트 쓰기 4번, 순서도 4→7→6→5
```

packed 는 "정렬 1" 이라 비정렬 접근 명령이 없는 M0 에선 바이트 단위로 쪼갠다. 같은 함수가 M4 에선 `str r1, [r0, #4]` 하나였다. 레지스터 구조체에 packed 는 **금지**다 (packed 일반론은 C-M04).

**B-3 인스턴스 번호 ↔ 베이스.**

```c
if (i >= VUART_NINST) return NULL;
return (VUART_TypeDef *)(VUART1_BASE + (uintptr_t)i * VUART_STRIDE);
```

정수로 계산하고 마지막에 한 번 캐스트한다. 역방향은 `d = addr - BASE` 가 stride 의 배수이고 범위 안일 때만 번호를 준다. 주의: **진짜 칩의 인스턴스는 등간격이 아니다.** STM32F4 에서 USART1 = 0x40011000, USART6 = 0x40011400 (APB2) 이지만 USART2 = 0x40004400, USART3 = 0x40004800 (APB1) 이다. 그래서 실제 드라이버는 계산 대신 표를 쓴다:

```c
static USART_TypeDef *const uart_tab[] = { USART1, USART2, USART3, USART6 };   /* 타깃에서 */
```

타깃에선 `(USART_TypeDef *)0x40011000u` 가 "정수 상수를 포인터로 캐스트한 것" 이라 **주소 상수(address constant)** 이고 (6.6p9), 이 표는 플래시(.rodata)에 들어간다. 호스트 모델의 `(uintptr_t)periph_window` 는 정수 상수식이 아니라서 같은 표를 정적 초기화할 수 없다 — 그래서 하네스는 함수로 계산한다. 모델과 타깃이 갈리는 지점이다.

### Part C — 드라이버: 쓰기 순서와 "아무것도 안 건드림"

```c
if (baud == 0u) return -1;
uint64_t div = ((uint64_t)pclk_hz + baud / 2u) / baud;
if (div < VUART_BRR_MIN || div > VUART_BRR_MAX) return -1;
u->CR1 = 0u;
u->BRR = (uint32_t)div;
u->CR1 = VUART_CR1_TE | VUART_CR1_RE;
u->CR1 = VUART_CR1_TE | VUART_CR1_RE | VUART_CR1_UE;
```

| 결정 | 이유 | 테스트 |
|---|---|---|
| 검사 먼저, 쓰기는 나중 | 실패 시 하드웨어 상태 불변 (반쯤 설정된 UART 가 제일 위험) | `baud=0`, 분주 9, 분주 80000 → 0x00–0x1F 전부 `0xA5A5A5A5` |
| `uint64_t` 반올림 | `4294000000 + 1000000` 은 32비트에서 넘쳐 32704 → 분주 0 → 잘못된 거절 | `BRR == 2147` |
| CR1 은 `=` | `|=` 는 부트로더가 남긴 비트(여기선 `0xA5A5A5A5`)를 그대로 둔다 | `CR1 == 0x200C` 정확히 |
| UE 는 마지막 | 많은 USART 는 UE=1 에서 프레임 설정 변경이 금지/무시된다 (예: STM32L4/H7 의 BRR 은 UE=0 일 때만 쓰기 가능) | (순서는 trace 모델이 있어야 검사 가능 — C-B09 방식) |
| reserved 를 안 건드림 | 예약 비트·영역 쓰기는 미래 실리콘에서 의미가 생길 수 있다 | 0x10–0x1F, ID, VUART2 불변 |

`16000000/115200 = 138.89` → 반올림 139 (`0x8B`). 버림(138)이면 보레이트 오차가 +0.64 % 로, 반올림(−0.08 %)보다 8배 나쁘다 (추가 연습 2).

**volatile 이 빠진 구조체였다면?** `demo_target.c` 의 `init_nv` (멤버에 volatile 없음), 같은 4줄의 실제 출력:

```svg
<svg viewBox="0 0 740 210" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">같은 init 4줄 — Cortex-M4 -O2 실제 출력의 버스 쓰기</text>
  <text x="10" y="62" fill="currentColor">volatile 멤버</text>
  <g stroke="currentColor">
    <rect x="130" y="42" width="140" height="30" class="f-ok"/>
    <rect x="280" y="42" width="140" height="30" class="f-ok"/>
    <rect x="430" y="42" width="140" height="30" class="f-ok"/>
    <rect x="580" y="42" width="150" height="30" class="f-ok"/>
    <rect x="130" y="112" width="140" height="30" class="f-warn" stroke-dasharray="5 4"/>
    <rect x="280" y="112" width="140" height="30" class="f-ok"/>
    <rect x="430" y="112" width="140" height="30" class="f-warn" stroke-dasharray="5 4"/>
    <rect x="580" y="112" width="150" height="30" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="200" y="62">CR1 = 0</text><text x="350" y="62">BRR = 139</text>
    <text x="500" y="62">CR1 = 0xC</text><text x="655" y="62">CR1 = 0x200C</text>
    <text x="200" y="132">(삭제됨)</text><text x="350" y="132">BRR = 139</text>
    <text x="500" y="132">(삭제됨)</text><text x="655" y="132">CR1 = 0x200C</text>
  </g>
  <text x="10" y="132" fill="currentColor">volatile 없음</text>
  <text x="10" y="172" fill="currentColor">"곧 덮어써질 저장(dead store)" 은 일반 메모리라면 지워도 된다 → CR1=0 이 사라져</text>
  <text x="10" y="192" fill="currentColor">UART 가 켜진 채로 BRR 이 바뀐다. str 4개 → 2개 (movw r0,#4104 / str / str [r0,#4]).</text>
</svg>
```

```text
init_struct:                   init_nv:
	movw r0, #4108  ; CR1     	movw r0, #4104   ; BRR
	movt r0, #16385           	movt r0, #16385
	movs r1, #0               	movs r1, #139
	str  r1, [r0]   ; CR1=0   	str  r1, [r0]    ; BRR=139
	movs r1, #139             	movw r1, #8204
	str  r1, [r0, #-4] ; BRR  	str  r1, [r0, #4] ; CR1=0x200C
	movs r1, #12              	bx   lr
	str  r1, [r0]   ; CR1=0xC
	movw r1, #8204
	str  r1, [r0]   ; CR1=0x200C
	bx   lr
```

`init_define` 는 `init_struct` 와 완전히 같은 출력이었다. CMSIS 가 volatile 을 **멤버마다** 붙이는 이유가 이것이다 — 포인터 선언에서 한 번 깜빡하면 위처럼 조용히 깨지지만, 타입에 박아 두면 잊을 수가 없다 (`volatile USART_NV *` 처럼 포인터 대상에 붙여도 멤버 접근이 volatile 이 되긴 한다 — 6.5.2.3p4, 추가 연습 1).

**STM32 USART 이름 대응.** 이 VUART 는 STM32F1/F2/F4 USART 의 앞 4개(SR 0x00, DR 0x04, BRR 0x08, CR1 0x0C; CR1 의 UE=bit13, TE=bit3, RE=bit2; SR 의 TXE=bit7, RXNE=bit5)와 같다. F4 에서 그 뒤는 CR2 0x10, CR3 0x14, GTPR 0x18 이다. 반면 F0/F3/F7/L4/H7 의 새 USART 는 **맵 자체가 다르다**: CR1 0x00, CR2 0x04, CR3 0x08, BRR 0x0C, … ISR 0x1C, ICR 0x20, RDR 0x24, TDR 0x28 — SR 대신 ISR(상태)+ICR(클리어), DR 대신 RDR/TDR 로 쪼개졌고 UE 는 CR1 bit0 이다. 같은 "USART" 라도 디바이스 헤더(`USART_TypeDef`)는 계열마다 다르고, 이 구조체들은 보통 벤더의 SVD 파일에서 생성된다. 레지스터 맵을 손으로 옮길 때 `_Static_assert` 를 거는 이유다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 48/48 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 48/48, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `#define UART_DR *(uint32_t *)0x40011004` | volatile 없음 → 폴링 루프 삭제·연속 쓰기 병합 (C-B09). 괄호 없음 → `&UART_DR` 등에서 우선순위 사고 | `(*(volatile uint32_t *)0x40011004u)` |
| `(volatile uint32_t *)BASE + 0x04` | 캐스트 후 더하면 원소 단위 → 0x10 바이트 이동 (C-B05) | `(volatile uint32_t *)(BASE + 0x04u)` |
| reserved 를 세다 틀림 (`RESERVED0[3]`) | 그 뒤 모든 레지스터가 4바이트씩 밀린다 — 런타임엔 "가끔 이상한 값" | `[(0x20 - 0x10) / 4]` + `_Static_assert(offsetof(...))` |
| `uint32_t base = 0x40011000u;` 를 64비트 호스트 테스트에도 그대로 | 포인터 ↔ 32비트 정수, 호스트 주소 상위 32비트 유실 (`-Wint-to-pointer-cast`, `-Wpointer-to-int-cast`, demo_diag 3) | 주소는 `uintptr_t` 로 보관 |
| 멤버에 volatile 없이 `USART_TypeDef * volatile p` | 포인터 변수만 volatile, 레지스터 접근은 일반 → init_nv 처럼 쓰기 삭제 | 멤버를 `volatile`, 또는 `volatile USART_TypeDef *` |
| 레지스터 구조체에 `__attribute__((packed))` | 정렬 1 가정 → M0 에서 `strb` ×4 (demo_packed) | packed 금지, 자연 정렬 + `_Static_assert` |
| 16비트 레지스터를 `uint32_t` 로, 혹은 반대로 | 버스 접근 폭이 달라져 무시·복제·폴트 | 데이터시트 access width 대로 멤버 타입 선택 |
| `CR1 |= UE | TE | RE;` 로 init | 리셋 값이 0 이라는 가정 — 부트로더/이전 펌웨어가 남긴 비트 잔존 | 필요한 값을 통째로 `=` (W1C 레지스터에서의 `|=` 함정은 C-M02) |
| 호스트에서 `#define USART1_BASE 0x40011000u` 그대로 테스트 | 매핑 안 된 주소 → SIGSEGV (demo_real_addr: exit 139) | 베이스만 정적 배열 주소로 바꾸는 호스트 모델 |

## 8. 면접 Q&A

<details>
<summary>Q1. `*(volatile uint32_t *)0x40011004 = 0x41;` 은 표준 C 인가요?</summary>

문법·제약 위반은 없지만 의미는 **구현 정의**다. 정수 → 포인터 변환 결과는 구현이 정하고 (6.3.2.3p5: 정렬이 틀릴 수도, 실체를 안 가리킬 수도, trap 표현일 수도 있다), volatile 객체에 대한 "접근" 이 무엇인지도 구현 정의다 (6.7.3p7). GCC/clang 은 평평한 주소 공간에서 비트 패턴 그대로의 주소로, 자연 정렬된 volatile 스칼라는 그 폭의 load/store 하나로 컴파일한다 (Cortex-M4 에서 `str r0, [r1, #4]` 1개 확인). 꼬리질문 "그럼 맥에서 돌리면?" → 그 가상 주소는 매핑이 안 돼 있어 SIGSEGV (실측 exit 139). Linux 커널에서도 물리 주소를 그대로 못 쓰고 `ioremap()` 으로 가상 주소를 받은 뒤 `readl()/writel()` 로 접근한다.

</details>

<details>
<summary>Q2. #define 방식과 구조체 방식 중 무엇을 쓰겠어요?</summary>

MCU 펌웨어면 구조체: 한 드라이버가 `USART_TypeDef *` 로 모든 인스턴스를 다루고, 디버거가 필드 이름으로 보여 주고, 벤더 헤더(CMSIS)가 이미 그렇다. 기계어는 같다 (`putc_define` 과 `putc_struct` 출력이 동일). 단 레이아웃을 `_Static_assert` 로 고정해야 한다. `#define`/base+offset 은 어셈블리·링커 스크립트와 공유해야 할 때, 레지스터가 넓은 주소 범위에 흩어져 있을 때, Linux/Zephyr 처럼 접근자 함수(`readl(base + OFS)`, `sys_write32`)가 배리어·엔디안을 함께 처리해야 할 때 쓴다.

</details>

<details>
<summary>Q3. 구조체 멤버 사이에 패딩이 끼면 어떻게 되나요? 어떻게 막나요?</summary>

C 표준은 멤버 사이 패딩을 금지하지 않는다 (6.7.2.1p15 는 "맨 앞에만 없다" 만 보장). 실제로 끼지 않는 건 ABI 가 `uint32_t` 를 4바이트 정렬로 정하기 때문이다. 그래서 ① 레지스터는 고정 폭 자연 정렬 타입만 ② 빈 구간은 `RESERVEDn[]` 로 명시 ③ 표의 한 줄마다 `_Static_assert(offsetof(T, REG) == 0xNN)` + 전체 `sizeof`. packed 로 "해결" 하면 안 된다 — 정렬 1 이 되어 M0 에서 `strb` 4번으로 쪼개진다 (demo_packed 실측).

</details>

<details>
<summary>Q4. CMSIS 의 __I, __O, __IO 는 뭐고, __O 는 왜 그냥 volatile 인가요?</summary>

`__I` = `volatile const` (읽기 전용, 대입하면 컴파일 에러 — demo_diag 1), `__O` = `volatile`, `__IO` = `volatile`. 구조체 멤버용은 `__IM/__OM/__IOM`. C 의 타입 시스템엔 "읽기 금지" 한정자가 없어서 쓰기 전용은 문서 표시일 뿐이다 — `x = USART->TDR` 같은 읽기는 컴파일되고, 하드웨어가 0 을 돌려주거나 부작용을 낼 수 있다. C++ 에서 `__I` 는 `volatile` 만이다 (const 멤버가 C++ 의 생성·대입 규칙과 충돌). 꼬리질문 "const volatile 은 모순 아닌가?" → const 는 "이 코드가 안 쓴다", volatile 은 "다른 무언가가 바꾼다" (C-B09 Q3).

</details>

<details>
<summary>Q5. volatile 을 멤버에 붙이는 것과 포인터에 붙이는 것은 어떻게 다르죠?</summary>

`volatile NV_TypeDef *p` — 가리키는 구조체가 volatile 이면 `p->DR` 도 volatile lvalue 다 (6.5.2.3p4: "포인터가 한정된 타입을 가리키면 멤버도 그 한정자를 갖는다"). 이것도 동작한다. `NV_TypeDef *volatile q` — 포인터 **변수** 가 volatile 이고 `q->DR` 는 일반 접근 (추가 연습 1 의 `_Generic` 판정). CMSIS 가 멤버에 붙이는 이유: 사용자가 포인터를 어떻게 선언하든 레지스터 접근이 volatile 이 되도록 타입에 박아 두는 것. 멤버에 없으면 `init_nv` 처럼 CR1=0 이 조용히 사라진다.

</details>

<details>
<summary>Q6. 레지스터 접근 폭은 왜 중요한가요? uint8_t 로 DR 에 쓰면 안 되나요?</summary>

멤버 타입이 곧 버스 트랜잭션 폭이다 (`uint16_t` → `strh`, `uint8_t` → `strb`). 주변장치는 APB 슬레이브로서 폭별 동작이 다르다: 둘 다 허용(STM32F4 USART: 하프워드·워드), 서브워드 쓰기 무시, 바이트를 4 레인에 복제, 혹은 에러 응답 → BusFault. 그리고 레지스터 일부 바이트에 쓰면 나머지 바이트가 0 으로 쓰여 다른 필드를 지우는 경우도 있다. 그래서 데이터시트 "access: word" 를 그대로 멤버 타입에 반영한다. volatile 비트필드는 접근 폭이 ABI 에 달려 있다 (AAPCS 는 선언 타입 폭으로 접근하라고 정함) → C-M03.

</details>

<details>
<summary>Q7. 인스턴스 테이블 `static USART_TypeDef *const tab[] = {USART1, USART2};` 은 정적 초기화가 되나요?</summary>

타깃에선 된다: `(USART_TypeDef *)0x40011000u` 는 "정수 상수를 포인터로 캐스트" 한 **주소 상수** 라서 (6.6p9) 정적 초기화식으로 쓸 수 있고, `const` 표라 플래시에 놓인다. 이 레슨의 호스트 모델은 베이스가 `(uintptr_t)periph_window` 라 정수 상수식이 아니어서 같은 표를 못 만든다 (구현이 확장으로 받아 줄 수는 있지만 6.6p10 — 보장 아님). 그리고 진짜 STM32F4 USART 들은 등간격이 아니라 (USART1 0x40011000, USART2 0x40004400) stride 계산보다 표가 맞다.

</details>

<details>
<summary>Q8. UART init 순서를 설명해 보세요. 왜 UE 가 마지막이죠?</summary>

① 클럭 enable (RCC, 여기선 생략) ② UE=0 으로 끄기 — 부트로더가 켜 뒀을 수 있다 ③ BRR, 프레임 형식(M, PCE, STOP) ④ TE/RE ⑤ UE=1. 많은 USART 는 UE=1 동안 BRR·M 같은 설정을 쓸 수 없거나(STM32L4/H7 RM: "UE=0 일 때만 쓰기 가능") 진행 중인 프레임을 깨뜨린다. 범위 검사는 첫 쓰기 **전에** 끝내야 실패 시 하드웨어 상태가 그대로다. volatile 이 이 "중간 상태" 쓰기들을 지킨다 — 없으면 dead store 로 지워진다 (init_nv 실측).

</details>

<details>
<summary>Q9. 이 레지스터 맵 헤더는 실무에서 누가 만들죠? 손으로 옮기면 어떻게 검증하나요?</summary>

벤더가 CMSIS-SVD(XML)로 레지스터·필드·접근 권한을 기술하고, 거기서 `SVDConv` 같은 도구로 C 헤더를(Rust 는 `svd2rust`) 생성한다. 손으로 옮길 땐 ① 표 한 줄마다 `offsetof` `_Static_assert` ② 블록 `sizeof` ③ 실제 보드에서 디버거로 `x/4wx 0x40011000` 과 구조체 출력 대조 ④ 리셋 값 테스트(모든 레지스터를 읽어 데이터시트 리셋 값과 비교). 실리콘 밸리데이션에선 ④ 를 "register reset-value / read-write test" 로 자동화한다 — 이때 W1C·RO 비트를 구분해야 하는 게 C-M02 의 주제다.

</details>

## 9. 추가 연습

### 연습 1 — 한정자는 어디에 붙나

멤버에 volatile 이 없는 `NV_TypeDef { uint32_t SR, DR; }` 에 대해 `volatile NV_TypeDef *p`, `NV_TypeDef *volatile q`, 그리고 `const VUART_TypeDef *c` 일 때 `p->DR`, `q->DR`, `c->DR`, `VUART1->ID` 의 타입을 `_Generic` 으로 판정하라.

<details>
<summary>답 보기</summary>

`p->DR` → `volatile uint32_t` (6.5.2.3p4: 포인터가 가리키는 타입의 한정자가 멤버로 전파), `q->DR` → `uint32_t` (volatile 은 포인터 변수에만), `c->DR` → `const volatile uint32_t` (구조체의 const + 멤버의 volatile), `VUART1->ID` → `const volatile uint32_t`. `_Generic(&(lv), ...)` 처럼 **주소**를 넣어야 한정자가 보인다 — lvalue 를 그대로 넣으면 lvalue 변환에서 최상위 한정자가 떨어진다 (C-B09 연습 1).

</details>

### 연습 2 — STM32F4 BRR 가수/소수와 보레이트 오차

STM32F4 USART (OVER8=0) 의 `USARTDIV = fck / (16 × baud)` 이고 BRR = 가수 12비트 `<<4` | 소수 4비트. `brr_f4(pclk, baud)` 와 실제 보레이트 오차(ppm) `baud_error_ppm` 을 구하라. 84 MHz / 115200 은?

<details>
<summary>답 보기</summary>

소수 4비트 = 1/16 단위이므로 `BRR = round(16 × USARTDIV) = round(fck / baud)` — Part C 의 분주와 같은 식이다. 84 MHz / 115200 = 729.17 → 729 = 0x2D9 → 가수 45 (0x2D), 소수 9 → USARTDIV 45.5625. 실제 보레이트 = 84e6/729 = 115226.3 → **+228 ppm** (+0.023 %). 16 MHz / 115200 → 139 (0x8B, 가수 8 소수 11) → −799 ppm, 16 MHz / 9600 → 1667 → −199 ppm, 42 MHz / 9600 → 4375 로 나누어떨어져 0. UART 는 보통 양쪽 합쳐 ±2 % 안팎까지 버티므로 이 값들은 넉넉하다. 16 MHz / 115200 에서 반올림 대신 버림(138)이었다면 16e6/138 = 115942 → +6441 ppm 으로 8배 나빠진다.

</details>

### 연습 3 — Cortex-M3/M4 비트밴드 별칭 주소

비트밴드(bit-band) 영역 SRAM 0x20000000–0x200FFFFF → 별칭 0x22000000, 주변장치 0x40000000–0x400FFFFF → 0x42000000. `bb_alias(addr, bit)` 을 구현하라 (영역 밖이면 0). USART1 SR(0x40011000) 의 TXE(bit7) 별칭은?

<details>
<summary>답 보기</summary>

`alias = alias_base + (addr − region_base) × 32 + bit × 4` — 원래 1비트가 별칭 영역에서 워드 하나(4바이트)로 늘어나므로 바이트당 32바이트. USART1 SR bit7 → 0x42000000 + 0x11000×32 + 28 = **0x4222001C**. 별칭 워드에 1 을 쓰면 그 비트만 원자적으로 set 된다 (하드웨어가 RMW 를 버스 수준에서 대신한다 — C-B01 Part C 의 RMW 경쟁 해법 중 하나). 단 비트밴드는 M3/M4 의 선택 기능이고 M0/M7/M33 에는 없다. 그리고 W1C/read-to-clear 레지스터에 쓰면 하드웨어 RMW 가 다른 비트를 건드릴 수 있어 위험하다 (C-M02).

</details>

### 연습 4 — offsetof 표로 레지스터 덤프

디버그 셸의 `uart dump` 명령: `{name, offsetof}` 표를 순회해 `"SR=00000080 DR=00000041 BRR=0000008B CR1=0000200C ID=56554152"` 를 만들어라. reserved 는 덤프하지 않고, 버퍼가 모자라면 0 을 반환한다.

<details>
<summary>답 보기</summary>

`static const struct { const char *name; size_t ofs; } regs[] = { {"SR", offsetof(VUART_TypeDef, SR)}, ... };` — `offsetof` 는 정수 상수식이라 정적 초기화에 쓸 수 있고 표는 .rodata 로 간다. 읽기는 `*(const volatile uint32_t *)((uintptr_t)u + regs[i].ofs)` 로 레지스터마다 **정확히 한 번**. read-to-clear 레지스터(예: 일부 칩의 상태 레지스터, DR)는 덤프가 상태를 바꾼다는 점을 표에 플래그로 표시해 건너뛰는 게 실무 관례다. `snprintf` 반환값이 남은 공간 이상이면 잘린 것.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 19/19 passed
```

## 10. 핵심 정리

- 레지스터 = "주소 정수 → `volatile` 포인터 → 역참조" 로 만든 lvalue. 정수 → 포인터는 **구현 정의**(6.3.2.3p5)이고 의미는 툴체인이 정한다.
- `#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))` 와 `#define USART1 ((USART_TypeDef *)BASE)` 는 같은 `ldr/str [base, #ofs]` 를 만든다. 차이는 설계(인스턴스·디버거·레이아웃 의존).
- 구조체 오버레이 = reserved 명시 + `_Static_assert(offsetof(T, REG) == 0xNN)` + `sizeof`. packed 금지.
- volatile 은 **멤버에** (CMSIS `__I/__O/__IO`). 빠지면 init 의 중간 쓰기가 dead store 로 지워진다 (실측 str 4개 → 2개).
- 멤버 타입 = 버스 접근 폭 (`uint16_t` → `strh`). 데이터시트 access width 를 따른다.
- 바이트 오프셋은 `uintptr_t` 차이로, 포인터 뺄셈은 원소 개수. 인스턴스는 등간격이 아니므로 표(주소 상수, 6.6p9).
- 호스트 테스트 = 정렬된 정적 배열을 베이스로 쓰는 **모델**. 베이스 매크로 두 줄만 다르고 드라이버 코드는 같다.
- init: 검사 → 끄기 → BRR → TE/RE → UE. 실패하면 한 레지스터도 건드리지 않는다.

## 참고자료

- C11 (N1570) 6.3.2.3p5–6 정수↔포인터 변환, 6.5p7 유효 타입, 6.5.2.3p4 멤버 접근의 한정자, 6.5.6p9 포인터 뺄셈, 6.6p9–10 주소 상수, 6.7.2.1p15 구조체 레이아웃, 6.7.3p7 volatile, 7.1.3 예약 식별자, 7.20.1.4 `uintptr_t` — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- [ARM CMSIS-Core — Peripheral Access (`__I`, `__O`, `__IO`, `__IM`, `__OM`, `__IOM`)](https://arm-software.github.io/CMSIS_6/latest/Core/index.html)
- [CMSIS-SVD — System View Description 형식](https://open-cmsis-pack.github.io/svd-spec/main/index.html)
- STMicroelectronics RM0090 (STM32F405/407 Reference Manual) — USART 레지스터 맵(SR/DR/BRR/CR1), BRR 가수·소수, 메모리 맵(USART1 0x40011000)
- STMicroelectronics RM0351 (STM32L4 Reference Manual) — 새 USART 맵(ISR/ICR/RDR/TDR), BRR 쓰기 조건
- ARM Cortex-M4 Technical Reference Manual / ARMv7-M ARM — bit-banding 영역과 별칭 계산
- [GCC manual — When is a Volatile Object Accessed?](https://gcc.gnu.org/onlinedocs/gcc/Volatiles.html)
- [Linux kernel — Bus-Independent Device Accesses (`ioremap`, `readl`/`writel`)](https://www.kernel.org/doc/html/latest/driver-api/device-io.html)
- 관련 레슨: C-B05 (×4 오프셋 함정), C-B09 (volatile 의 의미와 한계), C-B12 (패딩·offsetof·reserved), C-B15 (매크로 괄호), C-M02 (W1C·RMW), C-M03 (비트필드 레이아웃), C-M04 (packed)
