---
id: SIV-B05
time: 70분
updated: 2026-11-08
---
# SIV-B05 Walking 1s / Walking 0s 비트 테스트

## 학습 목표

- walking-1 / walking-0 패턴으로 **데이터 버스와 RW 레지스터의 stuck-at-0/1, 비트 간 short(wired-OR/AND)** 를 찾는 원리를 설명하고 C로 구현한다.
- "검출(detection)"과 "진단(diagnosis)"을 구분한다: walk1 하나로도 잡히지만, **결함 종류와 위치**를 가르려면 walk1 + walk0 의 증상(syndrome)이 둘 다 필요하다.
- 실전 함정 세 가지를 다룬다: **walk_mask** (RO·부작용 비트 제외), **save/restore**, **bus-hold** (끊긴 선이 직전 값을 기억해 테스트를 통과해 버리는 현상).
- IP 블록 전체를 걸어 "같은 비트가 여러 레지스터에서 틀리면 공통 경로(버스), 한 레지스터에서만 틀리면 그 레지스터" 로 **위치를 추정하는 리포트**를 만든다.
- 0/~0, 55/AA, walking, counting 패턴이 각각 어떤 결함 모델을 몇 번의 쓰기로 잡는지 표로 말할 수 있다.

## 1. 문제

First silicon 이나 새 보드가 들어오면 리셋값 확인(SIV-B03) 다음에 바로 돌리는 게 "선 하나하나가 살아 있나" 테스트다. Rivian·NVIDIA 같은 실리콘/보드 검증 면접에서 "데이터 버스 테스트를 짜 보라, walking 0s 는 왜 따로 하나" 는 단골 질문이다.

데이터 버스(data bus)의 선 32개는 각자 따로 0/1을 날라야 한다. 고장 모델(fault model)은 세 가지다.

| 결함 | 물리적 원인 예 | 증상 |
|---|---|---|
| **stuck-at-0 / 1 (SA0/SA1)** | 선이 GND/VDD에 붙음, RTL tie-off 실수, 핀 미연결(내부 pull) | 그 비트가 무엇을 써도 0(또는 1) |
| **bridge (short)** | PCB 솔더 브리지, 인접 배선 short, RTL에서 두 비트를 같은 신호에 연결 | 두 선이 같은 값으로 묶임. 1이 이기면 **wired-OR**, 0이 이기면 **wired-AND** |
| **open** | 냉납(cold joint), 끊긴 via | 그 선은 아무도 구동하지 않음 → 직전 값(bus-hold / 기생 용량)이나 pull 값을 읽음 |

**walking-1** 은 32개 선 중 딱 하나만 1, **walking-0** 은 딱 하나만 0인 패턴을 차례로 쓰고 다시 읽는다. 한 단계에 "튀는" 비트가 하나뿐이라 틀리면 **어느 선인지** 바로 보인다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">8비트로 줄여 본 walking-1 / walking-0 (단계 i = 비트 i 를 "걷는다")</text>
  <rect x="480" y="52" width="252" height="190" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="62" y="44" fill="currentColor" font-weight="bold">walking-1: 0 사이에 1 하나</text>
  <g fill="currentColor" text-anchor="middle" font-size="11" opacity=".7"><text x="73" y="58">7</text><text x="95" y="58">6</text><text x="117" y="58">5</text><text x="139" y="58">4</text><text x="161" y="58">3</text><text x="183" y="58">2</text><text x="205" y="58">1</text><text x="227" y="58">0</text></g>
  <text x="54" y="79" fill="currentColor" text-anchor="end" font-size="12">step 0</text><rect x="216" y="64" width="22" height="22" class="f-acc"/><rect x="62" y="64" width="176" height="22" fill="none" stroke="currentColor" stroke-width=".6"/><g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="73" y="79">0</text><text x="95" y="79">0</text><text x="117" y="79">0</text><text x="139" y="79">0</text><text x="161" y="79">0</text><text x="183" y="79">0</text><text x="205" y="79">0</text><text x="227" y="79">1</text></g>
  <text x="54" y="101" fill="currentColor" text-anchor="end" font-size="12">step 1</text><rect x="194" y="86" width="22" height="22" class="f-acc"/><rect x="62" y="86" width="176" height="22" fill="none" stroke="currentColor" stroke-width=".6"/><g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="73" y="101">0</text><text x="95" y="101">0</text><text x="117" y="101">0</text><text x="139" y="101">0</text><text x="161" y="101">0</text><text x="183" y="101">0</text><text x="205" y="101">1</text><text x="227" y="101">0</text></g>
  <text x="54" y="123" fill="currentColor" text-anchor="end" font-size="12">step 2</text><rect x="172" y="108" width="22" height="22" class="f-acc"/><rect x="62" y="108" width="176" height="22" fill="none" stroke="currentColor" stroke-width=".6"/><g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="73" y="123">0</text><text x="95" y="123">0</text><text x="117" y="123">0</text><text x="139" y="123">0</text><text x="161" y="123">0</text><text x="183" y="123">1</text><text x="205" y="123">0</text><text x="227" y="123">0</text></g>
  <text x="54" y="145" fill="currentColor" text-anchor="end" font-size="12">step 3</text><rect x="150" y="130" width="22" height="22" class="f-acc"/><rect x="62" y="130" width="176" height="22" fill="none" stroke="currentColor" stroke-width=".6"/><g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="73" y="145">0</text><text x="95" y="145">0</text><text x="117" y="145">0</text><text x="139" y="145">0</text><text x="161" y="145">1</text><text x="183" y="145">0</text><text x="205" y="145">0</text><text x="227" y="145">0</text></g>
  <text x="54" y="167" fill="currentColor" text-anchor="end" font-size="12">step 4</text><rect x="128" y="152" width="22" height="22" class="f-acc"/><rect x="62" y="152" width="176" height="22" fill="none" stroke="currentColor" stroke-width=".6"/><g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="73" y="167">0</text><text x="95" y="167">0</text><text x="117" y="167">0</text><text x="139" y="167">1</text><text x="161" y="167">0</text><text x="183" y="167">0</text><text x="205" y="167">0</text><text x="227" y="167">0</text></g>
  <text x="54" y="189" fill="currentColor" text-anchor="end" font-size="12">step 5</text><rect x="106" y="174" width="22" height="22" class="f-acc"/><rect x="62" y="174" width="176" height="22" fill="none" stroke="currentColor" stroke-width=".6"/><g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="73" y="189">0</text><text x="95" y="189">0</text><text x="117" y="189">1</text><text x="139" y="189">0</text><text x="161" y="189">0</text><text x="183" y="189">0</text><text x="205" y="189">0</text><text x="227" y="189">0</text></g>
  <text x="54" y="211" fill="currentColor" text-anchor="end" font-size="12">step 6</text><rect x="84" y="196" width="22" height="22" class="f-acc"/><rect x="62" y="196" width="176" height="22" fill="none" stroke="currentColor" stroke-width=".6"/><g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="73" y="211">0</text><text x="95" y="211">1</text><text x="117" y="211">0</text><text x="139" y="211">0</text><text x="161" y="211">0</text><text x="183" y="211">0</text><text x="205" y="211">0</text><text x="227" y="211">0</text></g>
  <text x="54" y="233" fill="currentColor" text-anchor="end" font-size="12">step 7</text><rect x="62" y="218" width="22" height="22" class="f-acc"/><rect x="62" y="218" width="176" height="22" fill="none" stroke="currentColor" stroke-width=".6"/><g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="73" y="233">1</text><text x="95" y="233">0</text><text x="117" y="233">0</text><text x="139" y="233">0</text><text x="161" y="233">0</text><text x="183" y="233">0</text><text x="205" y="233">0</text><text x="227" y="233">0</text></g>
  <text x="290" y="44" fill="currentColor" font-weight="bold">walking-0: 1 사이에 0 하나</text>
  <g fill="currentColor" text-anchor="middle" font-size="11" opacity=".7"><text x="301" y="58">7</text><text x="323" y="58">6</text><text x="345" y="58">5</text><text x="367" y="58">4</text><text x="389" y="58">3</text><text x="411" y="58">2</text><text x="433" y="58">1</text><text x="455" y="58">0</text></g>
  <text x="282" y="79" fill="currentColor" text-anchor="end" font-size="12">step 0</text><rect x="444" y="64" width="22" height="22" class="f-acc"/><rect x="290" y="64" width="176" height="22" fill="none" stroke="currentColor" stroke-width=".6"/><g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="301" y="79">1</text><text x="323" y="79">1</text><text x="345" y="79">1</text><text x="367" y="79">1</text><text x="389" y="79">1</text><text x="411" y="79">1</text><text x="433" y="79">1</text><text x="455" y="79">0</text></g>
  <text x="282" y="101" fill="currentColor" text-anchor="end" font-size="12">step 1</text><rect x="422" y="86" width="22" height="22" class="f-acc"/><rect x="290" y="86" width="176" height="22" fill="none" stroke="currentColor" stroke-width=".6"/><g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="301" y="101">1</text><text x="323" y="101">1</text><text x="345" y="101">1</text><text x="367" y="101">1</text><text x="389" y="101">1</text><text x="411" y="101">1</text><text x="433" y="101">0</text><text x="455" y="101">1</text></g>
  <text x="282" y="123" fill="currentColor" text-anchor="end" font-size="12">step 2</text><rect x="400" y="108" width="22" height="22" class="f-acc"/><rect x="290" y="108" width="176" height="22" fill="none" stroke="currentColor" stroke-width=".6"/><g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="301" y="123">1</text><text x="323" y="123">1</text><text x="345" y="123">1</text><text x="367" y="123">1</text><text x="389" y="123">1</text><text x="411" y="123">0</text><text x="433" y="123">1</text><text x="455" y="123">1</text></g>
  <text x="282" y="145" fill="currentColor" text-anchor="end" font-size="12">step 3</text><rect x="378" y="130" width="22" height="22" class="f-acc"/><rect x="290" y="130" width="176" height="22" fill="none" stroke="currentColor" stroke-width=".6"/><g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="301" y="145">1</text><text x="323" y="145">1</text><text x="345" y="145">1</text><text x="367" y="145">1</text><text x="389" y="145">0</text><text x="411" y="145">1</text><text x="433" y="145">1</text><text x="455" y="145">1</text></g>
  <text x="282" y="167" fill="currentColor" text-anchor="end" font-size="12">step 4</text><rect x="356" y="152" width="22" height="22" class="f-acc"/><rect x="290" y="152" width="176" height="22" fill="none" stroke="currentColor" stroke-width=".6"/><g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="301" y="167">1</text><text x="323" y="167">1</text><text x="345" y="167">1</text><text x="367" y="167">0</text><text x="389" y="167">1</text><text x="411" y="167">1</text><text x="433" y="167">1</text><text x="455" y="167">1</text></g>
  <text x="282" y="189" fill="currentColor" text-anchor="end" font-size="12">step 5</text><rect x="334" y="174" width="22" height="22" class="f-acc"/><rect x="290" y="174" width="176" height="22" fill="none" stroke="currentColor" stroke-width=".6"/><g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="301" y="189">1</text><text x="323" y="189">1</text><text x="345" y="189">0</text><text x="367" y="189">1</text><text x="389" y="189">1</text><text x="411" y="189">1</text><text x="433" y="189">1</text><text x="455" y="189">1</text></g>
  <text x="282" y="211" fill="currentColor" text-anchor="end" font-size="12">step 6</text><rect x="312" y="196" width="22" height="22" class="f-acc"/><rect x="290" y="196" width="176" height="22" fill="none" stroke="currentColor" stroke-width=".6"/><g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="301" y="211">1</text><text x="323" y="211">0</text><text x="345" y="211">1</text><text x="367" y="211">1</text><text x="389" y="211">1</text><text x="411" y="211">1</text><text x="433" y="211">1</text><text x="455" y="211">1</text></g>
  <text x="282" y="233" fill="currentColor" text-anchor="end" font-size="12">step 7</text><rect x="290" y="218" width="22" height="22" class="f-acc"/><rect x="290" y="218" width="176" height="22" fill="none" stroke="currentColor" stroke-width=".6"/><g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="301" y="233">0</text><text x="323" y="233">1</text><text x="345" y="233">1</text><text x="367" y="233">1</text><text x="389" y="233">1</text><text x="411" y="233">1</text><text x="433" y="233">1</text><text x="455" y="233">1</text></g>
  <g fill="currentColor" font-size="12.5"><text x="490" y="74">OR-short D3-D4 를 넣으면:</text><text x="490" y="93">walk1 step 3: 0000 1000 쓰기</text><text x="490" y="112">  → 0001 1000 읽힘 (비트4 rise)</text><text x="490" y="131">walk0 step 3: 1111 0111 쓰기</text><text x="490" y="150">  → 1111 1111 읽힘 (비트3 rise)</text><text x="490" y="188">두 선의 값이 다른 단계에서만</text><text x="490" y="207">short 가 보인다. walking 은</text><text x="490" y="226">모든 쌍을 한 번은 다르게 만든다.</text></g>
  <text x="10" y="270" fill="currentColor">32비트면 각 32단계. walk1 은 SA0 을 1번(step k), SA1 을 31번 본다 — walk0 은 정확히 반대.</text>
  <text x="10" y="294" fill="currentColor">검출(detection)은 walk1 하나로도 되지만, 결함 종류를 가르는 진단(diagnosis)에는 둘 다 필요하다.</text>
  <text x="10" y="318" fill="currentColor" opacity=".8">mask 밖 비트(RO, EN/START 같은 부작용 비트)는 걷지 않고 저장해 둔 값을 그대로 쓴다.</text>
</svg>
```

이 레슨의 mock 은 SIV-B02 의 레지스터 파일을 축약해 넣은 것이다. 모든 접근은 `bus_write(off, v)` / `bus_read(off)` 를 지나고, 결함은 두 군데에 넣을 수 있다: `s.bus` (데이터 버스 — **모든 레지스터** 접근에 적용), `s.regf[i]` (레지스터 하나의 flop / write 경로).

### Part A — walking-1 / walking-0 기본

- `walk_pattern(k, mask, i)` — 단계 i 의 패턴. WALK1 = `1u << i`, WALK0 = mask 안에서 비트 i 만 0.
- `walk(s, off, k, mask, decoy)` — mask 의 비트마다 쓰고 → 읽고 → `& mask` 비교. 결과 `walk_res_t` 에 실패 단계 수 `nfail`, 실패 단계 비트맵 `fail_steps`, **0→1로 틀린 비트 `rise`, 1→0으로 틀린 비트 `fall`**, 첫 실패의 `first_step/exp/act` 를 채운다. 시작할 때 값을 저장하고 끝나면 **복구**한다.
- 버스에 SA0 D7, SA1 D7, OR-short D7–D8, AND-short D7–D8 을 넣고 각각이 어떤 증상을 내는지 확인한다.

### Part B — mask, bus-hold, 그리고 판정(classify)

- `walk` 의 나머지 인자: mask 밖 비트(RO, EN/START 같은 부작용 비트)는 **저장해 둔 값으로 유지**하고, `decoy != NO_DECOY` 면 매 단계 write 직후 decoy 레지스터에 `~패턴` 을 써서 **버스를 반대로 흔든다**.
- `classify(w1, w0)` — 단일 결함 가정에서 두 결과의 증상만 보고 `PASS / SA0 / SA1 / OR_SHORT / AND_SHORT / OPEN / UNKNOWN` 과 관련 비트를 돌려준다. 이중 SA0(D7, D8)을 AND-short 로 오판하면 안 된다.
- 주어진 `detects()` 로 **결함 × 패턴 세트 매트릭스** (0/~0, 55/AA, walk1, walk0, walk1+walk0+shake)를 만들어 기대표와 비교한다.

### Part C — DMA0 블록 전체 walk + 위치 추정 리포트

가상의 DMA0 IP (SCRATCH, CTRL, SRC, DST, LEN, STATUS(RO), IRQ_EN, ID(RO))에 대해 `block_walk()` 를 작성한다.

- `walk_mask != 0` 인 레지스터마다 walk1 + walk0 (decoy = SCRATCH, SCRATCH 자신을 걸 땐 SRC).
- 실패하면 `FAIL reg=… off=… test=… step=… exp=… act=… diff=… bits=[…] (+N more steps)` 와 `DIAG reg=… class=… bits=[…]` 줄.
- 비트별로 "몇 개 레지스터에서 틀렸나"를 세어 **2개 이상 → `SUSPECT bus lines=[…]`**, **1개 → `SUSPECT reg=… bits=[…]`**, 마지막에 `RESULT PASS|FAIL … coverage=0x…` (walk_mask 들의 OR = 실제로 시험한 선).
- 주입: 결함 없음 / 버스 OR-short D7–D8 / CTRL flop bit3 SA1 / 버스 open D5 (decoy 있을 때와 없을 때).

## 2. Problem (English)

> **Data-bus and register bit test with walking ones / walking zeros.**
>
> All register accesses go through `bus_write(soc_t *s, uint32_t off, uint32_t v)` and `uint32_t bus_read(soc_t *s, uint32_t off)` of a mock SoC whose data bus and individual register flops can be injected with stuck-at-0/1 lines, a bridge between two lines (wired-OR or wired-AND), or an open line that returns the last value driven on the bus.
>
> 1. Implement `walk_res_t walk(soc_t *s, uint32_t off, walk_kind_t k, uint32_t mask, uint32_t decoy)`. For every bit `i` set in `mask`, write the walking-one (`1<<i`) or walking-zero (`mask & ~(1<<i)`) pattern, keeping bits outside `mask` at their saved value; if `decoy` is valid, write the complement pattern to the decoy register before reading back. Compare under `mask` and accumulate: failing step count and bitmap, bits that read 1 instead of 0 (`rise`) and 0 instead of 1 (`fall`), and the first failure. Restore the original register (and decoy) values.
> 2. Implement `diag_t classify(const walk_res_t *w1, const walk_res_t *w0)` that, assuming a single fault, identifies PASS, SA0/SA1 at bit `a`, OR/AND bridge between bits `a < b`, or OPEN (bit always reads the complement of what was written when the bus is shaken). Two stuck-at-0 bits must not be reported as an AND bridge.
> 3. Implement `block_sum_t block_walk(...)` that runs both walks on every register with a non-zero walk mask, writes one FAIL line per failing walk and one DIAG line per failing register, and localizes each bad bit: seen in two or more registers → common data path (bus); seen in exactly one → that register.
>
> Example: bus OR-bridge D7–D8, SCRATCH walk1 step 7 → `exp=0x00000080 act=0x00000180 diff=0x00000100 bits=[8]`; classify → `OR_SHORT bits=[7,8]`; block report → `SUSPECT bus lines=[7,8]`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨진 맥에서
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 이 쭉 나오고 "7/50 passed" (exit 1) — TODO 4개를 채워 50/50 을 만든다
```

TODO 는 `walk_pattern`, `walk`, `classify`, `block_walk` 4개다. 결함 모델(`apply_lines`), mock 버스(`bus_write/bus_read`), 매트릭스(`detects`), 리포트 헬퍼(`report_fail`, `bits_str`)는 주어진 코드다. 시작 코드에서 이미 PASS 하는 7개는 "아무것도 못 찾는 테스트" 도 통과하는 체크들이다 — 예를 들어 "decoy 가 없으면 open 을 놓친다" 는 빈 walk 로도 참이다. **결함 없음에서 통과하는 테스트는 아무것도 증명하지 않는다**, 그래서 Part B·C 는 결함을 넣고 잡히는지를 본다.

## 4. 힌트

<details>
<summary>힌트 1 — 패턴은 한 줄</summary>

`b = 1u << i;` WALK1 은 `b`, WALK0 은 `mask & ~b`. WALK0 을 `~b` 로 하면 mask 밖(RO/예약) 비트에도 1을 쓰게 되고, 기대값 비교도 어긋난다.

</details>

<details>
<summary>힌트 2 — rise / fall 은 XOR 를 둘로 쪼갠 것</summary>

`diff = exp ^ act` 중 `act & ~exp` 는 "0이어야 하는데 1" (rise), `exp & ~act` 는 "1이어야 하는데 0" (fall). 단계마다 OR 로 누적한다. SA1 은 rise 만, SA0 은 fall 만 만든다.

</details>

<details>
<summary>힌트 3 — mask 밖은 저장값, 끝나면 복구</summary>

`bus_write(s, off, (saved & ~mask) | exp)` — EN 비트가 1이던 CTRL 을 걸어도 EN 은 한 번도 안 바뀐다. 루프가 끝나면 decoy 와 대상 레지스터를 저장값으로 되돌린다. 실제 칩에서는 SCRATCH 가 아닌 레지스터를 걸고 그대로 두면 다음 테스트가 오염된다.

</details>

<details>
<summary>힌트 4 — OR-short 와 "이중 SA1" 은 rise 비트맵이 같다</summary>

둘 다 rise = {7,8}. 차이는 **walk1 에서 몇 단계가 실패했나**다. OR-short 는 두 선이 다를 때(step 7, 8)만 틀리고, 이중 SA1 은 모든 단계에서 틀린다. 그래서 `w1->fail_steps == bad` 를 같이 본다. AND 쪽은 walk0 으로 대칭.

</details>

<details>
<summary>힌트 5 — 위치 추정은 "히트 수 세기"</summary>

`hits[b]` = 비트 b 가 나빴던 레지스터 수. 버스 결함은 그 선을 쓰는 모든 레지스터에서 보이고, flop 결함은 한 레지스터에서만 보인다. 단, 레지스터의 walk_mask 가 그 비트를 안 덮으면 보일 기회도 없다 (coverage).

</details>

## 5. 풀이

### Part A — 한 단계에 한 비트만 튀게

```c
static walk_res_t walk(soc_t *s, uint32_t off, walk_kind_t k, uint32_t mask, uint32_t decoy) {
    walk_res_t r = { 0, 0, 0, 0, 0, -1, 0, 0 };
    uint32_t saved = bus_read(s, off);
    uint32_t dsave = (decoy != NO_DECOY) ? bus_read(s, decoy) : 0;
    for (unsigned i = 0; i < 32; i++) {
        if (!(mask & (1u << i))) continue;
        uint32_t exp = walk_pattern(k, mask, i);
        bus_write(s, off, (saved & ~mask) | exp);        /* mask 밖은 원래 값 */
        if (decoy != NO_DECOY) bus_write(s, decoy, ~exp); /* 버스를 반대로 흔든다 */
        uint32_t act = bus_read(s, off) & mask;
        ... nfail / fail_steps / rise / fall / first_* 누적 ...
    }
    if (decoy != NO_DECOY) bus_write(s, decoy, dsave);
    bus_write(s, off, saved);
    return r;
}
```

32비트 SCRATCH 에 버스 결함 하나씩 넣었을 때 Part A 테스트가 확인하는 증상 (D7 기준):

| 결함 | walk1 실패 단계 | walk1 rise/fall | walk0 실패 단계 | walk0 rise/fall | 첫 실패 (walk1) |
|---|---|---|---|---|---|
| SA0 D7 | **1** (step 7) | fall {7} | **31** | fall {7} | exp `0x80` act `0x00` |
| SA1 D7 | **31** | rise {7} | **1** (step 7) | rise {7} | step 0: act `0x81` |
| OR D7–D8 | 2 (step 7, 8) | rise {7,8} | 2 | rise {7,8} | exp `0x80` act `0x180` |
| AND D7–D8 | 2 (step 7, 8) | fall {7,8} | 2 | fall {7,8} | exp `0x80` act `0x00` |

읽는 법:

- **stuck-at 은 두 패턴 모두에서 잡힌다.** walk1 은 다른 비트를 전부 0으로 쓰므로 SA1 이 31단계 내내 보이고, 자기 단계에선 1을 써서 SA0 도 보인다. walk0 은 그 반대다. "walk0 은 SA1 용, walk1 은 SA0 용" 이라는 흔한 설명은 반만 맞다 — 둘 다 둘 다 잡는다. 다른 건 **관측 횟수**(증거의 양)다.
- **short 는 두 선의 값이 다를 때만 보인다.** walking 패턴은 어떤 두 비트 a, b 든 step a 와 step b 에서 값을 다르게 만드므로, 32단계면 496쌍 전부를 한 번 이상 "다르게" 만든다 (추가 연습 3에서 직접 센다).
- OR 은 "1이 이긴다" → 걸은 1이 옆 선으로 번진다(rise). AND 는 "0이 이긴다" → 걸은 1이 꺼진다(fall). 첫 실패 줄의 `diff` 비트가 **어느 쪽 선이 틀렸는지** 를 말해 준다: OR 의 walk1 step 7 은 diff 가 비트 8이다.

이 테스트의 원형은 Michael Barr 의 메모리 테스트 모음에 있는 `memTestDataBus()` 다. 외부 RAM 의 **한 주소**에 walking 1 을 쓰고 읽어 첫 실패 패턴을 돌려준다 (추가 연습 2). 주소 한 개만 쓰는 이유: 데이터 버스 테스트는 "선" 을 보는 것이지 메모리 셀을 보는 게 아니기 때문이다 — 주소선은 SIV-B06, 셀은 SIV-B07(March) 이 맡는다.

### Part B — 실전 함정 셋 + 증상으로 판정

**① walk_mask: 걸어도 되는 비트만 걷는다.** 실제 레지스터는 32비트 전부 RW 가 아니다.

| 비트 종류 | walk 하나? | 이유 |
|---|---|---|
| RW 설정 비트 | O | 이 테스트의 대상 |
| RO / 예약(reserved) | X | 쓴 값이 안 남는다 — 기대값이 다르다 (SIV-B04) |
| EN, START, SW_RESET | X | 쓰는 순간 하드웨어가 움직인다 (DMA 가 0번지로 전송 시작!) |
| W1C / RC / WO | X | 쓰기·읽기 자체가 상태를 바꾼다 — access type 별 테스트(SIV-B04), 인터럽트 상태는 SIV-B11 |

그래서 기술서(SIV-B01)에 `rw_mask` (하드웨어 속성)와 별개로 `walk_mask` (테스트 정책)를 둔다. DMA0 의 CTRL 은 `rw_mask=0x0000FFFF` 지만 `walk_mask=0x0000FFFC` 다. UVM 레지스터 모델에서도 같은 장치가 있다: `uvm_reg_bit_bash_seq` 는 레지스터에 `NO_REG_BIT_BASH_TEST` / `NO_REG_TESTS` 속성이 걸려 있으면 건너뛴다. 테스트 코드는 EN 비트를 `(saved & ~mask)` 로 **원래 값 그대로** 계속 쓴다 — 테스트가 `s.toggled[CTRL] & 0x3 == 0` 으로 확인한다.

**② coverage 구멍.** SRC/DST 는 워드 정렬이라 `[1:0]` 이 RO 0이다. 버스에 SA1 D1 을 넣고 SRC 만 걸면 **0단계 실패** — 결함이 없는 게 아니라 볼 수 없는 것이다. SCRATCH 는 같은 결함을 31번 본다. 그래서 많은 IP 가 32비트 전부 RW 인 SCRATCH(DUMMY) 레지스터를 일부러 두고, Part C 리포트는 `coverage` (걸어 본 선의 OR)를 찍는다.

**③ bus-hold: 끊긴 선이 테스트를 통과한다.** 이게 이 레슨에서 가장 실전적인 함정이다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">끊긴 선 D7: write 직후 읽으면 "방금 쓴 값"이 그대로 남아 있다</text>
  <rect x="20" y="50" width="110" height="60" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="75" y="85" fill="currentColor" text-anchor="middle" font-weight="bold">CPU</text>
  <rect x="560" y="50" width="160" height="60" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="640" y="76" fill="currentColor" text-anchor="middle" font-weight="bold">SCRATCH flop</text>
  <text x="640" y="98" fill="currentColor" text-anchor="middle" font-size="12">bit7 = 쓰기 안 닿음</text>
  <line x1="130" y1="80" x2="330" y2="80" stroke="currentColor" stroke-width="2"/>
  <line x1="370" y1="80" x2="560" y2="80" stroke="currentColor" stroke-width="2"/>
  <text x="350" y="86" fill="currentColor" text-anchor="middle" font-weight="bold">✕</text>
  <text x="230" y="70" fill="currentColor" text-anchor="middle" font-size="12">D7 (CPU 쪽 조각)</text>
  <text x="350" y="112" fill="currentColor" text-anchor="middle" font-size="12">open</text>
  <rect x="150" y="92" width="160" height="26" rx="5" class="f-warn"/>
  <text x="230" y="110" fill="currentColor" text-anchor="middle" font-size="12">마지막 구동 값을 기억</text>
  <g fill="currentColor" font-family="monospace" font-size="12.5">
    <text x="20" y="150">walk1 step 7, decoy 없음:</text>
    <text x="40" y="172">write SCRATCH=0x80 → D7 조각 = 1</text>
    <text x="40" y="194">read  SCRATCH → D7 = 1 (기억된 값) == exp ⇒ PASS (놓침)</text>
    <text x="20" y="226">walk1 step 7, decoy 있음:</text>
    <text x="40" y="248">write SCRATCH=0x80, write SRC=~0x80 → D7 조각 = 0</text>
    <text x="40" y="270">read  SCRATCH → D7 = 0 != exp ⇒ FAIL (잡음)</text>
  </g>
  <rect x="520" y="140" width="210" height="140" rx="8" fill="none" stroke="currentColor"/>
  <g fill="currentColor" font-size="12.5">
    <text x="532" y="162">open 선의 증상 (decoy 사용):</text>
    <text x="532" y="186">모든 단계에서 "쓴 값의</text>
    <text x="532" y="206">반대"가 읽힘 → rise·fall</text>
    <text x="532" y="226">둘 다 같은 비트 → OPEN</text>
    <text x="532" y="254">decoy 없음 → walk1·walk0</text>
    <text x="532" y="274">모두 0단계 실패</text>
  </g>
</svg>
```

보드 수준의 병렬 버스(외부 SRAM/NOR, FMC/EMC 같은 메모리 컨트롤러 버스)에서 끊긴 선은 아무도 구동하지 않는다. 그 선은 기생 용량에 남은 전하나 bus-hold(bus keeper) 회로 때문에 **마지막으로 구동된 레벨**을 한동안 유지한다. "쓰고 바로 같은 주소를 읽는" 테스트는 그 레벨을 다시 읽어 **통과**해 버린다. 대책은 write 와 readback 사이에 **다른 주소에 반대 패턴을 써서 버스를 반대로 흔드는 것**이다 (이 코드의 decoy). 테스트 `walk_bus_fault(&open7, WALK1, NO_DECOY).nfail == 0` 과 `... 0x008).nfail == 32` 가 이 차이를 고정한다.

> 모델 단순화: mock 은 "CPU 쪽 선 조각이 마지막 write/read 값을 그대로 기억" 하는 이상화다. 실제로는 시간이 지나면 pull 저항 쪽으로 흘러가므로 결과가 간헐적일 수 있다 — 간헐적 데이터 버스 실패가 보이면 open 을 의심하라. 칩 내부(on-chip AXI/APB) 데이터 경로에는 "떠 있는" 선이 거의 없다 — 연결이 빠지면 합성 단계에서 상수로 묶여 stuck-at 처럼 보인다.

**④ 증상으로 판정 (`classify`).** 단일 결함을 가정하면 네 비트맵(walk1/walk0 의 rise/fall)과 실패 단계 수로 결함이 갈린다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="sivb05-arr2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">classify(w1, w0): bad = rise | fall (두 walk 합)</text>
  <rect x="270" y="38" width="200" height="34" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="370" y="60" fill="currentColor" text-anchor="middle">bad 비트 수 = ?</text>
  <path d="M300 72 L120 112" stroke="currentColor" fill="none" marker-end="url(#sivb05-arr2)"/>
  <path d="M370 72 L370 112" stroke="currentColor" fill="none" marker-end="url(#sivb05-arr2)"/>
  <path d="M440 72 L610 112" stroke="currentColor" fill="none" marker-end="url(#sivb05-arr2)"/>
  <text x="190" y="92" fill="currentColor" font-size="12">0</text>
  <text x="380" y="98" fill="currentColor" font-size="12">1</text>
  <text x="540" y="92" fill="currentColor" font-size="12">2</text>
  <rect x="50" y="114" width="140" height="32" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="120" y="135" fill="currentColor" text-anchor="middle">PASS</text>
  <rect x="270" y="114" width="200" height="104" rx="6" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-size="12.5">
    <text x="282" y="136">fall 만 → SA0</text>
    <text x="282" y="158">rise 만 → SA1</text>
    <text x="282" y="180">둘 다, 4개 비트맵 전부</text>
    <text x="282" y="200">  같은 비트 → OPEN</text>
  </g>
  <rect x="500" y="114" width="230" height="104" rx="6" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-size="12.5">
    <text x="512" y="136">rise 만 &amp; walk1 실패 단계</text>
    <text x="512" y="156">  = 그 두 비트 → OR_SHORT</text>
    <text x="512" y="182">fall 만 &amp; walk0 실패 단계</text>
    <text x="512" y="202">  = 그 두 비트 → AND_SHORT</text>
  </g>
  <rect x="20" y="240" width="710" height="76" rx="8" class="f-warn"/>
  <g fill="currentColor" font-size="12.5">
    <text x="32" y="262">함정: 이중 SA0(D7, D8)도 fall = {7,8} 이다. 하지만 walk0 에서는 D7, D8 이 늘 1이어야 하므로</text>
    <text x="32" y="282">32단계 전부 실패한다 → AND_SHORT 아님 → UNKNOWN. AND-short 는 두 선이 다를 때(2단계)만 틀린다.</text>
    <text x="32" y="304">walk1 만 돌리면 이 둘을 구분할 수 없다 — walk0 이 "진단" 에 필요한 이유.</text>
  </g>
</svg>
```

판정 규칙을 표로:

| 결함 | walk1 실패 단계 | walk0 실패 단계 | rise | fall | 판정 |
|---|---|---|---|---|---|
| SA0 k | {k} | mask − {k} | — | {k} | SA0 |
| SA1 k | mask − {k} | {k} | {k} | — | SA1 |
| OR a–b | {a,b} | {a,b} | {a,b} | — | OR_SHORT |
| 이중 SA1 a,b | **전부** | {a,b} | {a,b} | — | UNKNOWN (walk1 이 가른다) |
| AND a–b | {a,b} | {a,b} | — | {a,b} | AND_SHORT |
| 이중 SA0 a,b | {a,b} | **전부** | — | {a,b} | UNKNOWN (walk0 이 가른다) |
| OPEN k (+decoy) | 전부 | 전부 | {k} | {k} | OPEN |

OPEN 과 같은 증상("늘 쓴 값의 반대")을 내는 다른 결함이 하나 더 있다: **write 경로에서만 반전된 비트**(RTL 에서 `~` 하나 잘못 붙음). 둘은 decoy 없이 한 번 더 돌리면 갈린다 — 반전은 decoy 없이도 틀리고, open 은 decoy 없이는 통과한다 (추가 연습 4).

**⑤ 결함 × 패턴 매트릭스.** `detects()` 로 실제로 돌린 결과 (solution 실행 출력 그대로):

```text
fault        0/~0        55/AA       walk1       walk0       w1+w0+shake
SA0 D7       detect      detect      detect      detect      detect     
SA1 D7       detect      detect      detect      detect      detect     
OR  D7-D8    -           detect      detect      detect      detect     
AND D7-D8    -           detect      detect      detect      detect     
OR  D0-D2    -           -           detect      detect      detect     
AND D0-D2    -           -           detect      detect      detect     
OPEN D7      -           -           -           -           detect     
```

- **0/~0** (2번 쓰기): stuck-at 만. 모든 선이 같은 값이라 short 는 절대 안 보인다.
- **55/AA 체커보드** (2번): 이웃 비트는 늘 다르므로 **비트 번호상 인접한** short 는 잡지만, D0–D2 처럼 홀짝이 같은 쌍은 두 패턴 모두에서 같은 값 → 놓친다. PCB 에서 물리적으로 붙어 있는 선이 비트 번호상 이웃이라는 보장은 없다 (BGA 볼 배치, 레이어 전환). 32비트 기준 496쌍 중 240쌍을 놓친다 (추가 연습 3).
- **walking** (32번): 모든 쌍을 잡는다. 검출만 보면 walk1 하나로 충분하다.
- **counting 패턴 + 보수** (10번 = 2·log2 32): 비트 i 의 값을 "i 의 이진수 j번째 자리" 로 정한 5개 패턴과 그 보수. 서로 다른 두 비트는 인덱스의 어느 자리에서는 다르므로 모든 쌍 short 를 잡는다. 보드의 boundary-scan 상호연결(interconnect) 테스트가 이런 계열의 코드를 쓴다. 대신 실패 시 어떤 쌍인지 가르는 해상도는 walking 보다 낮다.
- **open** 은 decoy 로 흔들 때만 보인다.

### Part C — 블록 전체를 걸고, 위치를 추정한다

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">같은 비트가 어디서 틀리나 → 공통 경로(버스) vs 레지스터 하나(flop)</text>
  <rect x="20" y="110" width="90" height="60" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="65" y="145" fill="currentColor" text-anchor="middle" font-weight="bold">CPU</text>
  <line x1="110" y1="140" x2="230" y2="140" stroke="currentColor" stroke-width="3"/>
  <text x="170" y="130" fill="currentColor" text-anchor="middle" font-size="12">D[31:0]</text>
  <rect x="140" y="146" width="70" height="22" rx="4" class="f-warn"/>
  <text x="175" y="162" fill="currentColor" text-anchor="middle" font-size="12">bus 결함</text>
  <line x1="230" y1="50" x2="230" y2="270" stroke="currentColor" stroke-width="3"/>
  <g stroke="currentColor" stroke-width="1.5">
    <line x1="230" y1="60" x2="300" y2="60"/><line x1="230" y1="100" x2="300" y2="100"/>
    <line x1="230" y1="140" x2="300" y2="140"/><line x1="230" y1="180" x2="300" y2="180"/>
    <line x1="230" y1="220" x2="300" y2="220"/><line x1="230" y1="260" x2="300" y2="260"/>
  </g>
  <g stroke="currentColor">
    <rect x="300" y="46" width="130" height="28" rx="4" class="f-mut"/>
    <rect x="300" y="86" width="130" height="28" rx="4" class="f-mut"/>
    <rect x="300" y="126" width="130" height="28" rx="4" class="f-mut"/>
    <rect x="300" y="166" width="130" height="28" rx="4" class="f-mut"/>
    <rect x="300" y="206" width="130" height="28" rx="4" class="f-mut"/>
    <rect x="300" y="246" width="130" height="28" rx="4" class="f-mut"/>
  </g>
  <rect x="402" y="90" width="24" height="20" rx="3" class="f-warn"/>
  <g fill="currentColor" font-size="12.5">
    <text x="310" y="65">SCRATCH  ffffffff</text>
    <text x="310" y="105">CTRL     0000fffc</text>
    <text x="310" y="145">SRC      fffffffc</text>
    <text x="310" y="185">DST      fffffffc</text>
    <text x="310" y="225">LEN      00ffffff</text>
    <text x="310" y="265">IRQ_EN   0000001f</text>
  </g>
  <g fill="currentColor" font-size="12.5">
    <text x="460" y="62">bus OR D7–D8 → 5개 레지스터에서 [7,8]</text>
    <text x="460" y="82">  (IRQ_EN 은 bit7,8 을 안 덮음)</text>
    <text x="460" y="102">  ⇒ SUSPECT bus lines=[7,8]</text>
    <text x="460" y="140">CTRL flop bit3 SA1 → CTRL 에서만 [3]</text>
    <text x="460" y="160">  ⇒ SUSPECT reg=CTRL bits=[3]</text>
    <text x="460" y="198">규칙: hits[b] ≥ 2 → bus</text>
    <text x="460" y="218">      hits[b] = 1 → 그 레지스터</text>
    <text x="460" y="256">STATUS·ID 는 RO → walk 안 함</text>
    <text x="460" y="276">coverage = walk_mask 들의 OR</text>
  </g>
</svg>
```

검증 엔지니어가 받은 리포트로 바로 다음 행동을 정할 수 있어야 한다. solution 이 찍은 실제 리포트:

```text
---- report: bus OR-short D7-D8 ----
FAIL reg=SCRATCH off=0x000 test=walk1 step=7 exp=0x00000080 act=0x00000180 diff=0x00000100 bits=[8] (+1 more steps)
FAIL reg=SCRATCH off=0x000 test=walk0 step=7 exp=0xFFFFFF7F act=0xFFFFFFFF diff=0x00000080 bits=[7] (+1 more steps)
DIAG reg=SCRATCH class=OR_SHORT bits=[7,8]
FAIL reg=CTRL off=0x004 test=walk1 step=7 exp=0x00000080 act=0x00000180 diff=0x00000100 bits=[8] (+1 more steps)
FAIL reg=CTRL off=0x004 test=walk0 step=7 exp=0x0000FF7C act=0x0000FFFC diff=0x00000080 bits=[7] (+1 more steps)
DIAG reg=CTRL class=OR_SHORT bits=[7,8]
  ... (SRC, DST, LEN 도 같은 모양) ...
SUSPECT bus lines=[7,8] (seen in >=2 registers: common data path)
RESULT FAIL failed_regs=5 failed_steps=20 coverage=0xFFFFFFFF
---- report: CTRL flop bit3 stuck-at-1 ----
FAIL reg=CTRL off=0x004 test=walk1 step=2 exp=0x00000004 act=0x0000000C diff=0x00000008 bits=[3] (+12 more steps)
FAIL reg=CTRL off=0x004 test=walk0 step=3 exp=0x0000FFF4 act=0x0000FFFC diff=0x00000008 bits=[3]
DIAG reg=CTRL class=SA1 bits=[3]
SUSPECT reg=CTRL bits=[3] (only this register: flop / write path)
RESULT FAIL failed_regs=1 failed_steps=14 coverage=0xFFFFFFFF
```

- 첫 리포트를 받은 사람은 **보드/패키지 쪽** (D7·D8 볼 주변 솔더 브리지, 또는 SoC 인터커넥트의 데이터 경로)을 본다. 다섯 레지스터가 모두 같은 두 비트를 말하므로 IP 내부 문제일 확률은 낮다.
- 두 번째는 **CTRL 하나**의 문제다: 그 레지스터의 flop, write-enable, 또는 RTL 에서 bit3 이 상수에 묶였는지를 본다 (RTL 리뷰 / 시뮬레이션 재현으로 넘길 버그).
- CTRL walk1 의 첫 실패가 step 2 인 이유: walk_mask 가 `0xFFFC` 라 step 0, 1 은 건너뛴다. 출력의 `step=` 은 "비트 번호" 이지 "몇 번째 실행" 이 아니다 — 리포트에서 이 둘을 섞으면 사람이 헷갈린다.
- 버스 open D5 시나리오는 `class=OPEN bits=[5]` 가 5개 레지스터에서 나오고 `SUSPECT bus lines=[5]` 로 끝난다. 같은 보드에서 decoy 를 끄면 `RESULT PASS` — 테스트 설계가 결함을 숨긴 셈이다.

위치 추정의 한계 (리포트를 읽는 사람이 알아야 할 것):

- **단일 결함 가정**이다. 서로 다른 레지스터 두 개에 우연히 같은 비트 결함이 있으면 버스로 오판한다.
- 버스로 판정돼도 그 "버스" 가 CPU–인터커넥트 구간인지, 이 IP 앞의 APB 브리지인지는 아직 모른다. 다음 단계는 **다른 IP 의 SCRATCH 를 같은 방법으로** 걸어 보는 것 (같은 비트가 다른 IP 에서도 틀리면 더 위쪽 공통 경로), 그리고 **다른 마스터(DMA, JTAG 디버그 접근 포트)** 로 같은 레지스터에 접근해 CPU 경로를 배제하는 것이다.
- 64→32비트 다운사이저 같은 버스 폭 변환 브리지는 **byte-lane** 을 잘못 이으면 비트 매핑 자체가 틀린다. 대칭으로 틀리면 readback 으로는 안 보이고, 값이 정해진 RO 레지스터(ID)로 잡는다 (추가 연습 1).

### 실무 연결: 누가, 언제, 어떻게 돌리나

| 단계 | 같은 아이디어의 형태 |
|---|---|
| Pre-silicon (RTL 시뮬) | UVM RAL 의 `uvm_reg_bit_bash_seq` — RW 필드의 각 비트에 1과 0을 차례로 써서 access policy 대로 set/clear 되는지 확인. 레지스터 맵은 SystemRDL / IP-XACT 에서 생성 |
| 생산 테스트 (ATE) | 칩 내부 stuck-at 은 scan/ATPG 구조 테스트가 다이마다 잡는다. 기능 레지스터를 walking 으로 하나하나 걷는 건 주로 ATE 의 몫이 아니다 |
| Post-silicon 검증 / 보드 bring-up | 이 레슨의 C 코드. on-chip 펌웨어로 돌리거나 JTAG/OpenOCD (`mww`/`mdw`) 로 호스트에서 레지스터를 읽고 쓴다. 잡는 것: **설계·통합 버그**(비트 매핑, tie-off, write-enable)와 **보드 조립 결함**(외부 메모리 버스 short/open) |
| SLT / 양산 보드 테스트 | 외부 메모리 데이터 버스 테스트는 보드 단위 자가진단(POST)에 그대로 들어간다 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 매트릭스 + 리포트 3개 출력, "50/50 passed"
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san   # 경고 없이 50/50
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| WALK0 패턴을 `~(1u << i)` 로 | mask 밖 RO/예약 비트에 1을 쓰고, 기대값도 mask 와 어긋나 RO 비트마다 거짓 실패 | `mask & ~(1u << i)` + 비교는 `& mask` |
| 패턴만 쓰기: `bus_write(off, exp)` | mask 밖 EN/START 비트를 0으로 덮어 테스트 중 장치를 멈추거나 시작시킴 | `bus_write(off, (saved & ~mask) \| exp)` |
| 끝나고 복구 안 함 | 다음 테스트(혹은 실제 동작)가 walk 의 마지막 패턴 위에서 돈다 | 시작 시 `saved`, 끝에 `bus_write(off, saved)` (decoy 도) |
| write 직후 같은 주소 readback 만 | 끊긴 선이 bus-hold 로 통과 — "PASS" 가 거짓말 | write 와 read 사이에 다른 주소에 `~패턴` (decoy) |
| `act == exp` 만 보고 bool 반환 | 어떤 비트가 0→1 인지 1→0 인지 사라져 SA0/SA1/short 를 못 가림 | `rise = act & ~exp`, `fall = exp & ~act` 누적 |
| walk1 만 돌리고 "short 없음" 판정 | 검출은 되지만 AND-short 와 이중 SA0 을 구분 못 함 | walk1 + walk0 둘 다, 실패 단계 수까지 비교 |
| 55/AA 로 short 테스트 끝 | 홀짝이 같은 쌍(D0–D2)을 영영 못 봄 | walking 또는 counting+보수 패턴 |
| 리포트에 "FAIL" 한 줄 | 디버그 엔지니어가 재현부터 다시 해야 함 | reg, off, test, step, exp, act, diff, bits, 반복 횟수 |
| W1C 상태 비트까지 walk | 1을 쓰면 지워지고 0은 무시 → 거짓 실패, 진짜 이벤트도 지움 | walk_mask 에서 제외, SIV-B04/B11 방식으로 |

## 8. 면접 Q&A

<details>
<summary>Q1. 데이터 버스 테스트를 짜 보세요. walking 1s 만 하면 되나요, walking 0s 는 왜 하나요?</summary>

한 주소에 `1<<i` 를 i=0..31 로 쓰고 읽어 비교 (Barr `memTestDataBus` 형태), 실패하면 첫 실패 패턴과 diff 를 남긴다. **검출만 보면 walk1 하나로 stuck-at 과 모든 쌍 short 를 잡는다** — 어떤 두 비트든 어느 단계에선 값이 다르기 때문이다. walk0 은 **진단** 에 필요하다: AND-short 와 이중 SA0 은 walk1 에서 똑같이 보이지만 walk0 에선 실패 단계 수가 2 vs 32 로 갈린다 (OR-short vs 이중 SA1 은 그 반대). 또 walk0 은 31개 선이 동시에 1 → 동시 스위칭(SSO)·그라운드 바운스 쪽 스트레스가 walk1 과 다르다.

꼬리질문: "쓰기 횟수를 최소로 하라면?" → stuck-at 만이면 0/~0 두 번. 인접 short 까지는 55/AA 두 번 (홀짝 같은 쌍은 놓침). 모든 쌍 short 까지는 counting 패턴 + 보수 2·log2(N) = 32비트에서 10번. 대신 실패한 쌍을 정확히 집는 해상도는 walking 이 가장 좋다.

</details>

<details>
<summary>Q2. wired-OR 와 wired-AND short 는 물리적으로 무엇이고, 테스트에서 어떻게 다르게 보이나요?</summary>

두 선이 붙으면 둘을 구동하는 드라이버가 서로 싸운다. 어느 레벨이 이기는지는 드라이버 세기와 기술에 따라 다르다 — 1이 이기면 OR, 0이 이기면 AND 로 모델링한다 (실제로는 중간 전압이 되어 받는 쪽 문턱에 따라 갈리는 저항성 브리지도 있다). walk1 에서 OR 은 걸은 1이 이웃으로 **번지고**(rise), AND 는 걸은 1이 **꺼진다**(fall). 리포트의 diff 비트가 OR 이면 이웃 비트, AND 면 걸은 비트 자신이다.

꼬리질문: "인접 비트 short 만 보면 되나?" → 비트 번호의 이웃과 물리적 이웃은 다르다. BGA 볼 배치·라우팅에서 D3 옆이 D11 일 수 있다. 그래서 모든 쌍을 덮는 패턴을 쓴다.

</details>

<details>
<summary>Q3. walking 테스트가 PASS 인데 데이터 선이 끊겨 있을 수 있나요?</summary>

있다. 끊긴 선은 아무도 구동하지 않아서 기생 용량이나 bus-hold 회로가 **마지막으로 구동된 레벨**을 유지한다. "쓰고 바로 같은 주소를 읽으면" 방금 CPU 가 쓴 값이 그대로 읽혀 통과한다. 대책: write 와 readback 사이에 다른 주소에 반대 패턴을 쓰거나(decoy), 반대 값을 가진 다른 위치를 읽어 버스 레벨을 뒤집어 놓는다. 이 레슨 mock 에서 decoy 없이는 open D7 이 0단계 실패, decoy 를 쓰면 32단계 모두 실패한다.

꼬리질문: "on-chip 레지스터 경로에도 해당되나?" → 칩 내부 버스는 항상 어떤 로직이 구동하므로 '떠 있는' 상태는 드물다. 연결 누락은 대개 상수 tie-off → stuck-at 으로 나타난다. open/bus-hold 는 보드 수준 병렬 버스(외부 SRAM/NOR/FMC)의 이야기다.

</details>

<details>
<summary>Q4. IP 의 모든 레지스터에 bit-walk 를 걸 때 무엇을 조심하나요? "bit-bash 가 W1C 를 망가뜨리지 않으려면?"</summary>

① **walk_mask**: RW 이면서 부작용 없는 비트만. EN/START/SW_RESET, W1C/RC/WO, 상태 비트는 제외. UVM 에서도 `NO_REG_BIT_BASH_TEST` 속성으로 레지스터를 뺀다. ② mask 밖 비트는 **읽어 둔 값을 계속 쓴다** (`(saved & ~mask) | p`). ③ 끝나면 **복구**. ④ 순서: 클럭·리셋이 풀린 뒤(SIV-B10), 리셋값 확인(SIV-B03) 다음에. W1C 는 "1을 쓰면 0이 된다" 가 기대값이므로 walking 대상이 아니라 access-type 별 기대값 테스트(SIV-B04)에서, 상태 비트를 세우는 방법(force/test 레지스터)과 함께 다룬다(SIV-B11).

꼬리질문: "walk 중 클럭 게이트된 블록을 건드리면?" → 읽기가 0 이나 0xDEADBEEF 같은 고정값, 혹은 버스 에러(SLVERR)로 돌아온다. 모든 비트가 같은 값으로 틀리면 데이터 선이 아니라 **블록이 응답을 안 하는 것** 이다 — 이 판단을 리포트 앞단에 둔다.

</details>

<details>
<summary>Q5. 실패가 났을 때 버스 문제인지 레지스터 문제인지 어떻게 가르나요?</summary>

같은 비트가 **여러 레지스터에서** 틀리면 공통 경로(버스·브리지·패키지), **한 레지스터에서만** 틀리면 그 레지스터의 flop/write-enable/RTL 연결이다. 이 레슨의 `block_walk` 가 비트별 히트 수로 이렇게 판정한다. 범위를 더 좁히려면 다른 IP 의 SCRATCH 로 같은 테스트 (인터커넥트 상위 공통 경로인지), 다른 마스터(DMA, 디버그 AP)로 같은 레지스터 접근 (CPU 쪽 경로 배제), 그리고 걸린 비트의 커버리지(어느 레지스터가 그 비트를 덮나)를 확인한다.

꼬리질문: "그 비트를 덮는 레지스터가 하나뿐이면?" → 버스/레지스터를 가를 수 없다. 그래서 32비트 전부 RW 인 SCRATCH 레지스터가 IP 마다 있으면 좋다 — 검증팀이 설계팀에 요구하는 DFT/DFV 항목이다.

</details>

<details>
<summary>Q6. 테스트가 실패했을 때 로그에 무엇을 남기나요? 간헐적으로만 실패하면?</summary>

reg 이름, 오프셋(+base), 테스트 종류, 단계(=걸은 비트), exp, act, diff, 틀린 비트 목록, 실패 반복 횟수, 그리고 헤더에 보드 ID·실리콘 리비전·FW 버전·전압/온도·시드. 리포트를 받은 사람이 재현하지 않고도 "어느 선, 어느 방향(0→1/1→0)" 을 알아야 한다 (SIV-B08 JSON 로그, SIV-B14 리포트).

꼬리질문: 간헐적이면 같은 패턴을 N번 반복해 실패 확률을 재고, 전압·온도·주파수를 바꿔(shmoo) 마진 문제인지 본다. 특정 비트가 저전압에서만 틀리면 타이밍/신호 무결성 마진, 시간 지나야 틀리면 open(전하가 새는 bus-hold) 쪽을 의심한다.

</details>

<details>
<summary>Q7. 생산 테스트(ATE)의 scan 이 이미 stuck-at 을 잡는데, post-silicon 에서 또 walking 을 왜 하나요?</summary>

목적이 다르다. ATE 의 scan/ATPG 는 **제조 결함**(다이마다 생기는 내부 net 의 stuck-at 등)을 구조적으로 잡는다. post-silicon 검증의 walking 은 **설계·통합 버그**(비트 매핑, 잘못된 tie-off, write-enable 누락 — 모든 칩에 똑같이 있는 버그)와 **보드 조립 결함**(외부 버스 short/open)을 기능 경로로 확인한다. 설계 버그는 scan 이 "정상"으로 볼 수 있다 — 설계대로 만들어졌으니까. Pre-silicon 의 `uvm_reg_bit_bash_seq` 가 같은 확인을 시뮬레이션에서 먼저 하고, post-silicon 에서는 같은 레지스터 기술서로 C 테스트를 재사용한다 (SIV-B13).

</details>

## 9. 추가 연습

### 연습 1 — walking-1 로 비트 매핑 복원

write 경로의 비트 연결이 틀린 칩(RTL 통합 실수, 버스 폭 변환 브리지의 byte-lane 역순)이 있다. walking-1 의 readback 으로 "CPU 비트 i 가 레지스터 비트 몇 번으로 갔나" `map[i]` 를 복원하라. stuck/short 이면 -1. 그리고 write 와 read 경로가 **똑같이**(대칭으로) 뒤바뀐 경우는 readback 으로 보이는가?

<details>
<summary>답 보기</summary>

`pw(1<<i); r = pr();` 에서 r 의 1이 정확히 하나면 `map[i] = lowbit(r)`, 아니면 -1. byte-lane 역순이면 32비트 모두 이동(`map[0]=24`, `map[31]=7`), D7↔D8 이면 2비트. **대칭 swap 은 readback 으로 안 보인다** — 쓸 때 7→8, 읽을 때 8→7 로 되돌아오기 때문. 값이 고정된 RO 레지스터(ID `0xD3A00105`)를 읽으면 `0xD3A00085` 로 틀려서 잡힌다. 그래서 bring-up 의 첫 읽기가 ID 레지스터다.

</details>

### 연습 2 — Barr 스타일 data bus test

`uint32_t data_bus_test()` : 한 주소에 walking 1 을 쓰고 읽어 PASS 면 0, 아니면 첫 실패 패턴을 돌려준다. SA1 D3 이면 무엇이 반환되나?

<details>
<summary>답 보기</summary>

`for (p = 1; p != 0; p <<= 1) { write(p); if (read() != p) return p; } return 0;` — `p <<= 1` 은 unsigned 라 비트 31 다음에 0이 되어 루프가 끝난다 (C-B03). SA1 D3 이면 첫 패턴 `0x1` 부터 bit3 이 1로 읽혀 `0x1` 이 반환된다. 즉 반환값은 "고장 난 선" 이 아니라 "처음 틀린 패턴" 이다 — 선을 알려면 readback 과의 XOR 을 로그에 남겨야 한다.

</details>

### 연습 3 — 패턴 세트 커버리지를 직접 센다

32비트의 모든 쌍 short (496쌍 × OR/AND = 992개)와 stuck-at (32 × 2 = 64개) 중 각 패턴 세트가 몇 개를 놓치는지 세라: 0/~0, 55/AA, counting(비트 i = i 의 j번째 이진 자리, j=0..4), counting+보수, walk1.

<details>
<summary>답 보기</summary>

practice.c 실제 출력:

```text
pattern set      #pat  short-miss/992  stuck-miss/64
0/~0                2             992              0
55/AA               2             480              0
counting            5               0              2
counting+compl     10               0              0
walk1              32               0              0
```

55/AA 는 홀짝이 같은 쌍 (짝수 16개 중 2개: 120쌍, 홀수 120쌍) × OR/AND = 480 을 놓친다. counting 5개만이면 short 는 다 잡지만 bit0 은 늘 0이라 SA0 을, bit31 은 늘 1이라 SA1 을 놓친다 → 보수를 더하면 해결 (10번).

</details>

### 연습 4 — "쓴 값의 반대가 읽힌다": 반전인가, 끊긴 선인가

decoy 를 쓴 walk 에서 어떤 비트가 늘 반대로 읽힌다. write 경로에만 `~` 가 붙은 RTL 버그(INVERT_W)와 open 을 구분하는 함수를 써라.

<details>
<summary>답 보기</summary>

decoy **없이** 한 번 더 걷는다. 반전은 flop 에 이미 반대 값이 저장돼 있어서 decoy 없이도 32단계 전부 틀린다 → `INVERT_W`. open 은 decoy 없이는 bus-hold 로 통과하고 decoy 가 있어야 틀린다 → `OPEN`. 참고로 write 와 read 경로가 둘 다 반전되면 readback 으로는 상쇄되어 안 보인다 (연습 1의 대칭 swap 과 같은 이유) — 기능 동작이나 RO 상수로만 잡힌다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 커버리지 표 출력, "21/21 passed"
```

## 10. 핵심 정리

- walking-1 = 0 사이에 1 하나, walking-0 = 1 사이에 0 하나. 한 단계에 한 선만 튀므로 **틀린 선이 바로 보인다**.
- 검출: walk1 하나로도 stuck-at + 모든 쌍 short. 진단: walk1 + walk0 의 **rise/fall 비트맵과 실패 단계 수**로 SA0/SA1/OR/AND/OPEN 을 가른다 (이중 SA0 ≠ AND-short).
- 패턴 비용: 0/~0(2) → stuck-at 만, 55/AA(2) → 인접 short 까지(홀짝 같은 쌍 놓침), counting+보수(2·log2N=10) → 모든 쌍, walking(32/64) → 모든 쌍 + 최고 해상도.
- **walk_mask**: RW 이면서 부작용 없는 비트만 걷고, mask 밖은 저장값 유지, 끝나면 save/restore.
- **bus-hold**: write 직후 같은 주소 readback 은 끊긴 선을 놓친다 → 다른 주소에 반대 패턴(decoy)으로 버스를 흔든다.
- 위치 추정: 같은 비트가 2개 이상 레지스터에서 → 공통 경로(버스), 1개 → 그 레지스터. coverage(걸어 본 선)를 같이 보고한다.
- 대칭 swap/반전은 readback 으로 안 보인다 → 값이 정해진 RO 레지스터(ID)로 확인.
- 리포트 한 줄 = reg, off, test, step, exp, act, diff, bits, 반복 횟수.

## 참고자료

- Michael Barr, "Fast Accurate Memory Test Code in C" — `memTestDataBus` (walking 1s), `memTestAddressBus`, `memTestDevice`: <https://barrgroup.com/embedded-systems/how-to/memory-test-suite-c>
- Accellera UVM 1.2 Class Reference — Register Layer built-in sequences (`uvm_reg_hw_reset_seq`, `uvm_reg_bit_bash_seq`, `uvm_reg_access_seq`, `uvm_mem_walk_seq`): <https://www.accellera.org/downloads/standards/uvm>
- Accellera SystemRDL 2.0 / IEEE 1685 (IP-XACT) — 레지스터 기술서 표준 (SIV-B01 참고): <https://www.accellera.org/downloads/standards/systemrdl>
- A. J. van de Goor, *Testing Semiconductor Memories: Theory and Practice* — stuck-at / bridging / coupling fault 모델 (SIV-B07 March 테스트와 같이)
- IEEE 1149.1 (JTAG boundary-scan) — 보드 상호연결(interconnect) 테스트와 counting 계열 패턴의 배경
- OpenOCD User's Guide — `mdw` / `mww` 메모리·레지스터 접근 명령: <https://openocd.org/doc/html/General-Commands.html>
- 관련 레슨: SIV-B02 (mock MMIO), SIV-B03 (리셋값), SIV-B04 (access type 별 R/W), SIV-B06 (주소 디코드), SIV-B07 (March C-), SIV-B08/B14 (로그·리포트), C-B03 (시프트), C-M01 (MMIO volatile)
