---
id: DIST-B03
time: 60분
updated: 2026-11-08
---
# DIST-B03 Vector Clock과 동시성 판정

## 학습 목표

- 벡터 클럭(vector clock)의 **tick / merge / compare** 를 구현하고, 두 이벤트가 인과 관계인지 동시(concurrent)인지 판정한다.
- Lamport 클럭이 지키는 방향(`a→b` 이면 `L(a)<L(b)`)과 못 지키는 방향을 구분하고, **같은 숫자 쌍인데 답이 다른 반례**로 증명한다.
- 시계가 동기화되지 않은 ECU 3개의 로그를 **인과 순서를 지키며 병합**하고 동시 이벤트를 표시한다.
- 벽시계 정렬과 Lamport last-write-wins 가 만드는 펌웨어 버그를 일부러 재현하고, 같은 도구로 수정 효과를 확인한다.
- 벡터 클럭이 TSan 같은 data race 검출기의 뿌리라는 것을 설명한다 (TOOL-M09 연결).

## 1. 문제

차량 사고 로그를 분석하는데 "브레이크 명령을 받은 시각"이 "브레이크 명령을 보낸 시각"보다 앞서 찍혀 있다면, 펌웨어 검증 엔지니어는 어떤 로그도 믿을 수 없게 된다. ECU마다 수정 발진기가 따로 돌고, CAN에는 공통 시계가 없기 때문에 실제로 자주 생기는 일이다. 면접에서는 "시계 동기화 없이 순서를 어떻게 정하나"로 나온다.

동시성 가이드 1~7층에서 다룬 레이스의 정의를 다시 보자. TSan의 data race는 "동기화 없이 일어난, 적어도 하나가 쓰기인 **동시** 접근"이다 (TOOL-M09). 여기서 **동시** 는 "같은 시각"이 아니라 **어느 쪽도 다른 쪽에 영향을 줄 수 없었다** 는 뜻이다. 이번 레슨은 그 관계를 계산하는 도구를 직접 만든다. 스레드 대신 ECU, 락 대신 메시지일 뿐 구조는 같다. (DIST-B02 Lamport 레슨은 아직 없어서 Lamport 규칙까지 이 레슨에 포함했다.)

**happened-before** (`→`, Lamport 1978)의 정의는 셋뿐이다.

1. 같은 ECU 안에서 먼저 일어난 이벤트 → 나중 이벤트
2. 메시지 송신 → 그 메시지의 수신
3. 전이: `a→b` 이고 `b→c` 면 `a→c`

어느 방향으로도 `→` 가 없으면 **동시** 다 (`a∥b`). 아래가 이 레슨에서 쓰는 시나리오다. 벡터는 `[BMS, VCU, GW]` 순서다.

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="distb03a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0L10 5L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">3-ECU 시공간도: 벡터는 [BMS, VCU, GW]</text>
  <g fill="currentColor"><text x="10" y="74">BMS</text><text x="10" y="154">VCU</text><text x="10" y="234">GW</text></g>
  <g stroke="currentColor"><line x1="60" y1="70" x2="720" y2="70"/><line x1="60" y1="150" x2="720" y2="150"/><line x1="60" y1="230" x2="720" y2="230"/></g>
  <g stroke="currentColor" fill="none" marker-end="url(#distb03a)">
    <line x1="220" y1="70" x2="294" y2="144"/><line x1="400" y1="150" x2="474" y2="224"/><line x1="560" y1="230" x2="617" y2="78"/>
  </g>
  <g fill="currentColor">
    <circle cx="120" cy="70" r="6"/><circle cx="220" cy="70" r="6"/><circle cx="620" cy="70" r="6"/>
    <circle cx="160" cy="150" r="6"/><circle cx="300" cy="150" r="6"/><circle cx="400" cy="150" r="6"/>
    <circle cx="200" cy="230" r="6"/><circle cx="480" cy="230" r="6"/><circle cx="560" cy="230" r="6"/>
  </g>
  <circle cx="360" cy="70" r="8" class="f-acc"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="120" y="56">b1 [1,0,0]</text><text x="220" y="56">b2 [2,0,0]</text><text x="360" y="56">b3 [3,0,0]</text><text x="620" y="56">b4 [4,3,3]</text>
    <text x="160" y="136">v1 [0,1,0]</text><text x="300" y="170">v2 [2,2,0]</text><text x="400" y="136">v3 [2,3,0]</text>
    <text x="200" y="250">g1 [0,0,1]</text><text x="480" y="250">g2 [2,3,2]</text><text x="560" y="266">g3 [2,3,3]</text>
  </g>
  <text x="10" y="284" fill="currentColor">화살표 = 메시지. 강조한 b3 는 v1 v2 v3 g1 g2 g3 전부와 동시다 (b3 를 아는 메시지가 없기 때문).</text>
</svg>
```

벡터 클럭 규칙은 짧다. ECU `i` 는 정수 N개짜리 배열 `V` 를 가진다.

| 이벤트 | 벡터 클럭 | Lamport 클럭 |
|---|---|---|
| 내부 이벤트 | `V[i]++` | `L++` |
| 송신 | `V[i]++` 후 `V` 를 프레임에 싣는다 | `L++` 후 `L` 을 싣는다 |
| 수신 (프레임 `Vm`, `Lm`) | `V = max(V, Vm)` 칸별, 그다음 `V[i]++` | `L = max(L, Lm) + 1` |

비교는 칸별로 한다. 작은 칸과 큰 칸이 둘 다 있으면 동시다.

```svg
<svg viewBox="0 0 720 170" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">vc_compare: 칸별로 보고, 방향이 섞이면 동시</text>
  <g stroke="currentColor" fill="none">
    <rect x="130" y="40" width="50" height="28"/><rect x="180" y="40" width="50" height="28"/><rect x="230" y="40" width="50" height="28"/>
    <rect x="130" y="68" width="50" height="28"/><rect x="180" y="68" width="50" height="28"/><rect x="230" y="68" width="50" height="28"/>
    <rect x="130" y="110" width="50" height="28"/><rect x="180" y="110" width="50" height="28"/><rect x="230" y="110" width="50" height="28"/>
    <rect x="130" y="138" width="50" height="28"/><rect x="180" y="138" width="50" height="28"/><rect x="230" y="138" width="50" height="28"/>
  </g>
  <rect x="180" y="40" width="50" height="56" class="f-ok"/>
  <rect x="130" y="110" width="50" height="56" class="f-warn"/>
  <rect x="230" y="110" width="50" height="56" class="f-acc"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="155" y="59">1</text><text x="205" y="59">2</text><text x="255" y="59">3</text>
    <text x="155" y="87">1</text><text x="205" y="87">3</text><text x="255" y="87">3</text>
    <text x="155" y="129">1</text><text x="205" y="129">2</text><text x="255" y="129">3</text>
    <text x="155" y="157">2</text><text x="205" y="157">2</text><text x="255" y="157">2</text>
  </g>
  <g fill="currentColor">
    <text x="20" y="59">a = </text><text x="20" y="87">b = </text><text x="20" y="129">a = </text><text x="20" y="157">c = </text>
    <text x="300" y="72">a 의 칸이 모두 ≤ 이고 하나는 &lt; 이다 → a BEFORE b</text>
    <text x="300" y="134">첫 칸은 a &lt; c, 셋째 칸은 a &gt; c → a CONCURRENT c</text>
  </g>
</svg>
```

### Part A — 벡터 클럭과 Lamport 규칙

`vc_tick`, `vc_merge`, `vc_compare` 와 ECU의 `on_local`, `on_send`, `on_recv` 를 구현한다. 비교 결과는 `VC_EQUAL / VC_BEFORE / VC_AFTER / VC_CONCURRENT` 넷 중 하나다. `0xFFFFFFFF` 같은 값에서도 맞아야 한다 (뺄셈 금지).

### Part B — 3개 ECU 로그 병합과 Lamport 반례

- `stamp_logs` : ECU별 로그를 재생해 각 이벤트의 벡터와 Lamport 값을 채운다. RECV는 짝이 되는 SEND가 먼저 재생될 때까지 기다린다. 잘못된 로그(짝 없는 RECV, 순환 대기, 범위 밖 번호, 중복 SEND)는 멈추지 말고 `-1`.
- `merge_logs` : 모든 이벤트를 **인과 순서를 지키는 하나의 순서**로 정렬한다.
- `mark_concurrent` : 각 이벤트가 몇 개의 이벤트와 동시인지 표시한다.
- `find_lamport_counterexample` : `L(a)==L(c)`, `L(b)==L(d)`, `L(a)<L(b)` 인데 `a→b` 이고 `c∥d` 인 두 쌍을 찾는다. Lamport 숫자가 똑같은데 답이 다르면, 숫자만 보는 어떤 규칙으로도 동시성을 가를 수 없다.

하네스는 VC와 무관하게 만든 **진짜 인과 그래프**(프로그램 순서 + 메시지 간선의 전이 폐포)와 300개 seed의 모든 이벤트 쌍을 비교한다.

### Part C — 펌웨어 버그 재현과 수정

두 버그를 일부러 만들어 둔 코드가 하네스에 있다.

1. **벽시계 정렬**: VCU 시계가 약 20 ms 느리고 GW 시계가 약 90 ms 빠르다. 벽시계로 정렬한 로그는 결과가 원인보다 앞선다. `count_causal_inversions` (검출 도구)를 구현해 뒤집힌 쌍을 센다.
2. **Lamport LWW 설정 복제**: BMS의 50 A 감량과 VCU의 120 A 설정이 동시에 일어났다. VCU는 바빠서 Lamport 값이 크고, 게이트웨이는 안전 한도를 **조용히** 버린다. `gw_apply_vc` 로 동시 쓰기를 형제(sibling)로 보관하고, `gw_effective_limit` 은 그중 작은 값을 낸다.

## 2. Problem (English)

Three ECUs exchange messages over CAN/Ethernet and have no synchronized clock.

```c
void vc_tick(vclock_t *v, int me);
void vc_merge(vclock_t *into, const vclock_t *other);
vc_order_t vc_compare(const vclock_t *a, const vclock_t *b);   /* EQUAL, BEFORE, AFTER, CONCURRENT */

void on_local(ecu_t *e);
stamp_t on_send(ecu_t *e);
void on_recv(ecu_t *e, const stamp_t *m);

int  stamp_logs(ecu_log_t logs[N_ECU]);                 /* 0, or -1 on a malformed log */
int  merge_logs(ecu_log_t logs[N_ECU], event_t *out[], int cap);
void mark_concurrent(event_t *const ev[], int n);
bool find_lamport_counterexample(event_t *const ev[], int n, int out[4]);
int  count_causal_inversions(event_t *const order[], int n);
int  gw_apply_vc(gw_store_t *s, const cfg_write_t *w);  /* 1 stored, 0 stale, -1 full */
int32_t gw_effective_limit(const gw_store_t *s);
```

Constraints: N is fixed at 3; no dynamic allocation; a malformed log must return -1 instead of hanging; `merge_logs` must output a total order that is a linear extension of happened-before, breaking ties deterministically.

Example: `a=[1,2,3], b=[1,3,3]` gives `VC_BEFORE`. `a=[1,2,3], c=[2,2,2]` gives `VC_CONCURRENT`. After `b2 [2,0,0]` is received by a VCU at `[0,1,0]`, the VCU becomes `[2,2,0]`. Follow-ups: why does sorting with `vc_compare` as the `qsort` comparator break? What happens when an ECU reboots and its counter returns to zero?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o st && ./st
```

스레드는 쓰지 않지만 이 묶음의 공통 빌드 옵션을 그대로 뒀다. 고칠 곳은 `TODO` 14군데다. 시작 코드를 실제로 돌리면 FAIL 이 나고 exit 1 로 끝난다.

```text
9/63 passed
```

분모가 67이 아니라 63인 것은 반례를 찾은 뒤에만 도는 CHECK 4개가 건너뛰어지기 때문이다.

## 4. 힌트

<details><summary>힌트 1 — 비교에 필요한 것은 불리언 두 개</summary>

"a가 작은 칸이 있었나", "a가 큰 칸이 있었나" 두 가지만 기록하면 네 경우가 다 갈린다. 뺄셈의 부호를 쓰지 말자. 부호 없는 값의 뺄셈은 감쌈이 일어난다.
</details>

<details><summary>힌트 2 — 수신의 순서</summary>

합치고 나서 증가한다. 증가를 빼먹으면 "수신 이벤트"가 "아무것도 새로 안 배운 이벤트"로 처리돼 앞 이벤트와 벡터가 같아진다.
</details>

<details><summary>힌트 3 — stamp_logs 의 종료 조건</summary>

바깥 루프를 한 바퀴 돌았는데 어느 ECU도 전진하지 못했고 아직 남은 이벤트가 있다면, 그 뒤로도 영원히 전진할 수 없다. 그때 -1을 돌린다. 이것이 멈춤 없는 종료의 보장이다.
</details>

<details><summary>힌트 4 — 정렬 키</summary>

`a→b` 이면 `V(a)` 의 모든 칸이 `V(b)` 이하이고 적어도 하나는 작다. 그러면 칸의 합은 어떻게 되나? 합은 숫자이므로 전순서를 만든다.
</details>

<details><summary>힌트 5 — gw_apply_vc</summary>

두 단계로 나눈다. 먼저 "이 쓰기가 이미 지배당했나"를 전부 확인하고 그 경우 아무것도 바꾸지 않는다. 다음에 지배되는 형제를 지우고 추가한다. 기존 형제끼리는 항상 서로 동시이므로, 어떤 형제를 지배하면서 다른 형제에게 지배당하는 쓰기는 나올 수 없다. 그래도 나눠 두면 읽기 쉽고, 저장소가 꽉 찼을 때 상태를 그대로 두기도 쉽다.
</details>

## 5. 풀이

### Part A — 왜 벡터가 인과를 정확히 담는가

`V(e)[i]` 는 **e의 인과 과거(causal history)에 들어 있는 ECU i의 이벤트 수**다. 수신에서 칸별 max를 취하는 이유가 여기 있다. 두 과거의 합집합을 표현하는 연산이기 때문이다.

그리고 각 ECU의 이벤트는 한 줄로 서 있으므로, 인과 과거를 ECU별로 자르면 언제나 **앞부분 전체(prefix)** 다. 앞부분은 길이 하나로 표현된다. 따라서

```text
a → b  ⇔  history(a) ⊂ history(b)  ⇔  V(a) < V(b) (모든 칸 ≤, 하나는 <)
```

수학으로 말하면 벡터는 "인과 과거들의 포함 관계"를 `(ℕ^N, ≤)` 의 곱 순서로 옮기는 순서 임베딩이다. 동시는 두 벡터가 비교 불가능(incomparable)인 경우와 정확히 같다.

Lamport는 이 N차원 정보를 정수 하나로 눌러 담는다. 한쪽 방향(`a→b ⇒ L(a)<L(b)`)은 살아남지만 역방향 정보는 사라진다. 합을 취하는 것과 비슷한 사영(projection)이라 비교 불가능한 쌍도 앞뒤가 생기는 것이다.

### Part B — 재생, 병합, 표시

**재생.** `stamp_logs` 는 ECU별로 갈 수 있는 데까지 전진하고, RECV에서 짝 SEND가 아직이면 그 ECU를 멈춘다. 전 ECU가 한 바퀴 동안 못 나아가면 로그가 잘못된 것이다. 이벤트를 하나씩은 반드시 소비하므로 반복 횟수는 이벤트 수로 제한된다.

**병합.** 정렬 키는 `(벡터 합, node, seq)` 다. `a→b` 면 합이 반드시 늘어나므로(모든 칸 ≤, 한 칸 <) 이 키의 오름차순은 인과 순서를 어기지 않는 전순서다. 합은 칸이 `uint32_t` 라서 넘칠 수 있으니 `uint64_t` 로 더한다.

**결과.** 시작 코드의 시나리오를 정답 코드로 돌린 실제 출력이다.

```text
  event          vector        L   wall  concurrent
  b1 overtemp    [1,0,0]       1   1000  || x2
  v1 pedal       [0,1,0]       1    985  || x4
  g1 diag        [0,0,1]       1   1100  || x6
  b2 tx_derate   [2,0,0]       2   1002  || x2
  b3 fan_on      [3,0,0]       3   1010  || x6
  v2 rx_derate   [2,2,0]       3    987  || x2
  v3 tx_torque   [2,3,0]       4    990  || x2
  g2 rx_torque   [2,3,2]       5   1103  || x1
  g3 tx_ack      [2,3,3]       6   1106  || x1
  b4 rx_ack      [4,3,3]       7   1020  
  counterexample: (b1 overtemp L=1) -> (b2 tx_derate L=2)  vs  (v1 pedal L=1) || (b2 tx_derate L=2)
```

읽는 법: `b4` 만 표시가 없다. `b4` 의 벡터 `[4,3,3]` 은 다른 모든 이벤트 이상이기 때문이다. 메시지를 많이 주고받은 이벤트일수록 동시 대상이 적다. `b3` 는 아무에게도 알려지지 않아 VCU·GW의 여섯 이벤트와 전부 동시다. 표의 동시 숫자를 모두 더하면 26이고 동시 쌍은 그 절반인 13개다.

**반례.** `(b1, b2)` 의 Lamport 값은 `(1, 2)` 이고 실제로 `b1→b2` 다. `(v1, b2)` 의 Lamport 값도 `(1, 2)` 인데 실제로는 `v1∥b2` 다. 입력 숫자가 완전히 같은데 정답이 다르다. 그러므로 **`L(a)`, `L(b)` 만 입력으로 받는 어떤 함수도** 동시성을 판정할 수 없다. "Lamport 숫자가 더 작다"는 말에서 끌어낼 수 있는 것은 "b가 a보다 먼저일 수는 없다"가 전부다.

**전수 검사 결과** (seed 300개, 무작위 3-ECU 실행, 실제 출력):

```text
  property: seeds=300 pairs=173102 vc_wrong=0 lamport_mono_bad=0 concurrent=86700 concurrent_but_L_smaller=38131 counterexample_seeds=300
```

| 값 | 뜻 |
|---|---|
| `vc_wrong=0` | 벡터 판정이 진짜 인과 그래프와 173,102쌍 전부에서 일치 |
| `lamport_mono_bad=0` | `a→b ⇒ L(a)<L(b)` 는 항상 성립 |
| `concurrent=86700`, `concurrent_but_L_smaller=38131` | 순서쌍 기준이라 동시 쌍은 실제로 43,350개다. 그중 38,131개(88%)는 Lamport 값이 달라서 앞뒤가 있는 것처럼 보이고, 나머지 5,219개만 값이 같다 |
| `counterexample_seeds=300` | 모든 실행에서 반례가 나옴. 희귀한 경우가 아니다 |

**qsort 함정 데모.** `vc_compare` 를 비교 함수로 바로 쓰고 동시를 0으로 돌려주면 안 된다. `0` 은 "같다"인데 `a∥b` 이고 `b∥c` 라고 `a∥c` 가 되지 않는다. C11 7.22.5는 비교 함수의 결과가 배열 위의 전순서여야 한다고 요구하므로, 이 비교 함수를 넘기는 것부터 규칙 위반이다.

<!-- code: demo_bad_sort.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bad_sort.c -o d_bad && ./d_bad
```

이 맥의 `qsort` 실제 출력이다.

```text
3개 이벤트, 입력 순서 6가지 (a->c, b 는 동시):
  in: a[1,0,0] b[0,1,0] c[2,0,0]  -> out: a[1,0,0] b[0,1,0] c[2,0,0]  inversions=0
  in: a[1,0,0] c[2,0,0] b[0,1,0]  -> out: a[1,0,0] c[2,0,0] b[0,1,0]  inversions=0
  in: b[0,1,0] a[1,0,0] c[2,0,0]  -> out: b[0,1,0] a[1,0,0] c[2,0,0]  inversions=0
  in: b[0,1,0] c[2,0,0] a[1,0,0]  -> out: b[0,1,0] a[1,0,0] c[2,0,0]  inversions=0
  in: c[2,0,0] a[1,0,0] b[0,1,0]  -> out: a[1,0,0] c[2,0,0] b[0,1,0]  inversions=0
  in: c[2,0,0] b[0,1,0] a[1,0,0]  -> out: c[2,0,0] b[0,1,0] a[1,0,0]  inversions=1
무작위 실행 300개: vc_compare 비교함수 inversions=7616 (깨진 실행 300개), merge_cmp inversions=0
```

마지막 입력 줄이 핵심이다. `c` 와 `b` 는 "같다", `b` 와 `a` 도 "같다"이므로 `a→c` 인데도 `c` 가 앞에 남는다. 무작위 실행에서는 300개 모두 깨졌다.

### Part C — 재현, 검출, 수정

버그 1은 그림으로 보면 바로 이해된다.

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="distb03b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0L10 5L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">벽시계로 정렬하면 결과가 원인보다 앞에 놓인다</text>
  <line x1="60" y1="110" x2="710" y2="110" stroke="currentColor"/>
  <text x="10" y="114" fill="currentColor">wall</text>
  <g fill="currentColor">
    <circle cx="174" cy="110" r="5"/><circle cx="268" cy="110" r="5"/><circle cx="455" cy="110" r="5"/><circle cx="642" cy="110" r="5"/>
  </g>
  <circle cx="211" cy="110" r="7" class="f-warn"/>
  <circle cx="492" cy="110" r="7" class="f-acc"/>
  <path d="M492 102 Q 350 20 217 103" fill="none" stroke="currentColor" marker-end="url(#distb03b)"/>
  <text x="350" y="46" text-anchor="middle" fill="currentColor">인과: b2 → v2 (메시지)</text>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="174" y="130">v1 985</text><text x="211" y="147">v2 987</text><text x="268" y="130">v3 990</text>
    <text x="455" y="130">b1 1000</text><text x="492" y="147">b2 1002</text><text x="642" y="130">b3 1010</text>
  </g>
  <text x="10" y="185" fill="currentColor">VCU 시계가 약 20 ms 느려서 수신(v2)이 송신(b2)보다 작은 값을 받는다. 시계 숫자는 ECU 안에서만 의미 있다.</text>
</svg>
```

`count_causal_inversions` 는 정렬 결과에서 앞에 놓인 이벤트가 뒤의 이벤트보다 인과적으로 **나중인** 쌍을 센다. 정답 코드의 실제 출력이다.

```text
  causal inversions: wall-clock sort=7, vector-clock merge=0
```

7개를 손으로 확인하면 `(v2,b1) (v2,b2) (v3,b1) (v3,b2)` 4개와 `(b4,g1) (b4,g2) (b4,g3)` 3개다. 벽시계 정렬은 `b4` (1020)를 GW의 이벤트(1100대)보다 앞에 놓는다. `b4` 는 `g3` 에 대한 응답인데도 그렇다.

스큐가 커지면 얼마나 망가지는지 seed를 고정해서 쟀다. 시간을 재는 코드가 없고 결과는 seed로 정해지므로 맥의 부하와 무관하다.

<!-- code: demo_skew_sweep.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_skew_sweep.c -o d_skew && ./d_skew
```

```text
skew(ms)  causal_pairs  wall_inv    wall%   lamport_inv vc_inv   
0         43201         0           0.00    0           0        
1         43201         20          0.05    0           0        
2         43201         58          0.13    0           0        
5         43201         319         0.74    0           0        
10        43201         945         2.19    0           0        
20        43201         2819        6.53    0           0        
50        43201         6482        15.00   0           0        
```

이벤트 사이 진짜 간격이 1~4 ms인 모델에서, 스큐가 간격보다 커지는 지점부터 뒤집힘이 빠르게 늘어난다. 벡터와 Lamport 정렬은 스큐와 무관하게 0이다. **순서만 필요하면 Lamport로 충분하다.** 벡터가 필요한 것은 "이 둘은 순서를 매길 수 없다"를 알아야 할 때다.

버그 2가 바로 그 경우다. LWW 게이트웨이는 전달 순서와 상관없이 항상 같은 결과로 수렴한다. **수렴은 하는데 틀린 값으로 수렴한다.** 동시를 감지할 방법이 없으니 한쪽을 조용히 버린다.

| 쓰기 | 벡터 | Lamport | 값 | Lamport LWW | 벡터 + 형제 |
|---|---|---|---|---|---|
| BMS 감량 | `[1,0,0]` | 1 | 50 A | 버려짐 | 형제로 유지 |
| VCU 설정 | `[0,7,0]` | 7 | 120 A | 채택 | 형제로 유지 |
| 게이트웨이 유효값 | | | | **120 A** | **50 A (min)** |
| VCU가 둘 다 보고 쓴 값 | `[1,8,0]` | 9 | 80 A | 80 A | 앞의 둘을 지배 → 80 A |

`gw_apply_vc` 는 세 갈래다. 동시면 보관, 앞서면 교체, 뒤지거나 같으면 버린다. 최종 형제 집합은 **전달받은 쓰기들의 극대원(maximal element) 집합** 이라서 전달 순서에 의존하지 않는다. 하네스는 2개·3개 쓰기의 모든 순열로 이것을 검사한다.

| 들어온 `w` 와 기존 `s` 의 관계 | 동작 |
|---|---|
| `w` BEFORE `s` 또는 EQUAL | 재전송·역전 도착, 버림 (`stale_dropped++`) |
| `w` AFTER `s` | `s` 는 지배됨, 지움 |
| `w` CONCURRENT `s` | 둘 다 유지 |

유효값을 고르는 규칙(`min`)은 벡터 클럭이 정해 주지 않는다. 충돌을 **알려 줄** 뿐이다. 전류 한도라면 작은 쪽이 안전하고, 다른 값이라면 다른 규칙을 정해야 한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o so && ./so   # 67/67 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o so_asan && ./so_asan
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_tsan && ./so_tsan
```

## 7. 자주 하는 실수

정답 코드에 실수를 하나씩 넣어 하네스를 돌린 실제 결과다. 어떤 CHECK가 잡는지까지 그대로 옮겼다.

| 실수 | 결과 (실제 실행) | 처음 걸린 CHECK |
|---|---|---|
| 수신에서 tick 빼먹기 | 60/67 | `vc_is(&q.vc, 1, 4, 0)` |
| Lamport 수신에서 `max` 빼먹기 | 60/67 | `p.lamport == 6` |
| 송신 프레임에 tick 전 값을 싣기 | 53/67 | `vc_is(&m.vc, 1, 0, 0) && m.lamport == 1` |
| `vc_compare` 에서 동시 분기 없음 | 44/63 | `vc_compare(&a, &c) == VC_CONCURRENT` |
| merge를 칸별 max 대신 덧셈으로 | 62/67 | `vc_is(&into, 3, 5, 2)` |
| 지배된 형제를 안 지움 | 66/67 | `runs3 == 6 && bad3 == 0` |
| 지배당한 쓰기(stale)도 보관 | 62/67 | `runs3 == 6 && bad3 == 0` |
| 정렬 키에서 합을 빼고 node로만 | 64/67 | 병합 순서 문자열 비교 |

**1. 동시를 `0` 으로 돌리는 비교 함수로 qsort** (위 데모). 왜 틀렸나: 전순서가 아니어서 `a→c` 가 뒤집힌다. 고침: 합 → node → seq 처럼 **이벤트마다 유일한 전순서 키**를 쓴다.

```c
if (sa != sb) return sa < sb ? -1 : 1;   /* 합: a→b 면 반드시 증가 */
if (a->node != b->node) return a->node < b->node ? -1 : 1;
return (a->seq > b->seq) - (a->seq < b->seq);
```

**2. 벡터 비교를 뺄셈으로** `if (a[i] - b[i] < 0)`. 왜 틀렸나: `uint32_t` 뺄셈은 음수가 되지 않으므로 이 조건은 항상 거짓이다. `int` 로 캐스팅해도 값이 크면 틀어진다. 고침: `<`, `>` 로 직접 비교하고 불리언으로 기록한다. 랩어라운드를 일부러 허용할 때만 연습 3의 순환 비교를 쓴다.

**3. 벽시계에 인과 의미를 부여.** "ms 단위 타임스탬프가 있으니 정렬하면 되지"는 위 표처럼 스큐가 간격을 넘는 순간 틀린다. 고침: 시계 숫자는 ECU 안에서만 비교한다. 다른 ECU와 비교하려면 동기화 오차를 함께 보고, 오차 안의 순서는 "알 수 없음"으로 다룬다.

**4. Lamport 값이 크면 나중이라고 판단.** 왜 틀렸나: 전수 검사의 동시 쌍 43,350개 중 38,131개(88%)는 Lamport 값이 달랐다. 값 차이는 인과가 아니라 **그 ECU가 얼마나 바빴는지** 를 보여 주기도 한다.

**5. merge 뒤에 tick을 생략하고 이벤트 시계를 저장.** 왜 틀렸나: 수신 이벤트가 앞선 이벤트와 같은 벡터를 가질 수 있어 `VC_EQUAL` 이 나온다. 위 표 첫 줄이 이 경우다.

**6. 재부팅을 무시.** 왜 틀렸나: RAM이 지워지면 ECU의 칸이 0으로 돌아가 재부팅 전 이벤트가 "나중"으로 보인다. 고침: 연습 4의 boot epoch.

## 8. 면접 Q&A

<details><summary>Q1. Lamport 클럭과 벡터 클럭의 차이는? 왜 Lamport는 동시성을 판정 못 하나?</summary>

Lamport는 정수 하나이고 `a→b ⇒ L(a)<L(b)` 만 보장한다. 역방향은 성립하지 않아 `L(a)<L(b)` 일 때 `a→b` 일 수도, `a∥b` 일 수도 있다. 이 레슨의 시나리오에서 `(b1,b2)` 와 `(v1,b2)` 는 Lamport 값 `(1,2)` 가 같지만 앞은 인과, 뒤는 동시다. 입력이 같고 답이 다르니 어떤 규칙도 판정할 수 없다.

벡터 클럭은 N칸이고 `a→b ⇔ V(a)<V(b)` 가 양방향으로 성립한다. 칸별로 비교해 방향이 섞이면 동시다.

**꼬리질문: 벡터를 N보다 작게 줄일 수 있나?** 일반적으로는 안 되는 것으로 알려져 있다(Charron-Bost 1991). 모든 동시 관계를 정확히 표현하려면 최악의 경우 N칸이 필요하다. 근사(bloom clock)는 거짓 양성을 허용하는 대신 크기를 줄인다.
</details>

<details><summary>Q2. 차량이나 드론의 ECU에서 벡터 클럭을 실제로 쓸 수 있나? 비용은?</summary>

ECU 수가 고정되고 작아서(수 개~수십 개) 서버 클러스터보다 조건은 좋다. 공간은 노드당 N칸, 메시지당 N칸이다. Classic CAN은 데이터가 8바이트뿐이라 16비트 3칸이면 6바이트를 쓰고 2바이트만 남는다. 그래서 프레임마다 싣는 일은 CAN FD(64바이트)나 Ethernet(SOME/IP) 쪽이 현실적이다.

Classic CAN에서는 보통 벡터를 싣지 않고, ECU가 로그에 "받은 메시지 ID + 순번"을 남긴 뒤 사후에 재구성한다. 이 레슨의 `stamp_logs` 가 정확히 그 방식이다. 이때 프레임에 순번이 있어야 하는데, 안전 통신의 alive counter가 그 역할을 할 수 있다(AUTOSAR E2E 프로파일 — 세부는 확인 필요).

**꼬리질문: 칸이 넘치면?** 16비트는 금방 돈다. 연습 3의 순환 비교는 두 값의 차이가 2^15 미만일 때만 뜻이 있다.
</details>

<details><summary>Q3. PTP로 시계를 맞추면 벡터 클럭이 필요 없지 않나?</summary>

필요한 곳이 줄어들 뿐이다. 동기화 오차가 ε이면, 두 이벤트의 시각 차가 ε보다 작을 때는 순서를 믿을 수 없다. demo_skew_sweep에서 스큐가 이벤트 간격을 넘으면서 뒤집힘이 늘어나는 것이 같은 현상이다. PTP/gPTP를 쓰면 ε을 작게 만들 수 있어 벽시계 정렬이 대부분 맞게 되지만, "오차 안의 순서"는 여전히 알 수 없다.

그리고 시계 동기화는 "언제"에 답하고, 벡터 클럭은 "**영향을 줄 수 있었나**"에 답한다. 두 ECU가 실제로 메시지를 주고받지 않았다면 시각이 달라도 인과는 없다. 설정 충돌 판단처럼 영향 관계가 중요한 곳에는 시각이 답이 되지 못한다. 실제 시스템은 둘을 섞어 쓴다 (예: Google Spanner의 TrueTime은 오차 구간을 명시한다).

**꼬리질문: 동기화 안 된 로그를 맞추려면?** 메시지 쌍(송신 시각, 수신 시각)으로 스큐를 추정한다 (DIST-A07에서 다룰 주제).
</details>

<details><summary>Q4. 동시 쓰기를 발견하면 그다음은? LWW는 왜 위험한가?</summary>

벡터 클럭은 충돌을 **탐지**할 뿐 해결하지 않는다. 선택지는 넷이다.

| 방법 | 대가 |
|---|---|
| LWW | 간단하고 수렴하지만 한쪽을 조용히 버린다 |
| 형제 유지 + 앱 규칙 (`min`, 사람 확인) | 저장·해소 로직 필요 |
| 도메인 규칙으로 병합 (CRDT, DIST-M07) | 연산이 교환·결합·멱등이어야 함 |
| 한 ECU만 쓰도록 소유권 지정 | 쓰기 경로가 단일 장애점 |

안전 관련 한도라면 LWW는 쓰면 안 된다. 이 레슨에서 BMS의 50 A가 VCU의 120 A에 밀려 사라진 것이 그 이유이고, VCU가 이긴 것은 그저 Lamport 값이 커서(더 바빠서)다. 펌웨어에서는 소유권을 하나로 정하는 방법을 먼저 검토한다. 애초에 충돌이 생기지 않기 때문이다.

**꼬리질문: Dynamo의 형제와 같은 것인가?** 구조가 같다(DeCandia 2007). 버전 벡터와 벡터 클럭의 차이는 Q5.
</details>

<details><summary>Q5. 벡터 클럭(vector clock)과 버전 벡터(version vector)는 같은가?</summary>

자료구조는 같고 세는 대상이 다르다. 벡터 클럭은 **모든 이벤트**의 인과를 추적한다 (local·send·recv마다 tick). 버전 벡터는 **데이터 복제본의 업데이트**만 추적한다 (쓰기마다 tick, 동기화에서는 tick 없이 병합). 이번 Part C의 쓰기 벡터는 쓰기에 붙은 버전이라는 점에서 버전 벡터 쪽에 가깝고, Part A의 ECU 시계는 벡터 클럭이다.

**꼬리질문: 쓰는 곳이 계속 늘면?** 클라이언트마다 칸이 생겨 벡터가 커진다. 서버 측 ID만 쓰고 dotted version vector 같은 변형을 쓰는 이유다(세부는 DIST-M07 쪽에서 확인).
</details>

<details><summary>Q6. 로그를 하나로 합칠 때 벡터로 어떻게 정렬하나? 동시 이벤트의 순서는?</summary>

벡터는 부분 순서라 "정렬"이 바로 정의되지 않는다. 인과 순서를 어기지 않는 전순서로 확장(linear extension)한다. 벡터 원소의 합은 `a→b` 면 반드시 늘어나므로 `(합, node, seq)` 를 키로 쓰면 된다. 다른 방법으로는 위상 정렬이 있다. 동시 이벤트의 앞뒤는 **임의로 정한 것** 이므로 분석 도구에서 표시를 함께 보여 줘야 한다 (이 레슨의 `||` 표시).

**꼬리질문: `vc_compare` 를 qsort에 넘기면?** 동시가 0이면 전순서가 아니다. 이 맥에서 무작위 실행 300개 전부 인과 순서가 깨졌다(inversions 7616).
</details>

<details><summary>Q7. TSan 같은 data race 검출기와 벡터 클럭은 무슨 관계인가?</summary>

"동시"의 정의가 같다. 스레드를 프로세스로, 락 해제·획득을 메시지로 보면 그대로 대응한다. 언락은 그 스레드의 시계를 락에 저장하고(송신), 락 획득은 그 시계를 합친다(수신). 두 접근 중 적어도 하나가 쓰기이고 벡터가 비교 불가능하면 data race다. 연습 6에서 이 알고리즘을 약 20줄로 구현한다. TSan이 쓰는 내부 알고리즘도 스레드별 벡터 클럭 기반으로 알고 있다 (세부는 버전마다 달라 확인 필요).

타이밍에 의존하지 않기 때문에, 그 실행에서 **실제로 충돌하지 않은** 접근 쌍도 잡아낸다 (연습 6의 `no_lock` 은 시간상 떨어져 있어도 race). 대신 그 실행에서 지나가지 않은 코드는 볼 수 없다 (TOOL-M09의 "TSan이 못 보는 것").

**꼬리질문: 벡터 매번 복사가 비싸지 않나?** 그래서 FastTrack(Flanagan & Freund 2009) 계열은 대부분의 접근을 "스레드 ID + 시각" 하나(epoch)로 기록한다. 연습 1의 "칸 하나만 비교"와 같은 발상이다.
</details>

<details><summary>Q8. 타이밍 의존 버그를 재현 가능하게 만들려면? 이 레슨의 테스트는 왜 결정적인가?</summary>

실행 순서를 프로그램이 정하게 만든다. 여기서는 스레드를 전혀 안 쓰고, xorshift seed로 "다음에 어느 ECU가 무슨 이벤트를 하나"를 고른다. 같은 seed면 출력이 바이트까지 같다 (`./so | md5` 를 두 번 돌려 같은 해시를 확인했다). 실패하면 그 seed만 다시 돌리면 된다. 이 방식의 일반화가 TEST-A05의 결정론적 시뮬레이터다. 분산 쪽의 검증은 DIST-A05 (선형성 검사기)로 이어진다.

**꼬리질문: 그럼 진짜 스레드 테스트는 필요 없나?** 필요하다. 시뮬레이터는 모델한 것만 잡는다. 실제 하드웨어 경합은 SIV-M09의 스트레스 테스트 같은 쪽의 몫이다.
</details>

## 9. 추가 연습

### 연습 1 — O(1) 인과 판정

이벤트 `a` 의 출신 ECU를 알면 벡터 전체를 비교할 필요가 있나? `a≠b` 일 때 칸 하나로 `a→b` 를 판정하는 식을 쓰고 전 쌍에서 확인하라.

<details><summary>답</summary>

`a→b ⇔ V(b)[node(a)] ≥ V(a)[node(a)]`. `V(a)[node(a)]` 는 `a` 가 자기 ECU의 몇 번째 이벤트인지를 나타낸다(이벤트마다 tick 하므로). `b` 의 그 칸이 그 값 이상이면 `b` 의 인과 과거에 `a` 가 들어 있다는 뜻이다. practice.c `hb_fast`, 100 seed의 156,000쌍에서 `vc_compare` 와 일치를 확인했다.
</details>

### 연습 2 — 인과 순서 전달 버퍼

게이트웨이가 메시지를 재조립해 인과 순서로만 올리고 싶다. "전달해도 되는가" 조건을 쓰고, 도착 순서 6가지에서 원인이 결과보다 항상 먼저 전달됨을 보여라. 여기서는 벡터를 **송신 수만 세도록** 정의가 바뀐다.

<details><summary>답</summary>

송신자 `s` 의 메시지 `m` 이 전달 가능하려면 `m.vc[s] == local[s]+1` (그 송신자의 바로 다음 메시지)이고, 다른 모든 `k` 에 대해 `m.vc[k] ≤ local[k]` (원인을 이미 봤다)이어야 한다 (Birman-Schiper-Stephenson). 전달 시 `local[s]++`. 조건을 못 맞춘 메시지는 버퍼에 남는다. 그래서 m1이 영영 안 오면 m2는 전달되지 않는다 (유실 대비 재전송 또는 타임아웃이 별도로 필요하다). practice.c `cd_deliverable`, `cd_run`.
</details>

### 연습 3 — CAN 프레임에 실을 때 (16비트 3칸, 랩어라운드)

벡터를 빅엔디언 6바이트로 인코딩하고, 65535 다음 0 이 "앞으로 간 것"으로 보이는 비교를 작성하라. 이 비교가 맞으려면 어떤 전제가 필요한가?

<details><summary>답</summary>

`d = (uint16_t)(a - b)` 로 차이를 구해 `0` 이면 같음, `d < 0x8000` 이면 `a` 가 앞선 것, 그 밖이면 뒤진 것으로 본다. 전제는 **비교하는 두 값의 차이가 2^15 미만** 이라는 것이다(RFC 1982의 일련번호 산술과 같은 발상). 오래된 로그를 나중에 비교하면 틀어질 수 있으니 긴 분석에는 쓰지 않는다. `(int16_t)` 캐스팅으로 음수를 얻는 것은 구현 정의 동작이라 뺄셈 결과를 부호 없이 비교했다. practice.c `vc16_compare`.
</details>

### 연습 4 — ECU 재부팅과 boot epoch

ECU가 재부팅하면 RAM의 벡터가 0이 된다. 어떤 문제가 생기고, 왜 boot epoch로 고쳐지나? 잊어버린 ECU1 칸은 어떻게 되나?

<details><summary>답</summary>

재부팅 전 `[3,5,0]` 뒤에 재부팅 후 첫 이벤트 `[1,0,0]` 이 오면 `vc_compare` 는 앞 이벤트를 AFTER로 판정한다. 시간이 거꾸로 간 셈이다. 칸의 상위 8비트에 부팅 번호를 두면 자기 칸의 순서가 복구된다 (`ep_entry(1,1)` > `3`). 잊어버린 다른 칸은 0이 되므로 재부팅 전 VCU 이벤트와의 관계는 CONCURRENT로 나온다. 틀린 "앞/뒤" 대신 "모름"이 되는 보수적인 결과다. 부팅 번호는 NVM에 부팅당 한 번만 쓰면 되므로 플래시 마모는 문제가 되지 않는다. practice.c `reboot_with_epoch`.
</details>

### 연습 5 — 형제를 하나로 접는 쓰기

게이트웨이가 형제 `{50 A [1,0,0], 120 A [0,7,0]}` 을 해소하는 쓰기를 만든다. 벡터는 무엇이어야 하나? 늦게 도착한 옛 BMS 쓰기는 어떻게 되나?

<details><summary>답</summary>

모든 형제의 칸별 max를 취한 뒤 자기 칸 tick: `[1,7,1]`, 값은 `min = 50`. 이 쓰기가 둘을 지배하므로 형제는 1개가 된다. 이후 옛 쓰기가 다시 와도 지배당해 버려진다. 다만 이 해소를 **보지 못한** VCU의 새 쓰기 `[1,8,0]` 은 `[1,7,1]` 과 동시이므로 충돌이 다시 생긴다. 충돌이 사라지지 않고 줄어드는 구조이며, 그래서 소유권을 정하는 설계가 낫다. practice.c `resolve`.
</details>

### 연습 6 — 벡터 클럭으로 data race 검출

스레드마다 `C[t]`, 락마다 `L[m]`, 변수마다 `W`, `R` 벡터를 둔다. Acquire, Release, Read, Write의 규칙을 쓰고 6개 추적을 판정하라.

<details><summary>답</summary>

Acquire: `C[t]` 에 `L[m]` 을 합친다. Release: `L[m] = C[t]`, `C[t][t]++`. Read: `W ≤ C[t]` 가 아니면 race, `R[t] = C[t][t]`. Write: `W ≤ C[t]` 이고 `R ≤ C[t]` 가 아니면 race, `W[t] = C[t][t]`. 판정: 락 없음 race 1, 같은 락 0, 다른 락 race 1, 읽기-읽기 0, 읽기-쓰기 race 1, 락을 스쳐 지나간 핸드오프 0. 시각 정보가 전혀 없는데도 판정된다는 점이 핵심이다. practice.c `rd_step`. 알고리즘은 Djit+ 계열의 단순화 판이다 (FastTrack 은 여기에 epoch 최적화).
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o pr && ./pr    # 26/26 passed, exit 0
```

## 10. 핵심 정리

- `a→b` 정의 3개: 같은 ECU 안의 순서, 송신→수신, 전이. 어느 쪽으로도 없으면 동시.
- 벡터: local·send는 내 칸 +1, recv는 칸별 max 뒤 내 칸 +1. `V(e)[i]` = e의 과거에 있는 ECU i의 이벤트 수.
- 비교: 칸별로 보고 "작은 칸"과 "큰 칸"이 둘 다 있으면 동시. 뺄셈 금지.
- Lamport: `a→b ⇒ L(a)<L(b)` 뿐. 역은 거짓이고, 같은 숫자 쌍이 인과일 수도 동시일 수도 있다.
- 로그 병합: `(벡터 합, node, seq)` 전순서. 동시를 0으로 돌리는 qsort 비교 함수는 금지.
- 벽시계 스큐가 이벤트 간격을 넘으면 정렬이 인과를 뒤집는다. 벡터로 찾고 벡터로 고친 결과가 7 → 0.
- 벡터는 충돌을 알려 줄 뿐이다. LWW로 지우면 안전 값이 사라질 수 있다. 형제 유지 + 도메인 규칙, 또는 소유권 지정.
- TSan의 data race 판정도 같은 happens-before다. ECU 대신 스레드, 메시지 대신 락.

## 참고자료

- L. Lamport, "Time, Clocks, and the Ordering of Events in a Distributed System", *CACM* 21(7), 1978.
- C. Fidge, "Timestamps in Message-Passing Systems That Preserve the Partial Ordering", 1988 / F. Mattern, "Virtual Time and Global States of Distributed Systems", 1989.
- B. Charron-Bost, "Concerning the size of logical clocks in distributed systems", *IPL* 39(1), 1991 — 벡터 크기 하한.
- K. Birman, A. Schiper, P. Stephenson, "Lightweight Causal and Atomic Group Multicast", *ACM TOCS* 9(3), 1991.
- G. DeCandia et al., "Dynamo: Amazon's Highly Available Key-value Store", SOSP 2007 — 형제와 벡터 클럭.
- C. Flanagan, S. Freund, "FastTrack: Efficient and Precise Dynamic Race Detection", PLDI 2009.
- ThreadSanitizer 알고리즘 설명 — https://github.com/google/sanitizers/wiki/ThreadSanitizerAlgorithm
- C11 7.22.5 (qsort 비교 함수의 전순서 요구).
- 같은 층: TEST-A05 (결정론적 시뮬레이션), DIST-A05 (선형성 검사기), SIV-M09 (스트레스 테스트), TOOL-M09 (TSan 보고 읽기), QFIN-M07 (비동기 메시지 경쟁). 이후 트랙: DIST-M01/M02 (PTP/gPTP), DIST-M07 (CRDT), DIST-A07 (로그 상관·스큐 추정).
