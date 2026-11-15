---
id: COMM-B11
time: 50분
updated: 2026-11-11
---
# COMM-B11 SLIP 인코더/디코더 (RFC 1055)

## 학습 목표

- 바이트 스터핑(byte stuffing)의 규칙 네 줄 — END 0xC0 = 프레임 끝, 데이터 0xC0 → `DB DC`, 데이터 0xDB → `DB DD`, 나머지는 그대로 — 을 출력 용량을 지키는 인코더와 **바이트 하나씩 먹는** 스트리밍 디코더(ISR 에서 불러도 되는 O(1) 상태 머신)로 구현한다.
- 연속 END(빈 프레임), 잘못된 이스케이프(`ESC 0x41`, `ESC ESC`, `ESC END`), 너무 긴 프레임을 주입하고, RFC 1055 예제 코드처럼 "그대로 넣는" 관대한(LENIENT) 정책과 거부하는(STRICT) 정책이 **실제 스트림에서 몇 프레임을 잃는지** 측정해 정책을 고른다.
- 최악 오버헤드 2n+2 를 전부 0xC0 / 0xDB 입력으로 확인하고, 랜덤 데이터 평균(n·2/256 + 2)과 "음수 float 텔레메트리"처럼 데이터에 따라 25% 까지 커지는 경우를 실측해 UART 버퍼·타임아웃을 계산한다.
- SLIP 에는 오류 검출이 없다는 점을 스트림 중간 시작 실험으로 보이고, CRC-16 을 붙인 링크 계층으로 잡음·END 유실·비트 오류가 섞인 스트림에서 멀쩡한 프레임은 전부, 손상된 프레임은 하나도 올리지 않음을 테스트로 증명한다.

## 1. 문제

ESP32 ROM 부트로더, nRF 시리얼 DFU, 아마추어 무선 KISS TNC 처럼 "UART 위에 바이너리 패킷"이 필요한 곳에서 SLIP 은 가장 짧은 답이다. 면접에서는 "바이트 스트림에서 패킷 경계를 어떻게 찾나요?" 의 첫 후보로 나오고, 꼬리질문은 **이스케이프 오류 정책, 최악 오버헤드, 재동기화, 오류 검출 부재**로 깊어진다.

UART 는 바이트를 나를 뿐 "어디서 패킷이 끝나는지"는 모른다. COMM-B10 은 길이 필드(LEN)로 경계를 표시했다 — 길이 바이트가 깨지면 엉뚱한 곳까지 읽고 재동기화(resync)를 따로 해야 한다. SLIP(Serial Line Internet Protocol, RFC 1055, 1988)은 반대로 간다: **특별한 바이트 하나(END = 0xC0)를 프레임 끝으로 예약**하고, 데이터에 같은 값이 나오면 두 바이트로 바꿔(escape) 보낸다. 그러면 와이어에 0xC0 이 보이는 곳은 언제나 프레임 경계다 — 수신기는 아무 데서나 듣기 시작해도 다음 END 에서 맞춰진다. 대가는 데이터에 따라 길이가 변한다는 것(최악 2배)이다.

RFC 의 상수는 8진수로 적혀 있다: END = 0300 = 0xC0, ESC = 0333 = 0xDB, ESC_END = 0334 = 0xDC, ESC_ESC = 0335 = 0xDD.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b11a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">페이로드 01 C0 DB 02 (4 B) → 와이어 8 B (lead END 사용)</text>
  <text x="10" y="52" fill="currentColor">payload</text>
  <g stroke="currentColor">
    <rect x="190" y="34" width="60" height="26" class="f-ok"/><rect x="290" y="34" width="60" height="26" class="f-warn"/>
    <rect x="410" y="34" width="60" height="26" class="f-warn"/><rect x="530" y="34" width="60" height="26" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace"><text x="211" y="52">01</text><text x="311" y="52">C0</text><text x="431" y="52">DB</text><text x="551" y="52">02</text></g>
  <g stroke="currentColor" marker-end="url(#b11a)">
    <line x1="220" y1="60" x2="160" y2="104"/><line x1="320" y1="60" x2="250" y2="104"/>
    <line x1="440" y1="60" x2="370" y2="104"/><line x1="560" y1="60" x2="460" y2="104"/>
  </g>
  <text x="10" y="126" fill="currentColor">wire</text>
  <g stroke="currentColor">
    <rect x="70" y="108" width="56" height="26" class="f-mut"/><rect x="130" y="108" width="56" height="26" class="f-ok"/>
    <rect x="190" y="108" width="56" height="26" class="f-warn"/><rect x="250" y="108" width="56" height="26" class="f-warn"/>
    <rect x="310" y="108" width="56" height="26" class="f-warn"/><rect x="370" y="108" width="56" height="26" class="f-warn"/>
    <rect x="430" y="108" width="56" height="26" class="f-ok"/><rect x="490" y="108" width="56" height="26" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="89" y="126">C0</text><text x="149" y="126">01</text><text x="209" y="126">DB</text><text x="269" y="126">DC</text>
    <text x="329" y="126">DB</text><text x="389" y="126">DD</text><text x="449" y="126">02</text><text x="509" y="126">C0</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="66" y="152">lead END</text><text x="198" y="152">ESC ESC_END</text><text x="318" y="152">ESC ESC_ESC</text><text x="505" y="152">END</text>
    <text x="560" y="118">n + (특수 바이트 수)</text><text x="560" y="134">+ 2 = 4 + 2 + 2 = 8</text>
  </g>
  <line x1="10" y1="168" x2="730" y2="168" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="10" y="190" fill="currentColor">END(0xC0) 한 바이트를 8N1, LSB 먼저로 보내면 — 로직 애널라이저에 보이는 모양</text>
  <path d="M90 215 H150 V250 H430 V215 H630" fill="none" stroke="currentColor" stroke-width="2"/>
  <g stroke="currentColor" stroke-dasharray="2 3">
    <line x1="190" y1="208" x2="190" y2="256"/><line x1="230" y1="208" x2="230" y2="256"/><line x1="270" y1="208" x2="270" y2="256"/>
    <line x1="310" y1="208" x2="310" y2="256"/><line x1="350" y1="208" x2="350" y2="256"/><line x1="390" y1="208" x2="390" y2="256"/>
    <line x1="470" y1="208" x2="470" y2="256"/><line x1="510" y1="208" x2="510" y2="256"/><line x1="550" y1="208" x2="550" y2="256"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="96" y="208">idle 1</text>
    <text x="158" y="272">S</text><text x="198" y="272">d0</text><text x="238" y="272">d1</text><text x="278" y="272">d2</text>
    <text x="318" y="272">d3</text><text x="358" y="272">d4</text><text x="398" y="272">d5</text><text x="438" y="272">d6</text>
    <text x="478" y="272">d7</text><text x="518" y="272">P</text>
    <text x="164" y="290">0</text><text x="204" y="290">0</text><text x="244" y="290">0</text><text x="284" y="290">0</text>
    <text x="324" y="290">0</text><text x="364" y="290">0</text><text x="404" y="290">0</text><text x="444" y="290">1</text>
    <text x="484" y="290">1</text><text x="524" y="290">1</text>
    <text x="570" y="240">7 비트 시간 low</text><text x="570" y="256">→ 3 비트 high</text>
  </g>
</svg>
```

*그림 1. 위: 특수 바이트 두 개가 각각 2바이트로 늘어나고 앞뒤에 END. 아래: 0xC0 = 1100 0000 이 LSB 먼저 나가므로 start 비트 포함 7 비트 시간 low, d6·d7·stop 이 high (COMM-B01 의 8N1 규칙). 캡처에서 이 모양이 보이면 프레임 경계다.*

### Part A — 인코더와 스트리밍 디코더

- `slip_encode(in, n, out, cap, lead_end, &out_len)`: 규칙대로 스터핑한다. `lead_end` 면 맨 앞에도 END 를 보낸다(RFC 가 권하는 방식, 이유는 Part C). 출력 용량 `cap` 을 바이트마다 확인하고, 모자라면 `SLIP_ERR_OVERFLOW` 를 돌려주며 `out[cap]` 이후는 절대 쓰지 않는다. 최악 크기는 `SLIP_ENC_MAX(n) = 2n + 2`.
- `slip_dec_push(d, b)`: UART RX 인터럽트가 바이트를 하나 받을 때마다 부르는 자리. 호출자가 준 정적 버퍼(`cap` = 디코딩 후 최대 프레임 길이)에 모으고, END 에서 프레임을 완성한다. 반환은 `SLIP_MORE` / `SLIP_FRAME`(버퍼는 다음 push 전까지 유효) / 오류 코드. **END 하나에 판정 하나** — 오류도 프레임을 끝내는 END 에서 보고한다.
- 테스트: 알려진 벡터, 1바이트 페이로드 256개 × lead 2 × 정책 2 와 2바이트 65536개 × lead 2 전수 왕복, 랜덤 5000개(특수 바이트 비율 1/4 로 치우침), 출력 구조 검사(END 는 양 끝에만, ESC 뒤엔 반드시 DC/DD), 200 프레임 연속 스트림.

### Part B — 결함 주입과 최악 오버헤드

- 연속 END: `C0 C0 C0 01 C0 C0` → 프레임은 `[01]` 하나, 빈 프레임 4개는 세기만 하고 버린다.
- 잘못된 이스케이프: `ESC 0x41`, `ESC ESC`, `ESC END` 를 두 정책으로. **LENIENT** = RFC 1055 본문의 C 예제 `recv_packet()` 처럼 모르는 값은 그대로 저장, **STRICT** = 그 프레임을 버리고 `SLIP_ERR_ESC`.
- 너무 긴 프레임: `cap = 16` 에서 16 바이트는 통과, 17 바이트는 `SLIP_ERR_LONG` 후 다음 프레임 정상. cap 은 **디코딩 후** 바이트 기준이다(0xC0 16개는 와이어 34 바이트지만 통과).
- 최악 오버헤드: 전부 0xC0 / 전부 0xDB, n = 0…1006 에서 정확히 2n+2(lead) / 2n+1. 랜덤 1006 바이트 2000개의 실측 평균. 데이터 종류별 표는 `demo_overhead.c`.
- 스트림 중간에서 듣기 시작: SLIP 만으로는 앞이 잘린 프레임이 "정상" 으로 올라간다 — Part C 의 동기.

### Part C — SLIP + CRC-16 링크: 잡음 섞인 UART 스트림

- 링크 프레임 = `SLIP( payload ‖ CRC_hi ‖ CRC_lo )`, CRC-16/CCITT-FALSE 를 **스터핑 전 페이로드**에 대해 계산, big-endian 으로 붙인다(CRC 바이트도 스터핑된다). `LINK_MTU = 64`.
- `link_send()` 와 ISR 자리의 `link_rx_byte()`(좋은 프레임만 올리고 `crc_err`/`short_err` 통계, 이스케이프·길이 오류는 디코더 통계).
- 스트림 실험 세 개 (400 프레임, 고정 시드): ① STRICT + lead END 에 중간 시작·프레임 사이 잡음(일부는 ESC 로 끝남)·끝 END 유실·와이어 비트 반전 → 반전 안 된 프레임은 **순서·내용까지 전부**, 반전된 프레임은 **하나도** 안 올라와야 한다. ② lead END 없이 같은 잡음 → 잡음 뒤 프레임이 죽는 수. ③ 잡음이 ESC 로 끝날 때 LENIENT 와 STRICT 의 손실 비교.

## 2. Problem (English)

> **SLIP framing for a UART link (RFC 1055).**
>
> Constants: `END = 0xC0`, `ESC = 0xDB`, `ESC_END = 0xDC`, `ESC_ESC = 0xDD`.
>
> 1. `int slip_encode(const uint8_t *in, size_t n, uint8_t *out, size_t cap, bool lead_end, size_t *out_len);`
>    Data byte `0xC0` → `DB DC`, `0xDB` → `DB DD`, everything else verbatim; append `END`; if `lead_end`, also prepend `END`.
>    Return `SLIP_OK` and the length, or `SLIP_ERR_OVERFLOW` without writing past `out[cap-1]`.
>    Example: `{01 C0 DB 02}`, lead → `C0 01 DB DC DB DD 02 C0` (8 bytes).
> 2. A streaming decoder `slip_rc slip_dec_push(slip_dec *d, uint8_t b)` called once per received byte from the RX ISR.
>    No dynamic memory; the caller supplies the frame buffer and its capacity. Return `SLIP_MORE`, `SLIP_FRAME`
>    (frame in `d->buf[0..d->frame_len)`, valid until the next call), `SLIP_ERR_ESC` or `SLIP_ERR_LONG`.
>    Empty frames (back-to-back `END`s) are silently dropped. A frame longer than `cap` is dropped and reported at its `END`.
>    Policy for an invalid escape (`ESC` followed by anything but `DC`/`DD`): `SLIP_LENIENT` stores the byte as-is
>    (RFC sample code), `SLIP_STRICT` drops the frame. Example (STRICT): `C0 05 DB 41 06 C0 07 C0` → `ERR_ESC`, then `FRAME {07}`.
> 3. Worst-case encoded size? Measure it, and the average on random data.
> 4. SLIP has no error detection. Add a CRC-16 trailer (`link_send` / `link_rx_byte`) and show that on a noisy stream
>    (receiver starts mid-frame, garbage between frames, lost `END`s, bit flips) every intact frame is delivered and no corrupted one is.
>
> Constraints: O(1) work per received byte, no `malloc`, every buffer bounded. Follow-ups: why the leading `END`? What would you do on `ESC END`? How do you size the TX buffer?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 네 군데: `slip_encode`, `slip_dec_push` (Part A·B), `link_send`, `link_rx_byte` (Part C). 상수·구조체·`slip_dec_init`·`crc16_ccitt_false`·하네스는 주어진다. 스텁은 "출력 0 바이트" / "아직 프레임 없음" 을 돌려주므로 멈추거나 죽지 않는다(하네스의 스트림 생성기도 길이 0 을 견디게 되어 있다). 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:201  slip_encode(p1, 4, enc, sizeof enc, true, &m) == SLIP_OK && m == 8 && memcmp(enc, e1, 8) == 0
…
exhaustive 1-byte x4 configs + 2-byte x2 configs: bad=262144
…
stream1 strict+lead: 400 frames, noise=88 dropEND=0 flips=0 -> delivered=0 (expect 400) crc_err=0 short=0 esc_err=0 long=0
…
FAIL starter.c:511  oks && ds == NFR
3/45 passed
```

(exit 1. 통과한 3개는 스텁의 0 이 우연히 맞는 검사 — 예: CRC check 값, "최대 크기 < 2014".)

## 4. 힌트

<details><summary>힌트 1 — 인코더: 용량 검사는 "쓰기 직전에, 쓸 만큼"</summary>

불변식 `k <= cap` 을 유지한다. 1 바이트를 쓰기 전엔 `k == cap` 이면 실패, 2 바이트(ESC 쌍)를 쓰기 전엔 `cap - k < 2` 면 실패. `k + 2 > cap` 대신 `cap - k < 2` 로 쓰면 `k <= cap` 이라 뺄셈이 음수(size_t 넘침)가 되지 않는다. 미리 `cap >= 2n+2` 만 요구해도 맞지만, 그러면 평균 1% 오버헤드인 데이터에 2배 버퍼를 강요한다.

</details>

<details><summary>힌트 2 — 디코더 상태는 세 개면 충분하다</summary>

`NORMAL`(평소), `ESC`(직전 바이트가 ESC), `DISCARD`(이 프레임은 이미 망가짐, END 까지 버림). DISCARD 에 들어갈 때 이유(`ERR_ESC`/`ERR_LONG`)를 저장해 두고 END 에서 돌려준다. END 를 만나면 상태와 무관하게 `len = 0`, `state = NORMAL` 로 돌아간다 — 이것이 재동기화의 전부다. 단 하나의 예외는 LENIENT 의 `ESC END` (힌트 3).

</details>

<details><summary>힌트 3 — ESC 다음 바이트를 먼저 처리하라</summary>

`if (state == ESC)` 를 END 검사보다 **먼저** 둔다. 이 자리에서 DC → 데이터 0xC0, DD → 데이터 0xDB 로 바꾼 뒤 "보통 데이터 저장" 경로로 보낸다. 바꾼 값이 0xC0 이어도 프레임 끝이 아니다 — END 판정은 **받은 원래 바이트**로 한다(`bool delim = (b == END)` 를 맨 처음에 계산). 모르는 값은 정책에 따라: LENIENT 는 그대로 저장(END 라도 데이터!), STRICT 는 DISCARD, 그 바이트가 END 였다면 그 자리에서 프레임을 끝낸다.

</details>

<details><summary>힌트 4 — 링크 수신: CRC 는 디코딩이 끝난 바이트에</summary>

`slip_dec_push` 가 `SLIP_FRAME` 을 돌려줄 때만 일한다: 길이 < 2 면 `short_err`, 마지막 두 바이트를 `hi << 8 | lo` 로 읽어 앞부분의 CRC 와 비교. 송신도 같은 순서 — CRC 를 먼저 붙이고 **그다음** 스터핑. 그래야 CRC 바이트가 0xC0 이어도 안전하다(랜덤 3000 프레임 중 45개가 실제로 그랬다).

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 범위 확인 | 표준 SLIP 인가(상수 C0/DB/DC/DD), leading END 를 보내나, 최대 프레임 길이(RFC 는 표준이 없다고 함), 오류 검출은 위 계층 책임인가, 수신은 ISR 바이트 단위인가 DMA 청크인가 |
| 2. 규칙 | END = 경계, 데이터의 END/ESC 만 2바이트로. **경계 바이트가 데이터에 절대 안 나타난다**는 불변식 |
| 3. 상태 머신 | NORMAL / ESC / DISCARD. END 는 언제나 리셋 지점 |
| 4. 정책 결정 | 잘못된 이스케이프·너무 긴 프레임·빈 프레임을 어떻게 할지 **먼저 말하고** 코드 |
| 5. 테스트 | 특수 바이트 포함 왕복, 전부 0xC0, 연속 END, ESC 로 끝나는 스트림, 용량 경계, 중간 시작 |
| 6. 복잡도 | 바이트당 O(1), RAM = 프레임 버퍼(cap) + 상태 몇 바이트. TX 최악 2n+2 |

### Part A — 인코더

규칙은 세 줄이다: `0xC0 → DB DC`, `0xDB → DB DD`, 그 외 그대로. 끝에 END, 옵션으로 앞에도 END. 0xDC·0xDD 는 **데이터로는 그대로** 나간다 — ESC 뒤에 올 때만 의미가 있기 때문이다. 그래서 디코더는 "ESC 다음 바이트" 만 특별 취급하면 된다.

길이는 정확히 계산된다: `encoded = n + (0xC0 과 0xDB 의 개수) + 1 + lead`. 테스트는 랜덤 5000 프레임과 2바이트 전수에서 이 식을 그대로 대조한다. 출력 구조도 검사한다: END 는 맨 앞(lead)·맨 끝에만, 모든 ESC 뒤에는 DC 또는 DD. 이 성질 덕분에 수신기는 END 를 보면 **무조건** 경계라고 믿어도 된다.

용량 처리의 핵심은 불변식 `k <= cap` 이다. 테스트는 6바이트 페이로드(특수 3개, 필요 11바이트)에 대해 cap = 0…10 을 전부 시도해 매번 `SLIP_ERR_OVERFLOW` 이고 `out[cap..31]` 의 카나리(0xAA)가 그대로인지 본다. cap = 11 이면 성공하고 `out[11]` 도 그대로다.

### Part A — 디코더 상태 머신

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b11b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <g stroke="currentColor">
    <rect x="60" y="130" width="150" height="56" rx="12" class="f-ok"/>
    <rect x="330" y="130" width="130" height="56" rx="12" class="f-acc"/>
    <rect x="560" y="130" width="150" height="56" rx="12" class="f-warn"/>
  </g>
  <g fill="currentColor" font-weight="bold"><text x="102" y="163">NORMAL</text><text x="377" y="163">ESC</text><text x="597" y="163">DISCARD</text></g>
  <g stroke="currentColor" fill="none" marker-end="url(#b11b)">
    <path d="M210 145 H328"/>
    <path d="M330 175 H212"/>
    <path d="M460 145 H558"/>
    <path d="M620 188 C 620 280, 135 280, 135 190"/>
    <path d="M100 130 C 80 92, 170 92, 160 128"/>
    <path d="M610 130 C 590 92, 690 92, 670 128"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="40">데이터 → buf[len++]</text>
    <text x="20" y="56">len == cap 이면 → DISCARD (ERR_LONG)</text>
    <text x="20" y="72">END, len == 0 → 무시 (empty++)</text>
    <text x="20" y="88">END, len &gt; 0 → SLIP_FRAME</text>
    <text x="232" y="138" font-family="monospace">0xDB (ESC)</text>
    <text x="464" y="110">STRICT: 그 외 바이트</text>
    <text x="464" y="125">(END 면 즉시 보고)</text>
    <text x="580" y="56">END 아닌 바이트는</text>
    <text x="580" y="72">전부 버림 (ESC 도)</text>
    <text x="222" y="202">DC → C0, DD → DB 로 저장</text>
    <text x="222" y="218">LENIENT: 그 외(END 포함)도 그대로 저장</text>
    <text x="170" y="296">END → 저장해 둔 오류 (ERR_ESC / ERR_LONG) 보고, len = 0</text>
    <text x="10" y="318">END 는 (LENIENT 의 ESC END 만 빼면) 어느 상태에서든 프레임을 닫고 NORMAL 로 돌아간다.</text>
    <text x="10" y="334">이것이 SLIP 재동기화의 전부다.</text>
  </g>
</svg>
```

*그림 2. `slip_dec_push` 의 상태 머신. 오류는 그 프레임을 닫는 END 에서 한 번만 보고된다.*

코드에서 END 판정은 **받은 원래 바이트**로 한다: 함수 첫 줄 `bool delim = (b == SLIP_END);`. ESC 상태에서 DC 를 0xC0 으로 바꿔도 delim 은 false 라 프레임이 끝나지 않는다. 이 한 줄이 "ESC_END 를 바꾼 뒤 END 검사에 다시 걸리는" 고전 버그를 막는다.

트레이스 — STRICT, 입력 `C0 05 DB 41 06 C0 07 C0` (테스트 `s_bad`):

| # | 바이트 | 들어갈 때 상태 | 동작 | len | 반환 |
|---|---|---|---|---|---|
| 0 | C0 | NORMAL | END, len 0 → 빈 프레임 무시 (empty=1) | 0 | MORE |
| 1 | 05 | NORMAL | 저장 | 1 | MORE |
| 2 | DB | NORMAL | → ESC | 1 | MORE |
| 3 | 41 | ESC | DC/DD 아님, STRICT → DISCARD, err = ERR_ESC | 1 | MORE |
| 4 | 06 | DISCARD | 버림 | 1 | MORE |
| 5 | C0 | DISCARD | END → 오류 보고, len = 0, NORMAL (esc_err=1) | 0 | **ERR_ESC** |
| 6 | 07 | NORMAL | 저장 | 1 | MORE |
| 7 | C0 | NORMAL | END, len 1 → 완성 | 0 | **FRAME [07]** |

LENIENT 로 같은 입력을 넣으면 #3 에서 0x41 을 그대로 저장해 `FRAME [05 41 06]`, `FRAME [07]` 이 나온다 — 오류는 조용히 데이터가 된다.

### Part B — 잘못된 이스케이프: 어떤 정책을 고르나

RFC 1055 본문의 C 예제 `recv_packet()` 은 ESC 다음 바이트가 ESC_END/ESC_ESC 가 아니면 "프로토콜 위반이지만 그냥 넣는 게 최선인 듯하다" 는 주석과 함께 그 바이트를 저장한다(본문 주석 취지, 원문 확인 권장). 연습 5 에서 이 코드를 그대로 옮겨 돌렸다. 세 경우의 결과 (테스트로 고정한 실제 동작):

| 입력 | LENIENT (RFC 예제) | STRICT (이 레슨) |
|---|---|---|
| `C0 05 DB 41 06 C0 07 C0` | `[05 41 06]`, `[07]` — 0x41 이 데이터로 | `ERR_ESC`, `[07]` |
| `C0 DB DB 09 C0` (인코더가 ESC 를 `DB DB` 로 잘못 보냄) | `[DB 09]` — 버그 인코더와 우연히 호환 | `ERR_ESC` |
| `C0 05 DB C0 07 08 C0` (ESC 직후 END) | `[05 C0 07 08]` — **END 를 삼켜 두 프레임이 하나로** | `ERR_ESC`, `[07 08]` |

세 번째 줄이 결정적이다. 선 잡음이 우연히 0xDB 로 끝나고 그 뒤에 다음 프레임의 lead END 가 오면, LENIENT 는 그 END 를 데이터로 먹어 버리고 다음 프레임 전체를 잃는다. 스트림 실험 ③(400 프레임, 잡음 102번)의 실측: **LENIENT 102 프레임 손실, STRICT 0**. 그래서 이 레슨은 STRICT 를 기본으로 권한다: "END 는 언제나 경계" 라는 불변식을 지키는 쪽이 재동기화가 확실하다. LENIENT 의 유일한 장점은 위 두 번째 줄 같은 비표준 송신기와의 호환인데, 그건 버그를 숨기는 것이다.

### Part B — 연속 END, 빈 프레임, 너무 긴 프레임

- **빈 프레임은 버린다.** lead END 를 쓰면 프레임 사이에 END END 가 생기므로 빈 프레임은 정상 상황이다. 200 프레임 스트림에서 `empty == 200` (프레임마다 lead END 하나) 을 확인한다. 결과적으로 **SLIP 은 길이 0 페이로드를 나를 수 없다** — `slip_encode(빈 입력)` 은 `C0 C0` 이고 수신기에선 아무것도 안 나온다. 테스트 `roundtrip()` 도 n = 0 이면 "프레임 0개" 를 정답으로 둔다. 빈 메시지가 필요하면 위 계층에서(예: Part C 처럼 CRC 2바이트가 항상 붙게) 해결한다.
- **너무 긴 프레임은 END 까지 버리고 END 에서 보고한다.** cap 을 넘는 순간 DISCARD 로 가고 그 뒤 바이트는 버린다. 오류를 바로 반환하지 않고 END 에서 반환하는 이유: (1) 호출자가 받는 사건 수가 "END 하나에 하나" 로 일정하고, (2) 나머지 바이트가 다음 프레임 앞에 붙는 일이 없다. RFC 예제는 반대로 넘친 바이트를 조용히 버리고 **잘린 프레임을 정상 길이로 돌려준다** — 연습 5 에서 22바이트 프레임을 16바이트 버퍼로 받으면 `r == 16` 이 오류 없이 나온다. 위에 CRC 가 없으면 잘린 데이터가 그대로 올라간다.
- cap 은 디코딩 후 기준: 0xC0 16개(와이어 34바이트)는 cap 16 에 들어가고, 17개는 마지막 `DB DC` 쌍을 바꾼 순간 넘쳐 `ERR_LONG`.
- RFC 1055 는 "표준 최대 크기는 없다, Berkeley UNIX 드라이버의 1006 바이트(프레이밍 문자 제외)를 따르는 게 좋다" 는 취지로 말한다(수치는 기억 기준 — 원문 확인 권장). 그래서 이 레슨의 측정 길이에 1006 을 썼다.

### Part B — 오버헤드 실측

최악은 모든 바이트가 0xC0 또는 0xDB 일 때 `2n + 2` (lead 없이 `2n + 1`). 테스트는 n = 0…1006 전부에서 정확히 이 값을 확인한다. 균일 랜덤 데이터에선 바이트가 특수할 확률이 2/256 이라 기대 길이 `n + 2n/256 + 2`, 표준편차 `√(n · 2/256 · 254/256)`. `demo_overhead.c` 실제 출력 (lead END 포함, 랜덤은 길이마다 10000회, 마지막 열 = 115200 baud 8N1 전송 시간):

| 데이터 | n | 인코딩 크기 (min / mean / max) | 오버헤드 | ms @115200 |
|---|---|---|---|---|
| 균일 랜덤 | 16 | 18 / 18.13 / 22 | 13.28% | 1.573 |
| 균일 랜덤 | 64 | 66 / 66.51 / 71 | 3.92% | 5.773 |
| 균일 랜덤 | 256 | 258 / 259.98 / 267 | 1.55% | 22.568 |
| 균일 랜덤 | 1006 | 1008 / 1015.84 / 1029 (sd 2.82, 이론 1015.86 / 2.79) | 0.98% | 88.181 |
| 전부 0x00 · ASCII 텍스트 | 1006 | 1008 | 0.20% | 87.500 |
| float32 LE, 값 (−4, −2] | 256 | 322 | 25.78% | 27.951 |
| float32 LE, 값 (−4, −2] | 1006 | 1267 | 25.94% | 109.983 |
| 전부 0xC0 / 전부 0xDB | 256 | 514 | 100.78% | 44.618 |
| 전부 0xC0 / 전부 0xDB | 1006 | 2014 | 100.20% | 174.826 |

읽는 법:
- 랜덤 데이터 평균 1% 이하 — "보통은 거의 공짜". solution.c 의 2000회 측정도 평균 1015.86, 최대 1027 이었다.
- **데이터가 랜덤이 아니면 얘기가 다르다.** IEEE 754 float 에서 −2.0 = 0xC000_0000 이고 (−4, −2] 의 값은 전부 최상위 바이트가 0xC0 이다. 그런 센서 값을 리틀 엔디언으로 보내면 4바이트마다 하나가 이스케이프 → 약 26%. 음수 가속도·온도 오프셋 같은 텔레메트리에서 실제로 생길 수 있는 모양이다.
- 버퍼와 타임아웃은 **최악**으로 잡는다: 1006 바이트 프레임은 115200 baud 에서 평균 88 ms 지만 최악 175 ms. "프레임 사이 간격 100 ms 넘으면 끊김" 같은 타임아웃은 최악 데이터에서 오탐한다.
- 고정 상한이 필요하면 COBS(COMM-B12): 0x00 을 구분자로 쓰고 254 바이트마다 1 바이트 정도로 오버헤드가 데이터와 거의 무관하다(정확한 경계는 B12).

### Part C — CRC 는 어디에, 왜

스트림 중간 시작 테스트가 문제를 보여 준다: `"ABCDEFGH"` 의 인코딩 `C0 41 42 … 48 C0` 을 다섯째 바이트부터 들으면 디코더는 `FRAME "DEFGH"` 를 **정상으로** 올린다. SLIP 은 경계만 알 뿐 내용이 맞는지 모른다. RFC 1055 스스로 SLIP 의 결함으로 주소 없음, 타입 필드 없음, **오류 검출/정정 없음**, 압축 없음을 든다 — 원래 IP 를 나르던 시절엔 IP/TCP 체크섬이 그 역할을 했다. 펌웨어 링크에선 직접 붙인다.

- **CRC 는 스터핑 전 페이로드에 계산하고, CRC 바이트까지 함께 스터핑**한다. 수신은 역순: 디코딩 → 길이 ≥ 2 → CRC 비교. 와이어 바이트(스터핑 후)에 CRC 를 걸면 수신기가 디코딩 전 바이트를 따로 모아야 하고 CRC 바이트 자체의 0xC0 을 처리할 방법이 없다.
- CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, 반사 없음, xorout 0, check("123456789") = **0x29B1** (테스트에서 계산 확인). 붙이는 순서는 big-endian 이라고 **정의**했다(Modbus 는 반대로 low 먼저 — COMM-B13). 반사 없고 xorout 0 인 CRC 는 "payload ‖ CRC_BE 전체의 CRC = 0" 성질도 있다(ALGO-M01).
- 왜 CRC 인가(체크섬이 아니라): 짧은 프레임에서 버스트·다중 비트 오류 보장 — COMM-B08 측정 참고.

### Part C — 스트림 실험

```svg
<svg viewBox="0 0 740 296" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">프레임 F1 과 F2 사이에 잡음 n n n 이 끼었을 때 수신기가 보는 것</text>
  <text x="10" y="54" fill="currentColor">① lead END</text>
  <g stroke="currentColor">
    <rect x="130" y="38" width="90" height="24" class="f-ok"/><rect x="220" y="38" width="30" height="24" class="f-mut"/>
    <rect x="250" y="38" width="80" height="24" class="f-warn"/><rect x="330" y="38" width="30" height="24" class="f-mut"/>
    <rect x="360" y="38" width="90" height="24" class="f-ok"/><rect x="450" y="38" width="30" height="24" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="160" y="55">F1</text><text x="225" y="55">C0</text><text x="270" y="55">n n n</text><text x="335" y="55">C0</text>
    <text x="390" y="55">F2</text><text x="455" y="55">C0</text>
  </g>
  <text x="495" y="48" fill="currentColor" font-size="12">잡음 조각은 CRC 오류로 버림</text>
  <text x="495" y="63" fill="currentColor" font-size="12">F2 정상 (실험: 손실 0)</text>
  <text x="10" y="114" fill="currentColor">② lead END 없음</text>
  <g stroke="currentColor">
    <rect x="130" y="98" width="90" height="24" class="f-ok"/><rect x="220" y="98" width="30" height="24" class="f-mut"/>
    <rect x="250" y="98" width="80" height="24" class="f-warn"/><rect x="330" y="98" width="90" height="24" class="f-warn"/>
    <rect x="420" y="98" width="30" height="24" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="160" y="115">F1</text><text x="225" y="115">C0</text><text x="270" y="115">n n n</text><text x="360" y="115">F2</text><text x="425" y="115">C0</text>
  </g>
  <text x="465" y="108" fill="currentColor" font-size="12">잡음이 F2 앞에 붙음 → F2 CRC 오류</text>
  <text x="465" y="123" fill="currentColor" font-size="12">(실험: 잡음 105번 → 105 프레임 손실)</text>
  <text x="10" y="174" fill="currentColor">③ 잡음이 ESC 로 끝남</text>
  <g stroke="currentColor">
    <rect x="160" y="158" width="30" height="24" class="f-mut"/><rect x="190" y="158" width="50" height="24" class="f-warn"/>
    <rect x="240" y="158" width="30" height="24" class="f-acc"/><rect x="270" y="158" width="30" height="24" class="f-mut"/>
    <rect x="300" y="158" width="90" height="24" class="f-ok"/><rect x="390" y="158" width="30" height="24" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="165" y="175">C0</text><text x="207" y="175">n n</text><text x="245" y="175">DB</text><text x="275" y="175">C0</text>
    <text x="330" y="175">F2</text><text x="395" y="175">C0</text>
  </g>
  <text x="435" y="168" fill="currentColor" font-size="12">STRICT: DB C0 = 오류 + 경계 → F2 정상</text>
  <text x="435" y="183" fill="currentColor" font-size="12">LENIENT: DB C0 → 데이터 C0, F2 와 합쳐짐</text>
  <line x1="10" y1="206" x2="730" y2="206" stroke="currentColor" stroke-dasharray="3 3"/>
  <g fill="currentColor" font-size="12">
    <text x="10" y="226">실측 400 프레임, 고정 시드 (solution.c 출력)</text>
    <text x="10" y="244">① STRICT + lead: 잡음 77, END 유실 47, 비트 반전 17 → 383 전달 (= 반전 안 된 프레임 전부)</text>
    <text x="10" y="262">② lead 없음: 잡음 105 → 295 전달 (105 손실)    ③ ESC 로 끝난 잡음 102 → LENIENT 298, STRICT 400 전달</text>
    <text x="10" y="280">END 유실은 다음 프레임의 lead END 가 닫아 줘서 손실 0 (lead 없으면 두 프레임이 합쳐져 둘 다 손실)</text>
  </g>
</svg>
```

*그림 3. lead END 와 STRICT 정책이 각각 어떤 손실을 막는지. 숫자는 solution.c 의 실제 출력.*

solution.c 실제 출력:

```text
link roundtrip 3000 frames (len 0..64): bad=0, frames whose CRC has a C0/DB byte=45
stream1 strict+lead: 400 frames, noise=77 dropEND=47 flips=17 -> delivered=383 (expect 383) crc_err=66 short=5 esc_err=24 long=0
stream2 strict, no lead END: noise before 105 frames -> delivered=295, lost=105
stream3 lead END, noise ending in ESC before 102 frames: LENIENT delivered=298 (lost 102), STRICT delivered=400 (lost 0)
```

- 스트림 ①의 검사는 "개수" 가 아니라 **전달된 프레임 목록 = 반전 안 된 프레임 목록 (순서·길이·내용)** 이다(`deliver_matches`). 오류 통계 합(66+5+24 = 95)은 잡음 조각 + 반전 프레임 + 중간 시작 조각 + 반전이 END 를 만들어 쪼갠 조각들이다 — 비트 반전 하나가 0xC0 을 만들면 프레임이 두 조각으로 갈라져 오류가 2개 세어진다. 그래서 통계는 정확한 값 대신 출력만 한다.
- 끝 END 유실만 따로 본 테스트: 프레임 `"AB"` 의 끝 END 를 버리고 `"CD"` 를 이어 보내면 lead END 가 있을 때 2개 모두 전달, 없을 때 0개 + `crc_err` 1 (두 프레임이 한 덩어리로 합쳐짐).
- lead END 의 가치는 두 가지로 측정됐다: 잡음이 다음 프레임을 망치지 않는다(①: 0 손실 vs ②: 105 손실), 끝 END 가 유실돼도 다음 lead END 가 닫아 준다(① 의 dropEND 47 에도 손실 0). 비용은 프레임당 1 바이트.
- CRC 위에서도 2^-16 확률로 잡음 조각이 통과할 수 있다. 이 시드에선 0 이지만, 그게 보장은 아니다 — 길이 상한·타입 필드·시퀀스 번호 같은 형식 검사를 같이 둔다(COMM-M10, M14).

### 복잡도와 RAM

- 인코더 O(n), 디코더 바이트당 O(1) (분기 몇 개, 루프 없음) → RX ISR 에서 직접 불러도 된다.
- RAM: 디코더 = 프레임 버퍼 `cap` + 상태 구조체(이 맥에서 `sizeof(slip_dec)` = 64 바이트 — 포인터·`size_t` 가 8 바이트라서. 32비트 Cortex-M 이면 같은 필드로 44 바이트). TX 는 블록 인코딩이면 `2n+2` 버퍼가 필요하지만, TXE 인터럽트에서 한 바이트씩 만드는 인코더(연습 4)는 상태 32 바이트로 버퍼가 필요 없다.
- `SLIP_FRAME` 의 버퍼는 다음 push 전까지만 유효하다. ISR 에서 디코딩한다면 프레임을 큐로 복사하거나 버퍼 두 개를 번갈아 쓰는(double buffering) 방식으로 메인 루프에 넘긴다 (Q6).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 45/45 passed, exit 0, 약 0.1 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
cc -std=c11 -Wall -Wextra -Werror -O2 demo_overhead.c -o demo_overhead && ./demo_overhead   # 5. 풀이의 오버헤드 표
```

## 7. 자주 하는 실수

**1. ESC 를 이스케이프하지 않는다 (END 만 바꾼다).** 페이로드 `DB DC` 를 그대로 보내면 수신기는 "ESC ESC_END" 로 읽어 0xC0 한 바이트를 만든다 — 2바이트가 1바이트로 바뀌고 오류도 없다. 고친 코드: `if (b == END || b == ESC)` 둘 다 스터핑.

**2. ESC 를 `DB DB` 로 보낸다 (ESC_ESC 대신).** LENIENT 수신기는 우연히 0xDB 로 받아 줘서 개발 중엔 안 보이다가, STRICT 수신기(또는 다른 회사 장비)를 만나면 그 바이트가 든 프레임만 전부 버려진다. 테스트 `s_escesc` 가 두 정책의 차이를 고정한다. 정답: `0xDB → DB DD`.

**3. ESC_END 를 바꾼 뒤 END 검사에 다시 건다.** `if (state == ESC) b = (b == ESC_END) ? END : …;` 다음에 `if (b == END) 프레임 끝` 을 두면 데이터 0xC0 이 프레임을 끊는다. 0xC0 이 든 모든 프레임이 두 조각 난다. 고친 코드: 경계 판정은 받은 원래 바이트로 — `bool delim = (b == SLIP_END);` 를 맨 처음에.

**4. 출력 용량을 n 으로만 검사한다.** `if (cap < n + 2) return ERR;` 은 특수 바이트 하나만 있어도 넘친다. 바이트마다 `k == cap` / `cap - k < 2` 로 검사하거나, 미리 `cap >= SLIP_ENC_MAX(n)` 을 요구한다. 테스트는 cap = 0…need−1 전부에서 카나리가 안 깨지는지 본다.

**5. 너무 긴 프레임을 잘라서 올린다.** RFC 예제처럼 `if (received < len) p[received++] = c;` 만 있으면 넘친 바이트는 사라지고 앞부분이 "정상 프레임" 으로 올라간다(연습 5, `r == 16`). 고친 코드: DISCARD 상태로 가고 END 에서 `SLIP_ERR_LONG`.

**6. ESC 상태를 END 에서 안 지운다 / ESC END 를 데이터로 받는다.** 프레임 끝의 ESC 가 다음 프레임 첫 바이트를 이스케이프로 해석하게 만들거나(상태 누수), LENIENT 처럼 END 를 삼켜 두 프레임을 합친다. 실측: ESC 로 끝나는 잡음 102번에 LENIENT 102 프레임 손실, STRICT 0.

**7. CRC 를 스터핑 후 바이트에 계산하거나, CRC 바이트를 스터핑하지 않는다.** 후자는 CRC 가 0xC0/0xDB 를 포함할 때(랜덤 3000 프레임 중 45개) 프레임이 쪼개진다 — 드물어서 짧은 테스트에선 안 보인다. 순서: CRC 붙이기 → 스터핑, 수신은 디코딩 → CRC.

**8. `SLIP_FRAME` 의 버퍼를 나중에 읽는다.** RX ISR 이 다음 바이트를 push 하면 `buf[0]` 이 덮인다. 메인 루프가 플래그만 보고 나중에 읽으면 첫 바이트가 다음 프레임 것으로 바뀐다. 프레임 완성 시점에 복사하거나 버퍼를 교대로 쓴다.

## 8. 면접 Q&A

<details><summary>Q1. 길이 prefix(COMM-B10) 대신 바이트 스터핑을 쓰는 이유는? 단점은?</summary>

장점은 **재동기화가 구조에 들어 있다**는 것. 경계 바이트가 데이터에 절대 나타나지 않으므로 수신기는 어디서 듣기 시작해도, 어떤 오류 뒤에도 다음 END 에서 맞춰진다. 길이 prefix 는 LEN 바이트 하나가 깨지면 엉뚱한 곳까지 읽고, SOF 를 다시 찾는 재검색 로직(B10 Part C)이 필요하다. 수신 버퍼 없이 바이트 단위로 처리하기도 쉽다. 단점: (1) 길이가 데이터에 따라 변한다(최악 2배) → 버퍼·전송 시간 계산이 최악 기준, (2) 바이트마다 비교가 필요해 DMA 로 바로 쓰기 어렵다(디코딩 단계 필요), (3) 수신기가 길이를 미리 모르므로 "이 프레임에 버퍼가 충분한가" 를 끝까지 받아 봐야 안다. 실무에선 둘을 섞기도 한다 — 스터핑으로 경계, 안에 길이·타입·CRC.

</details>

<details><summary>Q2. 왜 프레임 앞에도 END 를 보내나요? 꼬리: 비용은?</summary>

RFC 1055 가 권하는 방식(Phil Karn 제안으로 소개됨, 기억 기준): 프레임 사이에 선 잡음이 들어오면 수신기 버퍼에 쓰레기가 쌓인다. 앞 END 가 그 쓰레기를 별도의 조각으로 닫아 버리므로 다음 프레임은 깨끗하게 시작한다. 쓰레기 조각은 위 계층 검사(여기선 CRC)에서 버려진다. 이 레슨 실측: 잡음 105번에 lead 없으면 105 프레임 손실, lead 있으면 0. 덤으로 끝 END 가 유실돼도 다음 프레임의 lead END 가 닫아 준다(유실 47번, 손실 0). 비용은 프레임당 1 바이트와, 연속 END → 빈 프레임을 수신기가 무시해야 한다는 규칙 하나. 프레임을 쉬지 않고 연달아 보낼 땐 앞 프레임의 끝 END 가 이미 그 역할을 하므로 생략하는 구현도 있다.

</details>

<details><summary>Q3. 최악 오버헤드는? TX 버퍼는 얼마로 잡나요? 꼬리: COBS 와 비교하면?</summary>

모든 바이트가 0xC0/0xDB 면 2n, END 2개 더해 **2n+2**. 랜덤 데이터 평균은 n·2/256 + 2 (1006 바이트에 평균 1015.84, 이론 1015.86). 블록 인코딩이면 TX 버퍼는 `2·MTU + 2` 가 필요하다 — 1006 바이트 MTU 면 2014 바이트. RAM 이 아까우면 TXE 인터럽트에서 바이트 단위로 인코딩해 버퍼를 없앤다(연습 4, 상태 32 바이트). 또는 MTU 를 버퍼에서 거꾸로 정한다: 128 바이트 버퍼면 최대 페이로드 63(연습 1). 데이터가 랜덤이 아닐 수 있다는 점도 말한다: (−4, −2] 의 float 를 LE 로 보내면 26%. COBS(COMM-B12)는 0x00 을 구분자로 쓰고 최악 오버헤드가 254 바이트당 약 1 바이트로 데이터와 거의 무관하다 → 버퍼·타이밍 계산이 쉽다. 대신 인코더가 다음 0x00 까지 내다봐야 해서(최대 254 바이트) 바이트 단위 스트리밍 인코딩이 SLIP 보다 번거롭다.

</details>

<details><summary>Q4. ESC 다음에 이상한 바이트가 오면 어떻게 하나요? ESC 다음 END 는?</summary>

정책을 먼저 말한다. RFC 1055 의 예제 코드는 "그냥 저장" 이다 — 그 경우 ESC END 까지 데이터 0xC0 으로 받아 경계를 잃는다. 나는 STRICT 를 택한다: 그 프레임을 버리고(`ERR_ESC` 카운트), END 는 언제나 경계로 취급해 바로 재동기화한다. 근거는 측정: 잡음이 0xDB 로 끝나는 102번에서 LENIENT 는 102 프레임을 잃고 STRICT 는 0. 프레임을 버려도 되는 이유: 어차피 내용이 깨졌으므로 CRC 에서 떨어질 프레임이고, 빨리 버리는 게 CPU 와 버퍼에 이득이다. 리눅스 커널의 SLIP 드라이버도 END 에서 오류 플래그를 보고 프레임을 버리는 구조로 기억한다(세부 동작은 drivers/net/slip/slip.c 확인 권장).

</details>

<details><summary>Q5. SLIP 에 오류 검출이 없는데 무엇을 더하나요? 꼬리: CRC 는 스터핑 전인가 후인가?</summary>

RFC 1055 가 직접 인정하는 결함이다(주소·타입·오류 검출·압축 없음). 링크 프레임에 CRC-16(짧은 프레임) 또는 CRC-32 를 붙인다. **스터핑 전 페이로드에 계산하고 CRC 바이트도 함께 스터핑**한다 — 그래야 수신기는 디코딩 결과만 보고 검사할 수 있고, CRC 값이 0xC0 이어도 안전하다. 바이트 순서를 문서에 명시(여기선 BE). 이 레슨 스트림 실험: 비트 반전 17 프레임 전부 걸러지고, 반전 안 된 383 프레임은 순서·내용까지 전부 전달. 그리고 SLIP 이 앞이 잘린 프레임(`"DEFGH"`)을 정상으로 올리는 걸 보이면 CRC 가 왜 필수인지 설명이 끝난다. 재전송까지 필요하면 시퀀스 번호 + ACK(COMM-M14).

</details>

<details><summary>Q6. ISR 과 메인 루프는 어떻게 나누나요? DMA 로 받으면?</summary>

두 가지 배치. (1) ISR 에서 `slip_dec_push` 까지: 바이트당 O(1) 이라 가능하다. 프레임 완성 시 버퍼를 교대(double buffer)하거나 프레임 큐에 넘기고 플래그를 세운다 — 메인이 처리 중인 버퍼를 ISR 이 덮지 않게. (2) ISR 은 링 버퍼에 바이트만 넣고 메인 루프가 꺼내 디코딩: ISR 이 가장 짧고, 디코더 상태를 메인만 만지므로 공유 데이터 문제가 링 버퍼 인덱스로 줄어든다(C-M14). 고속이면 DMA circular 버퍼 + IDLE line 인터럽트로 청크를 받아 디코더에 먹인다(COMM-M11). 디코더가 바이트 단위 상태 머신이면 청크를 어디서 자르든 결과가 같다 — ESC 와 ESC_END 사이에서 잘려도(연습 3: 412 가지 2분할 + 2000 랜덤 청크, 불일치 0). 실제 칩 예: STM32 USART 의 RXNE 인터럽트 또는 DMA + IDLE, nRF 의 UARTE(EasyDMA) — 레지스터 세부는 RM 확인.

</details>

<details><summary>Q7. 로직 애널라이저로 SLIP 링크를 디버깅한다면 무엇을 보나요?</summary>

UART 디코더(COMM-A10, HWLAB-M06)로 바이트를 뽑고 0xC0 을 찾는다. 파형으로는 0xC0 이 LSB 먼저라 start 포함 **7 비트 시간 low 후 high** 라서 눈에 잘 띈다(그림 1). 확인 순서: (1) 프레임마다 앞뒤 END 가 있나, (2) 데이터 구간에 0xC0 이 나오지 않나(나오면 송신기가 스터핑을 안 함), (3) 0xDB 뒤가 항상 DC/DD 인가, (4) 수신 측 오류 카운터(esc_err/long_err/crc_err)와 대조. SLIP 은 Modbus RTU(COMM-B13)와 달리 **시간 간격에 의존하지 않으므로** 바이트 사이 간격이 길어도 괜찮다 — 그래서 RTOS 태스크가 띄엄띄엄 보내도 된다. baud 오차로 깨진 바이트(COMM-B03)는 CRC 오류로 나타난다.

</details>

<details><summary>Q8. PPP 의 HDLC 식 프레이밍과는 뭐가 다른가요? SLIP 은 지금 어디에 쓰이나요?</summary>

PPP(RFC 1662)는 플래그 0x7E, 이스케이프 0x7D, 이스케이프된 바이트는 XOR 0x20, FCS(CRC-16 또는 32) 필수, 제어 문자 맵(ACCM)으로 추가 바이트도 이스케이프 가능 — 같은 바이트 스터핑 계열에 SLIP 이 빠뜨린 오류 검출과 프로토콜 필드를 넣은 형태다. 오늘날 SLIP 형식이 쓰이는 곳(기억 기준, 문서 확인 권장): Espressif ESP8266/ESP32 ROM 시리얼 부트로더(esptool 이 SLIP 프레임으로 명령), Nordic nRF5 SDK 의 시리얼 DFU, 아마추어 무선 KISS TNC 프로토콜(FEND/FESC/TFEND/TFESC 가 같은 C0/DB/DC/DD 값). Don 의 RF 모듈 쪽으로 말하면: AT 명령은 텍스트라 줄바꿈이 경계(COMM-B14)지만, 바이너리 펌웨어 업데이트나 패킷 모드로 가면 이런 스터핑 프레이밍이 필요해진다.

</details>

## 9. 추가 연습

### 연습 1 — 크기 계산과 최대 페이로드

(a) 인코딩하지 않고 정확한 출력 크기를 돌려주는 `slip_encoded_len(in, n, lead)` 를 짜라(TX DMA 길이 설정용). (b) 고정 크기 TX 버퍼 `cap` 에 **어떤 데이터든** 들어가는 최대 페이로드 `slip_max_payload(cap, lead)` 를 구하라. 빈 프레임도 안 들어가면 −1.

<details><summary>답 보기</summary>

(a) `1 + lead + Σ (특수 ? 2 : 1)`. (b) `2n + 1 + lead ≤ cap` → `n = ⌊(cap − 1 − lead) / 2⌋`. cap 128, lead → 63 (2·63+2 = 128 딱 맞음), 129 → 63, 130 → 64. 테스트는 0xC0 63개가 128 바이트에 정확히 들어가고 64개는 `OVERFLOW` 인지 본다. MTU 를 버퍼에서 거꾸로 정하는 계산이다.

</details>

### 연습 2 — 제자리(in-place) 디코딩

END 를 뗀 프레임 본문이 든 버퍼를 **같은 버퍼 안에서** 디코딩하는 `long slip_decode_inplace(uint8_t *buf, size_t len)` 을 짜라. 잘못된 이스케이프, 끝에 홀로 남은 ESC, 본문 속 END 는 −1.

<details><summary>답 보기</summary>

읽기 인덱스 r, 쓰기 인덱스 w. 이스케이프 쌍은 2 바이트를 읽고 1 바이트를 쓰므로 항상 `w ≤ r` — 아직 안 읽은 바이트를 덮지 않는다. 그래서 DMA 로 받은 와이어 버퍼를 추가 RAM 없이 페이로드로 바꿀 수 있다.

```c
size_t w = 0;
for (size_t r = 0; r < len; r++) {
    uint8_t b = buf[r];
    if (b == SLIP_END) return -1;
    if (b == SLIP_ESC) {
        if (++r == len) return -1;
        if (buf[r] == SLIP_ESC_END) b = SLIP_END;
        else if (buf[r] == SLIP_ESC_ESC) b = SLIP_ESC;
        else return -1;
    }
    buf[w++] = b;
}
return (long)w;
```

</details>

### 연습 3 — DMA 청크 입력: 어디서 잘라도 같은 결과

`slip_dec_feed(d, chunk, n, cb, ctx)` 로 청크를 먹이고 프레임/오류마다 콜백을 부른다. 같은 스트림(30 프레임 + 잘못된 이스케이프 프레임 1개)을 바이트 단위, 가능한 모든 2분할, 랜덤 청크(1~64) 2000가지로 넣어 사건 순서와 내용(FNV-1a 해시)이 같은지 검증하라. ESC 바로 뒤에서 자르는 경우가 꼭 포함되게.

<details><summary>답 보기</summary>

디코더의 모든 상태가 구조체 안에 있고 바이트 단위로만 전이하므로 청크 경계는 의미가 없다 — 그게 테스트로 확인된다: 411 바이트 스트림, 412 가지 2분할(그중 35개는 ESC 바로 뒤) + 랜덤 2000가지, 불일치 0. 청크 단위로 "ESC 가 청크 끝이면 다음 청크를 기다린다" 같은 특수 처리를 넣는 구현이 오히려 버그를 만든다. 콜백 안에서 `p` 는 다음 바이트 처리 전까지만 유효하니 필요하면 복사한다(실수 8). COMM-M11 의 circular DMA 와 이어진다.

</details>

### 연습 4 — TXE 인터럽트용 바이트 단위 인코더

2n+2 출력 버퍼 없이, TX 레지스터가 빌 때마다 다음 와이어 바이트 하나를 돌려주는 `int slip_tx_next(slip_tx *t)` (끝이면 −1)을 짜라. 블록 인코더 출력과 랜덤 3000 프레임에서 같아야 한다.

<details><summary>답 보기</summary>

상태: 페이로드 포인터·길이·인덱스, 단계(lead END / 본문 / 이스케이프 둘째 바이트 대기 / 끝), 대기 중인 둘째 바이트. 특수 바이트를 만나면 ESC 를 내보내고 둘째 바이트(DC/DD)를 `pend` 에 저장, 다음 호출에서 내보낸다. 호출당 O(1). 이 맥에서 `sizeof(slip_tx)` = 32 바이트 — n = 1006 에서 2014 바이트 버퍼를 대신한다. 주의: 페이로드 버퍼는 전송이 끝날 때까지 바뀌면 안 된다(소유권을 TX 쪽에 넘긴다).

</details>

### 연습 5 — RFC 1055 예제 수신기의 두 함정

RFC 본문의 `recv_packet()` 을 그대로 C 로 옮긴 `rfc_recv()` 로 (a) 22 바이트 프레임(20 바이트 + CRC)을 16 바이트 버퍼로 받아 보고, (b) `C0 05 DB C0 07 08 C0` 을 받아 보라. CRC 와 이 레슨의 디코더는 각각 어떻게 반응하나?

<details><summary>답 보기</summary>

(a) `rfc_recv` 는 **16 을 정상 길이로** 돌려준다(넘친 6 바이트는 조용히 버림). 그 16 바이트의 마지막 2 바이트를 CRC 로 해석하면 맞지 않아 걸러진다 — CRC 가 없었다면 잘린 데이터가 올라갔다. 이 레슨의 디코더는 같은 입력에 `SLIP_ERR_LONG`. (b) `[05 C0 07 08]` 한 프레임 — ESC 뒤의 END 를 데이터로 받아 두 프레임이 합쳐진다. 원 RFC 코드는 1988년의 "예시" 였고, 그 코드의 정책을 그대로 제품에 쓰면 이 두 가지를 떠안는다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 14/14 passed
```

## 10. 핵심 정리

- SLIP = END(0xC0) 로 프레임을 닫고, 데이터 0xC0 → `DB DC`, 0xDB → `DB DD`. 경계 바이트가 데이터에 절대 안 나와서 **다음 END 에서 무조건 재동기화**.
- 디코더 상태 3개: NORMAL / ESC / DISCARD. END 판정은 받은 원래 바이트로(`delim` 먼저). 오류는 END 에서 한 번 보고, 빈 프레임은 무시 → SLIP 은 길이 0 페이로드를 못 나른다.
- 잘못된 이스케이프: RFC 예제는 그대로 저장(ESC END 가 경계를 삼킴), STRICT 는 프레임 버림. 실측 ESC 로 끝나는 잡음 102번 → LENIENT 102 손실, STRICT 0.
- 너무 긴 프레임은 잘라 올리지 말고 END 까지 버린 뒤 `ERR_LONG`. RFC 예제는 조용히 잘라서 정상처럼 돌려준다.
- 최악 2n+2 (1006 → 2014 바이트, 115200 에서 175 ms), 랜덤 평균 n·2/256 + 2 (≈1%), 하지만 (−4, −2] float 텔레메트리는 26%. 버퍼·타임아웃은 최악으로.
- lead END: 잡음 격리(105 손실 → 0) + 끝 END 유실 흡수. 비용 1 바이트.
- SLIP 에는 오류 검출이 없다(앞 잘린 프레임도 정상으로 나옴). CRC 는 스터핑 **전** 페이로드에, CRC 바이트도 스터핑, 바이트 순서 명시.

## 참고자료

- RFC 1055, J. Romkey, "A Nonstandard for Transmission of IP Datagrams over Serial Lines: SLIP" (1988) — 상수(8진 0300/0333/0334/0335), `send_packet`/`recv_packet` 예제 코드, 결함 목록, 최대 크기 1006 바이트 권고: <https://www.rfc-editor.org/rfc/rfc1055>
- RFC 1144, V. Jacobson, "Compressing TCP/IP Headers for Low-Speed Serial Links" (CSLIP) — SLIP 의 "압축 없음" 을 보완.
- RFC 1662, "PPP in HDLC-like Framing" — 0x7E 플래그, 0x7D 이스케이프 + XOR 0x20, FCS (Q8 비교).
- S. Cheshire, M. Baker, "Consistent Overhead Byte Stuffing", IEEE/ACM Trans. Networking, 1999 — COBS (COMM-B12).
- Linux `drivers/net/slip/slip.c` — 커널 SLIP 수신 경로(오류 플래그·버퍼 넘침 처리). 이 레슨의 설명은 기억 기준, 소스 확인 권장.
- Espressif esptool 문서 "Serial Protocol" (SLIP 프레이밍), Nordic nRF5 SDK 시리얼 DFU 전송(SLIP), KISS TNC 프로토콜 (K3MC/KA9Q) — 사용처 예, 기억 기준 확인 권장.
- IEEE 754 binary32 — −2.0f = 0xC0000000 (demo 의 float 행; 이 레슨에서 `memcpy` 로 확인한 값으로 측정).
- RevEng CRC catalogue — CRC-16/IBM-3740 (= CCITT-FALSE) check 0x29B1: <https://reveng.sourceforge.io/crc-catalogue/16.htm>
- 관련 레슨: COMM-B01 (8N1 비트 순서), COMM-B03 (baud 오차), COMM-B08 (체크섬 vs CRC 검출), COMM-B10 (길이 prefix 프레임), COMM-B12 (COBS), COMM-B13 (Modbus RTU, 시간 기반 경계), COMM-B14 (텍스트 라인 프로토콜), COMM-M10 (견고한 패킷 파서), COMM-M11 (DMA + IDLE 수신), COMM-M14 (ARQ 재전송), COMM-A10 (LA 디코더), ALGO-B14 (체크섬), ALGO-M01/M02 (CRC), C-M14 (ISR 공유 데이터), HWLAB-M06 (sigrok 디코더)
