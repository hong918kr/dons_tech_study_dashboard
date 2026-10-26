# V2 · Register Access Layer + Register-Sanity Test

> **ID**: V2 · **Priority**: P0 · **Pattern**: bitfield mask + RMW + table-driven HW test · **Time**: 25 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh V2` / `./run.sh V2 sol`

## 1. Problem

A new IP block (`SIF`, a sensor-interface block) has just come up on the FPGA prototype. Before anyone writes a driver, you write the **register access layer** and a **register-sanity test** that checks the RTL against the register spec.

The block is reached only through 32-bit MMIO accessors `reg_read(off)` / `reg_write(off, val)`. A HW model in the file emulates it: RW bits, RO bits, **W1C** (write-1-to-clear) status bits set by hardware, a **self-clearing** `SOFT_RST` bit (writing 1 resets the whole block), reserved bits that always read 0, and a `FIFO` register whose read pops data. The model can also inject HW bugs (stuck bits, wrong reset value, writable RO bit).

Implement:

1. `field_mask / field_get / field_prep` for a field `msb..lsb`. `field_mask(31, 0)` must return `0xFFFFFFFF` with **no undefined shift**.
2. `field_write(rd, msb, lsb, val)` — read-modify-write one field.
   - `-EINVAL` if `msb > 31` or `lsb > msb`; `-ERANGE` if `val` does not fit. On error, no bus access.
   - Bits outside the field are written back as read, **except W1C and self-clearing bits, which must be written as 0**.
3. `reg_sanity_test(tbl, n, log, log_cap)` — walk the description table (skip `REG_F_SKIP` entries). Per register:
   1. **RESET**: read; must equal `reset`. Call the value read `orig`.
   2. **RO**: write `orig` on RW bits and `~orig` on RO bits (not RW, not W1C, not SC — includes reserved); W1C/SC bits as 0. Read-back must equal `orig`.
   3. **RW** (if `rw_mask != 0`): patterns `0x55555555`, `0xAAAAAAAA`, then walking-1 over every RW bit from bit 0 up. Write `(p & rw) | (orig & RO bits)`; read-back must equal `(p & rw) | (orig & ~rw)`.
   4. **Restore**: write `orig` with W1C/SC bits cleared.

   Each failed check is one mismatch: `MM_RESET` for step 1, otherwise `MM_RW` if the difference touches an RW bit, else `MM_RO`. Log the first `log_cap` mismatches (`log` may be `NULL` when `log_cap == 0`) and **return the total count**.

```c
uint32_t reg_read(uint32_t off);              /* provided: 32-bit MMIO */
void     reg_write(uint32_t off, uint32_t val);

typedef struct {
    const char *name;
    uint32_t off, reset, rw_mask, w1c_mask, sc_mask, flags;   /* flags: REG_F_SKIP */
} RegDesc;
typedef enum { MM_RESET = 1, MM_RO = 2, MM_RW = 3 } MmKind;
typedef struct { uint32_t off; MmKind kind; uint32_t wrote, expect, got; } Mismatch;

uint32_t field_mask(unsigned msb, unsigned lsb);
uint32_t field_get(uint32_t reg, unsigned msb, unsigned lsb);
uint32_t field_prep(uint32_t val, unsigned msb, unsigned lsb);
int      field_write(const RegDesc *rd, unsigned msb, unsigned lsb, uint32_t val);
size_t   reg_sanity_test(const RegDesc *tbl, size_t n, Mismatch *log, size_t log_cap);
```

Register map of `SIF`:

| off | name | reset | rw_mask | w1c_mask | sc_mask | notes |
|---|---|---|---|---|---|---|
| 0x00 | ID | 0x5A1D0102 | 0 | 0 | 0 | RO |
| 0x04 | CTRL | 0x00000400 | 0x0000FF0F | 0 | 0x80000000 | [0] EN, [3:1] MODE, [15:8] DIV, [31] SOFT_RST |
| 0x08 | IRQ | 0x00000000 | 0x00070000 | 0x7 | 0 | [2:0] pending (W1C), [8] BUSY (RO), [18:16] IRQ_EN |
| 0x0C | TIMEOUT | 0x0000FFFF | 0x00FFFFFF | 0 | 0 | |
| 0x10 | THRESH | 0x08000100 | 0x0FFF0FFF | 0 | 0 | [11:0] LO, [27:16] HI |
| 0x14 | SCRATCH | 0x00000000 | 0xFFFFFFFF | 0 | 0 | |
| 0x18 | FIFO | – | – | – | – | read pops data → `REG_F_SKIP` |

### Example

```
IRQ = 0x00000007 (HW latched 3 pending interrupts), field_write(IRQ, 18, 16, 5)
  naive RMW writes 0x00050007 -> W1C clears pending -> IRQ = 0x00050000   (wrong)
  field_write writes 0x00050000                    -> IRQ = 0x00050007   (right)

HW bugs injected: SCRATCH bit 13 stuck-at-0, THRESH reset reads 0x08000180
  reg_sanity_test(..., log, 2) = 3
  log[0] = { 0x10, MM_RESET, wrote 0,          expect 0x08000100, got 0x08000180 }
  log[1] = { 0x14, MM_RW,    wrote 0xAAAAAAAA, expect 0xAAAAAAAA, got 0xAAAA8AAA }
  (3rd mismatch, walking-1 bit 13, is counted but not logged)
```

### Constraints

- No `malloc`. 32-bit registers, word-aligned offsets. Must be clean under `-fsanitize=undefined`.
- The test must not write 1 to `SOFT_RST`, must not read `FIFO`, and must leave every register as it found it.

### Clarifying questions to ask (first 3 minutes)

1. Is the description table the source of truth (generated from IP-XACT / SystemRDL), or do I hard-code the map?
2. Which bits have side effects on write (self-clearing, W1C, "go" bits) or on read (read-to-clear, FIFO pop)?
3. Can I assume the block is just out of reset when the test starts (no HW status already latched)?
4. Is writing 1s into reserved bits allowed on this IP, or must reserved bits always be written as 0?
5. On mismatch, should I stop or keep going and report everything? How big is the log?

## 2. Your workspace

`./run.sh V2`로 바로 테스트하세요. 테스트 7개(field 헬퍼, 정상 RMW, W1C 함정, 잘못된 인자, 정상 HW, 버그 주입, RO 누수)가 들어 있습니다. 파일 아래쪽의 HW model은 수정하지 마세요.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — width 32에서 UB 없는 mask</summary>

`((1u << (msb - lsb + 1)) - 1) << lsb`는 `(31, 0)`에서 `1u << 32`가 되어 **UB**입니다(UBSan이 잡음, x86에서는 실제로 `1u << 0`처럼 동작해서 mask가 0이 됨). 시프트 양이 항상 31 이하가 되도록 두 번 나눠 자르세요: `(~0u >> (31 - msb)) & (~0u << lsb)`.

</details>

<details>
<summary>Hint 2 — field_write에서 지워야 할 비트</summary>

읽은 값 `cur`를 그대로 다시 쓰면 W1C 비트에 읽힌 1이 그대로 써져서 **pending 상태가 지워집니다**. `keep = cur & ~field_mask & ~(w1c_mask | sc_mask)`. 그리고 검증(`-EINVAL`, `-ERANGE`)은 **읽기 전에** 하세요. 잘못된 인자로 버스를 건드리면 안 됩니다.

</details>

<details>
<summary>Hint 3 — sanity test는 "write → read-back → 비교" 하나로</summary>

`wr_check(rd, written, expected)` 헬퍼 하나를 만들면 RO 단계와 RW 단계가 같은 코드가 됩니다. 차이 `diff = got ^ expected`가 RW 비트에 걸리면 `MM_RW`, 아니면 `MM_RO`. 로그는 T1-1과 같은 `if (cnt < cap) log[cnt] = …; cnt++;` 패턴입니다. RO 비트 집합은 `~(rw | w1c | sc)` — W1C에 1을 쓰면 상태가 지워지고, SC에 1을 쓰면 블록이 리셋되기 때문에 빼야 합니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 비트마다 "속성"이 다르다

레지스터는 32비트 정수 하나지만, 비트마다 쓰기 규칙이 다릅니다. 같은 값을 써도 RW 비트는 저장되고, RO·reserved는 무시되고, W1C는 **1을 쓰면 지워지고**, SC는 **1을 쓰면 동작(리셋)이 일어납니다**. 이 문제의 모든 함정은 "읽은 값을 그대로 다시 쓰면 안전하다"는 착각에서 나옵니다.

<div class="chart" data-src="chart1.svg" data-caption="CTRL·IRQ 비트 배치. 초록 RW, 보라 RO, 빨강 W1C, 주황 self-clearing, 흰색 reserved(항상 0으로 읽힘). 한 레지스터 안에 속성이 섞여 있는 게 보통입니다"></div>

### 4.2 RMW 함정 — W1C 비트

<div class="chart" data-src="chart2.svg" data-caption="IRQ_EN만 바꾸려 했는데 naive RMW는 읽은 pending 1을 다시 써서 인터럽트 3개를 지웁니다(0x00050000). field_write는 W1C 비트를 0으로 써서 0x00050007을 유지합니다 (테스트 3번)"></div>

반대로 pending을 **일부러** 지울 때는 그 비트만 1로 씁니다. `field_write(IRQ, 1, 1, 1)`은 쓰기 값 `0x00050002` → 결과 `0x00050005`(bit 1만 클리어). 이게 W1C가 존재하는 이유입니다: 여러 status 비트 중 **처리한 것만** 원자적으로 지울 수 있습니다.

### 4.3 손으로 따라가기 (trace)

**field_mask** — 두 번의 시프트가 모두 0..31 범위입니다.

| msb, lsb | `~0u >> (31-msb)` | `~0u << lsb` | AND |
|---|---|---|---|
| 15, 8 | 0x0000FFFF | 0xFFFFFF00 | **0x0000FF00** |
| 31, 0 | 0xFFFFFFFF (>>0) | 0xFFFFFFFF (<<0) | **0xFFFFFFFF** |
| 31, 31 | 0xFFFFFFFF | 0x80000000 | **0x80000000** |
| 0, 0 | 0x00000001 | 0xFFFFFFFF | **0x00000001** |

**field_write on CTRL** (테스트 2번, 리셋 값 `0x00000400`):

| 호출 | read | keep (field·SC 제거) | prep | write = 결과 |
|---|---|---|---|---|
| EN (0,0) = 1 | 0x00000400 | 0x00000400 | 0x00000001 | 0x00000401 |
| MODE (3,1) = 5 | 0x00000401 | 0x00000401 | 0x0000000A | 0x0000040B |
| DIV (15,8) = 0x10 | 0x0000040B | 0x0000000B | 0x00001000 | **0x0000100B** |

**reg_sanity_test의 RO 단계 값** (정상 HW):

| reg | orig | RO bits (not rw, w1c, sc) | 쓰는 값 | 기대 read-back |
|---|---|---|---|---|
| ID | 0x5A1D0102 | 0xFFFFFFFF | 0xA5E2FEFD | 0x5A1D0102 |
| CTRL | 0x00000400 | 0x7FFF00F0 | 0x7FFF04F0 (bit 31은 0!) | 0x00000400 |
| IRQ | 0x00000000 | 0xFFF8FFF8 | 0xFFF8FFF8 (W1C는 0) | 0x00000000 |

**체크 횟수** (정상 HW에서 쓰기 총 117번, 비교도 117번):

| reg | RW 비트 수 | RESET | RO | 0x55/0xAA | walking-1 | restore 쓰기 |
|---|---|---|---|---|---|---|
| ID | 0 | 1 | 1 | – | – | 1 |
| CTRL | 12 | 1 | 1 | 2 | 12 | 1 |
| IRQ | 3 | 1 | 1 | 2 | 3 | 1 |
| TIMEOUT | 24 | 1 | 1 | 2 | 24 | 1 |
| THRESH | 24 | 1 | 1 | 2 | 24 | 1 |
| SCRATCH | 32 | 1 | 1 | 2 | 32 | 1 |
| FIFO | skip | – | – | – | – | – |

**버그 주입** (테스트 6번: SCRATCH bit 13 stuck-at-0, THRESH reset bit 7 틀림):

| # | reg | 단계 | 쓴 값 | 기대 | 읽은 값 | kind |
|---|---|---|---|---|---|---|
| 0 | THRESH | RESET | – | 0x08000100 | 0x08000180 | MM_RESET |
| 1 | SCRATCH | 0xAAAAAAAA | 0xAAAAAAAA | 0xAAAAAAAA | **0xAAAA8AAA** | MM_RW |
| 2 | SCRATCH | walk bit 13 | 0x00002000 | 0x00002000 | 0x00000000 | MM_RW |

- THRESH는 RESET 1건뿐입니다. 이후 단계의 기대값은 `reset`이 아니라 **실제로 읽은 `orig`** 기준이라 같은 버그가 연쇄로 세어지지 않습니다.
- `0x55555555`는 bit 13이 0이라 stuck-at-0을 못 잡습니다. 그래서 두 패턴 + walking-1을 같이 씁니다.
- 테스트 7번(ID bit 0이 쓰기 가능): RO 단계에서 `0xA5E2FEFD`를 쓰고 `0x5A1D0103`을 읽음 → `MM_RO` 1건. restore가 원래 값으로 되돌립니다.

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (25분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `field_write`는 검증 → 읽기 → `keep | prep` → 쓰기 순서입니다. `keep`에서 `w1c_mask | sc_mask`를 지우는 한 줄이 이 문제의 핵심입니다.
- `wr_check()` 하나로 RO 단계와 RW 단계를 처리합니다. 기대값 공식만 다릅니다: RO 단계는 `orig`, RW 단계는 `(p & rw) | (orig & ~rw)`.
- RW 패턴을 쓸 때 RO 비트에는 `orig`를 씁니다. 0을 쓰면 RO 버그(쓰기 누수)가 RW 단계에서도 중복으로 잡힙니다.
- `log_mm`은 cap과 관계없이 카운터를 올립니다 → 반환값은 항상 전체 개수(T1-1과 같은 규칙).
- 정답은 카운터를 파일 static(`g_nmm`)으로 두었지만, 실제 코드라면 컨텍스트 구조체로 넘기는 게 재진입에 안전합니다.

### 4.5 복잡도

- 시간: 레지스터당 `3 + 2 + popcount(rw)` 번 MMIO 쓰기/읽기 → O(레지스터 수 × 32). SIF 전체에서 쓰기 117번.
- 메모리: O(1) + 호출자 로그 버퍼.

---

## 5. 임베디드 관점으로 말할 것

- **`volatile`과 접근 폭**: 실제 `reg_read`는 `*(volatile uint32_t *)(BASE + off)`입니다. `volatile`이 없으면 컴파일러가 read-back을 지우거나 합칩니다. 또 32비트 레지스터는 반드시 32비트 load/store로 접근해야 합니다(일부 IP는 byte 접근에 bus error 또는 엉뚱한 lane 쓰기).
- **posted write와 read-back**: AXI/APB 브리지는 쓰기를 버퍼링(posted)할 수 있습니다. "쓰기 → 바로 다른 장치 동작" 순서가 중요하면 같은 장치에서 **한 번 읽어서 flush**하거나 `dsb`/`wmb()`를 씁니다. 이 테스트의 write → read-back 쌍은 자연스럽게 순서를 보장합니다(같은 장치, 같은 주소).
- **테이블 기반 테스트**: 레지스터 맵은 사람이 손으로 옮기면 틀립니다. IP-XACT/SystemRDL에서 `RegDesc` 테이블을 **생성**하면 RTL과 SW가 같은 소스를 봅니다. 이 테스트는 블록마다 처음 켜는 날 가장 먼저 돌리는 "smoke test"입니다.
- **RW 비트도 부작용이 있다**: walking-1 중에 `CTRL.EN = 1`이 써지면 실제 블록은 동작을 시작할 수 있습니다. 실무 테이블에는 "test에서 쓰지 말 것" 비트 마스크를 따로 둡니다(이 문제의 `sc_mask`를 일반화한 것).
- 관련 문제: 메모리 쪽 같은 아이디어(패턴 + walking bit)는 [V1](../V1_march_memtest/index.html), 결과를 보고서로 묶는 것은 [V5](../V5_test_runner_coverage/index.html).

## 5.1 면접관(HW 출신 Sr Manager)이 파고들 포인트

- **"FPGA와 first silicon에서 뭐가 달라지나?"** — FPGA에서는 레지스터가 대부분 맞지만 클록/리셋 도메인이 다릅니다. Silicon에서는 블록 클록·리셋·power domain을 먼저 켜야 하고, 안 켜면 read가 bus error나 0xDEADBEEF로 옵니다. 그래서 첫 체크는 ID 레지스터입니다.
  *"On first silicon I read the ID register of every block first — that proves clock, reset, power and the bus path before I trust any other result."*
- **"테스트 자체가 맞는지 어떻게 아나?"** — 일부러 버그를 넣은 모델(stuck bit, 틀린 reset, RO 누수)로 **테스트를 테스트**합니다. 잡아야 할 버그를 못 잡으면 테스트가 틀린 것입니다(테스트 6, 7번).
  *"I validate the test against a fault-injected model: if it can't catch a stuck bit I planted, it won't catch a real one."*
- **"레지스터가 수천 개면?"** — 테이블은 SystemRDL/IP-XACT에서 자동 생성, side-effect 레지스터는 속성으로 자동 skip, 결과는 블록별 pass/fail + 커버리지(검사한 비트 / 전체 RW 비트)로 리포트합니다.
  *"The map is generated from the same IP-XACT the RTL team uses, so coverage is measurable: bits checked versus bits defined."*
- **"Reset 값이 틀리면 RTL 버그인가 스펙 버그인가?"** — 둘 다 가능합니다. 실패 로그에 offset, 기대값, 실제값을 남겨서 RTL/아키텍처 팀과 바로 대조합니다. Silicon에서는 OTP/fuse가 reset 값을 덮는 경우도 확인합니다.
  *"A reset mismatch goes to triage with expected, actual, and the spec revision — sometimes the RTL is right and the document is stale."*

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Why `volatile`, and is it enough for MMIO ordering? | `volatile`은 컴파일러가 접근을 없애거나 순서를 바꾸지 못하게 할 뿐, CPU/버스의 재배열이나 posted write는 막지 못합니다. ARM에서는 Device 메모리 속성 + 필요 시 `dsb`, Linux에서는 `writel()`(barrier 포함) / `writel_relaxed()`를 구분해서 씁니다. |
| Why can a posted write bite you, and how do you flush it? | 쓰기가 인터커넥트 버퍼에 머문 상태에서 CPU가 다음 일을 진행합니다. 예: 인터럽트 클리어 후 바로 리턴하면 같은 IRQ가 다시 들어옵니다. 같은 장치 레지스터를 한 번 **읽으면** 앞선 쓰기가 끝났음이 보장됩니다. |
| Why is read-modify-write racy? | (1) HW가 읽기와 쓰기 사이에 status 비트를 바꾸면 그 변화가 덮입니다 → W1C/SET/CLR 레지스터가 존재하는 이유. (2) 두 코어/ISR이 같은 레지스터를 RMW하면 한쪽 변경이 사라집니다 → spinlock + IRQ off, 또는 SET/CLR alias 레지스터. |
| How would you generate these tests from IP-XACT / SystemRDL? | `peakrdl` 같은 도구로 RDL을 C 헤더 + `RegDesc` 테이블로 변환합니다. 속성(`sw=r/rw`, `onwrite=woclr`, `singlepulse`, `rclr`)이 그대로 `rw/w1c/sc/skip` 마스크가 됩니다. UVM `uvm_reg_hw_reset_seq` / `uvm_reg_bit_bash_seq`와 같은 아이디어를 bare-metal C로 옮긴 것입니다. |
| Which registers must a generic test skip? | 읽기 부작용(read-to-clear status, FIFO pop, 카운터 latch), 쓰기 부작용(doorbell, "go"/start 비트, soft reset, PLL/clock 설정 — 잘못 쓰면 버스가 죽음), 보안/lock 비트(한 번 쓰면 리셋 전까지 고정). |
| Bitfield structs vs masks? | `struct { uint32_t en:1; ... }`는 비트 순서·패딩·접근 폭이 **구현 정의**라 컴파일러마다 다르고, 필드 하나 쓰는데 byte store가 나올 수 있습니다. MMIO에는 mask/shift(Linux `FIELD_GET/FIELD_PREP`, `GENMASK`)가 이식성과 접근 폭 모두 안전합니다. |
| How do you test W1C and self-clearing bits then? | 일반 bit-bash가 아니라 블록별 directed test로 합니다: HW 이벤트를 만들어 status를 세우고 → 1을 써서 지워지는지, 0을 써서 유지되는지 확인. SC 비트는 1을 쓴 뒤 0으로 돌아오는지 + 동작이 일어났는지(레지스터가 reset 값으로 돌아왔는지) 확인합니다. |

## 7. 자주 하는 실수

- `((1u << width) - 1) << lsb` → width 32에서 UB (테스트 1번, UBSan이 잡음)
- RMW에서 W1C 비트를 마스크하지 않음 → pending 인터럽트 소실 (테스트 3번)
- 인자 검증 전에 레지스터를 먼저 읽음/씀 → 잘못된 호출이 버스 접근을 만듦 (테스트 4번 `H.writes == 0`)
- RO 단계에서 `~orig`를 SC 비트에도 씀 → `SOFT_RST`가 블록을 리셋, 테스트가 자기 자신을 망침 (테스트 5번)
- FIFO 같은 side-effect 레지스터를 "reset 값 확인"한다고 읽음 (테스트 5번 `fifo_pops == 0`)
- 기대값을 `reset` 기준으로 계산 → reset 버그 하나가 수십 건으로 번짐 (테스트 6번은 정확히 3건이어야 함)
- 로그가 꽉 차면 카운트도 멈춤 → 전체 개수 반환 규칙 위반 (테스트 6번)
- 테스트 후 원래 값 복원을 잊음 → 다음 테스트가 오염됨
