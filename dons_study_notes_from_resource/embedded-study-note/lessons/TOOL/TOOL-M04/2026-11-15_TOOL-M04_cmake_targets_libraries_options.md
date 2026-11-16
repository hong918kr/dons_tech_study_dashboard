---
id: TOOL-M04
time: 80분
updated: 2026-11-15
---
# TOOL-M04 CMake 기초: 타깃·라이브러리·옵션

## 학습 목표

- CMake 가 하는 일을 **configure → generate → build** 세 단계로 나누어 설명하고, 소스 트리(source tree)와 빌드 트리(build tree), 캐시 변수(cache variable)와 일반 변수(normal variable)의 차이를 실제 `CMakeCache.txt` · `build.ninja` · `compile_commands.json` 으로 보여 준다.
- 라이브러리가 **사용 요구사항(usage requirements)** 을 스스로 들고 다니게 한다: `target_include_directories(PUBLIC include PRIVATE src)`, `target_compile_definitions(PUBLIC …/PRIVATE …)`, 그래서 소비자(consumer)는 `target_link_libraries(app PRIVATE ringbuf)` 한 줄이면 된다.
- `include_directories` / `add_definitions` / `set(CMAKE_C_FLAGS …)` 가 **어디까지 새는지** 실측으로 알고, 그것이 만드는 실제 고장 다섯 가지(비공개 헤더 노출, 재사용 불가, 옵션 무시, CI 플래그 소실, 테스트 0 개인데 초록불)를 체커로 잡는다.
- `option(FEATURE_CRC …)` → 소스 목록 + PUBLIC 정의(`RB_HAVE_CRC=$<BOOL:…>`) 로 연결하고, OFF 일 때 심볼 집합이 바뀌는 것을 `nm` 으로 증명한다. 헤더 레이아웃을 바꾸는 정의를 PRIVATE 로 두면 **스택이 깨진다**는 것을 실제 abort / ASan 보고로 본다.
- `enable_testing()` + `add_test` + 라벨(labels) + CTest 로 단위 테스트를 돌리고, CI 에서 `--no-tests=error` 가 왜 필요한지 설명한다.

## 1. 문제

"CMake 쓸 줄 알아요?" 는 펌웨어 면접에서 이제 거의 기본 질문이다 — Zephyr, ESP-IDF, Pico SDK, STM32CubeIDE 의 CMake 프로젝트, 사내 HIL 테스트까지 전부 CMake 이고, 면접관이 진짜 보는 것은 **`target_*` 와 PUBLIC/PRIVATE 를 이해하느냐**(= 남이 재사용할 수 있는 컴포넌트를 만드느냐)다.

TOOL-B08 에서 Make 로 만든 펌웨어 호스트 빌드를 CMake 로 옮긴다. 이번에는 하드웨어 무관 부분을 **라이브러리 하나**로 떼어 낸다: 16 칸 바이트 링 버퍼 `ringbuf` (+ 선택 기능: 들어간 바이트의 누적 CRC-16). 앱과 단위 테스트, 그리고 **두 번째 제품(부트로더)** 이 이 라이브러리를 쓴다.

```text
proj/                                 # 공용 소스 (변형마다 같음)
  ringbuf/include/ringbuf.h           # PUBLIC  헤더: rb_t, rb_put/get, (RB_HAVE_CRC 면) rb_crc16
  ringbuf/src/rb_internal.h           # PRIVATE 헤더: RB_MASK, rb_idx(), RB_ASSERT
  ringbuf/src/ringbuf.c  rb_crc.c     # rb_crc.c 는 FEATURE_CRC=ON 일 때만
  app/main.c  app/sensor.c  app/sensor.h       # 앱 (sensor.h 는 앱 전용)
  tests/test_ringbuf.c                # 화이트박스 테스트: rb_internal.h 를 써도 되는 유일한 예외
  tests/test_crc.c                    # CRC 기능 테스트 (FEATURE_CRC=ON 일 때만)
<variant>/CMakeLists.txt  <variant>/ringbuf/CMakeLists.txt     # starter / solution (/ practice)
consumer/                             # 부트로더 프로젝트: add_subdirectory(ringbuf) 만 한다
probe/probe_private.c  probe_public.c # 체커가 덧붙이는 "새 앱 파일" 두 개
```

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m4a" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">사용 요구사항 — ringbuf 가 들고 다니는 것 / 안 내보내는 것</text>
  <rect x="220" y="34" width="300" height="150" rx="6" fill="none" stroke="currentColor"/>
  <text x="370" y="52" fill="currentColor" text-anchor="middle" font-weight="bold" font-family="monospace">ringbuf (STATIC)</text>
  <rect x="232" y="60" width="276" height="54" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="240" y="76" fill="currentColor" font-size="12">PRIVATE (ringbuf 컴파일에만)</text>
  <text x="240" y="91" fill="currentColor" font-size="12" font-family="monospace">-I ringbuf/src  (rb_internal.h)</text>
  <text x="240" y="105" fill="currentColor" font-size="12" font-family="monospace">-DRB_ASSERTS=1  -Wall..-Wundef</text>
  <rect x="232" y="120" width="276" height="56" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="240" y="136" fill="currentColor" font-size="12">PUBLIC (나 + 나를 링크하는 쪽)</text>
  <text x="240" y="151" fill="currentColor" font-size="12" font-family="monospace">-I ringbuf/include</text>
  <text x="240" y="166" fill="currentColor" font-size="12" font-family="monospace">-DRB_HAVE_CRC=1  c_std_11</text>
  <g font-family="monospace" font-size="12" fill="currentColor" text-anchor="middle">
    <rect x="20" y="230" width="120" height="30" rx="4" fill="none" stroke="currentColor"/><text x="80" y="249">app</text>
    <rect x="160" y="230" width="120" height="30" rx="4" fill="none" stroke="currentColor"/><text x="220" y="249">test_crc</text>
    <rect x="300" y="230" width="140" height="30" rx="4" fill="none" stroke="currentColor"/><text x="370" y="249">boot (다른 프로젝트)</text>
    <rect x="460" y="230" width="130" height="30" rx="4" class="f-mut" stroke="currentColor"/><text x="525" y="249">test_ringbuf</text>
    <rect x="610" y="230" width="120" height="30" rx="4" class="f-warn" stroke="currentColor"/><text x="670" y="249">probe_private</text>
  </g>
  <g stroke="currentColor" marker-end="url(#m4a)">
    <path d="M280 176 L90 228"/><path d="M330 176 L225 228"/><path d="M370 176 L370 228"/><path d="M450 176 L510 228"/>
  </g>
  <path d="M520 90 L565 228" stroke="currentColor" stroke-dasharray="2 3" marker-end="url(#m4a)"/>
  <path d="M520 70 L670 228" stroke="currentColor" stroke-dasharray="6 4"/>
  <text x="608" y="160" fill="currentColor" font-size="14" font-weight="bold">✗</text>
  <text x="20" y="292" fill="currentColor" font-size="12">초록 = target_link_libraries(X PRIVATE ringbuf) 만으로 X 에 자동으로 붙는다 (전이, transitive).</text>
  <text x="20" y="310" fill="currentColor" font-size="12">주황 = ringbuf 를 컴파일할 때만. test_ringbuf 는 src/ 를 스스로 명시(점선) — 예외는 보이게.</text>
  <text x="20" y="328" fill="currentColor" font-size="12">probe_private 가 rb_internal.h 를 include 하면 컴파일 에러가 나야 정상(✗).</text>
</svg>
```

### Part A — 타깃 셋: 라이브러리 / 앱 / 테스트

- `ringbuf/CMakeLists.txt` 에 `add_library(ringbuf STATIC …)`. include 디렉터리와 정의는 **타깃에** 건다: `include/` 는 PUBLIC, `src/` 는 PRIVATE.
- 최상위 `CMakeLists.txt`: `add_subdirectory(ringbuf)`, `add_executable(app …)` + `target_link_libraries(app PRIVATE ringbuf)`. 앱은 include 경로를 직접 적지 않는다.
- 경고는 컴파일러별 생성기 식(generator expression)으로 타깃마다: `"$<$<C_COMPILER_ID:AppleClang,Clang,GNU>:-Wall;-Wextra;-Werror;-Wundef>"`.
- `enable_testing()` + `add_test` 3 개(test_ringbuf, test_crc, app_smoke) + 라벨 `unit` / `smoke`. `ctest --output-on-failure` 가 3 개를 돌려야 한다.
- 빌드는 항상 소스 밖(out-of-source): `cmake -S src -B build -G Ninja`.

### Part B — starter 의 다섯 가지 함정 (체커가 각각 FAIL 줄로 잡음)

- (1) 최상위 `include_directories(ringbuf/include ringbuf/src)` — "화이트박스 테스트가 rb_internal.h 를 쓰니까 다 열어 두자". → 앱 코드가 비공개 헤더를 include 할 수 있다.
- (2) 최상위 `add_definitions(-DRB_HAVE_CRC=1 -DRB_ASSERTS=1)` + 라이브러리에서 `option(FEATURE_CRC)` 를 안 씀. → 라이브러리 전용 정의가 앱에 새고, `-DFEATURE_CRC=OFF` 가 아무 일도 안 한다.
- (3) `target_include_directories(ringbuf PRIVATE include src)` — 최상위의 전역 include 가 가려 주니 앱은 빌드된다. → 부트로더 프로젝트가 재사용하면 `'ringbuf.h' file not found`.
- (4) `enable_testing()` 없음. → `No tests were found!!!` 인데 **exit 0**.
- (5) `set(CMAKE_C_FLAGS "-std=c11 -Wall …")`. → CI 의 `-DCMAKE_C_FLAGS=-fsanitize=address` 가 조용히 사라진다.

### Part C — 펌웨어 현실: 재사용, CI, 생성기

- 같은 `ringbuf/` 를 부트로더(`consumer/`)가 `add_subdirectory` 로만 가져다 쓰기 — 컴포넌트 재사용의 실제 시험.
- CI 잡 세 개를 흉내: 기본 빌드 + ctest, `FEATURE_CRC=OFF` 빌드(심볼 집합 변화 `nm`), ASan 빌드(사용자 플래그 보존). 빌드 디렉터리만 다르다.
- `compile_commands.json` 내보내기(clangd / clang-tidy — TOOL-M12), Ninja vs Unix Makefiles 생성기, VM 의 gcc 15 + cmake 4.2 로 같은 트리. 크로스 툴체인 파일은 TOOL-M05 의 몫이다.

## 2. Problem (English)

> **Turn this "works on my machine" CMake into target-based CMake.**
>
> A small firmware host build has a static library `ringbuf` (public header in `ringbuf/include`, private header in `ringbuf/src`), an `app`, and two unit tests. An optional feature `FEATURE_CRC` adds a running CRC field to the public struct `rb_t` and the file `rb_crc.c`. The current build uses directory-wide `include_directories` / `add_definitions`, overwrites `CMAKE_C_FLAGS`, and never enables testing.
>
> Fix the two `CMakeLists.txt` files (no source changes) so that:
> 1. `cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build` builds everything and runs **3** tests; `ctest -L unit` runs 2.
> 2. A consumer needs only `target_link_libraries(x PRIVATE ringbuf)`; a consumer that includes `rb_internal.h` must **fail to compile**.
> 3. Another project can reuse `ringbuf/` with `add_subdirectory()` alone.
> 4. `-DFEATURE_CRC=OFF` removes `rb_crc16` from `libringbuf.a`, removes the CRC test, and the app prints `crc=off`.
> 5. `-DCMAKE_C_FLAGS=…` from the command line still reaches every compile; library-private macros never reach the app; `compile_commands.json` is exported.
>
> Follow-ups: configure vs generate vs build; PUBLIC vs PRIVATE vs INTERFACE; why a definition that changes a struct must be PUBLIC; cache vs normal variables; why `ctest` can be green with zero tests; when you need a generator expression.

Expected (solution):

```text
$ ./build/app
[app dev] samples=64 sum=4499 crc=0x39C4
$ ./build-off/app            # -DFEATURE_CRC=OFF
[app dev] samples=64 sum=4499 crc=off
```

## 3. 시작 코드

공개 헤더 — `RB_HAVE_CRC` 가 **구조체 레이아웃**을 바꾼다는 점이 이 레슨의 핵심 함정이다.

<!-- code: proj/ringbuf/include/ringbuf.h -->

고칠 파일 1 — `starter/CMakeLists.txt` (TODO 1–3):

<!-- code: starter/CMakeLists.txt -->

고칠 파일 2 — `starter/ringbuf/CMakeLists.txt` (TODO 4–5):

<!-- code: starter/ringbuf/CMakeLists.txt -->

재사용 시험용 두 번째 프로젝트 (체커가 `-DRINGBUF_DIR=<src>/ringbuf` 로 configure):

<!-- code: consumer/CMakeLists.txt -->

체커 — `proj/` + 변형 디렉터리를 `/tmp/TOOL-M04_work/<variant>/src` 에 깔고, 진짜 cmake 4.5 + Ninja + AppleClang + ctest + nm 으로 확인한다. 프로브(probe) 두 개는 "동료가 새 앱 파일을 추가하는" 상황을 흉내 내려고 복사본의 최상위 CMakeLists.txt 끝에 덧붙인다(레슨 폴더는 건드리지 않음).

<!-- code: check.sh -->

```bash
bash check.sh starter     # 6/14, exit 1
bash check.sh solution    # 14/14, exit 0   (맥에서 약 3.5 초)
bash check.sh practice    # 19/19, exit 0
```

`bash check.sh starter` 실제 출력:

```text
== starter: cmake version 4.5.0, ninja 1.13.2, Apple clang version 21.0.0 (clang-2100.0.123.102) ==
PASS configure -G Ninja (out-of-source: /tmp/TOOL-M04_work/starter/build)
PASS build all + app prints '[app dev] samples=64 sum=4499 crc=0x39C4'
FAIL ctest: 3 tests registered and passing
     No tests were found!!!  (ctest rc=0)
FAIL ctest -L unit (labels)
     No tests were found!!!
PASS compile_commands.json exported; -Wall..-Werror on ringbuf.c and main.c
FAIL definition scope on main.c
     -DRB_ASSERTS=1 -DRB_HAVE_CRC=1 
FAIL probe_private: app code can include the library's PRIVATE header
     build rc=0
PASS probe_public: only target_link_libraries(... ringbuf) needed
PASS second build: ninja: no work to do.
PASS touch rb_internal.h -> ninja -n compiles {rb_crc ringbuf test_ringbuf}
FAIL consumer/ (bootloader) reuse of ringbuf
     1 error: 'ringbuf.h' file not found;1 error: no member named 'crc' in 'rb_t'
FAIL FEATURE_CRC=OFF has no effect
     T _rb_* symbols ON=9 OFF=9; app: [app dev] samples=64 sum=4499 crc=0x39C4
FAIL FEATURE_CRC=OFF test list
     No tests were found!!!
FAIL -DCMAKE_C_FLAGS=-fsanitize=address was dropped
     cache: CMAKE_C_FLAGS:STRING=-fsanitize=address | ringbuf.c gets:  -std=c11 -Wall -Wextra -Werror -Wundef
6/14 passed
```

TOOL-B08 의 starter 처럼 **"첫 빌드는 멀쩡하다"**. 앱도 돌고 결과도 맞다. 틀린 것은 전부 두 번째 소비자, 두 번째 구성(OFF), 두 번째 빌드 잡(CI)에서 드러난다.

## 4. 힌트

<details>
<summary>힌트 1 — 전역 명령을 타깃 명령으로</summary>

`include_directories` → `target_include_directories(<tgt> PUBLIC|PRIVATE|INTERFACE …)`, `add_definitions` → `target_compile_definitions`, `CMAKE_C_FLAGS` 의 경고 → `target_compile_options`, `-std=c11` → `CMAKE_C_STANDARD` / `target_compile_features`. 기준: "**이 타깃을 링크하는 쪽도 이것이 필요한가?**" 예 → PUBLIC, 아니오 → PRIVATE.

</details>

<details>
<summary>힌트 2 — 어떤 정의가 PUBLIC 이어야 하나</summary>

공개 헤더 안의 `#if` 에 쓰이는 매크로는 라이브러리와 소비자가 **같은 값**을 봐야 한다. `RB_HAVE_CRC` 는 `rb_t` 에 필드를 하나 더한다. `RB_ASSERTS` 는 `rb_internal.h` 와 `.c` 안에서만 쓰인다. 옵션 값을 0/1 로 바꾸는 생성기 식은 `$<BOOL:${FEATURE_CRC}>`.

</details>

<details>
<summary>힌트 3 — 옵션이 소스 목록을 바꾸게</summary>

`if(FEATURE_CRC) target_sources(ringbuf PRIVATE src/rb_crc.c) endif()` 또는 소스 목록 안의 `$<$<BOOL:${FEATURE_CRC}>:src/rb_crc.c>`. 테스트도 같은 조건으로 만들고 등록해야 `OFF` 에서 2 개가 된다.

</details>

<details>
<summary>힌트 4 — 테스트와 플래그</summary>

`enable_testing()` 은 최상위에서 `add_test` 보다 먼저. 라벨은 `set_tests_properties(t PROPERTIES LABELS unit)`. `set(CMAKE_C_FLAGS …)` 는 지우고, 표준은 `set(CMAKE_C_STANDARD 11)` + `set(CMAKE_C_EXTENSIONS OFF)` — 이건 "앞으로 만들 타깃의 기본값" 이라 사용자 플래그를 덮지 않는다.

</details>

## 5. 풀이

### Part A — configure → generate → build, 그리고 타깃이 들고 다니는 것

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m4b" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">cmake -S src -B build -G Ninja ; cmake --build build ; ctest</text>
  <rect x="10" y="34" width="150" height="160" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="85" y="52" fill="currentColor" text-anchor="middle" font-weight="bold">소스 트리</text>
  <text x="20" y="74" fill="currentColor" font-size="12" font-family="monospace">CMakeLists.txt</text>
  <text x="20" y="92" fill="currentColor" font-size="12" font-family="monospace">*/CMakeLists.txt</text>
  <text x="20" y="110" fill="currentColor" font-size="12" font-family="monospace">*.c *.h *.in</text>
  <text x="20" y="140" fill="currentColor" font-size="12">읽기만 한다</text>
  <text x="20" y="156" fill="currentColor" font-size="12">(실측: 빌드 후</text>
  <text x="20" y="172" fill="currentColor" font-size="12"> 추가 파일 0 개)</text>
  <rect x="190" y="34" width="150" height="70" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="265" y="54" fill="currentColor" text-anchor="middle" font-weight="bold">1 configure</text>
  <text x="200" y="74" fill="currentColor" font-size="12">CMake 언어 실행,</text>
  <text x="200" y="90" fill="currentColor" font-size="12">컴파일러 검사 (0.4s)</text>
  <rect x="190" y="124" width="150" height="70" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="265" y="144" fill="currentColor" text-anchor="middle" font-weight="bold">2 generate</text>
  <text x="200" y="164" fill="currentColor" font-size="12">생성기 식 평가,</text>
  <text x="200" y="180" fill="currentColor" font-size="12">빌드 파일 쓰기 (0.0s)</text>
  <path d="M160 70 L188 70" stroke="currentColor" marker-end="url(#m4b)"/>
  <path d="M265 104 L265 122" stroke="currentColor" marker-end="url(#m4b)"/>
  <rect x="370" y="34" width="190" height="160" rx="6" fill="none" stroke="currentColor"/>
  <text x="465" y="52" fill="currentColor" text-anchor="middle" font-weight="bold">빌드 트리</text>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="380" y="74">CMakeCache.txt</text>
    <text x="380" y="92">build.ninja</text>
    <text x="380" y="110">compile_commands.json</text>
    <text x="380" y="128">CTestTestfile.cmake</text>
    <text x="380" y="152">*.o libringbuf.a app</text>
    <text x="380" y="170">.ninja_deps</text>
  </g>
  <path d="M340 70 L368 70" stroke="currentColor" marker-end="url(#m4b)"/>
  <path d="M340 160 L368 110" stroke="currentColor" marker-end="url(#m4b)"/>
  <rect x="590" y="34" width="140" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="660" y="54" fill="currentColor" text-anchor="middle" font-weight="bold">3 build</text>
  <text x="600" y="74" fill="currentColor" font-size="12">ninja (cmake 없이</text>
  <text x="600" y="90" fill="currentColor" font-size="12">돌아도 됨) 0.26s</text>
  <rect x="590" y="124" width="140" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="660" y="144" fill="currentColor" text-anchor="middle" font-weight="bold">4 ctest</text>
  <text x="600" y="164" fill="currentColor" font-size="12">CTestTestfile 를</text>
  <text x="600" y="180" fill="currentColor" font-size="12">읽어 테스트 실행</text>
  <path d="M560 92 L588 70" stroke="currentColor" marker-end="url(#m4b)"/>
  <path d="M560 128 L588 156" stroke="currentColor" marker-end="url(#m4b)"/>
  <text x="10" y="220" fill="currentColor" font-size="12">CMakeLists.txt 가 바뀌면 build.ninja 의 규칙이 cmake 를 다시 불러 1·2 를 자동으로 다시 한다(re-run).</text>
  <text x="10" y="238" fill="currentColor" font-size="12">-D 로 준 값은 CMakeCache.txt 에 남는다 → 다음 configure 에도 유지 (S2 실측).</text>
</svg>
```

`demo_cmake.sh` S1 실측 (`$D` = `/tmp/TOOL-M04_work/demo`):

```text
-- The C compiler identification is AppleClang 21.0.0.21000099
-- Check for working C compiler: /usr/bin/cc - skipped
-- Configuring done (0.4s)
-- Generating done (0.0s)
-- Build files have been written to: $D/sol-b
--- build tree after configure+generate (nothing compiled yet):
build.ninja
cmake_install.cmake
CMakeCache.txt
CMakeFiles/
compile_commands.json
CTestTestfile.cmake
ringbuf/
--- source tree after configure+build: files added/removed (diff of find lists):
(none)
```

- **configure**: CMakeLists.txt 를 위에서 아래로 *실행*한다 (CMake 는 스크립트 언어다). 컴파일러를 찾고 검사하고, 캐시를 쓴다. 이때는 아직 "어떤 컴파일러 ID 냐", "어느 구성(Debug/Release)이냐" 같은 값 일부가 확정되지 않을 수 있다 — 그래서 **생성기 식** `$<…>` 은 다음 단계에서 타깃마다 평가된다.
- **generate**: 타깃 그래프 + 사용 요구사항 전파(transitive propagation)를 계산해 `build.ninja` (또는 Makefile)와 `compile_commands.json`, `CTestTestfile.cmake` 를 쓴다. 0.0 s — 계산은 싸다.
- **build**: 이제 CMake 는 빠지고 Ninja 가 돈다. Ninja 는 컴파일러가 쓴 `.d` 를 `.ninja_deps` 로 흡수해 헤더 의존성을 안다 — TOOL-B08 에서 손으로 하던 `-MMD -MP -include` 를 공짜로 얻는다:

```text
######## S4 (ninja -t deps, 시스템 헤더는 grep 으로 뺌)
CMakeFiles/app.dir/app/main.c.o: #deps 59, deps mtime 1794792888682077093 (VALID)
    $D/sol/app/main.c
    $D/sol/ringbuf/include/ringbuf.h
    $D/sol/app/sensor.h
--- touch ringbuf.h; ninja -n -d explain (first lines):
ninja explain: output ringbuf/CMakeFiles/ringbuf.dir/src/ringbuf.c.o older than most recent input $D/sol/ringbuf/include/ringbuf.h (…)
[1/9] Building C object ringbuf/CMakeFiles/ringbuf.dir/src/ringbuf.c.o
```

`#deps 59` = 시스템 헤더 포함 — CMake 는 `-MD`(B08 의 `-MMD` 아님)를 쓴다(`ninja -t commands` 실측: `-MD -MT … -MF ….o.d`). `sensor.c` 는 ringbuf.h 를 안 쓰므로 9 단계에 없다.

**사용 요구사항 전파.** `target_link_libraries(app PRIVATE ringbuf)` 는 "링크 줄에 `libringbuf.a`" 만이 아니다. ringbuf 의 `INTERFACE_INCLUDE_DIRECTORIES`, `INTERFACE_COMPILE_DEFINITIONS`, `INTERFACE_COMPILE_FEATURES` 가 app 의 **컴파일** 줄에 붙는다. 실측 `compile_commands.json` (solution):

```text
main.c    : /usr/bin/cc -DRB_HAVE_CRC=1 -I…/src/app -I…/src/ringbuf/include  -arch arm64 -isysroot …/MacOSX26.4.sdk -std=c11 -Wall -Wextra -Werror -Wundef …
ringbuf.c : /usr/bin/cc -DRB_ASSERTS=1 -DRB_HAVE_CRC=1 -I…/src/ringbuf/include -I…/src/ringbuf/src  -arch arm64 -isysroot … -std=c11 -Wall …
```

키워드 표 (각 `target_*` 명령 공통):

| 키워드 | 이 타깃 컴파일에 | 이 타깃을 링크하는 쪽에 | 이 레슨의 예 |
|---|---|---|---|
| `PRIVATE` | O | X | `src/`, `RB_ASSERTS=1`, 경고 플래그 |
| `PUBLIC` | O | O (전이) | `include/`, `RB_HAVE_CRC=0/1`, `c_std_11` |
| `INTERFACE` | X | O | 헤더 전용 라이브러리, 9. P2 의 `fw_warnings` |

`target_link_libraries(app PRIVATE ringbuf)` 의 PRIVATE 는 "app 을 또 링크하는 누군가에게 ringbuf 의 요구사항을 넘기지 않는다" 는 뜻이다 (실행 파일이라 상관없지만 라이브러리끼리는 중요). `-isysroot …/MacOSX26.4.sdk` 는 우리가 쓴 게 아니라 `SDKROOT` 환경 변수를 보고 CMake 가 붙였다(관찰).

**캐시 변수는 끈적하다.** `option()` 은 캐시 BOOL 을 만든다. 실측 S2:

```text
// Running CRC-16 over the ring buffer stream
FEATURE_CRC:BOOL=ON
after -DFEATURE_CRC=OFF : FEATURE_CRC:BOOL=OFF
after plain 'cmake <b>' : FEATURE_CRC:BOOL=OFF   <- still OFF
after --fresh           : FEATURE_CRC:BOOL=ON
```

한 번 `-D` 로 준 값은 다음 configure 에도 남는다. `--fresh` 는 캐시를 버리고 다시 시작하는데, **생성기 선택도 버린다** — `-G Ninja` 없이 `--fresh` 하자 `CMAKE_GENERATOR:INTERNAL=Unix Makefiles` 로 바뀌었고, 그 뒤 `-G Ninja` 로 같은 폴더를 configure 하자 `Does not match the generator used previously: Unix Makefiles` 에러(실측). 구성이 다르면 **빌드 폴더를 나눈다**(`build`, `build-off`, `build-asan` — 체커가 그렇게 한다).

### Part B — 다섯 가지 함정, 각각의 실제 증거

**(1) `include_directories` 는 디렉터리 범위로 샌다.** 정확히 어디까지 새는지 실험(`/tmp/TOOL-M04_scope`):

```text
add_subdirectory(sub_before)      # sb
add_executable(early m.c)
include_directories(/INC_DIR)
add_definitions(-DDEF_X)
add_executable(late m.c)
add_subdirectory(sub_after)       # sa
------------------------------------------------
early: -DDEF_X -I/INC_DIR     <- 명령 "앞에" 만든 같은 디렉터리 타깃에도!
late:  -DDEF_X -I/INC_DIR
sb:                           <- 앞서 추가된 하위 디렉터리만 무사
sa:    -DDEF_X -I/INC_DIR
```

starter 는 최상위에 두었으니 app, 테스트, ringbuf 자신, 그리고 체커가 덧붙인 `probe_private` 까지 전부 `ringbuf/src` 를 본다. 결과: `probe_private` 빌드 rc=0 — 앱 코드가 `RB_MASK`, `rb_idx()` 에 손을 댈 수 있다. 다음 사람이 그걸 쓰면 라이브러리 내부를 못 바꾸게 된다(캡슐화 붕괴). solution 에서는 clang 이 `'rb_internal.h' file not found`, VM gcc 는 `fatal error: rb_internal.h: No such file or directory` (demo_vm V4).

**(2) `add_definitions` 도 같은 범위로 새고, 옵션은 연결이 안 됐다.** `main.c` 컴파일 줄에 `-DRB_ASSERTS=1` (라이브러리 내부 디버그 매크로)이 있다. 그리고 `option(FEATURE_CRC)` 은 캐시 변수만 만들 뿐, 아무것도 그 값을 읽지 않으니 OFF 가 무시된다: `T _rb_* symbols ON=9 OFF=9`. solution 에서 OFF 는 `9 -> 6` — VM ELF 로 보면 정확히 `rb_crc.c.o` 멤버(`rb_crc16`, `rb_crc_step`, `rb_running_crc`) 3 개가 사라진다 (demo_vm V5):

```text
--- ON:                               --- OFF:
ringbuf.c.o:                          ringbuf.c.o:
0000000000000018 T rb_assert_fail     0000000000000018 T rb_assert_fail
0000000000000080 T rb_count           0000000000000074 T rb_count
…                                     …
rb_crc.c.o:                           [app dev] samples=64 sum=4499 crc=off
0000000000000088 T rb_crc16           100% tests passed, 0 tests failed out of 2
0000000000000000 T rb_crc_step
00000000000000e4 T rb_running_crc
```

(`rb_put` 이 0xa4 → 0x98 로 작아진 것은 CRC 갱신 호출이 빠졌기 때문. 맥 Mach-O 의 nm 에서는 `_rb_crc16` 처럼 밑줄이 붙는다.)

**왜 `RB_HAVE_CRC` 는 PUBLIC 이어야 하나 — 실측 S8.** solution 의 정의를 일부러 `PRIVATE RB_HAVE_CRC=…` 로 바꾸면 라이브러리는 `sizeof(rb_t)=28`, 앱은 `24` 로 본다:

```text
--- (a) with -Wundef -Werror on the app (solution's FW_WARNINGS):
$D/abi/ringbuf/include/ringbuf.h:16:5: error: 'RB_HAVE_CRC' is not defined, evaluates to 0 [-Werror,-Wundef]
--- (b) same, but app built without -Wundef:
build ok
demo_cmake.sh: line 101:  8930 Abort trap: 6           "$D/abi-b/app"
app rc=134
--- (b2) same, app compiled with -fno-stack-protector:
[app dev] samples=64 sum=4499 crc=off
app rc=0
--- (c) same with -fsanitize=address:
==9131==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x… at pc 0x… bp 0x… sp 0x…
WRITE of size 2 at 0x… thread T0
    #0 0x… in rb_init ringbuf.c:19
    #1 0x… in main main.c:31
```

- (a) `-Wundef` 가 첫 방어선: 정의가 아예 안 온 것을 컴파일 에러로 바꾼다. 그래서 헤더는 `#ifdef` 가 아니라 `#if RB_HAVE_CRC` + "항상 0 또는 1 로 정의" 규칙을 쓴다 (`$<BOOL:…>` 가 항상 0/1 을 준다).
- (b) 경고가 없으면 링크는 깨끗하다(링커는 이름만 본다). `rb_init` 이 앱 스택의 24 바이트 `rb_t` 너머 offset 24 에 `crc` 2 바이트를 쓴다(ringbuf.c:19 = `rb->crc = 0xFFFFu`). 맥 clang 의 스택 보호(stack protector) 카나리가 깨져 abort (rc 134 — 해석은 추정이나 b2 에서 보호를 끄자 abort 가 사라진 것이 근거).
- (b2) 가장 나쁜 경우: 조용히 "성공", 그런데 `crc=off` — 라이브러리는 CRC 를 계산했는데 앱은 모른다. MCU 에서는 이게 기본값이다(카나리도 ASan 도 없음). TOOL-B08 의 `FILTER_TAPS` 크래시와 같은 계열: **헤더는 ABI 계약**이고, 그 계약을 바꾸는 매크로는 계약의 일부(PUBLIC)다.

**(3) PRIVATE include 는 전역 hack 에 가려진다.** starter 의 `target_include_directories(ringbuf PRIVATE include src)` — 앱은 최상위 `include_directories` 덕에 빌드되니 아무도 모른다. 부트로더가 `ringbuf/` 만 가져가면 드러난다: `'ringbuf.h' file not found` 와 함께 `no member named 'crc' in 'rb_t'` — `RB_HAVE_CRC` 도 최상위 `add_definitions` 에서 오고 있었고, `rb_crc.c` 는 옵션과 무관하게 항상 컴파일되기 때문이다. 즉 starter 의 ringbuf 는 **최상위 파일 없이는 존재할 수 없는** 라이브러리였다. 체커는 Ninja 를 `-k 0` 으로 돌려 두 에러가 항상 둘 다 나오게(결정적으로) 했다.

**(4) 테스트가 등록되지 않았는데 초록불.** `add_test()` 는 `enable_testing()` 이 없으면 조용히 무시된다 — 빌드 트리에 `CTestTestfile.cmake` 가 아예 없다(실측 `ls`). 그리고:

```text
$ ctest --output-on-failure ; echo rc=$?        $ ctest --no-tests=error ; echo rc=$?
Test project /tmp/TOOL-M04_work/starter/build-off   Test project …/starter/build-off
No tests were found!!!                              No tests were found!!!
rc=0                                                Errors while running CTest
                                                    rc=8
```

CI 가 `ctest` 의 exit code 만 보면 **테스트 0 개 = 통과**다. CI 에서는 `--no-tests=error` (또는 테스트 개수 확인)를 쓴다. 덤: 인자 없이 그냥 `ctest` 를 치면 `No test configuration file found!` + 사용법 출력 + rc=1 이었다 — 인자 유무로 동작이 달랐다(실측, 4.5).

**(5) `set(CMAKE_C_FLAGS …)` 는 캐시를 가린다.**

```svg
<svg viewBox="0 0 740 210" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m4c" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">${CMAKE_C_FLAGS} 를 읽을 때: 일반 변수가 있으면 캐시는 안 본다</text>
  <rect x="10" y="34" width="320" height="56" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="54" fill="currentColor" font-size="12">캐시 (CMakeCache.txt, -D 로 들어옴)</text>
  <text x="20" y="74" fill="currentColor" font-size="12" font-family="monospace">CMAKE_C_FLAGS=-fsanitize=address</text>
  <rect x="10" y="110" width="320" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="130" fill="currentColor" font-size="12">일반 변수 (최상위 set(), 하위로 복사됨)</text>
  <text x="20" y="150" fill="currentColor" font-size="12" font-family="monospace">"-std=c11 -Wall -Wextra -Werror -Wundef"</text>
  <path d="M330 138 L388 116" stroke="currentColor" stroke-width="2" marker-end="url(#m4c)"/>
  <path d="M330 62 L388 98" stroke="currentColor" stroke-dasharray="4 4"/>
  <text x="352" y="74" fill="currentColor" font-size="14" font-weight="bold">✗</text>
  <rect x="392" y="80" width="338" height="56" rx="6" fill="none" stroke="currentColor"/>
  <text x="402" y="100" fill="currentColor" font-size="12">ringbuf.c 컴파일 줄 (starter, 실측)</text>
  <text x="402" y="120" fill="currentColor" font-size="12" font-family="monospace">-std=c11 -Wall … -Wundef (ASan 없음)</text>
  <text x="10" y="192" fill="currentColor" font-size="12">solution: CMAKE_C_FLAGS 를 건드리지 않음 → 줄에 -fsanitize=address 가 남고, 경고는 타깃 옵션으로 따로 붙는다.</text>
</svg>
```

CMake 는 변수를 읽을 때 **일반 변수 → 없으면 캐시** 순서로 찾는다. 최상위 `set(CMAKE_C_FLAGS "…")` 는 일반 변수를 만들어 캐시 값을 가리고, `add_subdirectory` 는 일반 변수를 하위로 복사하므로 ringbuf 도 가려진다. 실측 S3:

```text
cache  : CMAKE_C_FLAGS:STRING=-fsanitize=address
command: /usr/bin/cc -DRB_ASSERTS=1 -DRB_HAVE_CRC=1 -I… -std=c11 -Wall -Wextra -Werror -Wundef -arch arm64 … -c …/ringbuf.c
solution command: /usr/bin/cc -DRB_ASSERTS=1 -DRB_HAVE_CRC=1 -I… -fsanitize=address -std=c11 -arch arm64 … -Wall -Wextra -Werror -Wundef … -c …/ringbuf.c
```

"CI 에 ASan 잡이 있다" 고 믿었는데 실제로는 그냥 빌드였다 — 가장 비싼 종류의 버그다. `CMAKE_C_FLAGS` 는 **사용자/툴체인 파일의 것**이다(TOOL-M05 에서 툴체인 파일이 `CMAKE_C_FLAGS_INIT` 로 CPU 플래그를 넣는다). 프로젝트는 타깃 속성으로 말한다. 꼭 덧붙여야 하면 `string(APPEND CMAKE_C_FLAGS " …")` 가 덮어쓰기보다는 낫지만, 여전히 전역이다.

**`-std=` 는 누가 정하나 — 실측 S5** (AppleClang 기본 = `C_STANDARD_COMPUTED_DEFAULT 17`, 확장 ON):

| `cmake_minimum_required` | 설정 | 실제로 붙은 플래그 |
|---|---|---|
| 3.20 | `target_compile_features(c_std_11)` + `CMAKE_C_EXTENSIONS OFF` | (없음) — 컴파일러 기본 gnu17 그대로 |
| 3.22 | 같음 | `-std=c17` |
| 3.20 | `CMAKE_C_STANDARD 11` | `-std=gnu11` |
| 3.20 | `CMAKE_C_STANDARD 11` + `CMAKE_C_EXTENSIONS OFF` | `-std=c11` |

1·2 행의 차이는 정책 CMP0128(3.22 도입) 때문이다: `cmake_minimum_required(VERSION x)` 는 "x 까지의 정책을 NEW 로" 라는 뜻이라, 같은 파일이 최소 버전 숫자 하나로 다른 플래그를 낸다. 교훈: compile feature 는 "**최소** 요구"(라이브러리가 PUBLIC 으로 선언), 정확한 표준은 프로젝트가 `CMAKE_C_STANDARD` + `CMAKE_C_EXTENSIONS OFF` 로 정한다 — solution 이 그렇게 했고 6 개 컴파일 줄 모두 `-std=c11`.

### Part C — 재사용, CI, 생성기

**재사용 = 컴포넌트의 진짜 시험.** solution 에서 `consumer/` 는 `add_subdirectory(${RINGBUF_DIR} ringbuf)` + `target_link_libraries(boot PRIVATE ringbuf)` 두 줄만으로 빌드된다: `[boot] accepted=16 last=P sizeof(rb_t)=28 lib=28` (20 바이트 중 16 개만 들어가고, 앱과 라이브러리의 구조체 크기가 일치). 부트로더 프로젝트는 최상위 `CMAKE_C_STANDARD` 를 안 정했는데도 ringbuf 의 PUBLIC `c_std_11` 가 최소 요구를 보장한다. Zephyr 모듈, ESP-IDF 컴포넌트, Pico SDK 라이브러리가 전부 이 모양이다(각자 `zephyr_library_*`, `idf_component_register` 같은 래퍼를 쓰지만 밑은 `target_*` 이다).

**CI 잡 = 빌드 디렉터리.** 체커 자체가 CI 의 축소판이다: 같은 소스 트리 하나에 `build/`(기본), `build-off/`(기능 OFF), `build-asan/`(플래그) 세 빌드 트리. 소스 트리를 안 건드리니 병렬로 돌려도 된다. `compile_commands.json` 은 generate 단계에서 써지므로 ASan 잡은 **configure 만으로** 검증할 수 있었다 — clang-tidy(TOOL-M12)도 이 파일만 있으면 된다.

**생성기(generator) 비교 — 실측 S9 (맥, 3 회, 노이즈 있음):**

| 생성기 | 클린 빌드 `-j 8` | 아무것도 안 바뀐 재빌드 |
|---|---|---|
| Ninja 1.13.2 | 0.26 / 0.26 / 0.25 s | 0.05 s |
| Unix Makefiles (GNU Make 3.81) | 0.59 / 0.64 / 0.63 s | 0.34 s |

파일 10 개짜리라 절대값은 의미가 적고, no-op 재빌드 차이(Makefile 생성기는 타깃마다 하위 make 를 여러 번 띄운다)가 큰 프로젝트에서 체감된다. 더 중요한 차이는 VM 실측 V6(GNU Make 4.4.1):

```text
--- after the first build: CMakeFiles/app.dir/compiler_depend.make
# Empty compiler generated dependencies file for app.
--- touch ringbuf.h; make -n | grep 'Building' (dry run):
0
--- real make:
[ 10%] Building C object ringbuf/CMakeFiles/ringbuf.dir/src/ringbuf.c.o
[ 20%] Building C object ringbuf/CMakeFiles/ringbuf.dir/src/rb_crc.c.o
[ 40%] Building C object CMakeFiles/app.dir/app/main.c.o
[ 70%] Building C object CMakeFiles/test_ringbuf.dir/tests/test_ringbuf.c.o
[ 90%] Building C object CMakeFiles/test_crc.dir/tests/test_crc.c.o
```

Makefile 생성기는 컴파일러의 `.d` 를 **다음 빌드의 'depend' 단계**에서 `compiler_depend.make` 로 합친다. `make -n` 은 그 단계를 실행하지 않으니 "할 일 없음(0)" 으로 보이지만 실제 `make` 는 정확히 5 개를 다시 만든다. B08 에서 배운 "`make -n` 으로 증명" 은 CMake 가 만든 Makefile 에는 그대로 통하지 않는다 — Ninja 의 `ninja -n` 은 `.ninja_deps` 를 바로 읽으므로 정확하다(체커가 Ninja 로 이 검사를 하는 이유).

**VM gcc 15 + cmake 4.2 (demo_vm V2–V3):** 같은 solution 이 수정 없이 빌드되고 3/3 통과. 생성기 식이 GNU 쪽 가지를 골라 `-std=c11 -Wall -Wextra -Werror -Wundef` 가 붙었다. ctest 요약 문구는 버전마다 다르다: 4.2 `100% tests passed, 0 tests failed out of 3` / 4.5 `100% tests passed out of 3` — 체커는 둘 다 맞는 정규식을 쓴다(로그 파싱 스크립트를 짤 때 주의).

## 6. 정답 코드

최상위 — 전역 상태를 걷어 내고, 테스트를 등록하고, 경고를 타깃에:

<!-- code: solution/CMakeLists.txt diff=starter/CMakeLists.txt -->

라이브러리 — 옵션이 소스와 PUBLIC 정의를 바꾸고, include 는 PUBLIC/PRIVATE 로 나눈다:

<!-- code: solution/ringbuf/CMakeLists.txt diff=starter/ringbuf/CMakeLists.txt -->

`bash check.sh solution` 실제 출력:

```text
== solution: cmake version 4.5.0, ninja 1.13.2, Apple clang version 21.0.0 (clang-2100.0.123.102) ==
PASS configure -G Ninja (out-of-source: /tmp/TOOL-M04_work/solution/build)
PASS build all + app prints '[app dev] samples=64 sum=4499 crc=0x39C4'
PASS ctest: 100% tests passed out of 3
PASS ctest -L unit: exactly 2 tests (test_ringbuf, test_crc)
PASS compile_commands.json exported; -Wall..-Werror on ringbuf.c and main.c
PASS main.c gets PUBLIC -DRB_HAVE_CRC=1, not the library's PRIVATE RB_ASSERTS
PASS probe_private: #include "rb_internal.h" fails ('rb_internal.h' file not found)
PASS probe_public: only target_link_libraries(... ringbuf) needed
PASS second build: ninja: no work to do.
PASS touch rb_internal.h -> ninja -n compiles {rb_crc ringbuf test_ringbuf}
PASS consumer/ (bootloader) builds with add_subdirectory(ringbuf) alone: [boot] accepted=16 last=P sizeof(rb_t)=28 lib=28
PASS FEATURE_CRC=OFF: rb_crc16 gone (T _rb_* 9 -> 6), app prints crc=off
PASS FEATURE_CRC=OFF: test_crc not built, 2 tests pass
PASS -DCMAKE_C_FLAGS=-fsanitize=address reaches ringbuf.c (CI ASan job really is ASan)
14/14 passed
```

바뀐 줄이 왜 그런지 한 줄씩:

| 바뀐 곳 | 이유 |
|---|---|
| `set(CMAKE_C_FLAGS …)` 삭제 → `CMAKE_C_STANDARD 11` + `CMAKE_C_EXTENSIONS OFF` | 사용자/CI/툴체인 플래그 보존. 이 두 변수는 "이후 만들 타깃 속성의 기본값" 이지 플래그 문자열이 아니다 |
| `include_directories` / `add_definitions` 삭제 | 사용 요구사항은 ringbuf 가 스스로 선언 |
| `FW_WARNINGS` 생성기 식 (`;` 로 구분, 따옴표) | 컴파일러별 플래그, generate 때 평가. 따옴표 안의 공백은 함정(7. 실수 4) |
| `target_include_directories(test_ringbuf PRIVATE ringbuf/src)` | 화이트박스 테스트만의 예외 — 숨기지 말고 그 타깃에 명시 |
| `enable_testing()` + `set_tests_properties(… LABELS …)` | CTestTestfile 생성, `ctest -L unit` |
| `if(FEATURE_CRC)` 로 test_crc 감싸기 | 기능이 없으면 테스트도 없음 (2 개) |
| `$<$<BOOL:${FEATURE_CRC}>:src/rb_crc.c>` | 옵션 → 소스 목록 |
| `PUBLIC RB_HAVE_CRC=$<BOOL:…>` / `PRIVATE RB_ASSERTS=1` | 레이아웃을 바꾸는 매크로는 계약(PUBLIC), 내부 디버그는 PRIVATE |
| `PUBLIC include PRIVATE src` | 소비자는 링크만으로 ringbuf.h 를 얻고, rb_internal.h 는 못 본다 |
| `target_compile_features(ringbuf PUBLIC c_std_11)` | 라이브러리의 최소 요구 — 단독 재사용(부트로더)에서도 보장 |

## 7. 자주 하는 실수

**1. "빌드되니까 맞다".** starter 는 첫 빌드·앱 출력 모두 정상이었다. CMake 버그는 두 번째 소비자 / 다른 옵션 / 다른 플래그에서 드러난다 → 최소한 OFF 구성 하나, 외부 소비자 하나를 CI 에 둔다.

**2. 공개 헤더의 `#if` 매크로를 PRIVATE 로.** 실측: 경고 없이 링크되고, 스택 보호가 있으면 abort, 없으면 조용히 틀린 값(`crc=off`). → PUBLIC + 헤더에서 `#if` (0/1) + `-Wundef`.

**3. `option()` 값을 바꿨는데 반영이 안 된다.** 캐시는 끈적하다(S2). 옛 값이 남은 빌드 폴더를 재사용하지 말고, 구성마다 폴더를 나누거나 `--fresh` (이때 `-G` 도 다시!).

**4. 생성기 식 따옴표.** 실측 S7:

```text
UNQUOTED: $<$<C_COMPILER_ID:AppleClang>:-Wall -Wextra>         -> -Wall -Wextra
QUOTED_SPACE: "$<$<C_COMPILER_ID:AppleClang>:-Wall -Wextra>"   -> "-Wall -Wextra"
   warning: unknown warning option '-Wall -Wextra'; did you mean '-Wextra'? [-Wunknown-warning-option]
QUOTED_SEMI: "$<$<C_COMPILER_ID:AppleClang>:-Wall;-Wextra>"    -> -Wall -Wextra
```

따옴표 없는 형태는 의외로 동작했다 — 인자 두 개(`$<…:-Wall`, `-Wextra>`)가 속성에 `;` 로 이어 붙은 뒤 생성기 식이 **합쳐진 문자열**에서 평가되기 때문으로 보인다(관찰 기반 해석). 따옴표 + 공백은 플래그 하나 `"-Wall -Wextra"` 가 되어 경고만 내고 `-Wall` 이 꺼진다. `-Werror` 없는 빌드라면 아무도 모른다. 규칙: **따옴표 + `;`**.

**5. 일반 이름 오타.** `target_link_libraries(app PRIVATE ringbuff)` 는 configure 를 통과한다(rc=0) — CMake 는 타깃이 아닌 이름을 링커 플래그 `-lringbuff` 로 취급한다. 실측 결과는 링크 에러가 아니라 그 전에 `fatal error: 'ringbuf.h' file not found` (사용 요구사항도 같이 안 왔으니까). `fw::ringbuff` 처럼 `::` 가 있는 이름은 configure 에서 바로 `but the target was not found` (9. P1).

**6. `enable_testing()` 을 하위 디렉터리에만.** ctest 는 빌드 트리 **최상위**의 `CTestTestfile.cmake` 에서 시작한다(CMake 문서: 최상위 소스 디렉터리에 두라고 함). 하위에만 두면 최상위에서 `No tests were found!!!` — 그리고 rc=0.

**7. 소스 트리 안에서 빌드 (`cmake .`).** CMakeCache.txt, CMakeFiles/ 가 소스와 섞여 git 이 더러워지고, 설정이 다른 두 번째 빌드를 못 만든다. 늘 `-S . -B build…`. 생성 파일(9. P3 의 build_info.h)도 `${PROJECT_BINARY_DIR}` 에만.

**8. 생성된 Makefile 에 `make -n`.** VM 실측: 헤더를 touch 해도 `make -n` 은 0 개, 실제 make 는 5 개. 증명이 필요하면 Ninja 의 `ninja -n -d explain`.

## 8. 면접 Q&A

<details>
<summary>Q1. PUBLIC / PRIVATE / INTERFACE 를 설명하고, 각각 언제 쓰나요?</summary>

`target_*` 명령은 두 속성에 쓴다: 이 타깃 자신의 것(`INCLUDE_DIRECTORIES` 등)과 이 타깃을 링크하는 쪽에 넘길 것(`INTERFACE_INCLUDE_DIRECTORIES` 등). PRIVATE = 앞쪽만, INTERFACE = 뒤쪽만, PUBLIC = 둘 다. 판단 기준은 "**공개 헤더가 그것을 필요로 하나**": 공개 헤더 경로, 공개 헤더의 `#if` 매크로, 공개 헤더가 쓰는 의존 라이브러리 → PUBLIC. 구현 파일만 쓰는 것(내부 헤더, 내부 디버그 매크로, 경고 플래그) → PRIVATE. 소스가 없는 헤더 전용 라이브러리나 "플래그 묶음" 타깃 → INTERFACE. 꼬리: "`target_link_libraries(libA PUBLIC libB)` 는?" → libA 의 공개 헤더가 libB 의 타입을 쓴다는 뜻 — libA 를 쓰는 쪽도 libB 의 include 와 링크가 필요하다.

</details>

<details>
<summary>Q2. include_directories / add_definitions 를 왜 피하나요? 정확히 어디까지 적용되죠?</summary>

디렉터리 속성이라 **현재 디렉터리의 모든 타깃(명령보다 먼저 만든 것 포함!)과 이후에 추가되는 하위 디렉터리**에 붙는다(실측: early/late/sa 에 붙고, 먼저 추가된 sb 만 무사). 그래서 (1) 라이브러리 비공개 헤더가 앱에 보이고, (2) 진짜 필요한 요구사항이 전역에 가려져 라이브러리를 혼자 떼어 쓰면 깨진다(실측: 부트로더에서 `'ringbuf.h' file not found`). 타깃 명령은 요구사항을 **그 타깃의 그래프 위로만** 흘려 보낸다. 레거시 SDK 를 감쌀 때 등 예외는 있지만, 새 코드는 `target_*` 만.

</details>

<details>
<summary>Q3. configure, generate, build 는 각각 무엇을 하고, 어떤 에러가 어느 단계에서 나나요?</summary>

configure = CMake 언어 실행 + 컴파일러 탐지/검사 + 캐시 기록. 문법 오류, `find_package` 실패, 잘못된 `-G` 변경이 여기서. generate = 타깃 그래프와 생성기 식 평가 → build.ninja/Makefile, compile_commands.json, CTestTestfile. 링크 그래프 검사도 여기라서 `::` 타깃 오타는 실측으로 `-- Configuring done` **다음에** 보고되고 `CMake Generate step failed.  Build files cannot be regenerated correctly.` 로 끝나며 build.ninja 가 안 생긴다. build = 생성기 도구(Ninja/make)가 컴파일·링크 — 컴파일 에러, 링크 에러. CMakeLists.txt 가 바뀌면 빌드 도구가 cmake 를 다시 불러 configure+generate 를 자동 반복한다. 실측 시간: configure 0.4 s, generate 0.0 s, 빌드 0.26 s.

</details>

<details>
<summary>Q4. 캐시 변수와 일반 변수는 어떻게 다르고, 왜 set(CMAKE_C_FLAGS ...) 가 문제인가요?</summary>

캐시 변수는 CMakeCache.txt 에 남아 다음 configure 에도 유지되고 `-D` 나 cmake-gui/ccmake 로 사용자가 바꾼다(`option()` 도 캐시 BOOL). 일반 변수는 configure 한 번, 디렉터리 범위로만 존재하고 `add_subdirectory` 때 하위로 복사된다. `${VAR}` 은 일반 변수를 먼저 보므로, `set(CMAKE_C_FLAGS "…")` 는 사용자의 `-DCMAKE_C_FLAGS=-fsanitize=address` 를 가린다 — 실측으로 CI ASan 잡에서 ASan 이 사라졌다. 이 변수는 사용자와 툴체인 파일의 것이다. 꼬리: "`option()` 이 같은 이름의 일반 변수가 있을 때는?" → 정책 CMP0077(3.13) 이후 NEW 면 일반 변수를 존중하고 캐시를 안 만든다(확인 권장).

</details>

<details>
<summary>Q5. 생성기 식(generator expression)은 왜 필요하고, if() 와 무엇이 다른가요?</summary>

`if()` 는 configure 시점에 한 번 평가된다. 하지만 컴파일러 ID(타깃 언어별), 빌드 구성(멀티-config 생성기에서는 Debug/Release 가 빌드 때 결정), 다른 타깃의 속성(`$<TARGET_PROPERTY:…>`, `$<TARGET_FILE:…>`)은 generate 때 타깃마다 알 수 있다. 그래서 `$<$<C_COMPILER_ID:GNU>:…>`, `$<$<CONFIG:Debug>:…>`, `$<BOOL:${opt}>`. 이 레슨에서 같은 FW_WARNINGS 가 맥에서는 AppleClang 가지, VM 에서는 GNU 가지를 골랐다(실측). 단점은 디버깅 — `file(GENERATE OUTPUT … CONTENT "$<…>")` 로 평가 결과를 파일로 찍어 본다(체커 P2 가 그렇게 `app` 의 LINK_LIBRARIES 를 확인).

</details>

<details>
<summary>Q6. 헤더의 struct 레이아웃을 바꾸는 매크로를 PRIVATE 로 두면 무슨 일이 생기나요?</summary>

라이브러리와 소비자가 같은 이름의 타입을 다른 크기로 컴파일한다(실측 28 vs 24 바이트). 링커는 심볼 이름만 맞추므로 경고가 없다. 라이브러리가 소비자의 스택 객체 너머에 쓴다 → 맥에서는 스택 보호가 abort(rc 134), 보호를 끄면 조용히 틀린 결과(`crc=off`), ASan 은 `stack-buffer-overflow … WRITE of size 2 … in rb_init ringbuf.c:19`. MCU 에서는 카나리도 ASan 도 없으니 이웃 변수 오염 → 나중에 엉뚱한 곳에서 HardFault. 방어: PUBLIC 정의, 헤더에서 `#if X` + 항상 0/1 로 정의 + `-Wundef -Werror`, 그리고 `rb_struct_size()` 같은 런타임 ABI 확인(테스트와 부트로더가 실제로 비교함).

</details>

<details>
<summary>Q7. CTest 로 펌웨어 단위 테스트를 CI 에 붙인다면? 함정은?</summary>

`enable_testing()`(최상위) 또는 `include(CTest)`(= enable_testing + `BUILD_TESTING` 옵션 + 대시보드 타깃), `add_test(NAME … COMMAND <타깃 이름>)` — 타깃 이름을 쓰면 실행 파일 경로를 CMake 가 채운다. 라벨로 묶어 `ctest -L unit` (빠름, 매 커밋) / `-L hil` (보드 필요, 야간) 식으로 나눈다. `TIMEOUT` 으로 멈춘 테스트를 잘라내고, `WILL_FAIL` 로 "실패해야 정상" 테스트, `PASS_REGULAR_EXPRESSION` 으로 출력 확인. 함정: 테스트가 0 개여도 `ctest --output-on-failure` 는 rc=0 (실측) → CI 는 `--no-tests=error`(rc=8 실측). 그리고 `--output-on-failure` 가 없으면 실패한 테스트의 출력이 로그에 안 남는다.

</details>

<details>
<summary>Q8. 한 소스 트리에서 호스트 테스트 빌드와 Cortex-M 펌웨어 빌드를 어떻게 나누나요?</summary>

빌드 트리를 나눈다: `build-host/`(호스트 컴파일러, 단위 테스트 + ctest), `build-target/`(`-DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake`). 하드웨어 무관 코드(ringbuf, CRC, 프로토콜 파서)는 라이브러리 타깃으로 만들어 두 쪽에서 같이 쓰고, 테스트 타깃은 `if(NOT CMAKE_CROSSCOMPILING)` 같은 조건으로 호스트에서만. CPU 플래그·링커 스크립트·try_compile 설정은 툴체인 파일에 — 프로젝트가 `CMAKE_C_FLAGS` 를 덮어쓰면 그게 다 날아간다(이 레슨 함정 5). 자세한 건 TOOL-M05.

</details>

## 9. 추가 연습

`practice/CMakeLists.txt` = solution 최상위 + 아래 P1–P4 (`ringbuf/CMakeLists.txt` 는 solution 그대로 — 체커가 solution/ 위에 practice/ 를 덮어 깐다). 체커는 기본 14 항목(테스트 수 4, 버전 문자열 `1.4.2`)에 5 항목을 더 확인한다.

**P1. 네임스페이스 별칭(ALIAS).** `fw::ringbuf` 로 링크하게 하고, 오타가 configure 에서 잡히는 것을 확인하라.

<details>
<summary>P1 답</summary>

`add_library(fw::ringbuf ALIAS ringbuf)`. `::` 가 들어간 이름은 CMake 가 "반드시 타깃" 으로 취급해서, 오타면 링커 플래그로 넘기지 않고 configure 에서 멈춘다. 실측(S6):

```text
CMake Error at CMakeLists.txt:21 (target_link_libraries):
  Target "app" links to:

    fw::ringbuff

  but the target was not found.  Possible reasons include:
    * There is a typo in the target name.
```

일반 이름 `ringbuff` 오타는 configure rc=0 이고 빌드에서야 `'ringbuf.h' file not found`. 같은 이름을 `find_package()` 의 IMPORTED 타깃(`fw::ringbuf`)과 맞춰 두면, 나중에 add_subdirectory ↔ 설치된 패키지를 바꿔도 소비자 코드가 그대로다. 별칭은 보통 `add_library` 바로 옆(ringbuf/ 안)에 두지만, 이 연습은 파일 하나로 하려고 최상위에 두었다.

</details>

**P2. 경고를 INTERFACE 타깃으로.** 변수 `FW_WARNINGS` 대신 소스 없는 `fw_warnings` 타깃에 `-Wshadow -Wconversion` 까지 담아 app 과 테스트에 링크하라. ringbuf 는 자기 경고 집합을 유지.

<details>
<summary>P2 답</summary>

`add_library(fw_warnings INTERFACE)` + `target_compile_options(fw_warnings INTERFACE "$<…:-Wall;…;-Wconversion>")` + `target_link_libraries(app PRIVATE fw::ringbuf fw::warnings)`. "플래그 묶음도 타깃" 이라 같은 PUBLIC/PRIVATE 규칙으로 흐르고, PRIVATE 로 링크했으니 app 을 쓰는 쪽에는 안 간다. 체커는 `file(GENERATE)` 로 평가 결과를 찍어 확인한다 — `app_links=` 에 `fw::warnings`, main.c 줄에 `-Wconversion`, ringbuf.c 줄에는 없음. 코드가 `-Wconversion` 에서도 깨끗하게 빌드됐다(실측).

</details>

**P3. 생성 헤더는 빌드 트리에.** `app/build_info.h.in` 에서 `configure_file` 로 `FW_VERSION_STR`(= `project(VERSION 1.4.2)`), `FW_BOARD`(캐시 STRING), `FEATURE_CRC`(0/1) 를 담은 헤더를 만들고 앱 출력에 버전을 넣어라.

<details>
<summary>P3 답</summary>

`configure_file(app/build_info.h.in ${PROJECT_BINARY_DIR}/generated/build_info.h @ONLY)` + app 에만 `PRIVATE ${PROJECT_BINARY_DIR}/generated`. `#cmakedefine01 FEATURE_CRC` 는 `#define FEATURE_CRC 1` / `0` 으로 바뀐다(실측: 기본 트리 1, OFF 트리 0). main.c 는 `__has_include("build_info.h")` 로 있을 때만 쓴다 → 앱 출력 `[app 1.4.2] samples=64 …`. 소스 트리에 build_info.h 가 없음을 체커가 `find` 로 확인. `@ONLY` 는 `${…}` 를 건드리지 않게 해서 C 코드 안의 `${}` 와 충돌을 막는다. configure_file 은 내용이 같으면 파일을 다시 쓰지 않는다 — 재configure 전후 `stat -f %m` 이 1794792976 그대로(실측) — 그래서 재configure 가 불필요한 재컴파일을 일으키지 않는다.

</details>

**P4. 음성 테스트와 타임아웃.** 인자를 주면 exit 2 로 거부하는 앱의 동작을 테스트로 고정하고(`app --bogus`), 모든 테스트에 10 초 타임아웃.

<details>
<summary>P4 답</summary>

`add_test(NAME app_rejects_args COMMAND app --bogus)` + `WILL_FAIL TRUE` + `LABELS negative`, 그리고 각 테스트에 `TIMEOUT 10`. 체커는 `ctest -L negative` 1/1 통과와, `ctest --show-only=json-v1` 의 JSON 에서 TIMEOUT ≤ 10 인 테스트가 4 개인지 센다. HIL 에서는 보드가 멈추면 테스트가 영원히 기다리므로 TIMEOUT 이 필수다. 실측 ctest 라벨 요약: `negative = 0.00 sec*proc (1 test)`, `smoke … (1 test)`, `unit … (2 tests)`.

</details>

**P5 (생각만). `CMAKE_BUILD_TYPE` 와 멀티-config.** `-G "Ninja Multi-Config"` 에서는 `CMAKE_BUILD_TYPE` 가 무시되고 `cmake --build b --config Release` 로 고른다. 그래서 `if(CMAKE_BUILD_TYPE STREQUAL Debug)` 대신 `$<$<CONFIG:Debug>:…>` 를 써야 두 생성기 모두에서 맞다. 체커 대상은 아님.

### 한 번에 검증

<!-- code: practice/CMakeLists.txt -->

```bash
bash check.sh practice
```

실제 출력 (기본 14 항목은 solution 과 같은 문구라 일부 생략):

```text
== practice: cmake version 4.5.0, ninja 1.13.2, Apple clang version 21.0.0 (clang-2100.0.123.102) ==
PASS configure -G Ninja (out-of-source: /tmp/TOOL-M04_work/practice/build)
PASS build all + app prints '[app 1.4.2] samples=64 sum=4499 crc=0x39C4'
PASS ctest: 100% tests passed out of 4
…
PASS FEATURE_CRC=OFF: test_crc not built, 3 tests pass
PASS -DCMAKE_C_FLAGS=-fsanitize=address reaches ringbuf.c (CI ASan job really is ASan)
-- practice --
PASS P1 fw::ringbuf alias links
PASS P1 typo fw::ringbuff -> configure error (rc=1): target was not found
PASS P2 app links fw::warnings; -Wconversion on main.c only (ringbuf keeps its own set)
PASS P3 build_info.h generated in <build>/generated (CRC 1 / OFF tree 0), source tree untouched
PASS P4 ctest -L negative: app_rejects_args passes (WILL_FAIL); TIMEOUT<=10 on all 4 tests
19/19 passed
```

## 10. 핵심 정리

- CMake = configure(언어 실행·캐시) → generate(생성기 식·빌드 파일·compile_commands) → build(Ninja/make). 소스 트리는 읽기 전용, 구성마다 빌드 트리 하나.
- 라이브러리가 사용 요구사항을 들고 다닌다: 공개 헤더 경로·공개 헤더의 `#if` 매크로 = PUBLIC, 내부 헤더·내부 매크로·경고 = PRIVATE. 소비자는 `target_link_libraries` 한 줄.
- `include_directories` / `add_definitions` 는 같은 디렉터리의 앞뒤 타깃 + 이후 하위 디렉터리로 샌다 → 비공개 헤더 노출, 단독 재사용 불가.
- 구조체를 바꾸는 매크로를 PRIVATE 로 두면 경고 없는 ABI 불일치(실측 28 vs 24 B → abort / 조용한 오답). `#if X` + 0/1 정의 + `-Wundef`.
- `option()` → 소스 목록 + PUBLIC 정의(`$<BOOL:…>`) + 테스트 목록. 캐시는 끈적하다(`--fresh` 는 생성기도 잊는다).
- `CMAKE_C_FLAGS` 는 사용자/툴체인의 것: `set()` 하면 CI 플래그가 사라진다. 표준은 `CMAKE_C_STANDARD` + `EXTENSIONS OFF`.
- `enable_testing()` 없으면 테스트 0 개 + rc=0. CI 는 `ctest --output-on-failure --no-tests=error`, 라벨로 나누고 TIMEOUT.
- 생성기 식은 `"…;…"` (따옴표 + 세미콜론). 헤더 의존성 증명은 `ninja -n -d explain` (생성된 Makefile 의 `make -n` 은 못 믿는다).

## 참고자료

- CMake 문서 — `target_include_directories`, `target_compile_definitions`, `target_link_libraries`, `include_directories`, `add_definitions`: https://cmake.org/cmake/help/latest/manual/cmake-commands.7.html
- cmake-buildsystem(7) — Build Specification and Usage Requirements, Transitive Usage Requirements: https://cmake.org/cmake/help/latest/manual/cmake-buildsystem.7.html
- cmake-generator-expressions(7): https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html
- cmake-language(7) — Variables (normal vs cache lookup), cmake-policies(7) — CMP0077, CMP0128: https://cmake.org/cmake/help/latest/manual/cmake-policies.7.html
- ctest(1) — `--no-tests`, `-L`, `--show-only=json-v1`; test properties `LABELS`, `TIMEOUT`, `WILL_FAIL`, `PASS_REGULAR_EXPRESSION`: https://cmake.org/cmake/help/latest/manual/ctest.1.html
- Daniel Pfeifer, "Effective CMake" (C++Now 2017) — target-centric CMake 의 고전 발표
- Craig Scott, *Professional CMake: A Practical Guide*
- 관련 레슨: TOOL-B08 (Make, -MMD), TOOL-B09 (정적 라이브러리·링크 순서), TOOL-B11 (ASan), TOOL-M05 (툴체인 파일 크로스 빌드), TOOL-M12 (clang-tidy 와 compile_commands.json), TOOL-M13 (size diet)
