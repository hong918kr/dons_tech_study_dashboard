---
id: C-B15
time: 50분
updated: 2026-11-07
---
# C-B15 매크로 기초: 괄호·do-while(0)·다중 평가

## 학습 목표

- 함수형 매크로(function-like macro)가 **텍스트 치환**일 뿐이라는 것을 `cc -E` 출력으로 직접 보고, 우선순위 버그를 괄호 규칙으로 막는다.
- 문장 여러 개짜리 매크로를 `do { ... } while (0)` 로 감싸야 하는 이유(if/else, for 본문)를 설명한다.
- `MAX(i++, j)` · `MAX(UART->DR, x)` 의 **다중 평가(multiple evaluation)** 를 GNU statement expression + `__typeof__`, 또는 `static inline` 함수로 고치고, 각각이 표준인지 확장인지 정확히 말한다.
- `#`(문자열화)과 `##`(토큰 붙이기)로 CMSIS 스타일 레지스터 매크로를 만들고, "한 단계 더 거치는" `XSTR` 이 왜 필요한지 안다.
- 중첩해도 안전한 임계 구역(critical section) 매크로를 PRIMASK 저장·복원으로 쓴다.

## 1. 문제

벤더 헤더(CMSIS, STM32 HAL, nRF MDK)와 RTOS 코드는 매크로 덩어리다. 면접에서도 "MAX 매크로 써보세요 → `MAX(i++, j)` 는요? → 그럼 어떻게 고치죠?" 꼬리질문이 거의 정해진 코스라서, **버그 3종 세트와 고치는 법을 손으로 쓸 수 있어야** 한다.

핵심은 하나다. 전처리기(preprocessor)는 C를 모른다. 매크로 인자를 **글자 그대로 복사해 붙여 넣을 뿐**이고, 연산자 우선순위·문장 구조·평가 횟수는 붙여 넣은 *뒤에* 컴파일러가 결정한다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">#define SQUARE(x) x * x  →  SQUARE(a + 1) 은 무엇이 되나</text>
  <rect x="10" y="40" width="200" height="34" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="62" fill="currentColor" font-family="monospace">SQUARE(a + 1)</text>
  <path d="M215 57 L265 57" stroke="currentColor" fill="none"/><path d="M258 52 L265 57 L258 62" stroke="currentColor" fill="none"/>
  <text x="222" y="48" fill="currentColor" font-size="12">텍스트 복사</text>
  <rect x="270" y="40" width="200" height="34" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="282" y="62" fill="currentColor" font-family="monospace">a + 1 * a + 1</text>
  <text x="490" y="62" fill="currentColor">a=2 → 2 + 2 + 1 = 5 (기대 9)</text>
  <text x="10" y="110" fill="currentColor" font-weight="bold">컴파일러가 보는 식 트리</text>
  <g stroke="currentColor" fill="none">
    <circle cx="120" cy="140" r="14"/><circle cx="70" cy="185" r="14"/><circle cx="170" cy="185" r="14"/>
    <circle cx="140" cy="228" r="12"/><circle cx="200" cy="228" r="12"/>
    <path d="M110 150 L80 175 M130 150 L160 175 M162 197 L146 218 M178 197 L194 218"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="120" y="145">+</text><text x="70" y="190">a+1</text><text x="170" y="190">*</text>
    <text x="140" y="233">1</text><text x="200" y="233">a</text>
  </g>
  <g stroke="currentColor" fill="none">
    <circle cx="470" cy="140" r="14"/><circle cx="410" cy="185" r="16"/><circle cx="530" cy="185" r="16"/>
    <path d="M460 150 L420 170 M480 150 L520 170"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="470" y="145">*</text><text x="410" y="190">a+1</text><text x="530" y="190">a+1</text>
  </g>
  <text x="250" y="150" fill="currentColor">순진한 버전:</text><text x="250" y="170" fill="currentColor">곱셈이 먼저 묶인다</text>
  <text x="580" y="140" fill="currentColor">((x) * (x)) :</text><text x="580" y="160" fill="currentColor">인자가 한 덩어리</text>
  <rect x="380" y="208" width="350" height="34" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="392" y="230" fill="currentColor" font-family="monospace">((a + 1) * (a + 1)) = 9</text>
</svg>
```

`starter.c` 의 매크로는 전부 이런 "순진한(naive)" 버전이다. 테스트 26개가 버그를 재현하고(처음 6/26), TODO 매크로만 고쳐서 26/26 을 만든다.

### Part A — 괄호: SQUARE, MAX

- `SQUARE(x)`: `SQUARE(a + 1) == 9`, `100 / SQUARE(5) == 4` 를 만족하게.
- `MAX(a, b)`: `MAX(3, 2) + 10 == 13`, `10 + MAX(3, 2) == 13` 을 만족하게.
- 규칙: **인자 하나하나에 괄호, 매크로 전체에 괄호.** (둘 중 하나만 하면 위 테스트 중 하나는 여전히 깨진다.)

### Part B — 다중 문장과 다중 평가

- `LED_ON_COUNT(n)`: `led_on(n); g_led_events++` 두 문장. `if (go) LED_ON_COUNT(1);` 에서 `go` 가 거짓이면 아무 일도 없어야 하고, `for` 본문으로 써도 두 문장이 다 반복돼야 한다 → `do { ... } while (0)`.
- `SWAP(a, b)`: `int tmp` 고정이라 `double`, `uint64_t` 가 잘린다 → `__typeof__(a)` 로 타입을 따라가고, 호출자 변수와 안 겹치는 이름을 쓴다.
- `MAX_ONCE(a, b)`: `MAX_ONCE(i++, j)` 는 `i` 를 한 번만 올리고, `MAX_ONCE(uart_read_dr(), 4u)` 는 **FIFO에서 한 바이트만** 꺼내야 한다 → GNU statement expression `({ ... })`.
- `max_u32(a, b)`: 같은 일을 하는 표준 C `static inline` 함수.

### Part C — 펌웨어 매크로: `#`, `##`, 임계 구역

- `REG_POS(P, R, F)` / `REG_MSK(P, R, F)`: `##` 로 `UART_CR2_STOP_Pos` 같은 이름을 조립.
- `REG_FIELD_READ` / `REG_FIELD_WRITE(inst, P, R, F, v)`: 필드만 바꾸는 read-modify-write (필드 자체는 C-B04 참고). `if/else` 안에서도 한 문장처럼 동작해야 한다.
- `REG_NAME(UART, CR2)` → `"UART_CR2"`, `XSTR(UART_CR2_STOP_Pos)` → `"12u"` (값으로 펼친 뒤 문자열화).
- `CRITICAL(...)`: 인자 문장들을 인터럽트를 막은 채 실행. **중첩**되거나 이미 IRQ가 꺼진 곳에서 불려도 끝날 때 *원래 상태*로 돌려놓아야 한다.

## 2. Problem (English)

> **Fix the macros.** You are given a header of naive function-like macros used in a firmware code base:
>
> ```c
> #define SQUARE(x)       x * x
> #define MAX(a, b)       a > b ? a : b
> #define LED_ON_COUNT(n) led_on(n); g_led_events++
> #define SWAP(a, b)      { int tmp = a; a = b; b = tmp; }
> #define CRITICAL(...)   cpu_disable_irq(); __VA_ARGS__; cpu_enable_irq()
> ```
>
> 1. For each macro, write a call site that compiles but behaves incorrectly (or fails to compile) and explain why, using the preprocessed output (`cc -E`).
> 2. Rewrite them so that: every argument and the whole expansion are parenthesized; multi-statement macros behave as a single statement (`if (c) M(); else ...` must compile and work); `SWAP` works for any scalar type.
> 3. Write `MAX_ONCE(a, b)` that evaluates each argument exactly once (`MAX_ONCE(i++, j)` increments `i` once; `MAX_ONCE(UART->DR, 4)` pops one byte from the RX FIFO). State which compiler extensions you used and give a standard-C alternative.
> 4. Using `##`, write `REG_MSK(P, R, F)` → `P_R_F_Msk` and a `REG_FIELD_WRITE` that updates only that field. Using `#`, produce `"UART_CR2"` and the *value* string `"12u"` of `UART_CR2_STOP_Pos`.
> 5. Make `CRITICAL(...)` nest correctly by saving and restoring PRIMASK.
>
> Example: with `a = 2`, `SQUARE(a + 1) == 9`; `100 / SQUARE(5) == 4`; `10 + MAX(3, 2) == 13`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 문제 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# FAIL 20줄 + "6/26 passed" — TODO 매크로를 고쳐 26/26 을 만든다
```

> 실패 줄의 `#expr` 출력이 `r == 9` 처럼 **매크로가 펼쳐지기 전 글자**로 나오는 것도 눈여겨보자. `#` 는 인자를 펼치지 않는다 (Part C의 `STR` vs `XSTR` 와 같은 규칙).

## 4. 힌트

<details>
<summary>힌트 1 — 먼저 펼쳐 보라</summary>

`cc -E -P starter.c | less` 로 `main` 을 찾아보면 `r = a + 1 * a + 1;`, `if (go) led_on(1); g_led_events++;` 가 그대로 보인다. 고친 뒤에도 매번 `-E` 로 확인하는 습관이 가장 빠른 디버깅이다.

</details>

<details>
<summary>힌트 2 — 괄호는 두 겹</summary>

인자 괄호 `(x)` 는 "**안에서** 인자가 쪼개지는 것"을, 바깥 괄호 `( ... )` 는 "**밖의** 연산자가 매크로 본문 일부를 뜯어가는 것"을 막는다. `100 / SQUARE(5)` 는 바깥 괄호 문제다.

</details>

<details>
<summary>힌트 3 — 왜 그냥 { } 가 아니고 do-while(0) 인가</summary>

`if (c) { ... }; else` — 중괄호 뒤에 사용자가 찍은 `;` 가 빈 문장이 되어 `if` 를 끝내 버린다. `do { ... } while (0)` 은 **세미콜론이 있어야 완성되는 한 문장**이라 `;` 를 정확히 흡수한다.

</details>

<details>
<summary>힌트 4 — 한 번만 평가하려면 값을 임시 변수에</summary>

식 안에서 변수를 선언할 수 없으니 GNU 확장 `({ 선언; 선언; 마지막 식; })` 을 쓴다 — 마지막 식의 값이 전체 값. 임시 변수 타입은 `__typeof__(a)` (피연산자를 **평가하지 않는다**). `-pedantic` 경고는 `__extension__` 으로 끈다.

</details>

<details>
<summary>힌트 5 — # / ## 는 인자를 펼치지 않는다</summary>

`#x` 는 `x` 자리에 들어온 **글자**를 문자열로 만든다. 값을 원하면 `#define XSTR(x) STR(x)` 처럼 한 단계 거친다: 바깥 매크로의 인자는 (#/## 에 안 붙어 있으므로) 먼저 완전히 펼쳐진 뒤 `STR` 로 넘어간다.

</details>

<details>
<summary>힌트 6 — 임계 구역은 "끄고 → 켜기"가 아니라 "저장 → 끄기 → 복원"</summary>

`uint32_t pm = cpu_get_primask(); cpu_disable_irq(); ...; cpu_set_primask(pm);` — 들어올 때 이미 꺼져 있었다면 나갈 때도 꺼진 채로 둔다.

</details>

## 5. 풀이

### Part A — 괄호 두 겹

`cc -E -P demo_expand.c` 로 순진한 매크로를 펼친 **실제 출력**:

```text
int demo(int a, int x, int go) {
    int r1 = a + 1 * a + 1;
    int r2 = 100 / 5 * 5;
    int r3 = 10 + 3 > 2 ? 3 : 2;
    int r4 = x & 0xF > 3 ? x & 0xF : 3;
    if (go) led_on(1); g_led_events++;
    return r1 + r2 + r3 + r4;
}
```

| 호출 | 펼친 결과 | 실제 묶임 | 값 | 필요한 괄호 |
|---|---|---|---|---|
| `SQUARE(a+1)`, a=2 | `a + 1 * a + 1` | `a + (1*a) + 1` | 5 | 인자 `(x)` |
| `100 / SQUARE(5)` | `100 / 5 * 5` | `(100/5)*5` | 100 | 바깥 `( )` |
| `MAX(3,2) + 10` | `3 > 2 ? 3 : 2 + 10` | `3>2 ? 3 : (2+10)` | 3 | 바깥 `( )` |
| `10 + MAX(3,2)` | `10 + 3 > 2 ? 3 : 2` | `(13 > 2) ? 3 : 2` | 3 | 바깥 `( )` |
| `MAX(x & 0xF, 3)` | `x & 0xF > 3 ? ...` | `x & (0xF > 3)` | 엉뚱 | 인자 `(a)` |

`?:` 는 대입 바로 위, 거의 맨 아래 우선순위라서 MAX 는 바깥 괄호가 없으면 양옆 어떤 연산자에게든 진다 (우선순위표는 C-B14 참고). 마지막 줄은 다행히 clang 이 잡아준다 — `cc -std=c11 -Wall -c demo_expand.c` 실제 출력 (발췌):

```text
demo_expand.c:16:20: warning: & has lower precedence than >; > will be evaluated first [-Wparentheses]
   16 |     int r4 = MAX(x & 0xF, 3);
demo_expand.c:16:14: warning: operator '?:' has lower precedence than '&'; '&' will be evaluated first [-Wbitwise-conditional-parentheses]
2 warnings generated.
```

나머지 넷(`r1`~`r3`, `if` 줄)은 **경고 0개**로 조용히 틀린다. 경고는 안전망일 뿐 규칙을 대신하지 못한다.

고친 형태: `#define SQUARE(x) ((x) * (x))`, `#define MAX(a, b) ((a) > (b) ? (a) : (b))`.

### Part B-1 — 다중 문장: 왜 do { } while (0)

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">if (go) M(1); else led_on(0);  — M 을 세 가지로 정의했을 때</text>
  <rect x="10" y="40" width="230" height="150" rx="8" class="f-warn" stroke="currentColor"/>
  <rect x="255" y="40" width="230" height="150" rx="8" class="f-warn" stroke="currentColor"/>
  <rect x="500" y="40" width="230" height="150" rx="8" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" font-weight="bold"><text x="20" y="60">① 나열: a; b</text><text x="265" y="60">② 중괄호: { a; b; }</text><text x="510" y="60">③ do { a; b; } while (0)</text></g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="88">if (go) led_on(1);</text><text x="20" y="108">g_led_events++;</text><text x="20" y="128">else led_on(0);</text>
    <text x="265" y="88">if (go) { ... }</text><text x="265" y="108">;          ← 빈 문장</text><text x="265" y="128">else led_on(0);</text>
    <text x="510" y="88">if (go)</text><text x="510" y="108">  do { ... } while (0);</text><text x="510" y="128">else led_on(0);</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="158">if 가 첫 문장에서 끝남</text><text x="20" y="176">→ else 고아: 컴파일 에러</text>
    <text x="265" y="158">if 가 { } 에서 끝나고 ; 가</text><text x="265" y="176">따로 → else 고아: 에러</text>
    <text x="510" y="158">; 가 do-while 을 완성</text><text x="510" y="176">→ 정확히 한 문장</text>
  </g>
  <text x="10" y="215" fill="currentColor">else 가 없으면 ① 은 컴파일은 되고 "조용히" 틀린다: g_led_events++ 가 if 밖으로 빠져 항상 실행.</text>
  <text x="10" y="238" fill="currentColor">for (k...) M(k); 도 같다 — 첫 문장만 반복되고 둘째 문장은 루프 뒤에 한 번.</text>
  <text x="10" y="261" fill="currentColor">while (0) 은 상수라 조건 분기가 생기지 않는다 (-O0 arm64 확인: 무조건 b 만, -O1 이상은 그것도 없음).</text>
</svg>
```

`demo_ifelse.c` 로 직접 확인한 결과 (clang 21):

```text
$ cc -std=c11 -c demo_ifelse.c               # ② 중괄호 버전
demo_ifelse.c:20:5: error: expected expression
   20 |     else
$ cc -std=c11 -c -DUNBRACED demo_ifelse.c    # ① 나열 버전
demo_ifelse.c:20:5: error: expected expression
$ cc -std=c11 -c -DFIXED demo_ifelse.c       # ③ do-while(0)
(성공)
$ cc -E -P -DFIXED demo_ifelse.c | tail -6
void demo(int go) {
    if (go)
        do { led_on(1); g_led_events++; } while (0);
    else
        led_on(0);
}
```

starter 에서는 else 가 없는 ① 형태라 컴파일은 되고 런타임에 틀린다: `if (go) LED_ON_COUNT(1);` 뒤 `g_led_events == 1`, `for` 4회 뒤 `g_led_events == 2`.

### Part B-2 — SWAP: 타입과 이름 포획

순진한 `{ int tmp = a; a = b; b = tmp; }` 의 문제는 세 가지다.

| 문제 | 증상 | 고치는 법 |
|---|---|---|
| 타입 고정 `int` | `double 1.5` → `1`, `uint64_t 0x1_0000_0001` → `1` (out-of-range → signed 변환은 **구현 정의**, C11 6.3.1.3p3) | `__typeof__(a) tmp = (a);` |
| `{ }` + `;` | if/else 에서 컴파일 에러 | `do { } while (0)` |
| 이름 포획(name capture) | 호출자가 `SWAP(tmp, other)` 하면 `int tmp = (tmp);` — 새 `tmp` 가 자기 자신으로 초기화 | 호출자가 안 쓸 이름 (`swap_tmp_`) |

이름 포획은 **경고조차 안 나온다**. `demo_capture.c` 실제 결과:

```text
$ cc -E -P demo_capture.c | tail -4      # (발췌)
    int tmp = 1, other = 2;
    do { int tmp = (tmp); (tmp) = (other); (other) = tmp; } while (0);
$ cc -std=c11 -Wall -Wextra -c demo_capture.c
(아무 출력 없음)
$ cc -std=c11 -Wall -Wextra -Wshadow -c demo_capture.c
demo_capture.c:13:5: warning: declaration shadows a local variable [-Wshadow]
```

안쪽 `tmp` 는 선언 지점부터 유효해서(C11 6.2.1p7) `(tmp)` 는 바깥이 아니라 **초기화 안 된 자기 자신**을 읽는다 → 값이 불확정이고 이 경우 UB 다 (6.3.2.1p2). 그래서 실행 결과는 붙이지 않는다. 바깥 `tmp`·`other` 는 하나도 안 바뀐다. 교훈: 매크로 안 지역 변수 이름은 `name_` 처럼 튀게, 그리고 `-Wshadow` 를 켠다. (`__tmp` 처럼 밑줄 두 개로 시작하는 이름은 구현 예약(C11 7.1.3)이라 리눅스 커널처럼 "구현 측" 코드가 아니면 피한다.)

### Part B-3 — 다중 평가: 괄호로는 못 고친다

괄호를 다 친 `MAX` 도 인자를 **두 번** 쓴다: `((a) > (b) ? (a) : (b))`. 인자에 부작용이 있으면 두 번 일어난다.

```svg
<svg viewBox="0 0 740 240" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">MAX(uart_read_dr(), 4u) — 읽으면 빠지는 RX FIFO</text>
  <g stroke="currentColor"><rect x="20" y="45" width="40" height="30" class="f-acc"/><rect x="60" y="45" width="40" height="30" class="f-warn"/><rect x="100" y="45" width="40" height="30" fill="none"/><rect x="140" y="45" width="40" height="30" fill="none"/></g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace"><text x="40" y="65">7</text><text x="80" y="65">9</text><text x="120" y="65">2</text><text x="160" y="65">11</text></g>
  <text x="20" y="95" fill="currentColor" font-size="12">FIFO 앞쪽 →</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="220" y="55">((uart_read_dr()) &gt; (4u) ? (uart_read_dr()) : (4u))</text>
    <text x="220" y="80">  ① 조건: 7 꺼냄, 7 &gt; 4 참</text>
    <text x="220" y="100">  ② 결과: 또 읽음 → 9 꺼냄, 결과 9</text>
  </g>
  <rect x="20" y="120" width="700" height="40" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="32" y="145" fill="currentColor">결과는 9 (틀림), 7은 사라졌고 FIFO에서 2바이트가 빠졌다 — 데이터 유실</text>
  <rect x="20" y="172" width="700" height="56" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="32" y="194" fill="currentColor" font-family="monospace" font-size="12">({ __typeof__(a) a_ = (a); __typeof__(b) b_ = (b); a_ &gt; b_ ? a_ : b_; })</text>
  <text x="32" y="216" fill="currentColor">값을 임시 변수에 한 번 담고 비교 → 7, 1바이트만 소비. 함수 호출도 인자를 한 번만 평가한다.</text>
</svg>
```

- `MAX(i++, j)` 는 UB 가 **아니다**: `?:` 의 첫 피연산자 평가 뒤에 시퀀스 포인트가 있다 (C11 6.5.15p4). 결과는 정확히 "i 가 두 번 증가, 값 6". 정의된 동작이라 sanitizer 도, 경고도 없다 — 그래서 더 위험하다.
- 펌웨어에서 진짜 무서운 건 `i++` 가 아니라 **읽기 부작용이 있는 레지스터**다: UART/SPI `DR`(FIFO pop), 일부 상태 레지스터(read-to-clear), `volatile` 변수(ISR이 중간에 바꿈 → 비교한 값과 반환한 값이 다를 수 있다).

고치는 세 가지 방법:

| 방법 | 코드 | 표준? | 장단점 |
|---|---|---|---|
| GNU statement expression | `__extension__ ({ __typeof__(a) a_ = (a); ... })` | 아니오 (GCC/clang 확장) | 타입 제네릭, 한 번 평가. MSVC·일부 상용 컴파일러 불가 |
| `static inline` 함수 | `max_u32(uint32_t a, uint32_t b)` | 예 (C99) | 타입 검사·디버거 친화. 타입마다 함수 필요 |
| C11 `_Generic` + inline 함수들 | 추가 연습 4 | 예 (C11) | 표준이면서 타입 제네릭. 조합이 많아지면 장황 |

**표준·경고 여부 — 직접 돌려본 결과** (`demo_gnu.c`, Apple clang 21):

| 빌드 | `({ })` + `__typeof__` | `__extension__ ({ })` | `typeof` (밑줄 없음) |
|---|---|---|---|
| `-std=c11 -Wall -Wextra` | 경고 없음 | 경고 없음 | **에러** (함수 호출로 해석) |
| `-std=c11 -pedantic` | `warning: use of GNU statement expression extension from macro expansion` | 경고 없음 | 에러 |
| `-std=gnu11 -pedantic` | 같은 경고 | 경고 없음 | OK |
| `-std=c23 -pedantic` | 같은 경고 | 경고 없음 | OK (C23 표준 키워드) |

즉 `-std=c11 -Wall -Wextra -Werror` 에서 그대로 통과하는 건 **clang/GCC가 -pedantic 없이 확장을 묵인하기 때문**이지 표준이라서가 아니다. `__typeof__` 는 밑줄 버전이라 엄격 모드에서도 키워드로 살아 있고, `typeof` 는 C23 에서 표준이 됐지만 statement expression 은 C23 에도 없다. `solution.c` 는 `-pedantic -Werror` 로도 통과하도록 `__extension__` 을 붙였다. (GCC 의 `-pedantic` 메시지는 "ISO C forbids braced-groups within expressions".)

`__typeof__(i++)` 는 `i++` 를 **평가하지 않는다** — 타입만 본다 (VLA 타입 제외, `sizeof` 와 같은 규칙). `cc -E` 로 보면 `__typeof__(i++) max_a_ = (i++);` 인데 `i` 는 한 번만 오른다.

### Part C-1 — `#` 와 `##`: 레지스터 이름 조립

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">## 는 토큰을 붙이고, # 는 글자를 문자열로 — 둘 다 인자를 펼치지 않는다</text>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="20" y="55">REG_MSK(UART, CR2, STOP)</text>
    <text x="20" y="80">→ UART ## _ ## CR2 ## _ ## STOP ## _Msk</text>
    <text x="20" y="105">→ UART_CR2_STOP_Msk          (새 토큰 하나, 재스캔)</text>
    <text x="20" y="130">→ (3u &lt;&lt; UART_CR2_STOP_Pos) → (3u &lt;&lt; 12u)</text>
  </g>
  <rect x="380" y="38" width="350" height="100" rx="8" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="390" y="60">STR(x)  = #x</text>
    <text x="390" y="80" font-family="monospace">STR(UART_CR2_STOP_Pos) → "UART_CR2_STOP_Pos"</text>
    <text x="390" y="105">XSTR(x) = STR(x)  : x 가 먼저 펼쳐진다</text>
    <text x="390" y="125" font-family="monospace">XSTR(UART_CR2_STOP_Pos) → STR(12u) → "12u"</text>
  </g>
  <rect x="20" y="160" width="710" height="98" rx="8" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="32" y="182">규칙 (C11 6.10.3.1): 매개변수가 # 뒤나 ## 앞뒤에 있으면 인자를 "그대로" 쓰고,</text>
    <text x="32" y="203">그 외에는 인자를 먼저 완전히 매크로 확장한 뒤 넣는다.</text>
    <text x="32" y="224">그래서 값이 필요하면 한 단계 더 거치는 XSTR / XCAT 을 쓴다.</text>
    <text x="32" y="245">REG_NAME(UART, CR2) = #P "_" #R → "UART" "_" "CR2" → 번역 6단계에서 "UART_CR2"</text>
  </g>
</svg>
```

`cc -E -P solution.c` 에서 뽑은 실제 확장 (Part C 테스트 줄):

```text
do { uint32_t reg_v_ = ((&fake_uart1))->CR2 & ~(3u << 12u); reg_v_ |= ((uint32_t)(2u) << 12u) & (3u << 12u); ((&fake_uart1))->CR2 = reg_v_; } while (0);
if (strcmp("UART" "_" "CR2", "UART_CR2") == 0) ...
if (strcmp("12u", "12u") == 0) ...
```

이게 바로 CMSIS-Core 가 쓰는 방식이다: `core_cm4.h` 의 `_VAL2FLD(field, value)` 는 `(((uint32_t)(value) << field ## _Pos) & field ## _Msk)` 로 `_VAL2FLD(SCB_AIRCR_VECTKEY, 0x5FA)` 같은 호출을 가능하게 한다. STM32 디바이스 헤더의 `SET_BIT(REG, BIT)`, `MODIFY_REG(REG, CLEARMASK, SETMASK)` 도 전부 인자에 괄호를 친 매크로다.

주의 두 가지: ① `##` 로 붙인 결과가 유효한 토큰이 아니면 UB (6.10.3.3p3) — 예: `CAT(+, /)`. ② `##` 옆 인자는 펼쳐지지 않으므로 `P` 자리에 `#define MYUART UART` 같은 별칭을 넘기면 `MYUART_CR2_..` 가 되어 컴파일 에러. 필요하면 `XCAT` 처럼 한 단계 거친다 (추가 연습 2).

### Part C-2 — 중첩 안전 임계 구역

순진한 `cpu_disable_irq(); body; cpu_enable_irq()` 는 버그가 둘이다: 다중 문장(`if (go) CRITICAL(shared++)` 에서 `shared++` 와 enable 이 항상 실행)과 **중첩 시 너무 일찍 켜기**.

| 시점 | 순진한 버전 PRIMASK | 저장·복원 버전 PRIMASK |
|---|---|---|
| 바깥 CRITICAL 진입 | 1 | 1 (저장: 0) |
| 안쪽 `shared_inc()` 진입 | 1 | 1 (저장: 1) |
| 안쪽 종료 | **0 ← IRQ 켜짐! 바깥 구역은 아직 진행 중** | 1 (복원: 1) |
| 바깥 종료 | 0 | 0 (복원: 0) |
| ISR 안(이미 1)에서 호출 후 | **0 ← ISR 중에 IRQ 켜버림** | 1 |

```c
#define CRITICAL(...)   do { uint32_t primask_saved_ = cpu_get_primask(); cpu_disable_irq(); \
                             __VA_ARGS__; cpu_set_primask(primask_saved_); } while (0)
```

- 실제 Cortex-M 에서는 CMSIS `__get_PRIMASK()` / `__disable_irq()`(= `CPSID i`) / `__set_PRIMASK()`. CMSIS 구현은 `"memory"` clobber 가 있는 인라인 asm 이라 컴파일러가 보호할 메모리 접근을 구역 밖으로 옮기지 못한다.
- `...` + `__VA_ARGS__` (C99 가변 인자 매크로) 덕분에 본문에 쉼표가 있어도(`shared = 1, shared += 2`) 인자 개수 에러가 안 난다.
- 같은 패턴: Zephyr `key = irq_lock(); ... irq_unlock(key);`, FreeRTOS `taskENTER_CRITICAL_FROM_ISR()` 이 돌려준 값을 `taskEXIT_CRITICAL_FROM_ISR(x)` 에 넘기는 것. (FreeRTOS 의 태스크용 `taskENTER_CRITICAL()` 은 대신 중첩 카운터를 쓴다.)
- 한계: 본문에서 `return` 하면 복원이 건너뛰어진다. 그리고 `break`/`continue` 는 **사용자의 루프가 아니라 do-while(0) 에 걸린다** — `CRITICAL(if (done) break;)` 는 바깥 for 를 빠져나가지 않고 do-while 만 빠져나가며 복원도 건너뛴다. 그래서 큰 로직은 매크로 인자로 넣지 말고 함수로 빼서 `CRITICAL(do_work());` 처럼 쓴다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution            # 26/26 passed
cc -std=c11 -Wall -Wextra -Werror -pedantic -O2 solution.c -o solution && ./solution  # __extension__ 덕분에 역시 통과
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution && ./solution  # 26/26, 보고 없음
```

실험용 파일 (모두 이 폴더에 있다, 결과는 5. 풀이에 붙인 그대로):

| 파일 | 명령 | 보여주는 것 |
|---|---|---|
| `demo_expand.c` | `cc -E -P demo_expand.c`, `cc -std=c11 -Wall -c demo_expand.c` | 순진한 매크로의 실제 확장, 경고가 잡는 것/못 잡는 것 |
| `demo_ifelse.c` | `cc -std=c11 -c [-DUNBRACED\|-DFIXED] demo_ifelse.c` | `{ }`·나열 매크로의 else 에러, do-while(0) 통과 |
| `demo_capture.c` | `cc -std=c11 -Wall -Wextra [-Wshadow] [-DFIXED] -c demo_capture.c` | SWAP 이름 포획, `-Wshadow` 만 잡음 |
| `demo_gnu.c` | `cc -std={c11,gnu11} [-pedantic] [-DPLAIN_TYPEOF] -c demo_gnu.c` | statement expression / `typeof` 의 모드별 경고·에러 |

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `#define SQUARE(x) (x * x)` | 바깥만 괄호 — `SQUARE(a+1)` = `(a + 1 * a + 1)` | `((x) * (x))` |
| `#define SQUARE(x) (x) * (x)` | 인자만 괄호 — `100 / SQUARE(5)` = `100 / (5) * (5)` | 바깥 괄호 추가 |
| `#define M(n) { a(n); b(); }` | `if (c) M(1); else ...` 에서 `;` 가 if 를 끝냄 → 에러 | `do { a(n); b(); } while (0)` |
| `#define M(n) do { ... } while (0);` | 끝에 `;` 를 넣으면 사용자 `;` 와 합쳐 두 문장 → else 에러 다시 발생 | 매크로 끝 `;` 금지 |
| 괄호만 치고 `MAX(i++, j)` 가 고쳐졌다고 생각 | 우선순위와 다중 평가는 별개 문제 | statement expression / inline 함수 |
| `MAX(UART1->DR, th)` 또는 `MAX(volatile_flag, x)` | 레지스터를 두 번 읽음 → FIFO pop 2회, 두 값이 다를 수 있음 | 지역 변수에 한 번 읽고 비교 |
| SWAP 안에 `int tmp` | 타입 잘림 + 호출자 `tmp` 포획 (`-Wall -Wextra` 로는 경고 없음) | `__typeof__(a) swap_tmp_`, `-Wshadow` |
| `#define XSTR(x) #x` 로 값 문자열 기대 | `#` 는 인자를 펼치지 않음 → 매크로 이름이 문자열로 | `#define XSTR(x) STR(x)` |
| 임계 구역 = `disable(); ...; enable();` | 중첩·ISR 안에서 IRQ를 너무 일찍 켬 | PRIMASK 저장 → 끄기 → 복원 |
| `CRITICAL(... break; ...)` | `break` 가 do-while(0) 를 빠져나가 복원 건너뜀 | 본문은 함수로 빼기 |
| 매크로 이름 = 함수 이름 (`max`) | `max(a,b)` 함수 정의/호출이 매크로로 치환됨 | 매크로는 대문자, 필요하면 `(max)(a, b)` 로 억제 |

## 8. 면접 Q&A

<details>
<summary>Q1. MAX 매크로를 써보세요. MAX(i++, j) 는 어떻게 되나요? UB 인가요?</summary>

`#define MAX(a, b) ((a) > (b) ? (a) : (b))`. `MAX(i++, j)` 는 `i > j` 이면 `i++` 가 두 번 평가되어 i 가 2 증가하고 결과는 증가된 값이다. **UB 는 아니다** — `?:` 는 첫 피연산자 평가 후 시퀀스 포인트가 있고 둘째/셋째 중 하나만 평가된다(C11 6.5.15p4). 정의된 동작이지만 의도와 다른 것. 고치는 법: `static inline` 함수, 또는 GCC/clang 의 `({ __typeof__(a) _a = (a); ... })`. 리눅스 `max()` 는 여기에 (예전 구현 기준) `(void)(&_a == &_b)` 같은 포인터 비교로 **두 인자 타입이 다르면 경고**까지 낸다 (signed/unsigned 섞인 비교 방지, C-B02 참고).

</details>

<details>
<summary>Q2. 왜 `{ }` 가 아니라 `do { } while (0)` 인가요? `if (1) { } else` 트릭은요?</summary>

`{ }` 뒤에 사용자의 `;` 가 빈 문장이 되어 `if (c) M(); else` 가 깨진다. do-while(0) 은 `;` 로 끝나야 완성되는 단일 문장이라 일반 함수 호출과 똑같이 쓸 수 있다. `if (1) { ... } else` 형태는 사용자가 `;` 를 빼먹어도 다음 문장을 else 로 삼켜 버리는 등 더 위험하다. 대안으로 식만 필요한 경우엔 쉼표 연산자로 한 식을 만들거나(`(a(), b())`), 아예 inline 함수를 쓴다.

</details>

<details>
<summary>Q3. 매크로 대신 inline 함수를 쓰면 성능이 떨어지지 않나요? 매크로가 꼭 필요한 경우는?</summary>

`static inline` 은 -O1 이상에서 거의 항상 인라인되어 매크로와 같은 코드가 나온다(`-Os` 에서도 작은 함수는 인라인). 게다가 타입 검사, 한 번 평가, 디버거 스텝이 된다. 매크로가 필요한 경우: ① `#`/`##` 로 이름·문자열 생성 (레지스터 이름, X-macro, 로그의 `#expr`/`__FILE__`/`__LINE__`) ② 타입 제네릭 (C에는 템플릿이 없다 — `_Generic` 이 일부 대체) ③ 컴파일 타임 상수가 필요한 곳 (배열 크기, `case` 라벨, `#if`, `_Static_assert`) — inline 함수 호출은 상수식이 아니다 ④ 호출자 문맥을 바꾸는 것 (`return`, 임계 구역 감싸기).

</details>

<details>
<summary>Q4. `#` 와 `##` 의 동작과, 왜 XSTR/XCAT 처럼 두 단계를 거치나요?</summary>

`#param` 은 인자의 철자를 문자열 리터럴로, `a ## b` 는 두 토큰을 하나로 붙인다. C11 6.10.3.1 에 따라 `#`/`##` 에 붙은 매개변수는 **확장 전** 인자를 쓴다. 그래서 `STR(__LINE__)` 은 `"__LINE__"`. 바깥 매크로 `XSTR(x) STR(x)` 의 `x` 는 `#` 에 안 붙어 있으므로 먼저 펼쳐져서 `STR(42)` 가 되고 `"42"` 가 나온다. `##` 도 같아서 고유 이름 생성 `XCAT(tmp_, __LINE__)` 에 두 단계가 필요하다. 펌웨어 예: 빌드 버전 문자열 `"fw v" XSTR(FW_MAJOR) "." XSTR(FW_MINOR)`.

</details>

<details>
<summary>Q5. 인터럽트 비활성화 매크로를 작성해 보세요. 중첩되면? ISR 안에서 불리면?</summary>

`do { uint32_t pm = __get_PRIMASK(); __disable_irq(); body; __set_PRIMASK(pm); } while (0)`. 무조건 `__enable_irq()` 로 끝내면 중첩 시 바깥 구역이 보호를 잃고, ISR 이나 이미 IRQ 가 꺼진 부트 코드에서 불렸을 때 IRQ 를 멋대로 켠다. 꼬리질문: "PRIMASK 대신 BASEPRI 는?" → Cortex-M3 이상에서 BASEPRI 는 특정 우선순위 이하만 막아 고우선순위(예: 모터 제어) 인터럽트는 계속 받는다 — FreeRTOS `configMAX_SYSCALL_INTERRUPT_PRIORITY` 방식. "멀티코어면?" → PRIMASK 는 자기 코어만 막으므로 스핀락/하드웨어 세마포어가 추가로 필요.

</details>

<details>
<summary>Q6. statement expression 과 __typeof__ 는 표준 C 인가요? MISRA 에서는?</summary>

아니다. `({ ... })` 는 GCC 확장(clang 지원, C23 에도 없음). `typeof` 는 C23 에서 표준이 됐고 그 전엔 GNU 확장 — `-std=c11` 에서는 `__typeof__` 철자만 쓸 수 있다. `-pedantic` 이면 경고가 나고 `__extension__` 으로 끈다. MISRA C:2012 는 언어 확장을 쓰지 말라고 권고하고(Rule 1.2, advisory — 쓰면 deviation 기록), 함수형 매크로보다 함수를 권장한다(Dir 4.9 advisory). 그래서 안전 규격 코드에서는 `static inline` + 타입별 함수(또는 `_Generic`)가 기본값이다.

</details>

<details>
<summary>Q7. CHECK/ASSERT 매크로가 FAIL 메시지에 식 자체를 찍을 수 있는 원리는?</summary>

`#expr` 이 인자 철자를 문자열로 만들고, `__FILE__`/`__LINE__` 은 매크로가 **사용된 위치**에서 펼쳐지기 때문이다 (함수였다면 함수 정의 위치가 찍힌다). 이 레슨의 `CHECK` 자체가 do-while(0) + `#expr` 예제다. 펌웨어에서는 문자열이 플래시를 먹으므로 릴리스 빌드에선 `#expr` 대신 `__LINE__` 과 파일 ID 만 남기거나, `NDEBUG` 로 통째로 비우는 경우가 많다 (추가 연습 5).

</details>

## 9. 추가 연습

### 연습 1 — DIV_ROUND_UP 과 BITS_TO_BYTES

`DIV_ROUND_UP(n, d)` (올림 나눗셈)과 이를 쓰는 `BITS_TO_BYTES(bits)` 를 써라. `DIV_ROUND_UP(9, 2 + 2) == 3` 이어야 한다.

<details>
<summary>답 보기</summary>

`#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))`. 괄호가 없으면 `(9 + 2 + 2 - 1) / 2 + 2 = 8`. 버퍼 블록 수, DMA 전송 단위 수, 비트맵 바이트 수 계산에 매일 쓴다. 주의: `d` 를 두 번 평가하고, `n + d - 1` 이 오버플로할 수 있다 (`n` 이 타입 최대값 근처면 `n / d + (n % d != 0)` 가 안전).

</details>

### 연습 2 — `##` 와 `__LINE__`: 고유 이름 만들기

`CAT(reg_, __LINE__)` 와 `XCAT(reg_, __LINE__)` 는 각각 무엇이 되나?

<details>
<summary>답 보기</summary>

실제 `cc -E` 결과: `CAT(reg_, __LINE__)` → `reg___LINE__`, `XCAT(reg_, __LINE__)` → `reg_4` (그 줄 번호). `##` 옆 인자는 펼쳐지지 않으므로 한 단계 거쳐야 한다. 매크로 안에서 충돌 없는 임시 변수·정적 단언 이름을 만들 때 쓴다 (리눅스 `__UNIQUE_ID` 는 `__COUNTER__` 로 같은 일을 한다).

</details>

### 연습 3 — X-macro 맛보기: 레지스터 표 자동 생성

레지스터 목록 `X(CR1, 0x00) X(CR2, 0x04) ...` 을 **한 곳에만** 쓰고 enum, 이름 문자열 표, 오프셋 표를 생성하라.

<details>
<summary>답 보기</summary>

`#define UART_REG_LIST(X) X(CR1, 0x00u) X(CR2, 0x04u) ...` 를 두고, `#define AS_ENUM(n, o) REG_##n,` / `#define AS_NAME(n, o) #n,` / `#define AS_OFF(n, o) o,` 를 차례로 넘긴다. 레지스터를 추가할 때 목록 한 줄만 고치면 세 표가 항상 동기화된다 — 레지스터 덤프 셸 명령, 에러 코드 → 문자열 표, 상태 머신 상태 이름에 흔히 쓴다. `REG_COUNT` 를 enum 마지막에 두면 표 크기 검사도 공짜.

</details>

### 연습 4 — 표준 C11 `_Generic` 으로 타입 제네릭 MAX

GNU 확장 없이, 인자를 한 번만 평가하면서 `int`/`unsigned`/`double` 에 맞는 함수를 고르는 `MAX_G(a, b)` 를 써라.

<details>
<summary>답 보기</summary>

`#define MAX_G(a, b) _Generic((a) + (b), int: max_i, unsigned: max_u, double: max_d)((a), (b))`. `_Generic` 의 제어식 `(a) + (b)` 는 **평가되지 않고** 타입만 본다(C11 6.5.1.1p3) — `+` 를 쓰면 usual arithmetic conversion 이 적용되어 섞인 타입도 공통 타입으로 고른다. 실제 평가는 함수 호출 인자에서 한 번. `<tgmath.h>` 가 이 방식이다. `-pedantic` 에서도 경고 없음.

</details>

### 연습 5 — 펌웨어 ASSERT: `#expr` + `__LINE__` + do-while(0)

실패한 식 문자열과 줄 번호를 기록하는 `FW_ASSERT(expr)` 를 쓰고, `if/else` 안에서도 쓸 수 있게 하라.

<details>
<summary>답 보기</summary>

`#define FW_ASSERT(expr) do { if (!(expr)) { g_fail_count++; g_last_expr = #expr; g_last_line = __LINE__; } } while (0)`. `!(expr)` 의 괄호를 빼면 `FW_ASSERT(a == b)` 가 `!a == b` 가 된다. 실제 제품에서는 여기서 `__BKPT()` 로 디버거를 세우거나, 레지스터 덤프를 noinit RAM 에 남기고 리셋한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 14/14 passed
```

## 10. 핵심 정리

- 매크로는 텍스트 치환이다. 의심되면 `cc -E -P file.c` 로 펼쳐 본다.
- 식 매크로: **인자마다 괄호 + 전체 괄호** — `((x) * (x))`, `((a) > (b) ? (a) : (b))`.
- 문장 매크로: `do { ... } while (0)`, 끝에 `;` 넣지 않기. 본문 안 `break`/`continue`/`return` 주의.
- 괄호는 다중 평가를 못 고친다. `i++`, FIFO `DR`, read-to-clear, `volatile` 인자는 한 번만 읽게: `static inline` (표준) 또는 `__extension__ ({ __typeof__(a) a_ = (a); ... })` (GNU 확장).
- `MAX(i++, j)` 는 UB 가 아니라 "정의된 두 번 증가" — `?:` 뒤 시퀀스 포인트.
- 매크로 안 지역 변수는 `name_` 처럼 튀는 이름 + `-Wshadow` (이름 포획은 `-Wall -Wextra` 로 안 잡힌다).
- `#` 문자열화, `##` 토큰 붙이기 — 둘 다 인자를 펼치지 않으므로 값이 필요하면 `XSTR`/`XCAT` 로 한 단계 더.
- 임계 구역은 PRIMASK **저장 → 끄기 → 복원**. 무조건 enable 은 중첩·ISR 에서 버그.

## 참고자료

- C11 표준 (N1570) 6.10.3 Macro replacement — 6.10.3.1 인자 치환, 6.10.3.2 `#`, 6.10.3.3 `##`, 6.10.3.4 재스캔; 6.5.15 조건 연산자 시퀀스 포인트; 6.5.1.1 `_Generic` — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- GCC Manual — [Statement Exprs](https://gcc.gnu.org/onlinedocs/gcc/Statement-Exprs.html), [Typeof](https://gcc.gnu.org/onlinedocs/gcc/Typeof.html), [Alternate Keywords (`__extension__`)](https://gcc.gnu.org/onlinedocs/gcc/Alternate-Keywords.html)
- GNU CPP Manual — [Macro Pitfalls (Operator Precedence, Swallowing the Semicolon, Duplication of Side Effects)](https://gcc.gnu.org/onlinedocs/cpp/Macro-Pitfalls.html), [Stringizing](https://gcc.gnu.org/onlinedocs/cpp/Stringizing.html), [Concatenation](https://gcc.gnu.org/onlinedocs/cpp/Concatenation.html)
- ARM CMSIS-Core — `__disable_irq`, `__get_PRIMASK`, `_VAL2FLD`/`_FLD2VAL`: <https://arm-software.github.io/CMSIS_6/latest/Core/index.html>
- Linux `include/linux/minmax.h` — 타입 검사가 있는 `min()/max()`, `swap()`
- MISRA C:2012 Dir 4.9 (함수형 매크로보다 함수), Rule 1.2 (언어 확장)
