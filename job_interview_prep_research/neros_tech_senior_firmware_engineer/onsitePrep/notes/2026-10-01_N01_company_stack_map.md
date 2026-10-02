# 🗺️ N01 · 회사 스택 지도 — 펌웨어·임베디드 공고 8개로 본 Neros

> 2026-10-01 Greenhouse 공고 105개 중 **펌웨어·임베디드 직군 8개**와 인접 직군 5개를 읽고, Neros 펌웨어 조직의 구조와 기술 스택을 추론했다. 온사이트에 누가 들어올지, 각자 무엇을 물을지, Don의 경험을 어디에 붙일지를 정리한다. 공고 원문은 `research/job_board_snapshots/2026-10-01_fw_embedded_jds.md`에 있다.

## 0. 한 장 요약

- **하드웨어**: 자체 실리콘이 없다. **STM32(ARM Cortex-M) MCU** 위에 FC, 라디오, 핸드셋 펌웨어가 돌고 [확인됨: Michael 10-01, Tennessee·Ukraine 공고], 자율비행 쪽은 **embedded Linux 컴패니언 컴퓨터**(Jetson급 가속기 [추정])를 쓴다
- **비행 스택**: **Betaflight와 PX4를 둘 다** 쓰고 그 사이에서 센서와 제어 알고리즘을 포팅한다 [확인됨: Flight SW 공고 "Port and tune … across flight stacks (Betaflight, PX4)"]
- **링크**: ExpressLRS(CRSF) 계열, LoRa C2 링크, 아날로그·디지털 비디오, 메시와 중계기 [확인됨: Test·RF Test·Connectivity 공고]
- **빌드와 테스트**: Bazel·CMake·make, GitLab CI 또는 Jenkins, Python 자동화, SITL·HITL. "Test organization"이 별도로 있다 [확인됨]
- **조직**: FW 15명(Director Adam), 그 안에 Flight SW / Platform / Peripherals / Test, 해외에 Ukraine office, 별도로 Autonomy Platform 팀과 Connectivity·RF 팀 [조직 경계는 추정]
- **지금 급한 것**: 테스트 펌웨어(연내 최대 5명), factory test FW(없음), 플랫폼 팀 리더(Lead, Platform Software 공고), Flight SW 매니저(밴드를 $225.5–316.5K로 올림 → 사람을 못 구하고 있다는 신호 [추정])

## 1. 펌웨어·임베디드 직군 8개

| # | 직군 | 팀 | 밴드 (base) | 핵심 스택 |
|---|---|---|---|---|
| 1 | Senior Firmware Engineer, Platform | Platform | $195–273K | MCU+Linux 공통 runtime(logging·telemetry·IPC·config), Bazel 크로스 컴파일, SDK |
| 2 | **Firmware Test Engineer** (Don 진행 중) | Test | $145.5–204K | HIL, Python, I2C/SPI/UART/Ethernet, GitLab CI/Jenkins, Betaflight·ELRS |
| 3 | Firmware Engineer – Flight Software | Flight SW | $145.5–204K | 비행 모드, 제어, Betaflight↔PX4 포팅, datalink·payload 통합, unit·integration 테스트 |
| 4 | Senior Embedded Linux Engineer | Platform | $165–231K | Yocto/Buildroot, 커널·드라이버·device tree, U-Boot, secure boot, OTA |
| 5 | Lead, Platform Software | Platform (리더) | $198–277K | 위 1·4를 이끄는 player-coach, MCU/RTOS + Linux, Bazel 리더십 |
| 6 | Flight Software Manager | Flight SW (리더) | $225.5–316.5K (10-01 상향) | 비행 SW 아키텍처, 릴리즈, 테스트 전략, SITL/HITL |
| 7 | Firmware Engineer – Tennessee Office | Peripherals | $138.5–194K | **STM32 필수**, FPGA, 비디오(카메라·압축·스트리밍), 핸드셋·헤드셋, clock domain crossing |
| 8 | Senior Firmware Engineer – Ukraine Office | Archer FW | 비공개 | 비행 제어·라디오 링크 코드, bare-metal/RTOS, **STM32 우대**, 테스트 케이스 작성 |

### 인접 직군 5개 (온사이트에서 마주칠 수 있는 관점)

| 직군 | 팀 | 밴드 | 펌웨어와 만나는 지점 |
|---|---|---|---|
| Autonomy Platform & Runtime Lead | Autonomy | $163.5–228.5K | C++ on embedded Linux, ROS 2/DDS/Zenoh/LCM, 실시간 스케줄링, **ring buffer + triggered capture 로깅**, PTP/PPS 시간 동기, HIL 벤치 |
| Senior Platform Engineer (10-01 신규) | Autonomy | $163.5–228.5K | 위와 같음 + MIPI CSI-2·GMSL 카메라, IMU 드라이버, 결정적 로그 재생 |
| Principal Connectivity Engineer (10-01 신규) | Connectivity | $255–357K | 비디오·C2·텔레메트리·메시 링크 아키텍처, 링크 버짓, 필드 업데이트 |
| Lead RF Test & Integration Engineer | RF Test | $162.5–227.5K | RF 검증, EVT/DVT/PVT, **제조 acceptance 테스트**, Python 자동화, GR&R |
| Senior EW Test & Evaluation Engineer (10-01 신규) | RF/EW | $187–261.5K | 재밍 대응 테스트, 주파수 호핑, 대응책 스펙 도출 |

## 2. 공고에서 뽑은 기술 스택

| 영역 | 스택 | 근거 |
|---|---|---|
| MCU | **STM32 (ARM Cortex-M)**, bare-metal과 RTOS(FreeRTOS, Zephyr) | Tennessee "STM32 family", Ukraine "ARM Cortex-M e.g. STM32", Tennessee nice "FreeRTOS, Zephyr", Michael 10-01 |
| 언어 | **C (주력)**, C/C++, C++ (autonomy), Python (테스트·툴), Rust (nice) | 전 공고 |
| 비행 스택 | **Betaflight + PX4** (ArduPilot 언급), MAVLink, uORB, EKF2, QGroundControl | Flight SW, Test, Manager 공고 |
| RC·링크 | **ExpressLRS**(CRSF), LoRa C2, 디지털·아날로그 비디오, 메시, 중계기 | Test, RF Test, Connectivity 공고 |
| 주변장치 | **I2C, SPI, UART, DMA 파이프라인, ADC/DAC, 인터럽트**, Ethernet, MIPI CSI-2, GMSL | Tennessee, Ukraine, Test, Senior Platform 공고 |
| FPGA·비디오 | FPGA 비디오 변환·압축·스트리밍, MCU↔FPGA 고속 데이터, GStreamer·ffmpeg | Tennessee 공고 |
| Embedded Linux | Yocto/Buildroot, 커널 드라이버, device tree, U-Boot, secure boot, PREEMPT_RT, OTA(Mender, RAUC, SWUpdate, OSTree) | Embedded Linux, Lead Platform 공고 |
| 공통 플랫폼 | logging, telemetry, IPC, configuration 라이브러리, SDK | Platform, Embedded Linux 공고 |
| 빌드 | **Bazel**, CMake, make, 크로스 컴파일 toolchain, hermetic·reproducible | Platform, Test 공고 |
| CI·테스트 | **GitLab CI**, Jenkins, Python 자동화, **SITL/HITL**, flight test | Test, Flight SW, Manager 공고 |
| 디버그 도구 | 오실로스코프, 로직 애널라이저, **JTAG/SWD 디버거** | Tennessee, Ukraine 공고 |
| Autonomy | C++ on embedded Linux, ROS 2/DDS/Zenoh/LCM, Jetson/TensorRT, Qualcomm, TI, Hailo, PTP/gPTP, PPS | Autonomy 공고 |
| 제조 | 공장 provisioning, 유닛별 calibration, 대규모 fleet staged update | Autonomy Lead nice, RF Test 공고 |

## 3. 조직도 추정

```
                  CEO / CTO
                      │
      ┌───────────────┼──────────────────┬──────────────┐
  Firmware (Adam)   Autonomy          Connectivity/RF   Ground SW
  ~15명              Platform&Runtime   RF Test, EW T&E    (GCS)
      │
  ┌───┼───────────┬────────────┬──────────────┐
 Flight SW     Platform      Peripherals     Test (Michael)
 (매니저 채용중) (리더 채용중)  (Tennessee)     FW Test 2명 + 최대 5명 채용
 Betaflight/PX4 runtime·Linux  핸드셋·헤드셋    HIL·SITL·CI
                Bazel·SDK     FPGA 비디오     factory test FW(신설 필요)

 + Ukraine office: Archer 비행 제어·라디오 링크 FW
```

- 경계는 공고 문구에서 추론한 것이다 [추정]. "Test organization"이 Flight SW 공고와 Manager 공고 양쪽에 나오는 걸 보면 **Test는 Flight SW와 별개 조직**이다 [확인됨: 공고 문구]
- Michael Honor = Test 쪽 리드 [확인됨 10-01]. 2명 + 최대 5명 → 1년 안에 Test가 FW 조직의 1/3 가까이 된다

## 4. 온사이트 면접관별 관심사 [추정]

| 들어올 수 있는 사람 | 무엇을 볼까 | Don이 꺼낼 것 |
|---|---|---|
| **Adam Kibit** (Director of FW) | 전체 적합도, 레벨, 발표에서 리더십·판단력, 자동차 플랫폼 출신이라 **모듈형 플랫폼·EOL 테스트·OTA** 감각 | 발표의 bring-up과 디버깅, factory test 비전 |
| **Michael Honor** (Test 리드, HM) | 테스트 설계, HIL, factory test FW, 임베디드 기본기 퀴즈 | N07 퀴즈 뱅크, N08 factory test FW |
| **Senior FW Platform HM** (Lead, Platform Software 자리일 가능성 [추정]) | logging·telemetry·IPC·config 설계, 인터페이스 안정성, 빌드 | N06 system design의 로깅·프로토콜·config 문제, NVMe telemetry 스토리 |
| **Flight SW 엔지니어** | STM32 주변장치, ISR·DMA, 센서, 실기에서만 재현되는 버그 | N02 STM32, N04 C, N05 ring buffer |
| **Embedded Linux / Autonomy** | ring buffer + triggered capture 로깅, 시간 동기, 실시간 | N05, N06 (1) 로깅 시스템 |

## 5. 공고 문구에서 읽히는 문화 키워드

- **"Your customers are other engineers"** (Platform, Embedded Linux): 플랫폼을 제품처럼. Don의 챔버 SDK 스토리와 맞는다
- **"debug issues that only reproduce in flight"**, **"failures that only reproduce on the bench or in flight, not in a debugger"** (Flight SW): 실제 HW 디버깅 능력. Don의 Apple 루트코즈와 맞는다
- **"Test discipline appropriate to safety-relevant firmware"** (Flight SW): 테스트 문화를 키우고 싶어 한다 → Test 조직 확장과 같은 흐름
- **"single configurable runtime … rather than per program forks"** (Autonomy): 제품군이 늘면서 fork가 늘어나는 것을 경계 → 플랫폼화 단계
- **"from ambiguous goal to flying firmware"**, **"light direction"**: Michael의 "정의 안 된 것의 테스트" 질문과 같은 맥락
- **"months, not years"**, **"extreme ownership"**: Don의 "done = 80%에서 내보내고 피드백" 답이 먹힌 이유

## 6. Don ↔ 스택 매칭

| 스택 | Don | 준비 |
|---|---|---|
| Embedded C, bare-metal | ✅ 7년 production FW | N04 퀴즈 속도 올리기 |
| ARM Cortex-M | ✅ 레쥬메 "Cortex-M", Cortex-R8/R82/M0+ | **STM32 고유 사항**(HAL/LL, CubeMX, DMA 스트림, 타이머) → N02 |
| I2C/SPI/UART/DMA/인터럽트 | ✅ SoC verification, Apple I2C | N07 B·C절 |
| 스코프·LA·JTAG | ✅ Trace32, JTAG, DSO, protocol analyzer | 그대로 강점 |
| RTOS | 🟡 명시 없음 | N07 D절 (FreeRTOS 개념) |
| Betaflight/PX4/ELRS | ❌ | `../../practice/html/00_drone_architecture.html`, FW Test 준비 사이트 N06 |
| Embedded Linux·Yocto | ❌ | 질문 오면 정직하게, 컴패니언 쪽은 사용자 수준 |
| Bazel | ❌ | 개념만 (FW Test 준비 사이트 N05) |
| factory·MFG test | ✅ Apple test-node, SSD MFG FW, 챔버 플랫폼 | **N08 — 최대 기회** |
| telemetry·logging | ✅ NVMe telemetry 디버그 기능 | N05, N06 (1) |
| PCIe·고속 인터페이스 | ✅ 깊음 (Neros에선 직접 쓰임 적음) | Michael이 물어봤다 → N07 A절. Jetson 컴패니언이나 FPGA 쪽에서 쓰일 수 있다 [추정] |

## 7. 이 지도로 준비 우선순위 정하기

1. **발표 1시간** (N03): 유일하게 Don이 주도하는 시간. factory test 비전으로 마무리
2. **ring buffer 구현** (N05): 확정된 주제
3. **C/C++ 기본기와 임베디드 퀴즈** (N04, N07): Michael 스타일 퀴즈가 다시 나올 가능성
4. **generic system design** (N06): 로깅·OTA·프로토콜·config 중 하나가 나올 가능성이 높다 [추정]
5. **STM32 기본** (N02): "STM 써 봤나?"에 대한 답
6. **factory test FW** (N08): 발표와 system design 양쪽에 끌어 쓸 무기

## 체크

- [ ] 8개 직군 표를 보고 Neros FW 조직 구조를 30초로 설명할 수 있다
- [ ] "자체 실리콘 없음, STM32 + Linux 컴패니언, Betaflight + PX4"를 한 문장으로 말할 수 있다
- [ ] 면접관별 관심사 표에서 각자에게 꺼낼 스토리를 하나씩 정했다
- [ ] Test 조직이 확장 중이고 factory test FW가 없다는 사실을 발표에 어떻게 연결할지 정했다
