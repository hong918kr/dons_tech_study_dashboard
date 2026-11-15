---
id: TOOL-B13
time: 60분
updated: 2026-11-15
---
# TOOL-B13 컴파일 타임 제거 가능한 로그 매크로

## 학습 목표

- `LOG_E/W/I/D(...)` 를 `LOG_LEVEL` 하나로 거르고, 꺼진 레벨은 **코드 · 포맷 문자열 · 인자 평가**까지 바이너리에서 사라지게 만든다 — 그러면서도 **printf 포맷 검사와 변수 사용은 남게**(`do { if (0) ... } while (0)`) 만든다.
- `__attribute__((format(printf, 5, 6)))` 의 숫자를 읽고, 속성이 붙는 위치(함수 vs 함수 포인터 typedef — clang 은 typedef 를 무시함, 실측)와 `uint32_t` + `%u` 가 arm-none-eabi-gcc 에서만 경고가 나는 이유를 설명한다.
- `__FILE__` · `__FILE_NAME__` · `-fmacro-prefix-map` · `__func__` 가 각각 문자열 테이블에 무엇을 남기는지 `llvm-size -A` · `objdump -s` · `readelf -p` 로 확인한다.
- `LOG_LEVEL` 0..4 별 `.text` / `.rodata.str1.1` 크기 표를 Cortex-M4(clang ELF, arm-none-eabi-gcc)와 Mac(Mach-O)에서 직접 만들고, 런타임 필터만 쓰는 방식과 비교한다.
- UART 링 버퍼 싱크(꽉 차면 줄 단위로 버리고 세기), 런타임 레벨은 컴파일 타임 상한 아래에서만 의미가 있다는 것, 토큰화(지연) 로그의 아이디어를 말할 수 있다.

## 1. 문제

펌웨어에서 로그는 공짜가 아니다 — **로그 한 줄 = Flash 의 포맷 문자열 + 호출 코드 수십 바이트 + 실행 시간**이고, 릴리스 빌드에서 디버그 로그가 남아 있으면 Flash 가 넘치거나 ISR 타이밍이 깨진다. 그래서 면접에서 "릴리스에서 디버그 로그를 완전히 없애는 매크로를 짜 보세요", "`((void)0)` 이랑 뭐가 달라요?", "이 로그가 Flash 를 얼마나 먹죠?" 를 묻는다. SSD 컨트롤러 펌웨어의 UART 디버그 로그, Cortex-M 의 `printf` 리타깃, Zephyr `LOG_DBG`, Linux `pr_debug` 가 모두 같은 문제를 푼다.

목표는 세 가지를 **동시에** 만족하는 매크로다.

| 요구 | 나쁜 예 | 이 레슨의 방법 |
|---|---|---|
| 꺼진 레벨은 바이너리에서 0 바이트 | 런타임 `if (lvl <= g_level)` 만 → 문자열·코드 그대로 남음 | `if (상수 0 && ...)` → 컴파일러가 통째로 버림 |
| 꺼진 레벨도 포맷 검사·변수 사용 유지 | `#define LOG_D(...) ((void)0)` → 경고·검사가 사라짐 | 호출 식은 그대로 두고 `if (0)` 로 막음 |
| 인자는 켜졌을 때 정확히 한 번만 평가 | 함수형 매크로에서 인자 두 번 사용 | 인자는 `log_emit(...)` 호출에만 한 번 |

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">LOG_D 한 줄이 .o 로 가는 길 (LOG_LEVEL=3, 실제 cc -E 출력 요약)</text>
  <rect x="10" y="34" width="230" height="44" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="20" y="54" fill="currentColor" font-family="monospace" font-size="12">LOG_D("erase sector %u",</text>
  <text x="20" y="70" fill="currentColor" font-family="monospace" font-size="12">      sector);</text>
  <path d="M240 56 L270 56" stroke="currentColor" stroke-width="2"/>
  <text x="246" y="48" fill="currentColor" font-size="12">-E</text>
  <rect x="272" y="34" width="458" height="44" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="282" y="54" fill="currentColor" font-family="monospace" font-size="12">do { if (((LOG_LVL_DBG) &lt;= LOG_LVL_INF) &amp;&amp; ...)</text>
  <text x="282" y="70" fill="currentColor" font-family="monospace" font-size="12">  log_emit((LOG_LVL_DBG), "solution.c", 134, …); } while (0);</text>
  <path d="M370 78 L370 104" stroke="currentColor" stroke-width="2"/>
  <text x="380" y="96" fill="currentColor" font-size="12">컴파일러 앞단: 4 &lt;= 3 는 상수 거짓</text>
  <rect x="10" y="108" width="350" height="110" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="20" y="128" fill="currentColor" font-weight="bold">그래도 하는 일 (파싱·의미 검사)</text>
  <text x="20" y="150" fill="currentColor" font-size="12">• log_emit 의 format(printf,5,6) 로 포맷 검사</text>
  <text x="20" y="170" fill="currentColor" font-size="12">• sector 같은 변수는 "사용됨" → unused 경고 없음</text>
  <text x="20" y="190" fill="currentColor" font-size="12">• 오타·없는 변수는 빌드 에러 (모든 레벨에서)</text>
  <text x="20" y="208" fill="currentColor" font-size="12">• 문법이 맞는지 항상 확인됨</text>
  <rect x="380" y="108" width="350" height="110" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="390" y="128" fill="currentColor" font-weight="bold">하지 않는 일 (코드 생성)</text>
  <text x="390" y="150" fill="currentColor" font-size="12">• 호출 코드 0 바이트 (-O0 에서도, 실측)</text>
  <text x="390" y="170" fill="currentColor" font-size="12">• "erase sector %u" 문자열 0 바이트</text>
  <text x="390" y="190" fill="currentColor" font-size="12">• 인자 sector, n++, f() 평가 안 함</text>
  <text x="390" y="208" fill="currentColor" font-size="12">• 런타임 레벨 변수도 읽지 않음 (&amp;&amp; 단락)</text>
  <text x="10" y="246" fill="currentColor">((void)0) 로 지우면 오른쪽은 같지만 왼쪽이 전부 사라진다 → 릴리스에서만 깨지는 빌드,</text>
  <text x="10" y="266" fill="currentColor">로그에서만 쓰던 변수의 unused 경고(-Werror 면 실패). 생성 코드는 같다 (5. 풀이 A-3).</text>
  <text x="10" y="290" fill="currentColor" font-size="12">134 = __LINE__, "solution.c" = __FILE_NAME__ (clang · GCC 12+ 의 컴파일 타임 basename)</text>
</svg>
```

### Part A — 레벨 게이트와 포맷 검사

- `LOG_PRINTF_(fmt_idx, first_arg)` 를 `__attribute__((format(printf, …)))` 로 채워 `log_emit` 에 포맷 검사를 붙인다 (GCC/clang 만; 그 외 컴파일러는 빈 매크로).
- `LOG_ENABLED(lvl)` — 컴파일 타임 상수 식. `LOG_AT_(lvl, ...)` — `LOG_ENABLED(lvl) && lvl <= g_log_runtime_level` 일 때만 `log_emit` 을 부르는 `do { if (...) ...; } while (0)`.
- 체크: `LOG_I("x=%d", 5)` 의 출력이 `"[I] solution.c:<줄> test_part_a: x=5\n"` 와 **정확히** 같다. `LOG_D("n=%d", n++)` 뒤 `n` 불변, 출력 0줄. `LOG_I("n=%d", n++)` 뒤 `n` 은 정확히 +1.
- 실측: `demo_size.sh` 로 `LOG_LEVEL` 0..4 의 `.text` / 문자열 크기 표 (Cortex-M4 clang, arm-none-eabi-gcc, Mac).

### Part B — 접두사, 잘림, 모서리

- `log_basename("src/drv/uart.c") == "uart.c"`, Windows 경로 `"C:\\fw\\src\\spi.c"` → `"spi.c"`, `"build/"` → `""`. (`__FILE_NAME__` 이 없는 컴파일러용 대체 경로)
- `log_emit`: `"[T] file:line func: "` + 본문 + `"\n"` 을 `LOG_LINE_MAX`(64) 안에 만든다. 넘치면 63 바이트(마지막은 `'\n'`)로 자르고 `g_log_truncated++`. 딱 맞으면(본문 62자) 자르지 않는다.
- 인자 없는 `LOG_E("boom")`, `"100%%"`, `if (..) LOG_W(..); else ...` (매달린 else), 꺼진 레벨의 함수 호출 인자(`side_effect()` 0번 호출), 싱크가 NULL 이면 조용히 무시.

### Part C — 펌웨어 현실: 런타임 레벨 + UART 링 싱크

- `g_log_runtime_level = WRN` 이면 INF 는 출력도 인자 평가도 없다. 런타임으로 DBG 를 켜도 **컴파일 타임 상한(INF)을 넘을 수 없다** (DBG 코드가 이미 없으므로).
- `uart_ring_sink` — 256 B TX 링(ISR 이 비운다고 가정). 한 줄이 **통째로** 들어갈 자리가 없으면 버리고 `dropped++` (절대 기다리지 않고, 반쪽 줄도 넣지 않는다).
- 고치지 않는 드라이버 `flash_erase()` 를 돌려 링 내용이 W → I → E 순서로 정확히 나오는지, 6줄을 연달아 넣으면 들어간 줄 수 + 버린 줄 수 = 6 인지 확인.

## 2. Problem (English)

> **Compile-time removable logging.**
>
> Implement `LOG_E/LOG_W/LOG_I/LOG_D(fmt, ...)` for a Cortex-M firmware code base:
>
> - A compile-time threshold `LOG_LEVEL` (0 = none … 4 = debug). Calls above the threshold must produce **no code, no format string in the image, and no evaluation of their arguments** — but must still be **type-checked** (`printf` format checking via `__attribute__((format(printf, ...)))`) so a release build cannot hide a broken debug log.
> - Each emitted line is `"[E|W|I|D] <basename>:<line> <function>: <message>\n"`, built in a 64-byte stack buffer; longer lines are truncated to 63 bytes ending in `'\n'` and counted.
> - Output goes through a sink function pointer `void (*)(int lvl, const char *line, size_t len)` (UART ring, RTT, or a test buffer). A runtime level can further silence messages below the compile-time ceiling.
> - The macros must behave as a single statement (`if (x) LOG_W("a"); else ...` must work) and must not require GNU `##__VA_ARGS__`.
>
> Example (LOG_LEVEL = INFO): `LOG_I("x=%d", 5);` → `[I] main.c:42 test_part_a: x=5`; `LOG_D("%d", n++);` → nothing, `n` unchanged; `LOG_I("%s", 42);` → compile-time warning, even when INFO is disabled.
>
> Follow-ups: measure `.text` and string-table size per `LOG_LEVEL`; why not `#define LOG_D(...) ((void)0)`; why `%u` with `uint32_t` warns only with arm-none-eabi-gcc; can you log from an ISR; what is tokenized/dictionary logging.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력). 시작 코드의 게이트는 "무조건 `log_emit` 호출", `log_emit` 은 빈 함수라서 아무것도 안 찍힌다:

```text
FAIL starter.c:193  strcmp(cap, want) == 0
FAIL starter.c:194  cap_lines == 1 && cap_last_lvl == LOG_LVL_INF
FAIL starter.c:201  strcmp(cap, want) == 0
FAIL starter.c:207  n == 10 && cap_lines == 0
FAIL starter.c:210  n == 11 && cap_lines == 1 && strstr(cap, ": n=10\n") != NULL
FAIL starter.c:212  LOG_ENABLED(LOG_LVL_INF) && !LOG_ENABLED(LOG_LVL_DBG)
FAIL starter.c:220  strcmp(log_basename("src/drv/uart.c"), "uart.c") == 0
FAIL starter.c:222  strcmp(log_basename("C:\\fw\\src\\spi.c"), "spi.c") == 0
FAIL starter.c:223  strcmp(log_basename("build/"), "") == 0
FAIL starter.c:231  strcmp(cap, want) == 0
FAIL starter.c:241  k == 1 && cap_lines == 1
FAIL starter.c:246  g_calls == 0
FAIL starter.c:248  g_calls == 1
FAIL starter.c:254  cap_len == LOG_LINE_MAX - 1 && cap[cap_len - 1] == '\n' && g_log_truncated == 0
FAIL starter.c:256  cap_len == LOG_LINE_MAX - 1 && cap[cap_len - 1] == '\n' && g_log_truncated == 1
FAIL starter.c:258  cap_len == LOG_LINE_MAX - 1 && cap[cap_len - 2] == 'x' && g_log_truncated == 2
FAIL starter.c:264  cap_lines == 1
FAIL starter.c:276  n == 1 && cap_lines == 1 && cap_last_lvl == LOG_LVL_WRN
FAIL starter.c:280  n == 1 && cap_lines == 1
FAIL starter.c:291  got > 0 && strcmp(norm, want) == 0
FAIL starter.c:304  ok == 6 && got > 0 && out[got - 1] == '\n' && got == lines * line_len
FAIL starter.c:305  line_len > 0 && lines == RING_SIZE / line_len && g_uart.dropped == 6u - lines
5/27 passed
```

TODO 는 6곳: `LOG_PRINTF_`, `LOG_ENABLED`, `LOG_AT_` 의 조건, `log_basename`, `log_emit`, `uart_ring_sink`. 하네스 설계:

- **캡처 싱크** `cap_sink` 가 받은 줄을 버퍼에 이어 붙인다 → 출력 문자열을 바이트 단위로 비교. 줄 번호는 같은 줄의 `__LINE__` 으로 기대값을 만들고, Part C 는 `strip_line_numbers` 로 `:N` 으로 바꿔 비교(레이아웃이 바뀌어도 안 깨짐).
- `LOG_LEVEL` 은 파일 안에서 INF 로 고정 (`-DLOG_LEVEL=` 로 바꾸면 테스트 기대값이 달라지므로 체크용 빌드에선 건드리지 않는다).

관찰용 데모 (이 디렉터리, 체크와 별개 — 실제 출력은 `demo_size.txt`, `demo_fmt.txt`):

| 파일 | 하는 일 | 실행 |
|---|---|---|
| `demo_fw.c` | UART·Flash·센서 모듈에 로그 13개 (E 3 · W 3 · I 3 · D 4). 스타일 `IF0`/`BOTH`/`VOID0`/`RUNTIME` | `demo_size.sh` 가 컴파일 |
| `demo_size.sh` | 레벨·스타일·파일이름 방식별 `.text`/문자열 크기, -O0, Mac Mach-O, VM arm-none-eabi-gcc, 링크 크기 | `bash demo_size.sh` (~3 초) |
| `demo_fmt.c` | 포맷 함정 3종: 함수 포인터 속성 위치 / `uint32_t` + `%u` / 비리터럴 포맷 | `demo_fmt.sh` 가 컴파일 |
| `demo_variadic.c` | 인자 없는 로그를 받는 4가지 매크로 × `-std=c11/c23 -Wpedantic` | `demo_fmt.sh` |
| `demo_fmt.sh` | 위 둘 + `solution.c -DLOG_DEMO_FMT_BUG` 경고 확인 (Mac + VM) | `bash demo_fmt.sh` |
| `demo_link.c` | 링크까지: vsnprintf 로그 vs 토큰화 로그 (VM, nano.specs) | `demo_size.sh` 7절 |

## 4. 힌트

<details><summary>힌트 1 — format 속성의 두 숫자</summary>

`format(printf, M, N)`: M = 포맷 문자열이 **몇 번째 매개변수**인지(1부터), N = 검사할 가변 인자가 시작하는 위치. `log_emit(int lvl, const char *file, int line, const char *func, const char *fmt, ...)` 에서 fmt 는 다섯 번째, `...` 은 여섯 번째부터.

</details>

<details><summary>힌트 2 — "사라지지만 검사는 되는" 게이트</summary>

`#if` 로 매크로 몸통을 지우면 컴파일러는 그 호출을 아예 보지 못한다. 몸통은 그대로 두고 **조건이 컴파일 타임 상수 0** 이 되게 하라. `if (0 && x)` 의 몸통은 파싱·검사는 되지만 코드 생성이 안 된다. 런타임 레벨은 `&&` 오른쪽에 두면 상수 0 일 때 읽지도 않는다.

</details>

<details><summary>힌트 3 — fmt 를 따로 받지 말기</summary>

`#define LOG_I(fmt, ...)` 로 두면 `LOG_I("boom")` 에서 `...` 이 비어 `log_emit(..., "boom", )` 가 된다. fmt 까지 `__VA_ARGS__` 에 넣어 `#define LOG_I(...) LOG_AT_(LOG_LVL_INF, __VA_ARGS__)` 로 넘기면 C11 그대로 해결된다.

</details>

<details><summary>힌트 4 — snprintf 반환값과 잘림</summary>

`snprintf`/`vsnprintf` 는 "공간이 충분했다면 썼을 길이" 를 돌려준다. 접두사 길이 n 이 버퍼를 넘었는지 먼저 보고, 본문 m 을 더한 `len` 이 `LOG_LINE_MAX - 2`(= 62) 보다 크면 62 로 자른 뒤 `'\n'` 과 `'\0'` 을 붙인다.

</details>

<details><summary>힌트 5 — 링 버퍼 여유 공간</summary>

head/tail 을 계속 증가하는 `size_t` 로 두면 사용량 = `head - tail`, 여유 = `RING_SIZE - (head - tail)`. `len > 여유` 면 버리고 센다. 인덱스는 `% RING_SIZE` (2의 거듭제곱이라 랩어라운드도 안전).

</details>

## 5. 풀이

### Part A-1 — `format(printf, 5, 6)`: 포맷 검사를 내 함수에 빌려오기

```c
#define LOG_PRINTF_(fmt_idx, first_arg) __attribute__((format(printf, fmt_idx, first_arg)))
void log_emit(int lvl, const char *file, int line, const char *func, const char *fmt, ...) LOG_PRINTF_(5, 6);
```

이 속성이 있으면 컴파일러가 `log_emit` 호출을 `printf` 처럼 검사한다. `solution.c` 에 `-DLOG_DEMO_FMT_BUG` 를 붙이면 `main` 끝에 `LOG_D("%s", 42); LOG_I("%s", 42);` 두 줄이 들어간다 (`demo_fmt.sh` 1절, 실제 출력):

```text
-- solution.c:
solution.c:315:17: warning: format specifies type 'char *' but the argument has type 'int' [-Wformat]
solution.c:316:17: warning: format specifies type 'char *' but the argument has type 'int' [-Wformat]
-- starter.c:
```

315 행이 **꺼진 레벨(DBG)** 이다 — 바이너리에는 없는 줄인데 경고는 나온다. 이게 이 레슨 매크로의 핵심 가치다. 시작 코드(속성 없음)는 아무 말도 하지 않는다. `va_list` 를 받는 래퍼(`log_vemit(..., const char *fmt, va_list ap)`)라면 `format(printf, M, 0)` — 인자를 볼 수 없으니 호출부의 포맷 문자열 자체가 올바른지만 검사한다(GCC 문서의 vprintf 설명, 확인 권장).

### Part A-2/3 — 게이트: `if (상수 && 런타임)`

```c
#define LOG_ENABLED(lvl) ((lvl) <= LOG_LEVEL)
#define LOG_AT_(lvl, ...) do { \
        if (LOG_ENABLED(lvl) && (lvl) <= g_log_runtime_level) \
            log_emit((lvl), LOG_FILE_, __LINE__, __func__, __VA_ARGS__); \
    } while (0)
```

`cc -E -P solution.c` 로 본 `flash_erase` 안 두 줄 (실제 출력):

```text
do { if (((LOG_LVL_DBG) <= LOG_LVL_INF) && (LOG_LVL_DBG) <= g_log_runtime_level) log_emit((LOG_LVL_DBG), "solution.c", 134, __func__, "erase sector %u start", sector); } while (0);
do { if (((LOG_LVL_INF) <= LOG_LVL_INF) && (LOG_LVL_INF) <= g_log_runtime_level) log_emit((LOG_LVL_INF), "solution.c", 142, __func__, "sector %u erased", sector); } while (0);
```

- 열거 상수는 전처리기가 모르므로 `-E` 결과에 이름 그대로 남는다. 판정은 **컴파일러**가 한다: `4 <= 3` 은 정수 상수 식 → 거짓 → `&&` 오른쪽과 `if` 몸통은 코드 생성 대상이 아님.
- `__FILE_NAME__` 은 `"solution.c"`, `__LINE__` 은 134 로 이미 바뀌었지만 `__func__` 는 그대로다 — `__func__` 는 매크로가 아니라 **함수마다 컴파일러가 만드는 `static const char[]`** 이기 때문 (C11 6.4.2.2).
- `do { } while (0)` 로 감싸 매크로가 **세미콜론 하나로 끝나는 문장 하나**가 된다 → `if (i == 0) LOG_W("first"); else k = 1;` 가 의도대로 동작 (Part B 체크).

**레벨별 크기 (실측, `demo_size.sh` 1·5·6절).** 같은 `demo_fw.c` 를 `-Os -c` 로:

| LOG_LEVEL | 켜진 로그 | clang thumbv7em `.text` | `.rodata.str1.1` | 문자열 수 (메시지) | arm-none-eabi-gcc `.text` | gcc `.rodata*` | Mac arm64 `__text` | `__cstring` |
|---|---|---|---|---|---|---|---|---|
| 0 NONE | 0 | 196 | 0 | 0 (0) | 164 | 0 | 260 | 0 |
| 1 ERR | 3 | 316 | 107 | 6 (3) | 256 | 107 | 432 | 107 |
| 2 WRN | 6 | 400 | 180 | 9 (5) | 316 | 180 | 536 | 180 |
| 3 INF | 9 | 528 | 268 | 13 (8) | 412 | 268 | 688 | 268 |
| 4 DBG | 13 | 716 | 366 | 17 (12) | 520 | 366 | 940 | 366 |

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">demo_fw.c, clang thumbv7em -mcpu=cortex-m4 -Os -c, llvm-size -A 실측 (1 px = 2 B)</text>
  <text x="10" y="58" fill="currentColor">L=0 NONE</text>
  <rect x="90" y="44" width="98" height="20" class="f-mut" stroke="currentColor"/>
  <text x="196" y="59" fill="currentColor" font-size="12">196 + 0</text>
  <text x="10" y="93" fill="currentColor">L=1 ERR</text>
  <rect x="90" y="79" width="158" height="20" class="f-mut" stroke="currentColor"/>
  <rect x="248" y="79" width="53" height="20" class="f-warn" stroke="currentColor"/>
  <text x="309" y="94" fill="currentColor" font-size="12">316 + 107</text>
  <text x="10" y="128" fill="currentColor">L=2 WRN</text>
  <rect x="90" y="114" width="200" height="20" class="f-mut" stroke="currentColor"/>
  <rect x="290" y="114" width="90" height="20" class="f-warn" stroke="currentColor"/>
  <text x="388" y="129" fill="currentColor" font-size="12">400 + 180</text>
  <text x="10" y="163" fill="currentColor">L=3 INF</text>
  <rect x="90" y="149" width="264" height="20" class="f-mut" stroke="currentColor"/>
  <rect x="354" y="149" width="134" height="20" class="f-warn" stroke="currentColor"/>
  <text x="496" y="164" fill="currentColor" font-size="12">528 + 268</text>
  <text x="10" y="198" fill="currentColor">L=4 DBG</text>
  <rect x="90" y="184" width="358" height="20" class="f-mut" stroke="currentColor"/>
  <rect x="448" y="184" width="183" height="20" class="f-warn" stroke="currentColor"/>
  <text x="639" y="199" fill="currentColor" font-size="12">716 + 366</text>
  <text x="10" y="233" fill="currentColor">런타임만</text>
  <rect x="90" y="219" width="392" height="20" class="f-acc" stroke="currentColor"/>
  <rect x="482" y="219" width="183" height="20" class="f-warn" stroke="currentColor"/>
  <text x="673" y="234" fill="currentColor" font-size="12">784+366</text>
  <rect x="90" y="256" width="14" height="12" class="f-mut" stroke="currentColor"/>
  <text x="110" y="267" fill="currentColor" font-size="12">.text</text>
  <rect x="160" y="256" width="14" height="12" class="f-warn" stroke="currentColor"/>
  <text x="180" y="267" fill="currentColor" font-size="12">.rodata.str1.1 (포맷 + 파일 + 함수 이름)</text>
  <rect x="450" y="256" width="14" height="12" class="f-acc" stroke="currentColor"/>
  <text x="470" y="267" fill="currentColor" font-size="12">런타임 레벨 검사 포함 .text</text>
</svg>
```

표 읽는 법:

- **문자열 테이블은 세 컴파일러가 바이트까지 같다** (107/180/268/366) — 같은 문자열을 같은 규칙(NUL 포함, 중복 병합)으로 넣기 때문. L=3 의 13개 = 파일 이름 `demo_fw.c` 1 + 함수 이름 4 (`uart_send`, `flash_erase`, `sensor_read`, `fw_boot_banner` — `__func__`) + 메시지 8. 켜진 로그는 9개인데 메시지는 8개다 — 빠진 건 `sensor_read` 의 `LOG_W("over temperature %d mC")`. `raw = ADC1_DR & 0xFFF` 이므로 `mc ≤ 4095·3300/4095 − 500 = 2800` 이고, 컴파일러가 값 범위로 `mc > 85000` 이 불가능함을 증명해 **그 분기와 문자열을 지웠다** (조건을 `mc > 2000` 으로 바꾸면 문자열이 생김 — `strings -a` 실측 0 → 1). L=4 의 13개 로그 / 메시지 12 도 같은 이유. 문자열 수는 "소스의 로그 수" 가 아니라 "도달 가능한 로그 수" 다.
- 차이: clang 은 `__func__` 를 `.rodata.str1.1`(병합 가능 문자열)에, GCC 는 일반 `.rodata` 에 둔다 (`readelf -S`: `.rodata.str1.1` 0xdb + `.rodata` 0x31 = 219 + 49 = 268, `readelf -p .rodata` 로 함수 이름 4개 확인).
- 로그 하나의 코드 비용: L=3 → L=4 에서 DBG 4개가 +188 B (clang) / +108 B (gcc) → 호출 하나에 대략 27~47 B. 예를 들어 `fw_boot_banner()` (로그 1개만 있는 함수)는 clang 에서 48 B, 그중 호출 준비가 `movw/movt` 쌍 3개(문자열 주소 3개: 포맷, 파일, `__func__`) + 인자 레지스터 + `bl log_emit` 이다:

```text
000001e0 <fw_boot_banner>:
     1e0: b580          push   {r7, lr}
     1e2: b082          sub    sp, #0x8
     1e4: 2008          movs   r0, #0x8                 ; reset cause 인자 → 스택(5번째 이후 인자)
     1e6: f240 0c00     movw   r12, #0x0   R_ARM_THM_MOVW_ABS_NC .L.str.9   ; 포맷 문자열
     1ea: f240 0100     movw   r1, #0x0    R_ARM_THM_MOVW_ABS_NC .L.str     ; "demo_fw.c"
     1ee: f240 0300     movw   r3, #0x0    R_ARM_THM_MOVW_ABS_NC .L__func__.fw_boot_banner
     ...  (movt 3개, str)
     200: 2003          movs   r0, #0x3                 ; lvl = INF
     202: 227f          movs   r2, #0x7f                ; __LINE__ = 127
     208: f7ff fffe     bl     ...         R_ARM_THM_CALL log_emit
     20e: bd80          pop    {r7, pc}
```

(`objdump -d -r --disassemble-symbols=fw_boot_banner` 실제 출력에서 relocation 줄을 옆으로 붙이고 일부 생략. 인자 6개 중 4개만 r0–r3, 나머지는 스택 — AAPCS. 그래서 인자 많은 로그는 비싸다.)

**`((void)0)` 과 비교 (실측, 2절).** `#if LOG_LEVEL >= n` 으로 매크로를 `((void)0)` 으로 바꾸는 옛 방식(`LOG_STYLE_VOID0`)은 모든 레벨에서 **디스어셈블리가 IF0 와 완전히 같다** (`cmp` 결과 `same-code`, 크기 동일). 차이는 진단뿐:

```text
VOID0 L=1  .text=316 .rodata.str1.1=107  same-as-IF0: same-code   warnings: 1
demo_fw.c:93:14: warning: unused variable 't0' [-Wunused-variable]
   93 |     uint32_t t0 = now_ms();
```

`t0` 는 INF 로그(`erased in %lu ms`)에서만 쓰인다. `((void)0)` 으로 지우면 L=0..2 빌드에서 unused 경고 → `-Werror` CI 에서 **릴리스 빌드만 실패**. 그리고 `demo_fmt.sh` 2절: 꺼진 `LOG_D("%s", 42)` 가 IF0 에선 경고, VOID0 에선 **침묵** — 디버그 빌드를 켜는 순간에야 터진다.

**-O0 에서도 사라지나? (4·6절)** 그렇다. LOG_LEVEL=1 에서 clang `-O0` 은 `.text` 546 B(최적화가 없어 큼)지만 문자열은 `-Os` 와 똑같이 6개(메시지 3)다. arm-none-eabi-gcc `-O0`/`-Og` 도 DBG/INF 문자열 0개(`DBG/INF text present? 0`). 두 컴파일러 모두 상수 거짓 `if` 몸통을 최적화 단계 이전에 버린다 — 그래서 디버그(-O0) 빌드에서도 크기 효과가 보장된다. 단 레벨을 **변수**로 두면 얘기가 다르다 (`/tmp` 실험, thumbv7em): `static const int lvl = 3; if (4 <= lvl) log_emit("debug only message");` 는 clang 이 -O0 에서도 접어 문자열 0 이었지만 arm-none-eabi-gcc `-O0` 에선 문자열이 **남았다**(-Os 에선 0). 그냥 `int lvl` 이면 두 컴파일러 모든 -O 에서 남는다. → 레벨은 매크로나 열거 상수(정수 상수 식)로 둔다.

### Part B — 접두사·파일 이름·잘림

**파일 이름.** `__FILE__` 은 **컴파일러에 넘긴 경로 그대로**다. CI 가 절대 경로로 컴파일하면 문자열 테이블에 전체 경로가 들어간다 (`demo_size.sh` 3절, LOG_LEVEL=4, 실측):

| 방법 | `.rodata.str1.1` | 남은 파일 문자열 |
|---|---|---|
| `__FILE_NAME__` (clang 9+ / GCC 12+ 로 알려짐; Apple clang 21 · arm-none-eabi-gcc 14 에서 동작 확인) | 366 | `'demo_fw.c'` |
| `__FILE__` + 절대 경로 컴파일 | **487** | `'/Users/donh/workspace/…/lessons/TOOL/TOOL-B13/demo_fw.c'` |
| `__FILE__` + `-fmacro-prefix-map=$PWD/=` | 366 | `'demo_fw.c'` |

121 B 차이는 파일 하나, 모듈 하나 기준이다. 파일마다 경로가 하나씩이니 수백 개 파일이면 수 KB, 게다가 빌드 머신의 디렉터리 구조(사용자 이름 포함)가 펌웨어 이미지에 박힌다 → 재현 가능한 빌드(TOOL-A06)에도 나쁘다. `log_basename()` 은 `__FILE_NAME__` 이 없는 컴파일러(오래된 GCC, IAR/Keil 등)용 런타임 대체다 — 이때는 **전체 경로 문자열이 Flash 에 그대로 남고** 포인터만 뒤로 옮긴다는 점을 알고 써야 한다. `'\\'` 도 구분자로 보는 이유: Windows 호스트에서 빌드한 펌웨어의 `__FILE__` 은 `C:\fw\src\spi.c` 꼴.

**잘림.** 64 B 스택 버퍼, 접두사 `"[I] solution.c:NNN log_body: "` 를 쓰고 남은 곳에 본문:

| 경우 | 본문 길이 | 결과 (`cap_len`) | `g_log_truncated` |
|---|---|---|---|
| 딱 맞음 | 62 − 접두사 | 63 (`...xx\n`) | 0 |
| 1 바이트 초과 | 63 − 접두사 | 63 (마지막 x 대신 `\n`) | 1 |
| 많이 초과 | 100 | 63 | 2 |

`vsnprintf` 는 버퍼 끝에 항상 NUL 을 쓰고 "썼을 길이" 를 돌려주므로 `len` 을 62 로 줄인 뒤 `buf[62] = '\n'`, `buf[63] = '\0'`. 접두사만으로 넘친 경우(`len >= 63`)는 본문을 건너뛴다 — `buf + len` 이 버퍼 밖을 가리키지 않게 하는 가드다. 펌웨어에서 로그 버퍼를 스택에 두면 **가장 깊은 호출 경로의 스택 + 64 B + vsnprintf 내부 스택**이 필요하다 (TOOL-A08).

**인자 없는 로그 — `##__VA_ARGS__` vs `__VA_OPT__` vs C11.** `demo_variadic.c` 의 4가지를 `-Wall -Wextra -Wpedantic` 로 (Mac clang 21, VM gcc 15 실측):

| STYLE | 매크로 | clang `-std=c11` | clang `-std=c23` | gcc `-std=c11` | gcc `-std=c23` |
|---|---|---|---|---|---|
| 1 GNU | `emit(fmt, ##__VA_ARGS__)` | 경고 `token pasting of ',' and __VA_ARGS__ is a GNU extension` | 같은 경고 | 경고 `ISO C99 requires at least one argument for the '...'` | 없음 |
| 2 C23 | `emit(fmt __VA_OPT__(,) __VA_ARGS__)` | 경고 `passing no argument for the '...' ... is a C23 extension` | 없음 | 경고 `'__VA_OPT__' is not available until C23` + 위 경고 | 없음 |
| 3 C11 | `#define LOG(...) emit(__VA_ARGS__)` | **없음** | 없음 | **없음** | 없음 |
| 4 순진 | `emit(fmt, __VA_ARGS__)` | **에러** `expected expression` | 에러 | 에러 `expected expression before ')' token` | 에러 |

`-Wpedantic` 없이는 1·2 도 조용하다. 이 레슨은 3번을 골랐다: 표준 C11 에서 경고 0, `fmt` 가 첫 인자라는 사실은 `format(printf, 5, 6)` 이 대신 지켜 준다(포맷 없는 `LOG_I()` 는 `log_emit(..., __func__, )` 로 펼쳐져 `error: expected expression`, 실측). 단점은 매크로 안에서 fmt 에 이름을 붙일 수 없다는 것 — 예를 들어 `fmt` 앞에 문자열 리터럴 `"[I] " fmt` 를 이어 붙이는 기법(접두사를 컴파일 타임에 합치기)은 STYLE 1/2 가 필요하다.

### Part C — 런타임 레벨, UART 링, 타깃의 포맷 함정

**컴파일 타임 상한 + 런타임 레벨.** `LOG_ENABLED(lvl) && lvl <= g_log_runtime_level` 의 의미:

| 레벨 | 컴파일 타임 (LOG_LEVEL=INF) | 런타임 (`g_log_runtime_level`) | 결과 |
|---|---|---|---|
| ERR/WRN | 켜짐 | WRN 이상이면 통과 | 출력 |
| INF | 켜짐 | WRN 으로 낮추면 막힘 | 출력 없음, **인자 평가 없음** (`&&` 단락) |
| DBG | **꺼짐** | DBG 로 올려도 | 출력 없음 — 코드가 이미 없다 |

비용: 런타임 검사(전역 로드 + 비교 + 분기)가 로그마다 붙어 L=4 기준 `.text` 716 → 784 B (+68 B, 13개 로그, `BOTH`/`RUNTIME` 스타일 실측). 런타임 필터**만** 쓰는 방식(`RUNTIME`)은 문자열 366 B 가 레벨과 무관하게 항상 남는다 — LOG_LEVEL=1 빌드의 107 B 와 비교하면 차이가 크다. 실무 조합: 릴리스 = 컴파일 타임 WRN + 런타임 WRN, 현장 디버깅 빌드 = 컴파일 타임 DBG + 런타임은 CLI 명령으로 조절.

**UART 링 싱크.** TX ISR 이 비우는 링에 넣는 쪽이 지켜야 할 것: **기다리지 않는다**(ISR·고우선순위 태스크에서 불릴 수 있다), **반쪽 줄을 넣지 않는다**(호스트 파서가 깨진다), **버린 것을 센다**(나중에 `"N lines dropped"` 를 찍을 근거).

```svg
<svg viewBox="0 0 740 210" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">uart_ring_sink: 256 B 링, 한 줄 48 B (solution.c 실측)</text>
  <rect x="10" y="40" width="96" height="36" class="f-ok" stroke="currentColor"/>
  <rect x="106" y="40" width="96" height="36" class="f-ok" stroke="currentColor"/>
  <rect x="202" y="40" width="96" height="36" class="f-ok" stroke="currentColor"/>
  <rect x="298" y="40" width="96" height="36" class="f-ok" stroke="currentColor"/>
  <rect x="394" y="40" width="96" height="36" class="f-ok" stroke="currentColor"/>
  <rect x="490" y="40" width="32" height="36" class="f-mut" stroke="currentColor"/>
  <text x="36" y="63" fill="currentColor" font-size="12">줄 1</text>
  <text x="132" y="63" fill="currentColor" font-size="12">줄 2</text>
  <text x="228" y="63" fill="currentColor" font-size="12">줄 3</text>
  <text x="324" y="63" fill="currentColor" font-size="12">줄 4</text>
  <text x="420" y="63" fill="currentColor" font-size="12">줄 5</text>
  <text x="493" y="63" fill="currentColor" font-size="12">16</text>
  <text x="10" y="96" fill="currentColor" font-size="12">tail(ISR 이 여기서 꺼냄)</text>
  <text x="380" y="96" fill="currentColor" font-size="12">head = tail + 240</text>
  <rect x="540" y="40" width="190" height="36" class="f-warn" stroke="currentColor"/>
  <text x="550" y="56" fill="currentColor" font-size="12">줄 6 (48 B) &gt; 16 B</text>
  <text x="550" y="71" fill="currentColor" font-size="12">→ 버림, dropped++</text>
  <text x="10" y="128" fill="currentColor">flash_erase(3) 6번 → 5줄 들어감 + 1줄 버림 (lines == 256 / 48, dropped == 6 − lines)</text>
  <text x="10" y="152" fill="currentColor">여유 = RING_SIZE − (head − tail).  head, tail 은 계속 증가하는 size_t, 위치는 % 256.</text>
  <text x="10" y="176" fill="currentColor">실제 하드웨어: head 는 로그 쪽만, tail 은 TX ISR 만 쓴다 → 단일 생산자/단일 소비자.</text>
  <text x="10" y="198" fill="currentColor">여러 태스크가 로그를 쓰면 생산자가 둘 이상 → 임계 구역 필요 (RTOS-B10, C-M14).</text>
</svg>
```

`flash_erase(0)`, `flash_erase(9)` 뒤 링에서 꺼낸 실제 내용 (solution.c 에 `printf` 두 줄만 끼운 복사본을 `/tmp/TOOL-B13_dbgdir/solution.c` 로 돌려서 찍음 — 파일 이름과 `flash_erase` 줄 번호는 원본과 같다):

```text
[W] solution.c:140 flash_erase: erasing boot sector
[I] solution.c:142 flash_erase: sector 0 erased
[E] solution.c:136 flash_erase: bad sector 9
got=240 line_len=48 lines=5 dropped=1
```

`LOG_D("erase sector %u start")`(134행)는 컴파일 타임에 사라져 링에 없다. 체크는 파일 이름에 상관없이 `lines == RING_SIZE / line_len` 으로 쓴다 (starter.c 는 9자라 줄 길이가 1 B 짧다).

**타깃에서만 나는 포맷 경고 — `uint32_t` + `%u`** (`demo_fmt.sh` 3·5절):

```text
-- arm-none-eabi-gcc PART=2
demo_fmt.c:18:26: warning: format '%u' expects argument of type 'unsigned int', but argument 3 has type 'uint32_t' {aka 'long unsigned int'} [-Wformat=]
-- arm-none-eabi-gcc uint32_t = #define __UINT32_TYPE__ long unsigned int
-- gcc (aarch64 linux) PART=2: 0 warnings
-- clang thumbv7em uint32_t =
#define __UINT32_TYPE__ unsigned int
```

Mac clang, Linux gcc, **clang `--target=thumbv7em-none-eabi`** 은 모두 조용한데 arm-none-eabi-gcc 만 경고한다 — 그 툴체인에서 `uint32_t` 가 `unsigned long` 이기 때문이다(둘 다 32비트라 ABI 는 같지만 printf 의 타입 규칙상 다른 타입). 호스트 단위 테스트에서 깨끗하던 로그가 타깃 빌드의 `-Werror` 에서 깨지는 전형적인 사고. 해결은 `<inttypes.h>` 의 `"%" PRIu32` (VM 에서 `report_fixed` 경고 0개 확인) 또는 `(unsigned long)x` + `%lu` 캐스트. (`demo_fw.c` 가 `%lu` + 캐스트를 쓰는 이유: Mac clang 의 bare-metal 타깃에선 `<inttypes.h>` 를 쓸 수 없다 — libc 가 없음.)

**함수 포인터에 붙이는 format 속성** (`demo_fmt.c` PART=1):

```c
typedef void (*sink_t)(const char *fmt, ...) __attribute__((format(printf, 1, 2)));    /* (a) */
static void (*sink_var)(const char *fmt, ...) __attribute__((format(printf, 1, 2)));   /* (b) */
```

| | Mac clang 21 | arm-none-eabi-gcc 14 |
|---|---|---|
| (a) typedef 로 선언한 매개변수 `a("%s\n", 42)` | **경고 없음** (속성을 조용히 무시, `-Wall -Wextra` 에서도 무경고) | 경고 |
| (b) 변수 선언에 직접 `sink_var("%s\n", 43)` | 경고 | 경고 |

그래서 이 레슨은 싱크 포인터가 **완성된 줄**만 받게 하고(포맷 없음), 포맷 검사는 일반 함수 `log_emit` 한 곳에 붙였다 — 컴파일러마다 다른 함수 포인터 속성 동작에 기대지 않는다.

**비리터럴 포맷.** `LOG_I(msg)` 처럼 변수를 포맷으로 넘기면 `-Wall` 에서 `format string is not a string literal (potentially insecure) [-Wformat-security]` + 고침 제안 `"%s", ` 이 나온다. 인자가 있는 비리터럴 포맷(`log_emit(3, msg, 1)`)은 `-Wformat=2`(`-Wformat-nonliteral`)에서만 잡힌다 (Mac clang 실측). `msg` 에 `%` 가 섞여 있으면 스택을 읽어 출력하는 버그(포맷 문자열 공격)가 된다.

**토큰화(지연) 로그 — 문자열을 아예 타깃에 두지 않기.** 지금까지는 "꺼서 없애기" 였다. 켜 둔 로그도 줄이려면 타깃에서 **포맷하지 않고** (포맷 문자열의 ID 또는 주소, 인자 값)만 기록하고 호스트가 ELF 의 문자열 테이블로 복원한다. Zephyr 의 dictionary-based logging, Pigweed `pw_tokenizer`, trice, Rust 의 defmt 가 이 방식이다(각 구현의 세부는 문서 확인 권장). `demo_link.c` 를 VM 에서 링크한 실측 (`arm-none-eabi-gcc -Os`, `demo_size.sh` 7절):

```text
   text	   data	    bss	    dec	    hex	filename
   6552	    108	    632	   7292	   1c7c	fmt_nano.elf      ← vsnprintf 로 한 줄 만들기 (newlib-nano)
   3392	    108	    828	   4328	   10e8	tok_nano.elf      ← (포맷 주소, a, b) 12 B 레코드만 저장
  32208	   1736	    940	  34884	   8844	fmt_full.elf      ← 같은 코드, nano 없이 full newlib
```

로그 5개짜리 프로그램에서 포맷 코드만 약 3.1 KB(nano 기준; full newlib 이면 28 KB 이상 — printf 계열이 끌고 오는 코드는 TOOL-B06 참고). 토큰화 쪽의 bss +196 B 는 16칸 레코드 링이다. 추가 이득: ISR 에서 몇 사이클이면 기록 끝, 포맷 문자열은 로드되지 않는 섹션(TOOL-M03 / TOOL-M01)에 두면 Flash 0 B. 대가: 호스트 디코더와 **정확히 같은 ELF** 가 필요하다(빌드 ID 관리 — TOOL-A06). 추가 연습 4 가 이 구조의 작은 모형이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 27/27 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_san && ./sol_san   # 27/27, 보고 없음
cc -std=c11 -Wall -Wextra -O2 -DLOG_DEMO_FMT_BUG solution.c -o /tmp/x              # 경고 2개가 나와야 정답 (꺼진 LOG_D 포함)
bash demo_size.sh > demo_size.txt; bash demo_fmt.sh > demo_fmt.txt                 # 크기·경고 표 재생성 (VM 필요)
```

실제 출력:

```text
27/27 passed
```

ASan + UBSan 빌드도 `27/27 passed`, 런타임 보고 없음 (starter 도 sanitizer 빌드에서 5/27 로 끝나고 보고 없음 — 시작 코드의 테스트가 0 길이 버퍼를 건드리지 않게 `got > 0 &&`, `line_len > 0 &&` 가드를 넣었다).

## 7. 자주 하는 실수

**1. `do { } while (0)` 없이 `if` 로 시작하는 매크로.**
`#define LOG_W(...) if (LOG_ENABLED(2)) log_emit(...)` 이면 `if (a) LOG_W("x"); else b();` 의 `else` 가 매크로 안의 `if` 에 붙는다(매달린 else). 중괄호만 쓴 `{ ... }` 는 뒤의 `;` 때문에 `else` 앞에서 문법 에러. → `do { if (...) ...; } while (0)`.

**2. 꺼진 레벨을 `((void)0)` 으로 지우기.**
코드 크기는 같지만(실측 `same-code`) 포맷 검사와 변수 사용이 같이 사라진다 → 릴리스에선 `unused variable 't0'`(실측), 디버그 빌드를 켜는 날 포맷 경고 폭탄. → `if (0)` 계열 게이트. (Linux 커널의 `no_printk()` 도 같은 이유로 `if (0) printk(...)` 꼴이다 — `include/linux/printk.h`, 확인 권장.)

**3. 로그 인자에 부수효과.**
`LOG_D("ack=%d", spi_read_ack());` — DBG 가 꺼진 릴리스에선 SPI 읽기 자체가 사라져 **동작이 바뀐다** (체크 `g_calls == 0` 이 바로 이것). `assert()` 와 같은 규칙: 로그 인자는 읽기 전용 식만. 꼭 필요한 값은 먼저 변수에 받는다.

**4. 런타임 레벨만 두고 "꺼 놨으니 괜찮다".**
문자열·코드가 그대로 남는다 (L=4 상당 366 B 문자열 + 784 B 코드 vs LOG_LEVEL=1 의 107 B + 316 B). 바이너리 `strings` 로 내부 메시지가 다 보인다 — 보안 리뷰에서도 지적받는다.

**5. `uint32_t` 를 `%u` 로.**
호스트(Mac/Linux)와 clang bare-metal 에선 조용하고 arm-none-eabi-gcc 에서만 경고(실측) → `PRIu32` 또는 `(unsigned long)` + `%lu`. `size_t` 는 `%zu`, 포인터는 `%p`.

**6. 포맷 속성을 함수 포인터 typedef 에만 붙이기.**
clang 은 typedef 에 붙은 `format` 속성을 경고 없이 무시한다(실측) → 검사가 되는 줄 알고 넘어감. 일반 함수나 변수 선언에 붙인다.

**7. `LOG_LEVEL` 을 헤더 include 뒤에 정의.**
헤더의 `#ifndef LOG_LEVEL` 기본값이 먼저 잡히고, 뒤의 `#define LOG_LEVEL 4` 는 `'LOG_LEVEL' macro redefined [-Wmacro-redefined]` 경고(실측)와 함께 그 뒤 줄부터만 적용된다 — 파일 안에서 레벨이 바뀌는 혼란. → `-DLOG_LEVEL=` 빌드 플래그 또는 include **앞**에 정의.

**8. `__FILE__` 을 그대로.**
절대 경로 컴파일이면 파일마다 경로 전체가 Flash 에 (실측 +121 B/파일, 빌드 머신 경로 노출). → `__FILE_NAME__` 또는 `-fmacro-prefix-map=$(SRC_ROOT)/=`.

## 8. 면접 Q&A

<details><summary>Q1. 로그 매크로를 왜 <code>do { ... } while (0)</code> 로 감싸나요? <code>{ }</code> 만으로는 안 되나요?</summary>

매크로를 "세미콜론으로 끝나는 문장 하나" 로 만들기 위해서다. `{ ... }` 만 쓰면 `if (x) LOG_W("a"); else ...` 에서 `{...};` 의 `;` 가 빈 문장이 되어 `else` 가 짝을 잃는다(문법 에러). `if` 로 시작하는 매크로는 매달린 else 문제를 만든다. `do { } while (0)` 은 정확히 한 문장이고 뒤에 `;` 를 요구하며, 최적화 후 루프 코드는 0 이다. 꼬리: 값을 돌려줘야 하면? → GNU statement expression `({ ... })` 나 인라인 함수 (표준 C 엔 없음).

</details>

<details><summary>Q2. 꺼진 레벨을 <code>#define LOG_D(...) ((void)0)</code> 로 하는 것과 <code>if (0) log(...)</code> 로 하는 것의 차이는?</summary>

생성 코드는 같다(이 레슨에서 디스어셈블리까지 동일 실측). 차이는 **컴파일러가 호출식을 보느냐**다. `if (0)` 은 파싱·타입 검사·포맷 검사를 하고, 인자 속 변수를 "사용됨" 으로 친다. `((void)0)` 은 그 줄이 사라지므로 (1) 꺼진 레벨의 포맷 오류·오타가 디버그 빌드를 켤 때까지 숨고, (2) 로그에서만 쓰던 변수에 unused 경고가 나 `-Werror` 릴리스 빌드가 깨진다. 단 `if (0)` 이라도 인자 안의 식은 **평가되지 않는다** — 부수효과가 사라지는 건 둘 다 같다.

</details>

<details><summary>Q3. 런타임 로그 레벨 변수 하나로 다 거르면 안 되나요? 왜 컴파일 타임 레벨이 필요하죠?</summary>

런타임 필터는 실행만 막을 뿐 **코드와 문자열은 이미지에 남는다**. 이 레슨 모듈에서 LOG_LEVEL=1 빌드는 `.text` 316 B + 문자열 107 B, 런타임 필터만 쓰면 784 + 366 B (Cortex-M4, 실측). 수백 개 파일이면 수십 KB, 256 KB Flash MCU 에선 결정적이다. 그리고 로그마다 전역 로드·비교·분기가 붙는다. 그래서 컴파일 타임 상한(Flash)과 런타임 레벨(현장 조절)을 둘 다 둔다 — 런타임은 상한 아래에서만 의미가 있다.

</details>

<details><summary>Q4. <code>__attribute__((format(printf, 5, 6)))</code> 의 5와 6은? vprintf 형 함수에는?</summary>

5 = 포맷 문자열이 다섯 번째 매개변수, 6 = 검사할 가변 인자가 여섯 번째부터(1부터 셈; C++ 비정적 멤버 함수면 암시적 `this` 가 1번). `va_list` 를 받는 함수(`log_vemit(..., const char *fmt, va_list ap)`)는 두 번째 숫자를 0 으로 — 호출부 인자는 볼 수 없으니 포맷 문자열 자체의 일관성만 검사한다(GCC 문서). 또 속성이 붙은 fmt 를 함수 안에서 `vsnprintf` 로 넘기는 것은 `-Wformat-nonliteral` 대상이 아니다 — 이 레슨의 `log_emit` 도 `-Wformat=2` 에서 조용한 이유(practice.c 의 디코더처럼 속성 없는 포인터를 넘기면 경고, 실측). GCC/clang 확장이라 매크로로 감싸 다른 컴파일러에선 비운다.

</details>

<details><summary>Q5. 호스트에선 경고가 없는데 타깃 빌드에서만 <code>%u</code> 경고가 납니다. 왜요?</summary>

arm-none-eabi-gcc 에서 `uint32_t` 는 `long unsigned int` 다(`__UINT32_TYPE__` 실측). Mac clang, Linux gcc, clang 의 thumbv7em 타깃에선 `unsigned int`. 크기·ABI 는 같아도 printf 규칙상 `%u` 는 `unsigned int` 전용이라 타입 불일치 경고. 해결: `PRIu32` 매크로, 또는 `%lu` + `(unsigned long)` 캐스트. 교훈: CI 에서 **타깃 툴체인으로도** `-Wall -Werror` 빌드를 돌린다 (호스트 테스트 빌드만으론 못 잡는다).

</details>

<details><summary>Q6. <code>__FILE__</code> 을 로그에 쓰면 무슨 문제가 있나요?</summary>

컴파일러에 넘긴 경로 그대로라 절대 경로 빌드면 `/home/ci/.../src/drv/uart.c` 전체가 Flash 에 들어간다 (이 레슨 실측 파일 하나 +121 B). 빌드 디렉터리가 바뀌면 바이너리도 바뀌어 재현 가능한 빌드가 깨지고, 내부 경로·사용자 이름이 노출된다. 해결: clang/GCC 12+ 의 `__FILE_NAME__`, 또는 `-fmacro-prefix-map=OLD=NEW` (`-ffile-prefix-map` 은 디버그 정보까지). 런타임 `strrchr` basename 은 출력만 짧아지고 Flash 의 전체 경로는 그대로라는 점에 주의.

</details>

<details><summary>Q7. ISR 안에서 <code>LOG_I</code> 를 불러도 되나요?</summary>

보통은 안 된다. (1) `vsnprintf` 는 수백 B 스택과 수~수십 µs 를 쓴다 — ISR 지연과 스택 오버플로 위험. (2) 블로킹 UART 출력이면 ISR 이 바이트 전송 시간만큼 멈춘다 (115200 baud 에서 50 B ≈ 4.3 ms). (3) 공유 링/버퍼가 태스크와 ISR 사이에서 경쟁한다. 실무: ISR 에선 아예 안 찍거나, 링에 **넣기만** 하고 꽉 차면 버리며 세는 비차단 싱크, 더 나아가 토큰화 로그(ID + 인자만 기록, 포맷은 호스트에서). 넣는 쪽이 여럿이면 짧은 임계 구역이 필요하다.

</details>

<details><summary>Q8. 토큰화(딕셔너리) 로깅이 뭔가요? 단점은?</summary>

타깃은 포맷 문자열 대신 그 ID(또는 비로드 섹션 안의 주소)와 인자 값만 이진으로 내보내고, 호스트 도구가 같은 빌드의 ELF/딕셔너리로 문자열을 복원한다(Zephyr dictionary logging, Pigweed pw_tokenizer, trice, defmt 등 — 세부는 각 문서 확인 권장). 이득: 문자열이 Flash 에 없고, printf 코드가 빠지고(이 레슨 링크 실측 nano 기준 약 3.1 KB), 출력 바이트·시간이 크게 준다. 단점: 빌드마다 딕셔너리를 보관·매칭해야 하고(빌드 ID), `%s` 같은 런타임 문자열 인자는 따로 처리해야 하며, 터미널에서 바로 읽을 수 없다.

</details>

<details><summary>Q9. 꺼진 로그가 정말 바이너리에 없는지 어떻게 증명하나요?</summary>

`llvm-size -A` / `arm-none-eabi-size -A` 로 `.text` 와 `.rodata*` 크기를 레벨별로 비교하고, `readelf -p .rodata.str1.1` (Mac 은 `objdump -s -j __cstring`)로 그 메시지 문자열이 없는지 확인한다. 최종 이미지에선 `strings fw.elf | grep "erase sector"`. 이 레슨의 `demo_size.sh` 가 정확히 이 순서이고, -O0 빌드까지 확인했다. 링크 맵에서 함수별 크기를 보는 법은 TOOL-M02.

</details>

## 9. 추가 연습

### 연습 1 — `LOG_ONCE`: 호출 지점마다 딱 한 번

센서 폴링 루프에서 "보정 안 됨" 경고가 1 ms 마다 찍혀 UART 를 막는다. 호출 지점별로 처음 한 번만 찍는 `LOG_ONCE(lvl, ...)` 를 만들어라. 서로 다른 두 호출 지점은 각자 한 번씩.

<details><summary>답 보기</summary>

매크로 안의 `do { static unsigned char once_; ... }` — 블록 스코프 `static` 은 매크로가 **펼쳐진 자리마다** 하나씩 생기므로 호출 지점별 플래그가 된다. `if (LOG_ENABLED(lvl) && !once_) { once_ = 1; LOG_AT_(...); }` — 꺼진 레벨이면 플래그 변수도 최적화로 사라진다. practice.c: `poll_sensor()` 5번 + 다른 지점 3번 → 정확히 2줄. 주의: 여러 태스크가 같은 지점을 동시에 치면 두 번 찍힐 수 있다(무해한 경쟁이지만 엄밀히는 데이터 레이스 — 필요하면 atomic exchange, TOOL-M09).

</details>

### 연습 2 — `LOG_EVERY_N`: 비율 제한

rx overrun 이 초당 수천 번 나도 1번째, N+1번째, 2N+1번째만 찍는 `LOG_EVERY_N(n, lvl, ...)`. 꺼진 레벨이면 카운터도 인자도 건드리지 않아야 한다.

<details><summary>답 보기</summary>

`static unsigned cnt_; if (LOG_ENABLED(lvl) && (cnt_++ % (n)) == 0u) LOG_AT_(...)`. `&&` 단락 덕분에 꺼진 레벨은 `cnt_++` 도 하지 않는다. practice.c: 10번 중 N=4 → `#0, #4, #8` 3줄, DBG 버전은 `evals == 0`. 실무에선 "건너뛴 횟수" 를 같이 찍는 게 좋고(`(%u suppressed)`), 시간 기반 제한(초당 1회)은 틱 카운터로 만든다.

</details>

### 연습 3 — 모듈별 컴파일 타임 레벨

FLASH 드라이버를 디버깅 중이라 그 모듈만 DBG, 시끄러운 UART 모듈은 ERR 만. `MLOG(FLASH, D, ...)` 처럼 모듈 이름을 받아 `FLASH_LOG_LEVEL` 과 비교하는 매크로를 만들어라.

<details><summary>답 보기</summary>

토큰 붙이기: `if (LVL_##L##_ <= mod##_LOG_LEVEL) log_emit(LVL_##L##_, #mod, __VA_ARGS__)`. `mod##_LOG_LEVEL` → `FLASH_LOG_LEVEL`, `#mod` → `"FLASH"`(접두사). 모듈 레벨이 정의 안 됐으면 컴파일 에러로 바로 알 수 있다. practice.c: `n` 3번 시도 중 UART W 는 평가 안 됨 → `n == 2`, 출력 2줄. Zephyr 의 `LOG_MODULE_REGISTER(name, level)` 이 같은 발상(파일마다 레벨)이다. 실무에선 각 .c 맨 위에 `#define LOG_MODULE_LEVEL ...` 를 두고 공용 매크로가 그걸 보게 한다.

</details>

### 연습 4 — 토큰화 로그 모형

`TLOG(fmt, a, b)` 가 포맷하지 않고 `{포맷 위치, a, b}` 레코드만 링(8칸)에 저장하고, 호스트 쪽 `tlog_decode()` 가 나중에 문자열로 바꾼다. 포맷 검사는 유지할 것. 9번째부터는 버리고 센다.

<details><summary>답 보기</summary>

`static const char fmt_[] = fmt;` 로 호출 지점마다 포맷 하나(= ID), `tlog_put(fmt_, a, b)`. 포맷 검사는 `if (0) tlog_fmt_check(fmt, (int)(a), (int)(b));` — `format(printf,1,2)` 가 붙은 빈 함수를 `if (0)` 으로 불러 **검사만** 한다(본문 게이트와 같은 기법). practice.c: 10번 기록 → 8개 저장, 2개 버림, 같은 지점 레코드는 같은 포인터, 디코드 결과 `"boot reason 3, wdt 0\nadc ch0 = 1000\n..."`. 실행 출력 `8 records x 16 B = 128 B stored vs 126 B of formatted text` — 호스트(64비트 포인터)라 레코드가 16 B 이고 메시지가 짧아 텍스트와 비슷하다. Cortex-M 에선 12 B 이고, 진짜 이득은 RAM 이 아니라 **Flash 의 문자열·printf 코드가 빠지는 것과 기록 시간**이다(5. 풀이 Part C 의 링크 실측). 디코더의 `snprintf(out, …, g_tlog[i].fmt, …)` 는 비리터럴 포맷이라 `-Wformat=2` 에서 경고가 난다(실측 1개) — 디코더 함수에만 국한된 정당한 예외다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o pr_san && ./pr_san
```

실제 출력 (두 빌드 같음):

```text
  [practice 4] 8 records x 16 B = 128 B stored vs 126 B of formatted text
9/9 passed
```

## 10. 핵심 정리

- 게이트 = `do { if (LOG_ENABLED(lvl) && lvl <= g_runtime) log_emit(lvl, __FILE_NAME__, __LINE__, __func__, __VA_ARGS__); } while (0)`. 상수 거짓이면 코드·문자열·인자 평가 0 (clang/gcc -O0 에서도, 실측), 포맷 검사·변수 사용은 유지.
- `((void)0)` 은 같은 코드지만 꺼진 레벨의 포맷 오류를 숨기고 unused 경고로 `-Werror` 릴리스 빌드를 깬다.
- `format(printf, 5, 6)` = 5번째가 fmt, 6번째부터 가변 인자. 함수 포인터 typedef 에 붙이면 clang 은 무시 → 일반 함수에 붙인다.
- 크기 (Cortex-M4, 로그 13개): LOG_LEVEL 0→4 에서 `.text` 196→716 B, 문자열 0→366 B. 런타임 필터만 쓰면 784 + 366 B 가 항상 남는다. 호출 하나 ≈ 30~50 B + 문자열.
- `__FILE__` 은 넘긴 경로 그대로(절대 경로면 +121 B/파일) → `__FILE_NAME__` / `-fmacro-prefix-map`. `__func__` 도 함수마다 문자열.
- 인자 없는 로그: fmt 를 `__VA_ARGS__` 안에 넣으면 C11 `-Wpedantic` 에서도 경고 0 (`##__VA_ARGS__` 는 GNU, `__VA_OPT__` 는 C23).
- arm-none-eabi-gcc 의 `uint32_t` = `unsigned long` → `%u` 경고, `PRIu32` 사용. 타깃 툴체인으로도 `-Werror` 빌드.
- 싱크는 비차단: 링이 차면 줄 단위로 버리고 센다. 더 줄이려면 토큰화 로그(실측 printf 계열 약 3.1 KB 절약, nano 기준).

## 참고자료

- GCC Common Function Attributes — `format (archetype, string-index, first-to-check)`: <https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html>
- Clang Attributes Reference — `format`; Clang 언어 확장 `__FILE_NAME__`: <https://clang.llvm.org/docs/AttributeReference.html>, <https://clang.llvm.org/docs/LanguageExtensions.html>
- GCC Common Predefined Macros (`__FILE_NAME__`, GCC 12+), `-fmacro-prefix-map` / `-ffile-prefix-map`: <https://gcc.gnu.org/onlinedocs/cpp/Common-Predefined-Macros.html>, <https://gcc.gnu.org/onlinedocs/gcc/Preprocessor-Options.html>
- GCC Variadic Macros (`##__VA_ARGS__`, `__VA_OPT__`): <https://gcc.gnu.org/onlinedocs/cpp/Variadic-Macros.html>
- C11 (N1570) 6.10.3 매크로 치환(가변 인자 매크로), 6.4.2.2 `__func__`, 7.8.1 `<inttypes.h>` PRI 매크로: <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- Zephyr Logging (dictionary-based logging, `LOG_MODULE_REGISTER`): <https://docs.zephyrproject.org/latest/services/logging/index.html>
- Pigweed pw_tokenizer: <https://pigweed.dev/pw_tokenizer/>, trice: <https://github.com/rokath/trice>, defmt: <https://defmt.ferrous-systems.com/>
- Linux `include/linux/printk.h` (`pr_debug`, `no_printk`), dynamic debug: <https://docs.kernel.org/admin-guide/dynamic-debug-howto.html>
- 관련 레슨: TOOL-B01 (전처리 -E), TOOL-B02 (경고 플래그), TOOL-B04 (오브젝트 섹션), TOOL-B06 (size, printf 비용), TOOL-B07 (전처리기 빌드 변형), TOOL-M02 (맵 파일), TOOL-M03 (커스텀 섹션), TOOL-M13 (크기 다이어트), TOOL-A06 (재현 가능한 빌드), TOOL-A08 (스택 사용량), TOOL-M09 (데이터 레이스), C-M14 (ISR 공유 변수), RTOS-B10 (뮤텍스)
