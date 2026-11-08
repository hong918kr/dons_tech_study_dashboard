---
id: DS-B14
time: 50분
updated: 2026-11-07
---
# DS-B14 트라이(Trie): 명령어 파서와 자동완성

## 학습 목표

- 문자 하나 = 간선 하나인 **트라이(trie, prefix tree)** 를 정적 노드 풀과 `uint8_t` 인덱스로 `malloc` 없이 만든다.
- 노드마다 **서브트리 단어 수(`n_words`)** 를 유지해 "고유 접두사 축약(`st` → `status`)"과 "모호함(`re` → read/reboot/reset)"을 O(길이)에 판정한다.
- 사전순 DFS로 **탭 자동완성 후보 목록**과 **최장 공통 접두사(LCP)** 를 만든다.
- 실패한 삽입이 노드를 새지 않게 **검증 → 예약 → 변경** 순서로 짠다.
- 트라이 vs 정렬된 `const` 테이블(플래시)의 RAM/속도 트레이드오프를 숫자로 설명한다.

## 1. 문제

UART 디버그 셸은 거의 모든 펌웨어에 있다 — 보드 브링업, 생산 테스트, 필드 디버깅(`i2c_scan`, `loglevel 3`, `reboot`). Cisco IOS나 U-Boot처럼 **명령어를 줄여 써도 알아듣고(`sh` → `show`), 탭을 누르면 후보를 보여주는** 셸은 문자열 접두사 검색 문제이고, 그 정석 자료구조가 트라이다. 면접에서는 "autocomplete 설계", "prefix로 단어 찾기"(LeetCode 208/1268)로 나온다.

트라이는 문자열을 글자 단위로 쪼개 **공통 접두사를 공유하는 트리**로 저장한다. 루트에서 아래로 내려간 경로가 곧 문자열이고, 단어가 끝나는 노드에 표시(`cmd = id`)를 한다.

```svg
<svg viewBox="0 0 720 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">명령어 트라이 (일부) — 초록 = 단어 끝, w = 서브트리 단어 수 n_words</text>
  <g stroke="currentColor" fill="none">
    <line x1="30" y1="160" x2="90" y2="55"/><line x1="30" y1="160" x2="90" y2="160"/><line x1="30" y1="160" x2="90" y2="255"/>
    <line x1="90" y1="55" x2="150" y2="35"/><line x1="90" y1="55" x2="150" y2="85"/>
    <line x1="150" y1="35" x2="330" y2="35"/><line x1="150" y1="85" x2="390" y2="85"/>
    <line x1="90" y1="160" x2="150" y2="160"/>
    <line x1="150" y1="160" x2="210" y2="125"/><line x1="150" y1="160" x2="210" y2="160"/><line x1="150" y1="160" x2="210" y2="195"/>
    <line x1="210" y1="125" x2="270" y2="125"/><line x1="210" y1="160" x2="390" y2="160"/><line x1="210" y1="195" x2="330" y2="195"/>
    <line x1="90" y1="255" x2="510" y2="255"/>
  </g>
  <g stroke="currentColor" class="f-mut">
    <circle cx="30" cy="160" r="13" class="f-acc"/>
    <circle cx="90" cy="55" r="13"/><circle cx="150" cy="35" r="13"/><circle cx="210" cy="35" r="13"/><circle cx="270" cy="35" r="13"/>
    <circle cx="150" cy="85" r="13"/><circle cx="210" cy="85" r="13"/><circle cx="270" cy="85" r="13"/><circle cx="330" cy="85" r="13"/>
    <circle cx="90" cy="160" r="13"/><circle cx="150" cy="160" r="13"/><circle cx="210" cy="125" r="13"/>
    <circle cx="210" cy="160" r="13"/><circle cx="270" cy="160" r="13"/><circle cx="330" cy="160" r="13"/>
    <circle cx="210" cy="195" r="13"/><circle cx="270" cy="195" r="13"/>
    <circle cx="90" cy="255" r="13"/><circle cx="150" cy="255" r="13"/><circle cx="270" cy="255" r="13"/>
    <circle cx="330" cy="255" r="13"/><circle cx="390" cy="255" r="13"/><circle cx="450" cy="255" r="13"/>
  </g>
  <g stroke="currentColor">
    <circle cx="330" cy="35" r="13" class="f-ok"/><circle cx="390" cy="85" r="13" class="f-ok"/>
    <circle cx="270" cy="125" r="13" class="f-ok"/><circle cx="390" cy="160" r="13" class="f-ok"/>
    <circle cx="330" cy="195" r="13" class="f-ok"/><circle cx="210" cy="255" r="13" class="f-ok"/>
    <circle cx="510" cy="255" r="13" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="90" y="60">s</text><text x="150" y="40">l</text><text x="210" y="40">e</text><text x="270" y="40">e</text><text x="330" y="40">p</text>
    <text x="150" y="90">t</text><text x="210" y="90">a</text><text x="270" y="90">t</text><text x="330" y="90">u</text><text x="390" y="90">s</text>
    <text x="90" y="165">r</text><text x="150" y="165">e</text><text x="210" y="130">a</text><text x="270" y="130">d</text>
    <text x="210" y="165">b</text><text x="270" y="165">o</text><text x="330" y="165">o</text><text x="390" y="165">t</text>
    <text x="210" y="200">s</text><text x="270" y="200">e</text><text x="330" y="200">t</text>
    <text x="90" y="260">l</text><text x="150" y="260">o</text><text x="210" y="260">g</text><text x="270" y="260">l</text>
    <text x="330" y="260">e</text><text x="390" y="260">v</text><text x="450" y="260">e</text><text x="510" y="260">l</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="8" y="140">w=15</text><text x="72" y="78">w=2</text><text x="135" y="108">w=1</text>
    <text x="135" y="183">w=3</text><text x="196" y="280">w=2</text><text x="258" y="280">w=1</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="440" y="40">"st" → t 노드 w=1 → 외길 → status (UNIQUE)</text>
    <text x="440" y="130">"re" → e 노드 w=3 → 모호 (AMBIG)</text>
    <text x="440" y="150">  탭: read reboot reset</text>
    <text x="440" y="200">"log" → g 노드가 단어 끝</text>
    <text x="440" y="220">  → 정확 일치 우선 (EXACT)</text>
  </g>
  <text x="10" y="305" fill="currentColor">(h, w, i2c_…, gpio, v… 가지는 생략) 전체 15개 명령어 → 노드 71개 (루트 포함).</text>
  <text x="10" y="324" fill="currentColor">탐색 비용은 명령어 개수가 아니라 입력 길이에 비례: 노드마다 child[문자] 배열을 한 번 인덱싱.</text>
</svg>
```

이 레슨의 명령어 집합 (id = 배열 인덱스):

```text
0 help   1 status  2 sleep  3 reset  4 reboot  5 read  6 write  7 log
8 loglevel  9 i2c_read  10 i2c_write  11 i2c_scan  12 gpio  13 version  14 verbose
```

허용 문자는 `0-9`, `_`, `a-z` 37개. 인덱스를 **ASCII 순서대로**(`'0'..'9'` → 0..9, `'_'` → 10, `'a'..'z'` → 11..36) 매기면 자식을 0번부터 도는 DFS가 곧 **사전순**이 된다. 노드는 `child[37]`(`uint8_t` 인덱스, 0 = 없음) + `n_words` + `cmd` 로 39바이트, 풀 128개 = 4992바이트.

### Part A — 삽입 · 경로 따라가기 · 정확 일치

- `trie_insert(w, id)` — 성공하면 `true`. 빈 문자열, 15자 초과, 허용 안 되는 문자(`"Bad!"`), 중복, 풀 부족이면 `false` 이고 **트라이와 풀이 전혀 바뀌지 않아야 한다** (`g_used` 그대로).
- `trie_walk(s)` — `s` 를 따라 내려간 노드 인덱스, 없으면 `-1`. `""` 은 루트(0).
- `trie_exact(s)` — 정확히 일치하는 명령어 id, 없으면 `-1`.
- 삽입하면서 경로의 모든 노드(루트 포함)의 `n_words` 를 1 올린다.

### Part B — 고유 접두사 축약

`trie_resolve(in, &id)` 가 네 가지 중 하나를 돌려준다.

| 결과 | 조건 | 예 |
|---|---|---|
| `R_EXACT` | 입력이 명령어 그 자체 (다른 명령어의 접두사여도 **우선**) | `log`, `status` |
| `R_UNIQUE` | 입력 아래 명령어가 정확히 1개 | `st` → status, `logl` → loglevel, `h` → help |
| `R_AMBIG` | 입력 아래 명령어가 2개 이상 | `s`, `re`, `lo`, `i2c` |
| `R_NONE` | 경로 없음, 빈 입력, 허용 안 되는 문자 | `x`, `statusx`, `""` |

하네스의 `shell_line()` 은 이 결과로 `"run status"`, `"ambiguous 're': read reboot reset"`, `"unknown 'frob'"` 같은 응답을 만든다 (모호할 때 후보를 보여주려고 Part C를 쓴다).

### Part C — 탭 자동완성

- `trie_complete(prefix, out, max)` — 접두사로 시작하는 명령어를 **사전순**으로 `out` 에 최대 `max` 개 기록하고, **전체 개수**를 돌려준다 (`snprintf` 처럼 잘렸는지 호출자가 알 수 있게). `out[max]` 이후는 건드리면 안 된다.
- `trie_lcp(prefix, out)` — bash처럼 탭 한 번에 **최장 공통 접두사**까지 채운다: `i` → `i2c_`, `v` → `ver`, `he` → `help`. 단어 끝(`log`)이나 갈림길(`re`)에서 멈춘다.

## 2. Problem (English)

> **Command-line abbreviation and tab completion for a debug shell.**
>
> Build a trie over the alphabet `[0-9_a-z]` using a static pool of 128 nodes (no heap). Each node stores child indices (`uint8_t`, 0 = none), the number of words in its subtree, and the command id ending there.
>
> 1. `bool trie_insert(const char *w, int id)` — reject empty, too long (> 15), invalid characters, duplicates, or insufficient pool space **without modifying the trie**. `int trie_walk(const char *s)`, `int trie_exact(const char *s)`.
> 2. `Res trie_resolve(const char *in, int *id)` returning `EXACT` (exact match wins even if it is a prefix of another command), `UNIQUE` (prefix of exactly one command), `AMBIG`, or `NONE`. Must run in O(|in| + |answer|·Σ), independent of the number of commands.
> 3. `int trie_complete(const char *prefix, char out[][16], int max)` — candidates in lexicographic order, return the total count; `bool trie_lcp(const char *prefix, char out[16])` — extend to the longest common prefix.
>
> Example: `resolve("st") → UNIQUE status`, `resolve("re") → AMBIG`, `complete("i2c_") → {i2c_read, i2c_scan, i2c_write}`, `lcp("v") → "ver"`.
>
> Follow-ups: How much RAM does this take, and how would you shrink it? Would you even use a trie for 15 commands on an MCU?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "12/37 passed" — TODO 7곳(insert, walk, exact, resolve, collect, complete, lcp)을 채워 37/37
```

```bash
# 문자열·배열 경계 실수는 ASan 으로 바로 잡힌다
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined solution.c -o sol_asan && ./sol_asan
```

## 4. 힌트

<details>
<summary>힌트 1 — walk 는 child[] 를 한 글자씩 인덱싱</summary>

`n = 0; for each char: c = char_index(ch); if (c < 0 || !g_node[n].child[c]) return -1; n = g_node[n].child[c];`. 자식 인덱스 0이 "없음"으로 쓰일 수 있는 이유: 0번은 루트라서 누구의 자식도 될 수 없다.

</details>

<details>
<summary>힌트 2 — insert 는 "검증 → 예약 → 변경"</summary>

글자를 하나씩 보며 노드를 만들다가 5번째 글자에서 `'!'` 를 만나 `false` 를 돌려주면, 앞에서 만든 4개 노드가 아무 단어에도 속하지 않은 채 풀에 남는다(누수). 그래서 ① 모든 글자를 먼저 검사하고 ② 남은 노드가 `len` 개 이상인지 확인한 다음 ③ 실제로 만든다. 중복 검사는 경로 끝에서 — 중복이면 경로가 이미 다 있으니 새 노드도 안 생긴다.

</details>

<details>
<summary>힌트 3 — n_words 는 성공이 확정된 뒤에 올린다</summary>

내려가면서 바로 `n_words++` 하면 중복일 때 되돌려야 한다. `cmd` 를 기록한 뒤 경로를 한 번 더 내려가며 올리면 깔끔하다 (길이 15짜리 두 번 순회는 공짜).

</details>

<details>
<summary>힌트 4 — UNIQUE 판정에 DFS가 필요 없다</summary>

접두사 노드의 `n_words == 1` 이면 그 아래는 **외길**이다. 단어 끝을 만날 때까지 "단어가 있는 유일한 자식"을 따라 내려가면 된다. 순서: 정확 일치(`cmd >= 0`) 먼저 → `n_words > 1` 이면 AMBIG → 아니면 외길.

</details>

<details>
<summary>힌트 5 — 후보 목록은 "자신 먼저, 자식은 인덱스 순"</summary>

`collect(n, buf, depth)`: `cmd >= 0` 이면 `buf[0..depth)` 를 기록(그래서 `log` 가 `loglevel` 보다 먼저), 그다음 `c = 0..36` 순서로 `buf[depth] = index_char(c)` 하고 재귀. 개수는 항상 세고, 기록은 `cnt < max` 일 때만.

</details>

## 5. 풀이

### Part A — 노드 구조와 안전한 삽입

| 필드 | 크기 | 역할 |
|---|---|---|
| `child[37]` | 37 B | 글자 → 자식 노드 인덱스. `uint8_t` 라 풀은 최대 255개. 0 = 없음 |
| `n_words` | 1 B | 서브트리(자신 포함)에서 끝나는 단어 수 — Part B·C 의 핵심 |
| `cmd` | 1 B | 이 노드에서 끝나는 명령어 id, 아니면 -1 |

포인터(`TNode *child[37]`)였다면 32비트 MCU에서 노드당 148B, 64비트 PC에선 296B — 인덱스로 바꾸는 것만으로 4~8배 작아진다. 실제 실행 결과:

```text
  commands=15  nodes=71/128  sizeof(TNode)=39  pool bytes=4992
```

15개 명령어의 글자 수 합은 89인데 노드는 70개(루트 제외)다. `re`·`i2c_`·`log`·`ver` 같은 공통 접두사를 공유해서 19개를 아꼈다. 트라이가 빛나는 건 접두사 공유가 많을 때다.

**삽입이 실패해도 트라이가 그대로여야 하는 이유**: 셸 명령어는 보통 부팅 시 모듈별로 등록된다(`SHELL_CMD_REGISTER`). 등록 하나가 실패했다고 다른 명령어 판정이 망가지면(유령 노드가 `n_words` 를 틀리게 하거나 풀을 먹으면) 디버깅 도구 자체를 못 믿게 된다. "검증 → 예약 → 변경"은 플래시 쓰기, 설정 테이블 갱신 등 **부분 실패가 위험한 모든 곳**에서 쓰는 패턴이다(트랜잭션의 가장 단순한 형태).

### Part B — 판정 순서가 곧 정책이다

```svg
<svg viewBox="0 0 720 210" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">trie_resolve(in) 판정 흐름</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="110" height="40" rx="6" class="f-mut"/>
    <rect x="160" y="40" width="120" height="40" rx="6" class="f-mut"/>
    <rect x="320" y="40" width="120" height="40" rx="6" class="f-mut"/>
    <rect x="480" y="40" width="120" height="40" rx="6" class="f-mut"/>
    <rect x="160" y="130" width="120" height="36" rx="6" class="f-warn"/>
    <rect x="320" y="130" width="120" height="36" rx="6" class="f-ok"/>
    <rect x="480" y="130" width="120" height="36" rx="6" class="f-warn"/>
    <rect x="630" y="40" width="80" height="40" rx="6" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="65" y="65">walk(in)</text>
    <text x="220" y="65">노드 있음?</text>
    <text x="380" y="65">cmd ≥ 0 ?</text>
    <text x="540" y="65">n_words &gt; 1 ?</text>
    <text x="220" y="153">R_NONE</text>
    <text x="380" y="153">R_EXACT</text>
    <text x="540" y="153">R_AMBIG</text>
    <text x="670" y="58">외길 따라</text><text x="670" y="74">R_UNIQUE</text>
  </g>
  <g stroke="currentColor" fill="none">
    <line x1="120" y1="60" x2="158" y2="60"/><line x1="280" y1="60" x2="318" y2="60"/>
    <line x1="440" y1="60" x2="478" y2="60"/><line x1="600" y1="60" x2="628" y2="60"/>
    <line x1="220" y1="80" x2="220" y2="128"/><line x1="380" y1="80" x2="380" y2="128"/><line x1="540" y1="80" x2="540" y2="128"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="285" y="54">예</text><text x="445" y="54">아니오</text><text x="604" y="54">아니오</text>
    <text x="226" y="108">아니오 (-1)</text><text x="386" y="108">예</text><text x="546" y="108">예</text>
  </g>
  <text x="10" y="195" fill="currentColor">모든 단계가 O(1) 또는 O(길이) — 명령어가 15개든 1500개든 판정 비용은 입력 길이로만 정해진다.</text>
</svg>
```

입력별 추적:

| 입력 | walk 결과 노드 | `cmd` | `n_words` | 판정 |
|---|---|---|---|---|
| `st` | s→t | -1 | 1 | 외길 a→t→u→s → **UNIQUE status** |
| `s` | s | -1 | 2 (sleep, status) | **AMBIG** |
| `re` | r→e | -1 | 3 | **AMBIG** → 후보 read reboot reset |
| `log` | l→o→g | 7 | 2 | **EXACT log** (loglevel 이 있어도) |
| `logl` | …→g→l | -1 | 1 | **UNIQUE loglevel** |
| `lo` | l→o | -1 | 2 | **AMBIG** |
| `statusx` | (s 다음 x 없음) | — | — | **NONE** |
| `STATUS` | `'S'` 는 허용 문자 아님 | — | — | **NONE** (추가 연습 1에서 대소문자 무시) |

**정확 일치 우선**이 없으면 `log` 를 영원히 실행할 수 없다(항상 `log`/`loglevel` 모호). Cisco IOS, U-Boot, Zephyr shell 모두 같은 규칙이다. 반대 방향 함정: 명령어 집합에 나중에 `stats` 를 추가하면 어제까지 되던 `st` 가 모호해진다 — 스크립트나 생산 테스트 지그가 축약어를 쓰고 있었다면 깨진다. 그래서 **자동화 스크립트에는 항상 전체 이름**을 쓰라고 문서화한다.

### Part C — 사전순 DFS와 최장 공통 접두사

`trie_complete("", out, 4)` 의 DFS 진행 (인덱스 순서 = ASCII 순서이므로 `g` < `h` < `i` …):

| 방문 순서 | 기록 | cnt | `out` 에 쓰나 (max=4) |
|---|---|---|---|
| g→p→i→o | `gpio` | 1 | ✓ out[0] |
| h→e→l→p | `help` | 2 | ✓ out[1] |
| i→2→c→_→r→e→a→d | `i2c_read` | 3 | ✓ out[2] |
| …→_→s→c→a→n | `i2c_scan` | 4 | ✓ out[3] |
| …→_→w→r→i→t→e | `i2c_write` | 5 | ✗ (세기만) |
| … | … | 15 | ✗ |

돌려준 값 15 > max 4 → 호출자는 "후보 15개 중 4개만 표시, `--more--`" 같은 처리를 할 수 있다. 테스트는 `out[4]` 에 넣어 둔 `"SENTINEL"` 이 그대로인지로 **버퍼 넘침이 없음**을 확인한다.

`trie_lcp` 는 더 단순하다: 현재 노드가 단어 끝이 아니고 자식이 정확히 1개인 동안 그 글자를 붙이며 내려간다.

| 입력 | 내려간 경로 | 멈춘 이유 | 결과 |
|---|---|---|---|
| `i` | 2 → c → _ | `_` 아래 r/s/w 갈림길 | `i2c_` |
| `v` | e → r | `r` 아래 b/s 갈림길 | `ver` |
| `he` | l → p | `p` 가 단어 끝 | `help` |
| `log` | (없음) | `g` 가 단어 끝 | `log` |
| `re` | (없음) | `e` 아래 a/b/s 갈림길 | `re` |

실제 셸의 탭 동작 = ① `lcp` 로 채울 수 있으면 채우고 ② 더 못 채우는데 후보가 여럿이면 두 번째 탭에 `complete` 목록 출력. 하네스 실행 예:

```text
  > re 0x40  -> ambiguous 're': read reboot reset
```

**재귀 깊이**: `collect` 의 재귀 깊이는 최대 명령어 길이(15)로 묶여 있다. DS-B13의 BST 재귀(깊이 = 높이, 최악 N)와 달리 **입력 데이터가 아니라 설계 상수가 깊이를 정하므로** 스택 사용량을 정적으로 계산할 수 있다. 그래도 MISRA 프로젝트라면 `buf` 와 명시적 스택으로 바꾼다.

### 트라이 vs 정렬 테이블 — 이 크기에선 무엇을 쓸까

| 방식 | RAM | Flash | 판정 비용 | 명령어 추가 |
|---|---|---|---|---|
| 배열 자식 트라이 (이 레슨) | 4992 B (풀 128) / 실사용 71×39 = 2769 B | 코드만 | O(길이) | 런타임 등록 가능 |
| LCRS 트라이 (첫 자식 + 형제) | 노드당 4~5 B (자식·형제 인덱스 + 글자 + cmd) | 코드만 | O(길이 × 형제 수) | 런타임 가능 |
| 정렬된 `const char *const[]` + 이진 탐색 | **0 B** | 문자열 104 B + 포인터 60 B | O(log N × 길이) | 빌드 타임 (링커 섹션 정렬) |

명령어 15개짜리 MCU 셸이라면 **정렬 테이블이 정답에 가깝다** (RAM 0, 코드 단순). 실제로 Zephyr shell은 링커 섹션에 모은 정적 명령어 테이블을 쓰고, U-Boot도 `U_BOOT_CMD` 로 링커 리스트를 만든다. 트라이는 ① 명령어가 수백~수천 개 ② 런타임에 등록·삭제 ③ 접두사 질의가 매우 잦을 때(라우팅 테이블의 longest prefix match, 키보드 자동완성 사전) 이긴다. 면접에서는 트라이를 구현한 뒤 **"이 규모면 저는 정렬 배열을 쓰겠습니다"** 라고 말할 수 있으면 가산점이다 (추가 연습 3에서 두 방식의 판정이 모든 접두사에서 같은지 교차 검증한다).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 37/37 passed
cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined solution.c -o sol_asan && ./sol_asan   # ASan/UBSan 깨끗
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 글자를 보면서 노드를 만들다 잘못된 문자에서 `return false` | 이미 만든 노드가 풀에 남아 누수, `n_words` 도 어긋날 수 있음 | 검증 루프를 먼저 → 예약 확인 → 생성 |
| 정확 일치 검사 없이 `n_words` 만 봄 | `log` 가 항상 모호 → 실행 불가 | `cmd >= 0` 이면 먼저 EXACT |
| `char_index(c)` 결과 `-1` 을 검사하지 않고 `child[c]` | `child[-1]` 은 배열 밖 접근(UB) — ASan이 바로 잡는다 | `if (c < 0) return -1;` |
| `c - 'a'` 만 쓰고 `'_'`, 숫자 처리 안 함 | `i2c_scan` 의 `'2'` 가 음수 인덱스 | 명시적 매핑 함수 + 허용 문자 목록 |
| 문자 매핑 순서를 ASCII와 다르게 (예: a-z 먼저, 숫자 나중) | DFS 결과가 사전순이 아님 → `strcmp` 정렬과 불일치 | 인덱스 순서 = 문자 코드 순서 |
| `complete` 가 `max` 를 넘어서도 `out` 에 씀 | 호출자 버퍼 오버플로 (스택 깨짐) | 개수는 세고, 기록은 `cnt < max` 일 때만 |
| `buf[depth]` 에 `'\0'` 안 붙이고 `strcpy` | 이전 후보의 꼬리가 섞여 나옴 (`reboot` 다음에 `reset` → `resett`) | `memcpy` 후 `out[cnt][depth] = '\0'` |
| 자식 0 = 없음인데 루트를 1번에 두고 0번을 일반 노드로 사용 | 0번 노드를 가리키는 링크가 "없음"과 구분 안 됨 | 0번을 루트로 고정 (루트는 누구의 자식도 아님) |

## 8. 면접 Q&A

<details>
<summary>Q1. 트라이의 시간·공간 복잡도는? 해시 테이블과 비교하면?</summary>

삽입·탐색 O(L) (L = 문자열 길이), 단어 수 N과 무관. 공간은 최악 O(총 글자 수 × Σ) (Σ = 알파벳 크기) — 배열 자식이면 대부분이 빈 칸이라 낭비가 크다. 해시는 정확 일치 평균 O(L)(해시 계산)이지만 **접두사 질의와 사전순 나열을 못 한다**. 자동완성·축약·longest prefix match가 필요하면 트라이, 정확 일치만이면 해시.

</details>

<details>
<summary>Q2. 노드당 메모리를 줄이는 방법을 말해보세요.</summary>

① 포인터 대신 `uint8_t`/`uint16_t` 인덱스 (이 레슨: 148B → 39B) ② **LCRS**(left-child right-sibling): 노드에 `first_child`, `next_sibling`, `ch` 만 → 노드당 3~4B, 대신 자식 찾기가 형제 리스트 선형 탐색 ③ 비트맵 + 압축 자식 배열(`popcount` 로 위치 계산, HAMT 방식) ④ **radix/Patricia 트리**: 외길 체인(`l-e-v-e-l`)을 간선 하나의 문자열로 압축 → 노드 수가 단어 수 수준으로 줄어든다 ⑤ 알파벳 축소(명령어는 소문자만 등).

</details>

<details>
<summary>Q3. "st" 처럼 축약을 허용하면 어떤 운영상의 위험이 있나요?</summary>

명령어가 추가되면 기존 축약이 모호해지거나(`stats` 추가 → `st` 모호) 더 나쁘게는 **다른 명령어로 해석**될 수 있다(정확 일치가 새로 생기는 경우). 생산 테스트 스크립트가 축약어를 쓰면 펌웨어 업데이트 후 조용히 다른 동작을 한다. 대책: 자동화에는 전체 이름만, 위험한 명령(`erase`, `reboot`)은 축약 금지 플래그 또는 확인 프롬프트, 릴리스 노트에 명령어 추가 명시.

</details>

<details>
<summary>Q4. 15개 명령어짜리 MCU 셸에도 트라이를 쓰겠습니까?</summary>

아마 아니다. 정렬된 `const` 테이블을 플래시에 두고 이진 탐색(lower_bound) 후 접두사가 같은 연속 구간의 길이로 EXACT/UNIQUE/AMBIG을 판정하면 RAM 0바이트로 같은 결과가 나온다 (추가 연습 3). 명령어 수백 개 이상, 런타임 등록·삭제, 매 키 입력마다 후보 갱신 같은 요구가 있을 때 트라이를 고른다. "구현할 줄 알지만 규모에 맞게 고른다"가 면접관이 듣고 싶은 답이다.

</details>

<details>
<summary>Q5. UART 셸을 ISR과 어떻게 연결하나요? 트라이 탐색을 ISR에서 해도 되나요?</summary>

RX ISR은 바이트를 링 버퍼(DS-B01)에 넣고 끝낸다. 메인 루프나 셸 태스크가 줄 단위로 모아 `'\r'`/`'\n'` 에서 파싱·해석·실행한다. 탭(`'\t'`)도 셸 태스크에서 처리. ISR에서 트라이를 돌리면 짧긴 해도 핸들러 실행까지 이어지기 쉬워 지연이 예측 불가해진다. 트라이 자체는 부팅 후 읽기 전용이면 락이 필요 없지만, 런타임 등록이 있으면 등록과 탐색을 같은 태스크에서 하거나 뮤텍스로 보호.

</details>

<details>
<summary>Q6. 트라이에서 단어를 삭제하려면?</summary>

경로를 기록하며 내려가 `cmd = -1`, 경로의 `n_words` 를 1씩 내리고, **아래에서부터** `n_words == 0` 이 된 노드를 부모의 `child[]` 에서 떼어 풀에 반납한다 (추가 연습 2: `loglevel` 삭제 시 `l-e-v-e-l` 5개 반납, `log` 노드는 남음). 반납하려면 풀에 free list가 필요하다 — 본문 풀은 bump 할당기(`g_used++`)라 반납이 안 된다.

</details>

<details>
<summary>Q7. 라우터의 longest prefix match(LPM)와 트라이의 관계는?</summary>

IP 라우팅 테이블은 비트 단위(Σ = 2) 트라이에서 "가장 깊이 내려간 곳의 마지막 표시 노드"를 찾는 문제다. 이 레슨의 `walk` 를 "갈 수 있을 때까지 가며 마지막 `cmd >= 0` 을 기억"으로 바꾸면 LPM이 된다. 실제로는 Patricia 트리나 multibit trie(한 번에 4~8비트)로 메모리 접근 횟수를 줄이고, 하드웨어는 TCAM을 쓴다.

</details>

## 9. 추가 연습

### 연습 1 — 대소문자 무시

`STATUS`, `Status`, `LogL` 도 받아들이게 하라. 트라이 크기는 늘리지 말 것. `HELP` 삽입은 `help` 와 중복으로 거부돼야 한다.

<details>
<summary>답 보기</summary>

`char_index()` 에서 `'A'..'Z'` 를 소문자로 접는다(fold). 삽입·탐색이 같은 매핑을 쓰므로 트라이는 소문자만 저장하고, 대문자 입력은 같은 경로를 탄다. 노드를 따로 만들면(알파벳 63개) 메모리가 늘고 `Help`/`help` 가 다른 명령이 되는 혼란이 생긴다.

</details>

### 연습 2 — 명령어 제거와 노드 반납

free list가 있는 풀로 바꾸고 `trie_remove(w)` 를 구현하라. `loglevel` 제거 후 `logl` 은 NONE, `lo` 는 이제 UNIQUE(log)여야 한다. 반납된 노드는 다음 삽입에서 재사용돼야 한다.

<details>
<summary>답 보기</summary>

경로 노드 인덱스와 글자 인덱스를 배열에 기록하며 내려간다(재귀 없이). `cmd = -1`, 경로 전체 `n_words--`, 그다음 깊은 쪽부터 `n_words == 0` 인 노드를 부모 `child[c] = 0` 으로 끊고 반납 — 처음으로 `n_words > 0` 인 노드에서 멈춘다(다른 단어가 쓰는 중). `loglevel` → 5개, 이어서 `log` → 3개 반납. free list는 반납된 인덱스의 스택(DS-B07 고정 크기 풀과 같은 아이디어).

</details>

### 연습 3 — 정렬된 const 테이블로 같은 판정 (RAM 0)

사전순 `const char *const SORTED[]` 와 이진 탐색으로 `table_resolve()` 를 만들고, **모든 명령어의 모든 접두사**와 잡음 입력에서 트라이 판정과 같은지 교차 검증하라.

<details>
<summary>답 보기</summary>

`lower_bound(in)` = 첫 `SORTED[i] >= in`. 같은 문자열이면 EXACT. 아니면 `i` 부터 `in` 으로 시작하는 항목이 연속으로 몇 개인지 센다 (사전순이라 접두사가 같은 항목은 반드시 연속) — 0개 NONE, 1개 UNIQUE, 2개 이상 AMBIG. 교차 검증(differential testing)은 "느리지만 확실한 구현"과 "빠른 구현"을 같은 입력 집합에 돌려 비교하는 기법으로, DS-B13의 참조 모델 테스트와 같은 정신이다.

</details>

### 연습 4 — 토크나이저 + 디스패치

`"  i2c_s   1 "` 같은 한 줄을 `malloc` 없이 제자리에서 `argv[]` 로 쪼개고(최대 4개), 첫 단어를 축약 해석해 함수 포인터 테이블로 핸들러를 부르라. `loglevel` 핸들러는 0~7만 허용하고 범위 밖이면 상태를 바꾸지 않는다.

<details>
<summary>답 보기</summary>

공백을 `'\0'` 으로 덮어쓰며 각 토큰의 시작 주소를 `argv` 에 저장한다(`strtok` 는 내부 정적 상태가 있어 재진입 불가라 피함). 인자 수 초과는 조용히 자르지 말고 에러. 숫자 파싱은 `atoi`(에러 검출 불가) 대신 직접 자릿수를 검사하고 범위를 넘는 순간 거부 — 셸 입력은 **신뢰할 수 없는 외부 입력**으로 다룬다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 18/18 passed
```

## 10. 핵심 정리

- 트라이 = 글자 하나가 간선 하나. 탐색·삽입 O(길이), 명령어 개수와 무관. 공통 접두사는 노드를 공유 (89글자 → 70노드).
- 정적 풀 + `uint8_t` 자식 인덱스(0 = 없음, 0번은 루트) → 노드 39B. 포인터 대비 4~8배 절약.
- 노드마다 `n_words` 를 두면 축약 판정이 O(길이): 정확 일치 우선 → `n_words > 1` AMBIG → 외길 UNIQUE.
- 문자 인덱스를 ASCII 순서로 매기면 DFS = 사전순 후보 목록. 개수는 다 세고 기록은 `max` 까지 (`snprintf` 규약).
- LCP: 단어 끝도 갈림길도 아닌 동안 내려간다 → 탭 한 번 = `i` → `i2c_`.
- 삽입은 검증 → 예약 → 변경. 실패해도 구조와 풀은 그대로.
- 작은 MCU 셸은 정렬된 `const` 테이블 + 이진 탐색(RAM 0)이 보통 더 낫다. 트라이는 규모·런타임 등록·잦은 접두사 질의에서 이긴다.

## 참고자료

- [Zephyr Project — Shell subsystem (명령어 등록, 탭 자동완성, 축약)](https://docs.zephyrproject.org/latest/services/shell/index.html)
- [U-Boot — Implementing shell commands (`U_BOOT_CMD`, 링커 리스트)](https://docs.u-boot.org/en/latest/develop/commands.html)
- Robert Sedgewick & Kevin Wayne, *Algorithms* 4판 5.2절 Tries (R-way trie, TST)
- Donald R. Morrison, "PATRICIA — Practical Algorithm To Retrieve Information Coded in Alphanumeric", JACM 1968
- [LeetCode 208 Implement Trie / 1268 Search Suggestions System](https://leetcode.com/problems/implement-trie-prefix-tree/) — 면접 단골 변형
- Cisco IOS CLI — 명령어 축약(unique abbreviation) 규칙
