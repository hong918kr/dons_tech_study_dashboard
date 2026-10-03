# 🐧 S06 · Embedded Linux & autonomy companion — 무엇을 만들고, 어떻게 테스트하나

> Embedded Linux 팀은 flight · ground · autonomy 팀이 올라타는 **Linux 배포판 · 커널 · bootloader · secure boot · update/recovery**를 소유하고 [확인됨 공고 5195277007], Autonomy Platform 팀은 그 위에서 **runtime · middleware · 실시간 스케줄링 · 고속 로깅 · 시간 동기**를 소유한다 [확인됨 공고 5234134007 · 5241249007]. 이 계층의 테스트는 "기능이 되나"보다 **부팅이 항상 되나 · 업데이트가 끊겨도 살아나나 · latency가 열 부하에서도 예산 안에 있나 · 로그의 시간이 맞아서 재생이 결정적인가**를 증명하는 일이다. 공고가 직접 "latency를 release gate로", "HIL bench를 자동 release gating에"라고 쓴다 [확인됨 공고 5234134007].

## 0. 한 장 요약

| 항목 | 내용 |
|---|---|
| 이 직군이 만드는 것 | Yocto/Buildroot 이미지, 커널 · 드라이버 · device tree, U-Boot · secure boot · OTA, autonomy runtime(ROS 2/DDS/Zenoh/LCM), 고속 로깅(ring buffer + triggered capture), PTP/gPTP · PPS 시간 동기, MCU↔companion 연결, 가속기 배포(Jetson/TensorRT, Hailo 등) [확인됨 공고 5195277007 · 5234134007 · 5241249007] |
| 주로 고장 나는 곳 | 부팅 실패 · 업데이트 중단, 드라이버 타이밍, **열 부하에서 latency 꼬리**, 시간 도메인 불일치(로그 재생 불가), 로깅이 제어 루프를 방해, 메시지 drop |
| 대표 테스트베드 | ① 부팅 · secure boot · A/B OTA 팜 (전원 사이클) ② latency/jitter 리그 (cyclictest + GPIO + LA) ③ 시간 동기 정확도 리그 (PPS 비교) ④ **센서 replay 벤치** (MCAP/rosbag 실시간 재생 → 출력 비교) ⑤ thermal soak 챔버 ⑥ 실보드 커널 드라이버 테스트 |
| 합격 기준 예 | 전원 사이클 1,000회 부팅 성공 100% · OTA 중단 어느 단계든 이전 슬롯 복귀 · 열 포화 상태 sensor→actuator p99 < 예산 · PPS 정렬 오차 < 1 μs [추정] · 같은 로그 replay 출력 비트 동일 |
| Don 연결 | 실리콘 **bring-up**(FPGA · SoC verification), 크로스 바운더리 루트코즈(Apple), telemetry 설계 — **embedded Linux 커널 · Yocto는 레쥬메에 없음** (정직하게) |

## 1. 이 직군 이해하기 — Neros 공고 근거

- **Senior Embedded Linux Engineer** [확인됨 공고 5195277007]: "own our embedded Linux distribution and board support packages (Yocto/Buildroot) — from board bring-up on new compute platforms to safe, reliable field updates", "kernel configuration, device drivers, and the bootloader, including secure boot and update/recovery paths". Nice: PREEMPT_RT · latency analysis, OTA(Mender, RAUC, SWUpdate, OSTree)
- **Autonomy Platform & Runtime Lead** [확인됨 공고 5234134007]: "a single configurable runtime across all Neros unmanned aircraft rather than per program forks", "end to end sensor to actuator measurement on flight hardware under sustained thermal load, and deliver the instrumentation required for latency to be enforced as a **release gate**", "full rate capture with triggered dump, time synchronization across sensor, compute and flight controller, and log integrity"
- **Senior Platform Engineer** (Autonomy) [확인됨 공고 5241249007]: "high-rate on-vehicle logging, including ring buffer and triggered capture", "clock domains and synchronization state are represented in logged data such that flight logs support **deterministic replay**", "Support hardware-in-the-loop bench infrastructure", 센서 인터페이스 MIPI CSI-2 · GMSL · SPI/I2C, nice: PX4/ArduPilot/MAVLink 경계의 arming · failsafe
- **Manager, Autonomy Evaluation, Data & Test** [확인됨 공고 5230189007]: "multi-tier regression system spanning unit and module tests, open-loop replay against logged flights, closed-loop simulation, and hardware-in-the-loop testing on production flight computers, including latency and deadline-miss measurement on target hardware"
- **Senior Data Platform Engineer** [확인됨 공고 5241234007]: 필드 로그 회수 → 시간 정렬 → 카탈로그 → "Serve logs to the evaluation harness with stable ordering, exact time alignment, and reproducible results across runs"
- 테스트 관점의 의미 [추정]: autonomy 쪽은 **이미 평가 조직(Evaluation, Data & Test)이 따로 있다**. FW 테스트가 맡을 곳은 그 아래 — **이미지 · 부팅 · 업데이트 · 시간 · latency · MCU 경계**라는 플랫폼 계층 [추정]

## 2. 이 펌웨어가 하는 일 — 데이터 흐름

```text
 cameras (MIPI CSI-2/GMSL) ─┐  IMU (SPI) ─┐        PPS / trigger lines
                            ▼             ▼              │
  ┌──────────── companion SoC (Jetson 등, Linux, PREEMPT_RT?) ───────────────┐
  │ kernel drivers + DT ──► HW timestamp ──► runtime / middleware (ROS2/DDS) │
  │                                          │ perception · estimation (GPU/NPU)│
  │   PTP/gPTP clock ◄── time sync ──────────┤                                 │
  │                                          ▼                                 │
  │   ring buffer (full rate) ──trigger──► dump to storage (log + clock state) │
  │                                          │ commands                        │
  └──────────────────────────────────────────┼─────────────────────────────────┘
        U-Boot → verified kernel/rootfs (A/B)│ UART / Ethernet (MAVLink 등)
                                             ▼
                           flight controller (STM32, see S01) ──► motors
```

- **sensor → actuator** 경로 전체의 latency가 예산이다. 공고는 이걸 열 부하 상태의 타깃 하드웨어에서 재라고 한다 [확인됨 공고 5234134007]
- 로그에는 **데이터 + 그 데이터의 시계 도메인 + 동기 상태**가 같이 기록돼야 재생이 결정적이다 [확인됨 공고 5241249007]
- MCU↔companion 프로토콜 설계는 [onsitePrep N06 5절](../../onsitePrep/site/notes/2026-10-01_N06_generic_system_design.html), 고속 로깅 설계는 [onsitePrep N06 3절](../../onsitePrep/site/notes/2026-10-01_N06_generic_system_design.html), OTA 설계는 같은 노트 4절 — 여기서는 **테스트**만

## 3. 무엇이 고장 나나

| failure mode | 증상 | 어느 레벨에서 잡나 |
|---|---|---|
| 부팅 실패 (DT 오류, 드라이버 probe 순서, rootfs 손상) | 일부 보드만 부팅 안 됨, 간헐적 hang | 부팅 팜 전원 사이클 (nightly) |
| OTA 중단 · 잘못된 슬롯 전환 | brick, 구버전 고착 | A/B OTA 팜 (release) |
| secure boot 체인 오류 | 정상 이미지 거부 · 서명 안 된 이미지 허용 | 부팅 팜 negative 테스트 (release) |
| latency 꼬리 (열 스로틀링, IRQ affinity, 우선순위 역전) | 평소엔 OK, 10분 비행 후 제어 지연 | **thermal soak + latency 리그** (nightly) |
| 시간 동기 깨짐 (PTP 미수렴, PPS 누락, 시계 점프) | 센서 융합 오차, 로그 재생 시 순서 뒤집힘 | 시간 동기 리그 (nightly), replay 검증 |
| 로깅이 제어/추론을 방해 | 저장 I/O 폭주 시 deadline miss | latency 리그 + 최대 로깅 부하 (nightly) |
| 메시지 drop · 큐 overflow (middleware QoS) | 프레임 누락, 오래된 데이터로 판단 | 센서 replay 벤치 + drop counter (PR/nightly) |
| 드라이버 회귀 (카메라 · IMU) | 프레임 rate 저하, 타임스탬프 jitter | 실보드 드라이버 테스트 (nightly) |
| 비결정적 재생 | 같은 로그인데 결과가 다름 → 회귀 판정 불가 | replay 두 번 실행 비교 (PR) |
| MCU 경계 failsafe | companion 다운 시 FC가 엉뚱하게 반응 | HIL: companion 링크 차단 주입 ([S08](2026-10-03_S08_system_integration_release.md)) |

## 4. 테스트 레벨 — 누가, 어디서, 언제

| 레벨 | 누가 | 무엇을 | 어디서 | CI 단계 |
|---|---|---|---|---|
| unit | Linux · autonomy 개발자 | C/C++ 라이브러리, 메시지 직렬화, 스케줄러 로직 | host (x86) + sanitizer | PR |
| component | 개발자 + 테스트 | 드라이버 하나, middleware 노드 하나를 실보드에서 | 실보드 (dev kit) | PR (스모크) · nightly |
| integration | **테스트 팀** + 평가 조직 | 전체 이미지 + runtime + 센서 **replay**(open-loop) | replay 벤치 (실제 companion 보드) | PR (짧은 클립) · nightly (전체 셋) |
| HIL | **테스트 팀** | companion + 실제 FC, 센서 주입, 시간 동기, latency | HIL 벤치 (production flight computer) | nightly · **release gate** |
| system | 평가 조직 + 테스트 | closed-loop sim + 기체 벤치, thermal soak | 챔버 · 기체 벤치 | release |
| field | flight test · 데이터 플랫폼 | 필드 로그의 drop · latency · 동기 상태 텔레메트리 | 필드 → 데이터 플랫폼 | 출시 후 |

- autonomy 성능 지표(정확도 등)는 **평가 조직의 몫** [확인됨 공고 5230189007]. FW 테스트는 "플랫폼이 약속한 latency · 시간 · 무결성 · 부팅"을 증명 [추정]
- 결과 데이터는 [N04 문제 C 결과 파이프라인](../notes/2026-10-02_N04_system_design_test_tooling.md)으로

## 5. 테스트베드 설계

```text
 GitLab CI ──► Yocto/Buildroot image build (sstate cache) ──► signed A/B artifacts
      │
      ├─► BOOT/OTA FARM (rack)                    ├─► TIMING HIL BENCH
      │   N × companion board                      │   companion + FC (STM32)
      │   ├ per-board relay power (PDU)            │   ├ IMU/camera inject or replay feed
      │   ├ serial console (U-Boot + kernel log)   │   ├ GPIO: "sensor captured" & "actuator cmd"
      │   ├ USB recovery mode (fallback flashing)   │   │      ──► logic analyzer / FPGA timestamper
      │   └ network (OTA server, TFTP)             │   ├ PPS generator (GNSS-disciplined) ─► all PPS inputs
      │                                            │   │      PPS outs ──► time interval counter / LA
      ├─► REPLAY BENCH                             │   └ thermal chamber or heat gun + fan control
      │   recorded MCAP/rosbag ──► replay at real  │
      │   rate into companion ──► outputs logged   └─► results ──► Postgres / dashboard (N04 C)
      │   ──► diff vs golden (bit-exact or tolerance)
```

| 장비 | 용도 | 대략 비용 [추정] |
|---|---|---|
| companion 보드 (Jetson 급 SoM + carrier) × 4–8 | 부팅 · OTA 팜 | 대당 $500–2,000 |
| 네트워크 PDU / 릴레이 보드 | 보드별 원격 전원 차단 | $300–800 |
| USB-UART 콘솔 × 보드 수 | U-Boot · 커널 로그 수집 | 개당 $15 |
| GNSS-disciplined PPS 소스 | 공통 기준 시간 | $300–1,500 |
| time interval counter 또는 LA (고속 샘플) | PPS 오차 · GPIO latency 측정 | $1,000–5,000 |
| 열 챔버 (소형) 또는 히터 + 온도 로거 | thermal soak | $2,000–10,000 |
| 스토리지 · 로그 서버 | replay 데이터셋 | $1,000+ |

**자동화 구조 (pytest)**

| 층 | 이름 예 | 역할 |
|---|---|---|
| fixture | `board` (session) · `console` | 콘솔 로그를 항상 아티팩트로 저장 |
| fixture | `power(board)` | `cycle()`, `cut_after(pattern="Starting kernel", delay_ms=...)` — 콘솔 패턴을 기준으로 차단 |
| fixture | `ota_server` | 서명된 / 손상된 / 다운그레이드 이미지 제공 |
| fixture | `latency_probe` | LA에서 GPIO 두 채널 간 지연 분포(p50/p99/max) 계산 |
| fixture | `pps_monitor` | 장치별 PPS 출력의 기준 대비 오프셋 · 수렴 시간 |
| fixture | `replay(dataset, rate=1.0)` | 로그를 실시간으로 주입하고 출력 토픽 수집 |
| marker | `@pytest.mark.thermal`, `@pytest.mark.release_gate` | 릴리즈 전용, 장시간 |

**리그 수 산정** [추정]: 전원 사이클 1회 ≈ 40초(부팅 30초 + 검증) → 보드 1대가 1시간에 ~90회. nightly 보드당 500회 목표면 보드 6대 × 6시간. OTA 중단 시나리오 5단계 × 20회 × 2 방향(업/다운) = 200회 × 3분 = 10시간 → 같은 팜의 2대를 OTA 전용으로. timing HIL은 열 포화까지 20분 + 측정 10분 → nightly 3구성이면 벤치 1–2대. 시작 구성: **부팅/OTA 팜 8대 + timing 벤치 2대 + replay 벤치 1대**.

## 6. 대표 테스트 케이스

| ID | 시나리오 | 자극 stimulus | 관측 | 합격 기준 |
|---|---|---|---|---|
| LX-01 | 부팅 안정성 | 전원 사이클 1,000회 (무작위 off 시간 0.1–5 s) | 콘솔 로그, 부팅 완료 마커까지 시간 | 성공 100%, 부팅 시간 p99 < 기준 +10% |
| LX-02 | 부팅 중 전원 차단 | U-Boot · 커널 · rootfs mount · 앱 시작 각 단계에서 차단 | 다음 부팅 결과 | 모든 경우 정상 부팅 (fs 손상 0) |
| LX-03 | A/B OTA 중단 | 다운로드 · 쓰기 · 슬롯 전환 · 첫 부팅 · health check 전 차단 | 활성 슬롯, 버전 | 항상 유효 슬롯 부팅, 첫 부팅 실패 시 자동 rollback |
| LX-04 | secure boot negative | 서명 깨짐, 다른 키, 다운그레이드(롤백 방지 카운터) | U-Boot · 커널 판정 | 전부 거부, 정상 이미지는 100% 통과 |
| LX-05 | sensor → actuator latency | 카메라/IMU 캡처 시 GPIO ↑, FC로 명령 송신 시 GPIO ↑ | LA로 지연 분포 10만 샘플 | p99 < 예산, max 기록 (예산은 팀과 합의 [추정]) |
| LX-06 | 열 포화 latency | 챔버 45 °C [추정] + 추론 최대 부하 30분 | LX-05 지표 + CPU/GPU 클럭 · 스로틀 이벤트 | p99 증가 < 20% [추정], deadline miss 0 |
| LX-07 | RT jitter | `cyclictest` + stress(CPU · I/O · 로깅 최대) | max latency | max < 100 μs (PREEMPT_RT 기준 예 [추정]) |
| LX-08 | 시간 동기 정확도 | 공통 PPS, PTP 시작 후 수렴 | 장치별 PPS 출력 오프셋 | 수렴 < 30 s, 정상 상태 오프셋 < 1 μs [추정] |
| LX-09 | 시간 동기 장애 | PTP master 단절 · PPS 케이블 분리 · 시계 점프 | 동기 상태 플래그, 로그의 clock state | 상태가 로그에 정확히 기록, 타임스탬프 역행 0 |
| LX-10 | triggered capture 무결성 | 트리거 직전 N초 + 직후 M초, 최대 로깅 부하 | 덤프 파일 체크섬 · 연속성 · 손실 | 손실 0 또는 drop counter와 정확히 일치, 제어 루프 영향 없음 (LX-05와 동시) |
| LX-11 | 결정적 replay | 같은 MCAP를 두 번 replay | 출력 토픽 해시 | 동일 (비결정 요소는 seed · 시간 소스 고정) |
| LX-12 | 드라이버 회귀 | 카메라 · IMU 스트림 10분 | 프레임 rate, 타임스탬프 간격 분포, 에러 카운터 | rate 목표 ±1%, 간격 jitter < 기준, 에러 0 |

## 7. 면접 시나리오 — "Design a test system for our companion computer platform"

### 요구사항 질문 (영어)

- "Which compute modules do we ship, and how many board variants run the same image?"
- "What's the update mechanism today — A/B with a health check, and who decides when a new slot is 'good'?"
- "What's the latency budget from sensor capture to actuator command, and is it measured on hardware today?"
- "How is time synchronized between sensors, the companion and the flight controller — PTP, PPS, trigger lines?"
- "Where does the evaluation team's regression end and the platform's begin — do they own replay, and do we own timing and boot?"

### 가정 숫자 [추정]

- 보드 변종 3종, 이미지 1개 (machine config로 분기), 빌드 sstate 캐시 사용 시 30–60분
- latency 예산 sensor→actuator 30 ms, 로깅 대역폭 200 MB/s 순간, 트리거 덤프 60초
- 릴리즈 2주마다, nightly 1회, PR은 replay 스모크 5분만

### 블록도

- 5절 그대로: **부팅/OTA 팜 · timing HIL 벤치 · replay 벤치**, 결과는 공통 DB로

### 핵심 결정과 trade-off

| 결정 | 선택 | trade-off |
|---|---|---|
| latency 측정 방법 | **외부 계측**(GPIO + LA/FPGA 타임스탬퍼) | 소프트웨어 타임스탬프는 측정 대상 시스템이 스스로를 재는 것 → 꼬리를 숨길 수 있음. 대신 GPIO 지점을 코드에 심어야 함 |
| latency 판정 기준 | 평균이 아니라 **p99 · max · deadline miss 수** | 꼬리는 샘플이 많이 필요 → 10만 샘플, nightly로 |
| 열 조건 | 챔버 포화 후 측정 | 시간 비용 큼 → nightly · release만, PR은 상온 스모크 |
| replay 판정 | 비트 동일(결정적 경로) vs 허용 오차(GPU 연산) | 비트 동일이 가장 강하지만 GPU 비결정성 → 결정적 부분과 허용 오차 부분을 분리 |
| OTA 테스트 | 실보드 팜, 단계별 차단 | 에뮬레이터로는 flash · 전원 동작을 못 믿음 |
| 이미지 빌드 | 캐시 + 재현성 해시 | Yocto 전체 빌드는 느림 → PR은 패키지 단위, nightly 전체 |

### 실패 모드

- 팜 보드 하나가 망가져 매일 실패 → 보드별 실패율 추적, 자동 quarantine ([N04 문제 B](../notes/2026-10-02_N04_system_design_test_tooling.md))
- USB recovery 경로가 없으면 brick된 보드를 사람이 복구 → 모든 팜 보드에 recovery 모드 핀 · 자동 재플래시
- 챔버 온도 편차로 latency 결과가 들쭉날쭉 → 측정 시작 조건을 "온도 안정 + 스로틀 상태 기록"으로 고정
- replay 데이터셋이 오래돼 현재 센서 구성과 안 맞음 → 데이터셋 버전 · provenance (데이터 플랫폼과 협의 [확인됨 공고 5241234007])

### 영어 2분 요약

> "I'd split it into three benches, each proving one promise the platform makes. A boot and update farm: several companion boards on switched power with serial consoles, cycled hundreds of times a night, with power cuts injected at each boot and update stage — the board must always come up on a valid slot, and secure boot must reject anything unsigned or rolled back.
>
> A timing bench with the real flight controller: the firmware raises GPIOs at sensor capture and at actuator command, and an external logic analyzer measures the distribution — p99, max and deadline misses — at full logging load and after the board reaches thermal saturation, so latency can be a release gate as the posting describes. The same bench checks time sync by comparing every device's PPS output against a GNSS reference.
>
> And a replay bench that feeds recorded flights at real rate and checks outputs are reproducible run to run. Results all land in one database, so a regression in boot time or tail latency shows up as a trend, not a surprise in the field."

## 8. 꼬리 질문

**Q. Why not trust software timestamps for latency?**

> "Because the system under test would be measuring itself — scheduling delays can hit the timestamping code too. An external analyzer on GPIO edges gives an independent clock. I'd still log software timestamps and compare them to the hardware measurement to calibrate."

**Q. How do you make latency a release gate without flaky failures?**

> "Fix the conditions — temperature, load profile, image — take enough samples to estimate p99 stably, and gate on a budget with a margin. Track the trend nightly so a slow creep is visible before it crosses the line."

**Q. The update succeeded but the new image fails after a few minutes.**

> "That's why the slot shouldn't be marked good on first boot alone. A health check — services up, sensors streaming, link to the flight controller — must pass within a window, otherwise the bootloader falls back on the next reset."

**Q. How would you test the companion going down mid-flight?**

> "On the HIL bench, kill the companion or cut its link at random points and check the flight controller's failsafe behavior and the time to detect it. That boundary — arming and failsafe between mission computer and autopilot — is called out in the posting."

**Q. You haven't done embedded Linux kernel work. How would you contribute here?**

> "Honestly, kernel and Yocto aren't on my resume. What I bring is bring-up and cross-boundary root cause on new silicon, and test infrastructure. On this platform I'd own the farms and the measurement — boot, update, timing and time sync — and learn the BSP side from the people who own it."

## 9. Don 경험 연결 (레쥬메 범위)

| 이 노트의 포인트 | Don 경험 | 말할 문장 |
|---|---|---|
| bring-up · 부팅 실패 | Solidigm **FPGA 이미지/FW bring-up** (Cortex R8/R82/M0+, PCIe/NVMe IP), SoC verification | "I've brought up firmware on new silicon before RTL freeze, so a board that won't boot is a familiar starting point." |
| 경계 버그 | Apple: 새 실리콘 통합 루트코즈 (PCIe/I2C/SPMI/RFFE) | "Most hard bugs live at a boundary — here it's sensor to companion to flight controller." |
| 측정 장비 | DSO, protocol analyzer, JTAG, 스코프/LA | "I'm comfortable instrumenting with a scope or logic analyzer instead of trusting software timestamps." |
| 로깅 · telemetry 무결성 | NVMe telemetry 디버그 기능 | "Logs are only useful if they're complete and you can trust their timing." (Don: telemetry의 타임스탬프 처리 방식 — 레쥬메에 없음) |
| 장시간 · 스케줄링 · 열 | SK hynix 챔버 테스트 플랫폼 (온도 제어 · 스케줄링) | "I built automation around temperature chambers, so thermal soak as a test stage is natural for me." |
| embedded Linux · Yocto · ROS 2 | ❌ 레쥬메에 없음 | 8절 마지막 답처럼 정직하게, 측정 · 팜 쪽으로 기여 |

## 체크

- [ ] 5절 세 벤치(부팅/OTA · timing · replay)를 보지 않고 그리고 각각이 증명하는 약속을 한 문장씩
- [ ] LX-05 GPIO + LA latency 측정과 "왜 소프트웨어 타임스탬프가 아닌가"를 30초 영어로
- [ ] LX-03 OTA 중단 단계 5개 외우기 + health check 개념
- [ ] 7절 영어 2분 요약 소리 내어 1회
- [ ] 8절 마지막 답(embedded Linux 경험 없음)을 내 말로 다듬기
