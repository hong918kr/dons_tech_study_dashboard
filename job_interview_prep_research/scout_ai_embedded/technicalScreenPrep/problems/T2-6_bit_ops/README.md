# T2-6 · Bit Ops on a Sensor Status Register

> **ID**: T2-6 · **Priority**: P0 · **Pattern**: mask & shift · Kernighan popcount · swap stages · **Time**: 25 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh T2-6` / `./run.sh T2-6 sol`

## 1. Problem

A temperature-sensor IC exposes a 32-bit `STATUS` register that the firmware reads over SPI:

| Bits | Name | Meaning |
|---|---|---|
| 0 | `OT_FAULT` | over-temperature |
| 1 | `UV_FAULT` | under-voltage |
| 2 | `CRC_ERR` | last SPI frame had a bad CRC |
| 3 | `DATA_RDY` | new sample available |
| 4..6 | `MODE` | 3-bit operating mode (0..7) |
| 8..15 | `FIFO_LVL` | 8-bit FIFO fill level |
| others | reserved | must be preserved on write |

Write five small helpers:

- `get_field(reg, pos, width)` — return bits `[pos, pos+width)` shifted down to bit 0.
- `set_field(reg, pos, width, val)` — return `reg` with those bits replaced by `val`. `val` is masked to `width` bits; every other bit is unchanged.
- `popcount32(x)` — number of 1 bits (LeetCode 191). Used to count active faults.
- `reverse_bits32(x)` — mirror bit 31..0 (LeetCode 190). The sensor's SPI shifts LSB-first, the controller MSB-first.
- `is_pow2(x)` — true if exactly one bit is set (LeetCode 231). Used to check that a FIFO depth is a power of two.

Preconditions: `1 <= width <= 32`, `pos + width <= 32`. Every shift must be free of undefined behavior.

```c
uint32_t get_field(uint32_t reg, unsigned pos, unsigned width);
uint32_t set_field(uint32_t reg, unsigned pos, unsigned width, uint32_t val);
unsigned popcount32(uint32_t x);
uint32_t reverse_bits32(uint32_t x);
bool     is_pow2(uint32_t x);
```

### Example

```
STATUS = 0x00002A35
get_field(STATUS, 4, 3)            = 3          (MODE)
get_field(STATUS, 8, 8)            = 42         (FIFO_LVL = 0x2A)
popcount32(STATUS & 0x7)           = 2          (OT_FAULT + CRC_ERR)
set_field(STATUS, 4, 3, 6)         = 0x00002A65
set_field(STATUS, 4, 3, 9)         = 0x00002A15 (9 = 0b1001, only 0b001 fits)
reverse_bits32(0x02941E9C)         = 0x39782940
is_pow2(0) = false, is_pow2(1) = true, is_pow2(6) = false
```

### Constraints

- `width` may be 32 (whole register) and `pos` may be 31 (top bit).
- No compiler builtins for the core answer (you may mention `__builtin_popcount` / `RBIT` afterwards).

### Clarifying questions to ask (first 3 minutes)

1. If `val` does not fit in `width` bits, should I mask it silently, assert, or return an error?
2. Can `width` be 32? Can it be 0?
3. Is this a read-modify-write on the real register? Are any bits write-1-to-clear?
4. Should `is_pow2(0)` be false?

## 2. Your workspace

빈 에디터처럼 아래 `stub.c`를 복사해서 시작하거나, 로컬에서 `./run.sh T2-6`로 바로 테스트하세요. 테스트 묶음 6개(체크 17개)가 들어 있습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 마스크 하나로 get과 set 모두</summary>

`width`비트짜리 1의 묶음 `mask = (1u << width) - 1u`를 만듭니다. get은 `(reg >> pos) & mask`, set은 `mask << pos`로 자리를 옮긴 뒤 "지우고(`& ~m`) 넣기(`| (val << pos) & m`)"입니다.

</details>

<details>
<summary>Hint 2 — width = 32 함정</summary>

`1u << 32`는 C에서 **undefined behavior**입니다(시프트 양 ≥ 타입 폭). x86에서는 시프트 양이 5비트로 잘려 `1u << 0 = 1`이 되므로 mask가 0이 되어 버립니다. `width == 32`면 `0xFFFFFFFF`를 바로 돌려주는 분기를 두세요.

</details>

<details>
<summary>Hint 3 — popcount, power of two, reverse</summary>

- `x & (x - 1)`은 가장 낮은 1비트 하나를 지웁니다. 0이 될 때까지 반복한 횟수 = popcount.
- 같은 식이 한 번에 0이 되면 1비트가 정확히 하나 → 2의 거듭제곱(단 `x != 0`).
- reverse는 16 → 8 → 4 → 2 → 1비트 단위로 이웃 블록을 맞바꾸면 5단계로 끝납니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 그림으로 보기

예제 레지스터 `0x00002A35`를 비트로 펼치면 필드가 바로 보입니다. 모든 필드 연산은 "**mask를 만들고 → pos만큼 옮기고 → AND/OR**"의 반복입니다.

<div class="chart" data-src="chart1.svg" data-caption="STATUS 0x2A35의 비트 배치. MODE(4..6)=3, FIFO_LVL(8..15)=42, fault 3비트 중 2개 켜짐. 아래는 MODE를 읽고 쓰는 마스크 계산"></div>

set의 핵심은 순서입니다. **먼저 필드 자리를 0으로 지우고**(`reg & ~m`), **잘라 낸 새 값을 OR**(`(val << pos) & m`)합니다. 지우지 않고 OR만 하면 기존 1비트가 남고, 새 값을 `& m`으로 자르지 않으면 넘친 비트가 옆 필드(여기서는 bit 7, FIFO_LVL)를 망가뜨립니다.

### 4.2 reverse bits — 단계별 맞바꾸기

비트를 하나씩 32번 옮겨도 정답이지만(O(32)), 면접에서는 **분할 정복 스왑**을 보여 주면 좋습니다. 큰 블록부터 맞바꾸고 블록 크기를 반으로 줄입니다.

<div class="chart" data-src="chart2.svg" data-caption="왼쪽: 8비트로 축소한 원리(abcdefgh → hgfedcba). 오른쪽: 실제 32비트 입력 0x02941E9C가 단계마다 바뀌는 값"></div>

### 4.3 손으로 따라가기 (trace)

**popcount32(0x2A35)** — Kernighan 방식. 한 바퀴에 가장 낮은 1비트가 하나씩 사라집니다.

| 반복 | x (before) | x − 1 | x & (x − 1) | c |
|---|---|---|---|---|
| 1 | 0x2A35 | 0x2A34 | 0x2A34 | 1 |
| 2 | 0x2A34 | 0x2A33 | 0x2A30 | 2 |
| 3 | 0x2A30 | 0x2A2F | 0x2A20 | 3 |
| 4 | 0x2A20 | 0x2A1F | 0x2A00 | 4 |
| 5 | 0x2A00 | 0x29FF | 0x2800 | 5 |
| 6 | 0x2800 | 0x27FF | 0x2000 | 6 |
| 7 | 0x2000 | 0x1FFF | 0x0000 | **7** |

**set_field(0x2A35, 4, 3, 9)** — 넘치는 값

| 단계 | 식 | 값 |
|---|---|---|
| mask | `field_mask(3) << 4` | `0x70` |
| 지우기 | `0x2A35 & ~0x70` | `0x2A05` |
| 새 값 | `(9 << 4) & 0x70` = `0x90 & 0x70` | `0x10` |
| 결과 | `0x2A05` OR `0x10` | `0x2A15` (MODE = 1) |

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- 모든 상수에 `u` 접미사를 붙였습니다. `1 << 31`은 `int` 오버플로라 UB이고, `1u << 31`은 정의됩니다.
- `get_field`의 `reg >> pos`는 `pos <= 31`이 보장되므로 안전합니다. 위험한 시프트는 `field_mask` 한곳에만 모아 두고 분기로 막았습니다.
- `popcount32`는 1비트 개수만큼만 돕니다. 상수 시간이 필요하면 SWAR(`x - ((x >> 1) & 0x55555555)` …) 버전을 말로 덧붙이면 됩니다.

### 4.5 복잡도

- `get_field` / `set_field` / `is_pow2` / `reverse_bits32`: **O(1)**, 분기 거의 없음.
- `popcount32`: **O(켜진 비트 수)** ≤ 32회. 추가 메모리 O(1).

---

## 5. 임베디드 관점으로 말할 것

- **Read-modify-write는 원자적이지 않습니다.** 실제 레지스터에 `set_field`를 쓰면 읽기와 쓰기 사이에 ISR이 같은 레지스터를 건드릴 수 있습니다. 인터럽트를 잠깐 막거나, 하드웨어의 SET/CLR 별도 레지스터나 bit-banding을 씁니다.
- **W1C(write-1-to-clear) 비트 주의**: fault 플래그가 W1C라면 RMW로 읽은 값을 그대로 다시 쓰는 순간 켜져 있던 fault가 모두 지워집니다. 이런 레지스터는 지울 비트만 1로 쓴 값을 직접 씁니다.
- **`volatile`과 고정 폭 타입**: MMIO는 `volatile uint32_t *`로 접근하고, 비트필드 구조체(`struct { unsigned mode:3; }`)는 컴파일러마다 배치가 달라서 레지스터 맵에는 쓰지 않는다고 말하면 좋습니다.
- **하드웨어 명령**: Cortex-M3 이상은 `RBIT`, `CLZ`가 한 명령이고, M0에는 popcount 명령이 없어서 테이블(4비트 16칸)이나 SWAR를 씁니다.

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Make popcount constant-time without a loop. | SWAR: `x -= (x >> 1) & 0x55555555; x = (x & 0x33333333) + ((x >> 2) & 0x33333333); x = (x + (x >> 4)) & 0x0F0F0F0F; return (x * 0x01010101) >> 24;` 분기 없이 12개 정도 연산입니다. |
| How would you find the index of the lowest set fault bit? | `x & -x`(= `x & (~x + 1u)`)로 가장 낮은 1비트만 남기고, `__builtin_ctz` 또는 `CLZ(RBIT(x))`로 위치를 구합니다. `x == 0`은 따로 처리합니다. |
| What if `val` is larger than the field? | 지금은 조용히 잘라 냅니다. 안전이 중요한 코드라면 `val > field_mask(width)`일 때 에러를 반환하거나 assert하는 편이 버그를 빨리 찾습니다. 질문 1번으로 미리 합의합니다. |
| Write a register field from an ISR and the main loop. | RMW 구간을 critical section(`__disable_irq()` / 복원)으로 감싸거나, LDREX/STREX 재시도 루프, 또는 하드웨어 SET/CLR 레지스터를 씁니다. |
| Reverse bits called millions of times? | 8비트 256칸 테이블로 바이트 4개를 뒤집어 조립하거나, ARM이면 `__RBIT` 한 명령입니다. |
| Round a FIFO size up to the next power of two. | `x - 1`의 가장 높은 1비트 아래를 전부 1로 채운 뒤(오른쪽 시프트 1·2·4·8·16을 OR) 1을 더합니다. `x == 0`과 `x > 0x80000000`은 별도 처리. |

## 7. 자주 하는 실수

- `(1u << width) - 1`을 `width == 32`에서 그대로 계산 → UB, 테스트 3번 실패(x86에서 mask가 0)
- `1 << 31`처럼 부호 있는 `int`로 시프트 → UB(UBSan이 잡음)
- set에서 **지우기를 빼먹고 OR만** 함 → 기존 MODE 비트가 남아 테스트 1번 실패
- set에서 `val`을 **mask로 자르지 않음** → bit 7 침범, 테스트 2번 실패
- `is_pow2(0)`을 true로 반환 → `0 & (0 - 1) == 0`이므로 `x != 0` 조건이 꼭 필요
- 관련 문제: [T1-5](../T1-5_raw_register_to_temp/index.html)(raw 레지스터 → 온도), [T2-9](../T2-9_memory_pool/index.html)(비트맵으로 블록 사용 여부 추적)
