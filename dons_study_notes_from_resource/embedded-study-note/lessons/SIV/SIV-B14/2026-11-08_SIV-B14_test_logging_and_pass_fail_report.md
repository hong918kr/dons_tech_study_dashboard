---
id: SIV-B14
time: 80분
updated: 2026-11-08
---
# SIV-B14 테스트 로깅과 Pass/Fail 리포트

## 학습 목표

- 한 줄 = 평평한(flat) JSON 객체 하나인 **JSON Lines 로그**를 라이브러리 없이 C로 파싱하고, 깨진 줄의 **줄·열 번호와 이유**를 보고할 수 있다.
- pass/fail/skip/error 집계와 **판정(verdict) 규칙**을 정하고, 그것을 CI가 믿는 **종료 코드(exit code) 0/1**로 바꿀 수 있다 — "로그가 잘렸다", "모르는 status", "전부 skip" 같은 함정까지 포함해서.
- 실패 한 줄만 보고 디버그 엔지니어가 바로 움직일 수 있는 리포트(레지스터·주소·기대값·실제값·diff 비트·시각)를 텍스트와 Markdown으로 만든다.
- 헤더(seed / 보드 / 칩 stepping / FW 버전 / 시작 시각)와 로그 레벨(INFO/WARN/ERR)이 왜 있어야 하는지 면접에서 설명할 수 있다.

## 1. 문제

first silicon 보드가 밤새 수백 개 테스트를 돌린 다음 날 아침, 사람이 보는 건 **리포트 한 장**이고 CI가 보는 건 **종료 코드 한 개**다. 여기서 틀리면(잘린 로그를 PASS로 판정, 재시도로 가려진 간헐 실패) 실리콘 버그가 그대로 양산까지 간다. Rivian·NVIDIA 검증 면접에서 "테스트 결과를 어떻게 남기고 판정하나?"가 꼭 나오는 이유다.

이 레슨의 입력은 SIV-B08 프레임워크가 내보내는 로그다(여기선 형식을 축약해 넣었다). **한 줄에 레코드 하나**, 중첩 없는 JSON 객체다. 줄 단위라서 보드가 중간에 죽어도 그 전 줄들은 멀쩡히 남고, `grep`/`tail -f` 가 바로 먹힌다.

```text
{"type":"header","t_us":0,"lvl":"INFO","run_id":"nightly-1108-faulty","start":"2026-11-08T09:00:00Z","seed":"0x5EED1234","board":"EVB-UART rev B sn0042","chip":"UART-IP A0","fw":"siv-0.3.1+g3f2a1c9"}
{"type":"log","t_us":0,"lvl":"WARN","msg":"VDD_IO 1.71 V, near min 1.70 V"}
{"type":"test","t_us":6,"lvl":"ERR","test":"uart.reset.LCR","status":"FAIL","dur_us":2,"reg":"LCR","addr":"0x4000100C","exp":"0x00000003","act":"0x00000000","msg":"reset value mismatch"}
{"type":"end","t_us":242,"lvl":"INFO","records":10}
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="sivb14-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">로그 → 리포트 → 종료 코드 파이프라인</text>
  <rect x="10" y="44" width="150" height="86" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="85" y="68" text-anchor="middle" fill="currentColor" font-weight="bold">DUT 보드 / mock</text>
  <text x="85" y="90" text-anchor="middle" fill="currentColor" font-size="12">SIV-B08 TEST() 실행</text>
  <text x="85" y="110" text-anchor="middle" fill="currentColor" font-size="12">UART · JTAG · 링버퍼</text>
  <rect x="200" y="44" width="160" height="86" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="280" y="68" text-anchor="middle" fill="currentColor" font-weight="bold">run.jsonl</text>
  <text x="280" y="90" text-anchor="middle" fill="currentColor" font-size="12" font-family="monospace">header</text>
  <text x="280" y="106" text-anchor="middle" fill="currentColor" font-size="12" font-family="monospace">test / log ...</text>
  <text x="280" y="122" text-anchor="middle" fill="currentColor" font-size="12" font-family="monospace">end records=N</text>
  <rect x="400" y="44" width="150" height="86" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="475" y="68" text-anchor="middle" fill="currentColor" font-weight="bold">Part A 파서</text>
  <text x="475" y="90" text-anchor="middle" fill="currentColor" font-size="12">json_parse_line</text>
  <text x="475" y="110" text-anchor="middle" fill="currentColor" font-size="12">깨진 줄 → line/col</text>
  <rect x="590" y="44" width="140" height="86" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="660" y="68" text-anchor="middle" fill="currentColor" font-weight="bold">Part B 집계</text>
  <text x="660" y="90" text-anchor="middle" fill="currentColor" font-size="12">run_t · sticky fail</text>
  <text x="660" y="110" text-anchor="middle" fill="currentColor" font-size="12">run_verdict</text>
  <g stroke="currentColor" fill="none" marker-end="url(#sivb14-a)">
    <path d="M160 87 L198 87"/><path d="M360 87 L398 87"/><path d="M550 87 L588 87"/>
    <path d="M660 130 L660 170 L560 170 L560 188"/><path d="M660 170 L180 170 L180 188"/><path d="M660 170 L660 188"/>
  </g>
  <rect x="80" y="190" width="200" height="70" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="180" y="214" text-anchor="middle" fill="currentColor" font-weight="bold">텍스트 (CI 로그)</text>
  <text x="180" y="236" text-anchor="middle" fill="currentColor" font-size="12" font-family="monospace">SIV RESULT: FAIL pass=6 ...</text>
  <rect x="440" y="190" width="160" height="70" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="520" y="214" text-anchor="middle" fill="currentColor" font-weight="bold">Markdown (사람)</text>
  <text x="520" y="236" text-anchor="middle" fill="currentColor" font-size="12">Failures · Slowest · Skipped</text>
  <rect x="620" y="190" width="110" height="70" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="675" y="214" text-anchor="middle" fill="currentColor" font-weight="bold">exit 0 / 1</text>
  <text x="675" y="236" text-anchor="middle" fill="currentColor" font-size="12">CI 게이트</text>
  <text x="10" y="288" fill="currentColor" font-size="12">Part C: mock UART에 결함 3개를 넣어 돌린 로그로 이 전 과정을 확인한다 (결함 → FAIL 3줄 → exit 1).</text>
</svg>
```

### Part A — 한 줄 JSON 파서 (평평한 객체만)

- `json_parse_line(line, &obj, &err)` — `{ "key": 값, ... }` 한 줄을 키/값 배열로. 값은 문자열·숫자·`true`/`false`/`null` 만. 문자열 escape(`\" \\ \/ \b \f \n \r \t \uXXXX`)를 푼다. 중첩 객체/배열, 중복 키, 끝 쉼표, 닫힌 뒤 꼬리 쓰레기는 **거부**하고 1부터 세는 열 번호 `err.col` 과 이유 `err.why` 를 남긴다.
- 값이 버퍼(127자)보다 길면 **잘라내고 계속 읽는다** (`truncated=1`). 긴 메시지 하나 때문에 FAIL 레코드가 통째로 사라지면 안 된다.
- `parse_u32("0x4000100C")` — 10진 / `0x` 16진. 빈 문자열, 이상한 글자, 32비트 초과는 실패.

### Part B — 집계와 판정 (함정이 모인 곳)

- `ingest_line(run, line)` — 줄마다 레코드 종류(`header`/`test`/`log`/`end`)별로 반영. 깨진 줄은 세고 넘어간다(전체를 버리지 않는다).
- 같은 테스트가 두 번 나오면(재시도) **가장 나쁜 결과를 유지(sticky fail)** 하고 "1/2 attempts failed" 를 남긴다. 모르는 status(`"TIMEOUT"`)는 **ERROR**, 모르는 레코드 종류(`"metric"`)는 **무시하고 센다**.
- `run_verdict()` — `FAIL > INCOMPLETE > INVALID > PASS` 우선순위로 판정하고 이유를 모두 적는다. end 레코드가 없거나 개수가 안 맞으면 INCOMPLETE, 깨진 줄·헤더 없음·PASS 0개면 INVALID. 종료 코드는 PASS만 0.
- `top_slowest(run, idx, n)` — 소요 시간 상위 n개 (동률은 먼저 나온 것 먼저).

### Part C — mock UART 야간 런 → 리포트

UART IP mock(SIV-B02의 mock을 축약해 넣었다)에 결함 3개를 주입한다: ① LCR 리셋값 tie-off 오류(SIV-B03) ② ISR이 W1C가 아니라 RW로 구현됨(SIV-B04) ③ EN 후 LSR.THRE가 영영 안 섬 → poll timeout(SIV-B09). 같은 스위트를 정상/결함 mock에서 돌려 로그를 만들고, `render_md()` 로 Markdown 리포트(실패 표 · 느린 테스트 · skip 사유 · WARN/ERR)를 만든다. 정상 → exit 0, 결함 → FAIL 3줄 + exit 1 이 나와야 한다. Markdown 표 칸 안의 `|` 는 `\|` 로 escape(`sb_md_cell`).

## 2. Problem (English)

> **Turn a raw validation log into a report and a CI exit code.**
>
> The test framework writes JSON Lines: one flat JSON object per line, with `type` ∈ {`header`, `test`, `log`, `end`}. Test records carry `test`, `status` (`PASS`/`FAIL`/`SKIP`), `dur_us`, `t_us`, `lvl` (`INFO`/`WARN`/`ERR`), and on failure `reg`, `addr`, `exp`, `act`, `msg`. The header carries `run_id`, `start`, `seed`, `board`, `chip`, `fw`; the end record carries the number of test records written.
>
> 1. Write `int json_parse_line(const char *line, jobj_t *o, jerr_t *e)` without any library. Accept strings (with escapes), numbers, `true/false/null`. Reject nesting, duplicate keys, trailing commas and trailing garbage, reporting a 1-based column.
> 2. Aggregate a run: per-test status where a retried test keeps its *worst* result, unknown statuses count as `ERROR`, malformed lines are counted (not fatal), unknown record types are ignored.
> 3. Decide a verdict with priority `FAIL > INCOMPLETE > INVALID > PASS` and return exit code 0 only for `PASS`. A run with no `end` record, a record-count mismatch, malformed lines, no header, or zero passing tests must **not** exit 0.
> 4. Render a Markdown report with a failure table (reg, address, expected, actual, XOR diff and bit list), top-3 slowest tests, skip reasons and WARN/ERR messages. Escape `|` inside table cells.
>
> Example: a run where `LCR` reads `0x00000000` instead of reset value `0x00000003` yields a failure row for `uart.reset.LCR` containing
> `LCR @ 0x4000100C | 0x00000003 | 0x00000000 | 0x00000003 bits=[1,0]`, and the process exits with code 1. A run whose log stops before the `end` record exits 1 even if every parsed test passed.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크가 깨져 있을 때
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter; echo "exit=$?"
# 처음엔 FAIL 줄이 쭉 나오고 "4/46 passed", exit=1 — TODO 9개를 채워 46/46 을 만든다
```

TODO는 `parse_string`, `json_parse_line`, `parse_u32` (Part A), `status_from`, `ingest_line`, `run_verdict`, `top_slowest` (Part B), `sb_md_cell`, `render_md` (Part C) 이다. 문자열 빌더 `sb_t`, mock UART, 축약 로거 `run_suite`, 텍스트 리포트 `render_text`, 파일 CLI는 주어진다.

같은 바이너리가 CLI도 된다 — 레슨 폴더의 `run_good.jsonl`, `run_faulty.jsonl` 은 `--emit` 으로 만든 것, `run_truncated.jsonl` 은 faulty 로그를 10번째 줄 중간에서 자른 것(보드 리셋으로 UART 콘솔이 끊긴 상황)이다.

```bash
./solution --emit faulty > run_faulty.jsonl      # mock 스위트 실행 → 로그
./solution --text run_faulty.jsonl; echo $?      # CI용 텍스트, 종료 코드 = 판정
./solution --md   run_faulty.jsonl > report.md   # 사람용 Markdown
```

## 4. 힌트

<details>
<summary>힌트 1 — 파서는 "포인터 하나 + 기대하는 다음 글자"</summary>

`const char *p` 를 앞으로만 민다. 공백을 건너뛰고(`skip_ws`) → `{` → (문자열 키 → `:` → 값 → `,` 또는 `}`) 반복 → 꼬리 공백 → `\0`. 기대와 다른 글자를 만나면 `jfail(e, line, p, "이유")` 로 `p - line + 1` 열을 기록하고 -1. 재귀가 필요 없는 건 중첩을 금지했기 때문이다.

</details>

<details>
<summary>힌트 2 — 문자열: 넘쳐도 끝까지 읽는다</summary>

`if (n + 1 < cap) dst[n++] = c; else *trunc = 1;` 처럼 **쓰기만** 멈추고 읽기는 닫는 따옴표까지 계속한다. 그래야 뒤의 `,"addr":...` 를 이어서 파싱할 수 있다. `\u` 뒤 4글자는 하나씩 hex 검사 — 문자열이 그 전에 끝나면 `hexval('\0') < 0` 에서 멈춘다(버퍼 밖을 안 읽는다).

</details>

<details>
<summary>힌트 3 — 재시도는 "더 나쁜 쪽"이 이긴다</summary>

status 열거형을 심각도 순서 `SKIP < PASS < FAIL < ERROR` 로 두면 `if (attempts == 1 || s > t->st)` 한 줄로 sticky fail 이 된다. 이때 상세(reg/exp/act/msg)도 더 나쁜 레코드의 것으로 바꾼다. 소요 시간은 attempt 합(런 전체 비용).

</details>

<details>
<summary>힌트 4 — 판정은 "이유 목록"을 먼저 만든다</summary>

조건마다 `add_reason(&sb, &v, 등급, "...")` 를 호출해 `v = max(v, 등급)` 으로 올리고 이유를 `; ` 로 잇는다. FAIL과 INCOMPLETE가 동시에 있으면 판정은 FAIL이지만 이유에는 둘 다 나와야 한다 — 디버그하는 사람은 "실패했는데 로그도 잘렸다"를 알아야 한다.

</details>

<details>
<summary>힌트 5 — 상위 N개는 정렬 말고 삽입</summary>

크기 N짜리 `idx[]` 를 내림차순으로 유지한다. 새 항목이 꽉 찬 배열의 마지막보다 **엄격히 클 때만** 들어가고, 앞 칸이 **엄격히 작을 때만** 앞으로 민다 → 동률은 먼저 나온 것이 앞에 남는다(안정). O(테스트 수 × N).

</details>

## 5. 풀이

### Part A — 평평한 JSON 한 줄 파서

**왜 C로, 왜 직접?** 리포터를 Python으로 쓰면 `json.loads` 한 줄이다(CI 서버라면 그게 맞다). 하지만 ① 같은 파서가 보드 쪽 펌웨어나 lab PC의 작은 C 도구(JTAG로 링버퍼를 덤프해 바로 요약)에 들어가야 할 때가 있고 ② 면접에서 "라이브러리 없이 로그 파서를 써보라"가 그대로 나온다. 그래서 형식을 **일부러 평평하게** 정했다: 중첩이 없으니 재귀 없는 상태 기계 하나로 끝난다.

| 문법 요소 | 받아들이는 것 | 거부 (err.why) |
|---|---|---|
| 객체 | `{` … `}`, 빈 객체 `{}` | `expected '{'`, `expected ',' or '}'` |
| 키 | `"…"` 23자 이하, 줄 안에서 유일 | `key too long`, `duplicate key` |
| 문자열 값 | escape 8종 + `\uXXXX`(ASCII만, 그 외 `?`) | `unterminated string`, `control char in string`, `bad escape` |
| 숫자 | `-`, 숫자, `.`, `e/E`, `+` 의 연속을 원문으로 보관 | — (해석은 `parse_u32` 가) |
| 리터럴 | `true` `false` `null` | `expected value` |
| 중첩 | — | `nested value (log must be flat)` |
| 꼬리 | 공백·`\r\n` 만 | `trailing garbage` |

열 번호가 왜 중요한가: 로그가 깨지는 원인은 대부분 **전송 계층**이다 — UART 오버런으로 글자가 빠짐, 두 코어가 같은 콘솔에 동시에 써서 줄이 섞임, 보드 리셋으로 줄이 중간에 끊김. `line 10 col 71: unterminated string` 은 "71번째 글자에서 끊겼다 → 그 시점에 리셋"이라는 단서다.

**중복 키를 거부하는 이유.** RFC 8259는 객체 안 이름이 유일해야 한다고(SHOULD) 하고, 중복이면 구현마다 동작이 다르다고 경고한다. 검증 로그에서 `"act"` 가 두 번 나오면 어느 값을 믿을지 모르는 것이고, 그건 producer 버그다 — 조용히 하나를 고르지 말고 깨진 줄로 센다.

**숫자를 원문으로 두는 이유.** 레지스터 값은 `"0x4000100C"` 처럼 **문자열 hex**로 쓴다(JSON 숫자는 16진이 없고, 많은 도구가 숫자를 double로 읽어 2^53 넘으면 정밀도를 잃는다). `dur_us` 같은 진짜 숫자만 JSON 숫자로 쓰고, 해석은 `parse_u32` 가 범위 검사(`v > 0xFFFFFFFF` 면 실패)와 함께 한다. `"-1"` 은 거부 — 음수 소요 시간은 시계 버그다.

### Part B — 집계 규칙과 판정

레코드 종류별 처리:

| type | 처리 | 이상 상황 |
|---|---|---|
| `header` | run_id/start/board/chip/fw 복사, seed 파싱 | 두 번째 header → 깨진 줄 ("두 런이 한 파일에 붙었다") |
| `test` | `find_or_add` → attempts++, dur 합, 더 나쁘면 상세 교체 | test/status 없음 → 깨진 줄, 테이블 꽉 참 → dropped |
| `log` | WARN/ERR 만 notes 에 저장, ERR 수 셈 | — |
| `end` | `records` 저장 | 숫자 아님 → 깨진 줄 |
| 그 외 | `unknown_types++` 후 무시 | (새 FW가 `metric` 을 추가해도 옛 리포터가 안 죽음) |

**모르는 type은 무시, 모르는 status는 ERROR — 왜 다르게?** type은 "추가 정보"라서 모르면 건너뛰어도 판정이 틀리지 않는다(forward compatible). status는 **판정 그 자체**라서 모르는 값을 PASS로 치면 거짓 초록불이 된다. 원칙: *이해 못 한 것은 절대 통과시키지 않는다(fail-safe)*.

**sticky fail.** 검증 런에서 재시도(retry)는 흔하지만, "1회차 FAIL → 2회차 PASS"를 PASS로 보고하면 **마진(margin) 버그**가 숨는다. 전압·온도·클럭 경계에서만 가끔 터지는 셋업/홀드 타이밍 문제, PLL lock 이 가끔 늦는 문제가 정확히 이렇게 보인다. 그래서 테스트 상태는 모든 attempt 중 최악이고, 리포트에 `failed 1/2 attempts` 를 남겨 shmoo(전압×주파수 스윕)로 넘길 근거를 준다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">run_verdict: 조건 → 등급 (가장 높은 등급이 판정, 이유는 전부 기록)</text>
  <g fill="currentColor" font-weight="bold">
    <text x="20" y="56">조건</text><text x="470" y="56">등급</text><text x="640" y="56">exit</text>
  </g>
  <line x1="10" y1="64" x2="730" y2="64" stroke="currentColor"/>
  <rect x="460" y="72" width="150" height="54" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="535" y="104" text-anchor="middle" fill="currentColor" font-weight="bold">FAIL (3)</text>
  <text x="20" y="92" fill="currentColor">FAIL/ERROR 테스트 ≥ 1</text>
  <text x="20" y="114" fill="currentColor">type=log, lvl=ERR 메시지 ≥ 1 (예: PMIC 과전압)</text>
  <rect x="460" y="134" width="150" height="54" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="535" y="166" text-anchor="middle" fill="currentColor" font-weight="bold">INCOMPLETE (2)</text>
  <text x="20" y="154" fill="currentColor">end 레코드 없음 (hang · 리셋 · 로그 유실)</text>
  <text x="20" y="176" fill="currentColor">end.records ≠ 파싱한 test 레코드 수</text>
  <rect x="460" y="196" width="150" height="76" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="535" y="239" text-anchor="middle" fill="currentColor" font-weight="bold">INVALID (1)</text>
  <text x="20" y="214" fill="currentColor">깨진 줄 ≥ 1 · MAX_TESTS 넘쳐 drop</text>
  <text x="20" y="236" fill="currentColor">header 없음 (seed/보드/FW 모름 → 재현 불가)</text>
  <text x="20" y="258" fill="currentColor">PASS 0개 (빈 런, 전부 SKIP)</text>
  <rect x="460" y="280" width="150" height="40" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="535" y="305" text-anchor="middle" fill="currentColor" font-weight="bold">PASS (0)</text>
  <text x="20" y="305" fill="currentColor">위 조건 하나도 없음</text>
  <g fill="currentColor" font-family="monospace" font-weight="bold">
    <text x="650" y="104">1</text><text x="650" y="166">1</text><text x="650" y="239">1</text><text x="650" y="305">0</text>
  </g>
</svg>
```

**end 레코드가 핵심인 이유.** 로그는 "쓰인 것"만 보여준다. 보드가 7번째 테스트에서 hang 되면 로그엔 PASS 6줄만 있고, 집계만 하면 `pass=6 fail=0` → 초록불이다. TAP(Test Anything Protocol)의 plan 줄 `1..N` 과 같은 발상으로, 마지막에 "나는 N개 썼다"를 남기고 리포터가 개수를 맞춰본다. 실제로 해본 결과 — 정상 로그를 9줄에서 자르면:

```text
$ head -9 run_good.jsonl | ./solution --text /dev/stdin; echo "exit=$?"
SIV RESULT: INCOMPLETE pass=7 fail=0 error=0 skip=0 | run=nightly-1108-good board="EVB-UART rev B sn0042" fw=siv-0.3.1+g3f2a1c9 seed=0x5EED1234
REASON: no end record (hang/reset/lost log?)
WARN  t=0us VDD_IO 1.71 V, near min 1.70 V
exit=1
```

FAIL 한 줄이 전송 중에 사라진 경우(끝은 멀쩡) — 개수 불일치로 잡힌다:

```text
$ grep -v '"uart.reset.LCR"' run_faulty.jsonl | ./solution --text /dev/stdin | head -2
SIV RESULT: FAIL pass=6 fail=2 error=0 skip=1 | run=nightly-1108-faulty board="EVB-UART rev B sn0042" fw=siv-0.3.1+g3f2a1c9 seed=0x5EED1234
REASON: 2 test(s) failed; end says 10 records, parsed 9
```

**PASS 0개 = INVALID.** 보드 점퍼가 빠져 모든 테스트가 SKIP 이면 `fail=0` 이다. 이걸 0으로 끝내면 "아무것도 검증 안 한 런"이 초록불이 된다(pytest도 테스트를 하나도 못 모으면 별도 종료 코드 5를 낸다). skip 사유를 리포트에 꼭 남기는 것도 같은 이유 — skip은 **커버리지 구멍**이다.

**top_slowest.** 느린 테스트 목록은 두 가지에 쓴다: ① 야간 런 시간 예산 관리 ② **timeout 냄새** — poll 이 매번 timeout 근처까지 가면 HW가 느리거나 timeout 값이 너무 빡빡하다. 이번 결함 런에서 1등이 `uart.poll.THRE` (202us = CTRL.EN 쓰기 2us + timeout 200us)인 게 그 예다.

### Part C — mock UART 런과 리포트

mock은 레지스터 6개(ID RO, CTRL RW, LCR RW, ISR W1C, LSR RO, SCR RW)와 fake clock(접근당 2us, 폴링 간격 8us)을 가진다. 시간이 가짜라 `dur_us` 와 `t_us` 가 매번 같고, 그래서 CHECK에 넣을 수 있다(SIV-B09 참고). 주입한 결함과 리포트가 보여주는 것:

| 결함 (faults_t) | 실제 실리콘에서의 원인 | 잡는 테스트 | 리포트 줄의 핵심 | 디버그 엔지니어의 다음 행동 |
|---|---|---|---|---|
| `lcr_reset_bug` | 리셋 tie-off 를 RTL/합성에서 잘못 연결 | `uart.reset.LCR` (SIV-B03) | exp 0x3, act 0x0, `bits=[1,0]` | RTL 리셋값 vs 기술서(SystemRDL/IP-XACT) 비교, errata 후보 |
| `isr_w1c_as_rw` | W1C 비트를 일반 RW 플롭으로 구현 | `uart.w1c.ISR` (SIV-B04, B11) | exp 0x6, act 0x1, `bits=[2,1,0]` | 1을 쓴 비트만 지워지는지 단독 재현 → ISR 클리어 SW workaround 검토 |
| `thre_stuck` | 클럭 게이트 미해제 / 상태 로직 고착 | `uart.poll.THRE` (SIV-B09) | timeout 200us, 20 polls, last LSR=0x0 | 클럭·리셋 컨트롤러 상태 덤프(SIV-B10), 스코프로 TX 핀 확인 |

결함 런의 실제 텍스트 리포트 (CI 로그용, 첫 줄은 grep 대상):

```text
$ ./solution --text run_faulty.jsonl; echo "exit=$?"
SIV RESULT: FAIL pass=6 fail=3 error=0 skip=1 | run=nightly-1108-faulty board="EVB-UART rev B sn0042" fw=siv-0.3.1+g3f2a1c9 seed=0x5EED1234
REASON: 3 test(s) failed
FAIL  uart.reset.LCR t=6us reg=LCR addr=0x4000100C exp=0x00000003 act=0x00000000 diff=0x00000003 bits=[1,0] msg="reset value mismatch"
FAIL  uart.w1c.ISR t=40us reg=ISR addr=0x40001010 exp=0x00000006 act=0x00000001 diff=0x00000007 bits=[2,1,0] msg="FORCE=0x7 then write 0x1 | expect bit0 cleared, bits[2:1] kept"
FAIL  uart.poll.THRE t=242us reg=LSR addr=0x40001014 exp=0x00000020 act=0x00000000 diff=0x00000020 bits=[5] msg="timeout 200us waiting LSR.THRE=1, 20 polls, last LSR=0x00000000"
WARN  t=0us VDD_IO 1.71 V, near min 1.70 V
exit=1
```

정상 런:

```text
$ ./solution --text run_good.jsonl; echo "exit=$?"
SIV RESULT: PASS pass=9 fail=0 error=0 skip=1 | run=nightly-1108-good board="EVB-UART rev B sn0042" fw=siv-0.3.1+g3f2a1c9 seed=0x5EED1234
REASON: all 9 test(s) passed, 1 skipped
WARN  t=0us VDD_IO 1.71 V, near min 1.70 V
exit=0
```

결함 런의 실제 Markdown 리포트 (`./solution --md run_faulty.jsonl`, exit=1). W1C 메시지 안의 `|` 가 `\|` 로 escape 된 것을 보라 — 안 하면 표가 한 칸 밀려 깨진다:

```text
# SIV Test Report: FAIL

> exit 1: 3 test(s) failed

| run | start | board | chip | fw | seed |
|---|---|---|---|---|---|
| nightly-1108-faulty | 2026-11-08T09:00:00Z | EVB-UART rev B sn0042 | UART-IP A0 | siv-0.3.1+g3f2a1c9 | 0x5EED1234 |

**10 tests**: 6 PASS, 3 FAIL, 0 ERROR, 1 SKIP (records 10, malformed 0) / log INFO 9, WARN 1, ERR 3

## Failures

| test | status | t (us) | reg @ addr | expected | actual | diff | msg |
|---|---|---:|---|---|---|---|---|
| `uart.reset.LCR` | FAIL | 6 | LCR @ 0x4000100C | 0x00000003 | 0x00000000 | 0x00000003 bits=[1,0] | reset value mismatch |
| `uart.w1c.ISR` | FAIL | 40 | ISR @ 0x40001010 | 0x00000006 | 0x00000001 | 0x00000007 bits=[2,1,0] | FORCE=0x7 then write 0x1 \| expect bit0 cleared, bits[2:1] kept |
| `uart.poll.THRE` | FAIL | 242 | LSR @ 0x40001014 | 0x00000020 | 0x00000000 | 0x00000020 bits=[5] | timeout 200us waiting LSR.THRE=1, 20 polls, last LSR=0x00000000 |

## Slowest 3

| test | dur (us) | status |
|---|---:|---|
| `uart.poll.THRE` | 202 | FAIL |
| `uart.rw.SCR` | 20 | PASS |
| `uart.w1c.ISR` | 8 | FAIL |

## Skipped

- `uart.loopback.ext`: needs loopback jumper J7 (TX-RX) on EVB

## WARN/ERR log

- t=0 us **WARN** VDD_IO 1.71 V, near min 1.70 V
```

잘린 로그(`run_truncated.jsonl`: 결함 로그를 W1C 줄 70글자에서 끊음)는 판정 우선순위가 어떻게 겹치는지 보여준다 — LCR FAIL이 이미 있으니 판정은 FAIL, 그러나 이유엔 잘림과 깨진 줄이 같이 나온다:

```text
$ ./solution --text run_truncated.jsonl; echo "exit=$?"
SIV RESULT: FAIL pass=6 fail=1 error=0 skip=0 | run=nightly-1108-faulty board="EVB-UART rev B sn0042" fw=siv-0.3.1+g3f2a1c9 seed=0x5EED1234
REASON: 1 test(s) failed; no end record (hang/reset/lost log?); 1 malformed line(s), first: line 10 col 71: unterminated string
FAIL  uart.reset.LCR t=6us reg=LCR addr=0x4000100C exp=0x00000003 act=0x00000000 diff=0x00000003 bits=[1,0] msg="reset value mismatch"
WARN  t=0us VDD_IO 1.71 V, near min 1.70 V
exit=1
```

**헤더 필드가 각각 왜 있나.**

| 필드 | 없으면 생기는 일 |
|---|---|
| `seed` | 랜덤 패턴(여기선 `uart.rw.SCR` 의 5번째 패턴 = xorshift32(seed)) 실패를 재현 못 함 |
| `board` (rev + 시리얼) | "이 보드만 실패" 를 못 가림 — 보드 불량/소켓 접촉 vs 실리콘 결함 구분의 첫 단서 |
| `chip` (stepping A0/B0) | errata 는 stepping 별로 다름. A0 에서 고친 버그를 B0 리포트에서 다시 쫓게 됨 |
| `fw` (git hash, dirty 여부) | 테스트 코드가 바뀐 건지 실리콘이 바뀐 건지 모름 |
| `start` (UTC 벽시계) + 레코드별 `t_us` (부팅 후 단조 시간) | 보드엔 RTC가 없거나 틀린 경우가 많다. 벽시계는 호스트가 한 번만 찍고, 각 레코드는 단조 시간 — 스코프·전원 로그와 맞출 때 쓴다 |

**로그 레벨.** INFO = 진행, WARN = 판정엔 영향 없지만 사람이 봐야 함(VDD_IO가 하한 근처), ERR = 실패. 테스트 밖의 ERR(`type=log`)도 FAIL로 친다 — 예: "PMIC over-voltage"는 어느 테스트에도 속하지 않지만 그 런의 모든 결과를 의심하게 만든다. UVM도 같은 구조다: `UVM_INFO/WARNING/ERROR/FATAL` 심각도가 있고, 보통 시뮬 끝의 report summary 에서 ERROR/FATAL 개수가 0인지로 테스트 통과를 정한다.

**실무 연결.** pre-silicon 에선 UVM report server 가 이 역할을 한다(예: RAL 의 `uvm_reg_hw_reset_seq` 는 리셋값 불일치마다 UVM_ERROR 를 내고 summary 가 그 수를 센다), post-silicon 에선 보드의 C 테스트(JTAG/OpenOCD로 로드하거나 온칩 펌웨어로 실행)가 UART·semihosting·공유 메모리 링버퍼로 로그를 내보내고 호스트가 수집해 리포트한다. ATE(양산 테스터)는 STDF 같은 표준 datalog 를 쓰고, SLT/system validation 랩은 보통 자체 JSON/CSV 로그 + CI(Jenkins/GitLab의 JUnit XML, GitHub Actions의 `$GITHUB_STEP_SUMMARY` Markdown)로 간다. 형식은 달라도 **헤더 · 레코드 · 끝 표시 · 종료 코드** 네 가지 뼈대는 같다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution; echo "exit=$?"     # 46/46 passed, exit=0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 46/46 passed (경고 없음)
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `fail == 0` 이면 exit 0 | 잘린 로그·빈 런·전부 SKIP 이 초록불이 됨 | end 레코드·개수·PASS≥1·header 를 판정에 포함 |
| 재시도 결과를 마지막 값으로 덮어씀 | 간헐 실패(마진 버그)가 PASS로 숨음 | `if (attempts == 1 \|\| s > t->st)` sticky fail + `failed k/n attempts` |
| 모르는 status 를 무시/PASS 처리 | 새 FW가 `"TIMEOUT"` 을 내면 거짓 PASS | 모르는 status → ERROR |
| 깨진 줄 하나에 리포터가 `exit(1)` 로 죽음 | 나머지 수백 줄의 실패 정보도 잃음 | 깨진 줄은 세고 첫 위치만 기록, 계속 진행 → INVALID |
| 긴 msg 를 만나면 파싱 실패 처리 | FAIL 레코드 자체가 사라짐 | 쓰기만 멈추고 끝까지 읽기(`truncated=1`) |
| `\u` 처리에서 4글자 존재 확인 없이 `p += 4` | 줄 끝에서 버퍼 밖 읽기 (ASan이 잡음) | 한 글자씩 `hexval` 검사 후 이동 |
| 레지스터 값을 JSON 숫자(`1073745932`)로 기록 | 사람이 못 읽고, 일부 도구는 double로 읽어 큰 값 정밀도 손실 | `"0x4000100C"` 문자열 hex |
| Markdown 표 칸에 msg 를 그대로 | `\|` 가 열 구분자로 먹혀 표가 깨짐 | `sb_md_cell`: `\|` → `\\|`, 줄바꿈 → 공백 |
| `snprintf` 반환값을 그대로 `len += n` | 잘리면 n 이 남은 공간보다 커서 다음 쓰기가 버퍼 밖 | `n >= room` 이면 overflow 표시 후 `len = cap-1` |
| 레코드에 벽시계 시각만 기록 | 보드 RTC 미설정·NTP 점프로 순서가 뒤집힘 | 헤더에 UTC 시작 시각, 레코드엔 단조 `t_us` |

## 8. 면접 Q&A

<details>
<summary>Q1. 테스트가 timeout 났을 때 로그에 무엇을 남기나요?</summary>

무엇을 기다렸는지(레지스터·필드·기대값), timeout 값, 경과 시간, **마지막으로 읽은 값**, 폴링 횟수, 호출 위치(테스트 이름, 가능하면 `__FILE__:__LINE__`), 그리고 관련 상태 레지스터 덤프(클럭 게이트·리셋 컨트롤러·에러 상태). 이번 로그의 `timeout 200us waiting LSR.THRE=1, 20 polls, last LSR=0x00000000` 이 최소형이다. 꼬리질문 "last value가 왜 중요?" → 0x0 이면 블록이 안 깨어난 것(클럭/리셋), 0xDEADBEEF·0xFFFFFFFF 같은 값이면 버스 에러나 클럭 게이트된 블록 읽기일 가능성, 다른 비트가 선 값이면 에러 상태로 빠진 것 — 원인 분기가 완전히 달라진다.

</details>

<details>
<summary>Q2. 보드가 테스트 도중 리셋돼서 로그가 중간에 끊겼습니다. 리포트는 어떻게 판정해야 하나요?</summary>

PASS가 아니다. 끝 표시(end 레코드, TAP plan 같은 것)가 없으면 INCOMPLETE 로 nonzero 종료. 이미 FAIL이 있으면 판정은 FAIL이되 이유에 "no end record" 와 깨진 마지막 줄 위치를 같이 적는다. 꼬리질문 "그럼 어떤 테스트에서 죽었는지는?" → 마지막 정상 레코드 다음 테스트가 용의자. 그래서 프레임워크는 테스트 **시작** 레코드도 남기거나(`"type":"begin"`), watchdog 리셋 원인 레지스터(reset cause)를 다음 부팅 때 로그 첫 줄에 남긴다.

</details>

<details>
<summary>Q3. 재시도해서 통과한 테스트는 PASS 인가요?</summary>

검증에선 아니다(sticky fail). 1/2 실패는 마진 문제의 전형 — 셋업/홀드 타이밍, 전원 노이즈, PLL lock 시간, 온도 의존. 리포트에 `failed 1/2 attempts` 와 그때의 전압·온도·seed 를 남기고 shmoo 로 넘긴다. 꼬리질문 "CI가 flaky 때문에 계속 빨갛다면?" → 원인 확인 전까지 quarantine 목록에 올려 **별도 집계**하되 리포트에서 사라지게 하지는 않는다. 테스트 쪽 버그(타임아웃이 너무 짧음 등)로 판명되면 테스트를 고친다.

</details>

<details>
<summary>Q4. 왜 JSON Lines 인가요? 큰 JSON 배열 하나나 CSV 는요?</summary>

큰 배열은 마지막 `]` 가 써져야 유효한 JSON이라 중간에 죽으면 전체가 깨진다. 줄 단위면 죽기 직전 줄까지 살아 있고, `tail -f`/`grep` 이 되고, 스트리밍 처리(메모리 O(1))가 된다. CSV는 메시지 안 쉼표·따옴표 처리와 스키마 변경(열 추가)에 약하다. 평평한 객체로 제한하면 보드 쪽 producer 도 `printf` 몇 줄로 쓸 수 있고 리포터 파서도 재귀가 필요 없다. 꼬리질문 "로그 크기가 문제면?" → 바이너리 레코드(고정 크기 구조체 + 버전 필드)를 링버퍼에 쓰고 호스트가 디코드, 문자열은 ID 테이블로(trace 방식).

</details>

<details>
<summary>Q5. 종료 코드를 0/1 만 쓰나요? CI 에서 더 나눌 이유가 있나요?</summary>

이 레슨은 0(PASS)/1(그 외) 이지만, 실무에선 "제품이 실패"(1)와 "판정 불가 — rig/로그/환경 문제"(2)를 나누면 triage 가 빨라진다: 1은 검증 엔지니어에게, 2는 랩 인프라 담당에게 간다(추가 연습 1). pytest 도 0 통과, 1 실패, 2 중단, 3 내부 오류, 4 사용법 오류, 5 수집된 테스트 없음으로 나눈다. 꼬리질문 "셸 파이프에서 종료 코드가 사라지는데?" → `set -o pipefail` 또는 `${PIPESTATUS[0]}`(bash) — `./report run.jsonl | tee out.txt` 는 기본적으로 tee 의 종료 코드를 돌려준다.

</details>

<details>
<summary>Q6. 실패 리포트 한 줄에 꼭 있어야 하는 필드는? 왜 diff 와 비트 목록까지?</summary>

테스트 이름, 레지스터 이름과 **절대 주소**, 기대값, 실제값, (마스크가 있었다면 마스크), XOR diff, diff 비트 목록, 시각, seed. 주소는 같은 이름 레지스터가 인스턴스마다 다르기 때문(UART0/UART1). diff 비트는 패턴을 바로 보이게 한다: 한 비트만 계속 틀리면 stuck-at(SIV-B05), 주소 비트 하나와 연관되면 디코드 문제(SIV-B06), W1C 레지스터에서 쓴 비트 외에 다른 비트까지 바뀌면 access type 구현 오류(SIV-B04). 여러 런을 모으면 "항상 bit 5" 같은 시그니처로 같은 버그끼리 묶을(bucketing) 수 있다.

</details>

<details>
<summary>Q7. 로그 producer(보드)와 consumer(리포터) 버전이 다르면?</summary>

헤더에 로그 스키마 버전을 넣고, consumer 는 ① 모르는 레코드 type·모르는 키는 무시(앞으로 호환) ② 판정에 쓰는 필드(status)의 모르는 값은 ERROR(안전 쪽) ③ 필수 키가 없으면 깨진 줄. 이번 코드의 `unknown_types` 와 `status_from` 이 그 규칙이다. 꼬리질문 "키 이름을 바꾸고 싶다면?" → 한동안 두 이름을 다 쓰고(producer), consumer 가 둘 다 읽게 한 뒤 옛 이름을 제거.

</details>

## 9. 추가 연습

### 연습 1 — 3단 종료 코드

`exit_code3(summary)`: 0 = PASS, 1 = 제품 FAIL(테스트 실패/에러가 하나라도), 2 = 판정 불가(end 없음, 개수 불일치, 깨진 줄, header 없음, PASS 0개). FAIL 과 잘림이 같이 있으면?

<details>
<summary>답 보기</summary>

실패가 하나라도 있으면 1을 먼저 돌려준다 — 사람이 봐야 할 실제 결함이 있으므로 검증 엔지니어에게 가야 한다. 실패가 없을 때만 인프라 조건을 보고 2. 리포트의 이유 문자열엔 둘 다 남긴다(본문 `run_verdict` 와 같은 우선순위).

</details>

### 연습 2 — diff 비트를 범위로

`bit_ranges(0x000000F3) == "[7:4,1:0]"`, `0x80000001 → "[31,0]"`, `0xFFFFFFFF → "[31:0]"`, `0 → "[]"`. 32비트 전체가 틀린 레지스터(클럭 꺼진 블록이 0xFFFFFFFF 를 돌려줌)를 `bits=[31,30,...]` 로 쓰면 읽을 수가 없다.

<details>
<summary>답 보기</summary>

위 비트부터 내려가며 1의 연속 구간 `[hi, lo]` 를 찾는다: 1을 만나면 hi 기록, 0을 만날 때까지 내려간 뒤 lo = b+1. 길이 1이면 `hi`, 아니면 `hi:lo` 를 쉼표로 이어 쓴다. 레지스터 필드 표기(`[10:8]`)와 같은 모양이라 데이터시트와 바로 대조된다.

</details>

### 연습 3 — 베이스라인 대비 회귀

어제(base)와 오늘(cur) 결과로 new_fail(PASS→FAIL/ERROR), fixed(FAIL→PASS), still_fail, missing(어제 있었는데 오늘 없음), added 를 센다.

<details>
<summary>답 보기</summary>

base 를 돌며 cur 에서 이름을 찾는다. 없으면 **missing** — 테스트가 조용히 빠진 것도 회귀다(커버리지 손실; 필터 실수나 빌드 설정 변경이 흔한 원인). 있으면 bad 여부 조합으로 분류. cur 에만 있는 것은 added. 야간 리포트 맨 위에 "new fail" 만 따로 보여주면 매일 같은 still_fail 에 묻히지 않는다.

</details>

### 연습 4 — JUnit XML 한 줄

`junit_case("uart.w1c.ISR", FAIL, msg)` → `<testcase classname="uart.w1c" name="uart.w1c.ISR"><failure message="..."/></testcase>`. msg 의 `& < > " '` 를 escape.

<details>
<summary>답 보기</summary>

Jenkins·GitLab 등 CI가 테스트 결과 표로 읽는 사실상 표준 형식이 JUnit XML 이다. 속성값 안에서는 `&amp; &lt; &gt; &quot; &apos;` 로 바꾼다 — Markdown 의 `|` escape 와 같은 문제(출력 형식의 메타문자)다. classname 은 마지막 `.` 앞까지로 하면 CI 화면에서 블록별로 묶인다.

</details>

### 연습 5 — 소요 시간 회귀

PASS 인 테스트 중 시간이 베이스라인의 2배를 넘고 **증가량이 min_delta 이상**인 것을 찾는다. 왜 증가량 조건이 필요한가?

<details>
<summary>답 보기</summary>

2us → 5us 같은 작은 값은 비율로는 2.5배지만 잡음이다. 비율과 절대량을 같이 봐야 한다. PASS 인데 느려진 테스트는 "아직 안 터진 timeout" — 예: PLL lock·PHY link-up·캘리브레이션 시간이 늘어나는 건 다음 stepping 이나 고온에서 실패로 바뀐다. 곱셈은 `uint64_t` 로 해서 `2*was` 오버플로를 피한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice; echo "exit=$?"   # 15/15 passed, exit=0
```

## 10. 핵심 정리

- 로그는 **한 줄 = 평평한 JSON 객체 하나**(JSON Lines): 중간에 죽어도 앞 줄은 산다, grep 된다, 파서에 재귀가 필요 없다. 레지스터 값은 `"0x…"` 문자열.
- 파서는 깨진 줄의 **line/col/이유**를 남기고 계속 간다. 긴 값은 잘라도 레코드는 살린다. 중복 키·중첩·꼬리 쓰레기는 거부.
- 판정 우선순위 `FAIL > INCOMPLETE > INVALID > PASS`, 이유는 전부 기록. exit 0 은 PASS 뿐.
- **end 레코드(개수 포함)** 가 잘린 로그·유실된 줄을 잡는다. PASS 0개(전부 SKIP)는 통과가 아니다.
- 재시도는 **sticky fail** + `failed k/n attempts` — 간헐 실패는 마진 버그 신호.
- 모르는 type 은 무시(앞으로 호환), 모르는 status 는 ERROR(안전 쪽).
- 실패 한 줄 = 테스트·레지스터·절대 주소·exp·act·diff·비트·시각. 헤더 = seed·보드(rev/SN)·칩 stepping·FW hash·UTC 시작 시각.
- 출력 형식마다 메타문자 escape: Markdown `|`, XML `& < > " '`, JSON `" \ 제어문자`.

## 참고자료

- [RFC 8259 — The JavaScript Object Notation (JSON) Data Interchange Format](https://www.rfc-editor.org/rfc/rfc8259) (§4 이름 중복, §7 문자열 escape)
- [JSON Lines](https://jsonlines.org/) — 한 줄 한 레코드 형식
- [Test Anything Protocol (TAP) 명세](https://testanything.org/) — plan 줄 `1..N` 으로 누락 테스트 검출
- [pytest — Exit codes](https://docs.pytest.org/en/stable/reference/exit-codes.html)
- [GitHub Flavored Markdown — Tables](https://github.github.com/gfm/#tables-extension-) (칸 안의 `\|`)
- [GitHub Actions — Adding a job summary (`$GITHUB_STEP_SUMMARY`)](https://docs.github.com/en/actions/using-workflows/workflow-commands-for-github-actions#adding-a-job-summary)
- Accellera UVM 1.2 User's Guide / IEEE 1800.2 — report server, 심각도(INFO/WARNING/ERROR/FATAL)와 report summary
- SIV-B03(리셋값) · SIV-B04(access type) · SIV-B08(프레임워크·JSON 로그) · SIV-B09(poll timeout) · SIV-B10(bring-up) · TEST-B01(미니 테스트 프레임워크)
