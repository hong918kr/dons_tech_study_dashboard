# J14. Experience with embedded debugging tools and workflows

> **분류**: Requirement 7/7 · **관련 개념 노트**: C10, C01, C04, S06
> **Don 현재 상태**: ✅ 강함 — "JTAG, Oscilloscope, Logic Analyzer, Power Analyzer", "DSOs and protocol Analyzer", Trace32. 다만 **어휘가 Lauterbach/SSD 쪽**이라 J-Link·OpenOCD·GDB·RTT·Zephyr 쪽 표현으로 번역해 둘 필요가 있다
> **이 노트를 다 읽으면**: ① 면접관이 "써봤다"와 "안다"를 가르는 지점이 도구별로 어디인지 안다 ② SWD/JTAG·GDB/OpenOCD·SWO/ITM·RTT·스코프·LA·전력계를 기초/중급/심화 3단으로 설명할 수 있다 ③ Trace32·DSO·프로토콜 분석기 경험을 Hark가 쓸 법한 툴체인 어휘로 바꿔 말할 수 있다

---

## 0. 문장 뜯어보기

| 구(句) | 표면적 의미 | 이 단어가 요구사항에 들어간 이유 [추정] |
|---|---|---|
| Experience with | 학습이 아니라 **사용 이력** | "튜토리얼을 봤다"가 아니라 "이걸로 실제 버그를 잡았다"를 증명하라는 뜻 |
| embedded debugging tools | 프로브·디버거·계측기 | 툴 이름을 아는지가 아니라 **각 툴이 어느 층을 보는지**를 아는지 |
| and workflows | 도구를 엮는 **절차** | 이게 진짜 요구사항이다. 도구는 배우면 되지만 워크플로는 프로그램을 겪어야 생긴다 |
| (Requirement 7/7, 마지막 항목) | 필수 자격의 마무리 | Responsibility 7번(J07)과 한 쌍. 책임 쪽은 "일을 굴릴 수 있나", 자격 쪽은 **"실제로 해봤음을 증명할 수 있나"** |

> **J07과의 분업**: J07은 **일이 어떻게 굴러가는가**(triage, 소유권, 계측 설계, 보고서)를 다룬다. 이 노트는 **면접관이 무엇으로 판별하는가**를 다룬다. 절차·조직 이야기는 J07 §2~3을 보고, 여기서는 도구별 깊이와 질문 등급에 집중한다.

---

## 1. Hark에서 이 요건이 어떻게 검증될 것인가 (추정)

context 4.1절(Figure 패턴 기반 추정 프로세스)과 JD를 근거로 한 **[추정]**이다.

### 1.1 라운드별로 이 요건이 나오는 방식 [추정]

| 라운드 | 형태 | 판별 목표 |
|---|---|---|
| 리크루터/HM 스크린 | "어떤 디버깅 도구를 써봤나요?" | 키워드 체크. 나열이 아니라 **한 사건**을 붙여 말하면 통과 |
| 기술 스크린 | 개념 질문 + 시나리오 | SWD가 무엇인지, HardFault를 어떻게 읽는지 같은 기초·중급 |
| FW 엔지니어 라운드 | 심화 질문 + 꼬리 물기 | 한 주제를 3단까지 파고들어 "직접 해봤나"를 확인 |
| HW 엔지니어 라운드 | 계측 질문 | 프로빙, 그라운드, 대역폭, 트리거 — 여기서 대부분이 무너진다 |
| case study / 경험 발표 | 과거 버그 1건 심층 | 캡처를 어떻게 잡고 무엇으로 결론 냈는지 |

### 1.2 Hark가 실제로 쓸 가능성이 큰 툴체인 [추정]

정해진 게 없을 가능성이 높다(context 4.7 역질문 4번). 하지만 이 구조라면 자연스러운 조합은 이렇다.

| 대상 | 프로브 | 소프트웨어 |
|---|---|---|
| always-on MCU (Cortex-M) | J-Link, 또는 벤더 보드 내장 디버거(CMSIS-DAP/DAPLink) | GDB + OpenOCD 또는 J-Link GDB Server, RTT, Zephyr `west debug` |
| Qualcomm SoC (Cortex-A, Android) | 벤더 전용 디버거 또는 Lauterbach | adb/logcat, ftrace/perf, gdbserver, crash dump 분석 |
| 버스 | Saleae 같은 LA | 내장 프로토콜 디코더 |
| 전기·전력 | DSO, 전류 프로파일러(PPK2/Joulescope/SMU) | 벤더 소프트웨어 |

> **면접에서 유리한 태도**: "제가 쓴 건 Trace32였고, J-Link+GDB로 같은 작업을 어떻게 하는지도 매핑해 뒀습니다. 팀이 쓰는 걸 씁니다." 도구 종교가 없는 사람으로 보이는 게 중요하다.

---

## 2. 핵심 개념 — 도구별 "깊이 3단"

각 도구마다 면접관이 파고드는 층이 정해져 있다. **기초 = 무엇인지 안다 / 중급 = 실제로 써봤다 / 심화 = 안 될 때 고칠 수 있다.**

### 2.1 SWD / JTAG — 물리와 프로토콜

**기초**: SWD는 SWCLK·SWDIO 2선, JTAG은 TCK·TMS·TDI·TDO(+nTRST) 4~5선. Cortex-M은 보통 SWD를 쓰고, 표준 커넥터는 0.05인치 피치 10핀 Arm Cortex Debug 커넥터다. 트레이스가 필요하면 20핀 Cortex Debug+ETM 커넥터로 `TRACECLK`/`TRACEDATA[0:3]`가 나온다.

**중급**: 프로브는 코어를 직접 보지 않는다. **DP(Debug Port) → AP(Access Port) → 버스**를 거쳐 메모리에 접근한다.

```
 [프로브] ──SWD/JTAG──► [DP: SW-DP / JTAG-DP / SWJ-DP]
                             │ (내부 버스)
                             ▼
                        [MEM-AP] ──► AHB/AXI ──► SRAM, 플래시, 주변장치, 코어 디버그 레지스터
                             │
                        [ROM table] : 이 칩에 어떤 CoreSight 컴포넌트가 있는지 목록
```

핵심 레지스터(ADIv5 기준).

| 블록 | 레지스터 | 역할 |
|---|---|---|
| DP | `IDCODE` (읽기, 주소 0x0) | 연결 확인의 첫 단추. 이게 안 읽히면 그 아래는 볼 것도 없다 |
| DP | `CTRL/STAT` (0x4) | `CDBGPWRUPREQ`/`CSYSPWRUPREQ`로 디버그·시스템 전원 도메인을 깨운다 |
| DP | `SELECT` (0x8) | 어떤 AP의 어떤 뱅크를 볼지 선택 |
| DP | `RDBUFF` (0xC) | AP 읽기는 파이프라인이라, 마지막 값을 여기서 받는다 |
| MEM-AP | `CSW` (0x00) | 접근 크기(8/16/32비트), 자동 증가 모드 |
| MEM-AP | `TAR` (0x04) | 읽고 쓸 타깃 주소 |
| MEM-AP | `DRW` (0x0C) | 실제 데이터 창 |

SWD 한 트랜잭션: 8비트 요청(start, APnDP, RnW, A[2:3], parity, stop, park) → 턴어라운드 → 3비트 ACK(`OK`=0b001, `WAIT`=0b010, `FAULT`=0b100) → 32비트 데이터 + 패리티. 자세한 파형은 C10 §1.3.

**심화 — "연결이 안 될 때"가 진짜 시험이다.**

| 증상 | 원인 후보 | 대처 |
|---|---|---|
| IDCODE가 안 읽힘 | 코어 전원 없음, SWCLK/SWDIO 미연결, 보드가 SWJ 핀을 GPIO로 재할당 | 스코프로 SWCLK 파형 확인, connect-under-reset |
| 붙었다가 바로 끊김 | 펌웨어가 부팅 직후 딥슬립 진입, 디버그 핀 비활성화 | connect-under-reset + `VC_CORERESET`(DEMCR bit0)로 리셋 벡터에서 halt |
| ACK가 계속 WAIT | 클럭이 너무 빠름, 전원 도메인 미기동 | 어댑터 속도 낮추기, `CDBGPWRUPREQ` 확인 |
| JTAG 체인에서 타깃을 못 찾음 | IR 길이/디바이스 순서 설정 오류 | 체인 스캔, BSDL/벤더 문서로 IR 길이 확인 |
| 아예 잠김 | readout protection, 디버그 포트 영구 잠금(양산 설정) | 보드 rev/펌웨어 빌드 확인. 개발 빌드는 잠그지 않는다 |

> `SWJ-DP`는 JTAG/SWD를 겸하며, 특정 16비트 시퀀스를 보내 모드를 전환한다(JTAG→SWD 전환 시퀀스). 프로브가 알아서 하지만, "왜 프로브가 연결 초기에 이상한 클럭을 쏘는가"의 답이다.

### 2.2 코어 디버그 — halt, breakpoint, watchpoint

**기초**: 브레이크포인트는 두 종류다. 소프트웨어 BP는 명령을 `BKPT`로 바꿔치기하므로 **RAM에서 실행되는 코드에만** 자유롭게 쓸 수 있고 개수 제한이 없다. 하드웨어 BP는 FPB(Flash Patch and Breakpoint) 비교기를 쓰므로 플래시에서도 되지만 **개수가 하드웨어로 제한**된다(코어 구현마다 다름, 흔히 6~8개).

**중급**: 주요 레지스터.

| 블록 | 레지스터 (주소) | 용도 |
|---|---|---|
| 코어 디버그 | `DHCSR` (0xE000EDF0) | halt/step 제어. 쓸 때 상위 16비트에 `DBGKEY`=0xA05F 필요. `C_DEBUGEN`, `C_HALT`, `C_STEP`, `C_MASKINTS` |
| 코어 디버그 | `DCRSR`/`DCRDR` (0xE000EDF4/F8) | 코어 레지스터 읽기·쓰기 창 |
| 코어 디버그 | `DEMCR` (0xE000EDFC) | `VC_CORERESET`(리셋 벡터에서 halt), `TRCENA`(bit24, DWT/ITM 쓰기 전 필수) |
| FPB | `FP_CTRL` (0xE0002000) | `ENABLE`+`KEY` 동시 세트, `NUM_CODE`로 비교기 개수 확인 |
| DWT | `DWT_CTRL` (0xE0001000) | `CYCCNTENA`, `PCSAMPLENA`, `EXCTRCENA` |
| DWT | `DWT_CYCCNT` (0xE0001004) | 사이클 카운터 — 프로파일링의 기본 도구 |
| DWT | `DWT_COMP0/MASK0/FUNCTION0` (0xE0001020~) | 데이터 watchpoint. 비교기 개수는 구현마다 다름 |

**심화 — watchpoint가 결정적인 상황.** "어떤 변수가 누가 언제 망가뜨리는지 모르겠다"는 부류에서 브레이크포인트는 쓸모가 없다. DWT 비교기를 그 주소에 걸고 쓰기 접근에서 halt하면 **범인의 PC가 바로 나온다**. 메모리 손상, 스택 오버런, 버퍼 오버플로 디버깅의 기본기다.

```
(gdb) watch *(uint32_t *)0x20001234      # 쓰기 시 정지
(gdb) rwatch *(uint32_t *)0x20001234     # 읽기 시 정지
(gdb) awatch *(uint32_t *)0x20001234     # 읽기/쓰기 모두
(gdb) info watchpoints
```

GDB가 하드웨어 watchpoint를 쓸 수 있으면 실행 속도 손실이 거의 없고, 소프트웨어 watchpoint로 떨어지면 싱글스텝으로 검사해 **수백~수천 배 느려진다**. "왜 갑자기 기어가지?"의 답이 이것이다. C10 §3에 FPB/DWT 세부가 있다.

### 2.3 GDB / OpenOCD / J-Link — 워크플로

**기초**: 프로브는 GDB 서버를 띄우고, GDB가 TCP로 붙는다.

```
 [arm-none-eabi-gdb] ──TCP:3333── [OpenOCD 또는 JLinkGDBServer] ──USB── [프로브] ──SWD── [MCU]
```

**중급**: 실제 명령.

```sh
# OpenOCD: 인터페이스 설정 + 타깃 설정
openocd -f interface/jlink.cfg -f target/nrf52.cfg
# (설정 파일 이름과 옵션은 OpenOCD 버전마다 다르다)

# 다른 터미널
arm-none-eabi-gdb build/zephyr/zephyr.elf
(gdb) target extended-remote localhost:3333
(gdb) monitor reset halt
(gdb) load
(gdb) break main
(gdb) continue
```

```sh
# SEGGER 쪽
JLinkGDBServer -device NRF52840_XXAA -if SWD -speed 4000
JLinkExe -device NRF52840_XXAA -if SWD -speed 4000   # 대화형: mem, w4, r, go, halt
```

```sh
# Zephyr 통합 명령 (런너가 뒤에서 OpenOCD/J-Link를 부른다)
west flash
west debug        # 빌드 → 플래시 → GDB 연결까지
west attach       # 돌고 있는 타깃에 붙기
```

자주 쓰는 GDB 조합.

| 목적 | 명령 |
|---|---|
| fault 직후 상태 | `info registers` → `p/x $lr` → `x/16xw $sp` |
| 콜스택 | `bt`, `bt full` |
| RTOS 스레드 | `info threads`, `thread apply all bt` (서버가 RTOS 인식을 지원할 때) |
| 메모리 덤프를 파일로 | `dump binary memory core.bin 0x20000000 0x20010000` |
| 플래시 내용이 ELF와 같은지 | `compare-sections` |
| 반복 작업 자동화 | `.gdbinit`, `define` 사용자 명령, `-ex` 배치 실행 |

**심화 — RTOS 인식과 그 한계.** OpenOCD는 타깃 설정에서 `-rtos FreeRTOS`처럼 지정하면 태스크를 GDB 스레드로 보여준다. 동작 원리는 **커널의 태스크 리스트 심볼을 읽어** 각 태스크의 저장된 스택 프레임에서 레지스터를 복원하는 것이다. 그래서 심볼이 없거나(`-O2`로 사라진 경우), 커널 버전이 바뀌어 구조체 오프셋이 달라지면 조용히 틀린 값을 보여준다. 지원 RTOS 목록과 옵션 이름은 **버전마다 다르다**.

**심화 — halt가 시스템을 깨는 문제.** 브레이크포인트로 멈추면 코어만 멈추고 타이머·DMA·라디오·watchdog은 계속 돈다. 그래서 halt 후 continue 하면 링크가 끊기거나 watchdog이 리셋을 건다. 대처는 세 가지다: ① 디버그 halt 시 주변장치를 같이 멈추는 설정을 쓴다(SoC마다 있고 이름이 다르다) ② watchdog을 개발 빌드에서 디버그 연결 시 비활성화한다 ③ 애초에 halt 대신 **로그/트레이스로 관찰**한다.

### 2.4 SWO / ITM — 싸게 보는 법 (그리고 그 한계)

**기초**: ITM(Instrumentation Trace Macrocell)은 32개 stimulus 포트를 가진 계측 블록이고, 그 출력이 TPIU를 통해 **SWO 한 핀**으로 나간다. `printf`를 SWO로 빼는 게 가장 흔한 용법이다.

**중급**: 켜는 순서가 정해져 있다.

| 순서 | 무엇 | 레지스터 |
|---|---|---|
| 1 | 트레이스 전체 활성화 | `DEMCR`의 `TRCENA`(bit24) |
| 2 | ITM 잠금 해제 | `ITM_LAR`(0xE0000FB0)에 0xC5ACCE55 |
| 3 | ITM 제어 | `ITM_TCR`(0xE0000E80): `ITMENA`, 타임스탬프, trace bus ID |
| 4 | 포트 활성화 | `ITM_TER`(0xE0000E00) 비트 = 포트 번호 |
| 5 | 출력 형식 | `TPIU_SPPR`(0xE00400F0): NRZ(UART) 또는 Manchester |
| 6 | 속도 | `TPIU_ACPR`: 코어 클럭에서 분주 |
| 7 | 쓰기 | `ITM_STIM[n]`(0xE0000000 + 4n)에 바이트/워드 write |

**심화 — SWO의 진짜 성질 세 가지.**

1. **속도가 코어 클럭에 묶인다.** `ACPR` 분주값을 호스트 뷰어의 설정과 정확히 맞춰야 한다. 클럭을 런타임에 바꾸는 저전력 기기에서는 DVFS 때마다 SWO가 깨진다. 이게 always-on MCU에서 SWO보다 RTT를 선호하는 큰 이유다.
2. **넘치면 조용히 버린다.** ITM FIFO가 차면 overflow 패킷만 남고 데이터가 사라진다. 로그가 "가끔 한 줄씩 빠지는" 증상의 정체다.
3. **printf만 나오는 게 아니다.** 같은 SWO 스트림에 DWT의 **PC 샘플링**(주기적 PC 스냅샷 → 저비용 프로파일러)과 **예외 진입/퇴출 트레이스**가 섞여 나간다. "인터럽트가 얼마나 자주, 얼마나 오래 도는가"를 코드 수정 없이 보는 방법이다. 이걸 아는 지원자는 드물다.

### 2.5 RTT — 왜 이것이 표준이 되었나

**기초**: SEGGER RTT는 타깃 RAM에 링버퍼를 두고, 펌웨어는 거기에 쓰기만 하며, 프로브가 그 메모리를 읽어간다.

**중급**: 동작의 핵심은 **background memory access**다. SWD/JTAG의 MEM-AP는 코어를 멈추지 않고 메모리를 읽을 수 있다. 그래서 펌웨어 입장에서 RTT 출력은 그냥 RAM `memcpy`이고, UART처럼 비트레이트에 묶이지 않는다.

```
 펌웨어  ──write──► [_SEGGER_RTT 제어 블록]
                      ID 문자열 "SEGGER RTT"
                      up 버퍼 (타깃→호스트)
                      down 버퍼 (호스트→타깃, 입력도 된다)
                           ▲
 호스트  ──MEM-AP read──────┘  (코어는 계속 실행 중)
```

프로브는 타깃 RAM을 훑어 ID 문자열을 찾아 제어 블록을 잡는다. 못 찾으면 주소를 직접 지정해 줘야 한다 — RTT 뷰어가 "control block not found"를 뱉는 전형적 상황이다.

**심화 — 버퍼가 꽉 찼을 때의 모드 선택이 곧 설계 판단이다.**

| 모드 | 동작 | 언제 쓰나 |
|---|---|---|
| no-block / skip | 안 들어가면 그 메시지를 통째로 버린다 | 기본값. 타이밍을 절대 안 건드려야 할 때 |
| no-block / trim | 들어가는 만큼만 쓴다 | 로그가 잘려도 되는 경우 |
| block if FIFO full | 공간이 생길 때까지 기다린다 | 한 줄도 잃으면 안 될 때. **타이밍을 바꾸므로 Heisenbug 주의** |

RTT를 받는 쪽은 J-Link 도구(RTT Viewer/Client/Logger) 외에 OpenOCD의 `rtt` 명령군, probe-rs 등이 있다. 지원 여부와 명령 이름은 **버전마다 다르다**. Zephyr에서는 `CONFIG_USE_SEGGER_RTT`, `CONFIG_RTT_CONSOLE` 계열 Kconfig로 콘솔을 RTT로 돌린다.

### 2.6 RTOS 트레이스와 프로파일링

| 방법 | 원리 | 얻는 것 |
|---|---|---|
| SEGGER SystemView | RTOS 훅에서 이벤트를 RTT로 내보냄 | 태스크 전환·ISR·API 호출 타임라인 |
| Percepio Tracealyzer | 커널 훅 + 저장(RAM/스냅샷/스트리밍) | 같은 계열. 큐·뮤텍스 상태까지 시각화 |
| FreeRTOS run-time stats | 고해상도 타이머 + `configGENERATE_RUN_TIME_STATS` | 태스크별 누적 CPU 시간 |
| FreeRTOS 스택 검사 | `configCHECK_FOR_STACK_OVERFLOW`, `uxTaskGetStackHighWaterMark()` | 스택 여유 |
| Zephyr Thread Analyzer | 스레드별 스택 사용량·CPU 점유 출력 | 같은 목적, Zephyr 표준 |
| DWT PC 샘플링 (SWO) | 주기적 PC 스냅샷 | 코드 수정 없는 통계 프로파일러 |
| `DWT_CYCCNT` 수동 계측 | 코드 앞뒤로 사이클 카운터 차이 | 함수 단위 정밀 측정 |
| ETM/MTB 명령 트레이스 | 하드웨어가 실행 흐름을 기록 | 완전 비침습. 경쟁 상태·폭주 추적의 최종 병기 |

```c
/* DWT 사이클 카운터로 구간 측정 (ARMv7-M) */
#include <stdint.h>
#define DEMCR      (*(volatile uint32_t *)0xE000EDFCu)
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000u)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004u)

static void cyccnt_enable(void)
{
    DEMCR    |= (1u << 24);   /* TRCENA: 트레이스 블록 전원/클럭 */
    DWT_CYCCNT = 0u;
    DWT_CTRL |= (1u << 0);    /* CYCCNTENA */
}

static uint32_t measure(void (*fn)(void))
{
    uint32_t t0 = DWT_CYCCNT;
    fn();
    return DWT_CYCCNT - t0;   /* 32비트 랩어라운드는 뺄셈으로 자연히 처리 */
}
```

주의: `DWT_CYCCNT`가 없는 구현도 있다(`DWT_CTRL`의 `NOCYCCNT` 비트로 확인). Cortex-M0+는 DWT가 축소돼 있고, 명령 트레이스는 ETM이 아니라 **MTB(Micro Trace Buffer)**로 제공되는 경우가 많다 — 실행 흐름을 RAM의 원형 버퍼에 남긴다.

### 2.7 오실로스코프 — HW 라운드에서 갈리는 곳

**기초**: 무엇을 보는가 — 전압 대 시간. 디코더가 못 보는 층(레벨, 상승시간, 링잉, 글리치, 전원 램프)을 본다.

**중급 — 대역폭과 상승시간.** 가우시안 응답(대략 1 GHz 이하)에서 `BW ≈ 0.35 / Tr_scope`이고, 실제로 보이는 값은 `Tr_measured ≈ sqrt(Tr_signal² + Tr_scope² + Tr_probe²)`로 합성된다. 경험칙은 스코프 Tr이 신호 Tr의 1/3~1/5일 것 — 2 ns 에지를 제대로 보려면 Tr 약 0.5 ns, 즉 대역폭 700 MHz 급이 필요하다. 파형 예시는 C10 §7.1.

**심화 — 프로빙이 측정을 결정한다.**

| 요소 | 문제 | 대처 |
|---|---|---|
| 그라운드 리드 인덕턴스 | 긴 악어클립 리드가 프로브 용량과 공진 → 오버슈트·링잉이 **없는 것도 생긴다** | 그라운드 스프링, 최단 리턴 경로 |
| 프로브 용량 부하 | 10:1 패시브 프로브의 입력 용량이 상승시간을 늘림. 1:1은 더 심함 | 빠른 신호는 능동 프로브 |
| 대역폭 부족 | 글리치가 안 보임 | 위 계산으로 검증 |
| 전원 리플 측정 | 10:1 + DC 결합이면 노이즈 플로어에 묻힘 | 1:1 + AC 결합, 또는 전원 레일 전용 프로브(저감쇠·대오프셋) |
| 기준 없는 신호 | 접지 기준으로 재면 틀리거나 위험 | 차동 프로브 |
| 저속 신호를 고속 세팅으로 | 메모리 깊이가 모자라 사건을 못 담음 | 샘플레이트와 메모리 깊이를 함께 고려 |

트리거는 스코프의 절반이다. 에지만 쓰는 사람과, 펄스 폭·런트·셋업/홀드·직렬 프로토콜 트리거를 쓰는 사람은 잡는 버그의 종류가 다르다(지원 트리거 종류는 기종·옵션마다 다름).

### 2.8 로직 분석기 — 디지털 버스의 진실

**기초**: 다채널 디지털 캡처 + 프로토콜 디코드. "펌웨어가 보낸 것"이 아니라 **"선 위에 실제로 무엇이 있었나"**를 본다.

**중급 — 샘플링.** 타이밍 모드(비동기)는 LA 자체 클럭으로 샘플하므로 에지 위치가 ±1 샘플 흔들린다. 경험칙은 대상 신호의 4~10배 — 400 kHz I2C는 수 MS/s, 25 MHz SPI는 100 MS/s 이상. 상태 모드(동기)는 대상의 클럭 에지에서만 샘플해 프로토콜 내용은 정확하지만 글리치는 못 본다. 트리거 설정은 C10 §6.2.

**심화 — LA로 못 보는 것을 아는 것이 실력이다.**

| LA가 놓치는 것 | 왜 | 대신 쓸 것 |
|---|---|---|
| 레벨 마진 | 임계값 하나로 0/1만 판정 | 스코프 |
| 상승시간·링잉 | 아날로그 정보 없음 | 스코프 |
| 좁은 글리치 | 샘플 사이에 숨음 | 스코프, 또는 글리치 트리거 |
| 1.8 V 로직을 3.3 V 임계값으로 볼 때 | 임계값 설정 실수로 전부 0 | 임계값을 레벨에 맞게 설정 |

반대로 스코프가 못 하는 것 — **수백 개 트랜잭션을 텍스트로 훑기**. I2C 주소 스캔, SPI 프레임 정렬 오류, UART 프레이밍 에러, I2S LRCLK 정렬 같은 것은 LA 디코더가 압도적으로 빠르다. 디코드 결과에 트리거를 걸 수 있는 기종이면(벤더마다 다름) "NACK가 나올 때만 캡처"가 가능해진다.

### 2.9 전력 프로파일링 — 저전력 기기의 디버거

always-on 배터리 기기에서 전류 파형은 **로그 그 자체**다. 무엇이 깨어 있는지 그래프에 다 나온다.

| 도구 | 성격 | 비고 |
|---|---|---|
| Nordic PPK2 | 전원 공급 겸 전류 측정, 디지털 입력 채널 동반 | 측정 범위·샘플레이트는 벤더 문서 확인. 저가·간편 |
| Joulescope | 넓은 동적 범위 전류·전압·전력 | μA와 수백 mA를 한 캡처에서 (제품 세대마다 다름) |
| Keysight/Tektronix SMU·DC power analyzer | 실험실급. 전압 스윕 + 정밀 측정 | Don의 "Power Analyzer" 경험이 여기 해당 `<확인 필요: 정확한 모델명>` |
| 션트 저항 + 스코프 | 임시 방편. 트랜지언트 파형은 잘 보임 | 션트 값 선택이 어렵다(작으면 노이즈, 크면 전압 강하) |

**심화 — 전류 파형을 코드와 묶는다.** 전류 측정기의 디지털 입력에 §J07 §2.4의 GPIO 마커를 연결하면, "이 8 mA 피크는 라디오 TX", "이 250 μA 바닥은 딥슬립이 아니라 idle"처럼 **파형의 각 구간에 코드 이름을 붙일 수 있다**. 저전력 디버깅의 핵심 기술이고, C05에 계산과 sleep 상태가 있다.

### 2.10 증상별 도구 선택

증상 → 첫 도구 매핑표는 **J07 §3.3**에 있다. 여기서는 침습도 순서만 기억한다: DSO·LA·ETM 트레이스·전류계는 침습 0, watchpoint는 정지 시점만, RTT는 수 μs, SWO는 클럭 의존, UART printf와 halt 디버깅은 시스템을 바꾼다.

---

## 3. 실무 패턴과 함정

### 3.1 면접관이 "진짜 써봤다"를 감지하는 신호

| 얕은 답 | 깊은 답(면접관이 찾는 것) |
|---|---|
| "JTAG으로 디버깅합니다" | "Cortex-M이라 보통 SWD 2선을 쓰고, 트레이스가 필요하면 20핀으로 TRACEDATA를 뺍니다" |
| "브레이크포인트를 겁니다" | "플래시라 하드웨어 BP를 쓰는데 개수가 제한돼서, 메모리 손상 추적엔 DWT watchpoint로 갑니다" |
| "printf로 봅니다" | "UART printf는 수 ms라 타이밍 버그를 덮습니다. RTT는 RAM 쓰기라 프로브가 background로 읽어갑니다" |
| "스코프로 봤습니다" | "10:1 프로브에 그라운드 스프링 쓰고, 리플은 1:1 AC 결합으로 다시 쟀습니다" |
| "로직 분석기로 I2C를 봤습니다" | "NACK가 주소 단계였는지 데이터 단계였는지 봤고, 상승시간이 의심돼 같은 지점을 스코프로 다시 쟀습니다" |
| "재현이 안 됩니다" | "발생률이 시도당 약 1%라 300회 자동화로 잡았고, 에러 시점에 GPIO를 때려 LA 트리거로 썼습니다" |
| "HardFault가 났습니다" | "CFSR를 읽고, stacked PC를 꺼내 `addr2line`으로 줄을 찾았습니다. imprecise면 write buffer 때문이라 재현 코드를 좁혔습니다" |
| "로그를 봤습니다" | "리셋 사유 카운터와 `.noinit` 링버퍼를 먼저 넣고, 그다음에 재현했습니다" |

### 3.2 워크플로 — 반복 가능하게 만드는 것들

**`.gdbinit`으로 fault 분석을 한 줄로**

```
# 프로젝트 .gdbinit 예시 (명령 이름은 GDB 버전에 따라 차이가 있을 수 있다)
target extended-remote localhost:3333
define fault
  printf "CFSR = 0x%08x\n", *(unsigned int *)0xE000ED28
  printf "HFSR = 0x%08x\n", *(unsigned int *)0xE000ED2C
  printf "MMFAR= 0x%08x\n", *(unsigned int *)0xE000ED34
  printf "BFAR = 0x%08x\n", *(unsigned int *)0xE000ED38
  printf "LR   = 0x%08x (EXC_RETURN)\n", $lr
  x/8xw $sp
end
```

halt 상태에서 `fault` 한 번이면 C10 §5의 분석 재료가 다 나온다. **팀에 공유되는 `.gdbinit`이 있는 프로젝트와 없는 프로젝트는 디버깅 속도가 배로 차이 난다.**

**빌드 산출물을 버린 적이 없어야 한다**

| 산출물 | 왜 필요한가 |
|---|---|
| `.elf` (심볼 포함) | coredump·crash PC를 줄 번호로 바꾸는 유일한 열쇠 |
| `.map` | 어느 심볼이 어디에 있는지, 섹션 크기, 스택 배치 |
| 빌드 ID / git hash를 펌웨어에 박기 | 필드 crash가 어느 빌드인지 확정 |

필드에서 올라온 PC를 줄로 바꾸는 명령.

```sh
arm-none-eabi-addr2line -e firmware.elf -f -C 0x0800a3d4
arm-none-eabi-nm -n -S firmware.elf | less        # 주소 순 심볼 목록
arm-none-eabi-objdump -d -S firmware.elf > dis.txt
arm-none-eabi-size -A firmware.elf                # 섹션별 크기
```

**재현을 스크립트로 남긴다**: 전원 사이클 릴레이 + 플래싱 + 스트레스 실행 + 로그 수집을 한 스크립트로. 이게 있으면 밤사이 수백 회가 돌고, 없으면 손으로 스무 번 하고 포기한다.

### 3.3 도구별 함정 표

| 도구 | 함정 | 증상 | 대처 |
|---|---|---|---|
| SWD | 슬립 중 연결 시도 | 연결 실패 또는 간헐 실패 | connect-under-reset, `VC_CORERESET` |
| SWD | 어댑터 클럭 과속 | 간헐 WAIT/FAULT, 읽기 값 깨짐 | 속도 낮추고 케이블 짧게 |
| GDB | halt 중 watchdog 리셋 | continue 하면 재부팅 | 개발 빌드에서 디버그 연결 시 WDT 정지 |
| GDB | 소프트웨어 watchpoint로 폴백 | 극도로 느려짐 | 하드웨어 비교기 개수 확인, 범위 줄이기 |
| OpenOCD | RTOS 인식 오탐 | 스레드 목록이 이상함 | 심볼 유무·커널 버전 확인, 안 되면 끄고 수동 분석 |
| SWO | 클럭 분주 불일치 | 글자가 깨져 나옴 | `ACPR` 계산값과 뷰어 설정 일치 |
| SWO | FIFO 오버플로 | 로그가 띄엄띄엄 빠짐 | 로그량 줄이기, RTT로 전환 |
| RTT | 제어 블록 못 찾음 | "control block not found" | RAM 탐색 범위 지정, 주소 직접 지정 |
| RTT | blocking 모드 | 타이밍이 바뀌어 버그 소멸 | no-block 모드로, 손실 허용 |
| 스코프 | 긴 그라운드 리드 | 없는 링잉이 보임 | 그라운드 스프링 |
| 스코프 | 대역폭 부족 | 글리치 누락 | Tr 계산으로 사전 검증 |
| LA | 샘플레이트 부족 | 에지가 흔들려 디코드 실패 | 4~10배 규칙 |
| LA | 임계값 오설정 | 전부 0 또는 전부 1 | 로직 레벨에 맞춰 설정 |

### 3.4 Trace32를 쓴 사람이 J-Link+GDB 세계로 번역하는 법

Don에게 가장 중요한 절이다. 명령 대응표는 **C10 §2.3에 있으므로 여기서 반복하지 않는다.** 대신 면접에서 쓸 **개념 번역**을 정리한다.

| Trace32 세계의 개념 | GDB/OpenOCD 세계의 대응 | 말하는 법 |
|---|---|---|
| PRACTICE 스크립트(`.cmm`) | `.gdbinit`, `define`, GDB Python, OpenOCD TCL | "디버그 세션을 스크립트로 자동화하는 습관이 있습니다. Trace32에서는 PRACTICE였고, GDB에서는 `.gdbinit`과 커스텀 명령입니다" |
| `SYStem.Up` / `SYStem.Mode Attach` | `monitor reset halt` / `west attach` | "리셋 후 halt로 들어갈지, 돌고 있는 타깃에 붙을지의 구분은 같습니다" |
| `Data.LOAD.Elf ... /NoCODE` | `add-symbol-file`, 심볼만 로드 | "플래시를 건드리지 않고 심볼만 얹는 건 양쪽 다 있습니다" |
| `Var.View` / `Var.Watch` | `print`, `display`, `watch` | 용어만 다르다 |
| `Break.Set /ReadWrite` | `watch` / `rwatch` / `awatch` | 밑단은 둘 다 DWT 비교기 |
| `Trace.List`, 오프칩 트레이스 | ETM + J-Trace, MTB | "명령 트레이스를 실제로 써봤다"는 건 강한 신호다. 이 경험을 반드시 언급 |
| OS awareness (태스크 인식) | OpenOCD `-rtos`, GDB `info threads` | "커널 자료구조를 디버거가 읽어 태스크를 보여주는 구조" |
| 프로토콜 분석기 캡처 | LA 프로토콜 디코드 | 같은 근육. "PCIe/RFFE 분석기로 하던 일을 I2C/I2S LA로 하는 것" |

**핵심 문장(영어)**: "Most of my probe work was with Lauterbach and vendor tools on SSD and wireless silicon, so my vocabulary is Trace32 — but the underlying mechanism is the same CoreSight debug port, and I've mapped my usual workflows onto J-Link and OpenOCD with GDB. I care about the workflow, not the brand."

### 3.5 Don이 "도구를 안다"를 넘어 보여야 할 것

면접관이 최종적으로 확인하고 싶은 건 도구가 아니라 **판단**이다.

- 언제 도구를 **안 쓰는지**: 로그 한 줄로 끝날 일에 LA를 꺼내지 않는다.
- 침습도를 **계산하고 선택하는지**: "이 계측이 몇 μs를 먹는가"를 말할 수 있는가.
- 팀에 **남기는지**: `.gdbinit`, 재현 스크립트, 랩 셋업 문서.
- 도구가 **거짓말할 수 있음**을 아는지: RTOS 인식 오탐, LA 임계값, 그라운드 리드 링잉.

---

## 4. 리서치 — 근거 자료

| 자료 | 무엇을 담고 있나 | 어디를 읽어야 하나 | URL |
|---|---|---|---|
| Arm Debug Interface Architecture Specification ADIv5 (IHI0031) | DP/AP 레지스터, SWD·JTAG 트랜잭션 | DP `CTRL/STAT`·`SELECT`·`RDBUFF`, MEM-AP `CSW`/`TAR`/`DRW`, SWD 패킷·ACK | https://developer.arm.com/documentation/ihi0031/latest/ |
| ARMv7-M Architecture Reference Manual (DDI0403) | 코어 디버그·DWT·ITM·FPB·TPIU·fault 레지스터 | Debug 장 전체. `DHCSR`, `DEMCR`, `DWT_CTRL`, `ITM_TCR`, `FP_CTRL`, `CFSR` | https://developer.arm.com/documentation/ddi0403/latest/ |
| ARMv8-M Architecture Reference Manual (DDI0553) | ARMv8-M(M23/M33) 디버그·보안 fault | `SFSR`/`SFAR`, TrustZone-M에서의 디버그 권한 | https://developer.arm.com/documentation/ddi0553/latest/ |
| CMSIS-DAP 문서 | 오픈 디버그 프로브 프로토콜 | 명령 집합, DAPLink와의 관계 | https://arm-software.github.io/CMSIS_5/DAP/html/index.html |
| OpenOCD User's Guide | 어댑터·타깃 설정, GDB 연동, RTOS 인식, RTT | "Debug Adapter Configuration", "GDB and OpenOCD", RTOS/RTT 절 | https://openocd.org/doc/html/index.html |
| GDB 매뉴얼 | 브레이크/워치포인트, 원격 디버깅, 스크립팅 | "Setting Watchpoints", "Remote Debugging", "Canned Sequences" | https://sourceware.org/gdb/current/onlinedocs/gdb.html/ |
| probe-rs | 러스트 생태계 디버그 호스트(RTT 포함) | 프로브 지원, RTT 사용 | https://probe.rs/ |
| SEGGER J-Link 사용자 매뉴얼 (UM08001) | J-Link 전체 기능 | GDB Server 옵션, SWO, RTT, unlimited flash breakpoints | https://www.segger.com/downloads/jlink/UM08001_JLink.pdf |
| SEGGER RTT 기술 설명 | RTT 원리와 성능 | 제어 블록, 버퍼 모드 | https://www.segger.com/products/debug-probes/j-link/technology/about-real-time-transfer/ |
| SEGGER SystemView | RTOS 이벤트 실시간 시각화 | 계측 방법, 오버헤드 | https://www.segger.com/products/development-tools/systemview/ |
| FreeRTOS Stack Overflow Checking | 스택 오버플로 감지 방법 1/2 | `configCHECK_FOR_STACK_OVERFLOW` | https://www.freertos.org/Stacks-and-stack-overflow-checking.html |
| Zephyr Debugging | `west debug/attach`, 프로브 백엔드 | 러너 설정 | https://docs.zephyrproject.org/latest/develop/debug/index.html |
| Zephyr Coredump | 온디바이스 coredump와 오프라인 GDB 분석 | 백엔드 선택, `coredump_gdbserver.py` | https://docs.zephyrproject.org/latest/services/debugging/coredump.html |
| Zephyr Thread Analyzer | 스레드 스택·CPU 분석 | Kconfig와 출력 해석 | https://docs.zephyrproject.org/latest/services/debugging/thread-analyzer.html |
| Tektronix "ABCs of Probes" | 프로빙 이론 | 로딩, 그라운드 리드 인덕턴스 | https://www.tek.com/en/search?q=ABCs%20of%20Probes |
| sigrok / PulseView | 오픈소스 LA + 디코더 | 디코더 목록, 트리거 | https://sigrok.org/wiki/PulseView |
| Saleae 지원 문서 | 상용 LA 워크플로 | 아날로그+디지털 동시 캡처, 디코더 | https://support.saleae.com/ |
| Nordic Power Profiler Kit II | 전류 프로파일링 하드웨어 | 측정 모드, 디지털 입력 | https://www.nordicsemi.com/Products/Development-hardware/Power-Profiler-Kit-2 |
| Joulescope | 고동적범위 전류·전력 측정기 | 스펙과 소프트웨어 | https://www.joulescope.com/ |
| Lauterbach 문서 포털 | Trace32 매뉴얼·트레이닝 | "Training Basic Debugging", ARM Debugger 매뉴얼 | https://www.lauterbach.com/ |
| Memfault Interrupt — Cortex-M 디버그 인터페이스 심층 | DP/AP를 소프트웨어로 다루기 | MEM-AP 읽기 절차 | https://interrupt.memfault.com/blog/a-deep-dive-into-arm-cortex-m-debug-interfaces |
| Memfault Interrupt — HardFault 디버깅 | fault 레지스터 실습 | stacked frame에서 PC 복원 | https://interrupt.memfault.com/blog/cortex-m-hardfault-debug |

> **버전·벤더 의존 경고**: OpenOCD의 설정 파일 이름·`rtos`/`tpiu`/`rtt` 명령 문법, J-Link의 SWO 최대 속도와 지원 디바이스 목록, PPK2/Joulescope의 측정 범위, Trace32 명령 옵션, LA의 샘플레이트·채널 수는 **모두 버전·제품 세대·라이선스에 따라 다르다**. 면접에서 숫자를 단정하지 말고 원리로 답한 뒤 "기종마다 다릅니다"를 붙인다.

---

## 5. 예상 면접 질문 (기초 → 중급 → 심화)

### Q01. [기초] What's the difference between JTAG and SWD, and which would you use?

**왜 묻나**: 첫 필터. 이걸 못 가르면 나머지를 물을 이유가 없다.

**30초 답변**: JTAG은 TCK/TMS/TDI/TDO 4선에 데이지 체인과 boundary scan을 지원하고, SWD는 SWCLK/SWDIO 2선으로 같은 디버그 포트에 접근한다. 핀이 귀한 Cortex-M 제품에서는 거의 SWD를 쓴다. 여러 디바이스를 한 체인에 물리거나 boundary scan이 필요하면 JTAG이다. 트레이스가 필요하면 SWD에 SWO 한 핀을 더하거나, 병렬 트레이스 포트를 쓴다.

**English answer**: "JTAG is the four-wire chain — TCK, TMS, TDI, TDO — and it gives you daisy-chaining across devices and boundary scan. SWD is a two-wire physical layer that reaches the same Arm debug port, so on a pin-constrained Cortex-M product you almost always use SWD and get two pins back. I'd pick JTAG when I need multiple devices on one chain or boundary scan for manufacturing, and SWD everywhere else. If I need instruction or instrumentation trace I add SWO on top of SWD, or bring out the parallel trace port."

**꼬리질문**
- "What is SWJ-DP?" → 둘 다 지원하며 특정 시퀀스로 모드를 전환하는 디버그 포트.

### Q02. [기초] Your debugger can't connect to the target. Walk me through it.

**왜 묻나**: 모든 임베디드 엔지니어가 겪는 일. 순서가 있는지 본다.

**30초 답변**: 전원부터 — 코어 레일이 올라와 있는지 스코프로. 그다음 SWCLK가 실제로 토글하는지, nRESET이 눌린 채인지. 그다음 펌웨어가 부팅 직후 슬립에 들어가거나 SWD 핀을 GPIO로 바꾸는지 의심하고 connect-under-reset을 쓴다. 어댑터 속도를 낮추고 케이블을 짧게 한다. 마지막으로 readout protection이나 디버그 잠금 설정을 확인한다.

**English answer**: "Power first — I scope the core rail and confirm it's actually up, because no rail means no debug port. Then I check that SWCLK is toggling and that reset isn't being held low by something on the board. The most common real cause is firmware: it boots, goes to deep sleep or reconfigures the SWD pins as GPIO within milliseconds, so I connect under reset and halt at the reset vector using the reset vector catch bit. After that I drop the adapter clock and shorten the cable, since long flying leads at high SWD speeds fail intermittently. And finally I check whether readout protection or a debug lock is set — which on a production build it should be."

**꼬리질문**
- "What is vector catch?" → `DEMCR`의 `VC_CORERESET` — 리셋 직후 코어를 halt 시킨다.
- "The board is a production unit and debug is fused off. Options?" → 디버그 대신 로그·coredump·telemetry. 그래서 J07 §2.3의 계측이 필요하다.

### Q03. [기초] How do you get logs off a device? Compare UART, SWO, and RTT.

**왜 묻나**: 침습도 개념이 있는지. 저전력·실시간 기기에서는 이 선택이 버그를 좌우한다.

**30초 답변**: UART는 어디서나 되지만 비트레이트에 묶여 한 줄에 수 ms가 들고 타이밍을 바꾼다. SWO는 ITM이 TPIU를 거쳐 한 핀으로 나가는 방식이라 훨씬 빠르지만 코어 클럭 분주에 묶이고 FIFO가 넘치면 조용히 버린다. RTT는 RAM 링버퍼에 쓰고 프로브가 코어를 멈추지 않고 읽어가므로 가장 싸다 — 그래서 개발 빌드 기본값으로 쓴다.

**English answer**: "UART is universal and the worst for timing: at 115200 baud a line of text is milliseconds of blocking, which is enough to hide a race. SWO is the ITM stimulus ports going out through the TPIU on one pin — much faster, but the bit rate is derived from the core clock, so it breaks whenever the clock changes, and when the FIFO overflows it drops data silently. RTT writes into a RAM ring buffer and the probe reads that buffer over the memory access port while the core keeps running, so from the firmware's point of view it's a memcpy. On a device that changes clocks for power management, RTT is the one I'd standardize on, and I'd keep the UART path only as a fallback."

**꼬리질문**
- "What else comes out of SWO besides printf?" → DWT PC 샘플링과 예외 트레이스.

### Q04. [기초] What's the difference between a breakpoint and a watchpoint, and when do you need each?

**왜 묻나**: watchpoint를 실제로 써본 사람은 소수다. 여기서 바로 갈린다.

**30초 답변**: 브레이크포인트는 주소(명령)에서 멈추고, watchpoint는 데이터 접근에서 멈춘다. 플래시에서 실행되면 소프트웨어 BP를 못 넣으니 FPB 하드웨어 비교기를 쓰고 개수가 제한된다. watchpoint는 DWT 비교기를 쓴다. "이 변수가 언제 망가지는지 모르겠다" 부류는 watchpoint가 유일한 실용적 해법이다 — 범인의 PC가 바로 나온다.

**English answer**: "A breakpoint stops on an instruction address; a watchpoint stops on a data access. On a Cortex-M running from flash you can't just patch in a BKPT instruction, so breakpoints consume FPB comparators and there's a small fixed number of them. Watchpoints use the DWT comparators, and they're the right tool for memory corruption: when a variable is getting clobbered and I have no idea by whom, I set a write watchpoint on that address and the very next halt gives me the program counter of the culprit. The one gotcha is that if the hardware comparators run out, GDB silently falls back to single-stepping, and the target slows down by orders of magnitude."

**꼬리질문**
- "How many comparators do you get?" → 구현마다 다르다. `FP_CTRL`/`DWT_CTRL`에서 개수를 읽는다.

### Q05. [기초] Which tool do you reach for first, and why, for: no boot / sensor NACK / rare reset / too much current?

**왜 묻나**: 도구를 증상에 매핑하는 감각. J07 §3.3과 같은 표를 말로 풀 수 있는지.

**30초 답변**: 부팅 불가는 스코프(레일·리셋·클럭). 센서 NACK는 LA(I2C 디코드)로 주소 단계인지 데이터 단계인지부터. 드문 리셋은 도구가 아니라 계측 — 리셋 사유와 flight recorder를 먼저 넣는다. 전류 과다는 전류 프로파일러에 GPIO 마커를 같이 걸어 무엇이 깨어 있는지 본다.

**English answer**: "No boot goes to the scope first, because if the rails or the clock aren't right nothing above matters. A sensor NACK goes to the logic analyzer, because seeing whether the NACK is on the address byte or a data byte immediately splits the problem in half. A rare reset isn't a tool question at all — I add instrumentation first: reset reason, a flight recorder in non-initialized RAM, and a coredump, then I let it run. Excess current goes to a current profiler with debug GPIOs wired into its digital inputs, so each bump in the trace has a name."

**꼬리질문**

### Q06. [중급] Explain how a probe actually reads target memory. What is DP, AP, ROM table?

**왜 묻나**: 도구 사용자와 **도구를 이해한 사람**의 경계선.

**30초 답변**: 프로브는 SWD/JTAG으로 DP에 말을 걸고, DP를 통해 AP를 고른다. MEM-AP는 `TAR`에 주소를 넣고 `DRW`로 데이터를 읽고 쓰는 창이며 그 뒤는 AHB/AXI 버스다. AP 읽기는 파이프라인이라 값이 한 박자 늦게 나와 `RDBUFF`로 받는다. ROM table은 이 칩에 어떤 CoreSight 컴포넌트(DWT, ITM, FPB, ETM)가 어디 있는지 알려주는 목록이라, 디버거가 이걸 읽고 기능을 판단한다.

**English answer**: "The probe never talks to the core directly. It drives the debug port, which is either a SW-DP or a JTAG-DP, and through it selects an access port. For memory the relevant one is the MEM-AP: you write the address into TAR, then reads and writes through DRW go out onto the internal AHB or AXI bus — which is why the probe can read RAM while the core is running, and why RTT works. AP reads are pipelined, so the value you asked for comes back on the next transaction or from RDBUFF. And the ROM table is the discovery mechanism: the debugger walks it to find out whether this part has a DWT, an ITM, an FPB, an ETM, and at what addresses, instead of hard-coding it per chip."

**꼬리질문**
- "Why does the probe need to set CDBGPWRUPREQ?" → 디버그·시스템 전원 도메인이 꺼져 있을 수 있어서.
- "How does this let RTT run without halting?" → MEM-AP 접근이 코어 실행과 독립적이기 때문.

### Q07. [중급] You get a HardFault. Walk me through finding the offending line.

**왜 묻나**: Cortex-M 디버깅의 표준 관문. 절차가 몸에 있는지.

**30초 답변**: `CFSR`(0xE000ED28)를 읽어 MemManage/BusFault/UsageFault 중 무엇인지, `HFSR`에서 escalation인지 본다. 주소가 유효하면 `MMFAR`/`BFAR`. 그다음 `EXC_RETURN`(LR) 비트로 진입 시 MSP였는지 PSP였는지 판정해 해당 스택 포인터에서 하드웨어가 쌓은 프레임(R0-R3, R12, LR, **PC**, xPSR)을 꺼낸다. 그 PC를 `addr2line`에 넣으면 줄이 나온다.

**English answer**: "First I read CFSR to see which fault it actually is — memory manage, bus fault or usage fault — and HFSR to check whether it escalated from one of those, which is the usual case when fault handlers aren't enabled. If the valid bits are set, MMFAR or BFAR give me the offending address. Then I recover the stacked frame: the EXC_RETURN value in LR tells me whether the exception came in on the main stack or the process stack, and from that stack pointer the hardware-stacked frame gives me R0 through R3, R12, LR, the PC and xPSR. That PC goes into addr2line against the ELF and I have the source line. The one case that doesn't work is an imprecise bus fault, where the write buffer means the PC has already moved on."

**꼬리질문**
- "How do you handle the imprecise case?" → 쓰기 버퍼링을 끄거나(구현 의존) 재현 구간을 좁혀 정밀 fault로 만든다. C10 §5.5.
- "How do you get this from a field device?" → coredump로 레지스터+스택 일부를 저장해 업로드(J07 §2.3).

### Q08. [중급] How do you profile where the CPU time goes on a Cortex-M?

**왜 묻나**: 레이턴시 예산(JD의 on-device AI 항목)과 직결. 측정 없이 최적화하는 사람을 거른다.

**30초 답변**: 세 층이 있다. 함수 단위는 `DWT_CYCCNT`로 앞뒤 차이를 재는 게 가장 정확하다. 전체 분포는 SWO로 DWT PC 샘플링을 받아 통계 프로파일을 만든다. 태스크·ISR 수준은 RTOS 런타임 통계나 SystemView 같은 트레이스로 본다. 코드를 못 건드리면 ETM 명령 트레이스가 있다.

**English answer**: "For a specific function I use the DWT cycle counter — enable TRCENA and CYCCNTENA, read CYCCNT before and after, and the subtraction handles wraparound. For a whole-system view without touching the code, DWT PC sampling over SWO gives a statistical profile, the same idea as a sampling profiler on a desktop. For task and interrupt level I use the RTOS's own run-time stats, or a trace tool like SystemView over RTT, which shows me preemptions and how long each ISR ran. And if I need ground truth with zero intrusion, instruction trace through ETM. I pick the cheapest one that answers the question."

**꼬리질문**
- "Not all parts have CYCCNT." → `DWT_CTRL`의 `NOCYCCNT`로 확인. 없으면 타이머 주변장치를 쓴다.

### Q09. [중급] How do you probe a 1.8 V I2C bus properly, and what would make you distrust your own capture?

**왜 묻나**: HW 라운드의 주력 질문. 계측 자체를 의심할 줄 아는지.

**30초 답변**: LA는 임계값을 1.8 V 로직에 맞춰야 하고, 안 그러면 전부 0으로 보인다. 샘플레이트는 버스 속도의 4~10배. 상승시간이 의심되면 같은 지점을 스코프로 다시 재고, 그라운드 스프링을 써서 리드 인덕턴스가 만든 가짜 링잉을 배제한다. 그리고 프로브를 대면 되고 떼면 안 되는 현상 자체가 마진 부족의 증거다.

**English answer**: "First the threshold: on a logic analyzer I set it for 1.8 V logic, because leaving it at a 3.3 V default makes a perfectly good bus look dead. Then sample rate — I want several times the bus clock, not just Nyquist, or the edges jitter by a sample and the decoder gives up. I distrust my capture in three cases: if I used a long ground lead, because its inductance rings and invents overshoot that isn't there; if the probe's capacitance is loading an already-marginal rise time; and if the behavior changes when I attach the probe at all, which tells me the bus has almost no margin. Whenever the logic analyzer says the levels are fine but the transaction still fails, I go back with the scope and look at the analog edge."

**꼬리질문**
- "Rise time is 900 ns at 400 kHz. Is that OK?" → Fast mode 규격 tr 상한 300 ns를 넘는다. pull-up이 약하거나 버스 용량이 큼.
- "How do you pick a pull-up?" → 최소값은 sink 전류 한계, 최대값은 tr 한계. C10 §8.3.

### Q10. [중급] Set up a debugging workflow for a new project from scratch. What do you put in place on day one?

**왜 묻나**: "workflows" 요건의 정면. 팀에 남기는 사람인지.

**30초 답변**: 프로브·GDB 서버 설정 스크립트, 공용 `.gdbinit`(fault 덤프 매크로 포함), RTT 콘솔, 빌드마다 `.elf`/`.map` 보관과 빌드 ID 임베딩, 디버그 GPIO 두세 개 확보, 전원 사이클 자동화, 그리고 최소 재현 스크립트. 이게 첫날에 있으면 이후 모든 버그가 싸진다.

**English answer**: "On day one I want four things. A scripted way to flash and attach — one command, so nobody wastes time on probe configuration. A shared gdbinit with a fault macro that dumps CFSR, HFSR, the fault addresses and the stacked frame, so reading a crash is one word instead of six memory reads. RTT wired up as the console, and a couple of GPIOs reserved for debug markers that I ask hardware for during schematic review. And archiving the ELF and map file for every build with a build ID compiled into the image, because the day a device crashes in the field, the ELF is the only thing that turns an address into a line of code."

**꼬리질문**
- "What do you ask hardware for?" → 테스트 포인트, 디버그 GPIO, SWD 커넥터 접근성, 레일 션트.

### Q11. [심화] Your debugger halts the core but the system falls apart when you continue. Why, and what do you do?

**왜 묻나**: halt 디버깅의 한계를 아는지. 실시간 시스템을 실제로 다뤄본 사람만 답한다.

**30초 답변**: halt는 코어만 멈추고 타이머·DMA·라디오 링크·watchdog은 계속 돈다. 그래서 재개하면 watchdog 리셋이 나거나, 연결이 타임아웃되거나, DMA 버퍼가 이미 덮여 있다. 대처는 디버그 halt 시 주변장치를 정지시키는 설정(SoC마다 다름), 개발 빌드에서 디버그 연결 시 watchdog 비활성화, 그리고 애초에 halt 대신 트레이스·로그·watchpoint로 관찰하는 것이다.

**English answer**: "Halting stops the core, not the system. The timers keep counting, DMA keeps moving data, the radio link times out, and the watchdog eventually bites — so when I continue, I'm resuming into a world that moved on without me. Most SoCs have a debug-freeze feature that stops selected peripherals and the watchdog when the core is halted, and I turn that on for development builds. But the deeper answer is that for real-time and wireless problems I try not to halt at all: I use watchpoints and trace and RTT logging so the system keeps running, because the moment I stop it I'm no longer debugging the bug I had."

**꼬리질문**
- "What about debugging a BLE connection issue?" → halt하면 링크가 죽는다. 트레이스·스니퍼로 간다.

### Q12. [심화] The RTOS-aware thread list in GDB looks wrong. What's happening?

**왜 묻나**: 도구를 의심할 줄 아는지. 아주 소수만 답한다.

**30초 답변**: RTOS 인식은 마법이 아니라 디버그 서버가 **커널의 태스크 리스트 심볼을 읽고 구조체 오프셋을 가정해** 저장된 스택 프레임에서 레지스터를 복원하는 것이다. 심볼이 스트립됐거나, 커널 버전이 달라 오프셋이 바뀌었거나, 태스크가 컨텍스트 스위치 중간이거나, FPU 컨텍스트 유무가 다르면 조용히 틀린다. 의심되면 기능을 끄고 현재 SP에서 수동으로 스택을 풀어본다.

**English answer**: "RTOS awareness is the debug server reading the kernel's own data structures — it finds the task list symbol, walks it assuming a particular structure layout, and reconstructs each task's registers from the frame that the context switch pushed onto that task's stack. So it breaks quietly in a few ways: symbols stripped or optimized away, a kernel version whose control block layout differs from what the server expects, a task caught mid-context-switch, or lazy FPU stacking changing the frame size so every register is off by a few words. When the thread list looks implausible I turn it off, take the current stack pointer, and unwind by hand against the map file — slower, but it doesn't lie to me."

**꼬리질문**
- "How would you sanity-check it?" → 각 스레드의 PC가 그 태스크의 코드 영역인지, SP가 해당 스택 범위 안인지.
- "What's a version-independent alternative?" → 커널 훅 기반 트레이스(SystemView/Tracealyzer)나 직접 계측.

### Q13. [심화] You suspect the bug is a race in an ISR, and every instrument you add makes it disappear. What's left?

**왜 묻나**: 침습도가 0인 도구를 아는지. 여기까지 답하면 "진짜"로 분류된다.

**30초 답변**: 침습 0인 계측만 남는다 — ETM/MTB 명령 트레이스, GPIO 토글(몇 사이클), 그리고 메모리 링버퍼에 쓰고 나중에 덤프하는 로그. ISR 진입/퇴출을 GPIO로 내고 LA로 폭과 간격을 본다. DWT의 예외 트레이스로 인터럽트 빈도·지연 통계를 낸다. 그리고 코드 쪽에서는 공유 상태에 watchpoint를 걸어 누가 만졌는지 잡는다.

**English answer**: "At that point I stop adding instruments that cost time and switch to ones that cost cycles or nothing. A GPIO toggle at ISR entry and exit is a couple of instructions, and on the analyzer the pulse width is the ISR duration and a missing pulse is a lost interrupt. Exception trace from the DWT over SWO gives me entry and exit events without touching the code path. If the part has ETM or an MTB I take an instruction trace, which is genuinely zero-intrusion and will show me the exact interleaving. And for the shared variable itself, a hardware watchpoint only costs time at the moment it triggers. If all that fails, I log into a RAM ring buffer and dump it after the fact rather than streaming anything out live."

**꼬리질문**
- "MTB vs ETM?" → MTB는 ARMv6-M(Cortex-M0+)에서 실행 흐름을 RAM 버퍼에 남기는 축소판, ETM은 본격 명령 트레이스.

### Q14. [심화] How do you correlate a current-consumption anomaly with what the firmware was doing?

**왜 묻나**: JD의 전력 항목과 디버깅 항목이 만나는 지점. 실제로 해본 사람만 답한다.

**30초 답변**: 전류 측정기의 디지털 입력에 디버그 GPIO를 연결해 같은 타임라인에 올린다. 상태별로 다른 핀 패턴을 내보내면 전류 그래프의 각 구간에 이름이 붙는다. 그다음 예상 전류와 비교해 차이 나는 구간을 좁히고, 그 구간의 sleep 진입 조건·주변장치 클럭·pull-up 누설을 확인한다.

**English answer**: "I wire the firmware's debug GPIOs into the current measurement tool's digital inputs, so the current trace and the firmware state end up on the same timeline. I encode the power state on two or three pins, so every plateau and every spike in the current graph is labeled — this is the radio transmitting, this is the sensor sampling, this is supposedly deep sleep. Then I compare each region against the budget. The interesting finding is usually a floor that's higher than it should be: a peripheral clock left running, a GPIO floating, or a pull-up sinking current through a device that's powered down. C05 has the arithmetic, but the debugging technique is just labeling the timeline."

**꼬리질문**
- "Sleep current is 300 μA instead of 3 μA. First checks?" → 남아 있는 클럭·주변장치, 플로팅 입력, 디버거가 붙어 있는지(디버그 활성화 자체가 전류를 먹는다), 외부 부품 누설.
- "Does the probe affect the measurement?" → 디버그 인터페이스 활성 상태가 슬립 전류를 크게 올릴 수 있다. 반드시 분리하고 잰다.

### Q15. [심화] Tell me about a time a tool lied to you.

**왜 묻나**: 시니어 판별. 도구를 믿지 않는 경험이 있는지.

**30초 답변**: 후보가 많다 — 긴 그라운드 리드가 만든 가짜 링잉, LA 임계값 오설정으로 멀쩡한 버스가 죽어 보인 경우, ELF와 플래시가 달라 엉뚱한 줄에서 멈춘 경우, RTOS 인식이 틀린 스레드 목록을 보여준 경우. 핵심은 "계측 결과가 이상할 때 대상이 아니라 계측을 먼저 의심했다"는 서술 구조다.

**English answer**: "The general shape of my answer is: the measurement disagreed with physics, so I checked the measurement first. The classic one is ringing that only exists because of a six-inch ground lead resonating with the probe capacitance — swap to a ground spring and the overshoot vanishes. The other one I've hit is the ELF not matching what's actually in flash, so the debugger confidently stops on a line that has nothing to do with the code that's running; comparing sections catches it in seconds. Now I have a habit: before I trust a strange capture, I take the same measurement a second way, and I always capture a known-good reference first so I have something to compare against."

**꼬리질문**
- "How do you build that habit into a team?" → golden 캡처 보관, 측정 조건을 티켓에 함께 기록, 빌드 ID 임베딩.

**Don 스토리 연결**: `<확인 필요: Don이 Apple/SK hynix에서 "측정이 틀렸던" 구체 사례 — shmoo나 SI 협업 중 프로빙/장비 설정 문제로 오판했던 일이 있으면 최고의 소재>`

---

## 6. Don 매핑

### 6.1 레쥬메 증거 → 이 요건의 하위 항목

| 하위 항목 | Don의 증거 (context 3.1) | 상태 |
|---|---|---|
| JTAG/SWD 프로브 | "JTAG" 명시 + Trace32 | ✅ — 단 SWD/DP/AP 어휘로 말하는 연습 필요 |
| 명령 트레이스 | Trace32 사용 경험 | ✅ 강함 — 대부분의 지원자가 못 하는 영역. **반드시 먼저 꺼낼 것** |
| 오실로스코프 | "Oscilloscope", "DSOs" | ✅ 강함 |
| 로직 분석기 / 프로토콜 디코드 | "Logic Analyzer", "protocol Analyzer" | ✅ 강함 — 고속 인터페이스 분석기 경험 |
| 전력 측정 | "Power Analyzer" | ✅ — PPK2/Joulescope 어휘로 번역 |
| 회로도 기반 HW 협업 | "SoC verification ... bring-up" | ✅ (J13에서 상세) |
| GDB / OpenOCD / J-Link | 레쥬메 명시 없음 | 🟡 — 개념은 동일. 실습으로 메울 것 |
| RTT / SWO / ITM | 명시 없음 | 🟡 — 원리는 이 노트 §2.4~2.5로 충분히 말할 수 있다 |
| RTOS 트레이스(SystemView/Tracealyzer) | 명시 없음 | 🟡 — `<확인 필요: SSD FW에서 자체 태스크 타이밍 계측/프로파일링 도구를 만들어 쓴 적이 있는지. 있으면 "직접 만들어 썼다"가 더 센 답이다>` |
| coredump 프레임워크 | 명시 없음 (error reporting은 있음) | 🟡 |
| pre-silicon/FPGA 디버깅 | "FPGA pre-silicon bring-up" | ✅ — 트레이스·JTAG 의존도가 가장 높은 환경. 좋은 차별점 |

### 6.2 Trace32 경험을 최대치로 쓰는 법

Trace32는 **면접관이 흔히 못 보는 카드**다. 쓰는 순서를 정해 둔다.

1. **먼저 급을 세운다**: "SSD와 무선 실리콘 bring-up에서는 Lauterbach Trace32를 주로 썼습니다." — 이 한 줄로 "장난감 보드 수준이 아님"이 전달된다.
2. **무엇을 했는지 구체화한다**: 실리콘 전 FPGA 단계에서 부팅 실패를 추적, 명령 트레이스로 실행 흐름 확인, OS/태스크 인식으로 상태 확인 `<확인 필요: Don이 Trace32에서 실제로 쓴 기능 범위 — 단순 halt/step/메모리인지, 트레이스·스크립트까지인지. 과장 금지>`
3. **즉시 번역해 준다**: "같은 작업을 J-Link와 GDB로 하면 `monitor reset halt` + `load` + watchpoint고, 트레이스는 ETM/MTB에 해당합니다."
4. **스크립트 습관을 언급한다**: PRACTICE `.cmm` ↔ `.gdbinit`/GDB Python. **도구가 아니라 자동화 습관**을 보여주는 대목.
5. **겸손 한 줄로 닫는다**: "Zephyr/`west debug` 흐름은 최근에 직접 해보고 있습니다." — 거짓말 없이 학습 의지를 보인다.

### 6.3 DSO·프로토콜 분석기 경험 번역

| Don이 한 것 | Hark 맥락의 대응 | 말하는 법 |
|---|---|---|
| 고속 인터페이스(PCIe 등) 프로토콜 분석기 캡처 | I2C/SPI/I2S LA 디코드 | "링크 계층 이벤트를 캡처해 어느 계층에서 깨졌는지 보던 일과, I2C NACK를 보는 일은 같은 사고방식입니다" |
| shmoo (전압·타이밍 스윕) | 코너에서 마진 찾기(J07 §2.5) | "발생률을 축을 흔들어 움직여 보는 것이 제 기본 접근입니다" |
| SI 팀 협업 / health monitoring | HW 엔지니어와 측정치로 대화 | "신호 무결성 데이터를 펌웨어 설정에 반영해 본 경험이 있습니다" |
| Power Analyzer | PPK2/Joulescope + GPIO 마커 | "장비는 다르지만 하는 일은 전류 파형에 코드 이름을 붙이는 것입니다" |
| SPMI/RFFE 디버깅 | 저속 제어 버스 디버깅 전반 | "I2C와 같은 부류의 버스입니다 — 오픈드레인, 주소, 타이밍 마진" |

### 6.4 갭을 정직하게 메우는 문장

- RTOS 트레이스 툴: "SystemView나 Tracealyzer를 프로덕션에서 쓴 적은 없습니다. SSD 펌웨어에서는 자체 계측으로 태스크 타이밍을 봤고, 지금 nRF 보드에서 SystemView를 붙여 보고 있습니다." `<확인 필요: 실제로 해보고 나서 말할 것>`
- Zephyr 워크플로: "`west build/flash/debug` 흐름과 coredump 백엔드는 최근에 익히는 중입니다. 밑단은 결국 OpenOCD/J-Link라 낯설지 않습니다."
- 지어내지 말 것: 써본 적 없는 도구를 "써봤다"고 말하면 꼬리질문 한 번에 무너진다. 이 노트의 §2는 **원리를 정확히 말하기 위한 것**이지 경험을 위조하기 위한 것이 아니다.

---

## 7. 준비 체크리스트

- [ ] §2.1의 DP → AP → 버스 다이어그램과 `TAR`/`DRW`/`RDBUFF` 역할을 화이트보드에 2분 안에 그린다
- [ ] §2.2의 주소 6개(`DHCSR`, `DEMCR`, `DWT_CTRL`, `DWT_CYCCNT`, `FP_CTRL`, `CFSR`)를 외우거나, 최소한 "어느 블록의 무엇인지"를 말할 수 있다
- [ ] UART vs SWO vs RTT 비교를 침습도·실패 모드까지 포함해 60초로 말한다 (Q03)
- [ ] nRF52840 DK 같은 보드에서 OpenOCD + GDB로 연결 → `monitor reset halt` → watchpoint로 변수 손상 잡기를 실제로 한 번 해본다
- [ ] 같은 보드에서 RTT 콘솔을 띄우고, blocking/non-blocking 모드 차이를 직접 관찰한다
- [ ] `.gdbinit`에 §3.2의 `fault` 매크로를 넣고, 일부러 HardFault를 내서 stacked PC → `addr2line`까지 한 번 완주한다
- [ ] `DWT_CYCCNT`로 함수 하나를 측정해 본다 (Q08의 실물 근거)
- [ ] Trace32 경험을 §6.2의 5단계 순서대로 90초 스크립트로 작성하고 소리 내어 읽는다
- [ ] Q06·Q11·Q12·Q13의 English answer를 각 60초 안에 말한다 (심화 4개가 이 노트의 차별 포인트)
- [ ] 프로빙 계산 두 줄(`BW ≈ 0.35/Tr`, `Tr_measured ≈ sqrt(합)`)과 LA 4~10배 규칙을 암기한다
- [ ] 역질문 준비: "MCU 쪽은 어떤 프로브와 RTOS 툴체인을 쓰나요? coredump나 필드 crash 수집 체계는 이미 있나요?"

---

## 8. 더 읽기

| 어디 | 무엇을 보충하나 |
|---|---|
| J07 | 같은 주제의 **업무 관점** — triage, HW/FW/ML 소유권, 계측 설계, root cause 보고서 |
| C10 §1 | SWD 트랜잭션 파형, JTAG TAP 상태 머신, 10핀 커넥터 핀맵 |
| C10 §2.1~2.2 | 프로브 비교표, GDB+OpenOCD / J-Link GDB Server 상세 흐름 |
| C10 §2.3 | **Trace32 ↔ J-Link+GDB 명령 대응표** (이 노트 §3.4의 실제 명령판) |
| C10 §3 | FPB/DWT 상세, 코드에서 직접 watchpoint 거는 법 |
| C10 §4 | ITM/SWO 코드와 RTT 설정 예제 |
| C10 §5 | HardFault 전체 해석: 스택 프레임, CFSR 비트 해설, 핸들러 구현, imprecise fault |
| C10 §6~7 | LA 샘플링·트리거, 스코프 대역폭·프로빙·전원 램프 |
| C04 | RTOS 내부 — 스레드 인식 디버깅이 무엇을 읽는지 이해하려면 |
| C05 | 전력 측정과 sleep 상태 — Q14의 계산 근거 |
| S06 D01~D12 | 시나리오 12개. 이 노트의 도구 지식을 실제 사건에 적용하는 연습 |
| S06 §6 | 디버깅 영어 표현 — Q01~Q15를 자기 말로 바꿀 때 |
| J13 | 회로도 읽기와 bring-up 협업 |
