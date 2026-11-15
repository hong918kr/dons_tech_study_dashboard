---
id: COMM-M10
time: 90분
updated: 2026-11-11
---
# COMM-M10 견고한 패킷 파서: 재동기화·타임아웃·통계

## 학습 목표

- u-blox UBX 프레임(`B5 62 | CLASS ID LEN_L LEN_H | PAYLOAD | CK_A CK_B`)을 바이트 하나씩 받는 상태 머신(state machine)으로 파싱하고, 알려진 프레임 `B5 62 0A 04 00 00 0E 34`(UBX-MON-VER poll)를 코드로 직접 재현한다.
- 손상된 링크에서 정상 프레임을 잃지 않게 하는 세 장치를 단다: **길이 가드**(max length guard), **바이트 간 타임아웃**(inter-byte timeout, 타임스탬프 기반 + 타이머 경로), **버퍼 안 재검색**(in-buffer rescan: 링크에서 다시 읽지 않고 이미 먹은 바이트에서 sync 를 다시 찾기).
- 오류 종류별 카운터(`sync_skip / bad_len / ck_fail / timeout / overflow / frames_ok`)를 두고, bring-up 때 그 숫자만 보고 "baud 가 틀렸다 / 노이즈다 / 메인 루프가 느리다" 를 가려낸다.
- 비트 뒤집기·바이트 유실·삽입·잘림·버스트·가짜 sync 를 주입한 고정 시드 스트림으로 **복원율**(recovery rate)과 **거짓 수락**(false accept)을 직접 측정해, naive 파서 대비 숫자로 말한다.
- 8비트 Fletcher 체크섬이 CRC 가 **아닌** 이유를 2비트 오류 전수 실험과 식으로 설명한다.

## 1. 문제

면접관이 "GPS 모듈 UART 파서 짜 보세요 → 바이트가 하나 빠지면? 중간에 켜지면? 쓰레기가 sync 처럼 보이면?" 을 묻는 이유: **현장에서 망가지는 건 프레임 포맷이 아니라 동기(sync)** 이고, 동기를 잃은 파서가 정상 프레임을 몇 개 더 버리느냐가 드론·로봇의 위치 데이터 공백이 된다.

COMM-B10 은 1바이트 SOF + LEN + CRC16 형식에서 "가짜 SOF 다음 바이트부터 다시 훑는" 재동기화를 만들었다. 이 레슨은 그 다음 단계다:

| | COMM-B10 | **COMM-M10 (이 레슨)** |
|---|---|---|
| 형식 | 내가 정한 `[A5][LEN][TYPE][..][CRC16]` | 실제 형식 **u-blox UBX** (2바이트 sync, LE16 길이, Fletcher) |
| 무결성 | CRC-16/CCITT-FALSE | 8비트 Fletcher 2개 — 약점까지 측정 |
| 미완성 프레임 | 연습으로 flush | **타임스탬프 타임아웃 + 타이머 경로**(`ubx_tick`) |
| 측정 | 쓰레기 비율별 복구 | 손상 **종류별**·**비율별** 복원율, 지연(latency), 거짓 수락, 바이트당 비용 |
| 펌웨어 | — | ISR 링 버퍼(바이트+타임스탬프, overflow), 통계 로그 줄, ACK 대기, 링크 진단 |

**UBX 프레임** (u-blox M8 Receiver Description 의 UBX 절 기준 — 문서 확인 권장. 길이 가드 256 은 **우리 펌웨어의 정의**):

| 오프셋 | 필드 | 값 / 의미 |
|---|---|---|
| 0, 1 | SYNC | `0xB5 0x62` (문자로 "µb") |
| 2 | CLASS | 메시지 그룹 (0x01 NAV, 0x05 ACK, 0x06 CFG, 0x0A MON …) |
| 3 | ID | 그룹 안 메시지 번호 |
| 4, 5 | LEN | PAYLOAD 바이트 수, **little-endian** 16비트. 이 파서는 0..256 만 받는다 |
| 6 .. 6+LEN−1 | PAYLOAD | 아무 바이트 (B5 62 포함) |
| 끝 2 | CK_A, CK_B | CLASS..PAYLOAD 에 대해 `A += b; B += A;` (둘 다 mod 256) |

MAVLink(v1: 시작 0xFE, CRC-16/MCRF4XX + CRC_EXTRA)도 같은 "sync + 길이 + 체크" 계열이지만, 이 레슨에선 CRC_EXTRA 를 두 가지 독립 방법으로 확인한 알려진 프레임이 없어 **일부러 다루지 않았다**. 원리는 같다 — 길이 가드 + 타임아웃 + rescan 은 그대로 옮겨 갈 수 있다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">UBX-MON-VER poll = B5 62 0A 04 00 00 0E 34 (solution.c 인코더 출력 = 파이썬 독립 계산)</text>
  <g stroke="currentColor">
    <rect x="30" y="50" width="80" height="44" class="f-acc"/>
    <rect x="115" y="50" width="80" height="44" class="f-acc"/>
    <rect x="200" y="50" width="80" height="44" class="f-mut"/>
    <rect x="285" y="50" width="80" height="44" class="f-mut"/>
    <rect x="370" y="50" width="80" height="44" class="f-mut"/>
    <rect x="455" y="50" width="80" height="44" class="f-mut"/>
    <rect x="540" y="50" width="80" height="44" class="f-warn"/>
    <rect x="625" y="50" width="80" height="44" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="38" y="66">SYNC1</text><text x="123" y="66">SYNC2</text><text x="208" y="66">CLASS</text><text x="293" y="66">ID</text>
    <text x="378" y="66">LEN_L</text><text x="463" y="66">LEN_H</text><text x="548" y="66">CK_A</text><text x="633" y="66">CK_B</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="14">
    <text x="58" y="86">B5</text><text x="143" y="86">62</text><text x="228" y="86">0A</text><text x="313" y="86">04</text>
    <text x="398" y="86">00</text><text x="483" y="86">00</text><text x="568" y="86">0E</text><text x="653" y="86">34</text>
  </g>
  <line x1="200" y1="108" x2="535" y2="108" stroke="currentColor"/>
  <line x1="200" y1="100" x2="200" y2="116" stroke="currentColor"/><line x1="535" y1="100" x2="535" y2="116" stroke="currentColor"/>
  <text x="210" y="132" fill="currentColor" font-size="12">체크섬 범위 = CLASS..PAYLOAD (sync 제외, 이 프레임은 PAYLOAD 0바이트)</text>
  <text x="380" y="148" fill="currentColor" font-size="12">LEN = 0x0000 (LE: 하위 바이트 먼저)</text>
  <text x="10" y="180" fill="currentColor" font-weight="bold">8비트 Fletcher 누적 (A += b, B += A, mod 256)</text>
  <g fill="currentColor" font-family="monospace" font-size="13">
    <text x="30" y="206">b :</text><text x="228" y="206">0A</text><text x="313" y="206">04</text><text x="398" y="206">00</text><text x="483" y="206">00</text>
    <text x="30" y="230">A :</text><text x="228" y="230">0A</text><text x="313" y="230">0E</text><text x="398" y="230">0E</text><text x="483" y="230">0E</text><text x="568" y="230">→ CK_A 0E</text>
    <text x="30" y="254">B :</text><text x="228" y="254">0A</text><text x="313" y="254">18</text><text x="398" y="254">26</text><text x="483" y="254">34</text><text x="568" y="254">→ CK_B 34</text>
  </g>
  <text x="10" y="288" fill="currentColor" font-size="12">B = Σ (남은 바이트 수) × b  →  위치 가중 합. "가중치가 있는 덧셈" 일 뿐 다항식 나눗셈(CRC)이 아니다 — 약점은 5. 풀이</text>
</svg>
```

### Part A — 체크섬, 인코더, 바이트 단위 파서

1. `ubx_checksum(p, n, &a, &b)`: 8비트 Fletcher. MON-VER poll 의 `0A 04 00 00` → `0E 34`.
2. `ubx_encode(cls, id, pl, len, out, cap)`: `len > 256` → `UBX_ERR_LEN`, `cap < len + 8` → `UBX_ERR_CAP` (둘 다 `out` 을 건드리지 않음). 아니면 쓴 바이트 수.
3. `ubx_step(p, b)`: 상태 9개 `SYNC1 → SYNC2 → CLASS → ID → LEN1 → LEN2 → PAYLOAD → CK_A → CK_B`. sync 뒤 바이트는 전부 `raw[262]` 에 기록. **`B5 B5 62`** 에서 두 번째 B5 를 sync 로 잡을 것. `LEN2` 에서 길이 가드.
4. 테스트: MON-VER / NAV-PVT poll / 4바이트 프레임 바이트 정확히, 용량 경계 11/12, LEN 0..256 전수 왕복(PAYLOAD 를 `B5 62 ..` 로 채워서, naive·rescan 둘 다), `00 B5 13 B5` 뒤 MON-VER → 받음 + `sync_skip == 4`.

### Part B — 오류 주입: 길이 가드, rescan, 타임아웃, 체크섬 약점

모든 오류 경로는 카운터를 올리고 `SYNC1` 로 간다. **naive** 모드는 거기서 끝(먹은 바이트는 버림), **rescan** 모드는 `ubx_run` 이 가짜 sync 다음 바이트부터 `raw[]` 를 다시 훑는다. 주입과 단언:

| # | 주입 | 기대 결과 (테스트로 확인) |
|---|---|---|
| B1 | LEN = 0x0101 (257) 헤더, 바로 뒤 Y | 둘 다 `bad_len` 1, Y 받음 (헤더 4바이트 만에 끊음) |
| B2 | X 의 CLASS·ID·PAYLOAD·CK 단일 비트 96가지 | 전부 `ck_fail` 1, Y 받음 (두 모드) |
| B3 | X 의 LEN 16비트 각각 뒤집기, 뒤에 Y Z | naive 는 32개 중 11개 잃음, rescan 0개 |
| B4 | 잘린 X(앞 9바이트) 바로 뒤 Y Z | naive 1/2, rescan 2/2 |
| B5 | X 의 PAYLOAD 1바이트 유실 | naive 1/2, rescan 2/2 |
| B6 | 쓰레기 `3C B5 62 05 01 40 00 77 88` (가짜 LEN 64) | naive 0/2, rescan 2/2 |
| B7 | 수신이 PAYLOAD 속 가짜 헤더(`B5 62 01 02 20 00`)부터 시작 | naive 0/2, rescan 2/2 |
| B8 | X 안에 바이트 1개 삽입 | 두 모드 모두 Y 받음 (`ck_fail` 1) — 삽입은 유실보다 덜 아프다 |
| B9 | 잘린 X + 3 ms 침묵 + Y Z / 느린 송신기 900·1000·1001 µs | 타임아웃 있으면 naive 도 Y Z 받음 (`timeout` 1). 1000 µs 는 통과, 1001 µs 는 프레임 못 받음 |
| B10 | 같은 홀짝 위치 두 바이트 bit7 뒤집기 / 다른 홀짝 / `B5 62 00×6` | **틀린 데이터 통과** / `ck_fail` / "유효한" 빈 프레임 — Fletcher 약점 |

### Part C — 펌웨어 쪽: 타이머 경로, ISR 링 버퍼, 측정

- `ubx_tick(p, now)`: 바이트가 더 안 와도(라인이 조용해져도) `now − last_t > T` 이면 미완성 프레임을 정리하고 그 안에 갇힌 진짜 프레임을 꺼낸다. 가짜 헤더(LEN 200) 뒤에 진짜 Y 가 오고 조용해진 경우: `last+T` 에서는 0, `last+T+1` 에서 Y 1개. 32비트 µs 타임스탬프 랩(0xFFFFFF00 → 0x100) 에서 오탐 없음.
- `rx_isr_push(ring, byte, t)`: UART RX ISR 이 64칸 링(바이트 + 수신 시각)에 넣기만, 가득 차면 버리고 `overflow++`. 메인 루프 `rx_poll` 이 꺼내 `ubx_feed`. 메인 루프가 멈춰 160바이트를 버린 경우, **버린 바이트와 겹치지 않은 프레임 118개 전부** 받음.
- `ubx_stats_line()`: `ubx ok=118 skip=5 len=0 ck=0 to=1 ovf=160 rescan=0` — snprintf 규칙대로 잘림을 반환값으로 알린다.
- 고정 시드 스트림(프레임 3000, 손상 10%, 6종 섞음, PAYLOAD 30% 에 가짜 헤더)을 naive / naive+T / rescan / rescan+T 로 돌려 복원율·거짓 수락·지연을 비교하고, 큰 측정은 `demo_recovery.c` 로.

## 2. Problem (English)

> **Robust UBX stream parser.** A GNSS receiver streams u-blox UBX frames over UART:
>
> `[0xB5][0x62][CLASS][ID][LEN_L][LEN_H][PAYLOAD × LEN][CK_A][CK_B]`, LEN little-endian,
> `CK_A/CK_B` = 8-bit Fletcher over CLASS..PAYLOAD (`A += b; B += A;` mod 256).
>
> The link drops, inserts and flips bytes, the MCU may boot mid-frame, and random noise can contain `B5 62`.
> Write an ISR-friendly parser, no dynamic allocation:
>
> ```c
> void ubx_init(ubx_parser *p, bool rescan, uint32_t timeout_us, ubx_cb cb, void *ctx);
> int  ubx_encode(uint8_t cls, uint8_t id, const uint8_t *pl, size_t len, uint8_t *out, size_t cap);
> int  ubx_feed(ubx_parser *p, uint8_t byte, uint32_t t_us);   // one received byte + its timestamp
> int  ubx_tick(ubx_parser *p, uint32_t now_us);               // called from a timer when no bytes arrive
> ```
>
> Requirements: (1) reject `LEN > 256` at the header; (2) if the gap between two bytes inside a frame exceeds
> `timeout_us`, abandon the partial frame; (3) after any failure (bad length, bad checksum, timeout), resume the
> sync search **from the byte after the false sync, inside the bytes already consumed** — never re-read the link,
> bounded buffer; (4) keep counters `frames_ok, sync_skip, bad_len, ck_fail, timeout, overflow`.
>
> Example: `ubx_encode(0x0A, 0x04, NULL, 0, out, 16)` → 8, `out = B5 62 0A 04 00 00 0E 34`. Feeding those bytes
> returns `0,0,0,0,0,0,0,1`.
>
> Follow-ups: How many intact frames does a naive parser lose per byte drop? Prove your rescan buffer never
> overflows and bound the work per byte. What is the false-accept rate of Fletcher-16 vs CRC-16? How do you choose
> the timeout at 9600 vs 921600 baud, or behind a USB-serial bridge?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여덟 개: A1 `ubx_checksum`, A2 `ubx_encode`, A3 `ubx_step`, B1 `ubx_run` 의 rescan 블록, B2a `ubx_flush`, B2b `ubx_feed` 의 간격 검사, C1 `ubx_tick`, C2 `rx_isr_push`. 하네스·스트림 생성기·`rx_poll`·`ubx_stats_line` 은 주어진다. 스텁은 "체크섬 0 / ERR_CAP / EV_NONE / 아무것도 안 함 / false" 를 돌려주므로 모든 루프가 끝난다. 처음 실행 (실제 출력, 앞부분):

```text
FAIL starter.c:375  a == 0x0E && b == 0x34
FAIL starter.c:376  ubx_encode(0x0A, 0x04, NULL, 0, out, sizeof out) == 8 && memcmp(out, monver, 8) == 0
FAIL starter.c:377  ubx_encode(0x01, 0x07, NULL, 0, out, sizeof out) == 8 && memcmp(out, navpvt, 8) == 0
…
15/54 passed
```

(exit 1.) 단계별 확인: A1–A3 만 채우면 **39/54** (B 표의 "rescan 은 받는다" 와 타임아웃·C 가 실패), B1·B2 까지 채우면 **50/54** (C1 타이머 경로, C2 링, C3 의 지연 비교가 남음).

## 4. 힌트

<details><summary>힌트 1 — Fletcher 는 두 줄</summary>

`a = (uint8_t)(a + p[i]); b = (uint8_t)(b + a);` — `uint8_t` 로 캐스트하면 mod 256 이 공짜다. 범위는 CLASS 부터 PAYLOAD 끝까지(= 인코더에서는 `out + 2`, `len + 4` 바이트). 파서에서는 sync 직후 `a = b = 0` 으로 초기화하고 CLASS 부터 바이트마다 누적한다 — 마지막에 한 번에 계산하지 않아 바이트당 일이 고르다.

</details>

<details><summary>힌트 2 — SYNC2 에서 B5 가 오면?</summary>

`B5 B5 62 …` 에서 SYNC2 상태가 "62 아니면 무조건 SYNC1" 이면 두 번째 B5 를 버려서 진짜 프레임을 잃는다. 규칙: 62 → 프레임 시작, **B5 → SYNC2 유지**(앞의 B5 만 버림), 그 외 → SYNC1. 2바이트 sync 에서 흔히 나는 버그다(KMP 의 실패 함수를 아주 작게 쓴 것과 같다).

</details>

<details><summary>힌트 3 — 길이는 LEN_H 를 받는 순간 검사</summary>

`len = raw[2] | (b << 8)` 을 만들자마자 `len > 256` 이면 `bad_len` — `raw[]` 에 PAYLOAD 를 쓰기 **전에**. 그러면 `raw[]` 는 4 + 256 + 2 = 262 바이트를 넘을 수 없다. LEN = 0 이면 PAYLOAD 를 건너뛰고 바로 CK_A.

</details>

<details><summary>힌트 4 — rescan: 목록 하나와 인덱스 두 개</summary>

오류가 나면 "다시 훑을 목록" = (가짜 sync 뒤에 먹은 바이트 `raw[0..raw_n)`) + (목록의 아직 안 본 부분 `q[i..qn)`). `memmove(q + raw_n, q + i, rest); memcpy(q, raw, raw_n); qn = raw_n + rest; i = 0;` 새 목록은 옛 목록의 접미사(가짜 sync 2바이트 뒤)라서 최소 2바이트 짧다 → `q[262]` 하나로 충분하고 루프는 끝난다. 재귀 금지.

</details>

<details><summary>힌트 5 — 타임아웃은 "바이트가 올 때" 와 "안 올 때" 두 경로</summary>

`ubx_feed` 는 새 바이트의 시각과 `last_t` 의 차가 T 를 넘고 프레임 중간이면 먼저 `ubx_flush`. 하지만 바이트가 **더 안 오면** 이 검사는 영원히 안 돈다 → 타이머 ISR(또는 UART receiver-timeout / IDLE 인터럽트) 뒤에 `ubx_tick(now)`. 뺄셈은 `(uint32_t)(now − last_t)` — 32비트 µs 카운터가 71.6분마다 넘쳐도 맞다. `ubx_flush` 는 rescan 모드면 `raw[]` 를 다시 훑고, 그 끝에서 또 프레임 중간이면 그것도 버리고 다시(매번 짧아짐).

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 |
|---|---|
| 1. 형식 | sync 2바이트, 헤더 4, LEN LE16, 체크섬 범위(sync 제외), 길이 상한(버퍼 = 최악 손실 구간) |
| 2. 상태 머신 | 바이트 하나 = 호출 하나, 상태 9개, 체크섬 누적, `B5 B5 62` |
| 3. 실패 모드 | 비트 오류는 그 프레임만, **길이 오류·유실·잘림·중간 시작은 다음 프레임까지** — 바이트로 보여 주기 |
| 4. 복구 | 길이 가드(가짜 sync 의 99.6% 를 4바이트 만에 제거) → rescan(삼킨 바이트 다시 보기) → 타임아웃(라인이 조용해지면 정리) |
| 5. 관측 | 종류별 카운터, 로그 한 줄, 진단 규칙 |
| 6. 한계 | Fletcher 거짓 수락(2비트 HD), 지연(가짜 긴 프레임 뒤의 진짜 프레임은 늦게 나온다), CPU 최악 비용 |

### Part A — 설계 결정

| 결정 | 이유 |
|---|---|
| 길이 가드 256 | UBX LEN 필드는 65535 까지지만 이 펌웨어가 쓰는 메시지(NAV-PVT 92 B 등)는 작다. 상한이 `raw[]` 크기와 **최악 손실 구간**을 정한다. 덤으로 랜덤 2바이트가 LEN 으로 통과할 확률이 257/65536 ≈ 0.39% → 가짜 sync 의 **99.6%** 를 헤더에서 걸러 낸다 (demo 3c: 760번 중 757번) |
| sync 뒤 모든 바이트를 `raw[]` 에 | PAYLOAD 위치(`raw + 4`)가 자동으로 정해지고, rescan 의 재료가 된다 |
| 체크섬 바이트마다 누적 | 마지막 바이트에서 256바이트를 돌지 않음 → ISR 지연이 고르다 |
| rescan 작업 목록 `q[]` 를 구조체 안에 | 262바이트를 ISR 스택에 올리지 않는다 (작은 MSP 스택) |
| 콜백에 `raw + 4` 포인터 | 복사 없음. 대신 "콜백 안에서만 유효" 를 문서화 (다음 바이트가 덮는다) |

MON-VER poll 을 한 바이트씩 넣은 추적 (값은 코드 출력과 일치):

| 입력 | 상태(전) | 한 일 | CK_A | CK_B | 상태(후) | 반환 |
|---|---|---|---|---|---|---|
| B5 | SYNC1 | sync 1 | — | — | SYNC2 | 0 |
| 62 | SYNC2 | 프레임 시작, raw_n = 0, A = B = 0 | 00 | 00 | CLASS | 0 |
| 0A | CLASS | raw[0] | 0A | 0A | ID | 0 |
| 04 | ID | raw[1] | 0E | 18 | LEN1 | 0 |
| 00 | LEN1 | raw[2] | 0E | 26 | LEN2 | 0 |
| 00 | LEN2 | len = 0 ≤ 256 → PAYLOAD 건너뜀 | 0E | 34 | CK_A | 0 |
| 0E | CK_A | raw[4] 저장 | | | CK_B | 0 |
| 34 | CK_B | 0E == 0E, 34 == 34 → 전달 | | | SYNC1 | **1** |

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs>
    <marker id="m10a" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="8" markerHeight="8" orient="auto">
      <path d="M0,0 L8,4 L0,8 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">ubx_step(): 상태 9개 (화살표 하나 = 바이트 하나)</text>
  <g stroke="currentColor">
    <rect x="20" y="60" width="100" height="40" rx="6" class="f-acc"/>
    <rect x="160" y="60" width="100" height="40" rx="6" class="f-acc"/>
    <rect x="300" y="60" width="100" height="40" rx="6" class="f-mut"/>
    <rect x="440" y="60" width="100" height="40" rx="6" class="f-mut"/>
    <rect x="580" y="60" width="100" height="40" rx="6" class="f-mut"/>
    <rect x="580" y="180" width="100" height="40" rx="6" class="f-mut"/>
    <rect x="440" y="180" width="100" height="40" rx="6" class="f-ok"/>
    <rect x="300" y="180" width="100" height="40" rx="6" class="f-warn"/>
    <rect x="160" y="180" width="100" height="40" rx="6" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="45" y="85">SYNC1</text><text x="185" y="85">SYNC2</text><text x="325" y="85">CLASS</text>
    <text x="478" y="85">ID</text><text x="610" y="85">LEN1</text><text x="610" y="205">LEN2</text>
    <text x="455" y="205">PAYLOAD</text><text x="330" y="205">CK_A</text><text x="190" y="205">CK_B</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m10a)">
    <line x1="120" y1="80" x2="158" y2="80"/>
    <line x1="260" y1="80" x2="298" y2="80"/>
    <line x1="400" y1="80" x2="438" y2="80"/>
    <line x1="540" y1="80" x2="578" y2="80"/>
    <line x1="630" y1="100" x2="630" y2="178"/>
    <line x1="580" y1="200" x2="542" y2="200"/>
    <line x1="440" y1="200" x2="402" y2="200"/>
    <line x1="300" y1="200" x2="262" y2="200"/>
    <path d="M45,60 C30,32 95,32 80,58"/>
    <path d="M185,60 C170,32 235,32 220,58"/>
    <path d="M475,180 C460,152 525,152 510,178"/>
    <path d="M200,100 C180,128 110,128 92,102"/>
    <path d="M160,200 H70 V102"/>
    <path d="M630,220 V262 H40 V102"/>
    <path d="M590,220 C560,250 380,250 360,222" stroke-dasharray="4,3"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="22" y="30">B5 아님: sync_skip</text>
    <text x="128" y="74">B5</text><text x="268" y="74">62</text>
    <text x="200" y="30">B5: 유지</text>
    <text x="112" y="136">그 외</text>
    <text x="640" y="145">→ len 조립</text>
    <text x="450" y="146">LEN 바이트</text>
    <text x="420" y="248">LEN = 0 → 바로 CK_A</text>
    <text x="76" y="238">CK 일치 → frames_ok, 전달</text>
    <text x="76" y="254">CK 불일치 → ck_fail</text>
    <text x="150" y="278">LEN &gt; 256 → bad_len  (raw[] 에 쓰기 전에)</text>
  </g>
  <text x="10" y="304" fill="currentColor" font-size="12">CLASS..CK_B 의 어느 상태든: 바이트 간격 &gt; T → ubx_flush (timeout++) — feed 경로 또는 ubx_tick 타이머 경로</text>
  <text x="10" y="322" fill="currentColor" font-size="12">rescan 모드: 오류 화살표(bad_len, ck_fail, timeout)를 탈 때 raw[] = 가짜 sync 뒤 바이트를 SYNC1 부터 다시 훑는다</text>
</svg>
```

### Part B — 오류가 "다음 프레임까지" 번지는 이유

UBX 는 길이 prefix 형식이라, 파서가 **어디까지가 이 프레임인지를 LEN 으로만** 안다. 그래서 손상 종류별로 피해가 다르다 (`solution.c` 실제 출력):

```text
B3 LEN bit flips (16): naive lost 11 of 32 Y/Z frames, rescan lost 0
B4 truncated X then Y Z:        naive got 1/2 (errors 1), rescan got 2/2 (errors 1)
B5 byte drop in X then Y Z:     naive got 1/2 (errors 1), rescan got 2/2 (errors 1)
B6 garbage A5..fake sync LEN64: naive got 0/2 (errors 1), rescan got 2/2 (errors 1)
B7 start mid-frame (fake hdr):  naive got 0/2 (errors 1), rescan got 2/2 (errors 1)
```

- **비트 뒤집기(CLASS·ID·PAYLOAD·CK)**: 길이가 그대로라 경계가 맞다 → 그 프레임만 `ck_fail`. 단일 비트 오류는 `A` 를 ±2^k 바꾸므로(mod 256 에서 0 이 아님) **항상** 걸린다 — 96가지 전부 확인.
- **LEN 비트 뒤집기**: 짧아지면 PAYLOAD 바이트를 CK 로 읽고 실패, 길어지면 뒤 프레임을 삼킨다. LEN_H 비트는 전부 > 256 이라 길이 가드가 즉시 끊는다(그래서 16가지 중 피해는 LEN_L 쪽). naive 는 삼킨 구간 안의 Y, Z 의 sync 를 같이 버린다 — 32개 중 11개 손실.
- **바이트 유실**: X 가 1바이트 모자라 Y 의 `B5` 를 CK_B 로 먹는다 → naive 는 Y 를 잃는다(Y 의 sync 가 사라짐). 
- **바이트 삽입**: X 가 1바이트 남아 원래 CK_B 한 바이트만 떠돈다 → Y 의 sync 는 멀쩡 → naive 도 Y 를 받는다. **유실이 삽입보다 훨씬 비싸다** (demo 표 1: naive 91.00% vs 99.78%).
- **가짜 sync + 그럴듯한 LEN** (쓰레기, 또는 PAYLOAD 안에 다른 UBX 헤더가 있는데 수신이 중간에 시작): 가짜 LEN 만큼 진짜 프레임들을 삼킨다. 길이 가드는 LEN 0x40 같은 "그럴듯한" 값은 못 거른다.

**in-buffer rescan.** 해결은 COMM-B10 과 같은 생각이다: 오류가 나면 **가짜 sync 바로 다음 바이트부터 이미 먹은 바이트를 다시 훑는다**. 링크에서 다시 읽을 수는 없으니(UART 는 되감기가 없다) 파서가 sync 뒤 바이트를 `raw[]` 에 갖고 있어야 한다.

```c
while (i < qn) {
    ubx_event ev = ubx_step(p, p->q[i++]);
    if (ev == EV_FRAME) { ubx_deliver(p); frames++; }
    else if (ev != EV_NONE && p->rescan) {
        size_t rest = qn - i;                          /* 아직 안 본 바이트 */
        memmove(p->q + p->raw_n, p->q + i, rest);      /* 뒤로 밀고 */
        memcpy(p->q, p->raw, p->raw_n);                /* 가짜 sync 뒤 바이트를 앞에 */
        qn = p->raw_n + rest; i = 0; p->stats.rescans++;
    }
}
```

**버퍼가 넘치지 않음 (증명).** `ubx_feed` 의 첫 패스는 목록 = 새 바이트 1개. 여기서 오류가 나면 새 목록 = `raw` 전체 ≤ 262 (`rest = 0`). 그 뒤 모든 패스는 `SYNC1` 에서 시작하므로, 다음 오류의 `raw` 는 **목록 안** 어떤 sync 쌍(위치 s, s+1) 뒤의 바이트뿐 → 새 목록 = 목록[s+2 ..], 최소 2바이트 짧다. 그래서 `q[262]` 로 충분하고, `memmove` 목적지 `raw_n ≤ i − 2` 는 원본보다 앞이라 안전하며, 루프는 끝난다. 한 번의 `ubx_run` 최악 step 수 ≤ 1 + 262 + 260 + … + 2 = **17,293**. `ubx_feed` 는 flush(같은 사슬, ≤ 17,292) + run 이므로 **≤ 34,585** (C3 테스트가 이 상한을 단언). 실측 최악은 135~303 step, 평균은 입력 바이트당 1.00~1.40 (demo 표 2).

**왜 정상 프레임을 잃지 않나.** 진짜 프레임의 sync 가 위치 P 에 있고 파서가 그보다 앞의 가짜 sync 에 걸려 있다고 하자. 가짜 프레임은 (a) 오류로 끝나 P 를 포함한 뒷부분이 다시 훑어지거나, (b) 체크섬이 우연히 맞아 거짓 수락되거나, (c) 미완성으로 남는다. (b) 만 손실을 만들고, (c) 는 타임아웃이 (a) 로 바꾼다 — 아래.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs>
    <marker id="m10b" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="9" markerHeight="9" markerUnits="userSpaceOnUse" orient="auto">
      <path d="M0,0 L8,4 L0,8 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">B6: 쓰레기 속 가짜 sync (LEN 64) 가 Y·Z 를 삼킨다 → rescan 이 꺼낸다</text>
  <g stroke="currentColor">
    <rect x="20" y="50" width="30" height="30" class="f-mut"/>
    <rect x="52" y="50" width="124" height="30" class="f-warn"/>
    <rect x="178" y="50" width="50" height="30" class="f-mut"/>
    <rect x="230" y="50" width="140" height="30" class="f-ok"/>
    <rect x="372" y="50" width="160" height="30" class="f-ok"/>
    <rect x="534" y="50" width="188" height="30" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="26" y="70">3C</text><text x="58" y="70">B5 62 05 01 40 00</text><text x="184" y="70">77 88</text>
    <text x="240" y="70">Y (12 B, B5 62 …)</text><text x="384" y="70">Z (14 B, B5 62 …)</text><text x="547" y="70">이후 바이트 …</text>
  </g>
  <line x1="96" y1="94" x2="640" y2="94" stroke="currentColor" stroke-width="2"/>
  <line x1="96" y1="86" x2="96" y2="102" stroke="currentColor"/><line x1="640" y1="86" x2="640" y2="102" stroke="currentColor"/>
  <text x="150" y="118" fill="currentColor" font-size="12">가짜 프레임: sync 뒤 4 + 64 + 2 = 70바이트를 기다린 뒤 ck_fail</text>
  <text x="150" y="142" fill="currentColor" font-size="12">naive: 이 70바이트를 버림 → Y·Z 의 sync 도 같이 버려져 0/2</text>
  <path d="M640,160 H100 V100" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#m10b)"/>
  <text x="150" y="180" fill="currentColor" font-size="12">rescan: raw[] = 05 01 40 00 77 88 Y Z … 를 SYNC1 부터 다시 → Y, Z 전달 (2/2)</text>
  <text x="10" y="222" fill="currentColor" font-weight="bold">C1: 가짜 헤더 (LEN 200) 뒤 진짜 Y, 그리고 라인이 조용해짐</text>
  <g stroke="currentColor">
    <rect x="20" y="240" width="124" height="30" class="f-warn"/>
    <rect x="146" y="240" width="150" height="30" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="26" y="260">B5 62 01 02 C8 00</text><text x="160" y="260">Y (12 B)</text>
  </g>
  <line x1="296" y1="255" x2="720" y2="255" stroke="currentColor" stroke-dasharray="2,4"/>
  <line x1="296" y1="236" x2="296" y2="290" stroke="currentColor"/><line x1="480" y1="236" x2="480" y2="290" stroke="currentColor"/>
  <text x="300" y="304" fill="currentColor" font-size="12">last_t</text><text x="430" y="304" fill="currentColor" font-size="12">last_t+T+1: ubx_tick → flush → rescan → Y</text>
  <text x="306" y="248" fill="currentColor" font-size="12">침묵 (바이트 없음: feed 경로는 안 돈다)</text>
</svg>
```

### Part B — 타임아웃: 무엇을 해결하고 무엇을 못 하나

UBX 수신기는 측위 주기(epoch)마다 메시지 몇 개를 **연달아** 보내고 다음 주기까지 조용하다. 바이트 간 타임아웃 T 는 이 구조를 쓴다.

| 상황 | 타임아웃 없이 | 타임아웃(T)으로 |
|---|---|---|
| 송신 쪽 리셋으로 잘린 프레임 + 침묵 | 다음 주기 첫 프레임(들)을 삼킴 (naive: 잃음) | 침묵에서 정리 → 다음 주기 정상 (demo: naive 88.63% → 100.00%) |
| 주기 마지막 프레임이 가짜 긴 프레임에 갇힘 | 다음 주기 바이트가 와야 풀림 → **20 ms+ 지연** | `last+T+1` 에 꺼냄 (C1 테스트) |
| 프레임 사이 간격 0 (연달아) 에서의 유실 | — | 못 푼다: 간격이 없으니 T 가 안 걸린다 → rescan 몫 |

T 고르기: **프레임 안 최대 바이트 간격 < T < 프레임 사이 최소 침묵**. 115200 baud 에서 바이트는 86.8 µs, 이 레슨은 T = 1 ms(≈ 11.5 바이트 시간). 9600 baud(u-blox 기본값으로 알려짐 — 확인 권장)면 바이트가 1.04 ms 라 T 는 수 ms 이상. USB-UART 브리지 뒤의 호스트라면 바이트가 USB 패킷 단위로 몰려 오고(FTDI latency timer 기본 16 ms 로 알려짐 — 확인 권장) 타임스탬프도 그 단위라, T 를 그보다 크게 잡아야 한다. 경계 테스트(B9): 바이트 간격 1000 µs 는 통과, 1001 µs 는 매 바이트가 타임아웃 → 프레임을 영영 못 받는다. **T 가 송신기보다 짧으면 파서가 링크를 스스로 죽인다** — T 는 여유 있게.

하드웨어 도움: STM32 USART 의 IDLE 플래그(라인이 1 프레임 시간 이상 idle)와, 신형 계열(F7/L4/G4/H7 등)의 receiver timeout(RTOR/RTOF) 가 바로 이 타이머 역할이다 (RM 확인 권장). 그 인터럽트에서 `ubx_tick` 을 부르면 된다. DMA + IDLE 은 COMM-M11.

### Part B — Fletcher 는 CRC 가 아니다

`B = Σ w_i·b_i (mod 256)`, `w_i` = i 번째 바이트 뒤로 남은 바이트 수(자신 포함), `A = Σ b_i`. 두 비트 오류(바이트 i < j, 비트 k 와 m)가 안 걸리려면 ΔA ≡ 0 이고 ΔB ≡ 0 (mod 256):

- k ≠ m 이면 ±2^k ± 2^m 은 256 의 배수가 아니다 → 항상 검출.
- **k = 7**: +128 ≡ −128 (mod 256) 이라 ΔA = ±128 ± 128 ≡ 0 이 **항상** 성립. ΔB = 128·(w_i + w_j) ≡ 128·(j − i) → **(j − i) 가 짝수면 미검출.** 비트 값과 무관.
- k < 7: ΔA = 0 이려면 한쪽은 0→1, 다른 쪽은 1→0 (두 바이트의 그 비트가 달라야). ΔB = 2^k·(j − i) ≡ 0 → (j − i) 가 2^(8−k) 의 배수.

그래서 20바이트 영역에서 bit7 쌍만 C(10,2)×2 = 90 개(B10 테스트: `PAYLOAD[0]`, `PAYLOAD[2]` bit7 → 틀린 데이터 통과), bit 6 은 거리 4의 배수 40쌍 중 비트가 다른 것(평균 20), bit 5 는 거리 8·16 의 16쌍 중 절반, bit 4 는 거리 16 의 4쌍 중 절반 → 기대 ≈ 120/프레임. 실측(demo 표 3a): **1000 프레임 × 12720 쌍 중 120,074 미검출(0.944%), 그중 bit7 쌍 90,000**, CRC-16/CCITT-FALSE 는 **0** (이 길이에서 HD ≥ 3 이라 2비트 오류를 전부 잡는다). 연습 2 가 예측식 = 전수 탐색을 60개 영역에서 확인한다.

또 하나: 헤더와 PAYLOAD 가 전부 0 이면 체크섬도 `00 00` 이다 → `B5 62 00 00 00 00 00 00` 은 "유효한" 프레임(B10 테스트). 라인이 low 로 붙어(break) 0x00 이 쏟아지는 상황에서 가짜 sync 한 번이면 빈 CLASS 0 프레임이 올라온다. CLASS/ID 표(연습 1)가 막는다.

랜덤한 큰 손상(긴 버스트, 랜덤 쓰레기)에서는 어떤 16비트 검사든 ≈ 2^−16 이다: 표 3b 의 1..16 바이트 랜덤 버스트에서 Fletcher 1.10e−5, CRC-16 1.75e−5 (둘 다 2^−16 = 1.53e−5 근처의 잡음 범위). 즉 **Fletcher 의 약점은 "구조적인 작은 오류"** 에 있다 — 그리고 UART 의 실제 오류(baud 오차로 MSB 근처가 틀어지는 비트 오류, COMM-B03)는 하필 구조적이다.

### Part C — ISR, 링 버퍼, 타이머, 로그

```text
ISR (RXNE)          : rx_isr_push(ring, DR, TIM->CNT)   ← 몇 사이클. 가득 차면 overflow++
타이머 / IDLE / RTO  : 플래그만 세우거나 메인 루프에 알림
메인 루프 / 태스크    : rx_poll(ring, parser)  → ubx_feed → 콜백(디스패치)
                      ubx_tick(parser, now)   ← 조용할 때 갇힌 프레임 정리
1 초마다             : ubx_stats_line() → 로그, link_diag() (연습 4)
```

왜 파서를 ISR 에 두지 않나: 평균은 바이트당 1 step 남짓이지만 **최악 한 바이트에 수백 step**(실측 303, 상한 34,585)과 콜백이 몰린다. 그동안 다음 바이트가 RDR 에 덮이면 하드웨어 overrun(STM32 ORE). ISR 은 바이트와 **수신 시각**만 링에 넣는다 — 타임스탬프를 메인 루프에서 찍으면 루프 지연이 "바이트 간격" 으로 둔갑해 가짜 타임아웃이 난다. 링 크기 = 메인 루프 최대 지연 × 바이트율 + 여유 (115200 에서 5 ms 지연 ≈ 58 B → 64 는 빠듯). 링은 단일 생산자-단일 소비자라 head(ISR)·tail(메인)을 한쪽만 쓰면 락이 없다(메모리 순서는 C-M14, DS-A02).

C2 테스트(실제 출력): 메인 루프가 poll 6번을 건너뛰어 224바이트가 쌓이고 64칸 링이 160바이트를 버렸다. 그 구간에 걸린 2개만 사라지고 나머지 118개 전부 받음:

```text
C2 ring: 7196 bytes, overflow=160, frames needed 118 got 118
   ubx ok=118 skip=5 len=0 ck=0 to=1 ovf=160 rescan=0
```

`to=1` 은 버린 구간 때문에 미완성으로 남은 프레임을 다음 침묵에서 타임아웃이 정리한 것이다. 이 한 줄이 bring-up 엔지니어가 읽는 것: `ovf` 가 오르면 메인 루프·링 크기, `ck` 만 오르면 비트 오류(노이즈·baud 오차), `skip` 만 오르고 `ok` 가 0 이면 baud 불일치나 수신기가 NMEA 만 내보냄, `len` 이 높으면 동기를 자주 잃는 중.

### 측정 — `solution.c` C3 와 `demo_recovery.c` (실측)

모델: 115200 8N1(87 µs/바이트), 주기마다 1..8 프레임 연속 + 20 ms 침묵, PAYLOAD 4..100 B(앞 4바이트 = 순번), 30% 의 PAYLOAD 에 그럴듯한 가짜 헤더, T = 1 ms. 손상 종류: 비트 뒤집기, 바이트 유실, 바이트 삽입(프레임 안쪽), 잘림 + 3 ms 침묵(송신 리셋), 버스트(2..16 바이트를 랜덤 + 가짜 sync 로 덮기), 프레임 사이 쓰레기(1..32 B, 1/4 이 `B5 62`). **복원율 = 받은 intact 프레임 / intact 프레임**, 거짓 수락 = 보낸 적 없는 내용을 받은 수, late = 마지막 바이트 후 5 ms 넘어 전달.

`solution.c` C3 (프레임 3000, 10% 손상):

```text
C3 stream: 177613 bytes, 3000 frames, intact 2736
   naive    recovered 2600/2736 (95.03%) false=0 late=0 max_lat=0us | ok=2600 skip=7495 len=149 ck=262 to=0 rescan=0 max_steps=1
   naive+T  recovered 2672/2736 (97.66%) false=0 late=0 max_lat=0us | ok=2672 skip=4502 len=140 ck=192 to=72 rescan=0 max_steps=1
   rescan   recovered 2736/2736 (100.00%) false=0 late=9 max_lat=49048us | ok=2736 skip=13078 len=306 ck=294 to=0 rescan=600 max_steps=245
   rescan+T recovered 2736/2736 (100.00%) false=0 late=0 max_lat=1001us | ok=2736 skip=13078 len=297 ck=227 to=76 rescan=524 max_steps=135
```

`demo_recovery.c` 표 1 — 손상 종류별 (프레임 50000, 그 종류만 10%, Apple clang 21, 약 5.5 초):

| 손상 | intact | naive | naive+T | rescan | rescan+T | 거짓수락 rescan / +T | late rescan / +T | 최대지연 ms rescan / +T |
|---|---|---|---|---|---|---|---|---|
| bit flip | 44947 | 99.68% | 99.77% | 100.000% | 100.000% | 0 / 0 | 23 / 8 | 48.7 / 9.5 |
| byte drop | 45022 | 91.00% | 93.00% | 100.000% | 100.000% | 1 / 1 | 15 / 2 | 37.8 / 11.8 |
| byte insert | 44947 | 99.78% | 99.84% | 100.000% | 100.000% | 0 / 0 | 3 / 0 | 21.0 / 1.0 |
| truncate+3ms | 45022 | 88.63% | 100.00% | 99.998% | 100.000% | 2 / 1 | 226 / 0 | 48.1 / 0.0 |
| burst(가짜 sync) | 44927 | 99.44% | 99.60% | 100.000% | 100.000% | 0 / 0 | 55 / 18 | 53.7 / 13.6 |
| noise 사이 쓰레기 | 50000 | 95.09% | 95.25% | 100.000% | 100.000% | 0 / 0 | 39 / 17 | 57.3 / 18.9 |
| 6종 섞음 | 45739 | 95.37% | 97.90% | 100.000% | 100.000% | 0 / 0 | 50 / 5 | 36.9 / 13.8 |

표 2 — 손상률 (6종 섞음, 프레임 50000):

| 손상률 | intact | naive | naive+T | rescan | rescan+T | 거짓수락 n/nT/r/rT | step/byte (rescan+T) | 최악 step/호출 |
|---|---|---|---|---|---|---|---|---|
| 0% | 50000 | 100.00% | 100.00% | 100.000% | 100.000% | 0/0/0/0 | 1.00 | 1 |
| 1% | 49593 | 99.53% | 99.81% | 100.000% | 100.000% | 0/0/0/0 | 1.01 | 151 |
| 5% | 47946 | 97.78% | 99.02% | 100.000% | 100.000% | 0/0/0/0 | 1.04 | 276 |
| 10% | 45739 | 95.37% | 97.90% | 100.000% | 100.000% | 0/0/0/0 | 1.08 | 241 |
| 20% | 41728 | 91.10% | 95.61% | 100.000% | 100.000% | 1/1/1/1 | 1.16 | 297 |
| 50% | 29177 | 78.59% | 87.89% | 99.997% | 99.997% | 1/0/1/1 | 1.40 | 303 |

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">복원율 vs 손상률 (demo_recovery.c 표 2 실측, 세로축 75%부터)</text>
  <g stroke="currentColor"><rect x="300" y="30" width="14" height="12" class="f-warn"/><rect x="400" y="30" width="14" height="12" class="f-mut"/><rect x="520" y="30" width="14" height="12" class="f-ok"/></g>
  <g fill="currentColor" font-size="12"><text x="320" y="41">naive</text><text x="420" y="41">naive+T</text><text x="540" y="41">rescan+T</text></g>
  <line x1="80" y1="60" x2="80" y2="260" stroke="currentColor"/>
  <line x1="80" y1="260" x2="720" y2="260" stroke="currentColor"/>
  <g stroke="currentColor" stroke-dasharray="2,4" opacity="0.5">
    <line x1="80" y1="60" x2="720" y2="60"/><line x1="80" y1="100" x2="720" y2="100"/><line x1="80" y1="140" x2="720" y2="140"/>
    <line x1="80" y1="180" x2="720" y2="180"/><line x1="80" y1="220" x2="720" y2="220"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="38" y="64">100%</text><text x="44" y="104">95%</text><text x="44" y="144">90%</text>
    <text x="44" y="184">85%</text><text x="44" y="224">80%</text><text x="44" y="264">75%</text>
  </g>
  <g stroke="currentColor">
    <rect x="100" y="63.8" width="30" height="196.2" class="f-warn"/><rect x="132" y="61.5" width="30" height="198.5" class="f-mut"/><rect x="164" y="60" width="30" height="200" class="f-ok"/>
    <rect x="230" y="77.8" width="30" height="182.2" class="f-warn"/><rect x="262" y="67.8" width="30" height="192.2" class="f-mut"/><rect x="294" y="60" width="30" height="200" class="f-ok"/>
    <rect x="360" y="97" width="30" height="163" class="f-warn"/><rect x="392" y="76.8" width="30" height="183.2" class="f-mut"/><rect x="424" y="60" width="30" height="200" class="f-ok"/>
    <rect x="490" y="131.2" width="30" height="128.8" class="f-warn"/><rect x="522" y="95.1" width="30" height="164.9" class="f-mut"/><rect x="554" y="60" width="30" height="200" class="f-ok"/>
    <rect x="620" y="231.3" width="30" height="28.7" class="f-warn"/><rect x="652" y="156.9" width="30" height="103.1" class="f-mut"/><rect x="684" y="60" width="30" height="200" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="130" y="280">1%</text><text x="260" y="280">5%</text><text x="388" y="280">10%</text><text x="518" y="280">20%</text><text x="648" y="280">50%</text>
    <text x="608" y="226">78.6</text><text x="648" y="152">87.9</text><text x="478" y="126">91.1</text><text x="518" y="90">95.6</text>
  </g>
  <text x="10" y="306" fill="currentColor" font-size="12">손상률(프레임 중 손상된 비율). rescan+T 는 50% 에서도 99.997% — 남은 손실은 거짓 수락이 삼킨 것뿐</text>
  <text x="10" y="324" fill="currentColor" font-size="12">naive 의 손실은 손상된 프레임 자체가 아니라 그 뒤 "멀쩡한" 프레임이다 (분모가 intact 프레임)</text>
</svg>
```

표 3 — 체크섬·거짓 수락 (실제 출력):

```text
표 3a: 2비트 오류 전수 (20바이트 영역, 프레임 1000개 x 12720 쌍)
  쌍 12720000: Fletcher 미검출 120074 (0.9440%, 그중 bit7 쌍 90000), CRC-16/CCITT-FALSE 미검출 0
표 3b: 랜덤 버스트 1..16 B (68바이트 영역) 1999497 회: Fletcher 미검출 22 (1.10e-05), CRC-16 미검출 35 (1.75e-05), 2^-16 = 1.53e-05
표 3c: 랜덤 바이트 50000000 개 → rescan 파서: 수락 0, bad_len 757, ck_fail 3, rescans 760, step/byte 1.000, 최악 182
      sync+유효 LEN 강제 랜덤 후보 4000000 개: 수락 59 (기대 61.0 = N/65536)
```

읽는 법:

- **rescan 이 복원율을 바꾼다.** naive 는 손상률 10% 에서 멀쩡한 프레임의 4.6%, 50% 에서 21% 를 잃는다. rescan 은 모든 손상 종류에서 ≥ 99.997%. 남은 손실은 거짓 수락이 진짜 프레임을 삼킨 경우뿐(50000 중 1~2개).
- **타임아웃은 손실보다 지연을 고친다.** rescan 만으로도 잘림 손실은 0.002% 지만, 잘린 프레임이 다음 주기를 기다리는 동안 226개가 5 ms 넘게 늦었다(최대 48 ms). T 를 켜면 0. 다른 종류의 남은 late(수~18개)는 **연달아 오는 프레임 안의 가짜 긴 프레임** 때문 — 간격이 없으니 T 가 못 걸고, 가짜 LEN 만큼 바이트가 와야 풀린다. 위치 데이터라면 "늦게 온 프레임" 은 버리는 게 맞을 수도 있다(앱의 판단).
- **거짓 수락**: 손상된 진짜 프레임이 체크섬을 통과한 경우가 50000×4 설정에서 0~2개. 원인은 유실/삽입/잘림 뒤 경계가 틀어진 프레임이 우연히 맞는 ≈ 2^−16 사건. 랜덤 쓰레기만으로는 5천만 바이트에서 0개 — 2바이트 sync(가짜 sync 확률 2^−16/바이트) × 길이 가드(0.39%) × 체크섬(2^−16). 3c 의 760 = 5천만/65536 ≈ 763 과 맞고, 길이를 통과한 3개는 760 × 257/65536 ≈ 2.98 과 맞는다.
- **비용**: 평균 step/byte 1.00~1.40, 최악 한 호출 303 step (상한 34,585 의 1%).

### 복잡도 정리

| 항목 | 값 |
|---|---|
| RAM | 파서 1개 ≈ `raw[262]` + `q[262]` + 상태·통계 ≈ 600 B, 링 64 × 5 B |
| 인코더 | O(LEN), 출력 버퍼·용량은 호출자 |
| naive 파서 | 바이트당 O(1) |
| rescan 파서 | 평균 바이트당 1.0~1.4 step(실측), 최악 ≤ 34,585 (증명), 실측 최악 303 |
| 손실 | naive: 오류 하나가 최대 262 B 구간의 진짜 프레임들을 데려감 / rescan: 거짓 수락 때만 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution          # 54/54 passed, exit 0 (약 0.1 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 54/54, 오류 없음
cc -std=c11 -Wall -Wextra -O2 demo_recovery.c -o demo_recovery && ./demo_recovery   # 약 5.5 초, 위 표 1~3
```

`solution.c` 의 테스트 외 출력은 5. 풀이에 그대로 실었다 (B3–B7, C2, C3). 마지막 줄 `54/54 passed`.

## 7. 자주 하는 실수

**1. SYNC2 에서 B5 를 버린다.** `case ST_SYNC2: st = (b == 0x62) ? ST_CLASS : ST_SYNC1;` 이면 `… B5 B5 62 …` (쓰레기 B5 직후 진짜 프레임)에서 진짜 sync 를 놓친다. 고친 코드: `else if (b == 0xB5) { /* SYNC2 유지 */ }`.

**2. LEN 을 검사하기 전에 버퍼에 쓴다 / LEN 을 big-endian 으로 읽는다.** `len = (raw[2] << 8) | b` 이면 MON-VER 응답(LEN 0x00A0 = 160) 을 0xA000 으로 읽어 전부 `bad_len`. 검사 없이 `pl[i++] = b` 면 LEN 0xFFFF 가 버퍼를 넘친다(원격 메모리 덮기). 고친 코드: `len = raw[2] | (b << 8); if (len > MAX) { bad_len++; … }` 를 저장보다 먼저.

**3. 체크섬 범위에 sync 를 넣는다 / LEN 을 뺀다.** 송신기와 범위가 다르면 **모든** 프레임이 `ck_fail` — 카운터에서 `ck` 가 `ok` 보다 많고 `ok` 가 0 이면 범위·순서부터 의심. 고친 코드: CLASS 부터 PAYLOAD 끝까지, sync 직후 `a = b = 0`.

**4. 오류 뒤 먹은 바이트를 버린다(naive).** 동작은 한다. 그런데 유실 하나·잘림 하나·가짜 sync 하나마다 **다음 멀쩡한 프레임**을 잃는다 — 표 1 에서 유실 10% 면 9%, 잘림이면 11% 손실. 고친 코드: Part B 의 rescan.

**5. 타임아웃을 `ubx_feed` 안에서만 검사한다.** 새 바이트가 와야 검사가 돈다 → 주기의 마지막 프레임이 가짜 긴 프레임에 갇히면 다음 주기(수십~수백 ms 뒤)까지 안 나온다. 고친 코드: 타이머/IDLE/receiver-timeout 경로에서 `ubx_tick(now)`.

**6. 타임스탬프를 메인 루프에서 찍는다 / `now - last > T` 를 signed 로 한다.** 메인 루프가 2 ms 늦으면 프레임 한가운데서 가짜 타임아웃. signed 비교는 32비트 µs 카운터가 넘칠 때(71.6분) 음수가 된다. 고친 코드: ISR 에서 타이머 값을 바이트와 같이 링에 넣고, `(uint32_t)(t - last_t) > T`.

**7. T 를 너무 짧게.** 송신기(또는 USB 브리지)의 바이트 간격이 T 를 넘으면 모든 프레임이 타임아웃 — B9 의 1001 µs 경우처럼 수신 0. 고친 코드: T = 프레임 안 최대 간격의 몇 배, 그리고 `timeout` 카운터가 프레임 수만큼 오르면 경고.

**8. "체크섬이 맞았으니 데이터는 맞다".** Fletcher 는 같은 홀짝 두 바이트의 bit7 동시 뒤집기를 못 잡는다(B10 테스트에서 틀린 PAYLOAD 가 통과). 고친 코드: 안전 관련이면 CLASS/ID 길이 표(연습 1) + 값 범위 검사, 또는 상위 계층 CRC.

## 8. 면접 Q&A

<details><summary>Q1. 바이트 하나가 빠지면 naive 파서는 몇 프레임을 잃나요? 왜 삽입보다 유실이 더 나쁘죠?</summary>

유실된 프레임 X 는 당연히 잃고, X 가 1바이트 모자라니 **다음 프레임 Y 의 B5 를 CK_B 로 먹는다** → `ck_fail` 뒤 sync 를 찾을 때 Y 의 sync 는 이미 없다 → Y 도 잃는다. 연달아 오는 프레임이면 Y 를 잃고 Z 부터 받는다(B5 테스트: 1/2). 삽입은 X 가 1바이트 남아 원래 CK_B 하나만 떠돌고 Y 의 sync 는 멀쩡해서 X 만 잃는다(B8). 실측: 유실 10% 에서 naive 복원율 91.00%, 삽입 10% 에서 99.78%. rescan 은 둘 다 100%.

</details>

<details><summary>Q2. in-buffer rescan 의 버퍼 크기와 바이트당 최악 비용을 증명해 보세요.</summary>

다시 훑을 목록은 처음 `raw[]`(≤ 4 + 256 + 2 = 262). 다시 훑는 패스는 SYNC1 에서 시작하니 다음 오류의 `raw` 는 목록 안 sync 쌍 뒤의 바이트뿐 → 새 목록은 옛 목록의 접미사이고 최소 2 짧다. 그래서 버퍼 262 B 하나, 오류 사슬 ≤ 131 번, `ubx_run` step ≤ 1 + 262 + 260 + … + 2 = 17,293, flush 사슬까지 합쳐 `ubx_feed` ≤ 34,585. 실측 최악 303, 평균 1.0~1.4 step/byte. 꼬리질문 "복사 없이?" → 원본 스트림을 링 버퍼에 프레임 최대 길이 이상 남겨 두고 읽기 포인터만 "가짜 sync + 1" 로 되돌리는 방법도 된다. 대신 링이 아직 파싱 안 끝난 바이트를 덮지 않게 tail 을 파서가 관리해야 한다.

</details>

<details><summary>Q3. 타임아웃 값은 어떻게 정하나요? 9600 baud, 921600 baud, USB-serial 브리지 뒤에서 각각?</summary>

규칙: 프레임 안 최대 바이트 간격 < T < 프레임 사이 최소 침묵. 바이트 시간 = 10 / baud: 9600 → 1.04 ms, 115200 → 86.8 µs, 921600 → 10.9 µs. 송신기가 바이트를 끊김 없이 보낸다면 T = 수~수십 바이트 시간이면 되지만, 송신기 쪽 펌웨어가 프레임 중간에 다른 일을 하거나(소프트웨어 UART TX 큐), 호스트가 USB-UART 브리지 뒤에 있으면 바이트가 USB 패킷 단위로 몰려 온다(FTDI latency timer 기본 16 ms 로 알려짐 — 확인 권장) → 호스트 T 는 수십 ms. 너무 짧으면 B9 처럼 파서가 링크를 스스로 죽인다. 반대로 너무 길면 갇힌 프레임 지연이 커진다. `timeout` 카운터와 지연 분포를 보고 조정하는 게 실무.

</details>

<details><summary>Q4. Fletcher-16 과 CRC-16 은 뭐가 다르죠? 거짓 수락률을 숫자로.</summary>

둘 다 16비트라 **랜덤** 오류에서는 ≈ 2^−16 (표 3b: 1.10e−5 vs 1.75e−5, 2^−16 = 1.53e−5). 차이는 작은 구조적 오류: CRC-16/CCITT 는 이 길이에서 모든 2비트 오류(그리고 16비트 이하 버스트)를 잡지만, mod-256 Fletcher 는 같은 홀짝 위치 두 바이트의 bit7 뒤집기(ΔA = 128+128 ≡ 0, ΔB = 128·짝수 ≡ 0)를 항상 놓친다. 20바이트 영역의 2비트 오류 중 0.944% 미검출, CRC 는 0. 그리고 전부 0 인 프레임의 체크섬이 0. 장점은 CPU: 바이트당 덧셈 2번, 테이블 없음. UBX 가 이걸 고른 건 2000년대 저가 수신기 CPU 사정으로 추정 — 링크가 깨끗하다는 전제다. 안전 필요하면 상위 계층에서 보강.

</details>

<details><summary>Q5. 2바이트 sync 와 1바이트 SOF 는 재동기화에서 무엇이 다르죠?</summary>

랜덤 바이트가 sync 로 보일 확률: 1바이트 SOF 는 1/256, 2바이트는 1/65536 — 가짜 sync 가 256배 적다. 표 3c: 랜덤 5천만 바이트에 가짜 sync 760번, 길이 가드 통과 3번, 수락 0. 하지만 **PAYLOAD 속 진짜처럼 생긴 헤더**(로그 터널링, 다른 UBX 메시지 포함)는 sync 길이로 못 막는다 — 표 1 의 burst/noise 행처럼 30% 의 PAYLOAD 에 가짜 헤더를 넣으면 naive 는 여전히 잃는다. 그 경우는 rescan + CLASS/ID 표(연습 1). 구현 함정은 `B5 B5 62`(7. 실수 1).

</details>

<details><summary>Q6. 이 파서를 UART RX 인터럽트에서 돌려도 되나요? DMA 라면?</summary>

naive 파서는 바이트당 O(1) 이라 ISR 에서도 된다. rescan 은 평균 1.0~1.4 step 이지만 최악 한 바이트에 수백 step + 콜백이 몰리고, 그동안 RDR 이 덮이면 ORE. 실전: ISR 은 `DR` 과 타이머 캡처 값을 링에 넣기만 하고, 메인 루프/태스크가 `rx_poll`. DMA circular + IDLE 이면 바이트별 타임스탬프가 없다 → IDLE 인터럽트 시각을 "그 덩어리의 마지막 바이트 시각" 으로 쓰고, IDLE 자체가 "라인이 조용해짐" 이므로 그때 `ubx_tick` 과 같은 flush 를 부른다 (COMM-M11 에서 위치 계산·랩어라운드). Cortex-M7 이면 DMA 버퍼 읽기 전 cache invalidate (ARCH-M12).

</details>

<details><summary>Q7. 통계 카운터만 보고 링크 문제를 어떻게 진단하죠?</summary>

1초 차분으로: `ovf` 증가 → 메인 루프 지연/링 크기(바이트를 이미 잃었으니 최우선). 바이트 0 → 선·전원·TX/RX 뒤바뀜·수신기 포트 출력 설정. 바이트는 오는데 `ok` 0, `skip` 만 증가 → baud 불일치 또는 수신기가 NMEA 만 출력(연습 4 에서 NMEA 문장 5줄 → NO_SYNC). `ck` 가 `ok` 의 1% 이상 → 노이즈·baud 오차·접지(COMM-B03 의 허용 오차), 모든 프레임이 `ck` 면 체크섬 범위/순서 버그. `to` 가 프레임 수만큼 → T 가 너무 짧거나 송신 쪽이 프레임 중간에 멈춤. `len` 이 높음 → 동기를 자주 잃음(유실·쓰레기). 로직 애널라이저로는 바이트 레벨(프레이밍 에러)부터 확인.

</details>

<details><summary>Q8. 설정 명령을 보내고 ACK 를 기다리는 코드는 어떻게 짜나요?</summary>

UBX 는 CFG 메시지마다 UBX-ACK-ACK(0x05 0x01) 또는 ACK-NAK(0x05 0x00) 를 돌려주고, PAYLOAD 2바이트가 확인 대상의 CLASS/ID 다 (u-blox 문서 확인 권장). 대기 루프는 같은 파서로 스트림을 계속 먹이며 (1) CLASS 0x05 && LEN 2 && PAYLOAD == 보낸 CLASS/ID 일 때만 결과로 받고, (2) 다른 메시지(NAV-PVT 등)와 **다른 명령에 대한 ACK** 는 무시/다른 소비자에게, (3) deadline(보낸 시각 + 예: 500 ms)을 넘으면 TIMEOUT. 연습 3 이 쓰레기·가짜 sync·남의 ACK 가 섞인 스트림에서 ACK / NAK / 응답 없음 / 늦은 ACK 4가지를 확인한다. 재시도·중복 처리는 COMM-M14 (stop-and-wait ARQ).

</details>

## 9. 추가 연습

### 연습 1 — CLASS/ID 별 허용 길이 표

`hdr_ok(cls, id, len)` 훅을 `LEN2` 상태에 넣어, 이 펌웨어가 쓰는 메시지(NAV-PVT 92, NAV-POSLLH 28, ACK-ACK/NAK 2, MON-VER 40..250 — 길이는 u-blox M8 문서 기준 대표값, 확인 권장)가 아니면 `bad_len` 으로 처리하라. sync + 유효 LEN(0..32) + 나머지 랜덤인 후보 40만 개에서 표 없는 파서와 거짓 수락을 비교하고, 진짜 NAV-PVT(92)·ACK 는 통과, 길이 84 NAV-PVT 는 거름을 확인하라.

<details><summary>답</summary>

실측: 표 없음 수락 5 (기대 400000/65536 ≈ 6.1), 표 있음 **0**. 헤더 거절 수 400,097 은 후보 수보다 많다 — rescan 이 거절된 후보의 랜덤 바이트를 다시 훑으며 그 안의 가짜 sync 도 헤더에서 거르기 때문이다. 표는 4바이트 만에 거르므로 비용도 줄인다(PAYLOAD 를 기다리지 않음). 대가: 새 메시지를 켜면 표를 고쳐야 한다(모르는 메시지가 "통계상 쓰레기" 로 보임 — `bad_len` 이 갑자기 늘면 수신기 설정 변경을 의심). 7. 실수 8 과 `B5 62 00×6` 문제도 같이 막힌다.

</details>

### 연습 2 — Fletcher 2비트 미검출 예측식

영역 바이트 `r[0..n)` 이 주어질 때 미검출 2비트 오류 쌍의 개수를 5. 풀이의 식(k = 7: 거리 짝수, k < 7: 거리가 2^(8−k) 의 배수이고 두 비트가 다름)으로 세는 `predict_undetected()` 를 짜고, 모든 쌍을 실제로 뒤집는 전수 탐색과 길이 8..24 랜덤 영역 60개에서 비교하라. 0 으로 채운 20바이트는 몇 개인가?

<details><summary>답</summary>

60개 전부 일치(미검출 쌍 합계 5205). 0 으로 채운 20바이트 = 90 — 모든 비트가 같으니 k < 7 쌍은 "비트가 달라야" 조건에서 탈락하고 bit7 쌍 C(10,2)·2 = 90 만 남는다. 의미: 데이터와 무관하게 bit7 쌍 약 n²/4 개는 항상 뚫린다. CRC 라면 이런 "예측 가능한 구멍" 이 HD 표(Koopman)로 정리되어 있고, 길이 한계 안에서 2비트는 0개다. 증명의 핵심은 mod 256 에서 128 ≡ −128 이라는 것 하나.

</details>

### 연습 3 — CFG 전송 뒤 ACK 대기

`cfg_wait_ack(parser, wait, bytes, times, n, t_sent, timeout)` 을 짜라. 스트림에는 NAV-PVT, 가짜 sync(LEN 64) 쓰레기와 3 ms 침묵, **다른 메시지(0x06 0x01)에 대한 ACK** 가 섞인다. 시나리오: (0) 우리 ACK → ACK, (1) 우리 NAK → NAK, (2) 응답 없음 → TIMEOUT, (3) 600 ms 뒤에 온 ACK(deadline 500 ms) → TIMEOUT.

<details><summary>답</summary>

콜백에서 `cls == 0x05 && (id == 1 || id == 0) && len == 2 && pl[0] == want_cls && pl[1] == want_id` 일 때만 결과를 정한다. 나머지는 `others++` (실제로는 NAV 소비자에게 전달). 대기 루프는 바이트 시각이 `t_sent + timeout` 을 넘으면 멈춘다. 실측 출력에서 네 시나리오 모두 `timeout=1` — 가짜 sync 프레임이 3 ms 침묵에서 타임아웃으로 정리됐고, 그 덕에 뒤의 ACK 가 갇히지 않았다. 남의 ACK 를 내 것으로 받는 버그(PAYLOAD 비교 생략)는 "설정이 됐다고 믿었는데 안 된" 현장 버그의 단골이다.

</details>

### 연습 4 — 카운터 차분으로 링크 진단

`link_diag(prev, cur, bytes)` 가 OVERRUN(overflow 증가) → SILENT(바이트 0) → NO_SYNC(프레임 0) → NOISY(오류 > 1%) → OK 순서로 판정하게 하라. 시나리오: 정상 NAV-POSLLH 10개, 10개 중 2개에 비트 오류, NMEA `$GNGGA…` 5줄, 무수신, overflow 3 증가.

<details><summary>답</summary>

실측: OK / NOISY / NO_SYNC / SILENT / OVERRUN. 우선순위가 중요하다: overflow 가 있으면 다른 카운터(ck, len)는 그 결과일 수 있으니 먼저 고친다. NMEA 문장은 `$`(0x24)·ASCII 라 B5 가 없어 `sync_skip` 만 443까지 올랐다 — "수신기가 UBX 출력을 안 켰다" 를 펌웨어가 스스로 말할 수 있다. 1% 문턱은 우리 정의, 실제로는 링크 사양(BER × 프레임 비트 수)에서 정한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 11/11 passed (약 0.1 초)
```

```text
ex1 forced-sync candidates 400000: accepted plain=5 (expect ~6.1), table=0; table header rejects=400097
ex2 60 random regions (8..24 B): undetected 2-bit pairs total 5205, predictor mismatches 0; all-zero 20 B: 90
ex3 scenario 0: result=1 others=2 timeout=1
ex3 scenario 1: result=2 others=2 timeout=1
ex3 scenario 2: result=3 others=3 timeout=1
ex3 scenario 3: result=3 others=2 timeout=1
ex4 diag: ok=OK noisy=NOISY nmea=NO_SYNC silent=SILENT overflow=OVERRUN (nmea sync_skip so far=443)
11/11 passed
```

## 10. 핵심 정리

- UBX = `B5 62 | CLASS ID LEN(LE16) | PAYLOAD | CK_A CK_B`, 체크섬 = CLASS..PAYLOAD 의 8비트 Fletcher(`A += b; B += A`). MON-VER poll `B5 62 0A 04 00 00 0E 34` 로 확인.
- 상태 9개, 바이트당 O(1). `B5 B5 62` 에서 SYNC2 유지, LEN 은 받자마자 가드(256) — 가짜 sync 의 99.6% 를 4바이트 만에 거른다.
- 길이 prefix 형식의 병: 유실·잘림·가짜 sync 는 그 프레임이 아니라 **다음 멀쩡한 프레임**을 잃게 한다 (naive: 10% 손상에 4.6%, 50% 에 21% 손실).
- in-buffer rescan: 오류 시 가짜 sync 뒤 바이트를 다시 훑는다. 목록이 매번 ≥ 2 짧아져 버퍼 262 B, 최악 ≤ 34,585 step, 실측 평균 1.0~1.4 step/byte. 복원율 ≥ 99.997%.
- 타임아웃은 두 경로: 새 바이트의 간격 검사 + 타이머/IDLE 의 `ubx_tick`. 잘림 손실과 **갇힌 프레임 지연**(48 ms → 0)을 고친다. T 는 프레임 안 최대 간격과 프레임 사이 침묵 사이, 타임스탬프는 ISR 에서, 비교는 unsigned 뺄셈.
- Fletcher ≠ CRC: 랜덤 오류는 둘 다 ≈ 2^−16 이지만, 같은 홀짝 bit7 쌍은 항상 통과(2비트 오류의 0.94%), 전부 0 이면 체크섬 0. 표(CLASS/ID/길이)와 상위 검사로 보강.
- 카운터(`ok skip len ck to ovf`)와 로그 한 줄이 bring-up 의 첫 계기판이다.

## 참고자료

- u-blox, *u-blox 8 / u-blox M8 Receiver Description Including Protocol Specification* (UBX-13003221) — UBX 프레임 구조, 8-Bit Fletcher 체크섬(문서는 TCP 표준 RFC 1145 의 알고리즘이라고 설명하는 것으로 기억 — 확인 권장), UBX-ACK-ACK/NAK, NAV-PVT(92 B), NAV-POSLLH(28 B), MON-VER(40 + 30·N B). 길이·ID 는 기억에 의존 — 문서 확인 권장. MON-VER/NAV-PVT poll 바이트는 이 레슨 코드와 독립 파이썬으로 계산.
- J. Zweig, C. Partridge, RFC 1145 / RFC 1146 "TCP Alternate Checksum Options" (1990) — 8비트·16비트 Fletcher 를 TCP 대안 체크섬으로 정의 (두 RFC 의 관계는 원문 확인 권장). <https://www.rfc-editor.org/rfc/rfc1146>
- J. G. Fletcher, "An Arithmetic Checksum for Serial Transmissions", IEEE Trans. Commun., 1982 — 원 논문 (mod 255 버전).
- T. Maxino, P. Koopman, "The Effectiveness of Checksums for Embedded Control Networks", IEEE TDSC 2009 — Fletcher / Adler / CRC 검출 능력 비교. Koopman CRC Zoo: <https://users.ece.cmu.edu/~koopman/crc/>
- MAVLink 직렬화 문서 (이 레슨에서는 다루지 않음, CRC_EXTRA 포함): <https://mavlink.io/en/guide/serialization.html>
- STMicroelectronics STM32 USART 레퍼런스 매뉴얼 (예: RM0351 STM32L4, RM0433 STM32H7) — IDLE, ORE, receiver timeout(RTOR/RTOF) — 계열별 유무·비트 위치는 RM 확인 권장.
- 관련 레슨: COMM-B02 (수신기·overrun), COMM-B03 (baud 오차), COMM-B08 (체크섬 vs CRC 검출력), COMM-B10 (1바이트 SOF 재동기화 — 이 레슨의 출발점), COMM-B13 (침묵 간격 프레이밍), COMM-M11 (DMA circular + IDLE), COMM-M14 (재시도·ARQ), ALGO-B14 (체크섬), ARCH-M12 (DMA 캐시 일관성), C-M14 (ISR 공유 변수), DS-B04·DS-A02 (링 버퍼), BM-B12 (UART 드라이버), TEST-M02 (파서 퍼징, 다음 단계).
