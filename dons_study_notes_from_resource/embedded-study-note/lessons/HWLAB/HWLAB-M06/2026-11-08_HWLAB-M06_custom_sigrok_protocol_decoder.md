---
id: HWLAB-M06
time: 80분
updated: 2026-11-08
---
# HWLAB-M06 커스텀 sigrok 프로토콜 디코더 작성 (libsigrokdecode)

## 학습 목표

- libsigrokdecode 프로토콜 디코더(protocol decoder, PD) API v3 의 모양 — `class Decoder(srd.Decoder)`, 메타데이터(`id`/`channels`/`options`/`annotations`/`annotation_rows`), `metadata()` → `start()` → `decode()` 생명주기, `self.wait()` 조건, `self.put()` annotation — 을 외우지 않고 **직접 써서** 설명할 수 있다.
- WS2812(B) 단선 NRZ 프로토콜을 `wait([{0: 'r'}, {'skip': n}])` 같은 조건 조합으로 디코드하고, 같은 샘플에서 두 조건이 동시에 맞는 경계, 프레임 중간에서 시작한 캡처, 칩마다 다른 reset 시간을 테스트로 막는다.
- 디코더의 정확성을 **독립 구현**(HWLAB-B07 로더 + transition 리스트)과 annotation 단위로 비교해 검증하고, 샘플링 양자화(±1 샘플)가 비트 판정과 타이밍 경고를 어떻게 바꾸는지 스윕으로 보인다.
- 기존 디코더 위에 쌓는 **스택 디코더**(stacked decoder)로 I2C → TMP117 레지스터 수준 해석을 만들고, 보드 브링업 엔지니어가 바로 움직일 수 있는 한 줄 리포트를 낸다.

## 1. 문제

LA(logic analyzer)의 내장 디코더가 모르는 사내/벤더 프로토콜이나 "센서 레지스터 수준" 해석이 필요할 때, 캡처를 CSV로 뽑아 스크립트를 짜는 대신 **툴체인 안에 디코더를 붙일 수 있느냐**가 검증 엔지니어의 생산성을 가른다 — Rivian/NVIDIA/Apple 검증 면접에서 "LA 디코더가 없는 프로토콜은 어떻게 봤나, 그 디코더가 맞는지는 어떻게 알았나"로 나온다.

상황: EVT1 상태 표시 보드. MCU(STM32 계열 가정)가 WS2812B 8개 체인(D20–D27)을 `LED_DIN` 한 가닥으로 구동한다. 흔한 펌웨어 기법대로 **SPI MOSI 2.4 MHz + DMA** 로 LED 비트 하나를 SPI 비트 3개로 만든다(0 → `100`, 1 → `110` → T0H 416.7 ns, T1H 833.3 ns, 주기 1.25 µs). 같은 보드의 I2C2 에는 온도 센서 TMP117(U7, ADD0=GND → 0x48)이 있다. 현장 보고: "LED 1~3 이 가끔 한 프레임 동안 엉뚱한 색으로 깜빡인다", "온도 센서 one-shot 설정이 안 먹는다". 팀은 sigrok/PulseView + FX2 계열 LA(16 MS/s 로 캡처)를 쓴다. upstream 에는 `rgb_led_ws281x` 와 `i2c` 디코더가 있지만, 우리는 (1) 타이밍 위반·DMA 갭을 짚어 주는 WS2812 디코더와 (2) I2C 위에서 TMP117 레지스터·온도를 바로 읽어 주는 디코더가 필요하다.

이 맥에는 sigrok-cli/PulseView 가 **없고 설치하지 않는다**. 그래서 디코더는 진짜 libsigrokdecode API 모양 그대로 쓰고, `srd_shim.py`(파이썬 모듈 `sigrokdecode` 를 흉내 내는 작은 shim)로 실행·테스트한다. 완성된 디코더는 `gen_decoders.py` 가 `decoders/<id>/{__init__.py, pd.py}` 패키지로 내보내므로 나중에 PulseView 에 그대로 넣을 수 있다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwm06-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">캡처 → 디코더 스택 → annotation (실기: libsigrokdecode / 이 레슨: srd_shim)</text>
  <rect x="10" y="44" width="120" height="50" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="70" y="66" fill="currentColor" text-anchor="middle">EVT1 보드</text>
  <text x="70" y="84" fill="currentColor" text-anchor="middle" font-size="12">LED_DIN · I2C2</text>
  <rect x="10" y="150" width="120" height="50" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="70" y="172" fill="currentColor" text-anchor="middle">.sr (zip)</text>
  <text x="70" y="190" fill="currentColor" text-anchor="middle" font-size="12">16 MS/s · 2 MS/s</text>
  <rect x="170" y="40" width="200" height="190" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="270" y="60" fill="currentColor" text-anchor="middle" font-weight="bold">sigrokdecode 모듈</text>
  <text x="270" y="80" fill="currentColor" text-anchor="middle" font-size="12">wait() 조건 매칭 · put() 라우팅</text>
  <text x="270" y="98" fill="currentColor" text-anchor="middle" font-size="12">metadata(SAMPLERATE)</text>
  <text x="270" y="200" fill="currentColor" text-anchor="middle" font-size="12">실기: C 라이브러리</text>
  <text x="270" y="218" fill="currentColor" text-anchor="middle" font-size="12">여기: srd_shim.py</text>
  <rect x="410" y="40" width="140" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="480" y="60" fill="currentColor" text-anchor="middle">ws2812_lab</text>
  <text x="480" y="76" fill="currentColor" text-anchor="middle" font-size="12">inputs [logic]</text>
  <rect x="410" y="110" width="140" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="480" y="130" fill="currentColor" text-anchor="middle">i2c (i2c_mini)</text>
  <text x="480" y="146" fill="currentColor" text-anchor="middle" font-size="12">outputs [i2c]</text>
  <rect x="410" y="186" width="140" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="480" y="206" fill="currentColor" text-anchor="middle">tmp117_lab</text>
  <text x="480" y="222" fill="currentColor" text-anchor="middle" font-size="12">inputs [i2c]</text>
  <rect x="590" y="40" width="140" height="44" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="660" y="60" fill="currentColor" text-anchor="middle">annotation</text>
  <text x="660" y="76" fill="currentColor" text-anchor="middle" font-size="12">bit · RGB · 경고</text>
  <rect x="590" y="186" width="140" height="44" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="660" y="206" fill="currentColor" text-anchor="middle">annotation</text>
  <text x="660" y="222" fill="currentColor" text-anchor="middle" font-size="12">레지스터 · °C · NACK</text>
  <rect x="170" y="246" width="380" height="42" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="360" y="264" fill="currentColor" text-anchor="middle">독립 구현: B07 로더 → transition → ws2812_ref()</text>
  <text x="360" y="281" fill="currentColor" text-anchor="middle" font-size="12">wait() 없이 같은 규칙 → (ss, es, class, key) 비교</text>
  <g stroke="currentColor" fill="none" marker-end="url(#hwm06-a)">
    <path d="M70 94 L70 148"/><path d="M130 175 L168 140"/>
    <path d="M370 62 L408 62"/><path d="M370 132 L408 132"/><path d="M480 154 L480 184"/>
    <path d="M550 62 L588 62"/><path d="M550 208 L588 208"/>
    <path d="M70 200 L70 267 L168 267"/><path d="M550 267 L660 267 L660 86"/>
  </g>
  <text x="560" y="128" fill="currentColor" font-size="12">OUTPUT_PYTHON</text>
  <text x="560" y="144" fill="currentColor" font-size="12">['ADDRESS WRITE', 0x48]</text>
</svg>
```

### Part A — WS2812 디코더 (API v3) 를 shim 위에서

`Ws2812Lab.decode()` 를 쓴다. `self.wait({0: 'r'})` 로 비트 시작, `wait({0: 'f'})` 로 high 폭, `wait([{0: 'r'}, {'skip': n_reset}])` 로 "다음 비트냐 reset(래치)이냐"를 가른다. 비트 annotation, 24비트마다 `#RRGGBB` annotation(GRB 순서), reset annotation, 샘플링률이 없으면 `SamplerateError`. 커밋된 `evt1_led_ws2812_16M.sr` 의 3 프레임 × 8 LED 색이 생성기에 주입한 색과 같아야 한다.

### Part B — 디코더가 거짓말하는 지점들 + 독립 구현 교차 검증

- `wait()` 경계: 마지막 비트 fall 뒤 **정확히 n_reset 샘플**에 다음 rise 가 오면 두 조건이 동시에 맞는다 → 다음 프레임 첫 비트를 잃지 말 것.
- 캡처가 high 로, 프레임 중간에서 시작 → `NOSYNC` 경고. 심어 둔 DMA underrun(프레임 2, LED 5 뒤 20 µs low) → `GAP` 경고. reset 이 짧은 클론 칩을 흉내 낸 `reset_us=9` 에서는 프레임이 `[8, 5, 3, 8]` 로 쪼개진다.
- `outside()` — ±1 샘플 가드밴드(HWLAB-B07). 그리고 `ws2812_ref()` — wait() 없이 B07 로더의 transition 리스트만으로 같은 규칙을 다시 구현해, 디코더 출력과 **annotation 단위로 완전히 같아야** 한다. 2–25 MS/s × 4 위상 × guard on/off 스윕에서 비트 판정·가짜 경고·두 구현 일치를 본다.

### Part C — I2C 위에 쌓는 TMP117 디코더 + EVT1 리포트

`Tmp117Lab`(inputs `['i2c']`)의 `decode(ss, es, data)` 와 `finish()`: upstream I2C 디코더의 파이썬 출력(`['START', None]`, `['ADDRESS WRITE', 0x48]`, `['DATA READ', 0x0B]` …)을 받아 pointer 쓰기 + 16비트 레지스터 읽기/쓰기, 온도(°C), Device_ID 확인, TMP117 주소대(0x48–0x4B)의 주소 NACK 경고를 낸다. 마지막으로 `board_report()` 가 두 디코더 출력을 엮어 `FAIL net=… frame 2: GAP … after LED 5 …` / `FAIL net=I2C2 addr 0x49 NACK …` 같은 조치 가능한 줄을 만든다.

## 2. Problem (English)

> **Write a sigrok protocol decoder (libsigrokdecode PD API v3) and prove it is right.**
>
> A local shim `srd_shim.py` stands in for the `sigrokdecode` module: `wait(conds)` with terms `'r','f','e','h','l','n'`
> and `{'skip': n}` (a dict ANDs its terms, a list ORs dicts; `self.matched` tells which matched, `self.samplenum` where),
> `register()`, `put(ss, es, out_ann, [cls, [texts...]])`, `metadata(SRD_CONF_SAMPLERATE, fs)`, and decoder stacking
> (`OUTPUT_PYTHON` of the lower decoder feeds `decode(ss, es, data)` of the upper one).
>
> 1. Implement `Ws2812Lab.decode()`: a bit is a high pulse (T0H 250–550 ns → 0, T1H 650–950 ns → 1, split at 600 ns) in a
>    650–1850 ns period; a low ≥ `reset_us` (default 50) ends the frame; 24 bits per LED, GRB, MSB first. Emit bit, RGB,
>    reset and warning (`T0H`/`T1H`/`PERIOD`/`GAP`/`PARTIAL`/`NOSYNC`) annotations. Use integer arithmetic for all
>    threshold comparisons.
> 2. Implement `outside(n, lo, hi, fs, guard)` (±1-sample guard band) and `ws2812_ref(cap)`, an independent decoder that
>    works on a transition list from a `.sr` loader. On every capture and every sweep run (2–25 MS/s, random phase), its
>    output must equal the PD's annotations `(ss, es, class, short text)` exactly.
> 3. Implement the stacked `Tmp117Lab` (input `i2c`) and `board_report()`. Expected on the EVT1 captures:
>    `Device_ID=0x0117 T=[23.5000, 23.5078] C`, a `NACK 49` warning, and two FAIL lines (frame 2 gap after LED 5;
>    address 0x49 NACK with `CONFIG readback 0x0220`).
>
> The decoder classes must run unchanged inside PulseView: only `import sigrokdecode as srd`, no shim-specific names.

## 3. 시작 코드

<!-- code: starter.py -->

shim (주어짐 — 경계 의미론은 §5 Part A 표):

<!-- code: srd_shim.py -->

캡처 생성기 (결정적: 시드 612, zip 타임스탬프 고정). `evt1_led_ws2812_16M.sr`(1330 B, 16961 샘플)와 `evt1_i2c_tmp117_2M.sr`(673 B, 4454 샘플)를 다시 만든다:

<!-- code: gen_capture.py -->

```bash
cd lessons/HWLAB/HWLAB-M06
python3 -I starter.py ; echo "exit=$?"      # TODO 상태
```

실제 출력 (일부):

```text
-- Part A: shim + WS2812 decoder (API v3) --
PASS  shim: wait r/f/e/h/l/skip semantics on a 10-sample list
PASS  decoder metadata validates (Ws2812Lab, I2cMini, Tmp117Lab)
PASS  capture: LED_DIN 16 MS/s, 1 probe
      (NotImplementedError: Ws2812Lab.decode)
FAIL  24 LEDs decoded, colours == injected truth (3 frames x 8)
...
FAIL  guard band: 4 MS/s, 4 samples (1000 ns) vs T1H max 950 -> naive warns, guard does not  (NotImplementedError: outside)
FAIL  independent (B07 loader + transitions) == srd decoder on the .sr  (NotImplementedError: ws2812_ref)
...
PASS  stacking tmp117_lab (inputs i2c) on ws2812_lab (outputs []) -> DecoderError
...
4/31 passed  (0.0 s)
exit=1
```

미리 통과하는 4개는 shim·메타데이터·캡처·스택 규칙 검사다 — 여러분이 쓸 것은 디코더 본문이다.

## 4. 힌트

<details>
<summary>힌트 1 — wait() 는 "다음 사건까지 감기"</summary>

API v3 디코더는 샘플을 직접 돌지 않는다. `pins = self.wait({0: 'f'})` 는 채널 0 의 다음 하강 에지까지 C 쪽에서 감아 두고 돌아오며, 그때 `self.samplenum` 이 그 샘플 번호다. high 폭 = `fall − rise` (샘플 수). 조건 리스트 `[{0: 'r'}, {'skip': n}]` 는 OR — 돌아온 뒤 `self.matched` 튜플로 어느 쪽이 맞았는지 본다.

</details>

<details>
<summary>힌트 2 — reset 판정은 "low 가 n_reset 샘플 지속" = skip 조건</summary>

fall 직후 `wait([{0: 'r'}, {'skip': n_reset}])`. rise 가 먼저면 보통 비트(주기 = 다음 rise − rise). skip 이 먼저면 low 가 n_reset 이상 → 프레임 끝. **둘 다 True** 일 수 있다(rise 가 정확히 fall + n_reset). 그때 현재 샘플이 이미 다음 비트의 rise 이므로 `wait({0: 'r'})` 를 또 부르면 그 rise 를 건너뛴다.

</details>

<details>
<summary>힌트 3 — 임계값 비교는 정수로</summary>

`h / fs >= 600e-9` 를 float 로 쓰면 독립 구현(ps 정수)과 경계에서 1 샘플 어긋날 수 있다. `h * 10**9 >= 600 * fs` 처럼 양변에 정수만 둔다. `n_reset = ceil(reset_us * fs / 1e6)` 도 `-(-a // b)`.

</details>

<details>
<summary>힌트 4 — 독립 구현은 rise/fall 배열 두 개</summary>

`edges = cap["tr"]["LED_DIN"][1:]`(첫 원소는 초기 레벨), `rises`/`falls` 를 샘플 번호(`t // ts`)로. rise 마다 그 뒤 첫 fall, 다음 rise `r2`. reset 판정은 `r2 − f >= n_reset`, 마지막 비트는 `f + n_reset <= n − 1`(캡처 안에서 skip 이 끝나야 디코더도 reset 을 본다). 디코더가 EOF 로 끝나는 경우를 똑같이 흉내 내야 annotation 이 일치한다.

</details>

<details>
<summary>힌트 5 — 스택 디코더는 상태 기계 하나</summary>

`decode(self, ss, es, data)` 에 `[cmd, val]` 이 하나씩 온다. START/Sr/STOP 에서 직전 세그먼트를 `finish()` 하고, `ADDRESS *` → ACK/NACK → `DATA *` 를 모은다. TMP117 은 쓰기의 첫 바이트가 pointer, 그 뒤 2바이트가 값(MSB 먼저); 읽기는 직전 pointer 의 레지스터. pointer 는 트랜잭션 사이에도 유지된다. `'BITS'` 같은 모르는 명령은 무시.

</details>

## 5. 풀이

### Part A — libsigrokdecode 디코더의 해부

**클래스 메타데이터** (libsigrokdecode 가 디코더를 로드할 때 읽는 속성 — shim 의 `validate_class` 가 그 부분집합을 검사):

| 속성 | 의미 | ws2812_lab |
|---|---|---|
| `api_version` | 3 = `wait()` 기반 API (libsigrokdecode 0.5 이후) | `3` |
| `id` / `name` / `longname` / `desc` / `license` | `-P <id>` 로 부르는 이름, GUI 표시 문자열, 라이선스 | `'ws2812_lab'`, `'gplv2+'` |
| `inputs` / `outputs` | 받는 것(`'logic'` 또는 아래 디코더의 출력 id) / 위로 내보내는 것 | `['logic']` / `[]` |
| `channels` / `optional_channels` | 튜플 순서 = `wait()` 의 채널 번호 0,1,… | `({'id': 'din', …},)` |
| `options` | `-P ws2812_lab:reset_us=9` 로 바꾸는 값. `values` 가 있으면 GUI 드롭다운 | `reset_us`, `order`, `guard` |
| `annotations` | annotation 클래스 `(id, desc)` 목록 — `put(..., [클래스 번호, …])` 의 번호 | bit, reset, rgb, warning |
| `annotation_rows` | PulseView 의 행 묶음 | Bits(0,1) / LEDs(2) / Warnings(3) |

**생명주기**: 인스턴스 생성(`__init__` → 보통 `self.reset()`) → `metadata(srd.SRD_CONF_SAMPLERATE, fs)` → `start()`(여기서 `self.out_ann = self.register(srd.OUTPUT_ANN)`) → `decode()`. logic 입력 디코더의 `decode()` 는 **끝나지 않는 루프**다 — 샘플이 떨어지면 라이브러리가 실행을 멈춘다(shim 은 `EndOfSamples` 를 던지고 `run()` 이 잡는다). 샘플링률은 `decode()` 안에서 확인하고 없으면 upstream 관례대로 `SamplerateError` 를 던진다.

**`wait()` 조건** (shim 이 구현한 의미론):

| 항 | 참이 되는 샘플 k | 비고 |
|---|---|---|
| `{0: 'r'}` / `'f'` | 채널 0 이 k−1 → k 에서 0→1 / 1→0 | 샘플 0 에서는 에지 없음 |
| `'e'` / `'n'` | 변화 있음 / 없음 | |
| `'h'` / `'l'` | 샘플 k 의 레벨 | 레벨만 있는 조건은 샘플마다 검사 (느림) |
| `{'skip': n}` | 호출 시점 samplenum + n | n ≥ 1 만 허용 (shim) |
| dict 안 여러 항 | 모두 참 (AND) | I2C START = `{0: 'h', 1: 'f'}` |
| dict 의 list | 하나라도 참 (OR), `self.matched` 에 각각의 결과 | |

검사는 첫 호출이면 샘플 0 부터, 이후에는 직전 매치 샘플 + 1 부터 — 같은 에지를 두 번 매치하지 않는다. shim 은 모든 조건에 에지/skip 항이 있으면 다음 에지 후보로 바로 점프한다(libsigrokdecode 가 C 쪽에서 조건을 맞추는 이유와 같은 효과). `_shim_selftest` 가 10샘플 리스트로 이 표를 검사한다.

> 불확실한 점(명시): 실제 libsigrokdecode 의 첫 샘플 처리(초기 핀 상태와 샘플 0 의 에지 여부), `{'skip': 0}`, 매칭되지 않은 optional 채널의 pins 값은 **실행해서 대조하지 못했다**. 이 레슨의 디코더는 에지 + skip(n ≥ 1) 조합만 쓰도록 짜서 그 경계에 기대지 않는다. PulseView 에서 처음 돌릴 때 `--protocol-decoder-samplenum` 으로 ss/es 를 shim 출력과 비교해 보라.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">WS2812 비트와 wait() 호출 지점 (16 MS/s, 1 샘플 = 62.5 ns)</text>
  <path d="M20 120 L60 120 L60 60 L120 60 L120 120 L260 120 L260 60 L390 60 L390 120 L460 120 L460 60 L520 60 L520 120 L720 120" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="70" y="52" fill="currentColor" font-size="12">T0H 417 ns</text>
  <text x="275" y="52" fill="currentColor" font-size="12">T1H 833 ns</text>
  <text x="560" y="112" fill="currentColor" font-size="12">low ≥ reset 50 µs → 래치</text>
  <g stroke="currentColor" stroke-dasharray="4 3">
    <path d="M60 130 L60 175"/><path d="M120 130 L120 175"/><path d="M260 130 L260 175"/><path d="M390 130 L390 175"/>
    <path d="M460 130 L460 175"/><path d="M520 130 L520 175"/><path d="M700 130 L700 175"/>
  </g>
  <rect x="40" y="180" width="60" height="22" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="70" y="196" fill="currentColor" text-anchor="middle" font-size="12">wait r</text>
  <rect x="100" y="206" width="60" height="22" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="130" y="222" fill="currentColor" text-anchor="middle" font-size="12">wait f</text>
  <rect x="170" y="180" width="180" height="22" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="260" y="196" fill="currentColor" text-anchor="middle" font-size="12">wait [r, skip n_reset] → r</text>
  <rect x="360" y="206" width="60" height="22" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="390" y="222" fill="currentColor" text-anchor="middle" font-size="12">wait f</text>
  <rect x="440" y="180" width="60" height="22" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="470" y="196" fill="currentColor" text-anchor="middle" font-size="12">… f</text>
  <rect x="530" y="206" width="190" height="22" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="625" y="222" fill="currentColor" text-anchor="middle" font-size="12">wait [r, skip] → skip: RESET</text>
  <text x="20" y="252" fill="currentColor" font-size="12">bit = (fall − rise)·10⁹ ≥ 600·fs ;  bit 의 ss..es = rise..다음 rise (마지막 비트는 rise + 공칭 1.25 µs)</text>
</svg>
```

**put() 형식**: `self.put(ss, es, self.out_ann, [클래스 번호, [긴 텍스트, …, 짧은 텍스트]])`. 텍스트 리스트는 긴 것부터 — PulseView 는 확대 정도에 맞는 길이를 골라 그린다. 이 레슨은 마지막(가장 짧은) 텍스트를 기계가 읽는 키로 쓴다(`'#FF0000'`, `'GAP'`, `'R 0F=0117'`): 테스트와 리포트가 긴 문장을 파싱하지 않게.

실제 annotation (solution, 앞부분과 프레임 2 의 갭 근처):

```text
1281 1760 rgb ('LED 1: #FF0000 (R=255 G=0 B=0)', '#FF0000')
1760 2241 rgb ('LED 2: #00FF00 (R=0 G=255 B=0)', '#00FF00')
1281 1301 bit ('0',)
1301 1321 bit ('0',)
7841 8320 rgb ('LED 4: #FFFFFF (R=255 G=255 B=255)', '#FFFFFF')
8320 9120 rgb ('LED 5: #102040 (R=16 G=32 B=64)', '#102040')
8787 9120 warning ('low gap 20.8 us (> bit 1850 ns, < reset 50 us)', 'GAP')
9120 9600 rgb ('LED 6: #123456 (R=18 G=52 B=86)', '#123456')
```

첫 rise 가 1281 인 것은 생성기가 80 µs(=1280 샘플) idle 뒤에 프레임을 시작하기 때문이다. 비트 하나 = 20 샘플(1.25 µs × 16 MS/s), LED 하나 ≈ 480 샘플. LED 5 의 annotation 이 800 샘플로 긴 이유: 마지막 비트의 es 를 "다음 rise" 로 정했기 때문 — 갭이 비트 안으로 들어가 보이는 것이 오히려 PulseView 에서 눈에 띈다(설계 선택이다; upstream 은 다르게 그릴 수 있다).

**패키지로 내보내기**: sigrok 디코더는 디렉터리 하나다 — `decoders/ws2812_lab/__init__.py`(docstring + `from .pd import Decoder`) 와 `pd.py`(`import sigrokdecode as srd` + `class Decoder(srd.Decoder)`). `gen_decoders.py` 가 solution.py 의 `# ==== pd:ws2812_lab begin/end ====` 구역을 그대로 잘라 클래스 이름만 `Decoder` 로 바꿔 쓰고, 마지막 테스트가 그 패키지를 shim 으로 import 해 in-file 클래스와 annotation 이 같은지, `srd_shim` 이라는 이름이 소스에 없는지 확인한다.

### Part B — 디코더가 거짓말하는 네 지점

**1. 두 조건이 같은 샘플에서 맞을 때.** low 가 정확히 n_reset 샘플이면 `wait([{0: 'r'}, {'skip': n_reset}])` 가 `matched == (True, True)` 로 돌아온다. reset 으로 처리하는 것은 맞지만, 습관대로 `self.wait({0: 'r'})` 를 부르면 **지금 샘플의 rise 는 이미 지나간 것**이라 다음 rise(= 다음 프레임의 둘째 비트)까지 감긴다 → 첫 비트 소실 → 그 프레임 전체 색이 1비트씩 밀린다. 테스트는 low 를 정확히 800 샘플로 만든 합성 캡처에서 LED 2개(`#FFFFFF`, `#000000`)와 비트 48개를 요구한다. 현실에서 low 가 reset 문턱과 같을 확률은 낮지만, 펌웨어가 reset 을 "딱 50 µs" 로 만들고 LA 가 정수비 샘플링이면 생긴다.

**2. 캡처가 프레임 중간에서 시작.** 트리거 전 버퍼가 짧으면 캡처 시작이 high(펄스 중간)이거나 프레임 중간이다. 에지 조건은 샘플 0 에서 참이 아니므로 시작 펄스는 무시되고, 첫 rise 앞의 low 가 n_reset 보다 짧으면 비트 정렬을 보장할 수 없다 → `NOSYNC` 경고 `[0, 첫 rise]`. 정렬이 틀린 프레임은 다음 reset 에서 저절로 맞춰진다.

**3. reset 문턱은 칩마다 다르다 — GAP.** 프레임 2 의 LED 5 뒤 20 µs low(펌웨어 SPI DMA 가 다음 버퍼를 늦게 채운 underrun 을 흉내 냄). 디코더 기준(reset 50 µs)으로는 래치가 아니므로 색은 그대로 디코드되고 `GAP` 경고만 뜬다. 그런데 실제 스트립은? WS2812B 구형 데이터시트는 RES > 50 µs, 신형(V5 계열)은 > 280 µs 로 적고, 50 µs 보다 훨씬 짧은 low 에서 래치하는 칩·클론에 대한 실측 보고도 있다 (**대표값 — 데이터시트/로트 확인 필요**). `reset_us=9` 로 돌리면 실제 출력이 `[8, 5, 3, 8]` — 프레임 2 가 LED 5 에서 래치되고, 남은 LED 6~8 의 데이터(#123456, #A55A0F, #000000)가 **체인 처음(LED 1~3)** 으로 들어간다. LED 6~8 은 프레임 1 값(같은 색)을 유지하므로 티가 안 나고, 엉뚱한 색은 **결함 위치와 반대쪽인 LED 1~3** 에 다음 프레임까지 보인다 — 현장 증상 그대로다. "앞쪽 LED 가 이상하니 앞쪽 배선 문제"라고 짚으면 틀리는 이유. 디코더 옵션 하나로 "그 칩이라면 무엇을 봤을까"를 재현할 수 있는 것이 커스텀 디코더의 힘이다.

**4. 양자화와 가드밴드.** 샘플링된 구간 n 샘플의 진짜 길이는 ((n−1)Ts, (n+1)Ts) (HWLAB-B07). 순진한 `n·Ts > 950 ns` 는 4 MS/s 에서 T1H 833 ns 를 4 샘플(1000 ns)로 잡는 순간 가짜 `T1H` 경고를 낸다. 가드밴드는 "확실히 밖"일 때만 경고: `(n+1)·Ts ≤ lo` 또는 `(n−1)·Ts ≥ hi`. 대가는 해상도 — Ts 가 스펙 여유(±150 ns)와 비슷하면 진짜 위반도 못 잡는다(그때는 "UNRESOLVED", 스코프의 일: HWLAB-B04).

실제 스윕 (solution 출력, 연속 파형 → 무작위 위상 4개 × fs, `colours ok` = 24 LED 전부 맞은 run 수, 가짜 경고 = 실제로는 스펙 안인 `T0H/T1H/PERIOD` 경고 수):

```text
       2 MS/s Ts=500.0 ns  colours ok 0/4  false timing warnings guard=yes  84  guard=no 342  srd==ref 4/4
       4 MS/s Ts=250.0 ns  colours ok 4/4  false timing warnings guard=yes   0  guard=no 157  srd==ref 4/4
       5 MS/s Ts=200.0 ns  colours ok 2/4  false timing warnings guard=yes   0  guard=no 120  srd==ref 4/4
       8 MS/s Ts=125.0 ns  colours ok 4/4  false timing warnings guard=yes   0  guard=no   0  srd==ref 4/4
      10 MS/s Ts=100.0 ns  colours ok 4/4  false timing warnings guard=yes   0  guard=no   0  srd==ref 4/4
      16 MS/s Ts= 62.5 ns  colours ok 4/4  false timing warnings guard=yes   0  guard=no   0  srd==ref 4/4
      20 MS/s Ts= 50.0 ns  colours ok 4/4  false timing warnings guard=yes   0  guard=no   0  srd==ref 4/4
      25 MS/s Ts= 40.0 ns  colours ok 4/4  false timing warnings guard=yes   0  guard=no   0  srd==ref 4/4
      (sweep 0.6 s)
```

가장 흥미로운 줄은 **4 MS/s 는 되는데 5 MS/s 는 안 된다**는 것이다. 폭 w 인 펄스는 ⌊w/Ts⌋ 또는 ⌈w/Ts⌉ 샘플로 잡히고, 디코더는 `n·Ts ≥ 600 ns` 이면 1 → `n ≥ m = ⌈600/Ts⌉`.

| fs | Ts | m = ⌈600/Ts⌉ | T0H 417 ns → n | T1H 833 ns → n | 판정 |
|---|---|---|---|---|---|
| 4 MS/s | 250 ns | 3 (2.4 올림) | 1 또는 2 (< 3) | 3 또는 4 (≥ 3) | 모든 위상에서 맞음 |
| 5 MS/s | 200 ns | 3 (정확히 3.0) | 2 또는 **3** | 4 또는 5 | T0H 가 3 샘플 = 600 ns 로 잡히면 1 로 오판 |
| 8 MS/s | 125 ns | 5 (4.8 올림) | 3 또는 4 | 6 또는 7 | 맞음 |

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 T0H 417 ns 펄스, 샘플 격자에 따라 보이는 폭 (판정선 600 ns)</text>
  <text x="10" y="70" fill="currentColor">4 MS/s</text>
  <path d="M90 80 L150 80 L150 45 L317 45 L317 80 L700 80" stroke="currentColor" fill="none" stroke-width="2"/>
  <g class="f-acc" stroke="currentColor">
    <circle cx="100" cy="92" r="4"/><circle cx="200" cy="92" r="4"/><circle cx="300" cy="92" r="4"/><circle cx="400" cy="92" r="4"/><circle cx="500" cy="92" r="4"/>
  </g>
  <text x="420" y="64" fill="currentColor" font-size="12">high 샘플 2개 = 500 ns &lt; 600 → 0 (최악도 2개)</text>
  <text x="10" y="160" fill="currentColor">5 MS/s</text>
  <path d="M90 170 L150 170 L150 135 L317 135 L317 170 L700 170" stroke="currentColor" fill="none" stroke-width="2"/>
  <g class="f-warn" stroke="currentColor">
    <circle cx="75" cy="182" r="4"/><circle cx="155" cy="182" r="4"/><circle cx="235" cy="182" r="4"/><circle cx="315" cy="182" r="4"/><circle cx="395" cy="182" r="4"/><circle cx="475" cy="182" r="4"/>
  </g>
  <text x="420" y="154" fill="currentColor" font-size="12">high 샘플 3개 = 600 ns ≥ 600 → 1 (오판!)</text>
  <text x="10" y="215" fill="currentColor" font-size="12">충분조건: Ts &lt; min(600 − T0H_max, T1H_min − 600). 넘지 않으면 판정선이 격자에 대해 어디 놓이느냐(운)에 달린다.</text>
</svg>
```

(그림은 개념도: 펄스 폭 167 px ≈ 417 ns 를 4 MS/s 격자 100 px = 250 ns, 5 MS/s 격자 80 px = 200 ns 에 대응시켰다.) 지터 ±20 ns 를 넣은 충분조건은 Ts < 163 ns → fs > 6.13 MS/s. 그 아래는 비정수비의 "운"이다. 연습 3 이 이 예측식을 1–25 MS/s 에 적용해 스윕과 대조한다(보장 안 됨: 1, 2, 3, 5 MS/s). 실무 결론: 16 MS/s FX2 LA 로 WS2812 **디코드**는 넉넉하지만, ±150 ns 스펙의 **타이밍 판정**은 Ts 62.5 ns 가드 때문에 실제 여유가 ±87.5 ns 로 줄어든다 — 경계 펄스를 판정하려면 스코프(HWLAB-B04/B05).

**5. 독립 구현으로 디코더를 검증하기.** `ws2812_ref()` 는 같은 스펙을 **다른 제어 흐름**으로 구현한다: 코루틴처럼 흐르는 `wait()` 대신 B07 로더가 준 transition 리스트에서 rise/fall 배열을 만들어 인덱스로 걷는다. 두 구현의 출력을 `(ss, es, class, 짧은 텍스트)` 정렬 리스트로 비교한다.

| 이 비교가 잡는 것 | 못 잡는 것 |
|---|---|
| wait() 의미 오해 (같은 에지 재매치, 두 조건 동시 매치, EOF 처리) | 두 구현이 **같은 스펙 오해**를 공유 (예: GRB 를 RGB 로 착각) |
| ss/es 경계 off-by-one, 임계값 반올림 차이 | 생성기(진실) 자체의 오류 |
| 상태 초기화 누락 (프레임 사이 비트 버퍼) | 실제 libsigrokdecode 와 shim 의 차이 |

그래서 셋을 겹친다: ① 생성기에 **주입한 색**과 비교(공통 오해 차단), ② 독립 구현과 annotation 완전 일치(구현 오류 차단), ③ PulseView 에서 upstream `rgb_led_ws281x` 출력과 대조(shim 차이 차단 — 도구가 생기면). 정수 산술은 ②가 비트 단위로 성립하게 만드는 전제다: 한쪽이 `h/fs >= 6e-7`(float) 이고 다른 쪽이 ps 정수면 600 ns 에 정확히 걸리는 펄스에서 결과가 갈린다.

### Part C — 스택 디코더: I2C 위의 TMP117

upstream `i2c` 디코더의 OUTPUT_PYTHON 은 `[<ptype>, <pdata>]` 다. `I2cMini` 는 그 형식만 흉내 낸 축약판(주어짐)이고, 실기에서는 upstream `i2c` 위에 `tmp117_lab` 을 그대로 쌓는다(`inputs = ['i2c']` 가 upstream `outputs = ['i2c']` 와 맞는다).

| ptype | pdata | 비고 |
|---|---|---|
| `'START'`, `'START REPEAT'`, `'STOP'` | `None` | |
| `'ADDRESS READ'` / `'ADDRESS WRITE'` | 7비트 주소 (R/W 비트 제외: 0x48, 0x90 아님) | |
| `'DATA READ'` / `'DATA WRITE'` | 바이트 | |
| `'ACK'` / `'NACK'` | `None` | 주소·데이터 바이트 뒤마다 |
| `'BITS'` | `[[bit, ss, es], …]` LSB 먼저 | 스택 디코더는 보통 무시 |

(upstream 버전에 따라 경고용 ptype 등이 더 있을 수 있다 — 그래서 모르는 ptype 은 **무시**하는 것이 스택 디코더의 기본 예의다.)

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwm06-b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">tmp117_lab 상태 기계 (입력 = i2c 의 [cmd, val] 한 개씩)</text>
  <rect x="10" y="90" width="90" height="40" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="55" y="115" fill="currentColor" text-anchor="middle">IDLE</text>
  <rect x="150" y="90" width="90" height="40" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="195" y="115" fill="currentColor" text-anchor="middle">ADDR</text>
  <rect x="290" y="90" width="110" height="40" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="345" y="115" fill="currentColor" text-anchor="middle">ADDR_ACK</text>
  <rect x="460" y="40" width="110" height="40" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="515" y="65" fill="currentColor" text-anchor="middle">DATA (0x48)</text>
  <rect x="460" y="140" width="110" height="40" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="515" y="165" fill="currentColor" text-anchor="middle">NACK 경고</text>
  <rect x="610" y="40" width="120" height="40" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="670" y="58" fill="currentColor" text-anchor="middle" font-size="12">finish(): R/W 레지스터</text>
  <text x="670" y="73" fill="currentColor" text-anchor="middle" font-size="12">°C · DID 확인</text>
  <g stroke="currentColor" fill="none" marker-end="url(#hwm06-b)">
    <path d="M100 110 L148 110"/><path d="M240 110 L288 110"/><path d="M400 102 L458 64"/><path d="M400 118 L458 158"/>
    <path d="M570 60 L608 60"/><path d="M515 40 L515 32 L195 32 L195 88"/>
  </g>
  <text x="104" y="104" fill="currentColor" font-size="12">START</text>
  <text x="244" y="104" fill="currentColor" font-size="12">ADDRESS</text>
  <text x="405" y="78" fill="currentColor" font-size="12">ACK</text>
  <text x="405" y="150" fill="currentColor" font-size="12">NACK</text>
  <text x="250" y="50" fill="currentColor" font-size="12">Sr / STOP → finish()</text>
  <text x="10" y="210" fill="currentColor" font-size="12">pointer 는 트랜잭션 밖에 저장: TMP117 은 마지막 pointer 를 기억 (pointer 없는 읽기 = 같은 레지스터)</text>
</svg>
```

TMP117 레지스터 (**대표값 — TI 데이터시트 확인 필요**): 0x00 Temp_Result(부호 16비트, 1 LSB = 7.8125 m°C = 1/128 °C), 0x01 Configuration(리셋값 0x0220, [11:10] MOD: 00 연속 / 01 셧다운 / 10 연속 / 11 one-shot), 0x02/0x03 THigh/TLow, 0x0F Device_ID(하위 12비트 DID = 0x117, 상위 4비트 REV). 주소는 ADD0 핀으로 0x48(GND) / 0x49(V+) / 0x4A(SDA) / 0x4B(SCL). 0x0BC0 = 3008 → 3008/128 = 23.5000 °C, 0x0BC1 → 23.5078 °C, 0xFF80 = −128 → −1.0000 °C.

실제 tmp117_lab annotation (긴 텍스트):

```text
624 964 read Read Device_ID (0x0F) = 0x0117 (DID=0x117 REV=0)
1090 1270 warning address 0x49 NACK: no TMP117 there (this decoder: 0x48)
1942 2282 read Read Temp_Result (0x00) = 0x0BC0
1942 2282 temp T = 23.5000 °C (raw 0x0BC0)
2974 3314 read Read Configuration (0x01) = 0x0220 (MOD=CC)
4006 4346 read Read Temp_Result (0x00) = 0x0BC1
4006 4346 temp T = 23.5078 °C (raw 0x0BC1)
```

(2 MS/s → 샘플 1개 = 0.5 µs, 100 kHz I2C 비트 하나 = 20 샘플. 바이트 annotation 은 첫 비트의 SCL rise 부터 ACK 비트의 SCL rise 까지.)

**EVT1 리포트** (solution 출력):

```text
      LED  net=LED_DIN fs=16MS/s frames=3 leds=[8, 8, 8]
      FAIL net=LED_DIN frame 2: GAP 20.8 us after LED 5 (bit period max 1.85 us < gap < reset 50 us) -> SPI DMA underrun / refill ISR latency; a strip that latches on a shorter low would split this frame
      I2C  net=I2C2 fs=2MS/s TMP117@0x48 Device_ID=0x0117 T=[23.5000, 23.5078] C CONFIG=0x0220
      FAIL net=I2C2 addr 0x49 NACK x1 -> a transfer meant for TMP117 went to the wrong address (U7 answers at 0x48: check ADD0 strap vs firmware); CONFIG readback 0x0220 = power-on default -> the config write never landed
```

20.8 µs = 주입한 20 µs + 그 비트 자신의 low(마지막 비트 0 → T0L 833 ns). 결함이 없는 보드(갭 없음, 올바른 주소, CONFIG 0x0C20 읽힘)로 같은 코드를 돌리면 `PASS` 두 줄이 나온다(테스트 `clean board`). 원인 → 증상 → 가르는 측정:

| 원인 | LA/디코더에서 보이는 것 | 가르는 측정 / 조치 |
|---|---|---|
| SPI DMA underrun, refill ISR 지연 | `GAP` (비트 주기 ≫ 1.85 µs, < reset) | DMA half/complete ISR 진입 시각을 GPIO 토글로 같은 캡처에 넣기, circular DMA·버퍼 크기 |
| 칩/클론의 짧은 reset 문턱 | `reset_us` 를 줄이면 프레임이 쪼개짐, 뒤쪽 LED 만 이상 | 같은 캡처를 reset_us 9/50/280 으로 재디코드, 해당 로트 데이터시트 |
| 3.3 V MCU → 5 V WS2812B 입력 (VIH ≈ 0.7·VDD) | 디코더는 정상, LED 는 랜덤 | LA 는 MCU 쪽 레벨을 본다 → 스코프로 첫 LED DIN 핀 (HWLAB-B02), 레벨 시프터 |
| ADD0 스트랩 ≠ 펌웨어 주소 | `NACK 49` + CONFIG 가 리셋값 | 스트랩 저항 실장 확인, 펌웨어 주소 상수, `i2cscan_lab`(연습 1) |
| 센서 무전원/리셋 중 | 0x48 에서도 NACK | 레일 확인, 전원 시퀀스 |

**장비/도구가 있을 때 (sigrok-cli · PulseView — 이 맥에는 없어 실행하지 않았다, 출력 없음):**

```bash
# 1) 우리 디코더 패키지를 libsigrokdecode 검색 경로에 (SIGROKDECODE_DIR 환경변수로 디렉터리 추가)
export SIGROKDECODE_DIR="$PWD/decoders"
sigrok-cli -L | grep _lab                              # ws2812_lab, tmp117_lab 이 목록에 보이는지
# 2) WS2812: 채널 매핑 din=LED_DIN, 옵션도 같은 -P 안에 ':' 로
sigrok-cli -i evt1_led_ws2812_16M.sr -P ws2812_lab:din=LED_DIN -A ws2812_lab=rgb:warning
sigrok-cli -i evt1_led_ws2812_16M.sr -P ws2812_lab:din=LED_DIN:reset_us=9 --protocol-decoder-samplenum
# 3) 스택: upstream i2c 위에 tmp117_lab (쉼표 = 위로 쌓기)
sigrok-cli -i evt1_i2c_tmp117_2M.sr -P i2c:scl=I2C2_SCL:sda=I2C2_SDA,tmp117_lab -A tmp117_lab
# 4) 교차 확인: upstream WS281x 디코더와 색 비교
sigrok-cli -i evt1_led_ws2812_16M.sr -P rgb_led_ws281x:din=LED_DIN
```

PulseView 에서는 같은 `decoders/` 를 디코더 검색 경로에 두고 재시작하면 디코더 목록에 `WS2812 (lab)` 이 보인다(사용자 디코더 디렉터리 위치는 OS·버전마다 다르다 — 확인 필요). 이 레슨의 `.sr` 이 실제 PulseView 에서 열리는지도 같은 이유로 확인하지 못했다(HWLAB-B07 과 같은 writer 를 축약해 썼다).

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

내보낸 패키지 (생성물, 손으로 고치지 않는다) 와 내보내기 스크립트:

<!-- code: decoders/ws2812_lab/pd.py -->

<!-- code: gen_decoders.py -->

```bash
python3 -I solution.py ; echo "exit=$?"
python3 -I -W error -X dev solution.py > /dev/null ; echo "exit=$?"   # 경고 없이 exit=0
```

실제 출력 (스윕 표와 리포트 줄은 §5 에 그대로 실었다):

```text
-- Part A: shim + WS2812 decoder (API v3) --
PASS  shim: wait r/f/e/h/l/skip semantics on a 10-sample list
PASS  decoder metadata validates (Ws2812Lab, I2cMini, Tmp117Lab)
PASS  capture: LED_DIN 16 MS/s, 1 probe
PASS  24 LEDs decoded, colours == injected truth (3 frames x 8)
PASS  576 bit annotations, 3 RESET
PASS  first LED annotation text 'LED 1: #FF0000 (R=255 G=0 B=0)'
PASS  no samplerate -> SamplerateError
PASS  order=RGB option swaps R/G of LED 1 -> #00FF00
-- Part B: edge cases, guard band, independent implementation --
PASS  planted DMA gap: GAP warning in frame 2 after LED 5, colours still right
PASS  reset_us=9 (early-latching clone): frame 2 splits -> LEDs per frame [8, 5, 3, 8]
PASS  rise exactly at fall + n_reset (both conditions match): 2 LEDs, no lost bit
PASS  capture starts high, mid-frame -> NOSYNC warning, then decodes
PASS  guard band: 4 MS/s, 4 samples (1000 ns) vs T1H max 950 -> naive warns, guard does not
PASS  guard band: 16 MS/s, 20 samples (1250 ns) vs T1H max 950 -> both warn
PASS  independent (B07 loader + transitions) == srd decoder on the .sr
PASS  independent == srd with reset_us=9
PASS  independent == srd on the both-match edge case and the NOSYNC capture
       ... (스윕 8줄, §5)
PASS  sweep: colours right at every phase for >= 8 MS/s; 2 and 5 MS/s break (4 MS/s only by grid luck)
PASS  sweep: guard=yes -> 0 false timing warnings at >= 4 MS/s; guard=no -> some at 4/5 MS/s
PASS  sweep: srd decoder == independent implementation on every run (8 rates x 4 phases x 2 modes)
-- Part C: stacked TMP117 decoder on I2C + EVT1 report --
PASS  i2c_mini python output: START, ADDRESS WRITE 0x48, ACK, DATA WRITE 0x0F, ACK, START REPEAT, ...
PASS  Device_ID read 0x0117
PASS  temperatures 23.5000 / 23.5078 C (0x0BC0, 0x0BC1 x 1/128)
PASS  planted defect: CONFIG write to 0x49 -> 'NACK 49' warning
PASS  CONFIG readback 0x0220 (MOD=CC)
PASS  negative temp 0xFF80 -> -1.0000 C; read without pointer -> NOPTR
PASS  stacking tmp117_lab (inputs i2c) on ws2812_lab (outputs []) -> DecoderError
       ... (리포트 4줄, §5)
PASS  report: LED FAIL line = frame 2, after LED 5, gap 20.5..21.0 us (20 us injected + T0L 0.83 us)
PASS  report: I2C FAIL line names 0x49 and the CONFIG default readback
      clean: LED  net=LED_DIN fs=16MS/s frames=3 leds=[8, 8, 8] | PASS net=LED_DIN all bits within WS2812B timing (+-1 sample guard) | I2C  net=I2C2 fs=2MS/s TMP117@0x48 Device_ID=0x0117 T=[23.5000, 23.5078] C CONFIG=0x0C20 | PASS net=I2C2 TMP117@0x48 all transfers ACKed, Device_ID ok
PASS  report: clean board (no gap, right address) -> two PASS lines
PASS  decoders/ws2812_lab + tmp117_lab packages (gen_decoders.py) == in-file classes
31/31 passed  (0.7 s)
exit=0
```

실행 시간 0.84 s (`time python3 -I solution.py`: 0.79 s user, 스윕 0.6 s 포함). 결정성: `python3 -I gen_capture.py` 와 `python3 -I gen_decoders.py` 전후 `shasum -a 256 *.sr decoders/*/*.py` → `IDENTICAL`.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `matched[1]` 이면 무조건 `wait({0: 'r'})` | rise 와 skip 이 같은 샘플이면 현재 rise 를 건너뜀 → 다음 프레임 1비트 밀림 | `if not self.matched[0]: self.wait({0: 'r'})` |
| `h / fs >= 0.6e-6` (float) | 독립 구현·다른 툴과 600 ns 경계에서 갈림 | `h * 10**9 >= 600 * fs` |
| `n_reset = int(reset_us * fs / 1e6)` (내림) | 문턱보다 1 샘플 짧은 low 도 reset 으로 | `-(-reset_us * fs // 10**6)` (올림) |
| 타이밍 경고를 `n·Ts` 로 바로 판정 | 4–5 MS/s 에서 정상 신호에 경고 수백 개 → 경고를 아무도 안 봄 | ±1 샘플 가드 `(n±1)·Ts` |
| 샘플링률을 `start()` 에서 읽고 없으면 조용히 기본값 | metadata 가 없는 입력(일부 형식)에서 엉뚱한 타이밍 | `decode()` 에서 확인 → `SamplerateError` |
| 스택 디코더가 모르는 ptype 에서 예외 | upstream `i2c` 의 `'BITS'` 나 버전별 추가 ptype 에 죽음 | 모르는 cmd 는 무시 |
| ADDRESS 값을 8비트(0x90/0x91)로 비교 | upstream 출력은 7비트 주소 | `val == 0x48` |
| TMP117 pointer 를 트랜잭션마다 초기화 | 읽기 전용 트랜잭션(pointer 생략)을 해석 못 함 | pointer 를 `reset()` 에서만 None |
| 디코더 파일에 `import srd_shim` | PulseView 에서 import 실패 | `import sigrokdecode as srd` 만, shim 은 테스트 쪽에서 `install()` |
| 디코더 검증 = "PulseView 에서 보니 맞아 보임" | 1비트 밀림·경계 오류는 눈으로 안 보임 | 주입 진실 + 독립 구현 + upstream 디코더, 3중 비교 |

## 8. 면접 Q&A

<details>
<summary>Q1. libsigrokdecode API v3 의 wait() 기반 설계는 왜 나왔고, 디코더 성능에 무슨 의미가 있나요?</summary>

구형 API 는 파이썬 디코더가 샘플 묶음을 받아 샘플마다 루프를 돌았다 — 수백 MS/s 캡처에서 파이썬 루프가 병목. v3 는 디코더가 "다음에 관심 있는 사건"(에지, 레벨 조합, skip)을 조건으로 넘기면 C 쪽이 샘플을 훑어 맞는 샘플에서만 파이썬으로 돌아온다. 그래서 디코더 코드는 프로토콜 순서대로 읽히는 직선형 상태 기계가 되고, 대부분의 샘플은 파이썬을 거치지 않는다.
꼬리질문: "PulseView 에서 내 디코더만 느리다면?" → 레벨만 있는 조건(`{0: 'h'}`)이나 `skip: 1` 을 반복하면 결국 샘플마다 파이썬으로 돌아온다. 에지 항을 넣고, 긴 대기는 큰 skip 하나로.

</details>

<details>
<summary>Q2. WS2812 디코더에서 "reset 인가 다음 비트인가"를 wait 로 어떻게 표현하나요? 함정은?</summary>

fall 다음에 `wait([{0: 'r'}, {'skip': n_reset}])` — rise 가 먼저 오면 다음 비트, skip 이 먼저 끝나면 low 가 reset 이상. `self.matched` 로 구분한다. 함정은 둘 다 참인 경우(rise 가 정확히 n_reset 째 샘플): reset 처리 후 다시 rise 를 기다리면 현재 rise 를 놓친다. 또 n_reset 은 올림으로 계산해야 문턱보다 짧은 low 를 reset 으로 보지 않는다.
꼬리질문: "reset 문턱 50 µs 는 믿을 만한가?" → 칩 세대·클론마다 다르다(신형은 280 µs 로 적힘, 더 짧은 low 에 래치하는 사례도 보고). 그래서 옵션으로 두고, 증상과 맞는 값으로 재디코드해 본다(이 레슨의 `[8, 5, 3, 8]`).

</details>

<details>
<summary>Q3. 직접 짠 디코더가 맞다는 것을 어떻게 증명하나요?</summary>

세 겹: ① 결정적 생성기로 **진실을 주입**한 캡처(색·결함 위치를 안다) ② 같은 스펙을 다른 제어 흐름으로 짠 **독립 구현**과 annotation 이 (ss, es, class, key) 단위로 완전히 같은지 — 샘플링률·위상 스윕 전체에서 ③ 도구가 있으면 upstream 디코더(`rgb_led_ws281x`, `i2c`)와 대조. ①은 두 구현이 공유하는 스펙 오해를, ②는 wait 의미·off-by-one 을, ③은 shim 과 실제 라이브러리 차이를 잡는다. 정수 산술로 임계값을 정해야 ②가 비트 단위로 성립한다.
꼬리질문: "두 구현을 같은 사람이 짜면 독립이 아니지 않나?" → 맞다, 그래서 ①이 필요하다. 가능하면 독립 구현은 다른 사람이, 다른 언어/도구(예: 스코프 export 를 별도 스크립트로)로.

</details>

<details>
<summary>Q4. 16 MS/s LA 로 WS2812 타이밍 위반(±150 ns)을 판정할 수 있나요? 5 MS/s 가 4 MS/s 보다 나쁜 이유는?</summary>

n 샘플로 잰 폭의 진짜 값은 ((n−1)Ts, (n+1)Ts) 라 Ts = 62.5 ns 면 판정 여유가 ±150 → ±87.5 ns 로 준다. 명백한 위반(예: 기간이 µs 단위로 깨짐, T0H 가 700 ns)은 잡지만 경계값은 "UNRESOLVED" — 스코프로 IEEE-181 측정(HWLAB-B04). 디코드 자체는 판정선 600 ns 가 격자에 대해 어디 놓이느냐의 문제: 5 MS/s 는 600 ns = 정확히 3 샘플이라 417 ns 펄스가 3 샘플로 잡히면 1 로 오판, 4 MS/s 는 판정선이 2.4 샘플이라 T0H 최대 2 샘플 / T1H 최소 3 샘플로 운 좋게 갈린다. 보장 조건은 Ts < min(600 − T0H_max, T1H_min − 600) ≈ 163 ns.
꼬리질문: "그럼 판정을 `>` 로 바꾸면 5 MS/s 도 되지 않나?" → 그 비율에서는 되지만 다른 비율에서 같은 문제가 반대쪽으로 생긴다. 해법은 연산자 선택이 아니라 충분한 오버샘플링.

</details>

<details>
<summary>Q5. 왜 I2C 디코더를 고치지 않고 TMP117 디코더를 위에 쌓나요? 스택 디코더 설계에서 조심할 점은?</summary>

관심사 분리: 비트·바이트·ACK 는 검증된 upstream `i2c` 가 하고, 센서 지식(pointer, 레지스터 이름, 7.8125 m°C, DID)만 위에 둔다. 같은 I2C 디코드 위에 여러 센서 디코더, 주소 스캐너(연습 1)를 나란히 걸 수 있고, 아래 디코더의 버그 수정이 자동으로 반영된다. 조심할 점: 아래 출력 형식(7비트 주소, ptype 문자열)을 정확히 따르고, 모르는 ptype 은 무시, 상태는 START/STOP 에서 재동기화, 다른 주소 트래픽은 조용히 통과.
꼬리질문: "API v3 스택 디코더의 decode 시그니처는?" → logic 입력은 `decode(self)` + `wait()`, 스택 입력은 `decode(self, ss, es, data)` — 아래 디코더가 `put(…, OUTPUT_PYTHON, data)` 할 때마다 호출된다.

</details>

<details>
<summary>Q6. 현장에서 "LED 1~3 이 가끔 한 프레임 동안 이상한 색"입니다. LA 와 커스텀 디코더로 어떻게 좁히나요?</summary>

① 디코더 출력의 색이 펌웨어 의도와 같은지 — 같으면 비트스트림은 맞고 문제는 아날로그(레벨: 3.3 V → 5 V VIH, 링잉, 전원 강하 — 스코프, HWLAB-B02/B05) ② 경고: `GAP` 이 LED 5 뒤에 있으면 DMA underrun — 스트립이 그 low 에서 래치하면 6~8 데이터가 1~3 으로 간다(증상은 앞쪽, 원인은 프레임 중간). `reset_us` 를 줄여 재디코드하면 증상이 재현된다 ③ 펌웨어: DMA refill ISR 우선순위, 다른 ISR 과의 경합 — ISR 진입 GPIO 를 같은 캡처에 넣어 갭과 상관을 본다 ④ 고친 뒤 같은 디코더로 회귀 캡처 → `PASS`.
꼬리질문: "갭이 3 µs 면?" → 1.85 µs 기간 한계를 넘으니 `GAP` 이지만 대부분 칩은 래치하지 않는다 — 경고의 심각도는 데이터시트 reset 값과 실측(같은 로트)으로 정한다.

</details>

<details>
<summary>Q7. TMP117 one-shot 설정이 안 먹습니다. 캡처에서 무엇을 보면 되나요?</summary>

스택 디코더 출력에서 CONFIG 쓰기 트랜잭션을 찾는다. 이 캡처는 쓰기가 0x49 로 나가 주소 NACK(데이터는 보내지지도 않음), 이후 0x48 에서 CONFIG 를 읽으면 리셋값 0x0220(MOD=CC) — "쓰기가 안 들어갔다"의 증거. 원인 후보: ADD0 스트랩(0x48 GND/0x49 V+/0x4A SDA/0x4B SCL)과 펌웨어 주소 불일치, 보드 리비전마다 다른 스트랩. 펌웨어가 NACK 를 무시하고 진행한 것도 버그 — HAL 반환값 확인.
꼬리질문: "주소는 ACK 인데 값이 안 바뀌면?" → 쓰기 바이트 수(pointer + 2), MSB/LSB 순서, 쓰기 후 EEPROM busy/소프트 리셋 비트, 읽은 레지스터 pointer 가 맞는지(읽기 전용 트랜잭션은 직전 pointer).

</details>

## 9. 추가 연습

### 연습 1 — `i2cscan_lab`: 캡처판 i2cdetect

I2C 위에 쌓아 모든 주소 단계에 ACK/NACK annotation 을 붙이고, 주소별로 집계한다. EVT1 캡처에서 0x48 과 0x49 는?

<details>
<summary>답 보기</summary>

`ADDRESS *` 를 기억했다가 다음 `ACK`/`NACK` 에서 `[ss_주소, es_ACK]` annotation 을 낸다. 실제 출력: `census: {'0x48': {'ACK': 8, 'NACK': 0}, '0x49': {'ACK': 0, 'NACK': 1}}` — 트랜잭션 4개 × (W + Sr R) = ACK 8, 심어 둔 0x49 쓰기 NACK 1. 보드에 i2c-tools 를 못 올리는 부트로더 단계에서도 "캡처에 찍힌 주소"만으로 버스 구성이 나온다. sigrok-cli 는 `-P i2c,tmp117_lab -P i2c,i2cscan_lab` 처럼 같은 입력에 스택 두 개를 걸 수 있다(이 맥에서 실행 못 함); shim 은 체인 하나라 두 번 돌려 python 출력이 같은지 본다.

</details>

### 연습 2 — 내 디코더 위에 쌓기: 프레임 diff

`Ws2812Py(Ws2812Lab)` 가 reset 마다 `['FRAME', ['#RRGGBB', …]]` 를 OUTPUT_PYTHON 으로 내고(`outputs = ['ws2812']`), `LedDiffLab`(inputs `['ws2812']`)이 연속 프레임에서 바뀐 LED 를 표시한다. reset 50 µs 와 9 µs 에서 결과는?

<details>
<summary>답 보기</summary>

하위 클래스는 `start()` 에서 python 출력을 하나 더 register 하고, `put()` 을 감싸 rgb annotation 을 프레임 버퍼에 모으고, `end_frame()` 에서 FRAME 을 put. 원래 annotation 은 그대로(테스트: `ws2812_lab` 과 canonical 동일). 실제 출력: `diff (reset 50 us): ['CHG 0', 'CHG 8']`, `diff (reset 9 us): ['LEN 5', 'LEN 3', 'LEN 8']` — 짧은 reset 칩이라면 프레임 길이가 들쭉날쭉해진다는 것이 한 줄로 보인다. 디코더를 "출력 형식이 있는 부품"으로 만들면 위에 분석을 계속 쌓을 수 있다.

</details>

### 연습 3 — `decodable(fs)`: 샘플링률별 판정 보장

폭 w 펄스는 ⌊w/Ts⌋ 또는 ⌈w/Ts⌉ 샘플. 판정 `n ≥ m = ⌈600/Ts⌉` 가 모든 위상에서 맞을 조건을 정수 산술로 쓰고(T0H ≤ 437, T1H ≥ 813 ns), 1–25 MS/s 에 적용해 Part B 스윕과 대조하라.

<details>
<summary>답 보기</summary>

`⌈w0_max·fs/10⁹⌉ < m ≤ ⌊w1_min·fs/10⁹⌋`. 실제 출력: `guaranteed MS/s: [4, 6, 7, 8, …, 25]`, `NOT guaranteed: [1, 2, 3, 5]` — 스윕의 2/4/5/8/10/16/20/25 결과와 전부 일치. 비단조(4 는 되고 5 는 안 됨)가 핵심이고, 충분조건 Ts < min(600−437, 813−600) = 163 ns (fs > 6.13 MS/s) 위로는 전부 보장. "LA 샘플링률은 높을수록 좋다"의 정량판이며, 디코드 경계를 데이터시트 숫자만으로 미리 계산할 수 있다는 점이 면접 포인트.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py ; echo "exit=$?"     # 11/11 passed, exit=0 (0.69 s)
```

## 10. 핵심 정리

- sigrok 디코더 = 디렉터리(`__init__.py` + `pd.py`) 안의 `class Decoder(srd.Decoder)`: `api_version = 3`, id/channels/options/annotations/annotation_rows, `metadata()` → `start()`(register) → `decode()`.
- `wait(dict | [dict…])`: dict 안 AND, 리스트 OR, 항 `r f e h l n` + `skip`. 돌아오면 `self.samplenum`, `self.matched`. 에지 항이 있어야 빠르다.
- `put(ss, es, out_ann, [cls, [긴…짧은]])`. 짧은 텍스트를 기계용 키로 쓰면 테스트·리포트가 쉬워진다.
- WS2812: `wait r → wait f → wait [r, skip n_reset]`. 두 조건 동시 매치에서 현재 rise 를 잃지 말 것. reset 문턱은 칩마다 다르니 옵션으로.
- 임계값은 정수 산술(`n·10⁹ ≥ ns·fs`), 타이밍 경고는 ±1 샘플 가드. 디코드 보장 조건 Ts < min(판정선 − T0H_max, T1H_min − 판정선) — 그 아래는 격자 운(4 MS/s ○, 5 MS/s ✗).
- 검증 3중: 주입 진실 · 독립 구현(annotation 완전 일치) · upstream 디코더.
- 스택 디코더: `inputs = ['i2c']`, `decode(ss, es, [cmd, val])`, 7비트 주소, 모르는 ptype 무시, pointer 같은 디바이스 상태는 트랜잭션 밖에.
- 리포트는 위치(프레임/LED, 주소)와 다음 행동(DMA ISR, ADD0 스트랩)을 담은 한 줄.

## 참고자료

- [sigrok wiki — Protocol decoder HOWTO](https://sigrok.org/wiki/Protocol_decoder_HOWTO) · [Protocol decoder API](https://sigrok.org/wiki/Protocol_decoder_API) · [API v3 queries (wait 조건)](https://sigrok.org/wiki/Protocol_decoder_API/Queries)
- [libsigrokdecode 소스 — decoders/](https://github.com/sigrokproject/libsigrokdecode/tree/master/decoders): `i2c/pd.py`(OUTPUT_PYTHON 형식 docstring), `rgb_led_ws281x/pd.py`, `uart/pd.py`(wait + skip 패턴), 스택 예 `ds1307`, `lm75`, `eeprom24xx`
- [sigrok-cli 매뉴얼](https://sigrok.org/wiki/Sigrok-cli) — `-P` 스택·옵션, `-A` annotation 필터, `--protocol-decoder-samplenum`
- Worldsemi WS2812B 데이터시트 (구형: T0H 0.4 / T1H 0.8 µs ±150 ns, RES > 50 µs; V5 계열은 값이 다름 — 확인 필요)
- TI TMP117 데이터시트 — 레지스터 맵, ADD0 주소 표, Temp 해상도 7.8125 m°C, Configuration MOD 필드 (이 레슨 값은 대표값)
- NXP UM10204 I2C-bus specification — Standard-mode 타이밍 (tHD;STA, tLOW, tHIGH)
- 관련 레슨: HWLAB-B07(.sr 형식·로더·±1 샘플), HWLAB-B02(로직 레벨·VIH), HWLAB-B04(IEEE-181 측정), HWLAB-B05(프로브·링잉), HWLAB-M05(LA 타이밍 컴플라이언스), COMM-A10(SPI/I2C/UART 디코더)
