<!-- theme: pastel -->
# Scout AI — 면접관 맞춤 준비 (Jason MacDonald)

> **작성일**: 2026-10-25(일) · **인터뷰**: **2026-10-26(월) 10:00 PT** · **면접관**: Jason MacDonald, Senior Software Engineer @ Scout AI (2026-06~)
> **같이 볼 것**: [D-3 노트](2026-10-23_scout_screen_D-3_prep.html) · [준비 문서 (10-12)](2026-10-12_scout_technical_screen_prep.html) · [연습 사이트](technicalScreenPrep/index.html) · [포지션 컨텍스트](scout_ai_embedded_context.html)
> 신뢰도: `[확인됨]` = LinkedIn 경력란·리크루터 안내 · `[추정]` = 그것을 바탕으로 한 판단. LinkedIn은 열람만 했고 연결 요청은 하지 않았다.

---

## 0. TL;DR

- **Jason은 17년차 "MCU + Embedded Linux 양쪽" 실무자다** [확인됨]: CANopen 모터 컨트롤러 펌웨어, TM4C129x 부트로더·공통 HAL 라이브러리(GPIO·UART·DMA·SPI·Ethernet·CAN·NTP·USB), STM32 RTOS 네트워킹, 배터리 전류 모니터링, Embedded Linux에서 USB 카메라 영상 처리, Qt/QML. 이어서 SiriusXM(Linux·C++) → **Anduril Altius 고객 통합 리드** → **Latitude AI**(자율주행) → Scout
- **조지아(애틀랜타) 사람이다** → 리크루터가 말한 "임베디드 팀 2명, 둘 다 조지아" 중 한 명일 가능성이 높다 [추정]. 즉 **면접관 = 미래의 직속 동료**. "같이 일하기 편한 사람인가"를 같이 본다
- **질문이 나올 가능성이 높은 순서** [추정]: ① CAN / CANopen ② Embedded Linux + C++ (유저 공간 vs 커널, USB 카메라, 디버깅) ③ 자원이 빠듯한 RTOS(메모리·네트워크 스택) ④ 부트로더·HAL·칩 포팅 ⑤ 고객/플랫폼 통합과 필드 디버깅 ⑥ 배터리·ADC ⑦ 시간 동기화. 전체 표는 §2
- **코딩은 리크루터가 말한 센서·과열 감지가 여전히 1순위** [확인됨 — 리크루터 10-12]. 다만 출제자가 Jason이면 follow-up이 **CAN 프레임·비트 디코드, 부트 이미지 검증, 배터리 적분** 쪽으로 갈 수 있다 → 새 드릴 **J1~J4**(§4)
- **가장 큰 위험은 CAN이다**: Don은 CAN을 실무로 출시한 적이 없다(W8). Jason은 10년 동안 CANopen을 했다. **과장하지 말고** "프로토콜은 이해하고 SocketCAN으로 실습했다 + 비슷한 버스(I2C/SPI/UART/PCIe) 디버그 경험으로 빠르게 따라잡는다"로 답한다(§3 Q1)
- **Don의 강점과 겹치는 곳**: Jason의 HIL·필드 지원 ↔ Don의 Apple factory test-node / bring-up → MP. **"1대 → 수백 대로 늘릴 때 무엇이 깨지는가"** 이야기를 Jason의 언어로 한다(§5)

---

## 1. 면접관 프로필 요약 (LinkedIn 경력란, 2026-10-25 열람)

| 기간 | 회사 · 직함 | 무엇을 했나 | Don에게 의미 |
|---|---|---|---|
| 2026-06 ~ 현재 (5개월) | **Scout AI** · Senior Software Engineer | (설명 없음) | 입사 5개월 차. 임베디드 팀 초기 멤버 [추정] |
| 2025-03 ~ 2026-06 | **Latitude AI** · Senior SWE | (설명 없음) Ford 자회사, 자율주행 | 센서 데이터·타임스탬프·안전 감각 [추정] |
| 2023-10 ~ 2025-03 | **Anduril** · Senior SWE, **Altius Customer Integrations Lead** (애틀랜타 onsite) | 태그: Linux, Embedded Systems | Altius(UAS)를 고객 플랫폼에 붙이는 일 → 인터페이스 정의, 통합 테스트, 현장 문제 [추정] |
| 2023-01 ~ 2023-10 | SiriusXM · Staff SWE (remote) | 태그: Linux, C++ | 리눅스 유저 공간 C++ |
| 2021-11 ~ 2023-01 | **Johnson Outdoors** · Senior SWE | Humminbird 멀티 디스플레이(**Embedded Linux + C++/Qt/QML**) · i-Pilot 트롤링 모터 **배터리 전류 모니터링으로 SoC 계산**(RTXC RTOS) · **STM32 RTOS 컨트롤러 네트워크 기능 3배**(자원 빠듯, 구형 제품 backport) · **칩 수급 문제로 STM → Artery 포팅** | RTOS 메모리, 포팅, 배터리 → J2 |
| 2011-02 ~ 2021-11 (10년 10개월) | **The Charles Machine Works** (Ditch Witch) · SWE III | **TM4C129x 부트로더 + 공통 라이브러리**(GPIO·UART·DMA·SPI·외부 flash/RAM·Ethernet·**CAN·CANopen**·외부 ADC·**NTP 시간 동기화**·USB) · **CANopen으로 모터 드라이브 제어 펌웨어** · **Embedded Linux에서 USB 카메라 + 영상 처리**로 지중 조향 측정 · 반자율 경로 추종 제어 · Coldfire **μClinux** serial-to-Ethernet 변환기 · Qt/OpenGL 3D 운영자 UI · 현장 지원·교육 | CAN → J1, 부트로더 → J3, NTP → J4, 카메라 → V4L2 질문 |
| 2009-02 ~ 2011-02 | FES BarDyne · Software Developer | 유압 시뮬레이터(C++), **DAQ 기반 Hardware-in-the-Loop 인터페이스**, Embedded Debian 단일보드 컴퓨터 | HIL ↔ Don의 factory test |
| 2008 | Emerson(시험 co-op) · Kiewit(제작 인턴) · Oklahoma State 공대 튜터 | 사이클·온도·유량 시험 | 기계/시험 배경 → 실물 하드웨어 감각 [추정] |

**읽히는 사람** [추정]
- 연구자나 알고리즘 출제자가 아니라 **실제 제품을 현장까지 내보낸 실무형 엔지니어**. 화려한 자료구조 문제보다 "**이 코드가 실제 보드에서 돌아가나**", "**고장 나면 어떻게 찾나**"를 물을 사람
- MCU(TM4C·STM32·RTOS)와 Linux(μClinux·Debian·Qt) **양쪽을 다 해 봤다** → "MCU에서 할 일 vs Linux에서 할 일 나누기" 질문이 자연스럽다
- **C++ 비중이 높다** → C로 풀어도 괜찮지만, "C++이라면 이걸 어떻게 감싸겠나"(RAII, class로 드라이버) follow-up이 나올 수 있다
- 고객 통합 리드 경험 → **소통·인터페이스 문서·다른 팀과의 경계**를 중요하게 본다. 메일 Tips의 "think collaboratively"와 같은 방향

---

## 2. 예상 질문 — 빈도순 (Jason 이력 × Scout JD)

> 점수 = 이력 근거(Jason이 오래 한 것) + JD 연관(Scout 업무에 직접 필요) + 15분 경험 파트·코딩 follow-up 어디서든 꺼내기 쉬운 정도. 영어 답은 §3.

| 순위 | 주제 | 왜 나올까 | 형태 | Don의 준비 상태 | 영어 답 |
|---|---|---|---|---|---|
| 1 | **CAN / CANopen** | 10년간 CANopen 모터 제어 + Scout JD에 CAN 명시 | 개념 질문, 코딩 follow-up(프레임 디코드) | 🔴 실무 없음 · S7 실습 | §3 Q1·Q2·Q3 · [J1](technicalScreenPrep/problems/J1_can_signal_decode/index.html) |
| 2 | **Embedded Linux에서 센서·카메라 다루기** | USB 카메라 영상 처리, μClinux, Debian SBC + JD 필수 항목 | "Linux 보드에 IMU를 붙인다면?" "USB 카메라 프레임이 끊긴다면?" | 🟡 노트 A–S 완료, 출시 경험 없음 | Q4·Q5·Q6 |
| 3 | **MCU vs Linux 역할 분담** | 양쪽 다 해 봄 + Scout는 컴퓨트 보드 + MCU 구조 [추정] | 설계 토론 | 🟢 경험으로 답 가능 | Q7 |
| 4 | **자원 빠듯한 RTOS** (메모리·스택·네트워크 버퍼) | STM32 네트워크 3배 확장 | "RAM이 부족하면 무엇부터?" | 🟢 SSD FW 경험 | Q8 |
| 5 | **디버깅 사례** (현장·필드·간헐적 문제) | 고객 통합 리드 + 현장 지원 | "가장 어려웠던 버그" | 🟢 Apple/SK hynix 사례 | Q9 + [D-3 §2.3](2026-10-23_scout_screen_D-3_prep.html) |
| 6 | **부트로더 · 펌웨어 업데이트** | TM4C 부트로더 + 필드 로봇 OTA 필요 | "업데이트 중 전원이 나가면?" | 🟡 개념 OK | Q10 · [J3](technicalScreenPrep/problems/J3_boot_slot_select/index.html) |
| 7 | **HAL · 칩 포팅** | 공통 라이브러리 + STM→Artery 포팅 | "칩이 단종되면 코드를 어떻게 옮기나" | 🟢 SDK 경험 | Q11 |
| 8 | **고객/플랫폼 통합, 인터페이스 정의** | Altius 통합 리드 | "다른 팀 하드웨어와 붙일 때 어떻게 하나" | 🟢 Apple 칩 통합 | Q12 |
| 9 | **테스트 자동화 · HIL** | FES BarDyne HIL + Don의 factory test | "센서 드라이버를 어떻게 테스트하나" | 🟢 강점 | Q13 |
| 10 | **배터리 · ADC · 전류 측정** | i-Pilot SoC | 개념 또는 코딩 변형 | 🟡 | Q14 · [J2](technicalScreenPrep/problems/J2_coulomb_counter/index.html) |
| 11 | **시간 동기화 · 타임스탬프** | NTP 라이브러리 + 자율주행 센서 퓨전 | "IMU와 카메라 시간을 어떻게 맞추나" | 🟡 | Q15 · [J4](technicalScreenPrep/problems/J4_time_sync_offset/index.html) |
| 12 | **C++ in embedded** | Qt/C++ 경력 대부분 | "C++을 MCU에서 쓴다면 무엇을 피하나" | 🟡 | Q16 |

**D-3 노트와 합친 최종 순위 (코딩 파트)**

| 순위 | 문제 유형 | 근거 | 연습 |
|---|---|---|---|
| 1 | 센서 시계열 과열 감지 | [확인됨] 리크루터 힌트 | [T1-1](technicalScreenPrep/problems/T1-1_overheat_intervals/index.html) · [T1-2](technicalScreenPrep/problems/T1-2_streaming_hysteresis/index.html) |
| 2 | ISR → 링버퍼 → 태스크 | 임베디드 기본 | [T2-1](technicalScreenPrep/problems/T2-1_ring_buffer/index.html) · [E3](technicalScreenPrep/problems/E3_isr_to_task_pipeline/index.html) |
| 3 | 바이트 스트림 패킷 추출 | Neros 실제 출제 2회 | [E2](technicalScreenPrep/problems/E2_extract_packet/index.html) |
| 4 | **CAN 프레임 / 비트 필드 디코드** | Jason 10년 CANopen ⬆ | **[J1](technicalScreenPrep/problems/J1_can_signal_decode/index.html)** · [T1-5](technicalScreenPrep/problems/T1-5_raw_register_to_temp/index.html) · [T2-6](technicalScreenPrep/problems/T2-6_bit_ops/index.html) |
| 5 | HAL 위 센서 드라이버 | JD 핵심 업무 | [E1](technicalScreenPrep/problems/E1_i2c_sensor_driver/index.html) |
| 6 | 디버그 | 메일 "problem solving" | [D](technicalScreenPrep/problems/D_debug_drills/index.html) |
| 7 | 배터리 적분(SoC) | Jason i-Pilot | [J2](technicalScreenPrep/problems/J2_coulomb_counter/index.html) |
| 8 | 부트 이미지 검증 · A/B 롤백 | Jason 부트로더 | [J3](technicalScreenPrep/problems/J3_boot_slot_select/index.html) |
| 9 | 시간 오프셋 계산 | Jason NTP | [J4](technicalScreenPrep/problems/J4_time_sync_offset/index.html) |

---

## 3. 영어 30초 답 — Jason 맞춤 16개

> 큰 소리로 2번씩. **모르는 것은 모른다고 말하고, 어떻게 알아낼지를 말한다**. Jason처럼 현장 경험이 많은 사람은 과장을 금방 알아챈다.

### Q1. "Have you worked with CAN?" (가장 중요)

> "Honestly, not in a shipped product — my production buses were I2C, SPI, UART, SPMI/RFFE and PCIe. But I know CAN at the level I'd need on day one: differential bus with 120-ohm termination at both ends, priority by arbitration on the ID — lower ID wins — bit stuffing, CRC and ACK slot, and error counters that take a node from error-active to error-passive to bus-off. I built a SocketCAN practice project on Linux with `vcan`, filters and `candump`. On a new bus my first steps are the same as with any bus: scope the physical layer, confirm bitrate and sample point, then decode frames. I'd love to hear how you use CAN on the platforms — CANopen, DroneCAN, or raw frames?"

- 핵심: **정직 + 지식 + 디버그 순서 + 질문으로 넘기기**. 마지막 질문이 Jason의 전문 영역으로 대화를 돌려 준다

### Q2. "What is CANopen? PDO vs SDO?"

> "CANopen is a higher-layer protocol on top of CAN. Each node has an object dictionary — indexed parameters. SDOs are confirmed request/response transfers for configuration, reading or writing any object, slow but flexible. PDOs are unconfirmed, pre-mapped process data — like speed and current — sent cyclically, on SYNC, or on change, so they're what you use for real-time control. NMT manages node states — pre-operational, operational, stopped — and heartbeats let a master detect a node that's gone silent. The COB-ID is the function code plus the node ID, so TPDO1 of node 5 is 0x185 and its heartbeat is 0x705."

### Q3. "A CAN bus works on the bench but nodes drop off in the vehicle. How do you debug?"

> "First physical: measure termination with power off — should be about 60 ohms across CAN-H/CAN-L — check grounding, stub lengths, and look at the differential signal on a scope for ringing. Then error counters: if a node goes bus-off, read its TEC/REC and when it happens — under motor load points to EMI. Then timing: bitrate and sample point must match across nodes, and long cables need a later sample point. Then load: bus utilization above roughly 70% or a babbling node starves low-priority IDs. I'd log with a CAN analyzer and correlate with motor current."

### Q4. "How would you bring up a new I2C/SPI sensor on an embedded Linux board?"

> "Start with hardware: confirm the device on the bus with `i2cdetect` and a logic analyzer. Prototype from user space with `i2c-dev` or `spidev` to validate registers against the datasheet. For production, describe the device in the device tree — compatible, reg, interrupt GPIO — and write or reuse a kernel driver, usually IIO for IMUs or hwmon for temperature, with regmap for register access. The data-ready interrupt triggers a buffered read so user space gets timestamped samples. I validate with known stimuli and compare against the reference tool."

- 근거 노트: [R1 Top 50](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/R/2026-10-18_R1_concept_top50.html) Q13·Q38·Q41 · [S4 IIO IMU](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/S/2026-10-18_S4_iio_imu_driver_portfolio.html)

### Q5. "User space driver or kernel driver — how do you decide?"

> "User space first when it's possible: easier to debug, a crash doesn't take down the system, and `i2c-dev`, `spidev`, `libgpiod` or UIO cover a lot. I move to the kernel when I need interrupt latency, DMA, a standard interface other software expects — IIO, V4L2, netdev — or when multiple consumers share the device. The rule is: put the minimum in the kernel and keep policy in user space."

### Q6. "Frames from a USB camera are dropping. Where do you look?"

> "Measure first: timestamps on each frame to see if drops are periodic or bursty. Then the pipeline stage by stage: USB bandwidth — is it on a shared hub, is the format MJPEG or raw YUV which needs much more bandwidth; V4L2 buffers — enough buffers queued, are we returning them fast enough; then CPU — is processing slower than frame rate, so decouple capture and processing with a queue and drop old frames deliberately instead of blocking capture. Also check CPU frequency scaling and thermal throttling on the board."

- 근거 노트: [I5 V4L2](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/I/2026-10-11_I5_v4l2_media.html) · [Q2 Linux camera pipeline](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/Q/2026-10-18_Q2_linux_camera_pipeline.html)

### Q7. "What runs on the MCU and what runs on the Linux compute?"

> "The MCU owns anything with hard timing or safety: sensor sampling with precise timestamps, motor control loops, watchdog and fail-safe — if Linux hangs, the MCU puts the vehicle in a safe state. Linux owns heavy compute, networking, logging, cameras and the model. Between them a simple, versioned protocol over UART, SPI or CAN with sequence numbers and CRC, and a heartbeat both ways. The MCU timestamps samples, and we sync clocks so Linux can fuse them."

### Q8. "RAM is tight on an RTOS controller and you need to add features. What do you do?"

> "Measure before cutting: map file for static usage, stack high-water marks per task, heap usage over time. Common wins: right-size task stacks from measured high-water marks plus margin, replace dynamic allocation with fixed pools, shrink network buffers and tune the stack's pbuf counts to the real traffic, move constant tables to flash, and merge low-rate tasks. After the change I run a stress test and watch for stack overflow hooks and allocation failures."

- Jason이 STM32 네트워크 기능 3배 확장을 했으므로, 이 질문이 오면 "**How did you approach it at Johnson Outdoors?**"로 되물어도 좋다

### Q9. "Tell me about a hard bug you found on hardware."

- [D-3 노트 §2.3](2026-10-23_scout_screen_D-3_prep.html)과 [준비 문서 §7](2026-10-12_scout_technical_screen_prep.html) 스토리 그대로. **숫자는 확실한 것만**
- Jason에게 맞추는 포인트: "**재현 → 계측(스코프/LA) → 가설 → 고정 → 재발 방지 테스트**" 순서를 분명히. 현장 경험자라 "어떻게 재현했나"를 꼭 묻는다

### Q10. "How do you design a bootloader / firmware update so it can't brick a unit?"

> "Two slots, A and B. The new image is written to the inactive slot while the old one keeps running. The image has a header with magic, version, size and CRC — ideally a signature too. The bootloader validates the header and the vector table before jumping. The new slot boots in a trial state with an attempt counter; the application confirms only after it's healthy, otherwise the watchdog resets and after N failed attempts the bootloader rolls back. The commit flag is written last, so a power loss at any point leaves one valid image. On Linux the same idea is U-Boot bootcount with RAUC or SWUpdate A/B."

### Q11. "A chip goes end-of-life (or out of stock). How do you port?"

> "It's much cheaper if the code already sits on a thin HAL: drivers talk to interfaces like `i2c_write_read` and `gpio_set`, not vendor registers. Then porting is a new HAL implementation plus clock, pin-mux and interrupt setup. Things that bite: peripheral differences like DMA alignment, flash page size for the bootloader, interrupt priority schemes, errata. I'd run the same unit tests against a mock HAL and the same hardware-in-the-loop tests on both boards to prove equivalence."

- Jason의 STM → Artery 포팅이 정확히 이 경우. 되물어 볼 만하다: "**What surprised you when you ported to Artery?**"

### Q12. "How do you integrate your system with another team's or a customer's hardware?"

> "Write the interface down early — an interface control document with message formats, timing, electrical levels and error behavior — and get both sides to sign off. Build a simulator for the other side so each team can test alone. Integrate in small steps with logging at the boundary, so when something breaks we can see which side sent what. At Apple, integrating new chips meant exactly this: when a PCIe or I2C link failed, the boundary logs and a protocol analyzer told us which side to fix."

### Q13. "How do you test a sensor driver?"

> "Three layers. Unit tests against a mock HAL — I can inject NACKs, timeouts and bad data that are hard to create on real hardware. Hardware-in-the-loop on a bench rig with known stimuli — a reference sensor or a controlled temperature — run automatically on every build. Then soak and environmental tests: long runs, temperature, vibration, where intermittent faults show up. At Apple I built factory test nodes that ran this kind of check across many units, which is where scaling problems show up."

### Q14. "How would you estimate battery state of charge?"

> "Coulomb counting as the base: sample current through a shunt with an ADC, integrate over time with a high-resolution accumulator so small currents aren't truncated. It drifts because of offset error and temperature, so I calibrate zero-current offset and correct with open-circuit voltage when the battery has rested. For production I'd consider a fuel-gauge IC that already does this with a battery model. And I'd add hysteresis on low-battery alerts so they don't chatter."

### Q15. "How do you align timestamps from different sensors?"

> "Timestamp each sample as close to the hardware as possible — in the data-ready interrupt or with hardware timestamping — using one monotonic clock per processor. Then sync the MCU clock to the host: NTP-style exchange with four timestamps gives offset and round-trip delay; take the sample with the smallest delay and slew instead of stepping. For tighter sync, PTP with hardware timestamps or a PPS signal. On Linux use CLOCK_MONOTONIC for intervals, never wall-clock time."

### Q16. "Would you use C++ on a microcontroller?"

> "Yes, a subset. Classes for drivers, RAII for locks and peripherals, `constexpr` and templates for zero-cost configuration, `enum class` for type safety. I avoid exceptions, RTTI and dynamic allocation after init, and I watch template bloat in the map file. In this interview I'll write C because it's closest to what I'd write for a driver, but I'm comfortable either way."

---

## 4. 코딩 준비 — 새 드릴 J1~J4 + 시간표

> 문제는 영어, 설명은 한국어. `./run.sh J1`로 실행. **D-3 플랜을 이미 하고 있었다면 오늘은 J1만 새로 푼다.** J2~J4는 읽기용(정답 복기 10분씩)이다.

| ID | 문제 | Jason 이력 연결 | 시간 | 오늘 할 것 |
|---|---|---|---|---|
| [J1](technicalScreenPrep/problems/J1_can_signal_decode/index.html) | CAN 신호 비트 추출 + 부호 확장 + CANopen TPDO 디코드 + heartbeat 감시 | CANopen 모터 컨트롤러 10년 | 30분 | **타이머 켜고 풀기 (P0)** |
| [J2](technicalScreenPrep/problems/J2_coulomb_counter/index.html) | 쿨롱 카운팅 SoC — 나머지 보존 적분, 히스테리시스 알림, OCV 보간 | i-Pilot 배터리 전류 모니터링 | 30분 | 풀이 읽기 → Part A만 직접 |
| [J3](technicalScreenPrep/problems/J3_boot_slot_select/index.html) | A/B 부트 슬롯 선택 — 헤더·CRC·벡터 테이블 검증, 시도 횟수, 롤백 | TM4C 부트로더 | 30분 | 풀이 읽기 + Q10 답과 연결 |
| [J4](technicalScreenPrep/problems/J4_time_sync_offset/index.html) | NTP식 4-타임스탬프 오프셋, 최소 지연 샘플, slew vs step | NTP 라이브러리, 자율주행 | 20분 | 공식만 손으로 2번 |

**코딩 중 Jason이 붙일 만한 follow-up** [추정] — 미리 한 문장씩 준비

| Follow-up (English) | 짧은 답 방향 |
|---|---|
| "What if this runs in an ISR?" | ISR에서는 저장만, 계산은 태스크로. 공유 변수는 `volatile` + 크리티컬 섹션 ([E3](technicalScreenPrep/problems/E3_isr_to_task_pipeline/index.html)) |
| "How would this look on CAN instead of UART?" | 8바이트 프레임 + ID가 타입 역할. 여러 프레임에 걸치면 ISO-TP 같은 분할·재조립 필요 |
| "What if the sensor is on Linux?" | IIO buffer/`read()`로 같은 로직을 유저 공간에. 타임스탬프는 커널 것을 쓴다 |
| "How would you test this?" | 정상 → 경계 → 결함 주입(Q13). "지금 짠 테스트가 그 축소판" |
| "In C++?" | 상태를 class로, 생성자에서 init, `std::array`, 예외 없이 (Q16) |
| "What about overflow / timer wrap?" | `(uint32_t)(now - start)` 비교, 누적은 `int64_t` |

---

## 5. 15분 경험 파트 — Jason에게 맞춘 이야기 배치

> 메일의 초점 "**building and scaling hardware systems**". 스토리 원문은 [준비 문서 §7](2026-10-12_scout_technical_screen_prep.html) A·B·C. 아래는 **Jason이 반응할 연결고리**만 정리한다.

| Don 스토리 | Jason의 비슷한 경험 | 강조할 말 |
|---|---|---|
| A. Apple factory test-node (1대 → 다수 확장) | FES HIL 인터페이스, 고객 통합·현장 지원 | "What breaks when you go from one unit to hundreds" — 테스트 자동화, 장비별 편차, 로그로 원인 분리 |
| B. SK hynix FPGA/SoC bring-up (I2C·SPI·DMA) | TM4C 공통 라이브러리, 칩 포팅 | datasheet → 첫 레지스터 읽기 → 드라이버 → 재사용 라이브러리 |
| C. UART SDK + NVMe telemetry | serial-to-Ethernet 변환기, 네트워크 3배 | 프로토콜 설계(버전·CRC·시퀀스), 제한된 자원에서 처리량 |

- **Jason 질문 예측**: "How did you make it reusable for the next product?" "What did field failures teach you?" "How did you hand it off to the factory / customer?" → 각 스토리마다 **재사용·현장 교훈·인수인계** 한 문장씩 준비
- **갭 처리**: Embedded Linux 출시 경험 질문에는 "MCU·펌웨어 쪽에서 출시, Linux는 드라이버 모델(DT·IIO·V4L2·SocketCAN)을 실습으로 따라잡는 중, 초반 3개월 목표"로 답한다

---

## 6. Don이 Jason에게 할 질문 (마지막 몇 분)

1. "**You've been here about five months — what has surprised you most about the embedded stack at Scout compared to Anduril or Latitude?**" (경력을 봤다는 것을 자연스럽게 보여 줌)
2. "How is the split between MCU firmware and the Linux compute on the UGV and drone platforms today? Who owns the boundary?"
3. "Do the platforms use CAN — CANopen motor drives, DroneCAN, or custom frames?" (Q1 다음에 아직 안 했다면)
4. "How do sensor timestamps get to the Fury model — is there a sync scheme between MCU and compute?"
5. "The embedded team is in Georgia and this role would be first in Sunnyvale — how do you see the two locations working together?" (리크루터 정보 [확인됨] 기반)
6. "What does testing look like — HIL rigs, field tests at the range, both?"

---

## 7. 오늘(10-25 일) ~ 내일 아침 시간표

| 시간 | 할 것 | 완료 기준 |
|---|---|---|
| 일 오후 1 | **J1 타이머 30분** → `./run.sh J1` → 복기 | ALL PASS, 또는 막힌 지점 1줄 기록 |
| 일 오후 2 | **E2 한 번 더 (차갑게)** — D-3 플랜의 최대 약점 | 30분 안에 Part A PASS |
| 일 저녁 1 | **45분 모의 1회**: 15분 경험(영어, §5 연결고리 포함) + 30분 [D-3 §3.1 미공개 변형](2026-10-23_scout_screen_D-3_prep.html) | 녹음에 30초 이상 침묵 없음 |
| 일 저녁 2 | §3 **Q1·Q2·Q3·Q4·Q7·Q10·Q13** 소리 내어 2번 | 막힘 없이 30초 |
| 일 저녁 3 | J2·J3·J4 풀이 읽기 (각 10분) | Q10·Q14·Q15 답과 연결됨 |
| **21시** | 종료, 수면 | — |
| 월 08:30 | 워밍업: T1-2 뼈대를 빈 파일에 10분 타이핑, §3 Q1 한 번 | 손이 풀림 |
| 월 09:30 | [D-3 §5 당일 체크리스트](2026-10-23_scout_screen_D-3_prep.html), §6 질문 메모를 화면 옆에 | — |

---

## 8. 재사용 링크

- CAN: [I10 CAN](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/I/2026-10-11_I10_can.html) · [Q4 CAN 실습](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/Q/2026-10-18_Q4_can_practice.html) · [S7 SocketCAN 프로젝트](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/S/2026-10-18_S7_socketcan_project.html)
- 카메라: [I5 V4L2](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/I/2026-10-11_I5_v4l2_media.html) · [Q1 카메라 HW 인터페이스](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/Q/2026-10-18_Q1_camera_hw_interface.html) · [Q2 Linux 카메라 파이프라인](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/Q/2026-10-18_Q2_linux_camera_pipeline.html)
- 부트·업데이트: [C3 U-Boot](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/C/2026-10-10_C3_uboot_usage.html) · [L4 OTA](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/L/2026-10-17_L4_ota_update.html) · [L5 Secure boot](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/L/2026-10-17_L5_secure_boot.html)
- 시간·실시간: [F8 time/timers](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/F/2026-10-10_F8_time_timers.html) · [N1 Linux는 실시간인가](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/N/2026-10-17_N1_is_linux_realtime.html)
- 종합: [R1 Top 50](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/R/2026-10-18_R1_concept_top50.html) · [R4 디버깅 시나리오](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/R/2026-10-18_R4_debugging_scenarios.html) · [R6 스토리 브리지](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/R/2026-10-18_R6_don_story_bridge.html)
