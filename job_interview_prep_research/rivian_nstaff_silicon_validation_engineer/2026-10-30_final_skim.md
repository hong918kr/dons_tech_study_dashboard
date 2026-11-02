# 면접 직전 훑어보기 — volatile · const 포인터 · 임베디드/실리콘 검증 핵심
<!-- theme: pastel -->

> **대상**: 10-30(금) 45분 · Manjiang Zhang (HW 출신 Sr Mgr Silicon Validation) · **사용법**: 위에서 아래로 쭉 훑기 · 굵은 글씨와 영어 한 줄만 읽어도 됨 · **같이 볼 것**: [C 기본기 벼락치기](2026-10-29_c_basics_cram.html) · [HM screen deck](2026-10-25_rivian_hm_screen_prep.html)

## TL;DR — 30초 버전

- **volatile** = "이 메모리는 내가 모르는 사이에 바뀌거나, 읽고 쓰는 것 자체가 부수효과다 → 컴파일러야 **매번 진짜로 읽고 써라**". **atomic도 아니고 memory barrier도 아니다.**
- **`const char *p`** = 내용 고정(포인터는 이동 가능) · **`char *const p`** = 포인터 고정(내용은 수정 가능) · **오른쪽에서 왼쪽으로 읽기**.
- 이 포지션 = **bare-metal C로 SoC 블록을 켜고, 자동 테스트·커버리지·벤치마크 툴을 만드는 SW 쪽 silicon validation**. HW 면접관은 "**레지스터 · 인터럽트 · DMA/캐시 · 버스 · 실패했을 때 HW냐 SW냐 테스트냐 가르는 법**"을 본다.

## 1. volatile 완전 정리

### 1.1 한 문장

- "volatile tells the compiler every read and write to this object is observable, so it must perform each access exactly as written, in order — no caching in a register, no merging, no removing."

### 1.2 volatile 이 **없으면** 생기는 일 (이 예시 하나로 설명)

```c
uint32_t *status = (uint32_t *)0x40001004u;   /* volatile 없음 */
while ((*status & BIT(0)) == 0)               /* "ready 될 때까지 대기" */
    ;
```

- 컴파일러는 루프 안에서 `*status` 를 바꾸는 코드가 없으니 **한 번만 읽고 레지스터에 캐시** → `-O2` 에서 **무한 루프** (또는 루프 자체 삭제).
- `volatile uint32_t *status` 로 바꾸면 매 반복 실제 버스 read 발생 → HW가 bit을 세우면 탈출.
- `-O0` 에선 잘 되다가 **릴리스 빌드에서만 깨지는** 대표 버그 → "works in debug, hangs in release".

### 1.3 언제 쓰나 (4가지)

| 경우 | 예 | 이유 |
|---|---|---|
| **MMIO 레지스터** | `#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))` | HW가 값을 바꿈 + 읽기/쓰기 자체가 동작(FIFO pop, W1C, RC) |
| **ISR ↔ main 공유 변수** | `static volatile bool rx_done;` | ISR이 바꾼 걸 main 루프가 매번 다시 읽어야 함 |
| **다른 버스 마스터가 바꾸는 메모리** | DMA descriptor의 status/owner 비트 | CPU 모르게 DMA 엔진이 씀 (캐시 처리는 별도!) |
| `setjmp/longjmp` 사이에 바뀌는 지역 변수 | 드묾 | 레지스터 복원 문제 |

### 1.4 volatile 이 **보장하지 않는 것** ★ (Staff 레벨 질문은 여기)

| 오해 | 사실 | 대신 쓸 것 |
|---|---|---|
| "volatile이면 atomic" | `count++` 는 여전히 **load → add → store 3단계**. ISR이 사이에 끼면 증가 하나 사라짐 | IRQ off 구간, C11 `_Atomic`, `LDREX/STREX`, `__atomic_fetch_add` |
| "volatile이면 순서 보장" | **volatile 접근끼리만** 순서 유지. 일반 변수 접근은 그 사이로 재배치될 수 있음 | compiler barrier `asm volatile("" ::: "memory")` |
| "volatile이면 CPU도 순서대로" | 컴파일러만 묶음. **CPU/버스는 재정렬·버퍼링** 가능 (ARM weakly ordered) | `dmb` / `dsb` / `isb`, C11 release/acquire |
| "volatile이면 DMA 데이터 보임" | **캐시**는 별개. 캐시에 옛날 값이 남아 있으면 volatile로 읽어도 옛날 값 | DMA 전 **clean**, DMA 후 **invalidate**, 또는 non-cacheable 영역 |
| "멀티코어 동기화 OK" | 위 이유로 안 됨 | spinlock, atomics, mailbox HW |

- 영어: "volatile is about the compiler, not the CPU. It doesn't make anything atomic, and it doesn't order memory across cores or with DMA — for that I need atomics, barriers, and cache maintenance."

### 1.5 선언 위치별 의미 (const와 같은 규칙)

| 선언 | 뜻 | 쓰는 곳 |
|---|---|---|
| `volatile uint32_t *p` | **가리키는 값**이 volatile | MMIO 레지스터 포인터 (99% 이것) |
| `uint32_t *volatile p` | **포인터 변수 자체**가 volatile | ISR이 포인터를 바꾸는 경우 (예: 현재 버퍼 포인터) |
| `volatile uint32_t *volatile p` | 둘 다 | 드묾 |
| `const volatile uint32_t *p` | SW는 못 쓰고(const) HW가 바꿈(volatile) | **read-only 상태 레지스터** |
| `typedef struct { volatile uint32_t CTRL; ... } regs_t;` | 멤버마다 volatile | 레지스터 맵 struct |

- `const volatile` 이 모순 아닌가? → 아님. **const = "내 코드가 못 쓴다"**, **volatile = "누군가 바꿀 수 있다"**. 서로 다른 주체.

### 1.6 barrier 3종 (ARM) — volatile 다음에 반드시 나오는 질문

| 명령 | 의미 | 쓰는 때 |
|---|---|---|
| **DMB** (Data Memory Barrier) | 앞 메모리 접근이 뒤 메모리 접근보다 **먼저 보이게** (순서만) | DMA descriptor 채운 뒤 doorbell 쓰기 전, ring buffer publish |
| **DSB** (Data Synchronization Barrier) | 앞 메모리 접근이 **완료될 때까지 CPU 정지** | 인터럽트 clear 후 IRQ 다시 켜기 전, 전원/클럭 레지스터 쓴 뒤, WFI 전 |
| **ISB** (Instruction Sync Barrier) | 파이프라인 flush, 이후 명령은 새 설정으로 | MMU/VBAR/시스템 레지스터 바꾼 뒤, 코드 수정 후 |
| compiler barrier | `asm volatile("" ::: "memory")` — CPU 명령은 없고 컴파일러 재배치만 막음 | 단일 코어 ISR ↔ main |

- 전형적 순서: `descriptor 쓰기 → DMB → DOORBELL 레지스터 쓰기`. "Otherwise the device can see the doorbell before the descriptor."
- **posted write**: 레지스터 write가 버스에서 늦게 도착 가능 → "IRQ clear 했는데 ISR이 또 들어옴(spurious)" → clear 후 **같은 디바이스 레지스터 read-back** 또는 DSB.

## 2. const 포인터 완전 정리

### 2.1 규칙 하나: **변수 이름에서 시작해 오른쪽→왼쪽으로 읽기**, `const` 는 **바로 왼쪽**(왼쪽에 없으면 바로 오른쪽)에 붙는다

| 선언 | 읽는 법 | `*p = 'x'` | `p++` / `p = q` |
|---|---|---|---|
| `const char *p` | p is a pointer to **const char** | ❌ | ✅ |
| `char const *p` | (위와 **완전히 같음**) | ❌ | ✅ |
| `char *const p = buf;` | p is a **const pointer** to char | ✅ | ❌ (선언 때 초기화 필수) |
| `const char *const p = buf;` | const pointer to const char | ❌ | ❌ |
| `const char **pp` | pointer to pointer to const char | `**pp` ❌ | `*pp`, `pp` ✅ |
| `char *const *pp` | pointer to const pointer to char | `**pp` ✅ | `*pp` ❌ |

### 2.2 컴파일 되는 것 / 안 되는 것 (눈으로 확인)

```c
char buf[8] = "abc", other[8] = "xyz";

const char *p1 = buf;
p1 = other;          /* OK  — pointer can move            */
/* *p1 = 'Z'; */     /* ERR — pointed-to data is const     */

char *const p2 = buf;
*p2 = 'Z';           /* OK  — data is writable             */
/* p2 = other; */    /* ERR — pointer itself is const      */

const char *const p3 = buf;   /* neither */
```

### 2.3 실무에서 의미

- **`size_t my_strlen(const char *s)`** — "이 함수는 네 문자열을 안 바꾼다"는 **API 약속**. 리터럴(`"abc"`)도 넘길 수 있음.
- **`char *const` 는 드묾** — 포인터를 고정하고 싶을 때. 레지스터 베이스 주소 상수: `volatile uint32_t *const UART0_DR = (volatile uint32_t *)0x40002000u;` (포인터는 고정, 레지스터 값은 바뀜).
- **const 전역은 flash(.rodata)** 에 들어가 RAM 절약 → 룩업 테이블, CRC 테이블은 `static const uint8_t tbl[256]`.
- `char *` → `const char *` 변환은 자동 OK. 반대(const 떼기)는 캐스트 필요하고, 원래 const 객체를 그걸로 쓰면 **UB**.
- `char **` → `const char **` 는 **자동 변환 안 됨** (const 구멍이 생겨서). 물어보면 "it would let you sneak a const pointer into a non-const slot".
- C의 `const int N = 10;` 은 **상수 표현식이 아님** → 배열 크기/`case` 에 못 씀(C++과 다름) → `#define` 또는 `enum`.
- 영어: "I read declarations right to left: `const char *p` is a pointer to const char — I can move the pointer but not write through it. `char *const p` is a const pointer — fixed address, writable data. For register bases I use `volatile uint32_t *const`."

## 3. C 함정 빠른 복습 (한 줄씩)

| 질문 | 답 |
|---|---|
| `1 << 31` | int signed overflow **UB** → `1u << 31` |
| `x << 32` (x는 32-bit) | **UB** (shift ≥ 폭) |
| 음수 `>>` | implementation-defined (보통 산술 shift) → 비트 조작은 unsigned로 |
| `uint8_t a=200,b=100; a+b` | int 승격 → **300** |
| `-1 < sizeof(int)` | **false** (-1 → size_t 거대한 수) |
| `char` 의 부호 | 구현 정의 (ARM은 보통 unsigned, x86은 signed) → `uint8_t`/`int8_t` 명시 |
| `sizeof(arr)` 함수 인자에서 | 배열은 포인터로 decay → 포인터 크기 → 길이를 따로 넘김 |
| `struct {char a; int b; char c;}` | 보통 **12** → 큰 멤버부터 놓으면 **8** |
| `#pragma pack` / `packed` | 패딩 없음, 대신 unaligned access (느림 / M0에선 fault) |
| `#define SQ(x) ((x)*(x))` 에 `SQ(i++)` | i 두 번 증가 → `static inline` 함수 |
| `static` 지역 변수 | 호출 사이 값 유지, .data/.bss, **재진입 불가** (ISR에서 주의) |
| 비트필드 struct로 레지스터 매핑 | 비트 순서·접근 폭이 구현 정의 → 마스크 매크로 권장 |
| `int *p; *p = 5;` | 초기화 안 된 포인터 → UB |
| `memcpy` 겹침 | UB → `memmove` |
| 엔디안 | ARM/x86/RISC-V little · PCIe config space little · 네트워크 바이트 순서 big → 바이트 배열은 shift로 조립 |

## 4. 임베디드 기본 개념 (bare-metal 검증 SW가 매일 쓰는 것)

### 4.1 Boot / 메모리 레이아웃

| 개념 | 핵심 |
|---|---|
| reset 후 순서 | reset vector → **startup.s**: 스택 포인터 설정 → (클럭/PLL·DDR init) → **.data를 flash→RAM 복사** → **.bss 0 채우기** → `main()` |
| 섹션 | `.text` 코드(flash) · `.rodata` const(flash) · `.data` 초기값 있는 전역(flash에 원본, RAM에 실행) · `.bss` 0 전역(RAM) · stack · heap |
| linker script | 메모리 영역(FLASH/SRAM/DDR 주소·크기)과 섹션 배치. DMA 버퍼를 non-cacheable 영역에 두는 것도 여기 |
| 스택 오버플로 감지 | 스택 끝에 패턴(0xDEADBEEF) 칠하고 검사, MPU guard 영역, 최대 사용량 high-water mark |
| 멀티 스테이지 부트 (SoC) | BootROM → FSBL/SPL(DDR training) → ATF/BL31 → U-Boot → Linux. 검증 초기엔 **BootROM 다음 바로 bare-metal 테스트 이미지** 로드 (JTAG로 SRAM에 load & run) |

### 4.2 인터럽트

| 개념 | 핵심 |
|---|---|
| ISR 규칙 | 짧게 · 블로킹/printf/malloc 금지 · **원인 clear** · 플래그나 링버퍼로 넘기고 main/task에서 처리 · 공유 변수 volatile, RMW는 보호 |
| level vs edge | level: 원인 clear 안 하면 **계속 재진입(IRQ storm)**. edge: 펄스를 놓치면 영영 못 받음 |
| clear 순서 | **디바이스 원인 clear → (read-back/DSB) → 인터럽트 컨트롤러 EOI** |
| GIC (Cortex-A) | **SGI**(0–15 SW 코어 간) · **PPI**(16–31 코어 전용, 예: 타이머) · **SPI**(32+ 공유 주변장치). Distributor → CPU interface. IAR 읽어서 ack, EOIR 써서 끝 |
| NVIC (Cortex-M) | 우선순위 숫자 **작을수록 높음**, tail-chaining, 자동 레지스터 stacking, preemption |
| 인터럽트 latency 측정 | GPIO toggle + 오실로스코프, 또는 HW 타임스탬프 vs ISR 진입 시 cycle counter |
| 검증 관점 | 인터럽트 테스트 = **trigger → pending 확인 → mask/unmask 동작 → ISR 진입 → clear → 다시 안 들어오는지** |

### 4.3 캐시 · DMA · MMU

| 개념 | 핵심 |
|---|---|
| DMA TX (메모리→디바이스) | CPU가 버퍼 씀 → **cache clean(flush)** → descriptor 준비 → DMB → doorbell |
| DMA RX (디바이스→메모리) | (시작 전 invalidate) → DMA 완료 IRQ → **cache invalidate** → CPU가 읽음 |
| 캐시 라인 정렬 | 버퍼가 다른 변수와 같은 라인이면 invalidate가 옆 변수를 날림 → **버퍼를 캐시 라인(64B) 정렬·크기 배수로** |
| write-back vs write-through | WB: 캐시에만 쓰고 나중에 내려감(빠름, DMA 주의) · WT: 매번 메모리까지 |
| memory type (ARMv8) | **Device**(nGnRE 등, 레지스터: 재정렬·병합·추측 접근 X) vs **Normal**(DDR, 캐시 가능) — MMIO를 Normal로 매핑하면 이상한 버그 |
| MMU vs MPU | MMU: 가상→물리 + 속성 (Cortex-A). MPU: 영역별 권한/속성만 (Cortex-M/R) |
| coherent interconnect | HW가 캐시 일관성 유지(ACE/CHI, CCI) → SW 캐시 관리 불필요. 검증에선 **coherent 경로와 non-coherent 경로 둘 다** 테스트 |
| descriptor ring | owner bit(HW/SW), 완료 status, wrap 비트. SW는 owner=SW인 것만 처리하고 다시 HW에게 넘김 |

### 4.4 동시성

| 개념 | 핵심 |
|---|---|
| race condition | 공유 자원 RMW가 끊김 → IRQ off(짧게), lock, atomic |
| critical section (bare-metal) | `primask = __get_PRIMASK(); __disable_irq(); ... ; __set_PRIMASK(primask);` — **원래 상태로 복원** (중첩 대비) |
| spinlock vs mutex | spinlock: 바쁜 대기, 짧은 구간, ISR 가능, 멀티코어 · mutex: sleep, 긴 구간, ISR 불가 |
| priority inversion | 낮은 task가 lock 쥐고 중간 task에 밀림 → 높은 task 굶음 → **priority inheritance** |
| deadlock | lock 순서 고정, timeout |
| 재진입(reentrant) | static/전역 상태 안 쓰는 함수. ISR과 main 양쪽에서 부르는 함수는 재진입 가능해야 |

## 5. 버스 / 인터페이스 (JD 우대사항: JTAG · I2C · UART · SPI · Ethernet · DDR · PCIe · MIPI)

| 버스 | 1줄 핵심 | 자주 나오는 디버그 질문 → 답 |
|---|---|---|
| **I2C** | 2선(SCL/SDA), **open-drain + pull-up**, 7-bit 주소, ACK/NACK, clock stretching, 100k/400k/1M | "NACK 나옴" → 주소(7 vs 8-bit 표기), 전원/리셋, pull-up. "**버스가 SDA low로 멈춤**" → SCL을 **9번 토글** 후 STOP |
| **SPI** | 4선(SCLK/MOSI/MISO/CS), full-duplex, **CPOL/CPHA 4모드**, 주소 없음(CS로 선택) | "값이 1비트씩 밀림" → **모드(CPHA) 불일치**. 첫 바이트 0xFF → CS 타이밍/MISO tri-state |
| **UART** | 비동기, 8N1(start+8+stop), baud 일치 필요(오차 ~2–3% 이내) | "깨진 글자" → baud/클럭 소스 오차. overrun → FIFO/DMA, flow control(RTS/CTS) |
| **JTAG** | TAP state machine, TCK/TMS/TDI/TDO(/TRST), IR/DR 스캔, **boundary scan**, ARM은 DAP/SWD로 메모리·레지스터 접근 | bring-up 첫날 도구: IDCODE 읽기 → 코어 halt → SRAM에 이미지 load & run → 레지스터 peek/poke |
| **Ethernet** | MAC(디지털) ↔ PHY(아날로그) 사이 MII/RGMII/SGMII, **MDIO로 PHY 레지스터**, FCS=CRC32 | "링크 안 뜸" → MDIO로 PHY ID·link status, auto-neg, **loopback(MAC/PHY/외부)** 으로 구간 나누기, PRBS로 BER |
| **PCIe** | 직렬 lane, TLP/DLLP, **LTSSM link training**(Detect→Polling→Config→L0), enumeration(config space, BAR sizing), MSI/MSI-X, AER | "링크가 Gen1로만" → 신호 품질/equalization, LTSSM 상태 로그. "디바이스 안 보임" → config read가 0xFFFFFFFF, CRS, reset 타이밍(PERST#) |
| **DDR** | 컨트롤러 + PHY, **training**(write leveling, read gate, eye centering), refresh, ECC | "랜덤 비트 에러" → **march test**(주소/데이터 라인 stuck·coupling), 온도·전압 margin(shmoo), training 결과 eye 폭 |
| **MIPI CSI-2** | 카메라 → SoC, D-PHY/C-PHY lane, 패킷(Short: FS/FE/LS/LE, Long: 픽셀 데이터 + CRC), virtual channel, data type | "프레임 깨짐" → ECC/CRC 에러 카운터, lane deskew, 라인 수/프레임 카운터 일치, FS/FE 짝 |

## 6. 실리콘 검증(post-silicon validation) — 이 포지션 핵심

### 6.1 큰 그림 — 칩 하나가 나오기까지 SW 검증이 하는 일

| 단계 | 어디서 | 검증 SW가 하는 일 |
|---|---|---|
| Pre-silicon | **RTL 시뮬레이션 / 에뮬레이터(Palladium/Veloce) / FPGA 프로토타입(HAPS)** | 같은 bare-metal 테스트를 미리 돌려 **첫 실리콘 전에 테스트를 디버그** — "tests ready on day 1" |
| Bring-up | 첫 실리콘 + 평가 보드 | 전원/클럭/리셋 → JTAG 연결 → BootROM → SRAM 테스트 → DDR training → 블록 하나씩 enable |
| Functional validation | 실험실 보드 | 블록별 기능 테스트(레지스터, 인터럽트, DMA, 데이터 경로), **커버리지 리포트** |
| Characterization | 온도 챔버, 가변 전원 | **PVT**(Process·Voltage·Temperature) 코너, **shmoo**(전압×주파수 pass/fail 맵), margin |
| Performance | 실험실 | 벤치마크: throughput · latency · utilization · 주파수별 성능 |
| Production | 공장 | **SLT**(System Level Test), ATE와 상호보완, 테스트 시간 최적화 |

- 영어: "Pre-silicon, I'd run the same bare-metal tests on FPGA or emulation so by the time silicon arrives the tests are already debugged and any failure is more likely a real silicon issue."

### 6.2 레지스터 테스트 (가장 기본, 가장 많이 물어봄)

| 테스트 | 방법 | 잡는 것 |
|---|---|---|
| reset 값 | 리셋 직후 전체 레지스터 읽어서 spec(IP-XACT/SystemRDL 생성 헤더)과 비교 | 스펙-RTL 불일치 |
| RW walk | 0x0, 0xFFFFFFFF, 0xAAAAAAAA, 0x55555555, walking-1 써서 다시 읽기 (**RW 비트만** 마스크) | stuck bit, 주소 디코드 |
| RO | 써도 안 바뀌는지 | 쓰기 보호 |
| W1C / RC / WO | 1 쓰면 clear, 읽으면 clear, 읽으면 0 | 접근 타입 |
| aliasing | 레지스터 A 쓰고 다른 레지스터 전부 안 바뀌었는지 | 주소 디코드 오류 |
| 주의 | **side effect 있는 레지스터(FIFO, 시작 비트, 리셋 비트)는 walk 금지** → 제외 리스트 | |

### 6.3 실패 났을 때 — HW냐 SW냐 테스트냐 (★ HW 면접관 최애 질문)

1. **재현**: 몇 번 중 몇 번? 특정 보드/칩만? 온도·전압 의존? → 간헐적이면 통계로.
2. **테스트 자체부터 의심**: 같은 테스트가 FPGA/에뮬레이터에선 pass? **golden model**(C reference)과 비교? 다른 칩(다른 lot)에서는?
3. **구간 나누기(bisect)**: loopback, 블록 단위로 끊기, 데이터 패턴 바꾸기, 클럭 낮추기, 캐시 끄기, DMA 대신 PIO.
4. **관측성 올리기**: 레지스터 덤프, 에러 카운터, 타임스탬프 로그, **로직 분석기/오실로스코프**, 칩 내부 trace/ILA.
5. **margin 확인**: 전압·주파수·온도 바꿔 shmoo → 경계에서만 실패하면 타이밍/아날로그 문제 쪽.
6. **결론과 기록**: silicon erratum이면 SW workaround + 재현 테스트를 회귀에 추가, RTL/설계팀에 재현 절차 전달.

- 영어: "First I make sure the test isn't the bug — run it on the FPGA build and against a golden model. Then I bisect: loopbacks, lower clocks, caches off, PIO instead of DMA. If it only fails at voltage or temperature corners, it points at timing margin rather than logic."

### 6.4 자동화 · 커버리지 · 회귀

| 항목 | 핵심 |
|---|---|
| 테스트 프레임워크 | 테스트 = 이름 + setup/run/teardown + **pass/fail + 에러 코드 + 로그**. 결과는 **UART/공유 메모리(mailbox)** 로 호스트에 보고 |
| 호스트 측 | Python이 보드 전원 제어, JTAG/UART로 이미지 로드·실행, 결과 파싱, DB/대시보드 |
| 커버리지 | **기능 커버리지**(스펙 기능·모드·레지스터 필드 중 몇 %를 테스트했나) · **코너 커버리지**(PVT 조합) · 코드 커버리지는 보조 |
| 회귀(regression) | 매 FW/RTL/보드 변경마다 nightly, **flaky 테스트 따로 추적**(재시도 통계), 보드 팜 |
| seed | 랜덤 테스트는 seed를 로그에 남겨 **재현 가능**하게 |
| timeout / watchdog | 테스트가 멈춰도 다음 테스트로 → HW watchdog 또는 호스트 timeout + 전원 사이클 |

### 6.5 성능 벤치마크 (JD: throughput · latency · utilization · IO/compute frequency) — 자세히: [SSD vs SoC 비교](2026-10-30_perf_metrics_ssd_vs_soc.html)

| 항목 | 핵심 |
|---|---|
| 시간 측정 | **PMU cycle counter**(CCNT/PMCCNTR) 또는 64-bit 시스템 타이머, 오버헤드 측정 후 빼기 |
| throughput | bytes / 시간 (MB/s, GB/s), 이론값(버스 폭 × 클럭 × 효율)과 비교해서 **% of peak** 로 말하기 |
| latency | 평균 말고 **p50 / p99 / max**, 히스토그램. 첫 실행(cold cache) 따로 |
| utilization | 실제 처리량 / 이론 peak, 또는 busy cycle / 전체 cycle (HW 성능 카운터) |
| 노이즈 줄이기 | warm-up, 반복 N회, 인터럽트 끄기, 클럭 고정(DVFS off), 캐시 상태 통제 |
| 주파수 스윕 | IO/compute 클럭 바꿔서 성능이 비례하는지 → 비례 안 하면 **다른 병목**(메모리 대역폭) |

### 6.6 AI accelerator (RAP1) 관련 — 가볍게

| 개념 | 핵심 |
|---|---|
| NPU 구조 | MAC array(systolic) + on-chip SRAM + DMA + 제어 코어. 성능 = **TOPS** (MAC 수 × 2 × 클럭) |
| int8 양자화 | `real = scale × (q - zero_point)`, 누적은 int32, 마지막 requantize(곱셈 + shift + 반올림 + saturate) |
| 검증 | **golden model**(CPU 위 C/Python reference)과 **bit-exact** 비교, 또는 허용 오차. 레이어 단위로 비교해 첫 불일치 위치 찾기 |
| 병목 | roofline: 연산 집약도 낮은 레이어는 **메모리 대역폭 바운드** → DDR/SRAM 대역폭 측정이 중요 |
| 자동차 | 카메라 여러 개(CSI-2) → ISP → NPU, 지연·결정성, **기능 안전(ISO 26262)**: ECC, lockstep, BIST, watchdog |

## 7. 자주 받는 질문 → 영어 한 줄 답

| 질문 | 답 |
|---|---|
| What does volatile do? | "Forces every access to actually happen, in order — for MMIO and ISR-shared data. It's not atomic and not a barrier." |
| const char * vs char * const? | "Pointer to const data versus const pointer to mutable data — read it right to left." |
| How do you write an ISR? | "Keep it short, clear the source, hand off via a flag or ring buffer, no blocking calls." |
| How do you share data between ISR and main? | "volatile for visibility, plus a critical section or a lock-free SPSC ring for anything multi-word." |
| DMA and cache? | "Clean before the device reads, invalidate before the CPU reads, align buffers to cache lines." |
| How do you bring up a new chip? | "Power, clocks, reset, then JTAG IDCODE, run from SRAM, memory tests, DDR training, then block by block." |
| How do you know your test is correct? | "Run it pre-silicon on FPGA or emulation, compare against a golden model, and inject faults to prove it can fail." |
| Failure only on some chips? | "Collect stats across parts and corners, shmoo voltage and frequency — it's likely margin, not logic." |
| How do you measure performance? | "Cycle counter, warm-up, many iterations, report percentiles and percent of theoretical peak." |
| Why bare-metal for validation? | "Full control and determinism, no OS noise, direct register access — so failures point at silicon." |

## 8. 들어가기 직전 5줄

- 코딩 전 **clarify 한 문장**, 코딩 후 **테스트 3개** 소리 내기.
- 코드에 먼저 쓰는 3단어: **`volatile` · `1u` · `(uint64_t)`**.
- 레지스터 필드: **clear → OR → mask**, W1C에 `|=` 금지.
- 실패 디버그: **테스트부터 의심 → bisect → margin(shmoo)**.
- 모르면: "I haven't done that directly, but here's how I'd approach it…" → 비슷한 경험(Solidigm FPGA bring-up, Apple RF integration)으로 연결.
