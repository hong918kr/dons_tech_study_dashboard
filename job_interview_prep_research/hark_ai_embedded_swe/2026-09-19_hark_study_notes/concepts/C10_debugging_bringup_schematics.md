# C10. 디버깅 · 보드 bring-up · 회로도 읽기 — JTAG/SWD에서 HardFault 해석, 스코프, 회로도까지

> **이 노트를 다 읽으면**: JTAG/SWD 배선과 프로토콜을 그리고 "연결이 안 된다"를 층별로 진단할 수 있다 · HardFault 핸들러를 직접 써서 CFSR/HFSR/stacked PC로 원인 코드 줄을 찾을 수 있다 · 회로도에서 power tree, pull-up, level shifter, test point를 읽고 bring-up 순서를 세울 수 있다 · 면접에서 Apple/SSD 디버깅 경험을 구조화된 스토리로 말할 수 있다
> **JD 연결**: "Debug complex hardware-software interactions using logic analyzers, oscilloscopes, and JTAG" · "Comfort reading schematics and working alongside hardware engineers during board bring-up" · "Experience with embedded debugging tools and workflows"
> **Don 기준 난이도**: JTAG/Trace32, DSO, LA, 인터페이스 root cause는 강점 / Cortex-M 전용 도구(SWO/ITM, RTT, J-Link+GDB, OpenOCD)와 CFSR 비트 해석, 컨슈머 기기 bring-up 체크리스트는 정리가 필요

---

## 0. 큰 그림

펌웨어 디버깅 도구는 "어디를 보느냐"로 나눌 수 있다. **CPU 내부**(레지스터, 메모리, 명령 흐름)는 디버그 프로브가, **핀 위의 디지털 신호**(버스 프로토콜, 타이밍)는 로직 분석기가, **전기적 현실**(전압 레벨, 램프, 링잉, 노이즈, 전류)은 오실로스코프와 전류 측정 장비가 본다. 좋은 디버거는 문제를 보고 어느 층의 문제인지부터 가른다.

```
           ┌──────────────── 보드 (DUT) ─────────────────────────────────────┐
 PC        │   ┌──────────── MCU / SoC ─────────────┐                        │
┌──────┐   │   │ Core ── DWT/FPB/ITM ── DAP(DP/AP) ◄─┼── SWD/JTAG ◄── 프로브 ─┼── USB ── GDB / Trace32 / Ozone
│ GDB  │   │   │   │                     TPIU/ETM ──┼── SWO / trace ──────────┘   (CPU 내부 상태)
│ IDE  │   │   │   ├─ I2C/SPI/I2S/UART ─────────────┼─── 핀 ──► 로직 분석기          (디지털 타이밍·프로토콜)
│ 로그 │   │   │   └─ GPIO 토글(계측용) ────────────┼─── 핀 ──►                      
└──────┘   │   └───────────────▲────────────────────┘                        │
           │     PMIC ─ buck/LDO ─┘ 전원 레일 ────────────► 오실로스코프       (아날로그: 램프, 리플, 링잉)
           │     배터리 ─ 전류 ────────────────────────────► 전류 프로브/PPK2  (전력)
           └──────────────────────────────────────────────────────────────────┘
                              ▲
                  회로도 = 이 모든 것의 지도 (어느 핀이 어느 net, 어디에 test point)
```

### 0.1 층별 진단 원칙

1. **전원**: 레일이 스펙 전압인가, 순서대로 올라왔나, 리플은?
2. **클럭**: 크리스탈/오실레이터가 돌고 있나, PLL이 lock 됐나?
3. **리셋**: 리셋 핀이 해제됐나, 리셋 원인 레지스터는?
4. **디버그 연결**: 프로브가 DP IDCODE를 읽을 수 있나?
5. **코드 실행**: PC가 reset handler에서 main까지 가나?
6. **주변장치**: 클럭 enable, 핀 mux, 신호가 핀에 나오나?
7. **프로토콜/타이밍**: LA로 디코드, 스펙 타이밍과 비교
8. **시스템**: RTOS, 전력 상태, 경쟁 조건, 온도·전압 의존성

아래 층이 확실하지 않으면 위 층 디버깅은 시간 낭비다. "I2C가 안 된다"는 버그의 상당수가 1~3층(레일 순서, pull-up 전원이 꺼져 있음, 리셋 상태의 센서)에서 끝난다.

> **Don 경험과 연결**: Apple에서 새 무선 칩이 전체 시스템을 만날 때 PCIe/I2C/SPMI/RFFE 장애를 root cause 하던 방식이 정확히 이 층별 분리다. 면접에서는 이 8단계를 "내가 쓰는 체크리스트"로 제시하면 된다.

---

## 1. JTAG vs SWD

### 1.1 신호와 배선

| 항목 | JTAG (IEEE 1149.1) | SWD (Arm Serial Wire Debug) |
|---|---|---|
| 신호 | TCK, TMS, TDI, TDO, (nTRST 옵션) | SWCLK, SWDIO (양방향) |
| 핀 수 | 4~5 + GND, VTref, nRESET | 2 + GND, VTref, nRESET (+SWO 옵션) |
| 다중 디바이스 | daisy chain(TDO→TDI)으로 여러 칩 연결 | 기본은 점대점. SWD v2(multi-drop)는 지원 칩에서만 |
| 용도 | 디버그 + boundary scan(보드 납땜 검사) | 디버그 전용 |
| 속도 | 수~수십 MHz (프로브·배선 의존) | 비슷, 핀이 적어 소형 기기에 유리 |
| 대표 대상 | Cortex-A/R, FPGA, 다칩 보드 | Cortex-M 거의 전부 |

Cortex-M 칩 대부분은 SWJ-DP를 가져서 같은 핀(TMS=SWDIO, TCK=SWCLK)을 JTAG 또는 SWD로 쓸 수 있다. 프로브는 특정 비트 시퀀스(ADIv5에서 JTAG→SWD 전환 코드 `0xE79E`, 앞뒤로 50클럭 이상 SWDIO high인 line reset)를 보내 모드를 바꾼다.

### 1.2 Arm 10핀 Cortex Debug 커넥터 (0.05인치 피치)

```
            ┌─────────┐
  VTref  1  │ ●     ● │  2  SWDIO / TMS
   GND   3  │ ●     ● │  4  SWCLK / TCK
   GND   5  │ ●     ● │  6  SWO / TDO
   KEY   7  │ ✕     ● │  8  NC / TDI
GNDDetect 9 │ ●     ● │ 10  nRESET
            └─────────┘
```

- **VTref**: 타깃의 I/O 전압을 프로브에 알려주는 핀. 프로브는 이 전압에 맞춰 레벨을 바꾼다. **VTref가 0V면 프로브는 "타깃 전원 없음"으로 판단**하고 연결을 거부한다. 1.8V I/O 보드에서 흔한 bring-up 함정이다.
- **nRESET**: 프로브가 칩을 리셋하거나 "connect under reset"(리셋을 잡은 채 연결)할 때 쓴다. 펌웨어가 SWD 핀을 GPIO로 바꿔 버리거나 바로 deep sleep에 들어가 연결이 안 될 때 구세주다.
- 커스텀 소형 보드에서는 커넥터 대신 **test point + pogo pin 지그**로 SWDIO/SWCLK/GND/VTref/nRESET만 뽑는 경우가 많다. 회로도에서 이 test point를 찾는 것이 bring-up 첫 일이다(8절).

### 1.3 SWD 한 번의 트랜잭션

```
SWCLK  _|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_ ... _|‾|_|‾|_
SWDIO  [S][APnDP][RnW][A2][A3][Par][Stop][Park] [Trn] [ACK0 ACK1 ACK2] [Trn?] [DATA 32bit][Par]
        1                                  0  1          호스트→타깃 ──┘ 타깃→호스트 ──┘
        └───────── 8비트 요청 (호스트 구동) ──────┘       ACK: OK=0b001, WAIT=0b010, FAULT=0b100
```

- 요청 8비트: Start(1), APnDP(DP냐 AP냐), RnW(읽기/쓰기), A[2:3](레지스터 주소), Parity, Stop(0), Park(1).
- **Turnaround(Trn)**: SWDIO 방향이 바뀌는 1클럭. 이 때문에 SWDIO에는 약한 pull-up이 권장된다.
- **ACK**: OK, WAIT(타깃이 바쁨, 재시도), FAULT(sticky error, DP CTRL/STAT에서 확인 후 ABORT로 해제). 응답이 아예 없으면(라인이 high로 떠 있음) 보통 전원/배선/핀 mux 문제다.

### 1.4 DP와 AP: 프로브가 메모리를 읽는 방법

```
프로브 ──SWD──► DP (Debug Port)          ┌─► MEM-AP ──► 시스템 버스(AHB) ──► 메모리, 주변장치, SCB, DWT...
                 DPIDR(IDCODE)            │    CSW  : 접근 크기, auto-increment
                 CTRL/STAT (전원 요청)     │    TAR  : 접근할 주소
                 SELECT ──────────────────┤    DRW  : 데이터 (읽기/쓰기)
                 RDBUFF                   └─► (다른 AP: 다른 코어, 벤더 전용 AP 등)
```

"J-Link가 메모리 0x2000_0000을 읽는다"는 실제로 **SELECT로 AP 선택 → TAR에 주소 쓰기 → DRW 읽기**다. CPU를 멈추지 않고도 메모리를 읽을 수 있는 이유(RTT, live watch)가 이 구조다. 디버그 연결 절차의 첫 단계는 DPIDR 읽기, 다음은 CTRL/STAT에 CDBGPWRUPREQ/CSYSPWRUPREQ를 써서 디버그 도메인 전원을 켜는 것이다. **"IDCODE는 읽히는데 메모리 접근이 FAULT"**라면 칩이 디버그 도메인 전원을 못 켜는 상태(깊은 sleep, 보안 잠금, 클럭 없음)를 의심한다.

### 1.5 JTAG TAP 상태 머신 (요약)

JTAG은 TMS 값으로 16-상태 TAP 컨트롤러를 움직여 IR(명령 레지스터)과 DR(데이터 레지스터)을 시프트한다. TMS를 1로 5클럭 유지하면 어느 상태에서든 Test-Logic-Reset으로 간다.

```
Test-Logic-Reset ─0─► Run-Test/Idle ─1─► Select-DR ─0─► Capture-DR ─► Shift-DR (TDI→TDO 시프트)
                                             │1                       ─1─► Exit1-DR ─1─► Update-DR
                                             ▼
                                         Select-IR ─0─► Capture-IR ─► Shift-IR ─► Exit1-IR ─► Update-IR
```

daisy chain에서는 IR 길이의 합, 디바이스 순서가 맞아야 한다. Trace32나 OpenOCD 설정에서 IRPRE/IRPOST, `-irlen` 같은 값을 넣는 이유다.

> **Don 경험과 연결**: SSD 컨트롤러의 멀티코어(Cortex-R8 여러 개 + M0+)를 Trace32로 붙일 때 JTAG 체인과 AP 선택을 설정했던 경험이 그대로 이어진다. Cortex-M 세계에서는 대부분 SWD 단일 코어이고, 도구가 J-Link/ST-Link/CMSIS-DAP로 바뀔 뿐이다.

---

## 2. 디버그 프로브와 소프트웨어 흐름

### 2.1 프로브 비교

| 프로브 | 인터페이스 | 강점 | 약점 | 주 사용처 |
|---|---|---|---|---|
| SEGGER J-Link | JTAG/SWD, SWO, RTT | 빠른 flash 다운로드, 광범위한 칩 지원, RTT, Ozone | 상용 라이선스 | Cortex-M 업계 표준 |
| ST-Link | SWD/JTAG, SWO | 저렴, Nucleo 보드 내장 | 주로 ST 칩 | STM32 |
| CMSIS-DAP (DAPLink 등) | SWD/JTAG | 오픈 표준, 저렴, 다수 개발 보드 내장 | 속도 느릴 수 있음 | 개발 보드, OpenOCD/pyOCD |
| Lauterbach TRACE32 | JTAG/SWD + 병렬 trace 포트 | 멀티코어, ETM 실시간 trace, 강력한 스크립트(PRACTICE) | 고가 | SoC, 자동차, SSD 컨트롤러 |
| Nordic nRF DK 내장(J-Link OB) | SWD | DK에 포함 | 외부 타깃은 제한적 | nRF 개발 |

### 2.2 GDB + OpenOCD / J-Link GDB Server 흐름

```
arm-none-eabi-gdb app.elf ──TCP :3333(OpenOCD) 또는 :2331(J-Link)──► GDB server ──USB──► 프로브 ──SWD──► 타깃
     (심볼, 소스)                                                      (프로토콜 변환, flash 알고리즘)
```

```sh
# 1) OpenOCD로 CMSIS-DAP 프로브 + nRF52 타깃 연결
openocd -f interface/cmsis-dap.cfg -f target/nrf52.cfg

# 또는 J-Link GDB server
JLinkGDBServer -device nRF52840_xxAA -if SWD -speed 4000

# 2) 다른 터미널에서 GDB
arm-none-eabi-gdb build/app.elf
(gdb) target extended-remote :3333        # J-Link라면 :2331
(gdb) monitor reset halt                  # 리셋 후 첫 명령에서 정지
(gdb) load                                # ELF를 flash에 쓰기
(gdb) break main
(gdb) continue
(gdb) info registers                      # r0~r12, sp, lr, pc, xpsr
(gdb) x/8wx $sp                           # 스택 8워드 16진 덤프
(gdb) watch g_state                       # 데이터 watchpoint (DWT 사용)
(gdb) bt                                  # backtrace (프레임 정보가 있을 때)
```

- `monitor ...`는 GDB가 해석하지 않고 GDB server에 그대로 넘기는 명령이다. OpenOCD와 J-Link의 monitor 명령 문법은 다르다.
- `-speed 4000`은 SWD 클럭 4MHz. 배선이 길거나 신호 품질이 나쁘면 **속도를 낮추는 것이 첫 번째 진단**이다. 낮춘 속도에서 되면 신호 무결성 문제다.

### 2.3 Trace32 ↔ J-Link+GDB 대응표 (Don용)

| 하고 싶은 일 | Trace32 (PRACTICE) | GDB (+OpenOCD/J-Link) |
|---|---|---|
| CPU 선택/연결 | `SYStem.CPU <칩>` → `SYStem.Up` | GDB server 실행 시 `-device` / target cfg |
| ELF 로드 | `Data.LOAD.Elf app.elf` | `load` |
| 브레이크포인트 | `Break.Set main` | `break main` |
| 레지스터 보기 | `Register.view` | `info registers` |
| 메모리 덤프 | `Data.dump 0x20000000` | `x/64wx 0x20000000` |
| 스크립트 자동화 | `.cmm` 스크립트 | `.gdbinit`, GDB Python API |
| 명령 trace | ETM trace (`Trace.List`) | 보통 없음 (J-Trace + Ozone이면 가능) |

> **Don 경험과 연결**: "Trace32로 멀티코어 SSD 컨트롤러를 디버깅했고, Cortex-M 쪽 J-Link/GDB 흐름은 같은 개념(DAP, AP, FPB/DWT)의 다른 프런트엔드"라고 말하면 도구 차이를 걱정하는 면접관을 안심시킬 수 있다.

---

## 3. Breakpoint와 watchpoint: FPB와 DWT

### 3.1 하드웨어 breakpoint (FPB)

flash의 코드는 디버거가 `BKPT` 명령으로 덮어쓸 수 없다(RAM 코드라면 software breakpoint 가능). 그래서 Cortex-M은 **FPB(Flash Patch and Breakpoint)** 유닛의 비교기로 "이 주소의 명령을 가져오면 멈춰라"를 하드웨어로 구현한다.

| 코어 | HW breakpoint 수 | watchpoint(DWT 비교기) 수 |
|---|---|---|
| Cortex-M0/M0+ | 최대 4 | 최대 2 |
| Cortex-M3/M4 | 보통 6 (+ literal 비교기 2) | 보통 4 |
| Cortex-M7 | 구현에 따라 4 또는 8 | 구현에 따라 2 또는 4 |
| Cortex-M33/M55 | 구현 옵션 (보통 4~8) | 구현 옵션 (보통 4) |

개수는 칩 벤더가 구현 옵션으로 정하므로 **데이터시트/`FP_CTRL`의 NUM_CODE 필드로 확인**한다. GDB에서 breakpoint를 너무 많이 걸면 "Cannot insert hardware breakpoint" 오류가 나는 이유다.

### 3.2 데이터 watchpoint (DWT)

DWT 비교기는 "이 주소에 쓰기/읽기가 일어나면 멈춰라"를 한다. 메모리 오염 버그의 결정적 도구다.

```
증상: g_rx_count 가 가끔 엉뚱한 값이 된다
(gdb) watch g_rx_count            # 쓰기 watchpoint
(gdb) continue
Hardware watchpoint 2: g_rx_count
Old value = 12
New value = 1094795585            # 0x41414141 = "AAAA" → 누군가 문자열을 넘치게 복사!
(gdb) bt                          # 누가 썼는지 바로 보인다 (watchpoint는 쓰기 직후 명령에서 정지)
```

watchpoint는 코드를 전혀 바꾸지 않고, 속도 저하도 없다. 단 주소 범위는 비교기 마스크 방식이라 정렬된 2의 거듭제곱 크기로만 감시할 수 있다(ARMv7-M 기준).

### 3.3 코드에서 직접 watchpoint 걸기 (디버거 없이)

필드에서만 재현되는 오염이라면 DWT를 펌웨어가 직접 설정하고 **DebugMonitor 예외**로 잡을 수 있다(ARMv7-M, 디버거가 halting debug를 쓰지 않을 때). 레지스터 이름은 `DWT->COMP0`, `DWT->MASK0`, `DWT->FUNCTION0`과 `CoreDebug->DEMCR`의 `MON_EN` 비트이며, ARMv8-M은 비교기 레지스터 구조가 다르므로(FUNCTION 레지스터 인코딩 변경) 코어별 Architecture Reference Manual을 확인한다.

---

## 4. printf 없이 로그 보기: ITM/SWO와 SEGGER RTT

### 4.1 비교

| 방식 | 핀 | 속도 | CPU 비용 | 전제 | 비고 |
|---|---|---|---|---|---|
| UART printf | TX 1핀 | 115200bps~수 Mbps | 블로킹이면 큼 | UART 여유 | 양산 로그에도 사용 |
| ITM + SWO | SWO 1핀 | 수 Mbps | 작음(stimulus 레지스터 쓰기) | Cortex-M3 이상, SWO 핀 배선 | M0/M0+에는 ITM 없음 |
| SEGGER RTT | 추가 핀 없음 (SWD 사용) | 빠름 | 매우 작음(RAM 링버퍼 복사) | J-Link 연결 | 디버거가 백그라운드로 메모리 읽음 |
| semihosting | 없음 | 매우 느림 | 매우 큼(BKPT로 CPU 정지) | 디버거 연결 | **디버거 없으면 HardFault** |

### 4.2 ITM/SWO 코드

```c
#include "device.h"   /* CMSIS-Core: ITM_SendChar() 제공 */

int _write(int fd, const char *buf, int len)   /* newlib의 printf 출력 훅 */
{
    (void)fd;
    for (int i = 0; i < len; i++) {
        ITM_SendChar((uint32_t)buf[i]);  /* ITM 활성 + stimulus port 0 활성일 때만 전송 */
    }
    return len;
}
```

- CMSIS의 `ITM_SendChar()`는 `ITM->TCR`의 ITMENA와 `ITM->TER`의 포트 0 enable을 확인하고, 포트가 준비될 때까지 기다린 뒤 `ITM->PORT[0]`에 쓴다. 디버거가 연결 안 돼 ITM이 꺼져 있으면 바로 반환하므로 양산 빌드에서도 안전하다.
- SWO 출력 형식과 속도는 TPIU 레지스터(`TPI->SPPR`: 2 = NRZ/UART, 1 = Manchester, `TPI->ACPR`: 분주비)로 정한다. 보통 디버거 소프트웨어가 설정해 주지만, 디버거와 타깃의 **SWO 클럭 설정이 코어 클럭과 맞지 않으면 깨진 글자**가 나온다. 코어 클럭을 바꾸는 코드(PLL, DVFS) 이후 SWO가 깨지는 버그가 흔하다.

### 4.3 SEGGER RTT

```c
#include "SEGGER_RTT.h"

void app_log_init(void)
{
    SEGGER_RTT_Init();                                   /* RAM에 "SEGGER RTT" 제어 블록 생성 */
    SEGGER_RTT_WriteString(0, "boot\n");                 /* 채널 0(up buffer)에 쓰기 */
    SEGGER_RTT_printf(0, "reset cause=0x%08x\n", 0x4u);  /* 간단한 printf */
}
```

RTT의 원리: RAM에 제어 블록(식별 문자열 + 링버퍼 포인터들)이 있고, 타깃은 링버퍼에 쓰기만 한다. J-Link는 SWD의 MEM-AP로 **CPU를 멈추지 않고** 그 버퍼를 주기적으로 읽는다(1.4절). 그래서 ISR에서 써도 부담이 적다. 버퍼가 가득 찼을 때의 동작(버리기/블록)은 설정으로 정한다.

> **Don 경험과 연결**: SSD에서 RAM 링버퍼 로그를 JTAG로 덤프하던 방식의 표준화된 버전이 RTT다. 개념은 같고 도구가 자동화해 준다.

---

## 5. HardFault 해석

### 5.1 예외 진입 시 하드웨어가 쌓는 스택 프레임

Cortex-M은 예외에 들어갈 때 **하드웨어가 자동으로 8워드를** 현재 스택(MSP 또는 PSP)에 쌓는다. 그래서 C 함수를 그대로 ISR로 쓸 수 있고, fault 원인 분석에서 "fault 직전의 PC"를 알 수 있다.

```
            높은 주소
            ┌──────────────────┐
  SP+0x1C   │ xPSR             │
  SP+0x18   │ PC  (return addr)│ ◄── fault를 일으킨 명령(또는 다음 명령)의 주소
  SP+0x14   │ LR  (R14)        │ ◄── fault 난 함수를 부른 곳 근처
  SP+0x10   │ R12              │
  SP+0x0C   │ R3               │
  SP+0x08   │ R2               │
  SP+0x04   │ R1               │
  SP+0x00   │ R0               │ ◄── 예외 진입 후 SP (MSP 또는 PSP)
            └──────────────────┘
            낮은 주소
FPU 사용 중(lazy stacking 포함)이면: R0~xPSR 위에 S0~S15, FPSCR, reserved가 추가 → 총 26워드(extended frame)
```

예외 진입 시 LR에는 **EXC_RETURN** 값(0xFFFFFFxx)이 들어간다.

| EXC_RETURN 비트 | 의미 |
|---|---|
| bit 2 (SPSEL) | 0 = 프레임이 MSP에, 1 = PSP에 (RTOS task에서 fault면 보통 PSP) |
| bit 3 (Mode) | 0 = Handler 모드로 복귀, 1 = Thread 모드로 복귀 |
| bit 4 (FType) | 0 = FP extended frame, 1 = 기본 프레임 (FPU 있는 코어) |

### 5.2 fault 상태 레지스터 (ARMv7-M / ARMv8-M Mainline)

| 레지스터 | 주소 | 내용 |
|---|---|---|
| CFSR | 0xE000ED28 | Configurable Fault Status = MMFSR[7:0] + BFSR[15:8] + UFSR[31:16] |
| HFSR | 0xE000ED2C | HardFault Status: FORCED(bit30), VECTTBL(bit1), DEBUGEVT(bit31) |
| MMFAR | 0xE000ED34 | MemManage fault 주소 (MMARVALID=1일 때만 유효) |
| BFAR | 0xE000ED38 | BusFault 주소 (BFARVALID=1일 때만 유효) |
| SHCSR | 0xE000ED24 | MemManage/BusFault/UsageFault 개별 enable 비트 등 |

CFSR 주요 비트:

| 비트 | 이름 | 소속 | 뜻 | 흔한 원인 |
|---|---|---|---|---|
| 0 | IACCVIOL | MMFSR | 명령 fetch MPU 위반 | XN 영역 실행, 함수 포인터 오염 |
| 1 | DACCVIOL | MMFSR | 데이터 접근 MPU 위반 | NULL 근처 접근(MPU로 보호 시), 스택 가드 침범 |
| 3 / 4 | MUNSTKERR / MSTKERR | MMFSR | 예외 복귀/진입 스택 중 MPU 위반 | 스택 오버플로 |
| 7 | MMARVALID | MMFSR | MMFAR 유효 | — |
| 8 | IBUSERR | BFSR | 명령 fetch 버스 에러 | 존재하지 않는 주소로 점프 |
| 9 | PRECISERR | BFSR | 정확한 데이터 버스 에러, BFAR 유효 가능 | 클럭 꺼진 주변장치 접근, 없는 주소 |
| 10 | IMPRECISERR | BFSR | 부정확한 버스 에러(write buffer) | 버퍼된 쓰기 실패, stacked PC가 원인 명령이 아님 |
| 11 / 12 | UNSTKERR / STKERR | BFSR | 예외 스택 처리 중 버스 에러 | 스택 포인터가 RAM 밖 |
| 15 | BFARVALID | BFSR | BFAR 유효 | — |
| 16 | UNDEFINSTR | UFSR | 정의 안 된 명령 | 코드 오염, 잘못된 점프 |
| 17 | INVSTATE | UFSR | 잘못된 실행 상태 | Thumb 비트(bit0)=0인 함수 포인터로 BX/BLX |
| 18 | INVPC | UFSR | 잘못된 EXC_RETURN | 스택 오염 후 예외 복귀 |
| 19 | NOCP | UFSR | 코프로세서 없음 | FPU enable(CPACR) 전에 float 명령 |
| 20 | STKOF | UFSR | 스택 한계 초과 (ARMv8-M만, PSPLIM/MSPLIM) | 스택 오버플로 |
| 24 | UNALIGNED | UFSR | 비정렬 접근 트랩 | CCR.UNALIGN_TRP=1이거나 LDM/STRD 비정렬 |
| 25 | DIVBYZERO | UFSR | 0으로 나누기 트랩 | CCR.DIV_0_TRP=1일 때 |

**HFSR.FORCED=1**이면 원래는 MemManage/BusFault/UsageFault였는데 그 핸들러가 enable되지 않았거나(SHCSR) 우선순위 때문에 실행될 수 없어서 HardFault로 **escalation**된 것이다. 그러니 CFSR을 보면 진짜 원인이 있다. **VECTTBL=1**이면 벡터 테이블을 읽다가 버스 에러가 난 것으로, VTOR 설정이나 벡터 테이블 위치를 의심한다.

> Cortex-M0/M0+(ARMv6-M)에는 CFSR/HFSR/MMFAR/BFAR가 **없다.** HardFault 하나뿐이라 stacked PC/LR, 스택 내용, 코드 역추적으로만 판단한다.

### 5.3 HardFault 핸들러: 레지스터와 stacked PC 덤프

```c
#include <stdint.h>
#include "device.h"   /* CMSIS-Core (core_cm4.h 등): SCB, SCnSCB */

/* CFSR 비트 (ARMv7-M ARM B3.2.15 기준) — CMSIS 버전에 따라 매크로 이름이 달라 직접 정의 */
#define CFSR_MMARVALID   (1u << 7)
#define CFSR_BFARVALID   (1u << 15)
#define CFSR_IMPRECISERR (1u << 10)

typedef struct {
    uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr;   /* 하드웨어가 쌓은 순서 그대로 */
} exc_frame_t;

typedef struct {
    uint32_t magic;
    exc_frame_t frame;
    uint32_t exc_return, cfsr, hfsr, mmfar, bfar, sp;
} crash_dump_t;

/* 리셋 후에도 남도록 .noinit(startup이 0으로 지우지 않는 섹션)에 둔다 — 링커 스크립트에 정의 필요 */
__attribute__((section(".noinit"))) volatile crash_dump_t g_crash;

#define CRASH_MAGIC 0xDEADFA17u

void hardfault_c(exc_frame_t *f, uint32_t exc_return)
{
    g_crash.frame      = *f;
    g_crash.exc_return = exc_return;
    g_crash.cfsr       = SCB->CFSR;
    g_crash.hfsr       = SCB->HFSR;
    g_crash.mmfar      = SCB->MMFAR;      /* MMARVALID일 때만 의미 있음 */
    g_crash.bfar       = SCB->BFAR;       /* BFARVALID일 때만 의미 있음 */
    g_crash.sp         = (uint32_t)f;
    g_crash.magic      = CRASH_MAGIC;

    __DSB();
    if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) {
        __BKPT(0);                        /* 디버거가 붙어 있으면 여기서 멈춰서 바로 분석 */
    }
    NVIC_SystemReset();                   /* 양산: 리셋 후 부팅 때 g_crash를 로그/텔레메트리로 */
}

/* naked: 컴파일러가 프롤로그로 스택을 건드리기 전에 어느 스택인지 판별 */
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst   lr, #4        \n"   /* EXC_RETURN bit2: 0=MSP, 1=PSP */
        "ite   eq            \n"
        "mrseq r0, msp       \n"
        "mrsne r0, psp       \n"
        "mov   r1, lr        \n"   /* 두 번째 인자: EXC_RETURN */
        "b     hardfault_c   \n"
    );
}
```

한 줄씩:

- `exc_frame_t`: 5.1절 그림의 순서 그대로. 포인터 하나로 stacked 레지스터를 모두 읽을 수 있다.
- `.noinit` 섹션: startup 코드가 `.bss`처럼 0으로 지우지 않는 RAM 영역. 리셋(전원 차단이 아닌)을 넘어 살아남으므로, 부팅 후 `magic`을 확인해 이전 crash 정보를 로그로 보내고 지운다. 링커 스크립트에 `(NOLOAD)` 섹션으로 정의해야 한다.
- `SCB->CFSR`, `SCB->HFSR`, `SCB->MMFAR`, `SCB->BFAR`: CMSIS-Core의 실제 레지스터 이름. fault 상태 비트는 write-1-to-clear라 읽기만 하면 유지된다.
- 순서가 중요하다: MMFAR/BFAR은 다른 fault가 나면 덮어쓰일 수 있으니 **먼저 CFSR의 VALID 비트와 함께 저장**한다.
- `CoreDebug->DHCSR`의 `C_DEBUGEN`: 디버거가 halting debug를 켰는지. 켜져 있을 때만 `__BKPT`을 쓴다. 디버거 없이 `BKPT`를 실행하면 그 자체가 또 HardFault다. (CMSIS 6에서는 `DCB->DHCSR`, `DCB_DHCSR_C_DEBUGEN_Msk`.)
- `HardFault_Handler`를 `naked` + 어셈블리로 쓰는 이유: 일반 C 함수는 프롤로그에서 스택에 레지스터를 push하므로 SP가 바뀐다. EXC_RETURN의 bit2로 MSP/PSP를 고른 뒤 그 값을 첫 인자(r0)로 C 함수에 넘긴다. 이 어셈블리는 Thumb-2(`ite`)라 **Cortex-M3/M4/M7/M33용**이다. M0+는 `ite`가 없으므로 `tst` + 분기(`bne`)로 바꿔 써야 한다.
- 양산에서는 리셋 전에 watchdog이 먼저 터지지 않게 핸들러를 짧게 유지한다.

### 5.4 stacked PC로 원인 코드 줄 찾기

```sh
# 덤프 결과 예: pc=0x0800_1a3c lr=0x0800_19f1 cfsr=0x0000_8200 bfar=0x4001_3008
arm-none-eabi-addr2line -e build/app.elf -f -C -i 0x08001a3c
#   spi_read_reg
#   drivers/spi.c:142
arm-none-eabi-addr2line -e build/app.elf -f -C 0x080019f0   # LR의 bit0(Thumb)을 빼고 호출자 확인
arm-none-eabi-objdump -d -S build/app.elf --start-address=0x08001a30 --stop-address=0x08001a44
```

위 예시를 해석해 보자.

- `CFSR = 0x00008200`: bit 15(BFARVALID)와 bit 9(PRECISERR) → **정확한 데이터 버스 에러, 주소는 BFAR**.
- `BFAR = 0x40013008`: 주변장치 영역(0x4000_0000대)의 SPI 레지스터 주소(가상의 예).
- `PC`는 `spi_read_reg()`. 가장 흔한 원인은 **그 SPI 주변장치의 버스 클럭 enable 전에 레지스터 접근**, 또는 저전력 모드에서 주변장치 전원이 꺼진 상태 접근이다.

### 5.5 imprecise bus fault 잡는 법

IMPRECISERR은 쓰기 버퍼 때문에 fault가 몇 명령 뒤에 보고되는 것이라 stacked PC가 진짜 원인이 아니다. 디버깅 중에만 Cortex-M3/M4의 write buffer를 끄면 precise로 바뀐다.

```c
SCnSCB->ACTLR |= SCnSCB_ACTLR_DISDEFWBUF_Msk;   /* Cortex-M3/M4: 기본 write buffer 끄기 (성능 저하, 디버그 전용) */
```

Cortex-M7은 ACTLR 비트 구성이 다르므로 TRM을 확인한다(구현마다 다름).

### 5.6 fault를 더 잘 보이게 만드는 설정

```c
void fault_config(void)
{
    /* MemManage/BusFault/UsageFault를 각자 핸들러로 받기 (escalation 대신) */
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk;
    /* 0으로 나누기를 조용히 0으로 만들지 말고 트랩 */
    SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;
}
```

추가로 MPU로 주소 0 근처를 no-access로 만들면 NULL 포인터 역참조가 바로 MemManage fault가 된다(기본 Cortex-M은 0번지가 벡터 테이블이라 NULL 읽기가 에러 없이 성공한다!). RTOS의 스택 오버플로 감지(FreeRTOS `configCHECK_FOR_STACK_OVERFLOW`, ARMv8-M의 PSPLIM)와 함께 쓰면 "필드에서 드물게 HardFault"의 상당수가 원인과 함께 잡힌다.

> **Don 경험과 연결**: SSD FW의 assert/exception 덤프를 NVMe telemetry로 호스트에 올리던 설계가 바로 이 구조(crash 정보를 비휘발 영역에 → 재부팅 후 업로드)다. Hark 기기라면 BLE/Wi-Fi로 폰이나 클라우드에 올라간다. 면접에서 "field crash telemetry 파이프라인"으로 연결해 말할 수 있다.

---

## 6. 로직 분석기

### 6.1 언제 LA, 언제 스코프

| 질문 | 도구 |
|---|---|
| I2C 주소에 ACK가 오나? SPI 모드가 맞나? 바이트 값은? | 로직 분석기 (프로토콜 디코드) |
| ISR 진입부터 응답까지 몇 µs? 이벤트 순서는? | 로직 분석기 + GPIO 토글 |
| 신호 레벨이 VIH를 넘나? 상승 시간은? 링잉/크로스토크는? | 오실로스코프 |
| 전원이 순서대로, 단조롭게 올라오나? | 오실로스코프 (여러 채널) |

로직 분석기는 신호를 임계 전압 기준으로 **0/1로만** 본다. 그래서 "LA에서는 멀쩡한데 칩이 인식을 못 한다"면 아날로그 문제(느린 상승, 중간 레벨, 글리치)이고 스코프로 넘어간다.

### 6.2 샘플링과 트리거

- 샘플 레이트는 신호 최고 주파수의 **최소 4배, 가능하면 10배 이상**으로 잡는다(경험칙). 400kHz I2C면 수 MS/s 이상, 8MHz SPI면 수십~100MS/s 이상.
- 임계 전압을 I/O 전압(1.8V/3.3V)에 맞춘다. 1.8V 신호를 3.3V 임계값으로 보면 절반이 0으로 읽힌다.
- 트리거: 단순 edge보다 **프로토콜 트리거**(예: I2C 주소 0x48에 NACK)나 **펄스 폭 트리거**(예: CS low가 50µs 이상)로 드문 이벤트를 잡는다.
- 긴 캡처가 필요하면 스트리밍 모드(Saleae 등)나 깊은 메모리를 쓰고, 펌웨어에서 **에러 순간에 GPIO 하나를 토글해 트리거로 쓰는 것**이 가장 강력하다.

### 6.3 GPIO 계측과 디코드 예

```
         ┌─ 펌웨어가 에러 감지 시 GPIO_DBG를 토글 → LA 트리거
GPIO_DBG ______________________________________________|‾‾‾‾‾‾
SCL      ‾‾‾‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_|‾|_______________   ← SCL이 low로 멈춤
SDA      ‾‾|___|‾‾‾‾‾|___|‾‾‾|_______|‾‾‾‾‾‾‾‾‾|_____________________ ← SDA low 고정 = 버스 hang
디코드     S  [0x48 W]            [ACK] [0x00]      ... (stuck)
```

위 파형은 I2C 슬레이브가 SDA를 잡은 채 멈춘 전형적인 bus hang이다. 복구는 SCL을 GPIO로 최대 9번 토글해 슬레이브가 바이트를 끝내게 하고 STOP을 보내는 것(C03 노트 참고).

### 6.4 도구

- Saleae Logic (Logic 2 소프트웨어, 프로토콜 디코더, Python 자동화 API)
- sigrok/PulseView (오픈소스, 저가 LA 지원). CLI 예: `sigrok-cli -d fx2lafw --config samplerate=4m --samples 1m -P i2c:scl=D0:sda=D1`
- 스코프의 MSO(디지털 채널) 기능: 아날로그와 디지털을 같은 시간축에서 볼 수 있어 bring-up에 가장 편하다.

---

## 7. 오실로스코프

### 7.1 대역폭과 상승 시간

- 경험칙: 스코프 시스템 대역폭 BW ≈ 0.35 / 상승시간(10~90%). 1ns 상승 에지를 제대로 보려면 350MHz 이상.
- 디지털 신호는 기본 주파수가 아니라 **에지 속도**가 대역폭을 결정한다. 1MHz SPI라도 에지가 2ns면 고대역이 필요하다.
- 프로브도 대역폭이 있다. 스코프 1GHz라도 프로브가 200MHz면 시스템은 200MHz급이다.

### 7.2 프로빙과 그라운드

```
나쁜 예: 15cm 악어클립 그라운드 리드            좋은 예: 그라운드 스프링 (수 mm)
  프로브 팁 ──●── 신호                           프로브 팁 ──●── 신호
              │                                          ╰●── 바로 옆 GND (스프링)
  GND 리드 ~~~~~~~~~~~~~~~ 멀리 있는 GND                  
  → 리드 인덕턴스 + 프로브 캡 = LC 공진 → 가짜 링잉      → 루프 면적 최소, 실제 파형
```

- 10x 패시브 프로브가 기본(입력 캡 낮음, 부하 적음). 1x는 대역폭이 매우 낮다.
- **긴 그라운드 리드는 없는 링잉을 만들어낸다.** 링잉이 보이면 먼저 그라운드 스프링으로 바꿔서 다시 본다.
- 크리스탈 핀을 직접 찍으면 프로브 캡(수 pF)이 부하가 되어 발진이 멈추거나 주파수가 바뀐다. 클럭은 MCO(클럭 출력) 핀이나 버퍼된 출력으로 본다.
- 전원 리플은 AC 커플링, 20MHz 대역 제한, 짧은 그라운드로 측정한다.

### 7.3 전원 램프와 시퀀싱

```
                     t0     t1        t2      t3
VBAT      ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾ 3.8V
VDD_1V8   ______/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾ 1.8V (MCU core/IO)
VDD_3V3   ______________/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾ 3.3V (센서)
PGOOD     ___________________|‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
nRESET    __________________________|‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾   ← 모든 레일 안정 후 리셋 해제
                                        ▲ 부팅 시작
체크: 단조 증가(monotonic)인가, 스펙 램프 시간 안인가, overshoot, 순서(데이터시트 요구),
      리셋 해제가 PGOOD 이후인가, 부하가 켜질 때 dip(brownout)은 없는가
```

스코프 4채널에 레일 3개와 nRESET을 걸고 **single 트리거**(VBAT 상승 에지)로 한 번에 찍는다. 이 한 장이 bring-up 문서의 첫 그림이 된다.

### 7.4 흔한 전기적 증상

| 파형 모습 | 가능한 원인 |
|---|---|
| 상승 에지가 RC 곡선처럼 느림 | pull-up이 너무 크다, 버스 용량이 크다 |
| 논리 레벨이 중간(예: 1.8V 중 0.9V)에 걸림 | 두 드라이버 충돌(contention), 전원 없는 칩으로 back-powering |
| 에지 뒤 큰 링잉 | 임피던스 불일치, 긴 트레이스, 또는 측정 그라운드 문제 |
| 부하 켜질 때 레일이 순간 하강 | 디커플링 부족, 레귤레이터 전류 한계, 배터리 내부 저항 |
| 레일이 올라갔다 내려갔다 반복 | 과전류 보호로 hiccup, 단락 |

---

## 8. 회로도 읽기

### 8.1 기본 기호와 표기

| 표기 | 뜻 | 펌웨어가 볼 포인트 |
|---|---|---|
| Net label (예: `I2C1_SCL`) | 같은 이름끼리 전기적으로 연결 | 이름이 같으면 선이 없어도 연결됨 |
| Off-page connector / 계층 포트 | 다른 페이지로 이어짐 | 페이지 넘겨가며 따라간다 |
| 전원 심볼 (`VDD_1V8`, `VBAT`) | 해당 레일 | 이 칩이 어느 레일에서 전원을 받나 |
| GND 심볼 (신호/아날로그/섀시 구분) | 그라운드 | 아날로그 GND 분리 여부 |
| R / C / L / FB | 저항 / 커패시터 / 인덕터 / 페라이트 비드 | 값과 용도(pull-up, 필터, 디커플링) |
| DNP / NC / NF | 실장 안 함 | **pull-up이 DNP라서 안 된다**는 버그가 흔하다 |
| 0Ω 저항 | 점퍼/옵션 선택 | 설정 변경 지점, rework 포인트 |
| TP (test point) | 측정 패드 | 스코프/LA/pogo 지점 |
| 화살표/`#`/`_N`/`B` 접미사 | active-low (예: `nRESET`, `INT_N`) | 극성 헷갈림 방지 |

### 8.2 power tree 읽기

회로도 첫 페이지나 블록도에 있는 power tree를 먼저 이해한다. 가상의 웨어러블 예:

```
USB-C VBUS ─► 충전기 IC ─► VBAT (Li-ion 3.0~4.4V) ─┬─► Buck1 ─► VDD_1V8 ─┬─► MCU VDDIO, PDM mic, IMU
                  │                                 │     (항상 ON)        └─► Load switch (EN=MCU GPIO) ─► 센서 1V8_SW
                  └─ fuel gauge (I2C)               ├─► Buck2 ─► VDD_0V8 ─► SoC core (EN=PMIC 시퀀스)
                                                    ├─► LDO1  ─► VDD_3V3 ─► 햅틱 드라이버, 일부 센서
                                                    └─► 라디오 모듈 VBAT 직결 (자체 PMU)
PMIC: I2C 제어, PGOOD 출력, 시퀀싱 레지스터, 인터럽트(INT_N → MCU)
```

펌웨어 관점의 질문:

1. MCU가 켜진 상태에서 **꺼질 수 있는 레일**은 무엇인가? 그 레일의 칩과 공유하는 I2C 버스는 어떻게 되나? (꺼진 칩으로 I/O가 전류를 흘리는 back-powering → 버스가 중간 레벨에 걸림)
2. 어떤 load switch/EN 핀을 **펌웨어가** 제어하나? 부팅 시 기본값(pull-up/down)은?
3. PMIC 설정은 OTP 기본값인가, 펌웨어가 I2C로 써야 하나? 잘못 쓰면 레일이 꺼져 스스로를 끌 수 있다.
4. 전류 측정용 shunt 저항이나 0Ω 점퍼가 어디 있나(전력 측정 지점)?

### 8.3 pull-up 저항 계산 (I2C)

I2C는 open-drain이라 pull-up이 버스를 high로 올린다. 값은 두 제약 사이에서 고른다(NXP UM10204 I2C 스펙 기준).

```
Rp(min) = (VDD − VOL(max)) / IOL        예) (3.3V − 0.4V) / 3mA ≈ 967Ω
Rp(max) = tr / (0.8473 × Cb)            예) Fast mode tr = 300ns, Cb = 100pF
                                             → 300e-9 / (0.8473 × 100e-12) ≈ 3.54kΩ
→ 1kΩ~3.5kΩ 사이, 보통 2.2kΩ 선택. Standard mode(tr = 1000ns)면 훨씬 큰 값 허용
```

- 버스 여러 곳에 pull-up이 중복되면 병렬로 합쳐져 너무 작아진다(모듈 보드마다 달려 있는 경우).
- pull-up이 **어느 레일**에 연결됐는지가 중요하다. 스위칭되는 레일이면 그 레일이 꺼질 때 버스 전체가 low가 된다.

### 8.4 level shifter

1.8V MCU와 3.3V 센서를 연결할 때 쓴다.

| 방식 | 예 | 적합 | 주의 |
|---|---|---|---|
| MOSFET 양방향 (BSS138 등 + 양쪽 pull-up) | I2C 레벨 변환 | open-drain 버스 | 상승 속도가 pull-up에 의존, 고속 부적합 |
| 방향 고정 버퍼 (DIR 핀) | SPI, UART | 푸시풀 단방향 | DIR 설정, 전원 순서 |
| 자동 방향 감지(auto-direction) 변환기 | 범용 GPIO | 약한 부하 | 강한 pull-up이나 큰 부하에서 오동작할 수 있어 데이터시트 확인 필수 |

bring-up에서 흔한 버그: level shifter의 **OE(output enable) 핀이 펌웨어 GPIO로 제어**되는데 초기화에서 안 켬, 또는 한쪽 전원이 없어서 변환기가 출력하지 않음.

### 8.5 그 밖에 펌웨어가 회로도에서 반드시 찾을 것

- **Boot/strap 핀**: 부팅 모드, 주소 선택(I2C 주소 핀 ADDR), 인터페이스 선택(SPI/I2C). 풀업/풀다운 값과 DNP 여부.
- **크리스탈과 load 캡**: 32.768kHz(RTC/저전력 타이머)와 메인 크리스탈. 캡 값이 틀리면 주파수가 어긋나 BLE 타이밍이 깨질 수 있다.
- **인터럽트 핀**: open-drain인지(외부 pull-up 필요), active-low인지, MCU의 wake 가능 핀에 연결됐는지.
- **디버그/테스트 접근**: SWD TP, UART 콘솔 TP, 주요 레일 TP, 버스 TP. 없다면 EVT 전에 추가 요청(DFT, design for test).
- **핀 mux 충돌**: 같은 핀이 SWD와 GPIO를 겸하는지. 펌웨어가 SWD 핀을 GPIO로 재설정하면 디버거가 끊긴다.
- **ESD/직렬 저항**: 신호선의 직렬 저항(22~33Ω 등)은 에지를 느리게 해 고속 신호 타이밍에 영향.

> **Don 경험과 연결**: Apple에서 RFFE/SPMI 같은 PMIC·라디오 제어 버스를 디버깅하며 power tree와 레일 시퀀스를 함께 봤던 경험이 이 절 전체다. 면접에서는 "회로도에서 제일 먼저 power tree와 reset/clock, 그다음 내가 드라이버를 쓸 버스의 pull-up과 레벨을 확인한다"라고 순서로 말한다.

---

## 9. 보드 bring-up 체크리스트

새 보드(proto/EVT)를 처음 받았을 때의 순서다. 각 단계의 결과를 **bring-up 로그**(보드 시리얼별)로 남긴다.

### 9.1 전원 인가 전

- [ ] 육안 검사: 부품 방향(다이오드, IC 1번 핀), 누락, 솔더 브리지(특히 BGA 주변)
- [ ] 회로도/BOM 대비 rework 목록 확인 (이 보드 리비전에서 알려진 수정)
- [ ] 각 레일-GND 간 저항 측정: 단락(수 Ω 이하) 없는지. 정상값을 이전 보드와 비교
- [ ] 전류 제한 벤치 파워서플라이 준비 (예상 소비 전류의 약간 위로 제한)

### 9.2 첫 전원

- [ ] 전류 제한 걸고 전원 인가, 전류가 예상 범위인지 (과전류면 즉시 차단)
- [ ] 각 레일 전압 측정 (TP에서 DMM), 스코프로 램프·순서·PGOOD·리셋 해제 확인 (7.3절)
- [ ] 열화상 카메라나 손등으로 비정상 발열 부품 확인
- [ ] 메인 클럭/32kHz 클럭 발진 확인 (MCO 출력 또는 버퍼 출력)

### 9.3 디버그 연결과 첫 코드

- [ ] VTref 전압 확인 → 프로브로 DP IDCODE 읽기 (안 되면: SWD 속도 낮추기, connect under reset, nRESET 상태 확인)
- [ ] 메모리 읽기/쓰기: SRAM 패턴 테스트(walking 1s, 주소 라인 테스트)
- [ ] 최소 펌웨어: 레지스터 직접 GPIO 토글(LED 또는 TP) → 코드 실행 확인
- [ ] 클럭 트리 설정 후 GPIO 토글 주기를 스코프로 재서 실제 코어 클럭 검증
- [ ] UART 콘솔 또는 RTT 로그 출력
- [ ] 리셋 원인 레지스터 출력 (POR, 브라운아웃, watchdog, 소프트웨어 리셋 구분)

### 9.4 주변장치 하나씩

- [ ] 외부 flash: JEDEC ID 읽기 → 쓰기/읽기/지우기 → XIP
- [ ] I2C 버스 스캔: 회로도상 주소 목록과 비교 (없는 장치 = 전원/리셋/주소 핀/pull-up)
- [ ] 각 센서 WHO_AM_I 레지스터 읽기
- [ ] PMIC/fuel gauge 레지스터 읽기, 레일 설정 확인
- [ ] 오디오: PDM/I2S 클럭 주파수 스코프 측정, 녹음 → PC로 덤프해 들어보기
- [ ] 라디오: 벤더 FW 다운로드, HCI 리셋 응답, 기본 advertising/scan
- [ ] 햅틱, 디스플레이, 버튼 등 나머지

### 9.5 시스템 수준

- [ ] sleep 전류 측정 (목표 대비), 각 전원 상태 전환
- [ ] 온도/전압 코너 (저전압 배터리, 고온) 에서 반복 부팅 수백 회
- [ ] 결과 요약을 HW 팀과 공유: 보드별 이슈, rework 제안, 다음 리비전 요청 (예: TP 추가)

> **Don 경험과 연결**: SSD 컨트롤러의 FPGA pre-silicon bring-up과 post-silicon 첫 부팅, Apple의 새 무선 칩 통합에서 이미 이 흐름을 여러 번 돌렸다. 컨슈머 기기에서 추가되는 것은 배터리/충전 경로, 오디오, 라디오 모듈, 그리고 작은 폼팩터라서 **test point가 부족하다**는 점이다. 그래서 EVT 이전 회로도 리뷰 때 TP를 요청하는 것이 펌웨어의 일이다.

---

## 10. 흔한 고장 패턴 (증상 → 첫 확인)

| 증상 | 가장 먼저 의심 | 확인 방법 |
|---|---|---|
| 프로브가 연결 안 됨 | VTref 없음, SWD 핀 mux 변경, 칩이 deep sleep | VTref 측정, connect under reset, SWD 속도 낮추기 |
| 일부 보드만 I2C 센서 무응답 | pull-up DNP/값 차이, 센서 전원 순서, 주소 핀 floating, 납땜 | LA로 ACK 확인, 스코프로 상승 시간, X-ray/리플로우 |
| 부팅이 가끔 멈춤 | 전원 램프 비단조, 리셋이 레일보다 먼저 해제, 크리스탈 기동 시간 | 스코프 single 캡처, 리셋 원인 레지스터 |
| 무선 켜면 리셋 | TX 버스트 전류로 레일 dip → 브라운아웃 | 전류 프로브 + 레일 동시 캡처, 리셋 원인 |
| 특정 온도에서만 실패 | 크리스탈 주파수 drift, 타이밍 margin, 납땜 크랙 | 열풍/냉각 스프레이로 국부 가열, 주파수 측정 |
| 필드에서 드물게 HardFault | 스택 오버플로, 경쟁 조건, 저전력 복귀 후 주변장치 미초기화 | crash dump(CFSR, stacked PC) 텔레메트리, 스택 watermark |
| 딥슬립 전류가 목표의 10배 | floating 입력 핀, 꺼지지 않은 주변장치, back-powering | 핀 상태 표 검토, 레일별 전류, 하나씩 끄기 |
| 오디오에 주기적 클릭 | DMA overrun, 버퍼 경계 처리, 클럭 불일치 | overrun 카운터, GPIO 토글로 처리 시간, PDM CLK 측정 |
| SWO 로그 글자 깨짐 | 코어 클럭 변경 후 SWO 분주 불일치 | 디버거 SWO 클럭 설정과 실제 코어 클럭 비교 |
| 디버거 붙이면 버그 사라짐 | 디버거가 TRCENA/클럭을 켜 줌, 타이밍 변화 | 디버거 없이 GPIO/로그로 재현, 초기화 코드 검토 |

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| HardFault 핸들러를 일반 C 함수로 작성 | stacked 레지스터가 엉뚱함 | 프롤로그가 SP를 바꿈 | naked 어셈블리로 MSP/PSP 선택 후 C로 전달 |
| MMFAR/BFAR를 VALID 비트 없이 신뢰 | 엉뚱한 주소를 원인으로 판단 | 유효하지 않은 값 | CFSR의 MMARVALID/BFARVALID 먼저 확인 |
| IMPRECISERR에서 stacked PC만 봄 | 원인 아닌 줄을 수정 | write buffer 지연 | 디버그 시 DISDEFWBUF, 직전 쓰기들 검토 |
| 디버거 없이 BKPT/semihosting 실행 | 양산 보드에서만 HardFault | 디버그 모니터 없음 | DHCSR.C_DEBUGEN 확인, 양산 빌드에서 제거 |
| 긴 그라운드 리드로 측정 | 없는 링잉을 "SI 문제"로 보고 | 측정 루프 인덕턴스 | 그라운드 스프링, 차동 프로브 |
| 크리스탈 핀을 직접 프로빙 | 측정할 때만 클럭 멈춤 | 프로브 캡 부하 | MCO/버퍼 출력으로 측정 |
| LA 임계 전압을 3.3V로 둔 채 1.8V 신호 측정 | 비트 누락, 디코드 오류 | 임계값 불일치 | 임계 전압을 I/O 레벨에 맞춤 |
| 펌웨어가 SWD 핀을 GPIO로 재설정 | 부팅 후 디버거 연결 불가 | 핀 mux | connect under reset, 디버그 빌드에서 보호 |
| 회로도의 DNP 표기를 놓침 | "배선은 있는데" 신호 없음 | 부품 미실장 | BOM/조립도와 함께 읽기 |

---

## 12. 면접에서 이렇게 말한다

**Q.** Walk me through how you'd debug a HardFault on a Cortex-M.

**A.** 핸들러에서 EXC_RETURN bit2로 MSP/PSP를 골라 stacked 프레임을 얻고, stacked PC/LR과 CFSR, HFSR, MMFAR, BFAR를 저장한다. HFSR.FORCED면 escalation이니 CFSR에서 진짜 원인을 본다. PRECISERR+BFARVALID면 BFAR 주소를 보고(보통 클럭 꺼진 주변장치), IMPRECISERR면 write buffer를 끄고 재현한다. UFSR의 INVSTATE는 Thumb 비트 없는 함수 포인터, STKERR/MSTKERR는 스택 오버플로다. stacked PC는 addr2line으로 소스 줄로 바꾼다. 양산에서는 이걸 `.noinit`에 저장하고 리셋 후 텔레메트리로 올린다.

**English**: In the fault handler I use bit 2 of EXC_RETURN to pick MSP or PSP, grab the stacked frame, and save the stacked PC and LR along with CFSR, HFSR, MMFAR and BFAR. If HFSR says FORCED, the real cause is an escalated configurable fault, so I decode CFSR: a precise bus error with BFARVALID usually means touching a peripheral whose clock is off; an imprecise one means I disable the write buffer to make it precise; INVSTATE points to a bad function pointer; stacking errors point to stack overflow. Then addr2line turns the stacked PC into a source line. In production the same data goes into a no-init RAM section and gets uploaded as crash telemetry after reboot.

**Q.** Your debugger can't connect to a new board. What do you check?

**A.** 아래 층부터: VTref 전압이 있나, 레일과 리셋 상태, 클럭. 그다음 SWD 속도를 낮춰 보고, 펌웨어가 SWD 핀을 바꾸거나 바로 sleep에 들어갈 수 있으니 connect under reset. IDCODE는 읽히는데 메모리 접근이 FAULT면 디버그 도메인 전원/보안 잠금을 의심한다. 필요하면 스코프로 SWCLK/SWDIO 파형을 본다.

**English**: I go bottom-up. Is VTref present, are the rails and reset in the right state, is there a clock? Then I drop the SWD clock and try connect-under-reset, because firmware might be remuxing the SWD pins or going straight to deep sleep. If I can read the DP IDCODE but memory accesses fault, I suspect the debug power domain or a security lock. And if all else fails I put a scope on SWCLK and SWDIO to see what's actually on the wire.

**Q.** Five out of a hundred EVT boards can't read an I2C sensor. How do you narrow it down?

**A.** 먼저 좋은 보드와 나쁜 보드를 나란히 놓고 차이를 찾는다. LA로 주소 단계에서 ACK가 있는지, 스코프로 SDA/SCL 상승 시간과 레벨을 본다. 전원 시퀀스(센서가 준비되기 전에 접근하는지), pull-up 값·실장, 주소 핀 floating을 확인하고, 부품을 좋은 보드와 교환해 원인이 부품인지 보드인지 가른다. 온도 의존성도 본다. 5%라는 비율 자체가 공정/부품 편차를 시사하므로 HW 팀과 X-ray나 BOM lot도 확인한다.

**English**: I compare a failing board side by side with a good one. On the logic analyzer I check whether the address gets an ACK; on the scope I check rise times and levels on SDA and SCL. Then I look at power sequencing, whether we talk to the sensor before it's ready, pull-up values and population, and floating address pins. Swapping the sensor between a good and a bad board tells me whether it follows the part or the board. A five percent rate smells like process or component variation, so I'd pull in the hardware team for X-ray and lot information.

**Q.** How do you read a schematic when you get a new board?

**A.** power tree 먼저: 어떤 레일이 항상 켜져 있고 어떤 걸 펌웨어가 스위칭하는지, PMIC 시퀀스와 PGOOD. 다음 MCU의 리셋·클럭·boot strap 핀과 디버그 접근(TP). 그다음 내가 드라이버를 쓸 버스마다 pull-up 위치와 레일, level shifter, 인터럽트 핀 극성과 wake 가능 여부. DNP와 0Ω 옵션도 표시한다. 이 결과로 bring-up 체크리스트를 만든다.

**English**: Power tree first: which rails are always on, which ones firmware switches, how the PMIC sequences them and where the power-good signals go. Then reset, clocks, boot straps and debug access on the MCU. Then, for every bus I'll write a driver for, where the pull-ups are and which rail they hang off, any level shifters, and interrupt pins: polarity, open-drain or not, and whether they land on a wake-capable pin. I also flag DNPs and zero-ohm options. That review becomes my bring-up checklist.

**Q.** Tell me about the hardest hardware/software interaction bug you've debugged. (Don 스토리 틀)

**A.** STAR로: 상황(새 무선 칩을 출하 플랫폼에 통합, EVT 단계, 특정 조건에서 제어 버스 장애) → 과제(HW 문제인지 FW 문제인지 가르기) → 행동(재현 조건 최소화, 프로토콜 분석기/DSO로 버스 캡처, 전원 레일 동시 측정, 가설 하나씩 배제) → 결과(원인과 수정, 재발 방지로 factory test 항목 추가). 구체적 숫자와 도구 이름을 넣되 기밀은 일반화한다.

**English**: (Don이 실제 사례로 채울 틀) We were integrating a new wireless chip into a shipping platform and saw intermittent failures on a control bus during EVT. My job was to figure out whether it was hardware, firmware or the interaction. I narrowed the reproduction to one condition, captured the bus with a protocol analyzer while probing the supply rails on the scope, and eliminated hypotheses one at a time. The root cause turned out to be (…). We fixed it in (…) and added a check to the factory test flow so it couldn't escape again.

**Q.** How would you log from an ISR without disturbing timing?

**A.** printf/UART 블로킹은 쓰지 않는다. 가장 가벼운 건 GPIO 토글 + LA. 텍스트가 필요하면 RTT(RAM 링버퍼, 디버거가 백그라운드로 읽음)나 ITM/SWO. 양산에서는 이벤트 ID와 타임스탬프만 RAM 링버퍼에 넣고 task가 나중에 내보낸다.

**English**: Never a blocking UART printf. The lightest option is toggling a GPIO and watching it on a logic analyzer. If I need data, I use RTT, which is just a RAM ring buffer the probe drains in the background, or ITM over SWO. In production I log compact event IDs with timestamps into a RAM ring buffer and let a low-priority task ship them out.

---

## 13. 직접 해보기

### 실습 1: 일부러 HardFault 내고 해석하기 (nRF52840 DK + Zephyr 또는 bare-metal)

```c
#include <stdint.h>
#include "device.h"   /* CMSIS-Core */

/* 세 가지를 하나씩 시험: which = 0, 1, 2 */
void fault_test(int which)
{
    if (which == 0) {
        volatile uint32_t *bad = (volatile uint32_t *)0xFFFFFFF0u;  /* 매핑 안 된 주소(칩마다 확인) → PRECISERR + BFARVALID 예상 */
        (void)*bad;
    } else if (which == 1) {
        void (*fp)(void) = (void (*)(void))0x00001000u;   /* bit0 = 0 → INVSTATE 예상 */
        fp();
    } else {
        SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;                 /* 0 나누기 → DIVBYZERO 예상 */
        volatile int z = 0;
        volatile int r = 10 / z;
        (void)r;
    }
}
```

5.3절 핸들러로 CFSR/BFAR/stacked PC를 출력하고 `arm-none-eabi-addr2line`으로 줄을 찾는다. Zephyr는 기본 fault 핸들러가 이미 이 정보를 출력하므로(`CONFIG_FAULT_DUMP`) 자기 핸들러 결과와 비교해 본다. escalation을 보려면 SHCSR 개별 enable을 켜고/끄고 HFSR.FORCED의 변화를 확인한다.

### 실습 2: OpenOCD + GDB로 watchpoint 잡기

```sh
openocd -f interface/cmsis-dap.cfg -f target/nrf52.cfg      # DK의 프로브 펌웨어에 맞게 interface 선택
arm-none-eabi-gdb build/zephyr/zephyr.elf -ex "target extended-remote :3333" -ex "monitor reset halt"
(gdb) watch g_counter
(gdb) continue
```

일부러 배열 오버런으로 `g_counter`를 덮는 코드를 넣고 watchpoint가 범인을 가리키는지 본다. (nRF DK의 온보드 프로브가 J-Link OB라면 `JLinkGDBServer -device nRF52840_xxAA -if SWD`와 포트 2331을 쓴다.)

### 실습 3: RTT vs UART 로그 오버헤드 비교

같은 ISR에서 UART printf와 `SEGGER_RTT_printf`를 각각 호출하고, ISR 진입/종료 GPIO를 LA로 측정해 ISR 길이를 비교한다. `JLinkRTTViewer` 또는 `JLinkRTTClient`로 출력을 본다.

### 실습 4: 회로도 리뷰 연습

Nordic nRF52840 DK의 공개 회로도(Nordic 웹사이트의 hardware files)를 받아서 power tree를 그리고, SWD 연결, 32MHz/32.768kHz 크리스탈, 버튼/LED 극성, 전류 측정 지점(DK의 전류 측정용 점퍼/solder bridge)을 찾아 표로 정리한다. 8.5절 목록을 체크리스트로 쓴다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| JTAG | IEEE 1149.1 Test Access Port | 4~5선 디버그/boundary scan, daisy chain |
| SWD | Serial Wire Debug | Arm의 2선 디버그 (SWCLK, SWDIO) |
| DP / AP | Debug Port / Access Port | 프로브가 붙는 포트 / 버스·코어 접근 포트 |
| MEM-AP | Memory Access Port | CSW/TAR/DRW로 시스템 버스 접근 |
| VTref | Target reference voltage | 프로브가 타깃 I/O 레벨을 아는 핀 |
| FPB | Flash Patch and Breakpoint | 하드웨어 breakpoint 비교기 |
| DWT | Data Watchpoint and Trace | watchpoint, CYCCNT, 프로파일 카운터 |
| ITM / SWO | Instrumentation Trace Macrocell / Serial Wire Output | 소프트웨어 trace 출력 채널과 1핀 출력 |
| ETM | Embedded Trace Macrocell | 명령 단위 실시간 trace |
| RTT | SEGGER Real-Time Transfer | RAM 링버퍼 + 디버거 백그라운드 읽기 로그 |
| EXC_RETURN | 예외 복귀 값 | LR에 들어가는 0xFFFFFFxx, 스택/모드/FP 정보 |
| CFSR | Configurable Fault Status Register | MMFSR+BFSR+UFSR |
| HFSR | HardFault Status Register | FORCED, VECTTBL |
| MMFAR / BFAR | fault 주소 레지스터 | VALID 비트가 1일 때만 유효 |
| stacked PC | 예외 프레임의 PC | fault 지점 추적의 출발점 |
| escalation | 승격 | 개별 fault가 HardFault로 올라가는 것 |
| PGOOD | Power Good | 레일 정상 신호 |
| back-powering | 역전원 | 꺼진 칩에 I/O 핀으로 전류가 흘러 들어감 |
| DNP | Do Not Populate | 회로도에 있지만 실장 안 함 |
| strap pin | 부트 설정 핀 | 리셋 시 레벨로 모드/주소 결정 |
| MSO | Mixed Signal Oscilloscope | 아날로그 + 디지털 채널 스코프 |

---

## 15. 요약 & 체크리스트

디버깅은 층을 가르는 일이다. 전원 → 클럭 → 리셋 → 디버그 연결 → 코드 → 주변장치 → 프로토콜 → 시스템 순으로 아래가 확실해야 위를 본다. CPU 안은 SWD/JTAG 프로브(DAP의 DP/AP, FPB breakpoint, DWT watchpoint, ITM/SWO, RTT)로, 핀 위의 디지털은 로직 분석기(임계 전압, 샘플링, 프로토콜 트리거, GPIO 계측)로, 전기적 현실은 오실로스코프(대역폭, 짧은 그라운드, 전원 램프 시퀀스)로 본다. HardFault는 EXC_RETURN으로 스택을 고르고 stacked PC와 CFSR/HFSR/MMFAR/BFAR로 원인을 좁히며, 양산에서는 crash dump를 비휘발 RAM에 남겨 텔레메트리로 올린다. 회로도는 power tree, 리셋/클럭/strap, 버스 pull-up과 레일, level shifter, DNP, test point 순으로 읽고, 그 결과가 bring-up 체크리스트가 된다.

- [ ] Arm 10핀 디버그 커넥터 핀아웃과 VTref의 역할을 그릴 수 있다
- [ ] SWD 트랜잭션(요청 8비트, turnaround, ACK, 데이터)과 DP/AP/MEM-AP 구조를 설명할 수 있다
- [ ] "디버거 연결 불가"를 층별로 진단하는 순서를 말할 수 있다
- [ ] Cortex-M 예외 스택 프레임 8워드와 EXC_RETURN 비트를 그릴 수 있다
- [ ] naked HardFault 핸들러를 쓰고 CFSR 주요 비트를 해석할 수 있다
- [ ] precise와 imprecise bus fault의 차이와 대처법을 설명할 수 있다
- [ ] LA와 스코프를 언제 쓰는지, 샘플 레이트·임계값·그라운드 함정을 말할 수 있다
- [ ] I2C pull-up의 최소/최대값을 계산할 수 있다
- [ ] 회로도에서 power tree를 뽑아 그리고 펌웨어가 제어하는 레일을 표시할 수 있다
- [ ] bring-up 체크리스트를 전원 전 → 첫 전원 → 디버그 → 주변장치 → 시스템 순으로 말할 수 있다

## 참고 자료

- [ARMv7-M Architecture Reference Manual (예외, fault 레지스터, 디버그)](https://developer.arm.com/documentation/ddi0403/latest/)
- [ARMv8-M Architecture Reference Manual](https://developer.arm.com/documentation/ddi0553/latest/)
- [Arm Debug Interface Architecture Specification ADIv5 (SWD, DP/AP)](https://developer.arm.com/documentation/ihi0031/latest/)
- [Cortex-M4 Devices Generic User Guide (CFSR/HFSR 비트 설명)](https://developer.arm.com/documentation/dui0553/latest/)
- [CMSIS 6 Core 문서](https://arm-software.github.io/CMSIS_6/latest/Core/index.html)
- [Arm: Cortex-M fault 분석 앱노트 (AN209, Using Cortex-M3/M4/M7 Fault Exceptions)](https://developer.arm.com/documentation/kan209/latest/)
- [OpenOCD 사용자 가이드](https://openocd.org/doc/html/index.html)
- [SEGGER J-Link GDB Server 및 RTT 문서](https://www.segger.com/products/debug-probes/j-link/technology/about-real-time-transfer/)
- [Lauterbach TRACE32 문서 포털](https://www.lauterbach.com/manual.html)
- [NXP UM10204 I2C-bus specification and user manual](https://www.nxp.com/docs/en/user-guide/UM10204.pdf)
- [Memfault Interrupt 블로그: How to debug a HardFault on an ARM Cortex-M MCU](https://interrupt.memfault.com/blog/cortex-m-hardfault-debug)
- [sigrok/PulseView](https://sigrok.org/wiki/Main_Page)
- [Nordic nRF52840 DK 제품 페이지 (회로도 등 hardware files 다운로드)](https://www.nordicsemi.com/Products/Development-hardware/nRF52840-DK)
