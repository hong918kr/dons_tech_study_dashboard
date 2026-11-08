---
id: DS-M06
time: 90분
updated: 2026-11-07
---
# DS-M06 해시 타이머 휠

## 학습 목표

- 슬롯 256개 + 라운드 카운터(rounds) 타이머 휠에서 **등록·취소 O(1)** 이 왜 성립하는지, `slot = expire & 255`, `rounds = (delay − 1) >> 8` 을 유도해서 설명한다.
- delay 0, delay = 256의 배수, 32비트 tick 넘침(wraparound) 같은 경계에서 **정확히 `now + delay` tick 에 한 번** 실행되는 구현을 만든다.
- 콜백 안에서 취소·재등록(re-arm)·delay 0 등록이 일어나도 안전한 **2단계 tick 처리**(만료 목록 분리 → 하나씩 실행)를 구현하고, 단일 패스가 왜 깨지는지 재현한다.
- 같은 tick 에 만료되는 타이머의 순서를 **(expire, 등록 순번 seq) = FIFO** 로 정의해 min-heap 참조 구현과 실행 로그가 한 줄도 다르지 않음을 랜덤 테스트로 보인다.
- 단일 레벨 휠의 약점(긴 delay 의 방문 비용)을 측정하고, 계층 휠(hierarchical wheel)·힙과의 선택 기준을 말할 수 있다.

## 1. 문제

네트워크 스택의 재전송 타임아웃, 드라이버의 "응답 없음" 타임아웃, 워치독 펫(pet) 같은 타이머는 **대부분 만료되기 전에 취소되거나 다시 걸린다** — 그래서 등록·취소가 O(1)인 타이머 휠이 Linux 커널, Netty, Kafka 같은 곳의 표준 답이고, 면접 "타이머 매니저를 설계하라"의 핵심 꼬리질문이다.

DS-B12 에서 본 이진 힙은 등록 O(log n), 취소는 위치 맵이 있어야 O(log n)이다. **해시 타이머 휠(hashed timing wheel)** 은 시간을 해시 키로 쓴다: 슬롯(slot) 256개짜리 원형 배열을 두고, 만료 시각 `expire` 의 하위 8비트로 슬롯을 고른다. 256 tick 보다 먼 타이머는 같은 슬롯을 여러 바퀴 지나야 하므로 **"앞으로 몇 바퀴 더 지나칠지" = rounds** 를 같이 적어 둔다. 매 tick 마다 커서(cursor)가 슬롯 하나만 보고, 그 슬롯의 타이머 중 rounds 가 0인 것만 실행하고 나머지는 rounds 를 1 줄인다.

각 슬롯은 **침입형 원형 이중 연결 리스트**(DS-B06의 sentinel 리스트, DS-M01에서 자세히)다. 타이머 객체 안에 링크가 들어 있어(intrusive) malloc 이 없고, 취소는 자기 링크를 떼는 포인터 4개 수정 = O(1)이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">now = 1000 → 커서 슬롯 232. 슬롯 번호 = expire 의 하위 8비트</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="60" width="70" height="34"/><rect x="90" y="60" width="70" height="34"/>
    <rect x="200" y="60" width="70" height="34"/><rect x="270" y="60" width="70" height="34"/>
    <rect x="380" y="60" width="70" height="34"/><rect x="490" y="60" width="70" height="34"/>
    <rect x="600" y="60" width="70" height="34"/>
  </g>
  <rect x="200" y="60" width="70" height="34" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="55" y="82">[0]</text><text x="125" y="82">[1]</text><text x="235" y="82">[232]</text><text x="305" y="82">[233]</text>
    <text x="415" y="82">[235]</text><text x="525" y="82">[240]</text><text x="635" y="82">[255]</text>
  </g>
  <g fill="currentColor"><text x="168" y="82">…</text><text x="350" y="82">…</text><text x="460" y="82">…</text><text x="570" y="82">…</text><text x="680" y="82">…</text></g>
  <g fill="currentColor"><text x="196" y="50">커서 = now</text><text x="275" y="50">다음 tick 에 볼 슬롯 →</text></g>
  <g stroke="currentColor" fill="none">
    <path d="M305 94 L305 120"/><path d="M305 154 L305 180"/>
    <path d="M415 94 L415 120"/>
    <path d="M525 94 L525 120"/><path d="M525 154 L525 180"/><path d="M525 214 L525 240"/>
  </g>
  <g stroke="currentColor">
    <rect x="255" y="120" width="100" height="34" class="f-ok"/><rect x="255" y="180" width="100" height="34" class="f-mut"/>
    <rect x="365" y="120" width="100" height="34" class="f-ok"/>
    <rect x="475" y="120" width="100" height="34" class="f-mut"/><rect x="475" y="180" width="100" height="34" class="f-ok"/>
    <rect x="475" y="240" width="100" height="34" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="305" y="135">T1 exp 1001</text><text x="305" y="149">rounds 0</text>
    <text x="305" y="195">T4 exp 1257</text><text x="305" y="209">rounds 1</text>
    <text x="415" y="135">T2 exp 1003</text><text x="415" y="149">rounds 0</text>
    <text x="525" y="135">T5 exp 1520</text><text x="525" y="149">rounds 2</text>
    <text x="525" y="195">T3 exp 1008</text><text x="525" y="209">rounds 0</text>
    <text x="525" y="255">T6 exp 1264</text><text x="525" y="269">rounds 1</text>
  </g>
  <g fill="currentColor">
    <text x="20" y="300">tick 1001: 슬롯 233 을 본다 → T1 실행 (rounds 0), T4 는 rounds 1 → 0 으로 줄이고 남는다 (256 tick 뒤 실행)</text>
    <text x="20" y="320">슬롯 안 순서 = 등록 순서(꼬리에 붙임). 같은 슬롯에 rounds 가 다른 타이머가 섞여 있어도 된다</text>
  </g>
</svg>
```

타이머 객체(`tw_timer_t`)는 `link`, 절대 만료 시각 `expire`, `rounds`, 등록 순번 `seq`, 콜백 `cb`, `id`, `armed` 를 가진다. 휠(`tw_wheel_t`)은 슬롯 sentinel 256개와 `now`(마지막으로 처리한 tick), `seq` 카운터, `count`(armed 개수)뿐이다. 타이머는 정적 배열 `g_tm[4096]` — malloc 없음.

**시간 규칙(이 레슨의 계약)**: `tw_add(w, t, delay)` 는 `now + delay` 번째 tick 처리에서 콜백을 부른다. `tw_tick()` 은 `now++` 후 그 tick 의 슬롯을 처리한다. delay 0 은 1로 올린다 — add 안에서 콜백을 부르지 않고 "다음 tick" 에 실행한다. 같은 tick 에 만료되는 타이머끼리는 **등록(또는 마지막 재등록) 순서**로 실행한다.

### Part A — 등록과 tick (슬롯 + 라운드)

- `tw_add(w, t, delay)` — `delay > TW_MAX_DELAY(2^31−1)` 면 `false`. delay 0 → 1. 이미 armed 면 먼저 떼고(옮겨 걸기, Linux `mod_timer` 와 같은 의미) `expire = now + delay`, `rounds = (delay − 1) >> 8`, `seq = w->seq++`, 슬롯 `expire & 255` 의 **꼬리**에 붙인다.
- `tw_tick(w)` — `now++`, 슬롯 `now & 255` 를 훑어 rounds 0 은 실행, 나머지는 rounds−−.
- 테스트: delay 1, 2, 255, **256**, 257, 511, **512**, 513, 768, 1000, 70000 이 각각 정확히 `start + delay` 에 한 번만 실행. delay 0 은 add 안에서 실행되지 않고 다음 tick 에. 같은 tick 만료 3개(rounds 가 다른 것 포함)는 등록 순서대로.

### Part B — 취소와 콜백 안 재진입

- `tw_cancel(w, t)` — armed 면 떼고 `true`, 아니면 `false`(이미 만료됐거나 취소됨). O(1).
- `tw_tick` 을 **콜백 재진입에 안전**하게: 콜백이 ① 같은 tick 에 만료 대기 중인 다른 타이머를 취소, ② 자기를 취소, ③ 자기를 delay 256 으로 재등록(같은 슬롯!), ④ delay 0 으로 등록, ⑤ 같은 tick 의 다른 타이머를 옮겨 걸기 — 모두 올바르게 동작해야 한다.
- 테스트: 32비트 넘침 구간(now = 0xFFFFFF00)에서 취소·옮겨 걸기, 위 ①~⑤ 각각의 고정 시나리오.

### Part C — 수천 개 타이머를 min-heap 참조와 대조 + tickless

- `tw_next_expiry(w)` — 다음 만료까지 남은 tick 수(없으면 0). tickless idle 에서 "이만큼 자도 된다"를 계산하는 함수.
- 타이머 4096개, 20000 tick, 시작 now = 0xFFFFE000(도중에 넘침). 같은 시드의 같은 스크립트(등록·짝 등록·취소·delay 0·범위 밖 delay·콜백 안 재등록/취소/옮겨 걸기)를 **휠**과 **(expire, seq) min-heap 참조 구현**에 각각 돌려 **(tick, id) 실행 로그 전체**와 최종 상태를 비교한다. 휠은 매 연산 후 불변식 검사 `tw_check()`, 매 step `tw_next_expiry()` 를 brute-force 최소값과 대조.
- 측정(`demo_wheel_vs_heap.c`): delay 범위별 방문 비용, 휠 vs indexed heap 시간.

## 2. Problem (English)

> **Hashed timing wheel with O(1) start/stop.**
>
> Implement a single-level timer wheel with 256 slots. Each slot is an intrusive circular doubly-linked list; timers are caller-owned objects (no dynamic allocation). A timer stores its absolute expiry tick and a remaining-rounds counter.
>
> 1. `bool tw_add(tw_wheel_t *w, tw_timer_t *t, uint32_t delay)` — arm (or re-arm) `t` to fire during the tick numbered `now + delay`. `delay == 0` behaves as `delay == 1` (never fire inside `tw_add`). Return `false` if `delay > 2^31 - 1`. O(1).
> 2. `bool tw_cancel(tw_wheel_t *w, tw_timer_t *t)` — O(1); `false` if not armed.
> 3. `void tw_tick(tw_wheel_t *w)` — advance `now` by one and run every timer due at the new `now`. Callbacks may call `tw_add`/`tw_cancel` on any timer, including the one running and others due in the same tick.
> 4. `uint32_t tw_next_expiry(tw_wheel_t *w)` — ticks until the next expiry, 0 if none.
> 5. Timers due in the same tick fire in arming order (FIFO). Verify against a binary min-heap ordered by `(expire, seq)` over thousands of random operations, including 32-bit tick wraparound.
>
> Example: at `now = 0`, arm A(3), B(259), C(256), D(0). Fires: D at 1, A at 3, C at 256, B at 259.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 이 Mac: 27.2 SDK 링크가 깨져 있음
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "4/27 passed", exit 1 — TODO 4곳(tw_add, tw_cancel, tw_tick, tw_next_expiry)을 채워 27/27 을 만든다
```

주어지는 것: 침입형 리스트 4함수(`link_init/empty/add_tail/del`) + `container_of`, 불변식 검사기 `tw_check()`, 참조 min-heap 모델, 같은 스크립트를 두 구현에 돌리는 `model_t` 인터페이스, 고정 시나리오용 콜백들, `CHECK` 하네스. 시작 코드의 `tw_tick` 은 `now++` 만 한다 — 타이머가 하나도 안 걸리므로 크래시 없이 FAIL 만 난다.

## 4. 힌트

<details>
<summary>힌트 1 — 슬롯 번호는 왜 now 가 아니라 expire 의 하위 비트인가</summary>

커서는 tick `T` 에서 슬롯 `T & 255` 를 본다. 그러면 `expire` 에 실행될 타이머는 슬롯 `expire & 255` 에 있어야 그 tick 에 커서가 지나간다. 2^32 가 256 의 배수라서 `now` 가 0xFFFFFFFF → 0 으로 넘어갈 때도 하위 8비트는 255 → 0 으로 자연스럽게 이어진다(연습 3에서 250 슬롯과 비교).

</details>

<details>
<summary>힌트 2 — rounds 공식: delay 256 은 몇 바퀴?</summary>

`d = delay` 라 하자. 커서가 이 슬롯에 오는 시점은 `now + k` (k ≡ d mod 256, k ≥ 1)이다. 첫 방문은 `k₀ = d mod 256` (0이면 256), 그 뒤 256씩. 실행은 k = d 일 때이므로 그 전에 "지나치는" 횟수는 `(d − k₀) / 256 = (d − 1) / 256`. d = 256 → 0 (첫 방문 = 256번째 tick 에 바로 실행), d = 257 → 1, d = 512 → 1. `d >> 8` 로 쓰면 256의 배수에서 한 바퀴 늦게 실행된다.

</details>

<details>
<summary>힌트 3 — 콜백 중에 리스트가 바뀐다</summary>

`list_for_each_safe` 처럼 `next` 를 미리 저장하는 순회는 "지금 노드"를 지우는 것만 안전하다. 콜백이 *다음* 노드를 취소하거나, 같은 슬롯에 새 노드를 꼬리로 붙이면 저장해 둔 `next` 가 거짓말을 한다. 콜백을 부르기 전에 이번 tick 에 실행할 것을 **전부 다른 리스트(due)로 옮겨 두고**, 그 리스트에서 머리를 하나씩 떼어 실행하라.

</details>

<details>
<summary>힌트 4 — next_expiry 를 O(n) 전체 탐색보다 빨리</summary>

슬롯 `now+1, now+2, …` 순서로 보면, k번째 슬롯의 타이머까지 거리는 `rounds·256 + k` 다. rounds 0 인 타이머를 처음 만난 슬롯 k 에서 멈춰도 된다 — 뒤 슬롯은 모두 k 보다 멀고, 앞 슬롯의 타이머는 rounds ≥ 1 이라 256 보다 멀다.

</details>

## 5. 풀이

### Part A — 등록: 슬롯과 rounds 를 정하는 두 줄

```c
t->expire = w->now + delay;          /* unsigned: 2^32 를 법으로 정의된 덧셈 (C11 6.2.5p9) */
t->rounds = (delay - 1u) >> TW_BITS; /* delay >= 1 이 보장된 뒤 */
link_add_tail(&w->slot[t->expire & TW_MASK], &t->link);
```

```svg
<svg viewBox="0 0 740 272" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">now = 0 에서 걸었을 때: 커서가 그 슬롯을 지나는 tick(점)과 실행 tick(별표)</text>
  <g stroke="currentColor"><path d="M120 200 L720 200"/></g>
  <g fill="currentColor" font-family="monospace" font-size="12" text-anchor="middle">
    <text x="120" y="218">0</text><text x="270" y="218">256</text><text x="420" y="218">512</text><text x="570" y="218">768</text>
  </g>
  <g stroke="currentColor" stroke-dasharray="3 3"><path d="M270 40 L270 200"/><path d="M420 40 L420 200"/><path d="M570 40 L570 200"/></g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="10" y="58">delay 255</text><text x="10" y="88">delay 256</text><text x="10" y="118">delay 257</text>
    <text x="10" y="148">delay 512</text><text x="10" y="178">delay 513</text>
  </g>
  <g class="f-ok" stroke="currentColor">
    <rect x="263" y="47" width="12" height="12"/>
    <rect x="264" y="77" width="12" height="12"/>
    <rect x="415" y="137" width="12" height="12"/>
  </g>
  <g class="f-mut" stroke="currentColor">
    <circle cx="121" cy="113" r="5"/><circle cx="271" cy="143" r="5"/><circle cx="121" cy="173" r="5"/><circle cx="271" cy="173" r="5"/>
  </g>
  <g class="f-ok" stroke="currentColor"><rect x="265" y="107" width="12" height="12"/><rect x="415" y="167" width="12" height="12"/></g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="600" y="58">slot 255, rounds 0</text><text x="600" y="88">slot 0,   rounds 0</text>
    <text x="600" y="118">slot 1,   rounds 1</text><text x="600" y="148">slot 0,   rounds 1</text><text x="600" y="178">slot 1,   rounds 2</text>
  </g>
  <g fill="currentColor"><text x="10" y="242">원 = 방문했지만 rounds &gt; 0 이라 rounds-- 만 함, 네모(초록) = 실행</text>
    <text x="10" y="262">방문 횟수 = rounds + 1 = (delay − 1)/256 + 1</text></g>
</svg>
```

그림에서 delay 257 은 tick 1에 슬롯 1을 지나며(rounds 1 → 0) 257 에 실행, delay 512 는 256 에 한 번 지나고 512 에 실행된다. **delay 256 은 슬롯 0 = 지금 막 처리한 커서 슬롯**에 들어간다 — 이번 tick 은 이미 끝났으니 다음 방문(256 tick 뒤)이 실행 시점이고, 그래서 rounds 는 0 이어야 한다.

**왜 꼬리에 붙이나**: seq 가 단조 증가하므로 꼬리 삽입이면 각 슬롯 리스트가 항상 seq 오름차순이다. tick 에서 rounds 0 인 것만 골라내도 부분 수열이므로 순서가 유지된다 → "같은 tick 은 FIFO" 가 공짜로 된다. 머리에 붙이면(LIFO) 같은 tick 의 순서가 뒤집혀 참조 힙과 로그가 달라진다(직접 바꿔 보면 `tw_check` 의 seq 오름차순 검사와 Part A FIFO 테스트가 먼저 터진다).

**같은 tick 순서(tie-break) 정의**: 힙은 안정(stable) 정렬이 아니라 `expire` 만 비교하면 같은 tick 의 순서가 힙 모양에 따라 바뀐다(DS-B12 실수 표). 그래서 참조 힙의 키를 `(expire, seq)` 전순서로 두고, 휠은 위의 꼬리 삽입으로 같은 순서를 만든다. 재등록은 새 seq 를 받으므로 "마지막으로 건 순서"다. Part A 테스트: T5 를 300 에(rounds 1), 44 tick 뒤 T3·T9 를 256 뒤(= 300, rounds 0)에 걸면 슬롯 44 리스트는 `[T5(r1), T3(r0), T9(r0)]` 이고 tick 300 실행 순서는 5, 3, 9 — seq 순서와 같다.

**단계별 추적** (now = 0, 문제의 예):

| 연산 | expire | slot | rounds | 슬롯 리스트 상태 |
|---|---|---|---|---|
| add A, 3 | 3 | 3 | 0 | s3: A |
| add B, 259 | 259 | 3 | (258)>>8 = 1 | s3: A, B |
| add C, 256 | 256 | 0 | 255>>8 = 0 | s0: C |
| add D, 0 → 1 | 1 | 1 | 0 | s1: D |
| tick → now 1 | | | | s1: D 실행 |
| tick → now 3 | | | | s3: A 실행, B rounds 1→0 |
| tick → now 256 | | | | s0: C 실행 |
| tick → now 259 | | | | s3: B 실행 |

**불변식(invariant)** — tick 과 tick 사이에 armed 타이머 t 는 항상:

1. `t` 는 슬롯 `t->expire & 255` 리스트에 있고 링크가 일관(`p->next->prev == p`)
2. `d = t->expire − now` (unsigned 차) 가 `1 ≤ d ≤ 2^31 − 1`
3. `t->rounds == (d − 1) >> 8`
4. 슬롯 안 seq 오름차순, 전체 노드 수 == `count`

**tick 이 3을 지키는 증명**: `now` 가 1 늘면 모든 d 가 1 준다. 새 거리를 D 라 하면(이전 d = D+1, 이전 rounds = D >> 8)

- 커서 슬롯의 타이머: `expire ≡ now (mod 256)` 이므로 D 는 256의 배수. D = 0 이면 rounds 0 → 실행(빠짐). D ≥ 256 이면 rounds = D/256 ≥ 1 → 1 줄이면 D/256 − 1 = (D − 1) >> 8. ✓
- 다른 슬롯의 타이머: D 가 256의 배수가 아니므로 `D >> 8 == (D − 1) >> 8` — 손대지 않아도 그대로 성립. ✓

그래서 tick 은 **커서 슬롯 하나만** 보면 된다. 이게 휠의 핵심이다. `tw_check()` 는 이 네 줄을 그대로 코드로 옮긴 것이고, 고정 테스트에서는 매 tick, 랜덤 테스트에서는 **매 연산 후** 돈다.

### Part B — 취소 O(1), 그리고 2단계 tick

취소는 `link_del` 한 번이다. 떼어낸 노드는 **자기 자신을 가리키게(self-loop)** 해 둔다 — DS-B06 처럼 NULL 로 지우면 실수로 다시 떼었을 때 바로 터지는 장점이 있지만, 여기서는 `armed` 플래그가 이중 취소를 막고, self-loop 는 "떼어진 노드를 또 del 해도 무해"하게 만든다. (Linux `timer_list` 는 `hlist_node` 의 `pprev = NULL` 로 "안 걸림"을 표시하고 `timer_pending()` 이 그것을 본다.)

어려운 것은 tick 이다. **콜백은 아무 타이머나 건드릴 수 있다.** next 를 저장하는 단일 패스 순회가 깨지는 두 경우(연습 4에서 실제로 재현):

| 콜백이 하는 일 | 단일 패스(next 저장 후 바로 콜백) | 결과 |
|---|---|---|
| 같은 tick 의 다음 타이머 B 를 취소 | 저장해 둔 next = B 로 이동 → **취소된 B 실행** | 연습 4: B 가 49번 실행(self-loop 로 guard 까지 반복) |
| 자기를 delay 256 으로 재등록 | 같은 슬롯 꼬리에 rounds 0 으로 붙음 → 뒤에 이웃이 있으면 **같은 tick 에 또 실행** | 연습 4: 한 tick 에 2번 실행 |
| 같은 tick 의 다음 타이머를 옮겨 걸기 | 저장한 next 가 다른 슬롯으로 이사 → 순회가 **다른 슬롯 리스트로 넘어감** | 엉뚱한 타이머 처리, 원래 sentinel 을 못 만나 그 슬롯의 sentinel 을 타이머로 착각(UB) |

2단계 처리로 셋 다 해결된다:

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">tw_tick: now 999 → 1000, 슬롯 232 = [A r0, B r1, C r0, D r0]</text>
  <g fill="currentColor"><text x="10" y="58">전</text><text x="10" y="138">1단계 후</text><text x="10" y="228">2단계</text></g>
  <g stroke="currentColor">
    <rect x="80" y="40" width="70" height="30" class="f-mut"/><rect x="160" y="40" width="70" height="30" class="f-ok"/>
    <rect x="240" y="40" width="70" height="30" class="f-mut"/><rect x="320" y="40" width="70" height="30" class="f-ok"/><rect x="400" y="40" width="70" height="30" class="f-ok"/>
    <rect x="80" y="120" width="70" height="30" class="f-mut"/><rect x="160" y="120" width="70" height="30" class="f-mut"/>
    <rect x="320" y="120" width="70" height="30" class="f-mut"/><rect x="400" y="120" width="70" height="30" class="f-ok"/>
    <rect x="480" y="120" width="70" height="30" class="f-ok"/><rect x="560" y="120" width="70" height="30" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="115" y="60">s232</text><text x="195" y="60">A r0</text><text x="275" y="60">B r1</text><text x="355" y="60">C r0</text><text x="435" y="60">D r0</text>
    <text x="115" y="140">s232</text><text x="195" y="140">B r0</text>
    <text x="355" y="140">due</text><text x="435" y="140">A</text><text x="515" y="140">C</text><text x="595" y="140">D</text>
  </g>
  <g fill="currentColor">
    <text x="80" y="100">콜백 없이 훑기: r0 → due 꼬리로 이동, 나머지 rounds-- (B: 1 → 0, 256 tick 뒤 실행)</text>
    <text x="80" y="190">while (due 가 비지 않음) { t = due 머리; 떼기; armed=false; count--; t-&gt;cb() }</text>
    <text x="80" y="214">· A 의 콜백이 C 를 취소 → C 는 due 에서 빠질 뿐 (취소 = link_del, 어느 리스트든 같음)</text>
    <text x="80" y="236">· A 가 자기를 delay 256 으로 재등록 → expire 1256 → 슬롯 232 (due 아님) → 이번 tick 엔 안 돈다</text>
    <text x="80" y="258">· D 의 콜백이 delay 0 등록 → 1로 올림 → 슬롯 233 → 다음 tick</text>
    <text x="80" y="280">· 새로 거는 타이머는 전부 expire ≥ now+1 이므로 due 에 절대 들어오지 않는다 → 루프가 반드시 끝난다</text>
  </g>
</svg>
```

핵심 논리:

- **1단계에는 콜백이 없다** → 리스트를 바꾸는 것은 내 코드뿐이라 `next` 저장 순회로 충분하다.
- **2단계는 "머리를 떼고 실행"** 을 반복한다. 다음 원소를 미리 기억하지 않으므로 콜백이 due 를 어떻게 바꿔도(중간 원소 취소, 재등록으로 빼가기) 다음 반복은 그 시점의 머리를 본다.
- due 안의 타이머는 실행 직전까지 `armed = true` 다. 그래서 콜백의 `tw_cancel(B)` 가 `true` 를 돌려주고 B 를 due 에서 뗀다(Part B 테스트 `g_cb_ret == 1`). 실행 중인 자기 자신은 이미 `armed = false` → 자기 취소는 `false`, 무해.
- due 는 **스택 위 지역 sentinel** 이다. tick 이 끝나면 항상 비어 있으므로 dangling 포인터가 남지 않는다.

Linux 커널도 같은 구조다: `__run_timers()` 가 `collect_expired_timers()` 로 만료 버킷을 지역 `heads[]` 로 옮긴 뒤 `expire_timers()` 가 머리부터 하나씩 떼어(`detach_timer`) `base->running_timer` 를 설정하고 콜백을 부른다(kernel/time/timer.c).

고정 시나리오 결과(solution.c Part B): 짝 취소 → 짝 실행 0회; 자기 취소 → `false`; delay 256 자기 재등록 → 256, 512, 768 에 정확히 한 번씩; 콜백 안 delay 0 → 5, 6, 7 에 한 번씩(무한 루프 없음); 같은 tick 의 짝을 옮겨 걸기 → 짝은 30 이 아니라 40 에만 실행.

### Part C — next_expiry 와 min-heap 대조

```c
for (k = 1; k <= 256; k++) {                 /* 슬롯 now+k */
    for each t in slot: d = t->rounds * 256 + k;  best = min(best, d);
    if (best != 0 && best <= k) return best;  /* rounds 0 을 만났다 */
}
return best;                                  /* 0 = 타이머 없음 */
```

불변식 3 덕분에 `rounds·256 + k == expire − now` 이다. 최악 O(256 + n)이지만 가까운 타이머가 있으면 일찍 끝난다. 랜덤 테스트는 매 step 이 값을 armed 타이머 전체의 `min(expire − now)` 와 비교한다.

**tickless idle 연결**: Cortex-M 에서 SysTick 을 멈추고 WFI 로 자려면 "다음 타이머까지 몇 tick" 이 필요하다(FreeRTOS `configUSE_TICKLESS_IDLE`, Zephyr tickless 커널이 쓰는 개념). 깨어난 뒤 밀린 n tick 을 따라잡는 `tw_advance(n)` 은 연습 2 — 점유 비트맵으로 빈 슬롯을 건너뛰면 405,883 tick 을 7,009 번의 슬롯 처리로 끝낸다.

**참조 구현**: `(expire, seq)` min-heap + **lazy 삭제**. 취소/재등록은 per-id 상태(`armed`, 현재 `seq`)만 바꾸고, pop 한 항목의 seq 가 현재 seq 와 다르거나 armed 가 아니면 버린다. 휠과 완전히 다른 방식이라 같은 버그를 공유하기 어렵다 — 참조 모델로 좋은 조건이다. 만료 비교는 랩어라운드 안전 `tick_before(a, b) = (uint32_t)(a − b) > 0x7FFFFFFF` 로 했다. `(int32_t)(a − b) < 0` 은 흔한 관용구지만, 2^31 이상인 unsigned 를 int32_t 로 바꾸는 것은 C11 6.3.1.3p3 에 따라 **구현 정의(implementation-defined)** 동작이다(gcc/clang 은 2의 보수로 정의). 이 구현은 그 변환 없이 같은 뜻을 쓴다. 조건: 걸려 있는 모든 expire 가 now 로부터 2^31 미만 — 그래서 `TW_MAX_DELAY = 2^31 − 1`.

**스크립트**: 매 step 0~6개 연산(등록 40%, 짝 `(2i, 2i+1)` 을 같은 delay 로 등록 20% → 같은 tick 만료 쌍, 취소 20%, delay 0 10%, 범위 밖 delay 거절 10%) 후 tick. delay 는 0, 256의 배수, 배수 ±1, 1~64, 1~3000 을 섞는다. 콜백은 xorshift32 로 자기 재등록 / 짝 취소 / 짝 옮겨 걸기 / 자기 취소 / 아무것도 안 함 중 하나. 두 구현이 같은 시드로 따로 돌므로, 실행 로그가 같으면 콜백 안의 난수 소비까지 같았다는 뜻이다. 실제 출력:

```text
[C] wheel vs heap: 20000 steps from now=0xFFFFE000 (wraps), fires=47419 (wheel) / 47419 (heap) -> identical order
[C] cb: re-arm self 11803, cancel mate 2235 (same-tick 921), mod mate same-tick 871
[C] registrations 71337, peak live timers 1288
27/27 passed
```

등록 71,337 번, 동시에 최대 1,288 개, 실행 47,419 번이 tick·id·순서까지 같다. "같은 tick 대기 중인 짝 취소" 921 번과 "같은 tick 의 짝 옮겨 걸기" 871 번이 실제로 일어났는지도 CHECK 로 확인한다 — 랜덤 테스트가 까다로운 경우를 **실제로 밟았는지** 세지 않으면 "통과"가 아무 의미 없을 수 있다.

테스트가 진짜 버그를 잡는지 돌연변이(mutation)로 확인했다(파일엔 없음, 직접 해 볼 것):

| 일부러 넣은 버그 | 잡은 곳 |
|---|---|
| `rounds = delay >> 8` | Part A 경계 delay, FIFO, 랜덤(첫 tw_check 에서 멈춤) |
| 머리 삽입(LIFO) | tw_check seq 순서, FIFO, 짝 취소/옮겨 걸기 |
| 단일 패스 tick | 크래시(bus error) — count 이중 감소와 self-loop 반복 |
| delay 0 을 1로 안 올림 | delay 0 테스트, 콜백 delay 0, 랜덤 로그 불일치 |

### 측정 — 단일 레벨 휠의 약점: 방문 비용

`demo_wheel_vs_heap.c` (solution 의 휠 복사 + 1단계 방문 카운터, 그리고 `pos[]` 맵을 가진 indexed min-heap):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_wheel_vs_heap.c -o demo && ./demo
```

표 1 — 주기 타이머 2048개가 콜백에서 delay `[1, D]` 균등으로 재등록, 200,000 tick (실제 출력):

| D | fires | 방문/실행 (측정) | 방문/실행 (이론 평균 (d−1)/256+1) | 방문/tick |
|---|---|---|---|---|
| 64 | 12602583 | 1.00 | 1.00 | 63.01 |
| 256 | 3186927 | 1.00 | 1.00 | 15.93 |
| 1024 | 798805 | 2.50 | 2.50 | 9.99 |
| 4096 | 199174 | 8.53 | 8.50 | 8.50 |
| 16384 | 49197 | 33.02 | 32.50 | 8.12 |
| 65536 | 11751 | 136.65 | 128.50 | 8.03 |

읽는 법:

- delay ≤ 256 이면 모든 방문이 곧 실행이다 — 낭비 0.
- D 가 커지면 타이머 하나가 실행되기까지 `(d−1)/256 + 1` 번 방문된다. **방문/tick 은 n/256 = 2048/256 = 8 로 수렴** — tick 비용이 실행 수가 아니라 "살아 있는 타이머 수 / 슬롯 수"에 비례한다는 뜻이다.
- D = 65536 에서 측정(136.65)이 이론(128.50)보다 큰 것은 창 경계 효과다: 200,000 tick 이 끝날 때 아직 실행 안 된 긴 타이머들의 방문은 세었지만 실행은 안 세었고, 짧은 delay 가 창 안에서 더 자주 실행되는 편향도 있다. D 가 작을수록 이 효과는 사라진다.

표 2 — 시간, 이 Mac (Apple Silicon, `-O2`, 3회 중 최솟값). 절대 ns 가 아니라 **비율**을 볼 것:

| 작업 | wheel ns | heap ns | heap/wheel | 실행 수 wheel / heap |
|---|---|---|---|---|
| W1 재시작 L=10000 D=10000 (ns/연산) | 14.0 | 21.3 | 1.52x | 200186 / 200186 |
| W2 등록+만료 L=65536 D=4096 (ns/타이머) | 46.8 | 60.3 | 1.29x | 65536 / 65536 |
| W2 등록+만료 L=8192 D=262144 (ns/타이머) | 819.6 | 53.3 | 0.07x | 8192 / 8192 |

- W1(타임아웃 재시작이 대부분 — TCP 재전송, UART idle 타임아웃): 휠이 1.5배 빠르다. 생각보다 차이가 작은 이유는 무작위 delay 의 힙 sift 가 평균적으로 짧기 때문이다. 그러나 휠의 등록·취소는 **최악도 O(1)** 이고 힙은 최악 ⌊log₂ 10000⌋ = 13 단계다 — 실시간에서는 평균보다 상한이 중요하다.
- W2 긴 delay(최대 262144 = 1024 바퀴): 휠이 **15배 느리다**. 타이머당 평균 512 번 방문되고, 262,144 번의 tick 대부분이 rounds 를 줄이는 일만 한다. 이것이 계층 휠이 존재하는 이유다.

### 계층 휠 — 이 레슨 범위 밖 (see: RTOS)

단일 레벨 + rounds 의 약점(긴 delay 가 매 바퀴 방문됨)을 고치는 것이 **계층 타이머 휠(hierarchical timing wheels)** 이다 — Varghese & Lauck(1987)의 논문이 해시 휠과 함께 제안했다. 시계처럼 "초 휠 / 분 휠 / 시 휠"을 두고, 먼 타이머는 거친 휠에 있다가 가까워지면 아래 휠로 **내려간다(cascade)**. Linux 커널 4.8 이전 timer wheel 이 정확히 이 모양이었다 — 첫 레벨 `tv1` 이 256 슬롯(TVR_BITS 8), `tv2`~`tv5` 가 64 슬롯씩, `cascade()` 로 내림. 4.8 부터는 레벨당 64 버킷에 레벨마다 8배 거친 해상도를 쓰는 **캐스케이딩 없는** 휠로 바뀌었다: 먼 타이머는 그 레벨의 해상도로 반올림되어 조금 늦게 실행되는 대신, 내려가는 비용이 사라진다(대부분의 타임아웃은 어차피 만료 전에 취소된다는 판단). 정밀 타이머는 `hrtimer`(레드블랙 트리, DS-A05)가 맡는다. 계층 휠의 설계·RTOS tick 통합은 RTOS 트랙이 소유한다.

### 복잡도와 메모리

| 연산 | 시간 | 결정성(최악) |
|---|---|---|
| `tw_add` / `tw_cancel` | O(1) | 상수 — ISR 에서 불러도 됨 |
| `tw_tick` | O(슬롯 길이) — 균등이면 n/256 | 한 슬롯에 몰리면 O(n) (같은 주기로 같은 시각에 건 타이머들) |
| `tw_next_expiry` | O(256 + 본 노드 수) | O(256 + n) |

메모리: 슬롯 sentinel 256 × 포인터 2개. 이 Mac(64비트)에서 `sizeof(tw_wheel_t)` = 4112 B, `sizeof(tw_timer_t)` = 48 B (실측). Cortex-M(32비트 포인터)이면 계산상 휠 2 KB + 12 B, 타이머 28 B(link 8 + expire/rounds/seq 12 + cb 4 + id 2 + armed 1 + 패딩 1). `seq` 는 테스트용이라 빼면 24 B, 슬롯을 단일 포인터 머리(Linux `hlist_head` 방식)로 하면 휠은 1 KB 다. RAM 32 KB 짜리 MCU 에서도 부담 없는 크기다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 27/27 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_asan && ./solution_asan   # 27/27, 경고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `rounds = delay >> 8` | delay = 256의 배수에서 한 바퀴 늦게 실행 (256 → 512) | `rounds = (delay - 1) >> 8` (delay ≥ 1 보장 후) |
| delay 0 을 그대로 `expire = now` | 슬롯 `now` 는 이미 처리됨 → 256 tick 뒤에 실행. 또는 add 안에서 바로 콜백 → 재진입 지옥 | `if (delay == 0) delay = 1;` — 다음 tick |
| 머리 삽입(LIFO) | 같은 tick 순서가 등록 역순 → 참조와 불일치, 프로토콜 순서 가정 깨짐 | 꼬리 삽입 = FIFO |
| `list_for_each_safe` 로 순회하며 바로 콜백 | 콜백이 next 를 취소/이동하면 취소된 타이머 실행·다른 리스트로 넘어감 (연습 4) | 1단계 due 로 옮기기 → 2단계 머리 떼며 실행 |
| armed 인 타이머를 다시 add (떼지 않고) | 같은 노드가 두 리스트에 연결 → 리스트 고리 파손 | add 시작에 `if (armed) link_del` |
| 실행 후 `armed = false` 를 콜백 **뒤에** 함 | 콜백 안에서 자기 재등록하면 그 직후 false 로 덮어써 "걸려 있는데 armed 아님" | 떼고 → armed=false → 콜백 순서 |
| `expire < now` 로 만료 판정 | 2^32 넘침 직후 순서 뒤집힘 | 휠은 같음 비교/rounds 만 사용, 순서 비교는 `(uint32_t)(a-b) > 0x7FFFFFFF` |
| 슬롯 수 250 + `expire % 250` | 2^32 mod 250 = 46 → 넘침에서 커서 순서가 46 칸 점프 → 오발(연습 3: 600개 중 15개) | 2의 거듭제곱 슬롯, 아니면 상대 커서 |

## 8. 면접 Q&A

<details>
<summary>Q1. 타이머 휠의 add/cancel/tick 복잡도는? "O(1)" 이라고 하면 꼬리질문은?</summary>

add·cancel 은 O(1) 최악까지. tick 은 슬롯 하나만 보지만 그 슬롯의 길이에 비례한다 — 균등 분포면 평균 n/256, 최악(모든 타이머가 같은 하위 8비트) O(n). 꼬리질문 "긴 타이머는?" → 단일 레벨 + rounds 는 delay d 인 타이머를 ⌈d/256⌉ 번 방문한다. 측정에서 delay 최대 262,144 일 때 휠이 힙보다 15배 느렸다. 해결은 계층 휠(cascade) 또는 Linux 4.8+ 처럼 해상도를 포기하는 비캐스케이딩 휠.

</details>

<details>
<summary>Q2. rounds 를 줄이는 대신 절대 expire 를 비교하면 안 되나요?</summary>

된다 — 연습 1. 방문할 때 `t->expire == now` 면 실행, 아니면 그냥 지나간다. 같음 비교는 랩어라운드에 안전하고(2^32 가 256의 배수), 기다리는 타이머에 **쓰기를 안 해서** 캐시 라인을 더럽히지 않는다(다른 코어가 그 타이머 객체를 캐시에 들고 있을 때 무효화 트래픽이 준다). 대신 rounds 방식은 "남은 바퀴"를 바로 알 수 있어 next_expiry 같은 계산이 쉽다. Netty `HashedWheelTimer` 는 `remainingRounds` 카운터 방식이다. 방문 횟수 자체는 두 방식이 같다.

</details>

<details>
<summary>Q3. 콜백 안에서 자기 타이머를 취소하거나 다른 타이머를 건드리면? Linux 는 어떻게 하나요?</summary>

단일 패스 순회는 깨진다(연습 4: 취소된 타이머 실행, 한 tick 에 2번 실행). 만료 대상을 지역 리스트로 먼저 옮기고 머리부터 하나씩 떼어 실행하면 안전하다. Linux `__run_timers()` 가 같은 구조로, 만료 버킷을 지역 `heads[]` 로 옮긴 뒤(`collect_expired_timers`) `expire_timers()` 가 하나씩 `detach_timer` 후 콜백을 부른다. SMP 에서는 다른 CPU 가 지금 실행 중인 콜백과 경쟁할 수 있어서 `del_timer_sync()`(최근 이름 `timer_delete_sync()`)가 `base->running_timer` 를 보고 콜백이 끝날 때까지 기다린다 — 모듈 언로드 전에 필수. 단, 그 콜백 안에서 자기 자신에 `_sync` 를 부르면 교착이다.

</details>

<details>
<summary>Q4. 같은 tick 에 만료되는 타이머의 순서는 보장되나요? 왜 정해야 하나요?</summary>

정의하기 나름이고, **정의하지 않으면 테스트가 비결정적**이 된다. 이 레슨은 "(expire, 마지막 등록 seq) 오름차순 = FIFO" 로 정하고, 휠은 꼬리 삽입으로, 참조 힙은 seq tie-break 로 같은 순서를 만든다. 힙은 안정하지 않아서 expire 만 비교하면 같은 tick 순서가 힙 모양에 따라 바뀐다. 실무에서도 "요청 A 의 타임아웃 처리 후 B" 같은 순서 가정이 있으면 FIFO 가 필요하다. 반대로 Linux timer wheel 은 같은 jiffy 의 순서를 API 로 보장하지 않으므로 그런 가정을 하면 안 된다.

</details>

<details>
<summary>Q5. tick ISR 과 태스크가 휠을 공유하면? 임계 구역은 얼마나 길어지나요?</summary>

add/cancel 은 포인터 몇 개라 짧은 임계 구역(Cortex-M `__disable_irq()`/PRIMASK 또는 BASEPRI)으로 감싸면 상한이 상수다 — 정렬 리스트(O(n))나 힙(O(log n))보다 유리한 점. 문제는 tick 처리: 슬롯 길이만큼 + 콜백 시간. 보통 ① ISR 은 `now++` 와 "슬롯이 비었나" 만 보고, ② 실제 처리와 콜백은 타이머 태스크/softirq 에서 한다(FreeRTOS timer service task, Linux TIMER_SOFTIRQ). FreeRTOS 처럼 ISR 의 요청을 큐로 태스크에 넘기면 휠을 태스크 하나가 독점해 락이 필요 없다(DS-B12 Q6 과 같은 패턴).

</details>

<details>
<summary>Q6. FreeRTOS, Zephyr 는 타이머 휠을 쓰나요?</summary>

아니다. FreeRTOS 소프트웨어 타이머는 만료 시각으로 **정렬된 리스트** 두 개(현재/넘침 후)와 timer service(daemon) task 를 쓰고, Zephyr 는 kernel/timeout.c 에서 앞 원소와의 **차이(delta)로 정렬된 리스트**를 쓴다. MCU 에서는 타이머가 수십 개 수준이라 O(n) 삽입도 충분히 빠르고, 다음 만료가 항상 리스트 머리라 tickless idle 계산이 O(1)이다. 휠은 수천~수백만 개(커넥션별 타임아웃)일 때 빛난다. 면접 답: "개수, 해상도, 범위, 취소 비율, 최악 시간 요구"로 고른다.

</details>

<details>
<summary>Q7. 슬롯 수는 어떻게 고르나요? 256 대신 64 나 4096 이면?</summary>

슬롯이 많을수록 tick 당 방문(n/S)과 rounds 가 줄지만 RAM(S × sentinel 크기)이 는다. 대부분 타이머의 delay 가 S 이하가 되도록 고르면 방문 = 실행이라 낭비가 없다(표 1: D ≤ 256 에서 방문/실행 1.00). 2의 거듭제곱이어야 `& mask` 가 되고 2^32 넘침에서 슬롯 순서가 이어진다 — 아니면 `%` 가 필요하고(Cortex-M0/M0+ 는 나눗셈 명령이 없다) 넘침에서 깨진다(연습 3). 점유 비트맵(256 비트 = 32 B)을 더하면 빈 슬롯을 CTZ 로 건너뛰어 tickless 따라잡기가 빨라진다(연습 2).

</details>

## 9. 추가 연습

### 연습 1 — rounds 없이 절대 시각 비교

rounds 필드를 없애고, 방문한 슬롯에서 `t->expire == now` 인 것만 실행하는 휠을 만들어라. delay 0/1/255/256/257/512/513 경계와, 무작위 등록·재등록·취소 6000 step 뒤 "마지막으로 건 시각에 정확히 실행"을 검증하라.

<details>
<summary>답 보기</summary>

슬롯 선택은 같고(`expire & 255`), tick 의 1단계가 `if (t->expire == w1.now)` 로 바뀐다. 같음 비교라 랩어라운드 걱정이 없다(시작 now = 0xFFFFFFF0). 기다리는 타이머는 **읽기만** 하므로 쓰기 0 — rounds 방식은 방문할 때마다 rounds-- 를 쓴다. 방문 횟수는 같다. 실제 출력:

```text
[1] abs-compare wheel: last-arm fired on time 2172, bad 0, cancels 259, slot visits 16056, writes to waiting timers 0
```

</details>

### 연습 2 — 점유 비트맵과 `tw_advance(n)` (tickless 따라잡기)

슬롯마다 "비어 있지 않음" 비트를 `uint32_t occ[8]` 에 유지하고, n tick 을 한 번에 진행하는 `w2_advance(n)` 을 만들어라. 결과는 `w2_tick` 을 n 번 부른 것과 실행 로그·상태가 완전히 같아야 한다.

<details>
<summary>답 보기</summary>

비트 유지: add 후 그 슬롯 set, 떼어낸 뒤(취소·옮겨 걸기·tick 1단계 후) 슬롯이 비었으면 clear. advance: `k = 다음 비어 있지 않은 슬롯까지 거리(1..256)` 를 CTZ 로 찾고(`__builtin_ctz(0)` 은 정의되지 않으므로 bits ≠ 0 일 때만 부른다; Cortex-M3+ 는 `RBIT`+`CLZ`, CMSIS `__CLZ()`), `k > n` 이면 `now += n` 하고 끝, 아니면 `now += k−1` 후 tick 한 번. **빈 슬롯에는 줄일 rounds 도 없으므로** 건너뛰어도 결과가 같다. 거리 계산에서 현재 커서 슬롯 자신은 거리 0 이 아니라 256(다음 바퀴)이다. 실제 출력 — 실행 1489번이 같고 슬롯 처리는 58배 적다:

```text
[2] advance vs tick-by-tick: 405883 ticks, fires 1489/1489, slot visits 405883 vs 7009 -> identical
```

</details>

### 연습 3 — 슬롯 250개면 무엇이 깨지나

슬롯 250개로 바꿔 ① `slot = expire % 250`, 방문 슬롯 = `now % 250` 인 버전과 ② 커서를 따로 돌리는(`cur = (cur+1) % 250`, `slot = (cur + delay) % 250`) 버전을 만들고, now = 0xFFFFFF00 에서 delay 1..600 타이머 600개를 걸어 700 tick 동안 잘못 실행된 개수를 세어라.

<details>
<summary>답 보기</summary>

2^32 = 250 × 17179869 + 46 이라 `now % 250` 은 넘침 순간 (true 값 기준) 46 칸 뒤로 점프한다. 그러면 한 슬롯의 방문 간격이 넘침 근처에서 250 이 아니게 되어 rounds 세기가 어긋난다. 상대 커서는 절대값을 쓰지 않으므로 넘침과 무관하다. 256 슬롯 + `& 255` 는 2^32 가 256 의 배수라 이 문제가 처음부터 없다.

```text
[3] 250 slots across 2^32 wrap, 600 timers: misfired abs%250=15, relative cursor=0
```

</details>

### 연습 4 — 단일 패스 tick 의 버그를 직접 재현

`next` 만 저장하고 순회 중에 바로 콜백을 부르는 `naive_tick` 과 2단계 `twophase_tick` 에 같은 두 시나리오를 돌려라. (a) A 가 콜백에서 delay 256 으로 자기 재등록(같은 슬롯 뒤에 rounds 3 인 이웃 하나), (b) 같은 tick 의 A 가 콜백에서 B 를 취소.

<details>
<summary>답 보기</summary>

(a) 순회가 `A → 이웃` 일 때 A 가 같은 슬롯 꼬리(이웃 뒤)로 rounds 0 으로 다시 붙으므로, 이웃을 지난 뒤 A 를 또 만나 같은 tick 에 두 번 실행된다. (b) A 를 처리할 때 저장한 next = B. 콜백이 B 를 떼어도(self-loop) 순회는 B 로 가서 **취소된 B 를 실행**하고, B 의 next 가 자기 자신이라 guard(50회)까지 B 를 반복 실행한다. NULL 로 지우는 리스트였다면 크래시, 안 지우는 리스트였다면 옛 이웃으로 계속 걸어간다 — 어느 쪽이든 버그. 2단계는 둘 다 정상:

```text
[4] single-pass: max fires in one tick 2, cancelled B fired 49 | two-phase: 1, 0
```

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 11/11 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_asan && ./practice_asan   # 11/11, 경고 없음
```

## 10. 핵심 정리

- 해시 타이머 휠 = 슬롯 256개 원형 배열 + 슬롯마다 침입형 이중 리스트. `slot = expire & 255`, `rounds = (delay − 1) >> 8`.
- add/cancel O(1) 최악까지(ISR 에서 OK). tick 은 커서 슬롯 하나만 — 비용은 n/256 + 실행 수.
- delay 0 → 1 (add 안에서 콜백 금지). delay 256 은 지금 슬롯·rounds 0 → 256 tick 뒤.
- 불변식: `1 ≤ expire − now ≤ 2^31−1`, `rounds == (expire − now − 1) >> 8`, 슬롯 = 하위 8비트, 슬롯 안 seq 오름차순.
- tick 은 2단계: 콜백 없이 due 로 옮기기 → due 머리를 떼며 실행. 단일 패스는 취소된 타이머 실행·이중 실행.
- 같은 tick 순서 = (expire, seq) FIFO — 꼬리 삽입으로 공짜. 참조 힙도 같은 키로.
- 약점: 긴 delay 는 매 바퀴 방문(실측 delay ≤ 262144 에서 힙보다 15배 느림) → 계층 휠(Varghese & Lauck, Linux timer wheel) — RTOS 트랙.
- 슬롯 수는 2의 거듭제곱: `& mask` + 2^32 넘침에서도 슬롯 순서 연속.

## 참고자료

- George Varghese, Tony Lauck, "Hashed and Hierarchical Timing Wheels: Data Structures for the Efficient Implementation of a Timer Facility", *SOSP* 1987 (확장판 *IEEE/ACM Trans. on Networking* 5(6), 1997)
- [Linux `kernel/time/timer.c` — `__run_timers`, `collect_expired_timers`, `expire_timers`, 비캐스케이딩 휠 설명 주석](https://github.com/torvalds/linux/blob/master/kernel/time/timer.c)
- LWN.net, "Reinventing the timer wheel" (Jonathan Corbet) — 비캐스케이딩 timer wheel 재설계 배경 (Linux 4.8 에 병합)
- [Netty `HashedWheelTimer` — `remainingRounds` 방식 해시 휠](https://github.com/netty/netty/blob/4.1/common/src/main/java/io/netty/util/HashedWheelTimer.java)
- [FreeRTOS `timers.c` — 정렬 리스트 + timer service task](https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/timers.c)
- [Zephyr `kernel/timeout.c` — delta 정렬 타임아웃 리스트](https://github.com/zephyrproject-rtos/zephyr/blob/main/kernel/timeout.c)
- ISO/IEC 9899:2011 (C11) 6.2.5p9 (unsigned 산술은 모듈로), 6.3.1.3p3 (범위 밖 signed 변환은 구현 정의)
- DS-B06 (sentinel 리스트), DS-B12 (min-heap 타이머 큐), DS-M01 (침입형 리스트), DS-M09 (계층 비트맵/CLZ), DS-M11 (indexed heap 취소)
