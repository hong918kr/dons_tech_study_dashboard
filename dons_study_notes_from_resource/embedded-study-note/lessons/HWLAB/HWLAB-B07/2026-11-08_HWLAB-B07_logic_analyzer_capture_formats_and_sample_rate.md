---
id: HWLAB-B07
time: 70분
updated: 2026-11-08
---
# HWLAB-B07 LA 캡처 포맷과 샘플링률: sigrok .sr·CSV·VCD

## 학습 목표

- 로직 애널라이저(logic analyzer, LA) 캡처의 세 가지 흔한 형식 — sigrok 세션 `.sr`(zip 안에 **샘플**), Saleae식 CSV(**transition**만), VCD(value change dump, transition만) — 을 하나의 공통 transition 리스트 `{채널: [(t_ps, level), ...]}` 로 읽는 로더를 쓴다.
- `.sr` 의 구조(`version` · `metadata` INI · `logic-1-N` 청크, `unitsize`, probe 번호 ↔ 비트)를 설명하고, 청크 순서·unitsize·샘플링률 문자열 같은 함정을 테스트로 막는다.
- 샘플링이 에지 위치를 **[0, Ts)** 만큼 늦게 보이게 하고, 두 에지 사이 간격은 **±1 샘플** 불확실하다는 것을 실험으로 보이고, 펄스가 샘플 사이로 사라져 SPI 디코드가 깨지는 샘플링률 경계를 찾는다.
- "디코드 결과가 이상하다"를 보고 **DUT 고장인지 캡처 무효인지** 먼저 가르는 리포트(오버샘플링 비, 최소 펄스 폭)를 만든다.

## 1. 문제

첫 보드에서 SPI 플래시가 "안 읽힌다"는 보고의 상당수는 보드가 아니라 **캡처**가 틀린 경우다 — 그래서 Rivian/NVIDIA/Apple 검증 면접에서 "LA로 무엇을 몇 MS/s로 봤고, 그 숫자를 왜 믿느냐"를 묻는다.

상황: EVT1 보드의 MCU가 SPI NOR 플래시 U12(W25Q128JV 가정)를 10 MHz SCLK, mode 0 으로 읽는다. 부트가 가끔 실패해서 버스를 LA로 잡았다. 책상 위에는 두 가지 LA가 있다 — sigrok/PulseView로 쓰는 FX2 기반 저가 LA(fx2lafw, 최대 24 MS/s급)와 100 MS/s 이상 LA. 캡처는 팀마다 다른 형식으로 돌아다닌다: PulseView가 저장한 `.sr`, Saleae Logic에서 내보낸 CSV, 시뮬레이션·다른 도구의 VCD. 어떤 형식이든 같은 분석 코드에 넣고 싶다.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="hwb07-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">캡처 형식 → 공통 transition 리스트 → 분석</text>
  <rect x="10" y="40" width="120" height="56" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="70" y="64" fill="currentColor" text-anchor="middle">EVT1 SPI 버스</text>
  <text x="70" y="82" fill="currentColor" text-anchor="middle" font-size="12">CS_N SCLK MOSI MISO</text>
  <rect x="10" y="130" width="120" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="70" y="154" fill="currentColor" text-anchor="middle">LA 샘플러</text>
  <text x="70" y="172" fill="currentColor" text-anchor="middle" font-size="12">fs, 임계값</text>
  <rect x="180" y="34" width="230" height="74" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="192" y="54" fill="currentColor" font-weight="bold">.sr (zip) — 샘플</text>
  <text x="192" y="72" fill="currentColor" font-size="12">version · metadata(INI)</text>
  <text x="192" y="90" fill="currentColor" font-size="12">logic-1-1 … logic-1-N (unitsize B/샘플)</text>
  <rect x="180" y="122" width="230" height="56" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="192" y="142" fill="currentColor" font-weight="bold">CSV — transition</text>
  <text x="192" y="160" fill="currentColor" font-size="12">Time [s],ch… (fs 정보 없음)</text>
  <rect x="180" y="192" width="230" height="56" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="192" y="212" fill="currentColor" font-weight="bold">VCD — transition</text>
  <text x="192" y="230" fill="currentColor" font-size="12">$timescale · #t · 0! 1! (EMU-B07)</text>
  <rect x="460" y="112" width="130" height="72" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="525" y="136" fill="currentColor" text-anchor="middle" font-weight="bold">공통 표현</text>
  <text x="525" y="154" fill="currentColor" text-anchor="middle" font-size="12">{ch: [(t_ps, lvl)]}</text>
  <text x="525" y="172" fill="currentColor" text-anchor="middle" font-size="12">+ ts_ps 또는 None</text>
  <rect x="620" y="70" width="110" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="675" y="97" fill="currentColor" text-anchor="middle">SPI 디코드</text>
  <rect x="620" y="182" width="110" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="675" y="209" fill="currentColor" text-anchor="middle">유효성 리포트</text>
  <g stroke="currentColor" fill="none" marker-end="url(#hwb07-a)">
    <path d="M70 96 L70 128"/><path d="M130 150 L178 72"/><path d="M130 158 L178 150"/><path d="M130 166 L178 220"/>
    <path d="M410 72 L458 136"/><path d="M410 150 L458 148"/><path d="M410 220 L458 162"/>
    <path d="M590 136 L618 94"/><path d="M590 160 L618 204"/>
  </g>
  <text x="10" y="272" fill="currentColor" font-size="12">.sr 은 fs 를 알고, CSV/VCD 는 transition 시각만 안다 → Part C 판정이 달라진다.</text>
</svg>
```

공통 표현: `make_capture(names, tr, ts_ps, t_end)` — `tr[name]` 의 첫 원소는 캡처 시작의 초기 레벨, 이후는 레벨이 바뀐 시각만. 시간은 **정수 ps** (float 초 금지 — 형식 간 비교가 비트 단위로 같아야 한다). 레벨은 0/1, 모르면(VCD `x`/`z`) `None`.

### Part A — 세 포맷 → 공통 transition 리스트

`parse_samplerate`("100 MHz" → 100000000), `samples_to_transitions`(unitsize 바이트 little-endian 샘플 → transition), `load_sr`, `load_saleae_csv` 를 작성한다. VCD 로더 `load_vcd` 는 EMU-B07 파서의 최소판으로 **주어진다**. 커밋된 같은 캡처의 세 파일(`evt1_spi_100M.sr/.csv/.vcd`)이 **완전히 같은 transition** 으로 읽혀야 한다.

### Part B — 포맷 함정과 샘플링률 실험

- 함정: 청크가 zip 에 섞인 순서로 들어있음(`logic-1-10` 이 `logic-1-2` 앞), 구형 단일 `logic-1`, unitsize=2(12채널), 잘린 샘플 데이터, CSV 의 음수 시각(트리거 이전), VCD `$timescale 10 ns` 와 `x`.
- 실험: 연속 시간 SPI 파형(`gen_capture.spi_waveform`: SCLK 10 MHz, LA가 보는 high 46 ns, 에지 지터 σ 0.7 ns)을 `resample(wave, fs, phase)` 로 20/50/100 MS/s 샘플링 → (1) 에지 위치 오차가 [0, Ts) 인지, 한 번의 캡처에서는 왜 몰려 있는지 (2) 측정 주기가 ±1 샘플로 양자화되는지 (3) `decode_spi` (mode 0) 가 깨지는 샘플링률 경계(16~100 MS/s 스윕, 200 프레임)를 찾는다.

### Part C — EVT1 U12 JEDEC ID 캡처 리포트

`capture_report(cap)` → `INVALID`(캡처를 못 믿음: fs 모름 / 오버샘플링 < 4× / SCLK high 가 1 샘플 이하) · `PASS`(JEDEC = EF 40 18) · `FAIL`(유효한 캡처인데 DUT가 틀림, 힌트 포함). 20 MS/s `.sr` 은 INVALID, 100 MS/s `.sr` 은 PASS, 심어 둔 결함(MISO stuck-high, 100 MS/s)은 FAIL 이어야 한다. 마지막으로 `.sr` → VCD/CSV → 재로딩 왕복이 원본과 같아야 한다.

## 2. Problem (English)

> **Logic-analyzer capture loader and sample-rate study.**
>
> Represent a digital capture as `{channel: [(t_ps, level), ...]}` (first entry = initial level, then changes only; integer picoseconds)
> plus the sample period `ts_ps` (or `None` if unknown). Using only the Python 3 standard library:
>
> 1. Load a sigrok v2 session (`.sr` = zip with `version`, INI `metadata` with `[device 1]` keys `samplerate`, `unitsize`,
>    `total probes`, `probeN`, `capturefile`, and binary chunks `logic-1-1..N`, each sample `unitsize` bytes little-endian,
>    probe N = bit N-1), a Saleae-style CSV (`Time [s],ch...`, one row per transition, possibly negative times) and a VCD.
>    All three files of the same capture must produce identical transitions.
> 2. Given a continuous-time 10 MHz SPI waveform, model sampling at `fs` with phase `phi` *without iterating samples*: an edge
>    at `t` appears at sample `ceil((t-phi)/Ts)`; an even number of toggles between two samples vanishes. Show that edge error
>    lies in `[0, Ts)`, intervals are off by up to ±1 sample, and find the lowest rate at which a mode-0 decoder never fails.
> 3. `capture_report(cap)` returns `INVALID` when the capture cannot support a verdict (unknown `fs`, oversampling < 4x, or SCLK
>    high pulses of < 2 samples), else `PASS`/`FAIL` on the JEDEC ID `EF 40 18`, with a root-cause hint for stuck MISO.
>
> Example: the 20 MS/s capture decodes the JEDEC frame as 21 bits and must be reported as
> `INVALID: oversample 2.0x < 4.0x, SCLK high = 1 sample -> re-capture at >= 40 MS/s`, not as a flash failure.

## 3. 시작 코드

<!-- code: starter.py -->

캡처 생성기 (결정적: 시드 707, zip 타임스탬프 고정). 커밋된 4개 파일(`evt1_spi_100M.sr` 1552 B, `.csv` 6369 B, `.vcd` 3374 B, `evt1_spi_20M.sr` 629 B)을 다시 만든다:

<!-- code: gen_capture.py -->

```bash
cd lessons/HWLAB/HWLAB-B07
python3 -I starter.py ; echo "exit=$?"      # TODO 상태
```

실제 출력 (일부):

```text
-- Part A: loaders --
FAIL  parse_samplerate('100 MHz') == 100000000  (NotImplementedError: parse_samplerate)
...
      (loader raised NotImplementedError: load_sr)
FAIL  .sr: names/ts/length from metadata  (TypeError: 'NoneType' object is not subscriptable)
...
PASS  VCD timescale 10 ns, x initial, 'b1 id' form
-- Part B: sample-rate experiment (10 MHz SCLK, high 46 ns) --
FAIL  resample(100M) == brute-force sampling  (NotImplementedError: resample)
...
FAIL  round trip .sr -> VCD/CSV -> load == original  (NotImplementedError: load_sr)
1/33 passed
exit=1
```

`resample` 테스트의 기준은 `gen_capture.sample_bytes` — 샘플 시각마다 `bisect` 로 레벨을 구하는 **무차별(brute-force) LA** 다. 여러분의 `resample` 은 에지만 보고 같은 결과를 내야 한다 (스윕을 빠르게 돌리기 위해).

## 4. 힌트

<details>
<summary>힌트 1 — .sr 은 그냥 zip + INI</summary>

`zipfile.ZipFile(src)` 로 열고 `z.read("version")` 이 `b"2"` 인지 본다. `metadata` 는 주어진 `parse_ini` 로 읽으면 `meta["device 1"]["samplerate"] == "100 MHz"`. 청크 멤버 이름은 `capturefile` 값 + `-<숫자>`. 정렬 키는 **`int(숫자)`** — 문자열 정렬은 `logic-1-10` 을 `logic-1-2` 앞에 둔다.

</details>

<details>
<summary>힌트 2 — 샘플 → transition</summary>

샘플 k 의 워드: `int.from_bytes(data[k*u:(k+1)*u], "little")`. probe N 은 비트 N-1. 이전 워드와 같으면 건너뛰고, 다르면 바뀐 비트의 채널만 `(k*ts_ps, bit)` 추가. `len(data) % unitsize != 0` 이면 잘린 청크 — 조용히 버리지 말고 `ValueError`.

</details>

<details>
<summary>힌트 3 — CSV 시간은 Fraction 으로</summary>

`float("0.000000570") * 1e12` 는 `569999.9999…` 가 될 수 있다. `round(Fraction(cell) * 10**12)` 는 정확히 `570000`. 음수(트리거 이전)도 그대로 둔다. CSV 에는 샘플 주기가 없으므로 `ts_ps=None`.

</details>

<details>
<summary>힌트 4 — resample 은 에지마다 ceil 하나</summary>

샘플 k 시각 `t_k = phase + k*Ts`, 샘플은 "그 시각까지 일어난 모든 에지"를 본다 → 에지 t 는 `k = ceil((t - phase)/Ts)` 에 처음 보인다. 같은 k 에 토글이 2번(펄스 하나)이면 그 펄스는 **존재하지 않았던 것**이 된다 → dict 로 k 별 토글 수를 세고 홀수일 때만 transition.

</details>

<details>
<summary>힌트 5 — 디코더와 리포트</summary>

네 채널 transition 을 `(t, ch, v)` 로 합쳐 정렬하고 **같은 t 의 변화를 모두 적용한 뒤** CS_N/SCLK 에지를 판정한다 (LA 의 한 샘플 = 모든 채널 동시 스냅샷). 리포트는 디코드 **전에** 캡처 자체를 판정: `fs / f_SCLK`(SCLK rising 간격 중앙값)와 `min(SCLK high 폭)/Ts`.

</details>

## 5. 풀이

### Part A — 세 형식의 실제 구조

**sigrok 세션 v2 (`.sr`)** — sigrok 위키 "File format:Sigrok/v2" 와 libsigrok `src/output/srzip.c` 기준:

| zip 멤버 | 내용 | 이 레슨 파일 |
|---|---|---|
| `version` | ASCII 정수 하나, 현재 `2` | `2` |
| `metadata` | INI (GKeyFile) — `[global]` `sigrok version`, `[device 1]` `capturefile`, `total probes`, `samplerate`, `total analog`, `probeN`, `analogN`, `unitsize` | 아래 |
| `logic-1-1` … `logic-1-N` | 로직 샘플 바이너리 청크. 구형은 단일 `logic-1` (위키: deprecated 지만 지원) | 200 샘플씩 10개 |
| `analog-1-<ch>-N` | 아날로그 채널 청크 (srzip 은 float 배열로 씀) | 없음 |

`gen_capture.py` 가 쓴 실제 metadata (`python3 -I -c` 로 zip 에서 읽어 확인):

```text
[global]
sigrok version=0.5.2

[device 1]
capturefile=logic-1
total probes=4
samplerate=100 MHz
total analog=0
probe1=CS_N
probe2=SCLK
probe3=MOSI
probe4=MISO
unitsize=1
```

- **unitsize** = 샘플당 바이트 = `ceil(로직 채널 수 / 8)` (srzip.c 가 이렇게 계산). 4채널 → 1, 12채널 → 2. 샘플은 연속으로 붙어 있고 probe1 = 비트 0. 첫 청크 일부: `0e 0e 0c 0c …` = MISO(b3)=1, MOSI(b2)=1, SCLK(b1)=1↔0, CS_N(b0)=0.
- **청크**: libsigrok 은 버퍼(CHUNK_SIZE 4 MiB)가 찰 때마다 `logic-1-<다음 번호>` 를 추가한다. 번호는 숫자 순서가 곧 시간 순서. 이 레슨은 함정을 실제로 만들려고 200 샘플(=200 B)씩 끊었다.
- **samplerate** 는 문자열(`sr_samplerate_string`)로 저장 — "100 MHz" 처럼 SI 접두사 + 단위. 정수 Hz 로 바꿔야 Ts 가 정확하다 (100 MHz → 10000 ps).
- `version`/`metadata` 는 우리가 직접 쓴 것이다. `sigrok version=0.5.2` 는 임의로 넣은 문자열이고, 이 파일이 실제 PulseView 에서 열리는지는 **도구가 없어 확인하지 못했다**.

> 불확실한 점(명시): ① 비활성(disabled) 채널이 섞였을 때 probeN 번호·비트 위치가 압축되는지는 libsigrok 버전별로 확인이 필요하다 — 이 로더는 "probeN → 비트 N-1" 만 가정한다. ② `sr_samplerate_string` 의 비정수 표기(예: 1.5 MHz)는 정확한 자릿수를 확인하지 못해 파서를 관대하게 만들었다. ③ zip 압축 방식은 libzip 기본값을 따른다고만 알고 있다 (우리는 deflate 로 씀; 리더는 무관). ④ 트리거 위치 같은 추가 메타데이터는 이 레슨에서 다루지 않는다.

**Saleae식 CSV** — 헤더 `Time [s],<채널 이름>…` 뒤에 *어느 채널이든 바뀐 시각마다* 한 줄 (모든 채널 값을 다 적음). 핵심 차이: **샘플 주기·캡처 끝 시각이 없다.** 그래서 이 레슨의 CSV 캡처는 `ts_ps=None`, `t_end=None`. 시각은 트리거 기준이라 음수일 수 있다. 헤더 문구·소수 자릿수는 Logic 버전/설정마다 달라서 로더는 "첫 열이 Time 으로 시작"만 요구한다.

```text
Time [s],CS_N,SCLK,MOSI,MISO
0.000000000,1,0,0,1
0.000000510,0,0,0,1
0.000000520,0,0,1,1
0.000000570,0,1,1,1
```

CS_N 하강(510 ns) → 10 ns 뒤 MOSI=1(0x9F 의 MSB) → 60 ns 뒤 첫 SCLK rise. 100 MS/s 라 모든 시각이 10 ns 배수다.

**VCD** — IEEE 1364 의 value change dump. `$timescale 1 ns $end`, `$var wire 1 ! CS_N $end`, `#510` 다음 `0!`. 로더는 EMU-B07 파서의 최소판(1비트 wire, `x/z` → None, `b1 !` 형식). 역시 샘플 주기 정보는 없다 (timescale 은 시간 *단위*이지 LA 샘플 주기가 아니다).

| 형식 | 저장하는 것 | fs 를 아나 | 크기(이 캡처) | 강점 / 약점 |
|---|---|---|---|---|
| `.sr` | 모든 샘플 (zip 압축) | 예 (`samplerate`) | 1552 B (1974 샘플) | 원본 그대로, 재디코드 가능 / 바이너리, 도구 필요 |
| CSV | transition 행 | 아니오 | 6369 B | 스프레드시트·grep / 해상도·끝 시각 손실, 자릿수 반올림 위험 |
| VCD | transition | 아니오 (단위만) | 3374 B | GTKWave·시뮬레이터 공용 / 다채널 대용량에서 텍스트 비대 |

조용한(idle) 구간이 긴 캡처에서는 transition 형식이 훨씬 작고, 매 샘플이 바뀌는 고속 신호에서는 `.sr` 이 작다. 그러나 **크기보다 중요한 것은 fs 를 잃느냐**다 (Part C).

### Part B — 샘플링이 시간을 어떻게 바꾸나

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">SCLK high 46 ns, 주기 100 ns — 샘플 격자에 따라 보이는 것</text>
  <text x="10" y="62" fill="currentColor">실제</text>
  <path d="M70 75 L110 75 L110 45 L202 45 L202 75 L310 75 L310 45 L402 45 L402 75 L510 75 L510 45 L602 45 L602 75 L700 75" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="130" y="38" fill="currentColor" font-size="12">46 ns</text>
  <text x="10" y="128" fill="currentColor">100 MS/s</text>
  <g stroke="currentColor" stroke-width="1" opacity=".6">
    <path d="M90 140 L90 148"/><path d="M110 140 L110 148"/><path d="M130 140 L130 148"/><path d="M150 140 L150 148"/><path d="M170 140 L170 148"/><path d="M190 140 L190 148"/><path d="M210 140 L210 148"/>
  </g>
  <path d="M70 135 L110 135 L110 110 L210 110 L210 135 L310 135 L310 110 L410 110 L410 135 L510 135 L510 110 L610 110 L610 135 L700 135" fill="none" stroke="currentColor" stroke-width="2"/>
  <text x="440" y="104" fill="currentColor" font-size="12">에지가 [0, 10 ns) 늦게 보임, 폭 40/50 ns</text>
  <text x="10" y="200" fill="currentColor">20 MS/s</text>
  <g class="f-warn" stroke="currentColor">
    <circle cx="100" cy="225" r="4"/><circle cx="200" cy="225" r="4"/><circle cx="300" cy="225" r="4"/>
    <circle cx="400" cy="225" r="4"/><circle cx="500" cy="225" r="4"/><circle cx="600" cy="225" r="4"/>
    <circle cx="150" cy="225" r="4"/><circle cx="250" cy="225" r="4"/><circle cx="350" cy="225" r="4"/>
    <circle cx="450" cy="225" r="4"/><circle cx="550" cy="225" r="4"/><circle cx="650" cy="225" r="4"/>
  </g>
  <text x="70" y="250" fill="currentColor" font-size="12">샘플 간격 50 ns &gt; high 46 ns → 위상이 나쁘면 high 구간 안에 샘플이 하나도 없다</text>
  <path d="M70 190 L150 190 L150 170 L200 170 L200 190 L350 190 L350 170 L400 170 L400 190 L550 190 L550 170 L600 170 L600 190 L700 190" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="5 3"/>
  <text x="430" y="166" fill="currentColor" font-size="12">high 가 1 샘플 폭(50 ns)으로만 보이거나 사라짐</text>
  <text x="10" y="285" fill="currentColor" font-size="12">경계: 모든 high 에 샘플이 들어가려면 Ts &lt; t_high(min) → fs &gt; 1/46 ns ≈ 21.7 MS/s (+지터 여유)</text>
</svg>
```

**1. 에지 위치 오차.** 샘플 k 는 시각 `t_k` 까지의 모든 에지를 반영하므로 에지 t 는 `k = ceil((t−φ)/Ts)` 에 보이고, 보이는 시각 − 실제 시각 ∈ **[0, Ts)**. 두 에지 사이 간격(펄스 폭, 주기, setup)은 오차의 차이라 **(−Ts, +Ts)**, 즉 ±1 샘플이다. 중간값 `t_k − Ts/2` 로 보정하면 단일 에지는 ±Ts/2 가 되지만 간격의 ±1 샘플은 그대로다.

실제 측정 (solution 출력):

```text
       50 MS/s  this one run: rising-edge error 9.7..16.0 ns (10 MHz vs 50 MS/s = fixed phase)
      100 MS/s  Ts=10ns  40 runs, random phase: rising-edge error min  0.0  mean  4.9  max 10.0 ns  (n=1280)
       50 MS/s  Ts=20ns  40 runs, random phase: rising-edge error min  0.0  mean  7.9  max 20.0 ns  (n=1280)
       20 MS/s  Ts=50ns  40 runs, random phase: rising-edge error min  0.0  mean 24.1  max 48.1 ns  (n=1189)
      100 MS/s  measured SCLK period histogram [ns:count] [(90, 1), (100, 139), (110, 1)]
       50 MS/s  measured SCLK period histogram [ns:count] [(100, 141)]
```

(`max 10.0`, `20.0` 은 출력 반올림; 테스트는 `< Ts` 를 확인한다.) 두 가지가 보인다.

- **한 번의 캡처 안에서는 오차가 몰려 있다**(50 MS/s: 9.7~16.0 ns). 10 MHz 와 50 MS/s 가 정수비(coherent)라 SCLK 에지와 샘플 격자의 위상이 거의 고정되고, 지터(σ 0.7 ns)만큼만 흔들린다. 그래서 측정 주기도 100 ns 로만 나온다 — "주기가 정확히 100 ns 로 측정됐다"는 것이 정밀도를 뜻하지 않는다. 비동기·무작위 위상으로 여러 번 잡아야 [0, Ts) 전체가 드러난다.
- 20 MS/s 에서 rising 에지 수가 1280 이 아니라 1189 — 펄스가 사라졌다.

**2. 펄스 소실 조건.** 한 high 구간 [r, f) 안에 샘플이 하나도 없으면 (`ceil` 결과가 같으면) 그 펄스는 캡처에 존재하지 않는다. `t_high < Ts` 이면 위상에 따라 반드시 일어날 수 있고, `t_high ≥ Ts` 이면 절대 안 일어난다. LA 가 보는 high 폭이 46 ns 인 이유(합성 가정): MCU 출력의 rise/fall 비대칭과 LA 입력 임계값 — 느린 상승 에지를 높은 임계값으로 자르면 high 가 짧아진다 (임계값·노이즈 마진은 HWLAB-B02).

**3. 디코드 실패 경계 (200 JEDEC 프레임, 무작위 위상):**

```text
      failed frames /200 by MS/s: {16: 200, 18: 200, 20: 24, 22: 22, 23: 0, 24: 0, 25: 0, 26: 0, 28: 0, 30: 0, 40: 0, 50: 0, 100: 0}  (0.1 s)
```

| fs | Ts | Ts vs t_high (46 ns) | 실패 | 해석 |
|---|---|---|---|---|
| 16, 18 MS/s | 62.5, 55.6 ns | Ts > t_high, 비정수비 | 200/200 | 위상이 클럭마다 미끄러져 32클럭 중 어딘가에서 반드시 빠짐 |
| 20 MS/s | 50 ns | Ts > t_high, **정수비** | 24/200 (12%) | 위상이 프레임 내내 고정 → 나쁜 위상(약 4/50+지터)에 걸린 프레임만 통째로 실패 |
| 22 MS/s | 45.5 ns | Ts ≈ t_high − 0.5 ns | 22/200 | 지터로 high 가 Ts 보다 짧아지는 순간 + 위상 미끄러짐 |
| 23~100 MS/s | ≤ 43.5 ns | t_high − 2.5σ 이상 여유 | 0 | 모든 high 에 샘플이 들어감 |

경계 ≈ 23 MS/s ≈ `1 / (t_high − 몇 σ)`. 의미 있는 결론 세 가지:

1. 20 MS/s 의 "88% 는 디코드 성공"이 가장 위험하다 — 한 번 잡아서 맞으면 믿게 된다.
2. 24 MS/s fx2lafw(Ts 41.7 ns)는 이 모델에서 0 실패지만 여유는 4 ns 남짓(high 폭 지터 σ≈1 ns 의 4배)이다. LA 가 보는 듀티가 41% 아래로만 내려가도(임계값, 온도, 다른 보드) 깨지기 시작한다.
3. 디코드만 하려면 경계 위면 되지만, **타이밍을 재려면** 오차 ±1 샘플이 스펙 대비 작아야 한다. 경험칙: 디코드 ≥ 4×, 타이밍 측정 ≥ 10× 신호 주파수 (10 MHz → 40 / 100 MS/s). 나이퀴스트(2×)는 "정현파 복원" 조건이지 디지털 펄스 검출 조건이 아니다.

`decode_spi` 의 의미론: 같은 샘플에서 SCLK rise 와 MOSI 변화가 함께 보이면 **새 값**을 읽는다 (샘플은 모든 채널의 동시 스냅샷). mode 0 은 데이터가 falling 후에 바뀌므로 rising 시점에는 반 주기 가까이 안정 — 그래서 경계를 결정하는 것은 데이터 setup 이 아니라 **클럭 펄스 소실**이다. CPOL/CPHA 를 잘못 고른 경우는 연습 2.

### Part C — 캡처 판정이 DUT 판정보다 먼저

실제 리포트 (solution 출력):

```text
      CAPTURE evt1_spi_20M.sr fs=20MS/s Ts=50.0ns
      SCLK f=10.00MHz oversample=2.0x min_high=1 smp (50.0ns)
      DECODE frames=3 ok=2 JEDEC=none bits=[21, 16, 96]
      INVALID: oversample 2.0x < 4.0x, SCLK high = 1 sample -> re-capture at >= 40 MS/s; decode above is NOT evidence about the DUT
      CAPTURE evt1_spi_100M.sr fs=100MS/s Ts=10.0ns
      SCLK f=10.00MHz oversample=10.0x min_high=4 smp (40.0ns)
      DECODE frames=3 ok=3 JEDEC=EF4018 bits=[32, 16, 96]
      PASS: JEDEC=EF4018 (expected EF4018), 3/3 frames clean
      CAPTURE evt1_u12_miso_stuck(100M) fs=100MS/s Ts=10.0ns
      SCLK f=10.00MHz oversample=10.0x min_high=4 smp (40.0ns)
      DECODE frames=3 ok=3 JEDEC=FFFFFF bits=[32, 16, 96]
      FAIL: JEDEC=FFFFFF expected EF4018, 3/3 frames clean -> MISO stuck high: flash not driving DO (check VCC, CS_N routing, HOLD#/RESET#, pin mux)
```

20 MS/s 캡처는 JEDEC 프레임이 21비트로 디코드됐다 — 순진하게 보면 "플래시 응답 이상, U12 교체". 하지만 같은 버스를 100 MS/s 로 잡으면 32비트, EF 40 18. 리포트가 디코드 **위에** 캡처 유효성 두 숫자를 두는 이유다:

- **oversample = fs / f_SCLK** — f_SCLK 는 CS_N low 구간 안의 rising 간격 중앙값 (펄스가 몇 개 빠져도 중앙값은 안전).
- **min_high (샘플 수)** — 1 이면 그 펄스의 실제 폭은 (0, 2Ts) 어디든 될 수 있다 = 글리치·소실과 구분 불가.

반대로 100 MS/s 에서 MISO 가 전부 1 이면 캡처는 유효하므로 그것은 DUT 문제다. 0xFF 는 "플래시가 DO 를 안 구동 + 풀업"의 전형 (W25Q 계열은 명령 바이트 동안에도 DO 가 hi-Z 라 첫 MISO 바이트는 풀업이면 FF — 이 레슨 데이터도 그렇게 모델링). 풀업이 없는 보드는 FF 대신 떠 있는 값이 나오므로 힌트는 "가능성"으로 읽어야 한다.

| 원인 | 증상 (LA) | 가르는 측정 |
|---|---|---|
| LA 샘플링률 부족 | 비트 수가 8의 배수 아님, 캡처마다 다른 결과, high 1 샘플 | fs 올려 재캡처, `min_high`/oversample |
| LA 임계값 부적절 (1.8 V 버스에 3.3 V 설정 등) | 펄스 폭이 이상하게 짧음/누락 | 스코프로 같은 지점 (HWLAB-B04/B05), 임계값 변경 |
| 프로브 그라운드 불량·긴 리드 | 링잉이 이중 에지로 보임 | 스코프 + 스프링 그라운드 (HWLAB-B05) |
| 플래시 무전원·CS 단선·HOLD# low | MISO 전부 FF (풀업) / 부유 | 레일 전압, CS_N 연속성, HOLD# 레벨 |
| 핀 먹스 오설정 | SCLK 자체가 없음 | 디코드 전에 SCLK transition 수 확인 |
| 모드 착각 (CPOL/CPHA) | 1비트 밀림, 위상에 따라 가끔 맞음 | 연습 2, 데이터시트 모드 |

**도구가 있을 때 (sigrok-cli — 이 맥에는 설치돼 있지 않아 실행하지 않았다, 출력 없음):**

```bash
# 데모 드라이버로 .sr 만들기 -> 이 레슨 로더로 읽기
sigrok-cli -d demo --channels D0,D1,D2,D3 --config samplerate=1m --samples 10000 -o demo.sr
# 왕복: 같은 .sr 을 sigrok 의 VCD/CSV 출력 모듈로 내보내고 load_sr 결과와 비교
sigrok-cli -i demo.sr -O vcd -o demo.vcd
sigrok-cli -i demo.sr -O csv -o demo.csv
# 이 레슨 캡처를 sigrok SPI 디코더로 교차 확인
sigrok-cli -i evt1_spi_100M.sr -P spi:clk=SCLK:mosi=MOSI:miso=MISO:cs=CS_N -A spi
# 실제 보드 (FX2 LA): 24 MS/s 가 상한 -> 이 레슨 기준 10 MHz SPI 의 경계
sigrok-cli -d fx2lafw --config samplerate=24m --channels D0,D1,D2,D3 --samples 2m -o evt1_board.sr
```

비교할 때 주의: sigrok 의 VCD/CSV 출력은 채널 이름·timescale·헤더가 이 레슨 writer 와 다를 수 있다 (확인 못 함). 그래서 비교는 파일 텍스트가 아니라 **로딩 후 transition 리스트**로 한다 — 이 레슨의 `round trip .sr -> VCD/CSV -> load == original` 테스트가 같은 방식이다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py ; echo "exit=$?"
python3 -I -W error -X dev solution.py > /dev/null ; echo "exit=$?"   # 경고 없이 exit=0
```

실제 출력 (Part A/B 일부, Part B 측정값·Part C 리포트는 §5 에 그대로 실었다):

```text
-- Part A: loaders --
PASS  parse_samplerate('100 MHz') == 100000000
PASS  parse_samplerate('1.5 MHz'/'200 kHz'/'1 GHz'/'20000000')
PASS  parse_samplerate('fast') -> ValueError
PASS  .sr: names/ts/length from metadata
PASS  .sr: 10 chunks logic-1-1..10 joined in numeric order
PASS  .sr: SCLK rising edges == 8 x (4+2+12) bytes = 144
PASS  CSV == .sr (same transitions)
PASS  VCD == .sr (same transitions)
PASS  CSV/VCD keep no sample period (ts_ps is None)
-- Part B: format traps --
PASS  .sr chunks stored shuffled (10,2,13,1,...) load the same
PASS  .sr legacy single 'logic-1' member loads the same
PASS  unitsize=2: probe12 = bit 11 of little-endian word
PASS  truncated sample data -> ValueError
PASS  CSV pre-trigger negative time, exact ps
PASS  VCD timescale 10 ns, x initial, 'b1 id' form
-- Part B: sample-rate experiment (10 MHz SCLK, high 46 ns) --
PASS  resample(100M) == brute-force sampling
PASS  resample(50M) == brute-force sampling
PASS  resample(20M) == brute-force sampling
...
PASS  20 MS/s capture of this board run: decode broken (lost SCLK pulse)
PASS  100 MS/s capture decodes JEDEC EF 40 18
...
PASS  sweep: 0 failures at 30/40/50/100 MS/s, > 0 at 20 MS/s
PASS  sweep: boundary (lowest rate with 0 failures from there up) is in 22..30 MS/s
-- Part C: EVT1 U12 JEDEC capture report --
...
PASS  planted defect: MISO stuck high at 100 MS/s -> FAIL with hint
PASS  CSV export (no sample period) -> INVALID
PASS  round trip .sr -> VCD/CSV -> load == original
33/33 passed
exit=0
```

전체 실행 시간 0.27 s (`time python3 -I solution.py`: 0.24 s user). 데이터 재생성 결정성: `python3 -I gen_capture.py` 전후 `shasum -a 256 evt1_*` 비교 → `IDENTICAL`.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `sorted(z.namelist())` 로 청크 연결 | 문자열 정렬: `logic-1-1, logic-1-10, logic-1-2` → 시간 순서가 섞인 캡처 | `sorted((int(m.group(1)), n) …)` |
| unitsize 무시하고 1바이트 = 1샘플 | 9채널 이상에서 샘플 경계가 틀어짐 | `int.from_bytes(data[k*u:(k+1)*u], "little")` |
| 워드를 big-endian 으로 읽음 | probe9~16 이 probe1~8 과 뒤바뀜 | `"little"` + 12채널 테스트 |
| 잘린 마지막 청크를 조용히 버림 | 캡처 끝이 잘려도 모름 | 길이 % unitsize ≠ 0 → ValueError |
| CSV 시간을 `float` 로 | 570 ns → 569999.99… ps, 형식 간 비교가 깨짐 | `round(Fraction(cell) * 10**12)` |
| CSV 캡처에 fs 를 "추정"해 정밀도 주장 | GCD 는 Ts 의 **배수**일 뿐 (연습 1) | `ts_ps=None` → 리포트 INVALID 또는 "(inferred)" 표기 |
| 측정 간격을 그대로 스펙과 비교 | ±1 샘플 불확실성 무시 → 거짓 PASS | 가드밴드 `meas ± Ts` (연습 3) |
| 한 번 디코드 성공 = 샘플링률 충분 | 20 MS/s 는 88% 확률로 "성공" | 오버샘플링·`min_high` 를 먼저 판정 |
| 리샘플을 샘플마다 레벨 계산 | 스윕이 느림 (그래도 맞긴 함) | 에지마다 `ceil` + 짝수 토글 소거 |
| 같은 시각 변화를 하나씩 처리하며 디코드 | 채널 처리 순서에 따라 결과가 달라짐 | 같은 t 를 모두 적용 후 에지 판정 |

## 8. 면접 Q&A

<details>
<summary>Q1. 10 MHz SPI 를 보려면 LA 샘플링률이 얼마나 필요한가요? 나이퀴스트로 20 MS/s 면 되지 않나요?</summary>

나이퀴스트는 대역 제한 신호를 *복원*하는 조건이고 LA 는 복원하지 않는다 — 각 샘플이 임계값보다 위인지 아래인지만 본다. 필요한 조건은 "가장 짧은 펄스 안에 샘플이 반드시 하나 이상 들어간다": `Ts < t_pulse(min)`. 10 MHz, 듀티 46% 면 high 46 ns → 21.7 MS/s 초과, 지터 여유를 넣으면 약 23 MS/s (이 레슨 스윕). 디코드용 실무 기준은 4× (40 MS/s), 타이밍 측정은 10× 이상.
꼬리질문: "20 MS/s 로 잡았는데 디코드가 됐다면?" → 정수비라 위상이 운 좋게 맞았을 뿐이다. 이 모델에서 12% 프레임이 깨진다. 캡처의 `min_high` 가 1 샘플이면 결과를 증거로 쓰지 않는다.

</details>

<details>
<summary>Q2. LA 캡처로 잰 setup time 30 ns 는 얼마나 정확한가요?</summary>

각 에지는 [0, Ts) 늦게 보이므로 두 에지의 간격은 (meas − Ts, meas + Ts). 100 MS/s 면 30 ± 10 ns. 스펙 min 이 25 ns 면 "30 − 10 = 20 < 25" 라 증명 못 함 = UNRESOLVED (연습 3). 해결: 더 높은 샘플링률(타이밍 모드 GS/s 급), 스코프(아날로그 임계값 교차, IEEE-181 방식 — HWLAB-B04), 또는 여러 캡처의 통계 (단, 정수비면 위상이 고정돼 평균해도 편향이 남는다).
꼬리질문: "LA 채널 간 skew 는?" → LA 사양의 channel-to-channel skew 와 프로브 길이 차이도 오차 예산에 더한다. 샘플 양자화만이 전부가 아니다.

</details>

<details>
<summary>Q3. timing mode 와 state mode 의 차이는?</summary>

timing(비동기) 모드는 LA 내부 클럭으로 샘플링 — 이 레슨의 모델. state(동기) 모드는 DUT 의 클럭(예: SCLK)을 LA 샘플 클럭으로 써서 그 에지에서 데이터 채널을 래치한다 — 디코드에는 샘플링률 문제가 없지만 에지 간 시간 정보는 없고, LA 입력의 setup/hold 요구를 만족해야 한다. 고급 벤치 LA 는 둘 다 지원하고, FX2 계열 저가 LA 는 timing 모드만 있다.

</details>

<details>
<summary>Q4. .sr, CSV, VCD 중 무엇으로 저장·공유해야 하나요?</summary>

원본 보존은 `.sr`(또는 벤더 네이티브 형식) — 샘플링률과 모든 샘플이 남아 나중에 다른 디코더·임계 분석을 다시 돌릴 수 있다. CSV 는 리포트·스프레드시트용 export 로 fs 와 캡처 끝을 잃는다. VCD 는 시뮬레이션 파형과 같은 도구(GTKWave)로 비교하거나 EMU-B07 같은 프로토콜 체커에 넣을 때 좋다. 버그 티켓에는 원본 + "fs, 임계값, 채널 매핑, 프로브 위치"를 함께 남긴다.
꼬리질문: "CSV 에서 샘플링률을 복원할 수 있나?" → transition 간격의 GCD 는 Ts 의 정수배라 상한만 준다. 긴 캡처라면 실제 Ts 와 같을 가능성이 높지만 증명은 아니다.

</details>

<details>
<summary>Q5. 부트 플래시 JEDEC ID 가 LA 에서 이상하게 나옵니다. 플래시를 교체하기 전에 무엇을 확인하나요?</summary>

① 캡처 유효성: fs/f_SCLK, 최소 펄스 폭(샘플 수), LA 임계값이 버스 전압에 맞는지 ② 디코더 설정: 모드(CPOL/CPHA), 비트 순서, CS 극성, 채널 매핑 ③ 비트 수가 8의 배수인지 — 아니면 거의 항상 캡처/프로빙 문제 ④ 캡처가 유효하면 DUT: VCC 레일, CS_N/HOLD#/WP# 레벨, MISO 가 FF(구동 안 함+풀업)/00(단락)인지, 다른 디바이스와 버스 경합. 같은 지점을 스코프로 한 번 보면 링잉·느린 에지가 LA 에서 이중 에지로 보이는지도 확인된다 (HWLAB-B05).

</details>

<details>
<summary>Q6. 샘플링률을 올리면 항상 좋은가요?</summary>

메모리 깊이 = fs × 캡처 시간이라 높은 fs 는 캡처 창을 줄인다 (FX2 계열은 USB 대역폭이 상한이라 채널 수를 줄여야 샘플링률이 올라간다). 또 샘플링률이 LA 입력 대역폭·임계 비교기 속도보다 높으면 더 얻는 것이 없다. 트리거(HWLAB-B06)로 관심 구간만 잡고 fs 는 "최소 펄스 > 2~4 샘플, 타이밍 오차 ±Ts 가 스펙 마진보다 충분히 작음"으로 고른다.

</details>

## 9. 추가 연습

### 연습 1 — CSV 에서 샘플 주기 추정

`infer_ts(cap)` = 모든 transition 시각 차이의 GCD. 100 MS/s 캡처의 CSV → 10 ns. 그리고 `capture_report` 에 넣어 PASS 가 나오는지 (라벨에 "inferred" 명시).

<details>
<summary>답 보기</summary>

모든 시각이 `k·Ts` 이므로 차이의 GCD 는 `Ts × gcd(k 차이들)` — Ts 의 정수배다. 에지가 40 ns, 80 ns 떨어진 세 점이면 40 ns 가 나오지만 실제 Ts 는 10/20/40 ns 어느 것일 수도 있다 (테스트 2). 그래서 추정값을 쓴 리포트는 반드시 그렇게 표시한다.

</details>

### 연습 2 — CPOL/CPHA 네 모드 디코더

`decode_spi_mode(cap, cpol, cpha)`. SCLK 를 반전시킨 캡처는 mode 2(CPOL=1, CPHA=0)로 같은 바이트, mode 0 으로는 쓰레기. mode 0 버스를 mode 1(falling 샘플)로 디코드하면?

<details>
<summary>답 보기</summary>

샘플 에지가 rising 인 경우는 `CPOL xor CPHA == 0` (mode 0, 3). 흥미로운 것은 CPHA 착각: mode 0 데이터는 falling 후 7 ns 뒤에 바뀌므로 falling 에서 읽어도 이론상 같은 비트다. 하지만 100 MS/s 에서는 falling 과 데이터 변화가 같은 샘플에 들어가는 위상이 있어 실제 출력은 `MISO correct as mode 0: 10/10, as mode 1: 2/10`. "가끔 맞는" 디코드는 설정 실수의 신호다.

</details>

### 연습 3 — 가드밴드 타이밍 판정 (tCLQV)

`interval_verdict(meas, Ts, spec, kind)` → PASS/FAIL/UNRESOLVED. 모델의 tCLQV(=7 ns)를 스펙 max 10 ns 와 비교: 100 MS/s 와 1 GS/s 에서.

<details>
<summary>답 보기</summary>

진짜 간격 ∈ (meas − Ts, meas + Ts). max 스펙이면 `meas + Ts ≤ spec` 일 때만 PASS, `meas − Ts ≥ spec` 이면 FAIL, 나머지 UNRESOLVED. 실제 출력: `100 MS/s tCLQV meas set (ns) [0.0, 10.0] -> UNRESOLVED`, `1000 MS/s ... [7.0] -> PASS`. 10 ns 급 스펙은 100 MS/s LA 로 검증할 수 없다 — 스코프나 GS/s 타이밍 LA 의 일이다. (W25Q 데이터시트의 실제 tCLQV 값은 이 레슨에서 확인하지 않았다; 7 ns / 10 ns 는 모델 가정.)

</details>

### 연습 4 — CSV → .sr 변환

`csv_to_sr(cap, fs, dst)`: transition 캡처를 샘플로 펼쳐 `.sr` 로 쓴다. 100 MS/s 로 쓰고 다시 읽으면 같아야 하고, 30 MS/s(33333 ps 격자)에서는 ValueError.

<details>
<summary>답 보기</summary>

모든 시각이 Ts 격자 위에 있어야 손실 없이 변환된다. 격자 밖 에지를 반올림하면 조용히 ±Ts/2 를 더하는 것이고, 30 MS/s 처럼 `10**12 % fs != 0` 이면 ps 정수 Ts 자체가 부정확하다. 실무에서 CSV 를 PulseView 로 다시 보려 할 때(`sigrok-cli -I csv …`) 같은 문제가 생긴다 — 원본을 보존하라는 이유.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py ; echo "exit=$?"     # 13/13 passed, exit=0
```

## 10. 핵심 정리

- `.sr` = zip(`version`=2, INI `metadata`, `logic-1-1..N` 청크). 샘플당 `unitsize = ceil(ch/8)` 바이트, little-endian, probeN = 비트 N−1, 청크는 **숫자 순서**로 잇는다.
- CSV/VCD 는 transition 만 — **fs 와 캡처 끝을 잃는다**. 시간은 Fraction → 정수 ps 로.
- 샘플된 에지는 [0, Ts) 늦게, 간격은 ±1 샘플. 정수비 클럭이면 오차가 한쪽에 몰려 "정확해 보인다".
- 펄스는 `t_pulse < Ts` 일 때 샘플 사이로 사라진다. 10 MHz/46 ns high → 경계 ≈ 23 MS/s, 20 MS/s 는 12% 프레임 실패.
- 경험칙: 디코드 ≥ 4×, 타이밍 ≥ 10×. 나이퀴스트 2× 는 LA 기준이 아니다.
- 리포트 순서: 캡처 유효성(oversample, min pulse in samples) → 디코드 → DUT 판정. INVALID 는 "DUT 무죄"가 아니라 "증거 없음".
- 타이밍 판정은 가드밴드 `meas ± Ts`; 스펙이 Ts 와 비슷하면 LA 가 아니라 스코프의 일.

## 참고자료

- [sigrok wiki — File format:Sigrok/v2](https://sigrok.org/wiki/File_format:Sigrok/v2) (zip 멤버, metadata 문법·키)
- [libsigrok `src/output/srzip.c`](https://github.com/sigrokproject/libsigrok/blob/master/src/output/srzip.c) — `logic-1-%u` 청크, CHUNK_SIZE, unitsize 계산
- [sigrok-cli 매뉴얼](https://sigrok.org/wiki/Sigrok-cli) · [fx2lafw](https://sigrok.org/wiki/Fx2lafw) · [SPI 프로토콜 디코더](https://sigrok.org/wiki/Protocol_decoder:Spi)
- IEEE 1364-2005 §18 Value Change Dump (VCD) files · [GTKWave](https://gtkwave.sourceforge.net/)
- Saleae Logic 2 — Data export (CSV) 문서 (헤더·자릿수는 버전별로 확인)
- Winbond W25Q128JV 데이터시트 — JEDEC ID (9Fh) EF 40 18, SPI mode 0/3, DO hi-Z 동작
- Keysight, *Logic Analyzer Fundamentals* / Tektronix *XYZs of Logic Analyzers* — timing vs state 모드, 샘플링 불확실성
- 관련 레슨: HWLAB-B02(임계값·노이즈 마진), HWLAB-B04(IEEE-181 측정), HWLAB-B05(프로브·그라운드), HWLAB-B06(트리거), EMU-B07(VCD 파서·프로토콜 체크)
