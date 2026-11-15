---
id: COMM-A11
time: 130분
updated: 2026-11-11
---
# COMM-A11 종합: 센서 허브 통신 스택 + 장애 주입

## 학습 목표

- I2C 센서 2개 + SPI NOR 로그 + UART COBS 텔레메트리(seq/ACK/재전송)를 **하나의 결정적 시뮬레이터**(1 tick = 1 ms, 협력형 스케줄러) 위에서 계층으로 엮는다: 버스 드라이버 → 센서 서비스 → 로거 / 텔레메트리.
- 계층 전체에서 쓰는 **에러 분류 하나**(`hub_err`)와 컴포넌트별 **건강 상태 기계**(OK / DEGRADED / FAULT)를 설계하고, 에러마다 "누구 탓인가"와 "무슨 복구 행동인가"를 표로 말할 수 있다.
- 버스 hang(SDA 붙잡힘·단락), 스트레칭 초과, 센서 무응답·브라운아웃, NOR hang·부분 프로그램, 링크 손상·손실·다운을 **주입**하고, 카운터·상태·회복 시간 상한을 테스트로 단언한다.
- "샘플 하나는 기록+전송되거나, 버린 이유가 카운터에 있다" — **조용한 손실 0** 을 매 tick 불변식과 샘플별 운명(fate) 추적으로 증명한다. ACK 가 없다는 건 "안 갔다"가 아니라 "모른다"임을 구분한다.
- bring-up 보고서(에러 통계표 + 이벤트 로그)를 찍고, 워치독·우선순위·필드 로그를 설계 리뷰 수준에서 토론한다.

## 1. 문제

시니어/스태프 펌웨어 면접의 설계 라운드는 결국 이 질문으로 모인다: **"센서 허브에서 버스 하나가 hang 되면 시스템 전체는 어떻게 행동하고, 그걸 하드웨어 없이 어떻게 검증하나?"** 단일 프로토콜 문제(COMM-B/M)를 다 풀 줄 알아도, 여러 개를 엮으면 새 문제가 생긴다 — 같은 버스를 쓰는 센서끼리의 운명 공유, 계층마다 재시도가 곱해지는 문제, 큐가 찰 때 무엇을 버리나, 타임아웃 뒤 상태를 모르는 문제(NOR 가 정말 썼나? 지상국이 정말 받았나?). Don 이 SSD 펌웨어에서 겪은 "전원이 끊긴 뒤 무엇이 플래시에 남았나"와 RF 모듈 통합에서 겪은 "링크가 끊겼다 붙을 때 무엇이 중복되나"가 한 보드에 모인 버전이다.

```svg
<svg viewBox="0 0 760 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a11a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">센서 허브 — 호출은 아래로, 결과(hub_err)는 번역되며 위로</text>
  <rect x="20" y="30" width="560" height="44" rx="6" class="f-mut" stroke="currentColor"/><text x="300" y="50" fill="currentColor" text-anchor="middle" font-weight="bold">건강 × 5 (OK / DEGRADED / FAULT) · 에러 통계 · 이벤트 로그</text><text x="300" y="66" fill="currentColor" text-anchor="middle" font-size="12">hub_report() — bring-up 엔지니어가 읽는 표</text>
  <rect x="20" y="100" width="176" height="62" rx="6" class="f-acc" stroke="currentColor"/><text x="108" y="120" fill="currentColor" text-anchor="middle" font-weight="bold">센서 서비스</text><text x="108" y="137" fill="currentColor" text-anchor="middle" font-size="12">tmp0 20 · hum1 50 tick</text><text x="108" y="153" fill="currentColor" text-anchor="middle" font-size="12">stale · 재초기화 백오프</text>
  <rect x="212" y="100" width="176" height="62" rx="6" class="f-acc" stroke="currentColor"/><text x="300" y="120" fill="currentColor" text-anchor="middle" font-weight="bold">로거 (큐 16)</text><text x="300" y="137" fill="currentColor" text-anchor="middle" font-size="12">20 B 레코드 · 페이지 분할</text><text x="300" y="153" fill="currentColor" text-anchor="middle" font-size="12">WIP 폴링 · 되읽기 검증</text>
  <rect x="404" y="100" width="176" height="62" rx="6" class="f-acc" stroke="currentColor"/><text x="492" y="120" fill="currentColor" text-anchor="middle" font-weight="bold">텔레메트리 (큐 16)</text><text x="492" y="137" fill="currentColor" text-anchor="middle" font-size="12">seq + ACK + 재전송</text><text x="492" y="153" fill="currentColor" text-anchor="middle" font-size="12">링크 probe (PING)</text>
  <path d="M196 120 H210" stroke="currentColor" marker-end="url(#a11a)"/><path d="M150 100 V88 H492 V98" stroke="currentColor" fill="none" marker-end="url(#a11a)"/><text x="330" y="84" fill="currentColor" font-size="12">hub_produce(): 샘플 하나 → 두 큐</text>
  <rect x="20" y="186" width="176" height="44" rx="6" class="f-ok" stroke="currentColor"/><text x="108" y="205" fill="currentColor" text-anchor="middle" font-size="12">I2C HAL → i2c_map()</text><text x="108" y="221" fill="currentColor" text-anchor="middle" font-size="12">+ 9클럭 복구 (GPIO)</text>
  <rect x="212" y="186" width="176" height="44" rx="6" class="f-ok" stroke="currentColor"/><text x="300" y="205" fill="currentColor" text-anchor="middle" font-size="12">NOR 명령</text><text x="300" y="221" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">WREN PP SE RDSR READ</text>
  <rect x="404" y="186" width="176" height="44" rx="6" class="f-ok" stroke="currentColor"/><text x="492" y="205" fill="currentColor" text-anchor="middle" font-size="12">COBS + CRC-16 프레임</text><text x="492" y="221" fill="currentColor" text-anchor="middle" font-size="12">스트리밍 수신 (0x00 재동기)</text>
  <rect x="20" y="256" width="176" height="56" rx="6" class="f-mut" stroke="currentColor"/><text x="108" y="278" fill="currentColor" text-anchor="middle" font-size="12">I2C 버스 (wired-AND)</text><text x="108" y="296" fill="currentColor" text-anchor="middle" font-size="12">tmp0 0x48 · hum1 0x40</text>
  <rect x="212" y="256" width="176" height="56" rx="6" class="f-mut" stroke="currentColor"/><text x="300" y="278" fill="currentColor" text-anchor="middle" font-size="12">SPI NOR 16 KB</text><text x="300" y="296" fill="currentColor" text-anchor="middle" font-size="12">4 섹터 × 4 KB, 페이지 256 B</text>
  <rect x="404" y="256" width="176" height="56" rx="6" class="f-mut" stroke="currentColor"/><text x="492" y="278" fill="currentColor" text-anchor="middle" font-size="12">UART 115200 (11 B/tick)</text><text x="492" y="296" fill="currentColor" text-anchor="middle" font-size="12">⇄ 지상국 (중복 제거 + ACK)</text>
  <path d="M108 162 V184" stroke="currentColor" marker-end="url(#a11a)"/><path d="M300 162 V184" stroke="currentColor" marker-end="url(#a11a)"/><path d="M492 162 V184" stroke="currentColor" marker-end="url(#a11a)"/><path d="M108 230 V254" stroke="currentColor" marker-end="url(#a11a)"/><path d="M300 230 V254" stroke="currentColor" marker-end="url(#a11a)"/><path d="M492 230 V254" stroke="currentColor" marker-end="url(#a11a)"/>
  <rect x="604" y="100" width="146" height="212" rx="6" class="f-warn" stroke="currentColor"/><text x="677" y="120" fill="currentColor" text-anchor="middle" font-weight="bold">장애 주입기</text><text x="677" y="137" fill="currentColor" text-anchor="middle" font-size="12">스크립트 + 무작위</text><text x="677" y="153" fill="currentColor" text-anchor="middle" font-size="12">(고정 시드)</text>
  <text x="614" y="178" fill="currentColor" font-size="12">SDA hold / short</text><text x="614" y="196" fill="currentColor" font-size="12">sensor off (NACK)</text><text x="614" y="214" fill="currentColor" font-size="12">stretch &gt; 1000 µs</text><text x="614" y="232" fill="currentColor" font-size="12">NOR busy hang</text><text x="614" y="250" fill="currentColor" font-size="12">NOR 부분 프로그램</text><text x="614" y="268" fill="currentColor" font-size="12">link corrupt / loss</text><text x="614" y="286" fill="currentColor" font-size="12">link down (방향별)</text>
  <path d="M677 312 V332 H108 V314" stroke="currentColor" fill="none" marker-end="url(#a11a)"/><path d="M300 332 V314" stroke="currentColor" marker-end="url(#a11a)"/><path d="M492 332 V314" stroke="currentColor" marker-end="url(#a11a)"/>
  <text x="10" y="358" fill="currentColor" font-size="12">1 tick = faults_apply → 모델 tick → sensor_task → logger_task → tlm_task → 지상국 → 불변식 검사</text>
  <text x="10" y="378" fill="currentColor" font-size="12">각 태스크는 막히지 않는다: 기다림은 상태 + 마감 시각으로 표현하고 다음 tick 에 다시 본다 (while 폴링 금지).</text>
</svg>
```

이 레슨의 시스템 (값은 전부 **이 모델의 정의**. 실제 부품 값은 "대표값"):

| 구성 | 정의 |
|---|---|
| 센서 | tmp0 (0x48, WHO 0xA1, 20 tick 주기) · hum1 (0x40, WHO 0xB2, 50 tick, 평소 400 µs 스트레칭). 레지스터 DATA 0x00 (BE16) · CFG 0x02 (bit0 EN) · WHO 0x0F. 전원이 다시 들어오면 CFG = 0 → DATA = 0x8000 |
| I2C 컨트롤러 | 스트레칭 한도 1000 µs (SMBus t_TIMEOUT 25–35 ms 는 대표값). 한도 초과 → 마스터는 포기, 슬레이브는 바이트 중간이라 SDA 를 3 클럭 더 잡는다 (COMM-M01/M03 축약판) |
| NOR | 16 KB, 섹터 4 KB, 페이지 256 B, PP 2 tick, SE 25 tick. WIP 중엔 RDSR 말고 무시, WREN 없는 PP/SE 무시, PP 는 페이지 안에서 감김, SE 중 hang → 섹터 절반만 지워진 채 중단 (COMM-A06 축약판) |
| 로그 레코드 | 20 B 슬롯 `[A5][sensor][id LE32][t LE32][value LE16][flags][FF×5][CRC16 BE]`, 섹터당 204 슬롯, 4 섹터 순환 |
| 링크 | 115200 8N1 ≈ 11 B/tick, 지연 1 tick. 와이어 = `COBS(평문 ‖ CRC-16/CCITT-FALSE) ‖ 00` (COMM-B12). DATA 14 B 평문 → 18 B 프레임 |
| ARQ | stop-and-wait (COMM-M14 축약판), 8비트 seq, RTO 10 → 80 tick (2배), 최대 4번 전송, 연속 실패 2 → PING probe 100 tick 마다 |
| 건강 | 에러 1번 → DEGRADED, 연속 3번 → FAULT, 연속 성공 2번 → OK. 에러로는 절대 좋아지지 않는다 |

### Part A — 정상 경로: 번역, 로거, 텔레메트리

- A1 `i2c_map(hal)`: HAL 코드 → `hub_err`, 0..255 전부에 전사적(total) — 모르는 코드는 `HUB_EIO`, 실패를 OK 로 바꾸지 않는다 (COMM-M12 규칙).
- A2 `logger_task(h, now)`: tick 마다 한 번, 절대 기다리지 않는다. 섹터 첫 슬롯이면 지우기, 레코드가 페이지 경계를 넘으면 **PP 두 번으로 분할**, RDSR 로 WIP 폴링(한도 PP 10 / SE 100 tick), 끝나면 **되읽어 비교**한 뒤에만 커밋.
- A3 `tlm_task(h, now)`: ACK 수신(바이트 스트림 → COBS → CRC → seq 비교), 타임아웃 → 지수 백오프 재전송, 4번 실패 → `FAILED` 로 보고하고 seq 전진, 연속 실패 2번이면 데이터 대신 PING 으로 링크를 두드린다.
- 테스트: 레코드 300개 → 경계 넘는 18개는 PP 두 번(`programs == 318`), 섹터 회전, 부팅 스캔으로 300개 전부 확인. 깨끗한 링크 50개 → 재전송 0. 전체 2000 tick → 140 샘플 전부 기록 + ACK, 상태 전이 0.

### Part B — 장애 주입과 복구 정책

- B1 `q_push_drop_oldest`, B2 `health_update`, B3 `i2c_bus_recover` (9 펄스 + STOP), B4 센서 정책 (`bus_try_recover`, `sensor_fail`, `sensor_sample`).
- 주입하는 장애와 단언 (전부 실제로 돈다):

| 장애 | 단언 |
|---|---|
| tmp0 가 바이트 중간에 멈춰 SDA 를 5 클럭 잡음 | 복구 1번, 정확히 5 펄스, miss 1, 센서 상태 전이 0 (버스 탓) |
| SDA 단락 300 tick | 버스 FAULT → 백오프 복구(10→160), 끝난 뒤 ≤ 160 + 2·20 tick 에 OK, 두 센서 stale 1번, 센서 재초기화 0 |
| hum1 무응답 2000 tick | 재초기화 간격 100, 200, 400, 400 … (상한 8 주기), tmp0·버스 영향 0, 돌아온 뒤 ≤ 500 tick 에 OK |
| hum1 브라운아웃 10 tick (샘플 사이) | 다음 읽기 0x8000 → EIO 1번 → 재초기화 → 0x8000 이 샘플로 나가지 않음 |
| tmp0 부팅부터 없음 | FAULT, 시도 수가 백오프로 제한, hum1 은 정상, 나중에 꽂으면 회복 |
| hum1 스트레칭 3000 µs × 200 tick | ETIMEDOUT 4 = 즉시 복구 4 (3 펄스씩), tmp0 miss 0, EBUSY 0 |
| NOR hang 1000 tick | FAULT, 바쁜 칩에 명령 0개, 로그 큐 drop-oldest, 텔레메트리 영향 0 |
| NOR 부분 프로그램 60 tick | 되읽기 실패 → 슬롯 태움, NOR 에 중복·깨진 레코드 0 |
| 분할 레코드 첫 조각 직후 hang | 타임아웃 뒤 두 번째 조각을 마저 쓰고 검증 통과 (태움 0) |
| 옛 데이터(0x00)가 찬 칩, 지우기 중 hang | 타임아웃 난 지우기를 **다시** 지움 (태움 0) |
| 링크 손상 1 % / 손실 2 % | 재전송 > 0, 지상국 중복 억제, 거짓 수락 0, 중복 전달 0 |
| 링크 다운 1000 tick / ACK 방향만 다운 / 20000 tick | 실패 정확히 2개 뒤 PING 만, 회복 ≤ 120 tick, "실패"했지만 도착한 샘플 존재, PING 181번에도 seq 간격 ≤ 3 |

### Part C — bring-up 보고서와 soak

- C1 `hub_report()`: snprintf 의미(잘려도 NUL, 반환은 필요한 길이)로 컴포넌트별 상태·에러 표 + 샘플 장부를 찍는다.
- 복합 장애(SDA 단락 → hum1 꺼짐 → NOR hang → 링크 다운 → tmp0 스트레칭)를 겹쳐 돌리고 이벤트 로그와 보고서를 읽는다.
- 무작위 soak: 시드 3개 × 20000 tick (장애 63–79개), **매 tick 불변식** 10개, 끝나면 샘플 하나하나의 운명을 NOR 부팅 스캔·지상국 수신 집합과 대조. 긴 측정은 `demo_soak.c`.

## 2. Problem (English)

> **Sensor hub comm stack under fault injection.**
>
> A hub MCU samples two I2C sensors (50 Hz and 20 Hz), logs every sample to an SPI NOR flash, and streams every sample over a UART radio link to a ground station. Build the comm stack on a deterministic tick simulator (1 tick = 1 ms, cooperative scheduler, no threads, no dynamic allocation):
>
> ```c
> typedef enum { HUB_OK = 0, HUB_EAGAIN = -1, HUB_ENODEV = -2, HUB_EIO = -3, HUB_ETIMEDOUT = -4,
>                HUB_EBUSY = -5, HUB_ECRC = -6, HUB_ENOSPC = -7, HUB_ELINK = -8, HUB_EBADID = -9, HUB_EINVAL = -10 } hub_err;
> hub_err i2c_map(int hal_status);                       /* total translation */
> void    health_update(Hub *h, int comp, hub_err e, uint32_t now);
> hub_err i2c_bus_recover(I2cBusM *b, uint8_t *pulses);  /* 9 clocks + STOP */
> void    sensor_task(Hub *h, uint32_t now);             /* periodic sampling, staleness, recovery policy */
> void    logger_task(Hub *h, uint32_t now);             /* NOR append, never blocks */
> void    tlm_task(Hub *h, uint32_t now);                /* COBS + CRC-16 + seq/ACK/retransmit */
> size_t  hub_report(const Hub *h, uint32_t now, char *buf, size_t cap);
> ```
>
> 1. Every component (I2C bus, each sensor, NOR, link) has a health state OK / DEGRADED / FAULT driven by one error taxonomy. Define who is blamed for each error and what recovery action follows (bus recovery, re-init with exponential backoff, mark stale, retransmit, probe the link, drop-oldest).
> 2. Inject: SDA held or shorted low, sensor NACK forever, clock stretch beyond the controller timeout, NOR busy hang and partial program, UART byte corruption/loss, link down. For each, assert the counters, the states reached and a recovery-time bound in ticks.
> 3. End-to-end invariant: every sample is either logged and delivered exactly once, or accounted for in a counter — no silent loss. A sample whose retransmissions were exhausted may still have arrived; account for that honestly.
> 4. Print a bring-up report a hardware engineer can read.
>
> Example (wire bytes of one telemetry frame, seq 7, id 0x01020304, sensor 1, t = 1234, value −2):
>
> ```text
> 08 01 07 04 03 02 01 01 03 D2 04 01 05 FE FF 97 9B 00   (18 bytes, CRC-16/CCITT-FALSE = 0x979B)
> ```
>
> Follow-ups: Where does the watchdog get kicked? What happens to the log if the flash hangs mid-erase? Why can't the hub know whether a "failed" sample arrived? What would you change on an RTOS?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 열 군데: Part A — `i2c_map`(A1), `logger_task`(A2), `tlm_task`(A3) / Part B — `q_push_drop_oldest`(B1), `health_update`(B2), `i2c_bus_recover`(B3), `bus_try_recover` · `sensor_fail` · `sensor_sample`(B4) / Part C — `hub_report`(C1). 하드웨어 모델 셋, 지상국, 장애 주입기, 스케줄러(`sensor_task`, `hub_tick`), 센서 드라이버(`sens_init`, `sens_read`), 레코드·프레임 코덱, 불변식 검사기, 하네스는 주어진다. 스텁은 아무것도 안 하거나 안전한 오답(`HUB_EIO`, `HUB_EAGAIN`, `false`)을 돌려주므로 모든 루프가 끝난다. 처음 실행 (실제 출력, 중간 생략):

```text
-- A1 error translation --
FAIL starter.c:964  i2c_map(I2C_HAL_OK) == HUB_OK
FAIL starter.c:965  i2c_map(I2C_HAL_NACK_ADDR) == HUB_ENODEV
…
-- A2 logger: page split, WIP polling, read-back verify, sector rotation --
logged=0 splits=0 erases=0 programs=0 timeouts=0 burned=0
…
92/158 passed
```

통과하는 92개는 주어진 모델의 자체 점검과, 아무 일도 안 일어나서 우연히 맞는 단언("바쁜 칩에 명령 0개", "중복 전달 0")이다. 첫 목표는 A1 → A2 → A3 로 `-- A4 whole hub, no faults --` 가 140 샘플을 다 기록·전송하게 만드는 것.

## 4. 힌트

<details>
<summary>힌트 1 — 기다림은 상태다 (logger_task)</summary>

`while (rdsr() & WIP) {}` 를 쓰는 순간 협력형 스케줄러 전체가 멈춘다 — 센서 샘플링도 텔레메트리도. 대신 `st = LG_WAIT; wait_start = now; return;` 하고 다음 tick 에 RDSR 을 한 번 본다. 한도를 넘으면 **명령을 더 보내지 말고** `retry_at = now + backoff` 로 물러난다. IDLE 로 돌아와서도 첫 일은 RDSR: 타임아웃 난 동작이 아직 진행 중일 수 있다.

</details>

<details>
<summary>힌트 2 — 페이지 경계 분할</summary>

`a = slot_addr + off; chunk = min(REC_SIZE - off, NOR_PAGE - a % NOR_PAGE)`. 20 B 레코드 슬롯 12 는 0x0F0..0x103 이라 16 B + 4 B 로 나뉜다. 두 조각 사이에 WIP 폴링이 끼므로 "지금 몇 바이트까지 썼나"(`off`)가 상태에 들어간다. 레코드 끝까지 쓰면 슬롯을 `nor_read` 로 되읽어 `memcmp` — 이것 하나로 부분 프로그램, 타임아웃 뒤 불확실성, 덜 지워진 섹터를 모두 잡는다.

</details>

<details>
<summary>힌트 3 — 누구 탓인가 (sensor_fail)</summary>

NACK(ENODEV)·0x8000(EIO)·WHO 불일치(EBADID)는 **버스가 대답한 것**이다 → 버스 건강 OK, 센서 건강 에러, 재초기화 백오프. EBUSY 는 START 도 못 만든 것 → 센서는 무죄, 버스만 에러 + 즉시 복구. ETIMEDOUT 은 둘 다: 센서가 늘였고, 버스는 반쯤 끝난 상태라 복구가 필요하다.

</details>

<details>
<summary>힌트 4 — 실패한 DATA 와 실패한 PING 의 seq</summary>

DATA 가 4번 다 ACK 없이 끝나도 지상국은 받았을 수 있다 (ACK 만 잃음). 그 seq 를 다음 샘플에 다시 쓰면 지상국은 "중복"으로 버리고 ACK 만 돌려준다 → 허브는 성공으로 착각 = 조용한 손실. 그래서 DATA 실패 뒤엔 seq++ (COMM-M14). PING 은 데이터가 없으니 같은 seq 를 재사용해도 안전하고, 오히려 **재사용해야** 긴 장애 동안 seq 간격이 128 을 넘지 않는다.

</details>

<details>
<summary>힌트 5 — 건강 상태의 비대칭</summary>

에러는 목표 상태(err_run ≥ 3 ? FAULT : DEGRADED)가 **지금보다 나쁠 때만** 바꾼다. 성공은 연속 2번이어야 OK. 이 비대칭이 없으면 "FAULT → 성공 1번 → 에러 1번 → DEGRADED" 처럼 에러가 상태를 좋게 만드는 버그가 생긴다 (변이 M8 이 바로 이것).

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. **데이터의 일생부터**: 샘플 하나가 태어나 두 싱크(로그, 텔레메트리)에서 각각 어떤 끝을 맞을 수 있는지 나열한다 — 기록됨 / 큐에서 밀려남, ACK 받음 / 실패(모름) / 큐에서 밀려남. 이 목록이 곧 불변식이다.
2. **기다림을 없앤다**: 협력형 루프에서 블로킹은 hang 과 같다. 모든 기다림은 상태 + 마감 시각.
3. **에러 하나의 언어**: 계층마다 번역하고 (COMM-M12), "누구 탓 / 무슨 행동"을 표로 고정한다.
4. **건강은 행동의 입력이 아니라 관찰의 출력**: 복구 행동은 에러 코드가 정하고, 건강 상태는 그 결과를 사람이 읽게 요약한다. 상태가 정책을 다시 바꾸는 곳(링크 probe 모드)은 한 군데로 제한한다.
5. **증명**: 매 tick 불변식 + 끝난 뒤 샘플별 대조 + 무작위 soak + 변이(mutant)로 테스트 자체를 검증.

### Part A — 에러 분류 하나

| hub_err | 누가 만드나 | 버스 건강 | 컴포넌트 건강 | 복구 행동 |
|---|---|---|---|---|
| `ENODEV` | I2C 주소 NACK | OK (대답함) | 센서 에러 | 재초기화, 백오프 1→2→4→8 주기 |
| `EIO` | 데이터 NACK, DATA = 0x8000, NOR 되읽기 불일치 | OK | 센서 / NOR 에러 | 센서: 재초기화 / NOR: 슬롯 태움 + 백오프 |
| `EBADID` | WHO_AM_I 불일치 | OK | 센서 에러 | 재초기화 백오프 (BOM 확인 로그) |
| `ETIMEDOUT` | 스트레칭 초과, NOR WIP 한도, DATA 4번 무응답 | I2C: 에러 | 센서 / NOR / 링크 에러 | I2C: 즉시 버스 복구 / NOR: 물러남 / 링크: seq++ |
| `EBUSY` | START 불가 (SDA LOW), 복구 실패 | 에러 | — (센서 무죄) | 9클럭 복구, 실패 시 백오프 10→160 |
| `ECRC` | 깨진 ACK 프레임 | — | 링크 카운터만 | 없음 (재전송이 처리) |
| `ENOSPC` | 큐 가득 참 | — | NOR / 링크 카운터만 | drop-oldest |
| `ELINK` | PING 무응답 | — | 링크 에러 | 100 tick 뒤 다시 PING |
| `EAGAIN` | 백오프 중이라 이번 슬롯은 건너뜀 | — | — | 없음 (miss 로 셈) |

`i2c_map` 은 `switch` + `default: return HUB_EIO`. 하네스는 0..255 전부를 넣어 결과가 도메인 안인지, 0 이 아닌 입력이 OK 로 바뀌지 않는지 본다.

### Part A — 로거: 페이지 분할과 되읽기

```svg
<svg viewBox="0 0 760 240" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="a11b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">20 B 슬롯 12 = 0x0F0..0x103 — 페이지 경계 0x100 을 넘는다</text>
  <text x="40" y="44" fill="currentColor" font-family="monospace">0x0DC</text><text x="250" y="44" fill="currentColor" font-family="monospace">0x0F0</text><text x="420" y="44" fill="currentColor" font-family="monospace">0x100</text><text x="474" y="44" fill="currentColor" font-family="monospace">0x104</text><text x="680" y="44" fill="currentColor" font-family="monospace">0x118</text>
  <text x="10" y="72" fill="currentColor" font-size="13">분할</text>
  <rect x="40" y="56" width="220" height="26" class="f-ok" stroke="currentColor"/><text x="150" y="74" fill="currentColor" text-anchor="middle">slot 11</text>
  <rect x="260" y="56" width="176" height="26" class="f-acc" stroke="currentColor"/><text x="348" y="74" fill="currentColor" text-anchor="middle">PP #1: 16 B @0x0F0</text>
  <rect x="436" y="56" width="44" height="26" class="f-acc" stroke="currentColor"/><text x="458" y="74" fill="currentColor" text-anchor="middle">#2 4B</text>
  <rect x="480" y="56" width="220" height="26" class="f-mut" stroke="currentColor"/><text x="590" y="74" fill="currentColor" text-anchor="middle">slot 13 (다음)</text>
  <text x="96" y="128" fill="currentColor" font-size="13">한 번에 (naive)</text>
  <rect x="40" y="110" width="40" height="26" class="f-warn" stroke="currentColor"/><text x="60" y="128" fill="currentColor" text-anchor="middle">!</text>
  <rect x="260" y="110" width="176" height="26" class="f-acc" stroke="currentColor"/><text x="348" y="128" fill="currentColor" text-anchor="middle">PP 20 B: 앞 16 B 만 제자리</text>
  <rect x="436" y="110" width="44" height="26" class="f-mut" stroke="currentColor" stroke-dasharray="3 2"/><text x="458" y="128" fill="currentColor" text-anchor="middle">FF</text>
  <path d="M436 142 C 300 175, 120 175, 62 140" stroke="currentColor" fill="none" marker-end="url(#a11b)"/><text x="100" y="186" fill="currentColor">뒤 4 B 는 같은 페이지 맨 앞 0x000..0x003 으로 감겨 slot 0 을 AND 로 망가뜨린다</text>
  <path d="M436 50 V146" stroke="currentColor" stroke-dasharray="5 3"/>
  <text x="10" y="212" fill="currentColor">실측 (test_a2): 레코드 300개 중 경계를 넘는 것 18개 → programs = 318, 감김(wraps) 0, 부팅 스캔 300/300.</text>
  <text x="10" y="230" fill="currentColor">분할을 빼면 (변이 M1): 불변식 "nor command discipline" 이 감김을 잡고 109/158 로 떨어진다.</text>
</svg>
```

`logger_task` 의 한 tick 은 이 순서다:

| 상태 | 하는 일 | 다음 |
|---|---|---|
| `now < retry_at` | 아무것도 안 함 (타임아웃·태움 뒤 백오프) | 그대로 |
| `WAIT`, RDSR WIP=1 | 한도 넘었나? 넘었으면 `timeouts++`, `suspect`, 백오프 2배 (4→64). **명령은 보내지 않는다** | 그대로 |
| `WAIT`, WIP=0, `OP_ERASE` | `suspect` 가 아니면 `erased[sector]` / suspect 면 표시 안 함 → 다시 지운다 (지우기는 멱등) | IDLE |
| `WAIT`, WIP=0, `OP_PROG` | `off += chunk`. 끝이면 슬롯 되읽기: 같음 → 커밋(LOGGED) / 다름 → 태움 + 백오프, 같은 샘플을 다음 슬롯에 | IDLE |
| `IDLE` | 레코드가 없으면 큐에서 꺼냄 → RDSR 이 바쁘면 `WAIT(OP_NONE)` → 섹터 끝이면 다음 섹터 → 지우기가 필요하면 `overwritten += recs_in`, WREN+SE → 아니면 조각 계산, WREN+PP | WAIT |

중요한 선택 두 가지:

- **꺼낸 레코드는 큐 밖(`cur`)에 둔다.** 큐 안에 둔 채 쓰면, NOR 가 바쁜 동안 drop-oldest 가 바로 그 레코드를 버리고 → 나중에 쓰기가 완료되면 "버렸다고 센 샘플이 플래시에 있다" — 장부가 깨진다.
- **되읽기 검증이 모든 불확실성을 한 곳에서 닫는다.** 타임아웃 뒤 "썼나 안 썼나"를 추측하지 않고, 끝까지 쓴 다음 비교한다. 지우기는 되읽기(4 KB) 대신 다시 지운다 — 싸고 멱등이니까. 반대로 타임아웃 난 지우기를 믿으면(변이 M12) 실측: `erases=1 burned=35 logged=102` 에서 멈춘다 — 섹터 뒤 절반의 모든 슬롯이 검증에 실패한다.

### Part A — 텔레메트리: stop-and-wait + probe

메시지 하나의 일생 (COMM-M14 와 같은 뼈대, 프레이밍만 COBS):

1. `q_pop` → `cur`, `tx_count = 0`, `rto = 10`, `deadline = now + 10`, 전송.
2. 하향 바이트를 `cobs_rx_feed` 에 하나씩. 0x00 에서 COBS + CRC 를 판정. `ACK(seq == T->seq)` 면 완료: `acked++`, `seq++`, 건강 OK.
3. `now ≥ deadline` 이면 RTO 2배(최대 80) 재전송. 4번째 전송도 무응답이면 `failed++`, `seq++`, 건강 ETIMEDOUT. 연속 2번이면 `probing`.
4. probing 중엔 큐를 건드리지 않고 100 tick 마다 PING 하나 (1번 전송, 같은 seq). ACK 오면 probing 해제 + EV_LINK_UP.

RTT 는 DATA 18 B (2 tick 직렬화 + 1 지연) + ACK 6 B (1 + 1) ≈ 3–4 tick, 그래서 RTO 10 은 약 3배. 한 메시지의 최악 실패 시간 = 10 + 20 + 40 + 80 = 150 tick, probing 진입까지 300 tick, 그동안 샘플 0.07 개/tick × 300 ≈ 21 개가 들어와 큐(16)가 넘친다 → drop-oldest 가 시작된다. 텔레메트리에서 오래된 것부터 버리는 이유: 지상의 운용자에게는 **최신 값**이 중요하고, 과거는 NOR 로그에 남아 있다.

### Part B — 건강 상태 기계

```svg
<svg viewBox="0 0 760 270" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="a11c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">health_update: 에러는 나빠지게만, 회복은 연속 성공 2번으로</text>
  <rect x="40" y="50" width="140" height="50" rx="25" class="f-ok" stroke="currentColor"/><text x="110" y="80" fill="currentColor" text-anchor="middle" font-size="14" font-weight="bold">OK</text>
  <rect x="310" y="50" width="140" height="50" rx="25" class="f-acc" stroke="currentColor"/><text x="380" y="80" fill="currentColor" text-anchor="middle" font-size="14" font-weight="bold">DEGRADED</text>
  <rect x="580" y="50" width="140" height="50" rx="25" class="f-warn" stroke="currentColor"/><text x="650" y="80" fill="currentColor" text-anchor="middle" font-size="14" font-weight="bold">FAULT</text>
  <path d="M180 66 H308" stroke="currentColor" marker-end="url(#a11c)"/><text x="244" y="60" fill="currentColor" text-anchor="middle">에러 1번</text>
  <path d="M308 86 H182" stroke="currentColor" marker-end="url(#a11c)"/><text x="244" y="102" fill="currentColor" text-anchor="middle">연속 성공 2번</text>
  <path d="M450 75 H578" stroke="currentColor" marker-end="url(#a11c)"/><text x="514" y="68" fill="currentColor" text-anchor="middle">err_run ≥ 3</text>
  <path d="M650 100 C 650 150, 110 150, 110 102" stroke="currentColor" fill="none" marker-end="url(#a11c)"/><text x="380" y="164" fill="currentColor" text-anchor="middle">연속 성공 2번 (rec 시간 = 지금 − OK 를 떠난 시각)</text>
  <path d="M690 50 C 720 20, 740 40, 712 56" stroke="currentColor" fill="none" marker-end="url(#a11c)"/><text x="684" y="36" fill="currentColor" text-anchor="end">에러: 그대로 (DEGRADED 로 안 감)</text>
  <text x="10" y="188" fill="currentColor" font-size="13" font-weight="bold">컴포넌트별 복구 행동 (에러 코드가 정하고, 상태는 요약일 뿐)</text>
  <text x="10" y="206" fill="currentColor">i2c0 : EBUSY/ETIMEDOUT → 9클럭 + STOP 즉시, 실패하면 백오프 10 → 160 tick. 그동안 센서 슬롯은 miss (센서는 무죄)</text>
  <text x="10" y="224" fill="currentColor">tmp0/hum1 : ENODEV/EIO/EBADID → inited = false, 재초기화 1 → 2 → 4 → 8 주기. 3 주기 동안 좋은 샘플이 없으면 stale</text>
  <text x="10" y="242" fill="currentColor">nor0 : 타임아웃 → 명령 중지 + 백오프 4 → 64, 되읽기 실패 → 슬롯 태움 + 백오프. 큐는 drop-oldest (ENOSPC 카운터)</text>
  <text x="10" y="260" fill="currentColor">uart0 : RTO 10 → 80 × 4번 → FAILED + seq++, 연속 2번 → PING probe 100 tick. 큐는 drop-oldest</text>
</svg>
```

B2 단위 테스트가 그대로 이 그림이다: `OK, EIO→DEG, OK→DEG, OK→OK, ETIMEDOUT×3→FAULT, OK→FAULT, EIO→FAULT(!), OK, OK→OK`. 전이 5번, `t=140` 에 OK 를 떠나 `t=200` 에 돌아와 `rec_max = 60`.

### Part B — 시나리오 결과 (solution.c 실제 출력)

```text
-- B: tmp0 stops mid-byte holding SDA for 5 clocks at t=400 --
rec 1/1 last_pulses=5 bus EBUSY=1 misses tmp0=1 hum1=0 bus rec_max=5
-- B: SDA shorted low t=[1000,1300) --
bus worst=FAULT rec 1/6 back_ok=t1320 (fault end +20) stale_events tmp0=1 hum1=1 misses 16/7
-- B: hum1 NACKs t=[300,2300) (powered off), then comes back reset --
hum1 init attempts after t=300: 355 455 655 1055 1455 1855 2255 2655  (back OK at t=2705)
-- B: hum1 stretches 3000 us (> 1000 us limit) t=[600,800) --
hum1 ETIMEDOUT=4 state=OK | bus ETIMEDOUT=4 EBUSY=0 rec 4/4 pulses=3 | tmp0 misses=0
-- B: NOR hangs busy t=[500,1500) --
nor worst=FAULT timeouts=17 log_dropped=59 back_ok=t1566 | tlm dropped=0 failed=0 | ignored-while-busy=0
-- B: chip full of old data (0x00), NOR hangs t=[3,200) during the first sector erase --
erase_aborts=1 erases=3 timeouts=1 burned=0 logged=245
-- B: link down both ways t=[1000,2000) --
link worst=FAULT failed=2 dropped=58 pings=8 back_ok=t2075 (+75) | log dropped=0
-- B: only the downlink (ACKs) is down t=[1000,1400) -> 'failed' but delivered --
failed=2 acked=126 ground_unique=128 dup_suppressed=7
-- B: link down both ways for 20000 ticks (> 128 probe periods) --
pings=181 failed=2 seq=116 ground_last=115 acked=113 ground_unique=113 link=OK
```

읽는 법:

- **hum1 무응답**: 305 에 읽기 실패 → 355, 455, 655, 1055 … 간격 100, 200, 400, 400 — 백오프가 상한(8 × 50)에 붙었다. 2300 에 전원이 돌아왔지만 다음 시도는 2655 (지연 355). 연습 4 에서 이 지연의 최악이 정확히 `C·P − 1 = 399` tick 임을 전수로 보인다.
- **스트레칭**: 늘이는 hum1 이 매번 타임아웃 → 매번 즉시 3 펄스 복구 → tmp0 는 한 번도 놓치지 않았다 (EBUSY 0). 복구를 다음 슬롯으로 미루면(변이 M9) tmp0 가 EBUSY 를 맞는다.
- **ACK 방향만 다운**: `failed=2` 인데 지상국은 `acked + 2` 개를 받았다. "실패" = **모름**. 정확한 장부는 "acked ≤ 지상국 수신 ≤ acked + failed + 진행 중 1".
- **20000 tick 장애**: PING 181번 동안 seq 는 실패한 DATA 2개만큼만 움직였다. PING 이 seq 를 소모하면(변이 M11) 실측 `acked=113 ground_unique=40` — 허브는 113개가 갔다고 믿지만 73개가 조용히 사라졌다: 간격이 128 을 넘어 지상국이 새 메시지를 "중복"으로 버리고 ACK 만 돌려줬기 때문이다 (COMM-M14 의 seq 공간 조건).

### Part B — 샘플 하나의 운명과 불변식

```svg
<svg viewBox="0 0 760 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="a11d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <rect x="6" y="90" width="108" height="40" rx="6" class="f-acc" stroke="currentColor"/><text x="60" y="108" fill="currentColor" text-anchor="middle">sample</text><text x="60" y="123" fill="currentColor" text-anchor="middle">id = produced++</text>
  <path d="M114 98 L140 66" stroke="currentColor" marker-end="url(#a11d)"/><path d="M114 122 L140 154" stroke="currentColor" marker-end="url(#a11d)"/>
  <rect x="142" y="30" width="100" height="36" rx="4" class="f-mut" stroke="currentColor"/><text x="192" y="52" fill="currentColor" text-anchor="middle">로그 큐 (16)</text>
  <rect x="290" y="30" width="110" height="36" rx="4" class="f-mut" stroke="currentColor"/><text x="345" y="52" fill="currentColor" text-anchor="middle">cur (큐 밖)</text>
  <rect x="450" y="30" width="120" height="36" rx="4" class="f-mut" stroke="currentColor"/><text x="510" y="46" fill="currentColor" text-anchor="middle">PP (+분할)</text><text x="510" y="60" fill="currentColor" text-anchor="middle">→ 되읽기</text>
  <rect x="620" y="30" width="120" height="36" rx="4" class="f-ok" stroke="currentColor"/><text x="680" y="52" fill="currentColor" text-anchor="middle">LOGGED</text>
  <path d="M242 48 H288" stroke="currentColor" marker-end="url(#a11d)"/><path d="M400 48 H448" stroke="currentColor" marker-end="url(#a11d)"/><path d="M570 48 H618" stroke="currentColor" marker-end="url(#a11d)"/>
  <path d="M510 66 C 510 92, 345 92, 345 68" stroke="currentColor" fill="none" marker-end="url(#a11d)"/><text x="428" y="106" fill="currentColor" text-anchor="middle">불일치 → 태움, 다음 슬롯</text>
  <path d="M192 30 V14 H560" stroke="currentColor" fill="none" marker-end="url(#a11d)"/><text x="564" y="18" fill="currentColor">LOG_DROPPED (drop-oldest)</text>
  <rect x="142" y="152" width="100" height="36" rx="4" class="f-mut" stroke="currentColor"/><text x="192" y="174" fill="currentColor" text-anchor="middle">텔레 큐 (16)</text>
  <rect x="290" y="152" width="110" height="36" rx="4" class="f-mut" stroke="currentColor"/><text x="345" y="174" fill="currentColor" text-anchor="middle">in-flight</text>
  <rect x="450" y="132" width="120" height="30" rx="4" class="f-ok" stroke="currentColor"/><text x="510" y="152" fill="currentColor" text-anchor="middle">ACKED</text>
  <rect x="450" y="178" width="120" height="30" rx="4" class="f-warn" stroke="currentColor"/><text x="510" y="198" fill="currentColor" text-anchor="middle">FAILED = 모름</text>
  <path d="M242 170 H288" stroke="currentColor" marker-end="url(#a11d)"/><path d="M400 164 L448 150" stroke="currentColor" marker-end="url(#a11d)"/><path d="M400 178 L448 192" stroke="currentColor" marker-end="url(#a11d)"/>
  <path d="M192 188 V226 H448" stroke="currentColor" fill="none" marker-end="url(#a11d)"/><text x="452" y="230" fill="currentColor">TLM_DROPPED</text>
  <text x="590" y="110" fill="currentColor" font-family="monospace">매 tick:</text>
  <text x="590" y="128" fill="currentColor" font-family="monospace" font-size="12">P = L+Ld+|Lq|+cur</text>
  <text x="590" y="146" fill="currentColor" font-family="monospace" font-size="12">P = A+F+Td+|Tq|+fl</text>
  <text x="590" y="164" fill="currentColor" font-family="monospace" font-size="12">A ≤ G ≤ A+F+fl</text>
  <text x="590" y="182" fill="currentColor" font-family="monospace" font-size="12">slots = smp+miss</text>
  <text x="10" y="246" fill="currentColor">P 생산 · L 기록 · Ld/Td 버림 · Lq/Tq 큐 · A ACK · F 실패 · G 지상국이 받은 수 · fl 진행 중 DATA (0/1)</text>
</svg>
```

불변식 10개 (`inv_check`, 매 tick): 로그 장부, 텔레메트리 장부, 센서별 `slots = samples + misses`, 샘플 합 = produced, 지상국 중복 전달 0·값 오류 0, `acked ≤ 지상국 수 ≤ acked + failed + 진행 중`, NOR 명령 규율(바쁠 때 명령 0, WREN 없는 PP 0, 페이지 감김 0), 큐 용량, 건강 범위·"OK 인데 err_run > 0" 금지, 운명 두 번 배정 금지. 끝난 뒤 `final_check`: 모든 id 가 두 싱크에서 정확히 하나의 운명, NOR 부팅 스캔의 유효 레코드 수 = `logged − overwritten` 이고 전부 LOGGED, ACKED 는 지상국에 있고, 지상국에 있는 것은 ACKED 또는 FAILED. 지상국은 받은 값이 `phys_value(sensor, t)` 와 같은지도 본다 — CRC 거짓 수락이 있었다면 여기서 터진다.

### Part C — 복합 장애 타임라인과 보고서

```svg
<svg viewBox="0 0 760 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="13" font-weight="bold">test_c_report 실측: 장애 창(가는 막대) 과 건강 상태(굵은 띠)</text>
  <text x="20" y="62" fill="currentColor">i2c0</text><text x="20" y="106" fill="currentColor">tmp0</text><text x="20" y="150" fill="currentColor">hum1</text><text x="20" y="194" fill="currentColor">nor0</text><text x="20" y="238" fill="currentColor">uart0</text>
  <path d="M160 72 H740 M160 116 H740 M160 160 H740 M160 204 H740 M160 248 H740" stroke="currentColor" stroke-width="0.5"/>
  <rect x="237" y="44" width="58" height="6" class="f-mut" stroke="currentColor"/><rect x="237" y="54" width="4" height="16" class="f-acc"/><rect x="241" y="54" width="58" height="16" class="f-warn"/><rect x="566" y="54" width="2" height="16" class="f-acc"/><rect x="570" y="54" width="7" height="16" class="f-acc"/>
  <text x="300" y="66" fill="currentColor">SDA 단락 400–700, OK 720</text>
  <rect x="566" y="88" width="12" height="6" class="f-mut" stroke="currentColor"/><rect x="566" y="98" width="8" height="16" class="f-acc"/><rect x="574" y="98" width="8" height="16" class="f-warn"/><text x="590" y="110" fill="currentColor">스트레칭 2100–2160</text>
  <rect x="334" y="132" width="135" height="6" class="f-mut" stroke="currentColor"/><rect x="335" y="142" width="29" height="16" class="f-acc"/><rect x="364" y="142" width="126" height="16" class="f-warn"/><text x="496" y="154" fill="currentColor">꺼짐 900–1600, OK 1705</text>
  <rect x="353" y="176" width="77" height="6" class="f-mut" stroke="currentColor"/><rect x="355" y="186" width="6" height="16" class="f-acc"/><rect x="361" y="186" width="70" height="16" class="f-warn"/><text x="438" y="198" fill="currentColor">hang 1000–1400, OK 1404</text>
  <rect x="392" y="220" width="135" height="6" class="f-mut" stroke="currentColor"/><rect x="421" y="230" width="31" height="16" class="f-acc"/><rect x="452" y="230" width="84" height="16" class="f-warn"/><text x="542" y="242" fill="currentColor">다운 1200–1900, OK 1945</text>
  <path d="M160 256 H740" stroke="currentColor"/>
  <text x="160" y="272" fill="currentColor" text-anchor="middle">0</text><text x="257" y="272" fill="currentColor" text-anchor="middle">500</text><text x="353" y="272" fill="currentColor" text-anchor="middle">1000</text><text x="450" y="272" fill="currentColor" text-anchor="middle">1500</text><text x="547" y="272" fill="currentColor" text-anchor="middle">2000</text><text x="643" y="272" fill="currentColor" text-anchor="middle">2500</text><text x="740" y="272" fill="currentColor" text-anchor="end">3000</text>
  <rect x="160" y="282" width="14" height="10" class="f-acc"/><text x="180" y="291" fill="currentColor">DEGRADED</text><rect x="260" y="282" width="14" height="10" class="f-warn"/><text x="280" y="291" fill="currentColor">FAULT</text><rect x="350" y="284" width="14" height="6" class="f-mut" stroke="currentColor"/><text x="370" y="291" fill="currentColor">주입한 장애 창 · 가로축 tick (= ms)</text>
</svg>
```

타임라인의 전이 시각은 이 시나리오의 이벤트 로그에서 그대로 옮겼다 (i2c0 400 DEGRADED → 420 FAULT → 720 OK / hum1 905 → 1055 → 1705 / nor0 1010 → 1042 → 1404 / uart0 1350 → 1510 → 1945 / tmp0 2100 → 2140 → 2180). 눈여겨볼 것: 장애가 겹쳐도 각 띠는 **자기 장애 창 근처에만** 있다 — 고장이 번지지 않았다. hum1 의 FAULT 가 창보다 105 tick 길게 남은 건 재초기화 백오프 탓 (1600 에 돌아왔지만 다음 시도가 1655, 두 번째 성공이 1705).

보고서 (실제 출력):

```text
== sensor hub report t=3000 overall=OK ==
comp  state     ok_ev err_ev trans rec_max fault_t  errors
i2c0  OK          177      9     7     320     300  ETIMEDOUT=3 EBUSY=6
tmp0  OK          131      3     3      80      40  ETIMEDOUT=3
hum1  OK           38      4     3     800     650  ENODEV=4
nor0  OK          166      8     3     394     362  ETIMEDOUT=8 ENOSPC=4
uart0 OK          142      6     3     595     435  ETIMEDOUT=2 ENOSPC=26 ELINK=4
bus: recover 4/9 last_pulses=3
tmp0: slots=150 samples=131 misses=19 init=1/1 stale_events=2
hum1: slots=60 samples=38 misses=22 init=2/5 stale_events=2
log: produced=169 logged=165 dropped=4 burned=0 overwritten=0 timeouts=8 splits=10 pending=0
tlm: acked=141 failed=2 dropped=26 retx=6 pings=5 pending=0
```

bring-up 엔지니어가 이 표에서 읽는 것: `bus: recover 4/9` — 복구 시도 9번 중 5번 실패 = 클럭으로 안 풀리는 고장(단락)이 있었다 → 스코프로 SDA 를 본다. `tmp0 init=1/1` — 버스가 죽어도 tmp0 를 재초기화하지 않았다 (탓을 제대로 돌렸다). `nor0 ENOSPC=4` vs `uart0 ENOSPC=26` — 로그는 4개, 텔레메트리는 26개를 버렸다; 둘 다 숫자로 남았다. `fault_t` 열 = 그 컴포넌트가 FAULT 로 지낸 tick 수.

### Part C — 무작위 soak (demo_soak.c 실측)

`demo_soak.c` 는 `solution.c` 를 include 해 시드 4개 × 50000 tick (+ 조용한 3000 tick + 비우기) 을 돈다. 표 1 = 모든 장애 종류, 장애 시작 간격 평균별 (실제 출력):

| 평균 간격 | 장애 수 | miss % | 기록 % | 로그 버림 % | 태움 | ACK % | 실패 % | 텔레 버림 % | 재전송/메시지 | FAULT % i2c/tmp/hum/nor/uart | 최대 회복 tick | 불변식/대조/미회복 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 없음 | 0 | 0.0 | 100.0 | 0.0 | 0 | 100.0 | 0.00 | 0.0 | 0.000 | 0/0/0/0/0 | 0/0/0/0/0 | 0/0/0 |
| 2000 | 95 | 2.9 | 98.9 | 1.1 | 85 | 95.5 | 0.16 | 4.4 | 0.013 | 0.7/2.1/2.2/3.0/4.2 | 320/1440/1200/762/1255 | 0/0/0 |
| 800 | 250 | 8.9 | 96.7 | 3.3 | 197 | 90.9 | 0.44 | 8.6 | 0.042 | 2.8/6.1/5.1/8.1/7.7 | 640/1600/2000/880/1915 | 0/0/0 |
| 300 | 689 | 23.5 | 92.8 | 7.2 | 418 | 77.0 | 1.22 | 21.8 | 0.112 | 8.0/14.8/18.6/18.7/20.3 | 800/2720/3500/834/2465 | 0/0/0 |
| 150 | 1024 | 32.1 | 90.4 | 9.6 | 420 | 74.5 | 1.23 | 24.3 | 0.116 | 11.8/22.2/25.6/22.8/30.7 | 840/2420/3250/1577/4335 | 0/0/0 |

표 2 = 같은 무작위 일정에서 한 종류만 남긴 것 (평균 간격 300, 몇 열만 옮김):

| 장애 | miss % | 로그 버림 % | 태움 | ACK % | 실패 % | 텔레 버림 % | 최대 회복 tick (해당 컴포넌트) |
|---|---|---|---|---|---|---|---|
| sda-hold | 0.5 | 0.0 | 0 | 100.0 | 0.00 | 0.0 | i2c 20 |
| sda-short | 8.3 | 0.0 | 0 | 100.0 | 0.00 | 0.0 | i2c 800 |
| sensor-off | 13.2 | 0.0 | 0 | 100.0 | 0.00 | 0.0 | tmp 2720 / hum 3200 |
| stretch | 3.3 | 0.0 | 0 | 100.0 | 0.00 | 0.0 | tmp 480 / hum 600 |
| nor-busy | 0.0 | 7.7 | 0 | 100.0 | 0.00 | 0.0 | nor 908 |
| nor-badprog | 0.0 | 0.7 | 469 | 100.0 | 0.00 | 0.0 | nor 512 |
| link-corrupt | 0.0 | 0.0 | 0 | 99.9 | 0.10 | 0.0 | uart 76 |
| link-loss | 0.0 | 0.0 | 0 | 99.6 | 0.18 | 0.2 | uart 155 |
| link-down | 0.0 | 0.0 | 0 | 77.9 | 0.74 | 21.4 | uart 2465 |

해석: (1) 56번의 긴 실행 전부에서 불변식 위반 0, 샘플 대조 불일치 0, 조용한 3000 tick 뒤 미회복 0. (2) 장애가 **격리**된다 — 표 2 에서 I2C 장애는 miss 만, NOR 장애는 로그 버림만, 링크 장애는 텔레메트리만 건드린다. (3) 텔레메트리 버림을 지배하는 건 링크 다운(21 %)이다: stop-and-wait + 큐 16 은 수백 tick 의 정전을 못 버틴다 → 설계 리뷰 질문 "다운 중엔 로그에서 다시 보내기(store-and-forward)" 로 이어진다. (4) 회복 시간 최대값에는 장애 길이 자체가 포함된다 (센서 꺼짐 최대 1500 tick + 백오프).

### Part C — 설계 리뷰 토론

- **워치독**: 메인 루프 끝 한 곳에서, 태스크마다 "이번 마감 안에 돌아왔나"를 보고 kick. 체크인 기준은 **살아 있음**(돌아왔음)이지 **진전**(일을 끝냄)이 아니다 — NOR 가 hang 이면 로거는 진전이 없지만 장애는 처리 중이다. 진전 기준으로 kick 하면 주변장치 하나의 고장이 MCU 리셋이 되어 센서·텔레메트리까지 끊긴다 (연습 1 실측: 진전 기준은 t=1249 에 리셋, 살아 있음 기준은 리셋 없음). 진짜 hang(무한 루프)은 둘 다 잡는다. 진전 감시는 건강 상태(FAULT 지속 시간)로 따로 한다. 워치독 하드웨어는 BM-M10, 태스크 체크인 설계는 TEST-B09 (다음 단계).
- **우선순위**: RTOS 로 옮기면 센서 태스크(주기 엄수, 짧음) > 텔레메트리 > 로거(긴 지우기·백오프). 사이는 지금처럼 고정 크기 큐 (RTOS-B 큐 레슨). 버스 복구는 센서 태스크 안에서만 — 버스 소유자가 하나여야 한다 (COMM-M12 Q7).
- **필드 로그에 남길 것**: 지금 보고서의 카운터 전부 (작고, 단조 증가, 리셋 사이 보존), "첫 실패" 스냅샷 (시각, 컴포넌트, 코드, HAL 원래 코드), 이벤트 링 최근 N개를 `.noinit` RAM 에 (BM-A09 크래시 덤프, 다음 단계). 로그 자체가 장애를 키우지 않게 이벤트 수에 상한·속도 제한.
- **아직 없는 것**: 링크 복구 뒤 로그에서 밀린 샘플 다시 보내기, 센서 전원 GPIO 로 재인가(재초기화가 계속 실패할 때 한 단계 위 행동), 버스 복구도 실패하면 I2C 컨트롤러 리셋.

### 테스트가 정말 버그를 잡나 — 변이 12개 (scratch 에서 실행, 레슨 폴더엔 없음)

| 변이 | 결과 | 처음 잡은 단언 |
|---|---|---|
| M1 페이지 분할 없음 | 109/158 | 로거 테스트 (`logged == N …`), 불변식: 감김 |
| M2 실패한 DATA 뒤 seq 그대로 | 155/158 | `final_check` — ACK 받았는데 지상국에 없음 |
| M3 EBUSY 를 센서 탓으로 | 156/158 | `trans == 0` (센서 상태 전이) |
| M4 IDLE 에서 WIP 확인 안 함 | 151/158 | `ign_busy == 0` |
| M5 되읽기 검증 없음 | 153/158 | `burned >= 1` + NOR 스캔 불일치 |
| M6 재초기화 백오프 없음 | 155/158 | `gaps_ok` (100, 200, 400 …) |
| M7 복구 뒤 STOP 없음 | 157/158 | 복구 단위 테스트 (`stops == 1`) |
| M8 에러가 상태를 좋게 만듦 | 156/158 | 건강 단위 테스트 |
| M9 타임아웃 뒤 복구를 미룸 | 157/158 | `EBUSY == 0 && tmp0 misses == 0` |
| M10 꽉 차면 조용히 새것 버림 | 138/158 | 장부 불변식 |
| M11 실패한 PING 이 seq 소모 | 156/158 | 20000 tick 장애: 불변식 "ground set bounds" |
| M12 타임아웃 난 지우기를 믿음 | 154/158 | 지우기 hang 시나리오 (`erases >= 2`, `burned == 0`) |

처음엔 M8, M11, M12 가 살아남았다 (144/144 통과). 그래서 건강 단위 테스트, 20000 tick 장애, "옛 데이터가 찬 칩 + 지우기 중 hang" 시나리오(그리고 지우기를 완료 시점에 반영하는 모델)를 추가했다. 변이가 살아남는 곳 = 테스트가 안 보는 곳이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 158/158 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 깨끗함
cc -std=c11 -Wall -Wextra -O2 demo_soak.c -o demo_soak && ./demo_soak     # 위 표 1·2
python3 -I verify_frame.py   # 텔레메트리 프레임 바이트를 독립 구현(stdlib)으로 재계산 — 위 예제와 같음
```

`verify_frame.py` 는 다른 방식(블록 단위 COBS)으로 같은 DATA 프레임을 만든다: `08 01 07 04 03 02 01 01 03 D2 04 01 05 FE FF 97 9B 00`, CRC `979B` — C 출력과 바이트 단위로 같다.

## 7. 자주 하는 실수

**1. 협력형 루프 안의 블로킹 대기.** `nor_wait_ready()` 가 `while (rdsr() & WIP)` 이면 NOR hang 한 번에 센서·텔레메트리가 같이 죽고, 결국 워치독 리셋. 고친 코드: `st = LG_WAIT; wait_start = now; return;` — 기다림은 상태다 (힌트 1).

**2. 타임아웃 뒤 "아마 됐겠지" 또는 "아마 안 됐겠지".**

```c
if (timeout) { slot++; have_cur = false; }   /* 됐다고 가정 → 반쪽 레코드가 LOGGED */
if (timeout) { /* 다시 같은 슬롯에 PP */ }   /* 안 됐다고 가정 → AND 로 덮어써 깨짐, 바쁜 칩엔 명령 무시 */
```

고친 코드: 바쁜 칩에는 RDSR 말고 아무것도 보내지 않고, 끝나면 **되읽어서** 판단. 지우기는 다시 지운다.

**3. 꺼낸 데이터를 큐 안에 둔 채 처리.** drop-oldest 가 처리 중인 항목을 버리면, 같은 샘플이 "버림"과 "기록"으로 두 번 세어진다. 고친 코드: `q_pop` 으로 꺼내 `cur` 에 소유권을 옮긴다 (CAM-M11 의 버퍼 소유권과 같은 생각).

**4. 버스 고장을 센서 탓으로.** EBUSY 에 센서 재초기화를 걸면 SDA 단락 300 tick 동안 두 센서가 백오프에 들어가 버스가 살아난 뒤에도 수백 tick 을 더 놓친다. 고친 코드: 에러 코드별 책임표 (힌트 3) — NACK 은 버스가 대답한 것, EBUSY 는 센서 무죄.

**5. 실패를 "안 감"으로 기록하고 재사용.** 실패한 DATA 의 seq 를 다음 샘플에 다시 쓰면 지상국이 중복으로 버린다 (변이 M2). 실패한 PING 마다 seq 를 쓰면 긴 장애 뒤 간격이 128 을 넘어 같은 일이 대량으로 (변이 M11, 73개 손실). 고친 코드: DATA 실패 → seq++, PING 실패 → seq 그대로.

**6. 모든 계층의 재시도와 백오프 없는 재초기화.** 센서가 빠진 동안 매 주기 init 을 두드리면 버스 시간을 낭비하고 같은 버스의 이웃 지연이 늘어난다; 손상된 플래시를 백오프 없이 태우면 섹터를 금방 다 쓴다 (백오프 넣기 전 soak 실측: 한 시드에서 태운 슬롯 188 → 넣은 뒤 33). 고친 코드: 실패마다 지수 백오프 + 상한, 성공하면 리셋.

**7. 카운터 없는 drop.** `if (full) return;` — 샘플이 조용히 사라진다 (변이 M10). 버리는 곳마다 카운터 + 운명 기록이 있어야 장부가 맞는다.

## 8. 면접 Q&A

<details>
<summary>Q1. I2C 버스가 hang 되면 이 허브 전체는 어떻게 행동하나요?</summary>

같은 버스의 두 센서는 운명을 공유한다 — 그건 피할 수 없다. 설계가 정하는 건 **번지지 않게** 하는 것이다. ① 감지: 다음 트랜잭션이 START 를 못 만들어 EBUSY. ② 탓: 버스 에러 (센서 건강은 그대로 — 재초기화 안 함). ③ 행동: 즉시 9클럭 + STOP (COMM-M03). 슬레이브가 바이트 중간에 멈춘 경우면 여기서 끝 (실측 5 펄스, miss 1). ④ 단락처럼 클럭으로 안 풀리면 FAULT, 복구는 백오프 10 → 160 tick 으로만 다시 시도하고, 그 사이 센서 슬롯은 miss, 데이터는 stale 표시. ⑤ 로거·텔레메트리는 다른 하드웨어라 영향 없음 — 표 2 의 sda-short 행: miss 8.3 %, 로그·텔레 손실 0. ⑥ 회복 상한: 장애가 끝나고 ≤ BUS_BACKOFF_MAX + 2·주기 (실측 +20 tick). 그래도 안 되면 다음 단계는 I2C 컨트롤러 리셋 → 센서 전원 재인가 → 그래도 안 되면 보고만 하고 계속 동작 (MCU 리셋은 버스를 못 고친다).

</details>

<details>
<summary>Q2. 재시도, 복구, 재초기화, 리셋 — 어떤 순서로 올라가나요?</summary>

비용과 영향 범위 순서의 사다리다. 재시도(같은 동작 다시, 링크 ARQ) → 복구(그 버스만, 9클럭) → 재초기화(그 장치만, 설정 다시 쓰기) → 전원 재인가(그 장치) → 주변장치 리셋(컨트롤러) → MCU 리셋(워치독, 전부). 원칙: (1) 한 단계는 **한 계층**에서만 (재시도가 곱해지지 않게, COMM-M12), (2) 위로 올라가는 조건은 "연속 실패 N" 이나 "FAULT 가 T 이상 지속" 같은 숫자로, (3) 각 단계에 백오프, (4) 위 단계일수록 드물게 + 로그 필수. 이 레슨은 재초기화까지 구현했고, 리셋 쪽은 워치독을 "살아 있음"에만 묶어 주변장치 고장이 MCU 리셋으로 번지지 않게 했다 (연습 1).

</details>

<details>
<summary>Q3. 지상국에 "정확히 한 번" 전달을 보장할 수 있나요?</summary>

전달 쪽 **중복 없음**은 보장한다 — 지상국이 seq(반 창 규칙, COMM-M14)로 중복을 버리고, 샘플 id 로 한 번 더 확인한다 (테스트에서 `dup_delivered == 0`). 하지만 송신자가 "도착했다"를 항상 아는 건 불가능하다: ACK 가 사라지면 데이터가 갔는지 모른다 (두 장군 문제). 그래서 운명은 ACKED / FAILED / DROPPED 셋이고, FAILED 는 "모름"이다 — ACK 방향만 끊은 실측에서 `failed=2` 인데 지상국은 2개를 더 받았다. 정직한 장부는 `acked ≤ 받은 수 ≤ acked + failed (+ 진행 중 1)`. 진짜 정확히 한 번이 필요하면: 영구 id(이 레슨의 sample id) + 수신 쪽 멱등 처리 + 링크 회복 뒤 "어디까지 받았나"를 물어 FAILED 를 재분류하거나 로그에서 다시 보내기 (store-and-forward). 즉 at-least-once 전송 + 수신 중복 제거 = 효과상 exactly-once.

</details>

<details>
<summary>Q4. 큐가 차면 왜 drop-oldest 인가요? 반대가 맞는 경우는?</summary>

텔레메트리: 운용자에게는 최신 값이 가장 가치 있다 → drop-oldest (과거는 NOR 에 있다). 로그: 사고 분석에서 "고장 직전" 데이터가 중요하므로 오히려 오래된 것을 지키고 새 것을 버리는(drop-newest) 블랙박스 정책도 있다 — 이 레슨은 로그도 drop-oldest 로 두고 숫자로 남겼다. 다른 선택지: 센서별 최신값 하나만 유지(coalescing), 우선순위 큐(알람 > 주기 데이터), 버림 대신 다운샘플(2개 중 1개). 어떤 정책이든 필수인 것: 버린 수가 카운터로 남고(ENOSPC), 버린 항목이 **처리 중인 항목이 아니어야** 한다 (실수 3). 큐 크기는 "최악 정전 시간 × 생산률"로 정하고, 그걸 못 담으면 정책으로 고른다: 이 설정은 300 tick × 0.07 = 21 > 16 이라 링크 다운마다 버림이 생긴다 (표 2 link-down 21 %).

</details>

<details>
<summary>Q5. NOR 가 타임아웃 났을 때 무엇을 하고, 무엇을 하면 안 되나요?</summary>

하면 안 되는 것: 바쁜 칩에 명령 보내기 (무시되거나, 칩에 따라 진행 중 동작을 망친다 — 그래서 `ign_busy == 0` 이 불변식), 결과를 추측하기. 하는 것: RDSR 만 백오프로 폴링, 건강 FAULT, 데이터는 큐에 쌓고 넘치면 drop-oldest, 칩이 돌아오면 **되읽기로 사실 확인**. 분할 레코드 첫 조각 뒤에 hang 이 와도 (실측) 두 번째 조각을 마저 쓰고 검증 통과 — 태운 슬롯 0. 지우기 중 hang 은 섹터가 반만 지워졌을 수 있으니 다시 지운다 (안 그러면 실측 `burned=35` 로 멈춤). 실제 칩이라면 hang 이 계속될 때 다음 단계는 소프트웨어 리셋 명령(Winbond 계열 0x66/0x99 — 데이터시트 확인 권장), 그다음 칩 전원.

</details>

<details>
<summary>Q6. 건강 상태를 연속 실패 횟수로 정하면 무엇을 놓치나요?</summary>

간헐 에러. 에러·성공이 번갈아 오면 연속 3번이 절대 안 나와서 50 % 에러율인 링크가 영원히 DEGRADED 와 OK 사이를 오간다 (연습 2 실측: 연속 규칙 FAULT 0번). 비율 기반 — 누수 버킷(에러 +4, 성공 −1, 12 이상 FAULT) — 은 7번째 사건에서 FAULT. 또 25 % 무작위 에러에서 상태 변화 수가 연속 규칙 1393번 vs 버킷 70번: 히스테리시스(FAULT 해제는 4 이하) 없이 상태를 알람에 묶으면 알람 폭주가 된다. 이 레슨이 연속 규칙을 쓴 건 단순하고 회복 시간 상한을 증명하기 쉬워서다. 실무에선 둘 다: 연속 규칙(빠른 반응) + 비율 창(느린 열화 감지).

</details>

<details>
<summary>Q7. 하드웨어 없이 이걸 검증했다는 걸 어떻게 믿나요?</summary>

네 겹. ① 결정적 시뮬레이터 + 고정 시드 — 같은 입력이면 같은 tick 에 같은 일이 일어나 실패를 재현할 수 있다. ② 시나리오마다 숫자 단언 (펄스 수, 재초기화 간격 100/200/400, 실패 정확히 2개, 회복 상한). ③ 매 tick 불변식 10개 + 끝난 뒤 샘플별 대조 — "버그를 예상한 곳"이 아니라 "깨지면 안 되는 성질"을 본다. 무작위 soak 56번 실행에서 위반 0. ④ 변이 테스트: 그럴듯한 버그 12개를 넣어 전부 잡히는지 확인했고, 처음에 살아남은 3개(M8, M11, M12)가 테스트의 빈 곳을 알려줘 테스트를 더했다. 한계도 말한다: 모델이 틀리면(예: 실제 칩은 지우기 중 hang 이 다르게 남는다) 검증도 그만큼만 맞다 → 실제 보드 녹화 재생(COMM-A09), 장시간 실보드 soak(TEST-A06, 다음 단계)으로 모델을 교정한다.

</details>

<details>
<summary>Q8. RTOS 로 옮기면 무엇이 바뀌나요?</summary>

태스크 셋 + 큐 둘. 센서 태스크(높은 우선순위, `vTaskDelayUntil` 로 주기 고정, 버스 소유자), 텔레메트리 태스크(중간, UART RX 는 ISR → 스트림 버퍼, COMM-M11 DMA+IDLE), 로거 태스크(낮음, NOR 대기를 `vTaskDelay` 로 — 이제 기다려도 다른 태스크가 돈다). 큐는 지금처럼 고정 크기, 단 drop-oldest 는 "꽉 차면 하나 꺼내고 넣기"를 원자적으로 (큐 API 가 덮어쓰기를 지원하지 않으면 뮤텍스 또는 단일 생산자 규칙). 건강 상태는 여러 태스크가 쓰므로 컴포넌트마다 소유 태스크 하나만 쓰게 하거나 크리티컬 섹션. 워치독은 각 태스크가 이벤트 그룹 비트를 세우고 감시 태스크가 모두 모였을 때만 kick (연습 1 의 살아 있음 기준). 협력형 버전에서 얻은 것 — 블로킹 없는 상태 기계, 백오프, 불변식 — 은 그대로 유효하고, 시뮬레이터 테스트도 그대로 쓸 수 있다 (태스크 함수가 `now` 를 받는 순수 상태 기계라서).

</details>

<details>
<summary>Q9. 필드에서 이 허브가 이상하다는 리포트가 오면 무엇부터 보나요?</summary>

보고서의 순서대로. ① overall 과 컴포넌트별 `fault_t`: 어디가 얼마나 오래 아팠나. ② 에러 종류: `bus: recover 4/9` 처럼 복구 실패가 있으면 하드웨어(단락, 풀업, 커넥터), ENODEV 반복이면 센서 전원/배선, ETIMEDOUT 이 특정 센서에만 있으면 그 센서의 변환 시간·온도. ③ 샘플 장부: `misses`, `log dropped`, `tlm dropped/failed` 로 사용자 영향 크기. ④ 이벤트 로그 시각으로 상관관계: 여러 컴포넌트가 같은 시각에 아프면 공통 원인(전원 강하, ESD) — 이 레슨의 장애들은 독립이라 띠가 따로 있다. ⑤ 그다음에야 스코프/LA. 카운터가 없으면 이 순서가 전부 "재현해 보자"로 바뀐다.

</details>

## 9. 추가 연습

### 연습 1 — 워치독: 살아 있음 vs 진전

협력형 루프 끝의 감시자가 태스크 셋의 체크인을 보고 하드웨어 워치독(200 tick)을 kick 한다. (a) 로거가 `while(WIP){}` 로 영원히 도는 hang, (b) NOR 가 600 tick 바쁘지만 로거가 올바르게 처리하는 경우를 "진전 기준" / "살아 있음 기준" 체크인으로 비교하라.

<details>
<summary>답 보기</summary>

```c
for (int k = 0; k < NTASK; k++) if (now - s->last_checkin[k] > s->deadline[k]) all = false;
if (all) s->last_kick = now;          /* 모두 마감 안에 체크인했을 때만 */
```

실측: hang at 1234 → 리셋 1433 (마지막 kick 1233 + 200) — 두 기준 모두 잡는다. NOR 바쁨 [1000, 1600): 진전 기준은 리셋 1249 (마지막 진전 999 + 마감 50 = 마지막 kick 1049, +200), 살아 있음 기준은 리셋 없음. 주변장치 고장을 MCU 리셋으로 키우지 않으려면 체크인은 "루프가 돌아왔다"로, 진전은 건강 상태로 따로 감시한다.

</details>

### 연습 2 — 누수 버킷 건강 판정

에러 +4, 성공 −1 (상한 32), 12 이상 FAULT, FAULT 해제는 4 이하(히스테리시스), 0 이면 OK. 본문의 연속 규칙과 번갈아 오는 에러 / 10 % / 25 % 무작위에서 비교하라.

<details>
<summary>답 보기</summary>

번갈아(50 %): 버킷 4, 3, 7, 6, 10, 9, 13 → 7번째 사건에서 FAULT, 연속 규칙은 FAULT 0번. 10 % (9번 성공마다 에러 1): 최대 수준 4, FAULT 0, 결국 OK. FAULT(12)에서 성공만 오면 8번 뒤 DEGRADED (12 → 4). 25 % 무작위 5000 사건: 상태 변화 버킷 70번 vs 연속 규칙 1393번. 정상 상태 드리프트 = p·W − (1 − p) > 0 ⇔ p > 1/(W+1) = 20 % — 이 경계가 "참을 수 있는 에러율"이라는 설계 파라미터다.

</details>

### 연습 3 — 부팅 스캔으로 쓰기 위치 복원

전원이 들어오면 NOR 4 섹터를 훑어 다음 쓰기 위치(섹터, 슬롯), 다음 id, 지우기 필요 여부를 복원하라. 찢어진 마지막 쓰기, 같은 샘플을 다음 슬롯에 다시 쓴 경우(본문 태움 경로), 꽉 찬 칩, 회전 중인 칩.

<details>
<summary>답 보기</summary>

가장 큰 id 레코드의 바로 다음 슬롯부터, **비어 있는(전부 0xFF)** 슬롯을 찾을 때까지 건너뛴다 (찢어진 쓰기·태운 슬롯은 0xFF 가 아니라서 다시 쓸 수 없다). 섹터 끝이면 다음 섹터로, 그 섹터가 비어 있지 않으면 `need_erase`. 다음 id = 최대 + 1. 테스트: 빈 칩 → (0, 0, id 0), 50개 → 슬롯 50, 찢어짐 + 재기록 → 슬롯 52 / id 51 / dirty 1, 4 섹터 꽉 참 → 섹터 0 + 지우기 필요, 무작위 위치 300개 → 전부 일치. 한계: id 가 32비트를 넘어 감기면 "최대"가 깨진다 — 섹터 헤더에 세대 번호를 두는 게 정석 (COMM-A06 의 로그 저장소, NAND FTL 의 블록 시퀀스 번호와 같은 생각).

</details>

### 연습 4 — 재초기화 백오프의 회복 지연 상한

본문 규칙(t0 실패 → a₁ = t0 + P, a_{k+1} = a_k + min(2^k·P, C·P))에서 센서가 `t_end` 에 돌아오면 첫 `a_k ≥ t_end` 에 회복한다. 지연의 최악값을 닫힌 식으로 구하고, 슬롯 단위 시뮬레이션과 장애 길이 1..5000 전수로 맞춰 보라.

<details>
<summary>답 보기</summary>

간격이 C·P 에 붙은 뒤 `t_end = a_k + 1` 이면 다음 시도까지 C·P − 1 을 기다린다 → 최악 지연 = **C·P − 1**. 실측: P = 20, C = 8 → 159 (L = 141), P = 50 → 399 (L = 351), 닫힌 식과 시뮬레이션 불일치 0. 본문 실측(305 실패, 2300 복귀 → 2655 회복, 시도 8번)과도 같다. 상한을 줄이려면 C 를 줄이거나(버스 부하 ↑), 센서의 "다시 살아났다" 신호(인터럽트 핀, 전원 good)를 이벤트로 받아 백오프를 건너뛴다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 18/18 passed, exit 0
```

## 10. 핵심 정리

- 샘플의 운명부터 나열하라: 로그 = LOGGED / LOG_DROPPED, 텔레메트리 = ACKED / FAILED(모름) / DROPPED. 이 목록이 불변식이고, 매 tick 검사한다.
- 협력형 루프에서 기다림은 상태 + 마감 시각. 바쁜 칩에는 RDSR 만, 결과는 추측하지 말고 되읽어서 확인 (지우기는 다시 지운다).
- 에러 분류 하나, 책임표 하나: NACK·0x8000 은 버스가 대답한 것(센서 탓), EBUSY 는 버스 탓(센서 무죄), ETIMEDOUT 은 둘 다 + 즉시 복구.
- 건강 = 에러로는 나빠지기만, 연속 성공 2번으로 회복. 상태는 요약이고 행동은 에러 코드가 정한다. 간헐 에러는 비율(누수 버킷)로 본다.
- 모든 재시도·재초기화·복구에 지수 백오프 + 상한, 회복 지연 상한 = C·P − 1 처럼 숫자로 말한다.
- 처리 중인 항목은 큐 밖으로 꺼내 소유하고, 버리는 곳마다 카운터.
- DATA 실패 뒤 seq++, PING 실패 뒤 seq 그대로 — seq 간격을 반 창(128) 안에 묶어 둔다.
- 테스트를 테스트하라: 변이가 살아남는 곳이 테스트의 빈 곳이다.

## 참고자료

- NXP, *UM10204 I2C-bus specification and user manual* — wired-AND, 클럭 스트레칭, bus clear(9 클럭, §3.1.16 — 절 번호 확인 권장). 이 레슨 I2C 모델의 근거 (COMM-B06/M01/M03)
- SBS-IF, *System Management Bus (SMBus) Specification* 3.x — t_TIMEOUT 25–35 ms (대표값, 스펙 확인 권장)
- Winbond, *W25Q80DV datasheet* — JEDEC ID EF 40 14, WREN/WEL, 페이지 프로그램 256 B 와 페이지 안 감김, 섹터 지우기 4 KB, t_PP / t_SE (값은 데이터시트 확인 권장 — 이 레슨의 tick 값은 모델 정의), 리셋 명령 66h/99h (확인 권장)
- S. Cheshire, M. Baker, "Consistent Overhead Byte Stuffing," *IEEE/ACM Trans. Networking*, 1999 — COBS (COMM-B12)
- RFC 6298, *Computing TCP's Retransmission Timer* — RTO 와 지수 백오프 (COMM-M14)
- Linux kernel `drivers/i2c/i2c-core-base.c` — `i2c_generic_scl_recovery()` 버스 복구 (기억, 소스 확인 권장)
- P. Koopman, *Better Embedded System Software* (2010) — 워치독, 에러 로깅, 장애 처리 장들 (기억, 확인 권장)
- 이 레슨 파일: `solution.c` / `starter.c` / `practice.c`, `demo_soak.c` (표 1·2), `verify_frame.py` (프레임 바이트 독립 검증)
- 관련 레슨: COMM-M01 (클럭 스트레칭), COMM-M03 (9클럭 버스 복구), COMM-M12 (계층별 에러 번역, bus_ops), COMM-M14 (stop-and-wait ARQ, seq 공간), COMM-B12 (COBS), COMM-B10 (프레임 파서와 재동기), COMM-M11 (DMA + IDLE UART 수신), COMM-A06 (SPI NOR 드라이버), COMM-A09 (트랜잭션 녹화·재생), CAM-M11 (버퍼 소유권), RTOS-B10 (뮤텍스), TEST-A05. 다음 단계: BM-M10 (워치독 하드웨어), TEST-B09 (워치독 체크인 설계), TEST-A06 (soak·chaos), BM-A09 (크래시 덤프 .noinit), TEST-M02 (퍼징)
