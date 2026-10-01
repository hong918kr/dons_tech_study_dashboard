# B04. BSP 면접 대비 — 41문항, 정직 스크립트, 화이트보드 순서

> **시리즈**: BSP 집중 4/6 · **선행**: B00(전략), B01(BSP 해부), B02(Zephyr 보드 포팅), B03(Linux·Android BSP) · **JD 근거**: "Own BSP development, peripheral driver integration (SPI, I2C, UART, I2S), and RTOS task scheduling"
> **Don 상태**: 🟡 — 아래 층(버스 bring-up, pre-silicon, 디버깅, 양산)은 ✅ 강하고, "BSP를 처음부터 소유해 본 경험"은 ❌ 없다. 이 노트는 **그 두 사실을 한 호흡에 말하는 연습**이다.
> **이 노트를 다 읽으면**: ① BSP 관련 41개 질문에 30초 안에 구조를 잡아 답할 수 있다 ② 레쥬메의 어느 줄이 어느 질문에 대응하는지 즉시 꺼낼 수 있다 ③ "BSP를 소유해 봤나"에 과장 없이, 그러나 약해 보이지 않게 답할 수 있다.

---

## 0. 이 노트 쓰는 법

### 0.1 난이도 표기

| 표기 | 뜻 | 못 하면 |
|---|---|---|
| 기초 | 3년차 임베디드면 즉답해야 하는 것 | 탈락 사유 |
| 중급 | JD가 요구하는 수준. **이 자리의 실제 판별선** | 합격선 아래 |
| 심화 | 시니어/오너 판정용. 틀려도 사고 과정으로 점수 | 상한선이 결정됨 |

### 0.2 답변 4단 구조 (모든 답에 적용)

1. **한 문장 결론** — 질문에 먼저 답한다.
2. **구조 2~4개** — 순서, 축, 층 중 하나로 나눈다.
3. **경험 1개** — 구체적 사건 하나. 없으면 없다고 말한다.
4. **경계** — 모르는 부분을 먼저 밝힌다.

4번이 이 면접의 승부처다. Don은 BSP를 소유해 본 적이 없으므로, **경계를 스스로 먼저 긋는 사람**이라는 인상을 만드는 게 가장 높은 기대값을 준다.

### 0.3 다른 노트와의 관계

J02는 JD 문장 해석 + 드라이버·RTOS 중심 14문항, J10은 Cortex/툴체인 15문항, J13은 회로도·bring-up 15문항, S02는 드라이버·RTOS 29문항이다. **이 노트는 "BSP 오너십"이라는 축 하나로 다시 묶은 것**이고, 겹치는 질문은 더 깊은 각도로 답한다. 겹치는 경우 어느 노트를 참조할지 각 문항에 적었다.

---

## 1. BSP 소유권과 정의

### Q01. [기초] What is a BSP, in your own words?

**왜 묻나**: 공고 제목이 BSP로 바뀌었다. 정의를 흐리게 말하면 그 다음 20분이 전부 흔들린다.

**30초 답변**: 보드가 바뀌어도 애플리케이션 코드가 안 바뀌게 만드는 층이다. 구체적으로는 부트 코드와 링커 스크립트·메모리 맵, 클럭 트리와 전원 초기화 순서, 핀 먹싱, 어떤 주변장치가 어디에 붙어 있는지의 기술(devicetree 또는 보드 헤더), 그리고 드라이버를 그 기술에 묶는 바인딩이다. "칩 지원"이 아니라 **"보드 지원"**이라는 게 핵심 — 같은 SoC라도 보드마다 BSP가 다르다.

> "The BSP is the layer that lets the application stay the same when the board changes. Concretely: startup and linker/memory map, clock tree and power-up ordering, pin muxing, a description of what peripheral is wired where — devicetree or board headers — and the binding that connects drivers to that description. The key word is board, not chip: the same SoC on two different boards needs two different BSPs."

**꼬리질문**: "SDK와 BSP의 차이는?" → SDK는 벤더가 칩 단위로 주는 것이고, BSP는 우리 보드 단위로 우리가 만드는 것. "HAL은 어디에 속하나?" → HAL은 칩 레벨 추상화, BSP는 그 위에서 보드 배선을 기술한다.

### Q02. [기초] If I opened your repo, which files would be the BSP?

**왜 묻나**: 추상적으로만 아는 사람과 실제로 파일을 만져 본 사람을 가른다.

**30초 답변**: Zephyr 기준이면 보드 디렉터리 하나 — 보드 식별 파일, `<board>.dts`(어떤 장치가 어느 버스·어느 핀에 있나), `<board>_defconfig`(무엇을 켤 것인가), `Kconfig.<board>`, 필요하면 보드 전용 초기화 C 파일, 그리고 리비전 정의다. bare-metal이면 startup 어셈블리, 링커 스크립트, 보드 헤더(핀·주소 정의), 클럭·전원 초기화 함수, 드라이버 인스턴스 테이블이다. 파일 이름·구성은 **Zephyr 버전에 따라 바뀌었으니**(hardware model v2) 트리를 보고 맞추겠다고 덧붙인다.

> "In Zephyr it's one board directory: the board identification file, the board .dts describing what's wired where, a defconfig, a Kconfig for the board, optionally a board init C file, and revision definitions. On bare metal it's startup, the linker script, a board header with pin and base-address definitions, clock and power-up init, and the driver instance table. I'd check the exact layout against the tree — Zephyr's board porting structure changed between versions."

**꼬리질문**: "왜 버전을 확인한다고 하나?" → hardware model v2에서 보드 이름 표기와 파일 구성이 바뀌었다. 외운 걸 단정하는 것보다 확인하는 게 맞다.

### Q03. [중급] What does it mean to you to "own" a BSP? What would your first 90 days look like?

**왜 묻나**: JD 동사가 `Own`이다(J02 0절). 단일 오너를 뽑는다는 뜻이고, 이 답이 레벨 판정에 직결된다.

**30초 답변**: 소유한다는 건 네 가지를 책임진다는 뜻이다. ① 새 보드가 오면 부팅시키는 사람이 나 ② 보드 기술(devicetree/보드 파일)의 단일 소스와 리비전 전략을 내가 정함 ③ 벤더 SDK/드라이버를 우리 코드베이스에 넣는 기준과 업그레이드 경로를 내가 정함 ④ "이게 HW 문제냐 FW 문제냐"를 판정하는 최종 책임. 90일이면 1개월 차에 기존 보드를 내 손으로 처음부터 빌드·플래시·부팅시켜 현행 구조를 몸으로 익히고, 2개월 차에 다음 리비전을 받아 bring-up을 주도하고, 3개월 차에 리비전 전략과 bring-up 체크리스트를 문서로 남겨 다음 사람이 반복할 수 있게 만든다.

> "Owning it means four things: I'm the person who boots a new board; I own the single source of truth for the board description and the revision strategy; I set the rules for pulling in vendor drivers and upgrading them; and I'm the final call on whether a failure is hardware or firmware. For the first ninety days — month one, build, flash and boot the existing board entirely by myself so I understand the current structure. Month two, take the next board revision and lead its bring-up. Month three, write down the revision strategy and a bring-up checklist so the next person repeats it instead of rediscovering it."

**Don 연결**: ④는 레쥬메 근거가 직접 있다 — "root-cause analysis of fundamental and interface level failures when a new chip meets the full HW/SW system".

### Q04. [심화] Where do you draw the line between BSP and application?

**왜 묻나**: 경계 설계 능력. 작은 팀에서는 이 선이 흐려지면 6개월 뒤 보드 리비전마다 앱 코드를 고치게 된다.

**30초 답변**: 판정 기준 하나만 쓴다 — **"보드를 바꾸면 이 파일이 바뀌어야 하나?"** 바뀌어야 하면 BSP, 아니면 앱이다. 그래서 핀 번호, I2C 주소, 클럭 소스, 전원 시퀀스 타이밍은 BSP. 샘플링 주기, 알고리즘 임계값, 상태 기계는 앱. 애매한 것은 센서 기본 설정값인데, 나는 "하드웨어가 강제하는 값"(최대 클럭, 필요한 대기 시간)은 BSP, "제품이 고른 값"(ODR, 게인)은 앱 설정으로 보낸다. 앱이 `#ifdef BOARD_REV_B`를 쓰기 시작하면 경계가 무너졌다는 신호다.

> "One test: if changing the board forces you to change this file, it's BSP. Pin numbers, I2C addresses, clock sources, power-up timing — BSP. Sampling policy, thresholds, state machines — application. The gray area is sensor defaults; I put hardware-mandated values in the BSP and product-chosen values in application config. The moment application code starts carrying `#ifdef BOARD_REV_B`, the boundary has already broken."

---

## 2. 부트와 bring-up 순서

### Q05. [기초] Walk me through what happens from power-on to main() on a Cortex-M.

**왜 묻나**: BSP의 가장 아래층. 여기가 막히면 나머지는 볼 필요가 없다. (J10 Q05, S02 Q01과 동일 주제 — 답을 통일해 둘 것)

**30초 답변**: 전원·클럭이 안정되고 리셋이 풀리면 코어가 벡터 테이블의 0번에서 MSP 초기값을, 1번에서 리셋 핸들러 주소를 읽어 점프한다. 리셋 핸들러가 최소 클럭을 세우고, `.data`를 flash에서 RAM으로 복사하고, `.bss`를 0으로 밀고, C++이면 정적 생성자를 돌린 뒤 `main()`을 부른다. BSP 관점에서 여기에 얹히는 게 클럭 트리 설정, 핀 먹싱, 전원 레일 enable 순서, 그리고 드라이버 초기화 테이블이다.

> "Out of reset the core loads the initial MSP from vector table entry zero and the reset handler address from entry one. The reset handler brings up a minimal clock, copies .data from flash to RAM, zeroes .bss, runs static constructors if it's C++, and calls main. The BSP work sits on top of that: clock tree setup, pin muxing, the order you enable power rails, and the driver init table."

**꼬리질문**: "`.data` 복사 전에 전역 변수를 쓰면?" → 쓰레기 값. startup 코드에서 전역을 건드리면 안 된다. "벡터 테이블을 옮기려면?" → `VTOR`에 새 주소를 쓴다. 부트로더가 앱으로 점프할 때 필수(J10 Q14).

### Q06. [기초] A brand-new board is on your desk and it does not boot. What do you check first?

**왜 묻나**: 순서가 있는 사람인지를 본다. 여기서 "일단 코드를 봅니다"라고 하면 감점.

**30초 답변**: 코드보다 전기가 먼저다. ① 전원 레일을 DMM/스코프로 순서대로 확인 — 데이터시트 시퀀스대로 올라오나, 리플은 없나 ② 리셋 핀이 실제로 해제됐나 ③ 클럭 소스가 발진하나(크리스털은 스코프로, 프로브 부하 주의) ④ strap/부트 모드 핀 레벨 ⑤ 그다음에야 디버거 attach. 이 순서는 뒤로 갈수록 비용이 커서, 싼 것부터 본다.

> "Electricity before code. Rails in sequence on the scope — do they come up in the order the datasheet requires, and are they clean. Then reset actually released. Then the clock source actually oscillating — on the scope, minding probe loading. Then strap and boot-mode pin levels. Only then do I attach the debugger. Cheapest checks first."

**Don 연결**: 이 순서는 레쥬메의 "Silicon/system bring up", "Oscilloscope, Logic Analyzer, Power Analyzer"와 직접 붙는다. 실제 사례 하나를 미리 준비할 것 <확인 필요: 첫 전원 인가에서 문제를 잡았던 구체 사례 1건>.

### Q07. [중급] Give me your bring-up order for a new board, and justify the order.

**왜 묻나**: BSP 오너의 핵심 산출물이 이 순서다. (J13 Q07과 같은 질문 — 여기서는 "왜 그 순서인가"를 더 깊게)

**30초 답변**: 전원 → 리셋·클럭 → 디버그 접속 → 콘솔(UART) → 내부 주변장치(GPIO 토글로 살아있음 확인) → 저장장치/flash → I2C 스캔 → SPI 디바이스 → 센서 → 오디오 → 무선 → 전력 측정. 순서의 원리는 두 가지다. **첫째, 관측 수단을 먼저 확보한다** — 디버거와 콘솔이 없으면 그 뒤 모든 디버깅이 장님 싸움이다. **둘째, 의존성의 아래에서 위로** — 클럭 없이 버스가 없고, 버스 없이 센서가 없다. 각 단계는 "다음으로 넘어가도 되는 판정 기준"을 하나씩 갖는다(예: I2C는 스캔에서 예상 주소가 전부 ACK).

> "Power, then reset and clocks, then debugger attach, then the console, then GPIO toggling to prove the core is alive, then storage, then an I2C scan, then SPI devices, sensors, audio, radios, and finally power measurement. Two principles: get your observation tools up first — without a debugger and a console everything after is blind — and go bottom-up through the dependency chain. Each step has an explicit pass criterion before I move on."

**꼬리질문**: "순서를 어길 때는?" → 하드웨어 일정 때문에 병렬로 가야 할 때. 그때는 각 조각이 독립적으로 검증 가능한지 먼저 확인한다.

### Q08. [중급] The debug probe can't connect to the new target. Walk me through it.

**왜 묻나**: bring-up에서 가장 자주 막히는 지점이고, HW/FW 경계 판정 능력을 본다. (J13 Q08 참조)

**30초 답변**: 위에서 아래로 좁힌다. ① 프로브 자체와 케이블을 알려진 좋은 보드에서 검증 ② 타깃 VREF가 프로브에 실제로 들어가나 — 여기가 제일 흔하다 ③ 리셋과 SWDIO/SWCLK 배선, 직렬 저항, 풀업 ④ 타깃 전원과 클럭이 살아 있나 ⑤ 코어가 잠들거나(deep sleep에서 디버그 도메인이 꺼짐) 디버그 포트가 잠겼는지(옵션 바이트, 이전 펌웨어가 SWD 핀을 GPIO로 먹싱) ⑥ connect-under-reset으로 재시도. ⑤가 특히 고약한 이유는 **펌웨어가 자기 디버그 포트를 죽일 수 있기 때문**이다.

> "Top down. Prove the probe and cable on a known-good board. Check VREF actually reaches the probe — that's the most common one. Check SWDIO/SWCLK routing, series resistors and pull-ups, and reset. Check target power and clock. Then consider the target locking itself out: deep sleep powering down the debug domain, option bytes, or previous firmware muxing the SWD pins to GPIO. Finally, connect under reset. The nasty class is firmware killing its own debug port."

### Q09. [심화] Describe the boot chain of an ARM application processor running Linux.

**왜 묻나**: Cortex-A 경험 유무를 보는 표준 질문. Don에게는 방어 문항이다. (B03 1절이 근거)

**30초 답변**: B03 10절의 답을 그대로. BootROM(root of trust) → TF-A BL1(AP Trusted ROM, EL3) → BL2(Trusted Boot Firmware, Secure-EL1, DRAM 세우고 나머지 로드) → BL31(EL3 Runtime Software, 상주, PSCI 제공) → 선택적 BL32(TEE) → BL33(normal world 부트로더: U-Boot proper 또는 Android ABL) → 커널(`x0`=DTB 물리주소, MMU off, EL2 권장) → init. **그리고 반드시 마지막에 경계를 붙인다**: 구조는 알지만 직접 소유해 본 건 Cortex-R/M bare-metal이다.

> (B03 10절의 영어 답변을 그대로 사용. 마지막 문장을 빼지 말 것.)

**꼬리질문**: "Qualcomm에서는 이름이 뭔가?" → PBL/XBL/ABL 식으로 불리지만 **세대마다 다르고 공개 1차 문서가 없어서** 단정하지 않겠다, 벤더 문서를 받아 맞추겠다(B03 1.5). 이게 정답이다.

### Q10. [심화] How is bring-up different when the silicon itself is new?

**왜 묻나**: 이 질문이 나오면 Don의 홈그라운드다. 대부분의 지원자가 못 하는 경험이다.

**30초 답변**: 세 가지가 달라진다. ① **무엇을 믿을 수 있는지가 불명확하다** — 양산 보드에서는 칩이 맞다고 가정하고 내 코드를 의심하지만, 새 실리콘에서는 셋 다(칩, 보드, 펌웨어) 용의자다. 그래서 각 가설을 독립적으로 죽일 수 있는 최소 테스트를 먼저 만든다. ② **문서가 틀려 있다** — 레지스터 맵과 실제 RTL이 다른 경우가 있어서, 읽기 전용 ID 레지스터나 알려진 리셋 값으로 문서 자체를 먼저 검증한다. ③ **FPGA/에뮬레이션이 선행된다** — 실리콘 전에 FPGA에서 드라이버를 올려 두면, 실물이 왔을 때 "FPGA에서 되던 게 안 된다"로 문제 공간이 확 줄어든다.

> "Three things change. First, you don't know what to trust — on a mature board you assume the chip is right and suspect your code; with new silicon the chip, the board and the firmware are all suspects, so I build the smallest test that can independently kill each hypothesis. Second, the documentation is sometimes wrong, so I validate the register map itself against read-only ID registers and known reset values before trusting anything else. Third, FPGA pre-silicon work comes first — if the driver already runs on FPGA, then when real parts arrive the problem space collapses to 'what changed between FPGA and silicon'."

**Don 연결**: 레쥬메 직접 근거 — "ARM Cortex R8/R82/M0+ … FW bring-up", "SoC verification … I2C, SPI, DMA, PCIe, SRAM/DRAM bring-up (FPGA pre-silicon)". **이 문항은 반드시 구체적 사례 1건을 준비할 것.**

---

## 3. Devicetree와 보드 기술

### Q11. [기초] What is devicetree and why does it exist?

**왜 묻나**: BSP의 현대적 형태를 아는지.

**30초 답변**: 하드웨어 배선을 코드가 아니라 데이터로 기술하는 문법이다. 존재 이유는 하나 — 보드가 하나 늘 때마다 C 코드를 늘리지 않기 위해서다. Linux 문서 표현으로는 "decoupling the hardware configuration from the board and device driver support". 노드는 `compatible`(누가 이걸 맡나), `reg`(어디 있나), `interrupts`(어떻게 알리나), `clocks`/`*-supply`/`pinctrl-*`(살아나려면 뭐가 필요한가)로 읽으면 된다.

> "It's a syntax for describing how hardware is wired as data instead of code. The reason it exists is to stop adding C for every new board — decoupling the hardware configuration from board and driver support. I read a node in four questions: compatible says who owns it, reg says where it is, interrupts says how it signals, and clocks, supplies and pinctrl say what it needs to come alive."

### Q12. [중급] In a Zephyr BSP, what goes in devicetree, what goes in Kconfig, and what goes in code?

**왜 묻나**: 이 셋을 섞어 말하는 지원자가 매우 많다. 판별력이 높은 질문이다. (J02 Q14와 동일 — 여기서는 판정 규칙을 명시적으로)

**30초 답변**: 판정 규칙은 "무엇이 바뀌면 이게 바뀌나"다. **devicetree = 보드가 바뀌면 바뀌는 것**(어떤 장치가 어느 버스·어느 핀·어느 주소에 있나, 인터럽트, 레귤레이터, 클럭). **Kconfig = 소프트웨어 구성**(어떤 서브시스템·드라이버를 빌드에 넣나, 로그 레벨, 스택 크기, 기능 on/off). **코드 = 정책과 동작**(무엇을 언제 읽고 어떻게 반응하나). 자주 하는 실수가 DT에 정책을 넣는 것 — "샘플링 100Hz"는 대개 앱 설정이지 하드웨어 사실이 아니다.

> "The test is: what has to change for this to change? Devicetree holds what changes when the board changes — which device is on which bus, at which address, on which pins, its interrupts, supplies and clocks. Kconfig holds software composition — which drivers and subsystems get built in, log levels, stack sizes, feature switches. Code holds policy and behavior. The common mistake is putting policy in devicetree: 'sample at 100 Hz' is usually an application choice, not a hardware fact."

**꼬리질문**: "드라이버를 켜려면 DT만 고치면 되나?" → 보통 둘 다 — DT에 노드가 `status = "okay"`로 있어야 하고 Kconfig에서 드라이버가 빌드에 포함돼야 한다.

### Q13. [중급] How does devicetree in Linux differ from Zephyr?

**왜 묻나**: Cortex-M과 Cortex-A 양쪽을 아는지 한 번에 확인하는 효율적 질문.

**30초 답변**: 문법은 같고 소비 시점이 다르다. Linux는 부트로더가 DTB를 메모리에 놓고 커널이 **런타임에** 파싱해 platform device를 만든다(`of_platform_populate`) — 커널 재빌드 없이 DTB만 교체 가능. Zephyr는 **빌드 타임에** 매크로로 펼쳐 `devicetree_generated.h`를 만들고 디바이스 인스턴스를 컴파일 타임에 확정한다 — DT를 고치면 반드시 재빌드, 대신 안 쓰는 디바이스는 바이너리에 안 들어간다.

> "Same syntax, different consumption. Linux parses a DTB at runtime and populates platform devices from it, so you can swap the DTB without rebuilding the kernel. Zephyr expands devicetree into macros at build time into devicetree_generated.h and fixes device instances at compile time — change it and you must rebuild, but unused devices cost zero flash."

### Q14. [중급] You add a sensor node to the devicetree but the driver never runs. Debug it.

**왜 묻나**: 실제로 해 본 사람만 가진 체크리스트가 있는지.

**30초 답변**: 다섯 군데를 순서대로 본다. ① 노드와 부모 버스가 `status = "okay"`인가 ② `compatible` 문자열이 바인딩과 정확히 같은가(오타 하나면 조용히 아무 일도 안 일어난다) ③ 해당 드라이버가 Kconfig로 빌드에 들어갔는가 ④ 부모 버스가 먼저 초기화되는가(초기화 순서/우선순위) ⑤ 생성된 결과물을 직접 확인 — Zephyr면 `build/zephyr/zephyr.dts`와 생성 헤더, Linux면 `/proc/device-tree`와 deferred probe 목록. 핵심은 **"내가 쓴 DT"가 아니라 "빌드가 만든 DT"를 보는 것**이다.

> "Five checks in order. Is the node and its parent bus status okay. Does the compatible string exactly match the binding — one typo and nothing happens, silently. Is the driver actually enabled in Kconfig. Does the parent bus initialize first. And then look at the generated output, not my source: zephyr.dts and the generated header in Zephyr, /proc/device-tree and the deferred-probe list on Linux. The rule is to inspect the devicetree the build produced, not the one I wrote."

### Q15. [심화] A new peripheral has no existing binding. What do you do?

**왜 묻나**: BSP 오너는 새 바인딩을 쓰는 사람이다. 소비자가 아니라 생산자인지 본다.

**30초 답변**: 먼저 비슷한 기존 바인딩을 찾아 최대한 따라간다 — 새 프로퍼티를 만드는 것보다 표준 프로퍼티(`reg`, `interrupts`, `clocks`, `*-supply`, `*-gpios`)를 재사용하는 게 항상 낫다. 그다음 바인딩 YAML을 쓰고(`compatible`을 `<vendor>,<device>` 형식으로), 드라이버를 그 compatible에 묶는다. 판단 기준은 "이 프로퍼티는 하드웨어 사실인가, 소프트웨어 선택인가" — 후자면 바인딩에 넣지 않는다. Linux 업스트림에 낼 거라면 바인딩 스키마 검토가 리뷰의 절반이다.

> "First look for the closest existing binding and follow it — reusing standard properties like reg, interrupts, clocks, supplies and gpios beats inventing new ones. Then write the binding YAML with a vendor,device compatible and bind the driver to it. The filter for each property is whether it's a hardware fact or a software choice; software choices don't belong in the binding. If it's headed upstream, the binding review is half the work."

---

## 4. 드라이버 통합

### Q16. [기초] SPI or I2C for a new sensor — how do you choose?

**왜 묻나**: 회로도 리뷰 단계에서 펌웨어가 실제로 내는 의견이다. (상세는 J02 2.5~2.7, S02 Q09)

**30초 답변**: 데이터 레이트와 핀 수의 트레이드오프다. I2C는 2선에 여러 디바이스를 공유하고 주소로 구분하지만, open-drain이라 속도가 제한되고 풀업·정전용량 계산이 필요하며 **버스 하나가 죽으면 여러 장치가 같이 죽는다**. SPI는 푸시풀이라 훨씬 빠르고 디바이스당 CS 핀이 필요하며 장애가 격리된다. 오디오나 고속 IMU 스트림이면 SPI, 저속 설정성 디바이스(PMIC, 코덱 제어, 온도센서)면 I2C. always-on 기기라면 **슬립 중 누설**도 기준에 넣는다 — I2C 풀업은 버스가 low일 때 전류를 계속 먹는다.

> "It's data rate against pin count. I2C shares two wires across devices by address, but it's open-drain: speed is limited, you have to do the pull-up and capacitance math, and one stuck bus takes several devices down with it. SPI is push-pull, much faster, needs a chip select per device, and isolates failures. Audio or a fast IMU stream goes SPI; slow configuration devices go I2C. On an always-on device I also weigh sleep leakage — I2C pull-ups burn current whenever the bus sits low."

### Q17. [중급] A vendor hands you an SDK driver. How do you integrate it?

**왜 묻나**: JD 단어가 `development`가 아니라 `integration`이다(J02 0절). 실무의 대부분이 이것이다.

**30초 답변**: 네 단계다. ① **격리** — 벤더 코드를 우리 트리의 별도 디렉터리에 원본 그대로 넣고 우리 수정은 패치로 분리한다. 그래야 다음 SDK 버전에 리베이스할 수 있다. ② **어댑터** — 벤더 API를 우리 드라이버 인터페이스로 감싸는 얇은 층을 둔다. 앱은 벤더 API를 직접 부르지 않는다. ③ **가정 점검** — 블로킹 딜레이, 자체 인터럽트 우선순위 설정, malloc, 자체 타이머 사용, 재진입 불가 여부. RTOS에서 이 네 가지가 가장 많이 터진다. ④ **검증** — 하드웨어 위에서 스트레스(연속 전송, 동시 버스 접근, 저전력 진입/복귀)를 돌려 본다. 그리고 **어느 버전을 썼는지 기록**한다.

> "Four steps. Isolate: vendor code goes in its own directory, pristine, with our changes as separate patches so the next SDK drop can be rebased. Adapt: a thin wrapper maps the vendor API to our driver interface, and application code never calls the vendor API directly. Audit assumptions: blocking delays, the driver setting its own interrupt priorities, malloc, its own timers, non-reentrancy — those four break RTOS integration most often. Then validate on hardware under stress: back-to-back transfers, concurrent bus access, sleep entry and exit. And record exactly which vendor version we shipped."

**Don 연결**: ③은 Apple에서 벤더 실리콘/펌웨어를 제품에 통합한 경험과 직접 연결된다. "벤더 코드가 우리 시스템 가정을 어겼던 사례"를 하나 준비할 것 <확인 필요: 해당 사례가 공개 가능한 수준인지>.

### Q18. [중급] How do you structure a driver so application code survives a board respin?

**왜 묻나**: Q04의 실행 버전. 리비전이 계속 나오는 초기 제품에서 가장 큰 비용 요인이다.

**30초 답변**: 앱은 "무엇을"만 알고 "어디에"는 모르게 한다. 구체적으로 ① 앱은 논리적 이름(`imu`, `mic0`)으로 디바이스를 얻고 물리 정보(버스, 주소, 핀)는 전부 보드 기술에 둔다 ② 드라이버 인터페이스는 기능 단위로 고정하고 칩 교체는 그 뒤에서 흡수한다 ③ 리비전 차이는 보드 기술의 overlay/리비전 파일로 표현하고 앱에는 `#ifdef`를 절대 넣지 않는다 ④ 런타임 능력 조회(이 보드에 햅틱이 있나)를 API로 제공한다. Zephyr는 이 구조를 devicetree + device model로 강제해 주기 때문에 respin 비용이 낮다.

> "The application knows what, never where. It gets devices by logical name while bus, address and pins live entirely in the board description. Driver interfaces are defined by capability so a chip swap is absorbed underneath. Revision differences live in board overlays, never as #ifdef in application code. And there's a runtime capability query for things like 'does this board have haptics'. Zephyr enforces most of that for you through devicetree and the device model, which is why respins get cheaper."

### Q19. [중급] Polling, interrupt or DMA — and what changes in the BSP for each?

**왜 묻나**: 드라이버 설계 판단 + BSP가 무엇을 준비해야 하는지 아는가. (S02 Q12 참조)

**30초 답변**: 판단 기준은 전송 빈도 × 전송 크기 × 허용 지연이다. 폴링은 아주 짧고 드문 전송(부팅 중 ID 읽기)이나 인터럽트를 못 쓰는 문맥에서. 인터럽트는 이벤트 단위가 작고 산발적일 때. DMA는 주기적이고 큰 스트림 — 오디오 I2S는 사실상 DMA 필수다. BSP 쪽 준비물이 다르다: 인터럽트면 NVIC 우선순위 정책과 RTOS가 허용하는 우선순위 범위, DMA면 채널 할당·중재 우선순위, 캐시가 있는 코어면 버퍼 정렬과 캐시 유지(clean/invalidate), 그리고 버퍼 메모리를 DMA가 접근 가능한 영역에 배치하는 링커 설정.

> "Rate times size times latency budget. Polling for short, rare transfers or contexts where you can't take an interrupt. Interrupts for small sporadic events. DMA for periodic bulk streams — audio over I2S is effectively DMA-only. What the BSP has to provide differs: for interrupts, an NVIC priority policy that respects the RTOS's allowed range; for DMA, channel assignment and arbitration priority, buffer alignment and cache maintenance on cores with a data cache, and linker placement so the buffer lives somewhere DMA can actually reach."

### Q20. [심화] An I2S microphone path works but there's a periodic click in the audio. How do you find it?

**왜 묻나**: I2S는 Don의 명시적 갭이다(context 3.1). 구조적으로 답하는 연습이 필요하다.

**30초 답변**: "주기적"이라는 단서부터 쓴다. 클릭 주기를 재서 버퍼 주기와 비교한다. 버퍼 주기와 같으면 **버퍼 경계 문제** — DMA ping-pong에서 한쪽을 처리하는 동안 다음 버퍼를 놓치는 오버런, 또는 캐시 일관성 때문에 경계에서 옛 데이터가 보이는 것. 프레임 주기와 관계없는 주기면 클럭 쪽 — 다른 클럭 도메인 사이의 드리프트나 PLL 지터, 혹은 전원 레일에 실린 주기적 노이즈. 측정 순서는 ① 로직 분석기로 BCLK/LRCLK/데이터를 떠서 프레임 정렬과 워드 길이가 맞는지 ② DMA 완료 인터럽트와 처리 task의 타이밍을 트레이스로 ③ 전원 레일을 스코프로. 마지막으로 코드에서 드롭 카운터를 심어 놓는다 — 추측 대신 숫자로 말하기 위해.

> "I start with 'periodic'. Measure the click interval and compare it to the buffer period. If they match, it's a buffer-boundary problem: overrun in the DMA ping-pong, or stale data at the boundary from cache coherency. If it doesn't match the frame period, I look at clocking — drift between domains, PLL jitter, or periodic noise on the rail. Measurement order: logic analyzer on BCLK, LRCLK and data to confirm frame alignment and word length; then trace the DMA completion interrupt against the consuming task; then the rail on the scope. And I add a drop counter in firmware so I'm quoting numbers, not impressions."

**Don 연결**: I2S 자체는 새롭지만, **DMA 더블 버퍼링과 링버퍼 오버런은 SSD 데이터 경로에서 다뤄 본 문제**다. 그 번역을 명시적으로 말하면 갭이 작아 보인다.

### Q21. [심화] How do you test a driver when the hardware isn't available yet?

**왜 묻나**: pre-silicon 경험자를 찾는 질문. Don의 강점 영역이다.

**30초 답변**: 세 층으로 나눈다. ① **레지스터 접근을 추상화**해서 테스트에서 모의 레지스터 파일로 바꿔치기할 수 있게 한다 — 상태 기계와 에러 처리는 호스트에서 유닛 테스트로 전부 커버 가능하다. ② **시뮬레이터/에뮬레이터** — QEMU나 Renode로 부팅과 초기화 순서를 돌려 보고, Zephyr면 `native_sim`으로 상위 로직을 검증한다. ③ **FPGA pre-silicon** — 실제 RTL이 있으면 이게 가장 가치 있다. 타이밍은 실물과 다르지만 프로토콜과 레지스터 시맨틱은 검증된다. 이렇게 해 두면 실물이 왔을 때 남는 문제가 "타이밍, 전원, 아날로그"로 줄어든다.

> "Three layers. Abstract register access so tests can substitute a mock register file — state machines and error paths get full unit coverage on the host. Then simulation: QEMU or Renode for boot and init ordering, or native_sim in Zephyr for the upper logic. Then FPGA pre-silicon if the RTL exists, which is the highest-value one: timing differs from real parts, but protocol and register semantics get validated. Do all three and what's left when real boards arrive is timing, power and analog."

**Don 연결**: ③은 레쥬메 근거가 직접 있다. 이 문항은 자신 있게 말할 것.

---

## 5. 초기화 순서 버그

### Q22. [기초] What is an initialization order bug? Give me a concrete example.

**왜 묻나**: BSP 버그의 대표 유형. 개념을 말로 정리해 본 적이 있는지.

**30초 답변**: A가 B에 의존하는데 A가 먼저 초기화되는 것이다. 예: 센서 드라이버가 I2C 컨트롤러보다 먼저 초기화되면 첫 전송이 실패한다. 더 흔하고 고약한 건 **전원/리셋 의존** — 레귤레이터가 안정되기 전에 디바이스에 말을 걸면 ACK가 안 오거나 레지스터가 이상한 값을 반환한다. 증상의 특징은 **간헐적**이라는 것 — 타이밍이 경계에 있으면 온도나 빌드 최적화 레벨에 따라 되기도 하고 안 되기도 한다.

> "A depends on B, but A initializes first. The simple case is a sensor driver coming up before its I2C controller, so the first transfer fails. The nastier case is a power or reset dependency: you talk to a device before its regulator has settled, and you get NACKs or garbage registers. The signature is intermittency — if the timing sits on the edge, it works or fails depending on temperature or optimization level."

### Q23. [중급] A driver works when you single-step it but fails at full speed. What's your first hypothesis?

**왜 묻나**: 초기화 순서·타이밍 버그의 전형적 증상. 이 연결을 즉시 못 하면 실무 경험이 얕은 것이다.

**30초 답변**: 스텝 실행이 **공짜 딜레이를 넣어 준 것**이다. 즉 어딘가에 필요한 대기가 빠져 있다. 후보는 레귤레이터 안정 시간, 리셋 해제 후 디바이스 부팅 시간, 크리스털·PLL 락 시간, 센서의 power-on-reset 시간. 확인 방법은 의심 지점에 명시적 딜레이를 넣어 증상이 사라지는지 보고, 사라지면 **데이터시트에서 정확한 수치를 찾아** 그 값으로 바꾼다. 딜레이 대신 상태 폴링(레디 비트, ID 레지스터 읽기)으로 바꾸는 게 더 낫다 — 온도·로트 편차에 강하다.

> "Single-stepping inserted free delay, so something is missing a required wait. Candidates: regulator settling, device boot time after reset release, crystal or PLL lock, sensor power-on-reset. I confirm by adding an explicit delay at the suspected point; if the symptom disappears I go find the real number in the datasheet. Better still, replace the delay with polling a ready bit or reading the ID register — that's robust across temperature and part lots."

### Q24. [중급] How do you control initialization order in an RTOS BSP, and what if two drivers depend on each other?

**왜 묻나**: 프레임워크의 실제 메커니즘을 아는지.

**30초 답변**: Zephyr는 초기화 레벨(예: `PRE_KERNEL_1`, `PRE_KERNEL_2`, `POST_KERNEL`, `APPLICATION`)과 레벨 안의 우선순위 값으로 순서를 결정한다 — 버스 컨트롤러는 그 위에 붙는 디바이스보다 앞 레벨/낮은 우선순위 값에 둔다. bare-metal이면 초기화 테이블의 배열 순서가 곧 순서다. 상호 의존이 생기면 대개 설계가 틀린 것이고, 해결책은 ① 초기화를 2단계로 쪼개기(등록 단계와 활성화 단계 분리) ② 늦은 초기화(첫 사용 시점에 초기화) ③ Linux처럼 실패 후 재시도(`-EPROBE_DEFER`). 어느 쪽이든 **순환을 코드로 숨기지 말고 명시적으로 깨야** 한다.

> "Zephyr uses init levels plus a priority within the level, so a bus controller sits ahead of the devices on it. Bare metal, the init table order is the order. If two drivers genuinely need each other, the design is usually wrong; the fixes are splitting init into a register phase and an activate phase, deferring to first use, or the Linux approach of returning EPROBE_DEFER and retrying. Whichever you pick, break the cycle explicitly rather than hiding it."

### Q25. [중급] Sensor probe fails on cold boot but succeeds after a warm reset. What's happening?

**왜 묻나**: 전원 시퀀싱과 초기화 순서가 만나는 지점. 실제 bring-up에서 매우 흔하다.

**30초 답변**: 따뜻한 리셋에서는 레일이 이미 올라와 있고 디바이스도 이미 부팅돼 있다. 즉 콜드 부트에만 있는 것은 **전원 램프와 디바이스 자체 초기화 시간**이다. 그래서 의심 순서는 ① 레귤레이터 enable 후 대기 부족 ② 디바이스 리셋 해제 후 내부 부팅 시간 부족 ③ 레일 램프 레이트가 데이터시트 요구를 못 맞춤(HW 이슈) ④ 콜드 부트에서만 다른 클럭 소스를 쓰는 경우. 측정은 스코프로 레일과 리셋, I2C SDA를 동시에 캡처해 **어느 순간에 첫 전송이 나가는지**를 눈으로 확인하는 것이 가장 빠르다.

> "A warm reset skips the rails coming up and the device's own boot. So the delta is power ramp plus device initialization time. My order of suspicion: not enough delay after regulator enable; not enough time after reset release for the device to boot; a ramp rate that violates the datasheet, which is a hardware issue; or a different clock source on cold boot. Fastest measurement is a scope capture of the rail, the reset line and SDA together, so I can literally see when the first transfer goes out relative to power."

### Q26. [심화] What can go wrong with static initialization in a C++ firmware BSP?

**왜 묻나**: C++를 쓰는 팀에서 실제로 물리는 문제. (C02와 연결)

**30초 답변**: 두 가지다. ① **static initialization order fiasco** — 서로 다른 번역 단위의 전역 객체 초기화 순서는 정의되지 않아서, 한 전역이 다른 전역에 의존하면 터진다. 해결은 함수 지역 static(최초 사용 시 초기화) 또는 아예 전역 객체를 안 쓰기. ② **생성자가 하드웨어를 건드리는 경우** — startup 코드가 `main()` 전에 생성자를 돌리는데, 그 시점엔 클럭·전원이 아직 준비 안 됐을 수 있다. 그래서 펌웨어에서는 생성자는 메모리만 초기화하고 하드웨어 접근은 명시적 `init()`으로 분리하는 규칙을 둔다.

> "Two things. The static initialization order fiasco: order across translation units is unspecified, so one global depending on another breaks. Fix it with function-local statics or by not having those globals. And constructors touching hardware: startup runs them before main, when clocks and rails may not be up yet. So the rule I'd set is that constructors only initialize memory, and any hardware access goes in an explicit init call."

---

## 6. 보드 리비전 관리

### Q27. [기초] Rev B moved a sensor to a different I2C bus. What has to change?

**왜 묻나**: Q18의 구체적 시험. 여기서 "코드에서 주소를 바꾼다"고 하면 설계 감각이 없는 것이다.

**30초 답변**: 이상적으로는 **보드 기술 파일 한 곳만**이다 — devicetree에서 센서 노드를 다른 버스 노드 아래로 옮기고 필요하면 주소를 바꾼다. 드라이버도 앱도 안 바뀐다. 만약 앱 코드를 고쳐야 한다면 그건 리비전 문제가 아니라 **설계가 새는 것**이고, 그 자체를 고쳐야 한다.

> "Ideally exactly one place: the board description. Move the sensor node under the other bus in devicetree, adjust the address if needed. Driver and application don't change. If I find myself editing application code, that's not a revision problem, that's a leak in the abstraction, and I'd fix that instead."

### Q28. [중급] How does firmware know which board revision it's running on?

**왜 묻나**: 실제 구현 방법을 아는지. 정답이 여러 개고 트레이드오프가 있다.

**30초 답변**: 흔한 방법이 넷이다. ① **저항 스트랩 / GPIO 조합** — 가장 흔하고 싸다. 부팅 초기에 읽고, 읽은 뒤 풀 저항 전류를 끄는 것까지 고려한다. ② **ADC 분압** — 핀 하나로 더 많은 리비전 구분. ③ **EEPROM/OTP에 보드 ID 기록** — 가장 견고하지만 생산 공정이 필요하다. ④ **존재하는 부품으로 추론**(I2C 스캔 결과) — 최후 수단이고 애매함이 생긴다. 실무에서는 ①이나 ③을 쓰되, **"미래의 리비전에도 식별 핀을 남겨 달라"고 회로도 리뷰 단계에서 요구하는 것**이 진짜 답이다. 그리고 알 수 없는 ID를 만났을 때의 동작(가장 안전한 기본값 + 명확한 로그)을 정해 둔다.

> "Four common ways: resistor straps on GPIOs, which is cheapest and most common; an ADC divider when you need more codes from one pin; a board ID written to EEPROM or OTP, which is the most robust but needs a factory step; or inferring from which devices answer, which is a last resort. I'd use straps or a stored ID — but the real answer is asking for identification pins during schematic review, before the board exists. And define what happens on an unknown ID: safest defaults plus a loud log."

**Don 연결**: 회로도 리뷰에서 펌웨어가 요구사항을 내는 이야기는 J13 3.5절과 같은 근거를 쓴다.

### Q29. [중급] One binary across all revisions, or one build per revision?

**왜 묻나**: 정답이 없다. 판단 근거를 보는 질문이다.

**30초 답변**: 기본은 **하나의 바이너리 + 런타임 리비전 감지**다. 이유는 공장과 현장에서의 실수 비용 — 잘못된 바이너리를 잘못된 보드에 굽는 사고가 실제로 가장 비싸다. 하나의 바이너리면 그 사고가 구조적으로 사라진다. 예외는 세 가지다: ① 리비전 간 차이가 너무 커서 코드 크기가 문제가 될 때 ② 보안상 구형 리비전 지원을 끊어야 할 때 ③ 리비전 감지 수단 자체가 없을 때. 그리고 MP에 들어가면 지원 리비전을 명시적으로 줄이는 계획을 함께 세운다.

> "Default: one binary with runtime revision detection. The reason is failure cost in the factory and the field — flashing the wrong image onto the wrong board is the expensive mistake, and a single binary makes that structurally impossible. I'd break that rule in three cases: the revisions diverge enough that code size hurts, security requires dropping old revisions, or there's no reliable way to detect the revision. And I'd plan to prune supported revisions as we move toward mass production."

### Q30. [심화] Three board revisions are alive at once. How do you keep them all working?

**왜 묻나**: 초기 하드웨어 스타트업의 실제 일상. 프로세스 설계 능력을 본다.

**30초 답변**: 세 가지를 건다. ① **CI에서 전 리비전 빌드** — 리비전 파일이 깨지면 머지 전에 잡힌다. ② **하드웨어 테스트 랙** — 각 리비전 실물 1대씩 CI에 붙여 부팅 + 스모크 테스트(버스 스캔, 센서 읽기, 슬립 진입/복귀)를 자동으로 돌린다. 이게 없으면 "빌드는 되는데 보드에서 안 되는" 상태가 계속 쌓인다. ③ **지원 정책 문서화** — 어느 리비전을 언제까지 지원하고 언제 끊는지. 그리고 리비전별 차이를 한 곳(리비전 표)에 모아 둔다 — 사람 머리에 흩어져 있으면 반드시 잊힌다.

> "Three things. Build every revision in CI so a broken revision file is caught before merge. A hardware rack with one unit of each revision wired into CI running boot plus a smoke test — bus scan, sensor read, sleep entry and exit — because otherwise 'builds fine, doesn't boot' accumulates silently. And a written support policy for which revisions we keep alive and when we drop them, with all the per-revision differences in one table rather than in people's heads."

---

## 7. 전원 시퀀싱

### Q31. [기초] What is a power sequencing requirement, and where do you find it?

**왜 묻나**: 회로도·데이터시트를 실제로 읽는 사람인지.

**30초 답변**: 여러 전원 레일을 가진 칩이 정해진 순서와 시간 안에 올라와야 한다는 요구다. 어기면 최악의 경우 래치업이나 영구 손상, 보통은 그냥 안 깨어나거나 간헐적으로 이상 동작한다. 출처는 칩 데이터시트의 전원 시퀀스 다이어그램(레일 순서, 최대/최소 지연, 램프 레이트)과 보드의 power tree(PMIC 어느 채널이 어느 레일인가, enable을 누가 제어하나). 펌웨어 입장에서 첫 질문은 항상 **"이 레일은 PMIC가 자동으로 올리나, 내가 GPIO로 올리나"**다.

> "It's a chip's requirement that its rails come up in a specific order and within specific timing. Violating it can mean latch-up or permanent damage; usually it just means the part doesn't come up, or comes up intermittently. You find it in the datasheet's power sequencing diagram — rail order, min and max delays, ramp rates — and in the board's power tree. My first firmware question is always: does the PMIC bring this rail up automatically, or do I have to do it with a GPIO?"

### Q32. [중급] What exactly is firmware responsible for during power-up?

**왜 묻나**: HW와 FW의 책임 경계를 아는지. (J13 2.4 참조)

**30초 답변**: 세 가지다. ① PMIC가 자동으로 안 하는 레일을 정해진 순서와 지연으로 enable ② 리셋 해제 타이밍 — 레일 안정 후 규정된 시간이 지난 뒤 ③ 확인 — 레일 상태를 PMIC에서 읽거나 power-good 신호로 검증하고, 실패하면 안전한 상태로 간다. 그리고 역순 — 셧다운/슬립 진입에서 반대 순서를 지키는 것도 펌웨어 책임이다. 자주 잊히는 게 **슬립 진입 경로**로, 여기서 순서를 틀리면 누설이나 리크가 생긴다. 하드코딩된 `delay(10)` 대신 데이터시트 수치를 상수로 이름 붙여 두고 출처를 주석에 남긴다.

> "Three things: enable whatever rails the PMIC doesn't handle, in order with the required delays; release reset at the right time after the rails settle; and verify — read rail status from the PMIC or check power-good, and fail into a safe state if it's wrong. The reverse sequence on shutdown and sleep entry is firmware's job too, and sleep entry is the path people forget, which is where leakage shows up. I name the datasheet delays as constants with the source in a comment instead of leaving a bare delay(10)."

### Q33. [중급] The device works on a bench supply but not on battery. Where do you look?

**왜 묻나**: 배터리 기기 특유의 문제. JD의 always-on/battery 요건과 직결.

**30초 답변**: 벤치 서플라이와 배터리의 차이는 **임피던스와 전압 범위**다. 그래서 ① 돌입 전류 — 무선 송신이나 햅틱 구동 순간에 배터리 내부 저항 때문에 전압이 훅 꺼지고 브라운아웃 리셋이 걸린다. 스코프로 레일을 보면서 그 이벤트를 트리거한다. ② 저전압 동작 — 배터리 방전 말기 전압에서 레귤레이터가 드롭아웃에 걸리는지. ③ 충전기/PMIC 상태 — 배터리 경로에서만 동작하는 보호 로직(OCP, 온도). ④ 펌웨어 쪽 — 부팅 초기에 여러 부하를 동시에 켜지 않고 시간차를 두는지. 펌웨어가 할 수 있는 실질적 대응은 **부하 램프를 분산시키는 것**이다.

> "The difference is source impedance and voltage range. So: inrush — when the radio transmits or the haptic fires, battery ESR drops the rail and you brown out; I scope the rail triggered on that event. Low-battery operation — does the regulator hit dropout at end-of-discharge voltage. Charger and PMIC protection logic that only engages on the battery path. And firmware: am I turning several loads on simultaneously during boot? The practical firmware fix is staggering the load ramp."

### Q34. [심화] Design the power and wake architecture for an always-on MCU plus an Android SoC.

**왜 묻나**: Hark 기기 구조 그 자체(context 2.7). 시스템 설계 능력을 본다.

**30초 답변**: 역할 분리부터. **MCU는 항상 깨어 있고 SoC는 기본적으로 자고 있다.** MCU는 마이크·센서를 DMA로 상시 수집하고 wake word 후보를 판정한다. 깨우는 경로는 전용 GPIO 라인(레벨 유지 + 핸드셰이크)으로, SoC가 깨면 ACK를 돌려주고 그 사이 MCU는 오디오를 링버퍼에 계속 담아 **발화 앞부분을 잃지 않게** 한다. 상태는 셋 정도 — deep sleep(SoC off, MCU만 low duty), active(SoC 부팅·추론), 그리고 그 사이의 warm 상태(SoC는 suspend, 빠르게 복귀). 설계에서 결정적인 숫자는 ① SoC의 wake latency ② 그 지연을 덮을 링버퍼 크기 ③ false wake 비율 — false wake 하나가 배터리에 미치는 비용을 먼저 계산해야 임계값을 정할 수 있다. 실패 모드도 정해 둔다: SoC가 ACK를 안 하면 재시도 후 강제 리셋, 그리고 그 사건을 telemetry로 남긴다.

> "Split the roles: the MCU is always on, the SoC is asleep by default. The MCU streams mics and sensors over DMA and decides on wake-word candidates. The wake path is a dedicated line with a handshake — the SoC acknowledges when it's up, and meanwhile the MCU keeps buffering audio so we don't clip the start of the utterance. Three states: deep sleep with the SoC off, a warm state with the SoC suspended for fast resume, and active. The numbers that drive the design are SoC wake latency, the ring buffer size needed to cover it, and the false-wake rate — you can't pick a threshold until you've costed a false wake in battery terms. And define the failure mode: if the SoC doesn't acknowledge, retry, then force a reset, and log it in telemetry."

**Don 연결**: 링버퍼와 오버런 처리, telemetry 설계는 레쥬메 근거가 있다(NVMe telemetry, error reporting/handling). 이 문항은 갭이 아니라 **번역**으로 답한다.

---

## 8. Factory와 양산

### Q35. [기초] What does factory test firmware do that production firmware doesn't?

**왜 묻나**: JD에 명시된 항목이고 Don의 강점이다. 먼저 정의를 깔끔히.

**30초 답변**: 제품 펌웨어는 "제품답게 동작"하는 게 목표고, 팩토리 펌웨어는 "보드가 제대로 만들어졌는지 증명"하는 게 목표다. 그래서 들어가는 것: 테스트 모드 진입 경로, 개별 부품을 직접 두드리는 커맨드 인터페이스, 캘리브레이션 절차와 저장, 결과를 구조화해 내보내는 로깅, 그리고 시간 예산. 빠지는 것: 사용자 UX, 그리고 대개 저전력 동작. 가장 중요한 차이는 **판정 기준이 숫자로 명시돼 있어야 한다**는 것 — pass/fail 경계와 마진이 문서화돼야 라인에서 논쟁이 없다.

> "Production firmware tries to behave like a product; factory firmware tries to prove the board was built correctly. So it adds a test-mode entry path, a command interface that pokes individual components directly, calibration routines and storage, structured result logging, and a time budget. It drops user experience and usually low-power behavior. The biggest difference is that every check has a numeric pass/fail limit with documented margin, so nobody argues on the line."

**Don 연결**: 레쥬메 직접 근거 — "designing and leading factory test-node architecture". 이 문항은 Don이 면접을 주도할 수 있는 지점이다.

### Q36. [중급] Where do you store calibration data so it survives an OTA update?

**왜 묻나**: 실제로 필드에서 사고가 나는 지점.

**30초 답변**: 원칙은 **코드 이미지와 다른 수명 주기를 갖는 데이터는 코드와 같은 영역에 두지 않는다**. 그래서 별도 파티션 또는 OTP/보호 영역에 두고, 업데이트 이미지가 그 영역을 건드리지 않도록 파티션 맵과 플래시 절차가 보장해야 한다. 데이터 자체에는 버전·길이·CRC를 붙여 형식이 바뀌어도 읽는 쪽이 판정할 수 있게 하고, 쓰기 중 전원이 끊겨도 안전하도록 이중 슬롯 또는 저널을 쓴다. 마지막으로 **읽기 실패 시의 기본값과 로그**를 정의한다 — 캘리브레이션이 사라졌는데 조용히 기본값으로 도는 게 가장 나쁘다.

> "Rule: data with a different lifecycle than the code image doesn't live with the code. Put it in its own partition or in OTP, and make the partition map and the flashing procedure guarantee the update never touches it. Give the blob a version, a length and a CRC so a future format change is detectable, and use two slots or a journal so a power cut mid-write is safe. And define what happens when the read fails — silently falling back to defaults with no log is the worst outcome."

### Q37. [중급] How does the BSP change as you go EVT → DVT → PVT → MP?

**왜 묻나**: 컨슈머 제품 출시 경험(JD 우대 사항)을 검증하는 질문. Don의 강점.

**30초 답변**: 단계마다 BSP가 받는 압력이 다르다. **EVT**는 "일단 켜진다"가 목표 — 리비전이 자주 바뀌고 임시 회피책이 많다. **DVT**는 설계 검증 — 여기서 BSP는 임시 회피책을 제거하고 리비전 처리를 정식화해야 한다. 전력·발열 수치가 이 단계에서 처음 진지해진다. **PVT**는 제조 공정 검증 — 팩토리 펌웨어, 캘리브레이션, 프로비저닝이 실제 라인에서 돌아가야 하고 테스트 시간이 예산 안에 들어와야 한다. **MP**는 변경을 최소화하고 추적성과 롤백 경로를 지킨다. 한 줄로 요약하면 **BSP의 변경 자유도가 단계마다 줄어들고, 대신 증거 요구가 늘어난다**.

> "Each stage puts different pressure on the BSP. EVT is 'does it come up' — revisions churn and workarounds pile up. DVT is design validation, so the BSP has to shed those workarounds and formalize revision handling, and power and thermal numbers get real. PVT validates manufacturing: factory firmware, calibration and provisioning have to run on the actual line inside a time budget. MP is about minimizing change and preserving traceability and a rollback path. In one line: the BSP's freedom to change shrinks at every stage while the demand for evidence grows."

**Don 연결**: 레쥬메 "Silicon/system bring up -> NPI -> MP". 이 문항은 구체적으로 답할수록 강해진다 <확인 필요: 단계별로 실제 겪은 대표 이슈 각 1건>.

### Q38. [심화] Design a per-unit self-test that runs on every board off the line in under 30 seconds.

**왜 묻나**: 커버리지와 시간의 트레이드오프를 설계할 수 있는지. 스타트업이 가장 약한 영역이다.

**30초 답변**: 먼저 **무엇을 잡으려는지**부터 정한다 — 조립 결함(미납땜, 부품 미실장, 역방향, 냉납)이 대상이지 설계 검증이 아니다. 그러면 테스트는 "존재·연결·기초 동작" 3종으로 줄어든다. 구체적으로 ① 버스 열거(I2C 스캔, SPI ID 레지스터) — 몇 십 ms ② 각 센서의 기초 sanity(정지 상태의 가속도가 1g 근처인가, 온도가 실온 범위인가) ③ 마이크·스피커 루프백(알려진 톤을 내고 받은 신호의 SNR 측정) ④ 전원 레일 ADC 측정 ⑤ 무선 송신 파워 확인(캘리브레이션과 묶어서). 시간 예산은 각 항목에 밀리초 단위로 배분하고, **병렬화 가능한 것을 병렬로** 돌린다(오디오 루프백 중에 센서 읽기). 결과는 구조화된 레코드로 남기고, 실패는 원인 코드까지 남겨 라인에서 바로 분류할 수 있게 한다. 마지막으로 **테스트 자체의 false fail 비율**을 추적한다 — 이게 yield를 갉아먹는다.

> "Start from what we're catching: assembly defects — missing or misplaced parts, reversed components, cold joints — not design validation. That reduces the suite to presence, connectivity and basic function. Bus enumeration by I2C scan and SPI ID reads, tens of milliseconds. Per-sensor sanity: is the accelerometer reading about 1g at rest, is the temperature in room range. A mic and speaker loopback with a known tone and an SNR check. Rail measurements via ADC. RF transmit power, folded into calibration. I budget each item in milliseconds and parallelize what I can — read sensors during the audio loopback. Results go out as structured records with failure reason codes so the line can bin failures immediately. And I track the test's own false-fail rate, because that's what quietly eats yield."

---

## 9. Linux/Android 방어 문항

### Q39. [중급] Have you owned a Linux BSP?

**왜 묻나**: 이 질문은 거의 확실히 나온다. 제목이 BSP로 바뀌었고 기기에 Android SoC가 있다.

**30초 답변**: B03 8.3절 스크립트. 아니요 → 내가 한 것은 무엇인지 → 그래도 지도는 있다는 증거 → 과장 안 하겠다는 명시. 11절에 전체 스크립트가 있다.

**꼬리질문**: "그럼 우리 SoC 쪽 일은 누가 하나?" → 좋은 역질문 기회다. "이 자리의 SoC 대 MCU 비중이 어떻게 되는지 궁금하다"로 되받는다(context 4.7 역질문 1번).

### Q40. [중급] What does a vendor BSP drop contain, and what's your first week with it?

**왜 묻나**: 벤더 플랫폼 위에서 일해 본 감각이 있는지. Don은 Apple 경험으로 답할 수 있다.

**30초 답변**: B03 7절 표 — 부트로더(소스 또는 서명 바이너리), LTS 기반 커널 포크와 패치 시리즈, SoC·레퍼런스 보드 DTS, 벤더 드라이버와 펌웨어 블롭, HAL 구현, 빌드 메타데이터, flash·서명 툴, NDA 문서, 릴리스 노트. 첫 주는 릴리스 노트와 알려진 이슈를 먼저 읽고, 레퍼런스 보드를 **손대지 않고** 빌드·부팅시켜 기준선을 만든다. 기준선 없이 우리 보드로 넘어가면 나중에 원인을 못 가른다.

> "Bootloader sources or signed binaries, a kernel fork on a specific LTS with a patch series, SoC and reference-board device trees, vendor drivers and firmware blobs, HAL implementations, build metadata, flashing and signing tools, NDA documentation and release notes. My first week: read the release notes and known issues, then build and boot the reference board completely untouched to establish a baseline. Skip the baseline and you can't attribute anything later."

### Q41. [심화] Where does the boundary sit between your MCU firmware and the Android side?

**왜 묻나**: 두 프로세서 제품의 인터페이스 설계 능력. Hark 기기 구조와 직결(context 2.7).

**30초 답변**: 경계는 IPC 프로토콜이고, 그 프로토콜을 계약으로 다룬다. 정해야 할 것: 전송 수단(UART/SPI/공유 메모리), 프레이밍과 버전 필드, 명령·이벤트 구분, 타임스탬프 동기화 방식, 흐름 제어와 재시도 정책, 그리고 **각 쪽이 독립적으로 재부팅될 때의 복구 절차**. Android 쪽에서는 이게 HAL 또는 vendor 서비스로 노출되고(새 HAL이면 AIDL), 파티션 경계를 넘으면 stable AIDL + VINTF 선언이 필요하다. 내가 소유하는 건 MCU 쪽 절반과 프로토콜 정의이고, HAL 구현은 협업 지점이다.

> "The boundary is the IPC protocol, and I'd treat it as a contract: transport, framing with a version field, command versus event, timestamp synchronization, flow control and retry policy, and — the one people forget — what happens when either side reboots independently. On the Android side it surfaces as a HAL or vendor service; a new one would be AIDL, and crossing the partition boundary means stable AIDL declared in the VINTF manifest. I'd own the MCU half and the protocol definition; the HAL implementation is a collaboration point."

---

## 10. Don 매핑 — 레쥬메 근거가 어느 질문을 먹여 주나

근거는 전부 context 3.1절의 레쥬메 인용이다. 없는 건 없다고 적었다.

| 레쥬메 근거 | 직접 답할 수 있는 문항 | 어떻게 쓰나 |
|---|---|---|
| "SoC verification … I2C, SPI, DMA, PCIe, SRAM/DRAM bring-up (FPGA pre-silicon)" | Q06, Q07, Q10, Q21 | bring-up 순서와 pre-silicon 검증의 1차 증거 |
| "ARM Cortex R8/R82/M0+ … FW bring-up", bare-metal C/C++ | Q02, Q05, Q19, Q26 | 부트·링커·드라이버 층의 직접 경험 |
| Apple "root-cause of fundamental and interface level failures … new chip meets the full HW/SW system" | Q03, Q08, Q17, Q23, Q25 | "HW냐 FW냐 판정" 오너십의 핵심 근거 |
| Apple PCIe/I2C/SPMI/RFFE 장애 분석 | Q16, Q20, Q33 | 버스·신호 레벨 디버깅. I2S도 이 방법론으로 번역 |
| "Silicon/system bring up -> NPI -> MP" | Q30, Q37 | 단계별 압력 변화를 경험으로 말할 수 있음 |
| "designing and leading factory test-node architecture" | Q35, Q36, Q38 | 이 팀에 흔치 않은 차별점. 면접을 주도할 지점 |
| "sign off on hardware safety margins (reliability vs performance/power)" | Q31, Q32, Q33 | 마진 판단과 전력 측정 |
| JTAG / Trace32 / DSO / LA / Power Analyzer | Q06, Q08, Q20, Q25, Q33 | 측정 수단을 구체적으로 말할 수 있다는 것 자체가 신호 |
| NVMe telemetry, error reporting/handling 설계 | Q30, Q34, Q38 | 관측성·실패 모드 설계로 번역 |
| **근거 없음** — Linux/Android BSP | Q09, Q39, Q40, Q41 | 구조만 말하고 경계를 긋는다 |
| **근거 없음** — 상용 RTOS 실무 | Q24 | 메커니즘은 설명하되 "Zephyr는 학습 중"으로 |
| **근거 없음** — I2S/오디오 | Q20 | DMA·링버퍼 경험으로 번역, 새 영역임을 명시 |
| **근거 없음** — devicetree 실무 | Q11~Q15 | B02 실습으로 보강. "샘플을 돌려 봤다" 수준으로만 말할 것 |

---

## 11. "BSP를 소유해 본 적 있나?" 전체 스크립트

이 질문은 한 번 나오고, 그 답이 면접 전체의 톤을 정한다. 아래를 **문장 단위로 외운다**.

### 11.1 한국어 구조

1. 결론 먼저: "제품의 BSP를 처음부터 끝까지 소유해 본 적은 없습니다."
2. 대신 무엇을 했는지: "제가 해 온 건 그 아래층입니다. FPGA에서 실리콘 나오기 전에 Cortex-R과 M 코어를 올리고, I2C·SPI·DMA·SRAM/DRAM을 하나씩 살리고, 그 드라이버를 양산 실리콘으로 옮겼습니다."
3. 오너십의 증거: "Apple에서는 새 무선 칩이 전체 시스템을 처음 만나는 지점에서 인터페이스 장애의 root cause를 제가 책임졌습니다. 'HW 문제냐 FW 문제냐'를 판정하는 자리였습니다."
4. 지도를 갖고 있다는 증거: "BSP의 범위 — 부트, 클럭·전원 순서, 핀 먹싱, 메모리 맵, 보드 기술과 드라이버 바인딩, 리비전 관리 — 는 정확히 알고 있고, 제가 손으로 하던 일이 Zephyr의 devicetree나 Linux의 clk·regulator·pinctrl 프레임워크의 어디에 해당하는지도 압니다."
5. 경계와 계획: "제가 아직 안 해 본 건 두 가지입니다. 상용 RTOS의 보드 포팅을 처음부터 해 보는 것, 그리고 Linux/Android BSP를 운영하는 것. 앞쪽은 지금 Zephyr로 보드 레벨 작업을 직접 해 보고 있고, 뒤쪽은 구조는 알지만 소유해 봤다고는 말하지 않겠습니다."
6. 되돌리기: "이 자리에서 SoC 쪽과 MCU 쪽 비중이 어떻게 되는지 알 수 있을까요?"

### 11.2 영어 전체 (그대로 말할 것)

> "Honestly — I haven't owned a product BSP end to end. What I've owned is the layer underneath it. At Solidigm and SK hynix I brought up ARM Cortex-R and Cortex-M cores on FPGA before silicon existed, got I2C, SPI, DMA and SRAM/DRAM working one at a time, and carried those drivers onto production parts. At Apple I own root cause when a new wireless chip meets the full hardware and software system — PCIe, I2C, SPMI, RFFE — which in practice means I'm the person who decides whether a failure is hardware or firmware.
>
> So I know what a BSP consists of: startup and memory map, clock and power-up ordering, pin muxing, the board description and how drivers bind to it, and revision management. And I know where the things I used to do by hand live in a framework — devicetree and the device model in Zephyr, the clock, regulator and pinctrl subsystems in Linux.
>
> What I haven't done is two specific things: porting a board in a commercial RTOS from scratch, and operating a Linux or Android BSP. I'm doing the first one right now on my own hardware. The second one I know structurally, but I'm not going to tell you I've owned it.
>
> Can I ask — for this role, how does the split land between the SoC side and the always-on MCU side?"

### 11.3 왜 이렇게 쓰나

- **1번을 먼저 말하는 이유**: 나중에 밝혀지는 것보다 먼저 말하는 게 항상 강하다. 면접관은 "이 사람 말은 검증 없이 믿어도 되겠다"를 얻는다.
- **3번이 핵심**: BSP를 안 만들어 봤어도 **"HW냐 FW냐를 판정하는 책임"** 은 BSP 오너의 가장 어려운 부분이고, 그건 레쥬메에 근거가 있다.
- **5번에서 두 갭을 스스로 분리하는 이유**: 하나는 메울 수 있는 것(진행 중), 하나는 아닌 것. 이 구분을 지원자가 스스로 해 주면 면접관의 판단 비용이 줄고 신뢰가 올라간다.
- **6번**: 대화를 끝내지 말고 넘긴다. 동시에 context 4.7의 역질문 1번을 자연스럽게 소진한다.

### 11.4 하지 말 것

- "사실상 BSP였습니다" 같은 재정의. 면접관이 판단할 몫을 뺏는 것처럼 들린다.
- 갭을 말한 뒤 바로 사과하거나 길게 변명하기. 1~2문장이면 충분하고, 그다음은 계획으로 넘어간다.
- Zephyr 사이드 프로젝트를 실무처럼 말하기. "직접 해 보고 있다"까지가 정확한 표현이다.

---

## 12. 화이트보드 그리는 순서

면접에서 그림을 그리는 건 시간을 벌고 대화를 주도하는 수단이다. **세 장만 연습한다.** 각각 60초 안에 그릴 수 있어야 한다.

### 12.1 그림 A — BSP 층 (가장 자주 쓸 그림)

```
 [ application / product logic ]
 ─────────────────────────────── ← 이 선이 respin 방어선
 [ drivers  (기능 단위 인터페이스) ]
 [ board description (devicetree / board header) ]   ← 보드가 바뀌면 여기만
 [ HAL / register access ]
 [ startup · clock · power · pinmux · linker ]
 ═══════════════════════════════
 [ silicon ]      [ board (회로도) ]
```

그리는 순서: 맨 아래 실리콘 → 위로 올라오며 5층 → 마지막에 "respin 방어선"을 그으면서 Q04/Q18/Q27의 답을 한 번에 깔아 둔다.

### 12.2 그림 B — bring-up 순서 (Q07)

```
 전원 ─ 리셋·클럭 ─ 디버거 ─ 콘솔 ─ GPIO ─ 저장장치
        └→ I2C 스캔 ─ SPI ID ─ 센서 ─ 오디오 ─ 무선 ─ 전력 측정
 각 단계마다: [통과 기준 1개] [측정 수단 1개]
```

그리는 순서: 가로로 단계 → 각 단계 아래에 "무엇으로 확인하나"를 한 단어씩(DMM, 스코프, LA, 콘솔 로그). **측정 수단을 적는 순간 신뢰도가 올라간다.**

### 12.3 그림 C — 두 프로세서 wake 아키텍처 (Q34)

```
  [MCU: always-on]                     [SoC: Android]
   mic ─DMA→ ring buffer                 suspend
   VAD → wake word 후보                     ▲
        │  wake GPIO ──────────────────────┘
        │  ACK ◄────────────────────────────
        └─ 링버퍼를 이어서 전달 (앞부분 보존)
   숫자: SoC wake latency / 링버퍼 크기 / false wake rate
```

그리는 순서: 두 박스 → 마이크·DMA·링버퍼 → wake 라인 → ACK → 마지막에 "결정적 숫자 3개"를 적는다. 숫자를 적는 순간 설계 질문이 엔지니어링 대화로 바뀐다.

---

## 13. 준비 체크리스트

### 13.1 반드시

- [ ] Q01·Q03·Q07의 답을 **막힘없이** 말할 수 있다 (BSP 정의 / 소유의 의미 / bring-up 순서)
- [ ] 11.2절 영어 스크립트를 외워서 60초 안에 말한다
- [ ] 그림 A·B·C를 각각 60초 안에 그린다
- [ ] Q10(새 실리콘 bring-up)에 쓸 구체적 사례 1건을 STAR로 정리했다
- [ ] Q35·Q38(팩토리)에 쓸 사례 1건을 STAR로 정리했다
- [ ] Q12(DT vs Kconfig vs 코드) 판정 규칙을 한 문장으로 말한다
- [ ] Q09(Linux 부트 체인)를 이름과 EL까지 붙여 말하고, **마지막에 경계 문장을 붙인다**

### 13.2 하면 좋음

- [ ] Q20(I2S 클릭)을 측정 순서까지 포함해 말한다
- [ ] Q28~Q30(보드 리비전)을 한 덩어리로 연결해 말한다
- [ ] Q34(wake 아키텍처)에서 "결정적 숫자 3개"를 먼저 꺼낸다
- [ ] Zephyr로 보드 파일을 실제로 한 번 수정해 빌드·부팅시켜 봤다 (B02)
- [ ] QEMU에서 DTB를 덤프해 DTS를 읽어 봤다 (B03 11.2)

### 13.3 자주 틀리는 포인트

| 포인트 | 틀린 답 | 맞는 답 |
|---|---|---|
| BSP의 단위 | "칩을 지원하는 코드" | 보드를 지원하는 코드. 같은 SoC라도 보드마다 다르다 |
| 보드 리비전 대응 | 앱 코드에 `#ifdef` | 보드 기술 파일/overlay에서만 처리 |
| DT vs Kconfig | 둘을 섞어 설명 | DT=배선 사실, Kconfig=소프트웨어 구성, 코드=정책 |
| 부팅 실패 디버깅 | 코드부터 본다 | 전원 → 리셋 → 클럭 → strap → 디버거 순 |
| 스텝하면 되고 풀스피드면 안 됨 | "타이밍 이슈네요"에서 멈춤 | 빠진 대기를 지목하고 데이터시트 수치로 대체, 가능하면 폴링으로 |
| 벤더 드라이버 | 우리 코드에 섞어 넣기 | 원본 격리 + 어댑터 + 가정 점검 + 버전 기록 |
| 캘리브레이션 저장 | 코드 이미지와 같은 영역 | 별도 파티션/OTP + 버전·CRC + 실패 시 동작 정의 |
| Linux BSP 질문 | 아는 척하거나 반대로 그냥 "모릅니다" | 구조는 정확히, 경계는 명시적으로 |

---

## 참고 자료

이 노트의 사실 근거는 대부분 B01~B03에서 확인한 1차 문서다. 아래는 이 노트를 준비하며 직접 확인(✅)하거나, 답변 근거로 삼은 페이지다.

| 자료 | 확인 | 어디에 쓰이나 | URL |
|---|---|---|---|
| TF-A — Firmware Design | ✅ 2026-09-21 | Q09 부트 체인 스테이지 이름·EL | https://trustedfirmware-a.readthedocs.io/en/latest/design/firmware-design.html |
| U-Boot — SPL/TPL | ✅ 2026-09-21 | Q09의 BL33 계열 설명 | https://docs.u-boot.org/en/latest/develop/spl.html |
| Linux — arm64 booting | ✅ 2026-09-21 | Q09 커널 진입 규약(`x0`=DTB, MMU off) | https://docs.kernel.org/arch/arm64/booting.html |
| Linux — Devicetree usage model | ✅ 2026-09-21 | Q11·Q13 DT의 존재 이유와 런타임 소비 | https://docs.kernel.org/devicetree/usage-model.html |
| Zephyr — DT input/output files | ✅ 2026-09-21 | Q13·Q14 빌드타임 산출물(`zephyr.dts`, 생성 헤더) | https://docs.zephyrproject.org/latest/build/dts/intro-input-output.html |
| Android — AIDL for HALs | ✅ 2026-09-21 | Q41 stable AIDL·VINTF | https://source.android.com/docs/core/architecture/aidl/aidl-hals |
| Android — HAL types | ✅ 2026-09-21 | Q41 vendor 파티션 경계, HIDL deprecated | https://source.android.com/docs/core/architecture/hal |
| Zephyr — 보드 포팅 가이드 | — | Q02 보드 파일 구성. **버전마다 다름**(hardware model v2) | https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html |
| Zephyr — device driver model / 초기화 | — | Q24 init level과 우선순위. 매크로 이름은 버전 확인 필요 | https://docs.zephyrproject.org/latest/kernel/drivers/index.html |
| NXP UM10204 — I2C 스펙 | — | Q16 풀업·속도·버스 복구 | https://www.nxp.com/docs/en/user-guide/UM10204.pdf |
| Devicetree Specification | — | Q11·Q15 표준 프로퍼티와 바인딩 | https://www.devicetree.org/specifications/ |
| FreeRTOS 공식 문서 | — | Q24 초기화·스케줄러 문맥 | https://www.freertos.org/Documentation/00-Overview |

> **버전 의존 표기**: Zephyr의 보드 디렉터리 구성·초기화 매크로 이름, Linux 커널 API 시그니처, Android 파티션 구성은 릴리스마다 바뀐다. 면접에서 파일명·매크로명을 단정하지 말고 "트리를 보고 맞추겠다"로 답하는 편이 정확하다.
> **벤더 문서 없음**: Qualcomm·Ambiq 등의 BSP/SDK는 NDA·포털 배포이므로 공개 URL을 만들지 않았다. Qualcomm 부트 스테이지 명칭은 세대마다 다르다(B03 1.5).
