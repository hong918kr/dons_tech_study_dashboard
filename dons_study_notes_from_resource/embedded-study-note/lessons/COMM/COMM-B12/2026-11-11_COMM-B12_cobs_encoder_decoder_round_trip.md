---
id: COMM-B12
time: 60분
updated: 2026-11-11
---
# COMM-B12 COBS 인코더/디코더 + 랜덤 왕복 테스트

## 학습 목표

- COBS(Consistent Overhead Byte Stuffing)의 규칙 — "코드 바이트 = 다음 0 까지의 거리", 0xFF = 0 없는 254 바이트, 끝의 가상 0 — 을 말로 설명하고, 코드 자리를 먼저 비워 두고 나중에 채우는 **한 번 훑기(single pass)** 인코더와 경계 검사가 들어간 디코더를 정적 버퍼만으로 짠다.
- 최악 길이 **n + max(1, ⌈n/254⌉)** (+ 구분자 1) 를 증명하고, n ≤ 254 이면 내용과 상관없이 **정확히 n + 1** 이라는 것을 254/255/508 경계 테스트로 확인한다. 이 숫자로 TX/RX 버퍼 크기를 컴파일 시간에 정한다.
- 손상된 입력(블록 안의 0x00, 끝 너머를 가리키는 코드 바이트, 빈 입력, 출력 용량 부족)을 구분된 오류 코드로 거부하고, **COBS 구조가 못 보는 손상**(데이터 바이트 변경)이 있어서 CRC 가 따로 필요함을 실측으로 보인다.
- UART RX ISR 에 바이트 하나씩 넣는 **스트리밍 디코더**를 짜고, 0x00 하나로 재동기화(resync)되는 것을 고장 주입 스트림(빠진 바이트, 깨진 바이트, 쓰레기, 너무 긴 프레임)으로 검증한다.
- SLIP(COMM-B11)과 오버헤드를 실측 표로 비교하고, 언제 어느 쪽을 고를지 말한다.

## 1. 문제

바이트 스트림(UART, USB CDC, RS-485)에 패킷을 올리는 프레이밍은 펌웨어 면접의 단골이고, bring-up 에서 "로그는 나오는데 패킷이 가끔 하나씩 사라진다"를 잡는 사람이 바로 프레이밍을 이해하는 사람이다. COBS 는 "구현 20줄 + 경계 조건 3개" 라서 화이트보드 코딩에 딱 맞는 크기다.

문제는 이것이다. 수신기는 끊임없이 들어오는 바이트에서 **프레임이 어디서 시작하고 끝나는지** 알아야 한다. 길이 필드(COMM-B10)는 길이 바이트 하나가 깨지면 그 뒤가 줄줄이 어긋난다. 구분자(delimiter) 바이트를 쓰면 언제든 다음 구분자에서 다시 맞출 수 있지만, payload 안에 같은 값이 나오면 안 된다. 그래서 바이트 스터핑(byte stuffing)으로 payload 에서 구분자 값을 없앤다.

- **SLIP**(RFC 1055, COMM-B11): 0xC0 을 `DB DC` 두 바이트로 바꾼다. 데이터가 운 나쁘면 크기가 **2배**가 된다 — 최악 2n + 2.
- **COBS**(Cheshire & Baker, 1997/1999): 0x00 을 없애는데, 0 하나하나를 "다음 0 까지 거리" 로 바꿔서 늘어나지 않는다. 늘어나는 것은 0 없는 254 바이트마다 1 바이트 + 프레임당 1 바이트뿐 — 최악도 **n + ⌈n/254⌉** (n ≥ 1).

```svg
<svg viewBox="0 0 740 256" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b12a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">데이터 11 22 00 33 → COBS 03 11 22 02 33 + 구분자 00</text>
  <text x="10" y="60" fill="currentColor">원본 (+가상 0)</text>
  <text x="10" y="130" fill="currentColor">COBS 인코딩</text>
  <g stroke="currentColor" fill="none">
    <rect x="210" y="40" width="64" height="30"/><rect x="290" y="40" width="64" height="30"/>
    <rect x="370" y="40" width="64" height="30" class="f-warn"/><rect x="450" y="40" width="64" height="30"/>
    <rect x="530" y="40" width="64" height="30" stroke-dasharray="4 3"/>
    <rect x="130" y="110" width="64" height="30" class="f-acc"/><rect x="210" y="110" width="64" height="30"/>
    <rect x="290" y="110" width="64" height="30"/><rect x="370" y="110" width="64" height="30" class="f-acc"/>
    <rect x="450" y="110" width="64" height="30"/><rect x="530" y="110" width="64" height="30" class="f-mut"/>
    <line x1="242" y1="70" x2="242" y2="110"/><line x1="322" y1="70" x2="322" y2="110"/><line x1="482" y1="70" x2="482" y2="110"/>
    <line x1="402" y1="70" x2="402" y2="110" stroke-dasharray="4 3"/><line x1="562" y1="70" x2="562" y2="110" stroke-dasharray="4 3"/>
    <path d="M162 142 Q282 200 402 142" marker-end="url(#b12a)"/>
    <path d="M402 142 Q482 190 562 142" marker-end="url(#b12a)"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="242" y="60">11</text><text x="322" y="60">22</text><text x="402" y="60">00</text><text x="482" y="60">33</text><text x="562" y="60">(00)</text>
    <text x="162" y="130">03</text><text x="242" y="130">11</text><text x="322" y="130">22</text>
    <text x="402" y="130">02</text><text x="482" y="130">33</text><text x="562" y="130">00</text>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="282" y="196">03: 3칸 뒤가 0</text>
    <text x="490" y="200">02: 2칸 뒤 = 끝</text>
  </g>
  <text x="604" y="130" fill="currentColor">← 구분자</text>
  <text x="604" y="60" fill="currentColor">← 가상의 0</text>
  <text x="10" y="230" fill="currentColor">진한 칸 = 코드 바이트 (블록 길이 + 1)</text>
  <text x="10" y="248" fill="currentColor">0 은 코드 바이트로 바뀌어 사라지고, 데이터 바이트는 그대로 복사된다</text>
</svg>
```

규칙 (이 레슨에서 쓰는 정의 — Cheshire & Baker 원 논문과 같은 "기본 COBS"):

1. 데이터 끝에 **가상의 0** 이 하나 있다고 생각한다. 그러면 데이터 = "0 아닌 바이트 묶음 + 0" 의 반복이다.
2. 각 묶음을 `[코드 c][0 아닌 바이트 c−1 개]` 블록으로 쓴다. c ∈ 1..0xFF. c < 0xFF 이면 블록 뒤에 0 이 하나 있었다는 뜻.
3. 0 없는 바이트가 254 개 넘게 이어지면 254 개에서 끊고 **c = 0xFF** 블록을 쓴다. 이 블록 뒤에는 0 이 없다.
4. 디코더는 마지막 블록 뒤의 0(= 1번의 가상 0)은 내놓지 않는다. 인코딩 결과에는 0x00 이 하나도 없으므로, 그 뒤에 0x00 을 **프레임 구분자**로 붙인다.

### Part A — 인코더/디코더와 예제 벡터

`cobs_encode(src, n, dst, cap)` 와 `cobs_decode(src, n, dst, cap)` 를 짠다. 동적 할당 없음, 출력 용량 `cap` 을 넘으면 `COBS_ERR_OVERFLOW`. 구분자 0x00 은 함수 밖(프레임 계층)에서 붙인다. 검증:

- 빈 입력(`01`)과 Wikipedia 의 COBS 예제 11개 (`00` → `01 01`, `11 22 00 33` → `03 11 22 02 33`, 254·255 바이트 예제들)를 바이트 단위로 맞춘다.
- 다른 방식(다음 0 까지 먼저 세는 두 단계)으로 짠 **기준 인코더**와 출력이 바이트까지 같은지, 길이 0~1000 랜덤 버퍼 4000개(0 비율 4가지: 균등, 절반, 없음, 전부)로 왕복(round trip)한다.

### Part B — 경계·최악 길이·손상 입력

- `cobs_max_encoded(n)` = n + max(1, ⌈n/254⌉). 0 없는 데이터가 정확히 이 값에 닿는지 길이 1/253/254/255/508/509/762/1000 에서, 그리고 `cap` 이 1 모자라면 `COBS_ERR_OVERFLOW` 인지 확인한다.
- n ≤ 254 면 어떤 데이터든 정확히 n + 1. 0 하나의 위치를 **전수**(n = 1..520, 모든 위치 — 13만 5460 경우)로 돌려 254/255/508 경계를 다 지나가게 한다.
- 손상 입력 거부: 빈 입력(`COBS_ERR_EMPTY`), 코드 자리나 블록 안의 0x00(`COBS_ERR_ZERO`), 끝 너머를 가리키는 코드(`COBS_ERR_TRUNC`), 출력 용량 부족. 랜덤 바이트 2만 개를 디코더에 넣어(퍼징 맛보기, 본격은 TEST-M02) 죽지 않는지 ASan/UBSan 으로 본다.

### Part C — UART 링크: CRC 붙인 프레임 + 바이트 단위 수신기

- `frame_build(payload, n, out, cap)`: 선로 형식은 `COBS(payload ‖ CRC16_hi ‖ CRC16_lo) ‖ 0x00`. CRC 는 CRC-16/CCITT-FALSE(poly 0x1021, init 0xFFFF, 반사 없음, xorout 0, check 0x29B1), payload 만 덮고 **상위 바이트 먼저**. TX payload 최대 254.
- `cobs_rx_feed(rx, byte)`: UART RX ISR 이 바이트마다 부르는 스트리밍 디코더. 디코딩된 바이트를 정적 버퍼(최대 payload 64 + CRC 2)에 바로 쌓고, 0x00 에서 판정한다: 빈 프레임(무시), 넘침, 블록 중간에 끊김(COBS 오류), 2 바이트 미만, CRC 오류, OK. 통계 카운터 `frames_ok, cobs_err, crc_err, overflow, short_err, empty`.
- 테스트: 이름 붙인 시나리오(빈 프레임 3개, 0 이 섞인 payload, 바이트 하나 빠짐, 바이트 하나 깨짐, 100 바이트 payload, 쓰레기, `01 00`, 빈 payload, 64/65 바이트 경계) + 랜덤 고장 스트림 3000 프레임. **멀쩡한 프레임은 하나도 빠짐없이, 순서대로, 내용 그대로** 나와야 한다.

## 2. Problem (English)

You are designing the packet layer for a sensor board that talks to a host over a 921600-baud UART. Use COBS framing with 0x00 as the delimiter.

```c
int    cobs_encode(const uint8_t *src, size_t n, uint8_t *dst, size_t cap); /* returns encoded length or COBS_ERR_OVERFLOW */
int    cobs_decode(const uint8_t *src, size_t n, uint8_t *dst, size_t cap); /* returns decoded length or <0:
                                                         COBS_ERR_EMPTY, COBS_ERR_ZERO, COBS_ERR_TRUNC, COBS_ERR_OVERFLOW */
size_t cobs_max_encoded(size_t n);                     /* tight worst-case encoded size, delimiter excluded */
int    frame_build(const uint8_t *payload, size_t n, uint8_t *out, size_t cap);  /* COBS(payload | CRC16-BE) | 0x00 */
int    cobs_rx_feed(cobs_rx *rx, uint8_t byte);        /* called from the RX ISR, one byte at a time */
```

Constraints: no heap; every function bounds-checks its output; the decoder must never read past `src + n` whatever the input; the receiver must recover at the next 0x00 after any corruption and must not deliver a damaged frame.

Examples (encoded bytes, delimiter not shown):

```text
encode([])                    -> 01
encode(00)                    -> 01 01
encode(11 22 00 33)           -> 03 11 22 02 33
encode(01 02 .. FE)  (254 B)  -> FF 01 02 .. FE                 (255 B, no trailing block)
encode(01 02 .. FF)  (255 B)  -> FF 01 .. FE 02 FF              (257 B)
encode(02 03 .. FF 00) (255B) -> FF 02 .. FF 01 01              (257 B)
decode(05 11 22)              -> COBS_ERR_TRUNC
frame_build("123456789")      -> 0C 31 32 33 34 35 36 37 38 39 29 B1 00
```

Follow-ups: What is the exact worst-case size, and why does it matter more than the average? Why is a CRC still needed? How does the receiver resynchronise? Can you encode byte-by-byte straight into the UART TX register? COBS vs SLIP — when would you pick SLIP?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여섯 군데: Part A `cobs_encode`, `cobs_decode`(Part B 의 오류 검사도 여기), Part B `cobs_max_encoded`, Part C `frame_build`, `rx_put`, `cobs_rx_feed`. CRC, 기준 인코더 `ref_encode`, 랜덤 채우기, 하네스는 주어진다. 스텁은 0 이나 `RX_NONE` 을 돌려주므로 멈추거나 죽지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:160  enc_is(NULL, 0, (const uint8_t[]){0x01}, 1)
FAIL starter.c:161  enc_is((const uint8_t[]){0x00}, 1, (const uint8_t[]){0x01, 0x01}, 2)
…
random round trip: 4000 buffers (len 0..1000, 4 zero densities), bad=4000
FAIL starter.c:200  cobs_max_encoded(0) == 1
…
single-zero placements: 135460 cases (n=1..520), bad=135460
decoder fuzz: 20000 random inputs, accepted=20000, bad=20000
…
scenario: ok=0 cobs=0 crc=0 overflow=0 short=0 empty=0  events:
fault stream: sent ok=3000 (delimiter-lost rescued 0) damaged=0 recv=0 mismatch=0
              stats ok=0 cobs=0 crc=0 ovf=0 short=0 empty=0
FAIL starter.c:373  g_mism == 0 && g_nrecv == nsent && (int)g_rx.st.frames_ok == nsent
2/53 passed
```

(exit 1. 통과한 2개는 주어진 CRC 의 check 값과, 스텁의 0 이 우연히 맞는 "`01` 을 디코딩하면 길이 0".)

## 4. 힌트

<details><summary>힌트 1 — 코드 바이트는 블록이 끝나야 안다: 자리부터 잡아 둔다</summary>

블록의 코드는 블록 **앞**에 오지만, 값은 블록이 끝나야(0 을 만나거나 254 개가 차야) 안다. 두 가지 방법: (1) 다음 0 까지 먼저 세고 쓴다(앞을 내다봄, 하네스의 `ref_encode`), (2) 출력에 코드 자리 `code_at` 을 비워 두고 데이터를 계속 쓰다가 블록이 끝나면 `dst[code_at] = code` 로 채운다(되돌아가 채우기). (2) 가 입력을 한 번만 읽는다.

</details>

<details><summary>힌트 2 — code 는 "지금까지 넣은 데이터 수 + 1" 로 시작값 1</summary>

블록을 열 때 `code = 1`. 0 아닌 바이트를 쓸 때마다 `code++`. 0 을 만나면 블록을 닫고(코드 채우기) 새 자리를 연다 — 0 자체는 출력에 안 쓴다. `code` 가 0xFF 가 되면(데이터 254 개) 0 없이 닫고 새 자리를 연다. 단, **그게 입력의 마지막 바이트였다면 새 자리를 열지 않는다** (7. 실수 1).

</details>

<details><summary>힌트 3 — 디코더: 코드 c 를 읽고, c−1 개 복사, 그다음 0 을 넣을지 결정</summary>

0 을 넣는 조건은 두 개다: `c != 0xFF` 그리고 **아직 입력이 남았을 때**(마지막 블록 뒤의 0 은 가상의 0). 복사 전에 `c − 1 > 남은 입력` 이면 `COBS_ERR_TRUNC` — 이 검사가 없으면 `05 11 22` 같은 3 바이트 입력에 4 바이트를 읽는다(ASan 이 바로 잡는다).

</details>

<details><summary>힌트 4 — 최악 길이: "늘어나는 코드 바이트"만 센다</summary>

0 하나는 코드 바이트 하나로 바뀌니 길이가 그대로다. 늘어나는 것은 (a) 마지막 블록의 코드(가상의 0 몫) 1 개, (b) 뒤에 블록이 더 있는 0xFF 블록마다 1 개. (b) 하나가 데이터 254 바이트를 먹고 그 뒤에 바이트가 적어도 하나 더 있으니 개수는 ⌊(n−1)/254⌋ 이하.

</details>

<details><summary>힌트 5 — 스트리밍 디코더는 0 을 "미뤄서" 넣는다</summary>

블록이 끝났을 때 0 을 바로 넣으면, 그 블록이 마지막이었을 때 가상의 0 까지 들어간다. 바이트 하나씩 받는 쪽은 "이게 마지막 블록인지"를 구분자 0x00 이 와야 안다. 그래서 `zero_pending` 플래그만 세워 두고, **다음 코드 바이트가 오면** 그때 0 을 넣는다. 0x00 이 먼저 오면 플래그는 그냥 버린다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "구분자 프레이밍이 필요한 이유 → 0x00 을 없애는 방법 → COBS 는 0 을 거리 정보로 바꾼다" 를 한 문장씩.
2. 예 하나(`11 22 00 33` → `03 11 22 02 33 00`)를 손으로 인코딩해 보이고, 규칙 세 개(코드 = 거리, 0xFF = 0 없는 254, 끝의 가상 0)를 말한다.
3. 코드를 쓰고 254/255 경계를 바로 언급한다 — 면접관이 기다리는 지점이다.
4. 최악 길이와 버퍼 크기, 손상 입력 처리, "COBS 는 오류 검출이 아니다 → CRC" 로 마무리.

### Part A — 인코더: 코드 자리를 먼저 비워 두고 나중에 채운다

상태는 셋: `code_at`(아직 안 채운 코드 바이트 위치), `o`(다음에 쓸 위치), `code`(현재 블록의 코드 = 넣은 데이터 수 + 1). `11 22 00 33` 을 따라가 보면:

| 단계 | 입력 | 동작 | code_at | o | code | dst |
|---|---|---|---|---|---|---|
| 시작 | — | 0 번 자리 예약 | 0 | 1 | 1 | `?` |
| i=0 | 11 | 복사, code++ | 0 | 2 | 2 | `? 11` |
| i=1 | 22 | 복사, code++ | 0 | 3 | 3 | `? 11 22` |
| i=2 | 00 | `dst[0]=03`, 3 번 자리 예약 | 3 | 4 | 1 | `03 11 22 ?` |
| i=3 | 33 | 복사, code++ | 3 | 5 | 2 | `03 11 22 ? 33` |
| 끝 | — | `dst[3]=02` (가상의 0) | — | 5 | — | `03 11 22 02 33` |

핵심 코드 (solution.c):

```c
for (size_t i = 0; i < n; i++) {
    if (src[i] != 0) {
        if (o >= cap) return COBS_ERR_OVERFLOW;
        dst[o++] = src[i];
        if (++code != 0xFF) continue;      /* 블록이 아직 안 찼다 */
        if (i + 1 == n) { dst[code_at] = 0xFF; return (int)o; }   /* 254 바이트로 딱 끝: 새 블록 없음 */
    }
    dst[code_at] = code;                   /* 0 을 만났거나, 254 바이트가 찼고 데이터가 더 있다 */
    if (o >= cap) return COBS_ERR_OVERFLOW;
    code_at = o++;
    code = 1;
}
dst[code_at] = code;
```

설계 선택:

- **0 과 "254 개 참" 을 같은 닫기 경로로**: 둘 다 "코드 채우고 새 자리 열기" 다. 다른 점은 0 은 출력에 안 쓰이고 254 개는 0 을 소비하지 않는다는 것뿐.
- **254 로 딱 끝나는 경우만 예외**: 여기서 새 자리를 열면 끝에 `01` 이 붙는다(`FF [254] 01`). 디코더는 이것도 같은 데이터로 풀지만 정규형(canonical)이 아니고, 무엇보다 길이가 최악 공식보다 1 크다 — 공식으로 잡은 정적 버퍼가 넘친다(7. 실수 1).
- **용량 검사 위치**: 데이터를 쓰기 전, 새 코드 자리를 열기 전. `dst[code_at]` 은 이미 확보한 자리라 검사가 필요 없다. 처음 `cap < 1` 하나만 따로.
- 시간 O(n), 추가 메모리 O(1). 입력을 한 번만 읽으므로 플래시에서 바로 읽어도 되고, 하네스의 두 단계 `ref_encode` 와 출력이 바이트까지 같은지로 서로를 검증한다.

### Part A — 디코더

```c
while (i < n) {
    uint8_t code = src[i++];
    if (code == 0) return COBS_ERR_ZERO;
    size_t len = (size_t)code - 1;
    if (len > n - i) return COBS_ERR_TRUNC;     /* 읽기 전에 범위부터 */
    if (len > cap - o) return COBS_ERR_OVERFLOW;
    for (...) { if (src[i] == 0) return COBS_ERR_ZERO; dst[o++] = src[i++]; }
    if (code != 0xFF && i < n) { ...; dst[o++] = 0; }
}
```

- `len > n − i` 는 뺄셈 방향이 중요하다. `i + len > n` 으로 써도 여기선 넘치지 않지만, 습관적으로 "남은 양과 비교" 를 쓰면 size_t 넘침 걱정이 없다.
- 디코딩 결과는 항상 입력보다 짧다(코드 바이트가 최소 1 개 사라짐). 그리고 쓰는 위치 o 가 항상 읽는 위치 i 보다 뒤에 있어서 **dst == src 제자리 디코딩**이 안전하다 — RX 버퍼 하나로 끝난다(연습 3).
- 받아들이는 입력 중 정규형이 아닌 것은 딱 하나의 모양, "마지막 블록이 `01` 이고 바로 앞이 `FF` 블록" 이다. 기본 디코더는 받아주고, 엄격 모드는 연습 2.

### Part B — 최악 길이: n + 1 + m

인코딩 길이를 세어 보자. 데이터에 0 이 z 개 있으면 블록이 끝나는 이유는 셋이다: 0 을 만남(z 번), 0xFF 블록이 차고 데이터가 더 있음(m 번), 데이터 끝(1 번). 블록 수 = z + m + 1 이고, 블록마다 코드 1 바이트, 0 은 출력에 안 쓰이니

**인코딩 길이 = (n − z) + (z + m + 1) = n + 1 + m.**

m 개의 FF 블록은 각각 0 아닌 바이트 254 개를 먹고, 그 뒤에 적어도 한 바이트가 더 있어야 하므로 254·m + 1 ≤ n, 즉 m ≤ ⌊(n−1)/254⌋. 그러면 n ≥ 1 에서 1 + ⌊(n−1)/254⌋ = ⌈n/254⌉ 이고 최악은 **n + ⌈n/254⌉** (n = 0 이면 1). 0 없는 데이터가 m 을 최대로 만들어 이 값에 정확히 닿는다. 그리고 **n ≤ 254 이면 m = 0** 이라 0 이 있든 없든 정확히 n + 1 이다. 이것이 "Consistent Overhead" 의 뜻이다.

```svg
<svg viewBox="0 0 740 296" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">경계 길이에서 블록 모양 (막대 길이는 비례 아님) — 선로 바이트 = 인코딩 + 구분자</text>
  <g fill="currentColor">
    <text x="10" y="58">253 B, 0 없음</text><text x="10" y="103">254 B, 0 없음</text>
    <text x="10" y="148">255 B, 0 없음</text><text x="10" y="193">254 B 뒤에 00</text>
    <text x="10" y="238">508 B, 0 없음</text>
  </g>
  <g stroke="currentColor">
    <rect x="150" y="40" width="34" height="28" class="f-acc"/><rect x="184" y="40" width="150" height="28" fill="none"/><rect x="338" y="40" width="34" height="28" class="f-mut"/>
    <rect x="150" y="85" width="34" height="28" class="f-acc"/><rect x="184" y="85" width="150" height="28" fill="none"/><rect x="338" y="85" width="34" height="28" class="f-mut"/>
    <rect x="150" y="130" width="34" height="28" class="f-acc"/><rect x="184" y="130" width="150" height="28" fill="none"/>
    <rect x="334" y="130" width="34" height="28" class="f-acc"/><rect x="368" y="130" width="34" height="28" fill="none"/><rect x="406" y="130" width="34" height="28" class="f-mut"/>
    <rect x="150" y="175" width="34" height="28" class="f-acc"/><rect x="184" y="175" width="150" height="28" fill="none"/>
    <rect x="334" y="175" width="34" height="28" class="f-acc"/><rect x="368" y="175" width="34" height="28" class="f-acc"/><rect x="406" y="175" width="34" height="28" class="f-mut"/>
    <rect x="150" y="220" width="34" height="28" class="f-acc"/><rect x="184" y="220" width="150" height="28" fill="none"/>
    <rect x="334" y="220" width="34" height="28" class="f-acc"/><rect x="368" y="220" width="150" height="28" fill="none"/><rect x="522" y="220" width="34" height="28" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="167" y="59">FE</text><text x="259" y="59">253 바이트</text><text x="355" y="59">00</text>
    <text x="167" y="104">FF</text><text x="259" y="104">254 바이트</text><text x="355" y="104">00</text>
    <text x="167" y="149">FF</text><text x="259" y="149">254 바이트</text><text x="351" y="149">02</text><text x="385" y="149">x</text><text x="423" y="149">00</text>
    <text x="167" y="194">FF</text><text x="259" y="194">254 바이트</text><text x="351" y="194">01</text><text x="385" y="194">01</text><text x="423" y="194">00</text>
    <text x="167" y="239">FF</text><text x="259" y="239">254 바이트</text><text x="351" y="239">FF</text><text x="443" y="239">254 바이트</text><text x="539" y="239">00</text>
  </g>
  <g fill="currentColor">
    <text x="580" y="59">255 = n+1 +1</text><text x="580" y="104">256 = n+1 +1</text>
    <text x="580" y="149">258 = n+2 +1</text><text x="580" y="194">258 = n+2 +1</text>
    <text x="580" y="239">511 = n+2 +1</text>
  </g>
  <text x="10" y="272" fill="currentColor">254 B: FF 블록으로 딱 끝나면 새 블록을 열지 않는다.</text>
  <text x="10" y="290" fill="currentColor">255 B: FF 블록 뒤에 데이터가 남아 코드가 하나 더 붙는다 (m = 1).</text>
</svg>
```

네 번째 줄(254 바이트 + 00, n = 255)은 0 이 있는데도 최악에 닿는다. FF 블록은 0 을 먹지 않으므로 0 이 다음 블록(`01`)의 몫이 되고, 마지막 가상 0 이 또 `01` 을 만든다 — 0 이 있다고 항상 싸지는 않다. 테스트의 "0 하나 위치 전수"(n = 1..520)가 이런 모양을 모두 지나간다.

버퍼 크기는 컴파일 시간 상수로: payload 64 + CRC 2 = 66 바이트 → 인코딩 최대 67 + 구분자 1 = **68 바이트** (연습 1 의 `COBS_MAX_ENC`). SLIP 이었다면 2·66 + 2 = 134.

### Part B — 손상 입력: 무엇을 거부하고 무엇은 못 보나

| 입력 | 결과 | 이유 |
|---|---|---|
| (빈 입력) | `COBS_ERR_EMPTY` | 정상 인코딩은 최소 1 바이트(`01`) |
| `00` | `COBS_ERR_ZERO` | 코드 자리에 0 |
| `03 11 00` | `COBS_ERR_ZERO` | 블록 안에 0 |
| `02 11 00 01` | `COBS_ERR_ZERO` | 두 번째 코드 자리에 0 |
| `05 11 22` | `COBS_ERR_TRUNC` | 코드가 4 바이트를 약속, 2 바이트뿐 |
| `FF` + 253 바이트 | `COBS_ERR_TRUNC` | 0xFF 는 254 바이트를 약속 |
| `02 11 02 22`, cap 2 | `COBS_ERR_OVERFLOW` | 결과 `11 00 22` 는 3 바이트 |

거부는 "구조가 틀렸다" 만 본다. **데이터 바이트가 다른 0 아닌 값으로 바뀌면 COBS 구조는 절대 모른다** — 블록 길이가 그대로라 디코딩이 성공하고 정확히 그 1 바이트만 다르다(연습 5 에서 2만 9597 번 손상 중 예외 0). 코드 바이트가 바뀐 경우는 대부분(실측 96.9%) TRUNC/ZERO 로 걸리지만 나머지는 디코딩이 성공한다. 그래서 COBS 위에는 CRC 를 올린다 (아래 손상 실험).

퍼징 결과: 랜덤 바이트 2만 개(반은 0 포함 균등, 반은 0 없음) 중 디코더가 받아들인 것은 93 개뿐이었고, 받아들인 것은 모두 다시 인코딩·디코딩해도 같았다. ASan/UBSan 깨끗.

### Part C — frame_build 와 스트리밍 디코더

송신은 단순하다: 스택의 `tmp[TX_PAYLOAD_MAX + 2]` 에 payload 와 CRC(상위 먼저)를 붙이고, 인코딩하고, 구분자 자리를 검사해 0x00 을 붙인다. `"123456789"` → CRC 0x29B1 → 11 바이트, 0 없음 → `0C 31 … 39 29 B1 00`, 13 바이트. 빈 payload 는 CRC 가 0xFFFF(초기값 그대로)라 `03 FF FF 00`.

수신은 ISR 에서 바이트마다 불리므로 프레임 전체를 모았다가 디코딩하지 않고 **들어오는 대로 디코딩**한다. 상태는 `remain`(현재 블록에 남은 데이터 수, 0 이면 다음 바이트가 코드), `zero_pending`, `started`, `overflow`.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b12c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <g stroke="currentColor">
    <rect x="30" y="50" width="190" height="56" rx="10" class="f-acc"/>
    <rect x="300" y="50" width="190" height="56" rx="10" fill="none"/>
    <rect x="560" y="50" width="160" height="56" rx="10" class="f-warn"/>
    <rect x="170" y="190" width="400" height="56" rx="10" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="125" y="74">코드 대기</text><text x="125" y="94">remain = 0</text>
    <text x="395" y="74">블록 안</text><text x="395" y="94">remain 은 1 이상</text>
    <text x="640" y="74">버림</text><text x="640" y="94">buf 넘침</text>
    <text x="370" y="212">0x00 수신 → 판정 후 초기화</text>
    <text x="370" y="232">빈 / 넘침 / 블록 중간(COBS) / 짧음 / CRC / OK</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M220 66 L298 66" marker-end="url(#b12c)"/>
    <path d="M300 92 L222 92" marker-end="url(#b12c)"/>
    <path d="M490 78 L558 78" marker-end="url(#b12c)"/>
    <path d="M90 50 C70 10 160 10 140 48" marker-end="url(#b12c)"/>
    <path d="M125 106 L250 188" marker-end="url(#b12c)" stroke-dasharray="4 3"/>
    <path d="M395 106 L380 188" marker-end="url(#b12c)" stroke-dasharray="4 3"/>
    <path d="M640 106 L500 188" marker-end="url(#b12c)" stroke-dasharray="4 3"/>
    <path d="M170 230 C60 230 40 160 70 108" marker-end="url(#b12c)"/>
  </g>
  <g fill="currentColor">
    <text x="228" y="44">코드 c ≥ 2</text>
    <text x="228" y="124">마지막 데이터</text>
    <text x="500" y="44">버퍼 꽉 참</text>
    <text x="160" y="22">코드 01</text>
    <text x="40" y="262">재동기화 (초기화)</text>
    <text x="590" y="172">점선: 0x00 수신</text>
  </g>
  <text x="10" y="290" fill="currentColor" font-family="monospace">… 00 | 03 11 22 02 33 | 00 | 0C 31 32 … 29 B1 | 00 | …</text>
  <text x="10" y="308" fill="currentColor">코드 바이트가 올 때 zero_pending 이면 0 을 먼저 넣는다.</text>
  <text x="10" y="324" fill="currentColor">0x00 이 오면 zero_pending 은 버린다 (가상의 0).</text>
</svg>
```

`03 11 22 02 33 00` 을 바이트 하나씩:

| 바이트 | 상태(전) | 동작 | buf | remain | zero_pending |
|---|---|---|---|---|---|
| 03 | 코드 대기 | 코드: pending 없음 → remain = 2 | — | 2 | 1 |
| 11 | 블록 안 | 데이터 | 11 | 1 | 1 |
| 22 | 블록 안 | 데이터 | 11 22 | 0 | 1 |
| 02 | 코드 대기 | 코드: **pending → 00 먼저 넣음**, remain = 1 | 11 22 00 | 1 | 1 |
| 33 | 블록 안 | 데이터 | 11 22 00 33 | 0 | 1 |
| 00 | 코드 대기 | 판정: remain 0 → 길이 4, pending 은 **버림** | 11 22 00 33 | — | — |

판정 순서와 이유:

1. `started` 가 아니면 **빈 프레임** — 무시하고 `empty++`. 연속 구분자(`00 00`)는 정상이다. 송신 측이 프레임 앞에도 0x00 을 보내면(앞 구분자) 앞의 쓰레기나 끊긴 프레임이 거기서 잘린다.
2. `overflow` 면 넘침. 넘친 순간부터 이 프레임의 나머지는 0x00 까지 버린다(디코딩도 안 함) — RX 버퍼 크기 = 이 장치가 받는 최대 프레임.
3. `remain != 0` 이면 블록 중간에 끊긴 것 → **COBS 오류**. 스트림에서는 "블록 안의 0" 이 곧 "프레임이 일찍 끝남" 이다. 바이트가 빠졌을 때 가장 흔한 증상.
4. 디코딩이 2 바이트 미만이면 CRC 자리도 없다 → `short_err` (예: `01 00`).
5. CRC 비교 → OK 면 `frame_len = len − 2`, 아니면 `crc_err`.
6. 어느 경우든 상태를 모두 초기화 — **다음 바이트부터 새 프레임**. 이것이 COBS 재동기화의 전부다. 0x00 은 인코딩 안에 절대 없으므로 "여기가 경계" 라는 판단이 틀릴 수 없다.

ISR 안에서 하는 일은 바이트당 비교 몇 개와 쓰기 하나, CRC 는 프레임 끝에서 한 번(66 바이트 × 8 비트 시프트). 921600 baud 에서 바이트 간격은 약 10.9 µs 라 Cortex-M4 에서 여유가 있지만, CRC 를 바이트마다 누적하거나 프레임 완료 후 메인 루프로 넘기는 편이 ISR 을 더 짧게 만든다(Q5).

### Part C — 고장 스트림 결과 (solution.c 실제 출력)

```text
scenario: ok=4 cobs=2 crc=1 overflow=2 short=1 empty=3  events: 1 1 -1 -2 -3 -1 -4 1 1 -3
fault stream: sent ok=2430 (delimiter-lost rescued 16) damaged=570 recv=2430 mismatch=0
              stats ok=2430 cobs=604 crc=275 ovf=0 short=0 empty=2683
```

- 시나리오 이벤트 순서: hello OK, `00 00 01 00` payload OK, 바이트 빠진 프레임 → COBS 오류(블록 중간에 끝남), 데이터 바이트 깨진 프레임 → **CRC 오류**(COBS 구조는 멀쩡), 100 바이트 → 넘침, 쓰레기 `AB CD EF` → COBS 오류(`AB` 가 170 바이트를 약속), `01 00` → 짧음, 빈 payload OK, 64 바이트 OK, 65 바이트 → 넘침.
- 랜덤 3000 프레임: 10% 앞에 쓰레기(0 포함 가능), 10% 바이트 하나 빠짐, 10% 바이트 하나 깨짐(0x00 으로 바뀌어 프레임이 둘로 쪼개지는 경우 포함). 손상 570 개는 하나도 배달되지 않았고, 멀쩡한 2430 개는 모두 순서대로 내용 그대로 나왔다.
- "delimiter-lost rescued 16": 빠진 바이트가 마지막 0x00 이었던 16 번은 프레임 자체는 멀쩡하고, **다음 프레임의 앞 구분자**가 끝을 대신 찍어 줘서 살아났다. 앞 구분자가 없었다면 이 16 개는 다음 프레임과 붙어 둘 다 잃는다. (끝 0x00 을 잃은 프레임 바로 뒤에 쓰레기가 끼면 그 프레임도 잃는다 — 테스트는 이 조합을 만들지 않고 구제 경우만 센다.)
- `empty=2683` 은 앞 구분자 때문에 생긴 빈 프레임 수 — 무해하다.

### 오버헤드 실험 — `demo_overhead.c` (실제 출력 발췌)

선로 바이트 − n. COBS 는 인코딩 + 구분자 1, SLIP 은 앞 END + 이스케이프 + 뒤 END(COMM-B11 축약판). 랜덤 줄은 평균 / 최대 (n ≤ 255 는 10만 번, 그 이상은 2만 번).

```text
data                    n | COBS (+00)      | SLIP (END..END)
uniform random         64 |  2.000 / 2     |   2.497 / 7
uniform random        254 |  2.000 / 2     |   3.980 / 13
uniform random        255 |  2.371 / 3     |   3.990 / 12
uniform random       1000 |  3.945 / 5     |   9.800 / 22
ASCII 0x20-0x7E        64 |  2.000 / 2     |   2.000 / 2
ASCII 0x20-0x7E      1000 |  5.000 / 5     |   2.000 / 2
50% zeros            1000 |  2.000 / 2     |   5.925 / 18
all 0x00             1000 | 2               | 2
all 0xC0               64 | 2               | 66
all 0xC0             1000 | 5               | 1002
no zero (1..255)     1000 |  5.000 / 5     |   9.845 / 24
worst case formula: COBS n + max(1, ceil(n/254)) + 1, SLIP 2n + 2
  n=64    COBS worst 66    (+2)   SLIP worst 130   (+66)
  n=1000  COBS worst 1005  (+5)   SLIP worst 2002  (+1002)
```

읽는 법:

- **n ≤ 254 의 COBS 는 항상 +2** (코드 1 + 구분자 1). 평균도 최대도 같다 — 버퍼 크기·전송 시간 계산이 결정적(deterministic)이다.
- 균등 랜덤 255 바이트의 평균 2.371: 처음 254 바이트에 0 이 하나도 없을 확률 (255/256)^254 ≈ 0.370 일 때만 FF 블록 뒤에 코드가 하나 더 붙는다. 이론과 실측이 맞는다. SLIP 의 1000 바이트 평균 9.800 도 이론 2 + 2·1000/256 = 9.81 과 맞는다.
- SLIP 이 이기는 곳: 0xC0/0xDB 가 없는 데이터(ASCII 텍스트, AT 명령) — 딱 +2. COBS 는 254 바이트마다 1 씩 더 낸다. 0 이 많은 바이너리(작은 정수 센서 값, 구조체 패딩)는 COBS 가 공짜다.
- 차이가 결정적인 곳은 최악: 0xC0 이 가득한 데이터(우연히, 또는 악의적으로)에서 SLIP 은 2배, COBS 는 0.4%.

### 손상 실험 — 한 바이트가 바뀌면 누가 잡나 (`demo_overhead.c` 2부, 실제 출력)

32 바이트 payload + CRC-16 을 COBS 로 싸고, 인코딩된 바이트 하나를 다른 값으로 바꿨다(200만 번).

```text
trials 2000000 (byte -> nonzero 1992128, byte -> 0x00 7872)
  caught by COBS structure (TRUNC/ZERO): 500649 (25.13%)
    of which code-byte hits: 500649 of 516620 code-byte corruptions (96.91%)
  decoded OK structurally, caught by CRC-16: 1491479 (74.87%)
  undetected: 0
  structurally-OK decodes: length changed 0, wrong bytes mean 1.06 max 25 (of 34)
```

COBS 구조가 잡은 것은 전부 코드 바이트 손상이었다. 데이터 바이트 손상(약 74%)은 구조로는 보이지 않고 CRC 만 잡는다. 코드 바이트 손상이 구조 검사를 통과하면 0 의 위치가 옮겨져 **최대 25 바이트**까지 틀린 결과가 나온다 — 오류가 퍼진다(error propagation). 이번 실험에서 CRC-16 이 놓친 것은 0 이지만, 이는 이 200만 번의 결과이지 보장이 아니다 (CRC 의 보장 범위는 COMM-B08). 바이트가 0x00 으로 바뀐 0.4% 는 프레임을 둘로 쪼개고, 스트림 테스트에서 본 것처럼 두 조각 모두 거부된다.

### 복잡도와 RAM

- 인코드/디코드: O(n) 시간, 상태 몇 바이트. 인코더는 출력 버퍼(최악 n + ⌈n/254⌉)가 필요하다 — 코드 바이트가 블록 앞에 오기 때문에 최대 254 바이트를 미리 봐야 한다.
- 스트리밍 수신기: `buf[66]` + 상태 몇 바이트 + 통계 24 바이트. 프레임 버퍼를 따로 두지 않고 디코딩 결과만 저장한다(인코딩된 바이트를 모았다가 제자리 디코딩해도 같은 크기 — 연습 3).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 53/53 passed, exit 0, 약 0.1 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_overhead.c -o demo_overhead && ./demo_overhead   # 약 3 초
```

## 7. 자주 하는 실수

이 레슨 작업 중 solution.c 를 한 군데씩 일부러 틀리게 고쳐(mutation) 테스트를 돌린 실제 결과다.

**1. 254 바이트로 딱 끝날 때 새 블록을 연다.** "코드가 0xFF 가 되면 무조건 닫고 새 자리" 로 짜면 254·k 바이트에서 끝에 `01` 이 붙는다. 디코더는 같은 데이터로 풀어서 왕복 테스트만으로는 안 보이고, 기준 인코더와 바이트 비교에서 걸렸다(랜덤 4000 개 중 11 개, 0 하나 전수 13만 5460 중 278 개, 53 개 중 6 FAIL). 진짜 피해는 길이: n = 254 에서 256 바이트가 나와 최악 공식(255)보다 크다 → `COBS_MAX_ENC` 로 잡은 정적 TX 버퍼를 1 바이트 넘친다. 고친 코드: `if (i + 1 == n) { dst[code_at] = 0xFF; return (int)o; }`.

**2. 디코더가 마지막 블록 뒤에도 0 을 넣는다.** 끝 검사 `i < n` 을 빼면(`if (code != 0xFF)` 만) 마지막 블록이 FF 가 아닌 모든 결과 끝에 0 이 하나 더 붙는다 — 랜덤 4000 개 중 3989 개 틀림. 반대로 `code != 0xFF` 를 빼면(`if (i < n)` 만) 0 없는 254 바이트 구간 뒤마다 가짜 0 이 들어간다 — 4000 개 중 1322 개 틀림. 두 조건 `code != 0xFF && i < n` 이 다 필요하다.

**3. 코드 바이트를 믿고 범위 검사 없이 복사한다.** `len > n − i` 검사를 빼면 `05 11 22` 같은 입력에서 버퍼 끝을 넘어 읽는다. ASan: `stack-buffer-overflow … in cobs_decode`. 코드 바이트는 선로에서 온 **신뢰할 수 없는 길이 필드**다. 펌웨어에서 이 버그는 인접 RAM 을 payload 로 내보내는 정보 누출이 된다.

**4. 스트리밍 디코더가 블록이 끝나자마자 0 을 넣는다.** `zero_pending` 없이 "remain 이 0 이 되면 0 추가" 로 짜면 모든 프레임 끝에 0 이 하나 붙는다. 놀랍게도 **CRC 가 이걸 못 잡는다**: 받은 쪽은 `crc(payload ‖ H)` 와 `L ‖ 00` 을 비교하게 되는데, 이 CRC(반사 없음, xorout 0)에서는 C = crc(payload) = H‖L 일 때 crc(payload ‖ H) = L‖00 이 항상 성립한다 (H 를 넣으면 레지스터가 00‖L 이 되고, 8 번 시프트하는 동안 최상위 비트가 늘 0 이라 다항식이 한 번도 XOR 되지 않는다). 실측: 랜덤 스트림에서 2400 프레임이 **CRC 통과 + 길이 1 큰 틀린 payload** 로 배달됐다(mismatch=2400). 교훈: CRC 는 프레이밍 계층 버그를 보장해 주지 않는다. 고정 test vector(`"123456789"` → `0C … 29 B1 00`)와 보낸 그대로 받았는지 비교하는 왕복 테스트가 필요하다.

**5. 구분자를 프레임 뒤에만 보낸다.** 틀린 건 아니지만, 마지막 0x00 이 빠지면 그 프레임과 다음 프레임이 붙어서 둘 다 잃는다. 앞에도 0x00 을 보내면(비용 1 바이트) 랜덤 스트림의 16 번처럼 살아난다. 수신기는 빈 프레임을 조용히 버리기만 하면 된다.

**6. 넘친 프레임의 나머지를 계속 디코딩한다.** 버퍼가 차면 버리고 "다음 바이트부터 새 프레임" 으로 가면, 넘친 프레임의 꼬리를 새 프레임의 머리로 읽는다. 넘침은 플래그로 기억하고 **다음 0x00 까지** 버린다(`overflow` 상태).

**7. `frame_build` 에서 구분자 자리를 검사하지 않는다.** 인코딩이 `cap` 에 딱 맞게 끝나면 `out[e] = 0` 이 1 바이트 넘친다. 테스트: `"123456789"` 를 cap 12 에 → `COBS_ERR_OVERFLOW` 여야 한다 (13 필요).

## 8. 면접 Q&A

<details><summary>Q1. 왜 SLIP 대신 COBS 를 쓰나요? 꼬리: COBS 의 단점은?</summary>

장점은 **최악이 작고 미리 안다**는 것. SLIP 은 평균은 괜찮지만(랜덤 1000 바이트 +9.8) 최악이 2n + 2 라 버퍼와 전송 시간을 2배로 잡아야 하고, 데이터에 따라 링크 대역폭이 반토막 날 수 있다. COBS 는 n ≤ 254 에서 항상 +1(+구분자), 일반적으로 n + ⌈n/254⌉. DMA TX 버퍼 크기, 최악 지연 계산, 실시간 스케줄링에서 이게 중요하다.

단점: (1) 인코더가 코드 바이트를 블록 앞에 써야 해서 **최대 254 바이트를 버퍼링**해야 한다 — 바이트가 생기는 대로 TX 레지스터에 바로 밀어 넣을 수 없다(SLIP 은 가능). (2) 텍스트처럼 0 이 없는 데이터는 SLIP 이 더 작다(ASCII 1000 바이트: SLIP +2, COBS +5). (3) 사람이 터미널로 읽기 어렵다. 그래서 AT 명령·디버그 콘솔은 텍스트 줄(COMM-B14), 바이너리 텔레메트리는 COBS 가 흔한 조합이다.

</details>

<details><summary>Q2. 왜 구분자가 0x00 인가요? 다른 값으로 바꿀 수 있나요?</summary>

COBS 의 구조상 "없애는 값" 은 하나면 되고, 0x00 을 고르는 이유는 바이너리 데이터에서 **가장 흔한 값**이기 때문이다(작은 정수의 상위 바이트, 구조체 패딩, 지워진 RAM). COBS 에서 0 은 코드 바이트로 바뀌어 **공짜**이니, 흔한 값을 없앨수록 이득이다(실측: 0 이 절반인 데이터는 길이와 상관없이 +2). 다른 값이 필요하면(예: 링크가 0x00 을 break 나 idle 로 쓰는 경우) 인코딩 결과 전체를 그 값 s 와 XOR 하면 된다 — 인코딩에 0 이 없으니 XOR 후엔 s 가 없다. 연습 4 에서 256 개 s 전부 확인. 원 논문은 0 쌍이나 0 연속을 더 줄이는 변형(COBS/ZPE)도 제안했고, 끝 블록 1 바이트를 아끼는 COBS/R 같은 변형도 쓰인다(세부는 원문 확인 권장).

</details>

<details><summary>Q3. 프레임이 깨지면 수신기는 어떻게 다시 맞추나요? 꼬리: 길이 필드 프레이밍과 비교하면?</summary>

0x00 은 인코딩 안에 절대 없으니 **다음 0x00 이 곧 다음 경계**다. 수신기는 0x00 을 받으면 무조건 상태를 초기화한다 — 깨진 프레임은 기껏해야 그 프레임 하나만 잃는다. 랜덤 스트림(3000 프레임, 손상 570)에서 멀쩡한 2430 프레임이 모두 나왔다. 앞 구분자를 함께 보내면 끝 0x00 을 잃어도 살아난다(16 번).

길이 필드 방식(COMM-B10)은 SOF 바이트가 payload 안에도 나올 수 있어서, 길이가 깨지거나 가짜 SOF 를 잡으면 "다음 바이트부터 다시 SOF 찾기" 같은 되감기(재스캔)가 필요하고, 버퍼를 다시 훑어야 한다. COBS 는 되감기가 없다. 대신 COBS 는 무결성 검사가 없으니 CRC 는 둘 다 필요하다.

</details>

<details><summary>Q4. COBS 디코딩이 성공했으면 데이터가 맞다고 봐도 되나요?</summary>

아니다. COBS 는 **프레이밍**이지 오류 검출이 아니다. 데이터 바이트가 다른 0 아닌 값으로 바뀌면 블록 길이가 그대로라 디코딩은 성공하고 정확히 그 바이트만 다르다(연습 5: 2만 9597 번 중 예외 0). 200만 번 한 바이트 손상 실험에서 COBS 구조가 잡은 것은 25%, 나머지 75% 는 CRC-16 만 잡았다. 또 코드 바이트가 바뀌면 0 의 위치가 옮겨져 최대 25 바이트가 틀어진다 — 한 바이트 손상이 여러 바이트 오류가 되므로 CRC 의 "버스트 ≤ 16 비트 전부 검출" 보장이 그대로 적용되지 않는다(실험에서는 0 개 놓침, 그러나 보장은 아님). 짧은 프레임이면 CRC-16, 긴 프레임·펌웨어 이미지면 CRC-32(COMM-B08, COMM-B09).

</details>

<details><summary>Q5. UART 수신을 ISR 과 메인 루프에 어떻게 나누나요? DMA 라면?</summary>

선택지 두 가지. (1) ISR 은 RDR 바이트를 링 버퍼에 넣기만 하고(가장 짧은 ISR, 오버런 위험 최소), 메인 루프가 `cobs_rx_feed` 를 돌린다. (2) ISR 에서 `cobs_rx_feed` 를 바로 부르고, 프레임 완료 시 버퍼를 메인 루프에 넘긴다 — 이때는 **이중 버퍼**가 필요하다(ISR 이 다음 프레임을 쓰는 동안 메인이 이전 프레임을 읽도록). 공유 플래그·인덱스 규칙은 C-M14. 실제 폴링/인터럽트 UART 드라이버는 BM-B12 / BM-M04.

DMA 라면: circular DMA + idle line 인터럽트(COMM-M11)로 덩어리째 받고, 덩어리에서 `memchr(…, 0x00, …)` 으로 경계를 찾은 뒤 **제자리 디코딩**(연습 3)한다. 디코딩 결과가 항상 입력보다 짧고 쓰기 위치가 읽기 위치 뒤에 있어서 같은 버퍼에서 된다. TX 는 프레임 전체를 인코딩해 DMA 한 번 — 최악 길이를 알아서 버퍼를 정적으로 잡을 수 있다.

</details>

<details><summary>Q6. 인코딩을 바이트 단위로 UART TX 에 바로 흘려보낼 수 있나요?</summary>

그대로는 안 된다. 블록의 코드 바이트가 블록 앞에 오는데, 값은 블록 끝(다음 0 또는 254 바이트)을 봐야 안다. 그래서 송신 측은 최대 254 바이트의 블록 버퍼가 필요하다 — 블록이 닫힐 때마다 "코드 + 블록" 을 내보내면 버퍼는 255 바이트로 고정된다(전체 프레임 버퍼보다 작다). 데이터가 생기는 대로 흘려야 하는 경우(로그 스트림)는 SLIP 이나 텍스트가 맞다. 반대로 **디코딩은 바이트 단위로 된다**(Part C) — 비대칭이다. 꼬리: 디코더가 0 을 바로 못 넣고 미뤄야 하는 이유는? 마지막 블록인지 0x00 이 와야 알기 때문(7. 실수 4).

</details>

<details><summary>Q7. 최악 인코딩 길이를 정확히 말해 보세요. 64 바이트 payload + CRC-16 이면 버퍼는 몇 바이트?</summary>

n = 0 이면 1, n ≥ 1 이면 n + ⌈n/254⌉, 구분자 +1. 증명: 인코딩 길이 = n + 1 + m, m = 뒤에 블록이 더 있는 0xFF 블록 수 ≤ ⌊(n−1)/254⌋. 그래서 n ≤ 254 는 내용과 무관하게 정확히 n + 1. 64 + 2 = 66 → 67 + 1 = 68 바이트. 0 없는 데이터가 최악에 닿는다. 함정: 254 바이트로 딱 끝날 때 `01` 을 덧붙이는 구현은 이 공식보다 1 바이트 길게 낸다(7. 실수 1) — 공식으로 잡은 버퍼가 넘친다.

</details>

<details><summary>Q8. 디코더가 받아들이지만 인코더는 절대 만들지 않는 입력이 있나요? 왜 신경 쓰나요?</summary>

있다, 한 모양뿐: 마지막 블록이 `01` 이고 그 앞이 `FF` 블록(`FF [254] 01`). 이것도 `FF [254]` 와 같은 254 바이트로 풀린다. 블록 분해가 데이터에 의해 강제되고(0 이 아닌데 c < 0xFF 블록을 끝낼 수 없음), 자유도는 끝의 빈 블록 하나뿐이기 때문이다. 신경 쓰는 이유: 같은 데이터에 인코딩이 두 개면 서명·해시를 인코딩된 바이트에 걸 때, 중복 제거, 그리고 퍼징의 "decode → encode → 같은가" 속성 테스트가 깨진다. 엄격 디코더는 이 모양을 `COBS_ERR_NONCANON` 으로 거부한다 — 연습 2 에서 랜덤 블록 열 2만 개로 "엄격 통과 ⇔ 재인코딩이 같음" 을 확인.

</details>

## 9. 추가 연습

### 연습 1 — 정확한 길이와 컴파일 시간 버퍼 크기

`size_t cobs_encoded_len(const uint8_t *src, size_t n)` (쓰지 않고 정확한 인코딩 길이)와 정수 상수식 매크로 `COBS_MAX_ENC(n)` 을 만들어 `static uint8_t txbuf[COBS_MAX_ENC(64 + 2) + 1];` 이 68 바이트인지 확인하라.

<details><summary>답 보기</summary>

```c
#define COBS_MAX_ENC(n) ((n) + ((n) == 0 ? 1 : ((n) + 253) / 254))

static size_t cobs_encoded_len(const uint8_t *src, size_t n) {
    size_t len = 1, run = 0;                                /* 첫 코드 바이트 */
    for (size_t i = 0; i < n; i++) {
        if (src[i] == 0) { len++; run = 0; continue; }      /* 0 → 다음 블록의 코드 */
        len++;
        if (++run == 254 && i + 1 < n) { len++; run = 0; }  /* 꽉 찬 블록 뒤에 데이터가 더 있음 */
    }
    return len;
}
```

0 은 "데이터 −1, 코드 +1" 이라 `len++` 하나로 같다. `_Static_assert(COBS_MAX_ENC(254) == 255 && COBS_MAX_ENC(255) == 257, …)` 로 컴파일 시간에도 확인. 랜덤 5000 개와 n = 250..520 의 0 없는 데이터에서 `cobs_encode` 길이와 일치.

</details>

### 연습 2 — 엄격(canonical) 디코더

`cobs_decode_strict` 를 만들어 "마지막 블록이 `01` 이고 바로 앞 블록이 `FF`" 인 입력을 `COBS_ERR_NONCANON` 으로 거부하라. 랜덤 블록 열(코드를 0xFF·0x01 위주로 뽑고 데이터는 0 아닌 값)로 **엄격 통과 ⇔ encode(decode(x)) == x** 를 확인하라.

<details><summary>답 보기</summary>

```c
static int cobs_decode_strict(const uint8_t *src, size_t n, uint8_t *dst, size_t cap) {
    size_t i = 0, prev = SIZE_MAX, last = SIZE_MAX;
    while (i < n && src[i] != 0) { prev = last; last = i; i += src[i]; }   /* 코드 바이트 사슬 따라가기 */
    if (i == n && last != SIZE_MAX && src[last] == 0x01 && prev != SIZE_MAX && src[prev] == 0xFF)
        return COBS_ERR_NONCANON;
    return cobs_decode(src, n, dst, cap);
}
```

코드 바이트는 "다음 코드까지의 거리" 이므로 `i += src[i]` 로 사슬을 따라가면 마지막 두 코드 위치가 나온다. 실측: 블록 열 2만 개 중 기본 디코더 20000 개 통과, 엄격 19028 개 통과, 비정규 972 개, 속성 위반 0. 즉 비정규형은 정말 이 한 모양뿐이었다.

</details>

### 연습 3 — 제자리 디코딩

`cobs_decode(buf, n, buf, n)` 처럼 입력과 출력이 같은 버퍼여도 결과가 같음을 랜덤 5000 개로 확인하고, 왜 안전한지 설명하라.

<details><summary>답 보기</summary>

코드 바이트를 읽을 때마다 i 가 1 늘고, 데이터 복사는 i 와 o 가 같이 늘고, 0 복원은 o 만 1 늘린다. 블록 하나를 처리하면 i 는 c, o 는 최대 c 만큼 늘므로 처음 o = 0 < i = 1 에서 시작한 "o < i (쓰기 전 시점)" 가 계속 유지된다 — 아직 읽지 않은 바이트를 덮어쓰는 일이 없다. 그래서 `memcpy` 대신 앞에서부터 바이트 루프(또는 `memmove`)로 복사하면 된다. RX DMA 버퍼에서 바로 디코딩해 RAM 을 아낀다. 왕복 5000 개 모두 일치.

</details>

### 연습 4 — 0x00 이 아닌 구분자

링크가 0x00 을 다른 용도로 쓴다고 하자. 구분자 s 로 프레이밍하는 `cobs_encode_sentinel(…, s)` / `cobs_decode_sentinel(…, s)` 를 만들고 s = 0..255 전부에서 출력에 s 가 없고 왕복이 되는지 확인하라.

<details><summary>답 보기</summary>

인코딩 결과의 모든 바이트를 `^= s`. 인코딩에는 0 이 없으므로 XOR 후에는 s 가 없다(x ^ s == s ⇔ x == 0). 디코딩은 받은 버퍼를 제자리에서 `^= s` 로 되돌린 뒤 보통 디코더. 비용은 바이트당 XOR 하나. s = 0 이면 원래 COBS. 256 × 40 개 랜덤 버퍼 모두 통과.

</details>

### 연습 5 — 한 바이트 손상: 구조가 보는 것, 못 보는 것

32 바이트 payload + CRC-16 을 인코딩하고 바이트 하나를 다른 **0 아닌** 값으로 바꿔 4만 번 디코딩하라. 데이터 바이트 손상은 "디코딩 성공 + 정확히 1 바이트 다름" 임을 단언(assert)하고, 코드 바이트 손상은 구조 / CRC / 놓침 으로 나눠 세라.

<details><summary>답 보기</summary>

코드 바이트 위치는 `for (i = 0; i < en; i += enc[i])` 사슬로 찾는다. 실측: 데이터 바이트 손상 29597 번 — 모두 디코딩 성공, 정확히 1 바이트 다름(속성 위반 0). 코드 바이트 손상 10403 번 — COBS 구조가 10083 번(96.9%), 나머지 320 번은 CRC 가 잡음, 놓침 0. 결론: 구조 검사는 코드 바이트만 지킨다. CRC 는 필수.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 10/10 passed
```

## 10. 핵심 정리

- COBS = 코드 바이트가 "다음 0 까지 거리". 블록 `[c][c−1 바이트]`, c < 0xFF 면 뒤에 0, c = 0xFF 면 0 없는 254 바이트. 끝에 가상의 0. 결과에 0x00 이 없어 0x00 을 구분자로 쓴다.
- 인코더: 코드 자리 예약 → 0 이나 254 개에서 채우기. **254 로 딱 끝나면 새 블록을 열지 않는다.** 디코더: `code != 0xFF && i < n` 일 때만 0 복원, 복사 전 `len > n − i` 검사.
- 인코딩 길이 = n + 1 + m (m = 뒤에 블록이 있는 FF 블록 수) → 최악 n + ⌈n/254⌉ (n = 0 이면 1), n ≤ 254 면 정확히 n + 1. 구분자 +1. 64 + CRC 2 → 68 바이트 버퍼. SLIP 최악은 2n + 2.
- 거부: 빈 입력 / 0x00 / 끝 너머 코드 / 용량. 데이터 바이트 손상은 구조가 못 본다 → CRC 필수 (실측 75% 를 CRC 만 잡음).
- 스트리밍 디코더: 0 을 다음 코드 바이트까지 **미룬다**(zero_pending). 0x00 = 판정 + 무조건 초기화 = 재동기화. 앞 구분자로 끝 0x00 손실도 구제.
- CRC 는 프레이밍 버그를 못 잡을 수 있다(끝에 0 이 붙는 버그가 CRC-16/CCITT-FALSE 를 항상 통과) → test vector 와 왕복 테스트.
- 인코딩은 254 바이트 버퍼링 필요(바이트 단위 TX 불가), 디코딩은 바이트 단위·제자리 가능. 텍스트는 SLIP/줄 단위, 바이너리는 COBS.

## 참고자료

- S. Cheshire, M. Baker, "Consistent Overhead Byte Stuffing", *IEEE/ACM Transactions on Networking* 7(2), 1999 (초판 ACM SIGCOMM '97) — COBS 정의, 최악 오버헤드(254 바이트당 1), COBS/ZPE 변형. 권·호·연도는 기억에 의존 — 확인 권장. <http://www.stuartcheshire.org/papers/COBSforToN.pdf>
- Wikipedia, "Consistent Overhead Byte Stuffing" — 인코딩 예 1~11 (이 레슨 Part A 의 벡터; 두 가지 인코더로 재현해 확인): <https://en.wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing>
- RFC 1055, "A Nonstandard for Transmission of IP Datagrams over Serial Lines: SLIP" (1988) — END 0xC0, ESC 0xDB, ESC_END 0xDC, ESC_ESC 0xDD: <https://www.rfc-editor.org/rfc/rfc1055>
- CRC RevEng catalogue, CRC-16/IBM-3740 (= CCITT-FALSE): width 16, poly 0x1021, init 0xFFFF, refin/refout false, xorout 0, check 0x29B1 (이 레슨 코드로 재계산): <https://reveng.sourceforge.io/crc-catalogue/16.htm>
- COBS 를 쓰는 라이브러리 예: Arduino PacketSerial, Rust `postcard` 의 COBS 플레이버 — 기억에 의존, 각 저장소 확인 권장.
- 관련 레슨: COMM-B10 (길이 + CRC 프레임, 재스캔 재동기화), COMM-B11 (SLIP), COMM-B08 (체크섬 vs CRC 검출 능력), COMM-B09 (HW CRC 유닛), COMM-B14 (텍스트 커맨드 라인), COMM-M10 (견고한 패킷 파서: 타임아웃·통계), COMM-M11 (circular DMA + idle line), ALGO-B14 (체크섬), ALGO-M01 / ALGO-M02 (CRC 원리 / 테이블), C-M14 (ISR 공유 데이터), BM-B12 / BM-M04 (UART 드라이버), TEST-M02 (파서 퍼징)
