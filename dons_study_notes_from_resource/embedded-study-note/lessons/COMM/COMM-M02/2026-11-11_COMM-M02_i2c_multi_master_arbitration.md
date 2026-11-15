---
id: COMM-M02
time: 75분
updated: 2026-11-11
---
# COMM-M02 I2C 멀티마스터 중재 (arbitration lost)

## 학습 목표

- wired-AND 버스에서 **"1 을 낸 쪽은 0 을 볼 수 있지만 0 을 낸 쪽은 1 을 볼 수 없다"** 는 한 줄로 중재(arbitration)를 설명하고, 두 마스터가 같은 틱에 START 한 파형에서 누가 어느 비트에서 지는지 읽는다.
- 각 마스터가 자기 클럭 상태 머신을 가진 시뮬레이터로 **SCL 동기화**(버스 low = 가장 긴 low, high = 가장 짧은 high)를 재현하고 측정으로 확인한다.
- 패배 처리를 구현한다: SDA 즉시 놓기, 정책(바로 손 떼기 / 그 바이트 끝까지 클럭), **STOP + 버스 free 시간 뒤 재시도**, 재시도 상한.
- 경계 경우를 말할 수 있다: 주소가 같고 데이터가 다를 때(데이터 단계 패배), 완전히 같은 메시지(둘 다 "성공"), 읽기의 ACK 비트 패배, 1틱 늦은 요청(중재가 아니라 busy 감지), 규격상 금지된 경우(STOP vs 데이터 비트).
- 드라이버 API 에서 중재 패배를 `-EAGAIN` 으로 올리고, 로그로 "누가 언제 졌고 언제 다시 갔는지"를 보여 준다. 낮은 주소가 이기는 구조가 만드는 **굶주림(starvation)** 을 측정한다.

## 1. 문제

멀티마스터 I2C 는 "보드에 MCU 가 둘인데 같은 EEPROM·PMIC·온도 센서를 본다"(메인 SoC + 안전 MCU, BMC + 호스트의 SMBus, 로봇의 주 제어기 + 모터 보드)에서 나온다. 면접 꼬리질문의 단골이 "둘이 동시에 START 하면?", "진 쪽은 뭘 하나?", "클럭은 누구 것이 되나?" 이고, 브링업에서는 STM32 의 `ARLO` 플래그나 Linux 의 `-EAGAIN` 이 가끔 뜨는 이유를 설명할 수 있어야 한다.

COMM-B06 에서 본 대로 SDA·SCL 은 오픈드레인 + 풀업 = **wired-AND** 다. 그래서 두 마스터가 동시에 비트를 내면 버스 레벨은 둘의 AND 이고, 0 을 낸 쪽은 자기가 낸 0 을 그대로 읽어 아무것도 모르지만, **1 을 낸 쪽은 0 을 읽고 "누가 다른 걸 보내는구나" 를 안다**. 그 쪽이 물러난다(중재 패배). 진 쪽이 낸 비트는 이긴 쪽 비트와 처음 다른 비트 직전까지 *모두 같았으므로* 버스 위 데이터는 이긴 쪽 메시지 그대로다 — 데이터가 망가지지 않는 중재(non-destructive arbitration). CAN 의 ID 중재(COMM-M05)와 같은 원리다.

아래는 이 레슨 `solution.c` 가 실제로 만든 트레이스다. 마스터 A 는 0x50 에, B 는 0x68 에 같은 틱에 쓰기를 시작한다 (`t_low = t_high = 2` 틱).

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="16" fill="currentColor" font-weight="bold">중재: A → 0x50+W (1010 0000) vs B → 0x68+W (1101 0000), 같은 틱 START — 틱 0..44</text>
  <rect x="224" y="128" width="15" height="84" class="f-warn" opacity=".35"/>
  <g stroke="currentColor" stroke-dasharray="2 3" opacity=".45">
    <line x1="177.5" y1="40" x2="177.5" y2="210"/><line x1="231.5" y1="40" x2="231.5" y2="210"/>
    <line x1="285.5" y1="40" x2="285.5" y2="210"/><line x1="339.5" y1="40" x2="339.5" y2="210"/>
    <line x1="393.5" y1="40" x2="393.5" y2="210"/><line x1="447.5" y1="40" x2="447.5" y2="210"/>
    <line x1="501.5" y1="40" x2="501.5" y2="210"/><line x1="555.5" y1="40" x2="555.5" y2="210"/>
    <line x1="609.5" y1="40" x2="609.5" y2="210"/><line x1="663.5" y1="40" x2="663.5" y2="210"/>
  </g>
  <g fill="none" stroke="currentColor" stroke-width="2">
    <polyline points="110,48 150.5,48 150.5,70 177.5,70 177.5,48 204.5,48 204.5,70 231.5,70 231.5,48 258.5,48 258.5,70 285.5,70 285.5,48 312.5,48 312.5,70 339.5,70 339.5,48 366.5,48 366.5,70 393.5,70 393.5,48 420.5,48 420.5,70 447.5,70 447.5,48 474.5,48 474.5,70 501.5,70 501.5,48 528.5,48 528.5,70 555.5,70 555.5,48 582.5,48 582.5,70 609.5,70 609.5,48 636.5,48 636.5,70 663.5,70 663.5,48 690.5,48 690.5,70 717.5,70"/>
    <polyline points="110,93 123.5,93 123.5,115 164,115 164,93 218,93 218,115 272,115 272,93 326,93 326,115 596,115 596,93 650,93 650,115 717.5,115"/>
    <polyline points="110,138 123.5,138 123.5,160 164,160 164,138 717.5,138"/>
    <polyline points="110,183 123.5,183 123.5,205 164,205 164,183 218,183 218,205 272,205 272,183 326,183 326,205 717.5,205"/>
  </g>
  <g fill="currentColor">
    <text x="10" y="64">SCL 버스</text>
    <text x="10" y="109">SDA 드라이버 A</text>
    <text x="10" y="154">SDA 드라이버 B</text>
    <text x="10" y="199">SDA 버스</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="177.5" y="36">A6</text><text x="231.5" y="36">A5</text><text x="285.5" y="36">A4</text><text x="339.5" y="36">A3</text>
    <text x="393.5" y="36">A2</text><text x="447.5" y="36">A1</text><text x="501.5" y="36">A0</text><text x="555.5" y="36">R/W</text>
    <text x="609.5" y="36">ACK</text><text x="663.5" y="36">D7</text>
    <text x="190" y="89">1</text><text x="244" y="89">0</text><text x="298" y="89">1</text><text x="352" y="89">0</text>
    <text x="190" y="134">1</text><text x="244" y="134">1</text>
    <text x="123.5" y="225">START</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="300" y="134">B 는 진 뒤 SDA 를 놓은 채(1) — 버스엔 A 의 비트만 남는다</text>
    <text x="10" y="245">틱 9 (A5 의 SCL↑): B 는 1 을 냈는데 버스 SDA = 0 → ARB_LOST.</text>
    <text x="10" y="263">A 는 0 을 내고 0 을 읽었으니 충돌을 모른다. ACK 칸(틱 36–39): 슬레이브 0x50 이 당김.</text>
    <text x="10" y="281">점선 = 버스 SCL↑ = 샘플·중재 비교 시점. 한 칸 = 1틱.</text>
    <text x="10" y="299">출처: solution.c 실제 트레이스 (trace_print 출력).</text>
  </g>
</svg>
```

**모델 (주어짐, COMM-B06 모델 축약판을 "동기식 틱"으로 바꿈).** B06 에서는 마스터가 하나라 "마스터가 선을 바꿀 때마다 1틱" 이었다. 마스터가 둘이면 둘이 *같은 시각에* 움직여야 하므로 이번 모델은 동기식이다:

| 항목 | 규칙 |
|---|---|
| 틱 t | 모든 노드(마스터·슬레이브·고장 노드)가 같은 레벨 L(t) 와 지난 레벨 L(t−1) 을 보고 자기 드라이버 D(t+1) 을 정한다. L(t+1) = 모든 D(t+1) 의 AND |
| 마스터 클럭 | low 를 `t_low` 틱 센 뒤 SCL 을 놓고, **SCL 이 실제로 1 이 될 때까지 기다린 뒤** high 를 `t_high` 틱. high 도중 SCL 이 0 이 되면 즉시 low 단계로 (동기화) |
| 데이터 | SDA 는 SCL low 의 **첫 틱**에만 바꾼다 (그래서 `t_low ≥ 2`). 샘플은 SCL↑ 을 본 틱 |
| 중재 비교 | 샘플 틱에 "이 비트를 내가 내는데, 1(놓음)을 냈고 0 을 읽음" → 패배 |
| START / STOP | B06 과 같다. 마스터마다 버스 모니터(START→busy, STOP→free)를 따로 가진다 |
| 버스 free | STOP 뒤 SDA=SCL=1 이 `t_buf` 틱 이어지면 free (UM10204 의 t<sub>BUF</sub> 역할) |

### Part A — 클럭 동기화 + 비트 단위 중재

- `m_clock(m, b)` — 마스터의 SCL 상태 머신: `M_START_HOLD`(SDA 를 당긴 채 `t_hd_sta` 틱) → `M_LOW` → `M_WAIT_HIGH` → `M_HIGH` → … → `M_STOP_HIGH`. 다른 마스터가 SCL 을 먼저 당기면 따라 들어간다.
- `m_setup_bit(m, b)` — low 첫 틱에 SDA 로 무엇을 낼지: 주소/쓰기 데이터 비트, 받는 바이트면 놓기, 9번째 클럭엔 송신 측은 놓고 수신 측(읽기)은 ACK/마지막 NACK, STOP 준비면 0, 진 뒤면 1.
- `m_sample(m, b)` — SCL↑ 틱: **중재 비교**, 받은 비트 시프트, ACK 슬롯 처리(NACK → `NACK_ADDR`/`NACK_DATA`), 마지막 바이트 뒤 STOP 으로.
- 테스트: 단일 마스터 SCL low/high = 설정값, 같은 메시지를 보내는 2·3 마스터의 버스 low = max(t_low), high = min(t_high), 0x50 vs 0x68 (B 가 주소 bit 1 에서 패배, 0x68 슬레이브는 진 시도에서 불리지 않음, B 재시도 성공, 재시도 START = A 의 STOP + `t_buf` 틱), **주소 바이트 256 × 256 전수**: 작은 바이트가 이기고 진 쪽의 `lost_bit` = 처음 다른 비트.

### Part B — 패배 처리, 재시도, 경계 경우

- `mon_step` / `mon_free` — 버스 모니터: SCL=1 중 SDA↓ = START → busy, SDA↑ = STOP → free, free 는 STOP 뒤 `t_buf` 틱 idle.
- `m_arb_lost` / `m_give_up` — 패배: SDA 를 놓고(이미 1 이지만 *다음* 0 비트를 내면 안 된다), 정책 `keep_clock` 이면 그 바이트의 9번째 클럭까지 클럭만 내고, 아니면 바로 손 뗀다. 그 다음 `attempts > max_retries` 면 `ARB_LOST` 로 끝, 아니면 `M_WAIT_FREE` 에서 STOP + `t_buf` 를 기다렸다 다시 START.
- 주입·검증할 경우 (모두 테스트):
  1. 같은 주소 0x50, 데이터 `11 22` vs `11 2A` → 데이터 바이트 2 의 bit 4 에서 패배, 슬레이브는 `11 22` 뒤 재시도의 `11 2A` 를 받음.
  2. 완전히 같은 메시지 → 아무도 안 진다, **둘 다 OK**, 슬레이브는 한 번만 받음 (클럭 속도가 달라도).
  3. 같은 주소 읽기, A 는 1바이트(마지막 NACK) B 는 2바이트(ACK) → **ACK 비트에서 A 가 진다**.
  4. 같은 주소 쓰기 vs 읽기 → R/W 비트(bit 7)에서 읽기가 진다.
  5. B 의 요청이 1틱 늦게 옴 → 중재가 아니라 busy 감지, 패배 0번, A 의 STOP + `t_buf` 에 시작. `t_buf` 창 안에 온 요청도 나머지를 기다림.
  6. 재시도 상한: A 가 낮은 주소로 3개 연속 → B 는 매번 진다. `max_retries = 2` → `ARB_LOST`(3번 시도), `= 3` → 4번째에 성공.
  7. 정책 비교: 느린 B(6, 6)가 진 뒤 `keep_clock` 이면 그 바이트 9클럭 동안 SCL low 가 6틱(B 의 low)으로 늘어나고 다음 바이트부터 A 의 3틱. 바로 손 떼면 진 다음 클럭부터 3틱.
  8. 랜덤: 3 마스터(클럭 제각각) × 3 슬레이브, 요청이 몰리게, 쓰기/읽기 섞어 400 라운드 → 모든 요청 OK, 슬레이브별 수신 바이트 = 완료 순서대로 이어 붙인 쓰기 메시지, 버스의 START 수 = 진 시도를 뺀 트랜잭션 수.

### Part C — 드라이버 API 와 로그

```c
typedef struct { int attempts, n_lost, lost_byte, lost_bit; uint32_t wait_ticks, ticks; } XferInfo;
int i2c_mm_xfer(Bus *b, Master *m, uint8_t addr7, bool rd, const uint8_t *w, uint8_t *r, size_t n, XferInfo *info);
/* 0, -EINVAL, -ENXIO(주소 NACK), -EIO(데이터 NACK), -EAGAIN(중재 패배·재시도 소진),
   -EBUSY(버스가 끝내 free 가 안 됨), -ETIMEDOUT(트랜잭션 도중 멈춤 — 선을 놓고 끝냄) */
```

- 다른 MCU(마스터 A)가 같은 틱에 요청을 낸 상태에서 B 가 블로킹 호출 → 0, `attempts = 2`, `lost_byte/bit = 0/1`, 이벤트 로그 순서 검사.
- 각 에러 경로를 주입: 없는 0x3C, 슬레이브 버퍼 꽉 참, `rd && n == 0`, `addr7 = 0xA0`, 낮은 주소 마스터 3연속 + `max_retries = 1`, SDA 를 붙잡은 고장 노드(START 를 하나도 못 냄), 트랜잭션 도중 SCL 을 붙잡은 고장 노드(→ `-ETIMEDOUT`, 마스터 선은 놓인 상태).
- `demo_arb.c`: 동기화 표, **부하별 지연 — 높은 주소 마스터의 p99 가 얼마나 늘어나나**, 패배 정책이 승자 시간에 주는 영향.

## 2. Problem (English)

> **Multi-master I2C arbitration on a simulated open-drain bus.**
>
> You are given a synchronous discrete-time model: at each tick every node sees the resolved levels L(t) (and L(t−1)) and sets its own SDA/SCL drivers for t+1; L(t+1) is the wired-AND of all drivers (1 = released, 0 = pulling low). Several masters share the bus; each one generates its own SCL.
>
> 1. Implement the master's clock state machine `m_clock()`: hold START for `t_hd_sta` ticks, then pull SCL low for `t_low` ticks, release it and **wait until SCL actually reads high**, keep it high for `t_high` ticks. If SCL goes low while you are counting high (another master pulled it), enter your low phase immediately. The resulting bus clock must have low = max(t_low) and high = min(t_high) of all active masters.
> 2. Implement `m_setup_bit()` (change SDA only in the first low tick) and `m_sample()` (on the SCL rising edge). A master that drives a bit (address/write-data bits, or the ACK bit when it is the receiver) as 1 but reads 0 has **lost arbitration**: it must release SDA immediately, optionally keep clocking until the end of the current byte (policy flag), and retry after it has seen STOP plus `t_buf` idle ticks, at most `max_retries` times.
> 3. Implement the bus monitor (`mon_step`, `mon_free`): START → busy, STOP → free after `t_buf` idle ticks.
> 4. Implement a blocking `i2c_mm_xfer()` that returns Linux-style negative errno values: `-EAGAIN` for arbitration lost after all retries, `-ENXIO` address NACK, `-EIO` data NACK, `-EBUSY` bus never free, `-ETIMEDOUT` stuck transfer (release your lines first), and fills attempt/loss statistics.
>
> Example: A writes `[11 22]` to 0x50 and B writes `[33]` to 0x68, both starting at tick 0. Address bytes are 0xA0 and 0xD0; they first differ at bit 1 (A5). B loses at that SCL rising edge, A completes without noticing, slave 0x68 is never addressed during the first attempt, and B's retry START comes exactly `t_buf` ticks after A's STOP.
>
> Follow-ups: Who wins when two masters write different data to the same address? When both send identical messages? What does the loser do if it is also a slave? Can a master that started one tick later still win?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여덟 군데: Part A 3개(`m_clock`, `m_setup_bit`, `m_sample`), Part B 4개(`mon_step`, `mon_free`, `m_arb_lost`, `m_give_up`), Part C 1개(`i2c_mm_xfer`). 버스 해석(wired-AND), 슬레이브(B06 축약판), 요청 큐, `m_begin_attempt`/`m_finish`, 고장 노드, 트레이스·로그, xorshift32 는 주어진다. 스텁 상태에서는 SCL 이 전혀 움직이지 않으므로 마스터가 START 에서 멈추고, 모든 시뮬레이션 루프는 틱 상한(`run_until_idle(b, cap)`)에 걸려 끝난다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:458  run_until_idle(&b, 5000)
FAIL starter.c:459  A.last.status == I2C_OK && A.last.attempts == 1 && A.last.n_lost == 0
…
random 3-master: 400 rounds, 0 messages, 0 arbitration losses, 0 merged identical reads, bad=10
…
FAIL starter.c:765  i2c_mm_xfer(&b, &B, 0x68, false, w11_22, NULL, 2, &info) == -LNX_ETIMEDOUT
4/70 passed
```

(exit 1, 약 0.1 초. 통과한 4개는 스텁이 우연히 맞는 것: `m_submit` 성공, 스텁이 `-EINVAL` 을 돌려주는 인자 오류 2개, "끝나고 마스터 선이 놓여 있음".)

## 4. 힌트

<details><summary>힌트 1 — 중재 비교는 한 줄이다</summary>

샘플 틱에 `내가_내는_비트 && 내_SDA_드라이버 == 1 && 버스_SDA == 0` 이면 진 것이다. "내가 내는 비트" 는: 내가 보내는 바이트(주소 바이트, 쓰기 데이터)의 bit 0..7, 그리고 내가 *받는* 바이트(읽기 데이터)의 9번째(ACK) 비트. 보내는 바이트의 ACK 비트와 받는 바이트의 데이터 비트는 슬레이브가 내므로 비교하지 않는다. 0 을 낸 쪽은 비교할 필요가 없다 — 0 을 읽는 건 당연하다.

</details>

<details><summary>힌트 2 — 동기화는 "high 를 세다가 SCL=0 을 보면 즉시 low 로"</summary>

`M_HIGH` 에서 매 틱 `if (!b->scl) → M_LOW` 를 먼저 본다. 이 틱을 low 의 1틱째로 센다(남이 당긴 틱부터 low 시간이 흐른다). 반대로 low 를 다 센 뒤엔 SCL 을 *놓기만* 하고 `M_WAIT_HIGH` 에서 실제로 1 이 읽힐 때까지 기다린다 — 다른 마스터의 low 가 더 길면 여기서 기다리게 되고, 그게 "low = 가장 긴 low" 다. 이 대기는 COMM-M01 클럭 스트레칭 대기와 같은 코드다.

</details>

<details><summary>힌트 3 — 상태 전이 순서: 상승 엣지를 먼저 처리</summary>

`M_WAIT_HIGH` 에서 SCL=1 을 본 틱은 ① 상승 엣지(샘플) 이면서 ② high 1틱째다. 그래서 `m_clock` 은 맨 앞에서 WAIT_HIGH → HIGH 전이 + `m_sample` 을 하고, `m_sample` 이 단계를 바꿨으면(패배 → WAIT_FREE / 종료) 바로 돌아오고, 아니면 이어서 HIGH 카운트 코드를 탄다. STOP 중이면 HIGH 대신 `M_STOP_HIGH`.

</details>

<details><summary>힌트 4 — 버스 모니터의 idle 카운트</summary>

`busy` 는 START 에서 true, STOP 에서 false. `idle` 은 `!busy && SDA && SCL` 인 동안 1씩, 아니면 0 으로. free = `!busy && idle >= t_buf`. STOP 틱 자체가 idle 1 이므로 STOP 틱 s 에서 free 판단은 s + t_buf − 1, 새 START(SDA↓)는 s + t_buf — 테스트가 이 숫자를 본다. SDA 가 고장으로 붙잡혀 있으면 idle 이 안 쌓이므로 `max_wait` 뒤 `BUS_BUSY`.

</details>

<details><summary>힌트 5 — 패배 정책과 재시도는 한 곳에서</summary>

`m_arb_lost`: 기록 → 로그 → SDA 놓기 → (`keep_clock && bit_i < 8`) 이면 `lost = true; bit_i++` 하고 계속 클럭(이후 `m_setup_bit` 은 1, `m_sample` 은 비트만 세다가 9번째에서 `m_give_up`). `m_give_up`: SDA·SCL 놓기 → `attempts > max_retries` 면 `m_finish(ARB_LOST)` → 아니면 `M_WAIT_FREE`. 재시도 횟수는 마스터 상태 머신 *한 곳*에서 세고, `i2c_mm_xfer` 는 결과만 errno 로 바꾼다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 물리 | wired-AND: 0 이 우세(dominant). 1 을 낸 쪽만 충돌을 감지할 수 있다 |
| 2. 클럭 | 마스터마다 자기 SCL — 동기화로 하나의 클럭이 된다 (low = 최장, high = 최단) |
| 3. 중재 | SCL high 에서 SDA 비교, 1 냈는데 0 → 패배. 비교는 주소 → 데이터(송신) / ACK(수신)까지 계속 |
| 4. 진 뒤 | SDA 즉시 놓기, 클럭은 그 바이트 끝까지 낼 수 있음, 슬레이브 기능이 있으면 즉시 슬레이브 모드, STOP + t<sub>BUF</sub> 뒤 재시도 (상한) |
| 5. 경계 | 같은 메시지 = 둘 다 성공, 같은 주소 다른 데이터, ACK 비트 패배, 늦은 START = busy, 금지 조합(STOP vs 데이터) |
| 6. 시스템 | 낮은 주소가 항상 이김 → 굶주림, 드라이버 errno, 재시도 정책 위치, 측정 |

### Part A — 클럭 동기화: 버스 low = 가장 긴 low, high = 가장 짧은 high

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">SCL 동기화: A(t_low 3, t_high 6) + B(t_low 6, t_high 3) — 틱 0..39, 같은 메시지</text>
  <rect x="165" y="140" width="90" height="48" class="f-acc" opacity=".25"/>
  <rect x="255" y="140" width="45" height="48" class="f-ok" opacity=".35"/>
  <g fill="none" stroke="currentColor" stroke-width="2">
    <polyline points="120,48 165,48 165,70 210,70 210,48 315,48 315,70 345,70 345,48 450,48 450,70 480,70 480,48 585,48 585,70 615,70 615,48 720,48"/>
    <polyline points="120,103 165,103 165,125 255,125 255,103 300,103 300,125 390,125 390,103 435,103 435,125 525,125 525,103 570,103 570,125 660,125 660,103 705,103 705,125 720,125"/>
    <polyline points="120,163 165,163 165,185 255,185 255,163 300,163 300,185 390,185 390,163 435,163 435,185 525,185 525,163 570,163 570,185 660,185 660,163 705,163 705,185 720,185"/>
  </g>
  <g fill="currentColor">
    <text x="10" y="64">SCL 드라이버 A</text>
    <text x="10" y="119">SCL 드라이버 B</text>
    <text x="10" y="179">SCL 버스</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="210" y="205">low 6틱 (B 의 low)</text>
    <text x="278" y="152">high 3</text>
    <text x="262" y="40">A 는 놓았지만 버스는 0 → WAIT_HIGH</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="228">① 둘 다 SCL 을 당긴다(틱 3). A 는 3틱 뒤 놓지만 B 가 6틱 당겨</text>
    <text x="10" y="244">    버스는 틱 8 까지 0 — A 는 SCL 을 다시 읽으며 기다린다.</text>
    <text x="10" y="262">② 둘 다 놓은 틱 9 에 동시에 SCL↑ → high 카운트가 같은 틱에 시작.</text>
    <text x="10" y="280">③ B 가 3틱 만에 당김(틱 12). A 는 SCL=0 을 보고 바로 low — 그 틱부터 센다.</text>
    <text x="10" y="298">출처: solution.c 실제 트레이스. 드라이버 줄이 버스보다 1틱 늦은 것은</text>
    <text x="10" y="314">"보고 → 다음 틱에 드라이버" 모델이라서다.</text>
  </g>
</svg>
```

UM10204 의 클럭 동기화 절(3.1.7 근처 — 절 번호 확인 권장) 요지가 이것이다: 처음 SCL 을 당긴 마스터가 모든 마스터의 low 카운트를 시작시키고, low 가 가장 긴 마스터가 놓을 때까지 SCL 은 0, 모두 놓으면 동시에 high 를 세기 시작하고, high 가 가장 짧은 마스터가 다시 당긴다. 그 결과 버스 클럭 = (최장 low) + (최단 high). `demo_arb.c` 측정 (실제 출력):

| 마스터 (t_low, t_high) | 버스 low | 버스 high | 주기 | max(low) | min(high) |
|---|---|---|---|---|---|
| (4,4) (4,4) | 4 | 4 | 8 | 4 | 4 |
| (3,6) (6,3) | 6 | 3 | 9 | 6 | 3 |
| (2,8) (8,2) | 8 | 2 | 10 | 8 | 2 |
| (2,2) (6,6) | 6 | 2 | 8 | 6 | 2 |
| (2,5) (4,2) (3,3) | 4 | 2 | 6 | 4 | 2 |
| (5,1) (2,4) | 5 | 1 | 6 | 5 | 1 |
| (2,1) (2,1) (9,9) | 9 | 1 | 10 | 9 | 1 |

모든 클럭 구간에서 최소=최대(표의 "4..4" 처럼)였다. 주의할 점: (3,6)+(6,3) 은 각자 주기 9 인데 합쳐도 9, (2,2)+(6,6) 은 각자 4·12 인데 합치면 8 — 느린 마스터 혼자보다 **빠르다**. 이것이 실제 버스에서 위험한 이유는 연습 2: 100 kHz 마스터와 400 kHz 마스터가 겹치면 high 가 400 kHz 쪽 값(약 1.2 µs)으로 줄어 Standard-mode 디바이스의 t<sub>HIGH</sub> ≥ 4.0 µs(대표값)를 어긴다. 그래서 멀티마스터 버스는 **가장 느린 디바이스 규격에 모든 마스터의 t_low·t_high 를 맞춘다.**

### Part A — 비트 설정과 샘플: 누가 어느 클럭의 SDA 를 내나

| 클럭 | 마스터 송신(주소, 쓰기 데이터) | 마스터 수신(읽기 데이터) | 중재 비교? |
|---|---|---|---|
| bit 0..7 | 마스터가 비트를 낸다 | 슬레이브가 낸다, 마스터는 놓고 시프트 | 송신일 때만 |
| 9번째 (ACK) | 마스터는 놓고 슬레이브 ACK 를 읽는다 | 마스터가 ACK(0, 더 읽음) / NACK(1, 마지막) | 수신일 때만 |

주소 바이트는 언제나 마스터 송신이므로 첫 바이트에서는 8비트 모두 비교된다. UM10204 는 "같은 디바이스를 부르면 중재는 데이터 비트(마스터-송신자) 또는 ACK 비트(마스터-수신자) 비교로 계속된다" 고 쓴다 (문구 확인 권장) — 위 표가 정확히 그 말이다.

**0x50 vs 0x68 처음 12틱** (solution.c 실제 동작, `t_low = t_high = 2`, `t_hd_sta = 2`):

| 틱 | SCL | SDA A | SDA B | SDA 버스 | 일어난 일 |
|---|---|---|---|---|---|
| 0 | 1 | 1 | 1 | 1 | 둘 다 모니터 free → 같은 틱에 START 결정 (드라이버 SDA=0) |
| 1–2 | 1 | 0 | 0 | 0 | 관찰자·슬레이브: START. 마스터는 hold 2틱 |
| 3–4 | 0 | 0→1 | 0→1 | 0→1 | low 첫 틱(3)에 A6 = 1 설정, 틱 4 에 SCL 놓기 |
| 5 | 1 | 1 | 1 | 1 | SCL↑ — A6 샘플: 둘 다 1 냈고 1 읽음, 통과 |
| 7 | 0 | 0 | 1 | 0 | low 첫 틱: A5 설정 — A 는 0, B 는 1 |
| 9 | 1 | 0 | 1 | **0** | SCL↑ — B: 1 을 냈는데 0 → **ARB_LOST byte0 bit1**, SDA·SCL 놓고 WAIT_FREE |
| 10– | … | A 의 비트 | 1 | A 의 비트 | 버스 = A 혼자 보내는 것과 똑같다 (연습 4 에서 틱 단위로 확인) |

진 쪽이 처음 다른 비트에서 *1* 을 냈다는 것은, 그 비트 앞까지 같은 MSB-first 접두사를 가진 두 바이트 중 진 쪽이 더 크다는 뜻이다. 그래서 **주소 바이트(addr7 &lt;&lt; 1 | R/W)가 작은 쪽이 이긴다** — 같은 주소면 R/W=0(쓰기)이 이긴다. 전수 검증 (실제 출력):

```text
exhaustive address bytes: 65280 pairs with a loser, 256 identical, bad=0
```

256 × 256 = 65536 쌍 중 같은 바이트 256쌍은 아무도 지지 않았고(둘 다 NACK — 슬레이브가 없는 버스), 나머지 65280쌍 모두 큰 쪽이 처음 다른 비트에서 졌다. 두 패배 정책(`keep_clock` on/off)을 번갈아 돌렸고, 모든 경우 버스의 START·STOP 은 1번씩 — 진 시도는 START 를 따로 만들지 않는다.

### Part B — 마스터 상태 머신

```svg
<svg viewBox="0 0 740 370" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m02arr" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="8" markerHeight="8" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">멀티마스터 마스터 상태 머신 (m_step / m_clock / m_sample / m_arb_lost)</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="130" height="34" rx="8" class="f-mut"/>
    <rect x="190" y="40" width="130" height="34" rx="8" class="f-warn"/>
    <rect x="360" y="40" width="130" height="34" rx="8" class="f-acc"/>
    <rect x="580" y="40" width="130" height="34" rx="8" class="f-acc"/>
    <rect x="360" y="150" width="130" height="34" rx="8" class="f-acc"/>
    <rect x="580" y="150" width="130" height="34" rx="8" class="f-acc"/>
    <rect x="580" y="250" width="130" height="34" rx="8" class="f-acc"/>
    <rect x="20" y="250" width="130" height="34" rx="8" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="85" y="62">IDLE</text><text x="255" y="62">WAIT_FREE</text><text x="425" y="62">START_HOLD</text>
    <text x="645" y="62">STOP_HIGH</text><text x="425" y="172">LOW</text><text x="645" y="172">WAIT_HIGH</text>
    <text x="645" y="272">HIGH</text><text x="85" y="272">DONE (m_finish)</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#m02arr)">
    <line x1="150" y1="57" x2="188" y2="57"/>
    <line x1="320" y1="57" x2="358" y2="57"/>
    <line x1="425" y1="74" x2="425" y2="148"/>
    <line x1="490" y1="167" x2="578" y2="167"/>
    <line x1="645" y1="184" x2="645" y2="248"/>
    <path d="M580,262 H440 V186"/>
    <line x1="645" y1="150" x2="645" y2="76"/>
    <path d="M710,57 H728 V340 H85 V286"/>
    <path d="M580,278 H285 V76"/>
    <line x1="285" y1="278" x2="152" y2="278"/>
    <path d="M220,74 V248"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="152" y="50">요청</text>
    <text x="300" y="34">free: SDA↓</text>
    <text x="432" y="110">t_hd_sta 또는 SCL=0</text>
    <text x="498" y="145">t_low: SCL 놓기</text>
    <text x="520" y="214">SCL=1 읽힘 =</text>
    <text x="520" y="230">샘플·중재 비교</text>
    <text x="448" y="246">t_high 또는 SCL=0 (동기화)</text>
    <text x="652" y="114">stopping</text>
    <text x="400" y="334">SDA↑ = STOP → result (OK/NACK)</text>
    <text x="292" y="300">ARB_LOST, 재시도 남음 → STOP + t_buf 대기</text>
    <text x="155" y="296">재시도 소진</text>
    <text x="155" y="310">→ ARB_LOST</text>
    <text x="214" y="160" text-anchor="end">max_wait 초과</text>
    <text x="214" y="176" text-anchor="end">→ BUS_BUSY</text>
  </g>
  <text x="10" y="362" fill="currentColor" font-size="12">keep_clock 정책: 패배 후에도 HIGH↔LOW 를 9번째 클럭까지 돌며 SDA 는 1, 그 다음 ARB_LOST 화살표로.</text>
</svg>
```

**패배 정책.** UM10204 는 진 마스터가 "그 바이트 끝까지 클럭을 낼 수 있다(can)" 고 쓰고(문구 확인 권장), 의무로 두지 않는다. 이 레슨은 두 정책을 플래그로 둔다:

| 정책 | 동작 | 승자에게 미치는 영향 (측정) |
|---|---|---|
| `release_now` (기본) | 진 틱에 SDA·SCL 을 놓고 끝 | 진 다음 클럭부터 승자 혼자의 클럭 |
| `keep_clock` | 9번째 클럭까지 SCL 동기화에 계속 참여, SDA 는 1 | 그 바이트의 low 가 패자 low 만큼 늘어남 |

테스트 7 의 실제 출력: `keep_clock policy: SCL low runs (ticks) clock0..9 = 6 6 6 6 6 6 6 6 6 | 3` — 승자 A 의 low 3 이 9클럭 동안 B 의 6 으로 늘었다가 다음 바이트에서 3 으로 돌아온다. `demo_arb.c` 3) 에서 승자 A(2,2)의 START→STOP 틱 수 (실제 출력): 패자 (2,2) 114 / 114, (6,6) 118 / 150, (12,2) 124 / 204 (`release_now` / `keep_clock`). 클럭을 계속 내는 이점은 하드웨어 단순성(바이트 카운터를 그대로 돌림) 정도이고, 패자가 느릴수록 승자를 늦춘다. 하드웨어 I2C 블록은 대개 패배를 플래그로 알리고 마스터 모드에서 빠진다 (STM32: `ARLO` — 아래 Part C, RM 확인 권장).

**재시도 시점.** 진 마스터의 모니터는 이미 START 를 봤으므로 busy 다. 승자의 STOP(틱 s) 뒤 `t_buf` 틱 idle 이면 free 판단 → 새 START 는 s + `t_buf`. 테스트가 `B.last.start_t == A.last.done_t + t_buf` 로 확인한다. 진 마스터가 **슬레이브 기능도 가진** 경우(멀티마스터 MCU 끼리 서로에게 말하는 구성), 주소 단계에서 졌다면 승자가 *자기를* 부르는 중일 수 있으므로 즉시 슬레이브 모드로 바꿔 주소를 계속 받아야 한다 (UM10204, 확인 권장). 이 레슨의 마스터는 슬레이브 기능이 없어 이 부분은 Q&A 로만 다룬다.

**경계 경우 결과** (전부 solution.c 테스트, 실제 값):

| 경우 | 결과 | 이유 |
|---|---|---|
| 같은 주소 0x50, `11 22` vs `11 2A` | B 가 byte 2 bit 4 에서 패배, 슬레이브 rx = `11 22 11 2A`, START 2·STOP 2 | 0x22 = 0010 0010, 0x2A = 0010 1010 — 처음 다른 비트(값 0x08)에서 B 가 1 |
| 완전히 같은 메시지 (클럭 (2,2)·(3,1)) | 둘 다 OK, 시도 1번씩, 슬레이브 rx 2바이트, 버스 START 1 | 끝까지 같은 비트 → 아무도 1/0 불일치를 못 본다 |
| 읽기 0x48: A 1바이트, B 2바이트 | A 가 byte 1 bit 8(ACK) 에서 패배, B 는 seed, seed+1 을 읽고 A 의 재시도는 seed+2 | A 는 마지막이라 NACK(1), B 는 ACK(0) |
| 0x50 읽기 vs 0x50 쓰기 | 읽기 쪽이 byte 0 bit 7 에서 패배 | R/W: 쓰기 0 이 우세 |
| B 가 1틱 늦게 요청 (B 주소가 더 작아도) | 패배 0번, A 의 STOP + 4틱에 START | B 의 모니터가 틱 1 에 START 를 봤다 → busy |
| A 가 0x10 으로 3개 연속, B → 0x68 | `max_retries=2`: B 는 3번 지고 `ARB_LOST`, `=3`: 4번째에 OK | 매번 둘이 같은 틱에 free 를 보고 동시에 START |

"완전히 같은 메시지" 는 면접에서 함정이다. 두 마스터 모두 성공을 보고받았지만 슬레이브는 **한 번** 받았다. 레지스터 쓰기처럼 멱등(idempotent)이면 괜찮지만, FIFO 푸시·카운터 증가·EEPROM 페이지 쓰기 누적처럼 "두 번 일어나야 하는" 동작이라면 하나가 조용히 사라진다. 읽기도 마찬가지로 둘이 같은 바이트를 받는다 (랜덤 테스트에서 `3 merged identical reads`). 해결은 프로토콜 밖에서: 메시지에 마스터 ID 를 넣거나, 공유 디바이스 접근을 한 마스터로 몰아준다.

**규격상 금지된 조합.** UM10204 는 "repeated START 와 데이터 비트, STOP 과 데이터 비트, repeated START 와 STOP 사이의 중재는 허용되지 않는다" 고 쓴다 (확인 권장). 예: 같은 주소에 A 는 `[11]`, B 는 `[11 00]` → A 가 STOP 을 내려고 SDA 를 놓을 때 B 는 0 데이터 비트를 내고 있다. 결과는 정의되지 않으므로 시스템 설계자가 피해야 한다. 이 레슨의 랜덤 테스트는 첫 데이터 바이트에 마스터 ID 를 넣어 피했고, 연습 1 의 예측기는 이런 쌍을 `-1`(미정)로 분류한다.

**랜덤 결과** (실제 출력):

```text
random 3-master: 400 rounds, 1743 messages, 979 arbitration losses, 3 merged identical reads, bad=0
```

979 번 졌어도 메시지 1743 개가 모두 OK, 슬레이브 수신 바이트는 완료 순서대로 이어 붙인 쓰기와 정확히 같고, 버스의 START 수 = 진 시도를 뺀 트랜잭션 수였다. 중재가 *데이터를 하나도 망가뜨리지 않는다*는 것을 이 불변식들이 보여 준다.

### Part C — 드라이버 API, 로그, 굶주림

`i2c_mm_xfer` 는 요청을 넣고 `n_finished` 가 바뀔 때까지 `bus_step` 을 돈다(다른 마스터·슬레이브도 같이 진행). 상한을 넘으면 `m_finish(TIMEOUT)` 로 선을 놓고 끝낸다 — 상한 없이 기다리면 SCL 을 붙잡는 고장 하나로 펌웨어 태스크가 영원히 멈춘다. 에러 변환은 Linux `Documentation/i2c/fault-codes.rst` 의 관례를 따랐다 (요지만 — 원문 확인 권장):

| 마스터 결과 | 반환 | fault-codes 의 뜻 | 펌웨어가 할 일 |
|---|---|---|---|
| OK | 0 | — | — |
| 인자 오류 | `-EINVAL` (22) | 잘못된 인자 | 호출자 버그 |
| 주소 NACK | `-ENXIO` (6) | 주소 단계 ACK 없음 | 디바이스 없음/리셋 중 |
| 데이터 NACK | `-EIO` (5) | 막연한 I/O 오류 (드라이버마다 `-EREMOTEIO` 를 쓰기도 함) | 디바이스가 거부 |
| 중재 패배, 재시도 소진 | `-EAGAIN` (11) | 어댑터가 중재에 짐 | 나중에 다시 / 상위 재시도 |
| 버스가 free 가 안 됨 | `-EBUSY` (16) | 버스가 허용보다 오래 busy | 복구 필요 (COMM-M03) |
| 트랜잭션 도중 멈춤 | `-ETIMEDOUT` (110) | 너무 오래 걸려 중단 | 스트레칭 한계(COMM-M01) / 버스 복구 |

괄호 숫자는 Linux 값(맥 `<errno.h>` 와 다르므로 코드에 `LNX_*` 상수로 둠). Linux 코어의 `__i2c_transfer()` 는 어댑터가 `-EAGAIN` 을 돌려주면 `adap->retries` 번까지, `adap->timeout` 안에서 다시 시도한다(소스 확인 권장). 즉 **재시도 정책을 어느 한 층에만** 둬야 한다: 이 레슨은 마스터 상태 머신(`max_retries`)에 두고 `i2c_mm_xfer` 는 재시도하지 않는다. 두 층이 다 재시도하면 최악 지연이 곱으로 늘어난다.

**로그** (테스트 시나리오: A → 0x50 `[11 22]`, B → 0x68 `[33]` 같은 틱, 실제 출력):

```text
t=   0 A   START (try 1)
t=   0 B   START (try 1)
t=   1 bus START (SCL=1, SDA fell)
t=   9 B   ARB_LOST byte0 bit1 -> SDA released
t=   9 B   wait bus-free (retry 1/3)
t=  35 s50 addr byte 0xA0 MATCH -> ACK
t=  35 s68 addr byte 0xA0 not me
t=  71 s50 rx 0x11 ACK
t= 107 s50 rx 0x22 ACK
t= 114 A   DONE OK tries=1
t= 115 bus STOP  (SCL=1, SDA rose)
t= 118 B   START (try 2)
t= 119 bus START (SCL=1, SDA fell)
t= 153 s50 addr byte 0xD0 not me
t= 153 s68 addr byte 0xD0 MATCH -> ACK
t= 189 s68 rx 0x33 ACK
t= 196 B   DONE OK tries=2
t= 197 bus STOP  (SCL=1, SDA rose)
```

브링업 엔지니어가 읽을 포인트: ① 버스 START 는 1번인데 마스터 START 는 2번 — 동시 시작 ② B 가 진 틱 9 = 주소 bit 1 의 SCL↑ ③ 슬레이브 0x68 은 첫 트랜잭션에서 주소 0xA0 을 받고 "not me" — 진 마스터의 메시지는 슬레이브에 *아무 흔적도* 남기지 않았다 ④ B 의 재시도 START 는 STOP(115) + t_buf(4) = 119.

**실제 칩에서의 대응** (RM 확인 권장): STM32 의 I2C v2 블록(F0/F3/F7/L4/G4/H7 등)은 `I2C_ISR.ARLO` 가 서고 `I2C_ICR.ARLOCF` 로 지우며, 구형 F1/F4 블록은 `I2C_SR1.ARLO`. 패배하면 하드웨어가 마스터 모드에서 빠지므로 드라이버는 플래그를 지우고 상위로 "다시 시도" 를 올린다 — 이 레슨의 `m_give_up` 이 하는 일이다. 구형 F1 블록의 멀티마스터·BUSY 관련 errata 는 칩별 errata sheet 를 볼 것.

**굶주림 측정** (`demo_arb.c` 2), 실제 출력). A 는 0x10, B 는 0x68 에 2바이트 쓰기를 같은 비율로 무작위 도착시키고, 오프로드 부하 ρ 를 바꿨다 (`t_low = t_high = 2`, 혼자일 때 한 메시지 START→STOP 114틱 + t_buf 4, 2,000,000 틱씩):

| ρ | A 메시지 | A 평균 / p99 / 최대 지연 | B 메시지 | B 평균 / p99 / 최대 지연 | B 패배/메시지 (최대) | 큐 넘침 A/B |
|---|---|---|---|---|---|---|
| 0.20 | 1661 | 128 / 251 / 341 | 1626 | 130 / 317 / 437 | 0.021 (2) | 0/0 |
| 0.40 | 3371 | 143 / 304 / 467 | 3306 | 164 / 548 / 969 | 0.114 (5) | 0/0 |
| 0.60 | 4964 | 163 / 374 / 536 | 4939 | 225 / 913 / 1912 | 0.248 (8) | 0/0 |
| 0.80 | 6538 | 189 / 447 / 677 | 6627 | 420 / 1705 / 2870 | 0.522 (21) | 0/43 |
| 0.90 | 7459 | 205 / 479 / 744 | 7376 | 679 / 2394 / 3535 | 0.724 (21) | 0/223 |
| 0.95 | 7927 | 216 / 532 / 905 | 7463 | 809 / 2834 / 3986 | 0.829 (26) | 2/358 |

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">p99 지연 (틱): 낮은 주소 A vs 높은 주소 B — demo_arb.c 실측</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="50">ρ = 0.20</text><text x="10" y="90">ρ = 0.40</text><text x="10" y="130">ρ = 0.60</text>
    <text x="10" y="170">ρ = 0.80</text><text x="10" y="210">ρ = 0.90</text><text x="10" y="250">ρ = 0.95</text>
  </g>
  <g stroke="currentColor">
    <rect x="120" y="34" width="42.7" height="12" class="f-ok"/><rect x="120" y="48" width="53.9" height="12" class="f-warn"/>
    <rect x="120" y="74" width="51.7" height="12" class="f-ok"/><rect x="120" y="88" width="93.2" height="12" class="f-warn"/>
    <rect x="120" y="114" width="63.6" height="12" class="f-ok"/><rect x="120" y="128" width="155.2" height="12" class="f-warn"/>
    <rect x="120" y="154" width="76.0" height="12" class="f-ok"/><rect x="120" y="168" width="289.9" height="12" class="f-warn"/>
    <rect x="120" y="194" width="81.4" height="12" class="f-ok"/><rect x="120" y="208" width="407.0" height="12" class="f-warn"/>
    <rect x="120" y="234" width="90.4" height="12" class="f-ok"/><rect x="120" y="248" width="481.8" height="12" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="168" y="45">251</text><text x="180" y="59">317</text>
    <text x="177" y="85">304</text><text x="219" y="99">548</text>
    <text x="189" y="125">374</text><text x="281" y="139">913</text>
    <text x="202" y="165">447</text><text x="416" y="179">1705</text>
    <text x="207" y="205">479</text><text x="533" y="219">2394</text>
    <text x="216" y="245">532</text><text x="608" y="259">2834</text>
  </g>
  <rect x="120" y="270" width="14" height="10" class="f-ok" stroke="currentColor"/>
  <text x="140" y="280" fill="currentColor" font-size="12">A (0x10, 한 번도 안 짐)</text>
  <rect x="320" y="270" width="14" height="10" class="f-warn" stroke="currentColor"/>
  <text x="340" y="280" fill="currentColor" font-size="12">B (0x68) — 막대 길이 = p99 × 0.17 px</text>
  <text x="10" y="298" fill="currentColor" font-size="12">같은 도착률인데 ρ = 0.9 에서 B 의 p99 는 A 의 5배. 한 메시지는 혼자 118틱 (START→STOP 114 + t_buf 4).</text>
</svg>
```

읽는 법: 버스가 끝날 때 기다리던 마스터들은 **모두 같은 틱에** free 를 보고 동시에 START 하므로, 기다림이 겹칠 때마다 중재가 일어나고 그때마다 A(낮은 주소)가 이긴다. 부하가 낮으면 겹침이 드물어 차이가 작지만(ρ 0.2: p99 251 vs 317), 부하가 높으면 B 는 "A 가 쉬는 틈" 을 기다려야 한다 (ρ 0.95: 메시지당 평균 0.83번 패배, 최대 26번 연속 패배, 큐 넘침 358). I2C 에는 공정성 장치가 없다 — 우선순위는 **주소로 고정**된다(CAN 의 ID 우선순위와 같은 구조, COMM-M05/M08 의 응답 시간 분석). 펌웨어가 할 수 있는 것: 낮은 주소 쪽 마스터가 연속 메시지 사이에 `t_buf` 보다 조금 더 쉬게 하기(연습 3: 1틱만 양보해도 B 는 다음 기회에 이김), 공유 디바이스를 한 마스터가 대리 접근, 메시지 비율 제한.

### 모델이 빼먹은 것

| 실제 | 이 모델 | 어디서 |
|---|---|---|
| 1틱 = 마스터마다 다른 진동자, 지터 | 공통 틱 | — |
| START 동시성 창: 실제로는 t<sub>HD;STA</sub> 안에 두 START 가 겹치면 둘 다 START 를 냈다고 믿음 | 같은 틱 결정만 동시 | Q4 |
| 슬레이브 겸 마스터: 주소 단계에서 지면 즉시 슬레이브 모드 | 마스터 전용 | Q3 |
| 슬레이브 클럭 스트레칭 | 슬레이브는 SCL 을 안 잡음 (WAIT_HIGH 코드는 같다) | COMM-M01 |
| rise time · 스파이크 필터 · 레벨 | 0/1 즉시 | HWLAB-B03 |
| 금지 조합(Sr/P vs 데이터) | 테스트에서 피함 | 위 문단 |
| 10비트 주소 중재 (두 번째 주소 바이트까지 비교) | 7비트만 | Q8 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 70/70 passed, exit 0 (약 0.1 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
cc -std=c11 -O2 -Wall -Wextra demo_arb.c -o demo_arb && ./demo_arb                 # 5. 풀이의 표 3개 (약 0.2 초)
```

## 7. 자주 하는 실수

**1. SCL 을 놓은 뒤 "high 가 됐겠지" 하고 high 를 센다.** 다른 마스터(또는 스트레칭하는 슬레이브)가 아직 SCL 을 잡고 있으면 이 마스터는 버스가 low 인 동안 high 를 세고, 상대보다 먼저 다음 비트로 넘어가 SDA 를 바꾼다 — SCL=1 이 되는 순간 이미 다른 비트가 나가 있거나, 상대가 SCL high 중 SDA 변화(가짜 START/STOP)를 본다. 고친 코드: `M_WAIT_HIGH` 에서 `if (!b->scl) return;` — 놓은 뒤 **읽어서** 확인 (COMM-M01 과 같은 줄).

**2. high 도중 SCL=0 을 무시하고 자기 t_high 를 끝까지 센다.** 그러면 자기 low 카운트가 늦게 시작해 다음 low 를 혼자 길게 끌고, 동기화가 "low = 최장" 보다 길어진다(최악엔 위상이 계속 밀림). 고친 코드: `case M_HIGH: if (!b->scl) { m->ph = M_LOW; m->cnt = 0; break; }` — 그 틱부터 low 를 센다.

**3. 진 뒤 SDA 를 놓지 않고 자기 메시지를 계속 보낸다.** 진 비트에선 이미 1(놓음)이라 티가 안 나지만, 다음 비트가 0 이면 승자의 1 비트를 덮어써 승자의 주소/데이터를 망가뜨린다. 승자 쪽에선 "1 을 냈는데 0" 이라 **승자도 진다** — 둘 다 물러나 버스엔 쓰레기 바이트만 남는다. 고친 코드: `m_arb_lost` 에서 `b->sda_drv[node] = 1` + `lost = true` 이면 `m_setup_bit` 이 항상 1.

**4. 수신 중인 데이터 비트를 중재 비교한다.** 읽기에서 마스터는 데이터 비트 동안 SDA 를 놓고(1) 슬레이브의 0 을 읽는다. 이걸 비교하면 슬레이브가 0 을 보낼 때마다 "패배" 로 오판한다. 반대로 **ACK 비트(마스터가 내는)** 는 비교해야 한다 — 안 하면 테스트 3 에서 둘 다 "성공" 이라고 믿지만 한쪽(1바이트만 원한 A)은 STOP 을 못 낸다. 고친 코드: `mine = (bit_i < 8) == tx`.

**5. 재시도를 바로 한다 (STOP 을 안 기다림).** 진 직후 START 를 내면 버스는 busy 이고, SCL=1 구간이 오는 즉시 SDA↓ 를 내면 승자의 트랜잭션 한가운데 가짜 START 가 된다. 고친 코드: `M_WAIT_FREE` + 모니터 `!busy && idle >= t_buf`.

**6. 재시도를 두 층에서 한다.** 마스터가 3번, 드라이버가 3번, 상위 서비스가 3번 → 최악 27번 + 각 대기. 실시간 예산을 계산할 수 없게 된다. 한 층에만 두고 나머지는 결과(`-EAGAIN`)만 올린다.

**7. "늦게 온 쪽이 지는 것" 을 중재로 착각한다.** 1틱 늦은 B 는 중재로 진 게 아니라 START 를 *보고* 기다린 것이다 (테스트 5: B 주소가 더 작아도 패배 0번). 중재는 둘 다 버스가 free 라고 믿고 START 를 낸 경우에만 일어난다. 로그에서 `ARB_LOST` 가 없는데 지연이 크면 원인은 busy 대기다.

## 8. 면접 Q&A

<details><summary>Q1. 두 마스터가 동시에 START 하면 누가 이기나요? 왜 데이터가 안 깨지나요?</summary>

SCL high 마다 각 마스터가 자기가 낸 SDA 와 버스 SDA 를 비교한다. wired-AND 라 0 이 우세하므로, 처음 다른 비트에서 1 을 낸 쪽이 0 을 읽고 진다. MSB first 라 "처음 다른 비트에서 0" = 주소 바이트(addr7 &lt;&lt; 1 | R/W)가 작은 쪽이다 (이 레슨 256×256 전수 확인). 진 쪽이 그 비트 앞까지 낸 비트는 승자와 같았고, 진 뒤로는 SDA 를 놓으므로 버스엔 승자의 메시지만 남는다 — 연습 4 에서 충돌 트레이스가 승자 혼자의 트레이스와 틱 단위로 같음을 확인했다. 꼬리: "SCL 도 둘이 내는데 그건 중재 안 하나요?" — SCL 은 중재가 아니라 동기화(wired-AND 로 합쳐짐)이고, 데이터 판정은 SDA 로만 한다.

</details>

<details><summary>Q2. 같은 주소에 다른 데이터를 쓰면? 완전히 같은 메시지면?</summary>

주소가 같으면 둘 다 슬레이브 ACK 를 받고 데이터 비트에서 비교가 계속된다 — `11 22` vs `11 2A` 는 두 번째 데이터 바이트 bit 4 에서 갈렸다. 읽기라면 데이터는 슬레이브가 내므로 비교할 것이 없고, 마스터가 내는 ACK/NACK 비트에서 갈린다 (짧게 읽는 쪽의 NACK 이 짐). 완전히 같은 메시지는 아무도 지지 않고 **둘 다 성공**을 보고받지만 슬레이브는 한 번만 받는다. 멱등 쓰기면 무해, FIFO/카운터/누적형 명령이면 하나가 조용히 사라진다. 그래서 공유 디바이스에 "같은 명령을 동시에" 보낼 수 있는 설계라면 메시지에 출처를 넣거나 접근을 한 마스터로 모은다.

</details>

<details><summary>Q3. 진 마스터는 정확히 무엇을 하나요? 그 MCU 가 슬레이브 주소도 갖고 있다면?</summary>

① 진 그 순간 SDA 를 놓는다(이후 0 비트를 내면 승자를 망가뜨림) ② 클럭은 그 바이트 끝까지 낼 수도 있고 바로 놓을 수도 있다 — UM10204 는 "낼 수 있다" 로 허용 (이 레슨 측정: 느린 패자가 계속 내면 승자의 그 바이트가 느려짐) ③ 주소 단계에서 졌고 자기도 슬레이브 주소가 있으면 **즉시 슬레이브 모드로** — 승자가 바로 자기를 부르는 중일 수 있다 (MCU 두 개가 서로 I2C 로 말하는 구성에서 실제로 일어난다; STM32 하드웨어도 패배 시 슬레이브 모드로 돌아간다고 RM 에 있음 — 확인 권장) ④ STOP 을 보고 버스 free 시간이 지나면 처음부터 다시(START 부터) — 진 바이트에서 이어 보내는 게 아니다 ⑤ 재시도 상한 후 `-EAGAIN`.

</details>

<details><summary>Q4. 한 마스터가 아주 조금 늦게 START 하면 어떻게 되나요?</summary>

상대의 START(SCL=1 중 SDA↓)를 먼저 *봤다면* 버스 busy 로 판단해 기다린다 — 중재 없음 (테스트 5: 1틱 늦은 B 는 주소가 더 작아도 STOP + t_buf 뒤에 시작). 실제 하드웨어에서는 START 를 내기 직전에 버스를 확인하므로, 두 마스터의 START 가 START hold 시간 t<sub>HD;STA</sub> 안쪽으로 겹치면 둘 다 "내가 START 했다" 고 믿는다. 이 경우엔 SCL 동기화가 둘의 첫 클럭을 맞추고 중재로 정리된다 (UM10204 의 동기화 그림이 정확히 이 상황 — 확인 권장). 모델의 "같은 틱 결정" 이 이 창을 대표한다.

</details>

<details><summary>Q5. 클럭 속도가 다른 마스터가 섞이면 버스 클럭은 어떻게 되나요? 문제는?</summary>

low = 가장 긴 low, high = 가장 짧은 high (이 레슨 측정 표: (2,2)+(6,6) → 6+2). 그래서 합쳐진 클럭은 "느린 쪽보다 빠를 수 있다". 400 kHz 마스터(t<sub>HIGH</sub> 약 1.2 µs)와 100 kHz 마스터(t<sub>LOW</sub> 약 5 µs)가 겹치면 low 5000 ns + high 1200 ns = 약 161 kHz 인데, high 1.2 µs 는 Standard-mode 의 t<sub>HIGH</sub> ≥ 4.0 µs 를 어긴다 (대표값, UM10204 표 확인 권장; 연습 2 계산). 결론: 멀티마스터 버스의 모든 마스터는 그 버스에서 가장 느린 디바이스의 Standard/Fast 규격에 맞는 t_low·t_high 로 설정한다.

</details>

<details><summary>Q6. 드라이버는 중재 패배를 어떻게 올리고, 재시도는 어디서 하나요?</summary>

하드웨어(STM32 `ARLO`, RM 확인 권장)나 비트뱅 루틴이 패배를 감지 → 마스터 모드에서 빠지고 플래그 정리 → 드라이버는 Linux 관례로 `-EAGAIN` (fault-codes: "중재에 졌다"). Linux i2c 코어는 `-EAGAIN` 이면 `adap->retries` 횟수·`adap->timeout` 안에서 자동 재시도한다(소스 확인 권장). 원칙은 **재시도를 한 층에만**: 이 레슨은 마스터 상태 머신의 `max_retries` 에 두고, `i2c_mm_xfer` 는 결과만 errno 로 바꾼다. 꼬리: "`-EAGAIN` 을 받은 앱은?" — 데이터가 아직 유효한 요청이면 나중에 재시도, 제어 루프 샘플처럼 시간이 지나면 의미 없는 것은 버린다.

</details>

<details><summary>Q7. 우선순위가 주소로 고정되면 무슨 문제가 생기나요? 어떻게 완화하나요?</summary>

동시에 기다린 마스터 중 낮은 주소(로 가는 메시지)가 항상 이기므로, 높은 주소 쪽은 버스가 바쁠수록 굶는다. 이 레슨 측정: 같은 도착률에서 ρ = 0.9 일 때 A 의 p99 479틱, B 의 p99 2394틱(5배), B 는 최대 21번 연속 패배. 완화: 낮은 주소 마스터가 연속 메시지 사이에 t<sub>BUF</sub> 보다 조금 더 쉰다(연습 3: 1틱 양보로 B 가 다음 기회에 이김), 공유 디바이스 접근을 한 마스터로 몰고 다른 MCU 는 메일박스로 요청, 메시지 비율 제한, 최악 응답 시간을 버스 부하로 계산(CAN 의 응답 시간 분석과 같은 방식 — COMM-M05/M08). 근본적으로 공정한 버스가 필요하면 I2C 멀티마스터 대신 다른 구조를 쓴다.

</details>

<details><summary>Q8. 10비트 주소나 SMBus 에서는 중재가 어떻게 다른가요?</summary>

10비트 주소는 첫 바이트가 `11110 A9 A8 R/W` 이고 두 번째 바이트가 A7..A0 이다. 같은 상위 2비트를 가진 두 마스터는 첫 바이트가 같으므로 두 번째 주소 바이트에서 중재가 계속된다 — 원리는 같다(마스터가 내는 비트를 비교). SMBus 는 I2C 의 중재·동기화를 그대로 쓰되, 클럭 low 타임아웃(25–35 ms, 대표값)이 있어 버스를 오래 잡은 디바이스가 스스로 놓게 하고, 호스트-알림(Host Notify)처럼 슬레이브가 마스터가 되는 프로토콜이 있어 멀티마스터가 기본 전제다 (SMBus 규격 확인 권장). 그래서 서버 보드의 BMC 쪽 SMBus 드라이버는 `-EAGAIN` 을 일상적으로 처리한다.

</details>

## 9. 추가 연습

### 연습 1 — 시뮬레이션 없이 승자 예측

`arb_predict(x, y, &byte, &bit)` 를 짜라. 두 메시지(주소, R/W, 길이, 쓰기 데이터)가 같은 틱에 시작하고 슬레이브가 모든 바이트를 ACK 한다고 가정한다. 반환: 0 = x 가 짐, 1 = y 가 짐, 2 = 아무도 안 짐, −1 = 규격상 미정(한쪽이 STOP 하려는데 다른 쪽은 계속 보냄). 랜덤 3000쌍(주소·데이터가 자주 겹치게)으로 시뮬레이션과 대조하라.

<details><summary>답 보기</summary>

핵심은 "각 (바이트, 비트) 위치에서 마스터가 SDA 를 *내는지*" 함수 하나다: 보내는 바이트의 bit 0..7 → 그 비트, 받는 바이트의 bit 8 → 마지막이면 1(NACK) 아니면 0(ACK), 나머지 → 안 냄. 두 마스터가 둘 다 내고 값이 다른 첫 위치에서 1 을 낸 쪽이 진다. 바이트 인덱스가 한쪽 길이를 넘으면(그쪽은 STOP 을 내려 함) −1. 이 정의로 실제 출력 `ex1: 2985 simulated pairs agree with arb_predict (bad=0), 15 undefined prefix pairs skipped` — 패배 위치(byte, bit)까지 전부 일치. 면접에서 "누가 이기나" 를 물으면 이 함수를 말로 설명하면 된다.

</details>

### 연습 2 — 동기화 공식과 실제 시간

① 마스터 2~3개의 (t_low, t_high) 를 무작위로 골라 300번 시뮬레이션하고, 모든 클럭의 버스 low = max(t_low), high = min(t_high) 임을 확인하라. ② 400 kHz 마스터(t_LOW 1300 ns, t_HIGH 1200 ns 로 설정)와 100 kHz 마스터(5000 ns / 5000 ns)가 동기화되면 버스 주기와 주파수는? Standard-mode t<sub>HIGH</sub> ≥ 4000 ns 를 지키나?

<details><summary>답 보기</summary>

① 300번 모두 일치 (CHECK). ② low = max(1300, 5000) = 5000 ns, high = min(1200, 5000) = 1200 ns, 주기 6200 ns → 1e9 / 6200 = 161290 Hz (정수 나눗셈, 실제 출력 `period 6200 ns = 161290 Hz; Sm tHIGH>=4000 ns? NO`). high 1200 ns 는 Fast-mode 최소(600 ns, 대표값)는 넘지만 Standard-mode 최소(4000 ns)는 못 넘는다 — 그 버스에 Standard-mode 전용 디바이스가 있으면 규격 위반이다. 규격 숫자는 UM10204 표로 확인할 것.

</details>

### 연습 3 — 양보 간격으로 굶주림 풀기

마스터에 `gap` 필드를 추가해, *자기 다음 메시지*의 첫 시도에서는 버스 free 조건을 `t_buf + gap` 로 둔다(재시도는 그대로 `t_buf`). X 가 0x10 으로 5개를 연달아 보내는 동안 Y 가 `i2c_mm_xfer` 로 0x68 에 한 개를 보낼 때, gap = 0, 1, 4 에서 Y 의 시도 횟수와 지연을 재라.

<details><summary>답 보기</summary>

`m_step` 의 free 조건 한 줄: `mon_free(&m->mon, m->t_buf + (m->attempts == 0 ? m->gap : 0))`. 실제 출력 `ex3: Y attempts / latency with X gap 0,1,4 = 6/489, 2/161, 2/161 ticks`. gap 0 이면 X 의 매 STOP 뒤 X 와 Y 가 같은 틱에 START → Y 는 X 의 5개 내내 지고 6번째에 성공. gap 1 이면 Y 의 재시도가 X 보다 1틱 먼저 START 를 내므로 X 는 busy 를 보고 기다린다 — 중재조차 일어나지 않는다. 비용은 X 의 메시지당 1틱. Y 의 첫 시도는 여전히 진다(둘 다 처음엔 동시에 시작) — gap 은 "연속 전송하는 쪽" 의 양보이고, 그래서 주로 대량 전송을 하는 마스터에 둔다.

</details>

### 연습 4 — 승자에게 중재는 무손실

B06 연습 1 같은 트레이스 디코더 `decode_trace(tr, from, to, out, cap)` 로 ① X 혼자 0x50 에 `11 22` ② 같은 속도 Y 가 0x68 로 충돌(Y 는 `keep_clock`) ③ 느린 Y(12, 2) 가 충돌 — 세 트레이스를 디코드해 비교하라. ①과 ②는 틱 단위로도 같은가?

<details><summary>답 보기</summary>

실제 출력:

```text
ex4: solo  : S A0 A 11 A 22 A P
     collide: S A0 A 11 A 22 A P
     slow Y : S A0 A 11 A 22 A P (A STOP at tick 205 vs 115 solo)
```

①과 ②는 SDA·SCL 트레이스가 STOP 까지 **틱 단위로 같다**(memcmp CHECK) — 같은 속도의 패자는 타이밍조차 바꾸지 않는다. ③은 디코드 결과는 같지만, 패자가 그 바이트 9클럭 동안 low 12틱을 강요해 STOP 이 90틱(= 9 × (12 − 2)) 늦다. 즉 중재는 승자의 *데이터*에는 무손실이지만 *시간*에는 패자 클럭만큼 비용이 있을 수 있다 — 5. 풀이의 정책 표와 같은 결론. 디코더는 출력 용량이 모자라면 −1 (CHECK).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 20/20 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o pr_asan && ./pr_asan   # 오류 없음
```

## 10. 핵심 정리

- wired-AND 에서 0 이 우세: **1 을 냈는데 0 을 읽은 마스터가 진다.** 0 을 낸 쪽은 충돌을 모른다 → 승자의 메시지는 그대로 (비파괴 중재).
- 비교 시점 = SCL high(샘플). 비교 대상 = 내가 내는 비트: 주소·쓰기 데이터 bit 0..7, 읽기의 ACK/NACK 비트. 슬레이브가 내는 비트는 비교하지 않는다.
- 결과: 주소 바이트(addr7 &lt;&lt; 1 | R/W)가 작은 쪽이 이긴다(256×256 전수). 같은 주소면 쓰기가 이기고, 데이터·ACK 비트에서 계속 갈린다. 완전히 같은 메시지 = 둘 다 성공, 슬레이브는 한 번.
- SCL 동기화: 버스 low = 최장 low, high = 최단 high. SCL 은 놓은 뒤 **읽어서** high 확인, high 중 0 을 보면 즉시 low. 섞인 속도는 느린 디바이스의 t<sub>HIGH</sub> 를 어길 수 있다.
- 진 뒤: SDA 즉시 놓기, 클럭은 바이트 끝까지 내도 되고(승자를 늦출 수 있음) 바로 놓아도 됨, 슬레이브 기능이 있으면 즉시 슬레이브 모드, STOP + t<sub>BUF</sub> 뒤 처음부터 재시도, 상한 후 `-EAGAIN`.
- 늦게 온 START 는 중재가 아니라 busy 감지. 금지 조합(STOP/Sr vs 데이터 비트)은 설계로 피한다.
- 우선순위는 주소로 고정 → 부하가 높으면 높은 주소 마스터가 굶는다 (ρ 0.9: p99 5배, 측정). 연속 전송 쪽의 1틱 양보, 접근 일원화, 재시도는 한 층에만.

## 참고자료

- NXP, *UM10204 I2C-bus specification and user manual* (Rev. 7, 2021) — 3.1.7 Clock synchronization, 3.1.8 Arbitration (절 번호 기억 기반, 확인 권장): 진 마스터의 "바이트 끝까지 클럭" 허용, 슬레이브 모드 전환, 금지 조합, 데이터/ACK 비트로 계속되는 중재; 타이밍 표(t<sub>LOW</sub>, t<sub>HIGH</sub>, t<sub>BUF</sub>, t<sub>HD;STA</sub>): <https://www.nxp.com/docs/en/user-guide/UM10204.pdf>
- Linux kernel, `Documentation/i2c/fault-codes.rst` — EAGAIN(중재 패배), ENXIO(주소 NACK), EBUSY, ETIMEDOUT, EIO: <https://docs.kernel.org/i2c/fault-codes.html>
- Linux kernel, `drivers/i2c/i2c-core-base.c` 의 `__i2c_transfer()` — `-EAGAIN` 시 `adap->retries` / `adap->timeout` 재시도 루프 (소스 확인 권장): <https://github.com/torvalds/linux/blob/master/drivers/i2c/i2c-core-base.c>
- ST, STM32 Reference Manual I2C 장 (v2 블록: `I2C_ISR.ARLO`, `I2C_ICR.ARLOCF`; F1/F4 블록: `I2C_SR1.ARLO`) — 비트 위치·동작은 해당 RM 확인 권장
- SMBus Specification 3.x — 클럭 low 타임아웃, Host Notify (멀티마스터 전제): <http://smbus.org/specs/>
- K. Tindell, A. Burns, A. Wellings, "Calculating Controller Area Network (CAN) Message Response Times" (1995) — 고정 우선순위 버스의 응답 시간 분석 (Q7 의 같은 구조)
- 관련 레슨: COMM-B06 (I2C 비트 수준 모델, 이 레슨 슬레이브의 원본), COMM-B07 (repeated START), COMM-M01 (클럭 스트레칭 — 같은 WAIT_HIGH), COMM-M03 (SDA stuck-low 버스 복구 — `-EBUSY` 다음 단계), COMM-M05 (CAN 비트 중재 — 같은 wired-AND), COMM-M08 (버스 부하), COMM-M12 (드라이버 계층과 에러 코드 변환), HWLAB-B03 (풀업·rise time)
