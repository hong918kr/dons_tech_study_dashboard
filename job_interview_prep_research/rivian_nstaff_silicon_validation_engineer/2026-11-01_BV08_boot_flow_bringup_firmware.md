# BV08 · 부트 흐름 · bring-up 테스트 펌웨어 (BootROM → FSBL → DDR → 앱)
<!-- theme: pastel -->

> **묶음**: C 첫 전원 인가·JTAG·부트 · **우선**: P1 ("How is your bring-up test firmware structured?" — 이 포지션 일상 업무 그 자체) · **선행 노트**: [BV07 JTAG·SWD·boundary scan](2026-11-01_BV07_jtag_swd_boundary_scan.html), [BV05 리셋·strap·boot mode](2026-11-01_BV05_reset_strap_boot_mode.html) · **다음 노트**: [BV09 DDR bring-up·training·memtest](2026-11-01_BV09_ddr_bringup_training_memtest.html) · **목록**: [BV 트랙 목록](2026-11-01_BV00_board_validation_track.html), 관련: [BV06 첫 전원 인가](2026-11-01_BV06_first_power_on_checklist.html), [BV14 검증 계획·자동화](2026-11-01_BV14_board_validation_plan_automation.html), [LP16 bare-metal vs Linux · Zynq](2026-10-31_LP16_baremetal_vs_linux_zynq.html), [LP14 검증 방법론](2026-10-31_LP14_validation_methodology.html), [C 기초 정리](2026-10-29_c_basics_cram.html) · **코드**: [onsitePrep/bv08/bringup_fw_sim.c](onsitePrep/bv08/bringup_fw_sim.c) + [parse_results.py](onsitePrep/bv08/parse_results.py) (macOS ASan/UBSan 빌드·실행, 출력 [bringup_fw_sim.out](onsitePrep/bv08/bringup_fw_sim.out))

## 0. TL;DR

- 이 노트로 답할 수 있는 것: **"Walk me through the boot flow of an Arm SoC"**, "DDR은 어디서·왜 거기서 초기화되나", "UART가 뜨기 전 hang은 어떻게 디버그하나", **"How would you structure a bring-up test firmware?"**, "자동화는 PASS를 어떻게 아나", "FPGA와 실리콘에서 같은 FW를 어떻게 돌리나", "레지스터 reset 값/RW 테스트를 짜 봐 — 쓰면 안 되는 레지스터는?".
- **외울 것 ① 부트 체인**: **BootROM**(칩 안 ROM, boot mode 읽고 첫 이미지를 **on-chip SRAM**에 로드·인증) → **FSBL/SPL**(SRAM에서 실행, 클럭·핀·**DDR 초기화**) → (**ZynqMP: PMU FW**) → **TF-A BL31**(EL3 런타임, PSCI) → **U-Boot**(DDR에서) → **Linux** 또는 bare-metal 앱. DDR init이 FSBL/SPL에 있는 이유 = ROM은 보드마다 다른 DDR 파라미터를 모르고, DDR이 켜지기 전엔 **SRAM에 들어가는 작은 코드**만 돌 수 있어서 [3][9].
- **외울 것 ② ZynqMP 구체값**: PMU ROM → CSU BootROM → FSBL을 **OCM(256 KB, `0xFFFC0000`)**에 로드; FSBL은 대략 `0xFFFC0000–0xFFFE9FFF`, TF-A BL31 기본 `0xFFFEA000` (OCM 안), PMU FW는 PMU 전용 128 KB RAM [1][5][13]. `psu_init`이 클럭·MIO·DDR 설정 [3].
- **외울 것 ③ 테스트 FW 6층**: startup(벡터·스택·`.data` 복사·`.bss` 0) → 최소 polled UART → **test registry {name, fn, timeout}** → **기계 파싱용 결과 줄** (`@@TEST name=.. result=PASS ..` + `@@SUMMARY` + `@@END`) → UART 커맨드 셸 → JTAG/semihosting 대체 출력. 그 아래 **platform HAL**(FPGA/실리콘 차이: 클럭 비율·타임아웃·없는 블록).
- **외울 것 ④ 레지스터 테스트 제외 규칙**: RW 비트만 bit-bash, **RO/W1C/self-clearing/reserved/write-only는 마스크로 제외**, **읽기 부작용(FIFO pop·clear-on-read)·시스템을 죽이는 레지스터(클럭/리셋/PLL/DDR 컨트롤러/UART 핀먹스/보안)는 스윕에서 제외**. 그리고 **write-all-then-read-all** 패스로 주소 alias 잡기 (이 노트 C 실습에서 주입 버그 4종 모두 검출).
- **30초 영어 ("How would you structure a bring-up test firmware?")**: "It's layered so the same tests run on the FPGA prototype and on silicon. At the bottom, minimal startup code and a platform HAL — clocks, timer, UART, and a slowdown factor so every timeout scales with the platform. On top, a test registry where each test is a name, a function and a timeout, and a runner that prints one machine-parseable line per test, a summary, and an explicit end marker, so the automation can tell pass, fail, timeout and 'the board died mid-run' apart. A small UART shell lets me run single tests or peek and poke registers by hand. Hangs are bounded: every poll has a deadline, and the watchdog is the last resort, with the current test ID kept in no-init memory so the next boot reports which test hung and continues."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — 열쇠를 넘겨받으며 건물을 여는 순서

- 칩이 켜지면 바로 Linux가 뜨는 게 아니다. **작은 열쇠 → 큰 열쇠 → 더 큰 열쇠**로 단계적으로 문을 연다.
  - **BootROM = 경비실 아저씨.** 칩 공장에서 ROM에 박아 넣은 코드라 **절대 바뀌지 않는다**. 하는 일은 딱 하나: "오늘 어느 문(boot mode)으로 들어온 짐(이미지)을 받을까?" 보고, 짐이 진짜인지(서명) 확인하고, **작은 방(on-chip SRAM)**에 넣고 열쇠를 넘긴다.
  - **FSBL/SPL = 관리사무소장.** 작은 방에 들어갈 만큼 작다. 이 사람이 **창고(DDR)의 문을 여는 법**을 안다 — 창고는 건물마다(보드마다) 구조가 달라서 경비실 아저씨(ROM)는 모른다. 창고를 정리(DDR training)하고, 큰 짐들(U-Boot, OS)을 창고로 옮긴 뒤 열쇠를 넘긴다.
  - **TF-A(BL31) = 건물 보안팀.** 가장 높은 권한(EL3)으로 상주하며 전원 관리(PSCI: CPU 켜고 끄기) 같은 "금고급" 일을 대신해 준다.
  - **U-Boot = 이사업체 반장.** 네트워크·SD·eMMC에서 OS를 가져와 실행. 사람이 명령을 칠 수 있는 셸이 있다.
  - **Linux 또는 bare-metal 앱 = 입주자.**
- 왜 이렇게 나누나: ROM은 바꿀 수 없으니 **최소한**만, SRAM은 작으니 **DDR을 켜는 코드까지만**, 그다음은 DDR에서 무엇이든.

### 1.2 비유 — 공장 검사 기계 같은 테스트 펌웨어

- Bring-up 초기에는 OS가 없다(DDR도 아직 안 될 수 있다). 그래서 SRAM에서 도는 **작은 검사 프로그램**을 만든다.
  - **검사 항목표(test registry)**: 항목 이름, 검사 방법(함수), **제한 시간(timeout)**.
  - **판정 양식(result format)**: 사람이 보기 좋은 문장이 아니라 **기계가 읽는 양식** — 바코드처럼 `@@TEST name=reg_rw result=FAIL ...`. 보드 수십 장을 밤새 돌리면 사람이 로그를 읽을 수 없기 때문.
  - **조작 패널(UART shell)**: 사람이 "이 항목만 다시", "이 레지스터 값 보여줘"를 칠 수 있게.
  - **안전 장치**: 검사 하나가 멈추면(hang) 제한 시간 후 FAIL로 적고 다음 항목으로. 칩 전체가 멈추면 감시견(watchdog)이 리셋하고, 재부팅 후 "몇 번 항목에서 죽었음"을 보고 (→ [BV05](2026-11-01_BV05_reset_strap_boot_mode.html)).
  - **FPGA/실리콘 겸용**: FPGA 프로토타입은 실리콘보다 수십 배 느리다. 같은 검사를 돌리려면 **제한 시간을 플랫폼 속도에 맞춰 늘리는** 층(HAL)이 필요하다.

### 1.3 그림 — 일반 Arm SoC 부트 체인과 "어느 메모리에서 도나"

```text
 리셋 해제
    │
    ▼
 ┌──────────────┐  boot mode(strap/OTP) 읽기, 이미지 헤더·서명 검사
 │  BootROM     │  실행 위치: 칩 내부 ROM (수정 불가)
 │  (BL1 역할)  │  쓰는 메모리: on-chip SRAM 일부 (스택)
 └──────┬───────┘
        │ 첫 이미지를 on-chip SRAM으로 로드 → 점프
        ▼
 ┌──────────────┐  클럭/PLL, 핀먹스, 전원 도메인, UART(첫 로그!)
 │ FSBL / SPL   │  ★ DDR 컨트롤러·PHY 초기화 + training
 │ (BL2 역할)   │  실행 위치: on-chip SRAM (수백 KB 이하)
 └──────┬───────┘
        │ 다음 이미지들을 DDR(또는 SRAM)로 로드
        ▼
 ┌──────────────┐  EL3 상주 런타임: PSCI(CPU on/off), SMC 서비스
 │ TF-A BL31    │  실행 위치: SRAM 또는 보호된 DDR
 └──────┬───────┘
        ▼
 ┌──────────────┐  드라이버·셸·네트워크/스토리지 부팅
 │ U-Boot (BL33)│  실행 위치: DDR
 └──────┬───────┘
        ▼
   Linux kernel  /  bare-metal 테스트 앱 (DDR)

 Bring-up 초기 지름길:  JTAG로 테스트 FW를 SRAM에 직접 load → PC 설정 → run
                       (BootROM·flash 없이도 됨 — boot mode = JTAG)
```

### 1.4 그림 — Zynq UltraScale+ MPSoC 실제 부트 체인 [1][2][3][4][13]

```text
 PS_POR_B 해제
    │
    ├──▶ PMU (MicroBlaze 계열 관리 프로세서) ── PMU ROM 실행: 전원·리셋 pre-boot 작업
    │         │ CSU 리셋 해제
    │         ▼
    │    CSU BootROM ── boot mode 레지스터 읽기 → OCM 초기화 → boot header 읽기
    │         │          (보안 모드, FSBL을 A53 or R5 중 누가 돌릴지)
    │         │          이미지 깨졌으면 32 KB 단위로 다음 이미지 탐색 (golden image search)
    │         ▼
    │    FSBL → OCM 0xFFFC0000~ (256 KB OCM 중 약 0xFFFC0000–0xFFFE9FFF)
    │         │  psu_init: 클럭, MIO 핀, DDR 컨트롤러+PHY, PS-GTR(SerDes)
    │         │  PMU FW 로드 (BootROM이 안 했으면) → PMU 128 KB RAM
    │         │  PL bitstream (PCAP 경유)
    │         │  TF-A BL31 → OCM 0xFFFEA000 (기본값), U-Boot → DDR
    │         ▼
    │    TF-A BL31 (EL3) ──▶ U-Boot (EL2) ──▶ Linux
    │
    └──▶ PMU FW 상주: 전원 관리, 에러 처리, 리셋 요청 처리 (APU의 reboot도 PMU FW에 IPI로 요청)
```

- BOOT.BIN = FSBL + PMU FW + bitstream + TF-A + U-Boot 등을 **bootgen**이 하나로 묶은 이미지 (PetaLinux: `petalinux-package --boot --fsbl --fpga --u-boot`) — 상세는 [LP16 §2.5](2026-10-31_LP16_baremetal_vs_linux_zynq.html).

### 1.5 그림 — bring-up 테스트 FW의 층

```text
 ┌──────────────────────────────────────────────────────────────────────────┐
 │  자동화 (PC): 전원 ON → UART 열기 → "@@BEGIN" 기다림 → "@@END"까지 수집 → 파싱 → DB │
 └───────────────▲──────────────────────────────────────────────────────────┘
                 │ UART (또는 JTAG semihosting / 메모리 로그 덤프)
 ┌───────────────┴──────────────────────────────────────────────────────────┐
 │ 6 셸:      list / run <test|all> / rd <addr> / wr <addr> <val> / loop     │
 │ 5 러너:    순서대로 실행, 타임아웃, @@TEST/@@SUMMARY/@@END 출력, noinit 기록  │
 │ 4 레지스트리: { "reg_reset_values", fn, 100us }, { "dma_m2m", fn, 5ms } …   │
 │ 3 테스트:   레지스터 · 메모리 · IRQ · DMA · 타이머 · 블록별 directed test     │
 │ 2 HAL:     platform(fpga/silicon) · clock 비율 · delay/deadline · MMIO rd/wr │
 │ 1 startup: 벡터 테이블 · 스택 · .data 복사 · .bss 0 · (.noinit 건드리지 않음)   │
 └──────────────────────────────────────────────────────────────────────────┘
          ▲ 링커 스크립트: 초기 = SRAM만 / DDR training 후 = 큰 테스트는 DDR로
```

### 1.6 용어 사전

| 용어 | 풀네임 | 한 줄 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| BootROM | (부트 롬) | 칩 안에 박힌 첫 코드 — 수정 불가 | 첫 이미지 로드·인증·fallback |
| FSBL | First Stage Boot Loader | SRAM에서 도는 1단계 부트로더 (AMD/Xilinx 용어) | DDR·클럭·핀 초기화 |
| SPL / TPL | Secondary / Tertiary Program Loader | U-Boot의 작은 앞단 버전 (FSBL과 같은 역할) | "Sets up SDRAM and loads U-Boot proper" [9] |
| OCM | On-Chip Memory | 칩 안 SRAM (ZynqMP 256 KB) | DDR 전에 코드가 돌 수 있는 유일한 곳 |
| PMU / PMU FW | Platform Management Unit (firmware) | ZynqMP의 전원·리셋·에러 관리 전용 프로세서와 그 FW | 리셋·전원 요청이 여기를 거침 |
| CSU | Configuration Security Unit | ZynqMP의 부트·보안 블록 (BootROM이 여기서 실행) | 인증·복호화·multiboot |
| TF-A | Trusted Firmware-A | Arm 표준 보안 펌웨어 (BL1/BL2/BL31/BL32/BL33) | EL3 런타임, PSCI |
| BL31 | Boot Loader stage 3-1 | EL3에 상주하는 런타임 펌웨어 | CPU on/off, 보안 서비스 [8] |
| BL33 | (Non-trusted firmware) | 보통 U-Boot/UEFI | OS 로더 |
| EL0–EL3 | Exception Level | Arm 권한 단계 (EL3 = 가장 높음) | 누가 무엇을 만질 수 있나 |
| PSCI | Power State Coordination Interface | OS가 "CPU 켜 줘/꺼 줘"를 펌웨어에 요청하는 표준 | 멀티코어 bring-up |
| BOOT.BIN | (부트 이미지) | ZynqMP의 묶음 부트 이미지 | bootgen으로 생성 |
| Boot header | (부트 헤더) | 이미지 앞부분의 식별값·크기·보안 정보 | BootROM이 제일 먼저 확인 [7] |
| Startup code / crt0 | (시작 코드) | main 전에 스택·.data·.bss를 준비하는 코드 | 여기서 죽으면 UART도 없음 |
| `.data` / `.bss` | (초기값 있는 / 없는 전역 변수) | .data = flash에서 RAM으로 복사, .bss = 0으로 채움 | 링커 스크립트와 짝 |
| `.noinit` (NOLOAD) | (초기화 안 하는 영역) | startup이 건드리지 않아 리셋 후에도 값이 남는 RAM | 부트 카운터·현재 테스트 ID |
| Linker script | (링커 스크립트) | 코드/데이터를 어느 메모리 주소에 둘지 정하는 파일 | SRAM/DDR 배치 |
| HAL | Hardware Abstraction Layer | 플랫폼 차이를 숨기는 얇은 함수 층 | FPGA↔실리콘 재사용 |
| Test registry | (테스트 목록) | {이름, 함수, 타임아웃} 배열 또는 링커 섹션 | 자동화·셸이 이걸 본다 |
| Semihosting | (세미호스팅) | 타깃이 특수 명령(BKPT 등)으로 디버거에게 printf·파일 I/O를 부탁 | UART 없을 때 출력 [10] |
| Postcode / breadcrumb | (진행 표시) | 단계마다 고정 주소나 GPIO에 숫자를 남김 | UART 전 hang 위치 찾기 |
| Bit-bash | (비트 두드리기) | 레지스터 비트를 하나씩 1/0 써 보고 읽기 | stuck bit·연결 오류 |
| W1C / RC / SC | Write-1-Clear / Read-Clear / Self-Clearing | 쓰면 지워짐 / 읽으면 지워짐 / HW가 스스로 0으로 | 일반 RW 테스트에서 제외 |
| Alias | (주소 별칭) | 디코딩 버그로 두 주소가 같은 레지스터를 가리킴 | 개별 테스트로는 안 보임 |
| Clock ratio / slowdown | (클럭 비율) | FPGA가 실리콘보다 몇 배 느린가 | 타임아웃을 이것으로 스케일 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 Arm 부트 단계 — TF-A 기준 정의 [8]

| 단계 | 실행 위치 | EL | 하는 일 |
|---|---|---|---|
| BL1 (AP Trusted ROM) | Trusted ROM, RW 데이터는 trusted SRAM | EL3 | cold/warm boot 판별, 예외 벡터·리셋 핸들러, 최소 플랫폼 초기화(watchdog·콘솔·인터커넥트·저장장치), BL2 로드 |
| BL2 (Trusted Boot FW) | trusted SRAM | S-EL1 | 플랫폼 보안 설정, **BL31·BL32·BL33 로드** (DRAM init은 플랫폼 포트의 BL2 초기화에 들어가는 경우가 많음 [추정: TF-A 문서 본문은 플랫폼별로 남김]) |
| BL31 (EL3 Runtime) | trusted SRAM | EL3 | GIC·전원 컨트롤러 등 초기화, **PSCI** 등 런타임 서비스, BL33으로 handoff |
| BL32 (선택) | SRAM 또는 보호된 DRAM | S-EL1 | Trusted OS (OP-TEE 등) |
| BL33 (Non-trusted) | non-secure DRAM | EL2 (있으면) | U-Boot / UEFI |

- **벤더 이름과 매핑**: 많은 SoC가 BL1 = 칩 BootROM, BL2 = 벤더 FSBL 또는 U-Boot SPL. ZynqMP는 CSU BootROM ≈ BL1, **FSBL ≈ BL2 역할** (TF-A BL2는 안 씀), TF-A는 BL31만 사용 [추정: 일반적 Xilinx 흐름].
- **U-Boot 단계**: TPL("very early init, as tiny as possible") → SPL("Sets up SDRAM and loads U-Boot proper") → U-Boot proper(명령 셸이 있는 유일한 단계) [9]. 분리 이유 = "main U-Boot binary may be too large to be loaded directly by the Boot ROM" [9].

### 2.2 DDR은 어디서·왜 거기서 초기화되나

- **어디서**: FSBL/SPL(TF-A 용어로 BL2 단계). ZynqMP FSBL은 "processor initialization" 단계에서 `psu_init`(Vivado가 생성한 클럭·핀·DDR 설정)으로 DDR을 초기화하며, **master-only reset에서는 이 단계를 건너뛴다** [3].
- **왜 ROM이 아니라 FSBL인가**:
  1. DDR 설정(타이밍·임피던스·토폴로지·training 파라미터)은 **보드마다 다르다** — ROM은 칩 공장에서 고정.
  2. DDR training 코드·데이터가 크고, 버그 수정이 필요하다 (ROM은 못 고침).
  3. DDR이 켜지기 전에는 **on-chip SRAM만** 쓸 수 있으므로 init 코드는 SRAM에 들어갈 만큼 작아야 한다 — ZynqMP FSBL ≈ 170 KB, OCM 256 KB 안 [6].
- **왜 더 늦게(U-Boot) 하면 안 되나**: U-Boot proper·OS·대부분의 테스트는 DDR 크기의 메모리가 필요하다. 그래서 "SRAM에 들어가는 마지막 단계"가 DDR을 켠다.
- **Bring-up 함의**: DDR이 안 되면 FSBL에서 멈추고 U-Boot는 절대 안 뜬다 → **FSBL의 UART 로그와 DDR training 결과 출력**이 첫 증거 (→ [BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html)). SRAM 전용 테스트 FW는 DDR 없이도 다른 블록 bring-up을 계속하게 해 준다.

### 2.3 Startup 코드 — main 전에 무슨 일이 일어나나

```c
/* Cortex-M 스타일 최소 startup (개념 예시) [미검증: 타깃 빌드 안 함] */
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;   /* 링커 스크립트가 정의 */
int main(void);

__attribute__((noreturn)) void Reset_Handler(void)
{
    /* 0) 리셋 원인 레지스터를 여기서 가장 먼저 저장 (BV05) */
    for (uint32_t *s = &_sidata, *d = &_sdata; d < &_edata; ) *d++ = *s++;  /* .data 복사 */
    for (uint32_t *d = &_sbss; d < &_ebss; ) *d++ = 0;                      /* .bss 0 */
    /* .noinit 은 건드리지 않음 → 리셋 넘어 값 유지 */
    main();
    for (;;) { }
}
__attribute__((section(".isr_vector"), used))
const void *vectors[] = { &_estack, (void *)Reset_Handler /* , NMI, HardFault, ... */ };
```

- Cortex-A(AArch64)는 추가로: 현재 EL 확인, 보조 코어는 WFE로 대기(또는 PSCI로 나중에 깨움), 스택 설정, **MMU·캐시는 처음엔 끈 채로**(또는 identity map) 시작 → 캐시 켜면 DMA 테스트에서 **cache maintenance(clean/invalidate)** 필요.
- startup에서 죽는 흔한 이유: 스택 포인터가 존재하지 않는 메모리, `.data` 로드 주소(LMA)가 틀림, 벡터 테이블 주소(VTOR/VBAR) 설정 안 함, ECC SRAM을 초기화 안 하고 읽어서 ECC 에러 [추정: ECC SRAM을 가진 칩에서 흔한 bring-up 함정].

### 2.4 링커 스크립트 — SRAM/DDR 영역 배치

```text
/* 개념 예시 (GNU ld) [미검증: 타깃 빌드 안 함] */
MEMORY {
  SRAM (rwx) : ORIGIN = 0xFFFC0000, LENGTH = 192K   /* 예: OCM 일부 — BootROM/TF-A 영역 피해서 */
  DDR  (rwx) : ORIGIN = 0x00100000, LENGTH = 512M   /* training 이후에만 사용 */
}
SECTIONS {
  .text   : { KEEP(*(.vectors)) *(.text*) *(.rodata*) } > SRAM
  .testlist : { KEEP(*(.testlist)) } > SRAM          /* registry를 섹션으로 모을 때 */
  .data   : { *(.data*) } > SRAM
  .bss    (NOLOAD) : { *(.bss*) *(COMMON) } > SRAM
  .noinit (NOLOAD) : { *(.noinit*) } > SRAM          /* startup이 안 지움 */
  .ddr_tests : { *(.ddr_text*) } > DDR                /* DDR 살아난 뒤 로드하는 큰 테스트 */
  .stack  (NOLOAD) : { . = ALIGN(16); . += 8K; _estack = .; } > SRAM
}
```

- `NOLOAD` = "로드하지 않는 섹션 — 실행 시 메모리에 올리지 않음" (ELF에선 공간은 예약) [14]. → `.noinit`에 적합.
- **Registry를 링커 섹션으로**: 각 테스트 파일에 `static const struct test t __attribute__((section("testlist"), used)) = {...};` 를 두면 GNU ld가 C 식별자 이름의 섹션에 대해 `__start_testlist`/`__stop_testlist` 심볼을 자동 제공 → 러너가 그 사이를 순회 [15]. 장점 = 테스트 파일 추가만으로 등록. (이 노트 실습은 macOS 이식성 위해 정적 배열 사용.)
- **Bring-up 초기**: 테스트 FW 전체를 SRAM에 → JTAG `load` → run. DDR 이후: 큰 패턴 버퍼·DMA 대상 버퍼를 DDR로.

### 2.5 이미지 로딩 경로 (bring-up 단계별)

| 경로 | 언제 | 방법 | 주의 |
|---|---|---|---|
| JTAG 직접 로드 | Day 1 (flash 없음, boot mode = JTAG) | 디버거가 ELF를 SRAM에 쓰고 PC 설정 (xsdb `dow`, OpenOCD `load_image`, Trace32 `Data.LOAD.Elf`) | 클럭 init 전이면 TCK 낮게; 캐시/MMU 상태 |
| BootROM 다운로드 모드 | flash 비었거나 깨짐 | USB/UART로 이미지 전송 (칩별 툴) | boot mode strap 바꿔야 |
| U-Boot 경유 | U-Boot가 뜬 뒤 | `loady`(UART ymodem), `tftpboot`, `fatload` → `go <addr>` | DDR이 살아 있어야 |
| Boot 장치 | 정상 부팅 | QSPI/eMMC/SD의 BOOT.BIN | 헤더·서명·오프셋 |
| Semihosting 파일 I/O | 디버거 연결 상태 | 타깃이 디버거 호스트의 파일을 읽음 | 매우 느림 |

### 2.6 Bring-up 테스트 FW 구조 — 정밀 버전

- **최소 UART**: polled TX(`while (TX FIFO full) ; write THR`), 인터럽트·DMA 없음, 포맷은 작은 자체 `printf`(정수·hex·문자열만). **첫 글자 출력 전에 baud 계산에 쓰는 클럭이 맞는지가 관건** → UART 깨지면 클럭 의심 (→ [BV04](2026-11-01_BV04_clocks_osc_pll_refclk.html)).
- **UART 전 진행 표시**: (a) GPIO LED 토글 (scope로 보임), (b) 고정 SRAM 주소에 postcode(`0xB0070001`, `…02`…) → JTAG로 읽기, (c) semihosting.
- **Semihosting** [10][11]: 트랩 명령 — **M-profile `BKPT #0xAB`**, **A64 `HLT #0xF000`**, A32/T32는 `SVC #0x123456`/`SVC #0xAB`(레거시) 또는 `HLT`. 연산 번호: `SYS_WRITE0`=0x04(0으로 끝나는 문자열 출력), `SYS_EXIT`=0x18. **디버거가 없으면 Cortex-M에서 BKPT는 HardFault** → 디버거 미연결 시 비활성화하는 스위치 필요 [11]. 호출마다 코어가 멈추므로 **타이밍 테스트 중엔 사용 금지**.
- **Registry 엔트리 권장 필드**: `name`, `fn`, `timeout_us`(실리콘 기준), `flags`(destructive / needs_ddr / fpga_only / silicon_only / long), `requires`(의존 블록), `owner`.
- **러너 규칙**:
  1. 각 테스트 전: 블록을 known state로 (soft reset), noinit에 `current_test_id` 기록.
  2. 실행: 모든 폴링은 **HAL deadline** 기반 — `while(!(reg & DONE));` 금지.
  3. 결과: `PASS / FAIL / TIMEOUT / SKIP` (+ `HANG` = 다음 부팅에서 WDT 원인으로 판정).
  4. 출력: **한 테스트 = 한 줄** key=value + 끝에 `@@SUMMARY`, `@@END`.
  5. 실패해도 계속(다음 테스트), 단 `destructive` 실패 후엔 리셋.
- **결과 줄 설계 원칙**: 고정 접두(`@@`)로 잡음(부트 배너·디버그 출력)과 분리, `key=value`(순서 무관·필드 추가해도 파서 안 깨짐), 따옴표로 detail, **BEGIN에 메타데이터**(FW 빌드 해시, 플랫폼, 칩 ID/stepping, 보드 SN, seed), **END 마커로 완료 판정**(없으면 hang/reboot = INCOMPLETE).
- **UART 커맨드 셸**: `list`, `run <name|all>`, `rd/wr <addr>`, `loop <name> <n>`(간헐 실패 재현), `seed <n>`(랜덤 테스트 재현), `info`(칩 ID·클럭). 셸이 있으면 자동화와 사람이 **같은 바이너리**를 쓴다.
- **Hang 처리 3단**: SW deadline(대부분) → 블록 soft reset 후 다음 테스트 → 그래도 CPU가 멈추면 WDT 리셋 → 다음 부팅에서 reset reason=WDT + noinit `current_test_id` → `@@TEST name=<그 테스트> result=HANG` 보고 후 그 다음 테스트부터 재개 (→ [BV05 §2.7–2.8](2026-11-01_BV05_reset_strap_boot_mode.html)).

### 2.7 테스트 종류 — 무엇을, 어떻게, 무엇을 제외

| 종류 | 방법 | 잡는 버그 | 주의/제외 |
|---|---|---|---|
| 레지스터 reset 값 | 리셋 직후 읽어서 `(read & reset_mask) == (reset & reset_mask)` | RTL 기본값 오류, 리셋 안 걸린 블록, 잘못된 tie-off | 휘발성 상태 비트(BUSY 등)는 mask에서 제외, 읽기 부작용 레지스터는 아예 제외 |
| 레지스터 RW (bit-bash) | RW 비트마다 walking-1 / walking-0 쓰고 읽기 | stuck-at, 비트 순서 뒤바뀜, 쓰기 마스크 오류 | **RW 비트만**: RO·W1C·W1S·SC·WO·reserved 제외, 원래 값 복원 |
| 주소 디코드/alias | 모든 레지스터에 고유 값 쓰고 → 전부 다시 읽기 | 주소 비트 미디코딩(alias), 잘못된 base | 개별 RW 테스트로는 **안 보임** |
| RO 불변 | RW 쓰기 후 RO 필드가 그대로인지 | RO가 실제로 쓰여짐 | |
| W1C 의미 | 이벤트 발생 → 0 쓰기(안 지워져야) → 1 쓰기(지워져야) | W1C가 RW로 구현됨, 이벤트 비트 미동작 | |
| 인터럽트 | 소스 트리거 → pending 비트 → (GIC/NVIC) ISR 진입 횟수 → clear → deassert | 라우팅·극성·엣지/레벨, 클리어 안 됨, 인터럽트 폭주 | ISR 카운터, 타임아웃 |
| DMA | src 패턴 → dst 비교, 길이 0/1/최대, 비정렬, 경계(4 KB 등) 넘기, 동시 채널 | 주소/길이 버그, 버스트 경계, **캐시 일관성** | 캐시 on이면 clean(전)·invalidate(후) |
| 메모리 | data bus · address bus · March C- | 단락·단선·셀 결함 | → [BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html) |
| 타이머/클럭 | 타이머 A로 잰 시간을 독립 기준(다른 클럭 도메인 타이머, 외부 GPIO 토글을 scope로)과 비교 | 분주비·PLL 설정 오류 | FPGA에선 클럭 비율 반영 |
| 에러 주입 | ECC/parity 에러 주입 레지스터, 잘못된 주소 접근(bus error) | 에러 보고·인터럽트·카운터 경로 | 안전 기구 검증 (→ BV15) |

- **스윕에서 절대 쓰지 말 것**: 클럭/리셋 컨트롤러·PLL(자기 발등), DDR 컨트롤러(코드가 DDR에 있으면 즉사), **UART 핀먹스/UART 자체**(출력이 끊김), 보안·퓨즈·lock 레지스터(되돌릴 수 없음), 인터럽트 컨트롤러 전역 enable, watchdog. → **register description(IP-XACT/SystemRDL 등)에서 테스트를 자동 생성**하되 access type·"no test" 속성으로 제외 — DV의 UVM RAL 내장 시퀀스(`uvm_reg_hw_reset_seq`, `uvm_reg_bit_bash_seq`)가 `NO_REG_HW_RESET_TEST`/`NO_REG_BIT_BASH_TEST`/`NO_REG_TESTS` 속성으로 제외하는 것과 같은 아이디어 [12].

### 2.8 FPGA ↔ 실리콘 재사용

| 차이 | FPGA 프로토타입 (HAPS/VCU) | 실리콘 | 테스트 FW 대응 |
|---|---|---|---|
| 속도 | 수~수십 MHz (실리콘의 1/10–1/100) [추정: 설계 크기별] | GHz | HAL `slowdown` 계수로 **모든 타임아웃·지연 스케일** |
| 클럭 비율 | 블록 간 비율이 실리콘과 다를 수 있음 | 설계 비율 | 타이머·baud 계산을 HAL에서 |
| 아날로그 PHY | 없음 / 다른 PHY(FPGA GT) / 디지털 루프백 | 실제 PHY | 테스트 `flags`: silicon_only / fpga_only |
| DDR | FPGA 보드 메모리 (다른 컨트롤러/PHY일 수 있음) | 실제 LPDDR | DDR init을 HAL 뒤로 숨김 |
| 없는 블록 | 일부 IP 스텁 | 전부 | registry `requires` + 런타임 SKIP |
| 디버그 | ILA 등 FPGA 내부 관측 | JTAG/trace만 | 결과 포맷은 동일하게 유지 |

- **런타임 플랫폼 감지**: 칩 ID/버전 레지스터(실리콘 stepping) 또는 FPGA 빌드 ID 레지스터를 읽어 `platform` 구조체 선택 → 같은 바이너리.
- **이 노트 실습 결과**: 타임아웃을 스케일하지 않으면 **정상 IP가 FPGA에서 TIMEOUT** (6.1의 3번 실행) — "FPGA에서만 실패하는 가짜 버그"의 전형.

### 2.9 자동화와 연결 (→ BV14)

```text
 board farm PC                                       타깃
 ─────────────                                       ────
 power_cycle(board)      ───────── 릴레이/PDU ───▶  POR
 open UART, start timer
 wait "@@BEGIN" (timeout 30 s) ◀──── UART ───────  부트 배너 … @@BEGIN plat=… build=…
 collect until "@@END" (timeout = Σ test timeouts × margin)
                               ◀────────────────  @@TEST … / # 사람용 디버그 줄
 parse → verdict PASS/FAIL/INCOMPLETE              @@SUMMARY … @@END
 store (DB/JUnit), attach raw log, on FAIL: JTAG dump script
```

- **INCOMPLETE를 FAIL과 구분**: END가 안 왔다 = 보드가 죽음/재부팅 → reset reason과 마지막 TEST 줄로 "어느 테스트에서 죽었나"를 자동 판정 (이 노트 `parse_results.py`가 구현).

## 3. 보드에서 볼 것 · 잴 것

### 3.1 "어디까지 부팅됐나" — 단계별 증거

| 단계 | 정상 증거 | 없으면 볼 것 |
|---|---|---|
| 리셋 해제 | POR_B High, JTAG IDCODE 읽힘 | → [BV05](2026-11-01_BV05_reset_strap_boot_mode.html), [BV07](2026-11-01_BV07_jtag_swd_boundary_scan.html) |
| BootROM이 boot 장치 읽음 | scope: QSPI CS#/CLK 토글, SD CLK | boot mode 레지스터, flash 전원·핀, 헤더 |
| FSBL 시작 | UART 첫 배너 / postcode | 이미지 헤더·서명, BootROM 에러 코드 레지스터(칩별 TRM) |
| 클럭·핀 설정 | UART 글자 정상 | 깨지면 클럭/baud (BV04) |
| DDR init | "DDR training PASS" 류 로그, 작은 memtest | training 단계별 결과 (BV09) |
| 다음 단계 로드 | TF-A `NOTICE: BL31:` 배너, U-Boot 배너 | 로드 주소·DDR·이미지 파티션 |
| OS / 테스트 앱 | Linux 로그 / `@@BEGIN` | DT, 콘솔 설정 |

### 3.2 UART가 뜨기 전 hang 디버그

1. **JTAG로 halt → PC 읽기** → ELF로 심볼 확인 (`addr2line -e fsbl.elf 0x…`). PC가 BootROM 주소 범위면 BootROM 단계에서 멈춤(이미지/boot mode 문제), FSBL 범위면 FSBL 코드.
2. **예외 레지스터**: AArch64 `ESR_ELx`·`ELR_ELx`·`FAR_ELx`(무슨 예외·어디서·어느 주소), Cortex-M `CFSR`·`HFSR`·`MMFAR`·`BFAR`. 예외 벡터에 무한 루프가 있으면 PC가 거기서 돈다.
3. **Postcode 메모리 읽기**: 고정 주소의 진행 코드 → 마지막으로 통과한 단계.
4. **Vector catch / connect under reset**: 리셋 직후 첫 명령에서 멈춰서 한 단계씩 (→ BV07).
5. **신호로 보기**: GPIO LED 토글, flash 버스 활동, PLL clock-out 핀.
6. **한 가지씩 줄이기**: SRAM에서 `while(1) toggle_gpio();` 같은 최소 코드가 도나? → 돈다면 하드웨어 경로 OK, 문제는 init 코드.

### 3.3 부트 로그 해석 `[예시 출력]`

```text
[예시 출력]  ZynqMP: FSBL → TF-A → U-Boot (형식만 참고; 버전·문구는 릴리스마다 다름)
Xilinx Zynq MP First Stage Boot Loader
Release 2023.2   ...
NOTICE:  BL31: v2.8(release):xlnx_rebase_v2.8
NOTICE:  BL31: Built : ...
U-Boot 2023.01 (...)
CPU:   ZynqMP
Model: ZynqMP ZCU102 RevB
DRAM:  4 GiB
Bootmode: QSPI_MODE
Reset reason:	EXTERNAL
```

- 1–2행: FSBL 실행 = BootROM이 이미지를 찾고 인증·로드 성공 + UART 클럭·핀 OK.
- 3–4행: TF-A BL31 배너 = FSBL이 DDR/OCM에 다음 단계 로드·handoff 성공.
- 5–8행: U-Boot가 DDR에서 실행 중 = **DDR 초기화가 최소한 동작**. `DRAM: 4 GiB` = 인식된 크기 (기대값과 비교).
- 9–10행: boot mode·reset reason (→ BV05).
- 멈춘 위치가 곧 진단: FSBL 배너 뒤 침묵 = DDR/PMU FW/다음 이미지 로드 의심 → FSBL 디버그 출력 켜고 재빌드.

### 3.4 테스트 FW 로그 해석 — 이 노트 실습의 **실제 출력**

```text
@@BEGIN plat=silicon_a0 slowdown=1 scale_timeouts=1 bugs=0xF ntests=5
#   IRQ_EN@0x10 reset got 0x00000001 exp 0x00000000 (mask 0xFFFFFFFF)
@@TEST id=0 name=reg_reset_values result=FAIL dur_us=0 errors=1 detail="IRQ_EN@0x10 reset got 0x00000001 exp 0x00000000 (mask 0xFFFFFFFF)"
#   CFG@0x0C RW bits wrote1-read0=0x00002000 wrote0-read1=0x00000000
@@TEST id=1 name=reg_rw_walk result=FAIL dur_us=0 errors=1 detail="CFG@0x0C RW bits wrote1-read0=0x00002000 wrote0-read1=0x00000000"
#   DATA@0x14 got 0xA5001C1C exp 0xA5001414 (aliased by a later write?)
@@TEST id=2 name=reg_alias_unique result=FAIL dur_us=0 errors=1 detail="DATA@0x14 got 0xA5001C1C exp 0xA5001414 (aliased by a later write?)"
#   STATUS.DONE not set: budget 200 us x scale 1, BUSY=1
@@TEST id=3 name=irq_w1c_done result=TIMEOUT dur_us=200 errors=1 detail="STATUS.DONE not set: budget 200 us x scale 1, BUSY=1"
@@TEST id=4 name=no_side_effect_reads result=PASS dur_us=0 errors=0 detail=""
@@SUMMARY total=5 pass=1 fail=3 timeout=1 skip=0 verdict=FAIL
@@END
```

- `@@BEGIN`: 메타데이터 — 플랫폼·스케일 여부·(실습용) 주입 버그 마스크. 실제 FW라면 빌드 해시·칩 stepping·보드 SN.
- `#` 줄: 사람용 상세 (파서는 무시).
- id=0: `IRQ_EN` 리셋 값이 0x1 — **RTL 기본값 버그** 또는 리셋 미적용. → 다른 리셋 종류(POR vs soft reset)에서도 같은지 확인 → 에라타 후보.
- id=1: `CFG` bit 13 = 1을 써도 0 → **stuck-at-0**. 다른 패턴은 정상 → 특정 비트 문제 (RTL tie-off / 레지스터 맵 문서 불일치).
- id=2: `DATA`(0x14)를 읽었더니 `SCRATCH`(0x1C)에 쓴 값 → **주소 bit 3 미디코딩 alias**. id=1(개별 RW 테스트)은 이걸 **못 잡았다** — 개별 테스트에선 쓰고 바로 읽으니까.
- id=3: BUSY=1인 채 DONE 안 옴 → 블록이 동작을 끝내지 못함 (클럭 게이팅? 리셋? 진짜 RTL 버그?) → 러너는 블록 soft reset 후 **다음 테스트로 진행**.
- id=4: 스윕 테스트가 FIFO 레지스터를 읽지 않았음 확인 (읽기 부작용 제외 규칙이 지켜짐).
- `@@SUMMARY` + `@@END`: 완주 → 자동화는 FAIL(INCOMPLETE 아님) 판정.

### 3.5 실리콘 도착 전 테스트 FW 준비 체크리스트

| # | 항목 | 통과 기준 |
|---|---|---|
| 1 | FPGA에서 전체 스위트 PASS (같은 소스) | N회 연속 PASS, 결과 줄 포맷 고정 |
| 2 | 모든 타임아웃이 HAL deadline 사용 | grep으로 무한 폴링 루프 0개 |
| 3 | 플랫폼 감지 + slowdown 테이블 | FPGA/실리콘 ID 각각 인식 |
| 4 | JTAG 로드 스크립트 (SRAM 실행) | 보드 없이 FPGA로 리허설 |
| 5 | UART 없는 경우 대체 출력 (postcode/semihosting/메모리 로그) | 스위치로 선택 |
| 6 | WDT + noinit current_test → 재부팅 후 HANG 보고 | FPGA에서 강제 hang으로 검증 |
| 7 | 레지스터 제외 목록 (클럭/리셋/DDRC/UART/보안) | 리뷰 완료 |
| 8 | 자동화 파서 + INCOMPLETE 판정 | 잘린 로그로 테스트 |
| 9 | 최소 "hello" 바이너리 (GPIO 토글 + UART 1글자) | 가장 먼저 쓸 것 |

## 4. 꼬리 질문 체인

### 4.1 BV00 체인 — "How would you structure a bring-up test firmware?"

<details><summary>답 펼치기</summary>

- **Q. How would you structure a bring-up test firmware?** — "In layers. Startup code and a platform HAL at the bottom — clocks, a timer, a polled UART, MMIO accessors, and a platform descriptor with the slowdown factor. Above that a test registry: each entry has a name, a function, a timeout and flags like 'needs DDR' or 'silicon only'. A runner executes them, prints one machine-parseable line per test and a summary with an end marker, and a small UART shell lets me run a single test or read and write registers by hand. It runs from on-chip SRAM so it doesn't depend on DDR." · 한국어: startup+HAL → registry{name,fn,timeout,flags} → 러너(한 줄 결과+END) → 셸, SRAM에서 실행.
- **꼬리1. How does the automation know a test passed?** — "From a strict, line-oriented format: a fixed prefix so it can't be confused with debug prints, key-value fields, a summary line whose counts must match the test lines it saw, and an explicit end marker. If the end marker never arrives within the overall timeout, the run is 'incomplete', not 'fail' — the board hung or rebooted, and the last test line plus the reset reason tell me where." · 한국어: 고정 접두 key=value + SUMMARY 개수 대조 + END 마커, END 없으면 INCOMPLETE.
- **꼬리2. How do you keep it running on FPGA and silicon?** — "Everything platform-specific goes behind the HAL: clock frequencies, the slowdown factor that scales every timeout, which blocks exist, and how DDR is brought up. The platform is detected at runtime from an ID register, so it's the same source and ideally the same binary. Tests that only make sense on one side are flagged and skipped, not deleted." · 한국어: HAL(클럭·slowdown·블록 유무·DDR) + 런타임 ID 감지 + 플래그로 SKIP.
- **꼬리3. What do you do when a test hangs the system?** — "First, it shouldn't: every poll has a deadline, so a stuck block produces a TIMEOUT and the runner soft-resets that block and moves on. If the CPU itself is stuck, the watchdog resets the chip; before each test I wrote the test ID into no-init memory, so on the next boot the firmware sees reset-reason watchdog, reports that test as HANG and resumes with the next one. Meanwhile, for root cause, I attach JTAG, halt, and look at the PC and the exception syndrome registers." · 한국어: deadline → TIMEOUT+soft reset, CPU hang → WDT + noinit test ID → HANG 보고 후 재개, 원인은 JTAG PC/ESR.

</details>

### 4.2 BV00 체인 — "Walk me through the boot flow of an Arm SoC."

<details><summary>답 펼치기</summary>

- **Q. Walk me through the boot flow of an Arm SoC.** — "After reset, the boot ROM runs from on-chip ROM. It reads the boot-mode straps or fuses, finds the first image on the boot device, checks its header and signature, and loads it into on-chip SRAM. That first-stage loader — FSBL or U-Boot SPL — sets up clocks, pin muxing, the UART, and crucially the DDR controller and PHY including training. It then loads the next stages into DDR: Trusted Firmware-A's BL31 as the EL3 runtime for PSCI and secure services, and U-Boot as BL33, which loads Linux. On Zynq UltraScale+ there's also the PMU and its firmware handling power and resets." · 한국어: ROM(모드·헤더·서명→SRAM) → FSBL/SPL(클럭·핀·UART·DDR) → BL31(EL3) → U-Boot → Linux, ZynqMP는 PMU FW 추가.
- **꼬리1. Where does DDR get initialized and why there?** — "In the first-stage loader. The boot ROM can't do it because DDR parameters depend on the board — the memory part, the topology, the training settings — and ROM code can't be patched. And it can't wait until U-Boot, because U-Boot and everything after it need DDR to run. So the last stage that still fits in on-chip SRAM is the one that brings DDR up." · 한국어: ROM은 보드별 파라미터를 모름·수정 불가, U-Boot는 DDR이 필요 → SRAM에 들어가는 마지막 단계.
- **꼬리2. How do you debug a hang before the UART is up?** — "With JTAG: halt and read the PC, map it to a symbol, and read the exception registers — on AArch64 ESR, ELR and FAR. I add breadcrumbs: a post-code written to a fixed SRAM address or a GPIO toggle at each init step, so the last value tells me how far it got. If the PC is still in the boot ROM range, it's a boot-mode or image problem, and I'd scope the flash chip-select to see if the ROM even reads it." · 한국어: JTAG halt → PC/ESR/ELR/FAR, postcode·GPIO breadcrumb, PC가 ROM 범위면 boot mode/이미지 → flash CS scope.
- **꼬리3. What's BL31 for, and why does it stay resident?** — "It's the EL3 runtime from Trusted Firmware-A. It stays because the OS needs services that only the most privileged level can provide — PSCI to power cores on and off, system reset and suspend, and secure-world dispatch. Linux calls into it through SMC instructions." · 한국어: EL3 상주 — PSCI(CPU on/off·reset·suspend)·보안 서비스, SMC로 호출.

</details>

### 4.3 BV00 체인 — "Write a register reset-value / read-write test."

<details><summary>답 펼치기</summary>

- **Q. Write a register reset-value and read-write test.** — "I'd drive it from a register description table: offset, reset value, a mask of bits that have a defined reset value, and access masks for RW, RO, write-1-to-clear, self-clearing, and reserved. The reset test reads each register right after reset and compares under the reset mask. The RW test walks a one and a zero across the RW bits only, saves and restores the original value, and checks that RO bits didn't move. Then a separate pass writes a unique value to every register and reads them all back, which catches address aliasing that a per-register test can't see." · 한국어: 레지스터 설명 표 기반 — reset 비교(마스크), RW 비트만 walking 1/0 + 복원 + RO 불변, 그다음 write-all/read-all로 alias.
- **꼬리1. What registers must you NOT write — or not even read?** — "Don't write W1C bits with ones unless you mean to clear an event, don't rely on self-clearing bits reading back, and don't touch reserved bits. Don't read registers with read side effects — FIFO pops, clear-on-read status. And keep anything that can kill the test itself out of the sweep: clock and reset controllers, PLLs, the DDR controller if code runs from DDR, the UART and its pin mux, security and fuse or lock registers, and the watchdog." · 한국어: W1C·SC·reserved 쓰기 금지, 읽기 부작용 금지, 클럭/리셋/PLL/DDRC/UART·핀먹스/보안·퓨즈/WDT 제외.
- **꼬리2. Your RW test passes but the block still misbehaves. What could the test have missed?** — "Aliasing — two addresses decoding to the same flop — which I catch with write-all-then-read-all. Also side effects: the register stores the value but the hardware doesn't act on it, so I need a functional test that uses the setting. And bits that only matter in combination or only after a reset of a different type." · 한국어: alias, 값은 저장되지만 기능에 반영 안 됨(기능 테스트 필요), 조합/리셋 종류 의존.
- **꼬리3. Where does the register description come from?** — "Ideally generated from the same source the RTL team uses — IP-XACT or SystemRDL — so the tests and the headers can't drift from the design. It's the same idea as the UVM register model's built-in reset and bit-bash sequences in DV, with 'no test' attributes for the registers to exclude." · 한국어: RTL과 같은 원천(IP-XACT/SystemRDL)에서 생성 — UVM RAL 내장 시퀀스와 같은 발상.

</details>

### 4.4 추가 체인 — "It passes on the FPGA but fails on silicon. How do you use the test firmware to triage?"

<details><summary>답 펼치기</summary>

- **Q. A test passes on FPGA but fails on A0 silicon. How do you triage?** — "Same source, so first I rule out the platform layer: is the timeout scaled correctly, is the clock configuration what the HAL thinks it is, does the block even exist in the same form — FPGA often has a different PHY or a digital loopback. Then I run the failing test alone from the shell in a loop to see if it's deterministic, dump the block's registers before and after, and compare with the FPGA dump. If it's deterministic and the configuration matches, it's a candidate silicon or integration issue and I reduce it to a minimal register sequence for the design team." · 한국어: 플랫폼층(타임아웃 스케일·클럭·PHY 차이) 먼저 → 셸에서 단독 loop → 레지스터 덤프 FPGA와 diff → 최소 재현 시퀀스.
- **꼬리1. It only fails sometimes.** — "Then I loop it thousands of times with timestamps and the seed logged, vary one thing at a time — clock frequency, voltage, temperature — and see which way the failure rate moves. A rate that changes with voltage or temperature points at timing margin; a rate that doesn't points at a logic race or a software ordering issue, like a missing barrier." · 한국어: 수천 회 loop + seed, 전압·온도·클럭 하나씩 → 마진 vs 로직 레이스/배리어 누락.

</details>

### 4.5 추가 체인 — "How would you test interrupts and DMA during bring-up?"

<details><summary>답 펼치기</summary>

- **Q. How would you test an interrupt path?** — "Trigger the source in a controlled way — a timer, a software-set bit, or the block's own test trigger — then check each hop: the block's status bit, the interrupt controller's pending bit, that the ISR ran exactly once by counting, that clearing the source with the right semantics — usually write-one-to-clear — drops the line, and that it doesn't fire again. Also masking: with the enable off it must not fire." · 한국어: 소스 트리거 → 상태 비트 → 컨트롤러 pending → ISR 1회(카운터) → W1C 클리어 → deassert, 마스크 시 미발생.
- **꼬리1. And DMA?** — "Memory-to-memory first with a known pattern, comparing source and destination, then the edges: length zero, one, maximum, unaligned addresses, crossing page or burst boundaries, several channels at once. With caches on I clean the source before and invalidate the destination after, otherwise I'm testing my cache handling rather than the DMA. Then error cases — bad address — must raise an error status, not hang." · 한국어: m2m 패턴 비교 → 경계(0·1·최대·비정렬·경계 넘기·동시 채널) → 캐시 clean/invalidate → 에러 경로.

</details>

### 4.6 추가 체인 — "Why not just bring up Linux and test from there?"

<details><summary>답 펼치기</summary>

- **Q. Why write bare-metal test firmware at all instead of booting Linux?** — "Because on day one Linux can't run — DDR may not be trained yet, and Linux needs working clocks, interrupts, timers and a device tree. Bare-metal tests run from SRAM, touch registers directly, are deterministic, and when they fail the suspect list is short. They also run on the FPGA years before silicon. Once the platform is stable, Linux is where I do system integration, stress and performance — so both, in that order." · 한국어: Day 1엔 Linux 불가(DDR·클럭·IRQ·DT 필요), bare-metal = SRAM·직접·결정적·FPGA에서도 → 이후 Linux로 통합·스트레스 (LP16과 같은 논리).

</details>

## 5. Don 경험 연결

- "At Solidigm I wrote bare-metal C validation firmware for our SSD controller SoC before RTL freeze — bringing up Cortex-R8, R82 and M0+ on the FPGA prototype and verifying I2C, SPI, DMA, SRAM and DRAM paths — and the same tests carried over to silicon with the silicon validation team. That's exactly the FPGA-to-silicon reuse I'd build in from day one here. [숫자 채우기: 테스트 수 / 블록 수]"
- "The thing I learned the hard way is timeouts: on the FPGA everything is many times slower, so a hard-coded timeout produces false failures — I parameterize every deadline by the platform. [숫자 채우기: FPGA 클럭 vs 실리콘 클럭]"
- "At SK hynix I built the automation side of a reliability test platform — the host scripts that ran devices in chambers for long periods and parsed results — so I design the firmware's output format with the parser in mind: strict lines, an end marker, and 'incomplete' as its own verdict. [숫자 채우기: 동시 디바이스 수 / 런 시간]"
- "I debug pre-UART hangs with Trace32 — halt, PC, exception registers, and breadcrumbs in SRAM."

## 6. 실습 / 코드

### 6.1 `onsitePrep/bv08/bringup_fw_sim.c` — 검증됨

- 무엇: 호스트에서 도는 **미니 bring-up 테스트 FW**: platform HAL(slowdown 계수, deadline), test registry, `@@` 결과 줄, 레지스터 3종 테스트 + W1C/IRQ 테스트 + 읽기 부작용 확인, UART 셸(스크립트 RX), 그리고 **mock 레지스터 블록**(RO / W1C / self-clearing / reserved / 읽기 부작용 FIFO 의미를 그대로 구현) + **주입 버그 4종**(CFG bit13 stuck-at-0, IRQ_EN reset 값 오류, 주소 bit 3 alias, 동작 완료 안 됨).
- 빌드·실행 (macOS) [확인됨]:

```bash
cc -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror \
   -fsanitize=address,undefined onsitePrep/bv08/bringup_fw_sim.c -o /tmp/bv08_fw && /tmp/bv08_fw > onsitePrep/bv08/bringup_fw_sim.out
python3 -I onsitePrep/bv08/parse_results.py onsitePrep/bv08/bringup_fw_sim.out ; echo "exit=$?"
```

- 5개 실행 시나리오와 결과 (실제 출력 요약):

| # | 시나리오 | 결과 | 의미 |
|---|---|---|---|
| 1 | 정상 IP, 실리콘 | 5/5 PASS | 기준선 |
| 2 | 정상 IP, FPGA(40× 느림), 타임아웃 스케일 ON | 5/5 PASS (irq 테스트 dur 2000 us) | HAL 스케일이 동작 |
| 3 | 정상 IP, FPGA, 타임아웃 스케일 OFF | irq_w1c_done **TIMEOUT** | **가짜 실패** — 흔한 실수 재현 |
| 4 | 버그 4종 주입, 실리콘 | reset/RW/alias **FAIL**, irq **TIMEOUT**, side-effect PASS | 각 테스트가 각 버그를 정확히 하나씩 잡음 (3.4 해석) |
| 5 | UART 셸 세션 (버그 칩) | 아래 | 사람이 손으로 재현 |

- **실제 출력 — 3번 (스케일 안 한 FPGA)**:

```text
===== 3) clean IP on FPGA, timeouts NOT scaled (common mistake) =====
@@BEGIN plat=fpga_haps slowdown=40 scale_timeouts=0 bugs=0x0 ntests=5
@@TEST id=0 name=reg_reset_values result=PASS dur_us=0 errors=0 detail=""
@@TEST id=1 name=reg_rw_walk result=PASS dur_us=0 errors=0 detail=""
@@TEST id=2 name=reg_alias_unique result=PASS dur_us=0 errors=0 detail=""
#   STATUS.DONE not set: budget 200 us x scale 1, BUSY=1
@@TEST id=3 name=irq_w1c_done result=TIMEOUT dur_us=200 errors=1 detail="STATUS.DONE not set: budget 200 us x scale 1, BUSY=1"
@@TEST id=4 name=no_side_effect_reads result=PASS dur_us=0 errors=0 detail=""
@@SUMMARY total=5 pass=4 fail=0 timeout=1 skip=0 verdict=FAIL
@@END
```

- 해석: 같은 IP가 2번(스케일 ON)에선 2000 us 걸려 PASS. 실리콘 기준 200 us 예산을 FPGA에 그대로 쓰면 BUSY=1인 채 시간 초과 → **IP 버그가 아니라 테스트 인프라 버그**. detail에 `x scale 1`을 찍어 둔 덕분에 로그만 보고 판별 가능.

- **실제 출력 — 5번 (UART 셸)**:

```text
bv> wr 0x0c 0x2000
  [0x0C] <= 0x00002000 ; readback 0x00000000
bv> wr 0x14 0x11111111
  [0x14] <= 0x11111111 ; readback 0x11111111
bv> wr 0x1c 0x22222222
  [0x1C] <= 0x22222222 ; readback 0x22222222
bv> rd 0x14
  [0x14] = 0x22222222
bv> run reg_reset_values
#   IRQ_EN@0x10 reset got 0x00000001 exp 0x00000000 (mask 0xFFFFFFFF)
@@TEST id=0 name=reg_reset_values result=FAIL dur_us=0 errors=1 detail="IRQ_EN@0x10 reset got 0x00000001 exp 0x00000000 (mask 0xFFFFFFFF)"
bv> run foo
  ERR unknown test 'foo'
```

- 해석: `CFG` bit 13 쓰기 후 0 = stuck-at-0 손으로 재확인. `0x14`에 쓰고 `0x1C`에 쓴 뒤 `0x14`를 읽으니 `0x22222222` = **alias를 손으로 증명** — 설계팀에 보낼 최소 재현 시퀀스가 바로 이 3줄. 셸에서 실행한 단일 테스트도 같은 `@@TEST` 형식으로 출력된다.

- **실제 출력 — 파서 (`parse_results.py`)**:

```text
run 0: plat=silicon_a0 bugs=0x0 tests_seen=5 verdict=PASS
run 1: plat=fpga_haps bugs=0x0 tests_seen=5 verdict=PASS
run 2: plat=fpga_haps bugs=0x0 tests_seen=5 verdict=FAIL
    TIMEOUT irq_w1c_done         STATUS.DONE not set: budget 200 us x scale 1, BUSY=1
run 3: plat=silicon_a0 bugs=0xF tests_seen=5 verdict=FAIL
    FAIL    reg_reset_values     IRQ_EN@0x10 reset got 0x00000001 exp 0x00000000 (mask 0xFFFFFFFF)
    FAIL    reg_rw_walk          CFG@0x0C RW bits wrote1-read0=0x00002000 wrote0-read1=0x00000000
    FAIL    reg_alias_unique     DATA@0x14 got 0xA5001C1C exp 0xA5001414 (aliased by a later write?)
    TIMEOUT irq_w1c_done         STATUS.DONE not set: budget 200 us x scale 1, BUSY=1
OVERALL FAIL
```

- 해석: BEGIN..END 블록 4개를 각각 판정, 셸 세션의 `@@TEST` 줄(BEGIN 밖)은 무시. 종료 코드 1 → CI가 실패로 처리.
- **잘린 로그 (보드가 중간에 재부팅)** — `onsitePrep/bv08/truncated_log.txt` (BEGIN + TEST 2줄 + 부트 배너, END 없음) [확인됨]:

```text
run 0: plat=silicon_a0 bugs=0x0 tests_seen=2 verdict=INCOMPLETE
OVERALL FAIL
```

- 해석: END가 없으므로 FAIL이 아니라 **INCOMPLETE** — 다음 행동은 "어느 테스트 중에 죽었나"(마지막 TEST 다음 = id 2) + reset reason 확인 (→ BV05).
- 한계: 시뮬레이션 시간은 가상(`now_us`), 레지스터 블록은 가짜, registry는 정적 배열(링커 섹션 방식은 2.4 설명만 `[미검증]`), WDT/noinit HANG 재개 경로는 이 프로그램에 없음 (BV05 실습이 noinit 부분을 다룸).

### 6.2 랩 레시피 `[미실행]`

- QEMU로 진짜 bare-metal 맛보기 (Lima VM `elinux` 또는 macOS에 qemu가 있을 때): `qemu-system-arm -M lm3s6965evb -nographic -semihosting -kernel test.elf` — Cortex-M3 보드 모델에서 semihosting `SYS_WRITE0`로 출력 / UART0로 `@@TEST` 출력 → `parse_results.py`로 파싱 [미실행: 툴체인 `arm-none-eabi-gcc` 설치 여부 미확인].
- ZCU102가 있다면: Vitis로 FSBL 디버그 출력(`FSBL_DEBUG_INFO` 류 빌드 플래그 [추정: 이름 확인 필요]) 켜고 부트 로그 단계 확인, xsdb로 OCM에 테스트 ELF `dow` 후 `con`.

## 7. 자주 틀리는 것

1. **"BootROM이 DDR을 켠다"** — 보통 아니다. DDR은 FSBL/SPL(BL2 단계)에서 [3][9].
2. **무한 폴링 루프** `while(!(reg & DONE));` — 블록 하나가 멈추면 스위트 전체가 멈춘다. 항상 deadline.
3. **FPGA에서 실리콘 타임아웃 그대로** — 가짜 TIMEOUT (6.1 3번). HAL slowdown으로 스케일.
4. **레지스터 스윕이 W1C/RC/FIFO까지 건드림** — 상태가 지워지거나 데이터를 꺼내 버려 다른 테스트가 오염.
5. **개별 RW 테스트만으로 주소 디코드가 검증됐다고 생각** — alias는 write-all-then-read-all로만 보인다 (6.1 4번).
6. **사람용 로그를 자동화가 정규식으로 긁음** — 문구 바뀌면 깨짐. 고정 접두 key=value + END 마커.
7. **END 마커 없음 → hang을 FAIL로 오판 또는 PASS로 놓침** — INCOMPLETE를 따로.
8. **Semihosting을 남겨 둔 채 디버거 없이 실행** — Cortex-M에서 BKPT → HardFault [11]. 또 semihosting은 코어를 멈춰서 타이밍 테스트를 왜곡.
9. **캐시 켠 채 DMA 테스트 + cache maintenance 없음** — DMA 버그처럼 보이는 캐시 버그.
10. **`.noinit`을 BootROM/FSBL/TF-A가 쓰는 SRAM 영역에 배치** — 매 부팅 덮어써짐 (BV05 6.1 재현). ZynqMP는 OCM에 FSBL·TF-A(0xFFFEA000)가 있다 [5][13].

## 8. 출처

1. AMD UG1137 Zynq UltraScale+ MPSoC Software Developer Guide, "Boot Flow" / "Boot Process Overview" — https://docs.amd.com/r/2025.2-English/ug1137-zynq-ultrascale-mpsoc-swdev/Boot-Flow , https://docs.amd.com/r/en-US/ug1137-zynq-ultrascale-mpsoc-swdev/Boot-Process-Overview
2. AMD UG1137, "Pre-Boot Sequence" (PMU ROM → CSU BootROM, boot mode 읽기, OCM 초기화, boot header) — https://docs.amd.com/r/en-US/ug1137-zynq-ultrascale-mpsoc-swdev/Pre-Boot-Sequence
3. AMD UG1137, "Initialization" (FSBL processor initialization 단계에서 DDR 초기화, master-only reset에선 생략, psu_init 클럭·주변장치) — https://docs.amd.com/r/2023.2-English/ug1137-zynq-ultrascale-mpsoc-swdev/Initialization
4. AMD UG1137, "PMU Firmware Loading Options" (BootROM 또는 FSBL이 PMU FW 로드, 보안상 차이) — https://docs.amd.com/r/en-US/ug1137-zynq-ultrascale-mpsoc-swdev/PMU-Firmware-Loading-Options
5. AMD/Xilinx Wiki, "Zynq UltraScale+ FSBL" (OCM 사용 영역 0xFFFC0000–0xFFFE9FFF, 마지막 512 B는 ATF handoff 파라미터) — https://xilinx-wiki.atlassian.net/wiki/spaces/A/pages/18842019/FSBL
6. AMD UG1137, "FSBL Build Process" (FSBL 크기 약 170 KB, OCM에 맞음) — https://docs.amd.com/r/en-US/ug1137-zynq-ultrascale-mpsoc-swdev/FSBL-Build-Process
7. AMD UG1283 Bootgen User Guide, "Zynq UltraScale+ MPSoC Boot Header" / "Boot and Configuration" — https://docs.amd.com/r/en-US/ug1283-bootgen-user-guide/Zynq-UltraScale-MPSoC-Boot-Header
8. Trusted Firmware-A documentation, "Firmware Design" (BL1/BL2/BL31/BL32/BL33 위치·EL·역할) — https://trustedfirmware-a.readthedocs.io/en/latest/design/firmware-design.html
9. U-Boot documentation, "Booting from TPL/SPL" (TPL·SPL 역할, "Sets up SDRAM and loads U-Boot proper", ROM이 로드하기엔 너무 큼) — https://docs.u-boot.org/en/latest/usage/spl_boot.html
10. Arm ABI, "Semihosting for AArch32 and AArch64" (트랩 명령 표: M-profile BKPT 0xAB, A64 HLT 0xF000, SVC 0xAB/0x123456; SYS_WRITE0 0x04, SYS_EXIT 0x18) — https://github.com/ARM-software/abi-aa/blob/main/semihosting/semihosting.rst
11. SEGGER Knowledge Base, "Semihosting" (디버거 없을 때 BKPT 처리를 위한 HardFault 핸들러, SYS_WRITE0 설명) — https://kb.segger.com/Semihosting
12. Verification Academy UVM 1.2 reference, `uvm_reg_hw_reset_seq` / `uvm_reg_bit_bash_seq` (NO_REG_HW_RESET_TEST, NO_REG_BIT_BASH_TEST, NO_REG_TESTS) — https://verificationacademy.com/verification-methodology-reference/uvm/docs_1.2/html/files/reg/sequences/uvm_reg_hw_reset_seq-svh.html , https://verificationacademy.com/verification-methodology-reference/uvm/docs_1.2/html/files/reg/sequences/uvm_reg_bit_bash_seq-svh.html
13. Trusted Firmware-A, `plat/xilinx/zynqmp/include/platform_def.h` (BL31_BASE 기본 0xfffea000, PLAT_OCM_BASE 0xFFFC0000) — https://github.com/ARM-software/arm-trusted-firmware/blob/master/plat/xilinx/zynqmp/include/platform_def.h
14. GNU ld manual, "Output Section Type" (NOLOAD 의미) — https://sourceware.org/binutils/docs/ld/Output-Section-Type.html
15. GNU ld manual, "Orphan Sections" (C 식별자 이름 섹션에 `__start_SECNAME`/`__stop_SECNAME` 자동 제공) — https://sourceware.org/binutils/docs-2.28/ld/Orphan-Sections.html
16. U-Boot, `board/xilinx/zynqmp/zynqmp.c` (`reset_cpu()`: APU의 reset 요청이 PMU FW에 IPI로 전달된다는 주석, 부트 모드·reset reason 출력) — https://github.com/u-boot/u-boot/blob/master/board/xilinx/zynqmp/zynqmp.c
