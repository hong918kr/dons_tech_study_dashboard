---
id: C-A04
time: 90분
updated: 2026-11-08
---
# C-A04 재진입성과 스레드 안전: strtok vs strtok_r

## 학습 목표

- `strtok` 의 정확한 의미(C11 7.24.5.8)를 경계 사례까지 구현하고, 숨은 `static` 상태 하나가 **중첩 호출·다른 스레드·ISR** 세 경우에서 각각 어떻게 깨지는지 결정적 테스트로 보일 수 있다.
- **재진입(reentrant)**, **스레드 안전(thread-safe)**, **ISR / async-signal 안전(async-signal-safe)** 을 서로 다른 세 성질로 구분하고, 한쪽만 만족하는 예(`_Thread_local` 상태, 뮤텍스로 보호한 `malloc`)를 들 수 있다.
- 상태를 **호출자에게 넘기는 API 설계**(`strtok_r`, `itoa_r`, 컨텍스트 구조체)가 왜 펌웨어 라이브러리의 기본형인지 설명하고, 4단 중첩 CLI 파서를 그 방식으로 짤 수 있다.
- `printf`, `malloc`, 정적 버퍼 `itoa`, `rand`, `strerror`, RTOS 뮤텍스, `xQueueSendFromISR`, `HAL_Delay` 등을 ISR 에서 불러도 되는지 **근거(표준 조항, POSIX 목록, 구현 구조)와 함께** 분류할 수 있다.
- 재진입성을 **시험하는 방법**(모든 지점 ISR 주입, TSan, `nm` 으로 쓰기 가능한 정적 객체 감사)을 말할 수 있다.

## 1. 문제

"이 함수 ISR 에서 불러도 되나요?" 는 시니어 펌웨어 면접에서 코드 리뷰 질문의 절반을 차지한다. 그리고 그 대표 예가 `strtok` 다 — 문법도 쉽고 답도 짧은데, **"왜 안 되는지" 를 세 가지 축으로 정확히 말하는 사람이 드물다.**

`strtok` 은 "다음에 어디서부터 자를지" 를 함수 안의 `static char *` 하나에 기억한다. glibc 는 `static char *olds;`, macOS/FreeBSD libc 는 `static char *last;` 를 두고 `strtok_r(s, delim, &last)` 를 부르는 한 줄짜리다. 이 숨은 상태를 **모든 호출자가 공유** 한다. 그러니 바깥 루프가 `';'` 로 그룹을 자르는 도중 안쪽 루프가 같은 `strtok` 으로 `','` 를 자르면, 안쪽이 바깥의 "다음 위치" 를 덮어써 버린다:

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">중첩 strtok: 안쪽 루프가 static last 를 덮어 바깥 루프가 "c=3,d=4" 를 잃는다</text>
  <g font-family="monospace" fill="currentColor">
    <rect x="40" y="40" width="40" height="30" class="f-acc" stroke="currentColor"/><text x="55" y="60">a</text>
    <rect x="80" y="40" width="40" height="30" class="f-acc" stroke="currentColor"/><text x="95" y="60">=</text>
    <rect x="120" y="40" width="40" height="30" class="f-acc" stroke="currentColor"/><text x="135" y="60">1</text>
    <rect x="160" y="40" width="40" height="30" class="f-mut" stroke="currentColor"/><text x="166" y="60">\0</text>
    <rect x="200" y="40" width="40" height="30" class="f-acc" stroke="currentColor"/><text x="215" y="60">b</text>
    <rect x="240" y="40" width="40" height="30" class="f-acc" stroke="currentColor"/><text x="255" y="60">=</text>
    <rect x="280" y="40" width="40" height="30" class="f-acc" stroke="currentColor"/><text x="295" y="60">2</text>
    <rect x="320" y="40" width="40" height="30" class="f-warn" stroke="currentColor"/><text x="326" y="60">\0</text>
    <rect x="360" y="40" width="40" height="30" class="f-ok" stroke="currentColor"/><text x="375" y="60">c</text>
    <rect x="400" y="40" width="40" height="30" class="f-ok" stroke="currentColor"/><text x="415" y="60">=</text>
    <rect x="440" y="40" width="40" height="30" class="f-ok" stroke="currentColor"/><text x="455" y="60">3</text>
    <rect x="480" y="40" width="40" height="30" class="f-ok" stroke="currentColor"/><text x="495" y="60">,</text>
    <rect x="520" y="40" width="40" height="30" class="f-ok" stroke="currentColor"/><text x="535" y="60">d</text>
    <rect x="560" y="40" width="40" height="30" class="f-ok" stroke="currentColor"/><text x="575" y="60">=</text>
    <rect x="600" y="40" width="40" height="30" class="f-ok" stroke="currentColor"/><text x="615" y="60">4</text>
    <rect x="640" y="40" width="40" height="30" class="f-mut" stroke="currentColor"/><text x="646" y="60">\0</text>
    <text x="55" y="88" font-size="12">0</text><text x="335" y="88" font-size="12">7</text><text x="375" y="88" font-size="12">8</text><text x="650" y="88" font-size="12">15</text>
  </g>
  <path d="M380 126 L380 94" stroke="currentColor" fill="none"/>
  <text x="372" y="140" fill="currentColor" font-size="12">① 후 last</text>
  <path d="M340 126 L340 94" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="280" y="140" fill="currentColor" font-size="12">② 후 last</text>
  <text x="40" y="172" fill="currentColor">① 바깥 strtok(s, ";") → 토큰 "a=1,b=2", 7번 ';' 를 \0 으로, last = 8 (다음 그룹)</text>
  <text x="40" y="196" fill="currentColor">② 안쪽 strtok(g, ",") 가 "a=1", "b=2" 를 자르며 같은 last 를 씀 → 그룹 끝 last = 7</text>
  <text x="40" y="220" fill="currentColor">③ 바깥 strtok(NULL, ";") 는 7번의 '\0' 에서 시작 → NULL. 결과 "[a=1|b=2]" 로 끝.</text>
  <rect x="30" y="256" width="690" height="34" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="42" y="278" fill="currentColor">strtok_r: 바깥은 &amp;sg, 안쪽은 &amp;si — 상태가 둘이라 서로 못 덮는다 → "[a=1|b=2][c=3|d=4]"</text>
</svg>
```

같은 "덮어쓰기" 가 다른 스레드에서 오면 **데이터 레이스**, 같은 스레드 안에서 ISR 이 끼어들면 **재진입 실패** 다. 이 레슨은 세 경우를 모두 결정적으로 재현하고, 세 성질을 구분한다.

**ISR 모델.** 단일 코어에서 ISR 은 "main 의 두 명령 사이에 끼워 넣어진, 끝까지 실행되는 함수 호출" 이다. 그래서 체크 테스트는 시그널 대신 `preempt_tick()` 훅으로 **n번째 토큰 호출 직후에 ISR 함수를 직접 부른다** (C-M14 의 `rd32()` 주입과 같은 발상이지만 `raise` 도 쓰지 않는다 — 이유는 5. 풀이 Part B). 진짜 비동기 시그널 실험은 `demo_malloc_signal.c` 에 있다.

### Part A — 구현: `my_strtok_r` 과 `my_strtok`

- `char *my_strtok_r(char *s, const char *delim, char **save)` — POSIX `strtok_r` 과 같은 의미. 상태는 `*save` 에만. `s != NULL` 이면 `*save` 의 이전 값은 무시. 라이브러리 함수 없이(주어진 `is_delim` 만).
- `char *my_strtok(char *s, const char *delim)` — `strtok` 과 같은 의미, 상태는 함수 안 `static`.
- 테스트: C11 7.24.5.8 의 EXAMPLE, 빈 문자열/구분자뿐/빈 구분자 집합, 끝난 뒤 재호출, libc `strtok_r` 과 **5000 케이스 결정적 퍼즈**(매 호출 구분자 집합도 바뀜), 2단 중첩.

### Part B — 세 축: 중첩 · 스레드 · ISR

- `my_strtok_tls` — 상태를 `static _Thread_local` 로. 4 스레드 동시 실행은 통과해야 하고, **같은 스레드 안의 중첩과 ISR 에서는 static 판과 똑같이 깨져야 한다** (테스트가 깨진 결과를 정확히 확인).
- ISR 이 main 의 2번째 토큰 직후 자기 버퍼 `"x y z"` 를 자른다: `_r` 판은 main 이 `m0..m3` 을 다 받고, static/TLS 판은 `m2, m3` 을 잃는다.
- `itoa_static` (주어짐, 정적 버퍼) vs `char *itoa_r(int v, char *buf, size_t n)` — 자리가 모자라면 NULL. INT_MIN 포함.

### Part C — 펌웨어 통합: UART CLI 파서와 ISR 안전성 분류

- `int cli_exec(char *line, char *out, size_t n)` — `';'` 명령 / `' '`·`'\t'` 단어 / `','` 항목 / `'='` 조각의 **4단 중첩** 을 `tok_r()` 로. `"led 1 on; pwm ch=2,duty=50 ;log"` → `"led(1|on) pwm(ch:2|duty:50) log()"`, 반환 3.
- 텔레메트리 ISR(같은 `my_strtok_r`/`itoa_r` 를 **자기 상태로** 사용)을 **모든 토큰 경계(39곳)에 하나씩** 주입해도 CLI 결과와 ISR 결과가 모두 정확해야 한다.
- 종이 문제 — 아래 함수를 **재진입 / 스레드 안전 / ISR 안전** 으로 분류하라 (답은 5. 풀이 Part C 의 표). 15분 안에 근거까지:

| # | 함수 | # | 함수 |
|---|---|---|---|
| 1 | `printf` | 9 | `rand` |
| 2 | libc `snprintf` (지역 버퍼에) | 10 | `strerror` |
| 3 | `malloc` / `free` | 11 | `my_strtok_tls` |
| 4 | `itoa_static` (정적 버퍼) | 12 | `atomic_fetch_add` (lock-free 타입) |
| 5 | `itoa_r` / C-M10 `mini_snprintf` | 13 | `pthread_mutex_lock` / RTOS 뮤텍스 take |
| 6 | `strtok` | 14 | FreeRTOS `xQueueSendFromISR` |
| 7 | `strtok_r` | 15 | STM32 HAL `HAL_Delay` |
| 8 | `memcpy`, `strlen` | 16 | `strtol` (errno 를 씀) |

## 2. Problem (English)

> **Reentrancy vs thread safety: strtok vs strtok_r.**
>
> 1. Implement `char *my_strtok_r(char *s, const char *delim, char **save)` with exactly POSIX `strtok_r` semantics (skip leading delimiters, overwrite the terminating delimiter with NUL, save the following position; return NULL when no token remains and keep returning NULL; the delimiter set may change between calls; ignore `*save` when `s != NULL`). Then `char *my_strtok(char *s, const char *delim)` with a function-scope `static` state. Show with a nested tokenizer (`';'` groups, `','` items) that `my_strtok` loses the second group while `my_strtok_r` does not.
> 2. Implement `my_strtok_tls` (state is `static _Thread_local`). Prove: it is correct under 4 concurrent threads, but still breaks under nesting and when an ISR on the same thread tokenizes. Implement `char *itoa_r(int v, char *buf, size_t n)` (NULL if it does not fit) and contrast with a static-buffer `itoa`.
> 3. Implement a UART CLI `int cli_exec(char *line, char *out, size_t n)` with four nesting levels (`;`, whitespace, `,`, `=`), e.g. `"led 1 on; pwm ch=2,duty=50 ;log"` → `"led(1|on) pwm(ch:2|duty:50) log()"`, returns 3. Its output must not change when an ISR that uses the same helpers is injected after *any* tokenizer call. Classify a given list of functions (`printf`, `malloc`, static-buffer `itoa`, `rand`, `strerror`, mutex lock, `xQueueSendFromISR`, `HAL_Delay`, ...) as reentrant / thread-safe / ISR-safe, with reasons.
>
> Follow-ups: Is a thread-safe function reentrant? Does wrapping `strtok` in a mutex fix it? What actually happens if a signal handler calls `malloc`? How do you *test* that a function is reentrant?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
# 처음엔 "6/28 passed" (주어진 itoa_static 의 동작 확인 등) — TODO 를 채워 28/28 을 만든다
```

`#define _POSIX_C_SOURCE 200809L` 은 **모든 include 앞** 에 둔다 (`-std=c11` 은 엄격 모드라 `<string.h>` 가 POSIX 함수 `strtok_r` 의 선언을 숨긴다 — 퍼즈가 libc `strtok_r` 과 비교하므로 필요).

관찰용 데모 (하네스와 별개, 각각 실제 출력은 5. 풀이에):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_strtok_race.c -o race && ./race static && ./race tls
cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_strtok_race.c -o race_tsan && ./race_tsan static   # TSan 레이스 보고
cc -std=c11 -g -O1 -fsanitize=address -fno-omit-frame-pointer demo_overrun.c -o ov && ./ov            # NUL 너머 읽기
cc -std=c11 -Wall -Wextra -O0 demo_literal.c -o lit && ./lit                                          # 리터럴에 strtok → 죽음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_malloc_signal.c -o msig && ./msig                          # 핸들러에서 malloc
```

## 4. 힌트

<details>
<summary>힌트 1 — my_strtok_r 은 세 단계: 건너뛰기 → 찾기 → 끊기</summary>

① `s == NULL` 이면 `s = *save`. ② 앞쪽 구분자를 건너뛴다. 끝(`'\0'`)에 닿으면 토큰 없음 — `*save` 를 그 `'\0'` 에 두고 NULL. ③ 토큰 시작을 기억하고 구분자나 끝까지 전진. ④ 구분자에서 멈췄으면 그 자리를 `'\0'` 으로 바꾸고 `*save = 그 다음`. **끝에서 멈췄으면 `*save = 그 '\0'`** — 한 칸 더 가면 다음 호출이 버퍼 밖을 읽는다 (`demo_overrun.c`).

</details>

<details>
<summary>힌트 2 — my_strtok 과 my_strtok_tls 는 한 줄씩</summary>

둘 다 `return my_strtok_r(s, delim, &last);` 다. 차이는 `last` 의 **저장 기간(storage duration)** 뿐 — static(프로그램 전체에 하나) vs thread(스레드마다 하나). 이게 실제 libc 의 구현 방식이기도 하다.

</details>

<details>
<summary>힌트 3 — itoa_r 은 "먼저 길이를 안 다음에 쓴다"</summary>

스택의 `tmp[12]` 에 거꾸로 숫자를 만들고(부호는 `0u - (unsigned)v` 로 INT_MIN 안전, C-M10), 필요한 길이 `자리수 + 부호 + NUL` 이 `n` 보다 크면 아무 것도 쓰지 않고 NULL. 정적 저장소는 하나도 쓰지 않는다.

</details>

<details>
<summary>힌트 4 — cli_exec: 중첩 단계마다 save 포인터 하나</summary>

`char *s_cmd, *s_word, *s_item, *s_kv;`. 첫 단어(이름)는 `tok_r(cmd, " \t", &s_word)`, 나머지 단어는 같은 `&s_word` 로 `tok_r(NULL, ...)`. 단어 안에서 `','` 루프, 항목 안에서 `'='` 루프. 이름이 NULL 이면(공백뿐인 명령) 출력 없이 건너뛴다. 모든 호출을 `tok_r()` 로 해야 ISR 주입 테스트가 의미가 있다.

</details>

## 5. 풀이

### Part A — strtok 의 정확한 의미

C11 7.24.5.8 (p2–p5)을 구현 단계로 옮기면:

| 단계 | 표준 문장 (요지) | 코드 |
|---|---|---|
| 시작 | 첫 호출은 non-null `s1`, 이어지는 호출은 NULL. 구분자 집합은 호출마다 달라도 된다 (p2) | `if (s == NULL) s = *save;` |
| 건너뛰기 | 구분자가 **아닌** 첫 문자를 찾는다. 없으면 토큰 없음 → NULL (p3) | `while (*s && is_delim(*s, delim)) s++;` |
| 찾기 | 거기서부터 구분자를 찾는다. 없으면 토큰은 끝까지, **이후 검색은 NULL** (p4) | `while (*s && !is_delim(...)) s++;` |
| 끊기 | 찾은 구분자를 `'\0'` 으로 덮고, **그 다음 문자** 를 저장 (p4) | `*s = '\0'; *save = s + 1;` |

```c
static char *my_strtok_r(char *s, const char *delim, char **save) {
    if (s == NULL) s = *save;
    if (s == NULL) return NULL;                     /* (방어) 시작 없이 NULL 로 불림 */
    while (*s && is_delim(*s, delim)) s++;
    if (*s == '\0') { *save = s; return NULL; }     /* 이후 호출도 NULL */
    char *tok = s;
    while (*s && !is_delim(*s, delim)) s++;
    if (*s) { *s = '\0'; *save = s + 1; }
    else *save = s;                                 /* 끝: NUL 을 가리킨 채 */
    return tok;
}
```

경계 사례와 테스트:

| 입력 / 상황 | 결과 | 왜 |
|---|---|---|
| `"?a???b,,,#c"`, 구분자 `"?"`, `","`, `"#,"`, `"?"` | `"a"`, `"??b"`, `"c"`, NULL | 표준 EXAMPLE 그대로. 두 번째 호출의 구분자는 `","` 뿐이라 `"??b"` 가 토큰 |
| `""` / `",,,"` | 첫 호출부터 NULL, 재호출도 NULL | p3 "no tokens" |
| 구분자 집합 `""` | 문자열 전체가 토큰 하나 | 어떤 문자도 구분자가 아님 |
| `"a,b,"` | `"a"`, `"b"`, NULL, 버퍼 `a\0b\0\0` | 끝 구분자도 `'\0'` 로 바뀐다 → **입력이 파괴된다** |
| `s != NULL`, `*save` 는 쓰레기 | 무시 | POSIX: "need not initialize the pointer pointed to by state" |
| 마지막 토큰이 문자열 끝까지 | `*save` = 끝의 `'\0'` | `s + 1` 이면 다음 호출이 버퍼 밖을 읽는다 ↓ |

마지막 줄이 실제로 흔한 구현 버그다. `demo_overrun.c` 는 `*save = s + 1` 을 무조건 하는 판이다. `-O2` 로는 멀쩡히 `token 'ON'`, `token 'OFF'` 를 찍고 끝나지만(우연히 다음 바이트가 0), ASan 은 정확히 잡는다 (실제 출력 발췌):

```text
==81530==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x6020000009b7 ...
READ of size 1 at 0x6020000009b7 thread T0
    #0 0x000100ba49a8 in main demo_overrun.c:26
0x6020000009b7 is located 0 bytes after 7-byte region [0x6020000009b0,0x6020000009b7)
```

7바이트 `"ON,OFF\0"` 바로 다음 바이트를 읽었다. 펌웨어의 RX 버퍼라면 그 다음 바이트는 **다른 변수** 라서 0 이 아닐 수 있고, 그러면 "OFF" 뒤에 쓰레기 토큰이 하나 더 나온다.

**퍼즈.** `fuzz_vs_libc(5000)` 은 xorshift32(고정 시드 `0xC0A04`)로 `"ab,; \t"` 문자 0–20개짜리 문자열을 만들고, 매 호출 `{",", ", ", ";, \t", " ", "", "x", "ab"}` 중 하나를 골라 `my_strtok_r` 과 libc `strtok_r` 을 **같은 사본** 에 돌린다. 비교 대상은 반환 포인터의 오프셋과 **버퍼 전체 내용** (어디에 `'\0'` 을 썼는지까지). 5000 케이스 불일치 0.

**중첩.** `nested_r` 은 단계마다 save 포인터를 하나씩 둔다 (`&sg`, `&si`). `nested_static(my_strtok, ...)` 은 그림의 ①②③ 그대로 `"[a=1|b=2]"` 를 낸다 — 테스트는 이 **틀린 결과** 를 정확히 확인한다. 바깥 루프가 끝난 것처럼 보이므로 에러도 없이 데이터만 조용히 사라진다. 실무에서 가장 찾기 어려운 형태다.

> 왜 `strtok` 는 이렇게 설계됐나? 1970년대 단일 스레드 유닉스에서 "호출자를 편하게" 한 API다. C11 은 이제 7.24.5.8p6 에 *"The strtok function is not required to avoid data races with other calls to the strtok function."* 이라고 못박고, 각주로 `strtok_s`(Annex K)를 권한다. 7.1.4p4 는 더 일반적으로 *"The functions in the standard library are not guaranteed to be reentrant and may modify objects with static or thread storage duration."* — 각주 188: 그래서 시그널 핸들러는 일반적으로 표준 라이브러리 함수를 부를 수 없다.

### Part B — 세 가지 성질은 서로 다르다

| 성질 | 질문 | 정의 출처 |
|---|---|---|
| **재진입 (reentrant)** | 이 함수가 끝나기 **전에** 같은 문맥(같은 스레드)에서 다시 불려도 — 재귀, 콜백, 중첩 루프, 그 스레드를 선점한 ISR/시그널 — 두 호출이 모두 맞는가? | 전통적 용어. POSIX.1-2017 의 정의 장(XBD 3)에는 별도 항목이 없다 |
| **스레드 안전 (thread-safe)** | 여러 스레드가 **동시에** 불러도 맞는가? | POSIX XBD 3.407: "can be safely invoked concurrently ... by multiple threads". 예로 "pure 함수" 와 "정적 저장소 접근 중 뮤텍스를 쥐는 함수" 를 든다 |
| **async-signal-safe / ISR-safe** | **아무 지점에서나** 끊긴 문맥 위에서(그 함수 자신이나 다른 함수의 한가운데서) 핸들러/ISR 이 불러도 되는가? | POSIX XBD 3.27 + XSH 2.4.3 의 함수 목록. C11 7.14.1.1p5 |

핵심은 **"동시에(다른 스레드)" 와 "끼어들어서(같은 스레드)"** 가 다르다는 점이다. 스레드는 서로를 기다릴 수 있다(뮤텍스). 같은 스레드 위에서 끼어든 ISR 은 자기가 선점한 코드를 **기다릴 수 없다** — 그 코드는 ISR 이 끝나야 다시 돈다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">상태를 어디에 두느냐 → 세 성질이 갈린다</text>
  <g fill="currentColor">
    <text x="300" y="50">중첩 (같은 문맥)</text>
    <text x="440" y="50">다른 스레드 동시</text>
    <text x="590" y="50">ISR / 시그널</text>
    <text x="10" y="82">static 상태</text>
    <text x="10" y="98" font-size="12" font-family="monospace">strtok, itoa_static, rand</text>
    <text x="10" y="132">_Thread_local 상태</text>
    <text x="10" y="148" font-size="12" font-family="monospace">my_strtok_tls, errno</text>
    <text x="10" y="182">뮤텍스로 보호한 공유 상태</text>
    <text x="10" y="198" font-size="12" font-family="monospace">malloc, printf</text>
    <text x="10" y="232">인터럽트 마스킹 임계 구역</text>
    <text x="10" y="248" font-size="12" font-family="monospace">xQueueSendFromISR</text>
    <text x="10" y="282">상태를 호출자가 소유</text>
    <text x="10" y="298" font-size="12" font-family="monospace">strtok_r, itoa_r, ctx 구조체</text>
  </g>
  <rect x="290" y="66" width="130" height="36" class="f-warn" stroke="currentColor"/><text x="300" y="89" fill="currentColor">✗ 덮어씀</text>
  <rect x="430" y="66" width="140" height="36" class="f-warn" stroke="currentColor"/><text x="440" y="89" fill="currentColor">✗ 데이터 레이스</text>
  <rect x="580" y="66" width="150" height="36" class="f-warn" stroke="currentColor"/><text x="590" y="89" fill="currentColor">✗ 덮어씀</text>
  <rect x="290" y="116" width="130" height="36" class="f-warn" stroke="currentColor"/><text x="300" y="139" fill="currentColor">✗ 덮어씀</text>
  <rect x="430" y="116" width="140" height="36" class="f-ok" stroke="currentColor"/><text x="440" y="139" fill="currentColor">✓ 스레드마다 하나</text>
  <rect x="580" y="116" width="150" height="36" class="f-warn" stroke="currentColor"/><text x="590" y="139" fill="currentColor">✗ 같은 스레드</text>
  <rect x="290" y="166" width="130" height="36" class="f-warn" stroke="currentColor"/><text x="300" y="189" fill="currentColor">✗ 교착/꼬임</text>
  <rect x="430" y="166" width="140" height="36" class="f-ok" stroke="currentColor"/><text x="440" y="189" fill="currentColor">✓ 기다리면 됨</text>
  <rect x="580" y="166" width="150" height="36" class="f-warn" stroke="currentColor"/><text x="590" y="189" fill="currentColor">✗ 교착/크래시</text>
  <rect x="290" y="216" width="130" height="36" class="f-ok" stroke="currentColor"/><text x="300" y="239" fill="currentColor">✓ 저장/복원</text>
  <rect x="430" y="216" width="140" height="36" class="f-mut" stroke="currentColor"/><text x="440" y="239" fill="currentColor">△ 단일 코어만</text>
  <rect x="580" y="216" width="150" height="36" class="f-ok" stroke="currentColor"/><text x="590" y="239" fill="currentColor">✓ 못 끼어듦</text>
  <rect x="290" y="266" width="130" height="36" class="f-ok" stroke="currentColor"/><text x="300" y="289" fill="currentColor">✓</text>
  <rect x="430" y="266" width="140" height="36" class="f-ok" stroke="currentColor"/><text x="440" y="289" fill="currentColor">✓ (상태가 다르면)</text>
  <rect x="580" y="266" width="150" height="36" class="f-ok" stroke="currentColor"/><text x="590" y="289" fill="currentColor">✓</text>
  <text x="10" y="322" fill="currentColor">맨 아래 줄만 세 칸이 모두 ✓ — 펌웨어 공용 라이브러리의 기본형이 "상태를 인자로" 인 이유.</text>
</svg>
```

**`_Thread_local` 은 스레드 안전이지만 재진입이 아니다.** 블록 범위에서는 `static _Thread_local` 처럼 `static`(또는 `extern`)을 반드시 같이 써야 한다(C11 6.7.1p3). 테스트 결과:

| 테스트 | `my_strtok` (static) | `my_strtok_tls` | `my_strtok_r` |
|---|---|---|---|
| 4 스레드 × 3000줄 동시 | (체크 안 함 — 레이스, 아래 데모) | 오류 0 | 오류 0 |
| 2단 중첩 | `"[a=1\|b=2]"` | `"[a=1\|b=2]"` | `"[a=1\|b=2][c=3\|d=4]"` |
| 2번째 토큰 뒤 ISR | main `"m0\|m1\|"` | main `"m0\|m1\|"` | main `"m0\|m1\|m2\|m3\|"` |

ISR 은 main 과 **같은 스레드** 위에서 돈다 (시그널 핸들러도 그 시그널을 받은 스레드 위에서 돈다). 그러니 TLS 는 아무것도 막지 못한다. 이 표의 가운데 열이 실무에서 실제로 존재한다: **newlib** 의 `strtok` 는 저장 포인터를 `struct _reent` 안(`_REENT_STRTOK_LAST`)에 두고, FreeRTOS 의 `configUSE_NEWLIB_REENTRANT` 는 태스크마다 `_reent` 를 하나씩 줘서 컨텍스트 스위치 때 갈아 끼운다. 결과: 태스크끼리는 안전, **ISR 에서는 여전히 불안전**, 같은 태스크 안의 중첩도 불안전.

**스레드 레이스 실측** (`demo_strtok_race.c`: 4 스레드가 각자 자기 줄 `"T<id>a,T<id>b,T<id>c,T<id>d"` 를 200,000번씩, Apple Silicon, Apple clang 21):

```text
static: 4 threads x 200000 lines: wrong lines=30, tokens from another thread's line=1
static: 4 threads x 200000 lines: wrong lines=18, tokens from another thread's line=0
static: 4 threads x 200000 lines: wrong lines=17, tokens from another thread's line=0
tls   : 4 threads x 200000 lines: wrong lines=0, tokens from another thread's line=0
```

80만 줄 중 17–30줄 — 0.004% 수준이라 **테스트를 몇 번 돌려서는 안 보인다**. 한 번은 **다른 스레드의 줄에서 나온 토큰** 까지 받았다 (남의 스택 버퍼를 가리키는 포인터!). 이런 버그는 실행 횟수가 아니라 도구로 잡는다. TSan 은 첫 실행에서 바로 짚는다 (실제 출력 발췌; TSan 빌드에서는 타이밍이 바뀌어 `wrong lines=228` 로 더 많이 깨졌다):

```text
WARNING: ThreadSanitizer: data race (pid=81308)
  Write of size 8 at 0x000100614028 by thread T4:
    #0 tok_static demo_strtok_race.c:21 (C-A04_race_tsan:arm64+0x100000b7c)
    #1 worker demo_strtok_race.c:35 (C-A04_race_tsan:arm64+0x100000c60)

  Previous write of size 8 at 0x000100614028 by thread T1:
    #0 tok_static demo_strtok_race.c:21 (C-A04_race_tsan:arm64+0x100000b7c)
    #1 worker demo_strtok_race.c:35 (C-A04_race_tsan:arm64+0x100000c94)

  Location is global 'tok_static.last' at 0x000100614028 (C-A04_race_tsan+0x100008028)
...
SUMMARY: ThreadSanitizer: data race demo_strtok_race.c:21 in tok_static
ThreadSanitizer: reported 5 warnings
```

`./race_tsan tls` 는 경고 0개. 레슨의 `solution.c` 도 `-fsanitize=thread` 로 28/28, 보고 없음 (스레드는 자기 버퍼와 TLS 만 만진다).

**"뮤텍스를 씌우면 strtok 이 스레드 안전해지나?"** — 각 *호출* 은 안전해지지만 *토큰화 순서* 는 여전히 깨진다. 상태가 호출과 호출 **사이** 에 살아 있기 때문이다: 스레드 A 가 `strtok(a)` → 스레드 B 가 `strtok(b)` → A 의 `strtok(NULL)` 은 B 의 문자열을 이어 자른다. 락을 "토큰화 전체" 에 걸면 맞긴 하지만 그동안 다른 스레드는 전부 대기하고, 중첩·ISR 은 여전히 안 된다. **API 에 숨은 상태가 있다는 것 자체가 결함** 이고, 고치는 길은 상태를 밖으로 꺼내는 것(`_r`)뿐이다.

**왜 체크 테스트에서 `raise()` 대신 훅인가.** C11 7.14.1.1p5 는 *비동기* 시그널 핸들러가 lock-free 원자 객체가 아닌 정적 객체를 참조(단, `volatile sig_atomic_t` 에 대입 제외)하거나 표준 라이브러리 함수(`abort`, `_Exit`, `quick_exit`, `signal` 제외)를 부르면 UB 라고 한다. `raise()` 로 일으킨 시그널은 이 조항 밖이지만, 5.1.2.3p5 는 "시그널 수신으로 처리가 중단되면" 핸들러가 수정한 일반 객체의 값은 핸들러가 끝날 때 **불확정(indeterminate)** 이 된다고 한다. `my_strtok` 의 `last` 를 핸들러가 고치고 main 이 그걸 읽는 테스트는 바로 그 객체를 확인하는 것이라, 시그널로 하면 "표준이 값을 보장하지 않는 것을 체크" 하게 된다. 훅 호출은 평범한 함수 호출이라 모든 값이 정의된다 — 그리고 단일 코어 ISR 의 의미 모델과 정확히 같다. (5.1.2.3p5 자체가 이 레슨의 결론을 표준 언어로 말한 것이기도 하다: "핸들러와 공유하는 일반 정적 객체는 믿을 수 없다".)

**itoa: 정적 버퍼의 두 얼굴.**

| 상황 | `itoa_static` | `itoa_r` |
|---|---|---|
| `a = itoa(1); b = itoa(2);` | `a == b`, 둘 다 `"2"` | 버퍼 둘 → `"1"`, `"2"` |
| `printf("%s %s", itoa(1), itoa(2))` | 같은 포인터 두 개 → `"1 1"` 또는 `"2 2"` (인자 평가 순서는 **미지정**, C-B14) | 정상 |
| main 이 결과를 쓰기 전 ISR 이 `itoa(-45)` | main 의 `"123"` 이 `"-45"` 로 바뀜 (테스트로 확인) | 무관 |
| 자리 부족 | (버퍼가 고정 12 라 없음) | NULL, `buf[0] = '\0'` — `INT_MAX` 는 `n = 11` 이면 되고 10 이면 NULL |

`inet_ntoa`, `asctime`, `ctime`, `localtime`, `strerror`(구현에 따라) 가 모두 이 "정적 결과" 패턴이다. POSIX 는 대부분에 `_r` 판(`localtime_r`, `strerror_r`, `asctime_r`)을 두고, 네트워크 쪽은 `inet_ntop(af, src, dst, size)` 로 호출자 버퍼를 받게 바꿨다.

**진짜 비동기 시그널에서 malloc — `demo_malloc_signal.c`.** 자식 프로세스에서 main 이 `malloc/free` 를 반복하고, 100 µs 주기 `SIGALRM` 핸들러도 `malloc(4000)/free` 를 한다 (부모가 3초 감시). 실제 출력 (3회, 크기별):

```text
    (child: handler ran 374 times)
run 0 main malloc(    32): exited 0 after 40 ms
    (child: handler ran 655 times)
run 0 main malloc(  4000): exited 0 after 60 ms
run 0 main malloc(200000): died by signal 9 after 10 ms
    (child: handler ran 402 times)
run 1 main malloc(    32): exited 0 after 40 ms
    (child: handler ran 648 times)
run 1 main malloc(  4000): exited 0 after 60 ms
    (child: handler ran 3896 times)
run 1 main malloc(200000): exited 0 after 330 ms
    (child: handler ran 387 times)
run 2 main malloc(    32): exited 0 after 40 ms
    (child: handler ran 654 times)
run 2 main malloc(  4000): exited 0 after 60 ms
run 2 main malloc(200000): died by signal 9 after 10 ms
```

대부분은 **멀쩡히 끝났다** — 이것이 이 버그가 위험한 이유다. 큰 할당 경로에서는 3번 중 2번 죽었고, macOS 크래시 리포트(`~/Library/Logs/DiagnosticReports/C-A04_msig-*.ips`)의 실제 내용:

```text
"type":"EXC_BREAKPOINT","signal":"SIGKILL"
"asi" : {"libsystem_platform.dylib":["BUG IN CLIENT OF LIBPLATFORM: Trying to recursively lock an os_unfair_lock", ...
크래시 스레드 (위가 최근):
  libsystem_platform.dylib _os_unfair_lock_recursive_abort
  libsystem_platform.dylib _os_unfair_lock_lock_slow
  libsystem_malloc.dylib   _xzm_segment_group_span_mark_smaller      ← 핸들러의 malloc 이 락을 잡으려 함
  ...
  C-A04_msig               on_alarm
  libsystem_platform.dylib _sigtramp                                 ← 여기서 시그널이 끼어듦
  libsystem_kernel.dylib   mach_vm_reclaim_try_cancel
  libsystem_malloc.dylib   _xzm_reclaim_mark_used_locked             ← main 의 malloc 이 락을 쥔 채
  libsystem_malloc.dylib   _xzm_segment_group_span_mark_smaller
```

main 의 `malloc` 이 힙 락을 쥔 상태에서 시그널 → 핸들러의 `malloc` 이 **같은 스레드에서 같은 락** 을 다시 잡으려 함. macOS 의 `os_unfair_lock` 은 이걸 감지해 프로세스를 즉시 죽였다. 감지 기능이 없는 일반 뮤텍스였다면 자기 자신을 영원히 기다리는 **교착(hang)** 이 된다. 이것이 그림의 "뮤텍스 보호 = 스레드 안전 ✓ / ISR ✗" 칸의 실물이다. 펌웨어에서는 더 나쁠 수 있다: newlib 의 `__malloc_lock` 훅은 RTOS 없이 빌드하면 아무것도 안 하는 스텁인 경우가 많아, 크래시도 교착도 없이 **힙 자료구조가 조용히 깨진다**.

### Part C — 4단 중첩 CLI 와 "모든 지점 주입" 테스트

```c
char *s_cmd, *s_word, *s_item, *s_kv;               /* 단계마다 save 하나 */
for (cmd = tok_r(line, ";", &s_cmd); cmd; cmd = tok_r(NULL, ";", &s_cmd)) {
    name = tok_r(cmd, " \t", &s_word);              /* 첫 단어 */
    ...
    for (word = tok_r(NULL, " \t", &s_word); word; ...)       /* 나머지 단어 */
        for (item = tok_r(word, ",", &s_item); item; ...)     /* 항목 */
            for (kv = tok_r(item, "=", &s_kv); kv; ...)       /* k=v 조각 */
```

`strtok` 로는 이 함수를 **쓸 수 없다** — 두 번째 단계부터 바깥 상태가 사라진다. 출력 버퍼는 `cat()` 이 잘림을 허용하며 이어 붙이므로 오버런이 없다.

| 입력 | 출력 | 반환 |
|---|---|---|
| `"led 1 on; pwm ch=2,duty=50 ;log"` | `"led(1\|on) pwm(ch:2\|duty:50) log()"` | 3 |
| `" ;; \t;"` | `""` | 0 |
| `"cfg\tbaud=115200,parity=N mode=rs485"` | `"cfg(baud:115200\|parity:N\|mode:rs485)"` | 1 |
| `"set a==1,,b"` | `"set(a:1\|b)"` | 1 |

마지막 줄은 **설계 경고** 다. strtok 계열은 연속 구분자를 하나로 합치므로 `"a==1"` 과 `"a=1"`, `",,"` 과 `","` 를 구분하지 못한다. CLI 에서는 대개 편리하지만, 빈 필드가 의미를 갖는 프로토콜(NMEA 0183 `"$GPGGA,123519,,,..."`, CSV)에서는 **필드 위치가 밀리는 버그** 가 된다 → 추가 연습 1 (`strsep`).

**모든 지점 주입.** 테스트는 먼저 ISR 없이 `cli_exec` 를 돌려 토큰 호출 수(이 명령줄에서 39회)를 센 뒤, `at = 1..39` 각각에 대해 "at 번째 호출 직후 `telemetry_isr()`" 를 주입해 다시 돌린다. ISR 은 자기 설정 문자열 `"id=7,rate=10"` 을 `my_strtok_r` 로 자르고 `itoa_r(-273)` 을 붙인다. 39번 모두 CLI 출력 `"led(1|on) pwm(ch:2|duty:50) log() cfg(baud:9600)"` 과 ISR 출력 `"id/7/rate/10/-273"` 이 정확해야 통과다. 이것이 재진입성의 **실용적인 정의 그대로의 테스트** 다: "어느 지점에서 끊고 같은 함수를 다시 불러도 둘 다 맞는다".

**정적 객체 감사 (`nm`).** 재진입을 깨는 것은 결국 **쓰기 가능한 정적 저장소** 다. 그러니 오브젝트 파일에서 그런 심볼을 나열하면 의심 목록이 나온다. `cc -std=c11 -O2 -c solution.c -o sol.o && nm -m sol.o | grep -E '__DATA,__(bss|data|thread)'` 의 실제 출력 중 테스트 하네스(`g_*`)를 뺀 것:

```text
0000000000003e1c (__DATA,__bss) non-external _itoa_static.buf
0000000000003df8 (__DATA,__bss) non-external _my_strtok.last
0000000000003c0c (__DATA,__thread_vars) non-external _my_strtok_tls.last
0000000000003e40 (__DATA,__thread_bss) non-external _my_strtok_tls.last$tlv$init
```

함수 안 static 은 `함수명.변수명` 으로 드러난다. `my_strtok_r`, `itoa_r`, `cli_exec` 에 딸린 심볼은 하나도 없다. ELF / arm-none-eabi 에서는 `arm-none-eabi-nm` 의 `b`/`d` 타입(`.bss`/`.data`)과 TLS 의 `.tbss`/`.tdata` 가 같은 역할이고, 링커 맵(`-Wl,-Map`)으로 라이브러리 객체(`lib_a-strtok.o` 등)가 끌고 온 정적 변수까지 볼 수 있다. 정적 분석 도구(예: MISRA-style 검사, C-A10)도 같은 원리로 "함수 안 non-const static" 을 경고한다.

**ISR 안전성 분류 — 1. 문제의 답.** ✓ 안전 / ✗ 불안전 / △ 조건부.

| # | 함수 | 재진입 | 스레드 안전 | ISR / AS-safe | 근거 |
|---|---|---|---|---|---|
| 1 | `printf` | ✗ | ✓ | ✗ | `FILE` 버퍼·락 공유, 내부 `malloc` 가능. POSIX AS-safe 목록에 없음. UART 폴링이면 ISR 이 ms 단위로 길어짐 (C-M10, C-M14 Q6) |
| 2 | libc `snprintf` (지역 버퍼) | △ | ✓ | ✗ | 버퍼는 내 것이지만 로캘·부동소수점 변환 내부 상태/할당은 구현 몫(newlib `%f` 는 `_dtoa_r` 이 `_reent` 의 큰수 버퍼를 할당). 목록에 없음 |
| 3 | `malloc` / `free` | ✗ | ✓ | ✗ | 힙 락 — 위 데모에서 실제 재귀 락 크래시 |
| 4 | `itoa_static` | ✗ | ✗ | ✗ | 정적 결과 버퍼 |
| 5 | `itoa_r`, C-M10 `mini_snprintf` | ✓ | ✓ | ✓ | 호출자 버퍼 + 스택만. `static const` 표는 읽기 전용이라 무해 |
| 6 | `strtok` | ✗ | ✗ | ✗ | C11 7.24.5.8p6, POSIX "need not be thread-safe". (newlib + 태스크별 `_reent` 면 스레드 ✓) |
| 7 | `strtok_r` | ✓ | ✓ | ✓ | POSIX: thread-safe 명시, XSH 2.4.3 AS-safe 목록에 **있음** (save 와 버퍼가 호출자 것일 때) |
| 8 | `memcpy`, `strlen` | ✓ | ✓ | ✓ | 인자만 만짐. POSIX AS-safe 목록에 있음 |
| 9 | `rand` | ✗ | ✗ | ✗ | 숨은 시드. C11 7.22.2.1p3 "not required to avoid data races". 대신 상태 인자 PRNG (연습 4) |
| 10 | `strerror` | △ | ✗ | ✗ | 7.24.6.2p3 data race 면제. 알 수 없는 번호는 정적 버퍼에 만들기도. `strerror_r` 사용 |
| 11 | `my_strtok_tls` | ✗ | ✓ | ✗ | Part B |
| 12 | `atomic_fetch_add` (lock-free) | ✓ | ✓ | ✓ | C11 7.14.1.1p5 가 lock-free 원자 객체를 허용. M0/M0+ 처럼 lock-free 가 아니면 libcall (C-M14) |
| 13 | 뮤텍스 lock | ✗ (비재귀 뮤텍스는 자기 교착) | ✓ | ✗ | ISR 은 블록할 수 없다. FreeRTOS 뮤텍스는 ISR 에서 사용 불가 |
| 14 | `xQueueSendFromISR` | ✓ (임계 구역으로) | ✓ | ✓ | 블록하지 않고, 내부에서 인터럽트 마스크를 저장/복원. 대신 `pxHigherPriorityTaskWoken` 처리 필요. `xQueueSend` 는 ✗ |
| 15 | `HAL_Delay` | — | — | ✗ | SysTick ISR 이 올리는 `uwTick` 을 폴링. SysTick 보다 우선순위가 높거나 같은 ISR 안에서 부르면 tick 이 안 올라 영원히 대기 |
| 16 | `strtol` | ✓ | ✓ | △ | 정적 상태는 없지만 `errno` 를 쓴다. errno 는 스레드 저장 기간(C11 7.5p2) → ISR 이 main 의 errno 를 바꾼다. AS-safe 목록에도 없음(로캘 의존) |

16번이 보여 주듯 **ISR 은 같은 스레드의 TLS 를 공유** 한다. POSIX 는 핸들러가 errno 를 쓸 수 있는 함수(`write` 등)를 부르면 진입 때 저장, 복귀 전에 복원하라고 요구한다 (XSH 2.4.3). newlib 에서도 errno 는 `_reent` 안이다.

**함수를 ISR 에서 쓸 수 있게 만드는 세 가지 방법** (면접 정리용):

1. **상태를 없애거나 호출자에게** — `_r` 함수, 컨텍스트 구조체 (연습 3). 지연 0, 가장 깨끗하다.
2. **공유 상태를 인터럽트 마스킹으로 보호** — 저장/복원형 임계 구역 (C-M14 Part B). `xQueueSendFromISR` 가 이 방식. 짧아야 한다.
3. **ISR 에서는 하지 않는다** — ISR 은 원자 플래그/링 버퍼에 이벤트만 넣고 파싱·포맷·할당은 main/태스크에서 (C-M10 Part C). 실무에서 가장 흔한 답이다. 이 레슨의 텔레메트리 ISR 은 "공유 도우미가 재진입 가능함" 을 보이려는 예일 뿐, 실제 ISR 에서 문자열 파싱은 피한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution        # 28/28 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o sol_san && ./sol_san   # 28/28, 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o sol_tsan && ./sol_tsan        # 28/28, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 중첩 루프에서 `strtok` 두 번 | 안쪽이 바깥의 static 상태를 덮는다 → 뒷부분이 **조용히** 사라짐 | 단계마다 `char *save` 를 두고 `strtok_r` |
| 마지막 토큰 뒤 `*save = s + 1` | 문자열 끝 `'\0'` 을 넘어 다음 호출이 버퍼 밖을 읽음 (`demo_overrun.c` ASan) | 끝이면 `*save = s` (NUL 에 머물기) |
| `strtok_r` 에 문자열 리터럴 | 입력에 `'\0'` 을 쓴다 → 읽기 전용 메모리 쓰기 UB. 이 맥: `Bus error: 10` (exit 138) | `char buf[] = "..."` 사본, 또는 비파괴 `tok_span` (연습 2). `-Wwrite-strings` 로 컴파일 타임에 잡기 |
| "`_Thread_local` 로 바꿨으니 ISR 에서도 안전" | ISR 은 같은 스레드 → 같은 TLS | 상태를 호출자에게 |
| `strtok` 을 뮤텍스로 감싸 "스레드 안전" 선언 | 호출 사이의 상태는 보호 못 함 — 다른 스레드의 시퀀스와 섞인다 | API 를 `_r` 로 |
| ISR 안 `malloc` / `printf` / `HAL_Delay` | 힙 락 재진입(교착/크래시/손상), 블로킹, tick 정지 | ISR 은 이벤트만, 일은 main. 풀(pool)은 미리 할당 |
| 정적 버퍼 반환 함수 두 번을 한 식에서 | `printf("%s %s", itoa(1), itoa(2))` → 같은 버퍼, 결과는 평가 순서(미지정)에 좌우 | 호출자 버퍼 `itoa_r` |
| CSV/NMEA 를 `strtok` 로 | 연속 구분자를 합쳐 빈 필드가 사라지고 필드 번호가 밀린다 | `strsep` 류 (연습 1) |
| 핸들러에서 errno 를 바꾸는 함수 호출 후 그대로 복귀 | main 이 보던 errno 가 바뀜 | 진입 시 `int e = errno;` 복귀 전 `errno = e;` |
| 퍼즈/스트레스 테스트 몇 번 돌려 보고 "레이스 없음" | 실측 80만 줄 중 17–30줄만 깨짐 | TSan, 모든 지점 주입 테스트, `nm` 정적 객체 감사 |

`demo_literal.c` 실제 출력 (bash 로 실행해 셸 메시지까지):

```text
array  : 'led'
bash: line 1: 81660 Bus error: 10           /tmp/C-A04_lit
exit=138
```

`-Wall -Wextra` 로는 경고가 없다(C 에서 리터럴 타입은 `char[]`). `-Wwrite-strings` 를 켜면 리터럴이 `const char[]` 취급되어 잡힌다:

```text
demo_literal.c:13:11: warning: initializing 'char *' with an expression of type 'const char[6]' discards qualifiers [-Wincompatible-pointer-types-discards-qualifiers]
```

MCU 에서는 리터럴이 플래시(`.rodata`)에 있어서, 쓰기는 무시되거나 버스 폴트(HardFault/BusFault)가 난다 — 칩과 MPU 설정에 따라 다르다.

## 8. 면접 Q&A

<details>
<summary>Q1. 재진입 함수와 스레드 안전 함수의 차이는? 한쪽만 만족하는 예를 들어 보세요.</summary>

스레드 안전은 "여러 스레드가 **동시에** 불러도 맞다", 재진입은 "한 호출이 끝나기 **전에** 같은 문맥에서 다시 불려도 맞다(재귀, 콜백, 그 스레드를 선점한 ISR/시그널)". 스레드 안전이지만 재진입이 아닌 예: `malloc`(뮤텍스로 보호 — 다른 스레드는 기다리면 되지만, 락을 쥔 채 끊긴 같은 스레드에서 다시 부르면 자기 교착; 데모에서 macOS 는 재귀 락을 감지해 프로세스를 죽였다), `_Thread_local` 상태를 쓰는 `my_strtok_tls`. 반대로 재진입이지만 스레드 안전이 아닌 예: 공유 전역을 쓰되 인터럽트를 막아 보호하는 함수 — 단일 코어에선 ISR 에 대해 안전하지만 다른 코어/스레드는 못 막는다. 상태를 전부 호출자에게 두는 함수(`strtok_r`, `itoa_r`)는 둘 다 만족한다.

</details>

<details>
<summary>Q2. strtok 에 뮤텍스를 씌우면 스레드 안전해지나요?</summary>

개별 호출은 레이스가 없어지지만 쓸 수 있게 되진 않는다. 상태가 호출과 호출 **사이** 에 살아 있으므로 A 의 `strtok(a)`, B 의 `strtok(b)`, A 의 `strtok(NULL)` 순서가 되면 A 는 B 의 문자열을 이어 자른다. 토큰화 전체를 락으로 감싸면 정확해지지만 직렬화되고, 같은 스레드의 중첩·ISR 은 여전히 안 된다. 근본 원인은 API 가 숨은 상태를 갖는 설계 — 그래서 POSIX 는 `strtok_r` 를, C11 Annex K 는 `strtok_s(s1, &s1max, s2, &ptr)` 를 따로 만들었다. (MSVC 의 `strtok_s` 는 인자 3개짜리로 사실상 `strtok_r` 이고 Annex K 판과 시그니처가 다르다. glibc 는 Annex K 를 제공하지 않고, 이 맥의 SDK 26.4 헤더에도 Annex K 함수는 `memset_s` 하나뿐 `strtok_s` 는 없다.)

</details>

<details>
<summary>Q3. FreeRTOS 에서 configUSE_NEWLIB_REENTRANT 를 켰습니다. 이제 strtok 을 ISR 에서 써도 되나요?</summary>

아니다. 그 옵션은 태스크마다 newlib `struct _reent` 를 하나씩 주고 컨텍스트 스위치 때 바꿔 끼울 뿐이다 — 상태가 "태스크 로컬" 이 된 것이지 사라진 게 아니다. ISR 은 선점한 태스크의 `_reent` 를 그대로 쓰므로 그 태스크의 `strtok` 상태를 덮는다. 레슨의 `my_strtok_tls` 가 정확히 이 모델이고, 테스트에서 ISR 주입 시 main 이 `m2, m3` 을 잃는다. 같은 태스크 안의 중첩도 여전히 깨진다. 답: `strtok_r` + 지역 save 포인터, 더 좋게는 ISR 에선 파싱 자체를 안 한다.

</details>

<details>
<summary>Q4. ISR 에서 malloc 을 부르면 실제로 무슨 일이 생기나요? 대안은?</summary>

힙은 공유 자료구조이고 보통 락으로 보호된다. main 이 `malloc` 안에서 락을 쥔 순간 ISR 이 `malloc` 을 부르면 ① 일반 뮤텍스: 자기 자신을 기다리는 교착 ② 재귀 락 감지: 즉시 중단 — 이 맥에서 실측한 `BUG IN CLIENT OF LIBPLATFORM: Trying to recursively lock an os_unfair_lock` ③ 락이 없는 bare-metal newlib(`__malloc_lock` 스텁): 프리 리스트가 조용히 깨져 나중에 엉뚱한 곳에서 HardFault. RTOS 에서는 `pvPortMalloc` 이 스케줄러 정지(`vTaskSuspendAll`)로 보호하는데 이것은 ISR 을 막지 못한다. 그리고 실측처럼 대부분의 실행은 **멀쩡하다** — 그래서 출시 후에 터진다. 대안: 부팅 때 미리 할당, 고정 크기 블록 풀(ISR 에서는 마스킹으로 보호한 free-list pop), 또는 ISR 은 이벤트만 넘기고 할당은 태스크에서.

</details>

<details>
<summary>Q5. 어떤 함수가 재진입 가능한지 리뷰할 때 무엇을 보나요?</summary>

체크리스트: ① 쓰기 가능한 static/전역을 읽거나 쓰는가 (함수 안 `static` 포함) ② 정적 저장소를 가리키는 포인터를 돌려주는가 (`itoa_static`, `localtime`) ③ 재진입 불가 함수를 부르는가 (`printf`, `malloc`, `strtok`, `rand`) ④ 공유 하드웨어 자원(UART 데이터 레지스터, SPI 트랜잭션 중간 상태)을 보호 없이 쓰는가 ⑤ errno 같은 스레드 로컬 부작용 ⑥ 락을 잡는가 (ISR 에서 교착). 도구: `nm` 으로 `.bss/.data` 의 `함수명.변수명` 심볼 감사(이 레슨에서 `_my_strtok.last`, `_itoa_static.buf` 가 그대로 드러남), 링커 맵으로 라이브러리가 끌고 온 정적 변수 확인, 정적 분석. 하드웨어 레지스터 접근의 재진입 문제는 C-M02 의 RMW 와 같은 구조다.

</details>

<details>
<summary>Q6. 재진입성을 테스트로 어떻게 증명하나요?</summary>

스레드 안전과 달리 재진입 버그는 **같은 스레드** 에서 생기므로 TSan 이 못 잡는다. 결정적 방법은 "모든 지점 주입": 함수의 내부 단계(여기서는 토큰 호출)마다 훅을 두고, ISR 역할 함수를 1번째, 2번째 … N번째 지점에 하나씩 끼워 넣어 N번 돌린 결과가 모두 기대값과 같은지 본다 (Part C: 39개 지점). 호스트에서는 훅, 타깃에서는 디버거 브레이크포인트 스크립트나 타이머 ISR 의 주기를 소수로 바꿔 가며 끼어드는 위치를 훑는다. 다중 스레드 부분은 TSan(`demo_strtok_race.c` 는 첫 실행에서 `tok_static.last` 레이스를 짚었다), 공유 정적 객체는 `nm` 감사로 보완한다. 단순 반복 스트레스는 실측 0.004% 확률이라 증명이 되지 못한다.

</details>

<details>
<summary>Q7. 재귀 함수는 재진입 가능한가요? 반대는?</summary>

정확히 동작하는 재귀 함수는 "자기 자신에 대한 재진입" 은 만족해야 하지만, 그렇다고 ISR 에 대해 재진입 가능한 건 아니다 — 재귀 깊이를 정적 카운터로 세거나 정적 작업 버퍼를 쓰면 재귀는 맞게 짜도 ISR 이 끼어들면 깨진다. 반대로 재진입 가능 함수가 재귀일 필요는 없다. 임베디드에서는 재귀 자체를 피한다 — 스택 사용량을 정적으로 계산할 수 없어서다 (MISRA-style 규칙도 재귀를 금지, C-A10). Cortex-M 의 핸들러는 항상 MSP 스택 위에서 돌고(RTOS 태스크는 PSP), bare-metal 에서는 main 도 같은 MSP 를 쓰기 때문에 "재진입 가능 = 스택만 쓴다" 는 곧 스택 여유를 ISR 몫까지 잡아야 한다는 뜻이기도 하다.

</details>

<details>
<summary>Q8. NMEA 문장 "$GPGGA,123519,,,1" 를 strtok 으로 필드 분해하면 무엇이 잘못되나요?</summary>

`strtok` 은 연속 구분자를 하나로 합치고 빈 토큰을 내지 않는다. 그래서 빈 필드(위성 못 잡은 위도/경도 등)가 사라지고 그 뒤 필드 번호가 전부 앞으로 당겨진다 — 품질 표시자를 위도로 읽는 식. 연습 1 에서 `"$GPGGA,123519,4807.038,N,,,1,08"` 은 `strsep` 로 8필드, `strtok` 로 6필드였다. 해결: `strsep`(BSD, 빈 필드도 토큰, 상태는 호출자의 `char **`) 또는 직접 쉼표 위치를 세는 파서. 같은 이유로 CLI 처럼 "공백 여러 개를 하나로" 가 편한 곳에서만 strtok 계열을 쓴다.

</details>

## 9. 추가 연습

### 연습 1 — `my_strsep`: 빈 필드를 살리는 토크나이저 (NMEA 0183)

`char *my_strsep(char **sp, const char *delim)` 를 작성하라 (BSD `strsep` 의미: 구분자 **하나하나** 가 필드 경계, 빈 필드도 토큰, 마지막 필드 뒤 `*sp = NULL`). `"$GPGGA,123519,4807.038,N,,,1,08"` 의 필드 수를 `my_strsep` 과 `my_strtok_r` 로 각각 세라.

<details>
<summary>답 보기</summary>

```c
char *tok = *sp, *s = tok;
if (tok == NULL) return NULL;
while (*s && !is_delim(*s, delim)) s++;
if (*s) { *s = '\0'; *sp = s + 1; } else *sp = NULL;
return tok;
```

8필드 vs 6필드. `"a,,b,"` → `"a"`, `""`, `"b"`, `""` 그리고 `*sp == NULL`. 빈 문자열 `""` 도 빈 필드 하나다(`strtok` 은 0개). 상태는 호출자의 `char *p` 하나라 재진입 가능하다. 앞쪽 구분자를 건너뛰지 않는다는 점이 `strtok_r` 과의 유일한 차이이자 핵심이다.

</details>

### 연습 2 — `tok_span`: 입력을 고치지 않는 재진입 토크나이저

`int tok_span(const char **cur, const char *delim, const char **start, size_t *len)` — 토큰이 있으면 시작과 길이를 돌려주고 1, 없으면 0. 입력은 `const` 다 (플래시의 명령 테이블, 다른 모듈이 아직 읽는 DMA 수신 버퍼). `my_strtok_r` 과 같은 토큰열을 내는지 퍼즈로 확인하라.

<details>
<summary>답 보기</summary>

`strtok_r` 과 같은 건너뛰기/찾기를 하되 `'\0'` 을 쓰지 않고 `*cur` 를 **구분자 위(또는 NUL)** 에 둔다 — 다음 호출의 건너뛰기 단계가 그 구분자를 넘는다. 출력이 NUL 종료가 아니라 (포인터, 길이) 이므로 호출자는 `strncmp(st, "led", sl) == 0 && sl == 3` 처럼 비교한다. 3000 케이스 퍼즈에서 오프셋·길이가 모두 같고 원본 길이가 변하지 않음을 확인한다. 많은 펌웨어 CLI 는 인자 배열(`argc/argv`)을 만들려고 결국 버퍼에 NUL 을 쓰지만, 수신 버퍼를 덮으면 안 되는 상황(재전송, 로그 보관)에서는 이 방식이 필요하다.

</details>

### 연습 3 — UART 두 개를 위한 줄 조립기: static → 컨텍스트 구조체

주어진 `line_feed_static(char c)` 는 `'\n'` 에서 완성된 줄을 돌려준다 (상태: 파일 static 버퍼·인덱스). UART1 `"AT+X\r\n"` 과 UART2 `"OK\r\n"` 의 바이트가 번갈아 도착할 때 무엇이 나오나? 상태를 `line_ctx_t` 로 옮긴 `line_feed(line_ctx_t *cx, char c)` 를 작성하라 (오버플로 글자 수도 센다).

<details>
<summary>답 보기</summary>

static 판: UART2 의 `'\n'` 이 먼저 와서 `"AOTK+X"` (두 포트의 글자가 한 버퍼에 섞임), UART1 은 빈 줄 `""`. 컨텍스트 판: `"AT+X"`, `"OK"`. 완성된 줄은 `cx->line` 에 따로 복사해 돌려준다 — 같은 버퍼를 돌려주면 다음 바이트가 호출자가 읽기 전에 덮을 수 있다. 이것이 HAL 의 `UART_HandleTypeDef *huart` 인자, Zephyr 의 `const struct device *dev` 인자가 존재하는 이유다: **드라이버 인스턴스가 여러 개가 되는 순간 static 상태는 버그** 다. (RX ISR 이 `line_feed` 를 부르고 main 이 `cx->line` 을 읽는다면, 그 공유는 다시 C-M14 의 문제 — 더블 버퍼나 플래그로 넘긴다.)

</details>

### 연습 4 — 난수: 숨은 상태 vs 상태 인자

`rng_static()` (숨은 xorshift32 시드) 로 main 이 4개를 뽑는 도중, 2번째 뒤에 ISR 이 1개를 뽑는다. main 이 받는 수열을 끼어들기 없는 기준 수열과 비교하라. `rng_r(uint32_t *state)` 로 고쳐 같은 실험을 하라.

<details>
<summary>답 보기</summary>

static 판: main 은 기준 수열의 0, 1, **3** 번째를 받고 ISR 이 2번째를 가져간다 — 테스트가 이 "훔치기" 를 정확히 확인한다. 재현성(같은 시드 → 같은 시험 패턴)이 중요한 펌웨어 자체 시험(BIST, 메모리 패턴 테스트)에서 이건 디버깅 불가능한 비결정성이 된다. `rng_r` 은 상태를 인자로 받으므로 main 과 ISR 이 각자 시드를 가진다 → main 수열이 기준과 동일. C 표준 `rand` 는 7.22.2.1p3 로 데이터 레이스 면제이고, POSIX `rand_r` 은 상태가 `unsigned` 하나뿐이라 품질이 제한적이다(POSIX.1-2008 에서 obsolescent 표시) — 펌웨어에선 상태 인자형 xorshift/PCG 를 직접 쓰는 편이 낫다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 11/11 passed
```

## 10. 핵심 정리

- **재진입**(끝나기 전에 같은 문맥에서 다시 불려도 됨) ≠ **스레드 안전**(동시에 불려도 됨) ≠ **ISR/async-signal 안전**(아무 데서나 끊긴 위에서 불려도 됨). 차이는 "기다릴 수 있느냐" — ISR 은 자기가 선점한 코드를 기다릴 수 없다.
- `strtok` 의 문제는 숨은 `static` 상태 하나: 중첩이면 덮어쓰기(조용한 데이터 손실), 스레드면 레이스(실측 0.004%, TSan 은 즉시), ISR 이면 재진입 실패.
- `_Thread_local` 은 스레드 문제만 푼다 — ISR 은 같은 스레드, 같은 TLS (newlib `_reent` 도 같다). 뮤텍스는 ISR 에서 교착/크래시 (macOS 실측: 재귀 `os_unfair_lock` 중단).
- 해법은 **상태를 호출자에게**: `strtok_r(s, d, &save)` — 중첩 단계마다 save 하나, `itoa_r(v, buf, n)`, 드라이버 컨텍스트 구조체.
- `strtok_r` 경계: 앞 구분자 건너뛰기, 끝에선 `*save` 를 NUL 에 머물게(넘어가면 OOB), `s != NULL` 이면 `*save` 무시, 입력 파괴(리터럴 금지), 빈 필드 합침(NMEA/CSV 는 `strsep`).
- ISR 금지 목록의 근거: `printf`/`malloc`(락·공유 버퍼), 정적 결과 반환(`itoa`, `localtime`, `strerror`), 숨은 상태(`rand`, `strtok`), 블로킹(뮤텍스, `HAL_Delay`), errno 부작용. 허용: 인자만 만지는 함수(`memcpy`, `strtok_r` — POSIX AS-safe 목록), lock-free 원자, `...FromISR` API.
- 시험: 모든 지점 ISR 주입(결정적), TSan(스레드), `nm` 으로 `함수명.변수명` 정적 객체 감사.

## 참고자료

- C11 (N1570): 7.24.5.8 strtok (p6 데이터 레이스 면제, EXAMPLE), 7.1.4p4–p5 와 각주 188 (라이브러리 재진입 비보장, 시그널 핸들러), 7.22.2.1p3 rand, 7.24.6.2p3 strerror, 7.5p2 errno 스레드 저장 기간, 6.7.1p3 블록 범위 `_Thread_local`, 7.14.1.1p4–p5 시그널 핸들러 제약, 5.1.2.3p5 핸들러 중/후 객체 값, K.3.7.3.1 strtok_s — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- POSIX.1-2017 strtok / strtok_r — <https://pubs.opengroup.org/onlinepubs/9699919799/functions/strtok.html>
- POSIX.1-2017 XSH 2.4.3 Signal Actions (async-signal-safe 함수 목록, errno 저장/복원) — <https://pubs.opengroup.org/onlinepubs/9699919799/functions/V2_chap02.html#tag_15_04_03>
- POSIX.1-2017 XBD 3.27 Async-Signal-Safe Function, 3.407 Thread-Safe — <https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/V1_chap03.html>
- newlib `strtok.c` / `struct _reent`, FreeRTOS `configUSE_NEWLIB_REENTRANT` — <https://sourceware.org/newlib/>, <https://www.freertos.org/a00110.html>
- FreeRTOS `xQueueSendFromISR` — <https://www.freertos.org/a00119.html>
- Clang ThreadSanitizer — <https://clang.llvm.org/docs/ThreadSanitizer.html>
- 관련 레슨: C-M14 (ISR 공유 데이터·임계 구역·async-signal-safe), C-M10 (재진입 가능한 포매터와 로거), C-B07 (strlen/strcpy), C-B10 (static 과 링키지), C-B11 (메모리 섹션), C-B14 (평가 순서), C-M02 (RMW), C-A03 (C11 atomics·TSan), C-A10 (정적 분석), C-A11 (ISR 코드 리뷰)
