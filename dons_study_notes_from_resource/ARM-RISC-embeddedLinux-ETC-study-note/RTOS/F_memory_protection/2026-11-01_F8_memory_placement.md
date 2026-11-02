# F8. 메모리 배치 실무 — 무엇을 어느 RAM에 둘지 링커에게 말하기

> **이 노트를 다 읽으면**: 여러 RAM(ITCM · DTCM · AXI SRAM · DMA SRAM · backup SRAM)을 가진 MCU에서 객체마다 위치를 근거 있게 정하고 표로 설명할 수 있다 · section attribute, 파일 패턴, CMSIS-6 copy/zero table, veneer · `long_call`로 원하는 배치를 만들고 map · `nm` · `objdump`로 증명할 수 있다 · 리셋을 넘어가는 크래시 로그(magic + 버전 + 크기 + CRC, 고정 주소, 부트로더와의 약속)를 설계할 수 있다 · ECC SRAM을 스택보다 먼저 초기화하는 startup을 어셈블리로 쓰고 비용을 잴 수 있다 · FreeRTOS heap_5 · 정적 스택, Zephyr DT 메모리 영역 · `zephyr_code_relocate`로 같은 일을 할 수 있다
> **왜 필요한가**: STM32H7 · i.MX RT · Corstone-300 같은 고성능 MCU에서 "DMA가 안 와요", "ISR 지터가 커요", "필드 크래시 로그가 비어 있어요", "전원 켤 때만 HardFault" 같은 버그의 상당수가 **배치** 문제다. 면접에서 "What do you put in TCM?"은 드론 · 로봇 펌웨어 단골 질문이다
> **Don 기준 난이도**: SSD 컨트롤러에서 TCM · DRAM · SRAM 뱅크에 객체를 나눠 넣고 TCM ECC를 지우던 경험이 그대로 쓰인다 / 새로 배울 부분은 Cortex-M 칩별 지도(H7 · RT1060 · AN547), CMSIS-6 표 형식, veneer와 `long_call`의 실제 비용, RTOS(heap_5 · 정적 스택 · 커널이 제공하는 idle 스택)의 배치, Zephyr의 DT 영역이 **초기화되지 않는다**는 함정
> **선행 노트**: B5(리셋부터 main까지 — 링커 스크립트 · VMA/LMA · `AT>` · NOLOAD · copy table 기본 · `.ramfunc` 기본 — **다시 설명하지 않는다**), B8(캐시 · TCM · H7 버스 지도 · 배치 표 초판), B9(Cortex-R TCM · ECC), B11(map 파일 · `--gc-sections`), E6(DMA + 캐시), E8(리셋 원인 · `.noinit` 기록 기본), F1(힙 구현). 이어지는 노트: F4(MPU 속성), M5(크래시 사후 분석), Q6(NPU arena 배치)
> 🔁 **Bare-Metal 겹침**: BM P2(실행 위치 최적화: flash vs RAM vs TCM 실행, 캐시 켜기, 정렬, ramfunc) · BM G6(저장 클래스 · 배치의 섹션 지정 부분) · BM Q7(메모리 오류 · ECC의 SRAM ECC 초기화 부분) — 이 노트가 주인. Bare-Metal 노트는 이 노트를 링크하고 const → flash · 초기화 순서 같은 C 저장 클래스 기초(G6)와 스크러빙 · 단일/이중 비트 오류 처리 · SEU(Q7)만 더한다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 컨트롤러에서 하던 일을 떠올려 보자. FTL의 명령 디스패치와 매핑 캐시는 Cortex-R의 TCM에, NAND 데이터 버퍼는 DRAM에, 컨트롤러 간 메일박스는 공유 SRAM에 두었다. 이유는 셋이었다. **빨라야 하는 것**(0 wait, 결정적), **누가 닿아야 하는 것**(DMA · 다른 코어 · 하드웨어 엔진), **오래 남아야 하는 것**(리셋 뒤에도 남는 로그). 이 세 질문이 MCU에서도 그대로 배치를 정한다.

고성능 Cortex-M MCU는 RAM이 한 덩어리가 아니다. STM32H743에는 ITCM · DTCM · AXI SRAM · SRAM1~3 · SRAM4 · backup SRAM이 있고, 각각 **속도, 캐시 여부, DMA 접근 가능 여부, 저전력 유지 여부, ECC 초기화 방식**이 다르다. C 컴파일러는 이것을 모른다. "이 변수는 저 RAM에"를 알려 주는 방법은 **섹션 이름**과 **링커 스크립트**뿐이다. 이 노트의 부제 그대로 — 무엇을 어느 RAM에 둘지 링커에게 말하는 법이다.

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="125" y="22" font-size="13" text-anchor="middle" font-weight="bold">객체 (C 코드가 만든 것)</text>
<text x="555" y="22" font-size="13" text-anchor="middle" font-weight="bold">메모리 영역 (링커 MEMORY)</text>
<rect x="10" y="40" width="230" height="32" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="125" y="61" font-size="12" text-anchor="middle">ISR · rate loop · 커널 핫패스</text>
<rect x="10" y="88" width="230" height="32" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="125" y="109" font-size="12" text-anchor="middle">태스크 스택 · 제어 상태</text>
<rect x="10" y="136" width="230" height="32" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="125" y="157" font-size="12" text-anchor="middle">DMA 버퍼 (SPI · UART · ADC)</text>
<rect x="10" y="184" width="230" height="32" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="125" y="205" font-size="12" text-anchor="middle">큰 버퍼 · 힙 · 로그 스테이징</text>
<rect x="10" y="232" width="230" height="32" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="125" y="253" font-size="12" text-anchor="middle">크래시 로그 · 부팅 카운터</text>
<rect x="10" y="280" width="230" height="32" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="125" y="301" font-size="12" text-anchor="middle">나머지 코드 · const 테이블</text>
<rect x="430" y="40" width="240" height="32" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="550" y="61" font-size="12" text-anchor="middle">ITCM — 0 wait, 명령 페치</text>
<rect x="430" y="88" width="240" height="32" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="550" y="109" font-size="12" text-anchor="middle">DTCM — 0 wait, 캐시 없음</text>
<rect x="430" y="136" width="240" height="32" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="550" y="157" font-size="12" text-anchor="middle">DMA SRAM (non-cacheable)</text>
<rect x="430" y="184" width="240" height="32" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="550" y="205" font-size="12" text-anchor="middle">AXI SRAM · 외부 RAM (캐시)</text>
<rect x="430" y="232" width="240" height="32" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="550" y="253" font-size="12" text-anchor="middle">backup SRAM · noinit 창</text>
<rect x="430" y="280" width="240" height="32" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="550" y="301" font-size="12" text-anchor="middle">Flash (XIP) + I-cache</text>
<line x1="242" y1="56" x2="426" y2="56" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="242" y1="104" x2="426" y2="104" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="242" y1="152" x2="426" y2="152" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="242" y1="200" x2="426" y2="200" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="242" y1="248" x2="426" y2="248" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="242" y1="296" x2="426" y2="296" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="334" y="50" font-size="12" text-anchor="middle">지터 0</text>
<text x="334" y="98" font-size="12" text-anchor="middle">push/pop 결정적</text>
<text x="334" y="146" font-size="12" text-anchor="middle">DMA가 닿아야</text>
<text x="334" y="194" font-size="12" text-anchor="middle">크기</text>
<text x="334" y="242" font-size="12" text-anchor="middle">리셋을 넘어</text>
<text x="334" y="290" font-size="12" text-anchor="middle">기본값</text>
<text x="340" y="338" font-size="12" text-anchor="middle">수단: section attribute · 파일 패턴 · copy/zero table · DT 영역 · heap 영역 표</text>
</svg>
```

그림 1 — 배치의 큰 그림. 왼쪽은 C 코드가 만든 객체, 오른쪽은 링커 스크립트의 `MEMORY` 영역. 화살표 위 글자는 "왜 거기인가"다. 이 연결을 만드는 수단이 이 노트의 내용이다.

B5 · B8과 이 노트의 경계를 먼저 정리한다. 같은 이야기를 다시 하지 않기 위해서다.

| 주제 | 이미 다룬 곳 | 이 노트에서 더하는 것 |
|---|---|---|
| `MEMORY` · `SECTIONS` · VMA/LMA · `AT>` · NOLOAD · KEEP | B5 §2 | 영역이 4~6개일 때의 설계, 파일 패턴 · `EXCLUDE_FILE` |
| copy table 직접 만들기, `.data` 와일드카드 함정 | B5 §2.7 | **CMSIS-6 표준 형식**(`__copy_table_start__`, 워드 단위)과 CMSIS가 직접 도는 `__cmsis_start()` |
| `.ramfunc` 한 개와 veneer 관찰 | B5 §9, B8 §4.3 | veneer · `long_call` · 함수 포인터의 **호출당 비용 실측**, 커널 핫패스를 TCM · SRAM으로 옮겼을 때 veneer 22 · 27개 |
| H7 버스 지도, 배치 표 초판 | B8 §4 | i.MX RT FlexRAM, Corstone-300(AN547), 배치 결정표 확장판, 메모리 예산 |
| `.noinit` 리셋 기록의 규칙(magic + CRC) | E8 §7 | 링 버퍼 패턴, **고정 주소 영역**, 부트로더 스택 충돌 실측, ECC와의 충돌 |
| TCM ECC 초기화 (Cortex-R) | B9 §7 | Cortex-M · 일반 SRAM의 ECC 초기화 규칙, 어셈블리 startup, 방식별 시간 측정 |
| ucHeap을 `.noinit`으로 | B5 §8.3 | heap_5로 **여러 뱅크**에 걸친 힙 |

### 실습 환경 한 줄

QEMU `mps2-an385`(Cortex-M3 25 MHz) bare-metal + FreeRTOS V11.3.2, QEMU `mps3-an547`(Cortex-M55, ITCM/DTCM 있음) + Zephyr v4.4.2, 일부는 VM 호스트 C 모델과 빌드 전용(H7 모양 맵). 시간은 `ICOUNT=1`(명령 1개 = 32 ns)과 FPGAIO COUNTER(25 MHz, 1 tick = 40 ns)로 잰 **QEMU icount 기준 값이며, 실제 칩과 절대값은 다르다**(캐시 · 플래시 wait state · 버스 · TCM 속도 미모델링). QEMU에는 ECC도 없다. 그래서 이 노트에서 "빨라졌다"는 숫자는 명령 수 차이만 보여 줄 수 있고, 메모리 속도 차이는 보여 줄 수 없다 — 그 부분은 매번 정직하게 표시한다.

### 0.1 실습 보드 — AN385에도 RAM이 네 덩어리 있다

랩 링커 스크립트(`mps2_m3.ld`)는 FLASH(0x0)와 RAM(0x2000_0000) 두 영역만 쓴다. 그런데 QEMU의 `info mtree`를 보면 AN385에는 RAM이 더 있다.

```sh
echo "info mtree -f" | qemu-system-arm -machine mps2-an385 -cpu cortex-m3 -nographic -monitor stdio -serial none | grep -v i/o
```

```text
  0000000000000000-00000000003fffff (prio 0, ram): mps.ssram1
  0000000000400000-00000000007fffff (prio 0, ram): mps.ssram1
  0000000001000000-0000000001003fff (prio 0, ram): mps.blockram
  0000000001004000-0000000001007fff (prio 0, ram): mps.blockram
  0000000001008000-000000000100bfff (prio 0, ram): mps.blockram
  000000000100c000-000000000100ffff (prio 0, ram): mps.blockram
  0000000020000000-00000000203fffff (prio 0, ram): mps.ssram23
  0000000020400000-00000000207fffff (prio 0, ram): mps.ssram23
  0000000021000000-0000000021ffffff (prio 0, ram): mps.ram
```

이 노트의 bare-metal 예제는 이 네 덩어리에 "실제 MCU의 역할"을 하나씩 맡긴다. 속도는 모두 같지만(QEMU), **주소가 다르다는 것** — 그래서 링커 배치 · 복사 · 초기화 · veneer가 실제 칩과 똑같이 일어난다는 것 — 이 중요하다.

```
0x0000_0000  SSRAM1    4 MB   -> FLASH  (code, const, load images)
0x0100_0000  blockram 16 KB   -> FAST   ("TCM": hot code, stacks)   aliased every 16 KB
0x2000_0000  SSRAM2/3  4 MB   -> RAM    (.data .bss MSP heap)        top 4 KB = NOINIT
0x2100_0000  PSRAM    16 MB   -> PSRAM  ("AXI / external": big buffers, 2nd heap bank)
```

예제 1 — 무엇을 확인하나: blockram이 정말 16 KB짜리가 반복(alias)되는지, 그리고 QEMU가 `SYSRESETREQ` 리셋 때 RAM 내용을 지우는지(지운다면 noinit 실험이 불가능하다).

```c
/* probe: blockram size/alias, and what QEMU does to RAM on SYSRESETREQ */
#include <stdint.h>
#include "board.h"
#define AIRCR REG32(0xE000ED0Cu)
__attribute__((section(".noinit"))) uint32_t ni_cnt;   /* lab .noinit (LMA in FLASH) */
uint32_t bss_cnt;
int main(void)
{
    volatile uint32_t *b0 = (uint32_t *)0x01000000u, *b1 = (uint32_t *)0x01004000u;
    volatile uint32_t *ram_hi = (uint32_t *)0x203FF000u, *ps = (uint32_t *)0x21000000u;
    board_printf_raw("boot: ni_cnt=%x bss_cnt=%u ram_hi=%x psram=%x bram=%x\n", ni_cnt, bss_cnt, *ram_hi, *ps, *b0);
    *b0 = 0x11111111u; *b1 = 0x22222222u;
    board_printf_raw("bram[0]=%x after writing alias +16K -> %s\n", *b0, *b0 == 0x22222222u ? "aliased (16 KB)" : "distinct");
    if (ni_cnt > 2 || ni_cnt == 0) ni_cnt = 0;
    ni_cnt++; bss_cnt++; *ram_hi = 0xA5A50000u + ni_cnt; *ps = 0xC0DE0000u + ni_cnt;
    if (ni_cnt < 3) { board_printf_raw("-> SYSRESETREQ\n"); AIRCR = 0x05FA0004u; for (;;) {} }
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/F8/probe/main.c OUT=/var/tmp/rtos/F8/probe && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/F8/probe/app.elf 5'
```

```text
boot: ni_cnt=0 bss_cnt=0 ram_hi=0 psram=0 bram=0
bram[0]=22222222 after writing alias +16K -> aliased (16 KB)
-> SYSRESETREQ
boot: ni_cnt=1 bss_cnt=0 ram_hi=a5a50001 psram=c0de0001 bram=22222222
bram[0]=22222222 after writing alias +16K -> aliased (16 KB)
-> SYSRESETREQ
boot: ni_cnt=2 bss_cnt=0 ram_hi=a5a50002 psram=c0de0002 bram=22222222
bram[0]=22222222 after writing alias +16K -> aliased (16 KB)
[board_exit] code=0
```

출력에서 볼 것:

- `0x0100_4000`에 쓴 값이 `0x0100_0000`에서 읽힌다 → blockram은 **16 KB가 4번 반복**된 창이다. 링커에는 16 KB로만 알려야 한다(더 크게 쓰면 앞부분을 덮어쓴다).
- QEMU는 `SYSRESETREQ`(AIRCR에 `0x05FA0004`) 리셋 때 **어느 RAM도 지우지 않는다**(ram_hi · psram · blockram 값이 남는다). 실칩의 웜 리셋과 같은 성질이라 noinit 실험을 할 수 있다. 반대로 QEMU를 새로 띄우면 RAM은 **0**이다 — 실칩의 전원 투입 직후(쓰레기 값)와 다르다. 이 차이는 §5에서 다시 짚는다.
- 처음 만났던 함정 하나: 첫 버전은 "SSRAM1이 0x40_0000에서 반복되나"를 보려고 `*(volatile uint32_t *)0x0`을 읽었는데, GCC가 **NULL 역참조를 UB로 보고 `udf #255`(정의되지 않은 명령)를 넣어** HardFault가 났다. 0번지가 정상 메모리인 MCU에서 0번지를 읽으려면 asm이나 링커 심볼을 거쳐야 한다(GCC의 `-fno-delete-null-pointer-checks`도 방법이다).

---

## 1. 실제 칩의 메모리 지도 — 무엇을 어디에 두나

> 🔁 이 절은 Bare-Metal BM P2(실행 위치 최적화: flash vs RAM vs TCM 실행, 캐시 켜기, 정렬, ramfunc)와 겹친다 (주인: 이 노트).

### 1.1 직관 — RAM마다 "성격"이 다섯 가지 있다

Don이 새 칩의 RM을 처음 열 때 각 RAM에 대해 다섯 가지를 표로 만들어 두면 배치가 거의 자동으로 정해진다.

| 성격 | 질문 | 왜 중요한가 |
|---|---|---|
| 접근 시간 | 0 wait인가, 버스 매트릭스를 몇 단계 지나나 | ISR · 제어 루프의 지터 |
| 캐시 | D-cache가 덮나 (MPU 기본 속성) | DMA 일관성(B8 §6, E6), WCET 분산 |
| 버스 마스터 | 어느 DMA가 닿나 | 닿지 않으면 전송이 "항상" 실패 |
| 유지 | 리셋 · 저전력 · VBAT에서 남나 | 크래시 로그 · 부팅 카운터 |
| ECC | ECC가 있나, granule은 몇 비트인가 | 첫 읽기 전에 써야 한다, 부분 쓰기 = RMW |

### 1.2 STM32H743 — B8의 지도에 두 가지를 더한다

B8 §4.2에서 H743의 ITCM 64 KB(0x0000_0000), DTCM 128 KB(0x2000_0000), AXI SRAM 512 KB(0x2400_0000), SRAM1~3 288 KB(0x3000_0000), SRAM4 64 KB(0x3800_0000)와 "DMA1/2는 TCM에 못 닿는다"를 봤다. 여기서 더할 것은 둘이다.

- **backup SRAM 4 KB, 0x3880_0000**. Zephyr v4.4.2 `dts/arm/st/h7/stm32h7.dtsi`의 `backup_sram: memory@38800000`(`compatible = "zephyr,memory-region", "st,stm32-backup-sram"`)로 확인했다. VBAT 전원으로 내용을 유지할 수 있는 영역이라 POR(주 전원 차단)까지 넘어가는 기록에 쓴다. 유지에 필요한 레귤레이터 · 클럭 설정 순서는 RM0433 원문을 이 노트에서 확인하지 못했다. Zephyr는 이 영역용 섹션 태그 `__stm32_backup_sram_section`을 준다(`include/zephyr/linker/section_tags.h`).
- **TCM ECC 초기화**. Zephyr v4.4.2 `soc/st/stm32/stm32h7x/soc_m7.c`의 `soc_reset_hook()`은 주석에서 ST AN5342 "How to use error correction code (ECC) management for internal memories protection on STM32 MCUs" Rev. 7 §3.1.1을 인용하며, 리셋 직후 TCM 내용은 정의되지 않았고 **읽기 · 정렬 안 된 접근 · 부분 크기 접근이 ECC 오류를 낸다**고 적고, ITCM은 64비트 쓰기, DTCM은 32비트 쓰기로 전체를 0으로 채운다. 이것이 §6의 실제 사례다.

### 1.3 i.MX RT1060 — 비율을 바꿀 수 있는 TCM (FlexRAM)

NXP i.MX RT10xx의 Cortex-M7은 512 KB짜리 **FlexRAM**을 32 KB 뱅크 16개로 나눠 각 뱅크를 ITCM · DTCM · OCRAM 중 하나로 쓴다. Zephyr v4.4.2에서 확인한 사실:

- `dts/arm/nxp/imxrt/nxp_rt10xx.dtsi`: `flexram,bank-size = <32>`, `itcm@0` 128 KB, `dtcm@20000000` 128 KB. `nxp_rt1060.dtsi`: `flexram,num-ram-banks = <16>`, FlexRAM의 OCRAM은 `ocram@20280000` 256 KB, FlexRAM 밖의 `ocram2: ocram@20200000` 512 KB가 따로 있다. 즉 기본 분할은 ITCM 128 + DTCM 128 + OCRAM 256 KB다.
- 분할을 런타임에 바꾸는 코드: `drivers/misc/nxp_flexram`의 `flexram_dt_partition()`은 DT의 `flexram,bank-spec` 목록을 뱅크당 2비트로 `IOMUXC_GPR->GPR17`에 쓰고 `GPR16`의 `FLEXRAM_BANK_CFG_SEL`을 세운다. 주석이 "stack 변수를 만들지 말고 RAM의 데이터를 쓰지 말라"고 경고한다 — **자기가 서 있는 RAM의 정체를 바꾸는 코드**이기 때문이다. 이 함수는 `soc/nxp/imxrt/imxrt10xx/soc.c`의 `soc_reset_hook()`에서 `SystemInit()`보다 먼저 불린다.
- RT1060-EVK 보드(`boards/nxp/mimxrt1060_evk/mimxrt1060_evk.dtsi`)의 `zephyr,sram`은 **외부 SDRAM**(`sdram0: memory@80000000`, 32 MB)이고 `zephyr,itcm` · `zephyr,dtcm`은 따로 지정돼 있다. 코드는 FlexSPI 플래시(AHB 0x6000_0000, XIP)에서 돈다. "기본 RAM이 외부 SDRAM"이라는 점 때문에 이 보드에서 성능이 필요한 객체는 **명시적으로** DTCM에 옮겨야 한다.

설계 함의: FlexRAM은 "ITCM이 모자라면 DTCM을 줄이자"가 링커 스크립트 + 레지스터 한 줄로 된다. 대신 **링커 스크립트의 MEMORY 크기, 부트 ROM이 읽는 fuse(기본 분할), 런타임 GPR17 값이 모두 같은 그림이어야** 한다(Zephyr는 `BUILD_ASSERT`로 DT 노드 크기와 뱅크 합을 대조한다). 부트 ROM이 쓰는 기본 분할의 fuse 이름은 RM 원문으로 확인하지 못했다.

### 1.4 Corstone-300 / MPS3 AN547 (Cortex-M55) — QEMU에 TCM이 있는 보드

Zephyr v4.4.2 `boards/arm/mps3/mps3_corstone300_an547.dts`와 QEMU 10.2 `info mtree`로 확인했다.

| 영역 | Secure 주소 (NS 별칭) | 크기 | Zephyr에서의 역할 |
|---|---|---|---|
| ITCM | 0x1000_0000 (0x0000_0000) | 512 KB | `zephyr,flash` — 코드가 여기서 돈다 |
| SRAM | 0x1100_0000 (0x0100_0000) | 2 MB | `zephyr,memory-region = "SRAM"` |
| DTCM | 0x3000_0000 (0x2000_0000) | 512 KB | `zephyr,sram` — .data · .bss · 스택 |
| ISRAM | 0x3100_0000 (0x2100_0000) | 4 MB | `zephyr,memory-region = "ISRAM"` |
| DDR4 | 0x6000_0000 ~ | 256 MB 블록 × 여러 개 | `zephyr,memory-region` |

이 보드에서는 "플래시"가 없고 ITCM이 그 역할을 한다. QEMU는 TCM을 일반 RAM으로 모델링한다(속도 차이 없음). §7의 Zephyr 실험이 이 보드에서 돈다.

### 1.5 Cortex-R — TCM이 기본인 세계

Cortex-R5/R52의 ATCM · BTCM(· CTCM), 위치 레지스터, TCM ECC 켜기와 부팅 초기 TCM 지우기는 B9 §7에 정리했다. Zephyr의 `CONFIG_DISABLE_TCM_ECC`와 `z_arm_tcm_disable_ecc()`(`arch/arm/core/cortex_a_r/tcm.c`, `reset.S`에서 호출)도 거기서 봤다. 이 노트의 규칙(§6)은 R에도 그대로 적용된다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
<text x="115" y="20" font-size="13" text-anchor="middle" font-weight="bold">STM32H743 (M7)</text>
<text x="340" y="20" font-size="13" text-anchor="middle" font-weight="bold">i.MX RT1060 (M7)</text>
<text x="565" y="20" font-size="13" text-anchor="middle" font-weight="bold">AN547 (M55)</text>
<rect x="10" y="32" width="210" height="38" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="20" y="48" font-size="12">ITCM 64 KB</text><text x="20" y="63" font-size="12">0x0000_0000 · ECC 64b</text>
<rect x="10" y="76" width="210" height="38" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="20" y="92" font-size="12">DTCM 128 KB</text><text x="20" y="107" font-size="12">0x2000_0000 · ECC 32b</text>
<rect x="10" y="120" width="210" height="38" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="20" y="136" font-size="12">AXI SRAM 512 KB</text><text x="20" y="151" font-size="12">0x2400_0000 · 캐시</text>
<rect x="10" y="164" width="210" height="38" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="20" y="180" font-size="12">SRAM1-3 288 KB</text><text x="20" y="195" font-size="12">0x3000_0000 · DMA1/2</text>
<rect x="10" y="208" width="210" height="38" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="20" y="224" font-size="12">SRAM4 64 KB</text><text x="20" y="239" font-size="12">0x3800_0000 · BDMA</text>
<rect x="10" y="252" width="210" height="38" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="20" y="268" font-size="12">Backup SRAM 4 KB</text><text x="20" y="283" font-size="12">0x3880_0000 · VBAT</text>
<rect x="10" y="296" width="210" height="38" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="20" y="312" font-size="12">Flash 2 MB</text><text x="20" y="327" font-size="12">0x0800_0000 · XIP</text>
<rect x="235" y="28" width="210" height="182" rx="6" fill="none" stroke="#e08a3c" stroke-dasharray="4 3"/>
<text x="340" y="204" font-size="12" text-anchor="middle">FlexRAM 512 KB = 32 KB × 16 뱅크</text>
<rect x="245" y="36" width="190" height="38" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="255" y="52" font-size="12">ITCM 128 KB (기본)</text><text x="255" y="67" font-size="12">0x0000_0000</text>
<rect x="245" y="80" width="190" height="38" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="255" y="96" font-size="12">DTCM 128 KB (기본)</text><text x="255" y="111" font-size="12">0x2000_0000</text>
<rect x="245" y="124" width="190" height="38" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="255" y="140" font-size="12">OCRAM 256 KB</text><text x="255" y="155" font-size="12">0x2028_0000</text>
<text x="340" y="186" font-size="12" text-anchor="middle">GPR17 뱅크당 2비트로 재분할</text>
<rect x="235" y="220" width="210" height="38" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="245" y="236" font-size="12">OCRAM2 512 KB</text><text x="245" y="251" font-size="12">0x2020_0000</text>
<rect x="235" y="264" width="210" height="38" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="245" y="280" font-size="12">SDRAM 32 MB (EVK)</text><text x="245" y="295" font-size="12">0x8000_0000 · zephyr,sram</text>
<rect x="235" y="308" width="210" height="38" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="245" y="324" font-size="12">FlexSPI NOR (외부)</text><text x="245" y="339" font-size="12">0x6000_0000 · XIP</text>
<rect x="460" y="32" width="210" height="38" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="470" y="48" font-size="12">ITCM 512 KB</text><text x="470" y="63" font-size="12">0x1000_0000 · 코드</text>
<rect x="460" y="76" width="210" height="38" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="470" y="92" font-size="12">SRAM 2 MB</text><text x="470" y="107" font-size="12">0x1100_0000</text>
<rect x="460" y="120" width="210" height="38" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="470" y="136" font-size="12">DTCM 512 KB</text><text x="470" y="151" font-size="12">0x3000_0000 · zephyr,sram</text>
<rect x="460" y="164" width="210" height="38" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="470" y="180" font-size="12">ISRAM 4 MB</text><text x="470" y="195" font-size="12">0x3100_0000</text>
<rect x="460" y="208" width="210" height="38" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="470" y="224" font-size="12">DDR4 (256 MB 블록)</text><text x="470" y="239" font-size="12">0x6000_0000 ~</text>
<text x="565" y="270" font-size="12" text-anchor="middle">Secure 주소 기준</text>
<text x="565" y="286" font-size="12" text-anchor="middle">(NS 별칭 = −0x1000_0000)</text>
</svg>
```

그림 2 — 세 칩의 RAM 지도 비교(주소 · 크기는 Zephyr v4.4.2 DTS와 QEMU `info mtree`, H7 TCM ECC 폭은 Zephyr `soc_m7.c` 주석이 인용한 AN5342). 주황 = TCM, 초록 = 일반 SRAM, 빨강 = DMA 전용으로 쓸 SRAM, 회색 = 유지 · 외부, 파랑 = 코드 저장소. H7은 영역이 많고 버스 마스터 제약이 강하며, RT1060은 TCM 비율을 바꿀 수 있고, AN547은 TCM이 크고 코드가 ITCM에서 돈다.

### 1.6 배치 결정표 (B8 §4.4의 확장판)

B8의 표를 RTOS 객체 단위로 넓히고, "왜"와 "함정"을 붙였다. 아래 칩 이름은 H7 기준이다.

| 객체 | 1순위 | 이유 | 함정 · 대안 |
|---|---|---|---|
| 벡터 테이블 | ITCM 또는 DTCM으로 복사 + `VTOR` | 예외 진입 때 벡터 페치가 플래시를 안 탄다 | `VTOR` 정렬(테이블 크기를 2의 거듭제곱으로 올림한 경계) — B2 |
| 고속 ISR (FOC 20 kHz, IMU DRDY) | ITCM | 페치 지터 0 | 호출하는 함수 · const 테이블도 같이 옮겨야 효과가 있다 |
| 커널 핫패스 (PendSV, `vTaskSwitchContext`, list) | ITCM | 컨텍스트 스위치 시간의 분산 감소 | veneer 수십 개 생김(§3.4), 크기 수 KB |
| 태스크 스택 · TCB | DTCM | push/pop이 캐시 miss · 버스 경합을 안 겪는다 | 커널이 제공하는 idle · timer 스택은 `.bss`로 가서 DTCM이 아닐 수 있다(§4.3) |
| MSP(ISR 스택) | DTCM | 예외 스태킹이 결정적 | DTCM이 ECC면 스택을 쓰기 전에 지워야 한다(§6) |
| 제어 상태 · 필터 계수 | DTCM (`.dtcm_data` + 복사) | 결정적 접근 | 초기값이 있으면 copy table 한 줄 필요 |
| DMA 버퍼 | DMA가 닿는 SRAM, MPU non-cacheable | DMA1/2는 TCM에 못 닿는다(B8) | 32 B 정렬 · 크기 32의 배수(캐시 라인, E6) |
| 큰 버퍼 · 블랙박스 로그 스테이징 | AXI SRAM · 외부 RAM | 크기 | 캐시 일관성(E6) |
| 힙 | AXI + DMA SRAM 두 뱅크(heap_5) | 큰 공간 | heap_5는 주소 순서로 first-fit — "빠른 힙"을 보장하지 않는다(§4.1) |
| 크래시 로그 · 부팅 카운터 | backup SRAM 또는 고정 noinit 창 | 리셋 · VBAT 유지 | magic + 버전 + 크기 + CRC, 부트로더와의 약속(§5) |
| 플래시 쓰기 · 지우기 코드 | RAM(`ramfunc`) | 같은 뱅크에서 페치 불가인 칩 | 그 코드가 부르는 모든 함수도 RAM에 |
| NPU 텐서 arena | AXI SRAM · NPU가 닿는 SRAM | NPU 버스 마스터 | Q6에서 |
| 나머지 코드 · const | Flash + I-cache | 기본 | 캐시를 켜는 순서(B8 §2.3) |

### 1.7 실행 위치와 성능 — flash vs RAM vs TCM, 캐시, 정렬

BM P2의 질문 "코드를 어디서 실행할까"에 대한 이 노트의 답을 정리한다. 측정은 QEMU가 메모리 속도를 모델링하지 않아 할 수 없으므로, 판단 순서만 적는다.

1. **먼저 캐시를 켠다.** M7 · M55의 I-cache · D-cache를 켜는 것(B8 §2.3)이 코드 위치를 옮기는 것보다 효과가 크고 공짜다. 플래시 가속기(ART 등, B8 §5)도 같다.
2. **지터가 문제일 때 TCM으로 옮긴다.** 캐시는 평균을 줄이지만 최악(miss)을 남긴다. 20 kHz 전류 루프처럼 "최악이 곧 사양"인 코드는 ITCM으로 옮겨 분산을 없앤다.
3. **플래시 동작 중 실행이 필요할 때 RAM으로 옮긴다**(B5 §9.1).
4. **정렬**: 핫 루프 시작을 페치 폭(M7 ITCM은 64비트 — B8 §4.1)에 맞추면 첫 페치가 반으로 잘리지 않는다. GCC는 `-falign-functions=8`, `-falign-loops=8` 같은 옵션을 주고, 함수 하나만이면 `__attribute__((aligned(8)))`를 붙인다. 효과는 칩마다 다르고 QEMU에서는 측정할 수 없다 — 실칩에서 DWT CYCCNT로 확인할 것.
5. **옮긴 뒤에는 호출 경로를 본다.** 함수 본체가 ITCM에 있어도 호출이 플래시의 veneer를 거치면 일부가 여전히 플래시 페치다(§3).

Don 경험과 연결: SSD 펌웨어에서 "핫 경로만 TCM, 나머지는 캐시 붙은 DRAM/플래시"로 나눴던 판단이 그대로다. 차이는 Cortex-M에서는 TCM이 수십~수백 KB라 **커널 핫패스와 몇 개 ISR이 들어갈 정도**라는 점이다.

---

## 2. 메커니즘 ① — section attribute, 파일 패턴, CMSIS-6 copy/zero table

> 🔁 이 절은 Bare-Metal BM G6(저장 클래스 · 배치)의 섹션 지정 부분과 겹친다 (주인: 이 노트).

### 2.1 배치를 지정하는 세 가지 손잡이

B5에서 출력 섹션 · `AT>` · NOLOAD 문법은 다 봤다. 이 노트에서 쓰는 손잡이를 정리하면 셋이다.

| 손잡이 | 쓰는 곳 | 장점 | 단점 |
|---|---|---|---|
| `__attribute__((section(".fast_text")))` | 소스 (객체 하나) | 의도가 코드에 보인다 | 서드파티 소스(커널, HAL)를 고쳐야 한다 |
| 파일 패턴 `*hot.o(.text*)` + `EXCLUDE_FILE` | 링커 스크립트 | 소스를 안 고친다, 라이브러리 통째로 | 오브젝트 이름이 빌드 시스템마다 다르다(`hot.o`, `hot.c.obj`, 임시 이름) |
| Zephyr `zephyr_code_relocate()` · DT 영역 | CMake · DT | 링커 스크립트를 생성해 준다 | 생성물을 열어 봐야 무슨 일이 일어났는지 안다(§7) |

한 가지 주의: 랩 템플릿 Makefile처럼 **모든 소스를 gcc 한 번에 컴파일하면 오브젝트가 `/tmp/ccXXXX.o` 같은 임시 이름**이 되어 파일 패턴이 매치되지 않는다. 이 노트의 빌드 스크립트(`bld.sh`)는 소스마다 `-c`로 따로 컴파일해 `hot.o`라는 이름을 남긴다. Zephyr는 `hot.c.obj`로 이름을 붙인다(§7의 생성 스크립트에 `*hot.c.obj`가 보인다).

### 2.2 CMSIS-6의 표준 copy/zero table

B5 §2.7에서는 표 형식을 직접 만들었다(`{lma, vma, bytes}`). CMSIS-6에는 **표준 형식과 그 표를 도는 코드가 이미 있다**. Zephyr 모듈 `modules/hal/cmsis_6`(커밋 30a859f, 2025-08)에서 확인한 내용:

- `CMSIS/Core/Include/m-profile/cmsis_gcc_m.h`의 `__cmsis_start()`: `__copy_table_t { const uint32_t *src; uint32_t *dest; uint32_t wlen; }`와 `__zero_table_t { uint32_t *dest; uint32_t wlen; }`를 `__copy_table_start__`~`__copy_table_end__`, `__zero_table_start__`~`__zero_table_end__` 사이에서 돌고, 끝나면 `_start()`(C 런타임 진입점)를 부른다. `__PROGRAM_START`가 정의돼 있지 않으면 이 함수가 `__PROGRAM_START`가 된다. picolibc를 쓰면 `__PROGRAM_START`가 picolibc의 `_start`로 바뀐다.
- **단위가 워드(`wlen`)다.** B5의 바이트 단위와 다르다. 링커 스크립트에서 `SIZEOF(.data) / 4`로 쓴다. 섹션 끝을 `ALIGN(4)`로 맞추지 않으면 마지막 1~3바이트가 복사되지 않는다.
- 템플릿 링커 스크립트 `CMSIS/Core/Template/Device_M/Config/Device_gcc.ld`는 `.copy.table`에 `.data` 한 줄을, `.zero.table`은 비워 두고 주석으로 "`.bss`는 C 런타임 startup이 지운다"고 적는다. 추가 영역은 주석 처리된 `.data2` · `.bss2` 줄을 풀어서 쓴다.

이 노트의 bare-metal 예제는 이 CMSIS 코드를 **그대로** 쓴다. startup은 벡터 테이블과 `Reset_Handler`만 만든다.

```c
/* startup_f8.c — vectors + Reset_Handler that hands off to CMSIS-6 __cmsis_start() (copy/zero tables) */
#include <stdint.h>
#include <stdlib.h>
#include "cmsis_compiler.h"           /* -> cmsis_gcc.h -> m-profile/cmsis_gcc_m.h: __cmsis_start() */
#include "board.h"
extern uint32_t __StackTop;
int main(void);
void Reset_Handler(void);
void c_start(void);
void Default_Handler(void);
void HardFault_Handler(void) __attribute__((naked));
#define WEAK_DEFAULT __attribute__((weak, alias("Default_Handler")))
void NMI_Handler(void) WEAK_DEFAULT;  void SVC_Handler(void) WEAK_DEFAULT;
void PendSV_Handler(void) WEAK_DEFAULT; void SysTick_Handler(void) WEAK_DEFAULT;
void TIMER0_IRQHandler(void) WEAK_DEFAULT;
typedef void (*vector_t)(void);
__attribute__((section(".vectors"), used)) const vector_t g_vectors[16 + 32] = {
    (vector_t)&__StackTop, Reset_Handler, NMI_Handler, HardFault_Handler,
    Default_Handler, Default_Handler, Default_Handler, 0, 0, 0, 0,
    SVC_Handler, Default_Handler, 0, PendSV_Handler, SysTick_Handler,
    [16 + 8] = TIMER0_IRQHandler,
};
__attribute__((weak)) void early_init(void) { }  /* hook: runs before any RAM is initialised */
void _start(void) { board_init(); board_exit(main()); }   /* CMSIS calls this after the tables */
void c_start(void) { __PROGRAM_START(); }                       /* = __cmsis_start(): tables, then _start */
__attribute__((weak)) void Reset_Handler(void) { early_init(); c_start(); }   /* ex2 overrides in asm */
void Default_Handler(void)
{
    uint32_t ipsr; __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    board_printf_raw("[Default_Handler] exception %u\n", (unsigned)ipsr); board_exit(254);
}
void HardFault_Handler(void)
{ __asm volatile(" tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n b board_hardfault\n"); }
```

메모리 영역과 섹션 규칙은 두 파일로 나눴다(B8 §4.3처럼 `INCLUDE`로 조합한다 — 메모리 지도만 바꿔 끼우면 같은 섹션 규칙으로 다른 보드를 만든다).

```text
/* mem_f8.ld — the four real RAMs of QEMU mps2-an385, each given a "real MCU" role */
MEMORY
{
    FLASH  (rx)  : ORIGIN = 0x00000000, LENGTH = 4M        /* SSRAM1  -> plays internal flash   */
    FAST   (rwx) : ORIGIN = 0x01000000, LENGTH = 16K       /* blockram -> plays TCM (ITCM/DTCM) */
    RAM    (rwx) : ORIGIN = 0x20000000, LENGTH = 4M - 4K   /* SSRAM2/3 -> plays DTCM/main SRAM  */
    NOINIT (rw)  : ORIGIN = 0x203FF000, LENGTH = 4K        /* fixed-address retained window     */
    PSRAM  (rw)  : ORIGIN = 0x21000000, LENGTH = 16M       /* PSRAM   -> plays AXI/ext big RAM  */
}
```

```text
/* sections_f8.ld — CMSIS-6 style copy/zero tables ({src,dest,wlen} in WORDS) for several regions */
ENTRY(Reset_Handler)
PROVIDE(_Main_Stack_Size = 0x1000);
SECTIONS
{
    .vectors : { KEEP(*(.vectors)) } > FLASH
    .text : {
        *(EXCLUDE_FILE(*hot.o) .text*)          /* everything except hot.o ...            */
        *(EXCLUDE_FILE(*hot.o) .rodata*)
        . = ALIGN(4);
    } > FLASH
    .ARM.exidx : { *(.ARM.exidx*) } > FLASH
    .copy.table : ALIGN(4) {
        __copy_table_start__ = .;
        LONG(LOADADDR(.fast_text)) LONG(ADDR(.fast_text)) LONG(SIZEOF(.fast_text) / 4)
        LONG(LOADADDR(.ram_text))  LONG(ADDR(.ram_text))  LONG(SIZEOF(.ram_text) / 4)
        LONG(LOADADDR(.data))      LONG(ADDR(.data))      LONG(SIZEOF(.data) / 4)
        LONG(LOADADDR(.psram_data)) LONG(ADDR(.psram_data)) LONG(SIZEOF(.psram_data) / 4)
        __copy_table_end__ = .;
    } > FLASH
    .zero.table : ALIGN(4) {
        __zero_table_start__ = .;
        LONG(ADDR(.bss))       LONG(SIZEOF(.bss) / 4)
        LONG(ADDR(.psram_bss)) LONG(SIZEOF(.psram_bss) / 4)
        __zero_table_end__ = .;
    } > FLASH
    .fast_text : ALIGN(4) {                     /* ... hot.o and tagged functions -> FAST */
        *hot.o(.text* .rodata*)
        *(.fast_text*)
        . = ALIGN(4);
    } > FAST AT > FLASH
    .ram_text : ALIGN(4) { *(.ram_text*) . = ALIGN(4); } > RAM AT > FLASH   /* code in SRAM, 512 MB from flash */
    .data : ALIGN(4) { *(.data*) . = ALIGN(4); } > RAM AT > FLASH
    .bss (NOLOAD) : ALIGN(4) { __bss_start__ = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end__ = .; } > RAM
    .stack (NOLOAD) : ALIGN(8) { __StackLimit = .; . += _Main_Stack_Size; __StackTop = .; } > RAM
    .heap (NOLOAD) : ALIGN(8) { __heap_start = .; end = .; } > RAM
    __heap_end = ORIGIN(RAM) + LENGTH(RAM);
    .psram_data : ALIGN(4) { *(.psram_data*) . = ALIGN(4); } > PSRAM AT > FLASH
    .psram_bss (NOLOAD) : ALIGN(32) { *(.psram_bss*) . = ALIGN(4); } > PSRAM
    .fast_bss (NOLOAD) : ALIGN(8) { *(.fast_bss*) . = ALIGN(8); } > FAST           /* stacks/TCBs: not zeroed */
    .psram_noinit (NOLOAD) : ALIGN(8) { *(.psram_noinit*) } > PSRAM               /* heap bank: not zeroed */
    .noinit (NOLOAD) : ALIGN(4) { __noinit_start__ = .; KEEP(*(.noinit*)) . = ALIGN(4); __noinit_end__ = .; } > NOINIT
}
```

이 스크립트에서 객체 종류가 **초기화 방식 세 가지**로 갈린다는 것을 눈여겨본다: copy table에 오른 것(`.fast_text` · `.ram_text` · `.data` · `.psram_data` — 초기값 복사), zero table에 오른 것(`.bss` · `.psram_bss` — 0), 어디에도 안 오른 것(`.fast_bss` · `.psram_noinit` · `.noinit` — 손대지 않음). 스택과 힙 메모리는 0일 필요가 없으므로 세 번째로 보낸다(B5 §8.3의 부팅 시간 교훈).

### 2.3 예제 2 — 객체 하나씩, 다섯 영역에, 실행으로 증명

무엇을 확인하나: 함수 두 개(하나는 attribute, 하나는 **파일 패턴만으로**), `.data` · `.bss` · PSRAM 초기값 테이블 · PSRAM 64 KB 버퍼 · noinit 변수를 각각 다른 영역에 두고, CMSIS 표가 이들을 올바르게 복사 · 0 채움했는지 실행해서 본다.

```c
/* f8.h — placement macros used across the F8 examples */
#ifndef F8_H
#define F8_H
#include <stdint.h>
#include "board.h"
#define FAST_FN     __attribute__((section(".fast_text"), noinline))  /* run from FAST (ITCM role) */
#define PSRAM_BSS   __attribute__((section(".psram_bss"), aligned(32))) /* big zeroed buffer        */
#define PSRAM_DATA  __attribute__((section(".psram_data")))             /* initialised, in PSRAM    */
#define NOINIT      __attribute__((section(".noinit")))                 /* survives warm reset      */
#define CYC         REG32(0x40028018u)                                  /* FPGAIO COUNTER, 25 MHz   */
#define AIRCR       REG32(0xE000ED0Cu)
__attribute__((noreturn)) static inline void sys_reset(void) { __asm volatile("dsb"); AIRCR = 0x05FA0004u; for (;;) { } }
#endif
```

```c
/* hot.c — no attributes at all: the linker script moves this whole file to FAST */
#include <stdint.h>
static const int16_t k_taps[4] = { 3, -1, 7, 2 };     /* .rodata of hot.o -> FAST too */
int32_t fir4(const int16_t *x)
{
    int32_t acc = 0;
    for (int i = 0; i < 4; i++) acc += k_taps[i] * x[i];
    return acc;
}
```

```c
/* ex1 — one object per memory class, CMSIS-6 copy/zero tables, prove placement at run time */
#include "f8.h"
int32_t fir4(const int16_t *x);                         /* from hot.o (moved by file pattern) */
FAST_FN int32_t rate_step(int32_t e) { static int32_t i; i += e; return 3 * e + i / 8; }
int g_mode = 7;                                         /* .data  -> RAM   (copied)  */
uint32_t g_count;                                       /* .bss   -> RAM   (zeroed)  */
PSRAM_DATA uint16_t g_lut[4] = { 10, 20, 30, 40 };      /* .psram_data -> PSRAM (copied) */
PSRAM_BSS uint8_t g_frame[64 * 1024];                   /* .psram_bss  -> PSRAM (zeroed) */
NOINIT uint32_t g_crash_magic;                          /* .noinit -> NOINIT (untouched) */
extern const uint32_t __copy_table_start__[], __copy_table_end__[];
extern const uint32_t __zero_table_start__[], __zero_table_end__[];

int main(void)
{
    static const int16_t x[4] = { 1, 2, 3, 4 };
    board_printf_raw("fir4      @ %x  -> %d\n", (unsigned)fir4, (int)fir4(x));
    board_printf_raw("rate_step @ %x  -> %d\n", (unsigned)rate_step, (int)rate_step(16));
    board_printf_raw("g_mode    @ %x = %d   g_count @ %x = %u\n", (unsigned)&g_mode, g_mode,
                     (unsigned)&g_count, g_count);
    board_printf_raw("g_lut     @ %x = %u,%u   g_frame @ %x [0]=%u [65535]=%u\n", (unsigned)g_lut,
                     g_lut[0], g_lut[3], (unsigned)g_frame, g_frame[0], g_frame[65535]);
    board_printf_raw("g_crash_magic @ %x = %x\n", (unsigned)&g_crash_magic, g_crash_magic);
    for (const uint32_t *c = __copy_table_start__; c < __copy_table_end__; c += 3)
        board_printf_raw("copy src=%x dest=%x wlen=%u\n", c[0], c[1], c[2]);
    for (const uint32_t *z = __zero_table_start__; z < __zero_table_end__; z += 2)
        board_printf_raw("zero dest=%x wlen=%u\n", z[0], z[1]);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/F8 && ./bld.sh ex1 common/f8.ld common/startup_f8.c ex1/main.c ex1/hot.c && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/F8/ex1/app.elf 5'
```

```text
   4448	    172	  69972	  74592	  12360	/var/tmp/rtos/F8/ex1/app.elf
fir4      @ 1000001  -> 30
rate_step @ 1000025  -> 50
g_mode    @ 20000000 = 7   g_count @ 20000068 = 0
g_lut     @ 21000000 = 10,40   g_frame @ 21000020 [0]=0 [65535]=0
g_crash_magic @ 203ff000 = 0
copy src=1160 dest=1000000 wlen=16
copy src=11a0 dest=20000000 wlen=0
copy src=11a0 dest=20000000 wlen=25
copy src=1204 dest=21000000 wlen=2
zero dest=20000064 wlen=84
zero dest=21000020 wlen=16384
[board_exit] code=0
```

출력에서 볼 것:

- `fir4`(속성 없음, 파일 패턴으로만 이동)와 `rate_step`(속성)이 둘 다 FAST(0x0100_0000~)에서 실행되고 맞는 값(3 − 2 + 21 + 8 = 30)을 냈다. 함수 포인터 끝의 1은 Thumb 비트다.
- 표 4줄: `.fast_text` 16워드, `.ram_text` **0워드**(이 예제에는 RAM 코드가 없다 — 빈 섹션은 `wlen=0`으로 남고 루프가 0번 돈다), `.data` 25워드, `.psram_data` 2워드. zero 표: `.bss` 84워드, `.psram_bss` 16384워드(= 64 KB).
- `g_crash_magic`은 고정 주소 0x203F_F000이고 표에 없다 — 아무도 건드리지 않는다.

같은 사실을 ELF에서도 확인한다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/F8/ex1; arm-none-eabi-objdump -h app.elf | awk "/^ *[0-9]+ /{print \$2, \$3, \$4, \$5}" | grep -v "debug\|comment\|ARM.attr"; arm-none-eabi-nm -n -S app.elf | grep -E " (fir4|rate_step|g_mode|g_count|g_lut|g_frame|g_crash_magic)$"; grep -n -A5 "^ \*hot.o" app.map; arm-none-eabi-objdump -d app.elf | grep -E "bl.*<(fir4|rate_step)>"'
```

```text
.vectors 000000c0 00000000 00000000
.text 00001060 000000c0 000000c0
.copy.table 00000030 00001120 00001120
.fast_text 00000040 01000000 00001160
.ram_text 00000000 20000000 20000000
.data 00000064 20000000 000011a0
.psram_data 00000008 21000000 00001204
.zero.table 00000010 00001150 00001150
.bss 00000150 20000064 00001204
.psram_bss 00010000 21000020 00001220
.stack 00001000 200001b8 00001208
.fast_bss 00000000 01000040 01000040
.noinit 00000004 203ff000 203ff000
01000000 00000022 T fir4
01000024 0000001c T rate_step
20000000 00000004 D g_mode
20000068 00000004 B g_count
203ff000 00000004 B g_crash_magic
21000000 00000008 D g_lut
21000020 00010000 B g_frame
301: *hot.o(.text* .rodata*)
302- .text.fir4     0x01000000       0x24 /var/tmp/rtos/F8/ex1/hot.o
303-                0x01000000                fir4
304- *(.fast_text*)
305- .fast_text     0x01000024       0x1c /var/tmp/rtos/F8/ex1/main.o
     19e:	f3ff d72f 	bl	1000000 <fir4>
     1ae:	f3ff d739 	bl	1000024 <rate_step>
```

출력에서 볼 것:

- 열은 이름 · 크기 · VMA · LMA. `.fast_text`는 VMA 0x0100_0000 · LMA 0x1160(플래시), `.data` · `.psram_data`도 LMA가 플래시에 줄줄이 붙는다. NOLOAD 섹션의 LMA 칸 값은 의미가 없다.
- map 파일 301~305줄이 "`*hot.o` 패턴이 `.text.fir4`를 가져갔다"를 증명한다. `k_taps`는 GCC가 상수로 접어 넣어서 `.rodata`가 생기지 않았다.
- `main`(0x19e)에서 `fir4`(0x0100_0000)로는 **그냥 `bl`**이다. 거리가 약 16 MB 안쪽이라 Thumb-2 `BL`(±16 MB)이 닿는다. §3에서 이 경계를 넘으면 무슨 일이 생기는지 본다.

**흔한 함정 (이 절)**

- 섹션 이름을 `.data2`처럼 표준 이름으로 시작하게 지으면 `*(.data*)`에 먹힌다(B5 §2.7 — 에러 없음).
- 표에 줄을 추가하는 것을 잊으면: 출력 섹션은 생기고 map에도 보이지만 **초기값이 복사되지 않는다**. 디버거로 ELF를 로드하면 디버거가 VMA에 직접 써 주므로(B5 §3.2) 디버그 세션에서는 멀쩡하다가 전원을 껐다 켜면 틀린다.
- `SIZEOF/4`는 내림이다. 섹션 끝 `. = ALIGN(4)`를 빼면 마지막 몇 바이트가 빠진다.

---

## 3. 메커니즘 ② — 코드를 RAM에서 부르기: veneer, `long_call`, 함수 포인터

### 3.1 직관 — 점프는 거리 제한이 있다

Thumb-2의 `BL`은 PC 기준 **±16 MB**까지만 간다. H7의 플래시(0x0800_0000)와 ITCM(0x0000_0000)은 128 MB, 플래시와 SRAM(0x2000_0000)은 수백 MB 떨어져 있다. 닿지 않는 호출을 만나면 두 가지 해법이 있다.

| 해법 | 누가 | 생성 코드 | 비용 |
|---|---|---|---|
| **veneer** (기본) | 링커 | 호출자는 그냥 `bl __f_veneer`, veneer = `ldr.w pc, [pc]` + 주소 1워드 | 호출마다 명령 1개 + veneer 페치(플래시에 있음) |
| **`long_call`** | 컴파일러 | `ldr rX, =f` + `blx rX` | 주소 로드 1개 — 루프 밖으로 끌어낼 수 있다 |
| 함수 포인터 | 프로그래머 | `blx rX` | 포인터 로드 |

B5 §9.2와 B8 §4.3에서 veneer가 생기는 것은 봤다. 여기서는 **호출당 비용**을 명령 수로 잰다. ICOUNT 모드에서 FPGAIO 카운터 1 tick = 40 ns, 명령 1개 = 32 ns이므로 `ticks × 40 / 32` = 실행 명령 수다.

### 3.2 예제 3 — 같은 함수, 다섯 가지 호출 경로

무엇을 확인하나: `return x + 1` 함수를 플래시, FAST(16 MB 안쪽), RAM(0x2000_0000, veneer), RAM + `long_call`, 함수 포인터로 각각 1만 번 부르고 명령 수를 센다. `noipa`는 GCC가 함수를 인라인하거나 결과를 미리 계산하지 못하게 한다.

```c
/* ex4 — calling code that lives in RAM: BL in range, linker veneer, long_call, function pointer */
#include "f8.h"
#define RAM_FN   __attribute__((section(".ram_text"), noipa))
#define LONGCALL __attribute__((long_call))
__attribute__((noipa)) uint32_t f_flash(uint32_t x) { return x + 1; }
FAST_FN  __attribute__((noipa)) uint32_t f_fast(uint32_t x) { return x + 1; }   /* 0x0100_0000: BL reaches */
RAM_FN   uint32_t f_ram(uint32_t x)  { return x + 1; }                           /* 0x2000_0000: veneer     */
LONGCALL RAM_FN uint32_t f_ram_lc(uint32_t x);                                  /* same, caller loads addr */
RAM_FN   uint32_t f_ram_lc(uint32_t x) { return x + 1; }
uint32_t (*volatile g_fp)(uint32_t) = f_ram;                                    /* indirect: BLX reg       */
#define N 10000u
#define RUN(name, call) do { uint32_t x = 0, t0 = CYC; for (uint32_t i = 0; i < N; i++) x = call; \
    uint32_t ins = (CYC - t0) * 40u / 32u;  /* ICOUNT: 1 insn = 32 ns, 1 tick = 40 ns */      \
    board_printf_raw("%s: x=%u  %u insn per %u calls\n", name, x, ins, N); } while (0)
int main(void)
{
    board_printf_raw("f_flash=%x f_fast=%x f_ram=%x f_ram_lc=%x\n", (unsigned)f_flash, (unsigned)f_fast,
                     (unsigned)f_ram, (unsigned)f_ram_lc);
    RUN("flash   ", f_flash(x)); RUN("fast(BL)", f_fast(x)); RUN("ram veneer", f_ram(x));
    RUN("ram long_call", f_ram_lc(x)); RUN("func ptr", g_fp(x));
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/F8 && ./bld.sh ex4 common/f8.ld common/startup_f8.c ex4/main.c && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/F8/ex4/app.elf 10'
```

```text
   4396	    164	   4424	   8984	   2318	/var/tmp/rtos/F8/ex4/app.elf
f_flash=199 f_fast=1000001 f_ram=20000001 f_ram_lc=20000005
flash   : x=10000  50003 insn per 10000 calls
fast(BL): x=10000  70003 insn per 10000 calls
ram veneer: x=10000  80003 insn per 10000 calls
ram long_call: x=10000  70003 insn per 10000 calls
func ptr: x=10000  80003 insn per 10000 calls
```

숫자만 보면 "플래시가 제일 빠르다"처럼 보이지만, 루프 코드가 서로 다르다. 그래서 생성 코드를 봤다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/F8/ex4; arm-none-eabi-nm -n app.elf | grep -E " f_(flash|fast|ram|ram_lc)$|veneer"; arm-none-eabi-objdump -d --no-show-raw-insn app.elf | grep -A2 "veneer>:"; arm-none-eabi-objdump -d --no-show-raw-insn app.elf | sed -n "/<main>:/,/^$/p" | grep -E "<f_|veneer|blx|ldr\s+r6"'
```

```text
00000198 T f_flash
00001000 t __f_ram_veneer
01000000 T f_fast
20000000 T f_ram
20000004 T f_ram_lc
00001000 <__f_ram_veneer>:
    1000:	ldr.w	pc, [pc]	@ 1004 <__f_ram_veneer+0x4>
    1004:	.word	0x20000001
     1a0:	ldr	r6, [pc, #264]	@ (2ac <main+0x110>)
     1be:	bl	198 <f_flash>
     1ec:	bl	1000000 <f_fast>
     21c:	bl	1000 <__f_ram_veneer>
     24c:	blx	r6
     274:	ldr	r6, [pc, #96]	@ (2d8 <main+0x13c>)
     27c:	blx	r3
```

출력에서 볼 것:

- **같은 모양의 루프**(`mov r0, rX` → 호출 → `subs` → `mov rX, r0` → `bne`, 5개 + 함수 본체 `adds` · `bx lr` 2개 = 7개)끼리 비교해야 한다. `f_fast`(BL 직접) **7**, `f_ram_lc`(`long_call`) **7**, `f_ram`(veneer) **8** — veneer는 **호출마다 정확히 명령 1개**(`ldr.w pc, [pc]`)를 더한다. 플래시의 5는 GCC가 레지스터 이동 두 개를 없앤 다른 루프라서 나온 숫자다(배치와 무관).
- `long_call`은 함수 주소 로드(`ldr r6, [pc, #264]`, 0x1a0)를 **루프 밖으로 끌어냈다**. 그래서 루프 안 비용이 BL과 같다. veneer는 링커가 만든 것이라 컴파일러가 최적화할 수 없다.
- 함수 포인터(`volatile`)는 매번 포인터를 메모리에서 읽어(`ldr`) 8이다.
- **QEMU가 못 보여 주는 것**: 실칩에서 veneer는 **플래시에 있다**(0x1000). ITCM에 넣은 ISR을 플래시 veneer를 거쳐 부르면 그 명령 페치가 플래시 wait state · I-cache miss를 탄다. 명령 1개의 차이보다 이것이 크다. 그래서 Zephyr의 GCC용 `__ramfunc`는 `long_call`을 붙인다(`include/zephyr/toolchain/gcc.h`: Arm + GCC일 때 `__attribute__((noinline)) __attribute__((long_call, section(".ramfunc")))`, Clang은 `long_call`이 없어 veneer에 맡긴다는 주석과 LLVM 이슈 39969 링크).

### 3.3 직접 해 본 경계 — 16 MB

AN385의 blockram은 0x0100_0000, 플래시 시작에서 **정확히 16 MB**다. 예제 2 · 3에서 `main`(0x19e, 0x1ec)이 FAST로 `bl`을 직접 쓸 수 있었던 것은 오프셋이 16 MB보다 **조금 작기** 때문이다. 다음 예제에서 FAST 안의 함수가 조금만 뒤로 가도(0x0100_0e31) 플래시 앞부분의 호출자에게는 범위를 넘어서 veneer가 생긴다. 거리 계산은 "호출하는 명령 주소 + 4 → 목적지"이고, 함수마다 · 호출 지점마다 다르다. 링커는 범위를 넘는 호출 지점이 하나라도 있으면 veneer를 만든다.

### 3.4 예제 4 — FreeRTOS 커널 핫패스를 TCM으로 (소스 수정 없이)

무엇을 확인하나: FreeRTOS 소스를 한 줄도 고치지 않고 링커 스크립트의 파일 패턴만으로 `tasks.o` · `list.o` · `port.o` · `queue.o`의 코드를 FAST(TCM 역할) 또는 RAM으로 옮긴다. veneer가 몇 개 생기는지, 태스크 두 개가 알림을 주고받는 왕복(컨텍스트 스위치 2번)이 명령 몇 개인지 본다.

링커 스크립트는 `sections_f8.ld`에서 패턴 두 줄만 바꿨다.

```text
/* sections_kfast.ld (diff from sections_f8.ld) */
    .text : {
        *(EXCLUDE_FILE(*tasks.o *list.o *port.o *queue.o) .text*)
        *(EXCLUDE_FILE(*tasks.o *list.o *port.o *queue.o) .rodata*)
        . = ALIGN(4);
    } > FLASH
    .fast_text : ALIGN(4) {
        *tasks.o(.text* .rodata*) *list.o(.text* .rodata*) *port.o(.text* .rodata*) *queue.o(.text* .rodata*)
        *(.fast_text*)
        . = ALIGN(4);
    } > FAST AT > FLASH
/* sections_kram.ld: the same four patterns go into .ram_text (> RAM AT > FLASH) instead */
```

처음에 `*tasks.o *list.o *port.o *queue.o (.text* .rodata*)`처럼 파일 이름을 나열하고 괄호를 한 번만 썼다가 고쳤다. GNU ld 문법에서 **괄호 없는 파일 이름은 "그 파일의 모든 섹션"**이라 `tasks.o`의 `.bss` · `.data`까지 코드 영역으로 끌려온다. 파일마다 `(섹션)`을 붙여야 한다.

```c
/* ex7 — same FreeRTOS app, kernel (tasks/list/port/queue .o) linked into FLASH, TCM, or SRAM */
#include "FreeRTOS.h"
#include "task.h"
#include "f8.h"
#define N 2000u
void xPortPendSVHandler(void);                 /* = PendSV_Handler via FreeRTOSConfig.h */
static TaskHandle_t g_ping, g_pong;
static void pong(void *a) { (void)a; for (;;) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); xTaskNotifyGive(g_ping); } }
static void ping(void *a)
{
    (void)a;
    uint32_t t0 = CYC;
    for (uint32_t i = 0; i < N; i++) { xTaskNotifyGive(g_pong); ulTaskNotifyTake(pdTRUE, portMAX_DELAY); }
    uint32_t ins = (CYC - t0) * 40u / 32u;
    board_printf_raw("xTaskGenericNotify @ %x  PendSV @ %x  round trip = %u insn (2 switches)\n",
                     (unsigned)xTaskGenericNotify, (unsigned)xPortPendSVHandler, ins / N);
    board_exit(0);
}
int main(void)
{
    xTaskCreate(pong, "pong", 256, NULL, 3, &g_pong);
    xTaskCreate(ping, "ping", 256, NULL, 2, &g_ping);
    vTaskStartScheduler();
    return 1;
}
```

`xTaskNotifyGive`는 매크로(→ `xTaskGenericNotify`)라 주소를 찍을 수 없어서 실제 함수 이름을 썼다. 빌드는 커널 파일을 각각 `-c`로 컴파일하는 `fbld.sh`(템플릿 FreeRTOSConfig.h, heap_4)로 했다.

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/F8 && for v in f8 kfast kram; do echo "== $v"; ./fbld.sh ex7$v common/$v.ld heap_4 ex7/main.c && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/F8/ex7$v/app.elf 10; echo "veneers: $(arm-none-eabi-nm /var/tmp/rtos/F8/ex7$v/app.elf | grep -c veneer)"; done'
```

```text
== f8
  12368	    164	 139500	 152032	  251e0	/var/tmp/rtos/F8/ex7f8/app.elf
xTaskGenericNotify @ 1269  PendSV @ 1f95  round trip = 642 insn (2 switches)
[board_exit] code=0
veneers: 0
== kfast
  12576	    164	 139500	 152240	  252b0	/var/tmp/rtos/F8/ex7kfast/app.elf
xTaskGenericNotify @ 1000e31  PendSV @ 100102d  round trip = 648 insn (2 switches)
[board_exit] code=0
veneers: 22
== kram
  12616	    164	 139500	 152280	  252d8	/var/tmp/rtos/F8/ex7kram/app.elf
xTaskGenericNotify @ 20000e31  PendSV @ 2000102d  round trip = 648 insn (2 switches)
[board_exit] code=0
veneers: 27
```

어떤 veneer가 생겼는지도 봤다(kfast, 주소 · 이름).

```text
00001728 __vPortEnterCritical_veneer
00001730 __vListInsert_veneer
00001738 __vTaskStartScheduler_veneer
00001740 __xQueueReceive_veneer
...
00001758 __xTaskGenericNotify_veneer
00001770 __ulTaskGenericNotifyTake_veneer
...
01001788 __board_printf_raw_veneer
01001790 __memcpy_veneer
01001798 __vApplicationStackOverflowHook_veneer
010017a0 __board_exit_veneer
010017a8 __board_now_us_veneer
```

출력에서 볼 것:

- 커널 핫패스(약 6 KB, `.fast_text` 0x17b0바이트)가 FAST/RAM으로 옮겨 갔고 **동작은 똑같다.** PendSV 핸들러 주소도 0x0100_102d — 벡터 테이블은 절대 주소라 veneer 없이 바로 간다.
- veneer는 **양방향**으로 생긴다. 플래시 쪽 0x17xx의 veneer는 앱 · `timers.o`가 커널을 부를 때, FAST 쪽 0x0100_17xx의 veneer는 커널이 플래시의 `memcpy` · `board_now_us`(런타임 통계 시계) · 훅을 부를 때 쓴다. **커널이 부르는 libc · 훅 · 시계 함수도 같이 옮기지 않으면 핫패스가 다시 플래시로 나간다.**
- 왕복 642 → 648 명령: 앱이 커널 API를 부르는 두 곳 × 두 태스크 등에서 veneer 명령이 붙었다. QEMU에서는 그것이 전부지만, 실칩에서는 반대로 커널 본체의 플래시 페치가 사라지는 이득이 이것보다 훨씬 크다(측정 불가 — 정직하게 표시). 실칩에서 이 실험을 하면 DWT CYCCNT로 왕복 사이클의 **평균과 최대**를 같이 본다. TCM의 이득은 최대값(지터)에서 드러난다.

Don 경험과 연결: SSD 펌웨어에서 "스케줄러 · 인터럽트 디스패처는 TCM"으로 두던 것과 같은 판단이다. 차이는 RTOS 커널이 남의 코드라는 것 — 그래서 **소스 대신 링커 스크립트로** 옮기는 기술이 중요하다.

---

## 4. RTOS 객체 배치 — 힙 · 스택 · TCB

### 4.1 FreeRTOS heap_5 — 여러 뱅크에 걸친 힙

F1에서 heap_1~5의 내부 구현을 봤다. 배치 관점에서 heap_5만 다시 본다. V11.3.2 `portable/MemMang/heap_5.c` 머리 주석의 규칙:

- `vPortDefineHeapRegions()`는 **어떤 `pvPortMalloc()`보다도 먼저**(큐 · 세마포어 · 태스크 생성 포함) 불러야 한다.
- `HeapRegion_t { uint8_t *pucStartAddress; size_t xSizeInBytes; }` 배열은 **주소 오름차순**이어야 하고 `{ NULL, 0 }`으로 끝난다.
- 각 영역의 시작은 `portBYTE_ALIGNMENT`(Cortex-M 포트는 8)로 올림 정렬된다.

예제 5 — 무엇을 확인하나: SRAM 32 KB(.bss) + PSRAM 1 MB(0 채우지 않는 `.psram_noinit`) 두 뱅크로 heap_5를 만들고, 할당이 어느 뱅크에서 나오는지 본다. 같이 태스크 스택과 TCB를 `xTaskCreateStatic`으로 FAST(TCM 역할)에 둔다.

```c
/* ex6 — FreeRTOS heap_5 over two banks (SRAM 32 KB + PSRAM 1 MB), static task stacks/TCBs in TCM */
#include "FreeRTOS.h"
#include "task.h"
#include "f8.h"
#define FAST_BSS     __attribute__((section(".fast_bss"), aligned(8)))
#define PSRAM_NOINIT __attribute__((section(".psram_noinit"), aligned(8)))
static uint8_t g_heap_sram[32 * 1024];                 /* bank 1: on-chip SRAM (.bss)      */
PSRAM_NOINIT static uint8_t g_heap_psram[1024 * 1024]; /* bank 2: PSRAM, never zeroed     */
static const HeapRegion_t k_regions[] = {              /* MUST be in ascending address order */
    { g_heap_sram, sizeof g_heap_sram }, { g_heap_psram, sizeof g_heap_psram }, { NULL, 0 } };
FAST_BSS static StackType_t  g_stk[2][256];            /* task stacks in TCM: no cache, no wait */
FAST_BSS static StaticTask_t g_tcb[2];
static const char *bank(void *p)
{
    uintptr_t a = (uintptr_t)p;
    return a >> 24 == 0x01 ? "TCM" : a >> 24 == 0x20 ? "SRAM" : a >> 24 == 0x21 ? "PSRAM" : "?";
}
static void worker(void *arg)
{
    uint32_t id = (uint32_t)arg; volatile uint32_t local = id;
    board_printf_raw("task%u: stack var @ %x (%s)  TCB @ %x (%s)\n", id, (unsigned)&local, bank((void *)&local),
                     (unsigned)&g_tcb[id], bank(&g_tcb[id]));
    if (id == 1) {
        void *p[6]; size_t sz[6] = { 1024, 8192, 16384, 4096, 40960, 512 };
        for (int i = 0; i < 6; i++) {
            p[i] = pvPortMalloc(sz[i]);
            board_printf_raw("malloc(%u) -> %x %s   free=%u\n", (unsigned)sz[i], (unsigned)p[i], bank(p[i]),
                             (unsigned)xPortGetFreeHeapSize());
        }
        HeapStats_t st; vPortGetHeapStats(&st);
        board_printf_raw("largest free block=%u  free blocks=%u  min ever free=%u\n",
                         (unsigned)st.xSizeOfLargestFreeBlockInBytes, (unsigned)st.xNumberOfFreeBlocks,
                         (unsigned)st.xMinimumEverFreeBytesRemaining);
        board_printf_raw("stack high water: task0=%u task1=%u words\n",
                         (unsigned)uxTaskGetStackHighWaterMark(xTaskGetHandle("w0")),
                         (unsigned)uxTaskGetStackHighWaterMark(NULL));
        board_exit(0);
    }
    for (;;) vTaskDelay(10);
}
int main(void)
{
    vPortDefineHeapRegions(k_regions);                 /* before ANY pvPortMalloc (incl. kernel) */
    board_printf_raw("heap_5: %u B total after define\n", (unsigned)xPortGetFreeHeapSize());
    xTaskCreateStatic(worker, "w0", 256, (void *)0, 2, g_stk[0], &g_tcb[0]);
    xTaskCreateStatic(worker, "w1", 256, (void *)1, 1, g_stk[1], &g_tcb[1]);
    vTaskStartScheduler();
    return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/F8 && ./fbld.sh ex6 common/f8.ld heap_5 ex6/main.c && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/F8/ex6/app.elf 10'
```

```text
  12872	    164	1092004	1105040	 10dc90	/var/tmp/rtos/F8/ex6/app.elf
heap_5: 1081328 B total after define
task0: stack var @ 1000458 (TCM)  TCB @ 1000000 (TCM)
task1: stack var @ 1000858 (TCM)  TCB @ 1000060 (TCM)
malloc(1024) -> 20000070 SRAM   free=1080296
malloc(8192) -> 20000478 SRAM   free=1072096
malloc(16384) -> 20002480 SRAM   free=1055704
malloc(4096) -> 20006488 SRAM   free=1051600
malloc(40960) -> 21000008 PSRAM   free=1010632
malloc(512) -> 20007490 SRAM   free=1010112
largest free block=1007600  free blocks=3  min ever free=1010112
stack high water: task0=210 task1=208 words
[board_exit] code=0
```

출력에서 볼 것:

- 두 뱅크가 **한 힙**이 됐다: 32768 + 1048576 = 1081344에서 영역 끝 표식 블록 등 16바이트가 빠진 1081328.
- heap_5의 빈 블록 목록은 **주소 순서**이고 할당은 first-fit이다. 그래서 작은 할당은 낮은 주소인 SRAM에서 먼저 나오고, SRAM에 남은 연속 공간(약 3 KB)보다 큰 40 KB 요청만 PSRAM으로 넘어갔다. 그다음 512바이트는 다시 SRAM이다.
- 이 성질은 우연히 "빠른 뱅크 먼저"처럼 보이지만 **보장이 아니다.** SRAM이 단편화되면 성능이 중요한 버퍼가 PSRAM으로 갈 수 있다. 위치가 중요한 객체는 heap에서 받지 말고 정적 배열로 두거나, 뱅크마다 **따로** 관리되는 할당자(F2의 메모리 풀, Zephyr는 뱅크마다 `k_heap`)를 쓴다.
- 할당 1024바이트에 free가 1032 줄었다 — 블록 헤더 8바이트(F1).
- 태스크 스택과 TCB는 FAST(0x0100_xxxx)에 있다. `.fast_bss`는 NOLOAD이고 zero table에도 없다 — FreeRTOS가 `xTaskCreateStatic`에서 스택을 `tskSTACK_FILL_BYTE`(0xa5)로 칠하고 TCB를 직접 초기화하므로 0 채우기가 필요 없다(high water 210/208워드가 칠한 값으로 계산된 것이다).

### 4.2 정적 할당이 배치의 기본 도구다

동적 할당(`xTaskCreate`)은 스택 위치를 힙에 맡긴다. **위치를 정하고 싶으면 정적 할당**이다. FreeRTOS는 `xTaskCreateStatic`, `xQueueCreateStatic`, `xSemaphoreCreateBinaryStatic` 등으로 버퍼를 넘기고, Zephyr는 `K_THREAD_STACK_DEFINE` · `K_THREAD_DEFINE`이 만드는 스택 배열에 섹션 속성을 붙일 수 있다(스택 영역의 정렬 · MPU 가드 요구는 F3 · F4). 인증 프로젝트의 "동적 할당 금지" 정책(F1)과도 맞는다.

### 4.3 커널이 제공하는 idle · timer 스택은 어디로 가나

`configKERNEL_PROVIDED_STATIC_MEMORY = 1`(랩 기본값)이면 V11.3.2 `tasks.c`가 `vApplicationGetIdleTaskMemory()` · `vApplicationGetTimerTaskMemory()`를 직접 정의하고 그 안의 `static` 배열(`uxIdleTaskStack`, `uxTimerTaskStack`)을 쓴다. 이 배열들은 **그냥 `.bss`**다. §8의 H7 모양 빌드에서 `nm`으로 확인하면 앱 태스크 스택은 DTCM에 있는데 이 두 스택은 `.bss`가 놓인 AXI SRAM에 있다.

```text
00000400 b uxIdleTaskStack.2
00000800 b uxTimerTaskStack.0
```

timer 태스크는 우선순위가 가장 높은(랩 설정 `configMAX_PRIORITIES - 1`) 태스크라 이 스택이 캐시 · 버스 경합을 겪으면 소프트웨어 타이머 콜백의 지터로 나타난다. 고치는 법: `configKERNEL_PROVIDED_STATIC_MEMORY = 0`으로 두고 두 함수를 앱이 정의해 DTCM 섹션의 배열을 넘긴다. 또는 링커 스크립트에서 `*tasks.o(.bss.uxIdleTaskStack*)`처럼 심볼 섹션 이름으로 끌어온다(`-fdata-sections`일 때만 섹션이 따로 생긴다).

### 4.4 ucHeap 하나짜리 heap_4를 원하는 뱅크에

heap_1/2/4는 `ucHeap[configTOTAL_HEAP_SIZE]` 하나다. `configAPPLICATION_ALLOCATED_HEAP = 1`이면 앱이 정의하므로 `__attribute__((section(".axi_noinit")))`를 붙여 원하는 뱅크 · 0 채움 없는 섹션에 둔다. 방법과 부팅 시간 효과는 B5 §8.3에서 쟀다.

---

## 5. noinit RAM — 리셋을 넘어가는 기록

### 5.1 직관 — "startup이 손대지 않는 서랍"

`.noinit`은 특별한 하드웨어가 아니다. **startup의 copy/zero 표에 오르지 않았고, ELF에 내용이 없어서(NOLOAD) 로더도 쓰지 않는 RAM 구간**일 뿐이다. 그래서 그 RAM이 전원을 유지하는 동안의 리셋(소프트웨어 리셋, 워치독, 핀 리셋 대부분)은 넘어가고, 전원이 끊기면 사라진다. E8 §7은 이 구간에 리셋 원인을 남기는 규칙(magic + CRC, 읽고 지우기)을 다뤘다. 여기서는 일반 메커니즘과 크래시 로그 링 버퍼, 그리고 **배치에서 생기는 함정 세 가지**(주소가 움직인다, 부트로더가 덮는다, ECC가 읽기를 막는다)를 실험한다.

| 사건 | noinit 내용 | 비고 |
|---|---|---|
| 소프트웨어 리셋 (`SYSRESETREQ`), 워치독 | 남는다 | 예제 6 |
| 핀 리셋 (NRST) | 대개 남는다 | 칩의 리셋 도메인 설계에 따라 다름 (RM 확인) |
| 브라운아웃 · POR | 쓰레기 (부분적으로 남을 수도) | 그래서 검증이 필수 |
| VBAT 유지 backup SRAM | POR도 넘는다 | H7 backup SRAM 등 (§1.2) |
| 디버거 `load` | 안 쓴다 (NOLOAD = `ALLOC`만, `LOAD` 플래그 없음) | 아래 `objdump -h` |
| 부트로더 실행 | 부트로더가 쓰는 RAM과 겹치면 깨진다 | 예제 8 |
| 새 펌웨어 | 주소가 바뀌면 다른 변수로 읽힌다 | 예제 7 |
| ECC SRAM + POR | 초기화 전 읽기 = ECC 오류 | §6.5 |

디버거 이야기의 근거: 링커가 NOLOAD 섹션에 붙이는 플래그는 `ALLOC`뿐이다. gdb의 `load`는 `LOAD` 플래그가 있는 섹션만 타깃에 쓴다(`.data`처럼 `CONTENTS, ALLOC, LOAD`).

```text
  4 .data         00000060  20000000  00001200  00020000  2**2
                  CONTENTS, ALLOC, LOAD, DATA
  7 .bss          0000014c  20000060  00001260  00020060  2**2
                  ALLOC
 10 .noinit       00000058  203ff000  203ff000  0002f000  2**2
                  ALLOC
```

단, 디버그 프로브 스크립트나 IDE가 "로드 전에 RAM 지우기"를 하는 설정이 있을 수 있다. 도구마다 다르니 크래시 로그를 디버거 세션에서 시험할 때는 리셋만 하고 다시 로드하지 않는다.

### 5.2 예제 6 — 크래시 로그 링 버퍼 (magic + 버전 + 크기 + CRC)

무엇을 확인하나: 고정 크기 링 버퍼를 `.noinit`(고정 주소 NOINIT 영역)에 두고, 부팅마다 검증 → 항목 추가 → `SYSRESETREQ`를 반복한다. 세 번째 리셋 전에 **봉인하지 않은 1비트 변경**을 넣어 CRC가 잡는지 본다. 비교용 `.bss` 카운터도 둔다.

```c
/* ex3 — crash-log ring buffer in .noinit: validate (magic+version+size+CRC), survive SYSRESETREQ */
#include <stddef.h>
#include "f8.h"
#define LOG_MAGIC 0x4C4F4721u                 /* "LOG!" */
#define LOG_VER   1u
#define NENT      4u
typedef struct { uint32_t seq, boot, cause, pc; } entry_t;
typedef struct {
    uint32_t magic; uint16_t version, size;  /* size = sizeof(crashlog_t): layout change detector */
    uint32_t boots, head; entry_t e[NENT];
    uint32_t crc;                            /* CRC-32 of everything above — keep it last */
} crashlog_t;
NOINIT crashlog_t g_log;
NOINIT uint32_t g_step;                      /* test driver state (also .noinit) */
uint32_t g_bss_boots;                        /* ordinary .bss: zeroed every boot */
#ifdef PAD
uint8_t g_pad[PAD];                        /* "next firmware version" grew its .bss */
#endif

static uint32_t crc32(const void *p, size_t n)
{
    const uint8_t *b = p; uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *b++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
    return ~c;
}
static void seal(void) { g_log.crc = crc32(&g_log, offsetof(crashlog_t, crc)); }
static const char *check(void)
{
    if (g_log.magic != LOG_MAGIC) return "bad magic";
    if (g_log.version != LOG_VER || g_log.size != sizeof g_log) return "layout changed";
    if (g_log.crc != crc32(&g_log, offsetof(crashlog_t, crc))) return "bad CRC";
    return 0;
}
static void log_add(uint32_t cause, uint32_t pc)
{
    entry_t *e = &g_log.e[g_log.head % NENT];
    e->seq = g_log.head++; e->boot = g_log.boots; e->cause = cause; e->pc = pc;
    seal();                                  /* reseal after every change */
}
int main(void)
{
    const char *why = check();
#ifdef PAD
    g_pad[0] = (uint8_t)g_step;
#endif
    if (why) {
        if (g_step > 4) g_step = 0;          /* step counter itself must be sane */
        board_printf_raw("[boot] log invalid (%s) -> init empty log\n", why);
        g_log = (crashlog_t){ .magic = LOG_MAGIC, .version = LOG_VER, .size = sizeof g_log };
    }
    g_log.boots++; g_bss_boots++; seal();
    board_printf_raw("[boot] boots=%u (noinit)  g_bss_boots=%u (.bss)  entries=%u\n",
                     g_log.boots, g_bss_boots, g_log.head);
    for (uint32_t i = g_log.head > NENT ? g_log.head - NENT : 0; i < g_log.head; i++)
        board_printf_raw("   #%u boot=%u cause=%u pc=%x\n", g_log.e[i % NENT].seq,
                         g_log.e[i % NENT].boot, g_log.e[i % NENT].cause, g_log.e[i % NENT].pc);
    switch (g_step++) {
    case 0: case 1: log_add(2, 0x1234 + g_step); board_printf_raw("   log + SYSRESETREQ\n"); sys_reset();
    case 2: ((volatile uint8_t *)&g_log.e[0])[5] ^= 0x10;           /* one flipped bit, no reseal */
            board_printf_raw("   flip 1 bit in entry 0 + SYSRESETREQ\n"); sys_reset();
    default: return 0;
    }
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/F8 && ./bld.sh ex3 common/f8.ld common/startup_f8.c ex3/main.c && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/F8/ex3/app.elf 5'
```

```text
   4564	    160	   4516	   9240	   2418	/var/tmp/rtos/F8/ex3/app.elf
[boot] log invalid (bad magic) -> init empty log
[boot] boots=1 (noinit)  g_bss_boots=1 (.bss)  entries=0
   log + SYSRESETREQ
[boot] boots=2 (noinit)  g_bss_boots=1 (.bss)  entries=1
   #0 boot=1 cause=2 pc=1235
   log + SYSRESETREQ
[boot] boots=3 (noinit)  g_bss_boots=1 (.bss)  entries=2
   #0 boot=1 cause=2 pc=1235
   #1 boot=2 cause=2 pc=1236
   flip 1 bit in entry 0 + SYSRESETREQ
[boot] log invalid (bad CRC) -> init empty log
[boot] boots=1 (noinit)  g_bss_boots=1 (.bss)  entries=0
[board_exit] code=0
```

출력에서 볼 것:

- 첫 부팅은 `bad magic` — QEMU를 새로 띄운 RAM은 0이다. 실칩의 POR 직후 RAM은 무작위라 magic이 우연히 맞을 수도 있지만, CRC까지 맞을 확률은 무시할 만하다. 이것이 magic만으로 부족한 이유다(E8 §7.2).
- `boots`(noinit)는 1 → 2 → 3으로 늘고 `g_bss_boots`(.bss)는 매번 1이다. 같은 RAM 칩, 다른 섹션 — 차이는 **표에 올랐느냐**뿐이다.
- 1비트를 봉인 없이 바꾸자 다음 부팅이 `bad CRC`로 거부하고 빈 로그로 다시 시작했다. 실제로 이런 변경은 "로그를 쓰는 도중 리셋"(torn write — E8 §1.5)이나 SRAM 비트 오류에서 온다.
- `version`과 `size` 필드는 **새 펌웨어가 구조체를 바꿨을 때** 옛 기록을 잘못 해석하지 않게 한다(다음 예제의 부트로더 실험이 이 필드에 걸렸다).
- 설계 메모: 이 예제는 로그 전체를 CRC 하나로 봉인해서 1비트 손상에 항목 전부를 버렸다. 실무에서는 **항목마다 CRC**를 두면 손상된 항목만 버리고 나머지를 살린다. 링 버퍼는 `head`가 계속 증가하는 시퀀스 번호를 겸하므로, 다음 부팅의 수집 태스크는 "마지막으로 플래시로 옮긴 seq" 이후 항목만 옮긴 뒤 로그를 비운다.

### 5.3 예제 7 — 주소가 움직이는 noinit vs 고정 주소 영역

무엇을 확인하나: E8 §7.3이 지적한 문제 — 랩 링커 스크립트의 `.noinit`은 `.bss` 바로 뒤에 붙기 때문에 **`.bss`가 커지면 주소가 움직인다.** "새 펌웨어가 `.bss`를 1000바이트 늘렸다"를 `-DPAD=1000`으로 흉내 내고 `g_log` 주소를 비교한다.

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/F8; for pad in "" "-DPAD=1000"; do make -s -B -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/F8/ex3/main.c OUT=/var/tmp/rtos/F8/ex3lab EXTRA_CFLAGS="-I/Users/donh/.rtos_scratch/F8/common $pad" >/dev/null; echo "lab mps2_m3.ld $pad: $(arm-none-eabi-nm /var/tmp/rtos/F8/ex3lab/app.elf | grep -w g_log)"; ./bld.sh ex3pad common/f8.ld common/startup_f8.c ex3/main.c -- $pad >/dev/null; echo "F8 NOINIT region $pad: $(arm-none-eabi-nm /var/tmp/rtos/F8/ex3pad/app.elf | grep -w g_log)"; done'
```

```text
lab mps2_m3.ld : 200001b0 B g_log
F8 NOINIT region : 203ff004 B g_log
lab mps2_m3.ld -DPAD=1000: 20000598 B g_log
F8 NOINIT region -DPAD=1000: 203ff004 B g_log
```

출력에서 볼 것: 랩 스크립트에서는 `g_log`가 0x2000_01b0 → 0x2000_0598로 1000바이트(+정렬) 움직였다. 옛 펌웨어가 남긴 크래시 기록을 새 펌웨어가 읽으려 하면 **엉뚱한 주소의 쓰레기**를 보게 된다(magic이 막아 주긴 하지만 기록은 잃는다). `MEMORY`에 `NOINIT` 영역을 따로 잘라 둔 쪽은 0x203F_F004로 고정이다. 실무 규칙:

- noinit 창은 **자기 MEMORY 영역**으로 만들고, 주소 · 크기를 펌웨어 버전 간 계약으로 문서화한다. 부트로더 링커 스크립트에도 같은 영역을 "쓰지 않음"으로 둔다.
- 한 영역 안에서도 구조체 순서가 바뀌면 같은 문제가 생긴다. 영역 안에 "헤더 + 버전별 구조체"를 두거나, 섹션 이름을 `.noinit.0_hdr` · `.noinit.1_log`처럼 지어 `KEEP(*(SORT(.noinit.*)))`로 순서를 고정한다.
- 이 예제에서 `g_log`가 0x203F_F000이 아니라 0x203F_F004인 것은 같은 영역의 `g_step`이 먼저 놓였기 때문이다 — 순서가 오브젝트 · 선언 순서에 의존한다는 증거다.

### 5.4 예제 8 — 부트로더의 스택이 noinit을 덮는다

무엇을 확인하나: 실제 제품에는 앱 앞에 부트로더(ROM, MCUboot 등 — L1, L2)가 돈다. 부트로더는 **자기 링커 스크립트**로 빌드되고, 흔히 스택을 SRAM 맨 끝에서 시작한다. 앱이 noinit 창을 SRAM 꼭대기에 두면 둘이 겹친다. `early_init()` 훅(앱의 RAM 초기화 전에 도는 약한 함수)에서 MSP를 0x2040_0000으로 바꾸고 스택 4 KB를 쓰는 "가짜 부트로더"를 돌린다.

```c
/* boot_sim.c — pretend a bootloader ran first, with ITS stack at the very top of SRAM (0x20400000) */
#include <stdint.h>
__attribute__((noinline)) void fake_bootloader(void)
{
    volatile uint8_t buf[4080];                     /* image-header parse, hash state, ... */
    for (unsigned i = 0; i < sizeof buf; i++) buf[i] = (uint8_t)(0xB0 + i);
}
__attribute__((naked)) void early_init(void)        /* overrides the weak hook in startup_f8.c */
{
    __asm volatile(
        " mov  r12, sp            \n"
        " ldr  r0, =0x20400000    \n"   /* bootloader's own _estack = end of SRAM */
        " mov  sp, r0             \n"
        " push {r12, lr}          \n"
        " bl   fake_bootloader    \n"
        " pop  {r12, lr}          \n"
        " mov  sp, r12            \n"
        " bx   lr                 \n");
}
```

두 번째 메모리 지도 `mem_f8_lo.ld`는 NOINIT을 SRAM **바닥**(0x2000_0000, 4 KB)으로 옮기고 RAM을 0x2000_1000부터 시작하게 한 것만 다르다.

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/F8 && for v in f8 f8_lo; do echo "##ex3 boot $v"; ./bld.sh ex3$v common/$v.ld common/startup_f8.c ex3/main.c ex3/boot_sim.c >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/F8/ex3$v/app.elf 5; done'
```

```text
##ex3 boot f8
[boot] log invalid (bad magic) -> init empty log
[boot] boots=1 (noinit)  g_bss_boots=1 (.bss)  entries=0
   log + SYSRESETREQ
[boot] log invalid (layout changed) -> init empty log
[boot] boots=1 (noinit)  g_bss_boots=1 (.bss)  entries=0
   log + SYSRESETREQ
[boot] log invalid (layout changed) -> init empty log
[boot] boots=1 (noinit)  g_bss_boots=1 (.bss)  entries=0
   flip 1 bit in entry 0 + SYSRESETREQ
[boot] log invalid (layout changed) -> init empty log
[boot] boots=1 (noinit)  g_bss_boots=1 (.bss)  entries=0
[board_exit] code=0
##ex3 boot f8_lo
[boot] log invalid (bad magic) -> init empty log
[boot] boots=1 (noinit)  g_bss_boots=1 (.bss)  entries=0
   log + SYSRESETREQ
[boot] boots=2 (noinit)  g_bss_boots=1 (.bss)  entries=1
   #0 boot=1 cause=2 pc=1235
   log + SYSRESETREQ
[boot] boots=3 (noinit)  g_bss_boots=1 (.bss)  entries=2
   #0 boot=1 cause=2 pc=1235
   #1 boot=2 cause=2 pc=1236
   flip 1 bit in entry 0 + SYSRESETREQ
[boot] log invalid (bad CRC) -> init empty log
[boot] boots=1 (noinit)  g_bss_boots=1 (.bss)  entries=0
[board_exit] code=0
```

출력에서 볼 것:

- 꼭대기 배치(`f8.ld`): 매 부팅이 첫 부팅처럼 보인다. 거부 사유가 `bad magic`이 아니라 **`layout changed`** — 부트로더 스택의 바닥이 `g_log`의 앞 4바이트(magic)는 건드리지 않고 `version`/`size` 필드부터 덮었다. 손상이 **부분적**이라는 점이 중요하다. magic만 검사했다면 덮어쓴 쓰레기를 기록으로 믿었을 것이다.
- 바닥 배치(`f8_lo.ld`): 부트로더가 같은 일을 해도 기록이 산다. 예제 6과 똑같은 출력이다.
- 처음에는 버퍼를 2 KB로 했다가 겹치지 않아서 4080바이트로 늘렸다. 실제 겹침 여부는 **부트로더의 최대 스택 깊이**에 달렸고, 부트로더 버전이 바뀌면 달라진다. 그래서 "운 좋게 안 겹친다"가 아니라 **두 링커 스크립트가 같은 noinit 창을 예약**해야 한다. MCUboot처럼 부트로더 소스가 있으면 그쪽 링커 스크립트에서 RAM 길이를 줄이고, ROM 부트로더면 RM의 "부트 ROM이 쓰는 RAM" 표를 본다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<text x="170" y="22" font-size="13" text-anchor="middle" font-weight="bold">NOINIT = SRAM 꼭대기 (f8.ld)</text>
<text x="510" y="22" font-size="13" text-anchor="middle" font-weight="bold">NOINIT = SRAM 바닥 (f8_lo.ld)</text>
<rect x="40" y="40" width="260" height="36" fill="#888" fill-opacity="0.15" stroke="#888"/>
<text x="50" y="63" font-size="12">NOINIT 0x203F_F000</text>
<rect x="40" y="76" width="260" height="50" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="50" y="105" font-size="12">app MSP 스택</text>
<rect x="40" y="126" width="260" height="100" fill="none" stroke="currentColor" stroke-width="0.8"/>
<text x="170" y="180" font-size="12" text-anchor="middle">빈 공간 · heap</text>
<rect x="40" y="226" width="260" height="50" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="50" y="255" font-size="12">.data · .bss (0x2000_0000~)</text>
<rect x="226" y="40" width="74" height="52" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="263" y="62" font-size="12" text-anchor="middle">부트로더</text>
<text x="263" y="80" font-size="12" text-anchor="middle">스택 4 KB</text>
<text x="170" y="300" font-size="12" text-anchor="middle">겹침 → 매 부팅 "layout changed"</text>
<rect x="380" y="40" width="260" height="52" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="390" y="70" font-size="12">app MSP 스택</text>
<rect x="566" y="40" width="74" height="52" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="603" y="62" font-size="12" text-anchor="middle">부트로더</text>
<text x="603" y="80" font-size="12" text-anchor="middle">스택 4 KB</text>
<rect x="380" y="92" width="260" height="134" fill="none" stroke="currentColor" stroke-width="0.8"/>
<text x="510" y="164" font-size="12" text-anchor="middle">빈 공간 · heap</text>
<rect x="380" y="226" width="260" height="36" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="390" y="249" font-size="12">.data · .bss (0x2000_1000~)</text>
<rect x="380" y="262" width="260" height="20" fill="#888" fill-opacity="0.15" stroke="#888"/>
<text x="390" y="277" font-size="12">NOINIT 0x2000_0000</text>
<text x="510" y="300" font-size="12" text-anchor="middle">안 겹침 → 기록 생존 (boots 1, 2, 3)</text>
<text x="340" y="328" font-size="12" text-anchor="middle">위쪽 = 높은 주소. 부트로더는 자기 _estack(SRAM 끝)에서 스택을 시작한다 (예제 8)</text>
</svg>
```

그림 3 — 예제 8의 두 배치. 왼쪽은 부트로더 스택(빨간 점선)이 noinit 창과 겹쳐 기록이 매번 깨지고, 오른쪽은 noinit 창을 SRAM 바닥으로 옮겨 살아남는다. 앱의 스택은 앱이 시작한 뒤에 쓰이므로 부트로더 스택과 겹쳐도 상관없다.

### 5.5 RTOS에서 noinit에 무엇을 남기나

드론 FC라면 HardFault 핸들러(M1)가 스택 프레임의 PC · LR · CFSR · 현재 태스크 이름(`pcTaskGetName(NULL)` 또는 Zephyr `k_thread_name_get(k_current_get())`)을 링 버퍼 항목에 쓰고 봉인한 뒤 리셋한다. 다음 부팅에서 낮은 우선순위의 수집 태스크가 항목을 플래시 로그나 텔레메트리로 보내고 지운다. 리셋 직전 경로에서는 **noinit 쓰기만** 한다 — 플래시 쓰기, `printf`, 커널 API는 쓰지 않는다(E8 §7.2). Zephyr는 이 역할의 추상화로 `retained_mem` 드라이버와 `subsys/retention`(E8에서 언급)을 준다. 리셋 원인과의 결합은 E8, 덤프 내용의 해석은 M5다.

---

## 6. ECC RAM 초기화 — 읽기 전에 써라

> 🔁 이 절은 Bare-Metal BM Q7(메모리 오류 · ECC)의 SRAM ECC 초기화 부분과 겹친다 (주인: 이 노트).

### 6.1 직관 — ECC 비트도 쓰레기로 시작한다

ECC SRAM은 데이터 워드(granule — 32비트 또는 64비트)마다 검사 비트를 같이 저장한다. 쓸 때 하드웨어가 검사 비트를 계산해 넣고, 읽을 때 다시 계산해 비교한다. 전원을 켠 직후에는 **데이터도 검사 비트도 쓰레기**다. 그 상태에서 읽으면 대부분 "이중 비트 오류"로 판정되어 bus fault나 ECC 오류 인터럽트가 난다. 더 나쁜 것은 일부가 "단일 비트 오류"로 판정되어 하드웨어가 **틀린 값으로 고쳐서** 돌려준다는 점이다.

여기에 숨은 함정이 하나 더 있다. **granule보다 작은 쓰기(STRB, STRH, 32비트 granule에 대한 정렬 안 된 쓰기, 64비트 granule에 대한 32비트 쓰기)는 하드웨어 안에서 읽기 → 병합 → 쓰기(read-modify-write)** 가 된다. 새 검사 비트를 계산하려면 나머지 바이트가 필요하기 때문이다. 즉 "아직 안 읽었으니 괜찮다"가 아니다. 초기화 전의 `strb`도 읽기를 일으킨다. Zephyr의 H7 코드 주석이 "reads, unaligned or sub-sized accesses will trigger an ECC error"라고 쓴 이유다.

### 6.2 예제 9 — SECDED(39,32) 모델: 초기화 전 읽기와 STRB가 어떻게 되나

QEMU는 ECC를 모델링하지 않으므로, VM 호스트에서 실제 SECDED 부호(32비트 데이터 + Hamming 6비트 + 전체 패리티 1비트 = 39비트)로 SRAM 한 워드를 흉내 낸다. 무엇을 확인하나: 전원 투입 쓰레기 100만 워드에 대해 (A) 32비트 읽기, (B) STRB(=RMW), (C) 워드 단위로 먼저 지운 뒤 STRB의 결과 분포.

```c
/* ecc_model.c — SECDED(39,32) SRAM model: what happens when you READ (or STRB) before initialising */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static uint64_t enc(uint32_t d)                 /* Hamming: parity at 1,2,4,8,16,32 + overall bit 0 */
{
    uint64_t c = 0; int k = 0;
    for (int p = 1; p <= 38; p++) if (p & (p - 1)) { if ((d >> k++) & 1) c |= 1ull << p; }
    for (int s = 1; s <= 32; s <<= 1) { int par = 0; for (int p = 1; p <= 38; p++) if ((p & s) && ((c >> p) & 1)) par ^= 1; if (par) c |= 1ull << s; }
    if (__builtin_parityll(c)) c |= 1;          /* overall parity -> even */
    return c;
}
static uint32_t data_of(uint64_t c) { uint32_t d = 0; int k = 0; for (int p = 1; p <= 38; p++) if (p & (p - 1)) d |= (uint32_t)((c >> p) & 1) << k++; return d; }
enum { CLEAN, CORRECTED, UNCORR };
static int dec(uint64_t *c)                     /* returns status, fixes *c on single error */
{
    int syn = 0; for (int p = 1; p <= 38; p++) if ((*c >> p) & 1) syn ^= p;
    int odd = __builtin_parityll(*c);
    if (!syn && !odd) return CLEAN;
    if (odd) { if (syn <= 38) { *c ^= 1ull << syn; return CORRECTED; } return UNCORR; }
    return UNCORR;                              /* syndrome != 0, parity even -> double error */
}
static uint64_t rnd39(void) { return (((uint64_t)rand() << 31) ^ (uint64_t)rand() ^ ((uint64_t)rand() << 20)) & ((1ull << 39) - 1); }
#define N 1000000
int main(void)
{
    srand(42); int cnt[3] = { 0 }, wrong = 0;
    for (int i = 0; i < N; i++) {               /* A) power-on garbage, then a 32-bit READ */
        uint64_t c = rnd39(); int st = dec(&c); cnt[st]++;
    }
    printf("A) read of uninitialised word : clean %.2f%%  'corrected' %.2f%%  uncorrectable %.2f%%\n",
           100.0 * cnt[0] / N, 100.0 * cnt[1] / N, 100.0 * cnt[2] / N);
    cnt[0] = cnt[1] = cnt[2] = 0;
    for (int i = 0; i < N; i++) {               /* B) STRB 0x5A into byte 0 = read-modify-write */
        uint64_t c = rnd39(); int st = dec(&c); cnt[st]++;
        if (st == UNCORR) continue;             /* bus fault / ECC error IRQ on a STORE */
        c = enc((data_of(c) & ~0xFFu) | 0x5A);  /* merged word written back with fresh ECC */
        if (dec(&c) == CLEAN && (data_of(c) & 0xFFu) == 0x5A) wrong += (data_of(c) >> 8) != 0;
    }
    printf("B) STRB into uninitialised word: faults %.2f%%  silently kept garbage in other 3 bytes %.2f%%\n",
           100.0 * cnt[2] / N, 100.0 * wrong / N);
    cnt[0] = cnt[1] = cnt[2] = 0;
    for (int i = 0; i < N; i++) {               /* C) word-scrubbed first (STR 0), then STRB, then read */
        uint64_t c = enc(0); dec(&c); c = enc((data_of(c) & ~0xFFu) | 0x5A); cnt[dec(&c)]++;
    }
    printf("C) scrub (32-bit STR) then STRB: clean %.2f%%\n", 100.0 * cnt[0] / N);
    uint64_t c = enc(0xDEADBEEF) ^ (1ull << 17); int s1 = dec(&c);
    uint64_t d = enc(0xDEADBEEF) ^ (1ull << 17) ^ (1ull << 30); int s2 = dec(&d);
    printf("check: 1-bit flip -> %s (data %08X), 2-bit flip -> %s\n", s1 == CORRECTED ? "corrected" : "?",
           data_of(c), s2 == UNCORR ? "uncorrectable" : "?");
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/rtos/F8/ex5 && gcc -O2 -Wall -Wextra -o /var/tmp/rtos/F8/ex5/ecc /Users/donh/.rtos_scratch/F8/ex5/ecc_model.c && /var/tmp/rtos/F8/ex5/ecc'
```

```text
A) read of uninitialised word : clean 0.79%  'corrected' 30.45%  uncorrectable 68.76%
B) STRB into uninitialised word: faults 68.78%  silently kept garbage in other 3 bytes 31.22%
C) scrub (32-bit STR) then STRB: clean 100.00%
check: 1-bit flip -> corrected (data DEADBEEF), 2-bit flip -> uncorrectable
```

출력에서 볼 것:

- 무작위 39비트 중 올바른 부호어는 2^32/2^39 = 1/128 = 0.78%, 한 비트 거리(39개 위치)는 39/128 = 30.5%, 나머지 68.75%는 정정 불가다. 시뮬레이션이 이론값과 맞는다. 즉 **초기화 안 한 ECC 워드를 읽으면 약 2/3는 fault, 약 1/3은 조용히 틀린 값**이다.
- STRB(B)도 같은 비율로 fault가 나고, fault가 안 난 경우에도 나머지 3바이트는 쓰레기로 "확정"된다(새 ECC가 쓰레기를 정상 데이터로 봉인한다).
- 워드 단위로 먼저 한 번 쓰면(C) 이후 바이트 쓰기 · 읽기는 모두 정상이다. 이것이 "scrub(초기화) 먼저"의 이유다.
- 실칩에서 정확히 어떤 비율이 나오는지는 칩의 부호(예: 64비트 granule이면 SECDED(72,64))와 전원 투입 SRAM 패턴에 달렸다. 결론(초기화 전 읽기 금지)은 같다.

```svg
<svg viewBox="0 0 680 180" xmlns="http://www.w3.org/2000/svg">
<text x="190" y="39" font-size="13" text-anchor="end">A) 초기화 전 32비트 읽기</text>
<rect x="200.0" y="24" width="3.5" height="22" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<rect x="203.5" y="24" width="134.0" height="22" fill="#e08a3c" fill-opacity="0.45" stroke="#e08a3c"/>
<text x="270.5" y="40" font-size="12" text-anchor="middle">30.4%</text>
<rect x="337.5" y="24" width="302.5" height="22" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="488.7" y="40" font-size="12" text-anchor="middle">68.8%</text>
<text x="190" y="83" font-size="13" text-anchor="end">B) 초기화 전 STRB (RMW)</text>
<rect x="200.0" y="68" width="137.4" height="22" fill="#e08a3c" fill-opacity="0.45" stroke="#e08a3c"/>
<text x="268.7" y="84" font-size="12" text-anchor="middle">31.2%</text>
<rect x="337.4" y="68" width="302.6" height="22" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/>
<text x="488.7" y="84" font-size="12" text-anchor="middle">68.8%</text>
<text x="190" y="127" font-size="13" text-anchor="end">C) 워드 scrub 후 STRB</text>
<rect x="200.0" y="112" width="440.0" height="22" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/>
<text x="420.0" y="128" font-size="12" text-anchor="middle">100.0%</text>
<rect x="60" y="154" width="14" height="12" fill="#3f9a6b" fill-opacity="0.45" stroke="#3f9a6b"/><text x="80" y="165" font-size="12">정상 읽기</text>
<rect x="190" y="154" width="14" height="12" fill="#e08a3c" fill-opacity="0.45" stroke="#e08a3c"/><text x="210" y="165" font-size="12">조용히 틀린 데이터 (single-bit "정정")</text>
<rect x="470" y="154" width="14" height="12" fill="#d0564a" fill-opacity="0.45" stroke="#d0564a"/><text x="490" y="165" font-size="12">ECC fault (double-bit)</text>
</svg>
```

그림 4 — 예제 9의 SECDED(39,32) 모델 결과(100만 워드, VM 호스트 C — 실칩 측정이 아니다). 초기화 전 읽기와 STRB는 3분의 2가 fault, 나머지 대부분은 조용히 틀린 데이터다. 워드 단위 scrub 뒤에는 100% 정상.

### 6.3 규칙 — ECC RAM startup 체크리스트

1. **스택보다 먼저.** 초기 MSP가 ECC RAM을 가리키면 첫 `push`(C 함수 프롤로그, 예외 스태킹)가 RMW · 쓰기 없는 읽기를 일으킬 수 있다. scrub 루프는 **스택을 쓰지 않는 어셈블리**(레지스터만)로 쓰거나, 스택을 이미 초기화된(또는 ECC 없는) 다른 RAM에 두고 C로 쓴다. Zephyr H7의 `soc_reset_hook()`이 C인 것은 그 시점 MSP(`z_main_stack`)가 TCM이 아닌 `zephyr,sram`(nucleo_h743zi에서는 `sram0` = AXI SRAM)에 있기 때문이다.
2. **granule 폭 이상, granule 정렬로 쓴다.** 32비트 granule이면 `STR`/`STM`, 64비트면 `STRD`/`STM`(짝수 개). H7 ITCM 64비트, DTCM 32비트(AN5342 인용, §1.2).
3. **캐시를 켜기 전에.** 캐시 가능한 영역을 write-allocate 캐시를 켠 채 부분적으로 쓰면 캐시가 라인 전체를 먼저 **읽어 온다**(line fill). 그 읽기가 ECC 오류를 낸다. scrub은 D-cache를 켜기 전에 하거나, 캐시 라인 단위(32 B)로 정렬해 통째로 쓴다(B8 §2.3 순서와 함께).
4. **`.data` 복사 · `.bss` 0 채우기보다 먼저.** `memcpy`의 꼬리 바이트 처리(`strb`)가 RMW를 만든다. scrub이 RAM 전체를 0으로 만들었다면 `.bss` zero table은 **중복이므로 빼도 된다**(부팅 시간 절약).
5. **noinit 창은 POR일 때만 scrub한다**(§6.5).
6. **DMA로 해도 된다.** DMA 엔진은 CPU보다 넓은 버스 쓰기를 하므로 빠를 수 있고, 그동안 CPU는 다른 초기화를 한다. 단 DMA 디스크립터 · 소스 버퍼가 아직 초기화 안 된 ECC RAM에 있으면 안 된다. QEMU AN385에는 DMA 컨트롤러가 없어 이 노트에서는 시험하지 못했다.

### 6.4 예제 10 — 어셈블리 Reset_Handler: 스택 전에 scrub, warm reset이면 noinit 건너뛰기

무엇을 확인하나: 위 규칙 1 · 2 · 4 · 5를 지키는 startup. 레지스터 8개를 0으로 두고 `stmia`로 32바이트씩 RAM 전체를 쓴 뒤에야 C로 넘어간다. QEMU AN385에는 리셋 원인 레지스터가 없으므로(E8 §7.1) blockram의 한 워드(`FAKE_RSR`)를 STM32의 `RCC_RSR` 역할로 쓴다 — 앱이 소프트웨어 리셋 전에 표시를 남긴다(흉내임을 밝혀 둔다).

```asm
/* startup_ecc.S — Reset_Handler for ECC SRAM: write every word BEFORE anything reads it,
 * including the stack. No C, no push, until the scrub is done. */
    .syntax unified
    .thumb
    .equ  FAKE_RSR, 0x01003FFC      /* stands in for RCC_RSR & co.: "was this a power-on reset?" */
    .equ  SFTRST,   0x5F7           /* app writes this before SYSRESETREQ (QEMU has no RSR)     */
    .equ  FPGA_CYC, 0x40028018
    .text
    .global Reset_Handler
    .type Reset_Handler, %function
    .thumb_func
Reset_Handler:
    ldr   r10, =FPGA_CYC
    ldr   r11, [r10]                /* t0 (register only: no RAM yet) */
    movs  r2, #0
    movs  r3, #0
    movs  r4, #0
    movs  r5, #0
    movs  r6, #0
    movs  r7, #0
    movs  r8, #0
    movs  r9, #0
    ldr   r0, =ORIGIN_RAM           /* 1) main SRAM incl. stack, .data, .bss, heap */
    ldr   r1, =END_RAM
1:  stmia r0!, {r2-r9}              /* 32 B per store, aligned: no read-modify-write */
    cmp   r0, r1
    bne   1b
    ldr   r12, =FAKE_RSR            /* 2) NOINIT only on power-on: warm reset keeps the log */
    ldr   r12, [r12]
    ldr   r1, =SFTRST
    cmp   r12, r1
    beq   2f
    ldr   r0, =ORIGIN_NOINIT
    ldr   r1, =END_NOINIT
3:  stmia r0!, {r2-r9}
    cmp   r0, r1
    bne   3b
2:  ldr   r12, [r10]                /* t1 */
    ldr   r0, =g_scrub_ticks        /* RAM is now safe to touch */
    sub   r1, r12, r11
    str   r1, [r0]
    b     c_start                   /* -> __cmsis_start(): copy/zero tables -> _start -> main */
```

링커 스크립트는 메모리 지도와 섹션 사이에 심볼 네 개만 끼웠다(`f8_ecc.ld`).

```text
INCLUDE mem_f8.ld
ORIGIN_RAM = ORIGIN(RAM);       END_RAM = ORIGIN(RAM) + LENGTH(RAM);
ORIGIN_NOINIT = ORIGIN(NOINIT); END_NOINIT = ORIGIN(NOINIT) + LENGTH(NOINIT);
INCLUDE sections_f8.ld
```

```c
/* ex2b — app side of startup_ecc.S: scrub time, .bss zero, NOINIT kept on warm reset only */
#include "f8.h"
#define FAKE_RSR REG32(0x01003FFCu)
uint32_t __attribute__((section(".noinit"))) g_scrub_ticks;  /* asm writes it; .bss would be re-zeroed */
uint32_t __attribute__((section(".noinit"))) g_warm;
int main(void)
{
    board_printf_raw("scrub %u KB: %u ticks = %u us   RSR=%x  g_warm=%u\n",
                     (unsigned)((0x203FF000u - 0x20000000u) / 1024u), g_scrub_ticks,
                     g_scrub_ticks / 25u, FAKE_RSR, g_warm);
    if (g_warm++ < 2) { FAKE_RSR = 0x5F7u; sys_reset(); }
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/F8 && ./bld.sh ex2b common/f8_ecc.ld common/startup_f8.c ex2/startup_ecc.S ex2/main_ecc.c && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/F8/ex2b/app.elf 10'
```

```text
   4152	    160	   4432	   8744	   2228	/var/tmp/rtos/F8/ex2b/app.elf
scrub 4092 KB: 314587 ticks = 12583 us   RSR=0  g_warm=0
scrub 4092 KB: 314279 ticks = 12571 us   RSR=5f7  g_warm=1
scrub 4092 KB: 314279 ticks = 12571 us   RSR=5f7  g_warm=2
[board_exit] code=0
```

출력에서 볼 것:

- 4092 KB를 `stmia` 8레지스터로 지우는 데 12.57 ms(QEMU icount). 계산: 4092 × 1024 / 32 = 130944회 × 루프 명령 3개 × 32 ns = 12.57 ms. 실칩 Cortex-M3라면 `STM` 8워드 = 1 + 8 사이클(TRM)이라 루프 1회 약 12사이클 → 25 MHz에서 약 63 ms다. 큰 SRAM에서 부팅 시간의 큰 몫이 scrub이다.
- 첫 부팅(RSR=0, "POR")은 NOINIT 4 KB도 지워서 308 tick(12.3 µs) 더 걸렸다. 리셋 뒤(RSR=0x5F7)에는 NOINIT을 건너뛰었고 `g_warm`이 0 → 1 → 2로 살아남았다.
- 처음 버전은 `g_scrub_ticks`를 일반 `.bss`에 뒀다가 0이 찍혔다. 어셈블리가 값을 쓴 **뒤에** CMSIS zero table이 `.bss`를 다시 지웠기 때문이다. "startup의 어느 단계가 어느 영역을 쓰는가"를 순서대로 그려 보지 않으면 이런 일이 생긴다 — 그래서 측정값을 noinit에 뒀다.

### 6.5 noinit과 ECC의 충돌 — POR이면 지우고, warm이면 남긴다

§5의 크래시 로그는 "아무도 쓰지 않는 RAM"이어야 하고, §6.3의 규칙은 "읽기 전에 써라"다. ECC RAM에서는 둘이 충돌한다. POR 직후 noinit 창을 검증하려고 **읽는 순간** ECC fault가 난다. 해법은 예제 10의 분기다.

- **리셋 원인 레지스터를 먼저 읽는다**(레지스터라 ECC와 무관). POR · BOR이면 noinit 창도 scrub한다(어차피 내용은 쓰레기다). 소프트웨어 · 워치독 · 핀 리셋이면 noinit 창은 건너뛴다 — 그 RAM은 전원이 유지됐으므로 ECC 비트도 유효하다.
- 리셋 원인을 지우는 것은 그 뒤다(E8 §7.1의 "읽고, 복사하고, 지운다").
- 일부 칩은 POR 때 하드웨어가 SRAM을 지우거나 ECC를 초기화해 준다. 이 노트에서 칩별로 확인하지 못했으므로, 쓰는 칩의 RM에서 "SRAM erase on reset" · "ECC initialization" 항목을 찾아본다.

### 6.6 예제 11 — 어떻게 지우나: 방식별 시간

무엇을 확인하나: 같은 영역을 지우는 다섯 가지 방법(`strb` 1바이트, newlib-nano `memset`, `str` 4바이트, `strd` 8바이트, `stm` 32바이트)의 시간을 1 ~ 256 KB에서 잰다. scrub 루틴은 스택을 쓰지 않는 leaf 함수다(측정 하네스용 `stm8`만 r4~r9를 보존하려고 push한다).

```asm
/* fill.S — five ways to write zeros over [r0, r1). Leaf routines, NO stack use
 * (on ECC RAM the stack itself is not initialised yet). r0,r1 must be 32-byte aligned. */
    .syntax unified
    .thumb
    .text
    .macro FN name
    .global \name
    .type \name, %function
    .thumb_func
\name:
    .endm
FN fill_strb                  /* 1 byte per store: on ECC RAM each one is a read-modify-write */
    movs r2, #0
1:  strb r2, [r0], #1
    cmp  r0, r1
    bne  1b
    bx   lr
FN fill_str                   /* 1 word per store */
    movs r2, #0
1:  str  r2, [r0], #4
    cmp  r0, r1
    bne  1b
    bx   lr
FN fill_strd                  /* 2 words per store (64-bit: matches ECC granule on M7/R5 TCM) */
    movs r2, #0
    movs r3, #0
1:  strd r2, r3, [r0], #8
    cmp  r0, r1
    bne  1b
    bx   lr
FN fill_stm8                  /* 8 words per store: 32 B = one Cortex-M7 cache line */
    push {r4-r9}              /* only here, for the sweep harness; startup version has no push */
    movs r2, #0
    movs r3, #0
    movs r4, #0
    movs r5, #0
    movs r6, #0
    movs r7, #0
    movs r8, #0
    movs r9, #0
1:  stmia r0!, {r2-r9}
    cmp  r0, r1
    bne  1b
    pop  {r4-r9}
    bx   lr
```

```c
/* ex2 — ECC-style RAM init: time five fill methods over 1..256 KB (ICOUNT, FPGAIO counter 25 MHz) */
#include <string.h>
#include "f8.h"
void fill_strb(void *s, void *e); void fill_str(void *s, void *e);
void fill_strd(void *s, void *e); void fill_stm8(void *s, void *e);
static void fill_memset(void *s, void *e) { memset(s, 0, (char *)e - (char *)s); }
PSRAM_BSS static uint8_t g_area[256 * 1024];
static const struct { const char *n; void (*f)(void *, void *); } k_m[] = {
    { "strb", fill_strb }, { "memset", fill_memset }, { "str", fill_str },
    { "strd", fill_strd }, { "stm8", fill_stm8 } };
int main(void)
{
    board_printf_raw("method,KB,ticks40ns\n");
    for (unsigned m = 0; m < sizeof k_m / sizeof k_m[0]; m++)
        for (unsigned kb = 1; kb <= 256; kb *= 4) {
            uint8_t *s = g_area, *e = g_area + kb * 1024;
            memset(g_area, 0xAA, kb * 1024);
            uint32_t t0 = CYC; k_m[m].f(s, e); uint32_t t1 = CYC;
            for (uint8_t *p = s; p < e; p++) if (*p) { board_printf_raw("BAD %s\n", k_m[m].n); return 1; }
            board_printf_raw("%s,%u,%u\n", k_m[m].n, kb, t1 - t0);
        }
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/F8 && ./bld.sh ex2 common/f8.ld common/startup_f8.c ex2/main.c ex2/fill.S >/dev/null && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/F8/ex2/app.elf 20'
```

```text
method,KB,ticks40ns
strb,1,2465
strb,4,9838
strb,16,39329
strb,64,157293
strb,256,629153
memset,1,284
memset,4,1052
memset,16,4124
memset,64,16412
memset,256,65564
str,1,622
str,4,2465
str,16,9837
str,64,39329
str,256,157293
strd,1,315
strd,4,1237
strd,16,4923
strd,64,19669
strd,256,78651
stm8,1,91
stm8,4,322
stm8,16,1243
stm8,64,4930
stm8,256,19675
[board_exit] code=0
```

같은 빌드를 두 번 돌리면 ±1 tick이 다르다(40 ns 카운터와 32 ns 명령 시계의 위상 차이). 모든 방식이 크기에 정확히 비례한다. 256 KB 값을 실칩 모델과 같이 그렸다. 모델은 Cortex-M3 TRM의 명령 사이클 표를 근사한 것이다(0 wait SRAM, STR 2 · STRD 3 · STM 1+N · CMP 1 · 분기 taken 2 사이클 가정, 루프 1회 = strb 5, str 5, strd 6, stm8 12 사이클). **모델이지 측정이 아니다.**

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="20" font-size="13" text-anchor="middle" font-weight="bold">256 KB를 0으로 채우는 시간 (ms, 로그 눈금)</text>
<text x="140" y="62" font-size="13" text-anchor="end">strb</text>
<rect x="150" y="44" width="376.1" height="16" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="532.1" y="57" font-size="12">25.17</text>
<rect x="150" y="62" width="426.1" height="16" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="582.1" y="75" font-size="12">52.43</text>
<text x="140" y="108" font-size="13" text-anchor="end">memset</text>
<rect x="150" y="90" width="222.3" height="16" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="378.3" y="103" font-size="12">2.62</text>
<text x="140" y="154" font-size="13" text-anchor="end">str</text>
<rect x="150" y="136" width="281.8" height="16" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="437.8" y="149" font-size="12">6.29</text>
<rect x="150" y="154" width="331.7" height="16" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="487.7" y="167" font-size="12">13.11</text>
<text x="140" y="200" font-size="13" text-anchor="end">strd</text>
<rect x="150" y="182" width="234.6" height="16" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="390.6" y="195" font-size="12">3.15</text>
<rect x="150" y="200" width="297.0" height="16" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="453.0" y="213" font-size="12">7.86</text>
<text x="140" y="246" font-size="13" text-anchor="end">stm8</text>
<rect x="150" y="228" width="140.4" height="16" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="296.4" y="241" font-size="12">0.79</text>
<rect x="150" y="246" width="249.8" height="16" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="405.8" y="259" font-size="12">3.93</text>
<line x1="150" y1="274" x2="620" y2="274" stroke="currentColor"/>
<line x1="150" y1="40" x2="150" y2="274" stroke="currentColor"/>
<line x1="150.0" y1="274" x2="150.0" y2="278" stroke="currentColor"/>
<text x="150.0" y="291" font-size="12" text-anchor="middle">0.1</text>
<line x1="306.7" y1="274" x2="306.7" y2="278" stroke="currentColor"/>
<text x="306.7" y="291" font-size="12" text-anchor="middle">1</text>
<line x1="463.3" y1="274" x2="463.3" y2="278" stroke="currentColor"/>
<text x="463.3" y="291" font-size="12" text-anchor="middle">10</text>
<line x1="620.0" y1="274" x2="620.0" y2="278" stroke="currentColor"/>
<text x="620.0" y="291" font-size="12" text-anchor="middle">100</text>
<rect x="170" y="302" width="14" height="12" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/><text x="190" y="313" font-size="12">QEMU icount 실측 (명령 수 × 32 ns)</text>
<rect x="420" y="302" width="14" height="12" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/><text x="440" y="313" font-size="12">M3 TRM 사이클 모델 @25 MHz</text>
</svg>
```

그림 5 — 256 KB를 0으로 채우는 시간(로그 눈금). 파랑 = QEMU icount 실측(명령 수 × 32 ns), 주황 = Cortex-M3 TRM 사이클 모델 @ 25 MHz. QEMU는 명령 하나를 32 ns로 세므로 32바이트를 한 명령에 쓰는 `stm8`이 실제보다 더 좋아 보인다(실측 32배 차이 vs 모델 13배 차이).

출력에서 볼 것:

- 순서는 두 기준에서 같다: `stm8` < `strd` < `str` < `strb`. ECC RAM에서는 `strb`가 느릴 뿐 아니라 **틀렸다**(RMW — 예제 9). granule 폭 이상의 정렬된 쓰기만 scrub으로 쓸 수 있다.
- newlib-nano `memset`이 `str` 루프보다 2.4배 빠르다. 라이브러리 `memset`은 정렬된 몸통을 워드 · 다중 워드로 쓴다. 하지만 **시작 · 끝이 정렬되지 않으면 바이트 쓰기를 한다** — scrub 대상이 정렬돼 있다는 보장이 없으면 `memset`을 쓰지 않는다. 또 `memset`은 스택을 쓰는 C 함수다.
- QEMU icount 기준 `stm8`은 256 KB에 0.79 ms, 모델 기준 3.9 ms(25 MHz). 실칩에서는 클럭이 높고(H7 480 MHz) 버스가 넓지만 SRAM도 크다. 결론: **scrub 시간은 RAM 크기에 비례하고, 수 MB면 수~수십 ms**다. 부팅 시간 예산(B5 §8)에 넣는다.

---

## 7. Zephyr에서 배치 — DT 메모리 영역, 섹션 태그, 코드 재배치

### 7.1 Zephyr가 주는 손잡이 (v4.4.2 소스에서 확인)

| 손잡이 | 위치 | 하는 일 | 초기화 |
|---|---|---|---|
| DT 노드 `compatible = "zephyr,memory-region"` + `zephyr,memory-region = "ISRAM"` | 보드 DTS · overlay | 링커 `MEMORY` 영역과 같은 이름의 **NOLOAD** 출력 섹션을 만든다(`include/zephyr/linker/devicetree_regions.h`의 `LINKER_DT_REGIONS()` · `LINKER_DT_SECTIONS()`) | **없음** — 복사도 0 채우기도 안 한다 |
| `__attribute__((section(LINKER_DT_NODE_REGION_NAME(DT_NODELABEL(isram)))))` | 소스 | 그 DT 영역 섹션에 객체를 넣는다(`drivers/console/ram_console.c`가 이 방식) | 없음 |
| `__itcm_section`, `__dtcm_data_section`, `__dtcm_bss_section`, `__dtcm_noinit_section`, `__ocm_*`, `__stm32_backup_sram_section` | `include/zephyr/linker/section_tags.h` | 정해진 섹션 이름(`.itcm`, `.dtcm_data` …)을 붙인다 | `arch_data_copy()` · `arch_bss_zero()`가 처리 — **보드에 `chosen` `zephyr,itcm` / `zephyr,dtcm`이 있을 때만** |
| `__noinit`, `__noinit_named(name)` | `section_tags.h` | `.noinit."파일"."번호"` | 없음 (의도대로) |
| `__ramfunc` | `include/zephyr/toolchain/gcc.h` | `.ramfunc` + GCC Arm에서 `long_call` | `arch_data_copy()`가 복사 |
| `__nocache` | `section_tags.h` (`CONFIG_NOCACHE_MEMORY`) | MPU로 non-cacheable인 영역 | B8 §8.2 |
| `zephyr_code_relocate(FILES … LOCATION <영역>_TEXT/_DATA/_BSS)` | CMake, `CONFIG_CODE_DATA_RELOCATION` | 파일 단위로 섹션을 옮기는 링커 조각과 복사 코드를 **생성**한다(`scripts/build/gen_relocate_app.py`) | 생성된 `data_copy_xip_relocation()` · `bss_zeroing_relocation()` |

### 7.2 예제 12 — AN547에서 DT 섹션 vs 코드 재배치 vs `__noinit` vs `__ramfunc`

무엇을 확인하나: 같은 ISRAM 영역에 (a) DT 영역 섹션 속성으로 놓은 변수 두 개(하나는 초기값 있음), (b) `zephyr_code_relocate`로 옮긴 파일의 `.data` · `.bss`를 두고, `hot.c`의 코드는 SRAM 영역으로, `__ramfunc` 함수 하나를 둔다. 첫 부팅 뒤 `sys_reboot(SYS_REBOOT_WARM)`으로 한 번 더 부팅해서 무엇이 남는지 본다.

```cmake
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(f8_placement)
target_sources(app PRIVATE src/main.c src/hot.c)
# hot.c: code -> "SRAM" region, initialised data + bss -> "ISRAM" region (names = zephyr,memory-region in DT)
zephyr_code_relocate(FILES src/hot.c LOCATION SRAM_TEXT)
zephyr_code_relocate(FILES src/hot.c LOCATION ISRAM_DATA)
zephyr_code_relocate(FILES src/hot.c LOCATION ISRAM_BSS)
```

```text
# prj.conf
CONFIG_CODE_DATA_RELOCATION=y
CONFIG_REBOOT=y
CONFIG_PRINTK=y
```

```c
/* hot.c — no attributes; CMake zephyr_code_relocate() moves it */
#include <stdint.h>
uint32_t hot_data = 42;          /* -> ISRAM_DATA: Zephyr copies it at boot */
uint32_t hot_bss;                /* -> ISRAM_BSS : Zephyr zeroes it at boot */
uint32_t hot_step(uint32_t e) { hot_bss += e; return hot_data + hot_bss; }
```

```c
/* z1 — Zephyr placement: DT memory-region section vs code relocation vs __noinit vs __ramfunc */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/linker/devicetree_regions.h>
#define IN_ISRAM __attribute__((section(LINKER_DT_NODE_REGION_NAME(DT_NODELABEL(isram)))))
IN_ISRAM uint32_t g_isram_init = 0x1234;     /* initialiser in a NOLOAD section: what happens? */
IN_ISRAM uint32_t g_isram_cnt;
__noinit uint32_t g_noinit_cnt;
uint32_t g_bss_cnt;
extern uint32_t hot_data, hot_bss;
uint32_t hot_step(uint32_t e);
__ramfunc int ram_mul3(int x) { return 3 * x; }
extern char __ISRAM_start[], __ISRAM_end[];
int main(void)
{
    if (g_noinit_cnt > 3) g_noinit_cnt = 0;   /* power-on garbage guard (QEMU: zero) */
    g_noinit_cnt++; g_isram_cnt++; g_bss_cnt++;
    printk("boot %u: g_isram_init=0x%x  g_isram_cnt=%u (DT section)  g_bss_cnt=%u\n",
           g_noinit_cnt, g_isram_init, g_isram_cnt, g_bss_cnt);
    printk("  hot_step @ %p -> %u   hot_data @ %p   hot_bss @ %p = %u\n", (void *)hot_step,
           hot_step(1), (void *)&hot_data, (void *)&hot_bss, hot_bss);
    printk("  ram_mul3 @ %p -> %d   main @ %p   g_noinit_cnt @ %p\n", (void *)ram_mul3, ram_mul3(14),
           (void *)main, (void *)&g_noinit_cnt);
    printk("  ISRAM section %p..%p\n", (void *)__ISRAM_start, (void *)__ISRAM_end);
    if (g_noinit_cnt < 2) sys_reboot(SYS_REBOOT_WARM);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc '/var/rtoslab/bin/rtos-zbuild mps3/corstone300/an547 /Users/donh/.rtos_scratch/F8/z1 /var/tmp/rtos/F8/z1 -o=-j3 && /var/rtoslab/bin/rtos-zrun /var/tmp/rtos/F8/z1 8'
```

```text
           FLASH:       16000 B       512 KB      3.05%
             RAM:        4268 B       512 KB      0.81%
            SRAM:          24 B         2 MB      0.00%
           ISRAM:          16 B         4 MB      0.00%
qemu-system-arm: warning: nic lan9118.0 has no peer
*** Booting Zephyr OS build v4.4.2 ***
boot 1: g_isram_init=0x0  g_isram_cnt=1 (DT section)  g_bss_cnt=1
  hot_step @ 0x11000001 -> 43   hot_data @ 0x31000000   hot_bss @ 0x31000004 = 1
  ram_mul3 @ 0x30000001 -> 42   main @ 0x1000025d   g_noinit_cnt @ 0x300010a8
  ISRAM section 0x31000008..0x31000010
qemu-system-arm: warning: icount sleep disabled and no active timers
*** Booting Zephyr OS build v4.4.2 ***
boot 2: g_isram_init=0x0  g_isram_cnt=2 (DT section)  g_bss_cnt=1
  hot_step @ 0x11000001 -> 43   hot_data @ 0x31000000   hot_bss @ 0x31000004 = 1
  ram_mul3 @ 0x30000001 -> 42   main @ 0x1000025d   g_noinit_cnt @ 0x300010a8
  ISRAM section 0x31000008..0x31000010
```

출력에서 볼 것:

- **`g_isram_init=0x0`** — 소스에 `= 0x1234`라고 썼는데 0이다. DT 영역 섹션은 NOLOAD라 초기값이 ELF에 실리지 않고, 복사하는 코드도 없다. 빌드는 **경고 없이** 성공했다. QEMU의 RAM이 0이라 0이 보였을 뿐 실칩에서는 쓰레기다. DT 영역 섹션에는 초기값 없는 버퍼(DMA 버퍼, 프레임 버퍼, 공유 메모리)만 둔다.
- `g_isram_cnt`는 1 → 2 — DT 영역 섹션은 **사실상 noinit**이다. 같은 성질의 `__noinit` 변수(DTCM 0x3000_10a8)도 남았다. 반면 `g_bss_cnt`는 매번 1.
- `zephyr_code_relocate`로 옮긴 `hot.c`: 코드는 SRAM 0x1100_0001, `hot_data`는 ISRAM 0x3100_0000에 **42로 복사**됐고, `hot_bss`는 **매 부팅 0으로 지워져** 두 번 모두 1이다. 같은 ISRAM이라도 재배치 경로는 초기화가 있고 DT 섹션 경로는 없다. ISRAM 섹션 범위(0x3100_0008~0x3100_0010)가 재배치된 8바이트 뒤에 붙은 것도 보인다.
- `__ramfunc`는 `zephyr,sram`(= DTCM 0x3000_0001)으로 갔다. `main`은 ITCM(0x1000_025d — 이 보드의 "플래시")에서 돈다.

생성된 것을 열어 본다. 재배치는 마법이 아니라 링커 조각과 C 함수 두 개다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/F8/z1/zephyr; sed -n 21,40p code_relocation.c; grep -v "^$" include/generated/linker_relocate.ld | sed -n 1,16p; arm-none-eabi-objdump -d zephyr.elf | sed -n "/<main>:/,/^$/p" | grep -E "hot_step|blx"'
```

```text
void data_copy_xip_relocation(void)
{

	arch_early_memcpy(&__isram_data_reloc_start, &__isram_data_rom_start,
		           (size_t) &__isram_data_reloc_size);


	arch_early_memcpy(&__sram_text_reloc_start, &__sram_text_rom_start,
		           (size_t) &__sram_text_reloc_size);


}

void bss_zeroing_relocation(void)
{

	arch_early_memset(&__isram_bss_reloc_start, 0,
		           (size_t) &__isram_bss_reloc_size);
/* Linker section for memory region ISRAM for DATA section  */
	SECTION_PROLOGUE(.isram_data_reloc,,)
        {
                . = ALIGN(4);
                
                KEEP(*hot.c.obj(".data.hot_data"))
                . = ALIGN(4);
	} 
#ifdef CONFIG_XIP
GROUP_DATA_LINK_IN(ISRAM , ROMABLE_REGION)
#else
GROUP_DATA_LINK_IN(ISRAM , ISRAM )
#endif
        __isram_data_reloc_end = .;
        __isram_data_reloc_start = ADDR(.isram_data_reloc);
        __isram_data_reloc_size = __isram_data_reloc_end - __isram_data_reloc_start;
        __isram_data_rom_start = LOADADDR(.isram_data_reloc);
1000028a:	f3ff d6b9 	bl	11000000 <hot_step>
100002a6:	4788      	blx	r1
```

출력에서 볼 것:

- 생성기는 파일 단위 지시를 **입력 섹션 단위**(`*hot.c.obj(".data.hot_data")`)로 풀어 쓴다(`-ffunction-sections` · `-fdata-sections` 덕분). `KEEP`이 붙으므로 안 쓰는 함수도 `--gc-sections`로 지워지지 않는다(그래서 `NOKEEP` 옵션이 있다).
- 복사 · 0 채우기는 `arch_early_memcpy` · `arch_early_memset` 호출이고, `arch/common/xip.c`의 `arch_data_copy()`와 `arch/common/init.c`의 `arch_bss_zero()`가 `CONFIG_CODE_DATA_RELOCATION`일 때 이 두 함수를 부른다.
- `main`(0x1000_028a)에서 `hot_step`(0x1100_0000)은 16 MB 경계 바로 안이라 `bl` 직접, `__ramfunc` 함수는 `blx r1` — `long_call`이 붙은 결과다(§3.2).

### 7.3 예제 13 — `chosen`이 없는 보드에서 `__dtcm_bss_section`을 쓰면

무엇을 확인하나: `section_tags.h`의 TCM 매크로는 Cortex-M 링커 스크립트(`include/zephyr/arch/arm/cortex_m/scripts/linker.ld`)에서 `DT_CHOSEN(zephyr_itcm)` · `DT_CHOSEN(zephyr_dtcm)`이 okay일 때만 출력 섹션이 정의된다. AN547 DTS는 ITCM · DTCM 노드가 있지만 `chosen`으로는 `zephyr,flash = &itcm`, `zephyr,sram = &dtcm`만 지정하고 `zephyr,itcm` · `zephyr,dtcm`은 지정하지 않는다. 이 보드에서 매크로를 쓰면 어떻게 될까.

```c
/* z2 — __dtcm_bss_section / __itcm_section on a board WITHOUT chosen zephyr,dtcm / zephyr,itcm */
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/linker/section_tags.h>
__dtcm_bss_section uint32_t g_ctrl_state[64];
__itcm_section int fast_add(int a, int b) { return a + b; }
int main(void)
{
    g_ctrl_state[0] = (uint32_t)fast_add(40, 2);
    printk("g_ctrl_state @ %p = %u   fast_add @ %p\n", (void *)g_ctrl_state, g_ctrl_state[0], (void *)fast_add);
    return 0;
}
```

```sh
limactl shell elinux -- bash -lc '. /var/rtoslab/zephyr-venv/bin/activate; export ZEPHYR_BASE=/var/rtoslab/zephyrproject/zephyr ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb GNUARMEMB_TOOLCHAIN_PATH=/usr; cd /var/rtoslab/zephyrproject && west build -p always -b mps3/corstone300/an547 /Users/donh/.rtos_scratch/F8/z2 -d /var/tmp/rtos/F8/z2 -o=-j3 2>&1 | grep -iE "warning|orphan|error:|dtcm|itcm" | grep -v "Building C" | head; /var/rtoslab/bin/rtos-zrun /var/tmp/rtos/F8/z2 5; arm-none-eabi-objdump -h /var/tmp/rtos/F8/z2/zephyr/zephyr.elf | grep -iE "dtcm|itcm"; arm-none-eabi-nm /var/tmp/rtos/F8/z2/zephyr/zephyr.elf | grep -E "g_ctrl_state|fast_add"'
```

```text
/usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/bin/ld.bfd: warning: orphan section `.itcm' from `app/libapp.a(main.c.obj)' being placed in section `.itcm'
/usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/bin/ld.bfd: warning: orphan section `.dtcm_bss' from `app/libapp.a(main.c.obj)' being placed in section `.dtcm_bss'
...
            ITCM:           0 B       512 KB      0.00%
            DTCM:           0 B       512 KB      0.00%
qemu-system-arm: warning: nic lan9118.0 has no peer
*** Booting Zephyr OS build v4.4.2 ***
***** MPU FAULT *****
  Data Access Violation
  MMFAR Address: 0x10003c38
r0/a1:  0x00000000  r1/a2:  0x10003c38  r2/a3:  0x0000002a
r3/a4:  0x10003c38 r12/ip:  0x0003cb07 r14/lr:  0x100012c3
 xpsr:  0x21000000
Faulting instruction address (r15/pc): 0x10000248
  2 .itcm         00000004  10002ce0  10002ce0  00002db4  2**1
 23 .dtcm_bss     00000100  10003c38  10003c38  00003d0c  2**2
10002ce0 T fast_add
10003c38 D g_ctrl_state
```

출력에서 볼 것:

- 링커는 정의되지 않은 섹션을 **orphan**으로 처리해 "적당한 곳"에 붙였다 — 경고만 하고 빌드는 성공한다(경고 4줄 중 같은 내용 2줄은 생략). `.dtcm_bss`는 DTCM이 아니라 코드 영역(0x1000_3c38, ITCM = 이 보드의 플래시) 끝에 붙었다.
- 첫 쓰기에서 **MPU FAULT** — Zephyr가 코드 영역을 MPU로 읽기 전용으로 막아 두었기 때문이다(F4). 쓰기 금지 영역이 아니었다면 더 나쁘다: 조용히 엉뚱한 곳에 쓰고, 0 채우기도 안 된 채로 동작한다.
- 교훈: 섹션 태그 매크로는 "**보드가 그 섹션을 정의했다**"는 약속 위에서만 동작한다. 새 보드로 옮길 때 빌드 로그에서 `orphan section` 경고를 **에러로 다룬다**(GNU ld `--orphan-handling=error`). 고치는 법은 보드 overlay에 `chosen { zephyr,dtcm = &dtcm; };`를 넣는 것이지만, AN547처럼 DTCM이 이미 `zephyr,sram`이면 그냥 일반 `.bss`가 DTCM이다.

---

## 8. 메모리 예산 — 영역별로 얼마나 찼나

### 8.1 예제 14 — 드론 FC 모양의 배치를 H7 지도에 링크하고 예산을 낸다

QEMU에는 H7 보드가 없으므로 이 예제는 **빌드 전용**이다. 무엇을 확인하나: 드론 FC가 실제로 가질 법한 객체(커널 핫패스, rate loop, PID 계수, 태스크 스택 5개, IMU · UART DMA 버퍼, 힙 두 뱅크, 블랙박스 스테이징, backup SRAM 크래시 기록)를 B8과 같은 H7 지도(ITCM · DTCM · AXI · D2 · backup)에 배치하고, 링크 결과에서 영역별 사용량을 계산한다. Cortex-M7 + FreeRTOS `ARM_CM7/r0p1` 포트 + heap_5로 빌드했다.

```c
/* ex8 — drone-FC-shaped placement on an H7-like map (build only): what goes where, then a budget */
#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>
#define SEC(s) __attribute__((section(s)))
#define ITCM_FN    SEC(".itcm_text") __attribute__((noinline))
#define DTCM_DATA  SEC(".dtcm_data")
#define DTCM_BSS   SEC(".dtcm_bss")
#define STACKS     SEC(".dtcm_stacks") __attribute__((aligned(8)))
#define DMA_BUF    SEC(".d2_bss") __attribute__((aligned(32)))
#define AXI_NOINIT SEC(".axi_noinit") __attribute__((aligned(32)))
#define D2_NOINIT  SEC(".d2_noinit") __attribute__((aligned(8)))
#define BKP_NOINIT SEC(".bkp_noinit")
DTCM_DATA float g_pid_gain[3][3] = { { 0.08f, 0.02f, 0.001f }, { 0.08f, 0.02f, 0.001f }, { 0.2f, 0.05f, 0 } };
DTCM_BSS  float g_rate_state[3][4];                    /* integrators, last error, filter state */
DMA_BUF   int16_t g_imu_dma[2][512 * 6];               /* SPI IMU ping-pong, 8 kHz bursts      */
DMA_BUF   uint8_t g_uart_tx_dma[2][1024];              /* telemetry UART DMA                   */
AXI_NOINIT uint8_t g_heap_axi[160 * 1024];             /* heap bank 1 (cached)                 */
D2_NOINIT  uint8_t g_heap_d2[64 * 1024];               /* heap bank 2 (DMA reachable)          */
AXI_NOINIT uint8_t g_blackbox[128 * 1024];             /* flight log staging before flash      */
BKP_NOINIT struct { uint32_t magic, boots, cause, pc, lr, crc; } g_crash;
static const HeapRegion_t k_regions[] = {
    { g_heap_axi, sizeof g_heap_axi }, { g_heap_d2, sizeof g_heap_d2 }, { NULL, 0 } };
STACKS static StackType_t s_rate[512], s_att[512], s_est[1024], s_tlm[512], s_log[768];
DTCM_BSS static StaticTask_t t_rate, t_att, t_est, t_tlm, t_log;
ITCM_FN void rate_loop(const int16_t *gyro)            /* called from the IMU DMA-complete ISR */
{
    for (int a = 0; a < 3; a++) {
        float e = -gyro[a] * 0.001f;
        g_rate_state[a][0] += e * g_pid_gain[a][1];
        g_rate_state[a][1] = e * g_pid_gain[a][0] + g_rate_state[a][0];
    }
}
static void task(void *arg) { for (;;) { rate_loop(g_imu_dma[0]); vTaskDelay((uint32_t)arg); } }
int main(void)
{
    vPortDefineHeapRegions(k_regions);
    g_crash.boots++; g_uart_tx_dma[0][0] = g_blackbox[0];
    xTaskCreateStatic(task, "rate", 512, (void *)1, 6, s_rate, &t_rate);
    xTaskCreateStatic(task, "att", 512, (void *)2, 5, s_att, &t_att);
    xTaskCreateStatic(task, "est", 1024, (void *)4, 4, s_est, &t_est);
    xTaskCreateStatic(task, "tlm", 512, (void *)20, 2, s_tlm, &t_tlm);
    xTaskCreateStatic(task, "log", 768, (void *)50, 1, s_log, &t_log);
    vTaskStartScheduler();
    return 1;
}
```

링커 스크립트 `h7.ld`의 메모리 지도와 핵심 줄(전체는 copy 3줄 · zero 3줄 표를 포함해 40줄):

```text
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 2M
    ITCM  (rwx) : ORIGIN = 0x00000000, LENGTH = 64K
    DTCM  (rw)  : ORIGIN = 0x20000000, LENGTH = 128K
    AXI   (rwx) : ORIGIN = 0x24000000, LENGTH = 512K
    D2    (rw)  : ORIGIN = 0x30000000, LENGTH = 288K   /* SRAM1-3: DMA1/2 reachable (B8 4.2) */
    BKP   (rw)  : ORIGIN = 0x38800000, LENGTH = 4K     /* backup SRAM */
}
...
    .itcm_text : ALIGN(8) { *tasks.o(.text* .rodata*) *list.o(.text* .rodata*) *port.o(.text* .rodata*)
                            *queue.o(.text* .rodata*) *(.itcm_text*) . = ALIGN(8); } > ITCM AT > FLASH
    .dtcm_data : ALIGN(4) { *(.dtcm_data*) . = ALIGN(4); } > DTCM AT > FLASH
    .dtcm_bss (NOLOAD) : ALIGN(8) { *(.dtcm_bss*) . = ALIGN(8); } > DTCM
    .dtcm_stacks (NOLOAD) : ALIGN(8) { *(.dtcm_stacks*) . = ALIGN(8); } > DTCM
    .stack (NOLOAD) : ALIGN(8) { __StackLimit = .; . += 0x1000; __StackTop = .; } > DTCM   /* MSP = ISR stack */
    .data : ALIGN(4) { *(.data*) . = ALIGN(4); } > AXI AT > FLASH
    .bss (NOLOAD) : ALIGN(4) { *(.bss*) *(COMMON) . = ALIGN(4); } > AXI
    .axi_noinit (NOLOAD) : ALIGN(32) { *(.axi_noinit*) } > AXI
    .d2_bss (NOLOAD) : ALIGN(32) { *(.d2_bss*) . = ALIGN(32); } > D2     /* DMA buffers (MPU: non-cacheable) */
    .d2_noinit (NOLOAD) : ALIGN(8) { *(.d2_noinit*) } > D2
    .bkp_noinit (NOLOAD) : ALIGN(4) { KEEP(*(.bkp_noinit*)) } > BKP
```

첫 링크는 `undefined reference to __heap_end`로 실패했다 — 랩 `board.c`의 `_sbrk`가 libc `malloc`용 심볼을 요구한다. 이 펌웨어는 libc `malloc`을 쓰지 않으므로 두 심볼을 같은 값(크기 0)으로 정의해 `_sbrk`가 항상 실패하게 했다. "libc 힙이 어디 있나"도 배치 결정이다(F7).

```sh
limactl shell elinux -- bash -lc '/Users/donh/.rtos_scratch/F8/ex8/build.sh'
```

```text
.vectors      size=192     vma=08000000 lma=08000000
.text         size=6128    vma=080000c0 lma=080000c0
.ARM.exidx    size=8       vma=080018b0 lma=080018b0
.copy.table   size=36      vma=080018b8 lma=080018b8
.itcm_text    size=5768    vma=00000000 lma=080018f8
.dtcm_data    size=36      vma=20000000 lma=08002f80
.data         size=100     vma=24000000 lma=08002fa4
.zero.table   size=24      vma=080018dc lma=080018dc
.dtcm_bss     size=528     vma=20000028 lma=08002fa8
.bss          size=4320    vma=24000068 lma=08003008
.d2_bss       size=14336   vma=30000000 lma=30000000
.dtcm_stacks  size=13312   vma=20000238 lma=08002fa8
.stack        size=4096    vma=20003638 lma=08002fa8
.axi_noinit   size=294912  vma=24001160 lma=08003020
.d2_noinit    size=65536   vma=30003800 lma=30003800
.bkp_noinit   size=24      vma=38800000 lma=38800000
```

`.bss`(AXI) 4320바이트의 내용을 `nm --size-sort`로 보면 큰 것이 커널의 idle 스택 1 KB와 timer 스택 2 KB다(§4.3). 이것을 "스택"으로 분류해 영역별 예산을 Python으로 그렸다(분류: 코드 = `.vectors` + `.text` + 표; load 이미지 = 복사 원본; data + bss; 스택 = `.dtcm_stacks` + `.stack` + 커널 스택 3 KB; DMA = `.d2_bss`; noinit 버퍼).

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="140" y="33" font-size="13" text-anchor="end">FLASH 2 MB</text>
<rect x="150" y="19" width="370" height="20" fill="none" stroke="currentColor" stroke-width="0.8"/>
<rect x="150.0" y="19" width="1.1" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0" stroke-width="0.6"/>
<rect x="151.1" y="19" width="1.0" height="20" fill="#888" fill-opacity="0.55" stroke="#888" stroke-width="0.6"/>
<text x="528" y="33" font-size="12">0.6% (12.0 KB)</text>
<text x="140" y="71" font-size="13" text-anchor="end">ITCM 64 KB</text>
<rect x="150" y="57" width="370" height="20" fill="none" stroke="currentColor" stroke-width="0.8"/>
<rect x="150.0" y="57" width="32.6" height="20" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0" stroke-width="0.6"/>
<text x="528" y="71" font-size="12">8.8% (5.6 KB)</text>
<text x="140" y="109" font-size="13" text-anchor="end">DTCM 128 KB</text>
<rect x="150" y="95" width="370" height="20" fill="none" stroke="currentColor" stroke-width="0.8"/>
<rect x="150.0" y="95" width="1.6" height="20" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b" stroke-width="0.6"/>
<rect x="151.6" y="95" width="49.1" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c" stroke-width="0.6"/>
<text x="528" y="109" font-size="12">13.7% (17.6 KB)</text>
<text x="140" y="147" font-size="13" text-anchor="end">AXI 512 KB</text>
<rect x="150" y="133" width="370" height="20" fill="none" stroke="currentColor" stroke-width="0.8"/>
<rect x="150.0" y="133" width="1.0" height="20" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b" stroke-width="0.6"/>
<rect x="151.0" y="133" width="2.2" height="20" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c" stroke-width="0.6"/>
<rect x="153.2" y="133" width="208.1" height="20" fill="#888" fill-opacity="0.55" stroke="#888" stroke-width="0.6"/>
<text x="528" y="147" font-size="12">57.1% (292.2 KB)</text>
<text x="140" y="185" font-size="13" text-anchor="end">D2 SRAM 288 KB</text>
<rect x="150" y="171" width="370" height="20" fill="none" stroke="currentColor" stroke-width="0.8"/>
<rect x="150.0" y="171" width="18.0" height="20" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a" stroke-width="0.6"/>
<rect x="168.0" y="171" width="82.2" height="20" fill="#888" fill-opacity="0.55" stroke="#888" stroke-width="0.6"/>
<text x="528" y="185" font-size="12">27.1% (78.0 KB)</text>
<text x="140" y="223" font-size="13" text-anchor="end">Backup 4 KB</text>
<rect x="150" y="209" width="370" height="20" fill="none" stroke="currentColor" stroke-width="0.8"/>
<rect x="150.0" y="209" width="2.2" height="20" fill="#888" fill-opacity="0.55" stroke="#888" stroke-width="0.6"/>
<text x="528" y="223" font-size="12">0.6% (24 B)</text>
<rect x="20" y="252" width="14" height="12" fill="#4a7bd0" fill-opacity="0.55" stroke="#4a7bd0"/><text x="40" y="263" font-size="12">코드</text>
<rect x="88" y="252" width="14" height="12" fill="#888" fill-opacity="0.55" stroke="#888"/><text x="108" y="263" font-size="12">load 이미지 / noinit 버퍼</text>
<rect x="300" y="252" width="14" height="12" fill="#3f9a6b" fill-opacity="0.55" stroke="#3f9a6b"/><text x="320" y="263" font-size="12">data + bss</text>
<rect x="412" y="252" width="14" height="12" fill="#e08a3c" fill-opacity="0.55" stroke="#e08a3c"/><text x="432" y="263" font-size="12">스택</text>
<rect x="480" y="252" width="14" height="12" fill="#d0564a" fill-opacity="0.55" stroke="#d0564a"/><text x="500" y="263" font-size="12">DMA 버퍼</text>
</svg>
```

그림 6 — 예제 14의 영역별 사용량(H7 모양 지도, 링크 결과에서 계산). DTCM은 스택이 대부분이고, AXI는 힙 1뱅크와 블랙박스 스테이징 버퍼가 57%를 차지한다. ITCM 64 KB 중 커널 핫패스 + rate loop는 5.6 KB(8.8%)라 ISR을 더 옮길 여유가 있다.

예산에서 읽는 것:

- **DTCM 13.7%** 중 스택이 17 KB다. 스택 크기는 F3의 high water mark 실측으로 정하고, 여유가 크면 제어 상태 테이블 · 필터 버퍼를 DTCM으로 더 옮긴다.
- **ITCM 8.8%**: FOC나 IMU 처리 ISR, `memcpy`(커널이 부르는 것 — §3.4)를 옮길 공간이 있다.
- **AXI 57%**의 대부분은 noinit 버퍼라 부팅 시간(0 채우기)에는 영향이 없다. 단 ECC scrub 대상이라면 전체 512 KB가 부팅 시간에 들어간다(§6.6).
- **D2 27%**: DMA 버퍼 14 KB + 힙 2뱅크 64 KB. MPU로 D2를 non-cacheable로 두면 힙의 이 뱅크도 non-cacheable이 된다 — 큰 CPU 작업 버퍼가 여기서 할당되면 느려진다. heap_5 first-fit이 AXI(낮은 주소)부터 쓰므로 평소에는 괜찮지만 보장은 아니다(§4.1).
- 이런 표를 CI가 매 빌드마다 만들고 영역별 상한을 넘으면 실패시키면 "어느 날 갑자기 DTCM 오버플로"가 사라진다. Zephyr는 빌드 끝에 영역별 사용률을 출력한다(예제 12의 `FLASH:` · `RAM:` · `SRAM:` · `ISRAM:` 줄 — DT 영역도 자동으로 들어간다).

---

## 9. RTOS 커널 안에서는 무슨 일이 일어나나

### 9.1 FreeRTOS — 커널은 배치를 모른다

FreeRTOS 커널에는 "메모리 영역"이라는 개념이 없다. 배치는 전부 커널 **밖**에서 정해진다.

- 부팅: 벤더 startup(또는 이 노트의 CMSIS `__cmsis_start()`)이 `main` 전에 표를 돈다. 커널 코드는 하나도 돌지 않았다(B5 §10.1).
- 힙: heap_5의 `vPortDefineHeapRegions()`(`portable/MemMang/heap_5.c`)가 앱이 준 영역 표로 빈 블록 목록을 만든다. 이후 `pvPortMalloc()`은 주소 순 first-fit이다(§4.1, F1).
- 스택 · TCB: `xTaskCreateStatic()`(`tasks.c`)은 받은 포인터를 그대로 쓴다. 위치는 배열을 정의한 섹션이 정한다. `prvInitialiseNewTask()`가 스택을 `tskSTACK_FILL_BYTE`(0xa5)로 칠하고 TCB를 `memset`으로 0으로 만든다 — 그래서 스택 · TCB 섹션은 0 채우기가 필요 없다.
- idle · timer 스택: `configKERNEL_PROVIDED_STATIC_MEMORY`이면 `tasks.c` 안의 `static` 배열(§4.3).
- 포트: `port.c`의 `xPortPendSVHandler` · `vPortSVCHandler`는 벡터 테이블의 절대 주소로 불리므로 어느 영역에 있어도 veneer가 없다(§3.4).

### 9.2 Zephyr — 부팅 경로가 영역마다 초기화를 부른다

Zephyr는 링커 스크립트를 DT에서 생성하고, 부팅 경로의 정해진 자리에서 영역별 복사 · 0 채우기를 부른다. v4.4.2 Cortex-M 경로:

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="165" y="20" font-size="13" text-anchor="middle" font-weight="bold">Zephyr v4.4.2 (Cortex-M)</text>
<text x="515" y="20" font-size="13" text-anchor="middle" font-weight="bold">FreeRTOS + 벤더 startup</text>
<rect x="10" y="34" width="310" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="20" y="50" font-size="12" font-weight="bold">z_arm_reset (reset.S)</text>
<text x="20" y="67" font-size="12">MSP = z_main_stack, MSPLIM 설정</text>
<rect x="10" y="86" width="310" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="20" y="102" font-size="12" font-weight="bold">soc_reset_hook()</text>
<text x="20" y="119" font-size="12">H7: ITCM 64b · DTCM 32b 0 채우기 (ECC)</text>
<rect x="10" y="138" width="310" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="20" y="154" font-size="12" font-weight="bold">z_prep_c() → arch_bss_zero()</text>
<text x="20" y="171" font-size="12">.bss · dtcm_bss · ocm_bss · reloc bss</text>
<rect x="10" y="190" width="310" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="20" y="206" font-size="12" font-weight="bold">arch_data_copy()</text>
<text x="20" y="223" font-size="12">data · ramfunc · itcm · dtcm · reloc 복사</text>
<rect x="10" y="242" width="310" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="20" y="258" font-size="12" font-weight="bold">z_cstart() → init 레벨 → 드라이버</text>
<text x="20" y="275" font-size="12">DT 영역 섹션(NOLOAD)은 손대지 않음</text>
<rect x="10" y="294" width="310" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="20" y="310" font-size="12" font-weight="bold">main()</text>
<text x="20" y="327" font-size="12">__noinit · DT 영역 섹션은 이전 값 그대로</text>
<line x1="165" y1="74" x2="165" y2="84" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="165" y1="126" x2="165" y2="136" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="165" y1="178" x2="165" y2="188" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="165" y1="230" x2="165" y2="240" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="165" y1="282" x2="165" y2="292" stroke="currentColor" marker-end="url(#ah7)"/>
<rect x="360" y="34" width="310" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="370" y="50" font-size="12" font-weight="bold">Reset_Handler (벤더 · 이 노트 asm)</text>
<text x="370" y="67" font-size="12">ECC scrub → copy/zero table</text>
<rect x="360" y="86" width="310" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="370" y="102" font-size="12" font-weight="bold">main() → vPortDefineHeapRegions()</text>
<text x="370" y="119" font-size="12">heap_5 영역 표: 주소 오름차순</text>
<rect x="360" y="138" width="310" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="370" y="154" font-size="12" font-weight="bold">xTaskCreateStatic(…, 스택, TCB)</text>
<text x="370" y="171" font-size="12">위치 = 배열을 정의한 섹션</text>
<rect x="360" y="190" width="310" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="370" y="206" font-size="12" font-weight="bold">vTaskStartScheduler()</text>
<text x="370" y="223" font-size="12">idle · timer 스택: tasks.c의 .bss</text>
<rect x="360" y="242" width="310" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="370" y="258" font-size="12" font-weight="bold">커널은 영역을 모른다</text>
<text x="370" y="275" font-size="12">port.c · tasks.c는 받은 주소만 쓴다</text>
<line x1="515" y1="74" x2="515" y2="84" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="515" y1="126" x2="515" y2="136" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="515" y1="178" x2="515" y2="188" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="515" y1="230" x2="515" y2="240" stroke="currentColor" marker-end="url(#ah7)"/>
<text x="340" y="362" font-size="12" text-anchor="middle">빨강 = ECC 초기화 자리, 초록 = 복사 · 0 채우기, 회색 = 초기화하지 않는 영역이 살아남는 구간</text>
</svg>
```

그림 7 — 두 RTOS에서 배치 · 초기화가 일어나는 자리. Zephyr는 `reset.S` → `soc_reset_hook()` → `z_prep_c()`(`arch/arm/core/cortex_m/prep_c.c`: `soc_prep_hook()`, `relocate_vector_table()`, FPU 초기화, `arch_bss_zero()`, `arch_data_copy()`, 인터럽트 초기화, `z_cstart()`) 순서다. FreeRTOS는 `main` 전의 일을 전부 벤더 startup에 맡긴다.

소스에서 확인한 이름과 위치:

| 단계 | 파일 · 함수 | 배치 관련 일 |
|---|---|---|
| 리셋 | `arch/arm/core/cortex_m/reset.S` `z_arm_reset` | MSP를 `z_main_stack`으로, `CONFIG_SOC_RESET_HOOK`이면 `bl soc_reset_hook` |
| SoC 훅 | `soc/st/stm32/stm32h7x/soc_m7.c` `soc_reset_hook()` | TCM ECC 초기화(§6.3) |
| SoC 훅 | `soc/nxp/imxrt/imxrt10xx/soc.c` `soc_reset_hook()` | `flexram_dt_partition()`(§1.3) → `SystemInit()` |
| 0 채우기 | `arch/common/init.c` `arch_bss_zero()` | `__bss_start`~`__bss_end`, chosen `zephyr,dtcm`이면 `__dtcm_bss_*`, `CONFIG_CODE_DATA_RELOCATION`이면 `bss_zeroing_relocation()`, `CONFIG_NOCACHE_MEMORY`면 nocache 영역 |
| 복사 | `arch/common/xip.c` `arch_data_copy()` | `.data`, `__ramfunc_region`, nocache load, `__itcm_*`, `__dtcm_data_*`, `data_copy_xip_relocation()` |
| 저수준 | `arch/common/init.c` `arch_early_memset()` · `arch_early_memcpy()` | `__weak` — 아키텍처가 "아직 쓰면 안 되는 최적화 memset"을 피하려고 바꿔 끼울 수 있다 |
| 링커 | `include/zephyr/arch/arm/cortex_m/scripts/linker.ld` | `LINKER_DT_REGIONS()`(MEMORY), `GROUP_START(ITCM)` · `GROUP_START(DTCM)`(chosen이 있을 때), `LINKER_DT_SECTIONS()`(NOLOAD), `CONFIG_CODE_DATA_RELOCATION` 조각 include |

Cortex-R 쪽(`arch/arm/core/cortex_a_r/reset.S`)은 `CONFIG_DISABLE_TCM_ECC`이면 `z_arm_tcm_disable_ecc()`를 부른다(B9 §7). M 프로파일에는 TCM ECC를 위한 아키텍처 공통 코드가 없고 SoC 훅이 맡는다 — "ECC 초기화는 칩마다 다르다"는 사실이 코드 구조에 그대로 드러난다.

### 9.3 Don 경험과 연결

SSD 펌웨어의 부트 코드는 "TCM zero-fill → 섹션 복사 → 코어별 스택 설정 → 실행부 진입"이었다. Zephyr의 `reset.S` → `soc_reset_hook` → `z_prep_c`가 같은 모양이고, 차이는 영역 목록이 DT에서 생성된다는 것뿐이다. FreeRTOS는 그 부트 코드를 아예 갖지 않는다 — 그래서 FreeRTOS 프로젝트에서는 **startup과 링커 스크립트가 팀의 코드**이고, 이 노트의 기법을 직접 써야 한다.

---

## 10. 실무에서 다시 보기

### 10.1 드론 비행 제어기 (STM32H743)

- IMU SPI DMA(8 kHz 버스트) 버퍼는 D2 SRAM에 32 B 정렬, MPU non-cacheable(E6). DMA 완료 ISR과 rate loop는 ITCM, PID 상태 · 계수는 DTCM, 태스크 스택 5개와 MSP는 DTCM(예제 14).
- 블랙박스 로그는 AXI SRAM에 모았다가 낮은 우선순위 태스크가 플래시 · SD로 쓴다. 크래시 기록은 backup SRAM에, 항목마다 CRC.
- 부팅: `soc_reset_hook` 수준에서 TCM scrub → copy/zero 표 → 캐시 켜기 → RTOS. "모터 재개까지 몇 ms"(B5 §11.1)에 scrub 시간이 들어간다.
- 함정: 비행 중 리셋(워치독) 후 빠른 재시작 경로에서는 noinit · backup의 "비행 중이었음" 표시를 보고 캘리브레이션을 건너뛴다 — 이 표시가 검증(magic · CRC)을 통과해야만 쓴다.

### 10.2 로봇 관절 모터 컨트롤러 (FOC 20 kHz, CAN)

- 50 µs마다 도는 전류 루프 ISR과 그것이 부르는 `sin`/`cos` 테이블 · Clarke/Park 변환을 **같이** TCM(또는 STM32G4라면 CCM SRAM)에 둔다. 테이블이 플래시에 남으면 효과가 반감된다(B8 §4.6).
- veneer를 피하려고 ISR 자체를 TCM에 두고 벡터 테이블을 RAM으로 옮긴다(`VTOR`). 호출되는 함수에는 `long_call`을 붙이거나 같은 영역에 둔다(§3).
- 플래시에 파라미터를 저장하는 코드는 RAM 함수로(같은 뱅크 페치 불가 칩).

### 10.3 센서 허브 MCU (Zephyr, Linux SoC 옆)

- Linux와 공유하는 메모리(OpenAMP vring, 타임스탬프 링)는 DT에 `zephyr,memory-region` 노드로 정의하고 섹션 속성으로 놓는다. 이 영역은 NOLOAD라 **MCU가 부팅할 때 지우지 않는다** — Linux가 먼저 써 둔 내용을 보존해야 할 때는 장점, 초기값이 필요할 때는 명시적 초기화가 필요하다(예제 12).
- 보드를 바꿀 때 `orphan section` 경고를 에러로(예제 13).

### 10.4 Don의 SSD 컨트롤러

- 코어별 TCM에 FTL 핫 경로 · 스택, 공유 SRAM에 코어 간 메일박스, DRAM에 데이터 버퍼 — 같은 결정표(§1.6)다. 전원 차단 복구(PLP) 경로에서 남겨야 하는 상태를 "어느 RAM이 언제까지 사는가"로 정한 것이 §5의 noinit 설계와 같은 사고다.
- 차이: SSD는 대개 벤더 고유 부트 코드 하나라 배치 규칙이 한 팀의 머릿속에 있었다. RTOS 생태계에서는 부트로더(MCUboot) · RTOS(Zephyr DT) · 앱이 서로 다른 팀 코드라 **링커 스크립트와 DT가 계약서** 역할을 한다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| DMA 버퍼를 DTCM에 (H7) | DMA가 항상 안 옴, 전송 오류 플래그 | DMA1/2가 TCM에 닿지 않음 | DMA가 닿는 SRAM으로, RM 버스 표 확인 (B8) |
| 새 섹션을 copy table에 안 올림 | 디버거로는 되고 전원 껐다 켜면 초기값이 틀림 | 디버거 `load`가 VMA에 직접 써 줌 | 표에 한 줄, 콜드 부팅 시험을 CI에 (§2.3) |
| DT 영역 섹션에 초기값 있는 변수 | 초기값이 0 또는 쓰레기, 경고 없음 | Zephyr DT 영역 섹션은 NOLOAD | 초기값 없는 버퍼만 두거나 `zephyr_code_relocate` 사용 (예제 12) |
| chosen 없는 보드에서 `__dtcm_bss_section` | orphan 경고, 첫 쓰기에 MPU FAULT 또는 조용한 오염 | 출력 섹션이 정의되지 않음 | `chosen` 추가, `--orphan-handling=error` (예제 13) |
| noinit을 `.bss` 뒤에 그냥 붙임 | 펌웨어 업데이트 뒤 첫 부팅에 기록 소실 | `.bss` 크기 따라 주소 이동 | 고정 MEMORY 영역 (예제 7) |
| noinit을 SRAM 꼭대기에 | 크래시 로그가 늘 비어 있음 ("layout changed") | 부트로더 스택이 덮음 | 부트로더와 같은 창을 예약 (예제 8) |
| ECC RAM에서 scrub 전에 C 함수 호출 | 전원 투입 때만 HardFault / BusFault, 디버거 연결 시 재현 안 됨 | 스택 push · RMW가 초기화 안 된 ECC 워드를 읽음 | 어셈블리 scrub을 맨 앞에 (예제 10) |
| ECC RAM을 `memset`/`strb`로 scrub | 가끔 fault, 가끔 쓰레기 | 정렬 안 된 끝 · 바이트 쓰기 = RMW | granule 폭 이상 정렬 쓰기 (`stm`, `strd`) (예제 9, 11) |
| D-cache를 켠 뒤 scrub | 첫 라인 fill에서 ECC 오류 | write-allocate가 라인을 먼저 읽음 | 캐시 켜기 전에 scrub (§6.3) |
| ITCM 함수가 플래시 const · 함수를 부름 | 옮겼는데 지터가 그대로 | veneer · 플래시 테이블 페치 | 호출 그래프 전체를 옮김, `long_call` (§3) |
| 파일 패턴 `*a.o *b.o (.text*)` | 데이터가 코드 영역으로, 이상한 오버플로 | 괄호 없는 파일 이름 = 모든 섹션 | 파일마다 `(섹션)` (예제 4) |
| heap_5 영역 표를 주소 역순으로 | `configASSERT` 또는 힙 손상 | heap_5는 오름차순 전제 | 주소 순서로 정렬, `{ NULL, 0 }`으로 끝 (예제 5) |

---

## 12. 면접에서 이렇게 말한다

**Q.** "On a Cortex-M7 like the STM32H7, what do you put in ITCM, DTCM, and regular SRAM, and why?"

**A.** ITCM에는 지터가 사양인 코드(고속 ISR, rate loop, 커널 핫패스), DTCM에는 스택 · TCB · 제어 상태, DMA 버퍼는 DMA가 닿는 SRAM(H7은 DMA1/2가 TCM에 못 닿음)에 MPU non-cacheable로, 큰 버퍼와 힙은 AXI SRAM. 기준은 지연 · 캐시 · 버스 마스터 · 유지 · ECC 다섯 가지이고, 결과는 map 파일로 증명한다.

> I sort every RAM by five properties: latency, cacheability, which bus masters can reach it, retention, and ECC. ITCM gets the code whose worst case is the spec — the IMU or current-loop ISR, the rate controller, and often the kernel's context-switch path. DTCM gets task stacks, the ISR stack and control state, because there are no cache misses there. DMA buffers go to a DMA-reachable SRAM bank marked non-cacheable, since on the H7 the general-purpose DMAs can't reach the TCMs at all. Big buffers and the heap live in AXI SRAM with the cache on. Then I prove the placement from the map file, not from intentions.

**꼬리 질문**: "Why not put everything in TCM?" → 크기(H7 ITCM 64 KB), DMA 접근 불가, 그리고 TCM 함수를 플래시에서 부르면 veneer가 생긴다.

**Q.** "You moved a function to RAM and the linker created something called a veneer. What is it and does it matter?"

**A.** Thumb-2 `BL`은 ±16 MB만 가므로 링커가 플래시에 `ldr.w pc, [pc]` + 주소 한 워드짜리 점프대를 만든다. QEMU icount로 재면 호출당 명령 1개가 늘었다. 실칩에서는 그 veneer가 플래시에 있다는 게 문제다. `long_call`을 쓰면 컴파일러가 주소를 레지스터로 로드해 `blx`하고, 루프 밖으로 끌어낼 수 있다. Zephyr의 GCC `__ramfunc`가 `long_call`을 붙이는 이유다.

> A BL instruction only reaches plus or minus 16 megabytes, and flash and SRAM are hundreds of megabytes apart, so the linker inserts a small trampoline — a load-PC-from-literal — and points the call at it. I measured it as exactly one extra instruction per call, but the real cost on silicon is that the veneer itself lives in flash, so a 'RAM-resident' ISR path still fetches from flash. Using the long_call attribute makes the compiler load the address into a register and BLX, which it can hoist out of loops; Zephyr's GCC ramfunc macro does exactly that.

**Q.** "How do you keep a crash log across a reset?"

**A.** startup이 건드리지 않는 noinit 창(고정 주소 MEMORY 영역 또는 backup SRAM)에 magic · 버전 · 크기 · CRC가 있는 링 버퍼를 둔다. 다음 부팅이 검증하고, 플래시로 옮기고, 지운다. 함정 셋: `.bss` 뒤에 붙이면 주소가 움직이고, 부트로더 스택이 덮을 수 있고, ECC RAM에서는 POR 때 그 창을 읽기 전에 scrub해야 한다.

> I reserve a fixed-address no-init window in the linker script — same address in every firmware version, and reserved in the bootloader's linker script too — and keep a ring buffer there with a magic, a version and size field, and a CRC per entry. The fault handler only writes and seals an entry, then resets; a low-priority task on the next boot validates, ships it to flash or telemetry, and clears it. I've seen two ways this silently fails: the address moves when .bss grows, and a bootloader's stack overwrites the top of RAM. On ECC SRAM you also have to read the reset cause first and scrub the window on power-on, because reading it uninitialized faults.

**Q.** "Why must ECC SRAM be initialized before use, and how do you do it?"

**A.** 전원 투입 직후 ECC 비트도 쓰레기라 읽으면 약 2/3가 정정 불가 fault, 1/3은 틀린 값으로 "정정"된다(SECDED 모델로 계산). 바이트 쓰기도 RMW라 읽기를 일으킨다. 그래서 스택을 쓰기 전에 어셈블리로 granule 폭 이상의 정렬된 쓰기(`stm`, `strd`)로 전체를 채우고, 캐시는 그 뒤에 켠다. 시간은 크기에 비례해 수 MB면 수십 ms라 부팅 예산에 넣는다.

> After power-up both the data and the check bits are random, so a read is usually an uncorrectable error and sometimes a silent mis-correction. Sub-word stores are read-modify-writes inside the memory controller, so even a byte store or a C function prologue can trip it. So the very first thing Reset_Handler does, before touching the stack, is fill the ECC regions with aligned stores at least as wide as the ECC granule — STM or STRD — with the D-cache still off. On a warm reset I skip the retained window. It scales linearly with size, so I budget it in boot time.

**Q.** "How would you split a FreeRTOS heap across two RAM banks?"

**A.** heap_5의 `vPortDefineHeapRegions()`에 주소 오름차순 영역 표를 주고, 어떤 할당보다도 먼저 부른다. 할당은 주소 순 first-fit이라 낮은 뱅크부터 쓴다. 하지만 위치 보장은 아니므로 위치가 중요한 객체는 정적 할당이나 뱅크별 풀로 둔다.

> heap_5 takes an array of regions in ascending address order, terminated by a NULL entry, and you call vPortDefineHeapRegions before anything allocates — including queues the kernel creates. It's first-fit over an address-ordered free list, so allocations naturally fill the lower bank first; in my test a 40 KB request spilled to PSRAM only when the on-chip bank couldn't fit it. That's not a placement guarantee, though, so anything that must be in fast or DMA-capable memory is statically allocated or comes from a per-bank pool.

**Q.** "In Zephyr, how do you place a variable or a whole file in a specific memory region?"

**A.** 변수는 DT의 `zephyr,memory-region` 노드 + `LINKER_DT_NODE_REGION_NAME()` 섹션 속성 — 단 NOLOAD라 초기화가 없다. 파일은 `zephyr_code_relocate(FILES … LOCATION X_TEXT/_DATA/_BSS)` — 생성된 코드가 복사 · 0 채우기를 한다. TCM 매크로는 보드에 `chosen` `zephyr,itcm` / `zephyr,dtcm`이 있을 때만 의미가 있다.

> For a buffer, I describe the RAM in devicetree as a zephyr,memory-region node and put the variable in that section with LINKER_DT_NODE_REGION_NAME — but those sections are NOLOAD, so an initializer is silently dropped; I've seen it read zero in QEMU. For a whole file, zephyr_code_relocate in CMake generates linker fragments plus copy and zero routines that the boot path calls. And the ITCM/DTCM section-tag macros only work if the board sets the chosen nodes — otherwise you get an orphan-section warning and the data lands in flash.

**Q.** "Your board works under the debugger but fails after a power cycle. Where do you look?"

**A.** 디버거 `load`는 `.data`를 VMA에 직접 써 주고 RAM을 이미 쓰인 상태로 둔다. 그래서 (1) copy table 누락, (2) `.bss`/noinit 0 가정, (3) ECC scrub 누락, (4) 클럭 · 플래시 wait state 순서(B5)를 본다. map에서 LMA ≠ VMA인 섹션이 모두 표에 있는지 확인한다.

> A debugger load writes initialized data straight to its run address and leaves RAM in a used state, which hides startup bugs. I check that every section with a load address different from its run address is in the copy table, that nothing assumes zeroed no-init memory, and on ECC parts that the scrub runs before the stack is used. Then I test from a true power-on, not a debugger reset.

---

## 13. 직접 해보기

1. **개념** — 예제 2의 `.ram_text`가 `wlen=0`인데 copy table에 남아 있다. 이것이 문제가 되는 경우가 있나? 반대로 섹션 끝 `ALIGN(4)`를 지우고 `.psram_data`에 `uint8_t` 3바이트만 두면 어떻게 되나?
   정답/힌트: 빈 줄은 무해하다(루프 0회). 3바이트면 `SIZEOF/4 = 0`이라 복사가 안 된다 — 직접 빌드해 `g_lut` 값을 확인해 보라.
2. **코드** — 예제 3에 `__attribute__((long_call))` 없이 `-mlong-calls` 옵션으로 전체를 빌드하고, `f_flash` 호출까지 `blx`가 되는지, 명령 수가 어떻게 바뀌는지 재라.
   정답/힌트: `-mlong-calls`는 모든 호출을 간접으로 만든다. 같은 영역 안의 호출에도 비용이 붙는다 — 그래서 함수 단위 속성이 낫다.
3. **코드** — 예제 4의 kfast 빌드에서 FAST 쪽 veneer 5개(`memcpy`, `board_now_us` …)를 없애라. `*libc_nano.a:lib_a-memcpy*.o(.text*)` 같은 아카이브 멤버 패턴과 `board.o`의 해당 함수 섹션을 `.fast_text`로 끌어오면 된다.
   정답/힌트: 아카이브 멤버 패턴은 `archive:member` 문법. map에서 `memcpy`가 어느 멤버에 있는지 먼저 찾는다.
4. **코드** — 예제 6의 크래시 로그를 **항목마다 CRC**로 바꾸고, 1비트 손상 시 그 항목만 버려지는지 확인하라. 버전 필드를 2로 올린 빌드를 리셋 사이에 바꿔 끼울 수는 없으니(QEMU), `version` 필드를 직접 바꾸는 단계를 `switch`에 추가해 "layout changed" 경로를 시험하라.
   정답/힌트: `entry_t`에 `crc`를 넣고 `seal`을 항목 단위로. 헤더 CRC는 `boots` · `head`만 덮는다.
5. **측정** — 예제 11에 "끝이 정렬 안 된 영역"(시작 + 3바이트)을 추가해 `memset`이 몇 개의 `strb`를 하는지 `objdump -d`로 newlib-nano `memset`을 읽고 추정하라. ECC RAM이라면 그 바이트 쓰기가 몇 번의 RMW가 되나?
   정답/힌트: 머리 · 꼬리 각각 최대 3바이트. 32비트 granule이면 머리 1워드, 꼬리 1워드의 RMW.
6. **Zephyr** — 예제 12에 overlay를 추가해 AN547의 `isram`을 `chosen` `zephyr,dtcm`으로 지정해 보고(DTCM이 아니지만 실험용), `__dtcm_bss_section` 변수가 0으로 지워지는지, 빌드가 `zephyr,sram`과의 겹침 때문에 무엇을 경고하는지 보라.
   정답/힌트: `arch_bss_zero()`의 `DT_NODE_HAS_STATUS_OKAY(DT_CHOSEN(zephyr_dtcm))` 분기가 켜진다. 생성된 `linker.cmd`에서 `GROUP_START(DTCM)`를 찾아라.
7. **설계** — 예제 14의 예산에서 ITCM 64 KB를 FOC ISR(4 KB), `arm_math` 핵심 함수(12 KB), 커널 핫패스(6 KB)로 채운다면 DTCM은 무엇으로 채우겠나? 각 결정의 근거를 §1.1의 다섯 성격으로 한 줄씩 써라.
   정답/힌트: 스택(실측 high water + 여유), ISR 스택, 필터 상태 · LUT, 커널 ready list는 TCB 안에 있으므로 TCB를 DTCM에.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| TCM (ITCM / DTCM) | Tightly Coupled Memory | 캐시가 아닌 코어 전용 SRAM, 0 wait · 결정적 (B8) |
| FlexRAM | i.MX RT의 재분할 가능 RAM | 32 KB 뱅크마다 ITCM · DTCM · OCRAM 지정 (GPR17) |
| backup SRAM | VBAT로 유지되는 작은 SRAM | H7은 4 KB @ 0x3880_0000 |
| section attribute | `__attribute__((section("…")))` | 객체를 지정 입력 섹션에 넣는다 |
| 파일 패턴 | `*hot.o(.text*)` | 소스 수정 없이 오브젝트 단위 배치 |
| `EXCLUDE_FILE` | 링커 스크립트 키워드 | 와일드카드에서 특정 파일을 뺀다 |
| orphan section | 스크립트에 규칙이 없는 입력 섹션 | 링커가 임의 위치에 둔다 (경고) |
| copy / zero table | `{src, dest, wlen}` · `{dest, wlen}` 표 | startup이 영역 수와 무관하게 같은 루프로 초기화 |
| `__cmsis_start()` | CMSIS-6 기본 startup 함수 | 표를 돌고 `_start()` 호출 |
| veneer | 링커가 만든 점프대 | 분기 범위를 넘는 호출을 이어 준다 |
| `long_call` | GCC Arm 함수 속성 | 주소를 레지스터로 로드해 `blx` |
| ramfunc | RAM에서 실행되는 함수 | VMA = RAM, LMA = 플래시, 복사 필요 |
| noinit | startup이 초기화하지 않는 영역 | 웜 리셋을 넘어 값 유지 |
| NOLOAD | 링커 섹션 타입 | ELF에 내용 없음, `ALLOC`만 |
| ECC granule | ECC가 붙는 데이터 단위 | 32비트 또는 64비트, 이보다 작은 쓰기는 RMW |
| SECDED | Single Error Correct, Double Error Detect | 1비트 정정, 2비트 검출 부호 |
| scrub (초기화) | ECC RAM 전체를 한 번 쓰기 | 부팅 시 첫 읽기 전에 (주기적 scrub은 BM Q7) |
| RMW | read-modify-write | 부분 쓰기가 내부적으로 읽기를 포함 |
| heap_5 | FreeRTOS 다중 영역 힙 | `vPortDefineHeapRegions()`, 주소 순 first-fit |
| `zephyr,memory-region` | Zephyr DT compatible | 링커 MEMORY 영역 + NOLOAD 섹션 생성 |
| `zephyr_code_relocate` | Zephyr CMake 함수 | 파일 단위 섹션 재배치 + 복사 코드 생성 |
| `chosen` | DT의 특별 노드 | `zephyr,sram` · `zephyr,flash` · `zephyr,dtcm` 등 역할 지정 |
| 메모리 예산 | 영역별 사용량 표 | CI에서 상한 검사 |

---

## 15. 요약 & 체크리스트

고성능 MCU의 RAM은 여러 덩어리이고, 덩어리마다 지연 · 캐시 · 버스 마스터 · 유지 · ECC가 다르다. 배치는 이 다섯 성격으로 정하고, section attribute · 파일 패턴 · copy/zero 표로 링커에게 알리고, map · `nm` · `objdump`로 증명한다. 코드를 RAM으로 옮기면 veneer가 생기며 호출 경로 전체를 같이 옮기거나 `long_call`을 써야 한다(호출당 1명령 실측). noinit 창은 고정 주소 영역으로 만들고 부트로더와 공유 계약을 맺으며, magic · 버전 · 크기 · CRC로 검증한다. ECC RAM은 스택보다 먼저, granule 폭 이상의 정렬 쓰기로, 캐시를 켜기 전에 scrub하고, warm reset이면 noinit 창을 건너뛴다. FreeRTOS는 배치를 모르므로 heap_5 영역 표와 정적 할당으로 앱이 정하고, Zephyr는 DT 영역(NOLOAD — 초기화 없음), 섹션 태그(chosen 필요), `zephyr_code_relocate`(초기화 생성)로 정한다.

- [ ] 새 칩의 RAM마다 다섯 성격(지연 · 캐시 · DMA · 유지 · ECC) 표를 RM에서 만들 수 있다
- [ ] ISR · 스택 · DMA 버퍼 · 힙 · 크래시 로그의 위치를 근거와 함께 말할 수 있다
- [ ] CMSIS-6 `__copy_table_start__` 형식(워드 단위)으로 영역을 하나 추가할 수 있다
- [ ] 파일 패턴과 `EXCLUDE_FILE`로 서드파티 코드를 소스 수정 없이 옮길 수 있다
- [ ] veneer가 왜 생기는지, `long_call`과 무엇이 다른지 objdump로 보여 줄 수 있다
- [ ] heap_5 영역 표의 규칙과 first-fit의 결과를 설명할 수 있다
- [ ] 커널이 제공하는 idle · timer 스택이 어디로 가는지 확인하고 옮길 수 있다
- [ ] 고정 주소 noinit 크래시 로그를 설계하고 부트로더와의 충돌을 피할 수 있다
- [ ] ECC RAM이 왜 초기화를 요구하는지(읽기 · RMW · 캐시 line fill) 설명할 수 있다
- [ ] 스택을 쓰지 않는 어셈블리 scrub 루프를 쓰고 그 시간을 예산에 넣을 수 있다
- [ ] Zephyr DT 영역 섹션이 초기화되지 않는다는 것과 `chosen` 없는 섹션 태그의 결과를 안다
- [ ] 링크 결과에서 영역별 메모리 예산을 뽑을 수 있다

---

## Bare-Metal 노트와의 관계

| BM ID | BM 주제 | 이 노트의 해당 절 | Bare-Metal 쪽에 남길 내용 |
|---|---|---|---|
| BM P2 | 실행 위치 최적화 (flash vs RAM vs TCM 실행, 캐시 켜기, 정렬, ramfunc) | §1.6, §1.7, §3 (예제 3 · 4) | 이 노트를 링크하고, 실칩에서 DWT CYCCNT로 flash · ITCM 실행 사이클과 지터를 재는 실습만 더한다 |
| BM G6 | 저장 클래스 · 배치 (const → flash, static, 섹션 지정, 초기화 순서, ROM 테이블) | §2 (섹션 지정 · copy/zero 표, 예제 2) | const → flash, `static`의 저장 기간, C 초기화 순서, ROM 테이블 같은 C 저장 클래스 기초 |
| BM Q7 | 메모리 오류 · ECC (SRAM ECC 초기화, 스크러빙, 단일/이중 비트 오류 처리, SEU) | §6 (예제 9 · 10 · 11) | 주기적 백그라운드 스크러빙, 단일/이중 비트 오류 인터럽트 처리, 오류 주소 로깅, SEU · 방사선 환경 |

---

## 참고 자료

- FreeRTOS Kernel V11.3.2 소스: `portable/MemMang/heap_5.c`(영역 규칙 머리 주석, `vPortDefineHeapRegions`), `tasks.c`(`xTaskCreateStatic`, `prvInitialiseNewTask`, `configKERNEL_PROVIDED_STATIC_MEMORY`의 `vApplicationGetIdleTaskMemory` · `vApplicationGetTimerTaskMemory`), `portable/GCC/ARM_CM3/port.c`, `portable/GCC/ARM_CM7/r0p1/port.c`
- FreeRTOS 문서 "Memory Management" (heap_1 ~ heap_5 설명 페이지, freertos.org)
- Zephyr v4.4.2 소스: `include/zephyr/linker/section_tags.h`, `include/zephyr/linker/devicetree_regions.h`, `include/zephyr/toolchain/gcc.h`(`__ramfunc`), `include/zephyr/arch/arm/cortex_m/scripts/linker.ld`, `arch/common/xip.c`(`arch_data_copy`), `arch/common/init.c`(`arch_bss_zero`, `arch_early_memset`), `arch/arm/core/cortex_m/prep_c.c`(`z_prep_c`), `arch/arm/core/cortex_m/reset.S`, `soc/st/stm32/stm32h7x/soc_m7.c`(`soc_reset_hook` TCM ECC), `drivers/misc/nxp_flexram/`, `include/zephyr/drivers/misc/flexram/nxp_flexram.h`, `scripts/build/gen_relocate_app.py`, `tests/application_development/code_relocation/`
- Zephyr v4.4.2 DTS: `dts/arm/st/h7/stm32h7.dtsi`, `dts/arm/nxp/imxrt/nxp_rt10xx.dtsi`, `dts/arm/nxp/imxrt/nxp_rt1060.dtsi`, `dts/bindings/memory-controllers/nxp,flexram.yaml`, `boards/arm/mps3/mps3_corstone300_an547.dts`, `boards/nxp/mimxrt1060_evk/mimxrt1060_evk.dtsi`, `boards/st/nucleo_h743zi/nucleo_h743zi.dts`
- Zephyr 문서 "Code And Data Relocation": https://docs.zephyrproject.org/latest/kernel/code-relocation.html
- CMSIS-6 (Zephyr 모듈 `modules/hal/cmsis_6`, 커밋 30a859f): `CMSIS/Core/Include/m-profile/cmsis_gcc_m.h`(`__cmsis_start`), `CMSIS/Core/Template/Device_M/Config/Device_gcc.ld`
- ST AN5342 "How to use error correction code (ECC) management for internal memories protection on STM32 MCUs" — 이 노트에서는 원문을 읽지 않았고 Zephyr `soc_m7.c` 주석의 인용(Rev. 7 §3.1.1)으로만 참조했다
- ST RM0433 (STM32H742/743/750/753 reference manual) — 버스 매트릭스 · backup SRAM 유지 조건은 원문 확인 필요 (B8과 같은 한계)
- Arm Cortex-M3 Technical Reference Manual, 명령 타이밍 표 — 그림 5의 사이클 모델 근거
- GNU ld 매뉴얼: "Input Section Basics"(`EXCLUDE_FILE`, `archive:member`), "Output Section Type"(NOLOAD), "Orphan Sections", `--orphan-handling`
- GCC 매뉴얼: ARM Function Attributes(`long_call`), `-mlong-calls`
- QEMU 10.2 `info mtree` (mps2-an385, mps3-an547) — 이 노트에서 직접 실행
- 이 노트 예제 소스: `~/.rtos_scratch/F8/` (빌드 산출물 VM `/var/tmp/rtos/F8/`)
