---
id: ALGO-M12
time: 80분
updated: 2026-11-11
---
# ALGO-M12 상태 기계 파서: NMEA 문장과 체크섬

## 학습 목표

- UART 바이트를 **한 개씩** 받는 NMEA 0183 파서를 상태 기계(FSM, finite state machine) `IDLE → BODY → CK1 → CK2 → CR → LF` 로 짜고, 바이트당 O(1)·루프 없음·스택 0바이트(Cortex-M4 실측)로 ISR 에서도 돌릴 수 있게 만든다.
- "다음 `'$'` 에서 반드시 복구한다", "체크섬이 틀린 문장은 절대 수락하지 않는다" 를 **불변식으로 증명**하고, 선언형 참조 스캐너와의 차분 비교 + xorshift 퍼즈 + ASan/UBSan 으로 확인한다.
- 빈 필드를 지우지 않는 제자리 필드 분리(`",,"` = 빈 필드 하나)를 짜고, XOR 체크섬이 무엇을 잡고(1글자 오류 100%) 무엇을 못 잡는지(자리 바꿈 0%) 전수로 잰다.
- GGA 의 `ddmm.mmmm` 을 **1e-7 도 정수**로 바꾸는 식을 유도하고 "가장 가까운 정수" 임을 증명한다. `float` 로 하면 경도 180° 근처 해상도가 1.7 m 까지 떨어지는 이유를 말할 수 있다.

## 1. 문제

GNSS 모듈 + UART 는 로봇·드론·차량 펌웨어에서 가장 흔한 "바이트 스트림 파싱" 상황이라, 면접관은 이 문제 하나로 **상태 기계 설계·경계 조건·잡음 복구·고정소수점**을 한꺼번에 본다.

GPS 모듈은 전원이 켜지면 `$GPGGA,...*47\r\n` 같은 ASCII 문장을 초당 수십~수백 바이트 쏟아낸다. 그런데 실제 선로에서는:

- 전원이 켜질 때 부트 배너나 보레이트(baud rate)가 안 맞는 `0xFF` 쓰레기가 먼저 온다.
- 전원 글리치·RX FIFO 오버런(overrun)으로 문장이 **중간에 잘리고** 바로 다음 `'$'` 가 온다.
- 비트 하나가 뒤집혀 숫자 하나가 바뀐다 (체크섬이 잡아야 한다).
- DMA 는 문장 경계가 아니라 **아무 데서나** 버퍼를 끊어 넘긴다.

그래서 "한 줄을 다 모은 다음 `strtok`" 이 아니라, **바이트가 올 때마다 상태 하나만 바꾸는** 파서가 필요하다. ALGO-B15 가 줄 단위 콘솔 명령의 토큰화를 다뤘다면, 이 레슨은 그 앞단 — 끝이 언제 올지 모르는 스트림에서 문장 경계를 찾는 일 — 을 다룬다.

```svg
<svg viewBox="0 0 740 210" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">NMEA 0183 문장 하나의 구조 (GGA 예시, 실제 체크섬 0x47)</text>
  <g stroke="currentColor">
    <rect x="10" y="44" width="34" height="34" class="f-warn"/>
    <rect x="44" y="44" width="56" height="34" class="f-acc"/>
    <rect x="100" y="44" width="62" height="34" class="f-acc"/>
    <rect x="162" y="44" width="390" height="34" class="f-mut"/>
    <rect x="552" y="44" width="34" height="34" class="f-warn"/>
    <rect x="586" y="44" width="62" height="34" class="f-ok"/>
    <rect x="648" y="44" width="80" height="34" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="13" text-anchor="middle">
    <text x="27" y="66">$</text><text x="72" y="66">GP</text><text x="131" y="66">GGA</text>
    <text x="357" y="66">,123519,4807.038,N,01131.000,E,1,08,...,,</text>
    <text x="569" y="66">*</text><text x="617" y="66">47</text><text x="688" y="66">\r \n</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="27" y="96">시작</text><text x="72" y="96">talker</text><text x="131" y="96">문장 종류</text>
    <text x="357" y="96">데이터 필드 (쉼표 구분, 빈 필드 가능)</text>
    <text x="569" y="96">구분</text><text x="617" y="96">16진 2자리</text><text x="688" y="96">끝</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <path d="M44 116 V126 H552 V116"/>
    <path d="M10 156 V166 H728 V156"/>
  </g>
  <text x="298" y="144" fill="currentColor" text-anchor="middle">본문(body) = '$' 와 '*' 사이 — 이 바이트들의 XOR 가 체크섬, 최대 76글자</text>
  <text x="369" y="186" fill="currentColor" text-anchor="middle">'$' 부터 LF 까지 전체 ≤ 82바이트  →  76 = 82 − 1('$') − 3("*hh") − 2(CR LF)</text>
</svg>
```

규칙 (이 레슨의 파서가 따르는 것):

| 항목 | 규칙 |
|---|---|
| 시작 | `'$'`. **어느 상태에서든** `'$'` 는 새 문장의 시작 (예약 문자라 본문에 나올 수 없다) |
| 본문 | 인쇄 가능한 ASCII `0x20..0x7E` 중 `$ * !` 제외, 최대 `max_body`(표준 76) 글자 |
| 체크섬 | `'*'` + 16진 2자리 (대소문자 둘 다 받음) = 본문 바이트 XOR. 이 파서는 **필수** |
| 끝 | `\r\n` (로그 파일용으로 `\n` 만도 받음) — 체크섬 비교는 줄 끝에서 |
| 에러 | 이벤트 1개를 내고 IDLE 로. IDLE 은 다음 `'$'` 까지 바이트를 버린다 |

### Part A — 바이트 단위 FSM 과 체크섬

- `nmea_checksum(s, n)` — XOR.
- `nmea_feed(p, c)` — 바이트 하나 → 이벤트 하나: `NONE`, `SENTENCE`(본문은 `p->body`), 에러 8종 `CHECKSUM / TOOLONG / TRUNCATED / BADCHAR / NOCKSUM / BADHEX / BADEND`. 힙·재귀·루프 없음.
- 검증: 실제 예시 문장 12개의 `*hh` 를 직접 계산해 일치 확인, 경계 사례 표 23개, 무작위 스트림 1,500개에서 **선언형 참조 스캐너**와 수락 목록이 바이트 위치까지 같은지.

### Part B — 필드 분리, 복구 증명, 체크섬의 한계

- `nmea_split(body, fields, max, &nf)` — 쉼표에서 제자리로 자른다. **빈 필드를 합치지 않는다** (`strtok` 과 B15 토큰화와의 차이). 필드 수 = 쉼표 + 1.
- 불변식으로 증명: (1) 수락된 문장은 원본 바이트에서 정말 `$본문*hh` 이고 `hh = XOR(본문)` (2) 쓰레기가 무엇이든 그 뒤 정상 문장 하나는 반드시 수락된다.
- `{a, ','}` 길이 0~14 문자열 32,767개 전수, XOR 의 1글자 치환 전수(5,551개) / 순열 1,000개.

### Part C — GGA 를 고정소수점으로, 그리고 GPS 태스크

- `parse_hms_ms("092750.000")` → 34,070,000 ms, `parse_latlon_e7("4807.038","N",2)` → 481,173,000 (1e-7 도), `nmea_parse_gga()` → 시각·위경도·fix 품질·위성 수·HDOP·고도, 에러 9종과 우선순위.
- 주어진 `gps_feed()` 가 DMA 덩어리 → FSM → 분리 → GGA 를 엮는다. 쓰레기·잘림·비트 오류·82자 초과가 섞인 로그로 검증하고, 덩어리 크기를 1~64 로 무작위로 바꿔도 결과가 같은지 본다.
- 퍼즈: 쓰레기(무작위 바이트 / 구분 문자 수프 / 예시 문장 변이) + 정상 문장 60,000개 — 틀린 체크섬 수락 0, 복구 실패 0, ASan/UBSan 깨끗.

## 2. Problem (English)

> **Streaming NMEA 0183 parser.**
>
> A GNSS receiver streams ASCII sentences over UART. Your RX ISR (or DMA callback) hands you one byte at a time. Implement, in C11, without heap, recursion, or line buffering by the caller:
>
> ```c
> uint8_t   nmea_checksum(const char *s, size_t n);
> nmea_ev_t nmea_feed(nmea_parser_t *p, uint8_t c);   /* O(1) per byte, returns one event */
> bool      nmea_split(char *body, char *fields[], int max_fields, int *nf);
> bool      parse_hms_ms(const char *s, uint32_t *ms);
> bool      parse_latlon_e7(const char *v, const char *hemi, int deg_digits, int32_t *e7);
> gga_err_t nmea_parse_gga(char *const f[], int n, gga_t *out);
> ```
>
> A sentence is `'$' body '*' hex hex CR LF`, body ≤ 76 printable chars, checksum = XOR of body bytes. `'$'` may arrive at any time and always starts a new sentence. On any error emit exactly one error event and resynchronise on the next `'$'`. Fields are comma-separated and **may be empty**. Latitude/longitude must be returned as `int32` in 1e-7 degrees, rounded to nearest.
>
> | input bytes | events |
> |---|---|
> | `$GPGLL,4916.45,N,12311.12,W,225444,A*31\r\n` | `SENTENCE` |
> | `$GPGGA,1235$GPGLL,...,A*31\r\n` | `TRUNCATED`, `SENTENCE` |
> | `$GPGGA,123519,...,M,,*48\r\n` (real: `*47`) | `CHECKSUM` |
> | `$GPGLL,...,A\r\n` | `NOCKSUM` |
> | `"4807.038","N"` → | `481173000` |
>
> Follow-ups: prove you never accept a corrupted sentence and always recover. What does an XOR checksum miss? Why not `float` for coordinates? What runs in the ISR?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨진 맥
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 이 쭉 나오고 "18/62 passed", exit 1 — TODO 6개를 채워 62/62 를 만든다
```

TODO 는 `nmea_checksum`, `nmea_feed`, `nmea_split`, `parse_hms_ms`, `parse_latlon_e7`, `nmea_parse_gga` 6개. `nmea_init`, `hex_val`, `is_body_char`, `nmea_finish`, `parse_fixed`, `gps_feed` 와 테스트 쪽 참조 스캐너·생성기·퍼저는 주어진다.

## 4. 힌트

<details>
<summary>힌트 1 — '$' 는 switch 밖에서 먼저</summary>

`'$'` 의 전이는 현재 상태와 무관하다 (IDLE 이 아니었으면 TRUNCATED 를 낼 뿐). 그래서 `switch (state)` 에 들어가기 **전에** 처리하면, 각 상태 case 는 `'$'` 를 신경 쓸 필요가 없고 "복구" 증명이 한 줄로 끝난다. `len = 0` 과 `ck = 0` 을 **둘 다** 초기화할 것.

</details>

<details>
<summary>힌트 2 — 체크섬은 쌓아 가며, 비교는 끝에서</summary>

본문 글자를 저장할 때마다 `ck ^= c`. `*hh` 두 글자는 `ck_rx` 로 모은다 (`h1 << 4 | h2`). 비교는 LF 를 받은 순간에 — 그래야 "체크섬은 맞는데 줄 끝이 안 온 문장" (잘림) 을 수락하지 않는다.

</details>

<details>
<summary>힌트 3 — 길이 검사는 저장 직전에</summary>

`len >= max_body` 인 상태에서 본문 글자가 또 오면 TOOLONG. `len > max_body` 로 쓰면 한 글자 더 들어가고, `max_body = 127` 일 때 `nmea_finish` 의 `body[len] = '\0'` 가 `body[128]` 을 쓴다 (5. 풀이의 심은 버그 1).

</details>

<details>
<summary>힌트 4 — 필드 분리는 "자르고 다음으로"</summary>

`fields[n++] = p` → 쉼표나 NUL 까지 달리기 → NUL 이면 끝, 쉼표면 `*p++ = '\0'` 후 반복. 구분자를 **건너뛰지 않는다** — 그래야 `,,` 사이에 빈 필드가 생긴다. 넘침 검사는 `fields[n++]` 앞에서.

</details>

<details>
<summary>힌트 5 — ddmm.mmmm 을 정수로</summary>

분을 정수 `M` (단위 10⁻ᵏ 분, k = 소수 자리 수) 로 읽으면 분이 차지하는 도 = `M / (60·10ᵏ)`. 1e-7 도 단위로는 `M·10⁷ / D` (D = 60·10ᵏ). 가장 가까운 정수는 `(M·10⁷ + D/2) / D`. k ≤ 8 이면 `M·10⁷ ≤ 6·10¹⁶` 이라 `uint64_t` 로 충분하다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 (이 문제) |
|---|---|
| 1. 질문으로 범위 확정 | 바이트가 ISR 에서 하나씩 오나 DMA 덩어리로 오나? 체크섬 필수? 최대 길이(82)? `\n` 만 오는 로그도 받나? 지원할 문장(GGA 만?) 과 talker(GP/GN/GL...)? 좌표 출력 형식(1e-7 도 정수)? |
| 2. 무식한 방법 + 복잡도 | 줄 버퍼에 `\n` 까지 모은 뒤 `strtok(",")` + `atof`. O(L). 문제: 잘린 줄이 다음 줄과 붙음, `strtok` 이 빈 필드를 삼켜 GGA 인덱스가 밀림, `atof` 는 에러를 못 알림, `float` 정밀도 |
| 3. 더 나은 설계 + 불변식 | 6상태 FSM. 불변식: "BODY 이후 상태에서 `body[0..len)` 은 마지막 `'$'` 이후 저장된 바이트, `ck = XOR(body[0..len))`, `len ≤ max_body`". `'$'` 는 상태 무관 리셋 → 복구 보장 |
| 4. 코드 | `'$'` 먼저 → `switch(state)` → 에러면 IDLE. 분리·숫자·GGA 는 태스크 쪽 |
| 5. 소리 내어 말할 테스트 | 정상 CRLF/LF, 소문자 hex, 틀린 체크섬, 체크섬 없음, 문장 중간 `'$'`, 76/77글자, `*3\r`, `*31x`, 제어 문자, 쓰레기만, 빈 필드 GGA, no-fix GGA, 90°/180° 경계 |
| 6. 복잡도 | 시간: 바이트당 O(1) (feed 안에 루프 없음), 문장당 분리·GGA O(L). 메모리: 파서 구조체 176 B (호스트 실측), 스택: `nmea_feed` 0 B (M4 실측) |

### Part A — 상태 전이

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m12a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">nmea_feed 상태 전이 (굵은 화살표 = 정상 경로)</text>
  <rect x="10" y="34" width="720" height="26" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="370" y="52" fill="currentColor" text-anchor="middle">'$' : 어느 상태에서든 → BODY (len = 0, ck = 0). IDLE 이 아니었으면 TRUNCATED</text>
  <g stroke="currentColor">
    <rect x="20" y="110" width="80" height="44" rx="8" class="f-mut"/>
    <rect x="140" y="110" width="90" height="44" rx="8" class="f-acc"/>
    <rect x="270" y="110" width="80" height="44" rx="8" class="f-acc"/>
    <rect x="390" y="110" width="80" height="44" rx="8" class="f-acc"/>
    <rect x="510" y="110" width="80" height="44" rx="8" class="f-acc"/>
    <rect x="630" y="110" width="80" height="44" rx="8" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-weight="bold">
    <text x="60" y="137">IDLE</text><text x="185" y="137">BODY</text><text x="310" y="137">CK1</text>
    <text x="430" y="137">CK2</text><text x="550" y="137">CR</text><text x="670" y="137">LF</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="2.2">
    <path d="M100 132 H138" marker-end="url(#m12a)"/><path d="M230 132 H268" marker-end="url(#m12a)"/>
    <path d="M350 132 H388" marker-end="url(#m12a)"/><path d="M470 132 H508" marker-end="url(#m12a)"/>
    <path d="M590 132 H628" marker-end="url(#m12a)"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="119" y="104">'$'</text><text x="249" y="104">'*'</text><text x="369" y="104">hex</text>
    <text x="489" y="104">hex</text><text x="609" y="104">'\r'</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.3">
    <path d="M165 110 C150 78 220 78 205 110" marker-end="url(#m12a)"/>
    <path d="M45 110 C30 78 90 78 75 110" marker-end="url(#m12a)"/>
  </g>
  <text x="185" y="74" fill="currentColor" font-size="12" text-anchor="middle">본문 글자: 저장, ck ^= c</text>
  <text x="60" y="74" fill="currentColor" font-size="12" text-anchor="middle">junk++</text>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <path d="M670 154 V196 H60 V158" marker-end="url(#m12a)"/>
    <path d="M550 154 V196" />
  </g>
  <g stroke="currentColor" fill="none" stroke-dasharray="5 4">
    <path d="M185 154 V240 H60 V200"/><path d="M310 154 V240"/><path d="M430 154 V240"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="262">에러 (점선) → IDLE:  BODY: CR/LF → NOCKSUM, 예약·제어·8비트 → BADCHAR, len == max → TOOLONG</text>
    <text x="20" y="282">CK1/CK2: hex 아님 → BADHEX     CR/LF: 기대한 글자 아님 → BADEND</text>
    <text x="20" y="302">실선 되돌아감: CR·LF 상태에서 '\n' → ck == ck_rx ? SENTENCE : CHECKSUM, 그리고 IDLE</text>
  </g>
</svg>
```

전이표 (빈칸 = 일어날 수 없음):

| 상태 \ 입력 | `'$'` | 본문 글자 | `'*'` | 16진 | `\r` | `\n` | 그 외 |
|---|---|---|---|---|---|---|---|
| IDLE | → BODY | junk | junk | junk | junk | junk | junk |
| BODY | → BODY, TRUNCATED | 저장 / len==max 면 TOOLONG | → CK1 | (본문 글자) | NOCKSUM | NOCKSUM | BADCHAR |
| CK1 | → BODY, TRUNCATED | BADHEX (16진 아닌 것) | BADHEX | → CK2 | BADHEX | BADHEX | BADHEX |
| CK2 | → BODY, TRUNCATED | BADHEX | BADHEX | → CR | BADHEX | BADHEX | BADHEX |
| CR | → BODY, TRUNCATED | BADEND | BADEND | BADEND | → LF | 비교 | BADEND |
| LF | → BODY, TRUNCATED | BADEND | BADEND | BADEND | BADEND | 비교 | BADEND |

**손 추적** — 스트림 `Z $ A $ A B * 0 3 \r \n` (`'A' ^ 'B'` = 0x41 ^ 0x42 = 0x03):

| # | 바이트 | 상태 (후) | len | ck | ck_rx | 이벤트 |
|---|---|---|---|---|---|---|
| 1 | `Z` | IDLE | – | – | – | (junk = 1) |
| 2 | `$` | BODY | 0 | 00 | – | NONE |
| 3 | `A` | BODY | 1 | 41 | – | NONE |
| 4 | `$` | BODY | 0 | 00 | – | **TRUNCATED** |
| 5 | `A` | BODY | 1 | 41 | – | NONE |
| 6 | `B` | BODY | 2 | 03 | – | NONE |
| 7 | `*` | CK1 | 2 | 03 | – | NONE |
| 8 | `0` | CK2 | 2 | 03 | 00 | NONE |
| 9 | `3` | CR | 2 | 03 | 03 | NONE |
| 10 | `\r` | LF | 2 | 03 | 03 | NONE |
| 11 | `\n` | IDLE | 2 | 03 | 03 | **SENTENCE** `"AB"` |

**불변식 (I)**: 상태가 BODY·CK1·CK2·CR·LF 중 하나일 때, 마지막 `'$'` 이후 받은 본문 글자들이 정확히 `body[0..len)` 이고, `ck = body[0] ^ … ^ body[len−1]`, `len ≤ max_body`.

- 성립 시작: `'$'` 전이가 `len = 0, ck = 0` 으로 만든다 (빈 XOR = 0).
- 유지: BODY 에서 본문 글자는 `len < max_body` 일 때만 저장되고 동시에 `ck ^= c`. 그 밖의 전이는 `body`, `len`, `ck` 를 건드리지 않거나 IDLE 로 나간다.

**정리 1 (틀린 체크섬 수락 없음)**: `SENTENCE` 는 CR/LF 상태에서 `'\n'` 을 받고 `ck == ck_rx` 일 때만 나온다. CR 상태에 오려면 BODY →`'*'`→ CK1 →hex→ CK2 →hex→ CR 를 **이 순서로 중간에 `'$'` 없이** 지나야 하므로, 원본 스트림의 해당 위치는 `'$' body '*' h1 h2 ['\r'] '\n'` 이고 `ck_rx = h1·16 + h2`. (I) 에 의해 `ck = XOR(body)`. 따라서 `XOR(body) = hh`. ∎ — 테스트의 `verify_accept()` 가 이 문장을 원본 바이트만 보고 다시 확인한다.

**정리 2 (복구)**: 임의의 바이트열 G 다음에 정상 문장 S (`'$'` 로 시작, 본문 ≤ max_body, 올바른 `*hh`, CRLF) 가 오면, S 의 마지막 바이트에서 정확히 `SENTENCE(S 의 본문)` 이 나온다. 증명: S 의 첫 `'$'` 를 받은 직후 상태는 G 와 무관하게 `(BODY, len 0, ck 0)` 이다 (`'$'` 전이가 상태 무관). S 의 나머지에는 `'$'` 가 없으므로 (예약 문자) 그 뒤 동작은 빈 파서가 S 를 받는 것과 같고, 정상 문장이니 SENTENCE. ∎ — G 의 길이·내용과 무관하다는 점이 핵심이다. `'$'` 를 IDLE 에서만 받는 구현(심은 버그 2)은 이 정리가 깨진다.

**선언형 참조와의 동치**: 테스트의 `ref_scan()` 은 상태 없이 "각 `'$'` 위치에서 본문 글자 L ≤ max 개 → `'*'` → hex 2 → (`\r`)`\n` → XOR 일치" 를 정규식처럼 찾는다. FSM 이 수락하는 문장은 정리 1 에 의해 이 패턴이고, 이 패턴인 부분 문자열은 그 `'$'` 에서 정리 2 의 논리로 FSM 이 수락한다. 그래서 두 구현의 수락 목록(끝 위치 + 본문)이 같아야 한다. 실측: 무작위 스트림 1,500개 (정상 / 비트 오류 / 잘림 / 길이 초과 / 쓰레기를 섞음, 한도 76·20·127) 에서 **수락 424,503개, 불일치 0**.

수락 수(424,503)가 정상 조각 수(424,302)보다 201개 많다 — 손상시킨 조각 일부도 정말로 유효한 문장이기 때문이다. 예: 16진 `'A'`(0x41)의 비트 5 를 뒤집으면 `'a'`(0x61) 로 **같은 값**이고, 잘린 문장이 마침 `*hh` 직후에서 끊기고 다음 쓰레기 조각이 `'\n'` 으로 시작하면 완전한 문장이 된다. 이건 버그가 아니라 정의상 맞는 수락이고, 두 구현이 그 201개까지 똑같이 판정했다.

### Part B — 필드 분리와 XOR 체크섬의 한계

```text
"GPGGA,,,,,,0,00,99.99,,,,,,"      (실제 u-blox no-fix GGA 본문, *48)
 nmea_split  → 15 필드: [GPGGA][][][][][][0][00][99.99][][][][][][]
 strtok(",") → 4 토큰:  [GPGGA][0][00][99.99]   ← f[6] 이 fix 품질이어야 하는데 "99.99" 가 들어온다
```

NMEA 는 **필드 위치가 의미**다 (GGA 의 6번 = fix 품질). 값이 없을 때 필드를 지우지 않고 비워 두므로, 구분자 여러 개를 하나로 합치는 `strtok` 이나 B15 의 토큰화(공백 여러 개 = 구분자 하나)를 그대로 쓰면 인덱스가 밀린다. `nmea_split` 은 쉼표 하나 = 경계 하나다. 불변식: 루프 시작마다 `fields[0..n)` 은 원래 버퍼 안의 NUL 종단 필드이고 `p` 는 다음 필드의 첫 글자 — 따라서 필드 수 = 쉼표 수 + 1, 다시 `','` 로 이으면 원래 문자열 (전수 테스트가 바로 이걸 확인: 32,767개, 0 wrong).

**XOR 는 GF(2)⁸ 위의 선형 함수다**: `ck(x ⊕ e) = ck(x) ⊕ ck(e)`. 그래서 오류 패턴 e (바뀐 바이트들의 차이) 는 **e 의 바이트들을 다 XOR 한 값이 0 일 때, 그때만** 못 잡는다.

| 오류 종류 | XOR 의 판정 | 실측 (GGA 본문 61자) |
|---|---|---|
| 1글자 치환 | 차이 `a ⊕ a' ≠ 0` → **항상 잡음** | 5,551개 중 놓침 0 |
| 2글자 치환 | `a ⊕ a' = b ⊕ b'` 이면 놓침 | 15,154,230개 중 145,976 (0.963%) — 연습 4 |
| 자리 바꿈 (순열) | XOR 는 교환법칙 → **절대 못 잡음** | 인접 바꿈 55개 중 55, 무작위 순열 1,000개 중 1,000 |
| 홀수 개 비트가 한 비트 열에서 뒤집힘 | 그 비트 열의 패리티가 바뀜 → 잡음 | (8개 비트 열 각각의 패리티 = 세로 패리티, LRC) |

`"4807.038"` 을 `"4870.038"` 로 바꿔도 체크섬이 통과한다 (테스트에 있음) — 위도 48°07′ 가 48°70′ 이 되는데, 이건 **범위 검사(분 ≤ 59)** 가 잡는다. 이게 Part C 에서 모든 필드를 엄격하게 검사하는 이유다: 체크섬은 선로의 랜덤 비트 오류용이고, 의미 검사가 그 사각지대를 메운다. 더 강한 검출이 필요하면 CRC (ALGO-M01) 를 쓰는 바이너리 프로토콜(u-blox UBX 는 8비트 Fletcher 계열 2바이트 체크섬, ALGO-B14 참고)로 바꾼다.

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">실측 GPS 로그 (solution.c, 737바이트) — 에러 뒤 항상 다음 '$' 에서 복구</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="40" height="34" class="f-mut"/>
    <rect x="50" y="40" width="200" height="34" class="f-ok"/>
    <rect x="250" y="40" width="40" height="34" class="f-warn"/>
    <rect x="290" y="40" width="50" height="34" class="f-ok"/>
    <rect x="340" y="40" width="50" height="34" class="f-warn"/>
    <rect x="390" y="40" width="30" height="34" class="f-mut"/>
    <rect x="420" y="40" width="60" height="34" class="f-warn"/>
    <rect x="480" y="40" width="250" height="34" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="30" y="62">배너</text><text x="150" y="62">GGA RMC GSA GSV</text><text x="270" y="62">잘림</text>
    <text x="315" y="62">GGA</text><text x="365" y="62">비트오류</text><text x="405" y="62">FF</text>
    <text x="450" y="62">89자</text><text x="605" y="62">GLL(LF) VTG GGA(no-fix) GGA</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="96">이벤트: SENTENCE ×4 → TRUNCATED → SENTENCE → CHECKSUM → (FF 는 IDLE 에서 junk)</text>
    <text x="10" y="114">        → TOOLONG → (나머지는 junk) → SENTENCE ×4    — 에러마다 다음 '$' 에서 다시 BODY</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="10" y="134">sentences=9 checksum=1 truncated=1 toolong=1 junk=38 | GGA ok=4 other=5</text>
    <text x="10" y="156">마지막 fix: lat_e7=533613367 lon_e7=-65056200 time_ms=34070000</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="186">같은 737바이트를 DMA 덩어리 1~64바이트로 무작위로 잘라 200번 넣어도 카운터·fix 가 바이트 단위로 같다:</text>
    <text x="10" y="206">파서 상태가 구조체 하나에 다 있으므로 "어디서 끊겼나" 는 결과에 영향을 줄 수 없다.</text>
  </g>
</svg>
```

### Part C — 위경도를 1e-7 도 정수로

`ddmm.mmmm` 은 **도(degree) + 분(minute)** 이다. 소수 도가 아니다 — `4807.038` 은 48.07038° 가 아니라 48° 7.038′ = 48.1173°. 이 실수가 실제 제품에서 수 km 오차를 만든다.

유도: 도 d, 분의 정수부 m, 소수부 k자리 정수 r → 분을 정수 `M = m·10ᵏ + r` (단위 10⁻ᵏ 분) 로 둔다. 1도 = 60분 = `D = 60·10ᵏ` 단위. 원하는 값은

  `v = d·10⁷ + M·10⁷ / D`  (1e-7 도 단위, 유리수)

가장 가까운 정수 = `d·10⁷ + ⌊(M·10⁷ + D/2) / D⌋` (D 는 짝수라 `D/2` 는 정확). **증명**: N = M·10⁷ 이라 하고 `q = ⌊(N + D/2)/D⌋` 이면 `qD ≤ N + D/2 < (q+1)D`, 즉 `−D/2 ≤ N − qD < D/2` → `|N/D − q| ≤ 1/2`. 등호는 `N/D = q − 1/2` 일 때뿐이므로 동률은 **올림** — 크기에 적용한 뒤 부호를 붙이니 "0 에서 먼 쪽". 동률은 실제로 생긴다: k = 6 에서 `M ≡ 3 (mod 6)` 이면 `M·10⁷/D` 의 소수부가 정확히 .5 (테스트 `"0000.000003"` → ±1).

| 입력 | d | M (k) | D | `(M·10⁷ + D/2) / D` | 결과 |
|---|---|---|---|---|---|
| `4807.038` N | 48 | 7038 (3) | 60,000 | 70,380,030,000 / 60,000 = 1,173,000 | **481,173,000** |
| `01131.000` E | 11 | 31000 (3) | 60,000 | 310,000,030,000 / 60,000 = 5,166,667 (.17 버림) | **115,166,667** |
| `00630.3372` W | 6 | 303372 (4) | 600,000 | 3,033,720,300,000 / 600,000 = 5,056,200 | **−65,056,200** |
| `8959.99999999` N | 89 | 5,999,999,999 (8) | 6·10⁹ | 9,999,999.998… → 10,000,000 | **900,000,000** (반올림으로 90°) |

넘침 한계: k ≤ 8 (9자리 이상은 거절) 이면 `M < 60·10⁸` → `M·10⁷ < 6·10¹⁶ < 2⁶⁴ ≈ 1.8·10¹⁹`. 결과 크기 ≤ 180·10⁷ = 1.8·10⁹ < 2³¹ − 1 = 2,147,483,647 → `int32_t` 에 들어간다. (8자리는 고정밀 수신기 문장 `12202.26957864` 에서 실제로 나온다.)

**검증 방식**: 무작위 200,000개 (도, 분, k = 0..8, 반구) 를 문자열로 만들어 파싱한 뒤, 정의 그 자체 `2·|r·D − M·10⁷| ≤ D` (r = 결과 − d·10⁷) 를 64비트 정수로 확인 → **0 wrong**. 같은 입력을 `llround((d + M/D)·1e7)` (double) 로 계산하면 **288개가 1 차이** — double 이 틀린 것이다 (반올림 경계 근처에서 이진 근사 오차). 테스트 쪽 참조로 double 을 쓰려면 ±1 을 허용해야 하는 이유이기도 하다.

**왜 `float` 이 아닌가**: Cortex-M4F 의 FPU 는 단정밀도(24비트 가수)다. 180° 근처에서 `float` 의 ulp = 2⁷⁻²³ = 2⁻¹⁶ ≈ 1.53·10⁻⁵ 도 ≈ **1.7 m** (적도에서 1° ≈ 111.3 km). 48° 에서도 2⁻¹⁸ 도 ≈ 0.42 m. 반면 1e-7 도 정수는 위도 방향 약 1.1 cm 이고, u-blox UBX-NAV-PVT 와 MAVLink `GPS_RAW_INT` 가 쓰는 것도 같은 `int32` 1e-7 도 형식이다. `double` 은 M4 에 하드웨어가 없어 소프트웨어 에뮬레이션이다.

**GGA 검사 순서** (= 에러 우선순위, 표 테스트 18개가 못 박음):

| 순서 | 필드 | 규칙 | 에러 |
|---|---|---|---|
| 1 | 0 | 대문자 2글자 talker + `GGA` (`GP`, `GN`, `GL`, `GA`, `GB`...) | NOTGGA |
| 2 | – | 필드 정확히 15개 | FIELDS |
| 3 | 1 | 비면 `has_time=false`, 아니면 `hhmmss[.s+]`, 윤초 `60` 허용, 소수 4자리부터 버림 | TIME |
| 4 | 6 | 한 자리 `0..8` (0 무효, 1 GPS, 2 DGPS, 4 RTK fixed, 5 RTK float, 6 추측 항법 …) | QUALITY |
| 5 | 2,3 / 4,5 | 넷 다 비고 quality 0 → 위치 없음. 아니면 둘 다 엄격히 | LAT / LON |
| 6 | 7 | 1~2자리 | SATS |
| 7 | 8 | 비거나 ≤ 99.99 → ×100 | HDOP |
| 8 | 9,10 | 둘 다 비거나, 값 + `"M"` → cm | ALT |

quality 를 위치보다 먼저 보는 이유: "위치가 비어도 되는가" 가 quality 에 달려 있다. 실패하면 `*out` 을 0 으로 지운다 — 반쯤 채운 위치가 밖으로 새면 "유효한 0,0 (기니만)" 으로 보인다.

### 퍼즈 — 무엇이 무엇을 잡나 (`demo_fuzz.c`)

`demo_fuzz.c` 는 `nmea_feed` 를 복사해 `-DBUG=n` 으로 버그 하나씩 심고, 쓰레기(3모드) + 정상 문장 300만 개 (5.46억 바이트) 를 ASan+UBSan 빌드로 돌린다. 실측 (각 3.2~3.6 s CPU):

| BUG | 심은 버그 | 속성 퍼즈 (틀린 수락 / 복구 실패 / len>max) | 참조 차분 |
|---|---|---|---|
| 0 | 없음 | 0 / 0 / 0 | 0 |
| 1 | TOOLONG `>=` → `>` | 0 / 0 / **1,723,675** | 0 |
| 2 | `'$'` 를 IDLE 에서만 | 0 / **291,383** / 0 | **301,932** |
| 3 | 재시작 때 `ck` 초기화 누락 | **548** / **276,200** / 0 | **283,164** |
| 4 | 느슨한 hex (`'G'` → 0) | **450** / 0 / 0 | **450** |
| 5 | 소문자 hex 거절 | 0 / 0 / 0 | **12,868** |

- **BUG 1 은 ASan/UBSan 이 이 퍼저에선 못 잡았다**: 넘친 문장이 `*hh\r\n` 으로 끝나야 `body[128]` 쓰기가 일어나는데, 무작위 생성기가 "정확히 한도 + 1 글자인 정상 문장" 을 만들지 못했다. 불변식 `len ≤ max_body` 검사가 잡았다. 같은 버그를 `solution.c` 에 심으면 차분 테스트(한도 근처 길이를 일부러 만듦)가 FAIL 하고 UBSan 이 `index 128 out of bounds for type 'char[128]'` 를 찍는다 (실측) — **경계를 겨냥한 생성기**가 필요하다.
- **BUG 3 의 548 건**은 정리 1 이 깨진 것이다: 묵은 `ck` 와 섞인 값이 우연히 `*hh` 와 같아 체크섬이 틀린 문장이 수락됐다.
- **BUG 5 는 속성 퍼즈가 못 잡는다** (거절만 많아질 뿐 수락·복구 성질은 유지). 정답을 아는 참조와의 차분, 또는 표 테스트("lower-case hex")만 잡는다. B15 와 같은 결론: 퍼저는 크래시·불변식, 값의 정답은 참조/표.

### Cortex-M 에서의 비용 (`demo_m4.c`, `demo_bench.c`)

`demo_m4.c` 는 `solution.c` 를 include 해 함수를 바깥으로 노출하는 래퍼만 둔다. 실측 (컴파일만, 링크 안 함):

```bash
SDK=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -Os -fstack-usage -isystem $SDK/usr/include \
      -c demo_m4.c -o /tmp/m4.o && cat /tmp/m4.su && llvm-nm --size-sort -S /tmp/m4.o && llvm-size /tmp/m4.o
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -Os -isystem $SDK/usr/include -S demo_m4.c -o - | grep -E '\bbl\b'
# Cortex-M0: --target=thumbv6m-none-eabi -mcpu=cortex-m0
```


| 함수 | M4 `.text` (B) | M4 스택 (B) | M0 `.text` | M0 스택 |
|---|---|---|---|---|
| `nmea_feed` (ISR 후보) | 312 | **0** | 312 | 16 |
| `nmea_split` (래퍼에 인라인) | 70 | 8 | 54 | 16 |
| `parse_hms_ms` | 202 | 24 | 196 | 40 |
| `parse_latlon_e7` | 418 | 48 | 480 | 72 |
| `nmea_parse_gga` | 396 | 64 | 416 | 64 |
| `gps_feed` | 212 | 176 | 196 | 168 |
| 오브젝트 전체 `text` | 1,806 | | 1,868 | |

- `parse_latlon_e7` 의 64비트 나눗셈은 M4 에서도 **`bl __aeabi_uldivmod`** (라이브러리 호출) 이다 — M4 의 `udiv` 는 32비트뿐. M0 에서는 64비트 곱셈도 `__aeabi_lmul` 5회, `parse_hms_ms` 의 `scale /= 10` 은 `__aeabi_uidiv` (M4 는 `umull` 매직 곱셈으로 바뀜). 그래서 좌표 변환은 ISR 이 아니라 **태스크**에서 한다. ISR 에는 0바이트 스택, 분기 몇 개짜리 `nmea_feed` 만 둔다.
- `gps_feed` 의 176 B 스택은 `char *f[24]` (M4 에서 96 B) + `gga_t` 임시값이다. 태스크 스택 예산에 넣을 숫자.

처리량 (이 맥, Apple Silicon, -O2, 4 MB 로그, 워밍업 + 9회 중앙값, 3회 실행 모두 비슷):

```text
stream 4194278 bytes (563 B per 10-line round): sentences=67049 GGA=22349 other=44700 junk=22350
nmea_feed only        : median 1.97 ns/byte  (min 1.72, max 2.11)
gps_feed (FSM+split+GGA): median 2.55 ns/byte  (min 2.41, max 2.83)
  UART   9600 baud: one byte every   1041667 ns -> parser uses 0.00025% of that (gps_feed, this Mac)
  UART 115200 baud: one byte every     86806 ns -> parser uses 0.00294% of that (gps_feed, this Mac)
  UART 921600 baud: one byte every     10851 ns -> parser uses 0.02354% of that (gps_feed, this Mac)
```

절대값은 맥 기준이라 의미가 적고, 비율이 핵심이다: 168 MHz M4 가 맥보다 수십 배 느려도 바이트 간격(115200 에서 86.8 µs)에 비하면 파싱은 무시할 만하다. 진짜 제약은 **대역폭**이다 — 위 로그 한 바퀴 563 B 는 9600 baud (960 B/s) 에서 0.59 초. 1 Hz 에 문장 9개면 들어가지만 10 Hz 로 올리면 안 들어가므로 보레이트를 올리거나 필요 없는 문장(GSV 등)을 꺼야 한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o solution_san && ./solution_san  # 깨끗
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench
cc -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -DBUG=0 demo_fuzz.c -o demo_fuzz && ./demo_fuzz
```

실측 출력 (`./solution`, 0.37 s; ASan+UBSan 빌드 0.69 s, 리포트 없음):

```text
sample checksums: 12/12 match the published *hh
FSM table: 23/23 cases ok
differential: 1500 streams, 424503 accepted sentences (424302 valid pieces), mismatches 0
split exhaustive: 32767 strings, 0 wrong
single-char substitutions: 5551 tried, 0 undetected
lat/lon random: 200000 parsed, 0 wrong, 288 differ from double llround by 1
GGA error table: 18/18 cases ok
gps log (737 bytes): sentences=9 checksum=1 truncated=1 toolong=1 junk=38 | GGA ok=4 other=5
fuzz random bytes   : 20000 cases, 0 sentences accepted inside the garbage
fuzz token soup     : 20000 cases, 1 sentences accepted inside the garbage
fuzz mutated samples: 20000 cases, 88 sentences accepted inside the garbage
fuzz total: 60000 cases, 89 in-garbage accepts, bad accepts 0, no recovery 0, len>max 0
62/62 passed
```

"쓰레기 안에서 수락 89개" 는 변이된 예시 문장 중 손상이 안 된(또는 손상이 무해한) 것들이고, 89개 모두 `verify_accept` 를 통과했다. 완전 무작위 바이트 20,000개 안에서는 하나도 수락되지 않았다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `'$'` 를 IDLE 에서만 문장 시작으로 봄 | 잘린 문장 다음 정상 문장을 통째로 잃는다 (퍼즈 실측 29만 건 복구 실패) | `switch` 앞에서 상태 무관하게 처리 |
| 재시작 때 `len` 만 0 으로 | 묵은 `ck` 가 섞여 정상 문장 거절 + **틀린 문장 수락** (실측 548건) | `len = 0; ck = 0;` 둘 다 |
| `if (len > max_body)` | 한 글자 넘쳐 `body[128]` 쓰기 (UBSan 실측) | `len >= max_body` 를 저장 **전에** |
| `*hh` 두 글자에서 바로 수락 | `"*31x..."` 처럼 줄 끝이 깨진·잘린 문장 수락 | CR/LF 까지 받고 비교 |
| `hex = c <= '9' ? c-'0' : (c|0x20)-'a'+10` | `'G'`, `'z'` 도 숫자로 통과 → 틀린 체크섬 수락 (실측 450건) | 세 범위를 명시적으로, 아니면 -1 |
| 16진 대문자만 | 소문자를 내는 장비·로그 거절 | 둘 다 받기 (보낼 땐 대문자) |
| `strtok(body, ",")` | 빈 필드를 삼켜 GGA 인덱스가 밀림 (no-fix 문장에서 quality 자리에 HDOP) | 쉼표 하나 = 경계 하나 (`nmea_split`) |
| `atof("4807.038")` 을 도로 사용 | ddmm.mmmm 은 도+분. 48.07° ≠ 48.1173° (약 5 km) | `d + M/(60·10ᵏ)` |
| `float lat = ...` | 180° 근처 ulp 1.7 m, 48° 에서 0.42 m | `int32_t` 1e-7 도 |
| `(M * 10000000) / D` (반올림 없음) | 항상 버림 → 최대 1e-7 도 치우침, `8959.99999999` 가 90° 가 안 됨 | `+ D/2` |
| 분 `60` 이상 허용 | `4870.038` (자리 바뀜) 이 체크섬과 함께 통과 | `min ≤ 59`, 도 ≤ 90/180, 90°면 분 = 0 |
| ISR 에서 `nmea_parse_gga` 까지 | `__aeabi_uldivmod`, 문자열 비교, 64 B+ 스택이 ISR 에 | ISR 은 `nmea_feed` 만, 완성 문장은 큐로 태스크에 |
| SENTENCE 직후 `p->body` 를 나중에 읽음 | 다음 `'$'` 이후 덮어써짐 | 이벤트 즉시 분리·복사 (또는 이중 버퍼, Q3) |

## 8. 면접 Q&A

<details>
<summary>Q1. 왜 줄 단위(readline + split)가 아니라 바이트 단위 FSM 인가요?</summary>

① 바이트가 ISR/DMA 에서 아무 경계로나 온다 — 줄 버퍼를 따로 두면 그 버퍼의 넘침·잘림을 또 처리해야 하는데, FSM 은 그 자체가 줄 버퍼다. ② `'$'` 재동기가 공짜다: 줄 단위 파서는 잘린 줄이 다음 줄과 붙어 `"$GPGGA,1235$GPGLL,..."` 한 줄이 되고, 그 줄 전체를 버리면 정상인 GLL 까지 잃는다. ③ 상태가 구조체 하나라 DMA 덩어리 크기와 결과가 무관하다 (덩어리 1~64 무작위 200회 실측, 결과 동일). ④ 바이트당 일이 상수라 최악 실행 시간이 정해진다 — ISR 예산을 숫자로 말할 수 있다.

</details>

<details>
<summary>Q2. "절대 틀린 체크섬을 수락하지 않는다" 와 "항상 복구한다" 를 증명해 보세요.</summary>

불변식: BODY 이후 상태에서 `body[0..len)` = 마지막 `'$'` 이후 저장된 본문, `ck` = 그 XOR, `len ≤ max`. `'$'` 가 `(len, ck) = (0, 0)` 으로 세우고, 저장과 XOR 는 같은 전이에서 같이 일어나므로 유지된다. SENTENCE 는 `'$' → 본문 → '*' → hex → hex → [\r] → \n` 경로를 `'$'` 없이 지난 뒤 `ck == ck_rx` 일 때만 → 수락된 문장의 체크섬은 맞다 (정리 1). 복구: `'$'` 전이가 현재 상태와 무관하므로 정상 문장 S 의 `'$'` 이후 파서는 새 파서와 같고, S 안에 `'$'` 가 없으니 S 는 수락된다 (정리 2). 테스트에서는 원본 바이트만 보는 `verify_accept` 와 G+S 퍼즈 60,000건(데모 300만 건)으로 확인했다. 단, "체크섬이 맞다" 가 "내용이 맞다" 는 아니다 — XOR 가 못 잡는 오류는 Q4.

</details>

<details>
<summary>Q3. ISR 에는 무엇을 두고 태스크에는 무엇을 두나요? 데이터는 어떻게 넘기나요?</summary>

ISR (또는 DMA half/full 콜백): `nmea_feed` 만 — 0 B 스택, 루프 없음, 라이브러리 호출 없음 (M4 실측). SENTENCE 가 나오면 본문을 **문장 큐**(고정 크기 슬롯 몇 개짜리 SPSC 링, DS-B03/B04)에 복사하고 태스크를 깨운다 (`xTaskNotifyFromISR` 등). 큐가 차면 문장을 버리고 카운터를 올린다 — ISR 이 기다리면 안 된다. 태스크: 분리, GGA/RMC 파싱 (`__aeabi_uldivmod`, `strcmp`, 64 B 스택), 위치 갱신. 대안은 ISR 이 바이트만 링 버퍼에 넣고 태스크가 `nmea_feed` 까지 다 하는 구성 — ISR 이 가장 짧고, 이 레슨의 `gps_feed` 가 그 모양이다. 어느 쪽이든 `p->body` 는 다음 `'$'` 에서 덮이므로 SENTENCE 이벤트 직후에 소비하거나 복사해야 한다. 115200 baud 면 바이트 간격 86.8 µs 라 바이트 ISR 도 가능하지만, 921600 이상이나 여러 UART 면 DMA + idle-line 인터럽트가 보통이다.

</details>

<details>
<summary>Q4. XOR 체크섬은 무엇을 놓치나요? 그럼 왜 아직 쓰나요?</summary>

XOR 는 선형이라 오류 패턴의 바이트 XOR 가 0 이면 못 잡는다. 실측(GGA 본문 61자): 1글자 치환 5,551개 전부 잡음, 2글자 치환 15,154,230개 중 0.963% 놓침, **자리 바꿈은 100% 놓침** (`4807` → `4870`). 같은 비트 위치에서 짝수 개 비트 오류도 놓친다 (세로 패리티라서). 그래도 쓰는 이유: 1980년대 표준이고 8비트 MCU 에서 바이트당 XOR 한 번이며, NMEA 의 주 위험(선로의 드문 랜덤 비트 오류) 에는 충분하다. 보완: 필드 의미 검사(분 ≤ 59, 위도 ≤ 90, 날짜 유효성), 연속 문장 간 일관성(속도로 설명 안 되는 위치 점프 거르기), 그래도 부족하면 CRC 가 있는 바이너리 프로토콜 (UBX, RTCM3 의 CRC-24Q — RTCM 은 확인 권장).

</details>

<details>
<summary>Q5. 82자 제한을 넘는 문장이 오면요? max_body 를 무한히 키우면 안 되나요?</summary>

표준 NMEA 0183 은 `'$'` 부터 CR LF 까지 82자로 알려져 있다 (표준 문서가 유료라 원문 대조는 확인 권장). 하지만 실제 고정밀 수신기는 넘긴다 — 이 레슨의 Trimble 형식 GGA 예시는 본문 83자(전체 89바이트)로, 위경도 소수 8자리 때문이다. 그래서 한도를 `nmea_init(p, max_body)` 런타임 값으로 두고 버퍼는 컴파일 시간 상한(`NMEA_BODY_CAP` 128) 으로 둔다: 표준 76 이면 TOOLONG, 100 이면 수락 (둘 다 테스트). 무한히 키우면 안 되는 이유: 한도 = 버퍼 크기 = 파서 하나의 최악 RAM 이고, 문장 큐(Q3)를 두면 슬롯 수만큼 곱해진다. 또 쓰레기에서 `'$'` 하나가 나온 뒤 `'*'` 가 안 오면 한도만큼 바이트를 모았다가 버리므로, 한도가 크면 "가짜 문장" 하나가 오래 살아 있다 (다음 진짜 `'$'` 가 오면 바로 끊기므로 정상 문장을 잃지는 않는다 — 정리 2). 장비가 실제로 내는 최대 길이 + 약간으로 정한다.

</details>

<details>
<summary>Q6. 위경도를 float 로 저장하면 안 되나요? double 은요?</summary>

`float` 의 가수는 24비트: 180° 근처 ulp 2⁻¹⁶ 도 ≈ 1.7 m, 48° 에서 0.42 m. RTK (cm 급) 는 물론 일반 GPS (≈1~3 m) 보다도 거칠어진다. `double` 은 정밀도는 충분하지만 Cortex-M4F 에는 배정밀도 FPU 가 없어 소프트웨어 에뮬레이션이고(M7 일부는 있음), 이 레슨 실측에서 double 로 계산한 반올림은 200,000개 중 288개가 정수 정답과 1 달랐다. `int32_t` 1e-7 도는 위도 방향 약 1.1 cm 해상도, 비교·차분이 정수 연산, 바이너리 프로토콜(UBX, MAVLink) 과 같은 형식이라 변환이 없다. 경도 1e-7 도의 실제 길이는 `cos(위도)` 배로 짧아진다.

</details>

<details>
<summary>Q7. 이 파서를 어떻게 퍼징했나요? 무엇을 확인했나요?</summary>

고정 시드 xorshift32 로 세 종류 쓰레기 — 완전 무작위 바이트 / `$ * , \r \n` 와 hex 위주 토큰 수프 / 실제 문장 변이(비트 뒤집기, 바이트 바꾸기, 삭제, 긴 본문, 소문자 hex) — 를 만들고 그 뒤에 정상 문장을 붙인다. 속성 세 가지: ① 모든 SENTENCE 를 원본 바이트로 재확인 (틀린 수락 0) ② 마지막 정상 문장 수락 (복구) ③ `len ≤ max_body`, 그리고 ASan+UBSan. 추가로 정답을 아는 **선언형 참조 스캐너**와 수락 목록을 바이트 위치까지 비교. 심은 버그 실험의 교훈: 속성만으로는 소문자 hex 거절(BUG 5) 같은 "덜 받는" 버그를 못 잡고, 무작위 생성기는 "정확히 한도 + 1" 같은 경계를 거의 만들지 못한다 (BUG 1) — 경계를 겨냥한 생성기 + 참조 차분 + 표 테스트를 같이 둔다. Apple clang 에는 libFuzzer 런타임이 없어 (B15 에서 확인) 커버리지 기반 퍼징은 다른 LLVM 배포판이나 Linux 에서 한다.

</details>

<details>
<summary>Q8. 시각 필드만으로 시간을 쓸 수 있나요? 윤초, 날짜, 1 Hz 펄스는요?</summary>

GGA 시각은 UTC `hhmmss.ss` 뿐이라 날짜가 없다 — 자정을 넘으면 0 으로 돌아간다. 날짜는 RMC (`ddmmyy`, 두 자리 연도 — 연습 1) 나 ZDA (네 자리 연도) 에서 온다. 윤초: 이 파서는 `ss = 60` 을 받아 들인다 (수신기가 실제로 내는지는 기종마다 다름 — 확인 권장). 그리고 문장은 그 시각이 지난 **뒤** 수십~수백 ms 늦게 도착하므로, 정밀 시각 동기에는 PPS (1 pulse-per-second) 핀의 엣지를 타이머 캡처로 잡고 NMEA 문장은 "그 펄스가 몇 시 몇 초였는지" 라벨로만 쓴다. 로봇에서 센서 퓨전 타임스탬프를 NMEA 수신 시각으로 찍는 것은 흔한 실수다.

</details>

## 9. 추가 연습

### 연습 1 — RMC 와 날짜 → Unix 시각

`nmea_parse_rmc()` 로 상태(`A`/`V`), 위경도, 속도(knot) → mm/s, 침로(×100), `ddmmyy` → 날짜 검증(윤년) → Unix ms 를 만든다. NMEA 2.3 의 모드 필드, 4.1 의 항법 상태 필드까지 필드 수 12/13/14 를 받는다. LeetCode 1360 Number of Days Between Two Dates 와 같은 날짜 계산.

<details>
<summary>답</summary>

속도: 1 knot = 1852 m / 3600 s 이므로 `knot×1000` 을 정수로 읽고 `(kn·1852 + 1800) / 3600` (반올림) → `022.4` → 11,524 mm/s. 날짜 → 일수는 Howard Hinnant 의 `days_from_civil`: 3월을 해의 시작으로 옮기면 2월(윤일)이 해 끝에 와서 `doy = (153·m' + 2)/5 + d − 1` (m' = 3월부터 센 달 번호 0..11) 같은 닫힌 식이 되고, 400년 = 146,097일 주기로 나눈다. 검증: 1970-01-01 부터 하루씩 세는 기준과 1970~2099 의 47,482일 전수 대조 0 wrong, `230394 123519` → 764,426,119,000 ms (1994-03-23 12:35:19Z, 파이썬 `calendar.timegm` 로 계산한 값). 두 자리 연도는 `yy ≥ 80 → 19yy` 로 피벗했다 — 관례이지 표준 규칙이 아니다 (확인 권장). `V` (무효) 에서는 위치가 비어도 된다.

</details>

### 연습 2 — GSV 묶음 조립

GSV 는 위성 4개씩 여러 문장(`total`, `num`) 으로 나뉜다. `gsv_feed()` 로 묶음을 조립해 완성되면 COMPLETE, 번호 누락·묶음 도중 새 1번·`total`/위성 수 불일치면 ERROR. 빈 SNR = 추적 안 됨(−1), 마지막 문장의 빈 칸 묶음 `,,,,` 은 건너뛴다.

<details>
<summary>답</summary>

상태는 `next` (기다리는 번호, 0 = 묶음 밖) 하나 — 문장 단위의 작은 FSM 이다. `num == 1` 이면 언제나 새 묶음 (이전 미완성은 에러 1회로 버림), 그 외엔 `num == next` 이고 `total`, `in_view` 가 첫 문장과 같아야 한다. 마지막 문장에서 모은 수 == `in_view` 확인. u-blox 문서 예시로 알려진 3문장 묶음 (`*74`, `*74`, `*4D` — 셋 다 직접 계산해 일치) 으로 11개 위성 조립을 확인했다. NMEA 4.1 은 끝에 signal ID 필드를 하나 더 붙여 `(n − 4) % 4 == 1` 이 된다 — 이 답은 받지 않는다 (확인 권장).

</details>

### 연습 3 — 생성기와 왕복 성질

`nmea_build(dst, cap, body)` 로 `"$body*HH\r\n"` 을 만든다. 본문 글자 위반·76자 초과·`cap` 부족이면 0. 무작위 본문 50,000개를 build → FSM 에 넣어 같은 본문이 나오는지 (왕복, round-trip).

<details>
<summary>답</summary>

필요 길이 = `1 + L + 3 + 2` (+ NUL). 경계: 41바이트 GLL 은 `cap = 42` 에서 성공, 41 에서 실패 (테스트). 76자 본문 → 정확히 82바이트, 77자 → 0. 생성기와 파서가 같은 `is_body_char` 를 쓰는 것이 왕복 성질의 전제다. 시뮬레이터·HIL 테스트 장비에서 가짜 GPS 를 만들 때 바로 이 함수가 필요하고, 파서 퍼저의 "정상 문장" 공급원도 된다.

</details>

### 연습 4 — XOR 체크섬 사각지대 전수 측정

GGA 본문 하나에 대해 1글자 치환 전부, 서로 다른 두 위치의 2글자 치환 전부(본문 글자 92종), 인접 자리 바꿈 전부를 세어 못 잡는 비율을 구한다.

<details>
<summary>답</summary>

2글자 치환은 `(a ⊕ a') == (b ⊕ b')` 일 때만 놓치므로 위치 쌍마다 O(1) 로 판정할 수 있다. 실측: 1글자 5,551개 놓침 0, 2글자 15,154,230개 놓침 145,976 (0.963%), 인접 바꿈 55개 전부 놓침. 0.963% 가 1/256 ≈ 0.39% 보다 큰 이유: 인쇄 가능 ASCII 끼리의 차이는 7비트(0x01..0x7F) 안에 몰리고 그 분포도 고르지 않아, 두 차이가 우연히 같을 확률이 커진다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
# days_from_civil: 47482 dates checked, 0 wrong
# XOR blind spots on a 61-char GGA body (92 body chars):
#   1-char substitutions : 5551, undetected 0
#   2-char substitutions : 15154230, undetected 145976 (0.963%)
#   adjacent swaps       : 55, undetected 55 (100%)
# 27/27 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o practice_san && ./practice_san
```

## 10. 핵심 정리

- 스트림 파서 = 바이트당 상태 하나 바꾸기. `IDLE → BODY → CK1 → CK2 → CR → LF`, 에러는 이벤트 하나 내고 IDLE.
- `'$'` 는 **상태 무관** 리셋 (`len = 0`, `ck = 0` 둘 다) — 이 한 줄이 "다음 `'$'` 에서 반드시 복구" 의 증명이다.
- 불변식 `ck = XOR(body[0..len))`, `len ≤ max` → 수락된 문장은 원본에서 `$body*hh` 이고 `hh = XOR(body)`. 비교는 LF 에서.
- 필드는 위치가 의미: 쉼표 하나 = 경계 하나, 빈 필드 유지. `strtok` 금지.
- XOR 는 선형: 1글자 오류 100% 검출, 자리 바꿈 0%, 2글자 치환 약 1% 놓침 → 필드 범위 검사가 사각지대를 메운다.
- `ddmm.mmmm` 은 도+분. 1e-7 도 = `d·10⁷ + ⌊(M·10⁷ + D/2)/D⌋`, D = 60·10ᵏ, `uint64_t` 중간값, `int32_t` 결과. `float` 은 180° 에서 1.7 m.
- ISR 엔 `nmea_feed` (M4 0 B 스택), 태스크엔 분리·GGA (`__aeabi_uldivmod`). 진짜 제약은 CPU 가 아니라 UART 대역폭.
- 테스트는 세 겹: 표(경계) + 참조 차분(값) + 퍼즈(크래시·불변식). 어느 하나만으로는 심은 버그 5개를 다 못 잡았다.

## 참고자료

- NMEA 0183 (National Marine Electronics Association) — 표준 원문은 유료. 문장 구조·82자 제한·체크섬 정의는 아래 수신기 문서들로 교차 확인 (확인 권장)
- u-blox, *u-blox 8 / M8 Receiver Description incl. Protocol Specification* — NMEA 문장(GGA/RMC/GSV), fix 품질 값, UBX 체크섬, UBX-NAV-PVT 의 1e-7 도 형식. 이 레슨의 `092725.00` GGA, `083559.00` RMC, GSV 3문장 예시가 이 계열 문서의 예시로 알려져 있다 (체크섬은 직접 계산해 일치 확인)
- Dale DePriest, "NMEA data" (gpsinformation.org) — `$GPGGA,123519,...*47`, `$GPRMC,123519,...*6A`, `$GPGSA ... *39`, `$GPGSV,2,1,08 ... *75` 의 고전 예시 (체크섬 직접 확인)
- Trimble, NMEA-0183 message 문서 — 고정밀 GGA 예시 (`3723.46587704`, 89바이트) (출처 확인 권장)
- MAVLink `GPS_RAW_INT` (lat/lon `int32` degE7) — <https://mavlink.io/en/messages/common.html#GPS_RAW_INT>
- Howard Hinnant, "chrono-Compatible Low-Level Date Algorithms" (`days_from_civil`) — <https://howardhinnant.github.io/date_algorithms.html>
- ARM, *Run-time ABI for the Arm Architecture* — `__aeabi_uldivmod`, `__aeabi_lmul`, `__aeabi_uidiv`
- [AddressSanitizer](https://clang.llvm.org/docs/AddressSanitizer.html), [UBSan](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)
- 이 트랙: ALGO-B15 (줄 단위 토큰화·퍼즈), ALGO-B14 (단순 체크섬), ALGO-M01 (CRC), DS-B03/B04 (ISR→태스크 링 버퍼)
- AlgoMonster 노트 M01 §18 Simulation (Don 정리본) — 상태 정의 + 단계별 전이 + 엣지 케이스
