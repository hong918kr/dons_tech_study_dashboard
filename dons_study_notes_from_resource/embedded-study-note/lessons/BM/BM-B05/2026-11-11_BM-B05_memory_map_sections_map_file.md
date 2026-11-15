---
id: BM-B05
time: 60분
updated: 2026-11-11
---
# BM-B05 MCU 메모리 맵과 섹션 배치: map 파일 읽기

## 학습 목표

- `arm-none-eabi-size -A` / `-B` 출력과 링커 map 파일을 읽어 **Flash = text + rodata + data(LMA)**, **RAM = data + bss + heap + stack** 예산표를 손으로도, 코드로도 만들 수 있다. 결과가 ld 의 `--print-memory-usage` 숫자와 바이트 단위로 같아야 한다.
- `.data` 가 왜 Flash(초기값 이미지, LMA) **와** RAM(실행 중 사본, VMA)을 둘 다 먹는지, `.bss` 는 왜 RAM 만 먹는지 설명할 수 있다. map 이 `.bss` 에도 `load address` 를 찍는 함정을 안다.
- `-ffunction-sections -fdata-sections` 로 만든 map 의 입력 섹션(input section)을 파싱해 두 빌드를 diff 하고, **const 를 빠뜨린 8 KB 테이블이 .rodata → .data 로 옮겨 간 것**을 찾아낸다.
- 부품(512K/128K, 64K/20K, 32K/8K)별 CI 예산 게이트를 만들고, ld 의 `region 'RAM' overflowed by N bytes` 를 링크 전에 예측할 수 있다.

## 1. 문제

펌웨어 면접과 보드 브링업에서 "이 이미지는 Flash 를 얼마나, RAM 을 얼마나 쓰나? 왜 RAM 이 갑자기 8 KB 늘었지?" 는 map 파일을 읽을 줄 아느냐를 바로 가르는 질문이다. SSD 컨트롤러 펌웨어에서 SRAM/TCM 예산을 바이트 단위로 쪼개 쓰던 것과 같은 일을 Cortex-M 에서 한다. 이번 레슨은 **진짜 빌드 결과**로 한다: Lima VM 의 `arm-none-eabi-gcc 14.2.1` 로 Cortex-M4 펌웨어(`fw.c` + 직접 쓴 `fw.ld`, STM32F411RE 급 512 KB Flash / 128 KB SRAM)를 두 번 빌드했다.

- **good**: `const uint16_t sine_lut[4096]` (12-bit DAC 사인 테이블 8192 B) + `const char *const cmd_names[4]`
- **bad** (`-DBAD`): 두 곳에서 `const` 를 빠뜨렸다 — 흔한 실수. 컴파일 경고도 없고, QEMU(`-M netduinoplus2`, STM32F405)에서 두 이미지 모두 정상 동작한다 (`qemu exit=0`). **버그는 예산표에만 보인다.**

캡처한 파일(레슨 폴더에 있음, 모두 VM 실제 출력): `size_A_{good,bad}.txt`, `size_B_{good,bad}.txt`, `fw_{good,bad}.map` (메모리 맵 부분만 잘라 둠), `ldmem_{good,bad,ovf}.txt` (`-Wl,--print-memory-usage`), `nm_{good,bad}.txt`.

```svg
<svg viewBox="0 0 760 400" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">실제 빌드의 메모리 맵 (위 = 낮은 주소, 크기 비례 아님) — fw_good / fw_bad</text>
  <text x="20" y="44" fill="currentColor" font-weight="bold">good (const 있음)</text>
  <text x="400" y="44" fill="currentColor" font-weight="bold">bad (const 빠짐, -DBAD)</text>
  <text x="30" y="58" fill="currentColor">Flash 0x08000000</text>
  <text x="200" y="58" fill="currentColor">RAM 0x20000000</text>
  <text x="410" y="58" fill="currentColor">Flash 0x08000000</text>
  <text x="580" y="58" fill="currentColor">RAM 0x20000000</text>
  <rect x="30" y="64" width="140" height="22" class="f-mut" stroke="currentColor"/>
  <text x="36" y="79" fill="currentColor">.isr_vector 0x40</text>
  <rect x="30" y="86" width="140" height="24" class="f-mut" stroke="currentColor"/>
  <text x="36" y="102" fill="currentColor">.text 0x110</text>
  <rect x="30" y="110" width="140" height="92" class="f-ok" stroke="currentColor"/>
  <text x="36" y="126" fill="currentColor">.rodata 0x2030</text>
  <text x="36" y="146" fill="currentColor">sine_lut 0x2000</text>
  <text x="36" y="164" fill="currentColor">@0x08000180</text>
  <text x="36" y="184" fill="currentColor">cmd_names, 문자열</text>
  <rect x="30" y="202" width="140" height="22" class="f-warn" stroke="currentColor"/>
  <text x="36" y="217" fill="currentColor">.data 이미지 0x1c</text>
  <rect x="30" y="224" width="140" height="96" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="36" y="264" fill="currentColor">비어 있음</text>
  <text x="36" y="282" fill="currentColor">515684 B</text>
  <rect x="200" y="64" width="140" height="22" class="f-warn" stroke="currentColor"/>
  <text x="206" y="79" fill="currentColor">.data 0x1c</text>
  <rect x="200" y="86" width="140" height="36" class="f-acc" stroke="currentColor"/>
  <text x="206" y="102" fill="currentColor">.bss 0xc08</text>
  <text x="206" y="117" fill="currentColor">rx/DAC 버퍼</text>
  <rect x="200" y="122" width="140" height="40" class="f-mut" stroke="currentColor"/>
  <text x="206" y="138" fill="currentColor">heap 0x800 + stack</text>
  <text x="206" y="154" fill="currentColor">예약 0x1000 (+4)</text>
  <rect x="200" y="162" width="140" height="128" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="206" y="222" fill="currentColor">비어 있음 121816 B</text>
  <rect x="200" y="290" width="140" height="30" class="f-mut" stroke="currentColor"/>
  <text x="206" y="309" fill="currentColor">실제 스택 ↑ _estack</text>
  <path d="M170 213 C185 213 185 75 198 75" stroke="currentColor" fill="none"/>
  <path d="M192 71 L199 75 L192 79" stroke="currentColor" fill="none"/>
  <text x="30" y="342" fill="currentColor">Flash 8604 B (1.64 %)</text>
  <text x="200" y="342" fill="currentColor">RAM 9256 B (7.06 %)</text>
  <rect x="410" y="64" width="140" height="22" class="f-mut" stroke="currentColor"/>
  <text x="416" y="79" fill="currentColor">.isr_vector 0x40</text>
  <rect x="410" y="86" width="140" height="24" class="f-mut" stroke="currentColor"/>
  <text x="416" y="102" fill="currentColor">.text 0x110</text>
  <rect x="410" y="110" width="140" height="22" class="f-ok" stroke="currentColor"/>
  <text x="416" y="125" fill="currentColor">.rodata 0x1c (문자열)</text>
  <rect x="410" y="132" width="140" height="92" class="f-warn" stroke="currentColor"/>
  <text x="416" y="148" fill="currentColor">.data 이미지 0x2030</text>
  <text x="416" y="168" fill="currentColor">sine_lut 초기값</text>
  <text x="416" y="186" fill="currentColor">LMA 0x0800016c</text>
  <rect x="410" y="224" width="140" height="96" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="416" y="264" fill="currentColor">비어 있음</text>
  <text x="416" y="282" fill="currentColor">515684 B (같음)</text>
  <rect x="580" y="64" width="140" height="92" class="f-warn" stroke="currentColor"/>
  <text x="586" y="80" fill="currentColor">.data 0x2030</text>
  <text x="586" y="100" fill="currentColor">sine_lut 0x2000</text>
  <text x="586" y="118" fill="currentColor">@0x20000030</text>
  <text x="586" y="138" fill="currentColor">(+ cmd_names 0x10)</text>
  <rect x="580" y="156" width="140" height="36" class="f-acc" stroke="currentColor"/>
  <text x="586" y="178" fill="currentColor">.bss 0xc08</text>
  <rect x="580" y="192" width="140" height="40" class="f-mut" stroke="currentColor"/>
  <text x="586" y="208" fill="currentColor">heap 0x800 + stack</text>
  <text x="586" y="224" fill="currentColor">예약 0x1000 (+0)</text>
  <rect x="580" y="232" width="140" height="58" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="586" y="265" fill="currentColor">비어 있음 113608 B</text>
  <rect x="580" y="290" width="140" height="30" class="f-mut" stroke="currentColor"/>
  <text x="586" y="309" fill="currentColor">실제 스택 ↑ _estack</text>
  <path d="M550 178 C566 178 566 110 578 110" stroke="currentColor" fill="none"/>
  <path d="M572 106 L579 110 L572 114" stroke="currentColor" fill="none"/>
  <text x="410" y="342" fill="currentColor">Flash 8604 B (1.64 %)</text>
  <text x="580" y="342" fill="currentColor">RAM 17464 B (13.32 %)</text>
  <text x="20" y="370" fill="currentColor">화살표 = Reset_Handler 의 복사 (LMA → VMA). bad 는 부팅마다 2060 워드를 복사하고, 같은 바이트가 Flash 와 RAM 에 둘 다 있다.</text>
  <text x="20" y="390" fill="currentColor">Flash 합은 두 빌드가 같다 (8604 B) — 테이블은 어차피 Flash 에 한 번 들어간다. 늘어난 것은 RAM 8208 B 뿐.</text>
</svg>
```

### Part A — size 출력 → Flash/RAM 예산표

- `parse_size_A()` — `size -A -x` 출력 파싱 (헤더 다음부터 `Total` 전까지, 잘린 캡처는 -1).
- `parse_size_B()` — `size -B` 의 `text data bss dec` 한 줄.
- `map_symbol()` — map 에서 심볼 값 (`_Min_Stack_Size = 0x1000` 같은 스크립트 대입과, 입력 섹션 아래 `0x08000180 sine_lut` 같은 심볼 줄 둘 다).
- `budget_build()` — 주소로 영역을 정하고 이름으로 NOBITS 를 가려 예산표를 만든다. 결과는 ld 의 `--print-memory-usage` 값과 같아야 하고, `size -B` 로도 교차 검증한다 (Flash = text + data, RAM = data + bss). 어느 영역에도 속하지 않는 섹션(예: CCM RAM `0x10000000`)은 **개수로 보고**한다.

### Part B — map 입력 섹션으로 두 빌드 diff → const 누락 찾기

- `parse_map_inputs()` — ` .rodata.sine_lut` 같은 입력 섹션 줄을 읽는다. 이름이 길면 주소·크기가 **다음 줄로 넘어가는** GNU ld 형식을 처리한다. `*fill*`, 패턴 줄, 크기 0 은 버린다.
- `diff_moves()` — 같은 심볼이 다른 출력 섹션으로 옮겨 간 것을 찾고 RAM 증감을 붙인다. good → bad 의 RAM 증가(8208 B)를 정확히 설명해야 한다.
- `lint_large_rw()` — 빌드 하나만 보고 "초기값 있는 큰 RAM 객체"를 찾는 린트 (CI 용). 임계값을 낮추면 생기는 오탐도 확인한다.

### Part C — CI 예산 게이트와 ld overflow 예측

- `budget_gate()` — 부품별 초과(OVER)·경고(WARN, 기본 80 %) 판정, 초과 바이트 수.
- 같은 이미지를 STM32F411RE(512K/128K), STM32F103C8(64K/20K), STM32L031K6(32K/8K)에 대 본다.
- `rx_buf` 를 1 KB → 120 KB 로 키운 빌드의 ld 에러 `region 'RAM' overflowed by 40 bytes` 를 **링크 전에** 예측하고 캡처(`ldmem_ovf.txt`)와 대조한다.

## 2. Problem (English)

> **Firmware memory budget from build artifacts.** You are given the outputs of an `arm-none-eabi-gcc` build for a Cortex-M4 part with Flash at `0x08000000` (512 KB) and SRAM at `0x20000000` (128 KB): `size -A -x`, `size -B`, the linker map (`-Wl,-Map`, built with `-ffunction-sections -fdata-sections`), and `-Wl,--print-memory-usage`.
>
> 1. `int parse_size_A(const char *txt, sec_t *out, int cap)` — returns the number of sections, or -1 if the `Total` line is missing or `cap` is exceeded. `bool parse_size_B(const char *txt, berk_t *b)`. `bool map_symbol(const char *map, const char *sym, uint32_t *val)` — exact-name match only.
> 2. `int budget_build(const sec_t *s, int n, const part_t *p, uint32_t heap, uint32_t stack, budget_t *b)` — Flash = text + rodata + data (load image); RAM = data + bss + heap + stack (+ alignment padding of the heap/stack reservation). Non-allocated sections (address 0: debug info) count nowhere. Return the number of allocated sections outside both regions. Example: good build → Flash 8604 B (1.64 %), RAM 9256 B (7.06 %), exactly what `ld --print-memory-usage` printed.
> 3. `int parse_map_inputs(const char *map, isec_t *out, int cap)` — parse input sections, including the wrapped form where a long name is on its own line and `addr size object` follow on the next line. `int diff_moves(before, nb, after, na, move_t *out, int cap)` — report symbols whose output section kind changed, with RAM delta, largest first. Example: `sine_lut .rodata → .data 8192 B RAM +8192`.
> 4. `unsigned budget_gate(uint32_t flash_used, uint32_t ram_used, const part_t *p, unsigned warn_pct, uint32_t *flash_over_by, uint32_t *ram_over_by)` — `GATE_*_OVER` if used > size, else `GATE_*_WARN` if used·100 > warn_pct·size.
>
> Follow-ups: Why does `.data` cost both Flash and RAM? The map prints a `load address` for `.bss` — does `.bss` use Flash? What is `dec` in `size -B`? Why can adding `const` to a table still leave it in RAM? How large can `rx_buf` grow before the link fails, and why does one extra byte cost eight?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cd <ROOT>/lessons/BM/BM-B05          # 캡처 파일을 상대 경로로 읽는다 — 반드시 레슨 폴더에서
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/BM-B05_st && /tmp/BM-B05_st
```

처음 실행 결과 (실제 출력, 앞 5줄과 마지막 줄):

```text
FAIL starter.c:223  ng == 16 && nbd == 16
FAIL starter.c:224  ng > 0 && strcmp(sg[0].name, ".isr_vector") == 0 && sg[0].size == 0x40u && sg[0].addr == 0x08000000u
FAIL starter.c:226  ro && ro->size == 8240u && ro->addr == 0x08000150u
FAIL starter.c:227  hs && hs->size == 0x1804u && hs->addr == 0x20000c24u
FAIL starter.c:232  parse_size_B(g_sb_good, &bg) && bg.text == 8576u && bg.data == 28u && bg.bss == 9228u && bg.dec == 17832u
...
18/58 passed
```

캡처를 다시 만드는 VM 스크립트 (`ENV: qemu` — 맥 실행에는 필요 없음, 이미 캡처된 파일이 레슨 폴더에 있다):

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/BM/BM-B05
limactl shell elinux -- bash -lc "bash $L/run_vm.sh capture"    # good/bad 빌드 + size/map/ldmem 캡처 → VM /tmp/BM-B05_work/out
limactl copy 'elinux:/tmp/BM-B05_work/out/*' "$L"/               # 캡처를 레슨 폴더로 (Mac 홈은 VM 에서 읽기 전용)
limactl shell elinux -- bash -lc "bash $L/run_vm.sh run"        # 두 이미지를 QEMU netduinoplus2 에서 실행
limactl shell elinux -- bash -lc "bash $L/run_vm.sh overflow"   # rx_buf 120 KB → ld region overflow (ldmem_ovf.txt)
limactl shell elinux -- bash -lc "bash $L/run_vm.sh probe"      # objdump -h, .bin 크기, Reset_Handler 루프, rx_buf 경계
```

<!-- code: run_vm.sh -->

예제 펌웨어와 링커 스크립트 (ST CubeMX 의 `STM32F4xx_FLASH.ld` 구조를 줄인 것 — 링커 스크립트 문법은 TOOL-M01):

<!-- code: fw.c -->

<!-- code: fw.ld -->

## 4. 힌트

<details>
<summary>힌트 1 — size -A 의 addr 0 은 "Flash 0번지" 가 아니다</summary>

`.debug_info`, `.comment`, `.ARM.attributes` 는 할당되지 않는(non-ALLOC) 섹션이라 주소가 0 으로 찍힌다. ELF 파일에는 있지만 칩에는 안 올라간다. 이 레슨의 부품은 Flash 가 `0x08000000` 이라 "addr == 0 이면 건너뜀" 이 통한다. Flash 가 0번지인 칩(nRF52, `mps2-an385`)에서는 이 규칙이 깨진다 — 7. 자주 하는 실수 참고.

</details>

<details>
<summary>힌트 2 — 무엇이 Flash 와 RAM 을 둘 다 먹나</summary>

RAM 영역에 있으면서 **내용(CONTENTS)이 있는** 섹션 = `.data`. 초기값을 어딘가에 저장해 두었다가 부팅 때 복사해야 하므로 Flash(LMA)에도 같은 크기가 있다. `.bss` 와 `._user_heap_stack` 은 내용이 없다(NOBITS) — map 이 `load address 0x0800219c` 를 찍어도 Flash 를 쓰지 않는다. 이 레슨은 이름으로 가린다 (`.bss`/`.noinit` 접두, `._user_heap_stack`).

</details>

<details>
<summary>힌트 3 — 예산표 숫자 맞추기</summary>

16진 → 10진: 0x40 = 64, 0x110 = 272, 0x2030 = 8240, 0x1c = 28, 0xc08 = 3080, 0x1804 = 6148. 6148 = 2048(heap) + 4096(stack) + **4**. 그 4 바이트는 `._user_heap_stack` 첫 줄의 `ALIGN(8)` 패딩이다 (.bss 가 0x20000c24 에서 끝나 8의 배수가 아님). 이걸 빼먹으면 RAM 이 ld 값보다 4 작게 나온다.

</details>

<details>
<summary>힌트 4 — 줄바꿈된 입력 섹션</summary>

` .rodata.str1.4` 처럼 이름만 있는 줄을 만나면 **다음 줄**을 읽어 `0x주소 0x크기 오브젝트` 를 얻는다. `sscanf(line + used, " 0x%lx 0x%lx", ...)` 가 2 가 아니면 줄바꿈형이다. `kind` 는 두 번째 `.` 앞 (`strchr(name + 1, '.')`).

</details>

<details>
<summary>힌트 5 — diff 의 RAM 증감</summary>

`.text`/`.rodata` → `.data`/`.bss` 는 +size, 반대는 -size. good→bad 합이 `Bb.ram_total - Bg.ram_total` (8208) 과 같아야 한다. `.data` 는 4 바이트 `*fill*` 만큼 더 커졌지만 heap/stack 정렬 패딩이 4 → 0 으로 줄어 상쇄된다.

</details>

## 5. 풀이

### Part A — 섹션을 Flash/RAM 으로 나누는 규칙

| 섹션 | 내용 | 놓이는 곳 (VMA) | 저장되는 곳 (LMA) | Flash 비용 | RAM 비용 |
|---|---|---|---|---|---|
| `.isr_vector` | 벡터 테이블 (C 배열) | Flash | Flash | 크기 | 0 |
| `.text` | 코드 | Flash | Flash | 크기 | 0 |
| `.rodata` | `const` 데이터, 문자열 리터럴 | Flash | Flash | 크기 | 0 |
| `.data` | 초기값 있는 전역/static | **RAM** | **Flash** | 크기 | 크기 |
| `.bss` | 0 초기화 전역/static (`= 0` 포함) | RAM | (없음, NOBITS) | 0 | 크기 |
| `._user_heap_stack` | heap·stack **예약** (내용 없음) | RAM | (없음) | 0 | 크기 |
| `.debug_*`, `.comment`, `.ARM.attributes` | 디버그·메타 | 할당 안 됨 (addr 0) | — | 0 | 0 |

근거는 `objdump -h` 의 플래그다 (`run_vm.sh probe`, 실제 출력):

```text
Idx Name          Size      VMA       LMA       File off  Algn
  2 .rodata       00002030  08000150  08000150  00010150  2**3
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  3 .data         0000001c  20000000  08002180  00020000  2**2
                  CONTENTS, ALLOC, LOAD, DATA
  4 .bss          00000c08  2000001c  0800219c  0002001c  2**2
                  ALLOC
  5 ._user_heap_stack 00001804  20000c24  0800219c  00020c24  2**0
                  ALLOC
```

`.data` 는 `LOAD` (Flash 에 바이트가 있다) + VMA 가 RAM. `.bss` 는 `ALLOC` 만 — LMA 칸에 `0800219c` 가 찍혀 있어도 내용이 없으니 Flash 를 안 먹는다. ld 는 LMA 를 "직전 섹션의 VMA−LMA 차이"를 이어받아 계산하므로 map 에도 `.bss ... load address 0x0800219c` 가 나온다. **map 의 "load address" 만 보고 Flash 에 더하면 3080 + 6148 B 를 이중으로 세는 버그**가 된다.

추적 표 — good 빌드 `size_A_good.txt` 한 줄씩:

| 섹션 | size (hex → dec) | addr | 영역 | 분류 | Flash 누적 | RAM 누적 |
|---|---|---|---|---|---|---|
| .isr_vector | 0x40 → 64 | 0x8000000 | Flash | text | 64 | 0 |
| .text | 0x110 → 272 | 0x8000040 | Flash | text | 336 | 0 |
| .rodata | 0x2030 → 8240 | 0x8000150 | Flash | rodata | 8576 | 0 |
| .data | 0x1c → 28 | 0x20000000 | RAM, 내용 있음 | data | **8604** | 28 |
| .bss | 0xc08 → 3080 | 0x2000001c | RAM, NOBITS | bss | 8604 | 3108 |
| ._user_heap_stack | 0x1804 → 6148 | 0x20000c24 | RAM, NOBITS | 2048 + 4096 + pad 4 | 8604 | **9256** |
| .debug_info … .debug_frame (10개) | — | 0x0 | 비할당 | 건너뜀 | 8604 | 9256 |

ld 의 `--print-memory-usage` (`ldmem_good.txt`): `FLASH: 8604 B 512 KB 1.64%`, `RAM: 9256 B 128 KB 7.06%` — 바이트 단위로 같다. 퍼센트는 `used * 10000 / len` 을 버림한 값으로 ld 출력과 자릿수까지 맞췄다 (8604·10000/524288 = 164.1 → 1.64 %).

**size -B 와의 관계.** Berkeley 형식은 섹션을 세 칸에 몰아넣는다: text = 읽기 전용 할당 섹션(코드 + `.rodata` + 벡터), data = 내용 있는 쓰기 가능 섹션, bss = 내용 없는 할당 섹션 (heap/stack 예약까지!). 그래서

```text
   text	   data	    bss	    dec	    hex	filename
   8576	     28	   9228	  17832	   45a8	fw_good.elf
    364	   8240	   9224	  17828	   45a4	fw_bad.elf
```

Flash = text + data = 8576 + 28 = 8604, RAM = data + bss = 28 + 9228 = 9256. `dec` (17832) 는 **아무 물리 메모리도 아니다** — data 를 한 번만 센 합이라 Flash 도 RAM 도 아니다. 그리고 good/bad 의 `dec` 는 4 바이트밖에 차이 나지 않는다. **size -B 의 합만 보는 CI 는 이 버그를 못 잡는다.** (TOOL-B06 에서 `size` 자체를 더 다룬다.)

솔루션의 실제 출력 (예산표 부분):

```text
--- good budget on STM32F411RE (Flash 512 KB / RAM 128 KB) ---
  item                       Flash     RAM
  .isr_vector + .text          336       -
  .rodata                     8240       -
  .data (LMA + VMA)             28      28
  .bss                           -    3080
  heap (_Min_Heap_Size)          -    2048
  stack (_Min_Stack_Size)        -    4096
  align pad                      -       4
  total                       8604    9256
  used                       1.64%   7.06%
--- bad (const missing) budget on STM32F411RE (Flash 512 KB / RAM 128 KB) ---
  item                       Flash     RAM
  .isr_vector + .text          336       -
  .rodata                       28       -
  .data (LMA + VMA)           8240    8240
  .bss                           -    3080
  heap (_Min_Heap_Size)          -    2048
  stack (_Min_Stack_Size)        -    4096
  align pad                      -       0
  total                       8604   17464
  used                       1.64%  13.32%
```

**스택은 사실 예약 자리에 있지 않다.** ST 식 링커 스크립트에서 `_estack = ORIGIN(RAM) + LENGTH(RAM)` = `0x20020000` 이고, 벡터 `[0]` 이 이 값이라 MSP 는 RAM **맨 위**에서 아래로 자란다. `._user_heap_stack` 은 .bss 바로 뒤에 "heap + stack 만큼은 비어 있어야 한다" 는 공간을 잡아 두어 **링크 때 넘치면 에러가 나게 하는 장치**일 뿐이다. 실제로 스택이 4 KB 를 넘어도 아무도 막지 않는다 — 빈 공간 121816 B 를 타고 내려와 heap 과 .bss 를 덮는다 (런타임 검출은 BM-A07 스택 페인팅, BM-A08 MPU 가드).

```svg
<svg viewBox="0 0 760 170" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">good 빌드 RAM 128 KB (0x20000000 → 0x20020000, 왼쪽 = 낮은 주소, 비례 아님)</text>
  <rect x="10" y="40" width="40" height="44" class="f-warn" stroke="currentColor"/>
  <text x="14" y="66" fill="currentColor">.data</text>
  <rect x="50" y="40" width="90" height="44" class="f-acc" stroke="currentColor"/>
  <text x="56" y="58" fill="currentColor">.bss</text>
  <text x="56" y="76" fill="currentColor">3080 B</text>
  <rect x="140" y="40" width="8" height="44" class="f-mut" stroke="currentColor"/>
  <rect x="148" y="40" width="80" height="44" class="f-mut" stroke="currentColor"/>
  <text x="154" y="58" fill="currentColor">heap 예약</text>
  <text x="154" y="76" fill="currentColor">2048 B</text>
  <rect x="228" y="40" width="110" height="44" class="f-mut" stroke="currentColor"/>
  <text x="234" y="58" fill="currentColor">stack 예약</text>
  <text x="234" y="76" fill="currentColor">4096 B (검사용)</text>
  <rect x="338" y="40" width="300" height="44" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="400" y="66" fill="currentColor">비어 있음 121816 B</text>
  <rect x="638" y="40" width="112" height="44" class="f-ok" stroke="currentColor"/>
  <text x="644" y="58" fill="currentColor">실제 스택</text>
  <text x="644" y="76" fill="currentColor">← 아래로 자람</text>
  <text x="10" y="102" fill="currentColor">0x20000000</text>
  <text x="96" y="118" fill="currentColor">0x20000c24 (+4 pad → 0x20000c28)</text>
  <text x="300" y="102" fill="currentColor">0x20002428 = 9256 B 끝</text>
  <text x="626" y="102" fill="currentColor">_estack 0x20020000</text>
  <text x="10" y="146" fill="currentColor">ld 가 세는 RAM 9256 B = .data + .bss + 패딩 + 예약. 예약을 넘는 스택 사용은 링크 때도 런타임에도 자동 검출되지 않는다.</text>
  <text x="10" y="164" fill="currentColor">벡터 [0] = 0x20020000 이 초기 MSP. 스택은 예약 칸이 아니라 RAM 맨 위에서 출발한다.</text>
</svg>
```

### Part B — map 파일 읽기와 두 빌드 diff

map 의 "Linker script and memory map" 부분은 세 종류의 줄로 되어 있다.

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">fw_good.map 발췌 (실제 출력) — 줄 종류</text>
  <rect x="8" y="30" width="468" height="20" class="f-acc"/>
  <text x="12" y="44" fill="currentColor" font-family="monospace">.rodata         0x08000150     0x2030</text>
  <text x="486" y="44" fill="currentColor">출력 섹션 (0열 '.') — 이름 · VMA · 크기</text>
  <text x="12" y="66" fill="currentColor" font-family="monospace"> *(.rodata*)</text>
  <text x="486" y="66" fill="currentColor">스크립트 패턴 ' *(' — 건너뜀</text>
  <rect x="8" y="76" width="468" height="38" class="f-ok"/>
  <text x="12" y="90" fill="currentColor" font-family="monospace"> .rodata.str1.4</text>
  <text x="12" y="108" fill="currentColor" font-family="monospace">                0x08000150       0x1a fw_good.o</text>
  <text x="486" y="90" fill="currentColor">입력 섹션, 이름 14자 이상 → 줄바꿈</text>
  <text x="486" y="108" fill="currentColor">다음 줄에 주소 · 크기 · 오브젝트</text>
  <text x="12" y="130" fill="currentColor" font-family="monospace"> *fill*         0x0800016a        0x2</text>
  <text x="486" y="130" fill="currentColor">정렬 채움 — 건너뜀 (Flash 는 먹는다)</text>
  <rect x="8" y="140" width="468" height="20" class="f-ok"/>
  <text x="12" y="154" fill="currentColor" font-family="monospace"> .data.g_cfg    0x20000014        0x8 fw_good.o</text>
  <text x="486" y="154" fill="currentColor">짧은 이름 — 한 줄형</text>
  <text x="12" y="176" fill="currentColor" font-family="monospace">                0x08000180                sine_lut</text>
  <text x="486" y="176" fill="currentColor">심볼 줄 — map_symbol() 이 읽는다</text>
  <text x="12" y="198" fill="currentColor" font-family="monospace">                0x00001000    _Min_Stack_Size = 0x1000</text>
  <text x="486" y="198" fill="currentColor">스크립트 대입 — map_symbol()</text>
  <rect x="8" y="208" width="468" height="20" class="f-warn"/>
  <text x="12" y="222" fill="currentColor" font-family="monospace">.data   0x20000000   0x1c load address 0x08002180</text>
  <text x="486" y="222" fill="currentColor">VMA = RAM, LMA = Flash</text>
  <rect x="8" y="236" width="468" height="20" class="f-warn"/>
  <text x="12" y="250" fill="currentColor" font-family="monospace">.bss    0x2000001c  0xc08 load address 0x0800219c</text>
  <text x="486" y="250" fill="currentColor">함정: NOBITS 인데 LMA 가 찍힌다</text>
  <text x="10" y="280" fill="currentColor">(공백은 줄여 그림.) 입력 섹션 이름 = 출력 kind + '.' + 심볼 — -fdata-sections 덕분.</text>
</svg>
```

**`-ffunction-sections -fdata-sections` 가 핵심이다.** 이 플래그가 없으면 `fw.o` 의 모든 초기값 전역이 `.data` 입력 섹션 하나로 뭉쳐서 map 에는 ` .data 0x20000000 0x2030 fw.o` 한 줄만 나온다 — "커졌다" 는 알아도 **누가** 커졌는지는 모른다. 플래그를 켜면 `sine_lut` 은 `.rodata.sine_lut` / `.data.sine_lut` 이라는 자기 이름의 입력 섹션을 갖고, map 이 심볼 단위 장부가 된다. (덤: `--gc-sections` 가 안 쓰는 함수·변수를 섹션 단위로 버릴 수 있게 된다.)

줄바꿈 규칙은 이 캡처에서 관찰한 대로다: `.bss.g_rx_buf` (13자)는 한 줄, `.rodata.str1.4` (14자)·`.data.g_fw_name` (15자)은 줄바꿈. 열 폭에 의존하지 말고 "다음 토큰이 0x 로 시작하나" 로 판단하는 게 안전하다. 파서 추적:

| 읽은 줄 | 상태 | 결과 |
|---|---|---|
| ` .isr_vector    0x08000000       0x40 fw_good.o` | 한 줄형 | kind=sym=`.isr_vector`, 0x40 |
| ` .text.startup.main` | 이름만 → 다음 줄 읽기 | — |
| `                0x08000044       0x9c fw_good.o` | 줄바꿈형 완성 | kind `.text`, sym `startup.main`, 0x9c |
| ` .glue_7        0x08000150        0x0 linker stubs` | 크기 0 | 버림 |
| ` *fill*         0x0800017c        0x4` | `' *'` | 버림 |
| ` .rodata.sine_lut` + 다음 줄 `0x08000180 0x2000` | 줄바꿈형 | kind `.rodata`, sym `sine_lut`, 8192 |

good 과 bad 모두 크기 > 0 인 입력 섹션 14개. diff 결과 (실제 출력):

```text
--- diff good → bad (moved input sections) ---
  sine_lut   .rodata  → .data    8192 B  RAM +8192
  cmd_names  .rodata  → .data      16 B  RAM +16
```

8192 + 16 = 8208 = 17464 − 9256. 정확히 RAM 증가분이다 (`.data` 의 4 B `*fill*` 증가와 heap/stack 정렬 패딩 4 → 0 이 상쇄).

**왜 `cmd_names` 도 옮겨 갔나.** `const char *cmd_names[4]` 는 "const char 를 가리키는 포인터"의 배열이다. 문자열(`"help"`…)은 `.rodata.str1.4` 에 그대로 있지만, **포인터 배열 자체는 쓰기 가능**이라 `.data` 로 간다. 배열까지 읽기 전용이려면 `const char *const cmd_names[4]` — const 가 두 개 필요하다. CLI 명령 표, 상태 이름 표, 함수 포인터 디스패치 테이블에서 아주 흔한 실수다.

**린트의 한계.** `lint_large_rw(bad, threshold=256)` 은 `sine_lut` 하나만 잡는다 (16 B 짜리 `cmd_names` 는 못 잡음). 임계를 16 으로 낮추면 3개 — `sine_lut`, `cmd_names`, 그리고 **오탐** `g_fw_name` (실제로 바꿔 쓰는 16 B 버퍼). 그래서 실무에서는 ① 큰 `.data` 객체 린트(임계 수백 B) + ② 기준 빌드와의 diff(PR 마다 "RAM +8208, sine_lut .rodata→.data") 를 같이 쓴다.

**런타임에는 무엇이 다른가.** 두 이미지 모두 QEMU 에서 `qemu exit=0` (사인 합 = 2048 × 4096 검사 통과). 차이는 ① RAM 8 KB ② Reset_Handler 가 부팅마다 2060 워드를 복사 — 실제 루프는 `ldr.w r0,[r3],#4 / str.w r0,[r2],#4 / cmp / bne` 4 명령 (`run_vm.sh probe` 의 objdump), 단어당 대략 5~7 사이클로 잡으면 16 MHz 에서 1 ms 가량 (추정, Flash 대기 상태에 따라 다름) ③ 테이블이 RAM 에 있으니 엉뚱한 포인터 쓰기가 **조용히 테이블을 망가뜨린다**. good 빌드에서는 같은 쓰기가 Flash 주소로 가서 무시되거나 버스 에러가 난다 (STM32 Flash 는 잠금 해제 + 프로그램 절차 없이는 안 써진다 — 정확한 반응은 RM 확인 권장, MPU 로 확실히 막기는 BM-A08).

고친 코드는 한 단어다:

```c
const uint16_t sine_lut[4096] = { ... };            /* .rodata: Flash 8192 B, RAM 0 */
const char *const cmd_names[4] = { "help", "led", "baud", "reset" };   /* 포인터 배열도 .rodata */
```

### Part C — 예산 게이트와 overflow 예측

```c
if (used > len)                         → OVER, over_by = used - len
else if (used * 100 > warn_pct * len)   → WARN   (64비트 곱 — 4 GB 영역도 안전)
```

| 부품 (RM/데이터시트 기준 크기, 확인 권장) | Flash | RAM | good | bad |
|---|---|---|---|---|
| STM32F411RE 512 KB / 128 KB | 8604 (1.64 %) | 9256 / 17464 | OK | OK |
| STM32F103C8 64 KB / 20 KB | 13.1 % | 45.19 % / 85.27 % | OK | **RAM_WARN** |
| STM32L031K6 32 KB / 8 KB | 26.3 % | 9256 > 8192 | **RAM_OVER 1064 B** | RAM_OVER |

L031K6 는 good 빌드도 안 들어간다 — .data + .bss 는 3108 B 뿐인데 heap 2 KB + stack 4 KB **예약**이 6 KB 다. 작은 칩으로 옮길 때 가장 먼저 줄이는 것이 이 예약이다 (malloc 을 안 쓰면 heap 0).

**overflow 예측.** `rx_buf` 를 1024 → 122880 B 로 바꾸면 .bss 가 121856 B 늘어난다. 121856 은 8의 배수라 정렬 패딩은 그대로 4. 예측 RAM = 9256 − 1024 + 122880 = 131112 → 131072 를 **40 B 초과**. 실제 ld (`ldmem_ovf.txt`):

```text
ld: fw_ovf.elf section `._user_heap_stack' will not fit in region `RAM'
ld: region `RAM' overflowed by 40 bytes
Memory region         Used Size  Region Size  %age Used
           FLASH:        8604 B       512 KB      1.64%
             RAM:      131112 B       128 KB    100.03%
collect2: error: ld returned 1 exit status
```

에러 메시지가 `._user_heap_stack` 을 지목하는 것에 주의: 넘친 원인은 .bss(rx_buf)인데, ld 는 **마지막에 자리를 못 찾은 섹션**을 말한다. "heap/stack 을 줄이세요" 로 읽으면 틀린 처방이 된다 — map 에서 가장 큰 .bss 입력 섹션부터 본다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cd <ROOT>/lessons/BM/BM-B05
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/BM-B05_so && /tmp/BM-B05_so      # 58/58 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/BM-B05_as && /tmp/BM-B05_as   # 58/58, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 큰 테이블에 `const` 누락 | `.data` 로 가서 Flash(초기값) + RAM(사본) 둘 다 먹고 부팅마다 복사 | `const uint16_t sine_lut[] = {...};` |
| `const char *names[]` | 문자열만 const, 포인터 배열은 쓰기 가능 → `.data` | `const char *const names[]` |
| 함수 안 `const uint16_t lut[4096] = {...};` (static 없음) | 자동 변수 — 호출마다 **스택에** 8 KB 를 잡고 `.rodata` 에서 복사(memcpy)하는 코드가 생길 수 있다 | `static const uint16_t lut[...]` 또는 파일 범위 |
| map 의 `load address` 가 있으면 Flash 에 더함 | `.bss`·`._user_heap_stack` 에도 찍힌다 (ALLOC only) → 9 KB 이중 계산 | 내용 있는 섹션(`CONTENTS, LOAD`)만 Flash 에. 이름 규칙 또는 `objdump -h` 플래그 |
| `size -B` 의 `dec` 를 Flash 사용량으로 보고 | dec = text + data + bss — 물리 메모리 아님. bad 빌드와 4 B 차이 | Flash = text + data, RAM = data + bss |
| `size -A` 의 모든 섹션을 더함 (Total 0x50ac = 20652) | `.debug_*` 는 칩에 안 올라감 | 할당된 섹션만 (addr ≠ 0 — 단, 아래 줄) |
| Flash 가 0번지인 칩에서 "addr 0 = 디버그" 규칙 사용 | nRF52, mps2 는 `.isr_vector` 가 0x0 → 벡터가 사라짐 | `readelf -S` 의 `A` 플래그(SHF_ALLOC)로 판정 |
| heap/stack 예약 패딩 무시 | `ALIGN(8)` 4 B — ld 값과 안 맞음 | 예약 섹션 크기 전체를 RAM 에 |
| ELF 파일 크기(136060 B)를 이미지 크기로 착각 | 디버그 정보·심볼·64 KB 세그먼트 정렬 포함 | `objcopy -O binary` 결과 8604 B = Flash 사용량 |
| `-fdata-sections` 없이 map 을 diff | 객체 단위 `.data` 한 덩어리 — 누가 커졌는지 모름 | `-ffunction-sections -fdata-sections -Wl,--gc-sections` |
| ld overflow 메시지의 섹션 이름을 범인으로 봄 | 마지막에 자리를 못 찾은 섹션일 뿐 | map 에서 큰 입력 섹션 순으로 정렬해 본다 |

## 8. 면접 Q&A

<details>
<summary>Q1. 초기값이 있는 전역 변수는 왜 Flash 와 RAM 을 둘 다 먹나요?</summary>

전원이 꺼지면 SRAM 내용은 사라진다. 그래서 초기값(`115200`, `"BM-B05 demo"`)은 비휘발성인 Flash 에 저장되고(LMA), 실행 중에는 쓰기가 가능해야 하므로 RAM 에 자리가 있어야 한다(VMA). 리셋 직후 Reset_Handler 가 `_sidata`(Flash) → `_sdata.._edata`(RAM) 로 복사한다 — 이 레슨 map 에서 good 은 `0x08002180 → 0x20000000` 28 B, bad 는 `0x0800016c → 0x20000000` 8240 B. 링커 스크립트에서는 `> RAM AT> FLASH` 한 줄이 이 둘을 나눈다. 꼬리: "`.bss` 는?" → 초기값이 모두 0 이라 저장할 필요가 없어 RAM 만 쓰고, startup 이 0 으로 채운다. 그래서 `int x = 0;` 도 `.bss` 다 (`g_err_count = 0` → `.bss.g_err_count`). 복사·제로화 루프 자체는 BM-B07, QEMU 부팅은 BM-B08.

</details>

<details>
<summary>Q2. map 파일에서 무엇을 알 수 있나요? size 로는 왜 부족하죠?</summary>

size 는 섹션 합계만 준다. map 은 ① 메모리 영역(MEMORY) 정의 ② 출력 섹션별 VMA/크기/LMA ③ 입력 섹션 — **어느 오브젝트(라이브러리 멤버 포함)의 어느 함수·변수가 어디에 몇 바이트** ④ 정렬 채움(`*fill*`) ⑤ 링커 스크립트 심볼 값(`_estack`, `_sidata`, `_Min_Stack_Size`) ⑥ 버려진 섹션(`--gc-sections`, "Discarded input sections") ⑦ 앞부분의 "Archive member included to satisfy reference by file (symbol)" — 왜 newlib 의 `printf` 가 딸려 들어왔는지 — 를 보여 준다. 실무: HardFault 의 PC/BFAR 주소 → 함수·변수 찾기 (연습 3), PR 별 크기 회귀 원인 찾기 (Part B), 오버레이/섹션 배치 확인 (BM-B06). 꼬리: "심볼 단위로 보려면?" → `-ffunction-sections -fdata-sections`, 또는 `nm -S --size-sort` (이 레슨 `nm_*.txt`), `-Wl,--cref` 로 교차 참조.

</details>

<details>
<summary>Q3. `size` 출력의 text/data/bss 로 Flash 와 RAM 사용량을 말해 보세요. dec 는 뭔가요?</summary>

Flash = text + data (data 는 초기값 이미지), RAM = data + bss. good: 8576 + 28 = 8604 B Flash, 28 + 9228 = 9256 B RAM — ld `--print-memory-usage` 와 같다. 주의 셋: ① Berkeley 의 text 에는 `.rodata` 와 벡터 테이블이 들어 있다 (8576 = 64 + 272 + 8240) ② bss 에는 ST 식 heap/stack **예약**까지 들어간다 (9228 = 3080 + 6148) ③ dec = text + data + bss 는 어떤 물리 메모리도 아니다 — good 17832 vs bad 17828 로 거의 같아서, RAM 이 8 KB 늘어난 회귀를 못 본다. `size -A` 로 섹션별로 보거나, ld 의 `--print-memory-usage` 를 CI 에서 파싱한다.

</details>

<details>
<summary>Q4. const 를 붙였는데도 테이블이 RAM 에 있습니다. 왜일까요?</summary>

① 포인터 배열: `const char *t[]` — 배열은 쓰기 가능 → `.data`. `const char *const t[]` 필요 (이 레슨 `cmd_names`). ② 함수 안의 non-static `const` 배열 — 자동 변수라 스택에 만들어지고 `.rodata` 에서 복사될 수 있다. `static const` 로. ③ C++ 에서 생성자가 `constexpr` 이 아닌 타입의 `const` 전역 — 동적 초기화가 필요하므로 `.bss` 에 두고 부팅 때 생성자(`.init_array`)가 채운다. `constexpr`/`constinit` 로 상수 초기화를 강제. ④ 하버드 구조 MCU(AVR): `const` 여도 데이터 공간(RAM)에 복사된다 — `PROGMEM`/`__flash` 와 전용 읽기 함수가 필요. Cortex-M 은 Flash 가 같은 주소 공간이라 `const` 만으로 된다. ⑤ 링커 스크립트가 `.rodata` 를 RAM 에 두는 경우 (RAM 에서 실행하는 이미지, 일부 부트로더). 확인 방법은 늘 같다: map 에서 그 심볼의 출력 섹션과 주소를 본다.

</details>

<details>
<summary>Q5. 스택과 힙 크기는 map 에 어떻게 나타나고, 그게 실제 스택 사용량을 보장하나요?</summary>

ST 식 스크립트는 `_Min_Heap_Size`/`_Min_Stack_Size` 심볼과 `._user_heap_stack` 예약 섹션(0x1804 = 2048 + 4096 + 정렬 4)으로 나타난다. 이건 "적어도 이만큼은 비어 있어라" 는 **링크 타임 검사**일 뿐이다. 실제 MSP 는 벡터 [0] = `_estack` = 0x20020000(RAM 꼭대기)에서 출발하고, 4 KB 를 넘게 써도 하드웨어는 모른다 — 빈 공간을 지나 힙과 .bss 를 덮는다. 실제 필요량은 `-fstack-usage`(함수별 .su) + 호출 그래프 최댓값 + ISR 중첩 깊이(+ FPU 문맥 저장)로 추정하고, 런타임에 스택 페인팅 high-water mark(BM-A07)나 MPU 가드 영역(BM-A08), Cortex-M33 의 MSPLIM 으로 검증한다. 꼬리: "힙은?" → malloc 을 안 쓰면 0 으로. newlib 의 `_sbrk` 가 `end` 심볼부터 키운다 (이 펌웨어는 `-nostdlib` 라 `PROVIDE(end = .)` 가 `[!provide]` — 아무도 안 써서 정의되지 않았다고 map 에 찍힌다).

</details>

<details>
<summary>Q6. 빌드가 "region 'RAM' overflowed by 40 bytes" 로 실패합니다. 어떻게 접근하나요?</summary>

① map 에서 `.bss`/`.data` 입력 섹션을 크기순으로 정렬 (`nm -S --size-sort` 도 됨) — 이번엔 `g_rx_buf` 120 KB. ld 가 지목한 `._user_heap_stack` 은 범인이 아니라 마지막에 못 들어간 섹션이다. ② 줄일 수 있는 것: 버퍼 크기(실제 필요량 = 최대 버스트 × 처리 지연), 안 쓰는 heap 예약, const 누락 테이블(.data → .rodata), 동시에 안 쓰는 버퍼를 union/오버레이로 공유. ③ 다른 RAM 으로: STM32F4 의 CCM(0x10000000, DMA 불가 — 칩별 확인), 별도 SRAM 뱅크를 MEMORY 에 추가하고 섹션 지정. ④ 정렬: 이 레슨에서 `rx_buf` 최대는 122844 B (VM 에서 링크 성공, 100.00 %). 1 B 만 늘려도 `overflowed by 8 bytes` — .bss 끝의 `ALIGN(4)` 와 예약의 `ALIGN(8)` 때문에 1 B 가 8 B 로 커진다 (연습 4).

</details>

<details>
<summary>Q7. 디버그 정보(-g)는 Flash 를 먹나요? ELF 가 136 KB 인데 512 KB 칩에 몇 개 들어가나요?</summary>

안 먹는다. `.debug_*` 는 할당되지 않는 섹션(addr 0)이라 칩에 쓰이지 않는다. `fw_good.elf` 는 136060 B 지만 `objcopy -O binary` 결과는 **8604 B** = ld 의 FLASH 사용량 (good, bad 둘 다 8604). ELF 크기에는 디버그 정보, 심볼 표, 세그먼트를 64 KB 파일 오프셋에 맞추는 정렬이 들어 있다 (`objdump -h` 의 File off 0x00010000). 프로그래머(ST-Link, J-Link)는 ELF 를 받아도 LOAD 세그먼트만 쓴다. 꼬리: "그럼 -g 는 릴리스에서 빼야 하나?" → 이미지에는 영향이 없으니 남기고, 배포 이미지와 같은 빌드의 ELF 를 보관해 크래시 주소를 해석한다 (BM-A09).

</details>

<details>
<summary>Q8. .data 가 크면 부팅 시간에도 영향이 있나요?</summary>

있다. Reset_Handler 의 복사 루프는 워드당 4 명령 (`ldr.w … #4 / str.w … #4 / cmp / bne`, 이 펌웨어 objdump). bad 빌드는 2060 번, good 은 7 번 돈다. 단어당 5~7 사이클로 잡으면 리셋 직후 16 MHz 내부 RC 에서 약 1 ms (추정). 저전력 노드가 Standby 에서 깰 때마다 리셋 경로를 다시 타므로(BM-M11) 매번 이 비용을 낸다. .bss 제로화도 같은 성격이라, 큰 버퍼 중 0 초기화가 필요 없는 것은 `.noinit` 섹션(부트 사이 보존, BM-A09)으로 빼기도 한다. `-fno-tree-loop-distribute-patterns` 를 안 주면 GCC 가 이 루프를 `memcpy`/`memset` 호출로 바꾸는데, `-nostdlib` 에서는 링크 에러가 난다 (BM-B07/B08).

</details>

## 9. 추가 연습

### 연습 1 — size -B 를 size -A 에서 재현하기

`size_A_*.txt` 만으로 `size -B` 의 text/data/bss/dec 를 정확히 재현하라. bad 빌드에서 text 가 8576 → 364 로 떨어진 이유는?

<details>
<summary>답 보기</summary>

Berkeley 분류: Flash 의 할당 섹션 → text, RAM 의 내용 있는 섹션 → data, RAM 의 내용 없는 섹션(.bss + heap/stack 예약) → bss. good 8576/28/9228/17832, bad 364/8240/9224/17828 — 캡처와 일치. bad 의 text 가 줄어든 건 코드가 줄어서가 아니라 `.rodata` 의 8212 B (8192 + 16 + 채움 4) 가 `.data` 칸으로 옮겨 갔기 때문이다 (Berkeley text 는 .rodata 를 포함). text + data(Flash)는 두 빌드 모두 8604, data + bss(RAM)는 8208 증가. 교훈: Berkeley 의 "text 가 줄었다" 를 좋은 소식으로 읽으면 안 된다.

</details>

### 연습 2 — Reset_Handler 의 일의 양

map 의 `_sidata`, `_sdata`, `_edata`, `_sbss`, `_ebss` 로 부팅 때 복사·제로화하는 바이트를 구하라.

<details>
<summary>답 보기</summary>

good: 복사 `_edata − _sdata` = 0x2000001c − 0x20000000 = 28 B (7 워드), LMA `_sidata` = 0x08002180. bad: 0x20002030 − 0x20000000 = 8240 B (2060 워드), LMA 0x0800016c. 제로화는 둘 다 0xc08 = 3080 B. 복사 바이트 = `size -A` 의 `.data` 크기와 같아야 한다 (링커 스크립트의 `ALIGN(4)` 덕분에 워드 배수 — BM-B07 의 꼬리 처리 이야기). 복사 루프는 워드당 4 명령.

</details>

### 연습 3 — 주소 → 심볼 (HardFault 분석)

HardFault 핸들러가 BFAR = `0x20000040` 을 기록했다. good 과 bad 빌드에서 각각 어떤 변수인가? `0x08000100` 은? `0x0800016a` 는?

<details>
<summary>답 보기</summary>

입력 섹션 `[addr, addr + size)` 에 들어가는 것을 찾는다. bad: `sine_lut + 0x10` (테이블이 RAM 에 있다). good: 같은 주소가 `g_dac_dma_buf + 0x1c` — **빌드마다 주소가 다르므로 크래시 덤프는 반드시 그 빌드의 map/ELF 로 해석**해야 한다. `0x08000100` = `Reset_Handler + 0x20` (good). `0x0800016a` 는 `.rodata.str1.4` 뒤 2 B `*fill*` — 어떤 심볼도 아니다 (NULL). `0x20001000` 은 heap 예약 안 — 입력 섹션이 없다. 실무에서는 `addr2line -e fw.elf 0x08000100` (코드), `nm -n` + 이분 탐색 (데이터)도 쓴다.

</details>

### 연습 4 — 링크 전에 overflow 예측

`fw.ld` 의 정렬 규칙(.bss 끝 `ALIGN(4)`, 예약 시작·끝 `ALIGN(8)`)을 그대로 계산해 ① 현재 RAM 9256 ② rx_buf 120 KB 일 때 131112 / 40 B 초과 ③ 들어가는 rx_buf 최대 크기를 구하라.

<details>
<summary>답 보기</summary>

used = ALIGN8(ALIGN8(ALIGN4(_sbss + bss)) + heap + stack) − 0x20000000. good: _sbss 0x2000001c + 3080 = 0x20000c24 → 0x20000c28 → + 6144 = 0x20002428 → **9256** ✓. rx 122880: .bss 124936 → 끝 0x2001e824 → ALIGN8 0x2001e828 (패딩 4, good 과 같음) → + 6144 → 0x20020028 → **131112**, 초과 40 ✓ (`ldmem_ovf.txt`). 최대 rx 는 이분 탐색으로 **122844** (VM 실측: 링크 성공, `RAM: 128 KB 128 KB 100.00%`). 122845 이면 .bss 끝 0x2001e801 → ALIGN4 0x2001e804 → ALIGN8 0x2001e808 → 0x20020008, `overflowed by 8 bytes` — VM 실측과 같다. 1 B 가 8 B 로 커지는 정렬 효과.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cd <ROOT>/lessons/BM/BM-B05
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-B05_pr && /tmp/BM-B05_pr    # 23/23 passed
```

실제 출력:

```text
berkeley(-A) good: text 8576 data 28 bss 9228 | bad: text 364 data 8240 bss 9224
startup copy: good 28 B (7 words), bad 8240 B (2060 words); zero 3080 B
rx_buf max = 122844 B (now 1024)
23/23 passed
```

## 10. 핵심 정리

- Flash = `.isr_vector` + `.text` + `.rodata` + `.data`(LMA 이미지). RAM = `.data` + `.bss` + heap + stack 예약(+정렬). `size -B` 로는 Flash = text + data, RAM = data + bss. `dec` 는 물리 메모리가 아니다.
- `.data` 는 둘 다 먹는다: Flash 에 초기값, RAM 에 사본, 부팅 때 Reset_Handler 가 복사 (`> RAM AT> FLASH`). `.bss`(`= 0` 포함)는 RAM 만.
- map 은 `.bss` 에도 `load address` 를 찍는다 — `objdump -h` 가 `ALLOC` 만이면 Flash 0.
- const 누락 테이블: Flash 합은 그대로, RAM 만 늘고 부팅 복사가 길어지며 테이블이 덮어써질 수 있다. 포인터 표는 `const T *const`. 함수 안이면 `static const`.
- `-ffunction-sections -fdata-sections` 로 map 이 심볼 단위 장부가 된다 → 빌드 diff 로 "누가 어디로 옮겼나" 를 찾는다 (이번: `sine_lut` 8192 + `cmd_names` 16 = RAM +8208).
- ST 식 `._user_heap_stack` 은 링크 타임 공간 검사일 뿐, 실제 스택은 `_estack`(RAM 꼭대기)에서 자란다. 런타임 검출은 BM-A07/A08.
- ld overflow 메시지는 마지막에 못 들어간 섹션을 말한다. 정렬 때문에 1 B 가 8 B 가 될 수 있다 — 예측식으로 링크 전에 계산 가능.
- ELF 크기(136 KB) ≠ 이미지(8604 B). CI 는 `--print-memory-usage` 파싱 + 부품별 게이트 + 기준 빌드 diff.

## 참고자료

- GNU ld 매뉴얼 — MEMORY, Output Section LMA (`AT>`), `LOADADDR`, `--print-memory-usage`, `-Map`, `--gc-sections`, `--cref` — <https://sourceware.org/binutils/docs/ld/>
- GNU binutils `size` (`-A` System V / `-B` Berkeley 형식), `objdump -h`, `nm -S --size-sort`, `objcopy -O binary` — <https://sourceware.org/binutils/docs/binutils/>
- GCC `-ffunction-sections` / `-fdata-sections`, `-fstack-usage` — <https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html>, <https://gcc.gnu.org/onlinedocs/gcc/Developer-Options.html>
- ST STM32CubeF4 의 GCC 링커 스크립트 템플릿 (`STM32F411RETx_FLASH.ld`: `_Min_Heap_Size`, `_Min_Stack_Size`, `._user_heap_stack`) — <https://github.com/STMicroelectronics/STM32CubeF4>
- RM0383 (STM32F411) 메모리 맵: Flash 0x08000000 512 KB, SRAM 0x20000000 128 KB — STM32F103C8 (64 KB/20 KB), STM32L031K6 (32 KB/8 KB) 크기는 각 데이터시트 기준, 확인 권장 — <https://www.st.com>
- Memfault Interrupt 블로그 "From Zero to main(): Bare metal C" (startup, .data/.bss) 와 "Best Firmware Size Tools" (map/size 분석 도구) — <https://interrupt.memfault.com/blog/zero-to-main-1>, <https://interrupt.memfault.com/blog/best-firmware-size-tools>
- QEMU `netduinoplus2` (STM32F405) 머신, semihosting — <https://www.qemu.org/docs/master/system/arm/stm32.html>
- 관련 레슨: TOOL-B06 (size), TOOL-M01 (링커 스크립트 기초), TOOL-A07 (고급 링커 스크립트), BM-B06 (플래시 분할 레이아웃), BM-B07 (.data 복사·.bss 제로화), BM-B08 (QEMU 첫 부팅), BM-A07 (스택 페인팅), BM-A08 (MPU), BM-A09 (.noinit 크래시 덤프)
