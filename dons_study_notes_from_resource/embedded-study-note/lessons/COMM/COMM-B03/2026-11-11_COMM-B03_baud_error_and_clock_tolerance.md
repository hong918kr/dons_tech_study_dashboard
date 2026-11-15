---
id: COMM-B03
time: 60분
updated: 2026-11-11
---
# COMM-B03 Baud rate 오차 계산과 허용 한계

## 학습 목표

- 클럭 fclk·목표 baud·오버샘플링(OVS 16/8)·분수 비트로 **분주값 N, 실제 baud, % 오차**를 정수 연산으로 계산하고, 범위(분주 < 1, 레지스터 폭 초과)를 오류 코드로 돌려준다. STM32 식 BRR(가수 + 분수 4비트)과 분수 올림이 가수로 넘어가는 경우까지 다룬다.
- 송신 비트 폭과 수신 샘플 주기가 다른 링크를 시간 단위로 시뮬레이션해, 16x 수신기가 **몇 % 편차에서 처음 틀리는지** 실측하고, 이론식 `(1 − m/16)/(j + m/16)` 과 0.01% 안에서 맞춘다.
- 허용 한계가 **비대칭**(빠른 TX +3.90% / 느린 TX −5.26%, 8N1)인 이유와, back-to-back 스트림에서 한 번 놓치면 계속 깨지는 이유를 설명한다.
- 양쪽 끝의 분주 오차 + 발진기 ppm 으로 링크 예산을 세워 OK / MARGINAL / FAIL 을 판정하고, 최악 코너를 시뮬레이션으로 확인한다 (AVR 16 MHz 의 115200, 내부 RC 발진기).

## 1. 문제

"16 MHz 에서 115200 이 왜 안 되죠?", "HSI 로 UART 써도 되나요?" — 보드 bring-up 에서 UART 가 **가끔** 깨질 때 가장 먼저 의심하는 것이 baud 오차이고, 면접에서는 "오차 몇 % 까지 되나, 그 숫자는 어디서 나오나"로 깊어진다.

UART 는 클럭 선이 없다. 수신기(RX)는 start 비트의 하강 에지(falling edge)에서 한 번 시계를 맞추고, 그 뒤로는 **자기 클럭만 믿고** 비트 가운데를 샘플링한다(COMM-B02). 송신기(TX)와 수신기의 baud 가 조금 다르면 샘플 위치가 비트마다 조금씩 밀리고, 프레임 끝(stop 비트)에서 가장 많이 밀린다. 밀린 양이 "샘플에서 비트 경계까지 남은 거리"를 넘으면 그 비트를 이웃 비트로 읽는다.

오차의 원천은 두 가지다.

1. **분주 반올림 오차** — baud 클럭은 fclk 를 정수(또는 1/16 단위 분수)로 나눠 만든다. 16 MHz / (16 × 115200) = 8.68 → 9 로 나누면 실제 111111 baud, **−3.55%**.
2. **발진기 오차** — 크리스털 ±20~100 ppm(0.002~0.01%)은 무시할 수준이지만, 내부 RC 발진기(STM32 HSI 등)는 ±1% 이상일 수 있다 (대표값 — 데이터시트 확인).

그림은 'A'(0x41, 8N1)를 **TX 가 4.5% 빠른** 링크로 보낼 때, RX 의 가운데 샘플(9번)이 비트마다 어디에 떨어지는지 이 레슨의 모델식으로 계산한 위치다 (최악 위상 φ = 1).

```svg
<svg viewBox="0 0 760 345" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">'A' = 0x41, 8N1, LSB first, back-to-back — TX 가 4.5% 빠를 때 RX 샘플 위치 (최악 위상)</text>
  <g stroke="currentColor" stroke-dasharray="2 3" stroke-width="0.8">
    <line x1="50" y1="40" x2="50" y2="190"/><line x1="110" y1="40" x2="110" y2="190"/><line x1="170" y1="40" x2="170" y2="190"/>
    <line x1="230" y1="40" x2="230" y2="190"/><line x1="290" y1="40" x2="290" y2="190"/><line x1="350" y1="40" x2="350" y2="190"/>
    <line x1="410" y1="40" x2="410" y2="190"/><line x1="470" y1="40" x2="470" y2="190"/><line x1="530" y1="40" x2="530" y2="190"/>
    <line x1="590" y1="40" x2="590" y2="190"/><line x1="650" y1="40" x2="650" y2="190"/><line x1="710" y1="40" x2="710" y2="190"/>
  </g>
  <text x="62" y="50" fill="currentColor" font-size="12">start</text><text x="128" y="50" fill="currentColor" font-size="12">b0</text>
  <text x="188" y="50" fill="currentColor" font-size="12">b1</text><text x="248" y="50" fill="currentColor" font-size="12">b2</text>
  <text x="308" y="50" fill="currentColor" font-size="12">b3</text><text x="368" y="50" fill="currentColor" font-size="12">b4</text>
  <text x="428" y="50" fill="currentColor" font-size="12">b5</text><text x="488" y="50" fill="currentColor" font-size="12">b6</text>
  <text x="548" y="50" fill="currentColor" font-size="12">b7</text><text x="604" y="50" fill="currentColor" font-size="12">stop</text>
  <text x="656" y="50" fill="currentColor" font-size="12">다음 start</text>
  <path d="M20,70 H50 V110 H110 V70 H170 V110 H470 V70 H530 V110 H590 V70 H650 V110 H710" fill="none" stroke="currentColor" stroke-width="2"/>
  <g font-family="monospace" font-size="13">
    <text x="76" y="128" fill="currentColor">0</text><text x="136" y="128" fill="currentColor">1</text><text x="196" y="128" fill="currentColor">0</text>
    <text x="256" y="128" fill="currentColor">0</text><text x="316" y="128" fill="currentColor">0</text><text x="376" y="128" fill="currentColor">0</text>
    <text x="436" y="128" fill="currentColor">0</text><text x="496" y="128" fill="currentColor">1</text><text x="556" y="128" fill="currentColor">0</text>
    <text x="616" y="128" fill="currentColor">1</text><text x="676" y="128" fill="currentColor">0</text>
  </g>
  <text x="10" y="150" fill="currentColor" font-size="12">e=0</text>
  <g stroke="currentColor">
    <rect x="80" y="141" width="7" height="10" class="f-mut"/><rect x="140" y="141" width="7" height="10" class="f-mut"/>
    <rect x="200" y="141" width="7" height="10" class="f-mut"/><rect x="260" y="141" width="7" height="10" class="f-mut"/>
    <rect x="320" y="141" width="7" height="10" class="f-mut"/><rect x="380" y="141" width="7" height="10" class="f-mut"/>
    <rect x="440" y="141" width="7" height="10" class="f-mut"/><rect x="500" y="141" width="7" height="10" class="f-mut"/>
    <rect x="560" y="141" width="7" height="10" class="f-mut"/><rect x="620" y="141" width="7" height="10" class="f-mut"/>
  </g>
  <text x="10" y="176" fill="currentColor" font-size="12">+4.5%</text>
  <g stroke="currentColor">
    <rect x="82" y="167" width="7" height="10" class="f-acc"/><rect x="144" y="167" width="7" height="10" class="f-acc"/>
    <rect x="207" y="167" width="7" height="10" class="f-acc"/><rect x="270" y="167" width="7" height="10" class="f-acc"/>
    <rect x="333" y="167" width="7" height="10" class="f-acc"/><rect x="395" y="167" width="7" height="10" class="f-acc"/>
    <rect x="458" y="167" width="7" height="10" class="f-acc"/><rect x="521" y="167" width="7" height="10" class="f-acc"/>
    <rect x="583" y="167" width="7" height="10" class="f-acc"/><rect x="646" y="167" width="7" height="10" class="f-warn"/>
  </g>
  <text x="100" y="204" fill="currentColor" font-size="12">■ = 9번 샘플. 비트마다 1.045배씩 늦어져 stop 에서 0.99 비트 지점까지 밀린다</text>
  <line x1="10" y1="212" x2="750" y2="212" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="10" y="232" fill="currentColor" font-weight="bold">확대: stop 끝 (TX 비트 9.0 ~ 10.25)</text>
  <path d="M100,255 H580 V295 H700" fill="none" stroke="currentColor" stroke-width="2"/>
  <line x1="580" y1="240" x2="580" y2="310" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="300" y="250" fill="currentColor" font-size="12">stop = 1</text><text x="600" y="312" fill="currentColor" font-size="12">다음 start = 0</text>
  <g stroke="currentColor">
    <line x1="545" y1="245" x2="545" y2="305"/><line x1="576" y1="245" x2="576" y2="305"/><line x1="608" y1="245" x2="608" y2="305"/>
  </g>
  <text x="538" y="322" fill="currentColor" font-size="12">8</text><text x="570" y="322" fill="currentColor" font-size="12">9</text>
  <text x="602" y="322" fill="currentColor" font-size="12" class="f-warn">10</text>
  <text x="100" y="280" fill="currentColor" font-size="12">샘플 8, 9 = 1, 샘플 10 = 0 → 다수결 1 (stop OK) + noise 플래그.</text>
  <text x="100" y="298" fill="currentColor" font-size="12">마지막 샘플이 0 이라 IDLE 에서 1→0 에지를 못 본다 → 다음 프레임 유실</text>
  <text x="100" y="338" fill="currentColor" font-size="12">위치(TX 비트) = 1.045 × (9 + (m−1+φ)/16), φ = 1: 샘플 8 = 9.928, 9 = 9.993, 10 = 10.058</text>
</svg>
```

### Part A — baud 계산기

- `baud_calc(fclk, baud, ovs, frac_bits, div_max, &r)` — N = round(fclk·2^F / (ovs·baud)) 를 **uint64 정수**로 반올림(half-up), 실제 baud = fclk·2^F/(ovs·N), 오차 % = (실제 − 목표)/목표 × 100 (+ 는 빠름). 인자 오류 `BAUD_EARG`, N < 2^F(분주 < 1) `BAUD_ETOO_FAST`, N > div_max `BAUD_ETOO_SLOW`. 실패 시 결과 구조체는 건드리지 않는다.
- `stm32_brr(n, over8)` — OVER8 = 0: BRR = n (가수 << 4 | 분수 4비트). OVER8 = 1: 가수 << 4 | 분수 3비트, BRR[3] = 0 (대표값, RM 확인 권장).
- 흔한 클럭(1.8432 / 8 / 14.7456 / 16 / 48 / 72 / 80 MHz) × baud(9600 … 921600) 오차 표를 출력한다.

### Part B — 클럭 편차 실험

- `tx_level(bits, n, t0, p, t)` — 시각 t 의 송신선 레벨 (프레임 밖은 idle 1).
- `sim_link(fmt, ovs, p_tx, s_rx, nframes, gap_bits, seed, &stats)` — TX 비트 폭 p_tx, RX 샘플 주기 s_rx 로 프레임 nframes 개를 보내고(랜덤 데이터, 랜덤 위상) 16x 수신기(COMM-B02 모델 축약판, 주어짐)로 받아 ok / fe / pe / nf / err 를 센다. `gap_bits = 0` 은 back-to-back, `> 0` 은 프레임 사이 idle.
- `tol_theory(j, m, ovs, late)` — "비트 j 의 샘플 m 이 그 비트 안에 있어야 한다"의 최악 위상 한계 (%).
- 테스트: 8N1·8E1 × back-to-back·isolated 에서 이론 한계 **0.1% 안쪽은 오류 0**, **0.3% 바깥은 오류 > 0**. 그리고 noise 플래그만 서고 데이터는 맞는 구간.

### Part C — 링크 예산

- `link_dir_check(tx, rx, baud, frame_bits, &d)` — 양쪽의 실제 baud(Part A) × (1 ± ppm) 으로 편차 범위 [e_lo, e_hi] 를 구하고, 수신측 한계(Part B 이론식, back-to-back 최악)와 비교해 OK / MARGINAL(한계의 절반 초과) / FAIL / BADCFG.
- 시나리오: STM32 16 MHz 크리스털, HSI ±1%, AVR 16 MHz(x16, U2X), 내부 RC ±4%, 8 MHz 에서 921600. 판정이 OK·MARGINAL 이면 최악 네 코너 시뮬레이션이 전부 오류 0, FAIL 이면 어느 코너에선 오류가 나는지 확인한다.

## 2. Problem (English)

> **Part A.** Implement
> `int baud_calc(uint32_t fclk, uint32_t baud, unsigned ovs, unsigned frac_bits, uint32_t div_max, baud_res *r);`
> The UART divides `fclk` by `ovs` (8 or 16) and by a divider `N/2^F` (F = `frac_bits`, 0..4). Choose `N = round(fclk·2^F / (ovs·baud))` using integer math only, report `div = N`, `actual = fclk·2^F/(ovs·N)` and `err_pct = (actual − baud)/baud·100`. Return `BAUD_EARG` for bad arguments, `BAUD_ETOO_FAST` if `N < 2^F`, `BAUD_ETOO_SLOW` if `N > div_max`; leave `*r` untouched on failure.
> Examples: `(16 MHz, 115200, 16, 0)` → N = 9, 111111.1 baud, −3.549 %. `(16 MHz, 115200, 8, 0)` → N = 17, +2.124 %. `(16 MHz, 115200, 16, 4)` → N = 139 (BRR 0x08B), −0.080 %. `(72 MHz, 115200, 16, 4)` → BRR 0x271, 0 %. `(8 MHz, 921600, 16, 4)` → `BAUD_ETOO_FAST`.
>
> **Part B.** Given a 16x-oversampling receiver model (majority of samples 8, 9, 10; sample 1 = first low sample after a 1→0 edge), simulate a link where the transmitter's bit time `p_tx` and the receiver's sample period `s_rx` differ. Mismatch `e = baud_tx/baud_rx − 1 = 16·s_rx/p_tx − 1`. Send N random frames with random phase, back-to-back or with idle gaps, and count frames received wrong. Find the mismatch where 8N1 and 8E1 start failing (both signs), and derive the worst-case bound analytically. Your simulator must agree with the bound to within 0.1 %.
>
> **Part C.** Each end has `fclk`, oscillator tolerance (ppm), OVS and divider resolution. For a given baud and frame length, compute the worst-case mismatch range in each direction, compare with the receiver's bound, and classify the link as OK / MARGINAL (beyond half the bound) / FAIL / BADCFG. Verify by simulating the corners.
>
> Constraints: no malloc, deterministic (fixed-seed xorshift32), O(1) work per receiver sample.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여섯 군데: Part A `baud_calc`, `stm32_brr` / Part B `tx_level`, `sim_link`, `tol_theory` / Part C `link_dir_check`. 프레임 인코더(COMM-B01 축약판), 수신기 `urx_step`(COMM-B02 축약판), 양방향 래퍼 `link_check`, 하네스는 주어진다. 스텁은 오류 코드 / idle / 0 을 돌려주므로 멈추지 않는다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:199  baud_calc(1843200, 9600, 16, 0, 0xFFFF, &r) == BAUD_OK && r.div == 12 && near(r.err_pct, 0, 1e-12)
…
baud_calc random: 0 valid cases, bad=0
…
FAIL starter.c:266  tx_level(bits, nb, 100, 10, 100) == 0
FAIL starter.c:274  st.rx_frames == 3000 && st.ok == 3000 && st.err == 0 && st.nf == 0
…
theory 8N1: b2b late +0.000%  early +0.000% (NF from +0.000%)  isolated late +0.000%
8N1 back-to-back   e=-0.30%: err 3000 | e=+0.10%: err 3000 | e=-0.10%: err 3000 | e=+0.30%: err 3000
…
10/54 passed
```

(exit 1. 통과한 10개는 주어진 코드(프레임 인코더)이거나 스텁 값이 우연히 맞는 경우 — 인자 오류 → `BAUD_EARG`, 프레임 밖 idle = 1, "한계 바깥이면 err > 0" 4개, 기대값이 OK 인 링크 판정 2개. 검사 개수가 정답(53)과 하나 다른 것은 BADCFG 판정이 틀리면 코너 검사를 한 번 더 하기 때문.)

## 4. 힌트

<details><summary>힌트 1 — 반올림을 정수로</summary>

round(a/b) = ⌊(2a + b) / (2b)⌋ (a, b ≥ 0, 반은 올림). a = fclk·2^F 는 80 MHz × 16 = 1.28e9 로 uint32 에 겨우 들어가지만, 400 MHz × 16 은 넘친다 — 처음부터 uint64. N = 0 이 나올 수 있으니(1 kHz 클럭으로 115200) 나누기 전에 걸러야 한다.

</details>

<details><summary>힌트 2 — 분수 BRR 은 "N 하나"로 생각하라</summary>

USARTDIV = 39.0625 를 "가수 39 + 분수 1/16"으로 따로 반올림하지 말고, N = round(USARTDIV × 16) = 625 = 0x271 하나로 구한다. 그러면 분수가 16 으로 반올림되는 경우(50.99 → 0x330)의 자리올림이 저절로 처리된다. OVER8 = 1 이면 N = USARTDIV × 8 이고 BRR 에 넣을 때만 `(N >> 3) << 4 | (N & 7)`.

</details>

<details><summary>힌트 3 — 시간축은 정수, 샘플 단위로 걷는다</summary>

RX 샘플 주기를 1000 단위로 두면 RX 비트 = 16000. 편차 e 는 TX 비트 폭 p_tx = 16000/(1+e) 로 표현된다 (1 단위 = 0.00625%). 루프는 t 를 s_rx 씩 올리며 `urx_step(rx, tx_level(t))` — 프레임 시간표는 "현재 프레임 끝을 지나면 다음 프레임을 올린다" 하나로 충분하다 (프레임은 샘플 160개 이상이라 한 샘플에 두 프레임을 건너뛰지 않는다).

</details>

<details><summary>힌트 4 — 샘플 m 은 에지에서 얼마나 떨어져 있나</summary>

에지는 두 샘플 사이 어딘가에서 일어나고, RX 가 0 을 처음 보는 샘플(1번)은 에지보다 φ ∈ (0, 1] 샘플 늦다. 그러면 m 번 샘플은 에지에서 (m − 1 + φ)/16 RX 비트. TX 시간으로는 (1 + e) 배. "비트 j 안에 있다" = j ≤ (1+e)(j + (m−1+φ)/16) < j + 1. φ 의 최악값을 각 부등식에 넣어라.

</details>

<details><summary>힌트 5 — 어느 샘플이 "마지막으로 맞아야" 하나</summary>

다수결 2/3 이 맞으려면 가운데 샘플(9)이 비트 안에 있으면 된다. 그런데 back-to-back 에서 stop 의 **10번** 샘플이 다음 start(0)에 들어가면? 다수결은 1 이지만 수신기가 IDLE 로 돌아갈 때 마지막으로 본 레벨이 0 이다 — 다음 하강 에지를 볼 수 있나? isolated 프레임에서는 stop 다음이 idle(1)이라 stop 샘플이 밀려도 무해하다 — 그럼 마지막으로 위험한 비트는?

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 범위 확인 | 클럭 소스(크리스털? RC?), OVS(16/8), 분수 분주 지원 여부, 프레임 형식(8N1? 패리티?), 연속 스트림인지, 상대편(PC/브리지/다른 MCU) |
| 2. 계산 | N = round(fclk/(OVS·baud)), 실제 baud, % 오차. 정수 연산, 반올림 vs 버림 |
| 3. 한계 | "stop 비트 가운데까지 9.5 비트 동안 반 비트 이상 밀리면 깨진다" → 0.5/9.5 ≈ 5.3%, 여기서 샘플 창과 에지 검출 지연을 빼면 ≈ 3.9% |
| 4. 예산 | 송수신 상대 편차 = 양쪽 분주 오차 + 발진기 오차. 설계 여유로 총 ±2% 정도 |
| 5. 검증 | LA 로 비트 폭 측정(HWLAB-M05), 0x55/0x00 패턴 에코 테스트, 연속 스트림 스트레스 |

### Part A — 분주, 반올림, 오차

**식.** UART 는 fclk 를 OVS(한 비트당 샘플 수)와 분주값 D 로 나눈다: baud = fclk / (OVS · D). D 가 정수면(16550 의 divisor latch, AVR 의 UBRR + 1) 오차는 D 의 반올림에서만 생긴다. D 에 분수 부분을 허용하면(STM32 USARTDIV 의 1/16 단위) 해상도가 16배 좋아진다. 둘을 하나로 쓰면 N = D·2^F 를 정수로 고르는 문제다.

```c
uint64_t num = (uint64_t)fclk << frac_bits;      /* fclk·2^F */
uint64_t den = (uint64_t)ovs * baud;             /* ovs·baud */
uint64_t n = (2 * num + den) / (2 * den);        /* round half up */
```

**트레이스 — 16 MHz, 115200.**

| 설정 | x = fclk·2^F/(ovs·baud) | N | 실제 baud | 오차 |
|---|---|---|---|---|
| 정수, OVS16 | 8.6806 | 9 | 111111.1 | −3.549% |
| 정수, OVS8 (AVR U2X) | 17.3611 | 17 | 117647.1 | +2.124% |
| 분수 F=4, OVS16 (STM32) | 138.8889 | 139 (BRR 0x08B) | 115107.9 | −0.080% |
| 버림(잘못), OVS16 | 8.6806 | 8 | 125000 | +8.507% |

AVR 의 UBRR 은 D − 1 이라 UBRR = 8 / U2X 일 때 16 — ATmega 데이터시트의 baud 표에 있는 −3.5% / +2.1% 가 바로 이 계산이다 (표 값과 같은지 확인 권장). 그래서 16 MHz Arduino 는 115200 에서 U2X(8배 오버샘플링)를 쓴다.

**성질 검사.** 정답과 다른 길로 확인한다: 정수 부등식 |2·N·den − 2·num| ≤ den (= |N − x| ≤ 1/2) 과 "N 이 x 보다 크면 실제 baud 는 느리다(오차 < 0)" 를 랜덤 20만 경우(유효 191928개)에서 검사 → bad = 0.

**오차 표** (solution.c 실제 출력. 위 줄 = 정수 분주 OVS16, 아래 줄 = 분수 F=4 OVS16, `-` = 분주 < 1):

| fclk | | 9600 | 19200 | 38400 | 57600 | 115200 | 230400 | 460800 | 921600 |
|---|---|---|---|---|---|---|---|---|---|
| 1.8432 MHz | int | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | −50.00 | - | - |
| | frac | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | - | - | - |
| 8 MHz | int | +0.16 | +0.16 | +0.16 | −3.55 | +8.51 | +8.51 | +8.51 | −45.75 |
| | frac | +0.04 | −0.08 | +0.16 | −0.08 | +0.64 | −0.79 | +2.12 | - |
| 14.7456 MHz | int | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| | frac | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 |
| 16 MHz | int | +0.16 | +0.16 | +0.16 | +2.12 | −3.55 | +8.51 | +8.51 | +8.51 |
| | frac | −0.02 | +0.04 | −0.08 | −0.08 | −0.08 | +0.64 | −0.79 | +2.12 |
| 48 MHz | int | −0.16 | +0.16 | +0.16 | +0.16 | +0.16 | +0.16 | −6.99 | +8.51 |
| | frac | 0.00 | 0.00 | 0.00 | +0.04 | −0.08 | +0.16 | +0.16 | +0.16 |
| 72 MHz | int | −0.05 | +0.16 | +0.16 | +0.16 | +0.16 | −2.34 | −2.34 | −2.34 |
| | frac | 0.00 | 0.00 | 0.00 | 0.00 | 0.00 | −0.16 | +0.16 | +0.16 |
| 80 MHz | int | −0.03 | +0.16 | +0.16 | −0.22 | +0.94 | −1.36 | −1.36 | +8.51 |
| | frac | 0.00 | −0.01 | +0.02 | −0.01 | +0.06 | +0.06 | −0.22 | −0.22 |

읽는 법:

- **1.8432 / 14.7456 MHz 는 "매직 크리스털"** — 1.8432 MHz = 16 × 115200. 표준 baud 가 모두 9600·2^k 또는 57600·2^k 꼴이라 정수 분주로 오차 0. 1.8432 MHz 에서 230400 은 x = 0.5 → 반올림 1 → 115200 이 나가 **−50%**: 계산기는 "분주 ≥ 1"만 보고 통과시키므로, 실제 드라이버는 |오차| 상한도 검사해야 한다 (7. 자주 하는 실수 5).
- **분수 분주가 큰 차이를 만든다** — 16 MHz 115200: 정수 −3.55% → 분수 −0.08%. 반올림 오차의 상한은 ±0.5/N 이므로 N 이 클수록(=fclk/baud 가 클수록) 작아진다. 921600 에서 16 MHz 분수 분주는 N = 17 이라 +2.12% 까지 커진다.
- **8 MHz 에서 921600 은 OVS16 으로 불가** — fclk/OVS = 500 kbaud 가 최대. OVS8 이면 최대 1 Mbaud 이고 분수로 −3.55%. 높은 baud 는 OVS8 의 존재 이유다(대가는 Part B 의 허용 한계 감소).
- **80 MHz 에서 300 baud 는 분수 BRR 에 안 들어간다** — N = 266667 > 0xFFFF → `BAUD_ETOO_SLOW`. 정수 분주(16667)는 들어간다. 최근 STM32 는 클럭 프리스케일러(PRESC)로 해결한다 (대표값 — RM 확인).

**STM32 BRR.** OVER8 = 0 이면 BRR 은 USARTDIV 를 1/16 단위로 쓴 고정소수점 수 그 자체다: 72 MHz / 115200 → USARTDIV 39.0625 → N = 625 = **0x271** (가수 0x27 = 39, 분수 1). F1 RM 의 예(기억 — RM0008 확인 권장)인 25.62 → 0x19A, 50.99 → 0x330 도 같은 식이다. 50.99 는 분수 15.84 가 16 으로 반올림돼 가수로 올라간다: N = round(815.84) = 816 = 0x330 — N 하나로 반올림하면 자연스럽다. OVER8 = 1 이면 N = USARTDIV × 8 (분수 3비트) 이고 BRR = `(N >> 3) << 4 | (N & 7)`, BRR[3] = 0: 16 MHz 115200 → N = 139 → 가수 17, 분수 3 → 0x113. 두 모드의 실제 baud 해상도는 같다 (둘 다 N = round(fclk/baud)).

### Part B — 수신기 모델과 이론 한계

**모델 (COMM-B02 축약판).** IDLE 에서 직전 샘플 1 → 이번 샘플 0 이면 그 샘플이 start 비트의 1번 샘플. 비트 k 의 샘플 8, 9, 10 (16x 기준) 다수결, 셋이 다르면 noise(NF). start 다수결이 1 이면 false start. 첫 stop 이 0 이면 framing error(FE). stop 의 10번 샘플을 처리하면 IDLE 로 돌아가고, 그 샘플 레벨이 다음 에지 검출의 "직전 샘플"이 된다. (STM32 도 16x 에서 8·9·10번 샘플을 쓴다고 RM 에 나오지만, start 검출 시퀀스 등 세부는 다르다 — 대표값.)

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">start 비트 한 개 안의 16개 샘플 (그림은 φ = 0.6)</text>
  <path d="M10,50 H60 V95 H720" fill="none" stroke="currentColor" stroke-width="2"/>
  <line x1="380" y1="35" x2="380" y2="120" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="384" y="44" fill="currentColor" font-size="12">비트 중앙 (0.5)</text>
  <line x1="700" y1="35" x2="700" y2="120" stroke="currentColor" stroke-dasharray="2 3"/>
  <text x="664" y="44" fill="currentColor" font-size="12">비트 끝</text>
  <g stroke="currentColor">
    <line x1="44" y1="100" x2="44" y2="112"/>
    <line x1="84" y1="100" x2="84" y2="112"/><line x1="124" y1="100" x2="124" y2="112"/><line x1="164" y1="100" x2="164" y2="112"/>
    <line x1="204" y1="100" x2="204" y2="112"/><line x1="244" y1="100" x2="244" y2="112"/><line x1="284" y1="100" x2="284" y2="112"/>
    <line x1="324" y1="100" x2="324" y2="112"/><line x1="484" y1="100" x2="484" y2="112"/><line x1="524" y1="100" x2="524" y2="112"/>
    <line x1="564" y1="100" x2="564" y2="112"/><line x1="604" y1="100" x2="604" y2="112"/><line x1="644" y1="100" x2="644" y2="112"/>
    <line x1="684" y1="100" x2="684" y2="112"/>
    <rect x="358" y="99" width="12" height="16" class="f-acc"/><rect x="398" y="99" width="12" height="16" class="f-acc"/>
    <rect x="438" y="99" width="12" height="16" class="f-acc"/>
  </g>
  <text x="36" y="128" fill="currentColor" font-size="12">0</text><text x="80" y="128" fill="currentColor" font-size="12">1</text>
  <text x="120" y="128" fill="currentColor" font-size="12">2</text><text x="360" y="130" fill="currentColor" font-size="12">8</text>
  <text x="400" y="130" fill="currentColor" font-size="12">9</text><text x="436" y="130" fill="currentColor" font-size="12">10</text>
  <text x="676" y="128" fill="currentColor" font-size="12">16</text>
  <line x1="60" y1="140" x2="84" y2="140" stroke="currentColor"/>
  <text x="50" y="156" fill="currentColor" font-size="12">φ: 에지 → 첫 0 샘플 (0 &lt; φ ≤ 1 샘플)</text>
  <text x="250" y="156" fill="currentColor" font-size="12">샘플 0 = IDLE 의 마지막 1 / 샘플 1 = 처음 본 0</text>
  <line x1="10" y1="172" x2="750" y2="172" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="10" y="194" fill="currentColor">샘플 m 의 위치 = (m − 1 + φ)/16 RX 비트 → TX 비트로는 (1 + e) 배,  e = baud_tx/baud_rx − 1</text>
  <text x="10" y="216" fill="currentColor">빠른 TX (e &gt; 0), 최악 φ = 1: 비트 j 의 샘플 m 이 j + 1 전에 → e &lt; (1 − m/16) / (j + m/16)</text>
  <text x="10" y="238" fill="currentColor">느린 TX (e &lt; 0), 최악 φ → 0: 샘플 m 이 j 뒤에 → e &gt; −((m − 1)/16) / (j + (m − 1)/16)</text>
  <text x="10" y="264" fill="currentColor" class="f-acc">8N1, j = 9 (stop): 샘플 10 → +0.375/9.625 = +3.896%   샘플 9 → −0.5/9.5 = −5.263%</text>
  <text x="10" y="288" fill="currentColor" font-size="12">(1 − m/16) = 샘플 뒤에 남은 비트 길이, (j + m/16) = 에지에서 그 샘플까지 누적된 비트 수 — "남은 여유 / 누적 거리"</text>
</svg>
```

**유도.** 에지는 TX 시간 0, RX 는 에지 뒤 φ 샘플에 1번 샘플을 찍는다. m 번 샘플은 RX 시간으로 (m − 1 + φ)/16 비트 뒤다. RX 비트는 TX 비트의 baud_tx/baud_rx = 1 + e 배 길이이므로, 비트 j 의 m 번 샘플은 TX 시간 (1 + e)(j + (m−1+φ)/16) 에 있다. 이게 [j, j+1) 안에 있어야 한다.

- 빠른 TX (e > 0): 샘플이 늦어진다. 오른쪽 경계가 문제이고, φ = 1 일 때 가장 늦다 → (1 + e)(j + m/16) < j + 1.
- 느린 TX (e < 0): 샘플이 일러진다. 왼쪽 경계가 문제이고, φ → 0 일 때 가장 이르다 → (1 + e)(j + (m−1)/16) ≥ j.

이것이 `tol_theory` 의 두 식이다. 교과서식 "0.5/9.5 = 5.26%"는 정확히 비트 중앙에서 하나만 샘플링하고 에지 지연이 없는 경우다. 실제로는 (1) 에지 검출이 최대 1샘플 늦고(φ), (2) 다수결 창이 중앙 뒤로 1샘플 더 뻗으므로 늦는 쪽 여유가 0.5 → 0.4375 → 0.375 비트로 줄어든다. **그래서 한계가 비대칭이다.**

**어느 샘플이 마지막으로 맞아야 하나.** 샘플이 이웃 비트로 넘어가도 이웃 비트 값이 같으면 오류가 아니다. 그래서 프레임 끝의 상황이 결정한다:

| 경우 | 빠른 TX (e > 0) 에서 결정하는 것 | 이론 | 느린 TX (e < 0) | 이론 |
|---|---|---|---|---|
| 8N1 back-to-back | stop(j=9)의 **샘플 10** 이 다음 start(0)에 들어가면 IDLE 진입 시 직전 레벨 0 → 다음 에지를 놓친다 | +3.896% | stop 의 샘플 9 가 b7(0일 때)에 → FE | −5.263% |
| 8N1 isolated | stop 뒤는 idle(1)이라 무해. b7(j=8)의 샘플 9 가 stop(1)에 → b7=0 이면 데이터 오류 | +5.109% | 같음 | −5.263% |
| 8E1 back-to-back | stop(j=10) 샘플 10 | +3.529% | stop 샘플 9 | −4.762% |
| 8E1 isolated | 패리티(j=9) 샘플 9 | +4.575% | stop 샘플 9 | −4.762% |

**비트별 트레이스** — 'A', e = +4.5%, φ = 1, 9번 샘플의 TX 위치 = 1.045 × (j + 0.5625):

| j | 0 start | 1 b0 | 2 b1 | 3 b2 | 4 b3 | 5 b4 | 6 b5 | 7 b6 | 8 b7 | 9 stop |
|---|---|---|---|---|---|---|---|---|---|---|
| TX 값 | 0 | 1 | 0 | 0 | 0 | 0 | 0 | 1 | 0 | 1 |
| 샘플 9 위치 | 0.588 | 1.633 | 2.678 | 3.723 | 4.768 | 5.813 | 6.858 | 7.903 | 8.948 | 9.993 |
| 비트 안 여유 | 0.41 | 0.37 | 0.32 | 0.28 | 0.23 | 0.19 | 0.14 | 0.10 | 0.05 | 0.007 |

stop 의 샘플 10 은 10.058 → 다음 start. 이 프레임 자체는 0x41 로 맞게 받지만(NF), 수신기가 IDLE 로 돌아갈 때 마지막 레벨이 0 이라 **다음 프레임의 start 에지를 놓친다**. 이후 데이터 안의 아무 1→0 을 start 로 잡아 쓰레기를 받고, back-to-back 이면 idle 이 없어 다시 맞출 기회가 거의 없다. 그래서 아래 곡선에서 back-to-back 의 빠른 쪽은 한계를 넘자마자 오류율이 ~100% 로 뛴다.

**실측 — `demo_sweep.c`** (프레임 20000개, 랜덤 데이터·위상, e = 0 에서 0.01% 씩 바깥으로. 실제 출력):

| 설정 | 빠른 TX 첫 오류 | 이론 | 첫 NF | 이론 | 느린 TX 첫 오류 | 이론 | 첫 NF | 이론 |
|---|---|---|---|---|---|---|---|---|
| 8N1 x16 back-to-back | +3.90% | +3.896% | +3.90% | +3.896% | −5.28% | −5.263% | −4.64% | −4.636% |
| 8N1 x16 isolated | +5.11% | +5.109% | +4.35% | +4.348% | −5.27% | −5.263% | −4.64% | −4.636% |
| 8E1 x16 back-to-back | +3.53% | +3.529% | +3.53% | +3.529% | −4.77% | −4.762% | −4.20% | −4.192% |
| 8E1 x16 isolated | +4.58% | +4.575% | +3.90% | +3.896% | −4.77% | −4.762% | −4.20% | −4.192% |
| 8N1 x8 back-to-back | +2.58% | +2.564% | +2.58% | +2.564% | −5.28% | −5.263% | −4.04% | −4.000% |
| 8N1 x8 isolated | +4.36% | +4.348% | +2.87% | +2.857% | −5.27% | −5.263% | −4.01% | −4.000% |

측정 온셋은 이론 최악값보다 0.00~0.04% 바깥에 있다 — 최악 위상(φ = 1 또는 φ → 0)과 최악 데이터(b7 = 0)가 동시에 올 확률이 작아, 한계 바로 바깥에서는 아직 오류가 안 보이기 때문이다. 이론이 **최악**이고 측정은 그 바깥에서 처음 걸리므로, 둘은 모순이 없다. solution.c 는 이것을 고정 점으로 검사한다: 한계 0.1% 안쪽 3000 프레임 오류 0, 0.3% 바깥 오류 > 0 (예: 8N1 back-to-back +4.20% → 2992/3000 오류, −5.56% → 751).

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">8N1 x16 프레임 오류율 vs 편차 e (demo_sweep.c 실측, 4000 프레임/점)</text>
  <g stroke="currentColor" stroke-width="0.8">
    <line x1="70" y1="250" x2="700" y2="250"/><line x1="70" y1="70" x2="70" y2="250"/>
    <line x1="70" y1="160" x2="700" y2="160" stroke-dasharray="2 4"/><line x1="70" y1="70" x2="700" y2="70" stroke-dasharray="2 4"/>
  </g>
  <text x="30" y="254" fill="currentColor" font-size="12">0%</text><text x="24" y="164" fill="currentColor" font-size="12">50%</text>
  <text x="18" y="74" fill="currentColor" font-size="12">100%</text>
  <g font-size="12" fill="currentColor">
    <text x="62" y="268">−7</text><text x="150" y="268">−5</text><text x="238" y="268">−3</text><text x="374" y="268">0</text>
    <text x="504" y="268">+3</text><text x="590" y="268">+5</text><text x="678" y="268">+7</text>
  </g>
  <text x="300" y="286" fill="currentColor" font-size="12">e = baud_tx/baud_rx − 1 (%)</text>
  <g stroke="currentColor" stroke-dasharray="5 3">
    <line x1="146" y1="60" x2="146" y2="250"/><line x1="549" y1="60" x2="549" y2="250"/><line x1="603" y1="60" x2="603" y2="250"/>
  </g>
  <text x="100" y="56" fill="currentColor" font-size="12">−5.263</text><text x="508" y="56" fill="currentColor" font-size="12">+3.896</text>
  <text x="600" y="56" fill="currentColor" font-size="12">+5.109</text>
  <line x1="246" y1="250" x2="510" y2="250" stroke="currentColor" stroke-width="3" stroke-dasharray="1 5"/>
  <polyline points="70,105 81,114 92,120 103,134 114,153 125,182 136,217 147,250 158,250 169,250 246,250" fill="none" stroke="currentColor" stroke-width="2.5"/>
  <polyline points="510,250 543,250 554,71 565,70 576,70 587,70 598,70 609,70 620,70 631,70 642,70 653,70 664,70 675,70 686,70" fill="none" stroke="currentColor" stroke-width="2.5"/>
  <polyline points="70,106 81,114 92,121 103,135 114,151 125,181 136,219 147,250" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="6 3"/>
  <polyline points="510,250 598,250 609,234 620,204 631,174 642,149 653,138 664,122 675,116 686,107" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="6 3"/>
  <rect x="250" y="88" width="230" height="56" class="f-mut" stroke="currentColor"/>
  <line x1="260" y1="104" x2="290" y2="104" stroke="currentColor" stroke-width="2.5"/>
  <text x="298" y="108" fill="currentColor" font-size="12">back-to-back</text>
  <line x1="260" y1="128" x2="290" y2="128" stroke="currentColor" stroke-width="1.5" stroke-dasharray="6 3"/>
  <text x="298" y="132" fill="currentColor" font-size="12">isolated (프레임 사이 ≥ 2비트 idle)</text>
  <text x="10" y="308" fill="currentColor" font-size="12">−3% ~ +3% 는 온셋 스윕(0.01% 간격, 20000 프레임)에서 오류 0. 세로 점선 = 이론 최악 한계.</text>
  <text x="10" y="324" fill="currentColor" font-size="12">느린 쪽은 두 곡선이 겹친다(stop 이 b7 로 밀림). 빠른 쪽 back-to-back 은 에지를 놓친 뒤 재동기화를 못 해 ~100%.</text>
</svg>
```

**STM32 RM 의 허용 표와 비교.** RM 의 "receiver tolerance to clock deviation" 표는 (기억값 — 해당 패밀리 RM 확인 권장) 8비트 데이터, 분수 0 일 때 OVER8 = 0 에서 3.75%(ONEBIT = 0) / 4.375%(ONEBIT = 1), OVER8 = 1 에서 2.50% / 3.75% 다. 이 숫자들은 이 레슨의 "남은 여유" 를 프레임 길이 10 비트로 나눈 값과 같다: (1 − 10/16)/10 = 3.75%, (1 − 9/16)/10 = 4.375%, (1 − 6/8)/10 = 2.5%, (1 − 5/8)/10 = 3.75%. 9비트 데이터(11비트 프레임)면 /11 → 3.41%, 3.98%. 분모를 9.625 대신 10 으로 잡은 만큼 보수적이고, RM 은 이 허용치를 송신 측 오차·분주 양자화·수신 측 오차·라인 비대칭 같은 항들의 **합**이 넘으면 안 되는 예산으로 쓴다. ONEBIT = 1(한 샘플만 봄)이 더 관대한 이유도 같다: 마지막으로 맞아야 할 샘플이 10 → 9 로 당겨진다. 대신 noise 검출을 잃는다.

### Part C — 링크 예산

상대 편차는 **양쪽의 차이**다: e = baud_tx·(1 + δ_tx) / (baud_rx·(1 + δ_rx)) − 1 ≈ e_tx − e_rx. 각 발진기 δ ∈ [−ppm, +ppm] 의 최악 조합 두 개(TX 빠름·RX 느림, 반대)가 범위의 양 끝이다. 한계는 수신측 OVS 의 back-to-back 최악값(늦는 쪽 샘플 m+1, 이른 쪽 샘플 m, j = 프레임 비트 − 1). 판정 규칙은 이 레슨의 약속이다: 한계를 넘으면 FAIL, 한계의 절반을 넘으면 MARGINAL — 절반은 신호 에지의 기울기(slew)·비대칭, 동기화기 지연, 노이즈를 위한 여유이고 결과적으로 흔한 "총 ±2%" 규칙과 비슷하다 (8N1: 3.9/2 ≈ 1.95%).

solution.c 실제 출력 (상대편 PC/브리지 = 14.7456 MHz ±50 ppm, 정수 x16, 8N1 은 frame_bits 10):

| 장치 A | 형식 | A→PC 범위 | PC→A 범위 | 수신측 한계 (A 쪽) | 판정 | 코너 시뮬레이션 오류 |
|---|---|---|---|---|---|---|
| STM32 16 MHz HSE ±50 ppm, 분수 | 8N1 | −0.09 ~ −0.07% | +0.07 ~ +0.09% | −5.26 / +3.90 | OK | 0 0 0 0 |
| STM32 HSI ±1% (대표값), 분수 | 8N1 | −1.08 ~ +0.92% | −0.92 ~ +1.10% | −5.26 / +3.90 | OK | 0 0 0 0 |
| AVR 16 MHz 정수 x16 | 8N1 | −3.56 ~ −3.54% | +3.67 ~ +3.69% | −5.26 / +3.90 | MARGINAL | 0 0 0 0 |
| AVR 16 MHz 정수 x16 | 8E1 | −3.56 ~ −3.54% | +3.67 ~ +3.69% | −4.76 / +3.53 | FAIL | 0 0 1990 1992 |
| AVR 16 MHz U2X x8 | 8N1 | +2.11 ~ +2.13% | −2.09 ~ −2.07% | −5.26 / +2.56 | MARGINAL | 0 0 0 0 |
| 내부 RC ±4% (가정), 분수 | 8N1 | −4.08 ~ +3.92% | −3.77 ~ +4.26% | −5.26 / +3.90 | FAIL | 1974 0 1996 0 |
| 8 MHz 분수 x16 @921600 | 8N1 | — | — | — | BADCFG | — |

해석:

- **AVR x16 의 8N1 은 "돌아가지만 위험"** — PC 가 AVR 로 보낼 때 AVR 입장에서 PC 가 3.68% 빠르다. 한계 3.90% 까지 0.2% 남았다. 같은 설정에서 패리티를 켜면(8E1, 한계 3.53%) 바로 깨진다: 시뮬레이션에서 PC→A 코너 두 개가 2000 프레임 중 1990, 1992 개 오류. "8N1 은 되는데 8E1 로 바꾸니 안 된다"는 현장 증상의 정체다.
- **U2X(x8)는 오차를 줄였지만 수신 여유도 줄었다** — 늦는 쪽 한계 2.56%. PC→AVR 편차가 −2.08%(이른 쪽)라 AVR 이 받는 데는 문제가 없고, AVR→PC 는 +2.12% 를 PC 의 x16 수신기(+3.90%)가 받는다. 방향마다 한계가 다른 쪽 수신기에 달려 있다는 점이 핵심이다.
- **HSI ±1% 는 분수 BRR 과 함께면 OK** — 하지만 온도 전 범위의 HSI 정확도는 ±1% 보다 넓을 수 있다 (데이터시트의 온도별 표 확인). 그래서 RC 발진기로 UART 를 쓰는 설계는 LSE 기준 보정(trim)이나 자동 baud 검출을 쓴다 (Q6).
- **내부 RC ±4%** 는 한쪽 코너만 깨진다 (TX 빠름 → 받는 쪽 late 한계 3.90% 초과). 반대 코너(4% 느림)는 이른 쪽 한계 −5.26% 안이라 오류 0 — 비대칭 한계가 그대로 보인다.

### 복잡도

`baud_calc` O(1), 64비트 곱·나눗셈 하나. 수신기 `urx_step` 은 샘플당 O(1), 상태 몇 바이트(실제 하드웨어에서는 USART 가 하는 일). `sim_link` 은 O(프레임 × 비트 × OVS), 3000 프레임 8N1 은 약 48만 샘플 — solution.c 전체 0.04 s. `demo_sweep.c` 는 6 설정 × 양방향 × 최대 수백 점 × 20000 프레임이라 약 30 s.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution      # 53/53 passed
cc -std=c11 -Wall -Wextra -Werror -O2 demo_sweep.c -o demo_sweep && ./demo_sweep   # 온셋 표 + 오류율 곡선 (~30 s)
```

## 7. 자주 하는 실수

**1. 반올림 대신 버림.** `div = fclk / (16 * baud)` 는 항상 내림이라 실제 baud 가 항상 목표보다 빠르고(오차 ≥ 0), 오차도 크다. 16 MHz 115200: 버림 8 → 125000 (+8.51%), 반올림 9 → −3.55%. 고친 코드: `(2 * num + den) / (2 * den)`. 단, 반올림해도 −3.55% 는 여전히 크다 — 분수 분주나 U2X 를 검토한다.

**2. 32비트 넘침.** `fclk << 4` 는 fclk > 268 MHz 에서 uint32 를 넘는다. `16 * baud * 2` 같은 중간값도 조심. 고친 코드: 처음부터 `(uint64_t)fclk << frac_bits`. 랜덤 성질 검사에 fclk 1~100 MHz, F 0~4 를 섞는 이유.

**3. 가수와 분수를 따로 반올림하고 OR 로 합친다.** 51.99: 가수 51 = 0x33 → 0x330, 분수 round(15.84) = 16 = 0x10 → `0x330 | 0x10` = **0x330** (USARTDIV 51.0, −1.9%). 정답은 N = round(831.84) = 832 = **0x340**. 50.99 는 가수 50(짝수)이라 `0x320 | 0x10` = 0x330 으로 우연히 맞아 버그가 숨는다. 테스트가 51.99 를 넣는 이유. 고친 코드: N 하나로 반올림하거나, 분수 16 이면 가수 + 1.

**4. "5.26% 까지 된다"고 믿는다.** 0.5/9.5 는 에지 지연 0, 샘플 하나, 이상적 에지일 때다. 다수결 창과 에지 지연을 넣으면 back-to-back 8N1 의 늦는 쪽은 3.90%, 8E1 은 3.53% 이고, 실제 보드는 에지 기울기·노이즈로 더 줄어든다. 그리고 이 값은 **송수신 합계**다 — 한쪽에 다 쓰면 상대편 몫이 없다.

**5. |오차| 상한을 안 본다.** 1.8432 MHz 에서 230400 은 분주 0.5 → 1 로 반올림돼 계산기가 OK 를 돌려주지만 실제는 115200 (−50%). 드라이버 초기화는 `|err| ≤ 한도`(예 1.5%)를 확인하고 실패를 보고해야 한다 — 조용히 엉뚱한 baud 로 도는 것이 최악이다.

**6. 한쪽 오차만 본다.** 두 장치가 같은 방향으로 3% 느리면 서로는 잘 통한다 (편차 ≈ 0). HWLAB-M05 의 "26 MHz 크리스털 + HSE_VALUE = 24" 보드처럼 MCU 의 모든 주변장치가 같은 비율로 틀리면 MCU 끼리는 되고 PC 와만 안 된다. 반대로 각자 ±2% 인 두 장치는 최악 4% 차이가 난다. 링크 예산은 **양쪽 다, 양방향 다**.

**7. 시뮬레이션을 고정 위상으로 돌린다.** RX 격자 위상을 고정하면 최악 위상(φ ≈ 1)을 영영 안 만날 수 있고, 온셋이 이론보다 0.6% 가까이 바깥에서 나온다 (위상에 따른 한계 폭 = 1/16 샘플 ÷ 9.6 비트 ≈ 0.65%). 위상과 데이터를 랜덤으로, 프레임 수천 개.

## 8. 면접 Q&A

<details><summary>Q1. UART 수신기는 왜 16배 오버샘플링하나요? 8배는 언제 쓰죠?</summary>

클럭이 없으니 start 비트의 하강 에지로 위상을 맞춰야 한다. 샘플이 촘촘할수록 에지를 늦게 볼 최대 지연(φ, 1샘플 = 1/OVS 비트)이 줄고 비트 중앙을 정확히 겨냥할 수 있다. 16x 면 에지 지연 최대 1/16 비트, 다수결 3샘플(8·9·10)로 노이즈도 거른다. 8x 는 같은 fclk 에서 최대 baud 가 두 배(fclk/8)라 높은 baud 에 쓰지만, 샘플 창(4·5·6)이 비트의 3/8 → 6/8 지점까지 뻗어 늦는 쪽 허용이 3.90% → 2.56% (8N1 back-to-back, 이 레슨 이론·실측)로 줄어든다. STM32 의 OVER8, AVR 의 U2X 가 이 선택이다.

</details>

<details><summary>Q2. 16 MHz 에서 115200 을 쓰라고 하면 어떻게 하나요?</summary>

16e6/(16·115200) = 8.68 → 정수 분주 9 → 111111 baud (−3.55%). 세 가지 길: (1) 분수 분주가 있으면(STM32) N = round(16e6/115200) = 139 → −0.08% — 끝. (2) 정수만 있으면 OVS8(U2X): 17 → +2.12% — 상대편 수신기 x16 기준 늦는 쪽 한계 3.90% 안이라 대개 동작하지만 여유가 크지 않다. (3) 클럭을 바꾼다: 14.7456 MHz 크리스털이면 모든 표준 baud 오차 0. 또는 baud 를 76800/38400 등으로 낮춘다 (38400 은 정수 x16 으로 +0.16%). 어느 경우든 "양쪽 다" 보는 것이 중요 — 상대가 FTDI 같은 브리지면 그쪽 분주 오차도 있다 (칩 앱노트 확인).

</details>

<details><summary>Q3. 허용 오차가 몇 % 인가요? 그 숫자는 어디서 나오나요? 왜 빠른 쪽과 느린 쪽이 다르죠?</summary>

수신기는 start 에지에서 한 번 맞추고 stop 까지 혼자 간다. 마지막으로 맞아야 할 샘플(8N1 의 stop, 에지에서 약 9.6 비트)까지 누적된 어긋남이 "그 샘플 뒤(또는 앞)에 남은 비트 길이"를 넘으면 깨진다: 늦는 쪽 e < (1 − m/16)/(j + m/16), 이른 쪽 e > −((m−1)/16)/(j + (m−1)/16). 8N1 back-to-back 16x 에서 +3.90% / −5.26%, 8E1 +3.53% / −4.76%. 시뮬레이션 온셋이 이론과 0.04% 안에서 맞았다. 비대칭인 이유: 에지 검출이 최대 1 샘플 늦고 다수결 창이 중앙 뒤로 1샘플 더 있어서, 수신기는 원래 "늦게" 샘플링한다 — TX 가 빠르면 그 늦음이 더해지고, 느리면 상쇄된다. 실무 규칙은 총 ±2% 정도(각 측 ±1%), 이유는 에지 기울기·노이즈·동기화 지연이 여유를 더 먹기 때문이다.

</details>

<details><summary>Q4. 클럭이 4% 틀린 보드에서 UART 를 스코프/LA 로 보면 무엇이 보이나요? 증상은요?</summary>

증상은 "가끔", "데이터에 따라" 깨진다. (1) 앞쪽 비트는 맞고 **마지막 비트(MSB, 패리티)와 stop** 이 틀린다 — b7 = 0 인 바이트만 깨지고 0xFF 나 b7 = 1 인 바이트는 멀쩡하다. 그래서 에코 테스트를 0x55('U')만 하면 놓치고, 0x00 이나 0x7F 로 하면 바로 보인다. (2) framing error 가 데이터 오류보다 많다(느린 TX). (3) 연속 전송(DMA 로 블록 송신)에서만 깨지고 한 글자씩 타이핑하면 멀쩡하다 — back-to-back 에서 에지를 놓치면 재동기화가 안 되기 때문(이 레슨의 오류율 곡선 ~100%). 스코프/LA 에서는 비트 폭을 직접 잰다: 프레임 안 첫 하강 에지부터 마지막 에지까지 span 을 비트 수로 나누면 baud 를 0.1% 수준으로 잴 수 있다 (HWLAB-M05). LA 디코더를 공칭 baud 로 걸면 같은 쓰레기가 보이고, 측정 baud 로 걸면 깨끗해진다 — 그게 결정적 증거다.

</details>

<details><summary>Q5. 송신 측 오차와 수신 측 오차는 어떻게 합치나요? 둘 다 3% 느리면?</summary>

중요한 것은 **상대 편차** baud_tx/baud_rx − 1 ≈ e_tx − e_rx 다. 둘 다 3% 느리면 편차 ≈ 0 이라 서로는 완벽히 통한다. 그래서 같은 클럭 트리 실수(크리스털 값 착각, PLL 설정)는 MCU 끼리는 숨고 PC 와 붙일 때 드러난다. 예산은 최악 조합: A 가 +δ 이고 B 가 −δ 인 코너. Part C 는 이 네 코너(두 방향 × 두 부호)를 이론으로 판정하고 시뮬레이션으로 확인한다. 크리스털 ±50 ppm 은 0.005% 라 분주 오차 옆에서 무시할 수 있고, RC 발진기 ±1~수 % 는 예산의 대부분을 먹는다.

</details>

<details><summary>Q6. 내부 RC 발진기로 UART 를 써도 되나요?</summary>

조건부로. 공장 보정된 HSI 가 상온 ±1% 정도면(대표값) 분수 BRR 과 함께 8N1 은 여유가 있다(Part C: −1.08 ~ +1.10%, OK). 하지만 온도 전 범위·전압에서는 더 넓어질 수 있고 상대편 오차까지 더하면 빠듯하다. 방법: (1) LSE 32.768 kHz 크리스털이나 외부 기준으로 RC 를 런타임 보정(trim), (2) 자동 baud 검출 — 알려진 문자(0x55, 0x7F 등)의 start 비트나 에지 간격을 타이머로 재서 BRR 을 맞춘다 (STM32 일부 패밀리는 하드웨어 ABR 이 있다 — RM 확인), (3) LIN 처럼 프로토콜이 매 프레임 sync 필드(0x55)를 보내 슬레이브가 맞추게 한다 (허용치는 LIN 스펙 확인). 크리스털이 있으면 크리스털을 쓰는 것이 기본.

</details>

<details><summary>Q7. 연속 스트림에서 한 번 어긋나면 왜 계속 깨지나요? 어떻게 다시 맞추죠?</summary>

UART 는 1→0 에지를 start 로 믿는다. stop 끝을 놓쳐 IDLE 진입이 늦으면 다음 start 에지를 못 보고, 데이터 중간의 1→0 을 start 로 잡는다. 그러면 프레임 경계가 계속 틀린 채로 간다 — idle 이 한 문자 시간 이상 오기 전까지는 맞출 근거가 없다. 대책: (1) 링크 계층에서 idle gap 이나 2 stop 비트를 두어 경계를 보장(연습 3: 8N2 로 보내면 늦는 쪽 한계 3.90% → 5.11%), (2) break(긴 0)로 리셋, (3) 바이트 스트림 위에 프레이밍 — SOF + 길이 + CRC(COMM-B10), SLIP(COMM-B11), COBS(COMM-B12) — 로 상위에서 재동기화, (4) 수신 측 IDLE line 인터럽트로 패킷 경계를 잡는다(COMM-M11).

</details>

<details><summary>Q8. 921600 이나 3 Mbaud 같은 높은 baud 에서 추가로 볼 것은?</summary>

(1) 분주 해상도: N 이 작아져 반올림 오차가 커진다 (16 MHz 분수로 921600 은 N = 17, +2.12%). fclk 를 높이거나 baud 에 맞는 클럭을 고른다. (2) 최대 baud = fclk/OVS — OVS8 이 필요할 수 있고, 그러면 허용 한계가 줄어든다. (3) 물리 계층: RS-232 트랜시버는 부품마다 최대 속도가 있고(데이터시트 확인), 긴 선의 에지 기울기가 비트 시간의 큰 비율이 되어 실제 여유를 줄인다. (4) 수신 처리: 921600 8N1 이면 문자 간격 약 10.9 µs — 바이트마다 인터럽트는 부담이라 DMA + IDLE 감지가 필요하다 (COMM-M11, BM-M04). (5) 하드웨어 흐름 제어(RTS/CTS) 없이 버퍼가 넘치면 overrun(COMM-B02).

</details>

## 9. 추가 연습

### 연습 1 — FPU 없는 MCU 의 정수 ppm 오차

Cortex-M0 에서 double 없이 오차를 ppm 정수로 계산하라 (부트 로그에 "baud 115108 (−799 ppm)" 출력용). double 버전과 1 ppm 이내로 맞는지 랜덤으로 확인.

<details><summary>답 보기</summary>

q = ovs·N·baud (실제 분모), ppm = round(1e6·(num − q)/q), 음수도 대칭 반올림: `d >= 0 ? (d + q/2)/q : −((−d + q/2)/q)`. |num − q| ≤ den/2 이므로 d = (num − q)·1e6 은 int64 에 충분히 들어간다. 랜덤 10만 경우(유효 96095) 중 double 버전과 1 ppm 넘게 다른 것 0. 16 MHz 115200: x16 정수 −35494, U2X +21242, 분수 −799 ppm.

</details>

### 연습 2 — 8x 오버샘플링의 허용 한계

OVS8(샘플 4·5·6)에서 8N1 back-to-back 의 이론 한계를 구하고 시뮬레이션으로 확인하라. 16x 와 비교해 어느 쪽이 얼마나 줄었나?

<details><summary>답 보기</summary>

늦는 쪽: 샘플 6, (1 − 6/8)/(9 + 6/8) = 0.25/9.75 = **+2.564%**. 이른 쪽: 샘플 5, −(4/8)/(9.5) = **−5.263%** (16x 와 같다 — 가운데 샘플이 정확히 0.5 + φ/8 이고 최악 φ → 0 이면 0.5). NF 는 샘플 4 로 −(3/8)/9.375 = −4.000%. 시뮬레이션: +2.46% 오류 0, +2.86% 2989/3000, −5.16% 0, −5.56% 346. 즉 8x 는 **늦는 쪽만** 1.33% 잃는다 — 샘플 하나가 1/8 비트라 에지 지연과 다수결 창이 두 배로 커지기 때문. demo_sweep 실측 온셋 +2.58% / −5.28%.

</details>

### 연습 3 — 8N2 로 보내고 8N1 로 받기

송신은 stop 2개, 수신기는 첫 stop 만 본다(흔한 구현 — 칩마다 확인). back-to-back 에서 빠른 쪽 한계는 얼마로 바뀌나?

<details><summary>답 보기</summary>

stop1 의 샘플 10 이 밀려도 stop2(1)에 떨어져 수신기가 IDLE 로 돌아갈 때 직전 레벨이 1 이다 → 다음 start 에지를 놓치지 않는다. 그러면 isolated 와 같아져, 마지막 위험 비트는 b7(j = 8)의 샘플 9: (1 − 9/16)/(8 + 9/16) = **+5.109%** (back-to-back 8N1 의 3.896% 에서 1.2% 증가). 시뮬레이션: +5.01% 오류 0, +5.41% 582/3000, NF 는 +4.25% 에서 0 (NF 한계 b7 샘플 10: 4.348%). 느린 쪽 −5.26% 는 그대로. 처리량 10% 를 내주고 클럭 여유와 재동기화를 산다 — 오차가 큰 링크에서 2 stop 비트를 쓰는 이유.

</details>

### 연습 4 — 매직 크리스털

정수 분주 x16 으로 9600 부터 오차 0 인 가장 큰 표준 baud 를 크리스털별로 구하라: 1.8432, 3.6864, 7.3728, 11.0592, 14.7456, 18.432, 8, 12, 16, 24 MHz.

<details><summary>답 보기</summary>

fclk % (16·baud) == 0 을 9600 부터 올라가며 검사. 결과: 1.8432 → 115200, 3.6864 → 230400, 7.3728 → 460800, 11.0592 → 230400, 14.7456 → 921600, 18.432 → 230400, 8/12/16/24 MHz → 0 (9600 부터 안 맞음). 18.432 MHz 가 14.7456 MHz 보다 크지만 18.432e6/16 = 1152000 = 2^10·3^2·5^3 에 921600 = 2^12·3^2·5^2 이 안 들어간다 — **2 의 거듭제곱 인수가 모자라기 때문**. 큰 크리스털이 항상 나은 것은 아니다. baud_calc 로 그 baud 까지 err == 0 도 함께 확인.

</details>

### 연습 5 — 발진기 오차 예산

STM32 16 MHz 분수 BRR(−0.0799%)이 115200 8N1 로 이상적인 PC 와 통신한다. MCU 클럭이 ±x% 흔들릴 때, FAIL 이 되지 않는 최대 x 와 MARGINAL 이 되지 않는 최대 x 를 닫힌 식으로 구하고 이분 탐색·시뮬레이션으로 확인하라.

<details><summary>답 보기</summary>

a = 1 + e0, 한계 L = 1 + lo, H = 1 + hi. 네 조건: MCU→PC 에서 a(1 + x) < H, a(1 − x) > L, PC→MCU 에서 1/(a(1 − x)) < H, 1/(a(1 + x)) > L. 각각 x < H/a − 1, 1 − L/a, 1 − 1/(Ha), 1/(La) − 1 이고 최솟값이 답. 8N1 x16 (lo −5.263%, hi +3.896%): **x_max = 3.673%** — 결정하는 것은 "MCU 가 느려서 PC 가 MCU 입장에서 빠른" 코너(1 − 1/(Ha)). 이분 탐색 60회 결과와 1e−6 이내로 같다. 시뮬레이션: MCU 3.62% 느림 → 오류 0, 3.97% 느림 → 2995/3000. 절반 규칙(MARGINAL 경계)으로는 1.832%.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 17/17 passed
```

## 10. 핵심 정리

- N = round(fclk·2^F/(OVS·baud)) 를 uint64 정수로, 실제 baud = fclk·2^F/(OVS·N), 오차 = 실제/목표 − 1. 버림 금지, 분수 BRR 은 N 하나로 반올림(가수 자리올림 자동).
- 16 MHz 115200: 정수 x16 −3.55%, x8 +2.12%, 분수 −0.08%. 1.8432/14.7456 MHz 는 표준 baud 오차 0.
- 수신기는 start 에지에서 한 번 맞추고 stop 까지 혼자 간다. 한계 = 남은 여유 / 누적 비트: 늦는 쪽 (1 − m/16)/(j + m/16), 이른 쪽 −((m−1)/16)/(j + (m−1)/16).
- 8N1 x16 back-to-back: **+3.90% / −5.26%**, 8E1: +3.53% / −4.76%, x8: +2.56% / −5.26%. 실측 온셋이 이론과 0.04% 안.
- 비대칭 원인: 에지 검출 지연(최대 1 샘플) + 다수결 창 → 수신기는 원래 늦게 샘플링한다. 빠른 TX 쪽이 약하다.
- back-to-back 에서 stop 끝을 놓치면 다음 에지를 못 보고 계속 깨진다 → idle, 2 stop, 상위 프레이밍으로 재동기화.
- 예산은 양쪽·양방향: 상대 편차 ≈ e_tx − e_rx + 발진기. 크리스털 ppm 은 무시 가능, RC ±1~수 % 는 예산의 대부분. 설계 규칙 총 ±2%.
- 증상: 데이터 의존(b7 = 0 만), 연속 전송에서만, 8N1 은 되는데 8E1 은 안 됨 → LA 로 비트 폭부터 잰다(HWLAB-M05).

## 참고자료

- STMicroelectronics, RM0008 (STM32F1 reference manual) — USART "Fractional baud rate generation": USARTDIV, BRR 가수/분수, 예제(0x1BC, 25.62 → 0x19A, 50.99 → 0x330 은 기억 — 확인 권장), 72 MHz 115200 → 39.0625.
- STMicroelectronics, 최근 패밀리 RM (예: RM0351 STM32L4, RM0440 STM32G4) — USART "Tolerance of the USART receiver to clock deviation" 표(OVER8, ONEBIT, M 별 3.75/4.375/2.50/3.75% 등 — 기억값, 확인 권장), 8·9·10번 샘플 다수결과 noise 검출, OVER8 = 1 의 BRR[3:0] 규칙, 자동 baud 검출(ABR, 패밀리별).
- Microchip, ATmega328P 데이터시트 — USART0 "Clock Generation"(UBRR = fosc/(16·baud) − 1, U2X), "Asynchronous Operational Range", baud 설정 예 표 (16 MHz 115.2k: −3.5% / U2X 2.1%).
- Texas Instruments, PC16550D UART 데이터시트 — divisor latch, 1.8432 MHz 기준 divisor 표.
- Maxim Integrated (현 Analog Devices), Tutorial 2141 "Determining Clock Accuracy Requirements for UART Communications" — 샘플 위치와 프레임 길이로 허용 오차를 유도 (제목·번호는 기억 — 확인 권장).
- LIN Specification 2.x — sync 필드(0x55)와 RC 발진기 슬레이브의 baud 동기 허용치 (수치는 스펙 확인).
- 관련 레슨: COMM-B01 (UART 프레이밍), COMM-B02 (16x 오버샘플링 수신기, 이 레슨의 모델 원본), COMM-B10 / B11 / B12 (바이트 스트림 재동기화), COMM-M10 (견고한 파서), COMM-M11 (DMA + idle line), HWLAB-M05 (LA 로 baud 측정, 26 MHz 크리스털 사례), HWLAB-M06 (LA 디코더), C-M10 (UART 용 mini printf), BM-B12 / BM-M04 (실제 polling / IRQ UART 드라이버), COMM-M06 (CAN 비트 타이밍: 같은 문제를 비트마다 재동기화로 푸는 방법).
