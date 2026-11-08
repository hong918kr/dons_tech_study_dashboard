---
id: C-A10
time: 110분
updated: 2026-11-08
---
# C-A10 MISRA 스타일 정적 검사와 리팩터링

## 학습 목표

- `-Weverything` 과 `clang-tidy`(bugprone/cert/misc/readability) 결과 150개를 **진짜 버그 / 잠재 위험 / 스타일·소음** 으로 분류(triage)하고, 각 항목을 MISRA C:2012 스타일 규칙 번호(10.x essential type, 16.x switch, 17.2 재귀, 20.7 매크로 괄호, 21.3/21.6 등)에 연결해 설명할 수 있다.
- 도구가 **못 잡는** 결함 — 함수 호출이 든 매크로 인자의 이중 평가, 8진수 상수, 뒤로 가는 goto, 한 식 안의 두 레지스터 읽기 순서 — 을 직접 재현하고 리뷰 체크리스트로 메울 수 있다.
- 리팩터링의 계약을 **동등성 도메인(old == new) + 의도된 차이 목록(D1..D6) + 보존할 레거시 동작** 으로 나누고, 전수(65536) · PRNG 차등 테스트(differential test)로 증명한다.
- 재귀 파서를 **명시적 스택 + 깊이 상한** 으로 바꾸고, `-fstack-usage` 로 Cortex-M4 의 프레임 크기를 실측해 "왜 재귀가 금지인가" 를 숫자로 말할 수 있다.
- 경고를 없애려고 넣은 캐스트·`u` 접미사가 **동작을 바꾸는** 함정(`n - 1u`, `(uint8_t)div`)을 알아보고, 실무용 경고 세트를 고를 수 있다.

## 1. 문제

안전 인증(ISO 26262, IEC 61508, DO-178C)을 받는 펌웨어 팀의 시니어 면접에서는 "MISRA 가 뭔지" 보다 **"레거시 코드를 정적 분석 결과로 고치되, 동작이 안 바뀌었다는 걸 어떻게 보장하느냐"** 를 묻는다. 경고 0개 만들기는 쉽다 — 캐스트를 뿌리면 된다. 어려운 건 그 캐스트가 버그를 숨겼는지, 고친 것이 필드의 장치 동작을 바꿨는지 아는 것이다.

`legacy_driver.c` 는 가상의 "LS200 온도 + FIFO 센서 허브" 드라이버다 (311줄). 10년 동안 제품에 들어갔다고 가정한다. 팀의 기존 빌드 옵션으로는 **경고 0개**, 자체 시험(selftest)도 **통과** 한다:

```text
$ cc -std=c11 -Wall -Wextra -Werror legacy_driver.c demo_legacy_main.c -o legacy && ./legacy
ls0 @0x48 up
odr 8 Hz div 400
ls0: run=0 rate=8 err=0 temp=25000
  reg 00 = 5a
  reg 01 = 81
  reg 02 = 01
  reg 03 = 90
  reg 10 = 01
  reg 11 = 90
selftest ok=1
```

그런데 같은 파일에 `-Weverything` 을 걸면 39개, `clang-tidy` 는 111개를 낸다. 위 출력에도 이미 버그가 하나 보인다: 분주기(divider)를 400 이라고 찍었는데 레지스터 `0x03` 에는 `0x90`(=144)이 들어 있다. 이 레슨의 작업 흐름:

```svg
<svg viewBox="0 0 740 240" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">레거시 리팩터링 파이프라인: 도구 → 분류 → 고정 → 수정 → 증명</text>
  <rect x="10" y="40" width="130" height="74" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="60" fill="currentColor">① 도구 실행</text>
  <text x="20" y="80" fill="currentColor" font-family="monospace" font-size="12">-Weverything</text>
  <text x="20" y="96" fill="currentColor" font-family="monospace" font-size="12">clang-tidy</text>
  <text x="20" y="110" fill="currentColor" font-family="monospace" font-size="12">--analyze</text>
  <rect x="158" y="40" width="130" height="74" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="168" y="60" fill="currentColor">② 분류 (triage)</text>
  <text x="168" y="80" fill="currentColor" font-size="12">진짜 버그</text>
  <text x="168" y="96" fill="currentColor" font-size="12">잠재 위험</text>
  <text x="168" y="110" fill="currentColor" font-size="12">스타일·소음</text>
  <rect x="306" y="40" width="130" height="74" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="316" y="60" fill="currentColor">③ 특성화 테스트</text>
  <text x="316" y="80" fill="currentColor" font-size="12">old 동작을</text>
  <text x="316" y="96" fill="currentColor" font-size="12">테스트로 못 박기</text>
  <text x="316" y="110" fill="currentColor" font-size="12">(버그 포함)</text>
  <rect x="454" y="40" width="130" height="74" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="464" y="60" fill="currentColor">④ 리팩터</text>
  <text x="464" y="80" fill="currentColor" font-size="12">new_* 작성</text>
  <text x="464" y="96" fill="currentColor" font-size="12">MISRA 스타일</text>
  <rect x="602" y="40" width="130" height="74" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="612" y="60" fill="currentColor">⑤ 증명</text>
  <text x="612" y="80" fill="currentColor" font-size="12">동등성 (old==new)</text>
  <text x="612" y="96" fill="currentColor" font-size="12">+ 의도된 차이</text>
  <text x="612" y="110" fill="currentColor" font-size="12">D1..D6 만</text>
  <path d="M140 77 L158 77 M288 77 L306 77 M436 77 L454 77 M584 77 L602 77" stroke="currentColor"/>
  <path d="M667 114 L667 150 L75 150 L75 114" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="190" y="144" fill="currentColor" font-size="12">⑥ 다시 도구 실행: 새 코드 구간은 -Weverything 0개 (§5 실측)</text>
  <rect x="10" y="166" width="722" height="64" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="188" fill="currentColor">도구가 못 잡는 것 (이중 평가되는 함수 호출, 8진수, 뒤로 가는 goto, 읽기 순서)은</text>
  <text x="22" y="208" fill="currentColor">리뷰 체크리스트 + ③의 테스트가 잡는다. 도구는 그물의 한 겹일 뿐이다 (C-M12 의 표와 같은 결론).</text>
  <text x="22" y="224" fill="currentColor" font-size="12">목록에 없는 old/new 차이는 전부 회귀(regression)로 취급한다.</text>
</svg>
```

`starter.c` 에는 레거시의 핵심 함수 7개가 `old_*` 로 **글자 그대로** 복사되어 있다 (고치지 않는다 — 동작의 기준이다). 같은 일을 하는 `new_*` 를 MISRA 스타일로 쓴다: 고정 폭 타입, 명시적 변환, 모든 if/for 에 중괄호, 모든 switch 절에 `break` + `default`, 재귀 없음, 단일 출구, 매크로 대신 함수, 부작용 있는 레지스터는 정확히 한 번 읽기. 하네스는 old 와 new 를 같은 입력 수만 개로 돌려 비교한다.

### Part A — 암시적 변환과 essential type

- `int32_t temp_lsb(uint16_t raw)` — 12비트 2의 보수(two's complement) 원시값 → −2048..2047. 상위 4비트 무시. 레거시는 `short v = raw << 4; v = v >> 4;` 로 **구현 정의 동작 두 개** 에 기댄다 — 그것 없이.
- `int32_t new_temp_mc(uint16_t raw)` — 밀리°C. `old_temp_mc` 와 **65536개 입력 전부** 에서 같아야 한다 (−62.5 → −62 같은 0 쪽 버림까지).
- `int16_t new_avg(const int16_t *s, uint32_t n)` — 합 ≥ 0 이면 old 와 같다. **D1**: `n == 0` 또는 `n > 4096` → 0. **D2**: 합이 음수면 진짜 평균 (old 는 쓰레기).
- `bool new_frame_ok(const uint8_t *buf, uint8_t n)` — 마지막 바이트가 앞 바이트들의 8비트 합인가. `n >= 1` 에서 old 와 같다. **D3**: `n == 0` → `false`.

### Part B — 부작용 있는 매크로 인자, switch fall-through

- `uint8_t new_fifo_peak(uint32_t n)` — FIFO 데이터 레지스터를 **정확히 n 번** 읽고 최대값. **D4**: 레거시는 `MAX(bus_read(...), peak)` 매크로 때문에 샘플을 잃는다 — 테스트가 그 동작을 먼저 고정해 보여 준다.
- `int32_t new_temp_cal_mc(uint16_t raw, int32_t offset_lsb)` — `(원시값 + 보정) × 62.5`. **D5**: 레거시의 괄호 없는 `SCALE_MC` 매크로.
- `int new_cmd(ls_dev *d, int cmd, int arg)` — 명령 처리기. 레거시 switch 의 fall-through 두 개의 **효과는 그대로** (필드의 호스트 툴이 거기에 기댄다), 단 명시적으로. 2만 단계 무작위 명령열에서 상태가 old 와 같아야 한다.

### Part C — 재귀 제거: 설정 TLV 파서

호스트 툴이 UART 로 보내는 설정 블롭은 TLV(type-length-value)다: `[type][len][payload]`, type 의 bit7 이 1이면 컨테이너(payload 가 다시 원소들). 레거시 `ls_tlv_sum` 은 컨테이너마다 **자기 자신을 호출** 한다.

- `int32_t new_tlv_sum(const uint8_t *p, size_t len)` — 재귀 없이, 결과와 **에러를 발견하는 순서** 까지 old 와 같게. **D6**: 컨테이너 중첩이 `TLV_MAX_DEPTH`(8)을 넘으면 `TLV_ETOODEEP`(−2).
- 무작위 유효 트리 3000개(깊이 ≤ 8) + 1바이트 변조·잘라낸 입력 5000개로 동등성 확인. Cortex-M4 스택 사용량은 `demo_stack.c` 로 실측한다.

## 2. Problem (English)

> **Static-analysis-driven refactor of a legacy sensor driver, with behavioural equivalence.**
>
> `legacy_driver.c` (311 lines) builds warning-free with `-Wall -Wextra` and its self-test passes. Run `-Weverything`, `clang-tidy` (bugprone/cert/misc/readability) and `clang --analyze` on it; triage every finding as *real bug / latent risk / style-noise* and map it to a MISRA C:2012-style guideline. Then rewrite seven functions (`old_*` in `starter.c`, copied verbatim) as MISRA-style `new_*`:
>
> 1. `temp_lsb`, `new_temp_mc` (12-bit two's complement → m°C, identical for all 65536 inputs, no implementation-defined behaviour), `new_avg` (int32 accumulator; 0 for `n == 0` or `n > 4096`; true truncated mean for negative sums), `new_frame_ok` (`false` for `n == 0`).
> 2. `new_fifo_peak` (exactly `n` reads of the pop-on-read FIFO register), `new_temp_cal_mc` (`(lsb + offset) * 625 / 10`), `new_cmd` (same state transitions as the fall-through switch, made explicit; unknown commands ignored).
> 3. `new_tlv_sum` — non-recursive, bounded nesting depth 8 (`-2` beyond), otherwise same result and same first error as the recursive version.
>
> The harness checks old == new on the *equivalence domain* (exhaustive 16-bit, xorshift32 random sequences) and checks each intended difference D1–D6 explicitly. Any other difference is a regression.
>
> Follow-ups: Which findings are noise? What did no tool catch? Is a cast a fix? Would you fix a bug you found during a refactor in the same commit? How do you bound stack usage without recursion? What does "MISRA compliant" mean for a project with deviations?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "6/32 passed" (레거시 동작을 고정하는 테스트 일부만 통과) — TODO 를 채워 32/32
```

분석 대상과 관찰용 파일 (하네스와 별개):

```bash
TIDY=~/.venvs/clangtidy/bin/clang-tidy                  # LLVM 23.1.4 (pip wheel). brew llvm 의 clang-tidy 도 같다
cc -std=c11 -Wall -Wextra -Werror legacy_driver.c demo_legacy_main.c -o legacy && ./legacy   # 경고 0, selftest ok=1
cc -std=c11 -Weverything -c legacy_driver.c -o /dev/null                                     # 39개
$TIDY legacy_driver.c --checks='-*,bugprone-*,cert-*,misc-*,readability-*' -- -std=c11 -isysroot $SDKROOT   # 111개
cc -std=c11 --analyze legacy_driver.c -o /dev/null                                           # 정적 분석기 1개
cc -std=c11 -g -O1 -fsanitize=implicit-conversion legacy_driver.c demo_legacy_main.c -o lic && ./lic   # 런타임 잘림 검출
cc -std=c11 -Wall -Wextra -O2 demo_tool_gaps.c -o dtg && ./dtg                                # 도구가 못 잡는 것들
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -fstack-usage -c demo_stack.c -o /tmp/ds.o && cat /tmp/ds.su
```

`clang-tidy` 의 `--` 뒤는 컴파일 옵션이다. 맥에서는 `-isysroot` 로 SDK 를 줘야 `<stdio.h>` 를 찾는다 (compile_commands.json 이 있는 프로젝트면 `-p build/` 로 대신한다).

## 4. 힌트

<details>
<summary>힌트 1 — 분류의 기준: "이 경고를 고치면 동작이 바뀌는가?"</summary>

경고마다 세 질문: ① 지금 입력에서 **틀린 값** 이 나오는가 (진짜 버그) ② 지금은 맞지만 다른 컴파일러/입력/타깃에서 틀릴 수 있는가 (잠재 위험) ③ 둘 다 아닌가 (스타일·소음). ①은 고치면 동작이 바뀌므로 **의도된 차이 목록** 에 올리고 테스트로 old 값과 new 값을 둘 다 못 박는다. ②③은 동작이 그대로여야 한다.

</details>

<details>
<summary>힌트 2 — Part A: 부호 확장은 마스크 + 뺄셈으로</summary>

`raw & 0x0FFF` 는 0..4095 (unsigned 끼리라 안전). 0x800 이상이면 4096 을 빼면 음수가 된다 — 시프트도, 범위 밖 변환도 없다. MISRA 10.8 은 복합식(composite expression) `(int32_t)(raw & mask)` 처럼 **연산 결과를 다른 부류로 캐스트** 하는 걸 싫어하니, 일단 `uint16_t` 변수에 담은 뒤 캐스트한다. 나눗셈은 C99 부터 0 쪽 버림(6.5.5p6)이라 old 와 같다.

</details>

<details>
<summary>힌트 3 — Part B: 조건 연산자는 고른 쪽을 "다시" 평가한다</summary>

`MAX(a, b)` 는 `((a) > (b) ? (a) : (b))` 다. `a` 가 `bus_read(...)` 면 비교에서 한 번, 이겼을 때 결과로 **또 한 번** 읽는다 (6.5.15p4). FIFO 데이터 레지스터는 읽으면 꺼내지므로(pop) 샘플이 사라진다. 읽기는 변수에 한 번 담고 비교한다. `SCALE_MC(v + off)` 는 손으로 펼쳐 보라.

</details>

<details>
<summary>힌트 4 — Part B: fall-through 를 "효과" 로 번역</summary>

`case RESET: ...; /* 흐름 */ case START: running = 1; break;` 는 "RESET 은 rate/err 를 초기화하고 **그다음 START 가 하는 일도 한다**" 는 뜻이다. 각 case 끝에 break 를 두고, 흘러 들어가던 case 의 문장을 복사(또는 공통 함수 호출)한다. `CMD_SET_RATE` 도 `CMD_STOP` 으로 흘러간다 — 버그처럼 보여도 **고치지 않는다** (힌트 1 의 ①이 아니라 "필드가 기대는 동작").

</details>

<details>
<summary>힌트 5 — Part C: 재귀의 "돌아갈 곳" 을 배열로</summary>

재귀 호출이 스택에 저장하던 것은 "부모 깊이에서 아직 안 읽은 바이트 수" 뿐이다. `rem[depth]` 배열에 두고, 원소 하나를 읽을 때 **부모 몫에서 원소 전체(헤더+payload)를 먼저 뺀 뒤** 컨테이너면 `depth++` / `rem[depth] = len`. `rem[depth] == 0` 이면 `depth--` (= return). 방문 순서가 재귀와 같으니 첫 에러도 같다.

</details>

## 5. 풀이

### 0. 도구를 돌린다 — 실측

**`-Weverything` (Apple clang 21) — 39개:**

```text
$ cc -std=c11 -Weverything -c legacy_driver.c -o /dev/null 2>&1 | grep -o '\[-W[a-z0-9-]*\]' | sort | uniq -c | sort -rn
  18 [-Wmissing-prototypes]
   8 [-Wimplicit-int-conversion]
   3 [-Wpadded]
   2 [-Wsign-conversion]
   2 [-Wimplicit-void-ptr-cast]
   2 [-Wimplicit-fallthrough]
   1 [-Wunused-macros]
   1 [-Wswitch-default]
   1 [-Wmissing-variable-declarations]
   1 [-Wformat]
```

prototypes 를 뺀 실제 줄 (발췌):

```text
legacy_driver.c:100:19: warning: implicit conversion loses integer precision: 'int' to 'short' [-Wimplicit-int-conversion]
legacy_driver.c:115:20: warning: implicit conversion loses integer precision: 'int' to 'short' [-Wimplicit-int-conversion]
legacy_driver.c:126:16: warning: implicit conversion loses integer precision: 'unsigned int' to 'short' [-Wimplicit-int-conversion]
legacy_driver.c:126:12: warning: implicit conversion changes signedness: 'int' to 'unsigned int' [-Wsign-conversion]
legacy_driver.c:155:12: warning: implicit conversion when assigning to 'uint8_t *' (aka 'unsigned char *') from type 'void *' is not permitted in C++ [-Wimplicit-void-ptr-cast]
legacy_driver.c:167:5: warning: 'switch' missing 'default' label [-Wswitch-default]
legacy_driver.c:172:5: warning: unannotated fall-through between switch labels [-Wimplicit-fallthrough]
legacy_driver.c:180:5: warning: unannotated fall-through between switch labels [-Wimplicit-fallthrough]
legacy_driver.c:232:28: warning: implicit conversion loses integer precision: 'int' to 'uint8_t' (aka 'unsigned char') [-Wimplicit-int-conversion]
legacy_driver.c:45:9: warning: padding struct 'ls_dev' with 3 bytes to align 'running' [-Wpadded]
legacy_driver.c:27:9: warning: macro is not used [-Wunused-macros]
```

**`clang-tidy` (LLVM 23.1.4) — 111개:**

```text
471 warnings generated.
Suppressed 359 warnings (359 in non-user code).
  28 [readability-identifier-length]
  25 [readability-braces-around-statements]
  19 [misc-use-internal-linkage]
  13 [readability-magic-numbers]
  10 [bugprone-signed-bitwise]
   6 [bugprone-narrowing-conversions]
   2 [readability-isolate-declaration]
   2 [misc-no-recursion]
   1 [readability-math-missing-parentheses]
   1 [bugprone-unhandled-code-paths]
   1 [bugprone-switch-missing-default-case]
   1 [bugprone-macro-parentheses]
   1 [bugprone-easily-swappable-parameters]
   1 [bugprone-assignment-in-if-condition]
```

`cert-*` 은 0개였다. 이 버전의 cert 체크는 대부분 다른 체크의 별칭(alias)이고(`cert-dcl16-c` = 대문자 접미사, `cert-err33-c` = 반환값 무시 등), `cert-err33-c`(반환값 무시) 의 감시 목록에는 `printf` 가 없다 — 작은 파일로 확인해 보면 `fputs`·`malloc` 의 반환값 무시는 잡고 `printf` 는 조용하다. 소음 3종(이름 길이·중괄호·매직 넘버)을 뺀 나머지 중 핵심:

```text
legacy_driver.c:36:33: warning: macro replacement list should be enclosed in parentheses [bugprone-macro-parentheses]
legacy_driver.c:100:15: warning: narrowing conversion from 'int' to signed type 'short' is implementation-defined [bugprone-narrowing-conversions]
legacy_driver.c:101:9: warning: use of a signed integer operand with a binary bitwise operator [bugprone-signed-bitwise]
legacy_driver.c:109:25: warning: '/' has higher precedence than '+'; add parentheses to explicitly specify the order of operations [readability-math-missing-parentheses]
legacy_driver.c:126:12: warning: narrowing conversion from 'unsigned int' to signed type 'short' is implementation-defined [bugprone-narrowing-conversions]
legacy_driver.c:192:5: warning: function 'ls_tlv_sum' is within a recursive call chain [misc-no-recursion]
legacy_driver.c:219:12: warning: function 'gcd' is within a recursive call chain [misc-no-recursion]
legacy_driver.c:242:12: warning: an assignment within an 'if' condition is bug-prone [bugprone-assignment-in-if-condition]
legacy_driver.c:271:5: warning: use of a signed integer operand with a binary bitwise operator [bugprone-signed-bitwise]
```

**정적 분석기와 런타임 검사 — 각각 1개, 둘 다 다른 도구가 못 본 것:**

```text
$ cc -std=c11 --analyze legacy_driver.c
legacy_driver.c:126:16: warning: Division by zero [core.DivideZero]

$ cc -std=c11 -g -O1 -fsanitize=implicit-conversion legacy_driver.c demo_legacy_main.c -o lic && ./lic
legacy_driver.c:232:28: runtime error: implicit conversion from type 'int' of value 400 (32-bit, signed) to type 'uint8_t' (aka 'unsigned char') changed the value to 144 (8-bit, unsigned)
```

`-fsanitize=implicit-conversion` 은 `-fsanitize=undefined` 에 **포함되지 않는다** (잘림은 UB 가 아니라 정의된 변환이므로). 하지만 펌웨어에서 가장 흔한 "조용한 오답" 이 바로 이것이라, 호스트 유닛 테스트에 따로 켜 둘 가치가 있다.

### 분류 — 경고를 규칙과 판정으로

MISRA C:2012 규칙 번호는 내가 확신하는 것만 적었다. 규칙 문장은 유료 문서이므로 인용하지 않고 취지만 쓴다 ("MISRA-style").

| 발견 (도구, 개수) | MISRA-style 규칙 (취지) | 판정 | 이 레거시에서 |
|---|---|---|---|
| `-Wimplicit-int-conversion` 8, `-Wsign-conversion` 2, `bugprone-narrowing-conversions` 6 | 10.3 더 좁은/다른 부류로 대입 금지, 10.4 산술 피연산자 부류 일치 | **켜 둠** — 3개가 진짜 버그 | 115:20 `last_temp`(short)에 m°C → **32.767 °C 넘으면 음수**. 232:28 ODR 분주기 잘림. 126 avg (D2) |
| `bugprone-signed-bitwise` 10 | 10.1 부호 있는 피연산자에 비트 연산 금지 | 켜 둠 (대부분 잠재) | `t & 0x80`, `1 << b` — `0x80u`, `1u << b` |
| `-Wimplicit-fallthrough` 2, `-Wswitch-default` 1, tidy switch 2 | 16.3 모든 절은 break 로 끝, 16.4 default 필수 | 켜 둠 | 하나는 의도, 하나는 우연 — 둘 다 효과 보존 (Part B) |
| `misc-no-recursion` 2 | 17.2 직·간접 재귀 금지 | 켜 둠 | TLV 파서, gcd |
| `bugprone-macro-parentheses` 1, `math-missing-parentheses` 1 | 20.7 매크로 인자 괄호, 12.1 우선순위 명시 | 켜 둠 — **진짜 버그** | `SCALE_MC(v + off)` (D5) |
| `-Wmissing-prototypes` 18, `misc-use-internal-linkage` 19, `-Wmissing-variable-declarations` 1 | 8.4 외부 정의 전에 선언, 8.7 한 파일에서만 쓰면 `static` | 켜 둠 (링크 충돌·데드 코드 탐지) | 헤더가 없다. `static` 또는 `ls200.h` |
| `bugprone-assignment-in-if-condition` 1 | 13.4 대입 결과를 쓰지 말 것 | 켜 둠 | `if ((d = malloc(...)) == 0)` |
| `-Wunused-macros` 1 | 2.5 안 쓰는 매크로 | 켜 둠 (경고만) | `CTRL_EN` |
| `--analyze` DivideZero 1 | 1.3 UB 금지 | **진짜 버그 (입력 n=0)** | `ls_avg(s, 0)` (D1) |
| `readability-braces-around-statements` 25 | 15.6 if/for/while 몸체는 블록 | 켜 둠 (`goto fail` 류 예방) | 기계적 수정 |
| `readability-magic-numbers` 13 | (MISRA 규칙 아님, 스타일) | 레지스터 맵은 이름 붙이기, 테스트 값은 끔 | |
| `readability-identifier-length` 28, `isolate-declaration` 2 | 없음 | **끔** (소음) | `i`, `p`, `d` 는 관례 |
| `-Wpadded` 3 | 없음 | **끔** — 와이어 구조체에만 (C-A08 의 `_Static_assert` 가 낫다) | |
| `-Wimplicit-void-ptr-cast` 2 | 없음 (C++ 호환) | **끔** — C 에서 `malloc` 캐스트는 오히려 안 한다 | |
| `bugprone-easily-swappable-parameters` 1 | 없음 (API 설계) | 판단 — `ls_cmd(d, cmd, arg)` 는 API 유지 | |
| (아무 도구도 안 잡음) | 7.1 8진수, 15.2 뒤로 가는 goto, 13.2 평가 순서, 21.3 `malloc`, 21.6 `stdio`, Dir 4.9 함수형 매크로 | **리뷰가 잡는다** | 아래 절 |

`-Weverything` 의 나머지 소음도 알아 두자: 새 코드(solution.c 하네스)에선 `-Wdeclaration-after-statement`(C89 호환), `-Wpre-c11-compat`(`_Static_assert`)가 나왔다. Clang 문서도 `-Weverything` 은 새 경고를 **찾아보는** 용도이지 일반 빌드용이 아니라고 말한다. 실무 결론은 맨 끝 "어떤 경고 세트를 켤까".

### 도구가 못 잡은 것 — `demo_tool_gaps.c` 실측

```text
$ ./dtg
peak_incr = 92  (m*10 + i)
peak_call = 144, bus reads = 2 (1 expected)
read16 = 0x0190  (H=0x01 first -> 0x0190, L first -> 0x9001)
k_hz = 1 5 8 40 100
retry: 3 -1
ft: 111 110 100
```

같은 파일에 도구를 걸면:

```text
$ cc -std=c11 -Weverything -c demo_tool_gaps.c
demo_tool_gaps.c:48:5: warning: unannotated fall-through between switch labels [-Wimplicit-fallthrough]
demo_tool_gaps.c:58:9: warning: mixing declarations and code is incompatible with standards before C99 [-Wdeclaration-after-statement]
$ clang-tidy demo_tool_gaps.c --checks='-*,bugprone-*,cert-*,misc-*' -- ...
demo_tool_gaps.c:14:46: warning: use of a signed integer operand with a binary bitwise operator [bugprone-signed-bitwise]
demo_tool_gaps.c:19:17: warning: side effects in the 1st macro argument 'a' are repeated in macro expansion [bugprone-macro-repeated-side-effects]
demo_tool_gaps.c:25:56: warning: use of a signed integer operand with a binary bitwise operator [bugprone-signed-bitwise]
demo_tool_gaps.c:25:70: warning: use of a signed integer operand with a binary bitwise operator [bugprone-signed-bitwise]
```

| 결함 | 실행 결과 | 잡힌 곳 |
|---|---|---|
| ① `MAX(p[i++], peak)` | i 가 2번 증가 (92 = 9×10 + 2) | tidy `macro-repeated-side-effects` (19:17) ✔ |
| ① `MAX(bus_read(), peak)` | 읽기 **2번**, 두 번째 값(0x90=144) 반환 | **아무것도 없음** — 체크는 `++`/대입만 부작용으로 본다 |
| ② `bus_read() << 8 \| bus_read()` | 이 빌드에선 왼쪽 먼저 (0x0190) | **없음**. 순서는 미지정(6.5p3, 함수 호출은 6.5.2.2p10 에 따라 indeterminately sequenced) — 컴파일러·최적화가 바꿔도 표준 위반이 아니다 |
| ③ `010`, `050` | 8, 40 | **없음** (clang-tidy 에 8진수 체크가 없다) |
| ④ 뒤로 가는 `goto again` | 동작은 맞음 | **없음**. `cppcoreguidelines-avoid-goto` 는 있지만 C 파일에선 아무것도 안 냈다 (실측) |
| ⑤ 표시 없는 fall-through | `ft(1)=110` | `-Wimplicit-fallthrough` ✔ (`__attribute__((fallthrough))` 로 표시한 0번 절은 경고 없음) |

②가 레거시 `ls_read_temp` (114줄) 에 그대로 있다: 16비트 값을 8비트 두 번에 읽는 하드웨어는 **한쪽을 읽는 순간 다른 쪽을 래치(latch)** 하는 경우가 많아서, 순서가 틀리면 지난번 래치 값과 새 값이 섞인다 (C-M14 의 찢어진 읽기와 같은 모양). 어느 쪽이 먼저인지는 칩마다 다르다 — 그러니 더더욱 코드가 순서를 **고정** 해야 한다. 고침은 문장을 나누는 것 — `;` 가 순서를 고정한다 (추가 연습 3).

### 수정 정책 — 무엇을 같게, 무엇을 다르게

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">한 함수의 입력 공간을 세 구역으로 나누고, 구역마다 다른 것을 검사한다</text>
  <rect x="10" y="34" width="720" height="150" rx="8" fill="none" stroke="currentColor"/>
  <rect x="20" y="44" width="340" height="130" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="32" y="66" fill="currentColor" font-weight="bold">동등성 도메인</text>
  <text x="32" y="88" fill="currentColor">검사: old(x) == new(x)</text>
  <text x="32" y="108" fill="currentColor" font-size="12">temp 65536개 전수, avg 합≥0, frame n≥1,</text>
  <text x="32" y="126" fill="currentColor" font-size="12">cmd 2만 단계, TLV 유효 트리 3000 + 변조 5000</text>
  <text x="32" y="148" fill="currentColor" font-size="12">레거시 "이상한" 동작도 여기 포함:</text>
  <text x="32" y="164" fill="currentColor" font-size="12">SET_RATE 가 멈춤, −62.5 → −62, 상위 니블 무시</text>
  <rect x="370" y="44" width="170" height="130" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="380" y="66" fill="currentColor" font-weight="bold">UB 영역</text>
  <text x="380" y="88" fill="currentColor" font-size="12">old 는 호출 금지</text>
  <text x="380" y="106" fill="currentColor" font-size="12">(ASan/UBSan 이 터진다)</text>
  <text x="380" y="128" fill="currentColor" font-size="12">검사: new 의 새 계약만</text>
  <text x="380" y="150" fill="currentColor" font-family="monospace" font-size="12">D1 avg n==0</text>
  <text x="380" y="166" fill="currentColor" font-family="monospace" font-size="12">D3 frame n==0</text>
  <rect x="550" y="44" width="170" height="130" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="560" y="66" fill="currentColor" font-weight="bold">의도된 차이</text>
  <text x="560" y="84" fill="currentColor" font-size="12">검사: old 값도 고정,</text>
  <text x="560" y="100" fill="currentColor" font-size="12">new 값도 고정 (둘 다)</text>
  <text x="560" y="120" fill="currentColor" font-family="monospace" font-size="12">D2 avg 음수 합</text>
  <text x="560" y="135" fill="currentColor" font-family="monospace" font-size="12">D4 FIFO 이중 pop</text>
  <text x="560" y="150" fill="currentColor" font-family="monospace" font-size="12">D5 SCALE_MC 괄호</text>
  <text x="560" y="165" fill="currentColor" font-family="monospace" font-size="12">D6 깊이 &gt; 8</text>
  <rect x="10" y="196" width="720" height="64" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="218" fill="currentColor">차이 목록은 리뷰어가 승인하는 "계약 변경서" 다. 목록 밖의 old≠new 는 전부 회귀.</text>
  <text x="22" y="240" fill="currentColor">old 값을 고정한 테스트 = 그 버그가 있었다는 증거 (릴리스 노트·필드 영향 분석의 근거).</text>
</svg>
```

| ID | 함수 | old (고정된 값) | new | 왜 바꾸나 |
|---|---|---|---|---|
| D1 | avg | `n==0` → 0 으로 나눔 (UB, `--analyze` 가 찾음) | 0 | UB 는 "동작" 이 아니다 — 보존할 수 없다 |
| D2 | avg | `{-3,-3,-3}` → **21842** | −3 | 부호 섞인 나눗셈 버그. 음수 온도 평균이 전부 틀림 |
| D3 | frame_ok | `n==0` → `buf[-1]` 읽기 (UB) | `false` | 범위 밖 접근 (18.1) |
| D4 | fifo_peak | `{1,2,3,4}`, n=2 → 4, FIFO **전부 비움** | 2, 2개 남음 | 하드웨어 데이터 유실 |
| D5 | temp_cal | 보정 0 이어도 **LSB 를 반환** (m°C 가 아님) | `(v+off)×62.5` | 동등성 도메인이 아예 없다 → 호출처 조사 필요 |
| D6 | tlv_sum | 깊이 9 → 7 (끝없는 재귀) | −2 | 스택 상한 (17.2) |

반대로 **보존** 한 것: `CMD_SET_RATE` 가 장치를 멈추는 동작. 버그처럼 보이지만 호스트 툴이 "SET_RATE 후 START" 를 보내도록 10년간 맞춰져 있을 수 있다. 리팩터 커밋에서는 동작을 그대로 두고 주석 + 티켓을 남긴 뒤, 고칠지는 **별도 커밋/별도 리뷰** 에서 정한다. 리팩터와 버그 수정을 한 커밋에 섞으면 회귀가 났을 때 어느 쪽 탓인지 가를 수 없다.

### Part A — 암시적 변환: 구현 정의 동작을 걷어 낸다

레거시 `ls_temp_mc`:

```c
short v = raw << 4;   /* raw → int 승격, 0..0xFFFF0. short 로 넣을 때 0x8000 이상은 범위 밖 */
v = v >> 4;           /* 음수의 >> */
return SCALE_MC(v);
```

| 단계 | C11 | 의미 |
|---|---|---|
| `raw << 4` | 6.3.1.1 정수 승격 → int, 6.5.7p4 | 음수가 아니고 int 범위 안이라 정의됨 |
| `short v = ...` | 6.3.1.3p3 | 값이 short 범위 밖이면 **구현 정의** (결과 또는 시그널) |
| `v >> 4` (v < 0) | 6.5.7p5 | **구현 정의** (산술/논리 시프트) |

clang/gcc 는 둘 다 "2의 보수 모듈러 + 산술 시프트" 라 동작했다. starter 는 이 가정을 `_Static_assert((-16 >> 4) == -1, ...)` 로 문서화한다 (C-A08). 새 코드는 둘 다 쓰지 않는다:

```c
static int32_t temp_lsb(uint16_t raw) {
    const uint16_t u = raw & TEMP_MASK;          /* 0..4095 */
    int32_t v = (int32_t)u;                      /* 값 보존 변환 (6.3.1.3p1) */
    if (v >= TEMP_SIGN) {
        v -= TEMP_MOD;                           /* 0x800..0xFFF → -2048..-1 */
    }
    return v;
}
```

`(int32_t)(raw & TEMP_MASK)` 로 한 줄에 쓰지 않은 이유: MISRA 10.8 은 **복합식의 값** 을 다른 부류(unsigned→signed)나 더 넓은 타입으로 캐스트하는 것을 금지한다 — "연산이 좁은 타입에서 이미 넘쳤는데 캐스트로 넓히면 늦었다" 는 실수를 막기 위해서다. 테스트는 65536개 전수 비교 + `0xF190 → 25000`(상위 니블 무시), `0xFFF → −62`(−62.5 의 0 쪽 버림). 반올림으로 "개선" 하고 싶어도 그건 D 목록에 올릴 **계약 변경** 이다.

**avg — 부호 섞인 나눗셈 (D1, D2):** `sum / n` 에서 `sum` 은 int, `n` 은 unsigned → 일반 산술 변환(6.3.1.8)이 `sum` 을 unsigned 로 바꾼다. `-9` → 4294967287, ÷3 → 1431655762, short 로 넣으면 21842. 런타임 검사가 정확히 그 값을 보여 준다:

```text
solution.c:73:12: runtime error: implicit conversion from type 'unsigned int' of value 1431655762 (32-bit, unsigned) to type 'short' changed the value to 21842 (16-bit, signed)
```

(`-fsanitize=implicit-conversion` 으로 solution.c 를 돌린 결과. 이 보고는 전부 old_* 줄(55, 62, 73, 81)에서만 나왔고 new_* 구간에서는 0건이었다.) 새 코드는 `int32_t` 누적 + `(int32_t)n` 으로 나눈다. `n <= 4096` 상한은 `32767 × 4096 = 134,213,632 < 2^31` 이라 합이 넘치지 않음을 보장하는 **문서화된 전제** 다.

**frame_ok — `n - 1` 의 두 얼굴 (D3):** 레거시의 `for (i = 0; i < n - 1; i++)` 는 `n` 이 int 로 승격되어 `n == 0` 이면 `-1` → 루프는 안 돌지만 바로 다음 `buf[n - 1]` 이 `buf[-1]` 을 읽는다. 여기서 MISRA 10.4 경고("unsigned 와 signed 상수를 섞지 마라")를 **기계적으로** 고쳐 `n - 1u` 로 쓰면? `0 - 1u` 는 unsigned 연산이라 **4294967295** — 이제 루프가 40억 번 돈다. 경고는 사라졌고 버그는 더 커졌다 (추가 연습 2 가 이걸 테스트로 보인다). 정답은 `n == 0` 을 **먼저** 거르는 것:

```c
if (n > 0u) {
    const uint8_t last = (uint8_t)(n - 1u);   /* n >= 1 이 보장된 뒤에만 */
    ...
    for (uint8_t i = 0u; i < last; i++) { sum = (uint8_t)(sum + buf[i]); }
```

`sum += buf[i]` (uint8_t) 의 8비트 감쌈은 컴파일 경고가 하나도 안 났지만 런타임 검사는 잡았다 — `solution.c:81:13: ... value 262 ... changed the value to 6`. 체크섬은 감쌈이 **의도** 이므로 명시적 캐스트로 적어 둔다. 명시적 캐스트는 sanitizer 가 보고하지 않는다 → "의도한 잘림" 과 "실수한 잘림" 을 도구가 구별할 수 있게 된다. 이것이 캐스트의 올바른 용도다.

### Part B — 매크로와 switch: 보이는 코드와 실행되는 코드가 다르다

**FIFO 이중 pop (D4).** `peak = MAX(bus_read(REG_FIFO_DATA), peak);` 를 펼치면

```c
peak = ((bus_read(REG_FIFO_DATA)) > (peak) ? (bus_read(REG_FIFO_DATA)) : (peak));
```

조건 연산자는 첫 피연산자를 평가한 뒤 고른 쪽 하나만 평가한다(6.5.15p4) — 고른 쪽이 또 `bus_read` 다. 테스트가 고정한 old 동작:

| FIFO | 호출 | old 결과 | 버스 읽기 | 남은 샘플 |
|---|---|---|---|---|
| 3, 9, 4 | `old_fifo_peak(3)` | 9 (정답과 같음 — selftest 통과) | **4** (빈 FIFO 를 한 번 더) | 0 |
| 1, 2, 3, 4 | `old_fifo_peak(2)` | 4 | 4 | **0** (2개만 꺼내랬는데 다 사라짐) |
| 1, 2, 3, 4 | `new_fifo_peak(2)` | 2 | 2 | 2 |

selftest 가 10년 동안 통과한 이유가 첫 줄이다: 증가-감소 패턴에서는 우연히 정답이 나온다. UART `DR`, SPI `DR`, 상태 레지스터의 read-to-clear 비트(C-M02)도 같은 함정이다. 새 코드는 읽기를 `const uint8_t v = bus_read(...)` 로 **한 번** 하고 비교한다. 타입별로 쓸 수 있는 MAX 가 필요하면 `static inline` 함수나 `_Generic` 디스패치(C-M11) — MISRA Dir 4.9 도 "바꿔 쓸 수 있으면 함수형 매크로보다 함수" 를 권한다.

**`SCALE_MC(v + off)` (D5).** `#define SCALE_MC(x) x * 625 / 10` 에 `v + g_temp_offset` 을 넣으면 `v + g_temp_offset * 625 / 10` 이 된다. 보정값이 0 이면 결과는 그냥 `v` — m°C 가 아니라 **LSB**! 하네스가 65536개 전부에서 `old_temp_cal_mc(raw) == temp_lsb(raw)` 를 확인한다. 즉 old 와 같아야 할 입력이 하나도 없다. 이런 함수를 만나면 코드보다 **호출처** 를 먼저 본다: 아무도 안 부르면 지우고(MISRA 2.x 의 데드 코드 취지), 누가 부르면 그 기능은 처음부터 잘못된 값을 보고해 온 것이다 — 버그 리포트감이지 조용한 리팩터 대상이 아니다. clang-tidy 는 `bugprone-macro-parentheses` 를 **치환 목록 전체** 에 대해서만 냈고 (36:33) 인자 `x` 와 `SET_BIT(r, b)` 의 인자 괄호(20.7)는 지적하지 않았다 — 20.7 은 리뷰로 확인해야 한다.

**switch (16.3, 16.4).** `-Wimplicit-fallthrough` 는 clang 에선 `-Wall -Wextra` 에 **들어 있지 않다** (레거시가 `-Wall -Wextra -Werror` 를 통과한 이유). GCC 는 `-Wextra` 가 켠다. 번역은 "흘러 들어가던 절의 효과를 복사하고 break":

| old (fall-through) | new (명시적) |
|---|---|
| `RESET: rate=1; err=0;` → `START: running=1; break;` | `RESET: rate=1; err=0; running=1; break;` |
| `SET_RATE: if (ok) rate=arg; else err++;` → `STOP: running=0; break;` | `SET_RATE: if/else {…} running=0; /* legacy */ break;` |
| `CLEAR: err=0;` (마지막 절, break 없음) | `CLEAR: err=0; break;` |
| (default 없음 → 모르는 명령 무시) | `default: break; /* 무시 (legacy 계약) */` |

2만 단계 무작위 명령열(정의 안 된 명령 `-1, 0, 6, 7`, 범위 밖 `arg` 포함)에서 `memcmp` 로 상태 전체를 비교한다. `default` 에서 에러 코드를 돌려주고 싶어도 그건 반환값 계약 변경이다. 덤: 레거시 `ls_fifo_peak(int n)` 의 `while (n--)` 는 음수 n 이면 2^31 번 돈 뒤 부호 있는 오버플로(UB)다 — 새 시그니처는 `uint32_t n` 이고 조건식은 본질적으로 Boolean(14.4)인 `i < n` 이다.

### Part C — 재귀를 배열로: 스택 상한을 숫자로

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">입력: 81 0F 81 0D 81 0B ... 01 01 07  (컨테이너 k개 중첩 + leaf 하나)</text>
  <text x="10" y="46" fill="currentColor">재귀: 깊이 = 입력이 정한다</text>
  <rect x="20" y="58" width="220" height="26" class="f-mut" stroke="currentColor"/>
  <text x="30" y="76" fill="currentColor" font-family="monospace" font-size="12">tlv_sum_rec  32 B (top)</text>
  <rect x="20" y="86" width="220" height="26" class="f-mut" stroke="currentColor"/>
  <text x="30" y="104" fill="currentColor" font-family="monospace" font-size="12">tlv_sum_rec  32 B (k=1)</text>
  <rect x="20" y="114" width="220" height="26" class="f-mut" stroke="currentColor"/>
  <text x="30" y="132" fill="currentColor" font-family="monospace" font-size="12">tlv_sum_rec  32 B (k=2)</text>
  <text x="120" y="156" fill="currentColor">⋮</text>
  <rect x="20" y="164" width="220" height="26" class="f-warn" stroke="currentColor"/>
  <text x="30" y="182" fill="currentColor" font-family="monospace" font-size="12">tlv_sum_rec  32 B (k=128)</text>
  <text x="20" y="212" fill="currentColor" font-size="12">len 은 1바이트 → 중첩 최대 128단</text>
  <text x="20" y="230" fill="currentColor" font-size="12">→ 129 × 32 B ≈ 4.1 KB (호출 체인 위에 더해짐)</text>
  <path d="M260 120 L330 120" stroke="currentColor"/>
  <path d="M322 114 L330 120 L322 126" stroke="currentColor" fill="none"/>
  <text x="350" y="46" fill="currentColor">반복: 프레임 하나 + 고정 배열</text>
  <rect x="350" y="58" width="370" height="132" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="362" y="78" fill="currentColor" font-family="monospace" font-size="12">tlv_sum_iter  72 B (항상)</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <rect x="362" y="92" width="36" height="26" class="f-acc" stroke="currentColor"/><text x="368" y="110">r0</text>
    <rect x="400" y="92" width="36" height="26" class="f-acc" stroke="currentColor"/><text x="406" y="110">r1</text>
    <rect x="438" y="92" width="36" height="26" class="f-acc" stroke="currentColor"/><text x="444" y="110">r2</text>
    <rect x="476" y="92" width="36" height="26" class="f-mut" stroke="currentColor"/><text x="482" y="110">r3</text>
    <rect x="514" y="92" width="36" height="26" class="f-mut" stroke="currentColor"/><text x="520" y="110">…</text>
    <rect x="552" y="92" width="36" height="26" class="f-mut" stroke="currentColor"/><text x="558" y="110">r8</text>
  </g>
  <text x="362" y="140" fill="currentColor" font-size="12">rem[k] = 깊이 k 에서 남은 바이트 (호출이 저장하던 것)</text>
  <text x="362" y="158" fill="currentColor" font-size="12">컨테이너: 부모 rem 에서 원소 전체를 빼고 depth++</text>
  <text x="362" y="176" fill="currentColor" font-size="12">rem[depth]==0: depth-- (= return). 9번째 중첩 → −2</text>
  <rect x="350" y="206" width="370" height="40" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="362" y="230" fill="currentColor" font-size="12">최악 스택이 입력과 무관 → 링크 후 정적 합산 가능</text>
  <text x="10" y="276" fill="currentColor" font-size="12">-fstack-usage 실측 (thumbv7em, Cortex-M4, -O2). 재귀 깊이별 합계는 프레임 × 호출 수로 계산한 값.</text>
</svg>
```

`demo_stack.c` 실측 (`.su` 파일, 둘 다 `static` = 고정 크기 프레임):

| 최적화 | `tlv_sum_rec` (재귀) | `tlv_sum_iter` (반복) |
|---|---|---|
| `-O0` | 40 B × 호출 수 | 76 B |
| `-O2` | 32 B × 호출 수 | 72 B |
| `-Os` | 24 B × 호출 수 | 56 B |

```text
$ clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -fstack-usage -c demo_stack.c -o /tmp/ds.o && cat /tmp/ds.su
demo_stack.c:15:tlv_sum_rec	32	static
demo_stack.c:42:tlv_sum_iter	72	static
```

얕은 입력에선 재귀가 오히려 작다. 문제는 **최악** 이다: `len` 필드가 1바이트라 컨테이너 사슬은 최대 128단(바깥 len = 2(k−1) ≤ 255) → 32 B × 129 ≈ 4.1 KB. STM32CubeMX 프로젝트의 기본 최소 스택이 0x400(1 KB)이고, 이 파서는 UART 명령 처리 깊은 곳에서 불린다. 악의적이거나 손상된 설정 프레임 하나로 스택이 `.bss` 를 덮는다 — 펌웨어에는 대개 가드 페이지가 없어서 HardFault 조차 안 나고 조용히 데이터가 깨진다. 반복 버전은 72 B 로 고정이고 `rem[]` 을 `uint16_t` 로 바꾸면 더 줄어든다. GCC 의 `-Wstack-usage=N` 이나 링크 후 콜 그래프 도구(puncover 등)는 재귀가 없을 때만 최악 스택을 합산할 수 있다. 이것이 17.2 의 실무적 이유다 (C 는 꼬리 호출 제거도 보장하지 않는다 — 레거시 `gcd` 처럼 꼬리 재귀여도 `-O0` 디버그 빌드에선 그대로 쌓인다).

**동등성 논증.** 재귀 버전이 호출 스택에 저장하는 상태는 "부모에서 남은 길이(`len`)와 위치(`p`)" 뿐이다. 반복 버전은 `pos` 하나(바이트를 앞에서부터 한 번씩만 지나가므로)와 `rem[depth]` 로 같은 정보를 들고, **같은 순서(깊이 우선, 전위)** 로 원소를 방문한다. 그래서 첫 에러 지점이 같고 반환값도 같다. 증거:

- 무작위 유효 트리 3000개 — 전부 같음. 이 중 깊이 ≥ 4 가 1918개, 정확히 8단이 36개 (생성기 계측값, 하네스가 `deep4 > 100 && deep8 > 0` 으로 확인).
- 잘라내기/1바이트 변조 5000개 — 5000개 전부 같음 (그중 −1 이 2878개라 에러 경로도 충분히 검사됨). `TLV_ETOODEEP` 는 0건 → 이 입력 분포에서 D6 이 동등성을 깎아 먹지 않았다.
- 깊이 8 사슬: 둘 다 7. 깊이 9: old 7, new −2 (D6).

clang-tidy 는 새 `new_tlv_sum` 에 `readability-function-cognitive-complexity`(29, 기준 25)를 냈다. 단일 출구(15.5)와 "루프 종료 break 하나"(15.4)를 지키느라 if 가 깊어진 대가다. 팀 규칙에 따라 원소 하나를 처리하는 `tlv_step()` 으로 쪼갤 수 있다 — 이것도 동등성 테스트가 있으니 안심하고 할 수 있는 리팩터다.

### 리팩터 후 재측정

| 도구 | old_* 구간 (legacy 복사본) | new_* 구간 |
|---|---|---|
| `-Weverything` | conversion 3, sign 1, fall-through 2, switch-default 1 | **0** |
| clang-tidy bugprone/misc | signed-bitwise 5, narrowing 5, no-recursion 1, switch 2, macro-parentheses 1, swappable 1 | swappable 1 (API 유지) |
| clang-tidy readability | identifier-length 11, braces 9, … | uppercase-literal-suffix 17, identifier-length 9, cognitive-complexity 1 |
| `--analyze` | DivideZero (73:16, old_avg) | 0 |
| `-fsanitize=implicit-conversion` | 55, 62, 73, 81 줄 | 0 |

`readability-uppercase-literal-suffix` 17개는 `0u` → `0U` 를 원한다. MISRA 7.3 이 금지하는 것은 소문자 **`l`** 접미사(숫자 1 과 헷갈림)뿐이고 `u` 는 괜찮다 — 팀 스타일로 정할 일이지 안전 규칙이 아니다. 이런 식으로 "도구의 의견" 과 "규칙" 을 구분하는 것이 분류의 핵심이다.

### 어떤 경고 세트를 켤까

`-Weverything` 을 CI 에 그대로 켜면 새 컴파일러 버전마다 새 경고가 생겨 빌드가 깨지고, 소음 때문에 결국 다들 무시한다. 대신 **목록을 명시** 한다:

```bash
-Wall -Wextra -Wconversion -Wsign-conversion -Wimplicit-fallthrough -Wswitch-default \
-Wmissing-prototypes -Wmissing-variable-declarations -Wshadow -Wcast-qual -Wstrict-prototypes \
-Wundef -Wformat=2 -Wvla
```

이 세트로 레거시를 빌드한 실측 (32개), new_* 구간은 0개. `-Weverything` 39개에서 빠진 7개는 소음 5개(`-Wpadded` 3, C++ 호환 2)와 `-Wunused-macros`·부호 불일치 `%x`(`-Weverything` 에선 `[-Wformat]` 로 표시, `-Wformat=2` 로는 안 나옴) 각 1개 — 뒤의 둘은 `-Wunused-macros -Wformat-signedness` 를 더하면 나온다 (실측):

```text
  18 [-Wmissing-prototypes]
   8 [-Wimplicit-int-conversion]
   2 [-Wsign-conversion]
   2 [-Wimplicit-fallthrough]
   1 [-Wswitch-default]
   1 [-Wmissing-variable-declarations]
```

`-Weverything` 은 컴파일러를 올릴 때 한 번씩 돌려 **새로 생긴 경고 종류** 를 검토하는 용도로 쓴다. clang-tidy 는 `.clang-tidy` 파일에 `bugprone-*,cert-*,misc-*,readability-braces-around-statements,...,-readability-identifier-length` 처럼 고정하고 `WarningsAsErrors` 는 핵심 체크에만 건다. MISRA 준수 자체는 이것으로 증명되지 않는다 — clang-tidy 에는 MISRA 모듈이 없고, 실제 인증 프로젝트는 PC-lint Plus, Polyspace, Helix QAC, LDRA, Parasoft, Coverity 같은 상용 검사기나 cppcheck 의 misra 애드온(규칙 문장 파일은 사용자가 구매한 문서에서 직접 넣어야 함)을 쓴다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution    # 32/32 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 32/32, 보고 없음
cc -std=c11 -Weverything -c solution.c -o /dev/null     # 경고 16개, 전부 old_* 구간과 하네스 (new_* 0개)
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 경고 위치마다 캐스트를 넣어 0개 만들기: `bus_write(REG_ODR_DIV, (uint8_t)div)` | 캐스트는 "의도한 잘림" 이라는 **주장** 이다. 400 → 144 버그가 경고 없이 굳는다 | 범위를 검사하고 실패를 돌려준다 (추가 연습 1) |
| `n - 1` 을 `n - 1u` 로 바꿔 10.4 경고만 없앰 | `n == 0` 에서 −1 이던 값이 4294967295 로 → 루프 40억 번 | `if (n > 0u)` 로 먼저 거르고 그 안에서만 `n - 1u` |
| 리팩터하며 `SET_RATE` 의 fall-through 를 "버그니까" 같이 고침 | 필드 호스트 툴이 기대는 동작이 바뀐다. 회귀가 나면 리팩터 탓인지 수정 탓인지 못 가린다 | 동작 보존 + 주석/티켓, 수정은 별도 커밋 |
| `MAX(read_reg(), x)` 를 그대로 두고 "tidy 가 조용하니 괜찮다" | `macro-repeated-side-effects` 는 함수 호출을 부작용으로 보지 않는다 (실측) | 읽기는 변수에 한 번, MAX 는 inline 함수 |
| 매크로에 바깥 괄호만 추가: `#define SCALE_MC(x) (x * 625 / 10)` | 인자 괄호가 없으면 `SCALE_MC(v + off)` 는 여전히 틀림 (20.7) | `((x) * 625 / 10)` — 아니면 함수 |
| 재귀를 "깊이 카운터 전역 변수" 로 제한만 함 | 여전히 재귀 (17.2), 스택 도구가 합산 못 함. 전역이라 재진입 불가 | 명시적 스택 배열 + 상한 |
| 동등성 테스트를 old 의 UB 입력까지 돌림 (`old_avg(s, 0)`) | UB 는 비교할 "동작" 이 없다. ASan/UBSan 이 터지거나 최적화에 따라 다름 | 도메인에서 빼고 new 의 계약만 검사 (D1, D3) |
| old 의 구현 정의 동작을 테스트 기준으로 쓰면서 가정을 안 적음 | 다른 컴파일러/타깃에서 old 자체가 다른 값을 내면 테스트가 엉뚱하게 실패 | `_Static_assert((-16 >> 4) == -1, ...)` 로 가정 고정 |
| `-Weverything -Werror` 를 CI 에 | 컴파일러 업그레이드마다 새 경고 → 빌드 깨짐, 소음 → 무시 습관 | 명시적 경고 목록 + 주기적 `-Weverything` 검토 |
| `if ((d = malloc(n)))` 를 "이중 괄호니까 의도" 로 통과 | `-Wparentheses` 만 끈 것. 13.4 위반이고 21.3 은 malloc 자체를 금지 | 정적 풀 (추가 연습 5) |

## 8. 면접 Q&A

<details>
<summary>Q1. 이 코드는 -Wall -Wextra -Werror 로 경고 0개입니다. 그런데 왜 정적 분석을 더 돌려야 하죠?</summary>

`-Wall -Wextra` 는 "거의 항상 버그인 것" 만 켠다. 암시적 축소 변환(`-Wconversion` 계열), clang 의 `-Wimplicit-fallthrough`, `-Wswitch-default`, 누락된 프로토타입은 꺼져 있다. 이 레거시에서 실제로 그 바깥에 진짜 버그 3개(`last_temp` 의 short 잘림 → 32.767 °C 이상에서 음수, ODR 분주기 400 → 144, 음수 평균 21842)가 있었다. 그리고 그 위의 분석기(`--analyze`)는 경고가 못 보는 경로 기반 버그(n=0 나눗셈)를, 런타임 검사(`-fsanitize=implicit-conversion`)는 실제로 일어난 잘림을 잡았다. 꼬리: "그럼 -Weverything 을 켜면?" → 탐색용이다. 39개 중 5개는 소음(`-Wpadded` 3, C++ 호환 2)이었고, 컴파일러를 올릴 때마다 새 경고가 생긴다. 명시적 목록을 쓴다.

</details>

<details>
<summary>Q2. "MISRA 준수" 란 정확히 무슨 뜻인가요? 위반이 0개여야 하나요?</summary>

아니다. MISRA C:2012 의 지침은 Mandatory / Required / Advisory 로 나뉘고, Required 는 **문서화된 deviation(이탈 승인)** 이 있으면 어길 수 있다 (Mandatory 는 불가). 준수 주장은 "어떤 도구로 어떤 지침을 검사했고, 어떤 deviation 이 누구의 승인으로 있으며, 프로젝트가 Advisory 를 어떻게 재분류했는지" 를 문서로 내는 것이다 — MISRA Compliance:2020 이 그 틀(지침 적용 계획, 재분류 계획, deviation 기록, 준수 요약)을 정한다. 예: C-M15 의 goto 정리 사다리는 15.1(Advisory) 이탈로 승인받아 쓰는 경우가 많다. 꼬리: "MISRA 준수면 안전한가?" → 아니다. 코딩 규칙은 언어 함정을 줄일 뿐이고, 이 레슨의 D4(FIFO 이중 읽기)처럼 **설계·하드웨어 의미** 의 버그는 테스트와 리뷰가 잡는다.

</details>

<details>
<summary>Q3. 경고를 없애려고 캐스트를 넣는 건 왜 위험한가요? 캐스트는 언제 쓰나요?</summary>

캐스트는 컴파일러에게 "이 변환은 의도했다" 고 말하는 것이라 경고가 사라지고, 런타임 검사(`-fsanitize=implicit-conversion`)도 명시적 캐스트는 보고하지 않는다. 즉 버그가 있던 자리에 붙이면 **버그를 숨기는 서명** 이 된다 (`(uint8_t)div` 는 400 → 144 를 영구화). 써야 할 때: ① 의도한 감쌈(체크섬 `sum = (uint8_t)(sum + b)`) ② 앞에서 범위를 **검사한 뒤** 의 축소(`if (div <= UINT8_MAX) reg = (uint8_t)div;`) ③ 값이 보존됨을 아는 부호 변환. 비슷한 함정이 `u` 접미사다: `n - 1` → `n - 1u` 는 경고를 없애면서 `n == 0` 의 결과를 −1 에서 4294967295 로 바꾼다. 그래서 경고 수정도 동등성 테스트 아래에서 한다.

</details>

<details>
<summary>Q4. 리팩터링 중에 명백한 버그를 발견했습니다. 같이 고치나요?</summary>

같은 커밋에서는 고치지 않는 게 원칙이다. 먼저 **특성화 테스트(characterization test)** 로 현재 동작을 고정하고, 리팩터 커밋은 그 테스트를 그대로 통과시킨다. 버그 수정은 별도 커밋에서 그 테스트의 기대값을 바꾸는 형태로 — 리뷰어는 "이 동작 변경이 맞나" 만 보면 된다. 예외는 **UB** 다 (n=0 나눗셈, `buf[-1]`): 보존할 동작이 없으니 새 계약을 정의하고 차이 목록에 올린다. 그리고 버그처럼 보여도 필드가 기대는 동작이 있다 — `SET_RATE` 가 장치를 멈추는 것은 호스트 툴이 "SET_RATE 후 START" 를 보내게 맞춰져 있을 수 있다. 고칠지는 제품 결정이다. D5(`temp_cal` 이 LSB 를 반환)처럼 같은 입력이 하나도 없으면 호출처부터 조사한다.

</details>

<details>
<summary>Q5. 재귀는 왜 금지인가요? 트리 순회처럼 재귀가 자연스러운 경우는요?</summary>

최악 스택 사용량을 **정적으로 상한 지을 수 없기 때문** 이다. 이 레슨의 실측: Cortex-M4 `-O2` 에서 재귀 TLV 파서의 프레임은 32 B, 깊이는 입력이 정해 최대 129 호출 ≈ 4.1 KB. 1 KB 스택에 가드 없는 MCU 에서는 조용한 메모리 오염이다. 반복 버전은 72 B 고정이라 콜 그래프 도구가 합산할 수 있다. C 는 꼬리 호출 제거를 보장하지 않으니 "꼬리 재귀라 괜찮다" 도 안 된다 (`-O0` 빌드). 트리는 명시적 스택 + 깊이 상한으로 바꾸고 상한을 넘으면 에러(D6). intrusive 리스트처럼 부모 포인터가 있으면 스택 없이도 순회할 수 있다. 꼬리: "깊이 8 은 어떻게 정했나?" → 프로토콜 명세(호스트 툴이 실제로 만드는 최대 중첩)에서 정하고, 상한 초과를 별도 에러 코드로 돌려 현장에서 구분되게 한다.

</details>

<details>
<summary>Q6. MAX 같은 매크로는 왜 위험하고, 대안은 뭔가요? 도구가 다 잡아 주지 않나요?</summary>

함수형 매크로는 인자를 **텍스트로 복사** 하므로 부작용 있는 인자가 두 번 평가되고(조건 연산자는 고른 쪽을 다시 평가, 6.5.15p4), 괄호가 없으면 우선순위가 바뀐다(`SCALE_MC(v + off)`). 도구는 일부만 잡는다: clang-tidy `bugprone-macro-repeated-side-effects` 는 `MAX(p[i++], x)` 는 잡았지만 `MAX(bus_read(), x)` 는 못 잡았다 (함수 호출의 부작용을 모름 — 실측). `bugprone-macro-parentheses` 는 `SCALE_MC` 의 치환 목록만 지적하고 `SET_BIT(r, b)` 는 조용했다. 대안: `static inline uint8_t max_u8(uint8_t a, uint8_t b)` — 타입 검사, 한 번 평가, 디버거에서 보임, `-O2` 에서 매크로와 같은 코드. 타입 일반화가 필요하면 `_Generic` (C-M11). 매크로가 꼭 필요한 곳(`__FILE__`/`__LINE__` 를 쓰는 로깅, X-macro C-A07, 컴파일 타임 상수)만 남긴다.

</details>

<details>
<summary>Q7. old 와 new 가 "같다" 는 걸 얼마나 확신할 수 있나요? 테스트 몇 개로?</summary>

입력 공간이 작으면 **전수**: 16비트 원시값 65536개를 전부 비교했다 (temp, temp_cal). 크면 **차등 테스트**: 같은 PRNG 입력(xorshift32, 고정 시드 → 재현 가능)을 둘에 넣고 비교 — avg 5000, frame 5000, cmd 2만 단계, TLV 8000. 중요한 건 개수보다 **분포가 경계를 덮는지** 다: 그래서 하네스가 "깊이 ≥ 4 트리 100개 이상, 8단 트리 존재", "−1 에러 경로 1000개 이상" 을 같이 확인한다 (분포 자체를 검사하지 않으면 생성기가 쉬운 입력만 만들어도 통과한다). 더 가려면 커버리지(`-fprofile-instr-generate`)로 new 의 모든 분기가 실행됐는지 보고, 퍼저(libFuzzer)로 `old != new` 를 찾게 한다. 그리고 old 가 구현 정의 동작에 기대면 그 가정을 `_Static_assert` 로 적어야 타깃 컴파일러에서도 같은 기준이 된다.

</details>

<details>
<summary>Q8. 의도적인 switch fall-through 는 어떻게 표시하나요? MISRA 에서는요?</summary>

컴파일러에게는 `__attribute__((fallthrough));` (GCC 7+/clang), C23 부터는 표준 `[[fallthrough]];` 를 다음 case 레이블 바로 앞에 둔다 — `demo_tool_gaps.c` 에서 표시한 절은 `-Wimplicit-fallthrough` 가 조용했고 표시 안 한 절만 경고가 났다. `/* fall through */` 주석은 GCC 가 `-Wimplicit-fallthrough=3` 에서 인식하지만 clang 은 주석을 보지 않는다. MISRA 16.3 은 표시 여부와 상관없이 모든 절이 break 로 끝나기를 요구하므로, 공통 동작은 함수로 빼서 두 절에서 호출하거나 효과를 복사한다 (Part B). 꼬리: "Duff's device 는?" → 16.x 여러 개를 동시에 어긴다. 펌웨어에서 쓸 이유가 없다 (`memcpy` 가 더 빠르다, C-M08).

</details>

## 9. 추가 연습

`legacy_driver.c` 의 나머지 발견 사항을 같은 방식(동등성 + 의도된 차이)으로 고친다. 답은 모두 `practice.c` 에 있다.

### 연습 1 — 8진수 ODR 테이블과 8비트 분주기

레거시 `k_odr_hz[] = { 001, 002, 005, 010, 020, 050, 100, 200 }`. ① 8진수 없이(7.1) **같은 값** 의 테이블을 써라. ② `ls_set_odr` 는 `3200 / hz` 를 8비트 레지스터에 쓴다. 들어가지 않으면 거부하는 `odr_div_checked(idx, &div)` 를 쓰고, 레거시가 실제로 쓰던 레지스터 값을 테스트로 고정하라.

<details>
<summary>답 보기</summary>

테이블은 `{ 1, 2, 5, 8, 16, 40, 100, 200 }` — "10, 20, 50 Hz" 가 아니다. 자리 맞추기용 0 이 8진수를 만들었다 (6.4.4.1). 리팩터는 **지금 값** 을 십진수로 옮기고(동작 보존), 원래 의도(10/20/50)로 바꾸는 건 별도 결정이다. 분주기: 3200, 1600, 640, 400 은 8비트에 안 들어가 레거시 레지스터에는 128, 64, 128, 144 가 들어갔다 — 1 Hz 를 요청하면 실제로는 3200/128 = **25 Hz** 로 돈다. 위 selftest 출력의 `reg 03 = 90` 이 이것이다. 새 함수는 `div <= UINT8_MAX` 와 나누어떨어짐을 확인하고 아니면 −1. 들어가는 구간(idx 4..7)은 레거시와 같은 레지스터 값을 테스트로 확인한다.

</details>

### 연습 2 — "u 붙이기" 의 함정

`payload_len_mechanical(uint8_t n) { return n - 1u; }` 와 올바른 `payload_len(n)` 을 비교하라. 두 함수가 같은 입력은? 다른 입력은?

<details>
<summary>답 보기</summary>

`n` 은 int 로 승격되고 `1u` 와 만나 일반 산술 변환으로 unsigned 가 된다 → `n == 0` 이면 `0u - 1u = UINT_MAX` (32비트 unsigned 에서 4294967295). `n >= 1` 에서는 255개 전부 같다 — 그래서 리뷰와 일반 테스트에서 놓친다. 답: `(n > 0u) ? (uint8_t)(n - 1u) : 0u`. 교훈: 경고를 고칠 때도 경계값(0, 최대값) 테스트가 같이 가야 한다.

</details>

### 연습 3 — 래치형 16비트 레지스터의 읽기 순서

`TEMP_H` 를 읽는 순간 하위 바이트가 래치되고, `TEMP_L` 은 래치를 돌려주는 모의 버스가 있다. 레거시 `bus_read(H) << 8 | bus_read(L)` 대신 순서가 보장된 `read_temp_raw()` 를 써라. 순서가 뒤집히면 어떤 값이 나오나?

<details>
<summary>답 보기</summary>

```c
const uint8_t hi = bus_read(REG_TEMP_H);   /* ; 가 순서를 고정 */
const uint8_t lo = bus_read(REG_TEMP_L);
return (uint16_t)(((uint16_t)hi << 8) | lo);
```

한 식 안의 두 함수 호출은 어느 쪽이 먼저인지 미지정이다 (6.5p3, 6.5.2.2p10 — UB 는 아니지만 MISRA 13.2 의 "모든 허용 순서에서 같은 값" 을 어긴다). 이 맥의 clang 은 왼쪽부터 했지만 보장이 아니다. L 을 먼저 읽으면 이전 래치(0xAA) + 나중 H(0x14) → `0x14AA`, 존재한 적 없는 값이다. 실제 예: STM32 RTC 는 `RTC_TR`(시간)을 읽으면 `RTC_DR`(날짜)을 읽을 때까지 잠근다. AVR 의 16비트 타이머 `TCNT1` 은 반대로 **하위 바이트를 먼저** 읽어야 상위가 TEMP 레지스터에 래치된다. 순서 규칙은 데이터시트가 정하고, C 코드는 문장을 나눠 그 순서를 지킨다.

</details>

### 연습 4 — 재귀 gcd → 반복

레거시 `gcd(a, b) { return b ? gcd(b, a % b) : a; }` 를 반복으로 바꾸고 `a ∈ 1..400, b ∈ 0..400` 전수로 동등성을 보여라.

<details>
<summary>답 보기</summary>

`while (b != 0u) { r = a % b; a = b; b = r; }` → `return a`. 꼬리 재귀라 `-O2` 컴파일러는 대개 루프로 바꾸지만 C 표준은 그걸 보장하지 않고, `-O0` 디버그 빌드나 다른 컴파일러에서는 스택이 쌓인다. 17.2 는 최적화 결과가 아니라 **소스** 에 대한 규칙이다. 16만 쌍 전수 비교로 확인. 덤: 이 드라이버의 ODR 값은 전부 3200 의 약수라 gcd 는 처음부터 필요 없었다 — 정적 분석은 종종 "이 코드가 왜 있지?" 로 이어진다.

</details>

### 연습 5 — malloc/strcpy → 정적 풀

레거시 `ls_open` 은 `malloc` + `strcpy(d->name, name)` (name[8])이다. 장치 4개짜리 정적 풀로 `pool_open(name, addr)` / `pool_close(d)` 를 써라. 이름은 잘라서 복사, 풀이 다 차면 NULL, 이중 close 와 풀 밖 포인터는 −1.

<details>
<summary>답 보기</summary>

MISRA 21.3 은 동적 할당을 금지한다: 단편화, 실패 시점 예측 불가, 최악 실행 시간. 정적 풀은 최대 개수가 링크 맵에 보이고 실패가 "풀 소진" 하나로 결정적이다. `strcpy` 는 "accelerometer"(13자)에서 `name[8]` 을 넘쳐 옆 필드를 덮는다 (CERT STR31-C). 답은 `LS_NAME_LEN - 1` 까지 복사하고 항상 `'\0'` 종료. close 는 포인터가 풀 배열 안의 슬롯 경계인지(`uintptr_t` 로 범위와 나머지 확인)와 `used` 플래그로 이중 해제를 잡는다. 반환 계약(NULL = 실패)은 `malloc` 실패와 같아서 호출처를 안 바꿔도 된다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 22/22 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san   # 22/22, 보고 없음
```

## 10. 핵심 정리

- `-Wall -Wextra` 클린 ≠ 안전. 이 레거시에서 **진짜 버그는 그 바깥** (`-Wconversion` 계열, `--analyze`, `-fsanitize=implicit-conversion`)에 있었다. 도구마다 잡는 게 다르니 겹쳐 쓴다.
- 분류가 일이다: 진짜 버그 → 의도된 차이 목록(D1..D6) + old/new 값 둘 다 테스트, 잠재 위험 → 동작 보존 수정, 소음(`-Wpadded`, 이름 길이, C++ 호환) → 설정에서 끈다. CI 에는 `-Weverything` 대신 명시적 경고 목록.
- 도구가 못 잡는 것: 함수 호출이 든 매크로 인자의 이중 평가, 8진수, 뒤로 가는 goto, 한 식 안의 두 레지스터 읽기 순서 → 리뷰 체크리스트.
- 캐스트와 `u` 접미사는 경고를 없애지만 **동작을 바꾸거나 버그를 굳힌다** (`n - 1u` → UINT_MAX, `(uint8_t)400` → 144). 범위 검사 뒤에만 축소 캐스트.
- 리팩터 = 동등성 도메인에서 old == new (전수 또는 고정 시드 PRNG, **분포도 검사**). UB 입력은 도메인 밖. 버그 수정은 별도 커밋. 필드가 기대는 이상한 동작은 보존하고 표시.
- 재귀 금지(17.2)의 이유는 숫자다: M4 `-O2` 32 B × 입력이 정한 깊이(최대 129) vs 반복 72 B 고정. 명시적 스택 + 깊이 상한 + 별도 에러 코드.
- MISRA 준수 = 위반 0 이 아니라 도구·deviation·재분류를 문서화한 상태. 규칙 번호는 확신하는 것만, 규칙 문장은 인용하지 않는다.

## 참고자료

- C11 (N1570): 6.3.1.1 정수 승격, 6.3.1.3 정수 변환(p3 구현 정의), 6.3.1.8 일반 산술 변환, 6.4.4.1 정수 상수(8진수), 6.5p2–3 순서·unsequenced, 6.5.2.2p10 함수 호출 순서, 6.5.5p6 나눗셈 버림, 6.5.7p4–5 시프트, 6.5.15p4 조건 연산자 — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- MISRA C:2012 (3판, Amendment 포함) 와 MISRA Compliance:2020 — <https://misra.org.uk/> (유료. 이 레슨은 규칙 번호와 취지만 인용)
- Clang Diagnostics Reference (`-Weverything`, `-Wimplicit-fallthrough`, `-Wconversion`) — <https://clang.llvm.org/docs/DiagnosticsReference.html>, Clang User's Manual (Diagnostics) — <https://clang.llvm.org/docs/UsersManual.html>
- clang-tidy checks: bugprone-macro-repeated-side-effects, bugprone-macro-parentheses, misc-no-recursion, bugprone-narrowing-conversions — <https://clang.llvm.org/extra/clang-tidy/checks/list.html>
- UBSan `-fsanitize=implicit-conversion` — <https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html>
- GCC `-Wimplicit-fallthrough`, `-fstack-usage`, `-Wstack-usage` — <https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html>, <https://gcc.gnu.org/onlinedocs/gcc/Developer-Options.html>
- SEI CERT C: PRE31-C (매크로 인자 부작용), STR31-C (문자열 버퍼), INT31-C (정수 변환), EXP30-C (평가 순서) — <https://wiki.sei.cmu.edu/confluence/display/c>
- cppcheck MISRA addon — <https://cppcheck.sourceforge.io/misra.php>
- Michael Feathers, *Working Effectively with Legacy Code* — 특성화 테스트(characterization test)
- 관련 레슨: C-M12 (UB 카탈로그, 도구별 검출표), C-M15 (goto 정리와 MISRA 15.x), C-M11 (매크로 vs inline/_Generic), C-M02 (read-to-clear·W1C 레지스터), C-M14 (찢어진 읽기), C-A07 (X-macro), C-A08 (`_Static_assert` 계약), C-A11 (코드 리뷰 면접)
