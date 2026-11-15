---
id: ALGO-B15
time: 60분
updated: 2026-11-11
---
# ALGO-B15 명령 문자열 파싱 기초: 토큰화와 검증

## 학습 목표

- `strtok` 없이 **제자리 토큰화**(in-place tokenizing)를 짠다: 공백·탭 여러 개, 줄 끝 CR/LF, 최대 줄 길이, 최대 토큰 수를 고정 크기 배열로 처리한다. `strtok` 이 왜 재진입 불가(non-reentrant)인지, `strtok_r` 이 왜 C11 이 아니라 POSIX 인지 말할 수 있다.
- `0x4000_0010` 처럼 **자리 구분자 `_`** 가 들어간 16진/10진 숫자를 엄격하게 파싱하고, "모양이 틀림(BADNUM)" 과 "범위 밖(RANGE)" 을 구분한다. `strtoul` 을 그대로 쓰면 생기는 함정(`-1` → `ULONG_MAX`, 앞 공백, `errno` 잔류, LP64 vs Cortex-M 의 `unsigned long` 폭)을 실측 출력으로 보여 준다.
- 명령 줄을 **태그 붙은 공용체(tagged union)** `cmd_t` 와 구분되는 에러 코드 10개로 바꾸고, 정상·비정상 입력 53개를 **표 기반 테스트(table-driven test)** 로 검증한다.
- UART RX ISR 이 1바이트씩 넣는 **줄 버퍼**(백스페이스, 넘친 줄 통째 폐기)와 태스크 문맥 파싱 사이의 소유권 넘기기(acquire/release)를 구현하고, 무작위 입력 루프 + ASan 으로 파서를 두드린다.

## 1. 문제

펌웨어 면접의 "문자열 문제" 는 거의 항상 **UART 디버그 콘솔 파서** 모양으로 나온다 — 짧은 코드 안에 경계 조건·정수 넘침·버퍼 넘침·ISR 경계가 다 들어 있어서, 면접관이 "이 사람이 현장 코드를 안전하게 짜는가" 를 15분 만에 볼 수 있기 때문이다.

보드에 붙은 디버그 UART 로 사람이(또는 테스트 스크립트가) 이런 줄을 보낸다.

```text
SET LED 3 ON
read 0x4000_0010 4
WRITE 0x4000_0020 0xDEAD_BEEF
HELP
```

파서는 이 줄을 **구조체 하나**로 바꾸거나, 정확한 이유와 함께 **거절**해야 한다. 반쯤 맞은 명령을 실행하는 콘솔은 현장에서 레지스터를 엉뚱한 값으로 덮어쓴다. C-M09 에서 `strtol` 자체를 구현하고 `SET <reg> <val>` 수준의 콘솔을 다뤘다면, 이 레슨은 그 위에서 **토큰화 → 문법 → 의미 검사** 를 나누는 법, 에러를 10가지로 구분해 표로 테스트하는 법, 그리고 ISR 에서 들어오는 바이트를 줄로 만드는 앞단까지 다룬다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b15a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">UART 콘솔 파이프라인: 바이트 → 줄 → 토큰 → cmd_t → 응답</text>
  <rect x="10" y="50" width="110" height="56" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="65" y="74" fill="currentColor" text-anchor="middle">UART RX</text>
  <text x="65" y="94" fill="currentColor" text-anchor="middle" font-size="12">1바이트씩</text>
  <rect x="150" y="50" width="150" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="225" y="72" fill="currentColor" text-anchor="middle" font-family="monospace">lb_rx_byte()</text>
  <text x="225" y="92" fill="currentColor" text-anchor="middle" font-size="12">ISR: O(1) 줄 편집</text>
  <rect x="330" y="50" width="120" height="56" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="390" y="72" fill="currentColor" text-anchor="middle">줄 버퍼 81B</text>
  <text x="390" y="92" fill="currentColor" text-anchor="middle" font-size="12">ready 플래그</text>
  <rect x="480" y="50" width="120" height="56" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="540" y="72" fill="currentColor" text-anchor="middle" font-family="monospace">lb_take()</text>
  <text x="540" y="92" fill="currentColor" text-anchor="middle" font-size="12">태스크 문맥</text>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <path d="M120 78 H148" marker-end="url(#b15a)"/><path d="M300 78 H328" marker-end="url(#b15a)"/>
    <path d="M450 78 H478" marker-end="url(#b15a)"/><path d="M540 106 V140" marker-end="url(#b15a)"/>
  </g>
  <line x1="465" y1="40" x2="465" y2="230" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="372" y="236" fill="currentColor" font-size="12">ISR 쪽</text>
  <text x="475" y="236" fill="currentColor" font-size="12">태스크 쪽</text>
  <rect x="480" y="142" width="250" height="78" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="490" y="162" fill="currentColor" font-family="monospace">cmd_parse(line, &amp;cmd)</text>
  <text x="490" y="182" fill="currentColor" font-size="12">A 토큰화 → B 숫자 → C 문법·의미</text>
  <text x="490" y="202" fill="currentColor" font-size="12">→ cmd_t 또는 ERR 코드 + 토큰 번호</text>
  <text x="10" y="150" fill="currentColor" font-family="monospace">"rea\bAD 0x4000_0010 4\r\n"</text>
  <text x="10" y="172" fill="currentColor">→ 줄 "reAD 0x4000_0010 4" (BS 가 a 를 지움)</text>
  <text x="10" y="194" fill="currentColor" font-family="monospace">→ {kind=READ, addr=0x40000010, len=4}</text>
  <text x="10" y="216" fill="currentColor" font-family="monospace">→ "OK READ 0x40000010 4"</text>
</svg>
```

문법 (키워드는 대소문자 무시, 구분자는 스페이스·탭 여러 개, 줄 끝 `\r`/`\n` 허용):

| 명령 | 인자 규칙 |
|---|---|
| `HELP` | 인자 없음 |
| `SET LED <n> ON\|OFF` | `n` = 0..7 (10진 또는 `0x`), 마지막은 `ON`/`OFF` |
| `READ <addr> <len>` | `addr`: 10진 또는 `0x` 16진, `_` 구분자 허용, **4바이트 정렬**. `len`: **10진만**, 1..64, 그리고 `addr + len − 1` 이 32비트를 넘으면 안 됨 |
| `WRITE <addr> <value>` | `addr` 위와 같음(정렬 포함), `value` 0..0xFFFFFFFF |

줄 전체 제한: 80글자 (`CMD_MAX_LINE`), 토큰 6개 (`CMD_MAX_TOKENS`). 힙 없음, 재귀 없음.

### Part A — 제자리 토큰화와 키워드 비교

- `cmd_tokenize(line, argv, max_tok, &argc)` — 줄 길이가 80을 넘으면 `CP_ERR_TOOLONG` (버퍼는 건드리지 않음), 토큰이 `max_tok` 보다 많으면 `CP_ERR_TOOMANY`. 그 밖엔 각 토큰 끝 구분자 자리에 `'\0'` 을 쓰고 `argv[k]` 가 **원래 버퍼 안의** 토큰 첫 글자를 가리키게 한다. `strtok` 금지.
- `kw_eq(tok, "READ")` — ASCII 만 접는 대소문자 무시 비교. `toupper` 쓰지 않음. `"REA"`, `"READX"` 는 다르다.
- 검증: 알파벳 `{a, B, ' ', '\t', '\n'}` 로 만든 길이 0~8 문자열 **488,281개 전부**를 테스트 쪽 참조 구현과 대조.

### Part B — 엄격한 숫자 파서

- `parse_u32(s, max, allow_hex, &out)` — 문자열 **전체**가 숫자여야 한다. 부호·공백 금지, `0x`/`0X` 는 `allow_hex` 일 때만, `_` 는 **숫자와 숫자 사이에만** (`_1`, `1_`, `1__0`, `0x_1` 은 BADNUM). 32비트를 넘거나 `max` 보다 크면 RANGE, 하지만 모양이 틀리면 넘쳤더라도 BADNUM. 성공했을 때만 `*out` 을 쓴다. `010` 은 10 (8진수 아님).
- 검증: 무작위 왕복 200,000개 (값 → 문자열 + 무작위 `_` → 파싱), 무작위 쓰레기 300,000개를 `_` 를 지우고 `strtoull` 을 쓰는 테스트 쪽 참조 구현과 대조.
- `demo_strtoul.c` 로 `strtoul` 의 함정을 이 맥에서 실측한다.

### Part C — 문법 → `cmd_t`, 그리고 ISR 줄 버퍼

- `cmd_parse(line, &cmd)` — 위 문법대로 `cmd_t` 를 채우거나 에러 코드 + 문제 토큰 번호(`err_tok`)를 돌려준다. 에러 우선순위는 5. 풀이의 ①~⑥.
- `lb_rx_byte(lb, c)` — UART RX ISR 이 바이트마다 부르는 줄 편집기. BS(0x08)/DEL(0x7F) 은 한 글자 지움, 제어 문자·8비트 바이트는 버림, 81번째 글자가 오면 **그 줄 전체를 다음 CR/LF 까지 버리고** `TOOLONG` 으로 알림. 태스크가 줄을 아직 안 가져갔으면 새 바이트는 버리고 센다.
- `lb_take(lb, dst, cap)` — 태스크가 줄을 꺼내고 버퍼를 ISR 에 돌려준다 (`ready` 플래그 acquire/release).
- 주어진 코드 `console_poll()` 이 이 둘을 엮어 `"OK READ 0x40000010 4"` / `"ERR BADNUM @2: malformed number"` 같은 응답을 만든다.

## 2. Problem (English)

> **Robust UART console command parser.**
>
> A debug console on a microcontroller receives ASCII lines over UART. Implement, in C11, without `strtok`, heap allocation or recursion:
>
> ```c
> cp_err_t cmd_tokenize(char *line, char *argv[], int max_tok, int *argc);
> bool     kw_eq(const char *tok, const char *kw);              /* kw is upper-case */
> cp_err_t parse_u32(const char *s, uint32_t max, bool allow_hex, uint32_t *out);
> cp_err_t cmd_parse(char *line, cmd_t *out);
> void        lb_rx_byte(linebuf_t *lb, uint8_t c);             /* called from the UART RX ISR */
> lb_status_t lb_take(linebuf_t *lb, char *dst, size_t cap);    /* called from task context   */
> ```
>
> Grammar (case-insensitive keywords, any run of spaces/tabs separates tokens, trailing CR/LF allowed):
> `HELP` | `SET LED <0..7> ON|OFF` | `READ <addr> <len 1..64, decimal>` | `WRITE <addr> <u32>`.
> Numbers are decimal or `0x` hex with optional `_` separators between digits. `addr` must be 4-byte aligned and `addr + len - 1` must not exceed `0xFFFFFFFF`.
>
> Constraints: line ≤ 80 chars, ≤ 6 tokens. On error return a distinct code (`EMPTY, TOOLONG, TOOMANY, UNKNOWN, MISSING, EXTRA, BADNUM, RANGE, ALIGN, BADARG`) and the index of the offending token.
>
> | input | result |
> |---|---|
> | `"  SeT\tLeD   0  oN  \r\n"` | `OK, SET_LED{idx=0, on=1}` |
> | `"READ 0x4000_0010 4"` | `OK, READ{addr=0x40000010, len=4}` |
> | `"READ 0x4000_0011 4"` | `ALIGN @1` |
> | `"READ 0xFFFF_FFFC 8"` | `RANGE @2` (wraps past 4 GiB) |
> | `"SET LED -1 ON"` | `BADNUM @2` |
> | `"READ 99999999999z 4"` | `BADNUM @1` (shape error beats overflow) |
> | `"HELP me"` | `EXTRA @1` |
>
> Follow-ups: Why not `strtok`? Why not `strtoul`? What runs in the ISR and what in the task? How would you fuzz this?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨진 맥
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 이 쭉 나오고 "16/103 passed", exit 1 — TODO 6개를 채워 103/103 을 만든다
```

TODO 는 `cmd_tokenize`, `kw_eq`, `parse_u32`, `cmd_parse`, `lb_rx_byte`, `lb_take` 6개다. `is_sep`, `ascii_upper`, `digit_of`, `fail`, `cmd_format`, `console_poll`, 에러 이름/메시지 표는 주어진다.

## 4. 힌트

<details>
<summary>힌트 1 — 토큰화는 "건너뛰기 → 표시 → 달리기 → 끊기" 네 동작의 반복</summary>

포인터 `p` 하나로: ① 구분자를 건너뛴다 ② `*p == '\0'` 이면 끝 ③ 토큰 수가 꽉 찼으면 TOOMANY, 아니면 `argv[k++] = p` ④ 구분자나 NUL 이 나올 때까지 달린다 ⑤ 구분자였다면 `*p++ = '\0'`. 길이 검사는 그 전에, **최대 81글자까지만** 세고 멈춘다.

</details>

<details>
<summary>힌트 2 — 키워드 비교의 마지막 줄</summary>

글자별 비교 루프가 끝났을 때 **둘 다** `'\0'` 이어야 같다. 하나만 끝났으면 접두사 관계다 (`"REA"` vs `"READ"`). `strncmp(tok, "READ", 4)` 가 왜 틀렸는지 여기서 보인다.

</details>

<details>
<summary>힌트 3 — 넘침은 곱하기 "전에"</summary>

`cutoff = UINT32_MAX / base`, `cutlim = UINT32_MAX % base` 를 한 번 계산하고, `acc > cutoff || (acc == cutoff && d > cutlim)` 이면 다음 `acc * base + d` 가 넘친다 (C-M09 와 같은 방법). 넘쳐도 **루프는 끝까지** 돈다 — 뒤에 `z` 가 있으면 RANGE 가 아니라 BADNUM 이어야 하니까.

</details>

<details>
<summary>힌트 4 — '_' 규칙을 한 줄로</summary>

`_` 를 만났을 때 "직전 글자가 숫자였다" 와 "다음 글자가 이 base 의 숫자다" 가 둘 다 참이어야 한다. `p[1]` 은 기껏해야 종단 NUL 이라 읽어도 안전하다. `0x_1` 은 직전 글자가 `x` 라서 걸린다.

</details>

<details>
<summary>힌트 5 — 줄 버퍼의 상태는 3개</summary>

평상시(글자 쌓기) / 버리는 중(넘친 줄, CR/LF 를 기다림) / ready(태스크 소유, ISR 은 손대지 않음). `ready` 는 `atomic_bool` 로, ISR 은 줄을 다 쓴 **다음에** release 로 세우고, 태스크는 acquire 로 읽은 뒤 복사하고 마지막에 release 로 내린다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 (이 문제) |
|---|---|
| 1. 질문으로 범위 확정 | 최대 줄 길이? 대소문자? 숫자 형식(16진, `_`, 음수)? 에러를 몇 가지로 구분? 입력 버퍼를 고쳐도 되나? 힙 써도 되나? |
| 2. 무식한 방법 + 복잡도 | `strtok` + `strcmp` + `strtoul`. O(L). 문제: 재진입 불가, 접두사 비교 실수, `strtoul` 이 `-1`·공백·`+` 를 받아 줌 |
| 3. 더 나은 설계 + 불변식 | 3단 분리: 토큰화(위치만) → 토큰별 모양·범위 → 명령 전체 의미. 불변식: "`argv[0..k)` 는 원래 버퍼 안의 NUL 종단 비어 있지 않은 토큰" |
| 4. 코드 | 토큰화 → 숫자 → 문법 순서로, 각 함수는 길이 제한이 있는 루프 하나 |
| 5. 소리 내어 말할 테스트 | 빈 줄, 공백뿐, 탭·CRLF, 정확히 80/81글자, 6/7토큰, `0x`, `-1`, `4294967295`/`4294967296`, `0xFFFF_FFFC 8`, `READX`, 인자 부족·초과 |
| 6. 복잡도 | 시간 O(L) (L ≤ 80, 각 글자를 상수 번 봄), 추가 메모리 O(T) 포인터 (T = 6), 재귀 없음. Cortex-M4 실측 스택 92바이트 (아래) |

### Part A — 제자리 토큰화

**불변식**: 루프의 매 반복 시작에서 `argv[0..k)` 는 `line` 안의 서로 겹치지 않는 NUL 종단 토큰이고, `p` 는 아직 보지 않은 첫 글자를 가리킨다. 각 글자는 "건너뛰기" 나 "달리기" 중 정확히 한 번만 지나가므로 O(L) 이다.

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">cmd_tokenize("  READ\t0x10   4 \r\n") — 토큰 끝 구분자 3곳만 '\0' 으로 바뀐다</text>
  <text x="10" y="62" fill="currentColor">전</text>
  <text x="10" y="122" fill="currentColor">후</text>
  <g stroke="currentColor" fill="none">
    <rect x="40" y="44" width="646" height="28"/><rect x="40" y="104" width="646" height="28"/>
  </g>
  <g class="f-acc" stroke="currentColor">
    <rect x="108" y="104" width="136" height="28"/><rect x="278" y="104" width="136" height="28"/><rect x="516" y="104" width="34" height="28"/>
  </g>
  <g class="f-warn" stroke="currentColor">
    <rect x="244" y="104" width="34" height="28"/><rect x="414" y="104" width="34" height="28"/><rect x="550" y="104" width="34" height="28"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="57" y="63">·</text><text x="91" y="63">·</text><text x="125" y="63">R</text><text x="159" y="63">E</text><text x="193" y="63">A</text><text x="227" y="63">D</text><text x="261" y="63">\t</text><text x="295" y="63">0</text><text x="329" y="63">x</text><text x="363" y="63">1</text><text x="397" y="63">0</text><text x="431" y="63">·</text><text x="465" y="63">·</text><text x="499" y="63">·</text><text x="533" y="63">4</text><text x="567" y="63">·</text><text x="601" y="63">\r</text><text x="635" y="63">\n</text><text x="669" y="63">\0</text>
    <text x="57" y="123">·</text><text x="91" y="123">·</text><text x="125" y="123">R</text><text x="159" y="123">E</text><text x="193" y="123">A</text><text x="227" y="123">D</text><text x="261" y="123">\0</text><text x="295" y="123">0</text><text x="329" y="123">x</text><text x="363" y="123">1</text><text x="397" y="123">0</text><text x="431" y="123">\0</text><text x="465" y="123">·</text><text x="499" y="123">·</text><text x="533" y="123">4</text><text x="567" y="123">\0</text><text x="601" y="123">\r</text><text x="635" y="123">\n</text><text x="669" y="123">\0</text>
    <text x="57" y="88" font-size="12">0</text><text x="125" y="88" font-size="12">2</text><text x="261" y="88" font-size="12">6</text><text x="295" y="88" font-size="12">7</text><text x="431" y="88" font-size="12">11</text><text x="533" y="88" font-size="12">14</text><text x="567" y="88" font-size="12">15</text><text x="669" y="88" font-size="12">18</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <path d="M125 165 V136"/><path d="M295 165 V136"/><path d="M533 165 V136"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="90" y="182">argv[0]=+2</text><text x="260" y="182">argv[1]=+7</text><text x="498" y="182">argv[2]=+14</text>
  </g>
  <text x="10" y="212" fill="currentColor">(· = 공백) 앞 공백·연속 공백·뒤 \r\n 은 "건너뛰기" 에서 지나갈 뿐 아무것도 쓰지 않는다. 복사 0바이트, argc = 3.</text>
</svg>
```

| 단계 | `p` 위치 | 동작 | `k` |
|---|---|---|---|
| 1 | 0 → 2 | 공백 2개 건너뛰기 | 0 |
| 2 | 2 | `argv[0] = +2`, `R E A D` 달리기 → 6 (`\t`) | 1 |
| 3 | 6 | `\t` → `'\0'`, `p = 7` | 1 |
| 4 | 7 | 구분자 없음, `argv[1] = +7`, 달리기 → 11 | 2 |
| 5 | 11 → 14 | `'\0'` 쓰고, 공백 2개 건너뛰기 | 2 |
| 6 | 14 | `argv[2] = +14`, 달리기 → 15, `'\0'`, `p = 16` | 3 |
| 7 | 16 → 18 | `\r\n` 건너뛰기, NUL → 끝 | 3 |

설계 포인트:

- **제자리(in-place)**: 토큰을 복사하지 않으니 토큰 배열이 포인터 6개(Cortex-M 에선 24바이트)면 된다. 대가는 입력 버퍼를 고친다는 것 — 그래서 태스크는 ISR 버퍼가 아니라 **자기 스택의 복사본**(`console_poll` 의 `line[81]`)을 넘긴다. 입력을 못 고치는 상황이면 C-M09 처럼 `(시작, 길이)` 쌍을 돌려주는 `const` 판을 쓴다.
- **길이 검사는 상한까지만 센다**: `strlen` 은 NUL 이 없으면 끝없이 읽는다. 81번째 글자에서 멈추면 최악의 경우도 81번 읽기다. (`strnlen` 은 POSIX 이고 C11 에는 없다.)
- **TOOMANY 와 EXTRA 를 나눈 이유**: 토큰화는 문법을 모른다. "배열이 넘친다" (TOOMANY, 토큰 7개 이상) 와 "이 명령엔 인자가 남는다" (EXTRA, `HELP me`) 는 다른 층의 에러라서 다른 코드로 보고한다.
- **`strtok` 을 안 쓰는 이유**: `strtok` 은 "다음에 어디서부터 자를지" 를 **함수 안의 숨은 정적 상태**에 기억한다. 콘솔 태스크가 토큰을 자르는 도중 다른 태스크(예: 설정 파일 파서)가 `strtok` 을 부르면 콘솔 쪽 상태가 덮어써진다. C11 7.24.5.8 은 `strtok` 이 다른 `strtok` 호출과의 데이터 경쟁을 피할 의무가 없다고 적어 두었다. 상태를 호출자에게 넘기는 `strtok_r` 은 **POSIX** 함수다 (이 맥의 `<string.h>` 는 `-std=c11` 에서도 선언을 보여 주지만, glibc 는 `-std=c11` 이면 `_POSIX_C_SOURCE` 를 정의해야 보인다). C11 Annex K 의 `strtok_s` 는 선택 사양이라 glibc 도, 이 맥의 헤더도 제공하지 않는다. 우리 토큰화는 상태가 `p` 하나뿐이고 지역 변수라 처음부터 재진입 가능하다.
- **`kw_eq` 를 직접 쓰는 이유**: `toupper(c)` 는 `c` 가 `unsigned char` 값이나 `EOF` 가 아니면 UB 다 (C11 7.4p1) — `char` 가 부호 있는 플랫폼에서 `0xC2` 같은 UTF-8 바이트가 음수로 들어간다. 또 현재 로캘(`LC_CTYPE`)에 따라 결과가 달라진다. 명령어는 ASCII 로 고정이니 `'a'..'z'` 만 접는 한 줄이 가장 안전하고 작다 (아래 Cortex-M4 실측 54바이트).

### Part B — 엄격한 `parse_u32`

규칙을 한 문장으로: **`[0x]` 다음에 "숫자 (`_`? 숫자)*" 가 정확히 문자열 끝까지**. 상태는 `acc`, `over`(이미 넘쳤나), `prev_digit`(직전 글자가 숫자인가), `ndig` 4개다.

| 입력 `"0x4000_0010"` | 동작 | `acc` | `prev_digit` |
|---|---|---|---|
| `0x` | 접두사 → base 16, cutoff = `0x0FFFFFFF`, cutlim = 15 | 0 | F |
| `4` `0` `0` `0` | 숫자 4개 | `0x4000` | T |
| `_` | 직전 숫자 ✓, 다음 `'0'` 숫자 ✓ → 통과 | `0x4000` | F |
| `0` `0` `1` `0` | 숫자 4개 | `0x40000010` | T |
| 끝 | `ndig = 8 > 0`, 넘침 없음, `≤ max` → OK | | |

| 입력 `"4294967296"` (base 10) | cutoff = 429,496,729, cutlim = 5 |
|---|---|
| 앞 9자리 후 | `acc = 429,496,729 == cutoff` |
| 마지막 `6` | `6 > cutlim` → `over = true` (곱하지 않음) |
| 끝 | 모양은 정상 → **RANGE** |

**모양 에러가 넘침보다 우선**: `"99999999999z"` 는 중간에 넘치지만 `z` 에서 BADNUM 으로 끝난다. 사용자에게 "숫자가 크다" 고 하면 거짓말이다 — 숫자가 아니니까.

**왜 `strtoul` 을 안 쓰나** — `demo_strtoul.c` 실측 (이 맥, Apple clang, LP64):

```text
sizeof(unsigned long) = 8, ULONG_MAX = 18446744073709551615
  -1                         base  0 -> 18446744073709551615 (0xffffffffffffffff)  end=+2  ''  errno=0
                             base  0 ->                    0 (0x0000000000000000)  end=+0  ''  errno=EINVAL
     42                      base  0 ->                   42 (0x000000000000002a)  end=+5  ''  errno=0
  +42                        base  0 ->                   42 (0x000000000000002a)  end=+3  ''  errno=0
  42abc                      base  0 ->                   42 (0x000000000000002a)  end=+2  'abc'  errno=0
  0x                         base  0 ->                    0 (0x0000000000000000)  end=+1  'x'  errno=0
  010                        base  0 ->                    8 (0x0000000000000008)  end=+3  ''  errno=0
  0x4000_0010                base  0 ->                16384 (0x0000000000004000)  end=+6  '_0010'  errno=0
  4294967296                 base  0 ->           4294967296 (0x0000000100000000)  end=+10 ''  errno=0
  99999999999999999999999    base  0 -> 18446744073709551615 (0xffffffffffffffff)  end=+23 ''  errno=ERANGE
stale errno: strtoul("7") = 7 but errno is still ERANGE
```

(출력 일부. 전체는 `./demo_strtoul`.) 읽는 법:

- **`-1` 이 에러가 아니다**: 표준은 "부호가 `-` 면 변환된 값을 반환 타입에서 부정" (C11 7.22.1.4p5) 하라고 하므로 `ULONG_MAX`, `errno` 0. `READ 0x10 -1` 이 "64바이트 초과" 가 아니라 "엄청 큰 len" 으로 들어온다.
- **앞 공백·`+` 허용**, **찌꺼기는 `end` 로만** 알 수 있고, **빈 문자열**은 macOS libc 에서 `EINVAL` 이지만 glibc 는 `errno` 를 안 바꾼다 — 판정은 `end == s` 로 해야 이식성이 있다.
- **`errno` 는 성공해도 안 지워진다** (C11 7.5p3): 마지막 줄처럼 `errno = 0` 을 빼먹으면 정상 값 7 이 ERANGE 로 보인다.
- **`unsigned long` 폭**: 이 맥은 8바이트라 `4294967296` 이 **정상 변환**된다. Cortex-M 은 다르다 — `clang --target=thumbv7em-none-eabi -dM -E` 실측: `__SIZEOF_LONG__ 4`, `__LONG_MAX__ 2147483647L`. 같은 소스가 맥에선 "`v > UINT32_MAX` 검사" 로, 보드에선 "ERANGE" 로 걸린다. 둘 다 검사해야 양쪽에서 맞다.
- **`_` 를 모른다**, base 0 에서 `010` 은 8진수 8.

그래서 `strtoul` 을 쓰려면 "앞에서 문자 집합을 직접 검사 → `errno = 0` → `end` 검사 → ERANGE → 도메인 범위" 를 다 해야 한다 (추가 연습 4). 그 정도면 20줄짜리 직접 파서가 더 짧고, 동작이 표준 라이브러리 구현(newlib/glibc/macOS)과 무관해진다.

### Part C — 문법: 검사 순서가 곧 에러 우선순위

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b15b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">cmd_parse 검사 순서 — 먼저 걸리는 것이 보고된다</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="160" height="48" rx="6" class="f-mut"/>
    <rect x="190" y="40" width="120" height="48" rx="6" class="f-mut"/>
    <rect x="330" y="40" width="130" height="48" rx="6" class="f-mut"/>
    <rect x="480" y="40" width="250" height="48" rx="6" class="f-acc"/>
    <rect x="480" y="120" width="250" height="48" rx="6" class="f-mut"/>
    <rect x="200" y="120" width="260" height="48" rx="6" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="60">① 길이·토큰 수</text><text x="20" y="78">TOOLONG / TOOMANY</text>
    <text x="200" y="60">② 토큰 0개?</text><text x="200" y="78">EMPTY</text>
    <text x="340" y="60">③ argv[0] 명령어</text><text x="340" y="78">UNKNOWN @0</text>
    <text x="490" y="60">④ 인자 i = 1, 2, ... 왼쪽부터</text><text x="490" y="78">MISSING / BADNUM·BADARG / RANGE</text>
    <text x="490" y="140">⑤ argc &gt; 필요 개수?</text><text x="490" y="158">EXTRA @need</text>
    <text x="210" y="140">⑥ 명령 전체 의미 (토큰 사이 관계)</text><text x="210" y="158">ALIGN @1 · addr+len 넘침 RANGE @2</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <path d="M170 64 H188" marker-end="url(#b15b)"/><path d="M310 64 H328" marker-end="url(#b15b)"/>
    <path d="M460 64 H478" marker-end="url(#b15b)"/><path d="M605 88 V118" marker-end="url(#b15b)"/>
    <path d="M480 144 H462" marker-end="url(#b15b)"/><path d="M200 144 H120 V190" marker-end="url(#b15b)"/>
  </g>
  <text x="60" y="210" fill="currentColor" font-weight="bold">OK → cmd_t</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="200" y="200">"READ 0x11"          → MISSING @2 (ALIGN 은 ⑥이라 아직)</text>
    <text x="200" y="220">"READ 0x11 4 junk"   → EXTRA @3   (⑤ 가 ⑥ 보다 먼저)</text>
    <text x="200" y="240">"READ 0x10 0 junk"   → RANGE @2   (④ 가 ⑤ 보다 먼저)</text>
    <text x="200" y="260">"READ 99999999999z 4" → BADNUM @1 (모양이 넘침보다 먼저)</text>
  </g>
</svg>
```

원칙: **문법(syntax) 에러를 먼저, 의미(semantic) 에러를 나중에**. 정렬이나 `addr + len` 넘침은 "토큰들이 각자 멀쩡한데 조합이 안 된다" 는 에러라서, 사용자가 인자를 빠뜨렸거나 오타를 냈다면 그쪽을 먼저 알려 주는 편이 고치기 쉽다. 어떤 순서든 **정하고, 문서화하고, 표로 테스트** 하는 것이 핵심이다 — 테스트 표의 마지막 네 줄이 이 순서를 못 박는다.

**태그 붙은 공용체**: `cmd_t` 는 `kind` (태그) + `union u` 다. `READ` 의 `addr/len` 과 `WRITE` 의 `addr/value` 와 `LED` 의 `idx/on` 은 동시에 쓰이지 않으니 겹쳐 두면 16바이트(호스트 실측 `sizeof(cmd_t)=16`). 읽는 쪽은 반드시 `switch (c->kind)` 로 해당 멤버만 읽는다. 에러면 `kind = CMD_NONE` 이고 union 은 0 — `fail()` 이 지워 주므로, 반쯤 채운 값이 밖으로 새지 않는다.

**`addr + len − 1` 넘침을 64비트 없이**: `addr + len - 1 > UINT32_MAX` 는 32비트에선 그 덧셈이 이미 넘쳐 의미가 없다. 양변에서 `addr` 를 옮기면 `len - 1 > UINT32_MAX - addr` — 둘 다 넘칠 수 없는 식이다 (`len ≥ 1`, `addr ≤ UINT32_MAX`). `0xFFFFFFC0 + 64 − 1 = 0xFFFFFFFF` 는 OK, `0xFFFFFFFC + 8` 은 RANGE (테스트 표에 둘 다 있음).

**표 기반 테스트**: `k_cases[]` 53줄 = 정상 14 + 비정상 39. 한 줄이 입력, 기대 에러, 기대 `err_tok`, 기대 `kind`, 기대 필드 2개다. 실패하면 그 줄의 입력과 기대/실제를 다 찍는다. 새 버그가 보고되면 **표에 한 줄 추가** 가 회귀 테스트(regression test)가 된다.

### Part C — ISR 줄 버퍼

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b15c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">lb_rx_byte 의 상태 3개 (ISR 은 바이트당 O(1))</text>
  <rect x="40" y="80" width="170" height="64" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="125" y="106" fill="currentColor" text-anchor="middle" font-weight="bold">COLLECT</text>
  <text x="125" y="126" fill="currentColor" text-anchor="middle" font-size="12">글자 쌓기 · BS 지우기</text>
  <rect x="300" y="170" width="170" height="64" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="385" y="196" fill="currentColor" text-anchor="middle" font-weight="bold">DISCARD</text>
  <text x="385" y="216" fill="currentColor" text-anchor="middle" font-size="12">CR/LF 까지 전부 버림</text>
  <rect x="540" y="80" width="170" height="64" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="625" y="106" fill="currentColor" text-anchor="middle" font-weight="bold">READY</text>
  <text x="625" y="126" fill="currentColor" text-anchor="middle" font-size="12">태스크 소유, 새 바이트 버림</text>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <path d="M210 98 H538" marker-end="url(#b15c)"/>
    <path d="M538 128 H230 L212 128" marker-end="url(#b15c)"/>
    <path d="M150 144 L300 192" marker-end="url(#b15c)"/>
    <path d="M470 196 L590 146" marker-end="url(#b15c)"/>
    <path d="M70 80 C 50 40, 110 40, 100 78" marker-end="url(#b15c)"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="260" y="90">CR/LF (len &gt; 0) → ready (release)</text>
    <text x="250" y="146">lb_take: 복사 후 ready=false(release)</text>
    <text x="90" y="186">81번째 글자</text>
    <text x="520" y="214">CR/LF → too_long, ready</text>
    <text x="20" y="38">출력 가능 글자 → buf[len++]</text>
  </g>
  <text x="10" y="252" fill="currentColor" font-size="12">CR/LF 인데 len == 0 (빈 줄, CRLF 의 LF) → 무시. 제어 문자·0x80 이상 → junk 로 세고 버림 (NUL 이 줄에 들어가지 않게).</text>
</svg>
```

- **ISR 에선 줄 편집만, 파싱은 태스크에서**: `lb_rx_byte` 는 분기 몇 개와 저장 하나다 — 루프도, 라이브러리 호출도 없다 (Cortex-M4 `-Os` 실측 160바이트, 스택 0). 파싱(최대 80글자 루프)과 특히 **명령 실행**(플래시 쓰기, 레지스터 접근, `snprintf` 응답)은 태스크 문맥에서 한다. ISR 이 길어지면 같은 우선순위 이하의 모든 인터럽트 지연이 늘어난다 (BM-M04, DS-A02 참고).
- **넘친 줄은 잘라서 실행하지 말고 통째로 버린다**: 잘라 쓰면 `WRITE 0x4000_0000 0x1234_5678` 이 `WRITE 0x4000_0000 0x12` 로 **다른 유효한 명령**이 될 수 있다. 그래서 81번째 글자에서 DISCARD 로 가고, CR/LF 에서 `too_long` 표시와 함께 태스크에 알려 `ERR TOOLONG` 을 돌려준다.
- **소유권 플래그**: `ready == true` 동안 `buf`/`len` 은 태스크 것이다. ISR 은 줄을 다 쓴 **뒤** `ready` 를 release 로 세우고, 태스크는 acquire 로 읽은 **뒤** 복사한다. 복사가 끝나기 전에 `ready = false` 를 쓰면 ISR 이 다음 바이트로 버퍼를 덮어쓰는 중에 태스크가 읽는다. Cortex-M4 로 컴파일한 실측 어셈블리에서 acquire 로드는 `ldrb` 다음 `dmb sy`, release 스토어는 `dmb sy` 다음 `strb` 로 나왔다. 그냥 `bool` 이면 컴파일러가 태스크 루프의 `ready` 읽기를 루프 밖으로 끌어내 영원히 기다릴 수 있다.
- **CRLF**: CR 에서 줄이 완성되고, 바로 뒤 LF 는 READY 중이라 `dropped_bytes` 로 세어지거나 (태스크가 아직 안 가져감) `len == 0` 이라 무시된다 (이미 가져감). 어느 쪽이든 빈 줄이 생기지 않는다.
- **트레이드오프**: 줄 하나짜리 버퍼라 태스크가 늦으면 다음 줄이 버려진다 (테스트에서 `HELP\n` 5바이트가 버려지는 것을 확인). 붙여넣기 대량 입력이 필요하면 ISR → 바이트 링 버퍼 → 태스크에서 줄 편집 (DS-B04, DS-A02) 으로 바꾼다. 바이트를 하나씩 받으며 프레임 구조를 상태 기계로 따라가는 스트림 파서는 ALGO-M12 (NMEA 문장과 체크섬) 에서 다룬다.

### 측정 — Cortex-M4 코드 크기와 스택 (`demo_m4.c`)

`clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -Os -fstack-usage -c demo_m4.c` (컴파일만, 맥 SDK 헤더를 빌려 씀. 링크하지 않으므로 libc 는 `memcpy` 참조만 남음) 실측:

| 함수 (`-Os`) | 코드 (바이트) | 스택 (바이트, `.su`) |
|---|---|---|
| `cmd_parse` (토큰화·`fail` 등 인라인 포함) | 872 | 64 |
| `parse_u32` | 284 | 28 |
| `kw_eq` | 54 | 0 |
| `lb_rx_byte` (ISR) | 160 | 0 |
| `lb_take` | 84 | 24 |
| `.text` 합계 | 1,454 | 최악 경로 `cmd_parse → parse_u32` = 92 |

같은 파일을 `-O2` 로 컴파일하면 `.text` 가 6,112바이트로 4배 넘게 커진다 (인라인·펼치기). 콘솔은 빠를 필요가 없는 코드라 `-Os` 가 맞다. RAM 은 `linebuf_t` 96바이트(호스트 실측, `atomic_bool` 정렬 포함) + 태스크 스택의 줄 복사본 81바이트 + `cmd_t` 16바이트.

### 퍼징 — 무작위 입력 루프 + ASan (`demo_fuzz.c`)

libFuzzer 를 쓰려고 이 맥에서 `cc -fsanitize=fuzzer` 를 해 보면 링크가 실패한다: `ld: library '.../clang/21/lib/darwin/libclang_rt.fuzzer_osx.a' not found` — Apple clang 은 libFuzzer 런타임을 싣지 않는다. 그래서 고정 시드 생성기 3종 × 300,000개를 `-fsanitize=address,undefined` 로 돌리며 불변식(에러면 `kind == NONE`, 성공이면 필드가 범위 안, 같은 입력 두 번 → 같은 결과, 성공 결과를 정규형으로 다시 써서 파싱 → 같은 구조체)을 검사했다. 실측 출력:

```text
random bytes  : EMPTY=3106 TOOLONG=57340 TOOMANY=37 UNKNOWN=239517
token soup    : OK=1162 EMPTY=37455 TOOLONG=3 TOOMANY=37479 UNKNOWN=186526 MISSING=6460 EXTRA=5892 BADNUM=13189 RANGE=1500 ALIGN=84 BADARG=10250
mutated seeds : OK=15472 EMPTY=204 UNKNOWN=181119 MISSING=825 EXTRA=200 BADNUM=75507 RANGE=5255 ALIGN=1683 BADARG=19735
linebuf       : lines=55447 toolong=2 dropped_bytes=385528 junk_bytes=921742 bad=0
parse invariant violations: 0  (300000 inputs x 3 modes, 0.27 s CPU)
```

- **완전 무작위 바이트는 30만 번 중 한 번도 OK 에 닿지 못했다** — 80% 가 첫 토큰에서 UNKNOWN. 깊은 코드(숫자 파서, 정렬 검사)를 두드리려면 사전 단어 조합(token soup)이나 정상 명령 변이(mutation)가 필요하다. libFuzzer 같은 커버리지 기반 퍼저는 이 "깊이 들어가는 입력" 을 자동으로 키운다.
- 퍼저가 실제로 무엇을 잡는지 보려고 `solution.c` 를 한 줄씩 망가뜨려 같은 퍼저를 돌렸다 (실측):

| 심은 버그 | 퍼저 결과 |
|---|---|
| `if (k == max_tok)` → `k > max_tok` (argv 한 칸 넘침) | ASan `stack-buffer-overflow` 로 즉시 중단 |
| `addr & 3u` → `addr & 1u` | 불변식 위반 972건 |
| `addr + len` 넘침 검사 삭제 | 불변식 위반 52건 |
| 줄 버퍼 상한 `CMD_MAX_LINE` → `+1` | 81글자 줄이 나와 linebuf 불변식 실패 |
| 넘침 검사(cutoff) 삭제 → 값이 조용히 wrap | **못 잡음** (값이 틀려도 불변식은 만족) |
| `kw_eq` 를 접두사 비교로 (`READX` 허용) | **못 잡음** |

퍼저는 **크래시와 불변식 위반**을 잡고, "값이 맞는가" 는 정답을 아는 테스트(표, 참조 구현과의 차분 비교)가 잡는다. 둘은 대체 관계가 아니다. 같은 망가뜨리기를 `solution.c` 의 테스트(ASan 빌드)에 돌리면 위 6개가 모두 잡힌다 (FAIL 또는 ASan 중단, 실측).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
# parse table: 53/53 cases ok
# 103/103 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_asan && ./solution_asan   # 깨끗
cc -std=c11 -Wall -Wextra -Werror -O2 demo_strtoul.c -o demo_strtoul && ./demo_strtoul
cc -std=c11 -g -O1 -fsanitize=address,undefined demo_fuzz.c -o demo_fuzz && ./demo_fuzz
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `strncmp(tok, "READ", 4) == 0` | `"READX"`, `"READ123"` 도 통과 (접두사 비교) | `kw_eq` 처럼 **둘 다 동시에 끝나는지** 확인 |
| `toupper(*p)` (`char *p`) | 음수 `char` 는 UB (7.4p1), 로캘 의존 | ASCII 만 접는 `ascii_upper`, 꼭 쓰면 `toupper((unsigned char)*p)` |
| 콘솔 태스크와 다른 태스크가 둘 다 `strtok` | 숨은 정적 상태를 서로 덮어씀 | 상태가 지역 변수인 직접 토큰화 (또는 POSIX `strtok_r`) |
| `v = strtoul(t, NULL, 0)` 하나로 끝 | `-1`→`ULONG_MAX`, `" 4"`·`"+4"` 허용, `"4abc"`→4, `""`→0, `010`→8 | 문자 집합 직접 검사 → `errno = 0` → `end` → ERANGE → 범위 (연습 4) |
| `n = acc*10 + d; if (n < acc) overflow;` | 곱셈 wrap 은 작아진다는 보장이 없다. 실측: `"9999999999"` → 1,410,065,407 (> 999,999,999) 로 **검출 실패** | 곱하기 전에 `acc > cutoff \|\| (acc == cutoff && d > cutlim)` |
| 넘침을 보자마자 `return RANGE` | `"99999999999z"` 가 BADNUM 이 아니라 RANGE 가 됨 | `over` 플래그만 세우고 끝까지 모양 검사 |
| `_` 를 그냥 건너뜀 | `"_"`, `"0x_"`, `"1__"` 이 0 이나 1 로 통과 | 직전 숫자 + 다음 숫자 둘 다 확인, `ndig == 0` 이면 BADNUM |
| `if (addr + len - 1 > 0xFFFFFFFF)` (`uint32_t`) | 좌변이 이미 wrap 돼 항상 거짓 | `len - 1 > UINT32_MAX - addr` |
| ISR: `buf[len++] = c;` 상한 없음 | 긴 줄 하나로 버퍼 뒤 변수(`len` 자체!)를 덮어씀 | `len == CMD_MAX_LINE` 이면 DISCARD |
| 넘친 줄을 잘라서 실행 | 잘린 명령이 **다른 유효한 명령**이 됨 | 줄 전체 폐기 + `ERR TOOLONG` |
| BS 처리 `lb->len--;` | 빈 줄에서 `uint16_t` 가 65535 로 wrap → 다음 저장이 버퍼 밖 (실측 UBSan: `index 65533 out of bounds for type 'char[81]'`) | `if (lb->len > 0) lb->len--;` |
| `bool ready;` (atomic/volatile 아님) | 컴파일러가 태스크 루프의 읽기를 끌어올림, 복사·플래그 순서도 보장 안 됨 | `atomic_bool` + release/acquire |

## 8. 면접 Q&A

<details>
<summary>Q1. strtok 을 쓰면 안 되나요? strtok_r 은요?</summary>

`strtok` 은 다음 자를 위치를 함수 안의 정적 상태에 둔다. 그래서 ① 두 태스크가 동시에 쓰면 서로의 상태를 덮어쓰고 ② ISR 에서 부르면 태스크가 자르던 문자열이 망가지고 ③ 한 문자열을 자르는 도중 다른 문자열을 자르는 중첩 루프도 안 된다. C11 7.24.5.8 은 `strtok` 이 다른 `strtok` 호출과의 데이터 경쟁을 피할 의무가 없다고 명시한다. `strtok_r(str, sep, &save)` 은 상태를 호출자에게 넘겨 이 문제를 없애지만 **POSIX** 함수다 — C11/C17 에는 없고, Annex K 의 `strtok_s` 는 선택 사양이라 glibc·macOS 가 구현하지 않는다. 또 둘 다 "빈 필드" (`a,,b`) 를 구분 못 하고 입력을 수정한다. 토큰화는 10줄이라 직접 짜는 쪽이 이식성도 동작도 명확하다.

</details>

<details>
<summary>Q2. 숫자는 strtoul 로 파싱하면 되지 않나요? 무엇을 확인해야 하죠?</summary>

쓸 수는 있지만 체크리스트가 길다 (실측은 5. 풀이 Part B). ① 호출 전 `errno = 0` (7.5p3: 라이브러리는 `errno` 를 0 으로 만들지 않는다) ② `end == s` 면 숫자 없음 ③ `*end != '\0'` 이면 찌꺼기 ④ `errno == ERANGE` ⑤ 도메인 범위 — 이 맥(LP64)에선 `unsigned long` 이 64비트라 `4294967296` 이 ERANGE 없이 통과하므로 `v > UINT32_MAX` 도 따로 ⑥ `strtoul` 이 스스로 받아 주는 앞 공백, `+`, **`-`** (`"-1"` → `ULONG_MAX`, `errno` 0 — 7.22.1.4p5), base 16 에서 두 번째 `0x` (`"0x0x5"` → 5) 를 미리 막기 ⑦ base 0 의 `010` = 8. 연습 4 의 차분 퍼징에서 ⑥ 을 "첫 글자가 부호·공백인지" 만 본 흔한 판은 30만 개 중 3,846개가 어긋났고 첫 사례는 `"0x dd8a"` 였다 (접두사 뒤 공백을 strtoul 이 건너뜀).

꼬리질문 "Cortex-M 에서는?" → `unsigned long` 이 32비트라 같은 입력이 ERANGE 로 걸린다. 결과는 같지만 경로가 다르니, ERANGE 와 `v > max` 를 **둘 다** 검사해야 양쪽에서 맞다.

</details>

<details>
<summary>Q3. 왜 ISR 에서 바로 파싱·실행하지 않나요? 줄 하나 파싱은 어차피 짧은데요.</summary>

파싱 자체는 O(80) 이라 ISR 에서 못 할 정도는 아니다. 문제는 ① **실행**: `WRITE` 가 플래시나 느린 버스를 건드리면 ISR 이 밀리초 단위로 길어진다 ② 응답 `snprintf` 는 스택을 많이 쓰고 재진입성이 라이브러리 설정에 달려 있다 ③ ISR 이 길면 같거나 낮은 우선순위 인터럽트 지연(latency)이 모두 늘어 다른 UART 바이트를 놓친다 (RX FIFO 오버런). 그래서 ISR 은 바이트당 O(1) 의 줄 편집만 하고 `ready` 로 넘긴다. RTOS 라면 `ready` 대신 `xTaskNotifyFromISR` / 세마포어로 태스크를 깨우는 게 보통이다 (폴링 대신).

</details>

<details>
<summary>Q4. 이 파서를 어떻게 퍼징하나요?</summary>

이상적으로는 libFuzzer (`LLVMFuzzerTestOneInput` + `-fsanitize=fuzzer,address`) 나 AFL++ 같은 커버리지 기반 퍼저다. 이 맥의 Apple clang 은 `libclang_rt.fuzzer_osx.a` 가 없어 링크가 실패하는 것을 확인했다 (다른 LLVM 배포판은 이 맥에서 확인 안 함). 대안은 `demo_fuzz.c` 처럼 고정 시드 무작위 루프 + ASan/UBSan + 불변식이다. 실측에서 배운 것: ① 순수 무작위 바이트는 OK 경로에 한 번도 닿지 못했다 → 사전 단어/정상 명령 변이로 입력을 만들어야 깊이 들어간다 ② 퍼저는 크래시·불변식 위반은 잡지만 "값이 틀림" (넘침 검사 삭제) 은 못 잡았다 → 정답을 아는 표 테스트·차분 테스트가 같이 있어야 한다 ③ 같은 입력을 두 번 파싱해 같은지, 결과를 정규형으로 다시 써서 다시 파싱해 같은지(왕복 성질)는 정답 없이도 쓸 수 있는 강한 불변식이다.

</details>

<details>
<summary>Q5. 숫자 넘침을 어떻게 검출했나요? 8/16비트 MCU 나 나눗셈 없는 코어에서는요?</summary>

곱하기 전에 `acc > cutoff || (acc == cutoff && d > cutlim)` (`cutoff = UINT32_MAX / base`, `cutlim = UINT32_MAX % base`). `acc*10 + d < acc` 로 wrap 을 보는 방법은 틀렸다 — `"9999999999"` 는 wrap 후 1,410,065,407 로 오히려 커져서 못 잡는다 (실측). `cutoff` 는 호출당 한 번만 계산하므로 하드웨어 나눗셈이 없는 Cortex-M0 에서도 나눗셈 루틴 호출이 숫자 하나당 한 번이지 글자당이 아니다 (`thumbv6m` 로 컴파일해 보면 `bl __aeabi_uidiv` 한 번, 나머지는 곱하고 빼서 구한다. M4 는 `udiv` 한 명령). base 가 10/16 둘뿐이니 상수 두 쌍을 `switch` 로 고르면 나눗셈이 아예 없어진다. 8비트 AVR 이면 `uint32_t` 곱셈 자체가 라이브러리 루틴이라, 16진은 `acc << 4 | d` 로, 넘침은 "상위 니블이 0 인가" 로 보는 편이 싸다.

</details>

<details>
<summary>Q6. 에러 우선순위는 왜 그렇게 정했나요? MISSING 과 BADNUM 이 둘 다 해당하면요?</summary>

규칙은 "줄 → 명령어 → 인자를 왼쪽부터 (없음/모양/범위) → 남는 인자 → 조합 의미" 다. 왼쪽부터 보면 `err_tok` 이 사용자가 처음 고쳐야 할 위치를 가리킨다. `"READ 0x11"` 은 두 번째 인자가 없어서 MISSING @2 — 정렬(ALIGN)은 조합 검사라 그 뒤다. 다른 순서(예: 먼저 인자 개수부터)도 타당하다. 중요한 건 ① 순서를 정하고 ② 문서에 적고 ③ 겹치는 경우를 표 테스트에 넣어 못 박는 것. 그래야 리팩터링 후에도 테스트 스크립트(호스트 쪽 pytest 등, INFRA-B04)가 기대하는 에러 문자열이 바뀌지 않는다.

</details>

<details>
<summary>Q7. 보안 관점에서 이 콘솔에 더 필요한 것은?</summary>

① 길이 제한과 넘친 줄 폐기 (이미 함) ② **완전히 파싱된 뒤에만 실행** — 부분 실행 금지 ③ `READ`/`WRITE` 주소는 문법만으로는 부족하다: 허용 영역 목록(allowlist)으로 주변장치 창만 열고, 키·부트로더 영역은 막는다 ④ 양산 펌웨어에선 콘솔을 끄거나 인증 뒤에만 연다 (디버그 UART 는 공격자가 가장 먼저 찾는 핀) ⑤ 에러 메시지에 내부 상태를 흘리지 않는다 ⑥ 파서를 퍼징한다. 숫자·길이 검증 실패가 그대로 임의 메모리 읽기/쓰기가 되는 구조라, 이 파서는 사실상 공격 표면의 입구다.

</details>

<details>
<summary>Q8. 공용체 대신 명령마다 구조체를 따로 두면 안 되나요? MISRA 는요?</summary>

따로 둬도 된다 — 메모리가 조금 늘 뿐이다 (공용체는 가장 큰 멤버 크기, 따로 두면 멤버 크기의 합). 공용체를 쓰면 **태그와 실제 멤버가 어긋나는 버그** (kind 는 READ 인데 `u.write` 를 읽음) 가 생길 수 있어서, MISRA C:2012 는 `union` 사용 자체를 권고 규칙(Rule 19.2, advisory)으로 막는다. 방어: 읽는 곳을 `switch (kind)` 한 곳으로 모으고, 에러 시 union 을 0 으로 지우고, 테스트가 `kind` 와 필드를 같이 확인한다. C++ 이면 `std::variant`, Rust 면 `enum` 이 이 검사를 컴파일러에게 맡긴다.

</details>

## 9. 추가 연습

### 연습 1 — 따옴표 토큰

`ECHO "hello world"` 처럼 공백이 든 인자를 받는 `qtok()` 를 만든다. 따옴표 안에서 `\"` → `"`, `\\` → `\` 로 **제자리에서** 풀고, 닫히지 않은 따옴표는 `QT_UNTERMINATED`, `ab"c"` 나 `"ab"c` 처럼 따옴표가 토큰 일부만 감싸면 `QT_BADQUOTE`. 무작위 토큰을 인코딩 → 한 줄 → `qtok` 으로 되돌리는 왕복 테스트 50,000개.

<details>
<summary>답</summary>

읽기 포인터 `r` 와 쓰기 포인터 `w` 를 같은 버퍼에 둔다 (ALGO-B09 의 투 포인터). 이스케이프는 2글자를 1글자로 줄이기만 하므로 **항상 `w ≤ r`** — 아직 읽지 않은 글자를 덮어쓰지 않는다. 닫는 따옴표 다음 글자가 구분자/NUL 이 아니면 BADQUOTE, 그다음 `*w = '\0'` (w < r 라 안전). 테스트 하나: `"x\"` 는 닫는 따옴표가 이스케이프돼 UNTERMINATED 여야 한다.

</details>

### 연습 2 — 고유 접두사 축약

Cisco IOS 처럼 `wr` → `WRITE`, `re` → 모호함(`READ`/`RESET`/`REBOOT`), `set` → `SET` (`SETUP` 의 접두사지만 **정확히 일치가 우선**) 인 `cmd_lookup()`.

<details>
<summary>답</summary>

표를 한 번 훑으며 "tok 이 이름의 접두사인가" 를 본다. 정확히 일치하면 즉시 반환, 접두사 일치가 처음이면 기억, 두 번째면 AMBIGUOUS. O(명령 수 × 토큰 길이). 테스트는 모든 이름의 모든 접두사를 "일치 개수를 세는" 느린 기준과 전수 대조한다. 주의: 명령이 추가되면 예전에 유일하던 축약이 모호해진다 (`WHO` 를 추가하면 `w` 가 깨짐) — 스크립트에는 축약을 쓰지 말라고 문서화한다.

</details>

### 연습 3 — 에코와 줄 편집

단말 에코를 만드는 `lb_rx_echo()`: 글자 → 그대로 에코, BS → `"\b \b"` (왼쪽, 공백으로 덮기, 다시 왼쪽), 빈 줄에서 BS → BEL `"\a"`, Ctrl-U(0x15) → 줄 길이만큼 `"\b \b"`, 꽉 찬 줄에 글자 → BEL, CR → `"\r\n"`.

<details>
<summary>답</summary>

`lb_rx_byte` 에 출력 버퍼 하나를 더한 것이다. 핵심은 **에코 바이트도 ISR 에서 기다리며 보내면 안 된다** 는 것 — 실제로는 TX 링 버퍼에 넣고 TX-empty 인터럽트가 뺀다. Ctrl-U 는 80글자 줄에서 240바이트를 한 번에 만들어 내므로 TX 링 크기를 그만큼 잡거나, 넘치면 에코를 생략해야 한다 (줄 내용은 정확하고 화면만 어긋남).

</details>

### 연습 4 — strtoul 로 같은 계약 만들기 + 차분 퍼징

`parse_u32_libc()` 를 `strtoul` 로 구현해 본문 `parse_u32` 와 (`_` 없는 입력에서) 완전히 같게 만들고, 무작위 문자열 300,000개로 차분 비교한다. "첫 글자가 `-`/`+`/공백인지만 보는" 흔한 판도 같이 돌려 어긋남을 찾는다.

<details>
<summary>답</summary>

접두사 뒤 **본문 전체가 그 base 의 숫자인지 먼저 직접 확인**하면 `strtoul` 은 계산과 ERANGE 만 맡는다. 그다음 `errno = 0` → `strtoul` → `end` 확인 → `errno == ERANGE || v > max`. 실측: 꼼꼼한 판 0건 차이, 흔한 판 3,846건 차이 (첫 사례 `"0x dd8a"` — 본문 앞 공백을 strtoul 이 건너뜀; `"0x0x5"` 도 5 로 통과). 즉 `strtoul` 을 안전하게 쓰려면 결국 직접 파서만큼 검사를 써야 한다 — Part B 에서 직접 파서를 고른 이유.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
# differential fuzz: careful=0 diffs, naive=3846 diffs (first: "0x dd8a")
# 34/34 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_asan && ./practice_asan
```

## 10. 핵심 정리

- 파서는 3층: **토큰화**(위치만, 길이·개수 제한) → **토큰별 모양·범위** → **명령 전체 의미**(정렬, `addr+len` 넘침). 검사 순서 = 에러 우선순위, 표 테스트로 못 박는다.
- 토큰화는 직접: 건너뛰기 → `argv[k++]=p` → 달리기 → `'\0'`. `strtok` 은 숨은 정적 상태(재진입 불가), `strtok_r` 은 POSIX, C11 아님.
- 키워드는 "둘 다 동시에 끝나야 같다". `strncmp(t,"READ",4)` 는 `READX` 를 받고, `toupper(char)` 는 음수면 UB.
- 숫자: 문자열 **전체**, 부호·공백 금지, `_` 는 숫자 사이만, 넘침은 곱하기 전에 cutoff/cutlim, 모양 에러가 넘침보다 우선.
- `strtoul` 함정: `-1`→`ULONG_MAX`, 공백·`+` 허용, `errno` 잔류, `""`→0, `010`→8, `unsigned long` 이 맥 64비트 / Cortex-M 32비트.
- 결과는 `kind` 태그 + union, 에러면 `CMD_NONE` + `err_tok`. 반쯤 채운 값은 지운다.
- ISR 은 바이트당 O(1) 줄 편집만 (BS, 제어 문자 버림, 넘친 줄 **통째 폐기**), 파싱·실행은 태스크. `ready` 는 atomic release/acquire (M4 에서 `dmb sy`).
- 퍼저(무작위 루프 + ASan)는 크래시·불변식을, 표·차분 테스트는 값을 잡는다. Apple clang 엔 libFuzzer 런타임이 없다.

## 참고자료

- C11 (N1570) 7.24.5.8 `strtok`, 7.22.1.4 `strtoul` (p5 반환 타입에서 부정, p8 ERANGE), 7.5p3 `errno`, 7.4p1 `<ctype.h>` 인자 조건, Annex K `strtok_s` — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- [POSIX `strtok_r`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strtok.html) · [POSIX `strtoul`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/strtoul.html)
- C23 자리 구분자는 `_` 가 아니라 `'` (`0x4000'0010`, N2626) — 이 레슨의 `_` 는 Rust/Python/Verilog 식 콘솔 관례
- [libFuzzer 문서](https://llvm.org/docs/LibFuzzer.html), [AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html), [AFL++](https://github.com/AFLplusplus/AFLplusplus)
- MISRA C:2012 Rule 19.2 (`union` 비권장, advisory), Rule 21.7 (`atoi` 계열 금지); CERT C ERR34-C (문자열→숫자 변환 에러 검출)
- ARM, *Armv7-M Architecture Reference Manual* — `DMB` 와 메모리 순서
- LeetCode 8 String to Integer (atoi), LeetCode 65 Valid Number, LeetCode 151 Reverse Words in a String (공백 여러 개 토큰화), LeetCode 224 Basic Calculator (토큰화 + 파싱)
- AlgoMonster 노트 M01 §18 Simulation, M11 §10 Basic Calculator (Don 정리본)
- 관련 레슨: C-B07 (문자열 함수, 줄 버퍼), C-M09 (`strtol` 직접 구현, `SET <reg> <val>` 콘솔), C-M06 (명령 디스패치 테이블), DS-B04 · DS-A02 (ISR↔태스크 링 버퍼), BM-M04 (인터럽트 구동 UART), COMM-B14 (UART 커맨드 라인/AT 커맨드 파서), ALGO-M12 (상태 기계 스트림 파서: NMEA), ALGO-B09 (투 포인터 — 연습 1)
