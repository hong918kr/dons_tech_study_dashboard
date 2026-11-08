---
id: C-B07
time: 50분
updated: 2026-11-07
---
# C-B07 문자열 기본 구현: strlen strcpy strcmp strncpy

## 학습 목표

- C 문자열 = **NUL(`'\0'`)로 끝나는 char 배열** 이라는 규칙 하나로 `strlen` / `strcpy` / `strcmp` 를 손으로 구현한다.
- `strcmp` 가 왜 **`unsigned char` 로 비교**해야 하는지, 플랫폼마다 `char` 의 부호가 다르다는 사실과 함께 설명한다.
- `strncpy` 의 비직관적 동작 두 가지 — **0 패딩**, **길이 ≥ n 이면 NUL 없음** — 을 정확히 말하고, `strlcpy` 의 반환값으로 **잘림(truncation)을 감지**한다.
- 경계값 테스트(빈 문자열, 정확히 n 길이, 버퍼 끝)를 **AddressSanitizer(ASan)** 와 함께 돌려 1바이트 넘침을 잡는다.
- 펌웨어의 고정 폭 이름 필드, UART 명령 줄 버퍼를 넘침 없이 설계한다.

## 1. 문제

"strcpy를 구현해 보세요"는 펌웨어 면접의 단골 워밍업이고, 꼬리질문(strncpy는 안전한가? strcmp 반환값은?)에서 실력이 갈린다. 실무에서도 버퍼 오버플로의 대부분이 문자열 처리에서 나온다 — MMU가 없는 Cortex-M에서는 넘친 1바이트가 segfault 없이 옆 변수를 **조용히** 망가뜨린다.

C에는 문자열 타입이 없다. **"첫 번째 `'\0'` 까지가 문자열"** 이라는 약속(C11 7.1.1)만 있다. 그래서 길이(`strlen`)와 필요한 저장 공간(`길이 + 1`)이 항상 1 차이 난다. 이 1이 거의 모든 버그의 원인이다.

```svg
<svg viewBox="0 0 720 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">"sensor-1" 을 담으려면 몇 바이트?</text>
  <text x="10" y="64" fill="currentColor">문자열 리터럴</text>
  <g stroke="currentColor" fill="none">
    <rect x="150" y="44" width="40" height="30"/><rect x="190" y="44" width="40" height="30"/>
    <rect x="230" y="44" width="40" height="30"/><rect x="270" y="44" width="40" height="30"/>
    <rect x="310" y="44" width="40" height="30"/><rect x="350" y="44" width="40" height="30"/>
    <rect x="390" y="44" width="40" height="30"/><rect x="430" y="44" width="40" height="30"/>
  </g>
  <rect x="470" y="44" width="40" height="30" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="170" y="64">s</text><text x="210" y="64">e</text><text x="250" y="64">n</text><text x="290" y="64">s</text>
    <text x="330" y="64">o</text><text x="370" y="64">r</text><text x="410" y="64">-</text><text x="450" y="64">1</text>
    <text x="490" y="64">\0</text>
  </g>
  <text x="530" y="56" fill="currentColor">strlen = 8</text>
  <text x="530" y="74" fill="currentColor">필요 공간 = 9</text>
  <text x="10" y="134" fill="currentColor">char name[8]</text>
  <g stroke="currentColor" fill="none">
    <rect x="150" y="114" width="320" height="30"/>
  </g>
  <rect x="470" y="114" width="40" height="30" class="f-warn" stroke="currentColor" stroke-dasharray="4 3"/>
  <g fill="currentColor" font-family="monospace">
    <text x="160" y="134">s  e  n  s  o  r  -  1</text>
    <text x="480" y="134">\0</text>
  </g>
  <text x="530" y="126" fill="currentColor">name[8] = 남의 메모리</text>
  <text x="530" y="144" fill="currentColor">(옆 변수, 리턴 주소…)</text>
  <text x="10" y="186" fill="currentColor">strcpy(name, "sensor-1") 은 9바이트를 쓴다. 마지막 NUL 1바이트가 배열 밖으로 나간다 (off-by-one).</text>
  <text x="10" y="210" fill="currentColor">반대로 NUL 이 없는 배열에 strlen 을 부르면, 0을 만날 때까지 배열 밖을 계속 읽는다.</text>
</svg>
```

### Part A — strlen / strcpy / strcmp

- `size_t my_strlen(const char *s)` — NUL 앞까지의 바이트 수.
- `char *my_strcpy(char *dst, const char *src)` — NUL 까지 포함해 복사, `dst` 반환.
- `int my_strcmp(const char *a, const char *b)` — 같으면 0, 사전순으로 a가 작으면 음수, 크면 양수. **바이트는 `unsigned char` 로 비교** (`"\x80"` 은 `"a"` 보다 크다).

테스트는 libc 결과와 대조한다. `strcmp` 는 값이 아니라 **부호**만 같으면 된다 (표준은 부호만 정한다. 이 맥에서도 상수 인자면 clang이 접어서 1, 런타임 libc 호출이면 차이값 31 이 나왔다).

### Part B — strncpy / strlcpy 와 경계값

- `char *my_strncpy(char *dst, const char *src, size_t n)` — 표준 그대로: **정확히 n 바이트**를 쓴다. src가 짧으면 나머지를 `'\0'` 으로 채우고, src 길이가 n 이상이면 **NUL을 붙이지 않는다**.
- `size_t my_strlcpy(char *dst, const char *src, size_t size)` — BSD 스타일: 최대 `size-1` 자 복사 + 항상 NUL (size > 0일 때), 반환값은 **`strlen(src)`**. `반환값 >= size` 이면 잘린 것.
- 테스트: 빈 문자열, 정확히 n 길이, n보다 긴 문자열, n=0 / size=0·1, 그리고 **딱 n 바이트로 malloc 한 버퍼**(버퍼 끝 경계)에서 libc와 바이트 단위로 비교.

### Part C — 장치 이름 필드 + UART 명령 줄 버퍼

- 플래시에 저장하는 `dev_record_t` 의 `char name[16]` 은 **고정 폭 필드**다: 남는 칸은 0으로 채우고(레코드 CRC가 매번 같도록), 정확히 16자면 NUL 없이 꽉 찬다.
  - `dev_set_name(r, name)` — 17자 이상이면 거부(-1, 레코드 그대로).
  - `dev_get_name(r, out, outsz)` — 필드 **밖을 절대 읽지 않고** C 문자열로 꺼낸다. 반환은 이름 길이 (strlcpy처럼 잘림 감지 가능).
- `line_feed(lb, c)` — UART RX ISR이 1바이트씩 부르는 줄 조립기. `'\r'` 무시, `'\n'` 에서 NUL 종료 후 1 반환, 버퍼(32바이트, 내용 최대 31자)를 넘친 줄은 끝까지 버리고 `'\n'` 에서 -1.
- (주어진 코드) `cmd_exec()` 가 `"NAME sensor-01"` / `"CLEAR"` 를 `my_strcmp` 로 분기한다.

## 2. Problem (English)

> **Implement the core C string functions and use them safely in a firmware context.**
>
> Without calling the library versions (except in tests), implement:
>
> 1. `size_t my_strlen(const char *s)`, `char *my_strcpy(char *dst, const char *src)`, `int my_strcmp(const char *a, const char *b)`. `my_strcmp` must order bytes as `unsigned char`, as the standard requires.
> 2. `char *my_strncpy(char *dst, const char *src, size_t n)` with exact ISO C semantics (writes exactly `n` bytes, zero-pads, no terminator if `strlen(src) >= n`), and `size_t my_strlcpy(char *dst, const char *src, size_t size)` with BSD semantics (always terminates when `size > 0`, returns `strlen(src)`).
> 3. A 16-byte fixed-width, zero-padded name field in a flash record (`dev_set_name`, `dev_get_name` that never reads past the field), and a byte-at-a-time UART line assembler with a 32-byte buffer that never writes out of bounds and reports overlong lines.
>
> Verify against libc on: empty string, length exactly `n`, length > `n`, `n == 0`, and buffers allocated to the exact size, under `-fsanitize=address`.
>
> Example: `my_strncpy(buf, "abcd", 4)` leaves `buf = {'a','b','c','d'}` with no NUL; `my_strlcpy(buf8, "12345678", 8)` returns 8 (`>= 8` → truncated) and leaves `"1234567"`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 깨짐 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# FAIL 줄이 쭉 나오고 "11/57 passed" — TODO를 채워 57/57 을 만든다

cc -std=c11 -g -O1 -fsanitize=address,undefined starter.c -o starter_asan && ./starter_asan
# 다 채운 뒤에는 반드시 ASan 빌드로도 돌린다 (넘침 1바이트도 여기서만 보인다)
```

테스트 버퍼는 `malloc(len + 1)`, `malloc(n)` 처럼 **딱 맞게** 잡는다. 일반 빌드에서는 1바이트 넘쳐도 malloc이 16바이트 단위로 반올림해 둔 여유 공간에 써서 PASS가 나올 수 있다. ASan은 그 1바이트를 잡는다.

## 4. 힌트

<details>
<summary>힌트 1 — strlen 은 포인터 뺄셈 한 번</summary>

포인터 `p` 를 NUL 까지 전진시킨 뒤 `p - s` 를 돌려준다. `ptrdiff_t` 결과를 `size_t` 로 캐스트.

</details>

<details>
<summary>힌트 2 — strcpy 는 "복사한 다음에" 검사</summary>

`while ((*d++ = *src++) != '\0') {}` — 대입식의 값이 방금 복사한 문자다. 검사를 복사 **앞에** 두면 NUL 을 안 쓰고 끝난다.

</details>

<details>
<summary>힌트 3 — strcmp 는 unsigned char 포인터로</summary>

`const unsigned char *p = (const unsigned char *)a;` 로 바꾼 뒤, 다르거나 NUL 이 나올 때까지 진행하고 `(int)*p - (int)*q`. `char` 그대로 빼면 macOS/x86에서는 0x80 이상 바이트가 음수가 된다.

</details>

<details>
<summary>힌트 4 — strncpy 는 루프 두 개, strlcpy 는 min(len, size-1)</summary>

strncpy: ① `i < n && src[i]` 동안 복사 ② `i < n` 동안 0 채우기. strlcpy: `len = strlen(src)`, `size != 0` 이면 `k = min(len, size-1)` 바이트 복사 후 `dst[k] = 0`. `size - 1` 은 size가 0이면 `SIZE_MAX` 가 되니 먼저 검사.

</details>

<details>
<summary>힌트 5 — 줄 버퍼는 NUL 자리 1칸을 예약</summary>

저장 조건을 `len < LINE_MAX - 1` 로 하면 `'\n'` 에서 `buf[len] = 0` 을 써도 항상 `len <= 31` 이라 안전하다. `len < LINE_MAX` 로 쓰면 32자 줄에서 `buf[32]` 에 NUL 을 쓴다.

</details>

## 5. 풀이

### Part A — NUL 이 유일한 끝 표시

| 함수 | 핵심 루프 | 끝나는 조건 | 반환 |
|---|---|---|---|
| strlen | `while (*p) p++;` | `*p == 0` | `p - s` (NUL 미포함) |
| strcpy | `while ((*d++ = *s++)) {}` | **복사한** 문자가 0 | `dst` (원래 시작 주소) |
| strcmp | `while (*p && *p == *q) {p++; q++;}` | 다르거나 a가 끝남 | `*p - *q` (unsigned char 값) |

- **strcmp 루프에서 `*q` 의 NUL 은 따로 검사하지 않아도 된다**: `*p != 0` 이고 `*p == *q` 면 `*q` 도 0이 아니다. 둘 중 하나가 먼저 끝나면 그 자리에서 값이 달라 멈춘다 (`"ab"` vs `"abc"` → `0 - 'c'` < 0).
- `strcpy` 의 `src`/`dst` 가 겹치면 **UB** (C11 7.24.2.3 — 프로토타입에도 `restrict`). 겹칠 수 있으면 `memmove`.
- `my_strcpy` 가 원래 `dst` 를 돌려주는 이유: `strcat(strcpy(buf, a), b)` 같은 체이닝. 끝 위치가 필요하면 POSIX `stpcpy`.

**왜 unsigned char 인가.** C11 7.24.4p1: 비교 함수의 부호는 "첫 번째로 다른 문자 쌍을 **unsigned char 로 해석한** 차이의 부호"로 정해진다. 그런데 plain `char` 의 부호는 **구현 정의(implementation-defined)** 다. 실제로 확인한 값:

| 타깃 (clang `--target`) | `char` | `__CHAR_UNSIGNED__` |
|---|---|---|
| arm64-apple-macos (이 맥) | signed | 0 |
| x86_64-linux-gnu | signed | 0 |
| aarch64-linux-gnu | unsigned | 1 |
| **arm-none-eabi (Cortex-M)** | **unsigned** | 1 |
| riscv32-unknown-elf | unsigned | 1 |

`return *a - *b;` (char 그대로) 로 쓴 naive 버전을 이 맥에서 돌린 결과:

```text
$ cc -std=c11 -O2 naive.c && ./a.out
naive=-225 libc=1          ← char 0x80 이 -128 로 읽혀 부호가 반대! (libc=1 은 clang 상수 접기 결과)
$ cc -std=c11 -O2 -funsigned-char naive.c && ./a.out
naive=31 libc=1            ← Cortex-M 처럼 unsigned char 면 우연히 맞음
```

즉 **호스트 단위 테스트는 통과했는데 타깃에서 정렬 순서가 다른** 버그(혹은 그 반대)가 난다. UTF-8 장치 이름(한글 등 0x80 이상 바이트)을 정렬·비교하면 바로 드러난다.

### Part B — strncpy 는 "안전한 strcpy" 가 아니다

`strncpy` 는 원래 Unix V7 디렉터리 엔트리(14바이트 고정 이름 필드)처럼 **고정 폭, 0 패딩 필드**를 채우려고 만들어졌다 (C11 7.24.2.4). 그래서 "버퍼 크기 n 짜리 C 문자열을 만든다" 가 아니라 "n 바이트 필드를 채운다" 로 동작한다.

```svg
<svg viewBox="0 0 720 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">n = size = 4 일 때 결과 비교 (· = 건드리지 않음)</text>
  <g fill="currentColor">
    <text x="170" y="48">strncpy(dst, src, 4)</text>
    <text x="430" y="48">strlcpy(dst, src, 4)</text>
    <text x="600" y="48">반환</text>
    <text x="10" y="82">src = "ab"</text>
    <text x="10" y="132">src = "abcd"</text>
    <text x="10" y="182">src = "abcdef"</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="170" y="62" width="128" height="30"/><rect x="170" y="112" width="128" height="30"/><rect x="170" y="162" width="128" height="30"/>
    <rect x="430" y="62" width="128" height="30"/><rect x="430" y="112" width="128" height="30"/><rect x="430" y="162" width="128" height="30"/>
  </g>
  <rect x="234" y="62" width="64" height="30" class="f-mut"/>
  <rect x="170" y="112" width="128" height="30" class="f-warn"/>
  <rect x="170" y="162" width="128" height="30" class="f-warn"/>
  <rect x="494" y="62" width="32" height="30" class="f-ok"/>
  <rect x="526" y="112" width="32" height="30" class="f-ok"/>
  <rect x="526" y="162" width="32" height="30" class="f-ok"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="186" y="82">a</text><text x="218" y="82">b</text><text x="250" y="82">\0</text><text x="282" y="82">\0</text>
    <text x="186" y="132">a</text><text x="218" y="132">b</text><text x="250" y="132">c</text><text x="282" y="132">d</text>
    <text x="186" y="182">a</text><text x="218" y="182">b</text><text x="250" y="182">c</text><text x="282" y="182">d</text>
    <text x="446" y="82">a</text><text x="478" y="82">b</text><text x="510" y="82">\0</text><text x="542" y="82">·</text>
    <text x="446" y="132">a</text><text x="478" y="132">b</text><text x="510" y="132">c</text><text x="542" y="132">\0</text>
    <text x="446" y="182">a</text><text x="478" y="182">b</text><text x="510" y="182">c</text><text x="542" y="182">\0</text>
  </g>
  <g fill="currentColor">
    <text x="310" y="82">0 패딩</text>
    <text x="310" y="132">NUL 없음!</text>
    <text x="310" y="182">NUL 없음!</text>
    <text x="600" y="82">2  (&lt; 4 OK)</text>
    <text x="600" y="132">4  (≥ 4 잘림)</text>
    <text x="600" y="182">6  (≥ 4 잘림)</text>
  </g>
  <text x="10" y="222" fill="currentColor">strncpy: 정확히 n 바이트, 꽉 차면 NUL 없음 → 이후 strlen/printf 가 배열 밖을 읽는다.</text>
  <text x="10" y="242" fill="currentColor">strlcpy: 항상 NUL 종료(size&gt;0), 패딩 없음, 반환 = strlen(src) → 잘림을 숫자로 안다.</text>
</svg>
```

경계 사례 정리 (전부 테스트에 있음):

| 입력 | strncpy | strlcpy |
|---|---|---|
| src = `""`, n=4 | 0 4개 | `dst[0]=0`, 반환 0 |
| 길이 = n-1 (`"abc"`, 4) | `abc\0` | `abc\0`, 반환 3 → OK |
| **길이 = n** (`"abcd"`, 4) | `abcd` (NUL 없음) | `abc\0`, 반환 4 → **잘림** |
| n = 0 / size = 0 | 아무것도 안 씀 | 아무것도 안 씀, 반환 strlen(src) |
| size = 1 | — | `dst[0]=0`, 반환 strlen(src) |

- `n == 0` 이어도 포인터 인자는 유효해야 한다 (C11 7.24.1p2). `strncpy(NULL, s, 0)` 은 UB.
- `strlcpy` 의 비용: 반환값 때문에 **src 전체를 strlen** 한다. src가 거대하거나 NUL 이 없는(신뢰할 수 없는) 버퍼면 과잉 읽기다. Linux 커널이 `strlcpy` 를 버리고 `strscpy`(잘리면 `-E2BIG`, src를 `count` 바이트까지만 읽음)로 옮긴 이유.
- `strncpy` 의 비용: 4 KB 버퍼에 3글자를 복사해도 4096 바이트를 쓴다.

| 상황 | 추천 |
|---|---|
| C 문자열 → C 문자열 (크기 제한) | `strlcpy` 또는 `snprintf(dst, sz, "%s", src)` + 반환값 검사 |
| 고정 폭, 0 패딩 레코드 필드 (NUL 없어도 됨) | `strncpy` — 원래 용도. 읽을 땐 `strnlen(field, N)` |
| 길이를 이미 앎 | `memcpy` + 직접 `dst[len] = 0` |
| 겹칠 수 있음 | `memmove` |

### Part C — 고정 폭 필드와 줄 버퍼의 경계

**이름 필드.** `dev_set_name` 은 길이 검사 후 `my_strncpy(r->name, name, 16)` 한 줄이다. 여기서는 strncpy가 정답이다: 남는 칸을 0으로 채워서 레코드의 CRC가 이전 쓰레기 값에 좌우되지 않고, 스택 쓰레기가 플래시/무선으로 새어 나가지도 않는다. 대신 읽는 쪽은 **NUL을 믿으면 안 된다** — `dev_get_name` 은 `n < 16 && name[n]` 으로 길이를 재서 필드 밖을 읽지 않는다. (참고: nRF5 SoftDevice의 `sd_ble_gap_device_name_set(&mode, name, len)` 처럼 펌웨어 API는 NUL 대신 **길이를 따로** 받는 경우가 많다.)

**UART 줄 버퍼.** STM32 HAL이면 `HAL_UART_Receive_IT(&huart, &rx, 1)` → `HAL_UART_RxCpltCallback()` 안에서 `line_feed(&lb, rx)` 를 부르는 구조다.

```svg
<svg viewBox="0 0 720 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">line_buf_t.buf[32] — 내용 31칸 + NUL 1칸 예약</text>
  <rect x="20" y="40" width="620" height="32" class="f-ok" stroke="currentColor"/>
  <rect x="640" y="40" width="60" height="32" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="30" y="61" font-family="monospace">buf[0] … buf[30]  : 받은 문자 (len 은 0..31)</text>
    <text x="648" y="61" font-family="monospace">buf[31]</text>
    <text x="20" y="92" font-size="12">저장 조건: len &lt; LINE_MAX - 1 (= 31)  →  '\n' 에서 buf[len] = '\0' 은 항상 buf[31] 이하</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="120" width="200" height="54" rx="8"/>
    <rect x="260" y="120" width="200" height="54" rx="8"/>
    <rect x="500" y="120" width="200" height="54" rx="8"/>
  </g>
  <g fill="currentColor">
    <text x="32" y="142">일반 문자</text>
    <text x="32" y="162" font-size="12">len&lt;31 ? 저장 : overflow=1</text>
    <text x="272" y="142">'\r'</text>
    <text x="272" y="162" font-size="12">무시 (CRLF 단말 대응)</text>
    <text x="512" y="142">'\n'</text>
    <text x="512" y="162" font-size="12">NUL 쓰기, 1 또는 -1, 리셋</text>
  </g>
  <text x="20" y="206" fill="currentColor">31자 줄 → 1 (경계, 정상).  32자 줄 → 32번째 바이트부터 버리고 '\n' 에서 -1.</text>
  <text x="20" y="230" fill="currentColor">-1 뒤에 len/overflow 를 리셋해야 다음 줄("CLEAR")이 정상 처리된다 (오류 복구).</text>
</svg>
```

- 넘친 줄을 **잘라서 실행하지 않고 버리는** 이유: `"NAME abc…(잘림)"` 이 엉뚱한 값으로 실행되는 것보다, 오류를 돌려주고 호스트가 재전송하게 하는 편이 안전하다.
- 실제 펌웨어에서는 ISR이 `buf` 를 채우는 동안 메인 루프가 같은 `buf` 를 파싱하면 경쟁이 난다. 줄이 완성되면 다른 버퍼로 복사하거나(더블 버퍼) 완성 플래그를 세우고 다음 바이트를 버린다.
- `cmd_exec` 은 첫 공백을 `'\0'` 으로 바꿔 **제자리 토큰 분리**를 한다 — 복사 없이 `line` 이 명령, `arg` 가 인자가 된다. `strtok` 은 내부 정적 상태가 있어 ISR/RTOS 태스크에서 재진입 불가라 펌웨어에서는 피한다.

### ASan 으로 확인

정답 코드 ASan 빌드 (깨끗함):

```text
$ cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o sol_asan && ./sol_asan
57/57 passed
```

**데모 1 — `demo_asan_offbyone.c`**: `malloc(strlen(src))` (+1 빠짐) 과 `char name[8]` 에 `"sensor-1"` 을 `my_strcpy`.

```text
$ cc -std=c11 -g -O0 -fsanitize=address demo_asan_offbyone.c -o demo_asan && ./demo_asan heap
==12092==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x602000000978 ...
WRITE of size 1 at 0x602000000978 thread T0
    #0 0x000104be4ba0 in my_strcpy demo_asan_offbyone.c:11
    #1 0x000104be49e0 in main demo_asan_offbyone.c:24
0x602000000978 is located 0 bytes after 8-byte region [0x602000000970,0x602000000978)
SUMMARY: AddressSanitizer: heap-buffer-overflow demo_asan_offbyone.c:11 in my_strcpy

$ ./demo_asan stack
==12103==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x00016d6c6aa8 ...
WRITE of size 1 at 0x00016d6c6aa8 thread T0
    #0 0x000102738ba0 in my_strcpy demo_asan_offbyone.c:11
  This frame has 1 object(s):
    [32, 40) 'name' (line 18) <== Memory access at offset 40 overflows this variable
```

"0 bytes after 8-byte region", "offset 40 overflows [32, 40)" — 정확히 NUL 1바이트다. 같은 코드를 **ASan 없이** 돌리면:

```text
$ cc -std=c11 -g -O0 demo_asan_offbyone.c -o demo_plain
$ ./demo_plain heap ; echo exit=$?
heap: sensor-1
exit=0                      ← 힙 넘침은 조용히 "성공" (malloc 반올림 여유에 씀)
$ ./demo_plain stack ; echo exit=$?
exit=134                    ← 스택은 macOS 기본 stack protector 카나리가 SIGABRT
```

`-fno-stack-protector` 로 빌드하면 stack 모드도 `stack: sensor-1`, exit 0 으로 조용히 지나갔다. Cortex-M 펌웨어는 보통 카나리도, MMU도 없으니 이 "조용히 지나감" 이 기본값이다.

**데모 2 — `demo_strncpy_nonul.c`**: `my_strncpy(name, "sensor-1", 8)` 후 `strlen(name)`.

```text
$ cc -std=c11 -g -O0 -fsanitize=address demo_strncpy_nonul.c -o demo_nonul && ./demo_nonul
==12485==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x00016d162ae8 ...
READ of size 9 at 0x00016d162ae8 thread T0
    #0 0x00010350a718 in strlen+0x22c (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x16718)
    #1 0x000102c9c908 in main demo_strncpy_nonul.c:17
    [32, 40) 'name' (line 15) <== Memory access at offset 40 overflows this variable
```

주의 — 최적화가 버그를 숨긴다: 같은 데모를 `-O1 -fsanitize=address` 로 빌드하면 컴파일러가 길이를 상수 8로 접어 버려 **보고 없이** `len = 8` 이 나왔고, ASan 없는 `-O2` 빌드는 `len = 9` (스택 쓰레기까지 읽음) 를 찍었다. 데모 1도 `-O1` 에서는 루프가 `memcpy` 9바이트로 바뀌어 `WRITE of size 9 ... in __asan_memcpy` 로 보고된다. 버그 재현용 ASan 빌드는 `-O0` 또는 `-O1` 둘 다 돌려 본다.

**데모 3 — `demo_idiom.c`**: 컴파일러가 "내 루프" 를 libc 호출로 바꾼다 (idiom recognition).

```text
$ cc -std=c11 -O2 -S demo_idiom.c -o - | grep -A4 '^_my_'
_my_strlen:
	b	_strlen            ← 루프 전체가 strlen 꼬리 호출로
_my_memset:
	cbz	x2, LBB1_2
	b	_memset            ← 마찬가지
$ cc -std=c11 -O2 -fno-builtin -S demo_idiom.c -o - | grep -A8 '^_my_strlen'
_my_strlen:
	mov	x8, #0
LBB0_1:
	ldrb	w9, [x0, x8]       ← 이제 진짜 바이트 루프
	add	x8, x8, #1
	cbnz	w9, LBB0_1
```

(Apple clang 21.) 두 가지 결론: ① `-O2` 빌드의 `my_strlen` 테스트는 사실상 libc 대 libc 비교다 → `-fno-builtin` 으로도 돌려 보자 (57/57 확인함). ② libc 자체(`strlen`, `memset`)를 구현하는 펌웨어에서는 이 변환이 **자기 자신을 부르는 무한 재귀**가 된다 → `-ffreestanding` / `-fno-builtin` (GCC는 추가로 `-fno-tree-loop-distribute-patterns`).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution              # 57/57 passed
cc -std=c11 -Wall -Wextra -Werror -O2 -fno-builtin solution.c -o sol_nb && ./sol_nb     # 57/57 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o sol_asan && ./sol_asan   # 57/57, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `malloc(strlen(s))` | NUL 자리 1바이트 부족 (데모 1) | `malloc(strlen(s) + 1)` |
| `while (*src) *d++ = *src++;` | NUL 을 복사하지 않고 끝남 | 루프 뒤 `*d = '\0';` 또는 `while ((*d++ = *src++))` |
| `return *a - *b;` (char 그대로) | char 부호가 플랫폼마다 다름 → 0x80 이상 바이트에서 부호 반대 | `unsigned char` 로 캐스트 후 빼기 |
| `if (strcmp(a, b) == 1)` | 반환값은 "부호"만 보장. 차이값(31, -2 등)일 수 있음 | `> 0`, `< 0`, `== 0` |
| `strncpy(buf, s, sizeof buf);` 후 printf | `strlen(s) >= sizeof buf` 면 NUL 없음 (데모 2) | `strlcpy`, 또는 `buf[sizeof buf - 1] = '\0';` 추가 |
| `strncat(buf, s, sizeof buf)` | n 은 남은 공간이 아니라 "붙일 최대 글자 수", NUL 은 별도 (연습 3) | `strncat(buf, s, sizeof buf - strlen(buf) - 1)` 또는 `strlcat` |
| `if (lb->len < LINE_MAX) buf[len++] = c;` | 32자 줄에서 `buf[32] = 0` — 1바이트 넘침 | `len < LINE_MAX - 1` |
| 플래시 필드를 `strlen(r->name)` 으로 읽음 | 16자 꽉 찬 필드엔 NUL 없음 → 옆 `crc` 까지 읽음 | `strnlen(r->name, 16)` |
| `size - 1` 을 size=0 검사 없이 | `size_t` 0 - 1 = `SIZE_MAX` → 무제한 복사 | `if (size != 0)` 먼저 |

## 8. 면접 Q&A

<details>
<summary>Q1. strcpy 를 구현해 보세요. 반환값은 왜 dst 인가요? src 와 dst 가 겹치면?</summary>

`char *d = dst; while ((*d++ = *src++)) ; return dst;` 반환값은 `strcat(strcpy(b, x), y)` 같은 체이닝용(역사적 설계). 겹치면 UB(C11 7.24.2.3, 인자에 `restrict`) — 앞쪽으로 겹치면 아직 안 읽은 src를 덮어써 NUL 이 사라지고 무한 복사가 될 수 있다. 겹침 가능하면 `memmove(dst, src, strlen(src) + 1)`.

</details>

<details>
<summary>Q2. strcmp 는 왜 unsigned char 로 비교하나요? 반환값 1/-1 에 의존해도 되나요?</summary>

표준(7.24.4p1)이 그렇게 정의한다 — 바이트 값 0~255 순서가 사전순이 되게. plain `char` 는 signed/unsigned가 구현 정의라 (macOS·x86: signed, Cortex-M·aarch64 Linux: unsigned) char로 빼면 플랫폼마다 순서가 달라진다. 반환값은 부호만 보장 — 이 맥에서 `strcmp("\x80", "a")` 는 상수면 컴파일러가 접어 1, 런타임 호출이면 31(=0x80-0x61), `"a"` vs `"c"` 는 -2 였다. `== 1` / `== -1` 비교는 버그.

</details>

<details>
<summary>Q3. strncpy 는 안전한가요? 무엇을 대신 쓰나요?</summary>

아니다. ① 길이 ≥ n 이면 NUL 미종료 ② 짧으면 n 까지 0 패딩(느림) ③ 잘렸는지 알려주지 않음. 원래 고정 폭 필드용이다. C 문자열 복사엔 `strlcpy`(반환값 ≥ size 이면 잘림), `snprintf(dst, sz, "%s", src)`(반환 int ≥ sz 면 잘림), 커널은 `strscpy`(-E2BIG). `strlcpy` 는 BSD/macOS/newlib에 오래 있었고 glibc 2.38, POSIX.1-2024에 들어갔다. C23 표준에는 없다(C23은 `memccpy`, `strdup`, `strndup` 을 추가).

</details>

<details>
<summary>Q4. strlen 을 더 빠르게 만들 수 있나요? 그 방법은 왜 버퍼 끝을 넘어 읽어도 괜찮나요?</summary>

워드 단위(word-at-a-time): 8바이트씩 읽어 `(x - 0x0101…01) & ~x & 0x8080…80` 이 0이 아니면 그 워드에 0 바이트가 있다(SWAR). 정렬된 워드 읽기는 페이지 경계를 넘지 않으므로 실제로는 폴트가 안 나지만, C 추상 기계에서는 객체 밖 읽기라 **UB** 다. libc는 어셈블리/특수 처리로 하고, ASan은 libc strlen을 인터셉트해서 정확한 길이만 검사한다. 사용자 코드에서 흉내내면 ASan이 보고한다.

</details>

<details>
<summary>Q5. libc 함수를 직접 구현하는데 -O2 에서 무한 재귀가 났습니다. 왜죠?</summary>

컴파일러의 idiom recognition이 바이트 루프를 `strlen`/`memset`/`memcpy` 호출로 바꾼다(데모 3에서 `b _strlen` 확인). 그 함수 이름이 바로 `memset` 이면 자기 자신을 부른다. 해결: `-ffreestanding` 또는 `-fno-builtin`(GCC는 `-fno-tree-loop-distribute-patterns` 도). GCC 문서상 freestanding에서도 `memcpy/memmove/memset/memcmp` 는 컴파일러가 호출을 만들 수 있으니 반드시 제공해야 한다.

</details>

<details>
<summary>Q6. 왜 함수 이름에 my_ 접두사를 붙였나요? strlcpy2 같은 이름이면 안 되나요?</summary>

`str`, `mem`, `wcs` + 소문자로 시작하는 함수 이름은 `<string.h>` 의 미래 확장용으로 예약돼 있다 (C11 7.31.13 Future library directions — String handling). `strfoo` 를 외부 링크로 정의하면 미래 표준/ libc와 충돌할 수 있다. `my_`, `util_` 같은 접두사가 안전하다.

</details>

<details>
<summary>Q7. 펌웨어에서 UART 명령 버퍼 오버플로를 어떻게 막고, 어떻게 테스트하나요?</summary>

① 버퍼 크기를 상수 하나로 두고 저장 조건을 `len < N - 1` 로 (NUL 자리 예약) ② 넘친 줄은 버리고 오류 응답 ③ ISR/메인 공유는 더블 버퍼나 완성 플래그로 ④ 파서는 `strtok` 대신 재진입 가능한 제자리 분리. 테스트는 **호스트에서** 같은 C 코드를 Unity/Ceedling 등으로 컴파일해 정확히 N-1, N, N+1 길이 줄을 넣고 ASan/UBSan으로 돌린다 — 타깃 MCU에는 ASan이 없으니 로직을 하드웨어와 분리해 두는 설계가 핵심.

</details>

## 9. 추가 연습

### 연습 1 — strnlen

`size_t my_strnlen(const char *s, size_t maxlen)` — 최대 `maxlen` 바이트만 본다. NUL 없는 `char field[4] = {'A','B','C','D'}` 에서 4를 돌려줘야 한다.

<details>
<summary>답 보기</summary>

`while (n < maxlen && s[n]) n++;` — **조건 순서가 중요**: `n < maxlen` 을 먼저 검사해야 `s[maxlen]` 을 읽지 않는다 (`&&` 단락 평가). 고정 폭 필드(Part C)를 읽는 표준 도구 (POSIX).

</details>

### 연습 2 — strncmp 로 AT 명령 접두사 매칭

`my_strncmp(line, "AT+NAME=", 8) == 0` 이 되게 구현. unsigned char 비교, n=0 이면 0.

<details>
<summary>답 보기</summary>

n 번 돌면서 다르면 차이 반환, 같은데 NUL 이면 0 반환. `"ab"` vs `"abc"` 를 n=2로 비교하면 0, n=3이면 음수. 모뎀/BLE 모듈 AT 명령 파서, FreeRTOS+CLI의 명령 매칭이 이 패턴이다.

</details>

### 연습 3 — strncat 의 함정

`char buf[6] = "dev";` 에 `strncat(buf, "-01xyz", 2)` 하면 결과와 필요한 버퍼 크기는?

<details>
<summary>답 보기</summary>

`"dev-0"`, 필요한 크기 = 3 + 2 + 1 = **6**. strncat의 n은 "붙일 최대 글자 수" 이고 **NUL은 항상 추가로** 붙인다 (C11 7.24.3.2). 그래서 `strncat(buf, s, sizeof buf)` 는 넘친다. strncpy와 반대로 strncat은 항상 NUL 종료한다는 것도 헷갈리는 포인트.

</details>

### 연습 4 — strlcat

`size_t my_strlcat(char *dst, const char *src, size_t size)` — size는 dst **전체** 크기. 반환 = `strlen(초기 dst) + strlen(src)`. dst 앞 size 바이트 안에 NUL이 없으면 아무것도 쓰지 않고 `size + strlen(src)`.

<details>
<summary>답 보기</summary>

`dlen = strnlen(dst, size)` 로 dst를 size 안에서만 잰다 (연습 1 재사용). `dlen == size` 면 특수 반환. 아니면 `room = size - dlen - 1` 만큼 복사하고 NUL. macOS libc `strlcat` 과 4가지 경우를 바이트 단위로 대조한다.

</details>

### 연습 5 — strchr 와 '\0'

`my_strchr("NAME=sensor", '=')` 는 `+4`, `'\0'` 을 찾으면 종료 NUL 주소 `+11`, 없으면 NULL. `c` 는 `int` 로 받아 `char` 로 변환해 비교.

<details>
<summary>답 보기</summary>

루프에서 **먼저 일치 검사, 그 다음 NUL 검사** — 순서를 바꾸면 `'\0'` 검색이 NULL을 돌려준다. 반환형이 `char *` 라 `const` 를 떼야 하는 것은 C 표준 시그니처의 오래된 흠 (C23은 `strchr` 를 입력의 const 를 보존하는 제네릭으로 바꿨다).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice                     # 18/18 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_asan && ./practice_asan  # 18/18, 보고 없음
```

## 10. 핵심 정리

- C 문자열 = 첫 `'\0'` 까지. **필요 공간 = strlen + 1** — off-by-one의 근원.
- strcpy: 복사한 **다음에** 검사 (`while ((*d++ = *s++))`). 겹치면 UB → memmove.
- strcmp: **unsigned char** 로 비교, 반환값은 부호만 의미. char 부호는 플랫폼마다 다르다 (Cortex-M은 unsigned).
- strncpy: 정확히 n 바이트, 0 패딩, **길이 ≥ n 이면 NUL 없음**. 고정 폭 필드용이지 안전한 strcpy가 아니다.
- strlcpy: 항상 NUL(size>0), 반환 strlen(src), **반환 ≥ size → 잘림**. 단 src 전체를 읽는다 (커널은 strscpy).
- strncat의 n은 붙일 글자 수, NUL은 별도 +1.
- 버퍼는 NUL 자리를 예약 (`len < N - 1`), 고정 필드는 `strnlen` 으로 읽는다.
- 테스트는 딱 맞는 크기 버퍼 + ASan, 그리고 `-O0` 와 `-fno-builtin` 도 — 최적화가 버그와 내 코드를 숨긴다.

## 참고자료

- ISO/IEC 9899:2011 (C11) 7.1.1 문자열 정의, 7.24 `<string.h>` (7.24.2.3 strcpy, 7.24.2.4 strncpy, 7.24.3.2 strncat, 7.24.4 비교 함수, 7.24.6.3 strlen), 7.31.13 Future library directions (string.h) — [N1570 draft](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf)
- Todd C. Miller & Theo de Raadt, [*strlcpy and strlcat — consistent, safe, string copy and concatenation*](https://www.usenix.org/legacy/event/usenix99/full_papers/millert/millert.pdf) (USENIX 1999)
- [Linux kernel — Deprecated interfaces: strcpy / strncpy / strlcpy → strscpy](https://www.kernel.org/doc/html/latest/process/deprecated.html)
- [man7: strncpy(3) / string_copying(7)](https://man7.org/linux/man-pages/man7/string_copying.7.html)
- [Clang AddressSanitizer 문서](https://clang.llvm.org/docs/AddressSanitizer.html)
- [GCC — `-ffreestanding` 와 memcpy/memset 요구사항](https://gcc.gnu.org/onlinedocs/gcc/Standards.html)
- SEI CERT C: STR31-C (충분한 저장 공간), STR32-C (NUL 종료 보장)
