---
id: BM-M03
time: 80분
updated: 2026-11-11
---
# BM-M03 외부 인터럽트(EXTI)와 버튼 디바운스

## 학습 목표

- STM32F4 EXTI 경로 — GPIO 핀 → `SYSCFG_EXTICR` mux → 엣지 검출(RTSR/FTSR) → IMR → PR(rc_w1) → NVIC IRQ 6–10/23/40 — 를 레지스터 단위로 설명하고, **그 라인의 비트·니블만** 바꾸는 `exti_config` 를 432 조합 exhaustive 로 증명한다.
- 버튼 바운스(bounce, 채터링)가 왜 "엣지 하나 = 이벤트 하나" 설계를 깨는지 숫자로 보이고 (10 s 세션: 핀 엣지 2012개), **EXTI 엣지 → 라인 마스크 → 원샷 타이머 → 레벨 재확인 → unmask** 디바운서를 상태 머신으로 짠다.
- unmask 전에 PR 을 지워야 하는 이유, 마스크 창 안의 변화를 놓치지 않는 "다시 읽기", 같은 틱에 두 IRQ 가 펜딩일 때의 가짜 진입(spurious entry)을 주입 테스트로 확인한다.
- 1 ms 주기 샘플링 + 10 샘플 시프트 레지스터 디바운서를 짜고, 두 방식이 깨끗한/튀는 입력에서는 같은 이벤트를 내지만 **주기 잡음(글리치 트레인)·접점 불량**에서는 갈린다는 것을 보여 준다.
- PRESS / RELEASE / LONG_PRESS(800 ms, 누름당 1번) 이벤트를 ISR → 링 버퍼 → 메인 루프로 넘기고, ISR 비용(엣지 수 vs ISR 수 vs 틱 수)을 표로 말할 수 있다.

## 1. 문제

"버튼 하나 읽는 드라이버" 는 펌웨어 면접에서 **인터럽트·타이머·경쟁 조건·상태 머신**을 한 번에 물어볼 수 있는 단골 문제다. "EXTI 로 받으면 왜 두 번 눌리나?", "PR 은 언제 지우나?", "롱프레스는 엣지가 없는데 어떻게 아나?", "Stop 모드에서 버튼으로 깨우려면?" 이 줄줄이 나온다.

Don 이 RF 모듈 보드나 SSD 평가 보드에서 봤을 "리셋 버튼 한 번에 로그가 세 번 찍힌다", "롱프레스로 공장 초기화가 가끔 두 번 된다" 같은 현상이 전부 이 레슨의 버그다. GPIO 입력 설정(입력 + 풀업)은 BM-B11 에서 했다고 보고 (BM-B11 참고), 여기서는 그 핀을 **EXTI 에 연결해 인터럽트로 받고, 바운스를 거르는 것**에 집중한다.

모든 것은 맥에서 도는 결정적(deterministic) 시뮬레이션이다: BM-B02 mock 축약판 위에 EXTI·SYSCFG·TIM7·GPIO IDR 모델과 NVIC-lite 를 얹고, `sim_step()` 1번 = 0.1 ms. 레지스터 주소·비트·IRQ 번호는 STM32F4 (RM0090) 기준 대표값이다 (RM0090 기준 — RM 확인 권장). 테스트의 정답은 이 레슨 mock 의 정의다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">EXTI 라인 13 경로 (RM0090 블록도 기준 모델 — 확인 권장)</text>
  <g stroke="currentColor" fill="none">
    <rect x="10" y="40" width="70" height="22"/><rect x="10" y="66" width="70" height="22"/>
    <rect x="10" y="92" width="70" height="22" class="f-acc"/><rect x="10" y="130" width="70" height="22"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="45" y="55">PA13</text><text x="45" y="81">PB13</text><text x="45" y="107">PC13</text><text x="45" y="124">⋮</text><text x="45" y="145">PI13</text>
  </g>
  <path d="M110 34 L150 54 L150 144 L110 164 Z" stroke="currentColor" fill="none"/>
  <g stroke="currentColor"><path d="M80 51h30M80 77h30M80 103h30M80 141h30"/></g>
  <text x="130" y="186" fill="currentColor" text-anchor="middle" font-family="monospace">EXTICR4[7:4]</text>
  <text x="130" y="201" fill="currentColor" text-anchor="middle">= 2 (포트 C)</text>
  <rect x="180" y="72" width="110" height="54" stroke="currentColor" fill="none"/>
  <text x="235" y="92" fill="currentColor" text-anchor="middle">엣지 검출</text>
  <text x="235" y="110" fill="currentColor" text-anchor="middle" font-family="monospace">RTSR / FTSR</text>
  <path d="M150 99h30" stroke="currentColor"/>
  <circle cx="330" cy="99" r="16" stroke="currentColor" fill="none"/><text x="330" y="104" fill="currentColor" text-anchor="middle">OR</text>
  <path d="M290 99h24" stroke="currentColor"/>
  <rect x="290" y="150" width="80" height="26" stroke="currentColor" fill="none"/><text x="330" y="167" fill="currentColor" text-anchor="middle" font-family="monospace">SWIER</text>
  <path d="M330 150v-35" stroke="currentColor"/>
  <rect x="370" y="84" width="60" height="30" class="f-warn" stroke="currentColor"/><text x="400" y="104" fill="currentColor" text-anchor="middle">AND</text>
  <path d="M346 99h24" stroke="currentColor"/>
  <rect x="370" y="150" width="60" height="26" stroke="currentColor" fill="none"/><text x="400" y="167" fill="currentColor" text-anchor="middle" font-family="monospace">IMR</text>
  <path d="M400 150v-36" stroke="currentColor"/>
  <rect x="460" y="76" width="100" height="46" class="f-acc" stroke="currentColor"/>
  <text x="510" y="96" fill="currentColor" text-anchor="middle" font-family="monospace">PR bit13</text>
  <text x="510" y="112" fill="currentColor" text-anchor="middle">rc_w1 (W1C)</text>
  <path d="M430 99h30" stroke="currentColor"/>
  <rect x="600" y="76" width="130" height="46" class="f-ok" stroke="currentColor"/>
  <text x="665" y="96" fill="currentColor" text-anchor="middle">NVIC IRQ 40</text>
  <text x="665" y="112" fill="currentColor" text-anchor="middle" font-family="monospace">EXTI15_10</text>
  <path d="M560 99h40" stroke="currentColor"/>
  <text x="580" y="140" fill="currentColor" text-anchor="middle">PR &amp; IMR</text>
  <g fill="currentColor">
    <text x="10" y="232">· IMR=0 이면 엣지가 와도 PR 이 안 선다 (이 레슨 기본 모델). "마스크 중에도 래치" 모델도 테스트한다.</text>
    <text x="10" y="250">· PR 에 1 을 쓴 비트만 지워진다. 그 라인의 RTSR/FTSR 을 바꿔도 지워진다 (RM 문구 — 확인 권장).</text>
    <text x="10" y="268">· 라인 n 은 포트 하나만: PA13 과 PC13 은 동시에 EXTI 를 못 쓴다.</text>
    <text x="10" y="286">· IRQ: 라인 0–4 = 6–10 각자, 5–9 = 23 공유, 10–15 = 40 공유 → 핸들러가 PR 로 누군지 확인.</text>
  </g>
</svg>
```

### Part A — EXTI 레지스터 드라이버

- `int exti_line_to_irqn(unsigned line)` — 0–4 → 6–10, 5–9 → 23, 10–15 → 40, 그 외 −1.
- `int exti_config(unsigned port, unsigned pin, unsigned edges)` — SYSCFG 클럭(RCC_APB2ENR bit 14) RMW + 읽어 보기 → **IMR 끄기** → `SYSCFG_EXTICR[pin>>2]` 의 니블 `4·(pin&3)` 에 포트 번호 → RTSR/FTSR → PR 에 그 비트 1 → **IMR 켜기(마지막)**. 인자 오류 `EXTI_EINVAL`, 라인이 다른 포트 소유면 `EXTI_EBUSY` (둘 다 레지스터 접근 0번).
- `exti_ack(line)` (PR 에 그 비트만), `exti_mask(line)`, `exti_unmask(line)` (PR 먼저 지우고 IMR 켜기).
- 검증: 9 포트 × 16 라인 × 3 엣지 = **432 조합**을 모든 레지스터를 무작위로 채운 상태에서 돌려 "그 니블/비트만 바뀌었나, 다른 PR 비트 보존, 쓰기 순서" 를 본다. SWIER 로 ISR 경로 자가 진단, 부팅 직후 가짜 인터럽트 0.

### Part B — 디바운서 1: EXTI + 원샷 타이머 재확인

- `dbx_exti_isr()` (EXTI15_10 핸들러) — 내 라인의 PR 이 아니면 가짜 진입, 맞으면 **마스크 → PR 지우기 → TIM7 원샷 20 ms** (진행 중이던 롱프레스 예약은 취소).
- `dbx_tim_isr()` (TIM7 핸들러) — UIF 확인/지우기 → (SETTLE) 레벨 1번 읽기 → 확정 상태와 다르면 PRESS/RELEASE → `exti_unmask` → **다시 읽어** 다르면 재예약 → 눌림이면 남은 시간으로 롱프레스 예약 (HOLD) → (HOLD) LONG_PRESS 1번.
- 주입하는 결함: 0.1–26 ms 글리치, 0–8 ms 무작위 바운스 150회 (절반은 "마스크 중 래치" 모델), 누른 채 튐(롱프레스 타이밍 유지), 마감 직전 튐, **재확인 읽기 직후의 누름**(접근 훅), 같은 틱의 TIM7 만료 + 떼는 엣지, 9.9 s 고착, 부팅 때 눌림, 2 s 접점 불량.

### Part C — 디바운서 2: 1 ms 샘플링, 비교, 비용

- `dbs_tick()` (SysTick 1 ms) — `hist = (hist << 1 | 눌림) & 0x3FF`, 10개 연속 같으면 상태 확정, 확정 눌림 800 ms → LONG 1번.
- 두 방식을 **같은 입력에 동시에** 돌린다: 120 세션 × 4 누름(무작위 길이·바운스)에서 이벤트 열 일치, 시간 경계 검사. 1 ms 폭 펄스가 20 ms 마다 오는 글리치 트레인에서는 갈린다.
- 이벤트는 ISR → 64칸 SPSC 링 → 메인 루프 `app_poll()`. 10 s 세션의 ISR 비용 표와 브링업 로그를 찍는다.

## 2. Problem (English)

> **Debounced push-button driver on STM32F4-style EXTI (behavioural mock, 0.1 ms simulation step).**
>
> Button on PC13, active low (pressed = 0). EXTI registers at 0x40013C00: IMR +0x00, EMR +0x04, RTSR +0x08, FTSR +0x0C, SWIER +0x10, PR +0x14 (rc_w1). Line→port mux: SYSCFG_EXTICR1..4 at 0x40013808 + 4·(line>>2), 4 bits per line at bit 4·(line&3), needs RCC_APB2ENR.SYSCFGEN (bit 14). TIM7 is a one-shot timer helper (`tim7_oneshot(ticks)` → one UIF after `ticks` steps). NVIC-lite dispatches pending+enabled IRQs lowest number first; pending is latched.
>
> ```c
> int  exti_line_to_irqn(unsigned line);                          /* 0-4→6-10, 5-9→23, 10-15→40 */
> int  exti_config(unsigned port, unsigned pin, unsigned edges);  /* only that line's fields change */
> void exti_ack(unsigned line); void exti_mask(unsigned line); void exti_unmask(unsigned line);
> void dbx_exti_isr(void);   /* edge → mask line, ack, start 20 ms one-shot                     */
> void dbx_tim_isr(void);    /* re-check level, emit PRESS/RELEASE/LONG_PRESS, unmask           */
> void dbs_tick(void);       /* alternative: 1 ms sampling, 10 equal samples to change state    */
> ```
> Events: PRESS, RELEASE, LONG_PRESS (debounced press held ≥ 800 ms, at most once per press). A button already held at boot produces no PRESS/LONG_PRESS, only RELEASE when let go.
>
> Requirements: glitches ≤ 20 ms never produce events (EXTI method); bounce on press and on release yields exactly one event each; no event storm (ISR rate bounded while the contact chatters); `exti_config` from reset produces exactly: `APB2ENR 0→0x4000`, `EXTICR4 0→0x0020`, `RTSR=FTSR=0x2000`, `PR←0x2000`, `IMR 0→0x2000` (last write).
>
> Follow-ups: Why mask the line instead of re-arming the timer on every edge? Why clear PR right before unmasking? What happens if you write `PR |= bit`? How do you detect a long press when no edge occurs while held? Which method lets the MCU sleep in Stop mode?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/BM-M03_st && /tmp/BM-M03_st   # 29/72 passed, exit 1
```

시작 코드에는 mock 코어(BM-B02 축약판 + BM-B04 식 접근 훅), EXTI·SYSCFG·GPIO IDR·TIM7 모델(BM-B14 타이머 모델 축약판), NVIC-lite, 이벤트 큐, 신호 생성기, 테스트 72개가 다 들어 있다. TODO 는 6 곳: A1 `exti_line_to_irqn`, A2 `exti_config`, A3 `exti_ack/mask/unmask`, B1 `dbx_exti_isr`, B2 `dbx_tim_isr`, C1 `dbs_tick`. 스텁 상태에서는 모델 자체 테스트, 잘못된 인자 거부, "글리치는 이벤트 없음" 같은 테스트만 통과한다 (29/72).

시뮬레이션 한 스텝의 순서는 고정이다: 핀 갱신 → EXTI 엣지 검출 → TIM7 카운트 → SysTick(10 스텝마다) → NVIC-lite 디스패치(번호 작은 IRQ 먼저) → 메인 루프 `app_poll()`. 그래서 "엣지가 틱 t 에 오면 EXTI ISR 도 틱 t, 재확인은 t+200" 이 정확히 맞아떨어진다.

관찰용 파일 (레슨 폴더에서 빌드 — `solution.c` 를 include):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bugs.c  -o /tmp/BM-M03_bugs  && /tmp/BM-M03_bugs    # 버그 7종 재현
cc -std=c11 -Wall -Wextra -Werror -O2 demo_sweep.c -o /tmp/BM-M03_sweep && /tmp/BM-M03_sweep   # 글리치·바운스 한계
cc -std=c11 -Wall -Wextra -Werror -O2 demo_trace.c -o /tmp/BM-M03_trace && /tmp/BM-M03_trace   # 레지스터 접근 로그
```

## 4. 힌트

<details><summary>힌트 1 — EXTICR 은 어느 레지스터, 몇 번째 니블?</summary>

라인 하나에 4비트, 레지스터 하나에 라인 4개. 레지스터 = `0x40013808 + 4·(line >> 2)`, 시프트 = `4·(line & 3)`. 라인 13 → EXTICR4 의 [7:4]. 값은 포트 번호 (A=0, B=1, C=2 …). 지우고 넣기(clear + set)를 잊지 말 것 — 같은 레지스터에 라인 12, 14, 15 가 같이 산다.
</details>

<details><summary>힌트 2 — 설정 순서가 왜 중요한가</summary>

리셋 직후 EXTICR 은 0 = 포트 A. PA13 은 0, PC13 은 풀업이라 1 이다. EXTICR 을 C 로 바꾸는 순간 mux 출력이 0→1 로 바뀐다 — 엣지 검출기 입장에서는 진짜 상승 엣지와 구분이 안 된다. 그래서 **마스크한 상태에서** mux 와 엣지를 바꾸고, PR 을 지운 뒤, 마지막에 unmask 한다.
</details>

<details><summary>힌트 3 — PR 은 W1C</summary>

`REG_WRITE(EXTI_PR, REG_READ(EXTI_PR) | bit)` 는 "지금 1 인 모든 비트에 1 을 쓰기" = 남의 펜딩까지 전부 지운다. 지울 비트 하나만 쓴다: `REG_WRITE(EXTI_PR, bit)`. (BM-B04 의 W1C 함정 그대로.)
</details>

<details><summary>힌트 4 — EXTI 핸들러는 짧게, 판단은 타이머에서</summary>

엣지 ISR 에서 레벨을 읽어 판단하면 바운스 한가운데를 읽는다. ISR 은 "무언가 바뀌기 시작했다" 만 기록한다: 마스크 → PR 지우기 → 20 ms 원샷. 판단은 20 ms 뒤 타이머 ISR 에서, **확정 상태(stable)와 비교**해서 한다. 엣지 방향(상승/하강)을 믿지 말고 레벨을 믿는다.
</details>

<details><summary>힌트 5 — unmask 와 다시 읽기</summary>

재확인 읽기와 unmask 사이에 핀이 바뀌면? 그 엣지는 마스크 중이라 PR 에 안 남는다 (기본 모델). 그래서 unmask 뒤에 **한 번 더 읽어** 확정 상태와 다르면 다시 마스크하고 재예약한다. 반대로 "마스크 중에도 래치" 하는 하드웨어라면 바운스가 PR 에 남아 있으니 unmask **직전에** PR 을 지운다 — `exti_unmask` 가 둘 다 처리한다.
</details>

<details><summary>힌트 6 — 롱프레스와 타이머 하나 돌려쓰기</summary>

누르고 있는 동안에는 엣지가 없다 → 인터럽트 방식에서는 타이머가 필요하다. PRESS 를 낸 순간 `tim7_oneshot(800 ms − 경과)` 로 HOLD 상태에 들어간다. 그 사이 엣지가 오면 EXTI ISR 이 20 ms 재확인으로 타이머를 **덮어쓴다** (= 롱프레스 예약 취소). 재확인 결과가 "여전히 눌림" 이면 남은 시간으로 다시 HOLD.
</details>

<details><summary>힌트 7 — 같은 틱에 두 IRQ</summary>

NVIC 펜딩은 래치된다. EXTI ISR 이 `tim7_oneshot` 으로 UIF 를 지워도, 그 전에 TIM7 펜딩이 이미 섰다면 TIM7 ISR 은 들어온다. 그래서 TIM7 ISR 첫 줄은 "UIF 가 정말 1 인가" — 아니면 아무것도 하지 않고 나간다.
</details>

<details><summary>힌트 8 — 시프트 레지스터 디바운서</summary>

`hist` 는 최근 10 샘플. 놓음 확정에서 `hist == 0x3FF` 이면 PRESS, 눌림 확정에서 `hist == 0` 이면 RELEASE. 롱프레스는 `now - press_t >= 800 ms` (wrap-safe 뺄셈, BM-B13).
</details>

## 5. 풀이

### Part A — 레지스터 규칙과 실제 접근 순서

| 레지스터 | 주소 | 접근 | 이 레슨에서 쓰는 비트 |
|---|---|---|---|
| RCC_APB2ENR | 0x40023844 | rw | bit 14 SYSCFGEN |
| SYSCFG_EXTICR1–4 | 0x40013808 … 0x40013814 | rw | 라인당 4비트, 값 = 포트(0=A … 8=I) |
| EXTI_IMR / EMR | 0x40013C00 / 04 | rw | 라인 비트 (EMR 은 이벤트 출력 — 건드리지 않는다) |
| EXTI_RTSR / FTSR | 0x40013C08 / 0C | rw | 상승/하강 엣지 선택 (둘 다 켜면 양쪽) |
| EXTI_SWIER | 0x40013C10 | rw | 0→1 쓰기 = 소프트웨어 엣지 |
| EXTI_PR | 0x40013C14 | rc_w1 | 1 쓰면 지워짐 |

`exti_config(PORT_C, 13, EXTI_BOTH)` 를 리셋 상태에서 부르면 mock 접근 로그가 정확히 이렇다 (실제 출력, `demo_trace.c`):

| # | 접근 | 주소 | 값 | 의미 |
|---|---|---|---|---|
| 1–3 | R, W, R | APB2ENR | 0 → 0x00004000 → 0x00004000 | SYSCFG 클럭 + 읽어 보기 (BM-B03) |
| 4–5 | R, W | IMR | 0 → 0x00000000 | 라인 13 마스크 |
| 6–7 | R, W | EXTICR4 (0x40013814) | 0 → 0x00000020 | [7:4] = 2 → PC13. 이 순간 mux 출력 0→1 (가짜 상승 엣지, 마스크라 PR 안 섬) |
| 8–11 | R, R, W, W | RTSR, FTSR | 0x00002000, 0x00002000 | 양쪽 엣지 |
| 12 | W | PR | 0x00002000 | 그 비트만 W1C |
| 13–14 | R, W | IMR | 0 → 0x00002000 | 마지막에 unmask |

라인 소유권: EXTICR 은 라인당 포트 하나라서 `exti_config(PORT_A, 13, …)` 는 PC13 이 이미 쓰고 있으면 `EXTI_EBUSY` 다. 하드웨어는 거부하지 않고 그냥 mux 를 바꿔 PC13 버튼이 조용히 죽는다 — 그래서 드라이버가 표로 막는다.

exhaustive 결과 (실제 출력):

```text
  exti_config exhaustive: 432 cases, field 0, other-bits 0, order 0, PR 0
```

각 케이스는 APB2ENR·EXTICR1–4·IMR·EMR·RTSR·FTSR·PR 를 무작위로 채우고 시작한다. 검사: 목표 니블 = 포트, 같은 레지스터의 다른 니블과 다른 EXTICR 그대로, RTSR/FTSR/IMR 의 다른 비트 그대로, EMR 그대로, PR = 이전 PR 에서 그 비트만 0, 쓰기 순서 APB2ENR < IMR(끄기) < EXTICR < PR, 마지막 쓰기 = IMR(켜기), PR 쓰기 값 = 정확히 `1 << pin`.

### Part B — 디바운서 1 의 상태 머신

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">EXTI + 원샷 재확인 디바운서 (dbx)</text>
  <rect x="30" y="110" width="120" height="56" rx="10" class="f-ok" stroke="currentColor"/>
  <text x="90" y="134" fill="currentColor" text-anchor="middle" font-weight="bold">IDLE</text>
  <text x="90" y="152" fill="currentColor" text-anchor="middle">라인 unmask</text>
  <rect x="310" y="110" width="140" height="56" rx="10" class="f-warn" stroke="currentColor"/>
  <text x="380" y="134" fill="currentColor" text-anchor="middle" font-weight="bold">SETTLE</text>
  <text x="380" y="152" fill="currentColor" text-anchor="middle">마스크, 20 ms 원샷</text>
  <rect x="590" y="110" width="130" height="56" rx="10" class="f-acc" stroke="currentColor"/>
  <text x="655" y="134" fill="currentColor" text-anchor="middle" font-weight="bold">HOLD</text>
  <text x="655" y="152" fill="currentColor" text-anchor="middle">unmask, 롱프레스 예약</text>
  <g stroke="currentColor" fill="none">
    <path d="M150 124 H306"/><path d="M300 119 l8 5 l-8 5"/>
    <path d="M310 156 H156"/><path d="M162 151 l-8 5 l8 5"/>
    <path d="M450 124 H586"/><path d="M580 119 l8 5 l-8 5"/>
    <path d="M590 156 H456"/><path d="M462 151 l-8 5 l8 5"/>
    <path d="M655 166 V230 H90 V170"/><path d="M85 176 l5 -8 l5 8"/>
    <path d="M360 110 C340 60, 420 60, 400 106"/><path d="M394 100 l6 8 l3 -9"/>
  </g>
  <g fill="currentColor">
    <text x="228" y="116" text-anchor="middle">EXTI 엣지 (PR &amp; 내 라인)</text>
    <text x="228" y="178" text-anchor="middle">타이머: 레벨 == 확정 상태</text>
    <text x="228" y="193" text-anchor="middle">(놓음 확정 or 롱 이미 냄)</text>
    <text x="518" y="100" text-anchor="middle">타이머: 눌림 확정,</text>
    <text x="518" y="116" text-anchor="middle">롱프레스 전 (PRESS)</text>
    <text x="518" y="178" text-anchor="middle">엣지 → 재확인으로</text>
    <text x="518" y="193" text-anchor="middle">타이머 덮어씀 (롱 취소)</text>
    <text x="380" y="248" text-anchor="middle">타이머 (HOLD): 아직 눌림 → LONG_PRESS 1번</text>
    <text x="380" y="52" text-anchor="middle">unmask 뒤 다시 읽어 다르면 재마스크·재예약 (n_rearm)</text>
    <text x="10" y="272">모든 전이는 ISR 안에서 일어난다. 확정 상태(stable)가 바뀔 때만 PRESS/RELEASE 를 낸다</text>
    <text x="10" y="290">→ 가짜 ISR 진입(묵은 PR, 래치된 NVIC 펜딩)은 가짜 이벤트가 되지 않는다.</text>
  </g>
</svg>
```

실제 접근 로그 — 깨끗한 누름, 엣지 틱 1000 (실제 출력 발췌):

```text
-- tick 1000 EXTI ISR
R 0x40013C14 0x00002000      PR: 내 라인 맞음
R 0x40013C00 0x00002000      IMR 읽고
W 0x40013C00 0x00000000      마스크
W 0x40013C14 0x00002000      PR W1C
W 0x40001400 0x00000008      TIM7 정지 (OPM)
W 0x4000142C 0x000000C7      ARR = 199 → 200 틱
W 0x40001414 0x00000001      UG (CNT=0, UIF 섬)
W 0x40001410 0xFFFFFFFE      rc_w0 로 UIF 지움
W 0x4000140C 0x00000001      UIE
W 0x40001400 0x00000009      CEN|OPM
-- tick 1200 TIM7 ISR
R 0x40001410 0x00000001      UIF 진짜
W 0x40001410 0xFFFFFFFE
R 0x40020810 0x00000000      PC13 = 0 → 눌림, 확정 상태와 다름 → PRESS @1200
W 0x40013C14 0x00002000      unmask: PR 먼저
R 0x40013C00 0x00000000
W 0x40013C00 0x00002000      그리고 IMR
R 0x40020810 0x00000000      다시 읽기: 그대로
W 0x4000142C 0x00001F3F      (중략) ARR = 7999 → 800 ms 롱프레스 예약 (HOLD)
```

(설명 칸은 이 문서에서 붙였다. 로그의 R/W·주소·값은 프로그램 출력 그대로.)

**글리치 경계.** 첫 엣지가 틱 t 면 판단은 정확히 t+200 의 레벨 한 번이다. 그래서 길이 L 의 펄스는 L ≤ 200 틱(20.0 ms)이면 무시, L ≥ 201 이면 누름(PRESS @t+200, RELEASE @t+L+200). 테스트가 L = 1…260 전부를 확인한다.

**롱프레스.** PRESS 시각 + 800 ms 에 정확히 1번. 누른 채 +500 ms 에 2 ms 튀어도 재확인이 "여전히 눌림" 이므로 남은 280 ms 로 다시 예약 → LONG 은 제때(+800 ms). 마감 직전(+785 ms)에 튀면 재확인이 +805 ms 에 끝나므로 LONG 은 +805 ms — 최대 T_db 늦다 (설계상 한계, 테스트로 고정).

**마스크 창 안의 누름.** 0.3 ms 글리치로 마스크가 걸린 상태에서, 재확인의 IDR 읽기 **직후**(접근 훅) 진짜 누름이 시작돼 그대로 유지된다. 이 엣지는 마스크 중이라 PR 에 안 남는다. 다시 읽기가 있으면 재예약 → PRESS @t+400 (`n_rearm == 1`), 없으면 PRESS 가 영영 안 나온다 (demo_bugs [3]).

**같은 틱의 두 IRQ.** HOLD 예약이 끝나는 틱(PRESS+800 ms)에 마침 손을 뗀다. 디스패치는 EXTI15_10(40) 먼저 → 재확인 예약이 TIM7 의 UIF 를 지운다 → 그래도 TIM7(55) 펜딩은 이미 래치돼 진입 → UIF = 0 → `n_spurious == 1`, 아무것도 안 함. 결과: LONG 없음, RELEASE @+820 ms. "정확히 800 ms 에 떼면 롱프레스가 아니다" 가 이 설계의 정의다.

**채터링 / 고착 (실제 출력).**

```text
  bouncy press/release x150: pin edges 6012, EXTI ISR 300, bad 0
  chatter 2 s: pin edges 10110, EXTI ISR 100, TIM7 ISR 100, events 46
```

0–8 ms 바운스 150번(절반은 "마스크 중 래치" 모델): 핀 엣지 6012개가 ISR 300번(누름·뗌 각 1번)으로 줄고, 이벤트 시각은 전부 정확히 첫 엣지 + 20 ms. 2 s 동안 0.1 ms 마다 무작위인 접점 불량에서는 엣지 10110개에 EXTI ISR 100번 — 20 ms 에 최대 1번이라는 **상한**이 마스크 덕분에 생긴다. 다만 매 재확인이 무작위 레벨을 읽으므로 이벤트가 46개 나온다 (번갈아 PRESS/RELEASE, 끝나면 놓음으로 수렴 — 테스트가 이 불변식을 본다). 9.9 s 고착: PRESS, LONG 1번, EXTI ISR 1번. 부팅 때 눌림: RELEASE 1개만.

### Part C — 1 ms 샘플링과 두 방식 비교

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">같은 누름, 두 디바운서 (가로 20 px = 1 ms, 바운스 7 ms 예)</text>
  <g fill="currentColor"><text x="10" y="58">PC13</text><text x="10" y="112">EXTI 방식</text><text x="10" y="176">SysTick 샘플</text><text x="10" y="236">이벤트</text></g>
  <path d="M100 40 H130 V70 H150 V40 H170 V70 H200 V40 H215 V70 H240 V40 H270 V70 H720" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="104" y="86" fill="currentColor">↑ 첫 엣지 (0 ms)</text>
  <text x="276" y="58" fill="currentColor">↓ 마지막 엣지 (7 ms)</text>
  <rect x="124" y="98" width="6" height="20" class="f-acc" stroke="currentColor"/>
  <rect x="130" y="100" width="400" height="16" class="f-mut" stroke="currentColor"/>
  <text x="330" y="112" fill="currentColor" text-anchor="middle">ISR: 마스크 → IMR = 0 (바운스 엣지 무시) 20 ms</text>
  <rect x="530" y="98" width="6" height="20" class="f-warn" stroke="currentColor"/>
  <text x="542" y="112" fill="currentColor">TIM7: 레벨 1번 읽기</text>
  <g stroke="currentColor"><path d="M145 160v20M165 160v20M185 160v20M205 160v20M225 160v20M245 160v20M265 160v20M285 160v20M305 160v20M325 160v20M345 160v20M365 160v20M385 160v20M405 160v20M425 160v20M445 160v20M465 160v20"/></g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle"><text x="145" y="196">1</text><text x="165" y="196">0</text><text x="185" y="196">1</text><text x="205" y="196">0</text><text x="225" y="196">1</text><text x="245" y="196">0</text><text x="265" y="196">0</text><text x="285" y="196">1</text><text x="305" y="196">2</text><text x="325" y="196">3</text><text x="345" y="196">4</text><text x="365" y="196">5</text><text x="385" y="196">6</text><text x="405" y="196">7</text><text x="425" y="196">8</text><text x="445" y="196">9</text><text x="465" y="196">10</text></g>
  <text x="490" y="176" fill="currentColor">← 연속 눌림 샘플 수</text>
  <rect x="457" y="220" width="16" height="22" class="f-ok" stroke="currentColor"/>
  <text x="440" y="262" fill="currentColor" text-anchor="middle">S PRESS 16.75 ms</text>
  <text x="440" y="278" fill="currentColor" text-anchor="middle">(마지막 엣지 + 9.75 ms)</text>
  <rect x="525" y="220" width="16" height="22" class="f-ok" stroke="currentColor"/>
  <text x="610" y="262" fill="currentColor" text-anchor="middle">E PRESS 20.0 ms</text>
  <text x="610" y="278" fill="currentColor" text-anchor="middle">(첫 엣지 + 20 ms)</text>
</svg>
```

| | EXTI + 재확인 (dbx) | 1 ms 샘플링 (dbs) |
|---|---|---|
| PRESS 시각 | 첫 엣지 + 20.0 ms (정확) | [첫 엣지 + 9 ms, 마지막 엣지 + 9.9 ms] |
| 무시하는 글리치 | ≤ 20.0 ms 전부 | ≤ 9.0 ms 전부, ≥ 10.0 ms 는 항상 검출, 그 사이는 위상에 따라 |
| 판단 근거 | 20 ms 뒤 **샘플 1개** | **연속 10 샘플** |
| CPU 가 깨는 횟수 | 엣지 묶음당 ~2번 (버튼 안 누르면 0) | 1000번/초, 항상 |
| Stop 모드 | EXTI 가 깨움 → 그대로 잘 맞음 (BM-M11) | 1 kHz 틱이 잠을 깸 |

두 방식 동시 실행 결과 (실제 출력):

```text
  agree: 120 sessions, 480 presses (234 long), seq 0, E-time 0, S-time 0, long 0
  glitch train (1 ms every 20 ms): EXTI-method events 46, sampling events 0
  cost over 10 s session: pin edges 2012 (= naive ISR-per-edge), EXTI-method ISR 67 (EXTI 33 + TIM7 34), sampling ISR 9999; events E 29 / S 19
```

- 깨끗하거나 0–8 ms 튀는 누름 480개(롱 234개): 이벤트 열 완전 일치, EXTI 쪽 시각은 전부 정확히 +20 ms, 샘플 쪽은 전부 위 구간 안, LONG − PRESS = 800 ms 정확.
- **글리치 트레인** (1 ms 펄스가 20 ms 마다 — 모터 PWM 커플링 같은 주기 잡음): 재확인 순간이 펄스와 겹치도록 맞추면 EXTI 방식은 46개의 가짜 이벤트, 샘플링은 0. "한 번 보고 믿는" 설계의 약점이다.
- 10 s 브링업 세션(클릭 9번 + 롱 1번 + 1 ms 글리치 + 300 ms 접점 불량): 엣지마다 ISR 이었다면 2012번, EXTI 방식 67번, 샘플링 9999번. 샘플링은 19개(정답), EXTI 방식은 접점 불량 구간에서 10개를 더 내 29개.

demo_sweep.c 의 실제 출력 (글리치 × 위상 10가지, 바운스 길이 × 300회):

```text
(1) 글리치 (놓인 버튼이 L 동안 0) — 위상 10가지 중 PRESS 가 나온 수
   L(ms)   EXTI+재확인(20 ms)   샘플링(1 ms x 10)
     8.9         0/10                 0/10
     9.0         0/10                 0/10
     9.1         0/10                 1/10
     9.5         0/10                 5/10
     9.9         0/10                 9/10
    10.0         0/10                10/10
    20.0         0/10                10/10
    20.1        10/10                10/10
(2) 바운스 길이 B (0.1 ms 마다 무작위 0/1, 누름·뗌 둘 다) — 600 ms 누름 300회
   B(ms)  E 틀림  S 틀림  E 지연 평균(ms)  S 지연 평균(ms)  E ISR/회  (지연 = 첫 엣지 → PRESS)
     0.0    0       0        20.00             9.41         4.00
     8.0    0       0        20.00            16.53         4.00
    20.0    0       0        20.00            28.32         4.00
    25.0    0       0        30.03            33.44         8.00
    40.0    0       0        30.44            47.77         8.00
    60.0  125       0        38.70            67.48        12.00
```

(행 일부만 옮김.) 바운스가 T_db(20 ms)보다 길어지면 EXTI 방식은 "다시 읽기 → 재예약" 덕에 40 ms 까지는 버티지만 지연이 늘고, 60 ms 에서는 300번 중 125번 틀린다. 샘플링은 무작위 바운스에서 10 연속이 우연히 나올 확률이 작아 안 틀리지만, 지연이 바운스 길이를 그대로 따라간다. **T_db 와 K 는 스위치 데이터시트의 bounce time 과 측정값으로 정한다.**

이벤트 전달: ISR 은 `ev_push()` 로 64칸 링(2의 거듭제곱, free-running 인덱스)에 넣고 메인 루프 `app_poll()` 이 꺼낸다. 두 디바운서의 ISR(EXTI15_10, TIM7, SysTick)은 우선순위가 같아 서로 선점하지 않으므로 생산자는 사실상 하나다 (우선순위가 다르면 생산자별 큐나 PRIMASK 구간 — C-M14, ARM-M10). 브링업 로그 (실제 출력):

```text
  bring-up log (first 8 events, both methods):
  [   509.0 ms] S PRESS
  [   520.0 ms] E PRESS
  [   659.0 ms] S RELEASE
  [   670.0 ms] E RELEASE
  [  1014.0 ms] S PRESS
  [  1020.0 ms] E PRESS
  [  1312.0 ms] S RELEASE
  [  1320.0 ms] E RELEASE
```

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/BM-M03_so && /tmp/BM-M03_so          # 72/72 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/BM-M03_as && /tmp/BM-M03_as   # 72/72, 진단 0
```

타깃 빌드: 맨 위 `#define UNIT_TEST` 를 빼면 `REG_READ/REG_WRITE` 가 volatile MMIO 로 바뀐다. 실보드에서는 `dbx_exti_isr` 를 `EXTI15_10_IRQHandler`, `dbx_tim_isr` 를 `TIM7_IRQHandler`, `dbs_tick` 을 `SysTick_Handler` 안에서 부르고, `sim_now()` 대신 SysTick ms 카운터를 쓴다 (그때 T_db/롱프레스 상수는 ms 단위로). NVIC enable 은 `NVIC_EnableIRQ(EXTI15_10_IRQn)` (BM-M01 이 레지스터 수준).

## 7. 자주 하는 실수

**1. `EXTI->PR |= bit`.** W1C 레지스터에 RMW 를 하면 읽은 순간 1 이던 비트가 전부 지워진다. demo_bugs [5] (실제 출력): `REG_WRITE(PR, REG_READ(PR) | bit) → PR 0x0000 → 라인 10 펜딩 사라짐 (이벤트 유실)`. 고친 코드: `REG_WRITE(EXTI_PR, 1u << line);`.

**2. 하강 엣지만 + "타이머 끝에 아직 0 이면 PRESS" + unmask 전에 PR 안 지움.** 흔한 인터넷 예제 구조다. demo_bugs [1] (실제 출력, 누름 1번):

```text
  mask→ack, unmask 때 PR 안 지움           default  PRESS 1  (1번 눌렀다)  EXTI ISR 2
  mask→ack, unmask 때 PR 안 지움           latch    PRESS 2  (1번 눌렀다)  EXTI ISR 4
  ack→mask (창에 바운스 주입), PR 안 지움 default  PRESS 2  (1번 눌렀다)  EXTI ISR 3
  NVIC disable 로 막음, 재허용 전 정리 없음 default  PRESS 2  (1번 눌렀다)  EXTI ISR 4
  NVIC disable 로 막음, EXTI PR + ICPR 정리 default  PRESS 1  (1번 눌렀다)  EXTI ISR 2
```

묵은 PR 이 unmask 하자마자 ISR 을 부르고, 버튼은 아직 눌려 있으니 "0 이면 PRESS" 가 한 번 더 나온다. 원인이 셋 — 마스크 중에도 PR 을 래치하는 하드웨어, "PR 지우고 → 마스크" 순서의 창, NVIC 로만 막아 EXTI PR 과 NVIC 펜딩이 쌓인 경우. 고친 코드: ISR 은 마스크 → ack 순서, unmask 는 `exti_unmask()` (PR 먼저), 그리고 **확정 상태를 추적**해서 "바뀌었을 때만" 이벤트. 같은 결함 셋을 정답 디바운서에 넣으면 전부 `PRESS 1 RELEASE 1` (demo_bugs [2]).

**3. SYSCFG 클럭을 안 켜고 EXTICR 에 쓰기.** 쓰기가 무시되어 EXTICR 이 0 = 포트 A 그대로 → PA13 을 보고 있으니 PC13 버튼이 반응하지 않는다. mock 은 클럭이 꺼져 있으면 쓰기 무시·읽기 0 (모델 테스트가 확인). 고친 코드: `REG_WRITE(RCC_APB2ENR, REG_READ(RCC_APB2ENR) | RCC_APB2ENR_SYSCFGEN); (void)REG_READ(RCC_APB2ENR);`.

**4. IMR/RTSR 을 먼저 켜고 EXTICR 을 나중에.** mux 가 PA13(0) → PC13(1) 로 바뀌는 순간 상승 엣지 → 부팅 직후 가짜 ISR. demo_bugs [4] (실제 출력): `solution exti_config  EXTI ISR 0` vs `IMR/RTSR 먼저, EXTICR 나중  EXTI ISR 1`. 고친 코드: 마스크 → mux → 엣지 → PR 지움 → unmask.

**5. unmask 뒤 다시 읽기를 빼먹음.** demo_bugs [3] (실제 출력): `unmask 뒤 다시 읽기 없음  PRESS 0 (버튼은 780 ms 째 눌려 있음)`. 재확인 읽기와 unmask 사이의 변화는 PR 에 안 남는다. 고친 코드: `exti_unmask(line); if (btn_raw_pressed(...) != stable) { exti_mask(line); exti_ack(line); tim7_oneshot(DB_TICKS); }`.

**6. 공유 벡터에서 PR 을 안 보고 "내 인터럽트" 라고 가정.** EXTI15_10 은 라인 10–15 공용이고, TIM7 처럼 펜딩이 래치된 가짜 진입도 있다. 핸들러 첫 줄은 항상 "내 플래그가 1 인가".

**7. 마스크 없이 엣지마다 타이머 재시작 (retrigger).** "마지막 엣지 뒤 20 ms 조용하면 확정" — 이벤트는 정답(19개)이지만 ISR 이 엣지 수만큼 돈다. demo_bugs [6] (실제 출력): `retrigger (마스크 없음) pin edges 2012, EXTI ISR 2012, TIM7 ISR 20, events 19` vs `solution (마스크) EXTI ISR 33, TIM7 ISR 34, events 29`. 접점 불량 환경이면 ISR 폭풍 + 끝없는 재시작. 마스크는 ISR 비용에 **상한**을 준다 (대신 위의 "1 샘플 판단" 약점이 생긴다 — 트레이드오프).

**8. `uint8_t hist` 에 10 샘플.** `hist == 0x3FF` 가 영원히 거짓 → PRESS 가 안 나온다. 샘플 수 K ≤ 비트 수, 마스크 `(1u << K) - 1u` 로 자른다.

## 8. 면접 Q&A

<details><summary>Q1. 버튼을 EXTI 엣지마다 그대로 이벤트로 쓰면 무엇이 문제인가요? 얼마나 튀나요?</summary>

기계식 접점은 붙고 떨어질 때 수 ms 동안 수십 번 튄다. 이 레슨 10 s 세션에서 클릭 10번이 핀 엣지 2012개였다 (모델 입력이지만 규모는 현실적). Ganssle 의 측정 글("A Guide to Debouncing")에 따르면 대부분의 스위치는 수 ms 안에 안정되지만 수십 ms 를 넘는 것도 있다고 한다 (원문 확인 권장). 엣지 = 이벤트면 한 번 누름이 여러 번이 되고, ISR 폭풍으로 CPU 를 잡아먹는다. 그래서 (1) 하드웨어 RC + 슈미트 트리거, (2) 소프트웨어 디바운스 — 재확인 또는 주기 샘플링 — 중 하나 이상이 필요하다.
</details>

<details><summary>Q2. EXTI 핸들러에서 마스크와 PR 클리어 순서는? unmask 할 때는?</summary>

ISR: **마스크 → PR 지우기**. 반대 순서면 둘 사이의 바운스 엣지가 PR 을 다시 세우고(라인이 아직 열려 있으니) 그 묵은 펜딩이 unmask 순간 ISR 을 부른다 — demo_bugs 에서 "ack→mask + 창에 바운스" 가 PRESS 2. unmask: **PR 지우기 → IMR 켜기**, 그다음 레벨을 다시 읽어 확정 상태와 비교. "마스크 중에도 PR 래치" 하는 하드웨어(계열·구현에 따라 다를 수 있어 RM 확인 권장)에서도, 래치 안 하는 하드웨어에서도 같은 코드가 맞다.

꼬리: "그럼 PR 을 ISR 맨 끝에서 지우면?" — 엣지 하나 늦게 지우는 문제가 아니라 ISR 동안 들어온 엣지를 버리는 것이고, 반대로 bus write buffer 때문에 플래그가 늦게 내려가 ISR 이 재진입하는 문제는 BM-M01 의 "late flag clear" 다.
</details>

<details><summary>Q3. PA13 과 PC13 을 둘 다 인터럽트로 받을 수 있나요? EXTI9_5 핸들러는 어떻게 쓰나요?</summary>

STM32F4 의 GPIO EXTI 라인은 16개(라인 번호 = 핀 번호)이고 라인마다 EXTICR 로 포트 하나만 고른다. 그래서 같은 핀 번호의 두 포트는 동시에 못 받는다 — 보드 설계 단계에서 핀 번호를 겹치지 않게 배정해야 한다 (이 레슨 드라이버는 `EXTI_EBUSY`). 라인 5–9 와 10–15 는 IRQ 하나(23, 40)를 공유하므로 핸들러가 `PR & IMR & 그룹마스크` 를 돌면서 비트별로 ack 하고 해당 콜백을 부른다 (추가 연습 P3). 그룹 안의 두 라인이 같은 틱에 펜딩이어도 하나도 잃지 않는다.
</details>

<details><summary>Q4. 롱프레스는 누르고 있는 동안 엣지가 없는데 인터럽트 방식에서 어떻게 검출하나요?</summary>

타이머가 필요하다. PRESS 를 확정하는 순간 "800 ms − 경과" 원샷을 걸고 HOLD 로 간다. 그 사이 엣지(떼거나 튐)가 오면 EXTI ISR 이 같은 타이머를 20 ms 재확인으로 덮어써서 예약이 취소되고, 재확인이 "여전히 눌림" 이면 남은 시간으로 다시 건다 — 누른 채 2 ms 튀어도 LONG 이 제때 1번 나온다 (테스트). `long_fired` 플래그로 누름당 1번. 마감 직전의 튐은 LONG 을 최대 T_db 늦춘다 (+805 ms, 테스트로 고정). SysTick 이 이미 있다면 HOLD 를 "1 ms 틱에서 deadline 비교" (BM-B13 wrap-safe) 로 해도 된다.
</details>

<details><summary>Q5. 재확인(EXTI) 방식과 주기 샘플링 방식, 언제 무엇을 쓰나요?</summary>

| | EXTI + 재확인 | 1 ms 샘플링 |
|---|---|---|
| 전력 | 버튼 안 누르면 CPU 0회 → Stop 모드에서 EXTI 로 깨움 (BM-M11) | 1 kHz 로 계속 깸 |
| 잡음 내성 | 샘플 1개로 판단 → 주기 잡음에 속음 (글리치 트레인 46 vs 0) | 10 연속 필요 → 강함 |
| 지연 | 첫 엣지 + T_db 고정 | 마지막 엣지 + K ms 정도 |
| 버튼 여러 개 | 라인·IRQ·타이머 자원 필요 | 한 틱에서 포트 IDR 한 번 읽어 전부 처리 (키 매트릭스도) |

배터리 기기 = EXTI 로 깨운 뒤 깨어 있는 동안만 샘플링하는 혼합형이 흔하다. 시스템에 이미 1 ms 틱(RTOS)이 있고 버튼이 많으면 샘플링이 단순하다.
</details>

<details><summary>Q6. 같은 틱에 TIM7 만료와 EXTI 엣지가 둘 다 오면 무슨 일이 생기나요?</summary>

NVIC 펜딩은 래치된다 — 라인이 내려가도 진입하거나 ICPR 로 지우기 전에는 남는다. 디스패치 순서상 EXTI(40) 가 먼저 돌고 `tim7_oneshot` 이 UIF 를 지워도, TIM7(55) 펜딩은 이미 섰으므로 TIM7 ISR 이 들어온다. 그래서 핸들러 첫 줄에서 UIF 를 확인하고 0 이면 나간다 (`n_spurious == 1` 테스트). 이 확인이 없으면 이미 취소된 롱프레스를 내 버린다. 실제 칩에서 같은 우선순위끼리는 번호 작은 쪽이 먼저라는 규칙은 ARMv7-M 기준 (ARM ARM 확인 권장, 선점·그룹 우선순위는 BM-M02).
</details>

<details><summary>Q7. NVIC_DisableIRQ 로 막는 것과 EXTI IMR 로 막는 것은 무엇이 다른가요?</summary>

NVIC 로 막아도 EXTI 는 계속 엣지를 보고 PR 을 세우며(IMR=1 이니까) NVIC 펜딩도 쌓인다. 다시 enable 하는 순간 묵은 펜딩으로 바로 진입한다 — demo_bugs 에서 PRESS 2. 막을 거라면 재허용 전에 EXTI PR 과 NVIC ICPR 둘 다 지운다 (추가 연습 P4: IRQ 40 → `ICPR1` bit 8, 그다음 `ISER1` bit 8). IMR 로 막으면 그 라인만 막히고 같은 IRQ 를 공유하는 다른 라인(10–15)은 살아 있다는 장점도 있다.
</details>

<details><summary>Q8. 하드웨어 디바운스(RC)만으로 충분한가요?</summary>

RC 로 엣지를 느리게 하면 튐이 걸러지지만, 느린 엣지가 입력 문턱 근처에서 머무는 동안 잡음 때문에 여러 번 넘나들 수 있다. 그래서 히스테리시스(슈미트 트리거)가 같이 필요하다. STM32 GPIO 입력에는 슈미트 트리거가 있다고 데이터시트에 나오지만 히스테리시스 폭이 작으므로(데이터시트 확인 권장) 외부 74LVC1G17 같은 버퍼를 두기도 한다. 실무에서는 RC(+ESD 보호)로 큰 튐과 ESD 를 막고 소프트웨어로 마무리하는 이중 구조가 흔하다 — 소프트웨어 디바운서는 어차피 글리치·EMI 를 위해 필요하다.
</details>

<details><summary>Q9. 디바운스 시간 20 ms 는 어떻게 정했나요? 너무 길거나 짧으면?</summary>

스위치 데이터시트의 bounce time(보통 수 ms, 부품마다 다름)과 실제 보드에서 스코프/로직 분석기로 잰 값(HWLAB-M05)에 여유를 둔다. 너무 짧으면 바운스 한가운데를 읽는다 — demo_sweep 에서 바운스가 T_db 를 넘으면 지연이 늘고(25 ms 바운스 → 평균 30 ms), 3배(60 ms)면 300번 중 125번 틀린다. 너무 길면 반응이 굼뜨고 빠른 연타를 놓친다(20 ms 보다 짧은 누름은 무시). 사람은 대략 수십 ms 이상의 지연부터 굼뜨다고 느낀다는 경험칙이 있어 10–50 ms 사이를 많이 쓴다 (경험칙, 제품 요구사항으로 정할 것).
</details>

## 9. 추가 연습

### P1 — 적분기(integrator) 디바운서

Ganssle 글에 소개된 Kuhn 의 적분기 방식(원문 확인 권장): 카운터 `cnt ∈ [0, K]`, 눌림 샘플이면 +1, 놓임 샘플이면 −1, `cnt == K` 에서 눌림 확정, `cnt == 0` 에서 놓임 확정. 300 ms 누름 동안 각 샘플이 drop% 확률로 '놓임' 으로 읽히는 낡은 스위치에서 시프트 레지스터와 지연을 비교하라.

<details><summary>답과 실제 출력</summary>

```text
P1  drop%  integ 지연(ms)  shift 지연(ms)  integ 틀림  shift 틀림   (200회 평균, 지연 = 누름 시작 → PRESS)
      0         9.00           9.00          0           0
     10        11.28          16.33          0           0
     20        15.47          40.34          0           0
     30        21.89          99.08          3           9
```

잡음이 없으면 둘 다 정확히 9 ms (10번째 샘플). 잡음이 있으면 시프트 레지스터는 "10 연속" 을 기다리느라 지연이 급격히 늘고 (20% → 40 ms), 30% 에서는 300 ms 안에 한 번도 10 연속이 안 나와 **누름을 놓치는** 경우가 9번. 적분기는 평균 경향(+0.4/샘플)을 따라가 빨리 확정하지만, 30% 에서는 누름 중간에 카운터가 0 까지 떨어져 **가짜 RELEASE+PRESS** 가 3번. 반대 샘플 1개(ms 5)에 대해 적분기는 한 칸만 물러나 ms 11 에 확정, 시프트는 처음부터 다시 세어 ms 15 (테스트로 고정).
</details>

### P2 — 오토 리피트

LONG_PRESS 뒤 누르고 있는 동안 200 ms 마다 REPEAT 를 낸다 (볼륨 버튼). 확정 레벨을 받는 1 ms 상태 머신 `rep_step()` 으로.

<details><summary>답</summary>

LONG 을 낼 때 `next_rep = now + 200`, 이후 `(int32_t)(now - next_rep) >= 0` 이면 REPEAT 하고 `next_rep += 200` (누적 오차 없음, wrap-safe — BM-B13). 1500 ms 누름(100–1599 ms): PRESS@100, LONG@900, REPEAT@1100/1300/1500, RELEASE@1600 → REPEAT 3개. 999 ms → LONG 만, 799 ms → PRESS/RELEASE 만, 5000 ms → REPEAT 20개 (1100 … 4900).
</details>

### P3 — 공유 벡터 디스패처

버튼 두 개가 PB10(라인 10)과 PC13(라인 13)에 있다. EXTI15_10 핸들러를 짜서 같은 틱에 둘 다 펜딩이어도 콜백이 각각 1번씩 불리게 하라. 마스크된 라인(12)과 다른 벡터의 라인(3)의 펜딩은 건드리지 않는다.

<details><summary>답</summary>

`act = PR & IMR & 0xFC00` 을 구해 비트마다 `REG_WRITE(EXTI_PR, 1u << line)` 후 콜백, `act &= act - 1`. 결과 PR = 라인 12, 3 만 남음. 비교용 버그 버전(`PR |=` + 첫 비트만)은 라인 10 콜백 1번 뒤 PR = 0 — 13 과 3 의 펜딩까지 사라진다.
</details>

### P4 — NVIC 로 막았던 라인 다시 허용하기

디바운서가 `NVIC_DisableIRQ(EXTI15_10_IRQn)` 로 막는 설계라면, 다시 허용하는 함수 `line_irq_reenable(line, irqn)` 를 레지스터로 짜라. 조건: 재허용 직후 가짜 진입 0, 다른 IRQ 의 펜딩(IRQ 35) 보존, 쓰기 3번.

<details><summary>답</summary>

`REG_WRITE(EXTI_PR, 1u << line); REG_WRITE(NVIC_ICPR1, 1u << (irqn & 31)); REG_WRITE(NVIC_ISER1, 1u << (irqn & 31));` — IRQ 40 은 워드 `40 >> 5 = 1`, 비트 `40 & 31 = 8`. ICPR/ISER 는 1 쓴 비트만 작용하므로 RMW 가 필요 없다 (0 쓰기는 무시 — NVIC 레지스터 규칙은 BM-M01). 순서: 주변장치 플래그 → NVIC 펜딩 → enable. enable 만 하면 묵은 펜딩으로 즉시 진입한다 (테스트가 양쪽 확인).
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-M03_pr && /tmp/BM-M03_pr   # 15/15 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o /tmp/BM-M03_pas && /tmp/BM-M03_pas
```

## 10. 핵심 정리

- EXTI 경로 = 핀 → `EXTICR[line>>2]` 니블 `4·(line&3)` (포트 번호, SYSCFG 클럭 필요) → RTSR/FTSR → IMR → PR(rc_w1) → IRQ 6–10 / 23 / 40. 라인당 포트 하나.
- 설정 순서: 클럭 → **마스크** → mux → 엣지 → PR 지움 → **unmask 마지막**. mux 변경도 엣지를 만든다.
- PR 은 `REG_WRITE(PR, bit)`. `|=` 는 남의 펜딩까지 지운다.
- 디바운서 1: 엣지 ISR = 마스크 → ack → 20 ms 원샷. 타이머 ISR = 플래그 확인 → 레벨 1번 → **확정 상태와 다를 때만** 이벤트 → PR 지우고 unmask → 다시 읽기.
- 롱프레스는 엣지가 없으니 타이머(HOLD)로, 누름당 1번. 같은 틱의 두 IRQ → 래치된 펜딩 → 핸들러는 항상 자기 플래그부터 본다.
- 디바운서 2: 1 ms × 10 연속 샘플. 잡음에 강하고 버튼 여러 개에 싸지만 늘 깨어 있다. EXTI 방식은 잠자기 좋고 ISR 수에 상한이 있지만 샘플 1개로 판단해 주기 잡음에 속는다.
- 숫자 (이 모델): 무시 글리치 EXTI ≤ 20.0 ms / 샘플링 ≤ 9.0 ms, 10 s 세션 엣지 2012 → ISR 67 (EXTI) / 9999 (샘플링).

## 참고자료

- STMicroelectronics, RM0090 Reference Manual (STM32F405/415, F407/417, F427/437, F429/439) — §EXTI (블록도, IMR/RTSR/FTSR/SWIER/PR), §SYSCFG_EXTICR1–4, §RCC_APB2ENR, 벡터 표 (EXTI0–4, EXTI9_5, EXTI15_10, TIM7). 이 레슨의 주소·비트·IRQ 번호는 여기 기준 대표값 — 확인 권장.
- ST Community, "Question about STM32 EXTI" — 마스크(IMR=0) 중 엣지가 PR 에 남는지에 대한 실험 토론: https://community.st.com/t5/stm32-mcus-products/question-about-stm32-exti/m-p/514469
- ST Community, "EXTI pending bit not cleared by edge detector setting change as promised" — 감도 변경으로 PR 이 지워진다는 RM 문구 관련: https://community.st.com/t5/stm32-mcus-products/exti-pending-bit-not-cleared-by-edge-detector-setting-change-as/td-p/540470
- Jack G. Ganssle, "A Guide to Debouncing" (스위치 바운스 측정, 적분기·시프트 레지스터 디바운서) — ganssle.com
- ARM, ARMv7-M Architecture Reference Manual — NVIC 펜딩 래치, ISER/ICER/ISPR/ICPR.
- 이 노트의 연결 레슨: BM-B02 (mock), BM-B03 (클럭 + 읽어 보기), BM-B04 (W1C, 끼어들기 훅), BM-B11 (GPIO 입력·풀업), BM-B13 (wrap-safe 시간), BM-B14 (타이머 UG/UIF), BM-M11 (저전력 — EXTI 깨움), C-M14 (ISR↔main 공유 데이터), DS 링 버퍼 레슨, HWLAB-M05 (로직 분석기로 바운스 재기). 다음 단계: BM-M01 (NVIC 레지스터 + late flag clear), BM-M02 (우선순위·선점), ARM-M10 (PRIMASK/BASEPRI), ARM-A08 (인터럽트 지연), BM-M06 (입력 캡처의 IC1F 디지털 필터).
