<!-- theme: pastel -->
# Scout AI — Technical Screen D-3 준비 노트

> **작성일**: 2026-10-23(금) · **인터뷰**: **2026-10-26(월) 10:00 PT** · **형식**: CoderPad 45분 = 경험 15분(building & scaling hardware systems) + 코딩 30분, 언어 자유(C)
> **🎯 10-25 추가**: [면접관 맞춤 노트 (Jason MacDonald)](2026-10-25_scout_interviewer_jason_prep.html) — CAN·부트로더·Embedded Linux 질문 + 드릴 J1–J4
> **기본 자료**: [준비 문서 (10-12)](2026-10-12_scout_technical_screen_prep.html) · [연습 사이트](technicalScreenPrep/index.html) · [포지션 컨텍스트](scout_ai_embedded_context.html)
> 신뢰도: `[확인됨]` = 리크루터 콜·메일 또는 Don의 실제 면접 기록 · `[추정]` = 그것을 바탕으로 한 판단

---

## 0. TL;DR

- **3일의 목표는 하나**: 처음 보는 센서·임베디드 문제를 **30분 안에 C로 끝까지 돌아가게** 만드는 것. 지식보다 **완주력**이 지금 가장 약하다(§1)
- **가장 큰 약점** [확인됨]: Neros에서 **패킷 추출 문제를 두 번 끝내지 못했다**(10-08 스크린 시간 부족, 10-22 온사이트 변형 문제 이해에 오래 걸림). 같은 유형이 Scout에서도 상위권이다 → 새 드릴 [E2](technicalScreenPrep/problems/E2_extract_packet/index.html)를 두 번
- **연습량이 0일 수 있다** [추정]: 10-12 이후 연습 사이트 `stub.c`에 수정 흔적이 없다. 다른 곳에서 풀었다면 괜찮지만, 아니라면 오늘 저녁부터 **타이머 켜고 실제로 타이핑**이 1순위
- **예상 문제 1~3위**: ① 센서 시계열 과열 감지(T1-1·T1-2) ② ISR → 링버퍼 → 태스크(T2-1·E3) ③ 바이트 스트림 패킷 추출(E2·T2-2). 그다음 레지스터·비트 디코드, 디버그, HAL 위 센서 드라이버(§2.1)
- **임베디드 개념 질문**은 코딩 follow-up이나 경험 파트에서 튀어나온다. `volatile`·ISR 규칙·I2C 디테일·디버깅 접근이 상위(§2.2, 영어 30초 답변 포함)
- **새로 만든 드릴 3개** (임베디드 집중): [E1 I2C 센서 드라이버(mock HAL)](technicalScreenPrep/problems/E1_i2c_sensor_driver/index.html) · [E2 패킷 추출](technicalScreenPrep/problems/E2_extract_packet/index.html) · [E3 ISR → 태스크 파이프라인](technicalScreenPrep/problems/E3_isr_to_task_pipeline/index.html)
- **경험 파트 숫자가 아직 비어 있다**: 준비 문서 §7 스토리 A·B·C의 `[ ]`를 오늘 채운다

---

## 1. 부족한 부분 진단

### 1.1 근거가 있는 약점

| # | 약점 | 근거 | 위험도 | 3일 안에 할 보강 |
|---|---|---|---|---|
| W1 | **처음 보는 문제를 빨리 이해하고 30분 안에 완주** | [확인됨] Neros 10-08 스크린: Task 2(`extract_packet`)를 시간 안에 못 끝냄. 10-22 온사이트: 문제가 약간 달라 이해에 시간이 오래 걸려 **말로 설명하고 끝남, 코드 미완** | 🔴 높음 | §4 "첫 5분 규칙"을 매 연습마다 적용. 처음 보는 변형으로 모의 2회(§3) |
| W2 | **바이트 스트림 · 패킷 추출** | [확인됨] 위 두 번이 모두 이 유형 | 🔴 높음 | E2를 토요일에 두 번(한 번은 차갑게, 한 번은 복기 후), T2-2(바이트 단위 버전) 1회 |
| W3 | **실전 타이핑 횟수** | [추정] 연습 사이트 `stub.c` 18개 모두 10-12 생성 상태 그대로. 30분 안에 ALL PASS를 낸 기록이 없음 | 🔴 높음 | 금·토·일 합쳐 **최소 8문제를 타이머로**. 정답은 30분 뒤에만 연다 |
| W4 | **언어 전환** | [확인됨] Neros 온사이트에서 C → Python으로 바꿨다가 문제 이해와 언어 전환이 겹쳤다 | 🟡 중 | Scout는 **C 고정**. 연습도 C로만 |
| W5 | **예측이 틀렸을 때의 대응** | [확인됨] Verkada 1차: 준비한 유형(bounded queue 등)이 아니라 "벤더 API 감싸기"가 나옴 | 🟡 중 | 어떤 문제든 쓰는 **범용 뼈대**(§4.3)를 손에 익힌다 |
| W6 | **경험 스토리에 숫자가 없음** | [확인됨] 준비 문서 §7 `[ ]` 미기입, Embedded Linux R6 STAR 템플릿도 `[Don 입력]` 상태 | 🟡 중 | 금요일 밤 30분: 확실히 아는 숫자만 채운다. 모르면 "about a dozen boards" 같은 정직한 범위로 |
| W7 | **Embedded Linux 실무(출시) 경험** | [확인됨] 레쥬메는 bare-metal·RTOS. 다만 10-18까지 Embedded Linux 노트 A–S 153개 완료, QEMU 랩·IIO IMU 드라이버·SocketCAN 포트폴리오 있음 | 🟢 스크린에선 낮음 · Final에선 중 | 질문 나오면 [컨텍스트 4.9 ④](scout_ai_embedded_context.html) 답 + "QEMU 랩에서 IIO 드라이버를 직접 만들었다"까지만. 출시했다고 말하지 않기 |
| W8 | **CAN · 카메라 파이프라인** | [확인됨] 실무 경험 없음. S7 SocketCAN 프로젝트, R1 Q49·Q50으로 개념은 정리됨 | 🟢 낮음 | §2.2의 Q14 30초 답만 외운다 |

### 1.2 지금 바로 해 보는 셀프 체크 (금요일 저녁, 각 항목 타이머)

> 못 하면 그게 토·일의 우선순위다. 결과를 체크박스에 표시한다.

- [ ] 준비 문서 §2.4 템플릿(`CHECK` 매크로 + `main`)을 **1분 안에** 백지에서 친다
- [ ] T1-1 과열 구간을 **25분 안에** ALL PASS
- [ ] SPSC 링버퍼(push/pop/count, 2의 거듭제곱 mask)를 **12분 안에** 백지에서
- [ ] `(int16_t)((msb << 8) | lsb) / 16`이 왜 맞는지 30초 안에 영어로 말한다
- [ ] wrap-safe 시간 비교 `(uint32_t)(now - start) >= timeout`이 왜 맞는지 예시 숫자로 설명한다
- [ ] 바이트 버퍼에서 sync → len → payload → CRC를 찾고, **부분 프레임이면 아무것도 소비하지 않는** 함수를 15분 안에 설계(코드 전 단계)까지
- [ ] "Tell me about a hardware system you built and scaled"에 90초 영어 답을 녹음 없이 끊김 없이

---

## 2. 예상 문제 — 빈도순

> **빈도를 어떻게 매겼나** [추정]: Scout 기술 스크린의 공개 후기는 아직 없다. 그래서 아래 세 가지를 합쳐 점수를 냈다.
> ① **Scout 리크루터 힌트**(콜·메일)와 직접 맞으면 +3 ② **Don이 실제로 받은 면접**(Verkada 1차, Neros HM·스크린·온사이트)에서 나왔으면 회당 +1 ③ **임베디드 CoderPad 스크린 전반에서의 흔함**(업계 일반, 추정) +1~2.
> 점수는 순서를 정하기 위한 것이지 확률이 아니다.

### 2.1 코딩 파트 (30분)

| 순위 | 문제 유형 | 근거 | 점수 | 연습 |
|---|---|---|---|---|
| 1 | **센서 시계열 과열 감지** — 구간 반환, 스트리밍 이벤트, 히스테리시스·연속 N회 | ① "sensor, time temp, detect, return overheating", "State, Edge" (+3) · ③ (+2) | 5 | [T1-1](technicalScreenPrep/problems/T1-1_overheat_intervals/index.html) · [T1-2](technicalScreenPrep/problems/T1-2_streaming_hysteresis/index.html) |
| 2 | **ISR → 링버퍼 → 태스크**, 또는 링버퍼 단독 | ② Neros 스크린 Task 1(+1) · ③ 임베디드 스크린 최다 빈출(+2) · ① "embedded constraint" 일부(+1) | 4 | [T2-1](technicalScreenPrep/problems/T2-1_ring_buffer/index.html) · [E3](technicalScreenPrep/problems/E3_isr_to_task_pipeline/index.html) |
| 3 | **바이트 스트림 패킷 추출 · 파싱 + 체크섬** | ② Neros 스크린 Task 2 + 온사이트 재출제(+2) · ③ (+2) | 4 | [E2](technicalScreenPrep/problems/E2_extract_packet/index.html) · [T2-2](technicalScreenPrep/problems/T2-2_packet_parser/index.html) |
| 4 | **레지스터 · 비트 디코드** — raw → 온도, 필드 추출·RMW, 부호 확장 | ① 센서 + "embedded constraint"(+2) · ③ (+2) | 4 | [T1-5](technicalScreenPrep/problems/T1-5_raw_register_to_temp/index.html) · [T2-6](technicalScreenPrep/problems/T2-6_bit_ops/index.html) |
| 5 | **버그 찾기 · 내 코드 디버그** | ① "debug"(+3) | 3 | [Debug D1–D8](technicalScreenPrep/problems/D_debug_drills/index.html) |
| 6 | **HAL 위 센서 드라이버** — I2C 레지스터 읽기, WHO_AM_I, 재시도, 타임아웃 | JD 핵심 업무가 센서 드라이버(+1) · ③ (+2) · Don의 "임베디드 집중" 예상 | 3 | [E1](technicalScreenPrep/problems/E1_i2c_sensor_driver/index.html) |
| 7 | **이동 평균 · 필터 · 상승률** | ① 센서 시계열(+1) · ③ (+1) | 2 | [T1-3](technicalScreenPrep/problems/T1-3_moving_average/index.html) · [T2-8](technicalScreenPrep/problems/T2-8_running_median/index.html) |
| 8 | **다중 센서 = Item 구조체 배열 + 센서별 상태** | ① "Item"(+2) | 2 | [T1-4](technicalScreenPrep/problems/T1-4_multi_sensor/index.html) |
| 9 | **센서 고장 감지** (stuck · dropout · spike · range) | ① "Edge"·센서(+1) · Scout 국방 로봇의 fail-safe 맥락(+1) | 2 | [T1-6](technicalScreenPrep/problems/T1-6_sensor_fault_detect/index.html) |
| 10 | **디바운스 · 엣지 감지** | ③ (+1) · T1-2와 같은 골격 | 1 | [T2-7](technicalScreenPrep/problems/T2-7_debounce_edge/index.html) |
| 11 | **블로킹 벤더 API를 thread-safe로 감싸기** | ② Verkada 1차(+1). 30분 1문제에는 큼 | 1 | [T1-8](technicalScreenPrep/problems/T1-8_blocking_api_wrapper/index.html) |
| 12 | 일반 LeetCode (구간 병합 · sliding max · 이분 탐색) | ① "Leetcode-ish"(+1) | 1 | [T2-3](technicalScreenPrep/problems/T2-3_merge_intervals/index.html) · [T2-4](technicalScreenPrep/problems/T2-4_window_max/index.html) · [T2-5](technicalScreenPrep/problems/T2-5_lower_bound/index.html) |
| 13 | 메모리 풀 · top-K | ③ (+1) | 1 | [T2-9](technicalScreenPrep/problems/T2-9_memory_pool/index.html) · [T2-10](technicalScreenPrep/problems/T2-10_top_k_sensors/index.html) |

**follow-up으로 붙기 쉬운 것** (1~6위 공통): "Make it streaming", "Now timestamps can wrap", "No malloc, fixed memory", "What if the ISR writes while you read?", "How would you test this on hardware?", "What's the complexity / memory footprint?"

### 2.2 임베디드 개념 질문 — 빈도순 (영어 30초 답)

> 경험 파트나 코딩 follow-up에서 나온다. **영어 답을 소리 내어 2번**씩. 리눅스 쪽은 [Embedded Linux R1 Top 50](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/R/2026-10-18_R1_concept_top50.html)의 해당 번호를 본다.

**Q1. What does `volatile` do, and when is it not enough?** — 빈도 최상
> `volatile` tells the compiler every access must really happen — no caching in a register, no removing "redundant" reads. I use it for memory-mapped registers and variables shared with an ISR. It does **not** make an operation atomic or add memory barriers: `count++` on a shared variable is still read-modify-write, so I protect it with a critical section or a C11 atomic.
- 포인트: "volatile ≠ atomic ≠ barrier" 세 단어를 꼭 말한다. → [D6 드릴](technicalScreenPrep/problems/D_debug_drills/index.html)

**Q2. What are the rules for writing an ISR?**
> Keep it short and bounded: read the hardware, clear the flag, push data into a lock-free buffer, maybe set an event — then return. No blocking, no malloc, no printf, no long loops. The heavy work goes to a task or main loop. Clear the interrupt source correctly so it doesn't re-fire.
- 포인트: "what goes in the ISR vs the task"를 E3 그림으로 설명할 수 있게

**Q3. How do you share data safely between an ISR and the main loop?**
> For a stream of samples, a single-producer single-consumer ring buffer: the ISR only writes head, the task only writes tail, so no lock is needed — with atomics or barriers for ordering on multi-core or out-of-order CPUs. For a counter or flag that the task reads and clears, I briefly mask the interrupt or use an atomic exchange, otherwise an increment between the read and the clear is lost.

**Q4. Compare I2C, SPI, and UART. When would you pick each?** — Neros HM에서 실제로 나옴 [확인됨]
> I2C: two wires, open-drain with pull-ups, addressed bus, ACK per byte, typically 100 kHz to 1 MHz — great for many slow sensors. SPI: four wires, push-pull, chip-select per device, full-duplex, tens of MHz — for fast sensors, ADCs, flash, IMUs. UART: asynchronous point-to-point, no clock line, both sides agree on baud rate — for GPS, debug consoles, radios. CAN when I need a robust multi-node bus across a vehicle.

**Q5. Walk me through an I2C register read. What can go wrong?**
> START, address with write bit, ACK, register pointer, repeated START, address with read bit, then the data bytes — the master ACKs each byte except the last, NACKs it, then STOP. Failure modes: NACK on address (wrong address, device not powered), a slave holding SDA low after a reset mid-transfer — I recover by clocking SCL nine times and sending STOP — clock stretching the master doesn't support, and weak pull-ups giving slow rise times at higher speed.
- 포인트: [E1](technicalScreenPrep/problems/E1_i2c_sensor_driver/index.html) 그림 1을 머릿속에 그리면서 말하기. 스코프로 rise time을 본 경험과 연결

**Q6. Polling vs interrupts vs DMA?**
> Polling is simplest and fine when the event is frequent or latency doesn't matter, but it burns CPU. Interrupts react quickly to rare events with no wasted cycles, but each one has overhead. DMA moves bulk data — like a burst of IMU samples or camera lines — without the CPU, and I get one interrupt per block instead of per byte.

**Q7. A sensor returns wrong or garbage values. How do you debug it?** — 빈도 높음, Don 강점
> I go bottom-up. Power and reset first, then I put a logic analyzer or scope on the bus to see whether the transaction is correct — address, ACKs, timing, signal quality. Then I check what the driver does with the bytes: register map against the datasheet, byte order, sign extension, scaling, configuration such as range and rate. I compare against a known-good reading — room temperature, gravity on the IMU — and I write a small test that dumps raw values so I can separate bus problems from conversion bugs.
- 포인트: Apple에서 I2C/SPMI 실패를 스코프·프로토콜 분석기로 루트코즈한 실제 사례 하나로 마무리

**Q8. Endianness and struct packing — how do you parse a sensor frame safely?**
> I don't cast a byte buffer to a struct: padding, alignment and byte order can all differ between the sensor and the CPU. I assemble each field explicitly — `(uint16_t)b[0] | ((uint16_t)b[1] << 8)` for little-endian — or `memcpy` and then convert. That's portable and avoids unaligned-access faults.

**Q9. Fixed-point or float on an MCU?**
> On a part without an FPU, float math is emulated and slow, so I keep temperatures as integers in 0.1 or 0.01 °C and keep the scale factor in mind. I watch overflow — sums go into a wider type — and rounding, because C integer division truncates toward zero. With an FPU, single-precision float is fine, but I still avoid it in ISRs if the context save is expensive.

**Q10. Explain the memory layout of an embedded C program. Why avoid malloc?**
> `.text` and `.rodata` in flash; `.data` initialised and copied to RAM at startup; `.bss` zeroed; then heap and stack. On a small system I avoid malloc because fragmentation and non-deterministic timing can fail late in the field. I use static allocation or a fixed-block pool, and I size stacks by measuring the high-water mark.

**Q11. Mutex vs semaphore vs spinlock? What is priority inversion?**
> A mutex is for mutual exclusion and has an owner — only the locker unlocks, and an RTOS can apply priority inheritance. A semaphore counts or signals — for example, an ISR gives it to wake a task. A spinlock busy-waits and only makes sense for very short sections on multi-core, never across a sleep. Priority inversion is a high-priority task waiting on a lock held by a low-priority task while a medium task preempts the low one; priority inheritance fixes it — the Mars Pathfinder story.

**Q12. How do you use a watchdog properly?**
> Kick it only from a place that proves the whole system is healthy — for example, the main loop kicks only after every critical task has checked in — never from a timer ISR, which would keep kicking while tasks are hung. After a watchdog reset I log the reset cause so field failures are visible.

**Q13. How do you timestamp sensor data, and keep sensors in sync?** — Scout 특화(카메라 기반 Fury + 다중 센서)
> Timestamp as close to the hardware event as possible — in the data-ready interrupt, or with hardware timestamps — using a monotonic clock, never wall time. A 32-bit millisecond tick wraps after about 49.7 days, so I compare with unsigned subtraction. For multiple sensors I'd use a shared trigger or sync pulse, or PTP across computers, and log the offset, because the model needs camera and IMU data aligned.

**Q14. What do you know about CAN?**
> It's a differential multi-master bus. Arbitration is by message ID: a dominant bit wins, so the lowest ID has priority and the winning frame isn't corrupted. Frames carry up to 8 bytes in classic CAN or 64 in CAN FD, with a CRC and an ACK slot. Nodes count errors and go error-passive, then bus-off, so one bad node can't take down the bus. On Linux I've worked with it through SocketCAN in a QEMU lab — `vcan`, `candump`, and a small sensor-kit project.
- 주의: 실무 경험은 없다. "lab project"라고 정확히 말한다 → [S7](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/S/2026-10-18_S7_socketcan_project.html)

**Q15. How would this temperature sensor show up on embedded Linux?**
> I'd describe it in the device tree under the I2C controller with its compatible string and address. A kernel `i2c_driver` probes on the compatible match, uses regmap for register access, and registers with hwmon or IIO so userspace reads it through sysfs, with alarms via thresholds. For quick bring-up I'd poke it from userspace with `i2c-dev` and `i2ctransfer` first.
- R1 Q13·Q14(Device Tree), Q38(I2C/SPI/regmap), Q41(IIO·hwmon), [S4 IIO IMU 드라이버](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/S/2026-10-18_S4_iio_imu_driver_portfolio.html)

**Q16. If a sensor fails on a robot in the field, what should the software do?**
> Detect it — range checks, stuck values, timeouts, CRC errors — report it as a fault instead of feeding bad data downstream, and fail safe according to system policy, for example treat a dead temperature sensor as "too hot" and derate the motors. Then make it diagnosable: log the raw values and error counters so we can root-cause it later.
- 포인트: T1-6 고장 감지와 연결. "정책은 시스템 팀과 정한다"는 협업 문장

### 2.3 경험 파트 (15분) — 빈도순

> 메일에 명시: *"discussion of your experience building and scaling hardware systems"* [확인됨]. 스토리는 [준비 문서 §7](2026-10-12_scout_technical_screen_prep.html)의 A·B·C, 숫자 칸 채우기는 W6.

| 순위 | 질문 (영어) | 쓸 스토리 |
|---|---|---|
| 1 | Tell me about yourself. | [컨텍스트 4.6](scout_ai_embedded_context.html) 60초판 |
| 2 | Tell me about a hardware system you brought up from scratch. | B (SK hynix FPGA → 제품) |
| 3 | How did you take something from one board to production scale? What broke? | A (Apple factory test-node) |
| 4 | How did you make testing or debugging scale? | C (UART 자동화 SDK · NVMe telemetry) |
| 5 | What's the hardest hardware–software integration bug you've debugged? | Apple I2C/SPMI 루트코즈 (Q7과 같은 사례) |
| 6 | What's your experience with embedded Linux? | [4.9 ④](scout_ai_embedded_context.html) + Q15 |
| 7 | You'd be the first embedded engineer in Sunnyvale. How would you start? | "First 30 days: learn the vehicle sensor stack from the Georgia team, set up a bring-up and test bench, then own one sensor path end to end." |

---

## 3. 3일 플랜 — 금 10-23 → 월 10-26 10:00

> 규칙: **문제 1개 = 타이머 30분 + 빈 에디터(자동완성 없이) + 10분 복기(소리 내어)**. 정답은 30분이 끝난 뒤에만 연다. 실행은 `technicalScreenPrep/run.sh <ID>`.

| 날 | 시간 | 할 일 | 끝났다는 기준 |
|---|---|---|---|
| **금 10-23 (D-3) 저녁** | 2.5~3시간 | ① §1.2 셀프 체크 ② **T1-1** · **T1-2** 타이머 ③ 스토리 A·B·C 숫자 채우기 ④ §2.2 Q1–Q6 소리 내어 | T1-1·T1-2 ALL PASS(시간 넘겨도 끝까지), 스토리 숫자 칸 0개 |
| **토 10-24 (D-2)** — 약점의 날 | 5~6시간 | ① **E2 차갑게 30분** → 복기 → 저녁에 **E2 다시 백지에서** ② **T2-1** 12분 목표 ③ **E3** ④ **T1-5** + T2-6 일부 ⑤ **Debug D1–D8**(드릴당 1분 말하기) ⑥ §2.2 Q7–Q12 | E2 두 번째 시도 25분 안에 ALL PASS, 링버퍼 12분 |
| **일 10-25 (D-1)** — 실전의 날 | 4~5시간, **21시 종료** | ① **E1** ② **T1-3** 또는 T1-4 ③ **45분 모의 2회**(15분 경험 영어 + 30분 아래 미공개 변형, 녹음) ④ §2.2 Q13–Q16 + R1 Q38·Q41·Q49 훑기 ⑤ §4 규칙 종이에 | 모의 2회 모두 코드가 돌아가는 상태로 끝남. 녹음에서 30초 이상 침묵 없음 |
| **월 10-26 (D-0)** | 07:30~09:40 | ① T1-2 백지 20분 ② 템플릿 1분 타이핑 ③ 질문 6개(준비 문서 §2.3) 읽기 ④ 스토리 B 한 번 소리 내어 ⑤ 09:40 CoderPad·마이크·전원 확인 | 새 문제는 풀지 않는다 |

**시간이 모자라면 버리는 순서**: T2-6 → T1-3/T1-4 → E1 → 모의 2회째. **E2 두 번, T1-1·T1-2, 모의 1회는 절대 버리지 않는다.**

### 3.1 모의용 미공개 변형 (일요일에 처음 본다 — 미리 읽지 말 것)

<details>
<summary>모의 1 — 펼치기 (일요일 모의 시작할 때)</summary>

> You receive temperature readings from a motor controller as `(uint32_t t_ms, int16_t temp_c10)`. The motor must be derated when the temperature has been at or above `warn` for at least 2 seconds, and shut down immediately at or above `crit`. It may return to normal only after the temperature stays at or below `warn - hyst` for 5 seconds. Implement a streaming `update()` that returns the current state (NORMAL, DERATED, SHUTDOWN) and test it. Timestamps are a free-running millisecond counter that can wrap.

- 연결: T1-2(히스테리시스) + 시간 기반 확인 + wrap-safe 비교 + 3단계 상태

</details>

<details>
<summary>모의 2 — 펼치기 (두 번째 모의 시작할 때)</summary>

> A UART delivers frames from a thermal sensor board: `0x7E`, `count` (1–8), then `count` records of `{uint8_t sensor_id; int16_t temp_c10 (little-endian)}`, then an 8-bit checksum = sum of all bytes after `0x7E`, modulo 256. Write a function that takes a byte buffer, extracts every complete valid frame, reports for each frame the hottest sensor, and tells the caller how many bytes it consumed so a partial frame at the end is kept for the next call.

- 연결: E2(버퍼 기반 추출 · consumed) + T1-4(Item) + 체크섬

</details>

---

## 4. 30분 운영 — 지난번 실패를 막는 규칙

### 4.1 첫 5분 규칙 (W1 대응 — Neros 교훈)
1. **다시 말하기**: "So the input is …, and I return …. Is that right?"
2. **예시 하나를 손으로**: 입력 3~5개와 기대 출력을 화면에 주석으로 적고 면접관에게 확인받는다. *"Let me write one small example to make sure I understand."*
3. **반환 경우를 먼저 나열**: 정상 / 데이터 부족 / 에러 / 버퍼 꽉 참. 이걸 코드 전에 적으면 패킷 문제에서 막히지 않는다
4. **시그니처 + 가정 주석**을 쓴다. 그다음에 코드

### 4.2 시간 체크포인트
- **8분**: 아직 코드를 안 쓰고 있으면, 가장 단순한 버전을 고르고 그렇게 말한다. *"I'll start with the simplest version that works, then extend it."*
- **20분**: **돌아가는 버전**이 있어야 한다(엣지 케이스가 빠져도). 그다음 테스트와 엣지
- **27분**: 손 멈추고 복잡도·메모리·임베디드 개선점을 말한다

### 4.3 예측이 틀렸을 때 쓰는 범용 뼈대 (W5 대응)
```c
typedef struct { /* state that survives between calls */ } Ctx;

void ctx_init(Ctx *c /*, config */);
int  ctx_update(Ctx *c, /* one input item */ /*, output */);   /* streaming: O(1) per item */

/* batch version on top of streaming: */
size_t run_all(const Item *in, size_t n, Out *out, size_t out_cap);   /* count all, write what fits */
```
- 거의 모든 센서·패킷·디바운스 문제가 이 모양이다: **상태 구조체 + 한 개씩 먹는 update + 호출자 버퍼**
- 언어는 끝까지 C. 막혀도 바꾸지 않는다 (W4)

### 4.4 말하기 (메일 Tips)
- 준비 문서 [§2.6](2026-10-12_scout_technical_screen_prep.html)의 문장들. 특히 막혔을 때 *"Let me trace the example by hand"* — 침묵 대신 손으로 따라가기

---

## 5. 당일 체크리스트 (10-26 월)

- [ ] 09:40 CoderPad 링크 열기, 언어 C 선택, 마이크·이어폰·충전기
- [ ] 종이 한 장: 첫 5분 규칙 4줄 · 체크포인트 8/20/27분 · 질문 6개 · 스토리 A·B·C 첫 문장
- [ ] 경험 15분: 각 답 90초 이내, 숫자 하나씩
- [ ] 코딩: 다시 말하기 → 예시 → 반환 경우 → 시그니처 → 코드 → 테스트(정상 1 + 엣지 2~3)
- [ ] 마지막 2분 역질문 1개: *"The embedded team is two engineers in Georgia — how would the Sunnyvale role work with them day to day?"*
- [ ] 끝나면 30분 안에 실제 문제·follow-up·막힌 곳을 [컨텍스트 §6](scout_ai_embedded_context.html)에 기록

---

## 6. 재사용 링크

- [준비 문서 (10-12)](2026-10-12_scout_technical_screen_prep.html) — 엣지 케이스 체크리스트, 질문 6개, CoderPad 템플릿, 치트시트, 경험 스토리
- [연습 사이트](technicalScreenPrep/index.html) — 21문제 + Debug, 영어 문제 · 한국어 풀이 · 차트 · 30분 타이머
- [Embedded Linux R1 Top 50](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/R/2026-10-18_R1_concept_top50.html) · [R4 디버깅 시나리오](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/R/2026-10-18_R4_debugging_scenarios.html) · [R6 스토리 브리지](../../dons_study_notes_from_resource/ARM-RISC-embeddedLinux-ETC-study-note/EmbeddedLinux/site/R/2026-10-18_R6_don_story_bridge.html)
- [Neros 스크린 복기 L09](../neros_tech_senior_firmware_engineer/technicalScreenPrep/site/lecture/2026-10-09_L09_screen_recap.html) — 링버퍼 + `extract_packet` 실제 기출
