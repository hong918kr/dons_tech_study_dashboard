# N06C · 연습① 저속 peripheral · SoC 기반 블록 — I2C · SPI · UART · GPIO · Timer · 인터럽트 컨트롤러 테스트 플랜
<!-- theme: pastel -->
> **묶음**: N06 검증 방법론 · 블록 연습① · **우선**: P0 ★ (C1 I2C · C6 인터럽트 컨트롤러) · **목록**: [N06 인덱스](2026-11-01_N06_validation_test_plan_design.html) · **이전/다음**: [N06B 커버리지·종료 기준](2026-11-01_N06B_coverage_metrics_exit_criteria.html) / [N06D 데이터 이동·메모리](2026-11-01_N06D_plans_dma_memory.html) · **관련**: [BV10 보드 인터페이스 테스트](2026-11-01_BV10_board_interface_tests.html) · [LP00A I/O primer](2026-10-31_LP00A_io_primer_pcie_ethernet_mipi.html) · [V8 I2C scan·recover](hmScreenPrep/problems/V8_i2c_scan_recover/index.html) · [N05C 레지스터·MMIO](2026-11-01_N05C_register_mmio_driver.html) · [LP07 PCIe 인터럽트](2026-10-31_LP07_interrupts_intx_msi_msix.html) · [BV12 shmoo/PVT](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) · [N06A 템플릿](2026-11-01_N06A_methodology_test_plan_template.html) · **코드**: onsitePrep/n06/c/ ([run.sh](onsitePrep/n06/c/run.sh) · [i2c_timing_check.c](onsitePrep/n06/c/i2c_timing_check.c) · [i2c_ctrl_fault_model.c](onsitePrep/n06/c/i2c_ctrl_fault_model.c) · [gic_model.c](onsitePrep/n06/c/gic_model.c))

## 0. TL;DR · 사용법

- **이 페이지**: "Design a test plan for an I2C controller / timer / interrupt controller" 같은 질문을 **IP 블록 검증 관점**으로 10분에 말하는 연습장. 블록 6개(C1 I2C ★ · C2 SPI · C3 UART · C4 GPIO/pinmux · C5 Timer/watchdog/RTC · C6 인터럽트 컨트롤러 ★), 블록마다 같은 9칸 틀.
- **BV10과의 차이**: BV10은 **보드 측정** 관점 — "이 보드에서 이 버스가 살아 있나"(풀업·ACK·JEDEC ID). 여기는 **"이 IP가 스펙의 모든 모드·에러·타이밍을 실리콘에서 지키나"** — 레지스터, 에러 경로, 타이밍 파라미터, 동시성, PVT. 보드 측정 내용은 링크만 하고 반복하지 않는다.
- **★ 외울 것 ① I2C 타이밍 예산에는 여유가 0**: UM10204 Table 11의 최소값을 더하면 **tLOW + tHIGH + tr(max) + tf(max) = 정확히 1/fSCL** — Sm 4.7+4.0+1.0+0.3 = 10 µs, Fm 1.3+0.6+0.3+0.3 = 2.5 µs, Fm+ 0.5+0.26+0.12+0.12 = 1.0 µs [1]. 그래서 **400 kHz를 50:50 duty로 만들면 tLOW 1.25 µs < 1.3 µs 위반**. 테스트 플랜의 타이밍 행은 "fSCL만"이 아니라 **tLOW·tHIGH·tHD;STA·tSU;STO·tBUF·tSU;DAT·tHD;DAT를 30/70% 기준으로 버스 부하(tr/tf) 코너별로** 잰다.
- **★ 외울 것 ② GIC 상태 기계**: inactive → pending → active → (active+pending) → EOI. **Edge**는 rising edge가 pending을 래치(여러 edge가 하나로 합쳐질 수 있음), **level**은 라인이 high인 동안 pending 유지(장치를 안 지우고 EOI하면 즉시 재진입 = storm). IAR 읽기 = ack, 1023 = spurious, EOI = priority drop + deactivate(EOImode 0) [2].
- **★ 외울 것 ③ 인터럽트 컨트롤러 테스트의 1순위는 "배선"**: 모든 peripheral IRQ 출력 → 맞는 INTID · 맞는 trigger type(edge/level) · 맞는 코어로 가는지 **소스별 1:1 연결 테스트**. GIC IP 자체는 라이선스·DV 완료일 확률이 높고, 실리콘 버그는 **통합(integration)**에서 난다.
- **실습 (실제 출력, §실습)**: ① I2C 타이밍 체커 — 컨트롤러 카운트 설정 7개 × tr/tf 코너 → UM10204 판정 **7/7 기대와 일치** ② I2C 에러 경로 fault-injection 모델 **9/9 PASS** ③ GIC 모델 directed 테스트 **12/12 PASS** + 인터럽트 latency Monte Carlo(평균은 1.55 µs인데 max 48 µs — "p99/max로 말하라"의 근거).
- **연습법**: 블록 하나 고르고 **10분 타이머** → §6 뼈대만 보고 소리 내어 말하기(녹음) → §2 매트릭스와 대조해서 빠진 카테고리 표시 → §7 꼬리 질문을 먼저 답해 보고 `<details>` 열기. ★ 블록(C1·C6)부터, 나머지는 매트릭스 훑기만.

## 1. 공통 틀 복습

자세한 건 [N06A](2026-11-01_N06A_methodology_test_plan_template.html). 여기선 한 장만.

| 단계 | 시간 | 말할 것 (영어 시작 문장) |
|---|---|---|
| ① clarify | 0:30 | "Before I start — is this our IP or licensed, which modes are in scope, and is there an FPGA image?" |
| ② 블록 그림 | 1:00 | "Let me draw where it sits: bus interface, FIFOs, protocol engine, IRQ, DMA, pads." |
| ③ 카테고리 매트릭스 | 3:00 | "I'll walk the categories: registers, function, config combos, boundaries, interrupts, errors, reset/power, stress, performance, PVT, soak, system." |
| ④ 우선순위·플랫폼 | 1:00 | "P0 is what blocks bring-up; FPGA for function, silicon for timing, analog and PVT." |
| ⑤ 통과 기준·커버리지 | 1:00 | "Every row has a measurable pass criterion and maps to a requirement ID." |
| ⑥ 자동화·회귀 | 1:00 | "Same C tests on FPGA and silicon through a HAL, results into a database, nightly regression." |
| ⑦ 리스크·A0 첫 주 | 1:00 | "On A0 the first week I'd do… and the biggest risk is…" |

| # | 카테고리 | 이 페이지 블록에서 흔한 예 |
|---|---|---|
| 1 | 레지스터 | reset 값, RW/RO/W1C, reserved, lock 비트 |
| 2 | 기본 기능 | 1 트랜잭션 / 1 프레임 / 1 토글 / 1 인터럽트 |
| 3 | 설정 조합 | 속도·모드·폭·prescaler·polarity 매트릭스 |
| 4 | 경계·corner | FIFO 0/1/full, 최대 길이, counter wrap |
| 5 | 인터럽트 | 발생·mask·W1C·중복/누락 없음 |
| 6 | 에러·negative | NACK, overrun, framing, timeout, 잘못된 설정 |
| 7 | 리셋·전원 | 전송 중 reset, clock gating, suspend/resume, wake |
| 8 | 동시성·stress | 여러 인스턴스 동시, 다른 master와 버스 경합, IRQ 폭주 |
| 9 | 성능 | throughput, latency, CPU 부하 |
| 10 | PVT·margin | 전압·온도·버스 부하 코너에서 타이밍 |
| 11 | soak | 10^6–10^9 트랜잭션, 수 시간 |
| 12 | 시스템·interop | 실제 센서·flash·PMIC, 실제 드라이버 |

## C1 · I2C controller (★ 가장 깊게)

### 0. 🔰 이 블록이 뭔가

- **비유**: 교실에 깔린 **공용 전화선 두 가닥**. 한 가닥(SCL)은 "박자", 다른 가닥(SDA)은 "말". 줄은 평소 저절로 위(high)에 걸려 있고, **누구든 아래로 당길 수만 있다**(open-drain + 풀업). 선생님(controller)이 "50번!"(주소)을 부르면 50번 학생(target)만 9번째 박자에 줄을 당겨 "네"(ACK). 학생이 "잠깐만요"라며 박자 줄을 붙잡는 게 clock stretching, 두 선생님이 동시에 말하면 먼저 "높게" 말하려다 줄이 낮은 걸 본 쪽이 물러나는 게 arbitration.
- **정확한 정의**: SoC 안의 I2C controller IP = APB 레지스터 슬레이브 + TX/RX FIFO + 프로토콜 FSM(START, 주소, R/W, 데이터, ACK/NACK, Sr, STOP) + SCL 생성기(high/low 카운트, clock synchronization) + SDA hold 지연 + 입력 spike filter + (선택) target 모드 주소 비교기 + 인터럽트 + (선택) DMA 핸드셰이크. 핀은 open-drain pad + 외부 풀업, pinmux로 연결. UM10204 Rev.7(2021)부터 master/slave 대신 **controller/target** 용어를 쓴다 [1].
- **속도 모드** [1]: Standard 100 kHz · Fast 400 kHz · Fast-mode Plus 1 MHz (여기까지 양방향, Table 11 대상) · High-speed 3.4 Mbit/s · Ultra Fast-mode 5 Mbit/s(단방향, push-pull). 보통 SoC I2C IP 범위는 Sm/Fm/Fm+ [추정].

```text
            APB/AXI-lite (CPU, 레지스터)                     DMA req/ack (선택)
                 │                                                 │
  ┌──────────────▼─────────────────────────────────────────────────▼─────────┐
  │ I2C controller IP                                                          │
  │  CTRL/CFG ─ SCL_HCNT/LCNT ─ SDA_HOLD ─ TIMEOUT ─ TAR(대상 주소)            │
  │  TX FIFO ─►┌───────────────────┐   ┌──────────────┐                        │
  │            │ protocol FSM      │◄─►│ SCL gen +    │── scl_oe ─┐            │
  │  RX FIFO ◄─│ S/Sr/addr/ACK/P   │   │ clock sync   │◄─ scl_in ─┤ spike      │
  │            │ arb-lost detect   │   └──────────────┘           │ filter     │
  │  STATUS(W1C)/INT_EN ──► irq ───┼──► GIC SPI #n                │ (tSP 50ns) │
  │  target-mode addr match ◄──────┘── sda_oe / sda_in ───────────┘            │
  └──────────────────────────────────────────────┬───────────────────────────┘
                                                 │ pinmux · open-drain pad · IO 전압 도메인
                                   VDD ─┬─ Rp ───┴── SCL ───────┬──────────┬──
                                        └─ Rp ────── SDA ───────┤          │
                                                         센서(target)  EEPROM · PMIC · 다른 controller
```

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| SCL / SDA | Serial Clock / Serial Data | 박자 줄 / 말 줄 | 둘 다 open-drain → "놓았다"와 "high다"가 다름 |
| controller / target | (구 master / slave) | 부르는 쪽 / 대답하는 쪽 | Rev.7 용어. 면접에서 둘 다 알아들어야 함 |
| ACK / NACK | Acknowledge | 9번째 클럭에서 SDA low = 들었음 | NACK 경로가 에러 테스트의 절반 |
| START / Sr / STOP | (repeated) START | SCL high 동안 SDA 하강 / 상승 | 레지스터 읽기는 Sr이 필요한 장치가 많음 |
| clock stretching | — | target이 SCL을 low로 붙잡아 대기시킴 | controller가 SCL을 다시 읽지 않으면 깨짐 |
| arbitration | — | 여러 controller가 동시에 시작할 때 비트 단위 승부 | multi-controller 지원 IP면 필수 테스트 |
| tHD;STA 등 | UM10204 Table 11 파라미터 | START 후 첫 클럭까지 최소 시간 등 | 측정 가능한 통과 기준의 원천 |
| tSP | spike suppression | 50 ns 미만 글리치 무시 (Fm/Fm+) | 긴 하네스 노이즈 → 여분 클럭 |
| HCNT/LCNT | (일반 명칭) | SCL high/low를 IP 클럭 몇 개로 만들지 | 계산 실수 = 타이밍 위반 1순위 |
| SMBus | System Management Bus | I2C 친척, timeout·PEC 규정 | PMIC/배터리류 interop |

**UM10204 Rev.7 Table 11 — 외워 둘 숫자** [1] (모두 VIL=0.3 VDD / VIH=0.7 VDD 기준)

| 파라미터 | 의미 | Sm 100k | Fm 400k | Fm+ 1M |
|---|---|---|---|---|
| tHD;STA (min) | (반복) START hold — 이후 첫 클럭 | 4.0 µs | 0.6 µs | 0.26 µs |
| tLOW (min) | SCL low | 4.7 µs | 1.3 µs | 0.5 µs |
| tHIGH (min) | SCL high | 4.0 µs | 0.6 µs | 0.26 µs |
| tSU;STA (min) | repeated START setup | 4.7 µs | 0.6 µs | 0.26 µs |
| tHD;DAT (min) | data hold (I2C 장치) | 0 | 0 | 0 |
| tSU;DAT (min) | data setup | 250 ns | 100 ns | 50 ns |
| tr (max) | 상승 시간 30→70% | 1000 ns | 300 ns (min 20) | 120 ns |
| tf (max) | 하강 시간 70→30% | 300 ns | 300 ns | 120 ns |
| tSU;STO (min) | STOP setup | 4.0 µs | 0.6 µs | 0.26 µs |
| tBUF (min) | STOP → 다음 START 사이 bus free | 4.7 µs | 1.3 µs | 0.5 µs |
| Cb (max) | 라인당 버스 용량 | 400 pF | 400 pF | 550 pF |
| tVD;DAT · tVD;ACK (max) | SCL low → 데이터/ACK 유효 | 3.45 µs | 0.9 µs | 0.45 µs |
| tSP | 억제할 spike 폭 | — | 0–50 ns | 0–50 ns |

- **예산 계산 [계산]**: tLOW + tHIGH + tr(max) + tf(max) = Sm 10.0 µs · Fm 2.5 µs · Fm+ 1.0 µs = 각 모드 최대 주파수의 주기 그대로. → 컨트롤러가 최대 주파수에서 spec을 지키려면 **edge가 느릴 땐 주파수가 내려가는 걸 받아들이고, edge가 빠를 땐 low 구간을 spec보다 길게** 잡아야 한다. 이게 C1의 핵심 실리콘 리스크.
- 보드 쪽 풀업·tr 계산·9-clock bus clear는 [BV10 §2.1](2026-11-01_BV10_board_interface_tests.html)에 있음 — 여기선 반복 안 함. bus clear 근거는 UM10204 §3.1.16 (SDA stuck → 9 클럭, SCL stuck → HW reset/전원 사이클) [1].

### 1. 먼저 물어볼 것 (clarify)

- "Is this our own RTL or a licensed IP like a DesignWare-class controller? If licensed, DV and the vendor already covered the protocol FSM, so I focus on integration, configuration, timing and PVT." → 가정: **라이선스 IP + 자체 통합** [추정].
- "Which roles and speeds are in scope — controller only, or target mode too? Sm/Fm/Fm+, any Hs-mode? 10-bit addressing? Multi-controller?" → 가정: controller+target, Sm/Fm/Fm+, 7-bit 주력·10-bit 지원, multi-controller는 P1.
- "How many instances, and what's on each bus on the reference board — PMIC, camera serializer config, EEPROM, temperature sensors?" → 가정: 인스턴스 6–8개, 일부는 안전 관련 PMIC [추정].
- "Is there DMA support and an SMBus/timeout feature?" → 가정: DMA 있음, SCL-low timeout 레지스터 있음.
- "What's the input clock and is the SCL count calculation done by firmware or by a lookup in the driver?" → 타이밍 행의 기대값을 계산하려면 필요.
- "Do we have an FPGA image with this IP and a way to put an external target on it?" → 가정: 있음, 단 pad·타이밍은 실리콘에서만 의미.

### 2. 테스트 매트릭스

| 카테고리 | 테스트 항목 | 조건/파라미터 | 플랫폼 | 통과 기준 | 우선 |
|---|---|---|---|---|---|
| 레지스터 | reset 값 · RW mask · W1C · reserved | 인스턴스 전부, walking-1 (N05C C8) | 둘 다 | 문서 표와 100% 일치, W1C는 1 쓰기로만 clear | P0 |
| 기본 기능 | 7-bit write/read, Sr 레지스터 읽기 | 실제 EEPROM/센서, Sm/Fm | 둘 다 | 1000 트랜잭션 data compare 0 mismatch, Sr 확인(analyzer) | P0 |
| 설정 조합 | 속도 × 역할 × 주소 폭 × FIFO threshold × DMA on/off | Sm/Fm/Fm+ × ctrl/target × 7/10-bit | 둘 다 | 조합당 10^4 바이트 compare 0 오류 | P1 |
| 경계 | 길이 0 (주소만), 1 바이트, FIFO depth, depth+1, 최대 길이 | read/write 각각 | 둘 다 | 바이트 수 정확, 마지막 read 바이트는 NACK 후 STOP | P1 |
| 경계 | 10-bit 주소 `11110xx` 접두 + reserved 주소 처리 | 0x000/0x3FF, general call 0x00, device ID | FPGA → Si | analyzer 디코드가 스펙과 일치, reserved 주소는 정책대로 거부 | P2 |
| 인터럽트 | DONE/RX_FULL/TX_EMPTY/NACK 등 각 원인 | mask on/off, W1C | 둘 다 | 원인당 IRQ 정확히 1회, mask 시 0회, clear 후 재발생 없음 | P0 |
| 에러 | NACK 주소 / NACK 데이터 k번째 | fault injector 또는 없는 주소 | 둘 다 | 해당 status 비트만 set, STOP 생성, TX FIFO flush, 다음 전송 정상 | P0 |
| 에러 | arbitration lost | 2번째 controller(SoC 다른 인스턴스) 동시 START, bit 위치 스윕 | Si (FPGA도 가능) | ARB_LOST 100% 검출, 진 쪽 SDA 구동 즉시 중단, 승자 데이터 손상 0 | P1 |
| 에러 | clock stretching 길이 스윕 + SCL-low timeout | 0 µs … timeout×2 | 둘 다 | timeout 미만은 정상 완료, 초과는 TIMEOUT 비트 ±10% 이내 시각, hang 0 | P0 |
| 에러 | SDA stuck / 전송 중 SoC 리셋 → bus clear | i2c-gpio fault injection, 리셋 위치 스윕 | Si | 9 클럭 이내 복구 100%, 복구 중 target에 의도치 않은 write 0 | P0 |
| 리셋·전원 | 전송 중 IP soft reset / clock gate / suspend-resume | 1000회 | Si | 리셋 후 레지스터 reset 값, 버스 idle, 다음 전송 성공 1000/1000 | P1 |
| 동시성 | 모든 인스턴스 동시 + DMA + 다른 IRQ 부하 | 1시간 | Si | 인스턴스별 compare 오류 0, IRQ 누락 0 (시퀀스 번호) | P1 |
| 성능 | 실효 throughput · 바이트 간 gap · ISR/DMA CPU 부하 | Fm+ 연속 read | Si | 바이트 간 idle ≤ 1 SCL 주기 [추정], 효율 ≥ 이론의 85% [추정] | P2 |
| 타이밍 (PVT) | fSCL · tLOW · tHIGH · tHD;STA · tSU;STA · tSU;STO · tBUF · tSU;DAT · tHD;DAT | 모드 3개 × Cb(빠른/느린 edge) × 전압 ±5% × 온도 3점 | **Si** | Table 11 전부 충족, 최소 margin ≥ 10% [추정] — 30/70% 기준 측정 | P0 |
| 타이밍 | tVD;DAT/ACK (target 모드일 때 우리 IP가 출력) | 모드별 | Si | ≤ 0.9 µs (Fm) 등 Table 11 max | P1 |
| margin | spike filter: 폭 스윕 글리치 주입 | 20–80 ns 펄스 on SCL/SDA | Si | < 50 ns 무시, 충분히 긴 펄스는 검출 (filter 폭 실측 기록) | P2 |
| margin | pad VOL @ 정격 sink 전류, 입력 threshold | 3 mA (Sm/Fm), 20 mA (Fm+) | Si | VOL ≤ 0.4 V [추정], VIL/VIH 0.3/0.7 VDD | P2 |
| soak | 연속 트랜잭션 + 랜덤 길이/주소 | 10^7 트랜잭션, 온도 hot | Si | 오류 0 → rule of three로 실패율 < 3e-7 (95%) | P1 |
| 시스템 | 실제 드라이버(Linux i2c-core) + 실제 센서·PMIC·serializer | i2cdetect/i2ctransfer, probe | Si | 기대 주소맵 일치, 드라이버 probe 100%, 부팅 1000회 0 실패 | P0 |
| 시스템 | multi-controller / target 모드 interop | Linux slave-testunit 또는 SoC 다른 인스턴스 | 둘 다 | 명령별 응답 일치, Host Notify 수신 | P2 |

- 코드화: 에러 행 4개를 table-driven fault-injection 테스트로 만든 것 → [i2c_ctrl_fault_model.c](onsitePrep/n06/c/i2c_ctrl_fault_model.c), 타이밍 행의 판정 로직 → [i2c_timing_check.c](onsitePrep/n06/c/i2c_timing_check.c) (§실습).

### 3. 우선순위 · A0 첫 주

- **P0 bring-up (첫 주)**:
  1. 레지스터 reset 값·RW sanity (모든 인스턴스) — clock/reset/pinmux 통합이 맞는지 가장 싸게 확인.
  2. PMIC·온도센서 버스에서 Sm으로 ACK + ID 읽기 → 이게 안 되면 보드 전원 관리 자체가 막힘.
  3. Fm/Fm+에서 analyzer + scope로 **타이밍 1차 측정**(tLOW·tHIGH·tHD;DAT) — 카운트 계산 버그는 첫 주에 잡아야 driver 기본값을 고친다.
  4. NACK·timeout·bus clear 경로 — hang은 이후 모든 자동 테스트를 망치므로 초기에.
- **P1**: 설정 조합 전체, arbitration, 리셋·suspend 1000회, 동시성 1시간, soak, 실제 드라이버 부팅 반복.
- **P2**: 10-bit·reserved 주소, spike filter 폭, pad 전기 특성, 성능 효율, target 모드 interop.
- **일부러 안 하는 것**: 프로토콜 FSM의 모든 상태 전이 조합 · 랜덤 시퀀스 → DV가 시뮬레이션에서 coverage로 이미 닫았다(라이선스 IP면 vendor도). 실리콘에선 **DV가 못 보는 것** — 실제 pad·풀업·버스 용량에서의 타이밍, 클럭·리셋 통합, 실제 장치 interop, PVT, 장시간 — 에 시간 쓴다. 대신 DV coverage 리포트를 받아 "구멍"만 확인.

### 4. 장비 · 셋업 · 관찰 지점

```text
                ┌────────── SoC (DUT) ──────────┐
   JTAG/UART ───┤ CPU: bare-metal test / Linux   │
   results      │ I2C0 (controller) ─┐           │      ┌── protocol analyzer (decode + timing)
                │ I2C1 (target mode)─┼── 내부 or ─┼──────┼── scope 2ch: SCL/SDA, 30/70% 측정
                │ GPIO (fault inj.) ─┘  보드 점퍼 │      ├── 가변 Cb 지그 (커패시터 뱅크) / Rp 선택
                └───────────────────────────────┘      ├── 실제 target: EEPROM, 온도센서, PMIC
                                                         ├── I2C exerciser (NACK·stretch 주입 가능한 장비) [추정]
                                                         └── 온도 챔버 + 가변 전원 (PVT)
```

- **상대 장치**: 실제 EEPROM·센서(interop), SoC의 다른 I2C 인스턴스를 **target 모드**로 걸어 칩 내부 loopback(장비 없이 nightly 가능), NACK·stretching을 원하는 위치에 넣을 수 있는 exerciser 또는 FPGA 기반 가짜 target.
- **Linux 도구**: `i2cdetect -y N`(주소 스캔), `i2ctransfer`(Sr 포함 임의 트랜잭션), i2c-gpio **fault injection** debugfs — `scl`/`sda` 강제 low, `incomplete_address_phase`, `incomplete_write_byte`, `lose_arbitration`(최대 100 ms SDA low), `inject_panic` [3]; **slave-testunit** 백엔드 — 보드를 원격 I2C 장치로 만들어 multi-controller·SMBus Host Notify·Alert 테스트 (테스트 전용, 양산 빌드 금지) [4].
- **관찰 지점**: STATUS raw/masked, TX/RX FIFO level, abort source류 레지스터, 바이트 카운터, 인터럽트 카운터(드라이버 `/proc/interrupts`), analyzer의 디코드 + 타이밍 통계(min/max), scope의 30/70% 측정(기본 10/90%에서 바꿀 것 — BV10 §7).

### 5. 잘 터지는 버그

1. **카운트 계산이 edge 시간을 무시** → 증상: 실험실 짧은 배선(빠른 edge)에선 일부 target이 Fm에서 간헐 NACK, 하네스 붙인 차량 보드에선 정상. 원인: 50:50 duty 또는 spec 최소값 그대로의 LCNT → tLOW < 1.3 µs. 잡는 법: 타이밍 행을 **빠른 edge·느린 edge 두 코너**에서 측정 (§실습 F1·F2가 바로 이 경우).
2. **SDA hold = 0** → 증상: 온도·Cb에 따라 간헐 오류, analyzer에 가짜 START/STOP. 원인: SDA가 SCL 하강과 동시에 바뀌어 느린 tf에서 target이 SCL high 중 SDA 변화로 인식. 잡는 법: tHD;DAT 측정(최소 0이지만 하강 edge가 30% 아래로 떨어진 뒤 바뀌어야 — Table 11 주석 [2]) + 최대 tf 코너.
3. **clock stretching을 ACK 뒤에서만 지원**(비트 단위 stretch 미지원) 또는 SCL 재확인 없음 → 특정 센서에서만 데이터 깨짐. 잡는 법: stretch 위치·길이 스윕 행.
4. **timeout 없음/잘못된 단위** → target이 SCL을 붙잡으면 CPU가 영원히 polling. 잡는 법: stretch > timeout 행 + 테스트 프레임워크 watchdog.
5. **W1C 레이스**: ISR이 status를 read-modify-write로 지워 다른 에러 비트까지 지움 → NACK이 "DONE"으로 보고됨. 잡는 법: 원인별 IRQ 정확히 1회 + status 정확값 비교 (fault model의 "status=정확히 0x04" 체크).
6. **Sr 대신 STOP+START** → 레지스터 포인터 읽기 장치에서 엉뚱한 값. 잡는 법: analyzer로 Sr 확인하는 기본 기능 행.
7. **SoC warm reset 후 bus stuck** → 부팅 중 PMIC 접근 실패로 부팅 hang. 원인: target이 바이트 중간에 멈춤 + 부팅 코드에 bus clear 없음. 잡는 법: 리셋 위치 스윕 × 1000회 + 부팅 첫 단계 bus clear 요구사항화.
8. **pad가 진짜 open-drain이 아님**(push-pull high 구간) 또는 IO 전압 도메인 불일치 1.8/3.3 V → stretching target과 contention, 전류 급증. 잡는 법: pad 전기 행 + scope 파형(high로 "끌어올리는" 빠른 상승이 보이면 의심).

### 6. 10분 영어 답 뼈대

1. "Let me clarify scope first: I'll assume licensed controller IP integrated by us, controller and target roles, Standard, Fast and Fast-mode Plus, with DMA and a timeout register."
2. "Picture: APB registers and FIFOs, a protocol state machine, an SCL generator with high and low counts, spike filters, open-drain pads through pinmux, an interrupt to the GIC, DMA handshake."
3. "Since DV already closed the state machine, silicon validation focuses on integration, real-bus timing, error recovery, interop and PVT."
4. "Registers first: reset values, read-write masks, write-one-to-clear status, on every instance."
5. "Function: writes and reads with repeated START against real devices, across speed, role, address width, FIFO threshold and DMA."
6. "Boundaries: zero-length, one byte, FIFO depth and depth plus one, last-byte NACK on reads."
7. "Interrupts: each cause fires exactly once, masks work, clear works."
8. "Errors are where I spend the most: address NACK, data NACK at byte k, arbitration loss, clock stretching past the timeout, SDA stuck after a reset — each must set the right status bit, release the bus, flush the FIFO, and the next transfer must succeed."
9. "Timing is the silicon-only part: with an analyzer and scope at 30/70 percent thresholds, measure tLOW, tHIGH, START hold, STOP setup, bus-free, data setup and hold against UM10204, at fast-edge and slow-edge bus loads, voltage and temperature corners."
10. "The budget has zero slack — at 400 kHz, minimum tLOW plus tHIGH plus max rise and fall is exactly 2.5 microseconds — so a 50 percent duty cycle violates tLOW. That's the first thing I'd check on A0."
11. "Reset and power: reset mid-transfer, suspend-resume, a thousand cycles, bus clear with nine clocks."
12. "Stress: all instances concurrently with DMA and other interrupts; soak ten million transactions."
13. "System: the Linux driver probing the real PMIC, sensors and serializers; boot loops."
14. "Priorities: P0 is register sanity, PMIC access, first timing measurement and the error paths; P1 combos, arbitration, soak; P2 characterization."
15. "Pass criteria are numeric — zero compare errors in ten to the seven transactions, every Table 11 parameter met with margin, recovery in nine clocks one hundred percent."
16. "Automation: the same C tests run on FPGA and silicon through a HAL, with an on-chip loopback using a second instance in target mode so it runs nightly without equipment."
17. "Biggest risk: timing at corners and reset-induced bus hangs; first week on A0 I'd measure timing and prove recovery."

### 7. 꼬리 질문

<details>
<summary>Q1. The analyzer says 400 kHz looks fine, but one sensor NACKs intermittently in Fast-mode only. What do you check?</summary>

- **EN**: "Frequency being right says nothing about the split between low and high. At 400 kHz the minimum tLOW plus tHIGH plus maximum rise and fall time adds up to exactly the 2.5 microsecond period, so a controller programmed with a 50 percent duty cycle gives about 1.25 microseconds low, below the 1.3 minimum. I'd measure tLOW, tHIGH and data hold at 30/70 percent on the scope, at the bus load where it fails, and compare against Table 11. If tLOW is short, the fix is the driver's count calculation, and I'd add that measurement to the timing regression at both edge-speed corners."
- 한국어 핵심: 주파수 ≠ 타이밍. Fm 예산 여유 0 → 50:50 duty는 tLOW 위반. 30/70%로 tLOW·tHD;DAT 실측.

</details>

<details>
<summary>Q2. How do you test arbitration loss without two external masters?</summary>

- **EN**: "The SoC usually has several I2C instances, so I wire two of them to the same bus on the board or through a test mux, start both at the same time from two cores or with a hardware trigger, and sweep which address bit differs. The loser must flag arbitration lost, stop driving SDA within that bit, and the winner's data must be intact. On Linux there's also the i2c-gpio fault injector's lose_arbitration, which pulls SDA low during a transfer. Repeat a few thousand times with random offsets, because arbitration timing is exactly the kind of race simulation underexplores."
- 한국어 핵심: SoC 인스턴스 2개를 같은 버스에 + 비트 위치 스윕, 또는 fault injector lose_arbitration. 진 쪽 즉시 SDA 해제·승자 데이터 무손상.

</details>

<details>
<summary>Q3. What do you do after a SoC reset leaves the bus stuck?</summary>

- **EN**: "Per the I2C spec, if SDA is stuck low the controller sends up to nine clocks, because the target can be holding the line for at most the rest of a byte plus the ACK; then it generates a STOP. If SCL is stuck low, clocks can't help, so it's a hardware reset or power cycle of the target. In the plan I sweep the reset point across the transaction a thousand times and require recovery every time, and I check that the recovery clocks don't turn into an unintended write — the Linux fault-injection docs specifically warn about that for incomplete writes. And I make 'bus clear at boot' a requirement, not an option."
- 한국어 핵심: SDA stuck → 9클럭 + STOP, SCL stuck → HW reset/전원. 리셋 위치 스윕 1000회 + 의도치 않은 write 0 확인 + 부팅 시 bus clear 요구사항.

</details>

<details>
<summary>Q4. Which parts would you run on FPGA and which only on silicon?</summary>

- **EN**: "On FPGA I run everything that's logic: registers, the functional and config matrix, interrupts, error paths with a fake target that injects NACK and stretching, and the DMA path. The FPGA's pads, clock and edge rates aren't the chip's, so timing parameters, spike filtering, pad drive, PVT and interop with the real PMIC only mean something on silicon. The test code is the same, through a HAL; only the timing checks and timeouts are platform-scaled."
- 한국어 핵심: FPGA = 로직(레지스터·기능·에러·DMA), 실리콘 = 타이밍·pad·filter·PVT·실장치. 같은 C 코드, timeout만 스케일.

</details>

<details>
<summary>Q5. How do you know you're done with this block?</summary>

- **EN**: "Every requirement ID — each mode, each error path, each Table 11 parameter — has a passing result on silicon at the agreed corners, on at least three parts including corner-lot material if we have it. Soak has run ten million transactions with zero errors, which bounds the failure rate below about three in ten million at 95 percent confidence. No open severity-one or two bugs, and every erratum has a verified driver workaround."
- 한국어 핵심: 요구사항 ID 100% + 코너·여러 칩 + soak 10^7 (rule of three) + S1/S2 0 + errata workaround 확인.

</details>

### 8. Don 연결

- "At Solidigm I verified I2C and SPI on the FPGA prototype of our PCIe Gen6 SSD controller before RTL freeze — [숫자 채우기: tests / bugs found] — and at Apple I root-caused I2C and SPMI failures on wireless chipsets with a scope and protocol analyzer, so measuring tLOW and hold time at 30/70 percent and correlating with the bus load is something I've actually done."

## C2 · SPI controller

### 0. 🔰 이 블록이 뭔가

- **비유**: 전화 교환원(controller)이 **각 집마다 전용 초인종 선(CS)**을 갖고 있는 구조. 초인종을 누른 집하고만 대화하고, 말하면서 동시에 듣는다(full duplex). 주소가 없으니 "누구랑 말하나"는 CS가 정한다.
- **정확한 정의**: SPI controller IP = 레지스터 + TX/RX FIFO + shift register + SCLK 생성(분주) + CS 제어(자동/수동, setup/hold/gap 카운트) + mode(CPOL/CPHA) + 워드 크기 + MSB/LSB first + RX sample delay + 인터럽트 + DMA. QSPI/OSPI(멀티 IO, flash XIP)는 범위를 따로 확인. 4개 mode 표는 [BV10 §2.2](2026-11-01_BV10_board_interface_tests.html).

```text
 APB ─► regs ─► TX FIFO ─► shift ─► MOSI ──────────────► flash / ADC / IMU / serializer
                 RX FIFO ◄─ shift ◄─ MISO ◄── (sample delay tap)
                 SCLK gen (div, CPOL/CPHA) ─► SCLK
                 CS ctrl (setup/hold/gap) ─► CS0..CSn
 irq ─► GIC     DMA req (TX/RX threshold)
```

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| CPOL / CPHA | Clock polarity / phase | 쉴 때 SCLK 레벨 / 어느 edge에서 샘플 | 4 mode 전부 상대 장치로 검증 |
| CS setup/hold/gap | — | CS 내린 뒤 첫 클럭까지, 마지막 클럭 뒤 CS 올릴 때까지, 프레임 사이 | 장치 datasheet 최소값과 비교 |
| RX sample delay | — | MISO를 몇 tap 늦게 샘플할지 | 고속에서 왕복 지연 보상 — 미니 shmoo |
| TX underflow / RX overflow | — | 보낼 게 없는데 클럭 / 받은 걸 못 꺼냄 | 에러 경로 + DMA 성능 |

### 1. 먼저 물어볼 것 (clarify)

- "Standard SPI only, or quad/octal with XIP from flash? That changes the plan a lot." → 가정: 표준 SPI + 별도 QSPI 블록은 범위 밖.
- "Controller only, or target mode too?" → 가정: 둘 다, target은 P1.
- "Max SCLK and what devices are on it — NOR flash, IMU, serializer config?" → 가정: 50 MHz [추정], NOR flash + IMU.
- "Word sizes, FIFO depth, DMA?" → 가정: 4–32 bit, depth 32 [추정], DMA 있음.
- "Does it have an internal loopback mode?" → 가정: 있음(없으면 MOSI–MISO 점퍼).

### 2. 테스트 매트릭스

| 카테고리 | 테스트 항목 | 조건/파라미터 | 플랫폼 | 통과 기준 | 우선 |
|---|---|---|---|---|---|
| 레지스터 | reset 값 · RW · W1C | 인스턴스 전부 | 둘 다 | 문서와 100% 일치 | P0 |
| 기본 기능 | 내부 loopback data compare | mode 0, 기본 속도, 8-bit | 둘 다 | 10^4 B 0 mismatch | P0 |
| 설정 조합 | mode 0–3 × 워드 4–32 × MSB/LSB × CS 0..n | 외부 loopback + 실장치 | 둘 다 | 조합당 10^5 B 0 오류, analyzer에서 edge·bit 순서 일치 | P1 |
| 경계 | 길이 1 워드, FIFO depth, depth+1, 최대 DMA 길이 | TX/RX/full duplex | 둘 다 | 바이트 수 정확, CS가 전송 전체 동안 유지 | P1 |
| 인터럽트 | TX threshold / RX threshold / DONE | threshold 스윕 | 둘 다 | FIFO level = threshold에서 정확히 발생, 원인당 1회 | P0 |
| 에러 | RX overflow (일부러 안 읽음), TX underflow (target 모드) | 강제 | 둘 다 | 해당 status set, 데이터 손상 범위가 문서대로, 복구 후 정상 | P1 |
| 에러 | 잘못된 설정: 분주 0, 워드 크기 범위 밖 | negative | FPGA → Si | 거부되거나 문서된 동작, hang 0 | P2 |
| 리셋·전원 | 전송 중 soft reset / clock gate / suspend | 1000회 | Si | CS high로 복귀, 다음 전송 성공 1000/1000 | P1 |
| 동시성 | 모든 인스턴스 + DMA + DDR 부하 | 1시간 | Si | underflow/overflow 0, compare 0 오류 | P1 |
| 성능 | DMA 연속 throughput | 최대 SCLK | Si | ≥ SCLK/8 B/s의 90% [추정] (50 MHz → 6.25 MB/s 이론) | P1 |
| 타이밍 | CS setup/hold/gap, SCLK duty, MOSI 출력 지연 | scope, 속도별 | Si | 장치 datasheet 최소값 이상, duty 45–55% [추정] | P0 |
| PVT·margin | RX sample delay tap × SCLK × V/T shmoo | 최대 속도 근처 | Si | 모든 코너에서 통과 창 ≥ 2 tap [추정], 기본 tap이 창 중앙 | P1 |
| soak | 랜덤 길이·패턴 | 10^9 B, hot | Si | 0 오류 | P1 |
| 시스템 | NOR flash: JEDEC ID 0x9F, erase/program/read CRC; IMU WHO_AM_I | Linux MTD / spidev_test | Si | ID 일치, 전체 flash CRC 일치, 드라이버 probe 100% | P0 |

### 3. 우선순위 · A0 첫 주

- **P0**: 레지스터 → 내부 loopback → flash JEDEC ID (부트 flash가 SPI면 이게 부팅 경로) → CS·SCLK 타이밍 1차 측정 → 인터럽트 threshold.
- **P1**: mode·워드·CS 매트릭스, DMA 성능, sample delay shmoo, 리셋·동시성·soak. **P2**: 잘못된 설정 negative, target 모드 interop.
- **안 하는 것**: shift register 비트 레벨 동작 전 조합(DV), QSPI XIP(별도 블록 플랜).

### 4. 장비 · 셋업 · 관찰 지점

- 4ch scope(SCLK·MOSI·MISO·CS)로 CS setup/hold, logic analyzer SPI 디코드, MOSI–MISO 점퍼(외부 loopback), 실제 NOR flash·IMU, SoC 다른 SPI 인스턴스를 target 모드로.
- Linux: `spidev_test -D /dev/spidevX.Y -s <Hz> -l -v`(loopback·속도) [BV10 §3.3 참고], MTD(`mtd_debug`, `flash_erase`) [추정: 도구 이름은 mtd-utils 기준].
- 관찰: FIFO level, overflow/underflow 비트, DMA 완료 카운트, CS 상태 레지스터.

### 5. 잘 터지는 버그

1. **최대 속도에서 MISO 샘플 시점 부족** → 25 MHz 통과·50 MHz 간헐 오류. sample delay shmoo 행이 잡음.
2. **CS가 FIFO가 비는 순간 올라감**(자동 CS + 느린 DMA) → 장치는 명령이 끊긴 걸로 인식. DMA 부하 동시성 행.
3. **CPHA=1 첫 비트 누락**/mode 2·3에서 idle 레벨 틀림 → 특정 장치만 실패. mode 매트릭스.
4. **RX overflow 미검출** → 조용한 데이터 손실. 에러 행(일부러 안 읽기).
5. **LSB-first + 워드 크기 비-8배수에서 비트 순서 오류**. 조합 행.
6. **soft reset 후 CS가 low로 남음** → 다음 장치 접근 충돌. 리셋 행.

### 6. 10분 영어 답 뼈대

1. "Scope: standard SPI controller with DMA, controller and target roles, up to 50 MHz; quad and XIP are a separate plan."
2. "Picture: registers, TX/RX FIFOs, shift register, clock divider with CPOL/CPHA, chip-select timing counters, RX sample delay, IRQ and DMA."
3. "Registers and internal loopback first — cheap proof the clock, reset and pinmux integration work."
4. "Function matrix: four modes, word sizes, bit order, each chip select, against an external loopback and real devices."
5. "Boundaries: one word, FIFO depth and depth plus one, max DMA length, CS held across the whole transfer."
6. "Interrupts at exact FIFO thresholds; errors: RX overflow, TX underflow in target mode, bad configuration."
7. "Silicon-only: chip-select setup, hold and gap against the device datasheet, and a sample-delay times frequency shmoo across voltage and temperature to show the passing window and that the default tap sits in the middle."
8. "Performance: DMA throughput close to SCLK over eight, under DDR load."
9. "Reset and suspend mid-transfer, concurrency of all instances, soak ten to the nine bytes."
10. "System: flash JEDEC ID, full erase-program-read with CRC, IMU WHO_AM_I through the Linux driver."
11. "P0 is loopback, flash ID and CS timing — the boot path may depend on it. Risk is max-speed sampling margin."

### 7. 꼬리 질문

<details>
<summary>Q1. Reads from flash are wrong at 50 MHz but fine at 25. How does your plan catch and fix it?</summary>

- **EN**: "That's the round-trip delay — SCLK out, flash output delay, MISO back — exceeding the sampling point. The plan has a sample-delay tap sweep at each frequency and corner, which turns this into a passing-window plot. The fix is choosing the tap at the window center for each speed, or limiting frequency; the regression then checks that the window stays at least two taps wide at hot and low voltage."
- 한국어 핵심: 왕복 지연 > 반주기. tap × 주파수 × PVT shmoo로 창 확인, 기본 tap = 창 중앙.

</details>

<details>
<summary>Q2. How do you test all four SPI modes when your flash only supports mode 0 and 3?</summary>

- **EN**: "Use a partner that supports all four: the SoC's other SPI instance in target mode, an FPGA target, or an analyzer that decodes each mode. Loopback alone can't prove the mode because the controller samples its own data consistently even if the edge is wrong — so I check edges against the analyzer, not just data compare."
- 한국어 핵심: loopback은 mode 오류를 못 잡을 수 있음 → target 모드 인스턴스/FPGA/analyzer로 edge 확인.

</details>

<details>
<summary>Q3. What's your pass criterion for SPI throughput?</summary>

- **EN**: "Theoretical single-line throughput is SCLK divided by eight bytes per second — 6.25 megabytes per second at 50 MHz. I'd expect DMA streaming to reach around 90 percent of that, with the gap set by CS and inter-word timing; the exact target comes from the architecture spec. I report it under concurrent DDR load too, because DMA starvation shows up as underflow or gaps."
- 한국어 핵심: 이론 = SCLK/8 B/s, 목표 ~90% [추정], DDR 부하 하에서도.

</details>

### 8. Don 연결

- "SPI was one of the blocks I verified on the FPGA prototype at Solidigm before RTL freeze, and SPI NOR boot paths are something I've debugged from the firmware side — [숫자 채우기]."

## C3 · UART

### 0. 🔰 이 블록이 뭔가

- **비유**: 두 사람이 **각자 손목시계로 박자를 맞춰** 모스 부호를 주고받는 것. 클럭 선이 없으니 시계(baud)가 조금만 달라도 끝으로 갈수록 박자가 밀린다.
- **정확한 정의**: UART IP = 레지스터(보통 16550 호환 또는 자체) + baud 분주기(정수/분수) + TX/RX FIFO + start 검출·16× oversampling + 프레임(데이터 5–8, parity, stop 1/2) + 에러 검출(overrun·parity·framing·break) + RTS/CTS 흐름 제어 + 인터럽트(+RX timeout) + DMA. 16550 호환이면 LSR 비트: DR 0x01 · OE 0x02 · PE 0x04 · FE 0x08 · BI 0x10 · THRE 0x20 · TEMT 0x40, MCR loopback 0x10, FIFO trigger 1/4/8/14 바이트 [5].

```text
 APB ─► regs (DLL/DLM div, LCR, FCR, IER, LSR) ─► TX FIFO ─► serializer ─► TX ──► partner / USB-UART
                                                  RX FIFO ◄─ 16x sampler ◄─ RX ◄──
 RTS ─► / CTS ◄─ flow control      irq (RX trig / RX timeout / THRE / LSR err) ─► GIC     DMA req
```

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| baud 오차 | — | 분주 정수화로 생기는 속도 오차 | 양쪽 오차 합이 허용치를 넘으면 framing |
| 16× oversampling | — | 비트 하나를 16번 보고 중앙에서 샘플 | 허용 오차 계산의 근거 |
| framing error | — | stop 비트 자리에 0 | baud 불일치 검출 |
| overrun | — | FIFO 가득 찼는데 또 들어옴 | 흐름 제어·ISR 지연 테스트 |
| break | — | 한 프레임 이상 계속 0 | 검출 기능 + 디버그 콘솔 SysRq 등 |

- **허용 오차 [계산]**: 10비트 프레임(8N1), stop 비트 중앙 = start edge 후 9.5비트. 16× 샘플링이면 start 검출 불확실성 ≈ 1/16비트. 누적 오차 한계 ≈ (0.5 − 1/16) / 9.5 ≈ **4.6% (양쪽 합)** → 한쪽 ±2% 목표가 흔한 이유. baud 표(24 MHz/25 MHz 예시)는 [BV10 §2.3](2026-11-01_BV10_board_interface_tests.html).

### 1. 먼저 물어볼 것 (clarify)

- "Is it 16550-compatible or custom registers? Fractional divider?" → 가정: 16550 호환 + 분수 분주 [추정].
- "Which baud rates are required — debug console 115200 only, or multi-megabaud for a radar/GNSS/MCU link?" → 가정: 9600–3 Mbaud.
- "Flow control and DMA in scope? RS-485 direction control?" → 가정: RTS/CTS + DMA, RS-485 없음.
- "What's the UART reference clock and its accuracy?" → 오차 기대값 계산에 필요.

### 2. 테스트 매트릭스

| 카테고리 | 테스트 항목 | 조건/파라미터 | 플랫폼 | 통과 기준 | 우선 |
|---|---|---|---|---|---|
| 레지스터 | reset 값 · RW · DLAB 류 banking | 전 인스턴스 | 둘 다 | 문서와 일치 | P0 |
| 기본 기능 | 내부 loopback (MCR loop) data compare | 115200 8N1 | 둘 다 | 10^4 B 0 mismatch | P0 |
| 설정 조합 | baud × 데이터 5–8 × parity N/E/O/mark/space × stop 1/2 | 외부 partner | 둘 다 | 조합당 10^5 B 0 오류 | P1 |
| 경계 | baud 정확도: 실제 비트 폭 측정 | 모든 지원 baud | Si | 실측 오차 = 계산 오차 ±0.1%, 절대값 ≤ 2% | P0 |
| margin | partner baud를 일부러 ±x% 어긋나게 | ±1…±6% 스윕 | Si | ±2%에서 0 오류, 한계(약 ±4%) 근처부터 FE/PE 검출 [추정] | P1 |
| 인터럽트 | RX trigger 1/4/8/14, THRE, RX timeout, LSR 에러 | 원인별 | 둘 다 | trigger 레벨 정확, timeout은 RX idle 후 발생 (약 4 문자 시간 [추정]) | P0 |
| 에러 | overrun (RX 안 읽기), parity 주입, framing, break | partner로 주입 | 둘 다 | OE/PE/FE/BI 각각 정확히 set, 어느 바이트에 붙는지 문서와 일치 | P0 |
| 에러 | 흐름 제어: CTS 내리면 TX 정지 / RX FIFO 차면 RTS 해제 | auto RTS/CTS | Si | CTS low 후 TX 정지 ≤ 1 문자 [추정], 장시간 overrun 0 | P1 |
| 리셋·전원 | 전송 중 FIFO reset / suspend-resume / wake on RX | 1000회 | Si | 복귀 후 정상, wake 문자 손실 정책 문서대로 | P1 |
| 동시성 | 모든 UART 최대 baud + DMA | 1시간 | Si | 0 오류, overrun 0 | P1 |
| 성능 | 3 Mbaud 연속 DMA, CPU 부하 | 최대 baud | Si | 바이트 간 idle 0 (연속 stop→start), CPU < X% [추정] | P2 |
| PVT | 참조 클럭 온도 drift에 따른 baud | -40/25/125 °C | Si | 오차 ≤ 2% 유지 | P1 |
| soak | 콘솔 + 고속 링크 | 10^9 B | Si | 0 오류 | P1 |
| 시스템 | Linux 8250/자체 드라이버 콘솔, 실제 상대 MCU | 부팅 1000회 | Si | 콘솔 깨짐 0 | P0 |

### 3. 우선순위 · A0 첫 주

- **P0**: UART는 **A0 첫날 디버그 콘솔** → 레지스터 · loopback · 115200 비트 폭 측정 · 에러 비트 기본 · 콘솔 부팅. 콘솔이 깨지면 모든 bring-up이 느려진다.
- **P1**: 전 조합, ±x% margin, 흐름 제어, 리셋·동시성·soak, PVT. **P2**: 성능·CPU 부하.
- **안 하는 것**: oversampling 샘플러의 모든 위상 조합(DV), 문자 길이 5·6비트의 전 parity 조합은 FPGA에서 한 번만.

### 4. 장비 · 셋업 · 관찰 지점

- USB-UART 어댑터(partner), baud를 미세 조정할 수 있는 패턴 생성기/FPGA partner(±x% 주입), scope로 비트 폭(1/baud) 측정, logic analyzer UART 디코드.
- Linux: `stty -F /dev/ttyS1 3000000 raw`, `/proc/tty/driver/serial`·`/sys/class/tty/.../` 에러 카운터(`fe`, `pe`, `oe`, `brk`) [추정: 8250 드라이버 출력 형식].
- 관찰: LSR, FIFO level, RTS/CTS 핀 상태.

### 5. 잘 터지는 버그

1. **분주기 입력 클럭이 문서와 다름** → 모든 baud가 같은 비율로 틀림 (콘솔 깨짐). 비트 폭 측정 행.
2. **분수 분주 반올림 오류** → 특정 baud만 2–3% 오차. baud 전수 측정.
3. **RX timeout 인터럽트 없음/너무 늦음** → 짧은 메시지가 FIFO에 갇혀 latency 폭증. 인터럽트 행.
4. **overrun 시 어느 바이트를 버리는지 문서와 다름** → 프로토콜 파서 혼란. 에러 행.
5. **auto-RTS threshold 오프바이원** → 고속에서 간헐 overrun. 흐름 제어 장시간 행.
6. **wake-on-RX에서 첫 문자 손실** → 저전력 모드 통신 실패. 리셋·전원 행.

### 6. 10분 영어 답 뼈대

1. "Scope: 16550-style UART with fractional divider, RTS/CTS, DMA, 9600 to 3 megabaud."
2. "It's the A0 debug console, so register sanity, internal loopback and measuring the actual bit width at 115200 come first."
3. "Function matrix: baud times data bits times parity times stop bits against an external partner."
4. "Accuracy: measure every baud's bit time on a scope and compare with the computed divider error; with 16x oversampling the combined budget is about 4.6 percent, so I want each side within 2."
5. "Margin: deliberately offset the partner by plus or minus one to six percent — no errors at 2, clean framing-error detection beyond the limit."
6. "Interrupts: each RX trigger level, THRE, RX timeout, line-status errors."
7. "Errors: overrun by not reading, injected parity and framing errors, break — each sets exactly its bit on the right byte."
8. "Flow control: CTS stops TX within a character; auto-RTS prevents overrun over hours."
9. "Reset, suspend, wake on RX; all instances at max baud with DMA; soak a billion bytes; temperature drift of the reference clock."
10. "System: Linux console through a thousand boots."

### 7. 꼬리 질문

<details>
<summary>Q1. The console prints garbage at 115200 on A0. Plan or debug — what's first?</summary>

- **EN**: "Measure one bit on the scope: if it's not 8.68 microseconds, the divider input clock isn't what the driver assumed — usually PLL or clock-mux setup, not the UART. If the bit time is right, check the frame format and voltage levels on the partner side. That's why bit-time measurement is a P0 row: it separates clock integration from the UART IP immediately."
- 한국어 핵심: 1비트 = 8.68 µs 실측. 틀리면 클럭 통합 문제, 맞으면 프레임 형식·전압.

</details>

<details>
<summary>Q2. How do you prove the receiver's error tolerance?</summary>

- **EN**: "Drive it from a partner whose baud I can trim in small steps, sweep from minus six to plus six percent, and plot errors versus offset. The pass criterion is zero errors over a large byte count inside plus or minus two percent, and that the edges of the window are symmetric and close to the oversampling math — about 4.6 percent combined for 8N1. An asymmetric window points at a sampling-point bug."
- 한국어 핵심: partner baud 스윕 → 오류 vs 오차 그래프, ±2% 0 오류, 창이 대칭·이론치 근처인지.

</details>

<details>
<summary>Q3. Why does the RX timeout interrupt matter?</summary>

- **EN**: "With a trigger level of, say, eight bytes, a five-byte message never reaches the trigger; without a character timeout it sits in the FIFO until more data comes. So I test that a short burst raises the timeout interrupt after the line goes idle, and I measure that delay because it's part of message latency."
- 한국어 핵심: trigger 미만 짧은 메시지가 FIFO에 갇힘 → timeout IRQ 발생·지연 측정.

</details>

### 8. Don 연결

- "On every bring-up I've done — SSD controller FPGA prototypes and now wireless chipsets — the UART console was the first thing to get right, and the bit-time measurement is my first step when it prints garbage."

## C4 · GPIO · pinmux

### 0. 🔰 이 블록이 뭔가

- **비유**: 칩 가장자리의 **멀티탭 콘센트**. 각 구멍(pad)에 어떤 가전(I2C, UART, GPIO…)을 꽂을지 정하는 게 pinmux, 구멍마다 전압(1.8/3.3 V)·전류 세기·풀업 스위치가 있다.
- **정확한 정의**: GPIO 컨트롤러 = 방향·출력값·입력값 레지스터(+ atomic set/clear), 인터럽트(edge rising/falling/both, level high/low), debounce 필터, wake 소스. pinmux/pad 제어 = pad별 function select, pull up/down/none, drive strength, slew, open-drain, input enable, IO 전압 도메인. 리셋 중 pad 기본 상태가 **안전 상태**여야 한다(자동차: 액추에이터·카메라 전원 enable이 리셋 중 튀면 안 됨).

```text
 APB ─► GPIO regs (DIR, OUT, IN, SET/CLR, IRQ type/en/status, DEBOUNCE) ─┐
 APB ─► PINMUX/PAD regs (func sel, pull, drive, slew, OD, IE) ──────────┤
 I2C/UART/SPI/PWM 신호 ─────────────────────────────────────────────────┼─► pad ring (도메인 1.8V/3.3V) ─► 핀
 irq ─► GIC (pad 그룹당 1개 또는 핀당) · wake ─► PMU                      │
```

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| pinmux | pin multiplexing | 한 핀에 여러 기능 중 하나 선택 | 표가 크고 통합 실수 1순위 |
| drive strength / slew | — | 출력 전류 세기 / edge 속도 | EMI·신호 품질 |
| debounce | — | 짧은 튐 무시 필터 | 버튼·스위치 입력 |
| IO 전압 도메인 | — | pad 그룹 전원 | 1.8 V pad에 3.3 V 장치 → 손상 |
| safe state | — | 리셋·전원 인가 중 pad 상태 | 기능 안전 [추정 깊이] |

### 1. 먼저 물어볼 것 (clarify)

- "Is there a machine-readable pinmux table — spreadsheet or IP-XACT — that I can generate tests from?" → 가정: 있음. 이게 있으면 행 대부분을 자동 생성.
- "How many GPIO banks, interrupt per pin or per bank, wake-capable pins?" → 가정: 뱅크당 IRQ 1개, 일부 wake.
- "Which pads are safety-relevant and what's their required reset state?" → 가정: 리스트를 safety 팀이 제공.
- "Do we have a loopback fixture or boundary scan for pad connectivity?" → 가정: 점퍼 지그 + JTAG BSDL.

### 2. 테스트 매트릭스

| 카테고리 | 테스트 항목 | 조건/파라미터 | 플랫폼 | 통과 기준 | 우선 |
|---|---|---|---|---|---|
| 레지스터 | reset 값(특히 pad 기본 기능·pull) | 전 pad, pinmux 표에서 기대값 자동 생성 | 둘 다 | 표와 100% 일치 | P0 |
| 기본 기능 | 출력 토글 → 입력 읽기 (pad 쌍 loopback) | walking-1, 모든 쌍 | Si (FPGA 부분) | 쌍마다 기대 레벨, short/open 0 | P0 |
| 설정 조합 | pull up/down/none × 입력 floating 읽기 | 외부 구동 없음 | Si | pull 방향대로 읽힘 100% | P1 |
| 설정 조합 | pinmux: 모든 pad × 모든 function 선택 | 표 기반 | FPGA → Si | 선택 기능 신호가 핀에 나타남 (loopback/analyzer) | P1 |
| 경계 | 같은 기능을 두 pad에 동시에 선택 (충돌) | negative | 둘 다 | 문서된 우선순위대로, 손상·hang 0 | P2 |
| 인터럽트 | edge rising/falling/both, level high/low | 펄스 N=10^6, 주파수 스윕 | Si | IRQ 수 = 펄스 수 (edge, ISR 처리율 이내), level은 유지 동안 재진입 | P0 |
| 인터럽트 | status W1C, mask, 뱅크 공유 IRQ 다중 핀 | 동시 엣지 | 둘 다 | 핀별 status 정확, 누락 0 | P1 |
| 에러·negative | debounce: 필터 폭 미만/이상 펄스 | 폭 스윕 | Si | 미만 무시 100%, 이상 검출 100%, 실측 필터 폭 기록 | P1 |
| 리셋·전원 | 리셋·전원 인가 중 pad 글리치 | scope, 1000회 | Si | safety pad 글리치 > X ns 0 [추정 기준] | P0 |
| 리셋·전원 | wake from sleep via GPIO, retention | 1000회 | Si | wake 100%, 출력 상태 유지 | P1 |
| 동시성 | 여러 코어가 같은 뱅크 SET/CLR 동시 | 10^6회 | Si | lost update 0 (atomic 레지스터) | P1 |
| 성능 | 최대 토글 주파수, GPIO→ISR latency | scope 2ch | Si | latency p99 < X µs [추정], 최대 토글 = spec | P2 |
| PVT·margin | VOH/VOL @ drive 설정, VIL/VIH | 도메인 전압 ±5%, 온도 3점 | Si | datasheet 전기 특성 표 충족 | P1 |
| 시스템 | Linux gpiochip 매핑, 실제 리셋/enable 라인 | libgpiod | Si | 라인 이름·번호가 DT와 일치 | P1 |

### 3. 우선순위 · A0 첫 주

- **P0**: pad reset 상태(안전) → pinmux 기본값 → 콘솔·I2C·부트 장치 pad 확인 → GPIO loopback → edge IRQ.
- **P1**: 전체 pinmux 매트릭스(자동 생성), pull·debounce·wake·atomic, 전기 특성. **P2**: 충돌·최대 토글·latency.
- **안 하는 것**: pad ring 아날로그 특성의 정밀 특성화(IO 라이브러리 vendor 특성 데이터 + ATE 범위), GPIO 레지스터 비트 조합 전수(DV).

### 4. 장비 · 셋업 · 관찰 지점

- 핀 쌍 loopback 지그(점퍼 보드), JTAG boundary scan(BV07)으로 pad 연결 확인, 펄스 생성기(폭·주파수 스윕), scope(리셋 중 글리치 single-shot 트리거), DMM/SMU로 VOH/VOL.
- Linux: `gpioget`, `gpioset`, `gpiomon --edges=rising` (libgpiod v2 기준) [추정: 옵션 표기는 버전별 상이], `/sys/kernel/debug/pinctrl/*/pinmux-pins`.
- 관찰: IRQ status raw vs masked, debounce 설정, pad 설정 readback.

### 5. 잘 터지는 버그

1. **pinmux 표와 RTL 불일치**(function 번호 한 칸 밀림) → 특정 인터페이스 전체 불통. 표 기반 자동 테스트.
2. **리셋 중 pad 기본 pull이 반대** → 카메라/모터 enable이 부팅 중 잠깐 켜짐. 리셋 글리치 행.
3. **edge IRQ를 W1C 전에 다시 오면 누락**(clear 레이스) → 고주파 입력에서 카운트 부족. 10^6 펄스 카운트 행.
4. **debounce가 level IRQ를 지연시켜 wake 실패**. wake 행.
5. **1.8 V 도메인 pad에 3.3 V 설정** → 누설·손상. 도메인 전압 행 + 표 검증.

### 6. 10분 영어 답 뼈대

1. "Scope: GPIO banks plus the pinmux and pad control; I'll generate most tests from the pinmux table."
2. "First, reset state of every pad — especially safety-relevant enables that must not glitch during reset or power-up — measured with single-shot scope triggers."
3. "Register defaults and pinmux defaults versus the table, auto-generated."
4. "Connectivity: pad-pair loopback with walking ones, plus boundary scan for pads without a loopback partner."
5. "Config: pull up, down, none; drive strength; open-drain; every pad times every function."
6. "Interrupts: each edge and level type, a million pulses with count equals pulses, shared-bank status, debounce filter width measured."
7. "Concurrency: multiple cores using atomic set/clear on the same bank."
8. "Power: wake from sleep through GPIO, output retention."
9. "Electrical: VOH and VOL at each drive setting across voltage and temperature."
10. "System: Linux gpiochip line names match the device tree."

### 7. 꼬리 질문

<details>
<summary>Q1. There are 400 pads and 8 functions each. How do you make that tractable?</summary>

- **EN**: "I don't write those tests by hand. The pinmux table from design is the single source of truth; a script generates the expected reset state and the function-select tests for every pad, and the same table generates the driver's pinctrl data. Then a mismatch is either a table bug or an RTL bug, and either way it's found automatically. Connectivity uses loopback pairs and boundary scan, so the number of physical setups stays small."
- 한국어 핵심: pinmux 표 = 단일 진실. 스크립트로 테스트·드라이버 데이터 동시 생성.

</details>

<details>
<summary>Q2. How do you test that GPIO interrupts are never lost?</summary>

- **EN**: "Drive a known number of pulses — a million — at increasing frequency and compare the interrupt count. Below the ISR's service rate the counts must match exactly; above it, edges coalesce and that's expected, so I find and document the max rate. Then repeat with both-edge mode and with several pins in the same bank firing together, because the shared status register and the write-one-to-clear race are where losses happen."
- 한국어 핵심: 펄스 10^6 vs IRQ 카운트, 처리율 이하에서 정확히 일치, 공유 status·W1C 레이스.

</details>

### 8. Don 연결

- "At Apple I build factory test nodes that catch latent pin-level defects, and the walking-ones loopback idea is the same one I use there — [숫자 채우기: pins/units covered]."

## C5 · Timer · watchdog · RTC

### 0. 🔰 이 블록이 뭔가

- **비유**: Timer = 주방 타이머(몇 초 뒤 울림), watchdog = **"10초마다 버튼 안 누르면 차를 세우는" 졸음 감지기**, RTC = 차 시계(시동 꺼도 배터리로 계속 감).
- **정확한 정의**: (1) 범용 timer — free-running counter, compare/match → 인터럽트, one-shot/periodic, prescaler, 32/64-bit. Arm 코어의 generic timer(시스템 카운터 + 코어별 PPI 타이머)도 SoC 레벨에서 확인 대상. (2) watchdog — 카운터가 만료되면 인터럽트/리셋. SBSA generic watchdog은 **2단계**: 첫 만료 WS0 = 인터럽트(경고), 두 번째 WS1 = 실제 하드웨어 리셋 [6]. windowed watchdog은 너무 일찍 kick해도 리셋. reset reason 레지스터. (3) RTC — 보통 32.768 kHz 결정, always-on 도메인, 알람 wake.
- **ppm 감각 [계산]**: 20 ppm × 86,400 s = **1.73 s/일**, 50 ppm → 4.3 s/일.

```text
 ref clk ─► prescaler ─► counter (32/64b) ─► compare[n] ─► irq ─► GIC (SPI 또는 코어별 PPI)
 watchdog: counter ─► WS0 (irq) ─► WS1 (reset req) ─► reset ctrl ─► RESET_REASON (sticky)
 RTC (AON domain, 32.768k xtal) ─► alarm ─► PMU wake
```

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| ppm | parts per million | 주파수 오차 단위 | 정확도 통과 기준 |
| WS0 / WS1 | watchdog signal 0/1 | 1단계 경고 IRQ / 2단계 리셋 | 둘 다 테스트 |
| windowed WDT | — | 허용 창 안에서만 kick | 너무 이른 kick도 고장으로 간주 |
| reset reason | — | 왜 리셋됐는지 남는 sticky 레지스터 | 현장 고장 분석의 근거 |
| AON | always-on | 꺼지지 않는 전원 도메인 | RTC 유지 |

### 1. 먼저 물어볼 것 (clarify)

- "Which timers: SoC general-purpose, the Arm generic timer integration, watchdogs per core or system-wide, RTC?" → 가정: 전부, watchdog은 시스템 1 + 안전 MCU용 1 [추정].
- "Watchdog: two-stage, windowed, can it be disabled after enable, does it pause in debug halt?" → 가정: 2단계·windowed·enable 후 disable 불가·debug 시 pause 설정 가능.
- "Accuracy targets — ppm for the timer reference and for the RTC?" → 가정: 시스템 ±50 ppm, RTC ±20 ppm [추정].
- "What reference do we have — GPS-disciplined 10 MHz or a frequency counter?" → 가정: 주파수 카운터 + 10 MHz 레퍼런스.

### 2. 테스트 매트릭스

| 카테고리 | 테스트 항목 | 조건/파라미터 | 플랫폼 | 통과 기준 | 우선 |
|---|---|---|---|---|---|
| 레지스터 | reset 값 · lock 비트 · W1C | 전 인스턴스 | 둘 다 | 문서와 일치, lock 후 write 무시 | P0 |
| 기본 기능 | counter 단조 증가 · compare IRQ | 기본 prescaler | 둘 다 | 10^6 읽기 단조, IRQ 시점 = compare ± 1 tick | P0 |
| 설정 조합 | prescaler × one-shot/periodic × up/down | 전 조합 | 둘 다 | 주기 = 계산값 ± 1 tick | P1 |
| 경계 | wrap: counter를 max−N에 설정 | 32/64-bit | 둘 다 | wrap 후 compare·IRQ 정상, 64-bit 읽기 찢김 0 (N05C C4) | P0 |
| 경계 | compare = 현재값/과거값 설정 | race | 둘 다 | 문서된 동작(즉시 발생 또는 다음 wrap), 누락 0 | P1 |
| 성능·정확도 | 주파수 정확도 vs 레퍼런스 | 1000 s 측정, 3 온도 | Si | 오차 ≤ ±50 ppm [추정] | P0 |
| 성능 | periodic IRQ jitter | 1 kHz, 10^6 주기 | Si | jitter p99 < X µs [추정], 누락 주기 0 | P1 |
| 인터럽트 | WS0 → 처리 → kick, WS0 무시 → WS1 리셋 | 2단계 | Si | WS0 시각 ± 1%, WS1 리셋 100%, reset reason = watchdog | P0 |
| 에러·negative | windowed: 창 밖(너무 이른) kick | 창 경계 스윕 | Si | 이른 kick → 리셋 100%, 창 안 → 리셋 0 | P1 |
| 에러·negative | enable 후 disable 시도 / 잘못된 key | lock | 둘 다 | 무시됨, watchdog 계속 동작 | P1 |
| 리셋·전원 | reset reason이 warm/cold/WDT/SW 구분 | 각 1000회 | Si | 원인 분류 100% 정확, sticky 유지 | P0 |
| 리셋·전원 | debug halt 중 watchdog pause | JTAG halt | Si | 설정대로 pause/계속 | P2 |
| 리셋·전원 | RTC: 메인 전원 off·sleep 동안 유지, 알람 wake | 24 h, 1000회 wake | Si | drift ≤ 20 ppm (≈1.73 s/일), wake 100% | P1 |
| PVT | 온도별 주파수, RTC 결정 온도 특성 | -40/25/105 °C [추정] | Si | 코너에서도 ppm 기준 충족 | P1 |
| 시스템 | Linux clocksource/clockevent, watchdog daemon, hwclock | 72 h | Si | 시간 drift 기준 내, 의도치 않은 리셋 0 | P1 |

### 3. 우선순위 · A0 첫 주

- **P0**: counter 단조·compare IRQ(OS tick의 기반), wrap·64-bit 읽기, 주파수 1차 정확도, watchdog WS1 리셋 + reset reason(안 그러면 hang 난 보드를 자동 복구 못 함).
- **P1**: 조합·jitter·windowed·lock·RTC·PVT. **P2**: debug halt 동작.
- **안 하는 것**: counter 비트별 토글 전수(DV, 그리고 wrap 테스트가 상위 비트를 간접 확인).

### 4. 장비 · 셋업 · 관찰 지점

- 주파수 카운터 + 10 MHz 레퍼런스(또는 GPS-disciplined), compare 출력을 GPIO로 내보내 scope로 jitter, 온도 챔버, 전원 스위치(RTC 유지 테스트).
- 관찰: counter·compare 레지스터, reset reason, watchdog 카운터, Linux `clocksource` 선택·`dmesg` 경고, `cyclictest`로 timer IRQ latency [추정: rt-tests].

### 5. 잘 터지는 버그

1. **64-bit counter를 32-bit 두 번 읽어 찢어진 값** → 시간이 순간 거꾸로/앞으로 튐. wrap 행 + N05C C4.
2. **compare를 과거값으로 쓰면 IRQ가 영원히 안 옴**(다음 wrap까지 수 시간) → 간헐 hang. compare race 행.
3. **reset reason이 cold reset에 지워지지 않음/WDT 리셋에 지워짐** → 현장 분석 불가. reset reason 행.
4. **watchdog이 debug halt 중 리셋** → 디버깅 불가, 또는 반대로 양산에서 pause가 켜져 안전 메커니즘 무력화. debug 행.
5. **sleep 진입 시 timer 클럭 게이팅** → wake 타이머 안 울림. RTC/wake 행.

### 6. 10분 영어 답 뼈대

1. "Three blocks: general-purpose timers plus the generic-timer integration, watchdogs, and the RTC."
2. "Timers: monotonic counter, compare interrupts at the exact tick, every prescaler and mode, wrap at 32 and 64 bits, torn 64-bit reads, compare written in the past."
3. "Accuracy: against a 10 MHz reference over a thousand seconds at three temperatures, within plus or minus 50 ppm; periodic interrupt jitter at p99 and no missed periods."
4. "Watchdog: stage one interrupt at the right time, stage two reset when ignored, early kicks outside the window reset, disable after enable is blocked, and reset reason is correct and sticky."
5. "RTC: keeps time through main-power off, about 1.7 seconds a day at 20 ppm, alarm wake a thousand times."
6. "System: Linux clocksource and watchdog daemon running 72 hours with no unexpected resets."
7. "P0 is the OS tick and the watchdog reset path, because automated board farms depend on watchdog recovery."

### 7. 꼬리 질문

<details>
<summary>Q1. How do you measure timer accuracy to ppm level?</summary>

- **EN**: "Route a compare output or a GPIO toggled from the compare interrupt to a frequency counter locked to a 10 MHz reference, and measure over a long gate — a thousand seconds gives plenty of resolution for 1 ppm. Hardware toggling avoids ISR jitter. Repeat at temperature corners, because the crystal or PLL reference drifts with temperature."
- 한국어 핵심: compare 출력 → 레퍼런스 고정 주파수 카운터, 긴 gate, HW 토글, 온도 코너.

</details>

<details>
<summary>Q2. Why is the watchdog a P0 for validation infrastructure, not just for the product?</summary>

- **EN**: "In a board farm running overnight regressions, a hung board must recover by itself and tell us why. So the watchdog reset path and the reset-reason register are what make every other test automatable; I validate them in the first week and then the test framework relies on them."
- 한국어 핵심: 보드 팜 자동 복구 + reset reason = 모든 자동화의 전제.

</details>

<details>
<summary>Q3. What's a two-stage watchdog and how do you test both stages?</summary>

- **EN**: "Stage one raises an interrupt so software can log state or attempt recovery; if it still isn't refreshed, stage two asserts a real hardware reset — that's how the Arm SBSA generic watchdog is described. I test that stage one fires on time and that refreshing in the handler prevents reset, then I mask the stage-one handler and confirm stage two resets with the right reset reason, a thousand times."
- 한국어 핵심: WS0 IRQ → 복구 기회, WS1 리셋. 각각 시각·리셋·reset reason 확인.

</details>

### 8. Don 연결

- "Our SK hynix reliability chamber platform ran unattended for long stretches, and watchdog-plus-reset-reason recovery was what made that possible — [숫자 채우기: chambers / hours]."

## C6 · Interrupt controller (GIC 류) (★ 가장 깊게)

### 0. 🔰 이 블록이 뭔가

- **비유**: 병원 **응급실 접수처**. 환자(인터럽트)마다 번호표(INTID)와 중증도(priority)가 있고, 접수처(Distributor)가 어느 의사(코어)에게 보낼지 정한다. 의사는 "다음 환자"를 부르며 번호표를 받고(IAR = acknowledge), 진료가 끝나면 "끝"(EOI)을 알린다. 진료 중인 의사에겐 **더 위급한 환자만** 끼어들 수 있다(preemption). 접수처 문 앞에 "중증도 X 이하는 받지 않음" 팻말 = priority mask(PMR).
- **정확한 정의 (Arm GICv3 기준)** [2][7]:
  - 구성: **Distributor**(SPI 설정·라우팅), **Redistributor**(코어별, SGI/PPI 설정·전원/wake — GICR_WAKER), **CPU interface**(IAR·EOI·PMR·BPR·RPR, 시스템 레지스터 ICC_*), **ITS**(MSI → LPI 변환; PCIe MSI 쪽은 [LP07](2026-10-31_LP07_interrupts_intx_msi_msix.html)).
  - INTID: **SGI 0–15**(소프트웨어가 생성, 코어 간 통신, 코어별 banked) · **PPI 16–31**(코어 전용, 예: generic timer) · **SPI 32–1019**(공유 peripheral) · **1020–1023 특수**(1023 = spurious) · LPI 8192+ · GICv3.1 확장 PPI 1056–1119, 확장 SPI 4096–5119.
  - 상태: inactive · pending · active · active-and-pending. **Level**: 입력이 high면 pending, ack 후에도 라인이 high면 active+pending, 장치가 라인을 내려야 active. **Edge**: rising edge가 pending을 래치, ack하면 active, 처리 중 다시 edge가 오면 active+pending → EOI 후 다시 pending.
  - 설정: 우선순위 8-bit(0x00 최고, 0xFF 최저, 구현 비트 수는 보안 상태 2개면 최소 5비트·1개면 최소 4비트), group(Group 0 = 항상 FIQ, Secure/Non-secure Group 1), edge/level(GICD_ICFGR; **SGI는 항상 edge**, RAO/WI), enable(set/clear 레지스터 분리 — RMW 불필요).
  - 전달 조건: group enable · INTID enable · 라우팅(SPI는 GICD_IROUTER: 특정 affinity 또는 1-of-N) · **priority < PMR** · **running priority보다 높을 것**(preemption은 BPR로 나눈 group priority만 비교).
  - 처리: IAR 읽기 = ack(INTID 반환, running priority 상승) → 처리 → EOI. EOImode 0이면 EOIR 쓰기 = priority drop + deactivation, EOImode 1이면 EOIR = drop, ICC_DIR = deactivate(하이퍼바이저용). 1023을 읽으면 아무것도 ack하지 않은 것.
  - **같은 priority끼리 선택 순서는 IMPLEMENTATION DEFINED**, 그리고 스펙은 "높은 priority가 항상 먼저"를 보장하지 않는다(대개 그렇다) — 테스트가 동일 priority 순서를 가정하면 안 된다 [7].
  - GICv2 vs v3 SGI: v2는 보낸 코어별로 banked(같은 SGI가 최대 8번 pending), v3는 대상 코어별 1개만 → 두 코어가 동시에 SGI 5를 보내면 v3는 **1번만** 받는다 [2].
  - GICD_TYPER.ITLinesNumber = N이면 최대 SPI INTID = 32(N+1) − 1 → 통합 스펙의 SPI 개수와 대조하는 첫 레지스터 테스트 [7].

```text
  peripherals ─ irq lines (level/edge) ─┐          MSI writes (PCIe) ─► ITS ─► LPI
  (I2C, UART, DMA, CSI-2, NPU, ...)     ▼
                               ┌─────────────────────┐
                               │ Distributor (SPI)    │  enable · priority · group · ICFGR · IROUTER
                               └──────┬──────┬───────┘
                         ┌────────────┘      └────────────┐
                ┌────────▼────────┐              ┌────────▼────────┐
                │ Redistributor 0 │ SGI/PPI      │ Redistributor N │  GICR_WAKER (sleep/wake)
                └────────┬────────┘              └────────┬────────┘
                ┌────────▼────────┐              ┌────────▼────────┐
                │ CPU if 0        │ IAR/EOI/PMR  │ CPU if N        │
                └────────┬────────┘ BPR/RPR      └────────┬────────┘
                       core 0 (IRQ/FIQ)                core N
   state per INTID:  inactive ─assert─► pending ─IAR─► active ─EOI─► inactive
                                           ▲              │ (edge re-assert / level still high)
                                           └── EOI ── active+pending
```

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| INTID | interrupt ID | 번호표 | 배선 테스트의 기대값 |
| SGI / PPI / SPI / LPI | software-generated / private peripheral / shared peripheral / locality-specific | 코어 간 / 코어 전용 / 공유 / MSI 기반 | 종류마다 설정·라우팅이 다름 |
| IAR / EOI / DIR | interrupt acknowledge / end of interrupt / deactivate | 접수 / 진료 끝 / 상태 해제 | 순서 틀리면 hang·storm |
| PMR | priority mask register | 이 priority 미만만 받음 | 마스크 테스트 |
| BPR | binary point register | priority를 group/sub로 나눔 | preemption 기준 |
| RPR | running priority | 지금 처리 중인 priority | 중첩 확인 |
| affinity / 1-of-N | — | 대상 코어 주소 / 아무 코어나 | 라우팅·부하 분산 |
| spurious (1023) | — | "줄 게 없음" | ISR이 반드시 처리해야 함 |

### 1. 먼저 물어볼 것 (clarify)

- "Is it an Arm GIC — which version, GIC-600/700-class licensed IP — or a custom controller? Do we have an ITS?" → 가정: 라이선스 GICv3 계열 + ITS [추정]. 그렇다면 **GIC IP 자체 로직보다 통합(배선·trigger type·라우팅·전원)이 핵심**.
- "Is there an integration spec listing every interrupt source, its INTID, trigger type and intended target?" → 가정: 있음 — 이게 배선 테스트의 golden 표.
- "How many cores and clusters, security states — TrustZone on, two security states?" → 가정: 다중 클러스터, 보안 상태 2개.
- "Any latency requirement — for example camera frame-start or safety interrupts within X microseconds?" → 가정: 안전 인터럽트에 latency 요구 있음 [추정].
- "Do peripherals have a 'force interrupt' or test register, so I can trigger each source without its full function?" → 가정: 일부만. 없으면 기능적으로 발생시킴.
- "Any safety-island or MCU-side interrupt controller too?" → 가정: 범위 밖, 별도 플랜.

### 2. 테스트 매트릭스

| 카테고리 | 테스트 항목 | 조건/파라미터 | 플랫폼 | 통과 기준 | 우선 |
|---|---|---|---|---|---|
| 레지스터 | GICD_TYPER(SPI 수, 보안·affinity 지원), IIDR, 각 레지스터 reset·RW | ITLinesNumber → 32(N+1)−1 | 둘 다 | 통합 스펙과 일치, 미구현 priority 비트는 RAZ | P0 |
| 레지스터 | priority 구현 비트 수 | IPRIORITYR에 0xFF 쓰고 읽기 | 둘 다 | 보안 2상태면 ≥ 5비트 (0xF8), 문서와 일치 | P1 |
| 기본 기능 (배선) | **소스별 1:1 연결**: 각 peripheral IRQ → 기대 INTID | 소스 전부, force/test 레지스터 또는 기능 발생 | 둘 다 | 기대 INTID에서만 1회 수신, 다른 INTID 0회 — 100% 소스 | P0 |
| 기본 기능 (배선) | 소스별 trigger type 일치 | 장치 status 안 지우고 EOI | 둘 다 | level 소스는 재진입, edge 소스는 재진입 없음 — 통합 표와 100% 일치 | P0 |
| 기본 기능 | SGI: 각 코어 → 각 코어, IRM=1 broadcast(자기 제외) | 모든 코어 쌍 | 둘 다 | 대상 코어만 수신, broadcast는 자기 제외 전부 | P0 |
| 기본 기능 | PPI: 코어별 generic timer | 모든 코어 | 둘 다 | 해당 코어만 수신 | P0 |
| 설정 조합 | SPI 라우팅: 각 affinity + 1-of-N | 소스 샘플 × 코어 전부 | Si | 지정 코어만 수신; 1-of-N은 참여 코어에 분산·opt-out 코어 0 | P1 |
| 설정 조합 | group/보안: Group 0 = FIQ, NS Group 1 = IRQ, Secure 설정을 NS에서 접근 | TrustZone | Si | 신호 종류 일치, NS에서 Secure 설정 변경 불가 (negative) | P1 |
| 경계 | 최저/최고 INTID(32, 최대 SPI), 1020–1023 처리 | 경계 INTID | 둘 다 | 정상 동작, 1023 읽기는 아무것도 ack 안 함 | P1 |
| 인터럽트 동작 | edge: 처리 중 재발생 → active+pending → 2회 전달 | 1 소스 | 둘 다 | 2회 수신 정확 | P0 |
| 인터럽트 동작 | edge 합침: ack 전 edge 여러 번 | 펄스 3회 | 둘 다 | 1회 전달(정상) — 장치 카운터로 손실 감지 가능함을 확인 | P1 |
| 인터럽트 동작 | level: ack 전 deassert → spurious | 글리치 | 둘 다 | IAR 1023, ISR 무해 처리 | P1 |
| 우선순위 | 동시 pending 3개 서로 다른 priority | 순서 측정 | 둘 다 | 높은 priority 먼저 (동일 priority 순서는 가정 안 함) | P0 |
| 우선순위 | PMR 경계: priority = PMR, PMR−1 | 스윕 | 둘 다 | priority < PMR만 전달 | P0 |
| 우선순위 | preemption + BPR (예: 0x10/0x20/0x21, sub 4비트) | 중첩 | 둘 다 | 0x10은 끼어듦, 0x20은 0x21 처리 중 대기, RPR 값 일치 | P1 |
| 에러·negative | 잘못된 EOI(순서 틀림, ack 안 한 INTID), 비활성 INTID 설정 변경 | SW 오류 주입 | 둘 다 | 문서된 동작, 시스템 hang 0 | P2 |
| 리셋·전원 | 코어 off/on: GICR_WAKER ProcessorSleep/ChildrenAsleep, 꺼진 코어 대상 인터럽트로 wake | 1000회 | Si | wake 100%, pending 손실 0 | P0 |
| 리셋·전원 | 전원 도메인 off 중 pending → 복귀 후 처리 | suspend-resume | Si | 손실 0, 중복 0 | P1 |
| 동시성·stress | 모든 SPI + SGI 폭주, 장치에 시퀀스 번호 | 10^7 인터럽트, 전 코어 | Si | 장치 측 카운트 = 처리 카운트 (level 소스), 중복 0, hang 0 | P1 |
| 성능 | latency: SGI 송신(CNTVCT) → 대상 ISR 첫 줄(CNTVCT) | idle / DDR·IRQ 부하 | Si | p50/p99/max 보고, p99 < X µs [추정, 아키텍처 요구에서] | P0 |
| 성능 | GPIO 엣지 → ISR GPIO 토글 (scope) | 10^5 샘플 | Si | 위와 일치(교차 검증) | P1 |
| PVT | latency·wake를 V/T/F 코너에서 | DVFS 단계별 | Si | 코너에서도 기준 충족 | P1 |
| soak | 실 OS + 모든 드라이버 + 부하 | 72 h | Si | 의도치 않은 spurious 폭주·lost IRQ 0, `/proc/interrupts` 카운트 일관 | P1 |
| 시스템 | Linux DT의 interrupts 속성 vs 통합 표 | 자동 대조 | Si | 불일치 0 | P0 |

### 3. 우선순위 · A0 첫 주

- **P0 bring-up**:
  1. GICD_TYPER·IIDR로 "칩에 들어간 GIC가 스펙대로 구성됐나" (SPI 수, 보안 상태).
  2. 코어별 PPI(generic timer tick)와 SGI(코어 간) — 이게 안 되면 OS SMP 부팅 불가.
  3. **소스별 배선·trigger type 1:1 테스트** — 콘솔 UART·I2C(PMIC)·DMA부터, 그 다음 전체.
  4. 코어 off/on wake — 전원 관리 bring-up의 전제.
  5. latency 1차 측정(idle) — 기준선.
- **P1**: 라우팅 전 조합·1-of-N, 보안 group, preemption/BPR, stress 10^7, PVT latency, soak.
- **P2**: 잘못된 SW 사용 negative, 경계 INTID 세부.
- **안 하는 것**: GIC 내부 arbitration 로직·레지스터 접근 조합 전수 — Arm/vendor IP + DV에서 이미 검증. 실리콘에서 시간 쓸 곳은 **"우리가 연결한 것"(배선·type·라우팅·전원·클럭)과 "실제 시스템에서의 latency"**.

### 4. 장비 · 셋업 · 관찰 지점

```text
  core0: t0 = CNTVCT ; write ICC_SGI1R (SGI 7 → core1)
  core1: ISR entry: t1 = CNTVCT ; IAR ; ... ; EOI       latency = t1 - t0  (같은 system counter)
  ─────────────────────────────────────────────────────────────────────────────
  GPIO_out ──(점퍼)──► GPIO_in (edge IRQ) ──► ISR first line: toggle GPIO2
  scope ch1 = GPIO_out, ch2 = GPIO2   → 하드웨어 기준 latency 교차 검증
  부하 생성: DDR memcpy 코어 N개, 다른 IRQ 폭주 소스, DVFS 단계
```

- **배선 테스트 하네스**: 통합 표(소스 이름 · INTID · type · 대상)를 읽어 소스마다 ① 모든 INTID enable, catch-all ISR ② 소스 발생(force 레지스터/기능) ③ 수신 INTID·횟수 기록 ④ level이면 장치 clear 전 EOI로 재진입 확인 → 표와 diff. GIC 쪽을 분리하려면 GICD_ISPENDR로 SW pending을 만들어 "GIC→코어" 경로만 먼저 검증.
- **관찰**: ICC_HPPIR1(최고 pending), ICC_RPR(running priority), GICD/GICR ISPENDR·ISACTIVER(INTID별 상태), 장치 status, Linux `/proc/interrupts`(코어별 카운트), `/sys/kernel/irq/<n>/`, trace `irq_handler_entry`.
- **Linux 도구**: `cat /proc/interrupts`, `echo <mask> > /proc/irq/<n>/smp_affinity`(라우팅), `cyclictest`(timer latency) [추정: rt-tests], `perf`/ftrace irq 이벤트.

### 5. 잘 터지는 버그

1. **배선 오류: 두 peripheral IRQ가 뒤바뀜/같은 INTID에 묶임** → 한 장치 인터럽트가 다른 드라이버로 가서 "가끔" 에러. 잡는 법: 소스별 1:1 테스트(다른 INTID 0회 확인까지).
2. **trigger type 불일치**: level 출력 장치를 edge로 설정 → 처리 중 재발생 놓쳐 장치가 영원히 대기(인터럽트 "멈춤"); edge 펄스 장치를 level로 → 짧은 펄스라 spurious. 잡는 법: type 일치 행 + spurious 카운트.
3. **장치가 펄스 폭이 너무 짧은 edge를 냄**(GIC 동기화 클럭보다 짧음, 클럭 도메인 교차) → 저주파 GIC 클럭/DVFS 저점에서만 IRQ 누락. 잡는 법: PVT/DVFS 코너의 stress 카운트 비교.
4. **코어 off 상태 wake 실패**: 대상 코어가 꺼진 상태에서 SPI가 오면 PMU로 wake 요청이 안 감 → 전원 관리 켜면 간헐 hang. 잡는 법: GICR_WAKER·wake 1000회 행.
5. **ISR이 1023을 처리 안 함/EOI를 spurious에 씀** → 드라이버 경고·드문 hang. 잡는 법: level 글리치 spurious 행.
6. **장치 status를 EOI 뒤에 지움**(level) → 매번 1회 추가 spurious/재진입, 부하에서 CPU 낭비. 잡는 법: level 재진입 행 + 인터럽트 카운트 vs 장치 이벤트 비율.
7. **보안 설정 누락**: Secure 전용 인터럽트를 NS에서 수정 가능 → 보안 결함. 잡는 법: NS 접근 negative 행.
8. **latency가 부하에서만 튐**(interconnect 경합으로 GIC 레지스터 접근 지연, 긴 IRQ-masked 구간) → p99는 정상, max만 위반. 잡는 법: 부하 하 p99/max 보고 (§실습의 Monte Carlo가 보여 주듯 평균은 거의 안 변함).

### 6. 10분 영어 답 뼈대

1. "Clarify: I'll assume a licensed GICv3-class controller with an ITS, multi-cluster, two security states, and an integration spec listing every source, INTID, trigger type and target."
2. "Picture: Distributor for shared interrupts and routing, a Redistributor per core for SGIs, PPIs and wake, the CPU interface with acknowledge, EOI, priority mask and binary point, and the ITS for MSIs."
3. "Because the GIC logic itself is licensed and DV-verified, my plan centers on what we integrated: wiring, trigger types, routing, power and clocks, and real-system latency."
4. "Registers first: GICD_TYPER must report the SPI count and security features from the integration spec; check implemented priority bits."
5. "The most important test is source-by-source connectivity: trigger every peripheral's interrupt and check it arrives exactly once at the expected INTID and nowhere else, and that level sources re-fire if not cleared while edge sources don't — generated from the integration table, diffed automatically."
6. "SGIs between every pair of cores and broadcast; PPIs per core; SPI routing to each core and 1-of-N."
7. "Semantics: edge re-assert while active gives active-and-pending and a second delivery; multiple edges before ack coalesce; a level glitch gives spurious 1023 and the handler must cope."
8. "Priority: higher priority first — without assuming order among equal priorities, which is implementation defined — the PMR boundary, and preemption with binary point."
9. "Security: Group 0 as FIQ, non-secure can't touch secure configuration."
10. "Power: cores off and on with the Redistributor wake handshake, interrupts targeting a sleeping core wake it, no lost pending across suspend."
11. "Stress: ten million interrupts across all sources and cores, device-side counts must equal handled counts."
12. "Latency: SGI timestamped with the shared system counter on both cores, cross-checked with GPIO and scope; report p50, p99 and max idle and under DDR and interrupt load, at DVFS corners."
13. "Pass criteria: 100 percent of sources match the table, zero lost or duplicated in stress, latency p99 below the architecture requirement."
14. "P0 week one: TYPER, timer PPI and SGIs for SMP boot, wiring of console, PMIC I2C and DMA, core wake, baseline latency."
15. "Risk: wiring and trigger-type mistakes and short pulses crossing clock domains — the kind of thing that only shows at low DVFS points."

### 7. 꼬리 질문

<details>
<summary>Q1. Edge versus level — what goes wrong if the type is misconfigured, and how does your test detect it?</summary>

- **EN**: "If a level-output peripheral is configured as edge, a new event while the line is still high generates no new edge, so the interrupt can be lost and the device waits forever. If a pulse-output device is configured as level, the pending state follows the line, so a short pulse may be gone by the time the CPU acknowledges and you read 1023, a spurious. My test triggers each source and, for level sources, does EOI without clearing the device: a level interrupt must immediately re-fire, an edge one must not. Comparing that against the integration table catches the misconfiguration for every source."
- 한국어 핵심: level을 edge로 → 누락·장치 대기, edge 펄스를 level로 → spurious. "장치 안 지우고 EOI → 재진입 여부"로 소스별 판정.

</details>

<details>
<summary>Q2. How exactly do you measure interrupt latency, and what number do you report?</summary>

- **EN**: "Two independent ways. Software: core zero reads the system counter and sends an SGI; the target core reads the same counter at the first instruction of the handler — both cores share one system counter, so the difference is the latency. Hardware: a GPIO edge into an interrupt, the handler toggles another GPIO, and the scope measures the gap. I report p50, p99, p99.9 and max over at least a hundred thousand samples, idle and under memory and interrupt load, because the mean hides exactly the cases that matter — in my toy model the mean was 1.5 microseconds while the max was 48."
- 한국어 핵심: SGI + 공유 system counter / GPIO + scope 교차 검증, p50·p99·p99.9·max, 부하 유무. 평균은 숨긴다.

</details>

<details>
<summary>Q3. How do you validate interrupt routing to cores that are powered down?</summary>

- **EN**: "Power the target core off through the normal PSCI path so the Redistributor marks it asleep, then fire an SPI routed to that core. The GIC must raise a wake request, the core comes up, completes the Redistributor wake handshake, and takes the interrupt. I count that it's delivered exactly once, and repeat a thousand times at different points in the power-down sequence, because races between the sleep handshake and a new interrupt are a classic integration bug."
- 한국어 핵심: 코어 off → 대상 SPI 발생 → wake → 정확히 1회 처리, 전원 시퀀스 각 시점에서 1000회.

</details>

<details>
<summary>Q4. Two cores send the same SGI to a third at the same time. How many interrupts arrive?</summary>

- **EN**: "On GICv3, one — SGIs are banked only by the target, so the target can only have one instance of a given SGI pending. On GICv2 it was banked by source as well, so you'd get two, distinguished by the source CPU ID in the acknowledge value. So a test or software protocol that counts SGIs as messages is wrong on v3; the message has to be in shared memory and the SGI is just a doorbell."
- 한국어 핵심: v3는 1번(대상별 banked), v2는 2번. SGI = 초인종, 메시지는 공유 메모리에.

</details>

<details>
<summary>Q5. The integration spec has 400 interrupt sources. How do you not drown?</summary>

- **EN**: "Generate it. The integration table is machine-readable, so a harness iterates over sources: enable everything with a catch-all handler, trigger the source, record which INTID fired and how many times, check the level/edge behavior, and diff against the table. The triggering is the hard part — I'd push for a force-interrupt bit in each peripheral, and for the rest reuse each block's basic functional test. The same table also generates the device tree interrupt properties, so software and hardware can't drift apart."
- 한국어 핵심: 통합 표 → 자동 하네스(catch-all + 발생 + diff). 발생 수단(force 비트) 요구, DT도 같은 표에서 생성.

</details>

### 8. Don 연결

- "At Solidigm I brought up Cortex-R8, R82 and M0+ cores on the FPGA prototype, where getting interrupt wiring and trigger types right for the PCIe/NVMe subsystem and DMA was part of bring-up — [숫자 채우기: interrupt sources / bugs]. I'd apply the same table-driven approach here: one source of truth for INTID, type and target, and a harness that diffs reality against it."

## 실습 · 코드 (onsitePrep/n06/c)

실행: `onsitePrep/n06/c/run.sh` (macOS, `-isysroot MacOSX26.4.sdk`, `-Wall -Wextra -Werror -fsanitize=address,undefined`). 아래는 2026-11-01 실제 실행 출력.

### 1) I2C 타이밍 체커 — [i2c_timing_check.c](onsitePrep/n06/c/i2c_timing_check.c)

- **무엇**: 컨트롤러 카운트(LCNT/HCNT/HDSTA/SUSTO/BUF/SDAHOLD @ 100 MHz)와 버스 edge(tr 20–1000 ns, tf 10–300 ns, 모드별 spec 범위 내만)로 UM10204 파라미터를 30/70% 기준으로 계산하고 Table 11과 비교. clock synchronization(SCL이 70%를 넘은 걸 감지한 뒤 HCNT 시작)을 모델링 [추정: 흔한 컨트롤러 동작]. 하강은 선형, 상승은 RC로 가정. 파라미터별 threshold 짝짓기(예: tHD;STA = SDA 30% → SCL 70%)는 이 모델의 가정 [추정].
- **결과 요약 (발췌)**:

```text
== F1 Fm naive 50/50 duty  [Fm 400k, ic_clk 100 MHz, LCNT=125 HCNT=125 HDSTA=60 SUSTO=60 BUF=130 SDAHOLD=30]
   tr=  20 tf= 10 | fSCL(k)=396 tLOW=1241! tHIGH=1258 tHD;STA=590! tSU;STO=608 tBUF=1279! tSU;DAT=930 tHD;DAT=290  -> FAIL
   tr= 300 tf= 10 | fSCL(k)=342 tLOW=1359 tHIGH=1258 tHD;STA=590! tSU;STO=726 tBUF=881! tSU;DAT=650 tHD;DAT=290  -> FAIL
== F2 Fm spec-min counts (130/60)
   tr=  20 tf= 10 | fSCL(k)=519! tLOW=1291! tHIGH=608 tHD;STA=590! tSU;STO=608 tBUF=1279! tSU;DAT=980 tHD;DAT=290  -> FAIL
== F3 Fm sized for edge envelope  [LCNT=185 HCNT=65 HDSTA=95 SUSTO=65 BUF=175 SDAHOLD=35]
   tr=  20 tf=300 | fSCL(k)=396 tLOW=1333 tHIGH=875 tHD;STA=650 tSU;STO=658 tBUF=1947 tSU;DAT=1480 tHD;DAT=50  -> ok
   tr= 300 tf= 10 | fSCL(k)=342 tLOW=1959 tHIGH=658 tHD;STA=940 tSU;STO=776 tBUF=1331 tSU;DAT=1200 tHD;DAT=340  -> ok
== P2 Fm+ derived (71/27)
   tr=  20 tf=120 | fSCL(k)=992 tLOW=508 tHIGH=360 tHD;STA=260 tSU;STO=268 tBUF=732 tSU;DAT=560 tHD;DAT=10  -> ok
== S1 Sm SDAHOLD=0 (hold bug)
   tr=  20 tf=120 | fSCL(k)=100 tLOW=5198 tHIGH=4690 tHD;STA=4180 tSU;STO=4008 tBUF=6212 tSU;DAT=5380 tHD;DAT=-120!  -> FAIL
RESULT F1 Fm naive 50/50 duty               failing_points=9 expected_FAIL -> PASS
RESULT F2 Fm spec-min counts (130/60)       failing_points=9 expected_FAIL -> PASS
RESULT F3 Fm sized for edge envelope        failing_points=0 expected_PASS -> PASS
RESULT P1 Fm+ (60/40)                       failing_points=3 expected_FAIL -> PASS
RESULT P2 Fm+ derived (71/27)               failing_points=0 expected_PASS -> PASS
RESULT S1 Sm SDAHOLD=0 (hold bug)           failing_points=12 expected_FAIL -> PASS
RESULT S2 Sm derived, SDAHOLD=30            failing_points=0 expected_PASS -> PASS

SUMMARY 7/7 configs matched expectation
```

- **읽는 법 (면접에서 쓸 포인트)**:
  - F1: 50:50 duty는 **빠른 edge(tr 20 ns)에서 tLOW 1241 ns < 1300** — 실험실 벤치에서만 실패하는 유형. 느린 edge(tr 300)에선 tLOW는 통과하지만 **fSCL이 342 kHz로 떨어지고 tBUF가 881 ns로 위반** — 같은 설정이 버스 부하에 따라 다른 파라미터로 실패한다.
  - F2: "spec 최소값 그대로" 카운트는 clock sync 때문에 **fSCL이 400 kHz를 넘는다(519 kHz)** — 최소값 + edge 시간을 따로 예산에 넣어야 한다.
  - F3·P2·S2: 모든 legal tr/tf 코너를 통과하도록 역산한 카운트 (예: Fm은 L+H ≥ 248, L ≥ 182, BUF ≥ 172 카운트). 대가는 느린 버스에서 fSCL 342 kHz — **"최대 주파수는 빠른 edge 코너에서 지키고, 느린 버스에선 주파수 저하를 수용"**.
  - S1: SDA hold 0 → tHD;DAT 음수 = SCL이 30% 아래로 내려가기 전에 SDA가 바뀜 (Table 11 주석: SDA가 불확정 구간에 들어가기 전에 SCL이 0.3 VDD 아래여야 함) [1].
  - 이 모델은 테스트 플랜의 **기대값 계산기**이지 측정이 아니다 — 실리콘에선 analyzer/scope 실측값을 같은 판정 함수에 넣는다.

### 2) I2C 에러 경로 fault-injection — [i2c_ctrl_fault_model.c](onsitePrep/n06/c/i2c_ctrl_fault_model.c)

- **무엇**: 가상의(벤더 무관) 컨트롤러 reference model + fault injector. 각 행 = 주입 fault + 기대 관측값(정확한 STATUS 값, ACK된 바이트 수, STOP 여부, 버스 상태, IRQ 정확히 1회, TX FIFO flush) + **복구 확인(W1C 후 다음 정상 전송 성공)**. 매트릭스의 에러 행을 그대로 실행 가능한 표로 만든 것.

```text
RESULT C1-F00 normal 4-byte write                status=0x01 acked=4 stop=1 bus=idle           irq=1 -> PASS
RESULT C1-E01 NACK on address (no device)        status=0x02 acked=0 stop=1 bus=idle           irq=1 -> PASS
RESULT C1-E02 NACK on data byte 2                status=0x04 acked=2 stop=1 bus=idle           irq=1 -> PASS
RESULT C1-E03 NACK on data byte 0                status=0x04 acked=0 stop=1 bus=idle           irq=1 -> PASS
RESULT C1-E04 arbitration lost at addr bit 3     status=0x08 acked=0 stop=0 bus=owned_by_other irq=1 -> PASS
RESULT C1-E05 SCL stretch 5 ms > 1 ms timeout    status=0x10 acked=0 stop=0 bus=held_by_target irq=1 -> PASS
RESULT C1-F06 SCL stretch 300 us < timeout       status=0x01 acked=4 stop=1 bus=idle           irq=1 -> PASS
RESULT C1-E07 SDA stuck low before START         status=0x40 acked=0 stop=0 bus=held_by_target irq=1 -> PASS
RESULT C1-E08 TX FIFO overflow (9 into 8)        status=0x21 acked=8 stop=1 bus=idle           irq=1 -> PASS
SUMMARY 9/9 PASS
```

- 포인트: arbitration lost는 **STOP을 내지 않는다**(버스는 이긴 controller 것) [1 §3.1.8], timeout은 SCL이 붙잡혀 STOP 불가 → 버스 상태가 "target이 잡고 있음"으로 남고 복구 절차가 따로 필요. 이런 "에러별로 다른 사후 상태"를 기대값 표로 미리 적어 두는 게 플랜의 일.

### 3) GIC 모델 + latency Monte Carlo — [gic_model.c](onsitePrep/n06/c/gic_model.c)

- **무엇**: 단일 코어·단일 group·EOImode 0의 GIC-like 모델(레지스터 정확도 아님). edge/level 상태 기계, PMR, BPR preemption, IAR/EOI, spurious 1023, SGI RAO/WI를 directed 테스트로 확인. 그리고 고우선 인터럽트 latency를 "저우선 ISR 20 µs가 10% 시간 점유 + 30 µs IRQ-masked 구간이 1% 시간"으로 가정한 장난감 Monte Carlo [모델 — 숫자는 가정값].

```text
RESULT C6-R01 after reset: nothing enabled, IAR reads 1023        -> PASS
RESULT C6-E01 edge: re-assert while active -> active+pending -> 2nd delivery -> PASS
RESULT C6-E02 edge: 3 edges before ack coalesce into 1 delivery (count in device!) -> PASS
RESULT C6-L01 level: EOI without clearing device -> immediate re-delivery (storm) -> PASS
RESULT C6-L02 level: clear device then EOI -> no re-delivery      -> PASS
RESULT C6-L03 level: deassert before ack -> IAR 1023 (handler must tolerate spurious) -> PASS
        order taken: 34 35 33 (prio 34=0x40 35=0x40 33=0x90)
RESULT C6-P01 priority: lowest value first, tie -> lowest INTID (model rule) -> PASS
RESULT C6-P02 PMR: prio 0xF0 masked at PMR 0xF0, delivered at PMR 0xF8 -> PASS
RESULT C6-P03 preemption: A(0x10) preempts C(0x21); B(0x20) waits (group prio) -> PASS
RESULT C6-N01 disabled INTID latches pending, delivered after enable -> PASS
RESULT C6-S01 SGI config write ignored: stays edge (RAO/WI)       -> PASS
RESULT C6-N02 out-of-order EOI (EOI outer before inner) is rejected by model -> PASS
SUMMARY 12/12 PASS

LAT no preemption, 20us low ISR, 30us crit mean= 1.55 p50= 0.40 p99=19.17 p99.9=28.14 max=48.34 us (N=200000)
LAT preemption on, same load           mean= 0.55 p50= 0.40 p99= 0.40 p99.9=27.36 max=30.40 us (N=200000)
LAT preemption on, crit section cut to 2us mean= 0.41 p50= 0.40 p99= 0.40 p99.9= 2.19 max= 2.40 us (N=200000)
```

- **읽는 법**: P01의 "tie → 낮은 INTID"는 **모델 규칙**일 뿐 — 실제 GIC는 IMPLEMENTATION DEFINED이므로 실리콘 테스트는 동일 priority 순서를 판정하지 않는다 [7]. P03은 Arm 가이드의 A/B/C 예시를 그대로 재현 [2]. Latency: preemption만 켜도 p99는 19 µs → 0.4 µs로 떨어지지만 **max는 30 µs 그대로** — 남은 꼬리는 IRQ-masked 구간이 만든다. 그래서 통과 기준은 p99와 max를 따로 두고, max가 걸리면 "가장 긴 IRQ off 구간"을 찾는 게 다음 디버그 단계(ftrace irqsoff 류 [추정]).

## 자주 틀리는 것

1. **"400 kHz 나오니 I2C 타이밍 OK"** — fSCL은 파라미터 하나일 뿐. tLOW·tHD;DAT·tBUF가 따로 깨진다(F1). Fm 예산 여유는 0.
2. **스코프 기본 10/90%로 I2C 측정** — UM10204 파라미터는 0.3/0.7 VDD 기준 [1].
3. **실리콘에서 DV를 반복** — 라이선스 IP의 FSM 조합 전수 테스트에 시간 쓰지 말고 통합·타이밍·PVT·interop·장시간에.
4. **에러 테스트에서 "status 비트 set"만 확인** — bus 해제·FIFO flush·IRQ 1회·**다음 전송 성공**까지가 통과 기준.
5. **GIC 테스트를 GIC 레지스터 테스트로 착각** — 실리콘 버그는 배선·trigger type·라우팅·wake에서 난다. 소스별 1:1 테스트가 1순위.
6. **동일 priority 인터럽트 순서를 테스트에서 단정** — IMPLEMENTATION DEFINED [7].
7. **edge 인터럽트 수 = 이벤트 수라고 가정** — ack 전 여러 edge는 하나로 합쳐진다. 이벤트 수는 장치 카운터/시퀀스 번호로 센다. GICv3 SGI도 대상별 1개만 pending.
8. **latency를 평균으로 보고** — p99·p99.9·max를 부하 유무로. 평균 1.55 µs, max 48 µs인 분포가 흔하다(§실습 모델).
9. **watchdog을 제품 기능으로만 봄** — 보드 팜 자동 복구와 reset reason은 검증 인프라의 전제, A0 첫 주 P0.
10. **pinmux를 손으로 테스트** — 설계 표 하나에서 테스트·DT·드라이버 데이터를 모두 생성해야 drift가 안 생긴다.

## 출처

1. NXP UM10204, "I2C-bus specification and user manual", Rev. 7.0, 2021-10-01 — Table 11(Sm/Fm/Fm+ 타이밍), Table 10(tSP 50 ns), §3.1.8 arbitration, §3.1.9 clock stretching, §3.1.11 10-bit(`11110XX`), §3.1.12 reserved addresses, §3.1.14 software reset, §3.1.16 bus clear (PDF 텍스트 추출로 확인) · https://www.nxp.com/docs/en/user-guide/UM10204.pdf
2. Arm, "Learn the architecture — Generic Interrupt Controller v3 and v4, Overview", 198123 v3.2 — INTID 범위, 상태 기계(level/edge 전이), group·FIQ/IRQ, priority 8-bit·구현 비트 수, ICFGR(SGI RAO/WI), IROUTER·1-of-N, IAR·1020–1023, running priority·BPR 예시(0x10/0x20/0x21), EOImode, SGI IRM, GICv2 vs v3 SGI banking, GICR_WAKER · https://documentation-service.arm.com/static/678108763f2a9a07789e1d9e
3. Linux kernel docs, "Linux I2C fault injection" (i2c-gpio debugfs: scl, sda, incomplete_address_phase, incomplete_write_byte, lose_arbitration, inject_panic) · https://www.kernel.org/doc/html/latest/i2c/gpio-fault-injection.html
4. Linux kernel docs, "Linux I2C slave testunit backend" (테스트 전용, multi-master·SMBus Host Notify·Alert 명령) · https://docs.kernel.org/i2c/slave-testunit-backend.html
5. Linux `include/uapi/linux/serial_reg.h` (16550 LSR 비트, MCR_LOOP 0x10, FCR trigger 1/4/8/14) · https://raw.githubusercontent.com/torvalds/linux/master/include/uapi/linux/serial_reg.h
6. Linux `drivers/watchdog/sbsa_gwdt.c` 헤더 주석 (SBSA generic watchdog 2단계 WS0 인터럽트 / WS1 리셋, action 파라미터) · https://github.com/torvalds/linux/blob/master/drivers/watchdog/sbsa_gwdt.c
7. Arm IHI 0069G, "Arm Generic Interrupt Controller Architecture Specification, GIC architecture version 3 and version 4" — GICD_TYPER.ITLinesNumber(최대 SPI INTID = 32(N+1)−1), 동일 priority 선택은 IMPLEMENTATION DEFINED·높은 priority 우선은 보장 아님 (PDF 텍스트 추출로 확인) · https://documentation-service.arm.com/static/601412d54ccc190e5e681269
8. BV10 보드 인터페이스 테스트 (I2C 풀업·9-clock 복구, SPI mode 표·JEDEC ID, UART baud 표, GPIO loopback) · [2026-11-01_BV10_board_interface_tests.html](2026-11-01_BV10_board_interface_tests.html)
