---
id: ROS-M03
time: 90분
updated: 2026-11-08
---
# ROS-M03 Executor & callback group

## 학습 목표

- rclcpp 실행 모델의 뼈대를 직접 구현한다. 작업 스레드(SingleThreaded = 1, MultiThreaded = N), 콜백 그룹 토큰(MutuallyExclusive / Reentrant), 스캔 순서, 합쳐지는(coalescing) 타이머.
- **타이머 콜백 안의 동기 서비스 호출**이 언제 멈추는지 2×2 + 그룹 분리 **6칸 매트릭스**로 실측하고, 각 칸의 원인이 **그룹 토큰**인지 **스레드 슬롯**인지 말한다.
- 절대 멈추지 않는 탐지기를 만든다. **bounded wait** 와 구조적 판정(누가 진행 가능한가), 그리고 `cycle: timer:tick -> call#0 -> group:node_default -> timer:tick` 형식의 **대기 그래프(wait-for) 보고**.
- 세 가지 수정을 구현하고 효과를 같은 테스트로 보인다. 그룹 분리 + MultiThreaded, async 호출 + continuation, 그리고 Reentrant 에도 남는 **스레드 고갈**(스레드 수 > 동시에 막힌 콜백 수).
- 이 교착을 Coffman 4조건(OS-M10)에 그대로 대응시키고, 수정마다 어느 조건을 깨는지 말한다.

## 1. 문제

> **이 레슨의 환경.** ROS 2 는 이 맥에도 Lima VM(Ubuntu 26.04)에도 설치돼 있지 않고, 이 레슨은 설치하지 않는다(트랙 `env_note` 의 "Lima Ubuntu 24.04 + ros-jazzy" 와 다른 점). **실제로 돌린 것**: C++20 + `std::thread` 로 만든 미니 executor 와 그 위의 매트릭스. **모델인 것**: 스케줄러 전부. rclcpp `jazzy` 브랜치의 `executor.cpp`, `multi_threaded_executor.cpp` 와 ROS 2 문서의 RST 원본을 읽고 옮겼다. **돌리지 못한 것**: 실제 rclcpp/rclpy 코드. §5 "ROS 2가 있을 때"에만 있고 컴파일도 실행도 안 했다.

펌웨어 쪽에서 이 버그는 **콜백 하나가 자기가 돌아가야 처리되는 응답을 기다리는** 모양으로 나타난다. FreeRTOS 타이머 콜백이 블로킹 API 를 부르면 타이머 서비스 태스크 전체가 멈추고, 인터럽트 하반부가 처리할 응답을 같은 워커에서 기다려도 똑같이 멈춘다. ROS 2 에서는 타이머 콜백이 서비스를 동기로 호출할 때 나타나고, 로봇·드론 회사 면접에서 "MultiThreadedExecutor 로 바꿨는데 왜 그대로 멈추죠?"로 자주 묻는다. 답은 **스레드가 아니라 콜백 그룹** 쪽에 있다. 동시성 가이드 8층의 목표는 앞에서 배운 교착 이론(OS-M10)을 실제 프레임워크 구조에서 찾아내고 검증하는 것이다.

핵심 그림은 이렇다. 타이머 콜백은 ① 스레드와 ② 그룹 토큰을 쥔 채로 응답을 기다린다. 그런데 응답은 executor 가 응답 콜백을 **실행해 줘야** 완료되고, 응답 콜백을 돌리려면 스레드 하나와 (클라이언트가 같은 MutuallyExclusive 그룹에 있으면) 그 그룹 토큰이 필요하다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<defs><marker id="m1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" fill="currentColor" font-weight="bold">Multi(2) + 같은 MutuallyExclusive 그룹: 스레드는 남는데 응답을 못 돌린다</text>
<rect x="20" y="40" width="210" height="96" rx="6" class="f-warn" stroke="currentColor"/>
<text x="32" y="60" fill="currentColor" font-weight="bold">T0: timer:tick 실행 중</text>
<text x="32" y="80" fill="currentColor">call_sync(call#0) 에서 막힘</text>
<text x="32" y="100" fill="currentColor">쥔 것: 스레드 T0</text>
<text x="32" y="120" fill="currentColor">쥔 것: group node_default</text>
<rect x="280" y="40" width="190" height="96" rx="6" class="f-acc" stroke="currentColor"/>
<text x="292" y="60" fill="currentColor" font-weight="bold">response:call#0</text>
<text x="292" y="80" fill="currentColor">응답은 이미 도착, 큐에 있음</text>
<text x="292" y="100" fill="currentColor">필요: 쉬는 스레드 (T1 있음)</text>
<text x="292" y="120" fill="currentColor">필요: 그룹 토큰 (없음)</text>
<rect x="520" y="40" width="200" height="96" rx="6" class="f-mut" stroke="currentColor"/>
<text x="532" y="60" fill="currentColor" font-weight="bold">group:node_default</text>
<text x="532" y="80" fill="currentColor">MutuallyExclusive</text>
<text x="532" y="100" fill="currentColor">taken = true</text>
<text x="532" y="120" fill="currentColor">holder = timer:tick</text>
<line x1="230" y1="70" x2="278" y2="70" stroke="currentColor" stroke-width="2" marker-end="url(#m1)"/>
<text x="232" y="62" fill="currentColor" font-size="12">기다림</text>
<line x1="470" y1="88" x2="518" y2="88" stroke="currentColor" stroke-width="2" marker-end="url(#m1)"/>
<text x="472" y="80" fill="currentColor" font-size="12">기다림</text>
<path d="M620 136 C 620 210, 130 210, 130 138" stroke="currentColor" stroke-width="2" fill="none" marker-end="url(#m1)"/>
<text x="340" y="205" fill="currentColor">쥔 쪽: timer:tick (고리 완성)</text>
<rect x="20" y="228" width="700" height="60" rx="6" class="f-ok" stroke="currentColor"/>
<text x="32" y="250" fill="currentColor">T1 은 쉬는데도 가져갈 수 있는 일이 없다. 스레드를 8개로 늘려도 결과가 같다.</text>
<text x="32" y="272" fill="currentColor">Single 에서는 T1 자체가 없어서 그룹이 달라도 멈춘다 (고리가 스레드 슬롯을 통과).</text>
</svg>
```

### Part A — 스케줄러: 그룹 토큰과 스캔 순서

`Executor` 의 작업 스레드는 mutex 아래에서 "다음에 실행할 일"을 고른다. 고르는 규칙을 구현한다.

- `take_next_ready_locked(out)`: `timers_` (ready 인 것) → `service_q_` → `response_q_` 순으로 본다. 그룹을 가져갈 수 없는 항목은 **건너뛰고 다음 항목을 본다**. 고르면 ready/큐에서 지우고, MutuallyExclusive 면 토큰을 잡는다(`taken=true`, `holder=label`).
- `release_group_locked(w)`: 콜백이 끝나면 토큰을 돌려준다.

타이머는 큐가 아니라 **ready 깃발**이다. 아직 안 가져간 타이머를 다시 트리거해도 합쳐진다(coalesce).

### Part B — 동기 호출: 멈춤을 "시간"이 아니라 "구조"로 판정

- `call_sync(client, req, budget)`: 보내고 기다린다. 기다리는 동안 매번 ① 완료 ② stop ③ 구조적 멈춤 ④ 시간 초과를 검사한다. 끝나면 포기한 호출을 표시해서 **늦은 응답을 안전하게 버린다**.
- `is_stuck_locked()`: executor 전체가 더 진행할 수 없는가(막힌 스레드 있음, 네트워크에 오는 응답 없음, 달리는 콜백 없음, 쉬는 스레드가 없거나 가져갈 일이 없음).
- `why_locked(c)`: `WhyGroup` / `WhyThread`. `report_locked(c, why)`: 대기 그래프 보고.

매트릭스 6칸(Single/Multi(2) × 같은 MutEx / 같은 Reentrant / 그룹 분리)을 실제로 돌려서 표를 만든다. 멈춘 칸도 콜백이 **돌아와야** 한다.

### Part C — 고치기와 남는 함정

- `fixed_groups(ex)`: Multi 에서 sync 호출이 안 멈추는 그룹 배치.
- `async_send` 와 `async_chain`: 기다리지 않는다. 6칸 전부 통과해야 한다. async 3개를 이으면 sync 3번 호출을 대신한다.
- **스레드 고갈**: 같은 Reentrant 그룹에 sync 호출 타이머 2개 + Multi(2)는 멈춘다(`WhyThread`). Multi(3)이면 통과한다.

## 2. Problem (English)

> You are given a minimal model of the rclcpp execution model: an `Executor` with worker threads, MutuallyExclusive and Reentrant callback groups, timers (a coalescing ready flag), services and clients. A service server may live in the executor or outside it (a remote process).
>
> Implement:
> 1. `bool take_next_ready_locked(Work &out)`: scan timers, then service requests, then responses; skip any item whose group cannot be taken; take the MutuallyExclusive token when selected.
> 2. `void release_group_locked(const Work &w)`: return the token after the callback ran.
> 3. `CallResult call_sync(int client, int request, ms budget)`: must never hang. Returns `Ok`, `Stuck`, `Timeout`, or `Stopped`, and marks the call abandoned on anything but `Ok` so that a late response is safely dropped.
> 4. `bool is_stuck_locked() const` and `unsigned why_locked(const Call &c) const`: decide *structurally* that no thread can make progress, and whether the next step of the call needs a held group token (`WhyGroup`) or a free worker (`WhyThread`), or both.
> 5. `std::string report_locked(const Call &c, unsigned why) const`: a wait-for report, e.g. `cycle: timer:tick -> call#0 -> group:node_default -> timer:tick`.
> 6. `void async_send(int client, int request, Continuation on_response)`, `NodeSetup fixed_groups(Executor &)`, and `async_chain` (three chained async calls).
>
> A timer callback calls a service synchronously. For executors `Single` and `Multi(2)` and arrangements {same MutuallyExclusive, same Reentrant, separate groups}, produce the table of outcomes. Expected: only `Multi(2)` with `same Reentrant` or `separate groups` completes; the other four cells report `STUCK` with the correct reason. Then show that two callbacks each making a sync call in one Reentrant group on `Multi(2)` exhausts the thread pool, while `Multi(3)` completes. CHECKs are deterministic; timeouts are only a safety net.

## 3. 시작 코드

스타터의 `take_next_ready_locked` 는 일단 돌아가도록 그룹을 안 보는 FIFO 이고, `call_sync` 는 보내지도 않고 `Timeout` 을 돌려준다. 그래서 멈추지 않고 FAIL 로 끝난다. TODO 는 Part A 2개, Part B 4개, Part C 3개다.

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # SDK 27.2 링크가 깨져 있다
c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread starter.cpp -o st && ./st
```

실제 출력(앞부분과 끝):

```text
== Part A: 스케줄링 규칙 ==
FAIL  !rv.met()  (line 444)
PASS  ran == 2
PASS  rv.met()
PASS  rv.met()
PASS  !rv.met()
PASS  ran == 1
PASS  ex.coalesced() == 2
FAIL  order == "TSR"  (line 498)

== Part B: 타이머 콜백 안의 동기 호출 매트릭스 ==
executor               timer/client group       result   why
SingleThreaded         same MutuallyExclusive   TIMEOUT  -
FAIL  c.r.status == r.want  (line 518)
…
FAIL  oks == 2  (line 693)

23/63 passed
```

## 4. 힌트

<details><summary>힌트 1 — 건너뛰기가 왜 중요한가</summary>

`service_q_` 맨 앞 항목의 그룹이 막혀 있어도, 뒤의 항목은 다른 그룹일 수 있다. 앞에서 멈추면 그룹 하나가 executor 전체를 멈춘다. `for` 로 돌면서 `can_take` 가 true 인 첫 항목을 고른다.
</details>

<details><summary>힌트 2 — 토큰은 언제 잡고 언제 놓나</summary>

고르는 순간(콜백 시작 전) 잡고, 콜백이 **돌아온 뒤** 놓는다. rclcpp 도 고를 때 `can_be_taken_from` 을 false 로, 실행 뒤 true 로 되돌린다.
</details>

<details><summary>힌트 3 — 언제 멈췄다고 말할 수 있나</summary>

"이 응답이 언제 올지"가 아니라 "지금 상태를 바꿀 수 있는 주체가 남아 있나"를 묻는다. 달리는 콜백(막히지 않은 busy 스레드)이 있거나, 쉬는 스레드와 가져갈 일이 함께 있거나, 네트워크에 응답이 오고 있으면 아직 아니다.
</details>

<details><summary>힌트 4 — WhyGroup 과 WhyThread 는 왜 둘 다 볼까</summary>

Single + same MutEx 는 **둘 다** 참이다. 그룹만 풀어도 스레드가 없고, 스레드를 늘려도 그룹이 막혀 있다. 그래서 수정할 때 무엇을 고쳐야 하는지 보고가 둘 다 알려 줘야 한다.
</details>

<details><summary>힌트 5 — async 는 왜 안 멈추나</summary>

`async_send` 는 큐에 넣고 돌아온다. 콜백이 끝나면서 스레드와 토큰을 돌려주고, 응답은 그다음에 그 자원으로 처리된다. 기다리는 코드가 없으면 고리의 첫 화살표가 없다.
</details>

## 5. 풀이

### Part A — 스케줄러의 두 규칙

**규칙 1. 그룹은 콜백 사이의 동시 실행을 막는 토큰이다.** Reentrant 는 토큰이 없고, MutuallyExclusive 는 토큰이 하나다. rclcpp Jazzy 의 `Executor::get_next_ready_executable` 도 같은 방식이다. 타이머 → 구독 → 서비스 → 클라이언트 → waitable 순으로 살피고, 콜백 그룹의 `can_be_taken_from()` 이 false 인 후보는 건너뛰며, 고른 그룹이 MutuallyExclusive 면 `can_be_taken_from` 을 false 로 내린다. `execute_any_executable` 이 실행 후 다시 true 로 올린다(소스 확인). `multi_threaded_executor.cpp` 의 `run()` 은 `wait_mutex_` 아래서 한 스레드씩 다음 일을 고르고, 락 밖에서 실행한다. 이 레슨의 `mu_` + `take_next_ready_locked` 와 대응된다.

| 이 레슨 | rclcpp (Jazzy, 소스에서 확인) |
|---|---|
| `Group::taken` | `CallbackGroup::can_be_taken_from()` 의 반대 |
| `take_next_ready_locked` | `Executor::get_next_ready_executable` (타이머 → 구독 → 서비스 → 클라이언트 → waitable) |
| `release_group_locked` | `execute_any_executable` 끝에서 `can_be_taken_from` 복구 |
| `spin(1)` / `spin(n)` | `SingleThreadedExecutor` / `MultiThreadedExecutor` (0이면 `max(hardware_concurrency, 2)`) |
| `Group` 인자 없이 만드는 콜백 | 노드의 기본 그룹 (문서: MutuallyExclusive) |

Reentrant 와 "서로 다른 MutuallyExclusive 그룹"의 차이는 **같은 콜백이 자기 자신과 겹칠 수 있는가**다. ROS 2 문서의 지침도 그렇게 나뉜다. 자기 자신과 병렬로 돌아도 되면(서버가 여러 호출을 동시에 처리) Reentrant, 자기 자신과는 절대 안 되면(제어 루프 타이머) MutuallyExclusive.

**규칙 2. 타이머는 큐가 아니라 깃발이다.** 콜백이 늦게 시작되면 밀린 주기는 쌓이지 않는다. rcl 의 `rcl_timer_call` 은 다음 호출 시각을 현재 시각이 아니라 **이전 예정 시각 + 주기**로 잡고, 이미 지났으면 놓친 주기를 한꺼번에 건너뛴다(소스 확인, 연습 3). 그래서 콜백 하나가 200 ms 를 쓰면 5 ms 타이머는 그 사이 수십 번을 그냥 놓친다. 제어 루프가 "늦게 도는" 게 아니라 **안 돈다**는 뜻이다.

스캔 순서 테스트(`order == "TSR"`)는 **이 모델의** 순서를 확인한다. ROS 2 문서는 타이머 우선 처리를 Eloquent 에서 제거했다고 적었고, 소스의 확인 순서는 여전히 타이머부터다. 언제 무엇이 먼저 도는지는 wait-set 결과에 달려 있으니, 실제 시스템에서 이 순서에 의존하면 안 된다.

### Part B — 왜 멈추는가, 어떻게 안 멈추고 알아채나

타이머 콜백 안에서 동기 호출을 하면 콜백은 `{스레드 슬롯, 그룹 토큰}` 을 쥔 채로 잠든다. 필요한 조건을 정리하면 이렇다.

```text
call_sync 가 끝나려면 → 응답 콜백이 돌아야 한다 (응답 콜백이 completed 를 세운다)
응답 콜백이 돌려면   → (a) 쉬는 스레드 하나  AND  (b) 응답이 속한 그룹을 가져갈 수 있어야 한다
```

실측 매트릭스(`solution.cpp` 의 실제 출력):

```text
executor               timer/client group       result   why
SingleThreaded         same MutuallyExclusive   STUCK    GROUP|THREAD
SingleThreaded         same Reentrant           STUCK    THREAD
SingleThreaded         separate groups          STUCK    THREAD
MultiThreaded(2)       same MutuallyExclusive   STUCK    GROUP
MultiThreaded(2)       same Reentrant           OK       -
MultiThreaded(2)       separate groups          OK       -
```

읽는 법:

| | 같은 MutuallyExclusive | 같은 Reentrant | 그룹 분리 |
|---|---|---|---|
| **Single** | 멈춤 (`GROUP\|THREAD`) | 멈춤 (`THREAD`) | 멈춤 (`THREAD`) |
| **Multi(2)** | 멈춤 (`GROUP`) | 통과 | 통과 |

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">실측 6칸 매트릭스: 응답 콜백이 돌려면 스레드와 토큰이 둘 다 필요하다</text>
<text x="210" y="50" fill="currentColor">같은 MutuallyExclusive</text>
<text x="415" y="50" fill="currentColor">같은 Reentrant</text>
<text x="600" y="50" fill="currentColor">그룹 분리</text>
<text x="20" y="100" fill="currentColor" font-weight="bold">Single (1)</text>
<text x="20" y="190" fill="currentColor" font-weight="bold">Multi (2)</text>
<rect x="150" y="60" width="190" height="70" class="f-warn" stroke="currentColor"/>
<text x="160" y="90" fill="currentColor">STUCK</text>
<text x="160" y="112" fill="currentColor" font-family="monospace" font-size="12">GROUP|THREAD</text>
<rect x="360" y="60" width="170" height="70" class="f-warn" stroke="currentColor"/>
<text x="370" y="90" fill="currentColor">STUCK</text>
<text x="370" y="112" fill="currentColor" font-family="monospace" font-size="12">THREAD</text>
<rect x="550" y="60" width="170" height="70" class="f-warn" stroke="currentColor"/>
<text x="560" y="90" fill="currentColor">STUCK</text>
<text x="560" y="112" fill="currentColor" font-family="monospace" font-size="12">THREAD</text>
<rect x="150" y="150" width="190" height="70" class="f-warn" stroke="currentColor"/>
<text x="160" y="180" fill="currentColor">STUCK</text>
<text x="160" y="202" fill="currentColor" font-family="monospace" font-size="12">GROUP</text>
<rect x="360" y="150" width="170" height="70" class="f-ok" stroke="currentColor"/>
<text x="370" y="180" fill="currentColor">OK</text>
<text x="370" y="202" fill="currentColor" font-size="12">토큰 없음</text>
<rect x="550" y="150" width="170" height="70" class="f-ok" stroke="currentColor"/>
<text x="560" y="180" fill="currentColor">OK</text>
<text x="560" y="202" fill="currentColor" font-size="12">다른 토큰</text>
<text x="20" y="242" fill="currentColor" font-size="12">Multi(8) + 같은 MutuallyExclusive 도 STUCK GROUP (solution.cpp 에서 CHECK).</text>
</svg>
```

실제 보고(같은 출력에서):

```text
--- 보고 (Multi(2), same MutuallyExclusive) ---
T0 BLOCKED timer:tick call#0
WaitsFor: call#0 needs group:node_default
cycle: timer:tick -> call#0 -> group:node_default -> timer:tick
--- 보고 (Single, same Reentrant) ---
T0 BLOCKED timer:tick call#0
WaitsFor: call#0 needs group:node_default
cycle: timer:tick -> call#0 -> thread-slot -> timer:tick
```

두 번째 보고는 그룹이 Reentrant 라 토큰이 문제가 아니다. `needs group` 줄은 어느 그룹에 속했는지만 말하고, 원인은 `cycle` 줄이 말한다.

**판정을 구조로 하는 이유.** 시간 초과만 쓰면 "충분히 길게" 기다려야 하고, 부하가 있으면 정상 호출도 실패로 볼 수 있다. 이 모델은 executor 의 상태를 다 알기 때문에, 상태를 바꿀 수 있는 주체가 없는 순간을 정확히 집어낸다.

```text
is_stuck =  blocked_ > 0                               (누군가 동기 호출에 막힘)
        AND in_flight_ == 0                            (네트워크 어딘가에서 오는 응답 없음)
        AND busy 인데 막히지 않은 스레드가 없음          (달리는 콜백이 풀어 줄 수 없음)
        AND (쉬는 스레드 0개 OR 가져갈 수 있는 일 없음)
```

호출·반납·응답 도착이 모두 `mu_` 안에서 `notify_all` 하기 때문에, 막힌 스레드는 마지막 상태 변화 직후 이 조건을 다시 평가한다. 깨우는 쪽을 빠뜨리면 탐지가 안 된다. 그래서 `call_sync` 는 블록 표시 뒤에도 `notify_all` 을 한다. 막힌 것 자체가 "다른 waiter 가 재평가해야 하는" 상태 변화이기 때문이다.

in-flight 조건이 왜 있는지는 테스트 두 개가 보여 준다. 응답을 붙잡은 채 Multi(2) + 그룹 분리로 돌리면 막혀 있어도 `Stuck` 이 아니고, 풀어 주면 `Ok` 다. 같은 테스트를 Single 로 돌리면 응답이 **도착하는 순간** 처음으로 `Stuck` 이 된다.

**실제 rclcpp 에는 이 판정기가 없다.** 다른 프로세스의 서버가 언제 답할지 executor 는 모른다. 쓸 수 있는 건 `future.wait_for(budget)` 이라는 시간 제한뿐이고, 이 레슨의 `detect=false` 경로가 그것이다(`Timeout`, 같은 `report` 내용). 그래서 실무 규칙은 "콜백 안에서는 무제한 대기를 쓰지 않는다"가 된다.

**늦은 응답.** 포기한 뒤에도 응답은 도착한다. 응답 콜백이 `abandoned` 를 보고 버리도록(`orphans_++`) 했고, 테스트가 "멈춘 칸마다 orphan 정확히 1"을 확인한다. rclcpp 의 `remove_pending_request` 가 맡는 역할과 같다고 알고 있지만, 레슨에서 확인하지는 않았다.

**서버가 같은 노드·같은 그룹이면** 응답 전에 **요청**이 막힌다. 이때 보고는 `WaitsFor: request needs call#0 needs group:node_default` 다.

**executor 밖 스레드의 동기 호출은 괜찮다.** `caller` 스레드는 슬롯도 토큰도 쥐지 않으므로, Single + 같은 그룹에서도 `Ok` 다. 흔히 쓰는 "main 에서 호출, 다른 스레드에서 spin" 패턴이 그래서 안전하다.

**Coffman 대응** (OS-M10 표와 같은 구조):

| 조건 | 여기서는 | 깨는 방법 | 대가 |
|---|---|---|---|
| 상호 배제 | MutEx 토큰, 스레드 슬롯 | **같은 Reentrant** 그룹 (토큰을 없앰) | 콜백이 스레드 안전해야 한다. 같은 콜백끼리도 겹친다 |
| 점유 대기 | 콜백이 스레드+토큰을 쥐고 응답을 기다림 | **async** + continuation | 흐름이 쪼개진다. 상태 기계가 생긴다 |
| 비선점 | executor 는 도는 콜백을 빼앗지 못함 | **시간 제한** 후 포기 | 일을 못 끝낸다. 늦은 응답을 처리해야 한다 |
| 순환 대기 | timer → 응답 → 토큰 → timer | **그룹 분리** (응답에 필요한 토큰을 다른 것으로) | 스레드가 하나 더 필요하다. 공유 데이터는 따로 지켜야 한다 |

스레드 슬롯도 이 표의 자원이다. 그래서 그룹만 분리한 Single 은 여전히 고리가 있고(`-> thread-slot ->`), 두 수정(그룹 분리, 스레드 2개)이 **함께** 있어야 멈추지 않는다.

### Part C — 수정 세 가지와 남는 함정

**수정 1. 그룹 분리 + Multi.** `fixed_groups` 는 타이머용과 클라이언트용 MutuallyExclusive 그룹을 따로 만든다. 문서에도 같은 구성이 있다. 타이머와 클라이언트는 "must not belong to the same Mutually Exclusive group" 이어야 한다. Reentrant 그룹에 함께 넣는 것도 목록에 있다. 다만 Reentrant 에는 아래 함정이 있고, 그래서 이 레슨은 `fixed_groups` 에서 MutEx 두 개를 택했다.

**수정 2. async + continuation.** 기다리지 않으면 고리의 첫 화살표가 사라진다. 6칸 모두 통과한다(`ok == 6`). 스레드가 1개면 순서도 정해진다. `order == "TR"`, 즉 타이머가 먼저 끝나고 그다음에 응답이 돈다. `async_chain` 은 sync 호출 3번을 응답 → 다음 호출로 잇는 것이다. 코드가 길어지는 대가로, 호출 하나에 스레드 하나를 낭비하지 않는다.

**함정: 스레드 고갈.** Reentrant 면 토큰 문제는 없지만, 콜백 두 개가 동시에 막히면 스레드 슬롯이 그대로 자원이 된다.

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
<defs><marker id="m3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
<text x="10" y="20" fill="currentColor" font-weight="bold">Multi(2) + 같은 Reentrant + 동기 호출 타이머 2개</text>
<rect x="20" y="40" width="160" height="56" class="f-warn" stroke="currentColor"/><text x="30" y="62" fill="currentColor">T0: t1 BLOCKED</text><text x="30" y="84" fill="currentColor">기다림 call#0</text>
<rect x="20" y="110" width="160" height="56" class="f-warn" stroke="currentColor"/><text x="30" y="132" fill="currentColor">T1: t2 BLOCKED</text><text x="30" y="154" fill="currentColor">기다림 call#1</text>
<rect x="290" y="70" width="170" height="60" class="f-acc" stroke="currentColor"/><text x="300" y="94" fill="currentColor">응답 call#0, call#1</text><text x="300" y="116" fill="currentColor">큐에 있음</text>
<line x1="180" y1="68" x2="288" y2="90" stroke="currentColor" stroke-width="2" marker-end="url(#m3)"/>
<line x1="180" y1="138" x2="288" y2="110" stroke="currentColor" stroke-width="2" marker-end="url(#m3)"/>
<text x="480" y="84" fill="currentColor">돌릴 스레드가 없다. 스레드 슬롯 고리.</text>
<text x="480" y="106" fill="currentColor">Multi(3) 이면 T2 가 응답을 돌린다.</text>
<text x="20" y="190" fill="currentColor">조건: 스레드 수 N &gt; 동시에 막힐 수 있는 콜백 수 K (K 개가 다 잠들어도 응답 처리할 한 자리)</text>
</svg>
```

테스트 결과: Multi(2) 는 `stuck == 1`, `oks == 1`. 하나가 포기하면 그 스레드가 풀려서 나머지 응답을 처리하기 때문에 정확히 하나만 실패한다. Multi(3) 은 둘 다 `Ok`. 이것은 OS-A11 의 worker pool 과 같은 문제다. 작업 스레드 수를 "동시에 막힐 수 있는 일 수"보다 크게 잡지 않으면, 스레드 풀 교착이 난다.

### ROS 2가 있을 때 (여기서 실행 안 함)

아래 코드는 문서와 소스 구조를 보고 쓴 **예시**이며 컴파일도 실행도 하지 않았다. API 인자 순서는 배포판마다 다르므로 그 버전의 헤더를 확인한다.

```cpp
// rclcpp (Jazzy 기준으로 작성, 미실행): 타이머와 클라이언트를 다른 MutuallyExclusive 그룹에 두고 MultiThreaded 로 돈다.
class Caller : public rclcpp::Node {
public:
  Caller() : Node("caller") {
    timer_cbg_ = create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    client_cbg_ = create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    client_ = create_client<AddTwoInts>("add_two_ints", rclcpp::ServicesQoS(), client_cbg_);
    timer_ = create_wall_timer(1s, std::bind(&Caller::on_timer, this), timer_cbg_);
  }
private:
  void on_timer() {
    auto req = std::make_shared<AddTwoInts::Request>();
    auto fut = client_->async_send_request(req);
    if (fut.wait_for(2s) != std::future_status::ready) {   // bounded wait
      RCLCPP_WARN(get_logger(), "service timeout");
      return;
    }
    RCLCPP_INFO(get_logger(), "sum=%ld", fut.get()->sum);
  }
  /* client_, timer_, timer_cbg_, client_cbg_ 멤버 생략 */
};

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  rclcpp::executors::MultiThreadedExecutor exec;
  auto node = std::make_shared<Caller>();
  exec.add_node(node);
  exec.spin();
}
```

```cpp
// async (미실행): 어느 executor 든 안 멈춘다. 콜백을 돌려주고 응답에서 이어 간다.
client_->async_send_request(req, [this](rclcpp::Client<AddTwoInts>::SharedFuture f) {
  RCLCPP_INFO(get_logger(), "sum=%ld", f.get()->sum);
});
```

```python
# rclpy (미실행): 그룹 분리 + MultiThreadedExecutor
timer_cbg = MutuallyExclusiveCallbackGroup()
client_cbg = MutuallyExclusiveCallbackGroup()
self.cli = self.create_client(AddTwoInts, 'add_two_ints', callback_group=client_cbg)
self.timer = self.create_timer(1.0, self.on_timer, callback_group=timer_cbg)
# ... rclpy.spin(node, executor=MultiThreadedExecutor())
```

재현 절차(VM 에 ros-jazzy 가 있을 때): 위 노드를 기본 콜백 그룹 + `rclcpp::spin(node)` 로 바꿔 돌리면 첫 호출에서 멈추는 것이 문서의 기본 예다. `ros2 run` 으로 서버를 띄운 뒤 `ros2 service list` 와 로그로 "요청은 도착했는데 응답 콜백이 안 돈다"를 확인한다. 콜백 안에서 `spin_until_future_complete` 를 부르면 교착이 아니라 예외가 날 가능성이 크다. 소스에 `"spin_until_future_complete() called while already spinning"` 문구가 있지만, 실제로 어떤 조합에서 나는지는 돌려 보지 못했다. rclcpp 의 예외 처리·버전 차이는 확인이 필요하다.

### 데모 — demo_matrix.cpp (벽시계 타이머, 탐지기 없음)

`demo_matrix.cpp` 는 `solution.cpp` 를 포함하고 탐지기를 끈다. 실제 rclcpp 와 같은 조건이다(시간 제한만 있음). 진짜 시간으로 타이머를 쏘고, 약 1 ms 지연을 준다. 실행 시각의 `uptime` 은 load 25.08 / 22.42 였다(다른 작성 세션이 같은 맥을 쓰는 중). **비율만 믿는다.** 머신: Apple M6, perflevel 2/4/6, Apple clang 21.0.0.

<!-- code: demo_matrix.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread demo_matrix.cpp -o dm && ./dm
```

```text
표 1: 20 ms 타이머, 콜백 안에서 호출, 응답 지연 약 1 ms, sync 예산 200 ms, 칸당 1000 ms, 탐지기 없음
executor           timer/client group       call     runs     ok  timeout   orphan
Single             same MutuallyExclusive   sync        6      0        6        6
Single             same Reentrant           sync        6      0        6        6
Single             separate groups          sync        6      0        6        6
Multi(2)           same MutuallyExclusive   sync        7      0        7        7
Multi(2)           same Reentrant           sync       51     51        0        0
Multi(2)           separate groups          sync       51     51        0        0
Single             same MutuallyExclusive   async      50     50        0        0

표 2: 5 ms 제어 타이머 + 100 ms 폴 타이머(콜백에서 서비스 호출), sync 예산 30 ms, 1.5초
구성                          max gap(us)  ctrl runs     ok  timeout
Single, 한 그룹, sync              35238        230      0       16
Multi(3), 그룹 분리, sync          9210        301     16        0
Single, 한 그룹, async              6258        301     16        0
```

(같은 프로그램의 직전 실행은 load 17.11 / 16.91 에서 멈춘 칸은 전부 `6 0 6 6`, 표 2 는 35127 / 10372 / 9173 us 였다. 칸의 판정은 같고 간격 숫자만 흔들렸다.)

읽을 것:

- **표 1**: 멈춘 칸은 1초 동안 타이머 콜백이 6번 정도만 돈다. 매번 sync 예산 200 ms 를 다 쓰고 응답을 버린다. 나머지 칸은 20 ms 주기대로 약 50번 돈다. async 는 Single + 같은 그룹이라는 **최악 조건**에서도 50번 다 돈다. `runs` 는 콜백 시작 횟수이고 합쳐진 타이머는 세지 않는다.
- **표 2**: 교착보다 흔한 피해는 **지터**다. 같은 실행 스레드에서 폴이 sync 예산 30 ms 를 쓰면 5 ms 제어 타이머의 최대 간격이 35 ms 까지 벌어지고, 실행 횟수도 230 대 300 으로 줄어든다. 그룹 분리(스레드 3개)와 async 는 둘 다 간격을 6–10 ms 범위로 되돌린다. 숫자는 Mac 의 부하에 따라 흔들린다. 실시간 측정은 ROS-A01 의 몫이다.

탐지기를 끈 이 데모와 `solution.cpp` 의 구조 판정이 **같은 6칸 결과**를 낸다(같은 칸이 멈춘다).

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread solution.cpp -o so && ./so
```

실제 결과: `63/63 passed`, exit 0 (8번 연속 같은 결과). ASan+UBSan(`-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer`) 깨끗, `-fsanitize=thread` 깨끗. `demo_matrix.cpp` 도 TSan 으로 깨끗하다. 스타터는 `23/63 passed`, exit 1 로 약 6초 안에 끝난다.

스레드 고갈 테스트가 `stuck == 1` 로 **정확히 하나**인 이유: 둘 다 막힌 순간 먼저 깨어난 스레드가 `mu_` 를 쥔 채 `Stuck` 을 판정하고 자기 블록 표시를 내린다. 다음 스레드가 재평가할 때는 busy 인데 막히지 않은 스레드가 있으니 조건 ③이 거짓이라 멈춤이 아니다. 앞 스레드의 콜백이 끝나면 그 스레드가 응답을 처리해서 뒤의 호출은 `Ok` 가 된다. 이 순서는 `mu_` 로 정해져 있어 타이밍과 상관없다.

## 7. 자주 하는 실수

**① MultiThreadedExecutor 로 바꾸고 끝낸다.** 노드의 기본 그룹은 MutuallyExclusive 다. 그룹을 안 바꾸면 스레드가 늘어나도 응답 콜백은 그 토큰을 못 얻는다. 매트릭스의 Multi(2)/same MutEx, Multi(8)/same MutEx 칸이 그 증거다. ROS 2 문서도 모든 것이 한 그룹이면 Multi 가 사실상 Single 로 동작한다고 적는다.
```cpp
// 틀림: 스레드만 늘림
rclcpp::executors::MultiThreadedExecutor exec;
// 고침: 타이머와 클라이언트를 다른 그룹으로 (또는 async)
```

**② Reentrant 면 무조건 안전하다고 생각한다.** Reentrant 는 토큰만 없앤다. Single 에서는 그대로 멈추고, Multi 에서도 막히는 콜백 수가 스레드 수에 도달하면 멈춘다(Part C). 동시에 막힐 수 있는 콜백이 K 개면 스레드는 K+1 개 이상이어야 한다. 게다가 같은 콜백이 겹쳐 돌 수 있어 데이터 레이스가 새로 생긴다(OS-B05).

**③ 시간 제한 없는 `future.get()`.** 멈춤이 영원한 멈춤이 된다. 이 레슨의 `call_sync` 는 항상 예산을 받는다. 예산을 쓰더라도 멈춤이 사라지는 것은 아니다(표 1: 1초에 6번만 돈다). 그래서 시간 제한은 탐지이지 수정이 아니다.

**④ 포기한 뒤의 늦은 응답을 안 처리한다.** 기다리는 쪽이 사라진 뒤 응답이 도착하면 지역 변수나 풀린 객체를 건드릴 수 있다. `abandoned` 표시로 버리거나 `shared_ptr` 로 상태 수명을 늘린다(연습 5).
```cpp
if (c.abandoned) { orphans_++; break; }   // 응답이 와도 쓰지 않는다
```

**⑤ 오는 중인 응답을 멈춤으로 판정한다.** in-flight 를 안 보면 외부 서버가 느릴 뿐인 정상 호출을 `Stuck` 으로 끊는다. `응답 붙잡기 → 풀어 주기` 테스트가 이 실수를 잡는다.

**⑥ 상태를 바꾸고 `notify_all` 을 안 한다.** 막힌 스레드가 재평가할 기회가 사라진다. 이 코드의 모든 상태 변화(요청, 응답 도착, 블록 표시, 토큰 반납, stop)는 `mu_` 안에서 `notify_all` 한다. 깨우기 하나만 빠져도 탐지가 안 되고 예산까지 기다리게 된다(그래도 멈추지는 않는다).

**⑦ 콜백 안에서 시간 끄는 일을 하고 "교착이 아니니 괜찮다"고 한다.** 표 2 의 지터를 보면 멈추지 않아도 제어 타이머는 늦는다. 콜백은 짧게 끝내고, 긴 일은 다른 스레드/다른 그룹으로 넘긴다. RTOS 에서 ISR 을 짧게 유지하고 하반부로 넘기는 것과 같은 규칙이다(RTOS-B08).

## 8. 면접 Q&A

<details><summary>Q1. 타이머 콜백에서 서비스를 동기로 호출했더니 멈춥니다. 왜죠?</summary>

응답은 executor 가 응답 콜백을 돌려야 완료된다. 그런데 타이머 콜백이 스레드와 (같은 MutuallyExclusive 그룹이면) 그룹 토큰을 쥔 채 그 완료를 기다린다. 응답 콜백에 필요한 자원을 기다리는 쪽이 쥐고 있으므로 순환 대기다. Single 이면 스레드가, Multi 에 같은 그룹이면 토큰이 고리를 이룬다.

**꼬리질문: MultiThreadedExecutor 로 바꾸면요?** 기본 그룹은 MutuallyExclusive 라서 그룹이 같으면 그대로 멈춘다. 이 레슨의 Multi(8) + same MutEx 도 STUCK(GROUP) 이었다. 그룹을 분리하거나 Reentrant 로 하거나 async 로 바꿔야 한다.
</details>

<details><summary>Q2. MutuallyExclusive 와 Reentrant 의 차이는 무엇이고, 다른 MutuallyExclusive 그룹 두 개는 어떻게 다른가요?</summary>

MutuallyExclusive 는 그룹 안의 콜백을 동시에 안 돌린다(토큰 1개). Reentrant 는 제한이 없어 같은 콜백이 자기 자신과도 겹친다. 서로 다른 MutuallyExclusive 그룹은 그룹끼리는 병렬이지만 각 콜백은 자기 자신과 겹치지 않는다. 그래서 제어 루프 타이머처럼 자기 겹침이 있으면 안 되는 콜백은 MutEx 그룹에 둔다.

**꼬리질문: 공유 데이터가 있으면요?** 같은 MutEx 그룹에 넣으면 프레임워크가 직렬화해 주고, 그룹을 나누면 직접 락이 필요하다(OS-B05). 다만 락을 쥔 채 동기 호출을 하면 같은 교착이 락으로 옮겨 간다.
</details>

<details><summary>Q3. 왜 Reentrant 그룹 + Multi 에서도 멈출 수 있나요?</summary>

스레드 슬롯 자체가 자원이기 때문이다. 동기 호출 콜백 K 개가 동시에 돌면 스레드 K 개가 잠들고, 응답을 돌릴 스레드가 없으면 교착이다. 조건은 N > K. 레슨의 실측: Multi(2) + 동기 타이머 2개는 `stuck == 1`, Multi(3) 은 둘 다 OK.

**꼬리질문: 호출이 체인으로 깊어지면?** 체인 깊이 d, 동시 체인 K 개면 스레드 K·d + 1 개가 필요하다(연습 4). 스레드 풀을 크게 잡는 것보다 체인에 async 를 쓰는 편이 낫다.
</details>

<details><summary>Q4. 이 교착을 Coffman 4조건으로 설명해 보세요. 수정마다 무엇을 깨나요?</summary>

상호 배제: MutEx 토큰과 스레드 슬롯은 한 콜백만 쓴다. 점유 대기: 콜백이 그것들을 쥔 채 응답을 기다린다. 비선점: executor 는 도는 콜백을 빼앗지 못한다. 순환 대기: timer → 응답 → 토큰 → timer. Reentrant 는 상호 배제를, async 는 점유 대기를, 시간 제한은 비선점을(스스로 포기), 그룹 분리는 순환 대기를 깬다. 그룹 분리만으로는 스레드 슬롯 고리가 남아서 스레드도 2개 이상 필요하다(표의 Single + separate 가 STUCK).

**꼬리질문: 시간 제한은 고친 건가요?** 아니다. 멈춤을 "느린 실패"로 바꾼 탐지다. 표 1 에서 1초에 6번만 돌았다.
</details>

<details><summary>Q5. 실제 시스템에서 이 교착을 어떻게 찾나요? 이 레슨의 구조 판정을 쓸 수 있나요?</summary>

그대로는 못 쓴다. 다른 프로세스의 서버가 언제 답할지 executor 가 모르기 때문이다(`in_flight_` 를 알 수 없다). 현실에서는 bounded wait(`future.wait_for`) 로 잡고, 타임아웃 시 어느 콜백이 무엇을 기다렸는지 로그를 남기며, `ros2_tracing` 으로 콜백 시작/끝을 보고(ROS-A02), gdb 로 executor 스레드 스택을 본다. 설계 단계에서 잡으려면 이 레슨의 `predict` 처럼 구성을 보고 판정하는 lint 를 쓴다(연습 1).

**꼬리질문: 재현이 어렵지 않나요?** 이 교착은 타이밍이 아니라 구조에서 나온다. 응답이 오기만 하면 첫 호출에서 확정된다. 재현이 어려운 쪽은 Part C 의 스레드 고갈처럼 "동시에 K 개가 막혀야" 하는 경우다. 이때는 이 레슨처럼 테스트에서 두 타이머를 강제로 준비시키거나, TEST-A05 의 결정론적 시뮬레이션을 쓴다.
</details>

<details><summary>Q6. 타이머가 콜백보다 짧은 주기로 트리거되면 어떻게 되나요?</summary>

타이머는 큐가 아니라 깃발이다. rcl 의 `rcl_timer_call` 은 다음 호출 시각을 이전 예정 시각에서 한 주기 뒤로 잡고, 이미 지나갔으면 놓친 주기를 건너뛴다. 콜백이 밀려도 몰아서 실행되지 않고 위상은 유지된다. 다만 Reentrant 그룹이면 같은 타이머 콜백이 자기 자신과 겹칠 수 있다.

**꼬리질문: 제어 루프에서는 어떻게 하죠?** MutuallyExclusive 그룹에 두어 겹침을 막고, 다른 콜백이 스레드를 잡아먹지 않도록 전용 그룹/전용 executor 를 쓴다. 문서는 `add_callback_group` 으로 그룹을 다른 executor 에 나누어 OS 스케줄러로 스레드 우선순위를 줄 수 있다고 설명한다.
</details>

<details><summary>Q7. 펌웨어에서 같은 패턴은 어디서 보나요?</summary>

FreeRTOS 소프트웨어 타이머 콜백은 타이머 서비스 태스크에서 돌기 때문에 블로킹하면 다른 타이머가 전부 밀린다(문서 규칙은 기억에 따른 것이다). 메인 루프가 ISR 을 기다리되 ISR 이 하반부 태스크를 깨워야 끝나는 구조, 혹은 Linux 워크큐에서 자기 워크를 flush 하는 것도 같은 모양이다(ELX-M06 영역, 여기서 실행해 보지 않았다). 공통 원칙은 "실행 컨텍스트가 하나인 곳에서는 그 컨텍스트가 돌려야 끝나는 일을 기다리지 않는다"이다.
</details>

## 9. 추가 연습

모든 답은 `practice.cpp` 에 있다. 본문 코드와 독립이고 전부 결정적이다.

### 연습 1 — 정적 판정 (lint)

구성만 보고 멈춤을 예측하는 `predict(threads, arrange)` 를 쓴다. `solution.cpp` 의 실측 6칸 + Multi(8) 을 기댓값으로 비교한다.

<details><summary>답</summary>

```cpp
static Prediction predict(int threads, Arrange a) {
    unsigned why = WhyNone;
    if (a == Arrange::SameMutEx) why |= WhyGroup;
    if (threads <= 1) why |= WhyThread;
    return Prediction{why != WhyNone, why};
}
```
같은 Reentrant 는 스레드만 본다. 이 표가 실측 매트릭스와 같다는 것이 이 판정의 근거다. 실제 프로젝트에서는 노드 구성을 파싱해 CI 에서 돌릴 수 있다.
</details>

### 연습 2 — 대기 그래프에서 순환 찾기

`std::map<std::string, std::vector<std::string>>` 그래프에서 순환을 `a -> b -> a` 형식으로 찾는다(OS-M11 의 lockdep 과 같은 일). 정렬된 map 의 첫 순환을 돌려준다.

<details><summary>답</summary>

경로 스택과 `done` 집합을 둔 DFS 다. 다음 노드가 경로에 있으면 거기서 고리를 끊어 문자열로 만들고, `done` 은 고리가 없다고 확인된 노드를 다시 안 본다. `practice.cpp` 의 `dfs` / `find_cycle`. 입력 `timer:tick → call#0 → group:cb → timer:tick` 은 정렬 순서상 `call#0` 부터 시작해 `call#0 -> group:cb -> timer:tick -> call#0` 을 돌려준다.
</details>

### 연습 3 — rcl 타이머의 다음 호출 시각

`next_call_after(next, now, period)` 를 쓴다. 이전 예정 시각 + 한 주기로 옮기고, 이미 지났으면(`next <= now`) 놓친 주기를 건너뛴다. 이것으로 `timer_starts(period, duration, horizon)` 을 구현한다.

<details><summary>답</summary>

```cpp
static long next_call_after(long next_call, long now, long period) {
    next_call += period;
    if (next_call <= now) {
        long ahead = now - next_call;
        next_call += (1 + ahead / period) * period;
    }
    return next_call;
}
```
검증값: `next_call_after(20, 35, 10) == 40`, `next_call_after(40, 60, 10) == 70`, 10 ms 주기·25 ms 콜백의 시작은 `10, 35, 60, 85, 110`. 코드는 rcl 의 `timer.c` 설명을 옮겼고, 실제 rcl 을 실행해 대조하지는 않았다.
</details>

### 연습 4 — 최소 스레드 수

동시 동기 호출 체인 K 개, 체인 깊이 d 일 때 필요한 최소 스레드를 공식으로 내고, 모든 스케줄을 도는 DFS 로 맞는지 확인한다.

<details><summary>답</summary>

체인마다 단계가 스레드 하나씩을 쥐고 내려간다. `K·d` 개가 다 차면 응답을 돌릴 자리가 없으니 `min_threads = K·d + 1`. `can_wedge(threads, ...)` 는 체인을 한 칸씩 내리는 모든 순서를 DFS 로 돌고, 스레드가 꽉 찬 상태에 도달하면 true 다. 공식의 ±1 을 K,d ∈ {1,2,3} 전부에서 확인한다. 본문 Part B 는 `K=1,d=1` → 2, Part C 는 `K=2,d=1` → 3 이다.
</details>

### 연습 5 — BoundedPromise

시간 제한 있는 대기 + 늦은 응답을 안전하게 버리는 객체를 만든다. 기다리는 쪽이 먼저 사라져도 `set` 이 안전해야 하고(상태를 `shared_ptr` 로), 진짜 `std::thread` 로 검증한다.

<details><summary>답</summary>

`State` 를 `std::shared_ptr` 로 두고 `set` 은 복사된 핸들로 호출한다. 대기 쪽이 시간 초과되면 `abandoned=true`, 늦은 `set` 은 `late++` 만 하고 false 를 돌려준다. 순서는 시간이 아니라 코드로 고정했다(시간 초과된 wait 뒤에 스레드를 시작). `practice.cpp` 의 `BoundedPromise` 는 ASan, TSan 모두 깨끗하다.
</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread practice.cpp -o pr && ./pr
```

실제 결과: `41/41 passed`, exit 0.

## 10. 핵심 정리

- 콜백이 끝나야 처리되는 응답을 콜백 안에서 기다리면 교착이다. 필요한 자원은 **스레드 슬롯**과 **그룹 토큰** 둘이다.
- 노드 기본 그룹은 MutuallyExclusive 다. 스레드만 늘려도 그룹이 같으면 그대로 멈춘다(Multi(8) STUCK GROUP).
- 6칸 실측: Multi(2) + 같은 Reentrant, Multi(2) + 그룹 분리만 통과. 나머지 4칸은 멈춘다.
- Reentrant 는 토큰만 없앤다. N > K(동시에 막힐 수 있는 콜백 수)를 지켜야 하고 겹침 레이스가 생긴다.
- 수정: 그룹 분리 + Multi(순환 대기), Reentrant(상호 배제), async(점유 대기), 시간 제한(비선점, 탐지일 뿐 수정이 아님).
- 타이머는 ready 깃발이다. 밀린 주기는 쌓이지 않고 건너뛰며 위상은 유지된다.
- 탐지는 구조로 한다: 달리는 콜백 없음 + 오는 응답 없음 + 쉬는 스레드/가져갈 일 없음. 실제 ROS 2 에서는 in-flight 를 모르므로 bounded wait + 로그가 현실적인 방법이다.
- 멈추지 않아도 sync 호출은 제어 타이머를 늦춘다(최대 간격 35 ms 대 6–10 ms).

## 참고자료

- rclcpp `jazzy` 소스: `rclcpp/src/rclcpp/executor.cpp` (get_next_ready_executable, execute_any_executable, 콜백 그룹 처리), `rclcpp/src/rclcpp/executors/multi_threaded_executor.cpp` (run, 스레드 수 기본값): <https://github.com/ros2/rclcpp/tree/jazzy/rclcpp/src/rclcpp>
- rcl `jazzy`: `rcl/src/rcl/timer.c` (`next_call_time` 계산): <https://github.com/ros2/rcl/blob/jazzy/rcl/src/rcl/timer.c>
- ROS 2 문서 (Jazzy), RST 원본으로 읽음: *Using Callback Groups* (`source/How-To-Guides/Using-callback-groups.rst`), *Executors* (`source/Concepts/Intermediate/About-Executors.rst`): <https://github.com/ros2/ros2_documentation/tree/jazzy/source> (사람이 보는 페이지: <https://docs.ros.org/en/jazzy/How-To-Guides/Using-callback-groups.html>)
- OS-M10 (Coffman 4조건, 대기 그래프), OS-M11 (mini lockdep), OS-B05 (뮤텍스), OS-B06 (조건 변수), RTOS-B08 (ISR → 태스크 지연 처리), SIV-M09 (스트레스 테스트와의 경계), TEST-A05 (결정론적 시뮬레이션), ROS-A01 (실시간 ROS 2 지터), ROS-A02 (ros2_tracing), OS-A11 (worker pool 서비스)
- E. G. Coffman, M. J. Elphick, A. Shoshani, "System Deadlocks", *ACM Computing Surveys* 3(2), 1971 (OS-M10 에서 재인용)
