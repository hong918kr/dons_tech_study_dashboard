---
id: TOOL-B08
time: 60분
updated: 2026-11-15
---
# TOOL-B08 Make 기초: 패턴 규칙·자동 변수·-MMD 의존성

## 학습 목표

- `src/ include/ build/` 구조의 Makefile 을 **패턴 규칙(pattern rule) 하나 + 자동 변수(automatic variable) `$@ $< $^`** 로 쓰고, `$(wildcard)` · 치환 참조(`$(SRCS:src/%.c=build/%.o)`)로 파일 목록을 손으로 관리하지 않는다.
- `-MMD -MP` 가 만드는 `.d` 파일을 한 줄씩 읽고, `-include $(DEPS)` 로 헤더 의존성을 붙인다. 헤더 하나를 `touch` 했을 때 `make -n` 이 **정확히 그 헤더에 기대는 오브젝트(직접 + 간접 include)와 재링크만** 보여 주는지 검증한다.
- 순서 전용 선행 조건(order-only prerequisite, `| $(BUILD)`), `.PHONY`, `-MP` 가 각각 **어떤 실제 고장**을 막는지 재현해서 설명한다 (끝없는 재빌드, `No rule to make target`, `` `clean' is up to date``).
- 헤더 의존성이 빠진 빌드가 "느린 빌드" 가 아니라 **구조체 레이아웃 불일치로 죽는 펌웨어**가 된다는 것을 실제 크래시로 본다.
- 맥의 GNU Make 3.81(2006) 과 VM 의 4.4.1 차이(`--trace`, `--output-sync`, `!=`, `$(file)`, `&:`, 타임스탬프 해상도)를 실측으로 알고, 같은 Makefile 로 Cortex-M4 크로스 빌드까지 돌린다.

## 1. 문제

"헤더 하나 고쳤는데 왜 펌웨어가 이상하게 죽죠?" / "CI 는 매번 클린 빌드라 40분 걸려요" — SSD 컨트롤러나 MCU 펌웨어 빌드에서 **증분 빌드(incremental build)가 정확한가**는 매일 겪는 문제이고, 면접에서도 "Makefile 에서 헤더 의존성은 어떻게 처리하나요?" 로 자주 나온다.

Make 는 아주 단순한 규칙 하나로 움직인다: **타깃(target)보다 더 새로운(mtime 이 큰) 선행 조건(prerequisite)이 있으면 레시피(recipe)를 다시 돌린다.** 그래서 make 가 **모르는** 의존성(예: `.c` 가 include 하는 헤더)은 아무리 바뀌어도 재빌드를 일으키지 않는다. 이 레슨의 과제는 그 DAG(의존 그래프)를 컴파일러가 대신 적게 만들고, 그것이 맞는지 `make -n` 으로 증명하는 것이다.

프로젝트 (`proj/`, 6 개 `.c` + 3 개 `.h`, 빌드 산출물은 전부 `build/`):

| 파일 | include 하는 헤더 | 비고 |
|---|---|---|
| `src/main.c` | `hal.h`, `dsp.h` | `static mavg_t g_filt;` — 레이아웃이 `FILTER_TAPS` 에 달림 |
| `src/uart.c` | `hal.h`, `board.h` | stdout 으로 쓰는 가짜 UART |
| `src/sensor.c` | `hal.h`, `board.h` | LCG 가짜 ADC |
| `src/log.c` | `hal.h` | `[tag] msg` 줄 |
| `src/filter.c` | `dsp.h` | 이동 평균 (`FILTER_TAPS` 칸) |
| `src/crc.c` | `dsp.h` | CRC-16/CCITT-FALSE |
| `include/dsp.h` | **`board.h`** | `mavg_t { int32_t buf[FILTER_TAPS]; ... }` |

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b8a" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">의존 DAG — touch include/board.h 하면 주황만 다시 만든다</text>
  <rect x="40" y="36" width="130" height="34" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="105" y="58" fill="currentColor" text-anchor="middle" font-family="monospace">board.h ★</text>
  <rect x="300" y="36" width="130" height="34" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="365" y="58" fill="currentColor" text-anchor="middle" font-family="monospace">dsp.h</text>
  <rect x="560" y="36" width="130" height="34" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="625" y="58" fill="currentColor" text-anchor="middle" font-family="monospace">hal.h</text>
  <path d="M300 53 L174 53" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#b8a)"/>
  <text x="237" y="47" fill="currentColor" text-anchor="middle" font-size="12">#include</text>
  <g stroke="currentColor" stroke-width="1">
    <path d="M105 70 L190 150"/><path d="M105 70 L310 150"/>
    <path d="M365 70 L70 150"/><path d="M365 70 L550 150"/><path d="M365 70 L670 150"/>
    <path d="M625 70 L70 150"/><path d="M625 70 L190 150"/><path d="M625 70 L310 150"/><path d="M625 70 L430 150"/>
  </g>
  <g font-family="monospace" font-size="12" fill="currentColor" text-anchor="middle">
    <rect x="20" y="150" width="100" height="28" rx="4" fill="none" stroke="currentColor"/><text x="70" y="169">main.c</text>
    <rect x="140" y="150" width="100" height="28" rx="4" fill="none" stroke="currentColor"/><text x="190" y="169">uart.c</text>
    <rect x="260" y="150" width="100" height="28" rx="4" fill="none" stroke="currentColor"/><text x="310" y="169">sensor.c</text>
    <rect x="380" y="150" width="100" height="28" rx="4" fill="none" stroke="currentColor"/><text x="430" y="169">log.c</text>
    <rect x="500" y="150" width="100" height="28" rx="4" fill="none" stroke="currentColor"/><text x="550" y="169">filter.c</text>
    <rect x="620" y="150" width="100" height="28" rx="4" fill="none" stroke="currentColor"/><text x="670" y="169">crc.c</text>
  </g>
  <g stroke="currentColor" marker-end="url(#b8a)">
    <path d="M70 178 L70 216"/><path d="M190 178 L190 216"/><path d="M310 178 L310 216"/>
    <path d="M430 178 L430 216"/><path d="M550 178 L550 216"/><path d="M670 178 L670 216"/>
  </g>
  <g font-family="monospace" font-size="12" fill="currentColor" text-anchor="middle">
    <rect x="20" y="218" width="100" height="28" rx="4" class="f-warn" stroke="currentColor"/><text x="70" y="237">main.o</text>
    <rect x="140" y="218" width="100" height="28" rx="4" class="f-warn" stroke="currentColor"/><text x="190" y="237">uart.o</text>
    <rect x="260" y="218" width="100" height="28" rx="4" class="f-warn" stroke="currentColor"/><text x="310" y="237">sensor.o</text>
    <rect x="380" y="218" width="100" height="28" rx="4" class="f-ok" stroke="currentColor"/><text x="430" y="237">log.o</text>
    <rect x="500" y="218" width="100" height="28" rx="4" class="f-warn" stroke="currentColor"/><text x="550" y="237">filter.o</text>
    <rect x="620" y="218" width="100" height="28" rx="4" class="f-warn" stroke="currentColor"/><text x="670" y="237">crc.o</text>
  </g>
  <g stroke="currentColor"><path d="M70 246 L350 290"/><path d="M190 246 L355 290"/><path d="M310 246 L362 290"/><path d="M430 246 L378 290"/><path d="M550 246 L385 290"/><path d="M670 246 L390 290"/></g>
  <rect x="300" y="290" width="140" height="30" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="370" y="310" fill="currentColor" text-anchor="middle" font-family="monospace">build/app</text>
  <text x="10" y="345" fill="currentColor" font-size="12">main.c 는 board.h 를 안 쓰지만 dsp.h 를 거쳐 기댄다(점선) → 간접 include 도 .d 에.</text>
</svg>
```

검증 표 (체커가 `make -n` 으로 확인하는 정답):

| 바꾼 파일 | 다시 컴파일할 오브젝트 | 재링크 |
|---|---|---|
| `include/board.h` | crc filter main sensor uart (log 제외) | 예 |
| `include/hal.h` | log main sensor uart | 예 |
| `include/dsp.h` | crc filter main | 예 |
| `src/crc.c` | crc | 예 |
| `build/` 안에 아무 파일 추가 | 없음 | 아니오 |
| 아무것도 안 바꿈 | 없음 (`make -q` exit 0) | 아니오 |

### Part A — 패턴 규칙 + 자동 변수 + out-of-tree 빌드

- `starter.mk` 는 "레거시" Makefile 이다: 소스 목록을 손으로 적고, 파일마다 규칙을 복사해 두었다. 이것을 `$(wildcard src/*.c)` + 규칙 **하나** `$(BUILD)/%.o: src/%.c` 로 바꾼다. 레시피는 `$<`(첫 선행 조건) 와 `$@`(타깃)만 쓴다.
- 새 드라이버 `src/watchdog.c` 를 추가하면 **Makefile 수정 없이** 컴파일·링크돼야 한다.
- 산출물은 전부 `build/` (out-of-tree). `make clean` 은 `build/` 만 지운다.

### Part B — 헤더 의존성과 세 가지 함정

- `-MMD -MP` 로 `build/x.d` 를 만들고 `-include $(DEPS)` 로 읽는다 → 위 표가 정확히 나와야 한다.
- 함정 1: 빌드 디렉터리를 **보통** 선행 조건으로 두면, 디렉터리 mtime 이 바뀔 때마다 전체 재빌드. → 순서 전용 `| $(BUILD)`.
- 함정 2: 어떤 `.c` 가 더 이상 include 하지 않는 헤더를 지웠더니 `No rule to make target`. → `-MP`.
- 함정 3: 누군가 프로젝트 루트에 `clean` 이라는 파일을 만들었더니 `make clean` 이 아무것도 안 한다. → `.PHONY`.
- 그리고 왜 이게 중요한지: 헤더 의존성이 없는 starter 로 `FILTER_TAPS` 를 4 → 16 으로 바꾸면 **실제로 크래시**가 난다 (5. 풀이).

### Part C — 펌웨어 현실: -j8, 크로스 빌드, Make 버전

- `make -j8` 클린 빌드가 같은 결과를 내야 한다 (CI 는 병렬 빌드).
- 같은 Makefile 을 VM 의 GNU Make 4.4.1 + gcc 로, 그리고 `CC=arm-none-eabi-gcc BUILD=build-m4` 오버라이드로 Cortex-M4 이미지까지 빌드한다 (`--trace` 로 "왜 다시 만드나" 보기, `arm-none-eabi-size`).
- 맥 `/usr/bin/make` 는 **3.81** 이다 — 4.x 기능을 쓰면 조용히 다르게 동작하는 것들을 실측표로 정리한다.

## 2. Problem (English)

> **Fix the firmware Makefile so incremental builds are exactly right.**
>
> The tree is `src/*.c`, `include/*.h`, and every artifact must go to `build/`. The legacy Makefile lists sources by hand, copies one rule per file, has no header dependencies, uses the build directory as a normal prerequisite, and declares no phony targets.
>
> Rewrite it (must work on **GNU Make 3.81** — the macOS default — and on 4.x) so that:
> 1. `make` builds `build/app`; a second `make` does nothing (`make -q` exits 0).
> 2. After `touch include/board.h`, `make -n` lists **exactly** the objects that include it directly or transitively, plus one relink — nothing else. Same for every header and source.
> 3. Creating a file inside `build/` does not trigger rebuilds.
> 4. Deleting a header that no source includes any more does not break the build.
> 5. A new `src/*.c` is compiled and linked with no Makefile edit; `make -j8` from clean works; `make clean` works even if a file named `clean` exists.
>
> Constraints: one pattern rule for objects, automatic variables only in the recipe, the compiler generates the dependency files. Follow-ups: what is in a `.d` file; why `-MP`; what an order-only prerequisite is; why a missing header dependency can crash firmware rather than just slow the build; how you would make the build react to a `CFLAGS` change.

Example after `touch include/dsp.h`:

```text
$ make -n
cc ... -c src/crc.c -o build/crc.o
cc ... -c src/filter.c -o build/filter.o
cc ... -c src/main.c -o build/main.o
cc  build/crc.o build/filter.o build/log.o build/main.o build/sensor.o build/uart.o -o build/app
```

## 3. 시작 코드

레슨 폴더 구성:

```text
TOOL-B08/
  proj/src/{main,uart,sensor,log,filter,crc}.c   proj/include/{board,hal,dsp}.h   proj/tests/test_dsp.c
  starter.mk  solution.mk  practice.mk           # 변형(variant) = 프로젝트에 Makefile 로 복사될 파일
  check.sh                                       # 체커 (맥 GNU Make 3.81)
  demo_make.sh  demo_compat.sh  demo_vm.sh       # 관찰용 데모 (+ *_out.txt 에 실제 출력)
```

간접 include 의 열쇠인 헤더:

<!-- code: proj/include/dsp.h -->

고칠 파일 — `starter.mk` (TODO 5 개):

<!-- code: starter.mk -->

체커 — 변형 Makefile 을 `/tmp/TOOL-B08_work/<variant>/Makefile` 로 복사하고, 진짜 make 로 빌드한 뒤 `make -n` 출력에서 `-o .../X.o` 를 뽑아 기대 집합과 비교한다. 타임스탬프는 `touch -t` 로 고정한다(이유는 5. 풀이 Part C — 맥 make 3.81 은 **초 단위**로만 비교한다).

<!-- code: check.sh -->

```bash
bash check.sh starter     # 일부 FAIL, exit 1
bash check.sh solution    # 13/13, exit 0
bash check.sh practice    # 23/23, exit 0  (9. 추가 연습)
```

`bash check.sh starter` 실제 출력:

```text
== starter: GNU Make 3.81 ==
PASS full build, app prints '[app] taps=4 samples=64 crc=0x8C7F'
PASS second make -> {} relink=0
PASS make -q all: up to date (exit 0)
FAIL touch include/board.h (via dsp.h too)
     want {crc filter main sensor uart} relink=1, got {} relink=0
FAIL touch include/hal.h
     want {log main sensor uart} relink=1, got {} relink=0
FAIL touch include/dsp.h
     want {crc filter main} relink=1, got {} relink=0
PASS touch src/crc.c -> {crc} relink=1
FAIL new file inside build/ (dir mtime bumped)
     want {} relink=0, got {crc filter log main sensor uart} relink=1
PASS deleted header (no longer included): build continues, log.o rebuilt
FAIL new src/watchdog.c
     watchdog.o missing
PASS make clean: build/ gone, 9 sources kept
PASS make -j8 from clean: 6 objects, same output
FAIL file named 'clean'
     make: `clean' is up to date.
7/13 passed
```

starter 는 **클린 빌드는 멀쩡하다** — 그래서 레거시 Makefile 이 오래 살아남는다. 틀린 곳은 전부 "두 번째 빌드부터" 다.

## 4. 힌트

<details>
<summary>힌트 1 — 패턴 규칙과 목록 만들기</summary>

`%` 는 타깃과 선행 조건에서 같은 문자열(stem)로 맞춰진다. `build/main.o` 를 만들 때 `$(BUILD)/%.o: src/%.c` 의 stem 은 `main` 이고 선행 조건은 `src/main.c` 가 된다. 목록은 `SRCS := $(wildcard src/*.c)`, `OBJS := $(SRCS:src/%.c=$(BUILD)/%.o)`, `DEPS := $(OBJS:.o=.d)`. 레시피에는 파일 이름을 쓰지 말고 `$<` 와 `$@` 만.

</details>

<details>
<summary>힌트 2 — 의존성은 컴파일러가 컴파일하면서 적는다</summary>

clang/gcc 의 `-MMD` 는 컴파일하는 김에 `-o` 이름 옆에 `.d` 를 쓴다 (`build/main.o` → `build/main.d`). 내용은 make 문법 그대로라 `-include` 로 읽으면 끝. 첫 빌드에는 `.d` 가 없지만 상관없다 — 어차피 `.o` 도 없어서 전부 컴파일된다. `include` 앞의 `-` 가 "없으면 조용히 넘어가" 다.

</details>

<details>
<summary>힌트 3 — 디렉터리는 "있기만 하면 되는" 선행 조건</summary>

디렉터리의 mtime 은 안에 파일이 생기거나 지워질 때마다 바뀐다. `.o` 가 디렉터리에 보통 의존하면, 마지막 `.o` 나 `app` 이 생기는 순간 디렉터리가 앞의 `.o` 들보다 새로워진다. `|` 뒤에 둔 선행 조건은 "없으면 먼저 만든다, 있으면 시각은 안 본다" 다.

</details>

<details>
<summary>힌트 4 — -MP 와 .PHONY</summary>

`-MP` 는 `.d` 끝에 헤더마다 `include/hal.h:` 같은 빈 규칙을 붙인다. 헤더가 사라져도 make 는 "규칙은 있는데 레시피가 없는 타깃 → 만들어졌다고 치자" 로 넘어간다. `.PHONY: all clean` 은 "이 이름은 파일이 아니다" — 같은 이름의 파일이 있어도 항상 레시피를 돈다.

</details>

## 5. 풀이

### Part A — 규칙 하나가 여섯 개를 대신한다

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">패턴 규칙 해부 — make 가 build/main.o 를 만들 때</text>
  <rect x="10" y="34" width="720" height="40" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="24" y="60" fill="currentColor" font-family="monospace" font-size="14">$(BUILD)/%.o  :  src/%.c   |  $(BUILD)</text>
  <text x="24" y="98" fill="currentColor" font-size="12">타깃 = $@</text>
  <text x="24" y="116" fill="currentColor" font-size="12" font-family="monospace">build/main.o</text>
  <text x="200" y="98" fill="currentColor" font-size="12">보통 선행 조건: 첫 번째 = $&lt;, 전부 = $^</text>
  <text x="200" y="116" fill="currentColor" font-size="12" font-family="monospace">src/main.c (+ .d 의 헤더들)</text>
  <text x="470" y="98" fill="currentColor" font-size="12">순서 전용 = $|  (시각 안 봄)</text>
  <text x="470" y="116" fill="currentColor" font-size="12" font-family="monospace">build</text>
  <text x="24" y="138" fill="currentColor" font-size="12">stem % = $* = main</text>
  <rect x="10" y="152" width="720" height="58" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="24" y="174" fill="currentColor" font-family="monospace" font-size="12">레시피: $(CC) $(CFLAGS) $(DEPFLAGS) -c $&lt; -o $@</text>
  <text x="24" y="196" fill="currentColor" font-family="monospace" font-size="12">  → cc -std=c11 ... -Iinclude -MMD -MP -c src/main.c -o build/main.o</text>
  <rect x="10" y="224" width="720" height="66" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="24" y="244" fill="currentColor" font-size="12">부산물 build/main.d (다음 make 가 -include 로 읽음):</text>
  <text x="24" y="263" fill="currentColor" font-family="monospace" font-size="12">build/main.o: src/main.c include/hal.h include/dsp.h include/board.h</text>
  <text x="24" y="281" fill="currentColor" font-family="monospace" font-size="12">include/hal.h:   include/dsp.h:   include/board.h:      ← -MP 가 붙인 빈 규칙</text>
</svg>
```

자동 변수 정리 (GNU Make 매뉴얼 "Automatic Variables"):

| 변수 | 뜻 | 이 레슨에서의 값 (`build/main.o`) |
|---|---|---|
| `$@` | 타깃 파일 이름 | `build/main.o` |
| `$<` | **첫 번째** 보통 선행 조건 | `src/main.c` (패턴 규칙의 것이 맨 앞) |
| `$^` | 보통 선행 조건 전부 (중복 제거) | `src/main.c include/hal.h ...` — 그래서 컴파일 레시피에 `$^` 를 쓰면 헤더까지 컴파일러에 넘어간다 |
| `$+` | `$^` 인데 중복 유지 | 라이브러리를 두 번 적어야 할 때 (TOOL-B09) |
| `$?` | 타깃보다 새로운 선행 조건만 | 증분 `ar` 등 |
| `$\|` | 순서 전용 선행 조건 | `build` |
| `$*` | stem | `main` |
| `$(@D)` / `$(@F)` | `$@` 의 디렉터리 / 파일 부분 | `build` / `main.o` |

링크 규칙 `$(TARGET): $(OBJS)` 의 레시피는 `$^` 가 맞다 (오브젝트 전부). 컴파일 규칙은 `$<` 가 맞다 (`.c` 하나).

목록은 함수로 만든다. `make -p` (데이터베이스 출력) 로 make 가 실제로 본 값을 확인한 결과 (`bash demo_make.sh 3`, 맥 3.81 실측):

```text
build/%.o: src/%.c | build
#  commands to execute (from `Makefile', line 26):
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@
...
build/main.o: src/main.c src/main.c include/hal.h include/dsp.h include/board.h | build
include/hal.h:
DEPFLAGS := -MMD -MP
SRCS := src/crc.c src/filter.c src/log.c src/main.c src/sensor.c src/uart.c
OBJS := build/crc.o build/filter.o build/log.o build/main.o build/sensor.o build/uart.o
```

- `build/main.o` 줄에 `src/main.c` 가 두 번 보이는 이유: 패턴 규칙에서 한 번, `.d` 파일의 명시 규칙에서 한 번. 레시피 없는 명시 규칙은 선행 조건만 **보탠다** — 레시피는 패턴 규칙 것을 그대로 쓴다. 이 "규칙 합치기" 가 `-include $(DEPS)` 가 동작하는 원리다.
- `SRCS` 가 알파벳순인 이유: `$(wildcard)` 의 glob 결과가 정렬돼 나왔다 (3.81, 4.4.1 둘 다 실측 `a.c m.c z.c` — `demo_compat_out.txt`). 3.82 ~ 4.2 사이에 정렬하지 않던 버전이 있었다는 기록이 있으니(확인 권장) 링크 순서가 중요하면 `$(sort ...)` 로 못 박는다.

클린 빌드 실제 출력 (`bash demo_make.sh 1`):

```text
mkdir -p build
cc -std=c11 -Wall -Wextra -Werror -O2 -Iinclude -MMD -MP -c src/crc.c -o build/crc.o
cc -std=c11 -Wall -Wextra -Werror -O2 -Iinclude -MMD -MP -c src/filter.c -o build/filter.o
cc -std=c11 -Wall -Wextra -Werror -O2 -Iinclude -MMD -MP -c src/log.c -o build/log.o
cc -std=c11 -Wall -Wextra -Werror -O2 -Iinclude -MMD -MP -c src/main.c -o build/main.o
cc -std=c11 -Wall -Wextra -Werror -O2 -Iinclude -MMD -MP -c src/sensor.c -o build/sensor.o
cc -std=c11 -Wall -Wextra -Werror -O2 -Iinclude -MMD -MP -c src/uart.c -o build/uart.o
cc  build/crc.o build/filter.o build/log.o build/main.o build/sensor.o build/uart.o -o build/app
...
make: Nothing to be done for `all'.
```

### Part B — 의존성 자동 생성과 세 가지 함정

**`.d` 파일 읽기.** 첫 줄은 "이 오브젝트는 이 파일들에 기댄다", 나머지는 `-MP` 의 빈 규칙이다. 컴파일러가 실제 전처리 중에 연 헤더를 적으므로 `dsp.h → board.h` 같은 **간접 include 가 자동으로** 들어간다 — `main.c` 에는 `board.h` 가 한 글자도 없는데도 `board.h` 가 적혀 있다. 손으로 적는 의존성이 반드시 틀리는 이유다.

| 옵션 | 하는 일 |
|---|---|
| `-M` / `-MM` | 의존성**만** 출력하고 컴파일은 안 함 (`-MM` = 시스템 헤더 제외). 옛날 방식: 별도 `make depend` 단계 |
| `-MD` / `-MMD` | 컴파일하면서 부산물로 `.d` 를 씀 (`-MMD` = 시스템 헤더 제외). 이름은 `-o` 기준 |
| `-MP` | 헤더마다 빈 규칙 추가 |
| `-MF file` / `-MT target` | `.d` 파일 이름 / `.d` 안의 타깃 이름 지정 |

`-MD` 와 `-MMD` 차이, 실측 (`main.c` 하나):

```text
-MD : 59 headers, e.g. /Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk/usr/include/stdio.h
-MMD: 3 headers (first line only)
```

시스템 헤더 59 개를 매번 stat 할 이유는 없다 — SDK/툴체인이 바뀌면 어차피 클린 빌드를 한다. 단 **벤더 SDK 를 `-I` 로 넣는 펌웨어**(STM32 HAL, CMSIS)에서는 그 헤더들도 "사용자 헤더" 로 잡혀 `.d` 에 들어간다 (`-MMD` 가 빼는 건 `-isystem`/시스템 경로 헤더. 이 구분은 TOOL-B02 의 `-isystem` 논의와 같다).

**왜 다시 만드나 — `make -d`.** `touch include/dsp.h` 후 (`bash demo_make.sh 2`):

```text
     Prerequisite `include/dsp.h' is newer than target `build/crc.o'.
    Must remake target `build/crc.o'.
     Prerequisite `include/dsp.h' is newer than target `build/filter.o'.
    Must remake target `build/filter.o'.
     Prerequisite `include/dsp.h' is newer than target `build/main.o'.
    Must remake target `build/main.o'.
   Prerequisite `build/crc.o' is newer than target `build/app'.
   Prerequisite `build/filter.o' is newer than target `build/app'.
   Prerequisite `build/main.o' is newer than target `build/app'.
  Must remake target `build/app'.
Must remake target `all'.
```

들여쓰기 깊이 = DAG 깊이. 4.x 라면 `--trace` 가 이걸 한 줄로 준다 (Part C).

**함정 1 — 디렉터리를 보통 선행 조건으로.** starter 의 `$(BUILD)/main.o: src/main.c $(BUILD)` 는 `build/` 의 mtime 과 비교한다. `build/` 안에 파일이 하나 생기기만 해도 (`touch build/notes.txt`, 혹은 링크가 `build/app` 을 만드는 순간) 전부 재빌드 (`bash demo_make.sh 4`):

```text
--- (b) new file in build/ ; make -n
  cc -std=c11 -Wall -Wextra -Werror -O2 -Iinclude  -c src/main.c -o build/main.o
  ...(6 개 전부)
  cc  build/main.o build/uart.o build/sensor.o build/log.o build/filter.o build/crc.o -o build/app
```

`| $(BUILD)` 로 옮기면 "없을 때만 `mkdir -p`" 가 된다. (레시피마다 `@mkdir -p $(@D)` 를 넣는 방법도 흔하다 — 같은 효과, 대신 매 컴파일마다 프로세스 하나.)

**함정 2 — `-MP` 없이 헤더를 지우면.** `log.c` 가 `version.h` 를 include 하던 시절의 `.d` 가 남은 상태에서, include 줄을 지우고 헤더 파일도 지웠다 (`bash demo_make.sh 5`):

```text
build/log.o: src/log.c include/hal.h include/version.h
make: *** No rule to make target `include/version.h', needed by `build/log.o'.  Stop.
exit=2
--- same with -MP (solution.mk)
build/log.o: src/log.c include/hal.h include/version.h
include/hal.h:
include/version.h:
cc -std=c11 -Wall -Wextra -Werror -O2 -Iinclude -MMD -MP -c src/log.c -o build/log.o
cc  build/crc.o build/filter.o build/log.o build/main.o build/sensor.o build/uart.o -o build/app
exit=0
```

옛 `.d` 는 "log.o 는 version.h 가 필요" 라고 말하는데 version.h 를 만들 규칙이 없다 → 정지. `-MP` 의 빈 규칙 `include/version.h:` 이 있으면 make 는 "레시피 없는 규칙의 타깃이 없다 → 방금 갱신된 것으로 간주" → `log.o` 를 다시 만들고, 새 `.d` 에서는 version.h 가 빠진다. 팀에서 헤더를 rename 할 때마다 모두가 `make clean` 해야 하는 상황을 막는다.

**함정 3 — `.PHONY` 없는 `clean`.**

```text
--- (c) a file named clean; make clean
make: `clean' is up to date.
build
```

선행 조건이 없는 타깃 `clean` 이 파일로 존재하니 "최신" 이다. `.PHONY: all clean` 이면 파일 존재와 무관하게 레시피를 돈다. (덤: phony 타깃에는 암시 규칙 검색도 안 한다.)

**왜 이게 "버그" 인가 — 헤더만 바뀌고 오브젝트 절반만 다시 만들어지면.** starter 로 빌드한 뒤 `board.h` 의 `FILTER_TAPS` 를 4 → 16 으로 바꾸고, 마침 `filter.c` 도 누가 손댔다 (`bash demo_make.sh 6`, 맥 실측):

```text
[app] taps=4 samples=64 crc=0x8C7F
cc -std=c11 -Wall -Wextra -Werror -O2 -Iinclude  -c src/filter.c -o build/filter.o
cc  build/main.o build/uart.o build/sensor.o build/log.o build/filter.o build/crc.o -o build/app
--- run (main.o still thinks mavg_t has 4 taps, filter.o writes 16)
demo_make.sh: line 85: 38681 Bus error: 10           ./build/app
exit=138
0000000100008000 b _g_filt
000000010000801c b _g_out
* thread #1, queue = 'com.apple.main-thread', stop reason = EXC_BAD_ACCESS (code=2, address=0x1c7e48028)
    frame #0: 0x000000010000076c app`mavg_push + 24
->  0x10000076c <+24>: str    w1, [x0, x8, lsl #2]
--- clean rebuild
[app] taps=16 samples=64 crc=0xD16B
```

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 g_filt 주소, 두 오브젝트가 서로 다른 struct 를 본다</text>
  <text x="10" y="52" fill="currentColor" font-size="12">main.o (옛 4 taps)</text>
  <rect x="150" y="36" width="112" height="26" class="f-ok" stroke="currentColor"/>
  <text x="206" y="54" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">g_filt 0x1c</text>
  <rect x="262" y="36" width="468" height="26" class="f-mut" stroke="currentColor"/>
  <text x="496" y="54" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">g_out[128] (main 이 필터 결과를 씀)</text>
  <text x="10" y="102" fill="currentColor" font-size="12">filter.o (새 16 taps)</text>
  <rect x="150" y="86" width="256" height="26" class="f-warn" stroke="currentColor"/>
  <text x="278" y="104" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">buf[16] 0x00–0x3f</text>
  <rect x="406" y="86" width="48" height="26" class="f-acc" stroke="currentColor"/>
  <text x="430" y="104" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">idx</text>
  <rect x="454" y="86" width="48" height="26" class="f-warn" stroke="currentColor"/>
  <text x="478" y="104" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">n,sum</text>
  <path d="M262 30 L262 120" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="266" y="132" fill="currentColor" font-size="12" font-family="monospace">+0x1c</text>
  <path d="M406 30 L406 120" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="410" y="132" fill="currentColor" font-size="12" font-family="monospace">+0x40 = g_out[36]</text>
  <text x="10" y="162" fill="currentColor" font-size="12">① filter.o 는 76 바이트 struct 로 쓰고 읽는다 → g_out 앞부분과 겹침</text>
  <text x="10" y="182" fill="currentColor" font-size="12">② main 이 g_out[36..39] 에 샘플 바이트를 씀 → filter 의 idx 가 엉뚱한 큰 값</text>
  <text x="10" y="202" fill="currentColor" font-size="12">③ buf[idx] = x 저장이 매핑 밖 주소 → EXC_BAD_ACCESS (MCU 라면 HardFault 또는 조용한 RAM 오염)</text>
</svg>
```

계산으로 맞춰 보기: 4 taps 의 `mavg_t` = 16 + 12 = 28 = 0x1c 바이트 → `nm` 에서 `_g_out` 이 정확히 `_g_filt + 0x1c`. 16 taps 는 64 + 12 = 76 바이트, `idx` 는 +0x40. 크래시 주소 `0x1c7e48028 − 0x100008000 = 0xC7E40028`, 4 로 나누면 idx ≈ `0x31F9000A` — 샘플 바이트 조각이 idx 자리에 들어갔다는 해석과 맞는다 (idx 값 자체는 추론). 핵심: **컴파일러도 링커도 경고 0 개**였다. struct 정의(=ABI)가 바뀌었는데 그것을 쓰는 오브젝트 일부만 새로 컴파일됐기 때문이다. 펌웨어에서는 MPU 가 없으면 크래시조차 안 나고 옆 변수를 조용히 덮는다 — "가끔 클린 빌드하면 고쳐지는 버그" 의 정체.

### Part C — -j8, 4.4.1 의 --trace, 같은 Makefile 로 Cortex-M4

**병렬 빌드.** DAG 가 완전하면 `-j` 는 공짜다. 체커가 `make clean; make -j8` 로 6 개 오브젝트 + 같은 출력을 확인한다 (solution 13/13). `| $(BUILD)` 는 디렉터리 규칙을 한 번만 돌게 하므로 `mkdir` 경쟁도 없다. `-j` 에서만 깨지는 빌드는 거의 항상 **DAG 에 빠진 간선**(생성 헤더, 같은 임시 파일)이다 (Q8).

**VM 의 GNU Make 4.4.1 + gcc 15.2** (`bash demo_vm.sh`, 실제 출력 발췌):

```text
### touch include/dsp.h; make --trace CC=gcc
Makefile:26: update target 'build/crc.o' due to: include/dsp.h
gcc -std=c11 -Wall -Wextra -Werror -O2 -Iinclude -MMD -MP -c src/crc.c -o build/crc.o
Makefile:26: update target 'build/filter.o' due to: include/dsp.h
gcc -std=c11 -Wall -Wextra -Werror -O2 -Iinclude -MMD -MP -c src/filter.c -o build/filter.o
Makefile:26: update target 'build/main.o' due to: include/dsp.h
gcc -std=c11 -Wall -Wextra -Werror -O2 -Iinclude -MMD -MP -c src/main.c -o build/main.o
Makefile:22: update target 'build/app' due to: build/crc.o build/filter.o build/main.o
gcc  build/crc.o build/filter.o build/log.o build/main.o build/sensor.o build/uart.o -o build/app
### second make
make: Nothing to be done for 'all'.
```

`--trace` = "어느 줄의 규칙이, 어느 선행 조건 때문에" — `make -d` 수백 줄 대신 이것 하나면 된다. gcc 의 `.d` 형식은 clang 과 같았다 (`build/main.o: src/main.c include/hal.h include/dsp.h include/board.h` + 빈 규칙 3 개).

**같은 Makefile, Cortex-M4 크로스 빌드.** `CC := cc` 처럼 Makefile 안에서 정한 변수도 **명령줄 변수가 이긴다** — 그래서 Makefile 을 고치지 않고 툴체인·플래그·출력 폴더를 바꿀 수 있다:

```text
$ make -j8 BUILD=build-m4 CC=arm-none-eabi-gcc \
    CFLAGS="-std=c11 -Wall -Wextra -Werror -Os -Iinclude $M4" \
    LDFLAGS="$M4 --specs=nano.specs --specs=nosys.specs"        # M4='-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16'
exit=0
arm-none-eabi-gcc -std=c11 -Wall -Wextra -Werror -Os -Iinclude -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -MMD -MP -c src/main.c -o build-m4/main.o
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 --specs=nano.specs --specs=nosys.specs build-m4/crc.o ... -o build-m4/app
warning: _close is not implemented and will always fail
...(_fstat _isatty _lseek _read 도 같은 줄)
warning: _write is not implemented and will always fail
build-m4/app: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV), statically linked, with debug_
   text	   data	    bss	    dec	    hex	filename
   9680	    108	    672	  10460	   28dc	build-m4/app
build
build-m4
```

- 호스트 빌드(`build/`)와 타깃 빌드(`build-m4/`)가 나란히 산다 — out-of-tree 의 진짜 이유. 한 폴더에 섞으면 호스트 `.o` 를 ARM 링커에 넘기는 사고가 난다.
- `_write is not implemented` 경고: nosys.specs 의 스텁이다. 실제 보드에서는 `_write` 를 UART 로 구현한다 (printf 비용과 `text/data/bss` 읽기는 TOOL-B06, 링커 스크립트는 TOOL-M01, CMake 툴체인 파일 방식은 TOOL-M05).
- `CFLAGS` 를 통째로 덮어쓰면 `-Iinclude` 까지 사라진다 — 그래서 실무 Makefile 은 `CPPFLAGS`(전처리) / `CFLAGS`(컴파일) / `ARCH_FLAGS` 를 나누거나 `override CFLAGS += ...` 를 쓴다.

**맥 make 3.81 의 타임스탬프는 초 단위.** 체커가 `sleep` 대신 `touch -t` 로 시각을 고정하는 이유 (`bash demo_make.sh 8`):

```text
in 1794784466.812428000
out 1794784466.801750520
make: `out' is up to date.
rebuilt out
```

`in` 이 `out` 보다 0.01 초 새로운데 3.81 은 "최신" 이라고 한다. VM 의 4.4.1 은 같은 실험에서 다시 만들었다 (나노초 mtime 사용). 체커를 처음 쓸 때 실제로 이것에 걸렸다: 링크 직후 같은 초에 만든 `watchdog.o`, 그리고 같은 초에 다시 쓴 플래그 스탬프가 "새롭지 않다" 로 판정돼 재링크·재컴파일이 빠졌다. 사람이 손으로 빠르게 편집할 땐 드물지만 **스크립트가 파일을 생성하는 빌드**(코드 생성, CI 의 체크아웃 직후 빌드)에서는 실제로 터진다.

**3.81 vs 4.4.1 실측표** (`demo_compat.sh` 를 양쪽에서 실행, `demo_compat_out.txt`):

| 기능 (도입 버전) | 맥 3.81 | VM 4.4.1 |
|---|---|---|
| `.ONESHELL` (3.82) | 무시 — `cd /tmp` 다음 `pwd` 가 원래 폴더 | 한 셸 — `/tmp` |
| `X != echo hi` (4.0) | **조용히 빈 값** `X=[]` | `X=[hi]` |
| `$(file >f,...)` (4.0) | **조용히 아무것도 안 함** (`no f.txt`) | 파일 생성 |
| `a b &: ; recipe` grouped target (4.3) | **레시피가 두 번** 돈다 | 한 번 |
| `--output-sync`, `--trace` (4.0) | `unrecognized option` | 동작 |
| `else ifeq`, order-only `\|` (≤3.81) | 동작 | 동작 |

무서운 건 에러가 아니라 **조용히 다르게 도는 것**들이다(`!=`, `$(file)`, `&:`). 맥 개발자와 Linux CI 가 같은 Makefile 을 쓰면 3.81 기준으로 쓰거나, `brew install make` 의 `gmake` 를 쓰도록 명시한다(확인 권장: Homebrew 는 `gmake` 이름으로 설치).

**재귀 make (짧게).** 서브디렉터리마다 Makefile 을 두고 `$(MAKE) -C drivers` 로 부르는 방식은 각 make 가 **DAG 의 일부만** 본다 — 디렉터리 간 의존성이 끊겨 순서를 손으로 맞추게 되고 `-j` 병렬성도 디렉터리 단위로 갇힌다(Peter Miller, "Recursive Make Considered Harmful", 1997). 이 레슨처럼 한 make 가 전체를 보는 non-recursive 방식(또는 `include drivers/module.mk`)이 기본값으로 낫다. Linux kbuild 는 재귀 구조지만 의존성·명령줄 추적을 아주 꼼꼼히 해서 쓴다는 점이 다르다.

**`CFLAGS` 변경은 추적되지 않는다.** solution 으로 빌드한 뒤 `make CFLAGS="-std=c11 -O0 -g -Iinclude -DTRACE"` → 실측 `make: Nothing to be done for `all'.` — make 는 파일 시각만 보지 명령줄은 안 본다. `-DTRACE` 를 붙였다고 믿고 디버깅하면 지옥이다. 해결은 9. 추가 연습 P3 (플래그 스탬프). CMake+Ninja, kbuild(`.cmd` 파일)는 명령줄 자체를 기록해 비교한다(확인 권장).

## 6. 정답 코드

<!-- code: solution.mk diff=starter.mk -->

`bash check.sh solution` 실제 출력:

```text
== solution: GNU Make 3.81 ==
PASS full build, app prints '[app] taps=4 samples=64 crc=0x8C7F'
PASS second make -> {} relink=0
PASS make -q all: up to date (exit 0)
PASS touch include/board.h (via dsp.h too) -> {crc filter main sensor uart} relink=1
PASS touch include/hal.h -> {log main sensor uart} relink=1
PASS touch include/dsp.h -> {crc filter main} relink=1
PASS touch src/crc.c -> {crc} relink=1
PASS new file inside build/ (dir mtime bumped) -> {} relink=0
PASS deleted header (no longer included): build continues, log.o rebuilt
PASS new src/watchdog.c compiled + linked automatically
PASS make clean: build/ gone, 9 sources kept
PASS make -j8 from clean: 6 objects, same output
PASS file named 'clean' exists, make clean still runs
13/13 passed
```

TODO 별로 고친 이유:

| TODO | 고침 | 막는 고장 (체커 항목) |
|---|---|---|
| (1) | `DEPFLAGS := -MMD -MP` | 헤더 touch 3 건, 헤더 삭제 |
| (2) | `SRCS := $(wildcard src/*.c)` | 새 `watchdog.c` |
| (3) | `.PHONY: all clean` | `clean` 파일 |
| (4) | 패턴 규칙 + `$<` `$@` + `\| $(BUILD)` | 6 중복 제거, `build/` 안 새 파일 |
| (5) | `-include $(DEPS)` | `.d` 를 만들어도 안 읽으면 헤더 touch 가 여전히 FAIL |

## 7. 자주 하는 실수

**1. `.d` 경로를 소스 기준으로 잡기.** `DEPS := $(SRCS:.c=.d)` → `src/main.d` 를 찾는데 `-MMD` 는 `build/main.d` 에 썼다. `-include` 는 없는 파일을 조용히 넘어가므로 **헤더 의존성이 통째로 사라져도 에러가 없다**. 실측: 이렇게 바꾸고 `touch include/dsp.h` → `make -n` 출력 없음. 고침: `DEPS := $(OBJS:.o=.d)`.

**2. `-include` 대신 `include`.** 첫 빌드에서 (맥 3.81 실측)

```text
Makefile:35: build/crc.d: No such file or directory
...
make: *** No rule to make target `build/uart.d'.  Stop.
```

VM 4.4.1 도 `No rule to make target 'build/crc.d'.  Stop.` 로 같다. `-include` (= `sinclude`) 로.

**3. 디렉터리를 보통 선행 조건으로.** 위 함정 1 — `build/` 안에 파일 하나만 생겨도 전부 재빌드. `| $(BUILD)` 로.

**4. `-MMD` 만 쓰고 `-MP` 빼기.** 헤더 rename/삭제 때 `No rule to make target 'include/x.h'`. 팀원 모두가 `make clean` 을 배우게 된다.

**5. 컴파일 레시피에 `$^`.** `.d` 의 헤더까지 `$^` 에 들어간다. 실측 (solution 의 `$<` 를 `$^` 로 바꾸고 재빌드):

```text
cc -std=c11 -Wall -Wextra -Werror -O2 -Iinclude -MMD -MP -c src/crc.c include/dsp.h include/board.h -o build/crc.o
clang: error: cannot specify -o when generating multiple output files
make: *** [build/crc.o] Error 1
```

첫 빌드(`.d` 가 아직 없음)에서는 멀쩡하다가 두 번째부터 깨지는 게 더 헷갈리는 점. 컴파일은 `$<`, 링크는 `$^`.

**6. 레시피 들여쓰기를 스페이스로.** 실측: `Makefile:26: *** missing separator (did you mean TAB instead of 8 spaces?).  Stop.` — 레시피는 반드시 탭(4.x 의 `.RECIPEPREFIX` 로 바꿀 수 있지만 3.81 에는 없다).

**7. 소스 파일을 지우고 "make 했으니 반영됐겠지".** make 는 **목록 변화**를 모른다 — 남은 오브젝트가 전부 `app` 보다 오래됐으면 재링크하지 않는다. 실측 (`bash demo_make.sh 7`):

```text
000000010000085c T _watchdog_kick
make: Nothing to be done for `all'.
000000010000085c T _watchdog_kick
```

`watchdog.c` 를 지웠는데도 `app` 에 `_watchdog_kick` 이 남아 있다. `make -B` 후에는 0 개. 근본 해결은 오브젝트 목록을 스탬프로 남겨 링크가 그것에 의존하게 하는 것(9. P3 와 같은 기법).

**8. 헤더 의존성 없는 빌드를 "느리지만 맞는 빌드" 로 착각.** Part B 크래시 — 맞지 않는 빌드다.

## 8. 면접 Q&A

<details>
<summary>Q1. make 는 무엇을 보고 다시 빌드할지 정하나요? 내용이 같아도 다시 빌드하나요?</summary>

DAG 를 아래에서 위로 훑으며, 타깃이 없거나 선행 조건 중 하나라도 타깃보다 **mtime 이 크면** 레시피를 돈다. 내용은 보지 않는다 — `touch` 만 해도 다시 만들고, 반대로 내용이 바뀌었는데 시각이 같으면(맥 3.81 은 초 단위라 같은 초) 놓친다. 내용 해시로 판단하는 건 ccache(컴파일 결과 캐시)나 Bazel 같은 다른 도구의 영역이다. 꼬리: "시계가 틀린 NFS / 압축 해제한 소스에서는?" → 미래 시각 파일이면 매번 재빌드, make 가 `Clock skew detected` 경고를 낸다(확인 권장).

</details>

<details>
<summary>Q2. -MMD -MP 를 각각 설명하고, 옛날 방식(make depend)과 비교하세요.</summary>

`-MMD`: 컴파일하는 김에 `-o` 옆에 `.d` 를 쓴다(시스템 헤더 제외; `-MD` 는 포함 — 실측 3 개 vs 59 개). `-MP`: 헤더마다 빈 규칙을 추가해 헤더가 사라져도 `No rule to make target` 이 안 나게 한다. 옛 방식은 `-MM` 으로 의존성만 뽑는 `make depend` 단계를 따로 두었는데, 사람이 잊으면 바로 틀린다. `-MMD` 방식은 의존성이 바뀔 수 있는 유일한 순간(= 그 파일을 다시 컴파일할 때)에 자동 갱신된다 — 의존성이 바뀌려면 `.c` 나 그 헤더가 바뀌어야 하고, 그러면 어차피 다시 컴파일되니까.

</details>

<details>
<summary>Q3. 헤더 의존성이 빠지면 왜 "느린 빌드" 가 아니라 "틀린 빌드" 인가요?</summary>

헤더는 **오브젝트 사이의 계약(ABI)**이다: struct 레이아웃, enum 값, 매크로 상수, 인라인 함수. 헤더가 바뀌었는데 일부 오브젝트만 다시 컴파일되면 같은 심볼을 서로 다른 레이아웃으로 본다. 링커는 이름만 맞추므로 경고가 없다. 이 레슨 실측: `FILTER_TAPS` 4 → 16 후 `filter.o` 만 새로 → `mavg_t` 28 B vs 76 B → 이웃 전역 `g_out` 과 겹쳐 `EXC_BAD_ACCESS`. MCU 라면 HardFault 이거나 더 나쁘게 조용한 RAM 오염. 레지스터 맵 헤더(`#define UART_CR1_OFFSET`)가 바뀐 경우도 같다.

</details>

<details>
<summary>Q4. order-only prerequisite 는 무엇이고, 왜 출력 디렉터리에 쓰나요?</summary>

`target: normal | order-only` — `|` 뒤의 것은 "없으면 먼저 만든다" 만 보장하고 시각은 비교하지 않는다. 디렉터리 mtime 은 안에 파일이 생길 때마다 바뀌므로 보통 선행 조건이면 매번 재빌드가 된다(starter 실측: `build/` 에 파일 하나 → 6 개 전부 + 재링크). GNU Make 3.80 부터 있는 기능이라 3.81 에서도 된다.

</details>

<details>
<summary>Q5. .PHONY 는 왜 필요한가요? 안 쓰면 어떤 일이?</summary>

`clean`, `all`, `test`, `flash` 같은 "동작 이름" 이 파일 이름과 부딪히지 않게 한다. 선행 조건이 없는 `clean` 은 같은 이름의 파일이 있으면 영원히 최신 → 실측 `` make: `clean' is up to date.``. 부수 효과로 phony 타깃은 암시 규칙 검색을 건너뛴다. 꼬리: "phony 타깃을 실제 파일의 선행 조건으로 두면?" → 그 파일이 매번 다시 만들어진다 (phony 는 항상 out of date). 일부러 그렇게 쓰는 게 `FORCE` 관용구다.

</details>

<details>
<summary>Q6. CFLAGS 를 바꿨는데 다시 빌드가 안 됩니다. 어떻게 고치나요?</summary>

make 는 명령줄을 기록하지 않는다(실측: `-DTRACE` 를 붙여도 `Nothing to be done`). 방법: (a) 플래그 문자열을 스탬프 파일에 써 두고 **바뀔 때만** 다시 써서, 모든 오브젝트가 그 스탬프에 의존하게 한다(practice.mk P3 — 파싱 시점 `$(shell)` 로 비교). (b) 구성마다 빌드 폴더를 나눈다(`build/debug`, `build/release` — P1). (c) Ninja/CMake 나 kbuild 처럼 명령줄을 기록·비교하는 도구. 함정: `FORCE` 로 스탬프 규칙을 매번 돌리면 `make -n` 이 항상 "전부 재빌드" 로 보인다(-n 은 레시피를 안 돌리고 "갱신됐다고 가정").

</details>

<details>
<summary>Q7. make -j 에서만 가끔 깨지는 빌드, 원인과 디버깅은?</summary>

원인은 거의 항상 **DAG 에 빠진 간선**: (1) 생성 헤더(`version.h`, 레지스터 맵 생성)에 대한 의존이 `.d` 에 아직 없는 첫 빌드 — 생성 헤더는 `.d` 가 아니라 Makefile 에 명시 선행 조건(또는 order-only)으로 걸어야 한다. (2) 두 레시피가 같은 임시 파일을 쓴다. (3) 재귀 make 에서 디렉터리 간 순서를 가정. 디버깅: `make -j1` 로 재현 안 되면 순서 문제 확정, `--trace`(4.x) 로 누가 왜 먼저 돌았는지, `--output-sync=target` 으로 로그를 타깃별로 묶기, `make -p` 로 실제 선행 조건 확인. 맥 3.81 에는 `--trace`/`--output-sync` 가 없어 `-d` 를 grep 한다.

</details>

<details>
<summary>Q8. 재귀 make 와 non-recursive make 의 차이는? 펌웨어 프로젝트라면?</summary>

재귀: 디렉터리마다 `$(MAKE) -C dir` — 각 make 가 부분 DAG 만 보므로 디렉터리 간 의존(예: `drivers/` 헤더가 바뀌면 `app/` 재빌드)을 놓치기 쉽고, 병렬성이 디렉터리 경계에서 끊긴다(Miller 1997). non-recursive: 최상위 make 하나가 `include drivers/module.mk` 처럼 조각을 모아 **전체 DAG** 를 본다 — 정확하고 `-j` 가 잘 먹는다. 펌웨어(보드 몇 개 × 드라이버 수십 개)라면 non-recursive 또는 CMake(TOOL-M04/M05)를 권한다. Linux kbuild 는 재귀지만 명령줄·의존성 추적을 꼼꼼히 구현해 단점을 막은 예외적인 경우.

</details>

<details>
<summary>Q9. 같은 Makefile 로 호스트 단위 테스트와 Cortex-M 타깃을 둘 다 빌드하려면?</summary>

툴체인/플래그를 변수로 두고(`CC`, `CFLAGS`, `LDFLAGS`), 빌드 폴더를 타깃마다 분리한다(`BUILD=build-m4`). 명령줄 변수는 Makefile 의 `:=` 를 이긴다 — 실측으로 `CC=arm-none-eabi-gcc BUILD=build-m4 ...` 하나로 ELF 32-bit ARM 이미지(`text 9680 data 108 bss 672`)가 나왔다. 하드웨어 무관 코드(filter, crc)는 호스트 테스트 바이너리와 오브젝트를 공유한다(P2). 실무에서는 `-include config/$(BOARD).mk` 로 보드별 변수를 분리하거나 CMake 툴체인 파일을 쓴다(TOOL-M05).

</details>

## 9. 추가 연습

`practice.mk` = `solution.mk` + 아래 P1–P4. 체커는 기본 13 항목(빌드 폴더만 `build/debug`)에 연습 10 항목을 더 확인한다.

**P1. debug / release 빌드 폴더.** `make CONFIG=release` 는 `build/release/` 에 `-O2 -DNDEBUG`, 기본 `CONFIG=debug` 는 `build/debug/` 에 `-O0 -g`. 두 구성이 서로를 무효화하지 않아야 하고, 오타(`CONFIG=bogus`)는 즉시 에러.

<details>
<summary>P1 답</summary>

`BUILD := build/$(CONFIG)` 하나로 모든 산출물 경로가 갈린다. `ifeq / else ifeq / else $(error ...)` 로 구성별 `OPTFLAGS` (3.81 에서 `else ifeq` 동작 실측). 오타 실측: `Makefile:16: *** CONFIG must be debug or release, got 'bogus'.  Stop.` 체커: release 빌드 후 debug 가 여전히 최신, `touch dsp.h` 후 각 구성이 자기 `{crc filter main}` 만.

</details>

**P2. 단위 테스트 바이너리.** `make test` 는 `tests/test_dsp.c` + 앱과 **같은** `filter.o`, `crc.o` 로 `test_dsp` 를 링크해 실행한다. `hal.h` 를 touch 해도 테스트 쪽은 아무것도 컴파일하지 않아야 한다.

<details>
<summary>P2 답</summary>

`TEST_OBJS := $(BUILD)/tests/test_dsp.o $(BUILD)/filter.o $(BUILD)/crc.o` 와 `tests/` 용 패턴 규칙 하나, `$(BUILD)/tests` 디렉터리도 order-only. `test` 는 phony (매번 실행). 실측 링크 줄 `cc  build/debug/tests/test_dsp.o build/debug/filter.o build/debug/crc.o -o build/debug/test_dsp`, 결과 `test_dsp: 3/3 passed` (CRC-16/CCITT-FALSE 검사값 `0x29B1` 포함). `touch dsp.h` → `make -n test` 는 `{crc filter test_dsp}` 만 — `main.o` 는 테스트와 무관하니 안 나온다.

</details>

**P3. CFLAGS 가 바뀌면 전부 다시.** `make EXTRA_CFLAGS=-DTRACE` → 6 개 전부 재컴파일, 같은 명령 다시 → 아무것도, 기본으로 돌아가면 → 다시 전부.

<details>
<summary>P3 답</summary>

파싱 시점에 `$(shell cat stamp)` 와 현재 `$(strip $(CFLAGS))` 를 비교해 **다를 때만** 스탬프를 다시 쓰고, `$(OBJS): $(FLAGS_STAMP)`. `FORCE` 규칙 방식과 달리 `make -n` 이 정확하다. 대가 두 가지(실측): ① `make -n EXTRA_CFLAGS=-DX` 만 해도 스탬프가 `... -DX` 로 바뀐다 (파싱 시점 부작용). ② 3.81 초 단위 시각 — 직전 빌드와 같은 초에 스탬프를 다시 쓰면 "새롭지 않다" 로 놓친다 (체커는 `settle` 로 회피; 실제로 처음엔 이 때문에 `uart` 하나가 빠진 FAIL 을 봤다). `clean` / `print-%` 목표에서는 스탬프를 건드리지 않게 `$(filter ...,$(MAKECMDGOALS))` 로 막았다.

</details>

**P4. Makefile 디버깅 도우미.** `make print-CFLAGS` 가 변수의 최종 값을 찍게 하라.

<details>
<summary>P4 답</summary>

`print-%: ; @echo '$* = $($*)'` — stem 으로 변수 이름을 받는 패턴 규칙. 실측 `CFLAGS = -std=c11 -Wall -Wextra -Werror -O0 -g -Iinclude `. 4.x 라면 `$(info ...)` 나 `make -p` 도 있지만 이 한 줄이 제일 빠르다. 체커 대상은 아님.

</details>

**P5 (생각만). 지운 소스가 남는 문제(7. 실수 7) 막기.** 힌트: P3 와 같은 스탬프를 `$(OBJS)` 목록으로 만들고 링크 규칙이 그것에 의존. 이 레슨의 체커에는 넣지 않았다.

### 한 번에 검증

<!-- code: practice.mk -->

```bash
bash check.sh practice
```

실제 출력 (기본 13 항목은 위 solution 과 같아 생략):

```text
== practice: GNU Make 3.81 ==
...
PASS file named 'clean' exists, make clean still runs
-- practice --
PASS P1 CONFIG=release -> build/release (-O2 -DNDEBUG)
PASS P1 debug still up to date after release build -> {} relink=0
PASS P1 release up to date -> {} relink=0
PASS P1 touch dsp.h: each config rebuilds its own {crc filter main}
PASS P2 make test: 3/3 passed, links shared build/debug/filter.o build/debug/crc.o
PASS P2 touch hal.h: test target compiles nothing -> {} relink=0
PASS P2 touch dsp.h: test target -> {crc filter test_dsp} relink=0
PASS P3 EXTRA_CFLAGS=-DTRACE -> {crc filter log main sensor uart} relink=1
PASS P3 same flags again -> {} relink=0
PASS P3 flags back to default -> {crc filter log main sensor uart} relink=1
23/23 passed
```

## 10. 핵심 정리

- make = DAG + mtime 비교. **모르는 의존성은 없는 의존성**이다. 내용도 명령줄도 안 본다.
- 오브젝트 규칙은 하나: `$(BUILD)/%.o: src/%.c | $(BUILD)` + `$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@`. 컴파일은 `$<`, 링크는 `$^`.
- `-MMD -MP` + `-include $(OBJS:.o=.d)`: 컴파일러가 간접 include 까지 적고, `-MP` 가 헤더 삭제를 견디게 한다.
- 디렉터리는 order-only, 동작 이름은 `.PHONY`, 목록은 `$(wildcard)` + 치환 참조.
- 증명은 `touch 헤더 → make -n` (+ `make -q`, `make -d`/`--trace`). 헤더 의존성 누락 = ABI 불일치 = 경고 없는 크래시.
- 맥 `/usr/bin/make` 는 3.81: 초 단위 mtime, `--trace`/`--output-sync`/`!=`/`$(file)`/`&:` 없음(일부는 조용히 오동작).
- 같은 Makefile 로 `CC=arm-none-eabi-gcc BUILD=build-m4` 크로스 빌드 — 출력 폴더 분리가 핵심. CFLAGS 변경 추적은 스탬프나 구성별 폴더로.

## 참고자료

- GNU Make Manual — Pattern Rules, Automatic Variables, Types of Prerequisites (order-only), Phony Targets, Include: https://www.gnu.org/software/make/manual/make.html
- GCC Preprocessor Options (`-M -MM -MD -MMD -MP -MF -MT`): https://gcc.gnu.org/onlinedocs/gcc/Preprocessor-Options.html
- Clang command line reference — dependency file generation: https://clang.llvm.org/docs/ClangCommandLineReference.html
- Paul D. Smith, "Auto-Dependency Generation" (make 관리자): https://make.mad-scientist.net/papers/advanced-auto-dependency-generation/
- Peter Miller, "Recursive Make Considered Harmful" (1997)
- GNU Make NEWS (버전별 기능: 3.82 `.ONESHELL`, 4.0 `--trace`/`--output-sync`/`!=`/`$(file)`, 4.3 grouped targets): https://git.savannah.gnu.org/cgit/make.git/tree/NEWS
- 관련 레슨: TOOL-B01 (컴파일 단계), TOOL-B02 (`-isystem`), TOOL-B06 (size), TOOL-B09 (정적 라이브러리·링크 순서), TOOL-M01 (링커 스크립트), TOOL-M04/M05 (CMake), BM-B08 (Cortex-M 부팅)
