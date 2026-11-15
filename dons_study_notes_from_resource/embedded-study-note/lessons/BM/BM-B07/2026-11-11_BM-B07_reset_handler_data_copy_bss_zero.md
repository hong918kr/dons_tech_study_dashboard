---
id: BM-B07
time: 60분
updated: 2026-11-11
---
# BM-B07 Reset handler 구현 (호스트 시뮬레이션): .data 복사·.bss 제로화

## 학습 목표

- `main()` 전에 일어나는 일을 순서대로 말할 수 있다: 하드웨어가 vector[0] → MSP, vector[1] → PC 를 읽고, `Reset_Handler` 가 **.data 복사 → .bss 제로화 → constructor(init_array) → main** 을 한다.
- 링커 심볼 `_sidata / _sdata / _edata / _sbss / _ebss / __init_array_start / __init_array_end` 가 **변수가 아니라 주소** 임을 알고, 그 주소만으로 세 루프를 직접 쓴다.
- 길이 0, 4의 배수가 아닌 길이, 정렬이 어긋난 시작을 테스트로 잡고, 실무 규칙(링커 스크립트의 `ALIGN(4)`)과 바이트 꼬리(tail) 처리 둘 다 설명할 수 있다.
- 세 단계의 **순서** 가 왜 정답의 일부인지 (constructor 가 .data 값을 읽는다), 그리고 0 으로 채워진 RAM 이 .bss 버그를 어떻게 숨기는지 재현한다.
- Reset_Handler 를 C 로 쓸 때 조심할 점 — 전역 변수 금지, GCC 가 루프를 `memcpy`/`memset` 호출로 바꾸는 문제 — 를 실제 asm 과 링크 오류로 보여 줄 수 있다.

## 1. 문제

"전원을 켜면 `main()` 까지 무슨 일이 일어나나요?" 는 펌웨어 면접의 단골 질문이고, 보드 브링업(bring-up)에서 "디버거로 받으면 되는데 워치독 리셋 뒤엔 전역 변수가 이상하다" 류 버그의 정체가 바로 이 구간이다. SSD 컨트롤러든 RF 모듈 보드의 STM32 든, 손으로 쓴 startup 이 한 줄 틀리면 증상은 엉뚱한 곳(통신 속도, 카운터, 상태 머신)에서 나온다.

C 언어는 "전역 변수는 프로그램 시작 때 초기값을 가진다" 고 약속한다 (`int x = 5;` 는 5, `static int y;` 는 0). PC 에서는 OS 로더(loader)가 이 약속을 지켜 주지만, 베어메탈(bare-metal)에는 OS 가 없다. Flash 에 구운 이미지 하나와 쓰레기가 든 SRAM 뿐이다. 그래서 C 런타임 시작 코드(crt0, C runtime zero)가 직접 한다:

- **.data** (초기값 있는 전역): 초기값은 Flash 에 저장되고(LMA, load address), 변수는 RAM 에서 산다(VMA, virtual/run address). → **복사**
- **.bss** (0 으로 시작하는 전역): Flash 에는 크기만 있다. → **0 으로 채우기**
- **.init_array** (C++ 전역 생성자, `__attribute__((constructor))`): 함수 포인터 표. → **차례로 호출** (CPP-B09 에서 C++ 쪽을 다뤘다)

이 레슨은 그 일을 호스트(맥)에서 시뮬레이션한다. 가짜 Flash 배열 `flash_img[]` 과 가짜 RAM 배열 `ram[]` 을 두고, 링커 심볼 역할을 하는 포인터 묶음을 넘겨 `reset_init()` 을 테스트한다. 진짜 Cortex-M 부팅(QEMU)은 BM-B08 에서 이 코드를 그대로 가져가 쓴다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">이 레슨의 장난감 메모리 맵 (solution.c 의 레이아웃 그대로)</text>
  <text x="20" y="44" fill="currentColor" font-weight="bold">Flash 0x08000000 (256 B, 읽기 전용)</text>
  <rect x="20" y="52" width="175" height="34" class="f-mut" stroke="currentColor"/>
  <rect x="195" y="52" width="262" height="34" class="f-mut" stroke="currentColor"/>
  <rect x="457" y="52" width="55" height="34" class="f-acc" stroke="currentColor"/>
  <rect x="512" y="52" width="208" height="34" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="30" y="73" fill="currentColor">vector table (w0..15)</text>
  <text x="205" y="73" fill="currentColor">.text  (w16..39)</text>
  <text x="461" y="73" fill="currentColor">.data</text>
  <text x="530" y="73" fill="currentColor">지워진 영역 0xFF…</text>
  <text x="430" y="106" fill="currentColor" font-family="monospace">_sidata = 0x080000A0 (LMA)</text>
  <text x="20" y="106" fill="currentColor" font-family="monospace">[0]=0x20000080 [1]=0x08000041</text>
  <path d="M484 112 L160 186" stroke="currentColor" fill="none" stroke-width="1.5"/>
  <path d="M160 186 L170 176 M160 186 L174 188" stroke="currentColor" fill="none" stroke-width="1.5"/>
  <text x="20" y="150" fill="currentColor" font-weight="bold">① 복사 5 워드 (LMA → VMA)</text>
  <text x="20" y="180" fill="currentColor" font-weight="bold">SRAM 0x20000000 (128 B)</text>
  <rect x="20" y="190" width="66" height="34" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <rect x="86" y="190" width="21" height="34" class="f-warn" stroke="currentColor"/>
  <rect x="107" y="190" width="110" height="34" class="f-acc" stroke="currentColor"/>
  <rect x="217" y="190" width="87" height="34" class="f-ok" stroke="currentColor"/>
  <rect x="304" y="190" width="22" height="34" class="f-warn" stroke="currentColor"/>
  <rect x="326" y="190" width="44" height="34" class="f-mut" stroke="currentColor"/>
  <rect x="370" y="190" width="350" height="34" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="115" y="211" fill="currentColor">.data w4..8</text>
  <text x="225" y="211" fill="currentColor">.bss w9..12</text>
  <text x="400" y="211" fill="currentColor">heap / stack (아래로 자람) ←</text>
  <text x="640" y="244" fill="currentColor" font-family="monospace">_estack</text>
  <text x="625" y="260" fill="currentColor" font-family="monospace">0x20000080</text>
  <text x="80" y="244" fill="currentColor" font-family="monospace">_sdata 0x20000010</text>
  <text x="206" y="260" fill="currentColor" font-family="monospace">_edata = _sbss 0x20000024</text>
  <text x="290" y="276" fill="currentColor" font-family="monospace">_ebss 0x20000034</text>
  <text x="300" y="244" fill="currentColor">.noinit w14..15</text>
  <text x="225" y="180" fill="currentColor" font-weight="bold">② 0 으로 4 워드</text>
  <text x="20" y="294" fill="currentColor">주황 = canary(0xA5A5A5A5 / 0x5A5A5A5A): 경계 바로 바깥 — 한 바이트라도 바뀌면 off-by-one. 점선 = 쓰레기 그대로여야 함.</text>
</svg>
```

### Part A — 워드 단위 crt0

`crt_syms_t` (링커 심볼 역할 포인터 7개)를 받아:

- `copy_data_words(dst, dst_end, src)` — `[sdata, edata)` 를 `sidata` 에서 4바이트씩 복사
- `zero_bss_words(dst, dst_end)` — `[sbss, ebss)` 를 0 으로
- `run_init_array(start, end)` — 함수 포인터를 앞에서부터 한 번씩 호출 (빈 배열이면 0번)
- `reset_init(s)` — 세 단계를 **올바른 순서로**

테스트: 기본 레이아웃(.data 5워드, .bss 4워드 바로 뒤), canary 보존, `.noinit` 보존, Flash 불변, 길이 0, 전수 243 레이아웃, init_array 순서·빈 배열, constructor 가 복사된 .data 와 0 이 된 .bss 를 보는지.

### Part B — 길이가 4의 배수가 아닐 때: 바이트 꼬리 변형

링커 스크립트에서 `. = ALIGN(4);` 를 빼먹으면 `_edata` 가 홀수 주소가 되고 `.bss` 가 정렬 안 된 주소에서 시작한다. `copy_bytes_tail()` / `zero_bytes_tail()` 은 **head 바이트 → 워드 본체 → tail 바이트** 로 처리하고 워드로 옮긴 횟수를 돌려준다. `reset_init_bytes()` 는 같은 순서로 이 둘을 쓴다. 테스트: data 길이 0..13 × bss 길이 0..13 × 시작 정렬 4 × 원본 정렬 4 = 3136 레이아웃 전수, 그리고 하네스가 주는 두 버그 모델(워드 개수 올림 / 내림)을 검증기가 잡는지.

### Part C — 미니 펌웨어 부팅: 콜드 부트, 웜 리셋, constructor 순서

전역 변수 8개(`g_baud = 115200`, `g_name = "BM-B07"`, `.bss` 의 `g_core_hz`, `g_uart_div`, `g_ticks` …)와 constructor 3개(`ctor_clock` → `ctor_uart` → `ctor_led`)가 있는 펌웨어. `ctor_uart` 는 .data 의 `g_baud` 와 앞 constructor 가 쓴 `g_core_hz` 를 읽어 `g_uart_div = 139` 를 계산한다. `fw_boot()` (Reset_Handler 역할)를 완성하고, 콜드 부트(난수 RAM)·웜 리셋(앱이 남긴 값)·`.noinit` 생존을 확인한다. 하네스가 주는 버그 부트 3개(constructor 먼저, .bss 제로 누락, init_array 링크 순서 뒤바뀜)가 각각 어떻게 드러나는지 본다.

## 2. Problem (English)

> **Write the C runtime start-up (crt0) for a Cortex-M part and prove it with host tests.**
>
> The linker gives you `_sidata` (load address of `.data` in Flash), `_sdata/_edata` (run address range of `.data` in SRAM), `_sbss/_ebss`, and `__init_array_start/__init_array_end`. On the host they are modelled as pointers into a fake Flash array and a fake SRAM array (`crt_syms_t`).
>
> 1. `void reset_init(const crt_syms_t *s)` — copy `.data` word by word, zero `.bss`, then call every function in the init array in order. Empty ranges must not touch memory. Nothing outside `[_sdata, _ebss)` may change (canaries, a `.noinit` area), and Flash is read-only.
> 2. `unsigned copy_bytes_tail(uint8_t *dst, const uint8_t *dst_end, const uint8_t *src)` and `zero_bytes_tail(...)` for boundaries that are **not** word multiples: bytes until `dst` is 4-aligned, then words (only if `src` is aligned too), then the tail bytes. Return the number of word transfers. Example: `copy_bytes_tail(ram+17, ram+30, flash+161)` → 3 head bytes, **2** words, 2 tail bytes.
> 3. `uint32_t fw_boot(void)` — the Reset_Handler: run-time init, then `main()`. A constructor computes `g_uart_div = (g_core_hz + g_baud/2) / g_baud` from a `.data` value and a value written by an earlier constructor; with 16 MHz and 115200 baud it must be **139** on a cold boot (random SRAM) and again after a warm reset (SRAM holds the previous run's values), while a `.noinit` reset counter goes 1 → 2.
>
> Follow-ups: Why can't this function use a `static` variable? Why does `.data` cost Flash *and* RAM? What does the linker script do so that a word loop is exact? Why does GCC `-O2` turn your loop into a call to `memcpy`, and why is that a problem in a `-nostdlib` build? Why might a missing `.bss` clear pass on the bench and fail after a watchdog reset?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "16/40 passed" — 버그 모델 검출·길이 0 같은 테스트는 빈 스텁으로도 통과한다. TODO 8곳을 채워 40/40.
./starter -v      # 정답 코드로 돌리면 RAM 덤프와 버그 부트 결과 (5. 풀이의 표)
```

관찰용 파일 (하네스와 별개, 타깃 컴파일 전용):

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -Wall -Wextra -Werror -S demo_target_crt0.c -o -   # 맥
limactl shell elinux -- bash -lc 'arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -std=c11 -O2 -Wall -Wextra -Werror -c /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/BM/BM-B07/demo_target_crt0.c -o /tmp/BM-B07_t.o && arm-none-eabi-objdump -dr --no-show-raw-insn /tmp/BM-B07_t.o'
# VM (5. 풀이). 비교: -ffreestanding 또는 -fno-tree-loop-distribute-patterns 를 붙여 다시
```

## 4. 힌트

<details>
<summary>힌트 1 — 루프 조건은 <code>!=</code> 가 아니라 <code>&lt;</code></summary>

`while (dst < dst_end) *dst++ = *src++;` — 길이 0 (`dst == dst_end`) 이면 한 번도 안 돈다. `!=` 로 쓰면 끝 주소가 4의 배수가 아닐 때 영원히 지나쳐 RAM 전체를 덮는다. 따로 "길이 0 이면 return" 같은 분기는 필요 없다.

</details>

<details>
<summary>힌트 2 — 순서: 복사 → 제로 → constructor</summary>

constructor 는 평범한 C/C++ 코드라 전역 변수를 읽고 쓴다. 복사 전에 부르면 쓰레기를 읽고, 제로 전에 부르면 constructor 가 .bss 에 쓴 값이 지워진다. 복사와 제로 사이 순서는 영역이 겹치지 않으니 결과는 같지만, 관례는 복사 먼저.

</details>

<details>
<summary>힌트 3 — head / body / tail</summary>

`(uintptr_t)dst & 3u` 가 0 이 될 때까지 바이트로 → `dst_end - dst >= 4` 인 동안 워드로 → 남은 것은 바이트로. 워드 본체는 **src 도 정렬돼 있을 때만** (dst 와 src 의 어긋남이 다르면 둘을 동시에 맞출 수 없다 → 전부 바이트). `ram[]` 이 `uint32_t` 배열이라 시작 주소가 4의 배수이므로, 바이트 오프셋의 정렬 = 실제 주소의 정렬이다.

</details>

<details>
<summary>힌트 4 — fw_boot 는 두 줄</summary>

`reset_init(&FW_SYMS); return fw_main();` — 실칩의 Reset_Handler 는 main 이 돌아오면 `for (;;)` 로 멈추지만, 시뮬레이션에서는 main 의 반환값(오류 비트)을 테스트가 본다.

</details>

## 5. 풀이

### 0. 리셋에서 main 까지

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">Cortex-M 리셋 시퀀스 (주소는 이 레슨의 레이아웃)</text>
  <rect x="10" y="34" width="160" height="62" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="54" fill="currentColor" font-weight="bold">하드웨어 (코드 없음)</text>
  <text x="20" y="72" fill="currentColor" font-family="monospace">MSP ← [0x08000000]</text>
  <text x="20" y="88" fill="currentColor" font-family="monospace">PC  ← [0x08000004]</text>
  <rect x="190" y="34" width="160" height="62" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="200" y="54" fill="currentColor" font-weight="bold">Reset_Handler</text>
  <text x="200" y="72" fill="currentColor" font-family="monospace">0x08000041 = 주소|1</text>
  <text x="200" y="88" fill="currentColor">(Thumb 비트, ARM-M08)</text>
  <rect x="370" y="34" width="170" height="62" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="380" y="54" fill="currentColor" font-weight="bold">SystemInit (CMSIS)</text>
  <text x="380" y="72" fill="currentColor">클럭·FPU·VTOR (BM-A01)</text>
  <text x="380" y="88" fill="currentColor">전역 변수 쓰면 안 됨!</text>
  <rect x="560" y="34" width="170" height="62" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="570" y="54" fill="currentColor" font-weight="bold">① .data 복사</text>
  <text x="570" y="72" fill="currentColor" font-family="monospace">0x080000A0→0x20000010</text>
  <text x="570" y="88" fill="currentColor">5 워드</text>
  <rect x="560" y="130" width="170" height="62" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="570" y="150" fill="currentColor" font-weight="bold">② .bss = 0</text>
  <text x="570" y="168" fill="currentColor" font-family="monospace">0x20000024..0x20000034</text>
  <text x="570" y="184" fill="currentColor">4 워드</text>
  <rect x="370" y="130" width="170" height="62" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="380" y="150" fill="currentColor" font-weight="bold">③ init_array</text>
  <text x="380" y="168" fill="currentColor">ctor_clock → ctor_uart</text>
  <text x="380" y="184" fill="currentColor">→ ctor_led  (log "CUL")</text>
  <rect x="190" y="130" width="160" height="62" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="200" y="150" fill="currentColor" font-weight="bold">main()</text>
  <text x="200" y="168" fill="currentColor">이제 C 의 약속이 성립</text>
  <text x="200" y="184" fill="currentColor">g_baud=115200, g_ticks=0</text>
  <rect x="10" y="130" width="160" height="62" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="150" fill="currentColor" font-weight="bold">main 이 돌아오면</text>
  <text x="20" y="168" fill="currentColor" font-family="monospace">for (;;) { }</text>
  <text x="20" y="184" fill="currentColor">(또는 리셋)</text>
  <path d="M170 65 L190 65 M350 65 L370 65 M540 65 L560 65 M645 96 L645 130 M560 161 L540 161 M370 161 L350 161 M190 161 L170 161" stroke="currentColor" fill="none" stroke-width="1.5"/>
  <text x="10" y="222" fill="currentColor">①②③ 구간의 코드는 레지스터와 스택만 쓸 수 있다: 전역/정적 변수는 아직 "약속된 값" 이 아니다.</text>
  <text x="10" y="240" fill="currentColor">스택은 하드웨어가 이미 MSP 로 잡아 두었으므로 Cortex-M 에서는 Reset_Handler 를 C 로 쓸 수 있다.</text>
</svg>
```

STM32Cube 의 GCC 스타트업(`startup_stm32f4xx.s`)도 대략 `ldr sp, =_estack → bl SystemInit → .data 복사 루프 → .bss 0 루프 → bl __libc_init_array → bl main` 모양이다 (Cube 버전에 따라 SystemInit 이 복사 앞/뒤 — 확인 권장). `ldr sp, =_estack` 는 하드웨어가 이미 한 일을 다시 하는 것인데, 부트로더나 디버거가 vector[0] 을 거치지 않고 Reset_Handler 로 점프하는 경우를 대비한 것이다 (BM-A03).

### Part A — 세 루프와 순서

| 단계 | 규칙 | 코드 |
|---|---|---|
| ① 복사 | `[_sdata, _edata)` 의 각 워드 ← `_sidata` 부터 같은 위치 | `while (dst < dst_end) *dst++ = *src++;` |
| ② 제로 | `[_sbss, _ebss)` 의 각 워드 ← 0 | `while (dst < dst_end) *dst++ = 0u;` |
| ③ ctor | `__init_array_start[i]()` for i = 0 .. n−1 | `for (; f < end; f++) (*f)();` |

모든 범위는 **반열림 구간 [시작, 끝)** 이다. 링커 심볼 `_edata` 는 ".data 의 마지막 바이트 다음 주소" 이므로 `<` 로 비교하면 길이 0 도 자연히 처리된다.

`./solution -v` 의 실제 덤프 (RAM 을 0xCC 로 채우고 canary 를 놓은 뒤 `reset_init`):

| 주소 | 워드 | 영역 | 전 | 후 |
|---|---|---|---|---|
| 0x2000000C | w3 | canary_lo | 0xA5A5A5A5 | 0xA5A5A5A5 |
| 0x20000010 | w4 | .data `g_baud` | 0xCCCCCCCC | 0x0001C200 (115200) |
| 0x20000014 | w5 | .data `g_led_pin` | 0xCCCCCCCC | 0x00000005 |
| 0x20000018 | w6 | .data `g_name[0..3]` | 0xCCCCCCCC | 0x422D4D42 ("BM-B", 리틀 엔디언) |
| 0x2000001C | w7 | .data `g_name[4..7]` | 0xCCCCCCCC | 0x00003730 ("07\0\0") |
| 0x20000020 | w8 | .data `g_mode` | 0xCCCCCCCC | 0x00000002 |
| 0x20000024 | w9..w12 | .bss | 0xCCCCCCCC ×4 | 0x00000000 ×4 |
| 0x20000034 | w13 | canary_hi | 0x5A5A5A5A | 0x5A5A5A5A |
| 0x20000038 | w14..15 | .noinit | 0xCCCCCCCC | 0xCCCCCCCC (그대로) |

검증은 하네스의 `ram_diff()` 가 한다: 리셋 전 RAM 스냅샷에 "복사 구간 = Flash 바이트, 제로 구간 = 0" 을 덮어 기대 이미지를 만들고 **128 바이트 전부** 를 비교한다. 그래서 canary 몇 개만 보는 것보다 강하다 — 어디를 한 바이트라도 더/덜 썼으면 잡힌다. 전수 테스트는 시작 워드 2..4 × .data 0..8 워드 × .bss 0..8 워드 = **243 레이아웃**, 매번 난수 내용과 난수 RAM 으로 돌려 실패 0.

**순서가 정답의 일부인 이유.** `probe_read` constructor 는 호출되는 순간의 `ram[W_SDATA]` 와 `ram[W_SBSS]` 를 기록한다. 올바른 `reset_init` 에서는 115200 과 0 이다. constructor 가 먼저 돌면 0xCCCCCCCC 두 개를 본다. 즉 "constructor 는 main 과 같은 세상을 본다" 를 보장하는 것이 crt0 의 일이다.

### Part B — 4의 배수가 아닐 때

**실무 규칙: 경계를 4의 배수로 만드는 것은 링커 스크립트의 일이다.** 보통 이렇게 쓴다 (ST CubeIDE 가 생성하는 스크립트와 같은 모양, 자세한 문법은 TOOL-M01):

```text
  _sidata = LOADADDR(.data);
  .data : {
    . = ALIGN(4);  _sdata = .;
    *(.data) *(.data*)
    . = ALIGN(4);  _edata = .;      /* ← 이 줄이 워드 루프를 "정확하게" 만든다 */
  } >RAM AT> FLASH
  .bss : {
    . = ALIGN(4);  _sbss = .;
    *(.bss) *(.bss*) *(COMMON)
    . = ALIGN(4);  _ebss = .;
  } >RAM
```

`ALIGN(4)` 가 있으면 길이가 항상 4의 배수라 Part A 의 워드 루프가 딱 맞는다. 빠지면? 워드 루프 `for (p = &_sdata; p < &_edata;) *p++ = *s++;` 는 마지막 워드를 **통째로** 쓰므로 길이를 4의 배수로 **올림** 해서 옮긴다. 컴파일러도 정확히 그렇게 번역한다 — `demo_target_crt0.c` 를 VM 의 arm-none-eabi-gcc 14.2.1 `-O2 -fno-tree-loop-distribute-patterns` 로 컴파일한 실제 출력 (`@` 뒤 설명은 덧붙인 것; 바로 앞에서 r2 = `&_sdata`, r1 = `&_edata`, r0 = `&_sidata`):

```text
   c:	subs	r1, #1                 @ r1 = _edata - 1
  10:	subs	r1, r1, r2             @      - _sdata
  12:	bic.w	r1, r1, #3             @ & ~3
  16:	adds	r1, #4                 @ + 4   → 바이트 수 = ((end-1-start) & ~3) + 4 = 올림(길이, 4)
  18:	add	r1, r0
  1a:	ldr.w	r0, [r3], #4           @ 워드 읽기 (post-increment)
  1e:	cmp	r3, r1
  20:	str.w	r0, [r2], #4           @ 워드 쓰기
  24:	bne.n	1a <Reset_Handler+0x1a>
```

하네스의 두 버그 모델을 같은 레이아웃(.data 5 B @ 바이트 16, .bss 5 B @ 바이트 21, 나머지 0xCC)에 돌린 실제 결과:

| 모델 | .data 가 쓴 바이트 | .bss 가 쓴 바이트 | `ram_diff` | 무엇이 망가졌나 |
|---|---|---|---|---|
| 정답 (head/body/tail) | 16..20 | 21..25 | 0 | — |
| 올림 (`p < &_edata` 워드 루프) | 16..23 | 21..28 | **3** | 26..28 (= `_ebss` 뒤, `.noinit` / 힙 시작)이 0 으로 덮임 |
| 내림 (`count = len / 4`) | 16..19 | 21..24 | **2** | 바이트 20 (.data 마지막) · 25 (.bss 마지막)가 쓰레기 0xCC |

흥미로운 점: 올림 모델에서 **.data 의 넘침(21..23)은 보이지 않는다** — 바로 뒤의 .bss 제로가 덮어 주기 때문이다. 진짜 피해는 .bss 의 넘침이 `_ebss` 너머를 지우는 것이다. `.noinit` 크래시 기록(BM-A09)이나 부트로더↔앱 공유 영역(BM-B06)이 거기 있으면 "가끔 리셋 이유가 사라진다" 는 버그가 된다.

바이트 꼬리 함수의 분해 — `copy_bytes_tail(ram+17, ram+30, flash+161)` (테스트 그대로, 반환값 2):

```svg
<svg viewBox="0 0 740 160" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">head / body / tail — dst 바이트 17..29 (13 B), src 도 어긋남 1 (161) → 2 워드</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="40" height="34" fill="none"/>
    <rect x="60" y="40" width="40" height="34" class="f-warn"/><rect x="100" y="40" width="40" height="34" class="f-warn"/><rect x="140" y="40" width="40" height="34" class="f-warn"/>
    <rect x="180" y="40" width="160" height="34" class="f-acc"/>
    <rect x="340" y="40" width="160" height="34" class="f-acc"/>
    <rect x="500" y="40" width="40" height="34" class="f-ok"/><rect x="540" y="40" width="40" height="34" class="f-ok"/>
    <rect x="580" y="40" width="40" height="34" fill="none"/><rect x="620" y="40" width="40" height="34" fill="none"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="40" y="62">16</text><text x="80" y="62">17</text><text x="120" y="62">18</text><text x="160" y="62">19</text>
    <text x="260" y="62">20..23 (STR)</text><text x="420" y="62">24..27 (STR)</text>
    <text x="520" y="62">28</text><text x="560" y="62">29</text><text x="600" y="62">30</text><text x="640" y="62">31</text>
  </g>
  <g fill="currentColor">
    <text x="62" y="96">head 3 B (STRB)</text>
    <text x="250" y="96">body: 워드 2개 — dst, src 둘 다 4-정렬</text>
    <text x="500" y="96">tail 2 B</text>
    <text x="582" y="96">dst_end=30</text>
    <text x="20" y="124">src 를 161 대신 160 에서 읽으면 (어긋남이 다름) dst 를 맞추는 순간 src 가 어긋난다 → 13 B 전부 바이트, 반환 0.</text>
    <text x="20" y="144">정렬된 경우 (16..28, src 160) → body 3 워드 + tail 1 B, 반환 3.  길이 0 → 반환 0, 메모리 불변.</text>
  </g>
</svg>
```

왜 굳이 워드로? Cortex-M3/M4 에서 바이트 루프는 바이트당 LDRB+STRB 한 쌍, 워드 루프는 4 바이트당 한 쌍이라 명령 수가 대략 1/4 이다. 그리고 Cortex-M0/M0+ (ARMv6-M) 는 **정렬 안 된 워드 접근이 HardFault** 다 — "어긋난 주소에 그냥 LDR 하면 되지" 는 M0 에서 부팅 자체를 죽인다. M3/M4 는 일반 메모리에 대한 비정렬 LDR/STR 을 허용하지만 (CCR.UNALIGN_TRP 가 0 일 때) 느리다. 실무 정답은 그래서 여전히 "링커 스크립트로 정렬을 보장하고 워드 루프" 이고, 바이트 꼬리 변형은 경계를 통제할 수 없을 때(다른 사람이 만든 스크립트, 여러 메모리 영역 표)의 안전망이다.

전수 테스트: dst 정렬 0..3 × src 정렬 0..3 × .data 0..13 B × .bss 0..13 B(바이트 단위로 바로 뒤) = **3136 레이아웃**, 실패 0.

### Part C — 콜드 부트, 웜 리셋, 그리고 버그 부트

`fw_boot()` 는 `reset_init(&FW_SYMS); return fw_main();` 두 줄이다. `fw_main` 은 부팅 직후 상태를 검사해 오류 비트를 돌려준다 (bit0 .data 정수, bit1 .data 문자열, bit2 `g_uart_div == 139`, bit3 .bss == 0). 그 다음 "앱이 돈 흔적" 으로 `g_baud = 9600`, `g_ticks += 1234`, `g_rx_count = 7` 을 남기고, `.noinit` 의 리셋 카운터를 올린다.

`ctor_uart` 의 계산: `(16000000 + 115200/2) / 115200 = 16057600 / 115200 = 139` (139.38 의 반올림). 이 값이 맞으려면 ① `g_baud` 가 복사돼 있어야 하고 ② `g_core_hz` 가 0 으로 지워진 **뒤에** `ctor_clock` 이 16 MHz 를 써 넣어야 하며 ③ `ctor_clock` 이 `ctor_uart` 보다 **먼저** 불려야 한다.

| 시나리오 (테스트 순서대로) | RAM 상태 | 결과 (실측, `./solution -v`) |
|---|---|---|
| 정답 콜드 부트 | 난수 쓰레기 | err 0, log "CUL", div 139, 리셋 카운트 1 |
| 정답 웜 리셋 | 앞 실행이 남긴 9600 / 1234 / 7 | err 0, log "CUL", seen baud 115200, 리셋 카운트 2 (`.noinit` 생존) |
| constructor 먼저 | 웜 (g_baud 에 9600 남음) | **err 0x4**, ctor 가 본 baud = **9600**, 이후 .bss 제로가 `g_core_hz` 를 지워 0, div 0 |
| .bss 제로 누락 | 전부 0 인 RAM (콜드) | **err 0x0 — 버그가 숨는다** |
| .bss 제로 누락 | 이어서 웜 리셋 | **err 0x8** (`g_ticks` 가 1234 인 채 main 진입; 덤프는 main 이 한 번 더 더한 뒤라 2468) |
| init_array 순서 U,C,L | 난수 | **err 0x4**, log "UCL", ctor_uart 가 본 core_hz = 0 → div 0 |

두 번째 줄의 교훈이 브링업의 함정이다. constructor-먼저 버그는 **이전 실행의 값(9600)** 을 읽는다 — 그래서 "가끔 첫 부팅만 baud 가 이상하다" 가 아니라 "리셋할 때마다 직전 설정을 따라간다" 처럼 보인다. .bss 누락은 RAM 이 우연히 0 이면(어떤 디버거 스크립트·시뮬레이터는 RAM 을 0 으로 채운다) 테스트를 통과하고, **워치독/소프트 리셋** 처럼 RAM 이 유지되는 리셋에서만 드러난다. SSD 펌웨어에서 "콜드 부트는 늘 정상인데 WDT 리셋 뒤 카운터가 이어서 센다" 같은 증상이 정확히 이것이다. 그래서 테스트는 RAM 을 일부러 0 이 아닌 값(난수, 0xCC)으로 채우고, 웜 리셋을 꼭 한 번 더 돌린다.

마지막 줄은 C++ 의 정적 초기화 순서 문제(SIOF, CPP-B09)의 C 버전이다: 다른 파일의 constructor 사이 순서는 **링크 순서** 가 정한다. 의존이 있으면 constructor 에 기대지 말고 main 에서 명시적으로 `clock_init(); uart_init();` 하는 것이 펌웨어의 정석이다.

### 왜 Reset_Handler 는 "조심스러운 C" 인가

1. **전역/정적 변수 금지.** `reset_init` 안에서 `static uint32_t copied;` 를 쓰면 그 변수는 .bss 에 산다 — 자기가 지우고 있는 영역 안에서 카운트를 세는 셈이다. 전역 표(`.data`)를 읽으면 아직 복사 전 쓰레기다. 지역 변수(레지스터/스택)와 링커 심볼 주소만 쓴다. CMSIS `SystemInit()` 이 복사 전에 불리는 스타트업이라면 SystemInit 도 같은 규칙을 지켜야 한다 (`SystemCoreClock` 같은 전역은 main 의 `SystemCoreClockUpdate()` 에서 갱신).
2. **스택은 준비돼 있다 (Cortex-M 한정).** 하드웨어가 vector[0] 을 MSP 에 넣으므로 첫 명령부터 C 함수 호출이 된다. RISC-V 나 Cortex-A/R 은 그렇지 않아 첫 몇 줄이 어셈블리(`la sp, _estack`)여야 한다. 스택이나 .data 가 외부 SDRAM(FMC)에 있으면 복사 **전에** 메모리 컨트롤러를 켜야 한다 (ST 의 `SystemInit_ExtMemCtl` 류, 확인 권장).
3. **컴파일러가 루프를 `memcpy`/`memset` 으로 바꾼다.** 같은 `demo_target_crt0.c` 를 그냥 `-O2` 로:

```text
$ arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -std=c11 -O2 -Wall -Wextra -Werror -c demo_target_crt0.c
$ arm-none-eabi-objdump -dr --no-show-raw-insn demo_target_crt0.o
   e:	bic.w	r2, r2, #3
  12:	mov	r0, r3
  14:	ldr	r1, [pc, #64]	@ (58 <Reset_Handler+0x58>)
  16:	adds	r2, #4
  18:	bl	0 <memcpy>
			18: R_ARM_THM_CALL	memcpy
  ...
  2c:	movs	r1, #0
  2e:	mov	r0, r3
  30:	adds	r2, #4
  32:	bl	0 <memset>
			32: R_ARM_THM_CALL	memset
```

`-nostdlib` 로 링크하면 (심볼은 `--defsym` 으로 줌, 한 줄짜리 `main` 추가) 실제 오류 (ld 경로만 `ld` 로 줄임):

```text
ld: /tmp/ccoQq9hK.o: in function `Reset_Handler':
demo_target_crt0.c:(.text+0x18): undefined reference to `memcpy'
ld: (memcpy): Unknown destination type (ARM/Thumb) in /tmp/ccoQq9hK.o
demo_target_crt0.c:(.text+0x18): dangerous relocation: unsupported relocation
ld: demo_target_crt0.c:(.text+0x32): undefined reference to `memset'
...
collect2: error: ld returned 1 exit status
```

실측 비교 (같은 소스):

| 컴파일 | 루프가 된 것 | `.text` |
|---|---|---|
| gcc 14.2.1 `-O2` | `bl memcpy`, `bl memset` | 108 B |
| gcc `-O2 -ffreestanding` | 직접 루프 (`ldr.w/str.w ... #4`) | 124 B |
| gcc `-O2 -fno-tree-loop-distribute-patterns` | 직접 루프 | 124 B (링크 성공, ELF 128 B) |
| Apple clang 21 `--target=thumbv7em-none-eabi -O2` | `bl __aeabi_memcpy4`, `bl __aeabi_memclr4` | — |
| clang `-O2 -ffreestanding` | 직접 루프 (4 배 언롤) | — |

newlib 이 링크되는 보통 빌드라면 `memcpy` 호출 자체는 동작한다 (newlib 의 memcpy 는 전역 상태를 쓰지 않는다). 문제는 `-nostdlib` 최소 빌드(BM-B08)와, "Reset_Handler 는 아무 것에도 기대지 않는다" 는 가정이 깨진다는 점이다. 그래서 BM 트랙의 VM 빌드는 `-ffreestanding -nostdlib -fno-tree-loop-distribute-patterns` 를 쓴다. `__aeabi_memcpy4` 는 ARM EABI 런타임 헬퍼(정렬 4 보장 버전)로, 이것도 런타임 라이브러리가 있어야 링크된다.

4. **포인터 비교의 작은 거짓말.** `&_sdata < &_edata` 는 C11 6.5.8p5 기준 **서로 다른 객체의 포인터 관계 비교** 라 엄밀히는 정의되지 않은 동작이다. 실무에서는 모든 컴파일러가 주소 비교로 번역하고 (위 asm), 걱정되면 `uintptr_t` 로 바꾸거나 `extern uint32_t _sdata[];` 처럼 배열로 선언한다. 호스트 시뮬레이션은 모든 심볼이 같은 `ram[]` 배열 안을 가리키므로 이 문제가 없다 — 그래서 ASan/UBSan 이 깨끗하다.

### 호스트 모델 ↔ 실제 타깃

| 이 레슨 | 실제 Cortex-M 펌웨어 |
|---|---|
| `crt_syms_t` 포인터 묶음 | `extern uint32_t _sidata, _sdata, ...;` + `&` (`demo_target_crt0.c`) |
| `flash_program_image()` | 링커가 .data 초기값을 `AT> FLASH` 로 배치, 프로그래머가 굽는다 |
| `sim_power_on(0/1/2)` | 전원 투입 SRAM (값 불명), 디버거가 0 으로 채운 RAM, 웜 리셋 RAM |
| `fw_boot()` | `Reset_Handler` (vector[1]) — BM-B08 에서 QEMU mps2-an385 로 진짜 부팅 |
| `G_BAUD` = `ram[4]` | 링커가 정한 주소의 `uint32_t g_baud = 115200;` |
| `FW_INIT[]` | `.init_array` 섹션 (Flash, `KEEP(*(.init_array*))`) |
| `W_NOINIT` 영역 | `(NOLOAD)` 섹션 `.noinit` — crt0 범위 밖 (BM-B06, BM-A09) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 40/40 passed
./solution -v                                                                  # RAM 덤프 + 버그 부트 표
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_san && ./sol_san   # 40/40, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `while (dst != &_edata)` | 끝이 4의 배수가 아니면 영영 같아지지 않아 RAM 전체·스택까지 덮는다 | `while (dst < &_edata)` |
| `uint32_t *p = (uint32_t *)_sdata;` | `_sdata` 의 **값** (그 주소의 RAM 쓰레기)을 주소로 씀 | `&_sdata` (심볼은 주소만 의미가 있다) |
| constructor 를 복사/제로 전에 호출 | ctor 가 쓰레기·직전 실행 값을 읽고, .bss 에 쓴 결과는 지워진다 (Part C: div 0, seen 9600) | 복사 → 제로 → `__libc_init_array()` |
| .bss 제로를 "RAM 은 원래 0" 이라며 생략 | 전원 투입 SRAM 값은 불명, 웜 리셋 RAM 은 이전 값 그대로 (Part C: 콜드 통과 / 웜 실패) | 항상 제로 루프 |
| 링커 스크립트에서 `. = ALIGN(4);` 생략 + 워드 루프 | 올림 → `_ebss` 뒤 최대 3 바이트 파괴 / 내림 → 마지막 바이트 쓰레기 | 스크립트에 `ALIGN(4)` (또는 바이트 꼬리 변형) |
| Reset_Handler 안에서 `static` 변수나 전역 표 사용 | 아직 초기화 전 — .bss 변수는 자기 루프가 지운다 | 지역 변수와 링커 심볼만 |
| `-O2` 로 `-nostdlib` 빌드 | GCC 가 루프를 `memcpy`/`memset` 으로 바꿔 undefined reference | `-ffreestanding` 또는 `-fno-tree-loop-distribute-patterns` |
| `.noinit` 을 .bss 범위 안에 둠 | crt0 가 매 리셋마다 지워 리셋 이유·크래시 기록이 사라짐 (연습 4) | `(NOLOAD)` 섹션을 `_ebss` 밖에 |
| M0/M0+ 에서 비정렬 주소에 워드 LDR/STR | ARMv6-M 은 비정렬 접근이 HardFault | 정렬 보장 또는 바이트 head/tail |
| `.init_array` 에 `KEEP` 없음 + `--gc-sections` | 아무도 참조 안 하는 섹션이라 버려져 constructor 가 조용히 사라짐 (CPP-B09) | `KEEP(*(.init_array*))` |

## 8. 면접 Q&A

<details>
<summary>Q1. 전원을 켜면 main() 까지 무슨 일이 일어나나요?</summary>

Cortex-M 기준: ① 하드웨어가 벡터 테이블(기본 0x00000000, STM32 는 Flash 0x08000000 이 별칭 매핑)의 워드 0 을 MSP 로, 워드 1 을 PC 로 읽는다. 워드 1 은 Thumb 비트가 1 인 주소(이 레슨 0x08000041)여야 한다. ② `Reset_Handler` 가 (CMSIS 스타트업이면) `SystemInit()` 으로 클럭·FPU·VTOR 를 만지고 ③ `.data` 를 Flash(LMA)에서 RAM(VMA)으로 복사 ④ `.bss` 를 0 으로 ⑤ `__libc_init_array()` 로 preinit/init 배열(C++ 전역 생성자 등)을 호출 ⑥ `main()`. main 이 돌아오면 무한 루프. 꼬리: "누가 SP 를 설정하나?" → Cortex-M 은 하드웨어(vector[0]). 그래서 Reset_Handler 를 C 로 쓸 수 있다. RISC-V/Cortex-A 는 어셈블리로 sp 부터 잡는다.

</details>

<details>
<summary>Q2. .data 는 왜 Flash 와 RAM 을 둘 다 쓰나요? .bss 와 const 는?</summary>

`.data` 변수는 실행 중 바뀌므로 RAM 에 있어야 하고(VMA), 전원이 꺼져도 초기값을 기억해야 하므로 Flash 에도 사본이 있다(LMA). 그래서 5 워드 `.data` = Flash 20 B + RAM 20 B. `.bss` 는 초기값이 전부 0 이라 크기만 알면 되므로 RAM 만 쓴다 (Flash 비용은 제로 루프 몇 바이트). `const` 표는 `.rodata` 로 Flash 에만 있다 — 큰 룩업 테이블에 `const` 를 빼먹으면 Flash 와 RAM 을 둘 다 먹는다 (BM-B05 의 map 파일 실습). 꼬리: "`int x = 0;` 은 어디로?" → GCC 는 0 초기화 전역을 `.bss` 로 보낸다 (`-fno-zero-initialized-in-bss` 로 바꿀 수 있음).

</details>

<details>
<summary>Q3. 벤치에서는 잘 되는데 워치독 리셋 뒤에만 전역 변수가 이상합니다. 어디부터 보나요?</summary>

리셋 종류에 따라 RAM 상태가 다르다는 것부터. 전원 투입 RAM 은 값 불명(종종 비슷한 패턴), 디버거 스크립트가 0 으로 채운 RAM, 워치독/소프트 리셋 뒤 RAM 은 **직전 실행 값 그대로**. 커스텀 스타트업이 .bss 제로를 빠뜨렸거나 범위가 틀렸으면 0 RAM 에서는 숨고 웜 리셋에서 드러난다 (Part C: 콜드 err 0, 웜 err 0x8). constructor 가 복사 전에 돌면 "직전 설정값" 을 읽는다 (seen baud 9600). 확인: map 파일에서 `_sbss/_ebss` 가 실제 `.bss` 를 다 덮는지, 리셋 직후 브레이크포인트에서 `x/8wx &_sbss` 를 보고, 테스트에서 RAM 을 일부러 0xCC 로 채운다. 반대 경우도 있다: 리셋을 넘어 살아야 할 `.noinit` 이 .bss 안에 들어가 매번 지워지는 것 (연습 4).

</details>

<details>
<summary>Q4. Reset_Handler 를 C 로 써도 되나요? 무엇을 조심하죠?</summary>

Cortex-M 에서는 된다 (스택이 이미 있음. CMSIS 디바이스 템플릿의 `startup_<device>.c` 도 C 다 — 확인 권장). 조심할 것: ① 전역/정적 변수를 쓰지 않는다 — 아직 초기화 전이고, .bss 변수는 자기 루프가 지운다. ② 라이브러리 호출에 기대지 않는다 — GCC `-O2` 는 루프를 `memcpy`/`memset` 호출로 바꾸고(실측: `bl memcpy`, `bl memset`), clang 은 `__aeabi_memcpy4`/`__aeabi_memclr4` 로 바꾼다. `-nostdlib` 이면 링크 실패. `-ffreestanding` 또는 `-fno-tree-loop-distribute-patterns`. ③ 스택/데이터가 외부 SDRAM 이면 컨트롤러 초기화가 복사보다 먼저. ④ 이 함수는 리턴하지 않으므로 main 이 돌아오면 멈출 곳(무한 루프)을 둔다. 꼬리: "왜 ST 는 어셈블리로?" → 역사적 이유와 툴체인 독립성, 그리고 `ldr sp, =_estack` 처럼 C 로 못 쓰는 한 줄 때문.

</details>

<details>
<summary>Q5. .data 길이가 4의 배수가 아니면 워드 복사는 어떻게 되나요?</summary>

실무 답: 링커 스크립트가 `. = ALIGN(4);` 로 `_sdata/_edata/_sbss/_ebss` 를 전부 4의 배수로 맞추므로 그런 일이 없게 만든다. 맞추지 않으면 `p < &_edata` 워드 루프는 길이를 올림해서 쓴다 — GCC asm 에 `((end-1-start) & ~3) + 4` 가 그대로 보인다. .data 의 넘침은 뒤따르는 .bss 제로가 덮어 주지만, .bss 의 넘침은 `_ebss` 뒤(.noinit, 힙)를 최대 3 바이트 지운다. 경계를 통제할 수 없으면 head 바이트 → 워드 본체 → tail 바이트 (src 와 dst 의 어긋남이 다르면 전부 바이트). 그리고 M0/M0+ 는 비정렬 워드 접근이 HardFault 라 "그냥 비정렬 LDR" 은 답이 아니다.

</details>

<details>
<summary>Q6. __libc_init_array 는 C 프로그램에도 필요한가요? 무엇을 부르죠?</summary>

newlib 의 `__libc_init_array()` 는 `__preinit_array` 전부 → `_init()` (crti/crtn) → `__init_array` 전부를 차례로 부른다. C++ 전역 객체 생성자, `__attribute__((constructor))` 함수, 일부 라이브러리 초기화가 여기 있다. 순수 C 이고 그런 것이 없으면 배열이 비어(start == end) 호출이 0 번이라 빼도 동작은 같지만, 나중에 C++ 파일 하나가 들어오면 생성자가 조용히 안 돈다 — 그래서 넣어 둔다. 순서: 같은 파일 안은 정의 순서, 파일 사이는 링크 순서, `constructor(N)` 우선순위가 있으면 `.init_array.00101` 같은 섹션 이름으로 정렬돼 앞에 온다 (연습 3, 101 미만은 구현 예약). 의존이 있는 초기화는 constructor 에 맡기지 말고 main 에서 명시적으로 (Part C 마지막 버그, CPP-B09 SIOF).

</details>

<details>
<summary>Q7. 왜 <code>_sdata</code> 가 아니라 <code>&amp;_sdata</code> 인가요?</summary>

링커 스크립트의 `_sdata = .;` 는 "심볼 `_sdata` 의 **주소** 를 현재 위치로 정한다" 는 뜻이다. 저장 공간이 따로 있는 변수가 아니다. C 에서 `extern uint32_t _sdata;` 로 선언하면 컴파일러는 "그 주소에 uint32_t 가 있다" 고 믿으므로, `_sdata` 라고 쓰면 0x20000010 의 RAM 값(쓰레기)을 읽고, `&_sdata` 라고 써야 0x20000010 을 얻는다. 그래서 `extern uint32_t _sdata[];` 처럼 배열로 선언해 이름 자체가 주소가 되게 하는 스타일도 많다. 꼬리: "`&_edata - &_sdata` 를 바이트 수로 쓰면?" → `uint32_t *` 끼리 빼면 **워드 수** 다. 이것을 바이트 수로 착각하거나 반대로 바이트 수를 워드 루프 카운트로 쓰면 4 배 넘치거나 1/4 만 복사한다.

</details>

<details>
<summary>Q8. 이 코드를 어떻게 테스트하나요? 실칩에서는요?</summary>

호스트: 이 레슨처럼 가짜 Flash/RAM 배열과 심볼 포인터로 crt0 를 단위 테스트 — RAM 을 0 이 아닌 패턴으로 채우고, 경계 바깥 canary 와 전체 스냅샷 비교, 길이 0·홀수·정렬 경계 전수, constructor 순서 probe, 웜 리셋 재실행. 타깃: QEMU 에서 main 이 .data 초기값과 .bss 0 을 스스로 검사해 semihosting 으로 보고 (BM-B08), gdb 로 `Reset_Handler` 에 브레이크 걸고 `.data` 변수를 복사 전/후로 보기. 양산 보드에서는 리셋 직후 GPIO 토글로 startup 시간을 스코프로 재고 (큰 .bss 는 수 ms 가 될 수 있다 — 워치독 첫 킥 타이밍에 영향), 디버거 없이 워치독 리셋을 반복하는 soak 테스트를 돌린다.

</details>

## 9. 추가 연습

### 연습 1 — 표(table) 기반 초기화: 여러 RAM 영역

STM32F405/407 에는 일반 SRAM 외에 CCM RAM (0x10000000, 64 KB — RM0090 기준, 확인 권장)이 있다. `.data`/`.bss` 와 별도로 `.ccmdata`/`.ccmbss` 를 두려면 루프가 두 벌 필요하다. CMSIS GCC 스타트업처럼 `{src, dest, wlen}` copy 표와 `{dest, wlen}` zero 표를 받아 한 함수로 처리하라. 빈 표·길이 0 항목도 테스트.

<details>
<summary>답 보기</summary>

```c
typedef struct { const uint32_t *src; uint32_t *dest; uint32_t wlen; } copy_table_t;
typedef struct { uint32_t *dest; uint32_t wlen; } zero_table_t;
for (; c < c_end; c++) for (uint32_t i = 0; i < c->wlen; i++) c->dest[i] = c->src[i];
for (; z < z_end; z++) for (uint32_t i = 0; i < z->wlen; i++) z->dest[i] = 0u;
```

링커 스크립트가 `__copy_table_start__ … __copy_table_end__` 사이에 `LONG(LOADADDR(.data)) LONG(ADDR(.data)) LONG(SIZEOF(.data)/4)` 같은 항목을 영역마다 내보낸다 (CMSIS 5 `cmsis_gcc.h` 의 `__cmsis_start()` 와 Arm 예제 스크립트 모양 — 버전에 따라 다름, 확인 권장). 장점: 영역이 늘어도 startup 코드는 그대로, 링커 스크립트만 바뀐다. ITCM 으로 복사하는 "RAM 에서 도는 함수" (`.ramfunc`, Flash 쓰기 중 실행용)도 같은 표에 한 줄 추가로 된다. 길이가 `wlen` (워드)이라 `ALIGN(4)` 전제가 표 형식에 박혀 있다.

</details>

### 연습 2 — 링커 심볼 값 검사기 (ASSERT 로 옮길 규칙)

map 파일이나 `arm-none-eabi-nm` 에서 읽은 심볼 값(주소 숫자)으로 레이아웃을 검사하는 `layout_check()` 를 작성하라: 정렬 4, `sdata ≤ edata`, `sbss ≤ ebss`, `.data` 와 `.bss` 가 겹치지 않음, RAM 범위 안, `.data` 초기값이 Flash 안 (덧셈 오버플로 없이), `_estack` = RAM 끝이고 `.bss` 뒤에 최소 스택 여유. 1 MB Flash / 128 KB RAM / 최소 스택 4 KB 로 테스트.

<details>
<summary>답 보기</summary>

첫 번째로 실패한 규칙을 돌려준다: ALIGN → ORDER → OVERLAP → RAM_RANGE → FLASH_RANGE → STACK. Flash 범위는 `sidata + dlen > flash_hi` 대신 `dlen > flash_hi - sidata` 로 비교해야 `sidata = 0xFFFFFFF0` 같은 값에서 랩어라운드하지 않는다 (테스트 포함). 같은 규칙을 링커 스크립트에 넣으면 빌드가 실패한다:

```text
ASSERT((_sdata % 4) == 0 && (_edata % 4) == 0, "data not word aligned")
ASSERT(_edata <= _sbss, ".data/.bss overlap")
ASSERT(_estack - _ebss >= 0x1000, "less than 4 KB left for the stack")
```

ASSERT 의 실제 오류 메시지와 더 큰 레이아웃 검사기는 BM-B06, 링커 스크립트 문법은 TOOL-M01.

</details>

### 연습 3 — constructor 우선순위와 fini 순서

`{a 무순위, b 200, c 101, d 무순위, e 65535}` 가 이 순서로 링크될 때 `.init_array` 의 최종 순서는? `__libc_init_array` (preinit `p`, `_init` = `I`) → main (`M`) → `__libc_fini_array` (fini 배열 `x, y`, `_fini` = `F`)의 전체 호출 순서는?

<details>
<summary>답 보기</summary>

GNU ld 기본 스크립트는 `KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))` 를 먼저, `KEEP(*(.init_array))` 를 나중에 둔다. `constructor(N)` 은 `.init_array.00101` 처럼 번호가 붙은 섹션으로 가서 숫자 오름차순 → **c(101) b(200) e(65535)**, 그 다음 무순위 a, d 를 링크 순서로 → `"cbead"`. 전체: `p I c b e a d M y x F` — fini 배열은 **역순** 으로 부르고 (`__libc_fini_array`: `for (i = count; i > 0; i--)`), 마지막에 `_fini()`. 베어메탈 펌웨어는 main 이 돌아오지 않으므로 fini 쪽은 보통 안 불린다 (그래서 소멸자 없는 전역이 권장된다 — CPP-B09 연습 5). 우선순위 0..100 은 구현(라이브러리) 예약이라 컴파일러가 경고한다. 섹션 이름 패딩·정렬 세부는 binutils 버전마다 다를 수 있다 (확인 권장).

</details>

### 연습 4 — .noinit 웜 부트 카운터

`.noinit` 에 `{magic, ~magic, boots, warm}` 을 두고, 부팅마다 콜드/웜을 판별해 센다. 전원 투입 → 웜 리셋 2번 → 전원 재투입 순서로 확인하고, `.noinit` 이 실수로 `.bss` 범위 안에 들어간 링커 스크립트(= `reset_init` 의 `ebss` 가 `.noinit` 을 덮음)에서는 어떻게 되는지 보여라.

<details>
<summary>답 보기</summary>

`if (magic == MAGIC && inv == ~MAGIC) { boots++; warm++; return WARM; } else { 초기화; boots = 1; return COLD; }`. 매직 하나만 보면 전원 투입 쓰레기가 우연히 같을 확률 2^-32, 반전 매직까지 보면 쓰레기 두 워드가 서로 반전 관계여야 해서 사실상 0 (그래도 CRC 를 붙이는 것이 BM-A09 의 크래시 덤프 방식). 결과: COLD(1) → WARM(2) → WARM(3, warm 2) → 전원 재투입 COLD(1). `.noinit` 이 `.bss` 안이면 crt0 가 매번 0 으로 지워 **항상 COLD** 다. 링커 스크립트에서는 `.noinit (NOLOAD) : { *(.noinit*) } >RAM` 을 `_ebss` 정의 **밖** 에 둔다. 또 하나: `.noinit` 변수에 초기값을 써도 crt0 가 그 값을 넣어 주지 않는다 (복사 범위 밖) — 초기화는 위의 콜드 부트 분기에서 코드로 한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 25/25 passed
```

## 10. 핵심 정리

- 리셋 → (HW) MSP ← vector[0], PC ← vector[1] → `Reset_Handler`: (SystemInit) → **.data 복사 → .bss 제로 → init_array → main** → 돌아오면 무한 루프.
- `.data` = Flash(LMA, `_sidata`) + RAM(VMA, `_sdata.._edata`) 두 벌, `.bss` = RAM 만, `const` = Flash 만. 링커 심볼은 주소 — `&_sdata`.
- 루프는 반열림 구간 `while (p < end)` — 길이 0 이 자동 처리되고 `!=` 의 폭주가 없다.
- 워드 루프가 정확하려면 링커 스크립트의 `. = ALIGN(4);` 가 필수. 없으면 올림(뒤 영역 최대 3 B 파괴, GCC asm `& ~3) + 4`) 또는 내림(마지막 바이트 쓰레기). 안전망 = head 바이트 → 워드 본체(둘 다 정렬일 때만) → tail 바이트. M0 는 비정렬 워드가 HardFault.
- 순서가 정답: constructor 는 복사된 .data 와 0 인 .bss 를 봐야 한다. 앞당기면 직전 실행 값(9600)을 읽고 결과는 .bss 제로에 지워진다.
- 0 으로 채워진 RAM 은 .bss 버그를 숨긴다 → 테스트는 RAM 을 0xCC/난수로 채우고 웜 리셋을 한 번 더. `.noinit` 은 `_ebss` 밖.
- Reset_Handler 는 전역 금지·라이브러리 금지. GCC `-O2` 는 루프를 `memcpy`/`memset` 으로 바꾼다 → `-nostdlib` 에서 undefined reference. `-ffreestanding` / `-fno-tree-loop-distribute-patterns`.

## 참고자료

- ARMv7-M Architecture Reference Manual (ARM DDI 0403E) — B1.5 예외 모델의 Reset behavior 절 (vector table 의 SP_main 초기값과 reset 벡터, Thumb 비트 — 절 번호 확인 권장) — <https://developer.arm.com/documentation/ddi0403/latest/>
- Arm Cortex-M4 Devices Generic User Guide (DUI 0553) — Vector table 절, Unaligned access support 절 (CCR.UNALIGN_TRP) — <https://developer.arm.com/documentation/dui0553/latest/>
- ARMv6-M Architecture Reference Manual (ARM DDI 0419) — Alignment support 절 (ARMv6-M 비정렬 접근 = fault)
- GCC Manual, Optimize Options — `-ftree-loop-distribute-patterns` (-O2 이상에서 기본), C Dialect Options — `-ffreestanding` — <https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html>
- GNU ld Manual — Output Section LMA (`AT>`), `LOADADDR`, `ALIGN`, `KEEP`, `SORT_BY_INIT_PRIORITY`, `ASSERT`, `NOLOAD` — <https://sourceware.org/binutils/docs/ld/>
- newlib `libc/misc/init.c` (`__libc_init_array`), `libc/misc/fini.c` (`__libc_fini_array`) — <https://sourceware.org/git/?p=newlib-cygwin.git>
- CMSIS-Core (Cortex-M) startup 문서와 `cmsis_gcc.h` 의 `__cmsis_start()` (copy/zero 표) — <https://arm-software.github.io/CMSIS_6/latest/Core/index.html>
- STM32CubeF4 `startup_stm32f4xx.s` (GCC) 와 CubeIDE 생성 `STM32F4xxxx_FLASH.ld` — <https://github.com/STMicroelectronics/STM32CubeF4>
- ISO/IEC 9899:2011 (C11) 6.5.8p5 관계 연산자의 포인터 비교, 6.7.9p10 정적 저장 기간 객체의 초기화 (0 초기화 규칙)
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* (3rd ed.) — 리셋 시퀀스, 스타트업 코드 장
- 관련 레슨: CPP-B09 (C++ 전역 생성자·SIOF·`__libc_init_array`), BM-B05 (map 파일·`.data` 의 Flash+RAM 비용), BM-B06 (.noinit·레이아웃 ASSERT), BM-B08 (QEMU 진짜 부팅, 이 reset_init 를 그대로 사용), TOOL-M01 (링커 스크립트 기초), ARM-M08 (asm 벡터 테이블과 Thumb 비트), BM-A01 (SystemInit 순서), BM-A03 (부트로더 → 앱 점프 시 MSP 재설정), BM-A09 (.noinit 크래시 덤프)
