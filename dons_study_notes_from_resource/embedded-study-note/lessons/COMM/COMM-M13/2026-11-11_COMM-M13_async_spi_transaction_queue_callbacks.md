---
id: COMM-M13
time: 90분
updated: 2026-11-11
---
# COMM-M13 공유 버스 트랜잭션 큐: 비동기 요청과 완료 콜백

## 학습 목표

- 트랜잭션 디스크립터(transaction descriptor: CS, tx/rx 버퍼, 길이, 타임아웃, 콜백, ctx)와 **고정 용량 링 큐**로 비동기 SPI 드라이버를 짠다. 큐가 차면 `SPI_E_QFULL`, malloc 없음, 디스크립터 메모리는 호출자 소유.
- 완료 인터럽트(ISR)가 **"완료 플래그 확인 → CS↑ → 다음 것 시작 → 끝난 것의 콜백"** 순서로 버스를 쉬지 않고 돌리게 하고, 그 순서가 왜 그런지 (버스 공백, 콜백 안 재제출, 가짜 IRQ) 설명할 수 있다.
- 취소(대기 중 / 실행 중 / 완료 IRQ 가 이미 떠 있는 실행 중), 타임아웃 감시(tick 랩어라운드 포함), 전체 취소(flush)를 **"제출 1번 = 콜백 정확히 1번"** 을 지키며 구현한다.
- 태스크·콜백·ISR 이 같은 큐를 만질 때 무엇을 인터럽트 마스크(크리티컬 섹션)로 감싸고 무엇을 밖에 둘지 정하고, 빠뜨리면 무엇이 깨지는지 선점 주입 실험으로 보인다.
- 우선순위 2단계(IMU 1 kHz vs 플래시 대용량)의 지연 차이를 시뮬레이션으로 재고, "비선점 버스에서 최악 지연 = 실행 중인 전송 1개 + 자기 자신" 을 공식과 측정으로 맞춘다.

## 1. 문제

센서 퓨전 보드에서는 SPI 버스 하나에 IMU·플래시·ADC·디스플레이가 같이 붙고, 1 kHz 제어 루프·로깅 태스크·DMA 완료 ISR 이 동시에 그 버스를 쓰려고 한다. 면접에서는 "한 SPI 버스를 여러 태스크가 쓰면 어떻게 하나?" → "콜백은 어떤 문맥에서 도나?" → "실행 중인 전송을 취소하면?" → "IMU 지연이 왜 튀나?" 로 이어지고, bring-up 에서는 "가끔 CS 두 개가 동시에 내려간다", "가끔 콜백이 안 온다/두 번 온다" 같은 버그가 바로 이 계층에서 나온다.

COMM-B05 는 "트랜잭션 하나 = CS 프레임 하나"를 블로킹 드라이버로 짰다. 이 레슨은 그 트랜잭션을 **디스크립터로 만들어 큐에 넣고**, 하드웨어 완료 인터럽트가 다음 것을 자동으로 시작하는 비동기 드라이버를 짠다 (Linux `spi_async()`, Zephyr `spi_transceive_cb()` 와 같은 모양 — 8장 Q8).

| 이 레슨의 정의 | 내용 |
|---|---|
| 버스 | CS0..CS3 (active-low, 모델에서는 `cs_mask` 비트 = 활성). 한 번에 CS 하나만 내려가야 한다 |
| 디스크립터 `SpiXfer` | 호출자가 `cs, prio(0 높음/1 낮음), len(1..1024), tx(NULL = 0xFF 송신), rx(NULL = 버림), timeout(≥1 tick), cb, ctx` 를 채운다. 드라이버는 `state, seq` 를 쓴다 |
| 큐 | 우선순위별 링 버퍼 8칸 (포인터만 저장). 실행 중 1개는 큐 밖 `active`. 그래서 버스가 바쁠 때 받는 최대 개수 = 1 + 8 (우선순위별) |
| mock SPI | 시작 후 준비 2 tick + 바이트당 1 tick 뒤 완료 플래그 + IRQ. 1 tick = 1 µs 로 읽으면 SCLK 8 MHz 의 1 바이트 = 1 µs. rx 는 바이트마다 채워진다 (DMA 처럼 — 중단되면 일부만) |
| 반환 코드 | `SPI_OK 0, E_ARG −1, E_BUSY −2 (이미 제출됨), E_QFULL −3, E_NOTFOUND −4, E_STOPPED −5 (flush 중)` |
| 콜백 status | `DONE 0, CANCELLED 1, TIMEOUT 2`. 제출이 받아들여진 디스크립터는 **정확히 한 번** 콜백을 받는다 |
| 정책 | 우선순위는 **비선점**(실행 중인 전송은 끝까지), 같은 우선순위는 FIFO. 실행 중 취소 = **중단 + CS 해제**(다른 정책은 연습 2). 타임아웃은 **전송 시작부터** 잰다 (대기 시간 데드라인은 연습 3) |

아래는 `solution.c` 의 타임라인 테스트가 실제로 남긴 로그를 그대로 그린 것이다 (L1 cs1 12 B·L2 cs2 8 B 제출 → t=5 에 H cs0 4 B(높음)와 L3 cs3 6 B → t=8 에 L3 취소).

```svg
<svg viewBox="0 0 760 390" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="16" fill="currentColor" font-weight="bold">한 SPI 버스, 네 개의 CS — solution.c 타임라인 로그 (1 tick = 1 µs)</text>
  <text x="10" y="54" fill="currentColor" font-size="12">태스크</text>
  <path d="M110 26 V42 M205 42 V58 M262 58 V74" stroke="currentColor" stroke-width="2"/>
  <text x="114" y="38" fill="currentColor" font-size="12">L1, L2 제출</text>
  <text x="209" y="54" fill="currentColor" font-size="12">H(높음), L3 제출</text>
  <text x="266" y="70" fill="currentColor" font-size="12">L3 취소 → 즉시 cb(CANCELLED)</text>
  <text x="10" y="96" fill="currentColor">CS0</text>
  <rect x="376" y="80" width="114" height="24" class="f-acc"/>
  <path d="M90 80 H376 V104 H490 V80 H730" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="433" y="97" fill="currentColor" text-anchor="middle" font-size="12">H 4B</text>
  <text x="10" y="146" fill="currentColor">CS1</text>
  <rect x="110" y="130" width="266" height="24" class="f-ok"/>
  <path d="M90 130 H110 V154 H376 V130 H730" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="243" y="147" fill="currentColor" text-anchor="middle" font-size="12">L1 12B (준비 2 + 12)</text>
  <text x="10" y="196" fill="currentColor">CS2</text>
  <rect x="490" y="180" width="190" height="24" class="f-ok"/>
  <path d="M90 180 H490 V204 H680 V180 H730" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="585" y="197" fill="currentColor" text-anchor="middle" font-size="12">L2 8B</text>
  <text x="10" y="246" fill="currentColor">CS3</text>
  <path d="M90 230 H730" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="300" y="250" fill="currentColor" font-size="12">L3: 큐에서 빠짐 — CS3 은 한 번도 안 내려감</text>
  <text x="10" y="296" fill="currentColor">IRQ</text>
  <path d="M90 300 H730" stroke="currentColor" fill="none" stroke-width="1"/>
  <path d="M376 300 V278 M490 300 V278 M680 300 V278" stroke="currentColor" stroke-width="2"/>
  <text x="380" y="276" fill="currentColor" font-size="12">L1 완료 → H 시작</text>
  <text x="494" y="292" fill="currentColor" font-size="12">H 완료 → L2 시작</text>
  <text x="676" y="276" fill="currentColor" font-size="12" text-anchor="end">L2 완료</text>
  <path d="M110 320 H690" stroke="currentColor" fill="none" opacity=".6"/>
  <path d="M110 316 V324 M205 316 V324 M262 316 V324 M376 316 V324 M490 316 V324 M680 316 V324" stroke="currentColor"/>
  <text x="110" y="338" fill="currentColor" font-size="12" text-anchor="middle">0</text>
  <text x="205" y="338" fill="currentColor" font-size="12" text-anchor="middle">5</text>
  <text x="262" y="338" fill="currentColor" font-size="12" text-anchor="middle">8</text>
  <text x="376" y="338" fill="currentColor" font-size="12" text-anchor="middle">14</text>
  <text x="490" y="338" fill="currentColor" font-size="12" text-anchor="middle">20</text>
  <text x="680" y="338" fill="currentColor" font-size="12" text-anchor="middle">30 tick</text>
  <text x="10" y="362" fill="currentColor" font-size="12">L2 가 H 보다 먼저 왔지만 H(높음)가 먼저 나간다. 하지만 실행 중인 L1 은 끊지 않는다(비선점).</text>
  <text x="10" y="380" fill="currentColor" font-size="12">완료 IRQ 하나가 "CS↑ + 다음 CS↓ + 시작"을 같은 tick 에 한다 → 전송 사이 버스 공백 0.</text>
</svg>
```

### Part A — 제출, 자동 연속 시작, 우선순위

- `spi_start_next(b)` — 버스가 비었으면 높은 우선순위 큐부터 맨 앞을 꺼내 `active` 로, CS↓ → `hw_start`. (마스크 안 또는 ISR 에서만 불린다.)
- `spi_submit(b, x)` — 인자 검사(`E_ARG`, 버스를 건드리지 않음) → 마스크 안에서 정지 중(`E_STOPPED`) / 이미 제출됨(`E_BUSY`) / 큐 가득(`E_QFULL`) 검사 → 큐에 넣고 `seq` 부여 → 버스가 놀면 바로 시작.
- `spi_isr(b)` — 완료 플래그 읽기(없으면 가짜 IRQ, 무시) → `active` 떼어 내기(CS↑) → **다음 것 시작** → 끝난 것의 `cb(ctx, DONE)`.
- 테스트: 단일 전송 시간(정확히 2 + len tick), 세 개 연속(완료 tick 6/11/15 — 공백 없음), 큐 포화(9개 받고 10번째 `QFULL`, 콜백 없음, 자리가 나면 다시 받음), 인자 오류 7종, 이중 제출 `BUSY`, 우선순위 순서 `L0 H L1 L2`, **콜백 안 재제출**(A 가 자기를 두 번 더 넣어도 `A B A A`), 콜백이 10 tick 걸려도 다음 전송은 이미 돌고 있음, tx/rx NULL, 위 그림의 로그.

### Part B — 취소와 타임아웃 (면접관이 찌르는 곳)

- `q_remove(q, x)` — 링 가운데 원소를 빼고 나머지 순서를 유지 (head 0..7 × count 1..8 × 위치 전수).
- `spi_cancel(b, x)` — 대기 중이면 큐에서 빼고, 실행 중이면 **하드웨어 중단 → CS 해제 → 다음 것 시작**. 찾았으면 마스크 밖에서 `cb(CANCELLED)` 정확히 한 번, 못 찾으면 `E_NOTFOUND` (두 번째 취소, 제출 안 한 것, 이미 끝난 것).
- `spi_tick(b)` — SysTick 마다: 실행 중인 전송이 시작 후 `timeout` tick 이 지나면 중단 → 다음 시작 → `cb(TIMEOUT)`.
- 주입하는 장애와 기대 결과: 대기 중 취소(동기 콜백, 순서 유지), 실행 중 취소(abort 1회, 다음 것이 즉시 CS↓), **완료 IRQ 가 이미 떠 있는데 취소**(오래된 IRQ 가 다음 디스크립터를 일찍 "완료"시키면 안 됨), **가짜 IRQ**(완료 플래그 없음 → 무시), 콜백 안에서 다른 것 취소, 영원히 안 끝나는 전송(정확히 t = 50 에 TIMEOUT, 큐는 계속), 경계(전송 12 tick: timeout 12 → DONE, 11 → TIMEOUT), tick 카운터 랩어라운드(0xFFFFFFF0 + 40 → 0x18 에 TIMEOUT).

### Part C — 펌웨어 현실: flush, 시나리오, 선점

- `spi_flush(b)` — 전원 절약 진입·버스 복구 전에 전부 취소. 실행 중 1개 + 모든 대기열을 마스크 안에서 떼어 내고, 마스크 밖에서 각각 `cb(CANCELLED)`. 진행 중에는 제출을 `E_STOPPED` 로 거절해 **재제출하는 콜백이 있어도 끝난다**. 콜백 안에서 또 flush → `E_STOPPED`.
- 시나리오: IMU(CS0, 높음, 7 B burst, 1 kHz) + 플래시(CS1, 낮음, 256 B 청크 32개를 콜백 체인으로). IMU 20번 전부 DONE, 놓친 샘플 0, 최악 지연 ≤ 플래시 청크 1개 + IMU 1개 = 258 + 9 = 267 tick.
- 콜백이 `spi_submit` 반환 *전에* 오는 경로를 결정적으로 재현 (마스크 안에서 끝난 전송의 IRQ 가 `crit_exit` 에서 돌고, 그 콜백이 방금 넣은 것을 취소).
- 랜덤 스트레스 6만 tick × 2 (선점 없음 / 선점 지점마다 30%): 태스크와 콜백이 무작위로 제출·취소·flush, 멈춤·준비 지연·가짜 IRQ 주입. **매 tick 불변식**: CS ≤ 1개, 켜진 CS == active 의 CS, 버스가 놀면 큐도 비어 있음, 큐의 원소는 전부 QUEUED, 콜백은 마스크 밖에서 IDLE 상태로, 하드웨어 규칙 위반 카운터 0, DONE 은 rx 값이 맞음, 우선순위별 시작 순서 = 제출 순서, 마지막에 모든 디스크립터 `제출 수 == 콜백 수`.

## 2. Problem (English)

> **Asynchronous transaction queue for a shared SPI bus.**
>
> Several drivers (IMU, flash, ADC) share one SPI controller with four chip selects. Callers must not block: they fill a descriptor and submit it; the driver runs transfers one at a time and calls the descriptor's callback when it finishes. Callbacks run in interrupt context and may submit or cancel descriptors.
>
> ```c
> typedef void (*spi_cb_t)(void *ctx, int status);   /* status: DONE / CANCELLED / TIMEOUT */
> typedef struct SpiXfer { uint8_t cs, prio; uint16_t len; const uint8_t *tx; uint8_t *rx;
>                          uint32_t timeout; spi_cb_t cb; void *ctx; /* driver-private: */ uint8_t state; uint32_t seq; } SpiXfer;
> int  spi_submit(SpiBus *b, SpiXfer *x);  /* OK, E_ARG, E_BUSY (already submitted), E_QFULL, E_STOPPED */
> int  spi_cancel(SpiBus *b, SpiXfer *x);  /* OK (callback CANCELLED delivered) or E_NOTFOUND */
> void spi_isr(SpiBus *b);                 /* transfer-complete interrupt */
> void spi_tick(SpiBus *b);                /* called every tick: per-transfer timeout */
> int  spi_flush(SpiBus *b);               /* cancel everything; returns count */
> ```
>
> Requirements: no dynamic allocation (fixed-capacity queue per priority, 8 entries); at most one CS asserted at any time; two priority levels, non-preemptive, FIFO within a level; every accepted submission gets exactly one callback; callbacks must not run with interrupts masked; a stuck transfer is aborted after `timeout` ticks from its start and the queue continues; canceling the running transfer aborts it and releases its CS.
>
> Example (transfer time = 2 + len ticks):
>
> ```text
> t=0  submit A(cs1,low,12B) B(cs2,low,8B)   -> A starts
> t=5  submit H(cs0,high,4B)                 -> queued ahead of B
> t=14 A done -> H starts;  t=20 H done -> B starts;  t=30 B done
> ```
>
> Follow-ups: In what order do you release CS, start the next transfer and call the callback — and why? What if the completion interrupt is already pending when the caller cancels? What do you mask, and for how long? Why is the high-priority latency still 267 µs in the worst case, and how would you reduce it?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 일곱 군데: Part A `spi_start_next` `spi_submit` `spi_isr`, Part B `q_remove` `spi_cancel` `spi_tick`, Part C `spi_flush`. mock 하드웨어(`hw_cs/hw_start/hw_ack/hw_abort/hw_tick`), 인터럽트 마스크 모델(`crit_enter/crit_exit`), 선점 지점(`preempt_point`), 링 `q_push/q_pop`, 떼어 내기 `spi_detach_active`, 콜백 호출 `spi_complete`, 로그, 하네스는 주어진다. 스텁은 `QFULL`/`NOTFOUND`/0 을 돌려주고 아무것도 시작하지 않으므로 모든 루프는 tick 상한에서 끝난다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:509  job_submit(&g_jobs[0]) == SPI_OK
FAIL starter.c:510  g_bus.active == &g_jobs[0].x && g_jobs[0].x.state == XS_ACTIVE && g_hw.cs_mask == (1u << 2)
…
app: imu reads=0 mean=0.0 max=0 overrun=0 | flash chunks=0 bytes=0
…
15/86 passed
```

통과하는 15개는 mock 자체 점검 5개, "아직 콜백이 없어야 한다" 2개, 그리고 아무 일도 안 일어나서 공짜로 맞는 불변식·상한 단언들이다 (아무것도 안 하는 드라이버는 불변식을 깨지 않는다 — 그래서 스트레스는 `done > 1000` 같은 "일을 했다" 단언을 같이 둔다).

## 4. 힌트

<details>
<summary>힌트 1 — 상태 네 개면 충분하다</summary>

`IDLE`(호출자 것) → `QUEUED` → `ACTIVE` → `CALLBACK`(드라이버 손은 떠났지만 콜백 전) → `IDLE`. `submit` 은 `IDLE` 일 때만 받는다. 콜백 직전에 `IDLE` 로 바꾸는 건 주어진 `spi_complete()` 가 한다 — 그래야 콜백 안에서 같은 디스크립터를 다시 낼 수 있다.

</details>

<details>
<summary>힌트 2 — start_next 는 "비었을 때만"</summary>

`if (b->active) return;` 한 줄이 이중 시작을 막는다. 우선순위 0 큐부터 `q_pop` 해서 처음 나온 것을 `ACTIVE` 로, `start_tick = now`, **CS↓ 다음에** `hw_start`. submit 은 큐에 넣은 뒤 무조건 `spi_start_next()` 를 부르면 된다 — 바쁘면 아무 일도 안 한다.

</details>

<details>
<summary>힌트 3 — ISR 의 네 줄 순서</summary>

`hw_ack()` 가 false 면 return (가짜 IRQ). `spi_detach_active(b, false)` (이미 끝났으니 중단 불필요) → `spi_start_next(b)` → `spi_complete(x, DONE)`. 다음 것을 콜백보다 먼저 시작하면 콜백이 오래 걸려도 버스는 쉬지 않는다.

</details>

<details>
<summary>힌트 4 — 취소는 "찾기"와 "알리기"를 나눈다</summary>

마스크 안: `QUEUED` 면 `q_remove` 성공 시 `CALLBACK` 상태로, `ACTIVE` 이고 `b->active == x` 면 `spi_detach_active(b, true)` + `spi_start_next(b)`. 마스크 밖: 찾았으면 `spi_complete(x, CANCELLED)`. `true`(하드웨어 중단)가 중요하다 — 완료 IRQ 가 이미 떠 있으면 `hw_abort()` 가 그것까지 지운다.

</details>

<details>
<summary>힌트 5 — 타임아웃 비교는 뺄셈으로</summary>

`(uint32_t)(now - start_tick) >= timeout`. `now >= start + timeout` 은 0xFFFFFFFF 를 넘는 순간 틀린다. 같은 tick 에 완료와 타임아웃이 겹치면 이 모델에서는 완료 IRQ 가 SysTick 보다 먼저 처리된다 (`sim_tick` 순서) → DONE.

</details>

<details>
<summary>힌트 6 — flush 는 목록을 먼저 만든다</summary>

마스크 안에서 `stopped = true`, active 와 큐 전부를 지역 배열(크기 2×8+1)로 옮기고, 마스크를 푼 뒤 콜백을 차례로 부른다. 끝나면 `stopped = false`. 이미 `stopped` 면 `E_STOPPED` (콜백 안에서 또 부른 경우).

</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "버스는 한 번에 한 트랜잭션. 호출자는 디스크립터를 큐에 넣고 바로 돌아가고, 완료 인터럽트가 다음 것을 시작한다. 큐는 우선순위별 고정 크기 링, 메모리는 호출자 소유."
2. "ISR 순서: 플래그 확인 → CS↑ → 다음 시작 → 콜백. 콜백은 ISR 문맥이니 짧게, 필요하면 태스크에 알림만."
3. "공유 상태(큐, active)는 태스크 쪽 API 에서 짧은 크리티컬 섹션으로 보호하고, 콜백은 반드시 마스크 밖에서 부른다."
4. "불변식: CS 최대 1개, 제출 1번 = 콜백 1번. 취소·타임아웃·flush 도 이걸 지킨다."
5. 꼬리질문 대비: 실행 중 취소 정책, 최악 지연(비선점), 뮤텍스 방식과 비교, DMA·캐시.

### Part A — 디스크립터 상태와 ISR 순서

```svg
<svg viewBox="0 0 760 312" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m13a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">디스크립터 상태 (SpiXfer.state) — 드라이버가 들고 있는 동안은 IDLE 이 아니다</text>
  <rect x="20" y="110" width="120" height="50" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="80" y="131" fill="currentColor" text-anchor="middle" font-weight="bold">IDLE</text>
  <text x="80" y="149" fill="currentColor" text-anchor="middle" font-size="12">호출자 소유</text>
  <rect x="210" y="110" width="120" height="50" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="270" y="131" fill="currentColor" text-anchor="middle" font-weight="bold">QUEUED</text>
  <text x="270" y="149" fill="currentColor" text-anchor="middle" font-size="12">링 q[prio] 안</text>
  <rect x="400" y="110" width="120" height="50" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="460" y="131" fill="currentColor" text-anchor="middle" font-weight="bold">ACTIVE</text>
  <text x="460" y="149" fill="currentColor" text-anchor="middle" font-size="12">CS↓, 전송 중</text>
  <rect x="590" y="110" width="150" height="50" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="665" y="131" fill="currentColor" text-anchor="middle" font-weight="bold">CALLBACK</text>
  <text x="665" y="149" fill="currentColor" text-anchor="middle" font-size="12">CS↑, 콜백 전</text>
  <path d="M140 125 H208" stroke="currentColor" fill="none" marker-end="url(#m13a)"/>
  <text x="174" y="100" fill="currentColor" font-size="12" text-anchor="middle">submit OK</text>
  <path d="M330 125 H398" stroke="currentColor" fill="none" marker-end="url(#m13a)"/>
  <text x="364" y="88" fill="currentColor" font-size="12" text-anchor="middle">start_next</text>
  <text x="364" y="104" fill="currentColor" font-size="12" text-anchor="middle">(버스 빔, prio 0 먼저)</text>
  <path d="M520 125 H588" stroke="currentColor" fill="none" marker-end="url(#m13a)"/>
  <text x="560" y="60" fill="currentColor" font-size="12" text-anchor="middle">ISR: DONE</text>
  <text x="560" y="76" fill="currentColor" font-size="12" text-anchor="middle">tick: TIMEOUT (abort)</text>
  <text x="560" y="92" fill="currentColor" font-size="12" text-anchor="middle">cancel / flush (abort)</text>
  <path d="M270 160 C320 205 560 205 640 162" stroke="currentColor" fill="none" marker-end="url(#m13a)"/>
  <text x="455" y="212" fill="currentColor" font-size="12" text-anchor="middle">cancel / flush (q_remove, q_pop)</text>
  <path d="M665 162 C640 290 120 290 80 162" stroke="currentColor" fill="none" marker-end="url(#m13a)"/>
  <text x="380" y="280" fill="currentColor" font-size="12" text-anchor="middle">spi_complete(): state = IDLE 로 바꾼 "뒤에" cb(ctx, status) — 콜백 안 재제출 가능</text>
  <text x="10" y="302" fill="currentColor" font-size="12">IDLE 이 아닌 상태에서 submit → E_BUSY.  큐 가득 → E_QFULL (IDLE 그대로, 콜백 없음).  flush 중 → E_STOPPED.</text>
</svg>
```

`CALLBACK` 상태가 따로 있는 이유: 취소는 마스크 안에서 디스크립터를 떼어 내고, 콜백은 마스크 밖에서 부른다. 그 사이에 IRQ 가 들어와 다른 콜백이 이 디스크립터를 다시 제출하면, 원래 주인이 받을 `CANCELLED` 와 새 제출이 뒤섞인다. `CALLBACK` 이면 그 사이의 submit 은 `E_BUSY` 로 막힌다. 콜백 직전에야 `IDLE` 이 된다.

ISR 의 순서별로 무엇이 달라지는지 (각 줄은 이 레슨 테스트로 확인):

| 순서 | 결과 |
|---|---|
| **ack → CS↑ → 다음 시작 → 콜백** (정답) | 콜백이 10 tick 걸려도 B 는 A 가 끝난 t=6 에 시작, t=17 에 콜백 (테스트 `cb_cost`) |
| ack → CS↑ → 콜백 → 다음 시작 | 기능은 맞지만 버스가 콜백 시간만큼 논다 — 위 테스트에서 B 시작이 t=16 으로 밀려 FAIL. 순서는 그대로다(재제출은 어차피 큐 꼬리) |
| 플래그 확인 없음 | 가짜 IRQ(공유 벡터의 다른 플래그·잡음) 한 번에 실행 중 전송이 "완료" — rx 는 반만 찬 채 DONE (FAIL 6개: 가짜 IRQ 테스트 2 + 스트레스 4) |
| `state = IDLE` 을 콜백 뒤에 | 콜백 안 재제출이 `E_BUSY` → `A B A A` 가 `A B` 로, IMU·플래시 체인이 끊김 (FAIL 5개) |

(표의 FAIL 수는 정답을 한 곳씩 바꿔 돌려 본 결과다.) 그림 1의 로그를 한 줄씩 보면:

| t | 이벤트 | active | q[0] | q[1] |
|---|---|---|---|---|
| 0 | SUBMIT L1 → START L1 (cs1) | L1 | — | — |
| 0 | SUBMIT L2 | L1 | — | L2 |
| 5 | SUBMIT H, SUBMIT L3 | L1 | H | L2 L3 |
| 8 | CANCEL L3 (q_remove, 즉시 콜백) | L1 | H | L2 |
| 14 | DONE L1 → START H (cs0) | H | — | L2 |
| 20 | DONE H → START L2 (cs2) | L2 | — | — |
| 30 | DONE L2 | — | — | — |

### Part B — 취소의 네 경우와 타임아웃

| 취소 시점 | 정답 동작 | 테스트에서 확인한 것 |
|---|---|---|
| 큐에서 대기 중 | `q_remove` → 즉시(동기) `cb(CANCELLED)` | 콜백이 `g_now == 0` 에서 옴, 나머지 순서 `0 1 3`, 시작 횟수 3 |
| 실행 중 | `hw_abort` → CS↑ → 다음 것 시작 → `cb(CANCELLED)` | abort 1회, 다음 것의 CS 가 즉시 내려감, `cs_cut == 0` (클럭을 먼저 멈추고 CS 를 올림) |
| 실행 중인데 완료 IRQ 가 이미 대기 중 | "취소가 이긴다": abort 가 대기 IRQ 까지 지운다 | `irq_pending` 이 false 로, 다음 것은 제 시간(t=8)에 정상 완료. abort 를 빼면 오래된 IRQ 가 다음 것을 다음 tick 에 반쪽 rx 로 "완료" (FAIL 8개) |
| 이미 끝남 / 콜백 대기 / 제출 안 함 | `E_NOTFOUND`, 콜백 없음 | 두 번째 취소에도 `cbs == 1` |

`q_remove` 는 "찾은 위치부터 뒤 원소를 한 칸씩 당기고 count−1" 이다. 인덱스는 모두 `(head + i) % CAP` 로 계산하므로 랩어라운드된 링에서도 같다. 전수 검사(head 8 × count 1..8 × 위치)로 순서 유지와 "없는 원소 → false" 를 확인한다. 길이 8 이라 O(n) 이동이 문제되지 않는다. 길어지면 "삭제 표시만 하고 pop 할 때 건너뛰기"(lazy deletion)도 가능하지만 그러면 큐가 슬롯을 계속 차지해 `QFULL` 판단이 흐려진다.

타임아웃은 **시작 시각 기준**이다. 큐에서 기다린 시간은 하드웨어 고장과 무관하므로 섞으면 바쁜 버스에서 멀쩡한 전송이 타임아웃 난다. 대기 시간 제한이 필요하면 별도 데드라인(연습 3). 경계: 12 tick 전송에 `timeout = 12` 는 같은 tick 에 완료 IRQ 가 먼저 처리되어 DONE, `11` 은 TIMEOUT 이고 그때 rx 는 9 바이트만 차 있다 (`rx[9] == 0xEE` 단언). 그래서 **TIMEOUT/CANCELLED 콜백에서 rx 를 쓰면 안 된다.**

### Part C — 무엇을 마스크하나

| 함수 | 실행 문맥 | 만지는 공유 상태 | 보호 |
|---|---|---|---|
| `spi_submit` | 태스크, 콜백(ISR) | `q[]`, `active`, `seq_next`, `stopped`, `x->state` | 짧은 crit (중첩 가능해야 — 콜백 안에서도 불림) |
| `spi_cancel` | 태스크, 콜백 | 위 + 하드웨어 abort | crit 안에서 떼어 내기, 콜백은 crit 밖 |
| `spi_isr` | SPI IRQ | 위 전부 | 같은 우선순위 ISR 끼리는 서로 선점 안 함 (이 모델의 가정) |
| `spi_tick` | SysTick ISR | `active`, 하드웨어 | crit — SysTick 과 SPI IRQ 우선순위가 다르면 꼭 필요. 이 모델은 같은 우선순위라 빼도 테스트가 통과한다 (정직하게: 이 모델로는 못 잡는 누락) |
| `spi_flush` | 태스크 | 전부 | crit 안에서 목록 만들기, 콜백은 밖 |

실제 Cortex-M 에서 `crit_enter/exit` 는 `uint32_t pm = __get_PRIMASK(); __disable_irq(); … __set_PRIMASK(pm);` (중첩 안전), 또는 BASEPRI 로 이 버스의 IRQ(SPI, DMA, SysTick) 우선순위 이하만 막는 방식이다. FreeRTOS 위라면 `taskENTER_CRITICAL()` / ISR 에서는 `taskENTER_CRITICAL_FROM_ISR()` (BASEPRI 기반 — 포트별 확인 권장). 어느 쪽이든 **구간은 큐 조작 몇 줄**이어야 하고, 콜백·memcpy·로그 출력은 밖이다.

모델은 이렇게 생겼다: 태스크 쪽 코드가 지나는 **선점 지점**(`q_push`/`q_pop` 가운데, `hw_cs`/`hw_start` 입구)에서 확률적으로 하드웨어가 1 tick 진행하고, 마스크가 안 돼 있으면 완료 ISR 이 바로 그 자리에서 돈다. 마스크 중이면 IRQ 는 대기했다가 바깥쪽 `crit_exit()` 에서 실행된다 — PRIMASK 를 풀면 대기 IRQ 가 들어오는 실제 동작과 같다. `q_push` 는 일부러 "count 를 읽고 → 슬롯 쓰고 → (선점) → count = 읽은 값 + 1" 모양이다.

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m13b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">마스크 없는 submit(B) 도중 완료 ISR 이 끼어들 때 — demo_race.c 결정적 재현</text>
  <text x="130" y="48" fill="currentColor" font-weight="bold" text-anchor="middle">태스크: submit(B)</text>
  <text x="400" y="48" fill="currentColor" font-weight="bold" text-anchor="middle">SPI 완료 ISR</text>
  <text x="640" y="48" fill="currentColor" font-weight="bold" text-anchor="middle">링 q[1] (head, count)</text>
  <path d="M130 58 V300 M400 58 V300" stroke="currentColor" opacity=".35"/>
  <rect x="40" y="68" width="180" height="40" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="130" y="85" fill="currentColor" text-anchor="middle" font-size="12">n = count (= 1, [C])</text>
  <text x="130" y="101" fill="currentColor" text-anchor="middle" font-size="12">slot[1+1] = B</text>
  <text x="640" y="92" fill="currentColor" text-anchor="middle" font-size="12">(1, 1): C | 슬롯3 = 옛 값 Z</text>
  <path d="M220 118 H300" stroke="currentColor" fill="none" marker-end="url(#m13b)" stroke-dasharray="4 3"/>
  <text x="260" y="134" fill="currentColor" font-size="12" text-anchor="middle">선점!</text>
  <rect x="300" y="118" width="200" height="56" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="400" y="136" fill="currentColor" text-anchor="middle" font-size="12">A 완료 → q_pop → C 시작</text>
  <text x="400" y="152" fill="currentColor" text-anchor="middle" font-size="12">head 1→2, count 1→0</text>
  <text x="400" y="168" fill="currentColor" text-anchor="middle" font-size="12">cb(A)</text>
  <text x="640" y="150" fill="currentColor" text-anchor="middle" font-size="12">(2, 0): 비어 있음</text>
  <path d="M300 182 H220" stroke="currentColor" fill="none" marker-end="url(#m13b)" stroke-dasharray="4 3"/>
  <rect x="40" y="186" width="180" height="40" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="130" y="203" fill="currentColor" text-anchor="middle" font-size="12">count = n + 1 = 2</text>
  <text x="130" y="219" fill="currentColor" text-anchor="middle" font-size="12">(옛 n 으로 덮어씀)</text>
  <text x="640" y="208" fill="currentColor" text-anchor="middle" font-size="12">(2, 2): B, Z ← 유령</text>
  <text x="10" y="256" fill="currentColor" font-size="12">결과 (실제 출력): DONE 순서 A C B Z — Z 는 제출된 적이 없는데 전송되고 콜백까지 받았다 (accepted=0, callbacks=1).</text>
  <text x="10" y="278" fill="currentColor" font-size="12">crit 버전: 선점 지점에서 하드웨어는 진행하지만 IRQ 는 crit_exit 까지 대기 → (1,2): C, B 가 된 뒤 ISR → DONE A C B.</text>
  <text x="10" y="300" fill="currentColor" font-size="12">링 버퍼는 꺼낸 슬롯을 지우지 않으므로 count 가 하나 크면 옛 포인터가 되살아난다 (NULL 이면 디스크립터 유실).</text>
</svg>
```

`demo_race.c` 실제 출력 (같은 시드, 6만 tick; 열 = 불변식 위반 비트, 제출 수≠콜백 수인 디스크립터 수, 남는 콜백 수, rx 불일치, 순서 위반, DONE 수):

```text
== (1) one deterministic interleaving ==
[crit  ] after submit(B): q.head=2 q.count=1 active=C
         DONE order: A C B   Z: accepted=0 callbacks=0  extra_cb=0 inv=0x000
[nocrit] after submit(B): q.head=2 q.count=2 active=C
         DONE order: A C B Z   Z: accepted=0 callbacks=1  extra_cb=1 inv=0x080

preempt/1000 | submit | inv bits | jobs cb!=submit | extra cb | bad rx | order bad | done
    0        | crit   | 0x000    |      0          |        0 |      0 |         0 | 2100
   10        | crit   | 0x000    |      0          |        0 |      0 |         0 | 2143
   50        | crit   | 0x000    |      0          |        0 |      0 |         0 | 2160
  100        | crit   | 0x000    |      0          |        0 |      0 |         0 | 2314
  300        | crit   | 0x000    |      0          |        0 |      0 |         0 | 2820
    0        | nocrit | 0x000    |      0          |        0 |      0 |         0 | 2100
   10        | nocrit | 0x0c7    |      2          |      907 |      1 |         2 | 2100
   50        | nocrit | 0x086    |      5          |      902 |      1 |         3 | 2187
  100        | nocrit | 0x084    |      2          |      316 |      0 |         2 | 2329
  300        | nocrit | 0x084    |     14          |     3187 |      1 |        31 | 2808
```

선점이 없으면 마스크 없는 버전도 완벽하게 통과한다는 줄이 핵심이다: **경쟁 버그는 단위 테스트로는 안 보이고, 끼어드는 지점을 주입해야 보인다.** 선점 1%(10‰)만으로도 CS 동시 활성·이중 시작(비트 0x40)·유령 콜백 907개가 나온다.

### Part C — 시나리오와 지연

`solution.c` 의 시나리오 (실제 출력): `app: imu reads=20 mean=47.0 max=230 overrun=0 | flash chunks=32 bytes=8192`. 같은 구성을 1초(10⁶ tick) 돌린 `demo_latency.c` 실제 출력:

```text
mode depth chunk | IMU lat mean   p99   max (us) | missed | flash KB/s | bus busy %
prio     1    16 |          18.5    27    27      |      0 |      880.9 |  100.0
prio     1    64 |          42.4    75    75      |      0 |      961.0 |  100.0
prio     1   256 |         138.2   265   267      |      0 |      983.3 |  100.0
prio     4    16 |          18.5    27    27      |      0 |      880.9 |  100.0
prio     4    64 |          42.4    75    75      |      0 |      961.0 |  100.0
prio     4   256 |         138.2   265   267      |      0 |      983.3 |  100.0
fifo     1    16 |          18.5    27    27      |      0 |      880.9 |  100.0
fifo     1    64 |          42.4    75    75      |      0 |      961.0 |  100.0
fifo     1   256 |         138.2   265   267      |      0 |      983.3 |  100.0
fifo     4    16 |          72.5    81    81      |      0 |      880.9 |  100.0
fifo     4    64 |         240.4   273   273      |      0 |      961.0 |  100.0
fifo     4   256 |         928.4  1039  1041      |    153 |      984.6 |  100.0
```

```svg
<svg viewBox="0 0 760 262" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">IMU 최악 지연 (µs) — 우선순위 큐 vs 같은 FIFO 에 플래시 4개 (demo_latency 실측)</text>
  <text x="190" y="68" fill="currentColor" text-anchor="end">플래시 청크 16 B</text>
  <rect x="200" y="44" width="13.5" height="16" class="f-acc" stroke="currentColor"/>
  <text x="219.5" y="57" fill="currentColor" font-size="12">prio 27</text>
  <rect x="200" y="64" width="40.5" height="16" class="f-warn" stroke="currentColor"/>
  <text x="246.5" y="77" fill="currentColor" font-size="12">fifo×4 81</text>
  <text x="190" y="128" fill="currentColor" text-anchor="end">플래시 청크 64 B</text>
  <rect x="200" y="104" width="37.5" height="16" class="f-acc" stroke="currentColor"/>
  <text x="243.5" y="117" fill="currentColor" font-size="12">prio 75</text>
  <rect x="200" y="124" width="136.5" height="16" class="f-warn" stroke="currentColor"/>
  <text x="342.5" y="137" fill="currentColor" font-size="12">fifo×4 273</text>
  <text x="190" y="188" fill="currentColor" text-anchor="end">플래시 청크 256 B</text>
  <rect x="200" y="164" width="133.5" height="16" class="f-acc" stroke="currentColor"/>
  <text x="339.5" y="177" fill="currentColor" font-size="12">prio 267</text>
  <rect x="200" y="184" width="520.5" height="16" class="f-warn" stroke="currentColor"/>
  <text x="210" y="197" fill="currentColor" font-size="12">fifo×4 1041 — 1 ms 주기를 넘어 1000 샘플 중 153 놓침</text>
  <path d="M200 36 V216" stroke="currentColor"/>
  <path d="M700 36 V216" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="700" y="230" fill="currentColor" font-size="12" text-anchor="middle">1000 µs (IMU 주기)</text>
  <text x="200" y="230" fill="currentColor" font-size="12" text-anchor="middle">0</text>
  <text x="450" y="230" fill="currentColor" font-size="12" text-anchor="middle">500</text>
  <text x="10" y="254" fill="currentColor" font-size="12">비선점 버스: prio = (2 + chunk) + 9,  fifo×k = k·(2 + chunk) + 9.  측정 최대값이 공식과 같다 (1 s, 1 tick = 1 µs).</text>
</svg>
```

읽는 법 (bus busy 100% = 플래시가 남는 시간을 전부 채운다):

- **우선순위는 줄이 있을 때만 의미가 있다.** 플래시 디스크립터가 1개면(depth 1) 플래시 콜백이 다음 청크를 넣기 *전에* ISR 이 이미 IMU 를 시작하므로 FIFO 도 우선순위와 똑같다. 플래시가 4개를 미리 줄 세우면 FIFO 에서 IMU 는 최대 4청크를 기다린다.
- 최악 지연 공식: 우선순위 = `(2 + chunk) + (2 + 7)`, FIFO depth k = `k·(2 + chunk) + 9`. 256 B 에서 267 µs vs 1041 µs — 측정 최대값이 공식과 정확히 같다 (연습 4 가 12 조합 전부 `측정 ≤ 공식` 을 단언).
- FIFO depth 4 / 256 B 는 지연이 1 ms 주기를 넘어 **1000 샘플 중 153 개를 놓친다**. 우선순위 큐는 그대로 0.
- 청크를 키우면 처리량이 오른다(준비 2 tick 의 비중이 줄어서 881 → 983 KB/s). 대신 우선순위 큐에서도 IMU 최악 지연이 청크 하나만큼 늘어난다 — **비선점 버스에서 청크 크기가 곧 높은 우선순위의 지연 한계**다. 그래서 대용량 전송은 쪼개서 낸다.

스트레스 실제 출력 (매 tick 불변식 검사):

```text
stress preempt=  0/1000: submits=5215 done=2100 cancel=2295 timeout=820 | busy=9032 qfull=2475 stopped=247 | preempts=0 cb-before-submit-returns=0 inv=0
stress preempt=300/1000: submits=5886 done=2906 cancel=2188 timeout=792 | busy=8911 qfull=2383 stopped=202 | preempts=19591 cb-before-submit-returns=0 inv=0
```

`cb-before-submit-returns` 는 "spi_submit 이 아직 안 돌아왔는데 그 디스크립터의 콜백이 먼저 온" 횟수다. 지금 시드에서는 0 이지만, 개발 중 (가짜 IRQ 주입을 넣기 전의 난수 흐름에서) 2 번 나왔고, 그때 하네스가 `accepted++` 를 submit 이 돌아온 *뒤에* 하고 있어서 "콜백 수 > 제출 수" 로 FAIL 이 났다 — 드라이버가 아니라 **호출자 코드**의 버그다. 경로: submit 의 마스크 안에서 실행 중인 A 가 끝나 IRQ 가 대기 → `crit_exit()` 에서 ISR 이 돌아 방금 넣은 X 를 시작 → A 의 콜백이 X 를 취소 → X 의 콜백이 submit(X) 안에서 실행. Part C 의 결정적 테스트가 이 경로를 그대로 재현한다 (`g_cb_early == 1`). 그래서 하네스는 제출 전에 기록하고 실패하면 되돌린다 (7장 실수 5).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 86/86 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 깨끗함
cc -std=c11 -Wall -Wextra -O2 demo_race.c -o demo_race && ./demo_race         # 위 경쟁 표
cc -std=c11 -Wall -Wextra -O2 demo_latency.c -o demo_latency && ./demo_latency   # 위 지연 표 (Apple clang 21, 결정적 시뮬레이션)
```

## 7. 자주 하는 실수

**1. 콜백을 마스크 안에서 부른다.** 콜백이 길면 그동안 모든 인터럽트가 막히고(UART 바이트 유실), 콜백이 블로킹 API 를 부르면 교착이다. 취소 콜백을 crit 안으로 옮겨 보면 스트레스의 `g_cb_in_crit` 가 0 이 아니어서 FAIL 2개.

```c
crit_enter(); /* 떼어 내기만 */ crit_exit();
if (found) spi_complete(x, SPI_ST_CANCELLED);     /* 밖에서 */
```

**2. 상태를 콜백 *뒤에* IDLE 로 바꾼다.** 콜백 안의 재제출이 `E_BUSY` 로 실패해 체인 전송(플래시 청크, 주기 센서)이 조용히 멈춘다. 소유권을 먼저 돌려주고 부른다 (`spi_complete`).

**3. 실행 중 취소에서 하드웨어를 안 멈춘다.** CS 만 올리면 클럭이 계속 돌거나(`cs_cut`), 이미 떠 있는 완료 IRQ 가 다음 디스크립터를 "완료"시킨다. `spi_detach_active(b, true)` — 클럭 정지 → CS↑ 순서.

**4. ISR 에서 플래그를 안 본다.** SPI IRQ 벡터 하나에 TXE/RXNE/에러 플래그가 같이 묶이는 MCU 가 많고, DMA 를 쓰면 HT·TC·에러가 한 벡터다. "IRQ 가 왔다 = 끝났다" 가정은 가짜 IRQ 한 번에 반쪽 rx 를 DONE 으로 넘긴다.

**5. 콜백이 `submit` 반환 뒤에 온다고 가정한다.** 마스크를 푸는 순간 대기 IRQ 가 돌 수 있고(멀티코어면 언제든), 그 콜백 체인이 방금 넣은 요청을 끝낼 수 있다. 제출 기록(카운터, 타임스탬프, "대기 중" 플래그)은 submit *전에* 하고 실패하면 되돌린다.

```c
j->accepted++; j->submit_tick = g_now;           /* 먼저 */
int rc = spi_submit(&g_bus, &j->x);
if (rc != SPI_OK) { j->accepted--; j->submit_tick = old_tick; }
```

**6. flush 가 큐가 빌 때까지 돈다.** `while (!empty) cancel(front)` + 재제출하는 콜백 = 무한 루프. 목록을 먼저 떼어 내고 진행 중엔 `E_STOPPED`. 이걸 빼면 flush 테스트 FAIL 8개 (재제출이 받아들여져 버스가 다시 돈다).

**7. 절대 시각 데드라인 비교.** `if (now >= start + timeout)` 은 `start + timeout` 이 넘치면 즉시 타임아웃(또는 영원히 안 남). 경과 시간을 뺄셈으로: `(uint32_t)(now - start) >= timeout`. 절대 비교로 바꿔 보면 랩어라운드 테스트 2개가 FAIL (0xFFFFFFF0 에서 시작하자마자 TIMEOUT).

**8. 스택 디스크립터를 큐에 남기고 돌아간다.** 동기 래퍼가 대기 시간 초과로 그냥 return 하면 큐에는 사라진 스택 프레임 주소가 남는다 → 나중에 그 주소에 DMA 가 쓴다. 돌아가기 전에 반드시 `spi_cancel` (연습 1).

## 8. 면접 Q&A

<details>
<summary>Q1. 한 SPI 버스를 여러 태스크가 써야 합니다. 뮤텍스로 할 건가요, 큐로 할 건가요?</summary>

둘 다 쓴다. **뮤텍스 방식**: `lock → 설정 → CS↓ → 블로킹 전송 → CS↑ → unlock`. 단순하고 태스크마다 동기 코드라 읽기 쉽다. 단점: ISR 에서 못 쓴다, 낮은 우선순위 태스크가 4 KB 플래시 읽기 동안 락을 쥐면 IMU 태스크가 통째로 기다린다(우선순위 역전 — 상속 뮤텍스 필요, RTOS-M03/M04), 버스 공백이 태스크 전환 시간만큼 생긴다. **큐 방식**(이 레슨): ISR·콜백에서도 제출 가능, 완료 ISR 이 다음 것을 바로 시작해 공백 0, 우선순위를 버스 단위로 줄 수 있다. 단점: 소유권·취소·콜백 문맥을 설계해야 한다. 실무 답: 드라이버 내부는 큐(비동기), 그 위에 `spi_sync` 류 블로킹 래퍼(연습 1)를 얹어 일반 태스크는 동기 API 를 쓰게 한다 — Linux 가 정확히 이 구조다. 읽기-수정-쓰기처럼 여러 트랜잭션을 원자적으로 묶어야 하면 디바이스 단위 락을 그 위에 더한다 (COMM-B05 Q6).

</details>

<details>
<summary>Q2. 완료 콜백은 어떤 문맥에서 실행되나요? 그 안에서 해도 되는 것과 안 되는 것은?</summary>

완료 ISR 안이다 (취소 콜백은 cancel 을 부른 문맥). 해도 되는 것: 짧은 상태 갱신, 다음 디스크립터 제출·취소(드라이버가 재진입 안전하게 설계됨), 태스크 깨우기(세마포어 give / 태스크 알림 FromISR — RTOS-B08, RTOS-M01). 안 되는 것: 블로킹 대기(동기 래퍼 → 연습 1 에서 `SPI_E_CONTEXT`), 긴 계산·printf, 뮤텍스. 정답 ISR 은 다음 전송을 콜백보다 먼저 시작하므로 콜백이 길어도 버스는 쉬지 않지만, 다른 인터럽트의 지연은 늘어난다 — 무거운 일(단위 변환, 필터, 로그)은 태스크로 넘긴다.

</details>

<details>
<summary>Q3. 실행 중인 전송을 취소하면 어떻게 하나요?</summary>

정책을 정해서 문서화한다. **중단(abort)** — 이 레슨 기본: DMA·SPI 정지 → CS↑ → 다음 것 시작 → `CANCELLED`. 센서 읽기처럼 중간에 끊어도 디바이스 상태가 망가지지 않는 경우에 맞다. CS↑ 가 칩의 프로토콜 상태를 리셋해 주기 때문이다 (COMM-B05). **너무 늦음(too late)** — 연습 2: 실행 중이면 `E_BUSY` 를 돌려주고 정상 완료를 기다린다. 플래시 page program 이나 설정 레지스터 묶음 쓰기처럼 반쯤 보낸 명령이 위험할 때 맞다. 어느 쪽이든 경계 경우를 정해야 한다: 하드웨어는 끝났는데 ISR 이 아직 안 돈 상태에서 취소가 오면? 이 레슨은 "취소가 이긴다" 이고 abort 가 대기 IRQ 플래그까지 지워야 한다 (안 지우면 다음 전송이 오래된 IRQ 로 완료된다 — Part B 테스트).

</details>

<details>
<summary>Q4. 높은 우선순위인데 IMU 최악 지연이 267 µs 입니다. 왜죠? 줄이려면?</summary>

버스가 비선점이라서다. IMU 요청이 플래시 256 B 청크가 막 시작된 직후에 오면 그 청크(2 + 256 µs)가 끝날 때까지 기다리고, 그다음 자기 전송 9 µs. 측정 최대값이 공식과 같다. 줄이는 방법: ① 대용량 전송을 작게 쪼갠다 (16 B 청크면 27 µs, 대신 처리량 881 vs 983 KB/s), ② 시간에 민감한 디바이스를 다른 SPI 버스로 분리, ③ 청크 경계에서만 선점 가능한 "분할 전송" 디스크립터, ④ IMU 의 FIFO 를 써서 지연 요구 자체를 완화. 같은 우선순위 FIFO 에 플래시가 4개를 미리 줄 세우면 최악 1041 µs 로 1 kHz 샘플의 15% 를 놓친다 (demo_latency) — "우선순위 큐가 필요한 이유"의 숫자다. 이 분석은 CAN 응답 시간 분석(COMM-M05/M08)과 같은 모양이다: 비선점 자원의 블로킹 항 + 자기 전송 시간.

</details>

<details>
<summary>Q5. 크리티컬 섹션에서 정확히 무엇을 막나요? 얼마나 길어도 되나요?</summary>

큐(`head/count/slot`), `active`, `stopped`, 디스크립터 `state` 를 읽고 쓰는 몇 줄만. 막을 대상은 그 상태를 만지는 다른 문맥 = 이 버스의 완료 IRQ, (DMA 를 쓰면) DMA IRQ, 타임아웃을 보는 SysTick. 전역 `__disable_irq` (PRIMASK) 는 간단하지만 모든 IRQ 를 막고, BASEPRI 는 우선순위 임계값 이하만 막아 모터 제어 같은 더 급한 IRQ 는 살린다. NVIC 에서 해당 IRQ 하나만 끄는 방법도 있지만 여러 IRQ 가 상태를 공유하면 빠지기 쉽다. 중첩 가능해야 한다(콜백 안에서 submit → 이미 ISR 안). 길이는 수십 사이클 수준이 목표 — 그래서 콜백 호출·memcpy·로그는 밖이다. 빠뜨리면? `demo_race` 가 보여 주듯 선점 1% 에서도 유령 디스크립터와 CS 동시 활성이 나오는데, 선점 없는 단위 테스트는 전부 통과한다.

</details>

<details>
<summary>Q6. 타임아웃은 어디서, 무엇을 기준으로 재나요?</summary>

전송 **시작** 기준, 주기 타이머(SysTick 이나 하드웨어 타이머)에서 `active` 하나만 본다 — O(1). 큐 대기 시간은 버스 혼잡이지 고장이 아니므로 따로(연습 3: 오래된 센서 요청은 시작하지 말고 버림). 실제 값은 "예상 전송 시간 × 여유 + tick 해상도"; 1 ms SysTick 이면 최소 해상도가 1 ms 라 짧은 전송에는 하드웨어 타이머가 낫다. 비교는 랩어라운드 안전한 뺄셈. 타임아웃이 나면 abort 후 큐를 계속 돌리고, 같은 디바이스에서 연속으로 나면 상위에서 디바이스 리셋·버스 복구 (I2C 라면 COMM-M03 의 bus clear).

</details>

<details>
<summary>Q7. 실제 칩에서 DMA 로 돌리면 무엇이 달라지나요?</summary>

`hw_start` = TX·RX DMA 스트림 설정 + SPI 활성, 완료 IRQ = **RX DMA 전송 완료** (TX 완료는 마지막 바이트가 아직 시프트 중일 수 있어서 CS↑ 가 이르다 — COMM-B05 Q5). abort = DMA 스트림 정지 + SPI 비활성 + 남은 FIFO·플래그 정리 (STM32 HAL 이면 `HAL_SPI_Abort` 류 — RM/HAL 확인 권장). Cortex-M7 처럼 D-cache 가 있으면 시작 전에 tx clean, 완료 후 rx invalidate, 버퍼는 캐시 라인 정렬 (ARCH-M12). 디스크립터·버퍼는 콜백까지 살아 있어야 한다 — 스택 버퍼를 넘기고 함수가 먼저 돌아가면 DMA 가 남의 스택에 쓴다 (실수 8). STM32 HAL 의 `HAL_SPI_TxRxCpltCallback` 도 IRQ 문맥에서 불린다.

</details>

<details>
<summary>Q8. Linux 나 Zephyr 는 이걸 어떻게 하나요?</summary>

Linux: `spi_async(spi, msg)` 는 어떤 문맥에서도 부를 수 있고 `msg->complete(msg->context)` 콜백은 잠들 수 없는 문맥에서 불린다는 것이 커널 주석의 설명이다. `spi_sync()` 는 그 위에 completion 을 기다리는 래퍼다. 컨트롤러마다 메시지 큐가 있고 "message pump"(kthread worker)가 하나씩 꺼내 `transfer_one_message` 로 돌린다. 메시지 사이 우선순위는 따로 없고, 펌프 스레드를 실시간 우선순위로 올리는 옵션(`ctlr->rt`)이 있는 정도다 (기억에 의존 — `drivers/spi/spi.c` 확인 권장). Zephyr: `spi_transceive()`(동기), `spi_transceive_cb()` / `spi_transceive_signal()`(비동기, `CONFIG_SPI_ASYNC`), 여러 전송 동안 버스를 쥐는 `SPI_LOCK_ON` 과 `spi_release()` 가 있다 (확인 권장). 공통점: 디스크립터(메시지) + 호출자 소유 버퍼 + 완료 콜백 + 동기 래퍼.

</details>

## 9. 추가 연습

### 연습 1 — 비동기 위의 동기 래퍼

`spi_transfer_sync(b, cs, tx, rx, len, timeout, max_wait)` 를 짜라. 디스크립터는 스택에, 완료는 콜백이 세우는 플래그로 기다린다 (RTOS 라면 세마포어). 대기 시간을 넘기면 `SPI_E_CANCELLED`, 하드웨어 멈춤이면 `SPI_E_TIMEOUT`, 콜백(ISR) 안에서 부르면 `SPI_E_CONTEXT`.

<details>
<summary>답 보기</summary>

```c
if (g_in_isr) return SPI_E_CONTEXT;          /* 기다리는 동안 완료 ISR 이 영원히 안 온다 */
SyncWait w = { false, -1 };
SpiXfer x; memset(&x, 0, sizeof x);           /* state = IDLE */
x.cs = cs; x.len = len; x.tx = tx; x.rx = rx; x.timeout = timeout; x.cb = sync_cb; x.ctx = &w;
int rc = spi_submit(b, &x);
if (rc != SPI_OK) return rc;
while (!w.done && waited < max_wait) { sim_tick(); waited++; }   /* xSemaphoreTake(sem, max_wait) */
if (!w.done) (void)spi_cancel(b, &x);        /* 스택 주소를 큐에 남기지 않는다 */
```

확인한 것: 빈 버스에서 정확히 7 tick(2 + 5), 앞에 30 B 전송이 있으면 39 tick, 대기 10 tick 으로 부족하면 `CANCELLED` 로 돌아오고 큐가 비어 있음, 멈춤 주입 시 20 tick 에 `TIMEOUT`, 콜백 안에서 부르면 `CONTEXT`. cancel 은 `CANCELLED` 콜백을 **동기적으로** 부르므로 return 시점엔 드라이버가 x 를 잊었다 — 이 보장이 없으면 스택 디스크립터는 쓸 수 없다.

</details>

### 연습 2 — "너무 늦음" 취소 정책

실행 중인 전송은 건드리지 않고 `SPI_E_BUSY` 를 돌려주는 `spi_cancel_soft()` 를 짜라. 대기 중이면 기존 취소와 같다.

<details>
<summary>답 보기</summary>

```c
crit_enter(); bool running = (b->active == x); crit_exit();
if (running) return SPI_E_BUSY;               /* 정상 완료(DONE) 콜백을 기다린다 */
return spi_cancel(b, x);
```

확인: 실행 중 → `BUSY`, 콜백 0, abort 0 → 나중에 정상 DONE(rx 일치). 대기 중 → `CANCELLED`, 시작 횟수 1. 두 검사 사이에 그 전송이 시작될 수 있다(crit 밖) — 그 경우 `spi_cancel` 의 abort 정책이 적용되므로, 엄격하게 하려면 판단과 제거를 같은 crit 안에서 해야 한다. 플래시 쓰기처럼 중단이 위험한 디바이스는 디스크립터에 `no_abort` 플래그를 두는 게 실무적이다.

</details>

### 연습 3 — 큐 대기 데드라인

디스크립터에 `t_submit` 을 기록하고, 큐에서 `max_age` 보다 오래 기다린 요청을 시작하지 않고 `TIMEOUT` 으로 돌려주는 `spi_expire_queued(b, max_age)` 를 짜라.

<details>
<summary>답 보기</summary>

crit 안에서 양쪽 큐를 훑으며 `now - t_submit > max_age` 인 것을 `q_remove` 로 빼서 목록에 담고(제거했으면 인덱스를 올리지 않는다), crit 밖에서 `cb(TIMEOUT)`. 실제 출력:

```text
  t= 90 TIMEOUT cs0 p0 len  7 seq 1
  t= 90 TIMEOUT cs0 p0 len  7 seq 2
  t=102 DONE    cs1 p1 len100 seq 0
  t=102 START   cs0 p0 len  7 seq 3
  t=111 DONE    cs0 p0 len  7 seq 3
```

102 tick 짜리 전송 뒤에서 나이 90·60·30 인 IMU 요청 셋 중 50 을 넘은 둘만 버려지고, 가장 최근 것만 실행된다. 1 kHz 센서라면 오래된 샘플 요청 여러 개를 줄줄이 실행하는 것보다 최신 하나가 낫다.

</details>

### 연습 4 — 최악 지연 공식 vs 시뮬레이션

`imu_bound(prio, chunk, depth)` 를 쓰고 FIFO/우선순위 × depth 1/4 × 청크 16/64/256 12 조합을 5만 tick 씩 돌려 `측정 최대 ≤ 공식` 을 확인하라. 샘플을 놓치는 조합은?

<details>
<summary>답 보기</summary>

`prio: (2 + chunk) + 9`, `fifo: depth·(2 + chunk) + 9`. 실제 출력 (일부):

```text
mode depth chunk | imu max  bound | overrun
fifo     1   256 |     264    267 | 0
fifo     4    64 |     273    273 | 0
fifo     4   256 |    1024   1041 | 7
prio     4   256 |     264    267 | 0
```

12 조합 모두 측정 ≤ 공식. 5만 tick 에서는 256 B 의 최악 위상(청크 시작 직후 도착)이 안 걸려 264 지만, 1초 돌린 demo_latency 에서는 267 / 1041 로 공식에 닿는다. FIFO·depth 4·256 B 만 1 ms 주기를 넘어 샘플을 놓친다.

</details>

### 연습 5 — 취소 경로의 크리티컬 섹션 누락 드러내기

본문 모델에서는 `spi_cancel` 의 crit 를 빼도 테스트가 통과한다 — `q_remove` 안에 선점 지점이 없기 때문이다. `q_remove` 의 이동 루프에 `preempt_point()` 를 넣고, 마스크 없는 취소를 스트레스(선점 30%)에 끼워 차이를 보여라.

<details>
<summary>답 보기</summary>

```c
uint8_t n = q->count;
for (uint8_t j = i; j + 1u < n; j++) { q->slot[...j] = q->slot[...j+1]; preempt_point(); }
q->count = (uint8_t)(n - 1u);                 /* 그 사이 ISR 이 q_pop 했다면 count 가 틀어진다 */
```

실제 출력:

```text
cancel with crit   : inv=0x000 mismatch=0 extra_cb=0
cancel without crit: inv=0x0c7 mismatch=10 extra_cb=1279
```

교훈: "테스트가 통과한다" 는 "모델이 그 경쟁을 표현할 수 있다" 일 때만 의미가 있다. 선점 지점(= 실제 코드에서 공유 변수를 읽고 다시 쓰는 사이)을 어디에 두느냐가 이 테스트의 힘이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 19/19 passed, exit 0
```

## 10. 핵심 정리

- 공유 버스 = **디스크립터 큐 + 완료 ISR 이 다음 것을 시작**. 큐는 고정 용량 링(포인터), 메모리는 호출자 소유, 가득 차면 `QFULL`.
- ISR 순서: **완료 플래그 확인 → CS↑ → 다음 시작 → 콜백**. 콜백은 ISR 문맥, 마스크 밖, 소유권을 돌려준 뒤.
- 불변식: CS 최대 1개, 켜진 CS == active 의 CS, 버스가 놀면 큐도 빔, **제출 1번 = 콜백 정확히 1번**(취소·타임아웃·flush 포함).
- 실행 중 취소는 정책(abort vs too late)을 정하고, abort 는 클럭 정지 → CS↑ → **대기 중인 완료 IRQ 까지 지우기**.
- 타임아웃은 시작 기준, 경과 시간 뺄셈(랩어라운드 안전). TIMEOUT/CANCELLED 의 rx 는 일부만 차 있다.
- 크리티컬 섹션은 큐 조작 몇 줄만, 중첩 가능하게. 빠뜨린 버그는 선점을 주입해야 보인다 (선점 1% 에서 유령 콜백 907개).
- 비선점 버스의 최악 지연 = 실행 중 전송 1개 + 자기 자신 → **청크 크기가 높은 우선순위의 지연 한계**. FIFO 에 미리 줄 세우면 그 배수.

## 참고자료

- Linux kernel, *SPI subsystem* — `spi_async()` / `spi_sync()` 문맥 설명, `struct spi_message` 의 `complete`/`context`, 컨트롤러 메시지 큐와 pump: <https://docs.kernel.org/driver-api/spi.html> 및 `drivers/spi/spi.c` (펌프·`rt` 옵션 세부는 기억에 의존 — 원문 확인 권장)
- Zephyr Project, *SPI API* — `spi_transceive`, `spi_transceive_cb`, `spi_transceive_signal`, `SPI_LOCK_ON`, `spi_release`: <https://docs.zephyrproject.org/latest/hardware/peripherals/spi.html> (확인 권장)
- Arm, *CMSIS-Core* — `__disable_irq`, `__get_PRIMASK` / `__set_PRIMASK`, BASEPRI: <https://arm-software.github.io/CMSIS_6/latest/Core/index.html>
- FreeRTOS, *taskENTER_CRITICAL / taskENTER_CRITICAL_FROM_ISR*, `configMAX_SYSCALL_INTERRUPT_PRIORITY`: <https://www.freertos.org/> API reference → RTOS kernel control (포트별 동작 확인 권장)
- STMicroelectronics, *STM32 HAL SPI* — `HAL_SPI_TransmitReceive_DMA`, `HAL_SPI_TxRxCpltCallback`(IRQ 문맥), `HAL_SPI_Abort` (UM 의 HAL 설명서, 계열별 확인)
- 관련 레슨: COMM-B04 (SPI 모드), COMM-B05 (트랜잭션 = CS 프레임, DMA 시 CS 타이밍), COMM-M12 (버스 독립 드라이버 계층), COMM-M03 (버스 복구), COMM-M05/M08 (CAN 비선점 지연 분석), CPP-B07 (RAII CS 가드), DS-B03/DS-B04 (링 버퍼), RTOS-B08 (ISR → 태스크 세마포어), RTOS-B10 (뮤텍스), RTOS-B11 (메시지 큐), RTOS-M01 (태스크 알림), RTOS-M03/M04 (우선순위 역전·상속), RTOS-M14 (재진입성), ARCH-M12 (DMA 캐시 일관성), BM-M08 (DMA 드라이버 — 다음 단계)
