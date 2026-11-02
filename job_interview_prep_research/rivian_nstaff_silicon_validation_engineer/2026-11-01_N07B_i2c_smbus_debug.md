# N07B · I2C / SMBus 디버그 — "The I2C sensor NACKs. Debug it."
<!-- theme: pastel -->
> **묶음**: N07 인터페이스 디버그 · **우선**: P0 ★ · **목록**: [N07 인덱스](2026-11-01_N07_interface_debug_scenarios.html) · **이전/다음**: [N07A 디버그 틀, 계측 장비](2026-11-01_N07A_debug_framework_instruments.html) / [N07C SPI, QSPI 디버그](2026-11-01_N07C_spi_qspi_debug.html) · **관련**: [BV13 S11 I2C NACK](2026-11-01_BV13_board_debug_scenarios.html), [BV10 §2.1, 체인 B, §6.1 stuck bus](2026-11-01_BV10_board_interface_tests.html), [N06C C1 I2C 테스트 플랜](2026-11-01_N06C_plans_peripherals_soc_infra.html), [V8 I2C scan, recover](hmScreenPrep/problems/V8_i2c_scan_recover/index.html), [최종 훑어보기 §5](2026-10-30_final_skim.html), [BV11 랩 장비](2026-11-01_BV11_lab_instruments_measurement.html), [N06H 디버그 방법론](2026-11-01_N06H_debug_methodology_triage.html) · **코드**: [onsitePrep/n07/b/i2c_capture_decode.c](onsitePrep/n07/b/i2c_capture_decode.c) + [gen_captures.py](onsitePrep/n07/b/gen_captures.py) + [run.sh](onsitePrep/n07/b/run.sh) (실행 출력 [run_output.txt](onsitePrep/n07/b/run_output.txt))

## 0. TL;DR

- **이 페이지로 답할 것**: "I2C 센서가 NACK / 400 kHz에서만 깨짐 / 레지스터 값이 엉뚱 / 단일 master인데 arbitration lost / 카메라 끄면 버스 전체 다운 / clock stretching timeout / 고온에서만 NACK" — 8개 시나리오를 **계층(전기 → 타이밍 → 프로토콜 → 설정·드라이버 → 시스템)** 순서로 좁히는 법.
- 기초(풀업 계산·9-clock 복구·NACK 기본)는 [BV10 §2.1](2026-11-01_BV10_board_interface_tests.html)·[BV13 S11](2026-11-01_BV13_board_debug_scenarios.html)·[N06C C1](2026-11-01_N06C_plans_peripherals_soc_infra.html)에 있다. 여기는 **증상 카탈로그 + 파형 읽기 + 새 시나리오 6개 + 캡처 디코더**.
- **외울 3개**
  1. **Fm 400 kHz 예산은 여유 0**: tLOW 1.3 + tHIGH 0.6 + tr 0.3 + tf 0.3 = **2.5 µs = 주기**. tr은 **30→70 %**로 재고, `tr = 0.8473·Rp·Cb` [1].
  2. **NACK은 어디서 났나가 절반**: 주소 바이트의 9번째 클럭(장치 없음·전원·주소) vs 데이터 k번째 바이트(장치가 바쁨·명령 거부·끝). UM10204는 NACK 조건 5개를 정의한다 [1]. Linux: 주소 단계 NACK = `-ENXIO` [3], 많은 드라이버(DesignWare)는 NACK 전반에 `-EREMOTEIO` [4].
  3. **"버스가 이상"하면 idle 레벨부터**: SDA low + SCL high = target이 바이트 중간 → 9 clock + STOP. SCL low = 클럭으로 못 풂 → target 리셋/전원 [1]. **0 V도 VDD도 아닌 0.5–0.7 V**면 전원 꺼진 장치로의 back-power를 의심.
- **30초 영어 답 (헤드라인)**: "First I split the NACK by where it happens. I put a scope with I2C decode near the device and check that the address byte on the wire is what we expect — seven-bit versus eight-bit is the classic mix-up — and whether the NACK is on the address or on a data byte. Address NACK with the right address means the device isn't there electrically: power, reset, address straps, or the bus itself, so I check idle levels and the 30-to-70 percent rise time against the mode limit. If it only fails at 400 kHz, it's usually rise time eating the setup margin. Then I make it a regression: an expected-address scan per board revision and a rise-time measurement at the bus-load corner."

## 1. 🔰 1분 설명

- **비유**: 교실에 깔린 **공용 줄 두 가닥**. 줄은 고무줄(풀업 저항)이 늘 위로 당겨 두고, 누구든 **아래로 당길 수만** 있다(open-drain). 한 줄(SCL)은 박자, 한 줄(SDA)은 말. 선생님(controller)이 "48번!"(주소)을 말하면 48번 학생(target)만 9번째 박자에 줄을 당겨 "네"(ACK). 고무줄이 약하면(풀업 큼) 줄이 **천천히** 올라가서, 빠른 박자(400 kHz)에서는 "위"로 다 올라가기 전에 박자가 와 버린다 — 이게 B3.
- **정확한 정의**: I2C = 2선(SCL, SDA) 양방향 직렬 버스, 두 선 모두 open-drain/open-collector + 외부 풀업 → 선 레벨 = 모든 장치 출력의 **wired-AND**. 속도 모드 Sm 100 kHz · Fm 400 kHz · Fm+ 1 MHz (양방향) + Hs 3.4 Mbit/s · UFm 5 Mbit/s [1]. UM10204 Rev.7부터 master/slave 대신 **controller/target** [1].
- **SMBus** = I2C의 친척(시스템 관리 버스): 같은 2선 전기 구조에 **최소 클럭 10 kHz, clock-low timeout 25–35 ms, 누적 stretch 한도, PEC(CRC-8)** 같은 규칙을 더한 것 [2]. PMIC·배터리·온도 센서·VR 컨트롤러 계열이 흔히 SMBus/PMBus.

```text
              SoC                                                                장치들 (target)
  ┌───────────────────────────┐                VDD_IO(예: 1.8 V)        ┌──────────────┐
  │ I2C controller IP         │                  │      │               │ 센서 0x48     │
  │  STATUS: NACK·ARB_LOST·    │                 Rp     Rp  ← 풀업 (보드)  └──────────────┘
  │          TIMEOUT·BUSY      │                  │      │               ┌──────────────┐
  │  FIFO · SCL HCNT/LCNT     │   pinmux/pad      │      │               │ EEPROM 0x50  │
  │  spike filter (tSP 50 ns) ├── open-drain ────┼─SCL──┼────────┬──────┤ (A0–A2 strap)│
  │                           ├── open-drain ────┼─SDA──┴────────┼──┬───┤              │
  └───────────────────────────┘                  │               │  │   └──────────────┘
       ▲ GPIO 모드 전환 (bus recovery용 scl-gpios/sda-gpios)       │  │   ┌──────────────┐
                                                                 │  └───┤ 레벨 시프터   ├── 3.3 V 쪽 Rp ── 카메라 모듈 (별도 전원!)
          Cb = 핀 용량 합 + 배선 + 커넥터 + 프로브 (≤ 400 pF Sm/Fm) │      └──────────────┘
                                                                 └── 테스트 포인트: scope SCL/SDA (수신단 가까이)
```

| 용어 | 풀네임 | 쉬운 설명 | 디버그에서 왜 중요 |
|---|---|---|---|
| open-drain | — | 당기기만 가능, 올리는 건 풀업 | "놓았다" ≠ "high다" → stretching·stuck의 근원 |
| Rp / Cb | pull-up resistor / bus capacitance | 고무줄 세기 / 줄 무게 | tr = 0.8473·Rp·Cb → B3 |
| tr / tf | rise / fall time | 30→70 % / 70→30 % 걸리는 시간 | scope 기본 10–90 %로 재면 틀림 |
| ACK / NACK | acknowledge | 9번째 클럭에 SDA low / high | 어느 바이트의 NACK인지가 첫 분기 |
| S / Sr / P | START / repeated START / STOP | SCL high 중 SDA ↓ / 다시 ↓ / ↑ | Sr 대신 P면 포인터 리셋 장치 → B4 |
| clock stretching | — | target이 SCL을 low로 붙잡아 대기 | controller timeout과 충돌 → B7 |
| arbitration | — | 여러 controller가 비트 단위로 승부, "1 보냈는데 0"이면 진 것 | 단일 master에서 나오면 글리치·숨은 master → B5 |
| tSP | spike suppression | 50 ns 미만 펄스는 입력 필터가 무시 (Fm/Fm+) | 50 ns 넘는 글리치는 **클럭으로 세어짐** |
| bus clear | — | SDA stuck → 9 클럭, SCL stuck → HW reset/전원 | UM10204 §3.1.16 [1] |
| tTIMEOUT | SMBus clock-low timeout | SCL low 25 ms 넘으면 모두 리셋 가능, 35 ms까지는 반드시 | SMBus 장치의 자동 복구 근거 [2] |
| PEC | Packet Error Code | 트랜잭션 끝 CRC-8 (x⁸+x²+x+1) | Linux `-EBADMSG` [2][3] |
| back-power | — | 전원 꺼진 장치의 ESD 다이오드로 버스 전류가 흘러 들어감 | 버스가 0.5–0.7 V에 붙음 → B6 |

## 2. 정상 기준선

### 2.1 정상 레지스터 읽기 파형 (combined format)

```text
       START                 9th clk                 9th clk  Sr                    9th clk                   9th clk STOP
 SCL ‾‾‾‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_/‾\_ ... _/‾\_/‾\_ ‾‾\_/‾\_ ... _/‾\_/‾\_ ... _/‾\_/‾‾‾‾‾
 SDA ‾‾\__< 1  0  0  1  0  0  0  W >[A]  < 0x0F >[A] _/‾\_< 0x48 + R >[A] < data 0xB5 >[N]_/‾‾‾
         S └─ 7-bit 0x48 ─┘ W=0    ↑target       ↑target  Sr                ↑target   ↑controller NACK (마지막 바이트)
                                    pulls low                                         → 그 뒤 STOP

  규칙: 데이터는 SCL low일 때만 바뀐다. SCL high 중 SDA가 바뀌면 그건 START(↓) 또는 STOP(↑) — 글리치도 그렇게 보인다.
  combined format: 방향이 바뀔 때 START+주소를 반복(Sr), controller-receiver는 Sr 직전에 NACK [1]
```

### 2.2 정상 edge vs 느린 RC edge (같은 400 kHz 설정)

```text
 정상: Rp 2.2 kΩ 급, Cb ≈ 55 pF → tr ≈ 0.10 µs           느림: Rp 4.7 kΩ, Cb 175 pF → tr = 0.8473 × 4.7k × 175p ≈ 0.70 µs
 VDD ─────  ┌──────────                                  VDD ─────            ___-------
 0.7 ······ │· · · · ·  ← 70 % 금방 통과                 0.7 ················/· · · · ·   ← 70 %에 늦게 도착
 0.3 ······ │· · · · ·                                   0.3 ··········__/‾‾‾ · · · ·
 0   ───────┘                                            0   ─────────/
            ↑ release                                               ↑ release      |←── 0.70 µs ──→|
  falling edge는 드라이버가 당기므로 둘 다 빠름(수십 ns). "RC 모양 상승 + 날카로운 하강" = open-drain의 정상 모양.
  상승도 날카로운 사각형이면 → 누군가 push-pull로 high를 "구동"하는 중 (pad 설정 오류, 경합 의심).
```

### 2.3 타이밍 숫자 — UM10204 Rev.7.0 Table 10·11 [1] (PDF 텍스트로 직접 확인)

| 파라미터 | Sm 100 kHz | Fm 400 kHz | Fm+ 1 MHz | 디버그 메모 |
|---|---|---|---|---|
| fSCL max | 100 kHz | 400 kHz | 1000 kHz | 주파수는 맞아도 tLOW가 틀릴 수 있다 |
| tLOW min | 4.7 µs | 1.3 µs | 0.5 µs | 50:50 duty Fm = 1.25 µs → **위반** |
| tHIGH min | 4.0 µs | 0.6 µs | 0.26 µs | clock sync controller는 70 %를 본 뒤부터 셈 |
| tHD;STA min | 4.0 µs | 0.6 µs | 0.26 µs | START → 첫 클럭 |
| tSU;STA min | 4.7 µs | 0.6 µs | 0.26 µs | Sr setup |
| tSU;DAT min | 250 ns | 100 ns | 50 ns | B3에서 음수가 됨 |
| tHD;DAT min | 0 (I2C 장치) | 0 | 0 | 단 SDA는 SCL이 30 % 아래로 떨어진 뒤 바뀌어야 (note 2) |
| tr max | 1000 ns | 300 ns (min 20) | 120 ns | **30→70 %** 기준 |
| tf max | 300 ns | 300 ns | 120 ns | Fm min = 20 ns × VDD/5.5 V |
| tSU;STO min | 4.0 µs | 0.6 µs | 0.26 µs | |
| tBUF min | 4.7 µs | 1.3 µs | 0.5 µs | STOP → 다음 START |
| tVD;DAT, tVD;ACK max | 3.45 µs | 0.9 µs | 0.45 µs | target 출력 지연 상한 |
| Cb max | 400 pF | 400 pF | 550 pF | 라인당 |
| tSP (Table 10) | — | 0–50 ns | 0–50 ns | 50 ns 미만 spike 억제 |
| VIL / VIH | 0.3 VDD / 0.7 VDD | 같음 | 같음 | 모든 타이밍의 기준 레벨 |
| VOL @ 3 mA (VDD > 2 V) | ≤ 0.4 V | ≤ 0.4 V | ≤ 0.4 V | Fm+는 IOL 20 mA |

- **Rp 한계** [1]: `Rp(max) = tr(max) / (0.8473·Cb)`, `Rp(min) = (VDD − VOL(max)) / IOL`. 0.8473은 RC 충전이 30 %→70 %를 지나는 시간(1.2040·RC − 0.3567·RC). 표 예제는 [BV10 §2.1.1](2026-11-01_BV10_board_interface_tests.html).
- **전원 꺼진 장치** [1]: Table 10 note 9 — **Fm/Fm+ 장치는 VDD가 꺼져도 SDA/SCL을 막으면 안 된다**. Sm 장치에는 이 요구가 없다 → B6의 근거.
- **SMBus 3.1 Table 2** [2]: fSMB 10–100 kHz(100 kHz class; 400 kHz·1 MHz class도 min 10 kHz), **tTIMEOUT 25–35 ms**, tLOW:SEXT(target 누적 stretch, START→STOP) ≤ 25 ms, tLOW:MEXT(controller, 바이트당) ≤ 10 ms, tHIGH max 50 µs (이보다 오래 둘 다 high면 bus idle로 간주), tPOR 500 ms.

### 2.4 정상 로그·도구 출력

```text
[예시 출력] (보드 없음 — 형식 학습용)
$ i2cdetect -l
i2c-2   i2c   Synopsys DesignWare I2C adapter   I2C adapter
$ i2cdetect -F 2
Functionalities implemented by /dev/i2c-2:
I2C                              yes          ← 임의 메시지(=Sr 가능)
SMBus Quick Command              yes
SMBus Read Byte With Command     yes
SMBus PEC                        no           ← PEC가 필요한 장치면 여기서 막힘
$ i2ctransfer -y 2 w1@0x48 0x0f r1          ← 한 transfer = S w Sr r P
0xb5
$ dmesg | grep -i i2c                          ← 정상이면 probe 성공 줄만
```

- **실제로 돌린 정상 기준선**: 이 페이지 디코더의 golden 캡처 (§8) — `decode: S 48W A 0F A Sr 48R A B5 N P`, fSCL 397 kHz, tr 120 ns, tSU;DAT 555 ns, 위반 0. **고장 캡처는 전부 이것과 diff해서 읽는다.**

## 3. 관찰 지점 지도

| 무엇 | 어디서 | 도구 | 정상값 | 이상하면 의미 |
|---|---|---|---|---|
| idle 레벨 SCL/SDA | 수신단 가까운 TP | DMM / scope DC | 둘 다 ≈ VDD_IO | SDA 0 V = stuck target · 둘 다 0 V = 풀업 전원 없음 · 0.5–0.7 V = back-power · VDD보다 낮은 high = 다른 도메인 풀업 |
| low 레벨 (VOL) | 같은 TP, ACK 중 | scope | ≤ 0.4 V @ 3 mA | 높으면 풀업 너무 셈(Rp < Rp(min)) 또는 직렬 저항·약한 드라이버 → 노이즈 마진 부족 |
| 상승 시간 30→70 % | SCL, SDA 각각 | scope (측정 레벨 30/70로 변경) | Fm ≤ 300 ns | 초과 → Rp/Cb (B3) · 계단 모양 = 레벨 시프터 one-shot · 사각형 = push-pull 경합 |
| tLOW / tHIGH / fSCL | SCL | scope 측정 통계 또는 analyzer timing | Table 11 | tLOW 부족 = HCNT/LCNT 계산 · fSCL 낮음 = clock sync가 느린 edge를 기다림 |
| tSU;DAT / 9번째 비트 | SDA vs SCL rising | scope 2ch, setup/hold 트리거 | ≥ 100 ns (Fm) | 음수/30–70 % 사이 샘플 = 비트 값이 수신기마다 다름 (B3) |
| 주소 바이트·ACK 위치 | 와이어 | protocol analyzer / scope decode | 기대 7-bit 주소 + A | 주소 다름 = SW · 주소 맞고 N = 장치/전기 |
| 글리치 | SCL/SDA | scope glitch/pulse-width 트리거 (< 100 ns) | 없음 | ≥ 50 ns 글리치 = 수신기가 셈 → 비트 밀림 (B5) |
| clock stretching 길이 | SCL low 구간 | scope 긴 시간축 + pulse-width 트리거 (> 1 ms) | 데이터시트 max 이내 | controller timeout보다 길면 B7 · SMBus면 25 ms 한도 |
| controller 상태 | NACK·arbitration lost·timeout·bus busy·FIFO level 비트 (개념명) | 드라이버 dev_dbg, devmem 덤프 | 에러 0, idle에서 busy=0 | 단일 master에서 ARB_LOST = B5 · busy 고정 = stuck |
| 드라이버 에러 코드 | dmesg / 반환값 | dmesg | 없음 | -6 ENXIO 주소 NACK · -121 EREMOTEIO NACK(DesignWare 등) · -11 EAGAIN arbitration · -110 ETIMEDOUT · -16 EBUSY recovery 실패 · -74 EBADMSG PEC [3][4] |
| 주소 맵 | 버스 전체 | `i2cdetect -y N` | 기대 표와 일치, UU는 드라이버 점유 | 빠진 주소 = 전원/리셋/strap · 모든 주소 응답 = SDA stuck low (ACK처럼 보임) |
| 트랜잭션 로그 | 커널 | i2c tracepoint (`events/i2c`) [추정 — 이벤트 이름 i2c_write/i2c_read/i2c_reply/i2c_result 기억] | 메시지·결과 순서 | Sr 대신 두 transfer로 갈라짐(B4) · 재시도 패턴 |
| 장치 전원·리셋·strap | 장치 VDD, RESET, A0–A2 | DMM / scope | 정격, RESET 해제, 회로도대로 | 전원 off·리셋 중·주소 다름 |
| 온도·시간 상관 | NACK 카운터 vs 챔버 온도 | 로그 + 챔버 | 평탄 | 온도 따라 증가 = 마진(B8) |

**첫 5분 증거 스냅샷** (순서대로, 보드 하나에서)

```bash
# 0) 버스 번호·기능: 이 adapter가 임의 메시지(Sr)·PEC를 지원하나
i2cdetect -l ; i2cdetect -F 2
# 1) 커널이 이미 본 에러: errno로 계층 1차 분류
dmesg | grep -iE 'i2c|timed out|arbitration|recover'
# 2) idle 레벨 (DMM 또는 scope DC): SCL, SDA, 그리고 장치 쪽 VDD
# 3) 주소 맵을 기대 표와 diff (probe 방식 주의: -r은 쓰기 전용 칩을 잠글 수 있음 [8])
i2cdetect -y 2
# 4) 문제 장치 하나에 Sr 포함 최소 트랜잭션 (드라이버 없이)
i2ctransfer -y 2 w1@0x48 0x0f r1
# 5) 그 트랜잭션을 scope로 캡처: 30/70 % tr, tLOW/tHIGH, 9번째 클럭, VOL, 글리치
# 6) controller 상태 레지스터 (NACK / arbitration lost / timeout / busy / FIFO) 덤프, 드라이버 dev_dbg on
# 7) 커널 tracepoint로 메시지 순서 확인 [추정]
echo 1 > /sys/kernel/tracing/events/i2c/enable ; cat /sys/kernel/tracing/trace_pipe
```

## 4. 증상 → 원인 한 장 표

| 증상 | 계층 | 의심 원인 (순위) | 첫 확인 | 확정 방법 |
|---|---|---|---|---|
| 주소 NACK (`-ENXIO`, i2cdetect `--`) | 설정 → 전기 | ① 7/8-bit 표기·버스 번호 ② 장치 전원/리셋 ③ strap ④ 풀업/stuck | 와이어 위 주소 decode | 다른 주소 스캔, VDD/RESET 측정 → B1 |
| 데이터 k번째 바이트에서 NACK | 프로토콜 | ① 장치 바쁨(변환·EEPROM write cycle) ② 지원 안 하는 레지스터/명령 ③ 버퍼 끝 | NACK 위치·직전 명령 | 지연 후 재시도로 성공하면 ①, 데이터시트 명령표 |
| SDA low stuck (SCL high) | 프로토콜 상태 | ① 리셋/halt로 target이 바이트 중간 ② 쓰기 중 abort ③ 단락 | idle 레벨 | 9 clock + STOP으로 풀리면 ①, 안 풀리면 무전원 저항 → B2 |
| SCL low stuck | 전기/장치 | ① target이 SCL 붙잡음(펌웨어 hang) ② 단락 ③ 전원 꺼진 장치 | 어느 장치 분리 시 풀리나 | 구간 분리(0 Ω 제거), target 리셋/전원 사이클 |
| 100 kHz OK, 400 kHz만 간헐 실패 | 타이밍/전기 | ① tr 초과(Rp·Cb) ② tLOW 부족(LCNT) ③ target tVD + 느린 edge | 30/70 % tr | 풀업 임시 강화 시 사라짐 → B3 |
| 항상 0xFF만 읽힘 | 프로토콜/설정 | ① target이 데이터 구동 안 함(잘못된 레지스터·리셋 중) ② 레벨 시프터 방향 ③ 다른 장치가 응답 | ACK는 오나 | analyzer로 SDA가 데이터 구간에 low로 가는지 |
| 모든 주소가 ACK (i2cdetect 전부 숫자) | 전기 | ① SDA stuck low가 ACK처럼 보임 ② 풀업 없음 + 잔류 | idle SDA | SDA 레벨 0 V면 stuck → B2 |
| 첫 바이트만 맞고 나머지 0xFF | 프로토콜/설정 | ① controller가 첫 바이트 뒤 NACK(길이 설정) ② 장치 auto-increment 미지원/비트 필요 ③ 페이지 경계 | 첫 바이트 뒤 ACK/NACK | analyzer, 데이터시트 burst 규칙 |
| 레지스터 값이 매번 다른 레지스터 것 | 설정·드라이버 | ① Sr 대신 STOP+START(포인터 리셋) ② 다른 agent가 사이에 끼어듦 ③ auto-increment 포인터 | 트랜잭션 사이 P 유무 | `i2ctransfer` 한 transfer로 재현 → B4 |
| 단일 master인데 arbitration lost (`-EAGAIN`) | 전기/시스템 | ① SCL/SDA 글리치 ≥ tSP ② 숨은 두 번째 master(BMC·다른 코어·지그) ③ 느린 rise로 controller가 0을 봄 | 캡처에 다른 클럭/START 있나 | glitch 트리거, 다른 agent 끄기 → B5 |
| 같은 주소 장치 2개 (주소 충돌) | 설정/보드 | ① strap 동일 ② 대체 BOM 부품 기본 주소 동일 ③ mux 채널 둘 다 열림 | 읽기 데이터가 깨짐(wired-AND) | 하나씩 분리, mux 상태 확인 |
| 레벨 시프터 넘어가면 NACK | 전기 | ① 한쪽 풀업 없음/전압 틀림 ② enable/OE 미설정 ③ 자동방향 시프터 + 너무 센 풀업 | 양쪽 파형 동시 | 시프터 우회(점퍼) 테스트 |
| 특정 모듈 전원 off면 버스 전체 다운 | 전기 | ① back-power(ESD 다이오드) ② Ioff 미지원 Sm 장치 ③ 시프터 전원 순서 | idle 레벨 0.5–0.7 V | 모듈 커넥터 분리 시 회복 → B6 |
| 온도/진동에서만 간헐 NACK | 타이밍/기계 | ① 마진(tVD↑, 누설↑) ② 장치 바쁨 시간 증가 ③ 커넥터/솔더 간헐 | NACK 위치·온도 상관 | 챔버 스윕 + segmented 캡처 → B8 |
| SMBus timeout (`-ETIMEDOUT`) | 프로토콜/장치 | ① target stretch > 25 ms ② controller timeout 설정 짧음 ③ target hang | SCL low 길이 | pulse-width 트리거 → B7 |
| PEC 에러 (`-EBADMSG`) | 프로토콜/전기 | ① 비트 오류(마진) ② PEC 계산 범위(주소 바이트 포함) ③ PEC on/off 불일치 | 와이어 바이트로 CRC-8 재계산 | analyzer 바이트로 PEC 직접 계산 [2] |
| 특정 센서에서만 데이터 깨짐 (stretch 장치) | 설정·드라이버 | ① controller/bit-bang이 SCL 재확인 안 함 ② 비트 단위 stretch 미지원 | stretch 위치 | analyzer: SCL low가 늘어난 구간에서 controller가 SDA를 바꾸나 |
| 부팅 직후 첫 트랜잭션만 실패 | 시스템 | ① 이전 리셋의 stuck target ② 장치 POR 시간 전 접근(SMBus tPOR 500 ms) ③ 풀업 rail 늦게 뜸 | 부팅 시 idle 레벨 | 부팅 첫 단계 bus clear 추가 후 재현율 |
| ACK는 오는데 쓰기가 반영 안 됨 | 설정 | ① write-protect 핀 ② EEPROM 페이지 wrap ③ write cycle 중 다음 접근 | 다시 읽기 | WP 레벨, 페이지 크기·ACK polling |
| 바이트 사이 gap 크고 throughput 낮음 | 시스템/드라이버 | ① 바이트마다 인터럽트·ISR 지연 ② FIFO threshold ③ 장치 stretch | SCL 공백 위치 | DMA/FIFO 설정 변경 전후 비교 |
| 400 kHz로 설정했는데 analyzer는 300 kHz대 | 타이밍 | ① clock sync가 느린 rise를 기다림 ② 입력 클럭 다름 ③ HCNT/LCNT 계산 | tr 측정 | tr 줄이면 fSCL 회복 (§8 B3: 299 kHz) |

## 5. 시나리오 드릴

### B1 · 주소 NACK — 장치가 응답하지 않음 (요약+링크)

- **증상**: `i2cdetect`에 0x50이 `--`, `at24 3-0050: probe ... failed with error -6`.
- **3줄 답**: ① 와이어 위 주소가 맞나(7-bit 0x50 = 쓰기 바이트 0xA0, 버스 번호) → 틀리면 SW. ② 맞는데 NACK → 장치 전원·리셋·strap·대체 부품 주소. ③ 전기: idle 레벨, 30/70 % tr, 레벨 시프터 양쪽.
- **여기서 새로 추가하는 것**:
  - UM10204의 NACK 조건 5개 [1]: (1) 주소에 receiver 없음 (2) receiver가 실시간 작업 중이라 준비 안 됨 (3) 이해 못 하는 데이터/명령 (4) 더 받을 수 없음 (5) controller-receiver가 전송 끝을 알림. → **(2)는 "장치가 있는데 바빠서 NACK"** — EEPROM write cycle 동안의 NACK가 대표이고, 이걸 이용하는 게 ACK polling. 주소 NACK = 무조건 "장치 없음"이 아니다.
  - errno로 계층 1차 분류: 주소 단계 NACK는 `-ENXIO`가 커널 규약 [3], DesignWare 드라이버는 NOACK abort 전부를 `-EREMOTEIO`로 반환 [4] → "-121이면 주소인지 데이터인지 아직 모른다".
  - `i2cdetect`의 `UU` = 드라이버가 잡고 있어 **건너뛴** 주소(장치가 있을 가능성 큼) [8]. `-r`/`-q` probe는 일부 칩을 오동작시킬 수 있다고 man page가 경고 [8].
- **링크**: 전체 드릴·HW/SW 가르는 실험·영어 2분 답은 [BV13 S11](2026-11-01_BV13_board_debug_scenarios.html).

### B2 · SDA가 리셋 후 low에 붙어 있음 (요약+9-clock, 링크)

- **증상**: SoC warm reset 뒤 PMIC 접근이 전부 실패, idle에서 SDA 0 V · SCL high. 모든 주소가 ACK처럼 보이거나 controller가 "bus busy".
- **3줄 답**: ① target이 바이트 중간에 멈춰 '0'을 내보내는 중 → GPIO로 SCL을 최대 9번, 매 펄스 STOP 시도, idle 확인. ② SCL이 low에 붙어 있으면 클럭으로 불가 → target HW reset/전원 사이클 [1]. ③ 9번 후에도 low면 단락/back-power → 하드웨어 티켓.
- **여기서 새로 추가하는 것**:
  - Linux `i2c_generic_scl_recovery()`는 `RECOVERY_CLK_CNT 9`, SCL이 안 올라오면 `"SCL is stuck low, exit recovery"`, 끝나도 SDA가 low면 `-EBUSY` [5]. GPIO 복구를 쓰려면 DT에 `scl-gpios`(필수)·`sda-gpios`(선택)가 있어야 한다 [6] — **보드 bring-up 체크리스트 항목**.
  - DesignWare 드라이버는 "controller timed out"일 때 `i2c_recover_bus()`를 부른다 [4] → B7처럼 timeout 뒤에 복구가 자동으로 도는지 확인할 것.
  - 이 페이지 디코더는 캡처 끝에서 SDA가 20·tLOW 이상 low면 `STUCK_SDA`를 붙인다 — B5·B7 둘 다 **다른 원인의 결과로 stuck bus가 남는다**는 걸 보여 준다 (§8).
- **링크**: 왜 9인지·"SDA가 3클럭 만에 풀렸는데 끝인가?"·쓰기 중 복구 위험은 [BV10 체인 B](2026-11-01_BV10_board_interface_tests.html), 시뮬레이션은 [BV10 §6.1](2026-11-01_BV10_board_interface_tests.html)와 [V8](hmScreenPrep/problems/V8_i2c_scan_recover/index.html).

### B3 · 400 kHz에서만 간헐 실패 ★

#### 증상

- 100 kHz에선 수만 번 OK. `clock-frequency = <400000>`로 올리면 WHO_AM_I가 가끔 0xB5 대신 0x10·0x14 같은 값, 드물게 NACK. 랩 짧은 배선 보드는 OK, **하네스·커넥터가 붙은 보드만** 실패.
- analyzer: "400 kHz"라고 설정했는데 실측 **299 kHz**. 디코드는 대체로 정상처럼 보임.

#### 먼저 물어볼 것

- "Does it fail on every board or only on boards with the long harness or extra devices on the bus?"
- "Which bytes go wrong — bits that change from 0 to 1, or random? Is it data the target sends or data we send?"
- "What pull-up value and roughly how many devices and how much trace or cable are on this bus?"
- "Is the controller configured by the driver from DT timing properties, or with fixed counts?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| ① tr 초과 (Rp 큼 · Cb 큼) | 속도 의존 + 배선 의존 | scope 30/70 % tr | tr > 300 ns, RC 모양 완만, 0→1 비트만 틀림 |
| ② tLOW 부족 (LCNT 계산이 tr/tf 무시) | 400 kHz에서만 | tLOW 통계 | tLOW < 1.3 µs, edge가 빠른 보드에서도 실패 |
| ③ target 출력 지연 + 느린 edge 합 | target이 보내는 바이트만 깨짐 | SDA 변화 시각 vs SCL rising | tSU;DAT 음수, 비트 샘플 시 SDA가 30–70 % 사이 |
| ④ 노이즈/글리치 | 하네스 | glitch 트리거 | 랜덤 위치 비트 오류, 0→1 방향성 없음 |
| ⑤ 너무 센 풀업(VOL ↑) | "고치려고" Rp를 줄였을 때 | ACK 중 VOL | VOL > 0.4 V, low 노이즈 마진 부족 |

#### 측정 · 계측

- scope 2ch(SCL, SDA)를 **수신단(target 핀) 가까이**, 10x 프로브·짧은 접지 스프링. 측정 레벨을 **30 %/70 % VDD**로 바꾸고 rise time 통계(min/max) — 기본 10–90 %면 tr을 2배 이상 크게 읽는다 [BV10 §2.1.1].
- 트리거: SCL rising edge에 대해 **setup/hold violation 트리거**(SDA, setup 100 ns) 또는 decode 트리거 "READ data ≠ 0xB5".
- 프로브 자체가 Cb에 ~10 pF 안팎을 더한다 [추정 — 일반 passive 10x 프로브 사양 범위] → 프로브를 붙이면 더 나빠지는 마진 버그도 있다.

```text
 B3 실패 비트 (target이 0 다음 1을 내보냄, Fm, tr 700 ns) — 이 페이지 디코더 B3 캡처와 같은 숫자
 SCL  ‾‾‾\______________________________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾\____
          ↓ 30 % (t = 0)                ↑ 30 % at 1.82 µs  ← 수신기가 "이때 SDA는 이미 유효해야" (tSU;DAT ≥ 100 ns 전)
 SDA  ____________ (prev bit 0)  release at 0.85 µs (target tVD)  ...30 % 1.15 µs ...... 70 % at 1.84 µs
                                                   ___----‾‾‾‾‾
                                         ____----‾‾
      → SCL이 30 %를 지날 때 SDA는 30–70 % 사이 = 비트 값은 수신기 입력 threshold·hysteresis 운에 달림 → "간헐"
      → tSU;DAT = 1.82 − 1.84 = −0.02 µs (디코더: -25 ns)
      → 같은 바이트의 1→1, 1→0, 0→0 비트는 멀쩡 → 0xB5(1011 0101) 중 0→1인 b7,b5,b2,b0 (mask 0xA5)만 위험
        그 비트를 0으로 읽으면 0xB5 & ~0xA5 = 0x10  ← 증상의 "0x10"
```

#### root cause · fix

- **Root cause**: 4.7 kΩ 풀업 + 하네스 포함 Cb ≈ 175 pF → tr = 0.8473 × 4.7 kΩ × 175 pF ≈ **0.70 µs** (Fm 한계 0.3 µs의 2.3배). target은 자기 데이터시트(tVD;DAT ≤ 0.9 µs) 안에서 0.85 µs에 SDA를 놓지만, 그 스펙은 정상 tr을 전제로 한다 — RC 상승이 끝나기 전에 SCL이 올라온다. controller는 clock synchronization 때문에 **SCL의 느린 상승을 기다려 주므로** 주파수만 299 kHz로 떨어지고 tHIGH는 멀쩡 → "analyzer로 보면 대충 괜찮아 보이는" 이유.
- **Fix (계산)**: Cb 175 pF에서 `Rp(max) = 300 ns / (0.8473 × 175 pF) ≈ 2.0 kΩ`. 1.8 V IO면 VDD ≤ 2 V 규정(VOL2 = 0.2·VDD @ 2 mA, Table 10 [1])으로 `Rp(min) = (1.8 − 0.36) / 2 mA ≈ 0.72 kΩ`, 3.3 V면 (3.3 − 0.4)/3 mA ≈ 0.97 kΩ. → **1.5–1.8 kΩ** 선택: 1.8 kΩ이면 tr ≈ 267 ns (마진 11 %), 1.5 kΩ이면 ≈ 222 ns. 동시에 DT의 `i2c-scl-rising-time-ns`/`i2c-scl-falling-time-ns`를 실측값으로 넣어 드라이버가 HCNT/LCNT를 다시 계산하게 [6]. Cb를 못 줄이면 버스 분할·버퍼, 또는 이 버스만 100 kHz.
- **하지 말 것**: "400 kHz → 380 kHz로 살짝 내림" — 마진이 tr에서 깨지는 거라 주파수 조금 내려서는 안 고쳐진다(tLOW만 조금 늘 뿐). 숫자로 보여 줘야 한다.

#### 회귀로 굳히기

- 보드 bring-up 스위트: 버스마다 **30/70 % tr 측정 + Table 11 판정** (rev마다, 하네스 붙은 최대 Cb 구성에서).
- 펌웨어 회귀: target이 보내는 **0→1 전이가 많은 패턴**(0xA5/0x55 레지스터 또는 EEPROM 패턴)을 10⁵회 compare.
- 회로도 리뷰 체크리스트: 버스별 Cb 예산 표 + Rp(min)/Rp(max) 계산 첨부.

#### 30초 영어 답

"Failing only at 400 kHz points at timing margin, and on I2C the usual culprit is rise time, because the rising edge is just the pull-up charging the bus capacitance. I'd scope SCL and SDA at the receiver with the measurement thresholds set to 30 and 70 percent, not the default 10 to 90. Here I'd expect around 700 nanoseconds against a 300 nanosecond limit — that's 4.7 k with about 175 picofarads. The symptom fits: only bits where the target goes from 0 to 1 are wrong, because SDA is still between thresholds when SCL rises. The fix is a pull-up around 1.8 k, which gives about 270 nanoseconds and is still above the minimum for the sink current, plus telling the driver the real rise time. Then I add a rise-time check to the board suite at the worst-case harness load."

#### 꼬리 질문

**Q1. The analyzer says the clock is 299 kHz though we programmed 400. Is the controller broken?**

<details><summary>답</summary>

Probably not. Most controllers do clock synchronization: after releasing SCL they wait until they actually see it high before counting the high period, so a slow rise stretches every period. 1550 ns low count plus about 1 µs of rise plus 800 ns high gives about 3.3 µs, which is 299 kHz. Fixing the rise time brings the frequency back without touching the driver.

- 한국어 핵심: clock sync가 느린 상승을 기다려 주파수가 내려간 것 — 주파수 저하 자체가 tr 문제의 증거.

</details>

**Q2. Why not just use a very strong pull-up, like 470 Ω?**

<details><summary>답</summary>

Because the devices must pull the line below VOL max with their rated sink current: at 3.3 V and 3 mA the minimum is about 970 Ω. A stronger pull-up raises the low level, eats the low-side noise margin, and can make ACKs look marginal. Fm+ parts can sink 20 mA, which is why Fm+ allows much smaller resistors.

- 한국어 핵심: Rp(min) = (VDD − VOL)/IOL — 너무 센 풀업은 low 레벨을 못 내림.

</details>

**Q3. What if no resistor satisfies both limits?**

<details><summary>답</summary>

Then the bus is too heavy for the mode. Options are splitting the bus with a buffer or a mux so each segment has less capacitance, moving the slow or far devices to their own bus, using an active pull-up or a buffer with rise-time acceleration, or running that bus at 100 kHz where the limit is 1000 ns. I'd show the calculation table to the hardware team rather than argue.

- 한국어 핵심: Fm·400 pF에선 Rp(max) < Rp(min) — 버스 분할·버퍼·속도 낮추기.

</details>

### B4 · 읽을 때마다 엉뚱한 레지스터 값이 나옴 — repeated START vs STOP+START

#### 증상

- 드라이버가 WHO_AM_I(0x0F)를 읽으면 0x00, 다시 읽으면 0x00 또는 다른 값. `i2cget -y 2 0x48 0x0f`(SMBus read byte data)로 읽으면 0xB5로 정상.
- 디코더/analyzer: `S 48W A 0F A P | S 48R A 00 N P` — **쓰기와 읽기 사이에 STOP**.

#### 먼저 물어볼 것

- "Does the same read work from i2cget or i2ctransfer in one transfer?"
- "In the driver, is it one i2c_transfer with two messages, or a send followed by a separate receive?"
- "Does anything else touch this device — another driver, user space, firmware on another core?"
- "What does the datasheet say happens to the register pointer on STOP, and does it auto-increment?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| ① 포인터 쓰기와 읽기가 STOP으로 갈라짐 → 장치가 STOP에서 포인터 리셋 | i2cget은 되고 드라이버만 틀림 | analyzer에서 P 유무 | `0F A P` 다음 `S 48R` |
| ② 사이에 다른 agent가 끼어 포인터 변경 | 값이 "다른 레지스터 것"이고 가끔 | 캡처에서 두 트랜잭션 사이 다른 주소 접근 | 중간에 다른 S…P |
| ③ auto-increment: 포인터 없이 연속 읽기 | 읽을 때마다 다음 레지스터 | 연속 읽기 값 비교 | 0x0F, 0x10, 0x11 레지스터 값 순서 |
| ④ adapter가 Sr 미지원(SMBus 전용·FIFO 자동 STOP) | 드라이버가 어쩔 수 없이 분리 | `i2cdetect -F`의 "I2C yes/no" | I2C no → 임의 메시지 불가 |

#### 측정 · 계측

- analyzer(또는 scope decode)로 한 번의 드라이버 read를 캡처: Sr과 P+S는 디코드에서 다른 기호로 보인다. tBUF(STOP→START idle ≥ 1.3 µs Fm)가 있으면 STOP이 확실.
- Linux 측: tracepoint로 메시지 묶음 확인 [추정], 드라이버 코드에서 `i2c_master_send()` + `i2c_master_recv()` 두 번 호출(= 두 transfer = STOP 사이) vs `i2c_transfer()` 2-msg / `i2c_smbus_read_byte_data()`(Sr).
- `i2ctransfer`: 한 transfer 안 메시지는 **repeated START로 이어지고 끝에 STOP** — man page가 "일부 장치는 Sr에서는 내부 상태를 유지하지만 STOP 후엔 리셋한다"고 명시 [7]. `i2ctransfer -y 2 w1@0x48 0x0f r1` (Sr) vs `i2cset`+`i2cget` 따로 (STOP) 두 방식으로 A/B.

#### root cause · fix

- **Root cause**: 드라이버가 포인터 쓰기와 데이터 읽기를 별도 transfer로 보냄 → STOP → 이 장치는 STOP에서 포인터를 0x00으로 리셋 → 레지스터 0x00 값을 읽음. UM10204 combined format은 방향 전환 시 START+주소를 **반복**(Sr)하도록 정의한다 [1].
- **Fix**: 한 `i2c_transfer()`에 write msg + read msg(I2C_M_RD) 두 개, 또는 SMBus read byte/word data API. 여러 agent가 쓰는 버스면 한 transfer 안에서 Sr로 묶으면 중간에 다른 bus 사용자가 끼어들 수 없다 [7].

#### 회귀로 굳히기

- 드라이버 단위 테스트(또는 i2c-stub/mock adapter)에서 "write-then-read는 반드시 단일 transfer" assert.
- HIL 회귀: analyzer 스크립트로 장치별 기대 시퀀스(`S addrW reg Sr addrR … P`) 비교 — 이 페이지 디코더의 `WRITE_STOP_READ` 규칙이 그 판정.

#### 30초 영어 답

"A read that returns another register's value is usually the pointer being lost between the write and the read. I'd capture one driver read with the analyzer and look for a STOP between writing the register address and the read. Many devices keep the pointer across a repeated START but reset it on STOP, which matches i2cget working, since that uses repeated START. In Linux that's the difference between one i2c_transfer with two messages and a separate send and receive. The fix is to put both messages in one transfer, which also stops another bus user from slipping in between. Then the sequence check goes into our analyzer regression."

#### 꼬리 질문

**Q1. Both are I2C — why would the device care about STOP versus repeated START?**

<details><summary>답</summary>

STOP ends the transaction and frees the bus, so many devices reset their internal state machine and pointer on it. A repeated START keeps the bus owned and is defined for exactly this change of direction. The i2c-tools documentation calls out this behavior explicitly.

- 한국어 핵심: STOP = 트랜잭션 끝(상태 리셋 가능), Sr = 같은 트랜잭션 안 방향 전환.

</details>

**Q2. The values change every read, not just wrong. What does that tell you?**

<details><summary>답</summary>

Either an auto-incrementing pointer — each read advances to the next register, so I'd see consecutive registers' values in order — or another agent changing the pointer in between. The capture tells them apart: no other traffic plus an increasing sequence means auto-increment; foreign transactions in between mean a shared-bus race.

- 한국어 핵심: 연속 레지스터 값 순서 = auto-increment, 중간 다른 트래픽 = 경합.

</details>

### B5 · 단일 master인데 arbitration lost ★

#### 증상

- dmesg: `i2c_dw_handle_tx_abort: lost arbitration`(형태 예시 [4]) 또는 `-EAGAIN`(=-11) [3]. 버스에는 controller가 하나뿐이라고 알고 있음. 하루 몇 번, 카메라 스트리밍·모터 PWM이 돌 때 더 잦음. 가끔 그 뒤에 bus busy로 이어짐.

#### 먼저 물어볼 것

- "Is it really single-controller — any BMC, debug adapter, test fixture, PMIC or another core's firmware that can drive this bus?"
- "Does it correlate with some other activity — PWM, camera streaming, a switching regulator mode?"
- "Where in the transfer does the controller report it — always the same byte or bit?"
- "What spike filter setting does the controller use?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| ① SCL 글리치 ≥ tSP → target이 클럭 하나 더 셈 → target이 한 비트 일찍 ACK/데이터 → controller가 '1' 보냈는데 0 | 다른 활동과 상관 | scope pulse-width 트리거(SCL < 100 ns), 캡처 끝 | 50–100 ns 폭 SCL 펄스, 직후 abort |
| ② 숨은 두 번째 controller (다른 코어 FW, BMC, 지그) | "단일"이 가정일 뿐 | 긴 캡처에서 우리 것 아닌 START·다른 fSCL | 다른 클럭 속도의 트랜잭션 |
| ③ SDA 글리치(SCL high 중 dip) | crosstalk | SDA glitch 트리거 | SCL high 중 짧은 SDA 하강 |
| ④ 느린 rise → controller가 release 후 너무 일찍 SDA를 다시 읽음 | B3 같은 버스면 | tr, controller sample 시점 | '1' 비트에서만, 랜덤 아님 |
| ⑤ target이 원래 상태 꼬임 (이전 abort 잔재) | 직전에 다른 에러 | 이전 트랜잭션 로그 | 첫 바이트부터 이상 |

#### 측정 · 계측

- UM10204 arbitration: SCL high 동안 각 controller는 SDA가 자기가 보낸 값과 같은지 보고, **'1'을 보냈는데 low면 진 것** → SDA 출력을 끈다; 진 controller는 바이트 끝까지 클럭을 낼 수 있고 bus free 후 재시작해야 한다 [1]. 즉 "arbitration lost" = **"내가 1을 보냈는데 누가 0을 만들었다"** — 누가? 가 질문의 전부.
- scope: SCL에 **pulse-width < 150 ns 트리거** (Fm 정상 tHIGH ≥ 600 ns라 정상 펄스는 안 걸림), segmented memory로 하루치 이벤트를 모은다. 동시에 의심 aggressor(PWM gate, 카메라 MCLK)를 ch3에.
- 디코더 관점: 글리치를 프로토콜에서 빼고 보면 controller는 바이트 2를 8클럭까지 냈고 **9번째 없이 멈춤**, SDA는 마지막 클럭에서 0(controller는 1을 보냄) → `ARB_LOST_GLITCH`. 그리고 target은 ACK 중이라 SDA를 계속 잡고 있어 **stuck SDA가 남는다**.

```text
 B5 (디코더 B5 캡처) — 레지스터 바이트 0x0F 전송 중
 controller 의도 : 0    0    0    0    1    1    1    1   [A]
 SCL            _/‾\__/‾\__/‾\__/‾|_|‾\__/‾\__/‾\__/‾\__/‾‾‾‾‾‾‾‾‾  ← b4 high 중간 60 ns dip (30 % 아래)
                                  ↑ 글리치 ≥ tSP 50 ns → target 입력 필터 통과 → target은 9개 셈
 target 시각    : b7   b6   b5   b4  (b3)  b2   b1   b0  → "8비트 끝" → 다음 falling에 ACK(SDA low)
 SDA            ________________________/‾‾‾‾‾‾‾‾‾‾‾‾‾\__________  ← controller b0='1' 차례인데 target이 이미 ACK로 low
 controller     : b0에서 SDA=0 ≠ 1 → ARBITRATION LOST → SDA·SCL 놓고 정지 → SCL high, SDA low(target) = stuck
```

#### root cause · fix

- **Root cause (모델)**: 인접 PWM 배선과의 crosstalk로 SCL high 중 60 ns dip → 스펙 필터(50 ns)를 넘어 target이 클럭으로 셈 → 비트 카운트가 하나 밀림 → target이 한 비트 일찍 ACK → controller가 자기 '1'에서 0을 봄. controller의 arbitration 로직은 정상 동작이고, 거짓 "다른 master"는 target이었다.
- **Fix**: 배선 분리/가드, aggressor edge rate 낮춤, controller 디지털 필터 폭 증가(DT `i2c-digital-filter-width-ns` 같은 속성이 있는 IP면) [6], 풀업 강화로 high 노이즈 마진 확보, 직렬 저항+작은 C로 필터 [추정 — 보드 팀과 값 결정]. 소프트웨어: arb-lost 후 **bus recovery + 재시도**(Linux adapter `retries`) — 그러나 이건 증상 완화이지 fix가 아님.
- 숨은 master였다면: 소유권 정리(한 agent만 controller, 다른 쪽은 mailbox 경유), 정말 multi-master면 DT `multi-master` 표시와 재시도 정책 [6].

#### 회귀로 굳히기

- 보드 스위트: aggressor(PWM·카메라 스트림) **켠 상태에서** I2C soak 10⁶ 트랜잭션, arb-lost·NACK 카운터 0.
- 실리콘 측(N06C C1): spike filter 폭 스윕 테스트(20–80 ns 펄스 주입)로 필터 실측값 기록 — 50 ns 근처 글리치 내성의 근거.
- Linux i2c-gpio fault injector의 `lose_arbitration`으로 드라이버의 arb-lost 경로(재시도·복구) 회귀 [9].

#### 30초 영어 답

"Arbitration lost means the controller sent a one and saw a zero, so the only question is who made that zero. On a supposedly single-controller bus I check three things: a hidden second controller, like firmware on another core or a test fixture; a glitch that the target counted as a clock; or a slow rise where the controller reads its own released line too early. I'd trigger the scope on SCL pulses narrower than about 150 nanoseconds with segmented memory, with the suspect aggressor on another channel. A glitch wider than the 50 nanosecond spike filter makes the target count an extra clock, so it ACKs one bit early during our one bit. After the abort the target still holds SDA, so we also get a stuck bus. Fix the coupling, then soak with the aggressor running."

#### 꼬리 질문

**Q1. How do you tell a hidden controller from a glitch in the capture?**

<details><summary>답</summary>

A second controller leaves its own signature: a START we didn't issue, a different clock frequency or duty cycle, an address our driver never uses. A glitch shows up as a single narrow pulse with no protocol around it, and it lines up in time with the aggressor channel. With a long segmented capture triggered on the abort, the two look completely different.

- 한국어 핵심: 숨은 master = 우리 것 아닌 START·다른 fSCL, 글리치 = 프로토콜 없는 좁은 펄스 + aggressor와 시간 일치.

</details>

**Q2. Why did the bus get stuck after the arbitration loss?**

<details><summary>답</summary>

Because the target was mid-acknowledge — it was pulling SDA low for the ACK and waits for the falling clock edge to release it. The controller stopped clocking when it lost, so nobody gives that edge. That's why the driver's arbitration-lost path should run bus recovery before retrying.

- 한국어 핵심: target이 ACK 중이라 SDA를 잡고 있고, 진 controller가 클럭을 멈춤 → 복구 후 재시도.

</details>

**Q3. Isn't retrying on -EAGAIN enough?**

<details><summary>답</summary>

Retry hides it, and on writes a corrupted bit count can commit a wrong value to a register before anyone notices. I'd keep the retry as defense in depth but treat any arbitration loss on a single-controller bus as a signal-integrity bug with a counter in the board test.

- 한국어 핵심: 재시도는 완화일 뿐, 쓰기에서 잘못된 값이 들어갈 수 있음 → SI 버그로 추적.

</details>

### B6 · 카메라 모듈 전원 off 상태에서 버스 전체가 다운 ★

#### 증상

- 절전 모드에서 카메라 모듈(별도 rail) 전원을 끄면, 같은 버스의 PMIC·온도센서까지 전부 NACK/timeout. 카메라 전원을 켜면 회복. scope로 보면 idle SCL/SDA가 VDD도 0 V도 아닌 **≈0.6 V**, 카메라 rail이 꺼졌는데 **≈1.2 V로 떠 있음**.

#### 먼저 물어볼 것

- "What are the idle voltages on SCL and SDA with the camera off — exactly zero, or a few hundred millivolts?"
- "Is the camera module's I2C on the same rail as the pull-ups, through a level shifter, or through a serializer?"
- "Does the camera rail itself sit at some voltage when it should be off?"
- "Does the datasheet of the module's I2C device say its pins are high-impedance when unpowered?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| ① back-power: 풀업 → SDA/SCL → 꺼진 장치의 ESD 다이오드 → 꺼진 VDD | 0.5–0.7 V idle | 카메라 VDD rail 전압·전류 측정 | 꺼진 rail이 떠 있음, 핀에서 VDD로 전류 |
| ② 꺼진 장치의 pad가 low로 당김(Ioff 미지원) | Sm 장치는 전원 off 시 비방해 의무 없음 [1] | 모듈 분리 시 회복 | 분리하면 즉시 정상 |
| ③ 레벨 시프터 한쪽 전원 꺼짐 → 시프터가 버스를 잡음 | 시프터가 경로에 있으면 | 시프터 양쪽 레벨, OE | 시프터 우회 시 회복 |
| ④ 반쯤 살아난(phantom-powered) 장치가 이상 상태로 SDA 잡음 | 꺼진 rail 1.2 V | 전원 off 순간부터 캡처 | 전원 off 직후 SDA가 low로 떨어짐 |
| ⑤ 전원 off 순서 중 트랜잭션 진행 → stuck | 타이밍 의존 | 전원 off와 마지막 트랜잭션 시각 | 9 clock으로 회복 |

#### 측정 · 계측

- DMM/scope: SCL·SDA idle, 카메라 rail(꺼진 상태), 풀업 rail. 카메라 rail에 **전류 프로브**(또는 직렬 저항 전압) — 꺼진 rail로 흘러드는 전류가 back-feed 증거.
- 구간 자르기: 카메라 커넥터 분리 → 회복하면 모듈 쪽. 시프터/버퍼가 있으면 그 앞뒤 분리.
- 전원 off 순간 트리거(카메라 rail falling edge)로 SDA/SCL을 같이 캡처 — ④·⑤ 구분.

```text
 back-power 경로 (카메라 rail OFF)
  VDD_IO 1.8 V ── Rp ──┬── SDA ──────────►│ ESD diode ──► 카메라 VDD (OFF, 원래 0 V)
                       │                               └─► 다른 부하로 전류가 빠짐 → rail이 "반쯤" 떠 있음
                       └── SDA 레벨 = 다이오드 drop 근처(≈0.5–0.7 V) → VIH(0.7·VDD = 1.26 V) 못 넘음 → 모두에게 "low"
```

#### root cause · fix

- **Root cause**: 카메라 모듈의 I2C 장치가 전원 off 시 고임피던스가 아닌 pad(ESD 다이오드가 VDD로) → 상시 전원 풀업이 그 다이오드를 통해 꺼진 rail로 전류를 보내 버스를 ~0.6 V에 묶음. UM10204는 Fm/Fm+ 장치에 "VDD off 시 SDA/SCL을 막지 말 것"을 요구하지만 [1] 모든 부품이 그런 건 아니고, SMBus도 꺼진 장치의 back-powering 보호를 별도 요구로 둔다 [2].
- **Fix**: ① 꺼질 수 있는 장치는 **isolation 기능(전원 off 시 양쪽 분리)이 있는 버퍼/스위치/mux 뒤로** ② 또는 그 구간 풀업을 장치와 **같은 switched rail**에 ③ 드라이버/PM: 전원 off 전에 그 구간을 mux로 끊고, 전원 on 후 tPOR(장치 데이터시트, SMBus는 500 ms 상한 [2]) 기다린 뒤 접근 + bus clear. 차량 카메라는 보통 SerDes(GMSL/FPD-Link) 뒤라 원격 I2C가 터널링되는데, 그 경우 로컬 deserializer 쪽 버스에서 같은 질문을 반복 [추정].

#### 회귀로 굳히기

- PM 회귀: 각 switchable rail을 하나씩 끈 상태에서 **나머지 장치 스캔 + 읽기** (rail × 장치 매트릭스), idle 레벨이 VDD의 90 % 이상인지 자동 측정(ADC/스코프).
- 부품 선정 체크리스트: "꺼질 수 있는 rail에 붙는 I2C 장치 → 전원 off 시 비방해(Ioff) 명시 여부".

#### 30초 영어 답

"When powering one module down kills the whole bus, I measure the idle levels first. Exactly zero means something is actively pulling low; a few hundred millivolts means current is flowing out of the bus into the unpowered device through its ESD diode, and the dead rail floating at a volt confirms it. The spec only requires Fast-mode devices to stay off the bus when unpowered, so it's a part-choice and topology issue. The fix is to put switchable devices behind an isolating buffer or mux, or tie that segment's pull-ups to the same switched rail, and to have the power-management code disconnect the segment before power-off. The regression turns off each rail in turn and scans everything else."

#### 꼬리 질문

**Q1. Why does a voltage of 0.6 V break every device, not just the camera?**

<details><summary>답</summary>

Because it's a shared wired-AND line: every device sees the same 0.6 V, and with a 1.8 V supply the high threshold is 0.7 times VDD, about 1.26 V. Nobody can see a high, so nobody can see a START, and the whole bus is down.

- 한국어 핵심: 공유 선이라 0.6 V < VIH(1.26 V) → 모든 장치에 low.

</details>

**Q2. How would you catch this before the board is built?**

<details><summary>답</summary>

In schematic review, for each I2C bus I list every device with its power rail and whether its pins are specified high-impedance when unpowered, and I flag any switchable rail sharing a bus with always-on pull-ups. That's a five-minute table that prevents a respin.

- 한국어 핵심: 회로도 리뷰에 "장치·rail·전원 off 시 비방해" 표.

</details>

### B7 · 센서가 가끔 clock stretching을 길게 → controller timeout

#### 증상

- 습도/온도 센서를 "hold" 방식으로 읽으면 대부분 OK, 가끔 `-ETIMEDOUT`(=-110) [3]. 그 직후부터 같은 버스 전부 실패, 재부팅하거나 복구가 돌아야 회복. controller HW SCL-low timeout = 10 ms로 설정됨, 센서 데이터시트의 변환 최대 시간은 ~12 ms [mock 가정].

#### 먼저 물어볼 것

- "How long does the target hold SCL low in the failing case, and what's the controller's timeout?"
- "Does the datasheet give a maximum conversion or stretch time, and over temperature?"
- "Is this an SMBus device — then the 25 ms rule applies — or plain I2C, which has no timeout?"
- "After the timeout, does the driver run bus recovery?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| ① target stretch > controller timeout (설정이 데이터시트 max보다 짧음) | 가끔만, 변환 시간 분산 | SCL low pulse-width 측정 | 12 ms stretch vs 10 ms timeout |
| ② controller가 그 위치의 stretch를 지원 안 함(비트 단위) | 특정 비트 위치에서만 | stretch 위치 | ACK 뒤가 아닌 비트 중간 stretch |
| ③ target hang(펌웨어 MCU target) | stretch가 끝나지 않음 | 긴 캡처 | SCL이 영원히 low → SCL stuck |
| ④ timeout 후 복구 없음 → stuck 잔존 | 이후 전부 실패 | timeout 직후 idle 레벨 | SCL high, SDA low (target이 바이트 중간) |

#### 측정 · 계측

- scope pulse-width 트리거: **SCL low > 5 ms**. 시간축 2 ms/div로 stretch 길이 분포를 segmented로 수집(온도별).
- 디코더 B7 결과: `stretch: longest SCL low 12.000 ms ... controller timeout 10.000 ms -> EXCEEDED | SMBus tTIMEOUT(min) 25 ms -> within` → **I2C/SMBus 규칙상 합법인 stretch를 controller 설정이 불법으로 취급**한 것.
- timeout 후: target이 12 ms에 SCL을 놓고 데이터 b7(=0)을 SDA에 올렸는데 controller는 이미 포기 → SCL high · SDA low = stuck (`STUCK_SDA`).

```text
 B7 (Sm, 디코더 B7 캡처)
 SCL  ‾\_/‾\_ ... [ACK of 40R] \_____________________ 12 ms (target stretch) ____________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
                                         ↑ 10 ms: controller timeout → -ETIMEDOUT, controller는 더 이상 클럭 안 냄
 SDA  ...  ‾\_[A]___________________________________________________________ (target: b7 = 0 유지) ____________
                                                                                    → idle인데 SDA low = stuck
```

#### root cause · fix

- **Root cause**: controller HW timeout 10 ms < 센서 최대 stretch 12 ms. I2C 자체는 stretch 시간 제한이 없고(Linux fault-codes도 "I2C has no such timeout, adapters often impose their own" [3]), SMBus는 tTIMEOUT 25–35 ms·누적 25 ms [2] — 즉 이 센서는 SMBus 규칙으로도 합법.
- **Fix**: ① 센서를 **no-hold 방식**(변환 시작 → 주기적으로 읽기 시도, 준비 전엔 NACK — UM10204 NACK 조건 2 [1])으로 바꿔 버스를 안 붙잡게 ② 또는 timeout을 "데이터시트 max × 마진"으로 (예: 12 ms × 1.5 → 18–20 ms [추정]) — 단 SMBus 장치가 같은 버스에 있으면 25 ms 전에 그 장치들이 리셋할 수 있음을 고려 ③ timeout 경로에서 반드시 **bus recovery**(9 clock + STOP) → 다음 트랜잭션 전 idle 확인.

#### 회귀로 굳히기

- 센서 변환 시간을 온도 코너별로 측정해 timeout 설정 근거 표로 남김.
- fault injection: FPGA/exerciser target으로 stretch 길이 스윕(0 → timeout × 2), "timeout 미만은 성공, 초과는 TIMEOUT 비트 + 복구 후 다음 전송 성공" (N06C C1 매트릭스의 stretch 행과 같은 판정).

#### 30초 영어 답

"First I'd measure how long the target actually holds SCL and compare it with the controller timeout. Here the sensor legally stretches about 12 milliseconds during a conversion and the controller gives up at 10, so this is a configuration mismatch, not a bad part — plain I2C has no stretch limit, and even SMBus allows 25. The second bug is what happens after the timeout: the target releases SCL and is mid-byte driving a zero, so SDA stays low and the whole bus is stuck. I'd switch the sensor to its polling mode so it doesn't hold the bus, set the timeout from the datasheet maximum with margin, and make the timeout path run bus recovery. A stretch-length sweep goes into regression."

#### 꼬리 질문

**Q1. Why not just set the timeout to one second?**

<details><summary>답</summary>

Because the timeout is also the detector for a truly hung target, and while one device holds the bus nobody else can use it — a PMIC or thermal sensor on the same bus would be blocked for a second. On a bus with SMBus devices, they may reset themselves after 25 to 35 milliseconds anyway. Better to stop holding the bus with polling mode and keep a timeout just above the real maximum.

- 한국어 핵심: timeout은 hang 검출기 — 너무 길면 버스 전체가 막힘, SMBus 장치는 25–35 ms에 스스로 리셋.

</details>

**Q2. What's the difference between SMBus tTIMEOUT and tLOW:SEXT?**

<details><summary>답</summary>

tTIMEOUT is about any single clock-low interval: after 25 milliseconds, devices may abort and must be ready for a new START by 35. tLOW:SEXT is the cumulative stretch a target may add across one whole message from START to STOP, up to 25 milliseconds, and the controller has its own 10 millisecond per-byte budget, tLOW:MEXT.

- 한국어 핵심: tTIMEOUT = 한 번의 clock low 25–35 ms, SEXT = 메시지 전체 누적 25 ms, MEXT = controller 바이트당 10 ms.

</details>

### B8 · 고온 chamber에서만 NACK 증가

#### 증상

- 25 °C에서 10⁶ 트랜잭션 0 NACK, 105 °C에서 시간당 수십 번 NACK. 저온 −40 °C는 OK. 보드 팜 20장 중 4장이 특히 심함.

#### 먼저 물어볼 것

- "Address NACK or data NACK — and which device?"
- "Is it steady at temperature or only while the temperature is changing?"
- "Is the whole board in the chamber, or just the SoC under a thermal head?"
- "Do the bad boards share a lot, a rev, or a harness?"

#### 가설 순위

| 가설 | 왜 의심 | 확인 방법 | 맞으면 보이는 것 |
|---|---|---|---|
| ① 타이밍 마진 잠식: 고온에서 target tVD↑, pad 구동 약화, 입력 threshold 이동 | 고온만, 일부 보드 | 고온에서 tSU;DAT·tr·VOL 측정 | 25 °C 대비 setup 마진 축소, B3 패턴(0→1 비트) |
| ② 장치가 바쁨(변환·write cycle이 고온에서 길어짐, 열 보호) | 데이터 NACK·특정 장치 | NACK 위치, 지연 후 재시도 | 지연 늘리면 사라짐 |
| ③ 누설 증가 → high 레벨 저하 / low 상승 | 고온 + 꺼진 장치 존재 | idle high 레벨 vs 온도 | VDD보다 낮은 high, 온도 따라 하강 |
| ④ 기계: 커넥터·솔더 간헐 (열팽창) | 온도 **변화** 중 | 램프 중 vs 정착 후 비교, 탭 테스트 | 램프 중 집중, 특정 보드 |
| ⑤ 공급 전압 droop at hot | PMIC·LDO 마진 | 장치 VDD 고온 측정 | VDD 하한 근접 |

#### 측정 · 계측

- 챔버 밖으로 프로브를 길게 빼면 그 자체가 Cb·노이즈를 바꾼다 → 고온 정격 프로브/케이블, 또는 **보드 위 측정(디코더용 GPIO 샘플러, controller 에러 카운터)**으로 1차 수집. [BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html)의 PVT·shmoo 절차.
- 온도 × 속도(100/400 kHz) × 풀업(정격/강화) 미니 shmoo: 실패율이 속도·풀업에 반응하면 ①, 무반응이면 ②/④.
- scope decode 트리거 "NACK" + segmented memory로 실패 순간만 수천 장 → NACK 위치(주소/데이터) 히스토그램.
- 실패율 통계: 0 실패를 주장하려면 rule of three (n회 0 실패 → 95 % 상한 3/n) [N06H](2026-11-01_N06H_debug_methodology_triage.html).

#### root cause · fix

- **전형 결론 (가정 사례)**: 4장의 나쁜 보드는 풀업이 마진 경계(tr ≈ 280 ns @ 25 °C)였고, 고온에서 target 출력 지연이 늘어 tSU;DAT가 음수로 → B3와 같은 메커니즘이 온도로 드러남. Fix = B3 계산대로 풀업 재선정 + 105 °C에서 tr·tSU;DAT 재측정. ②였다면 드라이버 대기 시간을 데이터시트 **온도 범위 전체의 max**로, 또는 ACK polling.
- 핵심 태도: "고온에서만" = **원래 마진이 작았다**는 신호. 온도는 원인이 아니라 돋보기.

#### 회귀로 굳히기

- PVT 회귀에 I2C soak 추가: 온도 3점 × 10⁶ 트랜잭션, NACK/arb-lost/timeout 카운터와 tr·VOL 측정값을 보드 시리얼별로 저장 → 마진이 줄어드는 보드를 실패 전에 찾음.
- 양산 테스트(SLT)에 "고온 스캔 + 읽기 패턴" 노드 [추정 — 비용 대비 결정].

#### 30초 영어 답

"Failures only at high temperature usually mean the margin was small to begin with and temperature exposed it. I'd first split address versus data NACKs and steady-state versus ramping, because ramping-only points at a mechanical intermittent. Then a small shmoo in the chamber — temperature against bus speed and pull-up strength. If the failure rate responds to speed and pull-up, it's timing margin, and I measure setup time and rise time at 105 degrees on the bad boards. If it doesn't, the device is probably busy longer at hot, and the driver's wait or polling has to cover the datasheet maximum over the full temperature range. Either way the fix is verified at hot with enough transactions to bound the failure rate."

#### 꼬리 질문

**Q1. How many transactions do you need to claim it's fixed?**

<details><summary>답</summary>

It depends on the failure rate I need to bound. With zero failures in n trials, the 95 percent upper bound is about three over n, so ten to the six clean transactions at hot bounds it below three per million. Before the fix I measured tens per hour, so I'd also compare rates with the same test time to show the change is real.

- 한국어 핵심: rule of three — 0/n → 95 % 상한 3/n.

</details>

**Q2. Why would only 4 of 20 boards fail?**

<details><summary>답</summary>

Because margin is a distribution: part-to-part variation in the target's output delay, the pull-up tolerance, and the harness capacitance stack up, and only the tail crosses the line at hot. I'd measure rise time and setup margin on all 20 at room temperature — if the four bad ones are the four with the least margin, the mechanism is confirmed without even heating them.

- 한국어 핵심: 마진은 분포 — 상온 마진 측정만으로 나쁜 4장을 예측하면 메커니즘 확정.

</details>

## 6. 꼬리 질문 체인

### 체인 1 — "The I2C sensor NACKs." → "Address on the wire is right." → "It ACKs on the bench, NACKs in the car." → "How do you make sure it never ships again?"

<details><summary>Q. The I2C sensor NACKs. Debug it.</summary>

"I split by where the NACK happens. With a scope or analyzer near the device I check the address byte and the ninth clock. Wrong address on the wire is software — seven versus eight bit, wrong bus. Right address and NACK means the device isn't participating: power, reset, straps, or the bus electrically."

- 한국어 핵심: 와이어 위 주소 → 주소/데이터 NACK 구분 → SW vs 장치/전기.

</details>

<details><summary>꼬리1. The address is right and it still NACKs.</summary>

"Then I go electrical and state: device supply and reset pin, the address-strap levels, and the bus idle levels and rise time at 30 and 70 percent. I also keep in mind a NACK can mean the device is busy — an EEPROM in its write cycle NACKs its own address — so I check whether a retry after a delay succeeds."

- 한국어 핵심: 전원·리셋·strap·idle·tr, 그리고 "바빠서 NACK" 가능성.

</details>

<details><summary>꼬리2. It ACKs on the bench but NACKs in the car harness.</summary>

"That's bus load. The harness adds capacitance and coupling, so rise time grows and glitches appear. I'd measure rise time with the harness attached and compute the pull-up limits for that capacitance; if it's over the 300 nanosecond Fast-mode limit, it's the B3 pattern. If rise time is fine, I look for glitches correlated with other harness signals."

- 한국어 핵심: 하네스 = Cb ↑ + crosstalk → tr 측정·풀업 재계산, 아니면 글리치 추적.

</details>

<details><summary>꼬리3. How do you make sure it never ships again?</summary>

"Three layers: a board test that scans the expected address map per board revision and measures rise time at the worst-case load, a schematic-review table of bus capacitance and pull-up limits, and a firmware soak with the real harness and aggressors running. Each layer would have caught this case."

- 한국어 핵심: 주소 맵 스캔 + tr 측정, 회로도 Cb 표, 실제 하네스 soak.

</details>

### 체인 2 — "Fails only at 400 kHz." → "How do you pick the pull-up?" → "No resistor works." → "Why is the clock slower than programmed?"

<details><summary>Q. It works at 100 kHz but fails at 400 kHz.</summary>

"Speed-dependent failure on I2C is almost always timing margin, so I measure rise time at 30 to 70 percent and tLOW and tHIGH against the Fast-mode table. The Fast-mode budget has zero slack — 1.3 plus 0.6 plus 0.3 plus 0.3 microseconds is exactly the 2.5 microsecond period — so either a slow edge or a 50 percent duty cycle breaks it."

- 한국어 핵심: Fm 예산 여유 0 → tr 또는 tLOW.

</details>

<details><summary>꼬리1. How do you pick the pull-up?</summary>

"Between two limits. The maximum comes from rise time: tr max divided by 0.8473 times the bus capacitance. The minimum comes from the sink current: VDD minus VOL max, divided by the rated current, about 970 ohms at 3.3 volts and 3 milliamps. At 175 picofarads in Fast-mode that's roughly 1 to 2 kilohms, and I'd pick 1.8 k with margin."

- 한국어 핵심: Rp(max) = tr/(0.8473·Cb), Rp(min) = (VDD−VOL)/IOL.

</details>

<details><summary>꼬리2. The calculation says no resistor satisfies both.</summary>

"Then the bus is too heavy for that mode — at 400 picofarads in Fast-mode the maximum is below the minimum. I'd split the bus with a buffer or mux, move far devices to another bus, use a rise-time accelerator, or run that bus at 100 kHz. That's a hardware decision, so I bring the numbers."

- 한국어 핵심: 버스 분할·버퍼·속도 낮추기 — 숫자로 HW 팀 설득.

</details>

<details><summary>꼬리3. The analyzer shows 299 kHz though we programmed 400. Why?</summary>

"Clock synchronization: the controller waits to see SCL high before counting its high time, so a slow rise adds to every period. It's actually a nice diagnostic — a frequency drop that tracks bus load is a rise-time fingerprint."

- 한국어 핵심: 주파수 저하 = tr의 지문.

</details>

### 체인 3 — "Arbitration lost on a single-controller bus." → "How can noise do that?" → "Prove it." → "Why is the bus stuck afterwards?"

<details><summary>Q. We have one controller but it reports arbitration lost.</summary>

"Arbitration lost means it sent a one and read a zero. So someone made a zero: a second controller we didn't know about, a target that's out of step, or a glitch. I'd first confirm there's really no other bus user, then look for glitches."

- 한국어 핵심: "1 보냈는데 0" — 누가 0을 만들었나.

</details>

<details><summary>꼬리1. How can noise cause that?</summary>

"A glitch on SCL wider than the 50 nanosecond spike filter is counted as a clock by the target. Its bit counter slips by one, so it drives its ACK one bit early, during a bit where the controller is sending a one. The controller sees the zero and correctly declares arbitration lost."

- 한국어 핵심: ≥ 50 ns SCL 글리치 → target 비트 카운트 +1 → 이른 ACK.

</details>

<details><summary>꼬리2. Prove it's a glitch.</summary>

"Trigger the scope on SCL pulses narrower than the shortest legal high time, with segmented memory, and put the suspected aggressor on another channel. If every arbitration loss is preceded by a narrow pulse aligned with the aggressor edge, and moving the aggressor or adding filtering makes the count go to zero over a long soak, that's the proof."

- 한국어 핵심: 좁은 펄스 트리거 + aggressor 상관 + 대책 후 soak 0.

</details>

<details><summary>꼬리3. Why does the bus stay stuck afterwards?</summary>

"The target was in its ACK and holds SDA until the next falling clock, but the losing controller stopped clocking. So the arbitration-lost handler must run bus recovery before the retry."

- 한국어 핵심: 진 controller가 클럭 멈춤 → target이 SDA 계속 잡음 → 복구 후 재시도.

</details>

### 체인 4 — "The sensor stretches the clock and we time out." → "Just raise the timeout?" → "I2C vs SMBus rules?" → "What must the driver do after a timeout?"

<details><summary>Q. A sensor sometimes holds SCL low and the controller times out.</summary>

"I measure the stretch length and compare it with the controller timeout and the datasheet maximum. In my capture the sensor stretches 12 milliseconds and the controller gives up at 10 — the sensor is legal, the configuration isn't."

- 한국어 핵심: stretch 실측 vs timeout vs 데이터시트 max.

</details>

<details><summary>꼬리1. Just raise the timeout?</summary>

"Somewhat, to the real maximum with margin, but the better fix is not holding the bus at all — use the sensor's polling mode, where it NACKs until data is ready. A long hold blocks every other device on the bus, and the timeout is also our hang detector."

- 한국어 핵심: polling mode가 정답, timeout은 실제 max + 마진만.

</details>

<details><summary>꼬리2. What do the I2C and SMBus specs say about this?</summary>

"Plain I2C has no clock-stretch limit. SMBus defines a clock-low timeout of 25 to 35 milliseconds, a 25 millisecond cumulative stretch limit for a target per message, and 10 milliseconds per byte for the controller. So if SMBus devices share this bus, a long stretch can make them reset."

- 한국어 핵심: I2C 무제한, SMBus 25–35 ms·SEXT 25 ms·MEXT 10 ms.

</details>

<details><summary>꼬리3. What must the driver do after a timeout?</summary>

"Assume the bus is dirty. After the timeout the target eventually releases SCL and is mid-byte, possibly driving a zero, so the driver must run bus recovery — nine clocks with STOP attempts — confirm both lines idle, and only then retry or report. In my capture, skipping that leaves SDA stuck low."

- 한국어 핵심: timeout 후 bus recovery + idle 확인 후 재시도.

</details>

## 7. Don 경험 연결

- "At Apple I root-cause I2C and SPMI failures between new wireless chips and the system using a scope and a protocol analyzer — [숫자 채우기: one case — symptom, what I measured, root cause] — so separating an address NACK from a rise-time problem from a protocol bug with real captures is daily work for me."
- "At Solidigm I verified the I2C and SPI blocks on the FPGA prototype of our PCIe Gen6 SSD controller before RTL freeze — [숫자 채우기: tests, bugs found] — which is where I learned that the controller's high and low count calculation and its timeout and abort paths are where the bugs hide."
- "At SK hynix I automated chamber reliability testing for seven years, so for a failure that only shows at 105 degrees, my instinct is a temperature-by-speed-by-pull-up shmoo with enough transactions to bound the rate — [숫자 채우기: chamber test scale]."

## 8. 실습 / 코드

### 8.1 무엇을 하나

- [i2c_capture_decode.c](onsitePrep/n07/b/i2c_capture_decode.c) (387줄): **2-threshold 캡처**(각 선을 0.3·VDD·0.7·VDD 두 레벨로 비교한 0/m/1 시퀀스 — scope/MSO export 형태 가정)를 읽어
  1. 선마다 Schmitt(히스테리시스) 논리 레벨 + 매 edge의 30 %/70 % 통과 시각 → tr, tf
  2. 글리치 필터: tHIGH(min)/4보다 좁은 펄스를 프로토콜에서 제거하고 보고, 30 % 아래 폭이 **tSP 50 ns 이상이면 "수신기가 셈"** 표시
  3. 프로토콜: START/Sr/STOP, SCL rise에서 샘플·fall에서 확정(그래서 Sr/STOP 직전 rise는 비트로 안 셈), 9번째 = ACK/NACK, 주소+R/W, 중간 포기(abort), 캡처 끝 stuck 판정
  4. UM10204 Table 11 대비: fSCL(중앙값), tLOW, tHIGH, tr, tf, tSU;DAT, tHD;STA, tSU;STA, tSU;STO, tBUF + **"SCL이 30 %를 지날 때 SDA가 30–70 % 사이"인 비트**
  5. clock stretching(SCL low > 4·tLOW(min)) 최장 길이 vs 헤더의 controller timeout, SMBus tTIMEOUT(min) 25 ms
  6. 고정 우선순위 진단(ARB_LOST_GLITCH > STRETCH_TIMEOUT > SLOW_RISE > WRITE_STOP_READ > STUCK_SDA > …) → `# expect`와 비교해 PASS/FAIL
- [gen_captures.py](onsitePrep/n07/b/gen_captures.py) (225줄, stdlib): 버스 모델로 mock 캡처 5개 생성 — falling 70→30 % = tf, rising = RC(30 % at 0.421·tr, 70 % at 1.421·tr, UM10204의 0.3567·RC/1.2040·RC에서 유도), controller는 clock sync(SCL 70 %를 본 뒤 HCNT), target 출력은 SCL 30 % 하강 후 tVD.
- 기존 코드와 다른 점: [V8](hmScreenPrep/problems/V8_i2c_scan_recover/index.html)은 bit-bang scan/recover, [BV10 §6.1](2026-11-01_BV10_board_interface_tests.html)은 stuck-bus 복구 상태기계, N06C의 i2c_timing_check.c는 **controller 설정값**으로 타이밍 판정 — 이건 **와이어 캡처를 읽고 진단**하는 쪽.

| 캡처 | 시나리오 | 모델 조건 | 기대 진단 |
|---|---|---|---|
| cap_golden.txt | 정상 기준선 | Fm, tr 120 ns, Sr 레지스터 읽기 → 0xB5 | OK |
| cap_b3_slow_rise.txt | B3 | 같은 부품, tr 700 ns (4.7 kΩ × 175 pF 상당), target tVD 0.85 µs | SLOW_RISE |
| cap_b4_stop_between.txt | B4 | 포인터 쓰기 뒤 STOP, 장치는 레지스터 0x00 값 반환 | WRITE_STOP_READ |
| cap_b5_glitch_arb.txt | B5 | 0x0F 전송 중 b4에서 SCL 60 ns dip, target 이른 ACK, controller 정지 | ARB_LOST_GLITCH |
| cap_b7_stretch_timeout.txt | B7 | Sm, 읽기 주소 ACK 뒤 12 ms stretch, controller timeout 10 ms | STRETCH_TIMEOUT |

### 8.2 실행

```bash
cd onsitePrep/n07/b && ./run.sh     # 캡처 재생성 → ASan/UBSan 빌드($TMPDIR) → 5개 판정 → run_output.txt
```

### 8.3 실제 출력 (2026-11-01, macOS, `-Wall -Wextra -Werror -fsanitize=address,undefined`, 경고 0, exit 0)

```text
### python3 -I gen_captures.py
wrote cap_golden.txt (202 rows)
wrote cap_b3_slow_rise.txt (202 rows)
wrote cap_b4_stop_between.txt (190 rows)
wrote cap_b5_glitch_arb.txt (88 rows)
wrote cap_b7_stretch_timeout.txt (144 rows)
### cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined i2c_capture_decode.c && run
== cap_golden.txt  [mode fm, 400 kHz max]
   case: golden -- Fm register read with repeated START, healthy edges
   decode: S 48W A 0F A Sr 48R A B5 N P |
   timing: fSCL(median) 397 kHz | tLOW min 1575 ns (>= 1300) | tHIGH min 800 ns (>= 600)
           tr max 120 ns (<= 300) x0/50 | tf max 25 ns (<= 300) | tSU;DAT min 555 ns (>= 100) x0
           tHD;STA min 700 (>= 600) | tSU;STA min 700 (>= 600) | tSU;STO min 750 (>= 600)
   flags : (none)
   diag  : OK
   PASS (expect OK, got OK)

== cap_b3_slow_rise.txt  [mode fm, 400 kHz max]
   case: B3 -- fails only at 400 kHz: tr 700 ns, target data valid late
   decode: S 48W A 0F A Sr 48R A B5? N(indeterminate bits mask 0xA5) P |
   timing: fSCL(median) 299 kHz | tLOW min 1819 ns (>= 1300) | tHIGH min 800 ns (>= 600)
           tr max 700 ns (<= 300) VIOL x50/50 | tf max 25 ns (<= 300) | tSU;DAT min -25 ns (>= 100) VIOL x4
           tHD;STA min 700 (>= 600) | tSU;STA min 700 (>= 600) | tSU;STO min 994 (>= 600)
   sample: 4 bit(s) had SDA between 30-70 % when SCL crossed 30 % rising -> value depends on the receiver
   flags : SLOW_RISE TIMING
   diag  : SLOW_RISE -- rise time over Table 11 limit -> fix Rp/Cb (or lower fSCL), measure at 30/70 %
   PASS (expect SLOW_RISE, got SLOW_RISE)

== cap_b4_stop_between.txt  [mode fm, 400 kHz max]
   case: B4 -- read returns another register: STOP+START instead of Sr
   decode: S 48W A 0F A P | S 48R A 00 N P |
   timing: fSCL(median) 397 kHz | tLOW min 1575 ns (>= 1300) | tHIGH min 800 ns (>= 600)
           tr max 120 ns (<= 300) x0/47 | tf max 25 ns (<= 300) | tSU;DAT min 700 ns (>= 100) x0
           tHD;STA min 700 (>= 600) | tSU;STO min 750 (>= 600) | tBUF min 1500 (>= 1300)
   read  : last read byte 0x00, expected 0xB5
   flags : WRITE_STOP_READ READ_MISMATCH
   diag  : WRITE_STOP_READ -- pointer write ended with STOP; device resets its pointer on STOP -> use repeated START (combined format)
   PASS (expect WRITE_STOP_READ, got WRITE_STOP_READ)

== cap_b5_glitch_arb.txt  [mode fm, 400 kHz max]
   case: B5 -- single controller reports arbitration lost
   glitch: SCL low pulse 60 ns @ t=34992 ns -> >= tSP 50 ns: spec input filters pass it, receivers COUNT it
   decode: S 48W A <abandoned: byte 2 after 8 clocks, SDA=0 on last clock>
   timing: fSCL(median) 397 kHz | tLOW min 1575 ns (>= 1300) | tHIGH min 800 ns (>= 600)
           tr max 120 ns (<= 300) x0/20 | tf max 25 ns (<= 300) | tSU;DAT min 1105 ns (>= 100) x0
           tHD;STA min 700 (>= 600)
   flags : ARB_LOST_GLITCH SCL_GLITCH ABORT_MIDBYTE
   diag  : ARB_LOST_GLITCH -- SCL glitch >= tSP counted by the target -> bit-count slip -> target drives SDA early -> controller sees 0 while sending 1
   PASS (expect ARB_LOST_GLITCH, got ARB_LOST_GLITCH)

== cap_b7_stretch_timeout.txt  [mode sm, 100 kHz max]
   case: B7 -- sensor clock-stretches 12 ms, controller timeout 10 ms
   decode: S 40W A E3 A Sr 40R A <abandoned: byte 2 after 1 clocks, SDA=0 on last clock> <SDA held low 17010.1 us at end, SCL=1>
   timing: fSCL(median) 100 kHz | tLOW min 5496 ns (>= 4700) | tHIGH min 4200 ns (>= 4000)
           tr max 300 ns (<= 1000) x0/35 | tf max 30 ns (<= 300) | tSU;DAT min 4770 ns (>= 250) x0
           tHD;STA min 4500 (>= 4000) | tSU;STA min 5000 (>= 4700)
   stretch: longest SCL low 12.000 ms before bit 1 of byte 2 | controller timeout 10.000 ms -> EXCEEDED | SMBus tTIMEOUT(min) 25 ms -> within
   flags : STRETCH_TIMEOUT STUCK_SDA ABORT_MIDBYTE
   diag  : STRETCH_TIMEOUT -- target stretches longer than the controller's SCL-low timeout -> controller aborts mid-byte -> bus left owned by target
   PASS (expect STRETCH_TIMEOUT, got STRETCH_TIMEOUT)

summary: 5 case(s), 0 FAIL
exit=0
```

### 8.4 출력 읽는 법

- **표기**: `48W` = 7-bit 주소 0x48 + W, `A`/`N` = 9번째 클럭 ACK/NACK, `|` = STOP 뒤 트랜잭션 경계, `byte n` = 마지막 S/Sr 이후 n번째 바이트(주소 바이트가 1).
- **golden**: 이게 기준선. fSCL 397 kHz(≤ 400), tLOW 1575 ns(Fm 1300 대비 +21 %), tHIGH 800 ns, tr 120 ns, tSU;DAT 555 ns — **고장 캡처와 숫자를 나란히 놓고 읽는다.** tSU;STO 750 ns = controller가 0.7 µs 기다린 뒤 SDA가 30 %를 지나는 시간까지.
- **B3**: ① `tr max 700 ns VIOL x50/50` — 모든 rising edge가 위반(RC 문제는 모든 edge에 공통) ② `fSCL 299 kHz` — clock sync가 느린 rise를 기다린 결과, tHIGH는 800 ns로 멀쩡 ③ `tSU;DAT min -25 ns VIOL x4` + `B5?` + `mask 0xA5` — target이 보낸 0xB5 중 0→1 전이인 b7·b5·b2·b0 4비트만 SDA가 30–70 % 사이에서 샘플됨. 수신기가 그 순간 0으로 보면 0x10 → 증상의 "가끔 0x10". ④ controller가 보내는 주소/레지스터 바이트는 멀쩡 — controller hold 0.3 µs + 상승 1.0 µs < tLOW라서. "target이 보내는 바이트만 깨진다"가 B3의 지문.
- **B4**: `0F A P | S 48R` — 포인터 쓰기와 읽기 사이 STOP(tBUF 1500 ns까지 측정됨). 타이밍은 golden과 동일 → **전기·타이밍은 무죄, 트랜잭션 구성 문제**. `READ_MISMATCH`는 보조 증거.
- **B5**: ① `glitch: SCL low pulse 60 ns ... receivers COUNT it` — 50 ns 필터를 넘음 ② `abandoned: byte 2 after 8 clocks, SDA=0 on last clock` — 0x0F의 마지막 비트는 1인데 와이어는 0, 9번째 클럭 없음 = controller가 arbitration lost로 멈춤 ③ 디코더는 글리치를 빼고 세므로 8클럭, target은 9개로 셌다 — 두 시각의 차이가 원인. 타이밍 숫자는 전부 정상 → "타이밍 표만 보면 못 찾는 버그".
- **B7**: ① `stretch: 12.000 ms ... controller timeout 10.000 ms -> EXCEEDED | SMBus ... within` — target은 합법, 설정이 불법 ② `abandoned ... after 1 clocks` + `SDA held low 17010 us at end, SCL=1` — stretch가 끝난 뒤 target은 b7=0을 내놓았고 controller는 이미 포기 → stuck SDA. 한 캡처에 **원인(timeout 설정)과 결과(stuck bus)**가 같이 있다.
- **한계 [미검증]**: 실제 MSO/analyzer export 포맷(예: 툴별 CSV)과는 다름, 2-threshold export 자체가 모델 가정. 측정 기준점(어느 파라미터를 30 %/70 % 중 어디서 재는지)은 UM10204 Fig. 38에 가깝게 잡은 모델 규칙. Fm+ 모드·10-bit 주소·Hs-mode·multi-byte PEC는 캡처로 돌려 보지 않음. 실보드 캡처로 검증 안 함.

## 9. 자주 틀리는 것

1. **scope 기본 10–90 %로 tr을 잼** → I2C는 30–70 %. RC에서 10–90 %는 2.197·RC, 30–70 %는 0.847·RC → 2.6배 크게 읽고 "위반"이라고 잘못 결론 내릴 수 있다.
2. **"주파수가 400 kHz니까 타이밍 OK"** → Fm 예산 여유 0. 50:50 duty면 tLOW 1.25 µs < 1.3 µs. tLOW·tHIGH를 따로 재야 한다.
3. **7-bit vs 8-bit 주소 표기** → 0x50 = 쓰기 바이트 0xA0. 데이터시트가 8-bit로 쓰는 경우가 흔하다.
4. **i2cdetect에 전부 숫자가 뜨면 장치가 많다고 생각** → SDA stuck low가 모든 주소에서 ACK처럼 보인 것. `--`와 `UU`도 구분.
5. **`i2cdetect -r`/`-q`를 무심코** → man page가 일부 칩을 잠그거나 손상시킬 수 있다고 경고 [8]. 쓰기 전용·상태기계 민감 장치가 있는 버스에선 주소를 지정해 `i2ctransfer`로.
6. **디코드 결과만 믿음** → analyzer는 threshold 하나로 자른 결과다. B3처럼 30–70 % 사이 샘플, B5처럼 50 ns 글리치는 디코드만 보면 "랜덤 데이터 오류"로 보인다. 아날로그 파형을 한 번은 본다.
7. **9-clock 복구를 쓰기 중에 함부로** → 추가 클럭이 장치에 쓰레기 값을 커밋할 수 있다 — 매 펄스 STOP 시도 방식으로 ([BV10 체인 B](2026-11-01_BV10_board_interface_tests.html), [9]).
8. **timeout을 늘리는 것으로 B7을 "해결"** → hang 검출이 늦어지고 버스 전체가 오래 막힌다. 그리고 timeout 뒤 복구가 없으면 여전히 stuck.
9. **Sr이 필요한 장치를 `i2c_master_send()` + `i2c_master_recv()`로** → 두 transfer = STOP. 한 `i2c_transfer()` 2-msg로 (B4).
10. **프로브가 버스를 바꾼다는 걸 잊음** → passive 프로브는 수 pF–십수 pF의 용량을 더하고 [추정], 긴 접지선은 링잉 착시를 만든다. 마진 버그는 "프로브 대면 재현율이 바뀜"으로 드러나기도 한다.

## 10. 출처

1. NXP UM10204, "I2C-bus specification and user manual", Rev. 7.0, 2021-10-01 — Table 10(VIL/VIH, VOL, IOL, tSP 50 ns, note 9 전원 off 시 비방해), Table 11(Sm/Fm/Fm+ 타이밍, 0.7/0.3 VDD 기준), §3.1.6 NACK 조건 5개, §3.1.8 arbitration, §3.1.9 clock stretching, combined format(Sr, NACK before Sr), §3.1.16 bus clear, 풀업 계산 절의 0.8473·RC 유도와 Rp(max)/Rp(min) (PDF 텍스트 추출로 확인) · https://www.nxp.com/docs/en/user-guide/UM10204.pdf
2. SMBus Specification Version 3.1, 2018-03-19 — Table 2 AC 사양(fSMB 10 kHz min, tTIMEOUT 25–35 ms, tLOW:SEXT 25 ms, tLOW:MEXT 10 ms, tHIGH max 50 µs, tPOR 500 ms), Note 3 timeout 동작, §4.3.5.1 back powering, PEC CRC-8 다항식 (PDF 텍스트 추출로 확인) · http://smbus.org/specs/SMBus_3_1_20180319.pdf
3. Linux kernel docs, "I2C/SMBUS Fault Codes" (EAGAIN arbitration lost, EBADMSG PEC, EBUSY, ENXIO 주소 NACK, ETIMEDOUT, EPROTO) · https://docs.kernel.org/i2c/fault-codes.html
4. Linux `drivers/i2c/busses/i2c-designware-common.c`(`i2c_dw_handle_tx_abort`: NOACK → -EREMOTEIO, ARB_LOST → -EAGAIN, "lost arbitration") 및 `i2c-designware-master.c`("controller timed out" 후 `i2c_recover_bus()`) · https://github.com/torvalds/linux/blob/master/drivers/i2c/busses/i2c-designware-common.c · https://github.com/torvalds/linux/blob/master/drivers/i2c/busses/i2c-designware-master.c
5. Linux `drivers/i2c/i2c-core-base.c` — `i2c_generic_scl_recovery()`, `RECOVERY_CLK_CNT 9`, "SCL is stuck low, exit recovery", SDA low 지속 시 -EBUSY · https://github.com/torvalds/linux/blob/master/drivers/i2c/i2c-core-base.c
6. Linux devicetree bindings `i2c/i2c.txt` (clock-frequency, i2c-scl-rising-time-ns, i2c-scl-falling-time-ns, i2c-sda-falling-time-ns, i2c-digital-filter-width-ns, scl-gpios/sda-gpios, multi-master, smbus) · https://www.kernel.org/doc/Documentation/devicetree/bindings/i2c/i2c.txt
7. i2ctransfer(8) man page (desc 문법, 한 transfer = S…Sr…P, "일부 장치는 Sr에서 상태 유지·STOP에서 리셋") · https://manpages.debian.org/testing/i2c-tools/i2ctransfer.8.en.html
8. i2cdetect(8) man page (-y -a -q -r -F -l, 기본 범위 0x08–0x77, `--`/`UU` 의미, -q/-r 경고) · https://manpages.debian.org/testing/i2c-tools/i2cdetect.8.en.html
9. Linux kernel docs, "Linux I2C fault injection" (i2c-gpio: lose_arbitration, incomplete_write_byte 등) · https://www.kernel.org/doc/html/latest/i2c/gpio-fault-injection.html
10. 내부 노트: [BV10 보드 인터페이스 테스트](2026-11-01_BV10_board_interface_tests.html) (풀업 표, 9-clock, 체인 B, §6.1), [BV13 S11](2026-11-01_BV13_board_debug_scenarios.html), [N06C C1](2026-11-01_N06C_plans_peripherals_soc_infra.html)
