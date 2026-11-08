---
id: C-A08
time: 110분
updated: 2026-11-08
---
# C-A08 컴파일 타임 계약: _Static_assert와 BUILD_BUG_ON

## 학습 목표

- `_Static_assert`(C11 6.7.10), `#if`/`#error`, `BUILD_BUG_ON`(문장), `BUILD_BUG_ON_ZERO`(식)가 **언제 평가되고, 어디에 쓸 수 있고, 무엇을 검사할 수 있는지** 구분해서 고른다.
- 정수 상수식(integer constant expression, 6.6p6)만으로 `IS_POW2`, `FIELD_MAX`, `MASK_IS_CONTIGUOUS`, `FIELD_FITS`, `FIELD_PREP_CONST` 를 만들고, 경계값(0, `0xFFFFFFFF`, 음수, 2^32)에서도 맞게 동작하게 한다.
- 와이어 프로토콜 구조체, 레지스터 블록, enum 범위, 표 길이, 설정값과 하드웨어 필드 폭 사이의 가정을 **스펙 표 한 줄 = 계약 한 줄** 로 고정한다.
- 계약 자체를 테스트한다: 일부러 필드를 추가하거나 순서를 바꾼 **돌연변이(mutant)** 가 빌드에서 죽는지 확인하고(`mutants.sh`), clang `-verify` 로 "이 줄은 이 에러가 나야 한다" 를 검사한다.
- 컴파일 타임에 못 잡는 것(외부에서 들어온 바이트, 런타임 값, 다른 타깃의 ABI)을 런타임 검증과 여러 타깃 빌드로 메운다.

## 1. 문제

펌웨어 코드는 데이터시트와 프로토콜 문서의 숫자에 기대어 산다. "헤더는 12바이트", "`LEN` 은 16비트", "링 크기는 2의 거듭제곱", "에러 코드는 3비트에 들어간다" 같은 가정이 코드 어디에도 적혀 있지 않으면, 누군가 필드 하나를 추가하는 순간 **조용히** 깨진다. 시니어 펌웨어 면접에서 "그 가정을 어떻게 지키나요?" 라고 물으면 기대하는 답은 "코멘트로 적어 둡니다" 가 아니라 **"빌드가 실패하게 만들고, 그 실패가 실제로 나는지도 테스트합니다"** 다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">가정이 깨진 것을 어디서 알게 되나: 왼쪽일수록 싸고, 오른쪽일수록 비싸다</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="110" height="54" rx="6" class="f-ok"/><rect x="130" y="40" width="130" height="54" rx="6" class="f-ok"/><rect x="270" y="40" width="110" height="54" rx="6" class="f-acc"/>
    <rect x="390" y="40" width="110" height="54" rx="6" class="f-acc"/><rect x="510" y="40" width="100" height="54" rx="6" class="f-warn"/><rect x="620" y="40" width="110" height="54" rx="6" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="20" y="62">전처리</text><text x="20" y="82" font-family="monospace" font-size="12">#if / #error</text><text x="140" y="62">컴파일</text><text x="140" y="82" font-family="monospace" font-size="12">_Static_assert</text>
    <text x="280" y="62">링크</text><text x="280" y="82" font-family="monospace" font-size="12">ld ASSERT()</text><text x="400" y="62">부팅</text><text x="400" y="82" font-size="12">self-test</text><text x="520" y="62">런타임</text>
    <text x="520" y="82" font-size="12">입력 검증</text><text x="630" y="62">필드(현장)</text><text x="630" y="82" font-size="12">RMA, 로그</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="122">CHAR_BIT, 엔디안</text><text x="10" y="140">(매크로 상수만)</text><text x="130" y="122">sizeof, offsetof, 타입,</text><text x="130" y="140">enum 범위, 표 길이, 마스크</text><text x="270" y="122">섹션 크기 ≤ RAM,</text>
    <text x="270" y="140">벡터 테이블 위치</text><text x="390" y="122">const 표 내용,</text><text x="390" y="140">비트필드 배치 (C-M03)</text><text x="510" y="122">외부 바이트,</text><text x="510" y="140">런타임 크기</text>
    <text x="620" y="122">"가끔 이상한 값"</text>
  </g>
  <rect x="10" y="160" width="720" height="128" rx="6" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="22" y="184">이 레슨: 가정 하나하나를 가능한 한 왼쪽(컴파일)으로 끌어온다. 못 끌어오는 것은 런타임 검증으로.</text><text x="22" y="208">그리고 계약이 진짜 작동하는지 확인한다 — 한 번도 실패해 본 적 없는 단언은 검증되지 않은 단언이다.</text>
    <text x="22" y="236" font-family="monospace" font-size="12">./mutants.sh solution.c   → 필드 추가·재배열·타입 변경·설정 변경 15개 → 15/15 killed</text>
    <text x="22" y="258" font-family="monospace" font-size="12">clang -Xclang -verify ...  → "이 줄은 이 메시지로 실패해야 한다" 를 자동 확인</text>
    <text x="22" y="280" font-family="monospace" font-size="12">--target=thumbv7em ...     → 계약은 "그 빌드" 만 검사한다: 타깃마다 돌려야 한다</text>
  </g>
</svg>
```

무대는 가상의 센서 노드 **SNX** 다. 호스트와 SPI 로 12바이트 헤더 패킷을 주고받고, SPI 수신은 DMA 디스크립터 링으로 받는다. 시작 코드에 정의(구조체·enum·`#define`)는 다 주어져 있다. 여러분이 만들 것은 **계약 도구**, **계약**, 그리고 계약이 정당화하는 **런타임 코드** 다.

| 대상 | 스펙 (문서가 말하는 것) |
|---|---|
| `pkt_hdr_t` | `sync`@0 u8 · `type`@1 u8 · `len`@2 u16 · `status`@4 u8 · `seq`@5 u8 · `rsvd`@6 u16 · `crc`@8 u32, 총 12바이트, 패딩 없음, LE |
| `status` 바이트 | `[2:0]` = `err_t`, `[3]` = ACK, `[7:4]` = 0 |
| `err_t` / `err_str[]` | `ERR_OK` … `ERR_HW` (7개) + `ERR_COUNT`. 문자열 표는 enum 과 1:1 |
| `SNX_SPI_TypeDef` | `CR1`@0x000 · `DR`@0x00C · `DMACR`@0x020 · `DESCADDR`@0x024 · `IDR`@0x3FC, 블록 1 KB |
| `CR1` 필드 | `CPHA`[0] `CPOL`[1] `MSTR`[2] `BR`[5:3] `SPE`[6] — 겹치지 않음 |
| `dma_desc_t` | `ctrl` `src` `dst` `next` 각 u32 (버스 주소), 16바이트, **16바이트 정렬**. `ctrl`: `LEN`[15:0] `IRQ`[16] `OWN`[31] |
| 설정 | `RING_N` = 8 (인덱스는 `& (RING_N-1)`), `RX_BUF_SIZE` = 512 (≤ `LEN` 최대, 4의 배수, 최대 패킷이 들어감), `SPI_BR_DIV` = 5 (`BR` 3비트) |

### Part A — 계약 도구 만들기 (전부 정수 상수식)

| # | 매크로 | 뜻 | 예 |
|---|---|---|---|
| A1 | `BUILD_BUG_ON(cond)` | 문장 자리. cond 가 참이면 빌드 실패 | `BUILD_BUG_ON(sizeof(pkt_hdr_t) != 12);` |
| A2 | `BUILD_BUG_ON_ZERO(cond)` | 식 자리. 거짓이면 `(size_t)0`, 참이면 빌드 실패 | `x + BUILD_BUG_ON_ZERO(...)` |
| A3 | `IS_POW2(x)` | 2의 거듭제곱 (0 은 아님). 음수·64비트도 UB 없이 | `IS_POW2(-8) == 0` |
| A4 | `LOWBIT(m)` | 가장 낮은 1 비트 | `0x38 → 0x08` |
| A5 | `FIELD_MAX(m)` | 필드 최댓값. `m == 0` 이면 0 | `0x38 → 7`, `0xFFFFFFFF → 0xFFFFFFFF` |
| A6 | `MASK_IS_CONTIGUOUS(m)` | 0 이 아니고 1 들이 한 덩어리 | `0x28 → 0`, `0xFFFFFFFF → 1` |
| A7 | `FIELD_FITS(m, v)` | v 가 필드에 들어가는가 (음수·2^32 이상은 거짓) | `FIELD_FITS(0x38, 8) == 0` |
| A8 | `FIELD_PREP_CONST(m, v)` | 상수 전용 필드 값. 마스크가 이상하거나 v 가 안 들어가면 **빌드 실패** | `(0x38, 5) → 0x28` |

컴파일러 내장 함수(`__builtin_ctz` 등)는 쓰지 않는다. 시프트 양을 모르니 **곱셈/나눗셈** 으로 해결한다.

### Part B — 와이어 프로토콜 · enum 범위 · 표 길이

- B1–B5: `MEMBER_SIZE(T, m)`, `FIELD_FOLLOWS(T, a, b)` (사이 패딩 없음), `ENDS_STRUCT(T, m)` (꼬리 패딩 없음), `IS_TYPE(e, type)` (`_Generic`), `PIN_FIELD(T, m, off, type)` (오프셋 + 타입을 한 번에 고정하는 `_Static_assert`).
- **B-계약**: 위 스펙 표의 `pkt_hdr_t` / `status` / `err_t` 줄을 모두 고정한다. 엔디안과 `CHAR_BIT` 가정도.
- B6: `int pkt_parse(const uint8_t *raw, size_t n, pkt_hdr_t *out)` — 계약 덕분에 `memcpy` 한 번으로 헤더를 읽을 수 있다. 하지만 **상대가 보낸 바이트는 계약이 아니다**: 길이, sync, type 범위, err 범위와 예약 비트, rsvd, len 을 순서대로 검사해 `PKT_E_*` 를 돌려준다.

### Part C — 레지스터 블록 · DMA 디스크립터 · 설정 ↔ 하드웨어

- **C-계약**: 레지스터 5개 오프셋+타입, 다중 비트 마스크의 연속성, `CR1` 마스크 겹침 없음, 디스크립터의 크기·정렬·패딩 없음·`next` 타입, `RING_N` 2^k, `RX_BUF_SIZE` 와 `LEN` 필드·워드 배수·최대 패킷.
- C1: `dma_ring_init(ring, n, ring_bus, buf_bus, buf_len)` — 원형 링을 채운다. 컴파일 타임에 모르는 값(`n`, `buf_len`, `ring_bus`)은 **같은 규칙을 런타임에** 검사해 `-1`.
- C2: `spi_dma_start(spi, ring_bus)` — `SPE` 끄고 → `DESCADDR`, `DMACR` → 마지막에 `CR1` (값은 `FIELD_PREP_CONST` 로 만든 컴파일 타임 상수).

**계약을 테스트하는 방법:** `mutants.sh` 가 원본의 사본에 치환 하나씩을 적용한 돌연변이 15개를 만들어 빌드한다. 모두 `static assertion failed` 로 죽어야 한다.

| 돌연변이 | 무엇을 바꾸나 | 실제로 일어날 법한 상황 |
|---|---|---|
| `pkt-add-field` | `type` 뒤에 `uint8_t flags;` 추가 | "플래그 하나만 넣자" |
| `pkt-reorder` | `len` 과 `status` 줄 교환 | 보기 좋게 정리 |
| `pkt-len-signed` | `uint16_t len` → `int16_t len` | 레이아웃은 그대로, 의미만 바뀜 |
| `pkt-enum-field` | `uint8_t type` → `pkt_type_t type` | "타입 안전하게" |
| `err-add-no-string` | `ERR_NOMEM` 추가, 문자열은 안 넣음 | 표 하나를 잊음 |
| `err-overflow-field` | 코드 2개 + 문자열 2개 추가 (9개) | 3비트 필드를 잊음 |
| `spi-reserved-miss` | `RESERVED0[4]` → `[3]` | 데이터시트 오독 |
| `spi-mask-overlap` | `SPE` 를 bit 6 → bit 5 | 오타 |
| `spi-mask-typo` | `BR` 을 `0x7u << 3` → `0x5u << 3` | 오타 |
| `spi-br-too-big` | `SPI_BR_DIV` 5 → 9 | 클럭 바꾸며 분주비 재계산 |
| `ring-not-pow2` | `RING_N` 8 → 6 | RAM 아끼기 |
| `buf-too-big` / `buf-too-small` | `RX_BUF_SIZE` → 65536 / 256 | 처리량·RAM 튜닝 |
| `desc-ptr-next` | `uint32_t next` → `void *next` | "포인터가 더 편하잖아" |
| `desc-drop-alignas` | `_Alignas(DESC_ALIGN)` 삭제 | 리팩터링 중 실수 |

## 2. Problem (English)

> **Compile-time contracts for a sensor-node SPI/DMA driver.**
>
> The datasheet and protocol spec fix a 12-byte little-endian packet header, a 1 KB SPI register block, a 16-byte, 16-byte-aligned DMA descriptor, 3 bits for the error code and 16 bits for the transfer length. Turn every one of those assumptions into a build failure.
>
> 1. Using only integer constant expressions (no compiler builtins), implement `BUILD_BUG_ON(c)` (statement), `BUILD_BUG_ON_ZERO(c)` (expression, yields `(size_t)0`), `IS_POW2`, `LOWBIT`, `FIELD_MAX`, `MASK_IS_CONTIGUOUS`, `FIELD_FITS` and `FIELD_PREP_CONST(mask, value)`, which must fail to compile if the mask is not contiguous or the value does not fit — and must also reject a non-constant value.
> 2. Implement `MEMBER_SIZE`, `FIELD_FOLLOWS`, `ENDS_STRUCT`, `IS_TYPE` (via `_Generic`) and `PIN_FIELD(T, m, off, type)`. Pin every row of the spec tables. The error-string table must stay in sync with the enum; the enum must fit its bit field. Then write `pkt_parse()` that `memcpy`s the header and validates the untrusted bytes.
> 3. Pin the register block and the descriptor, tie the configuration (`RING_N`, `RX_BUF_SIZE`, `SPI_BR_DIV`) to the hardware field widths, and write `dma_ring_init()` (the same rules checked at run time for run-time values) and `spi_dma_start()`.
>
> Your contracts are themselves tested: `./mutants.sh solution.c` applies 15 realistic edits (add a field, reorder, signed length, enum in the wire struct, forgotten string, overflowing enum, wrong reserved count, overlapping masks, bad divider, non-power-of-two ring, oversized/undersized buffer, pointer in the descriptor, lost `_Alignas`) and every one must die with a static assertion.
>
> Follow-ups: why was the old negative-array `BUILD_BUG_ON` unsafe? Why can't `#if` use `sizeof`? Your contracts pass on the host — do they pass on Cortex-M with GCC? How would you test a static assertion in CI?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter   # 처음엔 11/44 passed
./mutants.sh starter.c                                                    # 처음엔 4/15 killed
```

시작 코드의 TODO 매크로는 일부러 **"통과만 시키는"** 스텁이다 (`IS_POW2(x)` = `x != 0`, `FIELD_FITS` = 1, `IS_TYPE` = 1 …). 그래서 시작 코드도 경고 없이 컴파일되고, `FIELD_PREP_CONST(SPI_CR1_BR, SPI_BR_DIV)` 같은 계약도 "통과" 한다. 이게 이 레슨의 첫 교훈이다: **아무것도 검사하지 않는 단언은 올바른 단언과 똑같이 생겼다.** 차이는 실패시켜 봐야 보인다. 시작 코드에 대한 `mutants.sh` 실제 결과는 `4/15 killed` — 주어진 `sizeof` 계약 두 줄이 `pkt-add-field`, `pkt-reorder`, `pkt-enum-field`, `spi-reserved-miss` 를 잡고, 나머지 11개는 `SURVIVED` 다. 그 11개가 여러분 몫이다.

관찰용 파일 (하네스와 별개, 각 파일 머리에 빌드 명령):

```bash
cc -std=c11 -Wall -Wextra -O2 demo_vla_loophole.c -o dv && ./dv a b c        # 옛 BUILD_BUG_ON 의 VLA 구멍
clang -std=c11 -fsyntax-only --target=thumbv7em-none-eabi -mcpu=cortex-m4 demo_target.c   # 타깃별 계약
clang -std=c11 -fsyntax-only -Xclang -verify -Xclang -verify-ignore-unexpected=note demo_verify.c
cc -std=c11 -fsyntax-only demo_if_sizeof.c                                    # #if 에서 sizeof
clang -std=c11 -fsyntax-only demo_mask_overlap.c                              # 겹침 검사: XOR vs 합
```

## 4. 힌트

<details>
<summary>힌트 1 — A2: 식 안에 선언을 넣는 방법</summary>

`_Static_assert` 는 **선언** 이라 식 한가운데에 못 쓴다. 그런데 C11 은 구조체 멤버 목록 안에 `_Static_assert` 를 허용하고(6.7.2.1p1), `sizeof(struct { ... })` 는 식이다. 크기에 0 을 곱하면 값은 0 이 되고, 단언은 구조체를 해석할 때 검사된다. (C-M11 의 `IS_ARRAY` 와 같은 트릭.) 구조체에는 이름 있는 멤버가 하나 있어야 한다(6.7.2.1p8).

</details>

<details>
<summary>힌트 2 — A4/A5/A6: 2의 보수와 나눗셈</summary>

부호 없는 `m` 에 대해 `m & (0u - m)` 은 가장 낮은 1 비트만 남긴다 (`0 - m` = `~m + 1`). 시프트 양 대신 그 비트로 **나누면** 필드가 0번 비트로 내려온다: `0x38 / 0x08 = 7`. 연속인지는 내려온 값 `x` 가 `2^k - 1` 꼴인지, 즉 `(x & (x + 1)) == 0` 인지 보면 된다. `0xFFFFFFFF + 1` 이 0 으로 감기는 것까지 생각할 것. `m == 0` 일 때 0으로 나누지 않게.

</details>

<details>
<summary>힌트 3 — A7: 무엇으로 캐스팅해서 비교하나</summary>

`(uint32_t)v <= FIELD_MAX(m)` 는 `v = 0x100000000` 을 0 으로 잘라서 "들어간다" 고 말하고, `v = -1` 은 `0xFFFFFFFF` 가 된다. 가장 넓은 부호 없는 타입(`uintmax_t`)으로 올려서 비교하면 음수는 거대한 수가 되어 자연히 거짓이다.

</details>

<details>
<summary>힌트 4 — B4/B5: _Generic 은 무엇을 보나</summary>

`_Generic((e), type: 1, default: 0)` — 제어식 `e` 는 평가되지 않으므로(6.5.1.1p3) `((T *)0)->m` 을 써도 된다. 비교되는 타입은 lvalue 변환 후의 타입이라 `volatile`/`const` 는 벗겨지고, 정수 승격은 일어나지 않는다. `PIN_FIELD` 의 메시지는 `#T "." #m " must be " #type " at offset " #off` 처럼 문자열 리터럴을 이어 붙여 만든다.

</details>

<details>
<summary>힌트 5 — 표 길이 계약이 공허해지는 경우</summary>

`err_str[ERR_COUNT] = { ... }` 처럼 크기를 enum 개수로 적으면 `ARRAY_SIZE(err_str) == ERR_COUNT` 는 **항상 참** 이다. 크기를 비워 두면 지정 초기화자(designated initializer)의 가장 큰 인덱스가 크기를 정한다(6.7.9p22) — 그때 비로소 계약이 의미를 가진다.

</details>

## 5. 풀이

### 0. 도구 지도 — 무엇을 언제 쓰나

| 도구 | 평가 시점 | 쓸 수 있는 자리 | 검사할 수 있는 것 | 못 하는 것 |
|---|---|---|---|---|
| `#if` / `#error` | 전처리 | 어디든 (줄 단위) | 매크로 상수: `CHAR_BIT`, `__BYTE_ORDER__`, `UINT32_MAX`, 버전 매크로 | `sizeof`, `offsetof`, enum 상수, 타입 |
| `_Static_assert(c, "msg")` | 컴파일 | 파일 범위, 블록 안, 구조체 멤버 목록 | 정수 상수식 전부 (`sizeof`, `offsetof`, `_Alignof`, enum, `_Generic`) | 식 자리, 런타임 값 |
| `BUILD_BUG_ON(c)` | 컴파일 | 문장 자리 | 위와 같음 | 식 자리 |
| `BUILD_BUG_ON_ZERO(c)` | 컴파일 | **식 자리** (매크로 안, 초기화식) | 위와 같음 | — |
| 링커 스크립트 `ASSERT(exp, "msg")` | 링크 | `.ld` | 섹션 크기, 심볼 주소 (`.data + .bss ≤ RAM`) | C 타입 |
| `assert()` / self-test | 런타임 | 함수 안 | 무엇이든 | 이미 출하된 뒤일 수 있음 |

`_Static_assert` 는 **선언** 이다 (6.7.10). 코드를 만들지 않고, 상수식이 0 이면 "제약 위반" 으로 진단이 나온다. `<assert.h>` 는 C11 부터 `static_assert` 라는 이름의 매크로를 제공하고(7.2p3), C23 에서는 `static_assert` 가 키워드가 되며 메시지를 생략할 수 있다. `-std=c11` 에서 메시지를 빼면 이 맥 clang 은 이렇게 말한다 (실제 출력):

```text
$ echo '_Static_assert(sizeof(int) == 4);' | cc -std=c11 -fsyntax-only -x c -
<stdin>:1:32: warning: '_Static_assert' with no message is a C23 extension [-Wc23-extensions]
```

전처리기는 타입을 모른다. `#if` 안의 모든 남은 식별자는 — 키워드도 — 0 으로 바뀌므로(6.10.1p4) `sizeof(int)` 는 `0(0)` 이 된다. `demo_if_sizeof.c` 실제 출력:

```text
demo_if_sizeof.c:12:5: error: function-like macro 'sizeof' is not defined
   12 | #if sizeof(int) == 4
      |     ^
```

그래서 엔디안·`CHAR_BIT` 처럼 **매크로로 주어지는** 사실은 `#if`/`#error` 로, 타입과 레이아웃은 `_Static_assert` 로 나눈다. `__BYTE_ORDER__` 는 표준이 아니라 GCC/clang 이 미리 정의하는 매크로다 — 이 매크로가 없는 컴파일러에서는 `!defined(__BYTE_ORDER__)` 쪽으로 빠져 **실패** 하게 써 두는 것이 안전한 기본값이다 (solution.c 의 `#if`).

### Part A — 정수 상수식이라는 제약

모든 계약 도구는 **정수 상수식** 이어야 한다. 6.6p6 이 허용하는 재료: 정수·enum·문자 상수, 결과가 정수 상수인 `sizeof`(VLA 아님)와 `_Alignof`, 산술 상수를 정수 타입으로 바꾸는 캐스트. 허용하지 않는 것: `const` 변수(C 에선 상수식이 아님 — C-M11 C-4), 함수 호출, 쉼표 연산자, 대입. `offsetof` 는 "정수 상수식으로 펼쳐진다" 고 7.19p3 이 보장한다. `_Generic` 은 선택된 쪽이 정수 상수면 clang/GCC 에서 상수식으로 쓸 수 있다 (C-M11).

**왜 매크로인가:** `static inline int is_pow2(uint32_t)` 는 깔끔하지만 호출은 상수식이 아니다. `_Static_assert(is_pow2(RING_N), ...)` 는 C 에서 불가능하다. 그래서 계약 도구는 **인자를 여러 번 평가하는** 매크로다 — 상수에만 쓰고, 런타임 값에 쓸 때는 부작용 없는 인자만 넘긴다 (C-B15).

**A1/A2 — BUILD_BUG_ON 두 가지 모양:**

```c
#define BUILD_BUG_ON(cond) \
    do { _Static_assert(!(cond), "BUILD_BUG_ON(" #cond ")"); } while (0)
#define BUILD_BUG_ON_ZERO(cond) \
    (0 * sizeof(struct { _Static_assert(!(cond), "BUILD_BUG_ON_ZERO(" #cond ")"); char c_; }))
```

`#cond` 로 조건 글자를 메시지에 넣는다. 인접한 문자열 리터럴은 번역 6단계에서 하나로 합쳐지므로 `_Static_assert` 가 요구하는 "문자열 리터럴" 이다. `char c_;` 는 장식이 아니다 — 이름 있는 멤버가 없는 구조체는 UB 이다(6.7.2.1p8).

**옛날 BUILD_BUG_ON 의 구멍.** C11 이전에는 "크기가 음수인 배열" 로 같은 일을 했다: `((void)sizeof(char[1 - 2*!!(c)]))`. 상수로 실패할 때는 잘 잡는다 (`demo_vla_loophole.c -DOLD_FAIL`, 실제 출력):

```text
demo_vla_loophole.c:27:5: error: array size is negative
```

그런데 조건이 **상수식이 아니면** `char[...]` 는 가변 길이 배열(VLA)이 되고, 컴파일은 조용히 통과한다. 실제 출력:

```text
$ cc -std=c11 -Wall -Wextra -O2 demo_vla_loophole.c -o dv && ./dv a b c
argc=4: built and ran
$ cc -std=c11 -Wall -Wextra -Wvla -fsyntax-only demo_vla_loophole.c
demo_vla_loophole.c:25:18: warning: variable length array used [-Wvla]
$ cc -std=c11 -g -O1 -fsanitize=undefined demo_vla_loophole.c -o dvu && ./dvu a b c
demo_vla_loophole.c:25:5: runtime error: variable length array bound evaluates to non-positive value -1
argc=4: built and ran
$ cc -std=c11 -fsyntax-only -DUSE_C11 demo_vla_loophole.c
demo_vla_loophole.c:25:18: error: static assertion expression is not an integral constant expression
```

`-Wall -Wextra` 로도 경고가 없다. "빌드 타임 검사" 가 런타임 UB(6.7.6.2p5) 로 바뀐 것이다. `_Static_assert` 판은 상수식이 아니라는 것 자체를 에러로 만든다 — **상수가 아닌 것을 넣는 실수까지 잡는다** 는 게 C11 판의 진짜 장점이다.

리눅스 커널의 `BUILD_BUG_ON_ZERO` 는 `sizeof(struct { int:(-!!(e)); })` — 폭이 음수인 비트필드(6.7.2.1p4 제약 위반)를 쓴다. 조건이 거짓이면 이름 없는 0폭 비트필드만 있는 "빈 구조체" 가 되는데 이건 GNU 확장이다. 실제 출력:

```text
$ cc -std=c11 -Wall -Wextra -pedantic -fsyntax-only -DKERNEL_STYLE demo_vla_loophole.c
demo_vla_loophole.c:30:45: warning: struct without named members is a GNU extension [-Wgnu-empty-struct]
$ cc -std=c11 -fsyntax-only -DKERNEL_STYLE -DKERNEL_FAIL demo_vla_loophole.c
demo_vla_loophole.c:32:20: error: anonymous bit-field has negative width (-1)
```

커널은 GNU C 로만 빌드되니 문제없지만, IAR/Keil 같은 다른 컴파일러로 옮길 코드라면 이 레슨의 `_Static_assert` 판이 이식성 있는 선택이다. (요지: 지금 커널의 `BUILD_BUG_ON` 은 `compiletime_assert` 를 거쳐 `__attribute__((error))` 가 붙은 함수 호출을 최적화가 지우지 못하면 에러를 내는 방식이라, 인라인 후에야 상수가 되는 조건까지 잡을 수 있는 대신 최적화에 의존한다. 커널은 `_Static_assert` 를 감싼 `static_assert` 매크로도 함께 쓴다.)

**A3–A6 — 시프트 없이 필드 다루기.** 예: `m = 0x38`.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">LOWBIT, FIELD_MAX, 연속성 검사 — 0x38 (BR[5:3]) 과 0x28 (오타)</text>
  <g fill="currentColor" font-family="monospace">
    <text x="10" y="56">m            = 0011 1000  (0x38)</text><text x="10" y="78">0u - m       = 1100 1000  (하위 8비트만)</text><text x="10" y="100">LOWBIT(m)    = 0000 1000  = m &amp; (0u - m)</text>
    <text x="10" y="122">FIELD_MAX(m) = 0x38 / 0x08 = 0000 0111 = 7</text><text x="10" y="144">x &amp; (x + 1)  = 0111 &amp; 1000 = 0  → 연속</text><text x="400" y="56">m            = 0010 1000  (0x28)</text>
    <text x="400" y="100">LOWBIT(m)    = 0000 1000</text><text x="400" y="122">FIELD_MAX(m) = 0x28 / 0x08 = 0101 = 5</text><text x="400" y="144">x &amp; (x + 1)  = 0101 &amp; 0110 = 0100 → 구멍</text>
  </g>
  <rect x="10" y="160" width="720" height="80" rx="6" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="22" y="182">나눗셈 = 시프트: LOWBIT 이 2^k 이므로 m / 2^k 는 m &gt;&gt; k 와 같다. 곱셈 v * 2^k 는 v &lt;&lt; k.</text><text x="22" y="204">끝 경계: m = 0xFFFFFFFF → x = 0xFFFFFFFF, x + 1 이 0 으로 감겨 0 → 연속 (맞다).</text>
    <text x="22" y="226">m = 0 → LOWBIT 0 → FIELD_MAX 는 분모에 (m == 0) 을 OR 해 0/1 = 0. 연속성은 m != 0 부터.</text>
  </g>
</svg>
```

```c
#define IS_POW2(x)  ((uintmax_t)(x) != 0u && ((uintmax_t)(x) & ((uintmax_t)(x) - 1u)) == 0u)
#define LOWBIT(m)   ((uint32_t)((uint32_t)(m) & (0u - (uint32_t)(m))))
#define FIELD_MAX(m) ((uint32_t)(m) / (LOWBIT(m) | ((uint32_t)(m) == 0u)))
#define MASK_IS_CONTIGUOUS(m) ((uint32_t)(m) != 0u && (FIELD_MAX(m) & (FIELD_MAX(m) + 1u)) == 0u)
#define FIELD_FITS(m, v) ((uintmax_t)(v) <= FIELD_MAX(m))
```

| 경계 | 잘못된 구현 | 무슨 일이 | 이 구현 |
|---|---|---|---|
| `IS_POW2(0)` | `(x & (x-1)) == 0` 만 | 0 을 2의 거듭제곱이라 함 | `x != 0 &&` |
| `IS_POW2(INT_MIN)` 처럼 음수 | `int` 그대로 `x - 1` | 부호 있는 오버플로 UB (C-M12) | `uintmax_t` 로 올려서 |
| `MASK_IS_CONTIGUOUS(0xFFFFFFFF)` | `IS_POW2(FIELD_MAX(m) + 1)` | `+1` 이 0 으로 감겨 "연속 아님" | `x & (x + 1)` 은 감김에 안전 |
| `FIELD_FITS(0xFFFFFFFF, 0x100000000)` | `(uint32_t)v <= ...` | 잘려서 0 → "들어감" | `uintmax_t` 로 비교 |
| `FIELD_FITS(0x38, -1)` | `(int)v <= 7` | −1 ≤ 7 → "들어감" | `(uintmax_t)-1` 은 거대 → 거짓 |
| `FIELD_MAX(0)` | `m / LOWBIT(m)` | 0 으로 나누기 → 상수식 아님 | 분모에 `(m == 0)` OR |

`0u - (uint32_t)m` 은 `uint32_t` 가 `unsigned int` 인 플랫폼(호스트 arm64, Cortex-M 모두)에서 모듈러 산술이라 UB 가 없다. `uint32_t` 가 `int` 보다 좁은 이상한 플랫폼이면 승격(C-B02) 때문에 결과가 달라지므로, 바깥 `(uint32_t)` 캐스트로 다시 32비트로 자른다.

**A8 — FIELD_PREP_CONST:**

```c
#define FIELD_PREP_CONST(m, v)                                   \
    ((uint32_t)(BUILD_BUG_ON_ZERO(!MASK_IS_CONTIGUOUS(m))        \
              + BUILD_BUG_ON_ZERO(!FIELD_FITS(m, v))             \
              + (uint32_t)(v) * LOWBIT(m)))
```

검사 두 개가 0 을 더하고, 값은 `v << shift` 를 곱셈으로 만든다. 마지막 `(uint32_t)` 는 `size_t` 로 넓어진 합을 32비트로 돌려 놓는다. 이걸 쓰는 설정은 **상수식 자리** 에 둔다:

```c
#define SPI_CR1_INIT (FIELD_PREP_CONST(SPI_CR1_BR, SPI_BR_DIV) | SPI_CR1_MSTR | SPI_CR1_SPE)
static const uint32_t k_cr1_init = SPI_CR1_INIT;    /* = 0x6C */
```

`SPI_BR_DIV` 를 9 로 바꾸면 (mutants.sh `spi-br-too-big`) 이 줄이 실패한다:

```text
spi-br-too-big       KILLED   <- BUILD_BUG_ON_ZERO(!FIELD_FITS((0x7u << 3), 9u))
```

메시지에 `SPI_CR1_BR` 이 아니라 `(0x7u << 3)` 가 보이는 건, 매크로 인자는 `#` 에 쓰이지 않는 한 **먼저 펼쳐진 뒤** 바깥 매크로에 들어가기 때문이다(6.10.3.1). 그리고 런타임 값을 넣으면 (`demo_verify.c` 의 `rt(v)`) "not an integral constant expression" 으로 빌드가 실패한다 — 상수 전용이라는 것도 계약이다. 리눅스 `<linux/bitfield.h>` 도 `FIELD_PREP`(런타임, 상수일 때만 컴파일 타임 검사)와 `FIELD_PREP_CONST`(정적 초기화용) 를 나눠 둔다. 이 레슨은 런타임용으로 검사 없는 `field_prep()` 을 주고, 호출자(`dma_ring_init`)가 `FIELD_FITS` 로 먼저 검사한다.

### Part B — 와이어 프로토콜: 스펙 표 한 줄 = 계약 한 줄

```c
#define PIN_FIELD(T, m, off, type)                                         \
    _Static_assert(offsetof(T, m) == (off) && IS_TYPE(((T *)0)->m, type), \
                   #T "." #m " must be " #type " at offset " #off)

_Static_assert(sizeof(pkt_hdr_t) == 12, "pkt_hdr_t must be 12 bytes (wire format)");
PIN_FIELD(pkt_hdr_t, sync,   0x00, uint8_t);
PIN_FIELD(pkt_hdr_t, type,   0x01, uint8_t);
PIN_FIELD(pkt_hdr_t, len,    0x02, uint16_t);
/* ... status, seq, rsvd, crc */
```

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">pkt_hdr_t 바이트 지도 — 스펙대로 vs 돌연변이 두 개 (호스트 arm64 실제 크기)</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="140" y="48">0</text><text x="190" y="48">1</text><text x="240" y="48">2</text><text x="290" y="48">3</text>
    <text x="340" y="48">4</text><text x="390" y="48">5</text><text x="440" y="48">6</text><text x="490" y="48">7</text>
    <text x="540" y="48">8</text><text x="590" y="48">9</text><text x="634" y="48">10</text><text x="684" y="48">11</text>
  </g>
  <text x="10" y="76" fill="currentColor">스펙 (12 B)</text>
  <g stroke="currentColor">
    <rect x="130" y="58" width="50" height="28" class="f-ok"/><rect x="180" y="58" width="50" height="28" class="f-ok"/>
    <rect x="230" y="58" width="100" height="28" class="f-ok"/><rect x="330" y="58" width="50" height="28" class="f-ok"/>
    <rect x="380" y="58" width="50" height="28" class="f-ok"/><rect x="430" y="58" width="100" height="28" class="f-ok"/><rect x="530" y="58" width="200" height="28" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="138" y="77">sync</text><text x="188" y="77">type</text><text x="262" y="77">len</text><text x="334" y="77">status</text>
    <text x="392" y="77">seq</text><text x="462" y="77">rsvd</text><text x="616" y="77">crc</text>
  </g>
  <text x="10" y="126" fill="currentColor">+flags (16 B)</text>
  <g stroke="currentColor">
    <rect x="130" y="108" width="50" height="28" class="f-ok"/><rect x="180" y="108" width="50" height="28" class="f-ok"/>
    <rect x="230" y="108" width="50" height="28" class="f-acc"/><rect x="280" y="108" width="50" height="28" class="f-warn"/>
    <rect x="330" y="108" width="100" height="28" class="f-ok"/><rect x="430" y="108" width="50" height="28" class="f-ok"/>
    <rect x="480" y="108" width="50" height="28" class="f-ok"/><rect x="530" y="108" width="100" height="28" class="f-ok"/><rect x="630" y="108" width="100" height="28" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="138" y="127">sync</text><text x="188" y="127">type</text><text x="234" y="127">flags</text><text x="290" y="127">pad</text>
    <text x="364" y="127">len</text><text x="434" y="127">status</text><text x="492" y="127">seq</text><text x="562" y="127">rsvd</text><text x="652" y="127">pad…</text>
  </g>
  <text x="10" y="176" fill="currentColor">재배열 (16 B)</text>
  <g stroke="currentColor">
    <rect x="130" y="158" width="50" height="28" class="f-ok"/><rect x="180" y="158" width="50" height="28" class="f-ok"/>
    <rect x="230" y="158" width="50" height="28" class="f-acc"/><rect x="280" y="158" width="50" height="28" class="f-warn"/>
    <rect x="330" y="158" width="100" height="28" class="f-acc"/><rect x="430" y="158" width="50" height="28" class="f-ok"/>
    <rect x="480" y="158" width="50" height="28" class="f-warn"/><rect x="530" y="158" width="100" height="28" class="f-ok"/><rect x="630" y="158" width="100" height="28" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="138" y="177">sync</text><text x="188" y="177">type</text><text x="232" y="177">status</text><text x="290" y="177">pad</text>
    <text x="364" y="177">len</text><text x="442" y="177">seq</text><text x="490" y="177">pad</text><text x="562" y="177">rsvd</text><text x="652" y="177">pad…</text>
  </g>
  <rect x="10" y="202" width="720" height="68" rx="6" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="22" y="224">u16 는 2바이트, u32 는 4바이트 경계로 정렬되므로(ABI) 필드 하나가 끼면 패딩이 생기고 뒤가 전부 밀린다.</text><text x="22" y="246">sizeof 만 고정하면 "어디가" 틀렸는지 모른다. PIN_FIELD 는 어긋난 필드마다 이름을 댄다 (아래 출력).</text>
    <text x="22" y="264" font-size="12">(crc 는 두 돌연변이 모두 오프셋 12, 그림은 11번 바이트까지만 그렸다)</text>
  </g>
</svg>
```

`pkt-add-field` 돌연변이를 직접 빌드하면 (perl 한 줄로 `flags` 삽입, 실제 출력 — 파일 경로는 줄 번호만 남김):

```text
182:16: error: static assertion failed due to requirement 'sizeof(pkt_hdr_t) == 12': pkt_hdr_t must be 12 bytes (wire format)
185:1: error: static assertion failed due to requirement '__builtin_offsetof(pkt_hdr_t, len) == (2)': pkt_hdr_t.len must be uint16_t at offset 0x02
186:1: error: static assertion failed due to requirement '__builtin_offsetof(pkt_hdr_t, status) == (4)': pkt_hdr_t.status must be uint8_t at offset 0x04
187:1: error: static assertion failed due to requirement '__builtin_offsetof(pkt_hdr_t, seq) == (5)': pkt_hdr_t.seq must be uint8_t at offset 0x05
188:1: error: static assertion failed due to requirement '__builtin_offsetof(pkt_hdr_t, rsvd) == (6)': pkt_hdr_t.rsvd must be uint16_t at offset 0x06
189:1: error: static assertion failed due to requirement '__builtin_offsetof(pkt_hdr_t, crc) == (8)': pkt_hdr_t.crc must be uint32_t at offset 0x08
291:5: error: static assertion failed due to requirement '!(sizeof(pkt_hdr_t) != 12)': BUILD_BUG_ON(sizeof(pkt_hdr_t) != 12)
7 errors generated.
```

`sync` 와 `type` 줄은 조용하다 — 어긋남이 `type` **뒤** 에서 시작됐다는 걸 에러 목록이 알려 준다. (clang 은 `offsetof` 를 `__builtin_offsetof` 로 구현한다. 표준은 매크로가 상수식으로 펼쳐진다는 것만 정한다.)

**왜 타입까지 고정하나 — `pkt-len-signed`.** `uint16_t len` → `int16_t len` 은 크기도 오프셋도 같다. `sizeof`·`offsetof` 만 보는 계약은 이 돌연변이를 살려 보낸다(시작 코드 결과의 `SURVIVED`). 하지만 의미는 바뀌었다: 길이 40000 이 −25536 이 되고, `len > PKT_MAX_PAYLOAD` 검사를 음수가 통과한다. `IS_TYPE` 이 이것을 잡는다:

```text
pkt-len-signed       KILLED   <- pkt_hdr_t.len must be uint16_t at offset 0x02
```

`IS_TYPE(e, type)` = `_Generic((e), type: 1, default: 0)` 의 규칙 (하네스 테스트로 확인):

| 식 | 결과 | 이유 |
|---|---|---|
| `IS_TYPE(((SNX_SPI_TypeDef *)0)->CR1, uint32_t)` | 1 | 제어식은 lvalue 변환을 거친 타입으로 비교 → `volatile` 벗겨짐 (C11 의 모호함을 C17 이 DR 481 로 명확히 함) |
| `IS_TYPE('a', char)` / `IS_TYPE('a', int)` | 0 / 1 | C 에서 문자 상수는 `int` (6.4.4.4p10). C++ 와 다르다 |
| `IS_TYPE(a8 + b8, int)` (`uint8_t` 둘) | 1 | 정수 승격 (C-B02) |
| `IS_TYPE(a8, uint8_t)` | 1 | 승격은 연산에서만, `_Generic` 자체는 승격하지 않음 |
| 배열 `uint8_t x[4]` | `uint8_t *` 와 일치 | 배열은 포인터로 decay (C-B06) |

`uint8_t` 는 보통 `unsigned char` 의 typedef 라 `IS_TYPE(x, unsigned char)` 도 참이다 — `_Generic` 은 typedef 이름이 아니라 **타입** 을 본다. 그리고 `_Generic` 의 연관 목록에 서로 호환되는 타입을 두 번 쓰면 제약 위반이다(6.5.1.1p2) — `IS_TYPE` 은 연관이 하나라 문제없다.

**enum 은 와이어 구조체에 넣지 않는다 — `pkt-enum-field`.** enum 타입의 크기는 구현 정의다(6.7.2.2p4: `char`, 부호 있는/없는 정수 중 구현이 고른다). `demo_target.c` 를 세 가지 빌드로 돌린 실제 결과:

| 빌드 | `sizeof(pkt_type_t)` | `{u8; enum; u16}` | `{3×u32; void*}` | `{u8; long}` |
|---|---|---|---|---|
| 호스트 arm64 (clang 21) | 4 | 12 | **24** | **16** |
| `--target=thumbv7em-none-eabi -mcpu=cortex-m4` | 4 | 12 | 16 | 8 |
| 같은 타깃 + `-fshort-enums` | **1** | **4** | 16 | 8 |

```text
== clang (host)
demo_target.c:16:16: error: static assertion failed due to requirement 'sizeof(pkt_type_t) == 1': pkt_type_t is 1 byte
demo_target.c:17:16: error: static assertion failed due to requirement 'sizeof(hdr_enum_t) == 4': hdr_enum_t is 4 bytes
demo_target.c:18:16: error: static assertion failed due to requirement 'sizeof(desc_ptr_t) == 16': desc_ptr_t is 16 bytes
demo_target.c:19:16: error: static assertion failed due to requirement 'sizeof(rec_long_t) == 8': rec_long_t is 8 bytes
== clang --target=thumbv7em-none-eabi -mcpu=cortex-m4
demo_target.c:16:16: error: static assertion failed due to requirement 'sizeof(pkt_type_t) == 1': pkt_type_t is 1 byte
demo_target.c:17:16: error: static assertion failed due to requirement 'sizeof(hdr_enum_t) == 4': hdr_enum_t is 4 bytes
== clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -fshort-enums
exit=0
```

clang 은 `-dM -E` 로 `__ARM_SIZEOF_MINIMAL_ENUM` 을 알려 준다: 이 타깃 기본 4, `-fshort-enums` 면 1 (실제 출력). GCC `arm-none-eabi` 는 AAPCS 의 "가변 크기 enum" 을 기본으로 쓰는 것으로 알려져 있다(오브젝트의 `Tag_ABI_enum_size`) — 이 맥에는 GCC 가 없어 직접 확인하지 못했다. 같은 Cortex-M 에서도 **컴파일러에 따라 enum 크기가 다를 수 있다** 는 것, 바로 그래서 계약이 필요하다. 와이어에는 `uint8_t type` 을 두고 enum 은 값의 이름으로만 쓴다.

**enum 범위와 표 길이:**

```c
_Static_assert(ARRAY_SIZE(err_str) == ERR_COUNT, "err_str[] out of sync with err_t");
_Static_assert(ERR_COUNT <= FIELD_MAX(PKT_ST_ERR) + 1u, "err_t no longer fits status[2:0]");
_Static_assert((PKT_ST_ERR | PKT_ST_ACK | PKT_ST_RSVD) == 0xFFu
               && PKT_ST_ERR + PKT_ST_ACK + PKT_ST_RSVD == 0xFFu, "status bit fields must tile the byte");
_Static_assert(PKT_TYPE_LAST <= UINT8_MAX, "pkt_type_t must fit the uint8_t type field");
```

- `ERR_COUNT` 는 "마지막 + 1" 인 센티널이다. 값을 명시한 항목(`ERR_X = 0x80`)이 끼면 개수와 최댓값이 달라지므로, 그런 enum 에는 `ERR_LAST` 를 따로 두고 `ERR_LAST <= FIELD_MAX(...)` 로 검사한다 (`pkt_type_t` 가 그 모양: `PKT_PING = 1` 부터 시작).
- 마스크 셋이 **겹치지 않고 바이트를 꽉 채우는지** 는 "OR 이 0xFF" (빈 비트 없음) 와 "합이 OR 과 같다" (겹침 없음) 두 가지로 본다 (아래 설명).
- 두 표 계약은 서로 다른 돌연변이를 죽인다: 문자열 없이 코드 하나 추가 → 길이 계약(`err-add-no-string`), 코드와 문자열을 둘 다 넣어 9개 → 범위 계약(`err-overflow-field`).

**겹침 검사는 XOR 가 아니라 합.** 합은 항상 OR 이상이고, 어떤 비트가 두 번 이상 세어지면 그만큼 커진다 — 그래서 "OR == 합" 이면 서로소다. 흔히 보는 `OR == XOR` 는 **마스크 두 개일 때만** 맞다. 같은 비트가 세 마스크에 있으면 XOR 에서 살아남아 겹침을 놓친다. `demo_mask_overlap.c` (`0x0F` 세 개) 실제 출력 — XOR 판(5번 줄)은 통과하고 합 판(6번 줄)만 실패한다:

```text
demo_mask_overlap.c:6:16: error: static assertion failed due to requirement '(15U | 15U | 15U) == (15U + 15U + 15U)': sum: three copies of 0x0F overlap
demo_mask_overlap.c:6:40: note: expression evaluates to '15 == 45'
1 error generated.
```

마스크가 커서 합이 넘칠 수 있으면 `uintmax_t` 로 더한다.

**`err_str[ERR_COUNT]` 의 함정.** 크기를 enum 개수로 적으면 길이 계약은 항상 참이고, 빠진 항목은 조용히 `NULL` 이 된다. 직접 해 본 결과 (solution.c 에 크기 `[ERR_COUNT]` + `ERR_NOMEM` 추가, 실제 출력):

```text
$ perl -0pe 's/err_str\[\] = \{/err_str[ERR_COUNT] = {/; s/(    ERR_COUNT )/    ERR_NOMEM,\n$1/' solution.c > sized.c
$ cc -std=c11 -Wall -Wextra -Werror -O2 sized.c -o sized && ./sized
FAIL sized.c:333  err_str_holes == 0
FAIL sized.c:347  parse_with(4, 0x07, 16) == PKT_E_STATUS && parse_with(4, 0x0E, 16) == PKT_OK && parse_with(4, 0x18, 16) == PKT_E_STATUS
42/44 passed
```

경고 없이 빌드되고, 런타임 "구멍 검사" 테스트만 잡는다 (두 번째 FAIL 은 `ERR_COUNT` 가 8 이 되어 err 7 이 합법이 된 탓). 교훈: **표 크기는 비워 두고, 계약이 크기를 검사하게 한다.** 표가 여러 개면 X-macro 로 한 목록에서 모두 생성하는 것이 근본 해법이다 (C-A07).

**B6 — 계약은 "우리" 를, 검증은 "그들" 을.** 계약이 레이아웃과 엔디안을 보장하므로 `memcpy(out, raw, 12)` 는 올바르다 (포인터 캐스팅 대신 `memcpy` 인 이유는 정렬과 strict aliasing — C-M04, C-M13). 하지만 계약은 우리 빌드에 대한 것이고, 선 너머의 바이트에 대해서는 아무것도 말하지 않는다:

| 순서 | 검사 | 실패 | 이유 |
|---|---|---|---|
| 1 | `n < sizeof *out` | `PKT_E_SHORT` | 읽기 전에 (버퍼 오버리드 방지) |
| 2 | `sync != 0xA5` | `PKT_E_SYNC` | 프레이밍 어긋남 |
| 3 | `type` ∉ [`PKT_PING`, `PKT_TYPE_LAST`] | `PKT_E_TYPE` | enum 범위 계약의 런타임 짝 |
| 4 | `status[2:0] >= ERR_COUNT` 또는 `status[7:4] != 0` | `PKT_E_STATUS` | 3비트엔 8 이 들어가지만 정의된 건 7개 |
| 5 | `rsvd != 0` | `PKT_E_RSVD` | 예약 필드를 0 으로 강제해야 나중에 의미를 붙일 수 있다 |
| 6 | `len > 256` 또는 `len > n - 12` | `PKT_E_LEN` | 뒤에서 `payload[len]` 을 읽기 전에 |

### Part C — 레지스터 블록, 디스크립터, 설정 ↔ 하드웨어

레지스터 블록 고정은 C-B12/C-M01 에서 했던 그대로다 — 여기서는 `PIN_FIELD` 로 오프셋과 타입을 한 줄에, 블록 전체 `sizeof == 0x400` 으로 reserved 개수까지 묶는다 (`spi-reserved-miss` 는 `0x3FC` 크기로 죽는다). 새로운 것은 **값들 사이의 관계** 다.

**마스크 겹침:** `(A | B | C | ...) == (A + B + C + ...)` (Part B 의 이유로 XOR 가 아니라 합). 연속성: `MASK_IS_CONTIGUOUS(SPI_CR1_BR)`. 실제 출력:

```text
spi-mask-overlap     KILLED   <- SPI CR1 field masks overlap
spi-mask-typo        KILLED   <- BUILD_BUG_ON_ZERO(!MASK_IS_CONTIGUOUS((0x5u << 3)))
```

`spi-mask-typo` 는 명시적인 연속성 계약보다 **먼저** `SPI_CR1_INIT` 의 `FIELD_PREP_CONST` 안에서 죽었다 — mutants.sh 는 첫 번째 static assertion 메시지만 보여 준다.

**디스크립터 — 하드웨어가 읽는 구조체:**

```c
typedef struct dma_desc {
    _Alignas(DESC_ALIGN) uint32_t ctrl;   /* 첫 멤버의 정렬이 구조체 정렬을 끌어올린다 */
    uint32_t src, dst;
    uint32_t next;                        /* 버스 주소 */
} dma_desc_t;
_Static_assert(sizeof(dma_desc_t) == 16, "...");
_Static_assert(_Alignof(dma_desc_t) >= DESC_ALIGN, "...");
_Static_assert(FIELD_FOLLOWS(dma_desc_t, ctrl, src) && ... && ENDS_STRUCT(dma_desc_t, next), "...");
_Static_assert(IS_TYPE(((dma_desc_t *)0)->next, uint32_t), "...");
```

- 구조체의 정렬은 멤버 정렬의 최댓값이고 크기는 그 배수다(6.2.8, 6.7.5). `_Alignas(16)` 하나로 `dma_desc_t ring[8]` 의 모든 원소가 16바이트 경계에 놓인다. 지우면 정렬이 4 로 떨어지는데 **크기는 여전히 16** 이라 크기 계약은 못 잡는다 → `desc-drop-alignas` 는 정렬 계약이 죽인다.
- `FIELD_FOLLOWS` / `ENDS_STRUCT` 는 오프셋을 숫자로 반복하지 않고 "패딩이 없다" 를 말한다. 필드 순서는 지키되 숫자는 컴파일러에게 맡기는 쪽이다.
- **`next` 가 포인터면 안 되는 이유.** 호스트에서는 `void *` 가 8바이트라 크기가 24 가 되고(`demo_target.c`), Cortex-M4 에서는 16 이라 "맞는다". 그러나 의미가 틀렸다: DMA 가 보는 것은 **버스 주소** 다. 칩에 따라 CPU 주소와 DMA 주소가 다르거나(별칭, 주소 변환), DMA 가 닿지 못하는 메모리가 있다 (예: STM32H7 의 DTCM 은 DMA1/DMA2 가 접근하지 못한다). `uint32_t` + 명시적 변환 함수가 그 차이를 드러낸다. 호스트 단위 테스트 빌드가 이 실수를 잡아 준다는 점도 실무에서 중요하다.

**설정 ↔ 하드웨어 — 서로 다른 파일의 숫자를 묶기:**

| 계약 | 왜 | 죽이는 돌연변이 |
|---|---|---|
| `IS_POW2(RING_N)` | 코드가 `(i + 1) & (RING_N - 1)` 로 감는다. 6 이면 `& 5` 가 엉뚱한 인덱스 | `ring-not-pow2` |
| `FIELD_FITS(DESC_LEN, RX_BUF_SIZE)` | 65536 은 `LEN[15:0]` 에 0 으로 들어가 **0바이트 전송** 이 된다 | `buf-too-big` |
| `RX_BUF_SIZE % 4 == 0` | 워드 단위 DMA | — |
| `sizeof(pkt_hdr_t) + PKT_MAX_PAYLOAD <= RX_BUF_SIZE` | 최대 패킷이 버퍼 하나에 들어가야 파서가 단순하다 | `buf-too-small` |

마지막 줄이 가장 값지다. 프로토콜 담당과 드라이버 담당이 각자 자기 숫자를 바꿔도, 둘 사이의 관계가 빌드에서 지켜진다.

**C1 — 같은 규칙의 런타임 판.** `dma_ring_init` 의 `n`, `buf_len`, `ring_bus` 는 런타임 값이라 `_Static_assert` 를 못 쓴다. 그래서 똑같은 규칙을 런타임에 검사하고, 실패하면 **아무것도 쓰지 않고** `-1` 을 돌려준다 (테스트가 `0xCCCCCCCC` 가 그대로인지 본다).

```c
if (!IS_POW2(n) || buf_len == 0u || !FIELD_FITS(DESC_LEN, buf_len) || ring_bus % DESC_ALIGN != 0u)
    return -1;
for (uint32_t i = 0; i < n; i++) {
    ring[i].ctrl = field_prep(DESC_LEN, buf_len) | DESC_IRQ | DESC_OWN;
    ring[i].src  = SPI_DR_BUS;
    ring[i].dst  = buf_bus + i * buf_len;
    ring[i].next = ring_bus + ((i + 1u) & (n - 1u)) * (uint32_t)sizeof(dma_desc_t);
}
```

| 같은 규칙 | 컴파일 타임 (설정 상수) | 런타임 (인자) |
|---|---|---|
| 2의 거듭제곱 | `_Static_assert(IS_POW2(RING_N))` | `if (!IS_POW2(n)) return -1;` |
| LEN 필드 폭 | `FIELD_FITS(DESC_LEN, RX_BUF_SIZE)` | `!FIELD_FITS(DESC_LEN, buf_len)` |
| 16바이트 정렬 | `_Alignof(dma_desc_t) >= 16` | `ring_bus % DESC_ALIGN` |

계약 매크로가 상수식이면서 동시에 평범한 식이라 두 곳에서 그대로 쓰인다. 테스트는 디스크립터를 `memcpy` 로 바이트로 꺼내 "하드웨어가 메모리에서 보는 그대로" 를 확인한다: `ring[1]` = `00 02 01 80 | 0C 30 01 40 | 00 12 00 20 | 20 01 00 20` (LE). `buf_bus + n * buf_len` 이 32비트를 넘는지는 이 레슨에서 검사하지 않는다 — 실제 드라이버라면 메모리 영역 끝과 비교한다.

**C2** 의 쓰기 순서(SPE 끄고 → 설정 → SPE 켜기)는 값만 테스트한다. 순서까지 단위 테스트하려면 쓰기를 기록하는 mock 이 필요하다 (C-A01).

### 계약을 테스트하기: 돌연변이와 `-verify`

**mutants.sh.** 원본이 깨끗하게 컴파일되는지 먼저 확인하고(아니면 모든 돌연변이가 가짜로 죽는다), 치환이 실제로 무언가를 바꿨는지 `cmp` 로 확인한 뒤(아니면 "NOT APPLIED" 로 중단), 빌드 결과를 셋으로 나눈다: `static assertion failed` 로 실패 = **KILLED**, 성공 = **SURVIVED**, 다른 에러로 실패 = **ERROR** (계약이 아니라 우연이 잡음 — killed 로 세지 않는다). solution.c 실제 출력:

```text
pkt-add-field        KILLED   <- pkt_hdr_t must be 12 bytes (wire format)
pkt-reorder          KILLED   <- pkt_hdr_t must be 12 bytes (wire format)
pkt-len-signed       KILLED   <- pkt_hdr_t.len must be uint16_t at offset 0x02
pkt-enum-field       KILLED   <- pkt_hdr_t must be 12 bytes (wire format)
err-add-no-string    KILLED   <- err_str[] out of sync with err_t
err-overflow-field   KILLED   <- err_t no longer fits status[2:0]
spi-reserved-miss    KILLED   <- SNX_SPI block must span 1 KB
spi-mask-overlap     KILLED   <- SPI CR1 field masks overlap
spi-mask-typo        KILLED   <- BUILD_BUG_ON_ZERO(!MASK_IS_CONTIGUOUS((0x5u << 3)))
spi-br-too-big       KILLED   <- BUILD_BUG_ON_ZERO(!FIELD_FITS((0x7u << 3), 9u))
ring-not-pow2        KILLED   <- RING_N must be a power of two (index wrap uses & (RING_N - 1))
buf-too-big          KILLED   <- RX_BUF_SIZE must fit descriptor LEN[15:0]
buf-too-small        KILLED   <- a max-size packet must fit one RX buffer
desc-ptr-next        KILLED   <- dma_desc_t must be 16 bytes (hardware layout)
desc-drop-alignas    KILLED   <- dma_desc_t must be 16-byte aligned
15/15 killed
```

**`-verify` — 메시지까지 테스트.** mutants.sh 는 "어떤 계약이든 죽였는가" 만 본다. clang 의 `-verify` 모드는 소스에 적은 `// expected-error {{...}}` 와 실제 진단을 대조해, 일치하면 exit 0 이다 (clang·libc++ 자체의 negative compile test 방식). `demo_verify.c` 실제 출력:

```text
$ clang -std=c11 -fsyntax-only -Xclang -verify -Xclang -verify-ignore-unexpected=note demo_verify.c; echo "exit=$?"
exit=0
$ clang -std=c11 -fsyntax-only -DBREAK_CONTRACT -Xclang -verify -Xclang -verify-ignore-unexpected=note demo_verify.c; echo "exit=$?"
error: 'expected-error' diagnostics expected but not seen:
  File demo_verify.c Line 24: BUILD_BUG_ON_ZERO(!FIELD_FITS(0x38u, 8u))
  File demo_verify.c Line 26: not an integral constant expression
2 errors generated.
exit=1
```

`-DBREAK_CONTRACT` 는 누군가 `FIELD_FITS` 검사를 지워 버린 상황이다. 그러면 "8 은 3비트에 안 들어간다" 와 "런타임 값은 거부한다" 두 계약이 동시에 사라지고, negative test 가 그걸 알려 준다. `-verify-ignore-unexpected=note` 가 없으면 clang 이 붙이는 `note: expression evaluates to '4 == 6'` 같은 보조 메시지도 전부 `expected-note` 로 적어야 한다. GCC 에는 이 모드가 없으므로 이식성 있는 CI 에서는 mutants.sh 같은 "빌드 실패 기대" 테스트(CMake 의 `try_compile`, CTest `WILL_FAIL`)를 쓴다.

**비용:** `_Static_assert` 는 선언이라 코드가 없고, `BUILD_BUG_ON_ZERO` 는 `0 * sizeof(...)` 라 상수 0 으로 접힌다. 계약은 런타임 비용 0 — 펌웨어에서 쓰지 않을 이유가 없다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 44/44 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 44/44, 보고 없음
./mutants.sh solution.c                                                          # 15/15 killed
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `sizeof(T) == 12` 하나만 고정 | 같은 크기의 재배열·부호 변경(`int16_t len`)을 놓치고, 실패해도 어느 필드인지 모른다 | 스펙 표 한 줄마다 `PIN_FIELD(T, m, off, type)` |
| `#if sizeof(int) == 4` | 전처리기는 타입을 모른다 (6.10.1p4) → 에러 | `_Static_assert(sizeof(int) == 4, ...)`; `#if` 는 `CHAR_BIT`, `__BYTE_ORDER__` 같은 매크로만 |
| `((void)sizeof(char[1 - 2*!!(c)]))` 에 런타임 값 | VLA 가 되어 경고 없이 통과, 런타임 UB (demo) | `_Static_assert` 기반 `BUILD_BUG_ON` — 상수가 아니면 에러 |
| `static const uint32_t N = 8;` 를 `_Static_assert` 에 | C 에서 const 변수는 상수식이 아니다 (clang 은 확장으로 접어 줌, C-M11) | `#define` / `enum` / C23 `constexpr` |
| `static inline` 함수로 계약 도구 | 호출은 상수식이 아님 | 매크로 (인자 여러 번 평가 — 상수/부작용 없는 값만) |
| `IS_POW2(x)` = `(x & (x-1)) == 0` | 0 이 참, 음수 `int` 는 UB | `x != 0 &&`, `uintmax_t` 로 |
| 연속성 = `IS_POW2(FIELD_MAX(m) + 1)` | `0xFFFFFFFF` 에서 `+1` 이 0 으로 감김 | `(x & (x + 1)) == 0` |
| `FIELD_FITS` 를 `uint32_t` 로 잘라 비교 | `0x100000000` 이 0 이 되어 "들어감" | `uintmax_t` 로 |
| 와이어/공유 구조체에 enum, 포인터, `long`, `bool`, 비트필드 | 크기·배치가 컴파일러·타깃·옵션마다 다름 (demo_target, C-M03) | `uint8_t`/`uint16_t`/`uint32_t` + `PIN_FIELD` |
| `err_str[ERR_COUNT] = {...}` | 길이 계약이 항상 참, 빠진 항목은 조용히 `NULL` | `err_str[] = {...}` + `ARRAY_SIZE == ERR_COUNT` (+ 런타임 구멍 검사) |
| 메시지 없는 `_Static_assert(c);` 를 C11 에서 | C23 확장 경고, 다른 컴파일러에선 에러 | 메시지를 항상 쓰고, 무엇이 왜 깨졌는지 적는다 |
| 계약을 호스트 빌드에서만 컴파일 | 계약은 그 빌드만 검사한다 (enum 크기, 포인터 크기) | 타깃 빌드(`arm-none-eabi`)와 호스트 테스트 빌드 모두 CI 에서 |
| 계약을 써 놓고 실패를 한 번도 안 봄 | 스텁이나 오타로 공허한 계약일 수 있음 (starter 의 4/15) | 돌연변이 / `-verify` 로 실패를 확인 |
| 계약이 있으니 입력 검증 생략 | 계약은 우리 빌드에 대한 것, 상대 바이트는 아님 | `pkt_parse` 처럼 런타임 검증 |

## 8. 면접 Q&A

<details>
<summary>Q1. _Static_assert, #error, assert() 를 각각 언제 쓰나요?</summary>

평가 시점이 다르다. `#if`/`#error` 는 전처리 단계라 매크로 상수만 본다 — `CHAR_BIT`, `__BYTE_ORDER__`, 칩 선택 매크로, 설정 매크로의 조합. `_Static_assert` 는 컴파일 단계에서 정수 상수식을 본다 — `sizeof`, `offsetof`, `_Alignof`, enum 범위, 표 길이, 마스크 관계. `assert()` 는 런타임이라 무엇이든 보지만 이미 장비 위에서 돈 뒤고, `NDEBUG` 로 사라진다. 원칙: 할 수 있는 한 왼쪽(빌드)으로. 꼬리: "링크 단계는?" → GNU ld 링커 스크립트의 `ASSERT(SIZEOF(.data) + SIZEOF(.bss) <= LENGTH(RAM), "RAM overflow")` 처럼 섹션 크기·심볼 주소를 검사한다. C 로는 못 보는 것들이다.

</details>

<details>
<summary>Q2. 옛날 BUILD_BUG_ON(음수 배열 크기)은 무엇이 문제였고, 리눅스 커널은 지금 어떻게 하나요?</summary>

`sizeof(char[1 - 2*!!(c)])` 는 c 가 상수일 때만 컴파일 타임 검사다. 실수로 런타임 값을 넣으면 VLA 가 되어 `-Wall -Wextra` 에서도 경고 없이 빌드되고, 검사는 런타임 UB 로 바뀐다 (demo: UBSan 이 `variable length array bound evaluates to non-positive value -1`). C11 `_Static_assert` 기반이면 "상수식이 아니다" 가 에러다. 커널의 `BUILD_BUG_ON_ZERO` 는 음수 폭 비트필드 + GNU 빈 구조체 확장이고, `BUILD_BUG_ON` 은 `compiletime_assert` — `__attribute__((error))` 함수 호출이 최적화로 지워지지 않으면 에러 — 로 구현되어 인라인 후 상수가 되는 값도 잡지만 최적화에 의존한다. 커널은 `_Static_assert` 를 감싼 `static_assert` 도 쓴다. 이식성이 필요하면 `_Static_assert` 판.

</details>

<details>
<summary>Q3. 와이어 프로토콜 구조체를 memcpy 로 보내도 되나요? 무엇을 보장해야 하죠?</summary>

조건부로 된다. ① 고정 폭 정수만 (enum, 포인터, `long`, `bool`, 비트필드 금지 — 크기·배치가 구현 정의) ② 자연 정렬 순서로 배치해 패딩이 없게, 그리고 그걸 `PIN_FIELD` + `sizeof` 로 고정 ③ 엔디안 가정을 `#if __BYTE_ORDER__` 로 고정 ④ `CHAR_BIT == 8` ⑤ 수신 쪽은 포인터 캐스팅이 아니라 `memcpy` (정렬·strict aliasing — C-M04/C-M13) ⑥ 받은 값은 런타임 검증. 이 중 하나라도 보장이 어렵거나(빅 엔디안 상대, 여러 컴파일러) 장기 호환이 중요하면 필드별 명시적 직렬화(C-M05)가 낫다. 꼬리: "`__attribute__((packed))` 로 패딩을 없애면?" → 비정렬 접근이 생겨 Cortex-M0 에서 HardFault 나 바이트 단위 접근 (C-M04). 패딩이 없게 **설계** 하고 계약으로 확인하는 것이 먼저.

</details>

<details>
<summary>Q4. 정적 단언이 실제로 작동하는지 어떻게 테스트하나요?</summary>

실패시켜 본다. 한 번도 실패한 적 없는 단언은 공허할 수 있다 (오타, 항상 참인 식, `err_str[ERR_COUNT]` 처럼 크기를 개수로 적은 표). 방법: ① 돌연변이 테스트 — 소스 사본에 현실적인 변경(필드 추가, 재배열, 설정값 변경)을 가해 `static assertion failed` 로 빌드가 죽는지 확인 (mutants.sh) ② clang `-verify` 로 기대 진단 메시지까지 대조 ③ CMake `try_compile` / CTest `WILL_FAIL` 같은 "빌드 실패 기대" 테스트를 CI 에. 중요한 것은 "다른 이유로 실패" 를 구분하는 것 — 오타 난 돌연변이가 syntax error 로 죽으면 계약을 검증한 게 아니다 (mutants.sh 의 ERROR 분류).

</details>

<details>
<summary>Q5. enum 과 문자열 표(또는 핸들러 표)를 어떻게 동기화하나요?</summary>

센티널 `ERR_COUNT` 를 마지막에 두고, 표는 **크기를 비운** 지정 초기화자로 `[ERR_X] = "X"` 처럼 쓴 다음 `_Static_assert(ARRAY_SIZE(tbl) == ERR_COUNT)`. 크기를 비워야 가장 큰 인덱스가 크기를 정해(6.7.9p22) 새 enum 을 표에 안 넣었을 때 길이가 달라진다. `tbl[ERR_COUNT]` 로 적으면 계약이 항상 참이 되고 빠진 항목이 `NULL` 로 남는다 — 이 경우는 런타임 구멍 검사(부팅 self-test 또는 단위 테스트)로만 잡힌다. 표가 여럿이면 X-macro 로 하나의 목록에서 enum 과 표를 같이 생성한다 (C-A07). 꼬리: "enum 값이 비트필드에 들어가는지는?" → `ERR_COUNT <= FIELD_MAX(PKT_ST_ERR) + 1`. 값을 명시한 항목이 있으면 `ERR_LAST` 로.

</details>

<details>
<summary>Q6. 호스트에서 모든 정적 단언이 통과했습니다. Cortex-M 빌드에서도 안전한가요?</summary>

모른다. 계약은 **그 빌드** 에서만 평가된다. 실측 (`demo_target.c`): 와이어 구조체의 enum 은 호스트·clang Cortex-M4 에서 4바이트, `-fshort-enums` 면 1바이트. GCC `arm-none-eabi` 는 가변 크기 enum 이 기본인 것으로 알려져 있다. 포인터를 담은 디스크립터는 호스트 24바이트, M4 16바이트. `long` 은 8 vs 4. 그래서 CI 에서 타깃 크로스 빌드와 호스트 테스트 빌드 **양쪽** 에 같은 계약 헤더를 넣는다. 계약은 "스펙" 을 적는 것이라, 한쪽에서만 통과하는 계약은 코드가 스펙과 다르다는 신호다 — 계약을 고칠 게 아니라 타입을 고정 폭으로 고친다.

</details>

<details>
<summary>Q7. 컴파일 타임에 검사할 수 없는 가정은 어떻게 하나요?</summary>

종류별로. ① 비트필드의 비트 위치 — 상수식으로 못 꺼낸다 (C-M03) → 부팅 self-test 나 단위 테스트에서 탐침. ② `const` 배열의 **내용** (정렬된 표, 단조 증가 보율 표) — 원소는 상수식이 아니다 → X-macro 로 enum 상수를 만들거나 런타임 self-test. ③ 런타임 값(인자, 외부 바이트) — 같은 규칙의 런타임 판 (`dma_ring_init` 의 `IS_POW2(n)`, `pkt_parse`). ④ 메모리 맵(섹션이 RAM 에 들어가는가, 스택 여유) — 링커 스크립트 `ASSERT`. ⑤ 다른 타깃의 ABI — 타깃별 빌드. 도구를 상수식으로 만들어 두면 ③ 에서 같은 매크로를 재사용할 수 있다.

</details>

## 9. 추가 연습

### 연습 1 — C11 이전 컴파일러용 정적 단언

`_Static_assert` 가 없는 C99 컴파일러(오래된 벤더 툴체인)를 위해 `CT_ASSERT(cond, tag)` 와 태그 없이 줄 번호로 이름을 만드는 `CT_ASSERT_L(cond)` 를 작성하라. 한 단계 `##` 로는 왜 안 되는지 테스트로 보여라.

<details>
<summary>답 보기</summary>

`typedef char ct_assert_##tag[(cond) ? 1 : -1];` — 조건이 거짓이면 크기 −1 배열 → 제약 위반(6.7.6.2p1). 파일 범위 typedef 라 VLA 가 될 수 없어 본문의 VLA 구멍은 없다(파일 범위 VLA 는 허용되지 않음). 이름 충돌을 피하려 `__LINE__` 을 붙이는데, `a##__LINE__` 은 `__LINE__` 이 펼쳐지기 **전에** 붙어 `a__LINE__` 이 된다 — `##` 의 피연산자는 펼쳐지지 않기 때문이다(6.10.3.1). 그래서 `CT_CAT(a, b) → CT_CAT_(a, b) → a##b` 두 단계로 인자를 먼저 펼친다 (C-B15). 테스트: `STR(CT_CAT_(L, __LINE__))` 는 `"L__LINE__"`, `STR(CT_CAT(L, __LINE__))` 는 `"L<줄번호>"`. 한계: 같은 줄에 두 번 쓰거나, 두 헤더의 같은 줄 번호에서 쓰면 이름이 겹친다 — C11 은 같은 타입의 typedef 재정의를 허용하지만(6.7p3) C99 는 아니다. GCC 는 `__COUNTER__` 확장을 쓰기도 한다.

</details>

### 연습 2 — 입력을 검사하는 GENMASK(h, l)

리눅스처럼 비트 h..l 이 1 인 32비트 마스크를 만드는 `GENMASK(h, l)` 을 작성하라. `GENMASK(31, 0)` 에서 UB 가 없어야 하고, `GENMASK(3, 5)` 처럼 순서가 뒤집히거나 `h > 31` 이면 빌드가 실패해야 한다.

<details>
<summary>답 보기</summary>

`(~0u >> (31u - h)) & (~0u << l)` — 위쪽은 오른쪽 시프트로, 아래쪽은 왼쪽 시프트로 깎는다. `1u << 32` 같은 폭 이상 시프트(UB, C-B03)가 어디에도 없다: h = 31 이면 `>> 0`. 여기에 `BUILD_BUG_ON_ZERO((l) > (h) || (h) > 31)` 을 더한다. 실제 진단 (이 맥 clang):

```text
error: static assertion failed due to requirement '!((5) > (3) || (3) > 31)': BUILD_BUG_ON_ZERO((5) > (3) || (3) > 31)
error: static assertion failed due to requirement '!((0) > (32) || (32) > 31)': BUILD_BUG_ON_ZERO((0) > (32) || (32) > 31)
warning: shift count >= width of type [-Wshift-count-overflow]
```

`GENMASK(32, 0)` 은 단언과 별개로 `31u - 32` 가 감겨 시프트 경고까지 나온다 — 빌드는 어차피 실패한다. 리눅스의 `GENMASK_INPUT_CHECK` 는 인자가 상수일 때만 검사하도록 GNU 확장으로 감싸 런타임 인자도 허용한다. 이 버전은 상수 전용이다.

</details>

### 연습 3 — 플래시 설정 레코드: 8바이트 배수 + 버전 트립와이어

STM32L4/G4 류는 플래시를 64비트(더블워드) 단위로 프로그램한다. 12바이트 `cfg_body_t` 를 8의 배수로 채운 `cfg_rec_t` 를 만들고, 크기·페이지 계약과 "레이아웃이 바뀌면 버전을 올려라" 계약을 작성하라. `uint8_t pad[ROUND_UP(sz, 8) - sz]` 는 왜 위험한가?

<details>
<summary>답 보기</summary>

본문이 이미 8의 배수가 되는 날 `pad[0]` 이 된다 — 크기 0 배열은 제약 위반이다(6.7.6.2p1, GNU 는 확장으로 허용). `union { cfg_body_t b; uint8_t raw[ROUND_UP(sizeof(cfg_body_t), 8)]; }` 는 크기가 항상 올림 값이고 `raw` 가 지운 플래시(0xFF) 바이트 그대로를 보여 준다. 계약: `sizeof(cfg_rec_t) % 8 == 0`, `<= FLASH_PAGE`, 그리고 `sizeof(cfg_body_t) == CFG_BODY_SIZE_V2 && CFG_VERSION == 2` — 누가 필드를 추가하면 빌드가 "버전을 올리고 마이그레이션을 추가하라" 고 말한다. 이미 출하된 장치의 플래시에 옛 레이아웃이 남아 있기 때문이다. 크기가 같은 변경(필드 타입 교체)까지 잡으려면 `PIN_FIELD` 를 더한다.

</details>

### 연습 4 — 디스패치 표의 구멍

`cmd_t` 4개에 대한 함수 포인터 표를 지정 초기화자로 만들고 길이 계약을 걸어라. 그리고 크기를 `[CMD_COUNT]` 로 적고 `CMD_WRITE` 를 빠뜨린 표를 만들어, 길이 계약은 통과하지만 런타임 `first_hole()` 이 잡는 것을 보여라. `dispatch()` 는 범위 밖 opcode 와 구멍을 모두 `-1` 로.

<details>
<summary>답 보기</summary>

`table_ok[] = { [CMD_PING] = h_ping, ... }` 는 크기가 4 이고 `ARRAY_SIZE == CMD_COUNT` 가 의미 있다. `table_sized[CMD_COUNT]` 는 같은 단언이 항상 참이라 practice.c 에서 메시지를 아예 "always true — this check is vacuous" 로 적어 두었다. `first_hole` 은 첫 `NULL` 인덱스(2 = `CMD_WRITE`)를 돌려준다. 실무에서는 이 검사를 부팅 self-test 나 단위 테스트에 넣고, `dispatch` 는 와이어에서 온 opcode 를 `op >= n` 과 `NULL` 둘 다로 거른다 — Cortex-M 에서 `NULL` 함수 포인터 호출(`BLX` 로 주소 0)은 Thumb 비트(bit 0)가 0 이라 INVSTATE UsageFault 가 되고, UsageFault 를 따로 켜지 않았다면 HardFault 로 승격된다. C 로 보면 그냥 UB 다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 14/14 passed
```

## 10. 핵심 정리

- **가정은 빌드 실패로 적는다**: 매크로 상수는 `#if`/`#error`, 타입·레이아웃·enum·표·마스크는 `_Static_assert`, 식 자리는 `BUILD_BUG_ON_ZERO` (`0 * sizeof(struct { _Static_assert(...); char c_; })`), 섹션 크기는 링커 `ASSERT`.
- 계약 도구는 **정수 상수식 매크로** (const 변수·함수 호출은 상수식 아님). 그래서 같은 매크로를 런타임 검사에도 재사용한다.
- 시프트 없이 필드: `LOWBIT = m & (0u - m)`, `FIELD_MAX = m / LOWBIT`, 연속 = `x & (x+1) == 0`, 겹침 없음 = `OR == 합` (XOR 는 마스크 2개일 때만). 비교는 `uintmax_t`. 경계: 0, `0xFFFFFFFF`, 음수, 2^32.
- 옛 음수 배열 `BUILD_BUG_ON` 은 런타임 값이 들어오면 **VLA 로 조용히 통과** 한다. `_Static_assert` 판은 상수가 아닌 것도 에러.
- 와이어/하드웨어 구조체: 고정 폭 정수만, **스펙 표 한 줄 = `PIN_FIELD` 한 줄** (오프셋 + 타입) + `sizeof` + 정렬. enum·포인터·`long` 은 타깃마다 크기가 다르다.
- 표는 **크기를 비운** 지정 초기화자 + `ARRAY_SIZE == COUNT`. `tbl[COUNT]` 는 계약을 공허하게 만든다.
- 계약은 **그 빌드** 만 검사한다 → 호스트·타깃 빌드 모두. 그리고 **실패를 확인한다**: 돌연변이(mutants.sh), clang `-verify`, CI 의 "빌드 실패 기대" 테스트.
- 계약은 우리 코드에 대한 것 — 외부 바이트와 런타임 값은 같은 규칙의 런타임 검증으로.

## 참고자료

- C11 (N1570) — 6.7.10 Static assertions, 6.6 Constant expressions (p6 정수 상수식), 6.10.1p4 `#if` 의 식별자 → 0, 6.7.2.1 (p1 멤버 목록의 static_assert, p4 비트필드 폭, p8 이름 있는 멤버 없는 구조체), 6.7.6.2 배열 선언자 (p1 크기 > 0, p5 VLA), 6.5.1.1 Generic selection, 6.7.2.2p4 enum 의 호환 타입, 6.7.9p22 크기 없는 배열의 초기화, 6.2.8·6.7.5 정렬, 7.2p3 `static_assert` 매크로, 7.19p3 `offsetof` — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- C17 DR 481 (`_Generic` 제어식의 lvalue 변환) — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/summary.htm>
- Linux kernel — `include/linux/build_bug.h` (`BUILD_BUG_ON`, `BUILD_BUG_ON_ZERO`, `static_assert`), `include/linux/bitfield.h` (`FIELD_PREP`, `FIELD_PREP_CONST`), `include/linux/bits.h` (`GENMASK`) — <https://github.com/torvalds/linux/blob/master/include/linux/build_bug.h>
- Clang — `-verify` 모드 (Internals Manual, "Verifying Diagnostics") — <https://clang.llvm.org/docs/InternalsManual.html#verifying-diagnostics>
- GNU ld — `ASSERT(exp, message)` — <https://sourceware.org/binutils/docs/ld/Miscellaneous-Commands.html>
- ARM — Procedure Call Standard for the Arm Architecture (AAPCS32, enum 크기와 `Tag_ABI_enum_size`) — <https://github.com/ARM-software/abi-aa/releases>
- 관련 레슨: C-B12 (패딩·offsetof·`_Static_assert` 기초), C-M01 (레지스터 구조체 고정), C-M03 (비트필드 — 정적 단언 불가), C-M04 (packed·비정렬), C-M05 (명시적 직렬화), C-M11 (`IS_ARRAY`, const 는 상수식 아님), C-M13 (strict aliasing), C-B15 (`#`/`##` 두 단계), C-A01 (쓰기 순서 mock), C-A06 (`offsetof`/`container_of` 구현), C-A07 (X-macro 로 표 동기화)
