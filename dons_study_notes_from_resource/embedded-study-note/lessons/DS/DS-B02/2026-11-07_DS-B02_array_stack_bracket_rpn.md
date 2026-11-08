---
id: DS-B02
time: 50분
updated: 2026-11-07
---
# DS-B02 배열 기반 스택과 괄호·명령 검사

## 학습 목표

- 고정 용량 배열 위에 **스택 ADT** (`push / pop / peek`) 를 만들고 overflow / underflow 를 상태 코드로 처리한다.
- **최고 수위(high-water mark)** 를 기록해 "스택이 얼마나 깊어졌나"를 재는 펌웨어 습관을 익힌다.
- 스택으로 **괄호 균형 검사기**를 만들고, 오류를 종류(짝 불일치·미닫힘·남는 닫힘·너무 깊음)와 **위치**로 보고한다.
- 스택으로 **후위표기식(RPN) 계산기**를 만들고, 피연산자 순서·0 나누기·int32 넘침(`INT32_MIN / -1` 포함)을 정확히 처리한다.
- 테이블 기반 테스트 20개로 정상·오류 경로를 모두 검증한다.

## 1. 문제

스택(stack)은 "마지막에 넣은 것을 먼저 꺼내는(LIFO)" 구조다. 펌웨어에서는 어디에나 있다 — CPU 콜 스택, ISR 중첩, UART 디버그 셸이나 SCPI 명령 파서, 상태 머신의 "이전 화면으로" 기능. 면접에서는 **Valid Parentheses (LeetCode 20)** 와 **Evaluate RPN (LeetCode 150)** 이 단골이고, 펌웨어 면접관은 여기에 "용량이 정해져 있으면?", "넘치면?", "int 오버플로는?" 을 꼭 덧붙인다.

배열 스택은 배열 + `top` 하나다. `top` 은 "들어 있는 개수"이자 "다음에 쓸 칸"이다.

```svg
<svg viewBox="0 0 720 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b2a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">istack: items[8] + top (empty-ascending)</text>
  <g stroke="currentColor" fill="none">
    <rect x="80" y="48" width="80" height="26"/><rect x="80" y="74" width="80" height="26"/><rect x="80" y="100" width="80" height="26"/>
    <rect x="80" y="126" width="80" height="26"/><rect x="80" y="152" width="80" height="26"/>
  </g>
  <g class="f-acc" stroke="currentColor">
    <rect x="80" y="178" width="80" height="26"/><rect x="80" y="204" width="80" height="26"/><rect x="80" y="230" width="80" height="26"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="120" y="248">10</text><text x="120" y="222">20</text><text x="120" y="196">30</text>
  </g>
  <g fill="currentColor" text-anchor="end" font-size="12">
    <text x="72" y="248">0</text><text x="72" y="222">1</text><text x="72" y="196">2</text><text x="72" y="170">3</text>
    <text x="72" y="144">4</text><text x="72" y="118">5</text><text x="72" y="92">6</text><text x="72" y="66">7</text>
  </g>
  <path d="M230 165 L166 165" stroke="currentColor" fill="none" marker-end="url(#b2a)"/>
  <text x="236" y="170" fill="currentColor">top = 3</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="330" y="60">push(x): top == CAP → OVERFLOW</text>
    <text x="330" y="80">         items[top++] = x</text>
    <text x="330" y="108">pop():   top == 0   → UNDERFLOW</text>
    <text x="330" y="128">         *out = items[--top]</text>
    <text x="330" y="156">peek():  *out = items[top-1]</text>
  </g>
  <g fill="currentColor">
    <text x="330" y="200">ARM Cortex-M 콜 스택은 반대 모양 (full-descending):</text>
    <text x="330" y="222">SP 가 "마지막 원소"를 가리키고, 주소가 줄어드는</text>
    <text x="330" y="244">쪽으로 자란다. PUSH = SP -= 4 후 *SP = x.</text>
  </g>
</svg>
```

### Part A — 스택 ADT

- `st_push(s, x)` — 가득 차면 `ST_OVERFLOW`. 성공하면 `high_water` 를 갱신한다.
- `st_pop(s, &out)` / `st_peek(s, &out)` — 비었으면 `ST_UNDERFLOW`. peek 은 꺼내지 않는다.
- 실패한 호출은 스택을 바꾸지 않는다. 용량은 `STACK_CAP = 8` (테스트에서 넘치게 하려고 일부러 작게).

### Part B — 괄호 균형 검사기

`check_brackets(str, &err_pos)` — `()`, `[]`, `{}` 를 검사하고 그 밖의 글자는 무시한다 (명령줄 `led set(1) [on]` 같은 입력). 결과는 5가지:

| 결과 | 뜻 | `err_pos` |
|---|---|---|
| `BR_OK` | 균형 맞음 | — |
| `BR_MISMATCH` | `(]` 처럼 종류가 다름 | 닫는 괄호 위치 |
| `BR_EXTRA_CLOSE` | 열린 게 없는데 닫음 | 닫는 괄호 위치 |
| `BR_UNCLOSED` | 끝났는데 열린 게 남음 | 가장 안쪽 미닫힘 여는 괄호 위치 |
| `BR_TOO_DEEP` | 중첩이 `STACK_CAP` 초과 | 넘친 여는 괄호 위치 |

### Part C — RPN(후위표기식) 계산기

`rpn_eval(expr, &out)` — 공백으로 구분된 토큰. 숫자는 `-` 가 붙을 수 있는 int32 (`"-"` 한 글자는 연산자), 연산자는 `+ - * /` (나눗셈은 C 규칙대로 0 쪽으로 자름). **성공했을 때만** `*out` 에 쓴다. 오류 8가지:

`RPN_ERR_EMPTY` (토큰 없음), `RPN_ERR_TOKEN` (모르는 토큰), `RPN_ERR_UNDERFLOW` (피연산자 부족), `RPN_ERR_STACK_FULL`, `RPN_ERR_DIV0`, `RPN_ERR_RANGE` (int32 넘침), `RPN_ERR_LEFTOVER` (끝났는데 값이 2개 이상).

숫자 파싱 함수 `parse_number` 는 시작 코드에 **제공**된다. 여러분은 `apply_op` 과 `rpn_eval` 을 채운다. 하네스에는 괄호 10개 + RPN 10개 = **테스트 케이스 20개**가 표로 들어 있다.

## 2. Problem (English)

> **Bounded stack, bracket checker, and RPN evaluator.**
>
> 1. Implement a stack of `int32_t` with fixed capacity `STACK_CAP` (no heap): `st_push`, `st_pop`, `st_peek` returning `ST_OK`, `ST_OVERFLOW`, or `ST_UNDERFLOW`. Track the maximum depth ever reached.
> 2. `br_result check_brackets(const char *s, size_t *err_pos)` — validate `()[]{}` nesting, ignoring all other characters. Report the error kind and the index of the offending character (for an unclosed opener, the innermost one). Nesting deeper than `STACK_CAP` is an error.
> 3. `rpn_status rpn_eval(const char *expr, int32_t *out)` — evaluate a space-separated Reverse Polish Notation expression over 32-bit signed integers with `+ - * /`. Detect: empty input, invalid token, operand underflow, stack full, division by zero, result out of `int32_t` range, and leftover operands. Write `*out` only on success.
>
> Examples: `"5 1 2 + 4 * + 3 -"` → `14`; `"-7 2 /"` → `-3`; `"4 -"` → underflow; `"-2147483648 -1 /"` → range error; `"([)]"` → mismatch at index 2.
>
> Follow-up: what is the time and space complexity? How would you bound memory if the input came from a UART at runtime?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "7/28 passed" — 스택 3개, 괄호 검사 1개, RPN 2개 함수를 채워 28/28 을 만든다
```

> 맥 SDK 27.2에서 `ld: tapi error` 가 나면 `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` 후 다시 빌드.

표 기반 케이스가 틀리면 `FAIL ... ok` 다음 줄에 `bracket case 5 "([)]": got OK@999, want MISMATCH@2` 처럼 **어느 입력이 무엇을 기대했는지** 찍힌다.

## 4. 힌트

<details>
<summary>힌트 1 — push 는 top++, pop 은 --top</summary>

`items[top++] = x` (쓰고 올림) 과 `*out = items[--top]` (내리고 읽음) 은 정확히 거꾸로다. 검사는 항상 접근 **전에**: push 는 `top == STACK_CAP`, pop/peek 은 `top == 0`.

</details>

<details>
<summary>힌트 2 — 괄호 검사: 문자 대신 "위치"를 push</summary>

여는 괄호를 만나면 그 **인덱스 i** 를 push. 닫는 괄호를 만나면 pop 해서 `str[pos]` 가 짝인지 본다. 위치를 넣어두면 UNCLOSED 오류 때 "몇 번째 글자가 안 닫혔는지"를 그대로 알려줄 수 있다. 짝 찾기는 제공된 `opener_of(c)` 사용.

</details>

<details>
<summary>힌트 3 — 끝까지 읽은 뒤에도 할 일이 있다</summary>

문자열이 끝났을 때 스택이 비어 있지 않으면 `BR_UNCLOSED` 다. 이 검사를 빼먹으면 `"(("` 가 통과한다. 위치는 `peek` 한 값 (가장 안쪽).

</details>

<details>
<summary>힌트 4 — RPN: 연산자를 만나면 b 를 먼저 꺼낸다</summary>

`"5 3 -"` 는 5 − 3. 스택 꼭대기에 있는 건 **오른쪽** 피연산자(3)다. 그래서 `b = pop(); a = pop(); push(a op b)`. 순서를 바꾸면 `-` 와 `/` 만 틀리고 `+ *` 는 맞아서, 테스트가 덧셈뿐이면 버그가 숨는다.

</details>

<details>
<summary>힌트 5 — 넘침 검사는 넓은 타입에서</summary>

`int32_t` 끼리 `a * b` 를 하면 넘치는 순간 이미 UB 다. `(int64_t)a * b` 로 계산한 뒤 `INT32_MIN..INT32_MAX` 범위인지 확인. 나눗셈 중 유일하게 넘치는 경우가 `INT32_MIN / -1` (= 2^31) 이라는 것도 같은 방법으로 잡힌다.

</details>

## 5. 풀이

### Part A — 배열 스택과 최고 수위

| 상태 | `top` | push | pop / peek |
|---|---|---|---|
| 빈 스택 | 0 | OK | UNDERFLOW |
| 중간 | 1..CAP-1 | OK | OK |
| 꽉 참 | CAP | OVERFLOW | OK |

모든 연산이 O(1). `top` 을 "개수"로 두면 `st_size` 가 곧 `top` 이고, `items[top]` 은 항상 **비어 있는 칸**(또는 배열 끝)이다 — 이 방식을 empty-ascending 이라 부른다. ARM 콜 스택(full-descending)은 SP 가 마지막 원소를 가리키므로 PUSH 가 "먼저 줄이고 쓴다"(`STMDB`), POP 이 "읽고 늘린다"(`LDMIA`). 방향과 기준점만 다를 뿐 같은 구조다.

**high_water** 는 "지금까지 가장 깊었던 순간"이다. 비워도 줄지 않는다. 펌웨어에서 이게 왜 중요하냐면, RTOS 태스크 스택 크기를 정할 때 쓰는 값이 바로 이것이기 때문이다. FreeRTOS `uxTaskGetStackHighWaterMark()` 는 스택을 `0xA5` 로 칠해두고(stack painting) 안 지워진 바이트를 세서 같은 값을 구한다. 테스트 환경에서 최고 수위를 측정하고, 여유를 20~50% 두는 게 보통.

### Part B — 괄호 검사: 한 번 훑기, 스택에는 위치

알고리즘은 한 줄씩 읽으면서:

1. 여는 괄호 → 위치를 push. 넘치면 `TOO_DEEP`.
2. 닫는 괄호 → pop. 비었으면 `EXTRA_CLOSE`. 꺼낸 위치의 문자가 짝이 아니면 `MISMATCH`.
3. 그 밖의 글자 → 무시.
4. 끝 → 스택이 남아 있으면 `UNCLOSED` (위치 = peek), 아니면 OK.

`"([)]"` 와 `"x = (a + b"` 추적:

| 입력 | i | 글자 | 동작 | 스택(위치들) | 결과 |
|---|---|---|---|---|---|
| `([)]` | 0 | `(` | push 0 | [0] | |
| | 1 | `[` | push 1 | [0, 1] | |
| | 2 | `)` | pop → 1, `str[1]='['` ≠ `(` | [0] | **MISMATCH @2** |
| `x = (a + b` | 4 | `(` | push 4 | [4] | |
| | 5..9 | 기타 | 무시 | [4] | |
| | 끝 | | peek → 4 | [4] | **UNCLOSED @4** |

**왜 카운터 하나로는 안 되나?** `(` 에서 +1, `)` 에서 −1 하는 카운터는 괄호 종류가 하나일 때만 맞다. `"([)]"` 는 각 종류의 개수가 맞아서 카운터로는 통과하지만, 중첩 순서가 틀렸다. 순서를 기억하려면 스택이 필요하다.

**복잡도와 메모리 상한.** 시간 O(n), 공간 O(최대 중첩 깊이). 펌웨어에서 입력이 UART로 들어온다면 깊이를 공격자(또는 노이즈)가 정할 수 있다 → 그래서 고정 용량 + `TOO_DEEP` 오류가 필요하다. 재귀로 짜면 같은 입력이 **콜 스택**을 터뜨리는데, 그건 상태 코드로 돌아오지 않고 HardFault 로 끝난다 (MISRA C Rule 17.2 가 재귀를 금지하는 이유).

### Part C — RPN: 숫자는 쌓고, 연산자는 두 개 꺼내 하나 넣는다

```svg
<svg viewBox="0 0 720 245" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">"5 1 2 + 4 * + 3 -" — 토큰마다 스택 모습 (아래가 바닥)</text>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-weight="bold">
    <text x="58" y="70">5</text><text x="133" y="70">1</text><text x="208" y="70">2</text><text x="283" y="70">+</text><text x="358" y="70">4</text>
    <text x="433" y="70">*</text><text x="508" y="70">+</text><text x="583" y="70">3</text><text x="658" y="70">-</text>
  </g>
  <g class="f-acc" stroke="currentColor">
    <rect x="30" y="150" width="56" height="26"/>
    <rect x="105" y="150" width="56" height="26"/><rect x="105" y="124" width="56" height="26"/>
    <rect x="180" y="150" width="56" height="26"/><rect x="180" y="124" width="56" height="26"/><rect x="180" y="98" width="56" height="26"/>
    <rect x="255" y="150" width="56" height="26"/>
    <rect x="330" y="150" width="56" height="26"/><rect x="330" y="124" width="56" height="26"/><rect x="330" y="98" width="56" height="26"/>
    <rect x="405" y="150" width="56" height="26"/>
    <rect x="555" y="150" width="56" height="26"/><rect x="555" y="124" width="56" height="26"/>
  </g>
  <g class="f-warn" stroke="currentColor">
    <rect x="255" y="124" width="56" height="26"/><rect x="405" y="124" width="56" height="26"/>
    <rect x="480" y="150" width="56" height="26"/><rect x="630" y="150" width="56" height="26"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="58" y="168">5</text>
    <text x="133" y="168">5</text><text x="133" y="142">1</text>
    <text x="208" y="168">5</text><text x="208" y="142">1</text><text x="208" y="116">2</text>
    <text x="283" y="168">5</text><text x="283" y="142">3</text>
    <text x="358" y="168">5</text><text x="358" y="142">3</text><text x="358" y="116">4</text>
    <text x="433" y="168">5</text><text x="433" y="142">12</text>
    <text x="508" y="168">17</text>
    <text x="583" y="168">17</text><text x="583" y="142">3</text>
    <text x="658" y="168">14</text>
  </g>
  <line x1="20" y1="180" x2="700" y2="180" stroke="currentColor"/>
  <text x="10" y="210" fill="currentColor">색이 다른 칸 = 연산 결과. 연산자는 위의 두 칸(b=꼭대기, a=그 아래)을 빼고 a op b 를 넣는다.</text>
  <text x="10" y="233" fill="currentColor">최대 깊이 3, 마지막에 정확히 1개 남음 → 결과 14. 괄호도 우선순위 규칙도 필요 없다.</text>
</svg>
```

**왜 RPN 인가.** 중위식 `(1 + 2) * 4` 는 괄호와 우선순위 규칙이 필요하지만, 후위식 `1 2 + 4 *` 는 "읽은 순서대로 실행"하면 끝이다. 그래서 스택 머신 — JVM 바이트코드, WebAssembly, Forth, PostScript, HP 계산기 — 이 이 형식을 쓴다. 중위식을 후위식으로 바꾸는 것도 스택 하나로 된다 (shunting-yard, 추가 연습 4).

**오류 판단 위치 정리.**

| 언제 | 조건 | 오류 |
|---|---|---|
| 숫자 토큰 | 숫자가 아님 / 범위 밖 | `TOKEN` / `RANGE` (parse_number) |
| 숫자 토큰 | push 실패 | `STACK_FULL` |
| 연산자 | pop 두 번 중 하나라도 실패 | `UNDERFLOW` |
| 연산자 | `b == 0` (나눗셈) | `DIV0` |
| 연산자 | int64 결과가 int32 밖 | `RANGE` |
| 끝 | 토큰 0개 | `EMPTY` |
| 끝 | 스택 크기 ≠ 1 | `LEFTOVER` |

연산자 처리 뒤의 push 는 **실패할 수 없다** — 방금 두 개를 뺐으니 최소 한 칸이 비어 있다. 그래서 `(void)st_push(...)` 로 반환값을 버려도 된다 (주석으로 이유를 남긴다).

**int32 넘침의 세 얼굴.**

- `2147483647 1 +` → int64 로 2147483648 → `RANGE`.
- `100000 100000 *` → 10^10 → `RANGE`. int32 끼리 곱했다면 그 자체가 UB.
- `-2147483648 -1 /` → 2^31 → `RANGE`. 이건 단순 UB 가 아니라 **x86 에서는 실제로 프로세스가 죽는다** (`idiv` 가 #DE 예외 → SIGFPE). ARM Cortex-M 의 `SDIV` 는 예외 없이 `INT32_MIN` 을 돌려준다 — 같은 C 코드가 플랫폼마다 다르게 망가지는 전형.

나눗셈 반올림: C99 부터 `/` 는 **0 쪽으로 자른다** (`-7 / 2 == -3`, 파이썬 `//` 는 −4). 테스트 케이스 3번이 이걸 고정한다.

### 테스트 케이스 20개 (실제 실행 결과 전부 PASS)

| # | 괄호 입력 | 기대 | # | RPN 입력 | 기대 |
|---|---|---|---|---|---|
| 1 | `""` | OK | 11 | `3 4 +` | OK, 7 |
| 2 | `()[]{}` | OK | 12 | `5 1 2 + 4 * + 3 -` | OK, 14 |
| 3 | `{[()()]}` | OK | 13 | `-7 2 /` | OK, −3 |
| 4 | `led set(1) [on]` | OK | 14 | `"   "` | EMPTY |
| 5 | `(]` | MISMATCH @1 | 15 | `4 -` | UNDERFLOW |
| 6 | `([)]` | MISMATCH @2 | 16 | `1 2` | LEFTOVER |
| 7 | `x = (a + b` | UNCLOSED @4 | 17 | `2 x +` | TOKEN |
| 8 | `(()` | UNCLOSED @0 | 18 | `1 0 /` | DIV0 |
| 9 | `())` | EXTRA_CLOSE @2 | 19 | `1 2 3 4 5 6 7 8 9` | STACK_FULL |
| 10 | `((((((((()))))))))` | TOO_DEEP @8 | 20 | `-2147483648 -1 /` | RANGE |

설계 포인트: **오류 종류마다 최소 1개**, 그리고 경계(빈 입력, 정확히 CAP+1 깊이, 유일한 넘침 나눗셈)를 넣는다. RPN 실패 케이스는 `out` 이 미리 넣어둔 `12345` 그대로인지도 확인한다 ("실패 시 출력 안 건드림" 계약). 스택 단위 테스트 8개를 더해 하네스 총 28개.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 28/28 passed
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined solution.c -o sol_asan && ./sol_asan   # 깨끗
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `a = pop(); b = pop(); push(a - b);` | 꼭대기가 오른쪽 피연산자. `"5 3 -"` → −2 | `b = pop(); a = pop(); push(a - b);` |
| 끝에서 스택 비었는지 확인 안 함 | `"(("`, `"1 2"` 가 통과 | 끝에서 `UNCLOSED` / `LEFTOVER` 검사 |
| 괄호 종류 무시하고 카운터만 | `"([)]"` 통과 | 스택에 넣고 짝 비교 |
| `pop` 에서 빈 검사 없이 `items[--top]` | `top` 이 `size_t` 면 0 − 1 = `SIZE_MAX` → 배열 밖 접근 (ASan 빌드에서 바로 잡힌다) | `if (top == 0) return ST_UNDERFLOW;` 먼저 |
| `while (top >= 0)` (top 은 `size_t`) | unsigned 는 항상 ≥ 0 → 무한 루프 | `while (top > 0)` |
| `int32_t r = a * b;` 후 범위 검사 | 넘치는 순간 이미 UB, 검사는 의미 없음 | `int64_t r = (int64_t)a * b;` |
| `INT32_MIN / -1` 을 안 막음 | x86 SIGFPE 로 크래시, ARM 은 조용히 틀린 값 | int64 로 계산 후 범위 검사 |
| `atoi(tok)` | `"2x"` → 2, `"x"` → 0, 넘침은 UB — 오류를 구분 못 함 | 직접 파싱 또는 `strtol` + `endptr`/`errno` 확인 |
| `strtok(expr, " ")` | 원본을 고쳐 쓴다 → 문자열 리터럴이면 크래시, 전역 상태라 ISR/스레드에 위험 | 포인터 두 개로 토큰 경계만 찾기 (정답 코드) |
| 재귀 하향 파서로 괄호 처리 | 깊은 입력이 MCU 콜 스택을 넘쳐 HardFault | 명시적 고정 용량 스택 + `TOO_DEEP` |

## 8. 면접 Q&A

<details>
<summary>Q1. 괄호 검사의 시간·공간 복잡도는? 괄호 종류가 하나뿐이면 더 줄일 수 있나요?</summary>

시간 O(n), 공간 O(d) (d = 최대 중첩 깊이, 최악 n). 종류가 하나면 스택 대신 **카운터** 하나로 O(1) 공간이 된다 — 단, 중간에 카운터가 음수가 되는 순간 즉시 실패시켜야 `")("` 를 잡는다. 꼬리질문 "괄호 안 문자열 리터럴은?" → 따옴표 상태를 하나 더 두는 작은 상태 머신 (추가 연습 3).

</details>

<details>
<summary>Q2. 펌웨어에서 (콜) 스택 오버플로를 어떻게 감지하나요?</summary>

① **스택 페인팅 + 최고 수위**: 시작 시 스택을 `0xA5` 등으로 채우고 나중에 안 지워진 양을 센다 (FreeRTOS `uxTaskGetStackHighWaterMark`). ② **카나리 검사**: 스택 끝 몇 워드를 문맥 전환 때마다 확인 (FreeRTOS `configCHECK_FOR_STACK_OVERFLOW` 1/2). ③ **MPU 가드 영역**: 스택 바로 아래를 접근 금지로 → 넘는 순간 MemManage fault. ④ **ARMv8-M `MSPLIM/PSPLIM`**: 하드웨어가 SP 한계를 직접 검사 (Cortex-M33/M55). ⑤ 정적 분석: `gcc -fstack-usage` + 콜 그래프로 최악 깊이 계산. 오늘의 `high_water` 필드는 ①의 축소판이다.

</details>

<details>
<summary>Q3. 배열 스택 vs 연결 리스트 스택, 펌웨어에서는 어느 쪽을 쓰나요?</summary>

대부분 배열. 상한이 명확하고, 노드마다 포인터 오버헤드가 없고, 메모리가 연속이라 캐시에 좋고, malloc 이 필요 없다. 연결 리스트 스택은 크기를 모를 때 유리하지만 노드 할당이 필요하다 — 펌웨어에서는 정적 노드 풀(DS-B07)에서 꺼내 쓰는 **free list** 가 사실상 연결 리스트 스택이다 (마지막에 반납한 블록을 먼저 재사용 → 캐시에 따뜻한 블록).

</details>

<details>
<summary>Q4. RPN 계산에서 정수 오버플로를 어떻게 처리했나요? 다른 방법은?</summary>

넓은 타입(int64)에서 계산 후 범위 검사. 넓은 타입이 없다면(int64 계산이 비싼 MCU 등) **미리 검사**: 덧셈은 `b > 0 && a > INT32_MAX - b`, 곱셈은 나눗셈으로 한계 비교, 나눗셈은 `a == INT32_MIN && b == -1`. GCC/Clang 은 `__builtin_add_overflow` 등을 제공하고, C23 은 `<stdckdint.h>` 의 `ckd_add/ckd_mul` 로 표준화했다. 포화 연산(DSP)이 필요하면 범위 밖일 때 최대/최솟값으로 고정 (C-B02 `sat_add_i16`).

</details>

<details>
<summary>Q5. 재귀 대신 명시적 스택을 쓰는 이유를 펌웨어 관점에서 설명해보세요.</summary>

재귀 깊이는 입력에 따라 달라지고, 프레임 크기(레지스터 저장 + 지역 변수)는 컴파일러가 정해서 예측이 어렵다. 넘치면 오류 코드가 아니라 메모리 오염이나 HardFault 다. 명시적 스택은 ① 크기가 컴파일 타임에 고정 ② 넘침을 **상태 코드로** 처리 ③ 원소당 크기가 작다(여기선 4바이트 vs 프레임 수십 바이트). MISRA C:2012 Rule 17.2 (재귀 금지), JPL/NASA Power of Ten 규칙 1 도 같은 이유다.

</details>

<details>
<summary>Q6. 이 계산기를 UART 디버그 셸에 붙인다면 무엇을 더 고려하나요?</summary>

① 입력 줄 길이 상한 (라인 버퍼 크기, 넘치면 줄 전체 폐기) ② 파싱은 메인 루프/태스크에서 — ISR 은 바이트를 링 버퍼(DS-B03)에 넣기만 ③ 오류 메시지에 **위치**를 포함 (`err @col 7`) — 이번 괄호 검사기처럼 ④ 토큰화가 원본을 바꾸지 않게 (strtok 금지) ⑤ 재진입 가능하게 전역 상태 없이 (스택을 지역 변수/호출자 제공으로). 실제 계측 장비의 SCPI 파서나 Zephyr shell 이 이런 구조다.

</details>

## 9. 추가 연습

### 연습 1 — min-stack: 최솟값을 O(1) 로

`push / pop / get_min` 이 모두 O(1) 인 스택. 같은 최솟값이 여러 번 들어와도 맞아야 한다.

<details>
<summary>답 보기</summary>

보조 스택 `mins` 를 하나 더 두고, push 할 때마다 "지금까지의 최솟값" `min(x, mins.top)` 을 같이 push, pop 할 때 같이 pop. 두 스택 크기가 항상 같아서 `mins.top` 이 곧 현재 최솟값. 메모리 2배 대신 O(1). (최솟값이 바뀔 때만 push 하는 최적화도 있지만, 그때는 중복 최솟값 처리에 `<=` 를 써야 한다.)

</details>

### 연습 2 — 배열 하나에 스택 두 개

크기 8 배열 하나를 두 스택이 공유한다. 한쪽이 적게 쓰면 다른 쪽이 더 쓸 수 있어야 한다.

<details>
<summary>답 보기</summary>

왼쪽 스택은 `0` 에서 위로(`buf[lo++]`), 오른쪽 스택은 `CAP` 에서 아래로(`buf[--hi]`). 두 꼭대기가 만나면(`lo == hi`) 둘 다 가득. 고전적인 MCU 메모리 배치 — **힙은 위로, 스택은 아래로** 자라며 RAM 하나를 나눠 쓰는 것과 같은 아이디어다 (그래서 둘이 부딪히는 "stack-heap collision" 버그가 생긴다).

</details>

### 연습 3 — 따옴표를 아는 명령 검사

`set name "a)b" (x` 처럼 문자열 리터럴 안의 괄호는 무시하고, `\"` 이스케이프를 처리하며, 닫히지 않은 따옴표는 `BR_OPEN_QUOTE` (위치 = 여는 따옴표) 로 보고하라.

<details>
<summary>답 보기</summary>

상태 하나(`in_q`) 를 추가한 상태 머신. 따옴표 밖에서는 본문과 같은 괄호 처리, 따옴표 안에서는 `\` 다음 글자를 건너뛰고 `"` 만 본다. 끝에서 `in_q` 가 참이면 `OPEN_QUOTE` 를 UNCLOSED 보다 먼저 보고 (따옴표가 안 닫히면 그 뒤 괄호 판정은 의미가 없으므로). `"a)b"` 안의 `)` 는 무시되어 위치 15의 `(` 가 UNCLOSED 로 남는다.

</details>

### 연습 4 — shunting-yard: 중위식 → RPN

`"(1 + 2) * (3 + 4)"` → `"1 2 + 3 4 + *"` 로 바꾸고 본문 `rpn_eval` 로 계산하라. 음이 아닌 정수, `+ - * /`, 괄호만. 왼쪽 결합 (`10 - 4 - 3 == 3`). 괄호 짝이 안 맞거나 출력 버퍼가 모자라면 실패.

<details>
<summary>답 보기</summary>

Dijkstra 의 shunting-yard. 숫자는 바로 출력. 연산자는 **스택 꼭대기 연산자의 우선순위가 같거나 높은 동안** 꺼내 출력한 뒤 push (`>=` 가 왼쪽 결합을 만든다; `>` 로 쓰면 `10 - 4 - 3` 이 `10 4 3 - -` = 9 가 된다). `(` 는 push, `)` 는 `(` 가 나올 때까지 꺼내 출력 — 바닥까지 가면 짝 오류. 끝에 남은 연산자를 다 출력하는데 `(` 가 남아 있으면 짝 오류. 출력 버퍼는 `'\0'` 자리를 남기고 한 글자씩 경계 검사.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 19/19 passed
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined practice.c -o pr_asan && ./pr_asan
```

## 10. 핵심 정리

- 배열 스택 = `items[CAP] + top`. push `items[top++] = x`, pop `items[--top]`, 검사는 접근 **전에** (`top == CAP` / `top == 0`).
- 실패한 연산은 상태를 바꾸지 않고 상태 코드로 돌아온다. `high_water` 로 최대 깊이를 측정 (RTOS 스택 크기 결정과 같은 원리).
- 괄호 검사: 여는 괄호의 **위치**를 push, 닫을 때 pop 해서 짝 비교, 끝에 남으면 UNCLOSED. 카운터로는 `"([)]"` 를 못 잡는다.
- RPN: 숫자 push, 연산자는 **b 먼저** pop → `a op b` push, 끝에 정확히 1개.
- int32 연산은 int64 로 계산 후 범위 검사. `INT32_MIN / -1` 은 x86 에서 크래시, ARM 에선 조용히 틀림.
- C 나눗셈은 0 쪽으로 자른다 (`-7/2 == -3`).
- 펌웨어 파서는 재귀·strtok·atoi 대신 고정 용량 스택 + 포인터 토큰화 + 검증된 숫자 파싱.
- 테스트는 오류 종류마다 1개 이상 + 경계(빈 입력, CAP+1 깊이, 유일한 넘침 나눗셈).

## 참고자료

- [LeetCode 20 — Valid Parentheses](https://leetcode.com/problems/valid-parentheses/)
- [LeetCode 150 — Evaluate Reverse Polish Notation](https://leetcode.com/problems/evaluate-reverse-polish-notation/)
- [Wikipedia — Shunting yard algorithm](https://en.wikipedia.org/wiki/Shunting_yard_algorithm)
- [FreeRTOS — Stack Usage and Stack Overflow Checking](https://www.freertos.org/Stacks-and-stack-overflow-checking.html)
- Arm, *Armv8-M Architecture Reference Manual* — MSPLIM/PSPLIM 스택 한계 레지스터
- [SEI CERT C — INT32-C: Ensure that operations on signed integers do not result in overflow](https://wiki.sei.cmu.edu/confluence/display/c/INT32-C.+Ensure+that+operations+on+signed+integers+do+not+result+in+overflow)
- MISRA C:2012 Rule 17.2 — 재귀 금지 / Gerard Holzmann, *The Power of Ten: Rules for Developing Safety Critical Code* (2006)
- CLRS 10.1 — Stacks and queues
