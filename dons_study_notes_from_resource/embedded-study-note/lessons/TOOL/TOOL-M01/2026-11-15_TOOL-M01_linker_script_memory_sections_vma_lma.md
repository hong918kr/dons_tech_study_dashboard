---
id: TOOL-M01
time: 75분
updated: 2026-11-15
---
# TOOL-M01 링커 스크립트 기초: MEMORY·SECTIONS·VMA/LMA

## 학습 목표

- Cortex-M 용 링커 스크립트(linker script)를 **빈 파일에서** 쓸 수 있다: `ENTRY`, `MEMORY`(FLASH 512K / RAM 128K), `SECTIONS` 안의 `.isr_vector`(KEEP) / `.text` / `.rodata` / `.data`(`> RAM AT> FLASH`) / `.bss`, 그리고 스타트업이 쓰는 `_sidata/_sdata/_edata/_sbss/_ebss/_estack`.
- 링커가 스크립트를 **어떻게 실행하는지** 말로 설명한다: 입력 섹션(input section) → 출력 섹션(output section) 매칭, 위치 카운터(location counter) `.`, 영역(region)마다 따로 움직이는 주소, VMA(실행 주소)와 LMA(적재 주소).
- `readelf -S` / `readelf -l` / `objdump -h` / 맵 파일 / `--print-memory-usage` 출력에서 **.data 의 VMA ≠ LMA** 를 찾아 읽고, 프로그램 헤더의 `VirtAddr`/`PhysAddr` 가 누구에게 무슨 뜻인지 안다.
- 링커 스크립트 버그 4종(AT> 누락 → 402 MB .bin, KEEP 누락 → 벡터 표 삭제, `ADDR` vs `LOADADDR`, 끝 `ALIGN(4)` 누락)을 **링크 성공한 ELF 에서** 도구로 잡아낸다.
- 실무 확장: 부트로더 16 KB 오프셋, `.noinit (NOLOAD)`, CCM RAM 영역, 고아 섹션(orphan section), GNU ld 와 ld.lld 의 차이를 실측으로 안다.

## 1. 문제

펌웨어 면접의 단골 질문 "링커 스크립트에서 `.data` 의 VMA/LMA 설명해 보세요" 는, 실무에서는 "새 보드로 포팅했더니 전역 변수 초기값이 0 이에요", "`.bin` 이 400 MB 가 됐어요", "`--gc-sections` 켰더니 부팅을 안 해요" 로 나타난다 — 셋 다 링크는 **성공**하고, 셋 다 링커 스크립트 몇 글자 차이다.

링커(GNU ld)는 `.o` 들이 가진 **입력 섹션**(`.text.main`, `.data.g_boot_count`, `.bss.g_tick` …)을 스크립트의 **출력 섹션**(`.text`, `.data`, `.bss` …)에 모으고, 출력 섹션마다 두 개의 주소를 정한다.

- **VMA**(Virtual Memory Address) — 프로그램이 실행될 때 그 데이터가 있는 주소. 코드가 `ldr r0, =g_boot_count` 로 쓰는 주소다.
- **LMA**(Load Memory Address) — 이미지(.bin/.hex)에서 그 바이트가 **저장되는** 주소. 플래시 굽는 도구가 쓰는 주소다.

`.text`/`.rodata` 는 FLASH 에서 바로 실행·읽기되므로 VMA = LMA. `.data` 는 실행 중에 RAM 에서 바뀌어야 하지만, 전원이 꺼져도 남는 곳은 FLASH 뿐이라 **VMA = RAM, LMA = FLASH** 이고, 리셋 직후 스타트업 코드가 LMA → VMA 로 복사한다 (그 코드 자체는 BM-B08). `.bss` 는 0 이라 저장할 바이트가 없다 — RAM 에 자리만 있다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">링커가 하는 일: 입력 섹션 → 출력 섹션 → 메모리 영역 (solution.ld 실측)</text>
  <rect x="10" y="35" width="210" height="280" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="55" fill="currentColor" font-weight="bold">fw.o 의 입력 섹션</text>
  <text x="20" y="77" fill="currentColor" font-family="monospace" font-size="12">.isr_vector      0x40</text>
  <text x="20" y="97" fill="currentColor" font-family="monospace" font-size="12">.text.crc16_nibble …</text>
  <text x="20" y="115" fill="currentColor" font-family="monospace" font-size="12">.text.startup.main</text>
  <text x="20" y="133" fill="currentColor" font-family="monospace" font-size="12">.text.unused_debug_dump</text>
  <text x="20" y="155" fill="currentColor" font-family="monospace" font-size="12">.rodata.k_banner</text>
  <text x="20" y="173" fill="currentColor" font-family="monospace" font-size="12">.rodata.k_crc_table</text>
  <text x="20" y="197" fill="currentColor" font-family="monospace" font-size="12">.data.g_cal_offset</text>
  <text x="20" y="215" fill="currentColor" font-family="monospace" font-size="12">.data.g_boot_count</text>
  <text x="20" y="233" fill="currentColor" font-family="monospace" font-size="12">.data.g_fw_tag   9 B</text>
  <text x="20" y="257" fill="currentColor" font-family="monospace" font-size="12">.bss.g_tick</text>
  <text x="20" y="275" fill="currentColor" font-family="monospace" font-size="12">.bss.g_rx_buf   37 B</text>
  <text x="20" y="300" fill="currentColor" font-size="12">주소는 전부 0 (재배치 전)</text>
  <rect x="260" y="35" width="200" height="280" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="270" y="55" fill="currentColor" font-weight="bold">출력 섹션 (SECTIONS)</text>
  <text x="270" y="80" fill="currentColor" font-family="monospace" font-size="12">.isr_vector  KEEP</text>
  <text x="270" y="110" fill="currentColor" font-family="monospace" font-size="12">.text  *(.text .text.*)</text>
  <text x="270" y="128" fill="currentColor" font-size="12">unused_debug_dump: gc 삭제</text>
  <text x="270" y="162" fill="currentColor" font-family="monospace" font-size="12">.rodata</text>
  <text x="270" y="215" fill="currentColor" font-family="monospace" font-size="12">.data  &gt; RAM AT&gt; FLASH</text>
  <text x="270" y="265" fill="currentColor" font-family="monospace" font-size="12">.bss   &gt; RAM (NOBITS)</text>
  <path d="M220 75 L258 77" stroke="currentColor" stroke-width="1.5"/>
  <path d="M220 110 L258 108" stroke="currentColor" stroke-width="1.5"/>
  <path d="M220 165 L258 160" stroke="currentColor" stroke-width="1.5"/>
  <path d="M220 215 L258 212" stroke="currentColor" stroke-width="1.5"/>
  <path d="M220 266 L258 263" stroke="currentColor" stroke-width="1.5"/>
  <rect x="500" y="35" width="230" height="130" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="510" y="55" fill="currentColor" font-weight="bold">FLASH 0x08000000 (512K)</text>
  <text x="510" y="77" fill="currentColor" font-family="monospace" font-size="12">0x08000000 .isr_vector</text>
  <text x="510" y="97" fill="currentColor" font-family="monospace" font-size="12">0x08000040 .text</text>
  <text x="510" y="117" fill="currentColor" font-family="monospace" font-size="12">0x08000110 .rodata</text>
  <text x="510" y="137" fill="currentColor" font-family="monospace" font-size="12">0x0800014c .data 사본 (LMA)</text>
  <text x="510" y="155" fill="currentColor" font-size="12">끝 0x08000160 = .bin 352 B</text>
  <rect x="500" y="185" width="230" height="130" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="510" y="205" fill="currentColor" font-weight="bold">RAM 0x20000000 (128K)</text>
  <text x="510" y="227" fill="currentColor" font-family="monospace" font-size="12">0x20000000 .data (VMA)</text>
  <text x="510" y="247" fill="currentColor" font-family="monospace" font-size="12">0x20000014 .bss</text>
  <text x="510" y="267" fill="currentColor" font-family="monospace" font-size="12">0x20000040 _ebss</text>
  <text x="510" y="287" fill="currentColor" font-family="monospace" font-size="12">0x20020000 _estack</text>
  <text x="510" y="305" fill="currentColor" font-size="12">스택은 끝에서 아래로</text>
  <path d="M460 212 L498 140" stroke="currentColor" stroke-width="2" stroke-dasharray="5 3"/>
  <path d="M460 214 L498 226" stroke="currentColor" stroke-width="2"/>
  <text x="466" y="178" fill="currentColor" font-size="12">LMA</text>
  <text x="466" y="236" fill="currentColor" font-size="12">VMA</text>
</svg>
```

레슨 파일:

| 파일 | 역할 |
|---|---|
| `fw.c` | 데모 펌웨어: 벡터 표(`.isr_vector`), C 스타트업 `Reset_Handler`, `.data`/`.bss`/`.rodata` 전역, 안 쓰는 함수 하나 (`-DPRACTICE` 로 `.noinit`/`.ccmram` 변수 추가) |
| `starter.ld` / `solution.ld` / `practice.ld` | 변형 = 링커 스크립트. starter 에 TODO 4곳 |
| `check.sh` | `arm-none-eabi-gcc -T <변형>.ld` 로 링크 → readelf/objdump/objcopy/ld 출력으로 17개(practice 20개) 판정 |
| `demo_mac.sh` → `demo_mac.txt` | 정상/버그별 실제 도구 출력 (맥, gcc 16.2 + binutils 2.47) |
| `demo_vm_lld.sh` → `demo_vm_lld.txt` | VM 에서 같은 스크립트를 GNU ld 2.45 와 **ld.lld 21** 로 링크해 비교 |

### Part A — MEMORY / SECTIONS / VMA·LMA 를 직접 쓰고 읽기

`starter.ld` 의 뼈대(MEMORY, `_estack`, 다섯 출력 섹션)는 주어져 있다. 링크 결과를 `readelf -S`(섹션 주소), `objdump -h`(VMA/LMA 한 줄), `readelf -l`(프로그램 헤더 PhysAddr/VirtAddr), 맵 파일의 `load address`, `--print-memory-usage` 로 읽고, 숫자 하나하나가 스크립트의 어느 줄에서 왔는지 설명한다. 벡터 표 첫 워드 = `_estack` = `ORIGIN(RAM)+LENGTH(RAM)`, 둘째 워드 = `Reset_Handler|1`.

### Part B — 링크는 되는데 틀린 이미지: TODO 4곳

starter 는 **경고 없이 링크된다.** 그런데 (1) `.data` 에 `AT> FLASH` 가 없고, (2) `.isr_vector` 에 `KEEP` 이 없고, (3) `_sidata` 가 `ADDR(.data)` 이고, (4) `.data`/`.bss` 끝의 `ALIGN(4)` 가 빠졌다. 네 버그가 각각 이미지에서 어떤 모습으로 보이는지(바이트 수, 주소, ld 메시지) 실측으로 확인하고 고친다.

### Part C — 펌웨어 현실: 예산, 고아 섹션, 링커 두 개

`--print-memory-usage` 를 CI 예산 검사로 읽고, 스크립트에 규칙이 없는 섹션(`.ccmram`)이 어디로 가는지(고아 섹션), `NOLOAD` 가 왜 필요한지, 같은 스크립트를 ld.lld 로 링크하면 무엇이 달라지는지(VM 실측) 본다. 9. 추가 연습에서 16 KB 부트로더 오프셋 + `.noinit` + CCM RAM 을 `practice.ld` 로 완성한다.

## 2. Problem (English)

> **Write the linker script for a Cortex-M4 image and prove the layout with binutils.**
>
> Part: 512 KB FLASH at `0x08000000`, 128 KB RAM at `0x20000000`. You get `fw.c` (vector table in section `.isr_vector`, a C `Reset_Handler` that copies `.data` from `_sidata` to `[_sdata, _edata)` word by word and zeroes `[_sbss, _ebss)` word by word) and a broken `starter.ld` that links without any warning.
>
> Fix the script so that, when linked with `arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -nostdlib -ffreestanding -ffunction-sections -fdata-sections -Wl,--gc-sections -T fw.ld`:
>
> 1. `.isr_vector` (64 bytes) is at `0x08000000`, survives `--gc-sections`, word 0 = `_estack` = end of RAM, word 1 = `Reset_Handler | 1`.
> 2. `.data` runs from RAM but its initial values are stored in FLASH right after `.rodata` (VMA ≠ LMA); `_sidata` is the LMA.
> 3. `.bss` is NOBITS in RAM after `.data`; `_sdata/_edata/_sbss/_ebss` are 4-byte aligned.
> 4. `objcopy -O binary` produces exactly `LOADADDR(.data) + SIZEOF(.data) - 0x08000000` bytes (352 here), and the bytes at `_sidata` are the `.data` initial values.
>
> Example (`readelf -l`): `LOAD 0x002000 0x20000000 0x0800014c 0x00014 0x00040 RW` — VirtAddr in RAM, PhysAddr in FLASH, FileSiz < MemSiz (the difference is `.bss`).
>
> Follow-ups: VMA vs LMA; who uses PhysAddr; why `used` is not enough under `--gc-sections`; why `.bss` costs no Flash; what happens to a section the script does not mention; how to reserve 16 KB for a bootloader.

## 3. 시작 코드

```text
TOOL-M01/
  fw.c                 데모 펌웨어 (그대로 둔다)
  starter.ld           고칠 링커 스크립트 (TODO 1~4)
  solution.ld          정답          practice.ld   9. 추가 연습 답 (bootloader offset · .noinit · CCMRAM)
  check.sh             체커          demo_mac.sh/.txt   demo_vm_lld.sh/.txt
```

`check.sh` 는 `/tmp/TOOL-M01_work/<변형>/` 에서 `fw.c` 를 컴파일하고 `<변형>.ld` 로 링크한 뒤(실행은 안 함), `readelf -S -W` / `objdump -h` / `readelf -l -W` / `readelf -s -W` / `objcopy -O binary` / ld 의 `--print-gc-sections`·`--print-memory-usage` 출력에서 숫자를 뽑아 판정한다. `.bin` 을 만들기 전에 `readelf -l` 의 LOAD 세그먼트로 **이미지 범위를 먼저 계산**해서, 1 MiB 가 넘으면 만들지 않고 FAIL 한다 (starter 는 402 MB 짜리를 만들 뻔한다). 맥 전용, 0.5 초 안팎.

<!-- code: check.sh -->

데모 펌웨어:

<!-- code: fw.c -->

고칠 스크립트:

<!-- code: starter.ld -->

```bash
bash check.sh starter     # FAIL 이 나와야 정상 (exit 1)
bash check.sh solution    # 17/17
bash check.sh practice    # 20/20
bash demo_mac.sh > demo_mac.txt            # 맥: 정상/버그별 도구 출력
bash demo_vm_lld.sh > demo_vm_lld.txt      # VM: GNU ld vs ld.lld
```

`bash check.sh starter` 실제 출력:

```text
PASS  link: arm-none-eabi-gcc -T starter.ld --gc-sections 성공
PASS  ENTRY: e_entry 0x800008d == Reset_Handler (Thumb bit 1)
FAIL  .isr_vector addr=0x08000000 size=0 (기대 0x08000000, 64)
FAIL  vector[0] = -, _estack=0x20020000 (기대 0x20020000)
FAIL  vector[1] = -, Reset_Handler=0x0800008d
FAIL  --print-gc-sections: 엉뚱한 섹션이 지워졌거나 안 지워짐
        | removing unused section '.text.Default_Handler' removing unused section '.text.unused_debug_dump' removing unused section '.isr_vector'
PASS  .data VMA 0x20000000 는 RAM 안
FAIL  .data LMA 0x20000000 — FLASH 가 아니거나 VMA 와 같다 (AT> FLASH ?)
FAIL  .data LMA 0x20000000 != .rodata 끝 0x08000108
FAIL  readelf -l: .data 세그먼트 PhysAddr=0x20000000 (FLASH 여야 함)
FAIL  _sidata 0x20000000 != LOADADDR(.data) 0x20000000 — 스타트업이 RAM 을 RAM 에 복사한다
PASS  _sdata/_edata == .data 경계 [0x20000000, 0x20000011)
FAIL  정렬: _edata=0x20000011 _ebss=0x2000003d — 워드 루프가 끝을 최대 3 바이트 넘는다
PASS  .bss NOBITS, [_sbss,_ebss) == .bss, .data 뒤 RAM 안
FAIL  objcopy -O binary 범위 402653201 바이트 (0x08000000..0x20000011) — 만들지 않음 (.data LMA 가 RAM?)
FAIL  _sidata 위치의 .bin 내용 검사 불가 (이미지 없음)
FAIL  --print-memory-usage: FLASH=264 (기대 402653201) RAM=61 (기대 61)
5/17 passed
```

`_sidata 0x20000000 != LOADADDR(.data) 0x20000000` 줄이 이상해 보이는 건 정상이다 — 버그 (2)(AT> 없음) 때문에 지금은 LOADADDR 도 RAM 이라 두 값이 "같은데 둘 다 틀린" 상태다. (2) 만 고치면 이 줄은 `_sidata 0x20000000 != LOADADDR(.data) 0x0800014c` 로 바뀐다 (버그를 하나씩 넣어 본 실측).

## 4. 힌트

<details><summary>힌트 1 — 벡터 표가 0 바이트가 된 이유</summary>

`--print-gc-sections` 가 `removing unused section '.isr_vector'` 라고 말한다. `--gc-sections` 는 `ENTRY` 심볼(과 몇 가지 뿌리)에서 **참조를 따라 도달할 수 없는** 입력 섹션을 지운다. 벡터 표는 하드웨어가 읽을 뿐, 어떤 코드도 참조하지 않는다. C 의 `used` 속성은 *컴파일러*에게 "지우지 마" 일 뿐이다. 링커에게 말하는 방법은 스크립트에 있다.
</details>

<details><summary>힌트 2 — LMA 를 FLASH 로 보내는 문법</summary>

출력 섹션 뒤의 `> REGION` 은 VMA 를, `AT> REGION` 은 LMA 를 정한다. `AT>` 를 안 쓰면 LMA = VMA. `AT>` 로 정한 LMA 는 그 영역(FLASH)의 "다음 빈 자리" 에서 이어진다 — 그래서 `.rodata` 바로 뒤에 붙는다.
</details>

<details><summary>힌트 3 — ADDR 과 LOADADDR</summary>

`ADDR(sec)` = VMA, `LOADADDR(sec)` = LMA. 스타트업은 FLASH 에 있는 **원본**을 RAM 의 **실행 주소**로 복사한다. 원본 주소는 둘 중 무엇인가?
</details>

<details><summary>힌트 4 — 37 바이트 배열과 워드 루프</summary>

`Reset_Handler` 는 `uint32_t *dst` 로 `dst < _ebss` 동안 4 바이트씩 쓴다. `_ebss = 0x2000003d` 이면 마지막 쓰기는 `0x2000003c..0x2000003f` — 끝을 3 바이트 넘는다. 섹션 안에서 `. = ALIGN(4);` 를 **끝 심볼을 찍기 전에** 두면 위치 카운터가 4 의 배수로 올라간다.
</details>

## 5. 풀이

### Part A — 링커가 스크립트를 실행하는 방법

링커 스크립트는 선언문이 아니라 **위에서 아래로 실행되는 작은 프로그램**이라고 생각하면 정확하다 (GNU ld 매뉴얼 "Linker Scripts" 요지).

| 줄 | 링커가 하는 일 |
|---|---|
| `ENTRY(Reset_Handler)` | ELF 헤더 `e_entry` 를 정한다(실측 `0x80000d1`, Thumb bit). `--gc-sections` 의 뿌리(root)도 된다. **하드웨어는 e_entry 를 안 본다** — 코어는 벡터 표 [1] 을 읽는다. e_entry 는 디버거·로더용 |
| `MEMORY { FLASH (rx) : ORIGIN = …, LENGTH = 512K … }` | 이름 붙은 주소 범위. 각 영역은 **자기만의 위치 카운터**를 가진다 — FLASH 에 하나 놓으면 FLASH 쪽 "다음 빈 자리" 만 움직인다. 넘치면 링크 에러 (`region 'FLASH' overflowed`) — 이게 공짜 크기 검사 |
| `_estack = ORIGIN(RAM) + LENGTH(RAM);` | 심볼 대입. 메모리를 차지하지 않는 **주소 값**만 있는 심볼(실측 `0x20020000`) |
| `.isr_vector : { … } > FLASH` | 출력 섹션을 만들고, VMA 를 FLASH 의 다음 빈 자리(처음이니 `0x08000000`)로 정한다 |
| `KEEP(*(.isr_vector))` | "모든 입력 파일(`*`)의 `.isr_vector` 섹션을 여기에" + gc 의 뿌리로 등록 |
| `. = ALIGN(4);` | 위치 카운터 `.` (현재 출력 섹션 안의 현재 주소)를 4 의 배수로 올림 |
| `_sdata = .;` | 지금 위치를 심볼로 기록 — 섹션 안에서는 **VMA** 기준 |
| `} > RAM AT> FLASH` | VMA 는 RAM 의 다음 빈 자리, LMA 는 FLASH 의 다음 빈 자리 |
| `_sidata = LOADADDR(.data);` | `.data` 의 LMA 를 심볼로 |

입력 섹션 패턴 `*(.text .text.*)` 는 "모든 파일에서, 이름이 `.text` 이거나 `.text.` 로 시작하는 섹션" 이다. `-ffunction-sections -fdata-sections` 로 컴파일하면 함수/변수마다 섹션이 따로 생겨서(`.text.crc16_nibble`, `.data.g_fw_tag` — demo_mac.txt 1절) 그 단위로 gc 할 수 있다. `main` 은 GCC 가 `.text.startup.main` 에 넣었다 — 패턴이 `.text.*` 라서 따라 들어온다.

**solution 링크 결과 읽기** (demo_mac.txt 3~5절, 실측):

```text
  [Nr] Name              Type            Addr     Off    Size   ES Flg Lk Inf Al
  [ 1] .isr_vector       PROGBITS        08000000 001000 000040 00   A  0   0  4
  [ 2] .text             PROGBITS        08000040 001040 0000d0 00  AX  0   0  4
  [ 3] .rodata           PROGBITS        08000110 001110 00003c 00   A  0   0  2
  [ 4] .data             PROGBITS        20000000 002000 000014 00  WA  0   0  4
  [ 5] .bss              NOBITS          20000014 002014 00002c 00  WA  0   0  4

Idx Name          Size      VMA       LMA       File off  Algn
  3 .data         00000014  20000000  0800014c  00002000  2**2
  4 .bss          0000002c  20000014  08000160  00002014  2**2

Entry point 0x80000d1
  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
  LOAD           0x001000 0x08000000 0x08000000 0x0014c 0x0014c R E 0x1000
  LOAD           0x002000 0x20000000 0x0800014c 0x00014 0x00040 RW  0x1000
   00     .isr_vector .text .rodata
   01     .data .bss
```

- `readelf -S` 의 `Addr` 는 **VMA 만** 보여 준다. LMA 를 보려면 `objdump -h`(LMA 열) 또는 `readelf -l`(PhysAddr) 을 본다. 면접에서 "readelf -S 로 LMA 를 어떻게 봐요?" 는 함정 질문이다.
- `.data` LMA `0x0800014c` = `.rodata` 끝(`0x08000110 + 0x3c`). `AT> FLASH` 가 FLASH 의 위치 카운터를 이어 썼다.
- `.bss` 의 LMA `0x08000160` 은 objdump 가 계산해 보여 줄 뿐 의미 없는 값이다 — NOBITS 라 저장할 바이트가 없다. 맵 파일도 `.bss 0x20000014 0x2c load address 0x08000160` 이라고 쓴다.
- 프로그램 헤더(program header, 세그먼트)는 "파일의 Offset 부터 FileSiz 바이트를 PhysAddr 에 놓고, 실행 시 VirtAddr 에 MemSiz 만큼 있다고 봐라" 는 로더용 요약이다. 두 번째 LOAD 의 `FileSiz 0x14 < MemSiz 0x40` — 차이 `0x2c` 가 바로 `.bss` (파일에는 없고 메모리에만). OS 로더는 VirtAddr 를 쓰고, 베어메탈 쪽 도구(objcopy, 플래시 프로그래머, QEMU 의 ELF 로더)는 PhysAddr 를 쓴다 — BM-B08 에서 QEMU 가 `.data` 를 LMA 에만 올려 RAM 이 0 이었던 그 실측이 이것이다.

맵 파일(`-Wl,-Map=`)은 같은 사실을 한 줄로 쓴다 (demo_mac.txt 7절):

```text
82:.data           0x20000000       0x14 load address 0x0800014c
98:                0x0800014c                        _sidata = LOADADDR (.data)
```

**이미지(.bin)** — `objcopy -O binary` 는 LOAD 할 섹션들을 **LMA 순서로**, 가장 낮은 LMA 부터 가장 높은 LMA 끝까지 빈칸을 0 으로 채워 그대로 쓴다. 그래서 크기 = `0x0800014c + 0x14 - 0x08000000 = 0x160 = 352` 바이트이고, 끝 20 바이트가 `.data` 초기값이다 (demo_mac.txt 8절, offset 0x130 부터):

```text
352 bytes  sol.bin
           42  20  63  30  84  40  a5  50  c6  60  e7  70  08  81  29  91
           4a  a1  6b  b1  8c  c1  ad  d1  ce  e1  ef  f1  d6  ff  ff  ff
           07  00  00  00  66  77  2d  31  2e  34  2e  32  00  00  00  00
-- 벡터 표 처음 8 바이트 (리틀 엔디언 워드: [0]=_estack, [1]=Reset_Handler|1):
                 20020000        080000d1
```

`d6 ff ff ff` = `g_cal_offset = -42`, `07 00 00 00` = `g_boot_count = 7`, `66 77 2d …` = `"fw-1.4.2"` + 패딩. 링커는 `.data` 안 순서를 GCC 가 내보낸 입력 순서대로 놓았다 (`g_cal_offset, g_boot_count, g_fw_tag` — 소스의 정의 순서와 반대였다; 이 순서에 기대는 코드는 쓰면 안 된다).

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">.bin(LMA) 과 실행 중 RAM(VMA) — 리셋 직후 Reset_Handler 가 하는 두 루프</text>
  <text x="10" y="50" fill="currentColor" font-size="12">FLASH / fw.bin</text>
  <rect x="110" y="35" width="90" height="30" class="f-mut" stroke="currentColor"/>
  <text x="115" y="55" fill="currentColor" font-size="12">.isr_vector</text>
  <rect x="200" y="35" width="200" height="30" class="f-mut" stroke="currentColor"/>
  <text x="270" y="55" fill="currentColor" font-size="12">.text 0xd0</text>
  <rect x="400" y="35" width="110" height="30" class="f-mut" stroke="currentColor"/>
  <text x="420" y="55" fill="currentColor" font-size="12">.rodata</text>
  <rect x="510" y="35" width="70" height="30" class="f-acc" stroke="currentColor"/>
  <text x="515" y="55" fill="currentColor" font-size="12">.data 사본</text>
  <text x="105" y="82" fill="currentColor" font-family="monospace" font-size="12">0x08000000</text>
  <text x="470" y="82" fill="currentColor" font-family="monospace" font-size="12">0x0800014c</text>
  <text x="590" y="55" fill="currentColor" font-family="monospace" font-size="12">끝 +352 B</text>
  <text x="10" y="180" fill="currentColor" font-size="12">RAM (실행 중)</text>
  <rect x="110" y="165" width="70" height="30" class="f-acc" stroke="currentColor"/>
  <text x="117" y="185" fill="currentColor" font-size="12">.data</text>
  <rect x="180" y="165" width="130" height="30" class="f-ok" stroke="currentColor"/>
  <text x="200" y="185" fill="currentColor" font-size="12">.bss (0 으로)</text>
  <rect x="310" y="165" width="350" height="30" class="f-mut" stroke="currentColor"/>
  <text x="340" y="185" fill="currentColor" font-size="12">빈 RAM … 스택은 _estack 에서 아래로 자람</text>
  <text x="105" y="212" fill="currentColor" font-family="monospace" font-size="12">0x20000000</text>
  <text x="180" y="230" fill="currentColor" font-family="monospace" font-size="12">_sbss 0x20000014  _ebss 0x20000040</text>
  <text x="590" y="212" fill="currentColor" font-family="monospace" font-size="12">0x20020000</text>
  <path d="M545 68 L150 160" stroke="currentColor" stroke-width="2"/>
  <text x="300" y="110" fill="currentColor" font-size="12">① _sidata → [_sdata, _edata) 5 워드 복사</text>
  <text x="300" y="130" fill="currentColor" font-size="12">② [_sbss, _ebss) 11 워드 0 (FLASH 에 아무것도 없음)</text>
  <text x="10" y="262" fill="currentColor" font-size="12">readelf -l: PhysAddr 0x0800014c FileSiz 0x14 → VirtAddr 0x20000000 MemSiz 0x40 (차이 = .bss)</text>
</svg>
```

스타트업 쪽에서 보면 이 심볼들은 그냥 **주소 상수**다. `Reset_Handler` 디스어셈블리(demo_mac.txt 9절)의 리터럴 풀(literal pool):

```text
 80000fc:	.word	0x0800014c      ← _sidata  (LMA)
 8000100:	.word	0x20000000      ← _sdata
 8000104:	.word	0x20000014      ← _edata
 8000108:	.word	0x20000014      ← _sbss
 800010c:	.word	0x20000040      ← _ebss
```

C 에서 `extern uint32_t _sdata[];` 처럼 **배열로** 선언하는 이유: 링커 심볼에는 "값" 이 없고 주소만 있다. `extern uint32_t _sdata;` 로 선언하면 `&_sdata` 를 써야 하고, 실수로 `_sdata` 를 쓰면 그 주소의 내용을 읽는다. 배열 이름은 그대로 주소라 실수할 틈이 없다. 또 이 빌드는 `-Os/-O2/-O3` 모두 복사 루프가 `memcpy` 호출로 바뀌지 않고 링크됐다(실측) — GCC 가 그런 변환을 할 수 있다는 점과 대처는 BM-B08.

`readelf -s` 와 `nm` 의 차이 하나: Thumb 함수의 심볼 값은 ELF 안에서 **bit 0 = 1** 이다(`Reset_Handler` = `080000d1`). `arm-none-eabi-nm` 은 같은 심볼을 `080000d0` 으로 보여 준다 (demo_mac.txt 6절). 그래서 check.sh 는 벡터 [1] 과 비교할 때 `readelf -s` 의 원래 값을 쓴다.

### Part B — 네 버그가 이미지에서 보이는 모습

starter 의 네 버그를 하나씩만 넣어 링크한 실측 (demo_mac.txt 10~13절, 버그별 check FAIL 줄):

| 버그 | ld 경고 | 결과 | check FAIL 줄 |
|---|---|---|---|
| B1 `.data` 에 `AT> FLASH` 없음 | 없음 | LMA = VMA = `0x20000000`, `.bin` **402,653,204 B** | LMA ×3, `_sidata`, objcopy 범위, 내용, memory-usage (7줄) |
| B2 `KEEP` 없음 | 없음 | `.isr_vector` 0 바이트, `0x08000000` 에 `crc16_nibble` 코드 | vector 크기, [0], [1], gc (4줄) |
| B3 `_sidata = ADDR(.data)` | 없음 | `_sidata = 0x20000000` (RAM→RAM 복사) | `_sidata`, 내용 (2줄) |
| B4 끝 `ALIGN(4)` 없음 | 없음 | `_edata = 0x20000011`, `_ebss = 0x2000003d` | 정렬 (1줄) |

**B1 — AT> 누락.** `.data` 가 VMA 대로 RAM 에 "저장" 된다. 링크도 되고 `.elf` 를 디버거로 올리면(로더가 RAM 에 직접 써 주니) 돌아가기까지 한다 — 그래서 늦게 발견된다. 이미지로 뽑는 순간 드러난다:

```text
  LOAD           0x002000 0x20000000 0x20000000 0x00014 0x00040 RW  0x1000
           FLASH:         332 B       512 KB      0.06%
402653204 bytes  b1.bin  (0x08000000 .. 0x20000014 을 0 으로 채움)
-- ihex 로 뽑으면: 확장 선형 주소 레코드(:02000004 HHHH) 가 RAM(0x2000) 을 가리킨다
1::020000040800F2
23::020000042000DA
```

`objcopy -O binary` 는 FLASH 와 RAM 사이 384 MB 를 0 으로 채웠다(만든 뒤 바로 지움). `.hex` 는 작지만 RAM 주소 레코드가 들어가서, 플래시 도구가 거부하거나(범위 밖) 무시한다 → 초기값이 **어디에도 저장되지 않는다**. 보드에서는 `g_boot_count` 가 7 이 아니라 전원 투입 때의 쓰레기 값(또는 스타트업이 RAM→RAM 으로 "복사" 한 같은 쓰레기)이다. `--print-memory-usage` 의 FLASH 332 B 도 단서다 — `.data` 20 바이트만큼 정상(352)보다 작다.

**B2 — KEEP 누락.** `--gc-sections` 는 뿌리에서 재배치(relocation) 참조를 따라가는 그래프 탐색이다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">--gc-sections = 뿌리에서 참조를 따라 도달한 입력 섹션만 남긴다 (실측)</text>
  <rect x="10" y="40" width="170" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="58" fill="currentColor" font-size="12">뿌리: ENTRY</text>
  <text x="20" y="76" fill="currentColor" font-family="monospace" font-size="12">.text.Reset_Handler</text>
  <rect x="230" y="40" width="160" height="44" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="240" y="66" fill="currentColor" font-family="monospace" font-size="12">.text.startup.main</text>
  <rect x="440" y="30" width="140" height="30" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="450" y="50" fill="currentColor" font-family="monospace" font-size="12">.text.crc16…</text>
  <rect x="440" y="70" width="140" height="30" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="450" y="90" fill="currentColor" font-family="monospace" font-size="12">.rodata.k_*</text>
  <rect x="600" y="50" width="130" height="30" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="610" y="70" fill="currentColor" font-family="monospace" font-size="12">.data.* .bss.*</text>
  <path d="M180 62 L228 62" stroke="currentColor" stroke-width="2"/>
  <path d="M390 55 L438 45" stroke="currentColor" stroke-width="2"/>
  <path d="M390 70 L438 85" stroke="currentColor" stroke-width="2"/>
  <path d="M390 62 L598 65" stroke="currentColor" stroke-width="1.5" stroke-dasharray="4 3"/>
  <rect x="10" y="130" width="170" height="44" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="148" fill="currentColor" font-size="12">아무도 참조 안 함</text>
  <text x="20" y="166" fill="currentColor" font-family="monospace" font-size="12">.isr_vector</text>
  <rect x="230" y="130" width="170" height="44" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="240" y="148" fill="currentColor" font-size="12">벡터 표만 참조</text>
  <text x="240" y="166" fill="currentColor" font-family="monospace" font-size="12">.text.Default_Handler</text>
  <path d="M180 152 L228 152" stroke="currentColor" stroke-width="2"/>
  <rect x="440" y="130" width="200" height="44" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="450" y="148" fill="currentColor" font-size="12">아무도 참조 안 함</text>
  <text x="450" y="166" fill="currentColor" font-family="monospace" font-size="12">.text.unused_debug_dump</text>
  <text x="10" y="205" fill="currentColor" font-size="12">KEEP 없음: 아래 세 섹션 모두 삭제 → 0x08000000 에 crc16_nibble 의 첫 명령어(0x4a09090b)</text>
  <text x="10" y="225" fill="currentColor" font-size="12">KEEP(*(.isr_vector)): .isr_vector 가 뿌리가 됨 → Default_Handler 도 살아남고 unused_debug_dump 만 삭제</text>
</svg>
```

```text
removing unused section '.text.Default_Handler' in file 'b2.o'
removing unused section '.text.unused_debug_dump' in file 'b2.o'
removing unused section '.isr_vector' in file 'b2.o'
-- 0x08000000 의 첫 두 워드 (이제 코드다):
                 4a09090b        3310ea83
```

리셋 때 코어는 `0x08000000` 의 워드를 MSP 로, `0x08000004` 를 PC 로 읽는다 → MSP = `0x4a09090b`(존재하지 않는 주소), PC = `0x3310ea83` → 첫 스택 접근이나 첫 명령 인출에서 바로 폴트(HardFault 핸들러도 같은 망가진 표에서 찾는다 → 보통 lockup). 핵심: `fw.c` 의 `g_vectors` 는 **이미 `used` 속성이 있는데도** 지워졌다. `used` 는 "컴파일러야, 참조가 없어도 이 정의를 `.o` 에 내보내라" 이고, `--gc-sections` 는 그 다음 단계(링커)의 일이다. 링커에게 말하는 방법은 스크립트의 `KEEP` 이다. (GCC 11+/binutils 2.36+ 의 `__attribute__((retain))` 은 섹션에 `SHF_GNU_RETAIN` 플래그를 달아 KEEP 없이도 살린다고 알려져 있다 — 이 레슨에서는 검증 안 함, 확인 권장.)

**B3 — ADDR vs LOADADDR.** `_sidata = 0x20000000 = _sdata` (demo_mac.txt 12절). 복사 루프가 `*dst++ = *src++` 를 같은 주소에 하니 아무 일도 안 일어나고, `.data` 는 전원 투입 때의 SRAM 값 그대로다. QEMU 나 디버거 `load` 로 테스트하면 RAM 이 우연히 맞는 값이라 통과하고, 실제 보드에서 콜드 부팅할 때만 틀린다 — 그래서 check 는 "`_sidata` 위치의 .bin 바이트가 정말 초기값인가" 를 따로 본다.

**B4 — 끝 ALIGN(4) 누락.** 섹션 크기는 입력 섹션 크기의 합 + 정렬 빈칸이다. 마지막 입력이 9 바이트 `g_fw_tag`, 37 바이트 `g_rx_buf` 라서 `_edata = 0x20000011`, `_ebss = 0x2000003d` (demo_mac.txt 13절). 워드 루프는 `dst < _ebss` 인 동안 쓰므로 마지막 워드 `0x2000003c..3f` 를 쓰고 끝을 3 바이트 넘는다. 이 레이아웃에선 그 3 바이트가 정렬 빈칸이라 티가 안 나지만, `.bss` 바로 뒤에 `.noinit`(연습 2)나 바이트 정렬 버퍼가 오면 그 앞 3 바이트가 매 부팅마다 0 이 된다 — "리셋 이유가 가끔 0" 같은 증상. `.data` 쪽은 FLASH 이미지 끝을 3 바이트 넘어 읽는다(지워진 플래시 0xFF 또는 다음 데이터). 시작 쪽 `ALIGN(4)` 는 이 빌드에선 입력 섹션 정렬이 이미 4 라서 없어도 같았지만, 끝 쪽과 짝으로 둔다. 정렬 규칙을 `ASSERT` 로 박아 두는 방법은 TOOL-A07.

### Part C — 펌웨어 현실

**① 예산 검사: `--print-memory-usage`.** ld 가 영역별 "사용량" 을 찍는다. 사용량 = 그 영역에 놓인 마지막 바이트 - ORIGIN (정렬 빈칸 포함). solution 에서 FLASH 352 B 는 `.bin` 크기와 정확히 같고(.data 사본 포함), RAM 64 B = `_ebss - ORIGIN(RAM)` 이다. CI 에서는 이 출력을 저장해 PR 마다 diff 하는 것만으로 "누가 RAM 1 KB 를 먹었나" 가 보인다 (어느 오브젝트가 먹었는지는 맵 파일 — TOOL-M02). starter 의 `FLASH: 264 B` 는 거짓말에 가깝다: 벡터 표와 `.data` 초기값이 빠진 숫자다.

**② 고아 섹션(orphan section).** 스크립트에 규칙이 없는 입력 섹션을 GNU ld 는 에러 없이 **알아서** 비슷한 섹션 근처에 둔다. `-DPRACTICE` 코드(`.noinit`, `.ccmram` 변수)를 `solution.ld` 로 링크한 실측 (demo_mac.txt 15절):

```text
           FLASH:        4476 B       512 KB      0.85%
             RAM:        4168 B       128 KB      3.18%
  [ 4] .data             PROGBITS        20000000 002000 000014 00  WA  0   0  4
  [ 5] .ccmram           PROGBITS        20000014 002014 001000 00  WA  0   0  1
  [ 6] .bss              NOBITS          20001014 003014 00002c 00  WA  0   0  4
  [ 7] .noinit           NOBITS          20001040 003040 000008 00  WA  0   0  4
  LOAD           0x002000 0x20000000 0x08000168 0x01014 0x01040 RW  0x1000
   01     .data .ccmram .bss
```

GCC 는 `section(".ccmram")` 의 0 초기화 배열을 **PROGBITS** 로 냈다(`.o` 의 readelf, 이름이 `.bss.` 로 시작하지 않으니). 그래서 ld 는 `.ccmram` 을 쓰기 가능한 PROGBITS 인 `.data` 뒤에 붙이고 LMA 도 FLASH 로 이어 붙였다: **FLASH 가 4 KB 늘고**(352 → 4476 B, 0 4096 개), CCM 이 아니라 일반 RAM 에 놓였고, `_sdata.._edata` 밖이라 스타트업은 복사도 0 채우기도 안 한다 (값은 콜드 부팅 SRAM 쓰레기). `.noinit` 은 GCC 16 이 이름을 알아보고 NOBITS 로 냈다(실측) — 그래서 `.bss` 뒤로 갔다. 이런 일을 CI 에서 잡는 옵션이 `--orphan-handling=warn|error` 다:

```text
warning: orphan section `.ccmram' from `orph.o' being placed in section `.ccmram'
warning: orphan section `.noinit' from `orph.o' being placed in section `.noinit'
warning: orphan section `.iplt' from `orph.o' being placed in section `.iplt'
warning: orphan section `.rel.iplt' from `orph.o' being placed in section `.rel.dyn'
warning: orphan section `.igot.plt' from `orph.o' being placed in section `.igot.plt'
```

(위는 alloc 섹션만 추린 것. 실제로는 `.debug_*`, `.comment`, `.ARM.attributes`, 링커가 만드는 `.glue_7`/`.v4_bx`/`.vfp11_veneer`/`.iplt`/`.igot.plt` 도 전부 고아로 보고된다 — 바로 `error` 로 켜려면 이들에 대한 규칙(또는 `/DISCARD/`)을 먼저 써야 한다. 벤더 스크립트에 `.glue_7`, `.ARM.exidx` 줄이 있는 이유다.)

**③ `NOLOAD`.** `.bss` 는 입력이 전부 NOBITS 라 출력도 NOBITS — `(NOLOAD)` 를 붙여도 결과가 같았다 (demo_mac.txt 14절, 두 줄 동일). `NOLOAD` 가 진짜 필요한 곳은 입력이 PROGBITS 인데 파일엔 넣고 싶지 않은 섹션이다. `practice.ld` 에서 `.ccmram` 의 `(NOLOAD)` 를 빼 본 실측(16절): `.ccmram PROGBITS 10000000 … 001000`, `LOAD 0x003000 0x10000000 0x10000000 0x01000` → `.bin` 범위 `0x08004000..0x10001000` = 134,205,440 B. B1 과 같은 병이 다른 영역에서 난다.

**④ 링커 두 개: GNU ld vs ld.lld (VM, `demo_vm_lld.txt`).** 같은 `fw.o`(VM arm-none-eabi-gcc 14.2.1) 를 같은 `solution.ld` 로:

| 항목 | GNU ld 2.45 (`arm-none-eabi-gcc -T`) | ld.lld 21.1.8 (`ld.lld -T`) |
|---|---|---|
| 섹션 주소·크기 (`readelf -S`) | `.isr_vector 08000000 0x40` … `.bss 20000014 0x2c` | **같음** |
| `_sidata/_sdata/_edata/_sbss/_ebss` | `0800014c / 20000000 / 20000014 / 20000014 / 20000040` | **같음** |
| `--print-memory-usage` | FLASH 352 B, RAM 64 B | **같음** (lld 도 지원) |
| `--print-gc-sections` | `.text.unused_debug_dump` 1줄 | 빈 `.text`/`.data`/`.bss` + `unused_debug_dump` 4줄 |
| 프로그램 헤더 | LOAD 2개 (RX / RW) | LOAD 5개(섹션마다, R / R E / R / RW / RW) + `GNU_STACK` |
| `.bss` 세그먼트 PhysAddr | `.data` 와 한 세그먼트 | 따로, PhysAddr = VirtAddr `0x20000014` |
| `.bin` | 352 B | 352 B, 단 **2 바이트 다름** |

다른 2 바이트는 `Default_Handler`(2 바이트) 뒤, `main` 의 4-정렬 빈칸 `0x0800006e` 이다: GNU ld 는 `00 00`, lld 는 `d4 d4` 로 채웠다. 출력 섹션에 채움 값 `=0x00000000` 을 주면 두 `.bin` 이 바이트 단위로 같아졌다 (`bfd.bin == lldfill.bin`). 재현 가능한 빌드(TOOL-A06)에서 "링커만 바꿨는데 해시가 다르다" 의 전형적 원인.

lld 의 더 위험한 차이는 `practice.ld`(FLASH 가 `0x08004000` 에서 시작)에서 나왔다:

```text
  LOAD           0x000000 0x08000000 0x08000000 0x00154 0x00154 R   0x10000
  LOAD           0x004000 0x08004000 0x08004000 0x00040 0x00040 R   0x10000
lldp.bin = 384 bytes (objcopy 는 섹션 기준 → 0x08000000 의 헤더 세그먼트는 .bin 에 없음)
-- 0x08000000 세그먼트의 첫 4 바이트 (파일 offset 0):
 177   E   L   F
```

lld 가 FLASH 앞의 빈 16 KB 를 보고 **ELF 헤더 + 프로그램 헤더**(0x154 B)를 `0x08000000` 에 놓는 LOAD 세그먼트를 만들었다 — 하필 부트로더 자리다. `.bin` 은 섹션 기준이라 멀쩡하지만, ELF 의 **세그먼트**를 보고 굽는 도구라면 부트로더 첫 섹터를 덮어쓸 수 있다(어느 도구가 세그먼트 기준인지는 도구마다 확인 권장). `PHDRS { text PT_LOAD; data PT_LOAD; … }` 로 세그먼트를 직접 정하고 섹션마다 `:text` 를 달았더니 그 세그먼트가 사라졌다 (demo_vm_lld.txt 9절). PHDRS 자체는 TOOL-A07.

**⑤ 맥 ld64 에는 이게 없다.** `cc m.c -Wl,-T,solution.ld` → `ld: unknown options: -T`. Mach-O 는 링커 스크립트 대신 `-segaddr`, `-sectcreate`, `-image_base` 같은 옵션으로 조정하고, 그것도 PIE 에선 무시된다 (`ld: warning: Linking with PIE, -image_base will be ignored`, demo_mac.txt 17절). 펌웨어 레이아웃 연습은 그래서 GNU 툴체인이 필수다.

### 고친 뒤 — `bash check.sh solution` 실제 출력

```text
PASS  link: arm-none-eabi-gcc -T solution.ld --gc-sections 성공
PASS  ENTRY: e_entry 0x80000d1 == Reset_Handler (Thumb bit 1)
PASS  .isr_vector @ 0x08000000, 0x40 바이트 (16 벡터) — gc 뒤에도 살아 있음
PASS  vector[0] = 0x20020000 == _estack == ORIGIN(RAM)+LENGTH(RAM)
PASS  vector[1] = 0x080000d1 == Reset_Handler (Thumb bit 1)
PASS  --print-gc-sections: unused_debug_dump 제거, .isr_vector · Default_Handler 유지
PASS  .data VMA 0x20000000 는 RAM 안
PASS  .data LMA 0x0800014c 는 FLASH 안 (VMA != LMA)
PASS  .data LMA == .rodata 끝 0x0800014c (FLASH 에 빈틈 없이 이어 붙음)
PASS  readelf -l: VirtAddr 0x20000000 의 LOAD 세그먼트 PhysAddr 0x0800014c (FLASH)
PASS  _sidata 0x0800014c == LOADADDR(.data)
PASS  _sdata/_edata == .data 경계 [0x20000000, 0x20000014)
PASS  _sdata/_edata/_sbss/_ebss 모두 4-정렬 (uint32_t 루프가 경계를 안 넘음)
PASS  .bss NOBITS, [_sbss,_ebss) == .bss, .data 뒤 RAM 안
PASS  fw.bin = 352 바이트 == LOADADDR(.data)+SIZEOF(.data)-ORIGIN(FLASH) (.bss 는 0 바이트)
PASS  _sidata 위치의 .bin 내용: g_boot_count=7, g_fw_tag="fw-1.4.2" (offset 0x150, 0x154)
PASS  --print-memory-usage: FLASH 352 B (= .bin), RAM 64 B (= _ebss - ORIGIN(RAM))
17/17 passed
```

## 6. 정답 코드

<!-- code: solution.ld diff=starter.ld -->

바뀐 곳은 네 군데다: `KEEP(...)` 로 감싸기, `.data` 끝 `. = ALIGN(4);` + `AT> FLASH`, `LOADADDR`, `.bss` 끝 `. = ALIGN(4);`. 나머지(MEMORY, `_estack`, 섹션 순서)는 starter 그대로다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `.data : { … } > RAM` | LMA = VMA = RAM. 초기값이 이미지에 없고, `.bin` 은 FLASH~RAM 사이를 0 으로 채워 수백 MB | `} > RAM AT> FLASH` (옛 문법 `AT(LOADADDR(.rodata)+SIZEOF(.rodata))` 도 같은 뜻) |
| `*(.isr_vector)` + `--gc-sections` | 벡터 표는 아무도 참조하지 않는 섹션 → 삭제. `used` 속성은 링커 단계와 무관 | `KEEP(*(.isr_vector))` (`.init_array`, 명령 표 같은 등록 테이블도 KEEP — TOOL-M03) |
| `_sidata = ADDR(.data);` | VMA 를 원본으로 씀 → RAM→RAM 복사, 초기값 안 들어옴. QEMU/디버거 load 로는 안 보일 수 있음 | `_sidata = LOADADDR(.data);` |
| `_ebss = .;` 앞에 ALIGN 없음 | 워드 루프가 끝을 최대 3 B 넘어 씀 | `. = ALIGN(4); _ebss = .;` |
| `readelf -S` 로 LMA 를 찾음 | `Addr` 는 VMA 뿐 | `objdump -h` 의 LMA 열, `readelf -l` 의 PhysAddr, 맵 파일 `load address` |
| 새 `section(".xxx")` 를 쓰고 스크립트는 그대로 | 고아 섹션 → ld 가 아무 데나(대개 `.data` 근처) 둠, FLASH 가 몰래 늘어남 | 스크립트에 규칙 추가 + CI 에 `--orphan-handling=warn` 로그 감시 |
| 0 초기화 전용 섹션에 `NOLOAD` 없음 | 컴파일러가 PROGBITS 로 내면 파일·FLASH 에 0 이 실리고 LMA 가 그 영역 | `.ccmram (NOLOAD) : { … } > CCMRAM` |
| `extern uint32_t _sdata;` 후 `_sdata` 로 루프 | 심볼은 주소뿐 — 값을 읽으면 그 주소의 내용 | `extern uint32_t _sdata[];` 로 선언 |

## 8. 면접 Q&A

<details><summary>Q1. VMA 와 LMA 를 설명하고, `.data` 가 왜 둘이 다른지 말해 보세요.</summary>

VMA 는 실행 중 그 데이터가 있는 주소(코드가 쓰는 주소), LMA 는 이미지 안에서 그 바이트가 저장되는 주소다. `.text`/`.rodata` 는 FLASH 에서 바로 쓰이니 같다. `.data` 는 RAM 에서 바뀌어야 하지만 초기값은 전원이 꺼져도 남아야 하므로 FLASH 에 둔다: 스크립트 `> RAM AT> FLASH`, 이 레슨 실측 VMA `0x20000000` / LMA `0x0800014c`. 그 사이를 잇는 것이 스타트업의 복사 루프(`_sidata → [_sdata,_edata)`). 꼬리질문 "그럼 .data 는 Flash 와 RAM 을 둘 다 먹나요?" → 예, 크기만큼 둘 다. 그래서 큰 룩업 테이블에 `const` 를 빼먹으면 두 배 손해다 (TOOL-B06).
</details>

<details><summary>Q2. `readelf -l` 의 VirtAddr 와 PhysAddr 는 누가 쓰나요?</summary>

리눅스 같은 OS 로더는 VirtAddr(가상 주소)에 매핑하고 PhysAddr 는 보통 무시한다. 베어메탈 쪽은 반대로 PhysAddr 가 "이 바이트를 어디에 구워라/올려라" 다: `objcopy -O binary`/ihex 는 섹션 LMA 로 이미지를 만들고, QEMU 의 ELF 로더는 PhysAddr 에 올렸다(BM-B08 실측: `.data` 가 LMA 에만 있고 RAM 은 0). FileSiz < MemSiz 인 세그먼트의 차이는 0 으로 채워야 할 부분(.bss)이다 — 실측 `FileSiz 0x14 MemSiz 0x40`. gdb `load` 가 섹션 LMA 로 쓴다는 것은 일반적으로 알려진 동작이지만 이 레슨에서 실측하진 않았다(TOOL-A03 에서 확인).
</details>

<details><summary>Q3. 벡터 표에 `__attribute__((used))` 를 달았는데 `--gc-sections` 에서 사라졌습니다. 왜죠?</summary>

`used` 는 컴파일러 수준 — "참조가 없어도 이 정의를 오브젝트에 내보내라" 다. `--gc-sections` 는 링커가 뿌리(ENTRY 심볼, `KEEP` 섹션, `-u` 심볼 등)에서 재배치 참조를 따라 도달 못 한 **입력 섹션**을 지우는 것이고, 하드웨어만 읽는 벡터 표는 도달 불가다. 실측: `removing unused section '.isr_vector'` 와 함께 벡터 표에서만 참조되던 `Default_Handler` 까지 사라졌다. 해결은 스크립트의 `KEEP(*(.isr_vector))`. 대안으로 `retain` 속성(SHF_GNU_RETAIN)이 있다고 알려져 있으나 툴체인 버전 의존 — 확인 권장. 디버깅 팁: `-Wl,--print-gc-sections` 를 CI 로그에 남기면 "무엇이 지워졌나" 가 남는다.
</details>

<details><summary>Q4. `.bss` 는 왜 Flash 를 안 먹나요? `NOLOAD` 는 언제 필요하죠?</summary>

`.bss` 의 입력 섹션은 컴파일러가 NOBITS(`@nobits`)로 낸다 — "크기만 있고 내용 없음". 출력 `.bss` 도 NOBITS 라 파일 오프셋만 있고 바이트가 없다. 실측: `.bin` 352 B 에 `.bss` 44 B 는 없고, 세그먼트 `MemSiz - FileSiz = 0x2c`. 스타트업이 0 으로 채우므로 "0 이다" 는 약속이 지켜진다. `NOLOAD` 는 입력이 PROGBITS 인데 파일·Flash 에 싣고 싶지 않을 때 필요하다: 실측 `.ccmram` 은 GCC 가 PROGBITS 로 냈고, NOLOAD 없이 링크하면 4 KB 0 이 이미지에 실리며 `.bin` 범위가 134 MB 가 됐다. `.bss` 에 NOLOAD 를 붙여도 결과는 같았다.
</details>

<details><summary>Q5. `objcopy -O binary` 결과가 400 MB 입니다. 어디부터 보나요?</summary>

`.bin` 은 가장 낮은 LMA 부터 가장 높은 LMA 끝까지의 평면 메모리 덤프라서, LMA 가 FLASH 밖에 있는 **내용 있는** 섹션이 하나라도 있으면 그 사이가 0 으로 채워진다. `readelf -l` 에서 FileSiz > 0 인 LOAD 의 PhysAddr 를 본다 — 실측 `LOAD … 0x20000000 0x20000000 0x00014` (AT> 빠진 `.data`). 범위 = `0x20000014 - 0x08000000 = 402,653,204 B`. 다른 후보: NOLOAD 없는 RAM/CCM 섹션, 고아 PROGBITS 섹션, 외부 QSPI 영역에 둔 섹션. 체커처럼 "이미지 범위를 먼저 계산하고 상한을 넘으면 실패" 를 CI 에 넣으면 좋다.
</details>

<details><summary>Q6. 링커 스크립트에서 정의한 `_sdata` 를 C 에서 어떻게 선언하고, 왜 그렇게 하죠?</summary>

`extern uint32_t _sdata[];` 처럼 배열(또는 `extern char _sdata[];`)로 선언한다. 링커 심볼은 저장 공간이 없는 "주소에 붙은 이름" 이라 C 에서 의미 있는 건 `&_sdata` 뿐이다. 배열 이름은 곧 주소라 `_sdata` 를 그대로 쓰면 되고, 스칼라로 선언해 놓고 `&` 를 빼먹는 실수(그 주소의 내용을 읽음)를 막는다. 타입은 복사 단위(워드)에 맞추면 루프가 간단하다 — 그래서 경계가 4-정렬이어야 한다(B4). 컴파일된 코드에선 리터럴 풀의 상수가 된다(실측 `.word 0x0800014c` 등).
</details>

<details><summary>Q7. 16 KB 부트로더 뒤에 앱을 둘 때 앱의 링커 스크립트와 코드에서 바꿀 것은?</summary>

스크립트: `FLASH ORIGIN = 0x08000000 + 16K, LENGTH = 512K - 16K` (실측 `--print-memory-usage` 의 Region Size 496 KB, `.isr_vector @ 0x08004000`). RAM 은 보통 그대로(부트로더와 앱은 동시에 안 돈다 — 단 둘이 공유하는 `.noinit` 이나 매직 워드 자리는 양쪽 스크립트에서 같은 주소로 고정). 코드: 앱 스타트업에서 `SCB->VTOR = 0x08004000` (Cortex-M3/M4 는 VTOR 이 있음; M0 는 없고 M0+ 는 선택 사항이라, 없는 칩은 벡터 표를 RAM 에 복사 + 메모리 remap — 부품 매뉴얼 확인). 부트로더는 앱 벡터 표 [0] 으로 MSP 를, [1] 로 점프 — 분할과 점프 절차는 BM-B06/BM-A03. 이미지는 `0x08004000` 부터의 `.bin` 이 되므로 굽는 주소도 맞춰야 한다.
</details>

<details><summary>Q8. 스크립트가 언급하지 않은 섹션은 어떻게 되나요? CI 에서 어떻게 막죠?</summary>

GNU ld 는 고아 섹션을 **에러 없이** 속성이 비슷한 출력 섹션 근처에 둔다. 실측: 규칙 없는 `.ccmram`(PROGBITS, 4 KB) 이 `.data` 바로 뒤 RAM 에, LMA 는 FLASH 로 붙어 FLASH 사용량이 352 → 4476 B. `--orphan-handling=warn` 은 그런 섹션을 하나하나 보고하고(`orphan section '.ccmram' … being placed in section '.ccmram'`), `error` 로 하면 링크를 실패시킨다 — 단 디버그 섹션·링커 생성 섹션도 고아로 잡히니 규칙을 먼저 정리해야 한다. 보완책: `--print-memory-usage` 출력 diff, 영역 경계 `ASSERT`(TOOL-A07), 맵 파일 분석(TOOL-M02).
</details>

<details><summary>Q9. GNU ld 대신 ld.lld 를 쓰면 같은 스크립트로 같은 이미지가 나오나요?</summary>

이 레슨 실측으로는 섹션 주소·심볼·크기는 같고 `.bin` 이 정렬 빈칸 2 바이트만 달랐다(bfd `00 00`, lld `d4 d4`) — 출력 섹션 fill `=0` 로 맞추면 같아졌다. 프로그램 헤더는 다르다: lld 는 권한별로 세그먼트를 더 잘게(LOAD 5개 + GNU_STACK) 나눴고, FLASH 앞에 빈 공간이 있으면 ELF 헤더를 담은 LOAD 를 `0x08000000` 에 만들었다 → `PHDRS` 로 해결. `--print-gc-sections` 출력 형식도 다르다. 결론: 링커를 바꾸면 이미지 해시와 세그먼트 표를 diff 해서 확인해야 한다.
</details>

## 9. 추가 연습

`practice.ld` 한 파일에 1~3 을 모두 담고, `fw.c` 를 `-DPRACTICE` 로 빌드한다 (`g_reset_reason`, `g_crash_pc` → `.noinit`, `g_dsp_buf[4096]` → `.ccmram`).

### 연습 1 — 16 KB 부트로더 자리 비우기

앱이 `0x08004000` 부터 살도록 MEMORY 만 고친다. 벡터 표 위치, `.bin` 크기, `--print-memory-usage` 의 FLASH 영역 크기를 예측하라.

<details><summary>답</summary>

`FLASH (rx) : ORIGIN = 0x08000000 + 16K, LENGTH = 512K - 16K` — MEMORY 에서 식을 쓸 수 있다. 실측: `.isr_vector @ 0x08004000`, `fw.bin = 380 바이트`(`.bin` 은 가장 낮은 LMA 인 0x08004000 부터라 부트로더 자리는 안 들어감 → 굽는 주소를 0x08004000 으로), `FLASH: 380 B / 496 KB`. 380 이 352 보다 큰 건 PRACTICE 코드가 `.text` 를 0xd0 → 0xec 로 키워서다. 섹션 규칙은 하나도 안 바뀐다 — `> FLASH` 가 영역 이름만 가리키기 때문. 코드 쪽 VTOR 설정은 Q7.
</details>

### 연습 2 — `.noinit`: 리셋을 넘어 살아남는 변수

크래시 PC 와 리셋 이유를 워치독 리셋 후에도 읽고 싶다. `.noinit` 출력 섹션을 어디에 두어야 스타트업이 지우지 않을까? `_snoinit/_enoinit` 도 만든다.

<details><summary>답</summary>

`.bss` **뒤**, `_sbss.._ebss` 범위 **밖**에 `(NOLOAD)` 로 둔다. 실측: `.noinit NOBITS @ 0x20000040` (= `_ebss`), 8 바이트, `g_crash_pc 0x20000040`, `g_reset_reason 0x20000044`, `RAM: 72 B` = `_enoinit - ORIGIN(RAM)`. GCC 16 은 `.noinit` 이름을 알아보고 NOBITS 로 냈지만(실측), 다른 컴파일러/옛 버전에선 PROGBITS 일 수 있으니 `NOLOAD` 로 보장한다. 콜드 부팅 직후엔 쓰레기이므로 매직 값 + 체크섬으로 유효성을 따져야 한다 — 그 프로토콜은 BM-A09. B4(끝 정렬) 버그가 있으면 이 섹션 앞 3 바이트가 매 부팅마다 지워질 수 있다는 점도 연결해 기억하자.
</details>

### 연습 3 — CCM RAM 영역과 NOLOAD 함정

STM32F4 의 CCM RAM(64 KB @ `0x10000000`, 코어 전용이라 DMA 불가 — 부품마다 확인)을 DSP 작업 버퍼에 쓴다. 영역과 섹션을 추가하라. `(NOLOAD)` 를 빼면 어떻게 될지도 예측하라.

<details><summary>답</summary>

`CCMRAM (rw) : ORIGIN = 0x10000000, LENGTH = 64K` + `.ccmram (NOLOAD) : { . = ALIGN(4); *(.ccmram .ccmram.*) . = ALIGN(4); } > CCMRAM`. 실측: `.ccmram NOBITS @ 0x10000000`, `CCMRAM: 4 KB / 64 KB`, `.bin` 은 그대로 380 B. NOLOAD 를 빼면 GCC 가 낸 PROGBITS 그대로 `LOAD 0x10000000 0x10000000 FileSiz 0x01000` 이 생기고 `.bin` 범위가 `0x08004000..0x10001000 = 134,205,440 B` (demo_mac.txt 16절). 이 버퍼는 스타트업이 0 으로 지우지 않는다 — 0 이 필요하면 `_sccm/_eccm` 심볼을 만들어 루프를 하나 더 돌린다. DMA 버퍼를 실수로 여기에 두면 DMA 가 못 본다.
</details>

### 연습 4 — 예측: 규칙 없이 링크하면?

연습 1~3 을 하기 전, `-DPRACTICE` 코드를 `solution.ld` 그대로 링크하면 `.noinit`, `.ccmram` 은 어디로 가고 FLASH 사용량은 얼마일까?

<details><summary>답</summary>

5. 풀이 Part C ② 의 실측: `.ccmram` 은 PROGBITS 라 `.data` 뒤(RAM `0x20000014`)에, LMA 는 FLASH 로 이어 붙어 FLASH 4476 B(= 380 + 4096). `.noinit` 은 NOBITS 라 `.bss` 뒤(`0x20001040`). 링크 경고는 없다 — `--orphan-handling=warn` 을 켜야 보인다.
</details>

### 연습 5 — (VM) 같은 practice.ld 를 ld.lld 로

`demo_vm_lld.sh` 8~9절을 돌리고, lld 가 만든 `0x08000000` LOAD 세그먼트가 무엇인지, 왜 위험한지, 어떻게 없애는지 설명하라.

<details><summary>답</summary>

`LOAD 0x000000 0x08000000 0x08000000 0x00154 … R` — 파일 offset 0 부터라 첫 4 바이트가 `\177 E L F`: ELF 헤더 + 프로그램 헤더 9개. lld 가 FLASH 시작 앞의 빈 주소 공간에 헤더를 매핑했다. `.bin`(섹션 기준, 384 B)엔 안 들어가지만 세그먼트로 굽는 도구는 부트로더 영역을 건드릴 수 있다. `PHDRS { text PT_LOAD; data PT_LOAD; bss PT_LOAD; ccm PT_LOAD; }` 를 선언하고 섹션에 `:text` 등을 붙이자 LOAD 4개만 남았다(실측). GNU ld 는 같은 스크립트에서 이 세그먼트를 만들지 않았다.
</details>

### 한 번에 검증

<!-- code: practice.ld -->

```bash
bash check.sh practice
```

실제 출력 (앞 17줄은 solution 과 같은 항목, 주소만 0x08004000 기준):

```text
PASS  .isr_vector @ 0x08004000, 0x40 바이트 (16 벡터) — gc 뒤에도 살아 있음
…
PASS  _sidata 0x08004168 == LOADADDR(.data)
…
PASS  fw.bin = 380 바이트 == LOADADDR(.data)+SIZEOF(.data)-ORIGIN(FLASH) (.bss 는 0 바이트)
PASS  _sidata 위치의 .bin 내용: g_boot_count=7, g_fw_tag="fw-1.4.2" (offset 0x16c, 0x170)
PASS  --print-memory-usage: FLASH 380 B (= .bin), RAM 72 B (= _enoinit - ORIGIN(RAM))
PASS  .noinit NOBITS @ 0x20000040 — _ebss 뒤라 0-채우기 범위 밖, 변수 2개 포함
PASS  .ccmram NOBITS @ 0x10000000 (g_dsp_buf 4 KB, 파일·FLASH 0 바이트)
PASS  --print-memory-usage: FLASH 영역 496 KB (부트로더 16 KB 제외), CCMRAM 4 KB / 64 KB
20/20 passed
```

## 10. 핵심 정리

- 링커 스크립트 = 입력 섹션을 출력 섹션에 모으고, 영역마다 따로 움직이는 위치 카운터로 VMA/LMA 를 정하는 **작은 프로그램**.
- `> RAM AT> FLASH`: VMA 는 RAM, LMA 는 FLASH. `_sidata = LOADADDR(.data)`, `_sdata/_edata` 는 섹션 안 `.` (VMA).
- LMA 는 `readelf -S` 에 없다 → `objdump -h` LMA 열, `readelf -l` PhysAddr, 맵 파일 `load address`.
- `.bin` = 최저 LMA ~ 최고 LMA 끝의 평면 덤프. AT> 나 NOLOAD 를 빼먹으면 수백 MB (실측 402,653,204 B / 134,205,440 B).
- `--gc-sections` 는 참조 그래프 탐색 → 벡터 표·등록 테이블은 `KEEP`. `used` 는 컴파일러용이라 소용없다 (실측).
- 워드 루프용 경계는 `. = ALIGN(4);` 뒤에 심볼. `.bss` 는 NOBITS 라 Flash 0, `.noinit` 은 `.bss` 뒤 + NOLOAD.
- CI: `--print-memory-usage`(예산), `--print-gc-sections`(무엇이 지워졌나), `--orphan-handling=warn`(규칙 없는 섹션), 이미지 범위 상한.
- ld.lld 도 같은 스크립트를 먹지만 세그먼트 표·채움 바이트가 다르다 → 링커를 바꾸면 이미지와 `readelf -l` 을 diff.

## 참고자료

- GNU ld 매뉴얼 — 3. Linker Scripts: `MEMORY`, `SECTIONS`, Output Section LMA (`AT>`), Output Section Type (`NOLOAD`), `KEEP`, Builtin Functions (`ADDR`, `LOADADDR`, `ALIGN`, `ORIGIN`, `LENGTH`), Orphan Sections, `PHDRS` — https://sourceware.org/binutils/docs/ld/Scripts.html
- GNU ld 옵션 — `--gc-sections`, `--print-gc-sections`, `--print-memory-usage`, `--orphan-handling` — https://sourceware.org/binutils/docs/ld/Options.html
- LLD — Linker Script implementation notes / ELF 링커 문서 — https://lld.llvm.org/ELF/linker_script.html
- ARM — Cortex-M4 Devices Generic User Guide: Vector table, VTOR (reset 동작)
- ST — STM32CubeIDE/CubeMX 가 생성하는 `STM32F4xx_FLASH.ld` (CCMRAM, `_user_heap_stack`, `.ARM.exidx` 규칙 예시)
- 이 레슨 실측: `demo_mac.txt`(arm-none-eabi-gcc 16.2.0, GNU ld 2.47), `demo_vm_lld.txt`(VM arm-none-eabi-gcc 14.2.1, GNU ld 2.45.50, Ubuntu LLD 21.1.8)
- 관련 레슨: TOOL-B06(size 의 text/data/bss), BM-B05(맵 파일 눈으로 읽기), BM-B08(스타트업·QEMU 부팅), BM-B06(부트로더+앱 분할), BM-A09(.noinit 크래시 덤프), TOOL-M02(맵 파일 분석기), TOOL-M03(커스텀 섹션 등록 테이블), TOOL-M13(크기 다이어트), TOOL-A07(ASSERT·RAM 함수·정렬 심화·PHDRS)
