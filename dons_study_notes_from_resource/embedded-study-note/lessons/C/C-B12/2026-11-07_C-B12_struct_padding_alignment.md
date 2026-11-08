---
id: C-B12
time: 50분
updated: 2026-11-07
---
# C-B12 구조체 기초: 패딩·정렬·sizeof·offsetof

## 학습 목표

- 정렬(alignment) 규칙 두 줄만으로 어떤 구조체든 **sizeof / alignof / offsetof 를 손으로** 계산한다.
- 손 계산 값을 `_Static_assert` 로 **컴파일 타임에 고정**해서, 레이아웃이 바뀌면 빌드가 깨지게 만든다.
- 필드를 **정렬 내림차순**으로 재배열해 크기를 최소화하고, 왜 그게 최적인지 설명한다.
- 꼬리 패딩(tail padding)·구조체 배열·중첩 구조체·`_Alignas` 를 같은 규칙으로 처리한다.
- LP64(arm64 호스트)와 ILP32(Cortex-M)에서 레이아웃이 **어디서 달라지는지** 안다.
- 데이터시트 오프셋 표와 똑같은 **레지스터 블록 구조체**를 reserved 필드로 만들고 `offsetof` 로 검증한다.

## 1. 문제

펌웨어에서 구조체는 곧 메모리 지도다 — 레지스터 블록, DMA 디스크립터, 호스트와 주고받는 패킷이 전부 "몇 번 바이트에 무엇이 있나"로 정의된다. 면접에서도 `sizeof(struct {char; int; char;})` 는 단골 워밍업이고, 꼬리질문은 거의 항상 "줄이려면?", "Cortex-M 에선?", "레지스터 맵은 어떻게 검증해?"로 이어진다.

규칙은 두 줄이다 (이 레슨의 ABI: macOS/Linux arm64 = **LP64**, Cortex-M = **ILP32 AAPCS**).

1. 각 필드는 **자기 정렬값의 배수** 오프셋에 놓인다. 모자라면 앞에 **내부 패딩(padding)** 을 넣는다.
2. 구조체의 정렬 = 멤버 정렬의 최댓값, `sizeof` = 그 정렬의 배수로 올림 → 끝에 **꼬리 패딩**.

```svg
<svg viewBox="0 0 720 240" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">같은 필드 3개, 순서만 다름 — 바이트 지도 (LP64 · ILP32 동일)</text>
  <text x="10" y="70" fill="currentColor">struct s1 → 12 B</text>
  <text x="10" y="88" fill="currentColor" font-size="12" opacity=".8">{u8 a; u32 b; u8 c;}</text>
  <rect x="190" y="55" width="40" height="30" class="f-acc" stroke="currentColor"/><text x="210" y="75" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="13">a</text><rect x="230" y="55" width="40" height="30" class="f-warn" stroke="currentColor"/><rect x="270" y="55" width="40" height="30" class="f-warn" stroke="currentColor"/><rect x="310" y="55" width="40" height="30" class="f-warn" stroke="currentColor"/>
  <rect x="350" y="55" width="40" height="30" class="f-acc" stroke="currentColor"/><text x="370" y="75" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="13">b</text><rect x="390" y="55" width="40" height="30" class="f-acc" stroke="currentColor"/><text x="410" y="75" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="13">b</text><rect x="430" y="55" width="40" height="30" class="f-acc" stroke="currentColor"/><text x="450" y="75" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="13">b</text><rect x="470" y="55" width="40" height="30" class="f-acc" stroke="currentColor"/><text x="490" y="75" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="13">b</text>
  <rect x="510" y="55" width="40" height="30" class="f-acc" stroke="currentColor"/><text x="530" y="75" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="13">c</text><rect x="550" y="55" width="40" height="30" class="f-warn" stroke="currentColor"/><rect x="590" y="55" width="40" height="30" class="f-warn" stroke="currentColor"/><rect x="630" y="55" width="40" height="30" class="f-warn" stroke="currentColor"/>
  <text x="190" y="102" fill="currentColor" text-anchor="middle" font-size="12" opacity=".7">0</text><text x="350" y="102" fill="currentColor" text-anchor="middle" font-size="12" opacity=".7">4</text><text x="510" y="102" fill="currentColor" text-anchor="middle" font-size="12" opacity=".7">8</text><text x="670" y="102" fill="currentColor" text-anchor="middle" font-size="12" opacity=".7">12</text>
  <text x="10" y="150" fill="currentColor">struct s1_min → 8 B</text>
  <text x="10" y="168" fill="currentColor" font-size="12" opacity=".8">{u32 b; u8 a; u8 c;}</text>
  <rect x="190" y="135" width="40" height="30" class="f-acc" stroke="currentColor"/><text x="210" y="155" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="13">b</text><rect x="230" y="135" width="40" height="30" class="f-acc" stroke="currentColor"/><text x="250" y="155" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="13">b</text><rect x="270" y="135" width="40" height="30" class="f-acc" stroke="currentColor"/><text x="290" y="155" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="13">b</text><rect x="310" y="135" width="40" height="30" class="f-acc" stroke="currentColor"/><text x="330" y="155" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="13">b</text>
  <rect x="350" y="135" width="40" height="30" class="f-acc" stroke="currentColor"/><text x="370" y="155" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="13">a</text><rect x="390" y="135" width="40" height="30" class="f-acc" stroke="currentColor"/><text x="410" y="155" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="13">c</text><rect x="430" y="135" width="40" height="30" class="f-warn" stroke="currentColor"/><rect x="470" y="135" width="40" height="30" class="f-warn" stroke="currentColor"/>
  <text x="190" y="182" fill="currentColor" text-anchor="middle" font-size="12" opacity=".7">0</text><text x="350" y="182" fill="currentColor" text-anchor="middle" font-size="12" opacity=".7">4</text><text x="510" y="182" fill="currentColor" text-anchor="middle" font-size="12" opacity=".7">8</text>
  <rect x="190" y="200" width="16" height="14" class="f-acc" stroke="currentColor"/><text x="212" y="212" fill="currentColor">데이터</text>
  <rect x="280" y="200" width="16" height="14" class="f-warn" stroke="currentColor"/><text x="302" y="212" fill="currentColor">패딩: s1 은 6 B(50%), s1_min 은 꼬리 2 B</text>
  <text x="10" y="232" fill="currentColor">규칙: 필드 오프셋은 자기 정렬의 배수, sizeof 는 구조체 정렬(최대 멤버 정렬)의 배수.</text>
</svg>
```

### Part A — 손 계산 → 표 → `_Static_assert` 로 고정

`starter.c` 의 구조체 6개 `s1`~`s6` 에 대해 `sizeof`, `alignof`, 각 필드 `offsetof` 를 **먼저 종이에** 계산해 `enum` 표(`S1_SIZE = TODO, ...`)에 적는다. `struct s1 arr[4]` 의 크기와 `arr[1].b` 의 바이트 위치도 적는다.

- 실행하면 런타임 `CHECK` 가 표와 컴파일러 값을 비교한다 (틀린 칸만 FAIL).
- 전부 PASS 하면 `#define PIN_LAYOUT 0` 을 `1` 로 바꾼다 → 같은 값들이 `_Static_assert` 로 묶여, 이후 누가 필드를 바꾸면 **컴파일이 실패**한다.
- `s6` 는 `long` 과 포인터가 들어 있어 ABI 마다 다르다. 표는 LP64(이 맥) 기준으로 적고, ILP32 값은 5. 풀이 표로 비교한다.

> starter 는 일부러 `_Static_assert` 를 `#if PIN_LAYOUT` 뒤에 숨겨 두었다. 틀린 값이 있는 상태로도 **컴파일은 되고 런타임에 FAIL** 하게 해서, 한 칸씩 고쳐 나갈 수 있게 하려는 것이다.

### Part B — 레이아웃 계산기와 재배열

컴파일러가 하는 일을 직접 구현한다. 필드는 `{size, align}` 쌍(`field_t`)으로 주어진다.

- `align_up(x, a)` — `x` 를 2의 거듭제곱 `a` 의 배수로 올림.
- `compute_layout(f, n, off, &align)` — 선언 순서대로 배치해 각 오프셋, 구조체 정렬, `sizeof` 를 계산. 테스트는 `s1`~`s6` 에 더해 **중첩 구조체** `struct outer { u8 x; struct s3 in; u8 y; }` 와 **`_Alignas(16)` 멤버**가 있는 `struct dmabuf` 까지 컴파일러 결과와 대조한다.
- `min_layout_size(f, n)` — 순서를 마음대로 바꿀 수 있을 때 최소 `sizeof`.
- `s1_min`, `s4_min`, `s5_min`, `s6_min` — 필드 재배열로 실제 최소 크기 구조체를 만든다.

### Part C — 데이터시트 오프셋 표 → 레지스터 블록 구조체

가상의 SPI+DMA 컨트롤러 `SPIX` 의 레지스터 맵(아래 그림)대로 `SPIX_TypeDef` 를 완성한다. starter 에는 reserved 칸이 빠졌거나 개수가 틀려 있다. `offsetof` 검사와 `_Static_assert` 를 통과시키고, `spix_ch_start(p, ch, addr, cnt)` 가 **데이터시트 주소**(예: `CH[2].ADDR` = 0x048)에 정확히 써지는지 바이트 오프셋으로 다시 읽어 확인한다.

```svg
<svg viewBox="0 0 740 365" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">데이터시트 레지스터 맵 → SPIX_TypeDef → offsetof 로 한 줄씩 고정</text>
  <text x="390" y="46" fill="currentColor" font-size="12">각 줄 = _Static_assert(… , "…");</text>
  <text x="10" y="72" fill="currentColor" font-family="monospace" font-size="12">0x000</text><rect x="80" y="56" width="280" height="22" class="f-acc" stroke="currentColor"/><text x="220" y="72" fill="currentColor" text-anchor="middle" font-size="12">CR</text><text x="390" y="72" fill="currentColor" font-family="monospace" font-size="12">offsetof(SPIX_TypeDef, CR)  == 0x000</text>
  <text x="10" y="94" fill="currentColor" font-family="monospace" font-size="12">0x004</text><rect x="80" y="78" width="280" height="22" class="f-acc" stroke="currentColor"/><text x="220" y="94" fill="currentColor" text-anchor="middle" font-size="12">SR</text>
  <text x="10" y="116" fill="currentColor" font-family="monospace" font-size="12">0x008</text><rect x="80" y="100" width="140" height="22" class="f-acc" stroke="currentColor"/><text x="150" y="116" fill="currentColor" text-anchor="middle" font-size="12">DR (16b)</text><rect x="220" y="100" width="140" height="22" class="f-warn" stroke="currentColor"/><text x="290" y="116" fill="currentColor" text-anchor="middle" font-size="12">RESERVED0</text><text x="390" y="116" fill="currentColor" font-family="monospace" font-size="12">offsetof(SPIX_TypeDef, DR)  == 0x008</text>
  <text x="10" y="138" fill="currentColor" font-family="monospace" font-size="12">0x00C</text><rect x="80" y="122" width="280" height="22" class="f-acc" stroke="currentColor"/><text x="220" y="138" fill="currentColor" text-anchor="middle" font-size="12">BRR</text><text x="390" y="138" fill="currentColor" font-family="monospace" font-size="12">offsetof(SPIX_TypeDef, BRR) == 0x00C</text>
  <text x="10" y="160" fill="currentColor" font-family="monospace" font-size="12">0x010</text><rect x="80" y="144" width="280" height="22" class="f-warn" stroke="currentColor"/><text x="220" y="160" fill="currentColor" text-anchor="middle" font-size="12">RESERVED1[4]  (16 B)</text>
  <text x="10" y="182" fill="currentColor" font-family="monospace" font-size="12">0x020</text><rect x="80" y="166" width="70" height="22" class="f-acc" stroke="currentColor"/><text x="115" y="182" fill="currentColor" text-anchor="middle" font-size="12">CFG</text><rect x="150" y="166" width="70" height="22" class="f-acc" stroke="currentColor"/><text x="185" y="182" fill="currentColor" text-anchor="middle" font-size="12">CNT</text><rect x="220" y="166" width="70" height="22" class="f-acc" stroke="currentColor"/><text x="255" y="182" fill="currentColor" text-anchor="middle" font-size="12">ADDR</text><rect x="290" y="166" width="70" height="22" class="f-warn" stroke="currentColor"/><text x="325" y="182" fill="currentColor" text-anchor="middle" font-size="12">RES</text><text x="390" y="182" fill="currentColor" font-family="monospace" font-size="12">offsetof(SPIX_TypeDef, CH) == 0x020</text>
  <text x="10" y="204" fill="currentColor" font-family="monospace" font-size="12">0x030</text><rect x="80" y="188" width="280" height="22" class="f-mut" stroke="currentColor"/><text x="220" y="204" fill="currentColor" text-anchor="middle" font-size="12">CH[1]  CFG · CNT · ADDR · RES</text><text x="390" y="204" fill="currentColor" font-family="monospace" font-size="12">sizeof(SPIX_Chan)          == 0x010</text>
  <text x="10" y="226" fill="currentColor" font-family="monospace" font-size="12">0x040</text><rect x="80" y="210" width="280" height="22" class="f-mut" stroke="currentColor"/><text x="220" y="226" fill="currentColor" text-anchor="middle" font-size="12">CH[2]</text>
  <text x="10" y="248" fill="currentColor" font-family="monospace" font-size="12">0x050</text><rect x="80" y="232" width="280" height="22" class="f-mut" stroke="currentColor"/><text x="220" y="248" fill="currentColor" text-anchor="middle" font-size="12">CH[3]</text>
  <text x="10" y="270" fill="currentColor" font-family="monospace" font-size="12">0x060</text><rect x="80" y="254" width="280" height="22" class="f-warn" stroke="currentColor"/><text x="220" y="270" fill="currentColor" text-anchor="middle" font-size="12">RESERVED2[39]  (156 B)</text>
  <text x="10" y="292" fill="currentColor" font-family="monospace" font-size="12">0x0FC</text><rect x="80" y="276" width="280" height="22" class="f-mut" stroke="currentColor"/><text x="220" y="292" fill="currentColor" text-anchor="middle" font-size="12">VERSION (RO)</text><text x="390" y="292" fill="currentColor" font-family="monospace" font-size="12">offsetof(SPIX_TypeDef, VERSION) == 0x0FC</text>
  <text x="10" y="314" fill="currentColor" font-family="monospace" font-size="12">0x100</text><text x="390" y="314" fill="currentColor" font-family="monospace" font-size="12">sizeof(SPIX_TypeDef)       == 0x100</text>
  <text x="10" y="350" fill="currentColor">reserved 를 빠뜨리거나 개수를 틀리면 그 아래 레지스터가 전부 밀린다 → 컴파일 단계에서 잡자.</text>
</svg>
```

packed 구조체(`__attribute__((packed))`)와 비정렬 접근은 C-M04, 비트필드는 C-M03, 구조체를 그대로 보내지 않는 바이트 직렬화는 C-M05, `volatile` 레지스터 구조체 접근 자체는 C-M01 에서 다룬다. 여기서는 **자연 정렬(natural alignment)** 만 다룬다.

## 2. Problem (English)

> **Struct layout: padding, alignment, sizeof, offsetof.** Assume LP64 (AArch64) unless stated.
>
> 1. For the six structs below, compute by hand `sizeof`, `_Alignof`, and the offset of every member; then pin the values with `_Static_assert`. Also give `sizeof(struct s1[4])` and the byte offset of `arr[1].b`.
>    `s1 {u8 a; u32 b; u8 c;}` · `s2 {u32 b; u8 a; u8 c;}` · `s3 {u8 a; u16 b; u8 c; u64 d;}` · `s4 {u8 a; u64 d; u8 c;}` · `s5 {u16 a; u8 b[3]; u32 c; u8 d;}` · `s6 {u8 tag; long n; void *p; u16 len;}`
> 2. Implement `size_t align_up(size_t x, size_t a)`, `size_t compute_layout(const field_t *f, size_t n, size_t *off, size_t *align_out)` and `size_t min_layout_size(const field_t *f, size_t n)` where `field_t = {size, align}`. Your layout must match the compiler for nested structs and `_Alignas` members. Reorder the fields of `s1`, `s4`, `s5`, `s6` to minimise their size and argue why your order is optimal.
> 3. Given a datasheet register map (CR 0x000, SR 0x004, 16-bit DR 0x008, BRR 0x00C, reserved to 0x01F, four 16-byte DMA channel blocks at 0x020, reserved to 0x0FB, read-only VERSION at 0x0FC), write the register struct with reserved members and prove it matches the map at compile time. How does `s6` change on a Cortex-M (ILP32)?
>
> Example: `sizeof(struct s1) == 12`, `offsetof(struct s1, c) == 8`, `align_up(13, 8) == 16`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "1/54 passed" — 유일한 PASS 는 BRR 오프셋 (암묵적 패딩 덕에 우연히 맞음, 풀이 C 참고)
# Part A 가 다 PASS 하면 PIN_LAYOUT 을 1 로 → 빌드 자체가 검증이 된다
```

> 맥 SDK 27.2에서 `ld: tapi error` 가 나면 `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` 후 다시 빌드.

## 4. 힌트

<details>
<summary>힌트 1 — 표를 한 칸씩 채우는 법</summary>

`pos = 0` 에서 시작해 필드마다: ① `pos` 를 그 필드 정렬의 배수로 올림 → 그게 오프셋 ② `pos += 크기`. 마지막에 `pos` 를 최대 정렬의 배수로 올리면 `sizeof`. `uint8_t b[3]` 처럼 배열은 크기 3, 정렬은 **원소** 정렬 1.

</details>

<details>
<summary>힌트 2 — align_up 은 한 줄</summary>

`a` 가 2의 거듭제곱이면 `a - 1` 은 하위 비트 마스크다. `x + (a - 1)` 로 다음 경계를 넘긴 뒤 하위 비트를 지운다 (`& ~(a - 1)`). C-B01 의 마스크 감각 그대로.

</details>

<details>
<summary>힌트 3 — 최소 크기는 어떤 순서?</summary>

정렬이 큰 것부터 놓으면 앞 필드들의 크기 합이 항상 다음 필드 정렬의 배수가 된다 → 내부 패딩 0. 남는 건 꼬리 패딩뿐이고 그건 피할 수 없다.

</details>

<details>
<summary>힌트 4 — reserved 개수 계산</summary>

`(다음 레지스터 오프셋 − reserved 시작 오프셋) / 4` 워드. 0x060 부터 VERSION(0x0FC) 직전까지면 `(0x0FC − 0x060) / 4`. 16비트 레지스터 뒤의 2바이트도 `uint16_t` 로 **명시**한다.

</details>

## 5. 풀이

### Part A — 규칙대로 한 칸씩

C 표준이 정하는 것과 ABI 가 정하는 것을 먼저 구분하자.

| 누가 정하나 | 내용 |
|---|---|
| C11 6.7.2.1p15 | 멤버는 **선언 순서대로 주소가 증가**한다. 구조체 **맨 앞에는 패딩이 없다** (구조체 주소 == 첫 멤버 주소). 컴파일러는 필드를 재배열할 수 없다. |
| C11 6.7.2.1p17 | 끝에 패딩이 있을 수 있다. |
| C11 6.5.3.4p4 | 구조체 `sizeof` 는 내부·꼬리 패딩을 **포함**한다. |
| C11 6.2.8 | 정렬값은 **구현 정의(implementation-defined)** 이고 항상 2의 거듭제곱. 멤버 정렬도 구현 정의 (부록 J.3.9). |
| ABI (AAPCS64, AAPCS) | 실제 숫자: 각 기본 타입의 크기·정렬, "구조체 정렬 = 최대 멤버 정렬". |

즉 "패딩이 몇 바이트냐"는 표준이 아니라 **ABI 가 결정**한다. 그래서 값은 `_Static_assert` 로 플랫폼마다 고정해 둬야 한다.

| 구조체 | 필드 (크기/정렬) → 오프셋 | 내부 패딩 | 끝 | 정렬 | sizeof (꼬리) |
|---|---|---|---|---|---|
| s1 | a(1/1)@0, b(4/4)@**4**, c(1/1)@8 | 3 | 9 | 4 | **12** (3) |
| s2 | b@0, a@4, c@5 | 0 | 6 | 4 | **8** (2) |
| s3 | a@0, b(2/2)@**2**, c@4, d(8/8)@**8** | 1 + 3 | 16 | 8 | **16** (0) |
| s4 | a@0, d(8/8)@**8**, c@16 | 7 | 17 | 8 | **24** (7) |
| s5 | a(2/2)@0, b(3/1)@2, c(4/4)@**8**, d@12 | 3 | 13 | 4 | **16** (3) |
| s6 LP64 | tag@0, n(8/8)@**8**, p(8/8)@16, len(2/2)@24 | 7 | 26 | 8 | **32** (6) |
| s6 ILP32 | tag@0, n(4/4)@**4**, p(4/4)@8, len@12 | 3 | 14 | 4 | **16** (2) |

배열: `sizeof(struct s1[4]) = 4 × 12 = 48`, `arr[1].b` = 12 + 4 = **16**. 여기서 꼬리 패딩의 존재 이유가 보인다.

```svg
<svg viewBox="0 0 760 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">꼬리 패딩(tail padding)이 있는 이유 — struct s1 arr[2]</text>
  <text x="10" y="70" fill="currentColor">실제 sizeof=12</text>
  <rect x="150" y="55" width="25" height="30" class="f-acc" stroke="currentColor"/><text x="162.5" y="75" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">a</text><rect x="175" y="55" width="25" height="30" class="f-warn" stroke="currentColor"/><rect x="200" y="55" width="25" height="30" class="f-warn" stroke="currentColor"/><rect x="225" y="55" width="25" height="30" class="f-warn" stroke="currentColor"/>
  <rect x="250" y="55" width="25" height="30" class="f-acc" stroke="currentColor"/><text x="262.5" y="75" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">b</text><rect x="275" y="55" width="25" height="30" class="f-acc" stroke="currentColor"/><text x="287.5" y="75" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">b</text><rect x="300" y="55" width="25" height="30" class="f-acc" stroke="currentColor"/><text x="312.5" y="75" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">b</text><rect x="325" y="55" width="25" height="30" class="f-acc" stroke="currentColor"/><text x="337.5" y="75" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">b</text>
  <rect x="350" y="55" width="25" height="30" class="f-acc" stroke="currentColor"/><text x="362.5" y="75" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">c</text><rect x="375" y="55" width="25" height="30" class="f-warn" stroke="currentColor"/><rect x="400" y="55" width="25" height="30" class="f-warn" stroke="currentColor"/><rect x="425" y="55" width="25" height="30" class="f-warn" stroke="currentColor"/>
  <rect x="450" y="55" width="25" height="30" class="f-acc" stroke="currentColor"/><text x="462.5" y="75" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">a</text><rect x="475" y="55" width="25" height="30" class="f-warn" stroke="currentColor"/><rect x="500" y="55" width="25" height="30" class="f-warn" stroke="currentColor"/><rect x="525" y="55" width="25" height="30" class="f-warn" stroke="currentColor"/>
  <rect x="550" y="55" width="25" height="30" class="f-acc" stroke="currentColor"/><text x="562.5" y="75" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">b</text><rect x="575" y="55" width="25" height="30" class="f-acc" stroke="currentColor"/><text x="587.5" y="75" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">b</text><rect x="600" y="55" width="25" height="30" class="f-acc" stroke="currentColor"/><text x="612.5" y="75" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">b</text><rect x="625" y="55" width="25" height="30" class="f-acc" stroke="currentColor"/><text x="637.5" y="75" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">b</text>
  <rect x="650" y="55" width="25" height="30" class="f-acc" stroke="currentColor"/><text x="662.5" y="75" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">c</text><rect x="675" y="55" width="25" height="30" class="f-warn" stroke="currentColor"/><rect x="700" y="55" width="25" height="30" class="f-warn" stroke="currentColor"/><rect x="725" y="55" width="25" height="30" class="f-warn" stroke="currentColor"/>
  <text x="150" y="50" fill="currentColor" font-size="12">arr[0]</text>
  <text x="450" y="50" fill="currentColor" font-size="12">arr[1]</text>
  <line x1="450" y1="52" x2="450" y2="90" stroke="currentColor" stroke-width="2"/>
  <text x="150" y="102" fill="currentColor" text-anchor="middle" font-size="12" opacity=".7">0</text><text x="250" y="102" fill="currentColor" text-anchor="middle" font-size="12" opacity=".7">4</text><text x="350" y="102" fill="currentColor" text-anchor="middle" font-size="12" opacity=".7">8</text><text x="450" y="102" fill="currentColor" text-anchor="middle" font-size="12" opacity=".7">12</text><text x="550" y="102" fill="currentColor" text-anchor="middle" font-size="12" opacity=".7">16</text><text x="650" y="102" fill="currentColor" text-anchor="middle" font-size="12" opacity=".7">20</text><text x="750" y="102" fill="currentColor" text-anchor="middle" font-size="12" opacity=".7">24</text>
  <text x="600" y="122" fill="currentColor" text-anchor="middle" font-size="12">arr[1].b @16 ✓ (4의 배수)</text>
  <text x="10" y="170" fill="currentColor">가상 sizeof=9</text>
  <text x="10" y="188" fill="currentColor" font-size="12" opacity=".8">(꼬리 패딩 없음)</text>
  <rect x="150" y="160" width="25" height="30" class="f-acc" stroke="currentColor"/><text x="162.5" y="180" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">a</text><rect x="175" y="160" width="25" height="30" class="f-warn" stroke="currentColor"/><rect x="200" y="160" width="25" height="30" class="f-warn" stroke="currentColor"/><rect x="225" y="160" width="25" height="30" class="f-warn" stroke="currentColor"/>
  <rect x="250" y="160" width="25" height="30" class="f-acc" stroke="currentColor"/><text x="262.5" y="180" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">b</text><rect x="275" y="160" width="25" height="30" class="f-acc" stroke="currentColor"/><text x="287.5" y="180" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">b</text><rect x="300" y="160" width="25" height="30" class="f-acc" stroke="currentColor"/><text x="312.5" y="180" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">b</text><rect x="325" y="160" width="25" height="30" class="f-acc" stroke="currentColor"/><text x="337.5" y="180" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">b</text>
  <rect x="350" y="160" width="25" height="30" class="f-acc" stroke="currentColor"/><text x="362.5" y="180" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">c</text><rect x="375" y="160" width="25" height="30" class="f-mut" stroke="currentColor"/><text x="387.5" y="180" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">a</text><rect x="400" y="160" width="25" height="30" class="f-warn" stroke="currentColor"/><rect x="425" y="160" width="25" height="30" class="f-warn" stroke="currentColor"/>
  <rect x="450" y="160" width="25" height="30" class="f-warn" stroke="currentColor"/><rect x="475" y="160" width="25" height="30" class="f-mut" stroke="currentColor"/><text x="487.5" y="180" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">b</text><rect x="500" y="160" width="25" height="30" class="f-mut" stroke="currentColor"/><text x="512.5" y="180" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">b</text><rect x="525" y="160" width="25" height="30" class="f-mut" stroke="currentColor"/><text x="537.5" y="180" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">b</text>
  <rect x="550" y="160" width="25" height="30" class="f-mut" stroke="currentColor"/><text x="562.5" y="180" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">b</text><rect x="575" y="160" width="25" height="30" class="f-mut" stroke="currentColor"/><text x="587.5" y="180" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">c</text>
  <text x="375" y="155" fill="currentColor" font-size="12">arr[1]</text>
  <line x1="375" y1="157" x2="375" y2="195" stroke="currentColor" stroke-width="2"/>
  <text x="150" y="207" fill="currentColor" text-anchor="middle" font-size="12" opacity=".7">0</text><text x="250" y="207" fill="currentColor" text-anchor="middle" font-size="12" opacity=".7">4</text><text x="350" y="207" fill="currentColor" text-anchor="middle" font-size="12" opacity=".7">8</text><text x="375" y="207" fill="currentColor" text-anchor="middle" font-size="12" opacity=".7">9</text><text x="475" y="207" fill="currentColor" text-anchor="middle" font-size="12" opacity=".7">13</text><text x="600" y="207" fill="currentColor" text-anchor="middle" font-size="12" opacity=".7">18</text>
  <text x="475" y="227" fill="currentColor" font-size="12">arr[1].b @13 ✗ — 4의 배수 아님 → 정렬 위반</text>
  <text x="10" y="250" fill="currentColor">배열 원소 사이엔 틈이 없다 (sizeof(T[n]) == n·sizeof(T)).</text>
  <text x="10" y="268" fill="currentColor">그래서 모든 arr[i] 가 정렬되려면 sizeof 자체가 정렬의 배수여야 한다.</text>
</svg>
```

**ABI 비교** — 고정 폭 정수만 쓴 `s1`~`s5` 는 두 ABI 에서 같고, `long`/포인터가 든 `s6` 만 달라진다. `clang --target` 으로 실제 확인한 값이다 (`abi_layouts.c`).

| 타입 | LP64 (macOS arm64) | LP64 (Linux aarch64) | ILP32 (Cortex-M, arm-none-eabi) |
|---|---|---|---|
| `short` / `int` | 2 / 4 | 2 / 4 | 2 / 4 |
| `long` | **8** | **8** | **4** |
| `long long`, `uint64_t` (크기/정렬) | 8 / 8 | 8 / 8 | 8 / **8** (AAPCS 더블워드 정렬) |
| 포인터, `size_t` | **8** | **8** | **4** |
| `double` (크기/정렬) | 8 / 8 | 8 / 8 | 8 / 8 |
| `long double` | 8 | 16 | 8 |
| `alignof(max_align_t)` | 8 | 16 | 8 |
| `sizeof(s6)` | 32 | 32 | 16 |

참고로 다른 ABI: Windows x64(LLP64)는 `long` = 4, 포인터 = 8 이라 `s6` = 24, i386 System V 는 구조체 안의 `uint64_t`/`double` 을 4바이트 정렬해서 `s4` = 16 (`clang -Xclang -fdump-record-layouts` 로 확인). "64비트 타입은 항상 8 정렬" 도 보편 법칙이 아니다.

```bash
cc -std=c11 -fsyntax-only abi_layouts.c                                         # LP64 assert 통과
clang --target=thumbv7em-none-eabi -ffreestanding -std=c11 -fsyntax-only abi_layouts.c   # ILP32 assert 통과
clang --target=thumbv7em-none-eabi -ffreestanding -std=c11 -fsyntax-only \
      -Xclang -fdump-record-layouts abi_layouts.c                                # 레이아웃 덤프
```

```text
         0 | struct s6                       (thumbv7em-none-eabi)
         0 |   uint8_t tag
         4 |   long n
         8 |   void * p
        12 |   uint16_t len
           | [sizeof=16, align=4]
```

컴파일러에게 패딩 위치를 물어보는 방법도 있다 — `-Wpadded` (실제 출력 일부):

```text
$ cc -std=c11 -fsyntax-only -Wpadded abi_layouts.c
abi_layouts.c:11:33: warning: padding struct 'struct s1' with 3 bytes to align 'b' [-Wpadded]
abi_layouts.c:11:8: warning: padding size of 'struct s1' with 3 bytes to alignment boundary [-Wpadded]
abi_layouts.c:14:33: warning: padding struct 'struct s4' with 7 bytes to align 'd' [-Wpadded]
abi_layouts.c:16:31: warning: padding struct 'struct s6' with 7 bytes to align 'n' [-Wpadded]
abi_layouts.c:16:8: warning: padding size of 'struct s6' with 6 bytes to alignment boundary [-Wpadded]
```

**PIN_LAYOUT 의 의미.** `_Static_assert(상수식, "메시지")` (C11 6.7.10)는 선언이라 파일 스코프에도, 함수 안에도 둘 수 있다. 값이 거짓이면 그 자리에서 컴파일 에러다. 실무에서는 `#if` 없이 구조체 정의 바로 아래에 둔다 — starter 의 스위치는 학습용일 뿐이다.

### Part B — 계산기와 "왜 정렬 내림차순이 최적인가"

`compute_layout` 은 Part A 에서 손으로 한 것을 그대로 옮긴 것이다.

| 단계 | 코드 | s5 예시 (pos 변화) |
|---|---|---|
| 내부 패딩 | `pos = align_up(pos, f[i].align)` | c 차례: 5 → 8 |
| 배치 | `off[i] = pos; pos += f[i].size` | c @8, pos 12 |
| 최대 정렬 추적 | `max_al = max(max_al, f[i].align)` | 4 |
| 꼬리 패딩 | `return align_up(pos, max_al)` | 13 → 16 |

- `align_up(x, a) = (x + a − 1) & ~(a − 1)` — `a` 가 2의 거듭제곱일 때만 맞다 (6.2.8 이 정렬은 항상 2의 거듭제곱이라고 보장하므로 안전).
- **중첩 구조체**는 그냥 `{sizeof(struct s3), alignof(struct s3)} = {16, 8}` 인 필드 하나다. `outer`: x@0, in@8, y@24 → 25 → **32**. 안쪽 구조체의 꼬리 패딩은 바깥에서 재사용되지 않는다 (C 에선 `in` 다음 필드가 그 틈에 들어갈 수 없다).
- **`_Alignas(16) uint8_t buf[4]`** 는 `{4, 16}`: tag@0, buf@16 → 20 → **32**, 구조체 정렬 16. `_Alignas` 는 정렬을 **올리기만** 한다 — 원래보다 약하게 지정하면 제약 위반(6.7.5)이라 컴파일 에러다. 줄이는 건 packed(C-M04)의 영역.
- `alignof` 는 `<stdalign.h>` 의 매크로로 `_Alignof` 와 같다. 배열 타입에 쓰면 원소 정렬을 돌려준다 (6.5.3.4p3) — 그래서 `F(uint8_t[3])` 가 `{3, 1}` 이 된다.

**최적성 증명 (스케치).** 모든 완전한 객체 타입은 `sizeof(T) % alignof(T) == 0` 이다 (배열 원소가 틈 없이 붙으면서 모두 정렬돼야 하므로). 필드를 정렬 내림차순으로 놓으면 i번째 필드 앞의 크기 합은 앞 필드들 정렬(≥ 지금 정렬, 모두 2의 거듭제곱)의 배수들의 합 → 지금 정렬의 배수 → **내부 패딩 0**. 그러면 `sizeof = align_up(Σsize, max_align)`. 어떤 순서든 `sizeof ≥ Σsize` 이고 `max_align` 의 배수여야 하므로 이게 하한이다. ∎ (단 `_Alignas` 멤버는 `{4, 16}` 처럼 크기가 정렬의 배수가 아닐 수 있어 이 증명 밖이다.)

| 구조체 | 원래 | Σsize | 최소 | 재배열 예 |
|---|---|---|---|---|
| s1 | 12 | 6 | **8** | `u32 b; u8 a; u8 c;` |
| s3 | 16 | 12 | 16 | 이미 최소 (내부 패딩 4 를 없애도 꼬리 4 가 생김) |
| s4 | 24 | 10 | **16** | `u64 d; u8 a; u8 c;` |
| s5 | 16 | 10 | **12** | `u32 c; u16 a; u8 b[3]; u8 d;` |
| s6 | 32 / 16 | 19 / 11 | **24** / **12** | `long n; void *p; u16 len; u8 tag;` |

재배열의 비용도 말할 수 있어야 한다: 의미상 함께 쓰는 필드가 떨어지면 캐시 지역성이 나빠질 수 있고, 외부와 공유하는 구조체(레지스터 맵, 파일 포맷)는 순서를 바꿀 **자유가 없다**.

### Part C — 레지스터 블록: reserved 로 오프셋을 "그린다"

| 칸 | 계산 | 필드 |
|---|---|---|
| 0x00A–0x00B | 16비트 DR 뒤 2바이트 | `uint16_t RESERVED0;` |
| 0x010–0x01F | (0x020 − 0x010) / 4 = 4 워드 | `uint32_t RESERVED1[4];` |
| 채널 0x02C | stride 0x10 − 12 = 4바이트 | `SPIX_Chan` 안에 `uint32_t RESERVED;` |
| 0x060–0x0FB | (0x0FC − 0x060) / 4 = 0x9C / 4 = 39 워드 | `uint32_t RESERVED2[39];` |

- starter 가 `1/54` 를 통과한 이유: `RESERVED0` 가 없어도 `uint32_t BRR` 의 정렬 때문에 컴파일러가 2바이트 **암묵적 패딩**을 넣어 BRR 이 0x00C 에 "우연히" 맞는다. 결과는 같아도 의도가 코드에 안 보이므로 CMSIS 헤더처럼 명시한다. (`-Wpadded` 가 바로 이 자리를 지적한다: `padding struct 'SPIX_Partial' with 2 bytes to align 'BRR'`.)
- 채널 구조체에 `RESERVED` 가 없으면 `sizeof(SPIX_Chan)` = 12 가 되어 `CH[1]` 부터 4바이트씩 밀린다 — 배열 stride 는 곧 `sizeof` 다 (Part A 의 배열 규칙).
- 레지스터는 전부 4바이트(또는 2바이트) 자연 정렬이라 이 구조체는 LP64 와 ILP32 에서 **똑같다**. 레지스터 구조체에 `long`·포인터·`enum` 을 쓰지 않는 이유다.
- `volatile const uint32_t VERSION` 은 CMSIS 의 `__I` (읽기 전용). `volatile` 은 레지스터 접근이 최적화로 사라지지 않게 한다 (C-B09, C-M01).

`demo_pin_fail.c` 는 reserved 를 3워드로 잘못 적은 버전이다. `_Static_assert` 가 컴파일 단계에서 잡는다 (실제 출력):

```text
$ cc -std=c11 -fsyntax-only demo_pin_fail.c
demo_pin_fail.c:17:16: error: static assertion failed due to requirement
      '__builtin_offsetof(SPIX_Partial, CH0_CFG) == 32': CH0_CFG must be at 0x020
demo_pin_fail.c:17:48: note: expression evaluates to '28 == 32'
1 error generated.
```

보드에서 "DMA 가 이상한 주소로 간다"를 디버깅하는 대신, 빌드가 "28 ≠ 32" 라고 알려준다. 레지스터 맵 표 한 줄마다 assert 한 줄 — 이게 이 레슨의 실무 결론이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 54/54 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o sol_san && ./sol_san   # 54/54, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `sizeof(struct)` = 멤버 크기 합이라고 가정 | 내부·꼬리 패딩 포함 (6.5.3.4p4) | 손 계산 + `_Static_assert(sizeof(T) == N, ...)` |
| 구조체를 통째로 `uart_send(&s, sizeof s)` / 파일에 `fwrite` | 패딩·엔디안·`long` 크기가 양쪽 ABI 마다 다름 (호스트 arm64 ↔ MCU Cortex-M) | 필드별로 바이트 직렬화 (C-M05) |
| 구조체를 `memcmp` 로 비교, 통째로 CRC/해시 | 패딩 바이트 값은 unspecified (6.2.6.1p6) — 멤버에 값을 저장해도 패딩은 아무 값일 수 있음 | 필드별 비교 (추가 연습 5) |
| 공유 구조체에 `long`, `size_t`, 포인터, `enum` | ILP32 / LP64 / LLP64 에서 크기가 다름 | `uint32_t`, `uint64_t` 등 고정 폭 |
| 16비트 레지스터 뒤 reserved 생략 | 암묵적 패딩으로 "우연히" 맞지만 의도가 안 보이고, 다음 필드가 16비트면 틀어짐 | `uint16_t RESERVED0;` 명시 |
| reserved 를 바이트 수로 적음 `uint32_t RES[0x9C]` | 배열 원소 단위는 워드 → 4배 | `uint32_t RES[0x9C / 4]` |
| 범위 끝을 포함해서 셈: (0x0FF − 0x060)/4 + 1 | VERSION(0x0FC)까지 먹어버림 | (다음 레지스터 − 시작) / 4 |
| `align_up(x, 12)` | 비트 마스크 방식은 2의 거듭제곱 전용 | `((x + a - 1) / a) * a` 또는 assert |
| 크기 줄이려고 `#pragma pack(1)` | 비정렬 접근: Cortex-M0 은 HardFault, M3/M4 도 LDRD/LDM 은 fault, 그리고 느림 | 재배열이 먼저. packed 는 C-M04 |

## 8. 면접 Q&A

<details>
<summary>Q1. struct { char a; int b; char c; } 의 sizeof 는? 줄이려면?</summary>

일반적인 32/64비트 ABI 에서 12: a@0, 3바이트 패딩, b@4, c@8, 꼬리 3바이트 (정렬 4). `int b; char a; char c;` 로 바꾸면 8. 규칙: 필드 오프셋은 자기 정렬의 배수, sizeof 는 최대 정렬의 배수. 정렬 내림차순 배치가 최소를 보장한다 (내부 패딩 0, 꼬리는 불가피).

</details>

<details>
<summary>Q2. 꼬리 패딩은 왜 필요한가요? 컴파일러가 알아서 필드 순서를 바꿔주진 않나요?</summary>

배열 원소는 틈 없이 붙어 있으므로(`sizeof(T[n]) == n*sizeof(T)`) `arr[1]` 의 멤버도 정렬되려면 `sizeof` 가 정렬의 배수여야 한다. 순서는 C11 6.7.2.1p15 가 "선언 순서대로 주소 증가"를 요구하므로 컴파일러가 바꿀 수 없다. 첫 멤버 앞에 패딩이 없는 것도 보장 — 그래서 구조체 포인터를 첫 멤버 포인터로 바꿔 쓰는 관용구가 성립한다.

</details>

<details>
<summary>Q3. 패딩 바이트에는 어떤 값이 들어 있나요? 보안·펌웨어 관점에서 문제는?</summary>

unspecified. 멤버에 값을 저장할 때도 패딩은 아무 값이 될 수 있다 (6.2.6.1p6). static 저장 기간 객체는 패딩까지 0 으로 초기화(6.7.9p10)되지만, 자동 변수의 `= {0}` 이 패딩을 0 으로 만드는지는 해석이 갈린다. 결과: `memcmp` 비교·CRC 가 흔들리고, 구조체를 통째로 유저 공간·네트워크로 내보내면 스택 쓰레기가 새는 **정보 유출**(Linux 커널의 고전 버그 유형). 대책: 내보내기 전 `memset` 후 필드 채우기, 또는 필드별 직렬화.

</details>

<details>
<summary>Q4. 같은 헤더를 호스트 툴(arm64 Linux)과 Cortex-M 펌웨어가 공유합니다. 무엇이 깨질 수 있나요?</summary>

`long`·포인터·`size_t` 크기(8 vs 4)가 달라 레이아웃이 바뀐다 (이 레슨의 `s6`: 32 vs 16). `enum` 크기도 ABI/옵션(`-fshort-enums`) 따라 다르다. 엔디안도 다를 수 있다. 대책: 고정 폭 타입만, 필드를 정렬 내림차순으로, 양쪽 빌드에 같은 `_Static_assert(sizeof/offsetof)` 를 두고, 와이어 포맷은 명시적 직렬화(C-M05).

</details>

<details>
<summary>Q5. 레지스터 구조체가 데이터시트와 맞는지 어떻게 보장하나요?</summary>

reserved 멤버로 빈 구간을 명시적으로 채우고(`uint32_t RESERVED1[(0x020-0x010)/4]`), 레지스터 맵 한 줄마다 `_Static_assert(offsetof(X_TypeDef, REG) == 0xNN, ...)` 와 블록 전체 `sizeof` 를 고정한다. 레지스터는 자연 정렬 고정 폭 타입만 써서 ABI 와 무관하게 만든다. 런타임엔 base 주소 + 오프셋으로 읽어 디버거 메모리 뷰와 대조할 수 있다. 꼬리질문 "비트필드로 필드까지 표현하면?" → 배치가 구현 정의라 피한다 (C-M03).

</details>

<details>
<summary>Q6. 정렬이 안 맞는 주소로 접근하면 Cortex-M 에서 무슨 일이 생기나요?</summary>

ARMv6-M(Cortex-M0/M0+)는 비정렬 접근이 전부 HardFault. ARMv7-M(M3/M4/M7)은 LDR/STR/LDRH/STRH 의 비정렬 접근을 하드웨어가 처리하지만(느림, `CCR.UNALIGN_TRP` 로 트랩 가능) LDRD/STRD/LDM/STM/LDREX 는 항상 UsageFault (UsageFault 비활성 시 HardFault 로 승격). C 수준에서는 잘못 정렬된 포인터로 접근하는 것 자체가 UB 라서 컴파일러가 LDRD 같은 명령을 고를 수 있다. 자연 정렬을 지키는 구조체에선 이 문제가 생기지 않는다 — packed 로 깨뜨릴 때 생긴다 (C-M04).

</details>

<details>
<summary>Q7. _Alignas 는 언제 쓰나요? 정렬을 낮출 수도 있나요?</summary>

DMA 버퍼를 캐시 라인(Cortex-M7 은 32바이트) 경계에 맞추거나, 하드웨어가 디스크립터 주소의 하위 비트를 무시할 때 쓴다: `_Alignas(32) uint8_t rx[64];`. 구조체 정렬과 `sizeof` 도 함께 올라간다 (추가 연습 3). 원래 요구보다 약하게 지정하는 건 제약 위반(6.7.5)이라 컴파일 에러 — 낮추는 건 packed. 동적 할당에서 정렬 보장은 `aligned_alloc` (C-A05).

</details>

<details>
<summary>Q8. offsetof 는 어떻게 구현되나요? 아무 멤버에나 써도 되나요?</summary>

`<stddef.h>` 매크로로 `size_t` 정수 상수식을 낸다 (7.19p3). 고전 구현 `((size_t)&((T *)0)->m)` 은 널 포인터 역참조 형태라 표준상 보장이 없고, 요즘 컴파일러는 `__builtin_offsetof` 를 쓴다 (위 에러 메시지에 그대로 보인다). 비트필드 멤버에 쓰면 UB. 여기서 `container_of` 가 나온다 (C-A06).

</details>

## 9. 추가 연습

### 연습 1 — 링버퍼 SRAM 예산

`struct sample { u8 ch; u32 ts; u8 flags; u16 val; }` 256개 링버퍼. 크기를 손으로 구하고, 재배열로 몇 바이트 아끼는지 계산하라.

<details>
<summary>답 보기</summary>

ch@0, ts@4, flags@8, val@10 → 12 B. `u32 ts; u16 val; u8 ch; u8 flags;` → 8 B. 256 × 4 = **1 KB** 절약 — 64 KB SRAM MCU 에선 큰 돈이다. 두 ABI 모두 동일 (고정 폭).

</details>

### 연습 2 — 플렉서블 배열 멤버와 꼬리 패딩

`struct pkt { u16 len; u8 type; u8 data[]; }` 에서 `offsetof(data)` 와 `sizeof` 는? payload n 바이트를 담을 최소 할당 크기 함수를 써라.

<details>
<summary>답 보기</summary>

data@3, `sizeof` = 4 (정렬 2 로 올림; FAM 은 크기에 안 들어가지만 꼬리 패딩은 남는다, 6.7.2.1p18). `malloc(sizeof(struct pkt) + n)` 은 1바이트를 낭비한다. 정확한 값은 `max(offsetof(struct pkt, data) + n, sizeof(struct pkt))` — n=10 이면 14 가 아니라 13.

</details>

### 연습 3 — 캐시 라인 정렬 DMA 디스크립터

`{ u32 addr; u16 len; u8 flags; _Alignas(32) u8 rx[64]; }` 의 `offsetof(rx)`, `alignof`, `sizeof` 는? 이걸 배열로 만들면 모든 `rx` 가 32 경계에 있나?

<details>
<summary>답 보기</summary>

rx@32 (7 → 32 로 25바이트 패딩), 정렬 32, `sizeof` = 96 (32 의 배수). 배열 stride 96 도 32 의 배수라 모든 원소의 `rx` 가 32 경계. Cortex-M7 에서 `SCB_InvalidateDCache_by_Addr` 가 라인 단위로 동작하므로, 버퍼가 다른 데이터와 캐시 라인을 공유하지 않게 하는 게 목적.

</details>

### 연습 4 — 중첩 구조체 손 계산 (IMU 패킷)

`{ u8 id; struct { i16 x, y, z; } acc; u32 ts; struct { float x, y, z; } gyro; }` 의 오프셋과 크기.

<details>
<summary>답 보기</summary>

acc 는 {6, 2} → @2, ts @8, gyro 는 {12, 4} → @12, 끝 24 → `sizeof` 24, 정렬 4. 안쪽 구조체도 "크기·정렬 쌍 하나"로 보면 끝.

</details>

### 연습 5 — 패딩 있는 구조체 비교

`struct s1` 두 개가 같은지 판단하는 함수를 써라. 왜 `memcmp(&x, &y, sizeof x) == 0` 은 안 되나?

<details>
<summary>답 보기</summary>

`x->a == y->a && x->b == y->b && x->c == y->c`. `s1` 에는 패딩 6바이트가 있고 그 값은 unspecified — 필드가 같아도 `memcmp` 가 다를 수 있다. 테스트는 두 구조체의 패딩을 `0xAA`/`0x55` 로 다르게 채운 뒤 필드별 비교가 참인지 본다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 12/12 passed
```

## 10. 핵심 정리

- 필드 오프셋 = 자기 정렬의 배수로 올림. 구조체 정렬 = 최대 멤버 정렬. `sizeof` = 그 배수로 올림 (꼬리 패딩).
- 순서는 선언 순서 고정, 맨 앞 패딩 없음(6.7.2.1p15). 패딩 양은 표준이 아니라 **ABI** 가 정한다.
- 꼬리 패딩은 배열 때문: `arr[i]` 가 전부 정렬되려면 `sizeof % alignof == 0`.
- 최소 크기 = 정렬 내림차순 배치 = `align_up(Σsize, max_align)`.
- `align_up(x, a) = (x + a - 1) & ~(a - 1)` (a 는 2의 거듭제곱).
- LP64 ↔ ILP32: `long`·포인터·`size_t` 만 8 ↔ 4. AAPCS 도 `uint64_t`/`double` 은 8 정렬. 공유 구조체는 고정 폭 타입만.
- 레지스터 블록: reserved 로 빈 구간 명시 + 표 한 줄마다 `_Static_assert(offsetof(...) == 0xNN)`.
- 패딩 값은 unspecified → `memcmp`·CRC·통째 전송 금지. packed/비트필드/직렬화는 C-M04/C-M03/C-M05.

## 참고자료

- C11 표준 초안 N1570 — 6.2.8 (Alignment of objects), 6.5.3.4 (sizeof, _Alignof), 6.7.2.1 (Structure specifiers), 6.7.5 (Alignment specifier), 6.7.10 (Static assertions), 7.19 (offsetof): <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- [Arm — Procedure Call Standard for the Arm Architecture (AAPCS32), 기본 타입 크기·정렬](https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst)
- [Arm — Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst)
- [Apple — Writing ARM64 code for Apple platforms (Darwin arm64 ABI 차이, long double = double)](https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms)
- [Eric S. Raymond — The Lost Art of Structure Packing](http://www.catb.org/esr/structure-packing/)
- [ARM CMSIS-Core — 디바이스 헤더의 레지스터 구조체·`__I/__IO`·RESERVED 관례](https://arm-software.github.io/CMSIS_6/latest/Core/index.html)
- ARMv7-M Architecture Reference Manual — A3.2 Alignment support (비정렬 접근과 UNALIGN_TRP)
