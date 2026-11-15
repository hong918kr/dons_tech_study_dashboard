---
id: ALGO-A11
time: 110분
updated: 2026-11-11
---
# ALGO-A11 Rate Limiter: 토큰 버킷과 슬라이딩 로그

## 학습 목표

- **토큰 버킷**(token bucket)을 정수 틱(tick)과 유리수 속도 r = num/den 으로 구현하고, 토큰을 den 배로 스케일해 나머지까지 보관하면 **드리프트(drift)가 0**임을 증명한다. 10^9 틱 포화 테스트에서 통과 수가 정확히 b + ⌊rT⌋ 인지 확인한다.
- uint32 틱이 2^32 에서 넘어가도(wrap) **부호 없는 뺄셈**으로 경과 시간이 맞는 이유, "과거 틱"(늦게 읽은 now)과 2^31 틱 계약, `dt·num` 이 넘치지 않는 조건을 말할 수 있다.
- **창 정리**: 모든 창 [t, t+T] 에서 통과량 ≤ b + r·T 를 증명하고, 더 나아가 토큰 버킷이 "이 조건을 지키는 가장 탐욕적인(greedy) 판정기"와 **결정이 똑같음**을 브루트포스로 대조한다.
- 고정 창 카운터(경계 2배), 슬라이딩 로그(정확, 메모리 N), 슬라이딩 카운터(근사)를 같은 트래픽에 돌려 비교하고, 슬라이딩 카운터의 최악 과허용 **N + ⌈N(W−2)/W⌉** 을 증명·측정으로 맞춘다.
- 펌웨어 시나리오 두 개 — GPIO 인터럽트 폭주에서 IRQ 를 마스크하는 가드, 메시지 ID 별 로그 폭주 제한 — 를 만들고, ISR/스레드 문맥에서 무엇이 임계 구역이어야 하는지 설명한다.

## 1. 문제

"센서 선이 떨려서 초당 수만 번 인터럽트가 들어오면 메인 루프가 굶는다", "에러 로그 한 줄이 초당 1000번 찍혀 UART 가 막힌다" — 펌웨어 면접의 rate limiter 문제는 **나눗셈·부동소수점·malloc 없이, 정해진 메모리로, 증명 가능한 상한**을 묻는다.

Rate limiter 는 요청(메시지, 인터럽트, 패킷 바이트)마다 "지금 통과시켜도 계약을 지키나?" 를 O(1) 에 답한다. 먼저 **계약(contract)** 이 무엇인지 정해야 한다. 이 레슨에는 두 가지가 나온다.

| 계약 | 뜻 | 맞는 도구 |
|---|---|---|
| (b, r) 도착 곡선 | 모든 창 [t, t+T] 에서 통과량 ≤ b + r·T. 평균 속도 r, 한 번에 최대 b | 토큰 버킷 (= GCRA, 연습 1) |
| 창당 N 개 | 길이 W 틱인 **모든** 창 [t−W+1, t] 에서 통과 ≤ N | 슬라이딩 로그 (정확), 고정 창·슬라이딩 카운터 (근사) |

같은 "초당 10개" 라도 두 계약은 다르다. b = 10, r = 10/s 토큰 버킷은 1초 창에 **최대 19~20개**까지 통과시킨다 (가득 찬 10개 + 그 1초 동안 새로 생긴 10개). Part B 표에서 실제로 19 가 나온다.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">토큰 버킷 b=3, r=1/4 토큰/틱 (den=4 로 스케일: 틱마다 level +1, 토큰 1개 = 4)</text>
  <line x1="60" y1="230" x2="680" y2="230" stroke="currentColor"/>
  <line x1="60" y1="60" x2="60" y2="230" stroke="currentColor"/>
  <line x1="60" y1="80" x2="680" y2="80" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="600" y="72" fill="currentColor">cap = b = 3</text>
  <g fill="currentColor" text-anchor="end">
    <text x="54" y="234">0</text><text x="54" y="184">1</text><text x="54" y="134">2</text><text x="54" y="84">3</text>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="60" y="250">0</text><text x="160" y="250">4</text><text x="335" y="250">11</text><text x="360" y="266">12</text><text x="660" y="250">24</text>
  </g>
  <text x="690" y="234" fill="currentColor">틱</text>
  <path d="M60 80 L60 230 L160 180 L160 230 L360 130 L360 230 L660 80" fill="none" stroke="currentColor" stroke-width="2"/>
  <g class="f-ok" stroke="currentColor">
    <circle cx="60" cy="80" r="5"/><circle cx="60" cy="130" r="5"/><circle cx="60" cy="180" r="5"/>
    <circle cx="160" cy="180" r="5"/><circle cx="360" cy="130" r="5"/>
  </g>
  <g class="f-warn" stroke="currentColor">
    <rect x="55" y="225" width="10" height="10"/><rect x="130" y="187" width="10" height="10"/>
    <rect x="330" y="137" width="10" height="10"/>
  </g>
  <text x="76" y="110" fill="currentColor">t=0: 3개 통과, 4번째 거부</text>
  <text x="150" y="208" fill="currentColor">t=3: 0.75 → 거부</text>
  <text x="200" y="160" fill="currentColor">t=4: 1.00 → 통과</text>
  <text x="380" y="150" fill="currentColor">t=11: 1.75 → cost 2 거부, t=12: 2.00 → 통과</text>
  <text x="430" y="110" fill="currentColor">기울기 = r (cap 에서 멈춤)</text>
  <circle cx="80" cy="292" r="5" class="f-ok" stroke="currentColor"/><text x="92" y="296" fill="currentColor">통과(토큰 차감)</text>
  <rect x="215" y="287" width="10" height="10" class="f-warn" stroke="currentColor"/><text x="232" y="296" fill="currentColor">거부 (level 은 그대로)</text>
  <text x="400" y="296" fill="currentColor">1 토큰 미만 나머지(0.75, 1.75)도 버리지 않는다</text>
</svg>
```

**토큰 버킷 vs 리키 버킷(leaky bucket).** 이름이 섞여 쓰이니 정확히 구분하자.

- **리키 버킷 = 큐(queue)로서**: 요청이 크기 B 인 FIFO 에 들어가고 **일정 속도 r 로** 빠져나간다. 출력은 버스트 없이 완전히 고르고, 대가는 지연과 큐가 차면 버림.
- **리키 버킷 = 계량기(meter)로서**: 요청마다 물을 붓고 물은 r 로 샌다, 넘치면 거부. 이건 토큰 버킷의 **거울상**(물 높이 = b − 토큰 수)이라 결정이 똑같다. ATM 의 GCRA 가 이 형태다 (연습 1 에서 결정 일치를 확인).
- **토큰 버킷**: 토큰이 r 로 쌓이고(최대 b) 요청이 꺼내 간다. 쉬었다 오면 **b 만큼 버스트**를 허용한다. 거부하면 policer, 기다리게 하면 shaper (연습 3).

### Part A — 정수 틱 토큰 버킷

- `tb_init(tb, burst, num, den, now)` (주어짐): r = num/den 토큰/틱. `cap = burst·den`, `full_ticks = ⌈cap/num⌉` 를 미리 계산 (나눗셈은 여기서 한 번). 조건 `burst·den ≤ 2^31−1`.
- `tb_refill(tb, now)` — **TODO**. `dt = now − last` (wrap 안전), `dt == 0` 이나 "음수"(> 2^31−1, 늦게 읽은 틱)면 무시, `dt ≥ full_ticks` 면 가득, 아니면 `level += dt·num` 후 cap 으로 자르기.
- `tb_try(tb, now, cost)` — **TODO**. refill → `cost > burst` 면 거부 → `level ≥ cost·den` 이면 차감하고 통과.
- 검사: 손 계산(위 그림), wrap(0xFFFFFFF0 → 0x10), 과거 틱, 매 틱 호출 10^5, **10^9 틱 포화 5가지 속도**, 브루트포스 창 검사기와 결정 대조 + 모든 창 b + rT 전수 검사 (45회 실행).

### Part B — 창 기반 리미터 세 가지

- `fw_try` (주어짐): 고정 창 카운터 — 비교 기준선.
- `sl_try(s, now)` — **TODO**. 슬라이딩 로그: 최근 통과한 N 개 타임스탬프 링, O(1).
- `swc_try(c, now)` — **TODO**. 슬라이딩 카운터: `prev·(W−el) + cur·W < N·W` (나눗셈 없는 비교).
- 검사: 같은 트래픽 5종(steady, bursty, 창 경계 공격, idle→burst, random)에 리미터 5개를 돌려 "길이 W 창 최대 / 2틱 창 최대" 표, 로그·카운터는 브루트포스 기준과 결정 대조, 카운터 적대적 구성 = 증명한 상한 (5가지 (N, W)), 작은 (N, W) 무작위 300회 상한 초과 0.

### Part C — 펌웨어 시나리오

- `guard_on_edge` / `guard_tick` — **TODO**. GPIO 엣지 ISR 폭주 가드: 토큰이 없으면 그 엣지를 버리고 IRQ 를 마스크, 1 ms 하우스키핑이 토큰 ≥ 4 일 때 마스크 해제 (히스테리시스). 10 초 시뮬레이션(정상 20 Hz + 5 kHz 300 ms 폭주 + 50 kHz 20 ms 폭주).
- `logrl_try` — **TODO**. 메시지 ID 별 버킷 8칸 + 공용 overflow 버킷, 거부된 수를 세었다가 다음 출력 줄에 "(+N suppressed)" 로 붙인다.

## 2. Problem (English)

Implement integer-only rate limiters for a firmware target. Time is a free-running `uint32_t` tick counter that wraps at 2^32.

```c
bool tb_init(tbucket_t *tb, uint32_t burst, uint32_t num, uint32_t den, uint32_t now); /* rate = num/den tokens per tick */
bool tb_try(tbucket_t *tb, uint32_t now, uint32_t cost);   /* true = admit and consume `cost` tokens */
bool sl_try(slog_t *s, uint32_t now);                      /* at most N admits in every window of W ticks, exact */
bool swc_try(swc_t *c, uint32_t now);                      /* sliding-window counter approximation */
```

Constraints: no floating point, no division on the per-request path, no dynamic memory, O(1) per call (sliding log: O(1) with N timestamps of memory). The token bucket must not drift: after any call pattern, the total admitted under saturating demand over T ticks must equal exactly `b + floor(T*num/den)`. Elapsed time between calls is < 2^31 ticks; a `now` older than the last seen value must not refill the bucket.

Example (b = 3, rate 1/4 per tick, bucket full at t = 100):

```text
t=100 try(1) x4 -> 1 1 1 0
t=103 try(1)    -> 0      (0.75 tokens)
t=104 try(1)    -> 1
t=111 try(2)    -> 0      (1.75 tokens)
t=112 try(2)    -> 1
```

Follow-ups: prove that admitted traffic satisfies `A[t, t+T] <= b + r*T` for every window. What happens at the tick wrap? How wrong can a sliding-window counter be on adversarial traffic? Who may call `tb_try` — ISR, thread, both?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 일곱 군데(`tb_refill`, `tb_try`, `sl_try`, `swc_try`, `guard_on_edge`, `guard_tick`, `logrl_try`). 트래픽 생성기, 브루트포스 기준 3개, 창 검사기, 시뮬레이터는 주어진다. 하네스는 시간을 64비트로 들고 리미터에는 `(uint32_t)(0xFFFFF000 + t)` 를 준다 — **모든 테스트가 시작 4096 틱 안에 wrap 을 지난다.** 처음 실행 (실제 출력, 중간 생략):

```text
== Part A: token bucket ==
FAIL starter.c:318  tb_try(&tb, 100, 1) && tb_try(&tb, 100, 1) && tb_try(&tb, 100, 1)
…
  saturate 1e9 ticks  b=1000 r=1/3    accepted=0 expected b+floor(rT)=333334088
FAIL starter.c:348  ok
…
  vs brute-force window checker: 45 runs, decision mismatches=37975, window violations=0, runs with a tight window=0
…
  edges=2700  ISR entries=2700  handled=0  dropped-in-ISR=0  lost-while-masked=0  mask events=0  longest mask=0 ms
…
22/54 passed
```

(0.02 초, exit 1.) "window violations=0" 이 스텁에서도 통과하는 것을 눈여겨보자 — **아무것도 안 통과시키는 리미터는 상한을 지킨다.** 그래서 상한 검사만으로는 부족하고, "통과시켜야 할 것을 통과시켰나"(브루트포스 결정 대조, 꽉 찬 창 존재)를 같이 본다.

## 4. 힌트

<details><summary>힌트 1 — 토큰을 den 배로 세라</summary>

r = 1/3 토큰/틱을 정수로 쌓을 수 없다면, 단위를 1/3 토큰으로 바꾸면 된다. `level` 을 "토큰 × den" 으로 두면 틱마다 정확히 `num` 이 들어오고, 통과 비교는 `level ≥ cost·den`. 1 토큰 미만의 나머지가 `level` 에 그대로 남으니 버려지는 것이 없다.

</details>

<details><summary>힌트 2 — 경과 시간은 뺄셈 한 번</summary>

`uint32_t dt = now − last;` 는 C 에서 2^32 로 나눈 나머지 연산이라(부호 없는 정수는 넘쳐도 UB 가 아니다) wrap 을 넘어도 실제 경과가 2^32 미만이면 정확하다. 실제 경과가 2^31 미만이라는 계약을 두면, `dt > 0x7FFFFFFF` 는 "now 가 last 보다 과거" 라는 뜻이 된다 — 무시하라.

</details>

<details><summary>힌트 3 — 곱셈이 넘치지 않게</summary>

`dt` 가 크면 `dt·num` 이 넘친다. 그런데 `dt ≥ ⌈cap/num⌉` 이면 어차피 가득이다. 그 경우를 먼저 처리하면 남은 `dt·num < cap` 이다. 그리고 `last = now` 는 **가득 채우는 경우에도** 해야 한다.

</details>

<details><summary>힌트 4 — 슬라이딩 로그는 "N번째 최근" 하나만 보면 된다</summary>

링에는 최근 통과 N 개가 오래된 순으로 있다. 창 안에 N 개가 있다 ⇔ N 번째로 최근 통과(= 링에서 가장 오래된 `ts[head]`)가 창 안. 즉 `now − ts[head] < W` 면 거부. 통과면 `ts[head]` 를 덮어쓰고 head 를 한 칸 민다. 루프가 필요 없다.

</details>

<details><summary>힌트 5 — 슬라이딩 카운터의 창 넘김</summary>

`el = now − start`. `el ≥ W` 면 창이 k = el/W 개 지나갔다. k == 1 이면 이전 창 = 방금 끝난 창(cur), k ≥ 2 면 이전 창은 **비어 있었다**(0). start 를 kW 만큼, el 도 kW 만큼 줄이는 것을 잊지 말 것. 비교는 `prev·(W−el) + cur·W < N·W`.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 |
|---|---|
| 1. 질문으로 계약 정하기 | "초당 N 개" 가 **임의의 1초 창**인가, 평균 + 버스트인가? 버스트 허용량은? 비용은 개수인가 바이트인가? 거부하면 버리나 늦추나? 누가 호출하나(ISR/스레드/여러 코어)? 시간 소스와 폭은? |
| 2. 가장 단순한 것 + 결함 | 고정 창 카운터: 8 바이트, O(1). 결함: 창 경계에서 2틱 안에 2N (Part B 에서 실측) |
| 3. 정확한 것 | 슬라이딩 로그: 링 N 칸, O(1), 정확. 메모리 4N 바이트가 문제 |
| 4. 펌웨어 표준 | 토큰 버킷: 상태 8 바이트(level, last), 나눗셈 없음, (b, r) 계약을 정확히 — 증명: 모든 창 ≤ b + rT |
| 5. 코드 | 스케일 level, unsigned 경과, full_ticks 선계산, last 갱신 |
| 6. 테스트를 소리 내어 | 손 계산, wrap 경계, 과거 틱, 포화 10^9 틱 = b + ⌊rT⌋, 브루트포스 판정기와 결정 대조, 모든 창 전수 검사, 경계 공격 트래픽 |
| 7. 복잡도 | 호출당 O(1) 시간·O(1) 메모리(로그는 O(N) 메모리). 핫 패스 곱셈 2번, 나눗셈 0번 (Cortex-M4 asm 확인) |

### Part A — 스케일 표현: 드리프트 0 의 증명

상태는 `level ∈ [0, cap]` (단위 1/den 토큰). 한 번의 refill 은 `level ← min(cap, level + dt·num)`, 통과는 `level ← level − cost·den`. 모든 값이 정수이고 **잘라내는(truncate) 연산이 없다** — 반올림이 없으니 오차도 없다. 유일한 "손실" 은 cap 에서 자르는 것인데, 그건 계약 자체(버스트 b)다.

포화 테스트의 기대값: 버킷이 가득(b)에서 시작해 t=0 에 다 쓰고, 이후 호출 간격 ≤ (b−1)·den/num 이면 (소진 후 level < den 이므로) level 이 cap 에 닿지 않는다. 그러면 시각 T 까지 들어온 단위는 정확히 `T·num`, 꺼낸 토큰은 그중 정수 부분 → 총 통과 = b + ⌊T·num/den⌋. 실제 출력:

```text
  saturate 1e9 ticks  b=1000 r=1/3    accepted=333334088 expected b+floor(rT)=333334088
  saturate 1e9 ticks  b=500  r=7/10   accepted=700000266 expected b+floor(rT)=700000266
  saturate 1e9 ticks  b=6400 r=5/2    accepted=2500002355 expected b+floor(rT)=2500002355
  saturate 1e9 ticks  b=2    r=1/1000 accepted=1000001 expected b+floor(rT)=1000001
  saturate 1e9 ticks  b=1000 r=999/1000 accepted=999000749 expected b+floor(rT)=999000749
```

(호출 간격은 무작위, wrap 포함, 속도 5가지 합쳐 약 800만 호출.) 다른 표현과 비교 — `demo_drift.c`, 매 틱 호출 + 포화, 10^9 틱 (실제 출력):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_drift.c -o demo_drift && ./demo_drift
```

```text
burst 10, ticks 0..999999999, called every tick, saturating drain; diff = accepted - (b + floor(T*num/den))
  rate       expected    exact   Q16.16    float   double
  1/3       333333343       +0    -5087       +0       +0
  1/10      100000009       +0    +6104      +12       +0
  7/10      700000009       +0    -3052      -24       +0
  5/9       555555565       +0    +1695      +26       +0
  2/3       666666676       +0    +5086       +0       -1
  total seconds (5 rates): exact 20.5  Q16 4.8  float 21.3  double 28.1
  int-div r=1/3 called every  1 ticks: accepted=10 (0.0000 of expected)
  int-div r=1/3 called every  2 ticks: accepted=10 (0.0000 of expected)
  int-div r=1/3 called every  7 ticks: accepted=285714294 (0.8571 of expected)
  int-div r=1/3 called every 25 ticks: accepted=320000002 (0.9600 of expected)
```

읽는 법:

- **Q16.16**: 속도를 65536 분의 정수로 반올림한다. 1/3 → 21845/65536, 틱당 1/196608 토큰 부족 → 10^9 틱에 ≈ 5086 부족 (실측 −5087). 1/10 → 6554/65536 은 틱당 0.4/65536 **초과** → +6104 (계산 10^9 × 0.4/65536 ≈ 6103.5). 고정소수점은 den 이 2의 거듭제곱일 때만 정확하다.
- **float**: 1/3, 2/3 에서는 우연히 0 이다 (3틱 주기마다 1.0 으로 정확히 반올림되어 돌아오는 것으로 보인다). 1/10, 7/10, 5/9 에서는 ±12~26. **double** 도 2/3 에서 −1. "대부분 맞는다" 는 증명이 아니다.
- **int-div** (`add = dt·num/den; last = now`): 나머지를 버리고 last 를 now 로 옮긴다. 매 틱 부르면 `1/3 = 0` 이라 **영영 안 찬다**(초기 10개가 전부). 7틱마다 부르면 ⌊7/3⌋ = 2 → 6/7 = 0.857, 25틱마다면 8/8.33 = 0.96. **호출 빈도에 따라 속도가 바뀌는** 버그 — ISR 이 자주 부를수록 더 굶는다. (시간은 비교 의미 없음: 측정 때 load averages 46~68 로 기계가 매우 바빴고, 이 데모의 exact 는 소진 계산에 매 틱 나눗셈을 넣었다. 실제 `tb_try` 핫 패스엔 나눗셈이 없다 — 아래 asm.)

### Part A — wrap, 과거 틱, 넘침

**wrap.** C11 6.2.5p9: 부호 없는 연산은 넘치지 않고 2^32 로 나눈 나머지가 된다. 그래서 `now − last ≡ (실제 경과) (mod 2^32)` 이고, 실제 경과가 [0, 2^32) 이면 값도 같다. 테스트: 0xFFFFFFF0 에서 비우고 0x00000010 에 8토큰 (32틱 × 1/4). 주의: 틱이 `uint16_t` 면 `now − last` 가 **int 로 승격**되어 음수가 나온다 → `(uint16_t)(now − last)` 로 다시 잘라야 한다 (7. 실수 4).

**과거 틱.** 태스크가 `now = tick` 을 읽은 직후 ISR 에 선점되고, ISR 이 더 새 틱으로 `last` 를 갱신했다면, 태스크의 `now − last` 는 −1 = 0xFFFFFFFF 다. 이걸 "2^32−1 틱 경과" 로 읽으면 **버킷이 가득 찬다** (실수 2, 실제로 테스트 2개 FAIL). 계약 "호출 간격 < 2^31" 아래에서 `dt > 0x7FFFFFFF` 는 음수 경과이므로 무시한다. 대가: 2^31 틱(1 kHz 틱이면 2^31 ms ≈ 24.9일) 넘게 아무도 안 부르면 그 뒤로도 최대 2^31 틱 동안 "과거" 로 보여 **refill 이 멈춘다**. 그래서 Part C 의 `guard_tick` 은 마스크 상태가 아니어도 매 틱 `tb_refill` 을 불러 last 를 최신으로 유지한다. 64비트 틱이면 문제가 사라지지만 Cortex-M4 에서 64비트 읽기는 원자적이지 않다 (C-M14 찢어진 읽기).

**넘침.** `dt ≥ full_ticks` 면 가득으로 끝. 아니면 dt ≤ ⌈cap/num⌉ − 1 < cap/num 이므로 dt·num < cap ≤ 2^31−1, level + dt·num < 2·cap < 2^32. 그래서 init 조건이 `burst·den ≤ 2^31−1` 이다. 핫 패스는 32비트 곱셈 두 번(`dt·num`, `cost·den`)뿐이다.

### Part A — 창 정리: 모든 창에서 ≤ b + r·T

**정리.** 통과한 요청 비용을 틱 [t, u] 에 대해 더한 것을 A[t, u] 라 하자. 모든 t ≤ u 에서 A[t, u] ≤ b + r·(u − t). 통과량이 정수이므로 A[t, u] ≤ b + ⌊num·(u−t)/den⌋.

**보조정리 (게으른 refill = 매 틱 refill).** cap(y) = min(C, y) 라 하면 a, a′ ≥ 0 에 대해 cap(cap(x + a) + a′) = cap(x + a + a′). 증명: x + a ≤ C 면 양변 모두 cap(x + a + a′). x + a > C 면 좌변 = cap(C + a′) = C, 우변도 x + a + a′ > C 이므로 C. ∎ 따라서 호출 때만 몰아서 채운 level 은 "매 틱 r 씩 채우고 cap 으로 자른" 가상의 level E(τ) 와 모든 호출 시점에서 같다 (호출 사이에는 차감이 없으니까).

**증명.** E(t) ≤ b (cap). t 부터 u 까지 들어오는 양 ≤ r·(u − t) (cap 은 줄이기만 한다). 통과마다 비용만큼 빠지고 level ≥ 0 이 유지된다. 그러므로 A[t, u] ≤ E(t) + r(u − t) − E_끝 ≤ b + r(u − t). ∎

**역방향 — 토큰 버킷은 탐욕적으로 최적이다.** 재귀 ℓ ← min(b, ℓ − 통과량 + r·Δ) 를 풀면, 요청 k 직전 level 은

ℓ_k = min( b , min_{j: 통과한 이전 요청} [ b + r(s_k − s_j) − A(j .. k−1) ] )

이다 (min-plus 대수의 펼치기; 거부된 요청에서의 cap 항은 그다음 통과 항보다 항상 크거나 같아서 빠진다). 그러므로 cost ≤ ℓ_k ⇔ "이 요청까지 통과시켜도 s_k 로 끝나는 모든 창이 계약을 지킨다". 즉 토큰 버킷은 **계약을 깨지 않는 요청은 하나도 거부하지 않는다**. 하네스의 `ref_tb_ok()` 가 바로 이 판정기를 통과 로그 전체를 훑는 O(n) 브루트포스로 구현한 것이다. 실제 출력:

```text
  vs brute-force window checker: 45 runs, decision mismatches=0, window violations=0, runs with a tight window=45
```

45회 = 패턴 5종 + 무작위 비용(1..b) 트래픽 40종(매개변수 5세트). 결정 불일치 0, 모든 창(O(n²) 전수) 위반 0, 그리고 **매 실행마다 등호에 닿는 창이 있다**(정수 통과량 = ⌊b + rT⌋) — 상한이 빡빡하다는 뜻.

손 계산 추적 (테스트의 첫 블록, b=3, r=1/4, den=4 → cap=12, full_ticks=12):

| now | dt | refill 후 level | 요청 | 결과 | 이후 level | 토큰 |
|---|---|---|---|---|---|---|
| 100 | 0 | 12 | 1 ×3 | 통과 ×3 | 0 | 0 |
| 100 | 0 | 0 | 1 | 거부 | 0 | 0 |
| 103 | 3 | 3 | 1 | 거부 (3 < 4) | 3 | 0.75 |
| 104 | 1 | 4 | 1 | 통과 | 0 | 0 |
| 111 | 7 | 7 | — (`tb_tokens`) | 1 토큰 | 7 | 1.75 |
| 111 | 0 | 7 | 2 | 거부 (7 < 8) | 7 | 1.75 |
| 112 | 1 | 8 | 2 | 통과 | 0 | 0 |
| 112 | 0 | 0 | 4 | 거부 (cost > b) | 0 | 0 |
| 124 | 12 | 12 (dt ≥ full_ticks) | — | 3 토큰 | 12 | 3 |

### Part B — 고정 창: 경계에서 2N

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">계약 "길이 W 창마다 N" 을 깨는 두 가지 공격 (N=10, W=100)</text>
  <text x="10" y="52" fill="currentColor" font-weight="bold">① 고정 창 카운터: 창 경계 양쪽에 N 씩</text>
  <line x1="40" y1="120" x2="700" y2="120" stroke="currentColor"/>
  <g stroke="currentColor" stroke-dasharray="4 3">
    <line x1="60" y1="62" x2="60" y2="130"/><line x1="280" y1="62" x2="280" y2="130"/>
    <line x1="500" y1="62" x2="500" y2="130"/>
  </g>
  <g fill="currentColor" text-anchor="middle"><text x="60" y="146">0</text><text x="280" y="146">W</text><text x="500" y="146">2W</text></g>
  <rect x="268" y="70" width="10" height="50" class="f-warn" stroke="currentColor"/>
  <rect x="282" y="70" width="10" height="50" class="f-warn" stroke="currentColor"/>
  <text x="200" y="88" fill="currentColor" text-anchor="end">W−1 에 10개</text>
  <text x="300" y="88" fill="currentColor">W 에 10개 (새 창, count=0)</text>
  <text x="300" y="108" fill="currentColor">→ 2틱 창에 20 = 2N (실측 max/2tk = 20)</text>
  <text x="10" y="182" fill="currentColor" font-weight="bold">② 슬라이딩 카운터: 이전 창 끝에 N, 지금 창 el=W−2 에 홍수</text>
  <line x1="40" y1="260" x2="700" y2="260" stroke="currentColor"/>
  <g stroke="currentColor" stroke-dasharray="4 3">
    <line x1="60" y1="192" x2="60" y2="270"/><line x1="280" y1="192" x2="280" y2="270"/>
    <line x1="500" y1="192" x2="500" y2="270"/>
  </g>
  <g fill="currentColor" text-anchor="middle"><text x="60" y="286">W</text><text x="280" y="286">2W</text><text x="500" y="286">3W</text></g>
  <rect x="268" y="210" width="10" height="50" class="f-warn" stroke="currentColor"/>
  <rect x="490" y="211" width="8" height="49" class="f-warn" stroke="currentColor"/>
  <text x="80" y="224" fill="currentColor">2W−1 에 N=10 (prev 창)</text>
  <text x="300" y="224" fill="currentColor">3W−2: 추정 = 10·(2/100) + cur</text>
  <text x="300" y="242" fill="currentColor">→ cur 를 9.8 미만까지 = 10개 더</text>
  <rect x="268" y="296" width="230" height="14" class="f-acc" stroke="currentColor"/>
  <text x="510" y="308" fill="currentColor">길이 W 창 [2W−1, 3W−2] 에 20</text>
  <text x="10" y="326" fill="currentColor">일반식 N + ⌈N(W−2)/W⌉: 가중치 (W−el)/W 는 "고르게 퍼졌다" 는 가정 — 끝에 몰리면 틀린다</text>
</svg>
```

고정 창은 창마다 `count ≤ N` 이므로 어느 길이-W 창이든 정렬된 창 2개에만 걸쳐 **≤ 2N**, 그리고 위 ① 이 2N 에 정확히 닿는다. 실측(아래 표 "edge 10|10"): max/W = 20, max/2tk = 20.

### Part B — 슬라이딩 로그: 링 N 칸, O(1), 정확

**불변식**: 링은 지금까지 통과한 것 중 가장 최근 min(통과 수, N) 개를 오래된 순으로 담고, `ts[head]` 가 그중 가장 오래된 것이다.

**판정**: 창 [now−W+1, now] 안의 통과 수 < N ⇔ (통과 수 < N) 또는 (N 번째 최근 = `ts[head]` 가 창 밖, 즉 `now − ts[head] ≥ W`). (⇐) 타임스탬프가 정렬돼 있으니 N 번째가 밖이면 더 오래된 건 다 밖이고, 창 안에는 최대 N−1 개. (⇒) N 번째가 창 안이면 그보다 최근 N−1 개도 창 안 → N 개. ∎ 통과하면 `ts[head]` 를 now 로 덮어쓰고 head 를 민다 → 불변식 유지. 거부된 요청은 기록하지 않는다 (기록하는 변형은 "계속 두드리면 계속 막힘" 이 되는 다른 정책이다).

길이-W 창 ≤ N 을 모든 실행에서 확인했고, `ref_log_ok()` (통과 로그를 거꾸로 세는 브루트포스)와 결정 불일치 0. 대가는 **메모리 4N 바이트**: N = 1000 이면 4 KB — 작은 MCU 에선 크다. 연습 4 의 K-슬롯 카운터가 그 중간이다.

### Part B — 슬라이딩 카운터: 상한 N + ⌈N(W−2)/W⌉ 의 증명

창을 [kW, (k+1)W) 로 나누고, 지금 창 k 의 통과 수 c, 이전 창의 통과 수 p, 창 안 경과 el. 규칙: p·(W−el) + c·W < N·W 면 통과.

**정리.** 어느 길이-W 창이든 통과 ≤ N + ⌈N(W−2)/W⌉ (W ≥ 2), 그리고 이 값에 닿는 트래픽이 있다.

**증명.** 끝 틱 t 가 창 k 의 el 위치인 길이-W 창을 보자. el = W−1 이면 창 k 와 정확히 같아 ≤ N (매 통과 직전 c < N). el ≤ W−2 면 창 k−1 의 꼬리(p′ ≤ p 개)와 창 k 의 [0, el] (c 개)에 걸친다. 창 k 에서 el 까지 마지막 통과가 el′ ≤ el 에서 c 번째였다면 p(W−el′) + (c−1)W < NW, p(W−el′) ≥ p(W−el) 이므로 c ≤ N − ⌊p(W−el)/W⌋. 합하면 p′ + c ≤ p + N − ⌊p(W−el)/W⌋ = N + ⌈p·el/W⌉ ≤ N + ⌈N(W−2)/W⌉ (p ≤ N, el ≤ W−2). **도달**: 창 k−1 앞이 비어 있으면 p 창의 끝 틱에 N 개가 다 통과(추정 = c). 창 k 의 el = W−2 에 홍수를 보내면 c < N(W−2)/W 인 동안 통과 → c = ⌈N(W−2)/W⌉. 두 무리는 W−1 틱 떨어져 한 길이-W 창에 들어간다. ∎

실제 출력 (`swc_adversary()`, 그리고 작은 (N, W) 무작위 300회):

```text
  sliding counter adversary N=10  W=100  max/W=20 bound N+ceil(N(W-2)/W)=20 (2.00x N)
  sliding counter adversary N=50  W=20   max/W=95 bound N+ceil(N(W-2)/W)=95 (1.90x N)
  sliding counter adversary N=7   W=3    max/W=10 bound N+ceil(N(W-2)/W)=10 (1.43x N)
  sliding counter adversary N=100 W=1000 max/W=200 bound N+ceil(N(W-2)/W)=200 (2.00x N)
  sliding counter adversary N=3   W=2    max/W=3 bound N+ceil(N(W-2)/W)=3 (1.00x N)
  random small (N,W) x300: runs over bound = 0
```

결론이 생각보다 나쁘다: **N < W/2 면 최악은 고정 창과 같은 2N** 이다. 차이는 버스트의 모양이다 — 고정 창은 2N 을 **2틱 안에** 허용하지만 슬라이딩 카운터의 2N 은 W−1 틱에 걸쳐 있고, 경계 직후(el 작음)엔 이전 창 가중치가 커서 거의 막힌다(표의 edge 행: max/2tk = 10). Cloudflare 는 실제 트래픽 4억 요청(27만 출처)에서 잘못 허용/거부된 비율이 0.003% 였다고 보고했다 — **실제 트래픽**의 평균 오차이고, 적대적 최악과는 다른 질문이다.

### Part B — 같은 트래픽, 다섯 리미터

계약 N = 10 / W = 100 틱. 토큰 버킷 두 설정: 흔한 설정 b = 10, r = 1/10 (평균 속도 같음), 그리고 길이-W 창 ≤ N 을 보장하는 b + r(W−1) ≤ N 설정 b = 1, r = 1/11 (1 + 99/11 = 10). 길이 3000 틱 (실제 출력, `solution.c`):

| 트래픽 | 리미터 | 제출 | 통과 | 최대/W | 최대/2틱 |
|---|---|---|---|---|---|
| steady 1/5 | TB b=10 r=1/10 | 600 | 309 | 19 | 1 |
| | TB b=1 r=1/11 | 600 | 200 | 7 | 1 |
| | fixed window | 600 | 300 | 10 | 1 |
| | sliding log | 600 | 300 | 10 | 1 |
| | sliding counter | 600 | 300 | 10 | 1 |
| bursty 15@150 | TB b=10 r=1/10 | 300 | 200 | 10 | 10 |
| | TB b=1 r=1/11 | 300 | 20 | 1 | 1 |
| | fixed window | 300 | 200 | 10 | 10 |
| | sliding log | 300 | 200 | 10 | 10 |
| | sliding counter | 300 | 150 | 10 | 10 |
| edge 10\|10 | TB b=10 r=1/10 | 300 | 150 | 10 | 10 |
| | TB b=1 r=1/11 | 300 | 15 | 1 | 1 |
| | fixed window | 300 | 300 | **20** | **20** |
| | sliding log | 300 | 150 | 10 | 10 |
| | sliding counter | 300 | 150 | 10 | 10 |
| idle→burst | TB b=10 r=1/10 | 2950 | 159 | **19** | 10 |
| | TB b=1 r=1/11 | 2950 | 137 | 10 | 1 |
| | fixed window | 2950 | 150 | 10 | 10 |
| | sliding log | 2950 | 150 | 10 | 10 |
| | sliding counter | 2950 | 150 | 10 | 10 |
| random | TB b=10 r=1/10 | 1836 | 308 | 19 | 4 |
| | TB b=1 r=1/11 | 1836 | 212 | 9 | 1 |
| | fixed window | 1836 | 300 | 20 | 8 |
| | sliding log | 1836 | 291 | 10 | 7 |
| | sliding counter | 1836 | 300 | 12 | 4 |

```text
  decision mismatches vs brute force: sliding log=0, sliding counter=0
  worst max/W over all runs: fixed=20 (bound 2N=20), log=10 (N=10), counter=13 (bound 20)
```

읽을 것:

- **TB b=10** 은 길이-W 창에 19 를 허용한다 (b + ⌊r(W−1)⌋ = 10 + 9). 계약이 다르다 — 틀린 게 아니다. "임의의 1초에 N 개" 가 하드 요구면 토큰 버킷을 그대로 쓰면 안 된다.
- **TB b=1** 은 창 계약을 지키지만 **처리량을 잃는다**. steady 1/5 에서 200 (요청 15틱마다 하나만): 11틱에 토큰이 차는데 다음 요청은 15틱 → 4틱 동안 cap(=1)에 막혀 버려진다. 버스트 여유가 0 이면 도착 패턴이 속도와 안 맞을 때마다 손해다. bursty 에선 20/300.
- **슬라이딩 카운터**는 과허용(random 에서 12, 30 시드 중 최악 13)과 **과소허용**을 둘 다 한다: bursty 에서 150 (로그는 200). 150틱마다 오는 뭉치가 이전 창 앞쪽에 몰려 있는데 "고르게 퍼졌다" 고 가정해 가중치를 과하게 준다.
- **슬라이딩 로그**만 모든 행에서 ≤ 10 이면서 허용 가능한 것은 다 허용한다 (브루트포스 결정 일치).

### Part C — GPIO 인터럽트 폭주 가드

리미터를 ISR 안에서 "버리기" 로만 쓰면, 50 kHz 채터링에서 ISR 진입 자체가 초당 5만 번이다 — 버리는 판정이 아무리 싸도 진입/복귀 비용과 핸들러 앞부분이 CPU 를 먹는다(극단에선 수신 라이브록, receive livelock). 그래서 토큰이 바닥나면 **IRQ 를 마스크**해 진입을 멈추고, 1 ms 하우스키핑이 토큰이 `resume` (= 4) 개 모였을 때 푼다. 1개만 모이면 바로 푸는 식이면 폭주 중 10틱마다 마스크/해제를 반복한다 — 히스테리시스(hysteresis)가 그걸 막는다. 실제 출력:

```text
== Part C1: GPIO IRQ storm guard (1 ms tick, b=8, r=1/10 per tick = 100/s, resume at 4) ==
  edges=2700  ISR entries=245  handled=236  dropped-in-ISR=9  lost-while-masked=2455  mask events=9  longest mask=40 ms
  normal 20 Hz edges: 193/200 handled overall, 191/191 outside storm(+50 ms)
```

- 엣지 2700개 중 ISR 진입 245회 (9%). 마스크된 동안의 2455개는 ISR 이 아예 안 돈다.
- 마스크 9회, 가장 긴 마스크 40 ms = 토큰 4개 × 10 ms. 폭주 B(50 kHz, 20 ms)는 한 번의 마스크로 흡수.
- 정상 20 Hz 엣지 200개 중 폭주 구간(+50 ms) **밖의 191개는 전부** 처리, 안의 9개 중 2개 처리. 처리량은 모든 창에서 8 + T/10 이하 (창 정리, 전수 검사).

실제 칩에서는: 마스크 = 해당 EXTI 줄의 인터럽트 마스크 비트 클리어 또는 `NVIC_DisableIRQ`, 해제 전에 **대기 중 플래그를 먼저 지운다**(안 지우면 해제 즉시 오래된 엣지로 한 번 더 들어온다). 레지스터 이름·순서는 칩마다 다르다 — 확인 권장. 폭주 동안 잃은 엣지 수는 하우스키핑에서 "storm: N edges lost" 로 한 번 보고하는 것이 좋다 (C2 와 같은 생각).

**ISR/스레드 문맥 (한 문단).** 토큰 버킷 상태는 `level` 과 `last` **두 워드**이고 refill-비교-차감은 읽기-수정-쓰기다. (1) 한 버킷을 **한 문맥만** 쓰면(이 가드에서 `guard_on_edge` 는 EXTI ISR, `guard_tick` 은 SysTick — 둘이 다른 우선순위라면 서로 선점할 수 있다!) 잠금이 필요 없다. 이 시뮬레이션은 단일 스레드라 이 문제를 보지 않는다. 실제로는 두 ISR 을 같은 우선순위로 두거나(서로 선점 불가), 짧은 쪽을 임계 구역으로 감싼다. (2) 여러 문맥이 같은 버킷을 쓰면 `tb_try` 전체를 PRIMASK/BASEPRI 임계 구역에 넣는다 — 루프·나눗셈이 없는 30여 명령이라 인터럽트 지연이 짧고 상한이 있다. (3) `now` 를 읽는 것은 32비트 정렬 로드라 Cortex-M3/M4 에서 원자적이다. 늦게 읽은 now 는 Part A 의 "과거 틱" 규칙이 안전하게 무시한다. (4) 멀티코어는 코어별 버킷(속도 r/k 씩 — 버스트를 공유 못 함)이나 스핀락/HW 세마포어. 한 워드 상태인 GCRA(연습 1)는 LDREX/STREX CAS 루프로 잠금 없이 만들 수 있다 (DS-A01 의 원자 연산).

### Part C — 메시지 ID 별 로그 제한

```text
== Part C2: per-ID log limiter (b=3, 1 msg/s, 8 slots + overflow) ==
  id7: offered=5000 printed=7 reported-suppressed=3994 pending=999 first report=+997 ; ids 1,2: 8/8 printed
```

- ID 7(ADC overrun)이 5초 동안 매 ms: 출력 7 = 3 + ⌊4999/1000⌋. 보존 법칙 7 + 3994 + 999 = 5000 (출력 + 보고된 억제 + 아직 보고 안 된 억제 = 제출).
- t=1000 의 줄이 "(+997 suppressed)" 를 단다 (t=3..999).
- ID 1, 2 는 폭주의 영향을 전혀 안 받는다(8/8) — **ID 별 격리**가 전역 버킷 하나보다 나은 이유. 슬롯 8개가 다 차면 새 ID 는 공용 overflow 버킷을 같이 쓴다 (테스트: 슬롯 5개 + overflow 버스트 3 = 8 통과, 9번째부터 억제).

Linux 커널의 `printk_ratelimited` 계열도 같은 문제를 푼다: 기본값 `DEFAULT_RATELIMIT_INTERVAL (5 * HZ)`, `DEFAULT_RATELIMIT_BURST 10` (include/linux/ratelimit_types.h). 다만 `___ratelimit()` 은 토큰 버킷이 아니라 **첫 호출에 고정되는 창**(begin 부터 interval 동안 burst 개, 창이 지나면 다음 호출 시각에서 새 창) 이고, 억제된 수를 세어 보고한다 (lib/ratelimit.c, 2026-11-11 master 기준). 고정 창 계열이므로 경계에서 거의 2배가 나온다 — 연습 2 에서 전수 탐색으로 2b−1 (2틱 창) 을 확인한다.

### 메모리 / 정확도 트레이드오프

| 방식 | 최소 상태 (파라미터는 flash 상수) | 이 레슨 구조체 | 보장 | 길이-W 창 최악 | 2틱 창 최악 | 메모 |
|---|---|---|---|---|---|---|
| 토큰 버킷 | level + last = 8 B | 28 B | 모든 창 ≤ b + rT, **탐욕적 최적** | b + ⌊r(W−1)⌋ | b + ⌊r⌋ | 나눗셈 0, 드리프트 0 |
| GCRA (연습 1) | TAT 하나 4~8 B | 24 B (u64) | 토큰 버킷과 결정 동일 | 같음 | 같음 | 한 워드 → CAS 가능 |
| 고정 창 | start + count = 8 B | 16 B | 정렬된 창마다 ≤ N | **2N** | **2N** | 가장 싸고 가장 거침 |
| Linux 식 고정 창 (연습 2) | begin + left ≈ 8 B | — | 첫 호출 기준 창마다 ≤ b | 2b−1 (I+1 틱) | 2b−1 | 억제 수 보고가 핵심 |
| 슬라이딩 카운터 | start + prev + cur = 12 B | 20 B | 근사 | N + ⌈N(W−2)/W⌉ ≈ 2N | 작음 (경계 직후 막힘) | 과허용 + 과소허용 |
| K-슬롯 보수적 (연습 4) | 4(K+1) + 8 B | — | 모든 W 창 ≤ N | **N** | — | 처리량 N/(W + W/K) |
| 슬라이딩 로그 | 4N + 8 B | 4·256 + 16 B | 모든 W 창 ≤ N, 탐욕적 최적 | **N** | — | 정확, N 이 크면 비쌈 |

속도는 차이가 의미 없다 — `demo_bench.c` (Apple M6, Apple clang 21.0.0, -O2, 10^7 요청, 워밍업 + 7회 중앙값; 측정 직전 `uptime` load averages **40.45 54.27 41.02** — 다른 작업이 많이 돌던 중):

```text
10000000 requests over 6666086 ticks
  token bucket (b=100, r=1/10)     state   28 B  passed   666708  ns/decision median 2.21 (min 2.11, max 2.61)
  fixed window (N=100, W=1000)     state   16 B  passed   666700  ns/decision median 0.40 (min 0.36, max 0.51)
  sliding log  (N=100, W=1000)     state  416 B  passed   666359  ns/decision median 1.07 (min 0.91, max 1.40)
  sliding counter (N=100, W=1000)  state   20 B  passed   666609  ns/decision median 1.90 (min 1.79, max 2.27)
```

바로 다음 실행은 2.36 / 0.48 / 1.11 / 2.02 ns. 모두 결정당 수 ns — 선택 기준은 **계약·메모리·정확도**다. (토큰 버킷이 가장 느린 건 곱셈과 분기가 많아서로 보이지만 확인은 안 했다.)

### Cortex-M 코드 생성 — 핫 패스에 나눗셈이 없나

`demo_cm4.c` (freestanding, 실행 안 함):

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_cm4.c -o -
clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S demo_cm4.c -o -
```

이 레슨의 `tb_try_exact` 의 Cortex-M4 출력 앞부분 (실제, 지시어 제거):

```text
tb_try_exact:
	ldr	r3, [r0, #24]          @ last
	subs.w	r12, r1, r3        @ dt = now - last (플래그도 갱신)
	beq	.LBB0_6                @ dt == 0
	cmp.w	r12, #0
	bmi	.LBB0_6                @ dt > 0x7FFFFFFF  →  부호 비트 검사로 바뀜
	ldr	r3, [r0, #20]          @ full_ticks
	str	r1, [r0, #24]          @ last = now
	cmp	r12, r3
	bhs	.LBB0_4                @ dt >= full_ticks → 가득
	...
	mla	r3, r3, r12, lr        @ level + num*dt  (곱셈-누산 1개)
	...
	muls	r2, r3, r2         @ need = cost*den
```

(`@` 뒤 주석은 내가 붙인 것.) 함수별 요약 (실제 출력에서 센 것):

| 함수 | Cortex-M4 | Cortex-M0 |
|---|---|---|
| `tb_try_exact` (이 레슨) | 35 명령, `mla` + `muls`, 나눗셈 없음 | 34 명령, `muls` 2개 |
| `tb_try_div` (`dt*num/den`) | 22 명령, `udiv` | 29 명령, `bl __aeabi_uidiv` (라이브러리 호출) |
| `swc_try` | `udiv` 1개 (창 넘김 쪽에만) + `mla`/`mls`/`muls` | `bl __aeabi_uidiv` |

M0 에는 하드웨어 나눗셈이 없어 `__aeabi_uidiv` 소프트웨어 루틴을 부른다 (ALGO-M15). 나눗셈을 init 으로 옮긴 `full_ticks` 덕분에 토큰 버킷은 M0 에서도 곱셈만 쓴다. 슬라이딩 카운터의 `el / W` 는 W 가 2의 거듭제곱이면 시프트가 된다.

### 복잡도 정리

| | 시간/호출 | 메모리 | 최악 경로 |
|---|---|---|---|
| `tb_try` | O(1), 곱셈 2 | 8 B 상태 | 분기 몇 개, 루프 없음 |
| `sl_try` | O(1) | 4N B | 모듈로 1회 (N 이 2^k 면 마스크) |
| `swc_try` | O(1) | 12 B | 창 넘김 때 나눗셈 1회 |
| `logrl_try` | O(슬롯) = O(8) | 슬롯당 ~36 B | 선형 검색 (슬롯이 많으면 해시) |
| 테스트의 창 전수 검사 | O(n²) | — | 테스트 쪽만 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 54/54 passed (3회 동일 출력, 0.05 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san   # 54/54, 보고 없음
```

## 7. 자주 하는 실수

아래 1·2·3·5·6 은 `solution.c` 의 사본에 실제로 심어 돌린 결과다 (lesson 폴더 밖, 사본은 남기지 않음).

**1. 나머지를 버린다 (refill 반올림).**

```c
tb->level = (lv < tb->cap ? lv : tb->cap) / tb->den * tb->den;   /* 틀림: 1 토큰 미만 버림 */
```

매 호출 refill 이 1 토큰이 안 되면 영영 안 찬다. 실제: 38/54, b=2·r=1/1000 포화에서 **2000 vs 기대 1000001**, 손 계산 4개 FAIL. 같은 병의 다른 얼굴이 `add = dt·num/den; last = now` (demo_drift 의 int-div: 매 틱 호출이면 0, 7틱마다면 0.857). → 스케일된 level 에 나머지를 보관.

**2. "과거 틱" 을 거대한 경과로 읽는다.** `if (dt == 0) return;` 만 두면 늦게 읽은 now 에서 dt = 0xFFFFFFFF → 버킷이 가득. 실제: 52/54 (과거 틱 테스트 2개). 필드에서는 "가끔 버스트 한도가 무시된다" 로 보인다 — 선점 타이밍에 달려 재현이 어렵다. → `dt > 0x7FFFFFFF` 무시 + 2^31 틱 안에 한 번은 부르는 하우스키핑.

**3. 가득 채우는 경로에서 `last = now` 를 빼먹는다.**

```c
if (dt >= tb->full_ticks) { tb->level = tb->cap; return; }   /* 틀림: last 갱신 전에 return */
tb->last = now;
```

한 번 오래 쉬면 last 가 옛날에 멈춰서 그 뒤 모든 호출이 "가득" 이 된다. 실제: idle→burst 에서 **2950/2950 통과, 길이-W 창 1550** (계약 19), 결정 불일치 26734, 창 위반 35회. 손 계산 테스트는 전부 통과했다 — 무작위·패턴 트래픽 대조가 잡았다.

**4. `uint16_t` 틱에서 `now − last` 를 그대로 쓴다.** 두 uint16 은 int 로 승격되어 빼지므로 wrap 직후 −5 같은 음수가 나온다(C11 6.3.1.1 정수 승격). → `uint16_t dt = (uint16_t)(now − last);` 그리고 계약은 2^15 틱. (이 실수는 이 레슨 코드가 uint32 라 주입하지 않았다 — 원리 설명.)

**5. 슬라이딩 로그 창 경계를 한 칸 틀린다.** `now − ts[head] < W − 1` 로 쓰면 창이 W−1 틱이 되어 과허용. 실제: 52/54, 결정 불일치 1682, 길이-W 창 최대 **14** (N=10). 반대로 `≤ W` 면 W+1 틱 창이라 과소허용 — 둘 다 "창 = [now−W+1, now]" 를 먼저 적어 두면 피한다.

**6. 슬라이딩 카운터 창 넘김을 대충 한다.** (a) 2개 이상 창을 건너뛰었는데 `prev = cur` → 오래된 개수가 가중됨: 결정 불일치 127. (b) `el -= k·W` 를 빼먹음 → W−el 이 언더플로: 불일치 948, 그리고 (N=3, W=2) 에서 상한 3 을 넘어 **4**, 무작위 300회 중 40회 상한 초과.

**7. 부동소수점 토큰.** "double 이면 충분" — demo_drift 에서 double 도 2/3 속도에서 10^9 틱에 −1, float 은 ±12~26. 장기 계약이 증명 불가능해지고, FPU 없는 M0 에선 소프트웨어 실수 연산이다. 고정소수점 Q16 은 den 이 2^k 가 아니면 속도 자체가 틀려 ±수천.

**8. 아무것도 안 통과시키는 구현이 상한 테스트를 통과한다.** 스텁 출력의 "window violations=0" 이 그 예. 테스트는 반드시 **완전성**(통과시켜야 할 것을 통과시켰나: 브루트포스 결정 일치, 꽉 찬 창 존재, 포화 통과 수)도 본다.

## 8. 면접 Q&A

<details><summary>Q1. 토큰 버킷과 리키 버킷은 같은 건가요?</summary>

반은 같다. 리키 버킷을 **계량기**(요청마다 물 붓기, r 로 샘, 넘치면 거부)로 쓰면 물 높이 = b − 토큰 수인 거울상이라 결정이 같다 — ATM 의 GCRA 가 이 형태이고, 연습 1 에서 무작위 20만 결정이 토큰 버킷과 한 건도 다르지 않았다. 리키 버킷을 **큐**(FIFO 에서 일정 속도로 내보냄)로 쓰면 출력에 버스트가 전혀 없고 지연이 생긴다. 토큰 버킷 셰이퍼(연습 3)는 큐 + 토큰 버킷이라 b 만큼은 몰아서 내보낸다. 꼬리: "네트워크 장비에서는?" → policer 는 넘는 패킷을 버리거나 색을 칠하고(RFC 2697/2698 의 3색 마커, NETHW-M10), shaper 는 큐에 넣어 늦춘다(Linux `tbf`, NET-M12).

</details>

<details><summary>Q2. 부동소수점 없이 0.333 토큰/틱을 어떻게 정확히 하죠? 드리프트가 없다는 걸 증명할 수 있나요?</summary>

토큰을 den 배로 센다: level 단위 = 1/den 토큰, 틱마다 +num. 모든 연산이 정수 덧셈·뺄셈·비교·cap 이라 반올림이 없다. 포화 수요에서 T 틱 동안 들어온 단위는 정확히 T·num(cap 에 안 닿는 한) → 통과 = b + ⌊T·num/den⌋. 10^9 틱, 속도 5가지에서 정확히 일치했다. 반면 Q16 은 1/3 을 21845/65536 으로 반올림해 −5087, float 은 ±26 까지. 꼬리: "속도가 3.7 토큰/초, 틱이 1 ms 면?" → num/den = 37/10000. cap = b·10000 ≤ 2^31−1 이면 b ≤ 214748. 넘치면 64비트 level 이나 단위 변경.

</details>

<details><summary>Q3. 틱 카운터가 넘어가면(wrap) 어떻게 되나요? 49.7일 버그 아시죠?</summary>

1 ms 의 uint32 틱은 2^32 ms ≈ 49.7일에 넘어간다. `now − last` 를 부호 없는 뺄셈으로 하면 넘어가도 실제 경과(< 2^32)가 정확히 나온다 — C 에서 부호 없는 연산은 모듈러로 정의돼 있다. 틀리는 코드는 `if (now > deadline)` 같은 **절대값 비교**다. 계약 "호출 간격 < 2^31" 을 두면 뺄셈 결과의 최상위 비트를 "과거" 로 쓸 수 있다(asm 에서 `bmi` 로 바뀌는 것을 봤다). 2^31 틱(≈ 24.9일) 넘게 아무도 안 부르면 그 뒤로도 최대 2^31 틱 동안 과거로 보여 refill 이 멈춘다 → 주기적인 하우스키핑 호출. uint16 틱이면 정수 승격 때문에 캐스트가 필요하다(실수 4). 꼬리: "64비트 틱을 쓰면?" → wrap 은 사라지지만 M4 에서 64비트 읽기가 두 번의 로드라 ISR 과 찢어질 수 있다(C-M14: 상위-하위-상위 재읽기 루프).

</details>

<details><summary>Q4. b 와 r 을 어떻게 정하나요? 인터럽트 폭주 가드라면?</summary>

r 은 **지속 가능한 처리 능력**에서: 핸들러 한 번이 20 µs 이고 이 IRQ 에 CPU 2% 를 줄 수 있다면 r = 0.02 / 20 µs = 1000/s. b 는 **정상 동작의 최대 뭉치**와 **흡수할 수 있는 버퍼**에서: 정상 센서가 한 번에 엣지 4개를 내면 b ≥ 4, UART RX 링이 64 바이트면 바이트 버킷의 b ≤ 64 (그 이상 허용하면 어차피 넘친다). 마지막으로 창 계약 검사: 길이-T 창 최대 = b + rT 가 시스템이 견딜 수 있는가. resume 문턱(히스테리시스)은 마스크/해제 반복을 막을 만큼 — 이 레슨은 b/2. 꼬리: "정상 엣지를 놓치면 안 되는 센서라면?" → 폭주 판정을 레이트가 아니라 신호 품질로(디바운스 필터·하드웨어 글리치 필터), 그리고 리미터는 최후 방어선.

</details>

<details><summary>Q5. 슬라이딩 윈도우 카운터는 얼마나 틀릴 수 있나요?</summary>

적대적 최악은 길이-W 창에 N + ⌈N(W−2)/W⌉ — N < W/2 면 정확히 2N 으로, 고정 창과 같다. 증명은 "창 k 의 통과 c ≤ N − ⌊p(W−el)/W⌋" 에 이전 창 꼬리 p 를 더하는 것이고, 이전 창 끝 틱에 N, 다음 창 el = W−2 에 홍수를 보내면 정확히 닿는다(5가지 (N, W) 실측 = 상한). 다만 고정 창은 2N 을 2틱 안에 허용하고 카운터는 W−1 틱에 걸쳐 허용한다 — 순간 버스트가 문제면 카운터가 훨씬 낫다. 실제 트래픽에서는 Cloudflare 가 0.003% 오판을 보고했다(평균, 최악 아님). 반대 방향 오차도 있다: 이전 창 앞쪽에 몰린 트래픽을 과대평가해 bursty 에서 200 대신 150 만 통과. 꼬리: "정확하면서 싸게?" → 연습 4 의 K-슬롯 보수적 카운터: 4(K+1) 바이트로 과허용 0, 처리량 손실 1/(K+1).

</details>

<details><summary>Q6. ISR 과 태스크가 같은 리미터를 쓰면 무엇을 보호해야 하나요?</summary>

상태가 level 과 last 두 워드이고 refill → 비교 → 차감이 읽기-수정-쓰기다. 태스크가 level 을 읽은 뒤 ISR 이 끼어들어 토큰을 쓰면, 태스크는 옛 값으로 차감해 ISR 이 쓴 토큰을 되살린다(lost update) → 계약 위반. 해결: (1) 한 문맥만 쓰게 설계(ISR 은 큐에 넣기만, 판정은 태스크) (2) `tb_try` 전체를 PRIMASK 또는 BASEPRI 임계 구역에 — 루프·나눗셈 없는 30여 명령이라 지연 상한이 작다 (3) 한 워드 상태(GCRA 의 TAT)로 바꿔 LDREX/STREX CAS. 같은 우선순위의 ISR 끼리는 서로 선점하지 않으니 잠금이 필요 없다. 늦게 읽은 now 는 Part A 의 과거-틱 규칙이 막는다. 꼬리: "멀티코어?" → 코어별 버킷에 r/k 씩(버스트를 공유 못함) 또는 HW 세마포어·스핀락.

</details>

<details><summary>Q7. 분산 서버라면 무엇이 달라지나요?</summary>

알고리즘은 같고 **상태의 위치**가 달라진다 — 이건 SYSD-B07 이 다룬다. 핵심만: 상태를 공유 저장소(예: Redis)에 두고 읽기-수정-쓰기를 원자적으로(서버 쪽 스크립트나 CAS), 시간은 각 서버 시계가 아니라 저장소 쪽 시계를 써야 시계 오차가 버킷을 부풀리지 않는다(과거-틱 규칙과 같은 문제). 고정 창은 `INCR` + 만료 하나로 되어 흔히 쓰이고, 슬라이딩 카운터는 키 두 개로 된다. 노드별 로컬 버킷 + 주기적 동기화는 정확도를 지연과 바꾼다. 꼬리: "거부를 클라이언트에 어떻게 알리나?" → HTTP 429 Too Many Requests (RFC 6585) + 언제 다시 오라는 `Retry-After` — 토큰 버킷이면 연습 3 의 대기 시간 공식으로 계산할 수 있다.

</details>

<details><summary>Q8. 이걸 어떻게 테스트하겠어요? "통과율이 대충 맞다" 말고요.</summary>

네 겹: (1) **성질 검사**: 통과 로그의 모든 창이 b + rT 이하 (O(n²) 전수) — 안전성. (2) **완전성**: 같은 판정을 통과 로그만 보고 하는 브루트포스(창 검사기)와 결정 하나하나 대조, 그리고 꽉 찬 창이 존재하는지 — 아무것도 안 통과시키는 구현을 잡는다. (3) **장기 정확성**: 10^9 틱 포화 = b + ⌊rT⌋ — 드리프트를 잡는다. (4) **경계**: 시작을 0xFFFFF000 에 두어 모든 테스트가 wrap 을 지나게, 과거 틱, dt = full_ticks−1 / full_ticks, cost > b. 그리고 이 테스트가 정말 버그를 잡는지 확인하려고 버그 6개를 심어 봤다(7. 실수) — 특히 "가득 경로에서 last 미갱신" 은 손 계산은 다 통과하고 패턴 트래픽에서만 잡혔다. 트래픽 생성기는 steady/bursty/창 경계 공격/idle→burst/random 다섯 가지 — 경계 공격이 없으면 고정 창의 2N 이 안 보인다.

</details>

## 9. 추가 연습

### 연습 1 — GCRA: 상태 한 워드로 같은 결정

TAT(theoretical arrival time) 하나만 들고 다니는 GCRA 를 정수로 구현하라. 시간 단위를 1/num 틱(1 틱 = num, 토큰 1개 = den)으로 두면 통과 조건과 갱신은 어떻게 되나? 토큰 버킷과 같은 무작위 트래픽(비용 1..b)에서 결정이 한 건도 다르지 않음을 보여라.

<details><summary>답 보기</summary>

x = max(TAT − s·num, 0) 을 "밀린 양" 이라 하면 x = cap − level (토큰 버킷의 빈 부분)이다. 통과 ⇔ x + cost·den ≤ b·den ⇔ level ≥ cost·den. 통과 시 TAT ← max(TAT, s·num) + cost·den ⇔ x ← x + cost·den ⇔ level 차감, 그리고 max(·, 0) 이 cap 과 대응. 시간이 흐르면 s·num 이 커져 x 가 틱당 num 씩 줄어든다 ⇔ level 이 num 씩 는다. 실제 출력:

```text
ex1 GCRA vs token bucket: 199939 decisions (45070 accepted), mismatches=0
```

장점: 상태 = TAT 한 워드 → 원자적 CAS 로 잠금 없이(Q6). 연습 코드는 단순하게 64비트 TAT 를 썼다. 32비트로 줄이면 TAT 와 s·num 을 모듈러로 비교해야 하고, 스케일된 시간은 num 배 빨리 wrap 하므로 계약 간격이 2^31/num 틱으로 줄어든다. GCRA 의 표준 정의는 ITU-T I.371 (ATM 트래픽 관리)에 있다고 알고 있다 — 확인 권장.

</details>

### 연습 2 — Linux 식 "첫 호출 고정 창" 의 최악

`___ratelimit()` 모델: 처음 호출 때 begin = now, 남은 수 = b. 호출 시 now > begin + I 면 begin = now, 남은 수 = b. 남은 수 > 0 이면 통과. 틱마다 0..b 번 호출하는 모든 패턴(길이 I+4 틱)을 전수 탐색해 2틱 창, (I+1)틱 창, (I+2)틱 창의 최대 통과를 구하고 공식으로 설명하라.

<details><summary>답 보기</summary>

한 창은 [begin, begin + I] (I+1 틱)이고 다음 창은 그 뒤 첫 호출에서 시작하므로 창들은 서로소다. 창 A 의 첫 통과는 begin 에 있으니, A 의 b 개가 모두 들어가는 구간은 begin 을 포함해야 하고 다음 창 B 는 begin + I + 1 이후에 시작 → 길이 I+1 이하 구간에는 A 에서 최대 b−1 (begin 에 하나를 남겨 두고 begin+I 에 b−1) + B 에서 b = **2b−1**, 길이 I+2 면 **2b**. 실제 출력:

```text
ex2 linux-style b=2 I=1 (H=5, 3^5 patterns): max in 2 ticks=3 (2b-1=3)  in I+1=3  in I+2=4 (2b=4)
ex2 linux-style b=3 I=3 (H=7, 4^7 patterns): max in 2 ticks=5 (2b-1=5)  in I+1=5  in I+2=6 (2b=6)
ex2 linux-style b=4 I=2 (H=6, 5^6 patterns): max in 2 ticks=7 (2b-1=7)  in I+1=7  in I+2=8 (2b=8)
```

(7가지 (b, I) 모두 공식과 일치.) 로그 제한에서는 이 정도 경계 오차는 문제가 아니다 — 목적은 "UART 를 막지 않기 + 억제 수 남기기" 이고, 상태가 작고 설명하기 쉽다는 것이 더 중요하다.

</details>

### 연습 3 — 셰이퍼: 버리지 말고 늦추기

패킷(도착 시각 a_i, 크기 c_i ≤ b)을 FIFO 로 받아 토큰 버킷에 맞춰 **늦춰서** 내보낸다. `tb_wait(tb, now, cost)` = 토큰이 cost 개 모일 때까지의 틱 수를 닫힌 식으로 구하고, 출발 시각 d_i = max(a_i, d_{i−1}) + wait 를 틱 단위 시뮬레이션과 대조하라. 출력이 (b, r) 계약을 지키는지도 확인하라.

<details><summary>답 보기</summary>

부족분 need − level 을 틱당 num 씩 채우므로 wait = ⌈(need − level)/num⌉ (나눗셈 1번 — 셰이퍼는 패킷당 한 번이라 괜찮다). FIFO 라 앞 패킷이 떠나기 전엔 못 떠나고, 앞 패킷이 토큰을 써야 다음 대기가 정해지므로 순서대로 계산한다. 실제 출력:

```text
ex3 shaper: 20 runs x 3000 packets, closed-form vs tick sim mismatches=0, output window violations=0, max delay=13419 ticks
```

최대 지연 13419 틱 — 도착이 계속 속도를 넘으면 큐가 끝없이 자란다. 실제 셰이퍼는 큐 길이 상한(넘으면 tail drop 이나 AQM)이 필요하다 (NET-M12 의 bufferbloat, CoDel). 펌웨어에서는 "로그를 버리지 말고 늦춰 보내기", "모터 명령 속도 제한", DMA 요청 페이싱(SOCX-M03 의 대역폭 레귤레이터)이 같은 모양이다.

</details>

### 연습 4 — K-슬롯 보수적 카운터: 메모리 K+1 칸, 과허용 0

창 W 를 K 슬롯(크기 S = W/K)으로 나누고, **지금 슬롯 + 직전 K 슬롯**의 합 < N 이면 통과시켜라. 어느 길이-W 창도 N 을 넘지 않음을 증명하고, 매 틱 요청(포화)에서 K 별 처리량을 정확한 로그와 비교하라.

<details><summary>답 보기</summary>

세는 구간 = K·S + (지금 슬롯 경과 + 1) ≥ W + 1 틱 ⊇ [now−W+1, now]. N+1 개가 든 길이-W 창이 있다면 그 마지막 통과 시점에 나머지 N 개는 이미 세는 구간 안이었으니 거부됐어야 한다 — 모순. 대가: 세는 구간이 W 보다 최대 S 길어서 처리량이 약 N/(W + S). 실제 출력 (N=10, W=100, 10000 틱):

```text
ex4 K=1  slots=2  (  8 B): saturating 1/tick for 10000 ticks -> accepted 500 (exact log 1000, 50.0%), max/W=10, theory N*H/(W+S)=500
ex4 K=5  slots=6  ( 24 B): saturating 1/tick for 10000 ticks -> accepted 840 (exact log 1000, 84.0%), max/W=10, theory N*H/(W+S)=833
ex4 K=10 slots=11 ( 44 B): saturating 1/tick for 10000 ticks -> accepted 910 (exact log 1000, 91.0%), max/W=10, theory N*H/(W+S)=909
ex4 K=50 slots=51 (204 B): saturating 1/tick for 10000 ticks -> accepted 984 (exact log 1000, 98.4%), max/W=10, theory N*H/(W+S)=980
```

무작위 트래픽 100회에서도 길이-W 창 초과 0. 메모리 44 B(K=10)로 정확한 로그(N=10 이면 40 B — 이 경우는 로그가 더 싸다!)의 91%. **N 이 클 때** 의미가 있다: N=1000 이면 로그 4 KB vs K=10 슬롯 44 B. 슬라이딩 카운터(12 B)는 "근사 + 양쪽 오차", 이건 "안전 + 처리량 손실" — 하드 상한이 필요한 펌웨어에선 이쪽이 맞다.

</details>

LeetCode 에서 가까운 문제: **LeetCode 359 Logger Rate Limiter** (메시지별 10초에 한 번 — 이 레슨 C2 의 b=1 판), **LeetCode 362 Design Hit Counter** (최근 300초 히트 수 — 슬라이딩 로그/슬롯 카운터). 둘 다 프리미엄 문제.

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 12/12 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san && ./practice_san   # 12/12, 보고 없음
```


## 10. 핵심 정리

- 먼저 계약을 묻는다: (b, r) 도착 곡선이면 토큰 버킷, "임의의 W 창에 N" 이면 슬라이딩 로그(정확) 또는 K-슬롯 보수적 카운터. b=N, r=N/W 토큰 버킷은 W 창에 b + ⌊r(W−1)⌋ ≈ 2N 을 허용한다.
- 정수 토큰 버킷: level = 토큰 × den, 틱마다 +num, 통과 = level ≥ cost·den. 반올림이 없으니 드리프트 0 (10^9 틱 = b + ⌊rT⌋ 정확). Q16/float/`dt*num/den` 은 실측으로 틀린다.
- `dt = now − last` (unsigned) 는 wrap 안전. dt > 2^31−1 은 과거 → 무시. dt ≥ ⌈cap/num⌉ 이면 가득 — 이 경로에서도 `last = now`. burst·den ≤ 2^31−1 이면 넘침 없음. 핫 패스 나눗셈 0 (M4 asm 확인).
- 창 정리: 게으른 refill = 매 틱 refill (cap 결합 보조정리) → 모든 창 ≤ b + rT. 역으로 토큰 버킷은 계약을 지키는 요청을 하나도 거부하지 않는 탐욕적 판정기 — 브루트포스와 결정 일치로 검사.
- 고정 창: 2틱에 2N. 슬라이딩 카운터: 최악 N + ⌈N(W−2)/W⌉ (≈ 2N, 단 W−1 틱에 걸쳐), 과소허용도 함. 슬라이딩 로그: O(1) — N 번째 최근 하나만 보면 됨, 메모리 4N.
- 리키 버킷 = 큐(평탄한 출력, 지연) / 계량기(= 토큰 버킷 거울상, GCRA). Policer 는 버리고 shaper 는 늦춘다.
- ISR 폭주: 버리기만 하지 말고 IRQ 마스크 + 히스테리시스 재개 (ISR 진입 2700 → 245). 로그 폭주: ID 별 버킷 + "(+N suppressed)" + 보존 법칙.
- 문맥: 한 버킷은 한 문맥(또는 같은 우선순위)만, 아니면 짧은 임계 구역, 한 워드 GCRA 면 CAS. 테스트는 안전성(창 전수) + 완전성(브루트포스 결정) + 장기(포화) + wrap 경계.

## 참고자료

- Linux kernel, `include/linux/ratelimit_types.h` — `DEFAULT_RATELIMIT_INTERVAL (5 * HZ)`, `DEFAULT_RATELIMIT_BURST 10`: <https://github.com/torvalds/linux/blob/master/include/linux/ratelimit_types.h>
- Linux kernel, `lib/ratelimit.c` — `___ratelimit()`: 첫 호출 시각에 고정되는 창, 창이 지나면 다음 호출에서 리셋, 억제 수 보고 (2026-11-11 master 확인): <https://github.com/torvalds/linux/blob/master/lib/ratelimit.c>
- Cloudflare blog, "How we built rate limiting capable of scaling to millions of domains" (2017-06-07) — 슬라이딩 카운터 식 `rate = 42 * ((60-15)/60) + 18`, 4억 요청·27만 출처에서 0.003% 오판: <https://blog.cloudflare.com/counting-things-a-lot-of-different-things/>
- J.-Y. Le Boudec, P. Thiran, *Network Calculus* (Springer LNCS 2050, 2001) — 도착 곡선 b + rt, min-plus 대수, 탐욕적 셰이퍼. 무료 PDF 가 저자 페이지에 있다 (판·쪽은 확인 권장).
- RFC 2697 "A Single Rate Three Color Marker", RFC 2698 "A Two Rate Three Color Marker" — 토큰 버킷 두 개로 만드는 policer (NETHW-M10).
- RFC 6585 — HTTP 429 Too Many Requests.
- ITU-T Recommendation I.371 — ATM 트래픽 제어, GCRA 정의 (기억에 의존 — 확인 권장).
- J. C. Mogul, K. K. Ramakrishnan, "Eliminating receive livelock in an interrupt-driven kernel", *ACM TOCS* 15(3), 1997 — 인터럽트 폭주에서 처리량이 0 으로 무너지는 현상과 폴링 전환 (서지 기억에 의존 — 확인 권장).
- ISO/IEC 9899:2011 (C11) 6.2.5p9 (부호 없는 연산은 모듈러), 6.3.1.1 (정수 승격).
- LeetCode 359 Logger Rate Limiter, LeetCode 362 Design Hit Counter (프리미엄).
- AlgoMonster 노트 M11 (Don 정리본) — 매핑표에서 A11 은 △(직접 대응 레슨 없음); 창을 움직이는 불변식은 AlgoMonster 노트 M03 §13 Sliding Window Family Map (Don 정리본) 이 가깝다.
- 관련 레슨: ALGO-B10 (슬라이딩 윈도우 이동 평균), ALGO-M15 (상수 나눗셈, M0 의 `__aeabi_uidiv`), C-M14 (ISR 공유 변수·찢어진 읽기), DS-A01 (C11 atomics), SYSD-B07 (MCU vs 분산 rate limiter 설계 — 이 레슨의 알고리즘을 바탕으로), NETHW-M10 (egress shaper·policer), NET-M12 (`tbf`·CoDel), SOCX-M03 (인터커넥트 대역폭 레귤레이터)
