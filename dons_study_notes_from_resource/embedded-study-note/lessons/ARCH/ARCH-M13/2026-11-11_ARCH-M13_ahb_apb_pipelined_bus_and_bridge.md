---
id: ARCH-M13
time: 90분
updated: 2026-11-11
---
# ARCH-M13 AHB/APB 개념: 파이프라인 버스와 브리지

## 학습 목표

- AHB-Lite(매니저 1개)를 **사이클 단위** 로 모델링해, 전송 N+1 의 주소 단계(address phase)가 전송 N 의 데이터 단계(data phase)와 겹치는 것, `HREADY=0` 이 데이터 단계를 늘리고 **다음 주소 단계를 붙잡는** 것을 파형으로 보인다.
- `HTRANS` 4가지(IDLE / BUSY / NONSEQ / SEQ)의 의미를 말하고, IDLE·BUSY 의 데이터 단계가 왜 "0 wait OKAY" 여야 하는지 설명한다.
- APB(APB3/4, `PREADY` 있음)의 IDLE → SETUP → ACCESS 상태기계를 구현하고, AHB→APB 브리지가 `HREADYOUT=0` 으로 AHB 를 세우는 동안 APB 가 어떻게 움직이는지 파형으로 읽는다.
- 브리지를 거친 레지스터 접근 지연을 계산한다: 0 wait AHB 쓰기 N 개 = N+1 사이클, 같은 것을 APB 로 = 1 + N(2+w) (통과형), PCLK 분주와 응답 레지스터가 붙으면 `div·(2+w+reg)` 주기 + **위상에 따라 달라지는** 첫 지연.
- 사이클 시뮬레이터와 같은 답을 내는 트랜잭션 단위 예측식(TLM)을 만들고, "ISR 끝에서 플래그를 지웠는데 다시 들어온다" 를 브리지 지연으로 설명한다.

## 1. 문제

"AHB 가 파이프라인이라는 게 무슨 뜻이죠?", "APB 레지스터 하나 읽는 데 몇 사이클 걸려요?", "GPIO 를 왜 AHB 에 붙였을까요?" — MCU 펌웨어와 실리콘 검증(버스 bring-up, 파형 디버그, 성능 모델) 면접에서 바로 나오는 질문이고, 답은 **파형 한 장** 을 머릿속에 그릴 수 있느냐로 갈린다.

ARCH-B12 에서 "주소 단계 1 + 데이터 단계(1 + WS)" 와 파이프라인 공식 `1 + Σ(1+WS)` 를 만들었다. 이번엔 실제 AMBA 신호 이름으로 한 단계 내려간다.

- **AHB-Lite** (Arm IHI 0033) — 고성능 쪽 버스. 매니저(CPU, DMA)가 `HADDR/HTRANS/HWRITE/HSIZE` 를 주소 단계에 내고, 다음 사이클부터 데이터 단계에서 `HWDATA`(쓰기) 또는 `HRDATA`(읽기)가 오간다. 슬레이브는 `HREADYOUT=0` 으로 데이터 단계를 늘린다. 인터커넥트는 지금 데이터 단계에 있는 슬레이브의 `HREADYOUT` 을 골라 `HREADY` 로 **모두에게** 돌린다 — 그래서 HREADY=0 이면 이미 버스에 떠 있는 다음 주소도 그대로 멈춘다.
- **APB** (Arm IHI 0024) — 저속 주변장치(UART, 타이머, I2C 레지스터)용. 파이프라인이 없고, 전송마다 SETUP(PSEL=1, PENABLE=0) 1 사이클 + ACCESS(PENABLE=1) 가 PREADY=1 일 때까지. 단순·저전력·작은 면적이 목적.
- **AHB→APB 브리지** — AHB 쪽에서는 슬레이브 하나, APB 쪽에서는 유일한 요청자. APB 접근이 끝날 때까지 AHB 에 `HREADYOUT=0` 을 낸다.

```svg
<svg viewBox="0 0 720 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">AHB-Lite 파이프라인: SRAM 1 WS 에 W0 → R1(같은 주소) → W2 (demo_bus wave [1] 과 같은 파형)</text>
  <g fill="currentColor"><text x="10" y="60">HCLK</text><text x="10" y="96">HADDR</text><text x="10" y="136">HWDATA</text><text x="10" y="176">HRDATA</text><text x="10" y="216">HREADY</text></g>
  <path d="M90 64 H110 v-20 h42 v20 h42 v-20 h42 v20 h42 v-20 h42 v20 h42 v-20 h42 v20 h42 v-20 h42 v20 h42 v-20 h42 v20 h42 v-20 h42 v20 h42" stroke="currentColor" fill="none" stroke-width="1.5"/>
  <g stroke="currentColor" opacity=".35" stroke-dasharray="3 4"><path d="M110 40 V232"/><path d="M194 40 V232"/><path d="M278 40 V232"/><path d="M362 40 V232"/><path d="M446 40 V232"/><path d="M530 40 V232"/><path d="M614 40 V232"/><path d="M698 40 V232"/></g>
  <rect x="110" y="80" width="84" height="24" class="f-acc" stroke="currentColor"/>
  <rect x="194" y="80" width="168" height="24" class="f-mut" stroke="currentColor"/>
  <rect x="362" y="80" width="168" height="24" class="f-mut" stroke="currentColor"/>
  <rect x="194" y="120" width="168" height="24" class="f-ok" stroke="currentColor"/>
  <rect x="530" y="120" width="168" height="24" class="f-ok" stroke="currentColor"/>
  <rect x="446" y="160" width="84" height="24" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="152" y="97">W S010</text><text x="278" y="97">R S010 (붙잡힘)</text><text x="446" y="97">W S014 (붙잡힘)</text><text x="614" y="97">IDLE</text>
    <text x="278" y="137">1234 (유지)</text><text x="614" y="137">BEEF (유지)</text><text x="488" y="177">1234</text>
  </g>
  <rect x="194" y="202" width="84" height="22" class="f-warn" opacity=".5"/><rect x="362" y="202" width="84" height="22" class="f-warn" opacity=".5"/><rect x="530" y="202" width="84" height="22" class="f-warn" opacity=".5"/>
  <path d="M110 204 H194 V222 H278 V204 H362 V222 H446 V204 H530 V222 H614 V204 H698" stroke="currentColor" fill="none" stroke-width="2"/>
  <g fill="currentColor" text-anchor="middle" font-size="12" opacity=".85">
    <text x="152" y="248">c0</text><text x="236" y="248">c1</text><text x="320" y="248">c2</text><text x="404" y="248">c3</text><text x="488" y="248">c4</text><text x="572" y="248">c5</text><text x="656" y="248">c6</text>
  </g>
  <text x="10" y="272" fill="currentColor" font-size="12">주소 단계 A1 은 c1 에 나왔지만 W0 의 데이터 단계가 HREADY=0 이라 c2 끝에야 받아들여진다. HWDATA 는 데이터 단계 내내 유지.</text>
  <text x="10" y="292" fill="currentColor" font-size="12">읽기 데이터 1234 는 R1 데이터 단계의 HREADY=1 사이클(c4) 끝에 잡힌다. 총 7 = 1 + 3 × (1 + 1 WS).</text>
</svg>
```

체크 테스트는 전부 결정적(사이클 수 · 파형 문자열 · 고정 시드 xorshift 무작위 600 시퀀스)이다. 이 레슨의 범위: 단일 매니저 AHB-Lite + APB3/4 + 브리지. 주소 디코드·bus fault 는 ARCH-B11, WS 산수·플래시 프리페치는 ARCH-B12, AXI 의 VALID/READY 와 outstanding 은 ARCH-M14, 여러 매니저 중재는 ARCH-A10, 폭·프로토콜 브리지 일반론은 SOCX-B06(다음 단계), 이 규칙을 RTL assertion 으로 쓰는 것은 RTLDV-A04(다음 단계).

### Part A — AHB-Lite 사이클 모델

입력은 매니저가 내는 **주소 단계 슬롯** 목록 `ahb_req_t {htrans, hwrite, haddr, wdata}` (HSIZE 는 워드 고정). 슬레이브: `SRAM 0x2000_0000` (wait 설정값), `APB 0x4000_0000` (브리지 뒤).

- `sim_run(cfg, rq, n, wave, cap, rd)` — 사이클 루프. 매 사이클 ① 데이터 단계가 HREADY 를 정한다(IDLE/BUSY 의 데이터 단계는 0 wait, SRAM 은 `sram_ws` 사이클 HREADY=0, APB 는 브리지의 HREADYOUT) ② 주소 버스에는 아직 받아들여지지 않은 첫 슬롯이 떠 있다 ③ HREADY=1 인 사이클 끝에 데이터 단계가 끝나고 주소 단계가 받아들여진다. 사이클마다 `cyc_t` (HTRANS, HADDR, HWRITE, HWDATA, HRDATA, HREADY, PSEL…)를 기록.
- 텍스트 파형 `wave_format()` 은 주어진다(사이클 열 × 신호 행). 테스트는 파형을 **글자 그대로** 비교한다.
- 테스트: 0 WS 쓰기 4개 = 5 사이클 파형, `1 + N(1+k)` (N 1..64, k 0..3), 1 WS 에서 주소·HWDATA 유지, IDLE 슬롯, INCR4 + BUSY 버스트, 미매핑 NONSEQ = -1 (미매핑 IDLE 은 OK).

### Part B — APB 상태기계와 브리지

- `apb_next(state, req, pready)` — IDLE/SETUP/ACCESS 전이 (전수 12가지 테스트).
- `bridge_comb()` (사이클 동안: APB 신호와 HREADYOUT) / `bridge_edge()` (상승 엣지: 요청 받기, PCLK 엣지면 상태 전이와 레지스터 읽기·쓰기).
- 두 가지 브리지: **통과형** (PREADY → HREADYOUT, PRDATA → HRDATA 를 그대로) / **응답 레지스터형** (`reg_resp=1`, 완료를 플롭에 한 번 담고 다음 사이클 응답).
- 테스트: APB 쓰기 1개 = 3 사이클, W·W·R + PREADY 1 wait 파형 글자 비교, APB 규칙(PENABLE ⇒ PSEL, ACCESS 앞엔 같은 주소의 SETUP, wait 중 PADDR/PWDATA 고정), 연속 N 개 `1 + N(2+w+reg)`, SRAM/APB 섞기, 쓰고 되읽기.

### Part C — PCLK 분주, 예측식, 드라이버 시나리오

- `pclk_div` (PCLK = HCLK/div, APB 는 PCLK 엣지에서만 움직임): 단일 쓰기 /2 = 6, /4 = 12 사이클. 앞에 IDLE 이 있어도 같은 6 — 경계 대기에 흡수된다.
- `tlm_cycles(cfg, rq, n)` — 사이클 루프 없이 데이터 단계 길이만 더하는 예측식. 무작위 600 시퀀스에서 `sim_run` 과 **정확히** 같아야 한다.
- `apb_period(cfg)` = 연속 APB 접근 주기. sim 의 기울기(c9 − c8)와 비교.
- 시나리오: 주변장치 초기화(레지스터 8개 쓰기)를 AHB/APB /1 /2 /4 로, ISR 끝의 "플래그 클리어 + 되읽기" 에서 되읽기 SETUP 이 쓰기 완료 뒤에 오는지.

## 2. Problem (English)

> **AHB-Lite / APB cycle model with an AHB-to-APB bridge.**
>
> 1. Model a single-manager AHB-Lite bus cycle by cycle. The manager presents a list of address-phase slots (`HTRANS` ∈ {IDLE, BUSY, NONSEQ, SEQ}, `HADDR`, `HWRITE`, write data). The address phase of transfer *i+1* overlaps the data phase of transfer *i*; a slot is accepted at the end of a cycle with `HREADY=1`. IDLE/BUSY data phases are zero-wait. An SRAM slave inserts `sram_ws` wait cycles. Record per-cycle signals and print an ASCII waveform (one row per signal, one column per cycle).
> 2. Add an APB (v3/v4 with `PREADY`) bridge for `0x4000_0000`: SETUP (`PSEL=1, PENABLE=0`) for one PCLK, then ACCESS (`PENABLE=1`) until `PREADY=1`. The bridge holds `HREADYOUT=0` until the APB access completes (pass-through) or one cycle later (registered response).
> 3. Support `PCLK = HCLK/div`. Write a transaction-level estimator that returns the same total cycle count as the cycle model for any request list, and the steady-state cycles per back-to-back APB access.
>
> Examples: 4 back-to-back zero-wait AHB writes → 5 cycles. One APB write (div 1, no wait) → 3 cycles; *N* back-to-back → `1 + N(2 + w + reg)`. One APB write with `PCLK = HCLK/4` → 12 cycles; steady state `div·(2 + w + reg)`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨져 있을 때
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st
```

실제로 돌린 결과 (처음엔 대부분 FAIL, exit 1. 파형 비교 줄은 길어서 생략):

```text
FAIL starter.c:220  c == 5
FAIL starter.c:231  strcmp(g_txt, "cycle  |    0|    1|    2|    3|    4|\n" ...) == 0
FAIL starter.c:232  g_sram[0] == 0xA0u && g_sram[3] == 0xA3u
...
FAIL starter.c:473  c == tlm_cycles(&cf, g_rq, 2) && c == 13 && g_rd[1] == 0x0u
FAIL starter.c:479  write_commit == 5 && read_setup == 8 && read_setup > write_commit
4/39 passed
```

TODO 는 6곳: `sim_run` (Part A), `apb_next`, `bridge_comb`, `bridge_edge` (Part B), `tlm_cycles`, `apb_period` (Part C). 메모리 맵, 브리지 구조체, 파형 포매터, 하네스는 주어진다.

## 4. 힌트

<details>
<summary>힌트 1 — "이번 사이클" 과 "엣지" 를 나눠라</summary>

한 사이클 안에서 먼저 **조합 값** 을 정한다: 데이터 단계에 누가 있고 HREADY 가 몇인지, 주소 버스에 누가 있는지, APB 신호가 뭔지. 그 다음 **엣지** 에서 상태를 바꾼다: HREADY=1 이면 데이터 단계 끝 + 주소 단계 받아들이기, 브리지는 요청을 받고 PCLK 엣지면 APB 상태 전이. ARCH-B06 의 2단계(two-phase) 갱신과 같은 원리다 — 섞어서 쓰면 한 사이클 일찍 움직이는 버그가 난다.

</details>

<details>
<summary>힌트 2 — sim_run 의 상태는 네 개면 된다</summary>

`ai`(아직 받아들여지지 않은 첫 슬롯), `dp`(데이터 단계에 있는 슬롯, 없으면 -1), `dkind`(빈 단계 / SRAM / 브리지), `rem`(SRAM 남은 wait). 루프 조건은 `ai < n || dp >= 0`. 슬롯을 받아들일 때 IDLE/BUSY 면 빈 단계, 아니면 `region_of` 로 SRAM/APB/미매핑을 가른다.

</details>

<details>
<summary>힌트 3 — 브리지의 HREADYOUT 은 언제 1 인가</summary>

통과형: 상태가 ACCESS 이고 PREADY=1 이고 **이번 사이클 끝이 PCLK 엣지** 일 때만. (PCLK 가 느리면 PREADY=1 이 여러 HCLK 사이클 보이지만 APB 가 그것을 샘플하는 건 PCLK 엣지 한 번이다.) 응답 레지스터형: ACCESS 가 PREADY=1 로 끝난 엣지에서 `resp=1` 을 세우고, 다음 사이클에 HREADYOUT=1.

</details>

<details>
<summary>힌트 4 — 같은 엣지에서 일어나는 두 가지</summary>

통과형 브리지에서 ACCESS 가 끝나는 엣지 = AHB 가 다음 주소 단계를 받아들이는 엣지다. 그래서 `bridge_edge` 는 **먼저 새 요청을 `nxt` 에 받아 두고**, 그 다음 `apb_next(ACCESS, pend=1, pready=1) = SETUP` 으로 바로 다음 SETUP 에 들어간다. 이 순서를 뒤집으면 APB 에 IDLE 사이클이 하나 끼어 연속 접근이 `2+w` 가 아니라 `3+w` 가 된다. 끝난 쓰기를 레지스터에 반영하는 것도 `cur = nxt` 보다 먼저.

</details>

<details>
<summary>힌트 5 — TLM 식은 "데이터 단계가 언제 시작해서 언제 끝나나" 하나</summary>

`end` = 직전 데이터 단계가 끝난 사이클(첫 주소 단계는 cycle 0 이니 `end = 0` 으로 시작). 다음 데이터 단계는 `s = end + 1` 에 시작. APB 는 `s` 이후 첫 PCLK 경계 `b = ⌈s/div⌉·div` 에서 SETUP, 끝 `b + div(2+w) − 1`, 응답 레지스터면 +1. 총 사이클 = 마지막 `end + 1`.

</details>

## 5. 풀이

### Part A — 주소 단계와 데이터 단계의 겹침

사이클 루프의 뼈대 (정답 코드를 줄여 옮김):

```c
while (ai < n || dp >= 0) {
    int bready = bridge_comb(&br, cfg, c, &cy);          /* APB 신호 + 브리지 HREADYOUT */
    int hready = 1;
    if (dp >= 0 && dkind != D_EMPTY) {                   /* IDLE/BUSY 데이터 단계 = 0 wait */
        if (dkind == D_SRAM) { if (rem > 0) { hready = 0; rem--; } else /* 읽기/쓰기 완료 */ ... }
        else hready = bready;
    }
    if (ai < n) cy.a_idx = ai, cy.htrans = rq[ai].htrans, ...;   /* 주소 버스엔 첫 미수락 슬롯 */
    if (hready) { dp = -1; if (ai < n) { dp = ai++; /* 종류 결정, SRAM 이면 rem = ws */ } }
    bridge_edge(&br, cfg, c, acc_apb);
    c++;
}
```

**HTRANS 정리** — 이 모델이 지키는 규칙과 스펙(IHI 0033) 근거:

| HTRANS | 값 | 뜻 | 데이터 단계 | 모델에서 |
|---|---|---|---|---|
| IDLE | 00 | 전송 없음 | 슬레이브는 **0 wait OKAY** 로 응답해야 함 | 빈 단계 1 사이클 |
| BUSY | 01 | 버스트 중간에 매니저가 쉼. 주소는 **다음** 전송 것 | 0 wait OKAY, 무시 | 빈 단계 1 사이클 |
| NONSEQ | 10 | 단일 전송 또는 버스트 첫 전송 | 정상 | SRAM / 브리지 |
| SEQ | 11 | 버스트의 나머지. 주소 = 앞 + 크기 | 정상 | SRAM / 브리지 (SEQ 를 활용하는 슬레이브는 추가 연습 3) |

`demo_bus wave` [1] — 그림 1 과 같은 상황 (SRAM 1 WS):

```text
[1] AHB SRAM 1 WS: W, R(같은 주소), W  → 7 사이클 (TLM 예측 7)
cycle  |    0|    1|    2|    3|    4|    5|    6|
A-ph   |   A0|   A1|   A1|   A2|   A2|    -|    -|
D-ph   |    -|   D0|   D0|   D1|   D1|   D2|   D2|
HTRANS | NSEQ| NSEQ| NSEQ| NSEQ| NSEQ| IDLE| IDLE|
HADDR  | S010| S010| S010| S014| S014|    -|    -|
HWRITE |    W|    R|    R|    W|    W|    -|    -|
HWDATA |    -| 1234| 1234|    -|    -| BEEF| BEEF|
HRDATA |    -|    -|    -|    -| 1234|    -|    -|
HREADY |    1|    0|    1|    0|    1|    0|    1|
```

읽는 법 — 같은 열에 **두 전송** 이 산다:

1. c1: 주소 버스엔 A1(읽기), 데이터 버스엔 D0(쓰기 1234). HREADY=0 이니 A1 은 "아직 안 받아들여짐" → c2 에 그대로.
2. c2: D0 이 끝나는 사이클(HREADY=1). 이 엣지에서 쓰기가 SRAM 에 들어가고 A1 이 받아들여진다.
3. c4: R1 의 데이터가 1234 — 바로 앞 쓰기가 데이터 단계를 끝낸 뒤에 읽기 데이터 단계가 시작되므로 **읽기는 항상 앞 쓰기를 본다** (테스트 `g_rd[1] == 0x1234`).
4. HWRITE 행이 c1 에 이미 R 인 것에 주의 — HWRITE 는 **주소 단계** 신호다. 데이터 단계의 방향은 "한 사이클 전에 받아들여진 HWRITE" 로 정해진다. 파형을 읽을 때 가장 흔한 혼동.

공식: 0 WS 연속 N 개 = **N + 1**, k WS = `1 + N(1+k)` (테스트 N 1..64 × k 0..3). 지연(1 전송)은 2+k, 처리량은 1/(1+k) — ARCH-B12 의 결론을 실제 신호로 확인한 것.

**IDLE 과 BUSY** — 테스트 A4: `W, IDLE, W` 는 4 사이클, IDLE 의 데이터 단계(c2)는 HREADY=1·HWDATA 없음. 테스트 A5: `NSEQ a, SEQ a+4, BUSY a+8, SEQ a+8, SEQ a+C` 는 슬롯 5개 = 6 사이클, 메모리에는 4개만 쓰인다. 2 WS SRAM 이면 `1 + 4·3 + 1 = 14` — BUSY 는 wait 를 안 받는다.

**왜 IDLE 에도 0 wait OKAY 를 줘야 하나?** 매니저가 할 일이 없을 때도 주소 버스에는 무언가(HADDR 값)가 떠 있고, 디코더는 그 주소로 어떤 슬레이브를 HSEL 한다. 그 슬레이브가 IDLE 을 무시하지 않고 wait 를 걸면 **아무 일도 안 하는데 버스가 멈춘다**. 그래서 스펙이 IDLE/BUSY 에는 0 wait OKAY 를 의무로 둔다. 테스트 A6 의 "미매핑 주소의 IDLE 은 문제 없음" 도 같은 이유 — 실제 시스템의 default slave 도 IDLE 에는 OKAY, NONSEQ/SEQ 에만 ERROR 를 준다.

**대기 중 변경 예외** — 스펙은 HREADY=0 동안 주소·제어를 유지하는 것을 원칙으로 하되, 몇 가지 HTRANS 변경(예: 대기 중 IDLE → NONSEQ)을 허용하는 절이 있다. 이 모델의 매니저는 항상 그대로 둔다 — 실제 매니저를 검증할 땐 그 절을 직접 확인할 것.

### Part B — APB 상태기계와 브리지

```svg
<svg viewBox="0 0 740 380" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m13-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">APB 상태기계 (PCLK 엣지마다 apb_next) 와 시스템 안의 위치</text>
  <circle cx="120" cy="120" r="42" class="f-mut" stroke="currentColor"/>
  <circle cx="370" cy="120" r="42" class="f-acc" stroke="currentColor"/>
  <circle cx="620" cy="120" r="42" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle"><text x="120" y="117" font-weight="bold">IDLE</text><text x="370" y="117" font-weight="bold">SETUP</text><text x="620" y="117" font-weight="bold">ACCESS</text></g>
  <g fill="currentColor" text-anchor="middle" font-size="12"><text x="120" y="134">PSEL=0</text><text x="370" y="134">PSEL=1 PEN=0</text><text x="620" y="134">PSEL=1 PEN=1</text></g>
  <path d="M162 120 H326" stroke="currentColor" marker-end="url(#m13-arr)"/>
  <text x="244" y="110" fill="currentColor" text-anchor="middle" font-size="12">요청 있음</text>
  <path d="M412 120 H576" stroke="currentColor" marker-end="url(#m13-arr)"/>
  <text x="494" y="110" fill="currentColor" text-anchor="middle" font-size="12">항상 (1 PCLK)</text>
  <path d="M600 84 C 560 40, 430 40, 392 84" stroke="currentColor" fill="none" marker-end="url(#m13-arr)"/>
  <text x="496" y="46" fill="currentColor" text-anchor="middle" font-size="12">PREADY=1 &amp; 다음 요청</text>
  <path d="M600 158 C 520 230, 220 230, 142 158" stroke="currentColor" fill="none" marker-end="url(#m13-arr)"/>
  <text x="370" y="226" fill="currentColor" text-anchor="middle" font-size="12">PREADY=1 &amp; 요청 없음</text>
  <path d="M655 98 C 705 70, 715 160, 660 142" stroke="currentColor" fill="none" marker-end="url(#m13-arr)"/>
  <text x="690" y="182" fill="currentColor" text-anchor="middle" font-size="12">PREADY=0</text>
  <path d="M86 98 C 40 70, 30 160, 82 142" stroke="currentColor" fill="none" marker-end="url(#m13-arr)"/>
  <text x="40" y="182" fill="currentColor" text-anchor="middle" font-size="12">요청 없음</text>
  <g stroke="currentColor">
    <rect x="20" y="262" width="110" height="44" rx="6" class="f-acc"/><rect x="190" y="262" width="130" height="44" rx="6" class="f-mut"/>
    <rect x="380" y="242" width="110" height="34" rx="6" class="f-ok"/><rect x="380" y="292" width="150" height="34" rx="6" class="f-warn"/>
    <rect x="590" y="262" width="130" height="44" rx="6" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="75" y="282">CPU / DMA</text><text x="75" y="298">AHB 매니저</text>
    <text x="255" y="282">AHB-Lite</text><text x="255" y="298">디코더 + HREADY mux</text>
    <text x="435" y="264">SRAM (AHB)</text><text x="455" y="314">AHB→APB 브리지</text>
    <text x="655" y="282">APB: UART · TIM</text><text x="655" y="298">I2C · … 레지스터</text>
  </g>
  <g stroke="currentColor"><path d="M130 284 H186" marker-end="url(#m13-arr)"/><path d="M320 278 H376" marker-end="url(#m13-arr)"/><path d="M320 296 L376 308" marker-end="url(#m13-arr)"/><path d="M530 309 L586 290" marker-end="url(#m13-arr)"/></g>
  <text x="10" y="350" fill="currentColor" font-size="12">브리지는 AHB 쪽에선 슬레이브 하나(HREADYOUT 으로 AHB 를 세움), APB 쪽에선 유일한 요청자(PSEL·PENABLE 을 냄).</text>
  <text x="10" y="370" fill="currentColor" font-size="12">APB2(AMBA 2)에는 PREADY 가 없어 ACCESS 가 항상 1 사이클. PREADY·PSLVERR 은 APB3, PPROT·PSTRB 는 APB4 에서 추가.</text>
</svg>
```

`apb_next` 는 그림의 화살표를 그대로 옮긴 것이다. 중요한 점 두 가지:

- **SETUP 은 정확히 1 PCLK.** PREADY 를 보지 않는다. 그래서 APB 전송은 최소 2 PCLK (APB2 는 정확히 2).
- **ACCESS → SETUP 직행.** 연속 전송이면 PSEL 은 계속 1, PENABLE 만 1→0→1 로 토글된다. IDLE 을 거치지 않는다.

브리지 타이밍 (`demo_bus wave` [2], 통과형, PREADY 1 wait, W·W·R):

```text
[2] AHB→APB 브리지 (통과형), PREADY 1 wait: W, W, R  → 10 사이클 (TLM 예측 10)
cycle  |    0|    1|    2|    3|    4|    5|    6|    7|    8|    9|
A-ph   |   A0|   A1|   A1|   A1|   A2|   A2|   A2|    -|    -|    -|
D-ph   |    -|   D0|   D0|   D0|   D1|   D1|   D1|   D2|   D2|   D2|
HTRANS | NSEQ| NSEQ| NSEQ| NSEQ| NSEQ| NSEQ| NSEQ| IDLE| IDLE| IDLE|
HADDR  | P000| P008| P008| P008| P004| P004| P004|    -|    -|    -|
HWRITE |    W|    W|    W|    W|    R|    R|    R|    -|    -|    -|
HWDATA |    -| 0011| 0011| 0011| 0022| 0022| 0022|    -|    -|    -|
HRDATA |    -|    -|    -|    -|    -|    -|    -|    -|    -| C0DE|
HREADY |    1|    0|    0|    1|    0|    0|    1|    0|    0|    1|
PSEL   |    0|    1|    1|    1|    1|    1|    1|    1|    1|    1|
PENABLE|    0|    0|    1|    1|    0|    1|    1|    0|    1|    1|
PADDR  |    -| P000| P000| P000| P008| P008| P008| P004| P004| P004|
PWRITE |    -|    W|    W|    W|    W|    W|    W|    R|    R|    R|
PWDATA |    -| 0011| 0011| 0011| 0022| 0022| 0022|    -|    -|    -|
PRDATA |    -|    -|    -|    -|    -|    -|    -|    -|    -| C0DE|
PREADY |    -|    -|    0|    1|    -|    0|    1|    -|    0|    1|
```

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">브리지 통과형: AHB HREADY 와 APB PSEL/PENABLE/PREADY (위 표 [2] 를 그린 것)</text>
  <g fill="currentColor"><text x="10" y="54">HCLK</text><text x="10" y="88">HADDR</text><text x="10" y="132">HREADY</text><text x="10" y="172">PSEL</text><text x="10" y="212">PENABLE</text><text x="10" y="248">PREADY</text></g>
  <path d="M90 56 H110 v-16 h30 v16 h30 v-16 h30 v16 h30 v-16 h30 v16 h30 v-16 h30 v16 h30 v-16 h30 v16 h30 v-16 h30 v16 h30 v-16 h30 v16 h30 v-16 h30 v16 h30 v-16 h30 v16 h30 v-16 h30 v16 h30" stroke="currentColor" fill="none" stroke-width="1.5"/>
  <g stroke="currentColor" opacity=".3" stroke-dasharray="3 4"><path d="M170 36 V262"/><path d="M290 36 V262"/><path d="M350 36 V262"/><path d="M470 36 V262"/><path d="M530 36 V262"/><path d="M650 36 V262"/></g>
  <rect x="110" y="74" width="60" height="22" class="f-acc" stroke="currentColor"/>
  <rect x="170" y="74" width="180" height="22" class="f-mut" stroke="currentColor"/>
  <rect x="350" y="74" width="180" height="22" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-size="12"><text x="140" y="90">W P000</text><text x="260" y="90">W P008 (붙잡힘)</text><text x="440" y="90">R P004 (붙잡힘)</text><text x="620" y="90">IDLE</text></g>
  <path d="M110 118 H170 V138 H290 V118 H350 V138 H470 V118 H530 V138 H650 V118 H710" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M110 178 H170 V158 H710" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M110 218 H230 V198 H350 V218 H410 V198 H530 V218 H590 V198 H710" stroke="currentColor" fill="none" stroke-width="2"/>
  <g stroke="currentColor">
    <rect x="230" y="234" width="60" height="22" class="f-warn"/><rect x="290" y="234" width="60" height="22" class="f-ok"/>
    <rect x="410" y="234" width="60" height="22" class="f-warn"/><rect x="470" y="234" width="60" height="22" class="f-ok"/>
    <rect x="590" y="234" width="60" height="22" class="f-warn"/><rect x="650" y="234" width="60" height="22" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="260" y="250">0</text><text x="320" y="250">1</text><text x="440" y="250">0</text><text x="500" y="250">1</text><text x="620" y="250">0</text><text x="680" y="250">1</text>
    <text x="200" y="280">SETUP</text><text x="290" y="280">ACCESS</text><text x="380" y="280">SETUP</text><text x="470" y="280">ACCESS</text><text x="560" y="280">SETUP</text><text x="650" y="280">ACCESS</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12" opacity=".8">
    <text x="140" y="300">c0</text><text x="200" y="300">c1</text><text x="260" y="300">c2</text><text x="320" y="300">c3</text><text x="380" y="300">c4</text><text x="440" y="300">c5</text><text x="500" y="300">c6</text><text x="560" y="300">c7</text><text x="620" y="300">c8</text><text x="680" y="300">c9</text>
  </g>
  <text x="10" y="328" fill="currentColor" font-size="12">HREADY=1 은 PREADY=1 인 ACCESS 사이클에만(통과형). 그 엣지에서 다음 주소가 받아들여져 바로 다음 SETUP — PSEL 은 내려가지 않는다.</text>
  <text x="10" y="348" fill="currentColor" font-size="12">전송 하나 = AHB 주소 단계 위에 SETUP 1 + ACCESS (1 + w). 연속이면 접근당 2 + w 사이클.</text>
</svg>
```

계산 (div 1, 통과형):

| 접근 | 0 wait | PREADY w wait | 응답 레지스터형 |
|---|---|---|---|
| 1회 지연 (주소 단계부터) | 3 | 3 + w | 4 + w |
| 연속 N 개 | 1 + 2N | 1 + N(2+w) | 1 + N(3+w) |
| 같은 것을 AHB SRAM 0 WS | 2 / N + 1 | — | — |

테스트 B5 가 N 1..16 × w 0..3 × reg 0/1 전부를 확인한다. **APB 레지스터 연속 쓰기는 AHB 의 정확히 절반 이하 속도** 다 — 0 wait 라도 SETUP 1 사이클을 숨길 방법이 없기 때문(파이프라인 없음).

**응답 레지스터형이 왜 있나?** 통과형은 `PREADY → HREADYOUT → HREADY mux → 모든 AHB 매니저·슬레이브` 로 이어지는 조합 경로가 길어진다. 주변장치의 PREADY 가 그 주변장치 안의 로직에서 늦게 나오면 AHB 쪽 타이밍(ARCH-B13 의 setup slack)을 깎는다. 플롭 하나로 끊으면 Fmax 는 지키고 접근당 1 사이클을 낸다 — 면적·타이밍 vs 지연의 전형적인 교환. 테스트 B6 의 파형에서 응답 사이클(c3)은 PSEL=0 이다: 다음 요청을 이 사이클 끝에야 받으므로 APB 쪽에 IDLE 이 한 칸 끼고, PCLK 가 느리면 이 한 칸이 **PCLK 한 주기 전체** 로 커진다 (Part C).

실제 IP 예: Arm CMSDK 의 AHB-to-APB 브리지는 읽기/쓰기 데이터를 레지스터로 끊을지 고르는 파라미터가 있다고 알려져 있다. 정확한 파라미터 이름과 사이클 수는 그 IP 문서(또는 RTL)로 확인할 것 — 이 레슨의 두 모드는 "가장 단순한 두 극단" 이다.

### Part C — PCLK 분주, 위상, 예측식

MCU 는 보통 APB 를 HCLK 보다 느리게 돌린다 (STM32 의 APB1/APB2 프리스케일러, BM-B10 클럭 트리). 모델: PCLK 엣지 = `(c+1) % div == 0` 인 HCLK 사이클의 끝. APB 상태와 신호는 PCLK 엣지에서만 바뀐다.

`demo_bus wave` [3] — PCLK = HCLK/2, 응답 레지스터형, ISR 끝의 "플래그 클리어 쓰기 → 같은 레지스터 되읽기":

```text
[3] PCLK = HCLK/2, 응답 레지스터형 브리지: 플래그 클리어 W 후 되읽기 R  → 13 사이클 (TLM 예측 13)
cycle  |    0|    1|    2|    3|    4|    5|    6|    7|    8|    9|   10|   11|   12|
A-ph   |   A0|   A1|   A1|   A1|   A1|   A1|   A1|    -|    -|    -|    -|    -|    -|
D-ph   |    -|   D0|   D0|   D0|   D0|   D0|   D0|   D1|   D1|   D1|   D1|   D1|   D1|
HTRANS | NSEQ| NSEQ| NSEQ| NSEQ| NSEQ| NSEQ| NSEQ| IDLE| IDLE| IDLE| IDLE| IDLE| IDLE|
HADDR  | P010| P010| P010| P010| P010| P010| P010|    -|    -|    -|    -|    -|    -|
HWRITE |    W|    R|    R|    R|    R|    R|    R|    -|    -|    -|    -|    -|    -|
HWDATA |    -| 0000| 0000| 0000| 0000| 0000| 0000|    -|    -|    -|    -|    -|    -|
HRDATA |    -|    -|    -|    -|    -|    -|    -|    -|    -|    -|    -|    -| 0000|
HREADY |    1|    0|    0|    0|    0|    0|    1|    0|    0|    0|    0|    0|    1|
PSEL   |    0|    0|    1|    1|    1|    1|    0|    0|    1|    1|    1|    1|    0|
PENABLE|    0|    0|    0|    0|    1|    1|    0|    0|    0|    0|    1|    1|    0|
PADDR  |    -|    -| P010| P010| P010| P010|    -|    -| P010| P010| P010| P010|    -|
PWRITE |    -|    -|    W|    W|    W|    W|    -|    -|    R|    R|    R|    R|    -|
PWDATA |    -|    -| 0000| 0000| 0000| 0000|    -|    -|    -|    -|    -|    -|    -|
PRDATA |    -|    -|    -|    -|    -|    -|    -|    -|    -|    -| 0000| 0000|    -|
PREADY |    -|    -|    -|    -|    1|    1|    -|    -|    -|    -|    1|    1|    -|
```

- c1: 요청은 c0 끝에 받았지만 c0 끝은 PCLK 엣지가 아니다 → PSEL=0 으로 한 사이클 **경계 대기**.
- SETUP 2 HCLK(c2–c3), ACCESS 2 HCLK(c4–c5). 쓰기는 c5 끝(PCLK 엣지)에서 주변장치에 들어간다.
- c6 응답 사이클 → 읽기 주소는 c6 끝에 받아들여지지만 c6 끝은 PCLK 엣지가 아니라 c7 이 또 대기. 읽기 SETUP 은 c8 — **쓰기가 들어간 뒤(c5)** 다. 테스트 C5 가 `write_commit == 5 && read_setup == 8` 을 확인한다.

**지연 표** (`demo_bus table`, 쓰기, 첫 주소 단계 = cycle 0):

```text
target / config              single    N=8   N=32 cyc/access
AHB SRAM 0 WS                     2      9     33          1
AHB SRAM 1 WS                     3     17     65          2
APB /1 pass  PREADY-wait 0        3     17     65          2
APB /1 pass  PREADY-wait 1        4     25     97          3
APB /1 regR  PREADY-wait 0        4     25     97          3
APB /2 pass  PREADY-wait 0        6     34    130          4
APB /2 regR  PREADY-wait 0        7     49    193          6
APB /4 pass  PREADY-wait 0       12     68    260          8
APB /4 regR  PREADY-wait 1       17    129    513         16
```

- 주기 = `div · (2 + w + reg)` (`apb_period`). /2 응답 레지스터형이 6 인 이유: 응답 1 HCLK 이 다음 SETUP 을 PCLK 경계 하나만큼 밀어 **PCLK 한 주기** 를 통째로 먹는다. /1 에서는 +1, /2 에서는 +2.
- 초기화 레지스터 8개: AHB 9 사이클 vs APB /4 에서 68 사이클 (테스트 C4). 168 MHz 급 MCU 라도 느린 APB 버스의 레지스터 접근은 수십 ns 단위다.

**위상(phase)** — 같은 APB 쓰기라도 **언제 냈느냐** 에 따라 지연이 다르다 (`demo_bus phase`):

```text
PCLK = HCLK/4, 통과형, PREADY 0. 앞에 IDLE k 개를 두고 APB 쓰기 1개
  k   총 사이클   쓰기 주소단계 cycle   지연(주소단계→완료)
  0       12            0                  12
  1       12            1                  11
  2       12            2                  10
  3       12            3                   9
  4       16            4                  12
```

지연 = `(경계까지 대기 1..div) + div(2+w)` → /4 에서 9–12 사이클. 앞의 IDLE 3개가 "공짜" 인 것도 보인다(총 12 그대로). 실리콘 검증에서 "같은 레지스터 읽기의 사이클 수가 측정마다 1–3 사이클 흔들린다" 의 정체가 이것 — 버그가 아니라 클럭 위상이다. 추가 연습 4 가 최소/최대 닫힌 식을 시뮬레이터와 대조한다.

**TLM 예측식** — `tlm_cycles` 는 사이클 루프 없이:

```c
long s = end + 1;                                       /* 이 데이터 단계가 시작하는 사이클 */
if (IDLE/BUSY) end = s;
else if (SRAM) end = s + sram_ws;
else /* APB */ { long b = (s + d - 1) / d * d;  end = b + d * (2 + apb_ws) - 1 + reg_resp; }
```

테스트 C3: 고정 시드 xorshift 로 만든 600 개 시퀀스(길이 1–24, IDLE/BUSY/SRAM R·W/APB R·W 섞기, sram_ws 0–2 × apb_ws 0–3 × div {1,2,4} × reg 0/1)에서 `sim_run == tlm_cycles`. 단일 매니저·in-order 버스에서는 "데이터 단계가 겹치지 않는다" 는 성질 때문에 이렇게 닫힌 식으로 떨어진다. AXI 처럼 outstanding 이 생기면 이 식은 깨진다 (ARCH-M14, SIM-M01 의 cycle-approximate 모델로 이어짐). 성능 아키텍트가 "사이클 정확 모델 vs 트랜잭션 모델" 을 대조하며 TLM 을 검증하는 방식이 바로 이 테스트다.

**VCD 로 보기** — `./demo vcd` 는 같은 모델로 `/tmp/esn_ARCH-M13/m13.vcd` 를 쓴다 (HCLK 10 ns, don't care 는 `x`). Surfer(웹/데스크톱)나 GTKWave 로 열면 그림처럼 보인다. 이 Mac 엔 두 뷰어가 없어서 직접 열어 보진 못했고, 대신 헤더와 값 줄 문법을 작은 파이썬 검사로 확인했다(변수 14개, 값 변화 81개, 잘못된 줄 0).

```text
wrote /tmp/esn_ARCH-M13/m13.vcd: 15 cycles, 14 signals, 81 value changes (Surfer / GTKWave 로 열기)
...  (파형 표 생략 — 아래는 head /tmp/esn_ARCH-M13/m13.vcd)
$date 2026-11-11 $end
$version ARCH-M13 demo_bus (model, not RTL) $end
$timescale 1ns $end
$scope module ahb_apb $end
$var wire 1 ! HCLK $end
$var wire 2 " HTRANS $end
$var wire 32 # HADDR $end
...
#0
$dumpvars
1!
b10 "
b100000000000000000000000000000 #
```

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
# 39/39 passed   (exit 0)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san && ./san
# 39/39 passed   (ASan/UBSan 경고 없음)
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bus.c -o demo && ./demo wave   # table | phase | vcd [경로] | (없으면 전부)
```

`demo_bus.c` 는 `#define main solution_main` 후 `solution.c` 를 include 해서 **정답 모델 그대로** 파형·표·VCD 를 낸다 (체크 없음).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| HREADY=0 인데 다음 주소로 넘어감 (`ai++` 를 매 사이클) | 주소 단계는 HREADY=1 인 엣지에서만 끝난다. 슬레이브는 엉뚱한 주소를 디코드 | `if (hready) { … ai++; }` 안에서만 수락 |
| 데이터 방향을 지금 주소 버스의 HWRITE 로 판단 | HWRITE 는 주소 단계 신호. 같은 열의 데이터는 **앞 전송** 것 | 받아들일 때 `dp = ai` 로 요청을 통째로 기억 |
| IDLE 슬롯에 SRAM wait 를 적용 | IDLE/BUSY 는 0 wait OKAY 가 스펙. wait 를 주면 아무 일 없이 버스가 멈춤 | `dkind = D_EMPTY` |
| BUSY 의 주소를 "현재" 주소로 냄 | BUSY 는 버스트의 **다음** 전송 주소를 낸다 | A5 테스트: `BUSY a+8` 다음 `SEQ a+8` |
| 브리지 엣지에서 `cur = nxt` 를 레지스터 쓰기보다 먼저 | 끝나는 쓰기가 다음 전송 주소에 들어간다 | 완료 처리 → `apb_next` → SETUP 진입 시 `cur = nxt` |
| ACCESS 끝나면 무조건 IDLE | 연속 전송이 SETUP 으로 못 가고 IDLE 한 칸 → 주기 3+w | `apb_next(ACCESS, req, 1) = req ? SETUP : IDLE` |
| PCLK 분주 때 HREADYOUT 을 PREADY 와 같게 | PREADY=1 이 여러 HCLK 보이는 동안 매 사이클 "완료" → 전송이 여러 번 끝남 | `&& pclk_edge(cfg, c)` |
| 지연을 "APB = 2 사이클" 로 외움 | AHB 주소 단계 + 경계 대기 + div 배 + 응답 레지스터가 붙는다. /4 면 9–12 | `tlm_cycles` 로 계산 |

## 8. 면접 Q&A

<details>
<summary>Q1. AHB 가 "파이프라인 버스" 라는 건 정확히 무엇이 겹친다는 뜻인가요?</summary>

전송 N+1 의 **주소 단계** 가 전송 N 의 **데이터 단계** 와 같은 사이클에 있다. 그래서 0 wait 슬레이브에 연속 N 개 = N+1 사이클(처리량 1/사이클)이지만, 전송 하나의 지연은 여전히 2 사이클이다. 데이터 단계가 HREADY=0 으로 늘어나면 이미 나와 있는 다음 주소도 받아들여지지 않고 그대로 붙잡혀 있다.

꼬리질문: "그럼 HREADY 는 누가 내고 누가 보나요?" → 데이터 단계에 있는 슬레이브가 `HREADYOUT` 을 내고, 인터커넥트의 mux 가 그것을 `HREADY` 로 매니저와 **모든 슬레이브** 에 돌린다. 슬레이브도 HREADY 를 봐야 하는 이유: 주소 단계에서 자기가 선택(HSEL)됐어도 HREADY=0 이면 아직 다른 슬레이브의 데이터 단계가 진행 중이라 그 주소 단계는 "유효하게 끝난" 것이 아니기 때문이다.

</details>

<details>
<summary>Q2. HTRANS 네 가지를 설명하고, IDLE 과 BUSY 의 차이를 말해 보세요.</summary>

IDLE(00) = 전송 없음, BUSY(01) = 버스트 중간에 매니저가 잠깐 쉼(주소·제어는 다음 전송 것), NONSEQ(10) = 단일 또는 버스트 첫 전송, SEQ(11) = 버스트 나머지(주소 = 앞 + 크기). IDLE 과 BUSY 모두 슬레이브는 0 wait OKAY 로 응답하고 데이터는 무시한다. 차이는 **버스트 안이냐 밖이냐** — BUSY 는 "버스트는 계속된다, 이번 사이클만 데이터 없음" 이라 그 다음은 SEQ 가 온다.

꼬리질문: "SEQ 정보는 슬레이브에 왜 쓸모가 있나요?" → 다음 주소가 예측 가능하니 플래시 라인 버퍼·SDRAM 열린 행처럼 다음 데이터를 미리 준비할 수 있다. 추가 연습 3: NONSEQ 2 WS / SEQ 0 WS 슬레이브에서 16 워드가 싱글 49 → INCR4 25 → INCR16 19 사이클. SEQ 를 무시하는 슬레이브라면 버스트 이득은 0. 또 AHB 버스트는 1 KB 경계를 넘을 수 없다.

</details>

<details>
<summary>Q3. APB 는 왜 SETUP/ACCESS 두 단계인가요? PSEL 하나면 되지 않나요?</summary>

SETUP 사이클 동안 PADDR/PWRITE/PWDATA 가 안정되고, PENABLE 이 올라간 ACCESS 에서 주변장치가 실제로 읽고 쓴다. 주변장치 쪽은 "PSEL && PENABLE && PREADY 인 엣지에서 레지스터 갱신" 만 하면 되니 디코드 타이밍 여유가 생기고 로직이 아주 작다 — 수십 개 주변장치마다 붙는 인터페이스라 면적·전력이 중요하다. 대가는 접근당 최소 2 PCLK, 파이프라인 없음.

꼬리질문: "APB2/3/4 차이?" → APB2 는 PREADY 가 없어 항상 2 사이클. APB3 에서 PREADY(wait), PSLVERR(에러) 추가. APB4 에서 PPROT(보호 속성), PSTRB(바이트 스트로브) 추가. APB5 에는 wakeup·user 신호 등이 더 붙었다(세부는 IHI 0024 최신판 확인).

</details>

<details>
<summary>Q4. HCLK 100 MHz, APB 가 HCLK/4, 0 wait 주변장치. 레지스터 하나 쓰는 데 얼마나 걸리나요?</summary>

이 모델(통과형 브리지) 기준: 주소 단계 1 + PCLK 경계 대기 0–3 + SETUP 4 + ACCESS 4 → **9–12 HCLK = 90–120 ns**. 연속 쓰기는 접근당 8 HCLK = 80 ns. 브리지가 응답을 레지스터로 끊으면 연속 주기는 12 HCLK. 같은 쓰기가 AHB 0 WS 슬레이브면 지연 2, 연속 1 사이클.

꼬리질문: "그럼 GPIO 는 어디에 두는 게 좋죠?" → 빠른 비트 토글이 필요한 GPIO 는 AHB 에 둔다. STM32F1 은 GPIO 가 APB2 에 있었지만 F4 등 이후 세대는 AHB1 에 붙였다. Cortex-M0+ 계열 일부는 코어에 붙은 단일 사이클 I/O 포트를 따로 둔다. 비트 뱅잉 SPI 속도가 칩마다 크게 다른 이유다.

</details>

<details>
<summary>Q5. ISR 마지막 줄에서 주변장치 인터럽트 플래그를 지웠는데 ISR 이 한 번 더 들어옵니다. 왜죠?</summary>

플래그 클리어 쓰기는 브리지 → 느린 APB 를 거쳐 주변장치에 도착한다. 쓰기 버퍼나 posted-write 브리지가 있으면 CPU 입장에서는 쓰기가 "끝났는데", 실제 플래그는 수~수십 사이클 뒤에 내려간다. 그 사이 예외 복귀가 끝나면 인터럽트 선이 아직 1 이라 NVIC 가 다시 pending 으로 본다. 추가 연습 1 모델: PCLK/4 posted 에서 AHB 완료 cycle 1, APB 반영 cycle 11.

해결: ① 플래그를 ISR **앞쪽** 에서 지운다 ② 지운 뒤 같은 주변장치 레지스터를 **되읽는다** — 같은 브리지를 지나는 읽기는 앞 쓰기가 끝난 뒤에 끝나므로 순서가 보장된다(본문 C5: 되읽기 SETUP c8 > 쓰기 완료 c5) ③ DSB. 다만 DSB 는 "코어가 보는 버스 전송 완료" 까지만 보장하고, 그 뒤에 버퍼가 있는 시스템에서는 부족할 수 있어 Arm 문서(배리어 가이드 AN321 등)와 칩 벤더 노트가 되읽기를 함께 권한다 — 정확한 권고 문구는 해당 문서 확인. (C-M01 MMIO, BM-M01 NVIC 참고)

꼬리질문: "W1C 플래그면?" → 되읽기에 다른 비트를 지우는 부작용이 없는지 확인 (C-M02 W1C).

</details>

<details>
<summary>Q6. AHB 의 ERROR 응답은 왜 두 사이클인가요?</summary>

파이프라인 때문이다. 슬레이브가 에러를 내는 시점엔 매니저가 이미 **다음 전송의 주소 단계** 를 내고 있다. 첫 사이클(HRESP=ERROR, HREADY=0) 동안 매니저는 그 다음 전송을 취소(HTRANS 를 IDLE 로)할 기회를 얻고, 두 번째 사이클(HRESP=ERROR, HREADY=1)에 전송이 끝난다. 한 사이클짜리였다면 에러가 난 줄 알기도 전에 다음 전송이 받아들여진다.

꼬리질문: "APB 에러는 어떻게 CPU 까지 가나요?" → APB3 의 PSLVERR 를 브리지가 HRESP ERROR 로 바꾸고, Cortex-M 은 그것을 BusFault(PRECISERR/IMPRECISERR, BFAR) 로 받는다 (ARCH-B11). 쓰기가 버퍼링되면 imprecise.

</details>

<details>
<summary>Q7. 사이클 정확 모델과 트랜잭션 모델(TLM)은 어떻게 다르고, 어떻게 검증하나요?</summary>

사이클 모델은 매 클럭 신호를 갱신(이 레슨 `sim_run`), TLM 은 "전송이 언제 시작해 언제 끝나나" 만 계산(`tlm_cycles`). TLM 은 수천 배 빠르지만 경계 대기·응답 레지스터처럼 작은 효과를 빠뜨리기 쉽다. 그래서 같은 입력을 둘 다에 넣고 결과가 같은지 대량으로 비교한다 — 본문 C3 의 무작위 600 시퀀스가 그 축소판.

꼬리질문: "TLM 이 맞지 않는 경우는?" → outstanding·out-of-order(AXI), 여러 매니저 중재, 슬레이브 내부 상태(DRAM 행 버퍼)처럼 전송끼리 겹치거나 상태를 공유하면 단순 합산이 깨진다. 그때는 근사 모델의 오차를 측정하고 허용 범위를 정한다 (SIM-M01).

</details>

<details>
<summary>Q8. 실리콘 bring-up 에서 "APB 레지스터 읽기가 0 만 나온다/멈춘다" 면 버스 관점에서 무엇을 보나요?</summary>

① 주변장치 클럭(PCLK enable) 이 켜졌나 — 꺼진 주변장치는 0 을 읽거나 PREADY 가 영원히 0 이라 버스가 멈출 수 있다(칩 설계에 따라 다름). ② 주소 디코드: 브리지 뒤 PSEL 이 그 주변장치로 가나 (ARCH-B11). ③ 시뮬레이션/에뮬레이션 파형에서 PSEL→PENABLE→PREADY 순서, PADDR 유지(추가 연습 2 의 체커 규칙). ④ 리셋 해제 순서. 멈춤 대비로 버스 타임아웃을 두는 SoC 도 있다.

꼬리질문: "파형 없이 실칩에서?" → DWT CYCCNT 로 같은 레지스터 접근 루프를 재서 이 레슨의 표와 비교하면 PCLK 분주·브리지 모드를 거꾸로 추정할 수 있다 (BM-A10 에서 DWT 측정).

</details>

## 9. 추가 연습

### 연습 1 — posted-write 브리지와 ISR 재진입

브리지에 쓰기 1개 버퍼를 두어, 비어 있으면 APB 쓰기의 데이터 단계를 첫 사이클에 끝내는(HREADY=1) `posted_run` 을 트랜잭션 단위로 작성하라. 각 쓰기의 AHB 완료 사이클과 APB 반영(commit) 사이클을 구하고, `reentry_risk(ahb_end, commit, exit_cycles)` 로 ISR 재진입 위험을 판정하라.

<details>
<summary>답 보기</summary>

쓰기: `take = max(s, bfree)` (브리지가 받아 줄 수 있는 첫 사이클), AHB 끝 = take, SETUP = `take` 이후 PCLK 경계, commit = SETUP + div(2+w) − 1, `bfree = commit + 1`. 읽기는 posted 불가라 통과형과 같다. 결과(테스트로 확인): div 1 w 0 쓰기 1개 → AHB 끝 1, commit 2 (총 2 사이클, 통과형은 3). APB 쓰기 뒤 SRAM 쓰기 3개 (w 3) → posted 5 사이클 vs 통과형 9. 연속 APB 쓰기 8개는 posted 총 16 사이클(마지막 AHB 끝 cycle 15, commit 16) vs 통과형 17 — **지연은 숨기지만 처리량(2+w)은 못 바꾼다**. PCLK/4 에서 쓰기 후 되읽기: 쓰기 AHB 끝 1, commit 11, 읽기 끝 19 > 11 → 되읽기가 순서를 보장. ISR 복귀가 쓰기 6 사이클 뒤라면 commit 11 + 1 > 7 → 재진입 위험. 이것이 Q5 의 현상이다. 실제 브리지가 posted write 를 하는지는 IP/칩마다 다르고, AHB 의 HPROT bufferable 비트를 보고 결정하는 설계도 있다.

</details>

### 연습 2 — AHB/APB 프로토콜 체커

`cyc_t` 파형을 받아 7가지 규칙 위반을 비트마스크로 돌려주는 `check_trace` 를 작성하라: ① HREADY=0 다음 사이클에 주소·제어 변경 ② 쓰기 데이터 단계 wait 중 HWDATA 변경 ③ IDLE/BUSY 데이터 단계에 wait ④ PENABLE 인데 PSEL=0 ⑤ ACCESS 앞에 같은 주소의 SETUP 없음 ⑥ PREADY=0 ACCESS 다음에 PADDR/PWRITE/PWDATA 변경 또는 ACCESS 종료 ⑦ PREADY=1 로 끝난 PCLK 엣지 다음에도 PENABLE=1. 본문 시뮬레이터 파형(세 가지 설정)은 0, 일부러 한 칸씩 망가뜨린 파형은 해당 비트가 나와야 한다.

<details>
<summary>답 보기</summary>

규칙마다 "이번 사이클 y 와 다음 사이클 z" 를 비교하는 한 줄 조건이다. 주의점: ③ 은 IDLE 이 **받아들여진** 사이클(HREADY=1, 주소 단계에 IDLE 슬롯)의 다음 사이클을 봐야 한다. 테스트에서 위반을 심을 때도 마찬가지 — 처음엔 "대기 중인 IDLE" 다음 칸의 HREADY 를 0 으로 바꿨는데, 그 칸은 원래 APB wait 로 0 이라 아무 변화가 없었다. 위반 주입 테스트는 "정말 원래 파형과 달라졌나" 부터 확인해야 한다. ⑦ 은 PCLK 분주가 있으면 PREADY=1 이 여러 HCLK 보이므로 **PCLK 엣지 사이클** 에서만 본다. 이 규칙들은 RTL 에서 SVA 로 그대로 옮겨진다(RTLDV-A04 다음 단계). 실제 스펙은 대기 중 HTRANS 변경 예외를 몇 가지 허용하므로, 실제 매니저 검증용 체커는 ① 을 그 예외까지 반영해야 한다.

</details>

### 연습 3 — SEQ 를 아는 슬레이브와 1 KB 경계

NONSEQ 에 2 WS, SEQ 에 0 WS 인 슬레이브에서 16 워드 읽기를 싱글 / INCR4 / INCR16 으로 낼 때의 사이클을 구하라. 버스트 생성기는 1 KB 경계에서 새 NONSEQ 로 다시 시작해야 한다.

<details>
<summary>답 보기</summary>

`1 + Σ(1 + wait)`: 싱글 `1 + 16·3 = 49`, INCR4 `1 + 4·(3+1+1+1) = 25`, INCR16 `1 + 3 + 15 = 19`. SEQ 를 무시하는 슬레이브(항상 2 WS)면 셋 다 49 — 버스트는 슬레이브가 그 힌트를 쓸 때만 이득이다. `0x3F0` 부터 8 워드 INCR8 은 `0x400` 에서 NONSEQ 가 다시 나와 NONSEQ 2개. 본문 시뮬레이터(SEQ 를 모르는 SRAM 1 WS)로 돌리면 INCR4 든 싱글이든 33 사이클로 같다.

</details>

### 연습 4 — PCLK 위상과 1회 지연의 범위

주소 단계를 cycle t 에 낸 APB 접근 1개의 지연(주소 단계 ~ 완료, 양 끝 포함)의 최소·최대를 div, w 로 닫힌 식으로 쓰고, IDLE k 개를 앞에 두는 방법으로 모든 위상을 시뮬레이션해 맞춰 보라 (div 1/2/4/8 × w 0–2).

<details>
<summary>답 보기</summary>

`s = t+1`, `b = ⌈s/div⌉·div`, 지연 = `b − t + div(2+w)`. `b − t` 는 1 (t ≡ div−1) ~ div (t ≡ 0). 그래서 최소 `1 + div(2+w)`, 최대 `div(3+w)`. /4, w 0 → 9–12 (본문 phase 표). 평균은 `(1+div)/2 + div(2+w)`. 측정에서 이 폭만큼의 지터가 보이면 정상이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
# 24/24 passed   (ASan/UBSan 빌드도 24/24, 경고 없음)
```

`practice.c` 는 본문 모델을 그대로 쓰려고 `solution.c` 를 include 한다 (같은 폴더에서 빌드).

## 10. 핵심 정리

- AHB-Lite: 주소 단계(HADDR/HTRANS/HWRITE) 다음 사이클이 데이터 단계(HWDATA/HRDATA). 다음 주소가 지금 데이터와 겹친다 → 0 WS 연속 N 개 = **N+1**, 지연은 2.
- HREADY=0 → 데이터 단계가 늘고, 떠 있는 다음 주소·제어와 HWDATA 는 그대로. 주소는 HREADY=1 인 엣지에서만 받아들여진다.
- HTRANS: IDLE 00 / BUSY 01 / NONSEQ 10 / SEQ 11. IDLE·BUSY 데이터 단계는 0 wait OKAY. BUSY 는 다음 전송 주소를 낸다. 버스트는 1 KB 경계를 못 넘는다.
- APB: IDLE → SETUP(PSEL=1, PENABLE=0, 1 PCLK) → ACCESS(PENABLE=1, PREADY 까지). 연속이면 ACCESS → SETUP 직행. 최소 2 PCLK, 파이프라인 없음.
- 브리지(통과형): 1회 3 + w, 연속 `1 + N(2+w)`. 응답 레지스터형 +1. PCLK = HCLK/div 면 주기 `div(2+w+reg)`, 1회 지연은 위상에 따라 `1+div(2+w)` ~ `div(3+w)`.
- 단일 매니저 in-order 버스는 "데이터 단계 길이 합" TLM 으로 정확히 예측된다 — 사이클 모델과 대량 대조로 검증.
- 펌웨어: 느린 APB 레지스터 접근은 수십 ns. ISR 플래그는 앞에서 지우고 되읽기로 도착을 보장, 빠른 GPIO 는 AHB 에.

## 참고자료

- [AMBA AHB Protocol Specification (Arm IHI 0033)](https://developer.arm.com/documentation/ihi0033/latest) — AHB-Lite/AHB5: 주소·데이터 단계, HTRANS, HREADY/HREADYOUT, 대기 중 전송 유형 변경, 2 사이클 ERROR 응답, 1 KB 버스트 경계
- [AMBA APB Protocol Specification (Arm IHI 0024)](https://developer.arm.com/documentation/ihi0024/latest) — 상태기계(IDLE/SETUP/ACCESS), PREADY/PSLVERR(APB3), PPROT/PSTRB(APB4), APB5 추가 신호
- [Arm Cortex-M System Design Kit (CMSDK) Technical Reference Manual](https://developer.arm.com/documentation/ddi0479/latest) — AHB-to-APB 브리지 등 예제 버스 구성 요소
- [Arm AN321: Cortex-M Programming Guide to Memory Barrier Instructions](https://developer.arm.com/documentation/dai0321/latest) — 쓰기 버퍼·브리지 지연과 DSB/되읽기
- [STM32F4 Reference Manual RM0090](https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) — 버스 매트릭스, AHB1 위의 GPIO, APB1/APB2 프리스케일러
- [Surfer waveform viewer](https://surfer-project.org/) · [GTKWave](https://gtkwave.sourceforge.net/) — VCD 보기
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* — 버스 인터페이스, 쓰기 버퍼, 인터럽트 클리어 타이밍
- 관련 레슨: ARCH-B06 (2단계 갱신), ARCH-B11 (메모리 맵·bus fault), ARCH-B12 (wait state·처리량), ARCH-B13 (setup slack), ARCH-M14 (AXI), C-M01/C-M02 (MMIO·W1C), BM-B10 (클럭 트리), 다음 단계: SOCX-B06 (브리지 일반), RTLDV-A04 (SVA), SIM-M01 (cycle-approx 모델), BM-M01 (NVIC), BM-A10 (DWT 측정)
