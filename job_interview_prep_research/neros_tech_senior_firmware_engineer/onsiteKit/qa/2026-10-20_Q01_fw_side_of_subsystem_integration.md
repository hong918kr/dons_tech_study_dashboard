# 💬 Q01 새 서브시스템 통합 — FW는 어느 부분을 포팅하고 테스트하나 (테스트 펌웨어 입장)

> 어디서: [K02](../plan/2026-10-20_K02_final_cram_three_1on1s.md) 6절 · [M3](../modules/2026-10-12_M3_hw_test_design.md) 4~5절. Michael Honar 세션의 "테스트 안 된 서브시스템 통합" 질문에서 **FW 쪽 답**.

## 1. 짧은 답

새 서브시스템(예: GPS · ESC · 새 IMU · 무선 모듈 · 카메라)이 들어오면 FW는 **아래에서 위로 7층**을 건드린다: ① 보드 설정(핀 · 클럭 · IRQ · DMA) → ② 버스 드라이버 → ③ 디바이스 드라이버 → ④ 데이터 경로 → ⑤ 시스템 자원(타이밍 · 메모리) → ⑥ 고장 처리 → ⑦ 수명 주기(설정 · 버전 · 업데이트).

**테스트 펌웨어**는 이 중 **①~③ + 진단용 일부**만 포팅한다. 제어 로직(애플리케이션)은 넣지 않는다. 목적은 "이 HW와 드라이버가 사양대로 동작하는가"를 **다른 층의 간섭 없이** 보는 것이다 — 발표 ②의 검증 FW와 같은 생각(필요한 층만 남기고 분리).

## 2. 층별로 — 무엇을 포팅하고, 무엇을 테스트하나

| 층 | 포팅 / 작성할 것 | 테스트할 것 | 통과 기준 예 |
|---|---|---|---|
| ① 보드 설정 (BSP) | 핀 mux · alternate function, 주변장치 클럭 enable, **IRQ 번호와 우선순위**, **DMA 채널/스트림 배정**, GPIO 초기 상태(reset · enable · power 핀) | 레지스터 덤프를 기대값과 비교, 핀 토글을 스코프로, **다른 주변장치와 DMA/IRQ 충돌 없는지** | 덤프 diff 0, 부팅 직후 enable 핀이 안전한 상태 |
| ② 버스 드라이버 | I2C/SPI/UART/CAN 설정(속도 · mode · baud), 타임아웃, 에러 처리, 버스 복구 | 루프백, 버스 스캔, NACK · framing error · timeout 주입, 같은 버스의 다른 장치와 동시 사용 | 에러 카운터가 의도한 주입과 일치, 막힌 버스에서 복구 |
| ③ 디바이스 드라이버 | **init 순서**(전원 후 대기 · reset · ID 확인 · 설정 쓰기 → **다시 읽어 확인**), 데이터 읽기, 단위 · 엔디안 · 스케일, 보정값, 상태/에러 레지스터 | ID(WHO_AM_I) · 설정 readback · **알려진 자극 → 기대 값** (정지 상태 IMU는 1 g, 알려진 전압 등) · 장치 self-test 기능 | ID 일치, readback 일치, 값이 허용 범위 안 |
| ④ 데이터 경로 | 샘플 주기, 버퍼 · DMA, **타임스탬프**, 소비자에게 넘기는 방식(mailbox · 큐), 좌표계 · 단위 | 실제 출력 주기 측정, **유실/overflow 카운터**, 지연(자극 → 데이터 도착), 타임스탬프 단조 증가 | 주기 오차 · 유실 0 · 지연 상한 |
| ⑤ 시스템 자원 | (테스트 FW보다 **제품 FW 통합 때** 확인) CPU 시간, stack, RAM/flash, 인터럽트 우선순위 상호작용, 공유 버스 경합, 저전력 모드 | 통합 **전/후** 루프 지터 · CPU load · stack high-water 비교 | 회귀 없음, 여유 기준 유지 |
| ⑥ 고장 처리 | 부팅 때 장치 없음, 동작 중 분리, 쓰레기 데이터, 버스 멈춤, brownout 후 재초기화 | **고장 주입**: 커넥터 뽑기 · 전원 흔들기 · 깨진 프레임 · 재부팅 중 명령 | 안전한 상태로 가고, 에러가 **보고**되고, 복구된다 |
| ⑦ 수명 주기 | 설정/파라미터 저장, **서브시스템 자체 FW 버전**(ESC · GPS · 무선 모듈) 읽기 · 업데이트, 부팅 순서 · 시간, 공장 보정값 저장 | 버전 보고, 업데이트 중 전원 차단, 보정값 저장 후 재부팅 | 버전이 로그/결과에 남음, 업데이트 실패에서 복구 |

> 모든 층에 공통: **관측 가능성** — 에러 코드 · 카운터 · 로그 필드를 같이 만든다. 공장 테스트와 필드 디버그가 이걸로 돈다.

## 3. 테스트 펌웨어에 실제로 넣는 것

```text
 [station PC: Python runner]  ⇄ UART/USB CLI ⇄  [test FW on DUT]
                                                  ├─ ① BSP (production과 같은 핀/클럭 설정)
                                                  ├─ ② bus driver
                                                  ├─ ③ device driver (가능하면 production 코드 그대로)
                                                  └─ 진단 명령 (아래)
```

| 명령 예 | 하는 일 |
|---|---|
| `bus scan i2c1` | 응답하는 주소 목록 |
| `imu id` / `imu selftest` | ID 레지스터 · 장치 내장 self-test |
| `imu raw 100` | 원시 샘플 100개 + 타임스탬프 (주기 · 노이즈 계산은 PC에서) |
| `gps ver` / `gps fix_time` | 모듈 FW 버전, cold start fix까지 시간 |
| `esc spin 1 1000 2000ms` | 모터 1을 지정 값으로 2초 (프롭 없이, 전류 제한) |
| `esc telem` | ESC가 보내는 전압 · 전류 · RPM · 온도 |
| `rail read` | 전원 레일 ADC 값 |

- 출력은 **기계가 읽는 한 줄 형식**: `RESULT imu.whoami PASS value=0x47 limit=0x47`
- **판정은 PC에서**, FW는 측정만 → 한계값을 바꿔도 FW 재빌드가 필요 없다
- 명령 하나 = 측정 하나 → 실패한 항목이 바로 보인다

### 제품 드라이버를 재사용할까, 따로 쓸까

| 선택 | 장점 | 위험 |
|---|---|---|
| **제품 드라이버 재사용** (기본값) | 실제로 출하되는 코드를 테스트한다 | 제품 드라이버가 아직 없거나 앱 로직과 얽혀 있으면 못 씀 |
| 테스트 FW 전용 얇은 드라이버 | 드라이버보다 먼저 HW를 볼 수 있다 (bring-up 초기) | 두 코드가 갈라져서 "테스트는 통과, 제품은 실패" → 나중에 **하나로 합칠 계획**을 같이 세운다 |

## 4. 순서 — 위험이 큰 것부터

1. **망가뜨릴 수 있는 것 먼저 막기**: 모터 · 히터 · 고전압 출력이 부팅 직후 꺼져 있는지 (GPIO 초기 상태, PWM 0). 프롭 없이 · 전류 제한 전원
2. 말이 통하는가: ①②③ (ID, readback)
3. 값이 맞는가: 알려진 자극으로 ③④
4. 시간이 맞는가: ④ 주기 · 지연 · 유실
5. 깨졌을 때: ⑥ 고장 주입
6. 다른 것과 같이: ⑤ 제품 FW에 넣고 회귀 (모터 켠 EMI, 공유 버스)
7. 자동화: 위 명령들을 HIL/공장 station 스크립트로 → FW 바뀔 때마다

## 5. 예시 두 개 (말로 2분)

| 새 GPS 모듈 | 새 ESC |
|---|---|
| ① UART 핀 · DMA · PPS 입력 핀 | ① 타이머 채널 · DMA (DShot/PWM), 텔레메트리 UART |
| ② baud (모듈 기본값 → 설정 후 변경), framing error | ② 프로토콜 타이밍 (비트 폭 스코프 확인) |
| ③ 버전 읽기, 출력 메시지 · 주기 설정 후 확인 | ③ 펌웨어 버전, arm 시퀀스, 명령 → RPM 응답 |
| ④ 메시지 주기 · 유실, **PPS와 타임스탬프 정렬** | ④ 명령 → 회전까지 지연, 텔레메트리 주기 |
| ⑥ 안테나 · 케이블 분리, cold/warm start | ⑥ 신호 끊김 시 정지(ESC failsafe), 과전류 · 과온 |
| ⑤ 모터 켠 상태에서 fix 품질 (EMI) | ⑤ 4개 동시 · 스로틀 급변 시 레일 · FC 리셋 없는지 |

## 6. 면접에서 말할 문장

> "On the firmware side I go bottom-up: board config — pins, clocks, interrupts, DMA — then the bus driver, then the device driver: ID, config readback, and a known stimulus that should give a known value. Then the data path: rate, drops, latency, timestamps. Then fault injection — unplug it, corrupt it, brown it out — and make sure it fails safe and reports it."

> "For test firmware specifically, I'd port only the board config and drivers, ideally the same driver code that ships, and expose each measurement as a CLI command. The firmware measures, the station PC judges against limits, so changing a limit never needs a firmware rebuild."

> "The first thing I check is that anything that can hurt someone or the board — motors, heaters — is off by default at boot."

## 7. 관련 페이지

[K02 6절](../plan/2026-10-20_K02_final_cram_three_1on1s.md) · [M3 4~6절](../modules/2026-10-12_M3_hw_test_design.md) · [M1 7절 RX/TX/Motor](../modules/2026-10-12_M1_embedded_system_design.md)
