---
id: SIV-A07
time: 120분
updated: 2026-11-08
---
# SIV-A07 버스 인터페이스 실패 디버그 (LA 캡처 분석)

## 학습 목표

- 로직 애널라이저(LA) CSV를 받아 **I2C 프로토콜 디코더**를 직접 작성한다: START / repeated START / STOP, 7비트 주소·R/W, ACK/NACK, 바이트 재조립, STOP 직전의 "가짜 비트" 처리.
- **타이밍 위반을 ±1 샘플 오차까지 포함해 3값(VIOLATION / MARGINAL / OK)** 으로 판정한다. "측정값 < 스펙"이라는 소박한 비교가 왜 거짓 경보를 내는지 표로 보여 줄 수 있다.
- 디지털 샘플로 **볼 수 있는 것과 없는 것**을 구분한다. rise time은 직접 안 보이고 '지연'으로만 보이며, 이 지연을 SDA 상승/하강 launch 차이로 **설정값 없이 자가 교정**할 수 있다. LA 임계(0.5)는 디바이스 VIH(0.7)가 아니어서 "LA는 깨끗한데 칩은 실패"가 생긴다는 것을 수치로 설명한다.
- Arbitration lost는 버스 선(wired-AND)만으로는 원리상 안 보인다는 것을 이해하고, **구동 vs 관측** 채널을 추가해 검출한다.
- 합성 캡처 3종(약한 풀업 / 타이밍 위반 / 슬레이브 hang)에서 **원인 + 근거 + 다음 측정**이 적힌 리포트를 자동 생성한다.

## 1. 문제

첫 실리콘·첫 보드에서 가장 흔한 "간헐 실패" 중 하나가 저속 버스(I2C/SPI)의 NACK·행(hang)이고, 검증 엔지니어의 일은 LA 파일 한 개에서 **원인과 다음 측정**을 뽑아내는 것이다 — Rivian·NVIDIA 검증 면접에서 "NACK이 간헐적으로 납니다, LA 캡처로 어떻게 원인을 좁히나요?"로 그대로 나온다.

이 레슨은 그 작업을 도구로 만든다. 전제가 하나 있다. **LA는 1비트 샘플러다.** 파형이 아니라 "임계를 넘었나"의 결과만 저장한다. 그래서 면접과 실무의 핵심 질문은 "무엇을 디코드했나"가 아니라 **"이 표본으로 무엇을 주장할 수 있고 무엇을 주장할 수 없나"** 이다.

```svg
<svg viewBox="0 0 760 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">아날로그 상승 에지 → LA가 저장하는 것 (오픈드레인, Rp·Cb 충전)</text>
  <line x1="60" y1="255" x2="730" y2="255" stroke="currentColor"/>
  <line x1="60" y1="60" x2="60" y2="255" stroke="currentColor"/>
  <text x="14" y="62" fill="currentColor">VDD</text>
  <text x="26" y="258" fill="currentColor">0</text>
  <line x1="60" y1="199" x2="730" y2="199" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="690" y="194" fill="currentColor">0.3</text>
  <line x1="60" y1="157" x2="730" y2="157" stroke="currentColor" stroke-dasharray="6 3"/>
  <text x="640" y="152" fill="currentColor">0.5 = LA 임계</text>
  <line x1="60" y1="115" x2="730" y2="115" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="662" y="110" fill="currentColor">0.7 = VIH</text>
  <path d="M60 255 C 200 150, 300 90, 500 70 L 730 62" fill="none" stroke="currentColor" stroke-width="2"/>
  <line x1="165" y1="255" x2="165" y2="199" stroke="currentColor"/>
  <line x1="235" y1="255" x2="235" y2="157" stroke="currentColor"/>
  <line x1="330" y1="255" x2="330" y2="115" stroke="currentColor"/>
  <text x="168" y="272" fill="currentColor">0.357 τ</text>
  <text x="222" y="290" fill="currentColor">0.693 τ</text>
  <text x="316" y="272" fill="currentColor">1.204 τ</text>
  <rect x="60" y="300" width="175" height="26" class="f-mut" stroke="currentColor"/>
  <text x="147" y="318" text-anchor="middle" fill="currentColor">LA 표본 0 0 0 0 ...</text>
  <rect x="235" y="300" width="495" height="26" class="f-ok" stroke="currentColor"/>
  <text x="482" y="318" text-anchor="middle" fill="currentColor">LA 표본 1 1 1 1 ... (0.7·VDD 넘었는지는 모른다)</text>
  <text x="375" y="240" fill="currentColor" font-size="12">UM10204 tr = 0.3→0.7 = 0.847 τ</text>
  <text x="375" y="224" fill="currentColor" font-size="12">LA는 0.5 통과 한 시각만 안다 → tr 직접 측정 불가</text>
</svg>
```

### Part A — 파서 + 디코더

- `parse_la_csv(text)`: 메타(`# key=value`) + 헤더 + `time_ns,scl,sda[,aux...]` 행. 시각 단조 증가, 값 0/1 검증, 위반은 `ValueError("line N: ...")`.
- `I2CDecoder`: 에지 구동 FSM. START, repeated START, STOP, 주소·R/W, ACK/NACK, 바이트. 표시 항목: `NACK_ADDR`/`NACK_DATA`, `STRETCH`/`STRETCH_TIMEOUT`, setup 위반(`SETUP`), hold 위반(`HOLD`), tLOW/tHIGH/tHD;STA/tSU;STA/tSU;STO/tBUF.
- `check_arbitration(cap, dec, devices)`: 각 마스터의 **구동 채널**과 SDA 관측을 비교해 진 시점(바이트·비트)을 찾는다. ACK·읽기 데이터 슬롯은 비교에서 뺀다.

### Part B — 디지털 샘플의 한계

- `judge(measured, limit, dt)`: ±1 샘플 오차를 포함한 3값 판정.
- 샘플레이트 마진: Fast-mode tSU;DAT 위반을 샘플레이트별로 재측정(실측표).
- SCL·SDA가 같은 샘플에 바뀌는 **동시 에지 규칙**, 짧은 글리치 필터 `deglitch`.
- `estimate_rise_delay(dec)`: 같은 드라이버의 SDA 상승/하강 launch 오프셋 차이로 RC 지연을 추정 → `tr_est`. duty 설정과 RC 지연이 SCL만으로 **구분 불가**함을 테스트로 보인다.

### Part C — 합성 캡처 3종 → 원인 리포트

`gen_captures.py` 가 만든 CSV(`cap_good`, `cap_weak_pullup`, `cap_timing_violation`, `cap_slave_hang`)를 `diagnose()` 로 판정하고 근거와 다음 측정을 적은 리포트를 낸다. SCL을 쥔 행(복구 불가 variant), 9-클럭 복구 성공/실패도 구분한다.

## 2. Problem (English)

> **LA capture root-cause tool for I2C.** Input: a logic-analyzer CSV `time_ns,scl,sda[,aux...]` (1-bit samples; transition-only or dense).
> Implement `parse_la_csv(text) -> Capture`; `decode_i2c(cap, mode)` returning frames (start kind, 7-bit address, R/W, each byte + ACK, end kind) and findings; `check_arbitration(cap, dec, devices)` comparing per-master driven SDA against the observed bus; `judge(measured, limit, dt)`; `estimate_rise_delay(dec)`; `find_bus_hang(cap, dec)`; `diagnose(cap)`.
>
> Findings must include NACK (address vs data), clock stretching and stretching timeout (UM10204 has no stretching limit; timeout is a configurable policy, default SMBus 25 ms), setup violation (SDA change to SCL rising), hold violation (SDA change before SCL falling), tLOW, tHIGH, and arbitration lost. A digital sample cannot be more precise than ±1 sample, so verdicts are `VIOLATION | MARGINAL | OK`.
>
> Constraints: Python 3.9 stdlib. Do not claim rise time as "measured" — a 1-bit sampler only observes a threshold crossing. Examples: `ack=False` on the first byte → `NACK_ADDR`; a low SCL phase of 1.1 µs in Fast-mode → `TLOW VIOLATION`; SDA held low with SCL high for > 200 µs → `SDA_STUCK` plus a 9-clock recovery report.

## 3. 시작 코드

<!-- code: starter.py -->

```bash
python3 -I starter.py; echo exit=$?   # TODO 전: 일부 FAIL, exit 1
```

합성 캡처 생성기. `-I`는 스크립트 디렉터리를 `sys.path`에서 빼므로 `BusSim`·`render_csv`를 복사해 넣었다 (solution.py 와 같은 코드).

<!-- code: gen_captures.py -->

```bash
python3 -I gen_captures.py     # cap_*.csv + captures_truth.json 재생성, 같은 내용 (sha 동일)
```

## 4. 힌트

<details><summary>힌트 1 — 파서: gcd로 sample_ns 복원</summary>

transition-only CSV의 인접 시각 차는 샘플 간격의 배수다. 메타가 없으면 `gcd`로 복원한다. dense CSV도 같은 코드로 동작해야 한다 (같은 값이 반복되면 에지가 없다).

</details>

<details><summary>힌트 2 — START/STOP은 SCL HIGH 구간의 SDA 변화</summary>

SDA가 SCL HIGH에서 바뀌면 START(↓) 또는 STOP(↑)다. 단, STOP/repeated START 직전의 SCL↑은 비트처럼 보인다. 표본을 임시로 넣어 두었다가 START/STOP이 오면 그 표본 1개를 버린다.

</details>

<details><summary>힌트 3 — hold 위반 vs 진짜 START</summary>

프레임 안에서 SCL HIGH 중 SDA가 바뀌었는데 다음 SCL↓이 매우 가깝다면(0.5·tHD;STA 미만) 그것은 진짜 START가 아니라 hold 위반이다. SDA 변화는 기록하되 프레임을 끊지 않는다.

</details>

<details><summary>힌트 4 — ±1 샘플 판정</summary>

에지는 실제보다 [0, dt) 늦게 잡힌다. 두 에지의 차 오차는 (−dt, +dt)다. 그래서 `measured + dt <= limit` 이면 확실한 위반, `measured − dt >= limit` 이면 확실한 합격, 나머지는 MARGINAL.

</details>

<details><summary>힌트 5 — arbitration: "구동=1, 관측=0"</summary>

SDA는 wired-AND라 버스 CSV에는 이긴 쪽 파형만 남는다. 각 마스터의 구동 채널이 필요하다. 프레임마다 START 때 참가자를 정하고(그 시점 구동=0), 자기 슬롯(주소·쓰기 데이터 8비트)에서 구동=1인데 관측=0이면 진 것이다. ACK/읽기 슬롯은 정상적으로 구동=1, 관측=0이므로 제외한다.

</details>

<details><summary>힌트 6 — 자가 교정 지연</summary>

같은 드라이버가 같은 오프셋으로 SDA를 launch한다면, `median(상승 launch) − median(하강 launch)` 가 곧 RC 지연이다. 마스터 설정값이 필요 없다. 슬레이브 launch는 오프셋이 다르니 owner별로 나눈다.

</details>

## 5. 풀이

### 스펙 값과 기준점 (Part A·B 공통)

UM10204 F/S-mode 표에서 이 레슨이 쓰는 값 (단위 ns, LIMITS 딕셔너리):

| 항목 | Standard | Fast | 이 레슨의 쓰임 |
|---|---:|---:|---|
| tLOW (min) | 4700 | 1300 | SCL low |
| tHIGH (min) | 4000 | 600 | SCL high |
| tSU;DAT (min) | 250 | 100 | SETUP 판정 |
| tHD;DAT (min) | 0 | 0 | HOLD 판정 (<0 위반) |
| tHD;STA (min) | 4000 | 600 | START hold |
| tSU;STA (min) | 4700 | 600 | repeated START setup |
| tSU;STO (min) | 4000 | 600 | STOP setup |
| tBUF (min) | 4700 | 1300 | STOP→START |
| tr (max) | 1000 | 300 | 추정 tr 비교 |
| tVD;DAT (max) | 3450 | 900 | 슬레이브 launch 참고 |

UM10204는 데이터 hold 최솟값을 0으로 두면서, 디바이스가 SCL 하강의 미정의 구간을 건너도록 내부적으로 최소 300 ns hold를 제공하라고 규정한다. 측정 기준점도 표에 있다: tLOW는 SCL 0.3 VDD 통과 간, tHIGH는 0.7 VDD 통과 간, tr은 0.3→0.7. **LA의 임계는 이 기준점이 아니다.** 이것이 Part B의 핵심이다.

stretching 상한은 I2C에 없다. 타임아웃은 **정책값**이다. 기본값 25 ms는 SMBus tTIMEOUT(25~35 ms)에서 빌렸고, `stretch_timeout_ns` 로 바꾼다.

### Part A — 디코더 설계

입력은 시각 순서의 에지 목록이다. FSM의 상태는 작다: `in_frame`, 현재 바이트의 표본 `bits`, 마지막 SCL 상승/하강 시각, 마지막 SDA 변화 시각.

| 이벤트 | 처리 |
|---|---|
| SCL↑ | tLOW/stretch 판정. 프레임 안이면 setup 판정(이번 SCL low 안에 SDA가 바뀌었을 때만) 후 SDA 표본 |
| SCL↓ | tHIGH 판정, START 뒤 첫 하강이면 tHD;STA 판정. 표본 9개면 바이트 완성 |
| SDA 변화 & SCL low | launch 기록(owner, polarity, SCL↓ 후 오프셋) |
| SDA 변화 & SCL high | 프레임 안 + 표본함 + 다음 SCL↓ < 0.5·tHD;STA → HOLD. 그 외 ↓ START, ↑ STOP |

STOP이 항상 까다로운 이유는 STOP 직전의 SCL↑이 **다음 바이트의 첫 비트처럼** 보이기 때문이다. `_scl_rise` 는 일단 표본을 넣고(`bits`, `dec.slots`), STOP/rSTART가 오면 `_discard_tentative` 가 표본 1개는 조용히 버린다. 2개 이상이면 진짜로 잘린 프레임(`ABORTED_FRAME`)이다.

owner 추적은 단순하다: 주소 바이트 8비트와 쓰기 데이터 8비트는 master, 그 ACK는 slave, 읽기 데이터 8비트는 slave, 그 ACK는 master. 이 owner가 나중에 setup 위반의 책임 구분(슬레이브 launch가 늦었나)과 arbitration 슬롯 판정에 쓰인다.

**Arbitration lost.** 버스 선은 wired-AND이므로 진 마스터의 비트는 선에 흔적이 없다. 파형은 이긴 쪽의 정상 프레임이다. 볼 수 있으려면 구동 쪽을 채널로 빼야 한다(GPIO 미러, 직렬 저항 양단, FPGA 내부 신호). `check_arbitration` 은 프레임마다 START 시점 구동이 0인 마스터를 참가자로 정하고, master 슬롯에서 `구동=1 & 관측=0` 인 첫 슬롯을 진 시점으로 기록한다. 모든 참가자가 지는 경우는 스펙 정상 동작으로 설명되지 않으므로(진 쪽이 계속 구동하는 버그) `CONTENTION`으로 구분한다. 테스트 `A.arb.buggy_loser_contention` 이 이 경우다.

**이 디코더의 범위.** 7비트 주소만 처리한다. 10비트 주소(첫 바이트 `11110xx`)는 프레임을 해석하지 못하고, 랜덤 테스트도 주소 0x08~0x77 안에서만 돌린다. 실제 버스에 10비트 장치나 예약 주소가 있으면 먼저 이 부분을 확장해야 한다.

### Part B — 디지털 샘플로 주장할 수 있는 것

#### 1) ±1 샘플 판정

```svg
<svg viewBox="0 0 760 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">SDA 변화 → SCL 상승 간격의 측정 오차 (샘플 간격 dt)</text>
  <line x1="40" y1="60" x2="720" y2="60" stroke="currentColor" stroke-dasharray="2 4"/>
  <g stroke="currentColor">
    <line x1="80" y1="52" x2="80" y2="68"/><line x1="200" y1="52" x2="200" y2="68"/>
    <line x1="320" y1="52" x2="320" y2="68"/><line x1="440" y1="52" x2="440" y2="68"/>
    <line x1="560" y1="52" x2="560" y2="68"/><line x1="680" y1="52" x2="680" y2="68"/>
  </g>
  <text x="648" y="44" fill="currentColor" font-size="12">샘플 격자 dt</text>
  <path d="M120 150 L120 110 L700 110" fill="none" stroke="currentColor" stroke-width="2"/>
  <text x="44" y="116" fill="currentColor">SDA</text>
  <path d="M40 200 L500 200 L500 160 L720 160" fill="none" stroke="currentColor" stroke-width="2"/>
  <text x="44" y="194" fill="currentColor">SCL</text>
  <rect x="120" y="212" width="380" height="20" class="f-acc" stroke="currentColor"/>
  <text x="310" y="227" text-anchor="middle" fill="currentColor">진짜 setup</text>
  <line x1="200" y1="86" x2="200" y2="110" stroke="currentColor"/>
  <line x1="560" y1="86" x2="560" y2="160" stroke="currentColor"/>
  <text x="208" y="100" fill="currentColor" font-size="12">관측 SDA 에지 (+0..dt)</text>
  <text x="440" y="100" fill="currentColor" font-size="12">관측 SCL 에지 (+0..dt)</text>
  <text x="40" y="250" fill="currentColor" font-size="12">측정값 − 참값 ∈ (−dt, +dt) → 측정+dt ≤ limit: VIOLATION / 측정−dt ≥ limit: OK / 그 외 MARGINAL</text>
</svg>
```

샘플 격자 위 에지는 실제 시각보다 `[0, dt)` 늦다. 두 에지의 차는 그 오차가 서로 빠지므로 `(−dt, +dt)`. 이 범위를 무시하고 측정값을 스펙에 바로 대면 합격 비트를 불합격으로, 또는 그 반대로 찍는다.

`demo_sample_margin.py` 실측 (Fast-mode, 데이터 비트마다 setup을 주입: bad=60 ns, edge=130 ns, good=1300 ns, 샘플 phase 전수):

| dt ns | MS/s | 디코드 OK | bad VIOL % | bad MARG % | bad OK % | edge VIOL % | edge MARG % | naive edge 오탐 % | good VIOL |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 5 | 200.00 | 5/5 | 100.0 |   0.0 |   0.0 |   0.0 |   0.0 |   0.0 | 0/240 |
| 10 | 100.00 | 10/10 | 100.0 |   0.0 |   0.0 |   0.0 |   0.0 |   0.0 | 0/480 |
| 20 | 50.00 | 10/10 | 100.0 |   0.0 |   0.0 |   0.0 |   0.0 |   0.0 | 0/480 |
| 25 | 40.00 | 9/9 | 100.0 |   0.0 |   0.0 |   0.0 |   0.0 |   0.0 | 0/432 |
| 40 | 25.00 | 8/8 |  50.0 |  50.0 |   0.0 |   0.0 |  75.0 |   0.0 | 0/384 |
| 50 | 20.00 | 9/9 |  77.8 |  22.2 |   0.0 |   0.0 |  44.4 |   0.0 | 0/432 |
| 80 | 12.50 | 8/8 |  25.0 |  75.0 |   0.0 |   0.0 | 100.0 |  37.5 | 0/384 |
| 100 | 10.00 | 9/9 |  44.4 |  55.6 |   0.0 |   0.0 |  66.7 |   0.0 | 0/432 |
| 200 | 5.00 | 8/8 |   0.0 | 100.0 |   0.0 |   0.0 | 100.0 |  37.5 | 0/384 |
| 400 | 2.50 | 8/8 |   0.0 | 100.0 |   0.0 |   0.0 | 100.0 |  75.0 | 0/384 |

읽는 법:

- **bad OK % = 0 전 구간, edge VIOL % = 0, good VIOL = 0.** 3값 판정은 샘플레이트와 상관없이 틀린 단정을 하지 않는다 (테스트 `B.margin.*` 가 이 건전성을 CHECK). 대신 샘플이 거칠수록 MARGINAL로 후퇴한다. MARGINAL은 "이 샘플레이트로는 말할 수 없다"는 정직한 답이다.
- **naive 비교** (`측정 < 100` → 위반)는 80 ns·200 ns에서 합격 비트(130 ns)의 37.5%, 400 ns에서 75%를 위반으로 찍는다.
- `bad VIOL`이 dt에 단조가 아니다 (dt 25는 100%, 40은 50%, 100은 44%). 측정값이 dt의 배수이고 참값 60 ns의 격자 정렬이 dt마다 달라서다. **보장**은 `|limit − 참값| ≥ 2·dt`일 때만이다: limit 100에서 참값 60을 모든 phase에서 VIOL로 판정하려면 dt ≤ 20 ns, 즉 **50 MS/s**.

규칙으로 정리하면: **판정 해상도가 limit와 참값의 거리의 절반보다 촘촘해야 한다**. Fast-mode tSU;DAT 100 ns 근처를 단정하려면 수십 MS/s가 필요하고, 프로토콜 디코드만 하려면(아래) 훨씬 낮아도 된다.

#### 2) 동시 에지 규칙

SCL과 SDA가 한 샘플에 같이 바뀌면 순서 정보가 없다. 처리 규칙이 디코드 성패를 가른다.

| 경우 | 규칙 (spec) | 이유 |
|---|---|---|
| SCL↑ + SDA 변화 | SDA 먼저, 그다음 SCL↑ | 정상 버스면 SDA는 SCL↑ 전에 안정 → 표본은 새 값 |
| SCL↓ + SDA 변화 | SCL↓ 먼저, 그다음 SDA | 정상 launch는 SCL↓ 뒤 → SDA 변화를 HIGH로 보면 가짜 START/STOP |

실측 (SDA launch 300 ns, tHIGH 900 ns):

| dt ns | MS/s | coincident 평균 | spec 규칙 디코드 OK | naive 규칙 디코드 OK |
|---:|---:|---:|---:|---:|
| 100 | 10.00 | 0.0 | 17/17 | 17/17 |
| 200 | 5.00 | 0.0 | 17/17 | 17/17 |
| 300 | 3.33 | 0.0 | 17/17 | 17/17 |
| 400 | 2.50 | 4.0 | 16/16 | 0/16 |
| 500 | 2.00 | 6.6 | 17/17 | 10/17 |
| 700 | 1.43 | 9.1 | 17/17 | 0/17 |
| 1000 | 1.00 | 11.8 | 11/17 | 0/17 |
| 1500 | 0.67 | 12.4 | 0/17 | 0/17 |

naive(항상 SDA 먼저)는 샘플 간격이 launch 오프셋을 넘는 순간 가짜 START/STOP으로 무너진다(400 ns에서 0/16). spec 규칙은 tHIGH 900 ns 수준까지 버티고, dt=1000 ns에서 SCL 펄스가 일부 phase에서 사라지기 시작해 깨진다 (`B.floor.dt1000_breaks_some_phase`).

정리: **디코드 하한은 대략 min(tHIGH, tHD;STA)이 샘플 1~2개** 이상인 수준이다. 이 합성 모델에서 Fast-mode 디코드만이라면 2~5 MS/s로도 표가 깨지지 않지만, 실무에서는 잡음·임계 마진 때문에 훨씬 여유를 둔다. 그러나 setup을 단정하려면 위 표처럼 50 MS/s가 필요하다. **판정 목적이 샘플레이트를 정한다.**

#### 3) 글리치

SCL HIGH 구간의 짧은 SDA 글리치는 거짓 START+STOP을 만든다. Fast-mode 입력 필터는 50 ns 이하 스파이크를 억제한다(tSP). `deglitch(cap, 50)` 는 라인별로 폭 < 50 ns 펄스를 제거한다. 테스트: 필터 전 프레임이 깨지고 필터 후 복원된다. 다만 필터는 후처리다. 40 ns 글리치가 샘플에 잡힐지는 phase에 달렸다 (`B.limit.short_glitch_phase_dependent`: 샘플 100 ns, 글리치 40 ns → 일부 phase에서만 보임). **샘플 간격보다 짧은 글리치의 부재는 증명할 수 없다.**

#### 4) rise time은 안 보인다, 지연만 보인다 — 합성 모델의 약속

Part C의 약한 풀업을 이해하려면 합성 모델이 정확히 무엇을 하는지 알아야 한다. `BusSim` + `render_csv` 의 동작:

- 각 디바이스는 시각표대로 선을 끌어내리거나(0) 놓는다(1). 라인 값은 wired-AND.
- **마스터 타이밍은 개방 루프**다. SCL 해제 시각은 스케줄대로이고 SCL을 되읽지 않는다. (실제 컨트롤러는 HIGH 카운터를 SCL HIGH 감지 뒤부터 시작하는 경우가 많다 — 그러면 tHIGH는 유지되고 주기가 늘어난다. 추가 연습 3이 이 경우다.) SDA launch는 SCL↓ 뒤 고정 오프셋.
- 상승: 1차 RC `v(t) = 1−(1−v0)e^(−t/τ_r)`. 하강: 1차 `v0·e^(−t/τ_f)`, τ_f는 작게(드라이버 Ron·C).
- LA: 임계 1개(`vth=0.5`, 히스테리시스 없음). 샘플 k의 시각은 `k·dt+phase`, 샘플 값은 그 순간의 논리값. 임계에 못 닿는 짧은 펄스는 사라진다.
- 비모델: 전압 잡음, 공통 모드, 드라이버 Ron 분산, multi-master 클럭 동기 (두 마스터는 SCL을 공유한다고 단순화).

이 모델에서 임계 통과 지연은 상승 `τ_r·ln(1/(1−vth))`(vth=0.5면 0.693τ_r), 하강 `τ_f·ln(1/vth)`. 그러면 LA에 나타나는 효과는:

- SCL HIGH 폭: 설정값 − (상승 지연 − 하강 지연) → **줄어듦**
- SCL LOW 폭: 설정값 + (상승 지연 − 하강 지연) → **늘어남**
- SDA 상승 에지는 SDA 하강 에지보다 그만큼 늦게 보인다.

SCL만 보면 "duty가 낮게 설정됐다"와 구분이 안 된다. 테스트 `B.limit.duty_vs_pullup_scl_same` 이 SCL 파형을 샘플까지 동일하게 만들어 CHECK한다. **그러나 SDA는 구분해 준다**: 같은 드라이버가 같은 오프셋으로 launch하므로 `median(상승 launch) − median(하강 launch)` 는 RC 지연 그 자체다. 마스터 설정값이 필요 없다(자가 교정). 측정 `estimate_rise_delay` (τ_f=8 ns, dt=20 ns):

| τ ns | 지연 참값 | 지연 추정 | tr 참값 | tr 추정 | tr 오차 |
|---:|---:|---:|---:|---:|---:|
| 80 | 50 | 40 | 68 | 49 | -19 |
| 150 | 98 | 100 | 127 | 122 | -5 |
| 250 | 168 | 160 | 212 | 196 | -16 |
| 350 | 237 | 240 | 297 | 293 | -3 |
| 500 | 341 | 340 | 424 | 416 | -8 |
| 800 | 549 | 540 | 678 | 660 | -18 |

오차 허용은 `2·dt`(지연) 근처다: 두 median 각각에 양자화 오차 < dt. `tr_est = delay/ln2 × ln(7/3)` 는 **1차 RC + 알려진 vth를 가정한 추정**이지 측정이 아니다. 전제가 깨지면(이중 RC, 슬레이브 launch 혼재) 값이 틀린다. 그래서 리포트는 "스코프로 재확인"을 다음 측정으로 적는다.

#### 5) LA 임계 ≠ 디바이스 VIH — "LA는 깨끗한데 칩은 실패"

LA 임계 0.5는 UM10204 측정 기준점도, 디바이스 입력 레벨(VIH = 0.7 VDD)도 아니다. tHIGH를 LA로 재면 **과대평가**한다(LA 임계 통과는 VIH보다 일찍 온다). 실측 + 계산 (cfg tLOW=1600, tHIGH=900):

| τ ns | LA tHIGH (측정) | LA tLOW (측정) | spec tHIGH 0.7→0.7 (계산) | spec tLOW 0.3→0.3 (계산) | tHIGH 판정 LA / spec (0.6 µs) |
|---:|---:|---:|---:|---:|:---:|
| 100 | 840 | 1660 | 782 | 1626 | OK / OK |
| 250 | 740 | 1760 | 602 | 1680 | OK / OK |
| 300 | 700 | 1800 | 542 | 1697 | OK / FAIL |
| 500 | 560 | 1940 | 301 | 1769 | FAIL / FAIL |

τ=300 ns 줄을 보라. LA는 tHIGH 700 ns로 합격, 스펙 기준점 tHIGH는 542 ns로 불합격이다. 극단적인 경우 SCL 펄스가 0.5는 넘지만 0.7은 못 넘는다: LA는 깨끗한 클럭을 보여 주고 디바이스는 그 클럭을 아예 못 받는다 (`B.limit.la_clean_but_vih_missed`). LA 임계를 VIH 근처로 올리면 도움이 되지만, 정답은 스코프다.

#### 6) LA로 원리상 못 보는 것

| 현상 | LA로 왜 안 보이나 | 대신 무엇 |
|---|---|---|
| rise time·ringing·VOL 너무 높음 | 임계 통과만 저장 | 스코프 (HWLAB-B03, B05) |
| 누가 SDA/SCL을 쥐었나 | wired-AND 결과만 저장 | 직렬저항 양단 전압, 디바이스 분리 |
| Clock stretching vs 마스터 pause | 선 위 파형 동일 (`B.limit.stretch_owner_unobservable`) | 구동 채널 또는 슬레이브 분리 |
| Arbitration lost | 진 쪽 흔적 없음 | 구동 채널 (Part A) |
| 샘플 간격 미만 글리치 | 표본 사이에 숨음 | 스코프, 고속 샘플링 |
| 디바이스가 읽은 값 | LA 임계로 읽은 값일 뿐 | 디바이스 레지스터 덤프 |

### Part C — 세 캡처 진단

`diagnose()` 순서: hang → 약한 풀업 → 타이밍 위반 → NACK → CLEAN. hang이 맨 앞인 이유는 hang이 있으면 뒤의 프레임 해석이 무의미하기 때문이다. 약한 풀업이 타이밍보다 앞인 이유는 RC 지연이 tHIGH 위반을 **결과로** 만들기 때문이다 (원인을 먼저 짚는다).

```svg
<svg viewBox="0 0 760 240" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">diagnose() 규칙 순서</text>
  <rect x="20" y="40" width="130" height="60" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="85" y="62" text-anchor="middle" fill="currentColor">상태 유지 &gt; 200 µs?</text>
  <text x="85" y="84" text-anchor="middle" fill="currentColor" font-size="12">SDA_STUCK / SCL_STUCK</text>
  <rect x="190" y="40" width="130" height="60" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="255" y="62" text-anchor="middle" fill="currentColor">SDA 자기 기준</text>
  <text x="255" y="84" text-anchor="middle" fill="currentColor" font-size="12">tr_est &gt; tr_max?</text>
  <rect x="360" y="40" width="130" height="60" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="425" y="62" text-anchor="middle" fill="currentColor">타이밍 VIOLATION?</text>
  <text x="425" y="84" text-anchor="middle" fill="currentColor" font-size="12">owner · 극성 집계</text>
  <rect x="530" y="40" width="100" height="60" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="580" y="66" text-anchor="middle" fill="currentColor">NACK?</text>
  <rect x="660" y="40" width="80" height="60" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="700" y="66" text-anchor="middle" fill="currentColor">CLEAN</text>
  <g stroke="currentColor"><line x1="150" y1="70" x2="190" y2="70"/><line x1="320" y1="70" x2="360" y2="70"/><line x1="490" y1="70" x2="530" y2="70"/><line x1="630" y1="70" x2="660" y2="70"/></g>
  <text x="85" y="130" text-anchor="middle" fill="currentColor">SLAVE_HANG_SDA_LOW</text>
  <text x="85" y="146" text-anchor="middle" fill="currentColor" font-size="12">또는 SCL_HELD_LOW</text>
  <text x="255" y="130" text-anchor="middle" fill="currentColor">WEAK_PULLUP</text>
  <text x="425" y="130" text-anchor="middle" fill="currentColor">TIMING_VIOLATION</text>
  <text x="580" y="130" text-anchor="middle" fill="currentColor">NACK_ONLY</text>
  <text x="380" y="180" text-anchor="middle" fill="currentColor">hang이 맨 앞: 프레임 해석 이전의 사실 / 풀업이 타이밍 앞: tHIGH 위반의 원인 쪽</text>
  <text x="380" y="205" text-anchor="middle" fill="currentColor" font-size="12">각 판정은 근거(숫자)와 다음 측정을 함께 낸다</text>
</svg>
```

실제 출력 (`python3 -I solution.py --diagnose cap_*.csv`, 합성 데이터):

```text
=== cap_good.csv  cause=CLEAN ===
  근거: 위반 없음

=== cap_weak_pullup.csv  cause=WEAK_PULLUP ===
  근거: SDA 상승 launch가 하강보다 520 ns 늦음 (master 드라이버 자기 기준, 표본 96)
  근거: 1차 RC 가정 추정 tr(0.3→0.7) ≈ 636 ns > 300 ns
  근거: SCL HIGH 중앙값 560 ns, LOW 중앙값 1940 ns (HIGH<->LOW 비대칭)
  근거: 주소 NACK 4/24 프레임 — 간헐
  다음: 스코프로 SDA/SCL 0.3→0.7 VDD tr 직접 측정 (LA 값은 추정치)
  다음: Rp 값과 Cb 확인 (HWLAB-B03 윈도우), 약하면 Rp 낮춤

=== cap_timing_violation.csv  cause=TIMING_VIOLATION ===
  근거: SETUP 100건, 최악 측정 40 ns (limit 100 ns, owner=slave)
  근거: TLOW 560건, 최악 측정 1240 ns (limit 1300 ns, owner=None)
  근거: slave launch 중앙값 1160 ns (tVD;DAT max 900 ns)
  다음: SCL 분주/duty 재설정 (Fast-mode tLOW 1.3 µs 때문에 400 kHz 50% duty는 불가)
  다음: 슬레이브 데이터 launch 지연을 데이터시트 tVD;DAT와 비교

=== cap_slave_hang.csv  cause=SLAVE_HANG_SDA_LOW ===
  근거: SCL=1, SDA=0 가 2.0 ms 지속 (t=0.414 ms부터)
  근거: 직전 프레임: addr=0x48 rw=1, 바이트 #1 비트 #3 표본 직후 정지
  근거: 복구: SCL 하강 5회 뒤 SDA 해제=True (9회 이내=True), 복구 뒤 정상 ACK=True
  다음: SDA 소유자는 LA로 구분 불가 → 직렬저항 양단 전압 측정 또는 0x48 분리로 확인
  다음: 슬레이브 FSM의 중단 경로 확인(마스터 리셋·전력 이벤트 중 전송)
```

주입한 참값과 대조 (`captures_truth.json`, 테스트 `C.*`가 CHECK):

| 캡처 | 주입 | 리포트 | 허용 오차의 이유 |
|---|---|---|---|
| weak | 주소 NACK 4회 | NACK 4/24 | 정수 일치 |
| weak | SDA τ 프레임별 ±10% (중앙값 ≈ 781 ns), 지연 참값 ≈ 534 ns | 520 ns | `2·dt + 참값의 8%` (τ 분포의 표본 중앙값 + 양자화) |
| timing | tLOW 약 1238 ns | 1240 ns | `2·dt` |
| timing | 슬레이브 setup 설정 30 ns (양 극성) | 40 ns(최악) | 양자화 |
| hang | 고착 2 ms, 복구 하강 5회, 위치 byte 1 bit 3 | 일치 | 시각 `2·dt`, 횟수 정확 |

**캡처별 근본 원인 추론 (사람이 쓰는 방식).**

1. **약한 풀업**(시나리오: BMS 보드 rev A, 온도 센서 0x48, Rp 2.2 kΩ 자리에 10 kΩ 오장착 가정). 증상은 간헐 NACK. 근거 사슬: (1) 디코드는 깨끗하다 — LA는 주소를 0x48로 정확히 읽었다. 슬레이브가 NACK했는데 주소가 맞다 → LA의 임계보다 디바이스 임계 쪽 문제. (2) SDA 상승 launch가 하강보다 520 ns 늦다 → 드라이버 자기 기준이므로 설정값 문제 아님. (3) tr 추정 636 ns > 300 ns. (4) SCL HIGH 560 ns < 600 ns, LOW가 늘어남 → 전부 같은 RC 방향. 모델에서 NACK은 프레임별 τ가 커서 슬레이브 표본 시점(SCL이 VIH를 지나는 순간)에 SDA가 VIH에 못 미친 프레임이다 — LA 임계는 넘었으므로 LA 값은 1이지만 디바이스는 0을 읽었다. 그래서 **간헐**이다: 같은 코드, 같은 주소인데 프레임 RC가 조금씩 다르다.

2. **타이밍 위반**(시나리오: 같은 센서, 펌웨어가 Fast-mode 분주를 잘못 설정 + 슬레이브 데이터 launch 지연). 근거: `TLOW 560건` 전부, tLOW 약 1.24 µs. 흔한 실수가 "400 kHz 50% duty"다. 주기 2.5 µs의 절반은 1.25 µs라 tLOW 1.3 µs를 못 지킨다 (그래서 Fast-mode 컨트롤러에는 2:1 duty 같은 설정이 있다). `SETUP 100건`은 전부 **슬레이브 구동 비트**이고 **양 극성**이다 → RC 비대칭이 아니라 launch 자체가 늦다. 슬레이브 launch 중앙값 1.16 µs는 tVD;DAT max 0.9 µs(Fast-mode)를 넘는다. SDA 자기 기준 지연은 작아서(강한 풀업) 약한 풀업이 아니라는 것도 데이터로 배제된다.

3. **슬레이브 hang**(시나리오: 마스터가 읽기 도중 워치독 리셋, 슬레이브가 데이터 0을 내보내는 중에 멈춤). SCL=1, SDA=0이 2 ms. LA가 말할 수 있는 것: 어떤 디바이스가 끌고 있다, 직전 프레임은 0x48 읽기, 바이트 #1의 비트 #3 표본 직후 멈췄다. LA가 말할 수 없는 것: **누가** 끌고 있는지 — 추정 근거가 "직전 주소"이므로 리포트는 분리 측정을 다음 단계로 적는다. 복구는 UM10204의 Bus clear 절차다: SCL을 최대 9번 토글하면 슬레이브는 남은 비트를 내보내고 ACK 슬롯에서 SDA를 놓는다. 이 캡처에서는 5번째 SCL 하강 뒤 SDA가 올라왔고, STOP 뒤 정상 ACK를 받았다. 9번으로 안 풀리면(`C.hang_unrecoverable.*`) 슬레이브 리셋 또는 전원 사이클이다. **SCL을 슬레이브가 쥔 경우(SCL_HELD_LOW)는 마스터가 클럭을 낼 수 없어 9-클럭이 무효다** — 리포트가 그렇게 말한다 (`C.scl_held.no_9clock_advice`).

리포트 형식에서 지킨 것: 원인 단정은 데이터가 뒷받침하는 만큼만 ("추정", "구분 불가"를 쓴다), 숫자는 전부 캡처에서 나오고, 다음 측정이 항상 있다.

### 실측 요약 (Mac, Python 3.9.6, `python3 -I`)

```text
python3 -I starter.py    → 4/17 passed, exit=1
python3 -I solution.py   → 122/122 passed, exit=0
python3 -I practice.py   → 19/19 passed, exit=0
python3 -I -W error -X dev solution.py → 경고 없음
python3 -I gen_captures.py 두 번 → cap_*.csv, captures_truth.json sha 동일
cap_good.csv 6631, cap_slave_hang.csv 5970, cap_timing_violation.csv 16233, cap_weak_pullup.csv 16503 bytes
```

`장비가 있을 때` (이 맥에 sigrok/LA 없음, 위 출력은 전부 합성 데이터): 실제 캡처는 LA 소프트웨어에서 CSV로 내보내 같은 파서에 넣는다. 도구마다 시각 단위와 transition-only 여부가 다르므로 meta에 `sample_ns`를 명시하거나 gcd 복원에 맡긴다. HWLAB-M06(sigrok 디코더 플러그인)은 다른 프로토콜 디코드, 이 레슨은 원인 분석.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py; echo exit=$?            # 122/122 passed, exit 0
python3 -I solution.py --diagnose cap_slave_hang.csv
python3 -I demo_sample_margin.py                # §5 표 1~4 재현
```

## 7. 자주 하는 실수

1. **STOP 직전 SCL↑을 진짜 비트로 처리** → STOP/rSTART 때마다 다음 바이트가 1비트씩 밀린 것처럼 보인다.
   → 왜: STOP 전의 SCL↑은 SDA로 표본되지만 다음 바이트가 아니다. 표본을 임시로 넣고 버린다.
   ```python
   elif len(self.bits) == 1:      # STOP 직전 SCL↑ 표본 1개는 비트가 아니다
       self.dec.slots.pop()
   ```

2. **동시 에지를 입력 순서대로 처리(SDA 먼저)** → §5 표대로 샘플이 거칠어지는 순간 가짜 START/STOP. SCL↓+SDA는 SCL 먼저.

3. **측정값을 스펙과 바로 비교** → 샘플링 오차가 판정을 뒤집는다. 3값으로 바꾼다.
   ```python
   if measured + dt <= limit: return "VIOLATION"
   if measured - dt >= limit: return "OK"
   return "MARGINAL"
   ```

4. **Arbitration 비교에 ACK·읽기 슬롯 포함** → 슬레이브 ACK(구동=1, 관측=0)가 매번 "진 것"으로 찍힌다. master 슬롯만 비교한다. 비참가자도 빼야 한다: START 시점 구동 0인 마스터만.

5. **Stretch를 tLOW 위반으로 집계** → 정상 stretching 전부가 tLOW 위반? 아니다. 반대로 긴 low는 tLOW 하한을 절대 어기지 않는다. 판단은 "이 low가 평소의 몇 배냐"로 STRETCH 정보로 분리한다.

6. **rise time을 LA 측정값으로 보고** → LA는 1비트 샘플러다. `tr_est`는 RC 가정 추정치이며 리포트에 "스코프로 재측정"이 붙어야 한다.

7. **hang 뒤 프레임을 정상 트래픽으로 해석** → 복구 클럭은 디코더에 쓰레기 바이트로 보인다. 복구 구간은 프레임으로 믿지 않는다 (`find_bus_hang`이 원시 에지로 따로 본다).

## 8. 면접 Q&A

<details><summary>Q1. 50 MS/s LA로 재니 tSU;DAT 80 ns입니다. Fast-mode 위반인가요?</summary>

샘플 20 ns면 에지 둘의 차 오차는 (−20, +20) ns. 80 + 20 = 100 ≤ 100이니 참값은 100 미만 → **VIOLATION 단정 가능**. 90 ns로 찍혔다면 참값 범위 70..110 → MARGINAL. 임계/기준점도 고려해야 한다: tSU;DAT는 스펙에서 SDA 스윙의 기준점과 SCL VIL 쪽 기준으로 정의되며 LA 임계 통과 간격과 같지 않다.

**꼬리: 100 MS/s로 올리면 해결되나요?** 양자화 오차는 줄지만 임계 기준점 차이(1차 RC면 상승 쪽 0.5→VIH 차이)는 그대로다. "샘플레이트"와 "기준점"은 다른 오차원이다.

**꼬리: MARGINAL이 많으면 뭘 하나요?** 샘플레이트를 올리거나, LA 임계를 옮겨 재측정, 그래도 애매하면 스코프.

</details>

<details><summary>Q2. LA로 I2C 풀업이 약한지 어떻게 판단합니까?</summary>

LA는 전압을 저장하지 않아 rise time을 직접 볼 수 없다. 보이는 것은 임계 통과 지연이다. 같은 드라이버의 SDA 상승 launch와 하강 launch의 차 (`median(rise) − median(fall)`)가 곧 RC 지연이고, 마스터 설정값이 없어도 된다. 1차 RC를 가정하면 tr = 0.847τ, 지연 = 0.693τ (임계 0.5) → tr ≈ 1.22 × 지연.

**꼬리: SCL HIGH가 짧고 LOW가 길면 풀업 약한 거 아닌가요?** 그건 duty 설정과 구분이 안 된다. 테스트로 SCL 파형을 샘플까지 동일하게 만든다. SDA 자기 기준을 쓰거나 설정값을 알아야 한다.

**꼬리: 그래서 약하다고 보고합니까?** "추정됨. 스코프로 0.3→0.7을 재겠다"가 맞는 답이다. LA는 근거, 스코프가 증거.

</details>

<details><summary>Q3. LA 디코드는 전부 깨끗한데 NACK이 납니다. 뭘 의심합니까?</summary>

LA 임계와 디바이스 임계가 다르다는 것이 첫 후보다. LA는 0.5 VDD에서 판정하고 디바이스 VIH는 0.7 VDD다. 약한 풀업에서 SCL이나 SDA가 0.5는 넘지만 0.7은 못 넘거나, 디바이스가 표본하는 순간 SDA가 VIH에 못 미치면 LA는 1, 디바이스는 0을 본다. 다음은 스코프로 그 비트의 전압과 tHIGH(0.7 기준)를 재거나, LA 임계를 올려 재캡처하는 것이다.

**꼬리: 그 외에는?** 주소 틀림(디코드로 확인), 슬레이브 리셋 상태/전원(같은 시점 PMIC 로그), 슬레이브의 busy NACK(EEPROM 쓰기 사이클 중 ack polling은 정상 NACK다), 글리치, 레벨시프터 방향.

</details>

<details><summary>Q4. SDA가 low로 stuck입니다. 복구 순서는?</summary>

SCL을 9번 토글한다 (UM10204 Bus clear). 슬레이브가 데이터를 내보내다 멈췄다면 클럭을 받아 나머지 비트를 끝내고 ACK 슬롯에서 SDA를 놓는다. 그 뒤 STOP을 생성한다. 이 캡처에서는 5번째 하강 뒤 풀렸다.

**꼬리: 왜 9번입니까?** 슬레이브가 어느 비트에 멈췄든 데이터 8비트 + ACK 1비트면 한 바이트 경계를 지난다. 그 뒤 슬레이브는 SDA를 놓아야 한다.

**꼬리: 9번으로 안 풀리면?** 슬레이브 리셋 핀 또는 전원 사이클. 설계 측면에서는 hang 가능 디바이스의 RESET 핀을 GPIO로 빼 두는 이유다.

**꼬리: SCL이 low로 잡혀 있으면?** 마스터가 클럭을 낼 수 없으므로 9-클럭 복구가 무의미. 슬레이브를 리셋하거나 분리해 범인을 찾는다. SMBus는 이런 고착을 25~35 ms 타임아웃으로 규정한다.

</details>

<details><summary>Q5. Clock stretching timeout을 스펙이 정의합니까?</summary>

UM10204는 stretching 상한을 두지 않는다. 슬레이브가 얼마든지 SCL을 쥘 수 있다. 시스템에서는 타임아웃이 필요하므로 정책으로 둔다. SMBus의 tTIMEOUT 25~35 ms를 빌리는 경우가 많다.

**꼬리: LA 캡처에서 stretching과 마스터의 긴 low를 구분할 수 있나요?** 없다. 선 위 파형이 똑같다 (`B.limit.stretch_owner_unobservable`). 슬레이브 분리, 구동 채널, 또는 컨트롤러의 stretch 상태 레지스터로 구분한다.

**꼬리: 리포트에는 뭐라고 쓰나요?** "SCL low 지속 시간"을 사실로 적고 "슬레이브 stretch로 추정"을 주석으로 쓴다.

</details>

<details><summary>Q6. Arbitration lost를 LA로 어떻게 잡습니까?</summary>

버스 선만으로는 못 잡는다. wired-AND라 진 쪽의 비트 흔적이 없고 이긴 쪽의 정상 프레임만 남는다. 각 마스터의 **구동** 쪽 채널(GPIO 미러, 직렬저항 드라이버측 노드, FPGA 내부 신호)을 함께 캡처해 "구동 1 & 관측 0"을 찾는다. 첫 그런 master 슬롯이 진 시점이다.

**꼬리: ACK 슬롯도 비교합니까?** 안 한다. 슬레이브 ACK는 정상적으로 구동 1(마스터 해제)·관측 0이다. 마스터가 내보내는 슬롯(주소 8비트, 쓰기 데이터 8비트)만 비교한다.

**꼬리: 두 마스터가 같은 데이터를 보내면?** 끝까지 아무도 지지 않는다. 스펙상 정상이다.

**꼬리: 진 쪽이 계속 구동하면?** 이기는 쪽도 "구동 1·관측 0"이 나타난다 → 이 레슨의 `CONTENTION` 검사 (`A.arb.buggy_loser_contention`).

</details>

<details><summary>Q7. Hold 위반과 진짜 START를 어떻게 구별합니까?</summary>

SDA가 SCL HIGH 동안 변하면 START/STOP이다. 그러나 다음 SCL 하강까지 간격이 0.5·tHD;STA 미만이면 tHD;STA(0.6 µs Fast-mode) 어느 쪽도 안 지켜지므로 START라기엔 말이 안 되고, 직전 비트의 launch가 SCL↓보다 앞선 **hold 위반**으로 본다. 프레임을 끊지 않고 HOLD 위반으로 기록한다.

**꼬리: 그 0.5는 어디서 나옵니까?** 경험칙이며 튜닝 파라미터다. 진짜 START의 tHD;STA 위반이 이 경계보다 더 짧으면 HOLD로 오분류된다. 리포트는 "HOLD 위반 (또는 START의 tHD;STA 위반)"처럼 둘 다 적는 게 안전하다.

**꼬리: 디바이스의 300 ns 내부 hold는요?** UM10204 요구는 SCL 하강의 미정의 구간 대비 hold를 디바이스 내부에서 만들라는 것이다. LA 입장에서는 SDA 변화가 SCL↓ 이전이면 위반으로 표시되는 게 맞다.

</details>

<details><summary>Q8. 간헐 실패가 하루에 한 번 납니다. 무엇을 준비합니까?</summary>

(1) 트리거: NACK, 또는 SDA stuck 같은 이벤트로 프리트리거를 잡아 실패 앞 구간을 저장한다. (2) 레이트: 위 표대로 판정 목적에 맞춘다. 원인 추정에는 ≥ 50 MS/s, 그냥 디코드면 수 MS/s. 메모리와 시간의 트레이드오프를 계산한다. (3) 재현율 수학: 실패 확률 p면 95% 확률로 한 번 보려면 N ≥ ln(0.05)/ln(1−p) ≈ 3/p 회 (추가 연습 4). (4) 조건 스윕: 온도, VDD, 부하 RC. 약한 풀업은 온도·습도로 간헐이 된다.

**꼬리: 하루 한 번인데 어떻게 고치고 있다고 압니까?** 0회 관측은 없음의 증명이 아니다 — 수정 뒤 3/p 이상 돌려 관측 0이면 "실패율 < p 95% 신뢰"라고 적는다. 그리고 측정 가능한 마진(스코프 tr, LA tHIGH)을 수정 전후로 비교한다.

</details>

## 9. 추가 연습

1. **레지스터 트레이스** — `dec.frames` 를 펌웨어 관점으로 변환하라: `RD 0x48[0x01] -> 12 34` (포인터 쓰기 + rSTART 읽기), `WR 0x50[0x01] <- A5 3C`, `PTR 0x50 <- 0x07`, `NACK 0x60`.

<details><summary>답</summary>

프레임 인덱스 i를 쓰면서 "1바이트 쓰기 + 다음 프레임이 같은 주소 rSTART 읽기"면 두 프레임을 합친다. 주소 NACK을 먼저 처리한다. `practice.py` 의 `reg_trace`.

</details>

2. **SPI 모드 추론** — CSV 컬럼 scl=SCK, sda=MOSI, aux cs. `spi_infer_mode(cap)` 로 CPOL/CPHA를 찾고 `spi_decode`로 복원하라. CPHA를 틀리면 어떻게 되는가?

<details><summary>답</summary>

CPOL은 CS 낙하 때 SCK의 유휴 레벨. MOSI가 선행 에지 직후에 바뀌면 CPHA=1, 후행 에지 직후면 CPHA=0. 잘못된 CPHA 디코드의 비대칭이 재미있다: CPHA=0 신호(실제 표본: 선행)를 CPHA=1(후행)로 읽으면 후행 에지 표본도 MOSI가 바뀌기 전이라 **값이 맞다** (마진은 hold뿐). 반대로 CPHA=1 신호를 CPHA=0(선행 표본)으로 읽으면 직전 비트를 읽어 전부 밀린다. "어떤 모드는 오설정인데 저속에서만 된다"는 전형적인 간헐 실패의 배경이다. `practice.py` 의 `P2.*` 가 양쪽을 CHECK.

</details>

3. **개방 루프 vs 피드백 마스터** — `master_style(dec, cfg_low, cfg_high, tol)` 로 설정값 대비 HIGH·LOW 변화를 분류하라: HIGH만 줄고 LOW가 늘면 open_loop, HIGH 유지·LOW 늘면 closed_loop(카운터가 SCL HIGH 감지 후 시작), 둘 다 그대로면 no_delay.

<details><summary>답</summary>

이것은 §5의 모델 가정(개방 루프)이 실제 컨트롤러와 다를 수 있음을 보여 준다. 피드백 마스터는 지연이 HIGH를 깎지 않고 주기를 늘린다(그래서 tHIGH 위반 대신 fSCL 저하로 나타난다). `practice.py` 의 `master_style`.

</details>

4. **간헐 실패 통계** — 주소별 NACK률을 계산하고, 실패 확률 p인 고장을 95% 확률로 한 번 관찰하려면 몇 회가 필요한가? 시뮬레이션으로 검증하라.

<details><summary>답</summary>

(1−p)^N ≤ 1−conf → N ≥ ln(1−conf)/ln(1−p). p=0.5 → 5, 0.1 → 29, 0.01 → 299. Monte-Carlo 4000회 경험비를 0.95 ±0.012(≈3σ)로 CHECK. 작은 p에서 N ≈ 3/p (rule of three). `practice.py`.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py; echo exit=$?   # 19/19 passed, exit 0
```

## 10. 핵심 정리

- LA는 1비트 샘플러: 임계 통과 시각만 저장한다. rise time, 전압, 드라이버 소유자는 저장되지 않는다.
- 에지 차의 오차는 ±dt → VIOLATION / MARGINAL / OK 3값. naive 비교는 합격 비트를 위반으로 찍는다.
- 단정의 하한: |limit−참값| ≥ 2·dt. 디코드의 하한은 훨씬 낮다(판정 목적이 샘플레이트를 정한다).
- 동시 에지: SCL↑ 시 SDA 먼저, SCL↓ 시 SCL 먼저. 안 그러면 가짜 START/STOP.
- 약한 풀업 신호: SDA launch 상승−하강 지연(자가 교정), SCL HIGH 감소·LOW 증가. tr은 추정치이고 스코프로 확인.
- LA 임계 0.5 ≠ VIH 0.7: LA가 깨끗해도 칩은 실패할 수 있다.
- Arbitration lost는 버스 선으로는 안 보인다: 구동 vs 관측 채널이 필요하고, ACK·읽기 슬롯은 비교에서 뺀다.
- SDA low 고착 → 9-클럭 후 STOP, 안 풀리면 슬레이브 리셋/전원. SCL low 고착은 9-클럭 무효.

## 참고자료

- [NXP UM10204 I2C-bus specification and user manual](https://www.nxp.com/docs/en/user-guide/UM10204.pdf) — F/S-mode 타이밍 표, 타이밍 정의 그림(0.3/0.7 VDD 기준점), Bus clear(9클럭), clock stretching 절 (표 번호는 개정판마다 다름)
- SMBus Specification (Version 3.x) — tTIMEOUT (25~35 ms) 정의 — 클럭 고착 타임아웃 정책의 근거
- HWLAB-B03 (풀업 윈도우·rise time 스코프 측정), HWLAB-B05 (프로브·대역폭), HWLAB-B07 (LA 캡처 포맷·샘플레이트), HWLAB-M06 (sigrok 디코더)
- SIV-M10 (실패 클러스터링), SIV-M12 (시퀀서), SIV-B14 (리포트), COMM 트랙 (I2C/SPI 프로토콜 기초)
