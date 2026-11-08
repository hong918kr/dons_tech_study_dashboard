---
id: C-M07
time: 90분
updated: 2026-11-07
---
# C-M07 테이블 기반 상태 머신: State × Event

## 학습 목표

- 상태 머신(FSM, finite state machine)을 **state × event → {next, action}** 전이 표와 상태별 **entry/exit 액션** 으로 설계하고, 머신을 모르는 범용 엔진 `fsm_step` / `fsm_dispatch` 로 돌린다.
- 엔진의 가장자리 규칙을 정확히 말하고 구현한다: **빈 칸 = 무시**, **내부 전이(internal)** 와 **자기 전이(self-transition)** 의 차이, **범위 밖 이벤트·깨진 상태 값** 방어, 액션 안에서 생긴 이벤트의 **run-to-completion(RTC)** 처리.
- 버튼 디바운스(EXTI 엣지 + SysTick)와 UART 바이트 스트림 프레임 수신기(SYNC/LEN/PAYLOAD/CRC8)를 같은 엔진 위에 데이터로 올린다.
- "이벤트 시퀀스 → 기대 상태·출력 표" 와 고정 시드 퍼즈로 FSM 을 테스트한다.
- switch-case FSM 과 표 FSM 을 가독성·코드 크기(Cortex-M4 실측)·속도(실측)·안전성으로 비교해 언제 무엇을 쓸지 판단한다.

## 1. 문제

펌웨어는 거의 전부가 상태 머신이다 — 버튼, 통신 프로토콜 파서, 전원 관리, 부트로더, 모터 제어 모드. 면접에서 "UART 로 들어오는 패킷 파서를 짜 보세요", "디바운스를 상태 머신으로 짜 보세요" 는 단골이고, 꼬리질문은 늘 **"액션에서 이벤트가 또 생기면?", "ISR 에서 불러도 되나?", "상태 값이 깨지면?"** 이다.

switch-case 로 짜면 상태 하나가 `case` 하나, 그 안에 이벤트별 `if` 가 들어간다. 상태와 이벤트가 늘수록 "어떤 상태에서 어떤 이벤트가 오면 어디로 가는가" 가 코드 여기저기로 흩어진다. **표 기반 FSM** 은 그 정보를 한 장의 2차원 표(행 = 상태, 열 = 이벤트)로 모으고, 칸마다 `{다음 상태, 액션 함수 포인터}` 를 둔다. 실행 코드는 "표에서 칸을 찾아 순서대로 부르는" 작은 엔진 하나뿐이다. 함수 포인터 typedef 와 콜백 + context 포인터 자체는 C-M06 에서 다뤘다 — 여기서는 그것으로 **제어 흐름을 데이터로 바꾸는 법** 을 다룬다.

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="cm07-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">Part A — 버튼 디바운스 FSM (이벤트: DOWN / UP 엣지, TIMEOUT = 디바운스 타이머 만료)</text>
  <g stroke="currentColor">
    <rect x="30" y="50" width="190" height="70" rx="10" class="f-mut"/>
    <rect x="520" y="50" width="190" height="70" rx="10" class="f-acc"/>
    <rect x="520" y="210" width="190" height="70" rx="10" class="f-ok"/>
    <rect x="30" y="210" width="190" height="70" rx="10" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="125" y="80" font-weight="bold">IDLE</text>
    <text x="615" y="80" font-weight="bold">MAYBE_PRESSED</text>
    <text x="615" y="100" font-size="12">entry: timer=3 · exit: timer=0</text>
    <text x="615" y="240" font-weight="bold">PRESSED</text>
    <text x="125" y="240" font-weight="bold">MAYBE_RELEASED</text>
    <text x="125" y="260" font-size="12">entry: timer=3 · exit: timer=0</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#cm07-arr)">
    <path d="M220 70 L518 70"/>
    <path d="M520 100 L222 100"/>
    <path d="M615 120 L615 208"/>
    <path d="M520 230 L222 230"/>
    <path d="M220 262 L518 262"/>
    <path d="M125 210 L125 122"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="370" y="63">DOWN</text>
    <text x="370" y="118">UP (튐 → 없던 일)</text>
    <text x="370" y="223">UP</text>
    <text x="370" y="280">DOWN (튐 → 계속 눌림)</text>
  </g>
  <text x="605" y="170" fill="currentColor" text-anchor="end">TIMEOUT / emit 'P'</text>
  <text x="135" y="170" fill="currentColor">TIMEOUT / emit 'R'</text>
  <text x="10" y="310" fill="currentColor">그림에 없는 (상태, 이벤트) 조합 = 표의 빈 칸 = 무시. 예: PRESSED 에서 DOWN, IDLE 에서 TIMEOUT.</text>
  <text x="10" y="330" fill="currentColor">타이머 시작/정지는 전이마다 쓰지 않고 상태의 entry/exit 에 한 번만 — 그래서 튈 때마다 저절로 새로 시작한다.</text>
</svg>
```

하네스에서 이벤트 입력은 문자열 하나다: `D` = 눌림 엣지(EXTI ISR), `U` = 놓임 엣지, `t` = SysTick 1회, 공백 = 읽기 쉽게 구분. 매 입력 뒤 상태를 글자로 기록해(`I`=IDLE, `d`=MAYBE_PRESSED, `P`=PRESSED, `u`=MAYBE_RELEASED) **기대 상태 문자열** 과 비교하고, 앱으로 보낸 출력(`'P'` press, `'R'` release)도 비교한다. 예: `"DtU DtU Dttt"` → 상태 `"ddI ddI dddP"`, 출력 `"P"`.

### Part A — 엔진의 한 걸음 + 버튼 디바운스

- `fsm_step(m, ev)` — 현재 상태 행에서 `ev` 칸을 찾아 처리한다. 칸이 `{0, NULL}` 이면 무시(`FSM_IGNORED`), `{0, act}` 이면 `act` 만(`FSM_HANDLED`), `{s, act}` 이면 **exit(현재) → act → state = s → entry(s)** (`FSM_TRANSITION`).
- 상태 번호 0(`FSM_NO_STATE`)은 **예약** 이다. 그래서 지정 초기화자(designated initializer)로 적지 않은 칸이 자동으로 0 = "무시" 가 된다.
- 버튼 전이 표 `btn_table`, 상태 표 `btn_states`(MAYBE_* 의 entry = 타이머 시작, exit = 타이머 정지), `btn_tick()`(타이머가 0 이 되는 순간 TIMEOUT 을 1번) 을 채운다. 디바운스 = 3 tick.

### Part B — 엔진의 가장자리 규칙 (링크 감시 머신으로 시험)

`fsm_dispatch(m, ev)` 를 완성한다. 시험용 링크 머신(DOWN/UP, 이벤트 HB/DATA/TIMEOUT, 표는 주어짐)이 모든 entry/exit/action 을 trace 문자열로 남긴다.

1. **빈 칸**: DOWN 에서 DATA → `FSM_IGNORED`, 아무 함수도 안 불림.
2. **내부 전이 vs 자기 전이**: UP × DATA 는 내부 전이(워치독 그대로), UP × HB 는 UP → UP 자기 전이(exit + entry 가 돌아 워치독 재시작).
3. **잘못된 입력**: `ev >= n_events`, `state == 0`, `state >= n_states` → 표를 읽기 **전에** `FSM_EINVAL`.
4. **재진입**: 액션 안에서 `fsm_dispatch` 를 다시 부르면 바로 처리하지 말고 큐(`FSM_QLEN = 4`)에 넣고 `FSM_QUEUED`. 바깥 dispatch 가 지금 전이를 **끝까지** 마친 뒤 큐를 비운다(RTC). 큐가 차면 `FSM_EFULL`.

### Part C — UART 프레임 수신기 (RX ISR 처럼 1바이트씩)

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="cm07-arr2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">프레임 형식과 수신 상태 — 바이트 1개 = 이벤트 1개 (SYNC 또는 BYTE)</text>
  <g stroke="currentColor">
    <rect x="30" y="40" width="80" height="32" class="f-acc"/>
    <rect x="110" y="40" width="80" height="32" class="f-mut"/>
    <rect x="190" y="40" width="300" height="32" class="f-ok"/>
    <rect x="490" y="40" width="90" height="32" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="70" y="61">0xA5</text><text x="150" y="61">LEN</text>
    <text x="340" y="61">PAYLOAD[0 .. LEN-1]</text><text x="535" y="61">CRC8</text>
  </g>
  <text x="590" y="61" fill="currentColor" font-size="12">CRC8 = LEN+PAYLOAD 대상</text>
  <text x="30" y="92" fill="currentColor" font-size="12">LEN 0..16 · 다항식 0x07 · 페이로드 안의 0xA5 는 그냥 데이터</text>
  <g stroke="currentColor">
    <rect x="20" y="140" width="110" height="44" rx="8" class="f-mut"/>
    <rect x="210" y="140" width="110" height="44" rx="8" class="f-acc"/>
    <rect x="400" y="140" width="120" height="44" rx="8" class="f-ok"/>
    <rect x="600" y="140" width="110" height="44" rx="8" class="f-warn"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-weight="bold">
    <text x="75" y="167">HUNT</text><text x="265" y="167">LEN</text>
    <text x="460" y="167">PAYLOAD</text><text x="655" y="167">CRC</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#cm07-arr2)">
    <path d="M130 162 L208 162"/>
    <path d="M320 162 L398 162"/>
    <path d="M520 162 L598 162"/>
    <path d="M265 140 Q 450 100 640 138"/>
    <path d="M655 184 Q 380 250 85 186"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="169" y="154">SYNC</text>
    <text x="359" y="154">DONE</text>
    <text x="559" y="154">DONE</text>
    <text x="450" y="108">EMPTY (LEN == 0)</text>
    <text x="380" y="240">CRC: DONE / deliver · ERROR / err_crc</text>
    <text x="380" y="258">LEN: ERROR(LEN &gt; 16) / err_len · 어디서든 TIMEOUT / err_timeout</text>
  </g>
  <text x="10" y="286" fill="currentColor">LEN·PAYLOAD·CRC 의 바이트는 내부 전이 액션이 받고, 판정은 DONE/EMPTY/ERROR 이벤트로 다시 넣는다.</text>
</svg>
```

- 이벤트: `SYNC`(바이트 == 0xA5), `BYTE`(그 밖), `TIMEOUT`(프레임 도중 3 tick 침묵), 그리고 액션이 만드는 판정 이벤트 `DONE` / `EMPTY` / `ERROR`.
- 채울 것: `rx_table` 의 LEN·PAYLOAD·CRC 행, 액션 `rx_on_len`(길이 저장 + CRC, 판정 이벤트 발생), `rx_on_data`(저장 + CRC, 다 모이면 DONE), `rx_on_crc`(일치 → DONE, 불일치 → ERROR).
- 주어진 것: `crc8_update`, `rx_isr(byte)`(바이트를 분류해 dispatch), `rx_tick()`, 결과 카운터 액션 `rx_deliver` / `rx_err_*`, LEN 의 entry `rx_on_start`.
- 테스트: 바이트마다 상태 글자(`H` `L` `P` `C`) 표, 잡음·LEN 경계(0, 16, 17)·CRC 오류·타임아웃, 그리고 300 프레임 퍼즈(잡음, 랜덤 tick 간격, 7번째마다 손상).

## 2. Problem (English)

> **Table-driven finite state machine.**
>
> A generic engine runs any machine described by data: a transition table `fsm_trans_t table[n_states * n_events]` where each cell is `{uint8_t next; void (*action)(fsm_t *)}`, and a state table with `{name, entry, exit}`. State 0 is reserved so that a zero cell `{0, NULL}` means "event ignored"; `{0, action}` is an internal transition.
>
> 1. Implement `int fsm_step(fsm_t *m, unsigned ev)`: ignored → `FSM_IGNORED`; internal → run the action only, `FSM_HANDLED`; external (including `next == current`) → `exit(current)`, `action`, `state = next`, `entry(next)`, `FSM_TRANSITION`. Fill the transition and state tables of a 4-state push-button debouncer (IDLE, MAYBE_PRESSED, PRESSED, MAYBE_RELEASED; events DOWN, UP, TIMEOUT; debounce = 3 ticks started/stopped by entry/exit of the MAYBE states) and `btn_tick()`.
> 2. Implement `int fsm_dispatch(fsm_t *m, unsigned ev)`: reject out-of-range events and corrupted state values with `FSM_EINVAL` before touching the table; if called re-entrantly from inside an action, enqueue the event (`FSM_QUEUED`, or `FSM_EFULL` when the 4-entry queue is full) and process it only after the current transition has completed (run-to-completion).
> 3. Build a byte-at-a-time frame receiver `[0xA5][LEN 0..16][PAYLOAD][CRC-8 poly 0x07 over LEN+PAYLOAD]` on the same engine. Bytes arrive from a UART RX ISR as `SYNC`/`BYTE` events; a tick-driven inter-byte timeout returns the receiver to HUNT. Count delivered frames, length errors, CRC errors and timeouts.
>
> Example: input `"Dttt UtD ttt Uttt"` → states `"dddP uuP PPP uuuI"`, outputs `"PR"`. Bytes `A5 03 11 22 33 EE` → states `"LPPPCH"`, one frame `{11 22 33}`.
>
> Then compare this design with a switch-case FSM: readability, code size on Cortex-M4, speed, and safety.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 FAIL 줄과 표 덤프가 나오고 "9/51 passed" — TODO 를 채워 51/51 을 만든다
```

> 맥 SDK 27.2에서 `ld: tapi error` 가 나면 `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` 후 다시 빌드.

테스트가 실패하면 하네스가 `fsm_dump()` 로 지금 표를 사람이 읽는 모양(`MAYBE_PRESSED  entry=timer_start exit=timer_stop` / `ev2 -> PRESSED  emit_press`)으로 찍어 준다 — 표가 데이터라서 가능한 일이다.

관찰용 파일 세 개 (하네스와 별개):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_reentrancy.c -o demo_reentrancy && ./demo_reentrancy  # 재진입 3방식 비교
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o demo_bench && ./demo_bench                 # switch vs 표 속도
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_target.c -o -            # Cortex-M4 asm
```

## 4. 힌트

<details>
<summary>힌트 1 — 코드보다 표를 먼저 그린다</summary>

4 상태 × 3 이벤트 = 12칸짜리 표를 종이에 그리고, 칸마다 "어디로 가나 / 무엇을 하나" 를 채운다. 채울 칸은 6개뿐이고 나머지 6칸은 비워 둔다(= 무시). 튐(bounce)은 "MAYBE 상태에서 반대 엣지가 오면 원래 안정 상태로 돌아간다" 두 칸이다.

</details>

<details>
<summary>힌트 2 — fsm_step 의 순서</summary>

`t = &m->table[FSM_CELL(m->state, ev, m->n_events)]`. `t->next == 0` 이면 액션 유무로 무시/내부 전이를 가른다. 아니면 **exit → action → state 대입 → entry**. 자기 전이(`next == state`)를 따로 처리하지 않는다 — 그냥 같은 순서로 exit/entry 가 돈다.

</details>

<details>
<summary>힌트 3 — btn_tick 은 한 줄</summary>

"돌고 있는 타이머(>0)를 1 줄였더니 0 이 되었다" 일 때만 TIMEOUT. `if (b->timer > 0 && --b->timer == 0)` — `&&` 의 단락 평가 덕분에 멈춘 타이머(0)는 줄이지 않는다 (줄이면 `uint8_t` 가 255 로 감긴다).

</details>

<details>
<summary>힌트 4 — Part B: 검사 → busy 면 큐 → 아니면 처리 후 큐 비우기</summary>

`busy` 플래그를 세우고 `fsm_step` → `while (q_count) fsm_step(pop)` → `busy` 해제. 범위 검사는 맨 앞에. Part A 만 끝내고 돌리면 Part B ④ 에서 프로세스가 죽는다(실측: `-O2` 에서 종료 코드 138 = SIGBUS) — `ev = 7` 로 표 밖 칸을 읽어 쓰레기 값을 상태 번호·함수 포인터로 쓰기 때문이고, 그게 ③ 규칙이 있는 이유다.

</details>

<details>
<summary>힌트 5 — Part C: 받은 바이트는 내부 전이, 판단은 이벤트로</summary>

LEN/PAYLOAD/CRC 행의 SYNC·BYTE 칸은 모두 `{FSM_NO_STATE, rx_on_xxx}`. 액션이 결과를 `fsm_dispatch(m, RX_EV_DONE)` 처럼 **다시 넣으면** Part B 의 큐가 그 이벤트를 현재 처리 뒤에 실행한다. PAYLOAD 행의 SYNC 칸을 빠뜨리면 데이터 속 0xA5 가 무시되어 프레임이 깨진다.

</details>

## 5. 풀이

### Part A — 표가 곧 설계 문서

버튼 전이 표 (빈 칸 = 무시):

| 상태 \ 이벤트 | DOWN | UP | TIMEOUT |
|---|---|---|---|
| IDLE | → MAYBE_PRESSED | · | · |
| MAYBE_PRESSED *(entry: timer=3, exit: timer=0)* | · | → IDLE | → PRESSED / emit 'P' |
| PRESSED | · | → MAYBE_RELEASED | · |
| MAYBE_RELEASED *(entry: timer=3, exit: timer=0)* | → PRESSED | · | → IDLE / emit 'R' |

C 로 옮기면 표 모양이 거의 그대로 남는다:

```c
static const fsm_trans_t btn_table[BTN_N_STATES * BTN_N_EVENTS] = {
    [BT(BTN_IDLE, BTN_EV_DOWN)]              = { BTN_MAYBE_PRESSED, NULL },
    [BT(BTN_MAYBE_PRESSED, BTN_EV_UP)]       = { BTN_IDLE, NULL },
    [BT(BTN_MAYBE_PRESSED, BTN_EV_TIMEOUT)]  = { BTN_PRESSED, btn_emit_press },
    ...
};
```

세 가지 C 디테일:

| 디테일 | 이유 |
|---|---|
| **상태 0 예약** | 지정 초기화자로 적지 않은 원소는 정적 저장 기간 객체처럼 0 으로 초기화된다 (C11 6.7.9p19, p10 → 정수 0, 포인터는 null). `next` 가 0 이고 `action` 이 NULL 인 칸 = 무시. IDLE 이 0 이었다면 모든 빈 칸이 "IDLE 로 가라" 가 된다. |
| **1차원 표 + `FSM_CELL(s, e, n)`** | 엔진은 머신마다 다른 `n_events` 를 런타임에 받아야 한다. 2차원 배열 `tab[NS][NE]` 를 `&tab[0][0]` 로 펴서 `[s*NE+e]` 로 읽으면 첫 행 밖을 첨자로 넘어가는 것이라 엄밀히 UB 다 (C11 6.5.6p8, J.2 의 `a[1][7]` 예). 처음부터 1차원으로 선언하고 인덱스 매크로로 지정하면 깨끗하다. |
| **`const` 표** | 표와 상태 표는 `const` → MCU 에서 `.rodata` = 플래시. RAM 에는 `fsm_t` (현재 상태·큐·포인터 몇 개)만 남는다. 함수 포인터가 플래시에 있으니 런타임에 덮어쓸 수도 없다. |

`fsm_step` 의 순서는 UML 상태 머신의 전이 실행 순서와 같다: **나가는 상태의 exit → 전이 액션 → 들어가는 상태의 entry**.

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="cm07-arr3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">fsm_step(m, ev) — 칸 하나를 읽고 세 갈래</text>
  <rect x="10" y="40" width="150" height="44" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="85" y="60" fill="currentColor" text-anchor="middle">t = 표[state][ev]</text>
  <text x="85" y="77" fill="currentColor" text-anchor="middle" font-size="12">(범위 검사는 dispatch)</text>
  <g stroke="currentColor" fill="none" marker-end="url(#cm07-arr3)">
    <path d="M160 52 L218 52"/>
    <path d="M160 62 L218 100"/>
    <path d="M160 74 L218 150"/>
    <path d="M390 150 L418 150"/><path d="M500 150 L528 150"/><path d="M615 150 L628 150"/>
  </g>
  <rect x="220" y="36" width="250" height="32" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="230" y="57" fill="currentColor">{0, NULL} → 무시  FSM_IGNORED</text>
  <rect x="220" y="84" width="390" height="32" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="230" y="105" fill="currentColor">{0, act} → act() 만 (exit/entry 없음)  FSM_HANDLED</text>
  <rect x="220" y="134" width="170" height="32" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="230" y="155" fill="currentColor">{s, act} ① exit(현재)</text>
  <rect x="420" y="134" width="80" height="32" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="430" y="155" fill="currentColor">② act()</text>
  <rect x="530" y="134" width="85" height="32" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="538" y="155" fill="currentColor">③ state=s</text>
  <rect x="630" y="134" width="100" height="32" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="638" y="155" fill="currentColor">④ entry(s)</text>
  <text x="10" y="192" fill="currentColor">② 에서 m-&gt;state 는 아직 "이전" 상태다. s == 현재여도 ①~④ 를 그대로 한다 = 외부 자기 전이.</text>
</svg>
```

`btn_tick` 은 타이머 서비스다. 진짜 펌웨어에서 SysTick ISR(또는 소프트웨어 타이머 모듈)이 1 ms 마다 부르고, 만료 순간 TIMEOUT 이벤트를 **한 번** 만든다. 타이머의 시작·정지는 표의 칸이 아니라 MAYBE 상태의 entry/exit 에 붙어 있다. 튐 케이스 `"DtU DtU Dttt"` 를 한 칸씩 따라가면:

| 입력 | 칸 | 실행 | timer | 상태 |
|---|---|---|---|---|
| D | IDLE × DOWN | entry(MAYBE_P) | 3 | d |
| t | — (타이머만) | | 2 | d |
| U | MAYBE_P × UP | exit(MAYBE_P) | 0 | I |
| D | IDLE × DOWN | entry(MAYBE_P) — **새로 3** | 3 | d |
| t, U | (같은 반복) | | 0 | d, I |
| D t t t | … × TIMEOUT | exit → emit 'P' → (PRESSED entry 없음) | 0 | d d d P |

튈 때마다 MAYBE 상태에서 나갔다가 다시 들어오므로 타이머가 저절로 다시 시작된다. "마지막 엣지 이후 3 tick 동안 안정" 이라는 디바운스 정의가 코드 한 줄 없이 구조로 표현된 것이다. switch 로 짜면 `timer = DEB` 와 `timer = 0` 을 각 전이마다 손으로 써야 하고(demo_bench.c 의 `sw_event` 참고), 하나를 빠뜨리는 게 전형적인 버그다.

> 현실 수치: 기계식 스위치의 튐은 보통 수 ms 이고 드물게 훨씬 긴 것도 있다 (Ganssle 측정). 실제 코드는 1 ms tick 에 10~20 tick 을 쓴다. 테스트는 문자열을 짧게 하려고 3.

### Part B — 엔진이 지켜야 할 규칙

| 칸 / 입력 | 실행되는 것 | 반환 | 링크 머신 trace (실측) |
|---|---|---|---|
| 빈 칸 DOWN × DATA | 없음 | `FSM_IGNORED` | `""` |
| 외부 전이 DOWN × HB | exit → act → entry | `FSM_TRANSITION` | `xDOWN aUP eUP` |
| 내부 전이 UP × DATA | act 만 | `FSM_HANDLED` | `aDATA` (wdt 2 그대로) |
| 자기 전이 UP × HB → UP | exit → act → entry | `FSM_TRANSITION` | `xUP aHB eUP` (wdt 5 로 재시작) |
| `ev = 7`, `ev = n_events` | 없음 (표를 안 읽음) | `FSM_EINVAL` | `""` |
| `state = 0` 또는 `200` | 없음 | `FSM_EINVAL` | `""` |

**내부 전이 vs 자기 전이** 는 면접 단골 함정이다. 둘 다 상태는 그대로지만, 자기 전이는 상태를 "나갔다 다시 들어오므로" exit/entry 가 돈다. 링크 머신에서 하트비트(HB)는 자기 전이로 그려서 entry 가 워치독을 다시 거는 것이고, 데이터(DATA)는 내부 전이라 워치독을 건드리지 않는다. 이걸 반대로 그리면 "HB 는 오는데 워치독이 만료" 또는 "HB 가 끊겼는데 데이터 잡음이 워치독을 계속 살림" 버그가 된다. (UML 에는 둘 사이의 "local transition" 도 있지만 계층 상태에서만 의미가 있다.)

**잘못된 입력** 은 "그런 일은 없다" 로 넘어가면 안 되는 이유가 실측으로 보인다. 검사 없는 엔진에서 `ev = 7` 은 9칸짜리 `link_table` 의 인덱스 13 = 옆에 놓인 다른 `const` 데이터를 `{next, action}` 으로 해석하고, 그 쓰레기 값을 상태 번호·함수 포인터로 따라가다 SIGBUS 로 죽었다. 상태 값은 스택 오버플로·DMA 오버런·방사선 비트 플립으로 실제로 깨진다. 표 기반 FSM 에서 **깨진 인덱스 = 임의 주소로 점프** 이므로, 표를 읽기 전 범위 검사는 선택이 아니다. 실제 제품이라면 `FSM_EINVAL` 에서 안전 상태 진입·에러 로그·워치독 리셋 같은 정책을 정한다.

**재진입과 run-to-completion.** 링크가 올라오는 액션 `aUP` 가 DOWN 동안 쌓인 DATA 2개를 `fsm_dispatch` 로 다시 넣는다. `demo_reentrancy.c` 의 실측 출력:

```text
A) 재귀, 액션 뒤 상태 변경 trace: xDOWN aUP eUP                    rx=0 state=UP
B) 재귀, 액션 앞 상태 변경 trace: xDOWN aUP aDATA aDATA eUP        rx=2 state=UP
C) RTC 큐                     trace: xDOWN aUP eUP aDATA aDATA        rx=2 state=UP
```

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">액션 aUP 안에서 dispatch(DATA) 를 부를 때 — 세 엔진의 실행 순서</text>
  <text x="10" y="62" fill="currentColor">A) 재귀</text>
  <rect x="120" y="44" width="70" height="28" class="f-mut" stroke="currentColor"/><text x="128" y="63" fill="currentColor">xDOWN</text>
  <rect x="200" y="44" width="200" height="28" class="f-warn" stroke="currentColor"/><text x="208" y="63" fill="currentColor">aUP → DOWN×DATA = 빈 칸!</text>
  <rect x="410" y="44" width="60" height="28" class="f-mut" stroke="currentColor"/><text x="418" y="63" fill="currentColor">eUP</text>
  <text x="485" y="63" fill="currentColor">데이터 2개 유실</text>
  <text x="10" y="112" fill="currentColor">B) 재귀(상태 먼저)</text>
  <rect x="140" y="94" width="70" height="28" class="f-mut" stroke="currentColor"/><text x="148" y="113" fill="currentColor">xDOWN</text>
  <rect x="220" y="94" width="50" height="28" class="f-mut" stroke="currentColor"/><text x="228" y="113" fill="currentColor">aUP</text>
  <rect x="280" y="94" width="130" height="28" class="f-warn" stroke="currentColor"/><text x="288" y="113" fill="currentColor">aDATA aDATA</text>
  <rect x="420" y="94" width="60" height="28" class="f-mut" stroke="currentColor"/><text x="428" y="113" fill="currentColor">eUP</text>
  <text x="495" y="113" fill="currentColor">entry 전에 UP 의 일을 함</text>
  <text x="10" y="162" fill="currentColor">C) RTC 큐</text>
  <rect x="120" y="144" width="70" height="28" class="f-mut" stroke="currentColor"/><text x="128" y="163" fill="currentColor">xDOWN</text>
  <rect x="200" y="144" width="90" height="28" class="f-acc" stroke="currentColor"/><text x="208" y="163" fill="currentColor">aUP (큐에 2)</text>
  <rect x="300" y="144" width="60" height="28" class="f-mut" stroke="currentColor"/><text x="308" y="163" fill="currentColor">eUP</text>
  <rect x="370" y="144" width="130" height="28" class="f-ok" stroke="currentColor"/><text x="378" y="163" fill="currentColor">aDATA aDATA</text>
  <text x="515" y="163" fill="currentColor">전이가 끝난 뒤 UP 에서 처리</text>
  <text x="10" y="205" fill="currentColor">RTC: 이벤트 하나의 처리(exit·action·entry 전부)가 끝나기 전에는 다음 이벤트를 시작하지 않는다.</text>
  <text x="10" y="223" fill="currentColor">재귀 dispatch 는 "반쯤 바뀐 상태" 에서 표를 읽는다 — A 는 이전 상태로, B 는 entry 전의 새 상태로.</text>
</svg>
```

A 는 액션 시점에 상태가 아직 DOWN 이라 DOWN × DATA(빈 칸)를 읽어 데이터를 버린다. B 는 결과 값은 맞지만 UP 의 entry(워치독 시작) **전에** UP 의 일을 해 버린다 — entry 가 버퍼를 초기화하는 머신이었다면 방금 받은 데이터를 지운다. 게다가 재귀는 스택을 먹는다(MCU 스택은 수 KB). C 는 UML/QP 가 말하는 **run-to-completion** 이다: `busy` 중이면 큐에 넣고, 바깥 dispatch 가 지금 이벤트를 끝낸 뒤 꺼내 처리한다. 큐가 넘치면(`pending = 6`, 큐 4) 2개가 `FSM_EFULL` 로 돌아오고 호출한 쪽이 센다 — 조용히 버리지 않고 **보고** 하는 것이 핵심이다.

> 주의: 이 `busy` 플래그와 큐는 **같은 실행 문맥** 안의 재진입용이다. ISR 과 메인 루프가 같은 FSM 에 동시에 dispatch 하면 `busy` 검사와 대입 사이가 경쟁 구간이 된다(C-B09 의 lost update 와 같은 구조). 그 경우는 Q4 에서.

### Part C — 바이트 스트림 파서

완성된 수신 표 (빈 칸 = 무시, `int` = 내부 전이):

| 상태 \ 이벤트 | SYNC | BYTE | TIMEOUT | DONE | EMPTY | ERROR |
|---|---|---|---|---|---|---|
| HUNT | → LEN | · | · | · | · | · |
| LEN *(entry: len=idx=crc=0)* | int / on_len | int / on_len | → HUNT / err_timeout | → PAYLOAD | → CRC | → HUNT / err_len |
| PAYLOAD | int / on_data | int / on_data | → HUNT / err_timeout | → CRC | · | · |
| CRC | int / on_crc | int / on_crc | → HUNT / err_timeout | → HUNT / deliver | · | → HUNT / err_crc |

설계 포인트:

1. **이벤트 분류는 상태와 무관하게, 의미는 표가 정한다.** `rx_isr` 는 "0xA5 인가 아닌가" 만 본다. 같은 SYNC 가 HUNT 에서는 프레임 시작, PAYLOAD 에서는 데이터, LEN 에서는 길이 165(→ 초과 에러)다. 분류기가 상태를 보기 시작하면 표 밖에 두 번째 상태 머신이 생긴다.
2. **데이터에 따른 분기(guard)는 판정 이벤트로.** "LEN 이 0 이면 CRC 로, 16 넘으면 HUNT 로" 는 표 한 칸 `{next, action}` 으로 표현할 수 없다. 방법은 둘이다: 칸에 guard 함수를 붙여 여러 후보 중 고르기(추가 연습 3 의 희소 리스트), 또는 여기처럼 액션이 판정해서 `DONE/EMPTY/ERROR` 를 다시 넣기. 후자는 모든 분기가 **표의 칸으로 보인다** 는 장점이 있고, Part B 의 RTC 큐가 있어야 안전하다.
3. **내부 전이로 바이트를 먹는다.** 페이로드 바이트마다 exit/entry 가 돌 이유가 없다. 상태가 바뀌는 것은 판정 이벤트뿐이다.

`A5 03 11 22 33 EE` 를 한 바이트씩 (실측 상태 문자열 `"LPPPCH"`):

| 바이트 | 이벤트 | 칸 → 실행 | 큐에서 꺼낸 이벤트 | 상태 |
|---|---|---|---|---|
| A5 | SYNC | HUNT × SYNC → LEN, entry: len=idx=crc=0 | — | L |
| 03 | BYTE | LEN 내부: len=3, crc=crc8(0,03) | DONE → PAYLOAD | P |
| 11, 22 | BYTE | PAYLOAD 내부: buf[idx++], crc 갱신 | — | P, P |
| 33 | BYTE | PAYLOAD 내부: idx==3 | DONE → CRC | C |
| EE | BYTE | CRC 내부: 0xEE == crc | DONE → HUNT, deliver | H |

CRC-8(다항식 0x07, 초기값 0)은 표준 점검값 `crc8("123456789") == 0xF4` 로 확인했다(하네스 첫 CHECK). 차수 8 다항식이고 상수항이 1 이므로 8비트 이내에 갇힌 오류(= 어떤 바이트 하나의 손상)는 전부 잡는다 — 퍼즈 테스트의 "7번째마다 1바이트 손상 → 정확히 그 프레임만 `bad_crc`" 가 그 성질에 기대고 있다.

**타임아웃.** `rx_isr` 가 바이트마다 `idle = 0`, `rx_tick` 이 `idle` 을 올려 정확히 3 이 되는 순간 TIMEOUT 1번. HUNT 의 TIMEOUT 칸은 비어 있으므로 "조용한 회선" 은 에러가 아니다(테스트: 10 tick → `timeouts == 0`). `idle` 은 255 에서 포화시켜 `uint8_t` 가 감겨 다시 3 이 되는 일을 막는다. 프레임 도중 침묵을 잡지 않으면, 반쯤 받은 프레임 뒤에 오는 다음 프레임의 `A5` 가 페이로드로 먹혀 **두 프레임이 연달아** 깨진다.

**재동기화의 한계.** 이 형식은 SYNC 가 데이터에 나올 수 있어서, LEN 이 손상되면 엉뚱한 0xA5 에서 프레임을 시작할 수 있다(그다음은 대부분 CRC 에러로 걸러짐). 그래서 실제 프로토콜은 바이트 스터핑(SLIP, HDLC) 이나 COBS 로 구분자가 데이터에 절대 안 나오게 하거나, 헤더에 별도 CRC 를 둔다.

**어디서 dispatch 하나.** 테스트는 `rx_isr` 안에서 바로 dispatch 한다 — 수신기를 ISR 하나만 건드린다면 괜찮고, 액션이 짧다(바이트 저장, CRC 1회). 그러나 `deliver` 가 무거운 일(패킷 처리, 응답 송신)을 한다면 ISR 은 바이트를 링 버퍼에 넣기만 하고 메인 루프(또는 RTOS 태스크)가 꺼내 dispatch 하는 구조로 바꾼다. 표와 액션은 그대로이고 **dispatch 를 부르는 곳만** 바뀐다 — 엔진/머신 분리의 이점이다.

### switch FSM vs 표 FSM — 실측 비교

같은 버튼 디바운스를 두 방식으로 짰다(`demo_bench.c`, `demo_target.c`). 속도는 이 맥(Apple M6, Apple clang 21.0.0, `-O2`), 5천만 개 랜덤 입력(1/8 DOWN, 1/8 UP, 6/8 tick, xorshift 고정 시드), 입력 하나당 RNG·tick 처리 포함:

```text
run 0: switch 3.51 ns/input   table 3.94 ns/input   same=yes (press 1436516 release 1436515)
run 1: switch 3.33 ns/input   table 3.94 ns/input   same=yes (press 1436516 release 1436515)
run 2: switch 3.37 ns/input   table 3.90 ns/input   same=yes (press 1436516 release 1436515)
```

Cortex-M4 (`--target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb`) 코드 크기, `llvm-nm -S` 실측 (바이트):

| 심볼 | -O2 | -Os | 섹션 |
|---|---|---|---|
| `sw_event` (switch 전체) | 84 | 80 | .text |
| `tb_event` (엔진) | 96 | 94 | .text |
| 액션 4개 (`t_start` `t_stop` `e_press` `e_rel`) | 28 | 28 | .text |
| `tab` (5×3 칸 × 8 B) | 120 | 120 | .rodata |
| `sts` (5 × 8 B) | 40 | 40 | .rodata |

`-O2 -S` 실측 asm 에서 보이는 것:

```text
sw_event:  ldrb r2,[r0] ; subs r2,#1 ; cmp r2,#3 ; bhi ... ; tbb [pc, r2]     ← case 가 바이트 점프 표(TBB)로
           movw r1,#770 ; strh r1,[r0]                                         ← state=2,timer=3 을 16비트 쓰기 1번으로 합침
tb_event:  add.w r2, r0, r0, lsl #1 ; add.w r2, r3, r2, lsl #3 ; ldrb.w r5,[r2, r1, lsl #3]   ← 칸 주소 계산
           cmp r0,#4 ; ... cmp r0,#2 ; ... blx r1                              ← exit 있는 상태(2,4)를 컴파일러가 이미 앎
           ldr r1,[r7,#4] ; cbz r1 ; blx r1                                    ← 전이 액션: 간접 호출
           pop.w {r4-r7,lr} ; bx r1                                            ← entry 는 꼬리 호출
```

| 관점 | switch-case | 표 + 엔진 |
|---|---|---|
| 전체 그림 | 코드 여기저기에 흩어짐 | 한 화면짜리 행렬 = 설계 문서 |
| 처리 안 한 조합 | `default` 나 누락으로 암묵적 | 빈 칸 = 명시적 무시, 덤프·검증 가능 (추가 연습 2) |
| entry/exit | 전이마다 복붙 | 상태 표에 한 번 |
| 머신 1개의 크기 (M4) | **84 B** | 96 + 28 + 160(플래시 데이터) = 284 B |
| 머신 N개 | N × 로직 | 엔진 1개 + N × (표 + 작은 액션) |
| 속도 (M6 실측) | 3.3~3.5 ns/입력 | 3.9 ns/입력 — 둘 다 1 ms tick 대비 무시할 수준 |
| 데이터 의존 분기 | `if` 로 자연스러움 | guard 칼럼이나 판정 이벤트 필요 |
| 컴파일러 최적화 | 상수 전파·쓰기 합치기 가능 | 간접 호출이 인라인을 막음 |
| 디버깅 | 브레이크포인트 | 표 덤프, 전이 훅 하나로 전 머신 추적 (추가 연습 4) |
| 안전 | 함수 포인터 없음 | 깨진 인덱스 = 임의 점프 → 범위 검사 필수, 표는 `const` |

칸이 8바이트인 이유는 `uint8_t next` 뒤에 포인터 정렬용 패딩 3바이트가 붙기 때문이다 (C-B12). 플래시가 빠듯하면 액션을 함수 포인터 대신 `uint8_t` 인덱스로 저장해 칸을 2바이트로 줄인다. 결론: 상태 3~4개, 이벤트 2~3개의 작은 머신 하나는 switch 가 더 작고 빠르고 충분히 읽힌다. 상태·이벤트가 늘거나, 같은 모양의 머신이 여럿이거나, 표를 문서·검증·코드 생성의 원천으로 쓰고 싶으면 표가 이긴다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 51/51 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 51/51, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| IDLE 을 상태 0 으로 두고 빈 칸도 0 | 적지 않은 칸이 전부 "IDLE 로 전이" 가 됨 — 어떤 이벤트든 버튼이 리셋 | 상태 0 예약(`FSM_NO_STATE`), 실제 상태는 1부터 |
| `ev` / `state` 범위 검사 없음 | 표 밖을 `{next, action}` 으로 읽고 쓰레기 포인터 호출 (실측 SIGBUS) | dispatch 맨 앞에서 `FSM_EINVAL` |
| 액션 안에서 dispatch 를 그냥 재귀 호출 | 이전 상태 행을 읽어 이벤트 유실 (demo A: `rx=0`) | busy 플래그 + 큐, RTC |
| 상태를 액션보다 먼저 바꿈 | entry 전에 새 상태의 일을 함 (demo B), UML 순서와 다름 | exit → action → state → entry |
| 타이머 시작/정지를 전이 액션마다 복붙 | 한 군데 빠뜨리면 멈춘 줄 안 타이머가 엉뚱한 상태에서 TIMEOUT | entry/exit 에 한 번만 |
| 하트비트를 내부 전이로 그림 | exit/entry 가 안 돌아 워치독이 재시작되지 않음 → 멀쩡한 링크를 끊음 | `{LINK_UP, on_hb}` 자기 전이 |
| `--b->timer` 를 0 일 때도 실행 | `uint8_t` 0 → 255 로 감겨 255 tick 뒤 유령 TIMEOUT | `b->timer > 0 && --b->timer == 0` |
| PAYLOAD 행의 SYNC 칸을 비움 | 데이터 속 0xA5 가 무시되어 길이가 어긋남 | SYNC 칸도 `rx_on_data` |
| `tab[NS][NE]` 를 `&tab[0][0]` 로 펴서 `[s*NE+e]` | 첫 행 밖 첨자 = UB (C11 J.2) | 1차원 표 + `FSM_CELL` 매크로 |
| ISR 과 main 이 같은 FSM 에 dispatch | `busy`·큐·상태 갱신이 경쟁 | 한 문맥에서만 dispatch, ISR 은 큐에 post (Q4) |

## 8. 면접 Q&A

<details>
<summary>Q1. switch-case FSM 과 표 기반 FSM, 언제 무엇을 쓰나요?</summary>

작은 머신 하나(상태 ≤ 4~5, 분기에 데이터 조건이 많음)는 switch 가 작고 빠르고 디버깅이 쉽다 — Cortex-M4 실측으로 switch 84 B vs 표 방식 284 B. 상태·이벤트가 많거나, 같은 엔진으로 여러 머신을 돌리거나, 표를 스펙 문서·자동 검증·코드 생성기와 공유하고 싶으면 표가 낫다. 꼬리질문 "성능은?" → 간접 호출 1~3번이라 이벤트당 수 ns 차이(실측 3.4 vs 3.9 ns)이고, 이벤트가 ms 단위로 오는 펌웨어에서는 의미 없다. 진짜 차이는 **유지보수와 검증 가능성** 이다.

</details>

<details>
<summary>Q2. entry/exit 액션이 왜 필요하죠? 자기 전이에서 exit/entry 가 도는 게 맞나요?</summary>

"이 상태에 있는 동안" 의 자원(타이머, LED, 클럭, 인터럽트 enable)을 그 상태가 소유하게 만든다. 들어오는 길·나가는 길이 몇 개든 한 곳에서 켜고 끄므로 누락이 없다. 자기 전이(external self-transition)는 상태를 나갔다 다시 들어오는 것이라 exit/entry 가 도는 게 UML 의미론이고, 그게 필요할 때(워치독 재시작)를 위해 존재한다. 상태 유지 + 액션만 원하면 **내부 전이** 로 그린다. 둘을 구분하지 않는 엔진은 둘 중 하나를 표현할 수 없다.

</details>

<details>
<summary>Q3. 액션 안에서 새 이벤트가 생기면 어떻게 처리하나요?</summary>

바로 재귀 dispatch 하면 "전이 중간 상태" 에서 표를 읽는다 — 상태 대입 전이면 이전 상태 행(이벤트 유실), 대입 후면 entry 전에 새 상태가 일함(demo_reentrancy 실측 A/B). 정답은 run-to-completion: 큐에 넣고 현재 이벤트의 exit/action/entry 가 다 끝난 뒤 처리한다. 꼬리질문 "큐가 차면?" → 조용히 버리지 말고 에러를 돌려주고 센다, 큐 크기는 최악 연쇄 깊이로 정한다(여기선 Part C 의 판정 이벤트 1개라 1이면 충분, 여유로 4). QP 프레임워크의 active object 가 이 모델이다: 객체마다 이벤트 큐 + RTC 디스패치.

</details>

<details>
<summary>Q4. ISR 에서 fsm_dispatch 를 불러도 되나요?</summary>

그 FSM 을 **오직 그 ISR 만** 건드리고 액션이 짧다면 된다(Part C 의 바이트 수집). 하지만 같은 FSM 에 메인 루프도 dispatch 한다면, ISR 이 메인의 dispatch 한가운데(상태 대입 직전)에 끼어들어 표를 읽게 된다 — `busy` 플래그도 "검사 후 대입" 사이가 비원자적이라 지켜 주지 못한다. 표준 패턴: ISR 은 이벤트를 lock-free SPSC 링 버퍼(또는 RTOS 큐 `xQueueSendFromISR`)에 넣기만 하고, 메인 루프/태스크 하나가 꺼내 dispatch 한다. 그러면 FSM 은 단일 문맥에서만 실행되고 락이 필요 없다. 꼭 ISR 에서 직접 해야 하면 dispatch 전체를 임계 구역으로 감싼다(인터럽트 지연이 늘어남). 공유 데이터 원자성은 C-M14.

</details>

<details>
<summary>Q5. 상태 변수가 메모리 손상으로 이상한 값이 되면 어떻게 되나요? 안전 규격에서는?</summary>

switch 는 `default` 로 빠지지만, 함수 포인터 표는 그 값으로 주소를 계산해 **임의 위치를 함수로 호출** 한다 (실측: 검사 없는 엔진에 `ev = 7` → SIGBUS). 방어: ① dispatch 입구에서 `state`/`ev` 범위 검사 ② 표와 상태 표는 `const` → 플래시라 런타임에 덮이지 않음 ③ `EINVAL` 이면 안전 상태 + 에러 로그 + 필요하면 워치독 리셋 ④ 중요 상태 값은 보수(~x) 사본을 함께 저장해 일치 검사. MISRA C 는 함수 포인터 자체를 금지하진 않지만 변환·타입 일치를 엄격히 요구하므로 모든 액션을 같은 typedef 로 맞추고 캐스트하지 않는다.

</details>

<details>
<summary>Q6. 버튼을 EXTI 엣지 인터럽트로 받는 게 좋은가요, 주기 샘플링이 좋은가요?</summary>

튀는 스위치를 엣지 인터럽트에 바로 물리면 튐 한 번에 인터럽트가 수십 번 들어올 수 있다(인터럽트 폭주). 그래서 흔한 선택은 ① 1~5 ms 주기 타이머에서 핀을 샘플링하고 "레벨이 바뀌었을 때만" DOWN/UP 이벤트를 만들기, 또는 ② EXTI 를 깨우기(저전력 wake-up)에만 쓰고 첫 엣지 후 EXTI 를 끄고 타이머로 디바운스. 이 레슨의 FSM 은 이벤트 출처를 모르므로 ①로 바꿔도 표는 그대로다. 하드웨어 대안: RC 필터 + 슈미트 트리거, 일부 MCU 의 입력 글리치 필터.

</details>

<details>
<summary>Q7. 모든 상태에 "TIMEOUT → HUNT" 같은 공통 전이가 반복되면요?</summary>

평면 FSM 의 한계다 — Part C 표에서 TIMEOUT 칸 3개가 같다. 계층 상태 머신(HSM)은 LEN/PAYLOAD/CRC 를 "RECEIVING" 이라는 상위 상태 안에 넣고, 공통 전이를 상위 상태에 한 번만 둔다. 하위 상태가 처리하지 않은 이벤트는 상위로 올라간다. QP 의 QHsm, Zephyr 의 SMF(State Machine Framework, `CONFIG_SMF_ANCESTOR_SUPPORT`)가 이를 지원한다. 평면 표로 버틸 때는 "기본 행" 을 하나 두고 엔진이 빈 칸이면 기본 행을 보게 하는 정도로 흉내 낸다.

</details>

<details>
<summary>Q8. 상태 50개, 이벤트 30개면 표가 1500칸인데 대부분 비어요. 어떻게 하죠?</summary>

밀집 행렬 대신 **희소 전이 리스트** `{state, event, guard, next, action}` 를 상태별로 정렬해 두고 그 상태의 행만 선형 탐색한다(추가 연습 3). 메모리는 실제 전이 수에 비례하고, guard 도 자연스럽게 들어간다. 대가는 O(1) 조회가 O(그 상태의 전이 수) 가 되는 것 — 보통 몇 개라 무시할 만하다. 또는 계층화(Q7)로 공통 전이를 접어 표 자체를 줄인다.

</details>

## 9. 추가 연습

### 연습 1 — switch 로 다시 쓰고 동치 확인

같은 버튼 FSM 을 switch-case 로 작성하고, xorshift32 고정 시드 10,000 개 랜덤 입력(DOWN/UP/tick)에서 매 입력마다 표 버전과 상태·타이머·press/release 수가 같은지 확인하라.

<details>
<summary>답 보기</summary>

`practice.c` 의 `sw_event`. entry/exit 가 없으니 MAYBE 로 들어가는 두 전이에 `timer = DEB`, 나오는 네 전이에 `timer = 0` 을 **손으로** 넣어야 한다 — 하나라도 빠뜨리면 퍼즈가 바로 잡는다(타이머 값을 비교하므로). 이런 "두 구현 교차 검증" 은 표를 바꿀 때 회귀 테스트로도 쓴다. `default:` 는 깨진 상태 값을 무시하는 방어 코드.

</details>

### 연습 2 — 표 검증기

`fsm_validate(tab, n_states, n_events, initial, &unreach)` 를 작성하라: 범위 밖 `next` 가 있으면 `V_BAD_NEXT`, 초기 상태에서 도달할 수 없는 상태가 있으면 `V_UNREACHABLE` 과 그 상태들의 비트마스크.

<details>
<summary>답 보기</summary>

모든 칸의 `next < n_states` 검사 + 표를 그래프로 보고 초기 상태에서 고정점까지 반복(BFS 와 같은 결과)으로 도달 집합을 구한다. 버튼 표에 LONG_HELD 를 상태로만 추가하고 칸을 안 만들면 `unreach == 1u << LONG_HELD` 로 잡힌다. 펌웨어에서는 `fsm_init` 에서 한 번 돌리고 실패하면 assert — 표가 데이터라서 가능한 정적 검사다. switch FSM 에서는 같은 검사를 하려면 코드 분석기가 필요하다.

</details>

### 연습 3 — guard 가 있는 희소 전이 리스트 (동전 개찰구)

LOCKED/UNLOCKED, 이벤트 COIN(금액)/PUSH. 요금 50. LOCKED 에서 COIN 은 누적 금액이 50 이상이면 UNLOCKED(잔액 이월), 아니면 적립만. LOCKED 에서 PUSH 는 경보. `{state, ev, guard, next, action}` 리스트와 "위에서부터 첫 일치" 디스패처로 구현하라.

<details>
<summary>답 보기</summary>

같은 `(LOCKED, COIN)` 에 행이 두 개다: guard `enough` 가 있는 행이 **먼저**, guard 없는 적립 행이 뒤. 순서를 바꾸면 guard 없는 행이 항상 먼저 잡혀 개찰구가 영원히 안 열린다 — 희소 리스트의 대표 버그. 10+10+10+25 → 55 ≥ 50 → UNLOCKED, 잔액 5. Part C 의 LEN 판정도 이 방식으로 `(LEN, BYTE, len_ok) → PAYLOAD`, `(LEN, BYTE, len_zero) → CRC`, `(LEN, BYTE, NULL) → HUNT` 로 쓸 수 있다.

</details>

### 연습 4 — 전이 기록 링 버퍼

엔진에 전이 훅 `void (*hook)(fsm_t *, from, ev, to)` 을 추가하고, 마지막 8개 전이를 링 버퍼에 남겨 오래된 순으로 덤프하라. 전이 10번 후 결과는?

<details>
<summary>답 보기</summary>

`g_hist[count % 8]` 에 쓰고 `count` 는 계속 증가, 덤프는 `first = count - min(count, 8)` 부터. 전이 10번(`DtttUtttDtttUtttDttt`) 뒤 `"P>u u>I I>d d>P P>u u>I I>d d>P"` — 1·2번째가 밀려났다. 실제 제품에서는 이 버퍼를 `.noinit` RAM 에 두어 HardFault·워치독 리셋 후에도 읽는다(post-mortem). 엔진이 하나라 훅 하나로 **모든 머신** 을 추적할 수 있다 — switch FSM 이라면 상태 대입마다 로그 코드를 넣어야 한다.

</details>

### 연습 5 — 길게 누르기(long press): 기능 추가 = 행 추가

PRESSED 에서 5 tick 더 유지되면 LONG_HELD 로 가며 `'L'` 을 내보내라. 코드 수정 없이 표 두 행과 상태 표 한 줄만 바꿀 수 있나?

<details>
<summary>답 보기</summary>

`[PRESSED × TIMEOUT] = {LONG_HELD, e_long}`, `[LONG_HELD × UP] = {MAYBE_R, NULL}` 두 행 추가, PRESSED 의 entry/exit 를 `t_long`/`t_stop` 으로. 타이머는 한 번에 한 상태만 쓰므로 하나를 공유한다. `"Dttt ttttt Uttt"` → `"dddP PPPPL uuuI"`, 출력 `"PLR"`. 4 tick 만 누르면 `"PR"`. 뗄 때 튀어 MAYBE_R → PRESSED 로 돌아오면 PRESSED 의 entry 가 long 타이머를 **새로** 건다 — 원하는 동작인지(튐 때문에 long 판정이 늦어짐)는 스펙이 정할 문제이고, 표에서 그 칸이 바로 보인다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 18/18 passed
```

## 10. 핵심 정리

- 표 FSM = **state × event → {next, action}** 행렬 + 상태별 **{entry, exit}** + 머신을 모르는 엔진. 제어 흐름이 `const` 데이터(플래시)가 된다.
- 상태 0 을 예약하면 지정 초기화자로 적지 않은 칸이 자동으로 "무시" — 표에는 의미 있는 칸만 적는다.
- 전이 순서: **exit(현재) → 액션 → state = next → entry(next)**. 자기 전이는 exit/entry 가 돌고, 내부 전이 `{0, act}` 는 안 돈다.
- 입구에서 `ev`/`state` 범위 검사 — 표 FSM 에서 깨진 인덱스는 임의 주소 호출이다 (실측 SIGBUS).
- 액션이 만든 이벤트는 재귀하지 말고 큐 → **run-to-completion** (재귀는 이벤트 유실 또는 entry 전 처리).
- 데이터 의존 분기는 guard 칼럼(희소 리스트) 또는 판정 이벤트(DONE/EMPTY/ERROR)로 — 분기도 표에 보이게.
- switch 는 작고 빠르다(M4 84 B vs 284 B, 3.4 vs 3.9 ns). 표는 머신이 크거나 많을 때, 검증·덤프·추적이 필요할 때 이긴다.

## 참고자료

- C11 (N1570) 6.7.9 Initialization (p19 지정 초기화자에서 빠진 원소, p10 정적 초기화 값), 6.5.6p8 포인터 산술 범위, Annex J.2 (`a[1][7]` 예) — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- OMG UML 2.5.1 — 14장 State Machines (전이 실행 순서, internal/external/local transition, run-to-completion) — <https://www.omg.org/spec/UML/2.5.1/>
- Miro Samek, *Practical UML Statecharts in C/C++* 2nd ed. 와 QP/C 프레임워크 (active object, RTC, 계층 상태 머신) — <https://www.state-machine.com/>
- Zephyr Project — State Machine Framework (SMF) — <https://docs.zephyrproject.org/latest/services/smf/index.html>
- Jack Ganssle, "A Guide to Debouncing" — 스위치 튐 측정과 디바운스 방법 — <https://www.ganssle.com/debouncing.htm>
- Stuart Cheshire, Mary Baker, "Consistent Overhead Byte Stuffing" (COBS), 그리고 RFC 1055 (SLIP) — 구분자 기반 프레이밍
- CRC-8 (poly 0x07) 매개변수와 점검값 — Greg Cook, *Catalogue of parametrised CRC algorithms* — <https://reveng.sourceforge.io/crc-catalogue/1-15.htm>
- 관련 레슨: C-M06 (함수 포인터·콜백·ops 구조체), C-B12 (칸 크기와 패딩), C-B09 (volatile 과 lost update), C-M05 (패킷 직렬화·CRC 자리), C-M14 (ISR 공유 변수·원자성)
