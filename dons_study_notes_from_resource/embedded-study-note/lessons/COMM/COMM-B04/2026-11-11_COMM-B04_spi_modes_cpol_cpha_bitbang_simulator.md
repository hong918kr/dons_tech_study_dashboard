---
id: COMM-B04
time: 60분
updated: 2026-11-11
---
# COMM-B04 SPI 4가지 모드(CPOL/CPHA): 비트뱅 마스터+슬레이브 시뮬레이터

## 학습 목표

- CPOL(clock polarity) = SCLK 의 idle 레벨, CPHA(clock phase) = "몇 번째 엣지에서 샘플하나" 라는 두 줄 정의로 4모드의 **샘플 엣지 / 시프트 엣지 / 첫 비트가 나가는 시점**을 표 없이 유도한다 (`sample = leading XOR CPHA`).
- 마스터와 슬레이브가 같은 **시프트 엔진**(select / sample / drive / deselect)을 쓰는 시간 스텝 시뮬레이터를 짜고, 4모드 × 256×256 바이트 쌍 전이중 교환을 전수로 통과시킨다. LSB-first 와 CS 유지 다바이트까지.
- 모드가 틀렸을 때 생기는 **1비트 밀림**(값이 반/두 배, MSB 가 1 로 고정, 이전 바이트 LSB 가 넘어옴)을 16조합 전부 손으로 예측하고 시뮬레이션과 맞춘다. 어떤 조합이 "그냥 됨 / hold time 덕에 됨(race) / 밀림" 인지 말할 수 있다.
- SCLK 글리치, "CS 를 SCLK idle 보다 먼저 내리는" 드라이버 버그, CS 중간 해제를 주입하고, **CS 가 SPI 의 유일한 프레임 동기**라는 점을 증상으로 설명한다.
- 캡처를 디코드하는 로직 애널라이저(LA)식 디코더와 에코 슬레이브로, 전이중인데 왜 응답이 **다음 바이트**에 오는지 보인다.

## 1. 문제

SPI 는 펌웨어 bring-up 에서 가장 먼저 붙이는 버스이고, 면접에서도 "mode 0 과 mode 3 차이?", "WHO_AM_I 가 0x33 대신 0x99 로 읽혀요, 뭘 의심하죠?" 가 단골이다 — 답은 전부 **어느 엣지에서 내보내고 어느 엣지에서 읽나** 하나로 귀결된다.

SPI(Serial Peripheral Interface)는 선 4개다: 마스터가 만드는 클럭 **SCLK**, 마스터→슬레이브 데이터 **MOSI**, 슬레이브→마스터 데이터 **MISO**, 슬레이브 선택 **CS_N**(active low, NSS/SS 라고도 함). 속은 단순하다 — 양쪽에 8비트 시프트 레지스터(shift register)가 있고, 이 둘이 MOSI·MISO 로 **고리**를 이룬다. 클럭 한 번마다 한쪽 끝 비트가 상대에게 건너가므로 8클럭 뒤 두 레지스터 값이 맞바뀐다. 보내기와 받기가 늘 동시인 **전이중(full duplex)** 이고, 읽기만 하고 싶어도 무언가(더미 바이트)를 보내야 한다.

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b04ah" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <text x="10" y="18" fill="currentColor">SPI = 8비트 시프트 레지스터 두 개를 고리로 묶은 것 — 8클럭 뒤 두 값이 맞바뀐다 (전이중)</text>
  <rect x="30" y="40" width="260" height="110" class="f-acc" stroke="currentColor"/> <text x="42" y="60" fill="currentColor">마스터 (SCLK·CS_N 을 만든다)</text> <rect x="450" y="40" width="260" height="110" class="f-ok" stroke="currentColor"/> <text x="462" y="60" fill="currentColor">슬레이브</text>
  <g stroke="currentColor" fill="none"> <rect x="50" y="76" width="25" height="28"/><rect x="75" y="76" width="25" height="28"/><rect x="100" y="76" width="25" height="28"/><rect x="125" y="76" width="25" height="28"/>
  <rect x="150" y="76" width="25" height="28"/><rect x="175" y="76" width="25" height="28"/><rect x="200" y="76" width="25" height="28"/><rect x="225" y="76" width="25" height="28"/>
  <rect x="470" y="76" width="25" height="28"/><rect x="495" y="76" width="25" height="28"/><rect x="520" y="76" width="25" height="28"/><rect x="545" y="76" width="25" height="28"/>
  <rect x="570" y="76" width="25" height="28"/><rect x="595" y="76" width="25" height="28"/><rect x="620" y="76" width="25" height="28"/><rect x="645" y="76" width="25" height="28"/> </g> <g fill="currentColor" font-size="12" text-anchor="middle">
  <text x="62" y="95">b0</text><text x="87" y="95">b1</text><text x="112" y="95">b2</text><text x="137" y="95">b3</text> <text x="162" y="95">b4</text><text x="187" y="95">b5</text><text x="212" y="95">b6</text><text x="237" y="95">b7</text>
  <text x="482" y="95">b0</text><text x="507" y="95">b1</text><text x="532" y="95">b2</text><text x="557" y="95">b3</text> <text x="582" y="95">b4</text><text x="607" y="95">b5</text><text x="632" y="95">b6</text><text x="657" y="95">b7</text> </g>
  <line x1="250" y1="90" x2="468" y2="90" stroke="currentColor" stroke-width="1.6" marker-end="url(#b04ah)"/> <text x="330" y="84" fill="currentColor">MOSI (b7 먼저)</text> <path d="M670,90 H726 V170 H16 V90 H48" fill="none" stroke="currentColor" stroke-width="1.6" marker-end="url(#b04ah)"/>
  <text x="330" y="165" fill="currentColor">MISO (슬레이브의 b7 먼저)</text> <line x1="290" y1="120" x2="448" y2="120" stroke="currentColor" marker-end="url(#b04ah)"/> <text x="345" y="115" fill="currentColor" font-size="12">SCLK</text>
  <line x1="290" y1="140" x2="448" y2="140" stroke="currentColor" marker-end="url(#b04ah)"/> <text x="345" y="135" fill="currentColor" font-size="12">CS_N</text> <text x="42" y="132" class="f-mut" font-size="12">샘플: 입력 비트 → b0, 한 칸 밀기</text>
  <text x="462" y="132" class="f-mut" font-size="12">시프트: b7 → 출력 선 (양쪽 공통 규칙)</text> <text x="10" y="192" class="f-mut" font-size="12">8번의 (샘플, 시프트) 뒤: 마스터 SR = 슬레이브가 보낸 바이트, 슬레이브 SR = 마스터가 보낸 바이트. 클럭은 마스터만 만든다.</text>
</svg>
```

UART(COMM-B01~B03)와 결정적으로 다른 점: **클럭을 같이 보낸다**. 그래서 baud 오차라는 개념이 없고, 마스터가 바이트 중간에 멈췄다가 다시 클럭을 줘도 문제없다(비트뱅이 쉬운 이유). 대신 양쪽이 "어느 엣지에서 데이터를 바꾸고(시프트, drive) 어느 엣지에서 읽는지(샘플, sample)" 에 합의해야 한다. 그 합의가 **모드**다.

| 모드 | CPOL | CPHA | SCLK idle | 샘플 엣지 | 시프트(드라이브) 엣지 | 첫 비트가 선에 나오는 때 |
|---|---|---|---|---|---|---|
| 0 | 0 | 0 | low | 상승 (= leading) | 하강 | CS↓ 직후, 첫 엣지 **전** |
| 1 | 0 | 1 | low | 하강 (= trailing) | 상승 | 첫(leading) 엣지 |
| 2 | 1 | 0 | high | 하강 (= leading) | 상승 | CS↓ 직후, 첫 엣지 전 |
| 3 | 1 | 1 | high | 상승 (= trailing) | 하강 | 첫(leading) 엣지 |

leading 엣지는 idle 에서 **벗어나는** 엣지, trailing 은 idle 로 **돌아오는** 엣지다. CPHA=0 이면 leading 에서 샘플하므로 첫 비트가 첫 엣지 전에 이미 선에 있어야 한다 → CS 하강이 사실상 첫 "시프트" 역할을 한다. CPHA=1 이면 leading 에서 시프트, trailing 에서 샘플. 아래 그림은 이 레슨 시뮬레이터가 실제로 낸 파형(MOSI = 0xA5)을 그대로 그린 것이다.

```svg
<svg viewBox="0 0 740 392" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-size="13" text-anchor="start">SPI 4모드 — MOSI = 0xA5 (1010 0101), MSB first. 한 칸 = SCLK 반주기 (시뮬레이터 출력)</text> <text x="10" y="48" fill="currentColor" font-size="13" text-anchor="start">CS_N</text>
  <polyline fill="none" stroke="currentColor" stroke-width="1.6" points="150,34 179,34 179,52 208,52 237,52 266,52 295,52 324,52 353,52 382,52 411,52 440,52 469,52 498,52 527,52 556,52 585,52 614,52 643,52 672,52 701,52 701,34 730,34"/>
  <text x="182" y="65" class="f-mut" font-size="12" text-anchor="start">CS↓ (select)</text> <text x="699" y="65" class="f-mut" font-size="12" text-anchor="end">CS↑</text> <rect x="4" y="72" width="732" height="136" class="f-mut" stroke="none"/>
  <text x="10" y="88" fill="currentColor" font-size="13" text-anchor="start">CPHA = 0</text> <text x="150" y="88" class="f-mut" font-size="12" text-anchor="start">첫 비트는 CS↓ 직후 이미 선에 — 첫(leading) 엣지에서 샘플</text>
  <line x1="237" y1="92" x2="237" y2="198" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/> <line x1="295" y1="92" x2="295" y2="198" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/>
  <line x1="353" y1="92" x2="353" y2="198" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/> <line x1="411" y1="92" x2="411" y2="198" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/>
  <line x1="469" y1="92" x2="469" y2="198" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/> <line x1="527" y1="92" x2="527" y2="198" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/>
  <line x1="585" y1="92" x2="585" y2="198" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/> <line x1="643" y1="92" x2="643" y2="198" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/>
  <text x="10" y="112" fill="currentColor" font-size="13" text-anchor="start">SCLK mode 0</text>
  <polyline fill="none" stroke="currentColor" stroke-width="1.6" points="150,116 179,116 208,116 237,116 237,98 266,98 266,116 295,116 295,98 324,98 324,116 353,116 353,98 382,98 382,116 411,116 411,98 440,98 440,116 469,116 469,98 498,98 498,116 527,116 527,98 556,98 556,116 585,116 585,98 614,98 614,116 643,116 643,98 672,98 672,116 701,116 730,116"/>
  <text x="10" y="142" fill="currentColor" font-size="13" text-anchor="start">SCLK mode 2</text>
  <polyline fill="none" stroke="currentColor" stroke-width="1.6" points="150,128 179,128 208,128 237,128 237,146 266,146 266,128 295,128 295,146 324,146 324,128 353,128 353,146 382,146 382,128 411,128 411,146 440,146 440,128 469,128 469,146 498,146 498,128 527,128 527,146 556,146 556,128 585,128 585,146 614,146 614,128 643,128 643,146 672,146 672,128 701,128 730,128"/>
  <text x="10" y="172" fill="currentColor" font-size="13" text-anchor="start">MOSI</text>
  <polyline fill="none" stroke="currentColor" stroke-width="1.6" points="150,158 179,158 208,158 237,158 266,158 266,176 295,176 324,176 324,158 353,158 382,158 382,176 411,176 440,176 469,176 498,176 498,158 527,158 556,158 556,176 585,176 614,176 614,158 643,158 672,158 701,158 730,158"/>
  <text x="237" y="192" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">1</text> <text x="295" y="192" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">0</text>
  <text x="353" y="192" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">1</text> <text x="411" y="192" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">0</text>
  <text x="469" y="192" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">0</text> <text x="527" y="192" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">1</text>
  <text x="585" y="192" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">0</text> <text x="643" y="192" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">1</text> <text x="10" y="192" class="f-acc" font-size="12" text-anchor="start">샘플(점선)</text>
  <rect x="4" y="218" width="732" height="136" class="f-mut" stroke="none"/> <text x="10" y="234" fill="currentColor" font-size="13" text-anchor="start">CPHA = 1</text> <text x="150" y="234" class="f-mut" font-size="12" text-anchor="start">첫 비트는 첫(leading) 엣지에서 나감 — trailing 엣지에서 샘플</text>
  <line x1="266" y1="238" x2="266" y2="344" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/> <line x1="324" y1="238" x2="324" y2="344" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/>
  <line x1="382" y1="238" x2="382" y2="344" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/> <line x1="440" y1="238" x2="440" y2="344" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/>
  <line x1="498" y1="238" x2="498" y2="344" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/> <line x1="556" y1="238" x2="556" y2="344" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/>
  <line x1="614" y1="238" x2="614" y2="344" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/> <line x1="672" y1="238" x2="672" y2="344" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/>
  <text x="10" y="258" fill="currentColor" font-size="13" text-anchor="start">SCLK mode 1</text>
  <polyline fill="none" stroke="currentColor" stroke-width="1.6" points="150,262 179,262 208,262 237,262 237,244 266,244 266,262 295,262 295,244 324,244 324,262 353,262 353,244 382,244 382,262 411,262 411,244 440,244 440,262 469,262 469,244 498,244 498,262 527,262 527,244 556,244 556,262 585,262 585,244 614,244 614,262 643,262 643,244 672,244 672,262 701,262 730,262"/>
  <text x="10" y="288" fill="currentColor" font-size="13" text-anchor="start">SCLK mode 3</text>
  <polyline fill="none" stroke="currentColor" stroke-width="1.6" points="150,274 179,274 208,274 237,274 237,292 266,292 266,274 295,274 295,292 324,292 324,274 353,274 353,292 382,292 382,274 411,274 411,292 440,292 440,274 469,274 469,292 498,292 498,274 527,274 527,292 556,292 556,274 585,274 585,292 614,292 614,274 643,274 643,292 672,292 672,274 701,274 730,274"/>
  <text x="10" y="318" fill="currentColor" font-size="13" text-anchor="start">MOSI</text>
  <polyline fill="none" stroke="currentColor" stroke-width="1.6" points="150,304 179,304 208,304 237,304 266,304 295,304 295,322 324,322 353,322 353,304 382,304 411,304 411,322 440,322 469,322 498,322 527,322 527,304 556,304 585,304 585,322 614,322 643,322 643,304 672,304 701,304 730,304"/>
  <text x="266" y="338" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">1</text> <text x="324" y="338" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">0</text>
  <text x="382" y="338" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">1</text> <text x="440" y="338" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">0</text>
  <text x="498" y="338" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">0</text> <text x="556" y="338" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">1</text>
  <text x="614" y="338" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">0</text> <text x="672" y="338" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">1</text> <text x="10" y="338" class="f-acc" font-size="12" text-anchor="start">샘플(점선)</text>
  <text x="10" y="368" class="f-mut" font-size="12" text-anchor="start">mode 0·3 = 상승 엣지 샘플 / 하강 엣지 시프트,   mode 1·2 = 하강 엣지 샘플 / 상승 엣지 시프트.</text> <text x="10" y="386" class="f-mut" font-size="12" text-anchor="start">CPOL 은 idle 레벨만, CPHA 는 "몇 번째 엣지에서 샘플하나" 를 바꾼다.</text>
</svg>
```

**이 레슨의 모델** (`solution.c` 의 주어진 부분):

- 1 step = SCLK 반주기. `sim_step(sim, cs_n, sclk)` 를 부를 때마다 선 레벨을 한 번 갱신하고 캡처(trace)에 한 줄 남긴다.
- CS 가 내려가 있는 동안의 SCLK 엣지에서 **먼저 모두 샘플(엣지 직전 레벨), 그다음 모두 드라이브**한다. 실제 동기 회로에서 플립플롭이 클럭 엣지 순간의 값을 잡고 출력은 clock-to-output 지연 뒤에 바뀌는 것(= hold time 이 지켜짐)과 같다. 같은 엣지에서 한쪽은 샘플, 다른 쪽은 그 선을 바꾸면 **race** 로 센다.
- 아무도 몰지 않는 데이터 선(선택 안 된 슬레이브의 MISO, CPHA=1 엔진이 첫 엣지 전에 내는 값)은 1 로 본다(`LINE_RELEASED` — 풀업/hi-Z 를 단순화한 모델 선택).
- 마스터와 슬레이브는 같은 `spi_eng_t` 엔진을 쓴다. 마스터 엔진은 MOSI 를 몰고 MISO 를 샘플, 슬레이브 엔진은 반대.

### Part A — 모드 규칙, 시프트 엔진, 마스터 클럭 생성기

1. `spi_is_sample_edge(cfg, new_sclk)` — SCLK 가 `new_sclk` 로 바뀌는 엣지가 샘플 엣지인가.
2. `eng_select(e)` — CS 하강: 카운터 초기화, 첫 바이트 적재, CPHA=0 이면 첫 비트를 바로 선에.
3. `eng_sample(e, level)` — 한 비트 받아 넣기(MSB first / LSB first), 8비트면 바이트 저장(용량 넘으면 `rx_drop++`).
4. `eng_drive(e)` — 다음 비트를 선에. 8비트를 다 냈으면 다음 바이트 적재(없으면 0xFF).
5. `eng_deselect(e)` — CS 상승: 덜 찬 바이트는 버리고 `partial++`.
6. `spi_xfer(sim, cfg, tx, rx, n)` — 비트뱅 마스터: ① CS 높은 채로 SCLK 를 이 모드의 idle 로 ② CS 하강 ③ 반주기 대기 ④ 16·n 개 엣지 ⑤ CS 상승. n 이 1..64 밖이면 버스를 건드리지 않고 -1.

테스트: 모드별 샘플 엣지 8개, mode 0 파형 문자열 대조(아래 5. 풀이의 표와 같은 값), 4모드 × 65536 쌍 전수, LSB-first(양쪽 LSB 면 정상, 한쪽만이면 비트가 뒤집힌 바이트), CS 유지 다바이트 고정 시드 랜덤 4×500 회, 경계(n=0, n=65, rx=NULL).

### Part B — 모드 불일치와 버스 고장

7. `spi_predict(master_mode, slave_mode)` — 시뮬레이션 없이 **추론으로** 두 방향의 지연(0 / +1 / −1 비트)과 race 여부를 예측한다 (`link_delay` 도우미 포함).

테스트: 16조합 × 랜덤 다바이트 200회에서 예측 = 시뮬레이션(받은 바이트, race 수까지). 그리고 주입하는 고장 셋 — 코드는 주어지고 기대 증상을 assert 한다:

- **모드 불일치**: 마스터 mode 0, 슬레이브 mode 1, 둘 다 `12 34` 를 보냄 → 마스터는 `89 1A` 를 받는다.
- **SCLK 글리치**: mode 0, 3바이트 중 바이트 1 직전에 SCLK 펄스 하나 → 그 CS 구간 끝까지 계속 밀리고, CS 상승 때 슬레이브에 1비트가 남는다(`partial == 1`). 다음 CS 구간은 정상.
- **CS 를 idle 보다 먼저**: mode 0 장치를 쓴 뒤(SCLK=0) mode 3 장치를 선택하면서 CS 를 먼저 내리고 SCLK 를 1 로 → 슬레이브가 가짜 엣지를 본다. 0x80 을 썼는데 0xC0 이 들어가고 읽기는 멀쩡.
- **CS 중간 해제**: 6비트만 보내고 CS 상승 → 슬레이브는 부분 바이트를 버리고, 다음 프레임은 처음부터 정상.

### Part C — 펌웨어에서 보는 모습: 에코 슬레이브와 LA 디코더

8. `spi_decode(trace, len, cfg, out)` — 캡처(step 마다 CS/SCLK/MOSI/MISO)를 모드 설정대로 디코드하는 LA 프로토콜 디코더(HWLAB-M06 의 SPI 디코더를 C 로). CS 하강 전의 엣지는 무시(캡처가 프레임 중간에서 시작), 버퍼가 모자라면 -1.

테스트: 에코 슬레이브(다음에 보낼 바이트 = 마지막으로 받은 바이트)는 4모드 모두 `FF 11 22 33` 으로 응답 — 한 바이트 늦다. CPHA=0 엔진이 프레임 끝에서 다음 바이트를 미리 꺼내는 것 확인. 디코더: 4모드 × 3프레임 복원, 작은 버퍼 → -1, 중간 시작 캡처, 글리치 캡처(LA 는 슬레이브가 본 것을 본다), 모드를 잘못 고른 디코드.

## 2. Problem (English)

Build a deterministic, time-stepped model of a 4-wire SPI link and use it to explain mode bugs.

```c
typedef struct { uint8_t cpol, cpha, lsb_first; } spi_cfg_t;      /* mode = CPOL<<1 | CPHA */

int  spi_is_sample_edge(const spi_cfg_t *c, uint8_t new_sclk);    /* 1 = sample edge, 0 = shift edge */
void eng_select(spi_eng_t *e);      /* CS falling: reset counters, load first byte, CPHA=0 -> drive first bit now */
void eng_sample(spi_eng_t *e, uint8_t level);   /* shift one bit in; store a byte every 8 bits (bounded rx) */
void eng_drive(spi_eng_t *e);       /* put the next bit on the line; load the next byte after 8 bits */
void eng_deselect(spi_eng_t *e);    /* CS rising: drop and count a partial byte */
int  spi_xfer(spi_sim_t *s, spi_cfg_t cfg, const uint8_t *tx, uint8_t *rx, size_t n); /* bit-banged master */
spi_link_t spi_predict(unsigned master_mode, unsigned slave_mode); /* {miso_delay, mosi_delay, race} */
int  spi_decode(const spi_sample_t *tr, size_t len, spi_cfg_t cfg, spi_dec_t *out);   /* logic-analyzer decoder */
```

Constraints: no heap; every buffer has an explicit capacity and overflow is counted; one simulation step is half an SCLK period; at an SCLK edge every party samples the pre-edge level first and only then drives; an undriven data line reads 1. `spi_xfer` must set SCLK to the idle level **before** asserting CS, and must reject `n == 0 || n > 64` without touching the bus.

Examples:

```text
mode 0, master sends A5, slave sends 3C  ->  master reads 3C, slave reads A5
  MOSI at the 8 rising edges: 1,0,1,0,0,1,0,1
  CS_N -__________________-      (20 half-periods = 16*n + 4)
  SCLK ___-_-_-_-_-_-_-_-__
  MOSI ----__--____--__----
  MISO -_____--------____--
master mode 0, slave mode 1, both send 12 34  ->  master reads 89 1A (1-bit late), slave reads 12 34
spi_predict(0,1) = {miso_delay=+1, mosi_delay=0, race=1}    spi_predict(3,0) = {-1, 0, 0}
```

Follow-ups: Which of the 12 wrong combinations still "work", and why is that dangerous? What does a glitch on SCLK do if CS is tied low? Why does the reply to a command byte arrive one byte later? Two devices with different modes on one bus — what must the driver do between transfers?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여덟 군데(Part A 1~6, Part B 7a/7b, Part C 8). 버스 시뮬레이터 `sim_step`, 캡처 렌더러 `trace_row`, 기대값 생성기 `delayed_bytes`, 버그 드라이버 `spi_xfer_cs_first` 는 주어진다. 스텁은 아무 일도 안 하거나 0 을 돌려주므로 시뮬레이터 루프는 항상 끝난다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:269  spi_is_sample_edge(&(spi_cfg_t){0, 0, 0}, 1) == 1
FAIL starter.c:272  spi_is_sample_edge(&(spi_cfg_t){0, 1, 0}, 0) == 1
…
FAIL starter.c:282  mrx == 0x3C && srx == 0xA5
FAIL starter.c:283  s->tr_n == 20
…
FAIL starter.c:496  d.n == 2 && memcmp(dm, e, 2) == 0 && dm[0] == 0x89
8/93 passed
```

(exit 1. 통과한 8개는 스텁의 0 이 우연히 맞는 "시프트 엣지 = 0" 판정 4개, 주어진 거부 경로 3개(n=0, n=65, 버스 안 건드림), 그리고 모두 0 인 `spi_predict(0,0)` 한 칸.)

## 4. 힌트

<details><summary>힌트 1 — 표를 외우지 말고 한 줄로</summary>

leading 엣지 = `new_sclk != CPOL` (idle 에서 벗어남). CPHA=0 은 leading 에서 샘플, CPHA=1 은 trailing 에서 샘플. 그러니 `sample = leading XOR CPHA`. mode 0 과 3 이 둘 다 "상승 엣지 샘플" 이 되는 것도 이 식에서 바로 나온다.

</details>

<details><summary>힌트 2 — 엔진은 "몇 비트 냈나 / 몇 비트 받았나" 카운터 두 개</summary>

`out_cnt` 는 현재 바이트에서 선에 올린 비트 수, `in_cnt` 는 받은 비트 수. CPHA=0 은 select 때 이미 한 비트를 냈으므로 `out_cnt = 1` 로 시작한다. drive 는 `out_cnt == 8` 이면 다음 바이트를 꺼내 `out_cnt = 0` 으로 만든 뒤 비트를 낸다. 비트 위치는 `bitpos()` (MSB first 면 7 − k).

</details>

<details><summary>힌트 3 — 마스터는 순서가 전부다</summary>

`sim_step(s, 1, cpol)` → `sim_step(s, 0, cpol)` → `eng_select(&s->m); s->m_active = 1;` → `sim_step(s, 0, cpol)` → `16*n` 번 `sim_step(s, 0, s->sclk ^ 1)` → `s->m_active = 0; sim_step(s, 1, cpol)`. 한 바이트가 끝나면 SCLK 는 항상 idle 로 돌아와 있다(엣지 16개 = 짝수). 그래서 n 바이트 캡처 길이는 16n + 4.

</details>

<details><summary>힌트 4 — 불일치 예측은 "엣지 번호" 로</summary>

마스터 기준으로 엣지에 번호를 붙인다: CS 하강 = 0, e1(마스터의 leading), e2, …. 마스터는 CPHA=0 이면 홀수 엣지에서, CPHA=1 이면 짝수 엣지에서 샘플. 슬레이브에게 e1 은 `new_sclk = !CPOL_m` 인 엣지이므로, 홀수 엣지에서 샘플하는지는 `spi_is_sample_edge` 에 넣어 보면 `(CPOL_m ^ CPOL_s ^ CPHA_s) == 0`. 두 쪽의 홀/짝이 다르면 race. 지연은 "송신자의 첫 비트가 몇 번 엣지에 올라가고(d0) 몇 번 엣지에 사라지는지(d1)" 와 "수신자의 첫 샘플 엣지(e0)" 를 비교한다: e0 ≤ d0 이면 아직 안 올라간 값(1)을 읽음 → +1, d0 < e0 ≤ d1 이면 0, 그 뒤면 첫 비트를 놓침 → −1.

</details>

<details><summary>힌트 5 — 디코더는 "엣지 직전" 값을 읽는다</summary>

캡처의 한 줄은 그 step 이 끝난 뒤의 레벨이다. i 번째 줄에서 SCLK 가 바뀌었으면 엣지이고, 샘플 값은 `tr[i-1]` 의 MOSI/MISO. 시뮬레이터의 "샘플 먼저" 규칙과 같은 기준이어야 디코더가 슬레이브와 같은 값을 본다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 범위 확인 | 모드(CPOL/CPHA), 비트 순서(MSB first 기본), 워드 크기(8비트), CS 극성(active low), CS 를 바이트마다 올리나 유지하나, 몰지 않는 MISO 의 레벨 |
| 2. 정의 한 줄 | CPOL = idle 레벨, CPHA = 샘플이 leading(0) / trailing(1). `sample = leading XOR CPHA` |
| 3. 모델 | 시프트 레지스터 두 개의 고리 + "엣지 순간엔 샘플 먼저, 드라이브는 그 뒤" (hold time) |
| 4. 코드 | 엔진 4함수 + 클럭 생성기. 마스터·슬레이브가 같은 엔진 |
| 5. 소리 내어 말할 테스트 | 4모드 전수, 다바이트(바이트 경계의 재적재), CS 중간 해제, 모드 불일치 표 |
| 6. 비용 | step 당 O(1), 엔진 하나 수십 바이트 RAM, 힙 없음. 캡처(20 KB)는 테스트 전용 |

### Part A — 규칙: `sample = leading XOR CPHA`

| new_sclk | mode 0 (0,0) | mode 1 (0,1) | mode 2 (1,0) | mode 3 (1,1) |
|---|---|---|---|---|
| 1 (상승) | leading → **샘플** | leading → 시프트 | trailing → 시프트 | trailing → **샘플** |
| 0 (하강) | trailing → 시프트 | trailing → **샘플** | leading → **샘플** | leading → 시프트 |

그래서 mode 0·3 은 상승 샘플, mode 1·2 는 하강 샘플이다. **mode 0 과 3 의 차이는 idle 레벨과 첫 비트 시점뿐**이다 (Q3, 연습 3).

### Part A — 엔진과 한 바이트 추적

```c
static void eng_select(spi_eng_t *e) {
    e->in_cnt = 0; e->rx_sr = 0;
    e->tx_sr = eng_next_byte(e); e->out_cnt = 0;
    if (e->cfg.cpha == 0) { e->out = (uint8_t)(e->tx_sr >> bitpos(&e->cfg, 0) & 1u); e->out_cnt = 1; }
    else e->out = LINE_RELEASED;
}
static void eng_drive(spi_eng_t *e) {
    if (e->out_cnt == 8) { e->tx_sr = eng_next_byte(e); e->out_cnt = 0; }
    e->out = (uint8_t)(e->tx_sr >> bitpos(&e->cfg, e->out_cnt) & 1u);
    e->out_cnt++;
}
```

mode 0, 마스터 0xA5(1010 0101) / 슬레이브 0x3C(0011 1100). t 는 캡처 줄 번호, 레벨은 그 step 뒤의 값이다. 이 표가 테스트의 파형 문자열이고 위 그림의 mode 0 줄이다.

| t | 일어난 일 | CS_N | SCLK | MOSI | MISO | 마스터 rx_sr | 슬레이브 rx_sr |
|---|---|---|---|---|---|---|---|
| 0 | ① CS 높은 채 SCLK=idle(0) | 1 | 0 | 1 | 1 | | |
| 1 | ② CS↓ → 슬레이브 select, MISO = 0x3C 의 b7 = 0 | 0 | 0 | 1 | 0 | | |
| 2 | ③ 마스터 select, MOSI = 0xA5 의 b7 = 1 | 0 | 0 | 1 | 0 | | |
| 3 | e1 ↑ 샘플: M←0, S←1 | 0 | 1 | 1 | 0 | 0 | 1 |
| 4 | e2 ↓ 시프트: b6 (0 / 0) | 0 | 0 | 0 | 0 | | |
| 5 | e3 ↑ 샘플 | 0 | 1 | 0 | 0 | 00 | 10 |
| 6 | e4 ↓ 시프트: b5 (1 / 1) | 0 | 0 | 1 | 1 | | |
| 7 | e5 ↑ | 0 | 1 | 1 | 1 | 001 | 101 |
| 8 | e6 ↓ b4 (0 / 1) | 0 | 0 | 0 | 1 | | |
| 9 | e7 ↑ | 0 | 1 | 0 | 1 | 0011 | 1010 |
| 10 | e8 ↓ b3 (0 / 1) | 0 | 0 | 0 | 1 | | |
| 11 | e9 ↑ | 0 | 1 | 0 | 1 | 00111 | 10100 |
| 12 | e10 ↓ b2 (1 / 1) | 0 | 0 | 1 | 1 | | |
| 13 | e11 ↑ | 0 | 1 | 1 | 1 | 001111 | 101001 |
| 14 | e12 ↓ b1 (0 / 0) | 0 | 0 | 0 | 0 | | |
| 15 | e13 ↑ | 0 | 1 | 0 | 0 | 0011110 | 1010010 |
| 16 | e14 ↓ b0 (1 / 0) | 0 | 0 | 1 | 0 | | |
| 17 | e15 ↑ 8번째 샘플 → 바이트 완성 | 0 | 1 | 1 | 0 | **0x3C** | **0xA5** |
| 18 | e16 ↓ out_cnt==8 → 다음 바이트(없으면 0xFF) 의 b7 | 0 | 0 | 1 | 1 | | |
| 19 | ⑤ CS↑ → MISO 놓음 | 1 | 0 | 1 | 1 | | |

설계 이유:

- **① idle 을 CS 보다 먼저.** 선택된 슬레이브에게 SCLK 변화는 무조건 엣지다. 이전 장치가 다른 CPOL 이었다면 CS 를 내린 뒤 idle 을 바꾸는 순간 가짜 엣지가 생긴다 (Part B). Linux 의 `spi-gpio` 도 chipselect 활성화 때 클럭 레벨부터 맞추고 CS 를 움직인다(소스 기억 기반 — `drivers/spi/spi-gpio.c` 확인 권장).
- **③ 반주기 대기.** CPHA=0 에서 첫 비트는 첫 엣지 전에 setup 시간을 가져야 한다. 데이터시트의 t_CSS(CS↓ → 첫 SCLK 엣지) 가 이것 — 실제 측정은 HWLAB-M05.
- **같은 엔진을 마스터와 슬레이브가 공유.** SPI 는 대칭이다. 다른 점은 클럭을 누가 만드느냐와 어느 선을 모느냐뿐이다. 그래서 16조합 불일치 실험이 코드 수정 없이 된다.
- **e16 에서 다음 바이트를 미리 꺼낸다 (CPHA=0).** 마지막 trailing 엣지는 시프트 엣지라 다음 바이트의 MSB 를 내보내야 한다 — CS 를 유지하고 바로 다음 바이트를 보낼 때 필요하다. 대가: 슬레이브는 **마지막 샘플 후 반 클럭 안에** 다음 바이트를 준비해야 한다. 10 MHz SCLK 면 50 ns 라 CPU 로는 불가능하고, 하드웨어 SPI 슬레이브는 TX 버퍼(FIFO)에 미리 넣어 두거나 DMA 를 쓴다. 못 하면 underrun(이전 값이나 0 이 나감 — 칩마다 다름, RM 확인 권장). 그리고 거기서 CS 가 올라가면 미리 꺼낸 바이트는 사라진다 — 테스트 "mode 0/2 는 큐에서 한 바이트를 건너뛴다" 가 이것이고, 그래서 슬레이브 드라이버는 **CS 하강(또는 상승) 마다 TX 를 다시 준비(re-arm)** 한다. CPHA=1 은 다음 바이트를 첫 leading 엣지에서 꺼내므로 이 문제가 없다.
- **CS 상승 = 비트 카운터 리셋.** SPI 에 start bit 같은 동기 패턴은 없다. 몇 번째 비트인지 아는 유일한 근거는 "CS 가 내려간 뒤 몇 번째 샘플인가" 다.

### Part B — 16조합 예측

엣지에 마스터 기준 번호를 붙인다 (CS↓ = 0, e1 = 마스터의 leading, …). 마스터는 CPHA_m=0 이면 홀수, 1 이면 짝수 엣지에서 샘플한다. 슬레이브는 e1 에서 SCLK 가 `!CPOL_m` 으로 가는 것을 보므로, 홀수 엣지가 자기 샘플 엣지인지는 `((!CPOL_m) != CPOL_s) ^ CPHA_s` = `(CPOL_m ^ CPOL_s ^ CPHA_s) == 0`.

```c
static int link_delay(int tx_cpha, int tx_odd, int rx_odd) {
    int first = tx_odd ? 2 : 1;                /* 송신자의 첫 드라이브 엣지 */
    int d0 = tx_cpha == 0 ? 0 : first;         /* 첫 비트가 선에 올라가는 엣지 */
    int d1 = tx_cpha == 0 ? first : first + 2; /* 첫 비트가 사라지는 엣지 */
    int e0 = rx_odd ? 1 : 2;                   /* 수신자의 첫 샘플 엣지 (엣지 직전 값을 읽음) */
    return e0 <= d0 ? +1 : e0 <= d1 ? 0 : -1;
}
```

두 사람이 같은 홀짝에서 샘플하면, 상대는 그 반대 홀짝에서 드라이브하므로 샘플은 늘 반주기 setup 을 갖는다 — race 없음. 홀짝이 다르면 **샘플하는 엣지 = 상대가 바꾸는 엣지** 다. 이 모델은 hold 가 지켜진다고 보고 엣지 직전 값을 읽으므로 데이터가 맞을 수도 있지만, 실제 보드에선 t_hold 와 상대의 clock-to-output 지연(+배선) 싸움이다 (연습 5).

실측 (`demo_modes.c`, 256×256 쌍 + 랜덤 다바이트 2000회, 칸 = MISO 지연 / MOSI 지연 / 바이트당 race 수):

```text
(2) mode mismatch matrix: rows = master mode, cols = slave mode
    cell = MISO delay / MOSI delay / race-per-byte  (measured on all 256x256 byte pairs + 2000 random multi-byte)
  M0 |   0 /  0 / 0 |  +1 /  0 / 8 |   0 /  0 / 8 |  +1 /  0 / 0 |
  M1 |   0 / +1 / 8 |   0 /  0 / 0 |  -1 /  0 / 0 |  +1 / +1 / 8 |
  M2 |   0 /  0 / 8 |  +1 /  0 / 0 |   0 /  0 / 0 |  +1 /  0 / 8 |
  M3 |  -1 /  0 / 0 |  +1 / +1 / 8 |   0 / +1 / 8 |   0 /  0 / 0 |
  spi_predict agrees with measurement in 16/16 cells
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-size="13" text-anchor="start">마스터 × 슬레이브 모드 16조합 — demo_modes.c 실측 (256×256 쌍 + 랜덤 2000회)</text> <text x="215" y="48" fill="currentColor" font-size="13" text-anchor="middle">슬레이브 mode 0</text>
  <text x="345" y="48" fill="currentColor" font-size="13" text-anchor="middle">슬레이브 mode 1</text> <text x="475" y="48" fill="currentColor" font-size="13" text-anchor="middle">슬레이브 mode 2</text> <text x="605" y="48" fill="currentColor" font-size="13" text-anchor="middle">슬레이브 mode 3</text>
  <text x="140" y="84" fill="currentColor" font-size="13" text-anchor="end">마스터 mode 0</text> <rect x="150" y="56" width="130" height="46" class="f-ok" stroke="currentColor" stroke-width="1"/> <text x="215" y="75" fill="currentColor" font-size="12" text-anchor="middle">OK</text>
  <text x="215" y="92" fill="currentColor" font-size="12" text-anchor="middle">MISO 0 · MOSI 0</text> <rect x="280" y="56" width="130" height="46" class="f-warn" stroke="currentColor" stroke-width="1"/> <text x="345" y="75" fill="currentColor" font-size="12" text-anchor="middle">1비트 밀림, race</text>
  <text x="345" y="92" fill="currentColor" font-size="12" text-anchor="middle">MISO +1 · MOSI 0</text> <rect x="410" y="56" width="130" height="46" class="f-mut" stroke="currentColor" stroke-width="1"/> <text x="475" y="75" fill="currentColor" font-size="12" text-anchor="middle">데이터 OK, race</text>
  <text x="475" y="92" fill="currentColor" font-size="12" text-anchor="middle">MISO 0 · MOSI 0</text> <rect x="540" y="56" width="130" height="46" class="f-warn" stroke="currentColor" stroke-width="1"/> <text x="605" y="75" fill="currentColor" font-size="12" text-anchor="middle">1비트 밀림</text>
  <text x="605" y="92" fill="currentColor" font-size="12" text-anchor="middle">MISO +1 · MOSI 0</text> <text x="140" y="130" fill="currentColor" font-size="13" text-anchor="end">마스터 mode 1</text> <rect x="150" y="102" width="130" height="46" class="f-warn" stroke="currentColor" stroke-width="1"/>
  <text x="215" y="121" fill="currentColor" font-size="12" text-anchor="middle">1비트 밀림, race</text> <text x="215" y="138" fill="currentColor" font-size="12" text-anchor="middle">MISO 0 · MOSI +1</text> <rect x="280" y="102" width="130" height="46" class="f-ok" stroke="currentColor" stroke-width="1"/>
  <text x="345" y="121" fill="currentColor" font-size="12" text-anchor="middle">OK</text> <text x="345" y="138" fill="currentColor" font-size="12" text-anchor="middle">MISO 0 · MOSI 0</text> <rect x="410" y="102" width="130" height="46" class="f-warn" stroke="currentColor" stroke-width="1"/>
  <text x="475" y="121" fill="currentColor" font-size="12" text-anchor="middle">1비트 밀림</text> <text x="475" y="138" fill="currentColor" font-size="12" text-anchor="middle">MISO -1 · MOSI 0</text> <rect x="540" y="102" width="130" height="46" class="f-warn" stroke="currentColor" stroke-width="1"/>
  <text x="605" y="121" fill="currentColor" font-size="12" text-anchor="middle">1비트 밀림, race</text> <text x="605" y="138" fill="currentColor" font-size="12" text-anchor="middle">MISO +1 · MOSI +1</text> <text x="140" y="176" fill="currentColor" font-size="13" text-anchor="end">마스터 mode 2</text>
  <rect x="150" y="148" width="130" height="46" class="f-mut" stroke="currentColor" stroke-width="1"/> <text x="215" y="167" fill="currentColor" font-size="12" text-anchor="middle">데이터 OK, race</text> <text x="215" y="184" fill="currentColor" font-size="12" text-anchor="middle">MISO 0 · MOSI 0</text>
  <rect x="280" y="148" width="130" height="46" class="f-warn" stroke="currentColor" stroke-width="1"/> <text x="345" y="167" fill="currentColor" font-size="12" text-anchor="middle">1비트 밀림</text> <text x="345" y="184" fill="currentColor" font-size="12" text-anchor="middle">MISO +1 · MOSI 0</text>
  <rect x="410" y="148" width="130" height="46" class="f-ok" stroke="currentColor" stroke-width="1"/> <text x="475" y="167" fill="currentColor" font-size="12" text-anchor="middle">OK</text> <text x="475" y="184" fill="currentColor" font-size="12" text-anchor="middle">MISO 0 · MOSI 0</text>
  <rect x="540" y="148" width="130" height="46" class="f-warn" stroke="currentColor" stroke-width="1"/> <text x="605" y="167" fill="currentColor" font-size="12" text-anchor="middle">1비트 밀림, race</text> <text x="605" y="184" fill="currentColor" font-size="12" text-anchor="middle">MISO +1 · MOSI 0</text>
  <text x="140" y="222" fill="currentColor" font-size="13" text-anchor="end">마스터 mode 3</text> <rect x="150" y="194" width="130" height="46" class="f-warn" stroke="currentColor" stroke-width="1"/> <text x="215" y="213" fill="currentColor" font-size="12" text-anchor="middle">1비트 밀림</text>
  <text x="215" y="230" fill="currentColor" font-size="12" text-anchor="middle">MISO -1 · MOSI 0</text> <rect x="280" y="194" width="130" height="46" class="f-warn" stroke="currentColor" stroke-width="1"/> <text x="345" y="213" fill="currentColor" font-size="12" text-anchor="middle">1비트 밀림, race</text>
  <text x="345" y="230" fill="currentColor" font-size="12" text-anchor="middle">MISO +1 · MOSI +1</text> <rect x="410" y="194" width="130" height="46" class="f-warn" stroke="currentColor" stroke-width="1"/>
  <text x="475" y="213" fill="currentColor" font-size="12" text-anchor="middle">1비트 밀림, race</text> <text x="475" y="230" fill="currentColor" font-size="12" text-anchor="middle">MISO 0 · MOSI +1</text> <rect x="540" y="194" width="130" height="46" class="f-ok" stroke="currentColor" stroke-width="1"/>
  <text x="605" y="213" fill="currentColor" font-size="12" text-anchor="middle">OK</text> <text x="605" y="230" fill="currentColor" font-size="12" text-anchor="middle">MISO 0 · MOSI 0</text>
  <text x="10" y="262" class="f-mut" font-size="12" text-anchor="start">+1 = 맨 앞에 몰지 않은 비트(1)가 끼고 한 칸씩 밀림,  −1 = 첫 비트를 놓치고 한 칸씩 당겨짐.</text> <text x="10" y="280" class="f-mut" font-size="12" text-anchor="start">race = 모든 샘플이 상대가 선을 바꾸는 바로 그 엣지에서 (방향마다 바이트당 8번).</text>
</svg>
```

읽는 법:

- **대각선 4칸만 진짜 OK.**
- **(0,2), (2,0)**: CPHA 가 둘 다 0 이고 CPOL 만 다름 — 데이터는 맞지만 매 샘플이 race. 책상 위 짧은 배선·낮은 클럭에선 되다가 클럭을 올리거나 케이블을 늘리면 가끔 틀린다. 가장 위험한 칸이다.
- **(0,3), (3,0), (1,2), (2,1)**: 샘플 엣지 극성은 같아서(0·3 = 상승, 1·2 = 하강) race 는 없는데, 첫 비트 처리(CPHA)가 달라 MISO 가 한 칸 밀린다. CPHA=1 슬레이브 + CPHA=0 마스터면 +1(마스터가 아직 안 나온 비트 = 1 을 먼저 읽음), 반대면 −1(첫 비트를 놓치고 다음 바이트 MSB 를 끌어옴).
- 나머지 6칸: 밀림 + race.
- MOSI 방향이 밀리는 건 마스터가 CPHA=1 이고 슬레이브가 홀수 엣지에서 샘플할 때뿐이다 (CPHA=0 마스터는 첫 비트를 일찍 내놓으므로 늦게 읽혀도 안 놓친다).

```svg
<svg viewBox="0 0 740 228" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-size="13" text-anchor="start">불일치: 마스터 mode 0 (상승 샘플) ↔ 슬레이브 mode 1 (상승 시프트), 슬레이브 → 0x12</text> <text x="10" y="54" fill="currentColor" font-size="13" text-anchor="start">SCLK</text>
  <polyline fill="none" stroke="currentColor" stroke-width="1.6" points="150,58 179,58 208,58 237,58 237,40 266,40 266,58 295,58 295,40 324,40 324,58 353,58 353,40 382,40 382,58 411,58 411,40 440,40 440,58 469,58 469,40 498,40 498,58 527,58 527,40 556,40 556,58 585,58 585,40 614,40 614,58 643,58 643,40 672,40 672,58 701,58 730,58"/>
  <line x1="237" y1="34" x2="237" y2="152" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/> <line x1="295" y1="34" x2="295" y2="152" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/>
  <line x1="353" y1="34" x2="353" y2="152" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/> <line x1="411" y1="34" x2="411" y2="152" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/>
  <line x1="469" y1="34" x2="469" y2="152" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/> <line x1="527" y1="34" x2="527" y2="152" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/>
  <line x1="585" y1="34" x2="585" y2="152" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/> <line x1="643" y1="34" x2="643" y2="152" stroke="currentColor" stroke-dasharray="3 3" stroke-width="1" opacity="0.6"/>
  <text x="10" y="100" fill="currentColor" font-size="13" text-anchor="start">MISO</text>
  <polyline fill="none" stroke="currentColor" stroke-width="1.6" points="150,86 179,86 208,86 237,86 237,104 266,104 295,104 324,104 353,104 382,104 411,104 411,86 440,86 469,86 469,104 498,104 527,104 556,104 585,104 585,86 614,86 643,86 643,104 672,104 701,104 701,86 730,86"/>
  <text x="266" y="81" class="f-mut" font-size="12" text-anchor="middle" font-family="monospace">b7=0</text> <text x="324" y="81" class="f-mut" font-size="12" text-anchor="middle" font-family="monospace">b6=0</text>
  <text x="382" y="81" class="f-mut" font-size="12" text-anchor="middle" font-family="monospace">b5=0</text> <text x="440" y="81" class="f-mut" font-size="12" text-anchor="middle" font-family="monospace">b4=1</text>
  <text x="498" y="81" class="f-mut" font-size="12" text-anchor="middle" font-family="monospace">b3=0</text> <text x="556" y="81" class="f-mut" font-size="12" text-anchor="middle" font-family="monospace">b2=0</text>
  <text x="614" y="81" class="f-mut" font-size="12" text-anchor="middle" font-family="monospace">b1=1</text> <text x="672" y="81" class="f-mut" font-size="12" text-anchor="middle" font-family="monospace">b0=0</text> <text x="193" y="81" class="f-mut" font-size="12" text-anchor="middle">몰지 않음(1)</text>
  <text x="10" y="142" fill="currentColor" font-size="13" text-anchor="start">마스터가 읽은 값</text> <text x="237" y="142" class="f-warn" font-size="13" text-anchor="middle" font-family="monospace">1</text>
  <text x="295" y="142" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">0</text> <text x="353" y="142" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">0</text>
  <text x="411" y="142" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">0</text> <text x="469" y="142" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">1</text>
  <text x="527" y="142" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">0</text> <text x="585" y="142" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">0</text>
  <text x="643" y="142" class="f-acc" font-size="13" text-anchor="middle" font-family="monospace">1</text> <text x="701" y="142" class="f-acc" font-size="13" text-anchor="end" font-family="monospace">= 0x89</text>
  <text x="10" y="168" class="f-mut" font-size="12" text-anchor="start">상승 엣지마다 마스터는 엣지 직전 값(이전 비트)을 읽고, 슬레이브는 그 순간 새 비트를 낸다.</text> <text x="10" y="186" class="f-mut" font-size="12" text-anchor="start">→ 받은 값 = 1 + (0x12 &gt;&gt; 1) = 0x89. 같은 엣지에 샘플과 변경이 겹친다 = race (hold time 의존).</text>
  <text x="10" y="204" class="f-mut" font-size="12" text-anchor="start">MOSI 방향: 슬레이브가 하강 엣지에서 샘플 → 이 모델에선 맞게 받지만 역시 race.</text>
</svg>
```

증상 공식 (+1 밀림): 받은 바이트 = `prev_lsb << 7 | sent >> 1` — 첫 바이트의 prev_lsb 는 몰지 않은 레벨(보통 풀업 1). 그래서 "값이 정확히 반 + 0x80", "WHO_AM_I 0x33 이 0x99" 로 보인다. −1 이면 `sent << 1 | next_msb` — "값이 두 배". 다바이트 실측(위 그림과 같은 조합):

```text
(3) master mode 0, slave mode 1, both send 12 34 (CS held)
  master got 89 1A   slave got 12 34   races MISO=16 MOSI=16
  CS_N -__________________________________-
  SCLK ___-_-_-_-_-_-_-_-_-_-_-_-_-_-_-_-__
  MOSI --______--____--______----__--____--
  MISO ---______--____--______----__--____-
  smpl ...MSMSMSMSMSMSMSMSMSMSMSMSMSMSMSMS.
```

### Part B — 버스 고장 셋

**SCLK 글리치.** 바이트 1 직전(step 19)에 펄스 하나를 넣으면 슬레이브만 엣지 두 개(샘플 1 + 시프트 1)를 더 본다. 슬레이브는 MOSI 의 b7 을 두 번 받아 `A5 → D2` (1101 0010), 그 CS 구간이 끝날 때까지 계속 한 칸 밀린다. 슬레이브는 한 비트 앞서 나가므로 마스터는 −1: `3C → 78, 79`. CS 상승 때 슬레이브에 1비트가 남아 `partial == 1` — 이게 **검출 수단**이다. 하드웨어 SPI 슬레이브라면 "CS 상승 시 RX 비트 카운터 ≠ 0" 을 에러로 올리는 칩도 있고, 없으면 펌웨어가 프레임 길이를 확인한다. 결론: CS 를 GND 에 묶은 단일 장치 버스는 글리치 한 번에 **영원히** 밀린다(전원을 껐다 켜기 전까지). CS 를 프레임마다 토글하라.

```text
(4) mode 0, SCLK glitch before byte 1, master A5 A5 A5 / slave 3C 3C 3C
  master got 3C 78 79   slave got A5 D2 D2   slave partial=1
```

**CS 를 idle 보다 먼저 (`spi_xfer_cs_first`).** mode 0 전송 뒤 SCLK=0, MOSI=1(마지막에 꺼낸 0xFF 의 b7). mode 3 장치를 선택하며 CS 를 먼저 내리고 SCLK 를 0→1 로 올리면, mode 3 슬레이브에게 상승 = 샘플 엣지 → MOSI 의 1 을 한 비트 받아 버린다. 이후 진짜 8비트가 들어와 `0x80 → 0xC0` (1 | 0x80>>1), CS 상승 때 1비트가 남는다. 읽기 방향(MISO)은 슬레이브가 아직 아무것도 시프트하지 않았으므로 멀쩡 — "읽기는 되는데 설정 쓰기가 가끔 이상하다" 는, 늦게 발견되는 버그다. 고친 순서(①→②)로 같은 시나리오는 0x80 이 정확히 들어간다. 같은 SPI 버스에 모드가 다른 장치가 있다면: **CS 가 모두 높을 때** 모드를 바꾸고, 바꾼 뒤 SCLK 가 새 idle 에 자리 잡은 다음 CS 를 내린다 (CPP-B07 의 RAII CS 가드를 쓴다면 가드 생성자에서 이 순서로).

**CS 중간 해제.** mode 1 로 6비트(12 엣지)만 보내고 CS 를 올리면 슬레이브 `in_cnt = 6` → `partial = 1`, 저장된 바이트 0. 다음 프레임은 `eng_select` 가 카운터를 0 에서 시작하므로 정상. COMM-B05 의 "주소와 데이터 사이에 CS 가 풀리는" 버그는 바이트 경계에서 같은 일이 일어나는 것이다.

### Part C — 에코 슬레이브: 응답은 한 바이트 늦다

에코 슬레이브는 "다음에 보낼 바이트 = 마지막으로 받은 바이트". 마스터가 `11 22 33 44` 를 보내면 `FF 11 22 33` 이 돌아온다 (4모드 모두). 슬레이브가 바이트 k 를 다 받는 순간(8번째 샘플)은 바이트 k 의 마지막 비트가 이미 나간 뒤다. 그래서 명령에 대한 대답은 **다음 바이트**에서만 나올 수 있고, 마스터는 그동안 더미(보통 0x00 또는 0xFF)를 보낸다 — 센서 레지스터 읽기가 "주소 바이트 + 더미 바이트" 인 이유(COMM-B05), SPI NOR 의 `0x9F` 뒤에 ID 가 오는 이유(COMM-A06). 레지스터식 장치는 첫 바이트를 받는 동안 MISO 에 상태 바이트나 쓰레기를 낸다.

### Part C — LA 디코더

디코더는 슬레이브 엔진과 같은 규칙(CS 하강에서 시작, 샘플 엣지에서 엣지 직전 값, CS 상승에서 partial 판정)을 캡처 위에서 돈다. 그래서 글리치 캡처를 디코드하면 마스터가 보낸 값이 아니라 **슬레이브가 받은 값** `A5 D2 D2` 와 `partial = 1` 이 나온다 — LA 는 선 위의 사실을 본다. 모드를 잘못 고르면 디코더 자체가 불일치 표의 한 칸이 된다: mode 1 캡처를 mode 0 으로 디코드하면 MOSI 가 +1 밀린 `89 …` 로 보인다(테스트). 실제 LA 소프트웨어(sigrok/PulseView 의 SPI 디코더 등)도 CPOL/CPHA/bit order 옵션을 직접 맞춰야 한다 — 디코드가 이상하면 먼저 디코더 설정을 의심하라. 캡처가 프레임 중간에서 시작하면 첫 CS 하강까지 엣지를 무시한다(그렇지 않으면 첫 바이트들이 밀린다).

### 복잡도

- 엔진: step 당 O(1), 분기 몇 개. RAM: `sizeof(spi_eng_t)` = 72 바이트(맥 실측, 64비트 포인터·size_t), 32비트 MCU 에선 약 44 바이트(손 계산). 힙 없음.
- n 바이트 전송 = 16n + 4 step. 캡처 버퍼 4096 줄 × 5 바이트 = 20 KB (테스트·디코드용; 펌웨어 쪽 코드는 캡처 없이 동작).
- 전수 테스트 4 × 65536 전송 × 20 step ≈ 5.2M step, `solution.c` 전체가 0.1초 안팎.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 93/93 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 93/93, 오류 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_modes.c -o demo_modes && ./demo_modes # 파형 4개 + 16조합 표, 약 0.5초
```

## 7. 자주 하는 실수

**1. CPHA=0 에서 첫 비트를 첫 엣지에서 내보낸다.** CPHA=1 처럼 select 때 아무것도 안 내면, 상대의 첫 샘플(leading)이 몰지 않은 1 을 읽는다. 이 레슨 작업 중 `eng_select` 의 CPHA=0 분기를 지운 변형: mode 0/2 한 바이트 전수에서 **65535/65536 쌍 실패**(0xFF↔0xFF 만 우연히 통과), `A5/3C → 슬레이브 D2, 마스터 9E`. mode 1/3 은 통과하므로 "일부 장치만 안 됨" 으로 보인다. 고친 코드: `if (cpha == 0) { out = 첫 비트; out_cnt = 1; }`.

**2. leading 엣지를 "상승 엣지" 로 고정한다.** `leading = new_sclk != 0` 으로 쓰면 CPOL=0 인 mode 0/1 은 전부 통과하고 mode 2/3 만 깨진다(변형 실측: mode 3 `A5/3C → D2/9E`, mode 2 `→ 4B/79`, 각 65535/65536 실패). 데이터시트의 "data sampled on rising edge" 는 모드 결과일 뿐 정의가 아니다. leading = idle 에서 벗어나는 엣지 = `new_sclk != CPOL`.

**3. 바이트 경계에서 다음 바이트를 안 꺼낸다.** `out_cnt == 8` 에서 카운터만 0 으로 돌리고 `tx_sr` 를 그대로 두면 **한 바이트 전수 테스트는 4모드 모두 통과**한다(변형 실측 0/65536 실패). 두 바이트 `A5 0F` 를 보내면 `A5 A5` 가 나간다. 그래서 다바이트 테스트와 "바이트 사이에서 다음 MSB 가 나오는지(파형의 t=18)" 를 따로 본다. 카운터를 아예 안 돌리면 `7 − out_cnt` 가 음수가 되어 시프트 UB — UBSan 이 잡는다.

**4. CS 를 내린 뒤 모드(CPOL)를 바꾼다.** 위 Part B 그대로: 쓰기만 1비트 밀린다. 레지스터 쓰기 → 읽어서 확인(read-back)을 드라이버 init 에 넣으면 바로 드러난다.

**5. "루프백이 되니까 모드는 맞다".** MOSI–MISO 를 점퍼로 묶은 루프백은 4모드 전부 통과한다(연습 2) — 마스터는 자기가 낸 비트를 자기 규칙으로 읽기 때문이다. 루프백은 핀 먹스(AF)와 클럭 출력만 검증한다. 모드는 **장치 데이터시트의 타이밍도**와 LA 캡처로 맞춘다.

**6. 데이터시트의 "mode 0 and 3 supported" 를 "아무 모드나 된다" 로 읽는다.** 0 과 3 은 샘플 엣지가 같아서 장치가 CS 하강 때 SCLK 레벨로 구분할 수 있다. 1·2 로 몰면 (1,0) 칸이나 (2,3) 칸처럼 밀린다 (연습 3 실측).

**7. 엣지 "뒤에" 읽는 비트뱅으로 검증하고 하드웨어 SPI 로 옮긴다.** GPIO 비트뱅은 엣지를 만든 다음 한참 뒤에 MISO 를 읽는다. 그래서 mode 0 비트뱅이 mode 1 슬레이브와도 맞게 동작한다(연습 1 실측: `56 78` 정상 수신). 같은 조합을 엣지에서 샘플하는 하드웨어식 마스터로 돌리면 `AB …` (+1 밀림). "비트뱅으론 됐는데 SPI 주변장치로 바꾸니 안 된다" 의 흔한 원인이다.

## 8. 면접 Q&A

<details><summary>Q1. CPOL 과 CPHA 를 한 문장씩으로 정의해 보세요. 데이터시트에서 모드를 어떻게 읽나요?</summary>

CPOL 은 SCLK 의 idle 레벨(0 = low, 1 = high). CPHA 는 샘플이 첫(leading) 엣지냐(0) 둘째(trailing) 엣지냐(1). 둘을 합치면 `sample = (new_sclk != CPOL) XOR CPHA`, 시프트는 반대 엣지. 데이터시트에 모드 번호가 안 적혀 있으면 타이밍도에서 세 가지를 본다: CS 가 내려갈 때 SCLK 레벨(→ CPOL), "data latched/sampled on rising/falling" 혹은 SDI 의 setup/hold 화살표가 걸린 엣지(→ CPHA 와 CPOL 조합), 첫 비트가 CS 하강 직후에 나오는지(CPHA=0) 첫 엣지 뒤에 나오는지(CPHA=1). 모드 번호 매기기(CPOL<<1 | CPHA)는 Motorola 관례이고, 벤더에 따라 위상 정의를 반대로 쓰는 경우도 있다(예: TI MSP430 USCI 의 UCCKPH 는 1 이 "첫 엣지에서 캡처" 로 알고 있음 — 해당 문서 확인 권장). 그래서 **번호보다 그림**을 믿는다.

</details>

<details><summary>Q2. WHO_AM_I 가 0x33 이어야 하는데 0x99 로 읽힙니다. 무엇을 의심하나요? 꼬리: 0x66 이나 0x67 이면?</summary>

0x99 = 1001 1001 = `1 | 0x33 >> 1` — 한 비트 늦게 읽고 맨 앞에 몰지 않은 1 이 낀 **+1 밀림**. 마스터가 CPHA=0 인데 장치는 CPHA=1 로 첫 비트를 첫 엣지에서 내는 경우(예: 마스터 mode 0, 장치 mode 1 또는 3)다. 0x66/0x67 = `0x33 << 1 | 다음 비트` 는 −1 밀림: 마스터 CPHA=1, 장치 CPHA=0 이면서 샘플 엣지 극성이 같은 경우(mode 3 마스터 + mode 0 장치, mode 1 마스터 + mode 2 장치). CPHA=0 마스터 + CPHA=1 장치는 네 조합 모두 +1 이다(표). 둘 다 LA 를 물리면 MISO 가 바뀌는 엣지와 마스터가 샘플하는 엣지가 같은 쪽인지 반 클럭 어긋나 있는지로 바로 보인다. 모드를 맞추기 전에 확인할 것: 바이트가 늘 0xFF 면 MISO 를 아무도 안 몬다(CS 극성·배선·전원), 늘 0x00 이면 MISO 가 low 로 붙어 있다. (WHO_AM_I = 0x33 은 ST LIS3DH 류 가속도계의 값으로 알고 있음 — 데이터시트 확인 권장. 예시일 뿐 이 레슨 테스트와 무관.)

</details>

<details><summary>Q3. 왜 많은 장치가 "mode 0 과 mode 3 지원" 이라고 하나요? 1·2 는 왜 빠지죠?</summary>

mode 0 과 3 은 둘 다 **상승 엣지에서 샘플, 하강 엣지에서 시프트**다. 다른 점은 idle 레벨과 첫 비트 시점뿐이고, 장치는 CS 가 내려갈 때 SCLK 레벨을 보면 둘을 구분할 수 있다(low → mode 0: 첫 비트를 바로 냄, high → mode 3: 첫 하강 엣지에서 냄). 내부 시프트 레지스터는 하나로 충분하다. mode 1·2 는 샘플 엣지가 반대(하강)라 같은 하드웨어로는 안 된다. 연습 3 에서 이 자동 판정 슬레이브를 만들면 mode 0/3 마스터와는 정상, mode 1 마스터와는 MOSI +1, mode 2 마스터와는 MISO +1 로 실측된다. SPI NOR 플래시 데이터시트들이 흔히 이렇게 적는다(예: Winbond W25Q 계열 — 확인 권장).

</details>

<details><summary>Q4. 모드를 틀렸는데 책상에선 동작했습니다. 가능한가요? 양산에서 무슨 일이 생기죠?</summary>

가능하다. 16조합 표의 (0,2)/(2,0) 칸은 데이터가 맞지만 **모든 샘플이 상대가 데이터를 바꾸는 바로 그 엣지**에서 일어난다(race, 바이트당 8번). 실제로 맞게 읽히는지는 hold 여유 = (상대의 최소 clock-to-output + 왕복 배선 지연) − (샘플 쪽 hold 요구) 의 부호에 달렸다(연습 5: 2 + 2·1 − 3 = 1 ns 면 간신히). 온도, 전압, 케이블 길이, 다른 로트의 칩에서 이 1 ns 가 음수가 되면 가끔 한 비트가 틀린다 — 재현이 어려운 필드 불량. 또 7. 실수 7 처럼 "엣지 뒤 한참 있다 읽는" 비트뱅은 밀림 칸에서도 우연히 맞는다. 그래서 bring-up 체크리스트에 "LA 로 샘플 엣지에서 데이터가 반 클럭 동안 안정한지" 를 넣는다.

</details>

<details><summary>Q5. SPI 는 어떻게 바이트 경계를 아나요? 노이즈로 SCLK 에 펄스가 하나 더 생기면?</summary>

경계를 아는 유일한 근거는 CS 하강 이후 몇 번째 샘플인가다. 시작 비트나 동기 패턴이 없다. 글리치 하나면 슬레이브의 비트 카운터가 하나 앞서고, **CS 가 올라갈 때까지** 모든 바이트가 밀린다(실측: 슬레이브 `A5 D2 D2`, 마스터 `3C 78 79`). CS 상승 때 카운터가 0 이 아니면(partial) 검출할 수 있고, 다음 CS 하강에서 자동 복구된다. CS 를 GND 에 묶은 설계는 복구 수단이 없다. 대책: 프레임마다 CS 토글, 긴 배선이면 SCLK 에 직렬 저항(링잉 억제)이나 슈미트 트리거 입력, 프로토콜 레벨에서 CRC 또는 응답 검증(COMM-B08/B10).

</details>

<details><summary>Q6. 전이중인데 왜 레지스터 읽기 응답은 다음 바이트에 오나요? 슬레이브 펌웨어(MCU 가 SPI 슬레이브)라면 무엇이 어렵죠?</summary>

슬레이브가 명령 바이트를 다 받는 순간(8번째 샘플)엔 그 바이트 동안 보낼 비트가 이미 다 나갔다. 그래서 대답은 다음 바이트부터다(에코 슬레이브 실측 `FF 11 22 33`). 마스터는 그동안 더미를 보낸다. MCU 가 슬레이브일 때 어려운 점은 시간이다: CPHA=0 이면 마지막 샘플 뒤 **반 클럭** 안에 다음 바이트의 MSB 가 선에 있어야 한다. 10 MHz 면 50 ns — 인터럽트로는 절대 못 맞춘다. 그래서 (1) TX FIFO/DMA 에 응답을 미리 넣고, (2) 마스터가 명령과 응답 사이에 간격(또는 더미 바이트 여러 개)을 두도록 프로토콜을 정하고, (3) CS 하강 인터럽트에서 TX 를 다시 준비(re-arm)한다. CS 상승에서 TX FIFO 에 남은 바이트가 다음 트랜잭션 첫 바이트로 나가 "응답이 한 칸씩 밀리는" 문제는 MCU 슬레이브에서 흔하다 — 이 레슨의 "CPHA=0 은 프레임 끝에서 한 바이트를 미리 꺼낸다" 테스트가 같은 종류다(구체 동작은 칩 RM 확인 권장).

</details>

<details><summary>Q7. 비트뱅 SPI 와 하드웨어 SPI 주변장치 중 무엇을 쓰나요? 비트뱅 중에 인터럽트가 걸리면?</summary>

하드웨어가 기본이다: 수~수십 MHz, CPU 부담 없음, DMA 가능(COMM-M11 의 UART DMA 와 같은 구조). 비트뱅은 핀이 주변장치에 연결되지 않았을 때, 이상한 워드 크기(9비트, 3-wire 반이중), bring-up 에서 주변장치 설정을 의심할 때 쓴다. 인터럽트가 걸려도 **SPI 는 마스터의 클럭이 시간을 정의**하므로 바이트 중간에 멈췄다 재개해도 데이터는 틀리지 않는다 — UART(COMM-B03)와 정반대. 예외: 일부 장치는 SCLK 가 너무 오래 멈추면 타임아웃으로 인터페이스를 리셋한다(데이터시트 확인). 비트뱅의 함정은 7. 실수 7: 엣지 뒤에 읽기 때문에 모드가 틀려도 동작해 버린다.

</details>

<details><summary>Q8. 같은 SPI 버스에 mode 0 ADC 와 mode 3 플래시가 있습니다. 드라이버는 무엇을 해야 하죠?</summary>

트랜잭션마다 (1) 모든 CS 가 높은지 확인(버스 잠금, CPP-B07 의 RAII 가드나 RTOS 뮤텍스), (2) 주변장치를 끄고(SPE=0 류) CPOL/CPHA/속도를 이 장치 값으로 바꾸고 켬, (3) SCLK 가 새 idle 레벨에 자리 잡은 뒤 CS 하강, (4) 끝나면 CS 상승. (2)와 (3)의 순서를 바꾸면 Part B 의 가짜 엣지 버그다. Linux 에선 `spi_device` 마다 mode/max_speed_hz 를 갖고 컨트롤러 드라이버가 전송 전에 바꾸며, 이 순서 책임은 컨트롤러 드라이버에 있다. 속도는 장치마다 다르게: 느린 장치에 맞춘 클럭을 전체에 쓰지 않는다. COMM-M12 의 버스 추상화에서 이 "장치별 설정 + 버스 잠금" 을 인터페이스로 만든다.

</details>

<details><summary>Q9. 새 보드에서 SPI 가 전혀 응답이 없습니다. 무엇부터 보나요?</summary>

바깥에서 안으로: (1) 전원·리셋 핀, 장치가 SPI 모드인지(I2C/SPI 겸용 센서는 CS 레벨로 인터페이스를 고르는 경우가 있음 — 데이터시트), (2) LA/스코프로 CS 가 실제로 토글되는지, SCLK 가 **핀에서** 나오는지(GPIO AF 먹스 설정, HWLAB-B 시리즈), (3) MISO: 늘 0xFF 면 아무도 안 몬다(CS 극성, CS 핀 오배선, 장치 미선택), 늘 0x00 이면 단락이나 다른 장치가 MISO 를 잡고 있음(선택 안 된 장치가 MISO 를 tri-state 안 하는 경우 — 실수로 CS 두 개가 동시에 low), (4) 루프백으로 컨트롤러 확인(모드는 검증 못 함 — 7. 실수 5), (5) WHO_AM_I 같은 고정 값을 읽어 비트 밀림 패턴 확인(Q2), (6) 클럭을 1 MHz 이하로 낮춰 타이밍 문제와 논리 문제를 분리. 이 순서를 말로 할 수 있으면 bring-up 경험이 있다는 신호다.

</details>

## 9. 추가 연습

### 연습 1 — Linux 모양의 GPIO 비트뱅 루프

마스터 엔진을 쓰지 말고 GPIO 세 함수(`gpio_sclk`, `gpio_mosi`, `gpio_miso`)만으로 Linux `spi-bitbang-txrx.h` 모양의 `bb_txrx(mode, byte)` 를 짜라: CPHA=0 은 "MOSI 설정 → leading → MISO 읽기 → trailing", CPHA=1 은 "leading → MOSI 설정 → trailing → MISO 읽기". 짝이 맞는 슬레이브와 4모드 × 256×256 전수로 검증하고, 마스터 mode 0 / 슬레이브 mode 1 조합에서 비트뱅과 하드웨어식 마스터의 결과를 비교하라.

<details><summary>답 보기</summary>

```c
if (c.cpha == 0) { gpio_mosi(s, bit); gpio_sclk(s, !c.cpol); in = in << 1 | gpio_miso(s); gpio_sclk(s, c.cpol); }
else             { gpio_sclk(s, !c.cpol); gpio_mosi(s, bit); gpio_sclk(s, c.cpol); in = in << 1 | gpio_miso(s); }
```

4모드 전수 통과. 불일치 (0,1) 에서 비트뱅은 `56 78` 을 **정확히** 받고, 엣지에서 샘플하는 하드웨어식 마스터는 `AB …` (+1). 비트뱅은 엣지를 만든 **뒤에** MISO 를 읽기 때문에 상대가 그 엣지에서 바꾼 새 값을 읽는다 — 느린 비트뱅에서는 반주기 내내 기다려 주니 오히려 안전하지만, 같은 설정을 하드웨어 SPI 로 옮기면 깨진다(7. 실수 7). 모델에서 `gpio_mosi` 는 엣지 없는 step 하나를 쓰므로 바이트당 24 step 이 든다(하드웨어식은 16).

</details>

### 연습 2 — 루프백 자가 시험

슬레이브 대신 MOSI 를 MISO 에 직접 연결(`loopback = 1`)하고 4모드로 `a, ~a, 0x5A` 를 a = 0..255 에 대해 보내 rx == tx 인지 보라. 이 시험이 잡는 것과 못 잡는 것은?

<details><summary>답 보기</summary>

4모드 모두 768/768 바이트 일치. 마스터는 자기 시프트 엣지에서 비트를 내고 반주기 뒤 자기 샘플 엣지에서 그것을 읽으므로 **모드와 무관하게** 늘 맞는다. 잡는 것: 컨트롤러 클럭 켜짐, MOSI/MISO/SCLK 핀 먹스, DMA 경로, 워드 크기·비트 순서 설정이 송수신에 같게 적용되는지. 못 잡는 것: CPOL/CPHA 가 **장치**와 맞는지, CS 극성, 장치 쪽 배선. 공장 자가 시험(self-test)에는 쓸 만하지만 "모드 확인" 은 아니다.

</details>

### 연습 3 — mode 0/3 자동 판정 슬레이브

슬레이브가 CS 하강 순간의 SCLK 레벨로 `cpol = cpha = SCLK` (low → mode 0, high → mode 3)를 정하게 하라. 마스터 4모드 각각으로 4바이트를 주고받고 결과를 불일치 표와 대조하라.

<details><summary>답 보기</summary>

```c
if (!was && now) { if (s->auto03) { s->s1.cfg.cpol = sclk; s->s1.cfg.cpha = sclk; } eng_select(&s->s1); ... }
```

마스터 mode 0/3: 정상. mode 1: idle low 라 슬레이브는 mode 0 으로 판단 → 표의 (1,0) 칸 = MISO 0(race), **MOSI +1** — 슬레이브가 받는 값이 밀린다(쓰기가 깨짐). mode 2: idle high 라 mode 3 으로 판단 → (2,3) 칸 = **MISO +1**, MOSI 0(race). "0 and 3" 장치에 1·2 를 쓰면 안 되는 이유가 표에서 그대로 나온다.

</details>

### 연습 4 — 데이지 체인

마스터 MOSI → 장치 1 → 장치 1 의 출력 → 장치 2 → MISO 로 이어진 8비트 시프트 레지스터 두 개(모델: mode 0 에코 엔진 두 개)에 `A1 B2` 를 보낸 뒤, 각 장치에 남은 값과 다음 전송 `C3 D4` 때 돌아오는 값을 예측하라.

<details><summary>답 보기</summary>

첫 바이트가 **먼 쪽** 장치로 간다: 장치 2 = A1, 장치 1 = B2. 첫 전송의 응답은 `FF FF`(처음 내용). 둘째 전송 `C3 D4` 때는 이전 내용이 밀려 나와 `A1 B2` 가 돌아오고, 장치 2 = C3, 장치 1 = D4. 74HC595 같은 시프트 레지스터 체인은 이렇게 쓰고 CS(래치 클럭) 상승에서 출력에 반영한다. 돌아오는 값을 보면 체인 길이와 배선을 검사할 수 있다(알려진 패턴을 체인 길이 + 1 바이트만큼 밀어 넣고 언제 나오는지 본다).

</details>

### 연습 5 — 최대 SCLK 와 hold 여유

mode 0 에서 마스터는 상승에서 샘플, 슬레이브는 하강에서 MISO 를 바꾼다. (a) 반주기 ≥ 2·t_pd(왕복 배선) + t_V(슬레이브 clock→MISO 유효) + t_SU(마스터 setup) 로 `spi_fmax_hz` 를 짜라. (b) fPCLK / 2^k (k = 1..8) 중 이것 이하인 가장 빠른 분주비를 고르는 `spi_pick_div` 를 나눗셈 오차 없이 짜라. (c) race 칸(같은 엣지에서 샘플+변경)의 hold 여유 `2·t_pd + t_V(min) − t_H` 를 계산하라. (숫자는 예제용 대표값 — 실제 값은 데이터시트.)

<details><summary>답 보기</summary>

```c
uint32_t half = 2 * tpd_ns + tv_ns + tsu_ns;  return 1000000000ull / (2ull * half);
for (d = 2; d <= 256; d <<= 1) if ((uint64_t)fpclk <= (uint64_t)fmax * d) return d;   /* fpclk/d ≤ fmax */
return (int32_t)(2 * tpd_ns + tv_min_ns) - (int32_t)th_ns;
```

t_V = 12, t_SU = 4, t_pd = 2 ns → 반주기 20 ns → 25 MHz. 84 MHz PCLK 면 /4 = 21 MHz (/2 = 42 MHz 는 초과). 72 MHz 에 10 MHz 상한이면 /8 = 9 MHz. 80 MHz 에 40 MHz 상한이면 /2 가 정확히 40 MHz 로 허용 — `fpclk / d <= fmax` 를 정수 나눗셈으로 쓰면 80000001 Hz 같은 값에서 내림 때문에 실제보다 느리다고 착각한다. hold: t_V(min) = 2, t_H = 3, t_pd = 1 → +1 ns (간신히), t_V(min) = 0 이면 −3 ns (위반). 데이터시트의 t_V 는 보통 **max** 만 있고 min 이 없다 — 그래서 race 칸은 "보장 없음" 이다. 일부 컨트롤러는 샘플 시점을 늦추는 옵션이 있다(예: NXP LPSPI 의 SAMPLE 비트 — RM 확인 권장). (STM32 SPI 의 BR 필드도 fPCLK/2 … /256 의 2의 거듭제곱 — RM 확인 권장.)

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 26/26 passed
```

## 10. 핵심 정리

- CPOL = SCLK idle 레벨, CPHA = 샘플이 leading(0)/trailing(1). `sample = (new_sclk != CPOL) XOR CPHA`. mode 0·3 = 상승 샘플, 1·2 = 하강 샘플.
- CPHA=0 은 첫 비트를 CS↓ 직후(첫 엣지 전)에 내고, 마지막 trailing 엣지에서 다음 바이트 MSB 를 미리 낸다 → 슬레이브는 반 클럭 안에 다음 바이트가 필요(FIFO/DMA, re-arm).
- 드라이버 순서: SCLK idle → CS↓ → (t_CSS) → 16n 엣지 → CS↑. idle 을 CS 뒤에 바꾸면 가짜 엣지로 쓰기만 1비트 밀린다.
- 불일치 16조합: 대각선만 OK. CPHA=0 끼리 CPOL 만 다름(0↔2) = 데이터 OK 지만 race(hold 의존; 1↔3 은 밀림까지), 샘플 극성 같고 CPHA 다름(0↔3, 1↔2) = race 없이 1비트 밀림, 나머지 = 밀림 + race.
- +1 밀림 = `prev_lsb<<7 | x>>1` (0x33 → 0x99), −1 = `x<<1 | next_msb` (0x33 → 0x66/0x67). 값이 반/두 배면 모드부터.
- CS 가 유일한 프레임 동기: SCLK 글리치는 CS↑ 까지 밀림을 남기고 partial 로 검출, 다음 CS↓ 에서 복구. CS 를 GND 에 묶지 마라.
- 응답은 다음 바이트(더미 필요). 루프백은 모드를 검증 못 하고, 엣지 뒤에 읽는 비트뱅은 틀린 모드도 통과시킨다.

## 참고자료

- Motorola/Freescale, *SPI Block Guide* (예: S12SPIV3/V4) — CPOL/CPHA 와 모드 번호 관례의 출처로 널리 인용됨 (판 번호는 기억 기반 — 확인 권장).
- Wikipedia, "Serial Peripheral Interface" — 모드 표, 데이지 체인, 3-wire 변형: <https://en.wikipedia.org/wiki/Serial_Peripheral_Interface>
- Linux kernel, `Documentation/spi/spi-summary.rst` — SPI_MODE_0..3 정의, 컨트롤러/프로토콜 드라이버 구조; `drivers/spi/spi-bitbang-txrx.h` (CPHA 별 비트뱅 루프 — 연습 1 의 모양), `drivers/spi/spi-gpio.c` (chipselect 시 클럭 idle 설정): <https://www.kernel.org/doc/html/latest/spi/spi-summary.html>
- STMicroelectronics, STM32 Reference Manual 의 SPI 장 (예: RM0090 STM32F4) — CPOL/CPHA 비트, BR 분주, 슬레이브 모드 TX 준비, NSS 관리. 이 레슨의 STM32 관련 언급은 RM 확인 권장.
- NXP, LPSPI 장 (i.MX RT 등 RM) — CFGR1.SAMPLE (지연 샘플) 옵션. 기억 기반, 확인 권장.
- P. Dhaker, "Introduction to SPI Interface", *Analog Dialogue* 52-09, Analog Devices, 2018 — 모드별 타이밍도 그림 (권호는 기억 기반).
- sigrok/PulseView SPI protocol decoder 문서 — cpol/cpha/bitorder/cs_polarity 옵션: <https://sigrok.org/wiki/Protocol_decoder:Spi>
- 관련 레슨: COMM-B01~B03 (UART 프레임·오버샘플링·baud — 클럭 없는 직렬과 비교), COMM-B05 (SPI 레지스터 장치, 더미 바이트, CS 해제 버그), COMM-B06 (I2C 비트 수준), COMM-M11 (DMA 수신), COMM-M12 (I2C/SPI 겸용 드라이버 계층), COMM-A06 (SPI NOR 드라이버), COMM-A10 (LA 디코더 종합), HWLAB-M05 (LA 로 SPI 타이밍 t_CSS/t_SU/t_H 측정), HWLAB-M06 (sigrok 식 디코더), CPP-B07 (RAII SPI CS 가드), BM-B12 / BM-M04 (실제 폴링/IRQ 직렬 드라이버)
