---
id: C-M05
time: 75분
updated: 2026-11-07
---
# C-M05 엔디안 독립 직렬화: get_le16 / put_be32

## 학습 목표

- `get/put_{le,be}{16,32,64}` 12개 헬퍼를 **시프트와 `uint8_t` 만으로** 작성한다. 호스트 엔디안, 버퍼 정렬, 포인터 캐스트에 전혀 기대지 않는다.
- 32/64비트 값을 바이트로 조립할 때의 **정수 승격(integer promotion)** 함정 세 가지(int 오버플로, 64비트 부호 확장, 시프트 폭 초과)를 UBSan 출력으로 보여 주고 고친다.
- 부호 있는 필드(int16), `float` (memcpy 로 비트 이동), **경계 검사 커서(bounded cursor)** 와 sticky error 패턴으로 "직렬화 계층"을 만든다.
- 헤더·페이로드·CRC 자리를 가진 텔레메트리 패킷의 encode/decode 를 작성하고, **고정 바이트 벡터(golden vector) + 왕복(round-trip) + 오류 주입** 세 가지 테스트로 검증한다.
- 같은 C 코드가 arm64 / Cortex-M4 / Cortex-M0 에서 어떤 명령(`ldr+rev`, 바이트 로드, `vmov+str`)이 되는지 실제 asm 으로 설명한다.

## 1. 문제

펌웨어가 밖으로 내보내는 모든 바이트 — 텔레메트리, 로그, 설정 블롭, 부트로더 이미지 헤더 — 는 **"메모리에 있는 구조체"가 아니라 "명세서에 적힌 바이트 배열"** 이다. 면접에서 "이 패킷 인코더/디코더를 짜 보세요" 는 엔디안·정렬·패딩·승격·경계 검사를 한 번에 보는 단골 문제이고, 실무에서는 지상국/호스트 도구와 MCU 가 한 바이트라도 다르게 해석하면 데이터가 조용히 망가진다.

C-B13 에서 `read_be32()` 같은 헬퍼 몇 개와 "왜 `*(uint32_t *)(buf + off)` 가 오답인가"를 이미 다뤘다 (C-B13 참고). 이번에는 그것을 **계층(layer)** 으로 키운다: 폭·엔디안 전체 조합, 부호·실수 필드, 버퍼 넘침을 막는 커서, 그리고 그 위의 실제 패킷 코덱. 대안인 `packed` 구조체 캐스팅은 C-M04 가, 포인터 캐스트 type punning 의 위험은 C-M13 이 다룬다 — 여기서는 "캐스트 없이 바이트로" 가 정답 쪽이다.

이번 문제의 텔레메트리 프레임 (35바이트). 선로(wire) 형식은 명세가 정한다 — sync 와 CRC 는 **상위 바이트 먼저(BE)**, 데이터 필드는 MCU 에 자연스러운 **LE** (MAVLink 처럼). 헤더가 7바이트라서 페이로드의 모든 다바이트 필드가 **비정렬 오프셋**에 놓인다:

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">텔레메트리 프레임 — 1바이트 = 19px, 숫자는 바이트 오프셋</text>
  <g fill="currentColor" font-size="12">
    <text x="60" y="48">헤더 7B</text><text x="370" y="48">페이로드 26B</text><text x="652" y="48">CRC 2B</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M30 54 v6 h133 v-6"/><path d="M163 54 v6 h494 v-6"/><path d="M657 54 v6 h38 v-6"/>
  </g>
  <g stroke="currentColor">
    <rect x="30" y="66" width="38" height="40" class="f-warn"/>
    <rect x="68" y="66" width="19" height="40" class="f-mut"/>
    <rect x="87" y="66" width="38" height="40" class="f-acc"/>
    <rect x="125" y="66" width="38" height="40" class="f-acc"/>
    <rect x="163" y="66" width="152" height="40" class="f-acc"/>
    <rect x="315" y="66" width="38" height="40" class="f-acc"/>
    <rect x="353" y="66" width="38" height="40" class="f-acc"/>
    <rect x="391" y="66" width="114" height="40" class="f-acc"/>
    <rect x="505" y="66" width="76" height="40" class="f-acc"/>
    <rect x="581" y="66" width="76" height="40" class="f-acc"/>
    <rect x="657" y="66" width="38" height="40" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="49" y="90">sync</text><text x="77" y="90">t</text><text x="106" y="90">seq</text><text x="144" y="90">len</text>
    <text x="239" y="90">time_us u64</text><text x="334" y="90">temp</text><text x="372" y="90">vbat</text>
    <text x="448" y="90">accel i16 x3</text><text x="543" y="90">press f32</text><text x="619" y="90">status</text>
    <text x="676" y="90">crc</text>
  </g>
  <g fill="currentColor" font-size="12" font-family="monospace" text-anchor="middle">
    <text x="30" y="122">0</text><text x="68" y="122">2</text><text x="87" y="136">3</text><text x="125" y="122">5</text>
    <text x="163" y="136">7</text><text x="315" y="122">15</text><text x="353" y="136">17</text><text x="391" y="122">19</text>
    <text x="505" y="122">25</text><text x="581" y="122">29</text><text x="657" y="122">33</text><text x="695" y="136">35</text>
  </g>
  <rect x="30" y="160" width="16" height="14" class="f-warn" stroke="currentColor"/>
  <text x="54" y="172" fill="currentColor">BE: sync EB 90, CRC 상위 바이트 먼저 → put_be16</text>
  <rect x="30" y="184" width="16" height="14" class="f-acc" stroke="currentColor"/>
  <text x="54" y="196" fill="currentColor">LE: seq, len, 페이로드 전부 → put_le16/32/64, put_le_f32</text>
  <rect x="30" y="208" width="16" height="14" class="f-mut" stroke="currentColor"/>
  <text x="54" y="220" fill="currentColor">1바이트: type = 0x21 (엔디안 없음)</text>
  <text x="10" y="252" fill="currentColor">time_us 는 오프셋 7 (4·8 의 배수 아님) → *(uint64_t *)(buf+7) 은 비정렬 UB, M0 는 HardFault.</text>
  <text x="10" y="276" fill="currentColor">C 구조체 telem_t 는 32바이트(패딩 6B, C-B12) — 선로의 26바이트와 모양이 다르다. 그래서 필드별로 직렬화한다.</text>
</svg>
```

모든 파트는 `starter.c` 하나에 있다. 규칙: 포인터 캐스트 금지, `#if __BYTE_ORDER__` 같은 호스트 엔디안 분기 금지, `__builtin_bswap*` 금지.

### Part A — 고정 폭 헬퍼 12개

`get_le16` / `put_le16` 은 예시로 주어진다. 나머지 `get_be16, get_le32, get_be32, get_le64, get_be64, put_be16, put_le32, put_be32, put_le64, put_be64` 를 작성하라. 테스트는 홀수 주소에서 시작하는 고정 벡터 `01 02 … 08`, 상위 비트가 선 바이트 `FF FE … F8` (승격 함정 탐지), 쓰기 주변 바이트가 그대로인지(sentinel `0xCC`), 그리고 임의 오프셋 왕복 1000회를 본다.

### Part B — 부호 · float · 경계 검사 커서

- `s16_from_u16(u)` — `0xFB2E → -1234`. `(int16_t)u` 는 `u > 0x7FFF` 일 때 **구현 정의**라 쓰지 않는다.
- `get_le_f32` / `put_le_f32` — `float` 의 비트를 `memcpy` 로 `uint32_t` 에 옮겨 LE 로. NaN·Inf 포함 임의 비트 1000개가 그대로 왕복해야 한다.
- `w_reserve(w, n)` / `r_take(r, n)` — 커서에서 n 바이트를 예약/소비. 모자라면 `err = true` 를 세우고 NULL, `pos` 는 그대로. **한 번 선 err 는 계속 유지(sticky)** 되어, 그 뒤의 1바이트 쓰기도 거부한다. `n = SIZE_MAX` 같은 값에도 랩어라운드(wrap-around) 없이 거부해야 한다. 그 위의 `w_le32`, `r_le16s` 같은 래퍼는 주어진다.

### Part C — 텔레메트리 패킷 encode / decode

- `telem_encode(t, seq, out, cap)` — 위 그림대로 35바이트를 쓰고 35 반환. `cap` 이 모자라면 0, **cap 밖에는 한 바이트도 쓰지 않는다**. CRC 는 바이트 0..32 에 대한 CRC-16/CCITT-FALSE (주어진 함수, 검사값 `"123456789" → 0x29B1`) 를 BE 로.
- `telem_decode(in, n, t, seq)` — 성공하면 소비한 바이트 수(35), 실패하면 `TELEM_ERR_SHORT/SYNC/TYPE/LEN/CRC`. 실패 시 `*t`, `*seq` 는 **건드리지 않는다**. 뒤에 남는 바이트는 무시한다(스트림 파서가 다음 프레임으로 넘어갈 수 있게).
- 테스트: Python 으로 독립 생성한 고정 바이트 벡터와 정확히 일치, 280가지 1비트 오류 전부 거부, 임의 필드 500회 왕복.

## 2. Problem (English)

> **Endian-independent serialization layer.**
>
> Implement in C11, using only shifts on `uint8_t` (no pointer casts, no host-endianness checks, no compiler builtins):
>
> 1. `uint16_t get_be16(const uint8_t *p)`, `uint32_t get_le32/get_be32(...)`, `uint64_t get_le64/get_be64(...)` and `void put_be16/put_le32/put_be32/put_le64/put_be64(uint8_t *p, value)`. They must work at any alignment and on any host.
> 2. `int16_t s16_from_u16(uint16_t)` without implementation-defined conversions; `float get_le_f32(const uint8_t *)` / `void put_le_f32(uint8_t *, float)` that move the exact bit pattern (NaN payloads included). A bounded write cursor `uint8_t *w_reserve(wcur_t *w, size_t n)` and read cursor `const uint8_t *r_take(rcur_t *r, size_t n)` with a sticky error flag; they must never write/read outside the buffer and must not overflow on huge `n`.
> 3. A 35-byte telemetry frame: `sync u16 BE = 0xEB90`, `type u8 = 0x21`, `seq u16 LE`, `len u16 LE = 26`, payload `{u64 time_us, i16 temp_cC, u16 vbat_mV, i16 accel_mg[3], f32 pressure_pa, u32 status}` all LE, then `CRC-16/CCITT-FALSE` over bytes 0..32 stored big-endian. Implement `size_t telem_encode(const telem_t *, uint16_t seq, uint8_t *out, size_t cap)` (returns 35, or 0 if `cap` is too small, never writing past `cap`) and `int telem_decode(const uint8_t *in, size_t n, telem_t *, uint16_t *seq)` (returns 35 or a negative error; outputs untouched on error).
>
> Example: `{time_us=0x0123456789ABCDEF, temp=-1234, vbat=3700, accel={-1000,2,1000}, pressure=101325.0f, status=0xDEADBEEF}`, `seq=0x1234` encodes to `EB 90 21 34 12 1A 00 EF CD AB 89 67 45 23 01 2E FB 74 0E 18 FC 02 00 E8 03 80 E6 C5 47 EF BE AD DE CE 65`.
>
> Follow-ups: what does `get_be32` compile to on Cortex-M4 vs Cortex-M0? Why can round-trip tests alone not prove a serializer correct?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 의 tapi 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 줄이 47개 나오고 "9/56 passed" — TODO 를 채워 56/56 을 만든다
```

관찰용 파일 두 개가 같은 폴더에 있다 (하네스와 별개):

```bash
cc -std=c11 -O2 demo_promo.c -o demo_promo && ./demo_promo                                 # 승격 함정 3가지 (일부러 UB)
cc -std=c11 -g -O1 -fsanitize=undefined demo_promo.c -o demo_promo_ub && ./demo_promo_ub   # UBSan 보고
clang -std=c11 -O2 -S demo_asm.c -o -                                                      # arm64 asm
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -O2 -S demo_asm.c -o -
clang --target=thumbv6m-none-eabi -mcpu=cortex-m0 -mthumb -O2 -S demo_asm.c -o -
```

## 4. 힌트

<details>
<summary>힌트 1 — "바이트 k 의 무게" 만 정하면 된다</summary>

LE 는 바이트 k 가 `256^k` 자리, BE 는 `256^(n-1-k)` 자리. get 은 `(T)p[k] << (8 * 자리)` 를 OR 로 모으고, put 은 `p[k] = (uint8_t)(v >> (8 * 자리))`. 호스트가 무엇인지는 이 식 어디에도 나오지 않는다.

</details>

<details>
<summary>힌트 2 — 시프트하기 "전에" 결과 폭으로 캐스트</summary>

`p[0] << 24` 에서 `p[0]` 은 `int` 로 승격된다 (C-B02). `0xFF << 24` 는 int 범위 밖 → UB. `(uint32_t)p[0] << 24` 처럼 **먼저** 무부호 결과 타입으로. 64비트는 `get_le32` 두 개를 `(uint64_t)` 로 넓혀 `<< 32` 로 합치면 깔끔하다 — `(uint32_t)x << 32` 는 시프트 폭 초과 UB.

</details>

<details>
<summary>힌트 3 — 커서의 검사는 빼기로</summary>

`pos + n > cap` 은 `n` 이 크면 `size_t` 덧셈이 랩어라운드해서 통과해 버린다. 불변식 `pos <= cap` 을 지키고 있으니 `n > cap - pos` 는 절대 랩하지 않는다. 그리고 `if (err || ...)` 로 err 를 먼저 본다.

</details>

<details>
<summary>힌트 4 — decode 는 "믿기 전에 검사"</summary>

길이가 헤더만큼 되나 → sync → type → `len` 필드 값이 기대값인가 → 프레임 전체가 들어왔나 → CRC → 그다음에야 필드를 지역 `telem_t` 에 읽고, 마지막에 한 번에 `*t = d`. CRC 는 BE 로 저장했으니 `get_be16(in + 33)` 와 비교.

</details>

## 5. 풀이

### Part A — 12개 헬퍼: 무게만 다르고 모양은 같다

| 함수 | 바이트 0 의 자리 | 핵심 식 |
|---|---|---|
| `get_be16` | 상위 | `(uint16_t)((p[0] << 8) \| p[1])` — int 로 승격돼도 최대 `0xFFFF`, 안전 |
| `get_le32` | 하위 | `(uint32_t)p[0] \| (uint32_t)p[1] << 8 \| (uint32_t)p[2] << 16 \| (uint32_t)p[3] << 24` |
| `get_be32` | 상위 | `(uint32_t)p[0] << 24 \| … \| (uint32_t)p[3]` |
| `get_le64` | 하위 | `(uint64_t)get_le32(p) \| (uint64_t)get_le32(p + 4) << 32` |
| `get_be64` | 상위 | `(uint64_t)get_be32(p) << 32 \| get_be32(p + 4)` |
| `put_be32` | 상위 | `p[0] = (uint8_t)(v >> 24); … p[3] = (uint8_t)v;` |
| `put_le64` | 하위 | `put_le32(p, (uint32_t)v); put_le32(p + 4, (uint32_t)(v >> 32));` |

- **put 쪽은 승격 걱정이 없다**: `v` 는 이미 무부호 32/64비트이고, 오른쪽 시프트 후 `(uint8_t)` 변환은 mod 256 으로 정의돼 있다 (C11 6.3.1.3p2).
- **get 쪽이 함정이다**: `uint8_t` 는 `int` 보다 좁아서 산술에 들어가는 순간 `int` 로 승격된다 (6.3.1.1p2). 32비트 int 에서 `p[0] << 24` 는 `p[0] >= 0x80` 이면 결과가 int 로 표현되지 않아 UB (6.5.7p4).

`demo_promo.c` 의 틀린 세 가지 버전을 바이트 `FF FE FD FC FB FA F9 F8` 로 돌린 **실제 출력** (Apple clang 21, arm64):

```text
$ cc -std=c11 -Wall -Wextra -O2 demo_promo.c -o demo_promo && ./demo_promo      # 경고 0개
be32_bug       = 0xFFFEFDFC          (기대 0xFFFEFDFC)
be64_bug_sext  = 0xFFFFFFFFFBFAF9F8  (기대 0xFFFEFDFCFBFAF9F8)
be64_bug_width = 0xFFFFFFFFFBFAF9F8
be64_ok        = 0xFFFEFDFCFBFAF9F8

$ cc -std=c11 -O0 demo_promo.c -o demo_promo0 && ./demo_promo0
...
be64_bug_width = 0x00000000FFFEFDFC          ← -O0 과 -O2 결과가 다르다

$ cc -std=c11 -g -O1 -fsanitize=undefined demo_promo.c -o demo_promo_ub && ./demo_promo_ub
demo_promo.c:11:28: runtime error: left shift of 255 by 24 places cannot be represented in type 'int'
demo_promo.c:15:29: runtime error: left shift of 255 by 24 places cannot be represented in type 'int'
demo_promo.c:16:28: runtime error: left shift of 251 by 24 places cannot be represented in type 'int'
demo_promo.c:21:53: runtime error: shift exponent 56 is too large for 32-bit type 'uint32_t' (aka 'unsigned int')
(각 줄 뒤의 SUMMARY 줄과 값 출력 4줄은 생략 — 값은 -O1 에서 -O0 과 같았다)
```

| 버전 | 코드 | 무엇이 일어났나 |
|---|---|---|
| ① `be32_bug` | `(uint32_t)(p[0] << 24 \| …)` | int 오버플로 UB. 그런데 값은 **맞게 나온다** (2의 보수 랩 → uint32 변환). 테스트로는 못 잡고 UBSan 만 잡는다 — 가장 위험한 종류 |
| ② `be64_bug_sext` | 32비트를 `int` 로 조립 후 `(uint64_t)` | 음수 int → uint64 변환이 **부호 확장** 결과(`2^64 + 값`)라 하위 반쪽의 `FFFFFFFF` 가 상위 반쪽을 덮었다 |
| ③ `be64_bug_width` | `(uint32_t)p[i] << 56` | 시프트 폭 ≥ 타입 폭 → UB (6.5.7p3). arm64 `-O0` 은 `lsl w` 가 시프트 양을 mod 32 로 써서 상위 4바이트가 하위에 겹쳐 OR 됨. `-O2` 는 또 다른 값 |

`-Wall -Wextra` 는 셋 다 아무 경고도 내지 않았다. 그래서 Part A 테스트에는 `hi8 = {FF, FE, …}` 벡터가 있고 (②, ③ 은 값이 틀려 FAIL), 정답은 ① 까지 막으려고 **모든 바이트를 시프트 전에 `(uint32_t)` / `(uint64_t)` 로** 캐스트한다. Part A 정답이 ASan/UBSan 에서 조용한 것도 확인했다.

### Part A — 컴파일러는 이 바이트 조립을 알아본다

같은 함수들을 `demo_asm.c` 로 세 타깃에서 컴파일한 **실제 결과** (지시어 생략):

```text
# 호스트 arm64 (clang -O2)                 # Cortex-M4 (thumbv7em, -O2)
_get_be32:  ldr w8,[x0] ; rev w0,w8         get_be32:  ldr r0,[r0] ; rev r0,r0
_get_le64:  ldr x0,[x0]                     get_le64:  ldr r2,[r0] ; ldr r1,[r0,#4] ; mov r0,r2
_get_be64:  ldr x8,[x0] ; rev x0,x8         get_be64:  ldr r1,[r0] ; ldr r0,[r0,#4] ; rev r1,r1 ; rev r0,r0
_put_be32:  rev w8,w1 ; str w8,[x0]         put_be32:  rev r1,r1 ; str r1,[r0]
_put_be64:  rev x8,x1 ; str x8,[x0]         put_be64:  lsrs/strb x 8  (바이트 저장 8개, 15명령)
_get_be16:  ldrh w8,[x0] ; rev w8,w8 ; lsr w0,w8,#16
_s16_from_u16:  sxth w0,w0                  s16_from_u16:  sxth r0,r0
_put_le_f32:    str s0,[x0]                 put_le_f32:    vmov r1,s0 ; str r1,[r0]
_get_le_f32:    ldr s0,[x0]                 get_le_f32:    ldr r0,[r0] ; vmov s0,r0
```

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 C 소스(시프트 조립) → 타깃별 명령 (clang 21 -O2 실측, bx/ret 포함 명령 수)</text>
  <g fill="currentColor" font-weight="bold">
    <text x="20" y="50">함수</text><text x="170" y="50">arm64 (Apple M)</text><text x="370" y="50">Cortex-M4</text><text x="570" y="50">Cortex-M0</text>
  </g>
  <g stroke="currentColor">
    <rect x="160" y="62" width="190" height="30" class="f-ok"/><rect x="360" y="62" width="190" height="30" class="f-ok"/><rect x="560" y="62" width="170" height="30" class="f-warn"/>
    <rect x="160" y="102" width="190" height="30" class="f-ok"/><rect x="360" y="102" width="190" height="30" class="f-ok"/><rect x="560" y="102" width="170" height="30" class="f-warn"/>
    <rect x="160" y="142" width="190" height="30" class="f-ok"/><rect x="360" y="142" width="190" height="30" class="f-ok"/><rect x="560" y="142" width="170" height="30" class="f-warn"/>
    <rect x="160" y="182" width="190" height="30" class="f-ok"/><rect x="360" y="182" width="190" height="30" class="f-warn"/><rect x="560" y="182" width="170" height="30" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="82">get_be32</text><text x="170" y="82">ldr + rev (3)</text><text x="370" y="82">ldr + rev (3)</text><text x="570" y="82">ldrb x4 ... (11)</text>
    <text x="20" y="122">get_be64</text><text x="170" y="122">ldr x + rev x (3)</text><text x="370" y="122">ldr x2 + rev x2 (5)</text><text x="570" y="122">ldrb x8 ... (22)</text>
    <text x="20" y="162">put_be32</text><text x="170" y="162">rev + str (3)</text><text x="370" y="162">rev + str (3)</text><text x="570" y="162">strb x4 ... (8)</text>
    <text x="20" y="202">put_be64</text><text x="170" y="202">rev x + str x (3)</text><text x="370" y="202">strb x8 ... (15)</text><text x="570" y="202">strb x8 ... (15)</text>
  </g>
  <text x="10" y="238" fill="currentColor">초록: 바이트 접근이 워드 접근 1~2개로 합쳐짐. 노랑: 바이트 단위 그대로.</text>
  <text x="10" y="259" fill="currentColor">M0 는 비정렬 LDR 자체가 HardFault 라 합칠 수 없다.</text>
  <text x="10" y="280" fill="currentColor">M4 의 64비트는 LDRD/STRD 를 쓰지 않는다 — 비정렬 LDRD/STRD 는 ARMv7-M 에서도 fault 이기 때문.</text>
</svg>
```

관찰 정리:

1. **Cortex-M4 / arm64**: 32비트 get/put 은 `ldr+rev` / `rev+str` — `__builtin_bswap32` + 캐스트 로드와 똑같다. ARMv7-M 은 단일 `LDR/STR/LDRH/STRH` 의 비정렬 접근을 하드웨어로 허용하므로(`CCR.UNALIGN_TRP=0` 기본) 컴파일러가 합쳐도 안전하다. 바이트 조립은 "느린 이식성 코드" 가 아니다.
2. **`get_le64` 가 `ldrd` 가 아니라 `ldr` 두 개**: `LDRD/STRD/LDM/STM` 은 ARMv7-M 에서도 워드 정렬이 필요하다. `p` 의 정렬을 모르는 컴파일러는 정렬 요구가 없는 단일 `LDR` 만 고른다. 반대로 `*(uint64_t *)p` 로 캐스트하면 컴파일러는 8바이트 정렬을 **가정**하고 `ldrd` 를 낼 수 있다 → 비정렬이면 UsageFault (C-M04 에서 packed 와 함께 직접 본다).
3. **M4 의 `put_be64` 는 합쳐지지 않았다 (솔직한 관찰)**: 32비트 두 번(`rev+str` x2, 5명령)이면 될 것을 clang 21 은 `strb` 8개(15명령)로 남겼다. LLVM 의 연속 store 병합은 "바이트들이 하나의 레지스터 폭 값에서 왔는지" 를 보는데, 32비트 타깃에서 64비트 값은 레지스터 2개로 쪼개져 있어 패턴을 놓친다. arm64 에서도 서로 다른 두 32비트 값을 연달아 `put_be32` 하면 똑같이 `strb` 8개가 남는 것을 확인했다. 35바이트 패킷에서 몇 사이클 차이라 보통 무시하지만, 고속 경로라면 측정 후 판단한다 (GCC 는 이 Mac 에 없어 확인하지 않았다).
4. **Cortex-M0 (ARMv6-M)**: 모든 비정렬 접근이 HardFault 라 컴파일러는 바이트 로드/저장을 그대로 둔다 — `REV` 명령이 있어도 쓸 수 없다. 같은 C 코드가 타깃마다 **올바른** 코드로 바뀐다는 것이 시프트 방식의 진짜 장점이다.
5. `-mno-unaligned-access` 를 주면 M4 의 `put_be32` 도 `strb` 4개로 바뀌는 것을 확인했다 — 컴파일러가 하드웨어 능력에 맞춰 고른다는 증거.
6. `put_hdr` (sync `EB 90` + type + seq) 는 M4 에서 `movw r2,#37099 ; strh r2,[r0]` — 상수 `0x90EB` 를 LE `strh` 한 번으로 써서 선로 바이트 `EB 90` 을 만든다. 컴파일러가 엔디안까지 계산해 준 것.

### Part B — 부호 있는 필드

선로에는 2의 보수 비트 패턴이 간다. 쓰기는 쉽다: `put_le16(p, (uint16_t)v)` — 부호 → 무부호 변환은 표준이 "2^16 을 더하거나 빼서 범위 안으로" 라고 **정의**한다 (6.3.1.3p2), 그 결과가 정확히 2의 보수 비트다.

읽기가 문제다. `(int16_t)0xFB2E` 는 값 64302 가 int16 범위 밖이므로 **구현 정의 결과 또는 구현 정의 시그널** (6.3.1.3p3). GCC/Clang 은 문서상 mod 2^N 으로 정의하지만, MISRA/CERT 는 이것에 기대지 말라고 한다 (C23 에서야 정수가 2의 보수로 고정됐지만 이 변환 규칙 자체는 여전히 구현 정의로 남아 있다).

```c
return u <= 0x7FFFu ? (int16_t)u : (int16_t)((int32_t)u - 65536);
```

두 갈래 모두 범위 안의 값만 변환한다. 그리고 asm 은 위 표대로 **`sxth` 한 명령** — 이식성 비용 0.

### Part B — float 은 "비트를 옮긴다"

```c
uint32_t u; memcpy(&u, &f, sizeof u); put_le32(p, u);
```

- `*(uint32_t *)&f` 는 strict aliasing 위반(6.5p7). `memcpy` 로 객체 표현(object representation)을 복사하는 것은 명시적으로 허용된다 (6.2.6.1p4). 이 포인터 캐스트 vs union vs memcpy 비교는 C-M13 에서 asm 까지 본다.
- 산술이 없으므로 NaN 페이로드까지 비트 그대로 간다 — 테스트의 임의 비트 1000개(NaN·Inf·비정규 포함)가 그 증거.
- asm: arm64 는 `str s0` (FP 레지스터에서 바로, 정렬 무관), M4 hard-float 는 **`vmov r1,s0 ; str r1,[r0]`**. M4 의 `VSTR` 은 워드 정렬이 필요하고 비정렬이면 fault 라서, 컴파일러는 정수 레지스터로 옮긴 뒤 비정렬 가능한 `STR` 을 쓴다.
- **가정**: `float` 은 IEEE 754 binary32 이고, 바이트 순서가 정수와 같다. C 는 둘 다 보장하지 않는다. Annex F 를 지키면 `__STDC_IEC_559__` 가 정의되는데, 이 Mac 의 clang 은 호스트·`thumbv7em` 모두 이 매크로를 정의하지 않았다 (`clang -dM -E` 로 확인; Linux 에서는 glibc 의 `stdc-predef.h` 가 정의해 준다). 실무에서는 `_Static_assert(sizeof(float) == 4)` 와 테스트 벡터(`1.0f → 00 00 80 3F`)로 고정한다. 옛 ARM FPA 의 `double` 은 두 32비트 워드가 빅 엔디안 순서인 "혼합 엔디안" 이었다 — 정수 엔디안과 float 엔디안이 다를 수 있다는 역사적 증거.

### Part B — 경계 검사 커서와 sticky error

필드 10개를 쓸 때마다 `if (pos + 2 > cap) return ERR;` 를 쓰면 코드가 검사로 뒤덮이고, 하나만 빠뜨려도 버퍼 넘침이다. 대신 커서가 검사를 **한 곳에서** 하고, 실패하면 err 를 세운 채 이후 모든 연산을 무시하게 만든다. 호출자는 마지막에 `if (w.err)` 한 번만 본다 (Rob Pike 의 "Errors are values" 에 나오는 errWriter 와 같은 아이디어).

```c
static inline uint8_t *w_reserve(wcur_t *w, size_t n) {
    if (w->err || n > w->cap - w->pos) { w->err = true; return NULL; }   /* pos <= cap 불변식 */
    uint8_t *p = w->buf + w->pos; w->pos += n; return p;
}
```

| 설계 결정 | 이유 |
|---|---|
| `n > cap - pos` (빼기) | 불변식 `pos <= cap` 이면 `cap - pos` 는 절대 랩하지 않는다. `pos + n > cap` 은 `n = SIZE_MAX` 에서 `pos + n` 이 `pos - 1` 로 랩해 통과 — 테스트 `w_reserve(&w2, SIZE_MAX)` 가 이 변이(mutant)를 잡는다 (실제로 바꿔 돌려 55/56 확인) |
| 실패해도 `pos` 불변 | 실패 시점이 진단에 남는다. "어디까지 썼나" 가 의미를 유지 |
| err 는 sticky | 2바이트가 안 들어간 뒤 1바이트 필드가 "운 좋게" 들어가면 필드 순서가 어긋난 쓰레기 프레임이 된다. 테스트 `w_u8` 후 `pos == 4` 가 이를 확인 |
| `n == 0` 은 성공 | `cap - pos >= 0` 이므로 자연스럽게 통과, 포인터 `buf + pos` 는 끝 다음(one-past-the-end) 이라 합법 (6.5.6p8) |
| 읽기 래퍼는 실패 시 0 반환 | 호출자가 값을 쓰기 전에 `r.err` 를 보면 되므로, 미정 값 대신 결정적인 0 |

Linux 커널의 `seq_buf`, Zephyr 의 `net_buf_simple_add_le32()` (공간 부족 시 assert) 처럼 실무 라이브러리도 "검사는 버퍼 객체 안에서" 라는 같은 방향이다.

### Part C — 고정 바이트 벡터를 한 줄씩 읽기

| 오프셋 | 바이트 | 필드 | 해석 |
|---|---|---|---|
| 0 | `EB 90` | sync, BE | `put_be16(0xEB90)` |
| 2 | `21` | type | IMU |
| 3 | `34 12` | seq, LE | 0x1234 |
| 5 | `1A 00` | len, LE | 26 |
| 7 | `EF CD AB 89 67 45 23 01` | time_us, LE | 0x0123456789ABCDEF |
| 15 | `2E FB` | temp_cC | 0xFB2E = −1234 → −12.34 °C |
| 17 | `74 0E` | vbat_mV | 3700 |
| 19 | `18 FC 02 00 E8 03` | accel x/y/z | −1000, 2, 1000 mg |
| 25 | `80 E6 C5 47` | pressure f32 | 0x47C5E680 = 101325.0 |
| 29 | `EF BE AD DE` | status | 0xDEADBEEF |
| 33 | `CE 65` | CRC, BE | CRC-16/CCITT-FALSE(바이트 0..32) = 0xCE65 |

이 벡터는 C 코드가 아니라 **Python 으로 독립 생성** 했다:

```python
hdr  = struct.pack('>H', 0xEB90) + struct.pack('<BHH', 0x21, 0x1234, 26)
pl   = struct.pack('<QhH3hfI', 0x0123456789ABCDEF, -1234, 3700, -1000, 2, 1000, 101325.0, 0xDEADBEEF)
crc  = binascii.crc_hqx(hdr + pl, 0xFFFF)      # crc_hqx(…, 0xFFFF) == CRC-16/CCITT-FALSE, "123456789" → 0x29b1
```

**왜 왕복 테스트만으로는 부족한가**: encode 와 decode 를 같은 사람이 같은 오해로 짜면 **대칭 버그**는 왕복을 통과한다. 실제로 encode·decode 둘 다에서 `temp` 와 `vbat` 순서를 바꾼 변이를 돌려 보니 `trip_ok == 500` 은 그대로 PASS, 골든 벡터 3개 CHECK 만 FAIL (53/56) 이었다. 반대로 CRC 를 실수로 LE 로 쓰는 변이는 골든과 왕복이 둘 다 잡았다. 그래서 세 가지를 같이 쓴다:

| 테스트 | 잡는 것 | 못 잡는 것 |
|---|---|---|
| 골든 벡터 (독립 오라클) | 명세와 다른 바이트 배치, 대칭 버그 | 벡터에 없는 값 범위 |
| 왕복 500회 (임의 값) | 비대칭 버그, 특정 값(음수, NaN, 0xFFFF…)에서만 나는 버그 | 대칭 버그 |
| 오류 주입 (280 비트 플립, 잘림, 필드 변조) | 검증 누락, 에러 코드 순서, 실패 시 출력 오염 | — |

### Part C — encode: 커서 덕분에 명세가 그대로 코드가 된다

`telem_encode` 의 본문은 위 표를 위에서 아래로 옮겨 적은 것뿐이다 (`w_be16(SYNC); w_u8(TYPE); w_le16(seq); …`). 두 가지만 주의:

- CRC 는 **완성된 바이트** 위에서 계산해야 하므로, 페이로드까지 쓰고 `if (w.err) return 0;` 로 먼저 확인한다. 그렇지 않으면 cap 이 모자란 경우 덜 쓴 버퍼 위에서 CRC 를 계산하게 된다 (버그는 아니지만 의미 없는 일).
- `cap = 34` 테스트: 페이로드까지 33바이트는 들어가고 CRC 2바이트가 안 들어가 0 반환, `fr[34]` 는 `0xCC` 그대로 — "cap 밖에는 한 바이트도 안 쓴다" 를 커서가 보장한다.

### Part C — decode: 믿기 전에 검사하는 순서

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">telem_decode(in, n, t, seq) — 위에서부터 하나라도 실패하면 *t, *seq 는 그대로</text>
  <g stroke="currentColor">
    <rect x="40" y="36" width="300" height="30" rx="6" class="f-mut"/>
    <rect x="40" y="78" width="300" height="30" rx="6" class="f-mut"/>
    <rect x="40" y="120" width="300" height="30" rx="6" class="f-mut"/>
    <rect x="40" y="162" width="300" height="30" rx="6" class="f-mut"/>
    <rect x="40" y="204" width="300" height="30" rx="6" class="f-mut"/>
    <rect x="40" y="246" width="300" height="30" rx="6" class="f-acc"/>
    <rect x="40" y="288" width="300" height="30" rx="6" class="f-ok"/>
    <rect x="40" y="330" width="300" height="30" rx="6" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="52" y="56">n &gt;= 7 ?</text>
    <text x="52" y="98">get_be16(in) == 0xEB90 ?</text>
    <text x="52" y="140">in[2] == 0x21 ?</text>
    <text x="52" y="182">len == 26 ?   (len 을 믿기 전에)</text>
    <text x="52" y="224">n &gt;= 7 + len + 2 ?</text>
    <text x="52" y="266">crc(in, 33) == get_be16(in + 33) ?</text>
    <text x="52" y="308">필드 → 지역 telem_t d</text>
    <text x="52" y="350">*t = d; *seq = s; return 35;</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M190 66 v12"/><path d="M190 108 v12"/><path d="M190 150 v12"/><path d="M190 192 v12"/>
    <path d="M190 234 v12"/><path d="M190 276 v12"/><path d="M190 318 v12"/>
    <path d="M340 51 h80"/><path d="M340 93 h80"/><path d="M340 135 h80"/><path d="M340 177 h80"/>
    <path d="M340 219 h80"/><path d="M340 261 h80"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="350" y="46">아니오</text>
    <text x="430" y="56" font-family="monospace">TELEM_ERR_SHORT</text>
    <text x="430" y="98" font-family="monospace">TELEM_ERR_SYNC</text>
    <text x="430" y="140" font-family="monospace">TELEM_ERR_TYPE</text>
    <text x="430" y="182" font-family="monospace">TELEM_ERR_LEN</text>
    <text x="430" y="224" font-family="monospace">TELEM_ERR_SHORT</text>
    <text x="430" y="266" font-family="monospace">TELEM_ERR_CRC</text>
  </g>
  <text x="10" y="385" fill="currentColor">값싼 검사 → 비싼 검사(CRC) → 해석 → 출력. 공격자/잡음이 만든 len 으로 버퍼 밖을 읽는 일이 원천적으로 없다.</text>
</svg>
```

- **`len` 을 먼저 검증**: 길이 필드는 "선로에서 온 신뢰할 수 없는 숫자" 다. 이 값으로 `memcpy` 나 CRC 범위를 정하기 전에 기대값(또는 상한)과 비교해야 한다. 이것이 Heartbleed 류 버그(길이 필드를 믿고 버퍼 밖을 읽음)의 정확한 반대 습관이다.
- **`n < body + 2u`**: `body <= 7 + 65535` 라 덧셈이 랩할 수 없다. 빼기로 쓰고 싶으면 `n - body < 2` 전에 `n >= body` 를 먼저 확인해야 한다.
- **CRC 통과 전에는 필드를 해석하지 않는다**: 해석 결과를 출력에 흘리지 않기 위해 지역 `d` 에 모은 뒤 한 번에 `*t = d`. 테스트가 `memcmp(&t, &keep, …)` 로 확인한다.
- **280가지 1비트 오류 전부 거부**: sync/type/len 바이트의 플립은 각각의 에러로, 나머지는 CRC 로 잡힌다. CRC-16/CCITT 는 길이 16 이하의 모든 버스트 오류(그래서 모든 1비트 오류)를 검출한다. CRC 의 원리와 테이블 구현은 ALGO-M01/M02, 체크섬과의 검출력 비교는 COMM-B08.
- **CRC 를 BE 로 저장**: 명세가 정한 것이다. CCITT 계열 프로토콜은 상위 바이트 먼저가 흔하고(XMODEM-CRC 등), MAVLink 는 LE 로 보낸다 — "CRC 도 다바이트 정수이니 바이트 순서를 명세에 적어야 한다" 가 요점.

### Part C — packed 구조체 캐스팅(C-M04)과 비교

| 기준 | `packed` 구조체 + 캐스트 | 이 레슨의 get/put 계층 |
|---|---|---|
| 엔디안 | 호스트 순서 그대로 → BE 필드는 따로 스왑 필요 | 함수 이름이 명세 (`get_be16`) |
| 정렬 | 컴파일러 확장, 필드 포인터를 꺼내 쓰면 비정렬 UB | 바이트 접근이라 무관 |
| 이식성 | `__attribute__((packed))` / `#pragma pack` 컴파일러 의존 | 순수 C11 |
| 코드 생성 | 비정렬 로드 (M4 에서 빠름, M0 에서 바이트 로드) | 같은 결과 (위 asm) |
| 검증 | 레이아웃을 `offsetof` / `_Static_assert` 로 확인 | 골든 벡터로 확인 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution            # 56/56 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 56/56, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `p[0] << 24 \| p[1] << 16 …` | `p[0]` 이 int 로 승격 → `0x80` 이상이면 UB. 값은 맞아 보여 테스트로 못 잡음 (demo ①) | `(uint32_t)p[0] << 24` |
| `(uint64_t)(p[0] << 24 \| …) << 32` | int 음수 → uint64 부호 확장 (demo ②) | `(uint64_t)get_be32(p) << 32` |
| `(uint32_t)p[i] << 56` | 시프트 폭 ≥ 32 → UB, -O0/-O2 결과가 다름 (demo ③) | `(uint64_t)p[i] << 56` |
| `*(uint32_t *)(buf + 7)` | 비정렬 + strict aliasing + 호스트 순서 (C-B13), 64비트면 `ldrd` fault | `get_le32(buf + 7)` |
| `memcpy(buf, &t, sizeof t)` 로 송신 | 패딩 6바이트(32 vs 26), 호스트 엔디안, 컴파일러마다 다른 레이아웃 | 필드별 `put_*` |
| `(int16_t)get_le16(p)` | 범위 밖 변환 = 구현 정의 (6.3.1.3p3) | `s16_from_u16()` → asm 은 어차피 `sxth` |
| `*(uint32_t *)&f` 로 float 비트 얻기 | strict aliasing 위반 (C-M13) | `memcpy(&u, &f, 4)` |
| `if (pos + n > cap)` | `n` 이 크면 랩어라운드로 통과 | `if (n > cap - pos)` (불변식 `pos <= cap`) |
| 실패한 쓰기 뒤에 계속 쓰기 | 필드가 밀린 쓰레기 프레임 | sticky err — 실패 후 모든 연산 무시 |
| decode 에서 `len` 필드로 바로 CRC 범위 결정 | 조작된 len → 버퍼 밖 읽기 | len 을 기대값/상한과 먼저 비교 |
| CRC 확인 전에 `*t` 에 필드를 씀 | 실패해도 호출자 구조체가 반쯤 바뀜 | 지역 변수에 모은 뒤 마지막에 대입 |
| 왕복 테스트만 작성 | encode/decode 의 대칭 버그 통과 (실측 53/56 중 왕복은 PASS) | 독립 생성한 골든 벡터 추가 |

## 8. 면접 Q&A

<details>
<summary>Q1. 구조체를 그대로 memcpy 해서 보내면 안 되나요? 양쪽 다 Cortex-M 인데요.</summary>

지금은 동작할 수 있지만 계약이 "C 구조체 레이아웃" 이 된다. ① 패딩: `telem_t` 는 32바이트, 선로는 26바이트 — 패딩 바이트의 값은 미지정(6.2.6.1p6)이라 정보 누설과 CRC 불일치를 만든다. ② 상대가 호스트 PC 툴, Python, 다른 컴파일러 옵션(`-fshort-enums`, packed)이면 레이아웃이 달라진다. ③ 필드 추가 시 버전 호환이 불가능. 명세를 바이트 오프셋 표로 쓰고 필드별로 직렬화하면 이 문제가 전부 사라지고, asm 비용도 거의 없다(Part A 표).

</details>

<details>
<summary>Q2. htonl/ntohl 을 쓰면 되지 않나요? get_be32 와 뭐가 달라요?</summary>

`ntohl` 은 "이미 메모리에서 읽은 32비트 값" 을 바꾼다. 그 값을 읽는 단계(`*(uint32_t *)p`)에서 정렬·aliasing 문제가 그대로 남는다. 또 호스트 값과 네트워크 값이 같은 `uint32_t` 타입이라 스왑을 빠뜨리거나 두 번 해도 컴파일러가 모른다. `get_be32(p)` 는 바이트 배열 → 호스트 값 변환을 한 함수로 끝내 "변환 안 된 값" 이 존재하지 않는다. 또 LE 프로토콜에는 `ntohl` 짝이 없고, 64비트 변환(`be64toh`, macOS `OSSwapBigToHostInt64`)은 플랫폼마다 이름과 헤더가 다르다 — 베어메탈 툴체인엔 아예 없기도 하다.

</details>

<details>
<summary>Q3. 바이트를 하나씩 시프트하면 느리지 않나요?</summary>

직접 확인했다: clang -O2 에서 Cortex-M4 의 `get_be32` 는 `ldr + rev`, `put_be32` 는 `rev + str`, `get_le32` 는 `ldr` 하나 — 캐스트 + `__builtin_bswap32` 와 같다. M0 는 비정렬 워드 접근이 불가능하므로 바이트 로드가 **유일하게 올바른** 코드다. 예외: clang 21 이 32비트 타깃에서 64비트 put 의 바이트 저장을 합치지 못했다(M4 `put_be64` 15명령). 핫패스라면 사이클을 측정(DWT->CYCCNT)한 뒤 판단하지만, 35바이트 패킷에선 의미 없는 차이다.

</details>

<details>
<summary>Q4. float 을 선로로 보내도 이식성이 있나요? 꼬리질문: double 은?</summary>

C 표준은 float 형식을 정하지 않는다. 실무 가정은 "IEEE 754 binary32, 정수와 같은 바이트 순서" 이고, 이를 `_Static_assert(sizeof(float) == 4)` + 테스트 벡터(`1.0f → 00 00 80 3F`)로 고정한다. 비트는 `memcpy` 로 `uint32_t` 에 옮겨 정수처럼 직렬화한다 — 산술이 없으니 NaN 페이로드도 보존. double 도 같은 방법(`uint64_t`)이지만 옛 ARM FPA 처럼 워드 순서가 다른 플랫폼이 실제로 있었다. 더 안전하게는 **고정소수점 정수**(예: 0.01 °C 단위 int16)로 보내는 것 — 센서 텔레메트리가 대부분 그렇게 하는 이유이고, 추가 연습 2 가 그 변환(반올림·포화·NaN)이다.

</details>

<details>
<summary>Q5. 직렬화 코드를 어떻게 테스트하나요?</summary>

세 층: ① **독립 오라클이 만든 골든 바이트 벡터** — 다른 언어(Python `struct`), 데이터시트 예시, 와이어샤크 캡처. 대칭 버그를 잡는 유일한 방법(실측: 필드 순서를 양쪽에서 바꾼 변이는 왕복 PASS, 골든 FAIL). ② **왕복** — 고정 시드 PRNG 로 경계값(0, 최대, 음수, NaN) 포함 수백 개. ③ **오류 주입** — 모든 1비트 플립, 모든 잘림 길이, 각 헤더 필드 변조, 그리고 실패 시 출력이 그대로인지. 더 가면 libFuzzer 로 decode 에 임의 바이트를 넣고 ASan/UBSan 으로 크래시 없음을 본다.

</details>

<details>
<summary>Q6. 프로토콜에 필드를 추가해야 하는데 옛 지상국 소프트웨어도 살아 있어요. 어떻게 설계하나요?</summary>

길이 필드를 두고 "아는 필드만 읽고 남은 바이트는 건너뛰는" 디코더(추가 연습 4)로 **앞으로 호환(forward compatibility)** 을 만든다. 규칙: 필드는 끝에만 추가, 의미 변경 금지, `len` 은 상한(예: 200)으로 검증, CRC 는 전체 길이에 대해. 더 큰 변경은 type/버전 바이트를 올린다. 이 설계를 일반화한 것이 TLV, protobuf(varint + 필드 번호, DIST-B06), CBOR 이다.

</details>

<details>
<summary>Q7. 32비트 MCU 에서 time_us 같은 64비트 필드를 ISR 이 갱신하는데, 메인 루프가 그걸 직렬화하면?</summary>

직렬화 헬퍼와 별개로 **읽기 자체가 찢어질(tearing)** 수 있다. M4 에서 64비트 읽기는 `ldr` 두 번이라 그 사이에 ISR 이 들어오면 하위·상위 워드가 서로 다른 시점의 값이 된다 (예: 0x00000001_FFFFFFFF → 0x00000002_00000000 경계). 임계 구역에서 스냅샷을 지역 변수로 복사한 뒤 `put_le64` 한다. 재읽기 루프 등 다른 해법은 C-M14.

</details>

<details>
<summary>Q8. uint8_t 대신 char 버퍼를 쓰면 문제가 있나요?</summary>

`char` 의 부호는 구현 정의다 (x86 에선 signed, ARM AAPCS 에선 unsigned). signed 면 `0xFE` 가 −2 로 읽혀 `(uint32_t)p[1] << 8` 이 `0xFFFFFE00` — 부호 확장된 1들이 상위 바이트를 덮는다 (`get_le32` 결과가 엉망). 직렬화 버퍼는 `uint8_t` (또는 `unsigned char`) 로 고정한다. 덤으로 `unsigned char` 는 aliasing 규칙의 예외(6.5p7)라 어떤 객체의 바이트든 합법적으로 볼 수 있다 — 실제 구현에서 `uint8_t` 는 `unsigned char` 의 typedef 다.

</details>

## 9. 추가 연습

### 연습 1 — 가변 폭 get/put (24·40·48비트)

MAC 주소(48비트 BE), 24비트 ADC 샘플, 40비트 타임스탬프처럼 2의 거듭제곱이 아닌 폭이 흔하다. `get_uint_le(p, n)`, `get_uint_be(p, n)`, `put_uint_be(p, v, n)` (n = 1..8) 과 24비트 2의 보수 → int32 `s24_from_u24()` 를 작성하고, n = 2/4/8 에서 고정 폭 헬퍼와 같은 값인지 검증하라.

<details>
<summary>답 보기</summary>

BE 는 앞에서부터 `v = (v << 8) | p[i]` 로 밀어 넣고, LE 는 **뒤에서부터** 같은 식을 쓰면 된다 (`for (i = n; i-- > 0;)`). `v` 가 `uint64_t` 라 승격 문제가 없다. put 은 끝(LSB)부터 `p[i] = (uint8_t)v; v >>= 8;`. 부호 확장은 Part B 와 같은 원리: `(u & 0x800000) ? (int32_t)u - 0x1000000 : (int32_t)u`. 24비트 부호 확장 자체의 다른 방법(시프트 트릭)은 C-B13 연습 2 참고.

</details>

### 연습 2 — float °C → 0.01 °C int16 (반올림 · 포화 · NaN)

`int16_t degc_to_cC(float degC)` — 0에서 먼 쪽으로 반올림(25.375 → 2538, −0.125 → −13), ±327.67 °C 를 넘으면 ±32767 로 포화, NaN 이면 `INT16_MIN` (0x8000 = "값 없음" 예약). `<math.h>` 없이.

<details>
<summary>답 보기</summary>

**범위 검사를 변환보다 먼저**: 범위 밖 float → 정수 변환은 구현 정의가 아니라 **UB** 다 (6.3.1.4p1). Inf 와 1000 °C 도 이 검사에서 걸린다. NaN 은 `x != x` 로 검출(모든 비교가 거짓). 반올림은 `x + 0.5f` 대신 `t = (int32_t)x; frac = x - t;` 로 — `0.49999997f + 0.5f` 는 float 반올림 때문에 1.0f 가 되는 고전 버그가 있다. |x| < 2^15 라 `x - t` 는 정확하다. −32768 을 포화값으로 쓰지 않고 "값 없음" 센티널로 예약하는 것은 J1939 의 `0xFF…` = not available 같은 차량·산업 프로토콜 관례와 같은 발상이다.

</details>

### 연습 3 — 스트림 재동기화(resync) 스캐너

UART 로 들어온 바이트 스트림 `쓰레기 3B + 프레임(seq 1) + 1비트 손상 프레임(seq 2) + 가짜 "EB 90" + 프레임(seq 3) + 앞 20바이트만 온 프레임(seq 4)` 에서 좋은 프레임을 모두 찾는 `scan_frames()` 를 작성하라. 버린 바이트 수도 센다.

<details>
<summary>답 보기</summary>

각 위치에서 `telem_decode` 를 시도해 성공하면 반환값(35)만큼, 실패하면 **1바이트만** 전진한다. 손상 프레임을 35바이트 통째로 건너뛰면 안 되는 이유: 실패한 "프레임" 이 사실은 가짜 sync 에서 시작한 것일 수 있고, 진짜 프레임이 그 안쪽에서 시작할 수 있다. 끝에서 `TELEM_ERR_SHORT` 인데 sync 가 맞으면 버리지 말고 멈춘다 — 나머지가 다음 수신에서 온다 (링 버퍼에 남겨 둠). 결과: 좋은 프레임 2개(seq 1, 3), 버린 바이트 3 + 35 + 2 = 40. CRC 가 sync 우연 일치(2^-16)를 걸러 주므로 sync 를 2바이트로 둔 것도 설계의 일부다. 프레이머 상태기계 버전은 RF-B08, COMM-B10.

</details>

### 연습 4 — 앞으로 호환 디코더

펌웨어 v2 가 페이로드 끝에 4바이트를 추가해 `len = 30` 을 보낸다. 엄격한 `telem_decode` 는 `TELEM_ERR_LEN` 으로 거부한다. `telem_decode_v()` 를 만들어 `26 <= len <= 200` 이면 아는 필드만 읽고 39바이트를 소비하게 하라.

<details>
<summary>답 보기</summary>

바뀌는 곳은 두 줄: `len` 검사를 `len < 26 || len > TELEM_MAX_PAYLOAD` 로, 그리고 CRC 범위와 반환값을 `7 + len (+2)` 로. 아는 필드는 고정 오프셋이라 그대로 읽는다. **상한이 반드시 있어야 한다** — `len = 0xFFFF` 를 허용하면 수신 버퍼(보통 수백 바이트)를 넘는 길이를 기다리며 스트림이 멈추거나, 길이를 믿은 코드가 버퍼 밖을 읽는다. 테스트는 25 와 0xFFFF 둘 다 `TELEM_ERR_LEN` 인지 본다.

</details>

### 연습 5 — double 을 빅 엔디안으로

지상국 프로토콜이 위도/경도를 IEEE 754 binary64 BE 로 요구한다. `put_be_f64` / `get_be_f64` 를 작성하고 `1.0 → 3F F0 00 00 00 00 00 00`, `π → 40 09 21 FB 54 44 2D 18` 을 확인하라.

<details>
<summary>답 보기</summary>

`uint64_t u; memcpy(&u, &d, 8); put_be64(p, u);` — float 과 같은 방법에 폭만 64. 1.0 은 부호 0, 지수 1023 = `0x3FF`, 가수 0 → `0x3FF0000000000000`. π 의 비트 `0x400921FB54442D18` 은 외워 둘 만한 테스트 벡터다. (참고: Cortex-M4F 의 FPU 는 단정밀도만 있어 double 연산은 소프트웨어지만, 비트 이동은 정수 연산이라 상관없다.)

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 21/21 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o pr_san && ./pr_san   # 21/21, 보고 없음
```

## 10. 핵심 정리

- 선로 형식은 **바이트 오프셋 표** 다. 구조체가 아니다. 필드마다 `get/put_{le,be}{16,32,64}` 중 명세에 적힌 이름을 고르면 호스트 엔디안·정렬과 무관해진다.
- get 은 **시프트 전에 결과 폭 무부호로 캐스트**: `(uint32_t)p[0] << 24`, 64비트는 `(uint64_t)get_be32(p) << 32`. 틀리면 int 오버플로 UB(값은 맞아 보임) / 부호 확장 / 시프트 폭 초과.
- 부호 필드: 쓰기는 `(uint16_t)v` (정의됨), 읽기는 `u <= 0x7FFF ? u : u - 65536` (구현 정의 회피) → asm 은 `sxth`.
- float: `memcpy` 로 비트를 `uint32_t` 에 옮겨 정수처럼. IEEE 754·바이트 순서 가정은 `_Static_assert` + 테스트 벡터로 고정.
- 커서 + sticky error: 검사는 한 곳, `n > cap - pos` (랩 없음), 실패 후 모든 연산 무시, 마지막에 `err` 한 번.
- decode 순서: 길이 → sync → type → **len 검증** → 전체 길이 → CRC → 지역 변수로 해석 → 마지막에 출력.
- 테스트 3종: 독립 생성 골든 벡터(대칭 버그) + PRNG 왕복(값 범위) + 오류 주입(검증 누락).
- 컴파일러는 바이트 조립을 M4/arm64 에서 `ldr+rev` / `rev+str` 로, M0 에서는 바이트 접근으로 만든다. 64비트 put 은 32비트 타깃에서 안 합쳐질 수 있다(clang 21 실측).

## 참고자료

- C11 (N1570) 6.3.1.1 정수 승격, 6.3.1.3 정수 변환(p2 무부호 mod, p3 구현 정의), 6.3.1.4p1 float→정수 범위 밖 UB, 6.5.7 시프트(p3 폭, p4 signed 오버플로), 6.2.6.1 객체 표현과 memcpy, 6.5p7 aliasing — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- Rob Pike, ["The byte order fallacy"](https://commandcenter.blogspot.com/2012/04/byte-order-fallacy.html) / ["Errors are values"](https://go.dev/blog/errors-are-values) (sticky error writer)
- [CRC RevEng catalogue — CRC-16/IBM-3740 (= CCITT-FALSE): width 16, poly 0x1021, init 0xFFFF, check 0x29B1](https://reveng.sourceforge.io/crc-catalogue/16.htm)
- [Python `struct`](https://docs.python.org/3/library/struct.html), [`binascii.crc_hqx`](https://docs.python.org/3/library/binascii.html#binascii.crc_hqx) — 골든 벡터 생성에 사용
- Linux `include/linux/unaligned.h` (`get_unaligned_be32`, `put_unaligned_le16`), Zephyr `sys/byteorder.h` (`sys_get_be32`, `sys_put_le16`)
- [ARMv7-M Architecture Reference Manual](https://developer.arm.com/documentation/ddi0403/latest/) — A3.2 비정렬 접근 (LDR/STR 허용, LDRD/STRD/LDM/VSTR 는 fault), REV/SXTH
- [MAVLink serialization](https://mavlink.io/en/guide/serialization.html) — LE 필드 + CRC 의 실제 텔레메트리 프로토콜
- 관련 레슨: C-B02 (승격), C-B12 (패딩), C-B13 (엔디안·read_be32·REV), C-M04 (packed 구조체·비정렬 asm), C-M13 (strict aliasing·memcpy punning), C-M14 (64비트 tearing), ALGO-M01 (CRC 원리), COMM-B10 (길이 prefix 프레임), DIST-B06 (varint/TLV)
