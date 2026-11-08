---
id: C-M02
time: 80분
updated: 2026-11-07
---
# C-M02 Read-Modify-Write 위험과 W1C 레지스터

## 학습 목표

- 레지스터 비트의 **쓰기 의미(write semantics)** — RW, RO, W1C, W0C(STM32 `rc_w0`), W1S, RC — 를 하나의 모델 함수 `apply_write()` 로 정확히 구현하고 표로 검증한다.
- W1C 상태 레지스터에서 `SR |= FLAG` 가 **다른 pending 플래그를 전부 지우는** 이유를 버스 접근 기록(trace)으로 재현하고, "지울 비트만 쓴다" 로 고친다.
- STM32 `TIMx_SR` 는 반대 관습(0을 써야 지워짐)이라 `SR &= ~UIF` 가 **하드웨어와 경쟁** 한다는 것, 그래서 `SR = ~UIF` 가 정답인 이유를 설명한다.
- RW 비트와 W1C 비트가 섞인 레지스터에서 안전한 RMW 공식을 쓰고, main 의 RMW 가 ISR 의 변경을 되돌리는 **소프트웨어 경쟁** 을 set/clear alias 레지스터와 임계 구역으로 막는다.
- 실제 칩·규격(STM32, nRF52, Cortex-M NVIC, GIC, PCIe, NVMe, AHCI, RP2040)이 각각 어떤 관습을 쓰는지 말할 수 있다.

## 1. 문제

"인터럽트 플래그를 지웠는데 다른 인터럽트가 사라졌어요" — 펌웨어 버그 리포트의 단골이고, 면접에서는 "`SR |= FLAG` 에 뭐가 문제죠?" 한 줄로 나온다. **답을 아는 사람과 모르는 사람이 확실히 갈리는 문제** 다.

C-B01 에서 본 것처럼 `REG |= m` 은 CPU 에서 **읽기 → 수정 → 쓰기(Read-Modify-Write, RMW)** 3단계다. 일반 메모리라면 "읽은 값을 다시 쓰면 아무 일도 없다" 가 성립한다. 그런데 레지스터는 비트마다 **쓰기가 뜻하는 바가 다르다**. 상태 레지스터의 W1C(write-1-to-clear) 비트는 "1을 쓰면 지워라" 라서, 읽어 온 1을 그대로 다시 쓰는 순간 그 플래그가 지워진다. 즉 레지스터에서는 `x = x` 조차 부작용이 있다.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">같은 쓰기, 다른 결과 — 비트 하나의 저장값 (강조 = 쓰기로 값이 바뀐 칸)</text>
  <g fill="currentColor" font-weight="bold">
    <text x="12" y="56">의미</text><text x="122" y="48">1 일 때</text><text x="122" y="64">0 쓰기</text>
    <text x="212" y="48">1 일 때</text><text x="212" y="64">1 쓰기</text><text x="302" y="48">0 일 때</text><text x="302" y="64">1 쓰기</text>
    <text x="392" y="56">읽기</text><text x="470" y="56">실제 예</text>
  </g>
  <g stroke="currentColor" fill="none" opacity=".35">
    <line x1="10" y1="72" x2="730" y2="72"/><line x1="10" y1="104" x2="730" y2="104"/><line x1="10" y1="136" x2="730" y2="136"/>
    <line x1="10" y1="168" x2="730" y2="168"/><line x1="10" y1="200" x2="730" y2="200"/><line x1="10" y1="232" x2="730" y2="232"/>
    <line x1="10" y1="264" x2="730" y2="264"/>
  </g>
  <rect x="112" y="76" width="80" height="24" class="f-acc"/><rect x="292" y="76" width="80" height="24" class="f-acc"/>
  <rect x="202" y="140" width="80" height="24" class="f-acc"/>
  <rect x="112" y="172" width="80" height="24" class="f-acc"/>
  <rect x="292" y="204" width="80" height="24" class="f-acc"/>
  <rect x="382" y="236" width="80" height="24" class="f-acc"/>
  <g fill="currentColor">
    <text x="12" y="93" font-weight="bold">RW</text><text x="12" y="125" font-weight="bold">RO</text>
    <text x="12" y="157" font-weight="bold">W1C</text><text x="12" y="189" font-weight="bold">W0C (rc_w0)</text>
    <text x="12" y="221" font-weight="bold">W1S</text><text x="12" y="253" font-weight="bold">RC</text>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="152" y="93">0</text><text x="242" y="93">1</text><text x="332" y="93">1</text><text x="422" y="93">그대로</text>
    <text x="152" y="125">1</text><text x="242" y="125">1</text><text x="332" y="125">0</text><text x="422" y="125">그대로</text>
    <text x="152" y="157">1</text><text x="242" y="157">0</text><text x="332" y="157">0</text><text x="422" y="157">그대로</text>
    <text x="152" y="189">0</text><text x="242" y="189">1</text><text x="332" y="189">0</text><text x="422" y="189">그대로</text>
    <text x="152" y="221">1</text><text x="242" y="221">1</text><text x="332" y="221">1</text><text x="422" y="221">그대로</text>
    <text x="152" y="253">1</text><text x="242" y="253">1</text><text x="332" y="253">0</text><text x="422" y="253">→ 0</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="470" y="93">제어 레지스터 CR, ODR</text>
    <text x="470" y="125">BUSY, GPIO IDR, NVMe CSTS.RDY</text>
    <text x="470" y="157">EXTI_PR, PCIe Status, NVMe NSSRO</text>
    <text x="470" y="189">TIMx_SR, F4 USART_SR RXNE/TC</text>
    <text x="470" y="221">NVIC_ISER, nRF INTENSET, GPIO BSRR</text>
    <text x="470" y="253">읽으면 지워지는 이벤트 래치</text>
  </g>
  <text x="10" y="288" fill="currentColor">핵심: 상태 비트에는 항상 "써도 아무 일 없는 값" 이 있다 — W1C·W1S 는 0, W0C 는 1.</text>
  <text x="10" y="310" fill="currentColor">안전한 쓰기는 그 값을 기본으로 깔고, 바꿀 비트만 반대 값을 쓴다.</text>
</svg>
```

호스트(맥)에는 진짜 레지스터가 없으므로, 모든 접근을 `reg_read()` / `reg_write()` 로 보내고 이 두 함수가 비트별 규칙(`reg_spec`)을 적용하게 한다. 실제 칩에서 이 규칙을 적용하는 건 주변장치 하드웨어다. 하네스는 접근 기록을 `"rSR=107 wSR=107"` 같은 문자열로 남기고, **읽기 직후·쓰기 전** 에 한 번 실행되는 훅(`after_read`)으로 하드웨어 이벤트나 ISR 을 정확한 순간에 끼워 넣는다 — 경쟁(race)을 운에 맡기지 않고 결정적으로 재현하기 위해서다.

### Part A — 쓰기 의미 모델 (하드웨어 쪽)

`reg_spec` 은 다섯 마스크 `rw, w1c, w0c, w1s, rc` 를 가진다. 어느 마스크에도 없는 비트는 RO 다.

- `spec_valid(s)` — 다섯 마스크가 서로 겹치지 않으면 true.
- `apply_write(s, cur, w)` — 현재 저장값 `cur` 에 `w` 를 썼을 때 새 저장값. RW 는 교체, W1C 는 1 쓴 자리만 0, W0C 는 0 쓴 자리만 0, W1S 는 1 쓴 자리만 1, RO·RC 는 쓰기 무시.
- `apply_read(s, cur)` — 읽은 뒤 남는 값 (RC 비트는 0).

### Part B — 상태 레지스터를 올바르게 지우기

장치 SR: `RX`(bit0)·`TX`(bit1)·`ERR`(bit2) 는 W1C 이벤트, `BUSY`(bit8) 는 RO. 타이머 TSR: `UIF`·`CC1IF`·`CC2IF` 는 STM32 `TIMx_SR` 와 같은 **rc_w0**. 주어진 버그 코드 `sr_clear_buggy()` (= `SR |= flag`), `tim_clear_buggy()` (= `TSR &= ~flag`) 가 무엇을 망가뜨리는지 테스트가 재현한다.

- `sr_clear(flag)` — SR 에서 그 비트만 지운다.
- `tim_clear(flag)` — TSR 에서 그 비트만 지운다. 읽기와 쓰기 사이에 타이머가 다른 플래그를 세워도 잃지 않아야 한다.
- `sr_ack_seen()` — SR 을 **한 번** 읽고, 그때 본 이벤트만 정확히 ack 하고, 그 집합을 돌려준다. 이벤트가 없으면 쓰지 않는다.
- `reg_modify(r, clr, set)` — 어떤 spec 의 레지스터에서도 안전한 RMW. RW 비트만 바꾸고, 나머지 비트에는 "써도 무해한 값" 을 쓴다. RC 비트가 있는 레지스터나 RW 가 아닌 비트를 요청하면 **아무 접근 없이** false.

### Part C — 드라이버와 ISR

CR 은 RW 비트(`EN`, `RXIE`, `TXIE`, 2비트 `MODE`)와 W1C sticky 플래그 `OVF` 가 섞인 레지스터다. 같은 저장소를 공유하는 alias 레지스터 `CRSET`(W1S) / `CRCLR`(W1C) 가 있다 — RP2040 의 SET/CLR alias, nRF `INTENSET/INTENCLR`, NVIC `ISER/ICER` 와 같은 구조다.

- `dev_isr()` — SR 을 한 번 읽어 본 것만 ack, RX/ERR 개수를 세고, TX 완료면 `tx_left` 를 줄이고 보낼 게 없으면 `TXIE` 를 끈다.
- `dev_rxie(on)` — main 문맥에서 `RXIE` 를 켜고 끈다. 하네스는 **CR 을 읽는 순간** TX 완료 ISR 을 끼워 넣는다. 주어진 `dev_rxie_buggy()` 는 ISR 이 끈 `TXIE` 를 되살린다.
- `dev_set_mode(mode)` — 2비트 필드는 alias 한 번으로 바꿀 수 없다. 임계 구역 + `reg_modify` 로, `OVF` 를 지우지 않고.

## 2. Problem (English)

> **Register write semantics and read-modify-write hazards.**
>
> Each bit of a memory-mapped register has an access type: RW, RO, W1C (write 1 to clear), W0C (write 0 to clear — STM32 `rc_w0`), W1S (write 1 to set) or RC (cleared by a read). All accesses go through `uint32_t reg_read(sim_reg *r)` and `void reg_write(sim_reg *r, uint32_t v)`; a test hook can inject a hardware event or an interrupt between a read and the next write.
>
> 1. Implement `uint32_t apply_write(const reg_spec *s, uint32_t cur, uint32_t w)`, `uint32_t apply_read(const reg_spec *s, uint32_t cur)` and `bool spec_valid(const reg_spec *s)`.
> 2. `SR` holds W1C event flags `RX|TX|ERR` and an RO `BUSY` bit. Explain why `SR |= RX` clears every pending event, and write `sr_clear(flag)`. A timer status register `TSR` uses W0C flags; write `tim_clear(flag)` so that a flag raised by hardware between your read and your write is never lost. Write `uint32_t sr_ack_seen(void)` that acknowledges exactly the events it observed with one read. Write a generic `bool reg_modify(sim_reg *r, uint32_t clr, uint32_t set)` that is safe on any register without read side effects.
> 3. `CR` mixes RW control bits with a W1C `OVF` flag and has W1S/W1C aliases `CRSET`/`CRCLR`. Write the UART-style ISR and a main-context `dev_rxie(bool on)` that cannot undo a concurrent ISR update of `TXIE`. Change the 2-bit `MODE` field safely.
>
> Example: `SR = 0x107` (`RX|TX|ERR|BUSY`), `sr_clear(RX)` → trace `"wSR=1"`, `SR == 0x106`. With `TSR = UIF|CC2IF` and `CC1IF` raised right after the read, `TSR &= ~UIF` leaves `TSR == CC2IF` (CC1IF lost), whereas `TSR = ~UIF` leaves `CC1IF|CC2IF`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 이 쭉 나오고 "10/53 passed" — TODO 를 채워 53/53 을 만든다
```

> 맥 SDK 27.2에서 `ld: tapi error` 가 나면 `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` 후 다시 빌드.

시작 코드의 `sr_clear` / `tim_clear` / `dev_rxie` 는 일부러 **버그 코드를 그대로 호출** 하고 있다 — 흔한 코드가 왜 틀렸는지 FAIL 로 먼저 보라는 뜻이다. Part B·C 의 "재현" 테스트(`sr_clear_buggy` 가 모두 지우는지 등)는 Part A 의 `apply_write` 가 맞아야 통과한다. 하드웨어 모델이 틀리면 버그도 재현되지 않는다.

관찰용 파일 (하네스와 별개, `main` 없음):

```bash
clang -std=c11 --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -   # Cortex-M4 asm
```

## 4. 힌트

<details>
<summary>힌트 1 — apply_write 는 마스크별로 한 줄씩</summary>

`w & mask` 는 "1을 쓴 자리", `~w & mask` 는 "0을 쓴 자리" 다. RW 는 `(cur & ~rw) | (w & rw)`, W1C 는 `&= ~(w & w1c)`, W0C 는 `&= ~(~w & w0c)`, W1S 는 `|= (w & w1s)`. 마스크가 서로 겹치지 않으므로 순서는 상관없다 (그래서 `spec_valid` 가 필요하다).

</details>

<details>
<summary>힌트 2 — "지울 비트만" 의 뜻은 관습마다 다르다</summary>

W1C: 지울 비트만 **1**, 나머지 0 → `SR = flag`. W0C: 지울 비트만 **0**, 나머지 1 → `SR = ~flag`. 둘 다 **읽기가 없다**. 읽기가 없으면 "읽은 뒤 바뀐 값" 을 덮어쓸 일도 없다.

</details>

<details>
<summary>힌트 3 — reg_modify: 비트 종류별 "무해한 값"</summary>

읽은 값에서 RW 비트만 남기고(`v & rw`), 바꾸고(`& ~clr | set`), W0C 자리는 1 로 채운다(`| w0c`). W1C·W1S·RO 자리는 0 이 된다 — W1C/W1S 에 0 은 무해하고 RO 는 무시된다. RC 가 있으면 읽는 순간 이벤트가 사라지므로 아예 거부.

</details>

<details>
<summary>힌트 4 — Part C: 읽지 않으면 경쟁할 수 없다</summary>

`RXIE` 하나만 바꿀 땐 `CRSET`/`CRCLR` 에 한 번 쓰면 끝이다 (C-B01 의 BSRR 과 같은 원리). `MODE` 처럼 여러 비트 필드는 SET/CLR 두 번에 나누면 중간 상태(예: MODE=3)가 잠깐 생기므로, `irq_disable(); reg_modify(...); irq_enable();` 로 감싼다.

</details>

## 5. 풀이

### Part A — 비트 의미를 함수 하나로

```c
static uint32_t apply_write(const reg_spec *s, uint32_t cur, uint32_t w) {
    uint32_t v = cur;
    v = (v & ~s->rw) | (w & s->rw);   /* RW : 쓴 값으로 교체 */
    v &= ~(w & s->w1c);               /* W1C: 1 쓴 자리만 지움 */
    v &= ~(~w & s->w0c);              /* W0C: 0 쓴 자리만 지움 */
    v |= (w & s->w1s);                /* W1S: 1 쓴 자리만 켬 */
    return v;                         /* RO, RC: 그대로 */
}
```

테스트는 `MIX` 레지스터(바이트마다 의미가 다름: RW `0xFF`, W1C `0xFF00`, W0C `0xFF0000`, W1S `0x0F000000`, RC `0x30000000`, RO `0xC0000000`)에 8개 (cur, w) 쌍을 넣는다. 손으로 따라가 보면 규칙이 몸에 붙는다:

| cur | w | 결과 | 읽는 법 |
|---|---|---|---|
| `FFFFFFFF` | `00000000` | `FF00FF00` | RW→0, **W0C 전부 지워짐**, W1C·W1S·RC·RO 유지 |
| `FFFFFFFF` | `FFFFFFFF` | `FFFF00FF` | **W1C 전부 지워짐**, W0C 유지 |
| `00000000` | `FFFFFFFF` | `0F0000FF` | RW·W1S 만 켜짐. W0C/W1C 는 0 에 쓰기 → 그대로 0 |
| `00F0F000` | `00003000` | `0000C000` | W1C 두 비트만 지움, W0C 는 0 을 써서 전부 지움 |
| `C0000000` | `3F000000` | `CF000000` | RC·RO 는 쓰기로 안 바뀜, W1S 만 켜짐 |

표의 첫 두 줄이 이 레슨 전체의 요약이다: **"전부 1" 을 쓰면 W1C 가 다 지워지고, "전부 0" 을 쓰면 W0C 가 다 지워진다.** RMW 는 "읽은 값" 을 쓰는데, 읽은 값의 상태 비트는 1 과 0 이 섞여 있으니 두 관습 모두에서 뭔가를 지운다.

> 이 모델은 RTL 검증에서 쓰는 레지스터 모델과 같은 발상이다. IP-XACT 는 필드마다 `modifiedWriteValue`(oneToClear, zeroToClear, oneToSet …)와 `readAction`(clear …)을 적고, UVM `uvm_reg_field` 는 이를 `"W1C"`, `"W0C"`, `"W1S"`, `"RC"`, `"W1T"` 등 25가지 access policy 문자열로 표현한다. 펌웨어가 잘못 짚는 바로 그 지점을 검증이 bit-bash 시퀀스로 확인한다 (추가 연습 5).

### Part B — 버그 두 개, 관습 두 개

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">① W1C 상태 레지스터: SR |= RX  vs  SR = RX   (bit2 ERR, bit1 TX, bit0 RX)</text>
  <g fill="currentColor" font-family="monospace">
    <text x="20" y="54">SR       = 111   (RX, TX, ERR pending)</text>
    <text x="20" y="78">read     → 111</text>
    <text x="20" y="102">| RX     → 111   ← 쓸 값</text>
    <text x="20" y="126">write 111: 1 쓴 비트 전부 clear</text>
    <text x="400" y="54">SR       = 111</text>
    <text x="400" y="78">(읽기 없음)</text>
    <text x="400" y="102">write 001: bit0 만 1</text>
    <text x="400" y="126">0 쓴 bit2, bit1 → 무시</text>
  </g>
  <rect x="16" y="136" width="330" height="26" class="f-warn"/>
  <rect x="396" y="136" width="330" height="26" class="f-ok"/>
  <g fill="currentColor" font-family="monospace">
    <text x="24" y="154">SR = 000  — TX, ERR 유실</text>
    <text x="404" y="154">SR = 110  — TX, ERR 그대로</text>
  </g>
  <line x1="10" y1="178" x2="730" y2="178" stroke="currentColor" opacity=".35"/>
  <text x="10" y="202" fill="currentColor" font-weight="bold">② rc_w0 (STM32 TIMx_SR): SR &amp;= ~UIF  vs  SR = ~UIF   (bit 2·1·0 = CC2IF·CC1IF·UIF)</text>
  <g fill="currentColor" font-family="monospace">
    <text x="20" y="232">SR = 101 → read 101</text>
    <text x="20" y="256">HW: CC1IF 발생 → SR = 111</text>
    <text x="20" y="280">write 101 &amp; ~001 = 100</text>
    <text x="400" y="232">SR = 101, HW: CC1IF → 111</text>
    <text x="400" y="256">write ~001 = …11110</text>
    <text x="400" y="280">1 쓴 비트 무시, bit0 만 clear</text>
  </g>
  <rect x="16" y="290" width="330" height="26" class="f-warn"/>
  <rect x="396" y="290" width="330" height="26" class="f-ok"/>
  <g fill="currentColor" font-family="monospace">
    <text x="24" y="308">SR = 100  — CC1IF 유실 (0 을 씀)</text>
    <text x="404" y="308">SR = 110  — CC1IF 보존</text>
  </g>
</svg>
```

**① W1C 에서 `|=`.** `SR |= RX` 는 `SR = SR | RX`. 읽은 값 `0b111` 에 RX 를 OR 해도 `0b111` — 이걸 W1C 레지스터에 쓰면 1 인 비트 세 개가 **모두** 지워진다. 테스트의 실제 trace 는 `"rSR=107 wSR=107"` 이고 결과는 `SR == BUSY` (RO BUSY 비트는 쓰기 무시라 남는다). 고친 코드는 `reg_write(&SR, flag)`, trace `"wSR=1"` — 읽기 자체가 없다.

`SR &= ~RX` 도 틀렸다 — 그건 RX **를 빼고** 다른 pending 을 모두 1 로 써서, 정작 RX 는 남기고 나머지를 지운다. 정반대 버그다 (7. 자주 하는 실수).

**② rc_w0 에서 `&= ~`.** STM32 의 타이머 상태 레지스터는 반대 관습이다. RM0090/RM0440 의 TIMx_SR 비트 표기 `rc_w0` 은 "읽을 수 있고, **0 을 써서** 지운다. 1 쓰기는 무효" 라는 뜻이다. 그래서

- W1C 습관대로 `TIMx->SR = TIM_SR_UIF` 를 쓰면 → UIF 는 1 을 받아 **그대로 남고**, 0 을 받은 나머지 플래그가 **전부 지워진다**. 테스트: `TSR = UIF|CC2IF` 에 `UIF` 를 쓰면 `TSR == UIF`.
- `TIMx->SR &= ~TIM_SR_UIF` 는 "평소엔" 동작한다. 읽은 값에서 0 이었던 비트는 0 을 다시 써도 이미 0 이니까. 문제는 **읽기와 쓰기 사이에 하드웨어가 새 플래그를 세울 때** 다. 그 비트는 읽을 때 0 이었으므로 0 이 쓰여 지워진다. 하네스 trace `"rTSR=5 HW:TSR|=2 wTSR=4"` → `TSR == CC2IF`, CC1IF 유실.
- 정답 `TIMx->SR = ~TIM_SR_UIF` — 지울 비트만 0, 나머지는 무해한 1. ST HAL 의 `__HAL_TIM_CLEAR_FLAG(h, f)` 도 `((h)->Instance->SR = ~(f))` 로 정의되어 있다.

여기서 중요한 구분: **②의 경쟁 상대는 ISR 이 아니라 하드웨어** 다. 인터럽트를 꺼도, LDREX/STREX 를 써도 타이머 카운터가 플래그를 세우는 걸 막을 수 없다. 해결책은 오직 "읽지 않고, 무해한 값을 기본으로 깐 한 번의 쓰기" 뿐이다. 이게 하드웨어 설계자가 상태 비트를 RW 가 아닌 W1C/W0C 로 만드는 이유다 — RW 상태 비트였다면 소프트웨어가 무슨 짓을 해도 이 경쟁을 피할 수 없다.

**③ 본 것만 ack (`sr_ack_seen`).** "읽은 값을 그대로 다시 쓴다" 는 W1C 에서 사실 **올바른 관용구** 다. 읽는 순간 본 이벤트만 지우고, 읽은 뒤 도착한 이벤트는 쓴 값에서 0 이라 살아남는다. 하네스: SR=`RX|BUSY` 를 읽은 직후 TX 발생 → trace `"rSR=101 HW:SR|=2 wSR=1"`, 반환 `RX`, SR 에는 `TX|BUSY` 가 남는다. Linux `libahci` 의 포트 인터럽트 처리가 정확히 이 모양이다 (`status = readl(PORT_IRQ_STAT); writel(status, PORT_IRQ_STAT);`). 주의 두 가지:

1. RO 비트(BUSY)는 마스크로 걸러 반환한다 — 써도 무시되긴 하지만, "처리할 이벤트" 집합에 섞이면 안 된다.
2. `SR = SR` 과 `SR |= X` 의 차이는 **의도** 다. 전자는 "본 것 전부를 지금 처리한다" 일 때 맞고, 후자는 "X 만 처리한다" 고 생각하면서 실제로는 본 것 전부를 지운다.

**④ 혼합 레지스터의 RMW (`reg_modify`).** CR 처럼 RW 와 W1C 가 섞이면 평범한 `CR = (CR & ~TXIE) | RXIE` 가 읽어 온 `OVF=1` 을 다시 써서 오버플로 기록을 조용히 지운다 (테스트 재현: 결과 `EN|RXIE`, OVF 사라짐). NXP Kinetis `PORTx_PCRn` 이 실제 예다 — 핀 MUX·IRQC 설정(RW)과 핀 인터럽트 플래그 `ISF`(w1c)가 한 레지스터에 있어서, 인터럽트가 펜딩된 핀의 MUX 를 `|=` 로 바꾸면 그 인터럽트가 사라진다 (추가 연습 3). 일반 공식:

```text
v = read(r)
v = (v & rw & ~clr) | set      ← RW 만 남기고 수정. W1C·W1S·RO 자리는 0 (무해)
v |= w0c                       ← W0C 자리는 1 (무해)
write(r, v)
```

`MIX` 에서 RC 만 뺀 레지스터 M 을 `FFFFFFFF` 로 채우고 `reg_modify(&M, 0x0F, 0)` 하면 실제 쓰는 값은 `0x00FF00F0` 이고 결과는 `FFFFFFF0` — RW 하위 니블만 바뀌고 다른 모든 비트가 보존된다. RC 비트가 있는 레지스터는 **읽는 행위 자체가 이벤트를 지우므로** 이 공식이 성립하지 않는다 → false 를 돌려주고 접근하지 않는다(trace `""`). 그런 레지스터의 제어 비트는 소프트웨어 사본(shadow)으로 관리한다 (추가 연습 2 와 같은 방법).

> 이 공식은 **하드웨어 vs 소프트웨어** 경쟁(상태 비트)은 막지만, **소프트웨어 vs 소프트웨어** 경쟁(ISR 이 같은 RW 비트를 바꿈)은 막지 못한다. 읽기와 쓰기 사이에 ISR 이 RW 비트를 바꾸면 여전히 덮어쓴다. 그게 Part C 다.

### 실제 칩·규격은 어떤 관습을 쓰나

| 칩 / 규격 | 레지스터 | 의미 | 올바른 클리어 |
|---|---|---|---|
| STM32 (F4 등) | `EXTI_PR` (G4/L4 는 `EXTI_PR1`) | `rc_w1` = W1C | `EXTI->PR = 1u << n;` — `\|=` 는 다른 라인까지 지움 (RIOT OS 가 실제로 고친 버그) |
| STM32 | `TIMx_SR` | `rc_w0` = W0C | `TIMx->SR = ~TIM_SR_UIF;` (HAL `__HAL_TIM_CLEAR_FLAG`) |
| STM32F4 | `USART_SR` | RXNE·TC 는 `rc_w0`, ORE·FE·NE·IDLE 은 "SR 읽고 DR 읽기" 순서로 클리어 | 같은 SR 안에 관습이 두 가지 |
| STM32 L4/G4/H7 | `USART_ISR` + `USART_ICR` | ISR 은 읽기 전용, ICR 에 1 을 쓰면 해당 플래그 클리어 | 상태와 클리어를 **다른 주소** 로 분리 |
| nRF52 | `EVENTS_xxx` | 이벤트마다 **32비트 레지스터 하나**. HW 가 1 로, SW 가 0 을 써서 클리어 | `NRF_TIMER0->EVENTS_COMPARE[0] = 0;` 후 되읽기 — 이웃 비트가 없어 RMW 문제 자체가 없다 |
| nRF52 | `INTENSET` / `INTENCLR` | W1S / W1C 쌍, 읽으면 현재 enable 상태 | `INTENSET = mask;` |
| Cortex-M NVIC | `ISER/ICER`, `ISPR/ICPR` | W1S / W1C 쌍 | `NVIC->ICPR[n >> 5] = 1u << (n & 31);` |
| Arm GIC | `GICD_ISENABLERn/ICENABLERn`, `ISPENDRn/ICPENDRn` | W1S / W1C 쌍. ack 는 `IAR` **읽기**(부작용), 완료는 `EOIR` 쓰기 | 쌍 레지스터에 1 쓰기 |
| PCIe | Status(cfg 0x06) 오류 비트, Device Status, AER Error Status | `RW1C` (AER 는 리셋에도 유지되는 `RW1CS`) | 지울 비트만 1 을 써서 클리어 |
| NVMe | `CSTS.NSSRO` (bit 4) / `CSTS.RDY·CFS·SHST` | NSSRO 는 RW1C, 나머지 RO | 초기화 때 `CSTS = NVME_CSTS_NSSRO;` |
| NVMe | `INTMS` / `INTMC` | RW1S / RW1C 쌍 (MSI-X 사용 시엔 안 씀) | 마스크 set/clear 를 한 번의 쓰기로 |
| AHCI (SATA) | `PxIS`, `IS` | RWC = W1C | 읽은 값을 그대로 되써서 본 것만 ack |
| RP2040 | 모든 주변장치 레지스터 | +0x1000 XOR, +0x2000 SET, +0x3000 CLR alias (SIO 블록 제외) | `hw_set_bits(&reg, m)` = SET alias 쓰기 |
| NXP Kinetis | `PORTx_PCRn.ISF` | w1c, MUX/IRQC(RW)와 같은 레지스터 | RW 수정 시 ISF 자리에 0 |

같은 STM32 안에서도 EXTI 는 1, TIM 은 0 이다. **"이 칩은 W1C 다" 가 아니라 "이 비트는 W1C 다"** 가 올바른 기억 단위이고, 그래서 RM 의 비트 표기(`rc_w1`, `rc_w0`, `rs`, `r`, `w`, `rw`)를 읽는 습관이 필요하다.

### Part C — main 의 RMW 가 ISR 의 변경을 되돌린다

이번엔 경쟁 상대가 소프트웨어(ISR)다. UART TX 가 끝나 보낼 게 없으면 ISR 이 `TXIE` 를 끈다 (안 끄면 TX-empty 인터럽트가 계속 들어오는 인터럽트 폭주). 그 순간 main 이 `RXIE` 를 켜려고 CR 을 RMW 하고 있었다면:

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">CR = EN|TXIE (0x5), SR = TX 완료 대기 중 — main 이 RXIE 를 켠다</text>
  <g fill="currentColor" font-weight="bold"><text x="10" y="62">RMW</text><text x="10" y="94">ISR</text><text x="10" y="186">alias</text><text x="10" y="218">ISR</text></g>
  <rect x="70" y="44" width="120" height="28" class="f-acc"/>
  <rect x="200" y="76" width="250" height="28" class="f-warn"/>
  <rect x="460" y="44" width="150" height="28" class="f-acc"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="78" y="63">rCR=5 (v=5)</text>
    <text x="208" y="95">IRQ rSR=2 wSR=2 wCRCLR=4</text>
    <text x="468" y="63">wCR = v|2 = 7</text>
  </g>
  <rect x="70" y="118" width="660" height="26" class="f-warn"/>
  <text x="78" y="136" fill="currentColor">결과 CR = 0x7: ISR 이 끈 TXIE 가 되살아남 → TX-empty 인터럽트 폭주</text>
  <rect x="70" y="168" width="140" height="28" class="f-acc"/>
  <rect x="220" y="200" width="250" height="28" class="f-mut"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="78" y="187">wCRSET=2</text>
    <text x="228" y="219">IRQ rSR=2 wSR=2 wCRCLR=4</text>
  </g>
  <rect x="70" y="242" width="660" height="26" class="f-ok"/>
  <text x="78" y="260" fill="currentColor">결과 CR = 0x3 (EN|RXIE): 쓰기 한 번은 쪼갤 수 없다 — ISR 이 앞에 오든 뒤에 오든 같은 결과</text>
  <text x="10" y="290" fill="currentColor">volatile 이어도 RMW 는 RMW 다 (C-B09). 읽기가 없으면 "낡은 사본" 도 없다.</text>
</svg>
```

해결책 세 가지, 상황별로:

| 방법 | 언제 | 비용·한계 |
|---|---|---|
| **set/clear alias 에 한 번 쓰기** (`CRSET`, BSRR, NVIC ISER/ICER, RP2040 +0x2000/+0x3000) | 하드웨어가 제공할 때, 단일 비트·독립 비트 | 거의 공짜. 하지만 **여러 비트 필드는 못 바꾼다** — CLR 후 SET 두 번이면 중간 상태가 버스에 나간다 |
| **임계 구역** (`__disable_irq()` … `__enable_irq()`, 실전은 PRIMASK 저장·복원) | 필드 변경, alias 없는 칩 | 인터럽트 지연(latency)이 생긴다. 짧게. 중첩 호출이면 PRIMASK 를 저장했다 복원해야 한다 (C-M14) |
| **소유권 분리** — 그 레지스터는 ISR 만(또는 main 만) 쓴다 | 설계 단계 | 가장 깔끔. 예: TXIE 는 ISR 만 끄고 main 은 "켜기 요청" 만 큐에 넣음 |

`dev_set_mode(2)` 의 실제 trace 는 `"DI rCR=115 wCR=25 EI IRQ rSR=2 wSR=2 wCRCLR=4"` 이다. CR 을 읽는 순간 TX 인터럽트가 도착했지만 마스크 중이라 **펜딩** 되었고, `EI` 직후에 들어와 TXIE 를 껐다. 그리고 `wCR=25` 는 OVF 자리에 0 을 써서(reg_modify) OVF 를 보존했다 — 최종 CR = `0x121` (EN | MODE=2 | OVF). 임계 구역은 소프트웨어 경쟁을, reg_modify 는 W1C 비트를 각각 책임진다. 둘은 다른 문제라 둘 다 필요하다.

**ISR 의 한 번 읽기 (`dev_isr`)**: 주어진 `dev_isr_buggy()` 는 플래그마다 SR 을 다시 읽고 `sr_clear_buggy()` 로 지운다. 첫 `if` 에서 `SR |= RX` 가 TX·ERR 까지 지우므로 다음 두 `if` 는 0 을 본다 → 테스트에서 `rx==1, tx==0, err==0`. 고친 ISR 은 `sr_ack_seen()` 으로 한 번 읽고 본 것만 ack 한 뒤 **로컬 스냅샷** 으로 분기한다 (C-B09 의 read-to-clear 규칙과 같은 원리). ISR 이 SR 을 읽은 직후 ERR 이 오면 ERR 은 SR 에 남아 있다가(테스트 `sr_store == SR_ERR`) 다음 ISR 진입에서 처리된다 — 실제 NVIC 는 플래그가 남아 있는 한 IRQ 를 다시 펜딩한다.

### 두 종류의 경쟁 — 한 장 요약

| | 하드웨어 vs 소프트웨어 | 소프트웨어 vs 소프트웨어 |
|---|---|---|
| 대상 | 상태 비트 (HW 가 세움) | RW 제어 비트 (ISR·main 이 둘 다 씀) |
| 대표 버그 | `TIMx->SR &= ~UIF` 가 사이에 생긴 CC1IF 를 지움 | `CR \|= RXIE` 가 ISR 이 끈 TXIE 를 되살림 |
| 인터럽트 끄기로 해결? | **아니오** — 하드웨어는 계속 돈다 | 예 |
| LDREX/STREX? | 아니오 — 주변장치는 exclusive monitor 를 모른다 | 메모리라면 예. 주변장치 영역 exclusive 접근은 구현 정의라 기대하지 말 것 |
| 진짜 해결 | 읽지 않고 쓴다: W1C 는 `= bit`, W0C 는 `= ~bit` | alias 한 번 쓰기, 임계 구역, 소유권 분리 |

### 실제 asm — `|=` 한 줄은 버스 접근 두 번

`demo_asm.c` 를 `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S` 로 컴파일한 **실제 출력** (지시어 줄 생략, Apple clang 21):

```text
exti_clear_bad:                  @ EXTI_PR |= 1u << 3
	movw	r0, #15380           @ 0x3C14
	movt	r0, #16385           @ 0x4001 → r0 = 0x40013C14
	ldr	r1, [r0]             @ ① 읽기: pending 전부
	orr	r1, r1, #8
	str	r1, [r0]             @ ② 쓰기: 읽은 1 전부 + bit3 → 전부 클리어
	bx	lr
exti_clear_good:                 @ EXTI_PR = 1u << 3
	movw	r0, #15380
	movt	r0, #16385
	movs	r1, #8
	str	r1, [r0]             @ 쓰기 한 번
	bx	lr
tim_clear_bad:                   @ TIM2_SR &= ~1u
	movs	r0, #16
	movt	r0, #16384           @ 0x40000010
	ldr	r1, [r0]             @ ① 읽기
	bic	r1, r1, #1           @    ← 이 사이에 HW 가 CC1IF 를 세우면
	str	r1, [r0]             @ ② 0 을 써서 지운다
	bx	lr
tim_clear_good:                  @ TIM2_SR = ~1u
	movs	r0, #16
	movt	r0, #16384
	mvn	r1, #1               @ r1 = 0xFFFFFFFE
	str	r1, [r0]
	bx	lr
exti_ack_seen:                   @ s = EXTI_PR; EXTI_PR = s;
	movw	r1, #15380
	movt	r1, #16385
	ldr	r0, [r1]
	str	r0, [r1]             @ 본 것만 지움 — 반환값 s 로 처리
	bx	lr
```

`ldr`/`str` 사이 창은 짧지만 0 이 아니다. 주변장치 버스가 느리면(APB 브리지 대기) 수십 사이클이 될 수도 있고, 72 MHz 타이머 입력 캡처는 그 창에 충분히 들어온다. "드물게, 부하가 걸릴 때만" 재현되는 전형적인 필드 버그다.

> **Cortex-M3/M4 bit-band 도 함정이다.** bit-band alias 에 쓰는 건 C 코드로는 store 한 번이지만, 버스 쪽에서는 하드웨어가 워드를 읽고-고치고-쓰는 **원자적 RMW** 다. 원자성은 다른 마스터(ISR)에 대해서만 보장될 뿐, W1C 레지스터에선 읽어 온 다른 pending 비트들이 1 로 다시 쓰여 지워진다. STM32L476 의 `EXTI_PR1` 을 bit-band 로 클리어했더니 다른 라인까지 지워졌다는 사례가 ST 커뮤니티에 있다. (Cortex-M7 에는 bit-band 자체가 없다.)

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 53/53 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 53/53, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `EXTI->PR \|= EXTI_PR_PR3;` | 읽은 pending 1 들을 전부 다시 써서 모든 라인 클리어 | `EXTI->PR = EXTI_PR_PR3;` |
| W1C 에 `SR &= ~FLAG;` | FLAG **빼고** 다른 pending 을 1 로 써서 지움. FLAG 는 남는다 — 정반대 | `SR = FLAG;` |
| STM32 TIM 에 `TIMx->SR = TIM_SR_UIF;` (W1C 습관) | rc_w0: UIF 는 1 을 받아 무시, 0 받은 나머지 플래그 전부 클리어 | `TIMx->SR = ~TIM_SR_UIF;` |
| `TIMx->SR &= ~TIM_SR_UIF;` | 평소엔 되지만 읽기~쓰기 사이에 HW 가 세운 플래그를 0 으로 지움 | `TIMx->SR = ~TIM_SR_UIF;` |
| ISR 에서 플래그마다 SR 을 다시 읽고 따로 클리어 | 첫 클리어가 다른 플래그를 지우면 뒤의 `if` 가 못 봄 | 한 번 읽어 스냅샷, 본 것만 ack |
| 혼합 레지스터(RW + W1C)에 `REG = (REG & ~m) \| v;` | 읽어 온 W1C 1 을 다시 써서 sticky 플래그 소실 | `REG = (REG & RW_MASK & ~m) \| v;` (W0C 자리는 `\| W0C_MASK`) |
| ISR 과 공유하는 CR 에 `CR \|= RXIE;` | ISR 이 사이에 바꾼 비트를 낡은 사본으로 덮어씀 | `CRSET = RXIE;` 또는 임계 구역 |
| W1C 비트를 C 비트필드로 선언하고 `sr.rx = 1;` | 비트필드 쓰기는 숨은 워드 RMW → `\|=` 와 같은 버그 (C-M03) | 마스크 상수 + 전체 워드 쓰기 |
| 디버거 메모리 창을 RC 레지스터 위에 열어 둠 | 디버거의 읽기도 읽기다 → 이벤트가 사라짐 | RC 레지스터는 watch 에서 빼기. 설계라면 W1C 선호 |
| nRF 에서 `EVENTS_xxx = 0;` 직후 ISR 종료 | 쓰기가 주변장치에 닿기 전 복귀 → 같은 IRQ 재진입 | 클리어 후 같은 레지스터를 되읽기(`(void)NRF_X->EVENTS_Y;`) 또는 `__DSB()` |

## 8. 면접 Q&A

<details>
<summary>Q1. 상태 레지스터가 write-1-to-clear 입니다. ISR 에서 `SR |= RX_FLAG;` 로 지우면 무슨 일이 생기나요?</summary>

`SR |= RX` 는 `SR = SR | RX` — 읽은 값에 이미 1 인 모든 pending 비트를 다시 1 로 쓰므로 **pending 플래그가 전부 지워진다**. 그 이벤트들은 처리되지 않은 채 사라진다(예: TX 완료를 놓쳐 송신이 멈춤). 고침은 `SR = RX;` — 0 을 쓴 비트는 무시되므로 쓰기 하나로 RX 만 지운다. 꼬리질문 "`SR &= ~RX` 는?" → RX 만 빼고 나머지를 지우는 반대 버그.

</details>

<details>
<summary>Q2. STM32 타이머 UIF 는 어떻게 지우나요? EXTI 랑 왜 다르죠?</summary>

`TIMx_SR` 의 플래그는 `rc_w0` — 0 을 써야 지워지고 1 쓰기는 무효. 그래서 `TIMx->SR = ~TIM_SR_UIF;` (HAL `__HAL_TIM_CLEAR_FLAG` 의 정의와 같다). EXTI_PR 은 `rc_w1` 이라 `EXTI->PR = 1u << n`. 관습은 칩이 아니라 **비트마다** 다르므로 RM 의 표기를 확인해야 한다. 둘의 공통점: 어느 쪽이든 "무해한 값(W1C 는 0, W0C 는 1)을 기본으로 깔고 지울 비트만 반대 값" 인 **읽기 없는 단일 쓰기** 가 정답이다.

</details>

<details>
<summary>Q3. 그럼 `TIMx->SR &= ~UIF;` 앞뒤로 인터럽트를 끄면 안전해지나요?</summary>

아니다. 경쟁 상대가 ISR 이 아니라 **하드웨어(타이머 카운터)** 다. `ldr` 과 `str` 사이에 CC1IF 가 세워지면, 읽은 값에서 그 비트는 0 이었으므로 0 이 쓰여 지워진다. 인터럽트 마스크, LDREX/STREX 모두 하드웨어를 멈추지 못한다. 해결은 쓰기 값 자체를 바꾸는 것(`= ~UIF`)뿐. 꼬리질문 "그럼 하드웨어 설계자는 왜 상태 비트를 RW 로 안 만드나?" → RW 라면 소프트웨어가 비트 하나를 지우려면 반드시 RMW 를 해야 하고, 이 경쟁을 원천적으로 피할 수 없다. W1C/W0C 는 "0(또는 1) = 변화 없음" 을 줘서 단일 쓰기 클리어를 가능하게 하려고 존재한다.

</details>

<details>
<summary>Q4. 읽으면 지워지는(read-to-clear) 상태 레지스터와 W1C 의 장단점은?</summary>

RC 는 읽기 한 번으로 ack 까지 끝나 빠르지만 ① 디버거·로깅 코드의 "그냥 읽기" 가 이벤트를 지우고 ② 같은 레지스터에 제어 비트가 있으면 RMW 자체가 불가능하며 ③ 여러 소유자(ISR 과 main)가 한 레지스터를 나눠 쓸 수 없다 — 먼저 읽은 쪽이 다 가져간다. W1C 는 쓰기 하나가 더 들지만 "무엇을 지울지" 를 소프트웨어가 비트 단위로 고를 수 있다. 그래서 PCIe, AHCI, NVMe 같은 규격은 상태 비트에 RW1C 를 쓴다. GIC 의 `IAR` 처럼 "읽기 = ack" 가 의도된 설계(인터럽트 ID 를 한 번만 받아야 하는 경우)도 있다.

</details>

<details>
<summary>Q5. ISR 에서 플래그는 처리 전에 지우나요, 후에 지우나요?</summary>

보통 **처리 전에(본 것만) ack** 한다. 처리 도중 같은 이벤트가 다시 오면 플래그가 다시 서서 ISR 이 한 번 더 불린다. 처리 후에 지우면 처리 중 다시 온 같은 이벤트를 함께 지워 잃어버린다. 예외: 레벨형 원인(UART RXNE 처럼 FIFO 가 빌 때까지 다시 서는 플래그, 레벨 감지 GPIO)은 원인을 먼저 없애야(FIFO 비우기) 플래그가 실제로 내려간다. 그리고 ISR 마지막 쓰기가 주변장치에 닿기 전에 복귀하면 재진입하므로, 끝에서 지웠다면 되읽기 또는 `__DSB()`.

</details>

<details>
<summary>Q6. NVIC 는 왜 ISER/ICER 처럼 set/clear 레지스터를 따로 두나요? 필드도 그렇게 바꿀 수 있나요?</summary>

enable 비트를 RMW 하면 다른 문맥(다른 ISR, RTOS 태스크)이 사이에 바꾼 enable 을 되돌릴 수 있다. W1S/W1C 쌍은 "1 = 동작, 0 = 무시" 라서 **읽기 없는 단일 쓰기** 로 독립 비트를 바꾼다 — C-B01 의 BSRR, nRF `INTENSET/INTENCLR`, NVMe `INTMS/INTMC`, GIC `ISENABLER/ICENABLER`, RP2040 SET/CLR alias 가 모두 같은 발상. 여러 비트 필드는 CLR → SET 두 번 쓰면 중간 값이 잠깐 하드웨어에 보이므로(예: MODE 01 → 00 → 10), 그 중간 상태가 문제 되면 임계 구역 안에서 한 번에 쓴다. RP2040 은 XOR alias 로 `old ^ new` 를 한 번 쓰는 방법도 있지만, 그 역시 `old` 를 읽어야 하므로 동시 변경엔 임계 구역이 필요하다.

</details>

<details>
<summary>Q7. RW 비트와 W1C 비트가 같은 레지스터에 있습니다. 설정 비트 하나를 바꾸는 코드를 써 보세요.</summary>

`REG = (REG & RW_MASK & ~clr) | set;` — 읽은 값에서 W1C 비트를 마스크로 걸러 0 을 쓴다(무해). W0C 비트가 있다면 `| W0C_MASK` 로 1 을 깐다. RC 비트가 섞여 있다면 읽기 자체가 이벤트를 지우므로 RMW 를 포기하고 RW 비트를 소프트웨어 shadow 로 관리한다. 그리고 이 공식은 HW 경쟁만 막는다 — ISR 도 같은 RW 비트를 쓴다면 임계 구역을 추가로 씌운다. Kinetis `PORTx_PCRn` (MUX + ISF), 일부 UART 상태 레지스터(설정 비트와 W1C 플래그 혼재)가 실제 예.

</details>

<details>
<summary>Q8. (검증 관점) 새 실리콘의 레지스터 속성이 스펙대로인지 어떻게 테스트하나요?</summary>

비트마다 상태를 만들고 쓰고 다시 읽는 **bit-bash**: ① 리셋값 확인 ② "1 상태에서 0 쓰기", "1 상태에서 1 쓰기", "0 상태에서 1 쓰기" 결과로 RW/W1C/W0C/W1S/RO 판별 ③ 두 번 읽어 RC 판별 (추가 연습 5). 상태 비트를 1 로 만들려면 실제 이벤트(타이머 만료, 루프백 등)나 테스트 모드가 필요하다. UVM 은 `uvm_reg_bit_bash_seq` / `uvm_reg_hw_reset_seq` 로 이를 자동화하고, 필드 access policy("W1C", "RC" …)는 IP-XACT/SystemRDL 에서 생성된다. 펌웨어 헤더도 같은 소스에서 생성하면 관습 착오가 줄어든다.

</details>

## 9. 추가 연습

### 연습 1 — W1T 와 원자 alias (RP2040 식)

`reg_spec` 에 W1T(write-1-to-toggle) 마스크를 추가하고, 저장소 하나에 일반/XOR/SET/CLR 네 alias(RP2040 의 +0x0000/+0x1000/+0x2000/+0x3000)를 붙여 `led_toggle/on/off` 를 **읽기 없이** 구현하라.

<details>
<summary>답 보기</summary>

`apply_write` 에 `v ^= (w & w1t);` 한 줄. alias 는 spec 만 다른 4개 뷰다: `{rw=~0}`, `{w1t=~0}`, `{w1s=~0}`, `{w1c=~0}`. `led_toggle(pin)` = XOR alias 에 `1u << pin` 한 번 쓰기 — 읽기 횟수 0 이 테스트 조건이다. STM32 GPIO 에는 토글 레지스터가 없어서 (BSRR 은 set/reset 만) 원자적 토글이 필요하면 임계 구역을 쓴다 (C-B01). 참고로 RP2040 의 GPIO 출력은 SIO 블록이라 이 alias 대신 별도의 `GPIO_OUT_SET/CLR/XOR` 레지스터를 쓴다.

</details>

### 연습 2 — 쓰기 전용(WO) 레지스터와 shadow

읽으면 항상 0 이 나오는(RAZ) 쓰기 전용 설정 레지스터에서 `REG |= bit` 가 무엇을 하는지 재현하고, shadow 변수로 고쳐라.

<details>
<summary>답 보기</summary>

읽기가 0 이므로 `REG |= 0x10` 은 `REG = 0x10` — 이전에 써 둔 `0x3` 이 사라진다. 고침: 마지막으로 쓴 값을 RAM 에 `shadow` 로 들고 `shadow |= bit; REG = shadow;`. RC 비트가 섞인 레지스터의 제어 비트, 일부 SPI 주변 칩(DAC, PMIC)의 쓰기 전용 레지스터에서 같은 방법을 쓴다. shadow 자체를 ISR 과 공유하면 그건 다시 소프트웨어 경쟁이다 — 임계 구역.

</details>

### 연습 3 — Kinetis PORT_PCR 식 혼합 레지스터

MUX[10:8]·IRQC[19:16]·하위 설정 비트(RW)와 ISF[24](w1c)가 한 레지스터에 있다(단순화 모델). 핀 인터럽트가 펜딩된 상태에서 MUX 를 바꿔도 ISF 가 남게 `pcr_set_mux()` 를 작성하라.

<details>
<summary>답 보기</summary>

`v = pcr & ~(MUX_Msk | ISF); write(v | MUX(m));` — 읽은 값에서 W1C 비트를 0 으로 만든다. 버그 버전 `(pcr & ~MUX_Msk) | MUX(m)` 은 읽어 온 `ISF=1` 을 다시 써서 펜딩 인터럽트를 지운다. 본문 `reg_modify` 의 특수한 경우.

</details>

### 연습 4 — 상태 레지스터 하나, 주인 둘

ISR 은 RX·ERR 를, main 루프는 TX 완료를 폴링으로 처리한다. ISR 의 ack 가 main 의 TX 를 지우지 않게 `isr_ack_owned(owned)` 를 작성하라.

<details>
<summary>답 보기</summary>

`s = SR & owned; if (s) SR = s; return s;` — 한 번 읽고 **내 몫만** ack. `sr_ack_seen()` 처럼 본 것 전부를 지우면 main 이 폴링할 TX 를 ISR 이 먹어 버린다. 소유권을 비트 단위로 나누는 게 W1C 의 장점이고, RC 레지스터로는 불가능하다 (Q4).

</details>

### 연습 5 — bit-bash: 블랙박스 레지스터의 비트 의미 판별

리셋, 하드웨어 이벤트(비트를 1 로), 읽기, 쓰기만 써서 비트 하나가 RO/RW/W1C/W0C/W1S/RC 중 무엇인지 판별하는 `classify_bit()` 을 작성하라.

<details>
<summary>답 보기</summary>

먼저 1 로 만들고 두 번 읽는다 — 첫 읽기 1, 둘째 0 이면 RC. 그다음 세 실험: a = "1 에서 0 쓰기" 결과, b = "1 에서 1 쓰기" 결과, c = "0 에서 1 쓰기" 결과. 1. 문제의 첫 그림 표 그대로: RW (0,1,1), W1C (1,0,0), W0C (0,1,0), W1S (1,1,1), RO (1,1,0). 다섯 조합이 모두 달라서 판별된다. 실제 실리콘에서는 W1S/W1T 처럼 상태를 바꾸는 쓰기가 다른 블록을 깨울 수 있으므로(인터럽트 강제 펜딩 등) 테스트 순서와 복구 절차가 중요하다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 16/16 passed
```

## 10. 핵심 정리

- 레지스터는 **비트마다** 쓰기 의미가 다르다: RW, RO, W1C, W0C(rc_w0), W1S, RC. 칩 단위가 아니라 비트 단위로 RM 표기를 읽는다 (같은 STM32 에서 EXTI 는 rc_w1, TIM 은 rc_w0).
- RMW 는 "읽은 값" 을 쓴다 → W1C 에서는 읽은 1 들이, W0C 에서는 읽은 뒤 바뀐 0 들이 **엉뚱한 플래그를 지운다**.
- 상태 비트 클리어 = **읽기 없는 단일 쓰기**: W1C `SR = bit`, W0C `SR = ~bit`. 무해한 값(W1C·W1S 는 0, W0C 는 1)을 기본으로 깐다.
- "본 것만 ack" 는 `s = SR; SR = s;` — 읽은 뒤 온 이벤트는 살아남는다. 처리는 스냅샷 `s` 로.
- 혼합 레지스터 RMW: `REG = (REG & RW & ~clr) | set | W0C`. RC 가 섞이면 RMW 금지 → shadow.
- 하드웨어 경쟁은 인터럽트 끄기로 못 막는다(쓰기 값으로 해결). 소프트웨어 경쟁(ISR vs main 의 RW 비트)은 set/clear alias, 임계 구역, 소유권 분리로 막는다.
- bit-band 쓰기도, 비트필드 쓰기도 숨은 RMW 다 — W1C 레지스터에 쓰지 않는다.

## 참고자료

- [STM32F4 Reference Manual RM0090](https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) — 1.1 레지스터 비트 표기(`rc_w0`, `rc_w1`), EXTI_PR, TIMx_SR, USART_SR
- [ST Community — TIMx_SR rc_w0 와 `__HAL_TIM_CLEAR_FLAG` (`SR = ~flag`)](https://community.st.com/t5/stm32-mcus-products/information-missing-in-reference-manual-for-register-clearing/m-p/673168)
- [ST Community — STM32L476 EXTI_PR1 를 bit-band 로 클리어하면 다른 라인도 지워짐](https://community.st.com/t5/stm32-mcus-products/stm32l476-clearing-pending-bit-via-bit-banding-alias/m-p/486198)
- [RIOT OS commit — STM32 EXTI->PR `|=` 를 `=` 로 수정](https://git.riot-os.org/RIOT-OS/RIOT/commit/578bd31908fb4c474f1e7999e8b70c0afee71dec)
- [Nordic nRF52832 Product Specification](https://docs.nordicsemi.com/) — Peripheral interface: EVENTS 레지스터, INTENSET/INTENCLR, interrupt clearing 후 되읽기
- [Raspberry Pi RP2040 Datasheet 2.1.2 Atomic Register Access](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
- Arm Cortex-M4 Devices Generic User Guide — NVIC ISER/ICER/ISPR/ICPR, bit-banding; Arm GIC Architecture Specification — GICD_ISENABLER/ICENABLER, IAR/EOIR
- PCI Express Base Specification — 레지스터 속성 RW1C/RW1CS, AER; [NVM Express Base Specification](https://nvmexpress.org/specifications/) — CSTS.NSSRO, INTMS/INTMC; Serial ATA AHCI 1.3.1 — PxIS (RWC)
- Linux `drivers/ata/libahci.c` — 포트 인터럽트 상태 read-then-write-back ack
- IEEE 1800.2 (UVM) `uvm_reg_field` access policies, Accellera IP-XACT `modifiedWriteValue` / `readAction`
- 관련 레슨: C-B01 (BSRR 와 RMW 경쟁), C-B09 (volatile 은 원자성을 주지 않는다, read-to-clear), C-M01 (volatile 레지스터 구조체), C-M03 (비트필드 쓰기의 숨은 RMW), C-M14 (ISR 공유 데이터·임계 구역)
