---
id: COMM-B14
time: 60분
updated: 2026-11-11
---
# COMM-B14 UART 커맨드 라인/AT 커맨드 파서

## 학습 목표

- UART 로 한 바이트씩 들어오는 입력을 **라인 에디터(line editor)** 상태 머신으로 줄로 만든다: BS(0x08)/DEL(0x7F) 지우기, CR·LF·CRLF 를 "줄 끝 1번"으로, 최대 길이를 넘으면 **줄 끝까지 버리고 에러 1번**, NUL·ESC·0x80 이상 같은 비인쇄 문자 버리기, 에코(`"\b \b"`).
- **명령 테이블(command table)** 디스패처를 짠다: 이름, 최소/최대 인자 수, 핸들러, 사용법 문자열. 토큰화는 제자리(in-place)에서, 인자가 너무 많으면 조용히 자르지 않고 에러.
- `atoi`/`strtol` 없이 숫자 인자를 **문자열 전체 검사 + 넘치기 전 검사 + 범위 검사**로 파싱하고, `"12abc"`, `"99999999999"`, `"0x"`, `"-"` 를 각각 올바른 에러로 나눈다.
- 같은 라인 에디터·같은 숫자 파서 위에 **AT 명령**(`AT`, `ATE0`, `AT+BAUD=115200`, `AT+BAUD?`, `AT+BAUD=?`, 응답 `OK`/`ERROR`)을 올리고, UART RX ISR → 링 버퍼 → 메인 루프 구조로 연결한다.
- 악성 입력(넘치는 줄, NUL, 인자 폭탄, 공백만)과 고정 시드 퍼즈(fuzz)로 "어떤 바이트가 와도 죽지 않고, 다음 줄에서 회복한다"를 테스트로 보인다.

## 1. 문제

보드 bring-up 에서 가장 먼저 살리는 것이 UART 콘솔이고, 셀룰러·Wi-Fi·BLE 모듈은 거의 다 AT 명령으로 붙는다 — "콘솔 파서 짜 보세요"는 펌웨어 면접의 단골이고, 꼬리질문은 항상 **"이상한 입력이 오면요?"** 로 간다.

Don 의 경력으로 말하면: RF 모듈 통합 때 호스트 MCU 에서 `AT+...` 를 보내고 `OK` 를 기다리던 쪽, SSD 펌웨어의 UART 디버그 콘솔에서 `log`, `rd`, `wr` 명령을 치던 쪽 — 이번 레슨은 그 **반대편(장치 쪽 파서)** 을 만든다. 입력은 사람이 터미널(minicom, screen, PuTTY)에서 치거나 호스트 프로그램이 보낸다. 둘 다 **믿을 수 없는 외부 입력**이다: 사람은 Backspace 와 화살표를 누르고, 붙여넣기를 하고, 케이블은 노이즈를 넣고, 링 버퍼가 넘치면 바이트가 사라진다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">이번 레슨의 구조 — ISR 은 바이트만 넣고, 편집·파싱·실행은 전부 메인 루프</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="150" height="50" rx="6" class="f-mut"/>
    <rect x="195" y="40" width="160" height="50" rx="6" class="f-warn"/>
    <rect x="390" y="40" width="160" height="50" rx="6" class="f-mut"/>
    <rect x="585" y="40" width="145" height="50" rx="6" class="f-ok"/>
    <rect x="10" y="150" width="190" height="56" rx="6" class="f-acc"/>
    <rect x="240" y="150" width="200" height="56" rx="6" class="f-acc"/>
    <rect x="480" y="150" width="250" height="56" rx="6" class="f-acc"/>
    <rect x="240" y="240" width="200" height="44" rx="6" class="f-mut"/>
  </g>
  <g fill="currentColor">
    <text x="22" y="62">UART RX 핀</text><text x="22" y="80" font-size="12">USART RDR (1바이트)</text>
    <text x="207" y="62">RXNE 인터럽트</text><text x="207" y="80" font-family="monospace" font-size="12">uart_rx_isr(c)</text>
    <text x="402" y="62">RX 링 버퍼 64 B</text><text x="402" y="80" font-size="12">head(ISR) / tail(main)</text>
    <text x="597" y="62">메인 루프</text><text x="597" y="80" font-family="monospace" font-size="12">cli_poll()</text>
    <text x="22" y="172" font-weight="bold">Part A 라인 에디터</text><text x="22" y="192" font-family="monospace" font-size="12">le_feed(le, tx, c)</text>
    <text x="252" y="172" font-weight="bold">Part B / C 디스패처</text><text x="252" y="192" font-family="monospace" font-size="12">cli_exec() | at_exec()</text>
    <text x="492" y="172" font-weight="bold">명령 테이블 → 핸들러</text><text x="492" y="192" font-family="monospace" font-size="12">parse_i32, led, set, +BAUD</text>
    <text x="252" y="260">응답 out (TX 링 대용)</text><text x="252" y="277" font-size="12">에코 + OK / ERR / ERROR</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M160 65 H195"/><path d="M355 65 H390"/><path d="M550 65 H585"/>
    <path d="M657 90 V120 H105 V150"/>
    <path d="M200 178 H240"/><path d="M440 178 H480"/>
    <path d="M605 206 V262 H440"/>
    <path d="M105 206 V262 H240" stroke-dasharray="5 4"/>
    <path d="M195 65 l-8 -4 M195 65 l-8 4 M390 65 l-8 -4 M390 65 l-8 4 M585 65 l-8 -4 M585 65 l-8 4"/>
    <path d="M105 150 l-4 -8 M105 150 l4 -8 M240 178 l-8 -4 M240 178 l-8 4 M480 178 l-8 -4 M480 178 l-8 4"/>
    <path d="M440 262 l8 -4 M440 262 l8 4 M240 262 l-8 -4 M240 262 l-8 4"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="196" y="110">ISR 문맥: 넣고 끝 (수 µs)</text>
    <text x="440" y="110">메인 루프 문맥: 몇 바이트든 한 번에</text>
    <text x="112" y="236">에코 (점선)</text>
  </g>
</svg>
```

### Part A — 라인 에디터

`le_event_t le_feed(le_t *le, out_t *tx, uint8_t c)` — 바이트 하나를 처리하고 `LE_NONE` / `LE_LINE` / `LE_OVERFLOW` 를 돌려준다. 버퍼는 `char buf[CLI_LINE_MAX + 1]` (본문 40글자 + NUL), 항상 `strlen(buf) == len`.

- 인쇄 가능 문자(0x20–0x7E)는 붙이고 에코. TAB 은 공백으로 바꾼다.
- BS(0x08)·DEL(0x7F) 은 한 글자 지우고 `"\b \b"` 에코. 빈 줄이면 아무것도 안 함.
- CR, LF 는 줄 끝. **CR 바로 뒤의 LF 는 삼킨다** (CRLF = 줄 끝 1번). CR 과 LF 가 다른 호출로 와도 같아야 한다.
- 41번째 글자가 오면 지금까지 받은 것도 버리고 **줄 끝까지 버리는 모드**로. 줄 끝에서 `LE_OVERFLOW` 를 딱 한 번.
- 그 밖의 바이트(NUL, ESC, 0x80–0xFF, 기타 제어 문자)는 버리고 `n_ctrl_dropped` 를 센다.
- `LE_LINE` 뒤 `le->buf` 는 다음 `le_feed` 호출 전까지 유효하고, 다음 바이트에서 비워진다.

### Part B — 토크나이저, 숫자 파서, 명령 테이블

- `int tokenize(char *s, char *argv[], int max_args)` — 공백으로 제자리 분할. 토큰 수 또는 `max_args` 초과 시 `-1`.
- `num_status_t parse_i32(const char *s, int32_t lo, int32_t hi, int32_t *out)` — `[+-]? (0x 16진+ | 10진+)` 가 **문자열 전체**여야 한다. 모양이 틀리면 `NUM_E_SYNTAX`, int32 를 넘거나 `[lo, hi]` 밖이면 `NUM_E_RANGE`, 실패하면 `*out` 을 건드리지 않는다. 8진수 없음(`"007"` = 7).
- `cli_status_t cli_exec(cli_t *cli, const char *line)` — 상한 있는 길이 검사 → 작업 버퍼에 복사 → 토큰 → 이름으로 테이블 검색 → 인자 수 검사 → 핸들러. 결과는 `cli_finish()` 로 (응답 `OK` / `ERR ...`).
- 명령: `led <0|1>`, `set <reg 0..15> <val 0..0xFFFF>`, `get <reg>`, `echo [word...]`, `help`. 핸들러는 주어진다. **인자를 전부 검사한 뒤에만 쓴다.**

### Part C — AT 명령과 ISR 연결

- `cli_status_t at_exec(cli_t *cli, const char *line)` — 같은 라인 에디터, 같은 `parse_i32`. `AT`, `ATE0`/`ATE1`(에코 끄기/켜기), `AT+NAME`(실행), `AT+NAME?`(읽기), `AT+NAME=?`(지원 값 묻기), `AT+NAME=<값>`(설정). 응답은 `"\r\nOK\r\n"` / `"\r\nERROR\r\n"`, 정보는 `"\r\n+BAUD: 115200\r\n"`.
- `AT+BAUD=9600` 은 **OK 를 옛 속도로 다 보낸 뒤** 바꾼다 (`baud_pending` → `dev_tx_idle()`).
- `uart_rx_isr(c)` — 링 버퍼(64 B, 자유 증가 카운터, DS-B04)에 넣기만, 가득 차면 버리고 `overrun++`. `cli_poll()` — 메인 루프에서 꺼내 `cli_feed()`.
- 테스트: 호스트가 `OK` 를 안 기다리고 명령 11개(99 B)를 몰아 보낼 때 무슨 일이 생기는지, 고정 시드 퍼즈 30만 바이트에서 불변식과 회복.

## 2. Problem (English)

> **Design a UART command console for a microcontroller.** Bytes arrive one at a time from the UART RX interrupt. No heap.
>
> 1. `le_event_t le_feed(le_t *le, out_t *tx, uint8_t c)` — a line editor. Printable ASCII is appended (max 40 chars) and echoed; BS (0x08) and DEL (0x7F) erase one char (echo `"\b \b"`); CR, LF, and CRLF each end exactly one line; an over-long line is discarded up to the next end-of-line and reported once; all other bytes (NUL, ESC, 0x80–0xFF) are dropped.
> 2. `int tokenize(char *s, char *argv[], int max_args)` and `num_status_t parse_i32(const char *s, int32_t lo, int32_t hi, int32_t *out)` — no `atoi`/`strtol`/`sscanf`. Accept optional sign, decimal or `0x` hex; reject trailing junk, empty input, and overflow without ever invoking UB.
> 3. `cli_status_t cli_exec(cli_t *cli, const char *line)` — dispatch through a `const` table of `{name, min_args, max_args, handler, usage}`. Distinguish unknown command, wrong argument count, too many tokens, bad number, out of range, line too long.
> 4. Reuse the same core for a Hayes-style AT interface: `AT`, `ATE0/ATE1`, `AT+BAUD=<rate>`, `AT+BAUD?`, `AT+BAUD=?`, responses `OK`/`ERROR`. A baud change must take effect only after the `OK` has left the wire.
>
> Examples:
> ```text
> bytes "lef\x7fd 1\r\n"          -> one line "led 1"  -> "OK\r\n"
> "set 3 12abc"                   -> ERR bad number      (register unchanged)
> "set 3 99999999999"             -> ERR out of range
> "set 1 2 3 4 5 6 7 8"           -> ERR too many args   (9 tokens, max 8)
> 1000 x 'A' then "\r\n"          -> ERR line too long   (exactly once), next line works
> "AT+BAUD?"                      -> "\r\n+BAUD: 115200\r\n\r\nOK\r\n"
> "AT+BAUD=12345"                 -> "\r\nERROR\r\n"
> ```
>
> Follow-ups: Why not parse in the ISR? What happens when the host sends the next command before reading `OK`? How do you handle arrow keys? How would you fuzz this?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 일곱 군데: `le_feed`(Part A), `tokenize`·`parse_i32`·`cli_exec`(Part B), `at_exec`·`uart_rx_isr`·`cli_poll`(Part C). 출력 싱크, 가짜 장치, 셸/AT 핸들러, 응답 포맷(`shell_reply`, `at_reply`), 하네스는 주어진다. 스텁은 "아무 일도 안 함"이라 멈추거나 죽지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:338  strcmp(LT("led 1\r"), "led 1|") == 0
FAIL starter.c:339  strcmp(LT("led 1\r\n"), "led 1|") == 0
…
parse_i32 table: 36 rows, bad=23
…
fuzz shell: OK=0 EMPTY=0 UNKNOWN=0 ARGC=0 TOO_MANY=0 SYNTAX=0 RANGE=0 LONG=0  overrun=0 ctrl_dropped=0
FAIL starter.c:578  cov
FAIL starter.c:584  c->last == CLI_OK
5/91 passed
```

(exit 1. 통과한 5개는 스텁이 "아무 일도 안 해서" 우연히 맞는 것 — 빈 줄·공백만인 줄의 토큰 수 0, "ISR 은 파싱하지 않는다", 퍼즈 뒤 장치 상태가 정의역 안(2개), 퍼즈 불변식. 아무것도 안 받으면 불변식은 당연히 지켜진다 — 그래서 퍼즈엔 커버리지 검사 `cov` 가 같이 있어야 한다.)

## 4. 힌트

<details><summary>힌트 1 — le_feed 는 "판정 순서"가 전부다</summary>

① 지난번에 줄을 돌려줬으면(`ready`) 버퍼 비우기 → ② `last_cr` 갱신, CR 바로 뒤 LF 면 무시 → ③ CR/LF 면 줄 끝 (버리는 중이었으면 `LE_OVERFLOW`) → ④ 버리는 중이면 무시 → ⑤ BS/DEL → ⑥ TAB → 공백, 비인쇄면 버림 → ⑦ 꽉 찼으면 버리는 모드로 → ⑧ 저장 + 에코. 줄 끝 검사를 "버리는 중" 검사보다 **먼저** 해야 버리는 모드에서 빠져나올 수 있다.

</details>

<details><summary>힌트 2 — CRLF 는 "직전 바이트가 CR 이었나" 플래그 하나</summary>

"줄 끝 뒤에 오는 LF 를 무시"로 짜면 빈 줄(`"\n\n"`)까지 먹는다. 무시할 것은 **CR 바로 뒤의 LF 하나**뿐이다. 플래그는 `le_t` 안에 있어야 CR 과 LF 가 다른 ISR 배치로 와도 맞는다. LF CR 순서는 줄 끝 두 번(두 번째는 빈 줄)이다 — 빈 줄은 디스패처가 조용히 무시하면 된다.

</details>

<details><summary>힌트 3 — 넘침 검사는 곱하기 "전에"</summary>

`acc * base + d <= limit` 를 넘치지 않고 확인하려면 `acc > (limit - d) / base` 면 넘침이다. limit 은 양수면 2147483647, 음수면 2147483648 (`uint32_t` 로). 넘쳐도 **문자를 끝까지 훑어서** 문법 오류(`"99999999999x"`)가 범위 오류보다 먼저 보고되게 하라. 자세한 유도는 C-M09.

</details>

<details><summary>힌트 4 — cli_exec 의 에러 순서</summary>

길이 → 토큰 수(0 이면 빈 줄, -1 이면 too many) → 이름 → 인자 수 → 핸들러(숫자 모양 → 범위). 사용자에게 "가장 근본적인 잘못"을 먼저 알려준다. 입력 `line` 은 `const` 로 받고 지역 배열에 복사한 뒤 토큰화한다 — 라인 에디터 버퍼를 망가뜨리지 않는다.

</details>

<details><summary>힌트 5 — AT 는 "+이름" 까지 자르고 나머지 꼬리로 연산을 정한다</summary>

`strcspn(p, "=?")` 가 이름 길이. 꼬리가 `""` → 실행, `"?"` → 읽기, `"=?"` → 테스트, `"=" + 한 글자 이상` → 설정, 그 밖(`"="`, `"??"`)은 문법 오류. 이름 비교는 대소문자 무시, **길이까지 같아야** 한다 (`+BAU` 가 `+BAUD` 에 붙으면 안 된다).

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "ISR 에서는 바이트를 링 버퍼에 넣기만 하고, 메인 루프에서 라인 에디터 → 토큰 → 테이블 디스패치를 합니다."
2. "줄 버퍼는 고정 크기, 넘치면 줄 끝까지 버리고 에러 한 번. 잘라서 실행하지 않습니다."
3. "숫자는 `atoi` 대신 전체 문자열 검사 + 넘치기 전 검사 + 범위 검사. 실패하면 상태를 안 바꿉니다."
4. "명령 테이블은 `const` 라 플래시에 있고, 이름·인자 수·핸들러·사용법을 한 줄에 둡니다."
5. "테스트는 표 + 악성 입력 + 고정 시드 퍼즈. 불변식은 `len ≤ MAX`, `strlen == len`, 그리고 다음 줄에서 회복."

### Part A — 라인 에디터: 바이트 종류별 규칙

| 바이트 | 조건 | 동작 | 에코 |
|---|---|---|---|
| (모든 바이트) | `ready` (방금 줄을 돌려줌) | 먼저 버퍼 비우기 | — |
| LF 0x0A | 직전 바이트가 CR | 무시 (CRLF 의 LF) | 없음 |
| CR 0x0D / LF 0x0A | 버리는 중 | `LE_OVERFLOW`, 버리는 모드 해제 | `\r\n` |
| CR 0x0D / LF 0x0A | 평소 | `LE_LINE` | `\r\n` |
| 아무거나 | 버리는 중 | 무시 (BS 포함) | 없음 |
| BS 0x08 / DEL 0x7F | `len > 0` | 한 글자 지움 | `\b \b` |
| TAB 0x09 | — | 공백으로 바꿔 아래로 | — |
| 0x00–0x1F, 0x80–0xFF | — | 버림, `n_ctrl_dropped++` | 없음 |
| 0x20–0x7E | `len == 40` | 버퍼 비우고 버리는 모드 | `\a` (BEL) |
| 0x20–0x7E | 그 밖 | 붙이기 | 그 글자 |

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">le_feed 상태 — last_cr 은 상태와 별도인 1비트 기억 (CR 바로 뒤 LF 만 삼킴)</text>
  <g stroke="currentColor">
    <ellipse cx="130" cy="140" rx="95" ry="38" class="f-acc"/>
    <ellipse cx="610" cy="140" rx="105" ry="38" class="f-warn"/>
    <ellipse cx="370" cy="230" rx="105" ry="30" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="130" y="136" font-weight="bold">받는 중</text><text x="130" y="154" font-size="12">len 0..40</text>
    <text x="610" y="136" font-weight="bold">버리는 중</text><text x="610" y="154" font-size="12">discarding, len = 0</text>
    <text x="370" y="226" font-weight="bold">줄 완료</text><text x="370" y="244" font-size="12">ready: buf 유효</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M225 128 H505"/><path d="M505 128 l-9 -5 M505 128 l-9 5"/>
    <path d="M520 160 Q470 205 470 215"/><path d="M470 215 l-2 -10 M470 215 l6 -8"/>
    <path d="M200 166 Q240 220 265 226"/><path d="M265 226 l-10 -5 M265 226 l-9 4"/>
    <path d="M265 240 Q90 250 90 178"/><path d="M90 178 l-5 9 M90 178 l5 9"/>
    <path d="M60 106 C30 60 110 55 100 104"/><path d="M100 104 l-1 -10 M100 104 l-7 -6"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="122" y="62">인쇄 문자: 붙이기 · BS/DEL: 지우기</text>
    <text x="122" y="77">제어/0x80~: 버리기 (제자리 루프)</text>
    <text x="250" y="118">인쇄 문자인데 len == 40 → 비우고 BEL</text>
    <text x="500" y="205">CR/LF → LE_OVERFLOW (1번)</text>
    <text x="272" y="192">CR/LF → LE_LINE</text>
    <text x="40" y="262">다음 바이트: 버퍼 비우고 "받는 중"으로 처리</text>
    <text x="560" y="90">CR/LF 말고는 전부 무시</text>
  </g>
</svg>
```

**한 바이트씩 추적** — 터미널에서 `lef`, Backspace(이 터미널은 DEL 0x7F 를 보냄), `d 1`, Enter(CRLF) 를 쳤을 때:

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">입력 바이트 "lef" 7F "d 1" 0D 0A → 줄 "led 1" 한 번 (테스트 CHECK 와 같은 입력)</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="70" height="34" class="f-mut"/><rect x="95" y="40" width="70" height="34" class="f-mut"/>
    <rect x="170" y="40" width="70" height="34" class="f-mut"/><rect x="245" y="40" width="70" height="34" class="f-warn"/>
    <rect x="320" y="40" width="70" height="34" class="f-mut"/><rect x="395" y="40" width="70" height="34" class="f-mut"/>
    <rect x="470" y="40" width="70" height="34" class="f-mut"/><rect x="545" y="40" width="70" height="34" class="f-ok"/>
    <rect x="620" y="40" width="70" height="34" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="55" y="56">'l'</text><text x="130" y="56">'e'</text><text x="205" y="56">'f'</text><text x="280" y="56">DEL</text>
    <text x="355" y="56">'d'</text><text x="430" y="56">' '</text><text x="505" y="56">'1'</text><text x="580" y="56">CR</text><text x="655" y="56">LF</text>
    <text x="55" y="70" font-size="12">6C</text><text x="130" y="70" font-size="12">65</text><text x="205" y="70" font-size="12">66</text>
    <text x="280" y="70" font-size="12">7F</text><text x="355" y="70" font-size="12">64</text><text x="430" y="70" font-size="12">20</text>
    <text x="505" y="70" font-size="12">31</text><text x="580" y="70" font-size="12">0D</text><text x="655" y="70" font-size="12">0A</text>
    <text x="55" y="104">l</text><text x="130" y="104">le</text><text x="205" y="104">lef</text><text x="280" y="104">le</text>
    <text x="355" y="104">led</text><text x="430" y="104">led·</text><text x="505" y="104">led·1</text><text x="580" y="104">led·1</text><text x="655" y="104">(빈)</text>
    <text x="55" y="134" font-size="12">l</text><text x="130" y="134" font-size="12">e</text><text x="205" y="134" font-size="12">f</text>
    <text x="280" y="134" font-size="12">\b·\b</text><text x="355" y="134" font-size="12">d</text><text x="430" y="134" font-size="12">·</text>
    <text x="505" y="134" font-size="12">1</text><text x="580" y="134" font-size="12">\r\n</text><text x="655" y="134" font-size="12">—</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="164">buf (· = 공백) · 아랫줄 = 에코 바이트</text>
    <text x="400" y="164">CR: LE_LINE, last_cr = 1</text>
    <text x="400" y="182">LF: ready 라 먼저 비우고, last_cr 이라 삼킴</text>
  </g>
</svg>
```

설계 선택과 이유:

- **넘친 줄은 잘라서 실행하지 않는다.** 40글자에서 자르고 실행하면 `set 3 0x1234…` 같은 줄이 **다른 의미의 명령**으로 실행될 수 있다(뒤쪽 인자가 잘림). 사람이 친 줄의 의도는 줄 전체에 있으므로 전부 버리고 에러 한 번이 안전하다. 에러를 41번째 글자 시점이 아니라 **줄 끝에서** 내는 이유: 넘친 직후에 내면 사용자가 아직 치는 중이고, 남은 글자가 다음 "줄"로 해석되어 에러가 줄줄이 나온다. 테스트: 1000글자 + CRLF → `"!|"` 딱 한 번, 다음 `ok` 정상.
- **NUL 은 버린다(저장하지 않는다).** 버퍼는 C 문자열이라 NUL 을 넣으면 `strlen` 이 거기서 멈춰 `"set 4 1\0" "2"` 가 `set 4 1` 로 **잘려 실행**된다. 버리면 `set 4 12` 가 된다 (테스트). 더 보수적인 정책은 "제어 문자가 섞인 줄은 통째로 거부"인데, 사람이 쓰는 콘솔에선 잡음 한 바이트로 명령이 실패하는 쪽이 오히려 불편해 이 레슨은 버리기를 골랐다. 어느 쪽이든 **불변식 `strlen(buf) == len`** 은 지켜야 한다.
- **BS 에코는 `"\b \b"`.** 터미널에서 `\b` 는 커서만 왼쪽으로 옮긴다. 공백으로 덮고 다시 왼쪽으로 가야 화면에서 글자가 지워진다. Backspace 키가 0x08 을 보내는지 0x7F 를 보내는지는 터미널 설정마다 다르므로 둘 다 받는다(대부분의 최근 터미널 에뮬레이터 기본은 0x7F — 설정 확인 권장).
- **`ready` 플래그로 "줄을 돌려준 뒤 비우기"를 미룬다.** 호출자가 `le->buf` 를 읽을 시간을 준다. 별도 `le_reset()` 을 호출자에게 맡기면 언젠가 빼먹는다.
- **RAM**: `le_t` 60 B (맥에서 `sizeof` 확인. 40 B 줄 + NUL + 상태 + 카운터 3개). 줄 버퍼 하나뿐이고 복사는 `cli_exec` 의 지역 배열 41 B.

### Part B — 토크나이저

```c
for (;;) {
    while (*s == ' ') s++;
    if (*s == '\0') return argc;
    if (argc == max_args) return -1;      /* 9번째 토큰이 "존재"할 때만 에러 */
    argv[argc++] = s;
    while (*s != '\0' && *s != ' ') s++;
    if (*s != '\0') *s++ = '\0';
}
```

`strtok` 를 쓰지 않는 이유: 내부 정적 상태가 있어 재진입이 안 되고(두 UART 콘솔, RTOS 태스크 두 개), 연속 구분자를 다루는 방식이 숨겨져 있다. 인자 초과는 **에러**다 — 조용히 8개에서 자르면 `set 1 2 3` 같은 오타가 성공으로 보인다. 경계: 정확히 8개(`"a b c d e f g h    "`)는 OK, 9개는 -1.

### Part B — 숫자 파서: 문법이 먼저, 범위가 다음

`parse_i32` 의 문법은 `[+-]? ( 0[xX] 16진+ | 10진+ )` 이고 **문자열 끝까지** 맞아야 한다. 넘침 검사 `acc > (limit - d) / base` 는 C-M09 의 cutoff 기법을 32비트로 줄인 것이다 (`acc·base + d ≤ limit ⟺ acc ≤ ⌊(limit − d)/base⌋`, 정수라 양쪽이 동치). 넘쳐도 루프는 끝까지 돌며 **모양만** 검사한다 → `"99999999999x"` 는 SYNTAX, `"99999999999"` 는 RANGE.

라이브러리 함수와 비교 — `demo_libc.c` 실제 출력 (맥 arm64, `long` 64비트):

```text
input                  | atoi    | strtol(s,&e,10) val/rest/errno | strtol base 0  | strtoul base 0         | sscanf %d  | parse_i32
"42"                   | 42      | 42 / "" / 0                  | 42             | 42                     | n=1 v=42   | OK 42
"12abc"                | 12      | 12 / "abc" / 0               | 12 (+junk)     | 12                     | n=1 v=12   | E_SYNTAX
""                     | 0       | 0 / "" / EINVAL              | 0              | 0                      | n=-1 v=0   | E_SYNTAX
"abc"                  | 0       | 0 / "abc" / EINVAL           | 0 (+junk)      | 0                      | n=0 v=0    | E_SYNTAX
" 12"                  | 12      | 12 / "" / 0                  | 12             | 12                     | n=1 v=12   | E_SYNTAX
"007"                  | 7       | 7 / "" / 0                   | 7              | 7                      | n=1 v=7    | OK 7
"010"                  | 10      | 10 / "" / 0                  | 8              | 8                      | n=1 v=10   | OK 10
"0x1F"                 | 0       | 0 / "x1F" / 0                | 31             | 31                     | n=1 v=0    | OK 31
"1e3"                  | 1       | 1 / "e3" / 0                 | 1 (+junk)      | 1                      | n=1 v=1    | E_SYNTAX
"-1"                   | -1      | -1 / "" / 0                  | -1             | 18446744073709551615   | n=1 v=-1   | OK -1
"+-1"                  | 0       | 0 / "+-1" / EINVAL           | 0 (+junk)      | 0                      | n=0 v=0    | E_SYNTAX
"0x"                   | 0       | 0 / "x" / 0                  | 0 (+junk)      | 0                      | n=1 v=0    | E_SYNTAX
"2147483648"           | UB      | 2147483648 / "" / 0          | 2147483648     | 2147483648             | UB         | E_RANGE
"99999999999"          | UB      | 99999999999 / "" / 0         | 99999999999    | 99999999999            | UB         | E_RANGE
"99999999999999999999" | UB      | 9223372036854775807 / "" / ERANGE | 9223372036854775807 | 18446744073709551615 ERANGE | UB         | E_RANGE
```

읽는 법:

- `atoi("12abc") = 12`, `atoi("abc") = 0` — 에러를 알 방법이 없다. 값이 int 밖이면 **UB** 라 데모에서 호출조차 하지 않았다(`sscanf("%d")` 도 같음).
- `strtol` 은 맞게 쓰면 안전하지만 "맞게 쓰기"가 네 단계다(`errno = 0`, `end == s`, `*end != '\0'`, `ERANGE`). 그리고 `"99999999999"` 는 이 맥(LP64)에선 ERANGE 가 **아니다** — `long` 이 64비트라서. Cortex-M(ILP32)에선 ERANGE. 같은 코드가 플랫폼마다 다른 경로로 간다.
- base 0 의 `"010"` = 8. 콘솔에서 사람이 `010` 을 치면 10 을 뜻한다 → 이 레슨은 8진수를 지원하지 않는다.
- `strtoul("-1") = ULONG_MAX` — 부호 없는 변환이 음수를 조용히 받아들인다(연습 2).
- `strtol` 은 앞 공백을 건너뛴다(`" 12"`). 토큰에는 공백이 없으니 `parse_i32` 는 공백을 문법 오류로 본다.

### Part B — 디스패처

```c
static const cmd_t k_shell_cmds[] = {
    { "led",  1, 1,                cmd_led,  "led <0|1>" },
    { "set",  2, 2,                cmd_set,  "set <reg 0..15> <val 0..0xFFFF>" },
    { "get",  1, 1,                cmd_get,  "get <reg 0..15>" },
    { "echo", 0, CLI_MAX_ARGS - 1, cmd_echo, "echo [word...]" },
    { "help", 0, 0,                cmd_help, "help" },
};
```

- **인자 수 검사는 디스패처가 한다.** 핸들러는 `argv[1]` 이 있다고 믿어도 된다 — 핸들러 20개가 각자 `argc` 를 검사하다 하나를 빼먹는 일이 없다. FreeRTOS+CLI 의 `cExpectedNumberOfParameters`, Zephyr shell 의 `SHELL_CMD_ARG(..., mandatory, optional)` 가 같은 생각이다 (API 이름은 각 문서 확인 권장).
- **`const` 테이블 = 플래시.** RAM 0. ILP32 에서 항목 하나 16 B (포인터 3개 + `uint8_t` 2개 + 패딩) → 5개 80 B (계산값). 명령이 수십 개면 이름순 정렬 + 이진 탐색, 축약·자동완성이 필요하면 트라이(DS-B14).
- **원자적 실행**: `cmd_set` 은 레지스터 번호와 값을 **둘 다** 검사한 뒤에만 쓴다. 테스트: `set 3 12abc`, `set 3 -1`, `set 3 0x10000` 뒤에도 `reg[3] == 0x1234`, 랜덤 무효 `set` 5000개 뒤 레지스터 16개 `memcmp` 동일.
- **에러 메시지에 입력을 되돌려 찍지 않는다** (`ERR unknown command`). 라인 에디터가 제어 문자를 걸러도, 로그 수집기나 상위 프로그램이 응답을 파싱한다면 공격자 문자열을 그대로 반사하는 건 피하는 게 좋다.

### Part C — AT 명령: 같은 코어, 다른 문법

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">AT 줄의 구조와 AT+BAUD=9600 의 시간 순서 (115200 8N1: 1 문자 = 10 비트 = 86.8 µs)</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="60" height="32" class="f-mut"/><rect x="80" y="40" width="110" height="32" class="f-acc"/>
    <rect x="190" y="40" width="40" height="32" class="f-warn"/><rect x="230" y="40" width="130" height="32" class="f-ok"/>
    <rect x="360" y="40" width="50" height="32" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="50" y="61">AT</text><text x="135" y="61">+BAUD</text><text x="210" y="61">=</text><text x="295" y="61">115200</text><text x="385" y="61">CR</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="50" y="90">접두사</text><text x="135" y="90">이름 (strcspn "=?")</text><text x="210" y="90">연산</text>
    <text x="295" y="90">값 → parse_i32</text><text x="385" y="90">줄 끝</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="440" y="48">꼬리 ""      → EXEC</text>
    <text x="440" y="64">꼬리 "?"     → READ  "+BAUD: 115200"</text>
    <text x="440" y="80">꼬리 "=?"    → TEST  "+BAUD: (…)"</text>
    <text x="440" y="96">꼬리 "=값"   → SET</text>
  </g>
  <line x1="20" y1="130" x2="720" y2="130" stroke="currentColor" opacity=".4"/>
  <g fill="currentColor" font-size="12">
    <text x="20" y="160">호스트 → 장치</text><text x="20" y="215">장치 → 호스트</text><text x="20" y="270">장치 baud</text>
  </g>
  <g stroke="currentColor">
    <rect x="120" y="146" width="230" height="22" class="f-acc"/>
    <rect x="380" y="201" width="150" height="22" class="f-ok"/>
    <rect x="120" y="256" width="440" height="22" class="f-mut"/>
    <rect x="560" y="256" width="160" height="22" class="f-warn"/>
    <rect x="600" y="146" width="120" height="22" class="f-acc"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="128" y="162">"AT+BAUD=9600\r" 13문자</text>
    <text x="388" y="217">"\r\nOK\r\n" 521 µs</text>
    <text x="128" y="272">115200 (OK 도 옛 속도로)</text>
    <text x="568" y="272">9600</text>
    <text x="606" y="162">다음 명령</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M350 157 L380 212" stroke-dasharray="4 3"/>
    <path d="M530 212 L560 262" stroke-dasharray="4 3"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="392" y="190">파싱 · baud_pending = 9600</text>
    <text x="575" y="240">TC → dev_tx_idle()</text>
  </g>
</svg>
```

- **응답 형식**: ITU-T V.250 의 verbose 결과 코드는 `<CR><LF>OK<CR><LF>` 모양이다(S3 = CR, S4 = LF 기본값). 정보 응답도 `<CR><LF>` 로 감싼다. 그래서 `AT+BAUD?` → `"\r\n+BAUD: 115200\r\n\r\nOK\r\n"`. 셀룰러 모듈의 `+CME ERROR: <n>` 같은 확장 에러(3GPP TS 27.007)는 이 레슨 밖. (형식 세부는 V.250 §5.7 확인 권장.)
- **`+BAUD` 는 이 레슨의 가짜 명령이다.** 표준 V.250 의 DTE 속도 명령은 `+IPR` 이고, 모듈마다 자기 확장 명령도 있다. 공통된 실무 규칙은 그림 아래 줄: **OK 는 옛 속도로 보내고, 마지막 비트가 나간 뒤(TC 플래그) 바꾼다.** 파싱 즉시 BRR 을 바꾸면 OK 가 새 속도로 깨져서 나가 호스트는 응답을 못 받고, 호스트는 OK 를 받은 뒤에야 자기 속도를 바꿀 수 있다 — 교착. 테스트: `AT+BAUD=9600` 직후 `dev.baud == 115200 && baud_pending == 9600`, `dev_tx_idle()` 뒤 9600.
- **`ATE0`/`ATE1` 은 라인 에디터의 `echo` 를 끈다/켠다.** 에코 테스트가 흥미롭다: 에코가 켜진 상태에서 `ATE0` 을 치면 응답은 `"ATE0\r\n\r\nOK\r\n"` (명령까지는 에코), 다음 `AT` 는 `"\r\nOK\r\n"` 만. 호스트 프로그램은 보통 `ATE0` 부터 보낸다 — 응답 파서가 자기 명령의 에코를 걸러낼 필요가 없어진다.
- **엄격함의 선택**: 이 레슨은 `AT+BAUD = 9600`(공백), `ATE`(숫자 생략) 를 ERROR 로 본다. V.250 은 공백 무시, 숫자 생략 = 0 같은 관대한 규칙을 둔다(표준 확인 권장). 실제 모듈 호환이 목적이면 그 규칙을 따르고, 내부 공장 테스트 프로토콜이면 엄격한 쪽이 디버깅이 쉽다. 명령 이어 쓰기(`AT+LED=1;+BAUD?`)는 연습 4.

### Part C — ISR 과 메인 루프의 분리

`uart_rx_isr` 는 네 줄이다: 가득 찼나(`head - tail == 64`) → 넣기 → `head++`. 파싱을 ISR 에서 하지 않는 이유:

1. **시간**: 핸들러가 플래시를 쓰거나(설정 저장) I2C 를 기다리면 ISR 이 ms 단위로 길어지고, 그동안 다른 인터럽트와 다음 UART 바이트가 밀린다 → USART 하드웨어 overrun(STM32 의 ORE 플래그, RM 확인 권장).
2. **공유 상태**: 핸들러가 바꾸는 장치 상태를 메인 루프도 쓴다. ISR 에서 실행하면 모든 공유 변수가 경쟁 대상이 된다 (C-M14).
3. **응답 출력**: 응답 수십 바이트를 ISR 에서 블로킹 송신하면 끝장이다.

링 크기 계산: 115200 8N1 은 초당 11520 문자 → 64 B 링은 메인 루프가 **5.56 ms** 동안 `cli_poll` 을 못 불러도 견딘다. 921600 이면 0.69 ms. 실제 펌웨어에선 이 숫자로 링 크기를 정하거나, DMA + IDLE 라인 감지(COMM-M11)로 간다.

**호스트가 OK 를 안 기다리면** — 테스트의 실제 결과: `AT+LED=0\r`(9 B) × 11 = 99 B 를 poll 없이 밀어 넣으면 `overrun = 35`, poll 하면 **OK 7개**(63 B = 7 명령) 그리고 64번째 바이트 `'A'` 가 줄 버퍼에 남는다. 다음 `AT\r` 는 `"AAT"` 가 되어 **ERROR**, 그 다음 `AT\r` 에서 회복. 교훈: AT 호스트는 명령 하나 보내고 최종 결과 코드(`OK`/`ERROR`)를 기다린 뒤 다음을 보낸다 — 이게 사실상 흐름 제어(flow control)다. 하드웨어 RTS/CTS 가 있으면 링이 차기 전에 호스트를 멈출 수 있다.

### Part C — 퍼즈: 무엇을 확인했나

생성기는 "그럴듯한 줄"(명령 이름, 숫자, `0x`, `99999999999`, 공백/TAB 섞기, 0~11 토큰)에 5분의 1 확률로 임의 바이트 한 개(NUL, 0xFF, CR 포함)를 덮어쓰고, 8분의 1 확률로 BS/DEL 을 붙이고, 줄 끝을 CR / LF / CRLF / 없음 중에서 고른다. AT 쪽은 `AT`/`at`/`A`/`XY` + `+BAUD`/`+LED`/`E0`… + `?`/`=?`/`=값` 조합에 10분의 1 확률로 40글자 패딩. 바이트는 ISR 로 넣고 1~40 바이트마다 랜덤하게 poll 해서 링 넘침도 일어나게 했다. 실제 출력:

| 모드 | 바이트 | OK | EMPTY | UNKNOWN | ARGC | TOO_MANY | SYNTAX | RANGE | LONG | overrun | ctrl 버림 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| shell | 약 15만 | 140 | 276 | 2042 | 433 | 285 | 23 | 11 | 1316 | 2377 | 561 |
| AT | 약 15만 | 464 | 11 | 5996 | 0 | 0 | 601 | 534 | 1031 | 2852 | 1235 |

(AT 에는 ARGC/TOO_MANY 경로가 없다.) 검사한 것: 매 poll 뒤 `len ≤ 40`, `buf[len] == '\0'`, ready 가 아니면 `strlen(buf) == len`, `out.len ≤ OUT_CAP` / 셸 퍼즈가 **8가지 결과 전부**를 한 번 이상 밟음 / 장치 상태가 정의역 안(`led ≤ 1`, `baud_pending` 은 0 또는 허용 속도) / 퍼즈 직후 `"\r" + "led 1\r"` (AT: `"\rAT\r"`) 이 OK — 무엇이 들어왔든 **한 줄 만에 회복**. ASan/UBSan 빌드에서도 같은 테스트가 깨끗하다. 퍼즈의 가치는 숫자보다 "경로를 다 밟았다"는 커버리지 확인에 있다; 진짜 커버리지 유도 퍼저(libFuzzer, AFL++)로 `cli_exec` 를 돌리는 건 TEST-M02.

### 복잡도와 메모리

| 함수 | 시간 | 메모리 |
|---|---|---|
| `le_feed` | O(1) / 바이트 | `le_t` 60 B (줄 40 B) |
| `tokenize` | O(L) | 제자리, `argv` 8개 포인터 |
| `parse_i32` | O(토큰 길이) | 지역 변수 몇 개, 곱셈 1번/자리 |
| `cli_exec` | O(L + 명령 수 × 이름 길이) | 스택: 작업 줄 41 B + `argv` 32 B (ILP32) |
| `at_exec` | O(L + 명령 수) | 복사 없음 (`const char *` 위에서 직접) |
| `uart_rx_isr` | O(1) | 링 64 B + 인덱스 |

`cli_t` 전체는 맥에서 416 B, ILP32 로 계산하면 약 380 B (응답 버퍼 256 B 가 대부분). 실제 펌웨어에선 응답 버퍼 대신 TX 링에 바로 쓴다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 91/91 passed, exit 0 (약 0.1 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 91/91, 오류 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_libc.c -o demo_libc && ./demo_libc  # 5. 풀이의 atoi/strtol 비교표
```

## 7. 자주 하는 실수

**1. CRLF 를 줄 끝 두 번으로 센다.** `if (c == '\r' || c == '\n') → 줄 끝` 만 있으면 Windows 계열 터미널·대부분의 AT 호스트가 보내는 `\r\n` 마다 빈 줄이 하나 더 생긴다. 셸에선 프롬프트가 두 번 찍히는 정도지만, 빈 줄에 `ERROR` 를 내는 AT 파서라면 호스트가 받는 결과 코드가 하나 더 생겨 **명령과 응답의 짝이 어긋난다**. 이 레슨의 solution 에서 CRLF 검사 한 줄만 꺼 보면 91개 중 6개가 FAIL, 퍼즈의 EMPTY 가 셸 276 → 2130, AT 11 → 3390 으로 뛴다. 고친 코드: `last_cr` 플래그로 **CR 바로 뒤 LF 하나만** 삼킨다.

**2. 넘치는 줄을 잘라서 실행한다.** `if (len < MAX) buf[len++] = c;` (넘치면 그냥 무시)는 짧고 안전해 보이지만, 앞 40글자만으로 명령이 실행된다. 인자 순서에 따라 전혀 다른 명령이 된다. 고친 코드: 넘치는 순간 `discarding = true`, 줄 끝에서 `LE_OVERFLOW` 한 번.

**3. 넘침을 곱한 "뒤에" 검사한다.** `acc = acc * 10 + d; if (acc > INT32_MAX) …` 는 `int` 면 UB, `uint32_t` 면 조용히 감긴다. solution 의 넘침 검사 한 줄을 지우면 `"99999999999"` → **1215752191**, `"0x100000000"` → 0, `"2147483648"` → `INT32_MIN` 이 OK 로 통과했고 랜덤 범위 밖 20000개가 전부 통과했다. 레지스터 값 범위(0~0xFFFF) 검사가 그중 일부를 우연히 막아 디스패처 테스트는 통과한다는 점이 무섭다 — `set 3 4294967297` 은 2^32 + 1 이라 **1 로 감겨 성공**한다. 고친 코드: `if (acc > (limit - d) / base) big = true;` 를 곱하기 전에.

**4. 인자가 많으면 조용히 자른다.** `argv[8]` 이 차면 루프를 그냥 끝내는 토크나이저는 `set 1 2 3` 을 `set 1 2` 로 실행한다 (이 테이블에선 ARGC 로 걸리지만 `echo` 처럼 가변 인자 명령은 그대로 성공). 고친 코드: 9번째 토큰이 **존재하면** `-1` → `ERR too many args`.

**5. `strtok` / `atoi` / `sscanf`.** `strtok` 는 정적 상태로 재진입 불가, `atoi` 는 에러를 못 알리고 범위 밖이면 UB, `sscanf("%d")` 도 범위 밖이면 UB 에 newlib 에선 코드 크기도 크다 (5. 풀이 표). 펌웨어 콘솔은 외부 입력 처리 코드라 "보통은 괜찮다"가 통하지 않는다.

**6. BS 를 `"\b"` 만 에코한다 / 빈 줄에서도 에코한다.** `"\b"` 는 커서만 움직여 화면에 글자가 남는다 → 사용자는 지웠다고 생각한 글자를 보고 혼란. 빈 줄에서 `"\b \b"` 를 보내면 프롬프트 `> ` 까지 지워진다. 고친 코드: `if (le->len) { …; out_puts(tx, "\b \b"); }`.

**7. ISR 에서 `cli_exec` 를 부른다.** 줄 끝(`\r`)을 받은 ISR 에서 바로 실행하면 처음엔 잘 돈다 — 핸들러가 플래시 쓰기나 긴 응답을 하기 전까지는. ISR 은 바이트만 넣고, 줄 처리는 메인 루프(또는 RTOS 태스크를 세마포어로 깨워서, RTOS-B08).

## 8. 면접 Q&A

<details><summary>Q1. UART 콘솔의 전체 구조를 설명해 보세요. 어디까지 ISR 에서 하나요?</summary>

RXNE 인터럽트에서 RDR 을 읽어 링 버퍼에 넣는 것까지만 ISR (읽기 자체가 RXNE 를 지운다 — STM32 계열 기준, RM 확인). 메인 루프가 링에서 꺼내 라인 에디터 → 줄 끝에서 토큰화 → 테이블 디스패치 → 응답을 TX 링에 쓰고, TXE 인터럽트(또는 DMA)가 내보낸다. ISR 을 짧게 두는 이유는 다른 인터럽트 지연과 USART overrun(ORE) 때문이고, 공유 상태 경쟁을 메인 루프 하나로 모으기 위해서다. RTOS 라면 ISR 이 줄 끝에서 세마포어를 주거나 스트림 버퍼에 넣고, 콘솔 태스크가 처리한다(RTOS-M02). 바이트당 인터럽트가 부담인 고속 링크는 circular DMA + IDLE 라인 인터럽트(COMM-M11).

</details>

<details><summary>Q2. 줄이 버퍼보다 길면 어떻게 하나요? 왜 잘라서 실행하면 안 되죠?</summary>

넘치는 순간 버퍼를 비우고 "줄 끝까지 버리기" 모드로 가서, 줄 끝에서 에러를 한 번 낸다. 잘라서 실행하면 사용자가 의도하지 않은 명령이 실행될 수 있다(뒤쪽 인자가 사라지면 다른 의미). 에러를 줄 끝에서 내는 이유는 남은 바이트가 다음 줄로 해석되어 에러가 연달아 나오는 것을 막기 위해서. 버퍼 크기는 가장 긴 정상 명령 + 여유로 정한다 — V.250 은 장치가 받아야 하는 최소 명령 줄 길이를 정해 두고 있다(기억으로는 40자 — 표준 확인 권장). 이 레슨의 40 도 거기서 따왔다.

</details>

<details><summary>Q3. CR, LF, CRLF 가 섞여 들어오면?</summary>

터미널은 Enter 에 CR 을 보내는 게 보통이고(설정에 따라 CRLF), 리눅스 프로그램(`echo`, Python `serial.write(b"...\n")`)은 LF 를 보낸다. 셋 다 받으려면 CR 과 LF 를 둘 다 줄 끝으로 보고, **CR 바로 뒤의 LF 만** 삼킨다. 이 플래그는 줄 버퍼와 같이 상태로 들고 있어야 CR 과 LF 가 서로 다른 ISR 배치로 와도 맞다(테스트: `"x\r"` 다음 호출 `"\ny\r"`). `"\n\r"` 은 줄 끝 두 번이 되는데, 빈 줄은 무시하므로 해가 없다. 응답 쪽은 `\r\n` 을 보낸다 — 터미널이 CR 만 받으면 줄이 안 바뀌고 LF 만 받으면 계단처럼 밀린다.

</details>

<details><summary>Q4. atoi 대신 무엇을 쓰나요? strtol 이면 충분하지 않나요?</summary>

`atoi` 는 `"12abc"` → 12, `"abc"` → 0 이라 에러를 구분 못 하고 범위 밖은 UB 다. `strtol` 은 `errno = 0` → `end == s`(숫자 없음) → `*end != '\0'`(꼬리 쓰레기) → `ERANGE` → 목표 범위 검사까지 하면 안전하다(C-M09). 다만 `long` 폭이 플랫폼마다 달라(이 맥 64비트, Cortex-M 32비트) `"99999999999"` 의 실패 경로가 다르고, base 0 은 `"010"` 을 8 로 읽고, 앞 공백을 건너뛰고, newlib 의 `strtol` 은 로캘·`errno` 의존성을 끌고 온다. 콘솔 인자 하나에는 30줄짜리 전용 파서(`parse_i32`)가 더 명확하고 테스트하기 쉽다. 레지스터 비트 패턴이 필요하면 `parse_u32`(연습 2), 소수점은 고정소수점(연습 5) — `float` 파싱은 FPU 없는 MCU 에서 비싸다.

</details>

<details><summary>Q5. 터미널에서 화살표 키를 누르면 무슨 바이트가 오죠? 제어 문자만 거르면 되나요?</summary>

↑ 는 `ESC [ A` (1B 5B 41) 세 바이트다. `< 0x20` 만 거르면 ESC 는 사라지지만 `[A` 는 인쇄 가능 문자라 줄에 남는다 → `led[A 1` 같은 줄이 실행된다(연습 1 에서 순진한 필터로 재현). ESC 를 만나면 시퀀스 상태로 들어가 CSI(`ESC [`)는 끝 바이트(0x40–0x7E)까지, SS3(`ESC O`)는 한 바이트 더 삼킨다(ECMA-48 의 바이트 범위). 줄 끝은 시퀀스 중간이라도 줄을 끝내야 끊긴 시퀀스에 갇히지 않는다. 히스토리(↑)나 커서 이동까지 지원하면 그게 readline/linenoise 이고, MCU 에서는 Zephyr shell 처럼 선택 기능으로 둔다.

</details>

<details><summary>Q6. AT 명령을 보내는 호스트 쪽 드라이버를 짠다면 무엇을 조심하나요?</summary>

(Don 의 RF 모듈 경험 쪽 질문.) ① 명령 하나 보내고 **최종 결과 코드**(`OK`, `ERROR`, `+CME ERROR: n` 등)를 받을 때까지 다음을 보내지 않는다 — 이 레슨의 burst 테스트처럼 장치 링이 넘치면 줄이 섞여(`"AAT"`) 엉뚱한 ERROR 가 나온다. ② 타임아웃은 명령마다 다르다(네트워크 등록은 수 초~수 분). ③ 응답 사이에 **URC**(요청 안 한 알림, 예: `+CREG: 1`)가 끼어들 수 있으니 줄 단위로 분류한다. ④ 처음엔 `ATE0` 으로 에코를 끈다. ⑤ baud 변경은 OK 를 받은 뒤 호스트 속도를 바꾸고 `AT` 로 확인한다. ⑥ 바이너리 데이터 모드(예: `CONNECT` 이후, `+++` 탈출)는 라인 파서와 별도 상태로.

</details>

<details><summary>Q7. 이 파서를 어떻게 테스트했나요? 퍼즈는 어떻게 하죠?</summary>

세 층: (1) **표 테스트** — 숫자 36행(경계 `2147483647`/`-2147483648`/`0x80000000`, `"0x"`, `"-"`, `"99999999999x"`), 디스패처의 에러 종류별 응답 문자열. (2) **성질 테스트** — `snprintf` 로 만든 랜덤 int32 20000개 왕복, int32 밖의 랜덤 64비트 20000개는 전부 RANGE, 랜덤 유효 `set` 5000개 성공 + 무효 5000개 뒤 레지스터 불변. (3) **고정 시드 문법 퍼즈** — 30만 바이트, 매 poll 마다 불변식, 모든 결과 경로 커버, 마지막에 한 줄로 회복. 전부 ASan/UBSan 으로도 돌린다. 다음 단계는 `cli_exec` 를 libFuzzer 하네스로 감싸 커버리지 유도 퍼징(TEST-M02) — 이런 텍스트 파서는 퍼저가 가장 잘 깨는 코드다.

</details>

<details><summary>Q8. 보드 콘솔이 아무 반응이 없어요. 무엇부터 보나요?</summary>

바깥에서 안으로: ① 배선 — TX↔RX 교차, GND 공통, 전압 레벨(3.3 V UART 를 RS-232 ±12 V 에 직접 연결하지 않았나). ② 로직 애널라이저/스코프로 MCU TX 핀에 부팅 배너가 나오나, **비트 폭으로 실제 baud** 를 잰다(115200 이면 8.68 µs. 클럭 설정 오류로 baud 가 틀린 경우가 제일 흔하다 — COMM-B03). ③ 배너는 나오는데 입력이 안 먹으면: 터미널의 흐름 제어(RTS/CTS)가 켜져 있는데 선이 없거나, Enter 가 LF 만 보내는데 파서가 CR 만 보거나, 에코가 꺼져 있어 안 먹는 것처럼 보이거나. ④ 펌웨어 쪽: RXNE 인터럽트 enable / NVIC enable, ORE(overrun)가 세워진 채 안 지워져 수신이 멈춘 경우(STM32 일부 계열은 ORE 를 지워야 다시 받는다 — RM 확인). 카운터(`overrun`, `n_ctrl_dropped`, `n_status[]`)를 `stats` 명령으로 보이게 해 두면 현장에서 바로 원인이 좁혀진다.

</details>

## 9. 추가 연습

### 연습 1 — 터미널 키: 이스케이프 시퀀스와 Ctrl-U / Ctrl-W / Ctrl-C

화살표(`ESC [ A`), Delete(`ESC [ 3 ~`), F1(`ESC O P`), Ctrl+→(`ESC [ 1 ; 5 C`), Alt-문자(`ESC z`) 를 줄에 남기지 않고 삼켜라. Ctrl-U(0x15)는 줄 지우기, Ctrl-W(0x17)는 앞 단어 지우기, Ctrl-C(0x03)는 줄 취소(`L_CANCEL`). 먼저 "제어 문자만 거르는" 필터가 `led\x1b[A 1` 을 무엇으로 만드는지 보여라.

<details><summary>답 보기</summary>

순진한 필터 → `"led[A 1"` (실행된다!). 상태 3개를 더한다:

```c
switch (le->esc) {
case ESC_START: le->esc = (c == '[') ? ESC_CSI : (c == 'O') ? ESC_SS3 : ESC_NONE; return L_NONE;
case ESC_CSI:   if (c >= 0x40 && c <= 0x7E) le->esc = ESC_NONE; return L_NONE;
case ESC_SS3:   le->esc = ESC_NONE; return L_NONE;
case ESC_NONE:  break;
}
if (c == 0x1B) { le->esc = ESC_START; return L_NONE; }
```

CSI 의 파라미터(`0x30–0x3F`: 숫자, `;`)와 중간 바이트(`0x20–0x2F`)는 계속 삼키고 끝 바이트(`0x40–0x7E`: `A`, `~`, `C`)에서 끝난다. 줄 끝 검사는 이 `switch` 보다 **먼저** — `led\x1b[\r` 처럼 끊긴 시퀀스도 CR 에서 줄이 끝난다(테스트). Ctrl-W 는 뒤 공백을 먼저 지우고 단어를 지운다: `"set 3   " ^W ^W` → `""`.

</details>

### 연습 2 — 레지스터 값용 `parse_u32`

`parse_u32(s, &out)`: 부호 없음, 10진 / `0x` / `0b`, `0xFFFFFFFF` 까지. `"-1"` 은 SYNTAX(조용히 4294967295 가 되면 안 된다), `"4294967296"` 은 RANGE.

<details><summary>답 보기</summary>

`parse_i32` 에서 부호 처리를 빼고 limit 을 `UINT32_MAX` 로: `if (acc > (UINT32_MAX - d) / base) big = true;`. `0b` 접두사는 base 2 로. `strtoul("-1")` 이 `ULONG_MAX` 를 돌려주는 것(5. 풀이 표)과 대비된다. 랜덤 20000개를 10진·16진·32자리 2진으로 각각 만들어 왕복 확인, 경계 16행 표.

</details>

### 연습 3 — 따옴표 인자: `name "hello world"`

`tokenize_q(s, argv, max)`: `"..."` 안의 공백은 구분자가 아니고, 안에서 `\"` 와 `\\` 는 한 글자로. 닫히지 않은 따옴표는 `-2`. 제자리에서(추가 버퍼 없이) 하라.

<details><summary>답 보기</summary>

읽기 포인터 `r` 과 쓰기 포인터 `w` 를 따로 둔다. 따옴표는 건너뛰고(쓰지 않음), 이스케이프는 두 글자 읽고 한 글자 쓰므로 항상 `w ≤ r` — 아직 안 읽은 바이트를 덮어쓰지 않는다. 토큰 끝에서 `*w++ = '\0'`. `x "" y` 는 빈 인자를 포함한 3개, `ab"c d"e` 는 셸처럼 `abc de` 한 개. 따옴표 없는 랜덤 줄 5000개는 본문 `tokenize` 와 결과가 같아야 한다 (이때 `w == r`).

</details>

### 연습 4 — AT 명령 이어 쓰기: `AT+LED=1;+LED?`

`;` 로 이어진 확장 명령을 차례로 실행하고 마지막에 `OK` 하나. 하나라도 실패하면 거기서 멈추고 `ERROR` 하나. 앞서 실행된 명령은 어떻게 되나?

<details><summary>답 보기</summary>

`"AT"` 뒤를 작업 버퍼에 복사하고 `strchr(seg, ';')` 로 잘라 하나씩 실행. 정보 응답은 쌓이고 최종 결과 코드는 하나: `"\r\n+LED: 1\r\n\r\nOK\r\n"`. `AT+LED=2;+LED=0` → 첫 명령에서 ERROR, 두 번째는 실행 안 됨(led 그대로). 하지만 `AT+LED=0;+FOO` 는 **LED 가 이미 0 으로 바뀐 채** ERROR — 이어 쓰기는 트랜잭션이 아니다. V.250 도 에러 이후 나머지 명령을 실행하지 않는다고 정하지만 앞의 효과를 되돌리지는 않는다(표준 확인 권장). 호스트는 ERROR 를 받으면 상태를 다시 읽어야 한다. 빈 명령(`AT+LED=1;`, `AT;;`)은 ERROR 로 정했다.

</details>

### 연습 5 — 고정소수점 인자: `settemp 23.5`

`parse_milli(s, &out)`: `"23.5"` → 23500, `"-0.001"` → -1, 소수 셋째 자리까지, 결과는 int32 milli. `"1.2345"`, `"5."`, `".5"`, `"1e3"`, `"1,5"` 는 SYNTAX, `"2147483.648"` 은 RANGE.

<details><summary>답 보기</summary>

정수부를 `uint64_t` 에 모으되 1000만 이상이면 더 곱하지 않고(포화 — 넘침 없음, 어차피 RANGE), 소수부는 최대 3자리, 모자라면 ×10 으로 채운다. `m = ip·1000 + frac`, 음수면 limit 2147483648. FPU 없는 Cortex-M0 에서 `strtof` 를 끌어오지 않고 센서 설정값을 받는 방법이다(출력 쪽 고정소수점은 C-M10 연습 1). 랜덤 int32 20000개를 `"%s%lld.%03lld"` 로 만들어 왕복 확인.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 30/30 passed
```

## 10. 핵심 정리

- 구조: **ISR 은 링 버퍼에 넣기만**, 메인 루프가 라인 에디터 → 토큰 → `const` 명령 테이블 → 핸들러 → TX 링. 115200 에서 64 B 링 = 5.56 ms 여유.
- 라인 에디터 규칙: 인쇄 문자만 저장, BS/DEL 은 `"\b \b"`, **CR 바로 뒤 LF 하나만** 삼키기, 넘치면 줄 끝까지 버리고 에러 1번(잘라서 실행 금지), NUL/ESC/0x80~ 버리기, 불변식 `strlen(buf) == len`.
- 토큰 초과는 에러(조용히 자르지 않기). `strtok` 금지(재진입).
- 숫자: 문자열 전체 문법 → 넘치기 전 검사 `acc > (limit − d)/base` → 범위. 실패하면 상태 불변. `atoi`("12abc" = 12, 범위 밖 UB), base 0 의 `"010"` = 8, `strtoul("-1")` 함정.
- 디스패처가 인자 수를 검사하고, 핸들러는 모든 인자를 검사한 뒤에만 쓴다.
- AT: `AT` / `ATE0` / `+NAME` + `""`·`?`·`=?`·`=값`, 응답 `\r\nOK\r\n`. baud 변경은 OK 를 옛 속도로 보낸 뒤(TC). 호스트는 최종 결과 코드를 기다린 뒤 다음 명령.
- 테스트: 표 + 성질 + 고정 시드 퍼즈(불변식, 경로 커버리지, 한 줄 회복) + ASan/UBSan.

## 참고자료

- ITU-T Recommendation V.250, *Serial asynchronous automatic dialling and control* — AT 명령 줄 형식, `E`(에코), 결과 코드 형식(`<CR><LF>OK<CR><LF>`), `+IPR`, 명령 이어 쓰기. 이 레슨의 세부 조항(공백 무시, 최소 줄 길이, 에러 뒤 처리)은 기억에 의존 — 표준 확인 권장: <https://www.itu.int/rec/T-REC-V.250>
- 3GPP TS 27.007 — 셀룰러 AT 명령 집합, 테스트 명령 `=?` 응답 형식, `+CME ERROR`.
- ECMA-48, *Control Functions for Coded Character Sets* — CSI 시퀀스의 파라미터/중간/끝 바이트 범위 (연습 1): <https://ecma-international.org/publications-and-standards/standards/ecma-48/>
- ISO/IEC 9899:2011 (C11) §7.22.1 — `atoi`(표현 불가 시 UB), `strtol`/`strtoul` 계약; §7.21.6.2 — `fscanf` 변환 결과가 표현 불가면 UB.
- FreeRTOS+CLI — `CLI_Command_Definition_t`(명령 문자열, 도움말, 핸들러, 기대 인자 수): <https://www.freertos.org/FreeRTOS-Plus/FreeRTOS_Plus_CLI/FreeRTOS_Plus_Command_Line_Interface.html>
- Zephyr Project, *Shell* — `SHELL_CMD_ARG` 의 mandatory/optional 인자 수, 이스케이프 시퀀스·히스토리 옵션: <https://docs.zephyrproject.org/latest/services/shell/index.html>
- STMicroelectronics, STM32 계열 Reference Manual 의 USART 장(RXNE, ORE, TC, IDLE 플래그) — 플래그 이름·해제 방법은 계열마다 확인 권장.
- 이 레슨의 실측: `solution.c`(91개 검사, 퍼즈 표), `demo_libc.c`(atoi/strtol/strtoul/sscanf 비교표) — 맥 arm64, Apple clang, 2026-11-11.
- 관련 레슨: C-M09 (strtol/atoi 구현과 cutoff), C-M10 (UART 로그용 mini printf), DS-B14 (트라이 명령 파서·자동완성), DS-B04 (2의 거듭제곱 링 버퍼), DS-A01 (SPSC 큐와 atomics), C-M14 (ISR 공유 데이터), BM-B12 / BM-M04 (실제 폴링·인터럽트 UART 드라이버), COMM-B01~B03 (UART 프레임·오버샘플링·baud 오차), COMM-B10 (이진 프레임 파서), COMM-B13 (Modbus RTU), COMM-M10 (재동기화 파서), COMM-M11 (DMA + IDLE), RF-M08 (셀룰러 모뎀 AT 통합), RTOS-B08 / RTOS-M02 (ISR → 태스크), TEST-M02 (파서 퍼징), PLAT-B09 (bring-up 체크리스트: 콘솔).
