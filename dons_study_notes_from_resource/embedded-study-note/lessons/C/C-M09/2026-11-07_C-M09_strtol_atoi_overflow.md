---
id: C-M09
time: 80분
updated: 2026-11-07
---
# C-M09 strtol / atoi 구현: 오버플로와 에러 처리

## 학습 목표

- `strtol` 의 계약(C11 7.22.1.4) — 앞 공백, 부호, base 0/2–36, `0x` 접두사, **end 포인터**, `errno` — 을 표준 문장 그대로 구현할 수 있다.
- **넘치기 전에** 오버플로를 잡는 cutoff/cutlim 기법을 설명하고, `LONG_MIN` 의 비대칭(|LONG_MIN| = LONG_MAX + 1)을 UB 없이 처리한다.
- `"0x"`, `"08"`, `" - 1"`, `"9223372036854775808"` 같은 함정 입력에서 값·end·errno 가 무엇이어야 하는지 바로 말할 수 있다.
- `atoi` 가 왜 위험한지(범위 밖이면 UB, 에러 구분 불가) 설명하고, `errno = 0` → `end == s` → 찌꺼기 → `ERANGE` 순서의 올바른 검사 코드를 쓴다.
- 같은 코드가 `long` 이 64비트(맥 arm64)인 곳과 32비트(Cortex-M, ILP32)인 곳 모두에서 맞도록 `LONG_MAX` 만으로 한계를 표현하고, UART 명령 줄 파서에 적용한다.

## 1. 문제

UART 디버그 콘솔, AT 명령, 설정 파일, 부트로더 인자 — 펌웨어는 결국 어딘가에서 **문자열을 숫자로** 바꾼다. 면접에서 `atoi` 구현은 "쉬운 문제"처럼 나오지만, 꼬리질문은 항상 **"2147483648 을 넣으면요?"**, **"그걸 어떻게 감지하죠?"**, **"`0x` 만 들어오면 end 는 어디를 가리키죠?"** 로 이어진다.

`strtol(s, &end, base)` 는 입력을 세 부분으로 나눈다 (C11 7.22.1.4p2): ① 앞 공백(`isspace`), ② 숫자처럼 생긴 **주제 시퀀스(subject sequence)**, ③ 나머지(인식 못 한 문자들 + NUL). 반환값은 ② 의 값, `*end` 는 ③ 의 시작이다. ② 가 비어 있으면 "변환 없음" — `*end = s` (공백까지 되돌린다).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">strtol("  -0x1Fz", &amp;end, 0) 이 문자열을 나누는 방법</text>
  <g stroke="currentColor">
    <rect x="40" y="40" width="72" height="34" class="f-mut"/>
    <rect x="112" y="40" width="36" height="34" class="f-acc"/>
    <rect x="148" y="40" width="72" height="34" class="f-acc"/>
    <rect x="220" y="40" width="72" height="34" class="f-ok"/>
    <rect x="292" y="40" width="72" height="34" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="58" y="62">' '</text><text x="94" y="62">' '</text><text x="130" y="62">-</text>
    <text x="166" y="62">0</text><text x="202" y="62">x</text><text x="238" y="62">1</text><text x="274" y="62">F</text>
    <text x="310" y="62">z</text><text x="346" y="62">\0</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="76" y="92">① 공백</text><text x="130" y="92">부호</text><text x="184" y="92">접두사</text>
    <text x="256" y="92">숫자</text><text x="328" y="92">③ 나머지</text>
  </g>
  <path d="M112 100 H292" stroke="currentColor"/>
  <text x="140" y="116" fill="currentColor" font-size="12">② 주제 시퀀스 → 값 -31</text>
  <path d="M292 130 V80" stroke="currentColor"/>
  <text x="300" y="134" fill="currentColor">end = s + 7  ('z' 를 가리킴)</text>
  <text x="400" y="62" fill="currentColor">반환 -31, errno 그대로</text>
  <text x="10" y="172" fill="currentColor" font-weight="bold">함정: strtol("0xg", &amp;end, 16)</text>
  <g stroke="currentColor">
    <rect x="40" y="188" width="36" height="34" class="f-ok"/>
    <rect x="76" y="188" width="72" height="34" class="f-warn"/>
    <rect x="148" y="188" width="36" height="34" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="58" y="210">0</text><text x="94" y="210">x</text><text x="130" y="210">g</text><text x="166" y="210">\0</text>
  </g>
  <path d="M76 232 V196" stroke="currentColor"/>
  <text x="84" y="248" fill="currentColor">end = s + 1 — "0x" 뒤에 16진 숫자가 없으면 "0x" 는 접두사가 아니다.</text>
  <text x="84" y="268" fill="currentColor">가장 긴 "올바른 모양" 은 "0" 하나 → 값 0, 변환은 성공 (errno 0).</text>
  <text x="10" y="292" fill="currentColor">반면 "-" 나 "   " 처럼 숫자가 하나도 없으면 end = s (공백·부호까지 되돌림).</text>
</svg>
```

### Part A — 숫자 한 글자와 기본 변환

- `digit_val(c)` — `'0'`–`'9'` → 0–9, `'a'`–`'z'`/`'A'`–`'Z'` → 10–35, 그 밖은 36 ("어떤 base 에서도 숫자 아님").
- `long my_strtol(const char *s, char **end, int base)` 의 뼈대: base 검사(0, 2–36 외에는 `EINVAL`), 앞 공백 건너뛰기, `+`/`-` 부호, 숫자 누적, `*end` 설정 (`end` 가 NULL 이어도 된다).

### Part B — 함정과 오버플로 (면접관이 파고드는 부분)

- `resolve_base(p, &base)` — base 0 이면 `0x`/`0X` → 16, `0` → 8, 그 밖 → 10. `0x` 는 **뒤에 16진 숫자가 있을 때만** 건너뛴다.
- 오버플로: `LONG_MIN`/`LONG_MAX` 로 포화(clamp)하고 `errno = ERANGE`. **signed overflow 를 한 번도 일으키지 않고** 감지해야 한다. 넘친 뒤에도 숫자는 끝까지 소비해서 `end` 가 숫자 뒤를 가리켜야 한다.
- 숫자가 없으면 `0`, `*end = s`, `errno = EINVAL` (macOS libc 와 같게. 아래 풀이에서 glibc 와의 차이 설명). 성공하면 `errno` 를 **건드리지 않는다**.
- `my_atoi` (범위 밖을 포화로 정의한 atoi) 와 `parse_int_strict(s, &out)` — 문자열 전체가 int 하나인지, 숫자 없음/찌꺼기/범위 밖을 구분해 돌려준다.
- 검증: libc `strtol` 과 **값 · end 오프셋 · errno** 3가지를 표 70개 + 고정 시드 생성 문자열 약 6,600개로 대조.

### Part C — UART 명령 줄 파서 `"SET 0x1F -42"`

32개짜리 가짜 레지스터 파일에 대한 콘솔 명령 `SET <reg> <val>`, `GET <reg>`, `RST` 를 파싱한다. `reg` 는 0–31, `val` 은 int32 범위, 숫자는 base 0 (10진/`0x`/8진). 줄 끝 `\r\n` 허용. 결과는 `CLI_OK / EMPTY / UNKNOWN / ARGC / NOTNUM / RANGE` 중 하나 — `"SET 0x 5"`, `"SET 1 08"`, `"SET 1 12abc"`, `"SET 1 99999999999999999999"` 를 각각 올바른 에러로 구분해야 한다. 실패한 `SET` 은 레지스터를 바꾸지 않는다.

## 2. Problem (English)

> **Implement `strtol` from scratch, then use it in a UART command parser.**
>
> ```c
> long my_strtol(const char *s, char **end, int base);
> ```
>
> Follow C11 7.22.1.4: skip leading white space (`isspace`), accept an optional `+`/`-`, then digits in `base` (2..36, letters `a-z`/`A-Z` = 10..35). `base == 0` selects 16 for a `0x`/`0X` prefix, 8 for a leading `0`, else 10; for base 16 the `0x` prefix is optional. Store a pointer to the first unconsumed character in `*end` (if `end != NULL`). If no digits were consumed, return 0 and set `*end = s`.
>
> - On overflow return `LONG_MAX` or `LONG_MIN` and set `errno = ERANGE`, but still consume all digits.
> - Never execute a signed overflow — not even internally. The code must be correct whether `long` is 64-bit (LP64) or 32-bit (ILP32, e.g. Cortex-M).
> - Unsupported base or no conversion: `errno = EINVAL` (matches macOS libc). Never set `errno` to 0.
>
> Examples (`end` shown as offset):
>
> | input | base | value | end | errno |
> |---|---|---|---|---|
> | `"  -0x1Fz"` | 0 | -31 | 7 | 0 |
> | `"0x"` | 16 | 0 | 1 | 0 |
> | `"08"` | 0 | 0 | 1 | 0 |
> | `" - 1"` | 10 | 0 | 0 | EINVAL |
> | `"9223372036854775808"` | 10 | LONG_MAX | 19 | ERANGE |
> | `"-9223372036854775808"` | 10 | LONG_MIN | 20 | 0 |
>
> Then write `cli_parse("SET 0x1F -42", &cmd)` for commands `SET <reg> <val>`, `GET <reg>`, `RST`, rejecting partial numbers (`"12abc"`, `"0x"`), out-of-range values, and wrong argument counts with distinct error codes. Follow-ups: why is `atoi` unsafe? How does your overflow check change if `long` is 32 bits?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 줄과 "mismatch ..." 상세가 나오고 "6/112 passed" — TODO 7개를 채워 112/112 를 만든다
```

불일치가 나면 하네스가 libc 와 내 결과를 나란히 찍는다 (처음 8개까지). 시작 코드의 실제 출력 앞부분:

```text
FAIL starter.c:230  digit_val('0') == 0 && digit_val('9') == 9
  mismatch base=10 "42": libc v=42 end=2 errno=0 | my v=0 end=0 errno=0
  mismatch base=10 "  \t\n\v\f\r 7": libc v=7 end=9 errno=0 | my v=0 end=0 errno=0
...
6/112 passed
```

관찰용 파일 두 개 (하네스와 별개):

```bash
cc -std=c11 -O0 demo_atoi_ub.c -o d0 && ./d0                          # atoi·순진한 구현의 UB 결과
cc -std=c11 -O2 demo_atoi_ub.c -o d2 && ./d2                          # 사후 검사가 사라지는지
cc -std=c11 -O1 -fsanitize=undefined demo_atoi_ub.c -o du && ./du     # UBSan 보고
clang -std=c11 --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_ilp32.c -o -   # long=32비트
clang -std=c11 --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S demo_ilp32.c -o -   # 나눗셈 명령 없음
```

## 4. 힌트

<details>
<summary>힌트 1 — 순서가 곧 설계다</summary>

base 검사 → `isspace((unsigned char)*p)` 로 공백 건너뛰기 → 부호 한 개 → 접두사(`resolve_base`) → 숫자 루프. `isspace` 에 `char` 를 그대로 넘기면 0x80 이상 바이트가 음수가 되어 UB 다 (C11 7.4p1) — 반드시 `unsigned char` 로 캐스트.

</details>

<details>
<summary>힌트 2 — "0x" 는 뒤를 한 글자 더 봐야 한다</summary>

`p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && digit_val(p[2]) < 16` 일 때만 접두사다. `p[1]` 이 `'x'` 라면 NUL 이 아니므로 `p[2]` 를 읽어도 안전하다 (`&&` 의 단락 평가).

</details>

<details>
<summary>힌트 3 — 곱하기 전에 비교하라</summary>

`acc * base + d > LONG_MAX` 는 이미 넘친 뒤라 쓸모가 없다. 대신 미리 `cutoff = limit / base`, `cutlim = limit % base` 를 구해 두고, `acc > cutoff` 이거나 `acc == cutoff && d > cutlim` 이면 "다음 단계에서 넘친다" 고 판정한다. `limit` 은 양수면 `LONG_MAX`, 음수면 `LONG_MAX + 1` — 후자는 `long` 으로 표현이 안 되니 `unsigned long` 으로 계산한다.

</details>

<details>
<summary>힌트 4 — 넘쳐도 멈추지 마라 / 숫자가 없으면 되감아라</summary>

오버플로 플래그를 세운 뒤에도 루프는 숫자를 계속 소비해야 `end` 가 숫자열 끝을 가리킨다. 숫자를 하나도 못 읽었으면 `p` 가 아니라 **원래 `s`** 를 `*end` 에 넣는다.

</details>

<details>
<summary>힌트 5 — Part C: strtol 을 연쇄 호출하지 말고 먼저 토큰으로 자른다</summary>

토큰 `[t, t+n)` 을 base 0 으로 변환한 뒤 `end == t + n` 이면 "토큰 전체가 숫자" 다. `"0x"` 는 end 가 `t+1`, `"08"` 도 `t+1`, `"-"` 는 `t` → 모두 NOTNUM. 변환 전에 `errno = 0`.

</details>

## 5. 풀이

### Part A — 표준이 정한 계약부터

| 단계 | 표준 (C11 7.22.1.4) | 구현 포인트 |
|---|---|---|
| 앞 공백 | `isspace` 가 참인 문자들 (p2) — C 로캘에서 `' ' \t \n \v \f \r` | `isspace((unsigned char)*p)` |
| 부호 | 선택적인 `+` 또는 `-` **하나** (p3) | `"+-1"`, `"--1"` 은 숫자 없음 |
| 접두사 | base 16: `0x`/`0X` 선택. base 0: 정수 상수 문법(6.4.4.1) 그대로 (p3) | `resolve_base` |
| 숫자 | base 미만의 값을 갖는 숫자·글자, 가장 긴 앞부분 (p4) | `digit_val(c) < base` |
| 값 | 부호가 `-` 면 "반환 타입에서" 부정 (p5) | `-(long)acc` / `LONG_MIN` |
| end | 나머지 문자열의 시작 (p5). 변환 없음이면 `nptr` 그대로 (p7) | `*end = (char *)p` 또는 `s` |
| 범위 밖 | `LONG_MIN`/`LONG_MAX` 반환, `errno = ERANGE` (p8) | 포화 + 숫자 계속 소비 |

`digit_val` 은 단순하지만 표준 지식이 하나 숨어 있다. **숫자 `'0'`–`'9'` 는 연속이라고 표준이 보장** 하지만(5.2.1p3), 영문자는 보장하지 않는다 (EBCDIC 에서는 `'i'` 와 `'j'` 사이에 빈칸이 있다). ASCII 인 모든 MCU 에서는 `c - 'a' + 10` 이 맞고, 완전 이식성이 필요하면 `"0123456789abcdefghijklmnopqrstuvwxyz"` 에서 찾는 표 방식을 쓴다. "숫자 아님" 을 36 으로 돌려주면 루프 조건이 `digit_val(c) < base` 하나로 끝난다 — 별도의 `isalnum` 검사가 필요 없다.

`end` 의 타입이 `char **` 인 것도 눈여겨볼 만하다. 입력은 `const char *` 인데 결과 포인터는 `char *` 다 — C 에는 "const 를 그대로 전달하는" 오버로딩이 없어서 표준 라이브러리가 const 를 벗기는 쪽을 택했다 (`strchr` 도 같다). 그래서 구현 안에서 `(char *)s` 캐스트가 필요하고, 그 포인터로 쓰면 안 되는 책임은 호출자에게 있다 (C-B08 참고).

### Part B-1 — 접두사의 함정

| 입력 | base | 결정된 base | 주제 시퀀스 | 값 | end |
|---|---|---|---|---|---|
| `"0x1f"` | 0 | 16 | `0x1f` | 31 | 4 |
| `"010"` | 0 | **8** | `010` | **8** | 3 |
| `"08"` | 0 | 8 | `0` | 0 | **1** |
| `"0x"` | 0 | 8 (접두사 아님) | `0` | 0 | **1** |
| `"0xg"` | 16 | 16 | `0` | 0 | 1 |
| `"0x10"` | 10 | 10 | `0` | 0 | 1 |
| `"0x-1"` | 0 | 8 | `0` | 0 | 1 |
| `"0b101"` | 0 / 2 | 8 / 2 | `0` | 0 | 1 |

마지막 줄: **C23** 은 base 0 과 2 에 `0b`/`0B` 접두사를 추가했지만, 이 맥의 libc 는 C11 동작이다 (실측 `strtol("0b101", &e, 0)` → 0, end 1). 그래서 이번 구현도 `0b` 를 받지 않는다. 펌웨어 콘솔에서 `0b` 를 받고 싶으면 직접 처리하는 게 이식성이 있다.

`resolve_base` 가 "16진 숫자가 뒤따르는가" 를 보는 이유는 p4 의 "**가장 긴** 올바른 모양의 앞부분" 규칙이다. `"0x"` 전체는 base 16 의 올바른 모양이 아니고(숫자가 최소 하나 필요), 그 앞부분 `"0"` 은 올바르다. 그러니 end 는 `'x'` 를 가리켜야 한다. 이걸 놓친 구현 (접두사를 무조건 건너뜀) 은 `"0x"` 에서 숫자 없음으로 판정해 end = 0, errno = EINVAL 을 낸다 — 하네스의 변이 테스트에서 8개 CHECK 가 바로 잡는다 (아래 표).

### Part B-2 — 오버플로를 넘치기 전에 잡는 법

순진한 구현은 `int r; r = r * 10 + d;` 로 누적한다. 결과가 `int` 를 넘는 순간 **signed overflow = UB** 다 (C11 6.5p5). "넘친 뒤에 확인" 하는 코드는 두 가지 이유로 틀렸다 — `demo_atoi_ub.c` 실측 (Apple clang 21, arm64):

```text
-- -O0
2147483647   atoi=2147483647  naive=2147483647  after_the_fact=2147483647  ovf=0  div_check ovf=0
2147483648   atoi=-2147483648 naive=-2147483648 after_the_fact=-2147483648 ovf=1  div_check ovf=0
-2147483648  atoi=-2147483648 naive=-2147483648 after_the_fact=-2147483648 ovf=1  div_check ovf=0
4294967296   atoi=0           naive=0           after_the_fact=0           ovf=1  div_check ovf=1
10000000000  atoi=1410065408  naive=1410065408  after_the_fact=1410065408  ovf=0  div_check ovf=1
99999999999  atoi=1215752191  naive=1215752191  after_the_fact=1215752191  ovf=1  div_check ovf=0
-- -O2
(앞 다섯 열은 -O0 과 동일)                                                         div_check ovf=0 (전부)
```

1. **논리적으로도 틀렸다**: `after_the_fact` 는 "wrap 하면 값이 작아진다(`t < r`)" 고 가정한다. 그런데 `"10000000000"` 에서 `r = 1000000000` → `r * 10` 은 2³² 로 나눈 나머지 1410065408 로 wrap 되는데 이건 r 보다 **크다**. `ovf=0` 인 채로 엉뚱한 값을 돌려준다. (`-2147483648` 은 실제로 표현 가능한 값인데도 ovf=1 — 양수로 누적했기 때문.)
2. **최적화가 검사를 지운다**: `div_check` 의 `if (r * 10 / 10 != r)` 는 `-O0` 에서는 일부 잡지만, `-O2` 에서는 함수 전체가 이렇게 된다 (실제 출력):

```text
_div_check:
	str	wzr, [x1]        ; *ovf = 0   — 검사 끝. 루프도 없다
	ret
```

signed 곱셈은 넘치지 않는다고 가정해도 되므로 `r * 10 / 10` 은 `r` 이고 조건은 항상 거짓이다. (반환값은 호출자가 버리므로 루프도 같이 사라졌다.) 반면 `after_the_fact` 의 `t < r` 은 clang 21 이 이번엔 지우지 않았다 (`cmp w0, w12 ; b.ge`) — **UB 코드가 "우연히 동작" 하는 것과 정확한 것은 다르다**. UBSan 은 둘 다 잡는다:

```text
demo_atoi_ub.c:14:42: runtime error: signed integer overflow: 429496729 * 10 cannot be represented in type 'int'
demo_atoi_ub.c:14:47: runtime error: signed integer overflow: 2147483640 + 8 cannot be represented in type 'int'
demo_atoi_ub.c:15:18: runtime error: negation of -2147483648 cannot be represented in type 'int'; cast to an unsigned type to negate this value to itself
demo_atoi_ub.c:23:19: runtime error: signed integer overflow: 429496729 * 10 cannot be represented in type 'int'
```

libc `atoi` 열도 보자. macOS 의 `atoi("99999999999")` 는 1215752191 (= 99999999999 mod 2³²) — 내부적으로 `(int)strtol(...)` 을 하고 잘린 것이다. 하지만 표준은 **"결과를 표현할 수 없으면 동작은 정의되지 않는다"** (7.22.1p1) 고만 한다. 다른 libc 는 다른 값을 줄 수 있고, 무엇을 주든 에러를 알릴 방법이 없다.

해법은 **곱하기 전에** 판단하는 것이다:

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">base 10, 양수 (64비트): limit = LONG_MAX = 9223372036854775807</text>
  <text x="10" y="44" fill="currentColor">cutoff = limit / 10 = 922337203685477580,  cutlim = limit % 10 = 7</text>
  <path d="M40 110 H700" stroke="currentColor"/>
  <rect x="40" y="90" width="380" height="20" class="f-ok"/>
  <rect x="420" y="90" width="6" height="20" class="f-acc"/>
  <rect x="426" y="90" width="274" height="20" class="f-warn"/>
  <g fill="currentColor" font-size="12">
    <text x="44" y="84">acc &lt; cutoff</text>
    <text x="380" y="84">acc == cutoff</text>
    <text x="560" y="84">acc &gt; cutoff</text>
    <text x="44" y="132">acc*10 + d ≤ (cutoff-1)*10 + 9</text>
    <text x="44" y="148">&lt; cutoff*10 ≤ limit → 안전</text>
    <text x="330" y="132">d ≤ 7 → 안전, 정확히 limit 까지</text>
    <text x="330" y="148">d ≥ 8 → 넘침</text>
    <text x="560" y="132">acc*10 ≥ (cutoff+1)*10</text>
    <text x="560" y="148">&gt; limit → 무조건 넘침</text>
  </g>
  <text x="10" y="186" fill="currentColor" font-weight="bold">음수: limit = LONG_MAX + 1 = 2⁶³ (unsigned long 으로만 표현 가능)</text>
  <text x="10" y="206" fill="currentColor">cutoff 는 같은 922337203685477580, cutlim = 8 → "-9223372036854775808" 은 넘침이 아니다</text>
  <text x="10" y="234" fill="currentColor">핵심: 판정에 쓰는 값(cutoff, cutlim, acc)이 모두 limit 이하 → 계산 중 어떤 overflow 도 없다.</text>
  <text x="10" y="254" fill="currentColor">acc 는 unsigned long 이라 혹시 넘쳐도 wrap(정의됨)이지만, 이 조건 덕분에 wrap 도 일어나지 않는다.</text>
  <text x="10" y="276" fill="currentColor">32비트 long 이면 limit 만 바뀐다: cutoff 214748364, cutlim 7 / 8 — 코드는 그대로.</text>
</svg>
```

| long 폭 | 부호 | limit | cutoff (base 10) | cutlim | cutoff (base 16) | cutlim |
|---|---|---|---|---|---|---|
| 64 (LP64, 맥) | + | 2⁶³−1 | 922337203685477580 | 7 | 0x07FF…FF | 15 |
| 64 | − | 2⁶³ | 922337203685477580 | 8 | 0x0800…00 | 0 |
| 32 (ILP32, Cortex-M) | + | 2³¹−1 | 214748364 | 7 | 0x07FFFFFF | 15 |
| 32 | − | 2³¹ | 214748364 | 8 | 0x08000000 | 0 |

음수 base 16 의 cutlim 0 이 보여 주듯, `"-0x8000000000000000"` 은 마지막 자리 0 만 허용되어 정확히 `LONG_MIN` 이 된다 (표 케이스로 검증).

마지막 부호 처리도 UB 지뢰다:

```c
if (!neg) return (long)acc;                    /* acc ≤ LONG_MAX → 값 보존 변환 */
return acc == limit ? LONG_MIN : -(long)acc;   /* acc ≤ LONG_MAX 일 때만 (long) 후 부정 */
```

`-(long)acc` 를 `acc == 2⁶³` 에 적용하면 `(long)acc` 가 범위 밖 변환(구현 정의, 6.3.1.3p3)이고 그 부정은 overflow 다. `(long)(0ul - acc)` 같은 트릭도 결국 범위 밖 unsigned→signed 변환이라 구현 정의에 기댄다. `LONG_MIN` 상수를 직접 돌려주는 게 깔끔하다.

또 하나의 방법은 **음수 쪽으로 누적** 하는 것이다 (옛 BSD 구현, 추가 연습 2). 2의 보수에서 음수 범위가 하나 더 넓으므로 `acc` 를 0 이하로 유지하며 `acc = acc * base - d` 로 쌓고, `cutoff = LONG_MIN / base` (C99 부터 `/` 는 0 쪽 버림 — 6.5.5p6) 와 비교한다. unsigned 가 필요 없다는 장점, 양수 결과에서 `acc == LONG_MIN` 을 따로 걸러야 한다는 단점이 있다.

### Part B-3 — errno 규칙과 "변환 없음"

| 상황 | 반환 | `*end` | errno (macOS) | errno (glibc) |
|---|---|---|---|---|
| 성공 | 값 | 숫자 뒤 | **안 건드림** | 안 건드림 |
| 범위 밖 | `LONG_MIN`/`LONG_MAX` | 숫자열 끝 | `ERANGE` | `ERANGE` |
| 숫자 없음 (`""`, `"-"`, `" - 1"`) | 0 | `s` | `EINVAL` | 안 건드림 |
| base 가 1, 37, -1 … | 0 | `s` (실측) | `EINVAL` | `EINVAL` |

- C11 7.5p3: 어떤 라이브러리 함수도 errno 를 **0 으로 만들지 않는다**. 그래서 호출자가 직전에 `errno = 0` 을 해야 `ERANGE` 를 믿을 수 있다. 하네스는 `errno = EDOM` 을 넣고 성공 호출 뒤에도 `EDOM` 인지 확인한다.
- "숫자 없음 → EINVAL" 은 C 표준에는 없고 POSIX 가 **허용(may)** 하는 동작이다. macOS(FreeBSD 계열 libc)는 설정하고 glibc 는 설정하지 않는다 (glibc 는 지원 안 하는 base 에만 EINVAL). 그래서 이식성 있는 코드는 **"숫자 없음" 을 errno 가 아니라 `end == s` 로 판정** 한다. 이번 구현은 대조 기준이 macOS libc 라서 EINVAL 을 맞췄다.
- 0, 2–36 이외의 base 는 C 표준이 동작을 정의하지 않는다. 실측 macOS 는 0 / end = s / EINVAL 이고 구현도 그렇게 했다.

`parse_int_strict` 는 이 규칙을 전부 엮은 "atoi 의 올바른 대체" 다:

```c
errno = 0;
long v = my_strtol(s, &end, 10);
if (end == s)                 → PI_NODIGITS     /* "" "  -" "abc" — atoi 는 0 */
skip trailing spaces; if (*end) → PI_JUNK       /* "12abc"        — atoi 는 12 */
if (errno == ERANGE || v < INT_MIN || v > INT_MAX) → PI_RANGE   /* atoi 는 UB */
*out = (int)v;                                   /* 실패면 *out 그대로 */
```

`ERANGE` 검사와 `int` 범위 검사가 둘 다 필요한 이유: 64비트 `long` 에서 `"2147483648"` 은 strtol 입장에선 정상(ERANGE 아님)이지만 `int` 로는 넘친다. 32비트 `long` 이면 반대로 ERANGE 만으로 잡힌다. 두 검사를 다 쓰면 어느 쪽에서도 맞다.

### Part B-4 — 정말 맞는지: libc 대조와 변이 테스트

하네스는 `same(s, base)` 로 libc `strtol` 과 내 함수를 같은 입력에 돌려 **값, `end - s`, errno** 를 비교한다. 생성기는 xorshift32 고정 시드라 매 실행 같은 문자열이 나온다.

| 묶음 | 개수 | 무엇을 겨냥하나 |
|---|---|---|
| 표 `T[]` | 70 | 위 함정 전부, 64비트 경계 ±1 (10진·16진·8진·36진) |
| `fuzz_alphabet` | 3000 | `" \t\n+-0xXbB1789aAfFgGzZ_#."` 무작위 조합, base 0/2/8/10/16/36/무작위 + 잘못된 base |
| `fuzz_boundary` | 112 | LONG_MAX·LONG_MIN ±3 을 base 2,3,7,8,10,16,36,0 으로, 앞자리 0·접두사·꼬리 찌꺼기 |
| `fuzz_values` | 3000 | 무작위 64비트 값 + 공백·부호·접두사·찌꺼기 |
| `fuzz_long` | 500 | 20–60자리 숫자열 (넘친 뒤 end 위치) |

결과 분포(계측해 본 값): 약 4,190 정상 변환, 490 ERANGE, 2,000 EINVAL. 그리고 테스트가 버그를 **정말 잡는지** 보려고 정답 코드를 일부러 한 줄씩 망가뜨려 돌려 봤다 (mutation test, 실측):

| 심은 버그 | 결과 |
|---|---|
| `d > cutlim` → `d >= cutlim` (경계 1 차이) | 104/112 — 8 FAIL |
| 넘치면 `continue` 대신 `break` (end 가 중간에 멈춤) | 106/112 |
| `0x` 뒤 16진 숫자 확인 생략 | 104/112 |
| 숫자 없을 때 `*end = p` (공백·부호 뒤를 가리킴) | 103/112 |
| 음수 쪽 limit 도 `LONG_MAX` (비대칭 무시) | 108/112 |

### ILP32 — Cortex-M 에서는 무엇이 달라지나

AAPCS(Arm 32비트 ABI)에서 `long` 은 32비트다 (ILP32). 이번 코드는 `LONG_MAX` 와 `unsigned long` 만 쓰므로 소스 수정 없이 맞다. `demo_ilp32.c` 는 `_Static_assert(sizeof(long) == 4 && LONG_MAX == 2147483647L, ...)` 를 걸고 숫자 루프를 크로스 컴파일한다. 실제 출력 (Cortex-M4, `-O2`, 일부):

```text
digits_any:
	mvn	r0, #-2147483648          ; limit = 0x7FFFFFFF (LONG_MAX, 32비트)
	it	ne
	movne.w	r0, #-2147483648      ; neg 면 limit = 0x80000000
	...
	udiv	r11, r0, r2               ; cutoff = limit / base   (M4 하드웨어 나눗셈)
	mls	r9, r11, r2, r0           ; cutlim = limit - cutoff*base
	...
	mlaeq	r7, r8, r2, r5            ; acc = acc*base + d (조건부)
digits_dec:                           ; base 10 고정
	movw	r4, #52429 / movt r4, #52428 ; 0xCCCCCCCD — /10 을 곱셈으로 (umull)
	movw	r5, #52428 / movt r5, #3276  ; cutoff = 0x0CCCCCCC = 214748364 상수
	add.w	r4, lr, lr, lsl #2        ; acc*5
	addeq.w	lr, r7, r4, lsl #1        ; acc*10 + d — mul 명령도 없다
```

Cortex-M0 (`thumbv6m`, ARMv6-M 은 나눗셈 명령이 없다) 에서는 `digits_any` 가 `bl __aeabi_uidiv` 를, `digits_dec` 마저 `bl __aeabi_uidivmod` 를 부른다 (M0 에는 32×32→64 곱셈 `umull` 도 없어 상수 나눗셈을 곱셈으로 못 바꾼다). 변환 1번에 한 번이라 대개 문제 없지만, 핫 루프라면 base 별 cutoff 를 상수로 미리 계산하거나 표로 두는 방법이 있다 (glibc 의 `strtol_l.c` 가 base 별 표를 쓰는 것으로 알려져 있다 — 여기선 소스를 직접 확인하지 않았다). 오브젝트 크기는 `xcrun llvm-size` 로 M4 672 B, M0 692 B (두 함수 합).

호스트 arm64 는 같은 코드가 `mov x9, #0x8000000000000000` 과 `udiv x14, x0, x12` 로 나온다 — **소스 한 줄 안 바꾸고 64/32비트가 모두 맞는 것** 이 `LONG_MAX` 로만 한계를 쓰는 이유다. `9223372036854775807` 같은 숫자를 소스에 박아 두면 Cortex-M 에서 바로 깨진다.

### Part C — UART 명령 줄

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">cli_parse(" SET\t3  -0x10\r\n") — 먼저 자르고, 토큰마다 "전체가 숫자인가" 검사</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="70" height="30" class="f-acc"/>
    <rect x="130" y="40" width="40" height="30" class="f-acc"/>
    <rect x="210" y="40" width="90" height="30" class="f-acc"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="35" y="60">SET</text><text x="143" y="60">3</text><text x="222" y="60">-0x10</text>
    <text x="320" y="60">\r\n → 구분자</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="88">tok[0] len 3</text><text x="125" y="88">tok[1]</text><text x="210" y="88">tok[2] len 5</text>
  </g>
  <g stroke="currentColor">
    <rect x="20" y="104" width="160" height="40" rx="6" class="f-mut"/>
    <rect x="200" y="104" width="160" height="40" rx="6" class="f-mut"/>
    <rect x="380" y="104" width="170" height="40" rx="6" class="f-mut"/>
    <rect x="570" y="104" width="160" height="40" rx="6" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="30" y="122">① 명령어 비교</text><text x="30" y="138">아니면 UNKNOWN</text>
    <text x="210" y="122">② 토큰 수 == 필요 수?</text><text x="210" y="138">아니면 ARGC</text>
    <text x="390" y="122">③ strtol(t, &amp;end, 0)</text><text x="390" y="138">end != t+n → NOTNUM</text>
    <text x="580" y="122">④ ERANGE 또는</text><text x="580" y="138">범위 밖 → RANGE</text>
  </g>
  <g stroke="currentColor" fill="none"><path d="M180 124 H200"/><path d="M360 124 H380"/><path d="M550 124 H570"/></g>
  <text x="20" y="176" fill="currentColor">"0x"  → end = t+1 (≠ t+2) → NOTNUM      "08" → base 8 로 "0" 만 → NOTNUM</text>
  <text x="20" y="196" fill="currentColor">"-"   → end = t   → NOTNUM               "12abc" → end = t+2 → NOTNUM</text>
  <text x="20" y="216" fill="currentColor">"99999999999999999999" → ERANGE → RANGE   "2147483648" → int32 밖 → RANGE</text>
  <text x="20" y="244" fill="currentColor">토큰은 공백으로 시작하지 않으므로 strtol 의 공백 건너뛰기('\n' 포함)가 다음 줄을 먹을 일이 없다.</text>
</svg>
```

설계 포인트:

- **토큰화 먼저**: `strtol(p, &p, 0)` 를 연쇄 호출하는 방식은 strtol 이 앞 공백(`\n` 포함)을 스스로 건너뛰기 때문에 "인자가 빠진 줄" 에서 경계가 흐려지고, `"12abc"` 다음 토큰이 어디서 시작하는지 애매해진다. `next_tok` 이 `[t, t+n)` 을 주면 판정은 `end == t + n` 한 줄이다. 입력을 수정하지 않으므로(`const char *`) UART RX 버퍼를 그대로 넘길 수 있고 `strtok` 의 숨은 정적 상태(재진입 불가) 문제도 없다.
- **최대 4개만 센다**: 필요한 토큰은 최대 3개. 4번째가 존재하면 argc = 4 가 되어 `SET 1 2 3` 이 ARGC 로 걸린다. 고정 크기 배열이라 힙이 필요 없다.
- **에러 순서**: 명령어 → 인자 수 → 숫자 모양 → 범위. 사용자 입장에서 가장 근본적인 에러를 먼저 보고한다.
- **base 0 의 8진수 함정**: `"SET 1 010"` 은 10 이 아니라 **8** 이다. 콘솔에서 사람이 `010` 을 치는 일은 흔하다. 의도가 10진이면 base 10 을 쓰고 `0x` 만 따로 처리하는 것도 방법이다 (테스트는 현재 동작을 문서화하는 의미로 8 을 기대).
- **ILP32 주의**: 이 맥에선 `long` 이 64비트라 `"SET 1 0xFFFFFFFF"` 를 strtol 이 정상 변환한 뒤 int32 범위 검사에서 RANGE 가 된다. Cortex-M 에서는 strtol 자체가 ERANGE 를 낸다 — 결과는 같지만 경로가 다르다. 레지스터 값처럼 **비트 패턴** 을 받고 싶으면 `strtoul` + `uint32_t` 범위 검사가 맞다 (추가 연습 1·3).
- **실패한 SET 은 쓰지 않는다**: `cli_exec` 는 파싱이 완전히 성공한 뒤에만 `g_regs` 를 쓴다. 테스트 `"SET 0x1F 0x"` → `ERR NOTNUM` 다음 `GET 0x1f` 가 여전히 `R31=-42` 인지 확인한다. 반쯤 파싱된 명령을 실행하는 콘솔은 현장에서 장비를 망가뜨린다.
- **ISR 에서 파싱하지 않는다**: UART RX ISR 은 바이트를 링 버퍼에 넣고 `\n` 에서 플래그만 세운다. 파싱은 메인 루프(또는 태스크)에서. `errno` 는 newlib 에서 reent 구조체(스레드/태스크별)에 있으므로 RTOS 에서 태스크 간 섞이지 않게 설정돼 있는지도 확인할 것.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 112/112 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san
# 112/112 passed, 보고 없음 (생성 문자열 6,600여 개 포함)
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `r = r * 10 + d;` 후 `if (r < 0)` 로 넘침 검사 | 넘침 자체가 UB. 게다가 `"10000000000"` 처럼 wrap 후 양수가 되는 경우도 있다 | 곱하기 전에 `acc > cutoff \|\| (acc == cutoff && d > cutlim)` |
| 양수로 누적한 뒤 `-r` | `-2147483648` 은 누적 중 `2147483648` 에서 넘친다 (UBSan: negation of -2147483648) | unsigned 누적 + `acc == limit ? LONG_MIN : -(long)acc`, 또는 음수 누적 |
| `isspace(*p)` | `char` 가 signed 인 플랫폼에서 0x80 이상 바이트는 음수 → UB (7.4p1) | `isspace((unsigned char)*p)` |
| `0x` 를 무조건 건너뜀 | `"0x"` → end 0, EINVAL. 정답은 end 1, 값 0, 성공 | 뒤에 16진 숫자가 있을 때만 접두사 |
| 넘치면 `break` | end 가 숫자열 중간을 가리킴 → 호출자가 "찌꺼기" 로 오판 | 플래그만 세우고 계속 소비 |
| 숫자가 없을 때 `*end = p` | `"  -"` 에서 end 가 3 → 호출자의 `end == s` 검사가 실패 | `*end = (char *)s` |
| strtol 호출 전 `errno = 0` 생략 | 이전 호출의 ERANGE 가 남아 있으면 정상 값도 에러로 보인다 (7.5p3) | `errno = 0; v = strtol(...); if (errno == ERANGE) ...` |
| `if (v == LONG_MAX) error;` | 입력이 정말 `LONG_MAX` 일 수 있다 | `errno == ERANGE` 로 판정 |
| `end == s` 대신 `v == 0` 으로 "숫자 없음" 판정 | `"0"` 과 `"abc"` 를 구분 못 함 | `end == s` |
| 9223372036854775807 같은 리터럴로 한계 하드코딩 | Cortex-M(32비트 long)에서 틀림 | `LONG_MAX`, `INT32_MAX` 등 `<limits.h>`/`<stdint.h>` 매크로 |
| 콘솔 숫자를 base 0 으로 받으면서 앞 0 을 잊음 | `"010"` = 8, `"08"` = 파싱 실패 | 사용자 문서화, 또는 base 10 + `0x` 별도 처리 |

## 8. 면접 Q&A

<details>
<summary>Q1. atoi 대신 strtol 을 쓰라고들 하는데, atoi 의 문제가 정확히 뭔가요?</summary>

두 가지다. ① **에러를 알릴 방법이 없다**: `"0"`, `"abc"`, `""` 모두 0. `"12abc"` 는 12 — 찌꺼기를 모른다. ② **범위 밖이면 UB** (C11 7.22.1p1). macOS 는 `(int)strtol` 로 잘라 `atoi("99999999999") == 1215752191` 을 주지만 표준이 약속한 값이 아니다. `sscanf("%d")` 도 같은 문제 — 변환 결과를 표현 못 하면 UB (7.21.6.2p10). 꼬리질문 "그럼 MISRA 는?" → MISRA C:2012 Rule 21.7 이 `atoi/atol/atoll/atof` 사용을 금지한다.

</details>

<details>
<summary>Q2. strtol 의 에러 검사를 빠짐없이 쓰면 어떻게 되나요?</summary>

```c
char *end; errno = 0;
long v = strtol(s, &end, 10);
if (end == s)            /* 숫자 없음 */
if (*end != '\0')        /* 찌꺼기 (공백 허용 여부는 정책) */
if (errno == ERANGE)     /* long 범위 밖 — v 는 LONG_MIN/MAX */
if (v < lo || v > hi)    /* 내 타입·도메인 범위 */
```

꼬리질문: "errno 를 왜 먼저 0 으로?" → 라이브러리는 errno 를 0 으로 만들지 않으니(7.5p3) 이전 값이 남는다. "`v == LONG_MAX` 로 판정하면?" → 입력이 실제로 `LONG_MAX` 일 수 있다. "EINVAL 로 숫자 없음을 판정하면?" → glibc 는 안 세운다 (macOS 는 세움). `end == s` 가 이식성 있다.

</details>

<details>
<summary>Q3. 정수 누적에서 오버플로를 어떻게 감지하나요? 세 가지 방법을 비교해 보세요.</summary>

① **cutoff/cutlim** (이번 구현): `limit / base`, `limit % base` 를 미리 구하고 곱하기 전에 비교. 어떤 타입에도 이식성 있고 UB 없음. ② **음수 누적**: signed 만으로 `LONG_MIN` 까지 표현 (추가 연습 2). ③ **컴파일러 내장**: `__builtin_mul_overflow(acc, base, &acc) || __builtin_add_overflow(acc, d, &acc)` — GCC/Clang 전용, C23 에서는 `<stdckdint.h>` 의 `ckd_mul`/`ckd_add` 로 표준화. 틀린 방법: 넘친 뒤 `r < 0` / `t < r` / `r * 10 / 10 != r` — UB 이고, 마지막은 clang `-O2` 가 실제로 지운다 (demo 실측: `str wzr,[x1]; ret`).

</details>

<details>
<summary>Q4. "-9223372036854775808" 을 처리할 때 무엇을 조심해야 하나요?</summary>

2의 보수에서 |LONG_MIN| = LONG_MAX + 1 이라 **양수 long 으로 표현할 수 없다**. 양수로 누적하고 마지막에 부정하는 코드는 누적 단계에서 넘친다. 음수 쪽 limit 을 `(unsigned long)LONG_MAX + 1u` 로 잡고, 결과가 그 값과 같으면 `LONG_MIN` 상수를 돌려준다. `-(long)acc` 는 `acc ≤ LONG_MAX` 일 때만 쓴다. 꼬리질문 "C23 에서 바뀐 점?" → C23 은 signed 정수를 2의 보수로 **강제** 하지만 overflow 는 여전히 UB 다.

</details>

<details>
<summary>Q5. strtol("0x", &end, 16) 의 반환값과 end 는?</summary>

0 과 `s + 1`, errno 는 그대로(성공). 주제 시퀀스는 "기대한 모양의 가장 긴 앞부분" (7.22.1.4p4) 인데, `"0x"` 는 16진 숫자가 하나도 없어 기대한 모양이 아니고 `"0"` 은 맞다. 그래서 `'x'` 가 나머지 문자열의 시작이다. 같은 이유로 `strtol("08", &end, 0)` 은 base 8 로 결정돼 `"0"` 만 읽고 end = 1. 콘솔에서 `0x` 를 치다 만 입력을 "숫자 0 + 찌꺼기 x" 로 보고 거부하려면 `*end != '\0'` 검사가 필요하다.

</details>

<details>
<summary>Q6. strtoul("-1", &end, 10) 은 에러인가요?</summary>

에러가 아니다. 결과는 `ULONG_MAX`, errno 0. 표준은 "부호가 `-` 면 변환된 값을 **반환 타입에서** 부정" (7.22.1.4p5) 하라고 하므로 `0ul - 1` = `ULONG_MAX` (unsigned 산술은 wrap, 6.2.5p9). 반면 `"-18446744073709551616"` 은 크기 자체가 넘쳐 `ULONG_MAX` + ERANGE. 음수를 거부하려면 공백을 건너뛴 첫 글자가 `'-'` 인지 직접 확인해야 한다 (추가 연습 1).

</details>

<details>
<summary>Q7. 이 코드를 Cortex-M 펌웨어에 넣을 때 바뀌는 게 있나요?</summary>

소스는 그대로다 — 한계를 `LONG_MAX` 로만 썼기 때문. 바뀌는 것: ① `long` 이 32비트라 `strtol("0xFFFFFFFF")` 가 ERANGE (64비트 맥에선 정상) → 32비트 레지스터 값은 `strtoul` 로 받는다. ② cutoff 계산 나눗셈: M3/M4 는 `udiv` 1명령, M0/M0+ 는 `__aeabi_uidiv` 라이브러리 호출 (demo 실측). ③ newlib/newlib-nano 의 strtol 은 errno 가 reent 구조체에 있고, 로캘 지원이 크기를 키울 수 있어 작은 부트로더에선 직접 구현한 작은 파서를 쓰기도 한다. ④ `int` 와 `long` 이 같은 폭이라 `parse_int_strict` 의 `v > INT_MAX` 검사는 항상 거짓이 되고 ERANGE 검사가 그 역할을 맡는다 — 둘 다 써 두면 양쪽에서 맞다.

</details>

<details>
<summary>Q8. UART 콘솔 파서를 설계할 때 신경 쓰는 점은?</summary>

① ISR 은 바이트를 링 버퍼에 넣기만, 파싱은 메인/태스크에서 ② 한 줄 길이 제한과 넘침 시 줄 전체 폐기 (C-B07 의 줄 버퍼) ③ `strtok` 대신 재진입 가능한 토큰화, 입력 불변 ④ 토큰 전체가 숫자인지 `end == t + n` ⑤ 범위 검사는 strtol 의 ERANGE + 도메인 범위 둘 다 ⑥ 파싱이 완전히 성공한 뒤에만 실행 (원자적 명령) ⑦ 에러 코드를 구분해서 응답 (`ERR NOTNUM` vs `ERR RANGE`) — 현장 디버깅 시간이 줄어든다. 꼬리질문 "명령이 많아지면?" → 이름→핸들러 디스패치 테이블 (C-M06 참고).

</details>

## 9. 추가 연습

### 연습 1 — my_strtoul 과 "-1"

`unsigned long my_strtoul(const char *s, char **end, int base)` 를 같은 뼈대로 구현하고 libc `strtoul` 과 2,000개 생성 문자열로 대조하라. `"-1"`, `"18446744073709551616"`, `"-18446744073709551616"` 의 결과와 errno 는?

<details>
<summary>답 보기</summary>

limit 은 부호와 관계없이 `ULONG_MAX`, cutoff/cutlim 은 그 몫과 나머지. 숫자 누적이 끝나면 `neg ? 0ul - acc : acc` — unsigned 부정은 정의된 동작. 결과: `"-1"` → `ULONG_MAX`, errno 0 (에러 아님!). 크기가 넘치면 부호와 관계없이 `ULONG_MAX` + ERANGE. 실측 libc 와 2,000/2,000 일치.

</details>

### 연습 2 — unsigned 없이: 음수 쪽으로 누적

`strtol_negacc` 를 `long` 만으로 구현하라 (unsigned 금지). cutoff 와 cutlim 은 어떻게 구하나? 양수 결과에서 따로 처리해야 하는 값은?

<details>
<summary>답 보기</summary>

`cutoff = LONG_MIN / base` (음수, 0 쪽 버림), `cutlim = -(LONG_MIN % base)` (나머지는 (−base, 0] 범위). `acc < cutoff || (acc == cutoff && d > cutlim)` 이면 넘침, 아니면 `acc = acc * base - d` — 이 시점엔 결과가 `LONG_MIN` 이상임이 보장된다. 음수면 `acc` 그대로, 양수면 `acc == LONG_MIN` (입력 `"9223372036854775808"`) 이 넘침이므로 `acc < -LONG_MAX` 를 검사한 뒤 `-acc`. libc 와 3,000개 일치.

</details>

### 연습 3 — 한계를 매개변수로: ILP32 흉내

`DEFINE_STRTO(NAME, T, TMIN, TMAX)` 매크로로 `strto_i32`, `strto_i16` 을 만들어라. 기대값은 "libc strtol 결과를 해당 폭으로 포화 + 넘치면 ERANGE". `strto_i32("0xFFFFFFFF", NULL, 0)` 는?

<details>
<summary>답 보기</summary>

누적 타입을 `uintmax_t` 로 하고 `limit = neg ? (uintmax_t)TMAX + 1 : (uintmax_t)TMAX` — 이것이 "`LONG_MAX` 로만 한계를 표현" 하는 원리를 일반화한 것이다. `strto_i32("0xFFFFFFFF")` 는 `INT32_MAX` + ERANGE — Cortex-M 의 `strtol` 이 내는 결과와 같다. 레지스터 값(비트 패턴)이면 unsigned 변환을 써야 하는 이유. 3,000개 생성 문자열로 대조.

</details>

### 연습 4 — 쉼표 목록 파서

`parse_list("0x10, 7,-3", out, cap, &err_pos)` — 성공하면 항목 수, 실패하면 -1 과 문제 위치. `"1,,2"`, `"1, 2,"`, `"5 6"`, `"0x,1"`, `"1,9999…"`, cap 초과를 각각 어디서 거부하나?

<details>
<summary>답 보기</summary>

각 항목: 공백 건너뛰기 → `errno = 0; strtol(p, &e, 0)` → `e == p` / ERANGE / cap 초과면 그 항목 위치 → 뒤 공백 → `'\0'` 면 끝, `','` 면 다음, 그 밖이면 그 문자 위치. 위치: `"1,,2"` → 2, `"1, 2,"` → 5 (쉼표 뒤 항목 없음), `"5 6"` → 2 (쉼표 없이 다음 숫자), `"0x,1"` → 1 (`'x'`), `"1,9999…"` → 2, `"1,2,3,4,5"` (cap 4) → 8. 빈 문자열은 0개.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 22/22 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o pr_san && ./pr_san   # 22/22, 보고 없음
```

## 10. 핵심 정리

- strtol = **공백 → 부호 하나 → 접두사 → 가장 긴 숫자열**. end 는 나머지의 시작, 숫자가 없으면 `end = s` (공백·부호까지 되돌림).
- `"0x"` 는 16진 숫자가 뒤따를 때만 접두사 → `"0x"`, `"0xg"`, `"08"`(base 0) 모두 값 0, end 1, 성공.
- 오버플로는 **곱하기 전에**: `acc > cutoff || (acc == cutoff && d > cutlim)`, `cutoff = limit / base`, limit 은 `LONG_MAX` 또는 `LONG_MAX + 1`(unsigned). 넘쳐도 숫자는 끝까지 소비.
- `LONG_MIN` 은 양수로 못 만든다 → unsigned 누적 후 `acc == limit ? LONG_MIN : -(long)acc`, 또는 음수 누적.
- 범위 밖이면 `LONG_MIN/MAX` + `ERANGE`. 라이브러리는 errno 를 0 으로 안 만든다 → 호출 전 `errno = 0`. "숫자 없음" 은 errno 말고 `end == s` 로 (glibc 는 EINVAL 안 세움).
- `atoi` 는 에러 구분 불가 + 범위 밖 UB → `errno=0 → end==s → *end → ERANGE → 도메인 범위` 순서의 strict 파서로 대체.
- 한계는 `LONG_MAX` 로만 → 64비트 맥과 32비트 Cortex-M 에서 같은 소스가 맞다. M0 는 cutoff 나눗셈이 `__aeabi_uidiv` 호출.
- 콘솔 파서: 토큰화 먼저, `end == t + n`, ERANGE + 도메인 범위, 완전히 성공한 뒤에만 실행. base 0 의 `010` = 8 을 잊지 말 것.

## 참고자료

- C11 (N1570) 7.22.1.4 strtol (p2 분해, p3 base 0/16 접두사, p4 가장 긴 앞부분, p5 반환 타입에서 부정, p7 변환 없음, p8 ERANGE), 7.22.1p1 · 7.22.1.2 atoi, 7.5p3 errno, 7.4p1 ctype 인자, 5.2.1p3 숫자 연속성, 6.5p5 signed overflow, 6.3.1.3 정수 변환, 6.5.5p6 나눗셈 버림, 7.21.6.2p10 scanf — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- [POSIX strtol — ERRORS (ERANGE, EINVAL)](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strtol.html)
- [FreeBSD libc `strtol.c` — macOS libc 와 같은 cutoff/cutlim 구현](https://github.com/freebsd/freebsd-src/blob/main/lib/libc/stdlib/strtol.c)
- [newlib `strtol.c` — 임베디드 툴체인(arm-none-eabi)의 구현](https://sourceware.org/git/?p=newlib-cygwin.git;a=blob;f=newlib/libc/stdlib/strtol.c)
- CERT C INT32-C (signed overflow 방지), ERR34-C ("Detect errors when converting a string to a number" — atoi 금지) — <https://wiki.sei.cmu.edu/confluence/display/c>
- MISRA C:2012 Rule 21.7 (`atof/atoi/atol/atoll` 사용 금지)
- C23 `<stdckdint.h>` (`ckd_add`, `ckd_mul`) 와 strtol 의 `0b` 접두사 — N3096 (C23 최종 초안) `<stdlib.h>` 수치 변환 절
- 관련 레슨: C-B02 (정수 승격), C-B07 (문자열 함수·UART 줄 버퍼), C-B08 (const 와 `char **`), C-M06 (명령 디스패치 테이블), C-M10 (반대 방향: itoa·mini printf), C-M12 (UB 카탈로그)
