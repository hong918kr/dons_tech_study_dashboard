---
id: DIST-A05
time: 110분
updated: 2026-11-08
---
# DIST-A05 선형성(Linearizability) 검사기

## 학습 목표

- 선형성의 정의(각 연산이 호출과 응답 사이 **한 순간**에 일어난 것으로 설명되는 단일 순서)를 말로 하고, 순차 일관성과 어디서 갈리는지 반례 두 줄로 보인다.
- Wing-Gong 백트래킹을 구현하고, `(선형화된 연산 집합, 상태)` 메모(Lowe)가 왜 맞고 얼마나 줄이는지 실제 숫자로 설명한다.
- timeout 으로 끝난 연산(미완료, pending)을 "일어났을 수도, 안 일어났을 수도" 로 올바르게 처리한다.
- 결함 주입 quorum 저장소에서 위반 히스토리를 찾아 **2~4개 연산짜리 최소 반례**로 줄이고, 같은 seed 집합으로 고친 설정이 통과함을 보인다.
- `R+W>N` 이 왜 선형성의 충분조건이 아닌지, 무엇을 더해야 하는지를 검사기 결과로 답한다.

## 1. 문제

펌웨어·검증 일의 언어로 말하면 이 레슨은 "복제된 상태가 진짜로 **한 개처럼** 보이는가를 로그만 보고 판정하는 체커" 다 — 이중화 ECU 의 설정 저장소, 3중화 센서 값, 락프리 큐(DS-A03)까지 전부 같은 질문이고, 분산 시스템 면접의 단골이다.

SIV-M09 의 스트레스 테스트는 "값이 깨졌나" 를 체크섬으로 본다. 그런데 복제 저장소의 버그는 값이 깨지지 않는다. **모든 값은 누군가 실제로 쓴 값이고, 답의 순서만 불가능하다.** 체크섬으로는 절대 안 잡힌다. 그래서 각 클라이언트가 "언제 불렀고 언제 답을 받았고 뭘 받았나" 만 기록하고, 사후에 **그 답들을 설명하는 한 줄 순서가 존재하는지** 찾는다.

아래 두 히스토리에서 깨진 것은 없다. 왼쪽은 어떤 순서로도 설명이 되고, 오른쪽은 어떤 순서로도 안 된다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">가로축은 실제 시간. 막대는 호출(왼쪽 끝) ~ 응답(오른쪽 끝). 점은 연산이 "일어났다" 고 치는 순간.</text>
  <text x="10" y="50" fill="currentColor" font-weight="bold">선형화 가능</text>
  <text x="10" y="82" fill="currentColor">c0</text>
  <rect x="40" y="68" width="280" height="20" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="50" y="83" fill="currentColor">w(1)</text>
  <circle cx="215" cy="78" r="5" fill="currentColor"/>
  <text x="10" y="122" fill="currentColor">c1</text>
  <rect x="60" y="108" width="100" height="20" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="66" y="123" fill="currentColor">r → None</text>
  <circle cx="110" cy="118" r="5" fill="currentColor"/>
  <text x="10" y="162" fill="currentColor">c2</text>
  <rect x="190" y="148" width="100" height="20" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="196" y="163" fill="currentColor">r → 1</text>
  <circle cx="250" cy="158" r="5" fill="currentColor"/>
  <text x="40" y="200" fill="currentColor">순서: r→None, w(1), r→1 → 레지스터 명세와 일치</text>

  <text x="400" y="50" fill="currentColor" font-weight="bold">선형화 불가능 (new-old inversion)</text>
  <text x="400" y="82" fill="currentColor">c0</text>
  <rect x="430" y="68" width="280" height="20" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="440" y="83" fill="currentColor">w(1)</text>
  <text x="400" y="122" fill="currentColor">c1</text>
  <rect x="450" y="108" width="100" height="20" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="456" y="123" fill="currentColor">r → 1</text>
  <text x="400" y="162" fill="currentColor">c2</text>
  <rect x="580" y="148" width="100" height="20" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="586" y="163" fill="currentColor">r → None</text>
  <text x="400" y="200" fill="currentColor">r→1 이 끝난 뒤 r→None 이 시작했다.</text>
  <text x="400" y="220" fill="currentColor">w(1) 은 r→1 앞이어야 하고, r→None 은 w(1) 앞이어야</text>
  <text x="400" y="240" fill="currentColor">하므로 순환 → 설명 불가</text>
</svg>
```

문제의 깊이는 **"quorum 이면 되는 것 아닌가?"** 에서 나온다. `R+W>N` 을 지켜도 오른쪽 그림이 그대로 재현된다. 이 레슨의 저장소는 DIST-M05 가 아직 쓰이지 않아 안에 작은 시뮬레이터를 내장했다.

### Part A — 검사기

`check_linearizable(ops, model, memo=True, max_nodes=None)` 을 구현한다. 입력은 `Op(client, key, kind, val, t_inv, t_ret)` 목록.

- 실시간 순서: `A.t_ret < B.t_inv` (엄격) 일 때만 A 가 B 앞이다. 같은 시각은 겹친 것.
- `t_ret is None` 인 미완료 연산은 호출 이후 언제 적용됐을 수도, 전혀 안 적용됐을 수도 있다.
- 반환 `Result(ok, order, best, nodes)`: 성공하면 선형화 순서, 실패하면 가장 멀리 간 부분 순서, `nodes` 는 `step` 호출 수.
- 임의 히스토리 500개에서 전수 탐색 정답과 일치해야 한다.

### Part B — 키 분할, 최소 반례, 타임라인

- `check_kv(ops)`: 키별로 나눠 검사하고 첫 위반 키를 돌려준다. 통째로 검사한 판정과 같아야 한다(선형성의 국소성, locality).
- `shrink(ops)`: 위반을 유지하면서 연산을 지워 1-최소로 줄인다. 읽기가 돌려준 값의 쓰기는 지우면 안 된다.
- `render(ops)`: 사람이 읽을 ASCII 타임라인 (제공됨).

### Part C — 결함 주입 quorum 저장소

`QuorumStore` (가상 시계, 이벤트 큐, seed 하나의 PRNG)의 비어 있는 프로토콜 조각 `on_read_quorum`, `on_write_query_quorum` 과 `sweep(cfg, seeds)` 를 채운다.

| 설정 | N | R | W | write-back | 타임스탬프 | 기대 |
|---|---|---|---|---|---|---|
| `R1W1 none` | 3 | 1 | 1 | 없음 | 조회 | 위반 (R+W≤N) |
| `R2W1 none` | 3 | 2 | 1 | 없음 | 조회 | 위반 (R+W≤N) |
| `R1W3 none`, `R2W2 none` | 3 | | | 없음 | 조회 | R+W>N 이지만 위반 |
| `R3W1 none` | 3 | 3 | 1 | 없음 | 조회 | 직접 확인 |
| `R2W2 async` | 3 | 2 | 2 | 보내고 안 기다림 | 조회 | 직접 확인 |
| `R2W2 sync/local-ts` | 3 | 2 | 2 | W ack 대기 | 어긋난 로컬 시계 | 위반 |
| `R2W2 sync/amnesia` | 3 | 2 | 2 | W ack 대기 | 조회 | ack 후 데이터 소실 → 위반 |
| `R2W2 sync (ABD)`, `N5 R3W3 sync` | 3/5 | | | W ack 대기 | 조회 | 같은 seed 통과 |

## 2. Problem (English)

Given a history of client operations on a key-value store, decide whether it is linearizable.

```text
Op(client, key, kind, val, t_inv, t_ret)   kind: "w" (val = written value) or "r" (val = value returned)
                                           t_ret = None means the client never saw a response.

check_linearizable(ops, model=RegisterModel, memo=True, max_nodes=None)
    -> Result(ok, order, best, nodes)
check_kv(ops) -> (ok, bad_key_or_None, {key: Result})
shrink(ops, model=RegisterModel) -> normalized minimal non-linearizable sub-history
sweep(cfg, seeds, store_cls=QuorumStore)
    -> Sweep(violations, seeds, completed, total, first_seed, first_key, minimal)
```

Rules: A precedes B in real time iff `A.t_ret < B.t_inv`. Every completed operation must appear exactly once in the linearization; a pending write may appear at any point after its invocation or not at all; a pending read can be ignored. Python 3.9, standard library only, no wall clock.

Example:

```text
w(1) [0,10], r->1 [1,2], r->None [3,4]      => not linearizable (new-old inversion)
w(1) [0,None], r->1 [5,6]                   => linearizable (the pending write took effect)
w(1) [0,5], r->None [5,6]                   => linearizable (equal timestamps are concurrent)
```

Then build a quorum-replicated KV simulator with message delay, loss and replica crash, and find the configurations whose histories are not linearizable. Follow-ups: why is `R+W>N` not sufficient? What is the worst-case complexity and how do you keep it practical?

## 3. 시작 코드

<!-- code: starter.py -->

```bash
cd lessons/DIST/DIST-A05
python3 -I starter.py ; echo "exit=$?"
```

실행 결과 (TODO 상태, 일부만 표시):

```text
FAIL A empty history: TODO
FAIL A sequential write then read: TODO
…
PASS B render timeline
FAIL C same seed replays the identical history: TODO
…
FAIL C R2W2 minimal counterexample is 1 write + 2 ordered reads: TODO
1/36 passed
exit=1
```

`render`, `normalize`, `well_formed`, 전수 탐색 정답지(`brute_force`), 이벤트 엔진과 복제본 동작은 제공된다. 각 CHECK 에는 60초 alarm 이 걸려 있어 틀린 구현이 무한 루프를 돌아도 하네스가 끝난다.

## 4. 힌트

<details><summary>힌트 1 — 탐색 공간을 말로 먼저</summary>

"지금까지 선형화한 연산 집합" 과 "그 뒤의 저장소 상태" 만 알면 앞으로 뭘 할 수 있는지가 전부 결정된다. 다음 후보는 **아직 안 고른 연산 중, 그보다 먼저 끝나야 하는 연산이 하나도 안 남은 것** 들이다. 이벤트 리스트를 앞에서 훑을 때 첫 return 이전에 있는 call 들이 정확히 그 후보다.
</details>

<details><summary>힌트 2 — 왜 return 을 만나면 되돌아가나</summary>

리스트 앞에서 call 을 지나 어떤 return 을 만났다는 건, 그 연산을 고르지 않고 뒤로 갈 수 없다는 뜻이다. 후보를 다 시도해 실패했으니 직전 선택을 취소한다.
</details>

<details><summary>힌트 3 — 메모 키</summary>

같은 `(선형화 집합, 상태)` 에 다른 순서로 도달했다면 앞으로의 결과가 같으니 다시 안 본다. 집합은 정수 비트마스크, 상태는 해시 가능한 값이면 그대로 `set` 에 넣는다.
</details>

<details><summary>힌트 4 — 미완료 연산</summary>

return 노드를 아예 만들지 않으면 된다. 그러면 그 호출은 리스트에 끝까지 남아 후보로 계속 시도되고, 안 골라도 막히지 않는다. 종료 조건은 "완료 연산을 전부 골랐다" 다.
</details>

<details><summary>힌트 5 — 쓰기 값이 유일해야 축소가 안전하다</summary>

읽기는 지워도 안전하다(상태를 안 바꾼다). 쓰기는 **아무도 그 값을 안 읽을 때만** 지워도 된다. 지우고 나서 읽기가 쓰인 적 없는 값을 돌려주는 히스토리가 되면 반례가 아니라 인공물이다.
</details>

<details><summary>힌트 6 — quorum 프로토콜</summary>

읽기는 R 응답 중 최대 타임스탬프를 고른다. "지금 고른 값이 W 개 복제본에 있다" 가 보장되지 않으면 다음 읽기가 못 볼 수 있다. 그래서 답하기 전에 W 개에 써 둔다.
</details>

## 5. 풀이

### Part A — 검사기

**정의.** 히스토리 H 가 선형화 가능하다 = (완료 연산 전부 + 미완료 연산 일부)를 한 줄로 늘어놓을 수 있고, ① 실시간으로 앞선 연산이 앞에 오며 ② 그 줄을 순차 명세로 실행한 결과가 관측된 답과 같다. 각 연산의 "일어난 순간" 을 호출과 응답 사이에 찍을 수 있다는 말과 같다.

**Wing-Gong 은 이 정의를 그대로 깊이우선 탐색한다.** 이벤트를 시간순 연결 리스트로 만들고:

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">이벤트 리스트 위의 백트래킹 (w(1)[0,10], r→1[1,2], r→None[3,4])</text>
  <g font-family="monospace">
  <text x="10" y="52" fill="currentColor">시작</text>
  <rect x="70" y="38" width="62" height="22" class="f-acc" stroke="currentColor"/><text x="76" y="54" fill="currentColor">call W</text>
  <rect x="140" y="38" width="62" height="22" class="f-acc" stroke="currentColor"/><text x="146" y="54" fill="currentColor">call R1</text>
  <rect x="210" y="38" width="62" height="22" class="f-mut" stroke="currentColor"/><text x="216" y="54" fill="currentColor">ret R1</text>
  <rect x="280" y="38" width="62" height="22" class="f-acc" stroke="currentColor"/><text x="286" y="54" fill="currentColor">call R2</text>
  <rect x="350" y="38" width="62" height="22" class="f-mut" stroke="currentColor"/><text x="356" y="54" fill="currentColor">ret R2</text>
  <rect x="420" y="38" width="62" height="22" class="f-mut" stroke="currentColor"/><text x="426" y="54" fill="currentColor">ret W</text>

  <text x="10" y="102" fill="currentColor">W 선택</text>
  <rect x="140" y="88" width="62" height="22" class="f-acc" stroke="currentColor"/><text x="146" y="104" fill="currentColor">call R1</text>
  <rect x="210" y="88" width="62" height="22" class="f-mut" stroke="currentColor"/><text x="216" y="104" fill="currentColor">ret R1</text>
  <rect x="280" y="88" width="62" height="22" class="f-acc" stroke="currentColor"/><text x="286" y="104" fill="currentColor">call R2</text>
  <rect x="350" y="88" width="62" height="22" class="f-mut" stroke="currentColor"/><text x="356" y="104" fill="currentColor">ret R2</text>
  <text x="500" y="104" fill="currentColor">state = 1, W 의 ret 도 함께 뺀다</text>

  <text x="10" y="152" fill="currentColor">R1 선택</text>
  <rect x="280" y="138" width="62" height="22" class="f-warn" stroke="currentColor"/><text x="286" y="154" fill="currentColor">call R2</text>
  <rect x="350" y="138" width="62" height="22" class="f-mut" stroke="currentColor"/><text x="356" y="154" fill="currentColor">ret R2</text>
  <text x="440" y="154" fill="currentColor">R2 기대 None, state 1 → 불가능</text>

  <text x="10" y="202" fill="currentColor">ret R2 도달</text>
  <text x="130" y="202" fill="currentColor">R2 를 못 골랐는데 끝나 버림 → R1 취소, ret R1 도달 → W 취소</text>
  <text x="10" y="242" fill="currentColor">W 복구 후</text>
  <text x="130" y="242" fill="currentColor">다음 후보 R1: state None 인데 답이 1 → 불가능 → ret R1 도달, 스택 빈 → 실패</text>
  <text x="10" y="282" fill="currentColor">결과</text>
  <text x="130" y="282" fill="currentColor">Result(ok=False, order=None, best=[0, 1], nodes=4)  ← 실제 출력</text>
  </g>
</svg>
```

핵심 불변식 세 가지:

| 불변식 | 왜 |
|---|---|
| 첫 return 앞의 call 만 후보가 된다 | 그 return 의 연산을 아직 안 골랐다면, 그 return 뒤에 시작한 연산은 그 앞으로 올 수 없다 |
| 연산을 고르면 call 과 return 을 둘 다 리스트에서 뺀다 | 고른 연산의 return 은 더는 다른 연산을 막지 않는다 |
| 되돌릴 때는 뺀 역순으로 복원한다 | 이중 연결 리스트의 "빼기" 는 역순 복원일 때만 원래대로 돌아온다(dancing links 와 같은 원리) |

**메모의 정당성(Lowe).** 어떤 시점의 미래는 `(선형화 집합 S, 상태 s)` 만의 함수다. 같은 `(S, s)` 에 다른 순서로 왔다면 전에 실패한 것을 다시 할 필요가 없다. **상태를 키에서 빼면 틀린다.** `S` 가 같아도 쓰기 순서에 따라 상태가 다르고, 어떤 상태는 성공할 수 있기 때문이다. 메모 없이는 순서(순열)를 세고, 메모가 있으면 부분집합을 센다. 차이는 아래 실행 시간 표에서 본다.

**미완료 연산.** timeout 으로 답을 못 받은 쓰기는 이미 복제본에 내려앉았을 수도, 아닐 수도 있다. 알 수 없으니 두 경우를 다 허용해야 한다. 코드에서는 return 노드를 안 만들 뿐이다. 호출 노드는 리스트에 끝까지 남고, 종료 조건이 `remaining == 0` (완료 연산 전부) 이라 안 골라도 괜찮다. 미완료 **읽기**는 답을 모르는 채 `val=None` 으로 기록된다. 후보로 시도돼도 상태를 안 바꾸므로 판정에 영향이 없다.

**같은 시각.** `build_events` 는 `(t, 0=invoke / 1=return, i)` 로 정렬한다. 같은 틱이면 invoke 가 먼저 와서 두 연산이 겹친 것으로 처리된다. 벽시계 해상도가 낮으면 진짜 선후가 뭉개지지만, 그 경우에는 검사기가 **관대해질** 뿐 없는 위반을 만들지는 않는다. 안전한 쪽으로 틀린다.

전수 탐색 대조: `random_history` 500개를 `brute_force`(미완료 부분집합 × 모든 순열) 정답과 비교한다. 양쪽 판정이 다 나오는지(각 30개 이상)도 확인한다. 이런 체커를 만들 때 가장 가치 있는 테스트가 이 교차검증이다 — 체커 자체의 버그가 "위반 없음" 으로 보이면 알아챌 길이 없다.

### Part B — 키 분할, 최소 반례

**국소성(locality).** 선형성은 "모든 객체가 각각 선형화 가능하면 전체도 선형화 가능" 을 만족한다 (Herlihy & Wing). 그래서 키 하나짜리 탐색 여러 개로 나눌 수 있다. 직렬성(serializability)에는 이 성질이 없다. `check_kv` 와 통째로 돌리는 `KvModel` 의 판정이 300개 히스토리에서 같은 것을 CHECK 로 확인했다.

**축소가 왜 이렇게 생겼나.** 위반 히스토리에서 연산을 지워도 위반이 유지되는지 재검사만 하면 될 것 같지만, 지우는 것 자체가 위반을 **만들 수 있다.** `w(1)` 을 지우면 남은 `r→1` 은 쓰인 적 없는 값을 읽은 것이 되어 어떤 순서로도 설명이 안 된다. 그건 저장소의 버그가 아니라 축소가 만든 인공물이다.

쓰기 값이 유일하다는 가정 아래, 지워도 되는 경우는 정확히 둘이다:

| 지우는 것 | 왜 선형화 가능성을 해치지 않나 |
|---|---|
| 읽기 | 읽기는 상태를 안 바꾼다. 선형화 순서에서 그 읽기만 빼면 나머지 답은 그대로 설명된다 |
| 아무도 값을 안 읽은 쓰기 | 그 쓰기 뒤에서 다음 쓰기 전까지 읽기가 없으므로(있었다면 그 값을 읽었을 것), 순서에서 빼도 모든 읽기의 "직전 쓰기" 가 그대로다 |

따라서 **H 가 선형화 가능 → H 에서 허용된 삭제를 한 H′ 도 선형화 가능** 이다. 대우: H′ 가 위반이면 H 도 위반이다. 축소 결과는 원래 위반의 진짜 증거다. CHECK `B allowed removals never create a violation` 이 임의 히스토리로 이 정리를 확인한다. CAS 같은 연산이 들어오면 "지워도 안전" 이 깨지므로 그때는 이 정리를 다시 세워야 한다.

`shrink` 는 전역 최소가 아니라 **1-최소**(더 지울 수 있는 연산이 없음)다. 한 번에 두 연산을 지워야 작아지는 경우는 놓친다.

### Part C — 결함 주입 quorum 저장소

**프로토콜 조각:**

- 읽기: 전 복제본에 GET, 먼저 온 R 개에서 최대 `(ts, val)` 선택 → writeback 설정대로 처리.
- 쓰기: ts 조회 모드면 전 복제본에 GET, R 개 응답의 최대 counter + 1 로 `(counter, op_id)` 생성 → 전 복제본에 PUT, W 개 ack 후 응답. 복제본은 `ts > 저장된 ts` 일 때만 덮어쓴다.
- `writeback=="sync"` + ts 조회를 합친 것이 **ABD** (Attiya-Bar-Noy-Dolev) 의 다중 writer 변형이다.

**왜 R+W>N 만으로 부족한가.**

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">R2W2, write-back 없음: 쓰기가 복제본 A 에만 먼저 도착한 구간</text>
  <text x="10" y="55" fill="currentColor">A</text>
  <line x1="50" y1="50" x2="720" y2="50" stroke="currentColor"/>
  <text x="10" y="105" fill="currentColor">B</text>
  <line x1="50" y1="100" x2="720" y2="100" stroke="currentColor"/>
  <text x="10" y="155" fill="currentColor">C</text>
  <line x1="50" y1="150" x2="720" y2="150" stroke="currentColor"/>
  <rect x="130" y="38" width="590" height="24" class="f-ok" stroke="currentColor"/>
  <text x="140" y="55" fill="currentColor">A: ts1 = 1 (새 값)</text>
  <rect x="560" y="88" width="160" height="24" class="f-ok" stroke="currentColor"/>
  <text x="570" y="105" fill="currentColor">B: ts1 도착 (지연)</text>
  <rect x="600" y="138" width="120" height="24" class="f-ok" stroke="currentColor"/>
  <text x="608" y="155" fill="currentColor">C: ts1 도착</text>
  <rect x="180" y="190" width="130" height="24" class="f-acc" stroke="currentColor"/>
  <text x="188" y="207" fill="currentColor">r1: {A,B} → 1</text>
  <rect x="360" y="190" width="150" height="24" class="f-warn" stroke="currentColor"/>
  <text x="368" y="207" fill="currentColor">r2: {B,C} → None</text>
  <rect x="100" y="230" width="580" height="24" class="f-mut" stroke="currentColor"/>
  <text x="108" y="247" fill="currentColor">쓰기 w(1): W=2 ack 를 아직 못 받음 (호출 ~ 응답이 r1, r2 를 덮음)</text>
  <text x="10" y="282" fill="currentColor">{A,B} 와 {B,C} 는 B 에서 겹치지만 B 는 아직 새 값이 없다. r1 이 끝난 뒤 r2 가 시작 → 실시간 순서 위반</text>
</svg>
```

교집합 보장은 "완료된 쓰기를 다음 읽기가 본다" 만 준다. **아직 진행 중인 쓰기**는 어떤 읽기 quorum 은 보고 어떤 읽기 quorum 은 못 본다. r1 이 새 값을 봤는데 그게 W 개에 있다는 보장이 없으므로, 뒤따르는 r2 가 옛 값을 돌려줘도 quorum 규칙은 지켜진 것이다. 고치는 방법은 하나다. **읽기가 답하기 전에 자기가 고른 값을 W 개 복제본에 써 놓는다**(write-back). 그러면 r1 의 답이 나간 시점에 그 값은 쓰기 quorum 을 이미 가졌고, 이후의 모든 R-quorum 과 교차한다. 다중 writer 에서는 쓰기도 먼저 ts 를 조회해야 한다(아래 `local-ts` 행).

**실측.** 결함: 메시지 지연 1~10틱, 15% 확률 ×12, 2% 유실, 30% 쓰기는 복제본 하나만 빨리 받고 나머지는 ×12(표적 지연 `lag_p`), 300틱에 복제본 2 crash. 클라이언트 4, 키 2, 클라이언트당 12연산, timeout 150틱, seed 0..999. `load 4.36 → 4.15` (시간은 참고만; 위반 수는 결정적이라 두 번 돌려 동일함을 확인. `python3 -I` 는 PYTHONHASHSEED 를 무시해 실행마다 해시가 달라지므로 해시 순서 의존도 함께 걸러진다).

```text
$ sysctl -n machdep.cpu.brand_string hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu hw.perflevel2.physicalcpu
Apple M6
2
4
6
$ python3 --version
Python 3.9.6

config                   R+W        viol/seeds  done%   min    time
R1W1 none                  1+1=2    947/1000    99.8%      2   0.70s
R2W1 none                  2+1=3    316/1000    95.0%      2   0.67s
R1W3 none                  1+3=4    300/1000    67.7%      3   0.70s
R2W2 none                  2+2=4     66/1000    88.0%      3   0.69s
R3W1 none                  3+1=4      0/1000    28.3%      0   0.51s
R2W2 async                 2+2=4     11/1000    87.9%      3   0.80s
R2W2 sync/local-ts         2+2=4    222/1000    86.7%      2   0.64s
R2W2 sync/amnesia          2+2=4     25/1000    95.4%      2   0.93s
R2W2 sync (ABD)            2+2=4      0/1000    77.3%      0   0.77s
N5 R3W3 sync (ABD)         3+3=6      0/1000    92.4%      0   1.16s

같은 설정, 결함 없음 (지연 1..10 만):
R1W1 none nofault          1+1=2     82/1000   100.0%      2   0.70s
R2W2 none nofault          2+2=4      0/1000   100.0%      0   0.70s
R2W2 sync (ABD) nofault    2+2=4      0/1000   100.0%      0   0.85s

대조군: amnesia 만 뺀 R2W2 sync (crash 도 없음):
R2W2 sync no-crash         2+2=4      0/1000    95.4%      0   0.85s

같은 클라이언트가 timeout 뒤 다시 쓸 때의 ts 타이브레이커 (timeout=40):
R2W2 sync tie=client       2+2=4      1/1000    36.9%      4   0.72s
R2W2 sync tie=op           2+2=4      0/1000    36.9%      0   0.71s
```

`viol` = 위반 seed 수, `done%` = 답을 받은 연산 비율(가용성), `min` = 앞쪽 위반 최대 20개를 줄인 최소 반례의 연산 수. 표를 읽는 법:

| 행 | 검사기 결과 | 해석 |
|---|---|---|
| `R1W1`, `R2W1` | 위반 947, 316 | R+W≤N. 완료된 쓰기도 놓친다 (2연산 stale read) |
| `R2W2 none`, `R1W3 none` | 위반 66, 300 | **R+W>N 인데 위반.** 2연산 stale 은 안 나오고 3연산 inversion 만 나온다 |
| `R3W1 none` | 0 | R=N 이면 읽기가 모든 복제본을 보므로 write-back 없이도 위반이 없다. 대가: `done%` 28.3 — crash 하나로 읽기·쓰기(ts 조회)가 다 막힌다 |
| `R2W2 async` | 위반 11 | write-back 을 보내긴 했어도 **기다리지 않으면** 여전히 inversion |
| `sync/local-ts` | 위반 222 | ts 조회 대신 로컬 시계. 시계가 뒤처진 writer 의 쓰기는 ack 를 받고도 복제본의 `ts > 저장된 ts` 에서 버려진다 (DIST-B03 의 "동기화 안 된 시계") |
| `sync/amnesia` | 위반 25, 대조군 0 | 200틱에 복제본 0 이 재부팅하며 데이터를 잃음. ack 후 영속화 안 된 데이터 = quorum 교집합 가정 파괴. 같은 설정에서 amnesia 만 빼면 0 |
| `R2W2 sync (ABD)`, `N5 R3W3 sync` | 0 | 같은 seed 1000개 통과. `done%` 는 timeout 으로 깎임 |

**최소 반례 출력** (`python3 -I demo_sweep.py` 실제 출력에서 발췌):

```text
[R1W1 none] seed=3 key=k0  947개 위반 seed 중 앞 20개를 줄인 최소 반례 (2 ops)
c1  w(8)     |--|
c3  r->None        |--|

[R1W3 none] seed=24 key=k1  300개 위반 seed 중 앞 20개를 줄인 최소 반례 (3 ops)
c3  w(1)     |------------>?
c2  r->1        |--|
c0  r->None           |--|

[R2W2 none] seed=40 key=k1  66개 위반 seed 중 앞 20개를 줄인 최소 반례 (3 ops)
c3  w(4)     |--------------|
c0  r->4        |--|
c2  r->None           |--|

[R2W2 sync tie=client] seed=415 key=k1  1개 위반 seed 중 앞 1개를 줄인 최소 반례 (4 ops)
c2  w(5)     |------------------>?
c2  w(8)        |--|
c0  r->5              |--|
c0  r->8                    |--|
```

R2W2 none 은 위반 seed 40 의 그 키 히스토리 27개 연산이 3개로 줄었다(검사기 `nodes=20`). 해석할 필요도 없는 크기다: 읽기 두 개가 순서대로 일어났는데 값이 거꾸로 간다. 이것이 축소를 하는 이유다.

**결함의 가치.** 같은 코드에서 표적 지연만 끈 결과(`lag_p=0`, 1000 seeds): `R2W2 none` 18건, `R2W2 async` 2건. 켜면 66건, 11건이다. 그리고 `R2W2 async` 의 첫 위반 seed 는 207 이다 — **seed 200개까지는 위반 0** 이었다. 위반 비율이 약 1.1% 면 seed 200개에서 하나도 안 나올 확률이 `0.989^200 ≈ 11%` 다. "우리 테스트는 통과했다" 의 신뢰 구간(SIV-M09 의 0 실패 상한 3/n)을 여기서도 똑같이 계산해야 한다.

### 실행 시간 (`python3 -I demo_runtime.py`, 전체 30초 이내)

선형성 판정은 일반적으로 NP-완전이다 (Gibbons & Korach). 아래는 실측. `load 5.02 → 6.22`, 같은 Mac (Apple M6). 다른 작성 세션과 CPU 를 나눠 쓰므로 비율만 믿는다.

```text
표 1: 최악 입력 (쓰기 n 개 전부 겹침 + 불가능한 읽기 1개)
   n    memo nodes   memo sec    no-memo nodes  no-memo sec
   4            56      0.000              88       0.000
   6           492      0.000            2676       0.002
   8          3600      0.001          149920       0.085
   9          9234      0.004         1349289       0.892
  10         23060      0.009          (skip)           -
  12        135192      0.048          (skip)           -
  14        745500      0.262          (skip)           -
  16       3932192      2.025          (skip)           -

표 2: 키 분할 (키 k 개 × 키당 겹친 쓰기 w 개, 키마다 불가능한 읽기)
   k   w  whole nodes   whole sec   per-key nodes  per-key sec
   2   6       187668       0.535             984       0.0005
   3   5      3248070       4.202             510       0.0003
   2   8       >limit       3.201            7200       0.0022
   3   6       >limit       3.494            1476       0.0006

표 3: 시뮬레이터 히스토리 (R2W2 sync, 클라이언트 4 → 겹침 폭이 작다). 결함 켬 vs 결함 끔(미완료 0개)
 faults  ops/client   ops total  pending   check sec     nodes
     on          12          48       14      0.0001       102  ok=True
     on          50         200       60      0.0006      2244  ok=True
     on         200         800      250      0.0125     32832  ok=True
     on         800        3200     1096      0.1645    551885  ok=True
    off          12          48        0      0.0001        78  ok=True
    off          50         200        0      0.0004       359  ok=True
    off         200         800        0      0.0057      1371  ok=True
    off         800        3200        0      0.0801      5418  ok=True
```

(`>limit` 은 `max_nodes` 400만에서 멈춘 것.) 읽는 법:

- 메모 없음 8→9: 노드 ×9.0, 계승 증가. 메모 있음 8→9: ×2.6, 14→16: ×5.3 (두 칸에 약 ×5.3) — 지수는 그대로지만 밑이 작아졌다.
- 표 2: 전체를 한 모델로 돌리면 (키 3 × 쓰기 5) 3,248,070 노드, 키별로는 510 노드. 키가 늘수록 분할 효과가 곱해진다. 키별 분할이 없으면 실제 시스템의 히스토리는 검사할 수 없다.
- 표 3: 실제 히스토리는 겹침 폭이 클라이언트 수로 묶여서 3200개 연산도 0.16초다. 하지만 **미완료 연산이 비싸다.** 같은 3200 연산에서 결함을 켜면(미완료 1096개) 노드가 551,885, 끄면 5,418로 ×102 이다. 미완료 쓰기는 한 번 생기면 영원히 후보로 남아 모든 지점에서 다시 시도되기 때문이다. 긴 테스트에서 timeout 률을 줄이는 것이 체커 성능의 일부다.

### 펌웨어로 옮기면

| 분산 용어 | 펌웨어·실리콘 대응 |
|---|---|
| 복제본 N | 이중화 ECU, A/B 설정 NOR, 3중화 센서 hub 의 shadow 레지스터 |
| 진행 중 쓰기 | NOR page program 진행 중, CAN/Ethernet 으로 전파 중인 설정 갱신 |
| quorum 읽기 | 2-of-3 voter 가 고른 값 |
| write-back | 읽은 값을 뒤처진 복제본에 다시 써서 수렴시키는 scrub/repair |
| amnesia | ack 뒤 flush 전 전원 차단 (Don 의 SSD/FTL 경험: FUA, 전원 손실 보호 PLP) |
| 어긋난 로컬 시계 | 동기화 안 된 ECU 의 타임스탬프로 LWW 병합 (DIST-B03) |

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I solution.py ; echo "exit=$?"
```

실행 결과 (Part 별 첫 줄과 마지막만):

```text
PASS A empty history
…
PASS B render timeline
PASS C same seed replays the identical history
…
PASS C R2W2 minimal counterexample is 1 write + 2 ordered reads
36/36 passed
exit=0
```

## 7. 자주 하는 실수

**① 같은 시각의 invoke/return 을 return 먼저 정렬한다.**
`w [0,5]`, `r→None [5,6]` 이 위반으로 나온다. 겹친 것인데 실시간 순서로 취급한 것이다. 고침: 정렬 키를 `(t, 0=invoke, 1=return)` 으로.

**② 미완료 쓰기를 무시하거나, 꼭 적용된 것으로 친다.**
timeout 된 `w(1)` 을 지워 버리면 뒤의 `r→1` 이 "쓰인 적 없는 값" 이 되어 거짓 경보가 난다. 반대로 꼭 선형화해야 한다고 하면 `w(1)` 뒤에 `r→None` 만 있는 정상 히스토리를 거부한다. 고침: return 노드를 안 만들고 종료 조건을 완료 연산 수로.

**③ 메모 키에서 상태를 뺀다.**
`S` 가 같아도 쓰기 순서에 따라 현재 값이 다르다. 실제로 키를 `S` 만으로 바꾼 변형은 `w(1)`, `w(2)` 가 겹치고 뒤에 `r→1` 이 오는 히스토리를 `False` 로 답하고 (`brute_force` 는 `True`), CHECK 가 6개 FAIL 한다.

**④ 축소에 `well_formed` 검사를 뺀다.** `practice.py` P5 실제 출력:

```text
     naive: ['r->1']
     guard: ['r->None', 'w(1)']
```

naive 는 "r→1 한 줄" 로 줄었다. 위반은 맞지만 쓰기를 지워서 만든 인공물이고, 진짜 버그인 stale read 는 사라졌다. 고침: `well_formed(cand)` 를 지키는 삭제만 허용.

**⑤ 같은 클라이언트의 다음 연산을 같은 틱에 시작시킨다.** 이 레슨을 쓰면서 실제로 겪었다. 시뮬레이터 `next_op` 가 think time 0 을 허용할 때 `P3` 교차검증이 이렇게 실패했다 (수정 전 실제 출력):

```text
     R2W1 none, key k0, 300 seeds: 선형성 위반 52, 순차 일관성 위반 24
FAIL P3 every SC violation is also a linearizability violation: {43}
overlap Op(client=1, key='k0', kind='w', val=4, t_inv=86, t_ret=106) Op(client=1, key='k0', kind='r', val=3, t_inv=106, t_ret=116)
```

선형성 ⊂ 순차 일관성인데 SC 위반이 선형성 위반의 부분집합이 아니었다. 원인은 체커가 아니라 **기록**이었다. 클라이언트 1 의 다음 연산이 앞 연산의 응답과 같은 틱(106)에 시작해서, "같은 시각 = 겹침" 규칙으로 자기 프로그램 순서를 뒤집을 수 있게 됐다. 고침: `next_op` 를 최소 1틱 뒤로, CHECK `C one client's completed ops never overlap` 추가. 실무의 히스토리 기록에서도 똑같이 벽시계 해상도 때문에 생긴다.

**⑥ 타이브레이커를 클라이언트 id 로 만든다.** 위 표의 `tie=client`: timeout 후 같은 클라이언트가 같은 counter 로 다른 값을 쓰면 복제본들이 `(ts 동일, 값 다름)` 으로 갈라져 읽기 값이 뒤집힌다. 1000 seed 에 1건이지만, `tie=op` 는 0건. 고침: 타이브레이커에 연산마다 유일한 값 `(client, 클라이언트별 seq)`. Jepsen 도 응답 불명 후에는 프로세스 id 를 새로 쓰는 것으로 알고 있다 (hedge).

**⑦ 타이브레이커에 전역으로 증가하는 번호를 쓴다.** 이것도 작성 중에 실제로 겪었다. 처음에는 ts 를 `(counter, 전역 op 번호)` 로 만들었더니, 일부러 망가뜨린 변형(쓰기가 counter 를 안 올림)이 **36/36 통과**했다 (`no counter bump exit=0 fails=0`). 전역 번호가 숨은 시퀀서가 되어 "나중에 시작한 쓰기가 항상 이김" 을 공짜로 준 것이다. 실제 시스템에는 그런 번호가 없다. `(client, seq)` 로 바꾼 뒤 같은 변형은 4개 FAIL (`C ABD N=3 passes the same seeds: [0, 1, 2, 3, 4]` 등). 검사기를 믿으려면 **버그를 심어서 잡히는지** 먼저 본다 (mutation check).

**⑧ 위반 0건을 정답으로 읽는다.** `R2W2 async` 는 seed 200개까지 0건, 1000개에서 11건이다. 통과는 "이 시나리오에서 못 찾았다" 일 뿐이다. ABD 는 증명이 따로 있어서 믿는 것이고, 검사기는 구현이 증명을 따랐다는 증거를 더해 줄 뿐이다.

## 8. 면접 Q&A

<details><summary>Q1. 선형성, 순차 일관성, 직렬성의 차이를 예로 설명해 보세요.</summary>

선형성은 실시간 순서를 존중하는 단일 객체 수준의 조건이다. 순차 일관성은 클라이언트별 순서만 지키면 된다. `w(1)` 이 끝난 뒤 다른 클라이언트의 `r→None` 은 SC 로는 허용, 선형성으로는 위반이다 (`practice.py` P3). 직렬성은 트랜잭션 묶음이 어떤 순차 순서와 같으면 되고 실시간을 안 본다. 꼬리질문: 왜 선형성은 키별로 나눠 검사해도 되고 직렬성은 안 되나 → 선형성은 국소적(local)이다. 객체별로 선형화 가능하면 전체도 가능하다. 다중 키 트랜잭션의 원자성은 객체별 검사로 드러나지 않는다.
</details>

<details><summary>Q2. R+W>N 이면 선형화 가능한가요?</summary>

아니다. 교집합은 완료된 쓰기만 보장한다. 진행 중인 쓰기를 읽은 r1 뒤에 r2 가 옛 값을 돌려줄 수 있고(new-old inversion), 이번 레슨의 `R2W2 none` 이 1000 seed 중 66개에서 그걸 찾았다. 읽기가 W 개에 write-back 하고 ack 를 기다리면(ABD) 같은 seed 에서 0건. 꼬리질문: write-back 비용을 줄이려면? → R 개 응답의 ts 가 전부 같고 R ≥ W 면 그 값이 이미 쓰기 quorum 에 있어 생략 가능. `practice.py` P4 에서 연산당 메시지 5.81 → 4.76, 위반 0. 꼬리질문: R=N 이면? → 이 시뮬레이션에서는 write-back 없이 0건이지만, `done%` 28.3 — 복제본 하나 죽으면 읽기가 멈춘다.
</details>

<details><summary>Q3. Wing-Gong 알고리즘을 설명하고, Lowe 의 메모 키가 왜 (집합, 상태) 인지 말해 보세요.</summary>

이벤트를 시간순 연결 리스트로 두고, 첫 return 앞의 call 중 하나를 골라 적용 가능하면 리스트에서 빼고 재귀한다. return 을 만나면 그 연산을 못 고른 것이니 되돌린다. 미래는 어떤 연산이 끝났는지와 현재 상태만의 함수라서 같은 `(S, s)` 는 재탐색이 필요 없다. 꼬리질문: 상태를 빼면? → 쓰기 순서에 따라 현재 값이 달라서 탐색이 틀린다. 정상 히스토리를 거부한다. 꼬리질문: 집합만 쓰면 왜 안 되는가의 반례는? → `w(1)`, `w(2)` 가 겹치고 뒤에 `r→1`: `w(1) w(2)` 순서로 먼저 실패해 집합 {w1,w2} 를 캐시하면, 답이 되는 `w(2) w(1)` 순서가 잘린다.
</details>

<details><summary>Q4. 복잡도가 지수인데 실제로 어떻게 쓰나요?</summary>

일반 문제는 NP-완전이다 (Gibbons-Korach). 이번 측정에서 쓰기 16개 전부 겹침 + 불가능한 읽기는 메모로 393만 노드, 메모 없이는 9개부터 135만 노드(계승 증가). 그런데 실제 히스토리는 겹침 폭이 동시 클라이언트 수로 제한된다: 3200 연산 0.16초. 실전 전략은 ① 키별 분할 ② 클라이언트 수를 낮게 유지 ③ timeout(미완료) 줄이기 ④ 쓰기 값 유일화 ⑤ 시간 제한을 두고 "모름" 을 허용. 꼬리질문: 미완료가 왜 특히 비싼가? → 한 번 생기면 종료까지 후보로 남아, 같은 3200 연산에서 노드가 5,418 → 551,885 (×102).
</details>

<details><summary>Q5. 클라이언트가 timeout 난 쓰기는 어떻게 기록하고, 그 뒤 클라이언트는 어떻게 해야 하나요?</summary>

"완료"도 "실패"도 아닌 불명으로 기록한다. 판정에서 호출 이후 어느 시점에 적용됐을 수도, 아예 안 됐을 수도 있다. 쓰기는 서버에 도달했을 수 있으니 재시도해도 새 값으로 다뤄야 하고, 타임스탬프 타이브레이커는 연산마다 유일해야 한다(`tie=client` 에서 실제로 깨졌다). 꼬리질문: Jepsen 에서는? → timeout 난 프로세스는 새 번호로 갈아 끼우는 것으로 알고 있다(내 기억, 이 환경에 Jepsen 없음).
</details>

<details><summary>Q6. 검사기가 위반 0 을 보고했습니다. 무엇을 알게 된 건가요?</summary>

이 결함 조합과 이 seed 들에서 못 찾았다는 것뿐이다. `R2W2 async` 의 위반율이 약 1.1% 인데 200 seed 에서 0건일 확률이 11%다. 신뢰를 올리려면 seed 수 늘리기보다 **표적 결함**이 효율적이다: `lag_p` 를 켜니 `R2W2 none` 이 18→66건. 꼬리질문: 그럼 ABD 는 왜 믿나? → 쿼럼 교집합과 ts 단조성을 이용한 증명이 있기 때문이고, 검사기는 **구현이** 증명의 가정을 지켰는지를 본다. 예로 `amnesia` 는 그 가정(ack 한 데이터는 남는다)을 깨서 25건이 나왔다.
</details>

<details><summary>Q7. 타이밍에 의존하는 버그를 어떻게 재현 가능하게 만듭니까? (8층 체크)</summary>

입력의 비결정성을 전부 seed 로 몰아넣는다. 가상 시계 + 이벤트 큐, 메시지 지연·유실·crash 를 한 PRNG 에서 뽑고, 벽시계·스레드·해시 순서에 의존하지 않는다. 그러면 위반 seed 하나가 곧 재현 절차고, 축소한 3연산 반례가 버그 리포트가 된다. 이 레슨의 CHECK `C same seed replays the identical history`, 그리고 TEST-A05(가상 시간 + 이벤트 trace hash)가 같은 구조다. 꼬리질문: 시뮬레이터의 한계는? → 모델링한 결함만 찾는다. 실제 스레드 스트레스(SIV-M09)는 모델이 놓친 것을 찾지만 재현이 안 된다. 둘 다 필요하다.
</details>

<details><summary>Q8. 이 도구를 펌웨어 쪽 어디에 쓰겠습니까?</summary>

① 락프리/lock-less 큐의 멀티코어 테스트: 각 코어가 호출·응답 시각과 값을 ring buffer 에 남기고 host 에서 판정. DS-A03 의 강제 스케줄을 FIFO 모델로 넣으면 "완료된 enqueue 뒤의 empty" 를 위반으로 잡는다 (`practice.py` P1). ② 이중화 ECU 설정/DTC 저장소의 모델 체크: 시뮬레이터로 CAN 지연·재시작을 주입. ③ NVMe 쓰기 경로: ack 뒤 전원 손실 시나리오로 영속성 순서를 판정 (`amnesia` 행). 꼬리질문: MCU 의 타임스탬프가 코어 간에 안 맞으면? → 실시간 순서를 잘못 잡아 거짓 경보가 날 수 있다. 공통 카운터(DWT 대신 공용 타이머)를 쓰거나, 큰 쪽으로 겹치도록 구간을 넓혀(불확실성을 겹침으로 처리) 안전한 쪽으로 판정한다.
</details>

## 9. 추가 연습

**P1. FIFO 큐 명세로 바꿔 DS-A03 의 "빈 큐" 답을 판정하라.** `kind="e"/"d"`, 상태는 튜플.
<details><summary>답</summary>

`QueueModel`. enqueue 는 뒤에 붙이고, `val=None` 인 dequeue 는 상태가 비었을 때만 허용. A 가 슬롯만 잡고 멈춘 동안(`[0,20]`) B 의 `enq(201)` 이 완료되고 소비자가 `empty` 를 받으면 위반이다. 어떤 순서로도 `enq(201)` 이 `empty` 뒤에 올 수 없다. DS-A03 이 말로 한 "선형화 불가" 를 기계가 확인한다.
</details>

**P2. CAS 레지스터 명세 — 미완료 CAS 를 어떻게 다루나.**
<details><summary>답</summary>

`CasModel`. 미완료 CAS 는 "적용됐다면 성공" 으로만 본다(값이 다르면 그 시점에 적용될 수 없다). 동시에 두 CAS 가 둘 다 성공을 답하면 위반. 축소의 "읽기만 지워도 안전" 정리는 CAS 에 그대로 적용되지 않으니 조심한다.
</details>

**P3. 순차 일관성 검사기를 써서 선형성과 갈리는 예를 보여라.**
<details><summary>답</summary>

`check_sc`. 클라이언트별 순서만 지키고 실시간을 안 본다. 미완료 쓰기는 혼자인 가상 클라이언트로 넣어 건너뛸 수 있게 한다. 다른 클라이언트의 stale read 는 SC 통과·선형성 위반, 같은 클라이언트면 둘 다 위반. 실측 (`R2W1 none`, key k0, 300 seeds): 선형성 위반 60, SC 위반 30. SC 위반 seed ⊂ 선형성 위반 seed 를 집합 포함으로 확인했다(실수 ⑤ 를 찾아낸 CHECK).
</details>

**P4. write-back fast path: R 개 응답의 ts 가 같으면 생략. 조건은?**
<details><summary>답</summary>

`FastPath`. 조건은 **`R >= W`**. R 개가 모두 같은 ts 를 들고 있으면 그 값은 R 개에 있고, `R>=W` 일 때만 W 개가 쓰기 quorum 임이 보장된다. 실측: ABD 위반 0, 연산당 메시지 5.81 → 4.76. `R1W3` 에서 이 조건을 빼면 위반 300건, 넣으면 0건.
</details>

**P5. 축소에서 `well_formed` 를 빼면 무엇이 나오나.**
<details><summary>답</summary>

`naive_shrink`. 앞 §7 ④ 의 출력처럼 쓰지도 않은 값을 읽는 1연산으로 끝난다. 줄이는 방향이 원래 버그에서 멀어지는 대표적 인공물이다.
</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I practice.py ; echo "exit=$?"
```

실행 결과 (일부):

```text
PASS P1 DS-A03 forced schedule is not linearizable
…
     R2W1 none, key k0, 300 seeds: 선형성 위반 60, 순차 일관성 위반 30
PASS P3 every SC violation is also a linearizability violation
…
     messages/op: always write-back 5.81, fast path 4.76
PASS P4 fast path sends fewer messages
     R1W3 1000 seeds: no R>=W guard 300 violations, with guard 0
…
     naive: ['r->1']
     guard: ['r->None', 'w(1)']
PASS P5 guarded shrink keeps the real stale read
24/24 passed
exit=0
```

데모 실행: `python3 -I demo_sweep.py` (설정 표와 최소 반례, 시드 개수를 인자로 줄 수 있음), `python3 -I demo_runtime.py` (실행 시간 표 3개).

## 10. 핵심 정리

- 선형성 = 호출~응답 사이의 한 순간에 일어난 것으로 설명되는 단일 순서가 존재한다. 값이 안 깨져도 순서가 불가능하면 위반이다.
- Wing-Gong: 첫 return 앞의 call 을 골라 적용, return 에 부딪히면 되돌린다. 메모 키는 `(선형화 집합, 상태)`.
- 미완료 연산: return 노드를 안 만든다. 적용/미적용 둘 다 허용. 비용이 크다 (같은 3200 연산에서 노드 ×102).
- 선형성은 국소적이다 → 키별로 나눠라. 쓰기 값은 유일하게.
- 축소: 읽기와 아무도 안 읽은 쓰기만 지운다. 그래야 축소 결과가 원래 위반의 증거다.
- `R+W>N` 은 완료된 쓰기만 지켜 준다. inversion 을 막으려면 읽기가 W 개에 write-back 하고 **ack 를 기다린다**(ABD). 실측: R2W2 위반 66/1000 → 0/1000.
- 위반 0 은 증명이 아니다. seed 수와 표적 결함으로 검출률을 따로 말한다.

## 참고자료

서지 정보와 도구 동작은 작성 시 기억에 의존했고, 이 세션에서 웹으로 확인하지 않았다. 코드는 이 레슨에서 직접 돌린 것만 인용했다.

- M. Herlihy, J. Wing, "Linearizability: A Correctness Condition for Concurrent Objects", ACM TOPLAS, 1990 — 정의와 locality.
- J. Wing, C. Gong, "Testing and Verifying Concurrent Objects", JPDC, 1993 — 백트래킹 검사 (이 레슨의 Part A 골격).
- G. Lowe, "Testing for Linearizability", Concurrency and Computation: Practice and Experience, 2017 — `(집합, 상태)` 메모와 분할 (이 레슨은 그 아이디어만 사용).
- P. Gibbons, E. Korach, "Testing Shared Memories", SIAM J. Computing, 1997 — 판정 문제의 NP-완전성.
- H. Attiya, A. Bar-Noy, D. Dolev, "Sharing Memory Robustly in Message-Passing Systems", JACM, 1995 — ABD. 다중 writer 변형은 이후 연구들.
- Jepsen / Knossos (Clojure 기반 선형성 검사기), Porcupine (Go 구현, 빠른 것으로 알려짐) — 이 레슨에서는 실행하지 않았다. 도구 비교가 필요하면 직접 확인할 것.
- 이 트랙: DIST-B03 (vector clock), DS-A03 (Vyukov 큐의 비선형 "빈 큐"), SIV-M09 (0 실패의 통계), TEST-A05 (결정론적 시뮬레이션).
