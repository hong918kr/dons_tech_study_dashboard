---
id: ARCH-M09
time: 90분
updated: 2026-11-11
---
# ARCH-M09 인터럽트 컨트롤러 모델: 우선순위·마스킹·Pending

## 학습 목표

- NVIC 와 비슷한 컨트롤러를 **IRQ 마다 enable / pending / active 세 비트 + 8비트 우선순위** 로 모델링하고, 세 비트가 왜 따로 있어야 하는지(꺼져 있어도 pending 은 남고, active 중에도 다시 pending 이 된다) 설명할 수 있다.
- 구현 비트 수(implemented bits)와 PRIGROUP 으로 우선순위 바이트를 **그룹(선점 결정) / 서브(대기 순서만)** 로 나누고, "실행 우선순위(execution priority) = active 그룹 최솟값 ∧ BASEPRI ∧ PRIMASK" 로 선점 여부를 계산한다.
- 이산 사건(discrete-event) 시뮬레이터로 **선점·중첩, tail-chaining, late arrival** 을 구현하고, 실행 로그를 기대 문자열과 글자 단위로 비교해 증명한다.
- RTOS 임계 구역을 PRIMASK 대신 BASEPRI 로 걸면 왜 모터 제어 IRQ 지연이 92 → 12 사이클로 줄어드는지, 연속 IRQ k 개에서 tail-chaining 이 아끼는 사이클을 식으로 말할 수 있다.

## 1. 문제

"우선순위가 같은 두 인터럽트 중 누가 먼저?", "서브 우선순위는 왜 선점을 못 하죠?", "tail-chaining 이 정확히 무엇을 아끼나요?", "FreeRTOS 는 왜 PRIMASK 가 아니라 BASEPRI 를 쓰죠?" — Cortex-M 펌웨어·실리콘 검증 면접의 단골 질문이고, 답은 전부 **인터럽트 컨트롤러의 상태 기계 하나** 에서 나온다. 모터 제어 루프의 지터, UART 바이트 유실, "ISR 이 두 번 돈다" 같은 현장 버그도 같은 모델로 설명된다.

이 레슨은 **NVIC 와 비슷한(NVIC-like)** 모델을 C 로 만든다. 레지스터 이름·비트 위치(ISER/ISPR/IPR …)는 BM-M01, 스택 프레임과 EXC_RETURN 은 ARM-M09 의 몫이라 여기서는 **동작 규칙** 만 본다. 고정 우선순위 승자 고르기(우선순위 인코더)는 ARCH-B05 에서 이미 했다 — 여기서는 우선순위가 **프로그래머블** 하고, 시간이 흐르고, 코어가 선점된다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m09a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">우선순위 바이트 (구현 4비트, PRIGROUP = 5) 와 모델의 데이터 흐름</text>
  <g stroke="currentColor">
    <rect x="40" y="34" width="50" height="30" class="f-acc"/><rect x="90" y="34" width="50" height="30" class="f-acc"/>
    <rect x="140" y="34" width="50" height="30" class="f-ok"/><rect x="190" y="34" width="50" height="30" class="f-ok"/>
    <rect x="240" y="34" width="50" height="30" class="f-mut"/><rect x="290" y="34" width="50" height="30" class="f-mut"/>
    <rect x="340" y="34" width="50" height="30" class="f-mut"/><rect x="390" y="34" width="50" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="65" y="54">7</text><text x="115" y="54">6</text><text x="165" y="54">5</text><text x="215" y="54">4</text>
    <text x="265" y="54">3</text><text x="315" y="54">2</text><text x="365" y="54">1</text><text x="415" y="54">0</text>
    <text x="90" y="82">그룹 [7:6]</text><text x="190" y="82">서브 [5:4]</text><text x="340" y="82">미구현 → 0 으로 읽힘</text>
  </g>
  <text x="460" y="46" fill="currentColor" font-size="12">PRIGROUP p: 그룹 = [7:p+1], 서브 = [p:0]</text>
  <text x="460" y="64" fill="currentColor" font-size="12">값이 작을수록 급함 · 0x90 = 그룹 2, 서브 1</text>
  <g stroke="currentColor">
    <rect x="20" y="110" width="150" height="60" class="f-mut"/>
    <rect x="200" y="110" width="150" height="60" class="f-ok"/>
    <rect x="380" y="110" width="160" height="60" class="f-acc"/>
    <rect x="570" y="110" width="150" height="60" class="f-warn"/>
    <rect x="290" y="210" width="270" height="76" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="95" y="134">IRQ 사건</text><text x="95" y="154">도착 · 핸들러 setpend</text>
    <text x="275" y="134">pending AND enabled</text><text x="275" y="154">(32비트 마스크)</text>
    <text x="460" y="134">승자 = (값, 번호) 최소</text><text x="460" y="154">pick_pending</text>
    <text x="645" y="128">group(승자) &lt;</text><text x="645" y="146">실행 우선순위 ?</text><text x="645" y="162">→ entry / tail / late</text>
    <text x="425" y="232">실행 우선순위 =</text><text x="425" y="252">min(active 들의 그룹, group(BASEPRI))</text><text x="425" y="272">PRIMASK=1 이면 0 · 없으면 256</text>
  </g>
  <g stroke="currentColor" fill="none">
    <line x1="170" y1="140" x2="198" y2="140" marker-end="url(#m09a)"/>
    <line x1="350" y1="140" x2="378" y2="140" marker-end="url(#m09a)"/>
    <line x1="540" y1="140" x2="568" y2="140" marker-end="url(#m09a)"/>
    <polyline points="560,248 645,248 645,172" marker-end="url(#m09a)"/>
  </g>
  <text x="20" y="316" fill="currentColor" font-size="12">선점은 그룹만 본다 (엄격히 작아야). 서브와 번호는 "여럿이 기다릴 때 누가 먼저" 만 정한다.</text>
</svg>
```

**모델 규칙** (starter.c 머리 주석과 같다):

| 항목 | 규칙 |
|---|---|
| 시간 | 정수 사이클. 같은 시각에서는 **외부 사건(도착·마스크 변경) 먼저**, 그다음 코어 상태 전이 |
| entry / tail / exit | 12 / 6 / 10 사이클 — Cortex-M3/M4, 0-wait 메모리에서 흔히 인용되는 값을 **모델 파라미터** 로 쓴 것 (`timing_t`) |
| pending → active | entry(또는 tail) 가 **끝나는 순간** pending 해제 + active 설정, 핸들러 시작. 지연(latency) = 시작 시각 − 처음 pending 된 시각 |
| 선점 | 대기 승자의 그룹 값 < 실행 우선순위 (엄격히). 실행 중인 문맥은 스택에 남는다 |
| late arrival | entry/tail 이 **아직 남았을 때(remaining > 0)** 그룹이 지금 목표보다 엄격히 급한 IRQ 가 오면 목표만 바꾼다 (쌓기 공유) |
| 핸들러 끝 | 승자가 돌아갈 문맥을 빼앗을 수 있으면 tail(6), 아니면 exit(10) 후 resume |
| exit 중 도착 | 본문 모델은 기다린다 (pop-preemption 은 추가 연습 2) |

로그는 `@시각 사건` 한 줄씩: `pend IRQ5` · `entry IRQ2 over IRQ7` · `late IRQ1 (was IRQ6)` · `start IRQ1` · `done IRQ1` · `tail IRQ6` · `exit` · `resume thread` · `IRQ2 setpend IRQ0` · `primask 1` · `basepri 0x40`.

### Part A — 우선순위 산수

- `prio_store(n, p)` — 구현된 상위 `prio_bits` 비트만 남긴다 (4비트면 0x5A → 0x50).
- `group_of(n, p)` — 서브 비트 `[prigroup:0]` 를 0 으로. PRIGROUP 7 이면 그룹 비트가 없다 → 0.
- `exec_prio(n)` — active 그룹 최솟값, `group(BASEPRI)` (≠0 일 때), PRIMASK → 0, 아무것도 없으면 256.
- `pick_pending(n)` — enabled & pending 중 (값, 번호) 최소, 없으면 −1. 무작위 10만 상태를 키 비교 정답과 대조.

### Part B — 선점 · 중첩 · tail-chaining · late arrival

시뮬레이터 루프(`settle`/`sim_run`)는 주어진다. 상태 기계의 결정 세 개를 채운다:

- `try_preempt(s)` — THREAD/HANDLER 에서 승자가 `can_take` 면 ENTRY 시작.
- `late_arrival(s)` — ENTRY/TAIL 진행 중 목표 교체.
- `handler_return(s)` — pop + active 해제 → TAIL 또는 EXIT (`tm.tailchain` 이 거짓이면 항상 EXIT).

시나리오 B1–B9 의 로그를 글자 단위로 비교하고 (선점, tail, tail 없는 코어, late arrival 과 그 경계, 같은 그룹은 late 아님, PRIGROUP 비교, 핸들러 set/clear pending, 자기 자신 재-pending, PRIMASK, disable 중 pending), B10 은 무작위 2000 시나리오에서 불변식(모두 처리됨, 시간 보존, 핸들러 사이클 = Σ 실행 횟수·길이)을 본다.

### Part C — 보드 설정, RTOS 임계 구역, 오버헤드

- `encode_prio(n, group, sub)` — (그룹, 서브) → 저장 값 (CMSIS `NVIC_EncodePriority` + 시프트를 합친 것).
- `fw_config(n)` — 구현 4비트 보드에서 그룹 2비트 + 서브 2비트, MOTOR / ADC / UART1 / UART2 / TICK 우선순위와 enable.
- `burst_overhead(tm, k)` — 연속 k 개 IRQ 의 entry/tail/exit 합계 (tail-chain 있음 / 없음).
- 테스트: BASEPRI 임계 구역 vs PRIMASK 임계 구역 지연 비교, 4단 중첩 로그, 퍼즈 3000개로 "중첩 깊이 ≤ 그룹 레벨 수, MOTOR 위에는 아무도, UART 끼리는 안 겹침".

## 2. Problem (English)

> **NVIC-like interrupt controller model.** 32 IRQs, each with enable/pending/active bits and an 8-bit priority (lower value = more urgent). Only the top `prio_bits` bits are implemented (others read as zero). `prigroup` (0–7) splits the byte: bits `[7:prigroup+1]` are the group (preemption) priority, bits `[prigroup:0]` the subpriority. PRIMASK=1 raises the execution priority to 0; a non-zero BASEPRI raises it to `group(BASEPRI)`.
>
> 1. Implement `prio_store`, `group_of`, `exec_prio` (minimum group priority of active IRQs, BASEPRI, PRIMASK; 256 when nothing is active) and `pick_pending` (lowest priority value among enabled & pending, ties → lowest IRQ number; −1 if none).
> 2. A discrete-event simulator (given) jumps between external events (arrival, enable/disable, PRIMASK/BASEPRI writes), the end of entry (12 cycles), tail-chain (6) and exit (10) phases, and handler milestones (a handler may set/clear other IRQs' pending bits at given offsets). Implement the three decisions: `try_preempt` (start entry if the winner's group priority is strictly below the execution priority), `late_arrival` (during entry/tail, switch the target if a pending IRQ has a strictly more urgent group priority), `handler_return` (tail-chain if the winner can preempt the context being returned to, else exit).
> 3. Firmware: `encode_prio`, a board configuration with 2 group bits + 2 sub bits on a 4-bit part, and `burst_overhead(tm, k)` for k back-to-back IRQs.
>
> Example: IRQ6 (0x80) pends at t=0, IRQ1 (0x10) at t=5 → `@5 late IRQ1 (was IRQ6)`, `@12 start IRQ1`, `@22 tail IRQ6`, `@28 start IRQ6`, `@38 exit`, `@48 resume thread`. `encode_prio(4 bits, prigroup 5, 2, 1) == 0x90`. `burst_overhead(k=4) == 40` with tail-chaining, `88` without.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨져 있을 때
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

실제 출력 (일부):

```text
FAIL starter.c:353  prio_store(&n, 0xFF) == 0xF0 && prio_store(&n, 0x5A) == 0x50
FAIL starter.c:354  prio_store(&n, 0x5A) == 0x40 && prio_store(&n, 0x1F) == 0x00
...
  log line 2: got "" expected "@10 entry IRQ5"
FAIL starter.c:433  log_is("@10 pend IRQ5\n@10 entry IRQ5\n@22 start IRQ5\n@42 done IRQ5\n@42 exit\n@52 resume thread\n")
...
  log line 1: got "@0 basepri 0x00" expected "@0 basepri 0x40"
...
FAIL starter.c:662  bad == 0 && maxd == 4
20/79 passed
```

로그가 틀리면 **처음 다른 줄** 을 `got / expected` 로 보여 준다. 통과하는 20개는 "아무 일도 없음" 을 기대하는 줄(빈 pending → −1, 시뮬레이션이 오류 없이 끝남 등)이다. 79/79 를 만든다.

## 4. 힌트

<details>
<summary>힌트 1 — 마스크 두 개면 Part A 가 끝난다</summary>

구현 마스크 = `0xFF << (8 − prio_bits)` (8비트로 자름). 그룹 마스크 = `0xFF << (prigroup + 1)` (8비트로 자름, prigroup 7 이면 0). 둘 다 "아래쪽을 0 으로" 라서 AND 하나씩이다.

</details>

<details>
<summary>힌트 2 — 실행 우선순위는 "지금 코어를 붙잡고 있는 것들 중 가장 급한 것"</summary>

active 비트가 선 IRQ 들의 **그룹** 값, BASEPRI 의 그룹 값(0 이면 무시) 중 최소. PRIMASK 는 그 결과를 0 으로 덮는다 — 0 보다 엄격히 작은 그룹은 없으니 어떤 IRQ 도 못 들어온다. 아무것도 없으면 256: 어떤 IRQ(최대 255)도 이긴다.

</details>

<details>
<summary>힌트 3 — 승자 고르기와 선점 판단은 다른 비교다</summary>

승자: 전체 8비트 값(그룹+서브) 비교, 같으면 번호. 오름차순으로 돌면서 `<` (엄격히) 를 쓰면 같은 값에서 번호 작은 쪽이 자동으로 남는다. 선점: `group(승자) < exec_prio` — 서브는 끼지 않는다. 승자가 선점을 못 하면 다른 후보도 못 한다(승자의 그룹이 최소이므로).

</details>

<details>
<summary>힌트 4 — late arrival 은 "entry 중의 선점"</summary>

entry 중에는 목표 IRQ 가 아직 active 가 아니다. 새 승자의 그룹이 **목표의 그룹보다** 엄격히 작으면 — 즉 목표의 핸들러가 막 시작했다면 곧바로 선점했을 IRQ 라면 — 같은 쌓기를 그대로 쓰고 벡터만 바꾼다. 원래 목표는 pending 으로 남아 나중에 tail 로 들어간다.

</details>

<details>
<summary>힌트 5 — 핸들러 끝: "돌아갈 문맥" 기준으로 판단</summary>

먼저 pop 하고 active 를 지운 **다음** `can_take` 를 부른다. 그러면 `exec_prio` 가 돌아갈 문맥(선점당했던 핸들러나 thread)의 우선순위가 된다. 순서를 바꾸면 방금 끝난 핸들러의 우선순위로 판단해서, 대기 중인 같은 그룹 IRQ 를 놓친다.

</details>

## 5. 풀이

### Part A — 저장 값, 그룹, 실행 우선순위

**구현 비트** — 4비트 칩에 0x5A 를 쓰면 0x50 이 저장된다. 하위 비트가 무시되니 "우선순위 5" 를 그대로 쓰면 `0x05 → 0x00` = **가장 급한 레벨** 이 된다. CMSIS `NVIC_SetPriority` 가 `<< (8 − __NVIC_PRIO_BITS)` 를 해 주는 이유다 (ARCH-B05 Q7).

| prio_bits | 0x5A 저장 | 0xFF 저장 | 레벨 수 |
|---|---|---|---|
| 2 (Armv6-M, M0/M0+) | 0x40 | 0xC0 | 4 |
| 3 | 0x40 | 0xE0 | 8 |
| 4 (STM32F4 등 흔한 값) | 0x50 | 0xF0 | 16 |
| 8 | 0x5A | 0xFF | 256 |

**그룹/서브** — `p = 0xB7 = 1011 0111` 을 PRIGROUP 별로 자르면 (테스트가 그대로 확인):

| PRIGROUP | 그룹 마스크 | group(0xB7) | 서브 비트 |
|---|---|---|---|
| 0 | 0xFE | 0xB6 | [0] |
| 1 | 0xFC | 0xB4 | [1:0] |
| 2 | 0xF8 | 0xB0 | [2:0] |
| 3 | 0xF0 | 0xB0 | [3:0] |
| 4 | 0xE0 | 0xA0 | [4:0] |
| 5 | 0xC0 | 0x80 | [5:0] |
| 6 | 0x80 | 0x80 | [6:0] |
| 7 | 0x00 | 0x00 | [7:0] — 선점 없음 |

구현 4비트 칩에서는 PRIGROUP 0–3 이 모두 "서브 비트가 미구현 영역에만 있음" → 4비트 전부 그룹(16 레벨, 서브 1). 4 → 8×2, 5 → 4×4, 6 → 2×8, 7 → 1×16 (추가 연습 3 의 표). STM32 HAL 의 `NVIC_PRIORITYGROUP_4`(선점 4비트) 가 PRIGROUP 3, `_2`(2+2) 가 5 에 해당한다.

**실행 우선순위** — "지금 코어를 붙잡고 있는 가장 급한 것". 선점 조건은 `group(승자) < exec_prio` 로 **엄격한** 부등호다: 같은 그룹은 서로 선점하지 못한다. 이 하나로 세 가지가 동시에 설명된다 — (1) 같은 IRQ 는 자기 자신을 선점 못 한다 (B7), (2) 서브만 더 급해도 못 끼어든다 (B5 오른쪽), (3) BASEPRI 와 같은 그룹 값인 IRQ 는 막힌다 (C1 의 ADC).

BASEPRI 도 **그룹으로** 본다 (ARMv7-M 의사코드의 실행 우선순위 계산 기준). 그래서 PRIGROUP 5 에서 BASEPRI = 0x30 은 그룹 0x00 → PRIMASK 처럼 **전부** 막는다 (Part A 테스트 마지막 줄). "서브 비트만 다른 값을 BASEPRI 에 쓰면 예상보다 많이 막힌다" 는 함정.

### Part B — 코어 상태 기계

```svg
<svg viewBox="0 0 740 325" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m09b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">코어 상태 기계 — 결정 세 개 (try_preempt · late_arrival · handler_return)</text>
  <g stroke="currentColor">
    <rect x="20" y="150" width="120" height="50" rx="8" class="f-mut"/>
    <rect x="290" y="60" width="140" height="50" rx="8" class="f-warn"/>
    <rect x="290" y="150" width="140" height="50" rx="8" class="f-acc"/>
    <rect x="560" y="150" width="140" height="50" rx="8" class="f-warn"/>
    <rect x="290" y="250" width="140" height="50" rx="8" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="13">
    <text x="80" y="180">THREAD</text>
    <text x="360" y="82">ENTRY</text><text x="360" y="100" font-size="12">12 사이클 (쌓기)</text>
    <text x="360" y="172">HANDLER</text><text x="360" y="190" font-size="12">본문 + 동작</text>
    <text x="630" y="172">TAIL</text><text x="630" y="190" font-size="12">6 사이클</text>
    <text x="360" y="272">EXIT</text><text x="360" y="290" font-size="12">10 사이클 (꺼내기)</text>
  </g>
  <g stroke="currentColor" fill="none">
    <polyline points="80,148 80,85 288,85" marker-end="url(#m09b)"/>
    <line x1="330" y1="110" x2="330" y2="148" marker-end="url(#m09b)"/>
    <line x1="390" y1="150" x2="390" y2="112" marker-end="url(#m09b)"/>
    <path d="M340 60 C 330 28, 390 28, 380 58" marker-end="url(#m09b)"/>
    <line x1="430" y1="165" x2="558" y2="165" marker-end="url(#m09b)"/>
    <line x1="560" y1="188" x2="432" y2="188" marker-end="url(#m09b)"/>
    <line x1="340" y1="200" x2="340" y2="248" marker-end="url(#m09b)"/>
    <line x1="410" y1="250" x2="410" y2="202" marker-end="url(#m09b)"/>
    <polyline points="290,275 80,275 80,202" marker-end="url(#m09b)"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="90" y="78">승자 can_take</text>
    <text x="322" y="135" text-anchor="end">끝: pending→active</text>
    <text x="398" y="135">선점 (문맥은 스택에)</text>
    <text x="400" y="42">late: 목표 교체</text>
    <text x="440" y="158">끝 + 승자 can_take</text>
    <text x="470" y="206">tail 끝</text>
    <text x="332" y="230" text-anchor="end">끝 + 못 뺏음</text>
    <text x="418" y="230">resume 핸들러</text>
    <text x="100" y="268">resume (스택 비면)</text>
  </g>
  <text x="10" y="318" fill="currentColor" font-size="12">스택 = 선점당한 핸들러 문맥 (progress 포함). 판단은 셋뿐, 나머지 전이는 남은 사이클이 0 이 되면 일어난다.</text>
</svg>
```

결정이 일어나는 곳은 셋뿐이다 (나머지 — 사건 반영, 활성화, resume, 시간 건너뛰기 — 는 주어진 코드).

```c
static bool try_preempt(sim_t *s) {             /* THREAD / HANDLER */
    int b = pick_pending(&s->n);
    if (!can_take(&s->n, b)) return false;      /* group(b) < exec_prio 가 아니면 그대로 */
    /* 로그, ENTRY 시작: remaining = tm.entry, target = b */
}
static void late_arrival(sim_t *s) {            /* ENTRY / TAIL, remaining > 0 */
    int b = pick_pending(&s->n);
    if (b >= 0 && b != s->target && group(b) < group(target)) target = b;   /* 쌓기는 그대로 */
}
static void handler_return(sim_t *s) {
    pop + active 해제;                           /* 순서 중요: 먼저 지우고 */
    if (tm.tailchain && can_take(승자)) TAIL(6) else EXIT(10);   /* 돌아갈 문맥 기준 판단 */
}
```

**왜 이산 사건(discrete-event) 인가** — 사이클마다 도는 대신 "다음에 무언가 바뀌는 시각" = min(다음 외부 사건, 단계 끝, 핸들러의 다음 동작 지점) 으로 건너뛴다. 선점당한 핸들러는 `progress` 를 들고 스택에 있다가 resume 후 이어서 센다 → 핸들러 안 동작(setpend)의 **실제 시각** 이 선점만큼 밀린다 (B6: progress 10 의 clrpend 가 시각 50 에 일어남). 같은 시각의 순서 규칙(외부 사건 먼저)이 tail 판단과 late 창의 경계를 정한다.

세 가지 핵심 동작을 시간축에 놓으면 (모두 체크된 로그 그대로):

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">E = entry 12 · T = tail 6 · X = exit 10 · ▼ = pending 된 시각 (1 사이클 = 7 px)</text>
  <g fill="currentColor"><text x="10" y="58">(a) 선점·중첩</text><text x="10" y="108">(b) tail-chain</text><text x="10" y="158">(b') tail 없음</text><text x="10" y="208">(c) late arrival</text></g>
  <g stroke="currentColor">
    <rect x="120" y="42" width="84" height="24" class="f-mut"/><rect x="204" y="42" width="56" height="24" class="f-ok"/>
    <rect x="260" y="42" width="84" height="24" class="f-mut"/><rect x="344" y="42" width="70" height="24" class="f-acc"/>
    <rect x="414" y="42" width="70" height="24" class="f-mut"/><rect x="484" y="42" width="154" height="24" class="f-ok"/>
    <rect x="638" y="42" width="70" height="24" class="f-mut"/>
    <rect x="120" y="92" width="84" height="24" class="f-mut"/><rect x="204" y="92" width="140" height="24" class="f-ok"/>
    <rect x="344" y="92" width="42" height="24" class="f-warn"/><rect x="386" y="92" width="70" height="24" class="f-acc"/>
    <rect x="456" y="92" width="70" height="24" class="f-mut"/>
    <rect x="120" y="142" width="84" height="24" class="f-mut"/><rect x="204" y="142" width="140" height="24" class="f-ok"/>
    <rect x="344" y="142" width="70" height="24" class="f-mut"/><rect x="414" y="142" width="84" height="24" class="f-mut"/>
    <rect x="498" y="142" width="70" height="24" class="f-acc"/><rect x="568" y="142" width="70" height="24" class="f-mut"/>
    <rect x="120" y="192" width="84" height="24" class="f-mut"/><rect x="204" y="192" width="70" height="24" class="f-acc"/>
    <rect x="274" y="192" width="42" height="24" class="f-warn"/><rect x="316" y="192" width="70" height="24" class="f-ok"/>
    <rect x="386" y="192" width="70" height="24" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="162" y="58">E</text><text x="232" y="58">IRQ7</text><text x="302" y="58">E</text><text x="379" y="58">IRQ2</text><text x="449" y="58">X</text><text x="561" y="58">IRQ7 (이어서)</text><text x="673" y="58">X</text>
    <text x="162" y="108">E</text><text x="274" y="108">IRQ3</text><text x="365" y="108">T</text><text x="421" y="108">IRQ4</text><text x="491" y="108">X</text>
    <text x="162" y="158">E</text><text x="274" y="158">IRQ3</text><text x="379" y="158">X</text><text x="456" y="158">E</text><text x="533" y="158">IRQ4</text><text x="603" y="158">X</text>
    <text x="162" y="208">E</text><text x="239" y="208">IRQ1</text><text x="295" y="208">T</text><text x="351" y="208">IRQ6</text><text x="421" y="208">X</text>
  </g>
  <g fill="currentColor">
    <path d="M115 32 L125 32 L120 40 z"/><path d="M255 32 L265 32 L260 40 z"/>
    <path d="M115 82 L125 82 L120 90 z"/><path d="M150 82 L160 82 L155 90 z"/>
    <path d="M115 132 L125 132 L120 140 z"/><path d="M150 132 L160 132 L155 140 z"/>
    <path d="M115 182 L125 182 L120 190 z"/><path d="M150 182 L160 182 L155 190 z"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="532" y="108">끝 58</text><text x="644" y="158">끝 74 (+16)</text><text x="462" y="208">끝 48 · IRQ1 지연 7 (▼5 → 12)</text>
  </g>
  <g stroke="currentColor"><line x1="120" y1="236" x2="708" y2="236"/>
    <line x1="120" y1="232" x2="120" y2="240"/><line x1="190" y1="232" x2="190" y2="240"/><line x1="260" y1="232" x2="260" y2="240"/><line x1="330" y1="232" x2="330" y2="240"/>
    <line x1="400" y1="232" x2="400" y2="240"/><line x1="470" y1="232" x2="470" y2="240"/><line x1="540" y1="232" x2="540" y2="240"/><line x1="610" y1="232" x2="610" y2="240"/><line x1="680" y1="232" x2="680" y2="240"/>
  </g>
  <g fill="currentColor" text-anchor="middle"><text x="120" y="254">0</text><text x="190" y="254">10</text><text x="260" y="254">20</text><text x="330" y="254">30</text><text x="400" y="254">40</text><text x="470" y="254">50</text><text x="540" y="254">60</text><text x="610" y="254">70</text><text x="680" y="254">80</text></g>
  <text x="10" y="280" fill="currentColor">(a) IRQ7 0x80 ▼0, IRQ2 0x20 ▼20 · (b) IRQ3 0x40 ▼0, IRQ4 0x60 ▼5 · (c) IRQ6 0x80 ▼0, IRQ1 0x10 ▼5</text>
</svg>
```

- **(a) 선점** — IRQ2 가 20 에 오면 IRQ7(진행 8/30) 위에 새 entry. IRQ2 끝(42)에 대기 IRQ 가 없으니 exit 10 → 52 에 IRQ7 을 **이어서**(남은 22) → 74 에 끝, exit → 84. 최대 깊이 2.
- **(b) tail-chain** — IRQ4(0x60)는 5 에 왔지만 IRQ3(0x40)를 선점 못 하고(그룹 0x60 ≮ 0x40), late 도 아니다. IRQ3 끝(32)에 thread 로 돌아가는 대신 tail 6 → IRQ4 는 38 에 시작 (지연 33).
- **(b') tail 없음** — 같은 사건, `tm.tailchain = false`: exit 10 + entry 12 = 22 사이클 → 16 늦은 74 에 끝 (테스트가 `58 + 16` 을 확인).
- **(c) late arrival** — IRQ6 를 위해 쌓는 중(0–12)에 더 급한 IRQ1 이 5 에 오면, 같은 쌓기로 IRQ1 에 들어간다 (12 에 시작, 지연 7 < 12). IRQ6 은 pending 으로 남아 IRQ1 끝에서 tail 로 들어간다. entry 는 한 번(`n_entry == 1`).

**late 창의 경계** — 모델 규칙은 "remaining > 0". 그래서 IRQ1 이 11 에 오면 late (12 에 시작, 지연 **1**), 12 에 오면 IRQ6 이 이미 시작했으므로 보통 선점 (entry 한 번 더, 24 에 시작, 지연 12, 깊이 2). 지연 1 은 **모델의 산물** 이다: 실제 코어는 벡터 읽기와 파이프라인 채우기가 entry 끝부분을 차지하므로 late 로 받아 줄 수 있는 창이 entry 전체보다 짧고, 그 길이는 구현마다 다르다. 이 모델은 경계 규칙을 하나로 정해서 테스트로 못 박는 데 의미가 있다.

**같은 그룹은 late 가 아니다** (B4b) — PRIGROUP 5 에서 0x60 과 0x40 은 둘 다 그룹 0x40. IRQ1 이 entry 중에 와도 목표를 바꾸지 않는다 (IRQ6 의 핸들러가 시작됐어도 선점 못 할 IRQ 이므로). 대신 IRQ6 끝에서 tail.

**PRIGROUP 이 결과를 바꾼다** (B5, 같은 사건: IRQ8 0x60 ▼0, IRQ10·IRQ9 0x50 ▼15):

| | PRIGROUP 0 (그룹 4비트) | PRIGROUP 5 (그룹 2 + 서브 2) |
|---|---|---|
| 15 에 IRQ9·10 도착 | 그룹 0x50 < 0x60 → IRQ9 가 **선점** (번호 작은 쪽) | 둘 다 그룹 0x40 = IRQ8 → 선점 못 함 |
| 순서 | IRQ8 → IRQ9 → (tail) IRQ10 → IRQ8 이어서 | IRQ8 끝 → tail IRQ9 → tail IRQ10 |
| 끝 / 최대 깊이 / IRQ9 지연 | 90 / 2 / 12 | **74** / 1 / 23 |

서브 우선순위는 선점을 포기하는 대신 entry/exit 를 아낀다 — 전체는 16 사이클 빨리 끝나고 스택도 덜 쓰지만, IRQ9 의 지연은 늘어난다. 이게 PRIGROUP 설계의 거래(trade-off)다 (설계 원칙은 BM-M02).

**핸들러가 pending 을 건드리면** (B6) — IRQ2 가 진행 5 에 IRQ0(0x10) 을 set → 즉시 선점, 진행 10 에 IRQ12 를 clear → IRQ12 는 1 에 왔지만 **한 번도 안 돈다**, 진행 15 에 IRQ3 을 set → IRQ2 끝에서 tail. 소프트웨어 트리거(펌웨어에서 낮은 우선순위 IRQ 를 set-pending 해서 "나중 처리(bottom half)" 를 넘기는 패턴)가 이것이다.

**active 중 다시 pending** (B7) — IRQ4 실행 중(12–22) 15 에 같은 IRQ 가 또 오면: 자기 그룹 = 실행 우선순위 → 선점 못 함, pending 으로 남음 → 끝(22)에서 **자기 자신으로** tail → 28 에 다시 시작 (`runs == 2`, 지연 13). active 와 pending 비트가 따로인 이유다.

**PRIMASK / disable** (B8, B9) — 둘 다 "들어가지 못할 뿐 pending 은 남는다". PRIMASK 를 50 에 풀면 그 시각에 entry (지연 52). disable 된 IRQ6 의 pending 은 끝까지 남는다 (`pending == 1<<6`).

**퍼즈 불변식** (B10, 2000 시나리오, 무작위 구현 비트·PRIGROUP·우선순위·핸들러 동작·tail on/off) — 끝나면 pending/active/스택이 비고 THREAD, `now == thread + overhead + handler` (시간 보존), `handler_cyc == Σ runs[k]·dur[k]` (선점당한 핸들러도 정확히 자기 길이만큼 돈다). 3단 이상 중첩이 실제로 나왔는지도 확인한다 (coverage).

`./solution -v` 는 로그와 타임라인을 찍는다 (실제 출력, B4):

```text
== B4 late arrival ==
@0 pend IRQ6
@0 entry IRQ6
@5 pend IRQ1
@5 late IRQ1 (was IRQ6)
@12 start IRQ1
@22 done IRQ1
@22 tail IRQ6
@28 start IRQ6
@38 done IRQ6
@38 exit
@48 resume thread
cycle  0         1         2         3         4          (한 글자 = 1 사이클, 숫자 = 10 단위)
core   EEEEEEEEEEEE          TTTTTT          XXXXXXXXXX
thread                                                 
IRQ1               ##########                          
IRQ6                               ##########          
```

### Part C — 보드 설정과 RTOS 임계 구역

**encode_prio** — 서브 필드 = `[prigroup : 8−prio_bits]` (구현된 부분만), 그 위가 그룹 필드. 서브 필드가 미구현 영역에만 있으면 서브 0비트(값 무시). 각 값은 필드 폭으로 자른다 (CMSIS 도 자른다 — 그룹 5 를 2비트 필드에 넣으면 1 이 된다: 테스트 `encode_prio(&n, 5, 0) == 0x40`).

| prio_bits, PRIGROUP | 그룹 필드 | 서브 필드 | (2, 1) → |
|---|---|---|---|
| 4, 5 | [7:6] | [5:4] | 0x90 |
| 4, 0 | [7:4] | 없음 | 0x20 (서브 무시) |
| 3, 5 | [7:6] | [5] | 0xA0 |
| 8, 7 | 없음 | [7:0] | 0x01 |

**fw_config** — 구현 4비트(STM32F4 류)에서 그룹 2비트 + 서브 2비트 → 서브 필드가 [5:4] 가 되려면 PRIGROUP = 5.

| IRQ | 역할 | 그룹, 서브 | 값 | 의도 |
|---|---|---|---|---|
| 0 MOTOR | PWM 제어 루프 | 0, 0 | 0x00 | 무엇이든 선점, RTOS 임계 구역도 통과 |
| 1 ADC | DMA 완료 | 1, 0 | 0x40 | UART/TICK 선점 |
| 2 UART1 | RX | 2, 0 | 0x80 | UART2 와 같은 그룹: 서로 선점 안 함 |
| 3 UART2 | RX | 2, 1 | 0x90 | 둘 다 대기 중이면 UART1 먼저 |
| 4 TICK | RTOS 틱 | 3, 3 | 0xF0 | 가장 낮게 (RTOS 관례) |

**C1 — 임계 구역: BASEPRI vs PRIMASK** — thread 가 0–100 동안 임계 구역. `SYSCALL_MAX = encode(1, 0) = 0x40` 을 BASEPRI 에 쓰면 그룹 ≥ 1 만 막힌다. 사건: UART1 ▼5, TICK ▼8, MOTOR ▼20, ADC ▼25 (실제 로그에서 읽은 시각):

| IRQ | BASEPRI 0x40: 시작 / 지연 | PRIMASK: 시작 / 지연 |
|---|---|---|
| MOTOR (▼20) | 32 / **12** | 112 / **92** |
| ADC (▼25) | 112 / 87 | 126 / 101 |
| UART1 (▼5) | 128 / 123 | 142 / 137 |
| TICK (▼8) | 140 / 132 | 154 / 146 |

BASEPRI 쪽에서 MOTOR 끝(40)에 ADC 가 기다리고 있지만 tail 하지 않는다: 돌아갈 문맥의 실행 우선순위 = group(BASEPRI) = 0x40, ADC 그룹 0x40 ≮ 0x40 → exit. 100 에 BASEPRI 를 0 으로 쓰는 순간 ADC → tail UART1 → tail TICK. 이것이 FreeRTOS Cortex-M3/M4 포트의 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 구조다: 커널 임계 구역은 BASEPRI 로 그 값 이하(덜 급함)만 막고, 그보다 급한 IRQ 는 커널 때문에 늦어지지 않는다 — 대신 그런 ISR 은 `...FromISR` API 를 부르면 안 된다. Cortex-M0(Armv6-M)에는 BASEPRI 가 없어서 PRIMASK 로 전부 막는다.

**C2 — 4단 중첩** (`fw_config`, TICK 30 · UART2 20 · ADC 15 · MOTOR 8 사이클). 실제 `-v` 출력 (한 글자 = 2 사이클; `#` 실행, `.` active 지만 선점당함):

```text
cycle  0         1         2         3         4         5         6         7         8         (한 글자 = 2 사이클, 숫자 = 20 단위)
core   EEEEEE EEEEEE   EEEEEE   EEEEEE    XXXXX     XXXXX       TTT   XXXXX              XXXXX
thread                                                                                        
IRQ0                                  ####                                                    
IRQ1                         ###...............#####                                          
IRQ2                                                               ###                        
IRQ3                ###..................................#######                              
IRQ4         #.............................................................##############     
```

읽을 거리: UART1(IRQ2) 은 30 에 왔지만 UART2(IRQ3) 와 같은 그룹이라 못 끼어든다. ADC 가 끝난 89 에도 돌아갈 문맥이 UART2(그룹 0x80) 라 exit. UART2 가 끝난 113 에는 돌아갈 문맥이 TICK(그룹 0xC0) → UART1(0x80) 이 빼앗을 수 있으니 **thread 가 아니라 TICK 으로 돌아가는 길에서도 tail-chain** 한다. 최대 깊이 4.

**깊이 ≤ 그룹 레벨 수** — 중첩되려면 그룹이 엄격히 작아져야 하므로, 스택 위로 쌓인 핸들러들의 그룹 값은 엄격히 감소한다 → 깊이 ≤ 서로 다른 그룹 수 (여기 4). 퍼즈 3000개가 이를 확인하고 실제로 4에 도달한다. 펌웨어에서는 이 수 × (예외 프레임 + 각 ISR 의 최대 스택) 이 메인 스택(MSP) 크기 산정의 출발점이다 (프레임 크기는 ARM-M09).

**C3 — 연속 IRQ k 개의 오버헤드** (thread 에서 k 개가 동시에 pending, 서로 선점 안 함):

| k | tail-chain 있음 E + (k−1)·T + X | 없음 k·(E + X) | 절약 |
|---|---|---|---|
| 1 | 22 | 22 | 0 |
| 2 | 28 | 44 | 16 |
| 4 | 40 | 88 | 48 |
| 8 | 64 | 176 | 112 |

시뮬레이터의 `overhead` 와 끝 시각(`overhead + 10k`)이 k = 1..8, 두 모드 모두에서 식과 같다. 아끼는 것은 "꺼냈다가 바로 다시 쌓는" 메모리 왕복 — 쌓아 둔 프레임을 그대로 재사용한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san
```

```text
79/79 passed
79/79 passed          (ASan + UBSan, 경고 없음)
```

Apple M6, Apple clang 21.0.0 — 전체 테스트(퍼즈 5000 시나리오 포함)가 0.05초 안에 끝난다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 선점을 전체 8비트 값으로 비교 | 서브가 더 급하다고 선점하면 PRIGROUP 의 의미가 사라진다 (B5 오른쪽이 틀어짐) | `group_of(prio[b]) < exec_prio(n)` |
| `<=` 로 선점 판단 | 같은 그룹끼리, 심지어 같은 IRQ 가 자기 자신을 선점 → 무한 중첩 | 엄격한 `<` |
| handler_return 에서 active 를 지우기 전에 `can_take` | 실행 우선순위가 방금 끝난 핸들러 그룹으로 계산돼 같은 그룹 대기 IRQ 를 tail 하지 않고 exit | pop + `active &= ~bit` **다음에** 판단 |
| entry 를 시작할 때 pending 을 지움 | late arrival 로 목표가 바뀌면 원래 IRQ 를 잃는다 | 활성화(entry 끝)에서 pending 해제 + active 설정 |
| late arrival 을 "승자가 바뀌면" 으로 구현 | 같은 그룹·서브만 더 급한 IRQ 로도 목표가 바뀜 (선점도 못 할 IRQ) | 그룹이 목표보다 **엄격히** 급할 때만 |
| 우선순위를 구현 비트 무시하고 저장 (`prio = 5`) | 4비트 칩에서 하위 비트는 버려져 0x00 = 최우선이 된다 | `prio_store` / CMSIS 시프트 |
| BASEPRI 에 서브 비트만 다른 값 (`0x30`, PRIGROUP 5) | BASEPRI 도 그룹으로 보므로 그룹 0 → 전부 차단 | 그룹 경계 값(`encode_prio(g, 0)`)을 쓴다 |
| disable = pending 삭제라고 생각 | pending 은 남아 있다가 enable 하는 순간 들어온다 (B9) | 버리려면 clear-pending 을 따로 |

## 8. 면접 Q&A

<details>
<summary>Q1. enable / pending / active 세 비트가 왜 따로 있어야 하나요?</summary>

enable 은 "이 IRQ 를 받을지" (설정), pending 은 "요청이 와서 기다림" (사건 기억 1비트), active 는 "핸들러가 시작됐고 아직 안 끝남 — 선점당해 스택에 있어도 포함" (실행 상태). 독립이라 네 가지 조합이 다 의미가 있다: disable 중에도 pending 이 쌓인다(B9), active 중에 다시 pending 이 될 수 있다(B7: 끝나면 자기 자신으로 tail), active 가 여럿이면 선점 중첩, pending 을 소프트웨어가 set 해서 낮은 우선순위로 일을 넘긴다(B6).
꼬리질문 "그럼 IRQ 가 pending 일 때 두 번 더 오면?" → pending 은 1비트라 합쳐진다. 핸들러는 1번만 돈다 → UART 핸들러는 "바이트 하나" 가 아니라 "FIFO 가 빌 때까지" 읽어야 한다 (추가 연습 4).

</details>

<details>
<summary>Q2. 그룹 우선순위와 서브 우선순위의 차이는? 서브는 왜 쓰나요?</summary>

그룹만 선점을 결정한다 (실행 우선순위보다 엄격히 작아야). 서브는 같은 그룹 안에서 여럿이 **대기 중일 때** 순서만 정하고, 그것도 같으면 번호 작은 IRQ. 서브를 쓰면 같은 그룹끼리는 중첩되지 않으니 entry/exit 가 줄고 스택 깊이 상한이 낮아진다 (B5: 끝 90 → 74, 깊이 2 → 1). 대신 같은 그룹의 급한 IRQ 도 지금 핸들러가 끝날 때까지 기다린다 (IRQ9 지연 12 → 23).
꼬리질문 "FreeRTOS 는 어떻게 권하나요?" → 모든 비트를 그룹(선점)으로 (STM32 HAL 의 `NVIC_PRIORITYGROUP_4`) — 서브가 있으면 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 경계 해석이 복잡해지기 때문.

</details>

<details>
<summary>Q3. tail-chaining 은 정확히 무엇을 아끼나요? thread 로 돌아갈 때만 일어나나요?</summary>

핸들러가 끝났을 때 돌아갈 문맥보다 급한 IRQ 가 대기 중이면, 쌓아 둔 레지스터를 꺼냈다가 곧바로 다시 쌓는 대신 이미 있는 프레임을 그대로 두고 다음 핸들러로 간다. 이 모델의 값으로 exit 10 + entry 12 = 22 → tail 6, IRQ 당 16 사이클 (C3 표, k 개면 16(k−1)). thread 로 돌아갈 때만이 아니다: C2 에서 UART2 가 끝나고 TICK 으로 돌아가는 길에 UART1 로 tail 한다. 조건은 언제나 "돌아갈 문맥의 실행 우선순위보다 엄격히 급한가".
꼬리질문 "tail 중에 더 급한 게 오면?" → 이 모델은 entry 와 같은 late 규칙을 적용한다 (목표 교체). 실제 코어의 세부 창은 구현마다 다르다.

</details>

<details>
<summary>Q4. late arrival 과 pop-preemption 을 설명해 보세요.</summary>

late arrival: 낮은 IRQ 를 위해 쌓는 중에 더 급한 IRQ 가 오면, 같은 쌓기를 그대로 쓰고 벡터만 급한 쪽으로 바꾼다 → 급한 IRQ 의 지연이 entry 보다 짧아질 수 있다 (B4: 7 사이클), 원래 IRQ 는 나중에 tail. pop-preemption: exit(꺼내기) 중에 돌아갈 문맥보다 급한 IRQ 가 오면 꺼내기를 그만두고 tail 처럼 들어간다. 이게 없으면 최악 지연이 `X − 1 + E = 21` (exit 첫 사이클 직후에 도착), 있으면 `E = 12` (추가 연습 1·2 의 스윕). 두 기능 모두 Cortex-M3/M4 에 있는 것으로 문서화돼 있고, 이 모델 본문은 late 만, 연습 2 에서 pop 을 더한다.
꼬리질문 "그래서 최우선 IRQ 의 최악 지연은?" → 하드웨어만 보면 entry 사이클 + 메모리 대기 + 끝내지 못한 다중 사이클 명령, 여기에 소프트웨어의 가장 긴 PRIMASK 구간과 (같은 그룹이면) 다른 핸들러의 길이가 더해진다. 측정은 BM-A10 (DWT 사이클 카운터).

</details>

<details>
<summary>Q5. RTOS 는 임계 구역에 왜 PRIMASK 가 아니라 BASEPRI 를 쓰나요?</summary>

PRIMASK 는 설정 가능한 IRQ 전부를 막는다 → 모터 제어처럼 커널과 상관없는 급한 IRQ 도 임계 구역 길이만큼 늦는다 (C1: 92 사이클). BASEPRI 는 그룹 값이 문턱 이상인 IRQ 만 막아서, 문턱보다 급한 IRQ 는 커널 임계 구역 중에도 12 사이클에 들어온다. 대가: 그런 IRQ 의 ISR 은 커널 자료구조를 건드리는 API(`...FromISR`)를 부르면 안 된다 — 커널이 그 IRQ 를 막지 않으니 경쟁 상태가 된다.
꼬리질문 "Cortex-M0 에서는?" → Armv6-M 에는 BASEPRI 가 없다 → PRIMASK(CPSID i) 로 전부 막는 수밖에 없고, 그래서 M0 포트에서는 임계 구역을 짧게 유지하는 것이 더 중요하다.

</details>

<details>
<summary>Q6. 우선순위 값 "5" 를 넣었는데 가장 급한 IRQ 가 되어 버렸습니다. 왜죠?</summary>

구현 비트가 상위 4비트뿐인 칩에서 0x05 를 레지스터에 그대로 쓰면 하위 4비트가 버려져 0x00 이 저장된다 = 최우선 레벨. CMSIS `NVIC_SetPriority(irq, 5)` 는 `5 << (8 − __NVIC_PRIO_BITS)` = 0x50 으로 바꿔 쓴다. 레지스터에 직접 쓰거나 다른 SDK 의 "이미 시프트된 값" 과 섞을 때 생기는 실수. BASEPRI 에 쓰는 값도 같은 정렬을 따른다.
꼬리질문 "칩의 구현 비트 수를 런타임에 알아내려면?" → 우선순위 필드에 0xFF 를 써 보고 다시 읽으면 구현된 비트만 1 로 남는다 (모델로는 `prio_store(n, 0xFF)`). 레지스터 이름과 위치는 BM-M01.

</details>

<details>
<summary>Q7. "ISR 이 끝나자마자 한 번 더 들어옵니다." 원인 후보는?</summary>

(1) 레벨 신호: 주변장치 플래그를 안 지웠거나 늦게 지워서, 핸들러가 끝날 때 IRQ 선이 아직 high → 다시 pending → 모델의 B7 처럼 자기 자신으로 tail. 쓰기 버퍼·느린 주변장치 버스 때문에 "마지막 줄에서 플래그를 지우는 쓰기" 가 exception return 보다 늦게 도착할 수 있다 → 플래그를 핸들러 앞쪽에서 지우거나, 지운 뒤 다시 읽거나 배리어(DSB)를 둔다 (이 패턴은 여러 MCU 앱노트가 경고한다). (2) 처리 중에 진짜 새 사건이 와서 pending 이 다시 섰다 — 정상 동작. 둘을 가르려면 진입 시 주변장치 상태 레지스터와 카운터를 기록한다.
꼬리질문 "pulse 인터럽트면?" → 핸들러 실행 중(active)에 펄스가 오면 pending 이 다시 서서 한 번 더 돈다. pending 상태에서 또 오면 합쳐진다.

</details>

<details>
<summary>Q8. 이 시뮬레이터를 어떻게 검증했나요? 실리콘 검증에서는 무엇을 더 보겠어요?</summary>

(1) 손으로 유도한 기대 로그를 글자 단위로 비교 (시나리오마다 규칙 하나: 선점, tail, late 와 그 경계, 같은 그룹, PRIGROUP, set/clear, 재-pending, PRIMASK, disable). (2) 무작위 퍼즈에 불변식: 전부 처리됨, 시간 보존, 핸들러 사이클 = Σ 횟수·길이, 깊이 ≤ 그룹 레벨, 금지된 중첩 없음. (3) coverage: 3단·4단 중첩이 실제로 나왔는지 CHECK. 실리콘에서는 같은 모델을 **참조 모델(scoreboard)** 로 쓰고 DUT 의 핸들러 진입 순서·시각을 비교한다. 더 볼 것: 같은 사이클 동시 도착 전 조합, entry 각 사이클에 도착하는 late 창 스윕(연습 1 과 같은 방식), 우선순위 레지스터를 핸들러 안에서 바꾸는 경우, 우선순위 설정이 바뀌는 순간의 경쟁.

</details>

## 9. 추가 연습

### 연습 1 — 도착 시각 스윕으로 최악 지연 찾기

IRQ5(0x40, 20 사이클)가 t=0 에 온다. 최우선 IRQ0(0x00) 의 도착 시각 a 를 0..60 으로 옮기며 지연을 재고, 최악값과 그 시각을 찾아라. 식으로도 설명하라.

<details>
<summary>답 보기</summary>

실제 출력 (3 사이클 간격으로 표시): `12 9 6 3 12 12 12 12 12 12 12 21 18 15 12 …` → 최악 **21 @ a=33**. 구간별로: a=0 은 같은 시각이라 IRQ0 이 바로 승자(12). 1..11 은 late arrival (12−a). 12..31 은 IRQ5 선점(12). 32 는 IRQ5 끝과 같은 시각 → tail (6). 33..41 은 exit 중이라 기다렸다 resume 후 entry: `54 − a` → a=33 에서 `X − 1 + E = 9 + 12 = 21`. 42 부터 다시 12. 최악은 "가장 급한 IRQ 도 exit 의 첫 사이클 직후에 오면 꺼내기가 끝날 때까지 기다린다" 는 데서 나온다.

</details>

### 연습 2 — pop-preemption 더하기

exit 중(remaining > 0)에 돌아갈 문맥을 빼앗을 수 있는 IRQ 가 오면 꺼내기를 버리고 tail(6) 로 들어가게 하라. 연습 1 의 최악값은?

<details>
<summary>답 보기</summary>

`settle` 의 EXIT 분기에 `if (remaining && tm.pop && can_take(승자)) → PH_TAIL, remaining = tm.tail` 를 넣는다 (로그 `pop IRQb`). a=35 의 로그: `@32 done IRQ5 / @32 exit / @35 pend IRQ0 / @35 pop IRQ0 / @41 start IRQ0` (지연 6). 스윕 최악은 **12** (a=0 등) — 33..41 이 모두 6 이 되어 최악이 E 로 내려온다. 실제 출력: `sweep (pop 있음): 최악 12, a=33 → 6`.

</details>

### 연습 3 — decode_prio 와 PRIGROUP 표

`decode_prio(n, v, &group, &sub)` 를 encode 의 역함수로 만들고, 모든 (구현 비트 2–8, PRIGROUP 0–7, 저장 가능한 값) 에서 왕복을 확인하라. 구현 4비트에서 PRIGROUP 별 (그룹 레벨 수, 서브 레벨 수) 는?

<details>
<summary>답 보기</summary>

`split = max(prigroup+1, 8−prio_bits)`, `group = v >> split` (split 8 이면 0), `sub = (v >> lsb) & (2^(split−lsb) − 1)`. 레벨 곱은 언제나 `2^prio_bits`. 4비트: PRIGROUP 0–3 → 16×1, 4 → 8×2, 5 → 4×4, 6 → 2×8, 7 → 1×16. `decode(0x90)` = (2, 1).

</details>

### 연습 4 — 잃어버린 인터럽트 세기

PRIMASK 로 0–60 을 막은 동안 UART IRQ2 에 바이트가 10, 20, 30, 40, 50 에 오고, 핸들러 실행 중(72–82)에 75, 78 에 또 온다. 핸들러는 몇 번 돌고, 사건 몇 개가 "합쳐져 사라지나"? `set_pend` 에 카운터를 더해 확인하라.

<details>
<summary>답 보기</summary>

`set_pend` 에서 이미 pending 이면 `lost[irq]++`. 결과 `runs == 2`, `lost == 5`: 막힌 동안 첫 사건만 pending 을 세우고 나머지 4개는 합쳐진다. 75 는 핸들러가 이미 시작해 pending 이 지워진 뒤라 새로 pending (→ 82 에 자기 자신으로 tail), 78 은 그 pending 에 합쳐진다. 퍼즈 2000개에서 "사건 수 = 실행 + 잃음" 이 성립한다. 교훈: 핸들러는 FIFO/상태 레지스터가 빌 때까지 처리해야 하고, "IRQ 횟수 = 사건 수" 를 가정하면 안 된다 (DMA + idle-line 이 흔한 해법, BM-M08).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
```

```text
  sweep (pop 없음): a=0..60 지연 = 12 9 6 3 12 12 12 12 12 12 12 21 18 15 12 12 12 12 12 12 12  → 최악 21 @a=33
  sweep (pop 있음): 최악 12, a=33 → 6 (꺼내기를 버리고 tail 6)
12/12 passed
```

ASan + UBSan 빌드도 같은 출력, 경고 없음.

## 10. 핵심 정리

- IRQ 마다 **enable(받을지) · pending(기다림, 1비트라 합쳐짐) · active(시작됨, 선점당해도 유지)** — 셋은 독립이다.
- 우선순위 값은 작을수록 급함, 구현된 상위 비트만 저장. PRIGROUP: 그룹 `[7:p+1]` 은 **선점**, 서브 `[p:0]` 은 **대기 순서만**, 그다음 번호 작은 IRQ.
- 실행 우선순위 = min(active 그룹, group(BASEPRI)), PRIMASK → 0, 없으면 256. 선점 = `group(승자) < 실행 우선순위` (엄격히).
- tail-chain: 끝난 핸들러 → 다음 핸들러로 꺼내기/쌓기 없이 (모델 22 → 6). 판단 기준은 **돌아갈 문맥**.
- late arrival: 쌓는 중 더 급한 IRQ → 벡터만 교체. pop-preemption: 꺼내는 중 → 버리고 tail. 둘 다 최악 지연을 줄인다 (21 → 12).
- 중첩 깊이 ≤ 그룹 레벨 수 → 스택 산정의 출발점.
- RTOS 임계 구역은 BASEPRI 로: 문턱보다 급한 IRQ 는 커널과 무관하게 12 사이클 (PRIMASK 면 92). 그런 ISR 은 커널 API 금지.

## 참고자료

- [Armv7-M Architecture Reference Manual (DDI 0403)](https://developer.arm.com/documentation/ddi0403/latest) — B1.5 예외 모델: 우선순위 그룹, 실행 우선순위, PRIMASK/BASEPRI, pending/active
- [Armv6-M Architecture Reference Manual (DDI 0419)](https://developer.arm.com/documentation/ddi0419/latest) — 2비트 우선순위, BASEPRI·PRIGROUP 없음
- [Arm Cortex-M3 / M4 Technical Reference Manual — Exceptions (latency, tail-chaining, late-arriving)](https://developer.arm.com/documentation/100166/0001)
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors*, 3rd ed. — 7–8장 예외·인터럽트, tail-chaining·late arrival·pop preemption
- [CMSIS-Core: NVIC functions (`NVIC_SetPriority`, `NVIC_EncodePriority`, `NVIC_SetPriorityGrouping`)](https://arm-software.github.io/CMSIS_6/latest/Core/group__NVIC__gr.html)
- [FreeRTOS — Running the RTOS on an ARM Cortex-M core (`configMAX_SYSCALL_INTERRUPT_PRIORITY`, 우선순위 그룹 권장)](https://www.freertos.org/Documentation/02-Kernel/03-Supported-devices/04-Demos/ARM-Cortex/RTOS-Cortex-M3-M4)
- 다음 단계: BM-M01 (NVIC 레지스터 비트), BM-M02 (우선순위 설계), BM-A10 (DWT 로 IRQ 지연 측정), ARM-M09 (EXC_RETURN·스택 프레임), ARCH-B05 (고정 우선순위 인코더)
- 소스: `starter.c` · `solution.c` · `practice.c`
