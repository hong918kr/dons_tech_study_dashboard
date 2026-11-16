---
id: TOOL-M13
time: 80분
updated: 2026-11-15
---
# TOOL-M13 코드 크기 다이어트: gc-sections·LTO·-Os

## 학습 목표

- 하나의 Cortex-M4 펌웨어에 `-Os` → `-ffunction-sections -fdata-sections` + `--gc-sections` → `-flto` → newlib-nano → printf 제거를 차례로 적용하고, 단계마다 text/data/bss 와 **QEMU 부팅 결과**를 실측 표로 만든다 (36172 B → 2496 B).
- `--gc-sections` 가 "함수" 가 아니라 **입력 섹션(input section)** 을 지우는 마크-스윕(mark & sweep)이라는 것, 그래서 `-ffunction-sections` 없이는 거의 아무것도 못 지우고 `KEEP()` 없이는 벡터 표까지 지운다는 것을 실제 `--print-gc-sections` 출력으로 설명한다.
- LTO 객체는 기계어가 아니라 GIMPLE(IR)이라 **아카이버도 LTO 플러그인을 알아야** 한다는 것(`ar` vs `arm-none-eabi-gcc-ar`), 맥과 Ubuntu 에서 증상이 왜 다른지 실측으로 안다.
- `-g` 는 Flash 를 0 B 먹는다(`.bin` 바이트까지 동일), newlib-nano 는 `%f` 를 조용히 못 찍는다 — "작아졌다" 와 "똑같이 동작한다" 를 따로 검증하는 습관.
- 맵 파일 두 개를 파일(.o / 아카이브 멤버) 단위로 diff 해서 "누가 몇 바이트 줄었나" 를 말하고, 링커 MEMORY 영역으로 예산을 강제한다.

## 1. 문제

"Flash 가 2 KB 모자랍니다. 기능은 못 빼요. 어떻게 하죠?" — 펌웨어 팀에서 분기마다 나오는 질문이고, 면접에서는 "gc-sections 가 뭘 지우는지", "LTO 를 왜 안 켜는 팀이 있는지", "printf 하나에 왜 6 KB 가 붙는지" 로 나온다. SSD 컨트롤러 펌웨어처럼 ROM 이 칩에 박혀 있으면 1 KB 가 곧 마스크 리스핀이다. 플래그를 외우는 게 아니라 **각 기법이 링커 안에서 무엇을 하는지** 알아야 "왜 이번엔 안 줄었지?" 에 답한다.

재료는 `fw/` 의 작은 Cortex-M4 펌웨어다 (QEMU `mps2-an386` 메모리 맵: Flash 0x00000000, RAM 0x20000000). 부팅하면 UART 로 7줄을 찍고 세미호스팅으로 끝난다. 이 7줄(`golden_uart.txt`)이 **모든 단계의 정답**이다 — 작아졌는데 다른 걸 찍으면 다이어트가 아니라 버그다.

| 파일 | 내용 | 다이어트에서의 역할 |
|---|---|---|
| `fw/startup.c` | 벡터 표 24칸(`.isr_vector`), Reset_Handler, SysTick, 세미호스팅 종료 | 아무도 참조하지 않는 벡터 표 → `KEEP` 필요 |
| `fw/main.c` | 부팅 로그 7줄 (`g_boot_count = 7` 은 .data) | golden 출력의 출처 |
| `fw/crc.c` | CRC-32 1 KB 룩업 테이블 + 안 쓰는 `crc16_ccitt`, `crc32_bitwise` | 죽은 코드, 테이블 크기 트레이드오프 |
| `fw/fmt.c` | printf 대신 쓰는 작은 포매터 (+ 안 쓰는 `log_hexdump`) | printf 제거 단계 |
| `fw/uart.c`, `fw/sensor.c` | `libdrv.a` 로 묶이는 드라이버. 안 쓰는 `uart_read_line`+`g_dma_rx[2048]`, `sensor_selftest`, `g_cal_table[64]` | .bss 2 KB / .data 256 B 죽은 데이터, 아카이버 함정 |
| `fw/libc_min.c` | `-nostdlib` 빌드용 memcpy/memset | 맥 gcc 16 에는 newlib 이 없다 |

<!-- code: fw/main.c -->

두 툴체인을 쓴다. 채점(`check.sh`)은 **맥** `arm-none-eabi-gcc 16.2.0` (newlib 없음 → `-nostdlib` + `fmt.c`) 과 맥 `qemu-system-arm 11.1.3` 으로 1초 안에 끝난다. newlib / newlib-nano / printf 가 들어가는 단계 표는 **Lima VM** `arm-none-eabi-gcc 14.2.1` + `qemu-system-arm 10.2.1` 에서 `demo_stages.sh` 로 잰다. 아래 그림의 숫자가 그 실측이다 (`stages_vm.txt`).

```svg
<svg viewBox="0 0 740 312" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Flash = text + data, 단계별 (VM arm-none-eabi-gcc 14.2.1, 모두 QEMU 출력 = golden)</text>
  <text x="10" y="50" fill="currentColor">S0 -O0, newlib</text>
  <rect x="150" y="38" width="520" height="18" class="f-warn" stroke="currentColor"/>
  <text x="676" y="51" fill="currentColor" font-size="12">36172</text>
  <text x="10" y="82" fill="currentColor">S1 -Os</text>
  <rect x="150" y="70" width="507" height="18" class="f-warn" stroke="currentColor"/>
  <text x="663" y="83" fill="currentColor" font-size="12">35260</text>
  <text x="10" y="114" fill="currentColor">S2 +sections+gc</text>
  <rect x="150" y="102" width="476" height="18" class="f-warn" stroke="currentColor"/>
  <text x="632" y="115" fill="currentColor" font-size="12">33100</text>
  <text x="10" y="146" fill="currentColor">S3 +-flto</text>
  <rect x="150" y="134" width="474" height="18" class="f-warn" stroke="currentColor"/>
  <text x="630" y="147" fill="currentColor" font-size="12">32976</text>
  <text x="10" y="178" fill="currentColor">S4 newlib-nano</text>
  <rect x="150" y="166" width="120" height="18" class="f-acc" stroke="currentColor"/>
  <text x="276" y="179" fill="currentColor" font-size="12">8368  (−24608, 가장 큰 한 방)</text>
  <text x="10" y="210" fill="currentColor">S5 printf→fmt.c</text>
  <rect x="150" y="198" width="40" height="18" class="f-ok" stroke="currentColor"/>
  <text x="196" y="211" fill="currentColor" font-size="12">2768</text>
  <text x="10" y="242" fill="currentColor">S6 -nostdlib</text>
  <rect x="150" y="230" width="36" height="18" class="f-ok" stroke="currentColor"/>
  <text x="192" y="243" fill="currentColor" font-size="12">2496   (RAM 4484 → 80)</text>
  <text x="10" y="278" fill="currentColor" font-size="12">막대 ∝ 바이트 (520 px = 36172 B). -O 플래그(S0→S1)는 −912 B 뿐: newlib 은 이미 컴파일된 .a 라 -O 가 안 듣는다.</text>
  <text x="10" y="300" fill="currentColor" font-size="12">gc(S1→S2) −2160, LTO(S2→S3) −124, nano(S3→S4) −24608, printf 제거 −5600, newlib 제거 −272.</text>
</svg>
```

### Part A — gc-sections 를 제대로: 섹션 단위, 뿌리, 확인

- `check.sh` 는 variant 의 `<v>.flags` (CFLAGS / LDFLAGS / AR) 와 `<v>.ld` 로 펌웨어를 빌드한다. 예산: **text ≤ 2816, data ≤ 32, bss ≤ 256** (B).
- 죽은 함수 6개(`crc16_ccitt`, `crc32_bitwise`, `sensor_selftest`, `sensor_calibrate`, `uart_read_line`, `log_hexdump`) + 죽은 전역 3개(`crc16_table`, `g_cal_table`, `g_dma_rx`)가 `nm` 에서 사라져야 하고, `--print-gc-sections` 가 실제로 per-function 섹션을 8개 이상 지운 것을 보여야 한다.
- 왜 `-ffunction-sections` 가 필요한지 `readelf -S crc.o` 로 설명할 수 있어야 한다.

### Part B — 함정 (starter 에 심어 둔 4개)

- **gc-sections 만 켜고 `-ffunction-sections -fdata-sections` 는 없음** → 지울 단위가 파일 하나라 죽은 코드가 그대로 (text 2896, data 260, bss 2124).
- **`AR = ar`** (맥의 Apple ar) → ELF 를 못 읽어 심볼 색인이 빈 아카이브, 종료 코드는 0. LTO 를 켜면 `arm-none-eabi-ar` 도 같은 꼴이 된다 (플러그인 없음).
- **링커 스크립트에 `KEEP(*(.isr_vector))` 가 없음** → gc 가 벡터 표를 지우고 QEMU 는 `Lockup` 으로 죽는다.
- **`-g0` 로 "Flash 절약"** → 0 B 절약, 대신 현장 크래시 주소를 줄 번호로 못 바꾼다 (TOOL-M14).

### Part C — 펌웨어 현실: 단계 표, 맵 diff, 예산 강제

- VM 에서 newlib 단계 포함 전 단계를 빌드 + QEMU 로 출력 검증 (`demo_stages.sh`), 맵 파일 두 개를 파일 단위로 diff.
- newlib-nano 의 `%f` 함정과 `-u _printf_float` 비용, printf 를 작은 포매터로 바꾼 효과.
- 연습: Flash 2 KB 부품으로 이사 — LTO + 64 B CRC 표, `MEMORY` 의 LENGTH 로 링커가 예산을 강제.

## 2. Problem (English)

> **Put this Cortex-M4 firmware on a diet without changing what it does.**
>
> The release build of `fw/` (Cortex-M4, linked with `-nostdlib` against a linker script, drivers packaged as `libdrv.a`) is over budget. You may change **only** the build configuration: `starter.flags` (`CFLAGS`, `LDFLAGS`, `AR`) and `starter.ld`. Sources are frozen.
>
> Requirements, enforced by `bash check.sh <variant>`:
>
> - `text ≤ 2816`, `data ≤ 32`, `bss ≤ 256` bytes (`arm-none-eabi-size`).
> - The archive index of `libdrv.a` must list its members' symbols.
> - The vector table is at 0x0, word 0 = `_estack`, word 1 = `Reset_Handler | 1`; every handler survives.
> - Dead code/data (`crc16_ccitt`, `sensor_selftest`, `g_dma_rx`, …) is gone, and `--print-gc-sections` shows it was the linker that removed it.
> - Debug info stays in the ELF (we symbolicate field crashes); `fw.bin` must equal text + data.
> - The image boots in `qemu-system-arm -M mps2-an386` and prints exactly `golden_uart.txt`.
>
> Example: the starter config prints `3/13 passed` — `text 2896 > 2816`, `bss 2124 > 256`, and QEMU dies with `Lockup: can't escalate 3 to HardFault`.
>
> Follow-ups: what are the GC roots? Why doesn't `-g` cost Flash? Your CI on Ubuntu links fine with plain `ar` and `-flto`, a colleague's Mac does not — why? What would you do next if you needed another 6 KB (newlib, printf)?

## 3. 시작 코드

채점기 `check.sh` (빌드·검사·QEMU 전부 맥, 1초 내외)와 고칠 파일 두 개 `starter.flags`, `starter.ld`:

<!-- code: check.sh -->

<!-- code: starter.flags -->

<!-- code: starter.ld -->

```bash
bash check.sh starter      # 고칠 대상
bash check.sh solution     # 13/13
bash check.sh practice     # 16/16 (9. 추가 연습)
bash demo_stages.sh        # (선택) VM: newlib 단계 표 → stages_vm.txt, mapdiff_vm.txt, float_vm.txt, gc_trap_vm.txt
bash demo_ar.sh            # (선택) 아카이버 6+3 경우 → ar_mac.txt, ar_vm.txt
bash demo_mac.sh           # (선택) -g 표, KEEP 2×2, region overflow → mac_*.txt
```

처음 실행 결과 (실제 출력):

```text
CFLAGS =-Os -g0
LDFLAGS=-Wl,--gc-sections
AR     =ar   ld=starter.ld   budget: text<=2816 data<=32 bss<=256
FAIL libdrv.a index has no uart_init/sensor_push (ar: ranlib: warning: archive member 'uart.o' not a mach-o file) — re-archiving with arm-none-eabi-gcc-ar to keep grading
PASS link fw.elf
     size: text=2896 data=260 bss=2124
FAIL vector table: .isr_vector addr=00000000 size=000000, word0=0x68134a02 (want _estack 0x20004000) word1=0x60133301 (want 0x00000025)
PASS ISR symbols present: Reset_Handler HardFault_Handler SysTick_Handler Default_Handler
FAIL --gc-sections removed .isr_vector (no KEEP in starter.ld): .isr_vector  startup.o
FAIL --gc-sections removed only 0 per-function sections (need >= 8):  1 .ARM.attributes; 1 .comment; 1 .isr_vector; 1 .text;
FAIL dead symbols still in fw.elf: crc16_ccitt crc32_bitwise crc16_table sensor_selftest sensor_calibrate g_cal_table uart_read_line g_dma_rx log_hexdump
FAIL text 2896 > 2816 (over by 80)
FAIL data 260 > 32 (over by 228)
FAIL bss 2124 > 256 (over by 1868)
FAIL no .debug_line in fw.elf — -g0 saves 0 B of Flash, only makes field crashes unreadable
PASS flash image fw.bin = text+data = 3156 B (fw.elf file = 11028 B)
FAIL QEMU boot: rc=134, qemu: fatal: Lockup: can't escalate 3 to HardFault (current priority -, uart: 
3/13 passed
```

- 아카이브 색인이 비면 링크가 안 되어 나머지가 전부 "링크 실패" 로 묻힌다. 그래서 checker 는 FAIL 을 찍은 뒤 `arm-none-eabi-gcc-ar` 로 다시 묶어서 **나머지 함정을 하나씩 따로** 보여 준다.
- "ISR symbols present" 가 starter 에서 PASS 인 건 운이다: `-ffunction-sections` 가 없으니 핸들러들이 `Reset_Handler`(ENTRY) 와 같은 `startup.o` 의 `.text` 한 덩어리에 있어서 같이 살아남았다. 섹션을 쪼개는 순간 이 운이 사라진다 (5. 풀이 Part B, 2×2 표).

## 4. 힌트

<details><summary>힌트 1 — 링커는 "함수" 를 모른다</summary>

`--gc-sections` 의 지우는 단위는 **입력 섹션**이다. `arm-none-eabi-readelf -SW crc.o` 를 `-ffunction-sections` 유무로 비교해 보라. 섹션 하나에 산 함수와 죽은 함수가 같이 있으면, 산 함수 하나 때문에 섹션 전체가 산다. 데이터(`.data`, `.bss`)도 똑같다 → `-fdata-sections`.
</details>

<details><summary>힌트 2 — 마크는 어디서 시작하나</summary>

gc 는 뿌리(root)에서 재배치(relocation)를 따라가며 표시하고, 표시 안 된 섹션을 버린다. 뿌리는 `ENTRY(Reset_Handler)`, `-u`/`--undefined` 로 준 심볼, 그리고 링커 스크립트의 `KEEP(...)`. 벡터 표 `vectors[]` 를 참조하는 코드는 하나도 없다 — CPU 가 리셋 때 주소 0 을 읽을 뿐. `__attribute__((used))` 는 **컴파일러**에게만 하는 말이다.
</details>

<details><summary>힌트 3 — 아카이버가 읽을 수 있는 형식</summary>

`"$AR" rcs libdrv.a uart.o sensor.o` 다음에 `arm-none-eabi-nm -s libdrv.a` 의 `Archive index:` 를 보라. 색인은 아카이버가 멤버를 **읽어서** 만든다. Apple `ar` 는 Mach-O 만 읽는다. GNU `arm-none-eabi-ar` 는 ELF 는 읽지만, LTO 객체 안의 GIMPLE 심볼표는 플러그인이 있어야 읽는다. 둘 다 읽어 주는 건 `arm-none-eabi-gcc-ar` (gcc 의 `liblto_plugin.so` 를 넘겨 주는 래퍼).
</details>

<details><summary>힌트 4 — -g 의 비용은 어디에</summary>

`readelf -SW fw.elf | grep debug` 의 Address 칸과 Flags 칸을 보라. 주소 0, `A`(alloc) 플래그 없음 = 칩에 안 올라간다. `objcopy -O binary` 결과를 `-g0` 과 `-g3` 로 `cmp` 해 보면 답이 나온다.
</details>

<details><summary>힌트 5 — 고칠 줄은 딱 네 군데</summary>

`starter.flags` 의 CFLAGS(섹션 플래그 + `-g`), AR, `starter.ld` 의 `.isr_vector` 한 줄. LDFLAGS 는 이미 맞다.
</details>

## 5. 풀이

### Part A — gc-sections 는 섹션 그래프의 마크-스윕

링커 입장에서 프로그램은 **입력 섹션들과 그 사이의 재배치(relocation) 화살표**로 된 그래프다. `--gc-sections` 는 뿌리에서 화살표를 따라 닿는 섹션을 표시(mark)하고, 표시 안 된 할당(ALLOC) 섹션을 출력에서 빼 버린다(sweep). 함수 이름도, C 의 "호출" 도 모른다 — 오직 "이 섹션 안 어딘가가 저 섹션 안 어딘가를 가리킨다" 만 안다.

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">solution: 뿌리 → 재배치 화살표 → 표시. 표시 안 된 섹션은 버린다 (--print-gc-sections)</text>
  <rect x="10" y="40" width="150" height="34" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="18" y="55" fill="currentColor" font-size="12">뿌리: ENTRY</text>
  <text x="18" y="69" fill="currentColor" font-size="12" font-family="monospace">Reset_Handler</text>
  <rect x="10" y="100" width="150" height="34" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="18" y="115" fill="currentColor" font-size="12">뿌리: KEEP(...)</text>
  <text x="18" y="129" fill="currentColor" font-size="12" font-family="monospace">.isr_vector</text>
  <rect x="230" y="40" width="180" height="26" class="f-ok" stroke="currentColor"/>
  <text x="238" y="58" fill="currentColor" font-size="12" font-family="monospace">.text.startup.main</text>
  <rect x="230" y="100" width="180" height="26" class="f-ok" stroke="currentColor"/>
  <text x="238" y="118" fill="currentColor" font-size="12" font-family="monospace">.text.SysTick_Handler</text>
  <rect x="230" y="140" width="180" height="26" class="f-ok" stroke="currentColor"/>
  <text x="238" y="158" fill="currentColor" font-size="12" font-family="monospace">.text.HardFault_Handler</text>
  <rect x="470" y="40" width="130" height="26" class="f-ok" stroke="currentColor"/>
  <text x="478" y="58" fill="currentColor" font-size="12" font-family="monospace">.text.crc32</text>
  <rect x="470" y="80" width="170" height="26" class="f-ok" stroke="currentColor"/>
  <text x="478" y="98" fill="currentColor" font-size="12" font-family="monospace">.rodata.crc32_table</text>
  <rect x="610" y="40" width="120" height="26" class="f-ok" stroke="currentColor"/>
  <text x="618" y="58" fill="currentColor" font-size="12" font-family="monospace">.text.log_printf</text>
  <path d="M160 57 L228 53" stroke="currentColor" fill="none" stroke-width="1.5"/>
  <path d="M160 117 L228 113" stroke="currentColor" fill="none" stroke-width="1.5"/>
  <path d="M160 122 L228 152" stroke="currentColor" fill="none" stroke-width="1.5"/>
  <path d="M410 53 L468 53" stroke="currentColor" fill="none" stroke-width="1.5"/>
  <path d="M535 66 L545 78" stroke="currentColor" fill="none" stroke-width="1.5"/>
  <path d="M410 47 Q510 25 608 47" stroke="currentColor" fill="none" stroke-width="1.5"/>
  <text x="10" y="200" fill="currentColor" font-weight="bold">표시 안 됨 → 제거 (실제 13개 중 일부)</text>
  <rect x="10" y="212" width="150" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="18" y="230" fill="currentColor" font-size="12" font-family="monospace">.text.crc16_ccitt</text>
  <rect x="170" y="212" width="160" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="178" y="230" fill="currentColor" font-size="12" font-family="monospace">.rodata.crc16_table</text>
  <rect x="340" y="212" width="190" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="348" y="230" fill="currentColor" font-size="12" font-family="monospace">.bss.g_dma_rx (2048)</text>
  <rect x="540" y="212" width="190" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="548" y="230" fill="currentColor" font-size="12" font-family="monospace">.data.g_cal_table (256)</text>
  <text x="10" y="272" fill="currentColor" font-weight="bold">starter: -ffunction-sections 없음 → 파일당 .text 한 덩어리</text>
  <rect x="10" y="284" width="330" height="30" class="f-warn" stroke="currentColor"/>
  <text x="18" y="304" fill="currentColor" font-size="12" font-family="monospace">crc.o .text (0xa0) = crc32 + crc16 + bitwise</text>
  <text x="352" y="304" fill="currentColor" font-size="12">crc32 하나가 살아 있으니 전부 산다</text>
  <text x="10" y="340" fill="currentColor" font-size="12">KEEP 이 없으면 .isr_vector 도 "아무도 안 가리키는 섹션" → 제거 → 핸들러들도 줄줄이 제거 (아래 2×2 표).</text>
</svg>
```

**섹션 단위의 증거** — `gc_trap_vm.txt` (VM, 실제; 칸: 이름 타입 주소 파일오프셋 크기 ES):

```text
$ arm-none-eabi-readelf -SW crc.o | grep -E " \.(text|rodata)"    # -Os
.text PROGBITS 00000000 000034 0000a0 00
.rodata PROGBITS 00000000 0000d4 000420 00
$ arm-none-eabi-readelf -SW crc.o | grep -E " \.(text|rodata)"    # -Os -ffunction-sections -fdata-sections
.text PROGBITS 00000000 000034 000000 00
.text.crc32 PROGBITS 00000000 000034 000028 00
.text.crc16_ccitt PROGBITS 00000000 00005c 000040 00
.text.crc32_bitwise PROGBITS 00000000 00009c 000030 00
.rodata.crc16_table PROGBITS 00000000 0000cc 000020 00
.rodata.crc32_table PROGBITS 00000000 0000ec 000400 00
```

`-ffunction-sections` 는 함수마다 `.text.<이름>`, `-fdata-sections` 는 변수마다 `.rodata.<이름>` / `.data.<이름>` / `.bss.<이름>` 을 만든다. 원래 `.rodata` 0x420 = CRC-32 표 0x400 + CRC-16 표 0x20 이 한 섹션이었다. `main` 은 gcc 가 `.text.startup.main` 에 넣는다 (A1 링크 에러 메시지에 그 이름이 보인다) — 섹션 이름은 함수 이름과 꼭 같지 않다.

**solution 에서 링커가 지운 것** — 맥 `check.sh solution` 의 `--print-gc-sections` (작업 디렉터리 `gc.txt`, `.debug_*` 줄 제외, 실제):

```text
.text.crc16_ccitt  crc.o
.text.crc32_bitwise  crc.o
.rodata.crc16_table  crc.o
.rodata.log_hexdump.str1.1  fmt.o
.text.log_hexdump  fmt.o
.text.memcpy  libc_min.o
.text.memset  libc_min.o
.comment  libc_min.o
.ARM.attributes  libc_min.o
.text.uart_write  libdrv.a(uart.o)
.text.uart_read_line  libdrv.a(uart.o)
.bss.g_dma_rx  libdrv.a(uart.o)
.text.sensor_calibrate  libdrv.a(sensor.o)
.text.sensor_selftest  libdrv.a(sensor.o)
.data.g_cal_table  libdrv.a(sensor.o)
```

읽는 법:

- `.bss.g_dma_rx` 하나가 RAM 2048 B, `.data.g_cal_table` 이 Flash 256 + RAM 256 B 다. 죽은 **데이터**가 죽은 코드보다 크다 — `-fdata-sections` 를 빼먹으면 bss 2124 가 그대로 남는 이유.
- `.rodata.log_hexdump.str1.1` — 문자열 리터럴도 함수별 병합 섹션으로 나뉘어 같이 죽는다.
- `libc_min.o` 는 `.comment`, `.ARM.attributes`, `.debug_*` 까지 통째로 사라졌다: 이 펌웨어는 gcc 가 memcpy/memset 호출을 만들지 않았고(`-ffreestanding`), 파일의 모든 코드 섹션이 죽으면 GNU ld 는 그 파일의 부속 섹션도 버린다 (실측. 정확한 규칙은 bfd 의 gc 구현 기준 확인 권장).
- `uart_write` 는 printf 빌드(`_write` → `uart_write`)에서만 산다. 같은 소스라도 빌드 구성마다 "죽은 코드" 집합이 다르다 → 소스에서 지우기 전에 gc 에게 맡기는 이유.

맵 파일의 `Discarded input sections` 에도 같은 목록이 크기와 함께 남는다 — VM S2 맵에서 뽑은 것이 `mapdiff_vm.txt` 마지막 블록 (`.bss.g_dma_rx 0x800 uart.o`, `.data.g_cal_table 0x100 sensor.o` …).

### 단계 표 — newlib 까지 (VM, `bash demo_stages.sh`)

<!-- code: demo_stages.sh -->

`stages_vm.txt` (실제):

```text
# VM elinux: arm-none-eabi-gcc 14.2.1, QEMU emulator version 10.2.1
# base: -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -std=c11 -Wall -Wextra -Werror -Ifw, newlib.ld (FLASH 64K @0x0 / RAM 16K @0x20000000), printf stages: --specs=nosys.specs -nostartfiles
# Flash = text+data, RAM = data+bss (no heap/stack reservation in newlib.ld); elf = file size on disk
stg  what changed                                   text  data   bss |  Flash   RAM |    elf | QEMU output vs golden
S0   -O0 -g, printf, full newlib                   34192  1980  2504 |  36172  4484 | 417520 | same
S1   -Os                                           33280  1980  2504 |  35260  4484 | 419776 | same
S2   + -ffunction/-fdata-sections + --gc-sections  31376  1724   452 |  33100  2176 | 418312 | same
S3   + -flto                                       31252  1724   452 |  32976  2176 | 417848 | same
S4   + --specs=nano.specs (newlib-nano)             8272    96   408 |   8368   504 | 267712 | same
S5   printf -> fmt.c tiny formatter                 2764     4    76 |   2768    80 | 149984 | same
S6   -nostdlib + libc_min.c (no newlib at all)      2492     4    76 |   2496    80 | 149200 | same
# traps / side rows
T1   S1 + --gc-sections only (no -ffunction-sec.)  31728  1980  2500 |  33708  4480 | 418332 | same
T2   S5 without LTO and without -ffunction-sec.     3324   260  2124 |   3584  2384 | 150556 | same
T3   S5 without LTO (= Mac solution.flags)          2828     4    76 |   2832    80 | 150588 | same
F1   S4 + LOG_FLOAT (%.1f), nano, no -u            10220    96   408 |  10316   504 | 270380 | DIFF(rc=0)
F2   S4 + LOG_FLOAT + -u _printf_float             21828   460   408 |  22288   868 | 379260 | same
F3   S3 + LOG_FLOAT (full newlib)                  31316  1724   452 |  33040  2176 | 417904 | same
```

| 단계 | Flash 변화 | RAM 변화 | 왜 그만큼인가 |
|---|---|---|---|
| S0→S1 `-Os` | −912 | 0 | `-O` 는 **내 .c 파일에만** 듣는다. 33 KB 를 차지하는 newlib 은 이미 컴파일된 `.a` 라 그대로. -O 수준 일반론은 TOOL-B03 |
| S1→S2 섹션+gc | −2160 | −2308 | 내 죽은 코드·데이터 + newlib 의 안 쓰는 섹션(`strcmp`, `locale`, `reent` …). RAM 은 `g_dma_rx` 2048 + `g_cal_table` 256. elf 칸(400 KB+)은 newlib 디버그 정보 — Flash 와 무관 |
| S2→S3 `-flto` | −124 | 0 | 이 펌웨어는 파일 간 인라이닝 거리가 적다. LTO 효과는 코드 구조 따라 천차만별 — 측정 없이 "LTO 하면 20% 준다" 고 말하지 말 것 |
| S3→S4 nano | **−24608** | −1672 | printf 엔진(`_vfprintf_r` 6800 B → nano 580 B + `_printf_i` 652 B), `_dtoa_r` 4168 B, malloc, 부동소수 소프트 연산이 통째로 빠짐 |
| S4→S5 printf 제거 | −5600 | −424 | 남은 stdio 층(FILE, `__sfvwrite_r`, `fflush`, malloc) 전부. `fmt.c` 는 약 570 B (`log_printf` 340 + `out_num` 128 + `out_field` 102, 맥 `nm -S`) |
| S5→S6 newlib 제거 | −272 | 0 | 아래 "strlen" 참고 |

**S5→S6 의 272 B 는 어디서?** 내 `fmt.c` 에는 strlen 호출이 없는데 — `strlen_vm.txt` (VM, 실제, 정의 심볼 줄 일부 생략):

```text
--- S5 fw.map: input sections that come from libraries (section, size, member)
.data 0x0 libg_nano.a(libc_a-strlen.o)
.text 0xd8 libg_nano.a(libc_a-strlen.o)
$ arm-none-eabi-nm fmt.o    # -Os (hosted)
000000e8 T log_printf
         U strlen
         U uart_putc
$ arm-none-eabi-nm fmt.o    # -Os -ffreestanding
000000e8 T log_printf
         U uart_putc
```

gcc 가 `while (s[len]) len++;` 루프를 **strlen 호출로 바꿨다** (loop idiom 인식). hosted 모드에서는 libc 가 있다고 가정하니 정당한 변환이고, 그 대가로 newlib 의 strlen 216 B(0xd8)가 딸려 왔다. 나머지 56 B 는 hosted/freestanding 코드 생성 차이. `-ffreestanding` 은 이 가정을 끈다 (그래서 `libc_min.c` 에도 `no-tree-loop-distribute-patterns` 를 붙였다 — memcpy 루프가 memcpy 호출로 바뀌어 자기 자신을 부르는 사고 방지, 확인 권장).

**맵 파일 diff** — 같은 맵에서 "입력 파일별 Flash 바이트" 를 뽑아 두 단계를 비교했다 (`mapdiff_vm.txt`, 실제. 맵 파서를 제대로 만드는 건 TOOL-M02 — 여기는 awk 15줄짜리):

```text
=== S1 (-Os) -> S2 (+ -ffunction-sections -fdata-sections --gc-sections)
libg.a(libc_a-strcmp.o)                   736      0    -736
libgcc.a(_arm_muldf3.o)                   596      0    -596
sensor.o                                  572    176    -396
crc.o                                    1216   1064    -152
libg.a(libc_a-locale.o)                   504    364    -140
libg.a(libc_a-reent.o)                    108      0    -108
uart.o                                    140     76     -64
...
=== S3 (full newlib) -> S4 (newlib-nano), libc members only, biggest changes
libg.a(libc_a-vfprintf.o)                7238      0   -7238
libg.a(libc_a-dtoa.o)                    4468      0   -4468
libg.a(libc_a-vfiprintf.o)               3604      0   -3604
libg.a(libc_a-mprec.o)                   3040      0   -3040
libg.a(libc_a-mallocr.o)                 2464      0   -2464
libgcc.a(_arm_muldivdf3.o)               1060      0   -1060
...
libg_nano.a(libc_a-fflush.o)                0    408    +408
libg_nano.a(libc_a-findfp.o)                0    560    +560
libg_nano.a(libc_a-fvwrite.o)               0    728    +728
libg_nano.a(libc_a-nano-vfprintf.o)         0    781    +781
libg_nano.a(libc_a-nano-vfprintf_i.o)       0    944    +944
```

- 첫 블록: gc 는 **라이브러리 쪽에서 더 많이** 지웠다 (strcmp 736, 소프트 double 곱셈 596). newlib 자체가 `-ffunction-sections` 로 빌드되어 있어서, 내가 `-ffunction-sections` 를 안 줘도 라이브러리 섹션은 지워진다 — T1 행(gc 만, 내 섹션 플래그 없음)이 S1 보다 1552 B 작은 이유. T1 의 `--print-gc-sections` 는 `libg.a` 4개, `libgcc.a` 1개만 지웠고 내 파일은 0개, S2 는 거기에 `sensor.o` 3, `crc.o` 3, `uart.o` 2 가 더해진다 (`gc_trap_vm.txt`).
- sensor.o −396 은 지운 섹션의 합(0x24 + 0x7c + 0x100 = 416)과 딱 맞지 않는다. 한 `.text` 덩어리로 컴파일할 때와 함수별로 컴파일할 때 코드 배치·리터럴 풀·정렬이 달라서다 — "섹션 합 = 절약량" 은 근사치.
- 둘째 블록: 왜 `libg.a` 인가? VM gcc 의 spec 은 `*lib: %{!shared:%{g*:-lg} %{!p:%{!pg:-lc}}...}` — `-g` 를 주면 `-lg` 가 `-lc` 앞에 붙어 멤버가 거기서 온다 (nano.specs 는 이를 `-lg_nano` 로 바꾼다, 실제 spec 출력). `libg` 가 libc 의 디버그 변형이라는 건 newlib 관례로 알려진 것 — 내용 동일 여부는 확인 권장.
- `vfprintf` 와 `vfiprintf` 가 둘 다 들어온 이유: full newlib 에서 `printf` 는 `_vfprintf_r`, 정수 전용 경로 `_vfiprintf_r` 은 gcc 가 `printf` → `iprintf` 로 바꿀 수 있을 때 쓰인다 (이 빌드에서 둘 다 링크된 건 실측, 어느 호출이 어느 쪽인지는 확인 권장). nano 는 둘이 같은 주소의 별칭이다 (`float_vm.txt`: 둘 다 `00000b8c 00000244`).

### LTO — 왜 링크 때 다시 컴파일하나

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">gcc -flto: .o 안은 기계어가 아니라 GIMPLE. 링크 때 플러그인이 전체를 보고 다시 컴파일</text>
  <rect x="10" y="40" width="110" height="40" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="20" y="64" fill="currentColor" font-size="12" font-family="monospace">uart.c sensor.c</text>
  <path d="M120 60 L160 60" stroke="currentColor" stroke-width="1.5"/>
  <path d="M154 55 L162 60 L154 65" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <rect x="162" y="35" width="160" height="50" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="170" y="54" fill="currentColor" font-size="12">.o = .gnu.lto_* (IR)</text>
  <text x="170" y="72" fill="currentColor" font-size="12" font-family="monospace">nm: C __gnu_lto_slim</text>
  <path d="M322 60 L362 60" stroke="currentColor" stroke-width="1.5"/>
  <path d="M356 55 L364 60 L356 65" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <rect x="364" y="35" width="170" height="50" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="372" y="54" fill="currentColor" font-size="12">ar 이 색인을 만든다</text>
  <text x="372" y="72" fill="currentColor" font-size="12">IR 심볼 = 플러그인 필요</text>
  <path d="M534 60 L574 60" stroke="currentColor" stroke-width="1.5"/>
  <path d="M568 55 L576 60 L568 65" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <rect x="576" y="35" width="150" height="50" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="584" y="54" fill="currentColor" font-size="12">libdrv.a</text>
  <text x="584" y="72" fill="currentColor" font-size="12" font-family="monospace">uart_init in uart.o</text>
  <text x="300" y="104" fill="currentColor" font-size="12">gcc-ar: 색인 OK / 맥 arm-none-eabi-ar: __gnu_lto_slim 만</text>
  <rect x="10" y="130" width="200" height="56" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="18" y="150" fill="currentColor" font-size="12">ld + liblto_plugin.so</text>
  <text x="18" y="168" fill="currentColor" font-size="12">IR 객체를 "청구"하고 심볼</text>
  <text x="18" y="182" fill="currentColor" font-size="12">해석 결과를 넘긴다</text>
  <path d="M210 158 L250 158" stroke="currentColor" stroke-width="1.5"/>
  <path d="M244 153 L252 158 L244 163" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <rect x="252" y="130" width="230" height="56" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="260" y="150" fill="currentColor" font-size="12">lto-wrapper → WPA (전체 프로그램)</text>
  <text x="260" y="168" fill="currentColor" font-size="12">안 닿는 함수 삭제, 파일 간 인라인,</text>
  <text x="260" y="182" fill="currentColor" font-size="12">전역을 static 처럼 다룸</text>
  <path d="M482 158 L522 158" stroke="currentColor" stroke-width="1.5"/>
  <path d="M516 153 L524 158 L516 163" fill="none" stroke="currentColor" stroke-width="1.5"/>
  <rect x="524" y="130" width="202" height="56" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="532" y="150" fill="currentColor" font-size="12">ltrans .o (진짜 Thumb-2)</text>
  <text x="532" y="168" fill="currentColor" font-size="12">→ ld 가 최종 링크</text>
  <text x="532" y="182" fill="currentColor" font-size="12">(+ --gc-sections)</text>
  <text x="10" y="220" fill="currentColor" font-size="12">• 색인에 uart_init 이 없으면 ld 는 libdrv.a 에서 멤버를 안 꺼낸다 → undefined reference.</text>
  <text x="10" y="242" fill="currentColor" font-size="12">• LTO 가 죽은 함수를 ld 보다 먼저 지운다 → practice 의 gc 목록에 우리 섹션 0개.</text>
  <text x="10" y="264" fill="currentColor" font-size="12">• -ffat-lto-objects: IR + 기계어를 둘 다 넣는다 → 일반 ar 로도 색인 OK (A6).</text>
  <text x="10" y="286" fill="currentColor" font-size="12">• gcc-ar ≈ ar --plugin $(arm-none-eabi-gcc -print-prog-name=liblto_plugin.so)</text>
</svg>
```

LTO 의 핵심은 **"이 전역 함수를 밖에서 부르는 사람이 있나?"** 를 링크 시점에 안다는 것이다. 보통 컴파일러는 `.c` 하나만 보니 `crc16_ccitt` 같은 extern 함수를 지울 수 없다 (다른 파일이 부를 수도). LTO 의 WPA 단계는 링커 플러그인이 준 심볼 해석 결과로 "IR 안에서만 쓰인다/아무도 안 쓴다" 를 알고, 함수를 지우거나 다른 파일에 인라인하거나 `static` 처럼 다룬다. 그래서 practice(LTO)의 `nm` 에는 `main` 도 `crc32` 도 없다 — `Reset_Handler` 안으로 인라인되었다 (아래 실수 6). 반대로 그래서 LTO 빌드는 디버깅이 어렵고, `nm` 의 이상한 심볼(`main.c.0f832232` 같은 `파일.c.<해시>` W 심볼)에 놀라게 된다 (맥 실측; 무엇인지는 LTO 디버그 정보 쪽으로 추정 — 확인 권장).

### Part B — 함정 네 개, 실측으로

**① 아카이버** — `bash demo_ar.sh` (`ar_mac.txt` / `ar_vm.txt` 실제 출력 발췌. `→` 가 있는 마지막 네 줄은 내가 한 줄로 줄인 요약):

```text
### A1 no LTO, Apple /usr/bin/ar
ranlib: warning: archive member 'uart.o' not a mach-o file
  (archiver exit code 0)
link: FAILED
arm-none-eabi-ld: main.o: in function `main':
main.c:(.text.startup.main+0x4): undefined reference to `uart_init'
arm-none-eabi-ld: (uart_init): Unknown destination type (ARM/Thumb) in main.o
### A3 -flto, arm-none-eabi-ar
arm-none-eabi-ar: uart.o: plugin needed to handle lto object
  (archiver exit code 0)
Archive index:
__gnu_lto_slim in uart.o
__gnu_lto_slim in sensor.o
link: FAILED
arm-none-eabi-ld: /var/folders/…/ccXXXXXX.ltrans0.ltrans.o: in function `out_field':
<artificial>:(.text.out_field+0x44): undefined reference to `uart_putc'
### A4 -flto, arm-none-eabi-gcc-ar          → index: uart_init in uart.o …   link: OK  text=2476 data=4 bss=76
### A5 -flto, arm-none-eabi-ar --plugin …/liblto_plugin.so                → link: OK  text=2476
### A6 -flto -ffat-lto-objects, arm-none-eabi-ar                        → link: OK  text=2476
--- VM (Ubuntu): arm-none-eabi-ar rcs / ar rcs / arm-none-eabi-gcc-ar rcs, all -flto → link: OK  text=2492
# /usr/lib/bfd-plugins: LLVMgold-21.so liblto_plugin.so
```

| 아카이버 | 비 LTO 객체 | LTO 객체 | 왜 |
|---|---|---|---|
| Apple `/usr/bin/ar` (맥) | ✗ 색인 비어 있음 | ✗ | ranlib 이 Mach-O 만 읽는다. 경고 한 줄, **종료 코드 0** |
| `arm-none-eabi-ar` (맥 Homebrew) | ✓ | ✗ `__gnu_lto_slim` 만 | ELF 는 읽지만 IR 심볼표는 플러그인 필요. Homebrew binutils 옆에 `lib/bfd-plugins` 가 없다 |
| `arm-none-eabi-ar --plugin <gcc 의 liblto_plugin.so>` | ✓ | ✓ | gcc-ar 가 하는 일을 손으로 |
| `arm-none-eabi-gcc-ar` | ✓ | ✓ | gcc 자기 플러그인을 넘기는 래퍼 — **항상 이것** (`gcc-nm`, `gcc-ranlib` 도 있다) |
| VM 의 `arm-none-eabi-ar`, 심지어 네이티브 `ar` | ✓ | ✓ | Ubuntu 는 `/usr/lib/bfd-plugins/liblto_plugin.so` 를 bfd 가 자동으로 읽는다 |

마지막 줄이 실무 함정의 정체다: **Ubuntu CI 에서는 통과, 맥 개발자 PC 에서는 링크 실패**. 그리고 둘 다 아카이버는 종료 코드 0 이라 빌드 로그 맨 아래의 "undefined reference" 가 처음 보이는 증상이다. 이때 "Unknown destination type (ARM/Thumb)" / "dangerous relocation" 은 정의를 못 찾은 결과로 따라 나오는 2차 메시지다 — 첫 줄(`undefined reference`)만 보면 된다. (VM 의 플러그인은 네이티브 gcc 15 의 것인데 cross gcc 14 의 LTO 객체 색인을 잘 만들었다 — 플러그인은 IR 의 심볼표 섹션만 읽기 때문으로 보이지만 버전 호환 보장은 확인 권장.) CMake 는 `CMAKE_C_COMPILER_AR` 로 gcc-ar 를 찾아 쓴다 (TOOL-M05, 확인 권장).

**② KEEP 없는 벡터 표** — `mac_keep.txt` (맥, 실제):

```text
# solution.flags (-ffunction-sections -fdata-sections) + starter.ld (no KEEP on .isr_vector)
removing unused section '.text.SysTick_Handler' in file 'startup.o'
removing unused section '.text.Default_Handler' in file 'startup.o'
removing unused section '.text.HardFault_Handler' in file 'startup.o'
removing unused section '.isr_vector' in file 'startup.o'
$ arm-none-eabi-nm fw.elf | grep -E 'Handler|vectors'
00000014 T Reset_Handler
$ od -An -tx4 -N8 fw.bin    # what the CPU loads as SP and PC at reset
                 b0824b03        3000e9cd

# 2x2: section granularity x KEEP (all -Os -g --gc-sections, gcc-ar). vec = .isr_vector size
CFLAGS                           ld           text data   bss | vec      SysTick  QEMU
-Os -g                           starter.ld   2896  260  2124 | 0x000000 kept     rc=134 qemu: fatal: Lockup: can't escalate 3 to
-Os -g                           solution.ld  2992  260  2124 | 0x000060 kept     golden
-Os -g + -ffunc/-fdata-sections  starter.ld   2420    4    76 | 0x000000 GONE     rc=134 qemu: fatal: Lockup: can't escalate 3 to
-Os -g + -ffunc/-fdata-sections  solution.ld  2548    4    76 | 0x000060 kept     golden
```

- `__attribute__((used))` 가 `vectors[]` 에 붙어 있어도 소용없다: `used` 는 "컴파일러야, 안 쓰여 보여도 .o 에 내보내라" 이고, 링커의 gc 는 별개다. 링커에게 "이건 뿌리다" 를 말하는 건 `KEEP()` (또는 링크 옵션 `-Wl,-u,vectors` — 실측으로 0x60 B 표와 SysTick_Handler 가 살아남았다. 다만 링커 스크립트에 두는 쪽이 빌드 시스템과 무관해서 표준).
- 벡터 표가 사라지면 Flash 0x0 에 코드가 온다. 리셋 때 CPU 는 0x0 의 워드를 SP 로, 0x4 의 워드를 PC 로 읽는다 → SP=0xb0824b03, PC=0x3000e9cd (Thumb 명령 바이트) → 즉시 fault → fault 핸들러 주소도 쓰레기 → QEMU: `Lockup: can't escalate 3 to HardFault`. 실제 보드라면 "아무 반응 없음" 이다.
- 위 표에서 섹션을 쪼갠 행(셋째)만 SysTick 핸들러까지 GONE 이다. 벡터 표가 유일한 참조자였으니 표와 함께 줄줄이 지워진 것. 섹션을 안 쪼갠 starter 는 핸들러가 Reset_Handler 와 같은 `.text` 에 있어서 "우연히" 살았다.
- 같은 이유로 KEEP 이 필요한 것들: `.init_array`/`.fini_array`, 명령/드라이버 등록 표(TOOL-M03), 펌웨어 버전·빌드 ID 헤더, 부트로더가 고정 주소에서 읽는 앱 헤더(BM-B06). 정렬·ASSERT 를 포함한 심화는 TOOL-A07.

**③ `-g` 는 Flash 를 먹지 않는다** — `mac_debug.txt` (맥, 실제):

```text
CFLAGS      text data  bss | elf_file  fw.bin | fw.bin sha256    | .debug_* bytes
-Os -g0     2548    4   76 |    10408    2552 | 48e30a3cd0c7f611 | 0
-Os -g      2548    4   76 |    27232    2552 | 48e30a3cd0c7f611 | 16262
-Os -g3     2548    4   76 |    47336    2552 | 48e30a3cd0c7f611 | 36300
-O2 -g      3052    4   76 |    31380    3056 | b6d4e57626f9f77c | 20402
-Oz -g      2548    4   76 |    27232    2552 | 48e30a3cd0c7f611 | 16262

$ cmp fw.bin(-Os -g0) fw.bin(-Os -g3)
identical: debug info never reaches the chip
$ arm-none-eabi-readelf -SW fw.elf | grep debug    # -Os -g (name, type, addr, off, size, flags)
.debug_info      PROGBITS  00000000 002004 001294 0
.debug_line      PROGBITS  00000000 00490f 000fbf 0
.debug_str       PROGBITS  00000000 0058ce 0003d4 MS
…
```

- `-g0`/`-g`/`-g3` 의 `fw.bin` 은 **sha256 까지 같다**. 디버그 섹션은 주소 0, 플래그에 `A`(alloc) 없음 → `size` 도 안 세고 `objcopy -O binary` 도 안 넣는다. ELF **파일** 크기만 10 KB → 27 KB → 47 KB 로 커진다. "ELF 가 커졌으니 Flash 도" 는 파일 크기와 이미지 크기를 혼동한 것.
- GCC 는 `-g` 가 생성 코드를 바꾸지 않는 것을 원칙으로 한다 (sha 일치가 이 빌드에서의 확인). 그러니 릴리스 ELF 에 `-g` 를 넣고 칩에는 .bin/.hex 만 굽고, ELF 는 빌드 산출물로 보관 → 현장 크래시 PC 를 줄 번호로 (TOOL-M14, 빌드 ID 로 짝 맞추기는 TOOL-A06).
- 덤: 이 gcc 16 에서 `-Oz` 는 `-Os` 와 **바이트 단위로 같은** 이미지를 냈다 (sha 동일). `-O2` 는 +504 B. 이 숫자는 이 펌웨어 한정 — 큰 코드에서는 -Oz 가 다를 수 있다 (TOOL-B03).

**④ 섹션 플래그 없이 gc** — 위 2×2 표 첫 두 행과 VM T1/T2 행. `--gc-sections` 만 켜면 내 코드에서 지운 섹션 0개 (starter 의 gc 목록: `.isr_vector`, 그리고 통째로 안 쓰인 `libc_min.o` 의 `.text`/`.comment`/`.ARM.attributes` 뿐). 링커 옵션 하나로 다이어트했다고 믿는 가장 흔한 착각이다.

### Part C — 펌웨어 현실

**newlib-nano 의 `%f`** — `float_vm.txt` (VM, 실제):

```text
--- F1: temp line on UART (golden: temp=2.4C)
temp=C
--- F2: temp line on UART (golden: temp=2.4C)
temp=2.4C
--- F3: temp line on UART (golden: temp=2.4C)
temp=2.4C
```

nano 의 printf 는 기본으로 부동소수 변환을 빼고 링크된다. `%.1f` 는 **에러도 경고도 없이 빈 문자열**이 된다 (`temp=C`). `-u _printf_float` 는 링커에게 "이 심볼을 미정의로 취급해 끌어와라" 라는 뜻이고, 그 심볼이 float 변환 코드를 끌고 온다 → Flash 10316 → 22288 (+11972). F1 이 S4 보다 1948 B 큰 건 `deci / 10.0` 의 소프트 double 연산 때문이다 (VM `nm -S F1/fw.elf`: `__aeabi_dmul` 0x254, `__aeabi_ddiv` 0x1d0, `__aeabi_i2d` … — libgcc 의 소프트 double) — printf 가 못 찍는 값을 계산하느라 돈을 낸 셈. 펌웨어 답: 고정소수점 정수 (`%d.%d`, 이 레슨의 기본 경로).

**printf 가 끌고 오는 것** — `float_vm.txt` 의 최대 심볼 (실제):

```text
[S3] arm-none-eabi-nm -S --size-sort fw.elf | grep -i " [tT] " | tail -6
00004f98 000002d8 T __sfvwrite_r
000064d0 00000348 T _realloc_r
000006e4 0000058e T _malloc_r
0000561c 00000d70 T _vfiprintf_r
00002e74 00001048 T _dtoa_r
00001230 00001a90 T _vfprintf_r
[S4] arm-none-eabi-nm -S --size-sort fw.elf | grep -i " [tT] " | tail -6
00001830 00000134 T memcpy
000000c0 000001dc T Reset_Handler
00000b8c 00000244 T _vfiprintf_r
00000b8c 00000244 T _vfprintf_r
000010b4 0000028c T _printf_i
000014f4 000002d8 T __sfvwrite_r
```

`printf` = 서식 해석기 + stdio 버퍼 층(`__sfvwrite_r`, `fflush`, FILE 표) + malloc. 그래서 printf 를 없애면(S5) 해석기만이 아니라 malloc 까지 사라진다. 또 하나: newlib 의 stdout 은 FILE 버퍼를 거친다. `main` 끝의 `log_flush()` (= `fflush(stdout)`) 는 방어용이다 — 이 빌드(nano + nosys)에서는 그 줄을 지우고 돌려도 7줄이 다 나왔다 (VM 실측). 버퍼링 모드는 `_isatty`/`_fstat` 스텁이 무엇을 돌려주느냐에 달려 있어서, 다른 BSP 의 syscalls.c 에서는 세미호스팅 종료나 리셋 직전 마지막 줄이 사라질 수 있다 (일반론, 확인 권장). 작은 포매터는 버퍼가 없어 이 걱정도 없다.

**링커가 예산을 강제하게** — `mac_overflow.txt` (맥, 실제):

```text
# solution.flags + practice.ld (FLASH LENGTH = 2K), -Wl,--print-memory-usage
link exit code: 1
Memory region         Used Size  Region Size  %age Used
           FLASH:        2552 B         2 KB    124.61%
             RAM:          80 B        16 KB      0.49%
arm-none-eabi-ld: fw.elf section `.rodata' will not fit in region `FLASH'
arm-none-eabi-ld: region `FLASH' overflowed by 504 bytes
collect2: error: ld returned 1 exit status
```

`MEMORY` 의 `LENGTH` 는 그대로 예산이다. 넘치면 어느 섹션이 안 들어가는지와 몇 바이트 넘는지를 링커가 말해 준다 (2552 − 2048 = 504). CI 크기 게이트(TOOL-B06 의 PR 증가량 검사)는 추세를 잡고, 이 에러는 마지막 방어선이다. 실무 팁: 부트로더 영역(BM-B06)·설정 페이지만큼 `LENGTH` 를 정확히 줄여 두어야 이 방어선이 의미가 있다.

**solution 실행** (실제):

```text
$ bash check.sh solution     # (머리 3줄 생략)
PASS libdrv.a index lists uart_init/sensor_push (arm-none-eabi-gcc-ar)
PASS link fw.elf
     size: text=2548 data=4 bss=76
PASS vector table at 0x0 (0x60 B): SP=0x20004000 Reset=0x00000085
PASS ISR symbols present: Reset_Handler HardFault_Handler SysTick_Handler Default_Handler
PASS --gc-sections kept .isr_vector
PASS --gc-sections removed 13 per-function/per-object sections, e.g. .text.crc16_ccitt .text.crc32_bitwise .rodata.crc16_table 
PASS dead code/data gone (nm): none of 9 dead symbols left
PASS text 2548 <= 2816
PASS data 4 <= 32
PASS bss 76 <= 256
PASS debug info kept (.debug_line in fw.elf)
PASS flash image fw.bin = text+data = 2552 B (fw.elf file = 27232 B)
PASS QEMU boot: exit 0, UART output == golden_uart.txt (7 lines)
13/13 passed
```

`Reset=0x00000085` — 벡터 표의 두 번째 워드는 `Reset_Handler`(0x84) | 1. 최하위 비트 1 은 Thumb 상태 표시이고, Cortex-M 은 Thumb 만 있으므로 0 이면 즉시 fault 다 (ARM-B12, TOOL-M14 의 PC|1 함정). 체커가 이것까지 비교한다. 툴체인 버전 차이도 숫자에 들어간다: 같은 LTO + freestanding 설정의 text 가 VM gcc 14 2492, 맥 gcc 16 2476 (`ar_vm.txt` / `ar_mac.txt`). VM T3(2828)과 맥 solution(2548)의 차이 280 B 는 대부분 버전이 아니라 hosted 빌드가 끌고 온 newlib `strlen` 216 B 다 (위 S5→S6). 예산 게이트는 툴체인 버전과 hosted/freestanding 설정을 고정해야 의미가 있다.

## 6. 정답 코드

<!-- code: solution.flags diff=starter.flags -->

<!-- code: solution.ld diff=starter.ld -->

| 바꾼 줄 | 고친 함정 | 효과 (맥 실측) |
|---|---|---|
| CFLAGS `-g0` → `-g` | ③ | Flash 0 B, ELF 파일 +16 KB, 크래시 줄 번호 가능 |
| CFLAGS `+ -ffunction-sections -fdata-sections` | ④ | text −444, data −256, bss −2048 (2992/260/2124 → 2548/4/76) |
| AR `ar` → `arm-none-eabi-gcc-ar` | ① | 색인 생성. LTO 를 켜도 그대로 동작 |
| `KEEP(*(.isr_vector))` | ② | +96 B (벡터 24칸) + 핸들러 — 이건 줄이면 안 되는 바이트 |

## 7. 자주 하는 실수

**1. `-Wl,--gc-sections` 만 추가하고 "다이어트 끝".** 섹션이 파일 단위면 링커가 지울 게 없다 (starter: 내 섹션 0개 제거). 고침: `-ffunction-sections -fdata-sections` 를 **모든** 소스(라이브러리 포함)의 컴파일에. 확인은 `--print-gc-sections` 또는 맵의 `Discarded input sections`.

**2. 벡터 표에 `used` 를 붙였으니 안전하다고 생각.** `used` 는 컴파일러용, gc 는 링커용. `KEEP(*(.isr_vector))`. 증상은 크기가 **줄어서** 기뻐하는 순간 온다 (2548 → 2420).

**3. 호스트 `ar`, 또는 LTO 에 일반 `arm-none-eabi-ar`.** 경고 한 줄 + 종료 코드 0 → 링크 때 `undefined reference`. Makefile 에 `AR := $(CROSS)gcc-ar`, `RANLIB := $(CROSS)gcc-ranlib`, `NM := $(CROSS)gcc-nm`. 그리고 "Ubuntu CI 에서는 된다" 는 bfd-plugins 자동 로딩 덕일 뿐이다.

**4. 크기를 ELF 파일 크기로 잰다.** `ls -l fw.elf` 는 디버그 정보·심볼표 포함 (이 레슨: 10 KB~419 KB). Flash 는 `size` 의 text + data, 또는 `objcopy -O binary` 의 크기. `-g` 를 빼서 줄인 건 디스크뿐이다.

**5. 한 단계씩 재지 않고 플래그를 한꺼번에.** 이 레슨에서 LTO 는 −124 B, nano 는 −24608 B 였다. 한꺼번에 켜면 "LTO 덕분에 75% 줄었다" 같은 틀린 결론과, 디버깅 비용만 큰 플래그(LTO)를 이유 없이 떠안는 결과가 남는다. 단계마다 크기 **와 동작**(golden 출력)을 기록.

**6. LTO 후 `nm` 에 `main` 이 없다고 당황 / 브레이크포인트가 안 걸린다고 당황.** LTO 는 `main` 을 `Reset_Handler` 안으로 인라인했다 (practice 의 `nm`). 디버그 빌드는 LTO 없이, 릴리스만 LTO — 또는 디버깅해야 할 함수에 `__attribute__((noinline))`. 링커 스크립트로 특정 섹션에 놓는 함수(RAM 함수 등)는 LTO 와 섞일 때 섹션 속성이 유지되는지 맵으로 확인 (TOOL-A07).

**7. newlib-nano 로 바꾸고 출력을 확인 안 함.** `%f` 가 조용히 사라진다 (`temp=C`). `%lld`, `%zu` 같은 서식도 nano 에서 지원 범위가 다르니 (확인 권장) 로그 golden 테스트를 둔다.

**8. 1 KB 테이블을 무조건 줄인다.** 64 B nibble 테이블(연습 1)은 바이트당 조회 2번이다. CRC 가 DMA 수신 경로의 hot path 면 속도가 먼저일 수 있다 — 크기 다이어트는 "어디에 쓰는 코드인가" 를 보고 함수 단위로 결정 (`__attribute__((optimize("O2")))` 를 핫 함수에만, GCC 에서 지원하나 권장되지는 않음 — 파일 단위 `-O2` 가 더 예측 가능).

## 8. 면접 Q&A

<details><summary>Q1. --gc-sections 는 정확히 무엇을 지우나요? 왜 -ffunction-sections 가 필요하죠?</summary>

링커는 함수가 아니라 입력 섹션을 다룬다. gc 는 뿌리(ENTRY 심볼, `-u`/`--undefined`, 링커 스크립트 `KEEP`)에서 재배치를 따라 닿는 섹션을 표시하고 나머지 ALLOC 섹션을 버리는 마크-스윕이다. 기본 컴파일은 파일당 `.text` 하나라 그 안의 함수 하나만 살아 있어도 전부 산다. `-ffunction-sections`/`-fdata-sections` 로 함수·변수마다 섹션을 만들어야 지울 단위가 생긴다. 실측: crc.o 의 `.text` 0xa0 한 덩어리 → `.text.crc32`/`.text.crc16_ccitt`/`.text.crc32_bitwise`, gc 후 내 섹션 13개 제거, bss 2124 → 76.

꼬리: "단점은?" → 오브젝트 파일이 커지고 링크가 느려지며, 섹션마다 정렬 패딩이 생길 수 있다. ARM 에서는 `-fdata-sections` 가 section anchor 최적화(전역 여러 개를 한 기준 주소로 접근)를 막아 코드가 약간 커질 수 있다고 알려져 있다 (확인 권장 — 이 펌웨어에서는 순감소).
</details>

<details><summary>Q2. KEEP 은 왜 필요하고, 무엇에 써야 하나요?</summary>

코드에서 아무도 참조하지 않지만 하드웨어/다른 프로그램/런타임이 "위치로" 찾는 것들. 벡터 표(CPU 가 0x0 에서 읽음), `.init_array`(startup 이 심볼 범위로 순회), 등록 표(TOOL-M03), 부트로더가 읽는 앱 헤더·버전 문자열. 없으면 gc 가 지우고, 벡터 표의 경우 이미지가 작아진 채로 부팅 불가 — 실측 QEMU `Lockup: can't escalate 3 to HardFault`, 그리고 벡터 표가 유일한 참조자였던 SysTick/HardFault 핸들러까지 연쇄 제거. `__attribute__((used))` 는 컴파일러 단계만 막는다.
</details>

<details><summary>Q3. LTO 는 무엇을 하고, 왜 모든 팀이 켜지 않나요?</summary>

`-flto` 로 컴파일한 .o 에는 기계어 대신 GIMPLE IR 이 들어 있고, 링크 때 링커 플러그인이 어떤 심볼이 밖에서 쓰이는지 알려 주면 lto-wrapper 가 프로그램 전체를 보고(WPA) 안 쓰는 함수를 지우고, 파일 간 인라인하고, 다시 코드 생성(ltrans)한다. 안 켜는 이유: ① 디버깅 — 함수가 사라지거나 합쳐져 브레이크포인트·스택이 낯설다 (practice 에서 `main` 이 사라짐), ② 링크 시간·메모리 증가, ③ 툴체인 일관성 — 아카이버/nm/ranlib 까지 플러그인 인식 필요 (맥 실측 링크 실패), ④ 어셈블리·링커 스크립트 배치·`section` 속성과의 상호작용, 암묵적으로 쓰이는 심볼(ISR, 디버거가 보는 전역)에 `used` 가 필요, ⑤ 효과가 코드 구조에 달려 있다 — 이 펌웨어는 −124 B (0.4%) 뿐. 측정해서 이득이 있을 때 켠다.
</details>

<details><summary>Q4. newlib 과 newlib-nano 는 뭐가 다르고, 바꾸면 무엇을 확인해야 하나요?</summary>

nano 는 크기 우선으로 다시 빌드한 newlib: 작은 printf/scanf (기본 부동소수 미지원), 작은 malloc, 줄어든 stdio. `--specs=nano.specs` 는 `-lc` → `-lc_nano`, `-lg` → `-lg_nano` 로 바꾸고 nano 헤더 디렉터리를 `-isystem` 으로 넣는다 (VM 의 spec 파일 실제 내용). 실측 Flash 32976 → 8368. 확인할 것: ① `%f` (기본으로 빈 문자열 — 실측 `temp=C`), 필요하면 `-u _printf_float` (+약 12 KB), ② 다른 서식 지원 범위, ③ malloc 동작 차이(단편화 특성), ④ 재진입(reent) 구조 차이 — RTOS 의 newlib 락 훅 사용 시. 그리고 출력 golden 테스트.
</details>

<details><summary>Q5. -g 를 켜면 펌웨어가 커지나요? -g3 는?</summary>

Flash 이미지는 0 B 도 안 커진다. 디버그 섹션은 ALLOC 플래그가 없고 주소 0 이라 `size` 에도 `.bin` 에도 안 들어간다. 실측: `-g0`/`-g`/`-g3` 의 fw.bin sha256 동일, ELF 파일만 10408 → 27232 → 47336 B. `-g3` 는 매크로 정의까지 넣어 디버거에서 `p REG_BASE` 가 된다. 그러니 릴리스도 `-g` 로 빌드해 ELF 를 보관하고, 칩에는 bin/hex 만, 현장 크래시는 보관된 ELF 로 addr2line (TOOL-M14). "릴리스는 -g0" 정책은 디스크만 아낀다.
</details>

<details><summary>Q6. 펌웨어는 -Os 인가요 -O2 인가요?</summary>

기본은 -Os (이 펌웨어: -Os 2548 vs -O2 3052, gcc 16 의 -Oz 는 -Os 와 바이트 단위 동일). 하지만 Flash 가 남고 속도가 중요한 경로(DSP 루프, CRC on DMA path)는 그 파일만 -O2 로 빌드하는 게 일반적이다. 플래시 대기 상태(wait state)가 있는 MCU 는 작은 코드가 캐시/프리페치에 잘 맞아 -Os 가 더 빠른 경우도 있으니 재 본다. 그리고 -O 레벨은 내 코드에만 듣는다 — 라이브러리(newlib, 벤더 HAL .a)는 이미 컴파일되어 있어 S0→S1 이 −912 B 뿐이었다.
</details>

<details><summary>Q7. "Flash 가 2 KB 모자라요" — 순서대로 무엇을 하시겠어요?</summary>

① 측정: `size`, 맵 파일, `nm --size-sort -S` 로 상위 소비자 (내 코드 vs 라이브러리) — 이 레슨에서는 라이브러리가 90% 이상. ② 공짜인 것: `-ffunction-sections -fdata-sections --gc-sections`, newlib-nano, `-Os`. ③ 라이브러리 사용 습관: printf/`%f`/malloc 제거, 작은 포매터, 문자열을 칩 밖으로(토큰화 로그 TOOL-B13). ④ 데이터: 큰 const 표 축소(1 KB CRC 표 → 64 B), 초기값 있는 전역 줄이기 (.data 는 Flash+RAM). ⑤ LTO — 측정해서 이득이 있으면. ⑥ 마지막: 기능 재설계. 매 단계 크기와 동작(golden/HIL)을 기록하고 맵 diff 로 "누가 줄었나" 를 PR 에 남긴다. 링커 MEMORY LENGTH 를 실제 크기로 맞춰 링커가 넘침을 잡게.
</details>

<details><summary>Q8. 같은 브랜치가 CI(Ubuntu)에서는 링크되는데 제 맥에서는 "undefined reference" 가 납니다. LTO 를 켠 직후예요.</summary>

정적 라이브러리를 일반 `ar` 로 만들었을 가능성. LTO 객체는 IR 이라 아카이버가 LTO 플러그인으로 심볼을 읽어야 색인이 생긴다. Ubuntu 는 `/usr/lib/bfd-plugins/liblto_plugin.so` 를 binutils 가 자동 로드해서 일반 `ar` 로도 되지만(VM 실측), Homebrew 의 `arm-none-eabi-ar` 옆에는 그 디렉터리가 없어 `plugin needed to handle lto object` 경고만 찍고(종료 코드 0) 색인에 `__gnu_lto_slim` 만 남긴다. 확인: `arm-none-eabi-nm -s libdrv.a` 의 Archive index. 고침: `arm-none-eabi-gcc-ar` (또는 `ar --plugin $(gcc -print-prog-name=liblto_plugin.so)`, 또는 `-ffat-lto-objects`). 빌드 시스템에서 AR 을 gcc-ar 로 고정.
</details>

## 9. 추가 연습

<details><summary>연습 1 — Flash 2 KB 부품으로 이사 (practice.flags + practice.ld, 채점됨)</summary>

조건: text ≤ 1600, LTO 사용(객체에 `.gnu.lto_*`), 링커 스크립트의 FLASH LENGTH ≤ 2 KB, 링크 로그에 `--print-memory-usage` 표, 출력은 golden 그대로. 답은 아래 `practice.flags` + `practice.ld`(solution.ld 에서 한 줄):

```text
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 2K     /* 링커가 예산을 강제: 넘치면 "region overflowed" */
```

크기를 만든 건 LTO 가 아니라 `-DCRC_NIBBLE` 이다: 1 KB 표 → 16칸 64 B 표(바이트당 조회 2번). 실측 (맥): solution 2548 → LTO 만 2476 → nibble 만 1604 → 둘 다 1528. 연습 결과 `FLASH: 1532 B 2 KB 74.80%`, `16/16 passed`. CRC 값(`cbf43926`)이 golden 과 같으니 테이블을 바꿔도 알고리즘은 같다. LTO 를 켰으므로 AR 은 반드시 gcc-ar — `arm-none-eabi-ar` 로 바꿔 보면 첫 줄이 FAIL 이 된다.
</details>

<details><summary>연습 2 — 왜 VM 에서는 일반 ar 로도 LTO 가 되나</summary>

`ar_vm.txt`: `arm-none-eabi-ar`, 네이티브 `ar`, `gcc-ar` 셋 다 색인 OK, text=2492. `/usr/lib/bfd-plugins` 에 `liblto_plugin.so` 와 `LLVMgold-21.so` 가 있고, BFD 는 이 디렉터리의 플러그인을 자동으로 시도한다. 맥 Homebrew 에는 해당 디렉터리가 없다(`ar_mac.txt` 머리줄). 교훈: 빌드 재현성을 환경의 우연에 맡기지 말고 AR 을 명시. (자동 로드 경로가 binutils 의 `<prefix>/lib/bfd-plugins` 라는 것은 문서 기준 — 크로스 binutils 가 왜 네이티브 경로를 보는지는 배포판 빌드 설정에 따름, 확인 권장.)
</details>

<details><summary>연습 3 — %f 를 정말 써야 한다면</summary>

F1/F2/F3: nano + `-u _printf_float` 는 Flash 22288 B, full newlib 33040 B, nano 그대로는 `temp=C` (틀림). 정수 고정소수점(`%d.%d`)은 S4 의 8368 B 에 그대로 들어간다. 음수 처리 주의: `deci = -5` 면 `deci / 10 = 0`, `deci % 10 = -5` → `0.-5` 가 찍힌다. 부호를 따로 처리하는 `fmt_fixed1(int deci)` 를 만들어 보라 (이 펌웨어의 값은 양수라 golden 에는 안 걸린다 — 테스트 입력을 넓혀야 보이는 버그).
</details>

<details><summary>연습 4 — S5 와 S6 사이 272 B 추적</summary>

맵의 아카이브 멤버를 보면 `libg_nano.a(libc_a-strlen.o)` `.text 0xd8` (216 B). `fmt.c` 를 hosted 로 컴파일하면 `nm` 에 `U strlen` 이 생기고 `-ffreestanding` 이면 없다 — gcc 가 길이 세는 루프를 strlen 호출로 바꾼 것. 나머지 56 B 는 hosted/freestanding 코드 생성 차이 (함수별 크기를 `nm -S` 로 비교해 보라). 결론: 라이브러리를 링크한다는 것 자체가 컴파일러에게 "변환해도 된다" 는 허락이다.
</details>

<details><summary>연습 5 (생각) — CI 에 무엇을 남길까</summary>

빌드마다: ① `size` (text/data/bss) + `--print-memory-usage`, ② 맵 파일과 ELF(-g) 보관, ③ 기준 브랜치 대비 파일별·심볼별 diff (이 레슨의 awk 를 제대로 만든 것 = TOOL-M02), ④ QEMU/HIL golden 출력 비교, ⑤ 툴체인 버전과 libc 설정 고정 (같은 소스·같은 LTO 설정에서 gcc 14 2492 vs gcc 16 2476 text, hosted 냐 freestanding 이냐로 strlen 216 B). 게이트: 예산 초과 실패 + PR 증가량 경고 (TOOL-B06 의 size_gate).
</details>

### 한 번에 검증

<!-- code: practice.flags -->

```text
$ bash check.sh practice
CFLAGS =-Os -g -ffunction-sections -fdata-sections -flto -DCRC_NIBBLE
LDFLAGS=-Wl,--gc-sections -Wl,--print-memory-usage
AR     =arm-none-eabi-gcc-ar   ld=practice.ld   budget: text<=1600 data<=32 bss<=256
PASS libdrv.a index lists uart_init/sensor_push (arm-none-eabi-gcc-ar)
PASS link fw.elf
     size: text=1528 data=4 bss=76
PASS vector table at 0x0 (0x60 B): SP=0x20004000 Reset=0x00000311
…
PASS --gc-sections removed 0 per-function/per-object sections (LTO already dropped dead code before ld)
PASS dead code/data gone (nm): none of 9 dead symbols left
PASS text 1528 <= 1600
…
PASS QEMU boot: exit 0, UART output == golden_uart.txt (7 lines)
PASS objects are LTO IR (.gnu.lto_* sections in main.o)
PASS practice.ld FLASH region length 0x00000800 <= 2 KB (the linker enforces the budget)
PASS --print-memory-usage:  FLASH: 1532 B 2 KB 74.80%
16/16 passed
```

`Reset=0x00000311` — LTO 가 `main` 을 `Reset_Handler` 안에 인라인해서 Reset_Handler 가 0x310 으로 옮겨 갔다 (solution 은 0x84). `removed 0` 은 실패가 아니다: 죽은 코드는 ld 가 아니라 LTO 가 먼저 지웠고, ld 의 gc 목록에는 LTO 가 만든 임시 `*.debug.temp.o` 의 `.comment`/`.ARM.attributes` 같은 부속만 남는다.

## 10. 핵심 정리

- **측정 → 가장 큰 덩어리부터.** 이 펌웨어: nano −24608, printf 제거 −5600, gc −2160, -Os −912, LTO −124 (Flash 36172 → 2496 B, RAM 4484 → 80 B). 라이브러리 선택이 플래그보다 크다.
- `--gc-sections` = 뿌리(ENTRY, `-u`, `KEEP`)에서 재배치를 따라가는 **입력 섹션** 마크-스윕. `-ffunction-sections -fdata-sections` 없으면 지울 단위가 없다.
- 참조되지 않지만 필요한 것(벡터 표, init_array, 등록 표, 앱 헤더)은 `KEEP()`. `used` 는 컴파일러용일 뿐. 없으면 작아진 채 부팅 불가 (QEMU Lockup).
- LTO 객체는 IR → 아카이버·nm·ranlib 도 플러그인 필요: **`$(CROSS)gcc-ar`**. Ubuntu 는 bfd-plugins 자동 로드로 가려진다.
- `-g` 는 Flash 0 B (bin sha 동일). 크기는 `size`/`.bin` 으로, ELF 파일 크기로 재지 않는다. 릴리스도 `-g` 로 빌드해 ELF 보관.
- newlib-nano 는 `%f` 를 조용히 버린다 → `-u _printf_float` (+12 KB) 또는 고정소수점. 작아진 이미지는 **golden 출력/HIL 로 동작 검증**.
- 예산은 `MEMORY` LENGTH 로 링커가 강제(`region 'FLASH' overflowed by N bytes`), 추세는 CI 크기 게이트 + 맵 diff 로.

## 참고자료

- GNU ld 매뉴얼 — `--gc-sections`, `--print-gc-sections`, `KEEP`, `--print-memory-usage`: https://sourceware.org/binutils/docs/ld/Options.html , https://sourceware.org/binutils/docs/ld/Input-Section-Keep.html
- GCC 매뉴얼 — `-ffunction-sections`/`-fdata-sections`, `-flto`, `-ffat-lto-objects`, `-Os`/`-Oz`: https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html
- GCC LTO 내부 (WPA / ltrans, 링커 플러그인): https://gcc.gnu.org/onlinedocs/gccint/LTO.html
- binutils `ar` 의 `--plugin` 과 bfd-plugins 디렉터리: https://sourceware.org/binutils/docs/binutils/ar-cmdline.html
- newlib / newlib-nano (Arm GNU Toolchain 의 nano.specs, `-u _printf_float`): https://sourceware.org/newlib/ , https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
- QEMU Arm MPS2 보드 (mps2-an386, CMSDK UART): https://www.qemu.org/docs/master/system/arm/mps2.html
- 관련 레슨: TOOL-B06 (size 읽기, Flash/RAM 공식), TOOL-B03 (-O 레벨), TOOL-B09 (정적 라이브러리 링크 순서), TOOL-B13 (컴파일 타임 로그 제거), TOOL-M01 (링커 스크립트), TOOL-M02 (맵 파일 분석기), TOOL-M03 (등록 표 + KEEP), TOOL-M05 (CMake 크로스 빌드), TOOL-M14 (DWARF 심볼리케이션), TOOL-A06 (재현 빌드/빌드 ID), TOOL-A07 (링커 스크립트 심화), BM-B06 (부트로더 + 앱 Flash 분할), BM-B08 (리셋에서 main 까지)
