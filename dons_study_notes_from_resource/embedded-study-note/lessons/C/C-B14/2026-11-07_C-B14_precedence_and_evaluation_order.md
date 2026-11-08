---
id: C-B14
time: 50분
updated: 2026-11-07
---
# C-B14 연산자 우선순위와 평가 순서 함정

## 학습 목표

- `&` `^` `|` 가 `==` 보다, `<<` 가 `+` 보다 **늦게** 묶인다는 것을 알고 `reg & MASK == VAL` 류 버그를 한눈에 찾는다.
- **우선순위(precedence)** 와 **평가 순서(order of evaluation)** 가 서로 다른 개념임을 설명한다 — 괄호는 묶음만 정하지 순서는 정하지 않는다.
- 스니펫을 C11 기준으로 **정의됨 / 미지정(unspecified) / 정의되지 않은 동작(UB) / 컴파일 에러** 로 정확히 분류한다 (C11 6.5p2, 6.5.2.2p10).
- 시퀀스 포인트(sequence point) — `&&` `||` `?:` `,` 와 완전식(full expression) 끝 — 를 이용해 부작용(side effect)의 순서를 고정한다.
- 컴파일러 경고(`-Wparentheses`, `-Wshift-op-parentheses`, `-Wlogical-not-parentheses`, `-Wunsequenced`)가 잡는 것과 **못 잡는 것**을 구분한다.

## 1. 문제

레지스터 코드는 `&`, `<<`, `!`, `++` 가 한 줄에 몰려 있어서 우선순위 실수 하나가 "가끔 안 되는 드라이버"가 된다. 면접관이 종이에 `if (reg & MASK == VAL)` 를 써 놓고 "뭐가 문제죠?" 라고 묻는 건 고전 중의 고전이고, 이어서 "`i = i++` 는?", "`f() + g()` 는 어느 쪽이 먼저?" 로 **UB 와 미지정의 차이**를 파고든다.

핵심 그림은 C 우선순위 사다리다. 위가 먼저 묶인다. 함정은 **비트 연산자 `& ^ |` 가 비교 `==` 아래**에 있고 **시프트가 `+ -` 아래**에 있다는 것.

```svg
<svg viewBox="0 0 720 410" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">C 연산자 우선순위 (위 = 먼저 묶임)</text>
  <g fill="none" stroke="currentColor">
    <rect x="40" y="32" width="300" height="22"/><rect x="40" y="56" width="300" height="22"/>
    <rect x="40" y="80" width="300" height="22"/><rect x="40" y="104" width="300" height="22"/>
    <rect x="40" y="128" width="300" height="22"/><rect x="40" y="152" width="300" height="22"/>
    <rect x="40" y="176" width="300" height="22"/><rect x="40" y="200" width="300" height="22"/>
    <rect x="40" y="224" width="300" height="22"/><rect x="40" y="248" width="300" height="22"/>
    <rect x="40" y="272" width="300" height="22"/><rect x="40" y="296" width="300" height="22"/>
    <rect x="40" y="320" width="300" height="22"/><rect x="40" y="344" width="300" height="22"/>
    <rect x="40" y="368" width="300" height="22"/>
  </g>
  <rect x="40" y="128" width="300" height="22" class="f-warn"/>
  <rect x="40" y="200" width="300" height="70" class="f-warn"/>
  <rect x="40" y="176" width="300" height="22" class="f-acc"/>
  <rect x="40" y="104" width="300" height="22" class="f-acc"/>
  <g fill="currentColor" font-size="12">
    <text x="12" y="48">1</text><text x="12" y="72">2</text><text x="12" y="96">3</text><text x="12" y="120">4</text>
    <text x="12" y="144">5</text><text x="12" y="168">6</text><text x="12" y="192">7</text><text x="12" y="216">8</text>
    <text x="12" y="240">9</text><text x="8" y="264">10</text><text x="8" y="288">11</text><text x="8" y="312">12</text>
    <text x="8" y="336">13</text><text x="8" y="360">14</text><text x="8" y="384">15</text>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="50" y="48">()  []  -&gt;  .  x++  x--</text>
    <text x="50" y="72">++x --x ! ~ - *p &amp;x (T) sizeof</text>
    <text x="50" y="96">*  /  %</text>
    <text x="50" y="120">+  -</text>
    <text x="50" y="144">&lt;&lt;  &gt;&gt;</text>
    <text x="50" y="168">&lt;  &lt;=  &gt;  &gt;=</text>
    <text x="50" y="192">==  !=</text>
    <text x="50" y="216">&amp;</text>
    <text x="50" y="240">^</text>
    <text x="50" y="264">|</text>
    <text x="50" y="288">&amp;&amp;</text>
    <text x="50" y="312">||</text>
    <text x="50" y="336">?:          (오른쪽 결합)</text>
    <text x="50" y="360">=  +=  &amp;=  ... (오른쪽 결합)</text>
    <text x="50" y="384">,</text>
  </g>
  <g fill="none" stroke="currentColor">
    <path d="M345 139 C 380 139, 380 115, 345 115"/>
    <path d="M345 235 C 400 235, 400 187, 345 187"/>
  </g>
  <g fill="currentColor">
    <text x="395" y="122">a &lt;&lt; 2 + 1  ==  a &lt;&lt; (2 + 1)</text>
    <text x="395" y="140">시프트가 덧셈보다 늦다</text>
    <text x="410" y="200">reg &amp; MASK == VAL</text>
    <text x="410" y="218">==  reg &amp; (MASK == VAL)</text>
    <text x="410" y="236">비트 연산이 비교보다 늦다 (B 언어의 유산)</text>
    <text x="395" y="70">단항 ! 는 &amp; 보다 훨씬 먼저:</text>
    <text x="395" y="88">!flag &amp; mask  ==  (!flag) &amp; mask</text>
    <text x="395" y="340">대입이 쉼표보다 먼저:</text>
    <text x="395" y="358">w = 1, 2, 3;  ==  (w = 1), 2, 3;</text>
    <text x="395" y="290">&amp;&amp; || ?: , 에는 시퀀스 포인트</text>
    <text x="395" y="308">→ 순서까지 정해지는 유일한 연산자들</text>
  </g>
</svg>
```

하지만 우선순위는 **"무엇이 무엇과 묶이는가(grouping)"** 만 정한다. **"무엇을 먼저 계산하는가(evaluation order)"** 는 별개다. `f() + g() * h()` 에서 `*` 가 먼저 *묶이지만*, 세 함수 중 무엇이 먼저 *호출되는지*는 정해져 있지 않다 (C11 6.5p3).

### Part A — 12개 퀴즈: 예측하고, 분류하라

| # | 스니펫 | 준비 값 |
|---|---|---|
| Q1 | `reg & MASK == VAL` | `reg=0x20`, `MASK=VAL=0x20u` |
| Q2 | `a << 2 + 1` | `a=1` |
| Q3 | `v = *p++;` 그리고 `(*p)++;` | `arr={10,20,30}`, `p=arr` |
| Q4 | `x = y == z;` | `y=z=7` |
| Q5 | `!flag & mask` | `flag=0x02`, `mask=0x04` |
| Q6 | `sel ? b : c = d;` | — |
| Q7 | `i = i++;` | `i=5` |
| Q8 | `s = f() + g();` (f, g 는 호출 로그를 남김) | — |
| Q9 | `a[i] = i++;` | `i=1` |
| Q10 | `(pos > 0) \|\| (++cnt > 0)` / `&&` 버전 | `pos=1`, `cnt=0` |
| Q11 | `w = (1, 2, 3);` 그리고 `w = 1, 2, 3;` | — |
| Q12 | `sz = sizeof nn++;` | `nn=5` |

- **A-1** 값이 하나로 정해지는 8개 문항(Q1–Q5, Q10–Q12)의 결과 12개를 `predict[]` 에 **실행 전에** 적는다. 하네스가 스니펫을 *쓰인 그대로* 평가해 대조한다.
- **A-2** 12개 모두를 `K_DEFINED / K_UNSPEC / K_UB / K_ERROR` 로 분류해 `kind_of[]` 에 적는다.
- 그 다음 `quiz_demo.c` 를 **`-Werror` 없이** 빌드해서 clang 경고와 실제 출력을 확인한다 (UB 스니펫이 들어 있으므로 strict 빌드 대상이 아니다).

### Part B — 레지스터 체크 함수 6개 고치기

각 함수 위 주석에 **원본(버그) 코드**가 있다. 버그를 찾아 올바른 형태로 다시 쓴다.

| 함수 | 원본(버그) | 할 일 |
|---|---|---|
| B1 `rxne_set(sr)` | `sr & SR_RXNE == SR_RXNE` | RXNE(bit 5)가 1인지 |
| B2 `make_u16(hi, lo)` | `hi << 8 + lo` | 두 바이트 → 16비트 |
| B3 `flags_clear(sr, mask)` | `!sr & mask` | mask 비트가 하나도 없으면 true |
| B4 `err_bump(cnt)` | `*cnt++;` | 카운터 +1 |
| B5 `uart_drain(u, out, max)` | `while (uart_pop(u, &out[n]) && n < max) n++;` | 최대 max 개만 꺼내고 나머지는 FIFO 에 남김 |
| B6 `spi_read_be16(s)` | `spi_xfer(s) << 8 \| spi_xfer(s)` | 먼저 온 바이트가 상위 |

### Part C — UB 가 숨은 프레임 파서 다시 쓰기

UART 로 들어오는 프레임 `[0xA5][LEN_HI][LEN_LO][CMD][payload×LEN][CHK]` 를 파싱한다. CHK 는 LEN_HI부터 payload 끝까지의 XOR. 원본은 **UB 2개(①②) + 우선순위 버그 1개(③)** 를 품고 있다 (`buggy_originals.c` 에 그대로 있음). `parse_frame()` 을 부작용이 한 문장에 하나뿐인 코드로 다시 써서 정상/SYNC 오류/CHK 오류/길이 오류 프레임을 모두 통과시킨다.

## 2. Problem (English)

> **Operator precedence and evaluation order.**
>
> 1. Predict the value of each snippet in the table (Q1–Q5, Q10–Q12) without running it, then classify all twelve under C11 as *well-defined*, *unspecified*, *undefined behavior*, or *constraint violation / compile error*. Cite the rule (e.g. C11 6.5p2) for every non-well-defined one.
> 2. Fix six status-register helpers whose original code is `sr & SR_RXNE == SR_RXNE`, `hi << 8 + lo`, `!sr & mask`, `*cnt++;`, `while (uart_pop(u, &out[n]) && n < max) n++;` and `spi_xfer(s) << 8 | spi_xfer(s)`.
> 3. Rewrite `int parse_frame(const uint8_t *p, size_t n, frame_t *f)` for the frame `A5 | LEN(be16) | CMD | payload | CHK`, `CHK = XOR(LEN_HI..payload)`, `LEN <= 16`, `n == LEN + 5`. Return 0 on success, -1 otherwise. The original uses `f->len = *p++ << 8 | *p++;` and `f->payload[i] = p[i++];` — explain why both are undefined.
>
> Example: `parse_frame({A5 00 03 10 01 02 03 13}, 8, &f) == 0`, `f.len == 3`, `f.cmd == 0x10`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "7/47 passed" — 예측 12 + 분류 12 + Part B 15 + Part C 8 을 모두 채워 47/47
```

경고와 UB 를 직접 보는 데모 2개 (일부러 **`-Werror` 없이**):

```bash
cc -std=c11 -Wall -Wextra -O0 quiz_demo.c -o quiz_demo && ./quiz_demo     # 12 스니펫 그대로
cc -std=c11 -Wall -Wextra -DSHOW_Q6 -fsyntax-only quiz_demo.c             # Q6 컴파일 에러
cc -std=c11 -Wall -Wextra -fsyntax-only buggy_originals.c                 # Part B/C 원본 경고
cc -std=c11 -w -O0 buggy_originals.c -o buggy0 && ./buggy0                # 원본 실행 결과
```

하네스의 `actual()` 은 퀴즈 스니펫을 **쓰인 그대로** 평가하되 `#pragma clang diagnostic ignored` 로 그 함수의 경고만 끈다 (그래야 `-Werror` strict 빌드가 통과한다). UB 스니펫(Q7, Q9)과 에러 스니펫(Q6)은 하네스에 넣지 않았다 — 분류만 한다.

> 맥 SDK 27.2에서 `ld: tapi error` 가 나면 `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` 후 다시 빌드.

## 4. 힌트

<details>
<summary>힌트 1 — 괄호를 머릿속으로 그려라</summary>

사다리에서 더 위에 있는 연산자 쪽에 먼저 괄호를 친다. `reg & MASK == VAL` → `==` 가 위 → `reg & (MASK == VAL)`. `MASK == VAL` 은 `int` 0 아니면 1 이다.

</details>

<details>
<summary>힌트 2 — 후위 ++ 는 단항 * 보다 위</summary>

`*p++` 는 `*(p++)`: 값은 "옛 p 가 가리키던 것", 그리고 p 가 한 칸 이동. 값을 올리고 싶으면 `(*p)++` 또는 `++*p`.

</details>

<details>
<summary>힌트 3 — UB 판정은 한 문장으로</summary>

"**같은 스칼라 객체**에 대한 부작용이, 다른 부작용 또는 그 객체 값 읽기와 **순서가 없으면(unsequenced)** UB" (C11 6.5p2). `=` 의 저장(side effect)은 양쪽 피연산자의 *값 계산* 뒤로만 순서가 정해지고, `i++` 의 *저장* 과는 순서가 없다 (6.5.16p3).

</details>

<details>
<summary>힌트 4 — 함수 호출은 섞이지 않는다</summary>

`f() + g()` 에서 두 함수 몸체는 서로 끼어들지 않는다 — **불확정 순서(indeterminately sequenced)**, 즉 둘 중 하나가 먼저지만 어느 쪽인지 미지정 (6.5.2.2p10). 그래서 UB 가 아니라 *미지정*.

</details>

<details>
<summary>힌트 5 — Part B5/B6/C 는 "한 문장에 부작용 하나"</summary>

순서가 중요한 부작용(FIFO pop, SPI 전송, 포인터 전진)은 `;` 로 나눈 별도 문장에 둔다. `&&` 는 왼쪽부터 평가하고 왼쪽이 거짓이면 오른쪽을 **아예 평가하지 않는다** — 경계 검사를 왼쪽에.

</details>

## 5. 풀이

### Part A — 12개 퀴즈 정답과 근거

| # | 실제로 묶이는 모양 | 결과 (실측) | 분류 | 근거 |
|---|---|---|---|---|
| Q1 | `reg & (MASK == VAL)` = `0x20 & 1` | **0** (의도는 1) | 정의됨 | `==`(7) > `&`(8) |
| Q2 | `a << (2 + 1)` | **8** (의도는 5) | 정의됨 | `+`(4) > `<<`(5) |
| Q3 | `*(p++)` / `(*p)++` | v=**10**, 그 뒤 `*p`=**20** / arr[0]=**11**, p 그대로 | 정의됨 | 후위(1) > 단항(2) |
| Q4 | `x = (y == z)` | **1** (7 아님) | 정의됨 | `==` 결과는 `int` 0/1 (6.5.9p3) |
| Q5 | `(!flag) & mask` = `0 & 4` | **0** (의도 `!(flag & mask)` = 1) | 정의됨 | 단항 `!` 가 먼저 |
| Q6 | `(sel ? b : c) = d` | 컴파일 에러 | **에러** | `?:` 결과는 lvalue 아님 (6.5.15 각주) → 대입 제약 6.5.16p2 위반 |
| Q7 | `i = i++` | 실측 i=5 | **UB** | `=` 저장과 `++` 저장이 unsequenced (6.5p2) |
| Q8 | `f() + g()` | 3, 호출 순서 실측 `fg` | **미지정** | 호출은 불확정 순서 (6.5.2.2p10), 피연산자 평가 순서 미지정 (6.5p3) |
| Q9 | `a[i] = i++` | 실측 `t[2]=1`, i=2 | **UB** | 왼쪽의 `i` 읽기와 `i++` 저장이 unsequenced (6.5p2) |
| Q10 | `\|\|` 왼쪽이 참 → 오른쪽 생략 | `\|\|` 후 cnt=**0**, `&&` 후 cnt=**1** | 정의됨 | 단락 평가(short-circuit) 6.5.13p4, 6.5.14p4 |
| Q11 | `w = 3` / `(w = 1), 2, 3` | **3** / **1** | 정의됨 | 쉼표는 왼→오 + 시퀀스 포인트, 대입이 쉼표보다 위 (6.5.17) |
| Q12 | `sizeof` 피연산자 평가 안 함 | sz=4, nn=**5** | 정의됨 | 6.5.3.4p2 (VLA 만 예외) |

`quiz_demo.c` 실제 출력 (`-O0`, `-O2` 동일):

```text
Q1  reg & MASK == VAL      -> 0
Q2  a << 2 + 1             -> 8
Q3  v = *p++ -> v=10, *p=20 | (*p)++ -> arr[0]=11, p==arr:1
Q4  x = y == z             -> x=1
Q5  !flag & mask           -> 0
Q7  i = i++ (i=5)          -> i=5   [UB]
Q8  f() + g()              -> 3, call order fg [unspecified]
Q9  t[k] = k++ (k=1)       -> t={0,0,1,0} k=2 [UB]
Q10 || then &&             -> cnt after ||=0, after &&=1
Q11 (1,2,3) -> 3 ; w2 = 1, 2, 3 -> 1
Q12 sizeof nn++            -> 4, nn=5
```

Q7/Q9 의 숫자는 **"이 컴파일러가 이번에 낸 값"일 뿐 답이 아니다.** UB 라서 다른 컴파일러·최적화 수준·주변 코드에서 얼마든지 바뀐다. Q9 에서 clang 은 `k++` 를 먼저 하고 증가된 k 로 인덱싱했다(`t[2]`) — "왼쪽이 먼저"라는 직관과 반대다.

경고 (Apple clang 21, `-Wall -Wextra`, 노트 줄은 생략):

```text
quiz_demo.c:18:54: warning: & has lower precedence than ==; == will be evaluated first [-Wparentheses]
quiz_demo.c:20:57: warning: operator '<<' has lower precedence than '+'; '+' will be evaluated first [-Wshift-op-parentheses]
quiz_demo.c:30:50: warning: logical not is only applied to the left hand side of this bitwise operator [-Wlogical-not-parentheses]
quiz_demo.c:55:26: warning: expression with side effects has no effect in an unevaluated context [-Wunevaluated-expression]
quiz_demo.c:37:10: warning: multiple unsequenced modifications to 'i' [-Wunsequenced]
quiz_demo.c:42:13: warning: unsequenced modification and access to 'k' [-Wunsequenced]
quiz_demo.c:50:10: warning: left operand of comma operator has no effect [-Wunused-value]
quiz_demo.c:52:16: warning: expression result unused [-Wunused-value]
```

```text
$ cc -std=c11 -Wall -Wextra -DSHOW_Q6 -fsyntax-only quiz_demo.c
quiz_demo.c:33:17: error: expression is not assignable
   33 |     sel ? b : c = d;           /* C: 컴파일 에러. C++: sel ? b : (c = d) */
```

- 이 clang 은 위 경고들이 **`-Wall` 없이도 기본으로 켜져** 있었다 (`cc -std=c11 -fsyntax-only` 로 확인). GCC 에서는 `-Wall` 에 포함된 `-Wparentheses`(시프트·`&`·대입 조건 모두 포함), `-Wsequence-point`(= clang `-Wunsequenced`), `-Wlogical-not-parentheses` 가 담당한다 — arm-none-eabi-gcc 빌드에 `-Wall` 이 꼭 있어야 하는 이유.
- **UBSan 은 unsequenced 수정을 잡지 못한다** (`-fsanitize=undefined` 로 돌려도 Q7/Q9 에 런타임 보고 없음). 컴파일 타임 경고가 유일한 그물이다.
- Q6 을 C++ 로 컴파일하면 문법이 달라 `sel ? b : (c = d)` 로 묶이고 정상 컴파일된다 (실측: `c=3` 그대로, sel 이 1 이라 대입 안 일어남).

#### 우선순위 ≠ 평가 순서 — 시퀀싱 그림

```svg
<svg viewBox="0 0 720 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">i = i++ (UB)  vs  i++, i++ (정의됨)</text>
  <rect x="20" y="40" width="150" height="30" class="f-mut" stroke="currentColor"/>
  <text x="30" y="60" fill="currentColor">값 계산: i++ → 5</text>
  <rect x="20" y="110" width="150" height="30" class="f-warn" stroke="currentColor"/>
  <text x="30" y="130" fill="currentColor">부작용 ①: i ← 6</text>
  <rect x="200" y="110" width="150" height="30" class="f-warn" stroke="currentColor"/>
  <text x="210" y="130" fill="currentColor">부작용 ②: i ← 5</text>
  <g stroke="currentColor" fill="none">
    <path d="M95 70 L95 108"/><path d="M90 102 L95 108 L100 102"/>
    <path d="M150 70 L270 108"/><path d="M262 105 L270 108 L266 100"/>
  </g>
  <text x="195" y="76" fill="currentColor" font-size="12">= 의 저장은 값 계산 뒤</text>
  <text x="181" y="132" fill="currentColor" font-weight="bold">?</text>
  <text x="20" y="168" fill="currentColor">①과 ② 사이엔 화살표가 없다</text>
  <text x="20" y="186" fill="currentColor">→ 같은 객체에 순서 없는 쓰기 2개</text>
  <text x="20" y="204" fill="currentColor">→ C11 6.5p2: 정의되지 않은 동작</text>
  <line x1="380" y1="35" x2="380" y2="255" stroke="currentColor" stroke-dasharray="4 3"/>
  <rect x="400" y="40" width="130" height="30" class="f-ok" stroke="currentColor"/>
  <text x="410" y="60" fill="currentColor">i++  (5→6)</text>
  <rect x="400" y="130" width="130" height="30" class="f-ok" stroke="currentColor"/>
  <text x="410" y="150" fill="currentColor">i++  (6→7)</text>
  <line x1="390" y1="100" x2="700" y2="100" stroke="currentColor" stroke-width="3"/>
  <text x="545" y="94" fill="currentColor" font-size="12">쉼표 = 시퀀스 포인트</text>
  <g stroke="currentColor" fill="none"><path d="M465 70 L465 128"/><path d="M460 122 L465 128 L470 122"/></g>
  <text x="400" y="190" fill="currentColor">시퀀스 포인트가 만드는 순서:</text>
  <text x="400" y="210" fill="currentColor" font-family="monospace">&amp;&amp;  ||  ?:  ,  완전식 끝(;)</text>
  <text x="400" y="230" fill="currentColor">+ 함수 호출 직전 (인자 평가 후)</text>
  <text x="400" y="250" fill="currentColor">인자 사이 쉼표는 연산자가 아니다!</text>
</svg>
```

C11 은 "시퀀스 포인트" 대신 **sequenced-before** 관계로 정의를 다시 썼지만 결론은 같다. 핵심 규칙 3개:

1. **6.5p2 (UB 규칙)** — 스칼라 객체의 부작용이 *같은 객체의 다른 부작용* 또는 *같은 객체 값을 쓰는 값 계산* 과 unsequenced 면 UB. 허용되는 순서 중 하나라도 그러면 UB.
2. **6.5p3** — 따로 정하지 않은 한, 하위식의 부작용·값 계산은 unsequenced. (`+`, `*`, `<<`, `=` 의 두 피연산자, 함수 인자들끼리 모두 해당.)
3. **순서를 만드는 것** — `&&`, `||` (왼→오, 오른쪽 평가 시 사이에 시퀀스 포인트), `?:` (첫 피연산자 뒤), 쉼표 연산자, 완전식(full expression)의 끝(식 문장 `;`, `if`/`while` 조건, `return` 식, 초기화식), 함수 호출 직전. 함수 호출끼리는 섞이지 않는다(불확정 순서).

#### 분류 순서도

```svg
<svg viewBox="0 0 720 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">스니펫 분류하기</text>
  <rect x="10" y="40" width="210" height="44" stroke="currentColor" fill="none"/>
  <text x="20" y="58" fill="currentColor">문법/제약 위반?</text>
  <text x="20" y="76" fill="currentColor" font-size="12">(lvalue 아닌 곳에 대입 등)</text>
  <rect x="250" y="40" width="220" height="44" stroke="currentColor" fill="none"/>
  <text x="260" y="58" fill="currentColor">같은 객체에 부작용 + (부작용</text>
  <text x="260" y="76" fill="currentColor">또는 읽기) 가 unsequenced?</text>
  <rect x="500" y="40" width="210" height="44" stroke="currentColor" fill="none"/>
  <text x="510" y="58" fill="currentColor">평가/호출 순서에 따라</text>
  <text x="510" y="76" fill="currentColor">결과·로그가 달라지나?</text>
  <g stroke="currentColor" fill="none">
    <path d="M220 62 L248 62"/><path d="M242 57 L248 62 L242 67"/>
    <path d="M470 62 L498 62"/><path d="M492 57 L498 62 L492 67"/>
    <path d="M115 84 L115 128"/><path d="M110 122 L115 128 L120 122"/>
    <path d="M360 84 L360 128"/><path d="M355 122 L360 128 L365 122"/>
    <path d="M605 84 L605 128"/><path d="M600 122 L605 128 L610 122"/>
    <path d="M660 84 L660 190"/><path d="M655 184 L660 190 L665 184"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="222" y="37">아니오</text><text x="472" y="37">아니오</text>
    <text x="120" y="110">예</text><text x="365" y="110">예</text><text x="610" y="110">예</text><text x="665" y="140">아니오</text>
  </g>
  <rect x="40" y="130" width="150" height="34" class="f-warn" stroke="currentColor"/>
  <text x="50" y="152" fill="currentColor">컴파일 에러 (Q6)</text>
  <rect x="285" y="130" width="150" height="34" class="f-warn" stroke="currentColor"/>
  <text x="295" y="152" fill="currentColor">UB (Q7, Q9)</text>
  <rect x="510" y="130" width="130" height="34" class="f-acc" stroke="currentColor"/>
  <text x="520" y="152" fill="currentColor">미지정 (Q8)</text>
  <rect x="560" y="192" width="150" height="34" class="f-ok" stroke="currentColor"/>
  <text x="570" y="214" fill="currentColor">정의됨 (나머지)</text>
  <text x="10" y="200" fill="currentColor">구현 정의(implementation-defined)는 미지정 +</text>
  <text x="10" y="218" fill="currentColor">"문서화 의무" (예: 음수 &gt;&gt; 는 C-B03 참고).</text>
  <text x="10" y="240" fill="currentColor">f() + g() 는 함수 몸체가 섞이지 않으므로 UB 까지는 아니다.</text>
</svg>
```

Q8 이 UB 가 아닌 이유를 정확히: `f` 와 `g` 가 같은 전역을 수정해도, 각 함수 몸체 안의 부작용은 그 함수 안에서 순서가 정해져 있고 두 몸체는 **불확정 순서**(어느 한쪽이 통째로 먼저)라 unsequenced 가 아니다. 반면 `g_cnt++ + g_cnt++` 는 함수 경계가 없으므로 UB.

### Part B — 버그별 처방

| 함수 | 실제 동작 | 고친 코드 | 컴파일러 경고 |
|---|---|---|---|
| B1 | `sr & (1)` → **bit 0** 검사 | `(sr & SR_RXNE) != 0` | `-Wparentheses` |
| B2 | `hi << (8 + lo)` → 시프트 양 ≥ 32 이면 **UB** | `(uint16_t)((hi << 8) \| lo)` | `-Wshift-op-parentheses` |
| B3 | `(!sr) & mask` → sr≠0 이면 항상 0 | `!(sr & mask)` | `-Wlogical-not-parentheses` |
| B4 | `*(cnt++)` → 포인터만 이동, 값 그대로 | `(*cnt)++` 또는 `++*cnt` | `-Wunused-value` (간접적) |
| B5 | pop 이 먼저 → max 에 도달해도 1바이트 더 꺼내 `out[max]` 에 씀 | `while (n < max && uart_pop(u, &out[n])) n++;` | **없음** |
| B6 | 두 `spi_xfer` 호출 순서 **미지정** | `hi = spi_xfer(s); lo = spi_xfer(s);` 로 분리 | **없음** (`-Weverything` 도 조용) |

`buggy_originals.c` 를 실제로 돌린 결과 (`-O0`, `-O2` 동일):

```text
B1 rxne_set(0x20)=0  rxne_set(0x01)=1   (want 1, 0)
B2 make_u16(0x12,0x34)=0x0000            (want 0x1234)
B3 flags_clear(0,0x04)=0                 (want 1)
B4 errs after bump = {7,100}            (want {8,100})
B5 drain(max=3)=3 out[3]=0x04 left=1   (want 3, 0xEE, 2)
B6 spi_read_be16=0x1234                 (want 0x1234; clang 은 우연히 맞음)
C  parse_frame(ok)=-1 len=3 payload={0,1,2} (want 0, 3, {1,2,3})
```

- **B2** 는 단순 오답이 아니라 UB 다: `0x12 << 60`. ASan+UBSan 빌드에서 `runtime error: shift exponent 60 is too large for 32-bit type 'int'` 가 찍혔다. 같은 원본을 starter 하네스에 끼워 `-O2` 로 돌렸을 때는 `make_u16(0x12, 0x34) == 0x1234` CHECK 가 **PASS** 로 나왔다 (`-O0` 에선 FAIL) — UB 를 만난 최적화기가 비교식을 마음대로 접어버린 것. "테스트가 통과했으니 맞다"가 UB 앞에서는 성립하지 않는다. (재현: `starter.c` 의 `make_u16` 본문을 원본 한 줄 `return (uint16_t)(hi << 8 + lo);` 로 바꾸고 `cc -std=c11 -w -O0` / `-O2` 로 각각 빌드·실행 — Apple clang 21 에서 확인.)
- **B4** 의 정확한 의미: `*cnt++;` 는 `*(cnt++)` — 포인터 `cnt`(지역 복사본)를 올리고 옛 주소를 역참조한 값을 버린다. clang 은 "expression result unused" 만 알려준다 — 경고 문구가 버그를 직접 말해주지 않으니 읽을 줄 알아야 한다.
- **B5** 는 우선순위가 아니라 **단락 평가 순서** 버그다. `&&` 는 왼쪽부터 평가하므로 부작용 있는 `uart_pop` 이 경계 검사보다 먼저 실행된다. 결과: 버퍼 밖(`out[3]`)에 쓰고, 그 바이트는 FIFO 에서도 사라짐(left=1). 실제 칩에서는 RX 데이터 유실 + 스택 오염.
- **B6** 이 가장 위험하다. clang/arm64 는 왼쪽부터 호출해서 **우연히 맞는다**. 그런데 표준은 순서를 보장하지 않으므로 다른 컴파일러/버전/최적화에서 바이트가 뒤집힐 수 있고, 경고도 테스트도 못 잡는다. 규칙: **순서가 의미 있는 I/O 호출(SPI/I2C 전송, FIFO pop, volatile 레지스터 읽기)은 한 식에 두 번 넣지 않는다.**

### Part C — 파서의 UB 세 군데

| 원본 | 문제 | 고친 방법 |
|---|---|---|
| ① `f->len = *p++ << 8 \| *p++;` | `p` 를 순서 없이 두 번 수정 → **UB** (`-Wunsequenced: multiple unsequenced modifications to 'p'`) | 인덱스로 읽기: `(p[1] << 8) \| p[2]` — 부작용 0개 |
| ② `f->payload[i] = p[i++];` | 왼쪽 `i` 읽기와 `i++` 저장이 unsequenced → **UB** (`unsequenced modification and access to 'i'`). 실측: `payload={0,1,2}` (한 칸 밀림) | `for (i = 0; i < len; i++) f->payload[i] = p[4 + i];` |
| ③ `if (chk ^ p[f->len] != 0)` | `chk ^ (p[len] != 0)` → 올바른 프레임도 거부 (`-Wparentheses: ^ has lower precedence than !=`) | `(chk ^ p[4 + len]) != 0` 또는 `chk != p[4 + len]` |

추가로 정답 코드는 **검증 → 복사** 순서로 바꿨다: 길이와 CHK 를 모두 확인한 뒤에야 `f` 를 채운다. 원본처럼 검증 전에 `f->len` 을 써두면 실패한 프레임의 쓰레기가 호출자에게 남는다. 그리고 `n < 5` 검사를 맨 앞에 둬서 `p[1..4]` 를 읽기 전에 길이를 보장한다 (`||` 단락 평가 덕분에 `n < 5` 면 `p[0]` 도 읽지 않는다).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution            # 47/47 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o sol_san && ./sol_san   # 47/47, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `if (reg & MASK == VAL)` | `reg & (MASK == VAL)` | `if ((reg & MASK) == VAL)` |
| `if (reg & MASK != 0)` | `reg & (MASK != 0)` → bit 0 검사 | `if ((reg & MASK) != 0)` |
| `x = hi << 8 + lo` | `hi << (8 + lo)` | `(hi << 8) \| lo` (`\|` 는 `<<` 보다 아래라 괄호 없이도 맞지만 쓰는 게 낫다) |
| `if (!status & ERR)` | `(!status) & ERR` | `if (!(status & ERR))` |
| `*cnt++;` 로 카운트 | 포인터가 증가 | `(*cnt)++;` |
| `while (c = getc() != EOF)` | `c = (getc() != EOF)` → c 는 0/1 | `while ((c = getc()) != EOF)` (추가 연습 3) |
| `buf[i] = i++;`, `i = i++;`, `x = x++ + 1;` | 같은 객체를 순서 없이 수정/읽기 → UB | 부작용을 별도 문장으로 |
| `printf("%d %d", i, i++)`, `f(i++, i++)` | 인자 사이 쉼표는 연산자가 아님 → unsequenced → UB | 인자 계산을 먼저 지역 변수로 |
| `v = READ(SPI) << 8 \| READ(SPI)` | 호출 순서 미지정 | 두 문장으로 분리 |
| `if (fifo_pop(&b) && n < max)` | 부작용이 경계 검사보다 먼저 | `if (n < max && fifo_pop(&b))` |
| `n = sizeof(buf[i++])` 로 i 증가 기대 | `sizeof` 피연산자는 평가 안 됨 | `i++` 를 따로 |
| `"괄호를 쳤으니 순서도 정해졌다"` | 괄호는 묶음만 정함: `(f()) + (g())` 도 순서 미지정 | 문장 분리 |

## 8. 면접 Q&A

<details>
<summary>Q1. `if (reg & MASK == VAL)` 의 문제와, 왜 C 는 이런 우선순위를 갖게 됐나요?</summary>

`==` 가 `&` 보다 먼저 묶여 `reg & (MASK == VAL)` 이 된다 → `reg & 1` 또는 `reg & 0`. 역사: C 의 전신 B/초기 C 에는 `&&` `||` 가 없어서 `&` `|` 가 논리 연산도 겸했고, 그때는 `if (a == b & c == d)` 처럼 비교보다 낮은 게 자연스러웠다. 나중에 `&&` `||` 를 추가하면서 기존 코드를 깨지 않으려고 우선순위를 그대로 뒀다 (Ritchie, "The Development of the C Language"). 해결: 비트 연산은 항상 괄호, `-Wall` (GCC `-Wparentheses`).

</details>

<details>
<summary>Q2. UB, 미지정(unspecified), 구현 정의(implementation-defined)의 차이를 예와 함께 설명하세요.</summary>

- **UB**: 표준이 어떤 요구도 하지 않음. 최적화기가 "일어나지 않는다"고 가정해도 됨. 예: `i = i++`, `a[i] = i++`, 부호 있는 오버플로, 폭 이상 시프트.
- **미지정**: 허용된 결과 몇 가지 중 하나, 매번 같을 필요도 없고 문서화 의무도 없음. 예: `f() + g()` 호출 순서, 함수 인자 평가 순서.
- **구현 정의**: 미지정 + 컴파일러가 **문서화**해야 함. 예: 음수의 오른쪽 시프트, `char` 의 부호, 비트필드 배치.
꼬리질문 "UB 면 그냥 이상한 값이 나오는 거죠?" → 아니다. 프로그램 전체 의미가 사라진다 — 위 B2 처럼 테스트 비교식 자체가 접혀서 PASS 가 나올 수도 있다.

</details>

<details>
<summary>Q3. `i = i++ + 1;` 은 C 에서 UB 인가요? C++ 은요?</summary>

C(C11/C17/C23): UB — `=` 의 저장은 피연산자 *값 계산* 뒤로만 순서가 정해지고 `i++` 의 *저장* 과는 unsequenced (6.5.16p3 + 6.5p2). C++11 도 UB 였지만 **C++17** 부터 "대입의 오른쪽 피연산자가 왼쪽보다 먼저 sequenced" 규칙이 생겨 `i = i++ + 1` 은 i 를 1 증가시키는 정의된 동작이 됐다 (`i = i++` 는 i 불변). 실측: 같은 clang 이 `-std=c++11` 에선 `-Wunsequenced` 경고를 냈고 `-std=c++17` 에선 조용했다. 펌웨어 C 코드에선 여전히 금지.

</details>

<details>
<summary>Q4. 시퀀스 포인트를 모두 말해보세요. `a && b++` 와 `a & b++` 의 차이는?</summary>

C11 부록 C: 함수 호출 직전(지정자·인자 평가 후), `&&` `||` `,` 의 첫 피연산자 뒤, `?:` 의 첫 피연산자 뒤, 완전 선언자 끝, 완전식 끝(식 문장, `if`/`switch`/`while`/`do` 조건, `for` 의 각 식, `return` 식, 초기화식), 라이브러리 함수 반환 직전, printf/scanf 변환 지정자마다, qsort/bsearch 비교 함수 호출 전후. `a && b++` 는 a 가 0 이면 `b++` 가 **실행되지 않는다**(단락 평가). `a & b++` 는 두 쪽 모두 평가하고 순서 없음. MISRA C:2012 Rule 13.5 는 `&&` `||` 오른쪽에 영속적 부작용을 금지한다.

</details>

<details>
<summary>Q5. `x = USART1->DR + USART1->DR;` 는 괜찮나요? (DR 은 volatile, 읽으면 FIFO 에서 빠짐)</summary>

volatile 접근은 그 자체가 부작용이다 (5.1.2.3p2). 두 읽기가 한 식에서 unsequenced 라 어느 쪽이 첫 바이트인지 **최소한 미지정**이고, 6.5p2 를 엄격히 적용하면 UB 로 읽힐 여지도 있다 (해석 논쟁 있음). 어느 쪽이든 결론은 같다: 레지스터 읽기는 한 문장에 하나, 지역 변수에 받아서 쓴다. MISRA C:2012 Rule 13.2 ("허용되는 모든 평가 순서에서 값과 부작용이 같아야 한다") 위반. B6 의 `spi_xfer` 가 정확히 이 패턴이다.

</details>

<details>
<summary>Q6. `sizeof(x++)` 후 x 는? 예외는 없나요?</summary>

x 는 그대로. `sizeof` 피연산자는 평가하지 않고 타입만 본다 (6.5.3.4p2) — 결과는 정수 상수. 예외는 **VLA(가변 길이 배열) 타입**일 때만 피연산자가 평가된다. `sizeof(int[n++])` 같은 경우인데, 크기 식이 결과에 영향이 없으면 평가 여부조차 미지정(6.7.6.2p5)이라 더 헷갈린다. MISRA C:2012 Rule 13.6: `sizeof` 피연산자에 부작용 금지. 매크로 `ARRAY_SIZE(a)` 에 `a[i++]` 를 넣는 실수도 같은 이유로 i 가 안 바뀐다.

</details>

<details>
<summary>Q7. `while (*dst++ = *src++);` 는 UB 아닌가요? `++` 가 두 개인데.</summary>

정의된 동작이다. 수정되는 객체가 `dst`, `src` 두 개로 **서로 다르고** 각각 한 번씩만 수정된다. `*dst` 에 대한 저장도 별개 객체(가리키는 바이트)다. 단, `dst` 와 `src` 가 같은 포인터 변수라면(`*p++ = *p++`) UB. UB 판정 기준은 "`++` 개수"가 아니라 "**같은 스칼라 객체**에 순서 없는 접근이 있는가"다. 단 대입 결과를 조건에 쓰는 스타일은 MISRA 13.4 가 권고로 막는다.

</details>

## 9. 추가 연습

### 연습 1 — 짝수 판별

`bool is_even(uint32_t x)`. 원본: `return x & 1 == 0;`

<details>
<summary>답 보기</summary>

`x & (1 == 0)` = `x & 0` → 항상 false. 고친 것: `return (x & 1u) == 0;` clang: `& has lower precedence than ==` (-Wparentheses).

</details>

### 연습 2 — 범위 검사

`bool in_window(int lo, int x, int hi)`. 원본: `return lo <= x <= hi;`

<details>
<summary>답 보기</summary>

`(lo <= x) <= hi` → 0 또는 1 이 hi 보다 작거나 같은가 — hi ≥ 1 이면 거의 항상 true. 고친 것: `lo <= x && x <= hi`. 이 clang(21)은 아예 **기본 에러**로 막았다: `error: chained comparison 'X <= Y <= Z' does not behave the same as a mathematical expression [-Wparentheses]`.

</details>

### 연습 3 — K&R 의 고전: 대입과 비교

`size_t read_all(stream_t *st, int *out)` 는 END(-1)까지 읽는다. 원본: `while (c = next_char(st) != END) out[n++] = c;`

<details>
<summary>답 보기</summary>

`!=` 가 `=` 보다 위라 `c = (next_char(st) != END)` → c 는 늘 1 (개수는 맞지만 저장된 문자가 전부 1). 고친 것: `while ((c = next_char(st)) != END)`. clang: `using the result of an assignment as a condition without parentheses` (-Wparentheses).

</details>

### 연습 4 — `*p++` 관용구는 안전한가?

`copy_bytes(d, s, n)` 를 `*d++ = *s++` 로, `histogram(hist, p, n)` 을 `hist[*p++ & 3u]++` 로 작성하라. UB 인가?

<details>
<summary>답 보기</summary>

둘 다 정의된 동작. 각 식에서 수정되는 스칼라 객체(`d`, `s`, `*d` / `p`, `hist[k]`)가 모두 다르고 각자 한 번씩만 수정된다. `hist[*p++]++` 의 인덱스는 `p` 의 옛 값으로 계산된 뒤 그 원소가 증가한다.

</details>

### 연습 5 — 인자 평가 순서

`uint16_t two_ids(void)` 는 `next_id()` 를 두 번 불러 첫 ID 를 상위 바이트에 넣는다. 원본: `return pack_ids(next_id(), next_id());`

<details>
<summary>답 보기</summary>

함수 인자 평가 순서는 미지정 (6.5.2.2p10) → 0x0102 일 수도 0x0201 일 수도. `next_id` 가 함수라 UB 는 아니지만(불확정 순서) 결과가 컴파일러 따라 다르다. 고친 것: `uint8_t first = next_id(); uint8_t second = next_id(); return pack_ids(first, second);` — `;` 가 시퀀스 포인트.

</details>

### 연습 6 — `?:` 사슬

`char grade(int s)` 를 `?:` 한 줄로: 90↑ A, 80↑ B, 70↑ C, 나머지 F.

<details>
<summary>답 보기</summary>

`s >= 90 ? 'A' : s >= 80 ? 'B' : s >= 70 ? 'C' : 'F'` — `?:` 는 오른쪽 결합이라 `a ? b : (c ? d : e)` 로 묶여 if-else 사슬과 같다. 첫 피연산자 뒤에 시퀀스 포인트가 있고, 선택되지 않은 쪽은 평가되지 않는다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 15/15 passed
```

## 10. 핵심 정리

- 사다리 암기: 후위 > 단항 > `* / %` > `+ -` > `<< >>` > 비교 > `== !=` > `&` > `^` > `|` > `&&` > `||` > `?:` > `=` > `,`.
- **비트 연산은 비교보다, 시프트는 덧셈보다 늦다** → 레지스터 식은 무조건 괄호: `(reg & M) == V`, `(hi << 8) | lo`, `!(sr & m)`.
- `*p++` = `*(p++)`. 값을 올리려면 `(*p)++`.
- 우선순위 = 묶음, 평가 순서 = 별개. 괄호는 순서를 정하지 않는다.
- **UB** = 같은 객체에 순서 없는 수정+수정/읽기 (6.5p2): `i = i++`, `a[i] = i++`, `f(i++, i++)`, `*p++ << 8 | *p++`.
- **미지정** = 호출/인자 순서 (`f() + g()`, `pack(next(), next())`) — UB 는 아니지만 I/O 순서가 바뀐다.
- 순서를 만드는 것: `&&` `||` `?:` `,` 와 `;`. 부작용 있는 호출은 `&&` 오른쪽이나 같은 식 두 번 금지 (MISRA 13.2/13.5).
- `-Wall` 이 우선순위·unsequenced 대부분을 잡지만, **호출 순서(B6)와 단락 평가 순서(B5)는 못 잡는다**. UBSan 도 unsequenced 는 못 잡는다.

## 참고자료

- C11 표준 초안 N1570 — 6.5p1–3 (평가·시퀀싱), 6.5.2.2p10 (함수 호출), 6.5.3.4 (sizeof), 6.5.13–6.5.17 (`&&` `||` `?:` `=` `,`), 부록 C (시퀀스 포인트): <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- cppreference — C Operator Precedence: <https://en.cppreference.com/w/c/language/operator_precedence>
- cppreference — Order of evaluation (C): <https://en.cppreference.com/w/c/language/eval_order>
- Dennis M. Ritchie, *The Development of the C Language* (`&`/`|` 우선순위의 역사): <https://www.bell-labs.com/usr/dmr/www/chist.html>
- Clang diagnostics reference (`-Wparentheses`, `-Wshift-op-parentheses`, `-Wlogical-not-parentheses`, `-Wunsequenced`): <https://clang.llvm.org/docs/DiagnosticsReference.html>
- GCC Warning Options (`-Wparentheses`, `-Wsequence-point`): <https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html>
- MISRA C:2012 — Rule 12.1, 13.2, 13.3, 13.4, 13.5, 13.6
