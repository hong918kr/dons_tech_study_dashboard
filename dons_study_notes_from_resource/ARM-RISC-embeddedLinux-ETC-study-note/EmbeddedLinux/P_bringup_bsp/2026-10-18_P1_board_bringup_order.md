# P1. 새 보드 bring-up 순서 — 전원부터 공장까지, 단계마다 "들어가는 조건·나오는 조건"

> **이 노트를 다 읽으면**: 새 보드 bring-up 12단계를 순서·진입/완료 기준과 함께 말할 수 있다 · 증상(무출력, 깨진 글자, SPL 멈춤, 커널 무출력)에서 원인 후보와 측정 포인트를 바로 고를 수 있다 · QEMU에서 최소 DT로 부팅하고 노드를 하나씩 붙이며 틀린 DT의 증상을 재현할 수 있다 · 인쇄용 체크리스트와 STAR 답변을 가진다
> **왜 필요한가**: "Walk me through bringing up a new board" 는 Embedded Linux·펌웨어 면접의 스토리 질문 1순위다. 입사 후 첫 보드가 오면 실제로 이 순서대로 일한다.
> **Don 기준 난이도**: 전원·클럭·리셋·JTAG·버스 디버깅은 이미 강점(Apple RF 실리콘 bring-up, SSD 컨트롤러 펌웨어) / 새로 배울 것은 DDR 이후의 Linux 쪽 — 부트로더 체인, earlycon, DT를 한 노드씩 늘리는 방법
> **선행 노트**: C1(부트 지도), C2(BootROM·부트 미디어), C4(U-Boot 포팅·SPL DDR), C5(TF-A·PSCI), C6(커널 시작·earlycon), D2·D4(DT 문법·자원), G3(platform driver), M1(로그·earlycon), M4(커널 디버거), M11(HW 연동 디버깅)

---

## 0. 큰 그림 — bring-up은 "믿을 수 있는 영역"을 한 칸씩 넓히는 일

새 보드가 처음 책상에 오면, 믿을 수 있는 것이 하나도 없다. 회로도는 맞는지, 부품은 제대로 붙었는지, PMIC 설정은 맞는지, DDR 배선 길이는 맞는지 — 전부 가설이다. bring-up은 **아래층부터 하나씩 "이건 확인됐다"는 도장을 찍으면서 올라가는 일**이다. 아래층을 확인하지 않고 위층을 디버그하면, 커널 드라이버를 사흘 동안 의심했는데 사실은 레일 전압이 5% 낮았던 일이 생긴다.

Don에게 익숙한 말로 하면: SSD 펌웨어 bring-up에서 "ROM 코드 → 로더 → NAND 인터페이스 → FTL → NVMe 큐" 순서로 올라갔던 것과 같다. Linux 보드에서는 이 사다리가 더 길고, 중간에 **Device Tree** 라는 "보드 설명서"가 끼어 있다는 점만 다르다.

```svg
<svg viewBox="0 0 680 560" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="340" y="22" text-anchor="middle" font-size="14">bring-up 12단계 — 왼쪽 단계를 끝내는 조건(exit criteria)이 오른쪽</text>
  <g font-size="12">
    <rect x="10" y="40" width="210" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="20" y="61">① 전원 레일·시퀀싱</text>
    <rect x="240" y="40" width="430" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="250" y="61">레일 ±3% 이내 · 순서/PGOOD 정상 · 대기 전류가 예상치</text>
    <rect x="10" y="80" width="210" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="20" y="101">② 클럭·크리스털</text>
    <rect x="240" y="80" width="430" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="250" y="101">OSC 주파수·진폭 OK · PLL lock (CLKOUT 핀으로 확인)</text>
    <rect x="10" y="120" width="210" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="20" y="141">③ 리셋</text>
    <rect x="240" y="120" width="430" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="250" y="141">POR_B 해제 타이밍 OK · 리셋 원인 레지스터 정상</text>
    <rect x="10" y="160" width="210" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="20" y="181">④ JTAG attach</text>
    <rect x="240" y="160" width="430" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="250" y="181">IDCODE 읽힘 · halt/step · 내부 SRAM 읽기/쓰기</text>
    <rect x="10" y="200" width="210" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="20" y="221">⑤ UART 콘솔·부트 모드</text>
    <rect x="240" y="200" width="430" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="250" y="221">BootROM/SPL 첫 줄 출력 · 부트 모드 핀이 의도대로</text>
    <rect x="10" y="240" width="210" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="20" y="261">⑥ DDR 초기화·training</text>
    <rect x="240" y="240" width="430" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="250" y="261">training PASS · 전 용량 memtest 0 error (온도별)</text>
    <rect x="10" y="280" width="210" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="20" y="301">⑦ SPL → TF-A → U-Boot</text>
    <rect x="240" y="280" width="430" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="250" y="301">U-Boot 프롬프트 · 저장장치/네트워크에서 이미지 로드</text>
    <rect x="10" y="320" width="210" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="20" y="341">⑧ 커널 earlycon+최소 DT</text>
    <rect x="240" y="320" width="430" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="250" y="341">shell 프롬프트 · 타이머 tick · 모든 CPU online</text>
    <rect x="10" y="360" width="210" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="20" y="381">⑨ DT 노드 하나씩</text>
    <rect x="240" y="360" width="430" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="250" y="381">노드마다 probe OK · deferred 0 · dmesg 에러 0</text>
    <rect x="10" y="400" width="210" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="20" y="421">⑩ 주변장치 검증</text>
    <rect x="240" y="400" width="430" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="250" y="421">버스별 기능·성능 PASS (eMMC, Eth, USB, 카메라)</text>
    <rect x="10" y="440" width="210" height="32" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="20" y="461">⑪ stress·thermal</text>
    <rect x="240" y="440" width="430" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="250" y="461">N시간 stress 0 error · 온도·쓰로틀링이 한계 안</text>
    <rect x="10" y="480" width="210" height="32" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="20" y="501">⑫ 공장(factory)</text>
    <rect x="240" y="480" width="430" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="250" y="501">프로비저닝·자가진단 자동화 · 수율 데이터 수집</text>
  </g>
  <g stroke="currentColor" stroke-width="1.2">
    <line x1="220" y1="56" x2="238" y2="56" marker-end="url(#ah1)"/><line x1="220" y1="96" x2="238" y2="96" marker-end="url(#ah1)"/>
    <line x1="220" y1="136" x2="238" y2="136" marker-end="url(#ah1)"/><line x1="220" y1="176" x2="238" y2="176" marker-end="url(#ah1)"/>
    <line x1="220" y1="216" x2="238" y2="216" marker-end="url(#ah1)"/><line x1="220" y1="256" x2="238" y2="256" marker-end="url(#ah1)"/>
    <line x1="220" y1="296" x2="238" y2="296" marker-end="url(#ah1)"/><line x1="220" y1="336" x2="238" y2="336" marker-end="url(#ah1)"/>
    <line x1="220" y1="376" x2="238" y2="376" marker-end="url(#ah1)"/><line x1="220" y1="416" x2="238" y2="416" marker-end="url(#ah1)"/>
    <line x1="220" y1="456" x2="238" y2="456" marker-end="url(#ah1)"/><line x1="220" y1="496" x2="238" y2="496" marker-end="url(#ah1)"/>
  </g>
  <g font-size="12">
    <rect x="20" y="530" width="14" height="14" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="40" y="542">HW (scope·DMM·JTAG)</text>
    <rect x="200" y="530" width="14" height="14" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="220" y="542">부트 펌웨어</text>
    <rect x="320" y="530" width="14" height="14" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="340" y="542">Linux·DT</text>
    <rect x="430" y="530" width="14" height="14" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="450" y="542">제품화</text>
  </g>
</svg>
```
그림 1 — bring-up 파이프라인. 각 단계는 오른쪽 조건을 만족해야 "끝났다"고 말한다. 면접에서 단계 이름만 나열하지 말고 이 exit criteria를 같이 말하면 경험자처럼 들린다.

두 가지 원칙이 전 과정을 관통한다.

- **한 번에 하나만 바꾼다.** DT 노드 하나, 레일 하나, 설정 하나. 두 개를 동시에 바꾸고 고장 나면 어느 쪽 탓인지 모른다. 이 노트의 QEMU 랩(3절)이 정확히 이 방식이다.
- **관측 수단을 먼저 확보한다.** 전류계 → scope → JTAG → UART → earlycon → dmesg 순으로, 다음 단계로 가기 전에 "고장 나면 무엇으로 볼 것인가"를 먼저 정한다. bare-metal에서 LED·GPIO 토글로 살아 있는지 보던 것과 같은 발상이다(M11).

---

## 1. 단계별 상세 — 무엇을 하고, 무엇으로 끝났다고 보고, 고장은 어떻게 보이나

### 1.1 ① 전원 레일과 시퀀싱

**직관.** SoC는 전원 레일이 여러 개(코어, DDR, I/O, always-on 등)이고, **켜지는 순서와 간격**이 데이터시트에 정해져 있다. 순서가 틀리면 I/O 셀이 latch-up 되거나, SoC가 반쯤 리셋된 상태로 멈춘다. 대부분 PMIC가 순서를 책임지고, PMIC의 OTP(한 번 쓰는 설정)나 부팅 시 I2C 설정으로 순서·전압이 정해진다.

**진입 조건**: 보드 육안 검사(부품 방향, 브리지), 레일 간 short 검사(전원 넣기 전에 DMM 저항 측정).

**하는 일**

1. **전류 제한을 건 bench supply**로 처음 전원을 넣는다. 예상 전류(데이터시트 typical + 여유)의 1.5~2배 정도에 current limit을 건다. short가 있으면 supply가 CC 모드로 떨어지고 전압이 주저앉는다 — 보드가 타기 전에 알 수 있다.
2. 레일마다 DMM으로 정적 전압을 재고, scope로 **램프 순서와 간격**을 잰다(트리거는 첫 레일, 채널 4개에 레일 4개).
3. PGOOD(power good) 신호와 리셋 해제 순서를 같이 본다.
4. 대기 전류를 기록한다 — 나중에 "어제보다 30 mA 많다"가 고장 신호가 된다.

**완료 조건**: 모든 레일 공칭값 ±(데이터시트 허용치, 보통 3~5%), 순서·간격이 데이터시트 power-up sequence 표대로, 리플이 허용치 안, 대기 전류가 예상치.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" text-anchor="middle" font-size="14">전원 투입 시퀀스 (예시 모양 — 실제 순서·간격은 SoC 데이터시트)</text>
  <g font-size="12">
    <text x="10" y="56">VDD_SNVS</text><text x="10" y="90">VDD_SOC</text><text x="10" y="124">VDD_DRAM</text>
    <text x="10" y="158">VDD_IO 3.3V</text><text x="10" y="192">PGOOD</text><text x="10" y="226">OSC 24 MHz</text><text x="10" y="260">POR_B (리셋)</text>
  </g>
  <g fill="none" stroke-width="1.8">
    <polyline stroke="#e08a3c" points="130,60 160,60 175,42 660,42"/>
    <polyline stroke="#4a7bd0" points="130,94 230,94 245,76 660,76"/>
    <polyline stroke="#4a7bd0" points="130,128 300,128 315,110 660,110"/>
    <polyline stroke="#4a7bd0" points="130,162 370,162 385,144 660,144"/>
    <polyline stroke="#3f9a6b" points="130,196 440,196 440,178 660,178"/>
    <polyline stroke="#888" points="130,230 320,230 326,214 332,246 338,214 344,246 350,214 356,246 362,214 368,246 374,214 380,246 386,214 392,246 398,214 404,246 410,214 416,246 422,214 428,246 434,214 440,246 446,214 452,246 458,214 464,246 470,214 476,246 482,214 488,246 494,214 500,246 506,214 512,246 518,214 524,246 530,214 536,246 542,214 548,246 554,214 560,246 566,214 572,246 578,214 584,246 590,214 596,246 602,214 608,246 614,214 620,246 626,214 632,246 638,214 644,246 650,214 656,246"/>
    <polyline stroke="#d0564a" points="130,264 520,264 520,246 660,246"/>
  </g>
  <g stroke="currentColor" stroke-dasharray="3,3">
    <line x1="440" y1="36" x2="440" y2="280"/><line x1="520" y1="36" x2="520" y2="280"/>
  </g>
  <g font-size="12">
    <text x="446" y="290">PGOOD</text><text x="526" y="290">리셋 해제(지연 t_rst)</text>
    <text x="176" y="290">레일이 순서대로 램프</text>
  </g>
</svg>
```
그림 2 — 시퀀싱을 scope로 볼 때의 모양. 순서(누가 먼저), 간격(몇 ms 뒤), 램프 기울기(너무 빠르면 inrush), 리셋 해제가 "모든 레일 + 클럭 안정" 뒤인지가 체크 포인트다. 값은 그림용 예시다.

**흔한 고장 증상과 볼 것**

| 증상 | 의심 | 측정 |
|---|---|---|
| supply가 바로 CC 모드, 전압 주저앉음 | 레일 short, 탄탈 역삽입 | 레일별 저항, 열화상 카메라·손가락으로 뜨거운 부품 |
| 전류 0에 가까움 | PMIC가 안 켜짐 (enable 핀, OTP) | PMIC EN·PGOOD 핀, PMIC I2C 응답 |
| 레일 하나가 늦게/안 올라옴 | PMIC 시퀀스 설정, 부하 과다 | scope 4채널, PMIC 레지스터 덤프 |
| 가끔 부팅됨 | 램프 너무 느림/빠름, 리셋 해제가 레일보다 빠름 | POR_B와 마지막 레일의 간격 |

### 1.2 ② 클럭과 크리스털

SoC 내부 PLL은 외부 크리스털/오실레이터(많은 SoC가 24 MHz — 기억 기반, 해당 SoC 데이터시트로 확인)에서 출발한다. 크리스털이 안 돌면 BootROM부터 아무것도 안 돈다.

- **볼 것**: XTAL 핀 진폭과 주파수. 주의 — scope 프로브(약 10 pF)를 크리스털 핀에 대면 부하가 바뀌어 발진이 멈추거나 주파수가 바뀔 수 있다. 가능하면 SoC의 **CLKOUT 핀**(내부 클럭을 밖으로 내보내는 디버그 핀)으로 본다.
- **완료 조건**: 주파수가 ppm 허용치 안, 진폭 정상, 리셋 후 PLL lock 상태 비트(JTAG로 읽기).
- **나중 단계에 숨는 클럭 버그**: UART 기준 클럭이 틀리면 글자가 깨진다(4.4절 실습). RTC 32.768 kHz 크리스털이 안 돌면 부팅은 되는데 시간이 안 간다.

### 1.3 ③ 리셋

전원·클럭이 안정된 뒤 POR_B(파워온 리셋)가 해제되어야 한다. 외부 watchdog, 리셋 버튼, PMIC 리셋 출력이 얽혀 있다.

- **볼 것**: POR_B 파형(한 번 깨끗하게 올라가나, 계속 토글하나 — 토글하면 watchdog나 brown-out). SoC의 **리셋 원인 레지스터**(POR/WDOG/SW 중 무엇으로 리셋됐나)를 JTAG로 읽는다.
- **완료 조건**: 리셋이 한 번 해제되고 다시 안 걸린다. 리셋 원인이 POR.

### 1.4 ④ JTAG attach — 가장 강력한 관측 수단

UART가 아무것도 안 찍을 때 유일하게 "CPU가 지금 어디 있나"를 알려 준다. Don이 매일 쓰던 도구다.

1. 어댑터(J-Link, Lauterbach, OpenOCD + FTDI)로 **IDCODE**를 읽는다. 안 읽히면: JTAG 레벨(1.8 V vs 3.3 V VTref), TRST/SRST 연결, 보드의 JTAG 비활성 fuse·부트 모드.
2. halt → PC 읽기. BootROM 주소 범위면 "ROM이 부트 미디어를 찾으며 돈다", SPL 주소면 "DDR 초기화 중 멈춤" 같은 판단이 바로 된다.
3. 내부 SRAM에 쓰고 읽어 본다. 작은 테스트 코드를 SRAM에 올려 실행해 보기도 한다(DDR 없이도 가능).

- **완료 조건**: IDCODE, halt/resume/step, 메모리 읽기·쓰기가 된다.
- 이 노트의 랩에서는 QEMU의 gdbstub(`-gdb tcp:...`)이 JTAG 역할을 한다(4.2절). 커널 쪽 gdb 사용법 자체는 M4.

### 1.5 ⑤ UART 콘솔과 부트 모드 핀 — "첫 글자"

**UART 콘솔을 다른 무엇보다 먼저 살린다.** 그 다음 단계는 모두 UART로 본다.

- BootROM이 메시지를 내보내는 SoC도 있고(예: 일부 SoC는 ROM 로그를 UART로 낸다) 아무것도 안 내는 SoC도 있다. 첫 글자는 대개 **SPL**이 낸다(C1, C4).
- **부트 모드 핀/fuse**(C2): 부트 미디어(eMMC, SD, SPI NOR, USB 복구)를 고른다. 첫 bring-up은 보통 **USB 복구 모드**(NXP의 Serial Download, TI의 USB DFU 등 — C2)로 SPL을 RAM에 직접 밀어 넣는다. 아직 eMMC에 아무것도 없기 때문이다.
- **완료 조건**: SPL(또는 ROM)의 첫 줄이 깨끗이 찍힌다. 부트 모드 핀을 바꾸면 의도한 미디어로 바뀐다.

UART에서 **깨진 글자**가 나오면 거의 항상 baud 불일치다: 보드 쪽 UART 기준 클럭이 틀렸거나(분주비 계산이 틀림), 호스트 설정이 틀렸거나. **아무 글자도 없으면**: TX/RX 교차 배선, 전압 레벨(1.8 V UART에 3.3 V USB-UART), 핀mux(ROM이 기대하는 UART 인스턴스가 다름), 혹은 그 전 단계 문제. 4.4절에서 baud 불일치를 실제로 시뮬레이션한다.

### 1.6 ⑥ DDR — bring-up에서 가장 오래 걸리는 단계

DDR은 SoC가 혼자 켤 수 없다. SPL(또는 TF-A BL2, 벤더 DDR 로더)이 **DDR 컨트롤러·PHY 설정 + training**을 해야 쓸 수 있다(C4 6절).

- **training**: PHY가 보드 배선 지연에 맞춰 read/write 타이밍(DQS gate, write leveling, read eye)을 스스로 맞추는 과정. 예를 들어 i.MX8M Plus는 U-Boot SPL이 Synopsys PHY training 펌웨어(`lpddr4_pmu_train_1d_dmem_202006.bin` 등)를 불러 쓴다 — 랩의 U-Boot 소스 `doc/board/nxp/imx8mp_evk.rst`에 그 파일 이름이 나온다(확인함). 벤더 DDR 툴(NXP의 DDR config/stress tool 등 — 기억 기반)로 보드별 타이밍 표를 만든다.
- **볼 것**: training 로그(PASS/FAIL, 어느 byte lane), JTAG로 SPL이 어디서 멈췄나, DDR 레일 전압·VREF.
- **완료 조건**: training PASS, 전 용량에 대해 메모리 테스트 0 error, 가능하면 고온/저온에서도. U-Boot `mtest`(랩 U-Boot 소스 `cmd/mem.c`의 `do_mem_mtest`), Linux에서는 `memtester`·`stressapptest` 같은 도구. 5.1절에 작은 메모리 테스트를 직접 만들어 돌린다.
- **증상**: SPL이 "DDR init" 근처에서 멈춤, training fail 메시지, 커널은 뜨는데 무작위 Oops·segfault(DDR 마진 부족의 고전 증상 — 같은 코드가 매번 다른 곳에서 죽는다).

### 1.7 ⑦ 부트로더 체인 — SPL → TF-A → (OP-TEE) → U-Boot

C1·C4·C5에서 자세히 다뤘다. bring-up 관점의 요점만:

- SPL: 콘솔, DDR, 다음 단계 로드. TF-A BL31: EL3 상주, **PSCI**(CPU on/off, 리셋, 전원 끄기)를 제공 — 커널이 secondary CPU를 깨울 때 쓴다(3.4절 실습에서 PSCI가 없으면 CPU1이 안 뜨는 것을 본다). U-Boot: 저장장치·네트워크 드라이버, 커널·DTB·initramfs 로드.
- **완료 조건**: U-Boot 프롬프트, `mmc info`/`ping`/`tftp` 동작, U-Boot이 커널로 점프.
- **U-Boot이 DT를 고친다**는 점을 기억해 둔다: memory 노드 크기, `/chosen/bootargs`, initrd 위치, MAC 주소 등을 점프 직전에 채운다(fdt fixup). QEMU의 `-kernel`도 똑같은 일을 한다 — 3.1절에서 실제로 확인한다.

### 1.8 ⑧ 커널 — earlycon과 최소 DT

**최소 DT부터 시작한다**: cpus, memory, chosen, 인터럽트 컨트롤러, 아키텍처 타이머, UART 하나. 이 여섯 개면 커널이 shell까지 간다. 나머지 노드는 `status = "disabled"`로 두거나 아예 빼고 시작한다.

- **earlycon**: 정식 UART 드라이버가 probe되기 전부터 MMIO 레지스터에 직접 글자를 쓰는 콘솔(C6 5절, M1 5절). 커널 첫 bring-up에서는 항상 켠다. 이 노트 4.2절의 실험 결과 — **`earlycon` 맨이름보다 `earlycon=pl011,0x9000000`처럼 주소를 명시하는 쪽이 더 이른 시점부터 찍힌다** (ACPI SPCR을 지원하는 커널에서 맨이름 `earlycon`은 등록이 `paging_init()` 뒤로 미뤄진다).
- **완료 조건**: shell 프롬프트, `/proc/interrupts`에서 타이머 인터럽트가 증가, `nproc`이 CPU 수와 같음, `MemTotal`이 실제 DDR과 맞음.
- **증상**: "Starting kernel ..." 다음 무출력 → earlycon 명시, DT memory·stdout-path 확인, JTAG로 PC.

### 1.9 ⑨ DT 노드를 하나씩 켠다

순서는 **의존성 순서**다. 다른 노드가 참조하는 공급자(provider)부터:

1. pinctrl (핀mux — 이게 틀리면 모든 버스가 이상하다, H4)
2. clocks, resets, regulators (공급자 — H3)
3. GPIO 컨트롤러
4. I2C/SPI 컨트롤러 → 그 위의 PMIC·센서 (H5, H7)
5. eMMC/SD (rootfs를 옮길 수 있다 — J1)
6. Ethernet (NFS root, 빠른 이미지 전송 — I6)
7. USB
8. display, camera (가장 의존성이 많다 — Q1, Q2)

노드 하나를 켤 때마다: `dmesg`에서 probe 메시지, `/sys/kernel/debug/devices_deferred`(공급자가 아직 없어서 미뤄진 장치 목록), `/sys/bus/*/drivers/*`에 바인딩 됐는지. 3절 랩의 `audit.sh`가 이 확인을 자동화한다.

### 1.10 ⑩ 주변장치 검증

probe 성공 ≠ 동작. 버스마다 기능·성능 테스트를 한다.

| 장치 | 기능 확인 | 성능·신뢰성 |
|---|---|---|
| eMMC | `mmc` 정보, 파티션, 읽기/쓰기·검증 | `fio` 순차/랜덤, HS200/HS400 모드 확인, 전원 차단 시험 |
| Ethernet | link, `ping`, PHY 레지스터(`ethtool`) | `iperf3`, 장시간 패킷 손실, PTP(Q3) |
| USB | enumerate, 속도(HS/SS) | 대용량 전송, hot-plug 반복 |
| I2C/SPI 센서 | `i2cdetect`, ID 레지스터 | 샘플레이트·지터(Q7), 버스 에러 카운터 |
| 카메라 | `media-ctl -p`, 첫 프레임 | 프레임 드롭, 장시간 스트리밍(Q2) |

### 1.11 ⑪ stress와 thermal

- CPU·메모리·I/O를 동시에 오래 돌린다(`stress-ng`, `stressapptest`, `fio`, `iperf3`). 온도별(챔버) 반복.
- thermal zone(`/sys/class/thermal`)과 쓰로틀링 동작, 전력 소모(N5) 기록.
- **완료 조건**: N시간 0 error, 온도·전력이 설계 한계 안, 쓰로틀링이 의도대로.
- DDR 마진 문제는 여기서 자주 드러난다: 상온에선 통과, 고온에서 랜덤 Oops.

### 1.12 ⑫ 공장(factory)

bring-up에서 쓴 테스트가 **생산 테스트**로 이어진다(P6). USB 복구 모드로 빈 eMMC에 이미지 쓰기(C2), MAC 주소·시리얼·키 프로비저닝, 보드 자가진단(레일 전압 ADC, 버스 장치 존재, 메모리 테스트), 결과 로그 수집. bring-up 때 만든 체크 스크립트가 그대로 자가진단 도구의 씨앗이 된다.

---

## 2. 증상 → 원인 — 결정 트리

```svg
<svg viewBox="0 0 680 540" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="240" y="10" width="200" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="30" text-anchor="middle" font-size="13">전원 ON — 무엇이 보이나?</text>
  <line x1="30" y1="40" x2="30" y2="510" stroke="currentColor"/>
  <line x1="240" y1="25" x2="30" y2="25" stroke="currentColor"/>
  <g font-size="12">
    <rect x="45" y="55" width="200" height="50" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
    <text x="55" y="75">전류 과다 / 0 A</text><text x="55" y="93">(bench supply 표시)</text>
    <rect x="265" y="55" width="405" height="50" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
    <text x="275" y="75">레일 short · 역삽입 · PMIC EN/OTP</text><text x="275" y="93">→ 레일별 저항·전압, 열화상, PGOOD</text>
    <rect x="45" y="120" width="200" height="50" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
    <text x="55" y="140">UART 무출력,</text><text x="55" y="158">JTAG도 안 붙음</text>
    <rect x="265" y="120" width="405" height="50" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
    <text x="275" y="140">클럭(OSC)·리셋(POR_B 토글?)·JTAG 레벨</text><text x="275" y="158">→ CLKOUT, POR_B 파형, VTref</text>
    <rect x="45" y="185" width="200" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="55" y="205">UART 무출력,</text><text x="55" y="223">JTAG는 붙음</text>
    <rect x="265" y="185" width="405" height="50" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
    <text x="275" y="205">PC가 ROM? → 부트 모드 핀·미디어 / SPL? → DDR</text><text x="275" y="223">UART 핀mux·TX/RX 교차·전압 레벨</text>
    <rect x="45" y="250" width="200" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="55" y="270">깨진 글자</text><text x="55" y="288">(같은 쓰레기 반복)</text>
    <rect x="265" y="250" width="405" height="50" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
    <text x="275" y="270">baud 불일치 = UART 기준 클럭 설정 오류</text><text x="275" y="288">→ 분주 레지스터 읽기, scope로 비트 폭 재기</text>
    <rect x="45" y="315" width="200" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="55" y="335">SPL에서 멈춤 /</text><text x="55" y="353">무작위 Oops</text>
    <rect x="265" y="315" width="405" height="50" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
    <text x="275" y="335">DDR training·타이밍 표·VREF·온도 마진</text><text x="275" y="353">→ training 로그, memtest, 온도 바꿔 반복</text>
    <rect x="45" y="380" width="200" height="50" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
    <text x="55" y="400">Starting kernel</text><text x="55" y="418">다음 무출력</text>
    <rect x="265" y="380" width="405" height="50" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
    <text x="275" y="400">earlycon=드라이버,주소 명시 → memory reg·stdout-path</text><text x="275" y="418">→ JTAG로 PC·ESR·FAR, gdb로 로그 버퍼 읽기</text>
    <rect x="45" y="445" width="200" height="50" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
    <text x="55" y="465">커널 로그 나오다</text><text x="55" y="483">멈춤 / probe 실패</text>
    <rect x="265" y="445" width="405" height="50" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
    <text x="275" y="465">타이머·interrupt-parent·공급자 노드(clk/reg/pinctrl)</text><text x="275" y="483">→ /proc/interrupts, devices_deferred, dmesg</text>
  </g>
  <g stroke="currentColor" stroke-width="1.2">
    <line x1="30" y1="80" x2="43" y2="80" marker-end="url(#ah2)"/><line x1="30" y1="145" x2="43" y2="145" marker-end="url(#ah2)"/>
    <line x1="30" y1="210" x2="43" y2="210" marker-end="url(#ah2)"/><line x1="30" y1="275" x2="43" y2="275" marker-end="url(#ah2)"/>
    <line x1="30" y1="340" x2="43" y2="340" marker-end="url(#ah2)"/><line x1="30" y1="405" x2="43" y2="405" marker-end="url(#ah2)"/>
    <line x1="30" y1="470" x2="43" y2="470" marker-end="url(#ah2)"/>
    <line x1="245" y1="80" x2="263" y2="80" marker-end="url(#ah2)"/><line x1="245" y1="145" x2="263" y2="145" marker-end="url(#ah2)"/>
    <line x1="245" y1="210" x2="263" y2="210" marker-end="url(#ah2)"/><line x1="245" y1="275" x2="263" y2="275" marker-end="url(#ah2)"/>
    <line x1="245" y1="340" x2="263" y2="340" marker-end="url(#ah2)"/><line x1="245" y1="405" x2="263" y2="405" marker-end="url(#ah2)"/>
    <line x1="245" y1="470" x2="263" y2="470" marker-end="url(#ah2)"/>
  </g>
  <text x="340" y="528" text-anchor="middle" font-size="12">위에서 아래로 = 층 아래에서 위로. 위 칸을 통과하지 못했으면 아래 칸을 의심하지 않는다.</text>
</svg>
```
그림 3 — 증상에서 원인으로 가는 결정 트리. 초록 두 칸(커널)은 4절 QEMU 랩에서 실제로 재현한다. 주황·파랑 칸은 실보드에서만 생긴다(QEMU에는 전원·DDR 물리가 없다).

---

## 3. 랩 — QEMU virt에서 "점진적 bring-up"을 흉내 낸다

### 3.0 랩 구성과 한계

- VM 호스트 커널: `7.0.0-38-generic` (Lima `elinux`, `uname -r` 확인). 게스트 커널: `/var/lab/out/Image-dom` (6.18.55-dom), QEMU 10.2.1, `-machine virt -cpu cortex-a57 -smp 2 -m 512M`.
- 1~2단계(전원~JTAG)와 DDR training은 QEMU에 없다. QEMU가 흉내 낼 수 있는 것은 **⑧~⑪ — "펌웨어는 됐고, 이제 커널과 DT를 올린다"** 구간이다. 이 구간이 Linux 엔지니어가 가장 많이 시간을 쓰는 곳이기도 하다.
- 이 VM은 19명이 함께 쓰는 TCG 에뮬레이션이라 dmesg 타임스탬프(같은 단계가 1초~20초)는 실HW 숫자가 아니다. 숫자는 비교용으로도 쓰지 않는다.

### 3.1 먼저 발견한 함정 — QEMU `-kernel`은 DT를 "고쳐서" 넘긴다

계획은 "메모리 크기를 일부러 틀린 DT로 부팅해 증상을 본다"였다. 그런데 `-kernel ... -dtb 내.dtb`로 부팅하면 증상이 안 나온다. 확인해 보자.

**예제 1 — `-kernel` 경로에서 게스트가 실제로 받은 DT 보기.** memory를 2 GiB로 적고 psci 노드는 없는 DT(`w2_mem.dts`, 아래 4.2절)를 넘긴 뒤, 게스트 안에서 `/proc/device-tree`를 본다.

```sh
dtc -q -I dts -O dtb -o w2k.dtb ~/.elinux_scratch/P1/w2_mem.dts
timeout 90 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 512M -nographic \
  -kernel /var/lab/out/Image-dom -initrd /var/lab/out/initramfs.cpio.gz -dtb w2k.dtb \
  -append "console=ttyAMA0 LAB_CMD=ls~/proc/device-tree/psci;od~-An~-tx4~/proc/device-tree/memory@40000000/reg;nproc"
```

```text
lab initramfs: 6.18.55-dom up 53.53s
compatible   cpu_on       method       name
cpu_off      cpu_suspend  migrate
 00000000 00000040 00000000 00000020
2
```

출력에서 볼 것: 내 DT에는 **없던 `psci` 노드**가 생겼고, 2 GiB라고 쓴 memory `reg`가 `0x40000000`부터 `0x20000000`(512 MiB)로 **바뀌어 있다**(`od -tx4`는 little-endian으로 4바이트씩 묶어 보여 주므로 big-endian DT 값 `40 00 00 00`이 `00000040`으로 보인다). CPU도 2개 다 떴다.

즉 QEMU의 `-kernel` 경로는 작은 부트로더처럼 동작하면서 **memory 노드를 `-m` 값으로 덮어쓰고 PSCI 노드를 넣고 `/chosen`에 bootargs·initrd를 채운다**. 실보드의 U-Boot(memory fixup), TF-A(PSCI 제공)가 하는 일과 같다. 그래서 "DT를 틀리게 써도 부트로더가 고쳐 줘서 문제가 숨는" 일이 실보드에서도 생긴다 — 그리고 반대로, 부트로더를 바꾸자 갑자기 터지는 일도.

### 3.2 그래서 직접 쓴 40바이트 부트로더

DT를 손대지 않는 부트로더가 필요하다. arm64 부트 프로토콜(C1 4.1, C6 2절)은 간단하다: `x0` = DTB 물리 주소, `x1`~`x3` = 0, MMU off 상태로 Image 시작 주소로 점프.

**예제 2 — 부트 스텁과 실행 스크립트.** QEMU의 `-device loader`로 스텁·Image·DTB·initramfs를 각각 RAM에 올리고 CPU0을 스텁 주소에서 출발시킨다. `-kernel`을 안 쓰므로 QEMU는 DT를 고치지 않는다.

```c
/* stub.S — p1 boot stub: the smallest "bootloader", no DT fixups.
 * arm64 boot protocol: x0 = DTB phys addr, x1..x3 = 0, jump to Image. */
	.global _start
_start:
	ldr	x0, dtb_addr
	mov	x1, xzr
	mov	x2, xzr
	mov	x3, xzr
	ldr	x4, kernel_addr
	br	x4
	.balign 8
dtb_addr:	.quad 0x48000000
kernel_addr:	.quad 0x40200000
```

```sh
#!/bin/bash
# boot.sh <name> <stage-dts> "<bootargs>" [extra qemu args...]
# stub@0x40100000, Image@0x40200000, DTB@0x48000000, initrd@0x48100000
set -u
name=$1; dts=$2; app=$3; shift 3
S=/Users/donh/.elinux_scratch/P1; W=/var/tmp/P1; mkdir -p $W/logs
K=${K:-/var/lab/out/Image-dom}; INITRD=${INITRD:-/var/lab/out/initramfs.cpio.gz}
sz=$(stat -c %s $INITRD); end=$(printf '0x%x' $((0x48100000 + sz)))
cat > $W/run_$name.dts <<EOT
/include/ "$S/$dts.dts"
/ { chosen { bootargs = "$app"; linux,initrd-start = <0x0 0x48100000>; linux,initrd-end = <0x0 $end>; }; };
EOT
dtc -q -I dts -O dtb -o $W/run_$name.dtb $W/run_$name.dts || exit 1
timeout ${TMO:-120} qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m ${MEM:-512M} -nographic \
  -device loader,file=$W/stub.bin,addr=0x40100000,cpu-num=0 \
  -device loader,file=$K,addr=0x40200000,force-raw=on \
  -device loader,file=$W/run_$name.dtb,addr=0x48000000,force-raw=on \
  -device loader,file=$INITRD,addr=0x48100000,force-raw=on "$@" > $W/logs/$name.log 2>&1
echo "[exit=$? lines=$(wc -l < $W/logs/$name.log)]" >> $W/logs/$name.log
```

```sh
gcc -nostdlib -static -Wl,-Ttext=0x40000000 -o stub.elf stub.S
objcopy -O binary -j .text stub.elf stub.bin && xxd stub.bin
```

```text
00000000: c000 0058 e103 1faa e203 1faa e303 1faa  ...X............
00000010: 8400 0058 8000 1fd6 0000 0048 0000 0000  ...X.......H....
00000020: 0000 2040 0000 0000                      .. @....
```

출력에서 볼 것: 명령 6개(24바이트) + 주소 상수 2개(16바이트) = 40바이트. `ldr x0, dtb_addr`은 PC 상대 주소라 어디에 올려도 된다. 처음엔 스텁을 `0x40000000`에 올렸다가 QEMU가 "Some ROM regions are overlapping … dtb (0x40000000 - 0x40100000)"로 거절했다 — `-kernel` 없이도 QEMU virt는 자기 DTB를 RAM 맨 앞에 둔다. 그래서 `0x40100000`으로 옮겼다. **부트로더가 하는 일 = 이미지 배치 + DT의 `/chosen` 채우기 + 레지스터 계약** 이라는 것을 이 스크립트가 그대로 보여 준다(`run_<name>.dts`가 `/chosen`에 bootargs·initrd 주소를 넣는다).

### 3.3 Stage 0 — 최소 DT: cpus · memory · chosen · GIC · timer · UART

**예제 3 — `s0.dts`.** 실제 virt DT(`dumpdtb`로 뽑은 406줄)에서 6가지만 남겼다. UART는 primecell(AMBA) 장치라 `apb_pclk` 클럭이 있어야 probe되므로 fixed-clock 하나를 UART의 일부로 같이 둔다.

```dts
/dts-v1/;
/ {
	model = "p1,bringup-stage0";
	compatible = "linux,dummy-virt";
	#address-cells = <2>;
	#size-cells = <2>;
	interrupt-parent = <&gic>;

	cpus {
		#address-cells = <1>;
		#size-cells = <0>;
		cpu0: cpu@0 { device_type = "cpu"; compatible = "arm,cortex-a57"; reg = <0>; enable-method = "psci"; };
		cpu1: cpu@1 { device_type = "cpu"; compatible = "arm,cortex-a57"; reg = <1>; enable-method = "psci"; };
	};

	memory@40000000 {
		device_type = "memory";
		reg = <0x0 0x40000000 0x0 0x20000000>;	/* 512 MiB */
	};

	gic: intc@8000000 {
		compatible = "arm,cortex-a15-gic";
		#interrupt-cells = <3>;
		interrupt-controller;
		reg = <0x0 0x8000000 0x0 0x10000>, <0x0 0x8010000 0x0 0x10000>;
	};

	timer {
		compatible = "arm,armv8-timer";
		interrupts = <1 13 0x304>, <1 14 0x304>, <1 11 0x304>, <1 10 0x304>;
		always-on;
	};

	apb_pclk: apb-pclk {
		compatible = "fixed-clock";
		#clock-cells = <0>;
		clock-frequency = <24000000>;
		clock-output-names = "clk24mhz";
	};

	uart0: pl011@9000000 {
		compatible = "arm,pl011", "arm,primecell";
		reg = <0x0 0x9000000 0x0 0x1000>;
		interrupts = <0 1 4>;
		clocks = <&apb_pclk>, <&apb_pclk>;
		clock-names = "uartclk", "apb_pclk";
	};

	aliases { serial0 = &uart0; };
	chosen { stdout-path = "serial0:115200n8"; };
};
```

다음 단계 파일은 앞 단계를 `/include/`하고 노드만 더한다(`s1.dts`는 `/include/ "s0.dts"` + psci, `s2.dts`는 `/include/ "s1.dts"` + RTC·GPIO …). **"한 번에 하나만 더한다"를 파일 구조로 강제**하는 방법이다. 실무에서는 같은 일을 `status = "okay"`를 하나씩 바꾸는 커밋으로 한다.

각 단계 부팅 후 실행하는 점검 스크립트(initramfs에 넣었다):

**예제 4 — `audit.sh`: DT가 약속한 것 vs 실제로 probe된 것.**

```sh
#!/bin/sh
# p1 bring-up audit: what did the DT promise, what actually probed?
echo "model : $(cat /proc/device-tree/model)"
echo "cpus  : $(nproc) online, mem $(awk '/MemTotal/{print $2" kB"}' /proc/meminfo)"
echo "--- DT nodes with compatible (top level) -> bound driver"
for n in /sys/firmware/devicetree/base/*/; do
  [ -f "$n/compatible" ] || continue
  node=$(basename $n); drv="- (no struct device: early OF_DECLARE init)"; nod=""
  for d in /sys/bus/*/devices/*; do
    [ "$(readlink -f $d/of_node 2>/dev/null)" = "$(readlink -f $n)" ] || continue
    x=$(basename "$(readlink $d/driver 2>/dev/null)")
    if [ -n "$x" ]; then drv="$x ($(basename $(dirname $(dirname $d))))"; nod=ok; break; fi
    drv="NO DRIVER ($(basename $d))"
  done
  printf "  %-24s %s\n" "$node" "$drv"
done
echo "--- deferred : $(cat /sys/kernel/debug/devices_deferred 2>/dev/null | wc -l)"
cat /sys/kernel/debug/devices_deferred 2>/dev/null
echo "--- probe errors in dmesg:"; dmesg | grep -iE "probe.*failed|failed to|error -" | head -5
echo "--- block/net/pci:"; ls /sys/class/block /sys/class/net 2>/dev/null | tr '\n' ' '; echo
ls /sys/bus/pci/devices 2>/dev/null | tr '\n' ' '; echo
```

핵심 아이디어: `/sys/bus/<버스>/devices/<장치>/of_node` 링크가 DT 노드를 가리키고, `driver` 링크가 붙은 드라이버를 가리킨다. 이 둘을 엮으면 "DT 노드 → 장치 → 드라이버" 표가 나온다(G3, D3).

**예제 5 — Stage 0 부팅.**

```sh
B=~/.elinux_scratch/P1/boot.sh; export INITRD=/var/tmp/P1/p1_initrd.gz
$B s0 s0 "console=ttyAMA0 earlycon LAB_CMD=/p1/audit.sh;grep~-v~IPI~/proc/interrupts"
```

```text
[    0.000000][    T0] Booting Linux on physical CPU 0x0000000000 [0x411fd070]
[    0.000000][    T0] Linux version 6.18.55-dom (donh@lima-elinux) (gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0, GNU ld (GNU Binutils for Ubuntu) 2.46) #1 SMP PREEMPT Sun Oct 18 09:15:03 PDT 2026
[    0.000000][    T0] KASLR disabled due to lack of seed
[    0.000000][    T0] Machine model: p1,bringup-stage0
[    0.000000][    T0] efi: UEFI not found.
[    0.000000][    T0] earlycon: pl11 at MMIO 0x0000000009000000 (options '115200n8')
[    0.000000][    T0] printk: legacy bootconsole [pl11] enabled
...
[    0.946762][    T1] psci: no cpu_on method, not booting CPU1
[    1.133014][    T1] smp: Bringing up secondary CPUs ...
[    1.142320][    T1] smp: Brought up 1 node, 1 CPU
...
lab initramfs: 6.18.55-dom up 12.72s
model : p1,bringup-stage0
cpus  : 1 online, mem 459972 kB
--- DT nodes with compatible (top level) -> bound driver
  apb-pclk                 - (no struct device: early OF_DECLARE init)
  intc@8000000             - (no struct device: early OF_DECLARE init)
  pl011@9000000            uart-pl011 (amba)
  timer                    NO DRIVER (timer)
--- deferred : 0
--- probe errors in dmesg:
--- block/net/pci:
/sys/class/block: loop0 loop1 loop2 loop3 loop4 loop5 loop6 loop7  /sys/class/net: lo 

           CPU0       
 11:       5826 GIC-0  27 Level     arch_timer
 13:          0 GIC-0  33 Level     uart-pl011
Err:          0
```

출력에서 볼 것(로그 247줄 중 일부, 마지막 줄 뒤 QEMU는 timeout으로 종료):

- **shell까지 갔다.** 최소 DT 여섯 개면 충분하다는 증거. 타이머 인터럽트(`arch_timer`, GIC PPI 27)가 5826번 — 시간이 흐른다.
- **CPU가 1개뿐이다.** `psci: no cpu_on method, not booting CPU1`. cpu 노드에 `enable-method = "psci"`라고 썼지만 PSCI 노드가 없으니 CPU1을 깨울 방법이 없다. 그리고 `poweroff`도 안 된다("Power off not available" — 전원 끄기도 PSCI 호출이다).
- `timer` 노드는 platform device가 만들어졌지만 드라이버가 없다(NO DRIVER). 정상이다 — 아키텍처 타이머는 `TIMER_OF_DECLARE`로 부팅 초기에 이미 초기화됐다. GIC·fixed-clock은 초기화 코드가 노드에 `OF_POPULATED` 표시를 해서 device 자체가 안 만들어진다(6절).
- `earlycon:` 줄이 `efi:` 줄 **다음**에 나왔다는 점을 기억해 두자. 4.2절에서 중요해진다.

### 3.4 Stage 1 — PSCI 추가 → CPU1이 뜬다

```dts
/include/ "s0.dts"
/ {
	psci {
		compatible = "arm,psci-1.0", "arm,psci-0.2", "arm,psci";
		method = "hvc";
	};
};
```

**예제 6 — Stage 1 부팅** (같은 명령, `s1`):

```text
[    0.000000][    T0] psci: probing for conduit method from DT.
[    0.000000][    T0] psci: PSCIv1.1 detected in firmware.
[    0.000000][    T0] psci: Using standard PSCI v0.2 function IDs
[    0.000000][    T0] psci: Trusted OS migration not required
[    0.000000][    T0] psci: SMC Calling Convention v1.0
[    0.517672][    T1] smp: Bringing up secondary CPUs ...
[    1.287499][    T1] smp: Brought up 1 node, 2 CPUs
...
cpus  : 2 online, mem 459972 kB
  psci                     psci-cpuidle-domain (platform)
           CPU0       CPU1       
 11:       7263       7083 GIC-0  27 Level     arch_timer
```

출력에서 볼 것: "firmware"가 PSCI 1.1을 응답했다 — 여기서 firmware는 QEMU 자신이다(`method = "hvc"`로 부르면 QEMU가 받는다). 실보드에서는 TF-A BL31이 이 응답을 한다(C5). 실보드 bring-up에서 "CPU가 1개만 뜬다"면 첫 의심은 PSCI 노드·`method`(smc/hvc)·TF-A 버전이다.

### 3.5 Stage 2 — 저속 SoC 주변장치: RTC, GPIO 컨트롤러, GPIO 소비자

```dts
/include/ "s1.dts"
/ {
	rtc@9010000 {
		compatible = "arm,pl031", "arm,primecell";
		reg = <0x0 0x9010000 0x0 0x1000>;
		interrupts = <0 2 4>;
		clocks = <&apb_pclk>;
		clock-names = "apb_pclk";
	};
	gpio0: gpio@9030000 {
		compatible = "arm,pl061", "arm,primecell";
		reg = <0x0 0x9030000 0x0 0x1000>;
		interrupts = <0 7 4>;
		gpio-controller;
		#gpio-cells = <2>;
		clocks = <&apb_pclk>;
		clock-names = "apb_pclk";
	};
	gpio-keys {
		compatible = "gpio-keys";
		poweroff {
			label = "GPIO Key Poweroff";
			gpios = <&gpio0 3 0>;
			linux,code = <116>;	/* KEY_POWER */
		};
	};
};
```

**예제 7 — Stage 2 부팅.**

```text
[    7.952310][    T1] pl061_gpio 9030000.gpio: PL061 GPIO chip registered
[    8.625597][    T1] rtc-pl031 9010000.rtc: registered as rtc0
[    8.626637][    T1] rtc-pl031 9010000.rtc: setting system clock to 2026-10-18T16:20:58 UTC (1792340458)
[    9.886264][    T1] input: gpio-keys as /devices/platform/gpio-keys/input/input0
...
  gpio-keys                gpio-keys (platform)
  gpio@9030000             pl061_gpio (amba)
  rtc@9010000              rtc-pl031 (amba)
--- deferred : 0
 15:          0          0 GIC-0  34 Level     rtc-pl031
 16:          0          0 9030000.gpio   3 Edge      GPIO Key Poweroff
```

출력에서 볼 것: `gpio-keys`(소비자)가 `gpio0`(공급자)를 phandle로 참조한다. 마지막 줄의 인터럽트 컨트롤러 이름이 `GIC-0`이 아니라 `9030000.gpio`다 — **GPIO 컨트롤러가 2차 인터럽트 컨트롤러**가 되어 핀 3번을 IRQ로 내보낸다(H6의 계층 irqdomain). 소비자 노드를 공급자보다 먼저 켰다면 `devices_deferred`에 gpio-keys가 올라왔을 것이다. 의존성 순서로 켜는 이유다.

### 3.6 Stage 3 — virtio-mmio (eMMC·Ethernet 블록의 대역)

실 SoC의 eMMC 컨트롤러·Ethernet MAC은 "고정 주소의 MMIO 블록 + IRQ"다. QEMU에서 같은 모양은 virtio-mmio 전송 슬롯이다. 슬롯 4개만 DT에 넣고, QEMU에 `-device virtio-blk-device`와 `-device virtio-net-device`를 붙였다.

여기서 실제로 한 번 틀렸다. 처음에는 슬롯 0~3(`0xa000000`~`0xa000600`)을 넣었다.

**예제 8 — 슬롯 0~3만 기술한 DT(`s3low`).**

```text
  virtio_mmio@a000000      NO DRIVER (a000000.virtio_mmio)
  virtio_mmio@a000200      NO DRIVER (a000200.virtio_mmio)
  virtio_mmio@a000400      NO DRIVER (a000400.virtio_mmio)
  virtio_mmio@a000600      NO DRIVER (a000600.virtio_mmio)
--- deferred : 0
--- probe errors in dmesg:
--- block/net/pci:
/sys/class/block: loop0 loop1 loop2 loop3 loop4 loop5 loop6 loop7  /sys/class/net: lo 
```

출력에서 볼 것: 에러 메시지가 **하나도 없는데** 디스크(`vda`)도 `eth0`도 없다. QEMU virt는 virtio-mmio 장치를 **가장 높은 슬롯(31번, `0xa003e00`)부터** 채운다. 내가 기술한 0~3번 슬롯은 빈 슬롯(device ID 0)이고, 커널 `virtio_mmio_probe()`는 ID 0을 보면 "dummy placeholder"로 보고 **에러 없이** `-ENODEV`를 돌려준다(`drivers/virtio/virtio_mmio.c` 621행 주석 "End probing now with no error reported"). 실보드로 바꿔 말하면: **DT의 `reg` 주소가 회로상 실제 장치와 다른 인스턴스를 가리키면, 드라이버는 조용히 아무것도 못 찾는다.** 예를 들어 SoC에 UART가 4개인데 보드는 UART3을 썼고 DT는 UART1을 켰을 때.

**예제 9 — 슬롯 28~31로 고친 `s3`.**

```text
[   15.374201][    T1] virtio_blk virtio1: 1/0/0 default/read/poll queues
[   15.390791][    T1] virtio_blk virtio1: [vda] 32768 512-byte logical blocks (16.8 MB/16.0 MiB)
...
  virtio_mmio@a003800      NO DRIVER (a003800.virtio_mmio)
  virtio_mmio@a003a00      NO DRIVER (a003a00.virtio_mmio)
  virtio_mmio@a003c00      virtio-mmio (platform)
  virtio_mmio@a003e00      virtio-mmio (platform)
/sys/class/block: loop0 loop1 loop2 loop3 loop4 loop5 loop6 loop7 vda  /sys/class/net: eth0 lo 
 15:          1          0 GIC-0  79 Edge      virtio1
 16:          0          0 GIC-0  78 Edge      virtio0
```

출력에서 볼 것: 위 두 슬롯(30, 31)만 장치가 있어 바인딩됐다. GIC 번호 78·79 = SPI 46·47 + 32 — DT의 `interrupts = <0 46 1>` 셀 값에 32를 더한 GIC 하드웨어 번호다(D4 4.2의 "+32 함정").

### 3.7 Stage 4 — PCIe 호스트 브리지 + MSI

PCIe는 **호스트 브리지만 DT에 쓰고, 그 뒤의 장치는 열거(enumeration)로 발견**한다(D1: discoverable bus). 노드는 `pci-host-ecam-generic` + GICv2m MSI 프레임(GIC 노드의 자식). DT 원문은 실제 virt DT의 `pcie@10000000`과 같은 `ranges`·`interrupt-map`을 썼다(길어서 생략 — `dumpdtb` 결과에서 그대로 옮기면 된다). QEMU에는 `virtio-blk-pci`·`virtio-net-pci`를 붙였다.

**예제 10 — Stage 4 부팅.**

```text
[    0.000000][    T0] GICv2m: range[mem 0x08020000-0x08020fff], SPI[80:143]
[   11.191893][    T1] pci-host-generic 4010000000.pcie: host bridge /pcie@10000000 ranges:
[   11.194318][    T1] pci-host-generic 4010000000.pcie:       IO 0x003eff0000..0x003effffff -> 0x0000000000
[   11.198186][    T1] pci-host-generic 4010000000.pcie:      MEM 0x0010000000..0x003efeffff -> 0x0010000000
[   11.198353][    T1] pci-host-generic 4010000000.pcie:      MEM 0x8000000000..0xffffffffff -> 0x8000000000
[   11.205698][    T1] pci-host-generic 4010000000.pcie: ECAM at [mem 0x4010000000-0x401fffffff] for [bus 00-ff]
[   11.219924][    T1] pci 0000:00:00.0: [1b36:0008] type 00 class 0x060000 conventional PCI endpoint
[   11.240156][    T1] pci 0000:00:01.0: [1af4:1001] type 00 class 0x010000 conventional PCI endpoint
[   11.244894][    T1] pci 0000:00:02.0: [1af4:1000] type 00 class 0x020000 conventional PCI endpoint
...
[   11.252605][    T1] pci 0000:00:01.0: BAR 4 [mem 0x8000000000-0x8000003fff 64bit pref]: assigned
[   11.742204][    T1] virtio_blk virtio0: [vda] 32768 512-byte logical blocks (16.8 MB/16.0 MiB)
...
  pcie@10000000            pci-host-generic (platform)
0000:00:00.0 0000:00:01.0 0000:00:02.0 
 18:          1          0 GICv2m-PCI-MSIX-0000:00:01.0   1 Edge      virtio0-req.0
 21:          0          0 GICv2m-PCI-MSIX-0000:00:02.0   1 Edge      virtio1-input.0
```

출력에서 볼 것: DT에는 `pcie@10000000` 노드 **하나**뿐인데 PCI 장치 3개(호스트 브리지 `1b36:0008`, virtio-blk `1af4:1001`, virtio-net `1af4:1000`)가 나왔다. BAR 주소는 DT `ranges`의 창 안에서 커널이 배정했다. 인터럽트는 MSI-X로 GICv2m을 통해 온다(H11). Don의 Apple 경험(PCIe link·BAR·MSI 디버그)이 바로 쓰이는 곳: **link가 안 올라오면 열거 단계에서 장치가 0개**이고, `ranges`가 틀리면 BAR 배정 실패 메시지가 나온다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" text-anchor="middle" font-size="14">단계별 측정 (이 랩 실측, Image-dom)</text>
  <text x="170" y="44" text-anchor="middle" font-size="12">드라이버가 붙은 DT 최상위 노드 수</text>
  <text x="510" y="44" text-anchor="middle" font-size="12">커널 로그 줄 수</text>
  <g stroke="currentColor"><line x1="40" y1="250" x2="310" y2="250"/><line x1="40" y1="60" x2="40" y2="250"/>
  <line x1="380" y1="250" x2="660" y2="250"/><line x1="380" y1="60" x2="380" y2="250"/></g>
  <g fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0">
    <rect x="55" y="227" width="36" height="23"/><rect x="105" y="204" width="36" height="46"/>
    <rect x="155" y="135" width="36" height="115"/><rect x="205" y="89" width="36" height="161"/>
    <rect x="255" y="112" width="36" height="138"/>
  </g>
  <g fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b">
    <rect x="395" y="149" width="36" height="101"/><rect x="445" y="146" width="36" height="104"/>
    <rect x="495" y="145" width="36" height="105"/><rect x="545" y="144" width="36" height="106"/>
    <rect x="595" y="130" width="36" height="120"/>
  </g>
  <g font-size="12" text-anchor="middle">
    <text x="73" y="221">1</text><text x="123" y="198">2</text><text x="173" y="129">5</text><text x="223" y="83">7</text><text x="273" y="106">6</text>
    <text x="413" y="143">247</text><text x="463" y="140">254</text><text x="513" y="139">258</text><text x="563" y="138">260</text><text x="613" y="124">294</text>
    <text x="73" y="268">s0</text><text x="123" y="268">s1</text><text x="173" y="268">s2</text><text x="223" y="268">s3</text><text x="273" y="268">s4</text>
    <text x="413" y="268">s0</text><text x="463" y="268">s1</text><text x="513" y="268">s2</text><text x="563" y="268">s3</text><text x="613" y="268">s4</text>
  </g>
  <text x="340" y="292" text-anchor="middle" font-size="12">s4 감소: virtio 장치가 mmio 슬롯에서 PCI 뒤로 이동 (PCI 장치는 DT 노드 아님)</text>
</svg>
```
그림 4 — 단계마다 `audit.sh`가 센 "드라이버가 붙은 노드"(왼쪽, 막대 높이 = 값 × 23)와 커널 로그 줄 수(오른쪽, 축은 0부터가 아니라 대략 비례). bring-up 진척을 이렇게 숫자로 기록해 두면 "어느 커밋에서 무엇이 빠졌나"가 한눈에 보인다.

---

## 4. 일부러 틀린 단계 — 4가지 실수와 그 증상

모두 Stage 0(`s0.dts`)에 한 줄만 바꾼 DT다. dtc의 `/delete-property/`·`/delete-node/`로 "원본은 그대로, 차이만" 기술했다.

### 4.1 W1 — 루트 `interrupt-parent` 누락

```dts
/include/ "s0.dts"
/* WRONG 1: root interrupt-parent forgotten */
/ { /delete-property/ interrupt-parent; };
```

**예제 11 — dtc 경고와 부팅 로그.**

```text
s0.dts:39.8-43.4: Warning (interrupts_property): /timer: Missing interrupt-parent
s0.dts:52.23-58.4: Warning (interrupts_property): /pl011@9000000: Missing interrupt-parent
```

```text
[    0.000000][    T0] NR_IRQS: 64, nr_irqs: 64, preallocated irqs: 0
[    0.000000][    T0] Root IRQ handler: gic_handle_irq
[    0.000000][    T0] clocksource: jiffies: mask: 0xffffffff max_cycles: 0xffffffff, max_idle_ns: 7645041785100000 ns
[    0.000000][    T0] arch_timer: No interrupt available, giving up
[    0.000000][    T0] Failed to initialize '/timer': -22
[    0.000000][    T0] sched_clock: 64 bits at 250 Hz, resolution 4000000ns, wraps every 9007199254000000ns
...
[    0.000000][    T1] 9000000.pl011: ttyAMA0 at MMIO 0x9000000 (irq = 0, base_baud = 0) is a PL011 rev1
[    0.000000][    T1] printk: legacy bootconsole [pl11] disabled
[    0.000000][    T1] printk: legacy bootconsole [pl11] disabled
qemu-system-aarch64: terminating on signal 15 from pid 2425970 (timeout)
```

출력에서 볼 것:

- **dtc가 이미 경고했다.** bring-up 중 DT 빌드 경고를 무시하지 않는 습관(D7의 `dtbs_check`까지).
- GIC 자체는 떴지만(`Root IRQ handler`), 타이머와 UART가 "자기 인터럽트가 어느 컨트롤러 것인지" 모른다. `arch_timer: No interrupt available` → 타임스탬프가 끝까지 **`0.000000`** 이다. 시간이 안 간다. UART는 `irq = 0`.
- 로그는 콘솔 인계(`bootconsole disabled`)에서 끝나고, shell 출력(`lab initramfs:` 줄)은 끝내 안 나온다.

**예제 12 — "JTAG"로 CPU가 무엇을 하는지 보기.** QEMU를 `-gdb tcp:127.0.0.1:40602`로 띄우고 45초 뒤 3초 간격으로 세 번 붙어 레지스터를 읽었다(Image-dom은 디버그 정보가 없어 심볼 이름만 나온다).

```sh
$B w1g w1_noip "console=ttyAMA0 earlycon LAB_CMD=nproc" -gdb tcp:127.0.0.1:40602 &
sleep 45; for i in 1 2 3; do
  gdb -q -batch -ex "set debuginfod enabled off" -ex "target remote 127.0.0.1:40602" -ex 'p $pc' \
      -ex 'p/x $ELR_EL1' -ex 'p/x $SPSR_EL1' -ex 'p/x $ESR_EL1' -ex detach /var/lab/build/linux-dom/vmlinux
  sleep 3; done
```

```text
0xffff80008027d90c in trace_hardirqs_on_prepare ()
$2 = 0x4375c0
$3 = 0x20000000
$4 = 0x56000000

0x00000000004375c0 in ?? ()
$2 = 0x4375c0
$3 = 0x20000000
$4 = 0x56000000

0xffff800080011400 in vectors ()
$2 = 0x4375c0
$3 = 0x20000000
$4 = 0x56000000
```

출력에서 볼 것: 커널이 죽은 게 아니다. `ESR_EL1 = 0x56000000`은 EC = 0x15(AArch64 SVC = syscall), `SPSR_EL1 = 0x20000000`은 EL0 — **유저 공간이 같은 주소(`0x4375c0`)에서 syscall을 계속 부르고 있다.** BusyBox 비스트립 바이너리로 찾으면:

```sh
addr2line -f -e /var/lab/build/busybox/busybox_unstripped 0x4375c0
```

```text
__clock_gettime
??:?
```

`clock_gettime()` 반복 — 시간이 흐르기를 기다리는 루프인데, 타이머 인터럽트가 없어 시간이 영원히 안 간다. 실보드에서 "로그는 나오다 멈췄는데 보드는 뜨겁고 CPU는 돌고 있다"의 전형이다. **JTAG로 PC 몇 번 샘플링** 하는 것만으로 "커널 크래시"와 "시간이 안 가서 생긴 무한 대기"가 구분된다.

### 4.2 W2 — memory `reg`가 실제 DDR보다 크다

```dts
/include/ "s0.dts"
/* WRONG 2: memory says 2 GiB, board (QEMU -m 512M) has 512 MiB */
/ { memory@40000000 { reg = <0x0 0x40000000 0x0 0x80000000>; }; };
```

실보드로는 "DDR 부품이 1 GiB짜리로 바뀌었는데 DT는 2 GiB 그대로" 같은 상황이다.

**예제 13 — `earlycon`(맨이름)으로 부팅.**

```text
qemu-system-aarch64: QEMU: Terminated via GDBstub
[exit=0 lines=1]
```

**완전한 침묵** — 로그 파일에는 QEMU 자신의 종료 메시지(아래 예제에서 gdb로 kill 했다) 한 줄뿐이다. earlycon을 켰는데 한 글자도 없다. 이럴 때 할 일 = JTAG.

**예제 14 — gdb로 PC·예외 레지스터·로그 버퍼 읽기.** 로그 버퍼를 읽으려면 타입 정보가 필요해서, 디버그 정보와 `vmlinux-gdb.py`가 있는 Image-trace로 같은 실험을 했다.

```sh
K=/var/lab/out/Image-trace $B t_w2tr w2_mem "console=ttyAMA0 earlycon LAB_CMD=nproc" -gdb tcp:127.0.0.1:40601 &
sleep 35; cd /var/lab/build/linux-trace && gdb -q -batch -iex "set auto-load safe-path /" \
  -ex "target remote 127.0.0.1:40601" -ex "info registers pc" -ex 'p/x $ELR_EL1' \
  -ex 'info symbol $ELR_EL1' -ex "lx-dmesg" -ex kill ./vmlinux
```

```text
pc             0xffff8000816cbc88  0xffff8000816cbc88 <arch_timer_read_cntvct_el0+8>
$1 = 0xffff8000816cb430
clear_page + 48 in section .text
[    0.000000] Booting Linux on physical CPU 0x0000000000 [0x411fd070]
[    0.000000] Linux version 6.18.55-trace (donh@lima-elinux) (gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0, GNU ld (GNU Binutils for Ubuntu) 2.46) #1 SMP PREEMPT Sat Oct 17 23:37:47 PDT 2026
[    0.000000] KASLR disabled due to lack of seed
[    0.000000] Machine model: p1,bringup-stage0
[    0.000000] efi: UEFI not found.
[    0.000000] Internal error: synchronous external abort: 0000000096000050 [#1]  SMP
...
[    0.000000] pc : clear_page+0x30/0x70
[    0.000000] lr : __create_pgd_mapping_locked+0x404/0x918
[    0.000000] x29: ffff800083793b70 x28: ffffffffff229000 x27: ffff800083367000
[    0.000000] x26: 00000000bffff000 x25: 0000000040200000 x24: 0000000000000007
...
[    0.000000] Call trace:
[    0.000000]  clear_page+0x30/0x70 (P)
[    0.000000]  early_create_pgd_mapping+0x54/0x88
[    0.000000]  paging_init+0x14c/0x398
[    0.000000]  setup_arch+0x268/0x5d0
[    0.000000]  start_kernel+0x88/0x788
[    0.000000]  __primary_switched+0x88/0x98
[    0.000000] Kernel panic - not syncing: Attempted to kill the idle task!
```

출력에서 볼 것:

- CPU는 panic 뒤의 지연 루프(`arch_timer_read_cntvct_el0`)에 있고, 마지막 예외 복귀 주소(`ELR_EL1`)는 `clear_page+48`.
- `synchronous external abort` (ESR `0x96000050`: EC 0x25 = 같은 EL의 data abort, DFSC 0x10 = synchronous external abort). **존재하지 않는 물리 주소를 건드리면 버스가 에러를 돌려준다** — Don이 SoC에서 보던 "버스 decode error"와 같은 것이다.
- `x26 = 0xbffff000` = DT가 말한 메모리의 **맨 위 페이지**(0x40000000 + 2 GiB − 4 KiB). memblock은 위에서부터 할당하므로, `paging_init()`이 페이지 테이블을 만들려고 가장 높은 주소를 받아 0으로 지우다가 죽었다.
- 로그 버퍼에는 다 있는데 콘솔에는 안 나왔다. **earlycon이 아직 등록 전이었기 때문**이다 — 로그에 `earlycon:` 줄이 아예 없다.

왜 earlycon이 그렇게 늦나? 소스를 보면(`drivers/tty/serial/earlycon.c` `param_setup_earlycon`):

```c
	/* Just 'earlycon' is a valid param for devicetree and ACPI SPCR. */
	if (!buf || !buf[0]) {
		if (IS_ENABLED(CONFIG_ACPI_SPCR_TABLE)) {
			earlycon_acpi_spcr_enable = true;
			return 0;
		} else if (!buf) {
			return early_init_dt_scan_chosen_stdout();
		}
	}
```

`CONFIG_ACPI_SPCR_TABLE=y`인 커널(랩 커널 모두 — `CONFIG_ACPI=y`, `CONFIG_ACPI_SPCR_TABLE=y` 확인. ACPI를 켠 배포판·벤더 arm64 커널도 대개 그렇다)에서 맨이름 `earlycon`은 "ACPI SPCR을 볼지 DT를 볼지 아직 모르니 나중에"로 미뤄지고, 실제 등록은 `acpi_boot_table_init()`(arm64 `setup_arch()` 339행)에서 일어난다. 그런데 `paging_init()`은 334행 — **그 전**이다. 그래서 3.3절의 정상 로그에서도 `earlycon:` 줄이 `efi:` 줄 뒤에 나왔다.

**예제 15 — 주소를 명시한 `earlycon=pl011,0x9000000`으로 다시.**

```text
[    0.000000][    T0] Machine model: p1,bringup-stage0
[    0.000000][    T0] earlycon: pl11 at MMIO 0x0000000009000000 (options '')
[    0.000000][    T0] printk: legacy bootconsole [pl11] enabled
[    0.000000][    T0] efi: UEFI not found.
[    0.000000][    T0] Internal error: synchronous external abort: 0000000096000050 [#1]  SMP
...
[    0.000000][    T0] pc : clear_page+0x30/0x70
[    0.000000][    T0] lr : __create_pgd_mapping_locked+0x404/0x918
...
[    0.000000][    T0] ---[ end Kernel panic - not syncing: Attempted to kill the idle task! ]---
```

출력에서 볼 것: 이번엔 earlycon이 `parse_early_param()`(299행) 시점에 바로 등록되어 `efi:` **앞**에 나오고, 크래시 전체가 콘솔에 찍힌다. **교훈: bring-up 첫 커널은 `earlycon=<드라이버>,<주소>` 형태로 명시한다.** (pl011 외에 `uart8250,mmio32,0x...`, `ns16550a,mmio32,0x...` 등 — 지원 이름은 커널 문서 `admin-guide/kernel-parameters.txt`의 earlycon 항목.)

**변형 W2b — 메모리 시작 주소 오타(`0x80000000`).** 커널이 올라간 `0x40200000`이 "메모리 밖"이 된다.

```text
[    0.000000][    T0] initrd not fully accessible via the linear mapping -- please check your bootloader ...
[    0.000000][    T0] WARNING: CPU: 0 PID: 0 at arch/arm64/mm/init.c:266 arm64_memblock_init+0x130/0x1f8
[    0.000000][    T0] Internal error: synchronous external abort: 0000000096000050 [#1]  SMP
[    0.000000][    T0] Kernel panic - not syncing: Attempted to kill the idle task!
```

경고가 먼저 "initrd가 메모리 밖"이라고 알려 주고, 이어서 같은 external abort.

**반대 방향 — DT가 실제보다 작으면?** 5.2절 stress 실행은 `-m 1536M`으로 띄웠지만 DT는 512 MiB 그대로였다. 결과 로그: `node 0: [mem 0x0000000040000000-0x000000005fffffff]`, `Memory: 417064K/524288K available`. **죽지 않고, 조용히 1 GiB를 버린다.** 그래서 ⑧의 완료 조건에 "MemTotal이 실제 DDR과 맞음"이 들어간다.

### 4.3 W3 — `/chosen/stdout-path` 누락

```dts
/include/ "s0.dts"
/* WRONG 3: chosen/stdout-path missing */
/ { chosen { /delete-property/ stdout-path; }; };
```

**예제 16 — 세 가지 커맨드라인 비교.**

| 커맨드라인 | 결과 (실측) |
|---|---|
| `earlycon` | 한 글자도 안 나옴 (로그 1줄 = QEMU timeout 메시지뿐) |
| `console=ttyAMA0` | 출력됨 (244줄). 단, 정식 콘솔이 등록되는 순간(0.60 s) 버퍼를 한꺼번에 내보냄 — 그 전에 죽으면 안 보임 |
| `earlycon=pl011,0x9000000 console=ttyAMA0` | 첫 줄부터 정상 (249줄) |

```text
[    0.000000][    T0] Machine model: p1,bringup-stage0
[    0.000000][    T0] earlycon: pl11 at MMIO 0x0000000009000000 (options '')
[    0.000000][    T0] printk: legacy bootconsole [pl11] enabled
[    0.000000][    T0] efi: UEFI not found.
```

출력에서 볼 것: 맨이름 `earlycon`은 "어느 UART인지"를 `stdout-path`에서 찾는다. 그게 없으면 아무 일도 안 한다 — 에러도 없다. U-Boot도 `stdout-path`로 콘솔을 고르는 경우가 많아서, 이 속성 하나가 빠지면 **부트로더와 커널이 동시에 침묵**할 수 있다. 정식 콘솔(`console=ttyAMA0`)은 printk 버퍼를 등록 시점에 재생하므로 "앞부분도 다 보이네?" 하고 안심하기 쉽지만, 드라이버 probe 전에 죽는 버그는 못 잡는다.

### 4.4 W4 — UART 기준 클럭이 틀렸다

```dts
/include/ "s0.dts"
/* WRONG 4b: UART reference clock says 48 MHz, real clock is 24 MHz */
/ { apb-pclk { clock-frequency = <48000000>; }; };
```

QEMU의 PL011 모델은 baud를 흉내 내지 않는다(분주 레지스터를 무시하고 바이트를 그대로 내보냄). 그래서 화면은 멀쩡하다. 대신 **드라이버가 레지스터에 무슨 분주비를 썼는지**를 읽고, 그 분주비가 실제 24 MHz 클럭에서 몇 baud가 되는지 계산한다.

**예제 17 — `uartregs.sh`: DT 클럭 값과 PL011 분주 레지스터 읽기.**

```sh
#!/bin/sh
# p1: what divisor did the PL011 driver program, given the DT clock?
echo "DT uartclk (raw BE) = $(od -An -tx1 /proc/device-tree/apb-pclk/clock-frequency)"
stty -F /dev/ttyAMA0 115200 2>&1
echo "termios speed      = $(stty -F /dev/ttyAMA0 speed)"
for r in IBRD:0x9000024 FBRD:0x9000028 LCRH:0x900002c CR:0x9000030; do
  echo "${r%%:*} = $(devmem ${r#*:} 32)"
done
```

정상 DT(s0, 24 MHz):

```text
DT uartclk (raw BE) =  01 6e 36 00
termios speed      = 115200
IBRD = 0x0000000D
FBRD = 0x00000001
LCRH = 0x00000070
CR = 0x00000F01
```

W4b (48 MHz):

```text
DT uartclk (raw BE) =  02 dc 6c 00
termios speed      = 115200
IBRD = 0x0000001A
FBRD = 0x00000003
LCRH = 0x00000070
CR = 0x00000F01
```

W4 (1 MHz, `w4_clk.dts`는 같은 자리에 `<1000000>`):

```text
DT uartclk (raw BE) =  00 0f 42 40
stty: /dev/ttyAMA0: cannot perform all requested operations
termios speed      = 0
IBRD = 0x00000006
FBRD = 0x00000021
LCRH = 0x00000070
CR = 0x00000301
```

PL011의 분주비 = IBRD + FBRD/64, baud = UARTCLK / (16 × 분주비).

| DT가 말한 클럭 | 드라이버가 쓴 분주비 | 실제 24 MHz에서 나가는 baud | 호스트(115200)에서 보이는 것 |
|---|---|---|---|
| 24 MHz (정상) | 13 + 1/64 = 13.016 | 24e6 / (16 × 13.016) ≈ 115,246 | 정상 |
| 48 MHz | 26 + 3/64 = 26.047 | ≈ 57,590 (절반) | 깨진 글자, framing error |
| 1 MHz | 6 + 33/64 = 6.516 | ≈ 230,216 (두 배) | 깨진 글자, 글자 수 절반 |

1 MHz 경우가 흥미롭다: 1 MHz로는 115200을 만들 수 없어서(최대 1e6/16 = 62,500) 드라이버가 **9600으로 대체**했고(1e6 / (16 × 9600) = 6.51 → IBRD 6, FBRD 33), termios는 0(B0)이 되어 CR에서 RTS·DTR 비트(0xC00)가 꺼졌다. 이 분주비가 실제 24 MHz 클럭에서는 230,400 근처가 된다.

**예제 18 — 그 baud로 보내면 호스트에는 무엇이 보이나?** 8N1 파형을 만들고 115200 수신기로 샘플링하는 시뮬레이터.

```python
# p1_uartsim: what does a 115200 terminal show when the board really transmits at another baud?
def tx_bits(data, baud, fs):
    """8N1 waveform sampled at fs (idle=1, start=0, LSB first, stop=1)."""
    spb = fs / baud
    bits = [1] * 20
    for b in data:
        bits += [0] + [(b >> i) & 1 for i in range(8)] + [1]
    bits += [1] * 20
    n = int(len(bits) * spb)
    return [bits[int(i / spb)] for i in range(n)]

def rx(wave, baud, fs):
    """Ideal UART receiver: wait for falling edge, sample mid-bit."""
    spb, out, i, ferr = fs / baud, bytearray(), 1, 0
    while i < len(wave):
        if wave[i - 1] == 1 and wave[i] == 0:
            c = 0
            for k in range(8):
                j = int(i + (1.5 + k) * spb)
                if j < len(wave):
                    c |= wave[j] << k
            stop = int(i + 9.5 * spb)
            if stop < len(wave) and wave[stop] == 0:
                ferr += 1                      # framing error: stop bit not high
            out.append(c)
            i = int(i + 9.5 * spb)
        i += 1
    return bytes(out), ferr

FS = 115200 * 16 * 4
msg = b"U-Boot SPL 2026.10\r\n"
for real in (115200, 57600, 230400, 9600, 110000):
    got, ferr = rx(tx_bits(msg, real, FS), 115200, FS)
    shown = got.decode("latin-1").encode("unicode_escape").decode()[:44]
    print(f"board TX {real:>6} -> host 115200: {len(got):2d} bytes, framing err {ferr:2d}: {shown}")
```

```text
board TX 115200 -> host 115200: 20 bytes, framing err  0: U-Boot SPL 2026.10\r\n
board TX  57600 -> host 115200: 31 bytes, framing err 18: ff\x9e\x18\x80\xe6\x9e\xfe\x9e`\x06\x98\x98\
board TX 230400 -> host 115200: 10 bytes, framing err  0: ?\xbc\xed\xd8\xaeHJ+J\t
board TX   9600 -> host 115200: 60 bytes, framing err 60: \x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00
board TX 110000 -> host 115200: 20 bytes, framing err  0: U-Boot SPL 2026.10\r\n
```

출력에서 볼 것 — **패턴으로 비율을 추정하는 법**:

- 보드가 **느리면**(57600): 글자 수가 늘고 framing error가 많다. 한 비트가 두 번 샘플링되기 때문이다.
- 보드가 **빠르면**(230400): 글자 수가 절반쯤으로 줄고 엉뚱한 글자.
- 아주 느리면(9600): `0x00`과 framing error만 — 한 글자의 start bit가 수신기 입장에서 "0이 아주 긴 것"으로 보인다.
- 4.5% 차이(110000)는 **멀쩡히 통과한다**. 수신기는 매 글자 start bit에서 다시 맞추므로 10비트 동안의 누적 오차가 반 비트 미만이면 된다(약 ±5%). 그래서 "가끔 한 글자씩 깨진다"는 baud가 "약간" 틀린 경우(예: 크리스털이 다른 주파수, 분주비 반올림)이다.

```svg
<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" text-anchor="middle" font-size="14">'U'(0x55)를 보드가 57600으로 보냈고, 호스트가 115200으로 샘플링하면</text>
  <g font-size="12"><text x="10" y="70">보드 TX 57600</text><text x="10" y="140">호스트 샘플</text><text x="10" y="200">결과</text></g>
  <polyline fill="none" stroke="#4a7bd0" stroke-width="2" points="130,50 150,50 150,80 198,80 198,50 246,50 246,80 294,80 294,50 342,50 342,80 390,80 390,50 438,50 438,80 486,80 486,50 534,50 534,80 582,80 582,50 640,50"/>
  <g stroke="currentColor" stroke-dasharray="2,3">
    <line x1="150" y1="40" x2="150" y2="150"/><line x1="174" y1="40" x2="174" y2="150"/><line x1="198" y1="40" x2="198" y2="150"/>
    <line x1="222" y1="40" x2="222" y2="150"/><line x1="246" y1="40" x2="246" y2="150"/><line x1="270" y1="40" x2="270" y2="150"/>
    <line x1="294" y1="40" x2="294" y2="150"/><line x1="318" y1="40" x2="318" y2="150"/><line x1="342" y1="40" x2="342" y2="150"/>
    <line x1="366" y1="40" x2="366" y2="150"/><line x1="390" y1="40" x2="390" y2="150"/>
  </g>
  <g fill="#e08a3c">
    <circle cx="162" cy="125" r="4"/><circle cx="186" cy="125" r="4"/><circle cx="210" cy="125" r="4"/><circle cx="234" cy="125" r="4"/>
    <circle cx="258" cy="125" r="4"/><circle cx="282" cy="125" r="4"/><circle cx="306" cy="125" r="4"/><circle cx="330" cy="125" r="4"/>
    <circle cx="354" cy="125" r="4"/><circle cx="378" cy="125" r="4"/>
  </g>
  <g font-size="12" text-anchor="middle">
    <text x="162" y="145">S</text><text x="186" y="145">1</text><text x="210" y="145">1</text><text x="234" y="145">0</text><text x="258" y="145">0</text>
    <text x="282" y="145">1</text><text x="306" y="145">1</text><text x="330" y="145">0</text><text x="354" y="145">0</text><text x="378" y="145">stop=0</text>
  </g>
  <text x="130" y="200" font-size="12">비트 b0..b7 = 0,1,1,0,0,1,1,0 → 0x66 = 'f' + stop 자리가 0 → framing error</text>
  <text x="130" y="220" font-size="12">예제 18 출력의 첫 글자가 'f'인 이유 (U-Boot의 'U')</text>
</svg>
```
그림 5 — 보드가 절반 속도로 보내면 보드의 한 비트(굵은 칸 48px)를 호스트가 두 번 샘플링한다(점선 = 115200 비트 경계, 점 = 샘플 시점). 'U'가 'f'로 바뀌고 stop 비트 자리에서 0을 읽어 framing error. 실보드에서는 scope로 비트 폭을 재면 바로 확인된다: 115200의 1비트 = 8.68 µs.

---

## 5. DDR 단계와 stress 단계를 랩에서

### 5.1 작은 메모리 테스트 — 무엇을 왜 시험하나

DDR bring-up의 메모리 테스트는 "쓰고 읽어서 같은지"보다 구조적이다. 고장 유형마다 잡는 패턴이 다르다.

| 테스트 | 잡는 고장 | 원리 |
|---|---|---|
| data bus walking 1 | 데이터 라인 short/open (DQ) | 한 주소에 1비트씩 켜 써 보고 읽기 |
| address bus (2의 거듭제곱 오프셋) | 주소 라인 short/open → aliasing | 0번지에 쓴 값이 2^k 번지에서 보이면 그 주소 라인 불량 |
| own-address | 넓은 영역의 셀·디코딩 | 각 워드에 자기 주소를 쓰고 검증 |
| inverse | 비트가 0/1 한쪽으로 붙음 | own-address의 반전 |

**예제 19 — `p1_memtest.c`** (정적 빌드해 initramfs에 넣었다. 유저 공간에서는 가상 주소를 다루므로 "주소 라인" 테스트는 개념 시연이다 — 실보드 DDR 테스트는 SPL·U-Boot에서 물리 주소로 돈다).

```c
/* p1_memtest: tiny DDR smoke test (data-bus walking 1s, address-bus, own-address, inverse) */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <time.h>

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

int main(int argc, char **argv)
{
	size_t mb = argc > 1 ? strtoul(argv[1], NULL, 0) : 64, n = mb << 20 >> 3, i, err = 0;
	volatile uint64_t *m = mmap(NULL, n * 8, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
	if (m == MAP_FAILED) { perror("mmap"); exit(1); }
	if (mlock((void *)m, n * 8)) perror("mlock (continuing)");

	for (int b = 0; b < 64; b++) {			/* 1) data bus: one word, walking 1 */
		m[0] = 1ULL << b;
		if (m[0] != 1ULL << b) { printf("data bit %d stuck\n", b); err++; }
	}
	for (i = 1; i < n; i <<= 1) m[i] = 0xAAAAAAAAAAAAAAAAULL;	/* 2) address bus: power-of-2 offsets */
	m[0] = 0x5555555555555555ULL;
	for (i = 1; i < n; i <<= 1)
		if (m[i] != 0xAAAAAAAAAAAAAAAAULL) { printf("addr line 0x%zx aliases to 0\n", i * 8); err++; }

	double t0 = now();
	for (i = 0; i < n; i++) m[i] = (uint64_t)(uintptr_t)&m[i];	/* 3) own address */
	for (i = 0; i < n; i++) if (m[i] != (uint64_t)(uintptr_t)&m[i]) { if (err++ < 5) printf("own-addr miss @%zx\n", i * 8); }
	for (i = 0; i < n; i++) m[i] = ~(uint64_t)(uintptr_t)&m[i];	/* 4) inverse */
	for (i = 0; i < n; i++) if (m[i] != ~(uint64_t)(uintptr_t)&m[i]) { if (err++ < 5) printf("inverse miss @%zx\n", i * 8); }
	double dt = now() - t0;

	printf("p1_memtest: %zu MiB, 4 tests, errors=%zu, %.1f MiB/s (write+read x2)\n", mb, err, 4.0 * mb / dt);
	return err ? 2 : 0;
}
```

```sh
gcc -static -std=gnu11 -Wall -Wextra -O2 memtest.c -o p1_memtest    # 경고 0개
```

실행 결과는 다음 예제의 첫 줄에 같이 나온다.

### 5.2 Stage "stress" — 최종 DT(s4)로 rootfs-dom의 stress-ng 돌리기

rootfs-dom(6 GiB ext4)은 읽기 전용 원본이라 qcow2 overlay를 만들어 virtio-blk-pci로 붙이고, BusyBox initramfs에서 마운트 후 `chroot`로 배포판 도구를 실행했다. **최종 단계 DT(s4)로 실제 배포판 도구가 돈다** = "DT 하나씩 켜기"의 마무리.

**예제 20 — `stress.sh`와 실행.**

```sh
#!/bin/sh
# p1 stage "stress/thermal": run distro tools from rootfs-dom (qcow2 overlay) via chroot
mount /dev/vda /mnt || exit 1
for f in proc sys dev; do mount --bind /$f /mnt/$f; done
echo "thermal zones: $(ls /sys/class/thermal 2>/dev/null | wc -l), hwmon: $(ls /sys/class/hwmon 2>/dev/null | wc -l)"
/p1/p1_memtest 128
chroot /mnt /usr/bin/stress-ng --cpu 2 --vm 1 --vm-bytes 96M --verify --timeout 20s --metrics-brief 2>&1 | grep -v "^stress-ng: info:  \[[0-9]*\] setting\|dispatching"
chroot /mnt sh -c 'command -v memtester || echo "memtester: not installed in rootfs-dom"'
echo "dmesg errors after stress: $(dmesg | grep -ciE 'error|oops|bug:')"
for f in dev sys proc; do umount /mnt/$f; done; umount /mnt
```

```sh
qemu-img create -q -f qcow2 -F raw -b /var/lab/out/rootfs-dom.ext4 /var/tmp/P1/p1.qcow2
TMO=300 MEM=1536M $B st s4 "console=ttyAMA0 earlycon LAB_CMD=/p1/stress.sh" \
  -drive file=/var/tmp/P1/p1.qcow2,if=none,format=qcow2,id=d0 -device virtio-blk-pci,drive=d0
```

```text
lab initramfs: 6.18.55-dom up 5.10s
thermal zones: 0, hwmon: 0
p1_memtest: 128 MiB, 4 tests, errors=0, 705.6 MiB/s (write+read x2)
stress-ng: metrc: [88] stressor       bogo ops real time  usr time  sys time   bogo ops/s     bogo ops/s
stress-ng: metrc: [88]                           (secs)    (secs)    (secs)   (real time) (usr+sys time)
stress-ng: metrc: [88] cpu                 218     25.98     26.88      0.43         8.39           7.98
stress-ng: metrc: [88] vm                11774     28.96      6.55      9.93       406.60         714.18
stress-ng: info:  [88] skipped: 0
stress-ng: info:  [88] passed: 3: cpu (2) vm (1)
stress-ng: info:  [88] failed: 0
stress-ng: info:  [88] metrics untrustworthy: 0
stress-ng: info:  [88] successful run completed in 30.93 secs
memtester: not installed in rootfs-dom
dmesg errors after stress: 0
```

(stress-ng의 `sched_autogroup_enabled` 안내 한 줄은 생략했다.)

출력에서 볼 것:

- `--verify`를 켠 vm stressor가 0 fail — 메모리 내용 검증까지 통과. 실보드라면 이걸 N시간, 챔버 온도별로.
- **thermal zone 0개, hwmon 0개.** QEMU virt에는 온도 센서가 없다. 실보드 ⑪ 단계의 핵심 관측 대상(`/sys/class/thermal/thermal_zone*/temp`, 쓰로틀링 trip point — N5)이 여기서는 비어 있다는 것 자체가 랩의 한계다.
- `memtester`는 rootfs-dom에 없다(그래서 5.1절에서 직접 만들었다).
- 숫자(705.6 MiB/s, bogo ops/s)는 공유 VM의 TCG 값이라 의미가 없다. 실보드에서는 "같은 보드의 어제 값"과 비교하는 기준선으로 쓴다.

---

## 6. 커널 안에서는 무슨 일이 일어나나 — DT를 언제 누가 읽나

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="14">커널이 DT를 소비하는 순서와, 이 노트의 실수가 터지는 지점</text>
  <g font-size="12">
    <rect x="10" y="36" width="420" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="20" y="58">head.S: x0 = DTB 주소 저장, MMU on</text>
    <rect x="10" y="80" width="420" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="20" y="102">setup_machine_fdt: model, memory 노드 → memblock</text>
    <rect x="10" y="124" width="420" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="20" y="146">parse_early_param: earlycon=pl011,주소 → 즉시 등록</text>
    <rect x="10" y="168" width="420" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="20" y="190">arm64_memblock_init · paging_init (메모리 맨 위부터 할당)</text>
    <rect x="10" y="212" width="420" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="20" y="234">acpi_boot_table_init: 맨이름 earlycon → stdout-path</text>
    <rect x="10" y="256" width="420" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="20" y="278">OF_DECLARE 초기화: gic · arch_timer · fixed-clock</text>
    <rect x="10" y="300" width="420" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="20" y="322">of_platform_populate: 나머지 노드 → platform/amba device</text>
    <rect x="10" y="344" width="420" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="20" y="366">driver probe (+deferred 재시도) → console ttyAMA0</text>
    <rect x="460" y="80" width="210" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="470" y="102">W2b: initrd 경고</text>
    <rect x="460" y="168" width="210" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="470" y="190">W2: external abort</text>
    <rect x="460" y="212" width="210" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="470" y="234">W3: 아무 일도 안 함</text>
    <rect x="460" y="256" width="210" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="470" y="278">W1: timer 실패, 시간 정지</text>
    <rect x="460" y="344" width="210" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="470" y="366">W4: 분주비 계산 틀림</text>
  </g>
  <g stroke="currentColor" stroke-width="1.2">
    <line x1="220" y1="70" x2="220" y2="78" marker-end="url(#ah3)"/><line x1="220" y1="114" x2="220" y2="122" marker-end="url(#ah3)"/>
    <line x1="220" y1="158" x2="220" y2="166" marker-end="url(#ah3)"/><line x1="220" y1="202" x2="220" y2="210" marker-end="url(#ah3)"/>
    <line x1="220" y1="246" x2="220" y2="254" marker-end="url(#ah3)"/><line x1="220" y1="290" x2="220" y2="298" marker-end="url(#ah3)"/>
    <line x1="220" y1="334" x2="220" y2="342" marker-end="url(#ah3)"/>
    <line x1="458" y1="97" x2="432" y2="97" marker-end="url(#ah3)"/><line x1="458" y1="185" x2="432" y2="185" marker-end="url(#ah3)"/>
    <line x1="458" y1="229" x2="432" y2="229" marker-end="url(#ah3)"/><line x1="458" y1="273" x2="432" y2="273" marker-end="url(#ah3)"/>
    <line x1="458" y1="361" x2="432" y2="361" marker-end="url(#ah3)"/>
  </g>
  <text x="340" y="405" text-anchor="middle" font-size="12">파란 칸 = setup_arch() 안 (arm64 setup.c 292·299·332·334·339행), 초록 칸 = 그 뒤 (initcall)</text>
</svg>
```
그림 6 — 커널의 DT 소비 순서. 실수가 "어느 칸에서 터지느냐"가 증상(침묵/경고/시간 정지/깨진 글자)을 결정한다.

정리하면:

- **두 종류의 DT 노드 소비자가 있다.** (1) 부팅 초기에 매크로로 등록된 초기화 함수 — `IRQCHIP_DECLARE(cortex_a15_gic, "arm,cortex-a15-gic", gic_of_init)`(`drivers/irqchip/irq-gic.c`), `TIMER_OF_DECLARE(armv8_arch_timer, "arm,armv8-timer", ...)`(`drivers/clocksource/arm_arch_timer.c`), `CLK_OF_DECLARE(fixed_clk, "fixed-clock", ...)`(`drivers/clk/clk-fixed-rate.c`). 드라이버 모델이 뜨기 전에 필요한 것들이다. (2) 나머지는 `of_platform_populate()`가 `platform_device`/`amba_device`로 만들고, 일반 드라이버 매칭·probe(D3, G3)를 거친다.
- 초기화 코드가 노드에 `OF_POPULATED` 플래그를 세우면(`drivers/of/irq.c` 624행 — irqchip, `drivers/clk/clk.c` — `CLK_OF_DECLARE`) populate가 그 노드의 device를 만들지 않는다. 그래서 audit에서 intc·apb-pclk는 "struct device 없음", timer는 "device는 있으나 드라이버 없음"으로 나왔다. 둘 다 정상이다.
- probe 시점에 공급자(clock, regulator, gpio, pinctrl …)가 아직 없으면 드라이버는 `-EPROBE_DEFER`를 돌려주고, 커널이 나중에 다시 시도한다. `/sys/kernel/debug/devices_deferred`에 끝까지 남는 장치 = DT 공급자 노드가 빠졌거나 그 드라이버가 config에 없다. bring-up ⑨ 단계의 일상 도구다.
- 드라이버를 쓸 때 다시 만나는 곳: `devm_clk_get()`이 W4의 `clock-frequency`를 읽는 통로(H3), `platform_get_irq()`가 W1에서 0/에러를 받는 통로(D4 4.5), `dev_err_probe()`가 deferred 사유를 남기는 통로(M1).

---

## 7. Don의 경험을 이 지도에 올려 놓기

| bring-up 단계 | Don이 이미 한 일 (이력 기반) | 면접에서 연결하는 말 |
|---|---|---|
| ①~③ 전원·클럭·리셋 | Apple: 새 RF 칩셋 실리콘 통합, scope·로직 분석기로 측정 | "I start with a current-limited supply and verify rails and sequencing before trusting any software." |
| ④ JTAG | JTAG로 halt, PC·레지스터 확인 (Apple, SSD) | "When the console is silent, JTAG PC sampling tells me whether it is a crash or a hang." |
| ⑤ 콘솔 | SSD 펌웨어의 UART 디버그 로그 | "Console first — every later stage depends on it." |
| ⑥ DDR | SSD 컨트롤러의 DRAM/버퍼 초기화·에러 처리 경험(세부는 Don이 채울 것) | "DDR training and a structured memory test before booting anything big." |
| ⑦ 부트로더 | SSD ROM → 로더 → 펌웨어 체인 | "Same layered loader idea: ROM, SPL, TF-A, U-Boot." |
| ⑨ 버스·장치 | PCIe/I2C/SPI/SPMI/RFFE 버스 장애 root cause (Apple), NVMe/PCIe (SSD) | "Bring up providers first — clocks, regulators, pinctrl — then buses, then devices." |
| ⑩ 주변장치 검증 | RF 칩 기능 검증, SSD 성능 튜닝 | "Probe success is not functional success — I validate throughput and error counters." |
| ⑪ stress | SSD 양산 펌웨어의 신뢰성·에러 처리 | "Stress across temperature — marginal DDR shows up as random Oopses." |
| ⑫ 공장 | SSD 양산(mass production) 펌웨어 | "Bring-up scripts become factory self-test." |

약점과 메우는 법: Linux 쪽(⑧~⑨)의 실보드 경험이 적다 → 이 노트의 QEMU 랩 + D·G·H 모듈 노트를 "직접 해 봤다"로 말할 수 있게 한다. 거짓으로 실보드 Linux bring-up을 했다고 말하지 않는다 — 대신 "실리콘 bring-up 방법론은 같고, Linux 단계는 QEMU에서 단계별 DT로 연습했다"고 말한다.

---

## 8. 인쇄용 bring-up 체크리스트

보드 한 장마다 이 목록을 복사해 날짜·측정값을 적는다.

**① 전원** (진입: 육안 검사, 레일 short 검사 완료)

- [ ] 레일별 GND 대비 저항 측정·기록 (전원 넣기 전)
- [ ] bench supply current limit 설정 (예상 × 1.5~2)
- [ ] 첫 투입 전류 기록, CC 모드 아님
- [ ] 레일별 DC 전압 ±허용치, 리플 측정
- [ ] scope로 램프 순서·간격·PGOOD → 데이터시트 표와 대조
- [ ] 대기 전류 기준선 기록

**② 클럭 · ③ 리셋**

- [ ] 메인 OSC 주파수·진폭 (가능하면 CLKOUT으로)
- [ ] RTC 32.768 kHz 발진 확인
- [ ] POR_B가 마지막 레일·클럭 안정 뒤 한 번 해제, 재토글 없음
- [ ] 리셋 원인 레지스터 = POR

**④ JTAG · ⑤ 콘솔**

- [ ] VTref 레벨 맞음, IDCODE 읽힘
- [ ] halt/step, SRAM 쓰기/읽기
- [ ] USB-UART 전압 레벨 확인 (1.8 V / 3.3 V), TX/RX 방향
- [ ] 부트 모드 핀 설정 확인, USB 복구 모드로 SPL 로드
- [ ] 첫 줄 깨끗이 출력 (깨지면: scope로 비트 폭 측정)

**⑥ DDR · ⑦ 부트로더**

- [ ] DDR 레일·VREF 전압
- [ ] training PASS (byte lane별 로그 저장)
- [ ] 전 용량 메모리 테스트 0 error (상온 / 고온 / 저온)
- [ ] TF-A BL31 로드, PSCI 버전 확인
- [ ] U-Boot 프롬프트, `mmc info` / `ping` / `tftp`
- [ ] U-Boot이 고치는 DT 항목 확인 (memory, bootargs, MAC)

**⑧ 커널 최소 DT**

- [ ] `earlycon=<드라이버>,<주소>` 명시
- [ ] DT: cpus, memory(실제 용량), chosen/stdout-path, GIC, timer, UART만
- [ ] dtc 경고 0
- [ ] shell 도달, 타이머 인터럽트 증가, `nproc` = CPU 수, MemTotal ≈ DDR 용량

**⑨ DT 노드 하나씩** (노드마다 반복)

- [ ] pinctrl → clk/reset/regulator → GPIO → I2C/SPI → eMMC → Ethernet → USB → display/camera
- [ ] probe 메시지, 바인딩 확인 (`audit.sh` 류)
- [ ] `devices_deferred` 0, dmesg 에러 0
- [ ] 노드 하나 = 커밋 하나

**⑩ 검증 · ⑪ stress · ⑫ 공장**

- [ ] 장치별 기능 + 성능 테스트 결과 저장 (기준선)
- [ ] stress-ng / stressapptest / fio / iperf3 N시간, 챔버 온도별
- [ ] thermal zone 온도·쓰로틀링 trip 확인, 전력 측정
- [ ] 생산 테스트 스크립트화, 프로비저닝 절차, 결과 로그 수집

---

## 9. STAR 형식 인터뷰 답변

"Tell me about a time you brought up a new board" 에 대한 답. Don의 실제 사례로 대괄호 부분을 채워야 한다 — **지어낸 숫자나 사건을 말하지 않는다.** 아래는 구조와 문장 틀이다.

**S (Situation)**: [Apple에서 새 RF 칩셋 첫 실리콘이 호스트 플랫폼에 붙은 보드가 들어왔다 / 구체적 상황]. 아무 것도 검증되지 않은 상태였다.

**T (Task)**: 짧은 기간 안에 [칩이 호스트와 통신하는 상태 / 목표]까지 올리고, 실패하면 HW 문제인지 SW 문제인지 구분해서 담당 팀에 넘겨야 했다.

**A (Action)**: 아래층부터 한 단계씩 exit criteria를 정해서 올라갔다. 전류 제한을 건 상태로 전원을 넣고 레일·시퀀싱을 scope로 확인했다. 클럭과 리셋을 확인한 뒤 JTAG로 붙어 [레지스터 읽기]. 그 다음 [버스: PCIe/I2C/SPMI/RFFE] 링크를 올리다가 [증상]이 나왔고, 로직 분석기로 [측정]해서 [root cause]를 찾았다. 한 번에 하나만 바꾸고, 단계마다 결과를 기록했다.

**R (Result)**: [결과 — 일정, 찾은 버그, 재발 방지]. 그때 만든 체크 절차를 [팀/다음 보드]에 재사용했다.

**Linux로 잇는 마지막 한 문단**: "Linux 보드에서도 같은 방법론을 쓴다. 최소 Device Tree — CPU, 메모리, 인터럽트 컨트롤러, 타이머, UART — 로 earlycon을 명시해서 shell까지 올리고, 공급자부터 노드를 하나씩 켠다. QEMU에서 이 과정을 직접 연습하면서, 예를 들어 memory 노드가 실제보다 크면 `paging_init`에서 external abort가 나고, 맨이름 `earlycon`은 그 크래시보다 늦게 등록되어 침묵한다는 것을 확인했다."

영어 버전(그대로 말하는 틀):

> Situation: At Apple I was handed [first silicon of a new RF chipset on a host platform]. Nothing on the board had been validated yet.
> Task: My job was to get it to [goal] quickly, and when something failed, to tell whether it was a hardware or software problem.
> Action: I went bottom-up with explicit exit criteria for every stage. Current-limited bench supply first, then rails and sequencing on the scope, then clock and reset, then JTAG to read [registers]. When [bus] failed with [symptom], I captured it on the logic analyzer and found [root cause]. I changed one thing at a time and logged every step.
> Result: [outcome]. The checklist became the template for [next boards / team].
> On a Linux board I apply the same method: a minimal device tree — CPUs, memory, GIC, timer, UART — with an explicit earlycon to reach a shell, then enable nodes one by one in dependency order: pinctrl, clocks and regulators, buses, storage, network, USB, camera. I practiced this stage-by-stage on QEMU, including deliberately broken device trees, so I know what each mistake looks like.

**꼬리 질문 대비**: "What was the hardest stage?" → DDR(실보드)이라고 말하고, 마진 문제는 온도별 stress로 드러난다는 점. "How do you know a stage is done?" → 그림 1의 exit criteria 그대로.

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| current limit 없이 첫 전원 | 부품 손상, 연기 | short가 그대로 전류를 받음 | 예상 전류 1.5~2배로 limit, 첫 투입은 전류계 보면서 |
| 맨이름 `earlycon`만 사용 | 초기 크래시가 완전 무출력 | ACPI SPCR 커널에서 등록이 `paging_init` 뒤 | `earlycon=pl011,0x...`처럼 명시 (4.2) |
| memory `reg`가 실제보다 큼 | 무출력 또는 `synchronous external abort` in `clear_page` | memblock이 없는 주소의 맨 위 페이지를 할당 | 실제 DDR 용량으로, 부트로더 fixup 여부 확인 |
| memory `reg`가 실제보다 작음 | 정상 부팅, RAM 손실 | DT만 믿고 나머지 무시 | MemTotal을 exit criteria에 넣기 |
| 루트 `interrupt-parent` 누락 | 타임스탬프 0.000000 고정, 콘솔 인계 후 무응답 | 타이머·UART IRQ를 못 찾음 | dtc 경고를 에러로 취급 |
| `stdout-path` 누락 | earlycon·U-Boot 콘솔 침묵 | 콘솔 UART를 고를 정보 없음 | `chosen { stdout-path = "serial0:115200n8"; }` |
| UART 기준 클럭 오설정 | 깨진 글자, framing error | 분주비 계산이 틀림 | DT 클럭 값 확인, 분주 레지스터 읽기, scope로 비트 폭 |
| 장치 인스턴스/주소 착오 | 에러 없이 장치 없음 | DT `reg`가 다른 블록을 가리킴 | 회로도의 실제 인스턴스와 대조 (P4) |
| 소비자를 공급자보다 먼저 켬 | deferred 목록에 남음 | 공급자 노드·드라이버 없음 | 의존성 순서, `devices_deferred` |
| 여러 노드를 한꺼번에 켬 | 어느 것이 원인인지 모름 | 변수 두 개 이상 | 노드 하나 = 커밋 하나 |

---

## 11. 면접에서 이렇게 말한다

**Q.** Walk me through how you would bring up a brand-new board.

**A.** 아래층부터 단계마다 완료 조건을 정한다: 전원 레일·시퀀싱 → 클럭·리셋 → JTAG → UART 콘솔 → DDR training·메모리 테스트 → SPL/TF-A/U-Boot → 최소 DT + earlycon으로 커널 shell → DT 노드를 의존성 순서로 하나씩 → 주변장치 성능 검증 → 온도별 stress → 공장 테스트. 각 단계에서 "고장 나면 무엇으로 볼지"를 먼저 확보한다.

> I go bottom-up and define exit criteria per stage: power rails and sequencing on a current-limited supply, then clocks and reset, JTAG, UART console, DDR training plus a memory test, the bootloader chain, then the kernel with a minimal device tree and an explicit earlycon. After that I enable device-tree nodes one at a time in dependency order, validate each peripheral functionally and for performance, stress across temperature, and finally turn the tests into factory checks.

**꼬리 질문**: "What's in your minimal device tree?" → cpus, memory, chosen/stdout-path, interrupt controller, architected timer, one UART (+ 그 클럭).

**Q.** The kernel prints nothing after "Starting kernel". What do you do?

**A.** 먼저 earlycon을 드라이버·주소까지 명시해서 켠다. 그래도 없으면 JTAG(또는 gdb)로 PC, ESR, FAR을 읽고 로그 버퍼를 메모리에서 꺼낸다. 흔한 원인은 DT memory 노드 오류, stdout-path 누락, 커널 로드 주소, DTB 주소 전달.

> I enable earlycon with an explicit driver and address — a bare "earlycon" can register too late on ACPI-capable arm64 kernels. If it is still silent, I attach JTAG, read PC, ESR and FAR, and dump the printk buffer from memory. Typical causes are a wrong memory node, a missing stdout-path, or the bootloader passing the wrong DTB address.

**Q.** You see garbage characters on the serial console. What's the likely cause?

**A.** baud 불일치다. 보드 쪽 UART 기준 클럭 설정이 틀려 분주비가 틀렸거나, 크리스털 주파수가 다르거나. 글자 수가 늘고 framing error가 많으면 보드가 느리고, 줄면 빠르다. scope로 1비트 폭을 재면 바로 확정된다(115200이면 8.68 µs).

> Almost always a baud-rate mismatch — usually the UART reference clock in the device tree or clock driver is wrong, so the divisor is wrong. More characters with framing errors means the board is slower than the host; fewer means faster. A scope measurement of one bit time confirms it.

**Q.** Only one CPU comes up. Why?

**A.** secondary CPU를 깨우는 방법(`enable-method`)이 동작하지 않는다. arm64에선 대부분 PSCI — DT의 psci 노드, `method`(smc/hvc), TF-A BL31이 CPU_ON을 지원하는지. 랩에서 psci 노드를 빼면 "psci: no cpu_on method, not booting CPU1"이 정확히 나온다.

> The secondary-CPU enable method is failing. On arm64 that's usually PSCI: check the psci node, the conduit (smc or hvc) and whether the TF-A BL31 implements CPU_ON. Without a psci node the kernel says "no cpu_on method, not booting CPU1".

**Q.** How do you decide the order in which to enable device-tree nodes?

**A.** 의존성 그래프 순서. 공급자(pinctrl, clock, reset, regulator, GPIO, 인터럽트 컨트롤러)가 먼저, 버스 컨트롤러, 그 위의 장치, 마지막으로 의존성이 많은 display·camera. 각 단계에서 deferred 목록과 dmesg 에러를 0으로 만든 뒤 다음으로.

> Dependency order: providers first — pinctrl, clocks, resets, regulators, GPIO — then bus controllers, then devices on those buses, and finally display and camera, which depend on everything. Each step must end with an empty deferred-probe list and no new errors.

**Q.** How do you validate DDR on a new board?

**A.** SPL·벤더 툴로 training을 통과시키고 로그를 저장한 다음, data bus·address bus·own-address 같은 구조적 메모리 테스트를 전 용량에 돌린다. 마지막으로 온도별 장시간 stress. 마진 부족은 "같은 코드가 매번 다른 곳에서 죽는" 무작위 Oops로 나타난다.

> Training must pass with logs saved, then a structured memory test across the full size — data-bus walking ones, address-bus aliasing, own-address patterns — and then long stress at temperature corners. Marginal DDR shows up as random, non-repeatable Oopses.

**Q.** Your peripheral driver probes fine but the device doesn't work. What next?

**A.** probe 성공은 레지스터 읽기가 됐다는 뜻일 뿐이다. 핀mux, 클럭 속도, 전압 레벨, IRQ 트리거 타입을 확인하고, 버스를 로직 분석기로 본다. Don의 강점 — 버스 레벨 root cause — 이 가장 빛나는 지점.

> Probe success only proves register access. I check pinmux, clock rate, I/O voltage and IRQ trigger type, then put the bus on a logic analyzer. That bus-level root-causing is what I did daily on RF and SSD silicon.

---

## 12. 직접 해보기

1. **개념**: bring-up 12단계를 그림 없이 순서대로 쓰고, 각 단계의 exit criteria를 한 줄씩 붙여라.
정답/힌트: 그림 1. "probe 성공"이 아니라 "기능·성능 PASS"가 ⑩의 기준이라는 점을 빠뜨리지 말 것.

2. **랩**: `s0.dts`에서 `cpu@1` 노드의 `enable-method`를 지운 채 s1(psci 있음)로 부팅하라. CPU가 몇 개 뜨고 어떤 메시지가 나오는가?
정답/힌트: `enable-method`가 없으면 psci가 있어도 그 CPU를 깨울 방법이 없다고 판단한다. dmesg에서 `CPU1`·`enable-method`를 grep.

3. **랩**: W2를 `earlycon`(맨이름)으로 띄우고, `-gdb`로 붙어 `p/x $x26`(또는 Oops의 x26/x28)을 읽어 DT의 메모리 끝 주소와 비교하라.
정답/힌트: 0x40000000 + 크기 − 0x1000. memblock top-down 할당.

4. **랩**: `w4_clk.dts`를 12 MHz로 바꿔 분주 레지스터를 읽고, 실제 24 MHz에서의 baud를 계산하라. 그 baud로 `uartsim.py`를 돌려 호스트 화면을 예측하라.
정답/힌트: 12 MHz → 분주비 ≈ 6.51 → 24 MHz에서 ≈ 230,400 baud. 글자 수가 절반쯤.

5. **랩**: s3 단계에서 `-device virtio-blk-device`를 세 개 붙이면 어떤 슬롯이 쓰이고 어떤 것이 NO DRIVER로 남는가? 슬롯 28~31 중 하나만 남기면?
정답/힌트: QEMU는 31번부터 내려가며 채운다. DT에 없는 슬롯의 장치는 조용히 사라진다.

6. **스크립트**: `audit.sh`를 고쳐 "DT에 `status = "okay"`인데 바인딩 안 된 노드"만 빨간 줄로 요약하고 종료 코드 1을 내게 하라 — 이것이 CI(P6)의 bring-up 회귀 테스트가 된다.
정답/힌트: `/proc/device-tree/<node>/status`가 없으면 okay로 간주. NO DRIVER 줄 수를 세어 `exit`.

7. **인터뷰**: 9절 STAR 틀의 대괄호를 Don의 실제 사례 하나로 채우고, 90초 안에 영어로 말해 녹음하라.
정답/힌트: Action에 "exit criteria", "one change at a time", "measurement tool" 세 표현이 들어가는지 확인.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| bring-up | 첫 구동 | 새 HW를 아래층부터 하나씩 검증하며 살리는 과정 |
| exit criteria | 완료 조건 | 이 단계를 "끝났다"고 말하기 위한 측정 가능한 조건 |
| PMIC | Power Management IC | 여러 전원 레일을 만들고 순서·전압을 관리하는 칩 |
| PGOOD | power good | 레일이 정상 범위에 들어왔다는 출력 신호 |
| POR_B | power-on reset (active low) | 전원 안정 후 SoC 리셋을 푸는 신호 |
| CLKOUT | clock output | 내부 클럭을 핀으로 내보내는 디버그 기능 |
| IDCODE | JTAG ID 레지스터 | JTAG 체인이 살아 있는지 보는 첫 읽기 |
| boot mode pin | 부트 모드 핀 | BootROM이 어떤 미디어에서 부팅할지 고르는 핀/fuse |
| DDR training | DDR 타이밍 보정 | PHY가 보드 배선 지연에 맞춰 샘플링 타이밍을 맞추는 과정 |
| earlycon | 초기 콘솔 | 정식 UART 드라이버 전에 MMIO에 직접 쓰는 콘솔 |
| stdout-path | `/chosen` 속성 | 콘솔 UART를 가리키는 DT 경로 |
| PSCI | Power State Coordination Interface | CPU on/off·리셋·전원 끄기를 펌웨어(TF-A)에 요청하는 규약 |
| OF_DECLARE 계열 | 초기 DT 초기화 매크로 | irqchip·timer·clock을 드라이버 모델 전에 초기화 |
| OF_POPULATED | DT 노드 플래그 | "이미 처리됨" — populate가 device를 만들지 않음 |
| deferred probe | probe 연기 | 공급자가 아직 없어서 나중에 다시 probe |
| synchronous external abort | 동기 외부 abort | 버스가 접근에 에러로 응답 — 없는 주소 접근 등 |
| framing error | 프레이밍 에러 | UART stop 비트 자리에서 1이 아닌 값을 읽음 |
| fdt fixup | DT 수정 | 부트로더가 점프 직전에 memory·bootargs 등을 채워 넣는 것 |

---

## 14. 요약 & 체크리스트

bring-up은 "믿을 수 있는 영역"을 아래층부터 한 칸씩 넓히는 일이다: 전원 → 클럭 → 리셋 → JTAG → 콘솔 → DDR → 부트로더 → 최소 DT 커널 → DT 노드 하나씩 → 주변장치 검증 → stress → 공장. 각 단계에 측정 가능한 완료 조건을 두고, 다음 단계로 가기 전에 "고장 나면 무엇으로 볼지"를 확보한다. QEMU 랩에서는 `-kernel`이 DT를 고쳐 준다는 것을 확인하고 40바이트 스텁으로 우회한 뒤, 6개 노드짜리 최소 DT에서 PSCI → RTC/GPIO → virtio-mmio → PCIe 순으로 늘렸다. 일부러 틀린 DT 네 가지는 각각 다른 지점에서 다른 증상을 냈다: interrupt-parent 누락 = 시간 정지, 메모리 과대 = `paging_init`의 external abort(맨이름 earlycon으로는 무출력), stdout-path 누락 = 침묵, UART 클럭 오류 = 분주비 오류와 깨진 글자.

- [ ] bring-up 12단계와 각 단계의 exit criteria를 말할 수 있다
- [ ] 전류 제한 bench supply, 레일·시퀀싱 측정 방법을 설명할 수 있다
- [ ] UART 무출력과 깨진 글자의 원인 후보를 구분할 수 있다
- [ ] DDR training과 구조적 메모리 테스트(data/address/own-address)를 설명할 수 있다
- [ ] 최소 DT 6요소를 쓰고 QEMU에서 shell까지 부팅할 수 있다
- [ ] `earlycon` 맨이름과 `earlycon=드라이버,주소`의 등록 시점 차이를 설명할 수 있다
- [ ] JTAG/gdb로 PC·ESR·FAR을 읽어 크래시와 무한 대기를 구분할 수 있다
- [ ] DT 노드를 의존성 순서로 켜는 이유와 `devices_deferred` 사용법을 설명할 수 있다
- [ ] PL011 분주비로 실제 baud를 계산할 수 있다
- [ ] 부트로더(QEMU `-kernel`, U-Boot)가 DT를 고친다는 점과 그 함정을 설명할 수 있다
- [ ] Don의 RF·SSD 경험을 각 단계에 연결한 STAR 답변을 90초 안에 할 수 있다

---

## 참고 자료

- Linux 커널 문서 — The kernel's command-line parameters (`earlycon`): https://docs.kernel.org/admin-guide/kernel-parameters.html
- Linux 커널 문서 — Booting AArch64 Linux (부트 프로토콜, x0 = DTB): https://docs.kernel.org/arch/arm64/booting.html
- Linux 커널 문서 — Devicetree usage model: https://docs.kernel.org/devicetree/usage-model.html
- 랩 소스(확인함): `drivers/tty/serial/earlycon.c` (`param_setup_earlycon`), `arch/arm64/kernel/setup.c` (`setup_arch` 292·299·332·334·339행), `drivers/virtio/virtio_mmio.c` (ID 0 placeholder, 621행), `drivers/of/irq.c` (OF_POPULATED, 624행), `drivers/irqchip/irq-gic.c` 1516행, `drivers/clocksource/arm_arch_timer.c` 1187행, `drivers/clk/clk-fixed-rate.c` 197행 — elixir.bootlin.com의 v6.18 트리에서도 같은 경로
- U-Boot 문서 — i.MX8M Plus EVK (DDR training 펌웨어 파일): 랩 소스 `doc/board/nxp/imx8mp_evk.rst`, https://docs.u-boot.org/en/latest/board/nxp/imx8mp_evk.html
- U-Boot `mtest` 명령: 랩 소스 `cmd/mem.c` `do_mem_mtest`
- Arm — PL011 UART Technical Reference Manual (IBRD/FBRD 분주식)
- QEMU 문서 — Generic Loader (`-device loader`): https://www.qemu.org/docs/master/system/generic-loader.html
- QEMU 문서 — Arm virt 머신: https://www.qemu.org/docs/master/system/arm/virt.html
- Bootlin — Embedded Linux system development 교육 자료 (보드 bring-up·DT 장): https://bootlin.com/training/embedded-linux/
- 이 노트 시리즈: C1·C2·C4·C5·C6(부팅), D2·D4·D7(DT), G3(platform driver), H3·H4·H6·H11(clk/pinctrl/irq/PCIe), M1·M4·M11(디버깅), N5(전원·thermal), P4(회로도→DT), P6(테스트·CI)
