---
id: SIV-M12
time: 95분
updated: 2026-11-08
---
# SIV-M12 Python 테스트 시퀀서

## 학습 목표

- C 테스트(무엇을 검사하나)와 **시퀀서**(언제·어떤 조건에서·몇 번·결과를 어디에)를 나누는 이유를 설명하고, YAML 테스트 리스트 → 검증 → 장비 setup → 실행 → DB → 리포트 흐름을 직접 만든다.
- PyYAML 없이 **문서화된 YAML 부분집합** 파서를 쓰고, `1.10 → float`, `NO → false`(YAML 1.1), `0750 → 8진?` 같은 함정을 스키마 검증으로 막는다. 장비를 건드리기 **전에** 설정 오류를 exit 2로 끝낸다.
- 같은 C 소스를 **ctypes(.dylib)** 와 **바이너리(subprocess)** 두 러너로 부르고, 각각이 행(hang)·크래시에서 어떻게 다른지(죽일 수 있나, 시퀀서가 살아남나) 실측으로 보인다.
- 재시도를 **FLAKY** 로 기록해 간헐 실패를 숨기지 않고, `requires` 사전조건 → SKIP, `finally` teardown(전원 OFF · 상온 복귀)을 보장한다.
- 결과를 SQLite에 시도 단위로 저장하고, 디버그 엔지니어가 바로 움직일 수 있는 리포트(코너 × 테스트 매트릭스, 주소/기대/실제/diff, seed)와 CI 종료 코드를 만든다.

## 1. 문제

실리콘 검증 랩의 밤은 사람이 아니라 **시퀀서**가 돌린다: 챔버를 -40 °C로 내리고, 전원을 0.72 V로 맞추고, C 테스트 수백 개를 돌리고, 아침에 리포트 한 장을 남긴다 — Rivian·NVIDIA 검증 면접의 "벤치 자동화를 어떻게 짰나?"·"테스트가 행 걸리면 어떻게 되나?" 질문이 정확히 이 레슨이다.

SIV-B08에서 C로 테스트 프레임워크를, SIV-B14에서 로그 → 리포트를 만들었다. 그 C 테스트는 "**무엇을** 검사하나"다. 실제 랩에서는 그 위에 Python 층이 하나 더 있다: 어떤 **조건(PVT 코너)** 에서, 어떤 **순서**로, **몇 번**(재시도), **얼마나 기다리고**(타임아웃), 무엇이 **먼저 통과해야**(사전조건) 하는지, 그리고 결과를 **어디에** 남기는지. 업계 도구로는 NI TestStand, Google의 OpenHTF, pytest + fixture, Robot Framework 같은 것들이 이 자리에 있고, 장비는 PyVISA + SCPI로 제어하는 경우가 많다. 이 레슨은 그 골격을 표준 라이브러리만으로 만든다.

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m12a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">SIV-M12 시퀀서: 리스트 → 검증 → 장비 → 러너 → DB → 리포트</text>
  <rect x="10" y="40" width="130" height="70" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="75" y="64" text-anchor="middle" fill="currentColor" font-weight="bold">testlist.yaml</text>
  <text x="75" y="84" text-anchor="middle" fill="currentColor" font-size="12">run · limits</text>
  <text x="75" y="100" text-anchor="middle" fill="currentColor" font-size="12">corners · tests</text>
  <rect x="170" y="40" width="140" height="70" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="240" y="64" text-anchor="middle" fill="currentColor" font-weight="bold">A: parse + validate</text>
  <text x="240" y="84" text-anchor="middle" fill="currentColor" font-size="12">E_KEY E_LIMIT E_FUNC</text>
  <text x="240" y="100" text-anchor="middle" fill="currentColor" font-size="12">오류 → exit 2, 장비 0회</text>
  <rect x="340" y="40" width="190" height="150" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="435" y="62" text-anchor="middle" fill="currentColor" font-weight="bold">B: Sequencer.run</text>
  <text x="352" y="84" fill="currentColor" font-size="12">for corner: apply_corner</text>
  <text x="352" y="102" fill="currentColor" font-size="12">  for test: requires? → SKIP</text>
  <text x="352" y="120" fill="currentColor" font-size="12">    run_with_retries</text>
  <text x="352" y="138" fill="currentColor" font-size="12">    → PASS/FLAKY/FAIL/</text>
  <text x="352" y="156" fill="currentColor" font-size="12">      TIMEOUT/CRASH/ERROR</text>
  <text x="352" y="176" fill="currentColor" font-size="12">finally: safe_state()</text>
  <rect x="170" y="140" width="140" height="80" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="240" y="162" text-anchor="middle" fill="currentColor" font-weight="bold">Bench (mock)</text>
  <text x="240" y="182" text-anchor="middle" fill="currentColor" font-size="12">PSU: VOLT / OUTP</text>
  <text x="240" y="200" text-anchor="middle" fill="currentColor" font-size="12">Chamber: TEMP ramp+soak</text>
  <rect x="560" y="40" width="190" height="64" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="655" y="62" text-anchor="middle" fill="currentColor" font-weight="bold">ctypes 러너</text>
  <text x="655" y="82" text-anchor="middle" fill="currentColor" font-size="12">CDLL(siv_tests.dylib)</text>
  <text x="655" y="98" text-anchor="middle" fill="currentColor" font-size="12">같은 프로세스 · 1.5 µs/호출</text>
  <rect x="560" y="120" width="190" height="64" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="655" y="142" text-anchor="middle" fill="currentColor" font-weight="bold">바이너리 러너</text>
  <text x="655" y="162" text-anchor="middle" fill="currentColor" font-size="12">subprocess + timeout</text>
  <text x="655" y="178" text-anchor="middle" fill="currentColor" font-size="12">격리 · 3.7 ms/호출</text>
  <rect x="340" y="240" width="190" height="70" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="435" y="262" text-anchor="middle" fill="currentColor" font-weight="bold">C: SQLite 결과 DB</text>
  <text x="435" y="282" text-anchor="middle" fill="currentColor" font-size="12">run · result · attempt · instr</text>
  <text x="435" y="298" text-anchor="middle" fill="currentColor" font-size="12">결과마다 commit</text>
  <rect x="560" y="240" width="190" height="70" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="655" y="262" text-anchor="middle" fill="currentColor" font-weight="bold">리포트 + exit</text>
  <text x="655" y="282" text-anchor="middle" fill="currentColor" font-size="12">코너×테스트 매트릭스</text>
  <text x="655" y="298" text-anchor="middle" fill="currentColor" font-size="12">0 PASS / 1 FAIL / 2 설정</text>
  <g stroke="currentColor" fill="none" marker-end="url(#m12a)">
    <path d="M140 75 L168 75"/><path d="M310 75 L338 75"/><path d="M530 72 L558 72"/><path d="M530 152 L558 152"/>
    <path d="M340 175 L312 175"/><path d="M435 190 L435 238"/><path d="M530 275 L558 275"/>
  </g>
  <text x="10" y="250" fill="currentColor" font-size="12">DUT 조건 = 장비가 읽은 값</text>
  <text x="10" y="268" fill="currentColor" font-size="12">(measure_mv, measure_c)</text>
  <text x="10" y="286" fill="currentColor" font-size="12">→ siv_set_env / --vdd --temp</text>
</svg>
```

실리콘이 없으므로 DUT는 `siv_tests.c` 안의 mock "SoC A0"다 (C 테스트 6개 + mock 레지스터·SRAM·I2C·PLL·DMA). `defects` 비트가 소켓에 꽂힌 **불량 샘플**을 흉내 낸다:

| bit | 결함 | 언제 드러나나 | 시퀀서가 봐야 할 모습 |
|---|---|---|---|
| 0 | SRAM word 0x2A7 bit13 약한 셀 | vdd < 740 mV **그리고** temp ≥ 100 °C | `sram.march` FAIL, LV_HOT 코너에서만 |
| 1 | I2C 간헐 NACK (byte 3) | 시도마다 ~40%, seed·attempt로 결정적 | 재시도 후 통과 → **FLAKY** (PASS로 숨기면 안 됨) |
| 2 | PLL이 저온에서 lock 실패 | temp ≤ -20 °C | `max_polls: 0`(무한 대기) 설정과 겹쳐 **행** → TIMEOUT |
| 3 | DMA가 링 끝을 넘어 descriptor fetch | `desc_count` > 16 | 버스 폴트(호스트에선 `raise(SIGSEGV)` 모델) → **CRASH** |

### Part A — 테스트 리스트: YAML 부분집합 파서 · 스키마 검증 · ctypes 바인딩

- `parse_scalar(s)` / `_parse_map` / `_parse_list` — 매핑, 리스트(`- a: 1` 포함, 부모 키와 같은 칸의 `-` 도 허용), 한 줄 flow 리스트 `[a, b]`, 스칼라, 주석. 탭 들여쓰기·중복 키·앵커(`&`/`*`)·블록 문자열(`|`/`>`)·`---`·앞자리 0 정수는 **줄 번호와 함께 거부**한다.
- `_validate_test` — `E_NAME E_DUPNAME E_FUNC E_RUNNER E_RANGE E_REQUIRES E_CORNER E_PARAM` (키 오타 `E_KEY`·타입 `E_TYPE`·코너 한계 `E_LIMIT` 는 주어짐).
- `CTypesRunner._bind` / `run` — `argtypes/restype`, API major 버전과 `sizeof(siv_result_t)` 일치 확인, `siv_run()` 결과 구조체 → `Outcome`.

### Part B — 러너·재시도·사전조건·teardown (면접관이 파고드는 곳)

- `BinaryRunner.run` — `subprocess.run(timeout=...)`: 시간 초과 → TIMEOUT(프로세스 kill), 음수 returncode → CRASH(시그널 이름), JSON 줄 깨짐 / status와 exit code 불일치 → ERROR.
- `run_with_retries` — **FAIL에만** 재시도. FAIL→PASS는 FLAKY(첫 실패 정보 보존), 끝까지 FAIL은 FAIL. TIMEOUT/CRASH/ERROR는 재시도하지 않는다.
- `Sequencer.run` — 코너마다 setup, `requires` 가 이 코너에서 PASS/FLAKY가 아니면 SKIP, Python 예외는 그 테스트만 ERROR, `AbortRun`(운영자 중단)은 exit_code 3 기록 후 다시 던짐, **무슨 일이 있어도** `finally: safe_state()`.

### Part C — nightly 시나리오: 3 코너 × 6 테스트 → SQLite → 리포트

- `make_report(db, run_id)` — SQL `GROUP BY` 로 집계, 실패/FLAKY/SKIP 섹션(주소·기대·실제·XOR diff·seed), 시뮬레이션 벤치 시간, 종료 코드(0 / 1 / 2).
- 불량 샘플(`defects=0xF`)을 돌리면 결함 4개가 **각자 맞는 코너·맞는 상태**로 나와야 하고, 골든 샘플(`0`)은 18/18 PASS, exit 0이어야 한다. 설정 오류 리스트는 exit 2이고 장비 명령이 **하나도** 나가면 안 된다.

## 2. Problem (English)

> **Build a lab test sequencer in Python (standard library only).**
>
> A C test library `siv_tests.c` exports `int siv_run(const char *func, const char *params, uint32_t seed, int attempt, siv_result_t *out)`
> (plus `siv_set_env`, `siv_set_defects`, `siv_test_name`, `siv_api_version`, `siv_result_size`). Built with `-dynamiclib` it is loaded via
> `ctypes`; built as an executable it is a CLI (`--run NAME ...`) that prints one JSON result line and exits 0/1/2 (PASS/FAIL/ERROR).
>
> 1. Parse a test list written in a documented YAML subset (no PyYAML): `run` metadata, `limits`, `corners` (name, vdd_mv, temp_c) and
>    `tests` (name, func, runner `ctypes|binary`, timeout_s, retries, requires, corners, params). Reject unsupported syntax and schema
>    errors with line numbers / error codes **before** any instrument is touched (exit 2).
> 2. Run each test through the requested runner. The binary runner must enforce `timeout_s` (TIMEOUT), detect signals (CRASH) and
>    cross-check the JSON status against the exit code. Retry only `FAIL`; a fail followed by a pass is `FLAKY`, not `PASS`.
> 3. For each corner: power off → set chamber temperature (ramp + soak) → set voltage → power on, then run the tests whose `requires`
>    passed at that corner (otherwise `SKIP` with the reason). Always return the bench to a safe state (PSU off, ambient) — even on abort.
> 4. Store run / result / attempt / instrument-command rows in SQLite and render a report with a corner × test matrix, failure details
>    (`reg addr exp act diff seed`) and an exit code: 0 = no FAIL/TIMEOUT/CRASH/ERROR, 1 = otherwise, 2 = configuration error / no results.
>
> Example: on a defective sample, `sram.march @LV_HOT` must report `reg=SRAM addr=0x20000A9C exp=0xFFFFFFFF act=0xFFFFDFFF diff=0x00002000`,
> `pll.lock @NOM_COLD` must be `TIMEOUT` (killed after 0.5 s) and the dependent `dma.ring @NOM_COLD` must be `SKIP requires pll.lock = TIMEOUT`.

## 3. 시작 코드

<!-- code: starter.py -->

테스트 리스트와 C 테스트 라이브러리(완성본 — 레슨의 TODO는 전부 Python 쪽이다):

<!-- code: testlist.yaml -->

<!-- code: siv_tests.c -->

```bash
cd lessons/SIV/SIV-M12
python3 -I starter.py; echo "exit=$?"      # 하네스가 siv_tests.c 를 임시 디렉터리에 dylib + 바이너리로 직접 빌드한다
# (SDKROOT 가 없으면 MacOSX26.4.sdk 로 채워서 cc 를 부른다 — 이 맥의 27.2 SDK 링크 문제 회피)
```

실제 출력 (일부):

```text
-- Part A: YAML subset --
FAIL  parse_scalar('0x5EED') == 24301  (NotImplementedError: TODO parse_scalar)
....  setup: NotImplementedError: TODO _parse_map
FAIL  testlist.yaml: run.seed == 0x5EE9, fw str  (TypeError: 'NoneType' object is not subscriptable)
PASS  tab indentation -> YamlError line 3
-- Part A: test-list validation + ctypes --
FAIL  ctypes: C library exports the 6 tests  (AttributeError: 'int' object has no attribute 'decode')
PASS  ctypes: sizeof(SivResult) == siv_result_size() == 132
...
PASS  PSU refuses 1200 mV (OVP 1000) and sends nothing
PASS  chamber 25 -> -40 C @10 C/min + 300 s soak = 690 s fake time
....  setup: NotImplementedError: TODO Sequencer.run
FAIL  AbortRun mid-run -> re-raised  (NotImplementedError: TODO Sequencer.run)
...
4/58 passed
exit=1
```

> 눈여겨볼 것: `restype` 를 안 정한 starter에서 `siv_test_name()` 이 **`int`** 로 돌아온다 (`'int' object has no attribute 'decode'`). ctypes의 기본 반환형은 C `int` 라서 64비트 포인터의 하위 32비트만 남는다. 반대로 `siv_result_size()` 는 값이 132로 작아서 기본형으로도 "우연히" 맞는다 — 맞게 보이는 버그가 제일 위험하다.

TODO는 10곳: `parse_scalar`, `_parse_map`, `_parse_list`, `_validate_test` 의 의미 검사, `CTypesRunner._bind`/`run` (Part A), `BinaryRunner.run`, `run_with_retries`, `Sequencer.run` (Part B), `make_report` 의 집계·섹션·종료 코드 (Part C). 장비 mock(`FakeClock`, `MockPSU`, `MockChamber`, `Bench`), DB 쓰기(`ResultDB`), 전체 흐름(`sequence`)은 주어진다.

## 4. 힌트

<details>
<summary>힌트 1 — YAML은 "들여쓰기 칸 = 재귀 깊이"</summary>

주석·빈 줄을 지운 `[줄번호, 들여쓰기, 내용]` 목록(주어짐)을 만든 뒤, `_parse_map(lines, i, indent)` 는 **같은 칸**의 `key: value` 줄을 소비하다가 칸이 줄면 돌려준다. 값이 비어 있으면(`key:`) 다음 줄이 더 깊으면 그 칸으로 `_parse_block`, 같은 칸인데 `- ` 로 시작하면 리스트다. `- name: x` 는 그 줄을 "`-` 다음 칸에 있는 `name: x`" 로 바꿔 `_parse_map` 을 부르면 이어지는 `  func: y` 줄들이 자연스럽게 같은 매핑이 된다.

</details>

<details>
<summary>힌트 2 — 스칼라는 "모호하면 거부"</summary>

순서가 중요하다: 따옴표 → `null`/`~` → `true`/`false`(소문자만) → 앞자리 0 정수(`^[-+]?0[0-9]+$`) 거부 → `INT_RE`(10진, `0x`) → `FLOAT_RE` → 특수 문자로 시작하면 거부 → 문자열. `int(s, 0)` 은 `0x5EED` 와 `-40` 을 둘 다 처리하고, `int("0750", 0)` 은 Python 자체가 ValueError를 낸다 — 그 전에 더 친절한 메시지로 거부하자.

</details>

<details>
<summary>힌트 3 — ctypes: 프로토타입을 Python에 다시 적는다</summary>

`L.siv_run.argtypes = [c_char_p, c_char_p, c_uint32, c_int, POINTER(SivResult)]`, `restype = c_int`. `siv_test_name.restype = c_char_p` 면 `bytes` 가 온다. 구조체는 C와 필드 순서·타입이 같아야 하므로 C가 `sizeof(siv_result_t)` 를 내보내게 하고 로드 직후 비교한다. 결과 구조체는 `SivResult()` 하나 만들어 `ctypes.byref(r)` 로 넘긴다.

</details>

<details>
<summary>힌트 4 — subprocess 결과 3갈래</summary>

`subprocess.run(cmd, capture_output=True, text=True, timeout=t)` 는 시간이 넘으면 자식을 kill하고 `TimeoutExpired` 를 던진다. 끝났으면 `returncode < 0` 이 "시그널로 죽음"(`-11` = SIGSEGV, `signal.Signals(11).name`). 그 밖엔 stdout 마지막 줄을 JSON으로 읽고 **exit code와 status가 같은지** 확인한다 — 둘이 다르면 테스트 바이너리가 거짓말을 하는 것이니 ERROR.

</details>

<details>
<summary>힌트 5 — 시퀀서 골격은 try / except AbortRun / finally</summary>

```python
run_id = db.new_run(...)
try:
    for corner ...: bench.apply_corner(corner); here = {}
        for spec ...: SKIP or run_with_retries(...) (except AbortRun: raise / except Exception: ERROR)
except AbortRun:
    db.finish(run_id, 3); raise
finally:
    bench.safe_state(); db.add_instr(...); db.set_bench_time(...)
```

`here` 는 **코너마다** 새로 만든다 — 사전조건은 "같은 조건에서 통과했나" 이기 때문이다.

</details>

## 5. 풀이

### Part A — 리스트를 믿기 전에 검증한다

**왜 YAML 부분집합인가.** 이 맥의 Python 3.9에는 PyYAML이 없고(레슨은 표준 라이브러리만), 랩 PC에 패키지를 마음대로 깔 수 없는 경우도 흔하다. 더 중요한 이유: 테스트 리스트에 필요한 건 매핑·리스트·스칼라뿐인데, 완전한 YAML은 앵커·태그·여러 문서·암묵 타입 변환까지 있다. 쓰지 않는 기능은 **명시적으로 거부**하는 게 조용히 다르게 해석되는 것보다 안전하다.

| 입력 | 이 파서 | 완전한 YAML이라면 | 왜 이렇게 정했나 |
|---|---|---|---|
| `fw: 1.10` | `float 1.1` → 스키마가 `E_TYPE run.fw ... (quote it)` | 1.1 (float) | 버전 문자열이 숫자가 되는 고전 버그. 타입은 **스키마**가 판정 |
| `enable: NO` | `"NO"` (문자열) | YAML 1.1 로더는 `false` ("Norway problem") | bool은 소문자 `true/false` 만 |
| `mask: 0750` | ValueError "ambiguous leading-zero" | 1.1: 8진 488, 1.2 core: 10진 750 | 버전마다 다르면 거부 |
| `seed: 0x5EED` | `24301` | 1.2 core: 16진 정수 | 레지스터 값은 16진이 자연스럽다 |
| `a: &base 1`, `<<: *base` | 거부 | 앵커/병합 | 리뷰어가 펼쳐 보지 않으면 실제 값이 안 보인다 |
| 탭 들여쓰기 | `line N: tab in indentation` | YAML 스펙도 금지 | 에디터마다 칸 수가 달라 구조가 바뀐다 |
| `msg: a: b` | 거부 (`quote strings containing ': '`) | 파싱 에러 | 따옴표를 쓰게 만든다 |

파서는 줄 단위다: `_logical_lines` 가 주석(`따옴표 밖의 ' #'`)과 빈 줄을 지우고 `[줄번호, 칸, 내용]` 을 만든다. `_parse_map` / `_parse_list` 는 "같은 칸이면 내 것, 더 깊으면 자식, 얕으면 부모에게 반환" 규칙 하나로 재귀한다. 테스트한 경계: `tests:` 바로 밑에 **같은 칸**의 `- a: 1` (YAML에서 합법), 따옴표 안의 `#`, 중복 키(4번 줄), 들여쓰기 튐(3번 줄).

**스키마 검증 = 장비 보호.** 파싱이 끝나도 아직 아무 장비 명령도 보내지 않는다. `validate_plan` 이 전부 검사하고 하나라도 틀리면 exit 2. 실제 출력 (`fw` 따옴표 빼기, LV_HOT 1000 mV, `timout_s` 오타를 동시에 넣은 리스트):

```text
$ python3 -I solution.py --run /tmp/SIV-M12_bad.yaml; echo "exit=$?"
CONFIG ERROR E_TYPE run.fw: expected str, got float 1.1 (quote it)
CONFIG ERROR E_LIMIT corners[1] LV_HOT: vdd_mv 1000 not in (0, vdd_mv_max 950]
CONFIG ERROR E_KEY tests[4]: unknown key 'timout_s' (did you mean 'timeout_s'?)
exit=2
$ python3 -I solution.py --run /tmp/SIV-M12_tab.yaml; echo "exit=$?"
CONFIG ERROR yaml line 3: tab in indentation (YAML forbids tabs)
exit=2
```

| 코드 | 잡는 것 | 안 잡으면 밤에 생기는 일 |
|---|---|---|
| `E_KEY` (+did you mean) | `timout_s` 같은 키 오타 | 오타 키는 무시되고 기본 타임아웃 5 s로 돈다 — "설정했는데 왜 안 먹지?" |
| `E_LIMIT` | 코너 전압/온도가 `limits` 밖 | 1.0 V를 0.95 V 정격 코어에 인가. PSU OVP가 두 번째 방어선 (테스트: 1200 mV 거부, 로그 0줄) |
| `E_FUNC` | C 라이브러리에 없는 함수 | 18개 결과가 전부 ERROR — 실리콘 문제처럼 보임 |
| `E_REQUIRES` | 아직 정의되지 않은 테스트를 요구 | 순서 의존 → 첫 코너에선 항상 SKIP. "앞에서 정의된 것만" 규칙이면 순환도 원천 차단 |
| `E_PARAM` | `words: 1.5`, 음수, 32비트 초과 | C 쪽 `strtoul` 이 다르게 읽음 |
| `E_TYPE` | 문자열 자리에 숫자 등 | `fw: 1.10` → DB에 `1.1` 로 기록돼 버전 추적 실패 |

`E_KEY`·`E_TYPE`·`E_LIMIT` 은 starter에 이미 있다 — 키 이름과 타입은 표(`SCHEMA`)로 일괄 검사하고, 의미 검사(`_validate_test`)만 직접 쓴다.

**ctypes 바인딩.** ctypes는 헤더를 읽지 않는다. Python이 C 프로토타입을 **다시 적는** 것이고, 틀려도 대부분 에러 없이 틀린 값을 준다:

| 실수 | 실제 증상 (이 맥, `demo_ctypes_pitfalls.py`) |
|---|---|
| `restype` 안 정함 (`char*` 반환) | `siv_test_name(0) without restype -> 99981682 (int)` — 포인터 하위 32비트 |
| `argtypes` 안 정함 | 형 검사가 없다. 직접 해 보니 `siv_set_defects(1 << 40)` 도, `siv_set_defects(b'x')`(포인터!)도 **조용히 통과**했고 `1.5` 만 `ArgumentError` 가 났다. 구조체 포인터 자리에 엉뚱한 것을 넘겨도 못 막는다 |
| 구조체 필드 하나 틀림 (`msg[64]`) | `sizeof(Short) = 100` vs C 132 → C가 32바이트를 넘쳐 써서 Python 힙 오염 |

그래서 `_bind` 는 로드 직후 두 가지를 확인한다: `siv_api_version() >> 16 == API_MAJOR` (구조체·의미가 바뀌면 C 쪽 major를 올린다), `siv_result_size() == ctypes.sizeof(SivResult)`. 펌웨어의 "헤더 버전 레지스터 확인"(SIV-B10의 ID/VERSION 체크)과 같은 습관이다.

### Part B — 러너 두 개, 그리고 시퀀서가 죽지 않는 법

```svg
<svg viewBox="0 0 760 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">프로세스 경계가 어디 있나 — 크래시·행이 누구를 죽이나</text>
  <rect x="10" y="40" width="350" height="190" rx="10" class="f-mut" stroke="currentColor"/>
  <text x="22" y="62" fill="currentColor" font-weight="bold">시퀀서 프로세스 (python3)</text>
  <rect x="30" y="76" width="150" height="56" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="105" y="98" text-anchor="middle" fill="currentColor">Sequencer</text>
  <text x="105" y="118" text-anchor="middle" fill="currentColor" font-size="12">DB · 장비 · 리포트</text>
  <rect x="200" y="76" width="145" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="272" y="98" text-anchor="middle" fill="currentColor">siv_tests.dylib</text>
  <text x="272" y="118" text-anchor="middle" fill="currentColor" font-size="12">ctypes 로 직접 호출</text>
  <text x="22" y="160" fill="currentColor" font-size="12">SIGSEGV → 프로세스 전체 사망 (returncode -11)</text>
  <text x="22" y="180" fill="currentColor" font-size="12">무한 루프 → join(timeout) 은 감지만, kill 불가</text>
  <text x="22" y="200" fill="currentColor" font-size="12">static 변수가 테스트 사이에 남는다</text>
  <text x="22" y="220" fill="currentColor" font-size="12">대신 1.5 µs/호출, 구조체로 결과</text>
  <rect x="400" y="40" width="350" height="190" rx="10" class="f-ok" stroke="currentColor"/>
  <text x="412" y="62" fill="currentColor" font-weight="bold">테스트마다 새 프로세스 (siv_tests_bin)</text>
  <rect x="420" y="76" width="140" height="56" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="490" y="98" text-anchor="middle" fill="currentColor">subprocess.run</text>
  <text x="490" y="118" text-anchor="middle" fill="currentColor" font-size="12">timeout=0.5 s</text>
  <rect x="590" y="76" width="145" height="56" rx="6" class="f-warn" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="662" y="98" text-anchor="middle" fill="currentColor">자식 프로세스</text>
  <text x="662" y="118" text-anchor="middle" fill="currentColor" font-size="12">JSON 한 줄 + exit</text>
  <text x="412" y="160" fill="currentColor" font-size="12">SIGSEGV → CRASH 한 줄 기록, 시퀀서는 계속</text>
  <text x="412" y="180" fill="currentColor" font-size="12">무한 루프 → TimeoutExpired, 자식 kill</text>
  <text x="412" y="200" fill="currentColor" font-size="12">매번 깨끗한 상태 (리셋된 DUT 모델)</text>
  <text x="412" y="220" fill="currentColor" font-size="12">대신 3.7 ms/호출, 텍스트로 결과</text>
  <path d="M560 104 L588 104" stroke="currentColor" fill="none"/>
  <path d="M180 104 L198 104" stroke="currentColor" fill="none"/>
</svg>
```

데모로 실제 확인한 결과 (`python3 -I demo_ctypes_pitfalls.py`, Apple Silicon / Python 3.9.6, 위 표의 1번 결과도 이 실행에서 나왔다. 호출 비용은 실행마다 몇 % 흔들린다 — 다시 돌렸을 때 1.4 µs / 3551.8 µs):

```text
== 2) crash: ctypes vs binary ==
child: calling dma_desc(desc_count=24) via ctypes ...
child (sequencer) returncode = -11  -> the whole sequencer process died
binary runner -> Outcome(status='CRASH', msg='SIGSEGV')  -> recorded, sequencer continues

== 3) hang: ctypes call cannot be killed ==
child: join(0.3) returned after 0.31 s, thread alive = True
child: main thread still runs (ctypes released the GIL), but nothing can stop the C loop;
child: only process exit ends it (daemon thread) -> os._exit(7)
child returncode = 7

== 4) cost per call (id_check) ==
ctypes   2000 calls,       1.5 us/call
binary    100 calls,    3735.4 us/call
```

`ctypes.CDLL` 함수는 호출 동안 GIL을 놓으므로 다른 스레드는 돌 수 있다. 하지만 Python에는 다른 스레드를 강제로 죽이는 방법이 없고, C 루프는 Python 코드를 실행하지 않으니 예외 주입도 안 먹는다. 그래서 규칙: **행·크래시 위험이 있는 테스트는 `runner: binary`**. 이 리스트에서 `pll.lock`(`max_polls: 0` = 무한 대기)과 `dma.ring`(링 오버런)이 그렇다. 빠른 레지스터 테스트는 ctypes로 2500배 싸게 돈다. (중간안: ctypes 호출을 `multiprocessing` 워커 프로세스 안에서 하고 워커를 죽이는 방식 — Q1.)

`BinaryRunner.run` 의 판정표:

| 관찰 | 결과 | 근거 |
|---|---|---|
| `TimeoutExpired` | TIMEOUT `no result within 0.5s, process killed` | `subprocess.run` 이 자식을 kill하고 wait한다 |
| `returncode < 0` | CRASH `SIGSEGV` | POSIX: 시그널로 죽으면 Python은 `-signum` 을 준다 (셸에선 128+11 = 139) |
| 마지막 줄이 JSON이 아님 | ERROR `no/garbled result line` | 출력이 중간에 잘림, printf 디버그가 섞임 |
| JSON `status` 와 exit code 불일치 | ERROR | 둘 중 하나가 거짓 — 둘 다 믿지 않는다 |
| 그 밖 | JSON의 status/reg/addr/expected/actual | |

**재시도와 FLAKY.** `run_with_retries` 는 FAIL에만 재시도한다. TIMEOUT은 대개 결정적(같은 조건이면 또 행)이라 재시도하면 0.5 s × N을 버리고, CRASH는 보드 상태를 망가뜨렸을 수 있고, ERROR는 설정 문제라 몇 번을 돌려도 같다.

| 시도 결과 | 최종 | 기록 |
|---|---|---|
| PASS | PASS | 1 attempt |
| FAIL, PASS | **FLAKY** | 첫 실패의 reg/addr/exp/act 유지, `passed on attempt 2/3; first: NACK at byte 3` |
| FAIL, FAIL, FAIL | FAIL | 첫 실패 + `(failed 3/3 attempts)` |
| TIMEOUT | TIMEOUT | 재시도 안 함 (테스트: retries 2인데 attempts 1) |

SIV-B14는 재시도된 테스트에서 "가장 나쁜 결과"를 남겼다(sticky fail). 여기선 한 단계 더 나눠 **FLAKY** 라는 별도 상태를 둔다: 게이트(exit code)는 기본적으로 통과시키되(`--flaky-fails` 로 엄격하게 바꿀 수 있음) 리포트·DB·JUnit에서는 절대 사라지지 않는다. 간헐 실패는 마지널 타이밍·전원 노이즈의 첫 신호인 경우가 많고, "재시도하니 됐다"로 묻히면 양산에서 돌아온다.

**결정적 seed.** 테스트별 seed = `run_seed ^ crc32("코너/테스트")`. 같은 run seed면 한 테스트만 다시 돌려도 같은 난수열이다 (연습 1의 재실행 리스트가 같은 FLAKY를 재현한 이유). seed는 결과 행과 리포트에 남긴다.

**장비 mock과 setup/teardown.** 실제 대기는 가짜 시계(`FakeClock`)로 센다 — 챔버 램프 10 °C/min + 소크 300 s. 25 → -40 °C는 65/10 × 60 + 300 = **690 s** (테스트로 확인). 코너 setup 순서는 이 레슨의 선택으로 `OUTP OFF → TEMP → (램프+소크) → VOLT → OUTP ON` 이다. DUT에 넘기는 조건은 설정값이 아니라 **장비가 읽은 값**(`measure_mv`, `measure_c`)이라, setup에서 `OUTP ON` 을 빼먹으면 모든 테스트가 `ERROR DUT not powered (vdd=0 mV)` 로 나온다 — FAIL(실리콘 의심)이 아니라 ERROR(벤치 문제)로 갈리는 것이 핵심이다.

`Sequencer.run` 의 예외 정책 (모두 체크된 테스트로 확인):

| 상황 | 처리 | 이유 |
|---|---|---|
| 러너 안의 Python 버그 (`RuntimeError`) | 그 테스트만 ERROR, 다음 테스트 계속 | 하네스 버그 하나가 밤 전체를 날리지 않게 |
| `AbortRun` (운영자 Ctrl-C, 인터록) | `run.exit_code = 3` 기록 → 다시 던짐 | 즉시 멈춰야 하지만 "멈췄다"는 사실은 남긴다 |
| 무엇이든 | `finally: safe_state()` → 로그 끝이 `OUTP OFF`, `TEMP 25.0` | -40 °C·전원 ON 상태로 주말을 보내지 않게 |
| `requires` 미충족 | SKIP `requires reg.id = FAIL` | 원인 하나에 실패 열 줄이 붙는 연쇄를 끊는다 |

`safe_state` 자체도 한 단계가 예외를 내도 다음 단계를 시도한다 (PSU 통신이 끊겨도 챔버는 상온으로).

### Part C — 불량 샘플의 밤

`python3 -I solution.py --run testlist.yaml --defects 0xF --started 2026-11-08T02:00:00Z` 실제 출력:

```text
== SIV-M12 sequencer report: run 1 'nightly-a0-1108' ==
board=EVB-A0 sn0042  fw=siv-0.4.10  seed=0x5EE9  defects=0xF  started=2026-11-08T02:00:00Z
test          NOM_25C   LV_HOT    NOM_COLD  
reg.id        PASS      PASS      PASS      
reg.reset     PASS      PASS      PASS      
sram.march    PASS      FAIL      PASS      
i2c.loopback  FLAKY     PASS      PASS      
pll.lock      PASS      PASS      TIMEOUT   
dma.ring      CRASH     CRASH     SKIP      
counts: PASS=12 FLAKY=1 FAIL=1 TIMEOUT=1 CRASH=2 ERROR=0 SKIP=1 (total 18)
-- failures (first failure per test/corner; rerun with its seed) --
CRASH    dma.ring      @NOM_25C   att=1 seed=0x80669263 | SIGSEGV
FAIL     sram.march    @LV_HOT    att=1 seed=0x3235E1C1 reg=SRAM addr=0x20000A9C exp=0xFFFFFFFF act=0xFFFFDFFF diff=0x00002000 | march M2 r1
CRASH    dma.ring      @LV_HOT    att=1 seed=0x9A874E0B | SIGSEGV
TIMEOUT  pll.lock      @NOM_COLD  att=1 seed=0x21C2CCDF | no result within 0.5s, process killed
-- flaky (passed on retry - track it, do not forget it) --
FLAKY    i2c.loopback  @NOM_25C   att=2 seed=0xCBE19F81 reg=I2C_STATUS addr=0x40003004 exp=0x00000000 act=0x00000004 diff=0x00000004 | passed on attempt 2/3; first: NACK at byte 3
-- skipped --
SKIP     dma.ring      @NOM_COLD  att=0 seed=0xCB89851C | requires pll.lock = TIMEOUT
bench: 2640 s simulated, 4 chamber setpoints, last PSU cmd 'OUTP OFF'
RESULT: FAIL (exit 1)
```

디버그 엔지니어가 이 리포트에서 바로 읽는 것:

| 줄 | 읽는 법 | 다음 행동 |
|---|---|---|
| `sram.march` LV_HOT만 FAIL, `diff=0x00002000` | 한 워드·한 비트, 저전압+고온에서만 → 약한 셀 / Vmin 마진 | 그 주소로 shmoo (SIV-M07), 다른 샘플에서도 같은 주소인지 (SIV-M10 triage) |
| `pll.lock` NOM_COLD TIMEOUT | 저온에서 lock 안 됨 + 테스트가 무한 대기 설정 | `max_polls` 를 유한하게 바꿔 FAIL로 정확히 잡히게, 아날로그 팀에 저온 lock 보고 |
| `dma.ring` CRASH ×2, COLD는 SKIP | 온도 무관 — 기능 버그. COLD는 PLL 때문에 못 돌았을 뿐 | `desc_count` 를 16/17로 bisect (SIV-M11), DMA 링 wrap 로직 |
| `i2c.loopback` FLAKY @NOM_25C | 1/2 실패, seed 기록됨 | 그 seed로 반복 실행해 실패율 측정 (Q2의 3/n 규칙) |
| bench 2640 s | 25→105: 780 s, 105→-40: 1170 s, 복귀 690 s | 테스트 시간보다 **챔버 시간**이 지배 → 코너 순서 최적화는 SIV-M08 |

결과 DB 스키마 (주어진 `DB_SCHEMA`):

| 테이블 | 한 행 | 쓰는 곳 |
|---|---|---|
| `run` | 밤 한 번: name, board, fw, seed, defects, started, exit_code, bench_s | 대시보드, "어제 대비" (연습 2) |
| `result` | (run, corner, test): 최종 status, attempts, seed, 첫 실패 정보 | 리포트, 매트릭스 |
| `attempt` | 시도 하나하나 | FLAKY 근거, 실패율 추정 |
| `instr` | 장비 명령 하나하나 (가짜 시각) | "그때 전압이 정말 0.72 V였나?" 감사 기록 |

결과마다 `with self.con:` 으로 commit한다. 시퀀서가 새벽 3시에 죽어도 그때까지의 결과와 run 행이 남고, `exit_code IS NULL` 인 run은 "끝나지 않은 밤"으로 바로 보인다. 테스트는 DB 파일을 닫고 **새 연결로 다시 열어** run 1 = exit 1, run 2 = exit 0, bench 2640.15 s를 확인한다.

종료 코드 규칙: 결과 0개 → 2 (아무것도 안 돈 밤은 PASS가 아니다), FAIL/TIMEOUT/CRASH/ERROR가 하나라도 → 1, 아니면 0. FLAKY만 있는 밤(I2C 결함만 있는 샘플 `defects=0x2`)은 기본 0, `flaky_fails=True` 면 1 — 둘 다 체크된다. 골든 샘플은 `PASS=18 ... RESULT: PASS (exit 0)` 이다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py; echo "exit=$?"                     # 58/58 passed, exit=0 (이 맥에서 real 1.85 s)
python3 -I -W error -X dev solution.py > /dev/null; echo $?  # 경고 0개, 0
python3 -I solution.py --run testlist.yaml --defects 0xF     # 위 리포트, exit 1
python3 -I solution.py --run testlist.yaml --defects 0       # 골든: 18 PASS, exit 0
python3 -I demo_ctypes_pitfalls.py                           # restype / 크래시 / 행 / 호출 비용 데모
```

C 라이브러리 단독 strict 빌드도 확인했다 (경고 0, 둘 다 성공):

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 -dynamiclib siv_tests.c -o /tmp/SIV-M12_siv_tests.dylib
cc -std=c11 -Wall -Wextra -Werror -O2 siv_tests.c -o /tmp/SIV-M12_bin && /tmp/SIV-M12_bin   # 인자 없음 = 자체 점검
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer siv_tests.c -o /tmp/SIV-M12_san
/tmp/SIV-M12_san --run sram_march --vdd 720 --temp 105 --defects 15   # FAIL JSON, exit 1, 새니타이저 출력 없음
```

인자 없이 실행한 자체 점검(골든 조건, 파라미터 기본값)의 실제 출력 — ASan/UBSan 빌드도 같은 출력에 exit 0:

```text
PASS  id_check      checks=1 
PASS  reg_reset     checks=4 
PASS  sram_march    checks=5120 
PASS  i2c_loopback  checks=8 
PASS  pll_lock      checks=1 
PASS  dma_desc      checks=1 
6/6 passed
```

`--run` 경로도 새니타이저 빌드로 sram(FAIL)·i2c seed 7(FAIL)·dma 24개 골든(PASS)·pll max_polls=100 저온 결함(FAIL `no LOCK after 100 polls`)·잘못된 파라미터(ERROR `bad param token 'bogus'`, exit 2)를 돌려 stderr가 비어 있음을 확인했다. (dma 결함·pll 무한 대기는 일부러 죽거나 멈추는 경로라 새니타이저 대상에서 뺐다.)

<!-- code: demo_ctypes_pitfalls.py -->

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 재시도 후 PASS를 그냥 PASS로 기록 | 간헐 실패가 사라진다. 다음 스테핑에서 양산 불량으로 돌아온다 | `status="FLAKY"` + 첫 실패 정보 보존, attempt 테이블 |
| TIMEOUT·CRASH도 재시도 | 행은 결정적이라 시간만 N배, 크래시는 보드 상태 오염 | `if o.status != "FAIL": break` |
| 행 위험 테스트를 ctypes로 | 시퀀서 전체가 멈춤, 장비가 -40 °C에 방치 | `runner: binary` + `timeout_s` |
| `restype` / `argtypes` 생략 | `char*` 가 int로 잘림, 구조체 포인터 오용을 못 막음 | 모든 함수에 프로토타입, 로드 시 `sizeof` 비교 |
| 장비 setup 후에 리스트 검증 | 30분 램프 뒤에 오타 발견, 또는 잘못된 전압이 이미 인가됨 | `parse → validate → (오류면 exit 2)` 후에야 `Bench` 사용 |
| teardown을 루프 끝에만 둠 | 예외·중단 시 전원 ON·저온 방치 (결로, DUT 손상) | `try/finally: bench.safe_state()` |
| `requires` 를 run 전체 기준으로 판단 | 25 °C에서 통과했다고 -40 °C에서도 PLL이 lock된 건 아니다 | `here = {}` 를 **코너마다** 새로 |
| 설정값을 DUT 조건으로 기록 | PSU OFF인데 "0.8 V에서 FAIL"로 기록 | `env()` 는 `measure_mv()`·`measure_c()` |
| 결과를 run 끝에 한 번에 저장 | 새벽에 죽으면 밤 전체가 사라짐 | 결과마다 commit, run 행은 시작 때 |
| 빈 결과를 exit 0 | 필터 오타로 0개 실행 → 초록불 | `total == 0 → exit 2` |
| `fw: 1.10` 따옴표 없이 | float 1.1로 DB에 기록, 버전 추적 실패 | 스키마 `str` + `E_TYPE ... (quote it)` |
| 바이너리의 status만 믿음 | 출력 잘림·printf 혼입·잘못된 exit를 못 봄 | JSON status와 exit code 교차 검증 |

## 8. 면접 Q&A

<details>
<summary>Q1. C 테스트를 Python에서 부르는 방법(ctypes, subprocess, cffi/pybind11)은 어떻게 고르나요?</summary>

- **ctypes / cffi(ABI 모드)**: 빌드 없이 공유 라이브러리를 바로 부른다. 호출 비용이 µs 단위(이 맥 1.5 µs)라 레지스터 R/W 수천 번 같은 미세 테스트에 좋다. 단점: 같은 프로세스 — 크래시가 시퀀서를 죽이고(-11), 행을 죽일 수 없고, C의 static 상태가 테스트 사이에 남는다. 프로토타입을 손으로 다시 적어야 한다.
- **subprocess(테스트 바이너리)**: 프로세스 하나 생성에 ms 단위(3.7 ms)지만 타임아웃·크래시 격리·깨끗한 상태가 공짜다. 보드 위 펌웨어 테스트를 JTAG/UART로 돌리는 경우도 시퀀서 입장에선 이 모델이다(외부 프로세스 + 텍스트 결과 + 타임아웃).
- **pybind11 / C 확장**: 타입 안전하고 빠르지만 빌드 체인이 필요하다 — 장기 유지되는 API에 적합.
꼬리질문: "ctypes를 쓰면서도 타임아웃을 걸고 싶다면?" → 워커 프로세스(`multiprocessing`) 안에서 ctypes 호출을 하고, 부모가 시간 초과 시 워커를 kill 후 재생성. 스레드 `join(timeout)` 은 감지만 한다 (데모 3: `thread alive = True`).

</details>

<details>
<summary>Q2. 재시도는 버그를 숨기지 않나요? 간헐 실패는 어떻게 다루나요?</summary>

재시도 자체가 아니라 **재시도 결과를 PASS로 기록하는 것**이 숨기는 것이다. 이 시퀀서는 FAIL→PASS를 FLAKY로 남기고 첫 실패의 주소·값·seed를 보존한다. 게이트 정책(FLAKY를 실패로 볼지)은 단계마다 다르다: 브링업 초기엔 경고, 양산 테스트 프로그램 확정 단계에선 실패로.
꼬리질문: "몇 번 연속 통과하면 '고쳐졌다'고 할 수 있나?" → 실패 확률이 p일 때 n번 연속 통과 확률은 (1-p)ⁿ. 신뢰수준 95%로 p < p₀를 주장하려면 (1-p₀)ⁿ ≤ 0.05 → n ≥ ln(0.05)/ln(1-p₀) ≈ 3/p₀ ("rule of three"). p₀ = 1%면 약 299회. 40%로 실패하던 이 I2C 결함을 "고쳤다"고 하려면 2~3번 통과로는 부족하다. 간헐 실패의 커밋 bisect는 SIV-M11 참고.

</details>

<details>
<summary>Q3. 시퀀서가 밤중에 죽으면 장비는 어떻게 되나요?</summary>

1차: `finally` 의 `safe_state()` (전원 OFF → 상온). 하지만 `kill -9`, 정전, OS 패닉에선 `finally` 가 안 돈다. 그래서 2차는 장비 자체: PSU의 OVP/OCP, 챔버의 자체 상·하한 알람, 그리고 별도 감시 프로세스(watchdog)가 heartbeat가 끊기면 장비를 안전 상태로 돌린다. 리스트 검증의 `limits` 는 0차 방어선이다.
꼬리질문: "-40 °C에서 바로 상온으로 올리고 문을 열면?" → 차가운 DUT·보드에 결로가 생길 수 있다. 랩마다 건조 공기/질소 퍼지, 상온 도달 후 일정 시간 대기 같은 절차가 있다 — 시퀀서 teardown에 그 대기를 넣는다.

</details>

<details>
<summary>Q4. 왜 YAML(또는 JSON) 테스트 리스트이고, Python 코드로 쓰면 안 되나요?</summary>

데이터 리스트는 **검토·diff·lint가 가능**하다: "어젯밤과 무엇이 달라졌나"가 리스트 diff로 보이고, 장비를 켜기 전에 스키마로 거부할 수 있다. Python 코드 리스트는 유연하지만(루프로 코너 생성 등) 실행해 보기 전엔 무엇이 돌지 알기 어렵다. 실무는 혼합이 많다: 리스트는 데이터, 복잡한 시퀀스는 코드로 된 "테스트 스텝" 을 이름으로 참조.
꼬리질문: "YAML의 함정은?" → 암묵 타입 변환: YAML 1.1에서 `NO`/`off` → false, `1.10` → 1.1, `0750` → 8진(1.2 core에선 10진). 그래서 스키마가 타입을 강제하고, 버전·ID 문자열은 항상 따옴표. PyYAML을 쓴다면 반드시 `safe_load` (임의 객체 생성 금지).

</details>

<details>
<summary>Q5. 같은 실패를 다시 재현하려면 무엇을 기록해야 하나요?</summary>

seed(테스트별 파생 seed), 테스트 라이브러리 버전(API 버전 + git SHA), FW 버전, 보드 시리얼, 칩 ID/스테핑, 코너의 **측정값**(설정값이 아니라), 장비 명령 로그, 파라미터, 시도 번호. 이 시퀀서는 seed를 `run_seed ^ crc32("코너/테스트")` 로 파생해 테스트 하나만 다시 돌려도 같은 난수열이 나오게 했다 (연습 1의 재실행이 같은 FLAKY를 재현).
꼬리질문: "seed가 같은데 재현이 안 되면?" → 물리 조건(온도 드리프트, 전원 노이즈, 칩 자체 가열), 순서 의존(앞 테스트가 남긴 상태 — ctypes에선 static 변수까지), 다른 샘플. 단독 실행 vs 스위트 안 실행을 비교해 순서 의존부터 가른다.

</details>

<details>
<summary>Q6. 결과 DB로 SQLite를 쓴 이유와 한계는?</summary>

파일 하나, 서버 불필요, 표준 라이브러리, SQL로 "어제 대비 새 실패", "코너별 실패율" 같은 질문을 바로 던질 수 있다. 결과마다 commit하면 크래시에도 부분 결과가 남는다.
꼬리질문: "벤치 20대가 같은 DB 파일에 쓰면?" → SQLite는 동시에 쓰는 사람이 한 명(DB 단위 쓰기 잠금)이다. WAL 모드가 읽기와 쓰기의 동시성을 개선하지만, 네트워크 파일시스템 위의 SQLite는 잠금이 믿을 만하지 않아 권장되지 않는다. 그래서 벤치마다 로컬 파일 → 중앙 서버 DB(Postgres 등)로 업로드하거나, 처음부터 서버 DB에 쓴다.

</details>

<details>
<summary>Q7. 타임아웃 값은 어떻게 정하고, 타임아웃 시 무엇을 남기나요?</summary>

테스트별로, 정상 실행 시간 분포의 상한(예: 관측 최대값의 몇 배)으로 정하고 코너별 차이(저온에서 느린 PLL lock)를 반영한다. 너무 짧으면 느린 정상 칩을 TIMEOUT으로, 너무 길면 행 하나가 밤을 잡아먹는다. 테스트 안의 poll-with-timeout(SIV-B09)이 1차, 시퀀서 타임아웃은 최후의 방어선이다 — 이 레슨의 `max_polls: 0` 이 1차가 꺼진 사례다.
꼬리질문: "자식이 또 자식(openocd, gdb)을 띄우면?" → `subprocess.run` 의 kill은 직계 자식만 죽인다. `start_new_session=True` 로 새 프로세스 그룹을 만들고 `os.killpg` 로 그룹 전체를 죽인다. 손자가 stdout 파이프를 잡고 있으면 `communicate()` 가 끝나지 않을 수도 있어 출력 수집도 그룹 kill 뒤에 한다. 타임아웃 레코드에는 그때까지의 부분 출력, 마지막 장비 상태, 걸린 시간을 남긴다.

</details>

## 9. 추가 연습

### 연습 1 — 실패만 다시 돌리는 리스트 생성

`rerun_yaml(db, run_id, plan)`: FAIL/FLAKY/TIMEOUT/CRASH/ERROR 였던 (테스트, 코너)와 그 `requires` 를 **전이적으로** 모아, 필요한 코너만 담은 YAML 테스트 리스트를 만든다. 생성 결과가 본문의 파서·검증을 통과해야 한다.

<details>
<summary>답 보기</summary>

스택으로 (test, corner)를 펼치며 `requires` 를 같은 코너로 추가한다. 테스트는 **원래 순서대로** 내보내면 `requires` 가 항상 앞에 있어 `E_REQUIRES` 가 안 난다. 실제 결과: 테스트는 `reg.id, sram.march, i2c.loopback, pll.lock, dma.ring`, `sram.march` 는 `corners: [LV_HOT]`, `dma.ring` 은 `[NOM_25C, LV_HOT]` (SKIP 코너 제외), `pll.lock` 은 자기 TIMEOUT(COLD) + `dma.ring` 의 의존(25C, LV_HOT)으로 세 코너 전부. 같은 샘플로 다시 돌리면 `FAIL=1 TIMEOUT=1 CRASH=2` 가 그대로 재현된다 — seed가 결정적이기 때문. 정수 파라미터는 255 초과면 16진으로 써서(`words: 0x400`) 사람이 읽기 쉽게 했다.

</details>

### 연습 2 — 두 밤 비교

`compare_runs(db, base, new)` → `NEW_FAIL / FIXED / STILL_BAD / NEW_FLAKY / NOT_RUN`. 골든 → 불량 샘플이면 NEW_FAIL 4개(dma 2, pll COLD, sram LV_HOT), NEW_FLAKY 1개, NOT_RUN 1개(dma COLD)여야 한다.

<details>
<summary>답 보기</summary>

두 run의 (test, corner) → status 사전을 만들고 합집합을 정렬해 분류한다. NOT_RUN을 따로 두는 이유: "dma.ring @NOM_COLD가 고쳐졌나?"에 대한 답은 "모른다(안 돌았다)"이지 PASS가 아니다. 반대 방향 비교는 FIXED 4개, NEW_FAIL 0개. 실무에서 아침 리포트 맨 위 줄은 보통 이 "어제 대비 새 실패" 목록이다.

</details>

### 연습 3 — JUnit XML 내보내기

`to_junit(db, run_id)`: 코너 = `<testsuite>`, 테스트 = `<testcase>`. FAIL → `<failure>`, TIMEOUT/CRASH/ERROR → `<error>`, SKIP → `<skipped>`, FLAKY → 통과지만 `<system-out>` 에 첫 실패를 남긴다.

<details>
<summary>답 보기</summary>

`xml.etree.ElementTree` 로 만들고 suite마다 `tests/failures/errors/skipped` 개수를 단다. 실제 결과 일부: `<testsuite name="LV_HOT" tests="6" failures="1" errors="1" skipped="0">`, FLAKY 케이스는 `<system-out>FLAKY after 2 attempts: passed on attempt 2/3; first: NACK at byte 3 reg=I2C_STATUS addr=0x40003004 ...`. JUnit 형식은 Jenkins·GitLab CI가 그대로 읽어 테스트 추이 그래프를 그려 준다. FAIL과 ERROR를 나누는 건 "실리콘/테스트 실패" 대 "실행 자체가 깨짐"의 구분과 같다.

</details>

### 연습 4 — `requires` × `corners` lint

테스트 T가 코너 K에서 돌고, T의 `requires` R이 K에서 돌지 않으면 T@K는 **항상** SKIP이다. 이걸 리스트 단계에서 `E_REQCORNER` 로 잡는다.

<details>
<summary>답 보기</summary>

테스트마다 실행 코너 집합(`corners` 가 비면 전체)을 만들고 `T의 코너 - R의 코너` 가 비었는지 본다. `reg.id` 를 `[NOM_25C]` 로, `sram.march` 를 `[NOM_25C, LV_HOT]` 로 두면 `E_REQCORNER sram.march requires reg.id, which does not run at LV_HOT`. 이런 리스트는 매일 밤 "SKIP 1개"를 만들고 아무도 보지 않는다 — 실제 커버리지 구멍이다 (커버리지 정량화는 SIV-M13).

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py; echo "exit=$?"     # 15/15 passed, exit=0
```

## 10. 핵심 정리

- C 테스트 = **무엇을** 검사, 시퀀서 = **언제·어떤 조건·몇 번·얼마나 기다리고·어디에 기록**. 테스트 리스트는 데이터(YAML)로 두어 diff·lint·검토가 되게.
- YAML은 쓰는 부분만 지원하고 나머지는 **줄 번호와 함께 거부**. 타입은 스키마가 정한다: `fw: "1.10"`(따옴표), `NO` ≠ false, `0750` 거부.
- 검증 → (통과 시에만) 장비. 설정 오류는 exit 2, 장비 명령 0줄. 한계(`limits`) → PSU OVP → 장비 자체 알람 순으로 방어.
- ctypes = 1.5 µs, 같은 프로세스(크래시·행에 무방비, `restype`·`sizeof` 확인 필수). 바이너리 = 3.7 ms, 타임아웃·크래시 격리. 위험한 테스트는 바이너리.
- 재시도는 FAIL에만. FAIL→PASS는 **FLAKY**로 남긴다. TIMEOUT/CRASH/ERROR는 재시도 안 함.
- 사전조건은 코너별, 미충족이면 SKIP + 이유. `try/finally` 로 무조건 `safe_state()` (전원 OFF, 상온).
- 결과는 시도 단위로 SQLite에 즉시 commit, 리포트는 코너×테스트 매트릭스 + `reg addr exp act diff seed` + 종료 코드 0/1/2.

## 참고자료

- [Python `ctypes` 문서](https://docs.python.org/3/library/ctypes.html) — argtypes/restype, Structure, GIL 해제(`CDLL` vs `PyDLL`)
- [Python `subprocess` 문서](https://docs.python.org/3/library/subprocess.html) — `run(timeout=)`, `TimeoutExpired`, 음수 `returncode`, `start_new_session`
- [Python `sqlite3` 문서](https://docs.python.org/3/library/sqlite3.html) · [SQLite: Appropriate Uses / WAL](https://www.sqlite.org/whentouse.html)
- [YAML 1.2.2 명세](https://yaml.org/spec/1.2.2/) — core schema(정수·bool 규칙), 탭 들여쓰기 금지
- [OpenHTF (Google, 오픈소스 하드웨어 테스트 프레임워크)](https://github.com/google/openhtf) · NI TestStand(상용 테스트 시퀀서)
- [PyVISA](https://pyvisa.readthedocs.io/) — 계측기 제어, SCPI 명령(`VOLT`, `OUTP`)은 IVI Foundation의 SCPI 표준
- [JUnit XML 형식 (Jenkins/GitLab 테스트 리포트)](https://docs.gitlab.com/ee/ci/testing/unit_test_reports.html)
- P. Mishra, F. Farahmandi (eds.), *Post-Silicon Validation and Debug*, Springer 2019
- 관련 레슨: SIV-B08(TEST 프레임워크), SIV-B09(poll-with-timeout), SIV-B14(로그 → 리포트), SIV-M07(shmoo/Vmin), SIV-M08(PVT 스케줄), SIV-M10(triage), SIV-M11(bisect), SIV-M13(coverage)
