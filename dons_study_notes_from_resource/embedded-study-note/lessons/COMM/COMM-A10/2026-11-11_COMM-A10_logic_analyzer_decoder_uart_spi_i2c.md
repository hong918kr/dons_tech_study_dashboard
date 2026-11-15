---
id: COMM-A10
time: 120분
updated: 2026-11-11
---
# COMM-A10 로직 애널라이저 디코더: 샘플 캡처 → UART/SPI/I2C 디코드

## 학습 목표

- 로직 애널라이저(logic analyzer, LA) 캡처를 **채널별 "시작 레벨 + 토글 시각(ps 정수)"** 으로 표현하고, CSV(`time_ns,ch0,ch1,…`)와 VCD 부분집합(`$timescale`, `$var wire 1`, `#t`, `0!`/`1!`)을 같은 표현으로 읽는 C 파서를 쓴다 — 오류마다 다른 코드, 망가진 파일에도 OOB 없음(ASan 증명).
- UART 를 **auto-baud** 로 디코드한다: 펄스 폭이 비트 시간의 정수배라는 사실로 비트 시간을 추정(최소 폭 군집 → low 펄스 정수배 최소제곱)하고, 프레임마다 하강 에지에 다시 맞춰 비트 중앙에서 샘플한다. 그 추정이 언제 틀리는지(샘플링 비 < 4, 1비트 폭이 없는 트래픽 = "최대공약수" 함정)도 숫자로 안다.
- SPI(모드 0–3, 워드 폭, MSB/LSB first)와 I2C(START / Sr / STOP / ADDR+R/W / ACK·NACK)를 디코드하고, I2C 에서 **같은 샘플에 찍힌 에지의 처리 순서**와 **Sr/STOP 직전 SCL↑ 샘플 취소** 규칙을 설명한다.
- 글리치·클럭 오차 ±3%·지터·샘플링 양자화·잘린 캡처·채널 뒤바뀜·SDA 규칙 위반을 주입한 캡처를 **내 시뮬레이터**(COMM-B01/B04/B06 축약판)로 만들어 "디코드 결과 == 생성한 것" 을 수천 건 단언한다.
- 디코더를 bring-up 도구로 쓴다: LA 식 protocol table, "TX 클럭이 +2% 빠르다" 같은 진단 한 줄, 다른 도구가 만든 캡처(HWLAB-B07 의 Python 생성 VCD)와 독립 Python 디코더로 교차 검증.

## 1. 문제

LA 의 내장 디코더에 baud·모드·채널을 잘못 넣으면 "쓰레기 바이트" 가 나오고, 그걸 보드 고장으로 오해하는 일이 bring-up 에서 가장 흔한 시간 낭비다 — 그래서 펌웨어·검증 면접에서 "캡처만 주고 프로토콜을 복원해 봐, baud 는 모른다" 가 나온다.

상황: EVT 보드의 MCU 가 디버그 UART(`TX`), 온도 센서 I2C(`SDA`/`SCL`, TMP117 류 0x48), 로그용 SPI NOR(`CS_N`/`SCLK`/`MOSI`)를 쓴다. 현장 엔지니어가 LA 캡처를 CSV 나 VCD 로 보내 왔다. sigrok-cli / PulseView 는 **이 맥에 설치되어 있지 않고**, CI 에서도 GUI 없이 돌아야 한다. 그래서 표준 C 만으로 된 작은 디코더 라이브러리 + CLI 를 만든다. 디코더는 동시에 **테스트 오라클**이다 — 펌웨어 회귀 테스트에서 "핀에 실제로 나간 바이트" 를 판정하는 도구가 된다.

| | HWLAB-B07 | HWLAB-M06 | **COMM-A10 (이 레슨)** |
|---|---|---|---|
| 언어/형태 | Python 로더 | sigrok 디코더 API(Python, shim) | **C 라이브러리 + CLI**, malloc 없음 |
| 입력 | .sr / CSV(Saleae) / VCD | .sr | CSV(`time_ns`/`time_ps`) / VCD 부분집합 |
| 프로토콜 | SPI (본문 mode 0, 연습 4모드) | WS2812, I2C→TMP117 | **UART(auto-baud) · SPI(4모드) · I2C** |
| 초점 | 형식·샘플링률 판정 | 디코더 스택 | 추정·견고성·오류 보고, 시뮬레이터로 검증 |

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a10p" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">캡처 파일 → 채널별 토글 목록 → (글리치 필터) → 디코더 → protocol table</text>
  <rect x="10" y="40" width="120" height="46" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="70" y="60" fill="currentColor" text-anchor="middle">CSV</text>
  <text x="70" y="77" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">time_ns,TX,SCL</text>
  <rect x="10" y="100" width="120" height="46" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="70" y="120" fill="currentColor" text-anchor="middle">VCD</text>
  <text x="70" y="137" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">#28520 1!</text>
  <rect x="170" y="56" width="190" height="74" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="265" y="76" fill="currentColor" text-anchor="middle" font-weight="bold">la_capture</text>
  <text x="265" y="94" fill="currentColor" text-anchor="middle" font-size="12">채널 = init + t[0..n) (ps)</text>
  <text x="265" y="111" fill="currentColor" text-anchor="middle" font-size="12">t[i] 뒤 레벨 = init ^ ((i+1)&amp;1)</text>
  <rect x="390" y="66" width="110" height="54" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="445" y="88" fill="currentColor" text-anchor="middle">la_deglitch</text>
  <text x="445" y="106" fill="currentColor" text-anchor="middle" font-size="12">폭 &lt; g 펄스 제거</text>
  <rect x="540" y="36" width="190" height="34" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="635" y="58" fill="currentColor" text-anchor="middle">uart_autobaud + uart_decode</text>
  <rect x="540" y="78" width="190" height="34" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="635" y="100" fill="currentColor" text-anchor="middle">spi_decode (mode, bits)</text>
  <rect x="540" y="120" width="190" height="34" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="635" y="142" fill="currentColor" text-anchor="middle">i2c_guess_roles + i2c_decode</text>
  <rect x="300" y="196" width="430" height="88" rx="6" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="312" y="216">   0   560.440 us  START</text>
    <text x="312" y="233">   1   562.320 us  ADDR     0x48 W ACK</text>
    <text x="312" y="250">   3   607.960 us  Sr</text>
    <text x="312" y="267">   6   654.800 us  DATA     0x80 NACK</text>
  </g>
  <rect x="10" y="196" width="260" height="88" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="216" fill="currentColor" font-size="12">테스트용 캡처 = 내 시뮬레이터</text>
  <text x="20" y="234" fill="currentColor" font-size="12">B01 UART · B04 SPI · B06 I2C 축약판</text>
  <text x="20" y="252" fill="currentColor" font-size="12">→ 지터·클럭 오차·글리치 주입</text>
  <text x="20" y="270" fill="currentColor" font-size="12">→ LA 샘플링(올림 양자화) → CSV/VCD</text>
  <g stroke="currentColor" fill="none" marker-end="url(#a10p)">
    <path d="M130 63 L168 82"/><path d="M130 123 L168 106"/><path d="M360 93 L388 93"/>
    <path d="M500 86 L538 54"/><path d="M500 93 L538 95"/><path d="M500 100 L538 137"/>
    <path d="M635 154 L635 194"/><path d="M140 196 L140 170 L265 170 L265 132"/>
  </g>
  <text x="300" y="188" fill="currentColor" font-size="12">LA protocol table (la_format_table)</text>
</svg>
```

**캡처 표현** (이 레슨의 정의): `la_chan = { name, init, n, t[] }`. `init` 은 캡처 시작 `t0` 의 레벨, `t[]` 는 레벨이 **바뀐** 시각만(증가). 레벨은 토글마다 뒤집히므로 값을 따로 저장하지 않는다. 시간은 **정수 피코초(ps)** — `double` 초로 들고 다니면 CSV↔VCD 왕복이 비트 단위로 같지 않다(연습 1 에서 `atof` 로 읽으면 2만 개 중 737개가 1 ps 틀린다). 샘플러는 "샘플 k(시각 k·Ts)는 그 시각 **이하**의 토글을 본다" 로 정의 → 이상 파형의 에지는 `ceil(t/Ts)·Ts` 로 밀리고, 한 샘플 간격 안의 펄스 두 에지는 상쇄된다(펄스 소실).

### Part A — 파서 · 레벨 조회 · 세 디코더

1. `la_parse_csv`(주어짐)와 `la_parse_vcd`(TODO A1): 오류 코드 `HDR / COLS / VAL / TIME / CAP / EMPTY / TSCALE / VAR / ID / XZ / INIT`. 쓰기(`la_write_csv/vcd`, 주어짐) → 읽기 왕복이 무작위 캡처 300개에서 바이트가 아니라 **토글 시각까지** 같아야 한다.
2. `la_edge_upper(c, t)`(TODO A2): `t[i] ≤ t` 인 개수 = 이진 탐색 → `la_level_at` 이 O(log n).
3. `uart_autobaud`(A3) + `uart_decode`(A4): "Hi" @115200 8N1, 10 MS/s → 표준 baud 115200 으로 스냅, 오차 < 0.5%, 바이트 정확히.
4. `spi_decode`(A5): 4모드 × (8, 12, 16 비트, 8 비트 LSB first) × 트랜잭션 30개(워드 1–4개) → 전부 일치.
5. `i2c_decode`(A6): `S 0x48+W 0x00 Sr 0x48+R 0x0C 0x80(NACK) P` (400 kHz, 지터 ±20 ns) → 이벤트 8개 정확히.

### Part B — 디코더가 거짓말하는 지점을 주입하고 단언

| # | 주입 | 기대 (테스트로 확인) |
|---|---|---|
| B-1 | baud 9600–1M × 샘플링 1–100 MS/s (OVS ≥ 8) × 클럭 오차 ±3% × 지터 ±2% T × 형식 6종, 300 캡처 9000 프레임 | 표준 baud 스냅 300/300, 디코드 300/300, auto-baud 최악 오차 < 1.5% (실측 0.684%) |
| B-2 | 8E1 에서 parity 반전 / break / stop=0 / 둘 다 | 그 프레임만 `PERR` / `BREAK|FERR` / `FERR` / `PERR|FERR`, 뒤 프레임 정상 |
| B-3 | 30 ns 글리치 20개(절반은 비트 중앙) / SCL 에 30 ns 하이 펄스 / 붙어서 오는 링잉 1–4 펄스 | 필터 없으면 auto-baud 가 글리치 폭을 비트로 착각(115200 스냅 실패)·디코드 틀림, `la_deglitch(100 ns)` 뒤 정확. 링잉 500개 채널 원복 |
| B-4 | 마스터가 SCL high 중 SDA 를 바꿈 (비트 3 / 비트 0) | `ERR(3 bits) + STOP` / **오류 없이 `Sr` + 유령 주소 0x25** (비트 0 위반은 정상 Sr 과 구별 불가) |
| B-5 | UART 프레임 중간에서 캡처 끝 / SPI CS 가 안 올라감 + 3비트 부분 워드 / CS 가 low 로 시작 / I2C 바이트 중간 | `TRUNC`, `SPI-PART(3 bits)`, `TRUNC(aux=1)`, `TRUNC(5 bits, 0x1F)` |
| B-6 | SDA/SCL 채널 순서 뒤바뀜 (무작위 I2C 300개, 스캔·쓰기·읽기 섞음) | 토글 수로 역할 추정 300/300 (두 순서 모두) |
| B-7 | 1비트 폭이 없는 트래픽: 0x00 + 프레임 사이 2비트 idle | auto-baud = **3T** (폭 9T, 3T 의 최대공약수). 연속 전송·`'U'`(0x55)면 정확 |
| B-8 | mode 3 캡처를 mode 0 으로 / mode 1 을 mode 0 으로 | 데이터 같음 + 워드마다 `CPOL?` 경고 / 데이터 틀림·경고 없음 |
| B-9 | SDA 가 SCL↓ 1 ns 뒤에 바뀜 + 4 MS/s | 같은 샘플에 찍힌 에지 302개, 그래도 가짜 START/STOP 0개 |
| B-10 | 이벤트 버퍼 3칸 | `LA_E_EVCAP`, 앞 3개 온전 |
| B-11 | CSV/VCD 텍스트를 무작위로 6000번 변형(바이트 치환·삭제·잘림) | 5633개 오류 코드, 367개 파싱 성공 — 성공한 것도 불변식 위반 0 |

### Part C — 한 캡처에 세 버스, bring-up 리포트

- 6채널 캡처(TX `AT+TEMP?\r\n` @115200 **+1.5%**, I2C TMP117 읽기 @400 kHz, SPI NOR page program @5 MHz)를 50 MS/s 로 샘플 → CSV → 파싱 → 세 디코더 전부 일치, auto-baud 가 "+1.47%" 를 읽어 냄, VCD 왕복 동일.
- `la_format_table` 골든 문자열(시각까지), 작은 버퍼에서는 snprintf 규칙으로 필요한 길이를 돌려줌.
- CLI: `./solution --emit-demo > demo_capture.vcd` 로 만든 캡처와 **HWLAB-B07 이 Python 으로 만든 SPI 캡처**를 디코드하고, `verify_vcd.py`(독립 Python 디코더)와 표를 diff.

## 2. Problem (English)

> **Protocol decoder for logic-analyzer captures.** You get a digital capture exported from a logic analyzer:
> either CSV (`time_ns,ch0,ch1,...`, one row per change, first row = initial state) or a VCD subset
> (`$timescale`, `$var wire 1 <id> <name> $end`, `#<time>`, `0<id>` / `1<id>`). Write a C library, no dynamic
> allocation, that
>
> ```c
> int la_parse_csv(const char *s, size_t len, la_capture *cap);   /* 0 or a distinct negative error */
> int la_parse_vcd(const char *s, size_t len, la_capture *cap);
> int la_deglitch(la_chan *c, int64_t min_ps);                     /* returns #pulses removed */
> int uart_autobaud(const la_chan *c, uart_est *est);              /* bit time from pulse widths */
> int uart_decode(const la_chan *c, int64_t t_end, int64_t bit_ps, uart_fmt f, la_event *ev, int cap);
> int spi_decode(const la_capture *c, int cs, int sck, int mosi, int miso, spi_fmt f, la_event *ev, int cap);
> int i2c_decode(const la_chan *sda, const la_chan *scl, int64_t t_end, la_event *ev, int cap);
> ```
>
> Decoders return the number of events (or `LA_E_EVCAP` if `cap` is exceeded) and report framing/parity errors,
> break, NACK, partial SPI words, I2C START/Sr/STOP, protocol violations and captures that end mid-frame.
>
> Example: 8N1 at 115200 baud, `'A'` = 0x41 → line bits `0 1000 0010 1` (start, LSB first, stop); a capture of
> `"Hi"` sampled at 10 MS/s must auto-detect 115200 and decode `0x48 0x69`.
>
> Follow-ups: How does auto-baud fail, and what traffic defeats it? Minimum sample rate for each protocol?
> What happens when an SDA edge and an SCL edge land in the same sample? How would you tell SDA from SCL if the
> probes were swapped? How do you trust the decoder itself?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여덟 개: A1 `la_parse_vcd`, A2 `la_edge_upper`, A3 `uart_autobaud`, A4 `uart_decode`, A5 `spi_decode`, A6 `i2c_decode`, B1 `la_deglitch`, B2 `i2c_guess_roles`. 주어진 것: 캡처 구조체, CSV 파서, 토큰 도우미(`next_tok`, `skip_to_end`, `parse_fixed`), CSV/VCD 쓰기, 이벤트 싱크, protocol table, 송신 시뮬레이터 + LA 샘플러, CLI, 하네스. 스텁은 "0 / 오류 코드 / 이벤트 0개" 를 돌려주므로 모든 루프가 끝난다. 처음 실행 (실제 출력, 일부):

```text
FAIL starter.c:748  la_parse_vcd(vcd, strlen(vcd), &G_RD) == LA_OK
FAIL starter.c:749  G_RD.nch == 2 && !strcmp(G_RD.ch[0].name, "SDA") && !strcmp(G_RD.ch[1].name, "SCL")
…
Part A: 7/20
B-1 uart random: 300 captures, 9000 frames, worst auto-baud error 100.000%
…
11/52 passed
```

(exit 1, 0.2 초 미만.) 단계별 확인: Part A 의 TODO 여섯 개만 채우면 **45/52** — 남는 실패는 글리치(B-3 4개 + 링잉 B-3b), 역할 추정(B-6), 그리고 역할 추정을 쓰는 C-1.

## 4. 힌트

<details><summary>힌트 1 — 레벨은 저장하지 않는다</summary>

토글 목록만 있으면 `level(t) = init ^ (t 이하 토글 개수 & 1)`. 개수는 `upper_bound` 이진 탐색: `lo=0, hi=n; while (lo<hi) { mid; if (t[mid] <= t) lo=mid+1; else hi=mid; }`. 같은 시각의 토글은 "이미 일어남" 으로 센다(샘플러 정의와 같아야 한다).

</details>

<details><summary>힌트 2 — VCD 는 줄이 아니라 토큰</summary>

`$var wire 1 ! TX $end` 와 `$timescale 1 ns $end` / `$timescale 1ns $end` 가 모두 합법이다. 공백으로 자른 토큰을 상태 둘(정의부 / 값 영역)로 처리하고, 모르는 `$xxx` 섹션은 `$end` 까지 건너뛴다. 값 토큰은 첫 글자가 값(`0 1 x z`), 나머지가 id(여러 글자 가능). t0 의 값은 `init`, 그 뒤는 `la_set`.

</details>

<details><summary>힌트 3 — auto-baud: 폭은 비트 시간의 정수배</summary>

① 가장 짧은 펄스 폭 `wmin`, ② `1.5·wmin` 보다 짧은 펄스들의 평균 = 1비트 후보 T, ③ 각 폭을 `k = round(w/T)` 로 나누어 `|w − kT| ≤ T/4`, `1 ≤ k ≤ 11` 인 것만 `T = Σw / Σk` (두 번). **low 펄스만** 쓰는 이유: low 는 항상 한 프레임 안(start + 0 비트들)이지만, high 는 stop 비트에 프레임 사이 idle 이 붙어 정수배가 아닐 수 있다.

</details>

<details><summary>힌트 4 — UART: 프레임마다 다시 맞춘다</summary>

하강 에지 t0 를 찾고 비트 i 를 `t0 + (2i+1)·T/2` 에서 샘플. start 중앙이 1 이면 가짜 시작(글리치)이니 다음 에지로. 프레임이 끝나면 **마지막 샘플 시각 뒤 첫 에지**부터 다시 찾는다(프레임 끝 `t0 + nb·T` 부터 찾으면, TX 가 빠를 때 다음 start 에지를 놓친다). 마지막 샘플이 `t_end` 뒤면 `TRUNC`.

</details>

<details><summary>힌트 5 — SPI: 샘플 에지 하나만 보면 된다</summary>

CPHA=0 은 leading 에서, CPHA=1 은 trailing 에서 샘플한다. CPOL=0 이면 leading = 상승, CPOL=1 이면 leading = 하강. 정리하면 **CPOL == CPHA → 상승 에지에서 샘플**. CS 하강~상승 사이의 그 에지들에서 MOSI/MISO 를 읽고, CS 하강 순간 SCK 레벨이 CPOL 과 다르면 경고.

</details>

<details><summary>힌트 6 — I2C: 같은 시각의 에지 순서, 그리고 "취소되는 비트"</summary>

두 채널 에지를 병합할 때 같은 시각이면 **SCL↓ → SDA → SCL↑**(데이터는 SCL low 동안 바뀐다는 규칙을 그대로 순서로). Sr 과 STOP 은 "SCL 을 올렸다가 SCL high 중 SDA 를 움직이는" 모양이라, 그 SCL↑ 에서 샘플한 비트는 데이터가 아니다 — START/STOP 을 만나면 이번 SCL high 에서 샘플한 비트 하나를 빼고, 그래도 남으면 그때 위반이다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 |
|---|---|
| 1. 표현 | 샘플 배열이 아니라 토글 목록(압축, O(log n) 조회), 정수 ps, LA 샘플링 = 에지가 [0, Ts) 늦게 보임 |
| 2. 입력 | CSV/VCD → 같은 구조체, 모든 길이·용량·시간 순서 검사, 오류 코드가 원인을 말함 |
| 3. 디코드 | UART: auto-baud + 프레임마다 재동기 / SPI: 샘플 에지 하나 / I2C: 병합 + 순서 규칙 + 취소 비트 |
| 4. 거짓말 | 글리치, OVS 부족, 1비트 폭 없는 트래픽, 모드 착각, 채널 뒤바뀜, 잘린 캡처 — 각각 "어떻게 알아채나" |
| 5. 검증 | 내 송신 모델로 만든 캡처에서 decoded == generated, 다른 도구가 만든 캡처, 독립 구현 diff, 변이 테스트 |

### Part A — 표현과 파서

**왜 토글 목록인가.** 100 MS/s 로 10 ms 를 잡으면 샘플은 채널당 100만 개지만, 115200 baud UART 의 토글은 수백 개다. 디코더가 하는 일은 "에지 찾기" 와 "그 시각의 레벨" 뿐이라 토글 목록이면 충분하고, 레벨 조회는 이진 탐색 한 번이다. 메모리는 채널당 `16384 × 8 B = 128 KB` 고정(펌웨어 쪽 습관대로 malloc 없음, 넘치면 `LA_E_CAP`).

**CSV 와 VCD 의 차이** (이 레슨의 정의 — 실제 LA 내보내기 형식은 도구마다 다르다, HWLAB-B07 참고):

| | CSV | VCD |
|---|---|---|
| 시간 | 첫 열 `time_ns`(소수 3자리까지) 또는 `time_ps` | `$timescale` 의 `1/10/100` × `s ms us ns ps` |
| 행/토큰 | 한 행 = 그 시각의 **모든 채널 값** (변화 없는 열도 씀) | 변한 채널만 `0id` / `1id` |
| 시작 상태 | 첫 행 | 첫 `#t` 의 값들 (`$dumpvars` 블록) |
| 끝 | 마지막 행 (쓰기 때 끝 표시 행 추가) | 마지막 `#t` |
| 거부 | 열 수 불일치 `COLS`, 0/1 아님 `VAL`, 시간 역행 `TIME`, 9채널 `CAP` | `x/z` → `XZ`, 폭 ≠ 1 → `VAR`, 모르는 id → `ID`, t0 에 값 없음 → `INIT` |

`x`(미정) 를 0 이나 1 로 바꿔 넣지 않고 거부하는 이유: LA 캡처에는 원래 `x` 가 없다. `x` 가 있다면 시뮬레이터 덤프이고, 그걸 몰래 0 으로 읽으면 "디코드됐다" 는 거짓 안심을 준다. `parse_fixed` 는 정수부 15자리를 넘거나 `ip × unit` 이 int64 의 절반을 넘으면 거부한다 — B-11 변형 테스트에서 `#99999999999999` 같은 토큰이 실제로 만들어진다(UBSan 이 오버플로를 잡는다).

**왕복 테스트(A-4).** 무작위 캡처(1–8채널, 채널당 토글 0–60개, 1/3 은 ps 해상도) 300개를 쓰고 다시 읽어 `t0 / t_end / 이름 / init / 모든 토글` 이 같은지 본다. VCD 쓰기는 모든 시각이 ns 의 배수면 `1 ns`, 아니면 `1 ps` timescale 을 고른다. 이 테스트가 파서와 쓰기를 동시에 검증하므로, 뒤의 모든 디코드 테스트는 "파일 형식" 을 의심하지 않아도 된다.

### Part A — UART: 비트 중앙 샘플과 auto-baud

```svg
<svg viewBox="0 0 740 370" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">'O' = 0x4F, 8N1: start 0 | 1 1 1 1 0 0 1 0 | stop 1 (LSB first) — 데모 캡처의 첫 프레임</text>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="108" y="44">S</text><text x="164" y="44">b0</text><text x="220" y="44">b1</text><text x="276" y="44">b2</text>
    <text x="332" y="44">b3</text><text x="388" y="44">b4</text><text x="444" y="44">b5</text><text x="500" y="44">b6</text>
    <text x="556" y="44">b7</text><text x="612" y="44">Stop</text>
  </g>
  <g stroke="currentColor" stroke-dasharray="2 4">
    <line x1="80" y1="50" x2="80" y2="130"/><line x1="136" y1="50" x2="136" y2="130"/><line x1="192" y1="50" x2="192" y2="130"/>
    <line x1="248" y1="50" x2="248" y2="130"/><line x1="304" y1="50" x2="304" y2="130"/><line x1="360" y1="50" x2="360" y2="130"/>
    <line x1="416" y1="50" x2="416" y2="130"/><line x1="472" y1="50" x2="472" y2="130"/><line x1="528" y1="50" x2="528" y2="130"/>
    <line x1="584" y1="50" x2="584" y2="130"/><line x1="640" y1="50" x2="640" y2="130"/>
  </g>
  <path d="M20 66 H80 V106 H136 V66 H360 V106 H472 V66 H528 V106 H584 V66 H720" stroke="currentColor" stroke-width="2" fill="none"/>
  <g class="f-acc" stroke="currentColor">
    <circle cx="108" cy="106" r="4"/><circle cx="164" cy="66" r="4"/><circle cx="220" cy="66" r="4"/><circle cx="276" cy="66" r="4"/>
    <circle cx="332" cy="66" r="4"/><circle cx="388" cy="106" r="4"/><circle cx="444" cy="106" r="4"/><circle cx="500" cy="66" r="4"/>
    <circle cx="556" cy="106" r="4"/><circle cx="612" cy="66" r="4"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="108" y="150">L 1T</text><text x="248" y="150">H 4T</text><text x="416" y="150">L 2T</text>
    <text x="500" y="150">H 1T</text><text x="556" y="150">L 1T</text><text x="660" y="150">H stop+idle</text>
  </g>
  <text x="10" y="172" fill="currentColor" font-size="12">● 샘플 시각 = t0 + (i+½)T (비트 중앙), t0 = start 하강 에지</text>
  <text x="10" y="190" fill="currentColor" font-size="12">데모 캡처 실측 폭(ns): L 8520 · H 34040 · L 17000 · H 8560 · L 8520 · H 8480 …</text>
  <text x="10" y="208" fill="currentColor" font-size="12">→ 폭 / T = 1.001 · 4.000 · 1.997 · 1.006 · 1.001 · 0.996 (T = 8510.9 ns)</text>
  <text x="10" y="238" fill="currentColor" font-weight="bold">LA 샘플링: 에지는 다음 샘플로 밀린다 (Ts = 40 ns, 25 MS/s)</text>
  <g stroke="currentColor">
    <line x1="40" y1="320" x2="700" y2="320"/>
    <line x1="80" y1="314" x2="80" y2="326"/><line x1="200" y1="314" x2="200" y2="326"/><line x1="320" y1="314" x2="320" y2="326"/>
    <line x1="440" y1="314" x2="440" y2="326"/><line x1="560" y1="314" x2="560" y2="326"/><line x1="680" y1="314" x2="680" y2="326"/>
  </g>
  <path d="M40 256 H262 V292 H700" stroke="currentColor" stroke-dasharray="5 3" fill="none"/>
  <path d="M40 260 H320 V296 H700" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="250" y="280" fill="currentColor" font-size="12" text-anchor="end">실제 에지</text>
  <text x="330" y="280" fill="currentColor" font-size="12">캡처된 에지 = ceil(t/Ts)·Ts</text>
  <text x="262" y="346" fill="currentColor" font-size="12" text-anchor="middle">지연 ∈ [0, Ts)</text>
  <text x="360" y="346" fill="currentColor" font-size="12">폭 오차 ∈ (−Ts, +Ts): OVS 가 작으면 ±1 샘플</text>
  <text x="40" y="342" fill="currentColor" font-size="12">↑ 샘플 격자</text>
</svg>
```

(8E1 이면 b7 다음 칸이 parity, stop 은 한 칸 뒤.)

**디코드.** 프레임 길이 `nb = 1 + data + parity + stop`. 하강 에지 t0 에서 비트 i 를 `t0 + (2i+1)T/2` 에서 샘플. start 중앙이 1 이면 가짜 시작 → 다음 에지. stop 이 0 이면 `FERR`, 프레임 전체(데이터·parity·stop)가 0 이면 `BREAK|FERR`(parity 판정은 생략 — break 는 데이터가 아니다). 프레임마다 t0 에 다시 맞추므로 **오차는 프레임 안에서만 누적**된다. 이게 UART 가 클럭 공유 없이 동작하는 이유이고(COMM-B03), LA 디코더의 허용 오차가 고정 baud 수신기보다 큰 이유이기도 하다(아래 측정 표 2).

**auto-baud 실제 추적** (데모 캡처 `TX`, 글리치 필터 100 ns 뒤, 값은 위 Python 추적과 C 출력이 일치):

| 단계 | 계산 | 결과 |
|---|---|---|
| 펄스 | 토글 26개 → 완전한 펄스 25개 | |
| ① 최소 폭 | `wmin` | 8440 ns |
| ② 1비트 군집 | `w < 1.5·wmin = 12660 ns` 인 17개 평균 | T₁ = 8 510 588 ps |
| ③ low 정수배 맞추기 | low 펄스 13개, k 합 22, 폭 합 187 240 ns | T = 187 240 000 / 22 = **8 510 909 ps** |
| 반복 | 같은 13개 → 같은 값 (수렴) | |
| 보고 | 1e12 / T = 117 496 baud → 최근접 115200, **+1.99%** | 실제 TX = 115200 × 1.02 = 117 504 (−0.007%) |

`fit on 13 of 25 pulses` 는 "high 12개(stop+idle 포함)는 맞추기에 안 썼다" 는 뜻이다. 처음에 high 도 썼을 때는 C-1 의 `AT+TEMP?` 캡처(프레임 사이 무작위 idle)에서 +1.5% 를 +1.198% 로 읽었다 — idle 이 붙은 high 폭이 ±T/4 문턱 안에 들어와 평균을 끌어내렸다. low 만 쓰자 +1.470%. **어떤 펄스가 "정수배" 라는 가정을 만족하는지** 가 추정기의 핵심이다.

### Part A — SPI: 샘플 에지 한 종류

| mode | CPOL | CPHA | idle SCK | 샘플 에지 | 데이터 바뀌는 에지 |
|---|---|---|---|---|---|
| 0 | 0 | 0 | 0 | 상승(leading) | 하강 (첫 비트는 CS↓ 직후) |
| 1 | 0 | 1 | 0 | 하강(trailing) | 상승 |
| 2 | 1 | 0 | 1 | 하강(leading) | 상승 (첫 비트는 CS↓ 직후) |
| 3 | 1 | 1 | 1 | 상승(trailing) | 하강 |

"샘플 = 상승" ⇔ CPOL == CPHA. 디코더는 CS low 구간마다 그 에지들만 골라 `la_level_at(MOSI, t)` 를 읽는다 — 값은 **그 샘플 시각의 레벨**(sigrok 과 같은 관례). 생성기는 데이터를 반대 에지 + H/8 에 바꾸므로 샘플 에지와 데이터 변화는 반 주기 떨어져 있다. 워드 폭은 1–32, LSB first 옵션, CS 가 올라갈 때 남은 비트는 `SPI-PART(k bits)`. HWLAB-B07 의 SPI 디코드가 "mode 0 + 8 비트" 였다면 여기는 일반화 + 경계 보고다.

### Part A — I2C: 병합, 순서 규칙, 취소되는 비트

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">I2C 디코드: SCL↑ = 샘플, SCL high 중 SDA↓ = START/Sr, SDA↑ = STOP</text>
  <text x="10" y="64" fill="currentColor">SCL</text><text x="10" y="150" fill="currentColor">SDA</text>
  <path d="M40 50 H100 V80 H140 V50 H180 V80 H220 V50 H250" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M40 130 H70 V160 H120 V130 H200 V160 H250" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M280 80 H310 V50 H350 V80 H400 V50 H460 V80 H500" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M280 160 H370 V130 H430 V160 H500" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M530 80 H600 V50 H730" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M530 130 H560 V160 H650 V130 H730" stroke="currentColor" stroke-width="2" fill="none"/>
  <g class="f-ok" stroke="currentColor"><circle cx="140" cy="130" r="4"/><circle cx="220" cy="160" r="4"/><circle cx="310" cy="160" r="4"/></g>
  <g class="f-warn" stroke="currentColor"><circle cx="400" cy="130" r="5"/><circle cx="600" cy="160" r="5"/></g>
  <g stroke="currentColor" stroke-dasharray="3 3"><line x1="70" y1="40" x2="70" y2="170"/><line x1="430" y1="40" x2="430" y2="170"/><line x1="650" y1="40" x2="650" y2="170"/></g>
  <g fill="currentColor" font-size="12">
    <text x="48" y="188">START</text><text x="128" y="188">A6=1</text><text x="208" y="188">A5=0</text>
    <text x="290" y="188">ACK(0)</text><text x="380" y="188">임시 비트</text><text x="420" y="204">Sr</text>
    <text x="580" y="188">임시 비트</text><text x="636" y="204">STOP</text>
  </g>
  <text x="140" y="232" fill="currentColor" font-size="12" text-anchor="middle">① START + 주소 비트</text>
  <text x="390" y="232" fill="currentColor" font-size="12" text-anchor="middle">② 9번째 클럭 ACK → Sr</text>
  <text x="630" y="232" fill="currentColor" font-size="12" text-anchor="middle">③ STOP</text>
  <text x="10" y="260" fill="currentColor" font-size="12">● 샘플(데이터)  ● 주황 = SCL↑ 에서 샘플했지만 같은 high 에서 SDA 가 움직임 → Sr/STOP 의 클럭이었으므로 bits − 1</text>
  <text x="10" y="282" fill="currentColor" font-size="12">같은 샘플 시각의 에지 처리 순서: SCL↓ → SDA → SCL↑ (UM10204: 데이터는 SCL low 동안만 바뀐다)</text>
</svg>
```

상태는 `in_tx`(START 이후), `bits`(0–8), `idx`(이번 START 이후 바이트 번호, 0 = 주소), `hi_sampled`(이번 SCL high 에서 샘플했나). 핵심 두 규칙:

1. **순서.** LA 는 SDA 변화와 SCL 하강이 1 ns 차이로 일어나도 같은 샘플에 찍는다(B-9: 4 MS/s 에서 302번). 그때 SDA 를 먼저 처리하면 "SCL=1 인 동안 SDA 변화" = 가짜 START/STOP. 같은 시각이면 SCL↓ → SDA → SCL↑ 순서로 처리하는 것은 "데이터는 SCL low 동안 바뀌고 SCL↑ 전에 안정" 이라는 규칙을 그대로 옮긴 것이다. 변이 M1(순서 규칙 삭제)은 B-9 에서 실패했다.
2. **취소.** Sr 은 `SDA↑(SCL low) → SCL↑ → SDA↓(SCL high)`, STOP 은 `SDA↓(SCL low) → SCL↑ → SDA↑`. 둘 다 SCL↑ 가 먼저 오므로 디코더는 거기서 비트를 하나 샘플한다. 그 뒤 같은 high 에서 SDA 가 움직이면 그 샘플은 데이터가 아니었던 것 → `eb = bits − hi_sampled`. 이게 없으면 모든 Sr/STOP 앞에 "1 bit 위반" 이 찍힌다(처음 짠 버전이 실제로 그랬다: `ERR SDA changed while SCL high after 1 bits` 가 Sr 과 STOP 마다).

### Part B — 위반을 보고하는 법 (B-4 추적)

0xA5 = `1010 0101` 을 보내는 마스터가 SCL high 중 SDA 를 뒤집는다 (50 ns 샘플):

| 경우 | 디코더가 본 것 | 출력 |
|---|---|---|
| 비트 3 (값 0 → 1) | 비트 0..3 샘플(1,0,1,0) 중 SDA↑ → STOP, `eb = 4 − 1 = 3` | `START`, `ADDR 0x48 W ACK`, `ERR(3 bits, 0b101)`, `STOP` — 나머지 클럭은 트랜잭션 밖이라 무시, 끝의 진짜 STOP 도 무시 |
| 비트 0 (값 1 → 0) | 비트 0 샘플(1) 중 SDA↓ → Sr, `eb = 1 − 1 = 0` → **위반 아님** | `START`, `ADDR 0x48 W ACK`, `Sr`, 남은 7비트 + ACK 클럭 = `0100 1010` → `ADDR 0x25 W ACK`(끝의 STOP 준비 클럭이 9번째), `STOP` |

두 번째 줄이 면접 포인트다: **바이트 첫 비트에서의 위반은 정상 Sr 과 파형이 같다.** 디코더는 틀린 게 아니라 정보가 없다. LA 에 "0x25 에 쓰기" 가 보이는데 버스에 0x25 장치가 없다면, 그 직전 바이트 경계에서 SDA 타이밍(tHD;DAT, tSU;DAT)을 의심하라는 신호다(HWLAB-M05 의 타이밍 판정).

### Part B — 역할 추정과 auto-baud 의 함정

**SDA/SCL 구별(B-6).** SCL 은 비트마다 2번 토글한다(9비트 = 18). SDA 는 SCL low 구간마다 많아야 1번 + START/STOP 의 SCL high 중 변화뿐이라, 바이트 k 개 트랜잭션에서 대략 `SDA ≤ 9k + 3 < 18k ≈ SCL`(k ≥ 1). 그래서 "토글이 많은 쪽 = SCL". 주소만 보내고 NACK 받는 스캔(k = 1)이 가장 아슬하지만 300/300. 같으면(예: 트래픽 없음) 판단 불가 `1` 을 돌려준다 — 추측으로 틀린 디코드를 내는 것보다 낫다.

**최대공약수 함정(B-7).** auto-baud 가 찾는 것은 "모든 폭을 정수배로 설명하는 가장 큰 T" 다. 0x00 을 2비트 idle 을 두고 보내면 low 폭은 9T(start + 0×8), high 폭은 3T(stop + idle 2) → 둘 다 3T 의 배수 → **3T** 로 추정하고, 이건 틀린 게 아니라 데이터로는 구별할 수 없는 것이다. 같은 0x00 이라도 연속 전송이면 high = stop 1T 가 생겨 정확. 실무 대책: 1비트 폭이 확실히 있는 문자(`'U'` = 0x55 는 모든 폭이 1T)를 보내게 하거나, 표준 baud 로 스냅한 결과가 ±5% 밖이면 경고. STM32 USART 의 하드웨어 auto baud rate detection 도 start 비트 폭 / 특정 문자(0x7F, 0x55 등) 패턴을 전제로 하는 모드들이 있다(모드 목록·조건은 RM 확인 권장).

**SPI 모드 착각(B-8).** mode 3 를 mode 0 으로 디코드하면 둘 다 상승 에지 샘플이라 **데이터는 맞고**, CS↓ 순간 SCK=1 이라 `CPOL?` 경고만 뜬다. mode 1 을 mode 0 으로 디코드하면 경고 없이(CPOL 같음) 데이터가 틀린다 — 데이터가 바뀌는 에지에서 샘플했으니. 연습 2 는 MOSI 변화가 leading/trailing 중 어디 직후에 오는지로 CPHA 까지 추정한다.

### 측정 — `demo_autobaud.c` (실측)

```bash
cc -std=c11 -Wall -Wextra -O2 demo_autobaud.c -o demo_autobaud && ./demo_autobaud     # 레슨 폴더에서 (solution.c 를 include)
```

표 1 — auto-baud 오차 vs OVS(= 샘플링률 / baud), fs = 10 MS/s, 8N1, 캡처당 무작위 20바이트, 프레임 사이 idle U[0, 3T], 지터 ±1% T, 행마다 300 캡처:

| OVS | 클럭 오차 | 중앙값 \|err\| | p99 \|err\| | 최대 \|err\| | 전부 디코드된 캡처 |
|---|---|---|---|---|---|
| 2.5 | 0 / ±3% | 20.000% / 19.840% | 20.000% / 22.328% | 20.000% / 22.400% | 0% / 0% |
| 3 | 0 / ±3% | 0.000% / 2.450% | 33.333% / 33.793% | 33.333% / 34.233% | 66.0% / 55.7% |
| 4 | 0 / ±3% | 0.253% / 0.316% | 0.980% / 1.374% | 1.429% / 1.661% | 100% / 100% |
| 5 | 0 / ±3% | 0.194% / 0.281% | 0.792% / 1.234% | 1.000% / 1.372% | 100% / 100% |
| 8 | 0 / ±3% | 0.125% / 0.177% | 0.515% / 0.840% | 0.619% / 1.055% | 100% / 100% |
| 16 | 0 / ±3% | 0.073% / 0.141% | 0.375% / 0.537% | 0.438% / 0.733% | 100% / 100% |
| 32 | 0 / ±3% | 0.064% / 0.078% | 0.250% / 0.283% | 0.287% / 0.421% | 100% / 100% |
| 100 | 0 / ±3% | 0.039% / 0.044% | 0.154% / 0.163% | 0.179% / 0.264% | 100% / 100% |

(6, 12 행은 생략 — 데모 출력에 있다: OVS 6 ±3% p99 0.940%, OVS 12 ±3% p99 0.711%.)

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">auto-baud p99 오차 (클럭 오차 ±3%, demo_autobaud.c 실측) — OVS 3 이하는 축 밖</text>
  <g stroke="currentColor"><line x1="70" y1="260" x2="730" y2="260"/><line x1="70" y1="50" x2="70" y2="260"/></g>
  <g stroke="currentColor" stroke-dasharray="2 4">
    <line x1="70" y1="193" x2="730" y2="193"/><line x1="70" y1="127" x2="730" y2="127"/><line x1="70" y1="60" x2="730" y2="60"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="end">
    <text x="64" y="264">0%</text><text x="64" y="197">0.5%</text><text x="64" y="131">1.0%</text><text x="64" y="64">1.5%</text>
  </g>
  <g stroke="currentColor">
    <rect x="86" y="60" width="40" height="200" class="f-warn"/>
    <rect x="152" y="60" width="40" height="200" class="f-warn"/>
    <rect x="218" y="77" width="40" height="183" class="f-acc"/>
    <rect x="284" y="96" width="40" height="164" class="f-acc"/>
    <rect x="350" y="135" width="40" height="125" class="f-acc"/>
    <rect x="416" y="148" width="40" height="112" class="f-acc"/>
    <rect x="482" y="165" width="40" height="95" class="f-acc"/>
    <rect x="548" y="188" width="40" height="72" class="f-acc"/>
    <rect x="614" y="222" width="40" height="38" class="f-acc"/>
    <rect x="680" y="238" width="40" height="22" class="f-acc"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="106" y="54">22.3%</text><text x="172" y="54">33.8%</text><text x="238" y="72">1.37</text><text x="304" y="91">1.23</text>
    <text x="370" y="130">0.94</text><text x="436" y="143">0.84</text><text x="502" y="160">0.71</text><text x="568" y="183">0.54</text>
    <text x="634" y="217">0.28</text><text x="700" y="233">0.16</text>
    <text x="106" y="278">2.5</text><text x="172" y="278">3</text><text x="238" y="278">4</text><text x="304" y="278">5</text>
    <text x="370" y="278">6</text><text x="436" y="278">8</text><text x="502" y="278">12</text><text x="568" y="278">16</text>
    <text x="634" y="278">32</text><text x="700" y="278">100</text>
  </g>
  <text x="400" y="300" fill="currentColor" font-size="12" text-anchor="middle">OVS = 샘플링률 / baud. 디코드 성공: OVS 2.5 → 0%, 3 → 55.7%, 4 이상 → 100%</text>
</svg>
```

해석: OVS 3 에서 비트 하나가 2, 3, 4 샘플로 흔들리면 최소 폭이 2 샘플 → "1.5배 군집" 이 {2} 만 잡아 T = 2/3 배로 굳는다(33% 오차 = 정확히 2/3). OVS 2.5 는 폭이 2 또는 3 샘플 → T = 2 샘플(20%). **OVS ≥ 4 부터 추정이 안정**하고, 그 위로는 지터·양자화 평균화로 오차가 줄어든다. 추정 오차 1% 대는 디코드에는 무해하다 — 표 2 가 보여 주듯 프레임마다 재동기하는 디코더는 ±3.5% 이상을 견딘다.

표 2 — auto-baud 없이 **공칭 baud** 로 디코드, TX 클럭 오차별 성공 캡처 수 / 100 (fs = 10 MS/s, 연속 프레임):

| 형식 OVS | −7 | −6 | −5 | −4 | −3…+3 | +4 | +5 | +6 | 이론 (n = 마지막 비트 번호) |
|---|---|---|---|---|---|---|---|---|---|
| 8N1 8 | 0 | 0 | 100 | 100 | 100 | 0 | 0 | 0 | −5.26% .. +3.90% |
| 8N1 100 | 0 | 0 | 100 | 100 | 100 | 100 | 100 | 0 | −5.26% .. +5.15% |
| 8E1 8 | 0 | 0 | 6 | 100 | 100 | 0 | 0 | 0 | −4.76% .. +3.53% |
| 8E1 100 | 0 | 0 | 0 | 100 | 100 | 100 | 0 | 0 | −4.76% .. +4.66% |
| 8N2 8 | 0 | 0 | 100 | 100 | 100 | 0 | 0 | 0 | −4.76% .. +3.53% |

이론: 비트 n 의 샘플 `δ + (n+½)Tn` 이 실제 비트 n 구간 `[n·Ta, (n+1)·Ta)` 안에 있어야 한다(Ta = Tn/(1+e), δ ∈ [0, Ts) 는 start 에지 검출 지연). 느린 TX(e < 0): `(n+½) > n/(1+e)` → `e > −1/(2n+1)`. 빠른 TX(e > 0): 최악 δ = Ts 에서 `(n+½+1/OVS) < (n+1)/(1+e)`. **비대칭**인 이유는 샘플링 지연 δ 가 항상 "늦게" 쪽이라서다. 예외 둘: 8N2 의 −5% 가 통과하는 것은 두 번째 stop 샘플이 첫 stop 비트 안에 떨어져도 값이 같은 1 이기 때문이고, 8E1 OVS 8 의 −5% 에서 6개가 통과한 것도 넘어간 이웃 비트가 우연히 같은 값인 프레임들이다. COMM-B03 의 16x 수신기가 이보다 좁은 것은 3샘플 다수결 창이 비트 중앙 양쪽을 차지하기 때문.

표 3 — 클럭 한 주기당 샘플 수에 따른 SPI(모드 0–3 순환, 4바이트, 성공/400) / I2C(TMP117 류 읽기, 데이터 변화 = SCL low 중앙, 성공/200):

| 샘플/주기 | 2 | 3 | 4 | 5 | 6 | 8 | 10 | 16 |
|---|---|---|---|---|---|---|---|---|
| SPI | 383 | 400 | 400 | 400 | 400 | 400 | 400 | 400 |
| I2C | 0 | 96 | 194 | 200 | 200 | 200 | 200 | 200 |

SPI 는 "샘플 에지와 데이터 변화가 반 주기 떨어져 있다" 만 지켜지면 되므로 주기당 3샘플에서 이미 완벽하다(2샘플은 에지 사이 샘플이 하나뿐이라 지터에 따라 깨짐). I2C 는 START/STOP 이 **SCL high 의 일부 구간에서** SDA 가 움직이는 것이고 데이터 변화는 SCL low 의 중앙이라, 한 주기를 4등분 이상 해야 사건 순서가 보존된다 → 이 모델에서 5 이상. 일반 지침 "클럭의 4배 이상, 가능하면 10배" 가 숫자로 나온다(실제 버스의 rise time·tHD;DAT 에 따라 달라진다 — HWLAB-B07 의 샘플링률 판정 참고).

### Part C — bring-up: 데모 캡처, 남의 캡처, 독립 구현

`./solution --emit-demo > demo_capture.vcd` 가 만든 파일(레슨 폴더에 포함, 25 MS/s, TX 가 +2% 빠름, idle 중 40 ns 글리치 하나, I2C TMP117 읽기) 앞부분:

```text
$version COMM-A10 la_write_vcd $end
$timescale 1 ns $end
$scope module la $end
$var wire 1 ! TX $end
$var wire 1 " SCL $end
$var wire 1 # SDA $end
$upscope $end
$enddefinitions $end
#0
$dumpvars
1!
1"
1#
$end
#20000
0!
#28520
1!
…
#410440
0!
#410480
1!
```

글리치 필터 없이 디코드하면 (실제 출력):

```text
# demo_capture.vcd: 3 channels, 0 .. 700400 ns
# auto-baud: bit 40000 ps = 25000000 baud (nearest 3000000, +733.33%), fit on 1 of 27 pulses
   0       20.000 us  UART     0x00 '.' BREAK
   1       62.560 us  UART     0x00 '.' BREAK
…
```

"25 Mbaud, 27개 중 1개만 맞음" — 추정기가 스스로 "나를 믿지 마라" 를 말하고 있다(40 ns = 1 샘플짜리 글리치 하나가 `wmin`). `glitch=100` 을 주면:

```text
$ ./solution demo_capture.vcd uart ch=TX glitch=100
# demo_capture.vcd: 3 channels, 0 .. 700400 ns
# auto-baud: bit 8510909 ps = 117496 baud (nearest 115200, +1.99%), fit on 13 of 25 pulses
   0       20.000 us  UART     0x4F 'O'
   1      105.120 us  UART     0x4B 'K'
   2      190.200 us  UART     0x0D '.'
   3      275.360 us  UART     0x0A '.'
$ ./solution demo_capture.vcd i2c
# demo_capture.vcd: 3 channels, 0 .. 700400 ns
# i2c roles: SDA=SDA SCL=SCL (by edge count)
   0      560.440 us  START
   1      562.320 us  ADDR     0x48 W ACK
   2      584.800 us  DATA     0x00 ACK
   3      607.960 us  Sr
   4      609.800 us  ADDR     0x48 R ACK
   5      632.320 us  DATA     0x0C ACK
   6      654.800 us  DATA     0x80 NACK
   7      677.920 us  STOP
```

bring-up 엔지니어가 읽는 법: `+1.99%` 는 "MCU 의 UART 클럭원(예: 내부 RC)이 2% 빠르다" — 수신 측이 공칭 baud 의 고정 수신기라면 COMM-B03 기준으로 아슬아슬한 값이니 클럭 트림/외부 크리스털을 확인한다. `0x0C80` 은 TMP117 형식(7.8125 m°C/LSB, 데이터시트 확인 권장)으로 25.000 °C (연습 4).

**남이 만든 캡처.** HWLAB-B07 의 `gen_capture.py`(Python, 100 MS/s, 지터 0.7 ns, MOSI clock-to-out 7 ns)가 만든 `evt1_spi_100M.vcd` 를 그대로 복사해 `hwlab_b07_evt1_spi_100M.vcd` 로 두었다. 내 코드와 아무것도 공유하지 않는 생성기다:

```text
$ ./solution hwlab_b07_evt1_spi_100M.vcd spi cs=CS_N sck=SCLK mosi=MOSI miso=MISO mode=0
# hwlab_b07_evt1_spi_100M.vcd: 4 channels, 0 .. 18740 ns
   0        0.570 us  SPI      MOSI 0x9F MISO 0xFF
   1        1.370 us  SPI      MOSI 0x00 MISO 0xEF
   2        2.170 us  SPI      MOSI 0x00 MISO 0x40
   3        2.970 us  SPI      MOSI 0x00 MISO 0x18
   4        5.630 us  SPI      MOSI 0x05 MISO 0xFF
   5        6.430 us  SPI      MOSI 0x00 MISO 0x00
   6        9.130 us  SPI      MOSI 0x03 MISO 0xFF
…
  10       12.330 us  SPI      MOSI 0x00 MISO 0x42
…
  17       17.930 us  SPI      MOSI 0x00 MISO 0x31
```

JEDEC ID `9F → EF 40 18`, RDSR `05 → 00`, READ `03 00 00 00 → 42 52 44 49 44 3D 30 31` = `"BRDID=01"` — `gen_capture.py` 의 `TRANSACTIONS` 정의와 바이트 단위로 같다. `mode=1` 로 잘못 주면 `MOSI 0x02 MISO 0xFE`, `0x84 0x94 …` 처럼 한 비트 밀린 쓰레기가 나온다(실측) — "디코더 설정 오류는 보드 고장처럼 보인다" 의 실례.

**독립 구현과 diff.** `verify_vcd.py` 는 표준 라이브러리만 쓰는 Python 디코더로, auto-baud 를 일부러 다른 방법(최소 폭으로 k 를 정하고 low 펄스 `w/k` 의 **중앙값**)으로 구한다. 세 캡처의 표를 비교한 실제 결과:

```text
c1.txt:# auto-baud: bit 8510909 ps = 117496 baud (nearest 115200, +1.99%), fit on 13 of 25 pulses
p1.txt:# python auto-baud: 117371 baud (median of 13 low pulses)
identical: 4 rows          (uart, glitch 100 ns)
identical: 8 rows          (i2c)
spi tables identical (18 rows)   (HWLAB-B07 capture, mode 0)
```

추정값은 다르지만(117 496 vs 117 371, 실제 117 504) 디코드된 표는 같다 — 표 2 의 허용 폭 안이면 추정기의 차이는 결과를 바꾸지 않는다. 디코더를 믿는 근거는 이렇게 세 겹이다: ① 내 생성기로 만든 수천 건, ② 남의 생성기가 만든 캡처, ③ 독립 구현과의 diff. 그리고 변이 테스트(아래 7. 참고)가 하네스 자체를 검증한다.

### 복잡도와 메모리

| 함수 | 시간 | 메모리 |
|---|---|---|
| 파서 | O(파일 길이) | 캡처 구조체 고정 (8 × 128 KB) |
| `la_level_at` | O(log n) | — |
| `la_deglitch` | O(n), 제자리 | — |
| `uart_autobaud` | O(n) × 3 패스 | — |
| `uart_decode` | O(frames × bits × log n) | 이벤트 = 호출자 버퍼 |
| `spi_decode` / `i2c_decode` | O(n log n) / O(n_sda + n_scl) | 이벤트 = 호출자 버퍼 |

`solution.c` 전체 52 검사 약 0.15 초(ASan/UBSan 빌드 약 0.25 초), `practice.c` 약 0.1 초.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
```

```text
Part A: 20/20
B-1 uart random: 300 captures, 9000 frames, worst auto-baud error 0.684%
B-11 mutated files: 5633 rejected with an error code, 367 parsed, 0 invariant violations
Part B: 47/47
Part C: 52/52
52/52 passed
```

(`Part X: a/b` 는 누적 개수.) 같은 파일이 CLI 이기도 하다 — 인자를 주면 테스트 대신 파일을 디코드한다(머리 주석 참고). `practice.c` 와 `demo_autobaud.c` 는 `#define LA_NO_MAIN` 후 `#include "solution.c"` 로 라이브러리 부분만 가져다 쓴다.

## 7. 자주 하는 실수

1. **샘플 배열로 들고 다니기 / float 초.** 100 MS/s × 1 s = 1억 샘플. 토글 목록 + 정수 ps 면 메모리는 에지 수에 비례하고 CSV↔VCD 왕복이 정확하다. `(int64_t)(atof("0.000000510") * 1e12)` 류 변환은 2만 개 중 737개가 틀렸다(연습 1).
2. **auto-baud = 최소 폭.** 글리치 하나(데모 캡처의 40 ns)가 25 Mbaud 를 만든다. 변이 M3(정수배 맞추기 생략, `T = wmin`)은 A-5·B-1·C-1 에서 실패했다(52 중 47). 고친 것: 글리치 필터 + 군집 + 정수배 최소제곱 + "몇 개가 맞았나" 보고.
3. **high 펄스까지 정수배로 맞추기.** stop + idle 이 붙은 high 는 정수배가 아니다 — +1.5% 를 +1.198% 로 읽었다. low 만 쓰면 +1.470%.
4. **다음 프레임을 프레임 끝(`t0 + nb·T`)부터 찾기.** TX 가 빠르면 다음 start 에지가 그보다 먼저 와서 통째로 놓친다. 변이 M7 은 A-5·B-1·B-3 에서 실패(52 중 49). 고친 것: `i = la_edge_upper(c, last_sample)`.
5. **I2C 에지를 그냥 시각순으로.** 같은 샘플에 SCL↓ 와 SDA 변화가 찍히면 가짜 START/STOP. 순서 규칙 `SCL↓ → SDA → SCL↑`. 그리고 Sr/STOP 직전 SCL↑ 의 "임시 비트" 를 취소하지 않으면 모든 Sr/STOP 이 위반으로 찍힌다(변이 M5: A-7·B-3·B-4·B-9·C-1·C-2, 52 중 44).
6. **글리치 필터를 "직전 입력 에지" 와 비교.** 링잉처럼 펄스가 붙어 오면 진짜 에지까지 지운다. 비교 대상은 **직전에 남긴** 토글이어야 한다. 변이 M6 은 처음 하네스를 통과했다 — 링잉 테스트(B-3b, 500 채널)를 추가하고서야 잡혔다. 하네스도 변이로 검증해야 하는 이유.
7. **SPI 샘플 에지를 CPHA 만으로 결정.** CPOL 이 1 이면 leading 이 하강이다 → `sample_rise = (CPOL == CPHA)`. 변이 M4(부호 반대)는 A-6·B-8·C-1 에서 실패(52 중 49).

변이 실행 결과(스크래치 사본에서, 레슨 폴더에는 남기지 않음, 52 검사 기준): M1 순서 규칙 삭제 → 51 (B-9), M2 비트 시작 + T/8 에서 샘플 → 50 (B-1·B-3), M3 최소 폭 → 47, M4 SPI 에지 반대 → 49, M5 취소 없음 → 44, M6 입력 에지 비교 → 51 (B-3b, 링잉 테스트 추가 전에는 통과), M7 프레임 끝부터 탐색 → 49. 전부 exit 1.

## 8. 면접 Q&A

<details><summary>Q1. baud 를 모르는 UART 캡처에서 baud 를 어떻게 알아내죠? 언제 틀립니까?</summary>

모든 펄스 폭은 비트 시간의 정수배다. 최소 폭 근처 군집으로 1비트 후보를 잡고, 각 폭을 정수 k 로 나눠 맞는 것들로 `T = Σw/Σk` 를 다시 구한다(최소제곱). 프레임 사이 idle 이 섞이는 high 펄스는 빼고 low 펄스로 맞춘다. 틀리는 경우 셋: ① 글리치 — 최소 폭이 망가짐 → 필터 먼저, "몇 개 펄스가 맞았나" 로 자가 진단, ② OVS < 4 — 폭이 ±1 샘플 흔들려 군집이 무너짐(실측 OVS 3 에서 33% 오차), ③ 1비트 폭이 없는 트래픽 — 0x00 + 2비트 idle 이면 9T 와 3T 의 최대공약수 3T 를 답한다. ③은 정보 부족이라 알고리즘으로 못 고친다 — 보정 문자(0x55)를 요청하거나 표준 baud 목록과의 거리로 경고.

</details>

<details><summary>Q2. 고정 baud 로 디코드할 때 TX 클럭 오차를 얼마나 견디나요? 왜 비대칭이죠?</summary>

프레임마다 하강 에지에 맞추므로 오차는 프레임 안에서만 쌓인다. 마지막 비트 n 의 샘플 `(n+½)Tn + δ` 가 실제 비트 n 안에 있어야 하므로 느린 쪽 `e > −1/(2n+1)`(8N1: −5.26%), 빠른 쪽 `(n+½+δ/T)(1+e) < n+1`. δ(에지 검출 지연)는 [0, Ts) 로 항상 늦은 쪽이라 빠른 TX 쪽이 좁다: 8N1 OVS 8 에서 +3.90%, OVS 100 에서 +5.15%. 실측 표가 −5%..+3% / −5%..+5% 로 일치했다. 16x 다수결 수신기(COMM-B03)는 샘플 창이 넓어 더 좁다.

</details>

<details><summary>Q3. 프로토콜별 최소 샘플링률은?</summary>

실측(이 모델): UART auto-baud 는 OVS ≥ 4 에서 안정, 고정 baud 디코드는 OVS 가 클수록 빠른 쪽 허용이 넓어짐. SPI 는 샘플 에지와 데이터 변화가 반 주기 떨어져 있어 주기당 3샘플에서 400/400, I2C 는 START/STOP 이 SCL high 안의 SDA 변화라 주기당 5샘플에서야 200/200. 실무 답: "가장 빠른 클럭의 4배 이상, 타이밍을 재려면 10배 이상, 그리고 rise time 과 hold time 이 샘플 간격보다 짧으면 사건 순서가 뒤집힐 수 있다". 캡처가 그 조건을 못 만족하면 DUT 판정 전에 캡처를 INVALID 로 — HWLAB-B07 의 리포트 방식.

</details>

<details><summary>Q4. SDA 와 SCL 에지가 같은 샘플에 찍히면 어떻게 처리하죠?</summary>

규칙을 순서로 옮긴다: 같은 시각이면 SCL↓ → SDA → SCL↑. "데이터는 SCL low 동안 바뀌고 SCL↑ 전에 안정" 이니 SCL 하강 직후 바뀐 SDA 는 low 구간 변화로, SCL 상승과 같이 찍힌 SDA 는 상승 전 변화(셋업)로 해석한다. 이 규칙이 없으면 4 MS/s 의 100 kHz 버스에서 302번의 동률 중 일부가 가짜 START/STOP 이 된다(변이 M1 로 확인). 진짜 위반(SCL high 중간의 SDA 변화)은 동률이 아니라 다른 샘플에 찍히므로 여전히 잡힌다.

</details>

<details><summary>Q5. LA 에 SDA high-중 변화가 찍혔는데 디코더가 "Sr" 이라고만 합니다. 왜죠?</summary>

바이트 첫 비트가 1 이고 SCL high 중 SDA 가 떨어지면, 파형은 정상 repeated START(SDA↑ in low → SCL↑ → SDA↓ in high)와 같다. 디코더는 그 SCL↑ 샘플을 Sr 의 클럭으로 취소하므로 오류가 아니다. 그 뒤 비트들이 새 주소 바이트로 읽혀 유령 주소(B-4: 0x25)가 나온다. 단서는 "버스에 없는 주소" 와 "Sr 앞에 쓰기 데이터가 덜 끝남" — 타이밍(tSU;DAT, tHD;DAT)과 펌웨어의 GPIO 비트뱅 순서를 의심한다. 비트 1 이상에서의 위반은 `ERR(k bits)` 로 잡힌다.

</details>

<details><summary>Q6. 프로브를 뒤바꿔 꽂았는지 캡처만으로 알 수 있나요?</summary>

I2C 는 가능하다: SCL 은 비트당 토글 2번(바이트당 18), SDA 는 SCL low 마다 많아야 1번 + START/STOP → 토글 수가 많은 쪽이 SCL. 주소만 보내는 스캔에서도 성립(300/300). SPI 는 CS(트랜잭션당 2번) vs SCK(비트당 2번)는 쉽지만 MOSI/MISO 는 프로토콜 지식(명령 바이트가 MOSI 에 있다, 예: 0x9F)이 있어야 한다. UART TX/RX 는 둘 다 디코드해서 어느 쪽이 명령/응답인지 본다. 판단 불가면 추측하지 말고 "ambiguous" 를 보고한다.

</details>

<details><summary>Q7. 디코더 자체가 맞다는 걸 어떻게 믿죠?</summary>

세 겹: ① 내가 만든 송신 모델(B01/B04/B06 축약판)에 지터·오차·글리치·양자화를 넣어 "decoded == generated" 를 수천 건(9000 UART 프레임, 16 SPI 조합, 수백 I2C 트랜잭션), ② 다른 사람이 다른 언어로 만든 캡처(HWLAB-B07 Python 생성기의 JEDEC/READ)를 디코드해 그쪽 정의와 비교, ③ 공유 코드 없는 독립 구현(verify_vcd.py, 다른 auto-baud 추정기)과 표 diff. 마지막으로 하네스가 버그를 잡는지 변이 7개로 확인 — 한 개(M6)는 처음에 살아남아 테스트를 추가했다.

</details>

<details><summary>Q8. 이 디코더를 펌웨어 CI 에 넣는다면? 실제 LA/sigrok 과는 뭐가 다르죠?</summary>

HIL 랙에서 LA(또는 MCU 의 다른 타이머 캡처)를 트리거해 VCD/CSV 를 저장하고, 이 CLI 로 표를 만들어 골든 파일과 비교한다 — "펌웨어가 의도한 바이트가 핀에 나갔다" 를 판정. 시간 열은 지터가 있으니 비교에서 빼거나 허용 폭을 둔다(COMM-A09 의 record/replay 와 짝). sigrok(libsigrokdecode)은 샘플 스트림 위에서 `wait()` 조건으로 동작하는 Python 디코더 스택이고(HWLAB-M06), 수많은 프로토콜과 스택 디코더(I2C→센서)가 있다. 이 레슨의 C 디코더는 토글 목록 기반이라 작고 빠르며 malloc 이 없어 MCU 쪽(예: 타이머 input capture 로 다른 보드의 UART 를 감시하는 테스트 펌웨어)에도 옮길 수 있다. sigrok-cli 는 이 맥에 없어 출력 비교는 못 했다.

</details>

## 9. 추가 연습

### 연습 1 — Saleae 식 초 단위 시각을 정수 ps 로

`"0.000000510"`, `"-0.000001"`(트리거 이전) 같은 초 단위 문자열을 float 없이 정수 ps 로 읽는 `parse_seconds_ps()` 를 짜라(소수 12자리까지, 지수 표기 거부). ns 해상도 무작위 시각 2만 개를 소수 9자리로 쓰고 다시 읽어 전부 같은지, `(int64_t)(atof(s) * 1e12)` 는 몇 개 틀리는지 세라.

<details><summary>답</summary>

정수부와 소수부를 따로 정수로 읽고, 소수부를 12자리가 될 때까지 10을 곱하면 그대로 ps 다: `v = ip·10¹² + fp·10^(12−nf)`. 실측: 정확 20000/20000, `atof` 방식은 **737개** 틀림 — `0.000000510` 은 이진수로 정확히 표현되지 않아 `×1e12` 결과가 509999.99… 가 되고 정수 변환이 버린다. `llround` 로 반올림하면 이 범위에서는 맞겠지만, 초 단위 정수부가 커지면(긴 캡처) double 의 53비트 가수가 ps 해상도를 못 담는다(2^53 ps ≈ 2.5 시간 — 그보다 긴 캡처에서 ps 단위가 깨진다). 시간은 정수로.

</details>

### 연습 2 — SPI 모드 추정

CS/SCK/MOSI 만 보고 모드를 추정하는 `spi_guess_mode()` 를 짜라. CPOL 은 CS↓ 순간 SCK 레벨, CPHA 는 MOSI 변화가 leading 에지 직후(CPHA=1)인지 trailing 직후·첫 에지 이전(CPHA=0)인지 다수결. 4모드 200 캡처에서 맞히고, MOSI 가 한 번도 안 바뀌면(읽기 더미 0xFF) `-1`.

<details><summary>답</summary>

MOSI 변화 시각마다 "CS↓ 이후 그 시각까지의 SCK 에지 수 ne" 를 센다(`la_edge_upper` 두 번). `ne == 0` 또는 마지막 에지가 trailing(`ne−1` 홀수) → CPHA 0 표, leading 직후 → CPHA 1 표. 실측 200/200, MOSI 상수 → −1. 데이터가 샘플 에지가 아닌 쪽에서 바뀐다는 것이 SPI 의 정의이므로, "어느 에지 직후에 바뀌나" 가 곧 CPHA 다. 한계: MOSI 변화가 없으면 정보가 없다(MISO 로도 해 볼 수 있다 — 슬레이브 출력도 같은 규칙).

</details>

### 연습 3 — UART 형식 추정

baud 를 안 상태에서 8N1 / 8E1 / 8O1 / 7E1 / 7O1 중 무엇인지 고르는 `uart_guess_fmt()` 를 짜라. 각 정답 형식으로 무작위 32바이트(연속 프레임)를 50번씩, 250/250 을 맞혀라.

<details><summary>답</summary>

parity 형식 4개로 각각 디코드해서 PERR/FERR 가 0 인 것이 **정확히 하나**면 그것, 아니면 8N1. 무작위 데이터에서 틀린 parity 형식이 32프레임 연속 오류 0 일 확률은 2^−32 정도라 "오류 0" 은 강한 증거다. 실측 250/250. 주의: 7E1 프레임(10비트)은 8N1 로 디코드해도 오류가 안 난다(bit7 자리에 parity 가 있을 뿐) — 그래서 parity 후보를 먼저 보고, 8N1 은 "다른 게 다 안 맞을 때" 의 기본값이다. 8N1 과 8N2 는 프레임 사이 idle 이 있으면 구별할 수 없다.

</details>

### 연습 4 — I2C 이벤트를 i2ctransfer 식 요약으로

이벤트 목록을 트랜잭션(START..STOP)당 한 줄 `w1@0x48 0x00 r2@0x48 0x0C 0x80` 로 요약하고(주소 NACK 은 `w0@0x50 NACK`), TMP117(0x48) reg 0x00 을 2바이트 읽은 줄에는 온도를 붙여라 (7.8125 m°C/LSB, 부호 있음 — 데이터시트 확인 권장).

<details><summary>답</summary>

실측 출력:

```text
w0@0x50 NACK
w3@0x48 0x01 0x02 0x20
w1@0x48 0x00 r2@0x48 0x0C 0x80 => 25.000 C
w1@0x48 0x00 r2@0x48 0xFE 0x00 => -4.000 C
```

`ADDR` 에서 새 조각 시작, `DATA` 를 모으고, `Sr`/`STOP` 에서 조각을 출력. 온도는 `(int16_t)(d0<<8 | d1) × 78125 / 10000` m°C (0x0C80 = 3200 → 25000, 0xFE00 = −512 → −4000). Linux `i2ctransfer` 의 `w1@0x48 0x00 r2@0x48` 문법과 같게 두면, LA 에서 본 것을 그대로 셸 명령으로 재현할 수 있다 — bring-up 에서 "캡처 → 재현 명령" 이 한 줄로 이어진다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice      # 레슨 폴더에서 (solution.c 를 include)
```

```text
ex1 20000 ns-resolution times: exact 20000, (int64)(atof*1e12) wrong 737
ex2 spi mode guess: 200/200, constant MOSI -> -1
ex3 uart format guess (32 random bytes, back-to-back): 250/250
w0@0x50 NACK
w3@0x48 0x01 0x02 0x20
w1@0x48 0x00 r2@0x48 0x0C 0x80 => 25.000 C
w1@0x48 0x00 r2@0x48 0xFE 0x00 => -4.000 C
9/9 passed
```

독립 Python 디코더 교차 검증(선택):

```bash
./solution demo_capture.vcd uart ch=TX glitch=100 | grep -v '^#' > c.txt   # demo_capture.vcd = ./solution --emit-demo 출력
python3 -I verify_vcd.py demo_capture.vcd uart TX 100 | grep -v '^#' > p.txt && diff c.txt p.txt && echo identical
```

## 10. 핵심 정리

- 캡처 = 채널별 `init + 토글 시각(정수 ps)`. 레벨 조회는 이진 탐색, LA 샘플링은 에지를 `ceil(t/Ts)·Ts` 로 미루고 한 샘플 안의 펄스를 지운다.
- CSV(행 = 전체 상태)와 VCD(변화만, `$timescale`)를 같은 구조체로. 오류마다 다른 코드, `x/z`·벡터는 거부, 변형 6000건에도 OOB·불변식 위반 0.
- UART: 프레임마다 하강 에지에 재동기, 비트 i 는 `t0 + (i+½)T`. 고정 baud 허용 오차는 비대칭: `−1/(2n+1)` .. `(n+1)/(n+½+1/OVS) − 1` (8N1: −5.26% .. +3.90% @OVS 8).
- auto-baud = 펄스 폭의 정수배 최소제곱(low 펄스만, 글리치 필터 먼저). OVS ≥ 4 에서 안정(p99 < 1.4%), OVS 3 은 33% 틀림. 1비트 폭이 없으면 최대공약수를 답한다(0x00+idle → 3T).
- SPI: 샘플 에지 = 상승 ⇔ CPOL == CPHA. CS↓ 순간 SCK ≠ CPOL 이면 경고. mode 3↔0 은 데이터가 같고 1↔0 은 틀린다.
- I2C: 같은 샘플이면 SCL↓ → SDA → SCL↑. Sr/STOP 직전 SCL↑ 샘플은 취소. 바이트 첫 비트의 SDA 위반은 정상 Sr 과 구별 불가 → 유령 주소.
- 디코더 신뢰 = 내 생성기 수천 건 + 남의 캡처(HWLAB-B07 JEDEC `EF 40 18`) + 독립 구현 diff + 변이 테스트(7개 중 1개가 처음엔 살아남음).

## 참고자료

- IEEE Std 1364-2005 (Verilog HDL) §18 "Value change dump (VCD) files" — `$timescale`, `$var`, `$enddefinitions`, `$dumpvars`, `#time`, 스칼라 값 변화 형식. 이 레슨은 1비트 wire 부분집합만(원문 확인 권장).
- sigrok 프로젝트 — VCD/CSV 입출력 모듈과 libsigrokdecode 프로토콜 디코더(UART/SPI/I2C): <https://sigrok.org/wiki/File_format:Vcd>, <https://sigrok.org/wiki/Protocol_decoders> (이 맥에는 sigrok-cli 가 설치되어 있지 않아 출력 비교는 하지 않았다).
- NXP UM10204 *I²C-bus specification and user manual* — START/STOP/repeated START 정의, "SDA 는 SCL low 동안만 바뀐다", ACK/NACK, tSU;DAT·tHD;DAT (절 번호·수치는 원문 확인 권장).
- Motorola/NXP SPI Block Guide 류 문서 — CPOL/CPHA 정의(모드 번호 관례는 벤더마다 표기가 다르니 확인 권장). COMM-B04 의 4모드 그림.
- STMicroelectronics STM32 USART 레퍼런스 매뉴얼 (예: RM0351 STM32L4, RM0433 STM32H7) — Auto baud rate detection(ABREN/ABRMOD) 모드와 전제 문자, 오버샘플링 8/16 (계열별 차이 — RM 확인 권장).
- Texas Instruments TMP117 데이터시트 — 온도 레지스터 0x00, 7.8125 m°C/LSB, 2의 보수 (연습 4 의 변환은 이 값을 가정 — 데이터시트 확인 권장).
- Linux `i2c-tools` 의 `i2ctransfer` 문법 (`w1@0x48 0x00 r2@0x48`) — 연습 4 출력 형식.
- 관련 레슨: COMM-B01 (UART 프레임), COMM-B02 (16x 수신기), COMM-B03 (baud 오차·허용 오차), COMM-B04 (SPI 4모드), COMM-B06 (I2C 비트 레벨), COMM-B07 (repeated START), COMM-M01–M03 (clock stretching·arbitration·bus recovery), COMM-A09 (bus record/replay — 이 디코더 출력이 실보드 로그 소스), HWLAB-B07 (캡처 형식·샘플링률 판정, 교차 검증에 쓴 SPI 캡처), HWLAB-M05 (LA 타이밍 판정), HWLAB-M06 (sigrok 디코더), TEST-M02 (파서 퍼징, 다음 단계).
