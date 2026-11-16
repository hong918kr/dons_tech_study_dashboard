---
id: MEM-B09
time: 70분
updated: 2026-11-15
---
# MEM-B09 March 테스트: MATS+, March C-

## 학습 목표

- March 표기 문자열 `{⇕(w0); ⇑(r0,w1); ⇓(r1,w0)}` 의 **문법(grammar)** 을 정하고, ASCII 별칭(`^`/`up`, `v`/`down`, `*`/`any`)까지 받는 파서를 짜서 오류를 **바이트 위치**와 함께 돌려준다.
- 알고리즘을 코드가 아니라 **데이터(문자열 → element 리스트)** 로 두고, 엔진 하나로 MATS+ · MATS++ · March X · March C- · March B 를 돌린다 (알고리즘별 루프 하드코딩 없음).
- 비트 단위 결함 주입 메모리에 SAF · TF · CFin(aggressor 아래/위 × ↑/↓) · CFid · CFst · AF(alias / no-access / multi-access) 의 **모든 인스턴스**를 넣고, "보장 검출(guaranteed detection)" 기준으로 **알고리즘 × 결함 모델 커버리지 표**를 직접 만든다.
- 측정 표를 van de Goor 의 문헌 주장과 칸 단위로 맞춰 보고, 내 모델이 문헌과 **어디서 다른지** 말한다 (⇕ 해석, AF 모델, linked fault).
- bring-up 콘솔처럼: 문자열을 실행 전에 **의미 검증**(read 기대값 일관성)하고, word 메모리에서 `FAIL addr=.. exp=.. act=.. diff=.. bits=[..]` 리포트와 시간 추정(k·n)을 낸다.

## 1. 문제

새 보드/새 실리콘의 SRAM·DRAM 을 처음 켤 때 "어떤 March 를 왜 돌리나, 그게 뭘 못 잡나" 는 실리콘 검증·펌웨어 면접의 단골 질문이고, 답은 "커버리지 표를 숫자로" 해야 설득력이 있다.

SIV-B07 에서 March C- 를 **element 테이블 + 엔진** 으로 짜고 word SRAM 에 data background 를 붙였다. MEM-B08 (병렬 작성 중)은 Barr 식 3단계 bring-up 테스트(data bus → address bus → device)다. 이 레슨의 몫은 그 사이:

1. **표기법 자체를 입력으로 받는 인터프리터(interpreter).** 상용 MBIST(Memory Built-In Self-Test) 컨트롤러가 알고리즘을 "프로그래밍 가능" 하게 만드는 것, 그리고 bring-up 콘솔에서 `memtest march "{...}"` 처럼 새 알고리즘을 재빌드 없이 넣는 것과 같은 발상이다.
2. **여러 알고리즘 × 여러 결함 모델 커버리지 행렬(coverage matrix).** 문헌 표를 외우는 대신 직접 재서 "MATS+ 는 왜 TF↓ 가 0% 인가", "March C- 와 March B 의 차이는 어디서 보이나" 를 측정으로 말한다.

Don 의 SSD 경험과 연결하면: NAND 컨트롤러의 내부 SRAM(매핑 테이블 캐시, 데이터 버퍼)도 양산에서 MBIST 의 March 계열로 검사되고, 펌웨어는 부팅 때 같은 패턴을 시스템 경로로 다시 돌린다. DRAM 쪽에서는 March 가 그대로가 아니라 refresh·주소 scramble 을 고려한 변형으로 쓰인다 (§8 Q7).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">문자열 → element 리스트 → 엔진 × 결함 메모리 × power-up → 커버리지 표</text>
  <rect x="10" y="36" width="720" height="34" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="20" y="58" fill="currentColor" font-family="monospace" font-size="13">"{⇕(w0); ⇑(r0,w1); ⇑(r1,w0); ⇓(r0,w1); ⇓(r1,w0); ⇕(r0)}"</text>
  <path d="M370 72 L370 92" stroke="currentColor" stroke-width="2"/><path d="M364 86 L370 94 L376 86" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="380" y="88" fill="currentColor" font-size="12">march_parse()  — 오류면 code + 바이트 pos</text>
  <g stroke="currentColor">
    <rect x="10" y="100" width="112" height="46" rx="5" class="f-mut"/>
    <rect x="130" y="100" width="112" height="46" rx="5" class="f-acc"/>
    <rect x="250" y="100" width="112" height="46" rx="5" class="f-acc"/>
    <rect x="370" y="100" width="112" height="46" rx="5" class="f-warn"/>
    <rect x="490" y="100" width="112" height="46" rx="5" class="f-warn"/>
    <rect x="610" y="100" width="120" height="46" rx="5" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="66" y="119">e0 ANY</text><text x="66" y="137">{W0}</text>
    <text x="186" y="119">e1 UP</text><text x="186" y="137">{R0,W1}</text>
    <text x="306" y="119">e2 UP</text><text x="306" y="137">{R1,W0}</text>
    <text x="426" y="119">e3 DOWN</text><text x="426" y="137">{R0,W1}</text>
    <text x="546" y="119">e4 DOWN</text><text x="546" y="137">{R1,W0}</text>
    <text x="670" y="119">e5 ANY</text><text x="670" y="137">{R0}</text>
  </g>
  <g stroke="currentColor">
    <rect x="10" y="176" width="220" height="70" rx="6" class="f-acc"/>
    <rect x="260" y="176" width="220" height="70" rx="6" class="f-warn"/>
    <rect x="510" y="176" width="220" height="70" rx="6" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="196" font-weight="bold">march_run()</text>
    <text x="20" y="214">element → 주소(⇓ 면 n-1..0)</text>
    <text x="20" y="232">→ op 전부 → 다음 주소</text>
    <text x="270" y="196" font-weight="bold">bmem_t (n=16 셀, 결함 1~2개)</text>
    <text x="270" y="214">SAF TF CFin CFid CFst AF×3</text>
    <text x="270" y="232">관련 셀 2^k power-up × 나머지 {0,1}</text>
    <text x="520" y="196" font-weight="bold">detected() → coverage_row()</text>
    <text x="520" y="214">모든 power-up 에서 FAIL → 검출</text>
    <text x="520" y="232">행 = 결함 모델, 열 = 알고리즘</text>
  </g>
  <g stroke="currentColor" stroke-width="2" fill="none">
    <path d="M370 148 L370 162 L120 162 L120 174"/><path d="M230 211 L258 211"/><path d="M480 211 L508 211"/>
  </g>
  <text x="10" y="272" fill="currentColor" font-size="12">같은 element 리스트를 Part C 의 word 메모리(0/1 → background D/~D)와 march_validate()(기대값 일관성)도 그대로 쓴다.</text>
  <text x="10" y="290" fill="currentColor" font-size="12">색: 회색 = 방향 무관(⇕), 강조 = ⇑, 주황 = ⇓.</text>
</svg>
```

**표기법 (van de Goor).** `⇑`/`⇓` 주소 오름/내림차순, `⇕` 방향 무관, `rX` 읽고 X 여야 함, `wX` X 를 씀, `(r0,w1)` = **한 주소에서** r0 다음 w1 을 하고 다음 주소로. 셀당 연산 수 k 를 세면 복잡도 k·n.

**이 레슨의 문법 (EBNF).** 공백(스페이스·탭·CR·LF)은 토큰 사이 어디든 허용.

```text
alg   := '{' elem { ';' elem } [ ';' ] '}'          -- 끝의 ';' 하나는 허용, 빈 {} 는 오류
elem  := dir '(' op { ',' op } ')'                  -- op 은 1..8 개 (MAX_OPS)
dir   := '⇑' | '↑' | '^' | 'up'                     -- UP
       | '⇓' | '↓' | 'v' | 'down'                   -- DOWN
       | '⇕' | '↕' | '*' | 'any'                    -- ANY (구현 선택, 기본 = UP)
op    := ('r' | 'R' | 'w' | 'W') ('0' | '1')
```

유니코드 화살표는 UTF-8 3바이트(⇑ = E2 87 91, ⇓ = E2 87 93, ⇕ = E2 87 95)라 오류 위치는 **바이트 오프셋**이다. element 는 최대 12개(MAX_ELEMS) — March B(5개)·March SS(6개)도 충분히 들어간다.

**결함 모델 (이 레슨의 정의 — MEM-B08 과 같은 어휘).**

| 이름 | 정의 | 인스턴스 수 (n=16) |
|---|---|---|
| SAF0 / SAF1 | 셀이 항상 0 / 1 | 16 / 16 |
| TF↑ / TF↓ | 0→1 (1→0) 쓰기 전이 실패, 반대 방향은 정상 | 16 / 16 |
| CFin<↑;~> , CFin<↓;~> | aggressor a 가 실제로 ↑(↓) 전이하면 victim v 반전. **a<v 와 a>v 를 따로** 센다 | 각 120 × 4행 |
| CFid<↑;0>, <↑;1>, <↓;0>, <↓;1> | a 전이 시 v 를 특정 값으로 강제 | 각 240 |
| CFst<x;y> (4종) | a 가 상태 x 인 동안 v = y | 960 |
| AF alias | 주소 x → 셀 y (셀 x 는 어느 주소로도 못 감, 주소 y 도 셀 y) | 240 |
| AF no-access | 주소 x → 셀 없음, 쓰기 무시, 읽기 = 상수 0 또는 1 | 32 |
| AF multi-access | 주소 x → 셀 x 와 셀 y 동시. 쓰기 = 둘 다, 읽기 = wired-AND 또는 wired-OR | 480 |

### Part A — 파서와 엔진

- `march_parse(s, &alg, &err)` — 위 문법 그대로. 반환 `P_OK` 또는 `P_ERR_{EMPTY, LBRACE, DIR, LPAREN, OP, RPAREN, SEP, TOO_MANY_ELEMS, TOO_MANY_OPS, TRAILING}` + `err.pos`.
- `march_run(&alg, &mem, &r)` — element → 주소(⇓ 는 n-1..0, ⇕ 는 전역 `g_any_as`) → op. 읽기는 기대값과 비교, 첫 실패(element, op, 주소, exp, act)와 실패 수, 총 연산 수, 처음 64개 연산의 주소 trace 를 기록.
- CHECK: 5개 알고리즘 문자열(유니코드 / ASCII / 키워드 별칭 섞어서)이 k = 5, 6, 6, 10, 17 로 파싱되고, 별칭 표기가 같은 리스트가 되며, 오류 17가지가 정확한 코드·위치를 낸다. 정상 메모리에서 nfail = 0, nops = k·n.

### Part B — 결함 주입과 커버리지 행렬

- `cell_store()` 에 TF 와 동적 coupling(CFin/CFid) 의미를 넣는다 (SAF·CFst 는 `bmem_settle()`, AF 는 디코더 `bmem_read/write` 에 주어짐).
- `detected(alg, faults, nf, n)` — 결함에 관련된 셀 k 개의 power-up 값 2^k 가지 × 나머지 셀 {all0, all1} **전부에서** FAIL 해야 검출.
- 16셀 메모리의 모든 인스턴스로 표를 찍고, 문헌이 "잡는다" 고 한 칸이 전부 100% 인지, 그리고 주장 밖의 칸(MATS+ TF↓ 0% 등)이 이론과 맞는지 CHECK.

### Part C — bring-up 콘솔: 검증 · word 실행 · 리포트 · 시간

- `march_validate()` — 실행 전에 문자열이 말이 되는지: 쓰기 전 읽기(`READ_UNKNOWN`), 기대값 모순(`READ_MISMATCH`, 예: w0 뒤에 r1), 읽기가 하나도 없음(`NO_READ`).
- `wmarch_run()` — 64 word × 32비트 SRAM(@0x2000_0000) 에서 0/1 을 background D/~D 로. 결함 ① word 13 bit 5 stuck-at-0 ② 디코더가 word index bit 4 를 무시(alias). 리포트 첫 줄 문자열을 정확히 CHECK, 작은 버퍼에서 넘치지 않는지 CHECK.
- `march_test_ns()` — k · n_words · n_bg · ns/access.

## 2. Problem (English)

> **March test interpreter.**
>
> 1. Define a grammar for March notation and implement
>    `perr_t march_parse(const char *s, march_alg_t *out, parse_err_t *err)`.
>    Directions: `⇑ ↑ ^ up` (ascending), `⇓ ↓ v down` (descending), `⇕ ↕ * any` (either). Operations `r0 r1 w0 w1`.
>    Example: `"{⇕(w0); ⇑(r0,w1); ⇓(r1,w0)}"` → 3 elements, 5 ops/cell (MATS+, 5n).
>    Example: `"{^(r2)}"` → `P_ERR_OP` at byte 3. `"{⇑ w0)}"` → `P_ERR_LPAREN` at byte 5 (the arrow is 3 UTF-8 bytes).
> 2. Implement `int march_run(const march_alg_t *a, bmem_t *m, run_result_t *r)` on a bit-oriented memory with injectable faults: SAF0/1, TF↑/↓, CFin (aggressor below/above victim, ↑/↓), CFid, CFst, and address-decoder faults (alias, no-access, multi-access). A fault is *detected* only if the test fails for every power-up state of the cells involved.
> 3. For n = 16, inject every instance of every fault model and print an `algorithm × fault model → %` table for MATS+, MATS++, March X, March C-, March B — all built from strings, no per-algorithm loops. Report ops/cell.
> 4. Before running a user-supplied string, validate it: every read must expect the value the previous write left. Run it on a 64 × 32-bit memory with a data background and emit `FAIL addr=0x20000034 exp=0xAAAAAAAA act=0xAAAAAA8A diff=0x00000020 bits=[5] elem=M2.op0`.
>
> Constraints: no malloc; fixed capacities (12 elements × 8 ops); the report goes to a caller-supplied buffer and must never overflow.
> Follow-ups: Why both ⇑ and ⇓? Why is MATS+ blind to TF↓? What does March B buy over March C- for 7n more?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # SDK 27.2 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/MEM-B09_st && /tmp/MEM-B09_st
# 실제 출력 끝: "14/79 passed" (exit 1)
```

TODO 는 6개: `march_parse`, `march_run`, `cell_store`, `detected`, `march_validate`, `wmarch_run`. 파서가 비어 있으니 시작 코드에서는 모든 알고리즘이 `0n` 이고 커버리지 표가 전부 `0.0%` 다. 처음부터 PASS 하는 14개는 "아무것도 안 해도 맞는" CHECK(예: `parse_fails("", P_ERR_EMPTY, 0)`, 정상 알고리즘의 `march_validate() == V_OK`, 주어진 디코더·settle 코드만 쓰는 AF·CFst 의미 테스트)다 — 그래서 `nops == k·n` 처럼 **테스트가 실제로 일했는지** 보는 CHECK 를 같이 둔다.

## 4. 힌트

<details>
<summary>힌트 1 — 파서는 "커서 하나 + 기대하는 다음 토큰"</summary>

재귀 하강(recursive descent)까지도 필요 없다. `const char *p` 하나를 앞으로만 움직이며 `skip_ws → '{' → (dir → '(' → op (',' op)* → ')') (';' ...)* → '}' → 끝` 순서로 기대한다. 기대와 다르면 그 자리 `p - s` 를 pos 로 돌려준다. 방향 토큰은 주어진 `match_dir()` 이 소비 바이트 수를 준다.

</details>

<details>
<summary>힌트 2 — 오류 위치가 테스트와 안 맞을 때</summary>

`"{^(r0,)}"` 는 `,` 뒤에 op 가 없으므로 **`)` 의 위치(6)** 에서 `P_ERR_OP`. `"{^(r10)}"` 는 `r1` 을 받은 뒤 `0` 이 `,`/`)` 가 아니므로 위치 5 에서 `P_ERR_RPAREN`. 9번째 op 는 "넣기 전에" 용량을 확인해야 위치가 9번째 op 의 시작(27)이 된다.

</details>

<details>
<summary>힌트 3 — 엔진의 루프 순서</summary>

`for element → for k (addr = DOWN ? n-1-k : k) → for op`. op 루프가 **안쪽**이어야 한다. 쓰기는 `bmem_write(m, addr, op == OP_W1)`, 읽기는 `exp = (op == OP_R1)`.

</details>

<details>
<summary>힌트 4 — TF 와 coupling 은 "실제로 바뀐 것" 기준</summary>

`old = cell[c]` 를 먼저 저장. TF↑ 는 `old==0 && d==1` 이면 저장값을 0 으로 남긴다. coupling 은 aggressor 의 저장 전후가 **달라졌을 때만** 발동 — `w0` 을 0 셀에 쓰는 건 전이가 아니다.

</details>

<details>
<summary>힌트 5 — detected() 의 power-up 열거</summary>

관련 셀 = 각 결함의 v, 그리고 a 를 쓰는 종류(CF·AF alias/multi)면 a. 중복 제거 후 k 개. `for rest in {0,1}: for combo < 2^k:` 로 power-up 마스크를 만들고, 한 번이라도 `nfail == 0` 이면 즉시 false.

</details>

<details>
<summary>힌트 6 — march_validate 는 상태 변수 하나</summary>

모든 셀이 똑같은 op 열을 겪으므로 셀 하나의 기대 상태(-1 = 모름, 0, 1)만 따라가면 된다. 쓰기 → 상태 갱신, 읽기 → 상태가 -1 이면 UNKNOWN, 값이 다르면 MISMATCH.

</details>

## 5. 풀이

### Part A — 파서: 문법을 코드로 1:1 옮기기

문법의 각 줄이 코드의 한 블록이 된다. `"{⇑ (r0, w1)}"` 을 따라가 보자 (⇑ 는 바이트 1~3).

| 바이트 pos | 보는 글자 | 기대 | 동작 |
|---|---|---|---|
| 0 | `{` | `'{'` | 소비, skip_ws |
| 1..3 | `E2 87 91` | dir | `match_dir` → UP, 3바이트 소비 |
| 4 | ` ` | — | skip_ws |
| 5 | `(` | `'('` | 소비 |
| 6..7 | `r0` | op | OP_R0 |
| 8 | `,` | `,` 또는 `)` | 다음 op |
| 9 | ` ` | — | skip_ws |
| 10..11 | `w1` | op | OP_W1 |
| 12 | `)` | `,` 또는 `)` | element 끝, nelem=1 |
| 13 | `}` | `;` 또는 `}` | 끝 |
| 14 | `\0` | 문자열 끝 | P_OK |

설계 결정과 이유:

- **ASCII 별칭.** UART 콘솔·CI 로그·C 소스 주석에서 유니코드 화살표는 깨지기 쉽다. `^`/`v`/`*` 는 한 글자, `up`/`down`/`any` 는 읽기 쉬움. `v` 를 `down` 으로 쓰는 것과 `⇕`=`*` 는 이 레슨의 정의다.
- **바이트 위치로 오류 보고.** 콘솔에서 `^` 커서로 위치를 가리킬 수 있게. 잘린 UTF-8(`E2 87` 만 있음)은 `strncmp` 가 불일치로 판단해 `P_ERR_DIR` — 깨진 바이트를 "아는 척" 하지 않는다.
- **고정 용량 + 오류 코드.** `MAX_ELEMS=12`, `MAX_OPS=8` 를 넘으면 `TOO_MANY_*`. 펌웨어에서 사용자 입력으로 버퍼를 넘치게 하지 않는 기본기다 (op 를 넣기 **전에** 용량 확인).
- **끝의 `;` 허용, 빈 `{}` 거부.** 복붙한 문자열 관대하게, 의미 없는 입력은 엄격하게.

엔진은 SIV-B07 과 같다: `addr = (dir == DOWN) ? n-1-k : k`, op 루프가 안쪽. 차이는 element 리스트가 **문자열에서 온다**는 것뿐이고, 그래서 March B(17n)·March A(15n)·March Y(8n)를 추가하는 비용이 문자열 한 줄이다 (연습 2·3).

`solution.c` 의 Part A 출력 (실제):

```text
[Part A] March C- on SAF1@5: first fail M1 op0 addr=5 exp=0 act=1, nfail=3, nops=160
```

SAF1 은 r0 를 하는 M1·M3·M5 에서만 걸리므로 nfail = 3, nops = 10 × 16 = 160.

### Part B — "검출" 의 정의부터

March 이론의 "잡는다" 는 **초기 상태와 무관하게** 반드시 실패한다는 뜻이다. 실제 칩의 power-up 값은 셀마다 제멋대로이므로, 한 번의 랜덤 초기값으로 돌려서 FAIL 이 나온 것은 운이다. 그래서:

- 결함에 관련된 셀(v, 그리고 CF/AF 의 a)의 값 2^k 가지를 **전부** 시도하고,
- 나머지 셀은 all-0 과 all-1 두 경우 (모든 알고리즘이 `⇕(w0)` 로 시작하므로 나머지 셀 값은 결함 셀이 아니면 바로 덮인다),
- 이 2^(k+1) 번 **모두** FAIL 이어야 검출.

SIV-B07 은 4가지 전역 패턴(all0/all1/0101/1010)을 썼다. 관련 셀 전수가 더 엄밀하다: 두 셀 a, v 의 4가지 조합이 전역 패턴 몇 개로는 항상 다 나오지 않을 수 있다(a, v 가 같은 짝수/홀수일 때 0101 패턴은 같은 값만 준다).

**실제 측정 표** (`solution.c`, n = 16):

```text
[Part B] guaranteed detection, n=16 cells (involved cells: all 2^k power-ups x rest {0,1})
fault model     #inst     MATS+    MATS++   March X  March C-   March B
SAF0               16    100.0%    100.0%    100.0%    100.0%    100.0%
SAF1               16    100.0%    100.0%    100.0%    100.0%    100.0%
TF<up>             16    100.0%    100.0%    100.0%    100.0%    100.0%
TF<down>           16      0.0%    100.0%    100.0%    100.0%    100.0%
CFin<up> a<v      120    100.0%    100.0%    100.0%    100.0%    100.0%
CFin<up> a>v      120    100.0%    100.0%    100.0%    100.0%    100.0%
CFin<dn> a<v      120      0.0%      0.0%    100.0%    100.0%    100.0%
CFin<dn> a>v      120    100.0%    100.0%    100.0%    100.0%    100.0%
CFid<up;0>        240     50.0%     50.0%     50.0%    100.0%    100.0%
CFid<up;1>        240     50.0%     50.0%     50.0%    100.0%    100.0%
CFid<dn;0>        240     50.0%     50.0%     50.0%    100.0%    100.0%
CFid<dn;1>        240      0.0%      0.0%     50.0%    100.0%    100.0%
CFst (4 kinds)    960     75.0%     75.0%     75.0%    100.0%    100.0%
AF alias          240    100.0%    100.0%    100.0%    100.0%    100.0%
AF no-access       32    100.0%    100.0%    100.0%    100.0%    100.0%
AF multi-acc      480    100.0%    100.0%    100.0%    100.0%    100.0%
ops (k*n)                    5n        6n        6n       10n       17n
```

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">커버리지 행렬 — 이 맥(Apple M6)에서 solution.c 로 직접 측정한 값 (n=16, 보장 검출)</text>
  <g fill="currentColor" text-anchor="middle" font-weight="bold">
    <text x="250" y="42">MATS+ 5n</text><text x="350" y="42">MATS++ 6n</text><text x="450" y="42">March X 6n</text>
    <text x="550" y="42">March C- 10n</text><text x="650" y="42">March B 17n</text>
  </g>
  <g fill="currentColor">
    <text x="20" y="64">SAF0 / SAF1</text><text x="20" y="84">TF↑</text><text x="20" y="104">TF↓</text>
    <text x="20" y="124">CFin↑ a&lt;v, a&gt;v</text><text x="20" y="144">CFin↓ a&lt;v</text><text x="20" y="164">CFin↓ a&gt;v</text>
    <text x="20" y="184">CFid&lt;↑;0&gt; &lt;↑;1&gt; &lt;↓;0&gt;</text><text x="20" y="204">CFid&lt;↓;1&gt;</text>
    <text x="20" y="224">CFst (4종)</text><text x="20" y="244">AF alias / no-acc / multi</text>
  </g>
  <g stroke="currentColor">
    <rect x="205" y="51" width="90" height="18" class="f-ok"/><rect x="305" y="51" width="90" height="18" class="f-ok"/><rect x="405" y="51" width="90" height="18" class="f-ok"/><rect x="505" y="51" width="90" height="18" class="f-ok"/><rect x="605" y="51" width="90" height="18" class="f-ok"/>
    <rect x="205" y="71" width="90" height="18" class="f-ok"/><rect x="305" y="71" width="90" height="18" class="f-ok"/><rect x="405" y="71" width="90" height="18" class="f-ok"/><rect x="505" y="71" width="90" height="18" class="f-ok"/><rect x="605" y="71" width="90" height="18" class="f-ok"/>
    <rect x="205" y="91" width="90" height="18" class="f-warn"/><rect x="305" y="91" width="90" height="18" class="f-ok"/><rect x="405" y="91" width="90" height="18" class="f-ok"/><rect x="505" y="91" width="90" height="18" class="f-ok"/><rect x="605" y="91" width="90" height="18" class="f-ok"/>
    <rect x="205" y="111" width="90" height="18" class="f-ok"/><rect x="305" y="111" width="90" height="18" class="f-ok"/><rect x="405" y="111" width="90" height="18" class="f-ok"/><rect x="505" y="111" width="90" height="18" class="f-ok"/><rect x="605" y="111" width="90" height="18" class="f-ok"/>
    <rect x="205" y="131" width="90" height="18" class="f-warn"/><rect x="305" y="131" width="90" height="18" class="f-warn"/><rect x="405" y="131" width="90" height="18" class="f-ok"/><rect x="505" y="131" width="90" height="18" class="f-ok"/><rect x="605" y="131" width="90" height="18" class="f-ok"/>
    <rect x="205" y="151" width="90" height="18" class="f-ok"/><rect x="305" y="151" width="90" height="18" class="f-ok"/><rect x="405" y="151" width="90" height="18" class="f-ok"/><rect x="505" y="151" width="90" height="18" class="f-ok"/><rect x="605" y="151" width="90" height="18" class="f-ok"/>
    <rect x="205" y="171" width="90" height="18" class="f-mut"/><rect x="305" y="171" width="90" height="18" class="f-mut"/><rect x="405" y="171" width="90" height="18" class="f-mut"/><rect x="505" y="171" width="90" height="18" class="f-ok"/><rect x="605" y="171" width="90" height="18" class="f-ok"/>
    <rect x="205" y="191" width="90" height="18" class="f-warn"/><rect x="305" y="191" width="90" height="18" class="f-warn"/><rect x="405" y="191" width="90" height="18" class="f-mut"/><rect x="505" y="191" width="90" height="18" class="f-ok"/><rect x="605" y="191" width="90" height="18" class="f-ok"/>
    <rect x="205" y="211" width="90" height="18" class="f-mut"/><rect x="305" y="211" width="90" height="18" class="f-mut"/><rect x="405" y="211" width="90" height="18" class="f-mut"/><rect x="505" y="211" width="90" height="18" class="f-ok"/><rect x="605" y="211" width="90" height="18" class="f-ok"/>
    <rect x="205" y="231" width="90" height="18" class="f-ok"/><rect x="305" y="231" width="90" height="18" class="f-ok"/><rect x="405" y="231" width="90" height="18" class="f-ok"/><rect x="505" y="231" width="90" height="18" class="f-ok"/><rect x="605" y="231" width="90" height="18" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="250" y="64">100</text><text x="350" y="64">100</text><text x="450" y="64">100</text><text x="550" y="64">100</text><text x="650" y="64">100</text>
    <text x="250" y="84">100</text><text x="350" y="84">100</text><text x="450" y="84">100</text><text x="550" y="84">100</text><text x="650" y="84">100</text>
    <text x="250" y="104">0</text><text x="350" y="104">100</text><text x="450" y="104">100</text><text x="550" y="104">100</text><text x="650" y="104">100</text>
    <text x="250" y="124">100</text><text x="350" y="124">100</text><text x="450" y="124">100</text><text x="550" y="124">100</text><text x="650" y="124">100</text>
    <text x="250" y="144">0</text><text x="350" y="144">0</text><text x="450" y="144">100</text><text x="550" y="144">100</text><text x="650" y="144">100</text>
    <text x="250" y="164">100</text><text x="350" y="164">100</text><text x="450" y="164">100</text><text x="550" y="164">100</text><text x="650" y="164">100</text>
    <text x="250" y="184">50</text><text x="350" y="184">50</text><text x="450" y="184">50</text><text x="550" y="184">100</text><text x="650" y="184">100</text>
    <text x="250" y="204">0</text><text x="350" y="204">0</text><text x="450" y="204">50</text><text x="550" y="204">100</text><text x="650" y="204">100</text>
    <text x="250" y="224">75</text><text x="350" y="224">75</text><text x="450" y="224">75</text><text x="550" y="224">100</text><text x="650" y="224">100</text>
    <text x="250" y="244">100</text><text x="350" y="244">100</text><text x="450" y="244">100</text><text x="550" y="244">100</text><text x="650" y="244">100</text>
  </g>
  <rect x="205" y="270" width="16" height="12" class="f-ok" stroke="currentColor"/><text x="226" y="281" fill="currentColor">100%</text>
  <rect x="290" y="270" width="16" height="12" class="f-mut" stroke="currentColor"/><text x="311" y="281" fill="currentColor">부분 (50 / 75%)</text>
  <rect x="430" y="270" width="16" height="12" class="f-warn" stroke="currentColor"/><text x="451" y="281" fill="currentColor">0% (보장 검출 없음)</text>
  <text x="10" y="312" fill="currentColor">읽는 법: 열을 왼쪽에서 오른쪽으로 갈수록 element 하나씩 늘린 효과가 보인다.</text>
  <text x="10" y="332" fill="currentColor">MATS+ → MATS++ : 마지막 element 끝에 r0 하나 (5n → 6n) → TF↓ 0 → 100.</text>
  <text x="10" y="352" fill="currentColor">MATS++ → March X : r0 를 별도 ⇕(r0) element 로 (6n) → CFin↓ a&lt;v 0 → 100.</text>
  <text x="10" y="372" fill="currentColor">March X → March C- : ⇑ 두 번 + ⇓ 두 번 (10n) → CFid / CFst 전부 100.</text>
  <text x="10" y="392" fill="currentColor">March C- → March B : 단일 결함에서는 차이 없음 (17n). 차이는 linked fault 에서 (연습 3).</text>
</svg>
```

**칸 하나씩 이유 대기.**

① **MATS+ TF↓ 0%.** MATS+ = `{⇕(w0); ⇑(r0,w1); ⇓(r1,w0)}`. 1→0 전이는 M2 의 `w0` 에서 처음 일어나는데 그 뒤에 그 셀을 읽는 op 가 없다. MATS++ 는 M2 끝에 `r0` 을 붙여(`⇓(r1,w0,r0)`) 100% — 규칙: **모든 전이 쓰기 뒤에는 그 값을 확인하는 읽기가 있어야 한다.**

② **CFin↓ a<v 가 MATS+/MATS++ 에서 0%.** n = 4, a = 1, v = 3 으로 MATS+ 를 손으로 돌려 보자 (power-up 전부 0).

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">CFin&lt;↓;~&gt;, a=1 &lt; v=3, n=4 : MATS+ 는 놓치고 March X 는 잡는다</text>
  <g fill="currentColor" font-weight="bold" text-anchor="middle">
    <text x="215" y="44">addr 0</text><text x="335" y="44">addr 1 (a)</text><text x="455" y="44">addr 2</text><text x="575" y="44">addr 3 (v)</text>
  </g>
  <g fill="currentColor"><text x="10" y="72">M0 ⇕(w0)</text><text x="10" y="112">M1 ⇑(r0,w1)</text><text x="10" y="152">M2 ⇓(r1,w0)</text><text x="10" y="192">X: ⇕(r0)</text></g>
  <g stroke="currentColor">
    <rect x="160" y="54" width="110" height="28" class="f-mut"/><rect x="280" y="54" width="110" height="28" class="f-mut"/><rect x="400" y="54" width="110" height="28" class="f-mut"/><rect x="520" y="54" width="110" height="28" class="f-mut"/>
    <rect x="160" y="94" width="110" height="28" class="f-acc"/><rect x="280" y="94" width="110" height="28" class="f-acc"/><rect x="400" y="94" width="110" height="28" class="f-acc"/><rect x="520" y="94" width="110" height="28" class="f-acc"/>
    <rect x="160" y="134" width="110" height="28" class="f-acc"/><rect x="280" y="134" width="110" height="28" class="f-warn"/><rect x="400" y="134" width="110" height="28" class="f-acc"/><rect x="520" y="134" width="110" height="28" class="f-acc"/>
    <rect x="160" y="174" width="110" height="28" class="f-mut"/><rect x="280" y="174" width="110" height="28" class="f-mut"/><rect x="400" y="174" width="110" height="28" class="f-mut"/><rect x="520" y="174" width="110" height="28" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="215" y="73">w0</text><text x="335" y="73">w0</text><text x="455" y="73">w0</text><text x="575" y="73">w0</text>
    <text x="215" y="113">r0 w1</text><text x="335" y="113">r0 w1 (a↑)</text><text x="455" y="113">r0 w1</text><text x="575" y="113">r0 w1</text>
    <text x="215" y="153">④ r1 w0</text><text x="335" y="153">③ a↓ → v=1</text><text x="455" y="153">② r1 w0</text><text x="575" y="153">① r1 w0 → v=0</text>
    <text x="575" y="193">r0 → 1 ≠ 0 FAIL</text>
  </g>
  <text x="10" y="226" fill="currentColor" font-size="12">⇓ 에서는 v(=3) 가 a(=1) 보다 먼저 처리된다 (①→④ 순서).</text>
  <text x="10" y="244" fill="currentColor" font-size="12">v 를 0 으로 쓴 뒤 a↓ 가 v 를 1 로 뒤집지만, MATS+ 에는 그 뒤 v 를 읽는 op 가 없다.</text>
  <text x="10" y="262" fill="currentColor" font-size="12">MATS++ 의 끝 r0 은 M2 안(v 처리 시점)이라 역시 a↓ 보다 먼저 끝난다.</text>
  <text x="10" y="280" fill="currentColor" font-size="12">March X 의 별도 ⇕(r0) element 만 "a 전이 이후" 에 v 를 읽는다 (맨 아래 줄).</text>
</svg>
```

a > v 이면 ⇓ 에서 a 가 먼저 처리돼 v(=1)를 0 으로 뒤집고, v 의 `r1` 이 바로 본다 → 100%. 그래서 CFin 은 **a 의 위치별로 따로 세야** 한다. 합쳐서 "50%" 라고 하면 왜 빠지는지가 안 보인다.

③ **CFid 50% 의 정체 = 방향.** MATS+/MATS++/March X 에서 a↑ 는 ⇑ 에서만, a↓ 는 ⇓ 에서만 생긴다(power-up 우연 제외). 강제값이 victim 의 그 시점 값과 같으면 티가 안 나고, 다르면 victim 을 그 element 안에서 아직 안 읽었을 때만(= a 가 v 보다 먼저 처리될 때만) 잡힌다 → 대략 a 위치의 절반. March C- 는 (a↑, a↓) × (a<v, a>v) 4가지 각각에 대해 "a 전이 직후, v 가 다시 쓰이기 전에 v 를 읽는" element 를 하나씩 가진다:

| 경우 | a 의 전이를 만드는 element | v 를 읽는 곳 |
|---|---|---|
| a<v, a↑ | M1 ⇑(r0,w1) | 같은 M1, v 의 r0 |
| a<v, a↓ | M2 ⇑(r1,w0) | 같은 M2, v 의 r1 |
| a>v, a↑ | M3 ⇓(r0,w1) | 같은 M3, v 의 r0 |
| a>v, a↓ | M4 ⇓(r1,w0) | 같은 M4, v 의 r1 |

victim 이 0 일 때(M1·M3)와 1 일 때(M2·M4) 모두 읽히므로 강제값 0/1 어느 쪽도 빠지지 않는다 → CFid 4종, CFst 4종 100%.

④ **AF 는 MATS+ 도 100%.** 문헌의 AF 검출 충분조건은 `⇑(rx, …, wx̄)` 와 `⇓(rx̄, …, wx)` 를 둘 다 갖는 것 — MATS+ 가 정확히 그 최소형이다. 내 AF 모델에서는 사실 ⇑ element 하나로도 alias 가 잡힌다(x 가 셀 y 를 바꾸면 y 를 처리할 때 보인다). 그런데 `⇕` 로 적힌 element 를 구현이 어느 방향으로 돌리든 결과가 같아야 한다는 조건까지 생각하면 두 방향이 다 필요해진다 (연습 1 측정).

⑤ **March B 는 단일 결함에서 March C- 와 같다.** 7n 을 더 쓰는 이유는 **linked fault**(한 victim 에 두 결함이 겹쳐 서로 가리는 것)다. 연습 3 에서 측정: CFid+CFid 가리기 쌍(같은 전이 방향, 반대 강제값)에서 March C- 66.7%, March A/B 100%.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">linked CFid: &lt;↑;1&gt; from a1=2, &lt;↑;0&gt; from a2=4, victim v=7 (n=8)</text>
  <g stroke="currentColor">
    <rect x="120" y="34" width="70" height="28" class="f-mut"/><rect x="190" y="34" width="70" height="28" class="f-mut"/>
    <rect x="260" y="34" width="70" height="28" class="f-acc"/><rect x="330" y="34" width="70" height="28" class="f-mut"/>
    <rect x="400" y="34" width="70" height="28" class="f-acc"/><rect x="470" y="34" width="70" height="28" class="f-mut"/>
    <rect x="540" y="34" width="70" height="28" class="f-mut"/><rect x="610" y="34" width="70" height="28" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="155" y="53">0</text><text x="225" y="53">1</text><text x="295" y="53">a1=2</text><text x="365" y="53">3</text>
    <text x="435" y="53">a2=4</text><text x="505" y="53">5</text><text x="575" y="53">6</text><text x="645" y="53">v=7</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="92" font-weight="bold">March C- M1 ⇑(r0,w1)</text>
    <text x="10" y="112" font-family="monospace">addr2 w1: a1↑ → v=1 | addr4 w1: a2↑ → v=0 | addr7 r0: 0 = 0</text>
    <text x="10" y="130">→ PASS (두 결함이 서로 가림)</text>
    <text x="10" y="156" font-weight="bold">March C- M3 ⇓(r0,w1)</text>
    <text x="10" y="176" font-family="monospace">addr7 r0 PASS, w1 → v=1 | addr4: a2↑ → v=0 | addr2: a1↑ → v=1</text>
    <text x="10" y="194">→ M4 의 r1 도 PASS. 나머지 element 에는 ↑ 전이가 없다.</text>
    <text x="10" y="220" font-weight="bold">March A M2 ⇑(r1,w0,w1)</text>
    <text x="10" y="240" font-family="monospace">addr2 w0,w1: a1↑ → v=1 | addr4 w0,w1: a2↑ → v=0 | addr7 r1: 0 ≠ 1</text>
    <text x="10" y="258">→ FAIL (검출)</text>
    <text x="10" y="290">핵심: March C- 는 a↑ 를 v 가 0 을 기대하는 element(M1, M3) 에서만 만든다.</text>
    <text x="10" y="310">"나중에 처리된 aggressor 가 이긴" 값이 기대값과 우연히 같아 가려진다.</text>
  </g>
</svg>
```

**문헌과 비교 — 어디서 같고 어디서 다른가.** 문헌 요약은 van de Goor (1991) 표 기준이고 March B/C- 의 정의·길이·linked 주장은 Microchip Class B 문서로 확인했다. 나머지 칸은 기억에 의존한 요약이므로 원문 확인을 권장한다.

| 알고리즘 | k | 문헌이 주장하는 커버리지 | 내 측정 |
|---|---|---|---|
| MATS+ | 5 | AF, SAF | 일치. 추가로 TF↑ 100%, CF 일부 |
| MATS++ | 6 | AF, SAF, TF | 일치 |
| March X | 6 | AF, SAF, TF, CFin | 일치 (CFid 50%, CFst 75%) |
| March Y | 8 | AF, SAF, TF(+CFin 과 linked), CFin | 일치 (연습 2) |
| March C- | 10 | AF, SAF, TF, unlinked CFin/CFid/CFst | 일치. linked CFid 가리기 쌍 66.7% |
| March A | 15 | AF, SAF, TF, CFin, linked CFid | 일치 (연습 3) |
| March B | 17 | AF, SAF, TF(CFin/CFid 와 linked), CFin, linked CFid | 일치 |

내 모델이 문헌과 다른 점 (정직하게):

1. **⇕ 해석.** 문헌은 "어느 방향이든 된다" 가 **알고리즘이 보장해야 할 성질**이다. 나는 기본 ⇑ 로 돌리고, 연습 1 에서 ⇓ 로도 돌려 표가 같은지 확인했다 (같다). ⇕ 를 잘못 붙인 알고리즘은 표가 달라진다.
2. **AF 모델.** 문헌은 AF 4종(셀 없음 / 주소 없음 / 한 주소 → 여러 셀 / 여러 주소 → 한 셀)의 **조합**을 다룬다. 나는 3개의 구체적 조합만 만들었고 no-access 읽기를 상수(0 또는 1)로 뒀다. 실제로는 직전 읽은 값이 비트라인/센스앰프에 남아 보이는 경우도 있어서 더 잡기 어렵다.
3. **TF+CFid linked 가 March C- 에서도 100%.** 내 모델은 coupling 으로 인한 victim 변화에는 TF 를 적용하지 않고, 두 결함이 "같은 element 안에서 서로를 가리는" 짝이 되는 경우가 드물다. 문헌의 linked TF 는 "가리기가 일어나는 것" 을 골라 정의한 집합이라, 전부를 열거한 내 분모와 다르다. 이 행은 March B 를 정당화하는 증거로 쓰지 않는다.
4. **없는 것.** DRDF·read disturb(SIV-B07 연습 2), data retention(대기 element 필요), NPSF(이웃 패턴 민감), 속도 의존 동적 결함, word 내부 결함(data background, SIV-B07 Part C), DRAM 의 Row Hammer(MEM-A02).

### Part C — 실행 전 검증, word 실행, 리포트, 시간

**검증.** 모든 셀이 같은 op 열을 겪으므로 셀 하나의 기대 상태만 따라가면 된다. March C- 의 경우: w0 → (0) r0 ✓ w1 → (1) r1 ✓ w0 → (0) r0 ✓ w1 → (1) r1 ✓ w0 → (0) r0 ✓ = OK. 오타 `"{⇕(w0); ⇑(r0,w1); ⇑(r0,w0)}"` 은 element 2 op 0 에서 `READ_MISMATCH` — 이걸 실행하면 정상 메모리가 전부 FAIL 로 보고되고, 첫 실리콘에서 "SRAM 전체 불량" 이라는 잘못된 보고가 나간다. **테스트 버그를 HW 버그로 보고하지 않는 것**이 검증 엔지니어의 신뢰다.

**word 실행 + 리포트** (실제 출력):

```text
[Part C] word-oriented March C- on SRAM @0x20000000, 64 x 32-bit, bg=0x55555555
  [good]           nfail=0 nops=640
  [SA0 w13 bit5]   nfail=2
FAIL addr=0x20000034 exp=0xAAAAAAAA act=0xAAAAAA8A diff=0x00000020 bits=[5] elem=M2.op0
FAIL addr=0x20000034 exp=0xAAAAAAAA act=0xAAAAAA8A diff=0x00000020 bits=[5] elem=M4.op0
  [idx bit4 alias] nfail=128 (first 3 lines)
FAIL addr=0x20000040 exp=0x55555555 act=0xAAAAAAAA diff=0xFFFFFFFF bits=[32 bits] elem=M1.op0
FAIL addr=0x20000044 exp=0x55555555 act=0xAAAAAAAA diff=0xFFFFFFFF bits=[32 bits] elem=M1.op0
FAIL addr=0x20000048 exp=0x55555555 act=0xAAAAAAAA diff=0xFFFFFFFF bits=[32 bits] elem=M1.op0
  March C-, 65536 words x 6 bg x 10 ns = 39321600 ns (39.32 ms); March B = 66.85 ms
```

- **SA0, nfail = 2.** bg = 0x55555555 의 bit 5 는 0, ~bg 에서는 1. stuck-at-0 은 "1 을 기대하는 읽기" = ~D 를 읽는 M2·M4 의 r1 에서만 보인다. 주소 = 0x2000_0000 + 13×4 = 0x2000_0034.
- **alias, nfail = 128 을 손으로.** index bit 4 무시 → 16..31 이 0..15 로, 48..63 이 32..47 로 간다. M1 ⇑: 0..15 를 ~D 로 바꾼 뒤 16..31 을 읽으면 ~D(기대 D) → 16개, 48..63 도 16개 = 32. M2·M3·M4 도 같은 논리로 각 32, M5 는 전부 D 라 0 → 4 × 32 = **128**. 첫 실패 주소 0x2000_0040 = word 16 = 2⁴ → "index bit 4" 를 바로 의심한다 (주소선 진단 자체는 MEM-B08 몫).
- **리포트 버퍼.** 호출자가 준 버퍼에 줄 단위로 넣고, 줄 수 상한 또는 용량을 넘으면 **버린다**(nfail 은 계속 센다). 디코더 결함은 수천 줄을 만든다 — UART 로 다 뿜으면 로그가 테스트보다 오래 걸린다.

**시간 추정.** T = k · N_words · N_bg · t_access. 256 KiB SRAM(65 536 word), 6 background, 10 ns/접근: March C- = 10 × 65 536 × 6 × 10 ns = 39.3 ms, March B = 17/10 배 = 66.9 ms. 부팅 시간 예산이 50 ms 라면 March B 는 못 넣는다 — **알고리즘 선택은 커버리지 표 × 시간 예산**이다.

### 복잡도

- 파싱 O(|s|), 실행 O(k·n), 커버리지 표 = Σ(인스턴스 × 2^(k+1) power-up × k·n). n=16 에서 5개 알고리즘 전체가 이 맥에서 약 0.1 s (`-O2`, 측정 시 load avg 24~31 — 공유 머신이라 시간은 참고값).
- 메모리: element 리스트 12 × (1 + 8) 바이트 수준, 결함 인스턴스 정적 배열 `4·32·31 = 3968` 개. malloc 없음.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/MEM-B09_so && /tmp/MEM-B09_so      # 79/79 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/MEM-B09_as && /tmp/MEM-B09_as
# ASan/UBSan: 경고 없음, 79/79 passed
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 파서에서 op 를 넣은 **뒤** 용량 확인 | 9번째 op 가 `ops[8]` 에 써져 버퍼 넘침 (ASan 이 잡는다) | `if (e->nops == MAX_OPS) PFAIL(...)` 를 넣기 전에 |
| `p[1]` 을 보기 전에 `*p` 가 `'\0'` 인지 안 봄 | 문자열 끝 너머 읽기 | `rd \|\| wr` 가 참일 때만 `p[1]` (단락 평가) |
| UTF-8 화살표를 `char` 하나로 비교 | `'⇑'` 는 3바이트, 멀티문자 상수는 구현 정의 | `strncmp(p, "⇑", 3)` 로 바이트열 비교 |
| `v` 를 `down` 보다 먼저 매칭 | 이 문법에선 괜찮지만, 접두사가 겹치는 토큰을 추가하면 짧은 것이 먼저 먹는다 | 긴 토큰을 테이블 앞에 |
| 오류 위치를 "토큰 번호" 로 보고 | 콘솔에서 위치를 가리킬 수 없다 | 바이트 오프셋 `p - s` |
| 엔진에서 op 루프를 바깥에 | `(r0,w1)` 이 "전체 r0 → 전체 w1" 이 되어 CF 검출 조건이 깨짐 | 주소 루프 안에 op 루프 |
| CF 를 쓰기마다 발동 | 0 을 0 에 쓰는 건 전이가 아니다 → 커버리지가 부풀려진다 | `if (nw == old) return;` |
| 한 번의 랜덤 초기값으로 커버리지 계산 | MATS+ TF↓ 가 운으로 잡혀 0% 가 아닌 값이 나온다 | 관련 셀 2^k × 나머지 {0,1} 모두에서 FAIL |
| CFin 을 a 위치 구분 없이 합산 | "50%" 만 보여 ⇓ 의 역할이 안 보인다 | a<v, a>v 를 다른 행으로 |
| 사용자 문자열을 검증 없이 실행 | 오타 하나로 정상 칩을 불량 보고 | `march_validate()` 먼저 |

## 8. 면접 Q&A

<details>
<summary>Q1. March C- 를 써 보고, 복잡도와 March C 와의 차이를 말해 보세요.</summary>

`{⇕(w0); ⇑(r0,w1); ⇑(r1,w0); ⇓(r0,w1); ⇓(r1,w0); ⇕(r0)}` — 셀당 1+2+2+2+2+1 = 10 → 10n. 원래 March C(Marinescu 1982)는 가운데 `⇕(r0)` 가 하나 더 있는 11n 인데, 그 읽기가 잡는 결함은 다음 element 의 r0 가 이미 잡으므로 중복이라 빼서 March C- 가 됐다. **꼬리:** "256 KiB, 6 background, 10 ns 면?" → 39.3 ms (Part C).

</details>

<details>
<summary>Q2. 왜 ⇑ 와 ⇓ 가 둘 다 필요한가요? 주소 순서는 꼭 0,1,2,… 여야 하나요?</summary>

coupling 은 aggressor 가 victim 보다 **먼저** 처리될 때만 같은 element 안에서 영향이 읽힌다. ⇑ 만 있으면 a<v 쪽만 보인다. 측정: March C- 의 M3/M4 를 ⇕ 로 적고 ⇕=⇑ 로 돌리면 CFid<↑;0> 가 66/132 = 50%, ⇓ 로 돌리면 100% (연습 1). 순서는 임의여도 된다 — **⇓ 가 ⇑ 의 정확한 역순이기만 하면** 이론이 성립한다. 그래서 물리 배치(row/column scramble)를 몰라도 March 를 쓸 수 있고, MBIST 는 하드웨어로 만들기 쉬운 순서를 고른다.

</details>

<details>
<summary>Q3. MATS+ 는 왜 TF↓ 를 못 잡나요? 최소한으로 고치면?</summary>

1→0 전이는 마지막 element `⇓(r1,w0)` 의 w0 에서만 일어나고 그 뒤 읽기가 없다. 측정 0%. 고침: 그 element 끝에 r0 하나 → MATS++ `⇓(r1,w0,r0)` (6n), TF↓ 100%. 일반 규칙: 각 방향의 전이 뒤에 그 값을 확인하는 읽기가 있어야 한다. 단 MATS++ 의 r0 는 element 안이라 CFin↓ a<v 는 여전히 0% — 별도 `⇕(r0)` element 로 빼면(March X) 100%.

</details>

<details>
<summary>Q4. 커버리지를 잴 때 "검출" 을 어떻게 정의했나요? 그냥 한 번 돌려서 FAIL 이면 안 되나요?</summary>

실제 칩의 power-up 값은 셀마다 정해지지 않는다. 한 번의 초기값으로 FAIL 한 것은 그 초기값 덕일 수 있다(MATS+ 의 TF↓ 는 power-up 이 1 이면 M0 w0 의 실패가 M1 r0 에서 보인다). 그래서 관련 셀 2^k 조합 × 나머지 {0,1} **모두에서** FAIL 일 때만 검출로 셌다. 이것이 문헌의 "반드시 검출(guaranteed)" 에 해당한다. **꼬리:** "확률적 커버리지는 의미가 없나?" → 양산 품질(DPPM) 추정에는 결함 분포·power-up 분포를 넣은 확률 모델이 쓰이지만, 알고리즘 비교에는 보장 기준이 표준이다.

</details>

<details>
<summary>Q5. March C- 가 통과했는데도 남는 결함은?</summary>

linked fault(측정: CFid 가리기 쌍 66.7%), DRDF/read disturb(같은 셀 연속 읽기 필요), data retention(쓰고 기다렸다 읽기 — DRAM 은 refresh 를 끄거나 늘려야 의미가 있다), NPSF(이웃 셀 패턴), 속도·타이밍 의존 결함(at-speed 아니면 못 봄), word 내부 coupling(background 필요), 그리고 ECC 가 켜져 있으면 1비트 결함은 정정되어 숨는다(MEM-B07, SIV-M14). **꼬리:** "그럼 March B 로 바꾸면?" → linked CFid 는 잡지만 위의 나머지는 그대로이고 시간이 1.7배.

</details>

<details>
<summary>Q6. 왜 알고리즘을 문자열/테이블로 두나요? 펌웨어에 그냥 루프로 짜면 더 빠르지 않나요?</summary>

① 같은 엔진을 시뮬레이션(DPI)·에뮬레이터·실보드에서 재사용, ② 새 결함이 발견되면(예: read disturb) 재빌드 없이 알고리즘만 바꿔 넣기(MBIST 의 programmable algorithm, 콘솔 명령), ③ 실행 전 검증과 k·n 시간 추정을 자동으로. 속도 비용은 element/op 디스패치뿐이고 메모리 접근이 지배한다. 정말 빠르게 해야 하면 문자열을 **오프라인**에서 테이블/코드로 컴파일한다(파서는 그대로, 백엔드만 다름).

</details>

<details>
<summary>Q7. 이걸 DRAM bring-up 에 그대로 쓰면 되나요?</summary>

출발점으로는 되지만 차이가 있다. DRAM 은 셀이 커패시터라 retention·refresh 가 핵심이고(MEM-B05), 논리 주소와 물리 행/열 배치가 컨트롤러 주소 매핑(MEM-B01)과 칩 내부 scramble 로 섞인다. 그래서 March 의 "⇑ 역순" 은 성립해도 **물리 인접 셀** 사이 결함을 노리려면 매핑을 알아야 한다. 또 GB 단위라 10n 도 초 단위(1 GiB, 64-bit, 2 ns 가정 March C- ≈ 2.7 s, 연습 5) — 그래서 DDR bring-up 은 보통 data bus/address bus 테스트(MEM-B08)로 배선부터 보고, 전체 March 는 일부 영역이나 양산/번인(burn-in) 단계에서 돌린다. memtest86+ 류 도구는 moving inversions 같은 March 와 비슷한 패턴에 랜덤 패턴·대기 시간을 섞는다 (도구별 상세는 문서 확인). Row Hammer 는 별도(MEM-A02).

</details>

<details>
<summary>Q8. 사용자 입력 문자열을 받는 테스트 명령을 펌웨어에 넣을 때 무엇을 조심하나요?</summary>

고정 용량과 오류 코드(버퍼 넘침 금지), 실행 전 의미 검증(기대값 모순·쓰기 전 읽기·읽기 없음), 테스트 영역이 코드/스택/DMA 버퍼와 겹치지 않는지, 캐시를 끄거나 non-cacheable, ECC raw 모드, 리포트 줄 수 상한, 그리고 예상 시간을 먼저 출력하고 watchdog 이 만료되지 않게 영역을 나눠 실행(조각마다 watchdog kick). **꼬리:** "파서에 fuzzing 을 하나?" → 해야 한다. ASan + 랜덤 문자열로 `march_parse` 를 돌리는 것이 가장 싼 방어다.

</details>

## 9. 추가 연습

### 연습 1 — ⇕ 를 쓸 자격

모든 알고리즘을 `⇕ = ⇑` 와 `⇕ = ⇓` 로 각각 돌려 단일 결함 표가 같은지 비교하라. 그리고 March C- 의 M3/M4 를 `⇕` 로 잘못 적은 문자열은 어떻게 되는가?

<details>
<summary>답</summary>

7개 알고리즘(MATS+, MATS++, March X, March Y, March C-, March A, March B) 전부 차이 0칸 (n = 12, 실제 출력 `cells that change with any-direction: 0`). M3/M4 를 ⇕ 로 적은 March C- 는 `CFid<up;0> 66/132 (any=up) vs 132/132 (any=down)` — ⇕ 를 붙였다는 것은 "어느 방향이든 커버리지가 같다" 는 약속인데 이 문자열은 그 약속을 어긴다.

</details>

### 연습 2 — March Y 를 문자열 한 줄로

`{⇕(w0); ⇑(r0,w1,r1); ⇓(r1,w0,r0); ⇕(r0)}` (8n) 을 추가하고 커버리지를 재라. March X 와 무엇이 같고 무엇이 다른가?

<details>
<summary>답</summary>

단일 결함 표에서는 March X 와 같다: SAF·TF·CFin·AF 100%, CFid 50%, CFst 75%. 차이는 linked 행에서 보인다: TF+CFid 에서 March X 81.2% → March Y 100% (연습 3 표). 쓰기 직후 읽기(`w1,r1`)가 전이 실패를 그 자리에서 확인하기 때문이다.

</details>

### 연습 3 — linked fault: March C- vs March A vs March B

한 victim 에 결함 두 개를 겹쳐(n = 8) 커버리지를 재라: (a) CFid+CFid 모든 조합, (b) 가리기 쌍(같은 전이 방향, 반대 강제값), (c) TF+CFid. March A = `{⇕(w0); ⇑(r0,w1,w0,w1); ⇑(r1,w0,w1); ⇓(r1,w0,w1,w0); ⇓(r0,w1,w0)}` (15n).

<details>
<summary>답 (실제 출력)</summary>

```text
[연습 3] linked faults on one victim, n=8 cells
linked model     #inst     MATS+    MATS++   March X   March Y  March C-   March A   March B
CFid+CFid (all)   5376     50.0%     50.0%     62.5%     62.5%     91.7%    100.0%    100.0%
CFid+CFid mask    1344     41.7%     41.7%     50.0%     50.0%     66.7%    100.0%    100.0%
TF+CFid            448     56.2%     87.5%     81.2%    100.0%    100.0%    100.0%    100.0%
linked <up;1>@2 + <up;0>@4 -> v=7: March C- MISSES, March A detects
```

March A/B 가 linked CFid 를 잡는다는 문헌 주장과 일치한다. 이유는 §5 그림: March A 의 `⇑(r1,w0,w1)` 처럼 **v 가 1 을 기대하는 element 안에서 a↑ 를 만들면**, 나중 aggressor 의 강제값 0 이 기대값과 어긋난다. TF+CFid 가 March C- 에서도 100% 인 것은 내 모델 차이다(§5 "문헌과 다른 점" 3).

</details>

### 연습 4 — 정규형으로 되돌리기

`march_format(alg, buf, cap, ascii)` 를 짜서 element 리스트를 `"{⇕(w0); ⇑(r0,w1); ...}"` (또는 ASCII `"{any(w0); up(r0,w1); ...}"`) 로 출력하라. 모든 알고리즘에서 `parse(format(x)) == x` 왕복을 CHECK 하고, 버퍼가 작으면 -1 을 돌려라.

<details>
<summary>답</summary>

`snprintf` 를 이어 쓰며 매번 `w >= cap - o` 면 -1. 정규형이 있으면 로그·리포트에 "실제로 돌린 알고리즘" 을 별칭 섞인 입력과 상관없이 같은 문자열로 남길 수 있다. 실제 출력: `{any(w0); up(r0,w1); up(r1,w0); down(r0,w1); down(r1,w0); any(r0)}`.

</details>

### 연습 5 — bring-up 시간 표

T = k · N · N_bg · t_access 로 세 메모리의 시간을 모든 알고리즘에 대해 표로 만들어라 (가정값: 128 KiB TCM 32-bit 1 bg 5 ns, 2 MiB SRAM 32-bit 6 bg 10 ns, 1 GiB DDR 64-bit 1 bg 2 ns 순차).

<details>
<summary>답 (실제 출력)</summary>

```text
memory                                 MATS+    MATS++   March X   March Y  March C-   March A   March B
128 KiB TCM, 32b, 1 bg, 5 ns           0.8ms     1.0ms     1.0ms     1.3ms     1.6ms     2.5ms     2.8ms
2 MiB SRAM, 32b, 6 bg, 10 ns         157.3ms   188.7ms   188.7ms   251.7ms   314.6ms   471.9ms   534.8ms
1 GiB DDR, 64b, 1 bg, 2 ns (seq)    1342.2ms  1610.6ms  1610.6ms  2147.5ms  2684.4ms  4026.5ms  4563.4ms
```

접근 시간은 **가정값**이다 (non-cacheable 버스 접근은 수~수십 ns, DDR 순차 2 ns 는 burst 로 묶였을 때의 낙관값). 실제 보드에서는 작은 영역으로 한 번 재서 이 식에 넣는다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/MEM-B09_pr && /tmp/MEM-B09_pr   # 42/42 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o /tmp/MEM-B09_pas && /tmp/MEM-B09_pas
```

## 10. 핵심 정리

- March = 주소를 한 방향으로 행진하며 **한 주소에서** 같은 op 열을 실행. 복잡도 k·n. MATS+ 5n, MATS++ 6n, March X 6n, March Y 8n, March C- 10n, March A 15n, March B 17n.
- 알고리즘은 **데이터**: 문법을 정하고 파서 하나 + 엔진 하나. ASCII 별칭, 바이트 위치 오류, 고정 용량, 실행 전 기대값 검증.
- "검출" = 관련 셀의 **모든 power-up 상태에서** FAIL. 한 번 돌려 FAIL 은 운.
- 측정: MATS+ TF↓ 0%(전이 뒤 읽기 없음), CFin↓ a<v 0%(⇓ 에서 v 를 다시 안 읽음), March X 가 CFin 을, March C- 가 CFid/CFst 를 100% 로 만든다.
- ⇑ 와 ⇓ 가 둘 다 필요한 이유 = aggressor 가 victim 보다 앞에 처리되는 경우만 같은 element 에서 보인다. ⇓ 는 ⇑ 의 정확한 역순이면 된다.
- March B 의 +7n 은 단일 결함이 아니라 **linked CFid** 를 위한 것 (가리기 쌍: March C- 66.7% → March A/B 100%).
- 선택 = 커버리지 표 × 시간 예산(k·N·N_bg·t). DRAM 은 retention/scramble/Row Hammer 가 따로 필요.

## 참고자료

- A. J. van de Goor, *Testing Semiconductor Memories: Theory and Practice*, Wiley, 1991 — March 표기, 결함 모델, MATS+/March X/Y/C-/A/B 커버리지 표
- R. Nair, "Comments on 'An Optimal Algorithm for Testing Stuck-at Faults in Random Access Memories'", *IEEE Trans. Computers*, 1979 — MATS+ 의 출처로 흔히 인용 (아래 Suk & Reddy, Marinescu 와 함께 서지 정보는 기억 기반 — 원문 확인 권장)
- D. S. Suk, S. M. Reddy, "A March Test for Functional Faults in Semiconductor Random Access Memories", *IEEE Trans. Computers*, C-30(12), 1981 — March A, March B
- M. Marinescu, "Simple and Efficient Algorithms for Functional RAM Testing", ITC 1982 — March C
- S. Al-Harbi, S. K. Gupta, "Generating Complete and Optimal March Tests for Linked Faults in Memories", VLSI Test Symposium 2003 — March A/B 가 겨냥한 linked fault 집합에 대해 최적
- [Microchip online docs — Class B 자가진단 RAM 테스트 배경: March C- / March B 정의, 10n / 17n, CFid-TF · CFid-CFid linked 커버 (이 레슨 작성 시 확인)](https://onlinedocs.microchip.com/oxy/GUID-1BC922B5-0BDE-4D42-AC92-68359BB22BEC-en-US-1/GUID-81A68B22-3BC9-479D-A63A-DAD6B44A852F.html)
- S. Hamdioui, *Testing Static Random Access Memories*, Springer, 2004 — linked fault, 읽기 결함(DRDF), March SS
- 관련 레슨: SIV-B07 (March C- element 테이블 엔진, data background, DRDF), SIV-B05 (walking 1/0), MEM-B08 (Barr 식 bring-up 3단계 · 주소선 결함), MEM-B07 / SIV-M14 (ECC 가 결함을 가리는 문제), MEM-B01 (주소 매핑), MEM-B05 (refresh/retention), MEM-A02 (Row Hammer), MEM-A06 (patrol scrub · CE/UE)
