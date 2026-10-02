# ⚡ N07 · 임베디드 퀴즈 뱅크 — Michael 퀴즈 복기부터 RTOS·신호까지

> 2026-10-01 Michael Honor(FW Test 리드)는 UART/I2C/SPI 차이, "SW에서 PCIe를 어떻게 쓰나", 8b/10b, Git/PR/rebase를 **퀴즈처럼 연달아** 물었다. 온사이트 1:1도 비슷하되 더 깊을 것이다 [추정]. 질문마다 **한 줄 정의 → 왜 → 예시/숫자** 순서의 30초 영어 답을 달았다. A절은 실제로 받은 질문의 개선 답안, B~E절은 버스·MCU·RTOS·신호, F절은 그가 물은 테스트 철학 두 문항을 다듬은 버전이다.

## 0. 퀴즈형 질문에 답하는 틀

- **1문장 정의** → **왜 쓰나 / 무엇을 해결하나** → **숫자나 예시 하나** → 멈춘다. 30초
- 면접관이 "SW 관점에서"라고 하면 **레지스터·드라이버·데이터 흐름**으로, "HW 관점에서"라고 하면 **핀·신호·타이밍**으로 답한다. 질문의 관점을 먼저 확인해도 된다: "From the driver's point of view, or the physical layer?"
- 숫자는 확실한 것만. 애매하면 "on the order of…"
- Don의 경험 연결: NVMe·PCIe(SK hynix/Solidigm), I2C/SPI SoC verification(FPGA), Apple I2C/SPMI/RFFE 루트코즈. 한 문장씩 붙이면 퀴즈가 대화로 바뀐다

---

## A. Michael 퀴즈 복기 (2026-10-01 실제 질문)

### A1. What are the differences between UART, I2C, and SPI?

| 항목 | UART | I2C | SPI |
|---|---|---|---|
| 선 | TX, RX (+GND) | SDA, SCL (open-drain + pull-up) | SCLK, MOSI, MISO, CS (슬레이브마다 CS) |
| 클럭 | 없음 (비동기, 양쪽 baud 합의) | 마스터가 SCL 생성 (동기) | 마스터가 SCLK 생성 (동기) |
| 듀플렉스 | 전이중 | 반이중 | 전이중 |
| 주소 지정 | 없음 (점대점) | 7비트(또는 10비트) 주소 | CS 핀으로 선택 |
| 속도 | 보통 115200 baud ~ 수 Mbaud (CRSF 420000) | 100 kHz / 400 kHz / 1 MHz (Fm+) / 3.4 MHz (HS) | 수 MHz ~ 수십 MHz |
| 확인 응답 | 없음 (parity 선택) | 바이트마다 ACK/NACK | 없음 |
| 멀티 마스터 | 해당 없음 | 지원 (arbitration) | 사실상 단일 마스터 |
| 거리 | 보드 간 케이블 (RS-232/485로 확장) | 보드 내 짧은 거리 (버스 용량 제한) | 보드 내 짧은 거리 |
| 드론 예시 | GPS, ELRS 수신기(CRSF), ESC 텔레메트리 | 기압계, 자력계 | IMU(gyro/accel), 플래시, OSD |

> "UART is asynchronous point-to-point — no clock, both sides agree on a baud rate, framed with start and stop bits — great for links like GPS or a radio receiver. I2C is a two-wire open-drain bus with addressing and per-byte ACK, so many slow devices share two pins — barometers, magnetometers — but it's slow, typically 100 or 400 kHz. SPI is a four-wire full-duplex bus with a chip select per device and no addressing overhead, so it runs at tens of MHz — that's why IMUs on flight controllers use SPI."

- 개선 포인트: 표의 "왜 그걸 고르나"를 드론 예시와 묶어서 말하면 단순 암기 답이 아니라 설계 감각으로 들린다

### A2. How does software use PCIe? (SW 관점)

- **Don이 10-01에 한 답**: PCIe가 왜 필요한지 → 적은 핀으로 고속 통신, TX/RX 차동쌍, REFCLK, CLKREQ#, SerDes. **이건 물리 계층(HW) 답이다.** 틀린 건 아니지만 질문이 "SW에서는"이었다면 아래 흐름이 기대 답이었을 가능성이 크다 [추정]
- **더 나은 답의 뼈대 (7단계)**:

1. **Enumeration**: 부팅 시 펌웨어(BIOS/UEFI)나 OS가 bus/device/function(256/32/8)을 깊이 우선으로 훑으며 config space의 Vendor ID를 읽어 장치를 찾고 버스 번호를 매긴다
2. **Configuration space**: 장치마다 4 KB(기존 PCI 호환 256 B + 확장). ECAM으로 메모리 맵 접근. Vendor/Device ID, Class code, Command 레지스터(Memory Space Enable, Bus Master Enable), capability 리스트(MSI, MSI-X, PCIe capability, power management)
3. **BAR (Base Address Register)**: 장치가 필요로 하는 MMIO 영역 크기를 알리는 레지스터. 크기 탐지는 all-1s를 쓰고 다시 읽어 마스킹. OS가 주소를 할당해 BAR에 써 준다
4. **MMIO**: 드라이버가 BAR 영역을 가상 주소로 매핑(ioremap / pci_iomap)하고, 장치 레지스터를 load/store로 읽고 쓴다. 이 쓰기가 PCIe Memory Write TLP가 된다
5. **DMA와 descriptor ring**: 큰 데이터는 CPU가 옮기지 않는다. 드라이버가 호스트 메모리에 링(큐)을 만들고, 장치가 bus master로 호스트 메모리를 직접 읽고 쓴다. 드라이버는 **doorbell 레지스터**에 tail 인덱스를 써서 "새 일이 있다"고 알린다
6. **인터럽트 (MSI / MSI-X)**: 완료되면 장치가 특정 주소에 메모리 쓰기를 해서 인터럽트를 건다(INTx 핀 대신). MSI는 최대 32개, MSI-X는 최대 2048개 벡터 → 큐마다, CPU 코어마다 인터럽트를 나눌 수 있다
7. **Link**: 링크 학습과 상태는 하드웨어 LTSSM(Detect → Polling → Configuration → L0, 문제 시 Recovery, 절전 L0s/L1)이 처리하고, SW는 Link Status 레지스터로 속도와 lane 폭을 확인한다

- **NVMe로 구체화 (Don의 본진)**: BAR0에 컨트롤러 레지스터(CAP, CC, CSTS, AQA/ASQ/ACQ). Admin 큐와 I/O 큐는 **호스트 메모리의 Submission Queue(64 B 엔트리)와 Completion Queue(16 B 엔트리)** 쌍. 호스트가 SQ에 명령을 쓰고 **SQ Tail doorbell**을 쓰면, 컨트롤러가 DMA로 명령을 가져가 처리하고 CQ에 완료 엔트리를 쓴 뒤 MSI-X를 건다. 호스트는 **phase tag** 비트로 새 엔트리를 판별하고 **CQ Head doorbell**을 갱신한다. 큐는 최대 64K개, 큐당 최대 64K 엔트리
- **Linux 드라이버 흐름**: `pci_enable_device` → `pci_request_regions` → `pci_iomap`(또는 `ioremap`) → `pci_set_master` → `dma_set_mask_and_coherent` → `dma_alloc_coherent`(링 버퍼) → `pci_alloc_irq_vectors` + `request_irq` → 동작 → 역순으로 해제

> "From software's point of view, PCIe looks like memory. At boot the OS enumerates the bus — bus, device, function — reads each device's configuration space, sizes its BARs and assigns them addresses. The driver maps those BARs and talks to the device through memory-mapped registers. Bulk data moves by DMA: the driver builds descriptor rings in host memory and rings a doorbell register, the device fetches the work as a bus master, and signals completion with an MSI-X interrupt, which is itself just a memory write. NVMe is the cleanest example — submission and completion queue pairs in host memory, doorbells in BAR0, one MSI-X vector per queue — and that's the protocol I worked with for years in SSD firmware, from the device side."

- 마지막 문장이 핵심: Don은 **장치(컨트롤러) 쪽**에서 이 흐름을 구현했다. "From the device side, the firmware sees the same thing in reverse: doorbell writes arrive as register writes, and we fetch commands by DMA." (Don: 실제로 맡았던 부분 — 예: error reporting, telemetry log page, NVMe 2.0 기능 — 과 연결)

### A3. What is 8b/10b encoding, and why is it used?

- **한 줄**: 8비트 데이터를 10비트 심볼로 바꿔 보내는 선로 부호화. 오버헤드 25%(8비트당 2비트 추가, 선로 효율 80%)
- **왜 1 — DC balance**: 심볼마다 1과 0의 차이(disparity)가 0 또는 ±2이고, **running disparity**로 +와 −를 번갈아 골라 장기적으로 1과 0의 개수를 맞춘다 → AC 커플링(직렬 커패시터) 가능
- **왜 2 — 클럭 복원**: 같은 비트가 **최대 5개까지만** 연속 → 전이가 충분해서 수신 측 CDR(clock data recovery)이 별도 클럭선 없이 클럭을 뽑는다
- **왜 3 — K 문자(제어 심볼)**: 데이터로는 나올 수 없는 특수 심볼. **K28.5**는 comma 패턴을 포함해 심볼 경계 정렬(alignment)에 쓴다
- **왜 4 — 오류 검출 보조**: 유효하지 않은 10비트 코드나 disparity 위반이 나오면 바로 오류로 잡힌다
- **어디에**: PCIe Gen1(2.5 GT/s)·Gen2(5 GT/s), SATA, USB 3.x Gen1(5 Gbps), 1000BASE-X Ethernet, DisplayPort 1.x
- **이후 세대**: PCIe Gen3(8 GT/s)부터 Gen5까지는 **128b/130b**(2비트 sync header, 오버헤드 약 1.5%)와 **scrambling**으로 DC balance와 전이 밀도를 확률적으로 확보한다. USB 3.x Gen2는 128b/132b. PCIe 6.0(64 GT/s)은 **PAM4**(UI당 2비트)와 **FLIT 모드**를 쓴다. FLIT는 256 B 고정 크기(242 B 데이터 + 8 B CRC + 6 B FEC)이고, 블록 인코딩 오버헤드가 없는 **1b/1b**다. FEC는 PAM4에서 늘어난 오류를 고치고, CRC는 남은 오류를 검출해 재전송을 건다 (출처: [Synopsys](https://www.synopsys.com/articles/pcie-6-designs.html), [NextPCB](https://www.nextpcb.com/blog/pcie-gen6-pcb-design-challenges-pam4-fec-layout))

> "8b/10b maps each byte to a 10-bit symbol so the serial line stays DC-balanced and has enough transitions for clock recovery — never more than five identical bits in a row, and running disparity keeps ones and zeros balanced. It also gives special K-characters like K28.5, the comma, which the receiver uses to find symbol boundaries. The cost is 25% overhead, which is why PCIe moved to 128b/130b with scrambling at Gen3, and Gen6 went to PAM4 with FLIT mode and forward error correction."

- Don 연결: "On PCIe Gen5/6 SSD controllers I worked in exactly that transition." (Don: 실제로 다룬 범위만 — 링크 디버그나 shmoo를 했다면 그 맥락으로)

### A4. Git — how do you use it day to day? Pull requests? Rebase?

> "I work on a short-lived feature branch, commit small logical changes, and open a pull request early so CI runs and reviewers see it. Before merging I rebase onto the latest main to get a linear history and resolve conflicts on my side, squash fixup commits with an interactive rebase, and push with `--force-with-lease`, never plain force. I don't rebase branches others are building on. For regressions I use `git bisect`, and `git reflog` gets me out of almost any mistake."

| 질문 | 답 |
|---|---|
| rebase vs merge? | merge는 두 이력을 합치는 merge commit을 만든다(이력 보존). rebase는 내 커밋을 새 base 위에 다시 쓴다(선형 이력, 커밋 해시 바뀜). **공유된 브랜치는 rebase하지 않는다** |
| interactive rebase? | `git rebase -i main` → pick/squash/fixup/reword/drop으로 커밋 정리. `git commit --fixup <sha>` + `rebase -i --autosquash` |
| conflict 해결? | `git status`로 충돌 파일 확인 → 수정 → `git add` → `git rebase --continue`(또는 merge면 commit). 막히면 `--abort` |
| force push 주의점? | `--force-with-lease`: 원격이 내가 마지막으로 본 상태일 때만 덮어씀 → 동료 커밋을 날리지 않는다 |
| cherry-pick? | 특정 커밋 하나를 다른 브랜치에 적용. 릴리즈 브랜치에 hotfix 백포트할 때 |
| bisect? | `git bisect start <bad> <good>` → 이진 탐색. `git bisect run ./test.sh`로 자동화. 50커밋이면 약 6번 |
| 실수로 날렸다면? | `git reflog`로 이전 HEAD를 찾아 `git reset --hard <sha>` 또는 브랜치 생성 |
| 좋은 PR? | 작게, 한 가지 목적, 설명에 "왜"와 테스트 방법, CI 통과, 리뷰 코멘트에 답하고 해결 |
| `fetch` vs `pull`? | fetch는 가져오기만, pull은 fetch + merge(또는 `--rebase`) |
| tag와 FW 버전? | 릴리즈 커밋에 annotated tag, 빌드 시 `git describe`로 버전 문자열과 커밋 해시를 바이너리에 삽입 → 어떤 FW가 어떤 기체에 실렸는지 추적 |

---

## B. 버스와 통신

### B1. What is SPI mode (CPOL/CPHA)?

> "CPOL is the clock's idle level, CPHA is whether data is sampled on the first or second clock edge — four combinations, modes 0 to 3. Master and slave must match; a mismatch typically shows up as data shifted by one bit. Most IMUs support mode 0 and mode 3 [추정: 데이터시트 확인]."

### B2. Why does I2C need pull-up resistors?

> "I2C lines are open-drain: devices can only pull the line low, and the pull-up brings it high. That's what allows wired-AND arbitration, clock stretching, and multiple devices without contention. The resistor value is a trade-off — too large and the rise time is too slow for 400 kHz with bus capacitance, too small and devices can't sink the current."

### B3. What is I2C clock stretching?

> "A slave holds SCL low to make the master wait until it's ready. The master must check that SCL actually went high before continuing. Some controllers handle it poorly, which is a classic source of interoperability bugs."

### B4. How do you recover a stuck I2C bus?

> "If a slave is holding SDA low — usually because the master reset mid-transaction — the master switches SCL to GPIO and clocks it up to nine times until the slave releases SDA, then generates a STOP condition and re-initializes the peripheral."

### B5. How does I2C arbitration work?

> "Because the bus is wired-AND, a master that sends a 1 but reads back a 0 knows another master is driving, and backs off. The master with the lower address bits wins without corrupting the transfer."

### B6. What's UART baud tolerance, and what happens with a mismatch?

> "The receiver samples each bit around its middle, timed from the start bit, so the error accumulates across the frame. Over ten bits, the combined clock error of both sides needs to stay within a few percent — the usual rule of thumb is about two percent per side. Beyond that you get framing errors and garbage bytes."

### B7. UART framing — what's in a frame?

> "Idle high, a start bit low, five to nine data bits LSB-first, optional parity, then one or two stop bits high. 8N1 is the common one. Some protocols invert the signal — SBUS is inverted UART at 100000 baud with 8E2, while CRSF is non-inverted 420000 baud 8N1."

### B8. What is RS-485, and when would you use it?

> "A differential, half-duplex, multi-drop physical layer for UART-style data — up to 32 standard unit loads on one pair and long cable runs, on the order of a kilometer at low data rates. The differential signaling rejects common-mode noise, so it's used for long or noisy runs like motor controllers or ground equipment. You need termination at both ends and direction control."

### B9. CAN basics

> "CAN is a differential two-wire multi-master bus. Messages have identifiers instead of addresses, and arbitration is bitwise and non-destructive — a dominant zero wins, so lower IDs have higher priority. It has CRC, an ACK slot, automatic retransmission and error confinement, which is why it's used in vehicles. Classic CAN goes up to 1 Mbps with 8-byte payloads; CAN FD allows up to 64 bytes and a faster data phase. Both ends need 120-ohm termination."

### B10. USB basics

> "USB is host-centric: the host enumerates the device, reads its descriptors, and assigns an address. Communication goes to endpoints using four transfer types — control, bulk, interrupt, and isochronous. A flight controller usually shows up as a CDC-ACM virtual COM port for its CLI, and many MCUs also support USB DFU for firmware updates."

### B11. TCP vs UDP — which for telemetry or video?

> "UDP is connectionless and doesn't retransmit, so latency stays bounded and a lost packet is simply dropped — that's what you want for real-time telemetry and video, where old data is useless. TCP gives reliable ordered delivery but retransmissions add latency spikes. Commands that must arrive can use acknowledgments at the application layer. MAVLink, for example, is commonly carried over UDP."

### B12. What's the difference between a differential and a single-ended signal?

> "Single-ended measures one wire against ground; differential sends the signal as the difference between two wires, so noise that couples equally onto both cancels out. That's why high-speed and long-distance links — PCIe, USB, Ethernet, CAN, RS-485 — are differential."

### B13. What is SerDes?

> "A serializer/deserializer turns a parallel bus into a high-speed serial stream and back. The clock is embedded in the data and recovered by a CDR at the receiver, which is why line coding or scrambling is needed to keep enough transitions. It's the physical layer under PCIe, USB 3, SATA, and Ethernet."

### B14. What is ECAM? [깊은 꼬리 질문 대비]

> "The Enhanced Configuration Access Mechanism maps every PCIe function's 4 KB configuration space into a memory region, so software reads config space with ordinary memory accesses, indexed by bus, device, function, and register offset."

---

## C. MCU 코어

### C1. How do interrupt priorities work on a Cortex-M (NVIC)?

> "Each interrupt has a priority number where a lower number means higher urgency. Priority bits can be split into preemption priority — which decides whether one ISR can interrupt another — and subpriority, which only orders pending interrupts. STM32 parts implement four priority bits. The hardware stacks registers automatically, and features like tail-chaining and late arrival reduce overhead between back-to-back interrupts."

### C2. What is interrupt latency and what affects it?

> "It's the time from the interrupt request to the first instruction of the ISR. On a Cortex-M3 or M4 the hardware part is about 12 cycles with zero wait-state memory. In practice it's dominated by software: higher-priority ISRs running, sections where interrupts are disabled, flash wait states, and FPU lazy stacking. To keep latency low, keep critical sections and other ISRs short."

### C3. What does DMA do, and what are circular and double-buffer modes?

> "DMA moves data between peripherals and memory without the CPU. In circular mode the DMA wraps around a buffer forever — ideal for a UART receive ring. Half-transfer and transfer-complete interrupts let the CPU process one half while the DMA fills the other; double-buffer mode does the same with two separate buffers. The CPU only touches data the DMA has finished with."

### C4. Timers: PWM and input capture

> "A timer counts clock ticks from a prescaled source. In PWM mode the output toggles when the counter hits a compare value, so the period comes from the auto-reload register and the duty cycle from the compare register — used for servos, LEDs, and on flight controllers DShot is generated with timers plus DMA. Input capture latches the counter on an input edge to measure pulse width or frequency."

### C5. ADC sampling — what matters?

> "Resolution, sampling time, and reference. The input must settle through the source impedance during the sampling time; too short and readings are low and noisy. Sample at more than twice the signal bandwidth with an anti-alias filter, and use DMA with a timer trigger for evenly spaced samples. On a drone that's battery voltage and current sensing."

### C6. IWDG vs WWDG on STM32

> "The independent watchdog runs from its own low-speed internal oscillator, so it keeps working even if the main clock fails, and once started it can't be stopped except by reset. The window watchdog runs from the APB clock and resets if you refresh it too late or too early — catching code that runs away in a tight loop. In production, refresh the watchdog from a place that proves the whole system is healthy, not from a timer interrupt."

### C7. Clock tree and PLL

> "The MCU starts from an internal RC oscillator or an external crystal, and a PLL multiplies it up to the core frequency, with prescalers for each bus and peripheral. Changing clocks means waiting for the PLL to lock and adjusting flash wait states first. External crystals give accurate UART baud rates and USB timing; the internal RC drifts with temperature."

### C8. Low-power modes

> "Cortex-M has sleep (core stopped, peripherals running, wake on any interrupt), and STM32 adds stop (most clocks off, RAM retained, wake on EXTI) and standby (almost everything off, RAM lost, wake through reset). WFI enters sleep. The trade-off is wake-up time versus current."

### C9. How does a bootloader and firmware update work?

> "A small bootloader at the reset vector checks the application image — magic number, size, CRC or signature — then sets the vector table offset and stack pointer and jumps to it. Updates are written to a second slot or bank, verified, and only then marked bootable, so a power loss mid-update leaves the old image working. Many STM32 parts also have a ROM system bootloader selected by the BOOT0 pin that supports UART and USB DFU — which is how Betaflight boards are typically flashed."

### C10. What does a dual-bank flash buy you?

> "You can execute from one bank while erasing and programming the other, so the system keeps running during an update, and you can swap banks to boot the new image while keeping the old one for rollback. Not every STM32 has it; on single-bank parts you stall during erase or run the updater from RAM."

### C11. Why can DMA and cache conflict on a Cortex-M7?

> "The M7 has a data cache, but DMA reads and writes RAM directly. Before a DMA transmit, clean the cache lines so memory has the CPU's latest data; after a DMA receive, invalidate them so the CPU doesn't read stale cached data. Buffers should be aligned to 32-byte cache lines. Alternatively, put DMA buffers in a non-cacheable region using the MPU or in tightly coupled memory."

### C12. What is the MPU used for?

> "The memory protection unit sets access permissions and memory attributes per region: make code read-only, mark the stack's bottom as a no-access guard so overflow faults immediately, mark DMA buffers non-cacheable, and isolate tasks in an RTOS."

### C13. What is memory-mapped I/O?

> "Peripheral registers appear at fixed addresses, so the CPU controls hardware with normal loads and stores through volatile pointers. Writes can have side effects — clearing a status flag by reading it, for example — which is why the compiler must not reorder or remove them."

### C14. What happens at reset on a Cortex-M?

> "The core reads the initial stack pointer from address 0 of the vector table and the reset handler address from the next word. The startup code copies .data from flash to RAM, zeroes .bss, sets up clocks, runs C++ constructors if any, and calls main."

### C15. What is a HardFault and how do you debug it?

> "It's the catch-all fault for bus errors, invalid instructions, unaligned access where it's not allowed, or escalated faults. In the handler I read the stacked registers — especially the PC and LR — and the fault status registers like CFSR and HFSR, save them to no-init RAM, and log them on the next boot. Then the map file tells me which function the PC was in."

### C16. Bit-banding / atomic bit set on Cortex-M?

> "Cortex-M3 and M4 have bit-band regions that map each bit of SRAM and peripherals to a word address, so writing that word sets or clears one bit atomically. It's not available on M0 or M7, so portable code uses critical sections or the peripheral's dedicated set/reset registers like STM32's GPIO BSRR."

---

## D. RTOS

### D1. How does a preemptive RTOS scheduler work?

> "The highest-priority ready task always runs. When a higher-priority task becomes ready — because of an interrupt, a timeout, or a message — the kernel switches to it immediately. Tasks at the same priority can round-robin on each tick. On Cortex-M the context switch is done in the PendSV exception at the lowest priority."

### D2. What is priority inversion, and how is it solved?

> "A high-priority task waits on a mutex held by a low-priority task, while a medium-priority task preempts the low one — so the high task is effectively blocked by the medium task. Priority inheritance temporarily raises the holder to the waiter's priority. The Mars Pathfinder resets in 1997 were caused by exactly this. In FreeRTOS, mutexes have priority inheritance but binary semaphores don't."

### D3. Mutex vs semaphore vs queue

> "A mutex protects a shared resource — it has an owner, only the owner releases it, and it supports priority inheritance. A semaphore signals events or counts resources — any context can give it, including an ISR. A queue passes data by copy between tasks or from an ISR, which is often the cleanest design because it avoids shared state."

### D4. What does "FromISR" mean in FreeRTOS?

> "ISRs can't block, so FreeRTOS provides ISR-safe variants like xQueueSendFromISR that never wait. They report whether a higher-priority task was woken, and you pass that to portYIELD_FROM_ISR so the switch happens right as the ISR exits. You also must not call FreeRTOS APIs from interrupts above the configured max syscall priority."

### D5. How do you size task stacks?

> "Start from static analysis of the call depth and locals, run the system under worst-case load, then check the high-water mark — uxTaskGetStackHighWaterMark in FreeRTOS — and leave margin. Enable stack overflow checking during development."

### D6. What causes deadlock and how do you avoid it?

> "Two tasks each hold a lock the other needs. Avoid it with a global lock ordering, holding locks briefly, never blocking while holding a lock, and using timeouts so a deadlock becomes a detectable error instead of a hang."

### D7. What is tickless idle?

> "Instead of a periodic tick interrupt waking the CPU, the kernel programs a timer for the next scheduled wake-up and sleeps until then — saving power when the system is idle."

### D8. Bare-metal super loop vs RTOS — how do you choose?

> "A super loop with interrupts is simple, deterministic, and easy to reason about, and many flight controllers run a cooperative scheduler — Betaflight's task scheduler is one example. An RTOS earns its place when you have many independent activities with different deadlines and blocking I/O. The cost is RAM per task stack, more complex debugging, and synchronization hazards."

### D9. What is a cooperative scheduler, and what's its risk?

> "Tasks run to completion and give up the CPU voluntarily, so there's no preemption and little need for locks. The risk is that one slow task delays everything, so every task must have a bounded execution time — which is why such schedulers track task execution time and late counts."

---

## E. 신호와 HW 디버깅

### E1. Oscilloscope vs logic analyzer vs protocol analyzer

> "A scope shows the analog waveform — voltage over time — so you see ringing, rise time, noise, and levels. A logic analyzer samples many digital lines as ones and zeros and decodes buses, so you see timing between signals and the transactions. A protocol analyzer is dedicated to one protocol like PCIe or USB, captures at full speed, and decodes packets with triggers on protocol events. I start with the logic analyzer for 'what happened,' and use the scope for 'why is the signal bad.'"

- Don 연결: Apple에서 DSO와 protocol analyzer로 신호·버스·시스템 레벨 디버그 (레쥬메). (Don: 실제 예 하나)

### E2. What causes ringing, and why does it matter?

> "Fast edges on a trace with inductance and capacitance, or an impedance mismatch that reflects the signal. Ringing can cross the logic threshold and create false edges — double-clocking an SPI or I2C bus. Fixes are series termination resistors, slower edges, shorter traces, and a solid ground return."

### E3. What is rise time, and why does it matter for I2C?

> "Rise time is how long the signal takes to go from 10% to 90% of its swing. On I2C it's set by the pull-up resistor and bus capacitance, and the spec caps it — 1000 ns for 100 kHz and 300 ns for 400 kHz. A slow rise reduces timing margin and noise immunity."

### E4. What is ground bounce?

> "When many outputs switch at once, current through the package and ground inductance makes the chip's internal ground move relative to the board ground, so a quiet output or input sees a glitch. It's mitigated with good decoupling, multiple ground pins, and limiting simultaneous switching."

### E5. Why do we put decoupling capacitors next to ICs?

> "They supply the fast current spikes when the chip switches, so the supply voltage at the pin doesn't droop. Small capacitors placed close to the pins handle high frequencies, and bulk capacitors handle slower load changes. On a drone, motor current transients make this even more important."

### E6. What is an eye diagram?

> "You overlay many bit periods of a high-speed signal on top of each other. A wide, tall open eye means good timing and voltage margin; jitter closes it horizontally and noise or attenuation close it vertically. A compliance mask defines the minimum opening."

### E7. What is BER, and what's typical?

> "Bit error rate — errors per bit transmitted. Links like PCIe Gen1 through Gen5 are specified around 10 to the minus 12. To prove a BER that low you need to send enough bits — on the order of 3 times 10 to the 12 bits with zero errors for 95% confidence — which is why margining and shmoo testing matter more than a short pass/fail run."

### E8. What is jitter?

> "The deviation of signal edges from their ideal timing. Random jitter comes from noise and is unbounded; deterministic jitter comes from things like crosstalk, inter-symbol interference, and duty-cycle distortion. It eats into the eye's horizontal opening."

### E9. What's a shmoo plot? (Don의 브리지)

> "You sweep two parameters — say voltage and timing or frequency — run a test at each point, and plot pass or fail. The shape shows the operating window and how much margin the nominal point has. I built shmoo support in SSD firmware with the signal-integrity team, and I'd use the same idea on a drone: sweep UART baud error, SPI clock, supply voltage, or RF attenuation and find the edge."

### E10. How would you debug a signal that's fine on the bench but fails in the drone?

> "Recreate the field condition: motors spinning, real battery, real cable lengths. Then scope the signal and the supply rail at the failing moment — look for glitches synchronized with motor PWM, ground offset between boards, and rise-time margin. Change one variable at a time: slow the bus, add filtering, change grounding, and see which one moves the failure rate."

---

## F. 테스트 철학 (Michael이 물은 두 문항)

### F1. "How would you create test cases for something that isn't defined?"

- **Don의 10-01 답**: 격리된 unit test부터 시작해서 그 위로 쌓아 올린다
- **더 강한 버전**: unit부터 쌓는다는 뼈대는 유지하되, ① **정의가 없다는 것 자체를 첫 번째 결과물로 삼고** ② **관찰로 현재 동작을 기록하는 테스트(characterization test)**를 먼저 깔고 ③ **위험 순서**로 넓힌다

> "When behavior isn't defined, my first job is to make it defined. I start from what I can isolate — unit tests on the pieces with clear contracts — and I write characterization tests that record what the system does today, so any change is visible. Then I go to the people who own the requirement and turn the open questions into explicit decisions — what should happen on link loss, what's the timeout. And I prioritize by risk: safety behavior like arming and failsafe first, then the things that would ground a fleet, then everything else. The test suite ends up documenting the spec that didn't exist."

### F2. "What does 'done' mean to you?"

- **Don의 10-01 답**: 약 80%에서 일단 내보내고, 고객이나 다른 팀의 피드백을 받으며 보완 → **HM 반응이 좋았다**. 이 방향을 유지한다
- **더 강한 버전**: 80% 출하의 **조건**(안전하지 않은 20%는 아님)을 붙이면 무모하지 않고 성숙하게 들린다

> "Done means it's in the users' hands and we're learning from it — not that it's perfect. I'd rather ship the 80% that delivers value, with tests covering what it does and clear notes on what it doesn't, then iterate on real feedback from the field or the teams using it. The part I don't compromise on is the safety-critical 20%: arming, failsafe, and anything that can brick a unit has to be fully tested before it ships. So done is: it works for the intended use, it's tested, it's observable, and the known gaps are written down."

### F3. 꼬리 질문 대비 — "How do you decide what's in the 80%?"

> "Risk times frequency. The paths every flight exercises and the ones with severe consequences get covered first. Rare, low-impact edge cases can wait, as long as they're documented and the system fails safely when it hits them."

### F4. 꼬리 질문 — "A developer says their feature is done but has no tests. What do you do?"

> "I'd pair with them rather than block them: agree on the two or three tests that prove the main behavior and the failure path, and get those in before merge. Then make that the default in the PR template, so 'done' includes tests without anyone being the police."

### F5. 꼬리 질문 — "How do you test something you can't reproduce on the bench?"

> "Instrument it in the field first — logs, counters, reset reasons — so a failure leaves evidence. Then use that evidence to build a bench condition that reproduces it, with fault injection if needed. Once it reproduces, it becomes a regression test."

---

## G. 30초 안에 대답해야 하는 단답 모음

| 질문 | 답 |
|---|---|
| Little vs big endian? | LE는 최하위 바이트가 낮은 주소. Cortex-M, x86은 LE. 네트워크 순서는 BE |
| Harvard vs von Neumann? | 명령과 데이터 버스가 분리(Harvard) → 동시 접근. Cortex-M은 내부적으로 Harvard 계열 버스 |
| What's a watchdog for? | SW가 멈추거나 폭주하면 리셋해서 복구 |
| What's debouncing? | 기계식 스위치의 떨림을 시간 필터(수 ms)나 상태 확인으로 제거 |
| What is a race condition? | 결과가 실행 타이밍에 따라 달라지는 버그. 공유 상태 + 보호 없음 |
| JTAG vs SWD? | 둘 다 디버그 포트. SWD는 2선(SWDIO, SWCLK)으로 Arm 전용, JTAG는 4~5선 범용 |
| What is CRC good for? | 전송·저장 오류 검출(burst 오류에 강함). 보안 무결성은 아님 → 서명 |
| Fixed-point vs floating-point? | FPU 없는 코어에서 정수로 소수 표현(Q 포맷). 범위와 정밀도를 직접 관리 |
| What's a ring buffer for? | 생산자·소비자 속도 차 흡수, 동적 할당 없음, O(1) → [N05](2026-10-01_N05_ring_buffer_onsite.md) |
| What is aliasing (signal)? | 샘플링 주파수의 절반보다 높은 성분이 낮은 주파수로 접힘 → 안티앨리어싱 필터 |
| PWM duty 계산? | duty = CCR / (ARR + 1) |
| What is a brownout? | 전원이 동작 최소 전압 아래로 떨어지는 것. BOR이 리셋시켜 이상 동작 방지 |

## 체크

- [ ] A1 UART/I2C/SPI 비교를 드론 예시와 함께 45초에 말할 수 있다
- [ ] A2 PCIe를 SW 관점 7단계(enumeration → config space → BAR → MMIO → DMA ring/doorbell → MSI-X → link)로 말하고 NVMe SQ/CQ로 구체화할 수 있다
- [ ] A3 8b/10b의 목적 4가지(DC balance, 클럭 복원, K28.5 정렬, 오류 검출)와 128b/130b 전환 이유를 말할 수 있다
- [ ] A4 rebase vs merge, force-with-lease, bisect, reflog를 막힘없이 설명할 수 있다
- [ ] C11 Cortex-M7 cache + DMA의 clean/invalidate 순서를 설명할 수 있다
- [ ] D2 priority inversion을 Mars Pathfinder 예와 함께 설명할 수 있다
- [ ] E1 scope / LA / protocol analyzer 사용 순서를 내 경험과 함께 말할 수 있다
- [ ] F1, F2 테스트 철학 답을 영어로 각 40초 안에 말할 수 있다
