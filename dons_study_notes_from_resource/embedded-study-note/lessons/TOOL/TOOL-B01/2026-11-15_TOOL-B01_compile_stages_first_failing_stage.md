---
id: TOOL-B01
time: 60분
updated: 2026-11-15
---
# TOOL-B01 컴파일 4단계 관찰: -E -S -c 링크

## 학습 목표

- `cc -E`(전처리) → `-S`(컴파일) → `-c`(어셈블) → 링크를 따로 돌려서, 단계마다 **입력 · 출력 파일 · 그 파일을 보는 도구**(`.i` = 텍스트, `.s` = 텍스트, `.o` = `nm`/`objdump -r`, 실행 파일 = `nm`/실행)를 말할 수 있다.
- 에러 메시지 한 줄만 보고 **어느 단계에서 났는지** 분류한다 (`file not found`·`#error` = 전처리, `expected ';'` = 컴파일, `Error: unknown mnemonic` = 어셈블러, `Undefined symbols` / `undefined reference to` / `duplicate symbol` / `multiple definition of` = 링커).
- `-save-temps`, `-ccc-print-phases`, `-###`, `-v`, `-H` 로 "드라이버(driver)가 실제로 무엇을 실행하는지" 를 확인한다. clang 은 한 프로세스(`-cc1 -emit-obj`) 안에서 세 단계를 다 하고, gcc 는 `cc1` → `as` → `collect2/ld` 를 따로 띄운다.
- 단계 경계의 함정을 안다: 매크로 버그는 전처리가 아니라 컴파일에서 터진다, `-fsyntax-only` 를 통과해도 `-S` 에서 터지는 에러가 있다, 같은 인라인 asm 오류가 clang 에선 컴파일, gcc 에선 어셈블 단계다.
- 맥(Mach-O, ld64)과 펌웨어 쪽(ELF, GNU ld, `arm-none-eabi-gcc`) 메시지를 나란히 읽는다.

## 1. 문제

CI 로그 300줄 중 빨간 한 줄을 보고 "이건 헤더 경로 문제", "이건 .o 하나가 링크 라인에서 빠졌다" 를 바로 말하는 능력 — 펌웨어 빌드(SSD 컨트롤러 FW, Cortex-M + `arm-none-eabi-gcc`)에서 매일 쓰고, 면접에서는 "`undefined reference` 는 컴파일 에러인가요?" 로 확인한다.

`cc main.c util.c -o fw` 한 줄은 사실 네 단계를 숨기고 있다. 이 레슨은 그걸 하나씩 꺼내서 산출물을 눈으로 보고, 일부러 각 단계에서만 나는 에러를 만들어 분류한다.

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">cc main.c util.c -o fw 가 숨기는 4단계 (이 맥 실측, -std=c11 -O1)</text>
  <rect x="10" y="40" width="110" height="70" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="62" fill="currentColor" font-family="monospace">main.c</text>
  <text x="20" y="82" fill="currentColor" font-size="12">14줄</text>
  <text x="20" y="100" fill="currentColor" font-size="12">#include, LOG()</text>
  <rect x="165" y="40" width="120" height="70" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="175" y="62" fill="currentColor" font-family="monospace">main.i</text>
  <text x="175" y="82" fill="currentColor" font-size="12">643줄 (헤더 펼침)</text>
  <text x="175" y="100" fill="currentColor" font-size="12">linemarker 171개</text>
  <rect x="330" y="40" width="120" height="70" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="340" y="62" fill="currentColor" font-family="monospace">main.s</text>
  <text x="340" y="82" fill="currentColor" font-size="12">65줄 AArch64 asm</text>
  <text x="340" y="100" fill="currentColor" font-size="12">bl _sectors_for</text>
  <rect x="495" y="40" width="120" height="70" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="505" y="62" fill="currentColor" font-family="monospace">main.o</text>
  <text x="505" y="82" fill="currentColor" font-size="12">1208 B Mach-O</text>
  <text x="505" y="100" fill="currentColor" font-size="12">U 5개, reloc 12개</text>
  <rect x="650" y="40" width="100" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="660" y="62" fill="currentColor" font-family="monospace">fw</text>
  <text x="660" y="82" fill="currentColor" font-size="12">33632 B</text>
  <text x="660" y="100" fill="currentColor" font-size="12">주소 확정</text>
  <path d="M120 75 L163 75" stroke="currentColor" stroke-width="2"/>
  <path d="M285 75 L328 75" stroke="currentColor" stroke-width="2"/>
  <path d="M450 75 L493 75" stroke="currentColor" stroke-width="2"/>
  <path d="M615 75 L648 75" stroke="currentColor" stroke-width="2"/>
  <text x="122" y="132" fill="currentColor" font-family="monospace" font-size="12">-E</text>
  <text x="287" y="132" fill="currentColor" font-family="monospace" font-size="12">-S</text>
  <text x="452" y="132" fill="currentColor" font-family="monospace" font-size="12">-c</text>
  <text x="612" y="132" fill="currentColor" font-family="monospace" font-size="12">(링크)</text>
  <text x="122" y="150" fill="currentColor" font-size="12">전처리기</text>
  <text x="287" y="150" fill="currentColor" font-size="12">컴파일러</text>
  <text x="452" y="150" fill="currentColor" font-size="12">어셈블러</text>
  <text x="612" y="150" fill="currentColor" font-size="12">링커 ld</text>
  <rect x="10" y="164" width="740" height="66" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="184" fill="currentColor" font-size="12">여기서만 나는 에러 —  -E: #error · 헤더 없음 · #if 짝 · 매크로 인자 수   -S: 문법 · 타입 · 미선언 함수</text>
  <text x="20" y="202" fill="currentColor" font-size="12">-c: .org 역행 · (gcc) 모르는 명령어   링크: 정의 없는 심볼 · 중복 정의 · main 없음 · .o 빠짐</text>
  <text x="20" y="220" fill="currentColor" font-size="12">앞 단계를 다 통과해야 다음 단계 에러가 보인다</text>
  <text x="10" y="252" fill="currentColor" font-size="12">clang: -cc1 한 프로세스가 -E/-S/-c 를 메모리 안에서 다 함 (-### 실측) → 중간 파일은 -save-temps 로만 보임</text>
  <text x="10" y="272" fill="currentColor" font-size="12">gcc:  cc1 (C→.s) 와 as (.s→.o), collect2→ld 가 별도 프로그램 (VM gcc -v 실측)</text>
  <text x="10" y="292" fill="currentColor" font-size="12">단계별로 나눠 만든 main.o 와 cc -c main.c 의 main.o 는 바이트 단위로 같다 (cmp 실측)</text>
</svg>
```

실습 프로젝트 (레슨 폴더에 있음):

| 파일 | 역할 | 이 레슨에서 보는 것 |
|---|---|---|
| `config.h` | 매크로만 (`FW_MAJOR`, `SECTOR_SIZE`, `STR()`, `SECTORS_FOR()`, `LOG()`) | `-E` 후 사라지는 것 |
| `util.h` | 선언만 (`extern const char fw_version[]`, 함수 2개) | 선언은 컴파일러를 만족시키지만 링커는 정의가 필요 |
| `util.c` | 정의 (두 번째 번역 단위, translation unit) | `nm` 의 `T`/`S` |
| `main.c` | `LOG("boot")`, `printf`, util 호출 | `nm` 의 `U`, 재배치(relocation) |
| `bad_*.c`, `ok_single.c` | 일부러 망가뜨린 스니펫 16개 (+ 짝 파일) | 분류기 테스트 입력 |

### Part A — 4단계 산출물 관찰 (`demo_stages.sh`)

- `-E` → `main.i`: 몇 줄로 늘었나, `# 1 "./util.h" 1` 같은 linemarker 는 뭔가, `LOG()`·`FW_VERSION` 은 어떻게 펼쳐졌나.
- `-S` → `main.s`: 섹션 지시어, `bl _sectors_for` (아직 주소 모름).
- `-c` → `main.o`: `nm` 의 `U`(undefined) 5개, `objdump -r` 의 재배치 = 링커가 채울 구멍.
- 링크 → `fw`: `nm` 에 주소가 생기고 `U` 는 `_printf`, `_puts` 만 남는다 (동적 라이브러리).
- 드라이버 관찰 도구: `-save-temps`, `-ccc-print-phases`, `-###`, `-v`(include 검색 목록), `-H`(헤더 트리).

### Part B — `first_failing_stage`: 에러를 단계로 분류하는 함수

`starter.sh` 의 bash 함수 `first_failing_stage file.c [file2.c ...]` 를 완성한다. 네 단계를 **따로** 돌려서 처음 실패한 단계 이름 한 단어(`preprocess|compile|assemble|link|ok`)를 출력한다. `check.sh` 가 22개 케이스로 검사한다 — 함정 포함: 괄호 빠진 매크로(전처리 통과!), `__attribute__((error))`(`-fsyntax-only` 통과!), `.org` 역행(`-S` 통과!), 파일 여러 개에서 "가장 이른 단계" 찾기.

### Part C — 펌웨어 현실: GNU/ELF 메시지, gcc vs clang, 크로스 컴파일러

- Lima VM 에서 같은 함수를 `CC=gcc`, `CC=clang` 으로 돌려 결과가 **다른** 한 칸을 찾고 이유를 설명한다.
- ld64 `Undefined symbols for architecture arm64` ↔ GNU ld `undefined reference to` ↔ ld.lld `undefined symbol:` 를 한 표로.
- `arm-none-eabi-gcc -mcpu=cortex-m4` 로 같은 4단계, newlib `crt0` 이 `main` 을 찾는 링크 에러, `arm-none-eabi-size`.

## 2. Problem (English)

> **Which build stage failed?**
>
> Your firmware CI only prints `make: *** [fw.elf] Error 1` plus a wall of compiler output. Write a bash function
>
> ```bash
> first_failing_stage file1.c [file2.c ...]     # prints: preprocess | compile | assemble | link | ok
> ```
>
> that runs the four stages **separately** — `cc -E` (.c→.i), `cc -S` (.i→.s), `cc -c` (.s→.o), and one link of all objects — and prints the first stage that fails, exactly one word on stdout. Diagnostics go to stderr. Scratch files go to `$STAGE_DIR` only. With several files, report the *earliest* stage any file fails at (run stage by stage over all files, not file by file). `CC` and `STAGE_CFLAGS` (default `-std=c11 -Wall`) must be honoured.
>
> Constraints: bash 3.2 (macOS `/bin/bash`), `set -u` safe, no new files next to the sources.
>
> Example:
> ```text
> $ first_failing_stage bad_macro_paren.c   # #define SQUARE(x) ((x) * (x)   <- missing ')'
> compile                                     # -E succeeds; the EXPANDED text fails to parse
> $ first_failing_stage bad_dup_a.c bad_dup_b.c
> link
> ```
>
> Follow-ups: what does each stage output; why is `undefined reference` not a compiler error; what errors survive `-fsyntax-only`; why might clang and gcc disagree on the stage; what does `-save-temps` show; how is a missing `main` different on a Cortex-M build?

## 3. 시작 코드

검사기 (모든 변형 공통 — 수정하지 않는다):

<!-- code: check.sh -->

시작 코드 — TODO 는 함수 본문 하나:

<!-- code: starter.sh -->

```bash
bash check.sh starter      # 어디서 실행해도 됨: 스크립트가 자기 폴더로 cd, 작업은 /tmp/TOOL-B01_work/starter
bash demo_stages.sh        # Part A 관찰 (맥) → 결과 사본: demo_stages_mac.txt
limactl shell elinux -- bash "$PWD/demo_vm.sh"   # Part C (VM, gcc/ld.bfd/arm-none-eabi) → demo_vm.txt
```

starter 실행 결과 (실제 출력) — 한 방에 빌드하면 "실패했다" 는 알지만 "어디서" 는 모른다:

```text
== first_failing_stage (starter.sh, cc = Apple clang version 21.0.0)
FAIL  bad_error.c                        -> compile    (want preprocess) | bad_error.c:3:2: error: "BOARD_REV not set: build with -DBOARD_REV=2"
FAIL  bad_missing_header.c               -> compile    (want preprocess) | bad_missing_header.c:2:10: fatal error: 'board_pins.h' file not found
FAIL  bad_unterminated_if.c              -> compile    (want preprocess) | bad_unterminated_if.c:3:2: error: unterminated conditional directive
FAIL  bad_macro_args.c                   -> compile    (want preprocess) | bad_macro_args.c:6:32: error: too few arguments provided to function-lik
PASS  bad_syntax.c                       -> compile    | bad_syntax.c:4:20: error: expected ';' at end of declaration
PASS  bad_type.c                         -> compile    | bad_type.c:6:9: error: initializing 'int' with an expression of incompat
PASS  bad_macro_paren.c                  -> compile    | bad_macro_paren.c:7:25: error: expected ')'
PASS  bad_implicit.c                     -> compile    | bad_implicit.c:5:12: error: call to undeclared function 'flash_init'; IS
PASS  bad_attr_error.c                   -> compile    | bad_attr_error.c:7:5: error: call to 'debug_dump' declared with 'error' 
PASS  bad_asm_mnemonic.c                 -> compile    | bad_asm_mnemonic.c:5:22: error: unrecognized instruction mnemonic
FAIL  bad_asm_org.c                      -> compile    (want assemble) | bad_asm_org.c:5:42: error: invalid .org offset '16' (at offset '260')
FAIL  bad_undefined.c                    -> compile    (want link) | Undefined symbols for architecture arm64:
FAIL  bad_dup_a.c bad_dup_b.c            -> compile    (want link) | duplicate symbol '_g_boot_count' in:
FAIL  bad_no_main.c                      -> compile    (want link) | Undefined symbols for architecture arm64:
FAIL  main.c                             -> compile    (want link) | Undefined symbols for architecture arm64:
PASS  ok_single.c                        -> ok         | 
PASS  main.c util.c                      -> ok         | 
== flags reach the stages, stage-major order over several files
PASS  bad_error.c                        -> ok         | 
PASS  bad_no_main.c bad_syntax.c         -> compile    | bad_syntax.c:4:20: error: expected ';' at end of declaration
FAIL  bad_asm_org.c bad_macro_args.c     -> compile    (want preprocess) | bad_asm_org.c:5:42: error: invalid .org offset '16' (at offset '260')
PASS  stdout is exactly one word
PASS  lesson dir untouched (30 entries)
12/22 passed
```

스니펫 요약 (전부 레슨 폴더에 있음, 각 파일 첫 줄 주석에 기대 단계):

| 파일 | 망가뜨린 곳 (핵심 줄) | 기대 |
|---|---|---|
| `bad_error.c` | `#error "BOARD_REV not set: build with -DBOARD_REV=2"` | preprocess |
| `bad_missing_header.c` | `#include "board_pins.h"` (없는 파일) | preprocess |
| `bad_unterminated_if.c` | `#if USE_DMA` … `#endif` 없음 | preprocess |
| `bad_macro_args.c` | 3인자 매크로 `REG_FIELD(sr, 4)` | preprocess |
| `bad_syntax.c` | `int sectors = 3` (`;` 없음) | compile |
| `bad_type.c` | `int code = st;` (struct → int) | compile |
| `bad_macro_paren.c` | `#define SQUARE(x) ((x) * (x)` | compile |
| `bad_implicit.c` | 선언 없이 `flash_init(0)` 호출 | compile |
| `bad_attr_error.c` | `__attribute__((error("...")))` 함수 호출 | compile |
| `bad_asm_mnemonic.c` | `__asm__ volatile("frobnicate x0, x1")` | compile (clang) |
| `bad_asm_org.c` | `.org 0x100` 뒤에 `.org 0x10` | assemble |
| `bad_undefined.c` | `int flash_erase(unsigned);` 선언만 | link |
| `bad_dup_a.c` + `bad_dup_b.c` | `int g_boot_count = 0;` / `= 1;` | link |
| `bad_no_main.c` | `main` 없음 | link |
| `main.c` 단독 | `util.c` 를 링크 라인에서 뺌 | link |
| `ok_single.c`, `main.c util.c` | 정상 | ok |

## 4. 힌트

<details>
<summary>힌트 1 — 한 단계만 하고 멈추는 플래그</summary>

`-E` 는 전처리 결과를 내고 멈춘다(`-o x.i`). `-S` 는 어셈블리까지, `-c` 는 오브젝트까지. 입력 확장자로 "어디서부터" 할지 정해진다: `.i` 를 주면 전처리를 건너뛰고(cpp-output), `.s` 를 주면 어셈블러부터. 그러니 `cc -S x.i`, `cc -c x.s`, `cc x.o y.o` 로 단계를 정확히 하나씩 돌릴 수 있다.

</details>

<details>
<summary>힌트 2 — 파일 여러 개: 파일 순서가 아니라 단계 순서로</summary>

`bad_asm_org.c bad_macro_args.c` 를 파일별로 끝까지 돌리면 첫 파일이 assemble 에서 먼저 멈춘다. 그런데 둘째 파일은 그보다 이른 preprocess 에서 깨진다. "가장 이른 단계" 를 찾으려면 **모든 파일을 -E → 모든 .i 를 -S → 모든 .s 를 -c → 한 번 링크** 순서로 돈다. 링크는 원래 전체가 한 번이다.

</details>

<details>
<summary>힌트 3 — 출력은 한 단어, 진단은 stderr</summary>

검사기는 `got=$(first_failing_stage ...)` 로 stdout 만 받는다. 도구의 출력은 전부 `>&2` 로 보내고, `echo compile; return 0` 처럼 단어 하나 찍고 바로 끝낸다. `set -u` 환경이므로 bash 3.2 에서 빈 배열 `"${a[@]}"` 는 unbound 에러가 난다 — 오브젝트 목록은 문자열로 모아도 된다(`/tmp` 경로라 공백 없음).

</details>

<details>
<summary>힌트 4 — "컴파일 통과했는데 왜 어셈블에서?"</summary>

clang 은 `-S` 를 만들 때 인라인 asm 문자열을 내장 어셈블러로 **파싱**한다(그래서 모르는 명령어는 `-S` 에서 걸린다). 하지만 `.org` 가 뒤로 가는지는 바이트를 실제로 배치(layout)해 봐야 안다 — `.o` 를 만들 때다. `bad_asm_org.c` 는 `-S` 가 rc=0 이다.

</details>

## 5. 풀이

### Part A — 산출물 한 단계씩 보기 (`demo_stages.sh`, 실제 출력)

**① 전처리 `-E` (.c → .i)** — `#include` 는 파일 내용을 그 자리에 붙이고, 매크로를 펼치고, 조건부 컴파일을 고르고, 주석을 지운다. 결과는 여전히 C 텍스트다.

```text
      14 main.c
      17 util.c
     643 main.i
     297 util.i
linemarker lines in main.i: 171
--- linemarkers for our own headers
7:# 1 "main.c" 2
568:# 3 "main.c" 2
569:# 1 "./util.h" 1
625:# 6 "./util.h" 2
626:# 1 "./config.h" 1
627:# 7 "./util.h" 2
--- tail of main.i (macros are gone, LOG() expanded)
    puts("[fw] " "boot");
--- util.i: FW_VERSION and SECTORS_FOR after expansion
284:const char fw_version[] = "2" "." "7";
288:    return (((bytes) + 512u - 1u) / 512u);
--- no #define survives -E:  grep -c '#define' main.i = 0
```

- 14줄 → 643줄: 거의 다 `<stdio.h>` 와 그 밑 헤더다. 그래서 헤더 하나 고치면 그걸 include 하는 모든 `.c` 를 다시 컴파일해야 한다 (Make 의존성 = TOOL-B08).
- linemarker `# 줄번호 "파일" 플래그`: 플래그 `1` = 그 파일로 들어감, `2` = 그 파일로 돌아옴, `3` = 시스템 헤더(경고 억제), `4` = `extern "C"` 로 감쌈. `569:# 1 "./util.h" 1` → "여기부터 util.h 1번 줄", `627:# 7 "./util.h" 2` → "config.h 끝, util.h 7번 줄로 복귀". **컴파일러 에러가 `.i` 줄이 아니라 원래 `.c`/`.h` 줄 번호로 나오는 이유**가 이 표시다.
- `"2" "." "7"` — `STR()` 은 펼쳐졌지만 인접 문자열 붙이기(translation phase 6)는 전처리 뒤라서 아직 세 조각이다. `#define` 은 0개: 매크로는 컴파일러에게 존재하지 않는다 → 디버거에서 `p SECTOR_SIZE` 가 보통 안 되는 이유 (`-g3` 은 TOOL-M14).

**② 컴파일 `-S` (.i → .s)** — 구문 분석 · 타입 검사(프론트엔드) + 최적화 · 명령어 선택(백엔드). 결과는 대상 CPU 의 어셈블리 텍스트 (읽는 법은 ARM-B02).

```text
2:	.section	__TEXT,__text,regular,pure_instructions
3:	.globl	_main                           ; -- Begin function main
5:_main:                                  ; @main
21:	bl	_puts
23:	bl	_sectors_for
30:	bl	_checksum8
33:	adrp	x8, _fw_version@GOTPAGE
42:	bl	_printf
54:	.section	__TEXT,__cstring,cstring_literals
61:	.section	__TEXT,__const
```

`bl _sectors_for` — 이름만 있고 주소는 없다. 이 `.s` 안에는 `_sectors_for` 정의가 없으니까. (Mach-O 는 C 이름 앞에 `_` 를 붙인다. ELF 는 안 붙인다: VM 의 `main.s` 는 `bl sectors_for`.)

**③ 어셈블 `-c` (.s → .o)** — 명령어를 기계어로 인코딩하고, 모르는 주소 자리는 **재배치(relocation)** 로 남긴다.

```text
main.o: Mach-O 64-bit object arm64
--- nm main.o  (U = undefined here, to be filled in by the linker)
00000000000000a0 s _boot_block
                 U _checksum8
                 U _fw_version
0000000000000000 T _main
                 U _printf
                 U _puts
                 U _sectors_for
--- nm util.o:  000000000000000c T _checksum8 / 0000000000000030 S _fw_version / 0000000000000000 T _sectors_for
--- relocations in main.o (the 'holes')
0000000000000020 ARM64_RELOC_BRANCH26     _sectors_for
0000000000000038 ARM64_RELOC_GOT_LOAD_PAGE21 _fw_version
... (__text 에 12개: BRANCH26 4, PAGE21/PAGEOFF12 쌍 3, GOT_LOAD 쌍 1)
```

`U` 5개 = 이 파일이 쓰지만 정의하지 않은 심볼. `static const boot_block` 은 소문자 `s`(파일 안에서만 보임, C-B10). 주소가 전부 0부터인 건 각 `.o` 가 자기 섹션 기준 오프셋만 알기 때문. `nm` 글자·섹션·재배치 종류의 깊은 해부는 TOOL-B04 / ARM-A09.

**④ 링크 (.o → 실행 파일)** — 모든 `.o` 의 섹션을 합쳐 주소를 정하고, 각 `U` 를 다른 파일의 정의와 짝지어 구멍을 메운다.

```text
fw: Mach-O 64-bit executable arm64
0000000100000524 T _checksum8
00000001000005a0 S _fw_version
00000001000004b0 T _main
                 U _printf
                 U _puts
0000000100000518 T _sectors_for
[fw] boot
fw 2.7: 1500 bytes -> 3 sectors, csum=0x43
```

`_printf`, `_puts` 는 여전히 `U` — libSystem.dylib 에 있고 실행할 때 dyld 가 묶는다. 펌웨어(정적 링크)에서는 이 단계에서 newlib 의 정의가 실제로 복사되어 들어온다 (Part C).

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">링커가 하는 일: U 를 정의(T/S)와 짝짓기 (nm 실측)</text>
  <rect x="10" y="35" width="210" height="150" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="57" fill="currentColor" font-family="monospace" font-weight="bold">main.o</text>
  <text x="20" y="79" fill="currentColor" font-family="monospace" font-size="12">T _main</text>
  <text x="20" y="99" fill="currentColor" font-family="monospace" font-size="12">U _sectors_for</text>
  <text x="20" y="119" fill="currentColor" font-family="monospace" font-size="12">U _checksum8</text>
  <text x="20" y="139" fill="currentColor" font-family="monospace" font-size="12">U _fw_version</text>
  <text x="20" y="159" fill="currentColor" font-family="monospace" font-size="12">U _printf  U _puts</text>
  <rect x="300" y="35" width="210" height="110" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="310" y="57" fill="currentColor" font-family="monospace" font-weight="bold">util.o</text>
  <text x="310" y="79" fill="currentColor" font-family="monospace" font-size="12">T _sectors_for  @0x0</text>
  <text x="310" y="99" fill="currentColor" font-family="monospace" font-size="12">T _checksum8    @0xc</text>
  <text x="310" y="119" fill="currentColor" font-family="monospace" font-size="12">S _fw_version   @0x30</text>
  <rect x="300" y="160" width="210" height="45" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="310" y="180" fill="currentColor" font-family="monospace" font-size="12">libSystem.dylib</text>
  <text x="310" y="197" fill="currentColor" font-size="12">printf, puts (dyld 가 실행 시)</text>
  <path d="M150 95 L298 75" stroke="currentColor" stroke-width="1.5"/>
  <path d="M150 115 L298 95" stroke="currentColor" stroke-width="1.5"/>
  <path d="M150 135 L298 115" stroke="currentColor" stroke-width="1.5"/>
  <path d="M190 155 L298 180" stroke="currentColor" stroke-width="1.5" stroke-dasharray="4 3"/>
  <rect x="560" y="35" width="170" height="170" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="570" y="57" fill="currentColor" font-weight="bold">util.o 를 빼면</text>
  <text x="570" y="79" fill="currentColor" font-size="12">cc main.c -o x</text>
  <text x="570" y="99" fill="currentColor" font-size="12">→ 짝 없는 U 3개:</text>
  <text x="570" y="119" fill="currentColor" font-family="monospace" font-size="12">_checksum8</text>
  <text x="570" y="139" fill="currentColor" font-family="monospace" font-size="12">_fw_version</text>
  <text x="570" y="159" fill="currentColor" font-family="monospace" font-size="12">_sectors_for</text>
  <text x="570" y="181" fill="currentColor" font-size="12">컴파일은 성공,</text>
  <text x="570" y="197" fill="currentColor" font-size="12">링크만 실패</text>
  <text x="10" y="230" fill="currentColor" font-size="12">같은 정의가 두 번(bad_dup_a/b) → duplicate symbol. 정의 0번(bad_undefined) → Undefined symbols.</text>
  <text x="10" y="250" fill="currentColor" font-size="12">static 정의(bad_static_helper) 는 다른 .o 의 U 와 짝지어지지 않는다 → 역시 링크 에러 (연습 2).</text>
  <text x="10" y="270" fill="currentColor" font-size="12">fw 에서 _main=0x1000004b0, _sectors_for=0x100000518 … 주소는 링크 때 처음 정해진다.</text>
</svg>
```

**⑤ 드라이버 들여다보기.** `cc` 는 컴파일러가 아니라 **드라이버(driver)** — 단계별 도구를 골라 부르는 프로그램이다.

```text
===== 6. -ccc-print-phases =====
               +- 0: input, "main.c", c
            +- 1: preprocessor, {0}, cpp-output
         +- 2: compiler, {1}, ir
      +- 3: backend, {2}, assembler
   +- 4: assembler, {3}, object
   ...  (5-9: util.c 같은 경로)
+- 10: linker, {4, 9}, image

===== 7. -### (driver prints the jobs, runs nothing) =====
"<CLT>/usr/bin/clang" "-cc1" ... "-emit-obj" ... "-o" "$TMPDIR/main-c5f0a3.o" ... "main.c"   (146 args)
"<CLT>/usr/bin/clang" "-cc1" ... "-emit-obj" ... "-o" "$TMPDIR/util-5eec35.o" ... "util.c"   (146 args)
"<CLT>/usr/bin/ld" "-demangle" ... "-arch" "arm64" ... "-o" "fw4" "$TMPDIR/main-c5f0a3.o" "$TMPDIR/util-5eec35.o" "-lSystem" "<CLT>/usr/lib/clang/21/lib/darwin/libclang_rt.osx.a"
   (23 args)
```

- `-ccc-print-phases` = **논리적** 단계(전처리 → IR → 어셈블리 → 오브젝트 → 링크). clang 은 `compiler`(C→LLVM IR) 와 `backend`(IR→asm) 를 나눠 보여준다.
- `-###` = **실제로 띄울 프로세스**. 파일당 `clang -cc1 ... -emit-obj` 하나 + `ld` 하나뿐. 전처리·컴파일·어셈블이 한 프로세스 안에서 메모리로 넘어간다(내장 어셈블러, integrated assembler). 중간 파일은 `-save-temps` 를 줘야 디스크에 생긴다: 이 맥에선 `main.i main.bc main.s main.o` — `.bc` 는 LLVM 비트코드(`file`: `LLVM bitcode, wrapper`).
- VM gcc 는 다르다 (`gcc -v -c main.c` 실측): `/usr/libexec/gcc/aarch64-linux-gnu/15/cc1 ... main.c` 다음에 `/usr/bin/aarch64-linux-gnu-as -v -EL -mabi=lp64 -o main.o /tmp/ccjuZvHO.s` — `.s` 가 진짜 임시 파일로 디스크를 거친다. 링크는 `collect2` → `ld`. gcc 15 의 `-save-temps main.c util.c -o fw` 는 `fw-main.i fw-main.s fw-main.o` 처럼 출력 이름을 앞에 붙인다(`-c` 하나면 `main.i`).

**⑥ include 검색과 헤더 트리.**

```text
===== 8. -v : include search list =====
#include "..." search starts here:
#include <...> search starts here:
 <CLT>/usr/lib/clang/21/include
 <SDK>/usr/include
 ...
===== 9. -H : header tree for main.c (dots = include depth) =====
. <SDK>/usr/include/stdio.h
.. <SDK>/usr/include/_stdio.h
...
43:. ./util.h
44:.. <CLT>/usr/lib/clang/21/include/stdint.h
45:... <SDK>/usr/include/stdint.h
52:.. ./config.h
total header opens: 52, distinct files: 49
```

`"..."` 목록이 비어 있는 이유: 따옴표 include 는 **그 소스 파일이 있는 폴더**를 먼저 보고(목록에 안 찍힘), 그다음 `-iquote`/`-I`, 그다음 `<...>` 목록을 본다. `-I` 를 주면 이 목록에 그대로 끼어 들어가니, "헤더를 못 찾음" 디버깅의 첫 명령은 `-v` 다. `-H` 는 실제로 연 헤더를 깊이(점 개수)와 함께 보여준다 — `stdint.h` 가 clang 것 → SDK 것으로 `#include_next` 되는 것도 보인다. "엉뚱한 버전의 헤더를 집었다" 를 찾을 때 쓴다.

### Part B — `first_failing_stage` 와 단계 경계의 함정

정답 함수(6절)는 단계별 루프 4개다. `cc -S x.i` 처럼 앞 단계 산출물을 다음 단계 입력으로 주므로 **한 번에 한 단계만** 돈다. 그렇게 만든 `main.o` 가 `cc -c main.c` 결과와 바이트 단위로 같은 것도 확인했다 (`demo_stages.sh` 12절: `byte-identical: 1208 bytes`) — 나눠 돌려도 결과는 같고, 보이는 것만 늘어난다.

**`-fsyntax-only` 와 `-S` 의 차이** (`demo_stages.sh` 10절, 실측). `-fsyntax-only` 는 프론트엔드(전처리 + 구문 + 타입 검사)까지만 한다:

```text
file                     fsyntax-only   -S     first error at -S
bad_asm_mnemonic.c       rc=0           rc=1   unrecognized instruction mnemonic
bad_asm_org.c            rc=0           rc=0   
bad_attr_error.c         rc=0           rc=1   call to 'debug_dump' declared with 'error' attribute: debug_
bad_macro_paren.c        rc=1           rc=1   expected ')'
bad_undefined.c          rc=0           rc=0   
...
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">에러가 처음 보이는 지점 (이 맥 Apple clang 21 실측, gcc 차이는 점선)</text>
  <rect x="10" y="35" width="150" height="150" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="55" fill="currentColor" font-weight="bold">-E 전처리</text>
  <text x="20" y="77" fill="currentColor" font-size="12">#error</text>
  <text x="20" y="95" fill="currentColor" font-size="12">헤더 없음</text>
  <text x="20" y="113" fill="currentColor" font-size="12">#if 짝 없음</text>
  <text x="20" y="131" fill="currentColor" font-size="12">매크로 인자 수</text>
  <rect x="175" y="35" width="150" height="150" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="185" y="55" fill="currentColor" font-weight="bold">프론트엔드</text>
  <text x="185" y="77" fill="currentColor" font-size="12">; 빠짐 (문법)</text>
  <text x="185" y="95" fill="currentColor" font-size="12">struct→int (타입)</text>
  <text x="185" y="113" fill="currentColor" font-size="12">SQUARE( 괄호 ←매크로!</text>
  <text x="185" y="131" fill="currentColor" font-size="12">미선언 함수 호출</text>
  <rect x="340" y="35" width="150" height="150" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="350" y="55" fill="currentColor" font-weight="bold">백엔드 (codegen)</text>
  <text x="350" y="77" fill="currentColor" font-size="12">attribute error</text>
  <text x="350" y="95" fill="currentColor" font-size="12">인라인 asm 명령어</text>
  <text x="350" y="113" fill="currentColor" font-size="12">(clang 내장 as 가</text>
  <text x="350" y="131" fill="currentColor" font-size="12"> -S 때 파싱)</text>
  <rect x="505" y="35" width="105" height="150" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="515" y="55" fill="currentColor" font-weight="bold">-c 어셈블</text>
  <text x="515" y="77" fill="currentColor" font-size="12">.org 역행</text>
  <text x="515" y="95" fill="currentColor" font-size="12">(layout 때)</text>
  <rect x="625" y="35" width="105" height="150" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="635" y="55" fill="currentColor" font-weight="bold">링크</text>
  <text x="635" y="77" fill="currentColor" font-size="12">정의 없음</text>
  <text x="635" y="95" fill="currentColor" font-size="12">중복 정의</text>
  <text x="635" y="113" fill="currentColor" font-size="12">main 없음</text>
  <text x="635" y="131" fill="currentColor" font-size="12">.o 빠짐</text>
  <text x="635" y="149" fill="currentColor" font-size="12">static 정의</text>
  <path d="M415 140 C 460 205, 520 205, 555 188" stroke="currentColor" stroke-width="1.5" stroke-dasharray="4 3" fill="none"/>
  <text x="380" y="222" fill="currentColor" font-size="12">gcc: 인라인 asm 은 as 가 처음 읽음 → assemble</text>
  <path d="M175 200 L490 200" stroke="currentColor" stroke-width="1"/>
  <text x="185" y="216" fill="currentColor" font-size="12">-S 단계 = compile</text>
  <path d="M10 240 L325 240" stroke="currentColor" stroke-width="1"/>
  <text x="15" y="234" fill="currentColor" font-size="12">-fsyntax-only 가 보는 범위</text>
</svg>
```

함정 다섯 개:

1. **`bad_macro_paren.c`** — 매크로 정의에 괄호가 빠졌는데 `-E` 는 성공한다. 전처리기는 토큰을 치환할 뿐 C 문법을 모른다. 펼쳐진 `return ((n) * (n) - 9;` 를 파서가 읽을 때 터진다 → compile. 에러 위치가 매크로 사용 줄(7:26)로 나오는 것도 그래서다. (매크로 작성 규칙은 C-B15.)
2. **`bad_macro_args.c`** 는 반대로 진짜 전처리 에러: 인자 개수는 전처리기가 세기 때문 (`too few arguments provided to function-like macro invocation`).
3. **`bad_implicit.c`** — C89 에선 미선언 함수 호출이 허용됐다(`int f()` 로 가정) → 컴파일 통과, 링크에서 `undefined reference`. C99 부터 금지라 clang/gcc 최신판은 에러(GCC 14·Clang 16 부터 기본 에러로 바뀐 것으로 알고 있음 — 확인 권장). Apple clang 은 `-###` 에 `-Werror=implicit-function-declaration` 을 기본으로 넣는 것까지 실측 → 이 맥에선 `-std=c89` 여도 compile 에서 멈춘다(practice 체크). VM gcc `-std=c89 -Wall` 은 경고만 내고 링크에서 `undefined reference to 'flash_init'` (Part C).
4. **`bad_attr_error.c`** — `__attribute__((error("...")))` 는 "이 호출이 **최적화 후에도 남아 있으면**" 에러라서 백엔드가 판단한다 → `-fsyntax-only` rc=0, `-S` rc=1. 펌웨어에선 "릴리스 빌드에서 디버그 함수 호출 금지", "상수로 접혀야 할 경로가 남았으면 빌드 실패" 용도로 쓴다(리눅스 커널 `BUILD_BUG_ON` 계열의 한 구현 — 확인 권장).
5. **`bad_asm_org.c`** — `-S` 통과, `-c` 실패: `.org` 는 "현재 위치를 0x10 으로" 인데 이미 0x104(260)까지 왔다. 바이트를 배치해 봐야 아는 에러라 오브젝트를 쓸 때 나온다. 메시지 위치가 `$TMPDIR/bad_asm_org-5a967a.s:22:6` 인 것도 실측된 함정: **Apple clang 은 `.s` 도 전처리한다** (`-ccc-print-phases -c x.s` → `input, "x.s", assembler-with-cpp`). VM 의 upstream clang 21 은 `.s` → `assembler` 바로, `.S` 만 전처리 — 원래 규칙은 "대문자 `.S` 만 cpp".

**여러 파일은 단계 우선(stage-major).** `bad_asm_org.c bad_macro_args.c` 에서 starter(한 방 빌드)는 첫 파일의 `.org` 에러를 먼저 만나 멈췄다(파일 우선). 정답은 두 파일을 다 `-E` 해 보고 preprocess 를 보고한다. 실제 빌드에서도 `make -k` 로 전부 돌린 뒤 **가장 앞 단계 에러부터** 고치는 게 순서다 — 헤더 하나 못 찾으면 그 뒤 수십 개 에러는 그 결과일 뿐이다.

### Part C — 메시지 분류표와 GNU/ELF 쪽 (VM 실측)

**진단 메시지 → 단계 분류표** (전부 실제 출력에서 옮김):

| 단계 | Apple clang / ld64 (맥) | gcc 15 / GNU as / ld.bfd 2.46 (VM) | ld.lld (VM) |
|---|---|---|---|
| 전처리 | `fatal error: 'board_pins.h' file not found` · `error: "BOARD_REV not set..."` · `unterminated conditional directive` · `too few arguments provided to function-like macro invocation` | `fatal error: board_pins.h: No such file or directory` · `error: #error "BOARD_REV..."` · `error: unterminated #if` · `error: macro 'REG_FIELD' requires 3 arguments, but only 2 given` | — |
| 컴파일 | `expected ';' at end of declaration` · `initializing 'int' with an expression of incompatible type` · `call to undeclared function` · `declared with 'error' attribute` · `unrecognized instruction mnemonic` | `expected ',' or ';' before 'return'` · `incompatible types when initializing type 'int' using type 'struct status'` · `implicit declaration of function` · `declared with attribute error` | — |
| 어셈블 | `invalid .org offset '16' (at offset '260')` | `Error: attempt to move .org backwards` · `Error: unknown mnemonic 'frobnicate'` (대문자 `Error:` + `Assembler messages:`) | — |
| 링크: 정의 없음 | `Undefined symbols for architecture arm64:` / `"_flash_erase", referenced from: _main in bad_undefined-542928.o` / `ld: symbol(s) not found` | `in function 'main': bad_undefined.c:(.text+0xc): undefined reference to 'flash_erase'` | `error: undefined symbol: flash_erase` / `>>> referenced by bad_undefined.c` |
| 링크: 중복 | `duplicate symbol '_g_boot_count' in:` + .o 두 개 / `ld: 1 duplicate symbols` | `(.data+0x0): multiple definition of 'g_boot_count'; ...(.bss+0x0): first defined here` | `error: duplicate symbol: g_boot_count` |
| 링크: main 없음 | `"_main", referenced from: <initial-undefines>` | `Scrt1.o: in function '_start': (.text+0x1c): undefined reference to 'main'` | — |

읽는 요령: 파일명:줄:열 이 있으면 컴파일러(전처리 포함). `.o`/섹션+오프셋(`(.text+0xc)`)이 있으면 링커 — 링커는 소스 줄을 모르고 오브젝트 안 위치만 안다(줄 번호를 붙이는 건 디버그 정보가 있을 때, TOOL-M14). `clang: error: linker command failed` / `collect2: error: ld returned 1 exit status` 는 드라이버의 **요약**일 뿐 원인은 그 위 줄들이다. GNU ld 의 `(.bss+0x0): first defined here` 는 `= 0` 으로 초기화한 `bad_dup_a.c` 쪽이 `.bss` 에 들어갔다는 뜻이다(0 초기화 = bss, C-B11).

**gcc vs clang: 결과가 다른 한 칸** (`demo_vm.sh` 7절, 같은 `solution.sh` 를 VM 에서):

```text
input                        gcc-15     clang-21  
bad_error.c                  preprocess preprocess
...
bad_attr_error.c             compile    compile   
bad_asm_mnemonic.c           assemble   compile   
bad_asm_org.c                assemble   assemble  
bad_undefined.c              link       link      
...
main.c util.c                ok         ok        
--- gcc's message for bad_asm_mnemonic.c
/tmp/TOOL-B01_work/vm/stage/bad_asm_mnemonic.s:12: Error: unknown mnemonic `frobnicate' -- `frobnicate x0,x1'
```

gcc 는 인라인 asm 문자열을 해석하지 않고 `.s` 에 그대로 복사한다(`gcc -S rc=0`, `12:	frobnicate x0, x1`). 그래서 GNU as 가 처음 본다 → assemble, 에러 위치도 `.s` 줄. clang 은 `-S` 때 이미 내장 어셈블러로 파싱 → compile, 위치도 `.c` 줄. `clang -fno-integrated-as -S` 는 rc=0 (맥·VM 둘 다 실측) — 외부 어셈블러에 맡기면 clang 도 gcc 처럼 된다. 결론: "어느 단계" 는 언어 규칙만이 아니라 **툴체인 구현**에도 달렸다. 분류기는 툴체인별 기대표를 따로 가져야 한다.

**C89 에서는 단계가 바뀐다** (VM 실측):

```text
bad_implicit.c:5:12: warning: implicit declaration of function ‘flash_init’ [-Wimplicit-function-declaration]
gcc -std=c89 -Wall -c rc=0
bad_implicit.c:(.text+0xc): undefined reference to `flash_init'
bad_implicit.c:5:12: error: implicit declaration of function ‘flash_init’ [-Wimplicit-function-declaration]
gcc -std=c11 -c rc=1
```

오래된 펌웨어 코드베이스(`-std=gnu89`)에서 "링크 에러인데 사실은 헤더 include 빠짐" 이 이렇게 생긴다 — 그리고 반환형이 `int` 로 가정되어 포인터 반환 함수면 상위 32비트가 잘리는 런타임 버그까지 된다.

**펌웨어 크로스 컴파일러로 같은 4단계** (`demo_vm.sh` 9절):

```text
arm-none-eabi-gcc (15:14.2.rel1-1) 14.2.1 20241119
util.c  util.h  util.i  util.o  util.s          (ls -1 을 한 줄로)
util.o: ELF 32-bit LSB relocatable, ARM, EABI5 version 1 (SYSV), not stripped
--- util.s: sectors_for
sectors_for:
	@ ... (주석 3줄)
	addw	r0, r0, #511
	lsrs	r0, r0, #9
	bx	lr
--- link util.o alone (newlib crt0 wants main)
undefined reference to `main'
--- link main.c util.c with newlib-nano + nosys stubs
warning: _close is not implemented and will always fail     (+ _fstat _isatty _lseek _read _write, 6줄)
fw.elf: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV), statically linked, with debug_info, 
   text	   data	    bss	    dec	    hex	filename
   7808	    108	    504	   8420	   20e4	fw.elf
```

- `SECTORS_FOR(b)` = `(b + 511) / 512` 가 Thumb-2 명령 두 개(`addw` + `lsrs #9`)가 됐다 — 매크로는 `-E` 에서 사라지고, 나눗셈은 `-S` 에서 시프트가 된다.
- "main 없음" 은 여기서도 링크 에러지만 범인이 다르다: newlib 의 `crt0.o`(`_mainCRTStartup`)가 `main` 을 부른다. 실제 MCU 펌웨어는 보통 자기 startup(`Reset_Handler` + 벡터 테이블, BM-B08)과 링커 스크립트(TOOL-M01)를 쓰고, 그 경우 `main` 을 부르는 건 내 startup 코드다.
- `--specs=nosys.specs` 경고 6줄은 printf 가 결국 `_write` 시스템콜 스텁을 찾는다는 뜻 — 실제 보드에선 `_write` 를 UART 로 구현한다. 7808 B 의 대부분은 printf 다(크기 읽기는 TOOL-B06, 다이어트는 TOOL-M13).

## 6. 정답 코드

<!-- code: solution.sh diff=starter.sh -->

```text
$ bash check.sh solution        # starter 에서 FAIL 이던 10줄 + 요약만 옮김 (나머지 12줄은 같은 PASS)
PASS  bad_error.c                        -> preprocess | bad_error.c:3:2: error: "BOARD_REV not set: build with -DBOARD_REV=2"
PASS  bad_missing_header.c               -> preprocess | bad_missing_header.c:2:10: fatal error: 'board_pins.h' file not found
PASS  bad_unterminated_if.c              -> preprocess | bad_unterminated_if.c:3:2: error: unterminated conditional directive
PASS  bad_macro_args.c                   -> preprocess | bad_macro_args.c:6:32: error: too few arguments provided to function-lik
PASS  bad_asm_org.c                      -> assemble   | $TMPDIR/bad_asm_org-5a967a.s:22:6: error: invalid .org offset '16' (at o
PASS  bad_undefined.c                    -> link       | Undefined symbols for architecture arm64:
PASS  bad_dup_a.c bad_dup_b.c            -> link       | duplicate symbol '_g_boot_count' in:
PASS  bad_no_main.c                      -> link       | Undefined symbols for architecture arm64:
PASS  main.c                             -> link       | Undefined symbols for architecture arm64:
PASS  bad_asm_org.c bad_macro_args.c     -> preprocess | bad_macro_args.c:6:32: error: too few arguments provided to function-lik
22/22 passed
```

왜 이렇게 짰나:

- **앞 단계 산출물을 다음 입력으로** (`-S "$b.i"`, `-c "$b.s"`): `.i` 를 주면 드라이버가 전처리를 건너뛰므로 "이번 실패는 이 단계" 가 보장된다. `cc -S x.c` 로 하면 전처리 에러도 compile 로 잘못 분류된다.
- `$flags` 는 `-E` 와 `-S` 둘 다에 준다: `-D` 는 `-E` 에만 의미가 있지만 `-std=`·경고 옵션은 `-S` 에서 쓰인다. `-c`·링크엔 안 준다.
- `return 0` + 단어 하나: 함수의 "결과" 는 stdout, 성공/실패 코드는 쓰지 않는다(분류 자체는 성공). 인자 없음 같은 **사용법 오류만** `return 2`.
- `$cc` 따옴표 없이: `CC="ccache clang"` 같은 값도 쪼개져 동작하게. 대신 경로에 공백이 있으면 안 된다(이 레슨 범위 밖).
- 스크래치는 `$STAGE_DIR` 에만. 검사기는 레슨 폴더 엔트리 수를 앞뒤로 세서 확인한다.

## 7. 자주 하는 실수

- **`cc -S file.c` 로 컴파일 단계를 검사** → 전처리 에러까지 compile 로 분류. 고침: `-E` 로 `.i` 를 먼저 만들고 `-S x.i`.
- **파일 순서대로 끝까지** 돌림 → `bad_asm_org.c bad_macro_args.c` 에서 assemble 보고. 고침: 단계마다 모든 파일을 도는 루프 4개.
- **도구 출력을 stdout 에 흘림** → `got` 에 에러 문장이 섞여 `PASS` 비교 실패. 고침: 모든 도구 호출에 `>&2`.
- **"링크 에러니까 .c 를 고친다"** → `main.c` 단독 링크는 코드가 아니라 **빌드 명령**(링크 라인에 `util.o` 누락) 문제다. `Undefined symbols` 를 보면 먼저 "그 심볼을 정의한 `.o`/라이브러리가 링크 라인에 있나" 를 `nm` 으로 본다 (`nm util.o | grep sectors_for` → `T`).
- **헤더에 변수를 정의** (`int g_boot_count = 0;` 을 `.h` 에) → 그 헤더를 include 한 모든 `.c` 가 정의를 가져 duplicate symbol. 고침: 헤더엔 `extern int g_boot_count;`, 정의는 `.c` 한 곳 (TOOL-B05 에서 깊게).
- **`-fsyntax-only` 통과 = 빌드 통과로 착각** (에디터/LSP 가 빨간 줄을 안 띄움) → attribute error, 인라인 asm, `.org` 는 빌드에서 처음 터진다. CI 는 실제 `-c` + 링크까지 해야 한다.
- **bash 3.2 + `set -u` 에서 빈 배열** `local objs=(); ... "${objs[@]}"` → 맥 기본 bash 에서 `unbound variable`. 고침: 문자열 누적 또는 `${objs[@]+"${objs[@]}"}`.

## 8. 면접 Q&A

<details>
<summary>Q1. C 소스가 실행 파일이 되기까지 단계별 입력·출력·도구를 말해 보세요.</summary>

전처리(cpp): `.c` → `.i`, `#include` 붙이기·매크로 치환·`#if` 선택·주석 제거, 결과는 C 텍스트. 컴파일(cc1 / clang -cc1): `.i` → `.s`, 구문·타입 검사, 최적화, 명령어 선택. 어셈블(as): `.s` → `.o`, 기계어 인코딩 + 심볼 테이블 + 재배치. 링크(ld): `.o`들 + 라이브러리 → 실행 파일, 섹션 합치기·주소 배정·심볼 해석·재배치 적용. 꼬리: "clang 은 실제로 프로세스가 몇 개?" → `-###` 로 보면 파일당 `clang -cc1 -emit-obj` 하나 + `ld` 하나 (이 맥 실측). gcc 는 `cc1`, `as`, `collect2→ld` 가 따로.

</details>

<details>
<summary>Q2. "undefined reference to `foo`" 는 컴파일 에러인가요? 왜 컴파일러는 못 잡나요?</summary>

링크 에러다. 컴파일러는 번역 단위 하나만 본다 — 선언(`int foo(void);`)만 있으면 "어딘가에 있겠지" 하고 호출 자리에 재배치(`ARM64_RELOC_BRANCH26 _foo` / `R_AARCH64_CALL26`)를 남긴다. 정의가 정말 어디에도 없다는 건 모든 `.o` 와 라이브러리를 모은 링커만 안다. 원인 후보: 정의 안 함 / 이름 오타 / 정의한 `.o` 나 `-l` 이 링크 라인에 없음 / 정의가 `static` / C++ 이름 맹글링(TOOL-B05) / 라이브러리 순서(TOOL-B09).

</details>

<details>
<summary>Q3. `#include` 는 실제로 무엇을 하나요? 헤더 가드는 왜 필요하죠?</summary>

그 파일의 텍스트를 그 자리에 복사해 붙인다 — 그 이상도 이하도 아니다 (`main.c` 14줄 → `main.i` 643줄). 같은 헤더가 두 경로로 들어오면(main.c → util.h → config.h, 다른 곳에서도 config.h) 정의가 중복되므로 `#ifndef CONFIG_H / #define / #endif` 가드로 두 번째부터 빈 내용이 되게 한다. 가드는 **한 번역 단위 안**의 중복만 막는다 — 헤더에 변수 정의가 있으면 `.c` 두 개가 각각 정의를 가져 링크 단계 중복은 그대로 난다.

</details>

<details>
<summary>Q4. 재배치(relocation)가 뭔가요? `.o` 의 함수 주소가 왜 0 인가요?</summary>

`.o` 를 만들 때는 최종 주소를 모른다(다른 `.o` 와 어떻게 합쳐질지 모름). 그래서 각 섹션을 0 기준으로 만들고, 주소가 필요한 명령 자리마다 "오프셋 0x20 의 `bl` 에 `_sectors_for` 주소를 BRANCH26 방식으로 넣어라" 같은 기록을 남긴다. 링커가 주소를 정한 뒤 이 구멍들을 채운다. 실측: `main.o` 재배치 12개, `fw` 에서 `_sectors_for = 0x100000518`. 펌웨어에선 링커 스크립트가 Flash 주소(예: 0x08000000)를 정하는 게 이 단계다(TOOL-M01).

</details>

<details>
<summary>Q5. `-fsyntax-only` (또는 에디터 진단)이 깨끗한데 빌드가 실패할 수 있나요?</summary>

있다. 실측한 세 가지: `__attribute__((error))` 호출(백엔드가 판단), clang 의 인라인 asm 명령어 검사(`-S` 때 내장 어셈블러), `.org` 역행 같은 어셈블러 layout 에러. 그리고 링크 에러 전부. `-fsyntax-only` 는 프론트엔드까지만 돈다. 그래서 pre-commit 훅을 `-fsyntax-only` 로 가볍게 하더라도 CI 는 실제 빌드+링크를 해야 한다.

</details>

<details>
<summary>Q6. 같은 코드가 gcc 와 clang 에서 다른 단계에서 실패할 수 있나요?</summary>

있다 — 이 레슨의 `bad_asm_mnemonic.c`: gcc 는 인라인 asm 을 텍스트로 `.s` 에 복사하고 GNU as 가 `Error: unknown mnemonic` (assemble), clang 은 `-S` 때 파싱해서 `unrecognized instruction mnemonic` (compile). `-fno-integrated-as` 를 주면 clang 도 `-S` 를 통과한다. 그 외에 언어 모드 차이(C89 의 암시적 함수 선언 → 링크까지 감), Apple clang 의 기본 `-Werror=implicit-function-declaration` 처럼 배포판 기본값 차이도 있다.

</details>

<details>
<summary>Q7. `-save-temps`, `-v`, `-###`, `-H` 는 각각 언제 쓰나요?</summary>

`-save-temps`: 매크로가 어떻게 펼쳐졌는지(`.i`), 최적화된 코드(`.s`)를 같은 빌드 옵션으로 보고 싶을 때 — 빌드 시스템이 넘긴 플래그 그대로 중간 산출물을 남긴다. `-v`: include 검색 경로, 실제 실행된 cc1/as/ld 명령 (헤더 못 찾음, 엉뚱한 sysroot). `-###`: 실행하지 않고 명령만 — 크로스 툴체인이 어떤 링커·crt 파일·라이브러리를 고르는지 확인 (TOOL-B14). `-H`: 실제로 연 헤더 트리 — 빌드 시간 원인(헤더 폭발), 잘못된 헤더 버전 선택.

</details>

<details>
<summary>Q8. Cortex-M 펌웨어에서 "main 이 없다" 링크 에러는 리눅스 앱과 뭐가 다른가요?</summary>

리눅스 앱은 `Scrt1.o` 의 `_start` 가 `main` 을 참조한다(VM 실측 `undefined reference to 'main'` in `_start`). `arm-none-eabi-gcc` 기본 링크는 newlib 의 `crt0.o` `_mainCRTStartup` 이 참조한다(실측). 실제 MCU 프로젝트는 보통 `-nostartfiles` 로 그 crt0 을 빼고 자기 startup(`Reset_Handler`: `.data` 복사, `.bss` 0 채움, 그다음 `main` 호출 — BM-B07/BM-B08)과 벡터 테이블을 링크한다. 그러면 `main` 을 참조하는 쪽은 내 `startup.c` 다. 꼬리: "벡터 테이블이 링크에서 빠지면?" → 아무도 참조하지 않으면 `--gc-sections` 가 지울 수 있어 링커 스크립트에서 `KEEP()` 한다(TOOL-M01/A07).

</details>

## 9. 추가 연습

### 연습 1 — `stage_artifact_kind`: 파일 내용으로 단계 판별

확장자를 믿지 말고 내용으로 `.i/.bc/.s/.o/실행 파일` 을 구분하라: `source | preprocessed | bitcode | asm | object | executable | unknown`. `main.o` 를 `firmware.bin` 으로, `main.s` 를 `notes.txt` 로 바꿔도 맞혀야 한다. Mach-O 와 ELF(aarch64, Cortex-M4) 오브젝트 모두.

<details>
<summary>답 보기</summary>

앞 4바이트 매직(magic): `cf fa ed fe` = Mach-O 64 (LE) → 오프셋 12 의 `filetype` 1 = `MH_OBJECT`, 2 = `MH_EXECUTE`. `7f 45 4c 46` = ELF → 오프셋 16 의 `e_type` 1 = `ET_REL`(.o), 2 = `ET_EXEC`, 3 = `ET_DYN` (VM 의 `fw` 는 `pie executable` 이라 3. 공유 라이브러리도 3 이라 구분하려면 `PT_INTERP` 등을 봐야 함 — 헤더 파싱은 TOOL-B14/A11). `42 43 c0 de` / `de c0 17 0b` = LLVM 비트코드(이 맥 `-save-temps` 의 `.bc` 는 `file`: `LLVM bitcode, wrapper`). 텍스트면 첫 줄이 `# 숫자 "` 인 linemarker → preprocessed(clang `# 1`, gcc `# 0` 둘 다), `.text/.section/.globl…` 지시어가 있으면 asm, NUL 바이트가 있으면 unknown, 나머지 source. 실측 표는 아래 practice 출력.

</details>

### 연습 2 — 다른 파일의 `static` 함수 부르기

`bad_static_main.c` 는 `unsigned crc_update(unsigned, unsigned char);` 를 선언하고 호출, `bad_static_helper.c` 는 `static unsigned crc_update(...)` 를 정의한다. 어느 단계? 메시지는?

<details>
<summary>답 보기</summary>

link. 선언이 있으니 컴파일은 통과, helper 쪽 `crc_update` 는 내부 링키지(internal linkage)라 `nm` 에서 소문자 `t` 이고 링커의 심볼 해석 대상이 아니다 → `Undefined symbols for architecture arm64:` (`_crc_update`). "정의가 분명히 있는데 undefined" 일 때 `static` 을 의심하라 (C-B10).

</details>

### 연습 3 — C89 로 `bad_implicit.c`

`STAGE_CFLAGS="-std=c89 -Wall"` 로 돌리면 맥과 VM gcc 에서 각각 어느 단계?

<details>
<summary>답 보기</summary>

맥 Apple clang: compile (`implicit declaration of function 'flash_init'` — Apple 기본 `-Werror=implicit-function-declaration`, practice 체크에서 실측). VM gcc 15: 경고만 내고 `-c` rc=0 → link (`undefined reference to 'flash_init'`, Part C 실측). 같은 소스·같은 `-std` 여도 배포판 기본 경고 설정이 단계를 바꾼다.

</details>

### 연습 4 — 한 글자로 각 단계 깨기

정상 `main.c util.c` 에서 **한 곳만** 고쳐서 preprocess / compile / assemble / link 실패를 각각 만들어 보라.

<details>
<summary>답 보기</summary>

preprocess: `#include "util.h"` → `"utl.h"`. compile: `LOG("boot");` 의 `;` 삭제. assemble: clang 에선 `-S` 가 인라인 asm 을 검사하므로 `.org` 역행 같은 layout 에러가 필요(`bad_asm_org.c` 방식). link: `util.c` 의 `uint32_t sectors_for` 앞에 `static` 추가(연습 2와 같은 원리) 또는 `util.c` 를 명령에서 뺌.

</details>

### 한 번에 검증

<!-- code: practice.sh diff=solution.sh -->

```bash
bash check.sh practice
```

```text
== extra snippets
PASS  bad_static_main.c bad_static_helper.c -> link       | Undefined symbols for architecture arm64:
PASS  bad_implicit.c                     -> compile    | bad_implicit.c:5:12: error: implicit declaration of function 'flash_init
== stage_artifact_kind (decided by content; file(1) shown for comparison)
PASS  main.c         -> source       | file: c program text, Unicode text, UTF-8 text
PASS  config.h       -> source       | file: c program text, Unicode text, UTF-8 text
PASS  main.i         -> preprocessed | file: c program text, ASCII text
PASS  main.bc        -> bitcode      | file: LLVM bitcode, wrapper
PASS  main.s         -> asm          | file: assembler source text, ASCII text
PASS  main.o         -> object       | file: Mach-O 64-bit object arm64
PASS  fw             -> executable   | file: Mach-O 64-bit executable arm64
PASS  firmware.bin   -> object       | file: Mach-O 64-bit object arm64
PASS  notes.txt      -> asm          | file: assembler source text, ASCII text
PASS  util_a64.o     -> object       | file: ELF 64-bit LSB relocatable, ARM aarch64, version
PASS  util_m4.o      -> object       | file: ELF 32-bit LSB relocatable, ARM, EABI5 version 1
PASS  empty.dat      -> unknown      | file: empty
PASS  blob.bin       -> unknown      | file: data
PASS  lesson dir untouched (30 entries)
37/37 passed
```

(앞부분 22개는 solution 과 같은 출력이라 생략.)

## 10. 핵심 정리

- `-E` .c→.i (텍스트, 매크로·`#include` 처리) / `-S` .i→.s (구문·타입·codegen) / `-c` .s→.o (인코딩 + 재배치) / 링크 .o→exe (주소 배정 + 심볼 해석).
- 파일:줄:열 이 붙은 에러 = 컴파일러(전처리 포함). `(.text+0xc)` / `referenced from` / `Undefined symbols` / `undefined reference` / `duplicate symbol` / `multiple definition` = 링커.
- 매크로 **정의** 버그는 `-E` 를 통과하고 펼쳐진 뒤 컴파일에서 터진다. 매크로 **인자 개수**는 전처리 에러.
- `-fsyntax-only` 통과 ≠ 빌드 통과: attribute error, 인라인 asm(clang), 어셈블러 layout, 링크 에러는 그 뒤에 나온다.
- clang = 한 `-cc1` 프로세스 + 내장 어셈블러(인라인 asm 을 `-S` 때 검사), gcc = `cc1` → `as` → `collect2/ld` (인라인 asm 은 as 가 처음 봄).
- 진단 도구: `-save-temps`(중간 파일), `-v`(검색 경로·실제 명령), `-###`(드라이버 계획), `-H`(헤더 트리), `-ccc-print-phases`(논리 단계).
- 여러 파일은 단계 우선으로 — 가장 앞 단계 에러부터 고친다.
- 펌웨어: `main` 을 부르는 건 crt0 이 아니라 내 `Reset_Handler`, 링크 산출물 크기는 `arm-none-eabi-size` 로 본다.

## 참고자료

- [GCC Manual — Overall Options (-E, -S, -c, -save-temps, -v, -###)](https://gcc.gnu.org/onlinedocs/gcc/Overall-Options.html)
- [GCC CPP Manual — Preprocessor Output (linemarker flags 1/2/3/4)](https://gcc.gnu.org/onlinedocs/cpp/Preprocessor-Output.html)
- [Clang Driver Internals (phases, -ccc-print-phases, -###)](https://clang.llvm.org/docs/DriverInternals.html)
- [Clang Command Line Reference — -fsyntax-only, -save-temps, -fintegrated-as](https://clang.llvm.org/docs/ClangCommandLineReference.html)
- [GCC — Common Function Attributes: error / warning](https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html)
- [GNU as — .org directive](https://sourceware.org/binutils/docs/as/Org.html)
- John R. Levine, *Linkers and Loaders* — 1~4장 (오브젝트 파일, 심볼 해석, 재배치)
- C11 표준 5.1.1.2 Translation phases — 전처리(1~4) 와 문자열 연결(6)의 순서
