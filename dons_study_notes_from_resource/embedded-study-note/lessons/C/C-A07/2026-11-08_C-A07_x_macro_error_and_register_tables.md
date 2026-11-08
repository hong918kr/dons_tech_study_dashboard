---
id: C-A07
time: 90분
updated: 2026-11-08
---
# C-A07 X-Macro로 에러코드·레지스터 테이블 생성

## 학습 목표

- 에러 코드 목록 하나(`FW_ERR_LIST`)로 값 enum, 촘촘한(dense) 인덱스 enum, 디스크립터 표, 값→인덱스 `switch`, `err_to_str()`/`err_desc()`/`err_from_str()` 를 **생성**하고, 값이 희소(sparse)하고 음수일 때 왜 "값"과 "인덱스"를 나눠야 하는지 설명한다.
- 목록의 실수(값 중복, 이름 중복, 범위 밖 값, 정렬 안 된 오프셋, 오프셋 중복)를 X-macro 가 만든 코드가 **컴파일 에러로** 막게 만들고, 실제 clang 에러 메시지를 읽는다.
- 레지스터 맵(이름, 오프셋, 리셋값, 접근 속성)으로 덤프 함수와 POR(전원 투입) 셀프테스트를 생성하되, **읽으면 상태가 바뀌는(RC) 레지스터와 쓰기 전용(WO) 레지스터는 읽지 않는** 것을 mock 버스로 검증한다.
- "항목을 추가해도 모든 표가 동기화된다"를 `-DC_A07_EXTRA` 빌드로 직접 증명하고, 손으로 맞춘 표가 어떻게 조용히 어긋나는지(`demo_manual.c`) 비교한다.
- 설계 판단을 말할 수 있다: 매크로 인자 방식 vs `.def` 재포함 방식, switch vs 표, X-macro vs 외부 코드 생성기(SVD→헤더), 인덱스를 영속 저장하면 안 되는 이유, MISRA 관점.

## 1. 문제

펌웨어에는 "같은 목록을 여러 모양으로" 쓰는 곳이 많다. 에러 코드는 enum, 로그용 문자열, CLI 설명, 텔레메트리 카운터 칸으로 쓰이고, 레지스터 맵은 오프셋 상수, 덤프 표, 리셋값 셀프테스트, 디버그 셸 이름 표로 쓰인다. 이것들을 손으로 따로 관리하면 누군가 하나만 고치는 순간 **조용히 어긋난다** — 로그에 엉뚱한 에러 이름이 찍히는 버그는 리뷰에서 거의 안 잡힌다. 시니어 면접에서는 "에러 코드 문자열 표를 어떻게 유지하나요?"로 시작해 "값이 희소하면?", "중복을 컴파일 타임에 잡으려면?", "항목을 추가했을 때 무엇이 깨질 수 있나?"로 이어진다.

X-macro 는 목록을 **한 번만** 쓰고(single source of truth), 목록 매크로에 "각 항목을 어떻게 펼칠지"를 정하는 매크로 `X` 를 바꿔 넘겨 가며 여러 산출물을 만든다. 기본 아이디어는 C-B15 연습 3 과 C-M03 연습 5 에서 맛봤다. 이 레슨은 그 위에 **희소 값, 컴파일 타임 계약, 레지스터 접근 속성, 동기화 증명**을 얹는다.

```svg
<svg viewBox="0 0 740 345" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">목록 하나 → X 를 바꿔 끼우며 여러 번 펼친다</text>
  <rect x="10" y="34" width="250" height="150" rx="8" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="54" font-weight="bold">#define FW_ERR_LIST(X) \</text>
    <text x="28" y="74">X(OK,        0, INFO, "...")</text>
    <text x="28" y="92">X(TIMEOUT,  -1, WARN, "...")</text>
    <text x="28" y="110">X(BUSY,     -2, WARN, "...")</text>
    <text x="28" y="128">...</text>
    <text x="28" y="146">X(HW_FAULT,-100, FATAL,"...")</text>
  </g>
  <text x="20" y="172" fill="currentColor" font-size="12">원본은 여기 한 곳뿐</text>
  <g stroke="currentColor" fill="none">
    <path d="M260 70 L330 52"/><path d="M260 90 L330 96"/><path d="M260 110 L330 140"/>
    <path d="M260 130 L330 184"/><path d="M260 150 L330 228"/><path d="M260 170 L330 272"/>
  </g>
  <g stroke="currentColor">
    <rect x="330" y="36" width="400" height="32" rx="5" class="f-ok"/>
    <rect x="330" y="80" width="400" height="32" rx="5" class="f-ok"/>
    <rect x="330" y="124" width="400" height="32" rx="5" class="f-ok"/>
    <rect x="330" y="168" width="400" height="32" rx="5" class="f-warn"/>
    <rect x="330" y="212" width="400" height="32" rx="5" class="f-warn"/>
    <rect x="330" y="256" width="400" height="32" rx="5" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="340" y="57"><tspan font-family="monospace">X_ERR_ENUM</tspan> → enum fw_err { FW_ERR_BUSY = (-2), ... }</text>
    <text x="340" y="101"><tspan font-family="monospace">X_ERR_IDX</tspan> → enum { FW_ERR_IDX_OK, ..., FW_ERR_COUNT }</text>
    <text x="340" y="145"><tspan font-family="monospace">X_ERR_INFO</tspan> → k_err_info[] = { {-2, "BUSY", ...}, ... }</text>
    <text x="340" y="189"><tspan font-family="monospace">X_ERR_CASE</tspan> → switch: case (-1): return IDX_TIMEOUT;</text>
    <text x="340" y="233"><tspan font-family="monospace">X_ERR_CHECK</tspan> → 항목마다 _Static_assert(범위, 심각도)</text>
    <text x="340" y="277"><tspan font-family="monospace">X_ERR_NAMESZ</tspan> → union 크기 = 가장 긴 이름 + 1</text>
  </g>
  <text x="10" y="312" fill="currentColor" font-size="12">초록 = 런타임 표/이름, 주황 = 컴파일 타임 검사.</text>
  <text x="10" y="332" fill="currentColor" font-size="12">값 중복은 생성된 switch 의 duplicate case, 이름 중복은 enum 재정의로 공짜로 잡힌다.</text>
</svg>
```

`starter.c` 에는 두 목록과 예제 X 매크로 3개(값 enum, 오프셋 enum, 레지스터 ID enum), mock 버스, 테스트 37개가 있다. 처음엔 **10/37** 만 통과한다. TODO 14곳(A1–A5, B1–B3, C1–C6)을 채워 37/37 을 만들고, `-DC_A07_EXTRA` 로 목록에 항목을 추가한 빌드도 **코드 수정 없이** 통과시킨다.

### Part A — 에러 코드: enum · 표 · 조회

- A1 `enum fw_err_idx { FW_ERR_IDX_OK, ..., FW_ERR_COUNT }` — 값과 별개인 0부터의 촘촘한 인덱스.
- A2 `k_err_info[]` — 목록 순서대로 `{ 값, "이름", 심각도, "설명" }`. `_Static_assert(ARRAY_LEN(k_err_info) == FW_ERR_COUNT)` 는 이미 있다.
- A3 `err_index(code)` — 값 → 인덱스, 모르면 -1. 값이 `0, -1..-4, -10..-12, -100` 처럼 희소하고 음수라 배열 첨자로 쓸 수 없다 → **switch 를 생성**한다.
- A4 `err_to_str(code)`, `err_desc(code)` — 모르는 값은 `"UNKNOWN"`, `"unknown error"`.
- A5 `err_from_str("NACK", &v)` — 이름 → 값 (CLI·설정 파싱용), 대소문자 구분.

### Part B — 어려운 부분: 컴파일 타임 계약과 함정

- B1 항목마다 `_Static_assert` 를 생성: 값은 와이어 프로토콜의 **int8 필드**에 들어가야 하고(−128..0), 심각도는 enum 범위 안.
- B2 `ERR_NAME_MAX` — 가장 긴 이름 크기(널 포함)를 **컴파일 타임 상수**로. 로그 칸 폭·버퍼 크기에 쓴다.
- B3 `err_record(code)` + `err_stats_format()` — 에러별 카운터(텔레메트리). 목록 밖 값은 `?` 칸. `"TIMEOUT=2 CRC=1 ?=1"`.
- 함정: 파일 위쪽에 드라이버 헤더 흉내 `#define TIMEOUT 1000u` 가 있다. 그래도 `err_to_str(FW_ERR_TIMEOUT)` 는 `"TIMEOUT"` 이어야 한다 (`"1000u"` 가 아니라).
- 동기화 테스트: 표를 도는 **일반 성질**(왕복 변환, 이름·값 유일성)과 `-DC_A07_EXTRA` 빌드(목록 **중간**에 `ECC = -13` 삽입)로 "항목 추가 시 모든 표가 같이 바뀐다"를 확인한다.

### Part C — 레지스터 맵 → 덤프 · 셀프테스트

UART 비슷한 주변장치 8개 레지스터: `X(이름, 오프셋, 리셋값, 접근)`, 접근은 `RO / RW / WO / RC(read-clear) / W1C`.

- C1 `k_uart_regs[]` 디스크립터 표 (인덱스 = 주어진 `UART_REG_<name>`).
- C2 항목마다 `_Static_assert`: 오프셋 4바이트 정렬, 블록(0x400) 안.
- C3 `reg_name_at(ofs)` — 오프셋 → 이름, 생성된 switch (오프셋 중복 = 컴파일 에러).
- C4 `REG_NAME_MAX` — B2 와 같은 방법, 덤프 칸 맞춤용.
- C5 `reg_dump(bus, out, cap)` — 읽을 수 있는 레지스터는 값과 "리셋값과 다르면 `*`", **RC 는 읽지 않고** `(rc: not read)`, **WO 는** `(wo)`. 테스트가 mock 버스의 읽기 기록으로 RDR/TDR/ERRCNT 를 한 번도 안 읽었는지 확인한다.
- C6 `reg_reset_check(bus, &bad_mask)` — POR 직후 리셋값 셀프테스트. 틀린 레지스터 비트를 모으고 `FW_ERR_HW_FAULT` 를 돌려준다 (실리콘 밸리데이션·부트 BIST 에서 매일 하는 일).

## 2. Problem (English)

> **Single-source tables with X-macros.** Firmware keeps an error-code list and a peripheral register map. Each is written exactly once as a list macro:
>
> ```c
> #define FW_ERR_LIST(X)  X(OK, 0, SEV_INFO, "success") X(TIMEOUT, -1, SEV_WARN, "operation timed out") ... X(HW_FAULT, -100, SEV_FATAL, "hardware fault")
> #define UART_REG_LIST(X) X(CR1, 0x00u, 0x00000000u, ACC_RW) ... X(RDR, 0x10u, 0, ACC_RC) X(TDR, 0x14u, 0, ACC_WO) ... X(ID, 0x1Cu, 0x00A70011u, ACC_RO)
> ```
>
> 1. Generate a value enum, a dense index enum with `FW_ERR_COUNT`, a descriptor table, `int err_index(int code)` (−1 if unknown), `err_to_str`, `err_desc`, and `bool err_from_str(const char *name, int *out)`. Error values are sparse and negative.
> 2. Make the build fail if two entries share a value or a name, if a value does not fit an `int8_t` wire field, or if a register offset is unaligned, outside the block, or duplicated. Compute the longest name length as a compile-time constant. Note that a vendor header may `#define TIMEOUT 1000u`.
> 3. Add per-error counters and a formatter (`"TIMEOUT=2 CRC=1 ?=1"`).
> 4. Generate `reg_dump()` and a power-on reset-value self test over a bus abstraction `uint32_t (*rd)(void *ctx, uint32_t ofs)`. Read-clear and write-only registers must never be read.
> 5. Prove that adding an entry (in the middle of the error list, at the end of the register list) keeps every table in sync without touching any other code.
>
> Constraints: C11, `-Wall -Wextra -Werror`, no external code generator, no GNU extensions.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 문제 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# FAIL 27줄 + "10/37 passed" — TODO 14곳을 채워 37/37
```

> 처음부터 통과하는 10개는 주어진 예제(값 enum, 오프셋 enum, 레지스터 ID enum, `UART_REG` 접근자)와 "모르는 값은 UNKNOWN" 같은 스텁이 우연히 맞는 테스트다. starter 는 스텁이 `NULL` 을 돌려줘도 죽지 않도록 `STREQ` 가 NULL 을 FAIL 로 처리한다.

## 4. 힌트

<details>
<summary>힌트 1 — X 매크로의 매개변수 개수는 목록 열 개수와 같아야 한다</summary>

`FW_ERR_LIST(X)` 는 `X(name, val, sev, desc)` 를 부르므로 `#define X_ERR_IDX(name, val, sev, desc) FW_ERR_IDX_##name,` 처럼 4개를 다 받아야 한다. 쓰지 않는 열이 많으면 `X_ERR_IDX(name, ...)` 로 나머지를 삼킬 수 있다 (추가 연습 3). 마지막 쉼표는 enum·초기화 목록에서 허용된다(C11 6.7.2.2p1, 6.7.9p1 문법).

</details>

<details>
<summary>힌트 2 — 희소한 음수 값은 첨자가 될 수 없다</summary>

`names[-100]` 은 불가능하고 `names[100]` 처럼 뒤집어도 101칸 중 9칸만 쓴다. 값 → 인덱스를 **switch 로 생성**(`case (val): return FW_ERR_IDX_##name;`)하고, 나머지 표는 인덱스로 찾는다. switch 의 `case` 값이 겹치면 제약 위반(6.8.4.2p3)이라 중복 검사도 덤으로 얻는다.

</details>

<details>
<summary>힌트 3 — #name 은 안전하고, 한 단계 거치면 위험하다</summary>

매개변수가 `#` 나 `##` 의 피연산자면 인자는 **펼쳐지지 않은 채로** 쓰인다. 그 외에는 먼저 완전히 매크로 확장된 뒤 대입된다(6.10.3.1p1). 그래서 `#name`, `FW_ERR_##name` 은 `#define TIMEOUT 1000u` 가 있어도 `TIMEOUT` 그대로지만, `XSTR(name)` 처럼 다른 매크로에 넘기면 `"1000u"` 가 된다.

</details>

<details>
<summary>힌트 4 — "가장 긴 이름"을 상수식으로</summary>

`sizeof` 는 상수식이고, union 의 크기는 가장 큰 멤버 이상이다. 항목마다 `char n_##name[sizeof #name];` 멤버를 만들어 union 에 넣으면 `sizeof(union ...)` 가 가장 긴 이름 + 1 이 된다.

</details>

<details>
<summary>힌트 5 — 항목마다 _Static_assert 를 찍어 낼 수 있다</summary>

`_Static_assert` 는 선언이라 파일 범위에 몇 개든 둘 수 있다. `#define X_ERR_CHECK(name, val, sev, desc) _Static_assert(..., "FW_ERR_" #name ": ...");` 를 정의하고 파일 범위에서 `FW_ERR_LIST(X_ERR_CHECK)` 를 한 번 펼친다. 메시지는 문자열 리터럴 연결(번역 단계 6)로 항목 이름을 담을 수 있다.

</details>

<details>
<summary>힌트 6 — 덤프는 "읽어도 되는가"부터</summary>

RC(read-clear) 레지스터는 읽는 순간 FIFO 가 pop 되거나 카운터가 0이 된다. 덤프가 그걸 읽으면 **디버그 기능이 버그를 만든다**. 표의 접근 속성을 먼저 보고, RC/WO 는 버스를 건드리지 말고 표시만 한다. 셀프테스트도 같다.

</details>

## 5. 풀이

### Part A — 값과 인덱스를 나눈다

전처리기가 실제로 만든 코드 (`cc -std=c11 -E -P solution.c`, 일부):

```text
enum fw_err { FW_ERR_OK = (0), FW_ERR_TIMEOUT = (-1), FW_ERR_BUSY = (-2), FW_ERR_INVAL = (-3), FW_ERR_NOMEM = (-4), FW_ERR_CRC = (-10), FW_ERR_NACK = (-11), FW_ERR_OVERRUN = (-12), FW_ERR_HW_FAULT = (-100), };
enum fw_err_idx { FW_ERR_IDX_OK, FW_ERR_IDX_TIMEOUT, FW_ERR_IDX_BUSY, FW_ERR_IDX_INVAL, FW_ERR_IDX_NOMEM, FW_ERR_IDX_CRC, FW_ERR_IDX_NACK, FW_ERR_IDX_OVERRUN, FW_ERR_IDX_HW_FAULT, FW_ERR_COUNT };
        case (0): return FW_ERR_IDX_OK; case (-1): return FW_ERR_IDX_TIMEOUT; case (-2): return FW_ERR_IDX_BUSY; case (-3): return FW_ERR_IDX_INVAL; case (-4): return FW_ERR_IDX_NOMEM; case (-10): return FW_ERR_IDX_CRC; case (-11): return FW_ERR_IDX_NACK; case (-12): return FW_ERR_IDX_OVERRUN; case (-100): return FW_ERR_IDX_HW_FAULT;
```

`#define TIMEOUT 1000u` 가 있는데도 `FW_ERR_TIMEOUT`, `"TIMEOUT"` 이 그대로 나온 점을 기억해 두자 (Part B).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">희소한 값 → 촘촘한 인덱스 → 모든 표</text>
  <text x="10" y="44" fill="currentColor">값 (와이어로 나감)</text>
  <text x="10" y="114" fill="currentColor">인덱스 (메모리 안에서만)</text>
  <g stroke="currentColor">
    <rect x="160" y="30" width="56" height="24" class="f-acc"/><rect x="220" y="30" width="56" height="24" class="f-acc"/>
    <rect x="280" y="30" width="56" height="24" class="f-acc"/><rect x="340" y="30" width="56" height="24" class="f-acc"/>
    <rect x="400" y="30" width="56" height="24" class="f-acc"/><rect x="460" y="30" width="56" height="24" class="f-acc"/>
    <rect x="520" y="30" width="56" height="24" class="f-acc"/><rect x="580" y="30" width="56" height="24" class="f-acc"/>
    <rect x="640" y="30" width="90" height="24" class="f-acc"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="182" y="47">0</text><text x="240" y="47">-1</text><text x="300" y="47">-2</text><text x="360" y="47">-3</text>
    <text x="420" y="47">-4</text><text x="476" y="47">-10</text><text x="536" y="47">-11</text><text x="596" y="47">-12</text>
    <text x="668" y="47">-100</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M188 54 L188 98"/><path d="M248 54 L248 98"/><path d="M308 54 L308 98"/><path d="M368 54 L368 98"/>
    <path d="M428 54 L428 98"/><path d="M488 54 L488 98"/><path d="M548 54 L548 98"/><path d="M608 54 L608 98"/>
    <path d="M685 54 L685 98"/>
  </g>
  <text x="540" y="80" fill="currentColor">err_index() = 생성된 switch</text>
  <g stroke="currentColor">
    <rect x="160" y="100" width="56" height="24" class="f-ok"/><rect x="220" y="100" width="56" height="24" class="f-ok"/>
    <rect x="280" y="100" width="56" height="24" class="f-ok"/><rect x="340" y="100" width="56" height="24" class="f-ok"/>
    <rect x="400" y="100" width="56" height="24" class="f-ok"/><rect x="460" y="100" width="56" height="24" class="f-ok"/>
    <rect x="520" y="100" width="56" height="24" class="f-ok"/><rect x="580" y="100" width="56" height="24" class="f-ok"/>
    <rect x="640" y="100" width="90" height="24" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="184" y="117">0</text><text x="244" y="117">1</text><text x="304" y="117">2</text><text x="364" y="117">3</text>
    <text x="424" y="117">4</text><text x="484" y="117">5</text><text x="544" y="117">6</text><text x="604" y="117">7</text>
    <text x="681" y="117">8</text>
  </g>
  <rect x="160" y="146" width="570" height="66" rx="6" fill="none" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="170" y="166">인덱스로 바로 찾는 표 (크기 = FW_ERR_COUNT):</text>
    <text x="170" y="186" font-family="monospace">k_err_info[i].name / .desc / .sev     g_err_stats[i]</text>
    <text x="170" y="204" font-family="monospace">err_to_str(c) = k_err_info[err_index(c)].name</text>
  </g>
  <rect x="10" y="226" width="720" height="62" rx="6" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="20" y="246" font-weight="bold">-DC_A07_EXTRA: 목록 중간에 X(ECC, -13, ...) 삽입</text>
    <text x="20" y="264">값은 그대로, 인덱스는 HW_FAULT 가 8 → 9 로 밀린다 (테스트로 확인). 메모리 안 표는 다시 빌드되니 문제없지만,</text>
    <text x="20" y="281">인덱스를 플래시·로그·프로토콜에 저장했다면 펌웨어 업데이트 후 깨진다 → 밖으로 나가는 것은 "값"만.</text>
  </g>
</svg>
```

설계 포인트:

| 결정 | 이유 |
|---|---|
| 값 enum 과 인덱스 enum 을 따로 | 값은 프로토콜 계약(바뀌면 안 됨), 인덱스는 배열 첨자(빌드마다 바뀌어도 됨). 섞으면 `-100` 짜리 배열이나 구멍 난 표가 생긴다 |
| `err_index()` 만 switch, 나머지는 표 조회 | 생성된 switch 는 1개뿐이라 코드 크기가 작고, 그 switch 가 값 중복 검사기 역할까지 한다 |
| `FW_ERR_COUNT` 를 인덱스 enum 의 마지막에 | 개수가 정수 상수식 → 배열 크기, `_Static_assert` 에 그대로 쓴다 (C-M11: C 의 const 변수는 상수식이 아님) |
| 값에 괄호 `(val)` | `X(FOO, 1 << 3, ...)` 같은 식이 와도 우선순위가 안전 (C-B15) |
| `err_from_str` 은 표를 선형 탐색 | 항목 수십 개, CLI·설정 파싱용이라 O(N) 이면 충분. 수백 개면 이름 정렬 표 + 이분 탐색을 같은 목록에서 생성할 수 있지만, 전처리기는 정렬을 못 하므로 목록 자체를 정렬해 두고 런타임 테스트로 정렬을 확인한다 |

### Part B — 목록의 실수를 컴파일러가 잡게 한다

`demo_dup.c` 는 같은 패턴을 작게 만들고 `-D` 로 실수를 하나씩 넣는다. 실제 clang 21 출력 (노트 줄 일부 생략):

```text
$ cc -std=c11 -Wall -Wextra -c -DDUP_VALUE demo_dup.c        # X(STALL, -2) — BUSY 와 값이 같음
demo_dup.c:41:30: error: duplicate case value '-2'
   41 |     switch (code) { ERR_LIST(X_CASE) default: return "UNKNOWN"; }
$ cc -std=c11 -Wall -Wextra -c -DDUP_NAME demo_dup.c         # X(BUSY, -5) — 이름이 겹침
demo_dup.c:33:21: error: redefinition of enumerator 'ERR_BUSY'
$ cc -std=c11 -Wall -Wextra -c -DBAD_RANGE demo_dup.c        # X(BIG, -200)
demo_dup.c:36:10: error: static assertion failed due to requirement '(-200) >= -128': ERR_BIG: must fit int8
$ cc -std=c11 -Wall -Wextra -c -DBAD_ALIGN demo_dup.c        # X(FIFO, 0x0Au)
demo_dup.c:46:10: error: static assertion failed due to requirement '((10U) & 3U) == 0U': REG FIFO: offset not 32-bit aligned
demo_dup.c:46:10: note: expression evaluates to '2 == 0'
$ cc -std=c11 -Wall -Wextra -c -DDUP_OFS demo_dup.c          # X(SR2, 0x0Cu) — SR 와 오프셋이 같음
demo_dup.c:51:29: error: duplicate case value '12'
```

| 목록 실수 | 무엇이 잡나 | 근거 (C11) | 비용 |
|---|---|---|---|
| 값 중복 | 생성된 `switch` 의 `case` | 6.8.4.2p3 제약: 같은 switch 안 case 값 중복 금지 | 0 (어차피 필요한 switch) |
| 이름 중복 | 생성된 enum 상수 재정의 | 6.7p3 제약: 같은 범위·이름 공간에 연결 없는 식별자 두 번 선언 금지 | 0 |
| 값 범위 (int8) | 항목별 `_Static_assert` | 6.7.10 | 0 (코드 없음) |
| 오프셋 정렬·범위 | 항목별 `_Static_assert` | 6.7.10 | 0 |
| 오프셋 중복 | `reg_name_at` 의 `case` | 6.8.4.2p3 | 0 |
| 표 길이 ≠ COUNT | `_Static_assert(ARRAY_LEN(...) == COUNT)` | X-macro 에선 사실상 항상 참이지만, 누가 표를 손으로 고치면 잡힌다 | 0 |
| 오프셋 오름차순 | **못 잡음** (전처리기는 "이전 항목"을 모른다) | — | 런타임 테스트 `sorted` |

읽는 법 두 가지:

1. **switch 는 공짜 중복 검사기다.** `err_to_str` 을 `k_names[]` 표만으로 만들면 값 중복은 조용히 통과한다. X-macro 로 switch 를 하나라도 생성해 두면 중복이 빌드를 깬다.
2. **에러 위치는 "목록 줄"이 아니라 "펼친 줄"을 가리킬 수 있다.** `DUP_VALUE` 는 41번 줄(switch)만 가리키고 어느 항목인지는 값 `-2` 로 추리해야 한다. 반면 `_Static_assert` 메시지에는 `#name` 을 넣었기 때문에 `ERR_BIG` 이 바로 보이고, clang 의 "expanded from macro" 노트가 `EXTRA_ERR` 의 정의 줄까지 따라간다. **메시지에 항목 이름을 넣는 것**이 X-macro 진단의 요령이다.

**함정: 항목 이름이 다른 헤더의 매크로와 같을 때** (`demo_vendor_macro.c`, 실제 출력):

```text
$ ./demo_vendor_macro            # #define TIMEOUT 1000u, #define BUSY (1u << 7) 가 있는 상태
direct=OK       forwarded=OK
direct=TIMEOUT  forwarded=1000u
direct=BUSY     forwarded=(1u << 7)
E_TIMEOUT=-1 E_BUSY=-2
$ cc -std=c11 -E -P demo_vendor_macro.c | tail -n 8
static const char *const direct[] = { "OK", "TIMEOUT", "BUSY", };
static const char *const fwd[] = { "OK", "1000u", "(1u << 7)", };
enum { E_OK = (0), E_TIMEOUT = (-1), E_BUSY = (-2), };
```

- `X_NAME_DIRECT(n, v) #n` : `n` 이 `#` 의 피연산자 → 인자를 **펼치지 않는다** (6.10.3.1p1, 6.10.3.2). 안전.
- `X_NAME_FWD(n, v) XSTR(n)` : `n` 이 그냥 쓰였으므로 대입 **전에** 완전히 펼쳐진다 → `1000u`. "인자를 펼친 뒤 문자열화"하는 `XSTR` 은 버전 문자열(`XSTR(FW_MAJOR)`)에는 맞지만 X-macro 이름에는 틀린 도구다.
- `##` 로 붙인 `E_##n` 도 펼치기 전 토큰을 쓰므로 안전. 하지만 X 매크로 안에서 `n` 을 **맨몸으로** 쓰면(예: `case n:` 이나 다른 매크로 인자) 벤더 매크로로 바뀐다.
- 실전 방어: 목록 이름을 항상 접두사와 `##` 로만 식별자로 만들고(`FW_ERR_##name`), 문자열은 `#name` 으로 직접. Windows `wingdi.h` 의 `ERROR`, 여러 HAL 의 `BUSY`/`TIMEOUT`/`OK` 같은 짧은 이름이 실제로 부딪힌다. solution.c 는 파일 위에 `#define TIMEOUT 1000u` 를 둔 채로 테스트(`STREQ(err_to_str(FW_ERR_TIMEOUT), "TIMEOUT") && TIMEOUT == 1000u`)를 통과한다.

**B2 — 가장 긴 이름을 컴파일 타임에**

```c
#define X_ERR_NAMESZ(name, val, sev, desc) char n_##name[sizeof #name];
union err_name_sizer { FW_ERR_LIST(X_ERR_NAMESZ) };
#define ERR_NAME_MAX (sizeof(union err_name_sizer))      /* = sizeof "HW_FAULT" = 9 */
```

`sizeof` 는 상수식이고 union 의 크기는 가장 큰 멤버 이상이다. 표준은 union 끝에 패딩을 허용하므로(6.7.2.1p17) 엄밀히는 "≥ 가장 긴 이름 + 1"이고, `char` 배열만 있는 union 에 패딩을 붙이는 구현은 보기 어렵다 — 이 맥에서 테스트 `ERR_NAME_MAX == sizeof "HW_FAULT"` 가 통과했다. Part C 의 덤프는 같은 방법으로 얻은 `REG_NAME_MAX - 1` 을 `%-*s` 칸 폭으로 써서, 더 긴 레지스터 이름이 추가되면 칸이 저절로 넓어진다.

**B3 — 텔레메트리 카운터**

`g_err_stats[FW_ERR_COUNT]` 는 인덱스로, 목록 밖 값은 `g_err_unknown` 으로. 테스트는 `TIMEOUT, -10, TIMEOUT, 42` 를 기록해 `"TIMEOUT=2 CRC=1 ?=1"` (19글자)을 확인하고, 버퍼가 10바이트면 잘림을 `-1` 로 알리는지, `HW_FAULT` 를 추가하면 목록 순서대로 `"TIMEOUT=2 CRC=1 HW_FAULT=1 ?=1"` 이 되는지 본다. 목록 밖 값을 버리지 않고 세는 것이 중요하다 — 펌웨어 업데이트로 하위 모듈이 새 에러를 돌려주기 시작하면 `?` 칸이 먼저 알려 준다.

**동기화 증명: 항목 추가**

`-DC_A07_EXTRA` 는 에러 목록 **중간**(OVERRUN 과 HW_FAULT 사이)에 `X(ECC, -13, ...)`, 레지스터 목록 끝에 `X(DMACR, 0x20u, ...)` 를 넣는다. 다른 코드는 한 글자도 안 바뀐다:

```text
$ cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
37/37 passed
$ cc -std=c11 -Wall -Wextra -Werror -O2 -DC_A07_EXTRA solution.c -o solution && ./solution
39/39 passed
```

추가된 2개 테스트는 `FW_ERR_ECC == -13`, `err_index(-13) == 8`, 그리고 `err_index(FW_ERR_HW_FAULT) == 9` (8 에서 밀림) 이다. 나머지 37개는 같은 코드로 그대로 통과한다 — 개수 검사는 `9u + N_EXTRA`, 왕복·유일성 검사는 표를 도는 일반 성질, 덤프 기대 문자열은 `#ifdef` 로 한 줄이 더 붙는다. **"항목 추가 빌드"를 CI 매트릭스에 넣어 두면**, 누군가 X-macro 를 우회해 손으로 표를 만든 순간 이 빌드가 깨진다.

**비교: 손으로 맞춘 표는 어떻게 어긋나나** (`demo_manual.c`, enum 중간에 `ERR_NOMEM` 을 추가하고 이름 표는 안 고친 상황, 실제 출력)

```text
$ cc -std=c11 -Wall -Wextra demo_manual.c -o demo_manual && ./demo_manual
demo_manual.c:23:13: warning: enumeration value 'ERR_NOMEM' not handled in switch [-Wswitch]
code 0: positional=OK              designated=OK       switch=OK
code 1: positional=TIMEOUT         designated=TIMEOUT  switch=TIMEOUT
code 2: positional=BUSY            designated=BUSY     switch=BUSY
code 3: positional=CRC             designated=(NULL)   switch=?
code 4: positional=(out of table)  designated=CRC      switch=CRC
$ cc -std=c11 -Wall -Wextra -DPOS_ASSERT demo_manual.c
demo_manual.c:12:16: error: static assertion failed due to requirement 'sizeof names_pos / sizeof names_pos[0] == ERR_COUNT': names_pos length != ERR_COUNT
```

| 손 관리 방법 | 이번 실수(중간 삽입) | 못 잡는 실수 |
|---|---|---|
| 위치 초기화 `{ "OK", "TIMEOUT", ... }` | **조용히** `NOMEM` 이 `"CRC"` 로 찍힘 | 전부 |
| + 길이 `_Static_assert` | 잡음 (길이 4 ≠ 5) | 두 항목의 순서가 바뀜, 하나 지우고 하나 추가 |
| 지정 초기화 `[ERR_X] = "X"` | 이름은 안 밀리지만 `NULL` 구멍 → 런타임에 터짐 | 구멍 (길이 검사로 못 잡음) |
| enum switch + `-Wswitch` (default 없음) | 경고 (`-Werror` 면 에러) | 값이 희소한 int 코드에는 못 씀, default 를 넣는 순간 꺼짐 |
| X-macro | 구조적으로 불가능 | 목록 자체의 오타 (값·설명이 틀린 것) |

지정 초기화 + `-Wswitch` 조합도 괜찮은 대안이다 (MISRA 를 엄격히 따르는 팀이 `#`/`##` 를 피하려고 쓰는 방식). X-macro 의 강점은 "검사"가 아니라 **어긋날 방법 자체를 없앤다**는 데 있다.

### Part C — 레지스터 맵: 접근 속성이 덤프를 결정한다

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">reg_dump / reg_reset_check: 표의 접근 속성 → 버스를 건드릴지 결정</text>
  <rect x="10" y="32" width="190" height="230" rx="8" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace">
    <text x="20" y="52" font-weight="bold">k_uart_regs[]</text>
    <text x="20" y="74">CR1    0x00  RW</text>
    <text x="20" y="94">CR2    0x04  RW</text>
    <text x="20" y="114">BRR    0x08  RW</text>
    <text x="20" y="134">SR     0x0C  W1C</text>
    <text x="20" y="154">RDR    0x10  RC</text>
    <text x="20" y="174">TDR    0x14  WO</text>
    <text x="20" y="194">ERRCNT 0x18  RC</text>
    <text x="20" y="214">ID     0x1C  RO</text>
  </g>
  <text x="20" y="244" fill="currentColor">(+ DMACR 0x20 RW, EXTRA)</text>
  <g stroke="currentColor" fill="none">
    <path d="M200 120 L250 80"/><path d="M200 160 L250 160"/><path d="M200 190 L250 230"/>
  </g>
  <rect x="250" y="40" width="480" height="76" rx="6" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="260" y="60" font-weight="bold">RO / RW / W1C → 읽는다 (bus-&gt;rd)</text>
    <text x="260" y="80" font-family="monospace">SR     @0x0C = 0x000000E0 *     ← 리셋값 0xC0 과 다름</text>
    <text x="260" y="100">셀프테스트: 읽은 값 != 리셋값 → bad_mask |= 1u &lt;&lt; i → FW_ERR_HW_FAULT</text>
  </g>
  <rect x="250" y="126" width="480" height="76" rx="6" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="260" y="146" font-weight="bold">RC → 절대 읽지 않는다</text>
    <text x="260" y="166" font-family="monospace">RDR    @0x10 = (rc: not read)</text>
    <text x="260" y="186">읽으면 RX FIFO pop / 에러 카운터 0 — 덤프가 데이터를 먹는다</text>
  </g>
  <rect x="250" y="212" width="480" height="50" rx="6" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="260" y="232" font-weight="bold">WO → 읽지 않는다</text>
    <text x="260" y="250" font-family="monospace">TDR    @0x14 = (wo)</text>
    <text x="440" y="250">읽은 값은 의미 없음</text>
  </g>
</svg>
```

POR 상태 mock 에서 `reg_dump` 가 만든 실제 문자열 (테스트가 이 문자열과 **바이트 단위로** 비교해 PASS):

```text
CR1    @0x00 = 0x00000000
CR2    @0x04 = 0x00000000
BRR    @0x08 = 0x00000000
SR     @0x0C = 0x000000C0
RDR    @0x10 = (rc: not read)
TDR    @0x14 = (wo)
ERRCNT @0x18 = (rc: not read)
ID     @0x1C = 0x00A70011
```

C 파트 설계 포인트:

- **mock 은 목록과 독립적이어야 한다.** `mock_por()` 는 데이터시트 값(SR=0xC0, ID=0x00A70011)을 손으로 적고, "RDR/TDR/ERRCNT 를 안 읽었다"는 검사도 오프셋 `0x10, 0x14, 0x18` 을 직접 적는다. 테스트가 같은 X-macro 표로 기대값을 만들면 표가 틀려도 테스트가 같이 틀려서 통과한다 — **생성된 코드를 생성 원본으로 검증하지 말 것.** 버스 추상화(`bus_t` 의 함수 포인터, C-M06)로 실제 하드웨어 대신 기록하는 mock 을 끼우는 설계는 C-A01 에서 본격적으로 다룬다.
- 읽기 횟수: POR 덤프에서 `m.nreads == 5` (CR1, CR2, BRR, SR, ID). `-DC_A07_EXTRA` 에선 DMACR 까지 6 — 테스트는 `5u + N_EXTRA`.
- **실제 사고 사례**: IDE 의 주변장치 레지스터 뷰나 `x/8wx 0x40004400` 같은 디버거 메모리 덤프가 UART DR 을 읽어 RXNE 가 꺼지고 수신 바이트가 사라지는 일은 흔하다. 펌웨어 안의 덤프 셸 명령이 같은 짓을 하지 않도록 접근 속성을 **표에 두고 코드가 따르게** 만드는 것이 이 Part 의 핵심이다.
- `reg_reset_check` 는 W1C 상태 레지스터(SR)도 검사한다 — 리셋 직후 TXE|TC = 0xC0 은 결정적이기 때문. 하지만 실제 칩에서는 리셋 후에도 핀 상태에 따라 바뀌는 비트(예: RX 라인 idle 감지)가 있을 수 있어서, 실무 표에는 "리셋 검사 마스크" 열을 하나 더 두곤 한다 (열을 추가할 때 X 매크로를 어떻게 덜 깨지게 쓰는지는 추가 연습 3).
- `UART_REG(base, SR)` 접근자는 `UART_##name##_OFS` 로 오프셋 상수를 붙인다: 타깃에서는 `UART_REG(USART2_BASE, SR)`, 호스트 테스트에서는 `uint32_t fake_block[16]` 에 대고 써 본다 (volatile MMIO 는 C-M01).
- 오프셋 오름차순은 전처리기로 증명할 수 없어 런타임 테스트(`sorted && names`)로 확인한다. 정말 컴파일 타임에 원하면 목록을 "이전 오프셋"까지 적는 형태로 바꾸거나, 아래 Q4 의 외부 코드 생성기를 쓴다.

**생성된 switch 는 무엇이 되나** (`demo_m4.c`, 에러 목록 9개로 만든 `err_to_str` / `err_index`)

Cortex-M4 `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -Os -S` 실제 출력 (디렉티브 일부 생략, `-O2` 도 같은 모양):

```text
err_to_str:
	add.w	r1, r0, #12          @ r1 = code + 12   (-12..0 → 0..12)
	cmp	r1, #12
	bhi	.LBB0_4              @ 범위 밖 → -100 인지 따로 검사
	movw	r0, :lower16:.L.str  @ r0 = "OK" 를 미리 실어 둠 (code 0 이면 그대로 반환)
	movt	r0, :upper16:.L.str
.LCPI0_0:
	tbb	[pc, r1]             @ 바이트 점프 표, 13칸
.LJTI0_0:
	.byte	(.LBB0_3-(.LCPI0_0+4))/2   @ -12 → "OVERRUN"
	...                                @ -9..-5 다섯 칸은 모두 .LBB0_6 ("UNKNOWN")
.LBB0_4:
	adds	r0, #100
	bne	.LBB0_6              @ -100 이 아니면 "UNKNOWN"
```

```text
$ xcrun llvm-size -A m4.o            $ xcrun llvm-nm -S m4.o
.text           196                  00000000 00000080 T err_to_str
.rodata.str1.1   62                  00000080 00000044 T err_index
```

같은 소스를 arm64 호스트 `cc -O2 -S` 로 보면 점프 표 없이 `cmn w0, #5 / b.le ...` 로 이어지는 **비교 트리**(이분 탐색)가 나왔다. 어떤 모양을 고를지는 타깃별 컴파일러 휴리스틱이고, 소스는 같다. 면접에서 쓸 결론:

- 값이 거의 연속이면(−12..0) 컴파일러가 점프 표로 O(1), 멀리 떨어진 값(−100)은 따로 비교한다. "희소하면 switch 가 느리다"는 대체로 틀렸다.
- 문자열 9개 62바이트는 `.rodata.str1.1`(맥에선 `__TEXT,__cstring`) — 플래시에 있다. 이름 표가 부담이면 빌드 옵션으로 `err_to_str` 을 숫자만 찍는 버전으로 바꾸거나, 이름을 빼고 호스트 도구가 같은 목록(추가 연습 5 의 CSV)으로 복원하게 한다.

**include 방식 X-macro** (`demo_def_include.c` + `uart_regs.def`)

같은 아이디어를 "목록 매크로" 대신 **포함할 때마다 다르게 펼쳐지는 파일**로 쓸 수도 있다. LLVM 의 `Instruction.def`(`HANDLE_INST`), 리눅스 tracepoint 의 `define_trace.h` 재포함이 이 방식이다.

```c
#define UART_REG(name, ofs, rst, acc) UART_REG_##name,
enum uart_reg_id {
#include "uart_regs.def"          /* .def 끝의 #undef UART_REG 가 매번 정의를 지운다 */
    UART_REG_COUNT
};
```

```text
$ ./demo_def_include
UART_REG_COUNT=4 table=4
CR1  @0x00 reset 0x00000000
CR2  @0x04 reset 0x00000000
SR   @0x0C reset 0x000000C0
ID   @0x1C reset 0x00A70011
$ cc -std=c11 -Wall -Wextra -DFORGET demo_def_include.c        # 정의 없이 한 번 더 포함
./uart_regs.def:4:2: error: "define UART_REG(name, ofs, rst, acc) before including uart_regs.def"
```

| | 목록 매크로 `LIST(X)` | `.def` 재포함 |
|---|---|---|
| 파일 수 | 하나 (헤더 안) | 목록 파일 별도 |
| 긴 목록(수백 줄) | 줄마다 `\` 연결 — 하나 빠지면 이상한 에러 | 평범한 줄, `#if` 로 항목 조건부 포함도 쉬움 |
| X 정의 누수 | 이름을 바꿔 가며 정의, 필요하면 `#undef` | `.def` 끝에서 `#undef`, 없으면 `#error` |
| 2단 중첩 | `F(Y)` 로 자연스럽다 (추가 연습 1) | 파일 안에서 다른 `.def` 포함 |
| 정적 분석·IDE | 한 줄로 펼쳐져 진단 위치가 뭉침 | 진단이 `.def` 의 해당 줄을 가리킴 |
| include guard | 해당 없음 | **일부러 없어야** 한다 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution                 # 37/37 passed
cc -std=c11 -Wall -Wextra -Werror -O2 -DC_A07_EXTRA solution.c -o solution && ./solution   # 39/39 passed (항목 추가 빌드)
cc -std=c11 -Wall -Wextra -Werror -pedantic -O2 solution.c -o solution && ./solution       # 37/37, 확장 없음
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution && ./solution  # 37/37, 보고 없음
```

실험용 파일 (결과는 5. 풀이에 붙인 그대로):

| 파일 | 명령 | 보여주는 것 |
|---|---|---|
| `demo_dup.c` | `cc -std=c11 -Wall -Wextra -c [-DDUP_VALUE\|-DDUP_NAME\|-DBAD_RANGE\|-DBAD_ALIGN\|-DDUP_OFS] demo_dup.c` | 목록 실수 5종이 컴파일 에러가 되는 모습 |
| `demo_vendor_macro.c` | `cc -std=c11 -Wall -Wextra demo_vendor_macro.c && ./a.out`, `cc -E -P` | `#name` 은 안전, `XSTR(name)` 은 `"1000u"` |
| `demo_manual.c` | `cc -std=c11 -Wall -Wextra [-DPOS_ASSERT] demo_manual.c` | 손 관리 표의 조용한 어긋남, `-Wswitch`, 길이 assert |
| `demo_def_include.c` + `uart_regs.def` | `cc -std=c11 -Wall -Wextra [-DFORGET] demo_def_include.c` | `.def` 재포함 방식과 `#error` 가드 |
| `demo_m4.c` | `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -Os -S demo_m4.c -o -` / `cc -O2 -S` | 생성된 switch → M4 `tbb` 점프 표, arm64 비교 트리, 크기 |

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `const char *names[] = { LIST(X_NAME) }; ... names[code]` (값이 음수·희소) | 음수 첨자는 범위 밖 접근(UB), 희소하면 구멍 | 값 → 인덱스 switch 를 생성하고 표는 인덱스로 |
| 문자열화에 `XSTR(name)` | 인자가 먼저 펼쳐져 벤더 매크로 `TIMEOUT` → `"1000u"` | X 매크로 안에서 바로 `#name` |
| X 매크로 안에서 `name` 을 맨몸으로 사용 (`case name:` 등) | 같은 이름의 매크로가 있으면 바뀐다 | 항상 `PREFIX_##name` 으로 식별자를 만든다 |
| 값 매개변수에 괄호 없음 `case val:` / `= val` | `X(F, 1 << 3)` 같은 식이 오면 주변 연산자와 섞임 (C-B15) | `(val)` |
| 목록 줄 끝 `\` 하나 빠뜨림 | 목록이 그 줄에서 끝나고, 다음 줄의 `X(...)` 가 파일 범위 코드가 됨 → 엉뚱한 위치의 에러 | 긴 목록은 `.def` 방식, 또는 줄 끝 `\` 를 세로 정렬 |
| 인덱스(`FW_ERR_IDX_*`)를 플래시·로그·프로토콜에 저장 | 목록 중간 삽입 후 의미가 바뀜 (`HW_FAULT` 8 → 9) | 밖으로는 값만. 인덱스는 빌드 안에서만 |
| 값 enum 을 배열 크기로 사용 (`stats[FW_ERR_HW_FAULT]`) | 음수 크기 / 엉뚱한 크기 | `FW_ERR_COUNT` (인덱스 enum 마지막) |
| 덤프가 모든 레지스터를 읽음 | RC 레지스터를 읽어 FIFO pop·카운터 클리어 | 접근 속성 열을 두고 RC/WO 는 건너뜀 |
| 테스트 기대값을 같은 X-macro 표로 생성 | 표가 틀리면 테스트도 같이 틀려 통과 | mock·기대값은 데이터시트 숫자로 손으로 |
| 열을 하나 추가하고 X 매크로 12개를 전부 고침 | 하나라도 빠지면 "too few arguments" | 안 쓰는 열은 `...` 로 받는 습관 (추가 연습 3) |
| 같은 X 매크로 이름을 바깥·안쪽 목록에서 모두 사용 | 펼치는 중인 매크로는 다시 펼쳐지지 않음 (6.10.3.4p2) → 안쪽이 그대로 남음 | 단계마다 다른 이름 (`X_…` / `Y_…`) |

## 8. 면접 Q&A

<details>
<summary>Q1. X-macro 가 뭔지, 그리고 왜 쓰는지 30초 안에 설명해 보세요.</summary>

같은 목록(에러 코드, 레지스터, 명령, 상태)을 `#define LIST(X) X(a, ...) X(b, ...)` 로 **한 번만** 쓰고, `X` 자리에 "항목 하나를 어떻게 펼칠지"를 정의한 매크로를 넘겨 enum, 문자열 표, 디스크립터, switch, 정적 검사를 각각 생성하는 기법이다. 목적은 **동기화 버그를 구조적으로 없애는 것** — 손으로 맞춘 enum 과 이름 표는 중간 삽입 한 번에 조용히 어긋난다(`demo_manual.c` 에서 `NOMEM` 이 `"CRC"` 로 찍힘). 꼬리질문 "단점은?" → 매크로 문법이 낯설어 리뷰어가 읽기 어렵고, IDE·grep 이 `FW_ERR_TIMEOUT` 의 정의를 못 찾으며(`##` 로 만들어지니까), 진단 위치가 목록 줄이 아니라 펼친 줄을 가리킬 수 있다. 그래서 X 매크로는 짧게, 이름 규칙은 일정하게, `cc -E -P` 로 확인하는 습관을 같이 말한다.

</details>

<details>
<summary>Q2. 에러 값이 0, -1, -2, -10, -100 처럼 희소하고 음수입니다. err_to_str 을 어떻게 만들겠습니까? 성능은요?</summary>

값과 인덱스를 분리한다: 생성된 switch 로 값 → 촘촘한 인덱스(모르면 −1), 이름·설명·심각도·카운터는 인덱스로 찾는 표. 값으로 직접 첨자를 쓰면 음수는 범위 밖, 뒤집어도 구멍투성이다. 성능: Cortex-M4 clang `-Os` 에서 생성된 switch 는 `add/cmp/bhi` 범위 검사 + `tbb` 바이트 점프 표(−12..0, 13칸)와 −100 비교 한 번이 됐다 — O(1). arm64 호스트에서는 비교 트리였다. 어느 쪽이든 에러 경로라 성능보다 **크기**가 관심사이고, 측정해 보니 두 함수 196바이트 + 이름 문자열 62바이트였다. 꼬리질문 "왜 switch 가 굳이 필요한가요, 표를 선형 탐색하면 되잖아요?" → 그래도 되지만 생성된 switch 는 **값 중복을 컴파일 에러로 만든다**(6.8.4.2p3) — 공짜 검사를 버릴 이유가 없다.

</details>

<details>
<summary>Q3. 목록에 항목을 추가하거나 실수했을 때, 무엇을 컴파일 타임에 잡을 수 있고 무엇은 못 잡나요?</summary>

잡는 것: 값 중복(생성된 switch 의 duplicate case), 이름 중복(enum 상수 재정의, 6.7p3), 범위·정렬·블록 경계(항목별 `_Static_assert` 생성, 메시지에 `#name`), 표 길이(`ARRAY_LEN == COUNT`), 오프셋 중복(오프셋 switch). 못 잡는 것: 순서(오름차순) — 전처리기는 이전 항목을 모른다 → 런타임 테스트; 값·리셋값·설명의 **내용** 오타 → 데이터시트 기준 손 테스트(mock POR 값); 인덱스를 밖에 저장한 코드의 의미 변화 → 코드 리뷰 규칙("밖으로는 값만"). 그리고 CI 에 "항목 추가 빌드"(`-DC_A07_EXTRA` 같은) 를 넣어 일반 성질 테스트가 새 항목에서도 도는지 확인한다.

</details>

<details>
<summary>Q4. 레지스터 맵은 칩 벤더의 SVD 파일에 이미 있습니다. 그래도 X-macro 를 쓰나요?</summary>

목록의 **주인**이 누구냐로 고른다. 수백 개 레지스터·필드가 하드웨어 팀의 SVD/IP-XACT/엑셀에서 오고 자주 바뀌면 외부 코드 생성기(CMSIS `SVDConv`, svd2rust, 사내 Python 스크립트)로 헤더를 생성하는 게 맞다 — 정렬·겹침·오름차순 검사를 생성기가 자유롭게 할 수 있고, 구조체 레이아웃(예약 공간 포함)까지 만든다. X-macro 는 목록을 C 코드 쪽이 소유하고, 수십 항목 규모이고, 빌드에 도구를 추가하고 싶지 않을 때 좋다 (에러 코드, CLI 명령, 상태 이름, 테스트용 부분 레지스터 맵). 둘을 섞는 것도 흔하다: 생성기가 `#define UART_REG_LIST(X) ...` 형태의 X-macro 목록을 출력하고, 펌웨어는 필요한 산출물만 펼친다. 실리콘 밸리데이션에서는 이 목록으로 리셋값 검사·RW walking-ones 검사를 자동 생성해 레지스터 수백 개를 한 번에 커버한다.

</details>

<details>
<summary>Q5. 레지스터 덤프 함수를 리뷰한다면 무엇을 보겠습니까?</summary>

① 읽기 부작용 — RC(FIFO 데이터, read-to-clear 상태·카운터) 레지스터를 읽지 않는가. 이 레슨의 mock 은 읽기 기록 비트로 그걸 검사한다. ② WO 레지스터를 읽어 쓰레기 값을 "현재 설정"처럼 보여 주지 않는가. ③ 클록이 꺼진 주변장치를 읽어 버스 폴트가 나지 않는가 (실제 칩에서는 RCC enable 비트 확인이 먼저). ④ 출력 버퍼 경계 — `snprintf` 반환값으로 잘림을 감지하는가 (`reg_dump(..., 40) == -1` 테스트). ⑤ ISR 이 같은 레지스터를 만지는 중에 덤프하면 스냅샷이 일관되지 않다 — 진단용이라 보통 허용하지만 문서에 적는다 (C-M14). 꼬리질문 "디버거로 보면 되잖아요?" → IDE 의 주변장치 뷰도 DR 을 읽어 RXNE 를 지운다. 그래서 펌웨어 덤프와 디버거 모두 같은 접근 속성 정보를 존중해야 한다.

</details>

<details>
<summary>Q6. 목록 이름이 벤더 헤더의 매크로와 겹치면 어떻게 되나요?</summary>

X 매크로가 이름을 `#name` 이나 `PREFIX_##name` 으로만 쓰면 안전하다 — `#`/`##` 의 피연산자인 인자는 펼쳐지지 않기 때문이다(6.10.3.1p1). 하지만 이름을 다른 매크로에 넘기거나(`XSTR(name)`) 맨몸으로 쓰면 먼저 펼쳐진다. `demo_vendor_macro.c` 실측: `#define TIMEOUT 1000u` 아래에서 `#name` 은 `"TIMEOUT"`, `XSTR(name)` 은 `"1000u"`. 그래서 ① 이름은 항상 접두사 + `##` 로만 식별자로 ② 문자열은 X 매크로에서 직접 `#` ③ 목록 이름 자체를 `ERR_`, `REG_` 접두사 없이 짧게 두되, 펼칠 때 접두사를 붙이는 것이 관례다. solution.c 는 `#define TIMEOUT 1000u` 를 둔 채로 통과하는 것을 테스트한다.

</details>

<details>
<summary>Q7. 에러 코드에 새 항목을 목록 중간에 넣었습니다. 무엇이 깨질 수 있죠?</summary>

빌드 안의 것은 다 같이 다시 생성되므로 안 깨진다 (`-DC_A07_EXTRA` 39/39). 깨지는 건 **빌드 밖으로 나간 인덱스** 다: 플래시에 저장한 에러 카운터 배열(인덱스 순서), 인덱스를 담은 로그 레코드, 호스트 도구가 가진 이전 버전 이름 표. 업데이트 후 `HW_FAULT` 카운터 칸에 `ECC` 가 쌓인다. 규칙: ① 와이어·플래시에는 **값**만 쓴다 ② 값은 한 번 정하면 바꾸지도 재사용하지도 않는다(지운 코드는 "reserved"로 남김) ③ 영속 데이터에 목록 버전/해시를 같이 저장해 불일치 시 초기화 ④ 순서가 의미 있는 목록이면 "끝에만 추가" 규칙을 주석과 리뷰로 강제한다. 이건 enum 을 쓰는 모든 ABI 의 일반 원칙이기도 하다.

</details>

<details>
<summary>Q8. MISRA 나 코딩 규칙 관점에서 X-macro 는 괜찮나요?</summary>

MISRA C:2012 기준으로는 마찰이 있다: Rule 20.10(advisory)은 `#`·`##` 연산자를 쓰지 말라고 권고하고, Rule 20.5(advisory)는 `#undef` 를 권고하지 않으며, Dir 4.9(advisory)는 함수형 매크로 대신 함수를 권한다. 모두 advisory 라 **deviation 기록**으로 쓸 수 있고, 실제로 많은 팀이 "X-macro 패턴은 이 헤더에서만, 이 형태로만" 같은 프로젝트 단위 deviation 을 둔다. 근거는 "동기화 실패라는 더 큰 위험을 없앤다"는 것. 대안은 지정 초기화(`[ERR_X] = "X"`) + 길이 `_Static_assert` + enum switch `-Wswitch`(Part B 비교 표) 또는 외부 생성기 — 생성된 코드는 `#`/`##` 없이 평범한 C 라 규칙 검사기와 사이가 좋다. Rule 20.7(매크로 매개변수 괄호)은 X 매크로에서도 지킨다: `(val)`, `(ofs)`.

</details>

## 9. 추가 연습

### 연습 1 — 레지스터 + 필드 2단 X-macro

레지스터 목록의 각 항목이 **필드 목록 매크로 이름**을 열로 갖게 하라: `X(SR, 0x0Cu, SR_FIELDS)`, `#define SR_FIELDS(Y) Y(SR, PE, 0, 1) ... Y(SR, TXE, 7, 1)`. 이 하나로 `SR_RXNE_Msk` 같은 마스크 enum, 레지스터마다 필드 겹침 `_Static_assert`, 필드 디코더 `"SR: PE=0 FE=0 RXNE=1 TC=1 TXE=1"` 을 생성하라.

<details>
<summary>답 보기</summary>

바깥 X 매크로가 안쪽 목록을 **호출**한다: `#define X_P1_MSK(r, ofs, F) F(Y_MSK)`. `P1_REGS(X_P1_MSK)` → `X_P1_MSK(SR, 0x0Cu, SR_FIELDS)` → `SR_FIELDS(Y_MSK)` → 재스캔에서 펼쳐진다. 필드가 없는 레지스터는 `NO_FIELDS(Y)` (빈 목록)를 넘긴다. 겹침 검사는 C-M03 연습 5 의 "합 == OR" 트릭을 레지스터마다: `_Static_assert((0u F(Y_SUM)) == (0u F(Y_OR)), #r ": fields overlap");`. 디코더 표는 `{ P1_##r, #f, pos, w }` 로 평평하게 만들고 레지스터 ID 로 거른다. 주의: 바깥과 안쪽 X 매크로는 이름이 달라야 한다 — 펼치는 중인 매크로 이름은 재스캔에서 다시 펼쳐지지 않는다(6.10.3.4p2). 이 형태는 SVD 의 "레지스터 → 필드" 계층을 그대로 옮긴 것이라 생성기 출력 형식으로도 좋다.

</details>

### 연습 2 — CLI 명령 표: 핸들러를 안 만들면 빌드가 실패하게

`X(name, 최소 인자 수, help)` 목록으로 핸들러 프로토타입, 디스패치 표, `help` 출력을 생성하는 작은 디버그 셸을 만들어라. `err -10` → `"CRC: CRC mismatch (retry)"`, `peek SR` → 연습 1 의 디코더.

<details>
<summary>답 보기</summary>

`#define X_P2_PROTO(n, nargs, help) static int cmd_##n(int argc, char **argv);` 로 프로토타입을 생성하고, 표는 `{ #n, cmd_##n, (nargs), (help) }`. 핵심은 **명령을 목록에 추가하고 핸들러를 안 쓰면 빌드가 깨진다**는 것 — `-DP2_MISSING` (목록에 `reboot` 추가) 실제 결과:

```text
practice.c:128:10: warning: function 'cmd_reboot' has internal linkage but is not defined [-Wundefined-internal]
Undefined symbols for architecture arm64:
  "_cmd_reboot", referenced from:
      _k_cmds in practice-bc51e4.o
ld: symbol(s) not found for architecture arm64
```

`-Werror` 면 컴파일 단계에서, 아니어도 링크에서 멈춘다. 토큰 나누기는 줄을 직접 훑어 `'\0'` 을 넣는 방식 (`strtok` 의 재진입 문제는 C-A04), 숫자 파싱은 `strtol(..., 0)` + 끝 문자 검사 (C-M09). `help` 출력 `"help err peek"` 는 목록 순서 그대로라 문서와 셸이 어긋날 일이 없다.

</details>

### 연습 3 — 열(column)을 추가해도 X 매크로가 덜 깨지게

에러 목록에 "재시도 가능" 열을 추가하고, 이 열을 쓰지 않는 X 매크로는 고치지 않아도 되게 하라. 컴파일 타임 상수로 `P3_RETRY_MASK` (인덱스별 비트)와 `P3_N_FATAL` (치명 에러 개수)을 만들고 `p3_is_retryable(code)` 를 구현하라.

<details>
<summary>답 보기</summary>

안 쓰는 뒤쪽 열은 `...` 로 받는다: `#define X_P3_VAL(n, v, ...) E_##n = (v),`, `#define X_P3_IDX(n, ...) I_##n,`. C11 은 가변 인자 매크로를 부를 때 `...` 에 **최소 1개** 인자가 가야 하는데(6.10.3p4, C23 에서 완화) 여기선 항상 남는 열이 있어 문제없다 (`-pedantic` 에서도 경고 없음). 상수는 "앞에 연산자를 붙이는" 패턴으로 접는다: `#define P3_RETRY_MASK (0u P3_ERRS(X_P3_RETRY))` 에서 `X_P3_RETRY` 는 `| ((uint32_t)(retry) << I_##n)` → `0x16u` (TIMEOUT=1, BUSY=2, CRC=4 비트). 개수도 같은 방식: `enum { P3_N_FATAL = 0 P3_ERRS(X_P3_FATAL) };` 에서 `+ ((sev) == SEV_FATAL)`. 마스크가 `uint32_t` 라 `_Static_assert(P3_COUNT <= 32)` 를 같이 둔다.

</details>

### 연습 4 — 접근 속성을 지키는 reg_write_checked

같은 접근 속성 열로 쓰기 쪽도 지켜라: RO/RC 레지스터 쓰기는 `E_INVAL` 로 거부하고 버스를 건드리지 않는다, RW/WO 는 그대로 쓴다, W1C 는 **읽지 않고** 지울 비트만 쓴다.

<details>
<summary>답 보기</summary>

`k_p4_acc[id]` 로 `switch ((enum reg_acc)...)` 를 하되 `default` 를 두지 않아 `-Wswitch` 가 새 접근 종류 추가를 알려 주게 한다. W1C 에 RMW(`reg = reg | bit`)를 하면 읽은 순간 1이던 다른 플래그까지 다시 써서 지워 버린다 (C-M02). 그래서 mock 버스에는 **읽기 함수가 아예 없고**, 테스트는 SR=0xE0 에 0x20 을 쓰면 0xC0 이 남는지, 거부된 쓰기 후 `nwrites` 가 늘지 않았는지 확인한다. 범위 밖 ID(`P4_COUNT`, -1)도 거부.

</details>

### 연습 5 — 컴파일 타임 CSV: 호스트 도구와 목록 공유

연습 3 의 목록으로 `"name,code,desc,retry\n..."` CSV 를 **문자열 리터럴 하나**로 생성하라. 런타임 코드는 0줄이어야 한다.

<details>
<summary>답 보기</summary>

`#define X_P5_CSV(n, v, sev, desc, retry) #n "," #v "," desc "," #retry "\n"` — 인접한 문자열 리터럴은 번역 단계 6 에서 하나로 합쳐진다(5.1.1.2). `#v` 는 `-1` 토큰 열을 `"-1"` 로 만든다 (토큰 사이에 공백이 없으니 공백도 없다). 결과는 `static const char k_err_csv[]` 하나, `sizeof` 로 길이도 컴파일 타임에 안다. 쓰임새: 빌드 산출물로 CSV 를 뽑아 호스트의 로그 디코더·문서·테스트 장비가 펌웨어와 **같은 목록**을 쓰게 하거나, 펌웨어에서는 이름 문자열을 빼고(플래시 절약) 값만 로그로 보낸 뒤 호스트가 이 CSV 로 복원한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice                # 20/20 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice && ./practice      # 20/20, 보고 없음
cc -std=c11 -Wall -Wextra -DP2_MISSING practice.c -o practice                             # 핸들러 없는 명령 → 링크 실패
```

## 10. 핵심 정리

- X-macro = 목록을 한 번만 쓰고 `X` 를 바꿔 끼워 enum·표·switch·검사를 생성. 목적은 "검사"가 아니라 **어긋날 방법을 없애는 것**.
- 희소·음수 값은 **값 enum(밖으로 나감) + 인덱스 enum(안에서만) + 생성된 값→인덱스 switch** 로 나눈다. `COUNT` 는 인덱스 enum 마지막.
- 생성된 switch 는 공짜 중복 검사기(duplicate case), 생성된 enum 은 이름 중복 검사기. 범위·정렬은 항목별 `_Static_assert` 를 생성하고 메시지에 `#name` 을 넣는다. 순서는 못 잡는다 → 런타임 테스트.
- 이름은 `#name`·`PREFIX_##name` 으로만 쓴다. 한 단계 거치면(`XSTR`) 벤더 매크로 `TIMEOUT` 이 `"1000u"` 가 된다.
- `sizeof` + union 으로 "가장 긴 이름"도 컴파일 타임 상수. 안 쓰는 열은 `...` 로 받아 열 추가에 강하게.
- 레지스터 표에 접근 속성을 두고 덤프·셀프테스트·쓰기가 따르게: **RC 는 절대 읽지 않고, WO 는 읽지 않고, W1C 는 RMW 없이 쓴다.** mock 은 표와 독립적으로 데이터시트 숫자로.
- 동기화 증명 = 표를 도는 일반 성질 테스트 + "항목 추가 빌드"(`-DC_A07_EXTRA`, 37 → 39 PASS). 인덱스는 영속 저장 금지.
- 목록이 크고 하드웨어 팀 소유면 SVD → 생성기, C 쪽 소유의 수십 항목이면 X-macro. MISRA 20.10/20.5/Dir 4.9 는 advisory → deviation 으로.

## 참고자료

- C11 표준 (N1570) — 6.10.3 Macro replacement (p4 가변 인자 개수), 6.10.3.1 Argument substitution, 6.10.3.2 `#`, 6.10.3.3 `##`, 6.10.3.4 Rescanning (p2 같은 이름 재확장 금지), 6.8.4.2 switch (p3 case 중복 금지), 6.7p3 식별자 재선언, 6.7.2.2 enum, 6.7.2.1p17 union 끝 패딩, 6.7.9 초기화(지정 초기화), 6.7.10 `_Static_assert`, 5.1.1.2 번역 단계(문자열 연결) — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- Wikipedia — X macro: <https://en.wikipedia.org/wiki/X_macro>
- Randy Meyers, "The New C: X Macros", C/C++ Users Journal / Dr. Dobb's (2001)
- LLVM `llvm/include/llvm/IR/Instruction.def` (`HANDLE_INST` 재포함 방식): <https://github.com/llvm/llvm-project/blob/main/llvm/include/llvm/IR/Instruction.def>
- Linux kernel tracepoints — `include/trace/define_trace.h` (같은 헤더를 매크로를 바꿔 여러 번 포함): <https://www.kernel.org/doc/html/latest/trace/tracepoints.html>
- Arm CMSIS-SVD 와 SVDConv (SVD → 디바이스 헤더 생성): <https://open-cmsis-pack.github.io/svd-spec/main/index.html>
- Clang diagnostics — `-Wswitch`, `-Wundefined-internal`: <https://clang.llvm.org/docs/DiagnosticsReference.html>
- MISRA C:2012 — Dir 4.9, Rule 20.5, Rule 20.7, Rule 20.10 (모두 규칙 번호만 인용, 원문은 유료 문서)
- 이 트랙의 관련 레슨: C-B15 (매크로 함정, X-macro 맛보기), C-M03 (필드 X-macro, 겹침 검사), C-M11 (매크로가 꼭 필요한 곳), C-M01 (MMIO), C-M02 (W1C/RMW), C-M06 (함수 포인터 표), C-A01 (mock HAL), C-A08 (정적 계약)
