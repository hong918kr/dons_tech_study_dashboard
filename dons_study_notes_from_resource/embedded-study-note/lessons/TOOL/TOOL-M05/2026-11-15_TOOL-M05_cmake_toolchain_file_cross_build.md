---
id: TOOL-M05
time: 75분
updated: 2026-11-15
---
# TOOL-M05 CMake 툴체인 파일로 크로스 빌드

## 학습 목표

- 소스 트리 하나를 **빌드 트리 두 개**(`build-host` = 맥 clang + 단위 테스트, `build-target` = arm-none-eabi-gcc + 펌웨어 이미지)로 나누고, 그 경계를 `CMAKE_TOOLCHAIN_FILE` 한 줄로 바꾼다.
- 툴체인 파일(toolchain file)의 각 줄 — `CMAKE_SYSTEM_NAME Generic`, 컴파일러, `CMAKE_TRY_COMPILE_TARGET_TYPE`, `CMAKE_<LANG>_FLAGS_INIT`, `CMAKE_FIND_ROOT_PATH_MODE_*` — 이 configure 의 **어느 순간에** 왜 필요한지 실측으로 설명한다 (툴체인 파일이 configure 한 번에 **3 번** 읽힌다는 것 포함).
- 타깃 빌드에서 `.elf / .bin / .map / size` 가 **POST_BUILD 로 자동 생성**되게 하고, `readelf -h/-S/-l`, `.bin` 크기 산수로 결과를 검증한다.
- 실제로 무는 함정 4 개 — try_compile 링크 실패, `CMAKE_C_FLAGS` 를 덮어써 사용자 플래그가 사라짐, 링커 스크립트 의존성 누락, Apple Silicon 에서 `"arm64" MATCHES "arm"` — 을 재현하고 고친다.

## 1. 문제

펌웨어 팀의 표준 질문 두 개: "단위 테스트는 PC 에서, 펌웨어는 보드용으로 — **같은 소스**를 어떻게 두 번 빌드하나요?" 그리고 "CMake 에서 크로스 컴파일은 어떻게 설정하나요?" 면접에서는 툴체인 파일 한 장을 보여 주고 "이 줄은 왜 있죠?" 를 줄마다 묻는 식으로 나온다. SSD 컨트롤러 펌웨어처럼 호스트 시뮬레이션 + 타깃 이미지를 같이 굴리는 프로젝트에서는 매일 쓰는 구조다.

CMake 는 **configure 시점에** "이 트리는 어떤 시스템/컴파일러용인가" 를 한 번 정하고 `CMakeCache.txt` 에 박는다. 그래서 호스트와 타깃은 같은 폴더에 섞을 수 없고, 빌드 트리를 둘로 나눈 뒤 타깃 쪽에만 `-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake` 를 준다. TOOL-M04 가 "한 빌드 트리 안의 타깃 설계" 였다면, 이 레슨은 "**빌드 트리 자체를 어떤 툴체인으로 만들 것인가**" 다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m5a" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">소스 트리 하나 → 빌드 트리 둘 (configure 때 툴체인이 고정된다)</text>
  <rect x="255" y="34" width="230" height="104" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="370" y="54" fill="currentColor" text-anchor="middle" font-weight="bold">src/ (git)</text>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="268" y="74">lib/  crc.c filter.c dsp.h</text>
    <text x="268" y="92">tests/ test_dsp.c (stdio)</text>
    <text x="268" y="110">fw/  startup main .ld</text>
    <text x="268" y="128">cmake/arm-none-eabi.cmake</text>
  </g>
  <path d="M255 100 L150 168" stroke="currentColor" marker-end="url(#m5a)"/>
  <path d="M485 100 L590 168" stroke="currentColor" marker-end="url(#m5a)"/>
  <text x="120" y="140" fill="currentColor" font-size="12" font-family="monospace">cmake -B build-host</text>
  <text x="480" y="140" fill="currentColor" font-size="12" font-family="monospace">-DCMAKE_TOOLCHAIN_FILE=…</text>
  <rect x="20" y="170" width="300" height="150" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="170" y="190" fill="currentColor" text-anchor="middle" font-weight="bold">build-host  (Darwin / arm64)</text>
  <g font-size="12" fill="currentColor">
    <text x="34" y="212">컴파일러: AppleClang 21 (/usr/bin/cc)</text>
    <text x="34" y="232" font-family="monospace">libdsp.a  (Mach-O arm64)</text>
    <text x="34" y="252" font-family="monospace">test_dsp → ctest: 3 tests passed</text>
    <text x="34" y="272">CMAKE_CROSSCOMPILING = FALSE</text>
    <text x="34" y="292">펌웨어 타깃 없음 (fw/ 는 안 봄)</text>
  </g>
  <rect x="420" y="170" width="300" height="150" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="570" y="190" fill="currentColor" text-anchor="middle" font-weight="bold">build-target  (Generic / arm)</text>
  <g font-size="12" fill="currentColor">
    <text x="434" y="212">컴파일러: arm-none-eabi-gcc 16.2</text>
    <text x="434" y="232" font-family="monospace">libdsp.a  (ELF32 ARM)</text>
    <text x="434" y="252" font-family="monospace">fw.elf → fw.bin 504 B, fw.map,</text>
    <text x="434" y="270" font-family="monospace">        fw.size.txt (POST_BUILD)</text>
    <text x="434" y="292">CMAKE_CROSSCOMPILING = TRUE, 테스트 없음</text>
  </g>
</svg>
```

트리 (`proj/` 는 공통 소스, 변형(variant) 폴더는 CMake 파일만 다르다):

| 경로 | 내용 | 호스트 | 타깃 |
|---|---|---|---|
| `lib/include/dsp.h`, `lib/src/{crc,filter}.c` | CRC-16/CCITT-FALSE + 이동 평균. `<stdint.h>/<stddef.h>` 만 씀 | ✓ | ✓ |
| `tests/test_dsp.c` | `<stdio.h>` 쓰는 단위 테스트 (`crc`/`mavg`/`wrap` 3 케이스) | ✓ | ✗ (libc 없음) |
| `fw/startup.c`, `fw/main.c`, `fw/libc_min.c` | 벡터 표 + Reset_Handler, main, memset/memcpy | ✗ | ✓ |
| `fw/stm32f4.ld` | FLASH 512K @0x08000000, RAM 128K @0x20000000 (문법은 TOOL-M01) | ✗ | ✓ |
| `CMakeLists.txt`, `cmake/arm-none-eabi.cmake` | **고칠 파일** (`starter/` → `solution/`) | | |

이 맥의 `arm-none-eabi-gcc 16.2.0` (Homebrew) 은 **newlib 이 없다** — `crt0.o`, `libc.a`, `<stdio.h>` 가 없다. 그래서 펌웨어는 `-nostdlib -ffreestanding` + 자체 startup/memset 으로 빌드하고, newlib(-nano) 이 필요한 실험은 VM 의 gcc 14 로 한다 (Part C).

### Part A — 툴체인 파일과 두 빌드 트리

- `cmake/arm-none-eabi.cmake` 를 완성해 `cmake -S . -B build-target -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake` 가 성공하게 한다: `CMAKE_SYSTEM_NAME Generic`, `CMAKE_SYSTEM_PROCESSOR arm`, 컴파일러, `CMAKE_TRY_COMPILE_TARGET_TYPE`, CPU 플래그(`-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16`), `CMAKE_FIND_ROOT_PATH_MODE_*`.
- 링커 스크립트·`-nostdlib`·맵 파일은 **프로젝트 쪽**(`target_link_options`)에 둔다 — 툴체인 파일은 "CPU 를 위해 어떻게 컴파일하나", 프로젝트는 "이 보드 이미지를 어떻게 링크하나".
- 타깃 빌드는 POST_BUILD 로 `fw.bin`(objcopy), `fw.size.txt`(size) 를 만들고, 맵 파일은 링크 때 `-Wl,-Map=` 으로.

### Part B — 함정 4 개 (starter 에 심어 둔 것)

1. 타깃 configure 가 `is not able to compile a simple test program` 으로 죽는다.
2. 보드 엔지니어가 `CFLAGS=-DBOARD_REV=3 cmake ...` 로 구성했는데 이미지 안의 `fw_board_rev` 가 여전히 1 이다 (캐시에는 `-DBOARD_REV=3` 이 **보이는데도**).
3. 16 KB 부트로더가 생겨 링커 스크립트의 FLASH ORIGIN 을 `0x08004000` 으로 고쳤는데 `ninja` 가 `no work to do` — 옛 주소의 이미지가 그대로 나간다.
4. 이 Apple Silicon 맥에서 호스트 `ctest` 가 `No tests were found!!!` (인텔 CI 에서는 됐다).

### Part C — 펌웨어 현실

- `.bin` 크기 = `.data` 의 LMA 끝 − 0x08000000 이 되는지 산수로 확인하고, `readelf -l` 의 VirtAddr/PhysAddr 로 `.data` 가 Flash 에 실려 RAM 으로 복사되는 구조를 본다.
- VM(gcc 14 + newlib) 에서 같은 트리를 빌드하고 `--specs=nano.specs --specs=nosys.specs` 를 툴체인 파일에 넣었을 때 무엇이 달라지는지 본다 (try_compile, `end` 심볼, 시스템 콜 스텁).
- CI 관점: 두 빌드 트리, 프리셋(preset), 크기 예산 게이트 (9. 추가 연습).

## 2. Problem (English)

> **Make one firmware repository build for two machines.**
>
> The repo has a portable library (`lib/`), host-only unit tests (`tests/`, they use `<stdio.h>`), and a bare-metal Cortex-M4 image (`fw/`: startup, `main.c`, `stm32f4.ld`). The host compiler is Apple clang; the target compiler is `arm-none-eabi-gcc` **without a C library**.
>
> Write `cmake/arm-none-eabi.cmake` and fix `CMakeLists.txt` so that:
> 1. `cmake -B build-host -G Ninja && ninja -C build-host && ctest --test-dir build-host` runs 3 unit tests; no ARM flag appears in the host build.
> 2. `cmake -B build-target -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake` configures with no workaround, and `ninja -C build-target` produces `fw.elf` (ELF32 ARM, entry = `Reset_Handler|1`), `fw.bin`, `fw.map`, `fw.size.txt`; tests are not built there.
> 3. Extra flags given the standard way (`CFLAGS=-DBOARD_REV=3` on the first configure) reach the compiler, and CPU flags appear exactly once per compile command.
> 4. Editing only the linker script triggers a relink.
>
> Constraints: CPU flags live in the toolchain file; the linker script, `-nostdlib` and the map file live in the project. Do not use `CMAKE_C_COMPILER_WORKS`. Follow-ups: why does CMake try to link a test program; what is the difference between `CMAKE_C_FLAGS` and `CMAKE_C_FLAGS_INIT`; how many times is a toolchain file read; why is `CMAKE_SYSTEM_PROCESSOR` a bad switch on Apple Silicon; what do `CMAKE_FIND_ROOT_PATH_MODE_*` protect you from.

Expected target build tail:

```text
[7/7] Linking C executable fw.elf; fw.elf -> fw.bin + fw.size.txt
   text	   data	    bss	    dec	    hex	filename
    500	      4	     36	    540	    21c	.../build-target/fw.elf
```

## 3. 시작 코드

레슨 폴더:

```text
TOOL-M05/
  proj/{lib,tests,fw}/...                     # 공통 소스 (모든 변형이 공유)
  starter/  CMakeLists.txt  cmake/arm-none-eabi.cmake      # TODO 4 개
  solution/ CMakeLists.txt  cmake/arm-none-eabi.cmake
  practice/ CMakeLists.txt  cmake/{arm-none-eabi,fw_size_check}.cmake  CMakePresets.json
  check.sh                                     # 체커 (맥 전용, 3–7 초)
  demo_target.sh demo_flags.sh demo_vm.sh      # 관찰용 (+ *_out.txt 에 실제 출력)
```

고칠 툴체인 파일 (`starter/cmake/arm-none-eabi.cmake`, TODO 1–2):

<!-- code: starter/cmake/arm-none-eabi.cmake -->

고칠 프로젝트 파일 (`starter/CMakeLists.txt`, TODO 3–4):

<!-- code: starter/CMakeLists.txt -->

주어진 링커 스크립트 (해설은 TOOL-M01, 부팅은 BM-B08):

<!-- code: proj/fw/stm32f4.ld -->

체커 — 변형을 `/tmp/TOOL-M05_work/<variant>/src` 로 복사해 진짜 cmake 4.5 / ninja 1.13 / arm-none-eabi 16.2 로 두 트리를 만들고, `CMakeSystem.cmake`, `compile_commands.json`, `readelf`, `nm`, `arm-none-eabi-gdb -batch` (이미지 속 `fw_board_rev` 값 읽기) 로 판정한다. T1 이 실패하면 나머지를 계속 채점하려고 `-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY` 를 붙여 다시 configure 한다 (그 사실을 출력한다).

<!-- code: check.sh -->

```bash
bash check.sh starter     # FAIL 4 개, exit 1
bash check.sh solution    # 12/12, exit 0
bash check.sh practice    # 18/18, exit 0  (9. 추가 연습)
```

`bash check.sh starter` 실제 출력:

```text
== starter: cmake version 4.5.0, ninja 1.13.2, arm-none-eabi-gcc (GCC) 16.2.0 ==
-- host (Apple clang) --
PASS H1 host configure: CMAKE_SYSTEM_PROCESSOR=arm64, CMAKE_CROSSCOMPILING=FALSE
FAIL H2 host unit tests (want 3 tests, 100% passed)
     No tests were found!!!
PASS H3 host tree clean: no -mcpu/-mthumb/arm-none-eabi in compile_commands, lib objects are Mach-O arm64
-- target (toolchain file cmake/arm-none-eabi.cmake) --
FAIL T1 target configure with toolchain file
       is not able to compile a simple test program.
     /opt/homebrew/opt/arm-none-eabi-binutils/bin/arm-none-eabi-ld: cannot find crt0.o: Invalid argument
     /opt/homebrew/opt/arm-none-eabi-binutils/bin/arm-none-eabi-ld: cannot find -lc: Invalid argument
     (checker retries with -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY so the remaining checks still grade)
PASS T2 CMakeSystem: SYSTEM_NAME=Generic PROCESSOR=arm CROSSCOMPILING=TRUE, CC=arm-none-eabi-gcc
PASS T3 fw.elf = ELF32 ARM hard-float, entry 0x8000045 = Reset_Handler|1 (Thumb)
PASS T4 post-build: fw.bin 504 B (= .data LMA end - 0x08000000), fw.map, fw.size.txt
PASS T5 target tree has no test executable, ctest -N: Total Tests: 0
PASS T6 -mcpu= appears exactly once in the fw/main.c compile command
FAIL T7 CFLAGS=-DBOARD_REV=3 ignored or flags duplicated
     fw_board_rev=0x00000001 (want 0x00000003), -mcpu count=1
     CMAKE_C_FLAGS cache: -DBOARD_REV=3
PASS T8 second ninja: no work to do
FAIL T9 linker script edit did not relink (.isr_vector still at 0x08000000)
     ninja: no work to do.
8/12 passed
```

starter 는 **평범한 빌드는 전부 된다**(T3–T6). 틀린 것은 전부 "다른 사람 / 다른 머신 / 두 번째 빌드" 에서만 드러난다 — 그래서 오래 살아남는다.

## 4. 힌트

<details>
<summary>힌트 1 — T1: CMake 는 project() 에서 무엇을 링크하려 하나</summary>

에러 출력의 `[2/2]` 줄을 보자: `testCCompiler.c.obj -o cmTC_xxxx` — **실행 파일**을 만들려 한다. gcc 는 실행 파일 링크에 `crt0.o` 와 `-lc` 를 기본으로 붙이는데 이 맥에는 둘 다 없다. 컴파일러가 "동작하는지" 확인하는 방법을 실행 파일 대신 정적 라이브러리(링크 없음)로 바꾸는 변수가 있다: `CMAKE_TRY_COMPILE_TARGET_TYPE`.

</details>

<details>
<summary>힌트 2 — T7: 일반 변수(normal variable)는 캐시 변수(cache variable)를 가린다</summary>

`set(CMAKE_C_FLAGS "...")` 는 **일반 변수**를 만든다. `${CMAKE_C_FLAGS}` 를 읽으면 일반 변수가 먼저 보이고 캐시 값(`-DBOARD_REV=3`)은 가려진다. 캐시에는 남아 있어서 `CMakeCache.txt` 를 보면 "들어간 것처럼" 보인다. 툴체인 파일이 정해야 할 것은 "캐시의 **초기값**" — 이름이 `_INIT` 으로 끝나는 변수다.

</details>

<details>
<summary>힌트 3 — T9: ninja 는 무엇을 보고 다시 링크하나</summary>

`build.ninja` 에서 `build fw.elf` 줄을 찾아보자. 링크 단계의 입력은 `.obj` 와 `libdsp.a` 뿐이다. `-T/path/stm32f4.ld` 는 **플래그 문자열 안의 경로**라 ninja 는 그게 파일인지 모른다. 컴파일 단계는 `-MD` depfile 로 헤더를 알아내지만 링크 단계에는 그런 장치가 없다. 타깃 속성 하나로 암시적 입력(implicit input)을 추가할 수 있다.

</details>

<details>
<summary>힌트 4 — H2: 이 맥의 CMAKE_SYSTEM_PROCESSOR 는?</summary>

`build-host/CMakeFiles/4.5.0/CMakeSystem.cmake` 를 열어 보자. `MATCHES` 는 정규식 **부분 일치**다. "타깃용 빌드인가?" 는 CPU 이름이 아니라 **빌드 모드**로 물어야 한다 — 툴체인 파일이 `CMAKE_SYSTEM_NAME` 을 정하면 CMake 가 켜 주는 변수가 있다.

</details>

## 5. 풀이

### Part A — configure 가 툴체인 파일을 쓰는 순서

툴체인 파일은 "`project()` 전에 한 번 실행되는 스크립트" 가 아니다. `file(APPEND ...)` 로 읽힐 때마다 기록해 보면 (`bash demo_flags.sh` 끝부분, 맥 실측):

```text
first configure:
read in   b_trace
read in   b_trace
read in   b_trace/CMakeFiles/CMakeScratch/TryCompile-ZdAyag
re-configure of the same tree:
read in   b_trace
```

첫 configure 에 **3 번** — 메인 프로젝트에서 2 번(시스템 판별 단계와, 그 결과를 담은 `CMakeSystem.cmake` 를 다시 읽을 때), 그리고 **try_compile 프로젝트마다 1 번**(새 빌드 폴더 `CMakeScratch/TryCompile-*`, 새 캐시). 같은 트리를 다시 configure 하면 1 번. 이 사실 하나가 아래 함정 B1·B2 와 연습 P2 를 전부 설명한다.

```svg
<svg viewBox="0 0 740 380" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m5b" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">cmake -DCMAKE_TOOLCHAIN_FILE=… 첫 configure 의 순서 (★ = 툴체인 파일 실행)</text>
  <g stroke="currentColor">
    <rect x="20" y="34" width="340" height="40" rx="5" class="f-acc"/>
    <rect x="20" y="88" width="340" height="40" rx="5" class="f-acc"/>
    <rect x="20" y="142" width="340" height="40" rx="5" class="f-mut"/>
    <rect x="20" y="196" width="340" height="40" rx="5" class="f-mut"/>
    <rect x="20" y="250" width="340" height="56" rx="5" class="f-warn"/>
    <rect x="20" y="320" width="340" height="40" rx="5" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="32" y="58">★1 시스템 판별: Generic → CROSSCOMPILING=TRUE</text>
    <text x="32" y="112">★2 CMakeSystem.cmake 생성 후 다시 include</text>
    <text x="32" y="166">컴파일러 식별: "GNU 16.2.0" (컴파일만, 링크 없음)</text>
    <text x="32" y="220">CMAKE_C_FLAGS 캐시 생성 = $CFLAGS + _INIT</text>
    <text x="32" y="270">try_compile (ABI 검사, 컴파일러 검사)</text>
    <text x="32" y="292">→ 새 프로젝트 ★3, 새 캐시, TARGET_TYPE 대로 빌드</text>
    <text x="32" y="344">CMakeLists 본문 → build.ninja 생성 → ninja</text>
  </g>
  <g stroke="currentColor" marker-end="url(#m5b)">
    <path d="M185 74 L185 86"/><path d="M185 128 L185 140"/><path d="M185 182 L185 194"/>
    <path d="M185 236 L185 248"/><path d="M185 306 L185 318"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="380" y="58">B2: 여기서 set(CMAKE_C_FLAGS …) 하면 일반</text>
    <text x="380" y="74">변수가 생겨 뒤에 만들어질 캐시를 가린다</text>
    <text x="380" y="112">APPEND 형태는 ★1 + ★2 로 -mcpu 가 2 번</text>
    <text x="380" y="220">_INIT 은 이 순간에만 쓰인다 (첫 configure)</text>
    <text x="380" y="236">-DCMAKE_C_FLAGS=… 를 주면 _INIT 은 무시됨</text>
    <text x="380" y="270">B1: EXECUTABLE 이면 crt0.o / -lc 필요 → 실패</text>
    <text x="380" y="286">STATIC_LIBRARY 면 링크 단계가 없다 → 통과</text>
    <text x="380" y="302">P2: ★3 은 부모 캐시(-DFW_MCU)를 못 본다</text>
    <text x="380" y="344">re-configure 는 ★ 한 번 (CMakeSystem.cmake 재사용)</text>
  </g>
</svg>
```

(순서도의 "캐시 생성" 위치는 CMake 모듈 구조로 본 설명이다. 관찰된 결과 — try_compile 이 CPU 플래그로 빌드됨, APPEND 의 `-mcpu` 2 번, env `CFLAGS` 가 SET/APPEND 에서 사라짐 — 와 일치한다.)

**툴체인 파일 줄별 의미** (`solution/cmake/arm-none-eabi.cmake`):

| 줄 | 하는 일 | 빠지면 (실측/관찰) |
|---|---|---|
| `CMAKE_SYSTEM_NAME Generic` | OS 없는 타깃. 이 변수를 **정하는 순간** `CMAKE_CROSSCOMPILING=TRUE` | 실측: `CMAKE_SYSTEM_NAME "Darwin"`, `CROSSCOMPILING "FALSE"` → 테스트 분기로 가서 arm gcc 에 `-isysroot …/MacOSX26.4.sdk` 가 붙고 `stdio.h: No such file or directory` |
| `CMAKE_SYSTEM_PROCESSOR arm` | 정보용 문자열 (프로젝트가 읽음) | 실측 `set(CMAKE_SYSTEM_PROCESSOR "")` — 크로스 모드에서는 자동으로 안 채워진다 |
| `CMAKE_C_COMPILER arm-none-eabi-gcc` | PATH 에서 찾아 절대 경로로 고정 (`/opt/homebrew/bin/arm-none-eabi-gcc`) | 호스트 `cc` |
| `CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY` | 컴파일러 검사를 `.a` 로 — 링크 없음 | B1 configure 실패 |
| `CMAKE_C_FLAGS_INIT` | 캐시 `CMAKE_C_FLAGS` 의 **첫 값** (+ env `CFLAGS`) | 직접 `set()` 하면 B2 |
| `CMAKE_FIND_ROOT_PATH_MODE_*` | find_*() 가 호스트에서 찾을지 타깃 루트에서 찾을지 | Part C 의 find 실험 |

binutils 는 따로 안 적어도 **컴파일러 이름의 접두사**로 CMake 가 찾았다 (`demo_target.sh` §4, 타깃 캐시):

```text
CMAKE_AR:FILEPATH=/opt/homebrew/bin/arm-none-eabi-ar
CMAKE_OBJCOPY:FILEPATH=/opt/homebrew/bin/arm-none-eabi-objcopy
CMAKE_OBJDUMP:FILEPATH=/opt/homebrew/bin/arm-none-eabi-objdump
CMAKE_READELF:FILEPATH=/opt/homebrew/bin/arm-none-eabi-readelf
ARM_SIZE:FILEPATH=/opt/homebrew/bin/arm-none-eabi-size
```

`size` 는 CMake 표준 변수가 없어서 툴체인 파일에서 `find_program(ARM_SIZE ... REQUIRED)` 로 찾았다. 호스트 트리와 타깃 트리의 `CMakeSystem.cmake` 차이 (§4):

```text
[build-host]                                   [build-target]
set(CMAKE_SYSTEM_NAME "Darwin")                set(CMAKE_SYSTEM_NAME "Generic")
set(CMAKE_SYSTEM_PROCESSOR "arm64")            set(CMAKE_SYSTEM_PROCESSOR "arm")
set(CMAKE_CROSSCOMPILING "FALSE")              set(CMAKE_CROSSCOMPILING "TRUE")
(둘 다 CMAKE_HOST_SYSTEM_PROCESSOR "arm64")
```

**타깃 빌드와 산출물.** `ninja -v` 의 링크 줄 (§5, `$D` = 데모 작업 폴더):

```text
/opt/homebrew/bin/arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
  -ffreestanding -ffunction-sections -fdata-sections -nostdlib -T$D/s/fw/stm32f4.ld
  -Wl,--gc-sections -Wl,-Map=$D/b3ok/fw.map CMakeFiles/fw.dir/fw/startup.c.obj ... -o fw.elf  libdsp.a
    && cd $D/b3ok
    && /opt/homebrew/bin/arm-none-eabi-objcopy -O binary $D/b3ok/fw.elf fw.bin
    && /opt/homebrew/bin/arm-none-eabi-size $D/b3ok/fw.elf > fw.size.txt
```

- 링크 줄에 **`CMAKE_C_FLAGS` 가 그대로 들어간다** (CMake 가 GNU 링크 명령에 언어 플래그를 넘긴다). 그래서 `CMAKE_EXE_LINKER_FLAGS_INIT` 에 CPU 플래그를 또 넣으면 링크 줄에 두 번 나온다 — 처음 시제품에서 실제로 그랬다. 링크에도 `-mcpu/-mfloat-abi` 가 있어야 하는 이유: gcc 드라이버가 그것으로 **multilib**(`thumb/v7e-m+fp/hard/libgcc.a` 등)을 고른다.
- `add_custom_command(TARGET fw POST_BUILD ...)` 는 링크와 **같은 ninja 명령**에 `&&` 로 붙는다. `>` 리다이렉션도 `VERBATIM` 과 함께 그대로 셸로 갔다. 링크가 다시 돌 때만 같이 돈다 → 두 번째 `ninja` 는 `no work to do` (T8).
- `BYPRODUCTS` 를 적어 두면 ninja 가 `fw.bin` 의 생산자를 안다 (`build fw.elf fw.bin fw.size.txt | ...` — §6). 다른 타깃이 `fw.bin` 에 의존해도 순서가 맞는다.

검증 (§5):

```text
  [ 1] .isr_vector       PROGBITS        08000000 001000 000040 00   A  0   0  4
  [ 2] .text             PROGBITS        08000040 001040 0001a4 00  AX  0   0  4
  [ 3] .rodata           PROGBITS        080001e4 0011e4 000010 00   A  0   0  4
  [ 4] .data             PROGBITS        20000000 002000 000004 00  WA  0   0  4
  [ 5] .bss              NOBITS          20000004 002004 000024 00  WA  0   0  4
  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
  LOAD           0x001000 0x08000000 0x08000000 0x001f4 0x001f4 R E 0x1000
  LOAD           0x002000 0x20000000 0x080001f4 0x00004 0x00028 RW  0x1000
504 b3ok/fw.bin
00000000: 0000 0220 4500 0008                      ... E...
```

- `fw.bin` = Flash 의 0x08000000 부터 마지막 로드 바이트까지: 첫 세그먼트 `0x1f4` + `.data` 의 초기값 4 B (PhysAddr `0x080001f4`) = **0x1f8 = 504 B**. `.bss`(MemSiz − FileSiz = 0x24) 는 파일에 없다. 체커 T4 가 이 산수를 `_sidata/_sdata/_edata` 로 한다.
- `.bin` 첫 8 바이트 = 벡터 표 [0] 초기 SP `0x20020000` (= RAM 끝), [1] `0x08000045` = `Reset_Handler`(0x08000044) | 1 (Thumb 비트). `readelf -h` 의 `Entry point address: 0x8000045` 와 같다 (T3).
- `Flags: 0x5000400, Version5 EABI, hard-float ABI` — `-mfloat-abi=hard` 가 실제로 들어갔다는 증거. `-mfloat-abi=soft` 로 만든 오브젝트를 섞으면 링커가 이 플래그를 보고 거부한다 (실측 `error: … uses VFP register arguments, x.o does not`, `failed to merge target specific data`).

### Part B — 함정 4 개를 실제로

**B1. try_compile 이 실행 파일을 링크하려 한다 (T1).** starter 실측 (§1):

```text
-- Detecting C compiler ABI info - failed
-- Check for working C compiler: /opt/homebrew/bin/arm-none-eabi-gcc - broken
CMake Error at /opt/homebrew/share/cmake/Modules/CMakeTestCCompiler.cmake:65 (message):
  is not able to compile a simple test program.
    [2/2] : && /opt/homebrew/bin/arm-none-eabi-gcc -mcpu=cortex-m4 ... CMakeFiles/cmTC_a0511.dir/testCCompiler.c.obj -o cmTC_a0511   && :
    /opt/homebrew/opt/arm-none-eabi-binutils/bin/arm-none-eabi-ld: cannot find crt0.o: Invalid argument
    /opt/homebrew/opt/arm-none-eabi-binutils/bin/arm-none-eabi-ld: cannot find -lc: Invalid argument
-- Configuring incomplete, errors occurred!
```

컴파일(`[1/2]`)은 성공했다. 실패는 **링크**다 — 베어메탈 실행 파일은 링커 스크립트·startup·libc 가 있어야 의미가 있는데, 그건 프로젝트가 나중에 줄 것들이다. `STATIC_LIBRARY` 로 바꾸면 try_compile 은 `ar` 까지만 한다 → solution 에서는 `Check for working C compiler: ... - skipped` (이 CMake 4.5 는 ABI 검사 try_compile 이 성공하면 별도의 컴파일러 검사를 건너뛰었다 — 출력의 `skipped`).

"고치는" 다른 방법들과 왜 별로인지 (§2, §3, VM §A/§B 실측):

| 방법 | 결과 | 문제 |
|---|---|---|
| `-nostdlib` 를 `CMAKE_EXE_LINKER_FLAGS_INIT` 에 | configure 통과 | 로그에 `warning: cannot find entry symbol _start; defaulting to 00008000` — 시험 프로그램은 **엉터리 주소의 쓰레기**로 "성공". 검사가 아무것도 검사하지 않는다 |
| `-DCMAKE_C_COMPILER_WORKS=ON` (옛 해킹) | configure 통과 | `Detecting C compiler ABI info - failed` 그대로 → `CMAKE_C_SIZEOF_DATA_PTR` = `[]` (정상 `4`), `CMAKE_C_COMPILER_ABI` = `[]` (정상 `ELF`), 암시적 include 경로 비어 있음. `CheckTypeSize` 등이 틀린 답을 낼 수 있다 |
| VM: newlib 있음, specs 없음 | 실패 | `undefined reference to '_exit'`, `'_sbrk'` — libc 는 있는데 시스템 콜이 없다 |
| VM: `--specs=nano.specs --specs=nosys.specs` 를 링커 INIT 에 | configure 통과 | 진짜 링크가 되긴 한다(`_close is not implemented and will always fail` 경고). newlib 이 있는 툴체인에서만 가능 |
| **`CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY`** | 통과 | 정석. 링크 가능성은 펌웨어 타깃의 실제 링크가 검증한다 |

**B2. `set(CMAKE_C_FLAGS ...)` 는 사용자의 캐시를 가린다 (T7).** 같은 트리를 툴체인 스타일 3 개 × 사용자가 플래그를 넣는 방법 4 개로 빌드한 실측표 (`bash demo_flags.sh`, `rev` = 이미지 속 `fw_board_rev`):

| 툴체인 스타일 | 아무것도 안 줌 | env `CFLAGS=-DBOARD_REV=3` | `-DCMAKE_C_FLAGS=-DBOARD_REV=3` | 기존 트리 재구성, 캐시에 `-DBOARD_REV=3` 덧붙임 |
|---|---|---|---|---|
| `_INIT` (solution) | rev 1, `-mcpu`×1 | **rev 3**, ×1 | **빌드 실패** `fatal error: stdint.h`, ×0 | **rev 3**, ×1 |
| `set(CMAKE_C_FLAGS "…")` (starter) | rev 1, ×1 | rev 1 (무시), ×1 | rev 1 (무시), ×1 | rev 1 (무시), ×1 |
| `set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} …")` | rev 1, **×2** | rev 1 (무시), ×2 | rev 3, ×2 | rev 3, ×1 |

읽는 법:

- **SET** — 툴체인이 일반 변수를 만들어 캐시를 영원히 가린다. 캐시에는 `CMAKE_C_FLAGS:STRING=-DBOARD_REV=3` 이 **보이는데** 컴파일 줄에는 없다. `ccmake` 로 고쳐도 소용없다. 체커 T7 의 출력이 정확히 이것이다.
- **APPEND** — 흔히 복사해 쓰는 형태 (STM32CubeMX 가 생성하는 툴체인 파일도 이런 append 형태였던 것으로 안다 — 확인 권장). 첫 configure 에 툴체인이 2 번 읽혀 `-mcpu` 가 2 번, env `CFLAGS` 는 캐시가 생기기 전에 일반 변수가 이미 만들어져서 사라진다. 그리고 재구성하면 읽기가 1 번이라 ×1 로 **바뀐다** — 같은 트리가 configure 할 때마다 다른 명령줄을 만든다 (ninja 는 명령줄 해시를 기록하므로, 실측 `ninja explain: command line changed for ...crc.c.obj` 로 전부 재컴파일).
- **INIT** — 캐시의 첫 값만 정한다. env `CFLAGS` 는 합쳐지고, 나중에 캐시를 편집하면 그대로 반영된다. 단 **첫 configure 에 `-DCMAKE_C_FLAGS=...` 를 주면 그 값이 `_INIT` 을 대신한다** → CPU 플래그·`-ffreestanding` 이 사라져 `stdint.h` 의 `#include_next` 가 실패했다. 이건 버그가 아니라 문서화된 의미(캐시는 사용자 소유)다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">${CMAKE_C_FLAGS} 를 읽을 때: 일반 변수 → (없으면) 캐시</text>
  <text x="20" y="44" fill="currentColor" font-weight="bold">starter: set(CMAKE_C_FLAGS …)</text>
  <rect x="20" y="54" width="330" height="44" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="32" y="72" fill="currentColor" font-size="12">일반 변수 (툴체인이 만듦)</text>
  <text x="32" y="90" fill="currentColor" font-size="12" font-family="monospace">-mcpu=cortex-m4 … -ffreestanding …</text>
  <rect x="20" y="108" width="330" height="44" rx="5" class="f-mut" stroke="currentColor" stroke-dasharray="5 3"/>
  <text x="32" y="126" fill="currentColor" font-size="12">캐시 CMakeCache.txt (가려짐)</text>
  <text x="32" y="144" fill="currentColor" font-size="12" font-family="monospace">-DBOARD_REV=3</text>
  <text x="20" y="176" fill="currentColor" font-size="12">컴파일 줄: -mcpu … (BOARD_REV 없음)</text>
  <text x="20" y="194" fill="currentColor" font-size="12">→ fw_board_rev = 0x00000001</text>
  <text x="390" y="44" fill="currentColor" font-weight="bold">solution: set(CMAKE_C_FLAGS_INIT …)</text>
  <rect x="390" y="54" width="330" height="44" rx="5" fill="none" stroke="currentColor" stroke-dasharray="5 3"/>
  <text x="402" y="72" fill="currentColor" font-size="12">일반 변수 CMAKE_C_FLAGS: 없음</text>
  <text x="402" y="90" fill="currentColor" font-size="12">(_INIT 은 다른 이름의 변수)</text>
  <rect x="390" y="108" width="330" height="44" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="402" y="126" fill="currentColor" font-size="12">캐시 = $CFLAGS + _INIT (첫 configure)</text>
  <text x="402" y="144" fill="currentColor" font-size="12" font-family="monospace">-DBOARD_REV=3 -mcpu=cortex-m4 …</text>
  <text x="390" y="176" fill="currentColor" font-size="12">컴파일 줄: -DBOARD_REV=3 -mcpu … (×1)</text>
  <text x="390" y="194" fill="currentColor" font-size="12">→ fw_board_rev = 0x00000003</text>
  <text x="20" y="232" fill="currentColor" font-size="12">규칙: 툴체인 파일은 *_INIT 만. 보드 설정 같은 프로젝트 값은 C_FLAGS 가 아니라 프로젝트 캐시 변수로 (P1).</text>
</svg>
```

**B3. 링커 스크립트는 ninja 에게 보이지 않는다 (T9).** `build.ninja` 의 링크 간선 (§6):

```text
[solution] build fw.elf fw.bin fw.size.txt | ...: C_EXECUTABLE_LINKER__fw_ ...obj | libdsp.a $D/s/fw/stm32f4.ld || libdsp.a
[starter]  build fw.elf fw.bin fw.size.txt | ...: C_EXECUTABLE_LINKER__fw_ ...obj | libdsp.a || libdsp.a
```

`|` 뒤가 암시적 입력(implicit dependency) — 바뀌면 다시 만들지만 명령줄에는 안 들어가는 파일. `LINK_DEPENDS` 속성이 링커 스크립트를 여기에 넣는다. 결과 (`touch fw/stm32f4.ld` 후 `ninja -n -d explain`):

```text
[solution] ninja explain: recorded mtime of fw.elf older than most recent input $D/s/fw/stm32f4.ld (...)
           [1/1] Linking C executable fw.elf; fw.elf -> fw.bin + fw.size.txt
[starter]  ninja: no work to do.
```

컴파일 단계는 `-MD -MF x.obj.d` depfile 로 헤더 의존성을 **컴파일러가** 알려 주지만(TOOL-B08 의 `-MMD` 와 같은 원리), 링크에는 그런 depfile 이 없다. 체커 T9 은 "부트로더 16 KB 를 위해 ORIGIN 을 `0x08004000` 으로" 라는 진짜 시나리오로 확인한다: starter 는 `.isr_vector still at 0x08000000` — 이 이미지를 부트로더 뒤 0x08004000 에 구우면 벡터 표가 엉뚱한 곳을 가리켜 첫 예외에서 HardFault (ARM-M12). CI 는 클린 빌드라 못 잡고, 개발자 책상에서만 터지는 종류다.

**B4. `"arm64" MATCHES "arm"` (H2).** `cmake -P` 로 확인 (§7):

```text
  arm64 MATCHES arm -> TRUE
  aarch64 MATCHES arm -> FALSE
  arm MATCHES arm -> TRUE
  armv7 MATCHES arm -> TRUE
  x86_64 MATCHES arm -> FALSE
```

`MATCHES` 는 정규식 부분 일치라 Apple Silicon 호스트(`arm64`)도 "ARM" 이다. 같은 Linux aarch64 서버에서는 `aarch64` 라 FALSE — 머신마다 결과가 다르다. starter 는 인텔 CI 에서 멀쩡하다가 M 시리즈 맥에서 테스트가 통째로 사라진다 (`No tests were found!!!`). CI 가 "0 개 테스트" 를 성공으로 넘기지 않게 `ctest --no-tests=error` 를 쓴다 (이 ctest 4.5 의 `--help` 에 `--no-tests=<[error|ignore]>` 실측; 기본 동작의 종료 코드는 확인 권장). 묻고 싶은 건 "크로스 빌드인가" 이므로 `CMAKE_CROSSCOMPILING` (또는 `CMAKE_SYSTEM_NAME STREQUAL "Generic"`). CPU 를 꼭 봐야 하면 `STREQUAL` 로 정확히.

### Part C — 펌웨어 현실

**같은 트리, VM 의 gcc 14 + newlib** (`bash demo_vm.sh`, 실측):

```text
### C. solution tree unchanged (-nostdlib, own memset/memcpy), VM gcc 14
    508	      4	     36	    548	    224	/tmp/TOOL-M05_work/vm/c/fw.elf
### D. newlib-nano instead of -nostdlib: fw calls snprintf (specs in the toolchain file)
[D1 given stm32f4.ld: no 'end' symbol for the heap]
<newlib-src>/libgloss/libnosys/sbrk.c:20:(.text+0x18): undefined reference to `end'
[D2 add PROVIDE(end = _ebss) to the script copy]
<newlib-src>/newlib/libc/reent/writer.c:49:(.text+0x14): warning: _write is not implemented and will always fail
   4372	     84	    400	   4856	   12f8	/tmp/TOOL-M05_work/vm/bd/fw.elf
total libc_nano members: 29
```

- 툴체인 파일 수정 없이 같은 트리가 gcc 14 로도 빌드됐다 (text 508 vs 맥 gcc 16 의 500 — 컴파일러 버전 차이). 툴체인 파일은 **이름**(`arm-none-eabi-gcc`)으로 찾으므로 PATH 만 맞으면 된다. 버전을 고정하려면 절대 경로나 `CMAKE_C_COMPILER_VERSION` 검사를 넣는다.
- newlib-nano 로 바꾸는 건 **툴체인 파일 한 줄**(`CMAKE_EXE_LINKER_FLAGS_INIT "--specs=nano.specs --specs=nosys.specs"`)과 프로젝트의 `-nostdlib` 제거다. 그러자 링커 스크립트가 몰랐던 계약이 드러났다: nosys 의 `_sbrk` 는 힙 시작으로 `end` 심볼을 요구한다 (TOOL-B06 의 `fw.ld` 가 `PROVIDE(end = .)` 를 갖고 있던 이유). `snprintf` 하나로 text 508 → 4372 B, libc_nano 멤버 29 개 — 크기 다이어트는 TOOL-M13.
- `_write is not implemented` 경고: 시스템 콜 스텁. 실제 보드에서는 `_write` 를 UART 로 구현한다.

**`CMAKE_FIND_ROOT_PATH_MODE_*` 는 루트가 있어야 일한다** (§10, 맥에 `/opt/homebrew/lib/libcairo.a` 가 실제로 있다. 가짜 sysroot 에는 빈 `libcairo.a`):

```text
[-DCMAKE_PREFIX_PATH=/opt/homebrew]
-- CAIRO_LIB=/opt/homebrew/lib/libcairo.a  PY=/opt/homebrew/bin/python3
[-DCMAKE_PREFIX_PATH=/opt/homebrew -DCMAKE_FIND_ROOT_PATH=$D/fr/sysroot]
-- CAIRO_LIB=CAIRO_LIB-NOTFOUND  PY=/opt/homebrew/bin/python3
[-DCMAKE_PREFIX_PATH=/usr -DCMAKE_FIND_ROOT_PATH=$D/fr/sysroot]
-- CAIRO_LIB=$D/fr/sysroot/usr/lib/libcairo.a  PY=/usr/bin/python3
```

- 모드가 `LIBRARY ONLY` 인데도 루트 경로가 **비어 있으면** 호스트의 arm64 `libcairo.a` 를 ARM 펌웨어용으로 찾아 준다 (첫 줄). 이게 링크까지 가면 `file format not recognized` 류 에러다. 루트(`CMAKE_FIND_ROOT_PATH` 또는 `CMAKE_SYSROOT`)를 주면 경로가 그 아래로 다시 뿌리내려(re-rooted) 호스트 것은 안 보이고 sysroot 의 것만 보인다.
- `PROGRAM NEVER` 라서 `python3` 같은 **빌드 도구**는 항상 호스트에서 찾는다 — 코드 생성기(레지스터 맵 생성 스크립트 등)를 크로스 빌드에서 돌리려면 이게 맞다.
- 베어메탈(`Generic`)은 기본 시스템 검색 경로가 거의 없어서, 이 맥에서는 `CMAKE_PREFIX_PATH` 없이 `BOTH` 모드여도 호스트 라이브러리가 안 잡혔다 (실측). 사람 환경 변수(`CMAKE_PREFIX_PATH`)가 끼어드는 순간 위 첫 줄이 된다.

**맥 호스트 빌드의 함정 하나 더.** 이 레슨을 처음 만들 때 SDKROOT 없이 호스트 configure 를 했더니 CMake 가 고른 SDK(27.2)의 `libSystem.tbd` 를 링커가 못 읽어 `ld: tapi error: malformed file ... unknown architecture arm64e.x1-macos` 로 **호스트** 컴파일러 검사가 실패했다. 체커가 `export SDKROOT=…/MacOSX26.4.sdk` 를 하는 이유 — CMake 는 이 값을 `CMAKE_OSX_SYSROOT` 캐시로 가져갔다(실측 `CMAKE_OSX_SYSROOT:PATH=…/MacOSX26.4.sdk`). 크로스 빌드든 아니든 "configure 가 컴파일러를 시험한다" 는 같은 장치가 문제를 일찍 드러낸 셈이다.

**CI 에서는.** 두 잡: `host` (configure + build + `ctest --output-on-failure`, `compile_commands.json` 은 호스트 쪽을 clang-tidy 에 — TOOL-M12) / `target` (툴체인 파일, 산출물 `fw.elf fw.bin fw.map fw.size.txt` 를 아티팩트로, 크기 예산 검사 — P3). 빌드 폴더가 다르니 같은 러너에서 병렬로 돌려도 안 섞인다. 사람마다 다른 긴 `cmake` 명령줄은 `CMakePresets.json` 으로 저장소에 고정한다 (P1).

## 6. 정답 코드

<!-- code: solution/cmake/arm-none-eabi.cmake diff=starter/cmake/arm-none-eabi.cmake -->

<!-- code: solution/CMakeLists.txt diff=starter/CMakeLists.txt -->

`bash check.sh solution` 실제 출력:

```text
== solution: cmake version 4.5.0, ninja 1.13.2, arm-none-eabi-gcc (GCC) 16.2.0 ==
-- host (Apple clang) --
PASS H1 host configure: CMAKE_SYSTEM_PROCESSOR=arm64, CMAKE_CROSSCOMPILING=FALSE
PASS H2 host unit tests: 100% tests passed out of 3
PASS H3 host tree clean: no -mcpu/-mthumb/arm-none-eabi in compile_commands, lib objects are Mach-O arm64
-- target (toolchain file cmake/arm-none-eabi.cmake) --
PASS T1 target configure with toolchain file (no workaround needed)
PASS T2 CMakeSystem: SYSTEM_NAME=Generic PROCESSOR=arm CROSSCOMPILING=TRUE, CC=arm-none-eabi-gcc
PASS T3 fw.elf = ELF32 ARM hard-float, entry 0x8000045 = Reset_Handler|1 (Thumb)
PASS T4 post-build: fw.bin 504 B (= .data LMA end - 0x08000000), fw.map, fw.size.txt
PASS T5 target tree has no test executable, ctest -N: Total Tests: 0
PASS T6 -mcpu= appears exactly once in the fw/main.c compile command
PASS T7 CFLAGS=-DBOARD_REV=3 reaches the compiler: fw_board_rev=0x00000003, -mcpu once
PASS T8 second ninja: no work to do
PASS T9 linker script edit -> relink: .isr_vector now at 0x08004000
12/12 passed
```

| TODO | 고침 | 막는 고장 (체커) |
|---|---|---|
| (1) 툴체인 | `set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)` | T1 — configure 시 링크 불가 |
| (2) 툴체인 | `set(CMAKE_C_FLAGS ...)` → `set(CMAKE_C_FLAGS_INIT ...)` | T7 — 사용자 `CFLAGS` 무시 (APPEND 형태였다면 T6 도) |
| (3) 프로젝트 | `set_target_properties(fw PROPERTIES LINK_DEPENDS ${LD_SCRIPT})` | T9 — 스크립트 수정 후 재링크 안 됨 |
| (4) 프로젝트 | `if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm")` → `if(NOT CMAKE_CROSSCOMPILING)` | H2 — Apple Silicon 에서 테스트 0 개 |

## 7. 자주 하는 실수

**1. `CMAKE_C_COMPILER_WORKS=ON` 으로 configure 를 "통과" 시키기.** 검사를 끈 것이지 문제를 고친 게 아니다. 실측: ABI 검사가 여전히 `failed` → `CMAKE_C_SIZEOF_DATA_PTR` 가 빈 값. 정답은 `CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY`.

**2. 툴체인 파일에 `set(CMAKE_C_FLAGS ...)` (덮어쓰기 또는 append).** 덮어쓰기는 사용자 플래그를 가리고, append 는 첫 configure 에 두 번 + env `CFLAGS` 소실 + 재구성 때 명령줄이 바뀜. `*_INIT` 만 쓴다.

**3. 첫 configure 에 `-DCMAKE_C_FLAGS=-DFOO` 로 정의 추가.** `_INIT` 툴체인과 함께면 CPU 플래그가 통째로 사라진다 (실측 `fatal error: stdint.h: No such file or directory`). 정의 하나는 `CFLAGS` 환경 변수, 또는 더 좋게는 프로젝트 캐시 변수 + `target_compile_definitions` (P1).

**4. 링커 스크립트를 `target_link_options(-T...)` 로만 걸기.** 빌드 시스템에 안 보인다 → `LINK_DEPENDS`. 링커 스크립트를 `configure_file` 로 생성한다면 그 출력 파일을 `LINK_DEPENDS` 에.

**5. "ARM 이면" 을 `CMAKE_SYSTEM_PROCESSOR MATCHES "arm"` 으로.** Apple Silicon(`arm64`)·Raspberry Pi(`armv7l`/`aarch64`) 호스트에서 오판. `CMAKE_CROSSCOMPILING` 또는 `CMAKE_SYSTEM_NAME`.

**6. 호스트와 타깃을 같은 빌드 폴더에서 툴체인만 바꿔 재구성.** 실측: 기존 `build-host` 에 `-DCMAKE_TOOLCHAIN_FILE=…` 를 주자 CMake 가 `You have changed variables that require your cache to be deleted.` 를 출력하고 **캐시를 지운 뒤** 다시 configure → 그 폴더는 조용히 `Generic` 타깃 트리가 됐다(호스트 테스트 사라짐, exit 0). 빌드 트리는 툴체인당 하나 — 툴체인을 바꾸려면 새 폴더.

**7. 툴체인 파일에 프로젝트 일(링커 스크립트·`-nostdlib`·소스 목록)을 넣기.** 툴체인 파일은 try_compile 에도 적용된다 — 거기에 `-T stm32f4.ld` 가 있으면 시험 라이브러리/프로그램 빌드에도 붙는다. 그리고 보드가 둘이면 툴체인 파일을 복사하게 된다. CPU 는 툴체인, 보드·이미지는 프로젝트.

**8. `-nostdlib` 하면 libgcc 도 빠진다는 걸 잊기.** Cortex-M4 는 하드웨어 나눗셈이라 티가 안 나다가, Cortex-M0+ 로 옮기는 순간 `undefined reference to '__aeabi_idiv'` (P2 실측). `target_link_libraries(fw PRIVATE ... gcc)`.

## 8. 면접 Q&A

<details>
<summary>Q1. 툴체인 파일에 최소한 무엇이 있어야 하나요? 각각 왜?</summary>

`CMAKE_SYSTEM_NAME`(정하는 순간 크로스 컴파일 모드, 베어메탈은 `Generic`), 컴파일러(`CMAKE_C_COMPILER` …), 타깃 CPU 플래그(`CMAKE_C_FLAGS_INIT`), 베어메탈이면 `CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY`, sysroot 가 있으면 `CMAKE_SYSROOT`/`CMAKE_FIND_ROOT_PATH` + `CMAKE_FIND_ROOT_PATH_MODE_*` (프로그램은 호스트, 라이브러리/헤더는 타깃). 없어도 되는 것: objcopy/ar/nm — CMake 가 컴파일러 접두사로 찾는다(실측 `CMAKE_OBJCOPY=/opt/homebrew/bin/arm-none-eabi-objcopy`). 넣으면 안 되는 것: 링커 스크립트, 소스, 보드 정의.

</details>

<details>
<summary>Q2. CMake 는 왜 configure 때 테스트 프로그램을 링크하나요? 베어메탈에서는 어떻게?</summary>

`project()`/`enable_language()` 가 컴파일러가 동작하는지, 그리고 ABI 정보(포인터 크기, 암시적 include/링크 디렉터리)를 try_compile 로 알아낸다. 기본 형태가 **실행 파일**이라, `crt0.o`/libc/링커 스크립트가 없는 베어메탈 툴체인에서는 `cannot find crt0.o`, `cannot find -lc` (이 맥 실측) 또는 newlib 만 있을 땐 `undefined reference to '_exit'` (VM 실측)로 실패한다. `CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY` 로 링크 단계를 없앤다. 꼬리: "`-nostdlib` 로 통과시키면?" → `cannot find entry symbol _start; defaulting to 00008000` 경고와 함께 쓰레기 실행 파일이 "성공" — 검사가 무의미해진다.

</details>

<details>
<summary>Q3. CMAKE_C_FLAGS 와 CMAKE_C_FLAGS_INIT 의 차이는? 툴체인 파일에서 어느 쪽?</summary>

`CMAKE_C_FLAGS` 는 **사용자 소유 캐시 변수**, `_INIT` 은 그 캐시를 첫 configure 때 채울 기본값(+ env `CFLAGS`). 툴체인 파일에서 `set(CMAKE_C_FLAGS ...)` 하면 같은 이름의 일반 변수가 캐시를 가려 사용자 값이 무시되고(실측: 캐시엔 `-DBOARD_REV=3`, 이미지엔 rev 1), append 형태는 툴체인이 여러 번 읽혀 중복된다(`-mcpu` ×2). 그래서 `_INIT`. 꼬리: "`-DCMAKE_C_FLAGS=…` 를 주면 `_INIT` 은?" → 대체된다(실측으로 CPU 플래그가 사라져 빌드 실패). 필수 플래그를 사용자가 실수로 지울 수 없게 하려면 그 플래그를 프로젝트 쪽(`target_compile_options`)이나 별도 메커니즘으로 옮기는 설계도 있다.

</details>

<details>
<summary>Q4. 툴체인 파일은 몇 번 읽히나요? 그게 왜 중요하죠?</summary>

실측: 첫 configure 에 메인 프로젝트에서 2 번 + try_compile 프로젝트마다 1 번(이 프로젝트는 1 개 → 총 3 번), 재구성에는 1 번. 그래서 (a) 툴체인 파일은 **멱등**(idempotent)이어야 하고 — append 는 중복된다, (b) try_compile 프로젝트는 **새 캐시**라 `-DFW_MCU=cortex-m0plus` 같은 사용자 변수가 안 보인다(실측 `FW_MCU=[cortex-m4]`). 필요한 변수는 `CMAKE_TRY_COMPILE_PLATFORM_VARIABLES` 에 넣어 넘긴다. CMAKE_C_FLAGS 는 CMake 가 따로 넘겨서 이 레슨에서는 플래그가 같았지만, 툴체인이 그 변수로 컴파일러·sysroot·specs 를 고른다면 검사와 실제 빌드가 다른 설정이 된다.

</details>

<details>
<summary>Q5. 호스트 테스트 빌드와 타깃 빌드를 한 CMakeLists 에서 어떻게 나누나요?</summary>

빌드 트리를 둘로(`build-host`, `build-target`), 분기는 `if(CMAKE_CROSSCOMPILING)`. 이식 가능한 라이브러리(`dsp`)는 무조건, 펌웨어 실행 파일·POST_BUILD 는 타깃에서만, `enable_testing()`/`add_test` 는 호스트에서만. CPU 이름(`CMAKE_SYSTEM_PROCESSOR MATCHES "arm"`)으로 나누면 Apple Silicon 호스트(`arm64`)에서 오판한다(실측: 테스트 0 개). 꼬리: "하드웨어 의존 코드를 호스트에서 테스트하려면?" → HAL 인터페이스 뒤로 숨기고 호스트용 가짜 구현(fake)을 링크 — 이 레슨의 `lib/` 처럼 libc 외 의존이 없는 순수 로직을 최대한 키운다.

</details>

<details>
<summary>Q6. 링커 스크립트만 고쳤는데 재링크가 안 됩니다. 원인과 해결은?</summary>

링커 스크립트는 `-T` **플래그 문자열 안**에만 있어 ninja/make 의 의존 그래프에 없다. 컴파일 단계는 컴파일러가 depfile(`-MD`)로 헤더를 알려 주지만 링크 단계에는 그게 없다. `set_target_properties(fw PROPERTIES LINK_DEPENDS ${LD_SCRIPT})` → `build.ninja` 의 링크 간선에 암시적 입력으로 추가(실측). 위험: 부트로더 오프셋 변경(0x08000000 → 0x08004000)이 반영 안 된 이미지가 나가 벡터 표가 틀린 주소 → 부팅 직후 HardFault.

</details>

<details>
<summary>Q7. .elf / .bin / .hex / .map 은 각각 무엇이고 언제 쓰나요?</summary>

`.elf`: 섹션·심볼·디버그 정보·주소(VMA/LMA)를 가진 원본 — 디버거/`addr2line`/분석용, 대부분의 프로그래머(J-Link, OpenOCD)도 직접 읽는다. `.bin`: Flash 시작 주소부터의 **원시 바이트**(주소 정보 없음) — 이 레슨 504 B = `.text`+`.rodata`+`.data` 초기값, `.bss` 없음. 부트로더 DFU·OTA 패킷은 보통 이것. 주의: 섹션 사이에 큰 주소 공백이 있으면 그만큼 0 으로 채워져 거대해진다(예: `.data` 를 `AT> FLASH` 안 하면 — TOOL-M01). `.hex`: 주소가 들어 있는 텍스트(Intel HEX) — 여러 영역을 한 파일에. `.map`: 링커가 무엇을 어디에 왜 넣었는지 — 크기 분석(TOOL-M02), 이 레슨은 `-Wl,-Map=` 로 링크 때 생성.

</details>

<details>
<summary>Q8. CMAKE_FIND_ROOT_PATH_MODE_PROGRAM/LIBRARY/INCLUDE 를 왜 NEVER/ONLY/ONLY 로 두나요?</summary>

빌드 중에 **실행**할 도구(코드 생성기, python)는 호스트 것이어야 하니 PROGRAM=NEVER(루트 밑에서 찾지 않음). **링크**할 라이브러리와 헤더는 타깃 것이어야 하니 ONLY(루트 밑에서만). 실측 함정: 루트 경로(`CMAKE_FIND_ROOT_PATH`/`CMAKE_SYSROOT`)가 비어 있으면 ONLY 도 아무것도 막지 못해 `CMAKE_PREFIX_PATH=/opt/homebrew` 하나로 호스트 arm64 `libcairo.a` 가 ARM 빌드에 잡혔다. 루트를 주자 NOTFOUND, sysroot 의 것만 찾았다. 임베디드 Linux(Yocto SDK, Buildroot)의 툴체인 파일이 sysroot 를 꼭 설정하는 이유다.

</details>

## 9. 추가 연습

`practice/` = `solution/` + 아래 P1–P3. 체커는 기본 12 항목 + 연습 6 항목.

**P1. CMakePresets.json.** `cmake --preset host`, `cmake --build --preset host`, `ctest --preset host` 와 `cmake --preset target` 으로 두 트리를 만든다. 타깃 프리셋은 보드 리비전을 `BOARD_REV=2` 로 고정한다 — 단 C_FLAGS 가 아니라 **프로젝트 캐시 변수**로.

<details>
<summary>P1 답</summary>

`practice/CMakePresets.json` (version 6, cmake 4.5 / VM 4.2 둘 다 읽음):

```json
{ "name": "target", "generator": "Ninja", "binaryDir": "${sourceDir}/build/target",
  "toolchainFile": "${sourceDir}/cmake/arm-none-eabi.cmake",
  "cacheVariables": { "BOARD_REV": "2", "FW_MCU": "cortex-m4" } }
```

프로젝트 쪽: `set(BOARD_REV "" CACHE STRING ...)` + `if(BOARD_REV) target_compile_definitions(fw PRIVATE BOARD_REV=${BOARD_REV}) endif()`. 비워 두면 `fw/main.c` 기본값(1) — 그래서 env `CFLAGS=-DBOARD_REV=3` 경로(T7)와 충돌하지 않는다(둘 다 주면 `-DBOARD_REV=2 … -DBOARD_REV=3` 이 한 줄에 → 실측 `<command-line>: error: 'BOARD_REV' redefined [-Werror]`). `toolchainFile` 키는 `-DCMAKE_TOOLCHAIN_FILE` 과 같다. 체커: 호스트 3 테스트 통과, 타깃 이미지의 `fw_board_rev=0x00000002`. 왜 C_FLAGS 가 아니라 캐시 변수인가: 프리셋에서 `CMAKE_C_FLAGS` 를 주면 Part B 표의 "-DCMAKE_C_FLAGS" 열처럼 `_INIT` 을 대체해 CPU 플래그가 사라진다.

</details>

**P2. 툴체인 파일 하나로 두 코어.** `-DFW_MCU=cortex-m0plus` 이면 `-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft`, 기본은 Cortex-M4, 그 외 값은 configure 에러. M0+ 이미지가 링크되고 `readelf -A` 가 ARMv6-M 이어야 한다.

<details>
<summary>P2 답</summary>

툴체인 파일(`practice/cmake/arm-none-eabi.cmake`)의 핵심:

```cmake
if(NOT FW_MCU)
    set(FW_MCU cortex-m4)
endif()
if(NOT "FW_MCU" IN_LIST CMAKE_TRY_COMPILE_PLATFORM_VARIABLES)   # file is read 3x: stay idempotent
    list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES FW_MCU)
endif()
if(FW_MCU STREQUAL "cortex-m4")
    set(CPU_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16")
elseif(FW_MCU STREQUAL "cortex-m0plus")
    set(CPU_FLAGS "-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft")   # ARMv6-M: no FPU, no SDIV
else()
    message(FATAL_ERROR "FW_MCU must be cortex-m4 or cortex-m0plus, got '${FW_MCU}'")
endif()
```

함정 둘 (`demo_target.sh` §8, §9 실측):

- `CMAKE_TRY_COMPILE_PLATFORM_VARIABLES` 없이: try_compile 안에서 읽힌 툴체인은 `FW_MCU=[cortex-m4]` (새 캐시라 기본값), 있으면 `[cortex-m0plus]`. 이 레슨에서는 CMake 가 `CMAKE_C_FLAGS` 를 따로 넘겨 검사 빌드 플래그가 m0plus 로 같았다(`Build flags: -mcpu=cortex-m0plus;...`) — 그래서 체커 대상으로는 안 했다.
- `-nostdlib` 는 libgcc 도 뺀다. M0+ 에는 나눗셈 명령이 없어 `mavg_push` 의 `/` 가 `__aeabi_idiv` 호출이 된다 → `undefined reference to '__aeabi_idiv'`. `target_link_libraries(fw PRIVATE dsp gcc)` 로 해결, 맵에 `thumb/v6-m/nofp/libgcc.a(_divsi3.o)` — gcc 드라이버가 CPU 플래그로 multilib 폴더를 골랐다. 크기: M4 flash 504 B vs M0+ 1004 B (Thumb-1 + 소프트 나눗셈). `readelf -A`: `Tag_CPU_arch: v6S-M`, `Tag_FP_arch`/`Tag_ABI_VFP_args` 없음.

</details>

**P3. Flash 예산 게이트.** 타깃 빌드가 `--print-memory-usage` 를 출력하고, `text + data` 가 `FW_FLASH_BUDGET`(기본 8192 B) 을 넘으면 **빌드가 실패**해야 하며 그때는 `fw.bin` 이 만들어지지 않아야 한다.

<details>
<summary>P3 답</summary>

POST_BUILD 의 **첫** 명령으로 스크립트 모드 CMake 를 돌린다 (`cmake -P`, 셸·파이썬 의존 없음 → 맥/리눅스/윈도 CI 공통):

```cmake
execute_process(COMMAND ${SIZE_TOOL} ${ELF} OUTPUT_VARIABLE out RESULT_VARIABLE rc)
...
math(EXPR flash "${CMAKE_MATCH_1} + ${CMAKE_MATCH_2}")
if(flash GREATER BUDGET)
    message(FATAL_ERROR "fw size: flash ${flash} / ${BUDGET} B -> over budget by ${over} B")
endif()
```

POST_BUILD 명령들은 `&&` 로 이어지므로 게이트가 실패하면 뒤의 objcopy 가 안 돈다 → `fw.bin` 없음. 실측: 기본 `-- fw size: flash 504 / 8192 B, ram 40 B` + 링커의 `FLASH: 504 B 512 KB 0.10%`(M0+ 는 `1004 B ... 0.19%`), `-DFW_FLASH_BUDGET=256` → `fw size: flash 504 / 256 B -> over budget by 248 B` 로 ninja 실패. 링커 MEMORY 의 512K 는 "물리 한계", 예산은 "팀이 정한 한계"(부트로더·OTA 슬롯 여유)라 둘 다 둔다. 크기를 줄이는 방법은 TOOL-M13.

</details>

**P4 (생각만). 빌드 중 실행해야 하는 호스트 도구.** 레지스터 맵 헤더를 C 로 만든 생성기(`regmap_gen.c`)로 만들어야 한다. 타깃 트리에서 `add_executable(regmap_gen ...)` 을 하면 무엇이 문제이고, 어떻게 풀까?

<details>
<summary>P4 답</summary>

타깃 트리의 `add_executable` 은 **arm-none-eabi-gcc 로** 빌드된다 — 맥에서 실행할 수 없다(게다가 libc 도 없다). 해법: (a) 생성기를 호스트 트리에서 빌드해 설치하고 타깃 트리는 `find_program(REGMAP_GEN ...)`(PROGRAM=NEVER 라 호스트 경로에서 찾음)으로 찾는다, (b) 타깃 configure 안에서 `ExternalProject_Add` 로 호스트 하위 빌드를 돌린다, (c) 생성기를 python 으로 써서 문제 자체를 없앤다. CMake 는 크로스 빌드 시 실행 파일을 돌리는 `CMAKE_CROSSCOMPILING_EMULATOR`(예: qemu)도 지원하지만 베어메탈 생성기에는 맞지 않는다. 체커 대상 아님.

</details>

### 한 번에 검증

<!-- code: practice/CMakeLists.txt diff=solution/CMakeLists.txt -->

나머지 연습 파일: `practice/cmake/arm-none-eabi.cmake` (P2), `practice/cmake/fw_size_check.cmake` (P3), `practice/CMakePresets.json` (P1).

```bash
bash check.sh practice
```

실제 출력 (기본 12 항목은 solution 과 같아 생략):

```text
== practice: cmake version 4.5.0, ninja 1.13.2, arm-none-eabi-gcc (GCC) 16.2.0 ==
...
PASS T9 linker script edit -> relink: .isr_vector now at 0x08004000
-- practice --
PASS P1 cmake --preset host / --build / ctest --preset host: 3 tests passed
PASS P1 cmake --preset target: fw_board_rev=0x00000002 from the preset's BOARD_REV cache variable
PASS P2 -DFW_MCU=cortex-m0plus: Tag_CPU_arch v6S-M, soft-float, __aeabi_idiv linked from libgcc
PASS P2 unknown -DFW_MCU=cortex-m7 stops configure with a clear message
PASS P3 default build: fw size: flash 504 / 8192 B, ram 40 B
PASS P3 -DFW_FLASH_BUDGET=256: build fails, no fw.bin (fw size: flash 504 / 256 B -> over budget by 248 B)
18/18 passed
```

## 10. 핵심 정리

- 소스 트리 하나, **빌드 트리는 툴체인당 하나**: `build-host`(테스트) / `build-target`(`-DCMAKE_TOOLCHAIN_FILE=`). 분기는 `if(CMAKE_CROSSCOMPILING)` — CPU 이름(`"arm64" MATCHES "arm"`)이 아니라.
- 툴체인 파일 = CPU 를 위한 컴파일 방법: `CMAKE_SYSTEM_NAME Generic`, 컴파일러, `*_FLAGS_INIT`, `CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY`, `CMAKE_FIND_ROOT_PATH_MODE_*`. 링커 스크립트·`-nostdlib`·맵은 프로젝트.
- 툴체인 파일은 첫 configure 에 3 번(메인 2 + try_compile 1) 읽힌다 → 멱등, `set(CMAKE_C_FLAGS)` 금지(가림/중복), try_compile 에 넘길 변수는 `CMAKE_TRY_COMPILE_PLATFORM_VARIABLES`.
- `_INIT` 은 캐시의 첫 값(+ env `CFLAGS`). `-DCMAKE_C_FLAGS=` 는 그것을 **대체**한다 → 보드 설정은 프로젝트 캐시 변수 + `target_compile_definitions`.
- 링커 스크립트는 `LINK_DEPENDS` 로 그래프에 넣어야 수정 시 재링크된다.
- POST_BUILD 로 `objcopy -O binary`(504 B = Flash 시작 ~ `.data` LMA 끝, `.bss` 제외), `size`, `-Wl,-Map=`; 예산 게이트는 objcopy 앞에.
- 이 맥 gcc 16 은 newlib 없음(`cannot find crt0.o/-lc`), VM gcc 14 는 nano/nosys specs 로 링크 가능 — newlib 을 쓰면 `end` 심볼·시스템 콜 스텁이 링커 스크립트/보드 코드의 계약이 된다.

## 참고자료

- CMake `cmake-toolchains(7)` — Cross Compiling, toolchain files, `CMAKE_SYSROOT`, `CMAKE_FIND_ROOT_PATH_MODE_*`: https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html
- CMake 변수 `CMAKE_TRY_COMPILE_TARGET_TYPE`, `CMAKE_TRY_COMPILE_PLATFORM_VARIABLES`, `CMAKE_<LANG>_FLAGS_INIT`, `CMAKE_CROSSCOMPILING`: https://cmake.org/cmake/help/latest/manual/cmake-variables.7.html
- CMake 타깃 속성 `LINK_DEPENDS`, 명령 `add_custom_command(TARGET ... POST_BUILD)`: https://cmake.org/cmake/help/latest/prop_tgt/LINK_DEPENDS.html
- `cmake-presets(7)`: https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html
- Ninja manual — implicit dependencies, depfile, `-d explain`: https://ninja-build.org/manual.html
- GNU ld — `--print-memory-usage`, `-Map`; GCC ARM options (`-mcpu`, `-mfloat-abi`, multilib): https://sourceware.org/binutils/docs/ld/ , https://gcc.gnu.org/onlinedocs/gcc/ARM-Options.html
- newlib / libgloss nosys (`_sbrk`, `end`): https://sourceware.org/newlib/
- 관련 레슨: TOOL-B06 (size·text/data/bss), TOOL-B08 (Make·`-MMD`), TOOL-B14 (크로스 컴파일 첫걸음), TOOL-M01 (링커 스크립트), TOOL-M02 (맵 파일), TOOL-M04 (CMake 기초), TOOL-M12 (정적 분석), TOOL-M13 (크기 다이어트), BM-B08 (Cortex-M 부팅), ARM-M12 (HardFault)
