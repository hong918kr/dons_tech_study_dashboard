---
id: QFIN-M07
time: 95분
updated: 2026-11-08
---
# QFIN-M07 OMS: ExecutionReport 처리와 Cancel/Replace 경쟁

## 학습 목표

- ClOrdID / OrigClOrdID 체인으로 주문 하나의 수명 전체를 추적하고, 체인의 어느 ID로 온 보고든 같은 주문에 붙인다.
- CumQty · LeavesQty · 포지션을 **도착 순서와 중복에 상관없이** 맞게 계산한다. 상태를 저장하지 않고 사실에서 계산하는 설계로.
- too-late-to-cancel, replace 중 부분 체결, 순서가 뒤바뀐 ExecutionReport, unsolicited cancel을 시나리오 테스트로 고정한다.
- 이 경쟁이 **스레드가 아니라 비동기 메시지 사이의 경쟁**임을 설명하고, TSan으로는 안 보이는 버그를 시드 sweep과 불변식으로 잡는다.
- 버그를 하나씩 심어 보는 mutation 표로 "이 테스트가 정말 잡는가"를 증명한다.

## 1. 문제

NVMe 펌웨어에서 host의 Abort가 도착했을 때 그 커맨드가 이미 끝나 버렸다면 Abort는 "못 멈췄다"고 답하고 원래 완료도 그대로 나간다(스펙 세부는 기억에 의존). 트레이딩의 cancel과 체결이 정확히 같은 모양이고, 면접관(HRT, Jane Street, Citadel Securities 류의 trading-systems 직군)이 OMS에서 제일 먼저 묻는 것이 이 경쟁이다.

주문을 내고 나면 프로그램은 거래소의 상태를 **볼 수 없다**. 볼 수 있는 것은 얼마 전에 일어난 일을 알려 주는 메시지뿐이고, 그 사이에 또 무슨 일이 일어났는지는 모른다. 그래서 OMS(Order Management System, 주문 관리 시스템)는 "내가 아는 거래소 상태"를 메시지로부터 다시 만들어야 하고, 그 추정이 틀리면 돈이 샌다 — 포지션을 틀리게 알거나, 이미 취소된 물량을 믿고 또 내거나, 이미 체결된 물량에 또 수정 요청을 낸다.

**이 레슨의 프로그램에는 스레드가 하나뿐이다.** 그래도 경쟁은 있다. cancel 요청이 거래소로 가는 동안 체결 보고가 반대 방향으로 오면, 두 메시지는 선 위에서 서로 스쳐 지나간다. 공유 메모리도, 데이터 레이스도 없다. TSan이 할 말도 없다(§5에서 실제로 돌려 본다). 이것은 TEST-A05의 "타임아웃 타이머와 RX 완료 ISR이 같은 요청을 둘 다 완료시키는" 경쟁과 같은 모양이다 — **요청 하나에 완료가 둘**, 그리고 둘의 순서를 내가 정할 수 없다.

```svg
<svg viewBox="0 0 720 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="q7a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">too-late-to-cancel: 요청과 체결이 선 위에서 스쳐 지나간다</text>
  <text x="95" y="48" fill="currentColor" font-weight="bold">OMS</text>
  <text x="590" y="48" fill="currentColor" font-weight="bold">거래소</text>
  <line x1="115" y1="58" x2="115" y2="290" stroke="currentColor"/>
  <line x1="610" y1="58" x2="610" y2="290" stroke="currentColor"/>
  <line x1="115" y1="80" x2="610" y2="130" stroke="currentColor" marker-end="url(#q7a)"/>
  <text x="300" y="92" fill="currentColor">Cancel (cl=2, orig=1)</text>
  <line x1="610" y1="90" x2="115" y2="150" stroke="currentColor" marker-end="url(#q7a)"/>
  <text x="330" y="138" fill="currentColor">Trade 100 (cl=1, cum=100)</text>
  <rect x="470" y="150" width="240" height="44" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="480" y="168" fill="currentColor">거래소가 cancel을 처리: 이미 끝남</text>
  <text x="480" y="186" fill="currentColor">→ CxlRej reason=Too late</text>
  <line x1="610" y1="200" x2="115" y2="252" stroke="currentColor" marker-end="url(#q7a)"/>
  <text x="320" y="218" fill="currentColor">CancelReject (cl=2, TooLate)</text>
  <rect x="10" y="150" width="96" height="64" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="16" y="170" fill="currentColor" font-size="12">Cancel 보낸</text>
  <text x="16" y="186" fill="currentColor" font-size="12">시점의 믿음:</text>
  <text x="16" y="204" fill="currentColor" font-size="12">"아직 살아 있다"</text>
  <rect x="10" y="240" width="96" height="44" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="16" y="258" fill="currentColor" font-size="12">최종: Filled</text>
  <text x="16" y="276" fill="currentColor" font-size="12">position +100</text>
</svg>
```

용어 — 괄호 안 번호는 FIX 4.4 태그이며 **작성자 기억**이다(실제 값은 거래소의 Rules of Engagement로 확인할 것).

| 용어 | 뜻 |
|---|---|
| ClOrdID (11) | 클라이언트가 **요청마다** 새로 붙이는 ID. 주문의 ID가 아니라 요청의 ID |
| OrigClOrdID (41) | cancel/replace가 가리키는 "직전에 확정된 요청"의 ClOrdID |
| ExecID (17) | 보고 한 건의 ID. 중복 판별 키 |
| OrderQty (38) / CumQty (14) / LeavesQty (151) | 총 수량 / 누적 체결 / 아직 체결될 수 있는 양 |
| LastQty (32) | 이번 보고의 체결량 |
| ExecType (150) | 이번 보고가 **무슨 사건**인지: New, PendingCancel, Canceled, PendingReplace, Replaced, Trade, Rejected |
| CancelReject (35=9) | cancel/replace 요청 자체의 거절. ExecID 없음, 사유는 tag 102 |

```svg
<svg viewBox="0 0 720 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="q7b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">주문 하나 = ClOrdID 체인 하나</text>
  <rect x="20" y="40" width="150" height="62" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="32" y="62" fill="currentColor" font-weight="bold">cl=1 (주문)</text>
  <text x="32" y="82" fill="currentColor" font-size="12">qty 100 · Done</text>
  <rect x="210" y="40" width="150" height="62" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="222" y="62" fill="currentColor" font-weight="bold">cl=2 (replace)</text>
  <text x="222" y="82" fill="currentColor" font-size="12">orig=1 · qty 150 · Done</text>
  <rect x="400" y="40" width="150" height="62" rx="8" class="f-warn" stroke="currentColor"/>
  <text x="412" y="62" fill="currentColor" font-weight="bold">cl=3 (replace)</text>
  <text x="412" y="82" fill="currentColor" font-size="12">orig=2 · qty 90 · 거절</text>
  <rect x="590" y="40" width="120" height="62" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="600" y="62" fill="currentColor" font-weight="bold">cl=4 (cancel)</text>
  <text x="600" y="82" fill="currentColor" font-size="12">orig=2 · 진행 중</text>
  <line x1="170" y1="71" x2="208" y2="71" stroke="currentColor" marker-end="url(#q7b)"/>
  <line x1="360" y1="71" x2="398" y2="71" stroke="currentColor" marker-end="url(#q7b)"/>
  <line x1="550" y1="71" x2="588" y2="71" stroke="currentColor" marker-end="url(#q7b)"/>
  <text x="20" y="130" fill="currentColor">거절된 cl=3 은 건너뛴다: 다음 요청의 orig 는 "마지막으로 확정된" cl=2.</text>
  <text x="20" y="152" fill="currentColor">유효 수량은 체인에서 가장 뒤의 확정 링크(eff)에서 온다 = 150.</text>
  <text x="20" y="174" fill="currentColor">index_: 1→주문0, 2→주문0, 3→주문0, 4→주문0 (보낼 때 등록. 응답 올 때 아님)</text>
</svg>
```

### Part A — ClOrdID 체인과 주문 상태

`Oms` 클래스의 `cancel`, `replace`, `on_exec`, `on_cancel_reject`, `view`를 채운다 (`new_order`는 주어짐).

- `cancel` / `replace`: 수정 불가(종료됐거나 요청이 진행 중)면 `nullopt`. 새 ClOrdID를 체인과 `index_`에 **보낼 때** 등록하고 `orig`는 마지막으로 확정된 ID로.
- `new_qty`는 "남은 양"이 아니라 **새 주문 총량**이다.
- `view`: 상태를 저장하지 말고 사실로부터 계산. 우선순위 `Canceled/Rejected > Filled > PartiallyFilled > New > PendingNew`. `pending`(Cancel/Replace 진행 중)은 상태와 **별개의 축**이다.

### Part B — 경쟁 시나리오

아래가 전부 맞게 나와야 한다.

| 시나리오 | 도착하는 것 | 맞는 결과 |
|---|---|---|
| too-late-to-cancel | Cancel 보냄 → Trade 100 → CxlRej(TooLate) | Filled, 포지션 +100, pending 없음 |
| 거절이 체결을 앞지름 | CxlRej → Trade 100 | 거절 후 New, Trade 후 Filled |
| 취소 중 부분 체결 | Cancel 보냄 → Trade 40 → Canceled(cum=40) | Canceled, cum 40, 포지션 +40, 잔량 0 |
| replace 중 부분 체결 | Replace(150) → 옛 ID로 Trade 30 → Replaced(150) | cum 30, leaves 120 |
| replace-down < 체결량 | Replace(40) → Trade 60 → Replaced(order_qty=60) | 거래소가 준 60을 따른다, Filled |
| 새 ID Trade가 먼저 | Replace(150) → 새 ID로 Trade 120 → Replaced | Trade가 유실되지 않음 |
| Trade 뒤바뀜 | Trade(cum 50, last 20) → Trade(cum 30, last 30) | cum 50, 그 사이엔 `fill_gap`=30 |
| 중복 | 같은 ExecID 두 번 | 한 번만 반영, `duplicates`=1 |
| unsolicited cancel | 요청 없이 Canceled | Canceled, 잔량 0, 이후 수정 불가, 늦은 New로 되살아나지 않음 |
| replace 중 unsolicited | Replace → Canceled(요청 없음) → CxlRej(TooLate) | 계속 Canceled, 끝에는 pending 없음 |
| 취소 뒤 늦은 체결 | Canceled(cum=35) → Trade 35 | 상태 그대로, 포지션은 −35 (체결은 체결이다) |

### Part C — 경쟁을 찾는 도구

스레드를 안 쓰므로 TSan이 할 일은 없다. 대신:

1. **전수 순열.** 스크립트 몇 개는 메시지 전체를 모든 순서로 넣어 보고 최종 뷰가 같은지 본다 (5개 120가지, 7개 5040가지).
2. **seed sweep.** 주어진 모의 거래소(`Venue`) + 뒤섞는 선로(`window`, `dup_pct`)를 2000 seed 돌린다. 네가 채울 것은 판정자 `check_step`(매 스텝 불변식)과 `check_quiescent`(다 끝난 뒤 거래소와 완전 일치), 그리고 `worst_case_fill`(이 주문이 최대 얼마까지 체결될 수 있는가).
3. **coverage 확인.** too-late, 진행 중 체결, unsolicited 등이 실제로 일어났는지 counter로 확인한다. 그 시나리오를 안 건드린 sweep의 "실패 0"은 아무 뜻이 없다.
4. **재현.** 같은 seed는 같은 trace hash를 낸다.

`demo_sweep.cpp`는 `Oms`에 버그를 하나씩 심은 mutant 10종을 같은 sweep에 돌려 "이 도구가 무엇을 잡는지"를 표로 보여 준다.

## 2. Problem (English)

Implement an order management system that tracks orders across asynchronous exchange reports.

```cpp
class Oms {
    Request new_order(Side, Qty, Px);                                  // given
    std::optional<Request> cancel(ClOrdId any_id);
    std::optional<Request> replace(ClOrdId any_id, Qty new_qty, Px new_px);
    void on_exec(const ExecReport &er);
    void on_cancel_reject(const CancelReject &cr);
    OrderView view(ClOrdId any_id) const;
    Qty worst_case_fill(ClOrdId any_id) const;
    Qty position() const;
};
```

Every `ClOrdId` ever issued for an order must resolve to that order. A report may carry any ID of the chain. At most one cancel/replace may be in flight per order. Reports may arrive in **any order** and may be **duplicated** (same `exec_id`). After all reports have arrived the state must equal the exchange's, whatever the arrival order was. `replace` carries the new **total** quantity; the exchange may amend it (for example not below `CumQty`), so `Replaced.order_qty` is authoritative. `CancelReject` reports that a *request* failed; it says nothing about the *order*. `worst_case_fill` must never be below the quantity the exchange could still fill.

Example: buy 100, send cancel, receive `Trade last=100 cum=100`, then `CancelReject TooLate` → state `Filled`, `position() == 100`, `leaves == 0`, no pending request. The reverse arrival order gives the same final result. Suggested pacing: about 40 minutes for the state logic, then talk through testing.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 이 Mac 전용 (27.2 SDK 링크 깨짐)
c++ -std=c++20 -Wall -Wextra -Werror -O2 starter.cpp -o st && ./st
```

실행 결과 (TODO 상태, exit 1, 바로 끝남):

```text
--- Part C: 순열 전수 + seed sweep
FAIL  permutation_mismatches(setup, msgs, 1, g, 100, &total) == 0 && total == 120  (line 627)
FAIL  permutation_mismatches(setup, msgs, 1, g, 150, &total) == 0 && total == 5040  (line 637)
FAIL  permutation_mismatches(setup, msgs, 1, g, -40, &total) == 0 && total == 120  (line 645)
      first failure: seed=0 TODO check_step
FAIL  a.runs == 2000 && a.failures == 0  (line 653)
…
6/50 passed
```

## 4. 힌트

<details><summary>힌트 1 — 저장할 것과 계산할 것</summary>

`state` 필드를 두면 메시지 도착마다 "이 상태에서 이 메시지가 오면?" 표를 채워야 하고, 순서가 바뀌면 칸이 폭발한다. 사실들만 저장하자: 받은 Trade의 합, ER이 말한 CumQty의 최댓값, 체인의 링크별 진행 상태, 종료 여부. 나머지(상태, 잔량, pending)는 `view`에서 계산.

</details>

<details><summary>힌트 2 — ID 찾기</summary>

`index_`에 체인의 모든 ClOrdID를 넣는다. **보낼 때** 넣어야 Replaced보다 먼저 도착한 새 ID의 Trade도 찾을 수 있다. 옛 ID는 지우지 않는다(옛 ID로 체결이 온다).

</details>

<details><summary>힌트 3 — 합치는 연산의 성질</summary>

두 메시지의 효과를 어느 순서로 합쳐도 같으려면, 각 필드가 집합의 합집합이나 max처럼 "교환·결합·멱등" 연산이어야 한다. Trade는 ExecID 집합의 합집합 위의 합, reported_cum은 max, 링크는 Sent→Ack→(Done|Rej)로 앞으로만, eff는 max.

</details>

<details><summary>힌트 4 — worst_case_fill</summary>

취소는 "확인될 때까지 살아 있다". 진행 중 replace는 신/구 중 큰 쪽(내리는 replace도 확정 전까지는 옛 수량이 살아 있다). 종료된 주문은 cum이 아니라 ER이 말한 CumQty까지 — Trade 메시지가 아직 안 왔을 수 있다.

</details>

## 5. 풀이

### Part A — 체인과 상태 분리

ClOrdID를 **요청의 ID**로 보는 것이 핵심이다. 주문 하나의 수명 동안 여러 요청이 있고, 각각이 성공할 수도 거절될 수도 있다. FIX가 요청마다 새 ID를 요구하는 이유는 "내 요청 중 어느 것에 대한 답인가"를 메시지 안에서 판별할 수 있게 하기 위해서다. 그래서 자료구조는 `주문 → 링크 벡터`다.

| 필드 | 뜻 | 갱신 규칙 |
|---|---|---|
| `chain[k].st` | k번째 요청의 진행 | Sent → Ack → (Done \| Rej), 앞으로만 |
| `eff` | 유효 수량을 정한 링크 | `max(eff, k)` |
| `cum` | 받은 Trade의 합 | 새 ExecID일 때 `+= last_qty` |
| `reported_cum` | ER이 말한 CumQty 최댓값 | `max` |
| `term` | 종료 사유 | 한 번 정해지면 고정 |

`view`의 Filled는 `cum >= 유효 수량`의 **계산 결과**이고 저장하지 않는다. 그래서 Filled로 보였다가 Replaced가 뒤늦게 도착해 다시 열리는 일이 공짜로 처리된다 — 테스트 "새 ID Trade가 먼저"가 정확히 이 경우다.

`pending`을 상태에서 분리한 이유: "체결 중이면서 취소 진행 중"처럼 두 축은 독립이다. FIX는 이를 OrdStatus 하나에 우선순위 표로 담는다(Pending Cancel이 높은 쪽 — 정확한 순서는 기억에 의존, 스펙 Vol 4 확인 필요). 이 레슨은 축을 쪼갰다. OMS 내부 표현으로는 그게 다루기 쉽다.

### Part B — 왜 이렇게 하면 경쟁이 사라지는가

```svg
<svg viewBox="0 0 720 240" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="q7c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">사실은 쌓기만 하고, 상태는 매번 계산한다</text>
  <rect x="10" y="40" width="210" height="180" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="22" y="62" fill="currentColor" font-weight="bold">도착하는 메시지 (아무 순서)</text>
  <text x="22" y="86" fill="currentColor" font-size="12">Trade(exec=5, last=30)</text>
  <text x="22" y="106" fill="currentColor" font-size="12">Replaced(cl=2, qty=150)</text>
  <text x="22" y="126" fill="currentColor" font-size="12">Trade(exec=5, last=30) dup</text>
  <text x="22" y="146" fill="currentColor" font-size="12">PendingReplace(cl=2) 늦음</text>
  <text x="22" y="166" fill="currentColor" font-size="12">Canceled(cum=70)</text>
  <text x="22" y="186" fill="currentColor" font-size="12">CxlRej(cl=3, TooLate)</text>
  <rect x="270" y="40" width="200" height="180" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="282" y="62" fill="currentColor" font-weight="bold">사실 (교환 · 멱등 병합)</text>
  <text x="282" y="86" fill="currentColor" font-size="12">seen = seen ∪ {exec}</text>
  <text x="282" y="106" fill="currentColor" font-size="12">cum = Σ last (새 exec만)</text>
  <text x="282" y="126" fill="currentColor" font-size="12">reported_cum = max</text>
  <text x="282" y="146" fill="currentColor" font-size="12">link.st = 앞으로만</text>
  <text x="282" y="166" fill="currentColor" font-size="12">eff = max(k)</text>
  <text x="282" y="186" fill="currentColor" font-size="12">term = 처음 값 고정</text>
  <rect x="520" y="40" width="190" height="180" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="532" y="62" fill="currentColor" font-weight="bold">view = f(사실)</text>
  <text x="532" y="86" fill="currentColor" font-size="12">state, leaves, pending</text>
  <text x="532" y="106" fill="currentColor" font-size="12">order_qty, fill_gap</text>
  <text x="532" y="136" fill="currentColor" font-size="12">저장 안 함</text>
  <text x="532" y="156" fill="currentColor" font-size="12">→ 뒤집힐 상태가 없다</text>
  <line x1="222" y1="130" x2="268" y2="130" stroke="currentColor" marker-end="url(#q7c)"/>
  <line x1="472" y1="130" x2="518" y2="130" stroke="currentColor" marker-end="url(#q7c)"/>
</svg>
```

수학으로 말하면 `on_exec`, `on_cancel_reject`는 join-semilattice 위의 원소를 합치는 fold다. ExecID 집합은 합집합, `reported_cum`과 `eff`는 max, 링크 상태는 Sent < Ack < {Done, Rej}인 부분순서 위의 join이다. 교환·결합·멱등이면 어떤 순서로 fold 해도 같은 값에 수렴한다. 분산 시스템의 CRDT(Conflict-free Replicated Data Type)와 정확히 같은 논리다.

한 곳이 교환 법칙을 살짝 어기는 것처럼 보인다: `term`은 "처음 값 고정"이다. Canceled 뒤 Rejected가 오면 순서에 따라 결과가 갈릴 수 있다. 그러나 프로토콜에서 한 주문에 두 사건이 함께 있지 않으므로(신규 거절된 주문은 cancel될 수 없다) 실제로 일어나지 않는다. 이 가정이 깨지면 그것 자체가 거래소 버그이고, 경보를 울려야 할 일이다.

시나리오별로 무엇이 일을 했는지:

| 시나리오 | 지켜 준 규칙 |
|---|---|
| too-late-to-cancel | 거절은 링크 상태만 바꾼다. 주문의 생사는 체결과 Canceled만 바꾼다 |
| replace 중 부분 체결 | 옛 ID를 지우지 않는다. Trade는 수량과 무관하게 cum에 더한다 |
| replace-down | `Replaced.order_qty`가 권위. 요청 수량을 믿지 않는다 |
| 새 ID Trade가 먼저 | 새 ID를 보낼 때 등록 |
| Trade 뒤바뀜 | `cum`은 받은 Trade의 합, `reported_cum`은 max. 둘을 나눠야 gap이 보인다 |
| 중복 | ExecID 집합 |
| 늦은 PendingX | 앞으로만 |
| unsolicited | 진행 중 요청과 무관하게 Canceled 반영, 링크는 자기 거절을 기다린다 |
| 취소 뒤 늦은 체결 | **포지션은 체결에서만 나온다.** 상태로 체결을 거르지 않는다 |

**현실의 순서 뒤바뀜은 어디서 오는가.** 한 FIX 세션 안에서는 MsgSeqNum 덕분에 순서가 보장되고 틈은 ResendRequest로 메운다(QFIN-M05에서 다룰 주제). 그러니 "cancel과 체결이 스쳐 지나가는" 경쟁(§1 그림)은 FIFO 선로에서도 항상 있는 반면, ER끼리의 뒤바뀜은 **경로가 둘 이상일 때** 생긴다: 주문 세션과 드롭카피(drop copy) 세션을 합칠 때, 여러 게이트웨이/세션으로 분산했을 때, 재접속 후 복구 메시지가 실시간 메시지와 섞일 때, 파티션별로 순서를 주는 바이너리 프로토콜. 그래서 표에서 FIFO 열과 reorder 열을 나눠 본다.

### Part C — 판정자가 도구다

`check_step`이 매 스텝 보는 안전 불변식:

| 불변식 | 잡는 것 |
|---|---|
| `view.cum <= venue.cum` | 이중 계상 |
| `leaves >= 0` | 잔량 계산 오류 |
| `worst_case_fill >= potential_fill` | 리스크 상한이 진실보다 낮다 (**한도 초과로 이어진다**) |
| 한 번 Canceled/Rejected면 되살아나지 않음 | 종료 상태 역행 |

Filled를 빼고 Canceled/Rejected만 불변식에 넣은 이유: Filled는 Part B에서 봤듯이 reorder 중 잠깐 틀릴 수 있다(뒤늦은 Replaced로 다시 열린다). 불변식은 "언제나 참인 것"만 넣어야 한다. 넣었다가 sweep이 정답을 틀렸다고 하면 고칠 곳은 불변식이다.

`check_quiescent`는 선로가 빈 뒤 cum, order_qty, leaves, state, pending 없음, gap 0, 포지션 합까지 **거래소와 완전 일치**를 요구한다. (안전 불변식만 보면 "아무 일도 안 하는 OMS"도 통과한다.)

실행 결과 (정답, Apple clang 21):

```text
--- Part C: 순열 전수 + seed sweep
PASS  permutation_mismatches(setup, msgs, 1, g, 100, &total) == 0 && total == 120
PASS  permutation_mismatches(setup, msgs, 1, g, 150, &total) == 0 && total == 5040
PASS  permutation_mismatches(setup, msgs, 1, g, -40, &total) == 0 && total == 120
PASS  a.runs == 2000 && a.failures == 0
PASS  b.runs == 2000 && b.failures == 0
      coverage: too_late=3655 fill_during_modify=1224 unsolicited=736 clipped=256 reordered=17111 dups=4859
PASS  b.cov.too_late > 0 && b.cov.fill_during_modify > 0 && b.cov.unsolicited > 0
PASS  b.cov.clipped > 0 && b.cov.reordered > 0 && b.cov.dups > 0
PASS  same_hash == 200
PASS  uniq.size() >= 195
50/50 passed
```

coverage 수치가 있어야 "실패 0"이 의미를 갖는다. too-late 3655번, 수정 진행 중 체결 1224번을 거쳐 실패 0이다.

**mutation 표 — 이 도구는 정말 잡는가.** `demo_sweep.cpp`는 `Oms`의 사본 `Mutant`에 버그를 하나씩 켠다. 각 칸은 실패한 seed 수 / 2000. 시뮬레이션 안의 결과이고 시간에 의존하지 않으므로 몇 번을 돌려도 같다.

<!-- code: demo_sweep.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 demo_sweep.cpp -o demo_sweep && ./demo_sweep
```

```text
planted bug                             FIFO   reorder reord+dup   first violation (reord+dup)
none (Mutant = 정답과 같은 논리)         0         0         0   -
FORGET_OLD_ID                           1734      1736      1740   seed 0: cum mismatch id=3
NO_DEDUP                                   0         0      1094   seed 0: cum beyond venue (double count) id=1
CUM_LAST_WRITE                             0       877       894   seed 1: cum mismatch id=3
REVIVE_ON_NEW                              0       639       656   seed 0: dead order revived id=3
REJECT_REVIVES                           807       667       694   seed 6: dead order revived id=1
DROP_AFTER_TERM                            0       320       323   seed 0: cum mismatch id=1
PENDING_NOT_MONO                           0      1026      1043   seed 0: pending stuck id=3
QTY_FROM_REQUEST                         233       237       241   seed 19: order_qty mismatch id=1
LEAVES_LAST_ER                             0      1764      1765   seed 0: leaves mismatch id=1
RISK_ON_REQUEST                         1876      1888      1884   seed 0: worst_case below truth id=1
solution.cpp Oms                           0         0         0   -
(각 칸 = 실패한 seed 수 / 2000)
```

읽는 법:

- **FIFO 열에서 잡히는 것** (`FORGET_OLD_ID`, `REJECT_REVIVES`, `QTY_FROM_REQUEST`, `RISK_ON_REQUEST`)은 단일 세션에서도 나는 버그다. 요청과 체결의 교차(crossing)만으로 충분하다.
- **FIFO 열이 0인 것** (`CUM_LAST_WRITE`, `REVIVE_ON_NEW`, `DROP_AFTER_TERM`, `PENDING_NOT_MONO`, `LEAVES_LAST_ER`)은 뒤바뀜이 있어야 나타난다. 단일 세션 FIFO 전제가 지켜지는 동안은 **숨어 있다가** 드롭카피를 붙이는 날 터진다.
- `NO_DEDUP`은 `dup_pct>0` 칸에서만 잡힌다. **결함 모델에 없는 결함은 sweep이 못 찾는다.** 모델이 도구의 한계다.
- 표의 열끼리 숫자를 비교하지 말 것. seed가 같아도 선로 설정이 다르면 전혀 다른 실행이다.

**재현과 짧은 실패 찾기.** `trace <번호>`는 스텝 수를 1부터 늘리며(최대 20000 seed) 처음 실패하는 trace를 찾아서 출력한다. 재실행 hash가 같음을 함께 확인한다.

```text
bug=CUM_LAST_WRITE steps=4 seed=10669 violation: cum mismatch id=1
replay hash 8dc87eaddffb820c == 8dc87eaddffb820c
  client send New cl=1 orig=0 qty=131
  venue recv
  venue fill
  venue fill
  deliver  ER Trade cl=1 orig=0 exec=3 last=18 cum=129 leaves=2 qty=131  [reordered]
  deliver  ER Trade cl=1 orig=0 exec=2 last=111 cum=111 leaves=20 qty=131  [reordered]
  deliver  ER New cl=1 orig=0 exec=1 last=0 cum=0 leaves=131 qty=131  [reordered]
  deliver  ER Trade cl=1 orig=0 exec=3 last=18 cum=129 leaves=2 qty=131  [dup]
```

cum=129가 먼저 오고 cum=111이 나중에 도착해 값이 **뒤로 간** 경우다. `cum = er.cum_qty`는 111로 끝나고, 18은 사라진다. 이 mutant는 ExecID 중복은 걸러 내므로 뒤따르는 dup이 구해 주지도 못한다.

```text
bug=QTY_FROM_REQUEST steps=4 seed=2913 violation: order_qty mismatch id=1
replay hash cf6f06a26c98969e == cf6f06a26c98969e
  client send New cl=1 orig=0 qty=101
  venue recv
  client send Replace cl=2 orig=1 qty=11
  venue fill
  venue recv (drain)
  venue apply (drain)
  deliver  ER New cl=1 orig=0 exec=1 last=0 cum=0 leaves=101 qty=101
  deliver  ER Trade cl=1 orig=0 exec=2 last=61 cum=61 leaves=40 qty=101
  deliver  ER Replaced cl=2 orig=1 exec=4 last=0 cum=61 leaves=0 qty=61  [reordered]
  deliver  ER PendingReplace cl=2 orig=1 exec=3 last=0 cum=61 leaves=40 qty=101
  deliver  ER PendingReplace cl=2 orig=1 exec=3 last=0 cum=61 leaves=40 qty=101  [dup]
```

11로 내리는 replace와 61 체결이 교차했다. 거래소는 이미 체결된 61 아래로 못 내리므로 61로 확정하는데(이 레슨의 거래소 모델), 요청 수량 11을 믿으면 OMS의 order_qty가 틀린다. 실제로 거래소가 이 경우에 수량을 cum으로 맞추는지, 요청을 거절하는지는 **거래소마다 다르다** (FIX 4.4 Vol 4의 주문 상태 변화 표에 "요청 수량이 cum 이하" 사례가 있는 것으로 기억하지만 원문을 확인하지 못했다). OMS는 ER이 말한 값을 따르면 어느 쪽이든 맞는다.

**이 버그들을 TSan은 전혀 못 본다.** 같은 데모를 `-fsanitize=thread`로 빌드해 돌린 결과: 표의 숫자가 똑같이 나오고 TSan 경고는 0건, exit 0이었다. 프로그램에 스레드가 하나뿐이니 data race가 없다. 이 레슨의 race condition은 **순서 의존 버그**이고, 메모리 모델의 data race와 다른 범주다(SIV-M09의 "TSan이 못 잡는 atomicity violation"과 같은 구분).

`RISK_ON_REQUEST` 한 줄은 다른 버그와 성격이 다르다. 나머지는 나중에 대사(reconciliation)로 찾을 수도 있지만, 리스크 상한이 진실보다 낮으면 그 순간 이미 한도를 넘어 주문을 낸다. 취소를 **보낸 순간** 한도를 풀어 주는 버그다. 2000 seed 중 1876개가 걸렸다(FIFO).

비용 감각: 표 전체(12행 × 3열 × 2000 seed = 72,000 실행)를 도는 데 user 0.86 s. 측정 시점 `uptime` load averages 34.85 19.03 11.36(다른 작업 공유 중), Apple M6 Super 2 + Performance 4 + Efficiency 6, Apple clang 21.0.0. 이 규모면 CI마다 돌리는 데 부담이 없다는 정도로 읽을 것.

**스레드 스트레스와 경계.** SIV-M09는 실제 스레드를 돌려 시뮬레이션이 모델링하지 못한 것을 찾고, 이 레슨의 sweep은 모델 안의 모든 순서를 싸게 훑는다. 둘은 서로 대체하지 못한다. 결정론적 시뮬레이션 일반론(가상 시계, seed, shrink)은 TEST-A05가 맡고, 여기서는 "메시지 순서가 입력이면 seed 하나로 재현된다"만 쓴다.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 solution.cpp -o so && ./so
```

검증 결과 — starter 6/50 passed(exit 1), solution 50/50 passed(exit 0, 6회 반복 동일), practice 24/24 passed(exit 0). `-fsanitize=address,undefined`로 solution, practice, demo_sweep 모두 오류 0. `-fsanitize=thread`도 경고 0(스레드가 하나라 당연하다).

## 7. 자주 하는 실수

**1. `cum = er.cum_qty`, `leaves = er.leaves_qty` — 마지막 도착 값으로 덮어쓰기.**
FIFO에서는 맞아 보인다. 뒤바뀌면 값이 뒤로 간다(위 trace). 고친 코드: Trade마다 `cum += last_qty`, `reported_cum = max(...)`.

**2. "Cancel Reject = 주문은 아직 살아 있다"로 처리.**
거절은 요청의 실패다. 주문은 이미 Filled나 Canceled일 수 있다. `REJECT_REVIVES`가 FIFO에서 807/2000으로 잡혔다. 고친 코드: 링크 상태만 바꾼다.

```cpp
if (l && l->st != LinkSt::Done) l->st = LinkSt::Rej;  // 주문의 term, cum 은 건드리지 않는다
```

**3. replace를 보내면서 옛 ClOrdID를 지운다.**
요청이 처리되기 전까지 거래소는 옛 ID로 체결을 보낸다. 지우면 체결이 "모르는 주문"이 되어 포지션이 샌다. `FORGET_OLD_ID` 1734/2000.

**4. 종료된 주문의 Trade를 버린다.**
"이미 Canceled인데 무슨 체결?"이라며 무시하면 cancel 전에 일어난 체결의 늦은 보고가 사라진다. 포지션은 체결에서만 나온다.

**5. replace 수량을 증분으로 해석.**
replace의 OrderQty는 새 총량이다. 100주 중 30주 체결 후 잔량을 120으로 만들려면 150을 보낸다.

**6. 포지션을 `Σ cum_qty`로 계산.**
CumQty는 스냅숏이고 Trade는 이벤트다. 합산할 것은 이벤트의 `last_qty`다.

**7. 취소를 보낸 순간 리스크 한도를 푼다.**
확인될 때까지 그 물량은 체결될 수 있다. 위 mutation 표의 `RISK_ON_REQUEST`.

**8. 시나리오를 FIFO 순서로만 테스트.**
위 표에서 FIFO 열이 0인 mutant 여섯 개(`NO_DEDUP` 포함)를 전부 놓친다.

## 8. 면접 Q&A

<details>
<summary>Q1. 취소를 보냈는데 체결이 왔습니다. OMS는 어떻게 동작해야 하나요?</summary>

체결은 사실이므로 그대로 반영한다: cum, 포지션. Pending Cancel 표시는 유지하다가 Canceled 또는 CancelReject(Too late to cancel)가 오면 정리한다. 부분 체결이면 Canceled(cum=체결량, leaves=0)가 올 것이고, 전량 체결이면 거절이 올 것이다. 어느 경우든 **체결은 취소 요청의 결과에 좌우되지 않는다.**
꼬리질문 "cancel이 왔는데 왜 체결이 그 뒤에 오죠?" → 단일 세션에서는 거래소 순서대로 오지만, 여러 경로(드롭카피, 세션 분산)면 늦은 체결이 Canceled 뒤에 올 수 있다. 그래서 상태로 체결을 거르지 않는다. Canceled에 실린 CumQty가 이미 그 체결을 알려 주므로 `fill_gap`으로 "받을 게 남았다"를 알 수 있다.

</details>

<details>
<summary>Q2. ClOrdID를 왜 요청마다 새로 만드나요? OrderID 하나로 추적하면 안 되나요?</summary>

ClOrdID는 **어느 요청에 대한 답인지** 구분한다. 같은 ID를 재사용하면 거절이 어느 replace에 대한 것인지 모른다. OrderID(37)는 거래소가 주는 키라 주문 추적에 쓸 만하지만, ACK 전에는 없다. 게다가 replace 때 OrderID가 바뀌는 거래소도 있다(RoE 확인 필요). 그래서 내부 키는 "체인의 모든 ClOrdID → 내부 주문 번호" 맵이다.
꼬리질문 "OrigClOrdID에 무엇을 넣나요?" → 마지막으로 **확정된** 요청의 ClOrdID. 거절된 요청은 건너뛴다(§1 그림). FIX 정의상 "직전 비거절 주문"으로 기억하며 정확한 문구는 버전별로 확인이 필요하다.

</details>

<details>
<summary>Q3. replace 진행 중에 부분 체결이 나면 LeavesQty는?</summary>

`Leaves = OrderQty - CumQty`로 유지된다. 100주 중 30 체결되어 leaves 70인데 150으로 replace하면, Replaced 시점에 order_qty=150, cum=30, leaves=120. OMS는 replace의 결과를 요청 값이 아니라 **Replaced가 알려 준 order_qty**로 반영한다. replace 응답 전에 체결은 옛 수량 기준으로 계속 쌓인다.
꼬리질문 "체결량보다 작게 replace하면?" → 거래소 정책. 이 레슨의 모델은 cum으로 맞췄다. 거절하는 곳도 있으니 OMS가 가정하지 않고 ER을 따른다.

</details>

<details>
<summary>Q4. FIX 세션은 시퀀스 번호로 순서가 보장되는데 왜 out-of-order를 신경 쓰죠?</summary>

두 가지를 구분한다. **Crossing**: 양방향 FIFO가 있어도 요청과 체결은 서로를 모르고 지나간다. 이 경쟁은 항상 있다. **Reorder**: 같은 방향 메시지끼리 순서가 뒤집히는 것. 세션 하나 안에서는 안 생기지만, 드롭카피와 주문 세션을 합칠 때, 재접속 복구와 실시간이 섞일 때, 주문을 여러 세션으로 분산할 때 생긴다. 실측 표에서 FIFO 열은 crossing만, reorder 열은 뒤바뀜까지 본다.
꼬리질문 "그러면 순서를 맞춰 주는 레이어를 두면?" → 복잡한 재정렬 버퍼를 만드는 것보다 처리 자체를 순서 무관(교환·멱등)으로 만드는 편이 싸고 강건하다. 재정렬은 빠진 메시지를 기다리는 시간이 지연이 된다.

</details>

<details>
<summary>Q5. 스레드가 하나인데 이게 왜 race condition인가요? 어떻게 테스트하죠?</summary>

결과가 **제어할 수 없는 사건의 순서**에 따라 달라지면 race condition이다. 스레드는 그 사건들의 흔한 원천 중 하나일 뿐이고, 여기서는 네트워크 메시지가 원천이다. data race(메모리 모델의 정의)와는 구분된다. 실측: 버그 10종이 든 Mutant를 TSan으로 돌려도 경고 0건이다.
테스트: 순서를 입력으로 만든다. 짧은 스크립트는 전수 순열(120, 5040가지), 긴 시나리오는 seed를 받는 모의 거래소 + 스크램블 선로. 판정은 매 스텝 불변식 + 끝에서 거래소 진실과 일치. seed가 같으면 trace hash가 같아 실패를 그대로 재현한다.
꼬리질문 "그 테스트가 믿을 만한가?" → coverage counter로 시나리오 도달을 확인하고, 버그를 일부러 심어 잡히는지 본다(mutation).

</details>

<details>
<summary>Q6. 취소 요청 후 리스크 한도는 언제 풀어 주나요? replace는요?</summary>

취소는 **확인된 후에** 푼다. 그 전까지 잔량 전부가 체결될 수 있다. replace는 확정 전까지 신/구 수량 중 **큰 쪽**으로 계산한다. 올리는 replace는 거래소가 먼저 적용했을 수 있고, 내리는 replace는 아직 적용 안 됐을 수 있기 때문이다. 보수적 상한은 "거래소가 할 수 있는 어떤 일을 해도 넘지 않는 값"이어야 한다. 실측: 요청 즉시 낮추는 `RISK_ON_REQUEST`는 FIFO에서도 1876/2000 seed에서 상한 위반.
꼬리질문 "Canceled를 받았는데 그때 체결 메시지가 안 왔으면?" → Canceled의 CumQty가 알려 준 양(`reported_cum`)까지 상한에 넣는다.

</details>

<details>
<summary>Q7. Unsolicited cancel은 왜 오고 어떻게 처리하나요?</summary>

거래소/브로커가 임의로 취소한 경우: 세션 끊김 시 자동 취소(cancel-on-disconnect), 자기 체결 방지(STP), 장 종료, 위반 취소 등(사유 필드는 거래소별). 처리: 요청이 없어도 Canceled를 그대로 반영. 핵심은 진행 중인 replace/cancel이 있어도 그쪽 링크는 자기 응답을 기다리고, 주문은 이미 끝났다는 것이다. 그 뒤에 온 요청 거절이 주문을 되살리면 안 된다.
꼬리질문 "전략에는 어떻게 알리죠?" → pending을 통해 "내 요청이 아닌 취소"임을 구분해 전달한다. 재호가 로직이 "내가 취소했으니 재호가"로 오해하면 STP 등으로 끊긴 주문을 다시 내게 된다.

</details>

<details>
<summary>Q8. OMS를 멀티스레드로 만들면 달라지는 게 뭔가요?</summary>

OMS 로직은 그대로 두고 **주문별(또는 심볼별) 샤딩**으로 한 주문의 메시지가 한 스레드로만 가게 한다. 그러면 주문 안에서는 지금과 같이 싱글 스레드이고, 메모리 공유는 포지션 합계 같은 집계 지점에만 생긴다. 집계는 샤드별 카운터 + 합산, 또는 SPSC 큐로 집계 스레드에 전달(DS-A01).
꼬리질문 "seen_ 집합이 하루 종일 커지면?" → 주문별로 쪼개고 주문 종료 후 일정 시간 뒤 해제. 거래일 단위로 ExecID 유일성이 보장되는 곳이 많으니 일 단위 초기화(ExecID의 유일성 범위는 RoE로 확인).

</details>

## 9. 추가 연습

1. **평균 체결가.** cum과 notional을 정수로 유지해 `avg_px`를 계산하라. 체결 메시지의 모든 순열에서 같아야 한다. 부동소수점을 쓰면 왜 위험한가?

<details><summary>답</summary>

`Ledger::avg_px_x100`. notional = Σ q·px, 나눗셈은 조회할 때만 한다. 부동소수 누적은 순서에 따라 비트가 달라지므로 "순서 무관"이 깨진다. 정수 notional은 오버플로만 조심하면 교환 법칙이 성립한다(QFIN-B01).

</details>

2. **드롭카피 병합.** 주문 세션과 드롭카피가 같은 체결을 각각 FIFO로 보낸다. 세션 간 모든 끼어들기(4+4건이면 C(8,4)=70가지)에서 cum이 맞는지, 드롭카피가 일부를 놓친 경우도 확인하라.

<details><summary>답</summary>

`interleave` 전수 + ExecID 합집합. 두 세션에서 ExecID가 같다는 전제가 핵심인데, 실제로는 세션별로 ExecID를 다르게 매기는 거래소도 있을 수 있어(확인 필요) 그 경우 매칭 키를 따로 정의해야 한다.

</details>

3. **리스크 게이트.** 주문별 `worst()` 합 + 신규 수량이 한도 이하일 때만 허용하는 `allow_buy`를 구현하라. 취소 진행 중, 취소 확인 후, replace 진행 중(올림/내림)을 각각 테스트하라.

<details><summary>답</summary>

`OrdRisk::worst`. 취소 진행 중에는 줄지 않는다. 확인 후에는 체결분만. 진행 중 replace는 max(신, 구).

</details>

4. **수정 요청 합치기(coalescing).** 진행 중 수정이 있으면 새 희망값을 기억만 하고, 응답이 오면 **최신 값 하나만** 보낸다. 중간 값은 선로에 나가지 않아야 한다. 희망값이 이미 확정 값과 같으면 보내지 않는다.

<details><summary>답</summary>

`Coalescer`. 슬롯 하나(`held_`)면 충분하다. 큐를 쓰면 지연만 쌓인다. 이것이 Part A의 "요청 하나만" 규칙을 지키면서 호가를 빠르게 따라가는 표준적인 방법이다. 고빈도 호가에서는 cancel/replace 왕복 시간 동안의 중간 값을 전부 보내면 거래소의 메시지 제한에 걸린다.

</details>

5. **체결 취소(trade bust).** 거래소가 어떤 체결을 무효화하는 보고를 보낸다(ExecType H, ExecRefID로 원 체결 지정 — 기억, 확인 필요). bust가 원 체결보다 먼저 와도, 둘 다 중복돼도 결과가 같아야 한다.

<details><summary>답</summary>

bust한 ExecID의 **묘비(tombstone)**를 남긴다. 원 체결이 나중에 오면 묘비를 보고 무시한다. `Ledger::on_bust`. CRDT에서 삭제를 다루는 표준적인 방법과 같다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 practice.cpp -o pr && ./pr
```

```text
--- P5 체결 취소와 묘비
PASS  n == 120 && bad == 0
24/24 passed
```

## 10. 핵심 정리

- 경쟁은 스레드가 아니라 **비동기 메시지** 사이에 있다. 요청과 체결은 선 위에서 교차한다. TSan은 이 버그를 못 본다.
- ClOrdID는 요청의 ID다. 주문 = 체인. 모든 ID를 **보낼 때** 등록하고 옛 ID를 지우지 않는다.
- 상태를 저장하지 말고 사실에서 계산한다: 체결은 ExecID 위의 합, CumQty는 max, 링크는 앞으로만, 수량은 가장 뒤의 확정 링크.
- 포지션은 체결에서만 나온다. 종료된 주문의 늦은 체결도 체결이다.
- CancelReject는 요청의 실패일 뿐, 주문의 생사를 말하지 않는다.
- `Replaced.order_qty`가 권위. 요청 수량을 믿지 않는다.
- 리스크 상한은 취소 확인 전까지 안 줄이고, 진행 중 replace는 큰 쪽.
- 메시지 순서를 입력으로 만들면 seed 하나로 재현된다. 판정자 + coverage + mutation까지 있어야 "실패 0"에 뜻이 생긴다.

## 참고자료

- FIX Trading Community, FIX 4.4 Specification Volume 4 — Order State Change Matrices, Execution Report 설명. 태그 사전: [FIXimate](https://fiximate.fixtrading.org/) (이 레슨의 태그/의미는 작성자 기억. 실무는 거래소 RoE가 우선)
- Shapiro, Preguiça, Baquero, Zawirski. "A comprehensive study of Convergent and Commutative Replicated Data Types", INRIA RR-7506 (2011) — 교환·결합·멱등 병합
- Lamport. "Time, Clocks, and the Ordering of Events in a Distributed System", CACM 1978 — 인과와 동시성 (DIST-B03)
- NVM Express Base Specification, Abort 커맨드 — 같은 "요청 하나에 완료가 둘" (Abort 응답의 not-aborted 표시는 기억, 확인 필요)
- 같이 볼 레슨: TEST-A05 (결정론적 시뮬레이션), SIV-M09 (실스레드 스트레스), DIST-B03 (벡터 클록), DS-A01 (SPSC), QFIN-M05 · QFIN-M06 (세션, 사전 리스크)
