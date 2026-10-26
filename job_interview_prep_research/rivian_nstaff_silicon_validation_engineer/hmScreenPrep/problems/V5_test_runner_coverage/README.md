# V5 · Bare-metal Test Framework: Registry, Timeouts and Coverage Report

> **ID**: V5 · **Priority**: P1 · **Pattern**: table-driven registry + filter + wrap-safe timing + bounded snprintf report · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh V5` / `./run.sh V5 sol`

## 1. Problem

The SoC validation firmware has a static table of test cases. Each test is tagged with the **block** it exercises and the **requirement** it verifies. The same image runs on the FPGA prototype and on silicon. A host-side Python script parses the UART log and feeds a results database and a coverage report.

```c
uint32_t tf_now_ms(void);                        /* HAL: free-running ms clock, wraps */

typedef enum { PLAT_FPGA = 0, PLAT_SILICON = 1 } Platform;
#define TF_SKIP_ON_FPGA  (1u << 0)
#define TF_DESTRUCTIVE   (1u << 1)

typedef struct {
    const char *id;  const char *block;  const char *req_id;
    int (*fn)(void *ctx);   /* 0 = pass, negative errno = fail */
    uint32_t timeout_ms;    /* 0 = no limit */
    uint32_t flags;
} TestCase;
typedef struct { const char *req_id; const char *block; } Requirement;
typedef struct { Platform plat; const char *block_filter; bool allow_destructive; void *ctx; } RunCfg;
typedef enum { ST_PASS = 0, ST_FAIL, ST_SKIP, ST_TIMEOUT } Status;
typedef struct { const TestCase *tc; Status st; int rc; uint32_t ms; } TestResult;
const char *tf_status_name(Status s);            /* provided: "PASS" ... */

int    tf_run(const TestCase *tests, size_t n, const RunCfg *cfg, TestResult *res, size_t res_cap);
size_t tf_report(const TestResult *res, size_t nres, const Requirement *reqs, size_t nreq,
                 char *buf, size_t cap);
```

**`tf_run`**
- A test is *selected* if `cfg->block_filter == NULL` or its `block` equals the filter. Unselected tests are not recorded at all.
- If the number of selected tests is larger than `res_cap`, run **nothing** and return `-ENOSPC`.
- A selected test is **SKIP** (`rc = 0`, `ms = 0`, `fn` not called) if `plat == PLAT_FPGA && (flags & TF_SKIP_ON_FPGA)`, or if `(flags & TF_DESTRUCTIVE) && !allow_destructive`.
- Otherwise: `start = tf_now_ms(); rc = fn(cfg->ctx); ms = tf_now_ms() - start;` (wrap-safe). If `timeout_ms != 0 && ms > timeout_ms` → **TIMEOUT** (keep `rc`), else `rc == 0` → **PASS**, otherwise **FAIL**.
- Results are in table order. Return the number of results.

**`tf_report`** writes text into `buf` (at most `cap` bytes, always NUL-terminated when `cap > 0`) and returns the **full length** it needed, excluding the NUL, like `snprintf`. `buf` may be NULL when `cap == 0`. Every line ends with `\n`:

```
RESULT,<id>,<status>,<rc>,<ms>                  one per result, in order
SUMMARY,total=<n>,pass=<p>,fail=<f>,skip=<s>,timeout=<t>
COVERAGE,<block>,<covered>/<total>,<pct>%       one per block, in first-appearance order of reqs
COVERAGE,ALL,<covered>/<total>,<pct>%
UNCOVERED,<req_id>                              one per uncovered requirement, in reqs order
```

A requirement is **covered** if at least one result with the same `req_id` has status PASS. `pct = covered * 100 / total` rounded down (`0` when `total == 0`). No `malloc`.

### Example

```
tests (id, block, req, fn behaviour, timeout_ms, flags):
  PCIE-LINK-001   PCIE  REQ-PCIE-001  12 ms, rc 0    100
  PCIE-LINK-002   PCIE  REQ-PCIE-002   3 ms, rc -5   100
  PCIE-GEN4-003   PCIE  REQ-PCIE-003  12 ms, rc 0    100  SKIP_ON_FPGA
  DDR-TRAIN-001   DDR   REQ-DDR-001   40 ms, rc 0     40
  DDR-STRESS-002  DDR   REQ-DDR-002  600 ms, rc 0    500
  OTP-BURN-001    OTP   REQ-OTP-001   12 ms, rc 0    100  DESTRUCTIVE
requirements: REQ-PCIE-001..003, REQ-DDR-001..002, REQ-OTP-001, REQ-ISP-001 (block ISP)

cfg = { FPGA, no filter, destructive not allowed }, clock starts at 0xFFFFFFF8
tf_run -> 6
tf_report ->
RESULT,PCIE-LINK-001,PASS,0,12
RESULT,PCIE-LINK-002,FAIL,-5,3
RESULT,PCIE-GEN4-003,SKIP,0,0
RESULT,DDR-TRAIN-001,PASS,0,40
RESULT,DDR-STRESS-002,TIMEOUT,0,600
RESULT,OTP-BURN-001,SKIP,0,0
SUMMARY,total=6,pass=2,fail=1,skip=2,timeout=1
COVERAGE,PCIE,1/3,33%
COVERAGE,DDR,1/2,50%
COVERAGE,OTP,0/1,0%
COVERAGE,ISP,0/1,0%
COVERAGE,ALL,2/7,28%
UNCOVERED,REQ-PCIE-002
UNCOVERED,REQ-PCIE-003
UNCOVERED,REQ-DDR-002
UNCOVERED,REQ-OTP-001
UNCOVERED,REQ-ISP-001
(returns 451)
```

### Constraints

- C11, single core, no OS. `tf_now_ms()` is `uint32_t` and wraps.
- Strings in the table are static and NUL-terminated. Up to a few hundred tests and requirements (O(n·m) matching is fine).
- The test function returns by itself in this version (no real hang). The timeout is detected after it returns.

### Clarifying questions to ask (first 3 minutes)

1. If a test hangs forever, is post-hoc timing enough, or do you expect a watchdog to recover?
2. Is a test that passes but takes longer than its timeout a PASS or a TIMEOUT?
3. Should filtered-out tests appear in the report as SKIP, or not at all?
4. Does a requirement need *all* of its tests to pass, or *at least one*?
5. What if the report does not fit in the buffer — truncate, or stream line by line to the UART?
6. Is the log format fixed by an existing host parser? Any escaping needed (commas in ids)?

## 2. Your workspace

`./run.sh V5`로 바로 테스트하세요. 테스트 6묶음(27개 CHECK)이 있고, 파일 아래쪽 fake clock / fake 테스트 함수 / 테이블은 건드리지 않습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 판정 순서를 먼저 고정</summary>

선택(filter) → SKIP 조건 → 실행 → TIMEOUT → PASS/FAIL 순서입니다. TIMEOUT 검사가 `rc` 검사보다 **먼저**여야 "늦게 끝났지만 rc = 0"인 테스트(DDR-STRESS-002)가 PASS로 잡히지 않습니다. 경계는 `ms > timeout_ms`라서 정확히 40 ms인 DDR-TRAIN-001은 PASS입니다.

</details>

<details>
<summary>Hint 2 — snprintf 누적 헬퍼 하나</summary>

`put(buf, cap, &len, fmt, ...)`를 만듭니다. 안에서 `len < cap`이면 `buf + len`, 남은 공간 `cap - len`으로 `vsnprintf`를 부르고, 아니면 `vsnprintf(NULL, 0, ...)`로 길이만 셉니다. 그리고 **반환값을 항상 `len`에 더합니다**. 그러면 잘려도 마지막에 `len`이 필요한 전체 길이가 되고, `vsnprintf`가 NUL을 붙여 줍니다.

</details>

<details>
<summary>Hint 3 — 블록별 커버리지를 malloc 없이</summary>

블록 목록을 따로 만들지 않아도 됩니다. `reqs[i]`의 block이 `reqs[0..i-1]`에 처음 나오는 것일 때만, `j >= i`를 다시 훑어서 같은 block의 total/covered를 셉니다. O(m²)이지만 m은 수백 개라 문제없습니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 파이프라인

타깃 쪽은 "테이블 → 고르기 → 돌리기 → 결과 배열 → 텍스트" 다섯 단계이고, 그 텍스트를 호스트 Python이 받아 DB와 traceability 도구로 넘깁니다. 펌웨어는 **판정과 한 줄 기록**까지만 책임지고, 집계·이력·그래프는 호스트가 합니다.

<div class="chart" data-src="chart1.svg" data-caption="테스트 흐름. 타깃은 판정과 기계가 읽는 RESULT/COVERAGE 줄까지, 이력·flaky 분석·traceability는 호스트 Python 담당"></div>

### 4.2 커버리지 매트릭스

<div class="chart" data-src="chart2.svg" data-caption="requirement × test 매트릭스 (FPGA 실행). PASS 셀이 있는 행만 covered. REQ-ISP-001은 테스트 자체가 없어서 0%"></div>

커버리지가 "테스트를 돌렸는가"가 아니라 **"PASS 증거가 있는가"** 라는 점이 핵심입니다. SKIP, FAIL, TIMEOUT은 모두 증거가 아닙니다. 그래서 같은 테이블이라도 FPGA에서는 2/7, 실리콘에서는 4/7이 나옵니다. 필터를 걸면 다른 블록의 requirement는 이번 실행 기준으로 uncovered로 나옵니다(테스트 3번: PCIE 0/3) — 리포트는 "이 실행"의 증거만 말합니다.

### 4.3 손으로 따라가기 (trace)

테스트 1번 (FPGA, 시계 시작 0xFFFFFFF8):

| 테스트 | 선택 | SKIP? | start | end | ms = end − start | timeout | 판정 |
|---|---|---|---|---|---|---|---|
| PCIE-LINK-001 | ✓ | – | 0xFFFFFFF8 | 0x00000004 | **12** (wrap) | 100 | PASS |
| PCIE-LINK-002 | ✓ | – | 4 | 7 | 3 | 100 | FAIL (rc −5) |
| PCIE-GEN4-003 | ✓ | SKIP_ON_FPGA | – | – | 0 | – | SKIP (fn 호출 안 함) |
| DDR-TRAIN-001 | ✓ | – | 7 | 47 | 40 | 40 | 40 > 40 거짓 → **PASS** |
| DDR-STRESS-002 | ✓ | – | 47 | 647 | 600 | 500 | **TIMEOUT** (rc 0 유지) |
| OTP-BURN-001 | ✓ | DESTRUCTIVE | – | – | 0 | – | SKIP |

`fn` 호출 4번 → `g_calls == 4`. 카운트: pass 2, fail 1, skip 2, timeout 1.

커버리지 계산 (reqs 순서대로):

| i | req | block 첫 등장? | covered? | 출력 |
|---|---|---|---|---|
| 0 | REQ-PCIE-001 | ✓ PCIE | ✓ (LINK-001 PASS) | `COVERAGE,PCIE,1/3,33%` (100/3 = 33) |
| 1 | REQ-PCIE-002 | – | ✗ (FAIL) | – |
| 2 | REQ-PCIE-003 | – | ✗ (SKIP) | – |
| 3 | REQ-DDR-001 | ✓ DDR | ✓ | `COVERAGE,DDR,1/2,50%` |
| 4 | REQ-DDR-002 | – | ✗ (TIMEOUT) | – |
| 5 | REQ-OTP-001 | ✓ OTP | ✗ (SKIP) | `COVERAGE,OTP,0/1,0%` |
| 6 | REQ-ISP-001 | ✓ ISP | ✗ (테스트 없음) | `COVERAGE,ISP,0/1,0%` |

ALL = 2/7 → 200/7 = 28.57 → **28%**. 실리콘 + destructive 허용(테스트 2번)이면 GEN4와 OTP가 PASS가 되어 PCIE 2/3 = **66%**, ALL 4/7 = **57%**. DDR만 필터(테스트 3번)면 ALL 1/7 = **14%**.

버퍼가 작을 때 (테스트 5번, `cap = 40`): 첫 줄 `RESULT,PCIE-LINK-001,PASS,0,12\n`이 31바이트, 두 번째 줄은 남은 9바이트 중 8바이트 `RESULT,P` + NUL만 들어갑니다. 그 뒤 `put`은 쓸 공간이 없어 길이만 셉니다. 반환값은 전체 17줄 = **451**이고, 호출자는 `451 + 1` 바이트로 다시 부르면 됩니다.

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `tf_run`은 먼저 선택된 개수를 세고 `-ENOSPC`를 판단한 다음에 실행합니다. 결과 슬롯이 모자라서 중간에 멈추면 destructive 테스트가 "돌았지만 기록이 없는" 상태가 되기 때문입니다.
- `*r = (TestResult){ t, ST_SKIP, 0, 0 };`로 먼저 SKIP 기본값을 채우고, 실행할 때만 덮어씁니다. 그래서 SKIP 경로는 `continue` 한 줄입니다.
- `r->ms = tf_now_ms() - start;` — unsigned 뺄셈이라 wrap에서도 12가 나옵니다(테스트 1번 첫 줄).
- `put()`은 `*len < cap`일 때만 실제 포인터를 넘기고, 아니면 `vsnprintf(NULL, 0, ...)`로 길이만 셉니다. `buf + len`이 버퍼 밖을 가리키는 포인터 연산 자체를 피합니다.
- `%zu`, `%lu` + `(unsigned long)` 캐스트: `uint32_t`를 `%u`로 찍는 것은 대부분 맞지만 이식성 있게 쓰려면 캐스트하거나 `PRIu32`를 씁니다.
- 첫 줄 `if (cap > 0) buf[0] = '\0';` — 결과가 0줄이어도(실제로는 SUMMARY가 항상 있지만) 빈 문자열을 보장합니다.

### 4.5 복잡도

- `tf_run`: O(n) + 테스트 실행 시간. `strcmp` 필터 비용은 블록 이름 길이만큼.
- `tf_report`: 커버리지 판정이 requirement마다 결과 전체를 훑으므로 O(m·r + m²) (m = requirement 수, r = 결과 수). 수백 개면 수만 번 `strcmp`라 무시할 만합니다. 커지면 테스트 테이블에 requirement 인덱스를 미리 넣어 O(r + m)으로 줄입니다.
- 메모리: O(1) + 호출자 버퍼.

---

## 5. 임베디드 관점으로 말할 것

- **테이블 기반 등록**: 실제로는 `__attribute__((section(".testcases")))` + 링커 스크립트로 각 파일의 테스트를 한 섹션에 모읍니다(Zephyr ztest, U-Boot `U_BOOT_CMD`와 같은 방식). 등록 함수 호출이 없어서 테스트를 추가할 때 중앙 파일을 고치지 않아도 됩니다.
- **같은 이미지, 다른 플랫폼**: 플랫폼은 런타임에 SoC ID 레지스터나 FPGA 빌드 ID 레지스터를 읽어 정합니다. SKIP_ON_FPGA처럼 **테스트가 스스로 이유를 밝히는 플래그**가 있어야 "FPGA에서 0%인 이유"를 리포트로 설명할 수 있습니다.
- **기계가 읽는 로그**: `RESULT,...` 한 줄 고정 형식은 사람이 읽는 printf와 섞여도 호스트가 `startswith("RESULT,")`로 걸러낼 수 있습니다. 필드 추가는 뒤에만 붙여서 기존 파서를 깨지 않습니다.
- **UART에 바로 흘리기**: 버퍼가 작으면 `tf_report`를 버퍼 대신 `putc` 콜백으로 바꿔 한 줄씩 내보냅니다. 버퍼 버전은 결과를 공유 메모리에 남겨 JTAG으로 읽어 가는 경우에 씁니다.
- **벤치 결과도 같은 형식으로**: [V4](../V4_perf_bench_stats/index.html)의 min/p50/p99를 `PERF,<id>,...` 줄로 같이 내면 같은 파서와 DB를 씁니다.

## 5.1 면접관(HW 출신 Sr Manager)이 파고들 포인트

- **"테스트가 멈추면(hang) 어떻게 되나?"** — 이 버전은 사후 측정이라 진짜 hang은 못 잡습니다. 실제로는 테스트마다 HW watchdog을 `timeout_ms`로 다시 설정하고, 리셋 후 부트 코드가 **리셋 원인 레지스터 + no-init RAM에 남긴 "현재 테스트 id"** 를 보고 그 테스트를 TIMEOUT으로 기록한 뒤 다음 테스트부터 이어 갑니다. *"Before each test I arm the watchdog and write the test id to no-init RAM; after a watchdog reset the runner reads it back, logs that test as TIMEOUT and resumes with the next one."*
- **"FPGA와 실리콘에서 같은 테스트를 어떻게 유지하나?"** — 하나의 소스, 플랫폼 플래그와 런타임 감지, 그리고 FPGA에서 SKIP된 항목은 실리콘 체크리스트에 자동으로 넘어갑니다. *"One test source for both; anything skipped on FPGA is automatically tracked as open until silicon passes it, so coverage gaps are visible from day one."*
- **"커버리지 100%면 다 된 건가?"** — 이건 **requirement(기능) 커버리지**입니다. 테스트 코드가 HW 레지스터/기능의 어디를 건드렸는지는 별개이고, RTL 쪽 functional coverage(covergroup)와 매핑하거나, 레지스터 접근 로그로 보완합니다. *"This is requirement coverage; it says every requirement has passing evidence, not that every register field or corner was exercised."*
- **"결과가 PVT/SLT로 어떻게 이어지나?"** — 같은 RESULT 줄에 보드 ID, 전압, 온도, 주파수를 붙여 DB에 쌓고, PVT 코너별 pass rate로 보고, 안정된 테스트 일부는 production SLT 리스트로 승격합니다(runtime 예산 안에서). *"The same result lines carry board, voltage, temperature and frequency, so the PVT report and the SLT test list are just queries on that database."*
- **"테스트 자체가 맞는지는 어떻게 아나?"** — 실패해야 하는 상황을 주입해 실제로 FAIL이 나는지 봅니다(fault injection, 잘못된 기대값, 링크 강제 down). *"Every new test is checked once with an injected fault so we know it can actually fail."*

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| How does the host-side Python orchestrate this? | 보드 팜의 각 보드에 대해 전원 제어(PDU/릴레이) → 이미지 로드(JTAG/OpenOCD, 또는 UART 부트로더) → UART 로그 수집 → `RESULT,` 줄 파싱 → DB 기록 순서입니다. 타임아웃 없는 부팅·멈춤은 호스트 쪽 타이머로 감지해 전원 재인가합니다. pytest를 쓰면 보드 하나를 fixture로 두고 펌웨어 테스트 id를 parametrize합니다. |
| Functional coverage vs code coverage? | 이 리포트는 requirement(기능) 커버리지입니다. code coverage는 펌웨어 코드의 줄/분기 실행률(gcov, bare-metal이면 카운터를 RAM에 모아 덤프)이고, HW 관점에서는 RTL functional coverage(covergroup)와 레지스터 필드 접근 커버리지가 또 따로 있습니다. 셋 다 "얼마나 검증했나"의 다른 축입니다. |
| How do you keep requirement traceability? | requirement id(Jira/Polarion/DOORS)를 테스트 테이블에 문자열로 박고, 호스트가 결과를 그 id에 연결해 올립니다. 테스트 없는 requirement(REQ-ISP-001)와 requirement 없는 테스트 둘 다 리포트에 경고로 냅니다. |
| What is your retry / flaky policy? | 첫 실패에 자동 재시도는 하되 **결과는 FAIL→PASS(flaky)로 따로 기록**하고 숨기지 않습니다. flaky 비율이 기준을 넘으면 테스트나 HW 마진(→ [V7](../V7_shmoo_margin/index.html)) 이슈로 티켓을 엽니다. destructive 테스트는 재시도 금지. |
| How do you keep tests runnable both pre- and post-silicon? | HAL 아래만 플랫폼별로 바꾸고 테스트는 공통입니다. FPGA(HAPS)에서는 클록이 느리므로 timeout을 플랫폼 배율로 늘리고, 없는 블록(아날로그 PHY, 실제 DDR PHY)은 SKIP_ON_FPGA로 표시합니다. 같은 테스트가 RTL 시뮬레이션에서도 돌면 더 좋습니다. |
| What about a test that truly hangs? | 테스트별 watchdog + no-init RAM에 현재 테스트 id 기록 + 리셋 후 재개(§5.1). 멀티코어면 다른 코어가 감시하고, 호스트도 UART 무응답 타이머로 이중 감시합니다. |
| What if the report does not fit? | `tf_report`는 snprintf처럼 필요한 길이를 돌려주므로 다시 할당하거나, 줄 단위로 UART에 흘리는 콜백 버전으로 바꿉니다. 절대로 넘쳐 쓰지 않는 것이 우선입니다. |
| Why not count SKIP as covered? | SKIP은 "검증하지 않았음"입니다. FPGA에서 SKIP된 requirement는 실리콘에서 PASS가 나올 때까지 open으로 남아야 테이프아웃/샘플 출하 판단에 빈칸이 보입니다. |

## 7. 자주 하는 실수

- TIMEOUT보다 `rc`를 먼저 판정 → 늦게 끝난 테스트가 PASS (테스트 1번 DDR-STRESS-002)
- `>=`로 타임아웃 비교 → 정확히 40 ms인 DDR-TRAIN-001이 TIMEOUT (테스트 1번)
- SKIP 테스트의 `fn`을 호출함 → destructive 테스트가 실제로 실행됨 (`g_calls` 검사)
- 필터에서 빠진 테스트도 SKIP으로 기록 → total이 6이 됨 (테스트 3번)
- `snprintf` 반환값만큼 포인터를 움직이고 남은 크기를 다시 계산하지 않음 → 잘리는 순간 버퍼 오버런 (테스트 5번, ASan이 잡음)
- 잘렸을 때 실제로 쓴 길이를 반환 → 호출자가 필요한 크기를 모름
- `covered * 100 / total`에서 `total == 0` 처리 누락 → 0으로 나누기 (테스트 6번)
- requirement가 FAIL/SKIP 테스트만 있어도 covered로 셈
