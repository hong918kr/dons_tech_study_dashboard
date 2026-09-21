# 03. 레지스터 비트필드 매크로와 read-modify-write

> **주제**: 비트 조작 · MMIO · volatile · W1C · **난이도**: 기초 · **목표 시간**: 25분
> **해설은 보지 말 것**: 먼저 `starters/03_reg_bitfield.c`를 채워 `make run N=03`으로 통과시킨다.

## 면접관의 문장

"Let's warm up with registers. Suppose you have a memory-mapped peripheral: a 32-bit CTRL register with a few single-bit enables and two multi-bit fields, and a 32-bit STATUS register whose low bits are read-only and whose error flags are write-1-to-clear. Write the generic macros you'd put in a BSP header to build masks and to pack and extract fields, then write the accessor functions on top of them. I want a multi-bit field update that provably does not disturb its neighbours or the reserved bits. Then use them to configure the UART and to service its error flags from an ISR. At the end, tell me where a plain read-modify-write is the wrong instinct, and what you'd say if I asked you to map the register with a C struct bit-field instead."

## 요구사항

1. `BIT(n)`은 bit `n` 하나만 1인 32-bit 마스크를 만든다. `n`은 0..31. 결과 타입은 항상 `uint32_t`여야 하고, `1 << 31`류의 signed overflow가 없어야 한다.
2. `GENMASK(hi, lo)`는 bit `hi`..`lo`가 연속으로 1인 마스크를 만든다. `0 <= lo <= hi <= 31`. `GENMASK(31, 0)`은 `0xFFFFFFFF`여야 한다.
3. `FIELD_MASK(width, shift)`는 `shift`에서 시작하는 `width` 비트 마스크다. `1 <= width <= 32`이고, 모든 유효 입력에서 `FIELD_MASK(width, shift) == GENMASK(shift + width - 1, shift)`가 성립해야 한다.
4. `FIELD_SHIFT(mask)`는 `mask`의 최하위 1비트 위치를 준다. `mask != 0`을 가정해도 된다.
5. `FIELD_PREP(mask, val)`은 `val`을 그 필드 자리로 올린다. 필드 폭을 넘는 상위 비트는 **잘라낸다**(이웃 필드를 오염시키면 안 된다). `FIELD_GET(mask, reg)`는 반대로 필드 값을 0부터 시작하는 정수로 꺼낸다. 두 매크로는 모든 유효 값에서 왕복이 성립해야 한다.
6. `FIELD_FITS(mask, val)`은 `val`이 잘림 없이 들어가면 `true`다. 파라미터 검증(`assert`)에 쓴다.
7. 다섯 매크로 모두 상수 인자로 호출하면 **컴파일 타임 상수**로 접혀야 한다. 즉 함수 호출이나 런타임 루프를 쓰지 않는다.
8. `reg_update(reg, mask, val)`은 `mask` 영역만 `val`로 바꾸고 나머지 비트(이웃 필드, reserved)는 **읽은 값 그대로** 되쓴다. `reg`에 대한 bus read는 정확히 1회, bus write도 정확히 1회여야 한다.
9. `reg_read` / `reg_write` / `reg_set_bits` / `reg_clear_bits` / `reg_get_field` / `reg_set_field`를 위 매크로와 `reg_update` 위에 얹는다. `reg_set_bits`는 멱등이어야 한다(이미 1인 비트를 다시 세워도 다른 변화가 없다).
10. `reg_w1c(reg, mask)`는 write-1-to-clear 레지스터 전용이다. **읽지 않고** `mask` 값 자체를 1회 write한다.
11. `uart_config(ctrl, bauddiv, parity, two_stop, rxtrig)`는 순서대로 (1) `EN`을 내리고 (2) `PARITY`, `STOP2`, `BAUDDIV`, `RXTRIG`만 교체하고 (3) `EN | TXE | RXE`를 올린다. `LOOPBACK`과 reserved 비트 `[31:24]`는 호출 전후로 값이 같아야 한다.
12. `uart_set_baud`는 `BAUDDIV` 필드만, `uart_get_baud`는 `BAUDDIV` 필드만 다룬다.
13. `uart_rx_level`은 STATUS의 read-only 필드 `RXLVL[7:4]`를 꺼낸다. STATUS에 write해도 read-only 비트는 변하지 않아야 한다(시뮬레이터가 그렇게 동작한다).
14. `uart_pending_errors`는 STATUS에서 W1C 에러 비트만 돌려준다. `uart_clear_errors(hw, mask)`는 `mask` 중 W1C 비트만 클리어하고, **다른 pending 플래그는 살려 둔다**. bus write는 1회.
15. `uart_clear_errors_rmw_BUG`는 일부러 틀린 구현이다. STATUS를 읽어 `(읽은 값 | mask)`를 되쓴다. 테스트는 이 구현이 처리하지도 않은 플래그를 잃는 것을 확인한다.
16. `spi_config(ctrl, div, bits, mode)`는 S01 드릴과 같은 레이아웃이다. `mode` bit1이 CPOL, bit0이 CPHA이고, `WORD` 필드에는 `bits - 1`을 넣는다. 설정 변경 전에 `EN`을 내린다.

## 인터페이스

```c
#define BIT(n)                   /* uint32_t, bit n */
#define GENMASK(hi, lo)          /* uint32_t, bit hi..lo */
#define FIELD_MASK(width, shift) /* uint32_t, shift에서 width비트 */
#define FIELD_SHIFT(mask)        /* uint32_t, mask의 최하위 1비트 위치 */
#define FIELD_PREP(mask, val)    /* uint32_t, val을 필드 자리로 (초과분 절단) */
#define FIELD_GET(mask, reg)     /* uint32_t, 필드 값 추출 */
#define FIELD_FITS(mask, val)    /* bool, 잘림 없이 들어가는가 */

static inline uint32_t reg_read(const volatile uint32_t *reg);
static inline void     reg_write(volatile uint32_t *reg, uint32_t val);
static inline void     reg_update(volatile uint32_t *reg, uint32_t mask, uint32_t val);
static inline void     reg_set_bits(volatile uint32_t *reg, uint32_t mask);
static inline void     reg_clear_bits(volatile uint32_t *reg, uint32_t mask);
static inline uint32_t reg_get_field(const volatile uint32_t *reg, uint32_t mask);
static inline void     reg_set_field(volatile uint32_t *reg, uint32_t mask, uint32_t val);
static inline void     reg_w1c(volatile uint32_t *reg, uint32_t mask);

static void     uart_config(volatile uint32_t *ctrl, uint32_t bauddiv, uint32_t parity,
                            bool two_stop, uint32_t rxtrig);
static void     uart_set_baud(volatile uint32_t *ctrl, uint32_t bauddiv);
static uint32_t uart_get_baud(const volatile uint32_t *ctrl);
static uint32_t uart_rx_level(uart_sim_t *hw);
static uint32_t uart_pending_errors(uart_sim_t *hw);
static void     uart_clear_errors(uart_sim_t *hw, uint32_t mask);
static void     uart_clear_errors_rmw_BUG(uart_sim_t *hw, uint32_t mask);
static void     spi_config(volatile uint32_t *ctrl, uint32_t div, uint32_t bits, int mode);
```

레지스터 레이아웃은 전부 설명용 가짜다. 뼈대 파일에 이미 들어 있다.

```text
UART CTRL (R/W)
 31        24 23   20 19         8  7   6   5  4  3  2  1  0
+------------+-------+------------+---+---+------+--+--+--+--+
|  reserved  |RXTRIG |  BAUDDIV   |RTS|ST2|PARITY|LB|RX|TX|EN|
+------------+-------+------------+---+---+------+--+--+--+--+

UART STATUS
 31      12  11  10   9    8   7    4  3   2     1     0
+----------+---+----+----+----+------+---+-----+-----+-----+
| reserved |BRK|PERR|FERR|OVR |RXLVL |rsv|TXEMP|RXNE |TXRDY|
+----------+---+----+----+----+------+---+-----+-----+-----+
 bit 11..8 = write-1-to-clear,  bit 7..0 = read-only
```

## 제약

- 동적 할당 금지. 하드웨어/RTOS 헤더 금지(레지스터는 `volatile uint32_t`로 흉내 낸다).
- `cc -std=c11 -Wall -Wextra -O2`에서 경고 0개.
- 매크로는 상수 인자에서 컴파일 타임에 접혀야 한다. 실제 타깃에서 `reg_update` 한 번이 `LDR` / `BIC` / `ORR` / `STR` 수준으로 떨어지는 것이 목표다.
- 호출자는 task 문맥과 ISR 문맥 둘 다일 수 있다고 가정한다. 동기화를 코드로 넣지는 않되, 어디가 위험한지 말할 수 있어야 한다.
- 뼈대에 있는 레지스터 레이아웃, 하드웨어 시뮬레이터, 테스트는 수정하지 않는다.

## 예시 동작

```text
CTRL 초기값                0xA5000000  (reserved 비트만 세워 둔 상태)
uart_config(div=0x0D1, parity=ODD, two_stop=true, rxtrig=0x8)

  1) EN 내리기            0xA5000000 -> 0xA5000000   (원래 0이었다)
  2) 필드 교체            0xA5000000 -> 0xA580D160
     BAUDDIV=0x0D1, RXTRIG=0x8, PARITY=2, STOP2=1
  3) EN|TXE|RXE 올리기    0xA580D160 -> 0xA580D167

  reserved[31:24] = 0xA5  그대로,  LOOPBACK(bit3) = 0 그대로
```

```text
STATUS W1C 타임라인 (OVERRUN과 FRAMEERR가 동시에 pending)

  hw.status = ...0011_0000_0000   (OVR=1, FERR=1)

  올바른 코드:  write 0x0000_0100   -> hw.status = ...0010_0000_0000 (FERR 살아있음)
  틀린 코드:    read  0x0000_0300
                write 0x0000_0300   -> hw.status = ...0000_0000_0000 (FERR 유실!)
```

## 스스로 점검할 질문

1. `GENMASK(31, 0)`이 왜 `0xFFFFFFFFu >> (31 - 31)`으로 안전한가. `1u << 32`로 만들면 무슨 일이 생기나.
2. `1UL << n`으로 마스크를 만들면 ARM 타깃과 x86-64 host에서 결과가 어떻게 달라지나.
3. `reg_update`에서 read와 write가 각각 정확히 1회임을 어떻게 보장하나. `volatile`을 빼면 컴파일러가 무엇을 할 수 있나.
4. `FIELD_PREP`의 `& mask`를 빼면 어떤 입력에서 어떤 버그가 나나.
5. W1C STATUS에 `reg |= BIT(8)` 하면 정확히 어떤 비트들이 사라지나. 그게 왜 "인터럽트 유실"인가.
6. ISR과 task가 같은 CTRL을 `reg_update` 하면 어떤 시나리오에서 한쪽 변경이 사라지나. 해결책 세 가지는?
7. 레지스터를 C `struct` bit-field로 매핑하면 구현 정의(implementation-defined)인 것이 무엇 무엇인가.
8. 인터럽트 플래그를 지우고 바로 ISR을 빠져나왔는데 같은 인터럽트가 또 걸렸다. 왜인가.

## follow-up (면접관이 이어서 물을 것)

1. "Show me what this compiles to on Cortex-M. How many bus transactions per `uart_set_baud`?"
2. "Two writers, an ISR and a task, both touch CTRL. Fix it without disabling interrupts for long."
3. "The vendor header maps this register as a `struct` with bit-fields. Would you use it?"
4. "`volatile` versus `_Atomic` versus a memory barrier here. Which do you actually need, and for what?"
5. "How would you unit-test a driver like this on the host, with no hardware?"
