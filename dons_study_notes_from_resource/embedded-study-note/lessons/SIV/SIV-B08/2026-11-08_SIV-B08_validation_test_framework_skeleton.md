---
id: SIV-B08
time: 75분
updated: 2026-11-08
---
# SIV-B08 검증 테스트 프레임워크 골격

## 학습 목표

- `TEST(SUITE, name) { ... }` 한 줄로 테스트가 **자동 등록**되는 원리(constructor / linker section)를 설명하고 직접 매크로로 만든다.
- 비치명(non-fatal) 체크 `CHECK_EQ` / `CHECK_MASK` 가 무엇을 기록해야 디버그 엔지니어가 바로 움직일 수 있는지(레지스터·주소·기대값·실제값·마스크·소스 줄) 안다.
- gtest 스타일 이름 필터(`REG.*:-*.slow`), 이름순 정렬로 **링크 순서와 무관한 결정적 실행**, 중복 이름 검출을 구현한다.
- 한 줄 = 한 JSON 객체(JSON Lines) 결과 로그와 종료 코드 0/1/2 규칙을 만들고, 그 로그가 SIV-B14 리포트·CI 게이트의 입력이 된다는 흐름을 잡는다.
- SIV-B03~B07 테스트를 프레임워크에 이식하고, mock에 결함 5종을 주입해 **결함 × 테스트 검출 매트릭스**를 실제로 뽑는다.

## 1. 문제

first silicon이 랩에 도착한 날, 검증 엔지니어는 "레지스터 테스트 300개 중 `REG.*` 만 이 보드에서 돌리고, 실패는 주소·비트까지 로그로 남겨라"를 몇 분 안에 해내야 한다 — Rivian·NVIDIA 실리콘 검증 면접의 "작은 validation framework를 설계해 보라" 질문이 정확히 이것이다.

지금까지 SIV-B03~B07에서 만든 테스트(리셋값, R/W 패턴, walking 1/0, 주소 alias, March C-)는 각자 `main()` 을 갖고 있었다. 테스트가 수백 개가 되면 이렇게는 못 산다. 필요한 것은 네 가지다.

1. **등록(registration)**: 테스트를 쓰기만 하면 목록에 들어간다. 손으로 배열에 추가하다 빼먹는 일이 없어야 한다.
2. **선택 실행(filter)**: 이름 패턴으로 골라 돌린다. 보드·SKU마다 돌릴 집합이 다르다.
3. **집계(aggregation)**: PASS / FAIL / SKIP 개수, 소요 시간, 종료 코드.
4. **기계가 읽는 로그**: 실패한 레지스터·주소·기대값·실제값이 한 줄 JSON으로 남아야 리포트(SIV-B14)와 CI가 쓴다.

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b08-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">TEST() 한 줄이 JSON 로그 한 줄이 되기까지</text>
  <rect x="10" y="40" width="235" height="120" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="22" y="62" fill="currentColor" font-weight="bold">① 컴파일 시: TEST 매크로</text>
  <text x="22" y="86" fill="currentColor" font-size="12">본문 함수 siv_REG__reset_values</text>
  <text x="22" y="108" fill="currentColor" font-size="12">const test_case {이름, fn, 줄}</text>
  <text x="22" y="130" fill="currentColor">constructor 함수 _reg()</text>
  <text x="22" y="150" fill="currentColor" opacity=".7" font-size="12">(또는 섹션에 포인터 하나)</text>
  <rect x="265" y="40" width="225" height="120" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="277" y="62" fill="currentColor" font-weight="bold">② main 전: 등록</text>
  <text x="277" y="86" fill="currentColor" font-size="12">test_register(&amp;g_registry, &amp;tc)</text>
  <text x="277" y="108" fill="currentColor">tests[n++] = &amp;tc</text>
  <text x="277" y="130" fill="currentColor">꽉 차면 overflow++ (숨기지 않음)</text>
  <rect x="510" y="40" width="240" height="120" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="522" y="62" fill="currentColor" font-weight="bold">③ run_tests 준비</text>
  <text x="522" y="86" fill="currentColor">이름순 정렬, 중복 → exit 2</text>
  <text x="522" y="108" fill="currentColor">filter: REG.*:-REG.walk*</text>
  <text x="522" y="130" fill="currentColor">{"type":"run", matched: N} 헤더</text>
  <rect x="510" y="190" width="240" height="120" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="522" y="212" fill="currentColor" font-weight="bold">④ run_one × N</text>
  <text x="522" y="236" fill="currentColor">before_each() = IP 리셋 (격리)</text>
  <text x="522" y="258" fill="currentColor">t0 = now_us(); fn(t); dur</text>
  <text x="522" y="280" fill="currentColor">체크 0개면 PASS 아님 → FAIL</text>
  <rect x="265" y="190" width="225" height="120" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="277" y="212" fill="currentColor" font-weight="bold">⑤ emit_result_json</text>
  <text x="277" y="236" fill="currentColor">test / status / duration_us</text>
  <text x="277" y="258" fill="currentColor">첫 실패: reg addr expected</text>
  <text x="277" y="280" fill="currentColor">actual mask file line reason</text>
  <rect x="10" y="190" width="235" height="120" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="22" y="212" fill="currentColor" font-weight="bold">⑥ 소비자</text>
  <text x="22" y="236" fill="currentColor">summary 줄 + exit 0 / 1 / 2</text>
  <text x="22" y="258" fill="currentColor">SIV-B14 리포트, CI 게이트</text>
  <text x="22" y="280" fill="currentColor">재실행: --run 'REG.reset_*'</text>
  <g stroke="currentColor" fill="none" marker-end="url(#b08-arr)">
    <path d="M245 100 L263 100"/><path d="M490 100 L508 100"/><path d="M630 160 L630 188"/>
    <path d="M510 250 L492 250"/><path d="M265 250 L247 250"/>
  </g>
</svg>
```

검증 대상은 가상의 타이머 IP **TMR0** (base `0x4001_0000`) 와 64워드 SRAM (`0x2000_0000`) 이다. SIV-B02의 mock을 축약해 넣었다 — 레지스터마다 비트별 access 마스크(rw / w1c / ro)와 volatile 마스크를 둔다.

| 레지스터 | off | reset | 비트별 속성 | 비고 |
|---|---|---|---|---|
| ID | 0x000 | 0x5431_0001 | 전부 RO | 블록 식별 |
| CTRL | 0x004 | 0 | [7:0] RW, [31:8] 예약 | 예약 비트는 0으로 읽혀야 함 |
| LOAD | 0x008 | 0x0000_FFFF | 전부 RW | |
| COMPARE | 0x00C | 0 | 전부 RW | |
| STATUS | 0x010 | 0 | [3:0] W1C, [8] RO·volatile | RUNNING 비트는 시간 따라 변함 |
| COUNT | 0x014 | 0 | 전부 RO·volatile | 자유 카운터 → 값 비교 불가 |
| SCRATCH | 0x01C | 0 | 전부 RW | |

### Part A — 등록 · 체크 · 한 테스트 실행

- `test_register(r, tc)` — 레지스트리 끝에 붙인다. `MAX_TESTS`(128) 를 넘으면 `overflow` 를 세고 -1.
- `TEST(suite, name)` 매크로(주어짐)가 만드는 constructor가 main 전에 `test_register(&g_registry, ...)` 를 부른다.
- `check_mask(t, what, addr, mask, exp, act, file, line)` — `(exp ^ act) & mask == 0` 이면 true. 아니면 실패 정보를 **최대 4개까지** 기록하고(개수는 계속 셈) 상태를 FAIL로, `verbose` 면 사람이 읽는 한 줄을 stderr로. `CHECK_EQ` 는 mask가 전부 1인 경우다.
- `run_one(cfg, tc, t)` — `t` 를 초기화, `before_each()` (IP 리셋) → `now_us()` 로 시간을 재며 `tc->fn(t)` → 소요 시간 반환. **체크를 하나도 안 했는데 PASS면 FAIL**로 바꾸고 reason에 `no checks` 를 남긴다.

### Part B — 이름 필터 · 결정적 순서 · JSON 이스케이프

- `glob_match(p, plen, s)` — `*` (임의 길이), `?` (한 글자). 패턴은 NUL로 안 끝날 수 있다(필터 문자열의 일부).
- `filter_match(filter, name)` — `':'` 로 나눈 glob 목록. `-` 로 시작하는 토큰은 제외. 포함 토큰이 없으면 전부 포함. NULL/빈 문자열 = 전부.
- `registry_finalize(r, &dup)` — overflow면 -2, 이름순 `qsort`, 같은 이름이 있으면 `*dup` 에 담고 -1.
- `json_escape(dst, cap, src)` — `"` `\` → `\"` `\\`, `\n` `\t`, 나머지 제어문자 → `\u00XX`. 공간이 모자라면 **이스케이프를 반으로 자르지 않고** 멈춘다. 항상 NUL 종료.

### Part C — JSON 로그 러너, B03~B07 이식, 결함 주입

- `emit_result_json(lb, t, dur)` — 한 줄 JSON. 실패가 있으면 첫 실패의 `reg/addr/expected/actual/mask/file/line`, reason이 있으면 `reason`.
- `run_tests(cfg, r, s)` — finalize → `{"type":"run",...}` 헤더 → 필터에 맞는 테스트를 이름순 실행·기록 → `{"type":"summary",...}`. 종료 코드 **0 = 실패 없음(SKIP 허용), 1 = FAIL 있음, 2 = 설정 오류(중복/overflow/매칭 0개)**.
- `TEST(REG, addr_alias)` 본문 (SIV-B06 이식): 모든 RW 레지스터에 `addr_pattern(off)` 를 **먼저 다 쓰고 나서** 다 읽는다. 틀린 값이 다른 레지스터의 패턴이면 `TEST_NOTE` 로 원인 주소 비트를 추정해 남긴다.
- 나머지 이식 테스트(리셋값·R/W·walking·March C-·SKIP 예시)는 주어진다. 결함 5종(`reset` `stuck` `w1c` `alias` `cf`)을 주입해 리포트와 검출 매트릭스를 확인한다.

## 2. Problem (English)

> **Design a minimal post-silicon validation test framework in C11.**
>
> 1. Provide a `TEST(suite, name) { body }` macro that auto-registers the test before `main()` runs, without a hand-maintained table. Registration must fail loudly (not silently) when the registry is full.
> 2. Provide non-fatal checks `CHECK_EQ(what, addr, exp, act)` and `CHECK_MASK(what, addr, mask, exp, act)` that count checks, record up to 4 failures per test with register name, address, expected, actual, mask, file and line, and mark the test FAILED. A test that executes zero checks must not be reported as PASS.
> 3. `filter_match(filter, name)`: gtest-like filter, `':'`-separated globs (`*`, `?`), tokens starting with `-` exclude. Tests run in name order regardless of link order; duplicate names are a configuration error.
> 4. Emit one JSON object per line: a `run` header, one `test` record per executed test (`test, status, duration_us, checks, fails` + first failure + optional `reason`), and a `summary`. Return exit code 0 (no failures), 1 (failures) or 2 (configuration error, including "filter matched nothing").
> 5. Port reset-value, read/write, walking-1/0, address-alias and March C- tests onto it; inject faults into a mock register file / SRAM and show each is reported precisely.
>
> Example record: `{"type":"test","test":"REG.reset_values","status":"FAIL","duration_us":14,"checks":6,"fails":1,"reg":"LOAD","addr":"0x40010008","expected":"0x0000FFFF","actual":"0x0000FEFF","mask":"0xFFFFFFFF","file":"solution.c","line":412}`

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 문제 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행하면 이렇게 나온다 (실제 출력, 가운데 생략):

```text
FAIL starter.c:435  g_registry.n == 6
FAIL starter.c:440  lr.n == MAX_TESTS && lr.overflow == 2 && rc == -1
FAIL starter.c:445  t.nchecks == 1 && t.nfails == 0 && t.status == ST_PASS
...
alias            .     .    .     .      .
cf               .     .    .     .      .
4/49 passed
```

`TEST` 매크로, mock, 이식 테스트 4개, 채점 하네스는 이미 있다. TODO는 Part A 3개, Part B 4개, Part C 2개 함수 + `TEST(REG, addr_alias)` 본문이다. 하네스의 `CHECK(expr)` 는 **이 레슨 채점용**이고, 프레임워크의 `CHECK_EQ` / `CHECK_MASK` 와는 다른 것이다.

## 4. 힌트

<details>
<summary>힌트 1 — TEST 매크로가 펼쳐진 모습부터 본다</summary>

`TEST(REG, reset_values) {` 는 ① 본문 함수 선언 ② `static const test_case siv_REG__reset_values_tc = { "REG.reset_values", ... }` ③ `__attribute__((constructor))` 함수 ④ 본문 함수 정의의 머리로 바뀐다. 매크로 마지막이 함수 머리라서 뒤의 `{ ... }` 가 그대로 본문이 된다. `test_register` 는 배열 끝에 포인터 하나 붙이는 게 전부다.

</details>

<details>
<summary>힌트 2 — check_mask: 비교는 XOR 한 번</summary>

`diff = (exp ^ act) & mask`. diff가 0이 아니면 실패. 기록은 `nfails < MAX_FAIL_REC` 일 때만 하고 `nfails++` 는 항상 한다 — walking 테스트에서 실패가 32개 나와도 버퍼는 넘치지 않고 개수는 정확해야 한다.

</details>

<details>
<summary>힌트 3 — glob은 "마지막 * 위치"만 기억하면 된다</summary>

`*` 를 만나면 그 위치(star)와 그때의 문자열 위치(mark)를 기억하고 `*` 가 빈 문자열을 먹었다고 가정해 진행한다. 나중에 막히면 `*` 가 한 글자 더 먹었다고 보고 `pi = star+1, si = ++mark` 로 되돌아간다. 재귀 없이 O(|p|·|s|).

</details>

<details>
<summary>힌트 4 — 필터는 문자열을 복사하지 말고 (시작, 길이)로</summary>

`strchr(p, ':')` 로 토큰 끝을 찾고 `glob_match(p, len, name)` 에 길이를 넘긴다. 그래서 glob_match가 `plen` 을 받는 것이다.

</details>

<details>
<summary>힌트 5 — addr_alias: 쓰기와 읽기를 섞으면 alias를 못 본다</summary>

write→read를 레지스터마다 바로 하면 SCRATCH 쓰기가 COMPARE 셀에 들어가도 곧바로 같은 셀에서 읽으니 통과한다(walking 테스트가 alias를 못 잡는 이유). **전부 쓰고 → 전부 읽어야** 나중 쓰기가 앞 레지스터를 덮은 흔적이 보인다. 틀린 값이 다른 레지스터 o의 패턴과 같으면 `d->off ^ o->off` 가 무시된 주소 비트다.

</details>

## 5. 풀이

### Part A — 등록: constructor vs linker section

`TEST(REG, reset_values)` 가 실제로 만드는 코드는 이렇다 (매크로 전개를 손으로 정리):

```c
static void siv_REG__reset_values(test_ctx *t);
static const test_case siv_REG__reset_values_tc =
    { "REG.reset_values", siv_REG__reset_values, "solution.c", 408 };
__attribute__((constructor)) static void siv_REG__reset_values_reg(void) {
    test_register(&g_registry, &siv_REG__reset_values_tc);
}
static void siv_REG__reset_values(__attribute__((unused)) test_ctx *t) { /* 사용자가 쓴 본문 */ }
```

`#suite "." #name` 은 문자열화(`#`) + 인접 문자열 연결로 `"REG.reset_values"` 를 만든다. 본문 함수의 매개변수 이름이 `t` 로 고정되어 있어서 `CHECK_EQ` 매크로가 `t` 를 암묵적으로 쓴다 (gtest의 `this` 와 같은 역할).

자동 등록 방식은 두 가지다.

| | constructor (이 레슨) | linker section (demo_section.c) |
|---|---|---|
| 원리 | 함수 포인터를 `.init_array`(ELF) / `__mod_init_func`(Mach-O)에 넣어 main 전에 실행 | 테스트 포인터를 전용 섹션에 넣고, 링커가 여러 .o의 같은 섹션을 이어 붙임 |
| 목록 얻기 | 등록 함수가 전역 배열에 추가 | ELF: `__start_siv_tests` ~ `__stop_siv_tests` / Mach-O: `getsectiondata()` 또는 `section$start$__DATA$__siv_tests` |
| main 전 코드 | 필요 (startup이 init_array를 돌려야 함) | 불필요 — 데이터만 있음 |
| 순서 | TU 간 순서는 링크 순서에 의존, 표준 보장 없음 | 섹션 안 순서도 링크 순서 |
| 함정 | 베어메탈 startup이 `__libc_init_array()` 를 안 부르면 0개 등록 | 커스텀 링커 스크립트에 `KEEP(*(siv_tests))` 가 없으면 `--gc-sections` 가 지울 수 있음 |
| 쓰는 곳 | GoogleTest(정적 객체 초기화로 등록), 많은 C 테스트 러너 | Linux initcall·KUnit, Zephyr iterable section, Criterion |

**이 맥에서 직접 확인한 것 1 — `-O2` 에서는 constructor가 사라진다.** `cc -O2 -S solution.c` 의 어셈블리를 보면 `__mod_init_func` 섹션이 없고, `g_registry` 가 이미 채워진 **초기화 데이터**로 나온다. LLVM의 GlobalOpt가 "포인터를 배열에 넣기만 하는 constructor"를 컴파일 시간에 평가해 버린 것이다. 같은 TU 안에서는 정의 순서 그대로다.

```text
$ cc -std=c11 -O2 -S -o - solution.c | sed -n '6242,6252p'
	.section	__DATA,__data
	.p2align	3, 0x0                          ; @g_registry
_g_registry:
	.quad	_siv_REG__reset_values_tc
	.quad	_siv_REG__rw_patterns_tc
	.quad	_siv_REG__walking_bits_tc
	.quad	_siv_REG__addr_alias_tc
	.quad	_siv_MEM__march_c_minus_tc
	.quad	_siv_DMA__reset_values_tc
	.quad	0
	.quad	0
```

`-O0` 에서는 constructor가 남는다. 어셈블리는 `__DATA,__mod_init_func` 에 함수 포인터를 넣고, 링커(ld)가 최종 바이너리에서 `__TEXT,__init_offsets` (32비트 오프셋 표)로 바꿨다:

```text
$ cc -std=c11 -O0 -S -o - solution.c | sed -n '/__mod_init_func/,+8p'
	.section	__DATA,__mod_init_func,mod_init_funcs
	.p2align	3, 0x0
	.quad	_siv_REG__reset_values_reg
	.quad	_siv_REG__rw_patterns_reg
	.quad	_siv_REG__walking_bits_reg
	.quad	_siv_REG__addr_alias_reg
	.quad	_siv_MEM__march_c_minus_reg
	.quad	_siv_DMA__reset_values_reg
.subsections_via_symbols
$ cc -std=c11 -O0 solution.c -o /tmp/SIV-B08_o0 && xcrun otool -s __TEXT __init_offsets /tmp/SIV-B08_o0
Contents of (__TEXT,__init_offsets) section
0000000100005298	00000638 000006cc 000006f0 00000714
00000001000052a8	00000738 0000075c
```

어느 쪽이든 결과(등록 6개)는 같다. 하지만 "어떤 순서로 도느냐"는 컴파일러·링커가 정하는 것이지 C 표준이 정한 게 아니다 → Part B에서 **이름순 정렬**로 순서를 고정한다.

**이 맥에서 직접 확인한 것 2 — linker section 방식 (demo_section.c).** `section("__DATA,__siv_tests")` 에 `const test_case *` 를 하나씩 넣고 두 가지 방법으로 훑었다.

```text
$ cc -std=c11 -Wall -Wextra -Werror -O2 demo_section.c -o /tmp/SIV-B08_demo && /tmp/SIV-B08_demo; echo "exit=$?"
getsectiondata: 24 bytes -> 3 tests
  reg_reset_values   PASS
  reg_rw_patterns    PASS
  mem_march_c        FAIL
section$start/end: 3 tests (same array: yes)
exit=1
$ xcrun otool -s __DATA __siv_tests /tmp/SIV-B08_demo
/tmp/SIV-B08_demo:
Contents of (__DATA,__siv_tests) section
0000000100008000	00004010 00100000 00004020 00100000
0000000100008010	00004030 00000000
```

24바이트 = 포인터 3개. 디스크에 있는 값은 진짜 주소가 아니라 dyld가 로드할 때 고치는 **chained fixup** 형식이다(PIE라서 주소가 실행마다 바뀜). `-Wl,-dead_strip` 을 줘도 첫 줄이 똑같이 `24 bytes -> 3 tests` 였다 — `cc -S demo_section.c` 를 보면 clang이 `used` 변수마다 `.no_dead_strip _reg_reset_values_ptr` 같은 지시어를 붙인다. ELF 쪽은 이 맥에 ELF 링커가 없어 오브젝트만 만들어 봤다:

```text
$ clang --target=aarch64-linux-gnu -ffreestanding -std=c11 -Wall -Wextra -Werror -O2 -c demo_section.c -o /tmp/SIV-B08_elf.o
$ xcrun llvm-objdump -h /tmp/SIV-B08_elf.o | grep -E "Idx|siv"
Idx Name              Size     VMA              Type
  6 siv_tests         00000018 0000000000000000 DATA
  7 .relasiv_tests    00000048 0000000000000000
$ xcrun llvm-objdump -t /tmp/SIV-B08_elf.o | grep -E "start|stop"
0000000000000000         *UND*	0000000000000000 __start_siv_tests
0000000000000000         *UND*	0000000000000000 __stop_siv_tests
```

`__start_siv_tests` / `__stop_siv_tests` 는 오브젝트에서 **미정의(UND)** — 정의는 링크 때 GNU ld / lld가 "C 식별자 이름인 섹션"에 대해 만들어 준다. 그래서 ELF 섹션 이름에는 `.` 을 넣으면 안 된다(`.siv_tests` 는 `__start_` 심볼이 안 생김). arm-none-eabi 펌웨어처럼 링커 스크립트를 직접 쓰면 보통 이렇게 명시한다:

```text
.siv_tests : ALIGN(4) {
    PROVIDE(__start_siv_tests = .);
    KEEP(*(siv_tests))          /* --gc-sections 가 지우지 못하게 */
    PROVIDE(__stop_siv_tests = .);
} > FLASH
```

**check_mask가 남기는 것.** 실패 한 줄은 디버그 엔지니어가 JTAG/OpenOCD로 그 주소를 다시 찍어 볼 수 있어야 한다.

```text
FAIL REG.walking_bits reg=LOAD addr=0x40010008 exp=0x00000001 act=0x00000021 diff=0x00000020 bits=[5] (solution.c:437)
```

- **비치명(non-fatal)**: 실패해도 테스트를 계속한다. 레지스터 30개 중 3개가 틀린 걸 한 번에 봐야 한다. 대신 기록은 앞 4개만, 개수(`fails`)는 전부.
- **치명(fatal)**이 필요한 경우도 있다: ID가 `0xDEADBEEF` 면(클럭 꺼짐/블록 없음) 이후 체크는 쓰레기 실패 수십 개만 만든다 → 추가 연습 3의 `REQUIRE_EQ` (본문 함수에서 `return`). setjmp/longjmp 방식은 TEST-B01 참고.
- **vacuous pass 금지**: 기술서(SIV-B01)에서 RW 레지스터 필터가 잘못되면 루프가 0번 돌고 "PASS" 가 된다. 아무것도 확인 안 한 테스트가 초록불이 되는 게 가장 위험하다 → 체크 0개면 FAIL + `"reason":"no checks executed (vacuous pass)"`.
- SKIP 뒤에 FAIL을 덮어쓰지 않는다: 이미 실패한 테스트가 나중에 `TEST_SKIP` 해도 FAIL 유지.

### Part B — 필터·정렬·이스케이프

**glob 매칭** (`"*ab"` vs `"aab"` 추적):

| 단계 | pi (패턴) | si (문자열) | 동작 |
|---|---|---|---|
| 1 | 0 `*` | 0 `a` | star=0, mark=0, pi=1 (`*` 가 빈 문자열을 먹었다고 가정) |
| 2 | 1 `a` | 0 `a` | 일치 → pi=2, si=1 |
| 3 | 2 `b` | 1 `a` | 불일치 → star 있음: pi=1, si=++mark=1 (`*` 가 `a` 하나를 먹음) |
| 4 | 1 `a` | 1 `a` | 일치 → pi=2, si=2 |
| 5 | 2 `b` | 2 `b` | 일치 → pi=3, si=3, 문자열 끝 |
| 끝 | 3 = plen | | 남은 `*` 건너뛰고 pi == plen → **true** |

재귀가 없어 스택이 작은 타깃(검증 펌웨어를 칩 위에서 돌리는 경우)에서도 안전하다.

**필터 의미**: gtest의 `--gtest_filter=POS1:POS2-NEG1:NEG2` 는 `-` 뒤가 전부 제외 목록이다. 여기서는 토큰마다 `-` 를 붙이는 조금 더 단순한 규칙을 썼다 (`REG.*:-REG.walk*`). 어느 쪽이든 핵심은 ① 포함이 없으면 전부 포함 ② 제외가 우선 ③ glob은 **전체 이름**과 일치해야 함 (`"DMA"` 는 `"DMA.reset_values"` 와 불일치).

**이름순 정렬과 중복**: constructor/섹션 순서는 링크 순서다. 같은 소스라도 Makefile에서 .o 순서가 바뀌면 실행 순서가 바뀌고, 테스트 간 숨은 의존(앞 테스트가 클럭을 켜 놓음)이 있으면 어느 날 갑자기 실패한다. 정렬하면 로그 diff도 깔끔해진다. 같은 TU 안의 중복 이름은 함수 재정의라 컴파일 에러지만, **다른 .c 파일**에서 같은 `TEST(REG, reset_values)` 는 static이라 링크가 통과한다 → 런타임 검사가 필요하다 (exit 2).

**JSON 이스케이프**: RFC 8259는 문자열 안의 `"`, `\`, 제어문자(U+0000~U+001F)를 반드시 이스케이프하라고 한다. SKIP 이유 `fuse "DMA_EN"=0` 를 그대로 찍으면 그 줄은 JSON이 아니고, SIV-B14 파서가 줄 전체를 버린다. 파일 경로(`__FILE__`)도 이스케이프한다 — 윈도 경로의 `\` 가 대표적인 함정이다. 버퍼가 모자랄 때 `\"` 의 `\` 만 쓰고 끝나면 뒤의 닫는 따옴표를 이스케이프해 버려 줄 전체가 깨진다 → 이스케이프 단위로 멈춘다.

### Part C — JSON Lines 로그, 종료 코드, 결함 주입

로그 한 줄의 필드:

| 필드 | 예 | 왜 |
|---|---|---|
| `type` | `run` / `test` / `summary` / `error` | 파서가 줄 종류를 바로 구분 (SIV-B14) |
| `test`, `status` | `REG.reset_values`, `FAIL` | 재실행 필터에 그대로 붙여 넣기 |
| `duration_us` | `640` | 느린 테스트 상위 N (SIV-B14), 타임아웃 예산 |
| `checks`, `fails` | `208`, `32` | 체크 0개 탐지, 실패 규모 |
| `reg`, `addr` | `LOAD`, `0x40010008` | JTAG로 다시 찍을 주소 |
| `expected`, `actual`, `mask` | 16진 **문자열** | JSON 숫자는 16진이 없고, 64비트 값은 일부 파서(JS 등 double)에서 정밀도를 잃음 |
| `file`, `line` | `solution.c`, `412` | 어떤 CHECK가 실패했나 |
| `reason` | `addr bit 4 ignored?` | 사람이 쓴 진단 / SKIP 이유 |

`actual` 은 **읽은 원본 값**을 그대로 남기고, 어떤 비트를 비교했는지는 `mask` 가 알려준다. W1C 결함에서 STATUS의 RUNNING(bit 8, volatile)은 시계에 따라 0/1이 바뀌므로 `actual` 이 `0x0000000F` 일 수도 `0x0000010F` 일 수도 있다 — 그래서 하네스는 `expected` 와 `mask` 로 확인한다.

**시계 주입**: `run_cfg.now_us` 가 함수 포인터다. 채점 때는 "버스 접근 1회 = 1µs" 인 가짜 시계를 넣어 `MEM.march_c_minus` 의 `duration_us` 가 정확히 640 (March C- = 10n, n=64)이 되도록 했다. `--run` 모드는 `clock_gettime(CLOCK_MONOTONIC)` (NTP로 뒤로 가지 않는 시계)을 쓴다. 칩 위에서는 Cortex-M DWT `CYCCNT` 나 ARM generic timer `CNTVCT_EL0` 가 같은 자리에 들어간다. 타임아웃 자체는 SIV-B09.

**종료 코드 2 = 설정 오류**: 필터 오타(`NOPE.*`)로 0개가 돌았는데 exit 0이면 CI는 초록불이다. 도구에 따라 0개 실행을 성공으로 끝내기도 하니, 검증 러너는 이걸 별도 코드로 구분한다. 베어메탈에서 startup이 constructor를 안 돌려 등록이 0개인 경우도 같은 코드로 드러난다.

실제 실행 (`--run` 모드, stdout = JSON, stderr = 사람용 FAIL 줄):

```text
$ ./solution --run '*'
{"type":"run","filter":"*","registered":6,"matched":6}
{"type":"test","test":"DMA.reset_values","status":"SKIP","duration_us":0,"checks":0,"fails":0,"reason":"DMA block fused off (fuse \"DMA_EN\"=0)"}
{"type":"test","test":"MEM.march_c_minus","status":"PASS","duration_us":1,"checks":320,"fails":0}
{"type":"test","test":"REG.addr_alias","status":"PASS","duration_us":0,"checks":4,"fails":0}
{"type":"test","test":"REG.reset_values","status":"PASS","duration_us":0,"checks":6,"fails":0}
{"type":"test","test":"REG.rw_patterns","status":"PASS","duration_us":0,"checks":24,"fails":0}
{"type":"test","test":"REG.walking_bits","status":"PASS","duration_us":0,"checks":208,"fails":0}
{"type":"summary","pass":5,"fail":0,"skip":1,"exit":0}
$ ./solution --run 'NOPE.*'; echo "exit=$?"
{"type":"run","filter":"NOPE.*","registered":6,"matched":0}
{"type":"summary","pass":0,"fail":0,"skip":0,"exit":2}
exit=2
```

(mock이라 실제 시계로는 대부분 0~1µs다.) 주소 디코더가 bit 4를 무시하는 결함 (stderr 줄이 먼저 보이도록 정리):

```text
$ ./solution --run '*' --fault alias
FAIL REG.addr_alias reg=COMPARE addr=0x4001000C exp=0xCFCFCFCF act=0xDFDFDFDF diff=0x10101010 bits=[4,12,20,28] (solution.c:454)
FAIL REG.reset_values reg=STATUS addr=0x40010010 exp=0x00000000 act=0x54310001 diff=0x54310001 bits=[0,16,20,21,26,28,30] (solution.c:412)
FAIL REG.rw_patterns reg=STATUS addr=0x40010010 exp=0x00000000 act=0x54310001 diff=0x54310001 bits=[0,16,20,21,26,28,30] (solution.c:425)
...(rw_patterns 3줄 더)
{"type":"run","filter":"*","registered":6,"matched":6}
...(DMA SKIP, MEM PASS)
{"type":"test","test":"REG.addr_alias","status":"FAIL","duration_us":17,"checks":4,"fails":1,"reg":"COMPARE","addr":"0x4001000C","expected":"0xCFCFCFCF","actual":"0xDFDFDFDF","mask":"0xFFFFFFFF","file":"solution.c","line":454,"reason":"COMPARE@0x00C reads SCRATCH@0x01C pattern: addr bit 4 ignored?"}
{"type":"test","test":"REG.reset_values","status":"FAIL","duration_us":4,"checks":6,"fails":1,"reg":"STATUS","addr":"0x40010010","expected":"0x00000000","actual":"0x54310001","mask":"0xFFFFFEFF","file":"solution.c","line":412}
...
{"type":"summary","pass":2,"fail":3,"skip":1,"exit":1}
```

이 로그가 가르쳐 주는 것: **리셋값 테스트는 "STATUS 리셋값이 틀렸다"고 보고하지만 진짜 원인은 디코더다.** STATUS(0x010)에서 bit 4를 지우면 0x000 = ID, 그래서 `act=0x54310001` (ID 값)이 읽혔다. "실제값이 다른 레지스터의 리셋값/패턴과 같다"는 것은 거의 항상 alias 신호다. alias 테스트의 reason이 원인 비트를 바로 짚는다. 이게 리포트가 "무엇이 틀렸다"를 넘어 "어디를 보라"까지 말해야 하는 이유다.

결함 5종을 하나씩 넣고 전체를 돌린 결과 (solution.c 출력):

```text
fault \ test   reset  rw  walk  alias  march
reset            X     .    .     .      .
stuck            .     X    X     X      .
w1c              .     X    .     .      .
alias            X     X    .     X      .
cf               .     .    .     .      X
49/49 passed
```

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">결함 × 테스트 검출 매트릭스 (X = FAIL 로 잡음, 실제 실행 결과)</text>
  <g fill="currentColor" text-anchor="middle" font-weight="bold">
    <text x="335" y="52">reset (B03)</text><text x="425" y="52">rw (B04)</text><text x="515" y="52">walk (B05)</text>
    <text x="605" y="52">alias (B06)</text><text x="695" y="52">march (B07)</text>
  </g>
  <g fill="currentColor">
    <text x="10" y="84">리셋 tie-off: LOAD ^= 0x100</text>
    <text x="10" y="124">stuck-at-1: LOAD bit 5</text>
    <text x="10" y="164">W1C 를 RW 로 구현: STATUS</text>
    <text x="10" y="204">디코더가 주소 bit 4 무시</text>
    <text x="10" y="244">SRAM coupling: w10 0→1 ⇒ w11=1</text>
  </g>
  <g stroke="currentColor">
    <rect x="290" y="64" width="90" height="34" class="f-warn"/><rect x="380" y="64" width="90" height="34" class="f-mut"/><rect x="470" y="64" width="90" height="34" class="f-mut"/><rect x="560" y="64" width="90" height="34" class="f-mut"/><rect x="650" y="64" width="90" height="34" class="f-mut"/>
    <rect x="290" y="104" width="90" height="34" class="f-mut"/><rect x="380" y="104" width="90" height="34" class="f-warn"/><rect x="470" y="104" width="90" height="34" class="f-warn"/><rect x="560" y="104" width="90" height="34" class="f-warn"/><rect x="650" y="104" width="90" height="34" class="f-mut"/>
    <rect x="290" y="144" width="90" height="34" class="f-mut"/><rect x="380" y="144" width="90" height="34" class="f-warn"/><rect x="470" y="144" width="90" height="34" class="f-mut"/><rect x="560" y="144" width="90" height="34" class="f-mut"/><rect x="650" y="144" width="90" height="34" class="f-mut"/>
    <rect x="290" y="184" width="90" height="34" class="f-warn"/><rect x="380" y="184" width="90" height="34" class="f-warn"/><rect x="470" y="184" width="90" height="34" class="f-mut"/><rect x="560" y="184" width="90" height="34" class="f-warn"/><rect x="650" y="184" width="90" height="34" class="f-mut"/>
    <rect x="290" y="224" width="90" height="34" class="f-mut"/><rect x="380" y="224" width="90" height="34" class="f-mut"/><rect x="470" y="224" width="90" height="34" class="f-mut"/><rect x="560" y="224" width="90" height="34" class="f-mut"/><rect x="650" y="224" width="90" height="34" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-weight="bold">
    <text x="335" y="86">X</text><text x="425" y="126">X</text><text x="515" y="126">X</text><text x="605" y="126">X</text>
    <text x="425" y="166">X</text><text x="335" y="206">X</text><text x="425" y="206">X</text><text x="605" y="206">X</text><text x="695" y="246">X</text>
  </g>
  <text x="10" y="284" fill="currentColor">어느 한 테스트도 5종을 다 잡지 못한다 → 세트로 돌려야 하고, 그래서 프레임워크가 필요하다.</text>
</svg>
```

칸마다 이유:

- **tie-off → reset만**: R/W 테스트는 처음 쓰기에서 RW 비트를 모두 덮으므로 리셋값을 볼 기회가 없다. 실제 칩에서 리셋값 오류는 RTL의 잘못된 tie-off / 기술서와 RTL 불일치로 자주 나오고, 리셋값 테스트(UVM `uvm_reg_hw_reset_seq` 의 post-silicon 버전)만 잡는다.
- **stuck-at-1 bit5 → reset은 못 잡음**: LOAD 리셋값 `0xFFFF` 의 bit 5가 원래 1이라서. "리셋값이 1인 비트의 stuck-at-1"은 리셋 테스트의 사각지대다. 패턴 0x0 쓰기(rw), walking(walk), 주소 패턴(alias, `0xCBCBCBCB` 의 bit 5 = 0)이 잡는다.
- **W1C→RW → rw만**: 0xFFFFFFFF를 쓰면 W1C 비트는 0으로 남아야 하는데 1이 된다. walking은 RW 비트만 걷기 때문에 STATUS를 건드리지 않는다. UVM `uvm_reg_bit_bash_seq` 도 W1C 같은 비-RW 필드 비트는 비교에서 빼고(don't-care) 돈다. 즉 bit-bash만으로는 이 결함이 안 보이고, access type별 기대값 모델(SIV-B04)이 있어야 잡힌다.
- **주소 bit 4 무시 → walking은 못 잡음**: SCRATCH(0x01C) 쓰기·읽기가 둘 다 COMPARE(0x00C) 셀로 가서 자기 자신과는 일관되기 때문. "전부 쓰고 전부 읽기"(alias)만 진짜 원인을 짚는다.
- **SRAM coupling → march만**: 레지스터 테스트는 SRAM을 안 본다. March C-(SIV-B07)의 ⇑r0w1 원소에서 victim(워드 11)을 읽을 때 잡힌다: `addr=0x2000002C exp=0x00000000 act=0xFFFFFFFF`.

pre-silicon과의 대응: UVM RAL에는 이미 `uvm_reg_hw_reset_seq`, `uvm_reg_bit_bash_seq`, `uvm_reg_access_seq`, `uvm_mem_walk_seq` 같은 내장 시퀀스가 있고, 결과는 UVM report 서버가 집계한다. post-silicon에서는 같은 의도의 테스트를 C로 다시 써서 JTAG/OpenOCD로 내려받거나 on-chip 펌웨어로 돌리고, 결과를 이런 JSON 로그로 뽑아 ATE → SLT → system validation 단계 사이에서 같은 리포트 도구(SIV-B14)로 비교한다. 같은 테스트를 mock 백엔드와 실보드 백엔드에서 돌리는 구조는 SIV-B13.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution            # 49/49 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san   # 49/49, 경고 없음
./solution --run 'REG.*' --fault stuck; echo "exit=$?"                                  # 실패 리포트 + exit=1
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 체크 0개 테스트를 PASS로 | 기술서 필터 버그로 루프가 0번 돌면 아무것도 안 보고 초록불 | `if (status == PASS && nchecks == 0) status = FAIL;` |
| 실패 기록 배열에 무조건 `fails[nfails++] = ...` | walking 테스트 32실패 → 오버런. 실제로 해 보니 UBSan `index 4 out of bounds for type 'fail_rec[4]'`, 이어서 ASan `stack-buffer-overflow` | `if (nfails < MAX) fails[nfails] = ...; nfails++;` |
| 레지스트리 꽉 차면 조용히 무시 | 테스트 129번째부터 영영 안 돈다 | `overflow++` 후 러너가 exit 2 |
| 등록 순서에 의존 | 링크 순서가 바뀌면 실행 순서·결과가 바뀜 | 실행 전 이름순 `qsort` |
| 매칭 0개를 exit 0으로 | 필터 오타·constructor 미실행이 초록불 | `matched == 0 → exit 2` |
| 이름·이유 문자열을 그대로 `"%s"` 로 JSON에 | `"` 하나로 줄 전체가 JSON이 아니게 됨 | `json_escape()` 후 출력 |
| 값을 JSON 숫자로 (`"actual":4294967295`) | 16진이 안 보이고, 64비트 값은 double 파서에서 정밀도 손실 | `"actual":"0xFFFFFFFF"` |
| alias 테스트를 write→read 반복으로 | 같은 셀에 쓰고 읽어서 alias가 안 보임 | 전부 쓰고 → 전부 읽기 |
| 테스트마다 IP 리셋 안 함 | 앞 테스트가 남긴 상태(W1C 플래그, CTRL)로 다음 테스트 결과가 바뀜 | `before_each = ip_reset` |
| `TEST(A_B, c)` 와 `TEST(A, B_c)` | 둘 다 `siv_A_B_c` → 심볼 충돌 | 구분자 `__` (`siv_A_B__c` vs `siv_A__B_c`) |
| 테스트 .o를 static library(.a)로 묶음 | 링커는 미정의 심볼을 풀 때만 .a 멤버를 끌어온다 → 아무도 참조 안 하는 테스트 .o는 빠짐 | ELF `-Wl,--whole-archive`, Mach-O `-Wl,-force_load,lib.a` |

## 8. 면접 Q&A

<details>
<summary>Q1. C로 테스트 자동 등록을 어떻게 하나요? constructor와 linker section의 차이는?</summary>

① `__attribute__((constructor))` 함수가 main 전에 전역 레지스트리에 자기 테스트를 넣는다 — 이식성이 좋고 구현이 쉽다. 단 startup이 `.init_array` 를 실행해야 하고(newlib `__libc_init_array`), 순서는 링크 순서. ② 테스트 descriptor(또는 포인터)를 전용 섹션에 넣고 `__start_<sec>`/`__stop_<sec>` 로 훑는다 — main 전 코드가 필요 없고 ROM에 const로 놓을 수 있어 베어메탈·커널(initcall, KUnit, Zephyr)이 선호. 단 링커 스크립트에 `KEEP` 이 필요하고 Mach-O는 `getsectiondata` / `section$start$` 를 써야 한다.
꼬리질문 "constructor인데 -O2에서 init 섹션이 없던데요?" → LLVM이 단순 저장만 하는 constructor를 컴파일 시간에 평가해 레지스트리를 초기화 데이터로 바꿔 버렸다. 결과는 같다.

</details>

<details>
<summary>Q2. 테스트가 실패했을 때 로그에 무엇을 남기나요?</summary>

테스트 이름, 레지스터 이름, **절대 주소**(JTAG로 다시 찍을 수 있게), 기대값, 실제 읽은 원본 값, 비교 마스크, diff 비트 목록, 소스 파일:줄, 그리고 실행 환경(보드·실리콘 리비전·FW 버전·시드·전압/온도 — 헤더는 SIV-B14). 실패 수가 많으면 앞 N개만 상세, 개수는 전부.
꼬리질문 "타임아웃이면?" → 마지막으로 읽은 값, 기다린 시간, 기다리던 마스크/값, 호출 위치(SIV-B09). "버스 에러면?" → 그 주소와 접근 폭, 그리고 직전 성공 접근.

</details>

<details>
<summary>Q3. CHECK 실패 시 테스트를 멈춰야 하나요, 계속해야 하나요?</summary>

기본은 계속(non-fatal) — 한 번에 실패 패턴 전체를 봐야 원인(한 비트? 한 바이트 레인? 전부?)이 보인다. 하지만 전제 조건이 깨지면 멈춘다(fatal): ID가 `0xDEADBEEF`/`0xFFFFFFFF` (클럭 게이트·파워 도메인 꺼짐·블록 없음)면 이후 체크는 의미 없는 실패만 쏟아낸다. C에서는 본문이 함수라 `REQUIRE` 매크로 안에서 `return` 하면 되고, 중첩 호출 깊은 곳에서 빠져나와야 하면 setjmp/longjmp (TEST-B01).

</details>

<details>
<summary>Q4. 결과를 왜 JSON Lines로 남기나요? JUnit XML이나 TAP은?</summary>

한 줄 = 한 레코드라 ① 테스트 도중 보드가 죽어도 그 전 줄들은 온전하다(append-only) ② `grep`/`jq` 로 바로 거르고 ③ 파서가 아주 작다(SIV-B14가 직접 짠다). CI 서버가 JUnit XML을 원하면 JSON Lines → XML 변환기를 두면 된다. TAP(Test Anything Protocol)도 줄 단위라 비슷한 장점이 있지만 구조화된 필드(addr/expected/actual)를 넣기엔 JSON이 편하다.
꼬리질문 "로그를 칩 위에서 어떻게 꺼내나?" → UART/semihosting/RTT로 스트리밍하거나 SRAM 링버퍼에 쓰고 JTAG로 덤프.

</details>

<details>
<summary>Q5. 테스트 실행 순서는 어떻게 정하나요? 테스트 간 의존은?</summary>

이름순으로 고정해 결정적으로 만들고, 각 테스트 앞에서 IP를 리셋해(before_each) 서로 독립적으로 만든다. 그래야 `--run 'REG.rw_*'` 로 하나만 돌려도 전체에서 돌릴 때와 같은 결과가 나온다. 순서 의존 버그를 찾으려면 일부러 셔플(고정 시드) 모드를 둔다.
꼬리질문 "리셋이 비싸면?" → suite 단위 setup/teardown, 또는 리셋 없이 도는 테스트를 별도 suite로 묶고 그 사실을 이름에 드러낸다.

</details>

<details>
<summary>Q6. 필터가 아무 테스트도 고르지 못하면 종료 코드는?</summary>

설정 오류로 0이 아닌 코드(여기선 2). 0이면 오타 난 nightly 잡이 몇 주 동안 초록불이 된다. 같은 이유로 중복 이름·레지스트리 overflow도 2. 1(테스트 실패)과 2(인프라/설정 문제)를 나누면 CI가 "칩 버그"와 "스크립트 버그"를 다른 사람에게 보낼 수 있다.

</details>

<details>
<summary>Q7. 레지스터 테스트 몇 개로 어떤 결함을 잡을 수 있는지 설명해 보세요.</summary>

이 레슨의 매트릭스: 리셋값 테스트는 tie-off 오류만 확실히 잡고(리셋값이 1인 비트의 stuck-at-1은 못 봄), R/W 패턴은 access type 오류(W1C를 RW로)를, walking 1/0은 stuck-at·비트 간 short를, 전부-쓰고-전부-읽는 alias 테스트는 디코더 결함을, March C-는 SRAM coupling을 잡는다. 하나로 다 되는 테스트는 없어서 세트로 돌리고, 결함 주입(fault injection)으로 각 테스트가 정말 그 결함을 잡는지 테스트 자체를 검증한다 — "테스트의 테스트".

</details>

## 9. 추가 연습

### 연습 1 — `--list`: 실행하지 않고 이름만

`int list_tests(test_registry *r, const char *filter, logbuf *lb)` — 필터에 맞는 테스트 이름을 이름순으로 한 줄씩 쓰고 개수를 돌려준다. 레지스트리 오류면 -1.

<details>
<summary>답 보기</summary>

`registry_finalize` 를 먼저 부르고(정렬 + 중복 검사), 매칭되는 이름만 `log_printf(lb, "%s\n", name)`. 실제 랩에서는 "이 보드 설정에서 몇 개가 돌 건지" 를 실행 전에 확인하는 데 쓴다. 결과: `REG.*:-REG.walk*` → `REG.addr_alias`, `REG.id_first`, `REG.reset_values`, `REG.rw_patterns` (4개).

</details>

### 연습 2 — CI 샤딩

보드 3대에 테스트를 나눠 돌린다. `run_tests_shard(cfg, r, shard, nshards, s)` — 이름순으로 정렬된 **매칭 테스트**의 k번째를 `k % nshards == shard` 인 샤드가 맡는다. 세 샤드의 합 = 전체, 서로 겹치지 않음을 확인하라.

<details>
<summary>답 보기</summary>

정렬 후 인덱스로 나누므로 어느 보드에서 돌려도 같은 샤드 = 같은 테스트 집합이다(정렬이 없으면 링크 순서가 다른 빌드끼리 샤드가 어긋난다). 매칭된 것 중에서 세어야 필터와 함께 써도 균형이 맞는다. 7개 → 3/2/2. run 헤더에 `"shard":"0/3"` 을 남겨 리포트가 합칠 수 있게 한다. gtest의 `GTEST_TOTAL_SHARDS` / `GTEST_SHARD_INDEX` 와 같은 발상.

</details>

### 연습 3 — `REQUIRE_EQ`: 클럭이 꺼진 블록

mock에 `g_clock_gated` 를 켜면 모든 읽기가 `0xDEADBEEF`, 쓰기는 무시된다. ID를 먼저 `REQUIRE_EQ` 로 확인하고 틀리면 즉시 끝나는 `TEST(REG, id_first)` 를 작성하라. `REG.reset_values` (비치명)와 실패 수를 비교하라.

<details>
<summary>답 보기</summary>

```c
#define REQUIRE_EQ(what, addr, exp, act) \
    do { if (!CHECK_EQ(what, addr, exp, act)) { \
             TEST_NOTE("REQUIRE %s failed: test aborted", (what)); return; } } while (0)
```

본문 함수 안에서 `return` 하므로 longjmp가 필요 없다. 결과: `REG.reset_values` 는 `checks:6, fails:6` (쓰레기 실패 6개), `REG.id_first` 는 `checks:1, fails:1, actual:"0xDEADBEEF", reason:"REQUIRE ID failed: test aborted"`. 리포트를 읽는 사람이 "클럭/파워 도메인부터 보라"는 걸 한 줄로 안다 (bring-up 순서는 SIV-B10).

</details>

### 연습 4 — 간헐(flaky) 결함 잡기: repeat-until-fail

mock의 `g_flaky_period = 3` 이면 3번째 리셋마다 LOAD 리셋값 bit 0이 틀린다(리셋 동기화 마진 문제 흉내). `repeat_until_fail(cfg, tc, max_iter, t)` — 최대 N번 돌려 처음 FAIL한 반복 번호(1부터), 끝까지 통과면 0.

<details>
<summary>답 보기</summary>

매 반복이 `run_one` (= 리셋 포함)이라 리셋 경로의 간헐 결함이 드러난다. 10번 돌리면 3에서 FAIL(`LOAD act=0x0000FFFE`), 2번만 돌리면 0(못 잡음) — 간헐 결함은 반복 횟수가 곧 검출 확률이다. 실제 post-silicon에서는 전압/온도/주파수 코너(shmoo)와 함께 수천 번 돌리고, 처음 실패한 반복 번호와 그때의 조건을 로그에 남긴다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 20/20 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san && ./practice_san   # 20/20, 경고 없음
```

## 10. 핵심 정리

- `TEST()` = 본문 함수 + const descriptor + 등록 장치. 등록은 **constructor**(이식성) 또는 **linker section**(main 전 코드 없음, 베어메탈·커널 선호; ELF `__start_/__stop_`, Mach-O `getsectiondata`).
- 등록 순서는 링크 순서 → **이름순 정렬**로 결정적 실행. 다른 .c의 같은 이름은 런타임에 검사.
- 체크는 비치명 + 앞 N개 상세 기록 + 개수는 전부. 전제 조건은 `REQUIRE` 로 즉시 종료. **체크 0개 = FAIL.**
- 실패 기록 = 이름·절대 주소·expected·**원본** actual·mask·diff 비트·file:line. 값은 16진 문자열.
- 로그는 JSON Lines(`run` / `test` / `summary`), 문자열은 반드시 이스케이프. 종료 코드 0 / 1(FAIL) / **2(설정 오류: 매칭 0개·중복·overflow)**.
- 시계와 IP 리셋은 주입(`now_us`, `before_each`) → 호스트에선 결정적, 칩에선 실제 타이머.
- 어떤 테스트도 모든 결함을 못 잡는다: tie-off→reset, W1C→RW→rw, stuck-at→rw/walk, 디코더→alias, SRAM coupling→March. 결함 주입으로 **테스트 자체를 검증**한다.

## 참고자료

- [GoogleTest — Running a subset of tests (`--gtest_filter`), sharding](https://google.github.io/googletest/advanced.html)
- [GCC — Common Function Attributes (`constructor`, `section`, `used`, `retain`)](https://gcc.gnu.org/onlinedocs/gcc/Common-Function-Attributes.html)
- [GNU ld — Input Section Keep (`KEEP`) / orphan sections and `__start_SECNAME`](https://sourceware.org/binutils/docs/ld/Input-Section-Keep.html)
- [Linux kernel — KUnit architecture (test suites in a linker section)](https://docs.kernel.org/dev-tools/kunit/architecture.html)
- [Zephyr — Iterable Sections](https://docs.zephyrproject.org/latest/kernel/iterable_sections/index.html)
- [RFC 8259 — The JavaScript Object Notation (JSON) Data Interchange Format, §7 Strings](https://www.rfc-editor.org/rfc/rfc8259)
- [JSON Lines](https://jsonlines.org/)
- [Accellera UVM — Register layer built-in sequences (`uvm_reg_hw_reset_seq`, `uvm_reg_bit_bash_seq`, `uvm_mem_walk_seq`)](https://www.accellera.org/downloads/standards/uvm)
- 관련 레슨: SIV-B02 (mock MMIO), SIV-B03~B07 (이식한 테스트), SIV-B09 (timeout), SIV-B13 (백엔드 교체), SIV-B14 (로그 → 리포트), TEST-B01 (setjmp 기반 미니 프레임워크)
