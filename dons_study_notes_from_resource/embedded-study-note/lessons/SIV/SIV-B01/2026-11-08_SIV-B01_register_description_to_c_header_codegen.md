---
id: SIV-B01
time: 60분
updated: 2026-11-08
---
# SIV-B01 레지스터 기술서 파싱 → C 헤더 자동 생성

## 학습 목표

- 레지스터 맵을 **단일 소스(single source of truth)** 인 기술서(register description, 여기선 JSON)로 관리하고, 비트 범위 `[msb:lsb]` → `SHIFT/WIDTH/MASK`, 필드 리셋값 → 레지스터 리셋값을 계산할 수 있다.
- 기술서 **lint**: 필드 겹침(overlap), 32비트 폭 초과, 리셋값 넘침, 오프셋 정렬·중복·범위, 리셋값 불일치를 잡아 **잘못된 기술서를 거부**하고, 그 거부를 음성 테스트(negative test)로 증명한다.
- 통과한 기술서에서 결정적(deterministic) C 헤더(`#define *_OFFSET/_MASK/_SHIFT/_RESET`, `_GET/_PREP`, 레지스터 기술 테이블)를 생성하고, 생성물을 **C 컴파일러(`_Static_assert`)로 한 번 더 검증**한다.
- SystemRDL / IP-XACT → RTL·UVM RAL·C 헤더·문서로 이어지는 업계 흐름 안에서 이 작은 도구가 어디에 해당하는지 설명할 수 있다.

## 1. 문제

first silicon이 돌아온 날, 검증 엔지니어가 가장 먼저 믿어야 하는 것은 "내 헤더의 오프셋·리셋값이 RTL과 같다"는 사실이다 — Rivian/NVIDIA 검증 면접의 단골 질문 "레지스터 정의를 RTL과 어떻게 동기화하나?"의 답이 바로 이 레슨이다.

레지스터 맵은 RTL 설계자, 프리실리콘 DV(UVM), 펌웨어, 포스트실리콘 검증, 데이터시트 작성자가 **모두** 쓰는 계약서다. 각 팀이 PDF를 보고 손으로 `#define` 을 옮기면 반드시 어긋난다 (오프셋 하나 밀림, 필드 폭 하나 틀림, 리셋값 오타). 그래서 업계는 기술서 한 곳만 고치고 나머지는 **전부 생성**한다: SystemRDL(Accellera)이나 IP-XACT(IEEE 1685) 같은 표준 기술서, 혹은 회사 자체 스프레드시트/YAML/JSON → 생성기 → RTL 레지스터 블록, UVM RAL 모델, C 헤더, 문서.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="sivb01-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">단일 소스 → lint → 생성 (이 레슨의 파이프라인)</text>
  <rect x="10" y="90" width="150" height="72" rx="8" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="85" y="114" font-weight="bold">regmap.json</text>
    <text x="85" y="134">단일 소스</text>
    <text x="85" y="152" font-size="12">(SystemRDL·IP-XACT 대용)</text>
  </g>
  <rect x="200" y="100" width="110" height="50" rx="8" class="f-warn" stroke="currentColor"/>
  <rect x="350" y="100" width="110" height="50" rx="8" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="255" y="122" font-weight="bold">lint</text><text x="255" y="140" font-size="12">Part B</text>
    <text x="405" y="122" font-weight="bold">codegen</text><text x="405" y="140" font-size="12">Part C</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#sivb01-arr)">
    <path d="M160 125 L198 125"/><path d="M310 125 L348 125"/>
    <path d="M460 125 L508 58"/><path d="M460 125 L508 104"/><path d="M460 125 L508 150"/><path d="M460 125 L508 196"/>
    <path d="M255 150 L255 196"/>
  </g>
  <g stroke="currentColor">
    <rect x="510" y="40" width="220" height="36" rx="6" class="f-mut"/>
    <rect x="510" y="86" width="220" height="36" rx="6" class="f-mut"/>
    <rect x="510" y="132" width="220" height="36" rx="6" class="f-mut"/>
    <rect x="510" y="178" width="220" height="36" rx="6" class="f-mut" stroke-dasharray="4 3"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="520" y="63">spi0_regs.h  #define OFFSET/MASK</text>
    <text x="520" y="109">check_header.c  _Static_assert</text>
    <text x="520" y="155">SIV-B03 리셋 · B04 R/W 테스트</text>
    <text x="520" y="201">(업계) RTL · UVM RAL · 문서</text>
  </g>
  <text x="200" y="214" fill="currentColor" font-size="12">거부 → 헤더 안 씀, exit 1</text>
  <text x="10" y="252" fill="currentColor">한 곳만 고치고 나머지는 생성한다. 손으로 고친 헤더가 실리콘과 어긋나는 첫 번째 원인이다.</text>
</svg>
```

이 레슨의 IP는 가상의 **SPI0** 컨트롤러다 (base `0x4001_3000`, 14개 레지스터 — SIV 트랙의 다른 레슨도 비슷한 블록을 쓴다). 기술서 형식은 이렇다:

| 키 | 위치 | 의미 |
|---|---|---|
| `block`, `base`, `size`, `version` | 최상위 | 블록 이름(C 접두사), 베이스 주소, 주소 공간 크기, 기술서 개정판 |
| `name`, `offset`, `desc`, `reset`(선택) | 레지스터 | 레지스터 이름, 바이트 오프셋, 설명, 스프레드시트에 적힌 레지스터 리셋값(교차 검증용) |
| `name`, `bits`, `access`, `reset`, `volatile`(선택) | 필드 | `"11:8"` 또는 `"5"`, `RW/RO/WO/W1C/W1S/RC`, 필드 리셋값, HW가 바꾸는 비트인지 |

CTRL 레지스터는 이렇게 생겼다. 필드 리셋값을 제자리로 밀어 OR 하면 레지스터 리셋값이 된다.

```svg
<svg viewBox="0 0 760 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">SPI0.CTRL @ 0x004 (reset 0x0007_0302)</text>
  <g fill="currentColor" font-size="12" text-anchor="middle" opacity=".75">
    <text x="50" y="50">31</text><text x="100" y="50">21</text><text x="125" y="50">20</text><text x="245" y="50">16</text>
    <text x="275" y="50">15</text><text x="365" y="50">12</text><text x="395" y="50">11</text><text x="485" y="50">8</text>
    <text x="515" y="50">7</text><text x="545" y="50">6</text><text x="575" y="50">5</text><text x="605" y="50">4</text>
    <text x="635" y="50">3</text><text x="665" y="50">2</text><text x="695" y="50">1</text><text x="725" y="50">0</text>
  </g>
  <g stroke="currentColor">
    <rect x="40" y="58" width="70" height="34" class="f-mut"/>
    <rect x="110" y="58" width="150" height="34" class="f-acc"/>
    <rect x="260" y="58" width="120" height="34" class="f-mut"/>
    <rect x="380" y="58" width="120" height="34" class="f-acc"/>
    <rect x="500" y="58" width="60" height="34" class="f-mut"/>
    <rect x="560" y="58" width="30" height="34" class="f-acc"/><rect x="590" y="58" width="30" height="34" class="f-acc"/>
    <rect x="620" y="58" width="30" height="34" class="f-acc"/><rect x="650" y="58" width="30" height="34" class="f-acc"/>
    <rect x="680" y="58" width="30" height="34" class="f-acc"/><rect x="710" y="58" width="30" height="34" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="75" y="80">rsvd</text><text x="185" y="80">FSIZE</text><text x="320" y="80">rsvd</text>
    <text x="440" y="80">CLKDIV</text><text x="530" y="80">rsvd</text>
    <text x="575" y="80">LB</text><text x="605" y="80">LS</text><text x="635" y="80">PH</text>
    <text x="665" y="80">PO</text><text x="695" y="80">MS</text><text x="725" y="80">EN</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="75" y="112">-</text><text x="185" y="112">7</text><text x="320" y="112">-</text><text x="440" y="112">3</text>
    <text x="530" y="112">-</text><text x="575" y="112">0</text><text x="605" y="112">0</text><text x="635" y="112">0</text>
    <text x="665" y="112">0</text><text x="695" y="112">1</text><text x="725" y="112">0</text>
  </g>
  <text x="10" y="112" fill="currentColor" font-size="12">reset</text>
  <g fill="currentColor" font-family="monospace" font-size="12.5">
    <text x="10" y="148">reset   = (0x7 &lt;&lt; 16) | (0x3 &lt;&lt; 8) | (1 &lt;&lt; 1)         = 0x0007_0302</text>
    <text x="10" y="172">defined = 0x1F&lt;&lt;16 | 0xF&lt;&lt;8 | 0x3F                     = 0x001F_0F3F</text>
  </g>
  <text x="10" y="200" fill="currentColor" font-size="12">LB=LOOPBACK LS=LSBF PH=CPHA PO=CPOL MS=MSTR · 모두 RW · 회색(rsvd) = 어떤 필드에도 속하지 않은 비트</text>
  <text x="10" y="220" fill="currentColor" font-size="12">rsvd 비트는 리셋값 비교·bit-bash 대상에서 빠진다 (SIV-B03/B04) — 그래서 defined 마스크를 같이 생성한다.</text>
</svg>
```

### Part A — 기술서 파싱과 레지스터 요약

- `parse_bits(s)` — `"11:8"`, `"[11:8]"`, `"5"` → `(lsb, width)`. `msb < lsb`, 숫자 아님 → `ValueError`.
- `field_mask(lsb, width)` — `width` 개의 1을 `lsb` 만큼 민 마스크.
- `reg_summary(fields)` — 필드 목록 → `reset`, `defined`, access별 마스크(`rw ro wo w1c w1s rc`), `vol`(volatile 비트).

### Part B — lint: 잘못된 기술서를 거부한다

`lint(raw)` 는 에러 문자열 목록을 돌려준다 (빈 목록 = 통과). 각 줄은 `E_코드` 로 시작하고 **레지스터.필드[비트]** 를 담아, 기술서를 고칠 사람이 바로 찾아갈 수 있어야 한다.

- 필드 수준(`lint_fields`): `E_WIDTH`(msb > 31), `E_OVERLAP`(필드 겹침, 겹친 비트 마스크까지), `E_RESET`(리셋값이 필드 폭에 안 들어감), `E_ACCESS`(모르는 access), `E_RESETMISMATCH`(적힌 레지스터 리셋 ≠ 필드로 조합한 값), `E_IDENT`/`E_DUPNAME`/`E_BITS`.
- 레지스터 수준(`lint`): `E_ALIGN`(4바이트 정렬 아님), `E_RANGE`(블록 크기 밖), `E_DUPOFF`(두 레지스터가 같은 오프셋).
- 음성 테스트: `bad_overlap.json`(rev 1.1 초안 — CLKDIV를 `[11:4]` 로 넓히면서 LSBF/LOOPBACK을 안 옮김), `bad_width.json`(손으로 옮기다 틀린 기술서), 그리고 정상 기술서를 한 군데씩 망가뜨린 변형(mutation)들.

### Part C — C 헤더 생성과 컴파일러 교차 검증

- `gen_header(raw)` — lint 에러가 있으면 `LintError`, 없으면 헤더 문자열. 출력은 **결정적**이어야 한다 (타임스탬프·절대 경로 금지) → CI가 "커밋된 `spi0_regs.h` 가 기술서와 같은가" 를 문자열 비교로 검사할 수 있다.
- `check_header.c` — 생성된 헤더를 include 해서 `_Static_assert`(데이터시트 값 고정 + 필드 겹침) 와 런타임 테이블 일관성 검사를 strict `cc` 로 돌린다.
- 시나리오: 설계자가 rev 1.1 초안을 올렸다. codegen이 거부하는 메시지, 그리고 누군가 `--force` 로 lint를 우회했을 때 C 쪽 `_Static_assert` 가 빌드를 깨는 것까지 확인한다.

## 2. Problem (English)

> **Register map codegen with lint.**
>
> An IP block's registers are described in a JSON file: block name, base address, size, and a list of registers, each with an
> `offset` and a list of fields (`bits` as `"msb:lsb"` or `"bit"`, `access` in `RW/RO/WO/W1C/W1S/RC`, `reset`, optional `volatile`).
> Using only the Python 3 standard library:
>
> 1. `parse_bits(s) -> (lsb, width)` and `field_mask(lsb, width)`; `reg_summary(fields)` returning the register reset value
>    (composed from field resets), the defined-bit mask, one mask per access type, and the volatile mask.
> 2. `lint(regmap) -> list[str]`: reject overlapping fields (report both fields and the overlapping bits), fields beyond bit 31,
>    reset values that do not fit their field, unknown access types, misaligned / out-of-range / duplicate offsets, and a
>    register-level `reset` that disagrees with its fields. An empty list means the description is valid.
> 3. `gen_header(regmap) -> str`: raise `LintError` if lint fails; otherwise emit a deterministic C header with
>    `<BLK>_<REG>_OFFSET/_RESET/_DEFINED_MASK/_<ACCESS>_MASK/_VOLATILE_MASK` and per field `_SHIFT/_WIDTH/_MASK/_RESET/_GET(r)/_PREP(v)`,
>    plus a `static const` register-descriptor table guarded by a macro. Prove the header compiles under
>    `cc -std=c11 -Wall -Wextra -Werror` with `_Static_assert`s on a few datasheet values.
>
> Example: field `CLKDIV` at `"11:8"` with reset 3 → `#define SPI0_CTRL_CLKDIV_MASK 0x00000F00u`, and CTRL's reset is `0x00070302`.
> Changing `CLKDIV` to `"11:4"` must be rejected with `E_OVERLAP CTRL.CLKDIV[11:4] overlaps CTRL.LSBF[4] at bits 0x00000010`.

## 3. 시작 코드

<!-- code: starter.py -->

기술서(단일 소스). 음성 테스트용 `bad_overlap.json` 은 이 파일에서 `version` 과 CLKDIV `bits` 두 줄만 다르다.

<!-- code: regmap.json -->

<!-- code: bad_width.json -->

```bash
cd lessons/SIV/SIV-B01
python3 -I starter.py          # TODO 상태: 5/37 passed, exit 1
```

실제 출력 (일부):

```text
-- Part A: parse --
FAIL  parse_bits('11:8') == (8, 4)
...
-- Part B: lint --
PASS  good regmap: lint == []
FAIL  bad_overlap: E_OVERLAP CTRL.CLKDIV vs LSBF
...
PASS  bad_width: TXTHR[31:28] (msb 31) is legal
...
PASS  lowercase name -> E_IDENT
PASS  dup register name -> E_DUPNAME
-- Part C: codegen --
FAIL  header has include guard
...
PASS  gen twice -> identical (deterministic)
FAIL  gen(bad_overlap) raises LintError
FAIL  committed spi0_regs.h is up to date (CI stale check)
5/37 passed
```

> 눈여겨볼 것: **아무것도 안 하는 stub lint(`return []`)도 "정상 기술서 → 에러 0개" 테스트는 통과한다.** 그래서 lint는 반드시 *망가진 입력이 거부되는지* 로 검증해야 한다. "빈 헤더를 두 번 생성 → 같다" 도 마찬가지로 공짜 PASS다. (IDENT/DUPNAME 레지스터 이름 검사는 starter에 이미 들어 있다.)

## 4. 힌트

<details>
<summary>힌트 1 — 비트 범위 파싱</summary>

정규식 하나로 끝난다: `^\s*\[?\s*(\d+)\s*(?::\s*(\d+)\s*)?\]?\s*$`. 두 번째 그룹이 없으면 `lsb = msb`. `width = msb - lsb + 1`. `(\d+)` 만 받으므로 음수는 자동으로 거부된다.

</details>

<details>
<summary>힌트 2 — 겹침 검사는 마스크 AND</summary>

필드마다 `m = field_mask(lsb, width)` 를 만들고, 이미 자리 잡은 필드 목록 `used` 의 각 마스크와 `m & pm` 을 본다. 0이 아니면 겹침이고, 그 값이 곧 "겹친 비트"다. 어느 필드와 겹쳤는지 보고하려면 누적 OR 하나가 아니라 (이름, 마스크) 목록을 들고 다닌다.

</details>

<details>
<summary>힌트 3 — 폭 초과는 Python이 알려주지 않는다</summary>

C에서 `1u << 36` 은 UB지만 Python 정수는 무한 정밀도라 `field_mask(32, 5)` 가 그냥 `0x1F00000000` 을 만든다. 그래서 `lsb + width > 32` 를 **명시적으로** 검사하고, 그 필드는 이후 검사(겹침·리셋)를 건너뛴다 (`continue`) — 엉터리 마스크로 두 번째 에러를 만들지 않기 위해.

</details>

<details>
<summary>힌트 4 — 리셋값 넘침</summary>

`reset >> width != 0` 이면 폭에 안 들어간다 (예: 4비트 필드에 `0x13`). 그리고 레지스터에 `reset` 이 적혀 있으면 필드 리셋을 `(rv << lsb) & m` 으로 조합한 값과 비교한다 — 스프레드시트의 "레지스터 리셋" 열과 "필드 리셋" 열이 어긋나는 일은 실제로 흔하다.

</details>

<details>
<summary>힌트 5 — 결정적 출력</summary>

레지스터는 오프셋 순, 필드는 lsb 순으로 정렬하고 (`normalize` 가 해 준다), 생성 시각·사용자·절대 경로를 헤더에 넣지 않는다. `"%-34s"` 로 열 맞춤까지 고정하면 같은 입력 → 바이트 단위로 같은 출력.

</details>

## 5. 풀이

### Part A — 비트 범위 표기와 레지스터 요약

기술서마다 비트 범위 표기가 다르다. 변환 규칙을 한 곳(`parse_bits`)에 모아 두는 게 핵심이다.

| 출처 | 표기 | `(lsb, width)` 로 바꾸면 |
|---|---|---|
| 데이터시트·SystemRDL | `[11:8]` (msb:lsb) | `(8, 4)` |
| IP-XACT | `bitOffset=8`, `bitWidth=4` | 그대로 |
| CMSIS 헤더 | `_Pos = 8`, `_Msk = 0xFUL << 8` | `lsb = _Pos`, `width = popcount(_Msk)` |
| 이 레슨 JSON | `"11:8"`, `"5"`, `"[31:0]"` | `(8,4)`, `(5,1)`, `(0,32)` |

`reg_summary` 는 필드 하나하나를 레지스터 단위 마스크로 접는다. 실제 값 (테스트로 확인한 것):

| 레지스터 | reset | defined | access 마스크 | vol |
|---|---|---|---|---|
| ID | `0x53500102` | `0xFFFFFFFF` | ro `0xFFFFFFFF` | 0 |
| CTRL | `0x00070302` | `0x001F0F3F` | rw `0x001F0F3F` | 0 |
| STATUS | `0x00000002` | `0x0000000F` | ro `0xF` | `0xF` |
| INT_RAW | `0` | `0x7` | w1c `0x7` | `0x7` |
| ERR_CNT | `0` | `0xFFFF` | rc `0xFFFF` | `0xFFFF` |

왜 이 마스크들이 필요한가 — **생성된 값이 그대로 다음 테스트의 기대값이 된다**:

- `RESET` + `DEFINED_MASK` + `VOLATILE_MASK` → 리셋값 테스트 (SIV-B03): `(read ^ RESET) & DEFINED & ~VOLATILE` 가 0이어야 한다. STATUS.TXE처럼 HW가 바로 바꿀 수 있는 비트, RXDATA처럼 읽으면 FIFO가 빠지는 레지스터를 같은 규칙으로 비교하면 거짓 실패가 난다.
- access별 마스크 → write/read 기대값 계산 (SIV-B04), bit-bash에서 W1C·RC 레지스터를 어떻게 다룰지 결정.
- 리셋값을 **필드 수준**에서 기술하고 레지스터 값은 *계산* 한다. 둘 다 사람이 적으면 언젠가 어긋난다 (Part B의 `E_RESETMISMATCH`).

> 프리실리콘의 대응물: UVM RAL의 `uvm_reg_hw_reset_seq` 는 생성된 RAL 모델의 리셋값을 기대값으로 쓴다. 포스트실리콘 C 테스트도 같은 기술서에서 나온 헤더를 쓰면, 시뮬레이션과 실리콘이 **같은 기대값** 으로 비교된다. 그래야 "시뮬에선 통과, 실리콘에선 실패" 가 기대값 문제가 아니라 진짜 실리콘 문제가 된다.

### Part B — lint: 어떤 기술서 버그가 어떤 실리콘 버그가 되나

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">bad_overlap.json: CTRL.CLKDIV 를 [11:4] 로 넓힌 rev 1.1 초안</text>
  <g fill="currentColor" font-size="12" text-anchor="middle" opacity=".75">
    <text x="120" y="48">11</text><text x="240" y="48">8</text><text x="280" y="48">7</text><text x="320" y="48">6</text>
    <text x="360" y="48">5</text><text x="400" y="48">4</text><text x="440" y="48">3</text><text x="480" y="48">2</text>
    <text x="520" y="48">1</text><text x="560" y="48">0</text>
  </g>
  <text x="10" y="76" fill="currentColor" font-size="12">used</text>
  <g stroke="currentColor">
    <rect x="100" y="56" width="240" height="30" fill="none" stroke-dasharray="3 3"/>
    <rect x="340" y="56" width="40" height="30" class="f-acc"/><rect x="380" y="56" width="40" height="30" class="f-acc"/>
    <rect x="420" y="56" width="40" height="30" class="f-acc"/><rect x="460" y="56" width="40" height="30" class="f-acc"/>
    <rect x="500" y="56" width="40" height="30" class="f-acc"/><rect x="540" y="56" width="40" height="30" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="220" y="76" opacity=".6">(아직 비어 있음)</text>
    <text x="360" y="76">LB</text><text x="400" y="76">LS</text><text x="440" y="76">PH</text>
    <text x="480" y="76">PO</text><text x="520" y="76">MS</text><text x="560" y="76">EN</text>
  </g>
  <text x="10" y="122" fill="currentColor" font-size="12">새 필드</text>
  <rect x="100" y="102" width="320" height="30" class="f-warn" stroke="currentColor"/>
  <text x="260" y="122" fill="currentColor" text-anchor="middle">CLKDIV[11:4]  m = 0x0FF0</text>
  <rect x="340" y="140" width="80" height="22" class="f-warn" stroke="currentColor"/>
  <text x="380" y="156" fill="currentColor" text-anchor="middle" font-size="12">겹침!</text>
  <text x="430" y="156" fill="currentColor" font-size="12">m &amp; used = 0x0FF0 &amp; 0x003F = 0x0030</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="10" y="190">E_OVERLAP CTRL.CLKDIV[11:4] overlaps CTRL.LSBF[4] at bits 0x00000010</text>
    <text x="10" y="210">E_OVERLAP CTRL.CLKDIV[11:4] overlaps CTRL.LOOPBACK[5] at bits 0x00000020</text>
    <text x="10" y="230">E_RESETMISMATCH CTRL: register reset 0x00070302 != 0x00070032 composed from fields</text>
  </g>
</svg>
```

겹침 검사는 필드마다 `used` 목록 전체와 AND 하므로 레지스터당 O(n²)지만 n ≤ 32라 문제없다. 누적 OR 하나(`used_mask`)로도 "겹쳤다" 는 알 수 있지만 **누구와** 겹쳤는지 말할 수 없다. 검증 도구의 에러 메시지는 "고칠 사람이 바로 찾아가는 주소" 여야 한다.

| 에러 코드 | 기술서에서 일어나는 일 | 그냥 생성했다면 실리콘/테스트에서 생기는 일 |
|---|---|---|
| `E_OVERLAP` | 필드 폭을 넓히고 이웃 필드를 안 옮김 | `PREP(CLKDIV)` 가 LSBF/LOOPBACK을 같이 써서 SPI가 LSB-first + loopback으로 동작. "가끔 데이터가 뒤집힌다" 류의 디버깅 지옥 |
| `E_WIDTH` | 64비트 레지스터에서 복사, 오타 `36:32` | C 매크로 `0x1F00000000u` → 32비트 대입 시 잘림(조용히 0), 또는 시프트 UB |
| `E_RESET` | 필드 폭보다 큰 리셋값 (`0x13` in 4 bits) | 리셋 테스트 기대값이 틀려서 **좋은 실리콘을 불량으로** 판정 |
| `E_RESETMISMATCH` | 레지스터 열과 필드 열이 어긋난 스프레드시트 | 펌웨어 팀과 검증 팀이 서로 다른 리셋값을 믿음 |
| `E_ALIGN` / `E_RANGE` | `0x02E`, 블록 밖 `0x040` | 비정렬 32비트 접근 → 버스 에러(AXI SLVERR / DECERR) 또는 다른 블록 레지스터 접근 |
| `E_DUPOFF` | 두 레지스터를 같은 주소에 기술 | 기술서 자체의 aliasing — 디코더 aliasing(SIV-B06)과 구분 불가해짐 |
| `E_ACCESS` | `RWX`, `RW1C` 같은 비표준 표기 | 생성기가 RW로 취급 → W1C 레지스터를 bit-bash 하다 인터럽트 상태를 지움 |

실제 lint 출력 (두 bad 파일, `--emit` 경로로 실행):

```text
$ python3 -I solution.py --src bad_overlap.json --emit /tmp/SIV-B01_bad.h > /dev/null; echo "exit=$?"
lint: E_OVERLAP CTRL.CLKDIV[11:4] overlaps CTRL.LSBF[4] at bits 0x00000010
lint: E_OVERLAP CTRL.CLKDIV[11:4] overlaps CTRL.LOOPBACK[5] at bits 0x00000020
lint: E_RESETMISMATCH CTRL: register reset 0x00070302 != 0x00070032 composed from fields
codegen: bad_overlap.json rejected (3 lint errors), header NOT written
exit=1

$ python3 -I solution.py --src bad_width.json --emit /tmp/SIV-B01_w.h >/dev/null; echo "exit=$?"
lint: E_RESET CTRL.CLKDIV: reset 0x13 does not fit in 4 bit(s) (max 0xF)
lint: E_WIDTH CTRL.FSIZE[36:32] exceeds 32-bit register (msb 36 > 31)
lint: E_ACCESS DMA_CTRL.MODE: unknown access 'RWX' (allowed RW/RO/WO/W1C/W1S/RC)
codegen: bad_width.json rejected (3 lint errors), header NOT written
exit=1
```

경계값도 테스트한다: `TXTHR[31:28]` (msb = 31) 는 **합법**이어야 하고, `[31:0]` 32비트 필드도 통과해야 한다. lint가 너무 엄격하면 (`msb >= 31` 같은 off-by-one) 정상 기술서를 거부해서 팀이 lint를 끄게 된다 — 그게 더 나쁘다.

**변형 테스트(mutation test)**: 정상 기술서를 `copy.deepcopy` 하고 한 군데만 망가뜨린다 (SCRATCH 오프셋을 `0x030`/`0x02E`/`0x040` 으로, 필드 이름을 소문자로, 레지스터 이름 중복). 각 변형마다 *정확한 에러 코드와 이름* 이 나오는지 본다. 이것이 lint 자체의 "fault injection" 이다 — 검증 도구도 검증 대상이다.

### Part C — 생성된 헤더와 컴파일러 교차 검증

생성된 `spi0_regs.h` 중 CTRL 부분 (실제 파일에서 발췌, `...` 는 생략):

```c
/* ---- CTRL @ 0x004 : 제어 ---- */
#define SPI0_CTRL_OFFSET                   0x004u
#define SPI0_CTRL_ADDR                     (SPI0_BASE + SPI0_CTRL_OFFSET)
#define SPI0_CTRL_RESET                    0x00070302u
#define SPI0_CTRL_DEFINED_MASK             0x001F0F3Fu
#define SPI0_CTRL_RW_MASK                  0x001F0F3Fu
...
#define SPI0_CTRL_CLKDIV_SHIFT             8u
#define SPI0_CTRL_CLKDIV_WIDTH             4u
#define SPI0_CTRL_CLKDIV_MASK              0x00000F00u
#define SPI0_CTRL_CLKDIV_RESET             0x3u
#define SPI0_CTRL_CLKDIV_GET(r)            ((((uint32_t)(r)) & SPI0_CTRL_CLKDIV_MASK) >> SPI0_CTRL_CLKDIV_SHIFT)
#define SPI0_CTRL_CLKDIV_PREP(v)           ((((uint32_t)(v)) << SPI0_CTRL_CLKDIV_SHIFT) & SPI0_CTRL_CLKDIV_MASK)
```

생성 규칙에 담긴 결정들:

| 결정 | 이유 |
|---|---|
| 모든 상수에 `u` 접미사, 마스크는 8자리 16진수 | `0x80000000` 비교에서 signed/unsigned 혼동 방지, grep 가능 (C-B01, C-B02) |
| `_SHIFT` + `_MASK` 둘 다 | CMSIS `_Pos/_Msk` 관례, `_GET/_PREP` 는 Linux `FIELD_GET/FIELD_PREP` 와 같은 모양 (C-B04 참고) |
| `_PREP` 가 마지막에 `& MASK` | 넘치는 값이 이웃 필드를 덮지 않음: `PREP(0x1F) == 0xF00` |
| 비트필드 구조체 대신 매크로 | 비트필드 배치·접근 폭은 구현 정의 (C11 6.7.2.1p11) |
| 테이블은 `#ifdef SPI0_REGS_WANT_TABLE` 안의 `static const` | 헤더를 여러 .c가 include해도 테이블은 원하는 번역 단위에만 생긴다 |
| 타임스탬프·절대 경로 없음 | 결정적 출력 → CI stale check, 깨끗한 git diff |

**CI stale check**: 테스트 `committed spi0_regs.h is up to date` 는 커밋된 헤더와 지금 생성한 문자열을 비교한다. 누군가 기술서만 고치고 재생성을 안 했거나, 헤더를 손으로 고쳤으면 CI가 빨개진다. 생성물을 커밋할지(리뷰에서 diff가 보임) 빌드 때만 만들지(stale 불가능)는 팀 선택이지만, 커밋한다면 이 검사는 필수다.

**컴파일러로 한 번 더 (defense in depth)**: `check_header.c` 는 생성기와 독립적으로 사람이 데이터시트에서 확인한 값을 `_Static_assert` 로 못 박고, 필드 겹침을 C 쪽에서 다시 검사한다. 겹침 검사 트릭: 마스크들이 서로소이면 **합 == OR** 이고, 하나라도 겹치면 자리올림 때문에 합이 더 크다.

```bash
python3 -I solution.py --emit spi0_regs.h
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 문제 회피
cc -std=c11 -Wall -Wextra -Werror -O2 check_header.c -o /tmp/SIV-B01_hdr && /tmp/SIV-B01_hdr
```

```text
PASS  table_consistent()
PASS  SPI0_CTRL_ADDR == 0x40013004u
PASS  SPI0_CTRL_CLKDIV_GET(SPI0_CTRL_RESET) == 3u
PASS  SPI0_CTRL_FSIZE_GET(SPI0_CTRL_RESET) == 7u
PASS  SPI0_CTRL_CLKDIV_PREP(0x1Fu) == 0x00000F00u
PASS  SPI0_ID_PART_GET(SPI0_ID_RESET) == SPI0_ID_PART_RESET
PASS  (SPI0_CTRL_EN_PREP(1) | SPI0_CTRL_MSTR_PREP(1) | SPI0_CTRL_CLKDIV_PREP(5) | SPI0_CTRL_FSIZE_PREP(15)) == 0x000F0503u
PASS  SPI0_REGS[3].w1c == SPI0_INT_RAW_W1C_MASK && SPI0_INT_RAW_W1C_MASK == 0x7u
PASS  SPI0_ERR_CNT_RC_MASK == SPI0_ERR_CNT_VOLATILE_MASK

reg        off    reset      defined    vol       
ID         0x000  0x53500102 0xFFFFFFFF 0x00000000
CTRL       0x004  0x00070302 0x001F0F3F 0x00000000
STATUS     0x008  0x00000002 0x0000000F 0x0000000F
INT_RAW    0x00C  0x00000000 0x00000007 0x00000007
...
SCRATCH    0x03C  0x00000000 0xFFFFFFFF 0x00000000
9/9 passed
```

(ASan/UBSan 빌드 `cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer check_header.c` 도 깨끗하게 exit 0.)

**시나리오 — rev 1.1 초안이 lint를 우회했다면**: `--force` 로 lint를 무시하고 헤더를 만든 뒤, `-DREGS_H_PATH` 로 그 헤더를 끼워 컴파일하면 (실제 출력, 노트 줄 일부 생략):

```text
$ python3 -I solution.py --src bad_overlap.json --force --emit /tmp/SIV-B01_bad.h
$ cc -std=c11 -Wall -Wextra -Werror -O2 -DREGS_H_PATH='"/tmp/SIV-B01_bad.h"' check_header.c -o /tmp/SIV-B01_badbin
check_header.c:23:16: error: static assertion failed due to requirement '458802U == 459522U': CTRL reset (datasheet 표 4-2)
check_header.c:34:16: error: static assertion failed due to requirement '(0ULL + 1U + 2U + 4U + 8U + 16U + 32U + 4080U + 2031616U) == (1U | 2U | 4U | 8U | 16U | 32U | 4080U | 2031616U)': CTRL fields overlap
check_header.c:34:32: note: expression evaluates to '2035759 == 2035711'
2 errors generated.
```

`2035759 - 2035711 = 48 = 0x30` — 겹친 비트 4·5(LSBF, LOOPBACK)가 정확히 한 번씩 더 더해졌다. 이렇게 **lint(생성 전) → `_Static_assert`(빌드 시) → 리셋/RW 테스트(실리콘에서, SIV-B03/B04) → ID 레지스터 확인(런타임, SIV-B10)** 으로 층을 쌓는다. 실리콘에 들어간 RTL이 기술서와 다르면(ECO로 tie-off만 바뀐 경우 등) 앞의 두 층은 못 잡고 B03 리셋값 테스트가 잡는다 — 그래서 이 헤더의 `RESET` 이 B03의 기대값이 된다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py                     # 37/37 passed, exit 0
python3 -I solution.py --emit spi0_regs.h  # 헤더도 다시 쓴다 (커밋된 파일과 같아야 함)
```

C 쪽 교차 검증 (§5 Part C 출력 참고):

<!-- code: check_header.c -->

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 정상 기술서 → `lint == []` 하나만 테스트 | stub `return []` 도 통과한다 | `bad_*.json` + 변형마다 **정확한 코드·이름** 을 CHECK |
| 겹침을 누적 OR 하나로만 검사 | "겹쳤다" 만 알고 누구와·어느 비트인지 못 말함 | (이름, 마스크) 목록과 `m & pm` 을 비교해 보고 |
| `msb >= 31` 이면 E_WIDTH | off-by-one: 비트 31은 합법 | `lsb + width > 32` |
| E_WIDTH 뒤에도 겹침·리셋 검사 계속 | 엉터리 마스크로 2차 에러가 쏟아짐 | 그 필드는 `continue` |
| 리셋값을 레지스터 수준에서만 기술 | 필드가 바뀌어도 리셋값이 안 따라감 | 필드 리셋에서 **계산**, 레지스터 값은 교차 검증용 |
| 헤더에 `/* generated 2026-11-08 14:03 by don */` | 매번 diff 발생 → stale check 불가 | 입력만으로 결정되는 내용만 출력 |
| `#define X_MASK (0xF << 8)` (u 없음, signed) | `0xF << 28` 은 int 오버플로 UB, 비교 시 부호 문제 | `0xF0000000u` 처럼 계산된 리터럴 + `u` |
| `_PREP(v)` 에서 `& MASK` 생략 | 넘치는 값이 이웃 필드 침범 | `(((uint32_t)(v) << S) & M)` |
| 헤더에 `static const` 테이블을 무조건 정의 | 모든 .c 에 사본 + unused 경고 | `#ifdef <BLK>_REGS_WANT_TABLE` 로 감싸기 |
| 생성된 헤더를 손으로 고침 | 다음 생성 때 사라지거나 기술서와 영원히 어긋남 | 기술서를 고치고 재생성, CI stale check |

## 8. 면접 Q&A

<details>
<summary>Q1. 레지스터 정의(#define)를 RTL과 어떻게 동기화하나요?</summary>

단일 소스 기술서(SystemRDL / IP-XACT / 사내 YAML·스프레드시트)에서 RTL 레지스터 블록, UVM RAL 모델, C 헤더, 문서를 **모두 생성**한다. 생성 전 lint, 생성물 커밋 시 CI stale check, 빌드 시 `_Static_assert`, 실리콘에서는 ID/VERSION 레지스터를 읽어 헤더가 기대하는 rev와 맞는지 확인한다.
꼬리질문: "RTL은 생성기를 안 쓰고 손으로 짰다면?" → 리셋값 테스트(uvm_reg_hw_reset_seq / SIV-B03)와 access 테스트(uvm_reg_access_seq / bit-bash)를 프리실리콘에서 돌려 기술서-RTL 불일치를 테이프아웃 전에 잡는다. 포스트실리콘에서 처음 잡으면 ECO나 errata 문서로 가야 한다.

</details>

<details>
<summary>Q2. SystemRDL과 IP-XACT의 차이는? 실무에서 이 JSON 같은 걸 직접 만드는 이유는?</summary>

SystemRDL(Accellera)은 사람이 쓰는 텍스트 언어로 레지스터·필드·access 속성(sw/hw 접근, onread/onwrite 부작용 등)을 기술한다. IP-XACT(IEEE 1685)는 IP 패키징용 XML 스키마로 memory map/레지스터/필드를 담으며 도구 간 교환에 쓰인다. 오픈소스로는 SystemRDL 컴파일러(`systemrdl-compiler`)와 PeakRDL 계열 생성기(C 헤더, RTL regblock, UVM 등)가 있다. 회사마다 레거시 스프레드시트 → 자체 생성기도 흔하다. 검증 엔지니어는 그 생성기에 **새 출력(테스트 테이블, 디코더, diff 리포트)** 을 붙이는 일을 자주 한다 — 이 레슨의 JSON은 그 축소판이다.
꼬리질문: "IP-XACT에서 리셋값은 어디에?" → 레지스터/필드의 reset 요소(value, mask). 세부 스키마 버전마다 다르니 사용하는 버전 문서를 확인한다고 답하는 게 안전하다.

</details>

<details>
<summary>Q3. RC(read-clear) 레지스터는 리셋값 테스트에서 어떻게 다루나요?</summary>

기술서에서 RC 필드를 volatile(또는 "read side effect")로 표시하고, 생성된 `VOLATILE_MASK`/`RC_MASK` 로 리셋 비교에서 제외하거나 **첫 번째 읽기만** 리셋값과 비교하고 두 번째 읽기는 0을 기대한다. 디버거 메모리 창이나 레지스터 덤프 스크립트가 RC 레지스터를 읽어 카운터를 지워버리는 사고가 흔해서, 덤프 도구도 같은 마스크로 RC 레지스터를 건너뛴다.
꼬리질문: "RXDATA처럼 읽으면 FIFO가 빠지는 건?" → access는 RO지만 부작용이 있다. SystemRDL의 onread 같은 속성 또는 `volatile` + 별도 플래그로 표시해 bit-bash·리셋 테스트에서 빼고, 기능 테스트(루프백)에서 검증한다.

</details>

<details>
<summary>Q4. bit-bash 테스트가 W1C 레지스터를 망가뜨리지 않으려면 기술서에 무엇이 필요한가요?</summary>

access 타입이 필드 단위로 정확해야 한다. 생성기가 `W1C_MASK` 를 내보내면 bit-bash는 그 비트에 1을 써서 "지워지는지"만 확인하고 RW처럼 write-read 비교를 하지 않는다 (SIV-B04). UVM에서는 `uvm_reg_bit_bash_seq` 가 access 정책에 따라 기대값을 계산하고, 테스트하면 안 되는 레지스터는 리소스 DB의 `NO_REG_BIT_BASH_TEST` 같은 속성으로 제외한다. 연습 4의 `W_MIXED_W1C` 경고처럼 한 레지스터에 RW와 W1C가 섞여 있으면 펌웨어 RMW가 펜딩 플래그를 지우는 문제도 있다 (C-M02 참고).

</details>

<details>
<summary>Q5. lint도 통과했고 헤더도 맞는데 실리콘 리셋값이 다르면 무엇을 의심하나요?</summary>

① 기술서와 RTL 불일치 (RTL을 생성하지 않았거나 ECO로 tie-off가 바뀜) ② 리셋 도메인: 그 블록이 실제로 리셋을 받았나, 클럭이 켜져 있나 (클럭 게이트된 블록은 0, `0xDEADBEEF` 같은 버스 기본값, 또는 버스 에러) ③ 부트 ROM/펌웨어가 테스트 전에 이미 값을 바꿈 ④ volatile 비트를 비교에 넣음. 실패 리포트는 `reg=CTRL off=0x004 exp=0x00070302 act=... diff=... bits=[...]` 처럼 비트 단위로 남기고, 같은 기술서로 만든 프리실리콘 결과와 비교해 "RTL 문제 vs 실리콘(제조·타이밍) 문제" 를 가른다.

</details>

<details>
<summary>Q6. SPI0~SPI3 처럼 같은 IP가 여러 개면 헤더를 어떻게 생성하나요?</summary>

오프셋·필드는 IP 타입 단위(`SPI_CTRL_OFFSET`)로 한 번 생성하고, 인스턴스 베이스는 SoC 수준 기술서에서 별도로 생성한다 (`SPI0_BASE`, `SPI1_BASE` 또는 베이스 배열). 테스트는 `(base, desc_table)` 을 인자로 받아 모든 인스턴스에 같은 코드를 돌린다. 레지스터 배열(예: 채널별 CFG[8])은 stride와 개수를 기술서에 두고 `CFG_OFFSET(n)` 매크로를 생성한다.

</details>

<details>
<summary>Q7. 생성된 C 헤더를 왜 Python 테스트만으로 끝내지 않고 cc 로 다시 컴파일하나요?</summary>

펌웨어가 실제로 쓰는 것은 문자열이 아니라 **C 전처리기·컴파일러가 해석한 값** 이다. 매크로 괄호 누락, signed 리터럴, 이름 충돌, 64비트 상수 같은 문제는 C 컴파일러만 잡는다. 또 `_Static_assert` 는 생성기와 독립된 두 번째 의견(사람이 데이터시트에서 확인한 값)이라, 생성기 자체의 버그(예: 필드 정렬 실수)도 잡을 수 있다.

</details>

## 9. 추가 연습

### 연습 1 — 레지스터 값 디코더

`decode(raw, "CTRL", 0x00070303)` → `CTRL=0x00070303 EN=1 MSTR=1 CPOL=0 CPHA=0 LSBF=0 LOOPBACK=0 CLKDIV=0x3 FSIZE=0x7`. 정의되지 않은 비트가 1이면 `RSVD=0x...` 를 붙인다. 실패 리포트(SIV-B14)와 레지스터 덤프에 쓰는 도구다.

<details>
<summary>답 보기</summary>

같은 `normalize` 결과로 필드마다 `(value & mask) >> lsb`. 1비트는 10진, 여러 비트는 16진. `value & ~defined & 0xFFFFFFFF` 가 0이 아니면 RSVD — Python의 `~` 는 무한 정밀도 음수라 32비트로 잘라야 한다. reserved 비트가 1로 읽히는 것 자체가 버그 신호(디코드 불완전, 다른 레지스터 aliasing — SIV-B06)라 덤프에서 눈에 띄게 한다.

</details>

### 연습 2 — 기술서 개정판 diff

`diff_regmaps(old, new)` 가 `+REG / -REG / MOVED / +FIELD / -FIELD / FIELD [..] -> [..] / ACCESS / RESET` 줄을 이름 순으로 낸다. `regmap.json` → `bad_overlap.json` 은 정확히 `["FIELD CTRL.CLKDIV [11:8] -> [11:4]", "RESET CTRL 0x00070302 -> 0x00070032"]`.

<details>
<summary>답 보기</summary>

두 판을 정규화해 이름으로 dict를 만들고 합집합을 정렬해 돈다. 레지스터 리셋은 필드에서 *계산된* 값을 비교하므로 필드 이동만으로도 RESET 변화가 같이 보인다. 실무 용도: RTL rev가 올라올 때 "어느 테스트·드라이버 코드를 봐야 하나" 체크리스트, 그리고 실리콘 A0 → B0 스테핑 간 레지스터 변경 리뷰.

</details>

### 연습 3 — CMSIS 스타일 레지스터 구조체 생성

같은 기술서로 `SPI0_TypeDef` 구조체를 생성한다. 빈 주소는 `uint32_t RESERVED_034[2];` 로 채우고, 모든 필드가 RO/RC인 레지스터는 `const volatile`(CMSIS `__I`), 나머지는 `volatile`. 레지스터마다 `_Static_assert(offsetof(...))`, 마지막에 `sizeof == size`.

<details>
<summary>답 보기</summary>

오프셋 순으로 돌며 `pos` 와 다음 오프셋의 차이를 `RESERVED_<pos>[(off-pos)/4]` 로 메운다. offsetof 단언이 있으면 패딩 계산 실수가 컴파일 에러가 된다 (C-M01, C-B12 참고). 생성 후 실제 컴파일 확인:

```bash
python3 -I practice.py --emit-struct /tmp/SIV-B01_struct.h
cc -std=c11 -Wall -Wextra -Werror -fsyntax-only -x c /tmp/SIV-B01_struct.h; echo "cc exit=$?"   # cc exit=0
```

생성 결과 일부: `const volatile uint32_t STATUS; /* 0x008 */`, `uint32_t RESERVED_034[2];`, `_Static_assert(sizeof(SPI0_TypeDef) == 0x040, "SPI0 size");`.

</details>

### 연습 4 — 경고 lint (에러는 아니지만 위험)

`lint_warnings(raw)`: ① 한 레지스터에 W1C 필드와 RW/W1S 필드가 섞여 있으면 `W_MIXED_W1C` ② RC 필드인데 `volatile` 표시가 없으면 `W_RC_NOT_VOLATILE`.

<details>
<summary>답 보기</summary>

① 펌웨어가 RW 필드를 `reg = (reg & ~M) | v` 로 바꾸면 읽은 값의 W1C 1들을 다시 써서 펜딩 플래그가 지워진다 — 그래서 좋은 IP는 RAW(W1C)와 EN(RW)을 다른 레지스터로 나눈다 (이 SPI0도 그렇다). ② RC를 volatile로 표시 안 하면 리셋값 테스트가 두 번 읽거나 덤프 도구가 읽어 카운터를 지운다. 에러가 아닌 경고로 두는 이유: 실제 IP에 그런 레지스터가 존재할 수 있고(설계 선택), 그때는 테스트 쪽에서 특별 취급해야 한다는 신호이기 때문.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py                                   # 17/17 passed, exit 0
python3 -I practice.py --emit-struct /tmp/SIV-B01_struct.h && \
  cc -std=c11 -Wall -Wextra -Werror -fsyntax-only -x c /tmp/SIV-B01_struct.h
```

## 10. 핵심 정리

- 레지스터 맵은 **단일 소스** 기술서 → 생성기 → RTL·UVM RAL·C 헤더·문서. 손으로 옮긴 `#define` 은 어긋난다.
- 비트 범위 `[msb:lsb]` → `lsb`, `width = msb-lsb+1`, `mask = ((1<<width)-1) << lsb`. 리셋값은 **필드 수준** 에서 기술하고 레지스터 값은 계산.
- 생성할 것: `OFFSET/ADDR/RESET/DEFINED_MASK/<ACCESS>_MASK/VOLATILE_MASK`, 필드 `SHIFT/WIDTH/MASK/RESET/GET/PREP`, 기술 테이블 — 이것이 SIV-B03/B04 테스트의 기대값이 된다.
- lint: OVERLAP(누구와·어느 비트), WIDTH(msb ≤ 31, 31은 합법), RESET(폭 초과), RESETMISMATCH, ALIGN/RANGE/DUPOFF, ACCESS.
- lint는 **망가진 입력으로** 검증한다 (bad 파일 + mutation). stub `return []` 도 정상 입력 테스트는 통과한다.
- 출력은 결정적 → CI stale check. 생성물은 C 컴파일러로 다시 검증 (`_Static_assert`, 합 == OR 겹침 트릭).
- 층층이 방어: lint → `_Static_assert` → 실리콘 리셋/RW 테스트 → ID/VERSION 런타임 확인.

## 참고자료

- [Accellera SystemRDL 2.0 Register Description Language](https://www.accellera.org/downloads/standards/systemrdl)
- IEEE 1685 IP-XACT — memory map / register / field 기술 표준
- [SystemRDL Compiler (Python) 문서](https://systemrdl-compiler.readthedocs.io/) · [PeakRDL 생성기 모음](https://peakrdl.readthedocs.io/)
- IEEE 1800.2 UVM — 레지스터 레이어(RAL), `uvm_reg_hw_reset_seq` / `uvm_reg_bit_bash_seq` / `uvm_reg_access_seq`
- [Linux `include/linux/bitfield.h` — FIELD_GET / FIELD_PREP](https://github.com/torvalds/linux/blob/master/include/linux/bitfield.h)
- [ARM CMSIS-Core — 레지스터 `_Pos`/`_Msk` 정의와 `__I/__O/__IO`](https://arm-software.github.io/CMSIS_6/latest/Core/index.html)
- [Python `json` 모듈](https://docs.python.org/3/library/json.html)
- C11 6.10.2p4 (computed `#include`), 6.7.10 `_Static_assert`, 6.7.2.1p11 (비트필드 배치는 구현 정의)
- P. Mishra, F. Farahmandi (eds.), *Post-Silicon Validation and Debug*, Springer 2019
