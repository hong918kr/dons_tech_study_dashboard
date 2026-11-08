---
id: C-M10
time: 80분
updated: 2026-11-07
---
# C-M10 itoa와 미니 printf: UART 로깅용

## 학습 목표

- `INT_MIN` 을 포함한 모든 `int` 를 base 2..36 문자열로 바꾸는 `itoa` 를 **UB 없이** 구현하고, 왜 `-v` / `abs(v)` 가 틀렸는지 UBSan 출력으로 보여 줄 수 있다.
- `va_list` 로 `%d %u %x %X %c %s %%` + 플래그 `-` `0` + 폭을 지원하는 `mini_snprintf` 를 만들고, **snprintf 의 잘림(truncation) 규칙** — size>0 이면 항상 NUL, 반환값은 "썼을" 길이, size==0 이면 아무것도 안 씀 — 을 정확히 지킨다.
- libc `vsnprintf` 와 **표 기반 + 결정적 퍼즈(xorshift32)** 로 반환값과 버퍼 전체 바이트를 대조하는 테스트를 짠다 (`va_copy` 가 왜 필요한지 포함).
- `log_printf → log_vprintf(va_list) → 스택 줄 버퍼 → 임계 구역 → TX 링 버퍼` 구조의 UART 로거를 만들고, ISR 이 끼어들 때 줄이 섞이지 않음을 테스트로 증명한다.
- MCU 에서 newlib `printf` 를 피하는 이유(코드 크기, ISR 재진입, 블로킹)와 `__attribute__((format(printf, m, n)))` 로 포맷 실수를 컴파일 타임에 잡는 법을 설명할 수 있다.

## 1. 문제

"itoa 를 짜 보세요 — INT_MIN 은요?" 와 "printf 를 직접 짠다면?" 은 펌웨어 면접의 단골이고, 실무에서도 첫 보드 bring-up 때 가장 먼저 만드는 게 UART 로그다. 그런데 Cortex-M 에서 libc `printf` 는 플래시를 크게 먹고, ISR 에서 부르면 멈추거나 상태를 망가뜨릴 수 있다. 그래서 많은 팀이 **작고 재진입 가능한(reentrant) 포매터** 를 직접 갖고 있다.

이번 레슨은 그 포매터를 바닥부터 만든다. 핵심 설계는 **출력 콜백(putc callback) 하나로 모든 출력 대상을 통일** 하는 것이다 — 버퍼(`snprintf`), UART, 링 버퍼가 전부 "글자 1개를 받는 함수" 로 같아진다 (함수 포인터 + context 패턴은 C-M06 참고).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">이번 레슨의 전체 구조 — 가변 인자(...)는 맨 위에서 한 번만 va_list 로 바뀐다</text>
  <g stroke="currentColor">
    <rect x="10" y="36" width="220" height="44" rx="6" class="f-acc"/>
    <rect x="260" y="36" width="220" height="44" rx="6" class="f-acc"/>
    <rect x="510" y="36" width="220" height="44" rx="6" class="f-acc"/>
    <rect x="510" y="120" width="220" height="44" rx="6" class="f-ok"/>
    <rect x="260" y="120" width="220" height="44" rx="6" class="f-mut"/>
    <rect x="10" y="120" width="220" height="44" rx="6" class="f-mut"/>
    <rect x="10" y="210" width="220" height="44" rx="6" class="f-warn"/>
    <rect x="260" y="210" width="220" height="44" rx="6" class="f-mut"/>
    <rect x="510" y="210" width="220" height="44" rx="6" class="f-mut"/>
  </g>
  <g fill="currentColor">
    <text x="20" y="55">log_printf(lvl, fmt, ...)</text>
    <text x="20" y="72" font-size="12">va_start → v판 호출 → va_end</text>
    <text x="270" y="55">log_vprintf(lvl, fmt, ap)</text>
    <text x="270" y="72" font-size="12">"L: " + 메시지 + "\r\n" 조립</text>
    <text x="520" y="55" font-size="12">mini_vsnprintf(line+3, 28, ..)</text>
    <text x="520" y="72" font-size="12">잘림 규칙 담당 (bufsink)</text>
    <text x="520" y="139" font-size="12">mini_vformat(out, ctx, fmt, ap)</text>
    <text x="520" y="156" font-size="12">플래그→폭→변환→패딩, 상태 없음</text>
    <text x="270" y="139">my_utoa / my_itoa</text>
    <text x="270" y="156" font-size="12">Part A: 숫자 → 글자</text>
    <text x="20" y="139">char line[32]  (스택)</text>
    <text x="20" y="156" font-size="12">ISR 이 다시 불러도 안전</text>
    <text x="20" y="229">irq_save → ring_put × len</text>
    <text x="20" y="246" font-size="12">→ irq_restore (한 줄 = 한 덩어리)</text>
    <text x="270" y="229">TX ring[64]</text>
    <text x="270" y="246" font-size="12">꽉 차면 줄 통째로 버림 (안 막힘)</text>
    <text x="520" y="229">UART TX ISR → 핀</text>
    <text x="520" y="246" font-size="12">(uart_flush 가 흉내)</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M230 58 H258"/><path d="M480 58 H508"/><path d="M620 80 V118"/>
    <path d="M510 142 H482"/><path d="M260 142 H232"/><path d="M120 164 V208"/>
    <path d="M230 232 H258"/><path d="M480 232 H508"/>
  </g>
  <text x="10" y="285" fill="currentColor">A = 숫자 변환 · B = 포매터 + snprintf 규칙 · C = 로거 (va_list · 임계 구역 · 링)</text>
</svg>
```

### Part A — itoa: 임의 base, INT_MIN 포함

- `char *my_utoa(unsigned value, char *buf, int base)` — base 2..36, 숫자는 `0-9a-z` (소문자). 반환 `buf`.
- `char *my_itoa(int value, char *buf, int base)` — 음수면 `'-'` + 크기(magnitude). **어떤 base 에서도 같은 규칙** (`my_itoa(INT_MIN, b, 16)` → `"-80000000"`). `-value`, `abs(value)` 는 `INT_MIN` 에서 오버플로이므로 쓰지 않는다.
- base 가 범위 밖이면 `buf[0] = '\0'` 후 `NULL`. 버퍼는 `ITOA_BUFSZ` (= `sizeof(int)*CHAR_BIT + 2` = 34) 바이트.
- 테스트: 경계값 표 + 퍼즈 3000개(base 10/16/8 을 `snprintf("%d"/"%x"/"%o")` 와 비교) + 2000개 × base 2..36 을 libc `strtol` 로 되읽기(round-trip).

### Part B — mini_vformat / mini_vsnprintf / mini_snprintf

- `int mini_vformat(putc_fn out, void *ctx, const char *fmt, va_list ap)` — 코어. 지원: `%d %u %x %X %c %s %%`, 플래그 `-`(왼쪽 정렬) `0`(0 채움), 폭(10진 숫자). 정밀도·길이 수식어(`l`, `h`)는 없음. 모르는 변환은 원문 그대로 출력. 반환: 출력한 글자 수.
- `bufsink_putc` + `int mini_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)` — C11 `vsnprintf` 와 같은 잘림 규칙.
- `int mini_snprintf(char *buf, size_t size, const char *fmt, ...)` — `va_start` 해서 v판으로 넘기는 얇은 래퍼.
- 테스트: libc `vsnprintf` 와 **같은 인자** 로 돌려 반환값 + 64바이트 버퍼 전체(0xAA 센티널 포함)를 `memcmp`. 표 19개 + 무작위 플래그/폭/변환/size 퍼즈 3000개.

### Part C — UART 로그: log_printf / log_vprintf

- 한 줄 = `"<lvl>: " + 메시지 + "\r\n"`, 최대 `LOG_LINE_MAX`(32) 바이트. 메시지가 길면 잘라서 마지막 글자를 `'~'` 로.
- 줄을 **스택 버퍼** 에 다 만든 뒤, `irq_save()` ~ `irq_restore()` 안에서 TX 링 버퍼(64B)에 한 번에 넣는다. 자리가 없으면 **줄 통째로 버리고** `g_log_dropped++` (절대 기다리지 않는다).
- 시뮬레이터는 n번째 `ring_put` 직후 "인터럽트" 를 일으키고, 그 ISR 도 `log_printf('E', "isr%u", ...)` 를 부른다. 주어진 `log_printf_stream` (포맷하면서 바로 링에 넣는 버전)은 줄이 섞이는 것을 재현하고, 여러분의 `log_printf` 는 섞이지 않아야 한다.

## 2. Problem (English)

> **Write a tiny, reentrant printf for a UART logger.**
>
> 1. `char *my_utoa(unsigned v, char *buf, int base)` and `char *my_itoa(int v, char *buf, int base)` for bases 2..36 (lowercase digits). Negative values print as `'-'` followed by the magnitude in any base. `my_itoa(INT_MIN, b, 10)` must return `"-2147483648"` without signed overflow. Invalid base → `buf[0] = '\0'`, return `NULL`.
> 2. `int mini_vformat(putc_fn out, void *ctx, const char *fmt, va_list ap)` supporting `%d %u %x %X %c %s %%`, flags `-` and `0`, and a decimal field width. On top of it, `mini_vsnprintf` / `mini_snprintf` with exact C11 `snprintf` semantics: never write more than `size` bytes; if `size > 0` the output is always NUL-terminated; the return value is the length the full output would have had (excluding the NUL); with `size == 0` nothing is written and `buf` may be `NULL`.
> 3. `int log_printf(char lvl, const char *fmt, ...)` that formats `"<lvl>: <msg>\r\n"` (max 32 bytes, truncated messages end in `'~'`) into a stack buffer and enqueues the whole line into a 64-byte TX ring buffer inside a critical section. If the line does not fit, drop it and count the drop. An interrupt that logs while `log_printf` is running must not split the line.
>
> Verify against libc `vsnprintf` with table cases and a fixed-seed fuzzer. Then explain: why not just call newlib `printf` on a Cortex-M0 — and never from an ISR? What does `__attribute__((format(printf, 2, 3)))` buy you?
>
> Examples: `mini_snprintf(b, 5, "%05d", -7)` → returns 5, `b = "-000"`; `mini_snprintf(NULL, 0, "%s-%d", "ab", 7)` → 4; `log_printf('I', "temp=%d", 25)` → wire `"I: temp=25\r\n"`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 줄과 "0/54 passed" — TODO 8개를 채워 54/54 를 만든다
```

관찰용 파일 (하네스와 별개, 각자 빌드 명령이 파일 머리에 있다):

```bash
cc -std=c11 -O0 demo_intmin.c -o d0 && ./d0      # INT_MIN 에서 깨지는 교과서 itoa (-O0 / -O2 / UBSan 비교)
cc -std=c11 -Wall -c demo_format_check.c -o /dev/null      # format 속성이 잡는 경고들
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -Os -ffreestanding -c demo_target.c -o m4.o \
  && xcrun llvm-size -A m4.o                                # 포매터의 Cortex-M 코드 크기
```

## 4. 힌트

<details>
<summary>힌트 1 — itoa: 낮은 자리부터 나오니 뒤집는다, 0 은 do-while</summary>

`v % base` 가 가장 낮은 자리, `v /= base` 로 다음 자리. 임시 배열에 거꾸로 쌓고 마지막에 뒤집어 복사한다. `while (v)` 로 쓰면 `v == 0` 일 때 빈 문자열이 나온다 → `do { } while (v);`.

</details>

<details>
<summary>힌트 2 — INT_MIN: 크기는 unsigned 에서 구한다</summary>

`int` 에서 부호를 떼지 말고, `unsigned m = (unsigned)v;` (음수 → v + 2³², 잘 정의된 변환) 다음 `m = 0u - m;` 하면 정확히 |v| 다. unsigned 산술은 mod 2³² 라 오버플로가 없다. 그다음은 `my_utoa(m, ...)` 재사용.

</details>

<details>
<summary>힌트 3 — 포매터: 스펙 하나 = 플래그 → 폭 → 변환 → 패딩</summary>

`%` 를 만나면 ① `-`/`0` 을 반복해서 먹고 ② 숫자를 폭으로 누적하고 ③ 변환 문자로 `switch`. 숫자 변환은 **부호를 본문과 따로** 들고 있다가, 패딩 순서 `[공백][부호][0들][본문][공백]` 으로 내보낸다. `'-'` 가 있으면 `'0'` 은 무시. `%c` 는 `va_arg(ap, int)` — `char` 는 int 로 승격되어 넘어온다.

</details>

<details>
<summary>힌트 4 — snprintf 규칙은 "출력 대상" 이 책임진다</summary>

코어는 글자 수만 세고, 버퍼 sink 가 `len + 1 < size` 일 때만 실제로 쓰고 `len` 은 항상 증가시킨다 (마지막 칸은 NUL 몫). 끝나면 `size > 0` 일 때 `buf[min(len, size-1)] = '\0'`. 로거는 같은 코어에 다른 sink 를 꽂으면 되고, 줄을 다 만든 뒤에 임계 구역에서 링에 넣는다.

</details>

## 5. 풀이

### Part A — itoa 와 INT_MIN

자리 뽑기는 단순하다. `1234` 를 base 10 으로:

| 단계 | `v` | `v % 10` → tmp | `v / 10` |
|---|---|---|---|
| 1 | 1234 | `'4'` | 123 |
| 2 | 123 | `'3'` | 12 |
| 3 | 12 | `'2'` | 1 |
| 4 | 1 | `'1'` | 0 → 끝 |

tmp = `"4321"` → 뒤집어 `"1234"`. 함정은 전부 **음수 처리** 에 있다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">32비트 int 의 비대칭: 음수 쪽이 하나 더 많다</text>
  <line x1="40" y1="70" x2="700" y2="70" stroke="currentColor"/>
  <g stroke="currentColor"><line x1="60" y1="62" x2="60" y2="78"/><line x1="370" y1="62" x2="370" y2="78"/><line x1="680" y1="62" x2="680" y2="78"/></g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="98">INT_MIN = -2147483648</text>
    <text x="355" y="98">0</text>
    <text x="560" y="98">INT_MAX = 2147483647</text>
  </g>
  <rect x="10" y="118" width="350" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <rect x="380" y="118" width="350" height="56" rx="6" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="20" y="138">v = -v;  또는  abs(v)</text>
    <text x="20" y="158" font-size="12">+2147483648 은 int 에 없음 → 오버플로 = UB (6.5p5)</text>
    <text x="390" y="138">m = (unsigned)v;  m = 0u - m;</text>
    <text x="390" y="158" font-size="12">0u - 0x80000000 = 0x80000000 = 2147483648 ✔</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="200">(unsigned)v : 음수 → v + 2³² (6.3.1.3p2, 항상 정의됨)</text>
    <text x="10" y="218">0u - m : unsigned 산술은 mod 2³² (6.2.5p9) → 결과는 정확히 |v|</text>
    <text x="10" y="238">크기(magnitude)는 0 ~ 2³¹ 이라 unsigned 에 항상 들어간다. 부호 '-' 는 따로 붙인다.</text>
    <text x="10" y="258">다른 해법: 음수 영역에서 계산 (digit = -(v % 10), v /= 10) — C99+ 는 0 쪽 버림이라 v % 10 ∈ [-9, 0].</text>
  </g>
</svg>
```

`demo_intmin.c` 는 교과서에 흔한 두 버전(① `v = -v` ② `abs(v)`)과 ③ unsigned 버전을 `INT_MIN` 에 돌린다. 실제 출력:

```text
== -O0
negate: "-("
abs   : "-2147483648"
ok    : "-2147483648"
-7 % 10 = -7, -7 / 10 = 0   (C99+: 0 쪽으로 버림)
== -O2
negate: "-2147483648"
abs   : "-2147483648"
ok    : "-2147483648"
-7 % 10 = -7, -7 / 10 = 0   (C99+: 0 쪽으로 버림)
== -fsanitize=undefined (-O1)   — SUMMARY 줄과 이어지는 출력(-O0 과 같음)은 생략
demo_intmin.c:14:34: runtime error: negation of -2147483648 cannot be represented in type 'int'; cast to an unsigned type to negate this value to itself
demo_intmin.c:24:32: runtime error: negation of -2147483648 cannot be represented in type 'int'; cast to an unsigned type to negate this value to itself
```

- `-O0` 의 `"-("`: `-INT_MIN` 이 하드웨어에서 감싸져(wrap) 그대로 `INT_MIN`. 그러면 `v % 10 = -8` → `'0' + (-8)` = `'('`, `v / 10 = -214748364` 라 `while (v > 0)` 이 바로 끝난다.
- `-O2` 에선 **우연히 맞는다**. 최적화기는 "`-v` 는 오버플로하지 않는다(UB 이므로)" 고 가정하고 코드를 바꿨고, 그 결과가 이번엔 맞았을 뿐이다. 테스트가 `-O2` 로만 돌면 이 버그는 통과한다 — 그래서 UBSan 이 필요하다.
- `abs(INT_MIN)` 도 C11 7.22.6.1p2 "결과를 표현할 수 없으면 UB". UBSan 이 같은 negation 으로 잡았다 (clang 이 `abs` 를 인라인으로 펼쳤다).

**base 와 부호 규칙**: `itoa` 는 표준 C 함수가 아니다(AVR libc, MSVC `_itoa` 등의 확장). MSVC `_itoa` 는 **base 10 일 때만** `'-'` 를 붙이고 다른 base 는 2의 보수 비트 그대로(`_itoa(-255, b, 16)` → `"ffffff01"`) 출력한다고 문서화되어 있다. 이 레슨은 "모든 base 에서 부호 + 크기" 로 정했고, 2의 보수 hex 가 필요하면 `my_utoa((unsigned)v, b, 16)` 을 쓰면 된다 — 면접에선 **규칙을 먼저 말하고** 짜는 게 점수다.

**버퍼 크기**: base 2 최악 = 부호 1 + 32자리 + NUL 1 = 34 = `sizeof(int)*CHAR_BIT + 2`. 10진만이면 12 (`"-2147483648"` 11자 + NUL).

**Cortex-M 에서의 나눗셈**: `demo_target.c` 의 `utoa10` (상수 10으로 나누는 버전)을 크로스 컴파일한 실제 asm 루프:

```text
; Cortex-M4 (-Os) — 루프 부분
	movw	lr, #52429
	movt	lr, #52428              ; lr = 0xCCCCCCCD (10으로 나누기 위한 magic number)
.LBB0_1:
	umull	r3, r4, r2, lr          ; 64비트 곱의 상위 32비트
	lsrs	r3, r4, #3              ; q = v / 10
	add.w	r4, r3, r3, lsl #2      ; q*5
	sub.w	r4, r2, r4, lsl #1      ; r = v - q*10
	orr	r4, r4, #48             ; '0' + r
	strb.w	r4, [r12, r0]

; Cortex-M0 (-Os) — 루프 부분
.LBB0_1:
	movs	r7, #10
	mov	r0, r6
	mov	r1, r7
	bl	__aeabi_uidiv           ; 소프트웨어 나눗셈 (런타임 라이브러리)
	muls	r7, r0, r7
	subs	r1, r6, r7              ; r = v - q*10
	movs	r2, #48
	orrs	r2, r1
	mov	r1, sp
	strb	r2, [r1, r5]
```

M4 는 상수 나눗셈을 "곱하고 시프트"(magic number `0xCCCCCCCD`)로 바꿔 나눗셈 명령조차 안 쓴다. 임의 base 를 받는 `my_utoa` 는 `udiv` + `mls` (나머지 = v − q·b) 가 나왔다. **M0/M0+ (ARMv6-M) 는 나눗셈 명령도, 64비트 결과 곱셈(`umull`)도 없어서** `bl __aeabi_uidiv` 런타임 호출이 자리마다 들어간다 — 추가 연습 4 가 나눗셈 없는 변환을 다룬다.

### Part B — va_list 와 포매터

**가변 인자는 어떻게 넘어오나.** `...` 자리의 인자에는 프로토타입이 없으므로 **기본 인자 승격(default argument promotions)** 이 적용된다 (C11 6.5.2.2p6–7): `char`/`short`/`_Bool` → `int` (C-B02 의 정수 승격), `float` → `double`. 그래서 `%c` 는 `va_arg(ap, int)` 로 꺼내야 하고, `va_arg(ap, char)` 는 UB 다 (7.16.1.1p2 — 실제 다음 인자 타입과 호환되지 않음). clang 은 이를 `-Wvarargs` 로 경고한다 (아래 Part C 의 demo 출력).

| `va_list` 실체 (clang, 직접 확인) | `sizeof(va_list)` |
|---|---|
| arm64 Apple (이 맥) | 8 — 그냥 `char *` |
| AArch64 Linux (AAPCS64) | 32 — 레지스터 저장 영역 포인터들 담은 구조체 |
| x86-64 SysV | 24 — `struct __va_list_tag[1]` (**배열 타입** → 함수 인자로 넘기면 포인터로 decay) |
| Cortex-M (AAPCS32, thumbv7em) | 4 — `struct { void *__ap; }` |

ABI 마다 모양이 전혀 다르므로 **`va_list` 는 오직 `va_start / va_arg / va_copy / va_end` 로만** 다룬다. 대입(`ap2 = ap`)은 x86-64 에서 컴파일조차 안 된다(배열) — 복사는 `va_copy`.

**스펙 파싱 순서와 패딩.**

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">"%-08d" 같은 스펙 하나를 읽는 순서 (정밀도·길이 수식어는 이번 범위 밖)</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="80" height="40" rx="6" class="f-mut"/>
    <rect x="130" y="40" width="150" height="40" rx="6" class="f-acc"/>
    <rect x="320" y="40" width="150" height="40" rx="6" class="f-acc"/>
    <rect x="510" y="40" width="220" height="40" rx="6" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="38" y="65">'%'</text>
    <text x="140" y="58">① 플래그 반복</text><text x="140" y="74" font-size="12">'-' 왼쪽, '0' 0 채움</text>
    <text x="330" y="58">② 폭 = 숫자 누적</text><text x="330" y="74" font-size="12">width*10 + (c-'0')</text>
    <text x="520" y="58">③ 변환: d u x X c s %</text><text x="520" y="74" font-size="12">그 외 → 원문 그대로 출력</text>
  </g>
  <g stroke="currentColor" fill="none"><path d="M90 60 H128"/><path d="M280 60 H318"/><path d="M470 60 H508"/>
    <path d="M190 40 C 190 22, 230 22, 230 40"/></g>
  <text x="10" y="112" fill="currentColor" font-weight="bold">④ 패딩 배치: [공백] [부호] [0들] [본문] [공백]   (pad = width − (부호 + 본문 길이), 음수면 0)</text>
  <g font-family="monospace" fill="currentColor">
    <text x="20" y="140">"[%5d]"   -42  →  [  -42]    공백이 부호 앞</text>
    <text x="20" y="162">"[%05d]"  -42  →  [-0042]    0 은 부호 뒤 (부호를 따로 들고 있어야 하는 이유)</text>
    <text x="20" y="184">"[%-5d]"  -42  →  [-42  ]    '-' 이면 오른쪽 공백</text>
    <text x="20" y="206">"[%-05d]" -42  →  [-42  ]    '-' 가 '0' 을 이긴다 (7.21.6.1p6)</text>
    <text x="20" y="228">"[%2u]"  12345 →  [12345]    폭은 최소 폭: 절대 자르지 않는다</text>
  </g>
  <text x="10" y="252" fill="currentColor" font-size="12">'0' 플래그는 d i o u x X (와 부동소수) 에만 정의됨. %0s / %0c 는 UB → 이 구현은 공백으로 처리.</text>
</svg>
```

부호를 본문과 함께 `tmp` 에 넣어 버리면(`my_itoa` 결과를 그대로 쓰면) `%05d` 에서 0 이 부호 앞에 붙는다. 실제로 정답 코드에서 두 `emit_n` 줄의 순서만 바꿔 돌려 보면 퍼즈가 바로 잡는다 (실제 출력):

```text
  mismatch size=32 fmt="<%013d>": libc 15 "<-000003598892>"  mini 15 "<00000-3598892>"
  mismatch size=28 fmt="<%010d>": libc 12 "<-000000210>"  mini 12 "<000000-210>"
```

**snprintf 의 잘림 규칙 (C11 7.21.6.5p2–3, vsnprintf 는 7.21.6.12).**

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">snprintf(buf, 8, "%s", "abcdefghij")  →  반환 10</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="40" width="40" height="34"/><rect x="60" y="40" width="40" height="34"/><rect x="100" y="40" width="40" height="34"/>
    <rect x="140" y="40" width="40" height="34"/><rect x="180" y="40" width="40" height="34"/><rect x="220" y="40" width="40" height="34"/>
    <rect x="260" y="40" width="40" height="34"/>
  </g>
  <rect x="300" y="40" width="40" height="34" class="f-acc" stroke="currentColor"/>
  <rect x="340" y="40" width="80" height="34" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="40" y="62">a</text><text x="80" y="62">b</text><text x="120" y="62">c</text><text x="160" y="62">d</text>
    <text x="200" y="62">e</text><text x="240" y="62">f</text><text x="280" y="62">g</text><text x="320" y="62">\0</text>
    <text x="380" y="62">h i j</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="94">buf[0..7] — size 만큼만 쓴다</text>
    <text x="345" y="94">안 씀, 하지만 셈</text>
  </g>
  <g fill="currentColor">
    <text x="440" y="50">· size &gt; 0 → 항상 NUL 종료</text>
    <text x="440" y="70">· 반환 = 다 썼다면의 길이 (NUL 제외)</text>
    <text x="440" y="90">· 반환 &gt;= size ⇔ 잘렸다</text>
    <text x="20" y="130">size == 0 : 아무것도 쓰지 않음, buf 는 NULL 이어도 됨</text>
    <text x="40" y="150">→ n = snprintf(NULL, 0, ...) 로 크기 재기 (2-pass)</text>
    <text x="20" y="172">size == 1 : NUL 한 바이트만.     반환 음수 : 인코딩 오류 (POSIX 는 INT_MAX 초과 시 -1 + EOVERFLOW)</text>
    <text x="20" y="200">구현: sink 는 "len + 1 &lt; size 일 때만 쓰기, len 은 항상 ++" → 끝에서 buf[min(len, size-1)] = '\0'</text>
    <text x="20" y="222">strlcpy 의 "반환값 = 원본 길이" 와 같은 철학 (C-B07 참고) — 호출자가 잘림을 감지할 수 있다</text>
  </g>
</svg>
```

`%c` 로 `'\0'` 을 찍으면 그것도 한 글자다: `snprintf(b, 4, "a%cb", 0)` → 반환 3, 버퍼 `a \0 b \0`. 그래서 `%c` 는 `strlen` 이 아니라 `len = 1` 로 처리한다. 테스트는 이 바이트들까지 libc 와 `memcmp` 로 비교한다.

**libc 와 대조하는 법 — `va_copy`.** 테스트 헬퍼 `cmp_libc(size, fmt, ...)` 는 같은 가변 인자를 `vsnprintf` 와 `mini_vsnprintf` 에 **두 번** 넘긴다. C11 7.16p3: `va_list` 를 받은 함수가 `va_arg` 를 쓰고 나면 호출자 쪽 `ap` 는 **불확정(indeterminate)** 이다. 그래서 `va_copy(ap2, ap)` 로 사본을 만들어 각자 하나씩 쓰고, `va_start`/`va_copy` 각각에 `va_end` 를 짝지어 부른다 (7.16.1.3). arm64 맥에선 `ap` 가 그냥 포인터라 "운 좋게" 동작할 수 있지만, x86-64/AArch64 Linux 에선 첫 호출이 구조체 안의 오프셋을 전진시켜 두 번째 호출이 쓰레기를 읽는다.

테스트 구성과 실제 결과:

| 그룹 | 내용 | 결과 |
|---|---|---|
| itoa 표 | 0, -1, INT_MIN(10/16/2진), INT_MAX(36진 `zik0zj`), UINT_MAX, base 1/37 | 14 PASS |
| itoa 퍼즈 | 3000값 × base 10/16/8 vs snprintf, 2000값 × base 2..36 strtol 왕복 | 0 불일치 |
| vsnprintf 표 | 플래그 조합, 부호+0, 폭<길이, `%c` 의 NUL, size 0/1/4/5/6/8 | 19 PASS |
| mini 전용 | `%s` NULL → `"(null)"`, 직접 호출, 2-pass, `%q`·끝의 `%` | 4 PASS |
| vsnprintf 퍼즈 | 고정 시드 xorshift32: `<%[-][0][w]{duxXcs}>` × size 0..40 × 경계 편향 값, 3000개 | 0 불일치 |

**테스트에서 마주친 컴파일러 검사.** `mini_snprintf` 에 format 속성을 달았기 때문에, 처음에 표에 리터럴로 넣었던 두 케이스가 `-Werror` 로 막혔다 (실제 출력):

```text
error: flag '0' is ignored when flag '-' is present [-Werror,-Wformat]          ← "[%-05d]"
error: invalid conversion specifier '|' [-Werror,-Wformat-invalid-specifier]    ← "%q|%"
```

두 번째가 재미있다: clang 은 `%q` 의 `q` 를 BSD 의 길이 수식어(quad = `ll`)로 읽고, 그 뒤 `|` 를 변환 문자로 본다. 일부러 이상한 포맷을 시험하는 케이스만 포인터 변수(`const char *odd = "%q|%";`)로 넘겨 검사를 우회했다.

### Part C — 로거: va_list 전달, 재진입, 줄 단위 원자성

**v판(v-variant)이 필요한 이유.** C 에는 "받은 `...` 를 다른 가변 함수에 그대로 넘기는" 문법이 없다. 그래서 printf 류 API 는 언제나 쌍으로 만든다: `log_printf(fmt, ...)` 는 `va_start` → `log_vprintf(fmt, ap)` → `va_end` 만 하고, 실제 일은 v판이 한다. 그래야 상위 계층(예: `LOG_ERR(...)` 매크로, 모듈별 래퍼)이 또 감쌀 수 있다. libc 의 `printf/vprintf`, Zephyr 의 `printk/vprintk`, ESP-IDF 의 `esp_log_write/esp_log_writev` 가 모두 이 모양이다.

한 가지 표준 함정: `va_start(ap, parmN)` 의 `parmN` (마지막 이름 있는 매개변수)이 승격되는 타입(`char`, `short`, `float`)이면 UB 다 (C11 7.16.1.4p4). `log_printf(char lvl, const char *fmt, ...)` 처럼 **`fmt` 를 마지막** 에 두는 이유 중 하나. (C23 은 `va_start` 의 두 번째 인자를 선택으로 바꿨다.)

**줄 섞임 재현.** 링의 5번째 `ring_put` 직후 인터럽트가 들어오고, ISR 도 로그를 찍는다:

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">log("temp=%d", 25) 도중 5번째 put 직후 IRQ — ISR: log('E', "isr1")</text>
  <text x="10" y="58" fill="currentColor">stream</text>
  <rect x="80" y="40" width="130" height="28" class="f-acc" stroke="currentColor"/><text x="90" y="59" fill="currentColor" font-family="monospace">I: te</text>
  <rect x="220" y="40" width="170" height="28" class="f-warn" stroke="currentColor"/><text x="230" y="59" fill="currentColor" font-family="monospace">E: isr1\r\n</text>
  <rect x="400" y="40" width="130" height="28" class="f-acc" stroke="currentColor"/><text x="410" y="59" fill="currentColor" font-family="monospace">mp=25\r\n</text>
  <text x="545" y="59" fill="currentColor">→ 줄이 찢어짐</text>
  <text x="10" y="128" fill="currentColor">line</text>
  <rect x="80" y="110" width="110" height="28" class="f-mut" stroke="currentColor"/><text x="88" y="129" fill="currentColor" font-size="12">스택에서 포맷</text>
  <rect x="200" y="110" width="40" height="28" class="f-mut" stroke="currentColor"/><text x="208" y="129" fill="currentColor" font-size="12">save</text>
  <rect x="250" y="110" width="160" height="28" class="f-acc" stroke="currentColor"/><text x="260" y="129" fill="currentColor" font-family="monospace">I: temp=25\r\n</text>
  <rect x="420" y="110" width="58" height="28" class="f-mut" stroke="currentColor"/><text x="425" y="129" fill="currentColor" font-size="12">restore</text>
  <rect x="488" y="110" width="140" height="28" class="f-ok" stroke="currentColor"/><text x="498" y="129" fill="currentColor" font-family="monospace">E: isr1\r\n</text>
  <path d="M300 150 Q 420 185 548 142" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="250" y="190" fill="currentColor" font-size="12">마스크 중 도착한 IRQ 는 펜딩 → restore 직후 실행</text>
  <text x="10" y="222" fill="currentColor">핵심: 포맷(느림, 마스크 없이) 과 링 갱신(빠름, 마스크 안) 을 분리. 줄 하나가 원자적 단위가 된다.</text>
  <text x="10" y="242" fill="currentColor">ISR 이 포맷 도중에 와도 괜찮다: 포매터에는 static 가변 상태가 없고, 줄 버퍼는 각자의 스택에 있다.</text>
</svg>
```

테스트의 기대값이 정확히 이 두 줄이다: 스트리밍 `"I: teE: isr1\r\nmp=25\r\n"`, 줄 단위 `"I: temp=25\r\nE: isr1\r\n"`.

`log_vprintf` 의 길이 계산 (`LOG_LINE_MAX` = 32):

| 구간 | 바이트 | 비고 |
|---|---|---|
| `"L: "` | 3 | `line[0..2]` |
| 메시지 | 최대 27 | `mini_vsnprintf(line+3, 28, ...)` — 28 = 27 + NUL 자리 |
| `"\r\n"` | 2 | NUL 자리를 `\r` 이 덮는다 → 링에는 NUL 을 안 넣는다 |

반환값 `m > 27` 이면 잘린 것 → `line[3+27-1] = '~'`. 이것이 Part B 에서 지킨 "반환값 = 썼을 길이" 규칙의 실제 쓸모다: 호출자가 **잘림을 공짜로 감지** 한다.

**링이 꽉 찼을 때.** ISR 에서도 불리는 로거는 **절대 기다리면 안 된다**. 선택지는 ① 줄 통째로 버리고 카운트(이번 구현) ② 오래된 줄 덮어쓰기 ③ 부분 줄 쓰기. ③ 은 출력이 깨져 보이므로 피하고, 버린 개수를 나중에 `"[dropped 3]"` 로 찍어 주는 게 흔한 패턴이다. 주어진 스트리밍 버전은 글자 단위로 버려서 부분 줄이 생긴다.

**왜 newlib printf 를 그냥 쓰지 않나.**

`demo_target.c` (이번 포매터와 같은 코드, 프리스탠딩) 를 Cortex-M 용으로 컴파일해 `xcrun llvm-size -A` / `llvm-nm -S` 로 잰 실제 값:

| 빌드 | `.text` | 내역 |
|---|---|---|
| Cortex-M4 `-Os` | 850 B (+ `.rodata` 44 B) | `mini_vformat` 688 + `my_utoa` 82 + `utoa10` 80 |
| Cortex-M0 `-Os` | 808 B (+ 44 B) | + 링크 시 `__aeabi_uidiv` (compiler-rt/libgcc) 추가 |
| Cortex-M4 `-O2` | 2474 B | 인라인·언롤로 3배 |

즉 **1KB 미만** 이다. newlib 의 `printf` 는 (이 맥엔 arm-none-eabi 툴체인이 없어 여기서 측정하지 않았다) 부동소수점 변환(`_dtoa_r` 와 그 큰수 연산·`malloc`), 위치 인자 `%n$`, 와이드 문자·로캘, `FILE` 스트림 버퍼링과 `_write` 시스템 콜 계층까지 끌고 들어와 **수십 KB 단위** 로 커지는 것으로 잘 알려져 있다. newlib-nano (`--specs=nano.specs`) 는 이를 크게 줄이고 float 출력을 기본으로 빼며, 필요하면 `-u _printf_float` 로 다시 넣는다. 16~64KB 플래시 MCU 에선 이 차이가 결정적이다. 실측하려면 `arm-none-eabi-gcc ... -Wl,--print-memory-usage` 나 `-Wl,-Map` 으로 비교하면 된다.

측정 중 하나 더 배운 것: `-ffreestanding` 없이 컴파일하면 clang 이 내 `my_strlen` 루프를 **`bl strlen` 라이브러리 호출로 바꿔 버렸다** (asm 에 4번 등장, `-ffreestanding` 이나 `-fno-builtin` 으로 0번). 루프 → `memset`/`memcpy`/`strlen` 인식(idiom recognition)은 libc 가 없거나 그 함수 자신을 구현하는 펌웨어에서 링크 오류·무한 재귀의 원인이 된다 (C-M08 참고).

**ISR 에서 printf 를 부르면 안 되는 이유 (재진입성).**

| 문제 | 무슨 일이 생기나 |
|---|---|
| 공유 상태 | `stdout` `FILE` 의 버퍼 포인터·카운터를 main 이 갱신하던 중 ISR 의 printf 가 같은 구조체를 고친다 → 출력 유실·버퍼 오염 |
| 락 | newlib 은 스트림 락(`__retarget_lock_acquire_recursive`)을 잡는다. RTOS 가 이 락을 뮤텍스로 구현했으면 ISR 에서 블록 시도 → assert/HardFault, main 이 락을 쥔 상태면 교착 |
| 힙 | 첫 출력 때 스트림 버퍼, float 변환 때 큰수 버퍼를 `malloc` 할 수 있다 → ISR 에서 힙은 금물 |
| 시간 | `_write` 가 UART 폴링이면 115200 8N1 에서 바이트당 10비트 = 86.8 µs → 80자 한 줄에 약 6.9 ms 동안 ISR 이 안 끝난다 |

이번 설계는 ① 포매터에 **가변 static 상태 없음**(`digits[]` 는 `static const` 라 읽기 전용 — 재진입에 무해) ② 줄 버퍼는 스택 ③ 공유 자원(링)은 짧은 임계 구역 ④ 실제 전송은 TX 인터럽트가 비동기로 — 로 네 가지를 모두 피한다. 비교: 정적 버퍼를 돌려주는 옛날식 `itoa` 나 `strtok`, `inet_ntoa` 는 재진입 불가의 교과서 예다.

임계 구역은 `irq_save()/irq_restore(prev)` (PRIMASK 저장·복원) 형태다. 단순 `disable/enable` 쌍이면, 이미 인터럽트가 막힌 문맥(다른 임계 구역 안, 또는 PRIMASK 를 올린 상태)에서 로거를 부른 순간 enable 이 **남의 임계 구역을 깨 버린다**. 그리고 이 구간은 `len` 바이트 복사뿐이라 마스크 시간이 수십 사이클 수준으로 짧다 — 포맷까지 마스크 안에서 하면 인터럽트 지연(latency)이 포맷 시간만큼 늘어난다.

**format 속성으로 호출부 검사.** `demo_format_check.c` 를 `cc -std=c11 -Wall -c` 로 컴파일한 실제 경고 (속성 없는 `log_unchecked` 의 같은 실수들은 **아무 경고 없음**):

```text
demo_format_check.c:14:35: warning: format specifies type 'unsigned int' but the argument has type 'uint64_t' (aka 'unsigned long long') [-Wformat]
demo_format_check.c:15:33: warning: format specifies type 'int' but the argument has type 'const char *' [-Wformat]
demo_format_check.c:16:30: warning: format specifies type 'int' but the argument has type 'double' [-Wformat]
demo_format_check.c:17:31: warning: more '%' conversions than data arguments [-Wformat-insufficient-args]
demo_format_check.c:18:24: warning: more '%' conversions than data arguments [-Wformat-insufficient-args]
demo_format_check.c:19:33: warning: data argument not used by format string [-Wformat-extra-args]
demo_format_check.c:26:25: warning: second argument to 'va_arg' is of promotable type 'char'; this va_arg has undefined behavior because arguments will be promoted to 'int' [-Wvarargs]
```

`format(printf, 2, 3)` = "2번째 매개변수가 printf 형식 문자열, 3번째부터가 대응하는 인자". v판은 `format(printf, 2, 0)` (검사할 가변 인자가 없음, 형식 문자열만 추적). 속성이 없는 래퍼가 v판에 `fmt` 를 넘기면 `-Wformat-nonliteral` 이 `format string is not a string literal` 로 알려 준다 (실제 확인) — 래퍼에도 속성을 달라는 신호다. 주의: archetype 이 `printf` 이므로 내가 지원하지 않는 `%f` 를 써도 컴파일러는 통과시킨다. 지원 범위는 문서와 테스트로 지킨다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 54/54 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 54/54, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `if (v < 0) { *p++ = '-'; v = -v; }` | `INT_MIN` 에서 signed 오버플로 UB. `-O0` 에선 `"-("`, `-O2` 에선 우연히 정답 (demo_intmin) | `unsigned m = (unsigned)v; if (v < 0) m = 0u - m;` |
| `while (v) { ... }` 로 자리 뽑기 | `v == 0` 이면 빈 문자열 | `do { ... } while (v);` |
| `char buf[11]` 로 int 10진 변환 | `"-2147483648"` 은 11자 + NUL = 12 | `ITOA_BUFSZ` 또는 `12` |
| `%05d` 를 `my_itoa` 결과에 0 패딩 | `"00-42"` — 부호가 0 뒤로 감 | 부호를 따로 들고 `[부호][0들][숫자]` |
| `va_arg(ap, char)` / `va_arg(ap, float)` | 기본 인자 승격으로 `int`/`double` 이 넘어옴 → UB | `(char)va_arg(ap, int)`, `va_arg(ap, double)` |
| `ap` 를 한 함수에 넘긴 뒤 다시 사용 | 7.16p3: 호출자 쪽 `ap` 는 불확정 (x86-64/AArch64 Linux 에서 실제로 깨짐) | `va_copy` 로 사본, 각각 `va_end` |
| `snprintf` 반환값을 "쓴 바이트 수" 로 믿고 `off += n` | 잘리면 `off > size` → 다음 `size - off` 가 size_t 언더플로 → 버퍼 넘침 | `if ((size_t)n >= size - off) 잘림 처리` (추가 연습 3) |
| size 가 0 일 때도 `buf[0] = '\0'` | size 0 은 "아무것도 쓰지 않음", buf 가 NULL 일 수 있다 | `if (size > 0)` 안에서만 NUL |
| 로그를 `log_printf(user_string)` | 사용자 문자열의 `%` 가 스택을 읽는다 (format string 취약점) | `log_printf("%s", user_string)` |
| ISR 에서 `printf` / 스트리밍 로거 | 락·힙·FILE 공유 상태, UART 블로킹, 줄 섞임 | 스택 줄 버퍼 + 임계 구역 링 + TX ISR |
| `disable_irq(); ...; enable_irq();` 를 로거 안에서 | 호출자가 이미 막아 둔 상태를 풀어 버림 | `prev = irq_save(); ...; irq_restore(prev);` |

## 8. 면접 Q&A

<details>
<summary>Q1. itoa 를 짜는데 INT_MIN 이 왜 특별한가요? 어떻게 처리하나요?</summary>

2의 보수 32비트 int 범위는 −2³¹ … 2³¹−1 로 비대칭이라 `-INT_MIN` 은 표현할 수 없고, signed 오버플로는 UB 다 (6.5p5). `abs(INT_MIN)` 도 UB (7.22.6.1p2). 해법은 크기를 unsigned 로 구하는 것: `m = (unsigned)v` (6.3.1.3p2 로 정의된 변환) 후 `m = 0u - m`. 또는 음수 영역에서 자리를 뽑는다 (`-(v % 10)`, C99 의 0 쪽 버림 덕분에 −9..0). 꼬리질문 "테스트는 통과하던데요?" → demo_intmin 처럼 `-O2` 에선 우연히 맞을 수 있다. `-fsanitize=undefined` 로 돌려야 잡힌다.

</details>

<details>
<summary>Q2. snprintf 의 반환값은 정확히 뭐고, 잘림은 어떻게 감지하나요?</summary>

버퍼가 충분했다면 썼을 글자 수 (NUL 제외), 인코딩 오류면 음수 (7.21.6.5p3). `n >= 0 && (size_t)n < size` 일 때만 완전히 쓰였다. size>0 이면 항상 NUL 종료, size==0 이면 아무것도 안 쓰고 buf 는 NULL 이어도 된다 → `n = snprintf(NULL, 0, ...)` 로 필요한 크기를 잰다. 꼬리질문 "MSVC 옛 `_snprintf` 는?" → 잘리면 −1 을 돌려주고 NUL 도 안 붙였다. 이식 코드에서 흔한 사고 원인이라 C99 규칙의 `snprintf` 를 쓰거나 래퍼로 통일한다.

</details>

<details>
<summary>Q3. ISR 에서 printf 를 부르면 왜 안 되나요? 그럼 ISR 에서 로그는 어떻게 남기나요?</summary>

printf 는 재진입 가능하지 않다: `FILE` 공유 상태, 스트림 락(RTOS 뮤텍스면 ISR 에서 블록 불가), 첫 출력·float 변환 때의 `malloc`, 그리고 폴링 UART 출력이면 115200bps 에서 한 줄에 수 ms 동안 ISR 이 안 끝난다. ISR 에선 ① 스택 버퍼에 포맷 + 짧은 임계 구역으로 링에 넣고 TX 인터럽트/DMA 가 내보내거나 ② 더 가볍게 "이벤트 코드 + 값" 만 링에 넣고 main/idle 에서 포맷하는 **지연 로깅(deferred logging)** 을 쓴다. Zephyr 의 deferred 모드, 형식 문자열은 호스트에만 두고 ID 와 인자만 보내는 dictionary 로깅(Rust 의 defmt 도 같은 발상)이 그 예다.

</details>

<details>
<summary>Q4. MCU 에서 printf 가 왜 그렇게 큰가요? 줄이는 방법은?</summary>

printf 하나가 모든 변환(부동소수점 `%f %e %g` 와 그 정밀 변환 코드, `%n$` 위치 인자, 와이드 문자, 로캘)과 stdio 스트림 계층을 끌고 온다. 줄이는 법: newlib-nano(`--specs=nano.specs`, float 출력은 `-u _printf_float` 로만), float 출력 대신 고정소수점 정수 출력(추가 연습 1), 필요한 변환만 가진 작은 포매터(이번 레슨: M4 `-Os` 약 0.8KB 실측; 오픈소스로 mpaland/printf 계열, nanoprintf, Zephyr cbprintf 등), `-ffunction-sections -fdata-sections -Wl,--gc-sections`, 그리고 맵 파일로 무엇이 들어왔는지 확인. 꼬리질문 "`printf("hello\n")` 만 썼는데?" → GCC 는 이를 `puts` 로 바꾸기도 해서 크기가 작게 나오다가 `%d` 하나 넣는 순간 커진다.

</details>

<details>
<summary>Q5. 가변 인자 함수에서 받은 인자를 다른 가변 인자 함수로 넘길 수 있나요?</summary>

`...` 를 그대로 넘기는 문법은 없다. 그래서 `va_list` 를 받는 v판(`vprintf`, `log_vprintf`)을 만들고, `...` 판은 `va_start` → v판 → `va_end` 만 한다. 같은 인자를 두 번 써야 하면(크기 재기 + 쓰기, libc 대조 테스트) `va_copy` — 한 번 v판에 넘긴 `ap` 는 호출자 쪽에서 불확정이다 (7.16p3). 대안: `va_list *` 포인터를 넘기면 원래 함수가 이어서 쓸 수 있다 (7.16p3 각주). 매크로 쪽에선 `__VA_ARGS__` 로 넘길 수 있지만 그건 전처리 단계 이야기다 (C-B15 참고).

</details>

<details>
<summary>Q6. va_arg(ap, char) 는 왜 안 되나요? float 은요?</summary>

`...` 자리에서는 기본 인자 승격이 일어나 `char/short` 는 `int` 로, `float` 은 `double` 로 넘어간다 (6.5.2.2p6–7). `va_arg` 의 타입은 실제로 넘어온 타입과 호환되어야 하므로(7.16.1.1p2) `va_arg(ap, int)`, `va_arg(ap, double)` 로 꺼낸다. clang 은 `-Wvarargs` 로 경고한다 (demo 출력 확인). 같은 이유로 `va_start` 의 마지막 이름 있는 매개변수가 `char`/`float` 이면 UB (7.16.1.4p4). 꼬리질문 "printf 에 `%f` 로 float 을 넘기면?" → double 로 승격되어 넘어가므로 `%f` 하나로 float/double 둘 다 된다. 그래서 printf 엔 `%lf` 가 사실상 `%f` 와 같다.

</details>

<details>
<summary>Q7. __attribute__((format(printf, 2, 3))) 는 무엇을 해 주나요?</summary>

GCC/clang 에게 "이 함수는 printf 처럼 동작한다, 2번째 인자가 형식 문자열이고 3번째부터 대응 인자" 라고 알려 -Wformat 검사를 내 함수 호출부에 적용한다. `uint64_t` 에 `%u`, 포인터에 `%d`, 인자 개수 불일치 등이 컴파일 타임에 잡힌다 (demo 실측 6개 경고). va_list 판은 세 번째 숫자를 0 으로. 로깅 API 는 수천 곳에서 불리고 에러 경로처럼 잘 안 도는 코드에 많으므로 효과가 크다. 64비트는 `<inttypes.h>` 의 `PRIu64` 로 이식성 있게 찍는다. 한계: archetype 이 printf 라서 내 포매터가 지원하지 않는 `%f` 는 못 잡는다.

</details>

<details>
<summary>Q8. 로그 함수 안에서 인터럽트를 막는 구간은 어디까지가 좋나요?</summary>

공유 자원(링 버퍼 인덱스·내용) 갱신만. 포맷은 스택 버퍼로 마스크 밖에서 하고, 완성된 줄을 넣는 동안만 막는다 — 그래야 인터럽트 지연이 "줄 길이만큼의 복사" 로 제한되고, 줄 하나가 원자적 단위가 되어 섞이지 않는다 (Part C 테스트). 막는 방식은 PRIMASK 저장·복원(`irq_save/irq_restore`)이어야 중첩 호출에서 안전하다. 멀티코어나 더 짧게 하고 싶으면 원자적 head 예약(`atomic_fetch_add` 로 자리를 먼저 잡고 복사) 방식의 lock-free 링도 있다 (C-M14 참고).

</details>

## 9. 추가 연습

### 연습 1 — 고정소수점 출력: `-0.05` 를 정확히

온도를 0.01°C 단위 정수(`centi`)로 들고 있다. float 없이 `"12.34"`, `"-0.05"`, `INT_MIN` 이면 `"-21474836.48"` 을 찍는 `fmt_centi(buf, n, centi)` 를 작성하라. `"%d.%02d", v / 100, abs(v % 100)` 는 무엇을 잃나?

<details>
<summary>답 보기</summary>

`v = -5` 이면 `v / 100 == 0` 이라 부호가 사라져 `"0.05"` (practice.c 가 이 버그도 확인한다). 부호를 먼저 결정하고 **크기를 unsigned 로** 구한 뒤 `"%s%u.%02u", neg ? "-" : "", m / 100, m % 100`. Part A 의 INT_MIN 기법이 그대로 쓰인다. MCU 로그에서 `%f` 를 빼고도 소수를 찍는 표준 방법이다.

</details>

### 연습 2 — xasprintf: 두 번 포맷하려면 va_copy

필요한 만큼 `malloc` 해서 포맷 결과를 돌려주는 `char *xasprintf(const char *fmt, ...)` 를 `vsnprintf` 두 번으로 작성하라.

<details>
<summary>답 보기</summary>

`va_start(ap)` → `va_copy(ap2, ap)` → `n = vsnprintf(NULL, 0, fmt, ap)` (크기 재기) → `malloc(n + 1)` → `vsnprintf(s, n + 1, fmt, ap2)` → `va_end` 둘 다. `ap` 를 두 번째 호출에 재사용하면 7.16p3 위반. 펌웨어에선 힙 대신 고정 풀이나 스택을 쓰지만, "size 0 으로 크기 재기 + va_copy" 패턴은 그대로다. format 속성 `(1, 2)` 도 잊지 말 것.

</details>

### 연습 3 — 안전한 appendf 와 hexdump 한 줄

`appendf(buf, size, &off, fmt, ...)` 로 `"00000010: 48 65 6c 00 ff  |Hel..|"` 같은 hexdump 줄을 조립하라. 버퍼가 작으면(16바이트) 넘치지 않고 잘림을 보고해야 한다.

<details>
<summary>답 보기</summary>

순진한 `off += snprintf(buf + off, size - off, ...)` 는 잘린 뒤 `off` 가 `size` 를 넘는다 (16바이트 버퍼면 `9 + 3 + 3 + 3 = 18`). 다음 호출의 `size - off` 는 size_t 로 `2⁶⁴ − 2` 가 되어 **버퍼 크기 제한이 사라진다** → 오버플로. 고침: `n >= size - off` 이면 `off = size` 로 고정하고 −1, 이후 호출은 `off >= size` 를 보고 바로 −1. 테스트는 size 밖 센티널(0x5A)이 그대로인지 확인한다.

</details>

### 연습 4 — 나눗셈 없는 10진 변환 (Cortex-M0 용)

`/` 와 `%` 없이 `unsigned` 를 10진 문자열로 바꾸는 `utoa10_nodiv(v, buf)` 를 작성하라. libc `"%u"` 와 5000개 퍼즈로 대조한다.

<details>
<summary>답 보기</summary>

10⁹, 10⁸, …, 1 을 큰 것부터 빼며 각 자리의 횟수(0..9)를 센다. 자리당 최대 9번 뺄셈 → 최악 90번 비교·뺄셈으로 `__aeabi_uidiv` 호출 10번(각각 수십 사이클)을 대신한다. 앞자리 0 은 건너뛰되 마지막 자리는 항상 쓴다 (`v == 0` → `"0"`). 10⁹ 가 `unsigned` 에 들어가는 최대 10의 거듭제곱이라 표는 10칸.

</details>

### 연습 5 — 컴파일러가 잡는 것 / 못 잡는 것

`demo_format_check.c` 의 `use()` 안 호출 7개 중 `-Wall` 이 경고하는 것은 몇 개이고, 못 잡는 것은 무엇인가? `log_wrapper` 는 어떤 옵션에서 경고가 나는가?

<details>
<summary>답 보기</summary>

`log_checked` 호출 6개 모두 경고 (`%u`↔`uint64_t`, `%d`↔포인터, `%d`↔`double`, 인자 부족 2개, 인자 남음). `log_unchecked` 는 같은 세 가지 실수가 한 줄에 있어도 **0개** — 속성이 없으면 컴파일러는 그냥 가변 인자로 넘긴다. 같은 파일의 `va_arg(ap, char)` 는 `-Wvarargs` 로 경고. `log_wrapper` 는 속성이 없는 함수가 받은 `fmt` 를 v판에 넘기므로 `-Wformat-nonliteral` (또는 `-Wformat=2`) 에서 "format string is not a string literal" — 래퍼에 `format(printf, 1, 2)` 를 달면 사라지고 호출부 검사가 한 단계 위로 전파된다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 17/17 passed
```

## 10. 핵심 정리

- itoa 의 정답은 **크기를 unsigned 로**: `m = (unsigned)v; if (v < 0) m = 0u - m;` — `-v`, `abs(v)` 는 INT_MIN 에서 UB 이고 `-O2` 에선 우연히 맞아 숨는다 (UBSan 으로 잡기).
- 포매터 = 플래그 → 폭 → 변환 → 패딩 `[공백][부호][0들][본문][공백]`. `'-'` 가 `'0'` 을 이기고, 폭은 자르지 않는다.
- snprintf 규칙: size>0 이면 항상 NUL, 반환 = 썼을 길이(잘림 감지), size==0 은 아무것도 안 씀 (NULL 허용, 2-pass 크기 재기).
- `...` 는 기본 인자 승격(`char`→`int`, `float`→`double`). 넘길 땐 v판 + `va_list`, 두 번 쓰면 `va_copy`, 짝마다 `va_end`.
- 출력 대상은 putc 콜백 하나로 통일 → 버퍼·UART·링 버퍼 모두 같은 코어.
- ISR 안전 로거 = 상태 없는 포매터 + 스택 줄 버퍼 + 짧은 임계 구역(PRIMASK 저장·복원)으로 링에 한 줄씩 + 꽉 차면 버리고 센다. 절대 기다리지 않는다.
- newlib printf 는 MCU 에서 크고(float·stdio·로캘) 재진입 불가. 이번 포매터는 M4 `-Os` 실측 850B.
- 모든 printf 류 API 에 `__attribute__((format(printf, m, n)))` — 로그 인자 실수를 컴파일 타임에.

## 참고자료

- C11 (N1570) 7.21.6.1 fprintf 변환·플래그(p6 `0`/`-`, p8–9), 7.21.6.5 snprintf, 7.21.6.12 vsnprintf, 7.16 `<stdarg.h>` (p3 va_list 전달, 7.16.1.1 va_arg, 7.16.1.2 va_copy, 7.16.1.4 va_start), 6.5.2.2p6–7 기본 인자 승격, 6.3.1.3p2 unsigned 변환, 6.5p5 오버플로, 7.22.6.1 abs — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- [GCC — Common Function Attributes: `format`](https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html)
- [Clang — Attributes in Clang (`format`)](https://clang.llvm.org/docs/AttributeReference.html#format)
- [ARM — Procedure Call Standard for the Arm Architecture (AAPCS32), va_list 정의](https://github.com/ARM-software/abi-aa/releases)
- [Microsoft Learn — `_itoa`, `_itow` functions (base 10 만 부호 처리)](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/itoa-itow)
- [newlib — nano formatted I/O (`--specs=nano.specs`, `_printf_float`)](https://sourceware.org/newlib/)
- 작은 printf 구현들: [mpaland/printf](https://github.com/mpaland/printf), [eyalroz/printf](https://github.com/eyalroz/printf), [nanoprintf](https://github.com/charlesnicholson/nanoprintf), [Zephyr cbprintf / logging](https://docs.zephyrproject.org/latest/services/logging/index.html)
- 관련 레슨: C-B02 (정수 승격), C-B07 (strlcpy 반환값·잘림), C-B15 (`__VA_ARGS__`), C-M06 (콜백 + context), C-M08 (memcpy idiom 인식·freestanding), C-M09 (strtol — 역방향 변환), C-M14 (ISR 공유 데이터·임계 구역)
