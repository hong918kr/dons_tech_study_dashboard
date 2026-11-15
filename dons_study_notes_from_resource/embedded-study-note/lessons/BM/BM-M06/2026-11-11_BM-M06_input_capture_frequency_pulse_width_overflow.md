---
id: BM-M06
time: 80분
updated: 2026-11-11
---
# BM-M06 입력 캡처: 주파수·펄스폭 측정과 오버플로 처리

## 학습 목표

- 입력 캡처(input capture)의 레지스터 동작을 모델로 구현하고 설명한다: 에지 순간의 CNT → CCRx 복사, CCxIF(CCRx 를 읽으면 지워짐), 오버캡처 CCxOF, ICxPSC 분주, CC2S=10 으로 같은 핀을 두 번째 채널에 연결.
- 16비트 카운터를 overflow 횟수로 **32비트 타임스탬프로 확장**하고, 캡처와 업데이트(UIF)가 **같은 ISR 진입에서 둘 다 pending** 일 때 순서를 `CCR < 0x8000` 규칙으로 판정한다 — 그리고 이 규칙이 맞는 조건(ISR 지연 < 반 바퀴 = 32768 tick)을 전수 테스트로 증명한다.
- 오버캡처·읽기 순서(CCR 먼저, OF 나중)·두 채널 처리 순서·신호 끊김 같은 면접 단골 경계를 주입하고 결과를 단언한다.
- 주기·듀티를 정수로 계산하고 ground truth 대비 **ppm 오차**로 보고하며, PSC 를 분해능 vs overflow 빈도로 고른다.
- ISR → main 데이터 전달을 seqlock 으로 찢어짐 없이 하고, 이벤트 구동 시뮬레이션으로 브링업 엔지니어가 읽을 타코미터 로그를 만든다.

## 1. 문제

"16비트 타이머로 1 Hz 신호를 어떻게 재죠? 캡처랑 오버플로 인터럽트가 동시에 pending 이면요?" — 입력 캡처는 펌웨어 면접에서 타이머 문제의 두 번째 단계이고, 실제로는 팬 타코미터, 모터 홀 센서·엔코더 속도, RC 수신기 PWM, 초음파 에코 폭, GPS PPS 타임스탬프가 전부 이 구조다. RF 모듈 보드에서 PPS 와 내부 클럭을 비교해 ppm 오차를 재던 일, SSD 보드의 팬 RPM 감시가 같은 문제다.

STM32 범용 타이머(TIM2–TIM5, RM0090 기준 대표값 — RM 확인 권장)의 캡처 채널은 핀의 에지가 오는 순간 **CNT 값을 CCRx 에 복사**하고 `SR.CCxIF` 를 세운다. CPU 가 늦게 와도 값은 하드웨어가 찍은 그 순간의 것이다 — 이것이 "GPIO 인터럽트에서 타이머를 읽는 것" 과의 결정적 차이(지터 0)다. 문제는 CNT 가 16비트라는 것: 84 MHz 로 세면 **780 µs 마다 한 바퀴** 돈다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">입력 캡처 경로 (TIM3, 이 레슨의 모델 — RM0090 기준 단순화)</text>
  <rect x="10" y="50" width="70" height="40" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="75" fill="currentColor" font-family="monospace">TI1 핀</text>
  <rect x="105" y="50" width="120" height="40" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="113" y="68" fill="currentColor" font-size="12">동기화 + IC1F</text>
  <text x="113" y="84" fill="currentColor" font-size="12">(N 샘플 필터)</text>
  <rect x="250" y="35" width="165" height="34" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="258" y="57" fill="currentColor" font-size="12">IC1: CC1S=01, 상승</text>
  <rect x="250" y="80" width="165" height="34" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="258" y="102" fill="currentColor" font-size="12">IC2: CC2S=10, CC2P=1 하강</text>
  <rect x="430" y="35" width="82" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="437" y="57" fill="currentColor" font-size="12">IC1PSC ÷n</text>
  <rect x="530" y="35" width="200" height="34" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="538" y="57" fill="currentColor" font-family="monospace" font-size="12">CCR1←CNT, CC1IF (+CC1OF)</text>
  <rect x="530" y="80" width="200" height="34" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="538" y="102" fill="currentColor" font-family="monospace" font-size="12">CCR2←CNT, CC2IF (+CC2OF)</text>
  <rect x="10" y="170" width="90" height="40" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="195" fill="currentColor" font-family="monospace">f_tim 84M</text>
  <rect x="125" y="170" width="110" height="40" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="133" y="195" fill="currentColor" font-family="monospace">÷(PSC+1)</text>
  <rect x="260" y="170" width="200" height="40" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="268" y="195" fill="currentColor" font-family="monospace">CNT 0…0xFFFF (16비트)</text>
  <rect x="485" y="170" width="110" height="40" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="493" y="195" fill="currentColor" font-family="monospace">UIF (랩)</text>
  <rect x="300" y="250" width="430" height="66" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="310" y="270" fill="currentColor">SR &amp; DIER → TIM3 IRQ 29 → ISR (NVIC-lite)</text>
  <text x="310" y="290" fill="currentColor" font-family="monospace">ovf += UIF;  ext32 = (ovf' &lt;&lt; 16) | CCRx</text>
  <text x="310" y="308" fill="currentColor" font-size="12">ovf': 같이 pending 이면 CCR 크기로 판정 (Part B)</text>
  <g stroke="currentColor" fill="none">
    <path d="M80 70 L105 70"/><path d="M225 70 L237 70 L237 52 L250 52"/><path d="M237 70 L237 97 L250 97"/>
    <path d="M415 52 L430 52"/><path d="M512 52 L530 52"/><path d="M415 97 L530 97"/>
    <path d="M100 190 L125 190"/><path d="M235 190 L260 190"/><path d="M460 190 L485 190"/>
    <path d="M360 170 L360 135 L620 135 L620 114" stroke-dasharray="5 3"/>
    <path d="M540 210 L540 250"/><path d="M700 114 L700 250"/>
  </g>
  <text x="372" y="150" fill="currentColor" font-size="12">에지 순간의 CNT 값이 복사된다 (CPU 지연과 무관)</text>
</svg>
```

시뮬레이션 규약은 BM-M 공통이다: 시간 단위는 **타이머 클럭**(84 MHz → 11.9 ns), 주변장치 모델이 `irq_line_set(TIM3_IRQn, level)` 로 선을 올리고, `sim_step()` 이 클럭 1개를 진행한 뒤 선이 enable + pending 이고 마스크/실행 중이 아니면 핸들러를 동기 호출한다. `g_mask_until` (더 높은 우선순위 ISR·임계 구역 흉내), `g_isr_cost` (ISR 1회가 CPU 를 잡는 클럭 수), 접근 횟수 훅(BM-B04 식 "이 두 접근 사이에 X 가 끼어든다")으로 경계 상황을 결정적으로 만든다. 1 Hz 신호 3 주기(2.5억 클럭)도 돌릴 수 있도록 **이벤트 구동 빨리감기**(다음 에지·overflow·마스크 해제까지 카운터를 산술로 점프)를 넣었고, 매 클럭 실행과 결과가 같은지 테스트로 확인한다.

### Part A — 캡처 메커니즘과 32비트 확장

- `ic_capture_edge(ch)` (모델): ICxPSC 분주(div 번째 에지마다), CCxIF 가 이미 1 이면 CCxOF, `CCRx = CNT`, CCxIF = 1.
- 주어진 모델 규칙: SR 은 rc_w0, 입력 모드의 CCRx 는 읽기 전용이고 **읽으면 CCxIF 가 지워진다**, CCxE=0 이면 IC 프리스케일러 리셋, EGR.UG 는 PSC shadow 로드 + CNT=0 (+UIF).
- `ic_extend(ovf, ccr, uif_pending)` → 32비트 타임스탬프 `(hi << 16) | ccr`.
- 검증: 위 규칙을 레지스터로 직접 확인, IC1PSC 4 코드, PSC=3 이면 캡처값 = ⌊t/4⌋, 1 kHz 25 % → 84000/21000 tick, 그리고 무작위 1500 신호(20 Hz–200 kHz, 듀티 5–95 %, PSC 4종, 위상 임의)의 주기·high 가 모두 진짜 값과 **1 tick 미만** 차이.

### Part B — 면접관이 찌르는 경계

- **캡처 + 업데이트 동시 pending**: CNT 를 랩 직전으로 맞추고 상승 에지를 랩 앞뒤 모든 위치(CCR 값 65536 개 전부)에 두고, ISR 지연 L 을 0/1/100/32767 tick 으로 바꿔 전수 검사. 흔한 첫 시도 ① "overflow 먼저 센다", ② "캡처 먼저 처리한다" 가 각각 몇 건 틀리는지 센다. 지연 한계 32767 / 32768 tick 경계를 단언.
- 아주 낮은 주파수: 1 Hz (주기 사이 overflow 1281 번), 그리고 stall 타임아웃을 너무 짧게 잡으면 1 Hz 를 "신호 없음" 으로 판정하는 설정 버그.
- 측정할 수 있는 것보다 빠른 신호: ISR 1회 300 클럭, 신호 주기 250 클럭 → 오버캡처. 잃은 캡처를 버리는 드라이버 vs OF 를 무시하는 드라이버. IC1PSC=/8 로 해결.
- 읽기 순서 경쟁: SR 스냅샷과 CCR1 읽기 사이에 1 ms 선점 → CCR1 이 다음 캡처로 덮였는데 OF 를 스냅샷에서만 보면 엉뚱한 주기를 발행.
- 두 채널 순서: 지난 주기의 하강과 다음 상승이 한 ISR 에서 같이 pending.
- 신호 끊김(팬 정지): stall_ovf 번 overflow 동안 상승이 없으면 무효 + 사슬 끊기, 신호가 돌아오면 재동기.

### Part C — 펌웨어처럼 쓰기

- `ic_init(psc, icpsc, duty, stall_ovf)` — 접근 로그로 순서 검사 (CEN=0 → CCER=0 → … → UG → SR=0 → DIER → CEN).
- `TIM3_IRQHandler()` → `ic_shared_t` 에 seqlock 으로 발행, main 은 `ic_get()` 으로 읽는다. "main 이 period 를 복사한 직후 ISR 이 새 값을 발행" 을 훅으로 주입해 찢어진 (period, high) 쌍이 안 나오는지 확인.
- 400 ms 프로파일(1 kHz → 20 kHz → 50 Hz → 정지 → 3.333 kHz)을 이벤트 구동으로 돌리며 main 이 20 ms 마다 순간 주파수·듀티·창 평균 주파수(reciprocal counting)·ppm 오차를 찍는 로그.

## 2. Problem (English)

> **Measure frequency and duty cycle with a 16-bit input-capture timer.**
>
> TIM3 counts at `f_tim/(PSC+1)` with `ARR = 0xFFFF` (free-running). TI1 is routed to CH1 (rising edge, `CC1S=01`)
> and to CH2 (falling edge, `CC2S=10`, `CC2P=1`). The update interrupt (`UIF`) and both capture interrupts are enabled.
>
> 1. `uint32_t ic_extend(uint32_t ovf, uint32_t ccr, bool uif_pending)` — extend a 16-bit capture to 32 bits given the
>    number of overflows already handled and whether an overflow is still pending in the same `SR` snapshot.
>    State the condition under which your rule is always correct.
> 2. `void TIM3_IRQHandler(void)` — read `SR` once; for each pending capture read `CCRx` **then** check `CCxOF`
>    (discard on overcapture); process captures in time order; count the overflow; clear handled flags with a single
>    rc_w0 write. Publish `period` and `high` (in ticks) of each completed cycle.
> 3. `int ic_init(uint32_t psc, uint32_t icpsc, bool duty, uint32_t stall_ovf)` and `int ic_get(ic_meas_t *m)`
>    (consistent snapshot, ISR may run at any point of the copy).
>
> Examples (f_tim = 84 MHz, PSC = 0): 1 kHz 25 % → `period = 84000`, `high = 21000`, 1000.000 Hz, 25.00 %.
> 1 Hz → `period = 84000000` (1281 overflows per period). Capture at `CCR = 0xFFE8` with `UIF` pending → upper half
> = `ovf`; capture at `CCR = 0x0010` with `UIF` pending → upper half = `ovf + 1`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크가 깨져 있을 때
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "2/41 passed" — TODO 5곳(A1, A2, B, C1, C2)을 채워 41/41 을 만든다
./solution -v     # (정답 빌드 후) 랩 경계 표, 오버캡처·경쟁 결과, 타코미터 로그
```

TODO B 자리는 "흔한 첫 시도" 로 채워져 있다 (overflow 를 먼저 세고, OF·순서를 안 봄). A1·A2·C1 을 먼저 채우면 이 첫 시도가 랩 경계에서 정확히 몇 건 틀리는지 숫자로 나온다. 모델(타이머·NVIC-lite·입력 이벤트·빨리감기)과 `ic_on_rise/ic_on_fall/ic_chain_break` 같은 발행 헬퍼는 주어진다.

타깃 빌드: 맨 위 `#define UNIT_TEST 1` 을 지우면 `REG_READ/REG_WRITE` 가 `*(volatile uint32_t *)addr` 가 되고 드라이버(`ic_extend`, ISR, `ic_init`, `ic_get`)는 그대로다. `nvic_enable_irq` 자리는 CMSIS `NVIC_EnableIRQ(TIM3_IRQn)` (BM-M01), 그 전에 RCC 클럭(BM-B03)과 PA6 를 AF2(TIM3_CH1, F4 대표값 — 데이터시트 확인 권장)로 설정(BM-B11)이 필요하다.

## 4. 힌트

<details>
<summary>힌트 1 — 캡처 모델은 네 줄</summary>

`div = 1 << ICxPSC` 번째 에지마다 한 번만 캡처. 캡처 순간: "CCxIF 가 이미 1 이면 CCxOF" 를 **먼저** 판단하고, 그 다음 `CCRx = CNT`, `CCxIF = 1`. 순서를 바꾸면 OF 가 절대 안 선다.

</details>

<details>
<summary>힌트 2 — overflow 가 캡처 전이었나 후였나</summary>

UIF 가 pending 이 아니면 쉽다: 상위 = ovf. UIF 가 pending 이면 둘 중 하나다. 캡처가 overflow **후** 라면 CCR 은 "overflow 이후 흐른 tick 수" 라 작다 (ISR 이 늦어도 지연보다 작다). 캡처가 overflow **전** 이라면 CCR 은 "랩까지 남은 거리" 가 작다, 즉 CCR 이 크다. 경계를 반 바퀴(0x8000)에 두면 지연 < 32768 tick 일 때 두 경우가 겹치지 않는다.

</details>

<details>
<summary>힌트 3 — 왜 CCR 을 먼저, OF 를 나중에 읽나</summary>

SR 스냅샷 뒤에 새 캡처가 오면 CCR 이 덮이고 CC1OF 가 선다. 스냅샷의 OF 만 보면 "덮인 값" 을 정상으로 쓴다. CCR 을 읽은 **뒤에** SR 을 다시 읽어 OF 를 보면, 내가 읽은 값이 덮인 값인지 알 수 있다 (RM 의 오버캡처 처리 권고와 같은 순서 — RM 확인 권장).

</details>

<details>
<summary>힌트 4 — 두 캡처가 같이 오면</summary>

CH1(상승)과 CH2(하강)가 한 ISR 에서 둘 다 pending 이면, 채널 번호 순서가 아니라 **확장 타임스탬프 순서**로 처리한다. `(int32_t)(e2 − e1) < 0` 이면 e2 가 먼저다 (BM-B13 의 wrap-safe 비교).

</details>

<details>
<summary>힌트 5 — seqlock</summary>

ISR 은 발행할 때 `seq++ → 필드 쓰기 → seq++`. main 은 `s1 = seq → 복사 → s2 = seq`, `s1 == s2` 이고 짝수일 때만 채택, 아니면 다시. 단일 코어에서 ISR 은 main 을 끊을 수 있지만 그 반대는 없으므로 이것으로 충분하다.

</details>

## 5. 풀이

### Part A — 레지스터 규칙과 한 번의 트레이스

| 레지스터/비트 | 접근 | 이 모델의 동작 (RM0090 기준 대표값 — RM 확인 권장) |
|---|---|---|
| `CCMR1.CC1S[1:0]` | rw | 01 = IC1 ← TI1. `CC2S[9:8]` = 10 이면 IC2 ← TI1 (같은 핀을 두 번째 채널로). CCxE=0 일 때만 바꾸라는 것이 RM 규칙 |
| `CCMR1.IC1PSC[3:2]` | rw | 00 /1, 01 /2, 10 /4, 11 /8 — n 번째 에지마다 캡처. CCxE=0 이면 분주 카운터 리셋 |
| `CCER.CC1P/CC1NP` | rw | 입력: 00 상승, 01 하강, 11 양쪽 |
| `CCRx` (입력 모드) | r | 캡처값. **읽으면 CCxIF 가 지워진다**. 쓰기는 무시 |
| `SR.CCxIF` | rc_w0 | 캡처 때 1. CCRx 읽기 또는 0 쓰기로 지움 |
| `SR.CCxOF` | rc_w0 | CCxIF 가 1 인데 또 캡처되면 1. 0 쓰기로만 지움 |
| `SR.UIF` | rc_w0 | 랩(0xFFFF → 0) 또는 UG(URS=0) 때 1 |

Part A 첫 테스트(CH1 상승, CH2 하강, PSC=0, 드라이버 없음)를 클럭 단위로 따라가면:

| 시각 | 사건 | CCR1 | CCR2 | SR (CC2OF CC1OF … CC2IF CC1IF) |
|---|---|---|---|---|
| 100 | ↑ 캡처 | 100 | 0 | CC1IF |
| 150 | CPU 가 CCR1 읽음 → 100 | 100 | 0 | 0 (읽기가 CC1IF 를 지움) |
| 300 | ↓ 캡처 | 100 | 300 | CC2IF |
| 1100 | ↑ 캡처 | 1100 | 300 | CC2IF, CC1IF |
| 1500 | ↓, CC2IF 아직 1 | 1100 | 1500 | **CC2OF**, CC2IF, CC1IF (300 은 잃음) |
| 1700 | ↑, CC1IF 아직 1 | 1700 | 1500 | **CC1OF**, CC2OF, CC2IF, CC1IF = 0x0606 |
| — | `SR = ~CC1OF` | 1700 | 1500 | 0x0406 (rc_w0: 1 을 쓴 비트는 그대로) |

**32비트 확장.** ISR 이 처리한 overflow 수를 `ovf` 라 하면 캡처 시각은 `ext = ovf·65536 + CCR` (ARR = 0xFFFF 라서 `(ovf << 16) | CCR`). ext 를 uint32 로 두면 주기 = `ext₂ − ext₁` 이 2³² 랩에서도 맞는다 (BM-B13). 84 MHz, PSC=0 에서 2³² tick = 51.1 s 이므로 이보다 긴 주기만 아니면 된다.

**주파수·듀티 (정수, 반올림).** 카운터 tick 은 (PSC+1)/f_tim 초.

```c
f_mHz   = round(f_tim·1000·div / ((PSC+1)·period))     /* div = IC 프리스케일러 (1, 2, 4, 8) */
duty_bp = round(high·10000 / period)                  /* 0.01 % 단위 */
```

**정확도.** 에지가 클럭 사이에 오면 다음 클럭에 보이고(`ceil`), 카운터는 (PSC+1) 클럭마다 한 칸(`floor`)이라 각 타임스탬프의 오차는 (−1, +1/p) tick 안에 있다. 두 개의 차이인 주기는 진짜 값과 **1 tick 미만** 차이 — 테스트의 `within_tick()` 이 바로 이 부등식 `|ticks·(PSC+1)·T − F·div| < (PSC+1)·T` 이다. 무작위 1500 신호(실측 `무작위 신호: 1500 건 측정, 1 tick 이상 틀림 0 건`)의 주기와 high 모두 이 안에 들어왔다. 실제 칩은 입력 동기화·필터 때문에 2–3 클럭 늦게 캡처하지만, **모든 에지에 같은 지연이면 차이에서 사라진다** (연습 2 에서 확인).

### Part B — 캡처와 overflow 가 같이 pending 일 때

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">랩 경계: ISR 이 늦게 와서 UIF 와 CC1IF 가 같이 서 있다</text>
  <line x1="60" y1="250" x2="720" y2="250" stroke="currentColor"/>
  <line x1="60" y1="250" x2="60" y2="40" stroke="currentColor"/>
  <text x="14" y="55" fill="currentColor" font-family="monospace" font-size="12">0xFFFF</text>
  <text x="30" y="254" fill="currentColor" font-family="monospace" font-size="12">0</text>
  <polyline points="60,150 360,50 360,250 660,150" fill="none" stroke="currentColor" stroke-width="2"/>
  <line x1="60" y1="150" x2="720" y2="150" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="664" y="145" fill="currentColor" font-family="monospace" font-size="12">0x8000</text>
  <circle cx="330" cy="60" r="6" class="f-acc" stroke="currentColor"/>
  <text x="170" y="80" fill="currentColor" font-size="12">A: CCR=0xFFE8 (랩 전)</text>
  <circle cx="390" cy="240" r="6" class="f-ok" stroke="currentColor"/>
  <text x="215" y="236" fill="currentColor" font-size="12">B: CCR=0x0010 (랩 후) →</text>
  <rect x="352" y="258" width="16" height="16" class="f-warn" stroke="currentColor"/>
  <text x="300" y="290" fill="currentColor" font-size="12">UIF (랩)</text>
  <line x1="470" y1="40" x2="470" y2="250" stroke="currentColor" stroke-width="2"/>
  <text x="476" y="52" fill="currentColor" font-weight="bold">ISR 진입: UIF + CC1IF</text>
  <path d="M360 268 L470 268" stroke="currentColor" fill="none"/>
  <text x="380" y="290" fill="currentColor" font-size="12">지연 L</text>
  <text x="476" y="80" fill="currentColor" font-size="12">CCR ≥ 0x8000 → 캡처가 먼저: 상위 = ovf</text>
  <text x="476" y="98" fill="currentColor" font-size="12">CCR &lt; 0x8000 → 랩이 먼저: 상위 = ovf+1</text>
  <text x="476" y="116" fill="currentColor" font-size="12">L &lt; 32768 tick 이면 항상 맞다</text>
</svg>
```

**규칙의 증명.** ISR 이 SR 스냅샷을 읽은 시각에 UIF 가 서 있다고 하자 (아니면 상위 = ovf 로 끝). 처리하지 않은 overflow 는 많아야 하나다 (지연 < 65536). 지연 L 은 "처리 안 된 가장 이른 사건" 부터 스냅샷까지의 tick 수다.

- 캡처가 overflow **후**: CCR = overflow 이후 흐른 tick ≤ L. L ≤ 32767 이면 CCR < 0x8000 → 규칙은 "후" 로 판정, 맞다.
- 캡처가 overflow **전**: 캡처가 가장 이른 사건이므로 L ≥ (랩까지 남은 거리) = 65536 − CCR, 즉 CCR ≥ 65536 − L ≥ 32769 → 규칙은 "전" 으로 판정, 맞다.

그러므로 **L ≤ 32767 tick 이면 항상 맞고**, L = 32768 이 처음 틀리는 곳이다 (캡처가 랩 후 정확히 32768 tick 에 오고 ISR 이 그때 진입). PSC=0, 84 MHz 에서 32767 tick = 390 µs, PSC=83 이면 32.8 ms — 다른 ISR·임계 구역이 이보다 길게 막으면 안 된다는 것이 이 설계의 **타이밍 계약**이다.

**전수 검사 (실측).** `wrap_case()` 는 CNT 를 65536 − W 로 맞춰 overflow 를 클럭 W 에 두고, 상승 에지를 클럭 d 에, ISR 을 `min(d, W) + L` 까지 마스크한다. W = 32768, d = 1…65536 이면 CCR 이 0…0xFFFF 를 한 번씩 지난다.

```text
랩 전수 (65536 × L∈{0,1,100,32767}): 정답 틀림 0 / ①ovf먼저 100 / ②캡처먼저 101 (L=100)
-- 랩 경계: overflow @ 클럭 1024, 상승 에지 @ d, ISR 은 첫 pending + 100 클럭 (UIF·CC1IF 둘 다 pending) --
   d    CCR1   진짜 확장   정답 오차   ①ovf먼저 오차   ②캡처먼저 오차
 1000  0xFFE8      65512         +0         +65536               +0
 1023  0xFFFF      65535         +0         +65536               +0
 1024  0x0000      65536         +0             +0           -65536
 1040  0x0010      65552         +0             +0           -65536
```

① 은 "랩 직전 L 개" (CCR 이 큰데 ovf+1 을 붙여 +65536), ② 는 "랩 직후 L+1 개" (CCR 이 작은데 ovf 를 붙여 −65536) 를 틀린다 — 101 인 이유는 랩과 같은 클럭의 캡처(CCR = 0)도 랩 후로 치기 때문이다. 둘 다 평소엔 맞고 **ISR 이 늦을 때만** 틀리므로 벤치에서 몇 시간 돌려도 안 보이다가 "가끔 주파수가 반으로 튄다" 로 나타난다. 한 바퀴 오차가 주기에 들어가면 84000 → 149536 tick 같은 값이 된다.

지연 한계를 CCR 전체에 대해 훑은 결과 (demo_sweeps.c, 실측):

```text
      L[tick]   틀림(overflow 전 캡처)   틀림(overflow 후 캡처)   PSC=0 일 때 L[us]
          0                        0                        0          0.0
      16384                        0                        0        195.0
      32766                        0                        0        390.1
      32767                        0                        0        390.1
      32768                        0                        1        390.1
      32769                        1                        2        390.1
      40000                     7232                     7233        476.2
      65535                    32767                    32768        780.2
```

증명대로 L = 32768 에서 "후" 쪽 1 건이 처음 틀리고, 그 뒤로 양쪽이 L − 32767, L − 32768 건씩 늘어난다.

### Part B — 오버캡처, 읽기 순서, ISR 예산

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">TIM3_IRQHandler 흐름 (정답)</text>
  <rect x="10" y="40" width="170" height="40" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="20" y="65" fill="currentColor" font-family="monospace">sr = SR (스냅샷)</text>
  <rect x="210" y="40" width="250" height="40" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="218" y="58" fill="currentColor" font-size="12">CCxIF ∈ sr → v = CCRx 먼저</text>
  <text x="218" y="74" fill="currentColor" font-size="12">(읽기가 CCxIF 를 지운다)</text>
  <rect x="490" y="40" width="240" height="40" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="498" y="58" fill="currentColor" font-size="12">SR 다시 → CCxOF? 값 버림,</text>
  <text x="498" y="74" fill="currentColor" font-size="12">OF 지울 목록, 사슬 끊기</text>
  <rect x="210" y="110" width="250" height="40" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="218" y="128" fill="currentColor" font-size="12">ext = ic_extend(ovf, v, sr.UIF)</text>
  <text x="218" y="144" fill="currentColor" font-size="12">(OF 아니면)</text>
  <rect x="490" y="110" width="240" height="40" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="498" y="128" fill="currentColor" font-size="12">캡처 2개 → (int32)(e2−e1) 로</text>
  <text x="498" y="144" fill="currentColor" font-size="12">시각순 정렬 → on_fall / on_rise</text>
  <rect x="210" y="180" width="250" height="40" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="218" y="198" fill="currentColor" font-size="12">UIF ∈ sr → ovf++, stall 카운트,</text>
  <text x="218" y="214" fill="currentColor" font-size="12">UIF 지울 목록 (확장 계산 뒤에!)</text>
  <rect x="490" y="180" width="240" height="40" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="498" y="205" fill="currentColor" font-family="monospace" font-size="12">SR = ~(UIF|CCxOF…) 한 번</text>
  <g stroke="currentColor" fill="none">
    <path d="M180 60 L210 60"/><path d="M460 60 L490 60"/><path d="M335 80 L335 110"/>
    <path d="M460 130 L490 130"/><path d="M610 150 L610 165 L335 165 L335 180"/><path d="M460 200 L490 200"/>
  </g>
  <text x="10" y="248" fill="currentColor" font-size="12">rc_w0 이므로 ~mask 쓰기는 mask 비트만 지우고 나머지(1 쓰기)는 그대로 둔다</text>
  <text x="10" y="266" fill="currentColor" font-size="12">→ SR &amp;= ~X 의 읽기-쓰기 사이 경쟁이 없다 (BM-B04)</text>
</svg>
```

**오버캡처.** 캡처는 하나도 소리 없이 사라지지 않는다 — 잃으면 반드시 CCxOF 가 선다. 그래서 정답은 OF 를 보면 그 값을 버리고 사슬을 끊는다 (`ic_chain_break`, 다음 상승 두 개로 재동기). 값을 "새 기준점" 으로 쓰지 않는 이유: 덮어쓴 캡처가 스냅샷 **뒤에** 왔을 수 있어 UIF 판정 근거가 없다. 신호 주기 250 클럭(336 kHz), ISR 1회 300 클럭 실측:

```text
오버캡처 (P=250, ISR 300 클럭): 정답 ovc=666 발행 1000 틀림 0 / OF 무시 발행 1666 틀림 341
```

정답은 잴 수 있는 주기만 1000 개 발행하고 틀린 값은 0, OF 를 무시하는 ISR 은 2·3 주기짜리를 341 번 "주기" 로 내보낸다. 같은 신호를 `IC1PSC = /8` (주파수만) 로 재면 캡처 간격이 2000 클럭이 되어 오버캡처 0, `period = 2000` tick → `ic_freq_mHz(…, div = 8)` = 336.000 kHz (테스트 단언). 듀티가 필요하면 IC 프리스케일러를 못 쓴다 (CH2 를 /8 로 하면 어느 주기의 하강인지 짝이 안 맞는다) — 그래서 `ic_init` 은 그 조합을 거절한다.

**읽기 순서 경쟁.** SR 스냅샷(CC1IF 만, OF 없음) → **여기서 1 ms 선점** → CCR1 읽기. 선점 동안 다음 상승이 CCR1 을 덮고 CC1OF 가 서고, overflow 도 하나 지나간다. 정답은 CCR1 을 읽은 뒤 SR 을 다시 읽어 OF 를 보고 버린다 (`ovc = 1`, 이후 주기 84000 으로 회복). OF 를 스냅샷에서만 보는 ③ 은 실측 `발행된 주기 102464 tick (진짜 84000)` — 덮인 CCR(0xB750)에 스냅샷의 "UIF 없음" 판정을 붙여 한 바퀴가 빠진 값이다.

**ISR 예산 (demo_sweeps.c 실측).** ISR 1회가 C 클럭 동안 CPU 를 잡을 때 오버캡처 없이 400 주기를 잴 수 있는 최소 신호 주기:

```text
  C[클럭] (us) |  듀티(CH1+CH2)        주파수만 /1          주파수만 IC1PSC=/8   | 20 kHz 에서 CPU: 듀티   /8
     42 (0.50) |  P≥  42 ( 2000.0 kHz)  P≥  42 ( 2000.0 kHz)  P≥   6 (14000.0 kHz) |    2.1%   0.19%
     84 (1.00) |  P≥  84 ( 1000.0 kHz)  P≥  84 ( 1000.0 kHz)  P≥  11 ( 7636.4 kHz) |    4.1%   0.38%
    168 (2.00) |  P≥ 168 (  500.0 kHz)  P≥ 168 (  500.0 kHz)  P≥  21 ( 4000.0 kHz) |    8.2%   0.75%
    336 (4.00) |  P≥ 336 (  250.0 kHz)  P≥ 336 (  250.0 kHz)  P≥  42 ( 2000.0 kHz) |   16.5%   1.50%
```

읽는 법: (1) 한계는 P ≈ C — 한 번 진입에서 CC1·CC2 를 같이 처리하므로 채널을 하나 더 써도 한계가 반이 되지는 않지만, 그 지점에서 CPU 는 이미 ~100 % 다. (2) 실제 설계 한계는 다른 ISR 몫과 위 390 µs 지연 계약을 같이 보고 잡는다. (3) /8 은 ISR 을 1/8 로 줄이지만 모델의 수 MHz 는 **모델의 한계**다 — 실제 핀은 f_CK_INT 로 동기화·필터링되므로 펄스 폭이 몇 클럭은 돼야 한다 (RM 확인 권장). 그 이상은 캡처가 아니라 **게이트 카운팅**(신호로 카운터를 직접 세는 외부 클럭 모드 + 다른 타이머로 게이트, Q5)의 영역이다.

### Part B — 낮은 주파수, 두 채널 순서, 신호 끊김

- **1 Hz, 50 %**: 주기 84 000 000 tick, high 42 000 000, 1000 mHz, 끝날 때 `ovf = 3845` (2억 5천만 클럭 ÷ 65536 — 단언). 주기마다 overflow ISR 이 1281 번이지만 그 ISR 은 몇 줄이다 (84 MHz/65536 = 1281.7 회/s).
- **stall 타임아웃은 1/f_min 보다 길게**: 같은 1 Hz 를 `stall_ovf = 128` (99.86 ms) 로 재면 상승마다 사슬이 끊겨 `valid = 0`, `stalls = 3` — 영원히 "신호 없음". 실제 팬 타코미터에서 "저속에서 RPM 이 0 으로 보여요" 의 원인.
- **두 채널 순서**: 1 kHz 75 % 에서 하강(+63000)과 다음 상승(+84000)을 둘 다 넘겨 ISR 을 마스크하면 한 진입에 CC1IF·CC2IF 가 같이 있다. 채널 번호 순서(CH1 먼저)로 처리하면 on_rise 가 high 없이 주기를 발행하고, 이어서 on_fall 이 `하강 − 새 상승` = 음수(uint32 로는 거대한 값)를 high 로 기록한다. 정답은 시각 순서로 정렬해 바로 `period = 84000, high = 63000` 을 발행한다 (단언).
- **신호 끊김 → 복귀**: 1 kHz 10 주기 후 정지 → 마지막 상승 + 128 overflow 에 `valid = 0, stalls = 1, breaks = 1, rises = 0` → 210 ms 에 2 kHz 가 돌아오면 상승 두 개 만에 `42000 / 21000`. 0 % 인지 100 % 인지는 캡처로는 모른다 — stall 때 핀 레벨(GPIO IDR)을 읽어 구분한다.

**PSC 트레이드오프 (demo_sweeps.c 실측, 신호 주파수는 일부러 나눠떨어지지 않게):**

```text
 PSC  tick[ns]  랩[ms]   UIF/s  |  f_sig[Hz]   주기[tick]  한주기 최악[ppm]  1/주기[ppm]  평균(전체)[ppm]
   0      11.9    0.78  1281.7  |   1234.567      68040.0              14.0         14.7           +0.00   (n=199)
   0      11.9    0.78  1281.7  |  20987.654       4002.4             161.7        249.9           -0.10   (n=199)
   0      11.9    0.78  1281.7  |  98765.432        850.5             587.9       1175.8           +0.00   (n=199)
   7      95.2    6.24   160.2  |  20987.654        500.3            1410.9       1998.8           +2.40   (n=199)
  83    1000.0   65.54    15.3  |     52.346      19103.7              34.4         52.3           +0.00   (n=199)
  83    1000.0   65.54    15.3  |   1234.567        810.0            1233.8       1234.6           -5.70   (n=199)
  83    1000.0   65.54    15.3  |  98765.432         10.1           86419.8      98765.4         +374.30   (n=199)
```

(전체 15 줄 중 발췌.) 한 주기 오차의 상한은 1 tick / 주기 = "1/주기" 열이고 실측 최악이 항상 그 아래다. PSC 를 키우면 overflow 인터럽트가 1281 → 15 회/s 로 줄고 지연 계약도 390 µs → 32.8 ms 로 느슨해지지만, 높은 주파수의 분해능이 무너진다 (PSC=83 에서 98.8 kHz 는 한 주기 10 tick, 8.6 % 오차). **여러 주기를 평균**(첫 상승과 마지막 상승 사이 시간 ÷ 주기 수, reciprocal counting)하면 오차가 1 tick / (전체 시간) 으로 줄어든다 — 마지막 열. 그래서 실무 기본값은 "PSC=0 + overflow 확장 + 필요한 곳만 평균" 이고, PSC 는 지연 계약이 지켜지지 않을 때 키운다 (연습 3).

### Part C — 초기화 순서, seqlock, 타코미터 로그

**`ic_init` 순서와 이유:**

1. `CR1 = 0` — 카운터 정지. 2. `DIER = 0` — 설정 도중 인터럽트 금지.
3. `CCER = 0` — CCxS 는 CCxE=0 일 때만 쓸 수 있고(RM 규칙), IC 프리스케일러 분주 카운터도 리셋된다.
4. `PSC`, `ARR = 0xFFFF` — free-running 이어야 `(ovf << 16) | CCR` 가 성립한다.
5. `CCMR1` (CC1S=01, IC1PSC, CC2S=10), `CCER` (CC1E, CC2E|CC2P).
6. `EGR = UG` — PSC 는 항상 preload 라 UG 없이는 첫 랩까지 옛 PSC 로 센다 (BM-B14). UG 는 CNT=0 도 만든다.
7. `SR = 0` — UG 가 세운 UIF 와 묵은 캡처 플래그를 지운다. 이걸 빼면 첫 ISR 에서 가짜 overflow 를 세어 첫 타임스탬프가 한 바퀴 틀린다.
8. 드라이버 상태 초기화 → `DIER` → NVIC enable → `CEN = 1` (마지막 접근이 CEN 인지 접근 로그로 단언).

**seqlock.** `ic_get` 이 `period` 를 복사한 직후 훅이 시뮬레이션을 다음 상승까지 돌려 ISR 이 (84000, 21000) 대신 (42000, 31500) 을 발행하게 만든다. seq 확인이 없는 starter 는 (84000, 31500) — 존재한 적 없는 주기·듀티 쌍(37.5 %)을 돌려준다. 정답은 s1 ≠ s2 를 보고 다시 읽어 (42000, 31500) 과 최신 seq 를 돌려준다. 대안은 복사 동안 `__disable_irq()` (PRIMASK, C-M14 / ARM-M10) — 짧은 구조체면 그게 더 간단하고, seqlock 은 ISR 지연을 전혀 늘리지 않는다는 장점이 있다. 측정값 구조체가 2 워드 이상이면 `volatile` 만으로는 부족하다 — volatile 은 각 접근을 지켜 줄 뿐 여러 필드의 원자성은 주지 않는다.

**타코미터 로그 (solution -v 실측, PSC=0, stall = 128 overflow):**

```text
  t[ms]  상태     f_inst[Hz]   duty[%]   f_avg[Hz]  avg오차[ppm]  ovc stall
    20   OK         1000.000    25.00            -            -     0   0
    40   OK         1000.000    25.00     1000.000       +0.0     0   0
    60   OK        20000.000    50.00    18223.235   (구간 섞임)     0   0
    80   OK        20000.000    50.00    20000.000       +0.0     0   0
   100   OK        10000.000    25.00    10000.000   (구간 섞임)     0   0
   120   OK           50.000    10.00       50.000   (구간 섞임)     0   0
   140   OK           50.000    10.00       50.000       +0.0     0   0
   160   OK           50.000    10.00       50.000       +0.0     0   0
   180   OK           50.000    10.00       50.000       +0.0     0   0
   200   OK           50.000    10.00            -            -     0   0
   220   OK           50.000    10.00            -            -     0   0
   240   OK           50.000    10.00            -            -     0   0
   260   NO-SIG        0.000     0.00            -            -     0   1
   280   NO-SIG        0.000     0.00            -            -     0   1
   300   NO-SIG        0.000     0.00            -            -     0   1
   320   NO-SIG        0.000     0.00            -            -     0   1
   340   NO-SIG        0.000     0.00            -            -     0   1
   360   OK         3333.333    70.00            -            -     0   1
   380   OK         3333.333    70.00     3333.333       +0.0     0   1
   400   OK         3333.333    70.00     3333.333       +0.0     0   1
```

브링업 엔지니어가 읽어야 할 것: (1) **t=100 의 10 kHz, 25 %** 는 실제로 없던 값이다 — 20 kHz 마지막 상승(79.9 ms)과 50 Hz 첫 상승(80 ms) 사이 8400 tick 짜리 "섞인 주기". 신호가 바뀌는 순간의 첫 측정은 버리거나 두 번 연속 일치할 때만 믿는다. (2) 160 ms 에 마지막 상승 → 200–240 ms 는 옛 값 50 Hz 를 계속 보고하다가 **260 ms 에야** NO-SIG — stall 타임아웃(99.86 ms)만큼의 감지 지연이다. (3) 창 평균은 같은 사슬 안에서만 계산한다 (`breaks` 비교) — 60 ms 의 18223 Hz 는 두 구간이 섞인 평균이라 오차를 표시하지 않았다. 안정 구간(시작 후 2.2 주기 이상)의 샘플 11 개는 모두 순간값 1 tick 이내, 창 평균 ±1 ppm 이내 (단언).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 41/41 passed (0.13 s)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan
cc -std=c11 -Wall -Wextra -Werror -O2 demo_sweeps.c -o demo_sweeps && ./demo_sweeps   # 레슨 폴더에서 (solution.c 포함)
```

빨리감기 검증 실측: 같은 시나리오(무작위 마스크 창 20 개, ISR 비용 40 클럭)를 매 클럭 2 000 000 번 `sim_step` 으로 돌린 것과 이벤트 구동 161 번으로 돌린 것이 ISR 135 회의 진입 시각 해시까지 같다.

## 7. 자주 하는 실수

1. **UIF 를 항상 먼저 센다** (`if (UIF) ovf++; ext = ovf<<16 | CCR`) → 랩 직전에 찍힌 캡처에 한 바퀴를 더한다 (L=100 에서 100 건). 고침: `uif && ccr < 0x8000` 일 때만 +1.
2. **캡처를 항상 먼저 처리한다** → 랩 직후 캡처에서 한 바퀴를 뺀다 (101 건). 1·2 모두 평소엔 맞아서 테스트 벤치에서 안 보인다.
3. **OF 를 스냅샷에서만 본다** → 스냅샷과 CCR 읽기 사이에 덮인 값을 정상으로 쓴다 (102464 tick). 고침: CCR 읽기 → SR 다시 읽기 → OF 면 버림.
4. **`SR &= ~SR_UIF`** — rc_w0 레지스터에 RMW. 읽기와 쓰기 사이에 하드웨어가 세운 CC1IF 를 0 으로 써서 지운다 → 캡처 하나가 소리 없이 사라진다 (OF 도 안 섬). 고침: `SR = ~SR_UIF` (BM-B04).
5. **채널 번호 순서로 처리** → 하강(지난 주기)과 상승이 같이 오면 high 가 음수/거대. 고침: 확장 타임스탬프 순서.
6. **초기화에서 UG 후 SR 을 안 지운다** → UG 가 세운 UIF 를 첫 ISR 이 "랩" 으로 센다 → 첫 캡처가 65536 tick 밀린다. 그리고 **PSC 를 쓰고 UG 를 안 한다** → 첫 랩까지 PSC=0 으로 센다 (BM-B14).
7. **stall 타임아웃 < 1/f_min** → 저속 신호를 "정지" 로 판정 (1 Hz 를 100 ms 로 감시).
8. **main 에서 period 와 high 를 따로 읽는다** → ISR 이 사이에 끼면 존재하지 않는 듀티. 고침: seqlock 또는 짧은 IRQ 금지.

## 8. 면접 Q&A

<details>
<summary>Q1. 16비트 타이머로 1 Hz 를 어떻게 재나요? 84 MHz 면 780 µs 마다 랩인데요.</summary>

세 가지. (1) PSC 를 키워 한 주기가 65536 tick 안에 들게 한다 — 1 Hz 면 PSC ≥ 1281 → 분해능 15 µs, 그런데 그러면 고주파 정밀도를 잃는다. (2) **overflow 횟수로 확장** — UIF ISR 에서 ovf++ 하고 캡처를 `(ovf << 16) | CCR` 로. PSC=0 그대로 11.9 ns 분해능에 51 s 까지 (32비트 확장) 잴 수 있다. 이 레슨의 방식. (3) TIM2/TIM5 (F4 에서 32비트 — RM 확인 권장) 를 쓴다. 꼬리질문 "확장의 함정은?" → 캡처와 UIF 가 같이 pending 일 때의 순서 판정과 ISR 지연 한계.

</details>

<details>
<summary>Q2. 캡처와 오버플로가 한 ISR 에서 둘 다 pending 이면요?</summary>

CCR 값으로 판정한다: UIF pending 이고 CCR < 0x8000 이면 랩이 먼저(상위 = ovf+1), CCR ≥ 0x8000 이면 캡처가 먼저(상위 = ovf). 근거: 랩 후 캡처면 CCR ≤ 지연, 랩 전 캡처면 CCR ≥ 65536 − 지연. 지연 < 32768 tick 이면 두 범위가 안 겹친다 — 65536 개 CCR 전수로 확인했고 32768 에서 처음 1 건 틀린다. 꼬리질문 "지연이 더 길 수 있다면?" → PSC 를 키워 tick 을 늘리거나(지연 계약이 tick 단위라서), 이 IRQ 의 우선순위를 올리거나(BM-M02), 32비트 타이머. 새 STM32(F3/F7/L4/G4/H7 등, F4 는 아님 — 확인 권장)의 `CR1.UIFREMAP` 은 CNT 를 읽을 때 bit31 에 UIF 를 복사해 줘서 "CNT 읽기 + 랩 여부" 를 원자적으로 얻는다 — 같은 문제를 하드웨어가 푼 것.

</details>

<details>
<summary>Q3. 오버캡처는 어떻게 알고, 왜 CCR 을 먼저 읽나요?</summary>

CCxIF 가 1 인 채로 또 캡처되면 CCxOF 가 선다 — 캡처는 소리 없이 사라지지 않는다. 순서: CCR 을 먼저 읽고(이때 CCxIF 가 지워짐) 그 다음 SR 의 OF 를 본다. 반대로 하면 "OF 확인 → (그 사이 새 캡처) → CCR 읽기" 로 덮인 값을 놓친다. OF 면 그 값은 버리고 기준점도 다시 잡는다 — 덮은 캡처가 언제였는지(UIF 판정 근거)를 모르기 때문이다. 이 레슨에서 순서를 거꾸로 한 ISR 은 102464 tick 이라는 엉뚱한 주기를 발행했다.

</details>

<details>
<summary>Q4. CC1IF 는 어떻게 지우나요? SR 은 왜 =~ 로 쓰나요?</summary>

입력 캡처 모드에서는 CCR1 을 읽으면 CC1IF 가 지워진다 (RM0090 — 확인 권장). 그래서 ISR 은 값을 읽는 것만으로 플래그 처리가 끝나고, UIF·OF 만 `SR = ~(UIF | CC1OF)` 로 지운다. SR 은 rc_w0 라 1 을 쓴 비트는 그대로 → 지울 비트만 0 인 값을 한 번 쓰면 된다. `SR &= ~UIF` 는 읽기-쓰기 사이에 선 CCxIF 를 지워 캡처를 잃는다 (OF 도 안 서는 최악의 손실).

</details>

<details>
<summary>Q5. 분해능과 범위, 고주파는 어떻게 하나요?</summary>

한 주기 측정 오차 ≤ 1 tick / 주기. 84 MHz 에서 1 kHz 는 14 ppm, 100 kHz 는 1176 ppm. 여러 주기 평균(reciprocal counting: N 주기에 걸린 시간을 재기)이면 1 tick / 전체 시간으로 줄어든다. 신호가 MHz 대면 캡처 ISR 이 못 따라가므로 IC 프리스케일러(/8)로 줄이거나, **게이트 카운팅**: 신호를 타이머의 외부 클럭(ETR/TI 외부 클럭 모드 — RM 확인 권장)으로 넣어 카운터가 에지를 세게 하고, 다른 타이머로 정확히 1 s 게이트를 만든다. 저주파는 주기 측정, 고주파는 주파수 카운팅이 유리하다 (한 주기 측정과 게이트 T 카운팅의 상대 분해능 f/f_tim 과 1/(f·T) 가 같아지는 교차점 ≈ √(f_tim / T)).

</details>

<details>
<summary>Q6. 듀티는 어떻게 재나요? 방법별 장단점은?</summary>

(a) 이 레슨: CH1 상승 + CH2 하강(같은 TI1, CC2S=10), free-running + 확장 — 주기 길이 제한 없음, ISR 은 에지마다. (b) **PWM 입력 모드**: 슬레이브 리셋(SMS=100, TS=TI1FP1)으로 상승마다 CNT=0 → CCR1 = 주기, CCR2 = high, ISR 주기당 1번, 확장 불필요. 대신 주기 < 65536 tick 이어야 한다 (연습 1). (c) 한 채널 양쪽 에지(CC1P=CC1NP=1) + 핀 레벨로 극성 판정 — ISR 이 늦으면 극성을 틀린다 (연습 4, 선점 한 번에 period=168000, high=105000). 정답은 대개 (a) 또는 (b).

</details>

<details>
<summary>Q7. ISR 에서 잰 값을 main 에 어떻게 넘기나요? volatile 이면 되지 않나요?</summary>

volatile 은 각 접근을 컴파일러가 없애거나 옮기지 못하게 할 뿐, period 와 high 두 필드를 함께 원자적으로 읽게 해 주지 않는다. main 이 period 를 읽은 직후 ISR 이 새 값을 쓰면 (옛 period, 새 high) 쌍이 나온다 — 이 레슨에서 37.5 % 라는 존재한 적 없는 듀티. 해결: 짧은 구조체면 `__disable_irq()` 로 복사(PRIMASK, C-M14/ARM-M10), 아니면 seqlock (ISR: seq++/쓰기/seq++, main: 같을 때까지 재시도). 단일 코어라 ISR 쪽은 보호가 필요 없다. 멀티코어라면 메모리 배리어도 필요.

</details>

<details>
<summary>Q8. 입력에 노이즈·글리치가 있으면? 필터가 측정을 망치지 않나요?</summary>

IC1F 디지털 필터: 새 레벨이 N 샘플 연속이어야 에지로 인정 (예: IC1F=0011 → f_CK_INT 로 N=8 — RM 표 확인 권장). 짧은 글리치는 사라지고, 진짜 에지는 **양쪽 다 N−1 샘플 늦어지므로 주기·high 는 그대로**다 (연습 2: 글리치 섞인 신호가 필터 없이 rises=12, period=50725 → N=8 필터 후 84000/42000). 아날로그 쪽은 RC + 슈미트 트리거(히스테리시스). 필터 길이는 "최소 진짜 펄스 폭 > N·샘플 주기" 로 제한된다.

</details>

<details>
<summary>Q9. 신호가 끊기면(팬 정지) 측정값은 어떻게 되나요?</summary>

캡처는 에지가 있어야 생기므로 마지막 값이 영원히 남는다 — 타임아웃이 필수. overflow ISR 에서 "마지막 상승 이후 overflow 수" 를 세어 stall_ovf 에 닿으면 무효로 한다. 타임아웃은 1/f_min 보다 길어야 하고(아니면 저속을 정지로 오판), 감지 지연이 곧 그 값이다 (로그에서 160 ms 정지 → 260 ms 감지). 0 % 인지 100 % 인지는 그때 핀 레벨로 구분한다.

</details>

## 9. 추가 연습

### 연습 1 — PWM 입력 모드 (슬레이브 리셋)

SMCR 에 SMS=100(리셋), TS=101(TI1FP1) 을 설정해 상승 에지마다 카운터를 리셋하라. CC1 ISR 하나에서 CCR1(주기)과 CCR2(high)를 읽고, overflow 는 "너무 느림" 으로 처리하라. URS 는 왜 1 이어야 하나?

<details>
<summary>답 보기</summary>

순서: CEN=0 & URS=1 → DIER=0 → CCER=0 → PSC, ARR=0xFFFF → CCMR1 (CC1S=01, CC2S=10) → CCER (CC1E, CC2E|CC2P) → SMCR (TS=101, SMS=100) → UG → SR=0 → DIER (UIE|CC1IE) → CEN. URS=1 이 아니면 **리셋 트리거 자체가 update event** 라 상승마다 UIF 가 서서 "진짜 overflow(주기 > 65536 tick)" 와 구분이 안 된다. CCR2 는 같은 주기의 하강 때 찍혔으므로 CC1 ISR 에서 같이 읽으면 짝이 맞고(CC2IE 불필요), 첫 상승은 기준점이라 버린다. 실측: 1 kHz 25 % @PSC=1 → CCR1=42000, CCR2=10500, 6 주기에 ISR 6 회 / 같은 신호 @PSC=0 (84000 tick > 65535) → overflow 6 회로 "너무 느림" / 20 kHz 30 % → 4200/1260. 이 모델은 캡처값 = 리셋 후 tick 수 그대로다 — ST 예제(HAL TIM_PWMInput)도 `f = f_cnt / CCRx` 로 계산하지만 실제 칩의 ±1 은 RM·예제로 확인 권장.

</details>

### 연습 2 — IC1F 디지털 필터

`icf_filter(in, n, N, init, out)` 을 써라: 출력은 새 레벨이 N 샘플 연속일 때 그 N 번째 샘플에서 바뀐다. 폭 1–12 클럭의 글리치(N=8)와, 글리치가 섞인 1 kHz 신호를 모델에 넣어 비교하라.

<details>
<summary>답 보기</summary>

`run` = 현재 출력과 다른 샘플이 연속된 수. 다르면 `++run >= N` 일 때 전환, 같으면 `run = 0`. 폭 w < 8 글리치는 출력이 안 바뀌고, w ≥ 8 이면 폭 w 그대로 7 샘플 늦게 나온다 (전수 단언). 1 kHz 50 % 에 high 구간마다 2 클럭 low, low 구간마다 3 클럭 high 글리치를 넣으면 필터 없이 실측 `rises=12 period=50725 high=25356` (글리치를 에지로 캡처), N=8 필터 후 `rises=4 period=84000 high=42000` — 지연이 양쪽 에지에 같으므로 차이는 정확하다. STM32 의 IC1F 코드(0001 N=2, 0010 N=4, 0011 N=8 at f_CK_INT, 0100 이후는 f_DTS 분주 + N=5/6/8)는 RM 표 확인 권장.

</details>

### 연습 3 — PSC 고르기

`ic_pick_psc(f_tim, f_max_mHz, err_ppm, &psc)`: 최고 주파수에서 1 tick 이 허용 오차 이하인 **가장 큰** PSC (overflow 인터럽트 최소). 불가능하면 −1. brute force 와 비교하고 모델로 실제 오차를 재라.

<details>
<summary>답 보기</summary>

1 tick = (PSC+1)/f_tim ≤ err·10⁻⁶ / f_max ⇔ (PSC+1)·f_max_mHz·10⁶ ≤ err·f_tim·1000. 정수로 `q = err·f_tim·1000 / (f_max_mHz·10⁶)`, q=0 이면 불가능, q > 65536 이면 상한. 84 MHz: 1 kHz·100 ppm → PSC=7, 20 kHz·100 ppm → 불가능 (PSC=0 에서도 1 클럭 = 238 ppm → 평균하거나 더 빠른 타이머), 1 Hz·0.1 % → 65535 (상한). 무작위 300 건 brute force 와 일치. 모델 실측(PSC=7, 일부러 나눠떨어지지 않는 주파수): 0.999999 kHz 최악 94.2 ppm, 49.999 Hz 3.8 ppm (≤ 100), 2.000001 kHz 190.0 ppm — f_max 가정을 넘으면 오차도 넘는다.

</details>

### 연습 4 — 한 채널 양쪽 에지의 함정

CC1P=CC1NP=1 로 양쪽 에지를 CH1 하나로 캡처하고, ISR 에서 핀 레벨(GPIOA IDR bit6)로 상승/하강을 판정하라. CCR1 읽기와 IDR 읽기 사이에 30000 클럭 선점을 주입하면?

<details>
<summary>답 보기</summary>

선점이 없으면 1 kHz 25 % → period 84000, high 21000, 상승·하강 6 개씩. 85000 의 상승 ISR 에서 CCR1(85000)을 읽은 뒤 선점 동안 106000 에 하강이 지나가면 IDR 은 0 → 상승을 하강으로 센다. 실측: 상승 5/6 로 셈, 직후 `period=168000 high=105000` (2 주기, high > 주기), 다음 주기부터 회복. 같은 선점을 본문 두 채널 드라이버의 SR→CCR1 사이에 넣으면 영향 없음 (period=84000, high=21000, ovc=0) — 극성은 채널이 이미 알고 있으므로 핀을 다시 읽을 필요가 없다. 교훈: "ISR 이 빨리 온다" 를 가정한 설계는 다른 ISR 이 붙는 순간 깨진다. (STM32F1 등 일부 계열은 입력 캡처에서 양쪽 에지를 지원하지 않는다 — RM 확인 권장.)

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 레슨 폴더에서 (solution.c 포함), 16/16 passed
```

## 10. 핵심 정리

- 캡처 = 에지 순간 CNT → CCRx (하드웨어 타임스탬프, CPU 지연 무관). CCxIF 는 CCRx 읽기로 지워지고, 지우기 전에 또 오면 CCxOF.
- 확장: `ext32 = (hi << 16) | CCR`, hi = ovf, 단 UIF 가 같이 pending 이고 CCR < 0x8000 이면 ovf+1. 조건: ISR 지연 < 32768 tick (84 MHz PSC=0 에서 390 µs). ① UIF 먼저 / ② 캡처 먼저는 랩 근처에서 ±65536 틀린다.
- ISR 순서: SR 스냅샷 → CCRx 먼저 → SR 다시(OF 면 버림, 사슬 끊기) → 확장 → 시각순 처리 → UIF 는 마지막에 ovf++ → `SR = ~mask` 한 번 (rc_w0, `&=` 금지).
- 주기 = ext₂ − ext₁ (uint32, 랩 안전), 한 주기 오차 < 1 tick, 평균은 N 주기 시간으로. f = f_tim·div/((PSC+1)·period).
- PSC: 작을수록 분해능↑, overflow ISR 빈도↑, 지연 계약(tick 단위)↓. 고주파는 IC1PSC 또는 게이트 카운팅, 노이즈는 IC1F (양쪽 지연 같아 주기 불변).
- 초기화: CEN=0 → CCER=0 → PSC/ARR → CCMR1/CCER → UG → SR=0 → DIER → NVIC → CEN. stall 타임아웃 > 1/f_min.
- main 으로는 seqlock 또는 짧은 IRQ 금지로 — volatile 은 여러 필드의 원자성을 주지 않는다.

## 참고자료

- ST RM0090 *STM32F405/415, F407/417, F427/437, F429/439 Reference Manual* — General-purpose timers (TIM2–TIM5): input capture mode, PWM input mode, slave mode controller (reset mode), TIMx_CCMR1 (CC1S, IC1PSC, IC1F), TIMx_CCER (CC1P/CC1NP), TIMx_SR (CCxIF/CCxOF, rc_w0), TIMx_CCR1 — <https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf> (이 레슨의 주소·비트·동작은 기억에 의존한 대표값 — 원문 확인 권장)
- ST AN4776 *General-purpose timer cookbook for STM32 microcontrollers* — 입력 캡처로 주파수·듀티 측정, 긴 주기 측정
- STM32CubeF4 HAL `stm32f4xx_hal_tim.c` (`HAL_TIM_IC_ConfigChannel`, `HAL_TIM_IC_Start_IT`) 과 예제 `TIM_InputCapture`, `TIM_PWMInput`
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* (3rd ed.) — 인터럽트 우선순위·지연, PRIMASK
- 관련 레슨: BM-B02 (mock 프레임워크), BM-B04 (rc_w0 / RMW 경쟁, 접근 횟수 훅), BM-B11 (GPIO AF), BM-B13 (wrap-safe 차이), BM-B14 (PSC preload 와 UG, 타이머 모델 원본), BM-M01 (NVIC), BM-M02 (우선순위 설계 — 지연 계약), BM-M05 (PWM 출력 — 이 레슨의 입력 쪽 짝), C-M14 (ISR↔main 공유 데이터), DS ring buffer 레슨들 (측정값을 큐로 넘길 때)
- 다음 단계: ARM-M10 (PRIMASK/BASEPRI 임계 구역), ARM-A08 (인터럽트 지연 측정)
