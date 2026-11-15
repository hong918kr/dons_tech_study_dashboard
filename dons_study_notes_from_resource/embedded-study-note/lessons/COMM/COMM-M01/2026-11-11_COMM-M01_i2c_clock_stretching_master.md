---
id: COMM-M01
time: 80분
updated: 2026-11-11
---
# COMM-M01 I2C 클럭 스트레칭 지원 마스터

## 학습 목표

- 클럭 스트레칭(clock stretching)이 오픈드레인 wired-AND 위에서 **물리적으로 무엇인지**(슬레이브가 SCL 을 LOW 로 계속 당김) 설명하고, 슬레이브가 어디서 늘이는지(쓰기: ACK 뒤, 읽기: 데이터 첫 비트 앞) 파형으로 읽는다.
- "SCL 을 놓은 뒤 **다시 읽어서** 진짜 high 가 될 때까지 기다리고, 그때부터 high 반주기를 센다"는 마스터 규칙을 구현하고, 기다린 시간 w = max(0, N − t_low) 를 테스트로 확인한다.
- 최대 스트레칭 타임아웃을 경계값(딱 한도 / 한 틱 초과 / 영원히)까지 테스트하고, 타임아웃 뒤 **두 선을 놓아 정의된 상태**로 만든 다음 버스가 어떤 모습으로 남는지(쓰기 중 vs 읽기 중, MSB 0/1) 분류한다.
- SCL 이 high 라고 *가정하는* naive 마스터가 만드는 고장 네 가지(짧아진 tHIGH, 삼켜진 클럭, 준비 전 샘플, 사라진 STOP)를 재현하고 로직 애널라이저 캡처에서 알아본다.
- NACK 주소 / NACK 데이터 / TIMEOUT 을 구분해 돌려주는 `i2c_write_read()` 와, 브링업 엔지니어가 읽을 타이밍 로그를 만든다.

## 1. 문제

"센서가 가끔 쓰레기 값을 준다, 라즈베리파이에서만", "디버거로 멈췄다 풀면 I2C 가 죽는다" — 이런 버그 보고의 상당수가 클럭 스트레칭이다. 면접에서는 "슬레이브가 SCL 잡고 안 놓으면 마스터는 어떻게 해야 하나?" 로 시작해 타임아웃 값, 타임아웃 뒤 처리(COMM-M03), SMBus 와의 차이까지 꼬리를 문다.

I2C 의 SCL 도 SDA 처럼 오픈드레인이다 (COMM-B06). 그래서 **클럭은 마스터가 만들지만, 누구든 LOW 로 붙잡을 수 있다.** 슬레이브가 "아직 준비 안 됐다"(받은 바이트를 처리 중, 다음 데이터를 아직 못 만듦, 측정 변환 중)일 때 마스터가 SCL 을 내린 직후 자기도 SCL 을 당겨 두면, 마스터가 SCL 을 놓아도 선은 LOW 로 남는다. 이것이 클럭 스트레칭이다. 규격상 마스터는 SCL 을 놓은 뒤 **실제로 HIGH 가 된 것을 보고 나서** high 구간을 세야 한다 (UM10204 의 clock synchronization/clock stretching 절 — 절 번호는 확인 권장). 이 레슨은 그 규칙을 지키는 마스터와 지키지 않는 마스터를 같은 버스 모델에서 비교한다.

| 누가 늘이나 (예) | 언제 | 비고 (대표 예, 데이터시트 확인 권장) |
|---|---|---|
| MCU 가 I2C **슬레이브**일 때 (STM32 등) | 주소 매치 뒤, 바이트 수신·송신 뒤 펌웨어가 레지스터를 읽고/쓸 때까지 | STM32 I2C 슬레이브는 기본으로 늘인다. `NOSTRETCH` 비트로 끌 수 있다 (RM 확인 권장) |
| 측정형 센서의 "hold master" 모드 | 측정 명령 뒤 읽기에서 변환이 끝날 때까지 (수 ms) | 예: SHT3x 의 clock-stretching 측정 명령, HTU21D 의 Hold Master 명령 — 끄는 모드(폴링)도 있다 |
| SMBus 디바이스 | 처리 지연 | SMBus 는 늘이는 시간에 상한이 있다 (25–35 ms tTIMEOUT 등, 대표값 — SMBus 규격 확인 권장) |
| 마스터 자신 | 인터럽트로 비트뱅이 늦어짐 | 마스터가 low 를 늘리는 건 원래 허용 (클럭 주인이니까) |

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">정답 마스터 + 주소 ACK 뒤 N = 7 스트레칭 (t_low = 3, t_high = 2 틱) — solution.c 실제 트레이스 34–72 틱</text>
  <rect x="344" y="44" width="112" height="36" class="f-warn" opacity=".35"/>
  <rect x="392" y="160" width="64" height="35" class="f-acc" opacity=".35"/>
  <g stroke="currentColor" stroke-dasharray="2 3" opacity=".45">
    <line x1="152" y1="40" x2="152" y2="250"/><line x1="232" y1="40" x2="232" y2="250"/><line x1="312" y1="40" x2="312" y2="250"/>
    <line x1="456" y1="40" x2="456" y2="250"/><line x1="536" y1="40" x2="536" y2="250"/><line x1="616" y1="40" x2="616" y2="250"/>
    <line x1="696" y1="40" x2="696" y2="250"/>
  </g>
  <g fill="none" stroke="currentColor" stroke-width="2">
    <polyline points="120,75 152,75 152,50 184,50 184,75 232,75 232,50 264,50 264,75 312,75 312,50 344,50 344,75 456,75 456,50 488,50 488,75 536,75 536,50 568,50 568,75 616,75 616,50 648,50 648,75 696,75 696,50 728,50 728,75 744,75"/>
    <polyline points="120,130 344,130 344,105 504,105 504,130 584,130 584,105 664,105 664,130 744,130"/>
  </g>
  <g fill="none" stroke="currentColor" stroke-width="1.5">
    <polyline points="120,190 152,190 152,165 184,165 184,190 232,190 232,165 264,165 264,190 312,190 312,165 344,165 344,190 392,190 392,165 488,165 488,190 536,190 536,165 568,165 568,190 616,190 616,165 648,165 648,190 696,190 696,165 728,165 728,190 744,190"/>
    <polyline points="120,220 344,220 344,245 456,245 456,220 744,220"/>
  </g>
  <g fill="currentColor">
    <text x="10" y="67">SCL (선)</text><text x="10" y="122">SDA (선)</text>
    <text x="10" y="176" font-size="12">마스터 SCL</text><text x="10" y="190" font-size="12">드라이버</text>
    <text x="10" y="231" font-size="12">슬레이브 SCL</text><text x="10" y="245" font-size="12">드라이버</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="160" y="38">A1=0</text><text x="240" y="38">R/W=0</text><text x="320" y="38">ACK</text>
    <text x="432" y="38">b7=1 (늘어남)</text><text x="544" y="38">b6=0</text><text x="624" y="38">b5=1</text><text x="704" y="38">b4=0</text>
    <text x="320" y="146">슬레이브 ACK</text>
    <text x="400" y="98">N = 7 틱 LOW</text>
    <text x="424" y="156">w = 4 틱 대기</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="272">점선 = SCL↑ = 슬레이브 샘플 순간. 마스터는 틱 51 에 SCL 을 놓았지만 선은 LOW (슬레이브가 틱 48–54 붙잡음).</text>
    <text x="10" y="290">마스터는 SCL 을 다시 읽어 LOW 면 1 틱씩 기다리고(w = N − t_low = 4), 틱 55 에 HIGH 를 본 순간부터 t_high = 2 틱을 센다.</text>
    <text x="10" y="308">그래서 늘어난 것은 LOW 구간(3 → 7 틱)뿐, HIGH 구간은 모든 펄스에서 2 틱 그대로 (테스트: min_high == 2, max_low == 7).</text>
    <text x="10" y="326">출처: solution.c 의 WAVE_SCL / WAVE_SDA (전체 103 틱과 strcmp 로 대조). 드라이버 두 줄은 트레이스와 코드 순서에서 유도.</text>
  </g>
</svg>
```

**모델 (COMM-B06 모델 축약판 + 시간).** SDA/SCL = 모든 노드 드라이버의 AND (1 = 놓음, 풀업이 1). **1 틱 = 1 µs** 로 정한다 (이 모델의 정의). 마스터는 SCL 을 low `t_low` 틱, high `t_high` 틱으로 만든다 — 기본 5/5 = 100 kHz, Standard-mode 의 tLOW ≥ 4.7 µs · tHIGH ≥ 4.0 µs (대표값, UM10204 표 확인 권장)를 만족한다. 매 틱: ① 슬레이브 스트레칭 카운트다운(놓는 것은 이번 틱에 바로 보임) → ② AND → ③ 슬레이브들이 같은 스냅샷을 보고 엣지 처리 → ④ AND → ⑤ 기록 + SCL high/low 펄스 폭 측정(로직 애널라이저처럼 `min_high`, `min_low`, `max_low`).

슬레이브가 늘이는 위치 (`stretch_where` 비트마스크, 이 레슨의 정의):

| 플래그 | 위치 | 그동안 SDA |
|---|---|---|
| `STR_ADDR_W` | 주소(+W) ACK 클럭이 끝나는 SCL↓ | ACK 를 놓음 (마스터가 다음 비트를 SDA 에 올릴 수 있음) |
| `STR_RX` | 받은 데이터 바이트의 ACK 클럭이 끝나는 SCL↓ | 같음. 마지막 바이트 뒤면 **STOP 이 기다려야 한다** |
| `STR_TX` | 읽기: 보낼 바이트의 첫 비트(MSB) 앞 = 주소(+R) ACK 뒤, 마스터 ACK 뒤 | 놓아 둠(1). 데이터는 **붙잡기 마지막 틱에** 준비돼 MSB 가 나간다 |

길이 N 은 `stretch_n` 하나이거나, 이벤트마다 다른 `stretch_seq[k]` 다. `STRETCH_FOREVER` = 놓지 않는 고장 슬레이브.

### Part A — 늘이는 슬레이브와 기다리는 마스터

- `slave_stretch_start(s, b, kind)` — SCL↓ 틱에 불린다. kind 가 마스크에 있으면 N 을 고르고, N > 0 이면 `hold = N`, 자기 SCL 드라이버 = 0.
- `slave_stretch_tick(s, b)` — 매 틱 맨 앞. `hold--`; 1 이 되면 미뤄 둔 MSB 를 SDA 에 (읽기), 0 이 되면 SCL 을 놓는다.
- `m_scl_high(b)` — **이 레슨의 핵심 10줄.** SCL 을 놓고, 다시 읽고, LOW 면 1 틱씩 기다리고, `max_stretch` 틱을 기다렸는데도 LOW 면 `I2C_ERR_TIMEOUT`. 기다린 틱 수와 통계를 남긴다.
- `m_write_bit` / `m_read_bit` / `m_stop` — 반주기 단위 타이밍 + `b->scl_up(b)` 로 SCL 올리기 (에러 전파). high 반주기는 **확인된 high 부터** 센다.
- 테스트: 스트레칭 없음(100 kHz 펄스 폭 5/5, 196 틱), 위 그림 파형 틱 단위 일치, N = 0..60 전수 × (쓰기 2바이트 / 읽기 3바이트).

### Part B — 타임아웃과 naive 마스터

- 경계: `max_stretch = 20`, `t_low = 5` 에서 N = 25 (정확히 20 기다림) → OK, N = 26 → TIMEOUT 이고 기다린 건 정확히 20. 영원히 붙잡음 → TIMEOUT, 10000 틱 뒤에도 SCL stuck-low.
- `m_abort(b)` — 타임아웃 뒤 마스터의 SDA·SCL 드라이버를 모두 놓는다. 이후 버스 상태를 분류: 쓰기 중이었으면 슬레이브가 놓는 순간 idle, **읽기 중이었으면 슬레이브가 놓으면서 다음 바이트 MSB 를 내므로 MSB = 0 이면 SDA stuck-low** (COMM-M03 의 출발점).
- SMBus 식 25 ms (25000 틱) 한도, 바이트마다 다른 N (0, 3, 6, 40, 1, 17).
- naive 마스터 (`scl_up_naive`: 놓기만 하고 high 라고 가정)로 네 가지 고장을 재현해 정확한 결과를 assert 한다.

### Part C — 드라이버 `i2c_write_read()` + 타이밍 로그

```c
enum { I2C_OK = 0, I2C_ERR_ARG = -1, I2C_ERR_BUS_BUSY = -2, I2C_ERR_NACK_ADDR = -3,
       I2C_ERR_NACK_DATA = -4, I2C_ERR_TIMEOUT = -5 };
int i2c_write_read(Bus *b, uint8_t addr7, const uint8_t *wr, size_t nw, uint8_t *rd, size_t nr, I2cLog *log);
/* [S addr+W wr..] [Sr|S addr+R rd.. (마지막만 NACK)] P */
```

- NACK 이면 STOP 을 내고 `NACK_ADDR`/`NACK_DATA`, 타임아웃이면 STOP 없이 `m_abort` 후 `TIMEOUT`, 시작 전 SDA·SCL 중 하나라도 LOW 면 클럭 0개로 `BUS_BUSY`.
- 로그 = 호출자가 준 `I2cEvent ev[cap]` (malloc 없음). 단계마다 {시작·끝 µs, 종류, 바이트, ACK, 그 단계에서 기다린 스트레칭}. 꽉 차면 `overflow = true`.
- 테스트: 레지스터 읽기(쓰기 1 + Sr + 읽기 2, 바이트마다 50 µs 스트레칭) 로그, NACK 주소·데이터, 읽기 단계 타임아웃(로그의 phase = RX), 타임아웃 직후 `BUS_BUSY`(클럭 0개), 인자 오류, 로그 넘침, 고정 시드 랜덤 300 트랜잭션(일부는 타임아웃 길이) 결과 = 예측.

## 2. Problem (English)

> **Clock-stretching-aware I2C master on a simulated open-drain bus.**
>
> The bus model is given: SDA and SCL are the wired-AND of all node drivers (`1` = released). One tick = 1 µs. The master drives SCL low for `t_low` ticks and high for `t_high` ticks per bit. A slave may hold SCL low for `N` ticks starting at the SCL falling edge that ends an ACK clock (after its address/data ACK on writes; before the first bit of each byte it transmits on reads, where its data only becomes ready at the end of the stretch).
>
> 1. Implement the slave side: `slave_stretch_start()` (start holding SCL for this event's N) and `slave_stretch_tick()` (count down; drive the deferred MSB one tick before releasing).
> 2. Implement `int m_scl_high(Bus *b)`: release SCL, **read it back**, and wait tick by tick until it is actually high; if it is still low after `max_stretch` ticks of waiting, return `I2C_ERR_TIMEOUT`. The high half-period must start counting only once SCL is observed high. Use it in `m_write_bit`, `m_read_bit` and `m_stop`.
> 3. Implement `m_abort()` (release both lines after a timeout) and `int i2c_write_read(Bus*, uint8_t addr7, const uint8_t *wr, size_t nw, uint8_t *rd, size_t nr, I2cLog *log)` returning distinct codes for address NACK, data NACK, timeout, bus busy and bad arguments, and logging every phase with its start/end time and stretch time into a caller-provided, bounded log.
>
> Example: `t_low = t_high = 5`, slave stretches `N = 25` after its address ACK → the master waits exactly 20 ticks and the transfer succeeds; with `max_stretch = 20` and `N = 26` it returns `I2C_ERR_TIMEOUT` after waiting exactly 20 ticks, with both master drivers released.
>
> Follow-ups: What goes wrong if the master assumes SCL went high? What timeout would you choose and why? After a timeout in a read, what state can the bus be left in?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 아홉 군데: A1 슬레이브 스트레칭 2개, A2 `m_scl_high`, A3 `m_write_bit`·`m_read_bit`·`m_stop`, B `m_abort`, C1 `log_add`, C2 `i2c_write_read`. 버스 틱·AND·펄스 폭 측정, COMM-B06 슬레이브 상태 머신(스트레칭 훅 포함), 바이트 함수, START/Sr, naive `scl_up_naive`, 하네스용 최소 트랜잭션 `h_write`/`h_read` 는 주어진다. 스텁 `m_scl_high` 는 **naive 와 똑같이** "놓고 high 라고 가정"하므로 멈추지 않는다 (모든 루프는 유한). 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:423  r == I2C_OK && s.rx_len == 1 && s.rx[0] == 0xA5
FAIL starter.c:424  b.min_high == 5 && b.min_low == 5 && b.max_low == 5
FAIL starter.c:425  b.ticks - t0 == 6 + 2 * 90 + 10
…
random: ok=0 timeout=0 nack_addr=0, stretch total=0 us, min SCL high=4294967295 us, mismatches=300
FAIL starter.c:649  bad == 0 && n_to > 0 && n_nak > 0
FAIL starter.c:650  b.min_high == 5
7/61 passed
```

(exit 1. 통과한 7개는 스텁 `i2c_write_read` 가 `I2C_ERR_ARG` 를 돌려줘서 맞는 인자 검사 4개 등 우연이다.) Part A·B 테스트는 하네스 `h_write`/`h_read` 를 쓰므로 A1–A3 만 채워도 앞쪽이 통과하기 시작한다 — 실제로 A1–A3 만 채운 파일은 42/61, 그때 남는 Part B 실패는 `m_abort` 를 확인하는 STOP 직전 타임아웃 테스트와 그 여파(SDA 0 이 남아 뒤 테스트까지 막힘)다.

## 4. 힌트

<details><summary>힌트 1 — 스트레칭 = 슬레이브의 SCL 드라이버를 0 으로 두는 것뿐</summary>

슬레이브 코드는 SDA 드라이버만이 아니라 `b->scl_drv[s->node]` 도 가진다. SCL↓ 틱에 0 으로 두고 N 틱 뒤 1 로 돌리면 끝. 마스터가 SCL 을 놓아도 AND 는 0 이다. 카운트다운을 `bus_tick` 맨 앞(AND 계산 전)에서 하므로, 놓는 순간이 같은 틱의 스냅샷에 보인다.

</details>

<details><summary>힌트 2 — 읽기에서는 "데이터가 늦게 준비된다"</summary>

`STR_TX` 로 붙잡을 때는 MSB 를 바로 내지 말고 `pending_load` 로 미뤘다가 `hold` 가 1 이 되는 틱(놓기 한 틱 전, 아직 SCL LOW)에 `slave_load()` 를 부른다. N == 1 이면 시작 틱이 곧 마지막 붙잡은 틱이다. 이렇게 해야 naive 마스터가 "준비 전 데이터"를 읽는 장면이 재현된다.

</details>

<details><summary>힌트 3 — m_scl_high 는 while 하나</summary>

`set_scl(b, 1)` 다음 `w = 0`; `while (!b->scl)` 안에서 먼저 `w >= max_stretch` 면 TIMEOUT, 아니면 `bus_tick(b); w++`. 루프를 빠져나오면 `last_wait = w` 와 통계. "w == max_stretch 에서 high 가 되면 통과" 가 되도록 비교 순서를 맞춘다. 타임아웃 경로에서도 `last_wait` 를 남기면 테스트가 "정확히 20 틱 기다렸다"를 확인할 수 있다.

</details>

<details><summary>힌트 4 — 비트 함수는 반주기 셈</summary>

SCL↓ 틱(이전 비트의 마지막 틱)도 low 1 틱이다. 그래서 `set_sda` 1 + `idle_ticks(t_low − 2)` 뒤 `scl_up` 하면 low 가 정확히 `t_low`. high 는 확인된 첫 틱 + `idle_ticks(t_high − 1)` 다음 `set_scl(0)`. 읽기는 high 를 확인한 직후 `b->sda` 를 읽는다. STOP 도 같은 모양: SDA 0 → low 채우기 → `scl_up` → tSU;STO → SDA 1.

</details>

<details><summary>힌트 5 — 드라이버의 두 출구</summary>

NACK 은 "버스는 멀쩡, 상대가 거부" → STOP 으로 정상 종료. TIMEOUT 은 "버스를 신뢰할 수 없음" → STOP 을 시도하지 말고(SCL 이 LOW 라 STOP 조건을 만들 수 없다) `m_abort` 로 두 선을 놓고 반환. `goto stop;` / `goto timeout;` 두 라벨이 깔끔하다. 단계마다 `t0 = b->ticks; s0 = b->stretch_ticks;` 를 찍고 끝나면 `log_ev(...)`.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 말할 것 |
|---|---|
| 1. 범위 | SCL 을 읽을 수 있나(하드웨어 I2C 는 자동, 비트뱅은 SCL 핀이 open-drain + 입력 가능해야), 최대 스트레칭은 얼마까지 허용(디바이스 데이터시트 / SMBus), 타임아웃 뒤 정책 |
| 2. 물리 | SCL 도 wired-AND — "놓았다 ≠ high 다". 그러니 놓은 뒤 읽어야 한다 |
| 3. 규칙 | high 반주기는 SCL 이 실제로 high 가 된 순간부터 센다 (UM10204 clock synchronization 의 요지) |
| 4. 코드 | 슬레이브 hold 카운터 → `m_scl_high` (기다림 + 타임아웃) → 비트/STOP 에 적용 → 드라이버 에러 구분 + 로그 |
| 5. 테스트 | N 전수, 타임아웃 경계(=, +1, 영원히), 바이트마다 다른 N, 읽기 vs 쓰기, naive 대조, 랜덤 + 불변식(min tHIGH) |
| 6. 실제 | STM32 하드웨어는 자동으로 기다림 + TIMEOUTR, Linux `i2c-algo-bit` 의 `sclhi()`, BCM2835 버그, SMBus |

### Part A — 슬레이브가 붙잡는 틱과 마스터가 기다리는 틱

SCL↓ 틱을 t₀ 라 하자 (마스터가 9번째 클럭을 내리는 틱 = 슬레이브가 붙잡기 시작하는 틱).

| 틱 | 마스터 | 슬레이브 | SCL 선 |
|---|---|---|---|
| t₀ | `set_scl(0)` (low 1번째) | SCL↓ 감지 → `hold = N`, SCL 당김, ACK 놓기 | 0 |
| t₀+1 | `set_sda(bit)` (low 2번째) | hold = N−1 | 0 |
| t₀+2 … t₀+t_low−1 | `idle_ticks(t_low − 2)` | hold 감소 | 0 |
| t₀+t_low | `set_scl(1)` = 놓음, 읽어 보니 0 | hold = N − t_low | 0 (N > t_low 이면) |
| … | `bus_tick(); w++` | (읽기면 hold = 1 인 틱에 MSB 냄) | 0 |
| t₀+N | w = N − t_low 째 틱 | hold = 0 → SCL 놓음 (스냅샷 전) | **1** → 마스터 루프 탈출, 슬레이브들은 같은 틱에 SCL↑ 샘플 |
| 이후 t_high 틱 | high 반주기 | — | 1 |

그래서 **w = max(0, N − t_low)**. N ≤ t_low 면 스트레칭은 마스터의 low 구간 안에 숨어 보이지 않는다 (N = 0..5 에서 `stretch_events == 0`). 전수 테스트 N = 0..60 × (쓰기: 주소 ACK + 데이터 ACK 2번 = 이벤트 3개, 읽기: 주소 ACK 뒤 + 마스터 ACK 2번 = 이벤트 3개) 에서 `stretch_ticks == 3·w`, 데이터 일치, **모든 high 펄스 = 5, low 펄스 최소 5·최대 max(5, N)** 이 전부 성립했다 (실제 실행 `bad == 0`).

```c
static int m_scl_high(Bus *b) {
    set_scl(b, 1);
    uint32_t w = 0;
    while (!b->scl) {
        if (w >= b->max_stretch) { b->last_wait = w; return I2C_ERR_TIMEOUT; }
        bus_tick(b);
        w++;
    }
    b->last_wait = w;
    ...통계...
    return I2C_OK;
}
```

비트 함수는 B06 의 3틱 비트에 반주기 채우기를 더한 것이다: 비트 = t_low + t_high 틱(스트레칭 없을 때), 바이트 = 9(t_low + t_high), START(idle 에서) = t_high + 1, Sr = t_low + 2·t_high, STOP = t_low + t_high. 100 kHz 에서 1바이트 쓰기 = 6 + 2 × 90 + 10 = **196 µs** (테스트). 일반식과 시뮬레이션의 일치는 연습 3 에서 1440 케이스로 확인한다.

**모델에서 배운 것 하나 (실제와도 같다).** 처음 버전은 슬레이브가 SCL 을 놓는 처리를 엣지 처리 *뒤에* 했다. 그러면 놓는 틱의 스냅샷에는 아직 LOW 라 다른 노드는 한 틱 늦게 high 를 보는데, 마스터는 AND 결과를 바로 읽어 high 반주기를 세기 시작한다. `t_high = 1` 일 때 **슬레이브가 한 번도 보지 못한 1틱짜리 high 펄스**가 생겨 데이터 NACK 이 났다 (연습 3 의 공식 검사가 1440 중 257 케이스 불일치로 잡아냈다). 카운트다운을 AND 계산 앞으로 옮겨 "놓으면 모두가 같은 틱에 본다"로 고쳤다. 실제 버스에서 이에 해당하는 것이 **rise time 과 입력 필터**다: 마스터는 자기 핀에서 high 를 본 순간부터 세지만, 느린 rise time(HWLAB-B03) 때문에 다른 디바이스의 VIH 문턱 통과는 더 늦다. 그래서 하드웨어 I2C 는 SCL 을 놓은 뒤 SCL 입력이 high 로 *동기화된* 다음 tHIGH 를 센다.

### Part A — 흐름: 놓고, 읽고, 기다리고, 포기한다

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m01arr" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="8" markerHeight="8" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">m_scl_high() 와 그 호출자 — 정상 경로(초록)와 타임아웃 경로(주황)</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="170" height="36" rx="6" class="f-mut"/>
    <rect x="20" y="110" width="170" height="36" rx="6" class="f-acc"/>
    <rect x="20" y="190" width="170" height="36" rx="6" class="f-acc"/>
    <rect x="250" y="190" width="160" height="36" rx="6" class="f-mut"/>
    <rect x="250" y="110" width="160" height="36" rx="6" class="f-ok"/>
    <rect x="470" y="110" width="250" height="36" rx="6" class="f-ok"/>
    <rect x="20" y="265" width="170" height="36" rx="6" class="f-warn"/>
    <rect x="250" y="265" width="200" height="36" rx="6" class="f-warn"/>
    <rect x="490" y="265" width="230" height="36" rx="6" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="105" y="63">set_scl(1) — 놓기, w = 0</text>
    <text x="105" y="133">SCL 다시 읽기: 1 ?</text>
    <text x="105" y="213">w ≥ max_stretch ?</text>
    <text x="330" y="213">bus_tick(); w++</text>
    <text x="330" y="133">last_wait = w, 통계</text>
    <text x="595" y="133">high 반주기 t_high 틱 → SCL↓</text>
    <text x="105" y="288">return I2C_ERR_TIMEOUT</text>
    <text x="350" y="288">m_abort: SDA·SCL 놓기, 로그</text>
    <text x="605" y="288">호출자: 버스 진단 → M03 / 리셋</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m01arr)">
    <line x1="105" y1="76" x2="105" y2="108"/>
    <line x1="190" y1="128" x2="248" y2="128"/>
    <line x1="410" y1="128" x2="468" y2="128"/>
    <line x1="105" y1="146" x2="105" y2="188"/>
    <line x1="190" y1="208" x2="248" y2="208"/>
    <path d="M330,190 V170 H150 V148"/>
    <line x1="105" y1="226" x2="105" y2="263"/>
    <line x1="190" y1="283" x2="248" y2="283"/>
    <line x1="450" y1="283" x2="488" y2="283"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="200" y="122">예</text><text x="112" y="170">아니오</text>
    <text x="200" y="202">아니오</text><text x="112" y="248">예</text>
    <text x="20" y="320">naive 마스터 = 맨 위 상자 하나뿐 ("놓았으니 high 겠지"). 정답은 SCL 을 "출력"이 아니라 "입력"으로도 다룬다.</text>
  </g>
</svg>
```

### Part B — 타임아웃: 경계와 그 뒤의 버스

`max_stretch = 20`, `t_low = 5` 의 실제 결과:

| 슬레이브 N | 필요한 기다림 | 결과 | 확인한 것 |
|---|---|---|---|
| 25 | 20 | OK | `stretch_max == 20`, 데이터 정상 |
| 26 | 21 | `TIMEOUT` | `last_wait == 20` (정확히 한도만큼 기다림), 마스터 드라이버 둘 다 1 |
| 65 | 60 | `TIMEOUT` | 직후 SCL = 0, `hold > 0`, 20 틱 더 지나도 busy, 그 뒤 슬레이브가 놓으면 idle |
| 65, 마지막 데이터 ACK 뒤 (`STR_RX`) | 60 | `TIMEOUT` (STOP 단계) | 마스터는 STOP 을 위해 SDA = 0 으로 둔 채 기다리던 중 → `m_abort` 가 SDA 도 놓아야 슬레이브가 놓은 뒤 idle. 스텁 `m_abort` 면 SDA 0 이 남아 실패 |
| FOREVER | ∞ | `TIMEOUT` | 10000 틱 뒤에도 SCL = 0 → 마스터 쪽 수단 없음, 슬레이브 리셋 후 회복 |
| 20000 (한도 25000) | 19995 | OK | SMBus 식 25 ms 한도 |
| 30000 (한도 25000) | 29995 | `TIMEOUT` | `last_wait == 25000` |

**타임아웃 뒤 버스는 어떻게 남나?** 마스터가 선을 놓아도 슬레이브는 자기 상태 머신대로 계속한다:

| 타임아웃 난 곳 | 슬레이브가 결국 놓으면 | 다음 트랜잭션 | 처방 |
|---|---|---|---|
| 쓰기 (주소/데이터 ACK 뒤, STOP 직전 포함) | SDA = 1 (ACK 는 이미 놓음, 마스터 SDA 는 `m_abort` 가 놓음) → 버스가 idle 처럼 보임 | 버스 free 시간 뒤 START → 슬레이브 상태 리셋 → 정상 | 그대로 재시도 가능 |
| 읽기, 다음 바이트 MSB = 0 (0x12) | 놓기 직전에 MSB 0 을 냄 → **SCL = 1, SDA = 0** | `BUS_BUSY` | COMM-M03 9클럭 + STOP |
| 읽기, MSB = 1 (0x92) | SDA = 1 → idle 처럼 보이지만 슬레이브는 TX 상태 (`s.st == S_TX`) | START 가 상태를 리셋해 정상 (이 모델에서는) | 운에 의존 — 안전하게는 M03 절차를 먼저 |
| SCL 을 영원히 붙잡음 | 안 놓음 | `BUS_BUSY` | 마스터가 할 수 있는 게 없다: 슬레이브 RESET 핀/전원, 또는 SMBus 슬레이브의 자체 타임아웃(35 ms 넘게 low 면 인터페이스 리셋, 대표값) |

하나 더: 슬레이브가 SCL 을 놓은 **바로 그 틱**에 START 를 내면 슬레이브는 그것을 START 가 아니라 "SCL↑ 뒤 SDA 변화"로 본다 (첫 구현에서 실제로 났다). 그래서 하네스의 `bus_run_until_idle()` 은 idle 이 된 뒤 `t_low` 틱의 버스 free 시간을 더 기다린다 — 실제 규격의 tBUF(Standard-mode 4.7 µs, 대표값)와 같은 이유다.

### Part B — naive 마스터: 놓고, high 라고 가정한다

```svg
<svg viewBox="0 0 750 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">naive 마스터 + 주소 ACK 뒤 N = 25 (t_low = t_high = 5) — 0xA5 쓰기, 틱 86–196 (실제 트레이스)</text>
  <rect x="166" y="98" width="140" height="34" class="f-warn" opacity=".35"/>
  <g fill="none" stroke="currentColor" stroke-width="2">
    <polyline points="110,72 138,72 138,50 166,50 166,72 194,72 194,50 222,50 222,72 250,72 250,50 278,50 278,72 306,72 306,50 334,50 334,72 362,72 362,50 390,50 390,72 418,72 418,50 446,50 446,72 474,72 474,50 502,50 502,72 530,72 530,50 558,50 558,72 586,72 586,50 614,50 614,72 642,72 642,50 670,50 670,72 698,72 698,50 731.6,50"/>
    <polyline points="110,127 138,127 138,105 166,105 166,127 306,127 306,105 334,105 334,127 362,127 362,105 390,105 390,127 418,127 418,105 446,105 446,127 474,127 474,105 502,105 502,127 530,127 530,105 558,105 558,127 586,127 586,105 614,105 614,127 642,127 642,105 670,105 670,127 698,127 698,105 731.6,105"/>
    <polyline points="110,182 166,182 166,160 227.6,160 227.6,182 283.6,182 283.6,160 339.6,160 339.6,182 451.6,182 451.6,160 507.6,160 507.6,182 563.6,182 563.6,160 675.6,160 675.6,182 726,182 726,160 731.6,160"/>
  </g>
  <g fill="currentColor">
    <text x="10" y="58" font-size="12">마스터가</text><text x="10" y="72" font-size="12">낸 SCL</text>
    <text x="10" y="120">SCL (선)</text><text x="10" y="175">SDA (선)</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="152" y="42">ACK</text><text x="208" y="42">b7</text><text x="264" y="42">b6</text><text x="320" y="42">b5</text>
    <text x="376" y="42">b4</text><text x="432" y="42">b3</text><text x="488" y="42">b2</text><text x="544" y="42">b1</text>
    <text x="600" y="42">b0</text><text x="656" y="42">9번째</text><text x="712" y="42">P?</text>
    <text x="208" y="122" font-weight="bold">✗</text><text x="264" y="122" font-weight="bold">✗</text>
    <text x="236" y="146">삼켜진 클럭 2개</text>
    <text x="320" y="204">1</text><text x="376" y="204">0</text><text x="432" y="204">0</text><text x="488" y="204">1</text>
    <text x="544" y="204">0</text><text x="600" y="204">1</text><text x="656" y="204">1</text><text x="712" y="204">0</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="204">슬레이브가 센 비트</text>
    <text x="10" y="232">마스터는 b7·b6 의 SCL 을 "놓았지만" 슬레이브가 틱 96–120 을 붙잡아 선은 LOW — 그 두 비트는 슬레이브에게 존재하지 않는다.</text>
    <text x="10" y="250">슬레이브는 b5 부터 센다: 1 0 0 1 0 1 + 9번째(1) + STOP 준비의 SDA 0 = 8비트.</text>
    <text x="10" y="268">8번째 비트 직후 STOP 이 와서 바이트를 버린다. 마스터는 9번째 클럭에 "데이터 NACK" 을 본다.</text>
    <text x="10" y="286">원인은 슬레이브가 아니라 마스터. LA 로 보면 S 와 P 사이 완전한 펄스 = 16 (≠ 18).</text>
    <text x="10" y="304">출처: naive h_write 트레이스(N = 25) + 같은 트랜잭션의 N = 0 트레이스(= 마스터가 낸 SCL).</text>
    <text x="10" y="322">테스트: rx_len == 0, !all_ack (solution.c), pulses == 16 (practice.c 연습 4).</text>
  </g>
</svg>
```

네 가지 고장 (solution.c 테스트가 정확한 값을 assert):

| 시나리오 (t_low = t_high = 5) | naive 결과 | 정답 마스터 |
|---|---|---|
| 주소 ACK 뒤 N = 7 | 데이터는 맞지만 그 high 펄스 = **3 µs** (`min_high == 3`, Standard-mode tHIGH ≥ 4.0 µs 위반) | w = 2, 모든 high = 5 µs |
| 주소 ACK 뒤 N = 25, 0xA5 쓰기 | 클럭 2개 삼켜짐 → 슬레이브 수신 0바이트, 마스터는 데이터 NACK | 정상 |
| 읽기 {5A C3}, 바이트 앞 N = 25 | **`D6 7F` 를 읽고 OK 반환** — 조용한 데이터 오염 | `5A C3` |
| 마지막 데이터 ACK 뒤 N = 15 | STOP 의 SDA↑ 이 SCL LOW 중에 일어나 STOP 이 아님 → 슬레이브 `stops == 0`, `S_RX` 에 남음 | STOP 이 스트레칭을 기다림 |

읽기 `D6` 의 유도: 슬레이브가 붙잡은 동안 마스터는 두 번 "high" 라고 믿고 SDA 를 읽는데 SDA 는 놓인 1 → `1 1`. 세 번째 놓기(틱 t₀+25)에 슬레이브도 놓으며 0x5A 의 MSB(0)부터 내므로 `0 1 0 1 1 0` → `1101 0110` = 0xD6. 마스터의 ACK 클럭은 슬레이브에겐 bit1 의 클럭이라 그 뒤 바이트도 밀려 `7F`.

`demo_naive.c` 로 N = 0..30 을 훑은 실제 출력 (발췌):

```text
  N | aware: wait  write read stop | naive write: min tHIGH  slave got  ACK | naive read: got | naive STOP seen
  5 |         0  ok    ok   ok   |               5       0xA5  ACK  |           0x5A | yes
  6 |         1  ok    ok   ok   |               4       0xA5  ACK  |           0x5A | yes
  7 |         2  ok    ok   ok   |               3       0xA5  ACK  |           0xDA | yes
  9 |         4  ok    ok   ok   |               1       0xA5  ACK  |           0xDA | yes
 10 |         5  ok    ok   ok   |               5       0x4B  NACK |           0xAD | NO
 16 |        11  ok    ok   ok   |               4       0x4B  NACK |           0xAD | NO
 20 |        15  ok    ok   ok   |               5          -  NACK |           0xD6 | NO
 29 |        24  ok    ok   ok   |               1          -  NACK |           0xF6 | NO
 30 |        25  ok    ok   ok   |               5          -  NACK |           0xEB | NO
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">demo_naive.c 실측: 스트레칭 길이 N(틱 = µs)별 결과, t_low = t_high = 5</text>
  <g stroke="currentColor" stroke-width=".5">
    <rect x="150" y="40" width="558" height="26" class="f-ok"/>
    <rect x="150" y="80" width="108" height="26" class="f-ok"/>
    <rect x="258" y="80" width="72" height="26" class="f-mut"/>
    <rect x="330" y="80" width="378" height="26" class="f-warn"/>
    <rect x="150" y="120" width="126" height="26" class="f-ok"/>
    <rect x="276" y="120" width="432" height="26" class="f-warn"/>
    <rect x="150" y="160" width="180" height="26" class="f-ok"/>
    <rect x="330" y="160" width="378" height="26" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="58">정답 (3 종류 모두)</text><text x="10" y="98">naive 쓰기</text>
    <text x="10" y="138">naive 읽기</text><text x="10" y="178">naive STOP</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="429" y="58">전부 ok, 기다림 = max(0, N − 5), tHIGH 항상 5 µs</text>
    <text x="204" y="98">ok</text><text x="294" y="98">tHIGH 4,3,2,1</text>
    <text x="519" y="98">클럭 삼킴: 1개(10–19) → 0x4B NACK, 2개(20–29) → 수신 없음</text>
    <text x="213" y="138">ok</text><text x="492" y="138">틀린 값을 OK 로 반환 (DA, AD, ED, D6, F6, EB)</text>
    <text x="240" y="178">STOP 보임</text><text x="519" y="178">STOP 사라짐 (SCL LOW 중 SDA↑)</text>
  </g>
  <g stroke="currentColor">
    <line x1="150" y1="196" x2="708" y2="196"/>
    <line x1="150" y1="196" x2="150" y2="202"/><line x1="240" y1="196" x2="240" y2="202"/><line x1="330" y1="196" x2="330" y2="202"/>
    <line x1="420" y1="196" x2="420" y2="202"/><line x1="510" y1="196" x2="510" y2="202"/><line x1="600" y1="196" x2="600" y2="202"/>
    <line x1="690" y1="196" x2="690" y2="202"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="150" y="216">0</text><text x="240" y="216">5</text><text x="330" y="216">10</text><text x="420" y="216">15</text>
    <text x="510" y="216">20</text><text x="600" y="216">25</text><text x="690" y="216">30 = N</text>
  </g>
  <text x="10" y="242" fill="currentColor" font-size="12">한 칸 = 1 틱. 6 = t_low+1 (보이기 시작), 10 = t_low+t_high (한 비트 주기를 덮음), 주기 10 반복.</text>
</svg>
```

해석: 스트레칭이 마스터 low 구간(5)을 넘는 순간부터 naive 는 깨진다. N 이 한 비트 주기(10) 미만이면 "high 가 짧아짐"(타이밍 위반 — 이 모델에서는 데이터는 살지만, 실제로는 짧은 펄스를 느린 rise time 이 지워 버릴 수 있다), 10 이상이면 비트 주기 단위로 클럭이 사라진다. 쓰기는 대개 NACK 으로 드러나지만 **읽기는 마스터가 성공으로 보고**하므로 CRC 없는 센서 값은 그냥 틀린 값이 된다 — "가끔 쓰레기 값" 버그의 정체.

### Part C — 드라이버와 로그

| 결과 | 버스에 나간 것 | 반환 | 의미 |
|---|---|---|---|
| 정상 | S 90 A 0F A Sr 91 A 6A A 01 N P | `OK` | — |
| 없는 0x3C | S 78 N P | `NACK_ADDR` | 배선·주소·전원 (B06) |
| rx_cap 1 슬레이브에 3바이트 | S 90 A 10 A 20 N P | `NACK_DATA` | 디바이스가 거부 |
| 스트레칭 > 한도 | S … (SCL 붙잡힘) → 두 선 놓기, STOP 없음 | `TIMEOUT` | 디바이스 hang / 한도가 너무 짧음 |
| 직후 재호출 (SCL 아직 LOW) | 없음, 틱 증가 0 | `BUS_BUSY` | 기다리거나 복구 |
| addr 0x80, nw = nr = 0, NULL 버퍼 | 없음 | `ARG` | — |

레지스터 읽기 실제 로그 (바이트마다 50 µs 스트레칭, 100 kHz):

```text
    t(us)    dur  event    byte  ack   stretch(us)
        0      6  START                0
        6     90  ADDR     0x90  ACK   0
       96     90  TX       0x0F  ACK   0
      186     15  Sr                   0
      201     90  ADDR     0x91  ACK   0
      291    135  RX       0x6A  ACK   45
      426    135  RX       0x01  NACK  45
      561     10  STOP                 0
```

읽는 법: RX 바이트가 90 이 아니라 135 µs — 45 µs 가 스트레칭이다 (50 − t_low 5). 스트레칭은 슬레이브가 "주소+R ACK 뒤"와 "마스터 ACK 뒤"에 걸었지만, 마스터가 기다리는 곳은 **다음 바이트의 첫 비트**라서 RX 줄에 붙는다. 마지막 바이트 뒤(NACK)에는 슬레이브가 늘이지 않으므로 STOP 은 10 µs. 합계 571 µs = 기본 481 + 90.

같은 트랜잭션에서 슬레이브가 135 µs 붙잡고 한도가 100 µs 일 때:

```text
    t(us)    dur  event    byte  ack   stretch(us)
      963      6  START                0
      969     90  ADDR     0x90  ACK   0
     1059     90  TX       0x0F  ACK   0
     1149     15  Sr                   0
     1164     90  ADDR     0x91  ACK   0
     1254    106  TIMEOUT  in RX       0
```

106 = low 반주기 5 (SDA 놓기 1 + 채우기 3 + 놓기 1) + 기다림 100 + `m_abort` 1. 브링업 엔지니어가 이 로그에서 읽을 것: "Sr 과 주소+R 까지는 정상, 첫 데이터 바이트 앞에서 슬레이브가 100 µs 넘게 붙잡았다 → 측정 변환 시간 / hold-master 모드 / 한도 설정을 본다".

**랜덤 300 트랜잭션** (고정 시드, 대상 0x48/0x3C, nw·nr 0..3, 스트레칭 위치 랜덤, 1/16 은 한도 초과 길이) 실제 출력:

```text
random: ok=230 timeout=30 nack_addr=40, stretch total=6803 us, min SCL high=5 us, mismatches=0
61/61 passed
```

예측(이벤트 순서대로 기다림을 더하고 한도 초과면 TIMEOUT)과 결과 코드·데이터·스트레칭 합이 전부 일치했고, 타임아웃 30번을 포함한 전체 실행에서 **가장 짧은 SCL high 펄스 = 5 µs** — 정답 마스터는 어떤 스트레칭에서도 tHIGH 를 깎지 않는다.

**스트레칭이 처리량을 얼마나 먹나** (`demo_naive.c`, S 0x48+W reg Sr 0x48+R 6바이트 P = 81 클럭, 실제 출력):

| 바이트당 N (µs) | 전체 (µs) | 스트레칭 (µs) | 비율 | 실효 SCL (kHz) | 초당 읽기 |
|---|---|---|---|---|---|
| 0 | 841 | 0 | 0.0% | 96.3 | 1189 |
| 10 | 871 | 30 | 3.4% | 93.0 | 1148 |
| 50 | 1111 | 270 | 24.3% | 72.9 | 900 |
| 100 | 1411 | 570 | 40.4% | 57.4 | 709 |
| 500 | 3811 | 2970 | 77.9% | 21.3 | 262 |
| 1000 | 6811 | 5970 | 87.7% | 11.9 | 147 |

이벤트는 6번(주소+R ACK 뒤 + 마스터 ACK 5번), 각 N − 5 µs. 1 kHz IMU 를 이런 디바이스로 읽으면 N = 100 µs 만 돼도 예산(1000 µs) 의 1.4배 — 블로킹 드라이버라면 태스크가 그만큼 멈춰 있다. 하드웨어 I2C + 인터럽트/DMA 를 쓰면 CPU 는 놀지만 **버스**는 여전히 그 시간 동안 점유된다.

### 실제 칩·OS 에서는

| 환경 | 스트레칭 처리 (대표 — 문서·소스 확인 권장) |
|---|---|
| STM32 I2C 하드웨어 (마스터) | 하드웨어가 SCL 을 감시해 자동으로 기다린다. 새 세대(I2C v2: F0/F3/F7/L4/G4/H7 등)는 `TIMEOUTR` 로 SCL low 타임아웃(TIMEOUTA)과 누적 확장 한도(TIMEOUTB, SMBus 용)를 걸 수 있고 넘으면 TIMEOUT 플래그·인터럽트 |
| STM32 I2C 하드웨어 (슬레이브) | 기본으로 늘인다. `I2C_CR1.NOSTRETCH = 1` 로 끄면 펌웨어가 제때 데이터를 못 주면 overrun/underrun. RM 은 마스터 모드에서는 이 비트를 0 으로 두라고 한다 |
| Linux `i2c-algo-bit` (i2c-gpio 등) | `sclhi()` 가 SCL 을 놓고 `getscl` 로 읽어 `adap->timeout` 까지 기다린 뒤 `-ETIMEDOUT` — 이 레슨의 `m_scl_high` 와 같은 구조. SCL 을 읽을 수 없는 구성(출력 전용 SCL)이면 스트레칭 지원 불가 |
| Raspberry Pi (BCM2835 계열 I2C) | 특정 구간의 스트레칭을 제대로 처리하지 못한다는 하드웨어 버그 보고가 유명 (예: BNO055 같은 센서) — 우회: 클럭 낮추기, i2c-gpio 비트뱅. 정확한 조건은 포럼/errata 확인 권장 |
| SMBus | 디바이스가 SCL 을 tTIMEOUT(최소 25 ms, 최대 35 ms) 넘게 low 로 보면 인터페이스 리셋, 누적 확장 한도(슬레이브 T_LOW:SEXT 25 ms, 마스터 T_LOW:MEXT 10 ms) — 연습 1 이 누적 한도를 흉내 낸다 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 61/61 passed, exit 0 (약 0.1 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
cc -std=c11 -Wall -Wextra -O2 demo_naive.c -o demo_naive && ./demo_naive          # 5. 풀이의 표 두 개
```

## 7. 자주 하는 실수

**1. SCL 을 놓고 "high 겠지" 한다.** 위의 네 가지 고장 전부. GPIO 비트뱅에서 흔한 형태: `GPIO_SET(SCL); delay_us(5); v = GPIO_READ(SDA); GPIO_CLR(SCL);`. 고친 코드: 놓은 뒤 `while (!GPIO_READ(SCL)) { if (++w > max) return -ETIMEDOUT; delay_us(1); }` 다음에 `delay_us(t_high)`.

**2. high 반주기를 "놓은 순간"부터 센다.** 기다림 루프를 넣었어도 `t_high` 를 루프 *전에* 시작한 타이머로 재면, 슬레이브가 늦게 놓을수록 high 가 짧아진다 (naive 와 같은 runt). 고친 것: high 확인 *후* `idle_ticks(b, t_high - 1)`. 테스트 불변식 `min_high == t_high` 가 이것을 잡는다.

**3. 타임아웃이 없다.** `while (!scl_read());` — 고장 슬레이브 하나가 펌웨어 전체(또는 그 태스크와 버스 뮤텍스를 기다리는 모든 태스크)를 영원히 멈춘다. 워치독 리셋으로만 빠져나오고, 리셋 뒤에도 슬레이브는 여전히 붙잡고 있다. 고친 것: `max_stretch` + 에러 코드 + 상위 정책.

**4. 타임아웃이 너무 짧다.** "I2C 는 100 kHz 니까 100 µs 면 충분" — hold-master 측정 센서는 변환 시간(수 ms) 내내 늘인다. 정상 디바이스를 TIMEOUT 으로 끊고, 끊긴 읽기는 위 표처럼 SDA stuck-low 를 남길 수 있다. 고친 것: 데이터시트의 최대 스트레칭(변환 시간) × 여유, SMBus 디바이스면 25 ms 이상.

**5. 타임아웃 뒤 STOP 을 내려고 한다.** SCL 이 LOW 로 붙잡혀 있으면 "SCL high 중 SDA↑" 를 만들 수 없다. SDA 를 0→1 로 흔들어 봐야 SCL LOW 중 변화라 아무 의미가 없고, 놓지 않은 SDA 0 은 상황만 나쁘게 한다. 고친 것: `m_abort` 로 두 선을 놓고, 버스 상태를 보고(SCL 돌아왔나, SDA 는?) M03 절차로 넘긴다.

**6. 타임아웃 직후 바로 재시도한다.** 슬레이브가 아직 붙잡고 있으면 `BUS_BUSY`, 막 놓았다면 버스 free 시간 없이 낸 START 를 슬레이브가 놓친다 (Part B 에서 실제로 겪은 버그). 고친 것: idle 확인 + tBUF 대기 + 재시도 횟수 제한, 그리고 재시도 정책은 **한 계층에만** (COMM-M12).

**7. 읽기에서 타임아웃 = "그냥 실패" 로 끝낸다.** 슬레이브는 바이트 중간에 있고, MSB 가 0 이면 SDA 를 쥔 채 남는다. 다음 호출이 `BUS_BUSY` 를 반복한다. 고친 것: TIMEOUT 뒤에는 버스 상태를 검사해 SDA LOW 면 COMM-M03 의 9클럭 복구를 부르는 경로를 드라이버 상위에 둔다.

## 8. 면접 Q&A

<details><summary>Q1. 클럭 스트레칭이 정확히 뭐죠? 왜 I2C 에서만 가능한가요?</summary>

슬레이브가 SCL 을 LOW 로 당겨 마스터의 클럭 LOW 구간을 늘이는 것이다. SCL 이 오픈드레인 wired-AND 라서 가능하다 — 마스터가 놓아도 누가 하나라도 당기면 선은 LOW. 슬레이브는 이걸로 "바이트를 처리할 시간"(MCU 슬레이브가 ISR 에서 레지스터를 읽을 때까지, 센서가 측정을 끝낼 때까지)을 번다. SPI 는 SCLK 가 마스터의 push-pull 출력이라 슬레이브가 멈출 수단이 없어서 대신 "busy 비트 폴링"이나 별도 READY/DRDY 핀을 쓴다. 꼬리: I3C 는? SCL 이 push-pull 이라 타깃이 클럭을 늘일 수 없고, 대신 NACK·재시도나 IBI 같은 다른 메커니즘을 쓴다 (MIPI I3C 규격 확인 권장, COMM-A05).

</details>

<details><summary>Q2. 마스터가 스트레칭을 "지원한다"는 건 구현상 무엇인가요?</summary>

세 가지: ① SCL 을 **입력으로 읽을 수 있어야** 한다 (비트뱅이면 open-drain + 입력 버퍼, 출력 전용 SCL 이면 불가) ② SCL 을 놓은 뒤 실제 HIGH 를 볼 때까지 기다린다 ③ **high 반주기는 HIGH 를 본 순간부터** 센다. ③ 을 빼면 기다리긴 해도 tHIGH 가 깎인다 (7. 실수 2). 이 레슨에서는 `m_scl_high` 10줄이고, 비트·STOP·Sr 모든 "SCL 올리기"에 쓴다 — STOP 과 Sr 을 빼먹으면 마지막 ACK 뒤 스트레칭에서 STOP 이 사라진다 (naive 시나리오 4).

</details>

<details><summary>Q3. 스트레칭 타임아웃은 몇으로 잡나요? I2C 규격에 최대값이 있나요?</summary>

I2C(UM10204) 자체에는 스트레칭 최대 시간이 없다 — 슬레이브는 원하는 만큼 늘일 수 있다. 그래서 값은 시스템이 정한다: ① 버스에 붙은 디바이스들의 데이터시트 최대 스트레칭(예: hold-master 측정 = 변환 시간 수 ms) × 여유 ② SMBus 디바이스라면 tTIMEOUT 25–35 ms 와 누적 한도(연습 1) ③ 상위 데드라인(1 kHz 제어 루프라면 한 트랜잭션에 쓸 수 있는 시간). 너무 짧으면 정상 디바이스를 끊고, 너무 길면 고장 하나가 태스크를 오래 묶는다. 실무 답: 디바이스별로 다르게(드라이버가 per-device 한도), 그리고 타임아웃은 "버스 hang 감지"로 쓰고 측정 대기는 폴링 모드로 바꾸는 것을 먼저 검토.

</details>

<details><summary>Q4. 타임아웃이 났습니다. 그다음 무엇을 하나요?</summary>

① 마스터의 두 선을 놓는다 (STOP 시도 금지 — SCL 이 LOW 라 STOP 조건을 못 만든다) ② 에러를 위로 올리고 로그에 단계·시간을 남긴다 (이 레슨의 TIMEOUT 행) ③ 버스 상태 진단: SCL 이 계속 LOW → 마스터 쪽 수단 없음: 슬레이브 RESET 핀·전원 스위치, SMBus 슬레이브면 자체 타임아웃을 기다림, 하드웨어 고장(쇼트) 의심 / SCL HIGH 인데 SDA LOW → COMM-M03 의 9클럭 + STOP 복구 / 둘 다 HIGH → 버스 free 시간 뒤 재시도. ④ 재시도 횟수를 제한하고 반복되면 디바이스를 "offline" 으로 표시. 꼬리: 하드웨어 I2C 블록은? STM32 라면 TIMEOUT 플래그 처리 뒤 주변장치를 리셋(PE 비트 토글)하고, BUSY 가 안 풀리면 핀을 GPIO 로 바꿔 M03 절차를 한다 (RM/errata 확인 권장).

</details>

<details><summary>Q5. 스트레칭을 지원하지 않는 마스터에 스트레칭하는 센서를 붙이면 무슨 증상이 보이나요?</summary>

이 레슨의 실측 그대로: 짧은 스트레칭이면 high 펄스가 짧아지는 타이밍 위반(tHIGH 3 µs < 4.0 µs), 비트 주기 이상이면 클럭이 사라져 데이터가 밀린다. 쓰기는 NACK 으로 드러나지만 **읽기는 마스터가 성공으로 보고하고 값만 틀린다** (`5A C3` → `D6 7F`). 마지막 ACK 뒤 스트레칭이면 STOP 이 사라져 슬레이브가 바이트 중간 상태로 남는다. 로직 애널라이저에서 보는 법: S 와 P 사이 완전한 SCL 펄스 수가 9의 배수가 아니다 (연습 4 검사기가 16 을 찾아낸다), 디코더가 바이트 경계를 엉뚱하게 잡는다, 짧은 high 펄스. 라즈베리파이 + BNO055 조합이 유명한 사례로 회자된다 (조건은 확인 권장).

</details>

<details><summary>Q6. STM32 의 NOSTRETCH 는 언제 켜나요?</summary>

슬레이브 모드 전용이다. 버스의 마스터가 스트레칭을 지원하지 않을 때(Q5 같은 마스터) 슬레이브 쪽에서 늘이기를 끈다. 대가: 펌웨어가 다음 바이트를 **제때**(다음 클럭 전) 데이터 레지스터에 넣거나 받은 바이트를 꺼내야 하고, 못 하면 underrun(송신)·overrun(수신) 플래그가 서고 데이터가 틀린다 — 그래서 NOSTRETCH 슬레이브는 보통 DMA 로 데이터를 미리 준비한다. 마스터 모드에서는 이 비트를 0 으로 둬야 한다 (RM 확인 권장). 꼬리: "우리 MCU 가 슬레이브인데 ISR 지연이 크면?" → 스트레칭을 켜 두면 안전하지만 버스 전체가 그 ISR 지연만큼 느려진다.

</details>

<details><summary>Q7. 스트레칭은 RTOS 설계와 처리량에 어떤 영향을 주나요?</summary>

트랜잭션 시간이 디바이스가 정한다. 6바이트 버스트 읽기가 841 µs 에서 바이트당 100 µs 스트레칭이면 1411 µs (실측) — 같은 버스의 다른 디바이스(예: 1 kHz IMU)는 그동안 기다린다. 설계 대응: ① 느린 디바이스와 빠른 디바이스를 다른 버스로 ② hold-master 대신 "측정 시작 → 나중에 읽기" 폴링 모드 ③ 버스 뮤텍스를 잡는 시간을 짧게, 블로킹 대신 인터럽트/DMA 비동기 드라이버(COMM-M13 의 큐 구조) ④ 타임아웃을 데드라인 예산 안으로. 블로킹 비트뱅에서는 CPU 까지 묶인다.

</details>

<details><summary>Q8. 클럭 스트레칭과 멀티마스터 "클럭 동기화"는 같은 메커니즘인가요?</summary>

같은 wired-AND 현상이다. 두 마스터가 동시에 클럭을 내면 SCL LOW 는 **가장 긴 LOW** 를 가진 쪽이, HIGH 는 **가장 짧은 HIGH** 를 가진 쪽이 정한다 — 각 마스터가 "놓은 뒤 HIGH 를 볼 때까지 기다리고 그때부터 HIGH 를 센다"는 같은 규칙을 지키기 때문이다. 슬레이브 스트레칭은 그 규칙의 특수한 경우(LOW 를 늘이는 노드가 슬레이브). 그래서 스트레칭을 지원하는 마스터 코드는 멀티마스터 클럭 동기화의 절반을 이미 구현한 셈이고, 나머지(중재)는 COMM-M02 에서 한다.

</details>

## 9. 추가 연습

### 연습 1 — 트랜잭션 누적 스트레칭 예산

SMBus 는 한 번의 스트레칭뿐 아니라 START 부터 STOP 까지 **누적** 확장에도 한도를 둔다 (슬레이브 T_LOW:SEXT 25 ms, 대표값). `g_budget` 을 넘으면 `I2C_ERR_TIMEOUT_CUM` (−6) 을 돌려주는 `m_scl_high_budget()` 을 `b->scl_up` 에 꽂아라. 한 번 한도 100, 예산 150, 스트레칭 60·60·60 (각 55 기다림)에서 결과는? 한 번에 200 이면?

<details><summary>답 보기</summary>

```c
static int m_scl_high_budget(Bus *b) {
    uint32_t left = g_budget - g_spent, save = b->max_stretch;
    bool cum = left < save;
    if (cum) b->max_stretch = left;          /* 이번 기다림 한도 = min(한 번 한도, 남은 예산) */
    int r = m_scl_high(b);
    b->max_stretch = save;
    if (r == I2C_ERR_TIMEOUT && cum) return I2C_ERR_TIMEOUT_CUM;
    if (r == I2C_OK) g_spent += b->last_wait;
    return r;
}
```

이벤트 2개(주소 + 1바이트)면 110 으로 OK. 3개면 셋째에서 남은 40 만 기다리고 `TIMEOUT_CUM` (`last_wait == 40`) — 예산을 다 쓴 뒤가 아니라 **넘기 전에** 끊는다. 한 번에 200 이면 한 번 한도 100 이 먼저 걸려 보통 `TIMEOUT`. `i2c_write_read` 는 프리미티브 에러를 그대로 돌려주도록(`return r;`) 짜 두었기 때문에 수정 없이 새 코드가 위로 올라간다. 랜덤 500 트랜잭션에서 예측(누적이 먼저냐 한 번 한도가 먼저냐)과 일치.

</details>

### 연습 2 — ACK 앞에서 늘이는 슬레이브

어떤 슬레이브는 8번째 비트를 받은 SCL↓ 에서 바로 붙잡고, 바이트를 검사한 뒤 붙잡기가 끝날 때 ACK/NACK 을 낸다 (`STR_PRE_ACK`). 정답 마스터는 수정 없이 동작하나? naive 마스터는?

<details><summary>답 보기</summary>

정답 마스터는 그대로 동작한다: 9번째 클럭의 `m_read_bit` 이 `scl_up` 으로 기다린 뒤 SDA 를 읽으니까 (바이트마다 25 기다림, 버퍼가 차면 늦은 NACK 도 정확히 받음). naive 는 9번째 클럭에서 아직 놓인 SDA(1)를 읽어 **NACK** 으로 본다 — 그런데 슬레이브는 바이트를 이미 저장했다 (`rx_len == 1`). 드라이버가 NACK 을 보고 재시도하면 **같은 쓰기가 두 번** 들어간다 (FIFO·카운터 레지스터면 치명적). 게다가 늦게 나온 ACK(0) 를 마스터가 클럭으로 치워 주지 않아 실행 결과는 `SCL=1 SDA=0` 으로 버스가 남는다 — 다시 COMM-M03 의 상황. 실제 출력: `naive + pre-ACK stretch: ret=0 all_ack=0 slave rx_len=1, bus idle after=0 (SCL=1 SDA=0)`.

</details>

### 연습 3 — 트랜잭션 길이 공식

`xfer_ticks(t_low, t_high, nw, nr, Σw)` 를 세우고 t_low ∈ {2,3,5,8}, t_high ∈ {1,2,5}, nw·nr ∈ 0..3, 랜덤 스트레칭에서 시뮬레이션과 맞춰라. 100 kHz 레지스터 읽기(1 W + 2 R)와 6바이트 버스트는 몇 µs?

<details><summary>답 보기</summary>

T = t_low + t_high 라 하면 START (t_high + 1) + [nw > 0] 9T(1 + nw) + [nw > 0 ∧ nr > 0] (t_low + 2 t_high) + [nr > 0] 9T(1 + nr) + STOP T + Σw. 1440 케이스 전부 일치 (실제 출력 `formula vs sim: 1440 cases, mismatches=0`). 100 kHz: 레지스터 읽기 **481 µs**, 6바이트 버스트 **841 µs** — demo 의 N = 0 행과 같다. 이 검사가 본문 "모델에서 배운 것"의 1틱 펄스 버그(t_high = 1)를 처음 잡았다: 공식은 맞는데 시뮬레이션이 NACK 을 냈다. 공식 대조는 시뮬레이터 자체의 버그를 잡는 좋은 습관이다.

</details>

### 연습 4 — 로직 애널라이저 검사기

트레이스(SCL/SDA 문자열)만 보고 ① START/Sr 에서 다음 Sr/STOP 까지의 구간 수 ② 그 구간의 완전한 SCL 펄스 수가 9의 배수가 아닌 구간 수 ③ high 폭이 `t_high_min` 미만인 펄스(runt) 수를 세는 `la_check()` 를 짜라. 정답·naive 캡처에 돌려 보라.

<details><summary>답 보기</summary>

인접 두 칸 비교: SCL 1→1 + SDA 변화 = 조건(구간을 닫고, ↓ 면 새 구간 시작), SCL 0→1 = 펄스 시작(폭 1), 1→1 = 폭 +1, 1→0 = 완전한 펄스 (START 직후의 SCL↓ 은 "펄스 시작"이 없었으므로 세지 않는다). 결과(실제 출력): 정답 마스터 N = 7 → 구간 1, 펄스 18, runt 0 / naive N = 7 → `runts=1 min_high=3` / naive N = 25 → `bad=1 pulses=16` (2개 삼켜짐) / Sr 이 있는 레지스터 읽기 → 구간 2개, 18 + 36 펄스, 이상 없음. 실제 LA 소프트웨어의 I2C 디코더도 이 원리이고, 타이밍 규격 검사(tHIGH, tSU;DAT …)를 더하면 HWLAB-M05 의 검사기가 된다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 18/18 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o pr_asan && ./pr_asan   # 오류 없음
```

## 10. 핵심 정리

- 클럭 스트레칭 = 슬레이브가 SCL 을 LOW 로 계속 당김. SCL 도 wired-AND 라서 **"놓았다 ≠ high"**.
- 마스터 규칙: SCL 을 놓고 → **다시 읽어** HIGH 를 볼 때까지 기다리고 → **그때부터** t_high 를 센다. 비트·STOP·Sr 의 모든 SCL 올리기에 적용.
- 기다림 w = max(0, N − t_low): 마스터 low 구간 안의 스트레칭은 보이지 않는다.
- 타임아웃 필수: I2C 엔 상한이 없다 → 시스템이 정한다 (데이터시트 최대 × 여유, SMBus 25–35 ms, 데드라인). 경계 = "딱 한도는 통과, +1 은 실패".
- 타임아웃 뒤: STOP 시도 말고 두 선을 놓기 → 버스 진단: SCL LOW = 슬레이브 리셋/전원, SDA LOW = M03 9클럭, 둘 다 HIGH = tBUF 뒤 재시도.
- naive 마스터: tHIGH 축소 → 클럭 삼킴 → 읽기는 **틀린 값을 성공으로** → STOP 소실. LA 에선 "S–P 사이 펄스 ≠ 9k" 와 runt.
- 드라이버는 NACK_ADDR / NACK_DATA / TIMEOUT / BUS_BUSY / ARG 를 구분하고, 단계별 시간·스트레칭 로그를 남긴다 (예: RX 135 µs 중 45 µs 가 스트레칭).

## 참고자료

- NXP, *UM10204 I2C-bus specification and user manual* (Rev. 7) — clock synchronization, clock stretching, tLOW/tHIGH/tBUF 타이밍 표: <https://www.nxp.com/docs/en/user-guide/UM10204.pdf> (절 번호·숫자는 기억에 의존한 대표값 — 확인 권장)
- SMBus Specification 3.x — tTIMEOUT, T_LOW:SEXT / T_LOW:MEXT: <http://smbus.org/specs/> (확인 권장)
- Linux kernel `drivers/i2c/algos/i2c-algo-bit.c` (`sclhi()` 의 SCL 읽기·타임아웃)와 `drivers/i2c/busses/i2c-gpio.c`: <https://github.com/torvalds/linux/tree/master/drivers/i2c>
- ST, STM32 reference manual (I2C v2 장: `NOSTRETCH`, `TIMEOUTR` TIMEOUTA/TIMEOUTB/TEXTEN, TIMEOUT 플래그) 과 각 칩 errata sheet — 레지스터 세부는 RM 확인 권장
- Sensirion SHT3x / TE HTU21D 데이터시트 — clock stretching(hold master) 측정 명령과 폴링 명령 (명령 코드는 데이터시트 확인)
- Raspberry Pi 포럼·GitHub 이슈의 BCM2835 I2C clock-stretching 버그 논의 (조건·우회는 출처마다 다름 — 확인 권장)
- 관련 레슨: COMM-B06 (버스 모델·슬레이브 상태 머신의 원본), COMM-B07 (repeated START·센서 드라이버), COMM-M02 (중재·클럭 동기화), COMM-M03 (SDA stuck-low 9클럭 복구), COMM-M12 (bus_ops 계층과 재시도 정책 위치), COMM-M13 (비동기 트랜잭션 큐), COMM-A05 (I3C), HWLAB-B03 (풀업·rise time), HWLAB-M05 (LA 타이밍 검사), RTOS-B 뮤텍스 레슨들 (버스 공유)
