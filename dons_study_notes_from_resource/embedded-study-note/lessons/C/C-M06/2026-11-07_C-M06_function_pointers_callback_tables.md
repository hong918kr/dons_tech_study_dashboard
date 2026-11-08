---
id: C-M06
time: 80분
updated: 2026-11-07
---
# C-M06 함수 포인터와 콜백 테이블

## 학습 목표

- `void (*signal(int, void (*)(int)))(int)` 같은 선언을 **오른쪽-왼쪽 규칙(right-left rule)** 으로 읽고, typedef 로 같은 타입을 다시 쓸 수 있다.
- 콜백 + `void *ctx`, 함수 포인터를 **반환하는** 함수, 함수 포인터 배열을 널 안전(NULL-safe)하게 쓴다.
- 문자열 명령 → 핸들러 **디스패치 테이블(dispatch table)** 로 디버그 셸을 만들고, argc/argv 파싱과 에러 코드를 설계한다. 표는 `const` 로 플래시(.rodata)에 둔다.
- Linux `file_operations` 스타일 `driver_ops {init, read, write, ioctl}` 로 서로 다른 두 디바이스를 같은 코드로 구동하고, 드라이버 전용 데이터를 `container_of` 또는 `void *priv` 로 되찾는다.
- 간접 호출의 **비용**(인라인·벡터화 불가, 포인터 재로드)과 **위험**(틀린 타입으로 호출 = UB, 널 호출, RAM 에 있는 함수 포인터)을 asm·실측으로 설명한다.

## 1. 문제

펌웨어에서 함수 포인터는 어디에나 있다 — Cortex-M 벡터 테이블, HAL 콜백(`HAL_UART_RxCpltCallback`), 드라이버 모델(Linux `file_operations`, Zephyr `dev->api`), UART 디버그 셸, 상태 머신 표(C-M07). 면접에서는 "signal 선언을 읽어 보세요", "콜백에 상태를 어떻게 넘기죠?", "C 로 다형성(polymorphism)을 어떻게 만들죠?" 가 단골이다.

함수 포인터 문법이 어려운 이유는 C 선언이 "쓰는 모양대로" 생겼기 때문이다. `int (*f)(int)` 는 "`(*f)(x)` 라고 쓰면 int 가 나온다" 는 뜻이다. 읽을 때는 **이름에서 시작해서 오른쪽 → 막히면 왼쪽 → 괄호 밖으로** 나간다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">C11 7.14.1.1 의 signal 선언을 오른쪽-왼쪽 규칙으로 읽기</text>
  <g stroke="currentColor">
    <rect x="16" y="42" width="42" height="28" rx="4" class="f-mut"/>
    <rect x="66" y="42" width="22" height="28" rx="4" class="f-warn"/>
    <rect x="96" y="42" width="62" height="28" rx="4" class="f-acc"/>
    <rect x="164" y="42" width="256" height="28" rx="4" class="f-ok"/>
    <rect x="436" y="42" width="52" height="28" rx="4" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="14">
    <text x="20" y="61">void</text>
    <text x="62" y="61">(</text><text x="72" y="61">*</text>
    <text x="100" y="61">signal</text>
    <text x="168" y="61">(int sig, void (*func)(int))</text>
    <text x="424" y="61">)</text>
    <text x="440" y="61">(int)</text>
    <text x="494" y="61">;</text>
  </g>
  <g fill="currentColor" font-weight="bold" text-anchor="middle">
    <text x="127" y="92">①</text><text x="292" y="92">②</text><text x="77" y="92">③</text>
    <text x="462" y="92">④</text><text x="37" y="92">⑤</text>
  </g>
  <g fill="currentColor">
    <text x="20" y="124">① 이름 signal 에서 시작한다.</text>
    <text x="20" y="146">② 오른쪽에 ( ) → "signal 은 (int, void(*)(int)) 를 받는 함수이고"</text>
    <text x="20" y="168">③ 닫는 괄호에 막힘 → 왼쪽 * → "포인터를 반환한다"</text>
    <text x="20" y="190">④ 괄호 밖 오른쪽 (int) → "그 포인터는 int 를 받는 함수를 가리키고"</text>
    <text x="20" y="212">⑤ 왼쪽 void → "그 함수는 void 를 반환한다"</text>
  </g>
  <rect x="10" y="228" width="720" height="62" rx="8" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace">
    <text x="22" y="252">typedef void (*sighandler_t)(int);              /* ③④⑤ 를 이름 하나로 */</text>
    <text x="22" y="276">sighandler_t signal(int sig, sighandler_t func); /* ①② 만 남는다 — 같은 타입 */</text>
  </g>
</svg>
```

호스트(맥)에서 돌아가는 한 파일 `starter.c` 에 세 Part 가 들어 있다. 실제 칩에서는 Part B 의 셸이 UART RX 줄 단위 버퍼를 받고, Part C 의 ops 가 진짜 레지스터를 만진다. 이 문제는 그 **구조** 만 떼어 내 테스트한다.

### Part A — 함수 포인터 문법: typedef, signal 모양, 콜백 + ctx

- **A1** `my_signal` — libc `signal()` 과 **똑같은 날것(raw) 선언** `static void (*my_signal(int sig, void (*handler)(int)))(int)` 으로 정의된 함수의 몸통. 8칸짜리 "벡터 테이블" `g_vec[]` 에 핸들러를 등록하고 **이전 핸들러** 를 반환한다. 범위 밖이면 `MY_SIG_ERR`.
- **A2** `my_raise(sig)` — 등록된 핸들러를 호출(1), 없으면(널) 호출하지 말고 `g_unhandled++` 후 0, 범위 밖이면 -1.
- **A3** `pick_op(char)` — **함수 포인터를 반환하는 함수** `int32_t (*pick_op(char op))(int32_t, int32_t)`. 주어진 `k_binop_sym = "+-*/"` 과 `k_binops[]` 표로 찾고, 없으면 NULL.
- **A4** `for_each_u8(buf, n, visit, ctx)` — 각 바이트로 `visit(byte, ctx)` 를 부르는 반복자. 콜백이 false 를 반환하면 멈춘다. 반환은 콜백 호출 횟수. `visit == NULL` 이면 0.

테스트는 `_Generic` 으로 "날것 선언과 typedef 선언이 **같은 타입**" 임을 컴파일 타임에 대조한다.

### Part B — 디버그 셸: 문자열 명령 → 핸들러 디스패치 테이블

주어진 표 (모든 핸들러는 같은 시그니처 `int (*)(int argc, char *argv[], void *ctx)`):

| name | fn | argc 범위 | 하는 일 |
|---|---|---|---|
| `help` | `cmd_help` | 1..1 | 명령 이름 나열 |
| `peek` | `cmd_peek` | 2..2 | `peek <reg>` → `R1=0x00000010` |
| `poke` | `cmd_poke` | 3..3 | `poke <reg> <val>` (0x / 10진 / 8진) |
| `echo` | `cmd_echo` | 1..6 | 인자를 공백 하나로 이어 출력 |
| `reboot` | **NULL** | 1..1 | 자리만 있고 아직 구현 안 됨 |

- **B1** `shell_tokenize(line, argv, max)` — 줄을 **제자리에서(in place)** 자른다. 구분자 `' ' '\t' '\r' '\n'`, 연속 구분자는 하나. `argv[argc] = NULL`. 토큰이 `max` 를 넘으면 `SH_ERR_TOOMANY`.
- **B2** `shell_find(tbl, n, name)` — 정확히 일치(대소문자 구분, 접두사 일치 금지). `tbl == NULL` 이어도 안전.
- **B3** `shell_exec(tbl, n, line, ctx)` — 검사 순서: 인자 NULL(`SH_ERR_INVAL`) → 토큰화 → 빈 줄이면 `SH_OK`(아무것도 호출 안 함) → 찾기(`SH_ERR_UNKNOWN`) → argc 범위(`SH_ERR_ARGC`) → `fn == NULL`(`SH_ERR_NOFN`) → 핸들러 반환값.

### Part C — driver_ops: 같은 인터페이스, 다른 디바이스

```c
struct driver_ops {                 /* Linux struct file_operations 의 축소판 */
    int (*init)(struct device *dev);
    int (*read)(struct device *dev, uint8_t *buf, size_t len);
    int (*write)(struct device *dev, const uint8_t *buf, size_t len);
    int (*ioctl)(struct device *dev, unsigned cmd, uintptr_t arg);   /* 선택: NULL 가능 */
};
struct device { const char *name; const struct driver_ops *ops; void *priv; bool ready; };
```

- **C1** 공통 계층 `dev_init / dev_read / dev_write / dev_ioctl` — Linux `vfs_read` 자리. 널 디바이스, 드라이버 없음(`ops == NULL`), 그 op 없음(NULL), 초기화 전, `len == 0` 을 정해진 순서로 거른 뒤 드라이버 op 를 부른다.
- **C2** `uart0` — 8바이트 **링버퍼 루프백**. `write` 는 넣을 수 있는 만큼 넣고 그 수를 반환(하나도 못 넣으면 `-EAGAIN`), `read` 는 꺼낸다. `struct device` 를 **품은(embed)** `uart_dev_t` 를 `CONTAINER_OF` 로 되찾는다 (일부러 첫 멤버가 아니다).
- **C3** `eeprom0` — 32바이트, 지워진 상태 0xFF, 파일처럼 **커서(pos)** 가 있다. 끝에서 잘리는 write, `-ENOSPC`, 쓰기 보호 `-EPERM`, `ioctl(SEEK / WP)`. 전용 데이터는 `dev->priv` (**void \*ctx 방식**).

주어진 `led0` 는 read/ioctl 이 없는 쓰기 전용 디바이스다. 상위 코드 `dev_puts()` 는 어떤 디바이스인지 모른 채 세 디바이스 모두에 쓴다.

## 2. Problem (English)

> **Function pointers, callback tables, and an ops-based driver model.**
>
> **Part A.** Using only the raw declarator syntax in the definitions, implement:
> `void (*my_signal(int sig, void (*handler)(int)))(int)` — install `handler` for `sig` in an 8-entry table and return the previous handler (`NULL` if none), or `MY_SIG_ERR` if `sig` is out of range;
> `int my_raise(int sig)` — call the installed handler (return 1), or count an unhandled signal without calling through a null pointer (return 0), or return -1 if out of range;
> `int32_t (*pick_op(char op))(int32_t, int32_t)` — return the function for `'+' '-' '*' '/'` from a given table, `NULL` otherwise (including `'\0'`);
> `size_t for_each_u8(const uint8_t *buf, size_t n, bool (*visit)(uint8_t, void *), void *ctx)` — call `visit` per byte, stop early when it returns false, return the number of calls.
>
> **Part B.** Implement an in-place tokenizer (`argv[argc] == NULL`, error if more than `max` tokens), an exact-match lookup in a `const` command table, and `shell_exec()` that returns `SH_OK` for blank lines, `SH_ERR_UNKNOWN`, `SH_ERR_ARGC` (outside `[min_argc, max_argc]`), `SH_ERR_NOFN` (entry with a null handler), or the handler's return value.
>
> **Part C.** Implement the generic `dev_*` layer with checks in this order: `EINVAL` (null device, or null buffer with `len > 0`) → `ENODEV` (no ops) → `ENOTSUP` (op missing) → `ENXIO` (not initialized) → `len == 0` returns 0 → driver op. Implement a loopback UART (8-byte ring buffer, partial writes, `-EAGAIN` when full) that recovers its private struct with `container_of`, and a 32-byte EEPROM with a file-like cursor, truncating writes, `-ENOSPC`, write-protect `-EPERM`, and `ioctl` (`SEEK`, `WP`, unknown → `-ENOTTY`) that keeps its state in `void *priv`.
>
> Then discuss: what does an indirect call cost compared to a direct call? Why should ops/command tables be `const`? Is calling a function through a pointer of a different function type allowed?
>
> Examples: `shell_exec(..., "  peek   1  \r\n", ...)` → `SH_OK`, out `"R1=0x00000010"`; `"pok 1"` → `SH_ERR_UNKNOWN`; `dev_write(uart, "0123456789", 10)` → 8, next write → `-EAGAIN`; EEPROM at pos 30, `write("WXYZ")` → 2, next write → `-ENOSPC`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 줄과 "18/74 passed" — TODO(A1–A4, B1–B3, C1–C3) 를 채워 74/74 를 만든다
```

관찰용 파일 네 개 (하네스와 별개, 결과는 5. 풀이에):

```bash
clang -std=c11 -O2 -S demo_vtable.c -o -                                              # 직접 vs 간접 호출 asm
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_vtable.c -o -   # Cortex-M4
clang -std=c11 -O2 -c demo_sections.c -o /tmp/sec.o && nm -m /tmp/sec.o               # 표가 놓이는 섹션
cc -std=c11 -O2 demo_bench.c -o /tmp/bench && /tmp/bench                               # 호출 비용 실측
cc -std=c11 -Wall -Wextra -Wcast-function-type -O2 demo_wrong_type.c -o /tmp/wt && /tmp/wt   # 틀린 타입 호출 (UB)
```

## 4. 힌트

<details>
<summary>힌트 1 — A1: 몸통은 평범하다, 무서운 건 선언뿐</summary>

선언이 어떻게 생겼든 함수 안에서 `handler` 는 그냥 `sig_handler_t` 타입 변수다. `old = g_vec[sig]; g_vec[sig] = handler; return old;` — 범위 검사를 먼저. 반환 타입도 `sig_handler_t` 와 같으므로 `MY_SIG_ERR`, `NULL`, `old` 를 그대로 return 하면 된다.

</details>

<details>
<summary>힌트 2 — A3: strchr 함정</summary>

`strchr("+-*/", op)` 로 위치를 찾으면 편하지만, `strchr` 은 종료 문자 `'\0'` 도 문자열의 일부로 보고 찾아 준다 (C11 7.24.5.2). `op == '\0'` 이면 인덱스 4 → `k_binops[4]` 범위 밖. 표 길이만큼 직접 돌거나 `op != '\0'` 을 먼저 검사하라.

</details>

<details>
<summary>힌트 3 — B1: 읽기 포인터 하나, '\0' 로 자르기</summary>

`p` 로 앞쪽 구분자를 건너뛰고 → `'\0'` 이면 끝 → 토큰 수 검사 → `argv[argc++] = p` → 구분자나 끝까지 전진 → 구분자 자리에 `'\0'` 을 쓰고 한 칸 더. 복사하지 않으므로 `argv[i]` 는 `line` 안의 주소다. 마지막에 `argv[argc] = NULL` (배열 크기가 `max + 1` 인 이유).

</details>

<details>
<summary>힌트 4 — B3·C1: "검사 → 호출" 순서가 곧 스펙</summary>

함수 포인터를 부르기 직전까지 모든 실패 경로를 **반환 코드로** 처리한다. 특히 `cmd->fn == NULL`, `d->ops == NULL`, `d->ops->read == NULL` — 이것들을 빼먹으면 호스트에선 SIGSEGV, Cortex-M 에선 HardFault 다. argc 범위 검사가 디스패처에 있으면 핸들러는 `argv[1]` 이 존재한다고 믿고 쓸 수 있다.

</details>

<details>
<summary>힌트 5 — C2: 자유 증가 인덱스 링버퍼</summary>

`head`, `tail` 을 감싸지 않고 계속 증가시키면 "들어 있는 양" 은 `head - tail` (unsigned 뺄셈은 감아 돌아도 맞다), 인덱스는 `head & (SIZE - 1)`. 가득 참 = `head - tail == SIZE`, 비었음 = `head == tail`. 드라이버 함수 첫 줄은 `uart_dev_t *u = to_uart(d);`.

</details>

## 5. 풀이

### Part A — 선언 읽기, 함수 지정자, 콜백

**오른쪽-왼쪽 규칙** 으로 자주 나오는 선언들:

| 선언 | 읽기 | typedef 로 |
|---|---|---|
| `int *f(int)` | f 는 int 를 받아 **int 포인터** 를 반환하는 함수 | — |
| `int (*f)(int)` | f 는 int→int 함수를 가리키는 **포인터** | `typedef int (*fn_t)(int); fn_t f;` |
| `int (*a[4])(int)` | a 는 int→int 함수 포인터 4개짜리 **배열** | `fn_t a[4];` |
| `int (*const a[4])(int)` | 위 배열의 원소(포인터)가 const → 통째로 .rodata | `fn_t const a[4];` |
| `int (*(*p)[4])(int)` | p 는 "함수 포인터 4개짜리 배열" 을 가리키는 포인터 | `fn_t (*p)[4];` |
| `int (*pick(char))(int, int)` | pick 은 char 를 받아 (int,int)→int 함수 포인터를 반환 | `binop_fn pick(char);` |
| `void (*signal(int, void (*)(int)))(int)` | 그림 참고 | `sighandler_t signal(int, sighandler_t);` |
| `const int (*f)(int)` | 반환 타입이 const int — 의미 없는 한정자 (const 포인터 아님!) | — |

typedef 를 **함수 타입** 으로 두는 방법도 있다: `typedef void sig_fn(int); sig_fn *h;` — 포인터 별표가 사용처에 보여서 "이건 포인터다" 가 드러난다 (Linux 커널 일부 코드 스타일). 둘 다 같은 타입이다.

**함수 지정자(function designator)** 는 `sizeof`·`&` 의 피연산자가 아닐 때 자동으로 함수 포인터로 바뀐다 (C11 6.3.2.1p4). 그리고 함수 호출 연산자 `()` 는 원래 **함수 포인터** 를 피연산자로 받는다 (6.5.2.2p1). 그래서:

| 식 | 의미 |
|---|---|
| `h = on_sig;` 와 `h = &on_sig;` | 같다 |
| `h(3)`, `(*h)(3)`, `(**h)(3)` | 같다 — `*h` 는 함수 지정자가 되고 곧바로 다시 포인터로 바뀐다 |
| `pick_op('+')(40, 2)` | 반환된 포인터를 바로 호출. **널이면 UB** → 테스트는 받은 뒤 검사 |

`my_signal` 몸통은 평범하다:

```c
static void (*my_signal(int sig, void (*handler)(int)))(int) {
    if (sig < 0 || sig >= NSIG_SIM) return MY_SIG_ERR;
    sig_handler_t old = g_vec[sig];
    g_vec[sig] = handler;
    return old;                 /* "이전 핸들러" 반환 → 호출자가 나중에 복원할 수 있다 */
}
```

`MY_SIG_ERR` 은 **센티넬 함수의 주소** 로 만들었다. libc 의 `SIG_ERR` 은 보통 `((void (*)(int))-1)` 인데, 정수 → 포인터 변환은 **구현 정의(implementation-defined)** 다 (6.3.2.3p5). 라이브러리는 자기 플랫폼을 알기 때문에 그렇게 해도 되지만, 이식성 있는 사용자 코드라면 "절대 진짜 핸들러가 될 수 없는 고유 주소" 가 더 깔끔하다. 함수 포인터끼리는 `==` 로 비교할 수 있고, 같은 함수를 가리키거나 둘 다 널일 때만 같다 (6.5.9p6).

`my_raise` 의 핵심은 **널을 부르지 않는 것** 이다. `g_vec[]` 는 정적 저장 기간이라 초기값이 널 포인터다 (6.7.9p10). "핸들러가 없으면 기본 동작" 은 `signal` 의 `SIG_DFL` 과 같은 생각이다.

`pick_op` 의 함정은 힌트 2 의 `strchr` 이다. 테스트 `pick_op('\0') == NULL` 이 그걸 잡는다. 정답은 표 길이만큼 도는 선형 탐색:

```c
for (size_t i = 0; i < sizeof k_binops / sizeof k_binops[0]; i++)
    if (k_binop_sym[i] == op) return k_binops[i];
return NULL;
```

**콜백 + `void *ctx`**. `for_each_u8` 는 "무엇을 할지" 는 함수 포인터로, "누구의 상태로 할지" 는 `ctx` 로 받는다. 같은 반복자에 `sum_cb` (ctx = `uint32_t *`) 와 `find_cb` (ctx = `find_ctx_t *`) 를 넘겨 쓴다. `void *` ↔ 객체 포인터 변환은 왕복 시 값이 보존되므로 (6.3.2.3p1) 콜백 안에서 `find_ctx_t *f = ctx;` 로 되찾는 것은 합법이다. ctx 가 없는 API 의 대표가 `qsort` — 비교 함수가 상태를 받을 수 없어 전역 변수를 쓰게 된다. 그래서 `qsort_r` (glibc/BSD, 인자 순서가 서로 다름!) 과 C11 Annex K `qsort_s` 가 생겼다 (추가 연습 5).

### Part B — 디스패치 테이블

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">shell_exec(k_cmds, N, "  poke 1 0x10\r\n", &amp;ctx) 의 흐름</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="250" height="34" rx="6" class="f-mut"/>
    <rect x="10" y="92" width="250" height="34" rx="6" class="f-mut"/>
    <rect x="10" y="144" width="250" height="34" rx="6" class="f-mut"/>
    <rect x="10" y="196" width="250" height="34" rx="6" class="f-mut"/>
    <rect x="10" y="248" width="250" height="34" rx="6" class="f-mut"/>
    <rect x="10" y="290" width="290" height="34" rx="6" class="f-ok"/>
    <rect x="300" y="40" width="160" height="34" rx="6" class="f-warn"/>
    <rect x="300" y="92" width="160" height="34" rx="6" class="f-ok"/>
    <rect x="300" y="144" width="160" height="34" rx="6" class="f-warn"/>
    <rect x="300" y="196" width="160" height="34" rx="6" class="f-warn"/>
    <rect x="300" y="248" width="160" height="34" rx="6" class="f-warn"/>
    <rect x="490" y="40" width="240" height="190" rx="8" class="f-acc"/>
  </g>
  <g fill="currentColor">
    <text x="20" y="62">① tokenize → argc=3 (제자리 '\0')</text>
    <text x="20" y="114">② argc == 0 (빈 줄)?</text>
    <text x="20" y="166">③ shell_find("poke") 정확히 일치</text>
    <text x="20" y="218">④ min_argc ≤ argc ≤ max_argc ?</text>
    <text x="20" y="270">⑤ cmd-&gt;fn == NULL ?</text>
    <text x="20" y="312">⑥ return cmd-&gt;fn(argc, argv, ctx)</text>
    <text x="310" y="62">토큰 &gt; 6 → TOOMANY</text>
    <text x="310" y="114">예 → SH_OK (호출 X)</text>
    <text x="310" y="166">없음 → UNKNOWN</text>
    <text x="310" y="218">아니오 → ARGC</text>
    <text x="310" y="270">예 → NOFN</text>
    <text x="500" y="62" font-weight="bold">const shell_cmd_t k_cmds[]</text>
    <text x="500" y="80" font-size="12">→ ELF .rodata (플래시), RAM 0 B</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="500" y="106">"help"   cmd_help  1 1</text>
    <text x="500" y="126">"peek"   cmd_peek  2 2</text>
    <text x="500" y="146">"poke"   cmd_poke  3 3  ◀</text>
    <text x="500" y="166">"echo"   cmd_echo  1 6</text>
    <text x="500" y="186">"reboot" NULL      1 1</text>
    <text x="500" y="212">argv: "poke" "1" "0x10" NULL</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M260 57 H300"/><path d="M260 109 H300"/><path d="M260 161 H300"/>
    <path d="M260 213 H300"/><path d="M260 265 H300"/>
    <path d="M135 74 V92"/><path d="M135 126 V144"/><path d="M135 178 V196"/>
    <path d="M135 230 V248"/><path d="M135 282 V290"/>
  </g>
</svg>
```

**토크나이저.** 복사 없이 원래 버퍼에 `'\0'` 을 박아 넣는다 — RAM 이 귀한 MCU 셸의 표준 방식이고, 그래서 `line` 이 `char *` (쓰기 가능) 여야 한다. 문자열 리터럴을 넘기면 UB (6.4.5p7) — 테스트가 매번 `strcpy` 로 스택 버퍼에 복사하는 이유다. `argv[argc] = NULL` 은 C 의 `main` 과 같은 약속(5.1.2.2.1p2)이라 핸들러가 `for (char **a = argv; *a; a++)` 처럼 돌 수도 있다. `strtok` 은 내부 정적 상태 때문에 재진입이 안 된다 — 셸이 UART 와 USB 두 개거나 RTOS 태스크 두 개면 깨진다 (C-A04).

경계 케이스와 기대값 (테스트의 표 그대로):

| 입력 | 결과 | 이유 |
|---|---|---|
| `""`, `"   \t \r\n"` | `SH_OK`, 호출 없음 | 터미널에서 엔터만 친 경우 — 에러가 아니다 |
| `"  peek   1  \r\n"` | `R1=0x...` | 앞뒤·연속 공백, 터미널의 CR LF |
| `"peek"`, `"peek 1 2"`, `"help me"` | `SH_ERR_ARGC` | 디스패처가 막으므로 핸들러는 `argv[1]` 을 믿는다 |
| `"pee 1"`, `"peek1"`, `"PEEK 1"` | `SH_ERR_UNKNOWN` | 접두사·대소문자 일치 금지 |
| `"poke 9 1"`, `"poke 1 zz"`, `"poke 1 -1"`, `"poke 1 4294967296"` | `SH_ERR_BADVAL` | 핸들러 몫의 검증. `strtoul("-1")` 은 에러 없이 `ULONG_MAX` 를 준다 (7.22.1.4p5 — 부호 부정을 결과 타입에서 수행) |
| `"echo 1 2 3 4 5 6"` | `SH_ERR_TOOMANY` | 7 토큰 > 6 |
| `"reboot"` | `SH_ERR_NOFN` | 표에 자리만 있는 명령 — 널 호출 대신 에러 |

마지막 검사로 테스트는 `ctx.calls == 13` (성공 9 + BADVAL 4) 을 본다: 에러 경로에서는 **핸들러가 한 번도 불리지 않았음** 을 확인하는 것이다.

**모든 핸들러가 같은 시그니처.** `cmd_peek` 이 argc 를 안 써도 `(int argc, char *argv[], void *ctx)` 를 받는다. 표에 넣으려고 다른 시그니처의 함수를 캐스트하면 그 호출은 UB 다 (아래 "안전성").

**표는 어느 메모리에 놓이나** — `demo_sections.c` 를 세 타깃으로 컴파일한 **실제 결과** (`nm -m`, `llvm-objdump -t`):

| 선언 | macOS arm64 .o | thumbv7em (bare-metal ELF) | aarch64-linux `-fPIE` |
|---|---|---|---|
| `const struct cmd k_cmds_const[]` | `(__DATA,__const)` | `.rodata` | `.data.rel.ro` |
| `struct cmd g_cmds_rw[]` (const 빠짐) | `(__DATA,__data)` | `.data` | `.data` |
| `const char *g_names_ptr_rw[]` | `(__DATA,__data)` | `.data` | `.data` |
| `const char *const k_names_ptr_const[]` | `(__DATA,__const)` | `.rodata` | `.data.rel.ro` |

표 안에 **포인터**(이름 문자열 주소, 함수 주소)가 있다는 점이 열쇠다. ELF 오브젝트에는 이 자리마다 재배치(relocation)가 붙어 있다:

```text
RELOCATION RECORDS FOR [.rodata]:
00000000 R_ARM_ABS32              .rodata.str1.1
00000004 R_ARM_ABS32              c_peek
00000008 R_ARM_ABS32              .rodata.str1.1
0000000c R_ARM_ABS32              c_poke
```

- **bare-metal**: 링커가 링크 타임에 절대 주소를 채워 끝낸다 → 실행 중 쓸 일이 없으니 `.rodata` = 플래시. `.data` 로 가면 플래시(초기값) **+** RAM(실행 사본) 두 벌을 쓰고, 부팅 시 startup 코드가 복사한다. R_ARM_ABS32 는 Thumb 함수 심볼이면 **bit0(T 비트)를 1로** 채운다 (AAELF: `(S + A) | T`) — Cortex-M 에서 함수 포인터 값이 항상 홀수인 이유.
- **PIE/ASLR (Linux, macOS)**: 로드 주소를 실행 때 알기 때문에 로더가 포인터를 써야 한다 → 순수 읽기 전용 영역에 못 두고 "**쓴 다음 읽기 전용으로 잠그는**" 영역에 둔다 (`.data.rel.ro` + RELRO, Mach-O 는 링크 후 `__DATA_CONST`). 그래도 `const` 를 쓰는 의미는 같다 — 로딩 후엔 덮어쓸 수 없다.
- `const char *names[]` 는 **문자만** const 라 배열 자체는 `.data` 다. 표를 통째로 잠그려면 `const char *const` (C-B08).

**탐색 속도.** 명령 수십 개면 선형 `strcmp` 로 충분하다 (사람이 타이핑하는 속도에 비하면 0초). 많아지면 이름순 정렬 표 + `bsearch` (추가 연습 2), 부팅 때 정렬 여부를 한 번 검사.

### Part C — driver_ops 와 전용 데이터

```svg
<svg viewBox="0 0 740 366" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">하나의 인터페이스, 전용 데이터 두 방식 (위: RAM, 아래: 플래시)</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="300" height="140" rx="8" class="f-mut"/>
    <rect x="30" y="112" width="260" height="58" rx="6" class="f-acc"/>
    <rect x="400" y="40" width="160" height="104" rx="8" class="f-acc"/>
    <rect x="590" y="40" width="140" height="104" rx="8" class="f-mut"/>
    <rect x="10" y="222" width="300" height="96" rx="8" class="f-ok"/>
    <rect x="400" y="222" width="330" height="96" rx="8" class="f-ok"/>
  </g>
  <g fill="currentColor" font-weight="bold">
    <text x="20" y="60">uart_dev_t uart0 (RAM)</text>
    <text x="410" y="60">device ee0 (RAM)</text>
    <text x="598" y="60">ee_priv_t (RAM)</text>
    <text x="20" y="242">const driver_ops uart_ops (.rodata)</text>
    <text x="410" y="242">const driver_ops ee_ops (.rodata)</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="24" y="80">rb[8]  head  tail  tx_total</text>
    <text x="24" y="100">← offsetof(dev): 맥 24, Cortex-M 20</text>
    <text x="40" y="132">dev: name="uart0"  ops ●</text>
    <text x="40" y="150">     priv=NULL  ready</text>
    <text x="410" y="80">name="eeprom0"</text>
    <text x="410" y="98">ops  ●</text>
    <text x="410" y="116">priv ●</text>
    <text x="410" y="134">ready</text>
    <text x="598" y="84">mem[32]</text>
    <text x="598" y="104">pos  wp</text>
    <text x="20" y="264">.init=uart_init .read=uart_read</text>
    <text x="20" y="284">.write=uart_write</text>
    <text x="20" y="304">.ioctl=NULL  → dev_ioctl: -ENOTSUP</text>
    <text x="410" y="264">.init=ee_init   .read=ee_read</text>
    <text x="410" y="284">.write=ee_write .ioctl=ee_ioctl</text>
    <text x="410" y="304">(디바이스 N 개가 표 1 개를 공유)</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <path d="M200 128 C 220 190, 160 190, 160 222"/>
    <path d="M460 94 C 480 170, 540 170, 560 222"/>
    <path d="M460 112 C 520 112, 560 92, 590 92"/>
    <path d="M30 140 C 0 120, 0 90, 20 96" stroke-dasharray="4 3"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="340">점선: to_uart(d) = (char *)d − offsetof(uart_dev_t, dev) → 바깥 구조체 uart0</text>
    <text x="10" y="358">ee0: e = d-&gt;priv (void * → ee_priv_t *).  실선: 포인터</text>
  </g>
</svg>
```

**두 가지 전용 데이터 방식** 비교:

| | 방법 1: embed + `container_of` (uart) | 방법 2: `void *priv` (eeprom) |
|---|---|---|
| 모양 | `uart_dev_t { ...; struct device dev; }` | `struct device { ...; void *priv; }` |
| 되찾기 | `(char *)d - offsetof(uart_dev_t, dev)` — 덧셈 하나, 상수 | 포인터 로드 하나 |
| 메모리 | 한 덩어리, 포인터 추가 없음 | 포인터 1개 + 별도 객체 |
| 실수 시 | 다른 타입의 device 에 쓰면 엉뚱한 메모리 (타입 검사 매크로로 일부 방어, C-A06) | `priv` 를 안 달면 NULL → `ee_init` 이 `-ENODEV` 로 거른다 (테스트 `ee_bad`) |
| 실제 예 | Linux `container_of(inode->i_cdev, struct my_dev, cdev)`, 커널 전반 | Linux `file->private_data`, Zephyr `dev->data`, HAL 의 `huart->pUserData` 류 |

`offsetof(uart_dev_t, dev) != 0` 을 테스트에서 확인하는 이유: `dev` 가 첫 멤버면 단순 캐스트로도 "우연히" 동작해서, 빼기가 틀린 `container_of` 를 잡지 못한다. 첫 멤버 캐스트 자체는 표준이 보장하는 기법이다 (6.7.2.1p15 — 구조체 포인터를 적절히 변환하면 첫 멤버를 가리킨다) — C 의 "상속" 흉내.

**공통 계층의 검사 순서** 는 다음과 같이 고정했다 (Linux `vfs_read` 도 비슷하게 `f_op` 의 존재를 보고 에러를 돌려준다 — 거기선 `-EINVAL`):

| 순서 | 조건 | 반환 | 테스트 |
|---|---|---|---|
| 1 | `d == NULL`, `buf == NULL && len > 0` | `-EINVAL` | `dev_read(NULL,..)`, `dev_write(&led0, NULL, 3)` |
| 2 | `d->ops == NULL` | `-ENODEV` | `orphan` (드라이버가 안 붙은 디바이스) |
| 3 | 해당 op 가 NULL | `-ENOTSUP` | `led0` read, `uart0` ioctl |
| 4 | `!d->ready` | `-ENXIO` | init 전, init 실패한 `ee_bad` |
| 5 | `len == 0` | 0, 드라이버 안 부름 | `dev_write(&led0, NULL, 0)` |

그래서 드라이버 함수(`uart_write`, `led_write`...)는 "d 는 유효, 초기화됨, buf 유효, len ≥ 1" 을 **전제** 로 짧게 쓸 수 있다. 검사를 한 곳에 모으는 것이 ops 패턴의 또 다른 이득이다.

**UART 링버퍼** — 자유 증가 인덱스:

```c
while (n < len && u->head - u->tail < UART_RB_SIZE)
    u->rb[u->head++ & (UART_RB_SIZE - 1u)] = buf[n++];
if (n == 0) return DEV_EAGAIN;            /* 논블로킹 write: 하나도 못 넣음 */
```

`"0123456789"` 10바이트 → 8 반환 (부분 쓰기 = POSIX write 와 같은 약속, 호출자가 나머지를 다시 보냄), 다음 1바이트 → `-EAGAIN`, 3바이트 읽고 2바이트 쓰면 인덱스가 감아 돌아(wrap) `"34567ab"` 가 나온다. `head - tail` 은 `uint32_t` 뺄셈이라 2^32 에서 넘쳐도 맞다 (unsigned 는 모듈러, 6.2.5p9). 링버퍼 자체의 깊은 이야기(ISR 생산자/메인 소비자, 메모리 순서)는 DS 트랙 몫이다.

**EEPROM** 은 파일처럼 커서가 있다: pos 30 에서 `"WXYZ"` → 2 (잘림), 다시 쓰면 `-ENOSPC`, 읽으면 0 (EOF), `SEEK 33` → `-EINVAL`, `WP=1` 후 쓰기 → `-EPERM`, 모르는 ioctl → `-ENOTTY` (Linux 관례 — "inappropriate ioctl for device").

**Linux 와의 대응**:

```c
static const struct file_operations my_fops = {     /* 커널 드라이버의 실제 모양 */
    .owner          = THIS_MODULE,
    .open           = my_open,     /* container_of(inode->i_cdev, ...) → file->private_data */
    .read           = my_read,     /* ssize_t (*)(struct file *, char __user *, size_t, loff_t *) */
    .write          = my_write,
    .unlocked_ioctl = my_ioctl,
};
```

- 지정 초기자(designated initializer, 6.7.9p7)로 **쓰는 op 만** 채운다. 빠진 멤버는 정적 객체처럼 0 = 널 포인터로 초기화된다 (6.7.9p19 → p10). 구조체에 op 가 추가돼도 기존 드라이버 코드가 그대로 컴파일된다.
- Zephyr: `struct device { const char *name; const void *config; const void *api; ...; void *data; }`. `uart_poll_out(dev, c)` 같은 inline 래퍼가 `((const struct uart_driver_api *)dev->api)->poll_out(dev, c)` 를 부른다 — 이 레슨의 `dev_write` 와 같은 자리.
- C++ 로 보면 `struct driver_ops` = vtable, `dev->ops` = vptr. C 는 이걸 손으로 쓰는 대신 **op 를 선택적(NULL)** 으로 둘 수 있다.

### vtable 비용 — asm 과 실측

`demo_vtable.c` 를 `clang -O2 -S` (Apple clang 21.0.0, arm64) 로 컴파일한 **실제 출력** (지시어 생략):

```text
_call_direct:            ; twice(x) + 1
	mov	w8, #1
	orr	w0, w8, w0, lsl #1          ; 인라인 + 상수 접기: (x<<1)|1
	ret
_call_const_table:       ; k_ops.xform(x) + 1  (static const 표)
	mov	w8, #1
	orr	w0, w8, w0, lsl #1          ; 탈가상화(devirtualization) → 직접 호출과 똑같다
	ret
_call_rw_table:          ; g_ops_rw.xform(x) + 1  (외부 연결, non-const)
	stp	x29, x30, [sp, #-16]!
	adrp	x8, _g_ops_rw@PAGE
	ldr	x8, [x8, _g_ops_rw@PAGEOFF] ; 표에서 포인터 로드
	blr	x8                          ; 간접 호출
	add	w0, w0, #1
	ldp	x29, x30, [sp], #16
	ret
_call_param:             ; o->xform(x) + 1
	ldr	x8, [x0]
	mov	x0, x1
	blr	x8
```

루프에서 차이가 커진다:

```text
_sum_direct:   LBB5_7: ldp q4,q5,[x8,#-32] ; ldp q6,q7,[x8],#64
                       add.4s v4,v4,v4 ... add.4s v0,v4,v0 ...   ; NEON 으로 16개씩 (twice 인라인)
_sum_param:    LBB6_2: ldr  x8, [x20]       ; ← 매 반복 함수 포인터를 다시 읽는다
                       ldr  w0, [x19], #4
                       blr  x8              ; 원소마다 호출
                       add  w21, w0, w21
                       subs x22, x22, #1 ; b.ne LBB6_2
```

`sum_param` 에서 `o->xform` 을 **매 반복 다시 읽는** 이유: `o` 가 `const struct ops *` 여도 그건 "이 경로로는 안 쓴다" 일 뿐, 호출된 함수가 다른 경로로 표를 바꿀 수 있으므로 컴파일러는 값을 캐시하지 못한다. 그리고 호출 때문에 callee-saved 레지스터 저장(`stp x22,x21 ...`)이 늘었다.

Cortex-M4 (`--target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2`, 실제 출력):

```text
call_const_table:  lsls r0, r0, #1 ; adds r0, #1 ; bx lr           ← 역시 인라인
call_param:        push {r7, lr} ; ldr r2, [r0] ; mov r0, r1 ; blx r2 ; adds r0, #1 ; pop {r7, pc}
sum_param loop:    ldr r1, [r6] ; ldr r0, [r7, #4]! ; blx r1 ; subs r4, #1 ; add r5, r0 ; bne
sum_direct loop:   4개씩 펼치고 add.w r0, r0, r4, lsl #1           ← 호출 없음
```

**실측** (`demo_bench.c`, Apple M6, `-O2`, 약 2억 회, 3라운드 실제 출력):

```text
round 0: inline 0.058 ns/elem | direct call 0.706 ns/call | indirect (ops) 0.726 ns/call  [sums equal]
round 1: inline 0.061 ns/elem | direct call 0.563 ns/call | indirect (ops) 0.697 ns/call  [sums equal]
round 2: inline 0.062 ns/elem | direct call 0.607 ns/call | indirect (ops) 0.573 ns/call  [sums equal]
```

| 비교 | 결과 | 해석 |
|---|---|---|
| 직접 호출(noinline) vs 간접 호출 | 0.56–0.71 vs 0.57–0.73 ns — **측정 오차 안** | 예측이 맞는 간접 분기는 직접 호출과 거의 같다 (항상 같은 대상이라 분기 예측기가 맞춘다) |
| 인라인 vs 호출 | 0.06 vs ~0.6 ns — **약 10배** | 비용의 본체는 "간접" 이 아니라 **인라인·벡터화를 잃는 것** |

Cortex-M 에서는: M3/M4 에는 동적 분기 예측기가 없어 `BL` 과 `BLX Rm` 모두 파이프라인 재충전 비용(TRM 기준 1+P, P = 1~3 사이클)을 내고, 간접 호출은 여기에 표에서 포인터를 읽는 `LDR` (플래시 대기 상태(wait state) 포함)이 더해진다. 셸 명령, 드라이버 open/read 처럼 **드물게 부르는 경로** 에선 무시할 비용이고, 샘플마다 부르는 DSP 내부 루프·고속 ISR 에선 피한다. 같은 대상만 부른다면 `static const` 표 + 같은 번역 단위(또는 LTO)로 컴파일러가 탈가상화하게 둔다.

### 함수 포인터 안전성 — 틀린 타입으로 호출하면 UB

C11 6.3.2.3p8: 함수 포인터는 다른 함수 포인터 타입으로 **변환했다가 되돌릴 수 있고** 원래 값과 같다. 하지만 **변환된 포인터로, 가리키는 함수와 호환되지 않는 타입으로 호출하면 UB** 다. 호환(6.7.6.3p15)은 반환 타입 호환 + 매개변수 개수·타입 각각 호환이다. `struct uart *` 와 `void *` 는 호환 타입이 **아니다**.

`demo_wrong_type.c` 실제 출력 (`-O2`):

```text
1) call void(int) through void(*)(void):
  handler_int got x = 10               ← 인자 레지스터 w0 에 남아 있던 쓰레기
2) cast back to void(*)(int) and call:
  handler_int got x = 42               ← 되돌려 캐스트 후 호출: 합법
3) int(struct uart*) via int(*)(void*): 70   ← 같은 ABI 라 "우연히" 동작 (그래도 UB)
4) int(void*) callback, converts inside: 70  ← 올바른 방법
```

- 경고: `-Wall -Wextra` 로는 **아무 경고도 안 나왔다**. `-Wcast-function-type` 을 켜면 이 clang 에서 네 캐스트 모두 `converts to incompatible function type [-Wcast-function-type-strict]` 가 나왔다. 펌웨어 빌드에 켜 둘 만하다.
- 런타임 검사: clang `-fsanitize=function` 은 이런 호출을 잡는 sanitizer 인데, **이 맥(arm64-apple-darwin)에서는 계측 코드 자체가 생성되지 않아** 보고가 나오지 않았다 (-O1 빌드가 그냥 정상 실행). 같은 소스를 `--target=aarch64-linux-gnu -fsanitize=function -S` 로 컴파일하면 호출 지점에 `bl __ubsan_handle_function_type_mismatch` 가 들어가는 것을 확인했다 — 리눅스에서는 동작한다는 뜻이다 (여기선 실행해 보지 못함). 더 강한 방어는 `-fsanitize=cfi-icall` (LTO 필요), 하드웨어로는 arm64e PAC, ARMv8.1-M PACBTI.
- **널 함수 포인터 호출**: `./wt null` 은 `-O2` 에서 종료 코드 133 (SIGTRAP) — 컴파일러가 널임을 증명하고 호출 자리에 `brk #0x1` 를 넣었다. `-O0` 에서는 실제로 주소 0 으로 뛰어 139 (SIGSEGV). (그 직전 `puts` 출력은 파이프로 받으면 버퍼에 남은 채 사라진다.) Cortex-M 에서 `BLX r` 의 r 이 0 이면 bit0(Thumb 비트)이 0 이라 **INVSTATE UsageFault** → (UsageFault 미활성 시) HardFault 가 된다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 74/74 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 74/74, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `int *f(int);` 를 함수 포인터로 착각 | 괄호가 없으면 `*` 는 반환 타입에 붙는다 — "int 포인터를 반환하는 함수" | `int (*f)(int);` 또는 typedef |
| 핸들러를 `void *` 에 저장 (`void *cb = handler;`) | 표준 C 에서 함수 포인터 ↔ `void *` 변환은 정의되지 않음 (6.3.2.3 은 객체 포인터만). 함수/데이터 포인터 크기가 다른 아키텍처도 있다 | 함수 포인터 typedef 사용. 범용 슬롯이 필요하면 `void (*)(void)` 로 저장하고 원래 타입으로 되돌려 호출 |
| `qsort(a, n, sizeof *a, (int (*)(const void *, const void *))cmp_int)` — `cmp_int(const int *, const int *)` | 틀린 타입으로 호출 → UB (위 demo ③ 처럼 대부분 "동작" 해서 더 위험) | 비교 함수를 `const void *` 로 받고 안에서 변환 |
| 등록 안 된 콜백을 검사 없이 호출 | 호스트 SIGSEGV/SIGTRAP, Cortex-M INVSTATE → HardFault | `if (cb) cb(...)`, 또는 기본(no-op) 핸들러로 채워 두기 |
| `strncmp(tok, name, strlen(tok)) == 0` 로 명령 찾기 | `"p"` 가 `peek` 와 매칭, 빈 문자열은 첫 명령과 매칭 | `strcmp(...) == 0` (정확히 일치) |
| `strchr("+-*/", c)` 로 표 인덱스 계산 | `c == '\0'` 이면 종료 문자를 찾아 인덱스 4 → 범위 밖 | 표 길이만큼 직접 비교, 또는 `c != '\0' &&` |
| 명령/ops 표에서 `const` 를 빠뜨림 | `.data` → 플래시+RAM 두 벌, 메모리 손상 한 번이면 **임의 코드 실행 경로** | `static const shell_cmd_t k_cmds[]`, `static const struct driver_ops` |
| 디바이스마다 함수 포인터를 복사 (`struct device { int (*read)(); int (*write)(); ... }`) | 디바이스 N × op M 개가 RAM 에. 같은 드라이버 인스턴스끼리 공유 안 됨 | 디바이스에는 `const struct driver_ops *ops` 하나만 |
| `strtok` 으로 셸 토큰화 | 숨은 정적 상태 → 셸 두 개/태스크 두 개에서 깨짐 (C-A04) | 제자리 토크나이저 또는 `strtok_r` |
| ISR 이 부르는 `(fn, ctx)` 콜백 쌍을 메인에서 `cb.fn = f; cb.ctx = c;` 순으로 등록 | 두 번의 쓰기 사이에 ISR → 새 fn 이 옛 ctx 로 불린다 (찢어진 쌍) | 임계 구역에서 둘 다 쓰기, 또는 ctx 먼저·fn 나중 + 컴파일러 배리어 (C-M14, C-A02) |

## 8. 면접 Q&A

<details>
<summary>Q1. void (*signal(int sig, void (*func)(int)))(int); 를 읽어 보세요. typedef 로 다시 쓰면?</summary>

signal 은 (int, "int 를 받아 void 를 반환하는 함수의 포인터") 를 받아서, "int 를 받아 void 를 반환하는 함수의 포인터" 를 반환하는 함수다. 반환값은 이전 핸들러. `typedef void (*sighandler_t)(int); sighandler_t signal(int, sighandler_t);` — 이름에서 시작해 오른쪽(괄호·대괄호) → 막히면 왼쪽(`*`) → 괄호 밖으로. 꼬리질문 "`int (*(*fp)(int))[10]` 는?" → fp 는 int 를 받아 "int 10개짜리 배열의 포인터" 를 반환하는 함수를 가리키는 포인터.

</details>

<details>
<summary>Q2. f(x), (*f)(x), (**f)(x) 는 뭐가 다르죠? f 와 &f 는?</summary>

모두 같다. 함수 지정자는 `sizeof`/`&` 의 피연산자가 아닐 때 함수 포인터로 자동 변환되고 (6.3.2.1p4), 호출 연산자는 함수 포인터를 받는다 (6.5.2.2p1). `*f` 는 함수 지정자가 되었다가 다시 포인터로 바뀌므로 별표를 몇 개 붙여도 같다. `f` 와 `&f` 도 같은 값·같은 타입(함수 포인터). 반면 `sizeof f` 는 함수 타입이라 제약 위반(6.5.3.4p1) — 컴파일 에러(GCC 는 확장으로 1).

</details>

<details>
<summary>Q3. 콜백에 상태(context)를 넘기는 방법들을 비교해 보세요.</summary>

① 전역 변수 — 인스턴스가 하나일 때만, 재진입 불가 (qsort 의 한계) ② `void *ctx` 를 콜백과 함께 등록 — 가장 일반적 (`pthread_create`, FreeRTOS `pvTimerGetTimerID`, Zephyr `k_timer_user_data_get()`) — 타입 안전은 포기 ③ 콜백이 공통 구조체 포인터를 받고 `container_of` 로 바깥 구조체 복원 — Linux `work_struct`, `timer_list` 방식, 추가 포인터 없음. ④ 핸들 구조체에 콜백과 사용자 필드를 같이 넣기 — STM32 HAL `UART_HandleTypeDef` 를 넘겨 주는 방식. 주의: `int (*)(struct foo *)` 를 `int (*)(void *)` 로 캐스트해서 등록하면 UB — 콜백 쪽이 `void *` 를 받아 안에서 변환해야 한다.

</details>

<details>
<summary>Q4. C 로 다형성(polymorphism)을 어떻게 구현하나요? C++ 가상 함수와 비교하면?</summary>

ops 구조체(함수 포인터 표)를 `const` 로 하나 만들고, 객체에는 그 표의 포인터만 둔다 — C++ 의 vtable/vptr 와 같은 구조. 공통 코드는 `d->ops->read(d, ...)` 처럼 객체 자신을 첫 인자(this)로 넘긴다. 차이: C 는 op 를 NULL 로 두어 "선택적 기능" 을 표현할 수 있고 (Linux `file_operations` 대부분이 그렇다), 상속은 구조체 embed + `container_of` 로 흉내 낸다. 표를 객체마다 복사하지 않는 이유는 RAM (N×M 포인터 → N 포인터) 과 보안(표가 플래시에 있으면 덮어쓸 수 없음).

</details>

<details>
<summary>Q5. 간접 호출(함수 포인터)은 직접 호출보다 얼마나 느리죠?</summary>

호출 자체는 거의 같다 — Apple M6 실측에서 예측되는 간접 호출 0.57–0.73 ns vs 인라인 금지 직접 호출 0.56–0.71 ns. 진짜 비용은 **인라인 불가**: 인라인된 버전은 NEON 벡터화까지 되어 0.06 ns/원소 (약 10배). 그 밖에 표 포인터 로드, 루프 안 재로드(호출이 메모리를 바꿀 수 있으니까), callee-saved 레지스터 저장. Cortex-M3/M4 는 동적 분기 예측기가 없어 BL/BLX 모두 파이프라인 재충전, 간접은 플래시에서 포인터 LDR 이 추가. 컴파일러가 표 내용을 알면(`static const`, LTO) 탈가상화해서 비용이 0 이 된다 (`call_const_table` asm).

</details>

<details>
<summary>Q6. 명령 표·ops 표를 const 로 만들어야 하는 이유는? const 면 무조건 플래시에 가나요?</summary>

이유: ① bare-metal 에서 `.rodata` = 플래시 → RAM 0 B, 부팅 시 복사 없음 ② 메모리 손상(버퍼 오버런)으로 함수 포인터를 덮어써 임의 코드 실행으로 가는 경로 차단 — 플래시는 일반 store 로 못 쓴다 ③ 컴파일러 탈가상화 가능. "무조건" 은 아니다: 표 안에 포인터가 있으므로 PIE/공유 라이브러리(Linux, macOS)에서는 로더가 재배치해야 해서 `.data.rel.ro`(RELRO) / Mach-O `__DATA_CONST` 에 간다 — 실측 표 참고. 로드 후 읽기 전용으로 잠기므로 보안 목적은 같다. Cortex-M ROPI(위치 독립) 빌드도 비슷한 제약이 있다.

</details>

<details>
<summary>Q7. 함수 포인터를 다른 타입으로 캐스트해서 호출하면 어떻게 되나요?</summary>

변환 자체와 되돌리기는 합법이고 값이 보존된다 (6.3.2.3p8). **호환되지 않는 타입으로 호출하면 UB**. 실제로는 ABI 가 같으면 "동작" 하는 경우가 많아서 더 위험하다 — demo 에서 `void(int)` 를 `void(void)` 로 부르면 레지스터에 남은 쓰레기(10)를 받았고, `int(struct uart *)` 를 `int(void *)` 로 부르면 정상처럼 보였다. 잡는 법: `-Wcast-function-type`(-strict), clang `-fsanitize=function` (이 맥에서는 계측되지 않음, Linux 타깃에선 계측 코드 확인), CFI. 고치는 법: 래퍼 함수나 `void *ctx` 를 받는 시그니처로 통일.

</details>

<details>
<summary>Q8. 콜백을 ISR 에서 부를 때 등록/해제 코드는 무엇을 조심해야 하나요?</summary>

① 포인터 하나의 쓰기는 정렬된 32비트 Cortex-M 에서 단일 `STR` 이라 찢어지지 않지만, `(fn, ctx)` **쌍** 은 두 번의 쓰기 → 그 사이 ISR 이 새 fn + 옛 ctx 를 부를 수 있다. 임계 구역(PRIMASK)으로 묶거나, 해제 시 fn 을 먼저 NULL 로, 등록 시 ctx 먼저 → fn 나중 (컴파일러 배리어/volatile 로 순서 보장, C-B09·C-A02). ② ISR 은 읽은 포인터를 지역 변수에 한 번 담고 널 검사 후 호출 (두 번 읽으면 그 사이에 NULL 이 될 수 있다 — `my_raise` 의 `h`). ③ 콜백은 ISR 문맥에서 실행되므로 짧게, 블로킹·printf·malloc 금지라는 계약을 문서화. ④ 콜백이 자기 자신을 해지할 수 있게 순회는 복사본으로 (추가 연습 4).

</details>

<details>
<summary>Q9. STM32 HAL 의 __weak 콜백과 함수 포인터 등록 방식은 어떻게 다르죠?</summary>

`__weak void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h)` 는 **링크 타임** 바인딩: 사용자가 같은 이름의 강한 심볼을 정의하면 링커가 그걸 쓴다. 간접 호출이 없어 빠르고 RAM 0 B 지만, 프로그램 전체에 구현이 **하나** 뿐이라 UART 가 여러 개면 함수 안에서 `h->Instance` 로 분기해야 하고, 런타임에 바꿀 수 없다. 함수 포인터 등록(HAL 의 `USE_HAL_UART_REGISTER_CALLBACKS=1` → `HAL_UART_RegisterCallback`)은 **런타임** 바인딩: 인스턴스마다 다른 콜백, 교체 가능, 대신 RAM 의 포인터와 널 검사가 필요. 함정: weak 함수 이름 오타는 링크 에러 없이 기본(빈) 구현이 불린다.

</details>

<details>
<summary>Q10. 부트로더에서 애플리케이션으로 점프할 때 함수 포인터를 어떻게 만드나요?</summary>

앱 벡터 테이블의 두 번째 워드(리셋 핸들러 주소)를 읽어 함수 포인터로 변환해 호출한다: `uint32_t pc = *(volatile uint32_t *)(APP_BASE + 4u); void (*app)(void) = (void (*)(void))pc;` 정수 → 함수 포인터 변환은 **구현 정의** (6.3.2.3p5) 지만 ARM 컴파일러에서 기대대로 동작한다. 벡터 테이블 값은 이미 bit0 = 1 (Thumb) 이라 그대로 `BLX` 하면 된다 — 직접 주소를 계산했다면 `| 1u` 를 잊으면 INVSTATE 폴트. 호출 전 MSP 를 앱의 초기 SP(`APP_BASE + 0`)로 설정, 인터럽트 끄기·SysTick 정지·VTOR 갱신이 순서다.

</details>

## 9. 추가 연습

### 연습 1 — 함수 포인터를 반환하는 함수, 배열을 가리키는 포인터

typedef 없이 선언하라: (a) `char` 를 받아 `bool (*)(const char *, uint32_t *)` 를 반환하는 `pick_parser` ('d' → 10진, 'x' → 16진, 그 외 NULL), (b) 그런 파서 포인터 2개짜리 배열을 가리키는 포인터 `p_parsers`. `_Generic` 으로 typedef 버전과 같은 타입인지 확인하고 `(*p_parsers)[1]("zz", &v)` 를 호출해 보라.

<details>
<summary>답 보기</summary>

`static bool (*pick_parser(char fmt))(const char *, uint32_t *)` 와 `static bool (*(*p_parsers)[2])(const char *, uint32_t *) = &k_parsers;`. 읽기: p_parsers → `*` 포인터 → `[2]` 2개짜리 배열 → `*` 포인터 → `(const char *, uint32_t *)` 함수 → `bool`. 호출은 `(*p_parsers)` 로 배열을 얻고 `[1]` 로 원소(함수 포인터)를 고른 뒤 `(...)`. `&k_parsers` 와 `k_parsers` 는 값은 같은 주소지만 타입이 다르다 (`parse_fn (*)[2]` vs 감쇠한 `parse_fn *` — C-B06).

</details>

### 연습 2 — 정렬된 명령 표 + bsearch

명령 7개를 이름순으로 둔 `const` 표에서 `bsearch` 로 찾는 `find_sorted()` 와, 표가 엄격히 오름차순(중복 없음)인지 검사하는 `table_is_sorted()` 를 작성하라. 비교 함수의 시그니처에 주의.

<details>
<summary>답 보기</summary>

`static int cmd_key_cmp(const void *key, const void *elem) { return strcmp((const char *)key, ((const cmd_t *)elem)->name); }` — `bsearch` 는 첫 인자로 **key**, 둘째로 **배열 원소** 를 넘긴다 (7.22.5.1p2). 그래서 키를 이름 문자열 그대로 넘길 수 있다 (`qsort` 비교 함수는 둘 다 원소). 시그니처가 정확히 `int (*)(const void *, const void *)` 여야 하고 캐스트로 맞추면 UB. 정렬 검사는 부팅 때(또는 단위 테스트에서) 한 번 — 표에 명령을 추가하다 순서를 틀리면 일부 명령만 "가끔" 못 찾는 버그가 된다. O(log n) 이지만 명령이 수십 개면 선형과 차이 없다.

</details>

### 연습 3 — 따옴표 지원 토크나이저

`echo "hello world" x` → 3 토큰 (`hello world` 는 하나). 따옴표는 지우고, 닫히지 않은 따옴표는 -1, 토큰 초과는 -2. `name="a b"c` → `name=a bc`, `""` → 빈 토큰.

<details>
<summary>답 보기</summary>

읽기 포인터 `r` 와 쓰기 포인터 `w` 두 개를 쓴다. 토큰 안에서 `"` 를 만나면 상태(`inq`)만 뒤집고 쓰지 않으므로 글자들이 앞으로 당겨진다. `w ≤ r` 가 항상 성립해서 아직 안 읽은 글자를 덮지 않는다 (제자리 압축, memmove 와 같은 논리 — C-M08). 토큰 끝 판단은 `!inq && (공백 || '\0')`. 끝에서 `inq` 가 참이면 닫히지 않은 따옴표. `*w = '\0'` 을 쓰기 **전에** `*r == '\0'` 여부를 저장해 둬야 한다 (w == r 일 때 덮어쓰므로).

</details>

### 연습 4 — 이벤트 버스: (fn, ctx) 구독

최대 4개의 `(evt_cb fn, void *ctx)` 구독을 받는 `subscribe / unsubscribe / publish` 를 만들어라. 같은 fn 이라도 ctx 가 다르면 별개 구독 (LED 두 개), 같은 쌍은 중복 거부, 콜백 안에서 자기 자신을 해지해도 안전해야 한다.

<details>
<summary>답 보기</summary>

구독의 정체성은 **(fn, ctx) 쌍** 이다 — fn 만 비교하면 LED A 를 해지하려다 LED B 까지 지운다. `publish` 는 슬롯을 **복사본** 으로 읽고 호출하므로 콜백이 `unsubscribe` 로 자기 슬롯을 비워도 순회가 꼬이지 않는다 (테스트 `once_on_evt`). 빈 슬롯은 `fn == NULL` 로 표시, 호출 전 널 검사. ISR 에서 publish 한다면 Q8 의 쌍 쓰기 순서 문제도 같이 고려.

</details>

### 연습 5 — ctx 가 있는 정렬 (qsort 의 빈자리)

`isort_r(base, n, size, cmp, ctx)` (삽입 정렬, 비교 함수가 `void *ctx` 를 받음) 를 구현하고, 레지스터 인덱스 배열을 "그 레지스터의 값" 기준으로 오름/내림차순 정렬하라. 값에는 `0xFFFFFFFF` 가 있다.

<details>
<summary>답 보기</summary>

ctx 에 `{ const uint32_t *val; bool desc; }` 를 담아 비교 함수가 값 표와 방향을 안다 — `qsort` 였다면 전역 변수가 필요했다. 비교는 `(va > vb) - (va < vb)` — `(int)(va - vb)` 는 unsigned 뺄셈 결과를 int 로 바꾸면서 부호가 뒤집혀(0xFFFFFFFF − 0 → -1) 순서가 틀린다 (구현 정의 변환). 원소 크기가 런타임 값이라 `memcpy` 로 옮긴다. 결과: 오름 `{4, 2, 3, 0, 1}`, 내림 `{1, 0, 3, 2, 4}`.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 26/26 passed
```

## 10. 핵심 정리

- 선언은 **이름 → 오른쪽 → 왼쪽 → 괄호 밖** 으로 읽는다. 복잡하면 typedef (`typedef void (*sighandler_t)(int);`) 로 쪼갠다 — 같은 타입이다.
- `f(x) == (*f)(x)`, `f == &f` (6.3.2.1p4). 함수 포인터는 `==` 로 비교, 널 검사 후 호출.
- 콜백은 **(fn, void \*ctx) 쌍** 으로 등록한다. 콜백 시그니처를 `void *` 로 받고 안에서 변환 — 캐스트로 시그니처를 맞추면 UB (6.3.2.3p8).
- 디스패치 표는 `static const` → bare-metal `.rodata`(플래시, RAM 0 B, 덮어쓰기 불가). PIE 에선 `.data.rel.ro`. `const char *const` 까지.
- 셸: 제자리 토큰화 + `argv[argc] = NULL`, 정확히 일치 탐색, argc 범위·NULL 핸들러를 디스패처가 막는다. 빈 줄은 에러가 아니다.
- ops 패턴: 디바이스엔 `const ops *` 하나, 선택적 op 는 NULL, 공통 계층이 검사를 모은다. 전용 데이터는 embed + `container_of` 또는 `void *priv`.
- 비용: 예측되는 간접 호출 ≈ 직접 호출 (M6 실측), 진짜 손해는 인라인·벡터화 상실 (~10배). `static const` 표는 탈가상화된다.
- 널 함수 포인터 호출: 맥 -O2 SIGTRAP(`brk`), -O0 SIGSEGV, Cortex-M INVSTATE → HardFault.

## 참고자료

- C11 (N1570) 6.3.2.1p4 함수 지정자 변환, 6.3.2.3p1·p5·p8 포인터 변환, 6.5.2.2 함수 호출, 6.5.9p6 포인터 비교, 6.7.6.3p15 함수 타입 호환, 6.7.9p10·p19 초기화, 7.14.1.1 signal, 7.22.5 bsearch/qsort — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- Linux kernel — [Character device drivers / `struct file_operations`](https://www.kernel.org/doc/html/latest/filesystems/vfs.html#struct-file-operations), LDD3 3장 "Char Drivers" (`container_of` 로 `private_data` 설정)
- Zephyr — [Device Driver Model (`struct device`, `api`, `data`)](https://docs.zephyrproject.org/latest/kernel/drivers/index.html)
- Clang — [UndefinedBehaviorSanitizer `-fsanitize=function`](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html), [Control Flow Integrity `-fsanitize=cfi-icall`](https://clang.llvm.org/docs/ControlFlowIntegrity.html)
- ARM — Cortex-M4 Technical Reference Manual "Instruction timing" (BL/BLX 1+P), ARMv7-M ARM "INVSTATE UsageFault", [ELF for the Arm Architecture (R_ARM_ABS32 `(S + A) | T`)](https://github.com/ARM-software/abi-aa/blob/main/aaelf32/aaelf32.rst)
- [cdecl.org](https://cdecl.org/) — C 선언 ↔ 영어 번역기
- 관련 레슨: C-B06 (배열 vs 배열 포인터), C-B08 (`const` 위치, const ops 표), C-B09 (한 번 읽어 지역 변수에), C-B11 (섹션), C-B12 (offsetof), C-M07 (상태 머신 표), C-M09 (strtol), C-M14 (ISR 공유), C-A04 (strtok 재진입), C-A06 (타입 검사 container_of)
