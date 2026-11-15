---
id: BM-B08
time: 70분
updated: 2026-11-11
---
# BM-B08 C로 쓴 startup으로 첫 부팅: QEMU에서 main까지

## 학습 목표

- 리셋 직후 Cortex-M 코어가 **스스로** 하는 일(벡터 [0] → MSP, 벡터 [1] → PC + Thumb 비트)과 소프트웨어(Reset_Handler)가 하는 일(.data 복사, .bss 0, 생성자, main)을 주소까지 들어 순서대로 말할 수 있다.
- C 배열 벡터 테이블(`section(".isr_vector")`), `weak` + `alias("Default_Handler")` 기본 핸들러, 직접 쓴 링커 스크립트로 `qemu-system-arm -M mps2-an385` 를 **진짜로** 부팅하고, 세미호스팅(semihosting) `SYS_EXIT_EXTENDED` 로 main 의 반환값을 호스트 종료 코드로 받는다.
- gdb(`-s -S`)로 `x/2wx 0`, 복사 전 `.data` 의 RAM 값, `watch` 로 복사 순간을 직접 본다.
- Thumb 비트 누락, 잘못된 `_estack`, 핸들러 이름 오타, `KEEP` 누락, `memcpy` 자동 변환을 **실제로 일으켜 보고** 증상을 기억한다.
- 내가 쓴 startup 과 CMSIS `startup_*.s` / `system_*.c` (SystemInit, `__libc_init_array`) 의 차이를 설명할 수 있다.

## 1. 문제

보드 브링업(bring-up) 첫날의 질문은 늘 같다: **"main 에 브레이크포인트가 안 걸려요."** 면접에서도 "리셋부터 main 까지 무슨 일이 일어나나요?" 는 펌웨어 직무의 단골 첫 질문이다. Don 이 SSD 컨트롤러나 RF 모듈 보드를 처음 살릴 때 벤더 `startup_*.s` 를 그대로 썼다면, 이번 레슨은 그 파일을 **C 로 직접 다시 써서** 진짜 Cortex-M3 (QEMU) 에서 부팅해 보는 것이다. BM-B07 에서 호스트 배열로 연습한 `reset_init()` 을 이번엔 진짜 링커 심볼 위에서 돌린다.

이 레슨의 펌웨어 한 개(`solution.c` + `handlers.c` + `bm_b08.ld`)가 실제로 만든 메모리 배치다 (`run_qemu.sh map` 실측):

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">solution.elf 메모리 배치 (실측, 크기 비례 아님)</text>
  <text x="40" y="40" fill="currentColor" font-weight="bold">FLASH 0x00000000, 256 KB (QEMU 에선 SSRAM1)</text>
  <rect x="40" y="50" width="250" height="34" class="f-acc" stroke="currentColor"/>
  <text x="48" y="64" fill="currentColor">.isr_vector 192 B (48 워드)</text>
  <text x="48" y="78" fill="currentColor" font-family="monospace">0x00000000</text>
  <rect x="40" y="84" width="250" height="60" class="f-mut" stroke="currentColor"/>
  <text x="48" y="104" fill="currentColor">.text + .rodata 3020 B</text>
  <text x="48" y="120" fill="currentColor">코드, 문자열, g_table (const)</text>
  <text x="48" y="136" fill="currentColor" font-family="monospace">0x000000C0</text>
  <rect x="40" y="144" width="250" height="30" class="f-mut" stroke="currentColor"/>
  <text x="48" y="163" fill="currentColor">.init_array 8 B (생성자 포인터 2개)</text>
  <rect x="40" y="174" width="250" height="34" class="f-warn" stroke="currentColor"/>
  <text x="48" y="188" fill="currentColor">.data 초기값 28 B (LMA)</text>
  <text x="48" y="202" fill="currentColor" font-family="monospace">_sidata = 0x00000C94</text>
  <rect x="40" y="208" width="250" height="40" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="48" y="232" fill="currentColor">빈 칸 (0x00000CB0 ~ 0x0003FFFF)</text>
  <text x="440" y="40" fill="currentColor" font-weight="bold">RAM 0x20000000, 64 KB</text>
  <rect x="440" y="50" width="270" height="34" class="f-warn" stroke="currentColor"/>
  <text x="448" y="64" fill="currentColor">.data 28 B (VMA) g_magic, g_msg …</text>
  <text x="448" y="78" fill="currentColor" font-family="monospace">_sdata 0x20000000 ~ 0x2000001C</text>
  <rect x="440" y="84" width="270" height="14" class="f-mut" stroke="currentColor"/>
  <text x="448" y="95" fill="currentColor">패딩 4 B (.bss 가 8-정렬)</text>
  <rect x="440" y="98" width="270" height="34" class="f-ok" stroke="currentColor"/>
  <text x="448" y="112" fill="currentColor">.bss 76 B → 0 으로 채움</text>
  <text x="448" y="126" fill="currentColor" font-family="monospace">_sbss 0x20000020 ~ 0x2000006C</text>
  <rect x="440" y="132" width="270" height="56" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="448" y="164" fill="currentColor">빈 칸 (스택이 자라 내려올 자리)</text>
  <rect x="440" y="188" width="270" height="40" class="f-acc" stroke="currentColor"/>
  <text x="448" y="204" fill="currentColor">스택 ↓ (최소 2 KB, ld ASSERT)</text>
  <text x="448" y="220" fill="currentColor" font-family="monospace">_estack = 0x20010000 (꼭대기)</text>
  <path d="M290 191 C 360 191, 370 67, 436 67" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M436 67 l-9 -5 l0 10 z" fill="currentColor"/>
  <text x="300" y="150" fill="currentColor">reset_init:</text>
  <text x="300" y="166" fill="currentColor">7 워드 복사</text>
  <path d="M290 64 C 380 64, 360 228, 436 222" stroke="currentColor" fill="none" stroke-dasharray="5 3"/>
  <path d="M436 222 l-9 -4 l1 10 z" fill="currentColor"/>
  <text x="330" y="250" fill="currentColor">벡터 [0] = 0x20010000 → 리셋 때 MSP</text>
  <text x="40" y="282" fill="currentColor">.data 는 FLASH(초기값, LMA) 와 RAM(실행 주소, VMA) 를 둘 다 먹는다: readelf -l 의</text>
  <text x="40" y="300" fill="currentColor">LOAD VirtAddr 0x20000000 / PhysAddr 0x00000C94. .bss 는 RAM 만 (파일엔 0 바이트, 19 워드를 0 으로).</text>
  <text x="40" y="318" fill="currentColor">.rodata(const) 는 FLASH 에만 산다 — 복사도 RAM 도 안 든다. size: text 3212, data 36, bss 76.</text>
  <text x="40" y="336" fill="currentColor">(size 의 data 36 = .data 28 + .init_array 8. Berkeley 형식은 쓰기 가능한 섹션을 data 로 센다.)</text>
</svg>
```

### Part A — 벡터 테이블·링커 스크립트·reset_init 로 main 까지

- `bm_b08.ld` (주어짐): FLASH 256 KB @0x00000000, RAM 64 KB @0x20000000, `_estack = ORIGIN(RAM)+LENGTH(RAM)`, `.isr_vector` 를 맨 앞에 `KEEP`, `.data` 는 `> RAM AT > FLASH`, `_sidata/_sdata/_edata/_sbss/_ebss/__init_array_start/_end`, 공간 `ASSERT` 2개.
- `solution.c` 의 startup 부분 (주어짐): `const vector_t g_vectors[16+32]` 를 `section(".isr_vector")` 에 — `[0] = (vector_t)_estack`, `[1] = Reset_Handler`, 나머지는 `weak, alias("Default_Handler")` 로 선언한 이름들. `Reset_Handler` 는 `ram_poison(); reset_init(); sh_exit(main());`.
- **TODO**: `reset_init()` — BM-B07 의 세 단계 (.data 워드 복사 → .bss 0 → init_array 순서대로 호출) 를 진짜 링커 심볼로.
- main 의 자기 검사(self-test) 18개가 세미호스팅으로 결과를 찍고 `SYS_EXIT_EXTENDED` 로 끝난다 → `qemu exit=0`.

### Part B — 결함 주입: 시작 코드와 망가진 이미지

- 시작 코드(TODO 그대로)를 부팅하면: 부팅은 되지만 `.data`/`.bss`/생성자 검사 7개가 FAIL, `qemu exit=1`.
- `run_qemu.sh faults` 로 실제 관찰: (F1) Reset 벡터의 Thumb 비트 없음, (F2·F3·F4) 잘못된 `_estack` 세 가지, (F5) `-ffreestanding` 없이 `-nostdlib` 링크 → `memcpy`/`memset` 미정의.
- `run_qemu.sh nopoison`: RAM 을 더럽히지 않으면 QEMU 에선 `.bss` 검사가 **가짜로 통과**한다 — 왜인지 설명.

### Part C — gdb 로 첫 부팅 따라가기 + CMSIS startup 과 비교

- `run_qemu.sh gdb`: `-s -S` 로 멈춘 상태에서 `x/2wx 0`, `$sp`/`$pc`/`$xpsr`, 복사 전 `g_magic` 의 RAM 값과 FLASH 사본, `watch g_magic` 으로 복사 순간, main 에서 `.data` 초기값·`.bss` 0·생성자 순서 확인.
- 핸들러 덮어쓰기: `handlers.c` 의 강한(strong) `SysTick_Handler`/`HardFault_Handler` 가 weak 를 이기는지, 주인 없는 IRQ7 이 `Default_Handler` 로 가서 IPSR=23 이 찍히는지 main 이 검사한다.
- CMSIS `startup_stm32f4xx.s` + `system_stm32f4xx.c` 와 무엇이 같고 다른지 표로 정리한다. (어셈블리 벡터 테이블 자체는 ARM-M08 에서.)

## 2. Problem (English)

> **Bring up a Cortex-M3 from reset to `main` without any vendor startup file.**
>
> Target: QEMU `mps2-an385` (Cortex-M3). Toolchain: `arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -ffreestanding -nostdlib`. You are given a linker script that exports `_sidata, _sdata, _edata, _sbss, _ebss, _estack, __init_array_start, __init_array_end`.
>
> 1. Write the vector table in C: `const vector_t g_vectors[48]` in section `.isr_vector`. Entry 0 is the initial MSP (`_estack`), entry 1 is `Reset_Handler`, entries 7–10 and 13 are reserved (0). Every other handler name must be declared `__attribute__((weak, alias("Default_Handler")))` so the application can override it just by defining a function with the same name.
> 2. Implement `void reset_init(void)`: copy `.data` from its load address to RAM, zero `.bss`, then call every function pointer in `[__init_array_start, __init_array_end)` in order. It must not touch any `.data`/`.bss` object itself.
> 3. `Reset_Handler` calls `reset_init()` and reports `main()`'s return value to the host via semihosting `SYS_EXIT_EXTENDED` (0x20), so `qemu-system-arm` exits with that code.
>
> Example: with `uint32_t g_magic = 0xC0FFEE42;` and `uint32_t g_zero[8];`, `main` must see `g_magic == 0xC0FFEE42` and all of `g_zero` equal to 0 **even if RAM held 0xA5A5A5A5 at reset**; a constructor with priority 101 that reads `g_magic` must run before the default-priority one and must see `0xC0FFEE42`. The vector table must be at address 0 (`SCB->VTOR == 0`), `vectors[1]` must have bit 0 set.
>
> Follow-ups: What does the core do before the first instruction of `Reset_Handler`? Why does `.data` cost both Flash and RAM? What happens if bit 0 of the reset vector is 0? What if `_estack` points outside RAM? Why might your copy loop link-fail with `undefined reference to memcpy`? Why do vendor startups set SP again in `Reset_Handler`, and when does `SystemInit` run relative to the `.data` copy?

## 3. 시작 코드

<!-- code: starter.c -->

같이 쓰는 파일: `bm_b08.ld` (링커 스크립트), `handlers.c` (강한 핸들러), `demo_boot.gdb` (gdb 스크립트), `run_qemu.sh` (VM 빌드·실행), `demo_addr0.c` (7. 실수 4).

<!-- code: bm_b08.ld -->

<!-- code: handlers.c -->

실행은 Lima VM `elinux` 안에서 한다 (Mac 홈은 VM 에서 읽기 전용이라 스크립트가 `/tmp/BM-B08_work` 로 복사해 빌드; QEMU 는 `SYS_EXIT` 로 스스로 끝나고 `timeout 10` 이 안전망). Mac 에서 한 줄씩:

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/BM/BM-B08
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh starter"    # 시작 코드: FAIL 7개, qemu exit=1
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh solution"   # 정답: 18/18, qemu exit=0
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh gdb"        # -s -S + gdb-multiarch -batch -x demo_boot.gdb
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh faults"     # F1~F5 결함 주입
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh map"        # size -A, readelf -l, 벡터 덤프, 심볼
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh nopoison"   # RAM 을 안 더럽히면? (가짜 PASS)
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"   # 실시간 덤프 + practice.c
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh addr0"      # 주소 0 비교가 접히는 asm (7. 실수 4)
```

시작 코드 실행 결과 (실제 출력):

```text
   text	   data	    bss	    dec	    hex	filename
   3088	     36	     76	   3200	    c80	starter.elf
BM-B08 boot: main reached
  _sidata=0x00000c18 _sdata=0x20000000 _edata=0x2000001c
  _sbss=0x20000020 _ebss=0x2000006c _estack=0x20010000 sp=0x2000ffe0
  g_magic=0xa5a5a5a5 g_zero[0]=0xa5a5a5a5 ctor_n=2779096485
FAIL starter.c:198  g_magic == 0xC0FFEE42u
FAIL starter.c:199  g_neg == -1234
FAIL starter.c:200  str_eq(g_msg, "hello, .data")
FAIL starter.c:201  g_odd[0] == 0x11u && g_odd[1] == 0x22u && g_odd[2] == 0x33u
FAIL starter.c:204  nz == 0u && s_counter == 0u && g_explicit_zero == 0u
FAIL starter.c:209  g_ctor_n == 2u && g_ctor_log[0] == 1u && g_ctor_log[1] == 2u
FAIL starter.c:210  g_ctor_saw_magic == 0xC0FFEE42u
11/18 passed
qemu exit=1
```

부팅 자체(벡터 테이블, 스택, 예외 연결)와 주소 배치는 이미 맞아서 나머지 검사 11개는 통과한다. `ctor_n=2779096485` = 0xA5A5A5A5 — 0 으로 지워지지 않은 `.bss` 카운터다. 하네스의 `pass`/`total` 을 **지역 변수**로 둔 이유가 이것이다 (전역이면 셈 자체가 쓰레기).

## 4. 힌트

<details>
<summary>힌트 1 — 링커 심볼은 "주소" 만 의미가 있다</summary>

`extern uint32_t _sdata[];` 로 선언하면 `_sdata` 가 곧 `uint32_t *` 주소다. 값을 읽는 게 아니다 (`_sdata[0]` 은 그 주소의 내용 = g_magic). 길이는 `_edata - _sdata` (워드 단위). 링커 스크립트가 `ALIGN(4)` 로 경계를 맞춰 두었으니 워드 루프면 충분하다 (꼬리 바이트 처리 = BM-B07).

</details>

<details>
<summary>힌트 2 — 순서: 복사 → 0 → 생성자</summary>

생성자 `ctor_first` 는 `g_magic` (.data) 을 읽고 `g_ctor_n` (.bss) 을 올린다. `.data`/`.bss` 가 준비되기 전에 생성자를 부르면 두 검사가 다 깨진다. init_array 는 `[__init_array_start, __init_array_end)` 의 함수 포인터 배열 — 앞에서부터 하나씩 호출.

</details>

<details>
<summary>힌트 3 — 루프가 memcpy 로 바뀌지 않게</summary>

`-nostdlib` 이면 `memcpy`/`memset` 이 없다. GCC 는 "복사 루프 = memcpy" 로 바꿀 수 있으므로 `-ffreestanding -fno-tree-loop-distribute-patterns` 를 켜 둔 상태다 (run_qemu.sh 의 `CFLAGS`). 루프는 평범하게 `for (dst = _sdata; dst < _edata;) *dst++ = *src++;` 로.

</details>

## 5. 풀이

### Part A — 리셋에서 main 까지, 주소로 따라가기

**코어가 하드웨어로 하는 일** (ARMv7-M ARM B1.5.5 "Reset behavior" — 요약, 원문 확인 권장):

1. `VTOR` 를 0 (구현이 정한 리셋 값; Cortex-M3 는 0) 으로, 주소 `VTOR+0` 의 워드를 `MSP` 로, `VTOR+4` 의 워드를 읽어 bit0 을 `EPSR.T` 로, 나머지(`& ~1`)를 `PC` 로.
2. Thread 모드, 특권(privileged), MSP 사용, PRIMASK=0 (인터럽트 허용), 모든 IRQ 는 NVIC 에서 꺼진 상태, UsageFault/BusFault/MemManage 는 비활성(→ HardFault 로 승격).
3. 그다음부터가 소프트웨어 = `Reset_Handler`.

Cortex-M 은 **ARM 상태가 없다** (Thumb 만). 그래서 벡터에 들어가는 모든 코드 주소는 bit0=1 이어야 하고, C 로 쓰면 링커가 Thumb 함수 심볼(`STT_FUNC`)에 대해 알아서 `| 1` 을 해 준다. 실측 벡터 덤프:

```text
--- vector table (objdump -s -j .isr_vector, first 64 bytes) ---
 0000 00000120 5d020000 a1010000 99070000  ... ]...........
 0010 a1010000 a1010000 a1010000 00000000  ................
 0020 00000000 00000000 00000000 a1010000  ................
 0030 a1010000 00000000 a1010000 1d070000  ................
```

little-endian 이므로 `00000120` = **0x20010000** (= `_estack`), `5d020000` = **0x0000025D** (= `Reset_Handler` 0x25C | 1), `a1010000` = 0x1A1 (= `Default_Handler` 0x1A0 | 1), `99070000` = 0x799 (`HardFault_Handler`, handlers.c), `1d070000` = 0x71D (`SysTick_Handler`, handlers.c). 칸 7–10, 13 은 0 (예약).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">부팅 순서 (solution.elf 실제 주소)</text>
  <rect x="10" y="32" width="170" height="64" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="18" y="50" fill="currentColor" font-weight="bold">① 코어 (하드웨어)</text>
  <text x="18" y="68" fill="currentColor" font-family="monospace">MSP ← [0x0] 0x20010000</text>
  <text x="18" y="86" fill="currentColor" font-family="monospace">PC ← [0x4] 0x25D &amp; ~1</text>
  <rect x="200" y="32" width="160" height="64" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="208" y="50" fill="currentColor" font-weight="bold">② Reset_Handler</text>
  <text x="208" y="68" fill="currentColor" font-family="monospace">@0x25C, T=1</text>
  <text x="208" y="86" fill="currentColor">xPSR 0x41000000</text>
  <rect x="380" y="32" width="160" height="64" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="388" y="50" fill="currentColor" font-weight="bold">③ ram_poison</text>
  <text x="388" y="68" fill="currentColor">(하네스) RAM 을</text>
  <text x="388" y="86" fill="currentColor" font-family="monospace">0xA5A5A5A5 로</text>
  <rect x="560" y="32" width="170" height="64" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="568" y="50" fill="currentColor" font-weight="bold">④ reset_init @0x1E4</text>
  <text x="568" y="68" fill="currentColor">.data 7 워드 복사</text>
  <text x="568" y="86" fill="currentColor">.bss 19 워드 0</text>
  <path d="M180 64 L196 64" stroke="currentColor"/><path d="M196 64 l-7 -4 l0 8 z" fill="currentColor"/>
  <path d="M360 64 L376 64" stroke="currentColor"/><path d="M376 64 l-7 -4 l0 8 z" fill="currentColor"/>
  <path d="M540 64 L556 64" stroke="currentColor"/><path d="M556 64 l-7 -4 l0 8 z" fill="currentColor"/>
  <path d="M645 96 L645 126" stroke="currentColor"/><path d="M645 126 l-4 -7 l8 0 z" fill="currentColor"/>
  <rect x="560" y="126" width="170" height="64" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="568" y="144" fill="currentColor" font-weight="bold">⑤ init_array</text>
  <text x="568" y="162" fill="currentColor" font-family="monospace">0xC8C: ctor_first(101)</text>
  <text x="568" y="180" fill="currentColor" font-family="monospace">0xC90: ctor_second</text>
  <path d="M560 158 L544 158" stroke="currentColor"/><path d="M544 158 l7 -4 l0 8 z" fill="currentColor"/>
  <rect x="380" y="126" width="160" height="64" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="388" y="144" fill="currentColor" font-weight="bold">⑥ main @0x2B0</text>
  <text x="388" y="162" fill="currentColor">자기 검사 18개</text>
  <text x="388" y="180" fill="currentColor">return 0 / 1</text>
  <path d="M380 158 L364 158" stroke="currentColor"/><path d="M364 158 l7 -4 l0 8 z" fill="currentColor"/>
  <rect x="190" y="126" width="170" height="64" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="198" y="144" fill="currentColor" font-weight="bold">⑦ sh_exit(main())</text>
  <text x="198" y="162" fill="currentColor" font-family="monospace">BKPT 0xAB, r0=0x20</text>
  <text x="198" y="180" fill="currentColor">SYS_EXIT_EXTENDED</text>
  <path d="M190 158 L174 158" stroke="currentColor"/><path d="M174 158 l7 -4 l0 8 z" fill="currentColor"/>
  <rect x="10" y="126" width="160" height="64" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="18" y="144" fill="currentColor" font-weight="bold">⑧ 호스트</text>
  <text x="18" y="162" fill="currentColor">QEMU 종료</text>
  <text x="18" y="180" fill="currentColor" font-family="monospace">qemu exit=0</text>
  <text x="10" y="222" fill="currentColor">①은 명령어가 아니다 — 리셋 로직이 메모리를 두 번 읽는 것. 그래서 Reset_Handler 첫 줄부터 스택(push)을 쓸 수 있다.</text>
  <text x="10" y="242" fill="currentColor">③은 이 레슨의 하네스: 진짜 SRAM 은 전원 투입 때 쓰레기지만 QEMU RAM 은 0 이라서 일부러 더럽힌다.</text>
  <text x="10" y="262" fill="currentColor">④가 끝나기 전엔 전역을 쓰면 안 된다: .data 도 .bss 도 아직 0xA5…. ⑤의 ctor_first 는 g_magic 를 읽는다.</text>
  <text x="10" y="282" fill="currentColor">CMSIS/ST startup 은 ②와 ④ 사이(또는 ④ 뒤)에 SystemInit 을, ⑤ 자리에 __libc_init_array 를 부른다.</text>
  <text x="10" y="302" fill="currentColor">⑦ 진짜 보드에선 main 이 돌아오지 않는다 (무한 루프/RTOS). 돌아오면 ST startup 은 bx lr → 예측 불가.</text>
</svg>
```

**reset_init 단계별 추적** (실측 심볼 값):

| 단계 | 범위 | 크기 | 하는 일 | 끝난 뒤 (gdb/출력 실측) |
|---|---|---|---|---|
| 리셋 직후 | — | — | QEMU 로더가 ELF 를 **PhysAddr(LMA)** 에 올림 | `g_magic(RAM)=0x00000000`, FLASH 사본 `0x00000C94 = 0xc0ffee42` |
| ram_poison | `[0x20000000, 0x2000006C)` | 27 워드 | 하네스: 0xA5A5A5A5 로 채움 | `g_magic=0xa5a5a5a5 g_zero[0]=0xa5a5a5a5` |
| ① .data 복사 | `0x00000C94 → [0x20000000, 0x2000001C)` | 0x1C = 7 워드 | `*dst++ = *src++` | watch: `Old 0xa5a5a5a5 → New 0xc0ffee42` at `0x208` |
| ② .bss 0 | `[0x20000020, 0x2000006C)` | 0x4C = 19 워드 | `*dst++ = 0` | `g_zero = {0x0 × 8}` |
| ③ init_array | `[0x00000C8C, 0x00000C94)` | 2 포인터 | `0x271` → `0x295` 호출 | `g_ctor_n=2 log={1,2} ctor_saw_magic=0xc0ffee42` |

`.data` 입력 섹션은 실제로 0x1B = 27 바이트 (g_magic 4 + g_neg 2 + 패딩 2 + g_msg 13 + g_odd 3 … map 의 `.data 0x20000000 0x1b`) 이고, 링커 스크립트의 `. = ALIGN(4)` 가 `_edata` 를 0x2000001C 로 올려 워드 루프가 마지막 바이트까지 복사한다. `.init_array` 순서는 map 에서 확인된다:

```text
.init_array     0x00000c8c        0x8
                0x00000c8c                        __init_array_start = .
 *(SORT_BY_NAME(.init_array.*))
 .init_array.00101
                0x00000c8c        0x4 /tmp/ccfgRsje.o
 *(.init_array)
 .init_array    0x00000c90        0x4 /tmp/ccfgRsje.o
                0x00000c94                        __init_array_end = .
                0x00000c94                        _sidata = LOADADDR (.data)
```

`constructor(101)` 은 `.init_array.00101` 이라는 이름의 입력 섹션이 되고, `SORT(.init_array.*)` 가 숫자 순으로 앞에 놓는다. 우선순위 없는 생성자는 맨 뒤 `.init_array`.

정답 실행 (실제 출력):

```text
   text	   data	    bss	    dec	    hex	filename
   3212	     36	     76	   3324	    cfc	solution.elf
BM-B08 boot: main reached
  _sidata=0x00000c94 _sdata=0x20000000 _edata=0x2000001c
  _sbss=0x20000020 _ebss=0x2000006c _estack=0x20010000 sp=0x2000ffe0
  g_magic=0xc0ffee42 g_zero[0]=0x00000000 ctor_n=2
18/18 passed
qemu exit=0
```

**종료 코드 경로**: `sh_exit(code)` 는 `r0 = 0x20 (SYS_EXIT_EXTENDED)`, `r1 = {0x20026 (ADP_Stopped_ApplicationExit), code}` 블록 주소로 `BKPT 0xAB` 를 실행한다. QEMU 는 `-semihosting-config enable=on,target=native` 일 때 이것을 호스트 `exit(code)` 로 바꾼다 — 그래서 `starter` 는 1, `solution` 은 0, HardFault 경로는 3, Default_Handler 의 시스템 예외 경로는 2 가 실제로 나왔다. 32비트 ARM 의 옛 `SYS_EXIT` (0x18) 는 이유 코드만 받으므로 ApplicationExit 이면 0, 나머지는 1 만 줄 수 있다 (그래서 대비용으로 두 번째 호출로 남겨 둠).

### Part B — 결함 주입: 실제로 무슨 일이 일어났나

`run_qemu.sh faults` 의 실제 출력 (요약 없이):

```text
##### F1: Reset 벡터 Thumb 비트 없음 (-DFAULT_THUMB) #####
 0000 00000120 70020000 a1010000 9d070000  ... p...........
HardFault! CFSR=0x00020000 HFSR=0x40000000
  stacked pc=0x00000270 xpsr=0x40000000 lr=0xffffffff
qemu exit=3
##### F2: _estack = ORIGIN(RAM) (LENGTH 더하기를 잊음) #####
BM-B08: .data + .bss + stack do not fit in RAM
##### F3: _estack = 0x30000000 (아무것도 없는 주소) #####
qemu: fatal: Lockup: can't escalate 3 to HardFault (current priority -1)

R00=2fffffe0 R01=00000000 R02=00000000 R03=00000000
R04=2fffffe0 R05=e000e000 R06=00000000 R07=00000000
R08=00000000 R09=00000000 R10=00000000 R11=00000000
R12=00000000 R13=2fffffe0 R14=fffffff9 R15=00000732
XPSR=41000003 -Z-- T handler
FPSCR: 00000000
qemu exit=134
##### F4: RAM LENGTH = 8M (보드는 4 MB) → _estack 0x20800000 #####
BM-B08 boot: main reached
  _sidata=0x00000c94 _sdata=0x20000000 _edata=0x2000001c
  _sbss=0x20000020 _ebss=0x2000006c _estack=0x20800000 sp=0x207fffe0
  g_magic=0xc0ffee42 g_zero[0]=0x00000000 ctor_n=2
18/18 passed
qemu exit=0
##### F5: -ffreestanding · -fno-tree-loop-distribute-patterns 없이 -nostdlib 링크 #####
solution.c:128:(.text+0x134): undefined reference to `memcpy'
solution.c:129:(.text+0x14e): undefined reference to `memset'
solution.c:136:(.text+0x16): undefined reference to `memset'
```

| 결함 | 어떻게 만들었나 | 관찰 | 해석 |
|---|---|---|---|
| F1 Thumb 비트 | `.type %function` 없는 asm 라벨 `Reset_NoThumb` 를 벡터 [1] 에 (ARM-M08 의 실수). 링커는 `NOTYPE` 심볼엔 bit0 을 안 붙인다 → `0x00000270` | HardFault, `CFSR=0x00020000` = UFSR.**INVSTATE** (bit17), `HFSR=0x40000000` = **FORCED**, 쌓인 xPSR 의 T(bit24)=0, 쌓인 pc = 0x270 | T=0 으로 첫 명령을 실행하려는 순간 UsageFault. 리셋 직후 UsageFault 는 꺼져 있어 HardFault 로 승격. HardFault 벡터는 정상이고 스택도 정상이라 핸들러가 보고할 수 있었다 |
| F2 `_estack = ORIGIN(RAM)` | 링커 스크립트 한 줄 | **링크 실패**: `ld: BM-B08: .data + .bss + stack do not fit in RAM` | `ASSERT` 가 일하는 장면. ASSERT 가 없었다면 스택이 0x20000000 아래(QEMU 에선 RAZ/WI 예약 영역)로 자란다 |
| F3 `_estack = 0x30000000` | 아무 것도 매핑 안 된 주소 | `qemu: fatal: Lockup: can't escalate 3 to HardFault (current priority -1)`, exit 134 (QEMU abort) | Reset_Handler 의 첫 `push` → BusFault → HardFault 로 승격. 핸들러 진입 시 스태킹도 실패했지만 진입은 했고, `hardfault_report` 의 `push {r3, lr}` (0x732) 가 또 fault → HardFault 안의 HardFault = **Lockup**. R13 = 0x2FFFFFE0 (프레임 8워드만큼 내려간 SP) |
| F4 RAM 8 MB | `LENGTH = 64K` → `8M` | **그냥 통과** (`sp=0x207fffe0`) | QEMU `info mtree` 실측: 0x20400000–0x207FFFFF 는 SSRAM2&3 의 **미러(alias)**. 시뮬레이터가 버그를 숨긴다. 진짜 STM32 라면 SRAM 끝 너머 = BusFault → F3 처럼 Lockup |
| F5 memcpy | `-ffreestanding` 와 `-fno-tree-loop-distribute-patterns` 를 빼고 `-nostdlib` | 링크 실패 3건 (복사 루프 → memcpy, 0 루프 2개 → memset — ram_poison 루프 포함) | GCC 가 루프를 라이브러리 호출로 "최적화". 이 GCC 14.2.1 에선 `-ffreestanding` 만으로도 변환이 꺼졌다 (실측, `/tmp/BM-B08_work/t` 에서 확인) — 그래도 GCC 문서는 freestanding 에서도 memcpy/memset/memmove/memcmp 를 요구할 수 있다고 하니 두 플래그를 다 켜 두는 게 안전 |

**가짜 PASS** — `run_qemu.sh nopoison` (시작 코드를 ram_poison 없이):

```text
  g_magic=0x00000000 g_zero[0]=0x00000000 ctor_n=0
FAIL starter.c:198  g_magic == 0xC0FFEE42u
...
FAIL starter.c:210  g_ctor_saw_magic == 0xC0FFEE42u
12/18 passed
```

`.bss` 검사(204행)가 사라졌다. QEMU 의 RAM 은 0 으로 시작한다 — **.bss 제로화를 빼먹어도 QEMU 에선 티가 안 난다.** 진짜 보드는 전원 투입 직후 SRAM 이 쓰레기이고, 워치독 리셋이나 부트로더 점프(BM-A03) 뒤에는 이전 실행의 값이 그대로 남아 있다. `.data` 는 여전히 FAIL — QEMU 가 `.data` 를 VMA 가 아니라 LMA(0x00000C94) 에 올렸기 때문이다 (gdb (2) 에서 `g_magic(RAM)=0x00000000` 로 확인).

### Part C — gdb 로 첫 부팅 보기, 그리고 weak alias

`run_qemu.sh gdb` 실제 출력 (`demo_boot.gdb`):

```text
Reset_Handler () at solution.c:142
142	    ram_poison();

=== (1) -S 로 멈춘 리셋 직후: CPU 가 벡터 [0] [1] 에서 읽은 값 ===
0x0 <g_vectors>:	0x20010000	0x0000025d
sp=0x20010000  pc=0x0000025c  xpsr=0x41000000 (bit24 = T)
Reset_Handler in section .text

=== (2) 복사 전: .data 의 RAM 주소(VMA) 와 FLASH 사본(LMA) ===
&g_magic=0x20000000  g_magic(RAM)=0x00000000
LMA copy at 0x00000c94 = 0xc0ffee42
g_zero[0]=0x00000000  g_ctor_n=0

=== (3) reset_init 진입 (ram_poison 이 RAM 을 0xA5 로 더럽힌 뒤) ===
Breakpoint 1 at 0x1e4: file solution.c, line 128.

Breakpoint 1, reset_init () at solution.c:128
128	    for (uint32_t *dst = _sdata; dst < _edata;) *dst++ = *src++;
g_magic=0xa5a5a5a5  g_zero[0]=0xa5a5a5a5

=== (4) watch g_magic: 복사 루프가 쓰는 순간 ===
Hardware watchpoint 2: g_magic

Hardware watchpoint 2: g_magic

Old value = 0xa5a5a5a5
New value = 0xc0ffee42
0x00000208 in reset_init () at solution.c:128
128	    for (uint32_t *dst = _sdata; dst < _edata;) *dst++ = *src++;
#0  0x00000208 in reset_init () at solution.c:128
#1  0x00000266 in Reset_Handler () at solution.c:143

=== (5) main 도착: .data 초기값, .bss 0, 생성자 ===
Breakpoint 3 at 0x2b0: file solution.c, line 21.

Breakpoint 3, main () at solution.c:185
185	    sh_puts("BM-B08 boot: main reached\n");
g_magic=0xc0ffee42 g_neg=-1234 g_odd={0x11,0x22,0x33}
$1 = "hello, .data"
$2 = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0}
g_ctor_n=2 log={1,2} ctor_saw_magic=0xc0ffee42
sp=0x2000fff8 (_estack=0x20010000)
[Inferior 1 (process 1) exited normally]
qemu exit=0
```

읽는 법: (1) `-S` 로 멈춘 시점에 이미 `sp`/`pc` 가 벡터 두 칸과 같다 — 코어가 리셋 때 읽은 것. `xpsr=0x41000000` 의 bit24 가 T. (2) QEMU 로더는 `.data` 를 LMA 에만 썼다 → RAM 은 0. (4) watch 는 복사 루프의 `str` 바로 다음(0x208)에서 멈췄고, 콜 스택이 `Reset_Handler → reset_init`. (5) main 진입 시 `sp=0x2000fff8` = `_estack - 8` (Reset_Handler 의 `push {r3, lr}`).

**weak alias 동작**: `solution.c` 는 `void SysTick_Handler(void) __attribute__((weak, alias("Default_Handler")));` 로 이름만 만든다. `nm` 실측:

```text
000001a0 W BusFault_Handler
000001a0 W DebugMon_Handler
000001a0 T Default_Handler
000001a0 W IRQ0_Handler
000001a0 W IRQ7_Handler
000001a0 W MemManage_Handler
000001a0 W NMI_Handler
000001a0 W PendSV_Handler
000001a0 W SVC_Handler
000001a0 W UsageFault_Handler
0000071c T SysTick_Handler
00000798 T HardFault_Handler
```

`W` (weak) 이름들은 모두 0x1A0 = Default_Handler 와 같은 주소다. `SysTick_Handler` 와 `HardFault_Handler` 는 handlers.c 의 강한 정의(`T`)가 이겨서 다른 주소. main 의 검사가 이를 런타임으로 확인한다: SysTick 3 틱이 handlers.c 의 `g_ticks++` 로 들어오고, 핸들러 없는 IRQ7 을 `NVIC_ISPR0` 로 펜딩하면 `Default_Handler` 가 IPSR = 16 + 7 = **23** 을 기록한다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">벡터 테이블 48칸 → 핸들러 (실측 nm 주소)</text>
  <rect x="20" y="34" width="210" height="22" class="f-acc" stroke="currentColor"/>
  <text x="28" y="50" fill="currentColor" font-family="monospace">[0]  0x20010000  초기 MSP</text>
  <rect x="20" y="56" width="210" height="22" class="f-acc" stroke="currentColor"/>
  <text x="28" y="72" fill="currentColor" font-family="monospace">[1]  0x0000025D  Reset</text>
  <rect x="20" y="78" width="210" height="22" class="f-mut" stroke="currentColor"/>
  <text x="28" y="94" fill="currentColor" font-family="monospace">[3]  0x00000799  HardFault</text>
  <rect x="20" y="100" width="210" height="22" class="f-mut" stroke="currentColor"/>
  <text x="28" y="116" fill="currentColor" font-family="monospace">[15] 0x0000071D  SysTick</text>
  <rect x="20" y="122" width="210" height="44" class="f-warn" stroke="currentColor"/>
  <text x="28" y="138" fill="currentColor" font-family="monospace">[2][4-6][11][12][14]</text>
  <text x="28" y="156" fill="currentColor" font-family="monospace">IRQ0..31 = 0x000001A1</text>
  <rect x="20" y="166" width="210" height="22" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="28" y="182" fill="currentColor" font-family="monospace">[7-10][13] = 0 (예약)</text>
  <rect x="470" y="40" width="250" height="34" class="f-ok" stroke="currentColor"/>
  <text x="478" y="61" fill="currentColor">Reset_Handler @0x25C (solution.c)</text>
  <rect x="470" y="84" width="250" height="44" class="f-ok" stroke="currentColor"/>
  <text x="478" y="102" fill="currentColor">HardFault_Handler @0x798</text>
  <text x="478" y="120" fill="currentColor">SysTick_Handler @0x71C (둘 다 handlers.c)</text>
  <rect x="470" y="140" width="250" height="44" class="f-warn" stroke="currentColor"/>
  <text x="478" y="158" fill="currentColor">Default_Handler @0x1A0 (T)</text>
  <text x="478" y="176" fill="currentColor">← 이름 10개가 W (weak alias)</text>
  <path d="M230 67 L466 57" stroke="currentColor"/><path d="M466 57 l-8 -3 l1 8 z" fill="currentColor"/>
  <path d="M230 89 L466 100" stroke="currentColor"/><path d="M466 100 l-8 -4 l0 8 z" fill="currentColor"/>
  <path d="M230 111 L466 116" stroke="currentColor"/><path d="M466 116 l-8 -4 l0 8 z" fill="currentColor"/>
  <path d="M230 144 L466 160" stroke="currentColor" stroke-width="2"/><path d="M466 160 l-8 -4 l-1 8 z" fill="currentColor"/>
  <text x="250" y="186" fill="currentColor">39칸 (시스템 7 + IRQ 32)</text>
  <text x="20" y="214" fill="currentColor">모든 코드 주소는 홀수(bit0 = Thumb). 데이터인 [0] 만 짝수(8의 배수).</text>
  <text x="20" y="234" fill="currentColor">weak 규칙: 같은 이름의 강한(T) 정의가 링크에 들어오면 그것이 이기고, 없으면 alias 대상(Default_Handler)이 남는다.</text>
  <text x="20" y="254" fill="currentColor">이름 오타(Systick_Handler)는 에러가 아니다 — 새 함수가 하나 생기고 벡터는 여전히 Default_Handler.</text>
  <text x="20" y="274" fill="currentColor">IRQ7 펜딩 → Default_Handler 가 IPSR=23 을 읽음 → 벡터 오프셋 23×4 = 0x5C 의 주인이 없다는 뜻.</text>
</svg>
```

**CMSIS / ST startup 과의 비교** (STM32CubeF4 GCC `startup_stm32f407xx.s`, `system_stm32f4xx.c`, CMSIS 5 `startup_ARMCM3.c` 기준 — 버전에 따라 다르니 확인 권장):

| 항목 | 이 레슨 (C) | ST `startup_stm32f4xx.s` (GCC) | CMSIS 5 C 템플릿 `startup_ARMCM3.c` |
|---|---|---|---|
| 벡터 테이블 | `const vector_t g_vectors[48]` in `.isr_vector` | `g_pfnVectors: .word _estack, .word Reset_Handler, …` in `.isr_vector` | `const VECTOR_TABLE_Type __VECTOR_TABLE[]` in `.vectors` (`-Wpedantic` 끄는 pragma 동반) |
| 기본 핸들러 | `weak, alias("Default_Handler")` | `.weak NMI_Handler` + `.thumb_set NMI_Handler,Default_Handler` | `__attribute__((weak, alias("Default_Handler")))` |
| Default_Handler | IPSR 기록, IRQ 면 끄고 복귀 (검사용) | `Infinite_Loop: b Infinite_Loop` | `while(1);` |
| SP 다시 설정 | 안 함 (코어가 [0] 로딩) | `ldr sp, =_estack` (디버거·부트로더가 Reset_Handler 로 바로 점프하는 경우 대비) | M3 템플릿은 안 함, v8-M 템플릿은 `__set_MSPLIM` 으로 스택 한계 설정 |
| SystemInit | 없음 (QEMU 는 클럭 설정 불필요) | `bl SystemInit` — 최신 Cube 는 **.data 복사 전**에, 예전 버전은 .bss 뒤에 호출 | `SystemInit()` 을 `__PROGRAM_START` 전에 |
| .data / .bss | 워드 루프 2개 | `LoopCopyDataInit` / `LoopFillZerobss` (어셈블리 루프) | `__cmsis_start()` 가 `__copy_table_start__` / `__zero_table_start__` 표를 돌며 여러 영역 처리 |
| 생성자 | init_array 루프 직접 | `bl __libc_init_array` (newlib: preinit_array → `_init()` → init_array) | newlib `_start` 경유 |
| main 뒤 | `sh_exit(main())` | `bl main` 다음 `bx lr` (돌아오면 안 됨) | `_start` → `exit()` |

`SystemInit()` (`system_stm32f4xx.c`) 가 하는 일: FPU 가 있으면 `SCB->CPACR` 에 CP10/CP11 full access, (선택) 외부 메모리 컨트롤러 초기화, (선택) `USER_VECT_TAB_ADDRESS` 면 `SCB->VTOR` 재배치. PLL 을 켜는 진짜 클럭 설정은 HAL 프로젝트에선 main 안의 `SystemClock_Config()` 몫이다 (BM-A01 에서 순서를 다룬다). **SystemInit 이 .data 복사 전에 불린다면, 그 안에서 `.data`/`.bss` 전역 (예: `SystemCoreClock`) 을 쓰면 그 값은 곧이어 복사·제로화로 덮인다** — ST 의 SystemInit 이 전역을 건드리지 않는 이유.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

바뀐 것은 `reset_init` 의 세 줄짜리 본문뿐이다. 각 줄의 설계 이유:

- `const uint32_t *src = _sidata;` — `_sidata` 는 `LOADADDR(.data)`. FLASH 쪽은 읽기만 하므로 `const`.
- `dst < _edata` — 서로 다른 "배열" 의 포인터 비교는 ISO C 로는 정의되지 않지만(C11 6.5.8), 링커 심볼은 같은 연속 영역의 경계라 모든 임베디드 툴체인이 이 관용구를 쓴다. 신경 쓰이면 `uintptr_t` 로 바꿔 비교한다.
- 길이 0 (`.data` 가 없는 펌웨어) 이면 루프가 한 번도 안 돈다 — `<` 비교 덕. `!=` 로 쓰면 길이 0 은 괜찮지만, 경계가 4의 배수가 아니면 끝을 지나쳐 RAM 을 계속 덮어쓴다.
- `volatile` 이 없어도 되는 이유: 이 루프의 쓰기는 이후 main 이 읽으므로 컴파일러가 지울 수 없다. 반대로 컴파일러가 "memcpy 로 바꾸는" 것은 막아야 해서 플래그가 필요하다 (F5).

## 7. 자주 하는 실수

**실수 1 — `.isr_vector` 에 `KEEP` 을 빼먹음.** `-ffunction-sections -fdata-sections -Wl,--gc-sections` 로 빌드하면 아무도 참조하지 않는 벡터 테이블이 지워진다 (`__attribute__((used))` 는 **컴파일러**에게만 효과, 링커 GC 는 못 막는다). 벡터만 있는 최소 파일로 실측:

```text
[keep.ld]
.isr_vector          8           0
.text                4           8
[nokeep.ld]
.isr_vector          0           0
.text                4           0
```

`nokeep` 이미지는 주소 0 에 코드가 놓여 코어가 명령어 바이트를 SP/PC 로 읽는다. 고친 코드: `KEEP(*(.isr_vector))`. (이 레슨의 main 은 `g_vectors` 를 읽기 때문에 본 펌웨어에선 KEEP 없이도 살아남았다 — 그래서 더 위험하다.)

**실수 2 — 핸들러 이름 오타.** `handlers.c` 의 `SysTick_Handler` 를 `Systick_Handler` 로 바꿔 빌드 (실측):

```text
FAIL solution.c:221  (uintptr_t)SysTick_Handler != (uintptr_t)Default_Handler
Default_Handler: unexpected exception, IPSR=15
qemu exit=2
```

컴파일 에러도 링크 에러도 없다. weak alias 는 "없으면 기본값" 이라 오타 = 새 함수 하나 + 벡터는 Default_Handler. C++ 파일에서 `extern "C"` 를 빼먹어도 이름이 맹글링돼 같은 일이 생긴다. 고친 코드: CMSIS 이름을 그대로 복사하고, Default_Handler 에서 IPSR 을 찍게 해 둔다.

**실수 3 — 같은 파일에 weak alias 와 진짜 정의를 같이 둠.**

```text
dup.c:2:6: error: redefinition of 'SysTick_Handler'
    2 | void SysTick_Handler(void) { }
      |      ^~~~~~~~~~~~~~~
```

alias 선언은 그 이름의 정의다. 덮어쓰기는 **다른 번역 단위**에서 (그래서 handlers.c 가 따로 있다).

**실수 4 — "벡터 테이블이 주소 0 인가" 를 `&table == 0` 으로 검사.** C 에서 객체의 주소는 널 포인터일 수 없으므로 GCC 는 비교를 상수로 접는다. 처음 이 레슨의 검사를 `CHECK(SCB_VTOR == 0u && (uintptr_t)g_vectors == 0u)` 로 썼더니 정답 펌웨어가 FAIL 했다. `run_qemu.sh addr0` (demo_addr0.c) 실측:

```text
00000000 <table_at_zero_naive>:
   0:	2000      	movs	r0, #0
   2:	4770      	bx	lr

00000004 <table_at_zero_vtor>:
   4:	f04f 23e0 	mov.w	r3, #3758153728	@ 0xe000e000
   8:	4803      	ldr	r0, [pc, #12]	@ (18 <table_at_zero_vtor+0x14>)
   a:	f8d3 3d08 	ldr.w	r3, [r3, #3336]	@ 0xd08
   e:	1ac0      	subs	r0, r0, r3
  10:	fab0 f080 	clz	r0, r0
  14:	0940      	lsrs	r0, r0, #5
  16:	4770      	bx	lr
  18:	00000000 	.word	0x00000000
```

고친 코드: `CHECK(SCB_VTOR == (uintptr_t)g_vectors);` — volatile 레지스터 값과 비교하면 접을 수 없다. (주소 0 에 진짜 데이터가 있는 Cortex-M 에서 `*(uint32_t *)0` 을 읽는 코드도 같은 이유로 UB 취급 → GCC 는 `-fno-delete-null-pointer-checks` 를 쓰기도 한다.)

**실수 5 — reset_init 전에 전역을 씀.** 하네스 카운터를 `static int g_pass, g_total;` (.bss) 로 두면 시작 코드에서 셈이 0xA5A5A5A5 부터 시작한다. Reset_Handler 안에서 `printf` 나 전역 플래그를 쓰는 디버그 코드도 같다. 고친 코드: 초기화 전 코드는 지역 변수와 링커 심볼만.

**실수 6 — `-nostdlib` 만 쓰고 `-ffreestanding`/`-fno-tree-loop-distribute-patterns` 를 뺌.** F5 의 `undefined reference to memcpy/memset`. 더 나쁜 경우는 newlib 를 링크해 **링크는 되는데** memset 이 `.data` 가 필요한 구현이거나 스택을 많이 쓰는 경우. 고친 코드: 두 플래그 모두, 또는 startup 루프를 어셈블리로.

**실수 7 — QEMU 통과를 하드웨어 통과로 믿음.** nopoison 의 `.bss` 가짜 PASS, F4 의 미러 덕 통과. 고친 방법: RAM 을 일부러 더럽히는 하네스, 링커 `ASSERT`, 그리고 `practice.c` 같은 정적 린터로 "칩의 진짜 메모리 맵" 기준 검사.

## 8. 면접 Q&A

<details>
<summary>Q1. 리셋부터 main 의 첫 줄까지 무슨 일이 일어나나요?</summary>

코어가 하드웨어로: VTOR(리셋 값 0)+0 의 워드를 MSP 로, +4 의 워드를 PC 로(bit0 → EPSR.T). 특권 Thread 모드, MSP 사용. 그다음 `Reset_Handler`: (벤더에 따라 여기서 `SystemInit` — FPU 접근 허용, VTOR 등) → `.data` 를 FLASH(LMA)에서 RAM(VMA)으로 복사 → `.bss` 0 → C++ 생성자/`__attribute__((constructor))` (`__libc_init_array`) → `main`. 이 레슨 실측: MSP=0x20010000, PC=0x25C, 7 워드 복사, 19 워드 0, 생성자 2개, main @0x2B0. 꼬리질문 "main 이 리턴하면?" — ST startup 은 `bl main` 다음 줄이 `bx lr` 인데, 그때 LR 에 무엇이 남았느냐에 따라 제자리 루프가 되거나 main 중간 같은 엉뚱한 곳으로 간다 (예측 불가). 그래서 펌웨어 main 은 무한 루프, 또는 이 레슨처럼 명시적 exit.

</details>

<details>
<summary>Q2. 왜 .data 는 Flash 와 RAM 을 둘 다 먹나요? .rodata 와 무엇이 다른가요?</summary>

초기값은 전원이 꺼져도 남아야 하니 FLASH 에 (LMA), 프로그램이 값을 바꿔야 하니 RAM 에 (VMA). 그래서 `> RAM AT > FLASH`, readelf 의 `VirtAddr 0x20000000 / PhysAddr 0x00000C94`. `const` 를 붙이면 `.rodata` 로 가서 FLASH 에만 산다 — 큰 룩업 테이블에 `const` 를 빼먹으면 Flash 와 RAM 을 둘 다 먹는 대표 사고 (BM-B05). `.bss` 는 초기값이 0 이라 파일엔 없고 RAM 만, 대신 부팅 때 0 으로 채우는 시간이 든다.

</details>

<details>
<summary>Q3. 리셋 벡터의 bit0 이 0 이면 어떻게 되나요? 왜 홀수여야 하죠?</summary>

Cortex-M 은 Thumb 명령만 실행하고, 분기 주소의 bit0 이 "Thumb 상태" 를 뜻한다(interworking 관례). 벡터에서 읽은 bit0 이 EPSR.T 가 되는데, T=0 으로 명령을 실행하려 하면 UsageFault(INVSTATE). 리셋 직후엔 UsageFault 가 비활성이라 HardFault 로 승격(HFSR.FORCED). 실측: `CFSR=0x00020000 HFSR=0x40000000`, 쌓인 xPSR 의 T=0, 쌓인 pc=0x270. HardFault 벡터마저 짝수면 핸들러에서 또 fault → Lockup. C 로 쓰면 링커가 bit0 을 붙여 주지만, 어셈블리 라벨에 `.thumb_func`/`.type %function` 이 없으면 안 붙는다 (ARM-M08).

</details>

<details>
<summary>Q4. _estack 이 RAM 밖이면? 왜 바로 Lockup 이 되나요?</summary>

리셋은 문제없이 지나간다(SP 는 값일 뿐). 첫 `push` 에서 BusFault → HardFault 로 승격 → 예외 진입 시 8워드 스태킹도 같은 잘못된 SP 로 실패 → 핸들러 안에서 다시 push → 우선순위 -1 (HardFault) 안에서 fault = Lockup. QEMU 실측: `Lockup: can't escalate 3 to HardFault (current priority -1)`, R13=0x2FFFFFE0. 진짜 칩은 Lockup 상태에서 멈추고 디버거로 보면 PC 가 0xEFFFFFFE 같은 값을 가리키며, 칩/설정에 따라 리셋 요청이 나기도 한다(확인 권장). 예방: 링커 `ASSERT`, 칩별 정확한 `MEMORY`, 그리고 QEMU 의 미러처럼 "에뮬레이터는 돌았다" 를 믿지 않기 (F4).

</details>

<details>
<summary>Q5. weak alias 는 어떻게 동작하고, 무엇이 위험한가요?</summary>

`void X(void) __attribute__((weak, alias("Default_Handler")));` 는 X 라는 **약한 심볼**을 Default_Handler 와 같은 주소로 만든다. 링크에 같은 이름의 강한 정의가 오면 링커는 강한 쪽을 쓰고 약한 쪽은 버린다. 장점: 앱은 함수 이름만 맞추면 벡터 테이블을 안 고쳐도 된다. 위험: 오타·C++ 맹글링이 **조용히** Default_Handler 로 간다 (실측 `IPSR=15`). 그래서 Default_Handler 는 무한 루프라도 IPSR 을 레지스터/변수에 남겨 디버거로 "어느 예외가 주인이 없었나" 알 수 있게 한다 (IPSR−16 = IRQ 번호, IPSR×4 = 벡터 오프셋).

</details>

<details>
<summary>Q6. 벤더 Reset_Handler 는 왜 `ldr sp, =_estack` 를 또 하나요? 코어가 이미 했는데요.</summary>

코어가 벡터 [0] 을 읽는 건 **진짜 리셋** 때뿐이다. 디버거가 ELF 를 올리고 엔트리(`ENTRY(Reset_Handler)`)로 PC 만 바꿔 실행하거나, 부트로더가 앱의 Reset_Handler 로 점프할 때(BM-A03)는 SP 가 이전 값 그대로다. 그래서 방어적으로 다시 설정한다. 부트로더 쪽 정석은 점프 전에 앱 벡터 [0] 을 MSP 에 넣고 VTOR 을 바꾸는 것.

</details>

<details>
<summary>Q7. reset_init 같은 루프를 C 로 쓸 때 무엇을 조심하나요?</summary>

(1) 초기화 전에는 `.data`/`.bss` 전역을 읽지도 쓰지도 말 것 — 지역 변수와 링커 심볼만. (2) 컴파일러가 루프를 `memcpy`/`memset` 호출로 바꿀 수 있다 (`-nostdlib` 이면 링크 에러, 있으면 그 구현의 전제조건 문제) → `-ffreestanding -fno-tree-loop-distribute-patterns`. (3) 스택은 이미 쓸 수 있다 (코어가 MSP 를 세팅) — 그래서 C 로 쓸 수 있는 것. (4) 경계 정렬은 링커 스크립트의 `ALIGN(4)` 가 보장해야 워드 루프가 맞다 (BM-B07). (5) 외부 SDRAM 에 `.data` 를 둔다면 메모리 컨트롤러 초기화(SystemInit)가 복사보다 먼저.

</details>

<details>
<summary>Q8. QEMU 로 부팅 테스트할 때 믿으면 안 되는 것은?</summary>

실측 세 가지: RAM 이 0 으로 시작해 `.bss` 제로화 누락이 안 보임(nopoison), 4 MB SSRAM 이 0x20400000 에 미러돼 있어 RAM 크기 초과 스택이 돌아감(F4), 메모리 맵의 "RESERVED" 영역은 unimplemented device 라 읽으면 0, 쓰면 무시(RAZ/WI) — 진짜 칩이면 BusFault 일 곳. 그 밖에 QEMU 는 클럭 트리·플래시 대기 상태·캐시가 없다. 그래서 "QEMU 에서 됐다" 는 로직 검증일 뿐, 링커 `ASSERT` 와 실제 칩 메모리 맵 기준 린트(practice.c) 로 보완한다.

</details>

## 9. 추가 연습

VM 없이 Mac 에서 도는 정적 검사기다. `solution.elf` 에서 실제로 뽑은 벡터 48 워드(`objdump -s -j .isr_vector`)와 링커 심볼(`nm`)을 표로 넣어 두었다. 진짜 이미지는 깨끗해야 하고, Part B 의 결함들을 표 위에 주입하면 각각 정확히 그 오류 비트가 나와야 한다.

### 연습 1 — 벡터 테이블 린트 `vt_lint`

SP 8-정렬, SP ∈ (RAM 시작, RAM 끝] (full-descending 스택이라 끝 주소 자체는 OK, 시작 주소는 NG), Reset bit0, Reset 이 FLASH 안, 예약 칸(7–10, 13) 은 0, 나머지 칸은 0 이 아니고 bit0=1.

<details>
<summary>답</summary>

비트마스크로 모든 위반을 모은다. 주입: F1 `vt[1]=0x270` → `E_RESET_THUMB` 만, F3 `0x30000000` / F4 `0x20800000` / F2 `0x20000000` → `E_SP_RANGE` 만 (F4 는 QEMU 가 숨긴 버그를 **정적 검사가 잡는** 사례), `0x2000FFFC` → `E_SP_ALIGN`, 예약 칸에 값 → `E_RESERVED`, SysTick 칸 0 → `E_HANDLER_NULL` (그 예외가 오면 주소 0 = SP 값으로 점프), HardFault 칸 짝수 → `E_HANDLER_THUMB`, `0x00040001` → `E_RESET_RANGE`.

</details>

### 연습 2 — 리셋 예측 `reset_sim`

벡터 표만 보고 BOOT / HARDFAULT / LOCKUP 을 예측하고, HARDFAULT 면 CFSR·HFSR·쌓인 pc 를 낸다. 실측 F1·F3 와 같은 답이어야 한다.

<details>
<summary>답</summary>

MSP = `vt[0] & ~3` (SP[1:0] 은 하드웨어가 0 으로), PC = `vt[1] & ~1`, T = `vt[1] & 1`. SP 가 RAM 밖 → LOCKUP. T=0 → `CFSR = 1<<17` (INVSTATE), `HFSR = 1<<30` (FORCED), 쌓인 pc = PC, 그리고 HardFault 벡터 bit0 이 0 이면 LOCKUP. 단순화 모델임에 주의: 실제로는 SP 가 RAM 밖이어도 첫 push 전까지는 실행된다.

</details>

### 연습 3 — weak alias 인구조사 `weak_census`

Default_Handler(0x1A0) 로 가는 칸 수를 센다. 예상: 시스템 예외 7개(NMI, MemManage, BusFault, UsageFault, SVC, DebugMon, PendSV) + IRQ 32개 = 39. HardFault·SysTick 은 덮어써졌으니 제외.

<details>
<summary>답</summary>

`(vt[i] & ~1) == default_addr` 인 비예약 칸을 센다. 이 숫자가 갑자기 늘면 = 누군가 핸들러 이름을 바꿨거나 파일이 링크에서 빠졌다는 신호 → CI 에서 이 값을 고정해 두면 실수 2 를 빌드 단계에서 잡는다.

</details>

### 연습 4 — 링커 심볼 린트 + 예산 `sym_lint`

순서(sdata ≤ edata ≤ sbss ≤ ebss), 4-정렬, `.data` LMA 가 FLASH 안, `ebss + min_stack ≤ estack`, init_array 정렬. 예산: FLASH 사용량 = `_sidata + (.data 크기)` (= size 의 text+data 3212+36=3248), 복사 워드 7, 0 워드 19, 정적 RAM 0x6C, 스택 포함 0x86C.

<details>
<summary>답</summary>

`.data` 의 LMA 가 이미지의 마지막이라 "끝 주소 = 사용량" 이 성립한다 (이 링커 스크립트의 섹션 순서 덕). 주입: `ALIGN(4)` 빠진 경계 0x2000001B → `S_ALIGN`, `estack=0x20000000` → `S_STACK`, `sidata=0x3FFF0` → `S_LMA`, `.bss` 가 `.data` 와 겹침 → `S_ORDER`.

</details>

### 연습 5 — IPSR → 벡터 `ipsr_lookup`

Default_Handler 가 본 IPSR 로 벡터 오프셋, IRQ 번호(CMSIS `IRQn_Type` 관례: IPSR−16, 시스템 예외는 음수), 등록된 주소를 낸다.

<details>
<summary>답</summary>

IPSR 23 → 오프셋 0x5C, IRQ 7, 0x1A1 (Default). IPSR 3 → IRQn −13 (HardFault), IPSR 15 → −1 (SysTick), 오프셋 0x3C. IPSR 0 (Thread 모드) 과 표 밖(48 이상)은 false.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-B08_pr && /tmp/BM-B08_pr    # 27/27 passed
# VM 에서 최신 덤프와 함께: limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"
```

실행 스크립트 전체:

<!-- code: run_qemu.sh -->

<!-- code: demo_boot.gdb -->

## 10. 핵심 정리

- 리셋 = 코어가 `[VTOR+0] → MSP`, `[VTOR+4] → PC (bit0 → T)`. 첫 명령 전에 스택이 이미 있으므로 Reset_Handler 를 C 로 쓸 수 있다.
- Reset_Handler = (SystemInit) → `.data` 복사 (LMA→VMA) → `.bss` 0 → init_array → main. 그 사이엔 전역 금지.
- `.data` 는 FLASH+RAM, `.bss` 는 RAM 만, `const` 는 FLASH 만. `> RAM AT > FLASH`, `ALIGN(4)`, `KEEP(.isr_vector)`, `ASSERT`.
- 벡터의 코드 주소는 전부 홀수. bit0 누락 = INVSTATE → FORCED HardFault (실측 CFSR 0x00020000 / HFSR 0x40000000).
- `_estack` 이 RAM 밖 = 첫 push 에서 fault → HardFault 안에서 또 fault → Lockup.
- weak alias: 같은 이름의 강한 정의가 이긴다. 이름 오타는 조용히 Default_Handler — IPSR 을 남겨라.
- `-ffreestanding -nostdlib -fno-tree-loop-distribute-patterns`: 루프가 memcpy/memset 으로 바뀌지 않게.
- QEMU 는 RAM 0 초기화·메모리 미러·RAZ/WI 예약 영역으로 버그를 숨긴다. RAM 을 더럽히는 하네스와 정적 린트로 보완.

## 참고자료

- Arm, *ARMv7-M Architecture Reference Manual* (DDI 0403E) — B1.5.5 Reset behavior, B1.5.6 Exception entry, B1.5.15 Lockup, B3.2.15 CFSR / B3.2.16 HFSR (절 번호 확인 권장).
- Arm, *Cortex-M3 Devices Generic User Guide* (DUI 0552A) — 2.3.4 Vector table, 4.3 SCB (VTOR, CFSR, HFSR).
- Arm, *Semihosting for AArch32 and AArch64* — SYS_WRITE0 (0x04), SYS_EXIT (0x18), SYS_EXIT_EXTENDED (0x20): https://github.com/ARM-software/abi-aa/blob/main/semihosting/semihosting.rst
- QEMU 소스 `hw/arm/mps2.c` (AN385 메모리 맵), `target/arm/cpu.c` (M-profile 리셋 시 벡터 로딩), `semihosting/arm-compat-semi.c`.
- Arm, *Application Note AN385: ARM Cortex-M3 SMM on V2M-MPS2*.
- STMicroelectronics, STM32CubeF4 `Drivers/CMSIS/Device/ST/STM32F4xx/Source/Templates/gcc/startup_stm32f407xx.s`, `system_stm32f4xx.c`.
- Arm CMSIS 5 `Device/ARM/ARMCM3/Source/startup_ARMCM3.c`, `CMSIS/Core/Include/cmsis_gcc.h` (`__cmsis_start`).
- GNU ld 매뉴얼 — `KEEP`, `AT>`, `LOADADDR`, `ASSERT`, `SORT_BY_INIT_PRIORITY`; GCC 매뉴얼 — `alias`, `weak`, `constructor (priority)`, `-ffreestanding`, `-ftree-loop-distribute-patterns`.
- 관련 레슨: BM-B07 (호스트 crt0 시뮬레이션), BM-B05 (map 파일), BM-B06 (부트/앱 레이아웃), BM-B09 (printf · semihosting 이 디버거 없이 멈추는 이유), ARM-M08 (어셈블리 벡터 테이블), ARM-M11 (같은 QEMU 랩), BM-A01 (SystemInit 순서), BM-A03 (부트로더 → 앱 점프).
