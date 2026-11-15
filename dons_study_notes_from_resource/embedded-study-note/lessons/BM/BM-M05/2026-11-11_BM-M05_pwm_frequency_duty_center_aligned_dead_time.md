---
id: BM-M05
time: 90분
updated: 2026-11-11
---
# BM-M05 PWM: 주파수·듀티 계산, edge/center-aligned, 데드타임

## 학습 목표

- 타이머 카운터(CNT)와 비교 레지스터(CCR)에서 PWM 기준 신호 **OCxREF** 가 만들어지는 규칙(PWM mode 1/2, up/down/center)을 클럭 단위로 모델링하고, 한 주기 파형 배열로 `f_pwm` 공식과 듀티 0 %/100 % 경계를 증명한다.
- "center-aligned 면 주파수가 절반" 이 정확히 무슨 뜻인지 말한다: 같은 ARR 이면 주기가 (ARR+1) → 2·ARR 클럭, 같은 주파수면 ARR 이 절반 (= 분해능 1비트 손해).
- CCR **preload (OCxPE)** 가 없으면 생기는 글리치(짧은 펄스, 한 주기 두 펄스)를 재현하고, center 모드에서는 preload 만으로도 비대칭 펄스가 생길 수 있음을 보인다.
- TIM1 `BDTR.DTG` 의 **4 구간 비선형 인코딩**을 왕복 전수 검사로 구현하고, 상보 출력(CHx/CHxN)에 데드타임이 어떻게 들어가는지, 데드타임보다 짧은 펄스가 왜 사라지는지, **MOE** 가 무엇을 끄는지 파형으로 확인한다.
- 168 MHz TIM1 로 20 kHz 3상 하프브리지를 드라이버로 띄우고, 골짜기(valley) UEV ISR 의 **시간 예산(24.99 µs)** 을 넘기면 무엇이 깨지는지 숫자로 보인다.

## 1. 문제

모터 드라이버, LED 디머, DC-DC 컨버터, 서보, 부저 — 펌웨어 면접에서 "PWM 주파수와 듀티를 레지스터로 어떻게 만드나요, 하프브리지면 무엇을 더 조심하나요?" 는 로보틱스·자동차(Anduril, Rivian, Tesla) 포지션의 단골 질문이고, 브링업에서는 "TIM3 PWM 은 나오는데 TIM1 은 안 나와요"(MOE), "듀티를 바꾸는 순간 펄스가 튀어요"(preload), "MOSFET 이 뜨거워요"(데드타임) 로 다시 만난다.

PWM 은 BM-B14 의 타임 베이스(PSC → CNT → ARR 에서 UEV) 위에 **비교기 하나** 를 더 얹은 것이다. 매 카운터 값마다 CNT 와 CCRx 를 비교해 기준 신호 OCxREF 를 만들고, 그 뒤 **출력단**(데드타임 발생기 → 극성 CCxP/CCxNP → 출력 enable CCxE/CCxNE → 마스터 enable MOE) 을 거쳐 핀으로 나간다.

| 단계 | 레지스터 (TIM1, RM0090 기준 대표값 — 확인 권장) | 이 레슨의 모델 |
|---|---|---|
| 카운터 | CR1: CEN 0, DIR 4, CMS[6:5], ARPE 7, CKD[9:8] / PSC 0x28 / ARR 0x2C / CNT 0x24 | `tim_count_tick()` (TODO A2) |
| 비교 → REF | CCMR1 0x18 (CH1 = 하위 바이트, CH2 = 상위 바이트), CCMR2 0x1C (CH3): OCxM[6:4], OCxPE bit3 / CCR1..3 0x34..0x3C | `pwm_ref()` (TODO A1), `on_w_ccr()` (TODO B1) |
| 출력단 | CCER 0x20: 채널당 4비트 E, P, NE, NP / BDTR 0x44: DTG[7:0], MOE 15 | `dtg_decode/encode` (B2, B3), `out_stage()` (B4) |
| 인터럽트 | DIER UIE / SR UIF (rc_w0) / TIM1_UP_TIM10 IRQ 25 (F4 대표값) | NVIC-lite `irq_line_set()` + `sim_step()` |

모델의 시간은 `sim_step()` 한 번 = 타이머 커널 클럭 CK_INT 1개다 (168 MHz 면 5.952 ns). 파형 배열 한 칸 = 한 클럭이고, 0번 칸 = 주기 시작(edge 는 UEV, center 는 골짜기 UEV) 이다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 10 클럭 주기, 두 가지 카운터 (모델 실측 배열, PWM mode 1)</text>
  <text x="20" y="44" fill="currentColor">edge-aligned up: ARR=9, CCR=3</text>
  <rect x="22" y="150" width="28" height="1" class="f-mut" stroke="currentColor"/><text x="31" y="146" fill="currentColor" font-size="12" font-family="monospace">0</text>
  <rect x="54" y="141" width="28" height="9" class="f-mut" stroke="currentColor"/><text x="63" y="137" fill="currentColor" font-size="12" font-family="monospace">1</text>
  <rect x="86" y="132" width="28" height="18" class="f-mut" stroke="currentColor"/><text x="95" y="128" fill="currentColor" font-size="12" font-family="monospace">2</text>
  <rect x="118" y="123" width="28" height="27" class="f-mut" stroke="currentColor"/><text x="127" y="119" fill="currentColor" font-size="12" font-family="monospace">3</text>
  <rect x="150" y="114" width="28" height="36" class="f-mut" stroke="currentColor"/><text x="159" y="110" fill="currentColor" font-size="12" font-family="monospace">4</text>
  <rect x="182" y="105" width="28" height="45" class="f-mut" stroke="currentColor"/><text x="191" y="101" fill="currentColor" font-size="12" font-family="monospace">5</text>
  <rect x="214" y="96" width="28" height="54" class="f-mut" stroke="currentColor"/><text x="223" y="92" fill="currentColor" font-size="12" font-family="monospace">6</text>
  <rect x="246" y="87" width="28" height="63" class="f-mut" stroke="currentColor"/><text x="255" y="83" fill="currentColor" font-size="12" font-family="monospace">7</text>
  <rect x="278" y="78" width="28" height="72" class="f-mut" stroke="currentColor"/><text x="287" y="74" fill="currentColor" font-size="12" font-family="monospace">8</text>
  <rect x="310" y="69" width="28" height="81" class="f-mut" stroke="currentColor"/><text x="319" y="65" fill="currentColor" font-size="12" font-family="monospace">9</text>
  <line x1="16" y1="123" x2="344" y2="123" stroke="currentColor" stroke-dasharray="5 3"/>
  <text x="22" y="119" fill="currentColor" font-size="12">CCR=3</text>
  <text x="16" y="168" fill="currentColor" font-size="12">CNT ↑ / REF ↓</text>
  <rect x="20.0" y="180" width="96.0" height="25" class="f-acc"/>
  <polyline points="20.0,180 116.0,180 116.0,205 340.0,205" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <text x="32" y="222" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="64" y="222" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="96" y="222" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="128" y="222" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="160" y="222" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="192" y="222" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="224" y="222" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="256" y="222" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="288" y="222" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="320" y="222" fill="currentColor" font-size="12" font-family="monospace">0</text>
  <text x="20" y="240" fill="currentColor" font-size="12">▲UEV (CNT 9→0)</text>
  <text x="20" y="262" fill="currentColor" font-size="12">P = (PSC+1)(ARR+1) = 10 클럭, high = CCR = 3</text>
  <text x="20" y="280" fill="currentColor" font-size="12">REF = (CNT &lt; CCR)</text>
  <text x="390" y="44" fill="currentColor">center-aligned (CMS=01): ARR=5, CCR=2</text>
  <rect x="392" y="150" width="28" height="1" class="f-mut" stroke="currentColor"/><text x="401" y="146" fill="currentColor" font-size="12" font-family="monospace">0</text>
  <rect x="424" y="132" width="28" height="18" class="f-mut" stroke="currentColor"/><text x="433" y="128" fill="currentColor" font-size="12" font-family="monospace">1</text>
  <rect x="456" y="114" width="28" height="36" class="f-mut" stroke="currentColor"/><text x="465" y="110" fill="currentColor" font-size="12" font-family="monospace">2</text>
  <rect x="488" y="96" width="28" height="54" class="f-mut" stroke="currentColor"/><text x="497" y="92" fill="currentColor" font-size="12" font-family="monospace">3</text>
  <rect x="520" y="78" width="28" height="72" class="f-mut" stroke="currentColor"/><text x="529" y="74" fill="currentColor" font-size="12" font-family="monospace">4</text>
  <rect x="552" y="60" width="28" height="90" class="f-mut" stroke="currentColor"/><text x="561" y="56" fill="currentColor" font-size="12" font-family="monospace">5</text>
  <rect x="584" y="78" width="28" height="72" class="f-mut" stroke="currentColor"/><text x="593" y="74" fill="currentColor" font-size="12" font-family="monospace">4</text>
  <rect x="616" y="96" width="28" height="54" class="f-mut" stroke="currentColor"/><text x="625" y="92" fill="currentColor" font-size="12" font-family="monospace">3</text>
  <rect x="648" y="114" width="28" height="36" class="f-mut" stroke="currentColor"/><text x="657" y="110" fill="currentColor" font-size="12" font-family="monospace">2</text>
  <rect x="680" y="132" width="28" height="18" class="f-mut" stroke="currentColor"/><text x="689" y="128" fill="currentColor" font-size="12" font-family="monospace">1</text>
  <line x1="386" y1="114" x2="714" y2="114" stroke="currentColor" stroke-dasharray="5 3"/>
  <text x="392" y="110" fill="currentColor" font-size="12">CCR=2</text>
  <text x="386" y="168" fill="currentColor" font-size="12">CNT ↑ / REF ↓</text>
  <rect x="390.0" y="180" width="64.0" height="25" class="f-acc"/>
  <rect x="646.0" y="180" width="64.0" height="25" class="f-acc"/>
  <polyline points="390.0,180 454.0,180 454.0,205 646.0,205 646.0,180 710.0,180" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <text x="402" y="222" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="434" y="222" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="466" y="222" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="498" y="222" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="530" y="222" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="562" y="222" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="594" y="222" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="626" y="222" fill="currentColor" font-size="12" font-family="monospace">0</text><text x="658" y="222" fill="currentColor" font-size="12" font-family="monospace">1</text><text x="690" y="222" fill="currentColor" font-size="12" font-family="monospace">1</text>
  <text x="390" y="240" fill="currentColor" font-size="12">▲UEV 골짜기</text>
  <text x="550" y="240" fill="currentColor" font-size="12">▲UEV 꼭대기</text>
  <text x="390" y="262" fill="currentColor" font-size="12">P = (PSC+1)·2·ARR = 10 클럭, high = 2·CCR = 4</text>
  <text x="390" y="280" fill="currentColor" font-size="12">up: CNT&lt;CCR, down: CNT≤CCR (골짜기 중심)</text>
</svg>
```

### Part A — 카운터 세 가지와 OCxREF

BM-B14 타이머 모델 축약판(BM-B02 mock 코어 위)을 TIM1 으로 넓힌다.

- `tim_count_tick()` — edge up (0..ARR, ARR 다음 0 + UEV), edge down (ARR..0, 0 다음 ARR + UEV), center (0..ARR−1 up, ARR..1 down, 꼭대기와 골짜기에서 각각 UEV). center 의 방향은 하드웨어 상태이고 CR1.DIR 로 읽힌다.
- `pwm_ref()` — PWM mode 1: up 이면 `CNT < CCR`, down 이면 `CNT <= CCR` 일 때 1. mode 2 는 반대. 강제 모드 100/101.
- `pwm_period_clk()` — edge `(PSC+1)(ARR+1)`, center `(PSC+1)·2·ARR`.
- 검증: 이름 붙은 한 주기 배열 5개, 듀티 0 %/100 % 경계, **전수 7 520 설정**(ARR 1..40 × CCR 0..ARR+2 × PSC 0/2 × 4 모드)에서 주기·high 클럭 수 = 공식, 168 MHz 20 kHz, CMS=01/10/11 의 CCxIF 차이.

### Part B — preload 글리치, 데드타임, MOE

- `on_w_ccr()` — OCxPE=0 이면 CCR 쓰기가 shadow 에 즉시, 1 이면 다음 UEV 에.
- `dtg_decode()` / `dtg_encode(dead_ns, f_dts)` — 4 구간 인코딩, **요청값 이상인 최소값** 으로 올림.
- `out_stage()` — 상보(E+NE)면 REF 의 상승을 DT 만큼 늦춘 것이 CH, 하강을 DT 만큼 늦춘 것이 CHN. MOE=0 이면 둘 다 0, 극성은 마지막에 XOR.
- 검증: 주기 중간 CCR 변경(즉시 vs preload), DTG 256 코드 왕복 + 0..1100 tick 최소성 brute force, 데드타임 파형 위치, **무작위 3 000 설정에서 CH·CHN 동시 1 = 0 회** + on-time 공식, 펄스 소멸, MOE, 극성.

### Part C — 20 kHz 3상 하프브리지 드라이버

- `pwm3_init(base, f_tim, f_pwm, dead_ns, &cfg)` — center mode 1, PWM1 + OCxPE, ARPE, 데드타임, 정석 순서 (접근 로그로 검사).
- `pwm_set_duty(base, ch, duty_x10000)`, `pwm_emergency_off(base)` (MOE 만 0).
- `TIM1_UP_TIM10_IRQHandler()` — UIF 를 먼저 지우고 골짜기(DIR=0)에서만 제어 콜백.
- 이벤트 시뮬레이션: 사인 3상 듀티를 계산하는 제어 루프, ISR 계산 시간 `cost` 클럭 뒤에 CCR 이 쓰인다. 100 주기 동안 모든 펄스 대칭 + 한 주기 뒤 정확히 반영 + 관통 0 + 최소 데드타임 84 클럭, cost 4199 vs 4200 경계, 과전류 시 MOE 차단.

## 2. Problem (English)

> **Center-aligned complementary PWM for a half-bridge (STM32F4 TIM1-style, simulated).**
>
> The timer model steps one kernel clock per `sim_step()`. Implement:
>
> 1. `uint8_t pwm_ref(uint32_t ocm, uint32_t cnt, uint32_t ccr, bool down, uint8_t prev)` — OCxREF for PWM mode 1/2 and forced modes.
> 2. `void tim_count_tick(void)` — edge up / edge down / center-aligned counting with update events and preload transfer.
> 3. `uint64_t pwm_period_clk(uint32_t psc, uint32_t arr, bool center)`.
> 4. `uint32_t dtg_decode(uint32_t dtg)` and `int dtg_encode(uint32_t dead_ns, uint32_t f_dts_hz, uint8_t *dtg)` — pick the **smallest representable dead time ≥ the request**; return −1 if above 1008·tDTS.
> 5. The output stage: complementary outputs with dead time, polarity, CCxE/CCxNE, MOE.
> 6. `int pwm3_init(uint32_t base, uint32_t f_tim_hz, uint32_t f_pwm_hz, uint32_t dead_ns, pwm_cfg_t *cfg)`, `pwm_set_duty()`, `pwm_emergency_off()`, and the update ISR that runs the control callback once per PWM period.
>
> Constraints: no malloc, 16-bit PSC/ARR/CCR, integer math, deterministic tests.
>
> Examples: `f_tim = 168 MHz, f_pwm = 20 kHz, dead = 500 ns` → `PSC=0, ARR=4200, DTG=0x54 (84 tDTS = 500.000 ns)`.
> `dtg_encode(1000 ns, 168 MHz)` → `0x94` (= (64+20)·2 = 168 tDTS). `dtg_encode(6001 ns, 168 MHz)` → −1.
> Edge up, ARR=9, CCR=3 → one period of OCxREF = `1110000000`; center, ARR=5, CCR=2 → `1100000011`.
> Follow-ups: Why does a 20 kHz center-aligned PWM have half the duty resolution of an edge-aligned one? What breaks if the control ISR writes CCR after the counter peak? Why must a PWM-mode-2 "inversion" never be used for active-low gate drivers?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크가 깨져 있을 때
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "8/54 passed" — TODO 10곳 (A1–A3, B1–B4, C1–C3) 을 채워 54/54 를 만든다
./solution -v     # (정답 빌드 후) 파형 배열, f 표, DTG 표, ns 표, ISR 예산 표, 브링업 로그
```

시작 코드의 일부 TODO 는 "흔한 첫 시도" 로 채워져 있다: `pwm_period_clk` 는 center 를 무시한 `(PSC+1)(ARR+1)`, `dtg_decode` 는 DTG 를 선형으로 읽고, `dtg_encode` 는 내림 + 선형, `out_stage` 는 데드타임·MOE 없이 `CH = REF, CHN = !REF`, ISR 은 UIF 도 방향도 보지 않는다. 나머지를 다 정답으로 두고 이 첫 시도 하나씩만 남겨 돌려 본 실측:

| 첫 시도 하나만 남김 | 결과 |
|---|---|
| ISR (UIF 안 지움) | 49/54. 40 주기(336 000 클럭) 동안 ISR **331 801 번** = 거의 매 클럭 (레벨 감지라 UIF 가 서 있는 한 계속 들어온다) |
| `out_stage` (데드타임 없음) | 41/54. 샘플 단위 겹침은 0 이지만 최소 간격 `min_gap = 0` 클럭 — 실제 스위치는 끄는 데 시간이 걸려 관통(shoot-through) |
| `pwm_period_clk` (center 무시) | 48/54. 레지스터는 맞게(ARR=4200) 써 놓고 `cfg.f_act_mHz` 를 39 990.478 Hz 라고 보고한다 — 보고값과 실제가 다른 드라이버 |

타깃 빌드: 맨 위 `#define UNIT_TEST 1` 을 지우면 `REG_READ/REG_WRITE` 가 `*(volatile uint32_t *)addr` 가 되고 Part C 드라이버는 그대로 쓴다 (모델·하네스는 떼어 낸다 — seam 구조는 BM-B02). 실제 칩에서는 그 전에 RCC 에서 TIM1 클럭(APB2) 과 GPIO AF 설정(BM-B03, BM-B11), NVIC 활성화(BM-M01) 가 필요하다.

## 4. 힌트

<details>
<summary>힌트 1 — REF 는 "비교 결과" 일 뿐, 방향만 보면 된다</summary>

PWM mode 1 의 정의는 방향마다 부등호 하나다: up `CNT < CCR`, down `CNT <= CCR`. mode 2 는 그 부정. center 모드도 같은 규칙을 지금 방향으로 적용할 뿐이다. 그러면 center 에서 high 클럭 = up 쪽 `min(CCR, ARR)` + down 쪽 `min(CCR, ARR)` = `2·min(CCR, ARR)` 이 저절로 나온다.

</details>

<details>
<summary>힌트 2 — center 카운터의 두 끝</summary>

center 는 0 → ARR−1 까지 올라가다가 **ARR 에 도착하는 클럭** 에 방향을 down 으로 바꾸고 UEV, ARR → 1 까지 내려가다가 **0 에 도착하는 클럭** 에 up 으로 바꾸고 UEV. 그래서 한 주기에 CNT 값이 2·ARR 개 (0 과 ARR 은 한 번씩) 나온다.

</details>

<details>
<summary>힌트 3 — DTG 는 단조 증가</summary>

네 구간을 이어 붙이면 코드 0x00..0xFF 에 대해 데드타임이 엄격히 증가한다 (0..127, 128..254 짝수, 256..504 8 배수, 512..1008 16 배수). 그러니 "요청 이상 최소값" = 구간을 고른 뒤 그 구간 단계로 **올림** 한 값. 구간 경계 254 → 256, 504 → 512 의 빈자리를 조심.

</details>

<details>
<summary>힌트 4 — 데드타임은 "REF 가 지금 값을 유지한 시간" 하나로 충분하다</summary>

`run` = REF 가 현재 값으로 연속 유지된 샘플 수(이번 포함). REF 가 k 에서 올라가면 CH 는 k+DT 에서 올라가야 하므로 `CH = REF && run > DT`. CHN 은 거울상 `!REF && run > DT`. REF 펄스가 DT 이하이면 run 이 DT 를 못 넘으니 펄스가 사라지는 것도 공짜로 나온다.

</details>

<details>
<summary>힌트 5 — 초기화와 ISR 순서</summary>

CEN=0 에서 CMS 를 바꾼다 (동작 중 edge→center 전환 금지, RM). MOE 는 다 설정하고 UG 로 shadow 를 채운 **뒤** 켠다. UG 가 세운 UIF 는 UIE 전에 지운다 (BM-B14). ISR 은 UIF 부터 지우고(BM-M01 "늦은 플래그 지우기"), center 의 UEV 두 번 중 DIR=0 (골짜기를 지나 올라가는 중) 일 때만 제어를 돌린다.

</details>

## 5. 풀이

### Part A — 카운터와 REF 규칙

| 모드 | CNT 순서 (한 주기) | UEV | PWM mode 1 의 REF=1 조건 | high 클럭 / 주기 클럭 |
|---|---|---|---|---|
| edge up | 0, 1, …, ARR | ARR → 0 | CNT < CCR | (PSC+1)·min(CCR, ARR+1) / (PSC+1)(ARR+1) |
| edge down | ARR, …, 1, 0 | 0 → ARR | CNT ≤ CCR | (PSC+1)·min(CCR+1, ARR+1) / (PSC+1)(ARR+1) |
| center (CMS≠0) | 0, …, ARR−1 ↑, ARR, …, 1 ↓ | 꼭대기 + 골짜기 | ↑ CNT < CCR, ↓ CNT ≤ CCR | (PSC+1)·2·min(CCR, ARR) / (PSC+1)·2·ARR |

이 표가 모델의 정의이고 (RM0090 의 TIMx_CCMR1.OC1M 설명과 counter mode 절을 따른 것 — 확인 권장), 전수 테스트가 7 520 설정 모두에서 주기와 high 클럭 수를 이 공식과 맞춘다. 실제 배열 (`./solution -v`):

```text
== one-period arrays (index 0 = period start) ==
edge up   PWM1 ARR=9 CCR=3  P=10  CNT: 0 1 2 3 4 5 6 7 8 9  REF: 1110000000
edge up   PWM2 ARR=9 CCR=3  P=10  CNT: 0 1 2 3 4 5 6 7 8 9  REF: 0001111111
edge down PWM1 ARR=9 CCR=3  P=10  CNT: 9 8 7 6 5 4 3 2 1 0  REF: 0000001111
center1   PWM1 ARR=5 CCR=2  P=10  CNT: 0 1 2 3 4 5 4 3 2 1  REF: 1100000011
edge up PSC=1 ARR=4 CCR=2   P=10  CNT: 0 0 1 1 2 2 3 3 4 4  REF: 1111000000
```

**듀티 경계.** edge up: CCR=0 → 0 %, CCR=ARR → `ARR/(ARR+1)` (매 주기 1 카운트 low), **100 % 는 CCR ≥ ARR+1** (ARR=99 에서 CCR 100 과 0xFFFF 모두 100/100). edge down: CCR=0 이어도 CNT=0 한 칸이 high → **0 % 불가** (RM 도 이렇게 적는다). center: CCR=0 → 0 %, **CCR ≥ ARR → 100 %**. 테스트가 각각을 단언한다.

**"center 면 주파수 절반" 의 정확한 뜻.** 168 MHz 실측:

```text
== f_pwm at f_tim = 168 MHz (period measured in the model) ==
edge   PSC=0 ARR=8399  period=8400 clk  f=20000.000 Hz  formula=8400 clk
center PSC=0 ARR=4200  period=8400 clk  f=20000.000 Hz  formula=8400 clk
center PSC=0 ARR=8399  period=16798 clk  f=10001.191 Hz  formula=16798 clk
edge   PSC=0 ARR=4200  period=4201 clk  f=39990.478 Hz  formula=4201 clk
edge   PSC=1 ARR=4199  period=8400 clk  f=20000.000 Hz  formula=8400 clk
center PSC=2 ARR=1400  period=8400 clk  f=20000.000 Hz  formula=8400 clk
```

같은 ARR=8399 를 center 로 바꾸면 10 000 Hz 가 아니라 **10 001.191 Hz** — 비율은 정확히 (ARR+1)/(2·ARR) 이다. 같은 20 kHz 를 원하면 ARR 이 8399 → 4200 으로 절반이 되고, CCR 한 칸이 2 클럭(11.9 ns) 이 되어 **듀티 분해능이 1비트 줄어든다** (8400 단계 ≈ 13.04 bit → 4200 단계 ≈ 12.04 bit). 대신 펄스가 골짜기를 중심으로 대칭이라 3상의 스위칭 엣지가 한꺼번에 몰리지 않고, 하단 스위치가 모두 켜진 꼭대기(PWM1 기준)에 전류를 샘플링할 수 있다.

**CMS=01/10/11.** 파형은 셋 다 같고 (테스트가 첫 20 샘플을 비교) 다른 것은 비교 일치 플래그 CCxIF 를 세우는 방향뿐이다: 01 = down 일 때만, 10 = up 일 때만, 11 = 둘 다. ARR=10, CCR=4, 3 주기 실측 플래그 수 = 3 / 3 / 6. ADC 트리거나 CC 인터럽트를 한 주기에 한 번만 받고 싶을 때 고르는 옵션이다 (RM 확인 권장).

### Part B — CCR preload 와 글리치

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">주기 중간 CCR 변경 (edge up, ARR=99, CNT 49→50 사이에 씀) — 모델 실측 200 클럭</text>
  <text x="8" y="52" fill="currentColor" font-size="12">70→30 즉시</text>
  <rect x="100.0" y="36" width="150.0" height="22" class="f-warn"/>
  <rect x="400.0" y="36" width="90.0" height="22" class="f-warn"/>
  <polyline points="100.0,36 250.0,36 250.0,58 400.0,58 400.0,36 490.0,36 490.0,58 700.0,58" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <text x="8" y="70" fill="currentColor" font-size="12" font-family="monospace">50 | 30</text>
  <text x="8" y="100" fill="currentColor" font-size="12">70→30 preload</text>
  <rect x="100.0" y="84" width="210.0" height="22" class="f-ok"/>
  <rect x="400.0" y="84" width="90.0" height="22" class="f-ok"/>
  <polyline points="100.0,84 310.0,84 310.0,106 400.0,106 400.0,84 490.0,84 490.0,106 700.0,106" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <text x="8" y="118" fill="currentColor" font-size="12" font-family="monospace">70 | 30</text>
  <text x="8" y="148" fill="currentColor" font-size="12">30→70 즉시</text>
  <rect x="100.0" y="132" width="90.0" height="22" class="f-warn"/>
  <rect x="250.0" y="132" width="60.0" height="22" class="f-warn"/>
  <rect x="400.0" y="132" width="210.0" height="22" class="f-warn"/>
  <polyline points="100.0,132 190.0,132 190.0,154 250.0,154 250.0,132 310.0,132 310.0,154 400.0,154 400.0,132 610.0,132 610.0,154 700.0,154" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <text x="8" y="166" fill="currentColor" font-size="12" font-family="monospace">30 + 20 | 70</text>
  <text x="8" y="196" fill="currentColor" font-size="12">30→70 preload</text>
  <rect x="100.0" y="180" width="90.0" height="22" class="f-ok"/>
  <rect x="400.0" y="180" width="210.0" height="22" class="f-ok"/>
  <polyline points="100.0,180 190.0,180 190.0,202 400.0,202 400.0,180 610.0,180 610.0,202 700.0,202" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <text x="8" y="214" fill="currentColor" font-size="12" font-family="monospace">30 | 70</text>
  <line x1="250.0" y1="30" x2="250.0" y2="226" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="230.0" y="240" fill="currentColor" font-size="12">CCR 쓰기</text>
  <line x1="400.0" y1="30" x2="400.0" y2="226" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="380.0" y="240" fill="currentColor" font-size="12">UEV</text>
</svg>
```

ARR=99 에서 CNT 가 49 → 50 으로 넘어가기 직전에 CCR 을 쓴다. **즉시 반영(OCxPE=0)** 이면 70 → 30 은 이번 주기 펄스가 **50** (옛값도 새값도 아닌 폭), 30 → 70 은 한 주기에 **펄스 두 개**(30 + 20, 상승 엣지 2번) 가 된다. **preload(OCxPE=1)** 면 이번 주기는 옛값 그대로, UEV 에서 shadow 가 바뀌어 다음 주기부터 새값 — 테스트가 70|30, 30|70 을 단언한다. 모터에서 50 짜리 펄스 하나는 전류 스파이크, LED 에서는 깜빡임, DC-DC 에서는 출력 전압 튐이다.

**center 에서는 preload 만으로 부족하다.** center 의 UEV 는 꼭대기와 골짜기 **두 번** 이고 (RCR=0 기준), PWM1 의 펄스는 골짜기를 감싼다. ARR=100, CCR=40 에서 down 구간(CNT=10, 펄스 진행 중)에 CCR=20 을 쓰면 preload 가 켜져 있어도 골짜기 UEV 에서 반영되어 펄스 왼쪽 = 40 (옛값 31 + 새값 9), 오른쪽 = 20 — **비대칭**. 그래서 Part C 는 "골짜기에서 계산해 꼭대기 전에 쓴다" 로 시점을 정한다. TIM1 의 반복 카운터(RCR) 로 UEV 를 한 주기 한 번으로 줄이는 방법도 있지만, 홀수 RCR 에서 UEV 가 꼭대기와 골짜기 중 어디에 오는지는 RCR 을 쓰는 시점·카운터 시작 시점에 따라 달라진다 (기억 — RM 의 repetition counter 절 확인 필수). 이 모델은 RCR=0 만 다룬다.

### Part B — DTG 인코딩

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">BDTR.DTG[7:0] 4 구간 (RM0090 TIM1_BDTR 기준 — 확인 권장), ns 는 f_DTS = 168 MHz</text>
  <rect x="20" y="40" width="110" height="22" class="f-ok" stroke="currentColor"/>
  <text x="28" y="56" fill="currentColor" font-family="monospace">0xxx xxxx</text>
  <text x="145" y="56" fill="currentColor">DTG[7:0] × 1 tDTS</text>
  <text x="330" y="56" fill="currentColor" font-family="monospace">0..127</text>
  <rect x="20" y="68" width="110" height="22" class="f-acc" stroke="currentColor"/>
  <text x="28" y="84" fill="currentColor" font-family="monospace">10xx xxxx</text>
  <text x="145" y="84" fill="currentColor">(64+DTG[5:0]) × 2 tDTS</text>
  <text x="330" y="84" fill="currentColor" font-family="monospace">128..254</text>
  <rect x="20" y="96" width="110" height="22" class="f-warn" stroke="currentColor"/>
  <text x="28" y="112" fill="currentColor" font-family="monospace">110x xxxx</text>
  <text x="145" y="112" fill="currentColor">(32+DTG[4:0]) × 8 tDTS</text>
  <text x="330" y="112" fill="currentColor" font-family="monospace">256..504</text>
  <rect x="20" y="124" width="110" height="22" class="f-mut" stroke="currentColor"/>
  <text x="28" y="140" fill="currentColor" font-family="monospace">111x xxxx</text>
  <text x="145" y="140" fill="currentColor">(32+DTG[4:0]) × 16 tDTS</text>
  <text x="330" y="140" fill="currentColor" font-family="monospace">512..1008</text>
  <text x="440" y="56" fill="currentColor" font-size="12">0 .. 756.0 ns (1 단계 5.95 ns)</text>
  <text x="440" y="84" fill="currentColor" font-size="12">761.9 .. 1511.9 ns (11.9 ns)</text>
  <text x="440" y="112" fill="currentColor" font-size="12">1523.8 .. 3000.0 ns (47.6 ns)</text>
  <text x="440" y="140" fill="currentColor" font-size="12">3047.6 .. 6000.0 ns (95.2 ns)</text>
  <line x1="40" y1="215" x2="700" y2="215" stroke="currentColor"/>
  <rect x="40.0" y="185" width="83.2" height="26" class="f-ok" stroke="currentColor"/>
  <rect x="123.8" y="185" width="82.5" height="26" class="f-acc" stroke="currentColor"/>
  <rect x="207.6" y="185" width="162.4" height="26" class="f-warn" stroke="currentColor"/>
  <rect x="375.2" y="185" width="324.8" height="26" class="f-mut" stroke="currentColor"/>
  <text x="32.0" y="232" fill="currentColor" font-size="12" font-family="monospace">0</text>
  <text x="115.2" y="232" fill="currentColor" font-size="12" font-family="monospace">127</text>
  <text x="198.3" y="232" fill="currentColor" font-size="12" font-family="monospace">254</text>
  <text x="362.0" y="232" fill="currentColor" font-size="12" font-family="monospace">504</text>
  <text x="692.0" y="232" fill="currentColor" font-size="12" font-family="monospace">1008</text>
  <text x="40" y="256" fill="currentColor" font-size="12">tDTS 축. 위 구간일수록 단계가 굵고, 사이 값 (255, 505 …) 은 없다 → 요청값 이상으로 올림.</text>
</svg>
```

`dtg_encode` 는 ① `ticks = ceil(dead_ns · f_DTS / 1e9)` (정수, 올림) ② 구간 선택 ③ 그 구간 단계로 올림. **반올림이 아니라 올림** 인 이유: 데드타임이 요청보다 짧으면 관통 위험, 길면 효율·왜곡이 조금 나빠질 뿐이다. 테스트는 256 코드 전부 `encode(decode(c)) == c`, decode 의 엄격한 단조성, 0..1100 tick 전부를 brute force 최소값과 비교한다. 실측:

```text
dtg_encode(   0 ns) -> DTG=0x00     0 tDTS  actual 0.000 ns
dtg_encode(   6 ns) -> DTG=0x02     2 tDTS  actual 11.905 ns
dtg_encode( 100 ns) -> DTG=0x11    17 tDTS  actual 101.190 ns
dtg_encode( 500 ns) -> DTG=0x54    84 tDTS  actual 500.000 ns
dtg_encode( 760 ns) -> DTG=0x80   128 tDTS  actual 761.905 ns
dtg_encode(1000 ns) -> DTG=0x94   168 tDTS  actual 1000.000 ns
dtg_encode(1520 ns) -> DTG=0xC0   256 tDTS  actual 1523.810 ns
dtg_encode(2000 ns) -> DTG=0xCA   336 tDTS  actual 2000.000 ns
dtg_encode(3000 ns) -> DTG=0xDF   504 tDTS  actual 3000.000 ns
dtg_encode(4000 ns) -> DTG=0xEA   672 tDTS  actual 4000.000 ns
dtg_encode(6000 ns) -> DTG=0xFF  1008 tDTS  actual 6000.000 ns
dtg_encode(6001 ns) -> error -1
naive 'DTG = ticks' for 1000 ns: write 168 = 0xA8 -> HW decodes 208 tDTS = 1238.095 ns
```

마지막 줄이 면접 함정이다: 168 tick 이 필요하다고 `DTG = 168` 을 쓰면 0xA8 = `10`1 01000 → (64+40)·2 = 208 tDTS = **1238 ns**. 6 ns 요청이 11.9 ns 가 되는 것은 한 tick(5.95 ns) 보다 조금 넘어서 올림한 결과다. 1008 tDTS(6 µs @168 MHz) 를 넘으면 CR1.CKD 로 tDTS 를 2배·4배로 늘린다 (연습 1).

### Part B — 상보 출력과 데드타임

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">상보 출력 + 데드타임 (center ARR=50, CCR=20, DT=5 클럭 — 모델 실측, 샘플 60..99,0..59)</text>
  <text x="8" y="66" fill="currentColor" font-size="12">REF</text>
  <rect x="202.0" y="50" width="264.0" height="20" class="f-mut"/>
  <polyline points="70.0,70 202.0,70 202.0,50 466.0,50 466.0,70 730.0,70" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <text x="8" y="121" fill="currentColor" font-size="12">CH1 (act)</text>
  <rect x="235.0" y="105" width="231.0" height="20" class="f-acc"/>
  <polyline points="70.0,125 235.0,125 235.0,105 466.0,105 466.0,125 730.0,125" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <text x="8" y="176" fill="currentColor" font-size="12">CH1N (actn)</text>
  <rect x="70.0" y="160" width="132.0" height="20" class="f-ok"/>
  <rect x="499.0" y="160" width="231.0" height="20" class="f-ok"/>
  <polyline points="70.0,160 202.0,160 202.0,180 499.0,180 499.0,160 730.0,160" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <line x1="202.0" y1="40" x2="202.0" y2="195" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="196.0" y="210" fill="currentColor" font-size="12" font-family="monospace">80</text>
  <line x1="235.0" y1="40" x2="235.0" y2="195" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="229.0" y="210" fill="currentColor" font-size="12" font-family="monospace">85</text>
  <line x1="466.0" y1="40" x2="466.0" y2="195" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="460.0" y="210" fill="currentColor" font-size="12" font-family="monospace">20</text>
  <line x1="499.0" y1="40" x2="499.0" y2="195" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="493.0" y="210" fill="currentColor" font-size="12" font-family="monospace">25</text>
  <text x="172.0" y="228" fill="currentColor" font-size="12">CH 상승 = REF 상승 + DT</text>
  <text x="446.0" y="228" fill="currentColor" font-size="12">CHN 상승 = REF 하강 + DT</text>
  <text x="10" y="250" fill="currentColor" font-size="12">한 주기 100 클럭: REF high 40 → CH on 35, CHN on 55, 둘 다 꺼짐 10 (= 2 × DT).</text>
  <text x="10" y="268" fill="currentColor" font-size="12">CH·CHN 이 동시에 1 인 샘플: 0. REF 펄스가 DT 이하이면 CH 펄스는 통째로 사라진다</text>
  <text x="10" y="286" fill="currentColor" font-size="12">(20 kHz, DT 84 클럭에서 CCR ≤ 42) — 반대쪽 CHN 도 같다.</text>
</svg>
```

하프브리지는 같은 다리의 위 스위치(CH)와 아래 스위치(CHN)가 **절대 동시에 켜지면 안 된다** — 전원이 두 스위치를 통해 접지로 단락된다(관통, shoot-through). MOSFET 은 끄는 데 수십~수백 ns 가 걸리므로, 한쪽을 끄고 다른 쪽을 켜기 전에 둘 다 꺼진 시간(데드타임)을 넣는다. 출력단 규칙(모델 정의, RM0090 의 "complementary outputs and dead-time insertion" 절 기준 — 확인 권장):

- CH = REF 의 상승 엣지를 DT 만큼 늦춘 것: `act = REF && run > DT`.
- CHN = REF 하강 엣지를 DT 만큼 늦춘 REF 의 반전: `actn = !REF && run > DT`.
- 펄스 폭이 DT 이하면 그 펄스는 생성되지 않는다 (RM 도 같은 문장).
- CCxE 만 켜고 CCxNE 를 끄면 데드타임 없이 `CH = REF` (테스트: on 40 = REF 40).
- 극성은 데드타임 **뒤에** XOR: `pin = act ^ CCxP`. MOE=0 이면 모델은 두 핀 모두 0 (실제 idle 레벨은 OSSI/OSSR, CR2.OISx 가 정한다 — RM 확인).

위 그림 설정(center ARR=50, CCR=20, DT=5) 에서 테스트가 단언하는 위치: REF 1 구간 [80,100)∪[0,20), CH 는 85 에서 켜지고 20 에서 꺼짐, CHN 은 25..79. 한 주기 REF 40 → CH 35, CHN 55, 둘 다 꺼짐 10. 무작위 3 000 설정(5 모드 × PWM1/2 × PSC 0..2 × ARR 2..121 × DTG 0..69 × CKD 0..2 × 극성 4 조합) 에서 `act && actn` 동시 1 은 **0 회**, on-time 은 항상 `max(0, H − DT)` (H = 주기 전체면 그대로), 핀 = act ^ P.

**데드타임이 듀티를 깎는다.** 20 kHz center, ARR=4200, DT=84 (500 ns) 실측:

```text
duty%    CCR   REF_hi  CH_on[ns]  CHN_on[ns]  dead[ns]x2  eff_duty%
  0.00      0       0        0.0     50000.0         0.0     0.000
  1.00     42      84        0.0     49000.0       500.0     0.000
  1.03     43      86       11.9     48988.1       500.0     0.024
  5.00    210     420     2000.0     47000.0       500.0     4.000
 25.00   1050    2100    12000.0     37000.0       500.0    24.000
 50.00   2100    4200    24500.0     24500.0       500.0    49.000
 75.00   3150    6300    37000.0     12000.0       500.0    74.000
 95.00   3990    7980    47000.0      2000.0       500.0    94.000
 98.97   4157    8314    48988.1        11.9       500.0    97.976
 99.00   4158    8316    49000.0         0.0       500.0    98.000
100.00   4200    8400    50000.0         0.0         0.0   100.000
```

(`dead[ns]x2` 열 = 한 주기에서 둘 다 꺼진 시간 ÷ 2.) 위 스위치 실효 듀티는 명령보다 정확히 DT/T = 500 ns / 50 µs = **1 %p** 낮고, 1 % 이하 명령은 펄스가 통째로 사라진다 (CCR 42 → REF 84 = DT). 반대쪽도 대칭: 99 % 이상에서는 CHN 펄스가 사라진다. 0 % 와 100 % 는 전환이 없으니 데드타임도 없다. 실제 모터의 "데드타임 왜곡"(dead-time distortion) 은 데드타임 동안 다이오드가 전류 방향에 따라 출력 전압을 정하는 현상이라 전류 부호까지 봐야 한다 — 여기서는 게이트 신호 수준만 다룬다 (연습 2 의 보상도 같은 범위).

**MOE.** TIM1/TIM8 은 CCxE 를 켜도 BDTR.MOE=1 이 아니면 출력이 나가지 않는다 (테스트: MOE 없이 REF 40 클럭, CH/CHN 0). 켜면 35/55, 동작 중 MOE 를 끄면 **다음 샘플에** 네 출력 모두 0. 범용 타이머(TIM2–5) 에는 MOE 가 없어서 "TIM3 은 되는데 TIM1 은 안 된다" 가 생긴다.

### Part C — 드라이버 초기화 순서

| # | 쓰기 | 이유 |
|---|---|---|
| 1 | CR1 = 0, BDTR = 0 | 카운터 정지, MOE=0 — 설정 중 출력이 반쯤 맞은 상태로 나가지 않게 |
| 2 | PSC, ARR | preload 에 저장 (UG 에서 shadow 로) |
| 3 | CR1 = CMS01 \| ARPE | center 전환은 CEN=0 에서만 |
| 4 | CCMR1 = 0x6868, CCMR2 = 0x68 | PWM1 (110) + OCxPE, CH1·CH2·CH3 |
| 5 | CCR1..3 = ARR/2 = 2100 | 50 % = 세 상 같은 전압 (선간 전압 0) |
| 6 | CCER = 0x555 | 세 채널 모두 E + NE |
| 7 | BDTR = DTG (0x54) | MOE 를 켜기 전에 데드타임 확정 (LOCK 을 쓰면 이후 잠김 — RM 확인) |
| 8 | EGR = UG | PSC/ARR/CCR shadow 채움, CNT=0, 방향 up (모델) |
| 9 | SR = ~UIF | UG 가 세운 가짜 UIF 제거 (BM-B14) |
| 10 | DIER = UIE | 업데이트 인터럽트 |
| 11 | BDTR = DTG \| MOE | 출력 활성 |
| 12 | CR1 \|= CEN | 출발. 결과 CR1 = 0xA1 |

테스트가 접근 로그에서 이 순서(1 → 12) 와 최종 레지스터 값을 단언하고, `pwm3_init(168 MHz, 20 kHz, 500 ns)` → PSC=0, ARR=4200, DTG=0x54, dead 500 000 ps, 20 000.000 Hz. 7 µs 는 1176 tDTS > 1008 → `PWM_E_DEADTIME`, 1 kHz 는 steps 84 000 > 65 535 → PSC=1, ARR=42 000.

### Part C — 제어 루프와 시간 예산

```svg
<svg viewBox="0 0 740 245" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Part C 시간 예산: 168 MHz, ARR=4200, 20 kHz center — 골짜기 ISR 이 CCR 을 꼭대기 전에 써야 한다</text>
  <polyline points="40.0,150 205.0,60 370.0,150 535.0,60 700.0,150" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <rect x="40.0" y="160" width="165.0" height="18" class="f-ok" stroke="currentColor"/>
  <rect x="205.0" y="160" width="165.0" height="18" class="f-warn" stroke="currentColor"/>
  <text x="10" y="196" fill="currentColor" font-size="12">초록: cost ≤ 4199 클럭 (24.99 µs) → 꼭대기 UEV 에서 반영 → 다음 펄스 대칭</text>
  <text x="10" y="214" fill="currentColor" font-size="12">주황: cost ≥ 4200 → 다음 골짜기 UEV 에서 반영 → 펄스 왼쪽 = 옛값, 오른쪽 = 새값</text>
  <text x="165.0" y="52" fill="currentColor" font-size="12">꼭대기 UEV</text>
  <line x1="205.0" y1="56" x2="205.0" y2="158" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="495.0" y="52" fill="currentColor" font-size="12">꼭대기 UEV</text>
  <line x1="535.0" y1="56" x2="535.0" y2="158" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="211.0" y="78" fill="currentColor" font-size="12">shadow ← preload</text>
  <rect x="287.5" y="128" width="165.0" height="14" class="f-acc" stroke="currentColor"/>
  <text x="287.5" y="124" fill="currentColor" font-size="12">REF 펄스 = 골짜기 중심, 폭 2·CCR</text>
  <text x="10" y="232" fill="currentColor" font-size="12">실측 (60 주기, 3상): cost 4199 → 비대칭 0/174, cost 4200 → 174/174. 관통 0, 최소 데드타임 84 클럭.</text>
</svg>
```

ISR 은 UEV 마다(40 kHz) 들어오지만 골짜기에서만 제어를 돌린다. 계산 결과는 `cost` 클럭 뒤 CCR(preload) 에 쓰이고, 다음 **꼭대기 UEV** 가 shadow 로 옮긴다. 그러면 다음 골짜기를 감싼 펄스는 양쪽 다 새값 — 대칭이고 정확히 한 주기 지연. 경계는 모델에서 정확히 나온다: 골짜기 클럭 s 에서 꼭대기 UEV 는 s+4200 클럭의 스텝, 쓰기는 `clk ≥ s + cost` 인 첫 스텝 앞이므로 **cost ≤ 4199 (24.99 µs)** 이면 제때다. 실측:

```text
== ISR budget: control at valley, CCR written 'cost' clocks later (ARR=4200) ==
cost=  100 clk (  0.60 us)  pulses=58  asym=0  wrong=0  overlap=0  min_gap=84
cost= 2000 clk ( 11.90 us)  pulses=58  asym=0  wrong=0  overlap=0  min_gap=84
cost= 4199 clk ( 24.99 us)  pulses=58  asym=0  wrong=0  overlap=0  min_gap=84
cost= 4200 clk ( 25.00 us)  pulses=58  asym=174  wrong=174  overlap=0  min_gap=84
cost= 6000 clk ( 35.71 us)  pulses=58  asym=174  wrong=174  overlap=0  min_gap=84
cost= 8000 clk ( 47.62 us)  pulses=58  asym=174  wrong=174  overlap=0  min_gap=84
```

늦어도 **관통은 생기지 않는다** (데드타임은 하드웨어 출력단이 지킨다) — 망가지는 것은 제어 품질이다: 3상 × 58 펄스가 전부 "왼쪽 = 옛값, 오른쪽 = 새값" 이 되어 실효 듀티가 반 주기 늦게, 비대칭 펄스로 들어간다 (전류 리플·고조파 증가). 실제 예산은 24.99 µs 에서 ISR 진입 지연(ARM-A08), 플래시 대기(BM-B10), 더 높은 우선순위 ISR(BM-M02) 을 빼야 한다. 브링업 로그 (cost 1000 클럭):

```text
== bring-up log: first control periods (cost = 1000 clk) ==
cfg: PSC=0 ARR=4200 DTG=0x54 dead=500.000 ns f=20000.000 Hz
ctrl k=1  CCR A/B/C = 2347  354 3599  (applied around valley 2)
ctrl k=2  CCR A/B/C = 2589  274 3436  (applied around valley 3)
ctrl k=3  CCR A/B/C = 2823  226 3250  (applied around valley 4)
ctrl k=4  CCR A/B/C = 3045  210 3045  (applied around valley 5)
ctrl k=5  CCR A/B/C = 3250  226 2823  (applied around valley 6)
valleys=6 pulses=4 asym=0 wrong=0 overlap=0 min_gap=84 clk isr_calls=12 ctrl_calls=6
```

`isr_calls = 2 × ctrl_calls` 가 "center 는 UEV 두 번" 의 증거다. 100 주기 테스트: 골짜기 100, 제어 100, ISR 200, 검사한 펄스 98 (처음 두 골짜기는 시작 50 % 라 제외) 전부 대칭·정확, 관통 0, 최소 데드타임 84 클럭.

**고장 처리.** 제어 호출 20 에서 과전류를 가정해 ISR 안에서 `pwm_emergency_off()` (BDTR 의 MOE 만 0, DTG 보존) → 그 다음 샘플부터 6 출력 모두 0, 카운터와 ISR 은 계속 돈다 (40 주기 동안 제어 40 번). 실제 보드에서는 과전류 비교기를 **BKIN 브레이크 입력** 에 물려 CPU 없이 MOE 를 끄는 것이 1차 방어이고 (ISR 경로는 ISR 지연만큼 늦다), 소프트웨어 차단은 2차다 — 고장 기록과 리셋 정책은 BM-M13.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 54/54 passed (0.2 s 미만)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 54/54
```

## 7. 자주 하는 실수

1. **center 인데 edge 공식으로 ARR 계산.** `ARR = f_tim/f − 1 = 8399` 를 center 에 쓰면 10 001.191 Hz. → center 는 `ARR = f_tim / (2·f·(PSC+1))` = 4200.
2. **edge 에서 CCR = ARR 을 100 % 로 착각.** 매 주기 한 카운트(5.95 ns) low 가 남는다. 상보 출력이면 더 나쁘다: 20 kHz edge ARR=CCR=8399, DT=84 실측 — CH 는 매 주기 **85 클럭(1 + DT) 꺼지고** CHN 펄스는 0 (짧아서 소멸). 의도한 100 % 가 98.99 % 가 된다. → 100 % 는 `CCR = ARR + 1` 이상 (center 는 `CCR = ARR`).
   ```c
   uint32_t ccr = duty >= 10000u ? arr + 1u : (duty * (arr + 1u) + 5000u) / 10000u;   /* edge */
   ```
3. **OCxPE 를 안 켬.** 주기 중간 쓰기가 즉시 반영 → 50 클럭 펄스, 한 주기 두 펄스 (그림). → `CCMRx |= OCxPE`, 그리고 center 면 **쓰는 시점** 까지 정한다 (꼭대기 전).
4. **DTG 를 선형 값으로 씀.** `DTG = 168` → 0xA8 = 1238 ns. → `dtg_encode` (구간 + 올림).
5. **데드타임을 내림/반올림.** 6 ns → 1 tick (5.95 ns) 이면 요청보다 짧다. → 올림: `(ns·f + 1e9 − 1) / 1e9`.
6. **TIM1 에서 MOE 를 안 켬 / 고장 처리에서 CCER 을 하나씩 끔.** 출력이 안 나오거나, 끄는 도중 한 상만 꺼진 상태가 몇 사이클 존재. → MOE 한 비트로 6 출력 동시 차단, 하드웨어 경로는 BKIN.
7. **ISR 에서 UIF 를 안 지움 / 맨 끝에서 지움.** 레벨 감지라 336 000 클럭 동안 331 801 번 재진입 (실측). → 첫 줄에서 `SR = ~UIF` (rc_w0, BM-B04/B14), 늦은 지우기 문제는 BM-M01.
8. **active-low 게이트 드라이버를 PWM mode 2 로 뒤집음.** 데드타임 구간이 "둘 다 ON" 이 된다: 20 kHz, DT 84 에서 한 주기 168 클럭 관통 (연습 4 실측). → 극성은 데드타임 뒤의 CCxP/CCxNP 로 뒤집는다.

## 8. 면접 Q&A

<details>
<summary>Q1. 168 MHz 타이머로 20 kHz PWM 을 만들려면 ARR 은? edge 와 center 각각, 그리고 분해능은?</summary>

edge: `f = f_tim / ((PSC+1)(ARR+1))` → PSC=0, ARR=8399 (8400 클럭, 듀티 단계 8400 ≈ 13.04 bit). center: `f = f_tim / ((PSC+1)·2·ARR)` → ARR=4200 (단계 4200 ≈ 12.04 bit, CCR 한 칸 = 2 클럭 = 11.9 ns). 꼬리: "168 MHz 는 어디서?" — F407 에서 SYSCLK 168, APB2 = 84 MHz 이고 APB 프리스케일러 ≠ 1 이면 타이머 클럭은 ×2 (BM-B10, RM 확인). 해상도가 부족하면 PSC 를 늘리는 게 아니라(더 나빠짐) 타이머 클럭을 올리거나 고해상도 타이머(HRTIM 계열)를 쓴다.

</details>

<details>
<summary>Q2. 0 % 와 100 % 는 CCR 을 얼마로? 모드마다 다른가?</summary>

PWM1 edge up: 0 % = CCR 0, 100 % = CCR ≥ ARR+1 (CCR=ARR 은 한 카운트 low). edge down: CNT ≤ CCR 이 1 이라 CCR=0 이어도 한 카운트 high → 0 % 불가 (RM 문장), 100 % 는 CCR ≥ ARR. center: 0 % = 0, 100 % = CCR ≥ ARR. 이 레슨의 전수 테스트가 ARR 1..40 전부에서 이 경계를 확인한다. 실무 꼬리: 부트스트랩 게이트 드라이버는 하단 스위치가 주기적으로 켜져야 상단 부트스트랩 커패시터가 충전되므로, 100 % 를 오래 유지하면 안 되는 경우가 있다 (드라이버 데이터시트 확인).

</details>

<details>
<summary>Q3. center-aligned 를 왜 쓰나? 단점은?</summary>

① 펄스가 골짜기(또는 꼭대기) 중심 대칭 → 3상의 엣지가 한 시점에 몰리지 않고 고조파·EMI 가 줄어든다. ② 모든 하단 스위치가 켜진 구간의 한가운데가 고정 시점(카운터 꼭대기, PWM1 기준)이라 shunt 전류 샘플링 트리거를 걸기 쉽다 (CMS 로 CCxIF 방향 선택, 또는 TRGO). ③ 같은 주파수에서 분해능 1비트 손해, UEV 가 두 번(ISR 두 배, 또는 RCR). 꼬리 "center 면 주파수가 절반이죠?" — 같은 ARR 이면 (ARR+1)/(2·ARR) 배, 정확히 절반은 아니다 (8399 → 10 001.191 Hz 실측).

</details>

<details>
<summary>Q4. 동작 중 듀티를 바꿨더니 펄스가 튄다. 원인과 고침은? center 에서는?</summary>

OCxPE=0 이면 CCR 쓰기가 즉시 비교에 쓰여, 주기 중간에 쓰면 옛값도 새값도 아닌 펄스(70→30 에서 50) 나 한 주기 두 펄스(30→70) 가 생긴다. OCxPE=1 이면 UEV 때 shadow 로 옮겨 주기 단위로 바뀐다. center 는 UEV 가 꼭대기·골짜기 두 번이라, 골짜기 전(down 구간) 에 쓰면 골짜기에서 반영되어 펄스가 비대칭 (모델: 왼쪽 40, 오른쪽 20). 그래서 "골짜기에서 계산, 꼭대기 전에 쓰기" 로 시점을 맞추거나 RCR 로 UEV 를 한 번으로 줄인다. ARR 도 같은 이유로 ARPE (BM-B14).

</details>

<details>
<summary>Q5. 데드타임은 왜 필요하고 얼마로 잡나? 1 µs 를 DTG 로?</summary>

같은 다리의 두 스위치가 동시에 켜지면 관통 전류. 스위치의 turn-off 지연(게이트 방전 + 전류 하강) 은 turn-on 보다 길고, 게이트 드라이버의 전파 지연도 채널마다 다르다. 데드타임 ≥ (최악 turn-off − 최소 turn-on) + 드라이버 지연 차 + 여유 — MOSFET 이면 대략 수십 ns~1 µs, IGBT 는 µs 단위 (부품 데이터시트로 계산하고 오실로스코프로 VGS 를 보고 확인). 168 MHz 에서 1 µs = 168 tDTS > 127 → 10x 구간 `(64 + x)·2 ≥ 168` → x=20 → DTG = 0x80|20 = **0x94**, 정확히 1000 ns. 6 µs (1008 tDTS) 를 넘으면 CKD.

</details>

<details>
<summary>Q6. 데드타임을 넣으면 출력 전압이 명령과 달라진다. 얼마나, 왜, 어떻게 보상?</summary>

게이트 신호 기준으로 위 스위치 on-time 은 `2·CCR·t − DT` (center) — 20 kHz, 500 ns 면 듀티가 1 %p 깎이고, REF 펄스 ≤ DT 이면 아예 사라진다 (1 % 이하 명령 = 0). 실제 상 전압은 데드타임 동안 전류 방향에 따라 위/아래 다이오드가 도통해 결정되므로 오차의 부호가 전류 부호를 따른다 (저속·저전류에서 토크 리플). 보상: 게이트 기준이면 `CCR = ceil((on + DT)/2)` (연습 2), 전압 기준이면 전류 부호에 따라 ±DT/T 를 더하는 데드타임 보상 — 제어 알고리즘 쪽 주제.

</details>

<details>
<summary>Q7. TIM3 PWM 은 잘 나오는데 같은 코드로 TIM1 은 핀이 조용하다. 왜?</summary>

고급 타이머(TIM1/TIM8) 출력은 BDTR.MOE=1 이어야 나간다 (+ CCxE, GPIO AF). 모델 테스트: MOE 없이 REF 는 돌지만 CH/CHN = 0. 꼬리 "고장 때는?" — MOE 한 비트로 6 출력 동시 차단 (CCER 을 하나씩 끄면 그 사이 한 상만 켜진 상태). 하드웨어 경로: BKIN 브레이크 입력이 MOE 를 비동기로 지워 CPU·ISR 지연과 무관하게 차단, AOE=1 이면 다음 UEV 에 MOE 자동 복귀 (보통은 0 으로 두고 소프트웨어가 원인 확인 후 재가동). MOE=0 일 때 핀 레벨은 OSSI/OISx 로 정한다 (RM 확인).

</details>

<details>
<summary>Q8. 게이트 드라이버 입력이 active-low 다. PWM mode 2 로 뒤집으면 되나?</summary>

안 된다. 데드타임은 **REF 기준** 으로 "켜짐(active)" 을 늦춘다. PWM2 로 REF 를 뒤집으면 데드타임 구간(둘 다 inactive = 핀 둘 다 0) 이 active-low 드라이버에게는 "둘 다 ON" 이다. 연습 4 실측: DT=0 이면 PWM1+CCxP=1 과 PWM2 의 핀 파형이 8400 샘플 모두 같지만, DT=84 이면 PWM1+P 쪽 동시 ON 0 클럭, PWM2 쪽 **168 클럭/주기** (= 2 × DT) 관통. 극성은 데드타임 뒤의 CCxP/CCxNP 로 뒤집는다.

</details>

<details>
<summary>Q9. 20 kHz 전류 제어 루프 ISR 은 언제 돌리고 예산은 얼마인가?</summary>

center, PWM1 이면 골짜기 UEV 에서 (DIR=0 확인) 측정·계산하고 다음 꼭대기 UEV 전에 CCR 을 써야 다음 펄스가 대칭으로 반영된다 — 반 주기 = 4200 클럭, 모델 경계 cost ≤ 4199 클럭 = 24.99 µs. 넘기면 관통은 없지만(출력단이 데드타임 보장) 모든 펄스가 비대칭으로 반 주기 늦게 반영 (실측 174/174). 실제 예산에서 빼야 할 것: 예외 진입 12 사이클급 지연(ARM-A08), 더 높은 우선순위 ISR (BM-M02 설계), 플래시 대기 상태. 보통은 ADC 변환 완료 인터럽트를 골짜기/꼭대기에 동기 트리거해 그 ISR 에서 제어를 돌린다 (BM-M09 DMA 와 연결).

</details>

## 9. 추가 연습

### 연습 1 — CKD 로 데드타임 범위 넓히기

`dtg_pick_ckd(dead_ns, f_tim, &ckd, &dtg, &actual_ps)`: CKD = 0, 1, 2 (tDTS = 1, 2, 4 × tCK_INT) 중 인코딩이 되는 가장 작은 CKD 를 고른다. 8 µs, 15 µs, 30 µs @168 MHz 는? 모델에서 CKD=1 의 실제 간격도 확인하라.

<details>
<summary>답 보기</summary>

작은 CKD 일수록 단계가 곱다 → 0 부터 시도. 500 ns → CKD 0, 0x54. **8 µs** → 168 MHz 에서 1344 tick > 1008 → CKD 1 (84 MHz): 672 tick → 0xEA ((32+10)·16), 정확히 8000 ns. **15 µs** → CKD 2 (42 MHz): 630 tick → 올림 640 → 0xE8, 15 238.095 ns. **30 µs** → 42 MHz 에서도 1260 > 1008 → −1. 모델: CKD=1, DTG=0xEA 로 20 kHz center 를 돌리면 한 주기 둘 다 꺼짐 = 2 × 1344 클럭, CH on = 4200 − 1344. 주의: CKD 는 입력 디지털 필터(ETR, TIx) 샘플링 클럭에도 쓰인다 (RM 확인).

</details>

### 연습 2 — 데드타임 보상 CCR

center, PWM1 에서 위 스위치 on-time 이 목표 `on` 클럭 이상이 되는 가장 작은 CCR 을 구하라. 168 MHz, DT=84 에서 12 µs 는?

<details>
<summary>답 보기</summary>

on = 2·CCR − DT (2·CCR > DT 일 때) → `CCR = ceil((on + DT)/2)`, on=0 이면 CCR=0. ARR=200, DT=10 의 작은 모델에서 on 0..388 전수: 측정 on ∈ {목표, 목표+1}, CCR−1 은 항상 미달. 위쪽 끝은 함정: 389 를 요청하면 CCR=200 = ARR → **100 % (스위칭 없음)** 이라 on = 400. 12 µs = 2016 클럭 → CCR 1050 → 측정 2016 (= 12.000 µs). 보상 없이 듀티 공식(24 % → 1008) 이면 1932 클럭 = 11.5 µs.

</details>

### 연습 3 — 해상도 표

PSC=0 으로 `pwm_steps(f_tim, f_pwm, center)` → ARR, 듀티 단계 수, 비트 수, 정확 여부. 168 MHz 에서 20 kHz / 100 kHz / 33 kHz 를 edge·center 로.

<details>
<summary>답 보기</summary>

| f_pwm | edge ARR / 단계 / bit | center ARR / 단계 / bit | 정확 |
|---|---|---|---|
| 20 kHz | 8399 / 8400 / 13 | 4200 / 4200 / 12 | 예 |
| 100 kHz | 1679 / 1680 / 10 | 840 / 840 / 9 | 예 |
| 33 kHz | 5090 / 5091 / 12 | 2545 / 2545 / 11 | 아니오 |

각 행의 주기를 모델로 재서 공식과 비교한다. 1 kHz edge 는 ARR 167 999 > 65 535 → −1 (PSC 필요, BM-B14 의 검색). 비트 수 = floor(log2(단계)) — 13.04 bit 를 "13 bit" 로 말하는 것이 면접 관례.

</details>

### 연습 4 — active-low 드라이버: 어디서 뒤집나

20 kHz center, CCR=2100, 드라이버가 active-low (핀 0 = 스위치 ON). (a) PWM1 + CC1P=CC1NP=1, (b) PWM2 + 극성 0. DT=0 과 DT=84 에서 "두 핀 동시 0" 샘플 수를 세라.

<details>
<summary>답 보기</summary>

DT=0: 두 설정의 핀 파형이 8400 샘플 전부 같고 동시 0 은 0. DT=84: (a) 0, (b) **168 = 2 × DT**. (b) 는 REF 를 뒤집어 데드타임이 "둘 다 inactive" 로 들어가는데, active-low 에서 inactive(핀 0) 는 ON 이기 때문이다. 극성은 데드타임 뒤에서(CCxP) 뒤집어야 데드타임의 의미("둘 다 OFF") 가 보존된다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 13/13 passed
```

## 10. 핵심 정리

- edge: `f = f_tim / ((PSC+1)(ARR+1))`, center: `f = f_tim / ((PSC+1)·2·ARR)`. 같은 주파수면 center 의 ARR 은 절반 → 분해능 1비트 손해. 168 MHz, 20 kHz → 8399 / 4200.
- PWM1: up `CNT < CCR`, down `CNT ≤ CCR`. 100 % = edge `CCR ≥ ARR+1`, center `CCR ≥ ARR`. down 모드 0 % 불가.
- 주기 중간 변경은 OCxPE (preload) — 없으면 짧은 펄스/두 펄스. center 는 UEV 두 번이라 **쓰는 시점** (꼭대기 전) 까지 정해야 대칭.
- DTG 4 구간: ×1 (0..127), (64+x)×2, (32+x)×8, (32+x)×16 (최대 1008 tDTS). 인코딩은 요청 이상으로 **올림**, 넘치면 CKD.
- 상보: CH = REF 상승 + DT, CHN = REF 하강 + DT. on = max(0, H − DT), 펄스 ≤ DT 면 소멸. 극성은 데드타임 뒤 (CCxP) — PWM2 로 뒤집으면 관통.
- TIM1 은 MOE 가 마스터 스위치: 안 켜면 출력 없음, 고장 시 MOE 한 비트 (하드웨어는 BKIN).
- 초기화: CEN=0 → PSC/ARR → CMS+ARPE → CCMR(PWM1+OCxPE) → CCR → CCER → DTG → UG → SR=~UIF → UIE → MOE → CEN.
- 20 kHz center 제어 ISR: 골짜기에서 계산, 4199 클럭(24.99 µs) 안에 쓰기. 늦으면 관통은 없지만 펄스 비대칭.

## 참고자료

- ST RM0090 *STM32F405/415, F407/417, F427/437, F429/439 Reference Manual* — Advanced-control timers (TIM1/TIM8): center-aligned mode, PWM mode, complementary outputs and dead-time insertion, break function, TIMx_CR1 (CMS/DIR/CKD), TIMx_CCMR1 (OC1M/OC1PE), TIMx_CCER, TIMx_BDTR (DTG/MOE/AOE/OSSI/OSSR/LOCK), repetition counter — <https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf> (이 레슨의 주소·비트·규칙은 기억에 의존한 대표값 — 원문 확인 권장)
- ST AN4013 *STM32 cross-series timer overview* / AN4776 *General-purpose timer cookbook* — PWM, preload, center-aligned 응용
- STM32CubeF4 HAL `stm32f4xx_hal_tim.c`, `stm32f4xx_hal_tim_ex.c` — `HAL_TIM_PWM_ConfigChannel()` (OCxPE 설정), `HAL_TIMEx_ConfigBreakDeadTime()`, `HAL_TIMEx_PWMN_Start()`, `__HAL_TIM_MOE_ENABLE`
- ST X-CUBE-MCSDK (STM32 motor control SDK) 문서 — 3-shunt 전류 샘플링 시점과 center-aligned PWM, 데드타임 보상
- 관련 레슨: BM-B02 (mock 프레임워크 원본), BM-B04 (rc_w0), BM-B10 (APB 타이머 ×2), BM-B14 (타이머 모델 원본, PSC/ARR, UG, ARPE), BM-M01 (NVIC, 늦은 플래그 지우기), BM-M02 (우선순위 설계), BM-M06 (입력 캡처 — 같은 타이머의 반대 방향), BM-M09 (ADC+DMA), BM-M13 (고장 정책), ARM-A08 (인터럽트 지연, 다음 단계)
