---
id: CPP-B11
time: 50분
updated: 2026-11-07
---
# CPP-B11 std::span으로 버퍼+길이 인터페이스 정리

## 학습 목표

- `uart_write(const uint8_t*, size_t)` 같은 **(포인터, 길이) 쌍 API**를 `std::span<const std::byte>` 하나로 바꾸고, `std::array`·C 배열·`uint8_t` 버퍼·구조체를 각각 어떻게 넘기는지(암시적 변환 / `std::as_bytes` / `std::as_writable_bytes`) 안다.
- `first` / `last` / `subspan` 으로 프레임 헤더·페이로드·CRC를 **복사 없이** 잘라내되, 이 함수들이 **범위를 검사하지 않는다(위반 = UB)** 는 사실을 알고 모든 자르기 앞에 길이 검사를 둔다.
- **정적 extent** `std::span<T, N>` 은 포인터 1개, **동적 extent** `std::span<T>` 는 포인터+길이 2개 크기임을 측정하고, 어셈블리가 C 버전과 같음을 확인한다.
- span은 **소유하지 않는 뷰(non-owning view)** 다. 지역 버퍼·임시 객체·재사용되는 조립 버퍼에서 생기는 **dangling span** 을 설명하고 피한다.
- 원형(circular) RX DMA 링 → 두 조각 span → 프레임 조립 → 콜백이라는 실제 UART 수신 경로를 span으로 짠다.

## 1. 문제

펌웨어 버그의 단골은 "버퍼와 길이가 따로 논다" 이다 — `memcpy(dst, src, sizeof(src))` 에서 `src` 가 포인터라 4/8바이트만 복사, 길이 필드를 믿고 버퍼 끝을 넘어 읽기(Heartbleed 류). 면접에서는 "이 C API를 Modern C++로 안전하게 바꿔보라" 또는 "패킷 파서를 짜라(경계 검사 포함)" 로 나온다.

C에서는 `(const uint8_t *buf, size_t len)` 두 인자를 **사람이** 맞춰서 넘긴다. C++20 `std::span<T>` 은 이 두 개를 **한 값**으로 묶은 것이다. 길이는 배열에서 자동으로 따라오고, 잘라낸 조각(`subspan`)도 자기 길이를 들고 다닌다. 비용은 0 — 내부는 정말로 포인터+길이 두 워드다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">span = (포인터, 길이)를 한 값으로 — 소유하지 않는 "창문"</text>
  <text x="10" y="56" fill="currentColor">버퍼 (std::array&lt;std::byte, 12&gt; rx)</text>
  <g stroke="currentColor" fill="none">
    <rect x="250" y="40" width="480" height="28"/>
  </g>
  <rect x="250" y="40" width="40" height="28" class="f-acc"/>
  <rect x="290" y="40" width="80" height="28" class="f-mut"/>
  <rect x="370" y="40" width="240" height="28" class="f-ok"/>
  <rect x="610" y="40" width="40" height="28" class="f-warn"/>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="270" y="59">A5</text><text x="330" y="59">LEN CMD</text><text x="490" y="59">payload (LEN 바이트)</text><text x="630" y="59">CRC</text><text x="690" y="59">다음..</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="10" y="104">span&lt;const byte&gt;      buf  = rx;          → { data=&amp;rx[0], size=12 }  (16B)</text>
    <text x="10" y="128">span&lt;const byte, 3&gt;   hdr  = buf.first&lt;3&gt;();  → { data=&amp;rx[0] }         (8B)</text>
    <text x="10" y="152">span&lt;const byte&gt;      pl   = frame.subspan(3, len)  → { &amp;rx[3], len }</text>
    <text x="10" y="176">span&lt;const byte, 1&gt;   crc  = frame.last&lt;1&gt;();</text>
  </g>
  <text x="10" y="212" fill="currentColor">어떤 span도 복사하지 않는다 — 전부 rx 안을 가리킨다. rx가 죽으면 전부 dangling.</text>
  <text x="10" y="236" fill="currentColor">정적 extent는 길이가 타입에 있어 포인터 하나 크기 (arm64 8B, Cortex-M 4B).</text>
</svg>
```

### Part A — (ptr, len) UART API를 span으로

가짜 UART(`UartRegs{SR, DR}`)와 `uart_putc` / `uart_getc` 가 주어진다.

- `size_t uart_write(std::span<const std::byte> data)` — 모든 바이트를 `uart_putc` 로 보내고 개수 반환.
- `size_t uart_write_legacy(const uint8_t* p, size_t n)` — 옛 C 호출자용 **얇은 shim**: `(p, n)` → span → `std::as_bytes` → `uart_write`. 구현은 한 줄.
- `size_t uart_read(std::span<std::byte> dst)` — `dst` 가 찰 때까지 또는 FIFO가 빌 때까지 읽는다. 하네스는 `std::as_writable_bytes(std::span{&cfg, 1})` 로 **구조체에 직접** 받아본다.
- `kStaticSpanWords`, `kDynamicSpanWords` — `sizeof(std::span<const std::byte, 8>)` 와 `sizeof(std::span<const std::byte>)` 가 포인터 몇 개 크기인지 **예측**해서 적는다.

하네스는 `std::array<std::byte,4>`, C 배열 `std::byte[2]`, `std::array<uint8_t,3>`, 문자열 리터럴 `"AT\r\n"` 을 모두 같은 `uart_write` 로 보낸다 — 각각 어떤 변환이 필요한지 보라.

### Part B — 프레임 인코드/파싱: first / last / subspan + 명시적 길이 검사

프레임 형식: `[SOF=0xA5][LEN][CMD][payload LEN바이트][CRC8]`, `LEN ≤ 16`, CRC-8/SMBUS(다항식 0x07, 초기값 0)는 SOF부터 payload 끝까지. `crc8()` 은 주어진다.

- `size_t encode_frame(std::span<std::byte> out, std::byte cmd, std::span<const std::byte> payload)` — 자리가 모자라거나 payload가 너무 길면 0.
- `ParseResult parse_frame(std::span<const std::byte> buf)` — 결과는 `Ok / NeedMore / BadSof / BadLen / BadCrc` 와 `consumed`(Ok: 프레임 길이, Bad*: 1 = 한 바이트 버리고 재동기화, NeedMore: 0). **payload는 복사하지 말고 `buf` 안을 가리키는 span으로** 돌려준다.
- 엣지 케이스: 빈 버퍼, 헤더 미완성, CRC 미도착, `LEN=200` 거짓말, `LEN=15` 인데 5바이트뿐, 앞에 쓰레기, CRC 1비트 오류, 두 프레임이 붙어 온 경우.

### Part C — 가짜 circular RX DMA 링 위의 패킷 파서

STM32 UART RX를 circular DMA로 받는 구조를 흉내 낸다. DMA가 32바이트 링 `g_dma_ring` 에 쓰고, 레지스터 `NDTR`(남은 전송 수)이 줄어들다 0이 되면 32로 자동 reload. `sim_dma_rx()` 가 하드웨어 역할, `dma_write_index()` 가 `NDTR` 에서 쓰기 위치를 계산한다(주어짐).

- `RxChunks ring_readable(ring, rd, wr)` — `[rd, wr)` 를 **최대 두 개의 연속 span** 으로. 링 끝을 넘으면 `[rd, end)` + `[0, wr)`. 인덱스가 범위 밖이면 빈 결과(UB 금지).
- `void rx_append(RxParser&, chunk)` — 48바이트 선형 조립 버퍼 `acc` 뒤에 붙인다. 자리가 없으면 `overflows++`, 쌓인 것 버리고 chunk의 마지막 부분만 보관.
- `size_t rx_poll(RxParser&, FrameHandler)` — 새 데이터 수거 → `parse_frame` 반복 → Ok마다 `on_frame(cmd, payload)` 호출 → 처리한 앞부분 버리기. 콜백의 payload span은 **그 호출 동안만** 유효하다.

## 2. Problem (English)

> **Replace pointer+length buffer APIs with `std::span` and write a bounds-safe frame parser on a circular DMA buffer.**
>
> Build with `-std=c++20 -fno-exceptions -fno-rtti`, no heap.
>
> 1. Convert `size_t uart_write(const uint8_t* p, size_t n)` to `size_t uart_write(std::span<const std::byte>)`, keep a one-line legacy shim, and add `size_t uart_read(std::span<std::byte> dst)`. Callers must be able to pass `std::array<std::byte,N>`, `std::byte[N]`, `std::array<uint8_t,N>`, a string literal (without its NUL), and a trivially copyable struct. State `sizeof` of a static-extent and a dynamic-extent span.
> 2. Frame: `[0xA5][LEN][CMD][LEN payload bytes][CRC-8/SMBUS over SOF..payload]`, `LEN <= 16`. Implement `encode_frame(std::span<std::byte> out, std::byte cmd, std::span<const std::byte> payload) -> size_t` and `parse_frame(std::span<const std::byte>) -> {status, {cmd, payload_view}, consumed}`. The payload must be a view into the input (zero-copy). `subspan`/`first`/`last` are not bounds-checked, so every slice must be preceded by an explicit length check. A hostile `LEN` must never cause an out-of-bounds read.
> 3. A DMA engine writes into a 32-byte circular buffer and exposes a down-counting `NDTR`. Implement `ring_readable(ring, rd, wr)` returning up to two contiguous spans, and `rx_poll()` that assembles frames across DMA bursts and ring wrap-around, resynchronises on garbage/CRC errors, and delivers each frame to a callback.
>
> Example: `encode_frame(out, std::byte{0x10}, {1,2,3})` writes `A5 03 10 01 02 03 D3` and returns 7.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨져 있을 때
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 FAIL 이 쭉 나오고 "10/55 passed" — TODO 를 채워 55/55 를 만든다
```

파일 위쪽의 `static_assert` 묶음은 **이미 참인 사실**이다(starter에서도 컴파일된다). 읽고 왜 참인지 설명할 수 있어야 한다 — 5. 풀이 표 참고.

## 4. 힌트

<details>
<summary>힌트 1 — uint8_t 버퍼는 왜 바로 안 넘어가나</summary>

`std::span<const std::byte>` 는 `std::array<std::byte, N>` 과 C 배열 `std::byte[N]` 에서만 암시적으로 만들어진다. 원소 타입 변환은 `T(*)[] → const T(*)[]` 같은 **const 추가**만 허용된다([span.cons]). `uint8_t → std::byte` 는 다른 타입이라 `std::as_bytes(std::span{u8})` 로 명시적으로 바꾼다. 포인터+길이는 `std::span{p, n}` (CTAD → `span<const uint8_t>`).

</details>

<details>
<summary>힌트 2 — 문자열 리터럴과 구조체</summary>

`"AT\r\n"` 은 `const char[5]` — 끝의 NUL까지 5바이트다. `std::as_bytes(std::span{"AT\r\n"}).first<4>()` 로 NUL을 뺀다. 구조체는 `std::span{&cfg, 1}` (원소 1개짜리 span) → `std::as_writable_bytes` → `span<std::byte>` 길이 `sizeof(Cfg)`.

</details>

<details>
<summary>힌트 3 — parse_frame 의 검사 순서</summary>

`empty?` → `buf[0] == SOF?` → `size >= 3?` → `LEN <= 16?` → `size >= 3+LEN+1?` → CRC. 이 순서의 모든 단계는 **다음 줄의 자르기가 합법이 되도록** 존재한다. `first<3>()` 는 `size >= 3` 이후에만, `first(total)` 은 `size >= total` 이후에만.

</details>

<details>
<summary>힌트 4 — 링에서 읽을 부분</summary>

`wr >= rd` 면 한 조각 `ring.subspan(rd, wr - rd)` (rd == wr 이면 길이 0). 아니면 링 끝을 넘은 것: `ring.subspan(rd)` + `ring.first(wr)`. span은 **연속 메모리만** 표현하므로 랩어라운드 데이터는 반드시 두 개가 된다.

</details>

<details>
<summary>힌트 5 — rx_poll 의 루프</summary>

`acc = std::span{p.acc}.first(p.acc_len)`, `off = 0` 에서 `parse_frame(acc.subspan(off))` 를 반복. NeedMore면 멈추고, 아니면 `off += consumed`. 끝나고 `memmove` 로 `[off, acc_len)` 를 앞으로 당긴다. 콜백 호출은 반드시 이 memmove **전에** 끝나야 한다.

</details>

## 5. 풀이

### Part A — span 하나로 모든 버퍼를 받는다

```cpp
static size_t uart_write(std::span<const std::byte> data) {
    for (std::byte b : data) uart_putc(b);
    return data.size();
}
static size_t uart_write_legacy(const uint8_t* p, size_t n) {
    return uart_write(std::as_bytes(std::span{p, n}));
}
```

호출자별로 무엇이 일어나는지 정리하면:

| 호출자가 가진 것 | 넘기는 법 | 왜 |
|---|---|---|
| `std::array<std::byte, N>` | `uart_write(a)` | [span.cons] `span(array<U,N>&)` 암시적. 길이 N 자동 |
| `std::byte buf[N]` (C 배열) | `uart_write(buf)` | `span(element_type (&)[N])` 암시적. **decay 없이** 길이 유지 |
| `std::array<uint8_t, N>` | `uart_write(std::as_bytes(std::span{a}))` | 원소 타입 변환은 const 추가만 허용 → byte로는 명시적으로 |
| `const uint8_t* p, size_t n` | `std::as_bytes(std::span{p, n})` | 포인터는 길이를 모른다 — 경계에서 **한 번만** 묶는다 |
| 문자열 리터럴 `"AT\r\n"` | `std::as_bytes(std::span{"AT\r\n"}).first<4>()` | `const char[5]` → NUL 포함 5바이트 |
| 구조체 `Cfg cfg` (받기) | `std::as_writable_bytes(std::span{&cfg, 1})` | 객체 표현을 `std::byte` 로 보는 건 aliasing 규칙상 합법 |
| `const std::array<std::byte,N>` → `span<std::byte>` | **컴파일 에러** | const 버퍼에 쓰기 금지 |

`std::byte` (C++17, `<cstddef>`)는 "숫자가 아니라 메모리 한 바이트" 라는 뜻의 enum class다. 비트 연산(`& | ^ ~ << >>`)만 되고 `+` 는 안 된다. 값을 꺼낼 땐 `std::to_integer<uint8_t>(b)`, 넣을 땐 `static_cast<std::byte>(x)`. 그래서 `uart_write(std::span<const std::byte>)` 는 "이건 바이트 스트림이다"를 타입으로 말한다. `uint8_t` 를 쓰는 API도 흔하다 — 팀 규칙을 정하면 되고, `std::byte` 를 고르면 위 표처럼 경계에서 `as_bytes` 한 번이 필요하다.

**aliasing**: `as_bytes` / `as_writable_bytes` 는 내부에서 `reinterpret_cast<const std::byte*>` 를 한다. [basic.lval]/11 은 어떤 객체든 `char`, `unsigned char`, `std::byte` glvalue로 접근하는 것을 허용하므로 UB가 아니다(C-M13 참고). 단 `as_writable_bytes` 로 채워도 되는 건 **trivially copyable** 타입뿐이고, 패딩·엔디안은 여전히 내 책임이다(하네스는 `static_assert(std::endian::native == std::endian::little)` 로 가정을 박아 둠, 이식성 있는 직렬화는 C-M05).

**미묘한 점**: `std::span{&cfg, 1}` 은 (포인터, 개수) CTAD라 **동적 extent** 다. 정적 extent가 필요하면 `std::span<Cfg, 1>{&cfg, 1}` 처럼 명시한다(이 생성자는 정적 extent일 때 `explicit`). 그러면 `as_writable_bytes` 결과가 `span<std::byte, sizeof(Cfg)>` 가 된다(추가 연습 5).

#### sizeof(span) — 직접 재 보기

| 타입 | 저장하는 것 | arm64 (실측) | Cortex-M (32비트, 예상) |
|---|---|---|---|
| `std::span<const std::byte, 8>` | 포인터 | 8 | 4 |
| `std::span<const std::byte>` | 포인터 + `size_t` | 16 | 8 |
| `(const uint8_t*, size_t)` C 인자 | 포인터 + `size_t` | 16 (레지스터 2개) | 8 (r0, r1) |

표준([span.overview])은 멤버를 "exposition only" 로만 적고 크기를 강제하지 않지만, libc++ / libstdc++ / MSVC STL 모두 정적 extent에선 길이를 저장하지 않는다. 그리고 span은 **trivially copyable** 이라(C++23 P2251이 요구사항으로 명문화, 주요 구현은 C++20 때부터 이미 그랬음) 값으로 넘기면 C의 두 인자와 똑같이 레지스터 두 개로 간다. **`const std::span<T>&` 로 받지 말고 값으로 받는다** — 참조로 받으면 오히려 메모리 간접 접근이 하나 생긴다.

#### 어셈블리: C 버전과 같은가

`demo_asm.cpp` 를 `c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -S -o - demo_asm.cpp` 로 본 실제 출력(arm64, 지시어 줄 제거):

```text
_write_c:                                   __Z10write_spanNSt3__14spanIKSt4byteLm18446744073709551615EEE:
	cbz	x1, LBB0_3                          	cbz	x1, LBB1_3
	adrp	x8, _UART_DR@GOTPAGE                	adrp	x8, _UART_DR@GOTPAGE
	ldr	x8, [x8, _UART_DR@GOTPAGEOFF]       	ldr	x8, [x8, _UART_DR@GOTPAGEOFF]
LBB0_2:                                     LBB1_2:
	ldrb	w9, [x0], #1                       	ldrb	w9, [x0], #1
	str	w9, [x8]                            	str	w9, [x8]
	subs	x1, x1, #1                          	subs	x1, x1, #1
	b.ne	LBB0_2                              	b.ne	LBB1_2
LBB0_3:                                     LBB1_3:
	ret                                         	ret

__Z9write_hdrNSt3__14spanIKSt4byteLm3EEE:   ; std::span<const std::byte, 3>
	adrp	x8, _UART_DR@GOTPAGE
	ldr	x8, [x8, _UART_DR@GOTPAGEOFF]
	ldrb	w9, [x0]
	str	w9, [x8]
	ldrb	w9, [x0, #1]
	str	w9, [x8]
	ldrb	w9, [x0, #2]
	str	w9, [x8]
	ret
```

- `(ptr, len)` 와 `span` 은 **명령 하나까지 동일**: `x0` = data, `x1` = size. 비용 0.
- 정적 extent 3짜리는 길이 인자가 아예 없고(`x0` 만), 루프가 펼쳐져 `cbz`(빈 버퍼 검사)도 사라졌다. 길이가 타입에 있으니 컴파일러가 안다.
- arm-none-eabi-g++ (Cortex-M, Thumb-2) 예상(실행해 보지 않음): AAPCS32에서 8바이트 trivially copyable 구조체는 `r0, r1` 로 전달되므로 동적 span 인자도 C 두 인자와 같은 코드가 나와야 한다. 단 **반환값**은 다르다 — AAPCS32는 4바이트 넘는 복합 타입을 숨은 포인터로 메모리에 반환하므로, `span` 을 *반환*하는 함수는 `(ptr,len)` 을 out 파라미터로 주는 것과 비슷한 비용이 든다. 정적 extent span(4바이트)은 `r0` 으로 반환된다.

### Part B — 자르기 전에 반드시 잰다

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">parse_frame: 모든 자르기 앞에 길이 검사 (검사 → 그 다음 줄이 합법)</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="40" width="200" height="30" rx="6"/>
    <rect x="20" y="88" width="200" height="30" rx="6"/>
    <rect x="20" y="136" width="200" height="30" rx="6"/>
    <rect x="20" y="184" width="200" height="30" rx="6"/>
    <rect x="20" y="232" width="200" height="30" rx="6"/>
    <rect x="20" y="280" width="200" height="30" rx="6"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="30" y="60">buf.empty() ?</text>
    <text x="30" y="108">buf[0] != 0xA5 ?</text>
    <text x="30" y="156">size &lt; 3 ?</text>
    <text x="30" y="204">LEN &gt; 16 ?</text>
    <text x="30" y="252">size &lt; 3+LEN+1 ?</text>
    <text x="30" y="300">crc8(first(n-1)) != CRC ?</text>
  </g>
  <g stroke="currentColor">
    <line x1="120" y1="70" x2="120" y2="88"/><line x1="120" y1="118" x2="120" y2="136"/>
    <line x1="120" y1="166" x2="120" y2="184"/><line x1="120" y1="214" x2="120" y2="232"/>
    <line x1="120" y1="262" x2="120" y2="280"/>
    <line x1="220" y1="55" x2="250" y2="55"/><line x1="220" y1="103" x2="250" y2="103"/>
    <line x1="220" y1="151" x2="250" y2="151"/><line x1="220" y1="199" x2="250" y2="199"/>
    <line x1="220" y1="247" x2="250" y2="247"/><line x1="220" y1="295" x2="250" y2="295"/>
  </g>
  <rect x="252" y="42" width="120" height="26" class="f-mut"/><text x="260" y="60" fill="currentColor">NeedMore, 0</text>
  <rect x="252" y="90" width="120" height="26" class="f-warn"/><text x="260" y="108" fill="currentColor">BadSof, 1</text>
  <rect x="252" y="138" width="120" height="26" class="f-mut"/><text x="260" y="156" fill="currentColor">NeedMore, 0</text>
  <rect x="252" y="186" width="120" height="26" class="f-warn"/><text x="260" y="204" fill="currentColor">BadLen, 1</text>
  <rect x="252" y="234" width="120" height="26" class="f-mut"/><text x="260" y="252" fill="currentColor">NeedMore, 0</text>
  <rect x="252" y="282" width="120" height="26" class="f-warn"/><text x="260" y="300" fill="currentColor">BadCrc, 1</text>
  <g fill="currentColor" font-size="12">
    <text x="390" y="108">↳ 이제 buf[0] 읽기 OK (empty 아님)</text>
    <text x="390" y="156">↳ 이제 buf.first&lt;3&gt;() OK → hdr</text>
    <text x="390" y="204">↳ 악의적 LEN 차단 (조립 버퍼 크기 상한)</text>
    <text x="390" y="252">↳ 이제 first(total), subspan(3,LEN), last&lt;1&gt;() OK</text>
    <text x="390" y="300">↳ 통과하면 Ok, {hdr[2], subspan(3, LEN)}, total</text>
  </g>
  <text x="390" y="60" fill="currentColor" font-size="12">↳ 빈 버퍼: 아직 아무것도 안 왔다</text>
</svg>
```

핵심 코드:

```cpp
if (buf.size() < kHdrLen) return {ParseStatus::NeedMore, {}, 0};
const std::span<const std::byte, kHdrLen> hdr = buf.first<kHdrLen>();   // 정적 extent 3
const size_t len = std::to_integer<size_t>(hdr[1]);
if (len > kMaxPayload) return {ParseStatus::BadLen, {}, 1};
const size_t total = kHdrLen + len + kCrcLen;
if (buf.size() < total) return {ParseStatus::NeedMore, {}, 0};
const std::span<const std::byte> frame = buf.first(total);
const std::byte crc_rx = frame.last<kCrcLen>()[0];
```

#### subspan 은 범위를 검사하지 않는다

[span.sub]의 `first(count)`, `last(count)`, `subspan(offset, count)` 는 *전제조건(Preconditions)* 으로 `count <= size()`, `offset <= size() && (count == dynamic_extent || count <= size() - offset)` 를 요구한다. 전제조건 위반은 **UB** 다([res.on.required]). 예외도, 클램핑도, 에러 코드도 없다. `std::vector::at()` 같은 검사 버전은 `std::span` 에 C++26에야 `at()` 이 추가됐고(P2821), 그것도 예외를 던지므로 `-fno-exceptions` 펌웨어에선 못 쓴다.

`demo_hardening.cpp` 는 8바이트 버퍼를 `subspan(3, LEN)` 으로 검사 없이 자른다. 실제 실행 결과:

```text
$ c++ -std=c++20 -O2 -fno-exceptions -fno-rtti demo_hardening.cpp -o h0 && ./h0 9
buf.size()=8  LEN=9  payload.size()=9            ← 조용히 "9바이트짜리" span 탄생 (8바이트 버퍼인데!)

$ c++ ... -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST demo_hardening.cpp -o h1
$ ./h1 4   →  buf.size()=8  LEN=4  payload.size()=4
$ ./h1 9   →  Trace/BPT trap: 5   (exit 133, 메시지 없이 __builtin_trap)

$ c++ ... -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG demo_hardening.cpp -o h2 && ./h2 9
.../c++/v1/span:516: libc++ Hardening assertion __count <= size() - __offset failed:
    span<T>::subspan(offset, count): offset + count out of range
Abort trap: 6   (exit 134)
```

- 기본 빌드: 아무 일 없다. 이 span으로 CRC를 계산하면 버퍼 밖 메모리를 읽는다 — Heartbleed와 같은 모양.
- libc++ hardening(`_LIBCPP_HARDENING_MODE`, LLVM 18+)을 켜면 `operator[]`, `first`, `last`, `subspan` 전제조건이 검사된다. FAST 모드는 trap 명령 하나라 펌웨어에서도 현실적이다(HardFault로 떨어짐). libstdc++은 `-D_GLIBCXX_ASSERTIONS` 가 비슷한 역할.
- 그래도 **hardening은 안전망이지 로직이 아니다**. 외부 입력(LEN 필드)은 반드시 코드로 검사해 `NeedMore` / `BadLen` 같은 정상 경로로 처리한다. 하네스도 `-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG` 로 돌려 55/55 통과를 확인했다(= solution은 전제조건을 한 번도 어기지 않음).

`LEN=15, 실제 5바이트` 케이스가 `BadLen` 이 아니라 `NeedMore` 인 이유: 스트림에선 나머지가 아직 안 왔을 수 있다. 그래서 상한 검사(`LEN > 16` = 절대 불가능)와 도착 검사(`size < total` = 아직)는 **다른 결과**로 구분한다.

#### Bad* 에서 consumed = 1 인 이유

CRC가 틀렸다고 프레임 전체(`total`)를 버리면, 진짜 SOF가 그 안에 있었을 때 함께 날아간다. SOF 1바이트만 버리고 다음 바이트부터 SOF를 다시 찾는 것이 표준적인 재동기화다. 대가: payload 안에 `0xA5` 가 있으면 가짜 SOF로 잡힐 수 있다 — 실제 프로토콜은 바이트 스터핑(SLIP/COBS)이나 길이+CRC 이중 검사로 푼다(COMM-B10 참고).

### Part C — DMA 링은 "두 개의 span" 이다

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">circular DMA 링 32B: rd=30, wr=6 → 연속 메모리가 아니다</text>
  <g stroke="currentColor" fill="none"><rect x="20" y="50" width="704" height="34"/></g>
  <rect x="20" y="50" width="132" height="34" class="f-ok"/>
  <rect x="680" y="50" width="44" height="34" class="f-ok"/>
  <rect x="152" y="50" width="528" height="34" class="f-mut"/>
  <g fill="currentColor" font-size="12">
    <text x="24" y="104">0</text><text x="148" y="104">6 = wr</text><text x="664" y="104">30 = rd</text>
    <text x="40" y="72">second = ring.first(wr)</text>
    <text x="300" y="72">이미 읽은 / 아직 안 쓴 영역</text>
    <text x="684" y="72">first</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="134">wr = (32 - NDTR) % 32          // NDTR: 남은 전송 수, 0 되면 32로 reload</text>
    <text x="20" y="156">wr &gt;= rd : { ring.subspan(rd, wr - rd), {} }</text>
    <text x="20" y="178">wr &lt;  rd : { ring.subspan(rd), ring.first(wr) }   // [30,32) + [0,6)</text>
  </g>
  <text x="20" y="212" fill="currentColor">span은 (시작, 길이) 하나 → 랩어라운드 불가. 두 조각을 acc에 이어 붙여 파싱.</text>
  <text x="20" y="236" fill="currentColor">하네스의 0x50 프레임(8B) = 링 끝 2B + 앞 6B, 정확히 이 경우.</text>
</svg>
```

`rx_poll` 의 흐름:

| 단계 | 코드 | span 관점 |
|---|---|---|
| ① 쓰기 위치 | `wr = dma_write_index()` | `NDTR` volatile 읽기 + 컴파일러 배리어 |
| ② 새 데이터 | `ring_readable(g_dma_ring, p.rd, wr)` → `rx_append` ×2 | 링을 가리키는 뷰 두 개 → acc로 **복사** (링은 DMA가 곧 덮어씀) |
| ③ 파싱 | `parse_frame(acc.subspan(off))` 반복 | payload 뷰는 acc 안을 가리킴 |
| ④ 전달 | `on_frame(cmd, payload)` | 콜백 동안만 유효 — 핸들러는 **복사**하거나 즉시 처리 |
| ⑤ 정리 | `memmove(acc, acc+off, acc_len-off)` | 이 순간 ③의 모든 뷰가 **무효** |

**왜 링에서 바로 파싱하지 않나?** 프레임이 링 끝을 넘으면 연속이 아니라 span 하나로 못 가리킨다. 또 DMA가 계속 쓰고 있어서 처리 중에 덮어쓸 수 있다. 작은 프레임은 복사 비용이 무시할 만하고, 정말 zero-copy가 필요하면 디스크립터 링 설계로 간다(COMM-A08).

**dma_write_index 의 배리어**: `NDTR` 는 volatile이지만 `g_dma_ring` 은 일반 메모리다. volatile은 volatile끼리의 순서만 지키므로, 컴파일러가 링 읽기를 `NDTR` 읽기 *앞으로* 옮기면 아직 안 쓴 바이트를 읽을 수 있다. `std::atomic_signal_fence(std::memory_order_acquire)` 는 clang/gcc에서 명령을 내지 않는 컴파일러 배리어로 동작한다(CMSIS `__COMPILER_BARRIER()` 와 같은 효과). 하드웨어 쪽은 별도다: D-cache가 있는 Cortex-M7(STM32H7)이면 읽기 전에 `SCB_InvalidateDCache_by_Addr()` 가 필요하다. 자세한 건 C-A02, C-B09 참고.

**rx_append 오버플로 정책**: 48바이트에 못 붙이면 쌓인 바이트를 버리고 새 chunk의 끝부분만 남긴다. 최대 프레임(3+16+1=20B)보다 acc가 크므로 정상 흐름에선 안 일어나고, 일어나면 `overflows` 카운터로 드러난다. 링 자체가 한 바퀴 이상 밀리는 overrun(rd == wr 이 "없음"인지 "32바이트 전부"인지 구분 불가)은 이 레슨 범위 밖 — 실제로는 half/full-transfer 인터럽트와 IDLE 라인 인터럽트로 poll 주기를 보장한다(COMM-M11).

### 수명: span은 소유하지 않는다

`demo_dangling.cpp` — 블록 안 지역 배열의 span이 블록 밖으로 탈출:

```text
$ c++ -std=c++20 -Wall -Wextra -O2 -fno-exceptions -fno-rtti demo_dangling.cpp && ./a.out
sum=720                       ← 경고 0개, 값도 "맞아 보임" (8 × 0x5A = 720). 가장 위험한 형태

$ c++ -std=c++20 -g -O1 -fsanitize=address,undefined demo_dangling.cpp && ./a.out
==32526==ERROR: AddressSanitizer: stack-use-after-scope on address 0x00016f046b80 ...
    #0 ... in sum(std::__1::span<std::byte const, 18446744073709551615ul>) demo_dangling.cpp:14
    #1 ... in main demo_dangling.cpp:29

$ c++ -std=c++20 -Wall -O2 -DSHOW_WARN demo_dangling.cpp     # 단순한 경우는 clang이 잡는다
demo_dangling.cpp:19:87: warning: address of stack memory associated with local variable 'a' returned [-Wreturn-stack-address]
demo_dangling.cpp:31:36: warning: object backing the pointer will be destroyed at the end of the full-expression [-Wdangling-gsl]
```

`std::span<const std::byte> t = make_frame();` 가 **컴파일되는** 이유: C++20 range 생성자는 "range가 borrowed_range이거나 **원소 타입이 const** 이면" 임시 객체도 받는다([span.cons]). const span은 함수 인자로 임시값을 받아야 하니까(`uart_write(bytes(1,2,3))` 는 합법·안전) 이걸 막을 수 없다. 대신 non-const `std::span<std::byte> s = make_frame();` 는 컴파일 에러다(아래 실수 표). 수명 함정 전체 목록은 MCPP-B13 참고. 규칙: **span은 파라미터로 받고, 저장하거나 반환하지 않는다. 반환해야 한다면 호출자가 준 버퍼의 일부만**(추가 연습 6).

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution   # 55/55 passed
c++ -std=c++20 -g -O1 -fsanitize=address,undefined solution.cpp -o sol_asan && ./sol_asan                  # 55/55, 보고 0건
c++ -std=c++20 -O2 -fno-exceptions -fno-rtti -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG solution.cpp -o sol_h && ./sol_h  # 55/55
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `auto pl = buf.subspan(3, len);` (검사 없이) | 전제조건 위반 = UB. 기본 빌드는 버퍼보다 긴 span을 조용히 만든다 | `if (buf.size() < 3 + len + 1) return NeedMore;` 먼저 |
| `if (off + cnt <= s.size())` | 공격 입력 `cnt = SIZE_MAX` 면 덧셈이 랩어라운드해 통과 | `off <= s.size() && cnt <= s.size() - off` (추가 연습 3) |
| `memcpy(dst, s.data(), sizeof(s))` | `sizeof(span)` 은 8/16 — 원소 수가 아님. C의 `sizeof(ptr)` 버그가 그대로 | `s.size()` / `s.size_bytes()` |
| `uart_write(std::as_bytes(std::span{"AT\r\n"}))` | 리터럴은 NUL 포함 5바이트 → 0x00 이 선으로 나감 | `.first<4>()` 또는 `std::string_view` 경유 |
| `uart_write(u8_array)` (`std::array<uint8_t,N>`) | uint8_t → byte 암시적 변환 없음 (에러 E1) | `std::as_bytes(std::span{u8_array})` |
| `std::byte b{len};` (`len` 이 `int` 변수) | 리스트 초기화의 축소 변환 → 에러 E4 | `static_cast<std::byte>(len)` |
| `void f(const std::span<T>& s)` | span은 2워드 값. 참조는 간접 접근만 추가 | `void f(std::span<T> s)` |
| `static std::span<const std::byte> g_last;` 에 콜백 payload 저장 | 다음 `rx_poll` 의 memmove로 내용이 바뀜 → dangling | 콜백 안에서 복사 (`on_frame` 처럼) |
| 지역 배열의 span을 반환 / 블록 밖으로 | 스택이 죽은 뒤 접근. `-O2` 에서 그럴듯한 값이 나와 더 위험 | 호출자 버퍼를 받아 그 일부를 반환 |
| DMA 링을 span 하나로 `ring.subspan(rd, wr - rd)` | `wr < rd` 면 `wr - rd` 가 거대한 수 → UB | 두 조각으로 (`ring_readable`) |

`demo_errors.cpp` 를 `-DSHOW_ERROR` 로 빌드한 실제 에러(요약):

```text
$ c++ -std=c++20 -Wall -Wextra -O2 -fno-exceptions -fno-rtti -DSHOW_ERROR demo_errors.cpp
demo_errors.cpp:22:5: error: no matching function for call to 'uart_write'
  note: candidate function not viable: no known conversion from 'std::array<uint8_t, 4>'
        (aka 'array<unsigned char, 4>') to 'std::span<const std::byte>' for 1st argument
demo_errors.cpp:23:5: error: no matching function for call to 'read_into'
  note: ... no known conversion from 'const std::array<std::byte, 4>' to 'std::span<std::byte>'
demo_errors.cpp:24:5: error: no matching function for call to 'take8'
  note: ... no known conversion from 'std::array<std::byte, 4>' to 'std::span<const std::byte, 8>'
demo_errors.cpp:25:17: error: non-constant-expression cannot be narrowed from type 'int' to 'std::byte'
        in initializer list [-Wc++11-narrowing]
demo_errors.cpp:26:26: error: no viable conversion from 'std::array<std::byte, 4>' to 'std::span<std::byte>'
  note: candidate constructor [with _OtherElementType = std::byte, _Sz = 4] not viable: expects an lvalue for 1st argument
```

같은 사실들은 하네스 위쪽 `static_assert(!std::is_convertible_v<...>)` 로도 박혀 있어서, 표준 라이브러리 동작이 바뀌면 빌드가 깨진다.

## 8. 면접 Q&A

<details>
<summary>Q1. (ptr, len) API를 span으로 바꾸면 뭐가 좋아지나요? 비용은?</summary>

길이가 버퍼에서 자동으로 따라와 호출자가 틀린 길이를 줄 수 없고(`sizeof(ptr)` 버그, 다른 버퍼의 길이), 잘라낸 조각도 자기 길이를 들고 다닌다. 배열 decay가 없다. 비용: 동적 extent는 포인터+size_t 두 워드라 C 두 인자와 같은 레지스터로 전달되고, -O2 어셈블리가 명령 단위로 같다(demo_asm 실측). 꼬리질문 "그럼 안전한가?" → 아니다. `operator[]` / `subspan` 은 검사하지 않는다. span이 막는 건 "길이 불일치"이지 "범위 초과"가 아니다.

</details>

<details>
<summary>Q2. span, std::array, std::vector, std::string_view 의 차이는?</summary>

소유 여부와 크기 결정 시점. `std::array<T,N>` 은 소유·고정 크기(값 의미론, CPP-B10), `std::vector` 는 소유·힙(펌웨어에선 보통 금지, CPP-B14), `std::span<T>` 는 **비소유** 연속 메모리 뷰(읽기·쓰기 가능), `std::string_view` 는 비소유 읽기 전용 문자 뷰. 펌웨어 API 규칙: 저장은 `std::array` / 정적 버퍼, 함수 경계는 `span` / `string_view`.

</details>

<details>
<summary>Q3. 정적 extent와 동적 extent는 언제 쓰나요? sizeof 는?</summary>

크기가 프로토콜·하드웨어로 고정이면 정적: 3바이트 헤더, 4바이트 CRC32, 6바이트 MAC 주소, 256바이트 flash page. `std::span<const std::byte, 4>` 를 받는 `get_be32` 는 3바이트를 주면 컴파일 에러다. 정적 extent는 길이를 저장하지 않아 포인터 1개(arm64 8B / Cortex-M 4B), 동적은 2개. 동적 → 정적 변환은 `explicit` 이고 크기가 같아야 한다는 전제조건(검사 없음)이 있다. 표준이 sizeof를 강제하진 않지만 3대 구현 모두 이렇다.

</details>

<details>
<summary>Q4. subspan 범위 초과는 어떻게 되나요? 펌웨어에서 어떻게 막죠?</summary>

전제조건 위반이라 UB — 예외도 에러 코드도 없다. 막는 법: ① 코드로 길이 검사(외부 입력은 항상) ② 빌드 옵션으로 안전망: libc++ `-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST` (trap 한 개, 실측 exit 133), libstdc++ `-D_GLIBCXX_ASSERTIONS` ③ 테스트에서 ASan/UBSan ④ 헬퍼 `checked_subspan` 을 프로젝트 표준으로. C++26 `span::at()` 은 예외를 던져서 `-fno-exceptions` 에선 의미가 없다. C++20 이전 툴체인이면 ETL `etl::span`, GSL `gsl::span`(Expects로 검사).

</details>

<details>
<summary>Q5. span을 값으로 넘기나요, const 참조로 넘기나요? 반환해도 되나요?</summary>

값으로. trivially copyable 2워드라 레지스터로 간다(arm64 x0/x1, AAPCS32 r0/r1). const&는 메모리 간접 접근만 추가. 반환은 AAPCS32에서 4바이트 넘는 복합 타입이라 숨은 포인터로 메모리 반환(정적 extent 1워드는 r0) — 실행해 본 건 아니고 ABI 규칙상 예상. 더 중요한 건 수명: 반환하는 span은 **호출자가 소유한 버퍼의 일부**여야 한다(`std::to_chars` 처럼 "쓴 부분"을 돌려주는 패턴).

</details>

<details>
<summary>Q6. as_bytes 로 구조체를 바이트로 보는 건 strict aliasing 위반 아닌가요?</summary>

아니다. [basic.lval]/11 은 모든 객체를 `char`, `unsigned char`, `std::byte` 로 접근하는 것을 허용한다. `as_writable_bytes` 로 *쓰는* 것도 trivially copyable 타입이면 memcpy와 같은 의미다. 다만 패딩 바이트, 엔디안, 정렬은 언어가 해결해 주지 않는다 — 네트워크/센서 프레임은 필드별 `get_le16` 같은 직렬화가 정석(C-M05, C-B12). 반대 방향(`reinterpret_cast<Header*>(buf.data())`)은 aliasing + 정렬 문제로 UB이니 피한다.

</details>

<details>
<summary>Q7. DMA가 쓰는 버퍼를 span으로 감싸도 되나요? volatile이 필요한가요?</summary>

span 자체는 괜찮다 — 핵심은 순서와 캐시. 버퍼를 전부 volatile로 만들 필요는 없고(성능 손해, `span<volatile std::byte>` 는 표준 알고리즘과도 잘 안 맞음), "DMA가 여기까지 썼다"는 정보(NDTR, 완료 플래그)를 volatile로 읽은 **뒤에** 컴파일러 배리어(`atomic_signal_fence` / `__COMPILER_BARRIER`)를 두고, D-cache가 있으면 해당 범위를 invalidate한 후 읽는다. 그리고 DMA가 곧 덮어쓸 영역의 span은 오래 들고 있지 말고 복사해 둔다(Part C의 rx_append).

</details>

## 9. 추가 연습

### 연습 1 — starts_with

`bool starts_with(std::span<const std::byte> s, std::span<const std::byte> prefix)`. AT 명령 응답이 `"OK"` 로 시작하는지 확인하라. prefix가 s보다 길어도 UB가 없어야 한다.

<details>
<summary>답 보기</summary>

`s.size() >= prefix.size() && std::ranges::equal(s.first(prefix.size()), prefix)`. `&&` 의 단락 평가 덕에 길이 검사가 실패하면 `first()` 는 호출되지 않는다. 호출: `starts_with(rx, std::as_bytes(std::span{"OK"}).first<2>())`.

</details>

### 연습 2 — 정적 extent로 길이를 타입에 박기

`uint16_t get_le16(std::span<const std::byte, 2>)`, `uint32_t get_be32(std::span<const std::byte, 4>)`. 패킷 `7E 34 12 DE AD BE EF` 에서 오프셋 1의 LE16(0x1234)과 오프셋 3의 BE32(0xDEADBEEF)를 꺼내라.

<details>
<summary>답 보기</summary>

`p.subspan<1, 2>()` 는 `span<const std::byte, 2>` 를 돌려준다 — 동적 span에서 호출하면 `1 + 2 <= size()` 는 여전히 **실행 시간 전제조건**이므로 먼저 `p.size() >= 7` 을 확인한다. 정적 span에서 호출했다면 컴파일 타임에 검사된다. `std::span<const std::byte, 4>{p.data() + 3, 4}` 처럼 명시적 생성도 된다. 3바이트 span을 `get_be32` 에 넘기면 컴파일 에러(`static_assert(!is_convertible_v<span<..,3>, span<..,4>>)`).

</details>

### 연습 3 — 절대 UB가 안 나는 checked_subspan

`bool checked_subspan(s, off, cnt, out)`. `off == size(), cnt == 0` 은 합법(빈 span), `cnt = SIZE_MAX` 같은 공격 입력은 거부.

<details>
<summary>답 보기</summary>

`if (off > s.size() || cnt > s.size() - off) return false;` — 왼쪽이 거짓이면 `s.size() - off` 는 언더플로하지 않는다. `off + cnt <= s.size()` 로 쓰면 `2 + SIZE_MAX` 가 1로 랩어라운드해서 통과해 버린다. 이 형태가 [span.sub] 전제조건 문구(`count <= size() - offset`)와 똑같다는 점도 보라.

</details>

### 연습 4 — page 단위로 쪼개 쓰기

70바이트 이미지를 32바이트 SPI flash page로 나눠 `flash_page_write(addr, page)` 를 호출하라. 호출 횟수와 각 크기(32, 32, 6)를 검증. chunk=0이면 0회.

<details>
<summary>답 보기</summary>

```cpp
while (!data.empty()) {
    const size_t k = std::min(chunk, data.size());
    flash_page_write(addr, data.first(k));
    data = data.subspan(k);   // k <= size() 보장
    addr += k;
}
```

span을 "남은 부분" 커서로 쓰는 패턴 — C의 `p += k; len -= k;` 두 줄이 한 줄로 합쳐져 둘이 어긋날 수 없다. chunk=0 검사가 없으면 무한 루프.

</details>

### 연습 5 — 바이트 → 구조체 (as_writable_bytes)

`template<class T> bool load_pod(std::span<const std::byte> src, T& out)` — trivially copyable만 허용, 길이가 모자라면 false. `01 00 F6 FF` → `{id=1, temp_c10=-10}`.

<details>
<summary>답 보기</summary>

`std::as_writable_bytes(std::span<T, 1>{&out, 1})` 는 `span<std::byte, sizeof(T)>` — 정적 extent라 크기가 타입에 박힌다(`static_assert(... ::extent == 4)`). `std::span{&out, 1}` 로 쓰면 CTAD가 동적 extent를 고른다는 점에 주의. 복사는 `std::ranges::copy(src.first(dst.size()), dst.begin())`. 리틀 엔디안 가정은 `static_assert(std::endian::native == std::endian::little)` 로 명시.

</details>

### 연습 6 — span을 반환하는 올바른 방법

`std::span<std::byte> format_ack(std::span<std::byte> out, uint8_t seq)` — `06 seq ~seq` 3바이트를 쓰고 **실제로 쓴 부분**을 반환. 자리가 모자라면 빈 span.

<details>
<summary>답 보기</summary>

`return out.first(3);` — 반환 span은 호출자 버퍼 안을 가리키므로 수명 문제가 없다. 지역 `std::array` 를 만들어 그 span을 반환하는 것(-Wreturn-stack-address)과 대비된다. `std::to_chars` 가 "쓴 끝 위치"를 돌려주는 것과 같은 설계.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 19/19 passed
```

데모 파일(레슨 폴더에 있음):

```bash
B="-std=c++20 -O2 -fno-exceptions -fno-rtti"
c++ $B -S -o - demo_asm.cpp                                   # (ptr,len) vs span vs span<...,3> 어셈블리
c++ $B -Wall -Wextra -DSHOW_ERROR demo_errors.cpp             # 컴파일 에러 5종
c++ $B demo_hardening.cpp -o h0 && ./h0 9                     # UB: payload.size()=9 (버퍼는 8)
c++ $B -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG demo_hardening.cpp -o h2 && ./h2 9   # assertion
c++ -std=c++20 -g -O1 -fsanitize=address,undefined demo_dangling.cpp && ./a.out                   # stack-use-after-scope
```

## 10. 핵심 정리

- `std::span<T>` = (포인터, 길이) 한 값. 동적 extent 2워드, 정적 extent `span<T,N>` 1워드. 값으로 넘긴다 — 어셈블리는 C `(ptr, len)` 와 같다.
- `std::array` / C 배열 → span 은 암시적(길이 자동). `uint8_t` 버퍼 → `std::as_bytes`, 구조체 받기 → `std::as_writable_bytes(std::span{&s,1})`. 문자열 리터럴은 NUL 포함.
- `first` / `last` / `subspan` / `operator[]` 는 **검사하지 않는다** (전제조건 위반 = UB). 외부 길이는 코드로 검사, hardening(`_LIBCPP_HARDENING_MODE`, `_GLIBCXX_ASSERTIONS`)은 안전망.
- 범위 검사는 `off <= size && cnt <= size - off` 형태로 — `off + cnt` 는 랩어라운드한다.
- 파서 결과는 복사 없는 뷰: payload span은 입력 버퍼 안을 가리킨다. 버퍼가 바뀌면(memmove, DMA 덮어쓰기) 뷰도 무효.
- span은 비소유: 파라미터로 받고, 저장·반환하지 않는다. 반환한다면 호출자 버퍼의 일부만.
- 원형 DMA 링 = 최대 두 개의 span. 프레임 조립은 선형 버퍼에 복사한 뒤, NDTR 읽은 다음 컴파일러 배리어(+ D-cache invalidate).

## 참고자료

- C++20 표준 [views.span] — [span.cons] 생성자 제약(const 추가만 허용, borrowed_range), [span.sub] first/last/subspan 전제조건, [span.objectrep] as_bytes / as_writable_bytes
- [cppreference — std::span](https://en.cppreference.com/w/cpp/container/span), [std::as_bytes](https://en.cppreference.com/w/cpp/container/span/as_bytes), [std::byte](https://en.cppreference.com/w/cpp/types/byte)
- P2251R1 — Require span & basic_string_view to be trivially copyable (C++23)
- P2821R5 — span.at() (C++26)
- [libc++ Hardening Modes](https://libcxx.llvm.org/Hardening.html)
- [Embedded Template Library — etl::span (C++11용)](https://www.etlcpp.com/span.html), [C++ Core Guidelines — I.13: Do not pass an array as a single pointer, F.24 span](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Ri-array)
- STM32F4 RM0090 — DMA stream NDTR, circular mode / USART IDLE line ([PDF](https://www.st.com/resource/en/reference_manual/rm0090-stm32f405415-stm32f407417-stm32f427437-and-stm32f429439-advanced-armbased-32bit-mcus-stmicroelectronics.pdf))
- ARM AAPCS32 — 복합 타입 인자/반환 규칙 ([IHI 0042](https://github.com/ARM-software/abi-aa/releases))
- CRC-8/SMBUS 파라미터: [CRC RevEng catalogue](https://reveng.sourceforge.io/crc-catalogue/1-15.htm)
