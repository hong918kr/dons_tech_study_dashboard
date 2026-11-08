---
id: C-M04
time: 75분
updated: 2026-11-07
---
# C-M04 Packed 구조체와 비정렬 접근

## 학습 목표

- 바이트 스트림의 **아무 오프셋**에 놓인 프로토콜 헤더(1+2+4 바이트)를 `__attribute__((packed))` 구조체 캐스팅과 `memcpy` 두 방식으로 파싱하고, 둘이 같은 값·같은 기계어가 됨을 확인한다.
- **자연 정렬(non-packed) 구조체를 홀수 주소에 캐스팅**하면 생기는 두 가지 버그(레이아웃 불일치 + 비정렬 UB)를 `-fsanitize=alignment` 보고로 보여줄 수 있다.
- "packed 멤버 **접근**은 안전, packed 멤버의 **주소**(`&w->len`)는 위험" 을 설명하고, `-Waddress-of-packed-member` 가 잡는 것과 **못 잡는 것**(배열 멤버 decay)을 구분한다.
- Cortex-M4(ARMv7-M)와 Cortex-M0(ARMv6-M)에서 같은 C 코드가 어떤 명령(`LDR` 비정렬 / `LDRB`×4 / `LDM`)으로 바뀌는지 실제 asm 으로 읽고, 어느 것이 fault 나는지 말할 수 있다.
- 오버플로 안전한 경계 검사와 재동기(resync)하는 스트림 프레이머로 UART/DMA 수신 경로를 작성한다.

## 1. 문제

센서·무선 모듈·호스트 PC 와 주고받는 패킷은 "몇 번 바이트에 무엇" 이라는 **와이어 포맷(wire format)** 으로 정의되고, 그 헤더는 UART/DMA 버퍼의 아무 위치에나 떨어진다. 면접에서 "이 바이트 버퍼에서 헤더를 꺼내 보세요" 에 `*(struct hdr *)buf` 로 답하면 꼬리질문은 거의 항상 "정렬은요? packed 면 다 해결되나요? Cortex-M0 에서는요?" 로 이어진다.

이번 레슨의 헤더는 7바이트, 패딩 없음, 리틀 엔디안이다.

| 오프셋 | 크기 | 필드 | 설명 |
|---|---|---|---|
| 0 | 1 | `type` | 메시지 종류 |
| 1 | 2 | `len` | payload 길이 |
| 3 | 4 | `seq` | 시퀀스 번호 |

C-B12 의 규칙대로 같은 필드를 평범한 구조체로 선언하면 컴파일러는 `len` 앞에 1바이트, 즉 `len@2, seq@4, sizeof 8` 로 만든다. 와이어 포맷과 **모양부터 다르다**. 게다가 헤더가 버퍼의 홀수 오프셋에 오면 필드 주소도 정렬이 깨진다. 아래 그림은 `demo_misaligned_cast.c` 를 실제로 돌린 결과다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">rx[] (4 정렬 버퍼) — 헤더는 sync 뒤 오프셋 1 에 있다</text>
  <g font-family="monospace" font-size="12" fill="currentColor" text-anchor="middle">
    <text x="105" y="48">0</text><text x="175" y="48">1</text><text x="245" y="48">2</text><text x="315" y="48">3</text>
    <text x="385" y="48">4</text><text x="455" y="48">5</text><text x="525" y="48">6</text><text x="595" y="48">7</text><text x="665" y="48">8</text>
  </g>
  <rect x="70" y="56" width="70" height="30" class="f-mut" stroke="currentColor"/>
  <rect x="140" y="56" width="70" height="30" class="f-acc" stroke="currentColor"/>
  <rect x="210" y="56" width="140" height="30" class="f-ok" stroke="currentColor"/>
  <rect x="350" y="56" width="280" height="30" class="f-acc" stroke="currentColor"/>
  <rect x="630" y="56" width="70" height="30" stroke="currentColor" fill="none"/>
  <g font-family="monospace" font-size="12" fill="currentColor" text-anchor="middle">
    <text x="105" y="76">A5</text><text x="175" y="76">02</text><text x="245" y="76">10</text><text x="315" y="76">00</text>
    <text x="385" y="76">78</text><text x="455" y="76">56</text><text x="525" y="76">34</text><text x="595" y="76">12</text><text x="665" y="76">00</text>
  </g>
  <text x="10" y="76" fill="currentColor" font-size="12">바이트</text>
  <text x="10" y="118" fill="currentColor" font-weight="bold">packed (7 B, align 1)</text>
  <rect x="140" y="128" width="70" height="26" class="f-acc" stroke="currentColor"/>
  <rect x="210" y="128" width="140" height="26" class="f-ok" stroke="currentColor"/>
  <rect x="350" y="128" width="280" height="26" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="175" y="146">type</text><text x="280" y="146">len @+1</text><text x="490" y="146">seq @+3</text>
  </g>
  <text x="10" y="176" fill="currentColor" font-family="monospace" font-size="12">→ type=2 len=16 seq=0x12345678   ✓ (UBSan 조용함)</text>
  <text x="10" y="210" fill="currentColor" font-weight="bold">자연 정렬 nat_hdr (8 B, align 4)</text>
  <rect x="140" y="220" width="70" height="26" class="f-acc" stroke="currentColor"/>
  <rect x="210" y="220" width="70" height="26" class="f-warn" stroke="currentColor"/>
  <rect x="280" y="220" width="140" height="26" class="f-warn" stroke="currentColor"/>
  <rect x="420" y="220" width="280" height="26" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="175" y="238">type</text><text x="245" y="238">pad</text><text x="350" y="238">len @+2</text><text x="560" y="238">seq @+4</text>
  </g>
  <text x="10" y="268" fill="currentColor" font-family="monospace" font-size="12">→ type=2 len=30720 seq=0x00123456   ✗ 레이아웃이 틀려 엉뚱한 바이트를 읽음</text>
  <text x="10" y="288" fill="currentColor" font-size="12">+ 주소 4k+1 은 align 4 위반 → UBSan "member access within misaligned address" 6건, M0 에선 HardFault</text>
</svg>
```

> 엔디안: 호스트(arm64 맥)도 Cortex-M 도 리틀 엔디안이라 이 레슨은 **호스트 순서 그대로** 읽는다 (코드에 `#error` 가드). 빅 엔디안 피어나 엔디안 독립 직렬화(`get_le16`/`put_be32`)는 C-M05, 바이트 스왑 자체는 C-B13. 정렬·패딩·`offsetof` 기초는 C-B12, strict aliasing 의 깊은 이야기는 C-M13, `memcpy` 를 직접 구현하는 건 C-M08 에서 다룬다.

### Part A — 같은 헤더를 두 가지로 파싱

- `struct wire_hdr` 를 `__attribute__((packed))` 로 선언해 `sizeof == 7`, `len@1`, `seq@3` 이 되게 한다.
- `parse_hdr_packed(buf, n, off, &out)` — `buf + off` 를 `const struct wire_hdr *` 로 캐스팅해 멤버를 읽고 앱용 자연 정렬 구조체 `hdr_t` 에 담는다.
- `parse_hdr_memcpy(buf, n, off, &out)` — 같은 일을 필드마다 `memcpy` 로 지역 변수에 복사해서.
- 둘 다 바이트가 모자라면 `-1`, `out` 은 건드리지 않는다. 테스트: 오프셋 0..7 전부 + 무작위 바이트·오프셋 300회를 바이트 조립 참조 구현과 대조.

### Part B — 경계 · 쓰기 · packed 멤버의 주소

- `span_ok(n, off, len)` — `[off, off+len)` 이 버퍼 안인가. `off = SIZE_MAX` 처럼 `off + len` 이 **넘쳐(wrap)** 도 정답이어야 한다.
- `write_hdr(buf, n, off, &h)` — `memcpy` 로 직렬화. 홀수 오프셋에 쓰고 주변 바이트(카나리)는 보존.
- `wire_len_add(w, d)` — 주어진 레거시 API `u16_add_sat(uint16_t *p, uint16_t d)` 로 `w->len` 에 포화 덧셈. **`&w->len` 을 넘기면 안 된다.** (넘기면 `-Werror` 빌드가 깨지는 것도 확인한다.)
- packed 의 성질 확인: `alignof == 1`, `sizeof(struct wire_hdr[3]) == 21`, 다른 구조체 안에 넣으면 앞 패딩이 0.

### Part C — DMA 수신 버퍼 스트림 프레이머

UART 를 DMA 로 받으면 버퍼에는 쓰레기·잘린 프레임·여러 프레임이 섞여 있다. 프레임 형식은 `[0xA5][헤더 7][payload len][chk]`, `chk` = 헤더+payload 의 XOR, `len ≤ 32`. `parse_stream(s, n, out, max, &consumed)` 를 작성하라.

- sync 가 아니면 1바이트 건너뜀. `len > 32` 이거나 체크섬이 틀리면 **가짜 sync** 로 보고 1바이트만 건너뛰어 재동기.
- 헤더나 프레임이 버퍼 끝에서 잘렸으면 멈추고 `*consumed` = 그 sync 위치 (다음 DMA 조각과 이어 붙일 자리).
- 퍼즈 테스트: 무작위 프레임 1–16개 + 쓰레기를 만들고, **무작위 위치에서 두 조각으로 잘라** 첫 조각 → 남은 것+둘째 조각 순으로 파싱해도 모든 헤더가 정확히 복원되는지 200회 확인.

관찰용 데모 3개(하네스와 별개): `demo_misaligned_cast.c`(UBSan), `demo_addr_packed.c`(경고+UBSan), `demo_asm_target.c`(Cortex-M4/M0 asm, 컴파일 전용).

## 2. Problem (English)

> **Parse a packed protocol header from an arbitrary offset in a byte stream.**
>
> Wire header (7 bytes, no padding, little-endian): `type:u8 @0, len:u16 @1, seq:u32 @3`. Application struct: `typedef struct { uint8_t type; uint16_t len; uint32_t seq; } hdr_t;` (naturally aligned).
>
> 1. Declare `struct wire_hdr` so that it overlays the wire format exactly. Implement `int parse_hdr_packed(const uint8_t *buf, size_t n, size_t off, hdr_t *out)` by casting `buf + off` to `const struct wire_hdr *`, and `int parse_hdr_memcpy(...)` using `memcpy` into locals. Return 0, or -1 (leaving `*out` untouched) when fewer than 7 bytes remain. `off` may be any value.
> 2. Implement `bool span_ok(size_t n, size_t off, size_t len)` that is correct even when `off + len` overflows, and `int write_hdr(uint8_t *buf, size_t n, size_t off, const hdr_t *h)`. Implement `void wire_len_add(struct wire_hdr *w, uint16_t d)` using the existing API `void u16_add_sat(uint16_t *p, uint16_t d)` — without passing an unaligned pointer to it.
> 3. Frames are `[0xA5][hdr][payload(len ≤ 32)][xor of hdr+payload]`. Implement `size_t parse_stream(const uint8_t *s, size_t n, frame_t *out, size_t max, size_t *consumed)` which resynchronises on bad length/checksum by skipping one byte and stops at a truncated frame, reporting in `*consumed` where the caller should resume.
>
> Then explain: what happens if you cast to a *non-packed* struct at an odd offset (show `-fsanitize=alignment`), why `&w->len` is dangerous, and what code `w->seq` becomes on Cortex-M4 vs Cortex-M0.
>
> Example: bytes `02 34 12 78 56 34 12` at offset 5 → `{type=2, len=0x1234, seq=0x12345678}`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # SDK 27.2 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "8/36 passed" — 통과하는 8개는 "-1 을 돌려야 하는" 경우와 카나리 검사뿐
```

관찰용 데모 (하네스 아님, UB 를 일부러 담았다 — `-Werror` 없이 빌드):

```bash
cc -std=c11 -g -O1 -fsanitize=alignment demo_misaligned_cast.c -o mcast && ./mcast   # 자연 정렬 캐스팅
cc -std=c11 -Wall -Wextra -O2 demo_addr_packed.c -o apk && ./apk                       # &w->len 경고
cc -std=c11 -g -O1 -fsanitize=alignment demo_addr_packed.c -o apk_san && ./apk_san     # 그 포인터의 런타임 보고
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm_target.c -o -
clang --target=thumbv6m-none-eabi  -mcpu=cortex-m0 -mthumb -O2 -S demo_asm_target.c -o -
```

## 4. 힌트

<details>
<summary>힌트 1 — packed 는 어디에 붙이나</summary>

`struct __attribute__((packed)) wire_hdr { ... };` 또는 닫는 중괄호 뒤 `} __attribute__((packed));`. 구조체 전체의 정렬이 1 이 되고 멤버 사이 패딩이 사라진다. 확인은 `sizeof`, `alignof`, `offsetof` 세 가지로.

</details>

<details>
<summary>힌트 2 — memcpy 버전은 "필드 하나 = memcpy 하나"</summary>

`uint32_t seq; memcpy(&seq, p + OFF_SEQ, sizeof seq);` — 목적지 `&seq` 는 정렬된 지역 변수, 원본은 아무 주소. `memcpy` 는 바이트 복사로 정의되므로 정렬 요구가 없다. 1바이트 필드는 그냥 `p[OFF_TYPE]`.

</details>

<details>
<summary>힌트 3 — off + len &lt;= n 은 왜 틀리나</summary>

`size_t` 는 부호 없는 정수라 `SIZE_MAX + 7` 은 6 으로 감긴다(wrap). 뺄셈 쪽으로 옮기되, 뺄셈이 음수가 되지 않도록 `off <= n` 을 먼저 확인한다.

</details>

<details>
<summary>힌트 4 — 포인터 API 에 packed 멤버를 넘기려면</summary>

API 가 원하는 건 "정렬된 `uint16_t` 의 주소" 다. 정렬된 걸 하나 만들면 된다: 지역 변수로 복사 → 그 주소를 넘김 → 결과를 멤버에 되쓰기.

</details>

<details>
<summary>힌트 5 — 프레이머의 상태는 인덱스 하나</summary>

`i` 를 0 부터 올리며: sync 아님 → `i++`. 헤더 파싱 실패(잘림) → `break`. 길이 이상 → `i++`. 프레임 잘림 → `break`. 체크섬 틀림 → `i++`. 성공 → 기록 후 `i += 9 + len`. 마지막에 `*consumed = i`. 잘림을 "건너뛰기" 로 처리하면 다음 조각과 이어 붙일 수 없다.

</details>

## 5. 풀이

### Part A — 두 방식, 같은 결과

```c
const struct wire_hdr *w = (const struct wire_hdr *)(buf + off);   /* packed: align 1 */
out->len = w->len;                                                  /* 비정렬 안전 코드 */

uint16_t len; memcpy(&len, buf + off + OFF_LEN, sizeof len);        /* memcpy */
```

**packed 가 하는 일** — GCC/Clang 확장(ISO C 아님)이다. 구조체에 붙이면 ① 모든 멤버의 정렬을 1 로 낮춰 멤버 사이·꼬리 패딩을 없애고 ② 구조체 자체의 정렬도 1 이 된다. 그래서 `(const struct wire_hdr *)(buf + off)` 는 **어떤 주소에서도** 정렬 요구를 만족한다. 그리고 컴파일러는 `w->len` 이라는 **식** 을 볼 때 "이 멤버는 정렬 1" 임을 알기 때문에 타깃이 허용하는 비정렬 로드 또는 바이트 로드를 고른다.

**C 표준 관점의 정리** (C11 N1570):

| 코드 | 판정 | 근거 |
|---|---|---|
| `(const struct nat_hdr *)(buf + 1)` (자연 정렬, align 4) | **UB** — 역참조 전, 변환만으로 | 6.3.2.3p7: 변환 결과가 참조 타입에 맞게 정렬되지 않으면 UB |
| `(const struct wire_hdr *)(buf + 1)` (packed) | 정렬 문제 없음 (align 1) | 단 packed 자체가 표준 밖 — 컴파일러 문서가 보장 |
| `w->len` 으로 `uint8_t` 배열을 `uint16_t` 로 읽기 | 엄밀히는 effective type 규칙 위반 | 6.5p7: 문자 타입은 무엇이든 별칭 가능, **반대 방향은 아님** |
| `memcpy(&len, p, 2)` | 완전히 정의됨 | 7.24.2.1: 문자 단위 복사. 정렬·별칭 규칙 모두 무관 |
| `_Alignas(1)` 로 정렬 낮추기 | 제약 위반(컴파일 에러) | 6.7.5p4: 원래보다 약한 정렬 지정 금지 → 낮추는 표준 수단은 없다 |

세 번째 줄이 packed 캐스팅의 숨은 비용이다. 정렬은 해결하지만 strict aliasing(C-M13)은 해결하지 않는다. 실무에서 Linux 커널은 `-fno-strict-aliasing` 으로 빌드하고, lwIP·TinyUSB 등은 packed 오버레이를 쓰되 컴파일러가 이 패턴을 깨지 않는다는 관행에 기댄다. 이식성·정확성이 최우선이면 `memcpy`, 가독성(필드 이름으로 접근)이 중요하면 packed — 이 레슨의 결론은 **"성능은 둘이 같으니 정확성으로 고른다"** 이다.

**성능이 같다는 증거** — `demo_asm_target.c` ①② 의 실제 출력 (Apple clang 21, `-O2`):

| 대상 | `seq_packed(w)` = `w->seq` | `seq_memcpy(p)` = `memcpy(&v, p+3, 4)` |
|---|---|---|
| arm64 (맥 호스트) | `ldur w0, [x0, #3]` | `ldur w0, [x0, #3]` |
| Cortex-M4 (`thumbv7em`) | `ldr.w r0, [r0, #3]` | `ldr.w r0, [r0, #3]` |
| Cortex-M0 (`thumbv6m`) | `ldrb`×4 + `lsls`/`adds` 6개 (22 B) | 완전히 같은 22 B |
| M4 + `-mno-unaligned-access` | `ldrb`×4 + `orr.w`×3 | 완전히 같음 |

`memcpy` 는 함수 호출이 아니다. 크기가 상수인 `memcpy` 는 컴파일러 내장(builtin)으로 바뀌어, 타깃이 할 수 있는 가장 좋은 로드 하나가 된다. "memcpy 는 느리니까 캐스팅" 은 20년 전 이야기다.

Cortex-M0 의 실제 출력 (`seq_packed`):

```text
seq_packed:
	ldrb	r1, [r0, #3]
	ldrb	r2, [r0, #4]
	lsls	r2, r2, #8
	adds	r1, r2, r1
	ldrb	r2, [r0, #5]
	lsls	r2, r2, #16
	ldrb	r0, [r0, #6]
	lsls	r0, r0, #24
	adds	r0, r0, r2
	adds	r0, r0, r1
	bx	lr
```

ARMv6-M 은 비정렬 접근이 **전부** HardFault 이므로 컴파일러가 알아서 바이트 4개를 조립한다. 정확하지만 크기가 크다: 오브젝트 전체 `.text` 가 M4 68 B vs M0 130 B (`xcrun llvm-size -A`), `seq_packed` 하나만 6 B vs 22 B. packed 구조체를 M0 의 핫 루프에서 남발하면 코드 크기와 사이클이 함께 늘어난다.

### Part A' — 자연 정렬 구조체를 캐스팅하면 (demo_misaligned_cast.c)

```text
$ cc -std=c11 -g -O1 -fsanitize=alignment demo_misaligned_cast.c -o mcast && ./mcast
demo_misaligned_cast.c:23:25: runtime error: member access within misaligned address 0x0001007540c1 for type 'const struct nat_hdr', which requires 4 byte alignment
0x0001007540c1: note: pointer points here
 00 00 00  a5 02 10 00 78 56 34 12  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  00 00 00 00 00
              ^
demo_misaligned_cast.c:23:44: runtime error: load of misaligned address 0x0001007540c3 for type 'const uint16_t' (aka 'const unsigned short'), which requires 2 byte alignment
demo_misaligned_cast.c:23:62: runtime error: load of misaligned address 0x0001007540c5 for type 'const uint32_t' (aka 'const unsigned int'), which requires 4 byte alignment
(… 같은 줄의 member access 보고 포함 총 6건, SUMMARY 줄 생략)
nat   : type=2 len=30720 seq=0x00123456
packed: type=2 len=16 seq=0x12345678
```

읽을 거리 세 가지:

1. **packed 줄은 보고가 0건** — UBSan 의 alignment 검사는 "그 lvalue 타입이 요구하는 정렬" 과 비교하는데, packed 멤버의 요구 정렬은 1 이다.
2. **nat 값이 틀렸다** — 비정렬보다 먼저, `len@2`·`seq@4` 라는 **다른 레이아웃** 으로 읽어서 틀렸다. 맥(arm64)은 하드웨어가 비정렬 로드를 처리해 주므로 크래시 없이 "그럴듯한 쓰레기" 가 나온다. 가장 위험한 형태의 버그다.
3. 정적 경고도 있다. `-Wall -Wextra` 에는 없고 따로 켜야 한다:

```text
$ cc -std=c11 -Wall -Wextra -Wcast-align -fsyntax-only demo_misaligned_cast.c
demo_misaligned_cast.c:21:31: warning: cast from 'const uint8_t *' (aka 'const unsigned char *') to 'const struct nat_hdr *' increases required alignment from 1 to 4 [-Wcast-align]
```

(GCC 의 `-Wcast-align` 은 비정렬을 허용하지 않는 타깃에서만 경고하고, 항상 경고하게 하려면 `-Wcast-align=strict` 를 쓴다.)

### Part B — 경계 검사와 쓰기

```c
static bool span_ok(size_t n, size_t off, size_t len) {
    return off <= n && len <= n - off;
}
```

| 입력 (n, off, len) | `off + len <= n` (오답) | `off <= n && len <= n - off` |
|---|---|---|
| 64, 57, 7 | 64 ≤ 64 → 참 ✓ | 참 ✓ |
| 64, 58, 7 | 65 ≤ 64 → 거짓 ✓ | 거짓 ✓ |
| 64, `SIZE_MAX`, 7 | **6** ≤ 64 → 참 ✗ (wrap) | `off <= n` 거짓 ✓ |
| 64, `SIZE_MAX-3`, 7 | **3** ≤ 64 → 참 ✗ | 거짓 ✓ |
| 0, 0, 0 | 참 | 참 (빈 구간) |

부호 없는 덧셈의 wrap 은 UB 가 아니라 정의된 모듈러 연산(6.2.5p9)이라 sanitizer 도 조용하다. 실제로 오답 버전을 넣어 보면 `35/36 passed` 로 정확히 그 줄만 FAIL 한다. 프레임 길이·오프셋이 **패킷에서 온 값** 일 때 이 검사는 보안 경계다 (공격자가 `len` 을 고른다).

`write_hdr` 는 `parse_hdr_memcpy` 의 거울이다: `p[OFF_TYPE] = h->type; memcpy(p + OFF_LEN, &h->len, 2); memcpy(p + OFF_SEQ, &h->seq, 4);`. `&h->len` 은 정렬된 `hdr_t` 의 멤버라 주소를 꺼내도 안전하다. 경계 검사를 **먼저** 해서 실패하면 한 바이트도 쓰지 않는다 (테스트가 `buf[58]` 카나리로 확인).

### Part B' — packed 멤버의 주소는 정렬 정보를 잃는다

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">정렬 정보는 "식" 에 붙어 있다 — 포인터 타입으로 옮기는 순간 사라진다</text>
  <rect x="10" y="40" width="340" height="110" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="25" y="64" fill="currentColor" font-weight="bold">w-&gt;len = v;   (멤버 접근)</text>
  <text x="25" y="88" fill="currentColor">컴파일러: "packed 멤버 → 정렬 1"</text>
  <text x="25" y="112" fill="currentColor" font-family="monospace" font-size="12">M4: strh.w r1, [r0, #1]   (비정렬 OK)</text>
  <text x="25" y="134" fill="currentColor" font-family="monospace" font-size="12">M0: strb + lsrs + strb</text>
  <rect x="390" y="40" width="340" height="110" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="405" y="64" fill="currentColor" font-weight="bold">u16_add(&amp;w-&gt;len, 1);</text>
  <text x="405" y="88" fill="currentColor">타입: uint16_t *  → "정렬 2" 라고 가정</text>
  <text x="405" y="112" fill="currentColor" font-family="monospace" font-size="12">M4: ldrh / strh  (트랩 꺼져 있으면 동작)</text>
  <text x="405" y="134" fill="currentColor" font-family="monospace" font-size="12">M0: ldrh / strh  → 홀수 주소면 HardFault</text>
  <line x1="350" y1="95" x2="390" y2="95" stroke="currentColor" stroke-width="2"/>
  <polygon points="390,95 380,90 380,100" fill="currentColor"/>
  <text x="10" y="180" fill="currentColor">잡는 도구:</text>
  <text x="30" y="202" fill="currentColor">컴파일: -Waddress-of-packed-member (clang 기본 ON → -Werror 면 빌드 실패)</text>
  <text x="30" y="224" fill="currentColor">실행: -fsanitize=alignment → 콜리 안의 ldrh/strh 위치를 보고</text>
  <text x="30" y="246" fill="currentColor">구멍: packed 구조체 안의 배열 멤버 r-&gt;s 가 포인터로 decay 하면 clang 21 은 경고 없음 (연습 3)</text>
  <text x="10" y="270" fill="currentColor">해결: 정렬된 지역 변수로 복사 → 그 주소를 넘김 → 멤버에 되쓰기</text>
</svg>
```

`demo_addr_packed.c` 의 실제 출력 (헤더를 오프셋 2 에 두어 `len` 이 주소 4k+3):

```text
$ cc -std=c11 -Wall -Wextra -O2 demo_addr_packed.c -o apk && ./apk
demo_addr_packed.c:20:14: warning: taking address of packed member 'len' of class or structure 'wire_hdr' may result in an unaligned pointer value [-Waddress-of-packed-member]
   20 |     u16_add(&w->len, 1);             /* 위험: 정렬 정보가 사라진 uint16_t* 로 전달된다 */
demo_addr_packed.c:22:21: warning: taking address of packed member 'len' of class or structure 'wire_hdr' may result in an unaligned pointer value [-Waddress-of-packed-member]
len=17 (addr % 2 = 1)

$ cc -std=c11 -g -O1 -fsanitize=alignment demo_addr_packed.c -o apk_san && ./apk_san
demo_addr_packed.c:12:90: runtime error: load of misaligned address 0x000102518023 for type 'uint16_t' (aka 'unsigned short'), which requires 2 byte alignment
demo_addr_packed.c:12:74: runtime error: store to misaligned address 0x000102518023 for type 'uint16_t' (aka 'unsigned short'), which requires 2 byte alignment
demo_addr_packed.c:23:51: runtime error: load of misaligned address 0x000102518023 for type 'uint16_t' (aka 'unsigned short'), which requires 2 byte alignment
len=17 (addr % 2 = 1)
```

`w->len = 0x0010;` (19행, 멤버 직접 쓰기)에는 보고가 **없다** — 접근 자체는 안전하다는 증거. 보고는 전부 `u16_add` 안(12행)과 포인터 역참조(23행)에서 나온다. 맥에서는 값(17)이 맞게 나와서 "잘 되는데?" 라고 착각하기 쉽다.

`u16_add` 가 Cortex-M 에서 무엇이 되는지 (`demo_asm_target.c` ⑧, M4·M0 동일):

```text
u16_add:
	ldrh	r2, [r0]
	add	r1, r2
	strh	r1, [r0]
	bx	lr
```

함수는 포인터가 어디서 왔는지 모른다. `ldrh` 는 ARMv6-M 에서 홀수 주소면 HardFault 다. 그래서 정답은:

```c
static void wire_len_add(struct wire_hdr *w, uint16_t d) {
    uint16_t len = w->len;      /* packed 멤버 → 정렬된 지역 변수 */
    u16_add_sat(&len, d);       /* 정렬된 주소만 넘긴다 */
    w->len = len;               /* packed 멤버로 되쓰기 */
}
```

이 레슨의 빌드 명령(`-Werror`)에서 `u16_add_sat(&w->len, d)` 라고 쓰면 실제로 이렇게 멈춘다:

```text
error: taking address of packed member 'len' of class or structure 'wire_hdr' may result in an unaligned pointer value [-Werror,-Waddress-of-packed-member]
```

경고를 `(uint16_t *)(void *)&w->len` 캐스트로 "끄면" 빌드는 되고 테스트도 36/36 이 나오지만, `-fsanitize=undefined` 빌드에서 `load of misaligned address … for type 'uint16_t'` 와 `store to misaligned address` 가 찍힌다 — 직접 확인했다. **캐스트로 경고를 지우는 건 버그를 숨기는 것** 이다.

### Part B'' — packed 레이아웃의 성질

| 식 | 값 | 이유 |
|---|---|---|
| `sizeof(struct wire_hdr)` | 7 | 패딩 0 |
| `alignof(struct wire_hdr)` | 1 | 모든 멤버 정렬 1 |
| `sizeof(struct wire_hdr[3])` | 21 | 꼬리 패딩도 0 → `arr[1].seq` 는 오프셋 10, 비정렬 |
| `struct rec { u8 tag; struct wire_hdr h; u32 crc; }` | `h@1`, `crc@8`, `sizeof 12` | `h` 의 정렬 1 → 앞 패딩 없음. `crc` 는 여전히 자연 정렬(8) |

마지막 줄이 중요하다: packed 는 **선언된 구조체 안에서만** 효과가 있다. 그것을 멤버로 품은 바깥 구조체는 평범한 규칙(C-B12)을 따른다. 반대로 바깥을 packed 로 하면 안쪽 자연 정렬 구조체 멤버도 정렬 1 위치에 놓일 수 있다.

### Part C — 스트림 프레이머

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">DMA 조각 1 의 끝에서 프레임이 잘렸을 때 — consumed 가 이어 붙일 자리</text>
  <rect x="10" y="40" width="60" height="30" class="f-mut" stroke="currentColor"/>
  <text x="40" y="60" fill="currentColor" text-anchor="middle" font-size="12">쓰레기</text>
  <rect x="70" y="40" width="30" height="30" class="f-acc" stroke="currentColor"/>
  <text x="85" y="60" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A5</text>
  <rect x="100" y="40" width="140" height="30" class="f-ok" stroke="currentColor"/>
  <text x="170" y="60" fill="currentColor" text-anchor="middle" font-size="12">hdr 7 (비정렬)</text>
  <rect x="240" y="40" width="100" height="30" class="f-ok" stroke="currentColor"/>
  <text x="290" y="60" fill="currentColor" text-anchor="middle" font-size="12">payload</text>
  <rect x="340" y="40" width="30" height="30" class="f-ok" stroke="currentColor"/>
  <text x="355" y="60" fill="currentColor" text-anchor="middle" font-size="12">chk</text>
  <rect x="370" y="40" width="30" height="30" class="f-acc" stroke="currentColor"/>
  <text x="385" y="60" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">A5</text>
  <rect x="400" y="40" width="140" height="30" class="f-warn" stroke="currentColor"/>
  <text x="470" y="60" fill="currentColor" text-anchor="middle" font-size="12">hdr (끝까지 못 옴)</text>
  <line x1="540" y1="32" x2="540" y2="80" stroke="currentColor" stroke-width="2" stroke-dasharray="4 3"/>
  <text x="548" y="60" fill="currentColor" font-size="12">조각 1 끝 (n)</text>
  <text x="70" y="98" fill="currentColor" font-size="12">frame[0] 기록, i += 9 + len</text>
  <line x1="385" y1="74" x2="385" y2="120" stroke="currentColor" stroke-width="2"/>
  <text x="300" y="138" fill="currentColor" font-size="12">*consumed = 이 sync 위치 → 여기부터 보관</text>
  <rect x="10" y="160" width="20" height="16" class="f-acc" stroke="currentColor"/>
  <text x="38" y="173" fill="currentColor" font-size="12">sync</text>
  <rect x="90" y="160" width="20" height="16" class="f-ok" stroke="currentColor"/>
  <text x="118" y="173" fill="currentColor" font-size="12">검증된 프레임</text>
  <rect x="230" y="160" width="20" height="16" class="f-warn" stroke="currentColor"/>
  <text x="258" y="173" fill="currentColor" font-size="12">잘린 프레임 → break</text>
  <text x="10" y="205" fill="currentColor">가짜 sync(len &gt; 32, chk 불일치)는 i++ 로 1바이트만 건너뛴다 — 진짜 sync 가 그 안에 숨어 있을 수 있다.</text>
  <text x="10" y="228" fill="currentColor">잘림은 i++ 가 아니라 break — 건너뛰면 다음 조각과 합쳐도 그 프레임은 영영 못 찾는다.</text>
</svg>
```

```c
while (i < n && k < max) {
    if (s[i] != SYNC) { i++; continue; }
    hdr_t h;
    if (parse_hdr_memcpy(s, n, i + 1, &h) != 0) break;   /* 헤더 잘림 */
    if (h.len > MAX_PAYLOAD) { i++; continue; }           /* 가짜 sync */
    if (!span_ok(n, i, 9u + h.len)) break;                /* 프레임 잘림 */
    ... XOR 검사 실패 → i++; continue;
    out[k++] = (frame_t){ h, i + 8 };  i += 9u + h.len;
}
*consumed = i;
```

| 상황 (테스트) | 동작 | 결과 |
|---|---|---|
| 프레임 1개 | 정상 | k=1, consumed=n, payload_off=8 |
| 쓰레기 3B + 프레임 2개 | 헤더가 오프셋 4 (짝수지만 len@5·seq@7 비정렬) | k=2, 둘째 payload_off=20 |
| chk 를 뒤집은 프레임 + 정상 프레임 | 첫 프레임 i++ 재동기 → 내부 바이트 중 0xA5 없음 → 둘째 발견 | k=1 |
| `A5 00 C8 00` (len=200) + 정상 프레임 | len 상한으로 가짜 판정, 1B 씩 전진 | k=1, payload_off=12 |
| 둘째 프레임이 3B 모자람 / 헤더조차 5B 만 | break | k=1, consumed=15 (둘째 sync) |
| 프레임 3개, max=2 | 꽉 차면 멈춤 | k=2, consumed=18 |
| 퍼즈 200회, 두 조각 분할 | 조각 1 → `s + c1` 부터 나머지 | 200/200 모든 헤더 복원, `c1 ≤ split`, `c1 + c2 == n` |

**len 상한 검사가 체크섬보다 먼저인 이유**: 쓰레기 헤더의 `len` 이 65535 라면 체크섬을 계산하기 전에 "프레임이 잘렸다" 로 판단해 **영원히 기다리는(stall)** 수신기가 된다. 상한 검사가 그 가짜 sync 를 먼저 버린다. 실제 펌웨어에서는 여기에 프레임 간 타임아웃(UART IDLE 인터럽트)까지 더한다.

**정렬 관점의 덤**: 프레임이 4 정렬 주소에서 시작하면 헤더는 +1, `len` 은 +2, `seq` 는 +4 — sync 1바이트가 필드를 **전부 자연 정렬** 로 만든다 (연습 4). Linux 네트워크 드라이버가 `NET_IP_ALIGN`(보통 2) 바이트를 앞에 비워 14바이트 이더넷 헤더 뒤의 IP 헤더를 4 정렬로 맞추는 것과 같은 발상이다(C-B13). 하지만 퍼즈 테스트처럼 쓰레기가 끼면 그 보장은 사라지므로 파서는 항상 비정렬 안전해야 한다.

### 타깃별 정리 — 무엇이 fault 나는가

`demo_asm_target.c` 의 나머지 함수 실제 출력:

```text
=== Cortex-M4 (thumbv7em) ===          === Cortex-M0 (thumbv6m) ===
set_len_packed:                         set_len_packed:
	strh.w	r1, [r0, #1]                	strb	r1, [r0, #1]
	bx	lr                              	lsrs	r1, r1, #8
                                        	strb	r1, [r0, #2]
ts_packed:   (packed uint64_t @1)       ts_packed:
	ldr.w	r2, [r0, #1]                	ldrb ×8 + lsls/adds … (44 B)
	ldr.w	r1, [r0, #5]
copy_rec:    (자연 정렬 20 B 구조체 복사) copy_rec:
	ldm.w	r1, {r2, r3, r4, r12, lr}    	ldm	r1!, {r2, r3}
	stm.w	r0, {r2, r3, r4, r12, lr}    	stm	r0!, {r2, r3}  …
copy_wrec:   (packed 21 B 구조체 복사)  copy_wrec:
	movs	r2, #21                     	movs	r2, #21
	b	__aeabi_memcpy                  	bl	__aeabi_memcpy
```

| 명령 | ARMv6-M (M0/M0+, M23) | ARMv7-M (M3/M4/M7, M33) 기본 | ARMv7-M + `CCR.UNALIGN_TRP=1` |
|---|---|---|---|
| `LDR/STR`, `LDRH/STRH` 비정렬 | HardFault | **허용** (추가 사이클) | UsageFault |
| `LDRD/STRD`, `LDM/STM`, `PUSH/POP` 비정렬 | HardFault | **UsageFault (항상)** | UsageFault |
| `LDREX/STREX` 계열, `VLDR/VSTR` 비정렬 | (명령 없음) | UsageFault (항상) | UsageFault |
| `LDRB/STRB` | 항상 OK | 항상 OK | 항상 OK |

(UsageFault 를 활성화하지 않았으면 HardFault 로 승격된다.)

`copy_rec` 가 핵심이다. 자연 정렬 구조체 포인터는 "4 정렬" 을 약속하므로 컴파일러가 `LDM` 을 골라도 된다. 그 포인터가 사실 홀수 주소라면 **M4 에서도** UsageFault 다. 즉 "M4 는 비정렬 OK 니까 아무 캐스팅이나 괜찮다" 는 틀렸다 — 어떤 명령을 고를지는 컴파일러 마음이고, 그 자유는 정렬 약속에서 나온다. packed 나 `memcpy` 를 쓰면 컴파일러가 약속 없이 시작하므로 `LDR` 단일 또는 바이트 로드·`__aeabi_memcpy` 같은 안전한 선택만 한다. (이번 clang 21 은 64비트 로드에 `LDRD` 대신 `LDR` 2개를 골랐다. GCC 는 정렬된 `uint64_t` 에 `LDRD` 를 내는 경우가 흔하다고 알려져 있지만 이 맥에서는 확인하지 않았다.)

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 36/36 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san
# 36/36 passed, sanitizer 보고 없음 (alignment 검사 포함 — packed 접근과 memcpy 는 조용하다)
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `(const struct nat_hdr *)(buf + off)` (packed 없음) | 레이아웃이 와이어와 다르고(len@2), 비정렬 포인터 변환은 UB (6.3.2.3p7). 맥에선 쓰레기 값만, M0 에선 HardFault, M4 에서도 `LDM` 이면 fault | packed 구조체 또는 필드별 `memcpy` |
| `u16_add(&w->len, d)` | 정렬 정보가 사라진 `uint16_t*` → 콜리가 `LDRH` | 지역 변수 경유: `t = w->len; f(&t); w->len = t;` |
| 경고를 `(uint16_t *)(void *)&w->len` 로 침묵 | 빌드만 통과, UBSan 은 load/store misaligned 보고 | 위와 같음 |
| `sum_u16(rec->s, 4)` (packed 안의 배열) | 배열 → 포인터 decay 로 비정렬 `uint16_t*`. clang 21 은 **경고도 없다** | `memcpy(tmp, rec->s, sizeof tmp)` 후 `tmp` 사용 |
| `off + len <= n` | `size_t` wrap → 거대한 `off` 가 통과 | `off <= n && len <= n - off` |
| 레지스터 블록 구조체에 packed | M0 등에서 32비트 레지스터가 **바이트 접근 4번** 으로 쪼개짐 → 주변장치가 무시하거나 버스 에러 | 레지스터 구조체는 자연 정렬 + `_Static_assert(offsetof)` (C-B12, C-M01) |
| packed 구조체를 통째로 송수신하면 엔디안도 해결된다고 믿음 | packed 는 패딩만 없앤다. 바이트 순서는 호스트 순서 그대로 | 엔디안 명시 직렬화 (C-M05) |
| "packed 는 느리니까 캐스팅이 낫다" / "memcpy 는 함수 호출이라 느리다" | M4 에서 셋 다 `ldr.w` 1개, M0 에선 packed·memcpy 같은 바이트 로드, 캐스팅은 **틀린** 빠른 코드 | 정확성으로 고르고 asm 으로 확인 |
| 프레임 잘림을 `i++` 로 처리 | 다음 DMA 조각과 합칠 때 그 프레임을 잃는다 | `break` 후 `*consumed = i` |

## 8. 면접 Q&A

<details>
<summary>Q1. packed 구조체는 무엇이고, 표준 C 인가요? 다른 컴파일러에선?</summary>

멤버 정렬을 1 로 낮춰 패딩을 없애는 **컴파일러 확장** 이다. GCC/Clang `__attribute__((packed))`, `#pragma pack(n)` (MSVC 유래, GCC/Clang 도 지원), Arm Compiler 5·IAR 의 `__packed`. CMSIS 는 이를 `__PACKED_STRUCT` 매크로로 감싼다. ISO C 의 `_Alignas` 는 정렬을 **올리기만** 할 수 있고 낮추는 지정은 제약 위반이다 (6.7.5p4). 그래서 이식성 있는 코드는 packed 를 매크로 뒤에 숨기거나 `memcpy`/바이트 조립을 쓴다.

</details>

<details>
<summary>Q2. 바이트 버퍼에서 uint32_t 필드를 꺼내는 가장 안전한 방법은? 성능은요?</summary>

`memcpy(&v, p, 4)` (호스트 순서) 또는 바이트 조립 `p[0] | p[1]<<8 | …` (엔디안 명시, C-M05). 둘 다 정렬·strict aliasing 문제가 없다. 성능: 상수 크기 `memcpy` 는 내장 함수로 바뀌어 Cortex-M4 에서 `ldr.w r0, [r0, #3]` **한 명령**, packed 접근과 바이트 단위로 똑같은 코드가 나온다 (이 레슨 demo 로 확인). M0 에선 둘 다 `LDRB`×4. "캐스팅이 빠르다" 는 틀린 코드를 빠르게 만들 뿐이다.

</details>

<details>
<summary>Q3. packed 멤버에 접근하는 건 안전한데 왜 &pkt->field 는 위험한가요?</summary>

컴파일러는 `pkt->field` 라는 **식** 을 볼 때 그것이 packed 멤버(정렬 1)임을 알아 비정렬 안전 명령을 고른다. `&pkt->field` 의 타입은 그냥 `uint16_t *` 이고 C 의 포인터 타입은 "정렬 2" 라는 가정을 담고 있다. 그 포인터를 받은 함수(다른 번역 단위일 수도)는 `LDRH` 를 내고, ARMv6-M 에서 홀수 주소면 HardFault. clang 은 `-Waddress-of-packed-member`(기본 ON) 로 경고하고, UBSan `-fsanitize=alignment` 는 콜리 안에서 잡는다. 꼬리질문 "경고가 안 나는 경우는?" → packed 구조체 안의 **배열 멤버가 포인터로 decay** 할 때 (clang 21 에서 `-Weverything` 으로도 경고 없음을 확인).

</details>

<details>
<summary>Q4. Cortex-M 에서 비정렬 접근을 하면 정확히 무슨 일이 생기나요?</summary>

ARMv6-M (M0/M0+) 와 ARMv8-M Baseline (M23): 모든 비정렬 접근 → HardFault. ARMv7-M (M3/M4/M7) 과 ARMv8-M Mainline (M33): 단일 `LDR/STR/LDRH/STRH` 는 하드웨어가 여러 버스 전송으로 쪼개 처리(느림), `LDRD/STRD/LDM/STM/PUSH/POP/LDREX/STREX/VLDR/VSTR` 은 항상 UsageFault(UNALIGNED). `SCB->CCR.UNALIGN_TRP=1` 이면 단일 로드/스토어도 트랩 — 디버그 빌드에서 켜서 숨은 비정렬을 찾는 기법이 있다. 그리고 C 수준에서는 잘못 정렬된 포인터가 UB 라서 컴파일러가 `LDM` 을 골라도 된다 (이 레슨의 `copy_rec`).

</details>

<details>
<summary>Q5. 그럼 HardFault 가 났는데 비정렬이 원인인지 어떻게 알죠?</summary>

M3/M4/M7: `SCB->CFSR` 의 UsageFault 상태(UFSR, 상위 16비트)에서 **UNALIGNED** 비트(CFSR bit 24)가 서 있으면 비정렬. HardFault 로 승격됐으면 `HFSR.FORCED` 가 1. 스택된 PC(예외 프레임 R0,R1,R2,R3,R12,LR,**PC**,xPSR 의 7번째 워드, SP+0x18)로 fault 명령을 찾고 disassembly 에서 `ldrd/ldm/ldrh` 와 레지스터 값을 본다. M0 는 fault 상태 레지스터가 없어서 스택된 PC 와 명령 디코딩이 유일한 단서다. 예방: `-Wcast-align`, `-Waddress-of-packed-member` 를 `-Werror` 로, 호스트 테스트에서 UBSan.

</details>

<details>
<summary>Q6. 레지스터 블록 구조체에 packed 를 붙이면 왜 안 되나요?</summary>

① 레지스터 맵은 원래 자연 정렬이라 packed 가 필요 없다 (reserved 로 빈칸을 채우면 됨, C-B12). ② packed 구조체는 정렬 1 이므로 컴파일러가 "비정렬일 수 있다" 고 보고, 비정렬을 못 하는 타깃(M0, `-mno-unaligned-access`)에서는 32비트 레지스터 쓰기를 **`STRB` 4번** 으로 쪼갠다 (이 레슨의 `set_len_packed` 가 M0 에서 `strb` 2개가 된 것과 같은 현상). 많은 주변장치는 32비트 접근만 받아들이거나, 바이트 쓰기 각각을 별개의 쓰기로 처리해 FIFO 에 쓰레기를 넣는다. `volatile` 이 붙어 있어도 접근 **폭** 은 보장하지 않는다.

</details>

<details>
<summary>Q7. packed 와 aligned 를 같이 쓰는 건 무슨 뜻인가요?</summary>

`struct __attribute__((packed, aligned(4))) p2 { u8 a; u32 b; };` → 멤버 사이 패딩은 없고(`b@1`), 구조체 시작 주소와 `sizeof` 는 4 의 배수(`sizeof 8`, 꼬리 패딩 3). DMA 디스크립터 배열처럼 "내부는 촘촘하게, 각 원소 시작은 정렬" 이 필요할 때 쓴다. 반대로 `#pragma pack(2)` 는 정렬 상한을 2 로 — `{u8; u32; u8;}` 이 `b@2, c@6, sizeof 8`. 둘 다 연습 1 에서 컴파일러로 확인한다.

</details>

<details>
<summary>Q8. Linux 커널이나 CMSIS 는 비정렬 읽기를 어떻게 구현하나요?</summary>

Linux `get_unaligned()/put_unaligned()` 는 `const struct { type x; } __packed *` 로 캐스팅해 `->x` 를 읽는 **packed 래퍼 구조체** 트릭이다 (`<linux/unaligned.h>`, 예전 `asm-generic/unaligned.h`). 컴파일러에게 "이 한 번의 접근은 정렬 1" 이라고 알려주는 가장 좁은 방법이다. CMSIS-Core 의 `__UNALIGNED_UINT32_READ(addr)` 도 같은 방식(`T_UINT32_READ` packed 구조체). 엔디안이 붙은 `get_unaligned_le32()` 는 거기에 변환을 더한다. 연습 2 에서 직접 만든다.

</details>

## 9. 추가 연습

### 연습 1 — packed 변형들의 레이아웃

다음 네 구조체의 `sizeof`, `alignof`, 멤버 오프셋은? (1) `packed {u8 a; u32 b;}` (2) `packed, aligned(4) {u8 a; u32 b;}` (3) `#pragma pack(push, 2)` 안의 `{u8 a; u32 b; u8 c;}` (4) `{u8 a; u32 b __attribute__((packed)); u8 c;}` (멤버 하나만 packed).

<details>
<summary>답 보기</summary>

(1) 5, 1. (2) `b@1`, `sizeof 8`, `alignof 4` — 내부는 촘촘, 바깥은 정렬. (3) 정렬 상한 2: `b@2`, `c@6`, `sizeof 8`, `alignof 2`. (4) `b` 만 정렬 1 이 되어 `b@1`, `c@5`, 모든 멤버 정렬 1 이라 구조체 정렬 1, `sizeof 6`. 전부 clang 21 (arm64) 과 `thumbv7em` 에서 같다 — 고정 폭 타입만 썼기 때문.

</details>

### 연습 2 — get_unaligned_u16/u32/u64, put_unaligned_u32

packed 래퍼 구조체로 Linux 스타일 헬퍼를 만들고, 무작위 오프셋 500회를 `memcpy` 참조와 대조하라.

<details>
<summary>답 보기</summary>

```c
struct __attribute__((packed)) una_u32 { uint32_t x; };
static uint32_t get_unaligned_u32(const void *p) { return ((const struct una_u32 *)p)->x; }
static void put_unaligned_u32(void *p, uint32_t v) { ((struct una_u32 *)p)->x = v; }
```

`void *` 를 받는 이유: 호출자가 `uint8_t *` 든 무엇이든 캐스트 없이 넘길 수 있게. Part A 와 같은 이유로 엄밀히는 effective type 문제가 남는다 (커널은 `-fno-strict-aliasing`). 완전히 표준적인 버전이 필요하면 본문을 `memcpy` 로 바꾸면 되고, 생성 코드는 같다.

</details>

### 연습 3 — packed 구조체 안의 배열 멤버

`struct __attribute__((packed)) log_rec { u8 id; u16 s[4]; }` (`s@1`) 의 합을 주어진 `sum_u16(const uint16_t *p, size_t n)` 으로 구하라. `sum_u16(r->s, 4)` 는 왜 안 되고, 컴파일러는 경고하나?

<details>
<summary>답 보기</summary>

`r->s` 는 배열이라 `&r->s[0]` 으로 decay → 비정렬 `const uint16_t *`. **clang 21 은 `-Wall -Wextra` 는 물론 `-Weverything` 에서도 경고하지 않았다** (`&r->s[1]` 도 마찬가지). UBSan 은 `sum_u16` 안에서 `load of misaligned address … for type 'const uint16_t'` 로 잡는다. 정답: `uint16_t tmp[4]; memcpy(tmp, r->s, sizeof tmp); return sum_u16(tmp, 4);`. 원소 하나를 읽는 `r->s[3]` 은 멤버 접근이라 안전하다.

</details>

### 연습 4 — 어디에 두면 모든 필드가 자연 정렬인가

헤더(1+2+4)의 시작 주소 `addr` 에 대해 비정렬인 다바이트 필드 수를 돌려주는 `misaligned_fields(addr)` 를 쓰고, `addr % 4` 가 0, 1, 2, 3 일 때 값을 구하라.

<details>
<summary>답 보기</summary>

`len` 주소 `addr+1` 은 2 의 배수, `seq` 주소 `addr+3` 은 4 의 배수여야 한다. `addr%4`: 0 → 2개, **1 → 0개**, 2 → 2개, 3 → 1개(`len` 만 정렬). 즉 4 정렬 버퍼의 첫 바이트를 sync 로 쓰면 헤더 전체가 자연 정렬 — Part C 프레임 형식이 우연히도 이 성질을 가진다. 프로토콜을 **설계** 할 수 있다면 필드를 정렬 내림차순으로 두거나 이런 식으로 정렬을 맞춰 packed 자체를 불필요하게 만드는 것이 최선이다.

</details>

### 연습 5 — packed 구조체는 memcmp 해도 되나

`hdr_t` 를 `struct wire_hdr` 로 옮기는 `hdr_to_wire()` 를 쓰고, 미리 두 구조체를 `0xAA` 로 채운 뒤 결과를 와이어 바이트와 `memcmp` 하라. `hdr_t` 끼리는 왜 `memcmp` 하면 안 되나?

<details>
<summary>답 보기</summary>

packed 구조체는 7바이트 전부가 필드라 필드를 다 쓰면 객체 표현이 완전히 결정된다 → `memcmp`/CRC 가 안정적. `hdr_t` 는 `type` 뒤 1바이트 패딩이 있고 그 값은 unspecified (6.2.6.1p6, C-B12 Q3) — 필드가 같아도 `memcmp` 가 다를 수 있고, 통째로 송신하면 스택 쓰레기가 샌다. 단 packed 도 **엔디안은 호스트 순서** 이므로 와이어로 보낼 때는 C-M05 의 명시적 직렬화가 정석이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 15/15 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o pr_san && ./pr_san   # 15/15, 보고 없음
```

## 10. 핵심 정리

- 와이어 포맷 ≠ 자연 정렬 구조체. 평범한 구조체 캐스팅은 **레이아웃도 틀리고** 비정렬 포인터 변환 자체가 UB (6.3.2.3p7).
- `__attribute__((packed))` = 멤버·구조체 정렬 1, 패딩 0. 표준이 아닌 확장이고, strict aliasing(6.5p7)은 해결하지 않는다.
- `memcpy(&local, p + off, sizeof local)` 는 정렬·aliasing 모두 안전하고, M4 에서 `ldr.w` 1개로 packed 와 **같은 코드** 가 된다.
- packed 멤버 **접근** 은 안전, **주소**(`&w->len`, 배열 decay `r->s`)는 정렬 정보를 잃는다 → 지역 변수 경유.
- `-Waddress-of-packed-member`(clang 기본 ON), `-Wcast-align`, 호스트 테스트의 `-fsanitize=alignment` 로 잡는다. 배열 decay 는 경고가 없다.
- ARMv6-M: 비정렬 = HardFault. ARMv7-M: 단일 `LDR/LDRH` 만 허용, `LDRD/LDM/STM/LDREX` 는 항상 fault, `CCR.UNALIGN_TRP` 로 전부 트랩 가능.
- 경계 검사는 `off <= n && len <= n - off` (wrap 안전). 스트림 파서는 가짜 sync 면 1바이트 전진, 잘림이면 멈추고 sync 위치를 돌려준다.
- 레지스터 블록에 packed 금지(바이트 접근으로 쪼개짐). 엔디안은 packed 와 무관 → C-M05.

## 참고자료

- C11 표준 초안 N1570 — 6.2.6.1 (객체 표현·패딩), 6.3.2.3p7 (포인터 변환과 정렬), 6.5p7 (effective type / aliasing), 6.7.5 (`_Alignas`), 7.24.2.1 (`memcpy`): <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- [GCC — Common Type Attributes: `packed`, `aligned`](https://gcc.gnu.org/onlinedocs/gcc/Common-Type-Attributes.html) · [Structure-Layout Pragmas (`#pragma pack`)](https://gcc.gnu.org/onlinedocs/gcc/Structure-Layout-Pragmas.html)
- [Clang — Diagnostic flags: `-Waddress-of-packed-member`, `-Wcast-align`](https://clang.llvm.org/docs/DiagnosticsReference.html)
- [Clang — UndefinedBehaviorSanitizer (`-fsanitize=alignment`)](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)
- [Linux kernel — Unaligned Memory Accesses](https://docs.kernel.org/core-api/unaligned-memory-access.html)
- ARMv7-M Architecture Reference Manual (DDI 0403) — A3.2 Alignment support, SCB 의 CCR(UNALIGN_TRP)·CFSR(UFSR) 레지스터 설명: <https://developer.arm.com/documentation/ddi0403/latest/>
- ARMv6-M Architecture Reference Manual (DDI 0419) — A3.2 Alignment support (비정렬 = HardFault)
- [Arm CMSIS-Core — Compiler control 매크로 (`__PACKED_STRUCT`, `__UNALIGNED_UINT32_READ`)](https://arm-software.github.io/CMSIS_6/latest/Core/index.html)
- 관련 레슨: C-B12 (패딩·정렬), C-B13 (엔디안·비정렬 데모), C-M01 (레지스터 구조체), C-M05 (엔디안 독립 직렬화), C-M08 (memcpy 구현), C-M13 (strict aliasing)
