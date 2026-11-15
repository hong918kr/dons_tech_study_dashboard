---
id: SIV-A03
time: 130분
updated: 2026-11-08
---
# SIV-A03 전력 특성화 자동화

## 학습 목표

- INA226 전력 모니터를 **정수 연산만으로** 읽는 드라이버를 mock I2C 위에 쓰고, 포인터 레지스터·빅엔디안·부호·포화·CAL 반올림을 코드로 처리한다. CAL을 쓴 뒤 왜 되읽어야 하는지(브라운아웃) 설명할 수 있다.
- 합성 스윕에서 `P = Pleak(T,V) + αC·V²f` 를 **직접 구현한 최소제곱**으로 분리하고, 복원값을 주입한 참값과 표준오차 기준으로 대조한다.
- **이 스윕으로 무엇을 알 수 없는가**를 숫자로 말한다. f가 한 점, f 범위가 좁음, V-f가 묶임(DVFS 표), 자기발열을 조건수와 복원 오차로 측정하고, 조건수가 **모델 오류는 못 잡는다**는 것을 보인다.
- 8개 칩 캠페인에서 누설 과다, 클럭 게이팅 고장, shunt 오실장, Tj 센서 고착, 단일 글리치를 **서로 다른 시그니처**로 가려내고, 디버그 담당이 바로 움직일 수 있는 판정 줄을 낸다.
- "측정 오류인가, 실리콘인가?"를 판별하는 논리를 면접에서 설명할 수 있다.

## 1. 문제

전력 특성화는 칩 검증 리드가 "이 칩의 누설이 예산 안인가, 다음 스핀에서 고칠 게 있는가"를 답하는 일이고, Rivian·NVIDIA·Apple 면접에서 "누설과 동적 전력을 어떻게 분리하나, 그 측정을 어떻게 믿나"로 그대로 나온다.

이 레슨은 **두 벌의 코드**로 측정 사슬의 양 끝을 잇는다. 펌웨어 쪽 C 드라이버가 레일 하나의 숫자를 믿을 수 있게 만들고, Python 분석이 그 숫자로 물리량을 분리한다.

| 벌 | 파일 | 하는 일 |
|---|---|---|
| C | `starter.c` / `solution.c` / `practice.c` | INA226 유사 전력 모니터 드라이버 (mock I2C, 결함 주입) |
| Python | `starter.py` / `solution.py` / `practice.py` | 스윕 분석: 분리, 식별 가능성, 캠페인 판정 |
| 데이터 | `gen_power_sweep.py` → `power_campaign.csv`, `power_campaign_truth.json` | Part C 합성 캠페인(시드 고정, 89 KB) |

측정 사슬은 이렇다. 뒤로 갈수록 앞단의 오류가 **물리 현상처럼 보인다**는 게 이 레슨의 핵심이다.

```svg
<svg viewBox="0 0 760 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="a3a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="14" y="60" width="86" height="46" fill="none" stroke="currentColor"/>
  <text x="57" y="80" text-anchor="middle" fill="currentColor">전원 레일</text>
  <text x="57" y="96" text-anchor="middle" fill="currentColor">VDD_CPU</text>
  <line x1="100" y1="83" x2="138" y2="83" stroke="currentColor"/>
  <rect x="138" y="72" width="58" height="22" class="f-warn" opacity=".6"/>
  <text x="167" y="87" text-anchor="middle" fill="currentColor">Rshunt</text>
  <line x1="196" y1="83" x2="238" y2="83" stroke="currentColor" marker-end="url(#a3a)"/>
  <rect x="238" y="60" width="72" height="46" fill="none" stroke="currentColor"/>
  <text x="274" y="80" text-anchor="middle" fill="currentColor">SoC</text>
  <text x="274" y="96" text-anchor="middle" fill="currentColor">(부하)</text>
  <line x1="167" y1="94" x2="167" y2="140" stroke="currentColor" stroke-dasharray="3 3"/>
  <rect x="20" y="140" width="420" height="96" fill="none" stroke="currentColor"/>
  <text x="30" y="158" fill="currentColor" font-weight="bold">INA226 (이 레슨은 mock)</text>
  <rect x="34" y="170" width="78" height="30" class="f-mut"/><text x="73" y="189" text-anchor="middle" fill="currentColor">ADC</text>
  <rect x="134" y="170" width="86" height="30" class="f-acc" opacity=".35"/><text x="177" y="182" text-anchor="middle" fill="currentColor" font-size="12">Shunt 0x01</text><text x="177" y="196" text-anchor="middle" fill="currentColor" font-size="12">Bus 0x02</text>
  <rect x="244" y="170" width="88" height="30" class="f-acc" opacity=".35"/><text x="288" y="182" text-anchor="middle" fill="currentColor" font-size="12">Current 0x04</text><text x="288" y="196" text-anchor="middle" fill="currentColor" font-size="12">Power 0x03</text>
  <line x1="112" y1="185" x2="134" y2="185" stroke="currentColor" marker-end="url(#a3a)"/>
  <line x1="220" y1="185" x2="244" y2="185" stroke="currentColor" marker-end="url(#a3a)"/>
  <text x="232" y="215" text-anchor="middle" fill="currentColor" font-size="12">x CAL/2048 , x Bus/20000</text>
  <line x1="332" y1="185" x2="470" y2="185" stroke="currentColor" marker-end="url(#a3a)"/>
  <text x="400" y="178" text-anchor="middle" fill="currentColor">I2C</text>
  <rect x="470" y="165" width="122" height="44" fill="none" stroke="currentColor"/>
  <text x="531" y="183" text-anchor="middle" fill="currentColor">C 드라이버</text>
  <text x="531" y="199" text-anchor="middle" fill="currentColor">(Part A,B,C)</text>
  <line x1="592" y1="187" x2="614" y2="187" stroke="currentColor" marker-end="url(#a3a)"/>
  <rect x="614" y="165" width="134" height="44" class="f-ok" opacity=".35"/>
  <text x="681" y="183" text-anchor="middle" fill="currentColor">Python 분석</text>
  <text x="681" y="199" text-anchor="middle" fill="currentColor">L(T,V), C_eff, 판정</text>
  <text x="14" y="28" fill="currentColor" font-weight="bold">측정 사슬: 앞단의 오류는 뒤에서 '물리'로 보인다</text>
  <text x="14" y="46" fill="currentColor">shunt 오실장 → 전류 이득 → L과 C_eff가 같은 비율로 변함 · 온도 라벨 오류 → 온도 계수 왜곡</text>
</svg>
```

식의 각 항은 이렇다. 단위를 W, V, GHz로 쓰면 C_eff의 단위가 nF로 깔끔하게 떨어진다.

| 항 | 의미 | 무엇으로 정해지나 |
|---|---|---|
| `Pleak(T,V)` | 게이트·서브스레숄드 누설 전력 | 온도에 지수적, 전압에도 강하게 의존 (이 모델: `L25·(V/0.8)·exp(a(T−25)+b(V−0.8))`, a=0.025/℃, b=4/V — **대표값**) |
| `α·C·V²·f` | 스위칭(동적) 전력 | α(활동도)×C(스위칭 용량) = **C_eff**. 곱으로만 관측된다 |

주의: 누설이 전압에 의존하므로 `온도별로 P를 V²f에 대해 한 번 회귀`하면 V를 섞는 순간 절편이 누설이 아니게 된다. 이 함정을 Part B에서 측정한다. 그래서 Part A의 기본 절차는 **같은 (T, V) 안에서 f만 바꾼 슬라이스** 회귀다.

### Part A — 핵심: 드라이버와 분리

- **C**: `ina_read_reg` (포인터 쓰고 repeated START로 2바이트, MSB 먼저), `ina_write_reg` (`{reg, MSB, LSB}`), 코드→물리량 변환 4종(shunt nV, bus µV, current µA, power µW), `ina_compute_cal` (64비트 정수 반올림).
- **Python**: `lstsq` (MGS QR, 재직교화 2회), `singular_values` (one-sided Jacobi), `scaled_cond`, `fit_slices`, `combine_leak`, `combine_ceff`, `fit_temp_law` (ln L의 가중 회귀).
- 검사: 합성 스윕의 15개 `Pleak(T,V)` 셀과 C_eff 3개, 온도 계수 a가 **주입한 참값의 4 SE(표준오차) 안**.

### Part B — 어려운 부분: 경계와 식별 가능성

- **C**: 음수 전류, 양/음 풀스케일 포화, **shunt는 범위 안인데 current 레지스터가 오버플로** (CAL이 큼), CAL 반올림 오차(ppm) 표, 일시/지속 NAK와 재시도, 바이트 스왑 탐지(ID 고정 패턴 대 범위 검사의 한계).
- **Python**: f가 1점/2점, f 범위 좁음, V-f 묶임, 자기발열을 시나리오 표로 측정. 조건수·복원 오차·편향을 분리해 보여 준다. 자기발열은 측정 Tj를 공변량으로 쓰는 `fit_varpro`(variable projection)로 보정하고, Tj 센서 오프셋은 **이 방법으로 잡히지 않는다**는 것도 측정한다.

### Part C — 시나리오: 레일 점검과 칩 캠페인

- **C**: 가상 보드 `SENSE-EVB rev B`의 5개 레일을 전수 점검해 `NAK / ID / ENDIAN / CAL_READBACK / RANGE / CAL_LOST / GAIN`으로 원인별 FAIL 한 줄 보고.
- **Python**: `power_campaign.csv`(칩 8개)에서 결함 5개를 심은 칩을 판정한다. U2 글리치, U3 누설 과다, U5 클럭 게이팅 고장, U6 shunt 오실장, U7 Tj 고착. 양호 칩은 PASS, 복원값은 주입 참값과 대조한다.

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="14" y="20" fill="currentColor" font-weight="bold">왼쪽: 같은 V에서 f 스윕 → 절편 = 누설</text>
  <text x="400" y="20" fill="currentColor" font-weight="bold">오른쪽: V-f 묶임 / 자기발열이면 직선이 거짓말을 한다</text>
  <g stroke="currentColor"><line x1="40" y1="260" x2="360" y2="260"/><line x1="40" y1="260" x2="40" y2="40"/><line x1="420" y1="260" x2="740" y2="260"/><line x1="420" y1="260" x2="420" y2="40"/></g>
  <text x="200" y="285" text-anchor="middle" fill="currentColor">x = V² f</text>
  <text x="580" y="285" text-anchor="middle" fill="currentColor">x = V² f</text>
  <text x="48" y="52" fill="currentColor">P</text><text x="428" y="52" fill="currentColor">P</text>
  <line x1="40" y1="235" x2="340" y2="95" stroke="currentColor" stroke-width="1.5"/>
  <line x1="40" y1="170" x2="340" y2="30" stroke="currentColor" stroke-width="1.5"/>
  <circle cx="40" cy="235" r="4" class="f-ok"/><circle cx="40" cy="170" r="4" class="f-warn"/>
  <text x="48" y="232" fill="currentColor">L(25C)</text><text x="48" y="167" fill="currentColor">L(85C)</text>
  <text x="250" y="150" fill="currentColor">기울기 = C_eff</text>
  <g class="f-ok"><circle cx="110" cy="202" r="3"/><circle cx="170" cy="174" r="3"/><circle cx="230" cy="146" r="3"/><circle cx="290" cy="118" r="3"/></g>
  <g class="f-warn"><circle cx="110" cy="137" r="3"/><circle cx="170" cy="109" r="3"/><circle cx="230" cy="81" r="3"/><circle cx="290" cy="53" r="3"/></g>
  <path d="M 460 238 Q 560 215 620 165 T 720 55" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <line x1="420" y1="264" x2="730" y2="48" stroke="currentColor" stroke-dasharray="5 4"/>
  <circle cx="420" cy="264" r="4" class="f-warn"/>
  <text x="430" y="250" fill="currentColor">점선 절편 = 가짜 누설</text>
  <text x="540" y="195" fill="currentColor">실제 곡선: V 커질수록</text>
  <text x="540" y="211" fill="currentColor">L(V)도, 자기발열도 커진다</text>
</svg>
```

## 2. Problem (English)

**C set.** Implement an integer-only driver for a TI INA226 power monitor behind a transaction-level I2C abstraction `xfer(ctx, addr, tx, ntx, rx, nrx)`.

```c
int ina_read_reg(ina226_t *d, uint8_t reg, uint16_t *out);   /* pointer write + 2-byte read, big-endian, retries on NAK */
int ina_write_reg(ina226_t *d, uint8_t reg, uint16_t val);   /* {reg, MSB, LSB} */
int64_t  ina_shunt_nv(uint16_t raw);    /* signed, LSB = 2.5 uV  */
uint32_t ina_bus_uv(uint16_t raw);      /* unsigned, LSB = 1.25 mV */
int64_t  ina_current_ua(uint16_t raw, uint32_t lsb_ua); /* signed */
uint64_t ina_power_uw(uint16_t raw, uint32_t lsb_ua);   /* LSB = 25 x current LSB */
int ina_compute_cal(uint32_t lsb_ua, uint32_t rshunt_uohm, uint16_t *cal);
    /* CAL = 0.00512 / (I_LSB * Rshunt), rounded; error if 0 or > 0x7FFF */
int ina_probe(ina226_t *d);             /* 0x5449/0x2260; a byte-swapped 0x4954 => ENDIAN */
int ina_read_sample(ina226_t *d, ina_sample_t *s); /* OVF, SAT flags */
```

Registers: 0x00 Config, 0x01 Shunt, 0x02 Bus, 0x03 Power, 0x04 Current, 0x05 Calibration, 0x06 Mask/Enable, 0xFE Mfg ID 0x5449, 0xFF Die ID 0x2260. Then survey a board's rails and classify each as PASS or one of `NAK / ID / ENDIAN / CAL_READBACK / RANGE / CAL_LOST / GAIN`, emitting a single parseable report line per rail.

**Python set.** Given sweep rows `{w, t, v, f, p, tj}` for workload × temperature × voltage × frequency, recover the model `P = L(T,V) + C_eff(w)·V²·f`, implementing least squares yourself (stdlib only). Example: for a fixed `(w, t, v)` slice with five frequencies, `fit_slices` returns the intercept, slope and their unscaled covariances. Then fit `ln L` vs T with delta-method weights. Quantify identifiability: report the condition number and the recovered-parameter error for degraded designs (single frequency, narrow frequency range, V locked to f, self-heating), and correct self-heating by a Tj-covariate variable-projection fit. Finally classify a fleet of 8 chips from a campaign CSV.

Constraints: no compiler builtins for the arithmetic, no third-party Python packages, deterministic seeds, no wall-clock in the checks.

## 3. 시작 코드

C 세트.

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨져 있어 지정
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/SIV-A03_st && /tmp/SIV-A03_st   # 빌드 경고 0, FAIL 다수, exit 1
```

Python 세트. 레슨 디렉터리에서 실행한다(`power_campaign.csv`를 스크립트 옆에서 읽음). 시작 코드는 첫 TODO에서 `NotImplementedError`로 각 Part가 FAIL 처리되어 `0/3 passed` 로 끝난다.

<!-- code: starter.py -->

```bash
python3 -I starter.py; echo "exit=$?"    # FAIL, exit 1
python3 -I solution.py --report          # 시나리오 표(n=200)까지 출력
```

Part C 데이터 생성기(결정적, 다시 돌리면 같은 바이트).

<!-- code: gen_power_sweep.py -->

## 4. 힌트

<details><summary>힌트 1 — INA226 읽기의 두 단계</summary>

읽을 레지스터를 가리키는 **포인터를 먼저 쓴다** (주소+W, 포인터 1바이트). 그다음 repeated START 뒤 2바이트를 읽는다. 데이터는 MSB가 먼저 온다. NAK으로 재시도할 때는 전체 트랜잭션을 다시 해야 한다. 재시도에서 포인터 쓰기를 빼면, 포인터 바이트가 NAK으로 안 들어간 경우 이전 레지스터 값을 읽게 된다.
</details>

<details><summary>힌트 2 — 부호와 64비트</summary>

`raw`를 부호 있는 값으로 만들 때 `(int16_t)raw`는 범위 밖이면 구현 정의(C11 6.3.1.3)다. `raw >= 0x8000 ? raw - 65536 : raw`로 산술 해석하면 이식성 걱정이 없다. power는 `raw × 25 × lsb_ua`가 32비트를 넘는다 (65535 × 25 × 1000 µA > 2³²). 분모 `lsb_ua × rshunt_uohm`도 64비트로 계산한다.
</details>

<details><summary>힌트 3 — CAL 반올림과 범위</summary>

`0.00512 / (I_LSB[A] × R[Ω]) = 5.12e9 / (lsb_µA × R_µΩ)`. `(5120000000ULL + den/2) / den`이 반올림이다. 결과가 0이나 0x7FFF보다 크면 레지스터에 쓸 수 없다 (bit 15는 예약). CAL 값이 작을수록 반올림 오차 비율이 커진다.
</details>

<details><summary>힌트 4 — 분리: 슬라이스 회귀</summary>

같은 `(w, t, v)`에서는 V가 상수이므로 x = V²f는 f와 비례한다. x에 대해 직선을 맞추면 절편이 그 (T, V)의 누설이고 기울기가 C_eff다. 워크로드 간 절편은 같아야 하므로(같은 칩, 같은 T와 V) 역분산 가중으로 합쳐 정밀도를 올린다.
</details>

<details><summary>힌트 5 — 조건수를 어떻게 쓸 것인가</summary>

열을 단위 노름으로 맞춘 뒤 `σmax/σmin`을 본다. 단위 선택(GHz 대 Hz)에 값이 흔들리지 않게 하기 위함이다. 특이값은 `XᵀX`의 고유값이 아니라 `X`에 직접 one-sided Jacobi를 적용해 구한다 (XᵀX를 쓰면 조건수가 제곱된다). 다만 조건수는 **열 사이 공선성**을 재는 값이다. 모델 형태가 틀린 것(누설의 V 의존성을 무시한 경우)은 조건수로 보이지 않는다.
</details>

<details><summary>힌트 6 — 자기발열 보정은 왜 variable projection인가</summary>

Tj가 전력의 함수가 되면 슬라이스별 f 축이 온도 축과 섞인다. 측정 Tj로 `L25[V]·exp(a(Tj−25)) + C[w]·V²f`를 푼다. a를 고정하면 나머지는 **선형**이다. 그래서 a를 1차원으로 탐색하고 안쪽은 lstsq로 푼다.
</details>

## 5. 풀이

### Part A — C 드라이버

레지스터 접근의 요점은 두 가지다. 포인터 레지스터가 상태로 남는다는 것과, 데이터가 빅엔디안이라는 것이다. Cortex-M은 리틀엔디안이므로 `*(uint16_t *)buf` 같은 코드는 바이트가 뒤집힌다. 조립은 항상 `(rx[0] << 8) | rx[1]` 로 명시한다 (C-B13 참고).

스케일링은 LSB 표로 끝난다.

| 레지스터 | 값 형식 | LSB | 풀스케일/메모 |
|---|---|---|---|
| Shunt (0x01) | 부호 16비트 | 2.5 µV | ±81.92 mV (0x7FFF = 81.9175 mV, 0x8000 = −81.92 mV) |
| Bus (0x02) | 부호 없음 (bit 15 = 0) | 1.25 mV | 레지스터 풀스케일 40.96 V, 정격 입력은 36 V까지 |
| Current (0x04) | 부호 16비트 | Current_LSB | 사용자가 CAL로 정함 |
| Power (0x03) | 부호 없음 16비트 | 25 × Current_LSB | 역방향 전류의 부호는 Current에서 판단한다 |

칩 내부 계산은 `Current = Shunt × CAL / 2048`, `Power = Current × Bus / 20000`이다. 이 레슨의 mock은 나눗셈을 0쪽 절사로, 오버플로를 포화 + OVF 실시간 표시로, Power는 |Current| 사용으로 모델링한다. 이 세 가지는 데이터시트가 비트 단위로 규정한 부분이 아니라 **mock의 가정**이다. 실제 판정 드라이버는 OVF가 서면 current/power를 무효로 취급하면 된다.

**CAL 값의 의미.** Current 레지스터는 shunt 전압에 고정 이득 `CAL/2048`을 곱한 수다. CAL = 2048이면 Current 코드와 Shunt 코드가 같다. 따라서 CAL을 크게 잡아도 정보가 늘지 않는다. ADC 해상도는 2.5 µV로 고정이고, Current_LSB는 **표시 단위**일 뿐이다.

CAL을 크게 잡으면 오히려 `shunt 범위 안인데 current 레지스터가 먼저 넘친다`. 테스트로 박았다: 40 mV × CAL 5120 / 2048 = 40000 > 32767 → OVF 플래그, SAT는 아님.

왕복 시험은 정규화 난수 입력에 대해 드라이버 읽기를 기대값(double 독립 계산)과 비교한다. 허용 오차는 모두 양자화에서 유도했다.

```text
PASS 300 random shunt reads within 1/2 LSB (bad 0)
PASS 300 random bus reads within 1 LSB (bad 0)
PASS 300 random power reads within 25 mW bound (bad 0)
```

power 허용 25 mW의 근거: P_LSB(12.5 mW) + 전류 오차(절사 0.5 mA + shunt 반 LSB 환산 0.125 mA) × 최대 12 V + 전압 양자화 0.625 mV × 5 A ≈ 23 mW의 상계다. 시드 고정이라 결정적이고, 허용치를 넘으면 드라이버나 mock의 변환 경로가 틀린 것이다.

### Part A — Python 분리

**최소제곱.** `lstsq`는 modified Gram-Schmidt QR이다. 정규방정식 `XᵀX`를 직접 풀면 조건수가 제곱되므로(κ(XᵀX) = κ(X)²) 피했다. 재직교화 2회는 나쁜 조건에서 Q의 직교성을 지켜 준다. 열을 먼저 단위 노름으로 스케일해 `R_jj < 1e-10`을 "이 열은 앞 열들의 선형결합"의 상대 임계로 쓴다. 공분산은 `(XᵀX)⁻¹ = R⁻¹R⁻ᵀ` (열 스케일 복원 포함)이고, 분산은 슬라이스들의 잔차를 풀링한 `s²`을 곱해 얻는다.

```text
PASS lstsq exact line y=1+2x : [0.9999999999999991, 2.0000000000000004]
PASS singular_values vs known [[1,2],[3,4],[5,6]] : [9.525518091565106, 0.5143005806586443]
PASS 45 slices (3 wl x 5 T x 3 V), none underdetermined : 45
PASS pooled noise sigma between 3 mW and 10 mW : 4.71 mW
PASS all 15 Pleak(T,V) within 4 SE of truth : bad=[]
PASS C_eff per workload within 4 SE of truth : bad=[]
PASS temperature exponent a within 4 SE : a=0.02522 se=0.00023
PASS L25 within 4 SE : L25=0.1473 se=0.0025
```

**허용 오차 4 SE의 이유.** 슬라이스 하나의 잡음 σ를 슬라이스 잔차 풀링으로 추정했고(4.71 mW, 주입 잡음은 3 mW 절대 + 0.2% 상대라 고전력 슬라이스가 더 시끄럽다), 복원값의 표준오차는 분석식으로 나온다. 잡음이 정규이면 4σ 밖으로 나갈 확률은 약 6×10⁻⁵이다. 시드를 고정해 결정적으로 PASS이고, 편향이 있는 방법이면 이 검사를 넘지 못한다. 오차가 나쁜 시드를 골라서 맞춘 게 아니라 **추정량이 주장하는 불확실성을 믿는지**를 시험한다.

왜 슬라이스가 기본인가: 같은 (T, V) 안에서 x는 f에 비례한다. 그 안에서 절편은 "f→0으로 외삽한 전력"이고, 외삽 거리는 x의 최솟값이 정한다. 예를 들어 V=0.80 슬라이스의 x 범위는 0.51~1.28이고, 이 범위가 넓을수록 절편이 좋아진다.

**ln L 회귀의 가중치.** 저온에서 누설은 작고 슬라이스 절편의 표준오차는 거의 같다(절편의 SE는 x 배치와 잡음으로 정해진다). 그러므로 `ln L`의 분산은 `Var[ln L] ≈ se²/L²` (델타법)로, 차가운 점일수록 크다. 무가중이면 −40 ℃의 잡음이 a를 끌고 간다. 시드 120개에 대한 측정.

```text
[measure] mean|a error| weighted=0.00036 unweighted=0.00094 over 120 runs
```

세 배 가까운 차이다. L ≤ 0으로 나온 점은 로그를 취할 수 없으므로 버리고 어느 온도를 버렸는지 기록한다(`dropped`). 조용히 넘기면 "그 온도에 누설이 있었다/없었다"의 정보를 잃는다.

### Part B — C: 드라이버의 경계

```text
PASS current overflow flagged while shunt in range (flags 1)
PASS CAL err lsb=457 R=2000 cal=5602 err=44.5 ppm (bound 89.3)
PASS CAL err lsb=1000 R=100000 cal=51 err=-3906.3 ppm (bound 9765.6)
PASS CAL err lsb=1000000 R=10000 cal=1 err=953125.0 ppm (bound 976562.5)
[limit] range-check missed 1228/2000 byte-swapped currents (10 A window, 500uA LSB)
PASS range check alone misses some byte swaps (missed 1228/2000)
```

**CAL 반올림 오차.** `|err| ≤ 0.5/CAL_exact`. CAL이 수천이면 수십 ppm이라 무시할 수 있고, CAL이 수십이면 % 단위 이득 오차가 된다. 표는 이 수식을 CAL 범위에 걸쳐 확인한다 (표의 `bound`). Current_LSB를 임의로 작게 잡아 CAL을 크게 만드는 건 정보를 늘리지 않고, 큰 값은 current 오버플로 위험만 늘린다. 반대로 LSB를 지나치게 크게 잡으면 CAL이 작아져 % 단위 이득 오차가 된다. 실제 드라이버는 `Imax/32768` 이상의 깔끔한 값(P3)을 쓰고 CAL이 수백~수천이 되게 잡는다.

**NAK.** 일시 NAK(예: 첫 시도에서 데이터 바이트 NAK)은 전체 트랜잭션 재시도로 복구한다. 시험은 값까지 확인한다. 지속 NAK은 재시도 소진 후 `INA_ERR_NAK`.

**바이트 스왑.** 범위 검사(|I| < 10 A)는 스왑된 값이 우연히 범위 안에 들어오면 놓친다. 정상 전류를 8000 코드 범위에서 뽑아 스왑했더니 2000개 중 **1228개(약 61%)가 범위 검사를 통과**했다. 상위 바이트가 작으면 스왑 후 값이 10 A 안에 쉽게 들어오기 때문이다. ID 레지스터는 고정 패턴이라 스왑을 확정적으로 잡는다(`0x5449`가 `0x4954`로 읽힘). 이것이 **ID 검사를 값 검사보다 먼저 하는 이유**다.

### Part B — Python: 이 스윕으로 무엇을 알 수 없는가

시나리오는 T=25 ℃ 기준 `L(25, 0.80)`과 C_cpu 복원을 몬테카를로로 측정한다 (Tj 잡음 0.3 ℃, 전력 잡음 3 mW + 0.2%, n=200). S4b만 85 ℃ 기준이다. 아래는 `python3 -I solution.py --report`의 실제 출력이다.

```text
scenario                          kappa    L_rms%   L_bias%    C_rms%   C_bias%
S0 full 3Vx5f                       6.7      1.95     -0.19      0.27      0.03
S1 2 f points                       4.9      2.39     -0.01      0.32     -0.04
S2 narrow f 1.9-2.0               110.3     35.70     -2.96      3.87      0.38
S3 DVFS-locked pooled              16.7     66.90    -66.89      7.09      7.09
S4a self-heat 25C, slice            6.7      2.18      0.96      3.66      3.65
S4b self-heat 85C, slice            6.7     10.08     10.06     23.72     23.72
S5 self-heat, varpro(Tj)            nan      1.59      0.09      0.17     -0.01
S6 varpro, Tj offset +5C            nan     11.76    -11.67      0.17     -0.01
```

| 시나리오 | 무엇을 바꿨나 | 해석 |
|---|---|---|
| S0 | 기준: 3 V × 5 f, 슬라이스 회귀 | L 1.95% rms(잡음), 편향 없음 |
| S1 | f를 양 끝 2점만 | 놀랍게도 정밀도는 비슷하다. 양 끝점은 분산에 유리하다. 하지만 dof = 0이라 잡음을 추정할 수 없고 곡률도 못 본다. 코드는 pooled_s2 = None으로 답한다 |
| S2 | f를 1.90~2.00 GHz로 좁힘 | κ = 110, L 오차 **35.7%** — f→0 외삽 거리가 멀어지며 잡음이 증폭된다 |
| S3 | V-f가 DVFS 표로 묶임, 온도별 pooled 회귀 | L 편향 **−67%**, C 편향 +7%. 잡음이 아니라 **모델 오류** (누설의 V 의존이 V²f에 흡수됨) |
| S4a/S4b | 자기발열 Rth=6 ℃/W를 챔버 온도 라벨로 무시 | 25 ℃에서는 L 1%뿐이지만 C_eff +3.6%, 85 ℃에서는 L +10%, C_eff **+24%** |
| S5 | Tj 공변량 varpro | S4의 편향 제거 (L 1.6% rms, C 0.17%) |
| S6 | Tj 센서 +5 ℃ 오프셋 + varpro | L25 **−11.7%** — exp(−5a)−1 = −11.8%와 일치. **전력 데이터만으로는 구분 불가** (절편 L25에 흡수) |

여기서 배울 세 가지.

**첫째, 조건수는 공선성은 잡지만 모델 오류는 못 잡는다.** S3의 κ = 16.7은 S2의 110보다 훨씬 작다. 그런데 S3의 편향이 압도적으로 크다. pooled 회귀의 설계 행렬은 `[1, V²f]`라 그 자체로는 멀쩡해 보인다. 나쁜 것은 "누설이 V에 무관하다"는 모델 가정이다. 참 모델의 열 `[L(V) 형상, V²f]`로 재면 full 5.9 대비 DVFS 16.7로 2.8배 나빠진다(이 κ도 슬라이스 설계의 κ와는 다른 양이다). 코드 `true_kappa`로 측정했다.

```text
PASS V-f coupling inflates true-model kappa (>2x) : full=5.9 dvfs=16.7
PASS S3 DVFS-locked pooled: leakage biased >15% (not noise) : bias -67.0%
```

그래서 **DVFS 표 위 점만 찍는 스윕은 누설/동적 분리를 원리적으로 못 한다.** 추가 측정이 답이다 (§9 P4: 합법 영역의 V-f 비결합 점 추가).

**둘째, 자기발열은 온도 축을 파괴한다.** 챔버 온도를 T로 라벨링하면 "같은 T" 안에서 고전력 점이 더 뜨겁다. 자기발열이 큰 85 ℃에서 C_eff가 24% 커지는 이유는 고주파 점일수록 누설도 같이 커져 기울기에 흡수되기 때문이다. 온도 계수 a는 평균이 0.02758로 참값 0.025 대비 +10%, Tj를 공변량으로 쓴 varpro는 0.02490이다.

```text
[measure] mean a: slice(chamber T)=0.02758 varpro(Tj)=0.02490 truth=0.02500
```

**셋째, 데이터로 안 되는 것이 있다.** Tj 센서 오프셋은 전역 스케일로 흡수된다. 모델이 `L25·exp(a(Tj−25))`이므로, Tj에 +5 ℃를 더한 것과 L25를 `exp(−5a)`배 한 것이 완전히 같은 전력을 만든다. 이 축 하나는 절대 식별 불가이고 **외부 기준**이 필요하다. 예: 칩을 전원 끈 뒤 soak하여 다이 온도가 챔버와 같은 때의 센서 눈금 교정, 또는 온도 고정 열 헤드.

α와 C도 곱으로만 관측된다는 것을 코드로 확인했다(같은 곱이면 데이터가 비트 단위로 같다). α를 따로 쓰려면 칩의 활동 카운터(PMU, 내부 아비터 toggle) 같은 **별도 측정**이 필요하다.

슬라이스 수가 부족한 경우도 코드는 답을 지어내지 않는다.

```text
PASS single f per slice: underdetermined, no fabricated result : {'pooled_s2': None, 'underdetermined': 9}
PASS two f points: exact fit, dof=0, noise unknown : None
PASS DVFS-locked: every (w,t,v) slice has one f : {'pooled_s2': None, 'underdetermined': 15}
```

```svg
<svg viewBox="0 0 760 210" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="14" y="20" fill="currentColor" font-weight="bold">식별 가능성 판정 흐름 (데이터 설계를 받을 때 먼저 묻는 질문)</text>
  <g fill="none" stroke="currentColor">
    <rect x="14" y="40" width="142" height="50"/><rect x="176" y="40" width="142" height="50"/><rect x="338" y="40" width="142" height="50"/><rect x="500" y="40" width="246" height="50"/>
  </g>
  <text x="85" y="60" text-anchor="middle" fill="currentColor">V당 f가 3점 이상?</text>
  <text x="85" y="78" text-anchor="middle" fill="currentColor">아니면 잡음/곡률 불가</text>
  <text x="247" y="60" text-anchor="middle" fill="currentColor">V와 f가 분리?</text>
  <text x="247" y="78" text-anchor="middle" fill="currentColor">DVFS 표만이면 pooled 편향</text>
  <text x="409" y="60" text-anchor="middle" fill="currentColor">Tj를 측정했나?</text>
  <text x="409" y="78" text-anchor="middle" fill="currentColor">없으면 self-heat 혼입</text>
  <text x="623" y="60" text-anchor="middle" fill="currentColor">Tj 절대 정확도 기준이 있나?</text>
  <text x="623" y="78" text-anchor="middle" fill="currentColor">전력만으론 오프셋 분리 불가</text>
  <g stroke="currentColor" fill="none"><line x1="156" y1="65" x2="176" y2="65"/><line x1="318" y1="65" x2="338" y2="65"/><line x1="480" y1="65" x2="500" y2="65"/></g>
  <rect x="14" y="120" width="732" height="72" class="f-mut" opacity=".45"/>
  <text x="26" y="140" fill="currentColor">결과물 표기 규칙: 값마다 "측정 방법과 가정"을 같이 적는다.</text>
  <text x="26" y="158" fill="currentColor">L25@0.80V = 0.163 W (slice fit, 3V x 5f, Tj-covariate 없음이면 25C 기준 자기발열 편향 포함)</text>
  <text x="26" y="176" fill="currentColor">→ 숫자만 넘기면 나중에 "이 칩의 누설"이 측정 조건에 따라 달라져도 이유를 못 찾는다.</text>
</svg>
```

### Part C — C: 레일 전수 점검

검사 순서가 곧 진단 논리다. 앞 단계가 뒤 단계의 측정을 믿을 수 있게 한다.

1. **프로브**: NAK이면 소자 자체가 없다(납땜, 주소 핀, 전원). ID/ENDIAN을 값 검사보다 먼저 본다.
2. **CAL 쓰고 되읽기**: 쓰기가 안 먹은 레지스터는 뒤의 모든 전류를 망친다.
3. **샘플 + 플래그**: OVF/SAT이면 값 자체가 무효다.
4. **CAL 재확인(CAL_LOST)**: POR로 CAL이 0이 되면 current는 0이다. **shunt는 정상인데 current만 0** 인 전형적 브라운아웃 패턴이다. 측정 끝에 CAL을 다시 읽어 잡는다.
5. **벤치 기준과 이득 비교(GAIN)**: 여기서 비로소 "값이 맞는가"를 묻는다.

```text
  PASS rail=VDD_CPU addr=0x40 i_ma=2500
  PASS rail=VDD_GPU addr=0x41 i_ma=3000
  PASS rail=VDD_DDR addr=0x42 i_ma=1200
  PASS rail=VDD_IO addr=0x43 i_ma=300
  PASS rail=VDD_AON addr=0x44 i_ma=40
  FAIL rail=VDD_IO addr=0x43 reason=NAK
  FAIL rail=VDD_CPU addr=0x40 reason=ENDIAN
  FAIL rail=VDD_GPU addr=0x41 reason=ID
  FAIL rail=VDD_GPU addr=0x41 reason=CAL_READBACK
  FAIL rail=VDD_DDR addr=0x42 reason=RANGE
  FAIL rail=VDD_CPU addr=0x40 reason=CAL_LOST
  FAIL rail=VDD_AON addr=0x44 reason=GAIN i_ma=20 dmm_ma=40 ratio=0.500
  [limit] PASS rail=VDD_CPU addr=0x40 i_ma=2537
```

마지막 줄이 이 검사의 **한계**다. shunt 저항이 +1.5% 틀어져도(1% 공차 저항 범위 밖이지만 벤치 스크린의 2% 안) GAIN 검사는 통과한다. 2%는 "실장 오류·배선 사고"를 잡는 대표값 스크린이고, 정밀 특성화 값의 기준이 아니다. 진짜 정밀도가 필요하면 4단자 기준 저항으로 각 레일의 이득을 교정하거나 Part A 흐름에 교정 계수를 넣어야 한다. shunt 공차·온도계수와 INA226 자체의 게인 오차는 교정 없이는 합쳐서 남는다.

실패 메시지가 한 줄씩이라 벤치에서 grep으로 집계할 수 있다. `ratio=0.500`은 그 자체로 "반값 shunt"를 말해 준다 (shunt가 정확히 절반이면 전류가 절반으로 읽힌다).

### Part C — Python: 칩 캠페인

`gen_power_sweep.py`는 SoC `SENSE-1`의 VDD_CPU 레일을 칩 8개에 대해 1800행으로 만든다. Rth = 3 ℃/W(대표값)로 자기발열이 들어 있다. 칩별 공정 변동이 있고, 양호 칩은 U1, U4, U8이다.

**분석 파이프라인.** 칩마다 먼저 `precheck_chip`(데이터 관문) → `characterize_chip`(varpro + 구간별 MAD 이상치 제거, 1회 재피팅) → 전체를 모아 `classify_fleet`로 비율 판정. 관문이 앞에 있는 이유는 Tj 고착 칩의 온도 보정 피팅이 **그럴듯한 숫자를 낸 채로 틀리기** 때문이다. 먼저 막아야 "숫자가 나왔으니 맞겠지"가 안 나온다.

```text
  PASS chip=U1 verdict=PASS L25=0.163W fleet=0.163W r_L=1.00 r_Ccpu=1.00 r_Cidle=1.03 a=0.0249
  PASS chip=U2 verdict=PASS L25=0.156W fleet=0.163W r_L=0.96 r_Ccpu=0.98 r_Cidle=1.01 a=0.0246
  FAIL chip=U3 verdict=LEAKAGE_EXCESS L25=0.343W fleet=0.163W r_L=2.11 r_Ccpu=1.00 r_Cidle=1.00 a=0.0255
  PASS chip=U4 verdict=PASS L25=0.163W fleet=0.163W r_L=1.00 r_Ccpu=1.02 r_Cidle=0.98 a=0.0262
  FAIL chip=U5 verdict=IDLE_CEFF_HIGH L25=0.163W fleet=0.163W r_L=1.00 r_Ccpu=1.02 r_Cidle=3.12 a=0.0249
  FAIL chip=U6 verdict=SENSOR_GAIN L25=0.080W fleet=0.163W r_L=0.49 r_Ccpu=0.50 r_Cidle=0.50 a=0.0254
  BLOCKED chip=U7 reason=TJ_STUCK
  PASS chip=U8 verdict=PASS L25=0.155W fleet=0.163W r_L=0.95 r_Ccpu=1.01 r_Cidle=1.00 a=0.0243
PASS U2 outlier located exactly (cpu,85C,0.88V,1.7GHz) : [('cpu', 85, 0.88, 1.7, 0.3840305923764764)]
```

판정 논리는 **어느 조합이 어떤 원인을 가리키는가**다.

| 시그니처 | 판정 | 왜 그 원인인가 |
|---|---|---|
| r_L ≈ r_C(모두) ≠ 1 | SENSOR_GAIN | 실리콘 결함이 누설과 스위칭 용량을 정확히 같은 비율로 바꾸지 않는다 — 측정 사슬 쪽 |
| r_L ≫ 1, r_Ccpu ≈ 1 | LEAKAGE_EXCESS | 누설만 큼. 공정 코너(FF), 게이트 산화막/접합 누설 결함, 파워게이트 불량 |
| r_Cidle ≫ 1, r_Ccpu ≈ 1 | IDLE_CEFF_HIGH | idle에서만 활동량 증가 — 클럭 게이팅 결함 또는 펌웨어가 idle 진입 실패 |
| Tj 안 움직임 | BLOCKED | 데이터 무효. 실리콘 판정 대상이 아니다 |

U6의 r_L = 0.49는 "누설이 절반인 좋은 칩"으로 읽기 쉽다. r_C도 같이 0.50이라는 게 측정 오류의 지문이다. 실무에서 이 판정이 나오면 **칩을 보지 말고 먼저 보드를 본다**. Part C의 C 스크린에서 `GAIN ratio=0.500`이 같은 사건의 벤치 쪽 증상이다.

허용치의 근거: 양호 칩 복원 허용치(L 8%, C 1.5%)는 Part B S5에서 측정한 RMS(L 1.6%, C 0.17%)보다 L은 약 5배, C는 약 9배 넓게 잡아 칩별 잡음 실현값을 흡수한다. 칩별 참값은 `power_campaign_truth.json`에서 읽는다.

```text
PASS chip U1 L25@0.80V within 8% of truth : 0.1631 vs 0.1637
PASS chip U1 C_cpu within 1.5% of truth : 1.588 vs 1.587
```

**이상치와 잡음의 비균일.** 첫 시도에서 전체 잔차의 MAD로 8σ를 걸었더니 양호 칩에서 9개가 가짜로 잡혔다. 잡음이 전력에 비례해 커지기 때문이다(이득 잡음 0.2%, Tj 센서 잡음 × dL/dT). 뜨겁고 큰 점의 잔차가 전체 중앙값 기준으로는 커 보였다. **측정 전력으로 정렬한 4개 구간에서 구간별 MAD**로 바꿨더니 가짜가 0개가 되었고 U2의 글리치(잔차 +0.38 W)만 잡혔다.

```svg
<svg viewBox="0 0 760 170" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="14" y="20" fill="currentColor" font-weight="bold">Part C 판정 우선순위 (앞에서 걸리면 뒤 단계 해석 금지)</text>
  <g fill="none" stroke="currentColor">
    <rect x="14" y="46" width="130" height="48"/><rect x="164" y="46" width="130" height="48"/><rect x="314" y="46" width="130" height="48"/><rect x="464" y="46" width="130" height="48"/><rect x="614" y="46" width="132" height="48"/>
  </g>
  <rect x="14" y="46" width="130" height="48" class="f-warn" opacity=".4"/>
  <rect x="614" y="46" width="132" height="48" class="f-ok" opacity=".4"/>
  <text x="79" y="66" text-anchor="middle" fill="currentColor">데이터 관문</text><text x="79" y="84" text-anchor="middle" fill="currentColor">BLOCKED</text>
  <text x="229" y="66" text-anchor="middle" fill="currentColor">측정 이득</text><text x="229" y="84" text-anchor="middle" fill="currentColor">SENSOR_GAIN</text>
  <text x="379" y="66" text-anchor="middle" fill="currentColor">동적</text><text x="379" y="84" text-anchor="middle" fill="currentColor">IDLE_CEFF_HIGH</text>
  <text x="529" y="66" text-anchor="middle" fill="currentColor">누설</text><text x="529" y="84" text-anchor="middle" fill="currentColor">LEAKAGE_EXCESS</text>
  <text x="680" y="66" text-anchor="middle" fill="currentColor">양호</text><text x="680" y="84" text-anchor="middle" fill="currentColor">PASS</text>
  <g stroke="currentColor"><line x1="144" y1="70" x2="164" y2="70"/><line x1="294" y1="70" x2="314" y2="70"/><line x1="444" y1="70" x2="464" y2="70"/><line x1="594" y1="70" x2="614" y2="70"/></g>
  <text x="14" y="130" fill="currentColor">측정이 무효인 칩에서 '누설 과다' 같은 실리콘 판정을 내면, 디버그 담당이 엉뚱한 칩을 뜯는다.</text>
  <text x="14" y="150" fill="currentColor">fleet 중앙값 기준은 8개 칩이라 약하다. 양산에서는 코너 스플릿과 분포 기준이 필요하다.</text>
</svg>
```

**이 흐름이 놓치는 것.** fleet 중앙값 비교는 8개 전부가 같은 방향으로 틀리면(예: 같은 랙의 전원 공급 오프셋, 공통 shunt 로트 오류) 판정이 PASS다. 절대 기준(DMM, 교정 부하)을 칩 몇 개에는 반드시 넣어야 한다.

**실물로 옮길 때.** 합성 데이터를 실물로 바꿔도 분석 코드는 그대로다. 달라지는 것은 스윕 앞단이다. 챔버 soak 시간(SIV-M08), 전압 설정 후 안정화, 부하 정착 뒤 측정 시작, 측정 윈도우와 워크로드 주기의 동기화(짧으면 위상에 따라 평균이 달라진다), 측정 평균 시간 대 자기발열 시간상수, 전압 측정 지점(레일 설정값이 아니라 다이 쪽). 이 레슨의 판정 줄은 그 값들을 넣어 저장되는 캠페인 DB의 "한 행"이 된다.

## 6. 정답 코드

C 세트.

<!-- code: solution.c diff=starter.c -->

Python 세트.

<!-- code: solution.py diff=starter.py -->

## 7. 자주 하는 실수

**1. 전력 레지스터를 리틀엔디안으로 조립.** Cortex-M이 리틀엔디안이라 `memcpy`로 `uint16_t`에 넣으면 뒤집힌다. 0x7D00(32000)을 0x007D(125)로 읽어도 범위 검사를 통과할 수 있다(Part B의 1228/2000).
고침: `(uint16_t)((rx[0] << 8) | rx[1])`.

**2. `(uint32_t)power_raw * 25 * lsb_ua` 를 32비트로 계산.** 큰 LSB에서 넘친다. 최악값 65535 × 25 × 1 A = 약 1.6×10¹² µW.
고침: `(uint64_t)raw * 25u * lsb_ua`.

**3. CAL = 5.12e9 / (lsb × R) 를 32비트로 계산.** 5120000000은 `uint32_t`를 넘는다. 5.12e9 리터럴을 double로 계산하면 정수 전용 드라이버 규칙을 어기고 반올림도 맞추기 어렵다.
고침: `(5120000000ULL + den/2) / den`, den은 uint64_t.

**4. CAL 쓰기를 한 번만 하고 끝낸다.** 브라운아웃으로 POR이 일어나면 CAL이 0이 되어 전류가 조용히 0이 된다. shunt는 정상이라 전류만 틀리다.
고침: 측정 끝에 CAL을 다시 읽어 확인한다 (`V_CAL_LOST`).

**5. NAK 재시도에서 포인터를 다시 안 쓴다.** 포인터 바이트가 NAK으로 안 들어갔는데 데이터만 다시 읽으면 이전 레지스터를 읽는다. 값이 "그럴듯하게" 틀린다.
고침: 재시도는 트랜잭션 처음부터.

**6. Tj를 챔버 온도와 같다고 놓는다.** 저전력 코너에서만 맞다. 85 ℃에서 C_eff가 24% 커진다(Part B S4b).
고침: 측정 Tj를 공변량으로 쓰거나 전력을 짧은 펄스로 측정해 자기발열을 줄인다.

**7. 슬라이스 없이 온도별로 V를 섞어 회귀.** 누설의 V 의존이 V²f에 흡수되어 절편이 틀어진다(S3). DVFS 표만 찍으면 V당 f가 한 점이라 슬라이스 회귀도 불가능하다.
고침: 설계 단계에서 합법 영역의 비결합 점(같은 V, 다른 f)을 넣는다.

**8. κ가 작으면 맞다고 믿는다.** S3의 κ(16.7)는 S2(110)보다 작지만 편향은 훨씬 크다. κ는 모델 형태 오류를 재지 않는다.
고침: 잔차 패턴, 교차 검증, 설계 변경에 따른 안정성을 같이 본다.

**9. ln(L)을 무가중으로 회귀.** 저온의 잡음이 온도 계수를 끌고 간다 (측정 3배 차이). `dropped`를 무시해 음수 L을 그냥 지우면 그 온도의 정보를 잃는다.

**10. Power 레지스터만 믿는다.** Power는 내부 Current × Bus 계산 결과라 오버플로·포화 상태에서 같이 틀린다. 시험에서는 Shunt와 Bus를 독립적으로 읽어 교차 검증한다.

**11. 전역 MAD로 이상치 제거.** 잡음이 전력에 따라 달라 고전력 양호 점이 잘린다 (이 레슨의 첫 시도). 구간별로 시그마를 잡는다.

## 8. 면접 Q&A

<details><summary>Q1. INA226에서 Current_LSB를 어떻게 정하나? 작게 잡으면 더 정밀한가?</summary>

`Current_LSB ≥ Imax / 2¹⁵`를 넘는 깔끔한 값(1, 2, 5 시리즈)으로 잡고 CAL = 0.00512 / (Current_LSB × R)를 계산해 1..0x7FFF인지 확인한다. **작게 잡아도 더 정밀해지지 않는다.** Current는 2.5 µV ADC 코드에 CAL/2048 곱이므로 ADC 해상도를 넘는 정보는 없다. Current_LSB가 너무 작으면 CAL이 커져서 shunt 범위 안인데 Current가 오버플로한다(이 레슨에서 40 mV + CAL 5120로 재현).

꼬리질문: "ADC가 실제로 주는 해상도는?" → 2.5 µV. R = 10 mΩ면 전류로 250 µA이고, 평균을 늘리면 잡음은 줄지만 LSB 자체가 바뀌지는 않는다.
</details>

<details><summary>Q2. 누설과 동적 전력을 어떻게 분리하나?</summary>

**같은 T, V에서 f만 바꿔** P vs f를 직선으로 맞추면 절편이 누설, 기울기가 C_eff·V²이다. 여러 온도에서 `ln(절편)` vs T를 회귀해 온도 계수를 얻는다. 저온 점은 분산이 커서 가중 회귀(델타법)를 쓴다. 여러 워크로드는 절편을 공유하므로 역분산 가중으로 묶어 정밀도를 올린다.

꼬리질문: "DVFS 테이블로만 돌리면?" → V와 f가 한 곡선에 묶여 V당 f가 한 점뿐이라 슬라이스 회귀가 불가능하다. pooled로 풀면 누설의 V 의존이 V²f로 흡수돼 측정 시뮬레이션에서 누설이 −67% 틀어졌다. 합법 영역에서 V-f를 푸는 측정점을 추가해야 한다(과전압 저주파가 비결합 점). 꼬리2: "V_min 아래에서 찍을 수는 없나?" → 타이밍이 깨져 측정 자체가 무효다. 탐색 범위는 V ≥ V_min(f).
</details>

<details><summary>Q3. 측정한 누설이 스펙의 2배다. 실리콘 결함인가?</summary>

바로 칩을 의심하지 않는다. **동적 전력도 같은 비율로 틀어졌는지** 본다. 누설과 모든 워크로드 C_eff가 같은 비율로 벗어나면 측정 이득 문제(shunt 오실장, CAL, 잘못된 레일)이고, 누설만 크면 공정 코너(FF) 또는 누설 경로 결함이다. 코너 분포와 비교하고, 온도 계수 a가 동일 로트와 다른지, 전압 의존 b가 다른지도 본다(게이트 누설과 서브스레숄드 누설은 온도·전압 의존이 다르다). 그래도 이상하면 보드 다른 레일로의 누설 경로와 파워 게이트 결함을 분리하는 측정을 추가한다.

꼬리질문: "측정 이득을 어떻게 독립 확인하나?" → 벤치 DMM/전류 프로브로 한 점에서 교차 측정(이 레슨의 C Part C GAIN 스크린), 또는 정밀 전자부하로 알려진 전류를 흘린다.
</details>

<details><summary>Q4. 챔버 온도와 Tj의 차이를 어떻게 처리하나? Tj 센서를 어디까지 믿나?</summary>

자기발열 Tj = Ta + Rth·P 때문에 고전력 점일수록 뜨겁다. 측정 Tj를 공변량으로 쓴다(variable projection: a를 1차원 탐색, 안쪽은 선형 회귀). 수치로는 챔버 라벨만 쓰면 85 ℃에서 C_eff +24%, 온도 계수 a +10%였고 Tj로 푼 varpro는 그 편향을 없앴다.

꼬리질문: "Tj 센서 자체가 +5 ℃ 틀리면?" → **전력 데이터만으론 알 수 없다**. 모델에서 오프셋은 L25 스케일로 흡수된다(L25가 exp(−5a)배, 측정 −11.7%, 이론 −11.8%). 외부 기준이 필요하다. 전원 끈 뒤 충분히 soak해서 다이가 챔버와 같을 때 센서 판독을 교정하는 식이다.
</details>

<details><summary>Q5. 전력 측정이 몇 %까지 믿을 만한가? 이 흐름이 못 잡는 것은?</summary>

오차 예산으로 답한다. Shunt 저항 공차(1%급) + 온도계수, INA226 게인 오차(데이터시트 사양), CAL 반올림(이 레슨에서 측정), 전압 측정 지점과 다이 사이 IR drop(load-line), 측정 윈도우 동기 문제, 잡음은 평균으로 줄지만 **이득 오차는 평균으로 안 줄어든다**. 이 레슨의 2% GAIN 스크린은 실장 사고를 잡는 대표값이고, 1.5% 틀어진 shunt는 PASS로 통과했다(한계 테스트로 박아 둠).

놓치는 것: fleet 중앙값은 같은 방향의 공통 오류를 못 본다. Tj 센서 오프셋도 못 본다. 짧은 글리치는 평균/변환시간 안에 묻힌다. α 단독은 안 보인다(곱 C_eff만).

꼬리질문: "그럼 절대 정확도는 어떻게 확보하나?" → 4단자 정밀 부하/기준 저항으로 대표 레일을 교정하고, 그 교정 계수를 보드 ID별로 저장해 적용한다.
</details>

<details><summary>Q6. 데이터 포인트를 어떻게 설계하나? 측정 시간이 부족하면 무엇을 줄이나?</summary>

식별 가능성 먼저다. V당 f는 양 끝을 포함해 최소 3점 (2점이면 잡음·곡률을 못 본다, dof 0). 온도는 고온 쪽 비중을 높인다 (누설이 크고 분산이 작음; 저온은 절편 추정에 시간이 많이 든다). f 범위를 넓게 잡아 외삽 거리를 줄인다(1.9~2.0 GHz만 찍으면 누설 오차 36%). 시간이 없으면 전 조합(5×3×5)보다 **V-f 비결합 점 몇 개**를 지키고, 대표 온도와 코너부터 자른다.

꼬리질문: "측정이 추가된다면 어디에?" → 합법 영역 안에서 설계 행렬의 조건을 가장 줄이는 점. 이 레슨 P4에서 DVFS 표 위 5점(κ 16.7)에 V=0.88/f=0.8 점 하나를 더하면 κ 6.3이 됐다. 과전압 저주파가 V-f 결합을 끊는 점이기 때문이다.
</details>

<details><summary>Q7. I2C로 읽은 값이 전부 그럴듯하다. 드라이버는 무엇을 더 검증해야 하나?</summary>

ID 레지스터로 소자와 바이트 순서를 먼저 확정한다(값 범위 검사만으로는 스왑된 전류의 약 61%를 놓쳤다). CAL 쓰고 되읽기, 측정 끝에 CAL 재확인(브라운아웃), OVF/SAT 플래그 처리, 재시도 시 포인터까지 다시 쓰기. 그리고 벤치 기준과의 비교로 값을 닫는다. 시험 코드는 이걸 mock에 결함을 넣어 **각 결함이 올바른 이유로 FAIL 하는지**까지 확인한다.

꼬리질문: "버스가 다른 장치와 공유되면?" → 주소 충돌(0x40~0x4F 16개 주소). 프로브의 ID 불일치(V_ID)로 드러나고, 같은 주소의 다른 소자는 ID 레지스터가 다르다.
</details>

<details><summary>Q8. 자기발열 때문에 열폭주가 나면 측정을 어떻게 설계하나?</summary>

전력 P가 Tj를 올리고 Tj가 누설을 다시 올리는 양의 되먹임이다. 수렴 조건은 대략 `Rth × a × Pleak < 1`이다. 이 레슨의 데이터 생성기도 Rth = 5 ℃/W, a = 0.03일 때 실제로 발산해서 Rth와 a를 낮췄다. 측정 설계로는 짧은 전력 펄스(열 시간상수 안에서 측정), 열 헤드로 Tj 고정, 고온·고전압 코너에서 쿨다운과 전력 상한 인터록을 건다. 스크립트에도 Tj 상한 인터록을 넣어 측정이 칩을 태우지 않게 한다.

꼬리질문: "시간상수를 어떻게 아나?" → 전력 스텝을 주고 Tj 응답을 측정한다. 다이는 수~수십 ms, 패키지·보드는 수 초~수십 초 대역의 다중 시간상수다(대표적인 값이고 시스템마다 다르다).
</details>

## 9. 추가 연습

**P1. Configuration 인코딩과 샘플 주기.** AVG/VBUSCT/VSHCT/MODE 필드를 합쳐 0x4127을 재현하고, 평균까지 끝난 샘플 하나의 주기를 계산하라.

<details><summary>답</summary>

`(VBUSCT + VSHCT) × AVG` (연속, MODE 7). AVG 필드(000=1, 001=4, 010=16, 011=64, 100=128, 101=256, 110=512, 111=1024). 변환 시간 필드(000=140 µs, 001=204, 010=332, 011=588, 100=1.1 ms, 101=2.116, 110=4.156, 111=8.244). 측정 기준: POR 기본값 2200 µs. AVG 64 + 8.244 ms × 2이면 1.055 s로, 측정 창이 그만큼 길어져 온도·부하 변화가 평균 안에 섞인다.
</details>

**P2. 에너지 적산.** 샘플마다 `P × dt`를 더하되 절사가 누적되지 않게 한다.

<details><summary>답</summary>

나머지를 µW·µs 단위로 이월한다. 시험에서 1.5 W × 1 µs를 1000번 더하면 정확히 1500 µJ이고, 샘플마다 µJ로 절사하면 1000 µJ이다(33% 손실). 긴 전력 로그 적산에서 흔한 버그.
</details>

**P3. Current_LSB 선택.** `{1,2,5}×10^k` 단계에서 `Imax/32768` 이상이면서 CAL이 1..0x7FFF인 최소값을 찾는다.

<details><summary>답</summary>

표: (15 A, 2 mΩ) → 500 µA, (5 A, 10 mΩ) → 200 µA, (1 mA, 100 mΩ) → CAL을 맞추려고 2 µA까지 올라감, (40 A, 1 mΩ) → 2000 µA. "작을수록 좋다"가 아니라 CAL 범위가 하한을 건다.
</details>

**P4. DVFS 표에 측정점 하나 더 넣기.** 합법 영역(V ≥ V_min(f))에서 스케일 조건수를 가장 줄이는 점을 탐욕으로 고른다.

<details><summary>답</summary>

측정 결과: 기준 κ = 16.7, V=0.88/f=0.8을 더하면 **κ = 6.3**. 과전압 저주파 점이 V-f 결합을 끊는다. V_min 제약을 빼면 탐욕 설계가 물리적으로 불가능한 저전압 고주파 점을 고르게 된다. 설계가 물리적 제약을 포함해야 한다는 요점이다.
</details>

**P5. 잔차 붓스트랩과 해석해 비교.** 슬라이스 잔차를 풀링해 재표집한 표준오차가 해석적 SE와 맞는가?

<details><summary>답</summary>

5개 시드에서 비율 0.93~0.96. 잔차를 **풀링**해야 한다. 슬라이스 하나의 잔차 5개(dof 3)로 재표집하면 잡음 크기 자체가 실제보다 작게 나온다. 풀링했는데도 비율이 1보다 약간 작은 것은 잔차가 적합값에 의해 약간 줄어든 탓(leverage)이며, leverage 보정이 표준 처방이다.
</details>

**P6. Load-line.** V_die = V_set − I·R_LL로 처지는 슬라이스에서 V_set을 쓰면 C_eff가 얼마나 틀어지나?

<details><summary>답</summary>

R_LL = 15 mΩ(대표값), V_set = 0.88 V에서 측정: V_set 사용 시 C_eff 1.367 (−15%), V_die 사용 시 1.572 (−1.8%). 고주파일수록 전류가 커서 V가 처지므로, 그 V를 쓰지 않으면 기울기가 깎인다. 실물에서는 **Kelvin 센스 위치와 다이 쪽 전압 측정 지점**이 이 항을 정한다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

<!-- code: practice.py -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/SIV-A03_pr && /tmp/SIV-A03_pr   # 14/14 passed
python3 -I practice.py   # 6/6 passed
```

## 10. 핵심 정리

- INA226: 포인터 레지스터 + 16비트 빅엔디안. Shunt 2.5 µV(부호), Bus 1.25 mV, Power LSB = 25 × Current_LSB, CAL = 0.00512/(Current_LSB·R). POR 뒤 CAL은 0이라 current가 0이다.
- CAL은 표시 이득이지 해상도가 아니다. 크게 잡으면 shunt 범위 안에서 current가 먼저 오버플로한다.
- 값 범위 검사는 바이트 스왑의 약 61%를 놓친다 (측정). ID 고정 패턴을 먼저 본다. CAL은 되읽고, 측정 끝에 다시 확인한다(브라운아웃).
- 분리는 **같은 T, V 안에서** f로 회귀한다. V를 섞은 pooled 회귀는 누설을 −67% 틀리게 한다.
- 조건수는 공선성의 지표이지 모델 오류 검출기가 아니다. DVFS 표 위 점만으로는 분리가 안 되고 설계로만 고친다.
- 자기발열을 무시하면 85 ℃에서 C_eff +24%. Tj 공변량(variable projection)으로 편향을 지운다. Tj 센서 오프셋은 전력만으로 구분 불가(L25에 흡수), 외부 기준이 필요하다.
- 측정 이득 오류의 지문은 **누설과 모든 C_eff가 같은 비율로 벗어남**. 관문(BLOCKED)을 실리콘 판정보다 앞에 둔다.
- 잡음이 전력에 따라 달라지면 이상치 시그마는 구간별로 잡는다.

## 참고자료

- Texas Instruments, *INA226 High-Side or Low-Side Measurement, Bi-Directional Current and Power Monitor with I2C Compatible Interface* (SBOS547) — 레지스터 맵, CAL 식, 변환 시간/평균 표
- Linux kernel `drivers/hwmon/ina2xx.c` — 실제 드라이버의 Current_LSB·CAL 처리 (참고용)
- Golub & Van Loan, *Matrix Computations* — MGS/QR, one-sided Jacobi (Part A 수치 방법)
- Kaxiras & Martonosi, *Computer Architecture Techniques for Power-Efficiency* — C·V²·f, 누설 온도 의존 개요
- 이 트랙: SIV-M07 (Vmin 탐색), SIV-M08 (PVT 스케줄·soak), SIV-M12 (시퀀서), SIV-A06 (보드 전원 시퀀싱·ppm), SIV-A04 (열/스로틀)
