---
id: COMM-A09
time: 120분
updated: 2026-11-11
---
# COMM-A09 버스 트랜잭션 녹화·재생으로 드라이버 회귀 테스트

## 학습 목표

- COMM-M12 의 `bus_ops` 이음새(seam)에 **녹화기(recorder)** 를 끼워, 드라이버가 낸 모든 버스 트랜잭션을 `{seq, t, dur, bus, op, addr, reg, len, result, data}` 레코드로 남기고, 녹화가 버스 동작을 바꾸지 않는다(투명성)는 것을 테스트로 보인다.
- 로그를 **두 가지 형식**으로 저장한다: 버전·CRC-32 가 있는 바이너리(현장 장비용)와 사람이 읽고 코드 리뷰에서 diff 하는 텍스트(골든 파일용). 둘 다 왕복이 정확하고, 손상·잘림·버전 불일치를 구분된 코드로 거부한다.
- **재생기(replayer)** 로 하드웨어 없이 드라이버를 돌린다: 응답은 로그에서, 드라이버가 같은 op 를 같은 순서로 내는지 검사해 `seq 6: expected spi:00 READ reg 0x27 len 7, got spi:00 READ reg 0x27 len 1` 같은 정확한 diff 를 낸다. strict / relaxed(장치 사이 순서 자유) 를 정확히 정의하고, 녹화된 간격보다 너무 빨리 오는 op 를 잡는 **타이밍 검사**를 더한다.
- **변형 재생(mutation replay)** — 레코드 k 마다 NACK·타임아웃·중재 패배·짧은 전송·비트 반전을 주입하는 스윕으로 드라이버의 문서화된 에러 동작을 전수 검증하고, CRC 없는 데이터 비트 반전은 원리적으로 못 잡는다는 것을 숫자로 보인다.
- 실제 로그를 가져오는 길(Linux `i2c_write/i2c_read/i2c_reply/i2c_result` 트레이스 이벤트, LA 디코드)과 CI 의 골든 파일 판정(PASS strict / PASS relaxed / FAIL + diff / 다시 녹화) 흐름을 설계할 수 있다.

## 1. 문제

센서 드라이버를 "조금 정리"했더니 현장 보드에서만 가끔 설정이 안 먹는다 — 이런 회귀를 보드 없이 CI 에서 잡는 것이 record/replay 다. 펌웨어·검증 면접에서 "하드웨어 없이 드라이버 회귀를 어떻게 잡나?"의 시니어 답이고, SSD 펌웨어에서 호스트 I/O 트레이스를 재생해 FTL 회귀를 잡던 것과 같은 발상이다(버스가 NVMe 대신 I2C/SPI 일 뿐).

COMM-M12 에서 드라이버가 `bus_t { ops, ctx }` 만 알게 만들었다. 그 덕에 드라이버 소스를 한 줄도 안 바꾸고 `bus_ops` 를 갈아 끼울 수 있다. 이 레슨은 두 개를 끼운다: 진짜 버스 앞에 서서 기록만 하는 **RecPort**, 진짜 버스 대신 로그로 답하는 **RpPort**.

```svg
<svg viewBox="0 0 760 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a9a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">녹화(보드) → 로그 → 재생(CI). 드라이버 소스는 양쪽에서 같다</text>
  <text x="180" y="46" fill="currentColor" text-anchor="middle" font-weight="bold">(1) 녹화 — 보드 / 합성 보드</text>
  <rect x="40" y="58" width="280" height="40" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="180" y="83" fill="currentColor" text-anchor="middle">driver: imu_* / tmp_*  (bus_t 만 앎)</text>
  <rect x="40" y="122" width="280" height="40" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="180" y="147" fill="currentColor" text-anchor="middle">RecPort (bus_ops 데코레이터)</text>
  <rect x="40" y="186" width="130" height="40" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="105" y="211" fill="currentColor" text-anchor="middle">SPI port</text>
  <rect x="190" y="186" width="130" height="40" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="255" y="211" fill="currentColor" text-anchor="middle">I2C port</text>
  <rect x="40" y="250" width="130" height="40" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="105" y="268" fill="currentColor" text-anchor="middle" font-size="12">MIMU-6D</text>
  <text x="105" y="283" fill="currentColor" text-anchor="middle" font-size="12">CS0</text>
  <rect x="190" y="250" width="130" height="40" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="255" y="268" fill="currentColor" text-anchor="middle" font-size="12">MTMP-1</text>
  <text x="255" y="283" fill="currentColor" text-anchor="middle" font-size="12">0x48</text>
  <path d="M180 98 V120" stroke="currentColor" marker-end="url(#a9a)"/>
  <path d="M120 162 V184" stroke="currentColor" marker-end="url(#a9a)"/>
  <path d="M240 162 V184" stroke="currentColor" marker-end="url(#a9a)"/>
  <path d="M105 226 V248" stroke="currentColor" marker-end="url(#a9a)"/>
  <path d="M255 226 V248" stroke="currentColor" marker-end="url(#a9a)"/>
  <rect x="330" y="300" width="100" height="44" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="380" y="318" fill="currentColor" text-anchor="middle" font-weight="bold">TxLog</text>
  <text x="380" y="335" fill="currentColor" text-anchor="middle" font-size="12">.btxl / .txt</text>
  <path d="M320 142 C380 160 380 250 380 298" stroke="currentColor" fill="none" stroke-dasharray="5 4" marker-end="url(#a9a)"/>
  <text x="392" y="215" fill="currentColor" font-size="12">레코드 append</text>
  <text x="580" y="46" fill="currentColor" text-anchor="middle" font-weight="bold">(2) 재생 — 하드웨어 없음</text>
  <rect x="440" y="58" width="280" height="40" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="580" y="83" fill="currentColor" text-anchor="middle">같은 driver 소스 (리팩터 후)</text>
  <rect x="440" y="122" width="280" height="40" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="580" y="147" fill="currentColor" text-anchor="middle">RpPort: 대조 + 응답 + 타이밍</text>
  <rect x="440" y="186" width="135" height="58" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="507" y="206" fill="currentColor" text-anchor="middle" font-size="12">변형 주입 @seq k</text>
  <text x="507" y="222" fill="currentColor" text-anchor="middle" font-size="12">NODEV NACK TIMEOUT</text>
  <text x="507" y="237" fill="currentColor" text-anchor="middle" font-size="12">ARLO SHORT FLIP SLOW</text>
  <rect x="590" y="186" width="130" height="58" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="655" y="206" fill="currentColor" text-anchor="middle" font-size="12">diff 리포트</text>
  <text x="655" y="222" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">seq 6: expected</text>
  <text x="655" y="237" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">... got ...</text>
  <path d="M580 98 V120" stroke="currentColor" marker-end="url(#a9a)"/>
  <path d="M520 162 V184" stroke="currentColor" marker-end="url(#a9a)"/>
  <path d="M640 162 V184" stroke="currentColor" marker-end="url(#a9a)"/>
  <path d="M432 322 C520 322 560 220 560 164" stroke="currentColor" fill="none" stroke-dasharray="5 4" marker-end="url(#a9a)"/>
  <text x="470" y="290" fill="currentColor" font-size="12">응답은 로그에서</text>
  <text x="10" y="352" fill="currentColor" font-size="12">시계는 시뮬레이션 g_now_us 하나: 버스는 전송 시간만큼, 드라이버 지연은 plat_delay_us 만큼 민다.</text>
</svg>
```

모델 (값·비트는 이 레슨의 mock 정의, 배치는 COMM-M12 MIMU-6D 축약판 + 새 온도 센서):

| 항목 | 정의 |
|---|---|
| MIMU-6D (SPI CS0, 8 MHz → 바이트당 1 µs) | WHO_AM_I 0x0F = 0xB5, CTRL1 0x10 `[7:4] ODR [3:2] FS`, CTRL3 0x12 bit0 SW_RESET → 기본값 + **800 µs 동안 쓰기 무시**, STATUS 0x27 bit0 XLDA (bit7..1 예약 = 0), OUT 0x28–0x2D LE int16, Z_H 읽기 → XLDA = 0 |
| MTMP-1 (I2C 0x48, 400 kHz → 바이트당 9비트 × 2.5 µs) | ID 0x0F = 0x5A, CONFIG 0x02, TEMP 0x00–0x01 BE int16, 1 LSB = 1/128 °C → m°C = raw × 125 / 16 |
| `bus_ops` 계약 (M12 에서 바뀐 점) | 반환 = **전송한 바이트 수** n (성공), 0..n-1 (짧은 전송), `BUS_E*` (< 0). 드라이버는 `rc != n` 을 `SNS_EIO` 로 본다 |
| 드라이버 (주어짐, 테스트 대상) | `imu_init`: WHO → SW_RESET → `plat_delay_us(1000)` → CTRL1 = 0x44 → 되읽기. `imu_read_mg`: STATUS+XYZ 7바이트 burst. `tmp_init`: ID → CONFIG = 0x60. `tmp_read_mc`: 2바이트. 재시도는 `sns_xfer` 한 곳, `BUS_EAGAIN` 만 3번 |
| 세션 (앱 대본) | `imu_init`, `tmp_init`, 그다음 1 ms 틱 6번 × {`imu_read_mg`, `tmp_read_mc`}. 틱 3 은 IMU 새 샘플 없음 → `ENOTREADY`. 레코드 18개 |

### Part A — 녹화기와 로그 형식

- `rec_xfer()` — 안쪽 버스로 넘기고 반환값을 **그대로** 돌려주며, 선에 나간 op 하나당 레코드 하나를 붙인다. 인자 오류(`BUS_EINVAL`)는 선에 안 나가므로 기록하지 않는다. 로그가 가득 차면 `dropped++` 만 하고 버스 동작은 그대로.
- `bin_encode()` / `bin_decode()` — BTXL v1: 헤더 16 B (magic, version, hdr_len, count, payload_len, CRC-32) + 레코드 (14 B 고정 + data). 디코더는 입력 밖을 절대 읽지 않고, magic / version / 잘림 / CRC / 구조 / 용량을 각각 다른 코드로 거부한다. 앞으로 늘어날 헤더(hdr_len > 16)는 건너뛴다.
- `txt_parse()` — 텍스트 형식(주석, 빈 줄, CRLF 허용; 끝의 `# crc32=` 줄은 선택)을 읽는다. CRC 줄이 있으면 바이너리 헤더 CRC 와 같은 값이어야 한다 — 두 형식이 **같은 CRC** 를 공유한다.
- 테스트: 골든 세션 녹화 결과의 필드, 녹화 투명성, 두 형식 왕복, 무작위 로그 2000개 왕복, 모든 바이트 × 3가지 XOR 손상과 모든 잘린 길이 거부, 헤더 규칙별 코드, 텍스트 문법 오류의 줄 번호, 가득 찬 로그.

### Part B — 재생: 대조, 타이밍, 변형

- `rp_lookup()` — **strict**: 로그의 다음 레코드와 같아야 한다. **relaxed**: 같은 장치 `(bus, addr)` 의 아직 안 쓴 가장 이른 레코드와 같아야 한다 — 장치 *사이* 순서는 자유, 장치 *안* 순서는 고정. "같다" = bus, addr, op, reg, len (+ WRITE 데이터). 첫 어긋남에서 diff 를 남기고 탈선(derailed) 상태가 된다.
- `rp_xfer()` — 인자 검사 → 대조 → **타이밍 검사** (같은 장치의 이전 op 끝 → 이번 op 시작 간격이, 녹화된 간격이 500 µs 이상일 때 그 90 % 보다 짧으면 위반) → 변형 → 녹화된 응답. 변형: NODEV/NACK/TIMEOUT(+25 ms) 은 그 op 를 실패시키고, ARLO 는 레코드를 **소비하지 않고** `BUS_EAGAIN` (재시도가 같은 레코드를 받는다), SHORT 는 바이트 하나 덜, FLIP 은 비트 하나, SLOW 는 +5 ms 성공.
- `rp_finish()` — 안 쓰인 레코드("never issued")까지 세어 문제 수를 돌려준다. 0 = 통과.
- 테스트: 같은 드라이버 → 두 모드 모두 깨끗. 리팩터 3종: ① burst → 축별 읽기(보드에선 값이 같아 기능 테스트로 안 잡힘), ② 리셋 지연 삭제(op 순서가 똑같다 — 타이밍 검사만 잡고, 보드에선 설정이 무시됨), ③ 온도 → IMU 순서 바꿈(strict 거부, relaxed 통과). 일찍 멈춤 / 더 많이 함. **변형 스윕**: 레코드 18개 × 변형 6종, 그리고 모든 READ 비트 456개 반전.

### Part C — 실제 로그 가져오기와 CI 골든 판정

- `trace_import()` — Linux ftrace 의 i2c 이벤트 텍스트를 TxLog 로: `[write l=1 (reg)] + [read l=n]` 전송 → READ 레코드, `[write l≥2]` → WRITE 레코드, `ret = -6/-11/-110` → `BUS_ENODEV/EAGAIN/ETIMEDOUT`. 다른 어댑터·다른 이벤트는 무시, 모르는 전송 모양·잘린 캡처는 `LOG_EFORMAT`, 문법 오류는 `LOG_ESYNTAX` + 줄 번호. 가져온 로그로 온도 드라이버를 재생한다.
- `golden_check()` — 골든 = (버스 로그 텍스트, 기대 출력 텍스트) 한 쌍. strict+timing → 깨끗하고 출력 같으면 PASS strict. 버스는 맞는데 출력이 다르면 FAIL(변환 수식 회귀). strict 가 어긋나면 relaxed+timing 으로 다시 → PASS relaxed 또는 FAIL. 보고서엔 strict 의 첫 diff 를 남긴다. 의도된 변경이면 다시 녹화(UPDATE_GOLDEN)해서 새 골든을 리뷰에 올린다.

## 2. Problem (English)

> **Record and replay bus transactions to regression-test sensor drivers without hardware.**
>
> Drivers talk to the bus only through `bus_ops` (`read_regs/write_regs` return the number of bytes transferred, `0..n-1` for a short transfer, or a negative `BUS_E*`).
>
> ```c
> typedef struct { uint16_t seq; uint32_t t_us; uint16_t dur_us; uint8_t bus, op, addr, reg, len;
>                  int8_t result; uint8_t data[32]; } TxRec;
> int rec_xfer(RecPort *p, uint8_t op, uint8_t reg, uint8_t *rbuf, const uint8_t *wbuf, size_t n);
> int bin_encode(const TxLog *l, uint8_t *out, size_t cap, size_t *out_len);
> int bin_decode(const uint8_t *in, size_t len, TxLog *l);
> int txt_parse(const char *s, TxLog *l, int *err_line);
> int rp_xfer(RpPort *pt, uint8_t op, uint8_t reg, uint8_t *rbuf, const uint8_t *wbuf, size_t n);
> int rp_finish(Replayer *rp);                 /* 0 = replay matched */
> int trace_import(const char *ftrace_text, unsigned adapter, TxLog *l, int *err_line);
> int golden_check(const char *log_txt, const char *expected_output, const SessOpt *o, Text *report);
> ```
>
> 1. A recorder `bus_ops` forwards every call to the real bus unchanged and appends one record per transaction that reached the wire. Store logs in a versioned, CRC-protected binary format and in a diffable text format; both must round-trip exactly and reject corruption.
> 2. A replay `bus_ops` serves responses from the log and checks that the driver issues the same operations. Strict mode: same global order. Relaxed mode: order preserved per device `(bus, addr)` only. On mismatch report the first difference precisely, e.g. `seq 17: expected READ reg 0x28 len 6, got READ reg 0x28 len 2`. Also flag an operation issued much sooner after the previous one on the same device than in the recording.
> 3. Mutation replay: at record k inject NACK, timeout, arbitration loss, short read or a bit flip. For every k, assert the driver returns its documented error, stops issuing traffic, and never commits partial data.
> 4. Import a Linux I2C trace (`i2c_write/i2c_read/i2c_reply/i2c_result`) and decide a CI verdict for a golden file.
>
> Constraints: no malloc, every buffer has a capacity, decoders never read out of bounds (ASan).
>
> Example: the golden log starts with `0000 t=0 d=2 spi:00 R 0f n=1 rc=1 : b5`. A refactor that splits the 7-byte accelerometer burst into per-register reads must fail with `seq 6: expected spi:00 READ reg 0x27 len 7, got spi:00 READ reg 0x27 len 1`.
>
> Follow-ups: Why can a replay pass while the board fails? When is relaxed matching unsafe? What can't a mutation sweep detect? How do you keep golden files from becoming noise?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 아홉 군데: Part A — `rec_xfer`, `bin_encode`, `bin_decode`, `txt_parse` / Part B — `rp_lookup`, `rp_xfer`, `rp_finish` / Part C — `trace_import`, `golden_check`. 칩·포트·시계, 레코드 정의와 `rec_valid`, 레코드 직렬화(`rec_put`/`rec_get_fixed`), CRC 범위(`btxl_crc`), 텍스트 포매터(`txt_format`)와 파서 도우미(`lit/num/snum/sp/eol`), 재생기 도우미(`rp_note`, `op_desc`, `rp_key`), 트레이스 줄 머리 파서(`tr_head`), 드라이버·리팩터 변형·세션 러너는 주어진다. 스텁은 녹화 없이 그냥 넘기거나 `LOG_ESYNTAX` / `BUS_EIO` 를 돌려주므로 멈추거나 죽지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:779  glog.n == 18 && glog.dropped == 0
FAIL starter.c:784  r[0].bus == BUSID_SPI && r[0].op == OP_READ && ...
…
FAIL starter.c:1154  golden_check(gold_log, gold_out, &CASES[0].o, &rep) == GOLD_PASS_RELAXED
21/90 passed
```

(starter 는 인코더가 없어 헤더 규칙 블록 7개 단언을 건너뛰고 "블록 실행 불가" 단언 하나만 실패로 센다 — 그래서 분모가 solution 의 97 과 다르다.)

## 4. 힌트

<details>
<summary>힌트 1 — 녹화기는 "투명"해야 한다</summary>

시각 `t = g_now_us` 를 먼저 잡고, 안쪽 op 를 부르고, `dur = g_now_us - t`. 반환값은 손대지 않는다. 기록 안 할 조건 두 개: 인자 오류(선에 안 나감), 로그 가득 참(`dropped++`). READ 데이터는 `result` 바이트만 복사하고 나머지는 0 — 이게 `rec_valid` 의 정규형이라 디코더가 같은 레코드를 돌려줄 수 있다.

</details>

<details>
<summary>힌트 2 — 디코더 검사 순서</summary>

`len < 16` → ETRUNC, magic → EMAGIC, version → EVERSION, `hdr_len < 16` → EFORMAT, `hdr_len > len || payload > len - hdr_len` → ETRUNC (뺄셈 방향 주의: `hdr + pay > len` 은 넘칠 수 있다), `hdr + pay != len` → EFORMAT, CRC → ECRC, 그다음에야 개수와 레코드 구조. CRC 를 구조 검사보다 먼저 보면 "깨진 파일"과 "잘못 만든 파일"이 구분된다.

</details>

<details>
<summary>힌트 3 — 텍스트 파서는 도우미로 한 토큰씩</summary>

`num(&p, 16, 0x7F, &v)` 처럼 기수와 최댓값을 주면 넘침까지 거부한다. 한 줄 = `num sp lit("t=") num sp lit("d=") num sp ("i2c:"|"spi:") hex2 sp R|W sp hex2 sp "n=" num sp "rc=" snum sp ":" (sp hex)×len eol`. 레코드를 다 읽으면 `rec_valid(r, l->n)` 로 seq 연속성과 정규형까지 확인.

</details>

<details>
<summary>힌트 4 — strict 와 relaxed 는 "어떤 레코드를 꺼내나"만 다르다</summary>

strict: `idx = rp->next`. relaxed: `for i: !used[i] && r[i].bus == bus && r[i].addr == addr → 첫 번째`. 꺼낸 뒤의 비교·diff·탈선 처리는 같은 코드. diff 문자열은 `op_desc()` 로 양쪽을 같은 모양으로 만든다.

</details>

<details>
<summary>힌트 5 — 변형의 레코드 소비 규칙</summary>

ARLO 는 "버스를 못 얻었다 = 트랜잭션이 안 일어났다"이므로 `used[idx]` 를 건드리지 않고 `BUS_EAGAIN` 만 돌려준다 — 드라이버의 재시도가 같은 레코드를 받는다. 나머지는 그 레코드를 "대신한" 실패라 소비한다. 키별 시각(`end_actual`, `end_rec`)은 소비할 때만 갱신.

</details>

<details>
<summary>힌트 6 — 트레이스는 전송 단위로 모은다</summary>

`i2c_write`/`i2c_read` 가 메시지 `#0`, `#1` 을 열고, `i2c_reply` 가 읽기 데이터를 채우고, `i2c_result n=.. ret=..` 가 전송을 닫는다. 닫을 때 모양을 판정해 레코드 하나를 만든다. `ret` 이 메시지 수면 성공, 음수면 errno. 파일 끝에 열린 전송이 남으면 잘린 캡처.

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "드라이버가 버스 인터페이스 하나로만 나가게 만든다(M12). 그러면 그 자리에 녹화기와 재생기를 끼울 수 있다."
2. "녹화는 보드에서 한 번, 재생은 CI 에서 매번. 재생은 응답을 주면서 드라이버의 **요청 순서**를 검사한다 — 리팩터가 버스 트래픽을 바꾸면 정확한 seq 와 함께 실패."
3. "op 순서만 보면 타이밍 버그(리셋 지연 삭제)를 놓친다 → 녹화된 간격보다 빨라지면 실패. 장치 사이 순서만 바뀐 무해한 리팩터는 relaxed 규칙으로 통과."
4. "변형 재생으로 k 번째 트랜잭션마다 NACK·타임아웃·짧은 전송을 넣어 에러 경로를 전수 검사. 데이터 비트 반전은 CRC 없이는 못 잡는다 — 숫자로 보여줄 수 있다."
5. "로그는 Linux i2c 트레이스, LA 디코드, 타깃의 링 레코더에서 온다. CI 는 골든 판정 + diff, 의도된 변경은 다시 녹화해서 리뷰."

### Part A — 녹화기와 두 형식

녹화된 골든 세션 (solution.c 실제 출력). 드라이버 출력과 로그:

```text
imu_init=OK            tmp_init=OK
imu[0]=OK -3662 0 2000      tmp[0]=OK 25000
imu[1]=OK -2695 -500 0      tmp[1]=OK 25289
imu[2]=OK -1728 -1000 2000  tmp[2]=OK -5671
imu[3]=ENOTREADY -1728 -1000 2000   tmp[3]=OK 25867
imu[4]=OK 204 -2001 2000    tmp[4]=OK 26156
imu[5]=OK 1171 -2501 0      tmp[5]=OK -4804
```

(출력은 한 줄에 call 하나다. 지면 때문에 두 열로 접었다.)

```text
# btxl v1 n=18
0000 t=0 d=2 spi:00 R 0f n=1 rc=1 : b5
0001 t=2 d=2 spi:00 W 12 n=1 rc=1 : 01
0002 t=1004 d=2 spi:00 W 10 n=1 rc=1 : 44
0003 t=1006 d=2 spi:00 R 10 n=1 rc=1 : 44
0004 t=1008 d=90 i2c:48 R 0f n=1 rc=1 : 5a
0005 t=1098 d=68 i2c:48 W 02 n=1 rc=1 : 60
0006 t=1166 d=8 spi:00 R 27 n=7 rc=7 : 01 d0 8a 01 00 00 40
0007 t=1174 d=113 i2c:48 R 00 n=2 rc=2 : 0c 80
0008 t=2287 d=8 spi:00 R 27 n=7 rc=7 : 01 bf a9 fe ef ff ff
0009 t=2295 d=113 i2c:48 R 00 n=2 rc=2 : 0c a5
0010 t=3408 d=8 spi:00 R 27 n=7 rc=7 : 01 ae c8 fb df 00 40
0011 t=3416 d=113 i2c:48 R 00 n=2 rc=2 : fd 2a
0012 t=4529 d=8 spi:00 R 27 n=7 rc=7 : 00 ae c8 fb df 00 40
0013 t=4537 d=113 i2c:48 R 00 n=2 rc=2 : 0c ef
0014 t=5650 d=8 spi:00 R 27 n=7 rc=7 : 01 8c 06 f5 bf 00 40
0015 t=5658 d=113 i2c:48 R 00 n=2 rc=2 : 0d 14
0016 t=6771 d=8 spi:00 R 27 n=7 rc=7 : 01 7b 25 f2 af ff ff
0017 t=6779 d=113 i2c:48 R 00 n=2 rc=2 : fd 99
# crc32=0xc4f3f6f8
```

읽는 법: seq 1 → 2 사이 `t` 가 2 → 1004, 즉 SW_RESET 쓰기가 끝난 시각(2 + 2 = 4)부터 1000 µs 가 비었다 — 드라이버의 `plat_delay_us(1000)` 이 로그에 **간격으로** 남는다. I2C 읽기 1바이트 = 바이트 4개(주소 W, reg, 주소 R, 데이터) × 22.5 µs = 90 µs, SPI 7바이트 burst = 8 µs. seq 12 의 STATUS 0x00 은 틱 3 의 "새 샘플 없음"이고 뒤 6바이트는 이전 샘플 그대로다(칩의 OUT 레지스터는 안 바뀌었다). 녹화 투명성은 같은 세션을 녹화기 없이 돌린 출력과 `strcmp` 로 비교해 확인한다.

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">BTXL v1 바이트 배치 (모두 little-endian) — 실제 골든 파일의 앞 31바이트</text>
  <text x="10" y="50" fill="currentColor">헤더 16 B</text>
  <rect x="90" y="36" width="120" height="26" class="f-acc" stroke="currentColor"/><text x="150" y="54" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">42 54 58 4c</text>
  <rect x="210" y="36" width="34" height="26" class="f-mut" stroke="currentColor"/><text x="227" y="54" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">01</text>
  <rect x="244" y="36" width="34" height="26" class="f-mut" stroke="currentColor"/><text x="261" y="54" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">10</text>
  <rect x="278" y="36" width="64" height="26" class="f-mut" stroke="currentColor"/><text x="310" y="54" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">12 00</text>
  <rect x="342" y="36" width="120" height="26" class="f-mut" stroke="currentColor"/><text x="402" y="54" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">38 01 00 00</text>
  <rect x="462" y="36" width="120" height="26" class="f-warn" stroke="currentColor"/><text x="522" y="54" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">f8 f6 f3 c4</text>
  <text x="150" y="80" fill="currentColor" text-anchor="middle" font-size="12">"BTXL"</text>
  <text x="227" y="80" fill="currentColor" text-anchor="middle" font-size="12">ver</text>
  <text x="261" y="80" fill="currentColor" text-anchor="middle" font-size="12">hdr</text>
  <text x="310" y="80" fill="currentColor" text-anchor="middle" font-size="12">count 18</text>
  <text x="402" y="80" fill="currentColor" text-anchor="middle" font-size="12">payload 312 B</text>
  <text x="522" y="80" fill="currentColor" text-anchor="middle" font-size="12">CRC-32 0xc4f3f6f8</text>
  <text x="600" y="54" fill="currentColor" font-size="12">CRC 범위 =</text>
  <text x="600" y="70" fill="currentColor" font-size="12">[0,12) ++ [16, 끝)</text>
  <text x="10" y="132" fill="currentColor">레코드 0</text>
  <rect x="90" y="118" width="64" height="26" class="f-ok" stroke="currentColor"/><text x="122" y="136" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00 00</text>
  <rect x="154" y="118" width="120" height="26" class="f-ok" stroke="currentColor"/><text x="214" y="136" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00 00 00 00</text>
  <rect x="274" y="118" width="64" height="26" class="f-ok" stroke="currentColor"/><text x="306" y="136" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">02 00</text>
  <rect x="338" y="118" width="34" height="26" class="f-mut" stroke="currentColor"/><text x="355" y="136" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">02</text>
  <rect x="372" y="118" width="34" height="26" class="f-mut" stroke="currentColor"/><text x="389" y="136" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">52</text>
  <rect x="406" y="118" width="34" height="26" class="f-mut" stroke="currentColor"/><text x="423" y="136" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">00</text>
  <rect x="440" y="118" width="34" height="26" class="f-mut" stroke="currentColor"/><text x="457" y="136" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">0f</text>
  <rect x="474" y="118" width="34" height="26" class="f-mut" stroke="currentColor"/><text x="491" y="136" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">01</text>
  <rect x="508" y="118" width="34" height="26" class="f-mut" stroke="currentColor"/><text x="525" y="136" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">01</text>
  <rect x="542" y="118" width="34" height="26" class="f-acc" stroke="currentColor"/><text x="559" y="136" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">b5</text>
  <text x="122" y="162" fill="currentColor" text-anchor="middle" font-size="12">seq</text>
  <text x="214" y="162" fill="currentColor" text-anchor="middle" font-size="12">t_us</text>
  <text x="306" y="162" fill="currentColor" text-anchor="middle" font-size="12">dur_us</text>
  <text x="355" y="162" fill="currentColor" text-anchor="middle" font-size="12">bus</text>
  <text x="389" y="162" fill="currentColor" text-anchor="middle" font-size="12">'R'</text>
  <text x="423" y="162" fill="currentColor" text-anchor="middle" font-size="12">addr</text>
  <text x="457" y="162" fill="currentColor" text-anchor="middle" font-size="12">reg</text>
  <text x="491" y="162" fill="currentColor" text-anchor="middle" font-size="12">len</text>
  <text x="525" y="162" fill="currentColor" text-anchor="middle" font-size="12">rc</text>
  <text x="559" y="162" fill="currentColor" text-anchor="middle" font-size="12">data</text>
  <text x="90" y="190" fill="currentColor" font-size="12">고정 14 B + data[len]. 텍스트 줄: <tspan font-family="monospace">0000 t=0 d=2 spi:00 R 0f n=1 rc=1 : b5</tspan></text>
  <text x="90" y="214" fill="currentColor" font-size="12">골든 파일 크기 (실측): 바이너리 328 B = 16 + 18 × 14 + data 60 B, 텍스트 924 B</text>
  <text x="90" y="238" fill="currentColor" font-size="12">rc 는 int8 2의 보수 (BUS_ENODEV = −2 → 0xfe). READ 의 data 중 rc 뒤 바이트는 0 (정규형)</text>
  <text x="90" y="262" fill="currentColor" font-size="12">hdr_len &gt; 16 이면 그 사이 바이트는 미래 필드 — v1 리더는 CRC 에만 넣고 건너뛴다</text>
  <text x="90" y="286" fill="currentColor" font-size="12">version 이 다르면 CRC 보기 전에 LOG_EVERSION (해석 규칙 자체가 다를 수 있으니)</text>
</svg>
```

설계 이유:

| 결정 | 이유 |
|---|---|
| 레코드에 `seq` | 텍스트를 손으로 고친 뒤 줄이 빠지거나 섞이면 `rec_valid` 가 바로 잡는다. diff 리포트의 주소이기도 하다 |
| `t_us` + `dur_us` 둘 다 | 간격 = 이번 시작 − (이전 시작 + 이전 길이). 시작만 있으면 "버스가 느려졌다"와 "드라이버가 기다렸다"가 구분 안 된다 |
| `result` 를 원본 그대로 | 실패한 트랜잭션도 재생해야 한다 — 보드에서 본 NACK 이 회귀 테스트의 입력이 된다 (Part C 의 `ret=-6`) |
| 독립 검증 | 골든 바이너리를 16진으로 뽑아 python3 `zlib.crc32(b[0:12] + b[16:])` 로 계산 → 0xc4f3f6f8, 헤더 값과 같음. `struct.unpack("<HIHBBBBBb")` 로 레코드 18개를 다시 읽어 seq 0/2/6 이 위 텍스트와 같음 (scratch 에서 실행) |
| CRC 먼저, 구조는 나중 | 무작위 손상의 대부분이 "CRC 불일치" 하나로 분류된다. 실측: 바이트 328개 × XOR 3종 = 984 손상 중 954 는 ECRC, 나머지 30 은 magic/version/hdr_len/길이 필드라 그 앞 검사가 잡았다. 잘린 길이 328가지 전부 거부 |
| 텍스트와 바이너리가 같은 CRC | 텍스트의 `# crc32=` 는 "레코드를 BTXL 로 인코딩했을 때의 헤더 CRC". 그래서 CI 가 `.txt` 골든과 현장 `.btxl` 이 **같은 녹화**인지 숫자 하나로 확인한다 |
| CRC 줄은 선택 | 사람이 골든을 손으로 고칠 수 있게. 줄을 남긴 채 고치면 `LOG_ECRC`(실수 방지), 지우면 받아들인다 (테스트 둘 다 있음) |

헤더 규칙 테스트 (실제로 확인한 것): magic → `LOG_EMAGIC`, version 2 → `LOG_EVERSION`, 꼬리 쓰레기 1바이트 → `LOG_EFORMAT`, hdr_len 20 + 미래 필드 4바이트(CRC 다시 계산) → 정상 읽힘, count 200 → `LOG_ENOSPC`, count 17(payload 와 불일치) → `LOG_EFORMAT`, CRC 를 다시 맞춘 `rc = 127 > len` → `LOG_EFORMAT`. 마지막 것이 중요하다: CRC 는 **전송 손상**만 막는다. 잘못 만든 파일(버그 있는 도구가 쓴 파일)은 구조 검사가 막아야 한다.

가득 찬 로그: 128칸에 200번 읽기 → 200번 모두 정상 값, `n = 128`, `dropped = 72`, 진짜 버스 트랜잭션 200. 녹화기가 버스 동작을 바꾸면 "녹화할 때만 사라지는 버그"가 생긴다.

### Part B — 대조 규칙과 리팩터 3종

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a9b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">(a) 순서만 바뀐 리팩터: strict 는 seq 6 에서 거부, relaxed 는 장치별 줄로 짝짓기</text>
  <text x="10" y="56" fill="currentColor" font-size="12">골든</text>
  <rect x="70" y="40" width="120" height="26" class="f-acc" stroke="currentColor"/><text x="130" y="58" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">6 spi R27 n=7</text>
  <rect x="200" y="40" width="120" height="26" class="f-ok" stroke="currentColor"/><text x="260" y="58" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">7 i2c R00 n=2</text>
  <rect x="330" y="40" width="120" height="26" class="f-acc" stroke="currentColor"/><text x="390" y="58" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">8 spi R27 n=7</text>
  <rect x="460" y="40" width="120" height="26" class="f-ok" stroke="currentColor"/><text x="520" y="58" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">9 i2c R00 n=2</text>
  <text x="10" y="146" fill="currentColor" font-size="12">실제</text>
  <rect x="70" y="130" width="120" height="26" class="f-ok" stroke="currentColor"/><text x="130" y="148" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">i2c R00 n=2</text>
  <rect x="200" y="130" width="120" height="26" class="f-acc" stroke="currentColor"/><text x="260" y="148" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">spi R27 n=7</text>
  <rect x="330" y="130" width="120" height="26" class="f-ok" stroke="currentColor"/><text x="390" y="148" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">i2c R00 n=2</text>
  <rect x="460" y="130" width="120" height="26" class="f-acc" stroke="currentColor"/><text x="520" y="148" fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">spi R27 n=7</text>
  <path d="M130 68 L130 128" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="136" y="100" fill="currentColor" font-size="12">strict: ✗</text>
  <path d="M220 68 L150 128" stroke="currentColor" marker-end="url(#a9b)"/>
  <path d="M150 68 L240 128" stroke="currentColor" marker-end="url(#a9b)"/>
  <path d="M480 68 L410 128" stroke="currentColor" marker-end="url(#a9b)"/>
  <path d="M410 68 L500 128" stroke="currentColor" marker-end="url(#a9b)"/>
  <text x="600" y="92" fill="currentColor" font-size="12">relaxed: 같은 (bus, addr)</text>
  <text x="600" y="108" fill="currentColor" font-size="12">의 가장 이른 레코드와 짝</text>
  <text x="600" y="124" fill="currentColor" font-size="12">→ 장치 안 순서는 유지</text>
  <text x="10" y="196" fill="currentColor" font-weight="bold">(b) 리셋 지연 삭제: op 순서는 같다 — 같은 장치의 간격만 다르다</text>
  <line x1="70" y1="250" x2="740" y2="250" stroke="currentColor"/>
  <rect x="70" y="226" width="16" height="24" class="f-acc" stroke="currentColor"/><text x="78" y="220" fill="currentColor" text-anchor="middle" font-size="12">1 W12</text>
  <rect x="560" y="226" width="16" height="24" class="f-acc" stroke="currentColor"/><text x="568" y="220" fill="currentColor" text-anchor="middle" font-size="12">2 W10</text>
  <path d="M88 238 H556" stroke="currentColor" marker-start="url(#a9b)" marker-end="url(#a9b)"/>
  <text x="320" y="234" fill="currentColor" text-anchor="middle" font-size="12">녹화된 간격 1000 µs (칩은 800 µs 동안 쓰기 무시)</text>
  <rect x="70" y="276" width="16" height="24" class="f-acc" stroke="currentColor"/>
  <rect x="88" y="276" width="16" height="24" class="f-warn" stroke="currentColor"/>
  <text x="112" y="286" fill="currentColor" font-size="12">리팩터 후: 간격 0 µs &lt; 90 % × 1000 → "issued too early"</text>
  <text x="112" y="302" fill="currentColor" font-size="12">(보드에선 CTRL1 쓰기가 무시되어 imu_init = EIO)</text>
  <text x="10" y="324" fill="currentColor" font-size="12">녹화 간격 ≥ 500 µs 만 검사: 지터는 무시하고 "일부러 기다린" 간격만 지킨다.</text>
</svg>
```

리팩터 세 가지와 같은 레지스터에 다른 값 쓰기의 실제 결과:

```text
per-axis refactor, strict : seq 6: expected spi:00 READ reg 0x27 len 7, got spi:00 READ reg 0x27 len 1
per-axis refactor, relaxed: seq 6: expected spi:00 READ reg 0x27 len 7, got spi:00 READ reg 0x27 len 1
no-reset-delay refactor, strict+timing: seq 2: spi:00 WRITE reg 0x10 len 1 [44] issued too early: gap 0 us < 90% of recorded 1000 us
no-reset-delay refactor on the board: imu_init=EIO (chip ignored 1 write)
swapped order, strict : seq 6: expected spi:00 READ reg 0x27 len 7, got i2c:48 READ reg 0x00 len 2
swapped order, relaxed: clean
config change fs=2: seq 2: expected spi:00 WRITE reg 0x10 len 1 [44], got spi:00 WRITE reg 0x10 len 1 [48]
```

| 리팩터 | 보드 기능 테스트 | strict | relaxed | 타이밍 | 판단 |
|---|---|---|---|---|---|
| burst → 축별 읽기 | **통과** (값 같음) | 실패 seq 6 | 실패 seq 6 | — | 버스 시간 4배, 샘플이 원자적이지 않음(BDU 없는 칩이면 찢어진 샘플). 리뷰 대상 |
| 리셋 지연 삭제 | 실패 (EIO) | **통과** (함정) | **통과** | 실패 seq 2 | 진짜 버그. 타이밍 검사가 없으면 재생은 녹화된 되읽기 0x44 를 돌려줘 성공한다 |
| 온도 → IMU 순서 | 통과 | 실패 seq 6 | 통과 | 통과 | 무해. relaxed 로 통과시키고 골든을 다시 녹화 |
| FS 4 g → 8 g | — | 실패 seq 2 (데이터) | 실패 | — | 의도된 변경이면 다시 녹화 |

두 번째 줄이 이 레슨의 핵심이다: **재생은 칩의 물리를 모른다.** 로그는 "그때 칩이 이렇게 답했다"일 뿐이고, 드라이버가 칩에게 준 조건(시간, 순서)이 바뀌면 칩이 같은 답을 할 거라는 보장이 없다. 그래서 재생은 드라이버가 녹화 때와 **같은 조건**을 주는지 — 같은 op, 같은 순서, 최소한 같은 대기 — 를 검사해야 한다. 조건이 같으면 답도 같다고 믿을 수 있다.

일찍 멈춤 / 더 많이 함 (실제 diff): 틱 5개만 돌면 `seq 16: expected spi:00 READ reg 0x27 len 7, never issued`, 7개면 `op 19: unexpected spi:00 READ reg 0x27 len 7 (no more records)` (relaxed 는 `... for this device`).

### Part B — 변형 스윕: 에러 경로 전수 검사

레코드 k 하나에 변형 하나를 넣고 세션을 돌린다 (`stop_on_error`: 오류를 낸 첫 API 호출에서 멈춤). 단언: 그 레코드를 낸 API 호출(owner)이 문서화된 코드로 실패, 버스 op 는 정확히 k+1 개(실패 뒤 더 안 나감), 그 전의 호출 결과는 골든과 같음, 실패한 호출의 출력 불변(partial commit 0), 재생 어긋남 0. ARLO/SLOW 는 세션 끝까지 골든과 같고, ARLO 는 재시도 정확히 1번. 실제 출력:

```text
seq  record             NODEV       NACK       TIMEOUT    ARLO       SHORT      SLOW
   0 spi:00 R 0f n=1    ENOTFOUND  EIO        ETIMEDOUT  retry-OK   EIO        OK
   1 spi:00 W 12 n=1    ENOTFOUND  EIO        ETIMEDOUT  retry-OK   EIO        OK
   …   (seq 2..16 도 같은 줄 — 18행 × 6열 = 108 경우 모두 기대값)
  17 i2c:48 R 00 n=2    ENOTFOUND  EIO        ETIMEDOUT  retry-OK   EIO        OK
```

표가 단조로운 것이 결과다 — 드라이버의 번역 규칙(M12)이 모든 위치에서 똑같이 지켜진다. 이 스윕이 무엇을 잡는지는 일부러 버그를 넣어 확인했다(§9 연습 3 과 아래 "변형 검사" 참고): 짧은 전송을 성공으로 보는 드라이버는 SHORT 열 17칸, 모든 에러를 재시도하는 드라이버는 NODEV/NACK/TIMEOUT 54칸이 어긋난다.

비트 반전 스윕 (모든 READ 레코드의 모든 비트 456개, 실제 출력):

```text
bit-flip sweep: 456 flips -> detected 66, became NOTREADY 53, silently changed output 294, masked 43
  flipped byte               detected NOTREADY  changed   masked
  WHO/ID + CTRL1 read-back         24        0        0        0
  IMU STATUS byte                  42        5        1        0
  IMU X/Y/Z bytes                   0       48      197       43
  temperature bytes                 0        0       96        0
```

```svg
<svg viewBox="0 0 760 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">비트 하나 반전 → 드라이버가 알아챘나? (실측, solution.c 출력, 1 플립 = 1.6 px)</text>
  <text x="190" y="58" fill="currentColor" text-anchor="end">WHO/ID + 되읽기 (24)</text>
  <rect x="200" y="44" width="38.4" height="22" class="f-ok" stroke="currentColor"/>
  <text x="190" y="98" fill="currentColor" text-anchor="end">IMU STATUS (48)</text>
  <rect x="200" y="84" width="67.2" height="22" class="f-ok" stroke="currentColor"/>
  <rect x="267.2" y="84" width="8" height="22" class="f-mut" stroke="currentColor"/>
  <rect x="275.2" y="84" width="1.6" height="22" class="f-warn" stroke="currentColor"/>
  <text x="190" y="138" fill="currentColor" text-anchor="end">IMU X/Y/Z (288)</text>
  <rect x="200" y="124" width="76.8" height="22" class="f-mut" stroke="currentColor"/>
  <rect x="276.8" y="124" width="315.2" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="592" y="124" width="68.8" height="22" class="f-acc" stroke="currentColor"/>
  <text x="190" y="178" fill="currentColor" text-anchor="end">온도 데이터 (96)</text>
  <rect x="200" y="164" width="153.6" height="22" class="f-warn" stroke="currentColor"/>
  <text x="244" y="60" fill="currentColor" font-size="12">24 잡음 (EBADID / EIO)</text>
  <text x="284" y="100" fill="currentColor" font-size="12">42 잡음 (예약 비트 → EIO) · 5 NOTREADY · 1 조용히 바뀜</text>
  <text x="434" y="140" fill="currentColor" text-anchor="middle" font-size="12">197 조용히 바뀜</text>
  <text x="626" y="140" fill="currentColor" text-anchor="middle" font-size="12">43 가림</text>
  <text x="238" y="140" fill="currentColor" text-anchor="middle" font-size="12">48</text>
  <text x="360" y="180" fill="currentColor" font-size="12">96 조용히 바뀜 (1 LSB = 7.8 m°C, 가려지는 비트 없음)</text>
  <rect x="200" y="208" width="14" height="14" class="f-ok" stroke="currentColor"/><text x="220" y="220" fill="currentColor" font-size="12">잡음</text>
  <rect x="270" y="208" width="14" height="14" class="f-mut" stroke="currentColor"/><text x="290" y="220" fill="currentColor" font-size="12">NOTREADY 로 바뀜</text>
  <rect x="420" y="208" width="14" height="14" class="f-warn" stroke="currentColor"/><text x="440" y="220" fill="currentColor" font-size="12">잘못된 값이 OK 로 나감</text>
  <rect x="610" y="208" width="14" height="14" class="f-acc" stroke="currentColor"/><text x="630" y="220" fill="currentColor" font-size="12">출력 변화 없음</text>
  <text x="10" y="246" fill="currentColor" font-size="12">데이터 바이트 384개 중 잡힌 것 0 — 형식 검사(ID, 예약 비트, 되읽기)는</text>
  <text x="10" y="262" fill="currentColor" font-size="12">"값이 정해진" 비트만 지킨다. 값 자체는 CRC 가 있어야 지킨다.</text>
</svg>
```

해석:

- **잡을 수 있는 것**은 "값이 정해져 있는" 비트뿐이다: ID(1비트 차이 → EBADID), 예약 비트(STATUS bit7..1 = 0 이어야), 쓴 값의 되읽기. 24 + 42 = 66.
- STATUS bit0(XLDA) 는 "정상적인 두 값" 중 하나라 반전이 오류처럼 안 보인다: 새 샘플 5개는 NOTREADY 로 사라지고(샘플 유실), 틱 3 의 0x00 → 0x01 은 **이전 샘플을 새 샘플로** 내보낸다(1건, "조용히 바뀜").
- 데이터 바이트 384개(288 + 96) 중 잡힌 것은 0. IMU 는 FS ±4 g 에서 1 LSB = 0.122 mg 라 0 쪽 버림으로 하위 비트 43개가 출력에 안 나타나고(가림), 나머지 197개는 틀린 mg 가 OK 로 나간다. 틱 3 의 48비트는 STATUS 가 NOTREADY 라 아예 안 쓰인다.
- 결론(면접용 한 줄): **버스에 무결성 검사가 없으면 데이터 손상은 드라이버 계층에서 원리적으로 못 잡는다.** 잡으려면 센서 쪽 CRC(일부 센서는 I2C 데이터에 CRC-8 을 붙인다 — 예: Sensirion SHT3x, 데이터시트 확인 권장), 값의 그럴듯함(범위·변화율), 이중화가 필요하다. 변형 스윕은 이 한계를 숫자로 문서화한다.

무작위 세션 300개(틱 수 1..16, 샘플 무작위, 모드 번갈아): 녹화 → 바이너리 → 디코드 → 재생 → 출력 같음, 문제 0.

### Part C — 실제 로그: Linux i2c 트레이스 이벤트

리눅스 커널의 I2C 코어는 `i2c_transfer()` 마다 트레이스 이벤트를 남긴다 (`include/trace/events/i2c.h`, 형식 문자열은 참고자료 링크에서 확인함): `i2c_write: i2c-%d #%u a=%03x f=%04x l=%u [%*phD]`, `i2c_read: ... l=%u`, `i2c_reply: ... [%*phD]`, `i2c_result: i2c-%d n=%u ret=%d`. `%*phD` 는 바이트를 `-` 로 이은 16진수다. 켜는 법은 보통 `echo 1 > /sys/kernel/tracing/events/i2c/enable` 후 `cat /sys/kernel/tracing/trace` (tracefs 마운트 위치는 배포판마다 다름 — 확인 권장). 어댑터가 SMBus 를 직접 구현하면(`smbus_xfer`) `smbus_*` 이벤트가 따로 나온다 — 이 importer 의 범위 밖.

solution.c 의 `TRACE_TMP` 는 이 형식으로 **이 레슨이 만든** 예다 (값은 MTMP-1 mock; 다른 어댑터 `i2c-2` 줄과 `sched_switch` 줄이 섞여 있다). 가져온 결과 (실제 출력):

```text
imported from i2c trace events (adapter 1):
# btxl v1 n=4
0000 t=0 d=61 i2c:48 R 0f n=1 rc=1 : 5a
0001 t=100 d=62 i2c:48 W 02 n=1 rc=1 : 60
0002 t=1000000 d=84 i2c:48 R 00 n=2 rc=2 : 0c 80
0003 t=2000000 d=84 i2c:48 R 00 n=2 rc=2 : f3 5b
# crc32=0x2496ec6e
replayed MTMP-1 driver on imported trace: init=OK, 25000 m°C, -25289 m°C
```

`[0c-80]` = 0x0C80 = 3200 → 25.000 °C, `[f3-5b]` = 0xF35B = −3237 → −25 289 m°C (0 쪽 버림). 재생 테스트를 처음 짰을 때 초기화 직후 바로 첫 읽기를 불렀더니 **타이밍 검사가 실패했다** — 트레이스의 1 s 주기를 재생 쪽도 지켜야 한다. 실보드 로그는 "드라이버가 그때 어떤 리듬으로 불렸나"까지 담고 있다는 실례다. 실패 전송(`ret=-6` = `-ENXIO`, 주소 NACK) 은 `BUS_ENODEV` 레코드가 되고 재생하면 `tmp_init = ENOTFOUND` — 현장에서 본 고장을 그대로 회귀 테스트로 만든 것이다. errno 대응(ENXIO/EAGAIN/ETIMEDOUT)은 커널 *I2C/SMBus fault codes* 문서의 관례다.

SPI 쪽은 `spi_message_submit`, `spi_transfer_start/stop` 같은 트레이스 이벤트가 있다(`include/trace/events/spi.h` — 이름은 기억, 확인 권장). MCU 에서는 이 레슨의 `RecPort` 를 타깃에 그대로 올리고 링 버퍼에 담아 UART/SWO 로 덤프하거나(연습 1), 로직 애널라이저 캡처를 디코드한다(COMM-A10, HWLAB-M06). 로그 출처가 셋이어도 TxLog 하나로 모이면 재생기는 하나다.

### Part C — CI 골든 판정

실제 출력 (`golden_check` 의 보고서):

```text
[golden] unchanged driver ->
PASS strict
[golden] tmp read moved before imu ->
strict : seq 6: expected spi:00 READ reg 0x27 len 7, got i2c:48 READ reg 0x00 len 2
PASS relaxed (cross-device order changed; re-record golden to make it strict)
[golden] burst split per axis ->
strict : seq 6: expected spi:00 READ reg 0x27 len 7, got spi:00 READ reg 0x27 len 1
relaxed: seq 6: expected spi:00 READ reg 0x27 len 7, got spi:00 READ reg 0x27 len 1
FAIL
[golden] reset delay removed ->
strict : seq 2: spi:00 WRITE reg 0x10 len 1 [44] issued too early: gap 0 us < 90% of recorded 1000 us
relaxed: seq 2: spi:00 WRITE reg 0x10 len 1 [44] issued too early: gap 0 us < 90% of recorded 1000 us
FAIL
[golden] /32768 replaced by >>15 ->
output line 3: expected 'imu[0]=OK -3662 0 2000', got 'imu[0]=OK -3663 0 2000'
FAIL (bus traffic matches, driver output differs)
```

마지막 줄이 골든을 **두 파일**로 둔 이유다. `/32768` 을 `>>15` 로 바꾼 "최적화"는 버스 트래픽이 똑같아 op 재생만으론 안 보인다. 음수에서 나눗셈은 0 쪽, 산술 시프트는 −∞ 쪽으로 버려 −3662 가 −3663 이 된다(음수의 `>>` 는 C 에서 구현 정의 — clang/gcc 는 산술 시프트). 기대 출력 파일이 그것을 잡는다.

CI 흐름 (제안):

| 단계 | 내용 |
|---|---|
| 저장소 | `golden/imu_tmp.btxl.txt` (버스 로그) + `golden/imu_tmp.out.txt` (기대 출력). 텍스트라 PR diff 로 리뷰 가능 |
| 매 커밋 | `golden_check` 전부. PASS strict → 녹색, PASS relaxed → 녹색 + 경고("골든 갱신 권장"), FAIL → 빨강 + 첫 diff |
| 의도된 변경 | HIL 보드(또는 합성 보드)에서 다시 녹화(UPDATE_GOLDEN) → 새 `.txt` 를 PR 에 포함 → 리뷰어가 diff 를 읽고 승인. 테스트: 순서를 바꾼 드라이버로 다시 녹화하면 새 골든에서 PASS strict, 옛 순서 드라이버는 이제 PASS relaxed |
| 손상된 골든 | `LOG_ECRC` 등 → `GOLD_EBADLOG` (테스트가 실패가 아니라 "판정 불가") |
| 퍼징 | 로그 파서·트레이스 importer 는 외부 입력이다 → TEST-M02(다음 단계)의 libFuzzer 대상. Apple clang 엔 런타임이 없어 `brew install llvm` 의 clang 이 필요 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 97/97 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 97/97, 깨끗함
```

**변형 검사 (하네스가 버그를 잡는가)** — solution.c 의 사본에 그럴듯한 버그 6개를 하나씩 넣고 돌린 결과 (전부 exit 1):

| 넣은 버그 | 잡은 단언 |
|---|---|
| ARLO 변형에서도 레코드를 소비 | 변형 스윕 (`cases == 18 * 6 && bad == 0`) |
| `bin_decode` 가 CRC 검사를 건너뜀 | 손상 전수 (`accepted == 0`) |
| 대조에서 WRITE 데이터 비교 빠짐 | `config change fs=2` diff 문자열 |
| `trace_import` 가 어댑터 번호를 무시 | 가져온 레코드 수 4, 값, 어댑터 2 필터 (4개) |
| 드라이버가 짧은 전송을 성공으로 봄 | 변형 스윕 SHORT 열 |
| relaxed 가 장치 키를 무시 | 순서 바꾼 relaxed 재생 + 골든 판정 (3개) |

## 7. 자주 하는 실수

**1. 녹화기가 버스 동작을 바꾼다.** 기록하다 로그가 차면 `return BUS_EIO`, 또는 기록용으로 읽기 버퍼를 먼저 0 으로 지움. 녹화할 때만 동작이 달라지면 녹화본이 실제 동작의 증거가 아니다. 고친 코드: 안쪽 반환값을 그대로 돌려주고, 가득 차면 `dropped++` 만. 하네스는 녹화 유무로 출력이 같은지 `strcmp` 한다.

**2. 실패한 트랜잭션은 기록 안 함.** `if (rc < 0) return rc;` 를 기록 앞에 두면 보드에서 본 NACK·타임아웃이 로그에서 사라지고, 재생 때 드라이버의 재시도·복구 경로가 "예상 밖 op"로 보인다. 결과 코드까지 그대로 남겨야 현장 고장이 회귀 테스트가 된다 (Part C 의 `ret=-6`). 반대로 인자 오류는 선에 안 나갔으니 기록하지 않는다.

**3. 디코더의 길이 검사를 덧셈으로.**

```c
if (hdr + pay > len) return LOG_ETRUNC;          /* pay = 0xFFFFFFF0 이면 32비트 size_t 에서 넘쳐 통과 */
```

고친 코드: `if (hdr > len || pay > len - hdr) return LOG_ETRUNC;`. 레코드 루프도 `len - off < 14`, `len - off - 14 < r->len` 처럼 남은 길이와 비교한다. ASan 이 하네스의 잘림·손상 전수 테스트에서 이걸 확인한다.

**4. 순서만 비교하고 시간은 안 봄.** 리셋 지연을 지운 리팩터는 op 순서가 같아 strict 재생을 통과하고, 녹화된 되읽기 0x44 덕에 "설정 확인"까지 성공한다. 고친 코드: 장치별 간격 검사 (`rec_gap ≥ 500 µs && act_gap < 0.9 × rec_gap → 위반`). 간격이 **늘어난** 것은 문제 삼지 않는다 — 느려진 버스, 스케줄링은 정상이다.

**5. relaxed 를 "아무 순서나"로.** 키 없이 아무 안 쓴 레코드나 고르거나, 레지스터까지 키에 넣어 장치 안 순서도 풀면 clear-on-read(이 칩의 Z_H 읽기 → XLDA 클리어)나 리셋 → 설정 순서가 깨진 드라이버가 통과한다. 고친 코드: 키 = `(bus, addr)`, 키 안은 FIFO. 이것도 "장치끼리 상태를 공유하지 않는다"는 가정 위에 있다 (Q3).

**6. 변형 뒤에도 끝까지 대조.** 레코드 k 에서 NACK 을 넣으면 드라이버가 합법적으로 다른 길(중단, 복구)로 간다. 그 뒤를 골든과 비교하면 모든 변형이 "실패"로 보여 신호가 없다. 고친 코드: 문서화된 동작이 "중단"이면 `stop_on_error` 로 첫 오류에서 멈추고 *그 시점까지*를 단언(op 수 = k+1, 이전 결과 = 골든, 부분 커밋 0). 복구 동작까지 검사하려면 복구 시나리오를 따로 녹화한 골든이 필요하다.

## 8. 면접 Q&A

<details>
<summary>Q1. record/replay 와 mock(예: CMock 식 expect 큐)은 뭐가 다른가요?</summary>

둘 다 "드라이버 아래를 가짜로" 바꾸지만 기대값의 출처가 다르다. mock 은 테스트 작성자가 손으로 "이 레지스터를 읽으면 이 값"을 적는다 — 작성자가 칩을 잘못 이해하면 테스트도 같이 틀린다. record/replay 는 **실제 칩(또는 검증된 시뮬레이터)의 답**을 기대값으로 쓴다. 그래서 (1) 데이터시트에 없는 칩의 버릇(리셋 뒤 몇 번째 읽기에서야 값이 바뀐다 등)이 자동으로 들어가고, (2) 테스트를 쓰는 비용이 "한 번 녹화"로 줄어든다. 대신 녹화한 그 시나리오만 덮고, 칩 버전이 바뀌면 다시 녹화해야 한다. 실무에선 섞는다: 정상 경로는 녹화 골든, 드문 에러 경로는 변형 재생이나 손으로 쓴 mock.

</details>

<details>
<summary>Q2. 재생 테스트는 통과하는데 보드에서는 실패할 수 있나요?</summary>

있다. 재생은 "드라이버가 녹화 때와 같은 조건을 주면 칩이 같은 답을 한다"는 가정 위에 있고, 검사하는 것은 드라이버 쪽 조건뿐이다. 깨지는 경우: ① 타이밍 — 이 레슨의 리셋 지연 삭제처럼 op 순서는 같고 간격만 짧아짐 (그래서 타이밍 검사를 넣었다. 그래도 "더 짧아도 되는 간격"인지는 모른다 — 보수적으로 녹화값의 90 %), ② 칩 상태 — 전원 순서, 이전 부팅에서 남은 상태, 온도, ③ 칩 리비전·다른 제조 lot 이 다르게 답함, ④ 녹화에 없는 시나리오. 그래서 record/replay 는 HIL(하드웨어 루프) 테스트를 **대신**하지 않고 앞에 선다: 매 커밋은 재생으로 빠르게, 야간/릴리스는 실보드로.

</details>

<details>
<summary>Q3. relaxed 매칭은 언제 위험한가요?</summary>

"장치끼리 상태를 공유하지 않는다"는 가정이 깨질 때. 예: ① 한 칩의 두 기능이 다른 주소/CS 로 보이는 경우(가속도·자력계 콤보 칩, 같은 다이) — 한쪽 리셋이 다른 쪽에 영향, ② I2C 멀티플렉서(채널 선택 쓰기 후 그 채널의 장치에 접근 — mux 와 장치는 키가 다르지만 순서가 의미를 가짐), ③ 공유 인터럽트/동기 핀(한 센서의 트리거가 다른 센서의 측정을 시작), ④ 버스 자체의 상태(같은 I2C 버스에서 한 장치가 SDA 를 잡고 있으면 다른 장치 접근이 실패). 그래서 relaxed 통과는 녹색이지만 **경고**를 남기고, 골든을 다시 녹화해 strict 로 돌려놓는다. 더 정밀하게 하려면 키를 "독립 그룹"으로 사용자가 선언하게 한다 (mux + 그 아래 장치들 = 한 그룹).

</details>

<details>
<summary>Q4. 골든 파일이 자주 깨져서 팀이 무시하게 되면?</summary>

골든이 소음이 되는 원인은 대개 셋: ① 타임스탬프 지터 — 정확한 시각이 아니라 간격만, 그것도 "줄어든 것"만 검사 (이 레슨), ② 의미 없는 순서 변화 — relaxed 규칙과 장치 그룹, ③ 갱신 절차가 번거로움 — `UPDATE_GOLDEN=1` 한 번에 다시 녹화하고, PR 에 텍스트 diff 가 보이게. 그리고 실패 메시지가 정확해야 한다: "replay failed" 가 아니라 `seq 6: expected ... got ...` 와 첫 출력 차이 줄. 원칙: 골든 갱신은 **리뷰 대상 변경**이다 — 갱신 PR 에서 버스 트래픽 diff (연습 2 의 LCS diff) 를 읽고 "왜 바뀌었나"를 설명할 수 있어야 승인.

</details>

<details>
<summary>Q5. 변형을 넣은 뒤 드라이버가 다른 경로로 가면 재생은 어떻게 하나요?</summary>

세 가지 선택. ① 첫 오류에서 멈추고 그 시점까지 단언 (이 레슨 — 에러 감지·번역·부분 커밋 없음을 전수로 검사, 싸고 결정적), ② 일시적 변형만 재생 가능하게 설계 — ARLO 는 레코드를 소비하지 않아 재시도가 같은 레코드를 받으므로 세션이 끝까지 골든과 같아야 한다 (재시도 정책 검증), ③ 복구 경로까지 보려면 그 고장을 보드에서 실제로 일으켜 녹화한 별도 골든 (예: SDA stuck → 9클럭 복구 → 재초기화, COMM-M03/A11). 하지 말아야 할 것: 변형 뒤에도 원래 골든과 끝까지 비교해 "실패가 예상됨"으로 통과시키는 것 — 아무것도 검사하지 않는다.

</details>

<details>
<summary>Q6. 비트 반전 스윕에서 데이터 바이트 384개 중 하나도 못 잡았는데, 드라이버 버그인가요?</summary>

아니다, 그게 정보 이론적 한계다. 데이터 레지스터는 어떤 값이든 합법이라 드라이버는 "틀린 값"과 "다른 값"을 구분할 근거가 없다. 할 수 있는 것: ① 링크에 무결성 — 센서가 CRC 를 붙이면(일부 습도·온도 센서의 CRC-8, CAN/SPI 프레이밍 위 CRC) 드라이버가 검사, ② 값의 그럴듯함 — 물리적 범위, 샘플 간 변화율, 중복 센서 비교, ③ 시스템 차원 — 필터/추정기에서 이상치 거부. 스윕의 가치는 이 경계를 **문서화**하는 데 있다: "형식 비트 66/66 감지, 데이터 비트 0/384 — 무결성은 상위 계층 책임" 이라고 설계 리뷰에 적을 수 있다. 덤으로 STATUS XLDA 반전이 "이전 샘플을 새 것으로" 내보낸다는 것(1건)도 드러났다 — 시퀀스 카운터가 있는 센서라면 그걸로 잡는다.

</details>

<details>
<summary>Q7. 실보드 로그는 어떻게 뜨나요? 오버헤드는?</summary>

세 경로. ① Linux: i2c 트레이스 이벤트(`i2c_write/read/reply/result`) — 드라이버 수정 없이 켜고 끔, 타임스탬프는 커널 시계, 오버헤드는 이벤트당 수 µs 수준(기억 — 측정 권장). ② MCU: `RecPort` 를 타깃에 그대로 — 레코드 하나 = 바이트 14 + len, 1 kHz 루프에서 초당 37 KB(연습 4 실측 계산). 전부 못 내보내니 링 버퍼에 마지막 N개만 두고 오류·워치독 때 덤프(블랙박스, 연습 1), 타임스탬프는 DWT CYCCNT 같은 자유 증가 카운터. ③ 버스 밖: 로직 애널라이저 캡처를 디코드(COMM-A10) — 타깃 수정 0, 진짜 와이어 타이밍, 대신 드라이버의 "의도"(어느 API 호출이었나)는 모른다. 어느 것이든 TxLog 형식으로 바꾸면 같은 재생기로 들어간다.

</details>

<details>
<summary>Q8. 로그 형식에 버전과 CRC 를 왜 넣나요? 텍스트면 충분하지 않나요?</summary>

용도가 둘이다. 텍스트는 사람이 읽고 PR 에서 diff 하는 골든용 — 크기보다 가독성(실측 107 B/틱). 바이너리는 현장 장비가 플래시·RAM 에 담거나 무선으로 올리는 용 — 37 B/틱, 그리고 전송·저장 중 손상이 실제로 생긴다. CRC 가 없으면 깨진 로그로 재생해 "드라이버 회귀"라는 거짓 경보가 난다 (실측: 단일 바이트 손상 984가지 전부 거부). 버전은 형식이 바뀔 날을 위한 것: 다른 버전은 CRC 를 보기 전에 거부(해석 규칙이 다를 수 있으니), 헤더 길이 필드로 v1 안에서도 필드를 덧붙일 수 있게 했다(hdr_len 20 테스트). CRC 가 막지 못하는 것 — 버그 있는 도구가 CRC 까지 맞춰 쓴 잘못된 레코드 — 은 `rec_valid` 의 구조 검사가 막는다.

</details>

## 9. 추가 연습

### 연습 1 — 블랙박스 링 레코더

현장 펌웨어는 전체 로그를 담을 RAM 이 없다. 마지막 16개 트랜잭션만 덮어쓰며 보관하는 `RingPort`(bus_ops 데코레이터)를 짜고, 98번째 op 에서 주소 NACK 을 내는 버스로 100번 읽은 뒤 덤프하라. 덤프는 본문의 텍스트/바이너리 형식으로 왕복돼야 한다.

<details>
<summary>답 보기</summary>

레코드 하나짜리 임시 TxLog 로 `rec_xfer` 를 재사용하고, 결과를 `ring[total % 16]` 에 넣는다. 덤프는 `first = total - min(total, 16)` 부터 오래된 순, seq 는 0.. 으로 다시 매긴다.

```c
uint32_t cnt = g->total < RING_N ? g->total : RING_N, first = g->total - cnt;
for (uint32_t i = 0; i < cnt; i++) { out->r[i] = g->r[(first + i) % RING_N]; out->r[i].seq = (uint16_t)i; }
```

실행 출력:

```text
ex1 black box: 100 ops seen, dump = abs seq 84..99, failing op at dump index 13:
    0012 t=16532 d=90 i2c:48 R 0f n=1 rc=1 : 5a
    0013 t=16622 d=23 i2c:48 R 0f n=1 rc=-2 : 00
    0014 t=16645 d=90 i2c:48 R 0f n=1 rc=1 : 5a
    0015 t=16735 d=90 i2c:48 R 0f n=1 rc=1 : 5a
```

NACK 은 주소 바이트 하나(23 µs)만 쓰고 끝났다는 것도 `d=` 로 보인다. 링 인덱스는 DS-B03/B04 의 링 버퍼 그대로이고, 덤프가 같은 형식이라 사고 직전 16개를 그대로 재생 테스트로 쓸 수 있다.

</details>

### 연습 2 — 두 로그의 LCS diff 리포트

재생기는 첫 어긋남만 보고한다. 골든 리뷰에는 "무엇이 바뀌었나" 전체가 필요하다. op 키(bus, addr, op, reg, len, WRITE 데이터)로 두 로그의 LCS(ALGO-M08)를 구해 `-`/`+`/` ` 줄을 내라. 축별 읽기 리팩터와 순서 바꾼 리팩터를 각각 다시 녹화해 비교하라.

<details>
<summary>답 보기</summary>

`L[i][j] = LCS(a[i..], b[j..])` 를 뒤에서부터 채우고 앞에서부터 걸으며 출력 (128×128 `uint16_t` 정적 표 = 33 KB — 호스트 도구라 괜찮지만 MCU 에선 안 된다). 실행 출력:

```text
ex2 diff golden -> per-axis: 12 same, 6 removed, 21 added; first lines:
      …        (0000..0004 같음, 생략)
      0005 i2c:48 W 02 n=1
    - 0006 spi:00 R 27 n=7
    + 0006 spi:00 R 27 n=1
    + 0007 spi:00 R 28 n=2
    + 0008 spi:00 R 2a n=2
    + 0009 spi:00 R 2c n=2
      0007 i2c:48 R 00 n=2
ex2 diff golden -> swapped order: 17 same, 1 removed, 1 added
```

축별 읽기는 IMU 읽기 6개가 빠지고 21개(새 샘플 틱 5 × 4 + NOTREADY 틱 1)가 들어왔다 — 리뷰어가 "트랜잭션 3.5배"를 한눈에 본다. 순서 바꾼 쪽은 읽기 키가 매 틱 같아서 LCS 가 "한 칸 밀림"(맨 앞 하나 빠지고 맨 뒤 하나 추가)으로 본다. LCS 는 순서를 보는 도구라 장치 사이 재배열의 판정은 relaxed 규칙이, 설명은 diff 가 맡는다.

</details>

### 연습 3 — 변형 스윕이 드라이버 버그를 잡는가

드라이버에 버그 두 개를 넣어 보라: (1) 짧은 전송을 성공으로 봄 (`rc < 0` 만 검사), (2) 모든 에러를 3번 재시도. 레코드 18개 × {NODEV, NACK, TIMEOUT, SHORT} = 72경우에서 문서화된 동작과 다른 경우를 세라.

<details>
<summary>답 보기</summary>

실행 출력:

```text
ex3 driver bug 0 (documented driver): 0/72 deviations (NODEV 0, NACK 0, TIMEOUT 0, SHORT 0)
ex3 driver bug 1 (short transfer treated as OK): 17/72 deviations (NODEV 0, NACK 0, TIMEOUT 0, SHORT 17)
    first: seq 0 SHORT: driver ENOTFOUND, replay says: seq 1: expected spi:00 WRITE reg 0x12 len 1 [01], never issued
    SHORT at seq 3 (CTRL1 read-back): driver EIO -> same as documented, by accident
ex3 driver bug 2 (retry every error): 54/72 deviations (NODEV 18, NACK 18, TIMEOUT 18, SHORT 0)
    first: seq 0 NODEV: driver EIO, replay says: seq 1: expected spi:00 WRITE reg 0x12 len 1 [01], got spi:00 READ reg 0x0f len 1
```

버그 1: SHORT 18칸 중 17칸. 빠진 하나(seq 3)는 되읽기 버퍼가 0 으로 초기화돼 있어 "0x44 ≠ 0" → EIO 가 **우연히** 맞았다 — 테스트가 통과한다고 코드가 옳은 게 아니라는 예. 첫 칸은 WHO 읽기가 0바이트 → `id = 0` → "칩 없음"으로 오진. 버그 2: 재시도가 같은 op 를 다시 내는 순간 재생기가 `got spi:00 READ reg 0x0f` 로 정확히 짚는다. 타임아웃 재시도는 실보드에서 25 ms × 3 = 75 ms 블록이 된다 (COMM-M12 연습 1 의 재시도 증폭).

</details>

### 연습 4 — 로그 크기 예산

1 kHz 루프(틱마다 IMU 7바이트 + 온도 2바이트 읽기)에서 바이너리와 텍스트 로그가 초당 몇 바이트인지, 4 KiB RAM 링이 몇 ms 를 담는지 계산하라.

<details>
<summary>답 보기</summary>

바이너리 = (14 + 7) + (14 + 2) = 37 B/틱. 텍스트는 골든의 실제 두 줄 길이를 잰다. 실행 출력:

```text
ex4 per 1 ms tick: binary 37 B, text 107 B -> 37000 B/s vs 107000 B/s at 1 kHz; a 4 KiB RAM ring holds 110 ms (binary) / 38 ms (text)
```

110 ms 면 "사고 직전 100여 틱"이다 — 대부분의 버스 고장 진단엔 충분하다. 더 길게 담으려면 t_us 를 델타로(가변 길이), 반복되는 폴링을 개수로 압축한다(DS-M10 의 가변 길이 레코드 링, 블랙박스 인코딩은 DRONE-M10 — 다음 단계). 115200 baud UART 로 실시간 전송은 약 11.5 KB/s 라 37 KB/s 를 못 따라간다 — 그래서 링 + 사고 시 덤프다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 16/16 passed, exit 0
```

## 10. 핵심 정리

- `bus_ops` 이음새에 데코레이터 둘: **RecPort**(진짜 버스로 넘기며 기록, 반환값 불변 — 투명성 테스트), **RpPort**(로그로 답하며 드라이버의 요청을 대조).
- 레코드 = `{seq, t, dur, bus, op, addr, reg, len, result, data}`. 실패한 트랜잭션도 결과 그대로 기록. 바이너리(버전 + CRC-32, CRC 먼저 구조 나중)와 텍스트(diff 용, 같은 CRC) 두 형식.
- 대조: strict = 전역 순서, relaxed = 장치 `(bus, addr)` 별 순서. 첫 어긋남을 `seq N: expected …, got …` 로. 안 쓰인 레코드도 문제.
- op 순서만으론 타이밍 회귀(리셋 지연 삭제)를 못 잡는다 → 같은 장치 간격이 녹화의 90 % 미만이면 위반. 재생은 칩의 물리를 모른다 — 조건이 같아야 답도 같다.
- 변형 재생: 레코드 k 마다 NODEV/NACK/TIMEOUT/SHORT → 문서화된 코드, op k+1 개에서 멈춤, 부분 커밋 0. ARLO 는 레코드를 소비하지 않아 재시도가 같은 답을 받는다.
- 비트 반전: 형식 비트 66/66 감지, 데이터 비트 0/384 — 무결성은 CRC·그럴듯함 검사의 몫이고, 스윕이 그 경계를 숫자로 문서화한다.
- 골든 = 버스 로그 + 기대 출력 두 파일 (수식 회귀는 버스가 같다). PASS strict / PASS relaxed(경고) / FAIL + diff / 다시 녹화는 리뷰 대상.
- 로그 출처: Linux i2c 트레이스 이벤트, 타깃 링 레코더(블랙박스), LA 디코드 — TxLog 하나로 모으면 재생기는 하나.

## 참고자료

- Linux kernel, `include/trace/events/i2c.h`: <https://github.com/torvalds/linux/blob/master/include/trace/events/i2c.h> — `i2c_write`/`i2c_read`/`i2c_reply`/`i2c_result` 의 TP_printk 형식 (`i2c-%d #%u a=%03x f=%04x l=%u [%*phD]`, `i2c-%d n=%u ret=%d`) (원문 확인함)
- Linux kernel, *I2C/SMBus fault codes*: <https://docs.kernel.org/i2c/fault-codes.html> — ENXIO(주소 NACK), EAGAIN(중재 패배), ETIMEDOUT (COMM-M12 에서 확인한 문서)
- Linux kernel, *Event Tracing* (`Documentation/trace/events.rst`) — `events/<subsystem>/enable`, tracefs 위치 (확인 권장); `include/trace/events/spi.h` 의 SPI 이벤트 이름 (기억 — 확인 권장)
- RevEng CRC catalogue, CRC-32/ISO-HDLC — check value 0xCBF43926 (solution.c 가 직접 계산해 단언)
- Michael Feathers, *Working Effectively with Legacy Code* — "seam" 개념 (bus_ops 가 테스트 이음새인 이유)
- 관련 레슨: COMM-M12 (bus_ops 계층화 — 이 레슨의 바탕), COMM-B05 / COMM-B07 (SPI·I2C 레지스터 칩), COMM-M02 (중재 패배 = 재시도 안전), COMM-M03 (버스 복구), COMM-A10 (LA 디코더 — 로그 출처), COMM-A11 (센서 허브 장애 주입), HWLAB-M06 (sigrok 디코더), HWLAB-B07 (캡처 형식), TEST-A05 (결정론적 시뮬레이션·가상 시간), ALGO-M08 (LCS), DS-B03 / DS-B04 / DS-M10 (링 버퍼·가변 길이 로그), DRONE-M10 (블랙박스 인코딩 — 다음 단계), C-A01 (호스트 테스트 가능한 레지스터 계층), TEST-M02 (퍼징 — 다음 단계)
