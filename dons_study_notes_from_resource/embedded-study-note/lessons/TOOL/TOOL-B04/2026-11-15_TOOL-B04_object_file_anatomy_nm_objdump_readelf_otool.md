---
id: TOOL-B04
time: 60분
updated: 2026-11-15
---
# TOOL-B04 오브젝트 파일 해부: nm objdump readelf otool

## 학습 목표

- C 소스의 각 선언(전역 초기화/0/미초기화, `static`, static 지역, `const`, 문자열, `extern`, weak, `used`)이 오브젝트 파일(object file, `.o`)의 **어느 섹션 · 어떤 nm 글자**가 되는지 출력 없이 예측하고, `nm` / `nm -m` / `objdump -t` / `readelf -s` 로 확인한다.
- `.o` 안의 **리로케이션(relocation)** = "링커가 나중에 채울 구멍" 을 `objdump -d -r` 로 찾아, `ARM64_RELOC_BRANCH26` ↔ `R_AARCH64_CALL26`, `PAGE21/PAGEOFF12` ↔ `ADR_PREL_PG_HI21/…_LO12_NC`, GOT 계열이 각각 어떤 명령의 어떤 비트를 고치는지 말한다.
- 같은 소스의 **Mach-O(맥) ↔ ELF(리눅스·펌웨어)** 차이를 섹션 대응표로 설명한다: `__TEXT,__text`↔`.text`, `__TEXT,__cstring`↔`.rodata.str1.1`, `__DATA,__common`(섹션)과 `common`(심볼)의 차이, 맥 clang 은 아직 `-fcommon` 이라는 사실.
- Cortex-M4 오브젝트(`thumbv7em-none-eabi`)에서 GOT 가 사라지고 `R_ARM_THM_CALL` / `MOVW·MOVT` / 리터럴 풀 `R_ARM_ABS32` 가 나오는 이유, `REL` 과 `RELA` 의 차이를 실제 바이트(`f7ff fffe`)로 설명한다.

## 1. 문제

"이 `.o` 에 `nm` 돌려 보세요 — `T` `t` `W` `U` `C` 가 각각 뭐죠?", "링크 전 `bl` 명령은 어디로 점프하게 돼 있죠?" — 링크 에러(TOOL-B05), 맵 파일(TOOL-M02), 약한(weak) 인터럽트 핸들러 버그를 푸는 출발점이 전부 **`.o` 를 읽는 능력**이라서 펌웨어 면접에서 자주 확인하는 기본기다. SSD 컨트롤러 펌웨어처럼 수백 개 `.o` 를 링크하는 빌드에서 "이 심볼 누가 정의했어?", "왜 이 함수가 Flash 에 남아 있어?" 의 답은 거의 항상 `nm` 한 줄에 있다.

재료는 `anatomy.c` 하나다 — `main` 이 없는 "오브젝트 파일 표본". 링크하지 않고 `-c` 로만 만들어서 도구로 들여다본다.

| `anatomy.c` 의 선언 | 무엇을 보려고 넣었나 |
|---|---|
| `int g_init = 42;` / `int g_zero = 0;` / `int g_tent;` | 초기화 데이터 · 0 데이터 · **잠정 정의(tentative definition)** — common 인가 bss 인가 |
| `static int s_file = 7;` / `static int s_file_zero;` | 파일 static (로컬 심볼) — 그리고 `-O2` 에서 살아남는가 |
| `const int g_const = 100;` / `const char *g_msg = "hello";` | 읽기 전용 데이터, 문자열 리터럴, **데이터 안의 포인터 리로케이션** |
| `static int helper()` (noinline) / `unused_static()` / `__attribute__((used)) kept_static()` | 로컬 함수 · 안 쓰는 static 이 지워지는가 · `used` 가 막는가 |
| `__attribute__((weak)) int board_init()` | weak 정의 (펌웨어 `Default_Handler` 의 원형, TOOL-B05) |
| `static int s_local;` (함수 안) | static 지역 변수의 심볼 이름 |
| `extern int ext_counter;` / `extern void ext_log()` | 미해결 심볼 `U` 와 GOT/콜 리로케이션 |

```svg
<svg viewBox="0 0 740 380" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 anatomy.c (-O2) → 두 가지 오브젝트 포맷 (실측 수치)</text>
  <text x="20" y="46" fill="currentColor" font-weight="bold">ELF64 AArch64 (clang --target=aarch64-linux-gnu -c)</text>
  <text x="390" y="46" fill="currentColor" font-weight="bold">Mach-O arm64 (cc -c)</text>
  <rect x="20" y="56" width="340" height="44" class="f-mut" stroke="currentColor"/>
  <text x="30" y="74" fill="currentColor">ELF header 64 B: Type REL, Machine AArch64</text>
  <text x="30" y="92" fill="currentColor" font-family="monospace" font-size="12">e_shoff → 섹션 헤더 표 (15개)</text>
  <rect x="390" y="56" width="340" height="44" class="f-mut" stroke="currentColor"/>
  <text x="400" y="74" fill="currentColor">mach_header_64: MH_OBJECT, ARM64</text>
  <text x="400" y="92" fill="currentColor" font-family="monospace" font-size="12">flags SUBSECTIONS_VIA_SYMBOLS, 5 LCs</text>
  <rect x="20" y="110" width="340" height="88" class="f-acc" stroke="currentColor"/>
  <text x="30" y="128" fill="currentColor" font-weight="bold">섹션 (각자 주소 0 부터)</text>
  <text x="30" y="148" fill="currentColor" font-family="monospace" font-size="12">.text a4  .data 10  .bss c (NOBITS)</text>
  <text x="30" y="166" fill="currentColor" font-family="monospace" font-size="12">.rodata 4  .rodata.str1.1 10</text>
  <text x="30" y="184" fill="currentColor" font-family="monospace" font-size="12">.eh_frame 90  .comment ...</text>
  <rect x="390" y="110" width="340" height="88" class="f-acc" stroke="currentColor"/>
  <text x="400" y="128" fill="currentColor" font-weight="bold">LC_SEGMENT_64 (이름 없음) 안의 7 섹션</text>
  <text x="400" y="148" fill="currentColor" font-family="monospace" font-size="12">__TEXT,__text a8  __DATA,__data 10</text>
  <text x="400" y="166" fill="currentColor" font-family="monospace" font-size="12">__const 4 __cstring 10 __common 4</text>
  <text x="400" y="184" fill="currentColor" font-family="monospace" font-size="12">__bss 4  __LD,__compact_unwind a0</text>
  <rect x="20" y="208" width="340" height="62" class="f-ok" stroke="currentColor"/>
  <text x="30" y="226" fill="currentColor" font-weight="bold">.symtab 25개 + .strtab</text>
  <text x="30" y="246" fill="currentColor" font-size="12">Bind · Type · Ndx(섹션 번호 / UND)</text>
  <text x="30" y="262" fill="currentColor" font-size="12">로컬 데이터는 섹션 심볼(.bss, .rodata.str1.1)로 참조</text>
  <rect x="390" y="208" width="340" height="62" class="f-ok" stroke="currentColor"/>
  <text x="400" y="226" fill="currentColor" font-weight="bold">LC_SYMTAB 22개 + LC_DYSYMTAB</text>
  <text x="400" y="246" fill="currentColor" font-size="12">local 12 · extdef 7 · undef 3 (common 포함)</text>
  <text x="400" y="262" fill="currentColor" font-size="12">l_.str / ltmpN 같은 임시 심볼도 남음</text>
  <rect x="20" y="280" width="340" height="62" class="f-warn" stroke="currentColor"/>
  <text x="30" y="298" fill="currentColor" font-weight="bold">리로케이션 = 별도 섹션</text>
  <text x="30" y="318" fill="currentColor" font-family="monospace" font-size="12">.rela.text 15  .rela.data 1</text>
  <text x="30" y="334" fill="currentColor" font-size="12">RELA: (offset, type, symbol, addend)</text>
  <rect x="390" y="280" width="340" height="62" class="f-warn" stroke="currentColor"/>
  <text x="400" y="298" fill="currentColor" font-weight="bold">리로케이션 = 섹션 헤더의 reloff/nreloc</text>
  <text x="400" y="318" fill="currentColor" font-family="monospace" font-size="12">__text nreloc 16  __data nreloc 1</text>
  <text x="400" y="334" fill="currentColor" font-size="12">.rel* 라는 섹션 자체는 없음</text>
  <text x="20" y="368" fill="currentColor">네 층(헤더 · 섹션 · 심볼 · 리로케이션)은 같다. 이름과 담는 방식만 다르다.</text>
</svg>
```

### Part A — 심볼 표 예측 (`[symbols]`, 16줄)

`starter.txt` 의 `[symbols]` 표를 채운다. 각 심볼마다 **Mach-O nm 글자 · Mach-O 섹션(`nm -m` 의 괄호) · ELF nm 글자 · ELF 섹션(`objdump -t` 의 섹션 칸)**. 오브젝트에 없으면 `-`. 도구를 돌려 베끼기 전에 **먼저 예측**하고, 틀린 칸이 왜 틀렸는지를 5. 풀이와 맞춰 본다.

### Part B — 리로케이션 표 예측 (`[relocs]`, 11줄) — 함정 포함

각 참조(호출 `call:`, 변수 참조 `ref:`, 데이터 안의 포인터 `data:`)에 대해 **그 대상을 가리키는 리로케이션 타입 전부**를 쓴다. 리로케이션이 아예 없으면 `-`. 함정: 같은 파일 안의 static 함수 호출, `const` 전역 읽기, 잠정 정의 `g_tent` 는 Mach-O 와 ELF 가 **서로 다르게** 처리한다. `relsec:text` 는 코드의 리로케이션을 담는 섹션 이름.

### Part C — Cortex-M4 오브젝트 (`[cortex-m4]`, 7줄)

같은 파일을 `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -c` 로 만든 ELF32 에서 리로케이션 타입을 예측한다. 베어메탈(bare-metal)에는 동적 링커가 없다 → GOT 가 남을까? 64비트 `adrp` 대신 32비트 Thumb-2 는 주소를 어떻게 만들까? 풀이에서는 VM 의 `arm-none-eabi-gcc` 가 만든 객체(리터럴 풀)와도 비교한다.

## 2. Problem (English)

> **Read an object file without linking it.**
>
> You are given `anatomy.c` (no `main`). Build it three ways:
> `cc -O2 -c` (Mach-O arm64), `clang --target=aarch64-linux-gnu -O2 -c` (ELF64), and
> `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -c` (ELF32 ARM, firmware).
>
> 1. For each of 16 symbols, predict the `nm` letter and the section in both Mach-O and ELF, or `-` if the symbol is not in the object at all.
> 2. For 11 references (calls, variable loads, a pointer stored in `.data`), predict the relocation types the assembler leaves for the linker, or `-` if none.
> 3. Do the same for 7 references in the Cortex-M4 object.
>
> Constraints: edit only the prediction file (`starter.txt` → your answers). `bash check.sh starter` builds the three objects, parses real `nm`, `nm -m`, `llvm-nm`, `objdump -t/-r/-h` output and prints `PASS/FAIL` per row plus `N/M passed`. Cells are whitespace-separated; multiple relocation types are comma-separated in any order.
>
> Example rows (given): `api_entry  T __TEXT,__text  T .text` and `call:ext_log  ARM64_RELOC_BRANCH26  R_AARCH64_CALL26`.
>
> Follow-ups: why are all addresses in an ELF `.o` zero? Why does a call to a `static` function in the same file have a relocation in Mach-O but not in ELF? Why does `ext_counter` go through a GOT on aarch64-linux but not on Cortex-M?

## 3. 시작 코드

관찰 대상 소스:

<!-- code: anatomy.c -->

예측 표 (채울 파일). 몇 줄은 예시로 채워져 있다:

<!-- code: starter.txt -->

검사기 — 세 오브젝트를 실제로 빌드하고 `nm` / `nm -m` / `llvm-nm` / `objdump -t -r -h` 출력을 파싱해서 표와 비교한다 (쓰기는 `/tmp/TOOL-B04_work/<variant>` 에만):

<!-- code: check.sh -->

```bash
bash check.sh starter                 # 3/34 passed, exit 1 — '?' 를 채워서 34/34 로
SHOW_ACTUAL=1 bash check.sh starter   # FAIL 줄에 실제 값까지 (스포일러, 예측 후에만)
bash demo_mac.sh                      # 맥: otool/nm/objdump 전체 덤프 (bash demo_mac.sh relocs 처럼 일부만도)
bash demo_vm.sh                       # Lima VM: readelf -h -S -s -r, GNU nm, gcc / arm-none-eabi-gcc 비교
```

실제 시작 상태 출력 (앞부분과 끝):

```text
== [symbols]  symbol | macho_nm macho_section | elf_nm elf_section
PASS [symbols] api_entry
FAIL [symbols] board_init   predicted: ? ? ? ?
FAIL [symbols] counter_next   predicted: ? ? ? ?
...
FAIL [cortex-m4] relsec:text   predicted: ?
3/34 passed
```

> 도구 주의(이 맥): `/usr/local/bin/llvm-objdump`, `/usr/local/bin/llvm-size` 는 LLVM 3.2 라 고장 — PATH 앞에 있으니 **`/usr/bin/objdump`(Apple LLVM 21, Mach-O 와 ELF 둘 다 읽음)** 와 `/Library/Developer/CommandLineTools/usr/bin/llvm-nm` 을 전체 경로로 쓴다. `readelf` 는 맥에 없으므로 VM(`demo_vm.sh`). 맥 SDK 27.2 는 링크가 깨져 있어 `SDKROOT=…/MacOSX26.4.sdk` 를 export 한다 (`-c` 만 할 때도 헤더 경로 때문에 그대로 둔다).

## 4. 힌트

<details>
<summary>힌트 1 — nm 글자의 두 규칙</summary>

대문자 = 외부(global) 심볼, 소문자 = 파일 안(local). 글자 자체는 "어느 종류 섹션인가": `T/t` 코드, `D/d` 초기화 데이터, `B/b` 0 데이터, `R/r` 읽기 전용(ELF), `S/s` 그 밖의 섹션(Mach-O 에서는 `__const`, `__cstring`, `__common` 섹션 등), `U` 미정의, `C` common, `W` weak(ELF). `static` 을 붙이면 글자가 소문자로 바뀐다 — 이게 "static = 내부 연결(internal linkage)" 의 nm 버전이다.

</details>

<details>
<summary>힌트 2 — 컴파일러가 아예 안 만드는 심볼</summary>

`-O2` 에서 값이 한 번도 바뀌지 않는 `static` 변수는 상수로 접혀서(constant folding) 사라질 수 있다. 호출되지 않는 `static` 함수는 clang 이 만들지 않는다 (`-Wunused-function` 경고가 힌트). `used` 속성은 "안 쓰여도 내보내라" 다.

</details>

<details>
<summary>힌트 3 — 잠정 정의 int g_tent; 는 컴파일러 기본값에 달려 있다</summary>

`-fcommon` 이면 `C`(common), `-fno-common` 이면 일반 bss 정의. Clang 11 / GCC 10 부터 기본이 `-fno-common` 으로 바뀌었다고 알려져 있지만(확인 권장), **맥 드라이버가 실제로 무엇을 넘기는지** `cc -### -c anatomy.c` 로 직접 보라.

</details>

<details>
<summary>힌트 4 — 리로케이션이 생기는 조건</summary>

어셈블러가 **오프셋을 지금 확정할 수 없을 때만** 리로케이션이 남는다: 다른 파일의 심볼, 링크 때 바뀔 수 있는 심볼(weak), 다른 섹션에 있어 최종 거리를 모르는 대상. 같은 섹션 안의 로컬 함수는 ELF 어셈블러가 바로 계산한다. 단, Mach-O `.o` 는 헤더 플래그 `SUBSECTIONS_VIA_SYMBOLS` 를 보라. 값이 상수로 접힌 `const` 는 메모리를 아예 안 읽는다.

</details>

<details>
<summary>힌트 5 — AArch64 주소 만들기 = 2개 명령</summary>

`adrp xN, page` (4 KB 페이지 주소, ±4 GB) + `add`/`ldr` 의 12비트 하위 오프셋. 그래서 리로케이션도 2개씩: 상위(`PAGE21` / `ADR_PREL_PG_HI21`)와 하위(`PAGEOFF12` / `ADD_ABS_LO12_NC`·`LDST32_ABS_LO12_NC`). 외부 변수는 주소 자체를 GOT 에서 읽어 오므로 `GOT_LOAD_*` / `ADR_GOT_PAGE`+`LD64_GOT_LO12_NC`. Thumb-2 는 `movw`/`movt` 로 16비트씩 두 번 넣거나, 코드 뒤 리터럴 풀(literal pool)의 32비트 워드를 `ldr` 로 읽는다.

</details>

## 5. 풀이

### 도구 지도 — 무엇을 볼 때 무엇을 쓰나

| 보고 싶은 것 | 맥 (Mach-O) | ELF (맥에서 읽기) | ELF (VM, GNU binutils) |
|---|---|---|---|
| 파일 헤더 | `otool -hv` | `/usr/bin/objdump --private-headers`(ELF 는 거의 빈 출력) | `readelf -h` |
| 섹션 목록 | `otool -l`(load command), `objdump -h` | `objdump -h` | `readelf -S -W` |
| 심볼 | `nm`, **`nm -m`**(섹션·weak·[no dead strip]), `objdump -t` | `llvm-nm`, `objdump -t` | `nm`, `readelf -s -W` |
| 리로케이션 | `objdump -r`, `objdump -d -r` | 같음 | `readelf -r -W`(addend 까지) |
| 디스어셈블 | `objdump -d -r --no-show-raw-insn` | `objdump -d -r` | `objdump -d -r`, `arm-none-eabi-objdump` |

### Part A — 심볼: 글자와 섹션

맥 `nm -m` 실제 출력 (`bash demo_mac.sh "nm -m"`):

```text
0000000000000024 (__TEXT,__text) external _api_entry
0000000000000008 (__TEXT,__text) weak external _board_init
0000000000000010 (__TEXT,__text) external _counter_next
0000000000000174 (__DATA,__bss) non-external _counter_next.s_local
                 (undefined) external _ext_counter
                 (undefined) external _ext_log
00000000000000b8 (__TEXT,__const) external _g_const
00000000000000a8 (__DATA,__data) external _g_init
00000000000000b0 (__DATA,__data) external _g_msg
0000000000000004 (common) (alignment 2^2) external _g_tent
0000000000000170 (__DATA,__common) external _g_zero
000000000000009c (__TEXT,__text) non-external _helper
0000000000000000 (__TEXT,__text) non-external [no dead strip] _kept_static
00000000000000bc (__TEXT,__cstring) non-external l_.str
00000000000000c2 (__TEXT,__cstring) non-external l_.str.1
0000000000000000 (__TEXT,__text) non-external ltmp0
...                                   (ltmp1~ltmp6: 섹션 시작 표시용 임시 심볼)
```

같은 소스의 ELF 는 맥 `llvm-nm` 과 VM 의 GNU `nm` 2.46 이 13줄 모두 같은 글자를 냈다 (`T api_entry`, **`W board_init`**, `b counter_next.s_local`, `U ext_counter`, `R g_const`, `D g_init`, **`B g_tent`**, `B g_zero`, `t helper` …, 전체는 `demo_vm.sh` 의 "clang GNU nm").

`readelf -s -W` (VM) 은 같은 정보를 칸으로 쪼개 보여 준다 — nm 글자가 어디서 오는지가 보인다:

```text
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     5: 0000000000000004     4 OBJECT  LOCAL  DEFAULT    6 counter_next.s_local
     6: 0000000000000098    12 FUNC    LOCAL  DEFAULT    2 helper
    15: 0000000000000008     8 FUNC    WEAK   DEFAULT    2 board_init
    18: 0000000000000000     0 NOTYPE  GLOBAL DEFAULT  UND ext_log
    19: 0000000000000000     4 OBJECT  GLOBAL DEFAULT    6 g_zero
    21: 0000000000000008     4 OBJECT  GLOBAL DEFAULT    6 g_tent
    23: 0000000000000000     4 OBJECT  GLOBAL DEFAULT    7 g_const
```

nm 글자 = **Bind**(LOCAL → 소문자, GLOBAL → 대문자, WEAK → `W`) × **Ndx 가 가리키는 섹션의 성격**(6 = `.bss` NOBITS → B, 7 = `.rodata` 읽기 전용 → R, UND → U).

줄별로 정리 (정답 표의 근거):

| 심볼 | Mach-O | ELF | 왜 |
|---|---|---|---|
| `api_entry`, `counter_next` | `T` `__TEXT,__text` | `T` `.text` | 외부 함수 |
| `board_init` | **`T`** (`nm -m`: `weak external`) | **`W`** | Apple nm 은 weak 정의를 글자로 구분하지 않는다. weak 여부는 `nm -m` 이나 `objdump -t` 의 `w` 플래그로 본다 |
| `helper`, `kept_static` | `t` | `t` | static → 로컬. `kept_static` 은 `[no dead strip]` — `used` 가 링커의 dead-strip 까지 막는다는 표시 |
| `unused_static` | `-` | `-` | clang 은 호출 안 되는 static 함수를 **`-O0` 에서도** 만들지 않는다 (연습 2). VM 의 `gcc -O0` 은 `t unused_static` 을 남겼다 — 컴파일러마다 다르다 |
| `counter_next.s_local` | `b` `__DATA,__bss` | `b` `.bss` | static 지역 = 로컬 bss. 이름은 clang 이 `함수.변수` 로 붙인다. gcc 는 `s_local.0` (VM 실측) |
| `ext_log`, `ext_counter` | `U` `undefined` | `U` `*UND*` | 쓰기만 하고 정의 안 함 → 링커가 다른 `.o`/라이브러리에서 찾아야 함 |
| `g_init`, `g_msg` | `D` `__DATA,__data` | `D` `.data` | 0 이 아닌 초기값 → 파일에 바이트가 실림 |
| `g_zero` | **`S` `__DATA,__common`** | `B` `.bss` | Mach-O 는 외부 심볼의 0-초기화 데이터를 `__common` 이라는 **zerofill 섹션**에 둔다 (로컬은 `__bss`). nm 은 text/data/bss 가 아닌 섹션이라 `S` |
| `g_tent` | **`C` `common`** | **`B` `.bss`** | 아래 상자 |
| `g_const` | `S` `__TEXT,__const` | `R` `.rodata` | 읽기 전용. Mach-O 에는 `R` 글자가 없고 `S` 로 나온다 |
| `s_file`, `s_file_zero` | `-` | `-` | `-O2` 에서 값이 상수로 접혀 사라짐 — ELF `helper` 가 `add w0, w8, #0x7`, Cortex-M 은 `adds r0, #0x7` 로 7 을 즉시값으로 넣었다. `-O0` 에선 `d`/`b` 로 존재 (연습 2) |

> **`g_tent` — common 의 정체.** `int g_tent;` 처럼 초기값 없는 파일 범위 선언은 C 의 잠정 정의다. 옛 유닉스 관례(`-fcommon`)는 이것을 "아직 정해지지 않은 정의" = **common 심볼**로 내보내, 여러 `.o` 에 같은 이름이 있어도 링커가 하나로 합쳐 줬다. 맥 실측: `cc -### -c anatomy.c` 의 cc1 인자에 **`-fcommon` 이 들어 있다** — Apple clang 21 은 Darwin 에서 아직 common 이 기본이다. 반면 `--target=aarch64-linux-gnu` 의 인자에는 없다 → `-fno-common` → 평범한 `.bss` 정의 `B`. (기본값이 GCC 10 / Clang 11 에서 `-fno-common` 으로 바뀌었다는 이력은 릴리스 노트 확인 권장.) Mach-O 의 common 은 심볼 표에서 "미정의 + 값 = 크기" 로 표현된다: nm 의 `0000000000000004 C` 는 주소가 아니라 **크기 4**, `otool -l` 의 `LC_DYSYMTAB` 도 `nundefsym 3`(ext_log, ext_counter, g_tent)이다. `-fno-common` 을 주면 `S __DATA,__common`(섹션!) 으로 바뀐다 — 이름이 같아서 헷갈리는 함정 (연습 1).

#### 섹션 대응표 — Mach-O ↔ ELF

Mach-O 섹션은 `세그먼트,섹션` 두 단 이름이다. `.o` 의 `LC_SEGMENT_64` 는 이름이 비어 있고(`segname` 공백) 섹션마다 소속 세그먼트 이름을 따로 들고 있다. `otool -l` 실제 발췌:

```text
Load command 0
      cmd LC_SEGMENT_64
  segname 
   vmsize 0x0000000000000178
   nsects 7
Section
  sectname __text
   segname __TEXT
      size 0x00000000000000a8
    reloff 1176
    nreloc 16
Section
  sectname __common
   segname __DATA
      addr 0x0000000000000170
    offset 0
     flags 0x00000001
```

`__common` 의 `offset 0`, `flags 0x1`(S_ZEROFILL) = 파일에 바이트가 없다 (ELF 의 `NOBITS` 와 같은 뜻). VM `readelf -S -W` (VM clang 21.1.8 로 같은 플래그 빌드 — 내용은 맥 빌드와 같고 `.comment` 문자열 길이만 달라 오프셋이 조금 다르다):

```text
  [Nr] Name              Type            Address          Off    Size   ES Flg Lk Inf Al
  [ 2] .text             PROGBITS        0000000000000000 000040 0000a4 00  AX  0   0  4
  [ 3] .rela.text        RELA            0000000000000000 000420 000168 18   I 14   2  8
  [ 4] .data             PROGBITS        0000000000000000 0000e8 000010 00  WA  0   0  8
  [ 5] .rela.data        RELA            0000000000000000 000588 000018 18   I 14   4  8
  [ 6] .bss              NOBITS          0000000000000000 0000f8 00000c 00  WA  0   0  4
  [ 7] .rodata           PROGBITS        0000000000000000 0000f8 000004 00   A  0   0  4
  [ 8] .rodata.str1.1    PROGBITS        0000000000000000 0000fc 000010 01 AMS  0   0  1
  [14] .symtab           SYMTAB          0000000000000000 0001c8 000258 18      1  15  8
```

`.rela.text` 크기 0x168 = 360 B = 24 B(ES 0x18) × **15개**, `Inf 2` = ".text 용". `.rodata.str1.1` 의 `AMS` = Alloc + **Merge + Strings**, `ES 01` = 1바이트 문자 — 링커가 같은 문자열을 합칠 수 있다는 표시이고 이름 끝 `str1.1` 이 "문자 크기 1, 정렬 1" 이다 (gcc 는 `.rodata.str1.8`, Cortex-M gcc 는 `.rodata.str1.4` 로 정렬만 달랐다 — VM 실측).

| 내용 | Mach-O (`nm -m`) | ELF | 파일에 바이트? | 펌웨어 링크 후 |
|---|---|---|---|---|
| 코드 | `__TEXT,__text` | `.text` | 있음 | Flash |
| 문자열 리터럴 | `__TEXT,__cstring` | `.rodata.str1.1` | 있음 | Flash |
| `const` 데이터 | `__TEXT,__const` | `.rodata` | 있음 | Flash |
| 초기화 데이터 | `__DATA,__data` | `.data` | 있음 | Flash 에 원본 + RAM 에 복사본 (BM-B08, TOOL-B06) |
| 0 데이터 (로컬) | `__DATA,__bss` | `.bss` | 없음 (zerofill / NOBITS) | RAM, 스타트업이 0 으로 |
| 0 데이터 (외부) | `__DATA,__common` 섹션 | `.bss` | 없음 | RAM |
| common 심볼 | `(common)` 심볼, 섹션 없음 | `*COM*` (`-fcommon` 일 때) | 없음 | 링커가 bss 에 자리 배정 |
| 언와인드 정보 | `__LD,__compact_unwind` | `.eh_frame` (+ Cortex-M `.ARM.exidx`) | 있음 | 보통 `-fno-unwind-tables`/링커 스크립트로 정리 |

#### 왜 `.o` 의 주소는 0 인가

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">.o 안의 "주소" — 아직 아무 데도 놓이지 않았다 (실측, 16진수)</text>
  <text x="10" y="48" fill="currentColor">ELF: 섹션마다 0 부터 (Address 칸 전부 0, 심볼 값 = 섹션 안 오프셋)</text>
  <rect x="20" y="58" width="224" height="26" class="f-acc" stroke="currentColor"/>
  <text x="28" y="76" fill="currentColor" font-family="monospace" font-size="12">.text 0..a4</text>
  <rect x="254" y="58" width="96" height="26" class="f-ok" stroke="currentColor"/>
  <text x="260" y="76" fill="currentColor" font-family="monospace" font-size="12">.data 0..10</text>
  <rect x="360" y="58" width="96" height="26" class="f-mut" stroke="currentColor"/>
  <text x="366" y="76" fill="currentColor" font-family="monospace" font-size="12">.bss 0..c</text>
  <rect x="466" y="58" width="96" height="26" class="f-warn" stroke="currentColor"/>
  <text x="472" y="76" fill="currentColor" font-family="monospace" font-size="12">.rodata 0..4</text>
  <rect x="572" y="58" width="150" height="26" class="f-warn" stroke="currentColor"/>
  <text x="578" y="76" fill="currentColor" font-family="monospace" font-size="12">.rodata.str1.1 0..10</text>
  <text x="20" y="104" fill="currentColor" font-family="monospace" font-size="12">g_init = .data+0   g_msg = .data+8   g_zero = .bss+0   g_tent = .bss+8   s_local = .bss+4</text>
  <text x="10" y="138" fill="currentColor">Mach-O: 섹션들을 한 줄로 이어 붙인 가상 주소 0..178 (심볼 값 = 그 줄 위 위치)</text>
  <rect x="20" y="148" width="269" height="26" class="f-acc" stroke="currentColor"/>
  <text x="28" y="166" fill="currentColor" font-family="monospace" font-size="12">__text 0..a8</text>
  <rect x="289" y="148" width="26" height="26" class="f-ok" stroke="currentColor"/>
  <rect x="315" y="148" width="6" height="26" class="f-warn" stroke="currentColor"/>
  <rect x="321" y="148" width="26" height="26" class="f-warn" stroke="currentColor"/>
  <rect x="347" y="148" width="6" height="26" class="f-mut" stroke="currentColor"/>
  <rect x="353" y="148" width="256" height="26" class="f-mut" stroke="currentColor"/>
  <text x="360" y="166" fill="currentColor" font-family="monospace" font-size="12">__compact_unwind d0..170</text>
  <rect x="609" y="148" width="6" height="26" class="f-mut" stroke="currentColor"/>
  <rect x="615" y="148" width="6" height="26" class="f-mut" stroke="currentColor"/>
  <text x="270" y="196" fill="currentColor" font-family="monospace" font-size="12">__data a8 · __const b8 · __cstring bc</text>
  <text x="560" y="196" fill="currentColor" font-family="monospace" font-size="12">__common 170 · __bss 174</text>
  <text x="10" y="228" fill="currentColor">어느 쪽이든 진짜 주소는 링커가 정한다 → 그 주소를 쓰는 명령·포인터 자리는 "구멍" + 리로케이션.</text>
</svg>
```

`.o` 는 **재배치 가능(relocatable)** 파일이다 (`readelf -h`: `Type: REL (Relocatable file)`, `Entry point address: 0x0`, `Number of program headers: 0`; `otool -hv`: `filetype OBJECT`). 다른 `.o` 와 합쳐져 최종 위치가 정해지기 전이라, ELF 는 각 섹션을 0 기준으로 두고 심볼 값 = 섹션 안 오프셋이다. Mach-O `.o` 는 섹션들을 한 가상 범위(0~0x178)에 이어 붙여 두지만 이것도 링크 때 전부 다시 놓인다 (실행 파일에서는 `_g_init` 이 `0x100008000` 이 됐다). 프로그램 헤더(세그먼트)가 0 개인 것도 같은 이유 — "어디에 로드할지" 는 링커(펌웨어면 링커 스크립트, TOOL-M01)가 정한다.

### Part B — 리로케이션: 링커가 채울 구멍

ELF `objdump -d -r` 실제 발췌 (`api_entry`):

```text
      34: 90000000     	adrp	x0, 0x0 <kept_static>
		0000000000000034:  R_AARCH64_ADR_PREL_PG_HI21	.rodata.str1.1+0x6
      38: 91000000     	add	x0, x0, #0x0
		0000000000000038:  R_AARCH64_ADD_ABS_LO12_NC	.rodata.str1.1+0x6
      40: 94000000     	bl	0x40 <api_entry+0x1c>
		0000000000000040:  R_AARCH64_CALL26	ext_log
      48: 94000014     	bl	0x98 <helper>
      50: 9000000a     	adrp	x10, 0x0 <kept_static>
		0000000000000050:  R_AARCH64_ADR_GOT_PAGE	ext_counter
      54: 90000009     	adrp	x9, 0x0 <kept_static>
		0000000000000054:  R_AARCH64_ADR_PREL_PG_HI21	g_tent
      5c: f940014a     	ldr	x10, [x10]
		000000000000005c:  R_AARCH64_LD64_GOT_LO12_NC	ext_counter
      60: b9400129     	ldr	w9, [x9]
		0000000000000060:  R_AARCH64_LDST32_ABS_LO12_NC	g_tent
      80: 94000000     	bl	0x80 <api_entry+0x5c>
		0000000000000080:  R_AARCH64_CALL26	board_init
      88: 11019100     	add	w0, w8, #0x64
```

- `94000000` = `bl` 의 26비트 오프셋이 0 → "자기 자신으로 점프" 로 디스어셈블된다. 이것이 **구멍**이다. 바로 아래 줄이 그 구멍을 채울 처방: "오프셋 0x40 의 26비트를 `ext_log` 주소로 고쳐라 (`CALL26`)".
- `bl 0x98 <helper>` (`94000014`, 0x14×4 = 0x50 = 0x98−0x48) 는 **이미 채워져 있고 리로케이션이 없다** — 같은 `.text` 안이라 어셈블러가 거리를 안다.
- `add w0, w8, #0x64` — `g_const`(100) 는 메모리에서 읽지 않고 즉시값으로 접혔다 → `ref:g_const` 는 `-`. 맥 `-O0` 빌드에서도 `add w8, w8, #0x64` 였다 (clang 은 초기값이 보이는 `const int` 전역을 `-O0` 에서도 접는다; 연습 2 에서 ELF `-O0` 로 확인). 심볼 `g_const` 자체는 다른 파일이 쓸 수 있으니 `.rodata` 에 남는다.
- 로컬 데이터는 **섹션 심볼 + addend** 로 가리킨다: `"api_entry"` 리터럴 = `.rodata.str1.1+0x6` ("hello\0" 6바이트 다음), `s_local` = `.bss+0x4`. readelf 로 보면 addend 가 엔트리 안에 따로 있다 (RELA):

```text
Relocation section '.rela.text' at offset 0x420 contains 15 entries:
    Offset             Info             Type               Symbol's Value  Symbol's Name + Addend
0000000000000034  0000000b00000113 R_AARCH64_ADR_PREL_PG_HI21 0000000000000000 .rodata.str1.1 + 6
0000000000000040  000000120000011b R_AARCH64_CALL26       0000000000000000 ext_log + 0
0000000000000050  0000001400000137 R_AARCH64_ADR_GOT_PAGE 0000000000000000 ext_counter + 0
0000000000000080  0000000f0000011b R_AARCH64_CALL26       0000000000000008 board_init + 0

Relocation section '.rela.data' at offset 0x588 contains 1 entry:
0000000000000008  0000000b00000101 R_AARCH64_ABS64        0000000000000000 .rodata.str1.1 + 0
```

`Info` = (심볼 번호 << 32) | 타입: `0x12` = 심볼 18 `ext_log`, `0x11b` = 283 = `R_AARCH64_CALL26`. `.rela.data` 의 한 줄이 `g_msg` — 데이터 안의 8바이트 포인터 자리(`.data+8`)에 "문자열 주소를 통째로 써라(ABS64)". `objdump -s` 로 보면 그 자리는 지금 0 이다: `.data` = `2a000000 00000000 00000000 00000000` (42, 패딩, 빈 포인터).

맥 `objdump -r` (Mach-O) 은 같은 구멍을 이렇게 적는다 — 섹션별 목록, addend 칸 없음:

```text
RELOCATION RECORDS FOR [__text]:
0000000000000084 ARM64_RELOC_BRANCH26     _board_init
0000000000000080 ARM64_RELOC_PAGEOFF12    _g_init
000000000000007c ARM64_RELOC_PAGE21       _g_init
0000000000000064 ARM64_RELOC_GOT_LOAD_PAGEOFF12 _ext_counter
0000000000000060 ARM64_RELOC_GOT_LOAD_PAGE21 _ext_counter
0000000000000058 ARM64_RELOC_GOT_LOAD_PAGEOFF12 _g_tent
0000000000000054 ARM64_RELOC_GOT_LOAD_PAGE21 _g_tent
0000000000000050 ARM64_RELOC_PAGEOFF12    _g_zero
000000000000004c ARM64_RELOC_PAGE21       _g_zero
0000000000000048 ARM64_RELOC_BRANCH26     _helper
0000000000000040 ARM64_RELOC_BRANCH26     _ext_log
0000000000000038 ARM64_RELOC_PAGEOFF12    l_.str.1
0000000000000034 ARM64_RELOC_PAGE21       l_.str.1
...                                       (+ _counter_next.s_local 3줄: 0x1c, 0x14 PAGEOFF12, 0x10 PAGE21)
RELOCATION RECORDS FOR [__data]:
0000000000000008 ARM64_RELOC_UNSIGNED     l_.str
```

| 참조 | Mach-O | ELF (aarch64-linux, PIE 기본) | 고치는 것 |
|---|---|---|---|
| 외부 함수 호출 `ext_log` | `BRANCH26` | `CALL26` | `bl` 의 imm26 (×4 → ±128 MB) |
| weak 함수 호출 `board_init` | `BRANCH26` | `CALL26` | 같은 파일에 정의가 있어도 **링크 때 강한 정의로 바뀔 수 있어서** 구멍으로 남긴다 |
| static 함수 호출 `helper` | **`BRANCH26`** | **`-`** | ELF 는 어셈블러가 해결. Mach-O 는 `SUBSECTIONS_VIA_SYMBOLS`: 링커가 심볼 단위 조각(atom)으로 잘라 옮기거나 dead-strip 할 수 있으므로 같은 섹션 안 호출도 리로케이션으로 남긴다 (관찰은 사실, 동기 설명은 ld64 문서 확인 권장) |
| `g_init` (정의된 전역) | `PAGE21` + `PAGEOFF12` | `ADR_PREL_PG_HI21` + `LDST32_ABS_LO12_NC` | `adrp` 의 페이지 + `ldr w` 의 12비트 오프셋. ELF 는 하위 12비트를 **어느 명령이 쓰는지**(add=ADD_ABS, 4바이트 ldr=LDST32 → 4로 나눠 인코딩)까지 타입에 담고, Mach-O 는 `PAGEOFF12` 하나로 명령을 보고 판단한다 |
| `g_tent` | **`GOT_LOAD_PAGE21` + `GOT_LOAD_PAGEOFF12`** | `ADR_PREL_PG_HI21` + `LDST32_ABS_LO12_NC` | Mach-O 에선 common = 미정의 취급이라 외부 변수처럼 GOT 경유. ELF 에선 그냥 내 `.bss` |
| `ext_counter` (외부 변수) | `GOT_LOAD_*` | `ADR_GOT_PAGE` + `LD64_GOT_LO12_NC` | 주소를 GOT 칸에서 8바이트 `ldr` 로 읽어 온 뒤 접근. clang 의 linux 타깃은 PIE 가 기본(`-###` 에 `-pic-is-pie`)이라 "공유 라이브러리에 있을지도 모르는" 외부 변수는 GOT. `-fno-pic` 이면 직접 접근 (연습 4) |
| `s_local` | `PAGE21` + `PAGEOFF12` (`_counter_next.s_local`) | 같은 두 종류 (`.bss+0x4`) | 로컬 심볼 vs 섹션+addend |
| `"api_entry"` | `PAGE21` + `PAGEOFF12` (`l_.str.1`) | `ADR_PREL_PG_HI21` + **`ADD_ABS_LO12_NC`** | 주소 자체가 필요(인자로 넘김) → `add` |
| `g_msg` 안의 포인터 | `UNSIGNED` | `ABS64` | 데이터 8바이트 전체 |
| 코드 리로케이션이 사는 곳 | 섹션 헤더의 `reloff`/`nreloc` | `.rela.text` | — |

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">구멍(hole) → 링커가 채움: bl ext_log (ELF, VM 의 GNU ld 2.46 로 링크, 실측)</text>
  <rect x="10" y="36" width="340" height="120" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="58" fill="currentColor" font-weight="bold">anatomy.o  .text</text>
  <text x="20" y="80" fill="currentColor" font-family="monospace" font-size="12">40: 94000000  bl 0x40  (offset 0)</text>
  <text x="20" y="104" fill="currentColor" font-weight="bold">.rela.text 엔트리</text>
  <text x="20" y="124" fill="currentColor" font-family="monospace" font-size="12">r_offset 0x40  type CALL26</text>
  <text x="20" y="142" fill="currentColor" font-family="monospace" font-size="12">symbol ext_log  addend 0</text>
  <rect x="390" y="36" width="340" height="120" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="400" y="58" fill="currentColor" font-weight="bold">linked (PIE 실행 파일)</text>
  <text x="400" y="80" fill="currentColor" font-family="monospace" font-size="12">api_entry = 0x80c  → P = 0x80c+0x1c</text>
  <text x="400" y="100" fill="currentColor" font-family="monospace" font-size="12">828: 94000019  bl 88c &lt;ext_log&gt;</text>
  <text x="400" y="124" fill="currentColor" font-family="monospace" font-size="12">S = ext_log = 0x88c</text>
  <text x="400" y="142" fill="currentColor" font-family="monospace" font-size="12">(S + A - P) &gt;&gt; 2 = 0x64 &gt;&gt; 2 = 0x19</text>
  <path d="M350 96 L388 96" stroke="currentColor" stroke-width="2"/>
  <path d="M380 90 L388 96 L380 102" stroke="currentColor" fill="none" stroke-width="2"/>
  <rect x="10" y="176" width="720" height="104" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="198" fill="currentColor" font-weight="bold">같은 원리, 다른 구멍들 (링크 후 실측)</text>
  <text x="20" y="220" fill="currentColor" font-family="monospace" font-size="12">문자열: adrp x0, 0 ; add x0, x0, #0x922   (.rodata.str1.1 = 0x91c, +6 = "api_entry")</text>
  <text x="20" y="240" fill="currentColor" font-family="monospace" font-size="12">board_init(weak): 868: 97ffffe2  bl 7f0 &lt;board_init&gt;   (강한 정의가 없어 weak 기본값으로)</text>
  <text x="20" y="260" fill="currentColor" font-family="monospace" font-size="12">ext_counter: ldr x10, [x10, #4048] (GOT 칸) — 칸은 실행 시 R_AARCH64_RELATIVE 로 채움</text>
</svg>
```

링크해서 구멍이 메워지는 걸 직접 보려면 `demo_link.c`(없는 `ext_counter`/`ext_log` 와 `main` 제공)를 같이 링크한다. 실행 결과는 맥·VM 모두 같다:

```text
$ cc -O2 anatomy.c demo_link.c -o /tmp/TOOL-B04_work/linked && /tmp/TOOL-B04_work/linked
log: api_entry 5
api_entry(5)=142 ext_counter=1022 g_msg=hello
```

맥 링커(Apple ld-1266.8) 결과 `objdump -d --disassemble-symbols=_api_entry` 발췌:

```text
100000628:     	bl	0x100000690 <_ext_log>
100000630:     	bl	0x100000684 <_helper>
10000063c:     	adrp	x9, 0x100008000 <_g_init>
100000640:     	add	x9, x9, #0x18          ← .o 에선 ldr x9,[x9] (GOT_LOAD) 였던 g_tent
10000066c:     	bl	0x100000720 <_printf+0x100000720>   ← board_init 호출이 __stubs 로
```

두 가지가 눈에 띈다. (1) `.o` 에선 GOT 를 읽던 `ldr` 이 `add` 로 바뀌었다 — 대상이 같은 실행 파일 안에 있으니 링커가 GOT 경유를 직접 주소 계산으로 줄였다 (`.o` 의 `LC_LINKER_OPTIMIZATION_HINT` 가 이런 최적화용 힌트). (2) weak `board_init` 호출은 `__stubs` → GOT 를 거친다. `dyld_info -fixups` 에 `__got 0x100004008 bind <weak-def-coalesce>/_board_init` 가 있다 — Darwin 은 실행 시에 다른 이미지의 같은 이름 weak 정의와 합칠 수 있게 남겨 둔다. VM 의 GNU ld 는 `bl 7f0 <board_init>` 직접 호출이었다. 펌웨어(정적 링크, 동적 로더 없음)는 VM 쪽처럼 링크 때 끝난다.

### Part C — Cortex-M4 오브젝트: GOT 가 없는 세계

맥 clang 이 만든 `thumbv7em-none-eabi` 객체의 `objdump -r` (실측):

```text
RELOCATION RECORDS FOR [.text]:
OFFSET   TYPE                     VALUE
00000008 R_ARM_THM_MOVW_ABS_NC    counter_next.s_local
0000000c R_ARM_THM_MOVT_ABS       counter_next.s_local
0000001c R_ARM_THM_MOVW_ABS_NC    .L.str.1
00000020 R_ARM_THM_MOVT_ABS       .L.str.1
00000026 R_ARM_THM_CALL           ext_log
0000003a R_ARM_THM_MOVW_ABS_NC    ext_counter
0000004a R_ARM_THM_MOVT_ABS       ext_counter
00000052 R_ARM_THM_MOVW_ABS_NC    g_init
00000056 R_ARM_THM_MOVT_ABS       g_init
00000060 R_ARM_THM_CALL           board_init

RELOCATION RECORDS FOR [.data]:
00000004 R_ARM_ABS32              .rodata.str1.1
```

그리고 디스어셈블:

```text
      1c: f240 0000    	movw	r0, #0x0
      20: f2c0 0000    	movt	r0, #0x0
      26: f7ff fffe    	bl	0x26 <api_entry+0xe>    @ imm = #-0x4
      2c: f000 f81e    	bl	0x6c <helper>           @ imm = #0x3c
```

- **GOT 가 없다.** `none-eabi` 는 PIC 가 아닌 절대 주소 모델이 기본이다 — 펌웨어는 링커 스크립트가 정한 고정 주소에 놓이고, 실행 중 주소를 고쳐 줄 동적 링커가 없다. 그래서 `ext_counter` 도 `g_init` 과 똑같이 절대 주소 `MOVW`(하위 16비트) + `MOVT`(상위 16비트).
- **REL vs RELA.** 섹션 이름이 `.rel.text` 다 (`relsec:text`). 32비트 ARM ELF 는 addend 를 엔트리에 따로 두지 않고 **명령 비트 안에** 둔다(implicit addend). `bl` 이 `f7ff fffe` = `imm = #-0x4` 인 이유: Thumb 에서 PC 는 현재 명령 + 4 를 가리키므로, "대상 − 4" 를 미리 넣어 두면 링커가 S + A − P 로 정확한 오프셋을 만든다. AArch64 는 `.rela.text`(엔트리 24 B) + addend 칸이라 명령 비트는 0 이었다. Cortex-M `.rel.text` 엔트리는 8 B (`readelf -S` 의 ES 08, 7개 = 0x38).
- `bl helper` 는 여기서도 이미 해결(`f000 f81e`, 리로케이션 없음). `R_ARM_THM_CALL` 의 도달 거리는 Thumb-2 `bl` 기준 ±16 MB 다 — Flash 가 그보다 크거나 RAM 함수(TOOL-A07)를 부르면 링커가 veneer 를 끼운다 (확인 권장).
- `readelf -h` (VM, arm-none-eabi-gcc 객체): `Class: ELF32`, `Machine: ARM`, `Flags: 0x5000000, Version5 EABI`.

VM 의 `arm-none-eabi-gcc 14.2.1 -mcpu=cortex-m4 -mthumb -O2 -c` 는 같은 소스를 **리터럴 풀**로 만들었다:

```text
  26:	480b      	ldr	r0, [pc, #44]	@ (54 <api_entry+0x34>)
  28:	f7ff fffe 	bl	0 <ext_log>
			28: R_ARM_THM_CALL	ext_log
  ...
	...
			54: R_ARM_ABS32	.rodata.str1.4
			58: R_ARM_ABS32	.bss
			5c: R_ARM_ABS32	ext_counter
			60: R_ARM_ABS32	.data
```

함수 끝(0x54~0x63)에 32비트 워드 4개를 두고 `ldr rN, [pc, #off]` 로 읽는다. 구멍은 그 **데이터 워드**에 있고 타입은 `R_ARM_ABS32`. clang 의 `movw/movt` 는 명령 2개(8 B)로 리터럴 풀 접근을 없애고, gcc 의 리터럴 풀은 명령 1개(2 B) + 4 B 워드 — 그리고 같은 기준 주소(`.bss`)에 오프셋을 더해 `g_zero`·`g_tent` 를 `ldrd` 한 번에 읽었다 (섹션 앵커(section anchor) 최적화로 보인다 — 확인 권장). Flash 실행 전용 메모리(XOM, `-mpure-code`)를 쓰는 칩이면 리터럴 풀이 금지라 movw/movt 가 필수다 (확인 권장).

펌웨어 현장에서 이 표를 어디에 쓰나:

| 상황 | 볼 것 |
|---|---|
| "undefined reference to `HAL_GetTick`" | 그걸 부르는 `.o` 의 `nm -u` / `U`, 정의해야 할 `.o` 의 `nm` 에 `T` 가 있는지 (TOOL-B05) |
| 인터럽트 핸들러가 안 불린다 | 벡터 테이블 쪽 `W Default_Handler` 를 덮어야 할 내 핸들러가 `T` 로, **정확히 같은 이름**으로 있는지 |
| RAM 이 예상보다 많다 | `B`/`b`/`C` 와 `D`/`d` 의 크기 (`nm -S --size-sort`, TOOL-B06, TOOL-M02) |
| 안 쓰는 코드가 Flash 에 남아 있다 | `-ffunction-sections` 로 함수마다 섹션이 생기는지(연습 3) → `--gc-sections` (TOOL-M13) |
| 부트로더↔앱 이 고정 주소 함수 테이블을 공유 | 리로케이션 없이 절대 주소로 고정됐는지 `objdump -r` |

## 6. 정답 코드

<!-- code: solution.txt diff=starter.txt -->

```text
$ bash check.sh solution
== [symbols]  symbol | macho_nm macho_section | elf_nm elf_section
PASS [symbols] api_entry
...                                   (symbols 16 + relocs 11 + cortex-m4 7 줄 모두 PASS)
PASS [cortex-m4] relsec:text
34/34 passed
```

검사기가 진짜로 틀린 예측을 잡는지도 확인했다: `board_init` 의 ELF 글자를 `T` 로, `ref:ext_counter` 의 ELF 칸을 GOT 없는 타입으로, Cortex-M `call:helper` 를 `R_ARM_THM_CALL` 로 바꾼 사본 → `31/34 passed`, 정확히 그 세 줄이 FAIL. 실행 시간은 변형당 약 2~5초(빌드 3개 + 파싱).

## 7. 자주 하는 실수

**1. `/usr/local/bin/llvm-objdump` 를 그냥 부른다.** 이 맥에선 LLVM 3.2 라 최신 오브젝트를 못 읽는다. → `/usr/bin/objdump`(Apple LLVM 21), `/Library/Developer/CommandLineTools/usr/bin/llvm-nm` 을 전체 경로로. `readelf` 가 필요하면 VM.

**2. Mach-O `nm` 의 `T` 를 보고 "weak 가 아니네" 라고 결론낸다.** Apple `nm` 은 weak 정의도 `T` 로 찍었다. → `nm -m` 의 `weak external`, `objdump -t` 의 ` w ` 플래그로 확인. ELF 에선 `W`.

**3. `C _g_tent` 의 `0000000000000004` 를 주소로 읽는다.** common 심볼의 값은 크기다(Mach-O). ELF `*COM*` 도 주소가 아니라 정렬·크기 정보다. 주소는 링커가 정한다.

**4. `__DATA,__common`(섹션)과 `(common)`(심볼)을 같은 것으로 본다.** `int g_zero = 0;` 과 `-fno-common` 의 `g_tent` 는 **정의**(섹션에 자리 있음, `S`), `-fcommon` 의 `g_tent` 는 **아직 정의가 아닌** common(`C`) 이다. 둘 다 링크 결과는 0 으로 채운 RAM 이다.

**5. 리로케이션이 없으면 그 심볼을 안 쓴다고 생각한다.** `helper` 는 ELF 에서 리로케이션 없이 호출되고, `g_const` 는 값이 접혀서 아예 안 읽힌다. "누가 이걸 참조하나" 는 리로케이션만이 아니라 디스어셈블(`objdump -d`)까지 봐야 한다. 반대로 Mach-O 는 같은 섹션 호출에도 리로케이션을 남긴다.

**6. 데이터 섹션에는 리로케이션이 없다고 생각한다.** `const char *g_msg = "hello";` 처럼 **포인터를 초기값으로 가진 데이터**는 `.rela.data`(`ABS64`) / Cortex-M `.rel.data`(`ABS32`) 를 만든다. 펌웨어의 함수 포인터 테이블·벡터 테이블이 정확히 이 모양이다.

## 8. 면접 Q&A

<details>
<summary>Q1. nm 에서 T, t, D, B, R, U, W, C 는 각각 무엇이고, static 을 붙이면 왜 글자가 바뀌나요?</summary>

글자는 "어느 종류 섹션인가", 대소문자는 "외부에 보이는가". `T` 코드, `D` 초기화 데이터, `B` 0 데이터, `R` 읽기 전용, `U` 이 파일에 정의가 없음, `W` weak 정의(링크 때 강한 정의가 있으면 그쪽이 이김), `C` common(잠정 정의를 링커가 합침). `static` 은 내부 연결이라 심볼 바인딩이 LOCAL 이 되고 nm 은 소문자로 찍는다 (`readelf -s` 의 Bind 칸). 다른 `.o` 에서 그 이름으로 참조하면 링커가 찾지 못해 undefined 가 난다. Mach-O 는 `R` 대신 `S`, weak 도 `T` 로 찍는 차이가 있다 (실측).

</details>

<details>
<summary>Q2. 리로케이션이 뭔가요? 오브젝트 파일의 주소가 0 인 이유는?</summary>

`.o` 는 아직 최종 위치가 정해지지 않은 조각이라, 다른 심볼의 주소가 들어가야 할 명령·데이터 자리를 0(또는 ARM32 처럼 addend)으로 비워 두고 "이 오프셋을, 이 방식으로, 이 심볼 주소로 고쳐라" 라는 기록을 남긴다 — 그게 리로케이션이다. 예: `40: 94000000 bl` + `R_AARCH64_CALL26 ext_log`. 링커는 모든 섹션을 배치해 S(심볼 주소), P(구멍 위치)를 알게 된 뒤 타입별 공식(CALL26 이면 (S+A−P)>>2 를 imm26 에)으로 채운다. 실측: VM 링크 후 `828: 94000019 bl 88c <ext_log>`. ELF `.o` 의 섹션 주소가 전부 0 인 이유도 같다 — 심볼 값은 섹션 안 오프셋일 뿐이다.

</details>

<details>
<summary>Q3. 같은 파일 안 static 함수 호출에 리로케이션이 있나요?</summary>

ELF 에선 없었다 — 같은 `.text` 안이라 어셈블러가 상대 거리를 계산해 `bl 0x98 <helper>` 로 바로 넣는다 (Cortex-M 도 동일). Mach-O 에선 `ARM64_RELOC_BRANCH26 _helper` 가 남았다. Mach-O `.o` 는 `SUBSECTIONS_VIA_SYMBOLS` 플래그로 "심볼 경계에서 잘라도 된다" 고 선언하고, 링커가 함수 단위로 재배치·dead-strip 할 수 있어서 상대 거리를 고정하지 않는 것으로 이해된다(동기는 확인 권장). ELF 에서 비슷한 효과를 내려면 `-ffunction-sections` 로 함수마다 섹션을 만든다 — 그러면 섹션 간 호출이 되어 리로케이션이 생긴다.

</details>

<details>
<summary>Q4. -fcommon 과 -fno-common 의 차이와, 왜 기본값이 바뀌었나요?</summary>

`int x;`(초기값 없음)를 두 `.c` 에 쓰면, `-fcommon` 에선 둘 다 common 심볼이라 링커가 하나로 합쳐 조용히 링크된다. `-fno-common` 에선 둘 다 `.bss` 의 진짜 정의라 multiple definition 에러가 난다. 조용한 합침은 크기·타입이 다른 같은 이름 변수를 숨기는 버그원이라, GCC 10 / Clang 11 에서 기본이 `-fno-common` 이 됐다고 알려져 있다(릴리스 노트 확인 권장). 실측 함정: **Apple clang 21 은 Darwin 에서 cc1 에 `-fcommon` 을 넘긴다**(`cc -###`) — 맥에선 `C`, linux/펌웨어 타깃에선 `B`. 해법은 헤더에 `extern int x;`, `.c` 하나에만 정의. 옛 펌웨어 코드를 새 arm-none-eabi-gcc 로 올리면 이 에러가 쏟아지는 게 흔한 경험담이다 (TOOL-B05).

</details>

<details>
<summary>Q5. weak 심볼은 어떻게 동작하고 무엇이 위험한가요?</summary>

weak 정의는 "다른 데 강한 정의가 없으면 나를 써라". 같은 파일 안에 정의가 있어도 링크 때 바뀔 수 있으니 호출은 리로케이션(`CALL26 board_init`)으로 남는다 (실측). 펌웨어 스타트업은 모든 IRQ 핸들러를 `Default_Handler` 의 weak 별칭으로 두고, 사용자가 같은 이름으로 정의하면 그걸 쓴다. 위험: 이름 오타(`USART1_IRQHandler` vs `USART1_IRQHandle`)면 링커는 아무 경고 없이 weak 기본(보통 무한 루프)을 쓴다 → `nm` 으로 최종 ELF 에서 그 이름이 `T` 인지 `W` 인지 확인하는 CI 체크가 싸고 효과적이다 (TOOL-B05). Darwin 은 weak 정의를 실행 시 coalescing 대상으로 남겨 `__stubs` 경유 호출이 됐다 (실측) — 플랫폼마다 비용도 다르다.

</details>

<details>
<summary>Q6. 외부 변수 접근이 aarch64 Linux 객체에선 GOT 를 거치는데 Cortex-M 객체에선 왜 안 거치나요?</summary>

GOT 는 "링크 때 주소를 모를 수 있는(공유 라이브러리·ASLR) 심볼의 주소를 담는 표" 다. clang 의 linux 타깃은 PIE 가 기본이라 외부 변수는 다른 모듈에 있을 수도 있다고 보고 `ADR_GOT_PAGE` + `LD64_GOT_LO12_NC` 로 GOT 칸을 읽는다. `-fno-pic` 이면 `ADR_PREL_PG_HI21` + `LDST32_ABS_LO12_NC` 로 직접 접근(연습 4). `thumbv7em-none-eabi` 는 정적 링크·고정 주소가 기본이라 `MOVW/MOVT ABS` 절대 주소. 링크된 PIE 에선 GOT 칸 자체가 `R_AARCH64_RELATIVE` 동적 리로케이션으로 로드 시 채워졌다 — 펌웨어엔 이걸 해 줄 로더가 없다.

</details>

<details>
<summary>Q7. 이 오브젝트에서 Flash 와 RAM 을 각각 얼마 쓸지 어떻게 보나요?</summary>

섹션 크기로 본다: `.text` 0xa4 + `.rodata` 4 + `.rodata.str1.1` 0x10 은 Flash, `.data` 0x10 은 Flash(초기값 원본) **와** RAM(복사본) 둘 다, `.bss` 0xc 는 RAM 만(파일엔 바이트 없음 = NOBITS). 단 `.o` 는 링크 전이라 `--gc-sections` 로 지워질 것, 라이브러리에서 끌려올 것(printf 등)이 빠져 있다 — 최종 판단은 링크된 ELF 의 `size -A` 와 맵 파일로 한다 (TOOL-B06, TOOL-M02). `.rela.*`, `.symtab`, `.comment` 는 호스트용 정보라 타깃 메모리를 쓰지 않는다.

</details>

## 9. 추가 연습

`practice.txt` 는 빌드 플래그를 바꾼 5개 변형에 대한 예측이다 (`sym <build> <symbol> <nm> <section>`, `rel <build> <ref> <types>`). 먼저 예측하고 `bash check.sh practice` 로 확인.

### 연습 1 — common 을 뒤집기

맥에 `-fno-common`, ELF 에 `-fcommon` 을 주면 `g_tent` 의 글자·섹션·리로케이션은?

<details>
<summary>답 보기</summary>

맥 `-fno-common`: `S __DATA,__common` — common 심볼이 진짜 정의가 되어 `__common` **섹션**으로 들어갔고, 리로케이션도 `GOT_LOAD_*` 에서 `PAGE21` + `PAGEOFF12` 로 바뀌었다. 즉 Part B 에서 g_tent 가 GOT 를 거친 이유는 "common = 아직 이 파일 정의가 아님" 이었다는 증거다. ELF `-fcommon`: `C *COM*` 인데 리로케이션은 여전히 `ADR_PREL_PG_HI21` + `LDST32_ABS_LO12_NC`(GOT 아님) — clang 의 ELF 쪽은 common 을 이 실행 파일 안에 생길 것으로 본다. 같은 "common" 이라도 코드 생성 결과는 포맷마다 다르다(실측).

</details>

### 연습 2 — -O0 에서 다시 나타나는 것

ELF `-O0` 빌드에서 `s_file`, `s_file_zero`, `unused_static` 의 글자·섹션, 그리고 `g_const` 를 읽는 리로케이션은?

<details>
<summary>답 보기</summary>

`s_file` → `d .data`, `s_file_zero` → `b .bss` (최적화가 없으니 메모리에서 읽는다). `unused_static` → 여전히 없음: clang 은 호출되지 않는 static 함수를 `-O0` 에서도 내보내지 않는다. VM `gcc -O0` 은 `0000000000000030 t unused_static` 을 남겼다 — "안 쓰는 static 은 사라진다" 는 컴파일러·최적화 레벨 의존이다. `g_const` 는 `-O0` 에서도 리로케이션 `-`(값 100 이 즉시값). 반면 gcc 는 static 지역 이름을 `s_local.0` 으로 붙였다.

</details>

### 연습 3 — 함수·데이터마다 섹션 하나씩

`-ffunction-sections -fdata-sections` 를 주면 각 심볼의 ELF 섹션 이름은?

<details>
<summary>답 보기</summary>

`.text.api_entry`, `.text.board_init`(W), `.data.g_init`, `.bss.g_zero`, `.bss.g_tent`, `.rodata.g_const`, `.bss.counter_next.s_local` — 규칙은 "원래 섹션 + `.` + 심볼 이름". 이렇게 쪼개야 링커의 `--gc-sections` 가 **섹션 단위**로 안 쓰는 함수·변수를 버릴 수 있다 (TOOL-M13). 부작용: 섹션이 다르니 `api_entry → helper` 호출도 이제 리로케이션이 필요해진다 (Q3 의 Mach-O 와 같은 상황).

</details>

### 연습 4 — PIC 를 끄면 GOT 가 사라진다

`--target=aarch64-linux-gnu -O2 -fno-pic` 에서 `ref:ext_counter` 의 리로케이션은?

<details>
<summary>답 보기</summary>

`R_AARCH64_ADR_PREL_PG_HI21` + `R_AARCH64_LDST32_ABS_LO12_NC` — `g_init` 과 똑같은 직접 접근. GOT 칸을 읽는 `ldr x10, [x10]` 한 단계가 없어진다. 이게 펌웨어(Part C)가 기본으로 사는 세계다. 공유 라이브러리를 만들 땐 반대로 `-fPIC` 가 필수 (TOOL-M06).

</details>

### 연습 5 (VM, 검사 없음) — gcc 가 만든 객체와 비교

`bash demo_vm.sh` 의 `gcc -O2 readelf -S` / `-r` 를 보고 clang 객체와 다른 점 세 가지를 찾아라.

<details>
<summary>답 보기</summary>

(1) `g_msg` 가 `.data` 가 아니라 **`.data.rel.local`** 에 있고 리로케이션은 `.rela.data.rel.local` 의 `ABS64 .rodata.str1.8 + 10` — gcc 는 PIE 에서 "로드 시 고쳐야 하는 포인터를 담은 데이터" 를 따로 모은다(로드 후 읽기 전용으로 돌리는 RELRO 와 연관, 확인 권장). (2) 문자열 섹션이 `.rodata.str1.8`(8바이트 정렬). (3) `g_zero`, `g_tent`, `g_init` 을 이름이 아니라 `.bss + 0`, `.data + 0` 기준으로 접근(섹션 앵커로 보임)하고, static 지역 이름이 `s_local.0`. 글자(`T W D B R U t b`)는 clang 과 같다.

</details>

### 한 번에 검증

<!-- code: practice.txt -->

```text
$ bash check.sh practice
== [practice]  sym <build> <symbol> <nm> <section>  /  rel <build> <ref> <types>
PASS [practice] sym macho_nocommon g_tent
...                                   (12 sym 줄 + 4 rel 줄 모두 PASS)
PASS [practice] rel elf_nopic ref:ext_counter
16/16 passed
```

## 10. 핵심 정리

- `.o` = 헤더 + 섹션 + 심볼 표 + 리로케이션. ELF 는 섹션마다 주소 0 부터, Mach-O `.o` 는 한 범위에 이어 붙임 — 어느 쪽이든 진짜 주소는 링커가 정한다.
- nm: 글자 = 섹션 종류(T D B R/S U C W), 대문자 = 외부, 소문자 = `static`. Mach-O 는 `R` 대신 `S`, weak 도 `T` → `nm -m` 으로 확인.
- 잠정 정의 `int x;`: 맥 clang 은 아직 `-fcommon`(`C`), linux/펌웨어 타깃은 `-fno-common`(`B .bss`). `__DATA,__common` 섹션 ≠ `(common)` 심볼.
- 리로케이션 = 구멍 + 처방(offset, type, symbol, addend). `bl` 은 `CALL26`/`BRANCH26`, 주소는 `adrp`+lo12 두 개, 외부 변수는 PIE/Mach-O 에서 GOT, 데이터 속 포인터는 `ABS64`/`UNSIGNED`/`ABS32`.
- 리로케이션이 없는 참조도 있다: 같은 섹션 로컬 호출(ELF), 상수로 접힌 `const`. weak 호출은 같은 파일이어도 구멍으로 남는다.
- Cortex-M: GOT 없음, `R_ARM_THM_CALL` + `MOVW/MOVT_ABS`(clang) 또는 리터럴 풀 `ABS32`(gcc), `.rel.text` = addend 가 명령 안에(`f7ff fffe`).
- 맥에서 ELF 를 볼 땐 `/usr/bin/objdump`(LLVM 21)·CLT `llvm-nm`, `readelf` 는 VM. `/usr/local/bin/llvm-*` 는 쓰지 않는다.

## 참고자료

- System V gABI — Object Files (ELF header, sections, symbol table, `SHN_COMMON`, relocation REL/RELA): <https://www.sco.com/developers/gabi/latest/contents.html>
- ARM ELF for the Arm 64-bit Architecture (AAELF64: `R_AARCH64_CALL26`, `ADR_PREL_PG_HI21`, `*_LO12_NC`, GOT 계열): <https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst>
- ARM ELF for the Arm Architecture (AAELF32: `R_ARM_THM_CALL`, `R_ARM_THM_MOVW_ABS_NC`, `R_ARM_ABS32`, REL 의 implicit addend): <https://github.com/ARM-software/abi-aa/blob/main/aaelf32/aaelf32.rst>
- Apple `<mach-o/loader.h>`, `<mach-o/nlist.h>`, `<mach-o/arm64/reloc.h>` (LC_SEGMENT_64, S_ZEROFILL, `MH_SUBSECTIONS_VIA_SYMBOLS`, `ARM64_RELOC_*`) — CLT SDK 의 `usr/include/mach-o/`
- GNU binutils 문서: `nm`(글자 표), `readelf`, `objdump`: <https://sourceware.org/binutils/docs/binutils/>
- LLVM 도구 문서: `llvm-nm`, `llvm-objdump`, `llvm-readelf`: <https://llvm.org/docs/CommandGuide/>
- GCC 10 변경 사항 (`-fno-common` 기본): <https://gcc.gnu.org/gcc-10/changes.html>
- John R. Levine, *Linkers and Loaders* — 3장(오브젝트 파일), 7장(리로케이션)
- 관련 레슨: ARM-B02 (clang -S 읽기), ARM-B12 (objdump 로 Thumb 코드 밀도), BM-B08 (링커 스크립트·벡터 테이블로 Cortex-M3 부팅), TOOL-B01 (컴파일 단계), TOOL-B05 (링크 에러·weak 핸들러), TOOL-B06 (size·Flash/RAM), TOOL-B09 (정적 라이브러리), TOOL-M01 (링커 스크립트), TOOL-M02 (맵 파일), TOOL-M06 (공유 라이브러리·PIC), TOOL-M13 (gc-sections), TOOL-M14 (DWARF), TOOL-A07 (RAM 함수·veneer), TOOL-A11 (ELF 파서 직접 구현)
