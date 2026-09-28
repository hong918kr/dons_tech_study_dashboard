# Module 9 — Speak Trading Systems (트레이딩 시스템 말하기)

> 출처: AlgoMonster Quant SWE Interview Prep, Module 9 (레슨 6개) · 한국어 개념 정리 노트
> 목표: 면접관이 설명 없이 쓰는 단어들 — **order book, matching engine, gateway, fill, position, tick-to-trade** — 을 그릴 수 있게. 그리고 **트레이딩 도메인은 새 엔지니어링이 아니라 새 어휘**라는 걸 보이기.

## 한눈에 보기 (5분 복습용)

```
 [피드 UDP] → Feed handler → Order book → Strategy → Gateway(risk) → [TCP] → Exchange matching engine
                                                                                   │
            ◄──────────── Position 갱신 ◄──────── Fill / Ack ◄──────────────────────┘
 ├──────────── tick-to-trade 시계 (여기까지만) ────────────┤
```

| 컴포넌트 | 본질 (엔지니어링) | 가져온 모듈 |
|---|---|---|
| Order book | 가격 정렬 레벨(각각 FIFO) + id→위치 해시. liquid 종목은 **가격 인덱스 평평한 배열** | M2 stable handle, M3 컨테이너, M5 캐시 |
| Matching engine | **single-writer** hot loop, 심볼별 샤딩 | M4, M8 |
| Gateway | cold path에서 계산한 한도 + **예측 가능한 분기**로 정수 비교 | M1, M5 |
| Feed→Strategy handoff | 같은 코어면 함수 호출, 다르면 SPSC ring. **hot chain은 한 코어 inline** | M4, M5, M6 |
| Feed handler | 거래소별 포맷 → **정규화된 내부 업데이트** | M7 |

**어휘 카드:** bid/ask · best bid/ask · **BBO** · spread · depth · price level · resting order · limit/market order · **marketable** · maker/taker · fill(execution) · partial fill · position · price improvement · slippage · IOC/FOK · tick-to-trade · colocation · market access rule · kill switch · cancel-on-disconnect

---

## 1. Trade Lifecycle Overview

**핵심:** 8개 모듈 동안 만든 기계(= how)가 봉사하는 것(= what): 전략이 업데이트로 뭘 하고, 보낸 주문은 어떻게 되나. **AAPL 업데이트 하나를 시스템 전체로 한 번 따라가며** 역마다 이름 붙이기.

### Order book — 전략이 읽는 그림
- 피드 메시지(새 매수 주문, 취소, 체결)는 그 자체로 잡음 → 조립하면 **order book**: AAPL의 모든 대기 매수·매도 주문을 가격순으로.
- **Resting order:** 시장에 앉아 기다리는, 아직 체결 안 된 주문.
- 매수 = **bid**, 최고 매수가 = **best bid** / 매도 = **ask(offer)**, 최저 매도가 = **best ask**. best bid < best ask, 차이 = **spread**. AAPL 같은 초유동 종목은 보통 1센트 (bid 190.10 / ask 190.11).
- 오더북 = 이 순간의 수급 → 정확하고 최신으로 유지하는 게 전략의 입력 전부.

### 결정과 주문
- **Strategy:** 오더북을 읽고 매수/매도/대기 결정. 결정 규칙 = 회사의 edge, 시스템 면접 범위 밖. 중요한 건 **출력 모양**: **order** = side(buy/sell) + quantity(예 100주) + 보통 price limit.
- **Limit order:** 수용 가능한 최악 가격 명시 ("190.11 이하로 100주 매수"), 지금 매칭 안 되면 **book에 대기**.
- **Market order:** 가격 없음, 지금 book이 주는 대로 즉시.
- **Marketable (limit) order:** 반대편 대기 주문과 즉시 체결 가능하게 가격 매긴 limit. (190.11 매도 대기 중에 190.11 매수 → 대기 없이 즉시 체결) — **대기 vs 체결** 구분이 다음 두 레슨의 동력.

### Gateway → Matching engine
- 주문은 바로 선으로 안 감 → 회사 자체 출구 검문소 **order gateway** → **pre-trade risk check** (크기 정상? 가격이 시장 근처? **position limit** — 보유 가능한 순 AAPL 한도 — 안?).
- 예의가 아니라 **규제**: 미국 주식은 SEC **market access rule**. hot path 한가운데 → 빠르게 만드는 게 엔지니어링 문제.
- 통과 후 거래소 wire format으로 직렬화 → **TCP** (피드는 UDP) — 주문 유실은 정확성 실패, 호가 하나 유실은 아님 (M7 분할의 구체화).
- 거래소의 **matching engine**: 매수·매도를 짝지음. **진짜(authoritative) 오더북** 보유 (내 로컬 사본은 이걸 미러링하려는 것). marketable 매수가 오면 최우선 매도와 짝 → **trade**.

### Fill과 그것이 바꾸는 것
- 거래소 응답: **ack** = 접수됨 / **fill (execution)** = 특정 수량이 특정 가격에 체결.
- 주문 하나가 여러 대기 주문과 매칭되면 **fill 여러 개** → Module 1의 `Order`가 `std::vector<Fill>`을 가진 이유 (주문 1 : 부분 체결 다수).
- 각 fill이 바꾸는 가장 중요한 숫자 = **position** (현재 보유 순 수량). 매수 fill 100 → +100, 매도 → −. 전략은 position과 book 뷰를 갱신하고 다음 업데이트로. **루프 닫힘.**
- 그 뒤 **청산·결제(clearing & settlement)** (다음 날 주식과 돈이 실제로 이동) = 비즈니스엔 필수, hot path엔 전무 → 코스 범위 밖.

### Tick-to-trade — 코스가 줄여온 그 숫자
- **Tick-to-trade** = 결정을 촉발한 시세 tick부터 **주문이 회사를 떠나 거래소로 향할 때까지.** 범위: 피드 핸들러 패킷 수신 → 파싱 → book 갱신 → 전략 결정 → 리스크 게이트웨이 → send.
- 캐시·할당·분기 예측·lock-free 핸드오프·kernel bypass 전부 이 구간을 줄이기 위한 것. 최고 회사들은 자기 박스 안에서 **수십~수백 ns**.
- **시계에 없는 것:** 돌아오는 fill, position 갱신, 그 이후. 정확성엔 중요하지만 **경주는 outbound 절반에서 승패**.
- 5개 역, 시계는 앞 3개 위에서 돈다.

- ✅ 체크포인트: best ask 190.11에서 190.11 limit 매수 100주 → **marketable** → 게이트웨이 통과 → 매칭 엔진이 190.11 대기 매도와 체결 → fill로 position +100. tick-to-trade = 피드 업데이트·결정·리스크 체크·send (돌아오는 fill은 **미포함**). (모든 limit이 먼저 대기하는 것도, 리스크 체크가 market order에만 적용되는 것도 아님)
- 🎤 "전략이 거래하려 할 때 무슨 일이?" → book 읽기 → side·수량·가격 결정 → 주문 → pre-trade 리스크 게이트웨이 → TCP → 거래소 매칭 엔진이 대기 주문과 짝 → fill → position 갱신.

---

## 2. Order Book API Shape

**핵심:** 분야 **최다 빈출 시스템 디자인 프롬프트** = "오더북을 만든다면 뭘로?" → 금융 옷을 입은 **컨테이너 선택 문제**. 필요한 도구는 이미 M2·M3·M5에.

### 양쪽과 최우선가
- 한 종목의 모든 대기 주문, 두 side: **bids**(매수), **asks/offers**(매도). 같은 가격의 주문들 = **price level** → book = 각 side의 레벨 스택.
- **가격은 정수**: AAPL 190.10 → `19010` (센트). 반올림 함정 회피(M10) + **정확한 키**(비교·인덱싱 가능).
- best bid = 최고 bid, best ask = 최저 ask, 합쳐서 **BBO** (best bid and offer), 차이 = **spread**, 레벨(또는 상위 몇 레벨)의 총 수량 = **depth**.

### 각 레벨은 큐
- 같은 가격이면 **먼저 온 주문이 우선** = **time priority** → 레벨 = **FIFO 큐** (새 주문은 뒤로, 앞부터 서비스).
- 레벨 간 정렬과 결합 → **price-time priority**: 더 좋은 가격 먼저, 같은 가격이면 더 이른 시간 먼저. book의 일 전체 = 이걸 정확히 유지.
- 데모 관찰: 레벨의 맨 앞을 취소하면 뒤의 주문으로 레벨 유지 / 그 가격의 **마지막 주문**을 취소하면 레벨 소멸 → 최우선가가 다음 레벨로 점프.

### 4가지 연산과 비용 목표 (빈도가 결정)
| 연산 | 동작 | 목표 |
|---|---|---|
| **Add** | 해당 가격 레벨 맨 뒤에 (없으면 레벨 생성) | O(1)~O(log n) |
| **Cancel** | **id만으로**, 어디 있든 제거 | **O(1)** — 가장 바쁜 쓰기! |
| **Modify** | 가격/수량 변경. **가격 변경·수량 증가 = cancel + 새 add** (줄에서 자리 잃고 맨 뒤로). 수량 감소는 제자리 가능 | — |
| **Query** | BBO, depth | BBO는 **매 업데이트마다 → O(1)** |
- 😮 처음 들으면 놀라는 사실: **대기 주문 대부분은 체결이 아니라 취소된다** (회사들이 시장 따라 호가를 계속 올렸다 내림) → cancel이 가장 바쁜 쓰기 → O(n) 탐색이면 재앙.

### 설계: 세 요구사항이 세 방향으로 당긴다
1. **가격순 정렬** (BBO = 각 side 꼭대기, 매칭은 가격순으로 book을 걸음)
2. **id로 O(1) 검색** (cancel은 가격을 모른 채 id만 가져옴)
3. **캐시 친화** (매 tick 읽힘 → M5의 연속 레이아웃)
- 같은 데이터에 대한 서로 다른 세 접근 패턴.

**id 요구사항 → 해시 인덱스** `std::unordered_map<OrderId, Location>` = **M2 stable handle 패턴**. id는 절대 안 변하는 안정 핸들, map이 현재 위치로 해석. Location이 **intrusive linked list** 노드를 가리키면 unlink도 O(1), shift 없음.

**가격 정렬 요구사항 → M3의 3자 트레이드 재등장:**
```cpp
// 옵션 1: 가격 키 균형 트리. 정렬 순회 공짜, 전부 O(log n).
//   노드 기반이라 레벨마다 캐시 미스 (M3). 화이트보드 정답.
std::map<int, Level> asks;              // best ask = asks.begin()

// 옵션 2: 레벨의 정렬 vector. 연속·캐시 친화, O(log n) 탐색,
//   중간 삽입 시 shift.
std::vector<Level> asks;                // best ask = asks.front()

// 옵션 3 (프로의 수): 가격 인덱스 평평한 배열.
//   가격 = 유한 밴드 안의 이산 tick → 가격이 곧 슬롯.
//   add/cancel/BBO 전부 O(1), 완전 연속. 대가 = 유한 가격 범위.
std::array<Level, PRICE_SLOTS> asks;    // level = asks[(price - base) / tick]
```
- 옵션 3 = M3의 "키가 작고 조밀한 정수면 평평한 배열이 모든 map을 이긴다". AAPL은 1센트 tick의 좁은 밴드 → 수천 슬롯이면 활성 book 전체 커버. 접근 = 뺄셈 + 인덱스, 레벨들이 연속이라 prefetcher 친화, **비어 있지 않은 최우선 슬롯을 증분 추적**해 BBO O(1). 대가 = 유한 가격 범위 + 빈 레벨 메모리 → liquid 종목엔 헐값.

### 한 호흡 요약
> **가격 정렬 레벨 컬렉션(각 레벨 FIFO) + order id → 위치 해시 인덱스.** 정렬 컬렉션을 **평평한 가격 인덱스 배열**로 바꾸면 모든 연산이 캐시 친화 메모리 위 O(1). 각 절이 M2·M3·M5 교훈에 트레이딩 라벨을 붙인 것.

- ✅ 체크포인트: cancel을 O(1)로 (id만 옴, 가장 빈번) → **order id → 위치(side, 레벨, 노드) 해시 인덱스**, cancel = lookup 1번 + unlink. = M2 stable handle. (정렬 vector 이진 탐색 O(log n)이나 최우선가부터 스캔 ❌)
- 🎤 "std::map of levels보다 잘할 수 있나?" → liquid 종목이면 예. 가격은 유한 밴드의 조밀한 정수 tick → 가격 인덱스 평평한 배열로 O(1) + 연속 메모리. M3의 key-indexed array 트릭.

---

## 3. Matching Engine Concepts

**핵심:** 오더북은 무언가가 교차하기 전까진 가만히 있다. **Matching engine**이 움직이게 한다: 들어온 주문을 받아 대기 book을 걸으며 수량을 체결로 바꿈. 면접관 애용 — 방금 만든 자료구조 위의 깔끔한 알고리즘 + 동시성 답은 이미 M4에서 증명.

### Aggressive vs Passive
| | Passive | Aggressive |
|---|---|---|
| 조건 | 현재 시장과 교차 안 함 | 반대편 대기 주문과 가격이 교차 (**marketable**) |
| 행동 | book에 **대기** | 이미 있는 것과 **즉시 체결** |
| 유동성 | **공급**(make) → **maker** | **소비**(take) → **taker** |
- 둘이 만나면 거래 → 양쪽 모두 **fill**.
- **체결 가격 = 대기(passive) 주문의 가격** (먼저 있었으니 조건을 정함), 들어온 주문의 limit 아님.
- 예: 19012까지 낼 의향의 매수가 19011 대기 매도를 만나면 → **19011**에 체결, 매수자는 1센트 절약 = **price improvement**, 항상 **aggressor**에게.

### Book 걷기 (price-time priority를 단계별로 실행)
1. 반대편 **최우선가**에서 시작
2. 그 레벨 **맨 앞 주문**(가장 오래 기다린)과 체결
3. 들어온 주문 수량이 남고 레벨이 비면 → **다음 가격 레벨**로
4. 멈춤: 들어온 주문 소진 **또는** 다음 대기 가격이 더 이상 limit과 교차 안 함
```cpp
// 개념 스케치 (노트용): aggressive BUY
while (incoming.qty > 0 && !asks.empty() && asks.best_price() <= incoming.limit) {
    Level& lvl = asks.best_level();
    Order& resting = lvl.front();                         // time priority
    int q = std::min(incoming.qty, resting.qty);
    emit_fill(incoming, resting, lvl.price, q);           // 가격 = resting 가격
    incoming.qty -= q; resting.qty -= q;
    if (resting.qty == 0) lvl.pop_front();
    if (lvl.empty()) asks.remove_best_level();            // 다음 레벨로
}
// 남은 수량 처리는 주문 유형이 결정 (아래)
```
- 결과 ①: 들어온 주문 하나가 **fill 여러 개** (소비한 대기 주문마다 하나) → `Order`가 `std::vector<Fill>`을 가진 이유.
- 결과 ②: 최우선 레벨을 다 먹을 만큼 큰 주문은 **더 나쁜 가격으로 계속** → 깊이를 쓸면(sweep) 평균가가 top of book보다 나빠짐. 최우선가와 실현 평균가의 차이 = **slippage** (앞단 book이 가진 것보다 더 큰 사이즈를 요구한 직접 비용).
- 데모: 250주 매수가 대기 매도 3개를 만나 → 최우선가의 가장 오래된 두 주문을 먹고 → 나머지는 한 레벨 위로 → 평균 **19011.20** (best ask 19011보다 위, 마지막 50주가 19012까지 올라가서) = slippage 한 숫자.

### 주문 유형 = 남은 수량을 어떻게 하나
| 유형 | 잔량 처리 |
|---|---|
| **Limit** | limit 이하로 체결 가능한 만큼 → **나머지는 새 passive 주문으로 book에 대기** |
| **Market** | 가격 limit 없음, 채워질 때까지 book이 주는 대로 (slippage 감수), 못 채운 건 **취소** (대기 안 함) |
| **IOC** (immediate-or-cancel) | 이 순간 가능한 만큼, 나머지 **즉시 취소**, 절대 대기 안 함 |
| **FOK** (fill-or-kill) | **전부 아니면 전무**: 전체 수량을 한 번에 못 하면 아무것도 안 함 |
- 모두 **같은 walk**, 루프의 **마지막 줄**(걸음이 멈췄을 때 남은 수량 처리)만 다름.

### 왜 단일 스레드이고, 그게 왜 괜찮은가
- 속도보다 위의 하드 제약: **결정적이고 공정해야 함.** 주어진 순서로 도착한 두 주문은 **매번** 그 순서로 매칭돼야 함 — 아니면 price-time priority는 거짓말.
- 여러 스레드가 한 book을 갱신하게 하면 M4가 경고한 인터리빙이 **누가 거래하는지**를 결정 → 승자는 스케줄러가 우연히 편애한 쪽.
- 그래서 **한 종목 엔진은 도착 순서대로 한 스레드에서 하나씩.** 성능 희생처럼 들리지만 M4가 증명한 것: **single writer엔 락·atomic·조정이 필요 없다** (경쟁할 두 번째 writer가 없음). **가장 결정적인 설계 = 가장 싼 올바른 설계.** book은 hot → 루프 안에서 모든 hot-path 습관(할당 없음, 캐시 친화 레벨, 예측 가능한 분기) 여전히 적용, 동시성 모델만 가장 단순.
- **스케일 = 심볼별 샤딩** (M8 기법): AAPL은 엔진 하나, MSFT는 다른 엔진, 같은 book을 절대 안 만짐 → 공유 상태 없이 다른 코어/머신에서 병렬. 합칠 것도 없음. **Book 하나 = writer 하나, book 여럿 = 코어 여럿.**

- ✅ 체크포인트: 바쁜 AAPL 엔진을 빠르게 하려고 4스레드가 **업데이트마다 mutex**로 AAPL book을 병렬 갱신 → 가장 강한 반론: 모든 갱신을 mutex 하나 뒤로 직렬화하니 실질 병렬성도 없고, 더 나쁘게는 각 락의 승자가 스케줄러 의존 → **price-time 공정성 붕괴**. 정답: 한 book = 한 스레드·락 없음, 병렬성은 **심볼 샤딩**으로.
- 🎤 "거래 가격은 어디서?" → 대기 주문이 가격을 정함. aggressor의 limit이 더 좋았으면 price improvement를 받음. top 레벨을 넘어 쓸면 평균이 나빠짐 = slippage.
- 🎤 "market vs limit 차이?" → 같은 walk, 잔량 처리만 다름. limit은 대기, market은 있는 걸 먹고 나머지 취소, IOC는 즉시 취소, FOK는 전부 아니면 전무.

---

## 4. Order Gateway & Risk Checks

**핵심 질문 (토이 루프만 최적화한 사람 vs 실제 시스템을 출하한 사람을 가르는):** "이렇게 빠른 기계가 **스스로를 파괴하지 않게** 어떻게 하나?"

### 모든 주문이 통과하는 검문소
- **Order gateway** = 회사 자체 outbound 검문소, **pre-trade risk check** 실행 (주문이 건물을 떠나기 전 빠른 정상성 테스트).
- 지연 아끼려고 건너뛸 수 있는 예의가 **아님**: 미국 주식은 **SEC market access rule** — 거래소 접근권을 가진 회사는 리스크 통제를 **pre-trade로, 회사의 직접·배타적 통제 하에** 적용해야 함. send 후에 볼트로 붙이거나 외주 불가 → 좋든 싫든 **tick-to-trade 경로 위**.
- 나쁜 주문 하나는 작은 문제가 아님: 버그 있는 알고리즘은 초당 수천 주문, 가격·크기 틀린 주문은 사람이 알아채기 전에 막대한 손실. 게이트웨이 = **소프트웨어 버그와 시장 사이 마지막 자동 방어선**.

### 게이트가 검사하는 것
| 검사 | 종류 | 잡는 것 |
|---|---|---|
| **Max order size** | 주문별 | **fat-finger**: 평소 300주인데 5,000주 → 거의 확실히 버그 |
| **Price collar** | 주문별 | 가격이 현재 시장에서 너무 멀면 (시장보다 훨씬 높은 매수 = 오타 or 오작동) |
| **Position limit** | 주문별 | 이 주문이 순 보유를 long/short 한도 너머로 가져가면 차단 |
| **Rate limit / throttle** | 상태 기반 | 초당 주문 수 상한 → 폭주 루프가 거래소를 flood하고 거래소 한도를 건드리는 것 방지 |
| **Kill switch** | 상태 기반 | 사람이나 자동 모니터가 **모든 outbound 주문 정지 + 대기 주문 일괄 취소** (비상 브레이크) |
| **Cancel-on-disconnect** | 상태 기반 | 전략이 죽으면 대기 주문이 관리 없이 남지 않게 |

### 왜 여기선 안전이 싼가
- 검사의 실체 = **정수 비교**. 할당·락·syscall·이전 주문이 데우지 않은 메모리 접근 **없음**.
- 한도는 **cold path**에서 계산 (사람이 설정할 때, fill이 position을 갱신할 때) → hot path는 **읽기만**. 리스크 게이트 전체 = 비교 몇 번, 네트워크를 건너며 쓸 수백 ns 대비 **수 ns**.
- 두 번째 이유 = **M5 분기 예측기**: 압도적 다수 주문이 모든 검사를 **통과**(전략이 보통 버그가 없으니) → 모든 분기가 "pass"로 예측되고 거의 항상 맞음 → 파이프라인 flush 없이 통과. **거의 항상 같은 쪽으로 가는 검사 = 하드웨어가 돈을 안 받는 검사.**
- 사람들이 예상하는 안전 ↔ 속도의 긴장은 **사실상 없다**: 게이트는 hot path에 대한 세금이 아니라 **정확성을 향한 hot path 자신의 규율**. 주문을 빠르게 하는 모든 기법(cold에서 미리 계산, hot에서 읽기, 데이터 작고 hot하게, 분기 예측 가능하게) = 검사를 빠르게 하는 기법. **안전한 시스템과 빠른 시스템 중 고르지 않는다 — 안전한 검사를 빠른 방식으로 짓는다.**

### 와이어로 직렬화
- 게이트 통과 후 거래소 **order-entry 포맷**으로 직렬화 → TCP send = **M7 피드 핸들러를 거꾸로**: 피드 핸들러는 수신 바이트에 struct를 겨눠 제자리 읽기(zero-copy parse), 게이트웨이는 packed 버퍼에 필드를 써서 소켓에. **같은 wire-layout 규율, 반대 방향: 들어올 땐 parse, 나갈 땐 serialize.**
- TCP인 이유 = M7: 주문 유실·순서 뒤바뀜은 재전송 비용을 낼 가치가 있는 정확성 실패.

- ✅ 체크포인트: "tick-to-trade에 지연을 더해서 pre-trade 리스크 체크를 게이트웨이에서 뺐다" → 엔지니어링: 미리 계산한 한도와의 정수 비교 + 거의 항상 통과 → 분기 예측으로 거의 공짜 → 빼도 절약이 적음. 정확성: market access rule로 **의무**이고 폭주 알고리즘에 대한 마지막 방어선 → 불허 + 위험. (야간 배치로 옮기기, 결과 캐싱 ❌)

---

## 5. Market Data → Strategy Handoff

**핵심:** 4개 역(피드 핸들러, 오더북, 전략, 게이트웨이)을 배선. 모듈의 시스템 디자인 절반 — **M4·M6의 동시성·OS 교훈이 기계 전체의 모양을 결정.**

### 컴포넌트로 본 파이프라인
- 피드 핸들러가 NIC를 **busy-poll** → 패킷 frame·parse → **book에 적용**(tick마다 최신 유지) → 전략이 book 읽고 결정 → 행동 시 게이트웨이가 리스크 체크 + send. 시세가 한쪽으로 들어와 주문이 다른 쪽으로 나감, tick-to-trade 시계가 체인 전체에 걸쳐 돔.
- 각 단계의 깨끗한 인터페이스: 피드 핸들러 출력 = **정규화된 book 업데이트 스트림**, 전략 출력 = **주문 스트림**, 게이트웨이 출력 = **소켓 위 바이트**. 아무도 남의 내부를 건드리지 않음.

### Book은 누가 소유하나 (배선 대부분을 결정)
- **M4 single-writer 규칙:** 피드 핸들러가 **유일한 writer** (book = 피드의 투영), 전략은 reader → book에 **락 불필요** (매칭 엔진과 같은 이유).
- reader가 writer의 갱신을 보는 방법:
  | 배치 | 핸드오프 |
  |---|---|
  | **같은 스레드** | 그냥 **함수 호출**: 패킷 parse → book 갱신 → 전략 호출 |
  | **다른 코어** | release/acquire 발행·구독, 또는 피드 핸들러가 **SPSC ring**으로 업데이트 push → 전략이 자기 뷰 재구성 |
- 어느 쪽이든 동기화 예산 = 원자 인덱스 갱신 몇 번, **절대 락도 커널도 아님.**

### "코어를 몇 개 쓰나"가 진짜 질문
- 유혹: 단계마다 코어 하나 + 전부 ring으로 연결. **hot path엔 틀린 본능.**
- 모든 코어 간 핸드오프엔 비용: ring push/pop 수십 ns + 데이터 실은 캐시 라인이 생산자 코어 → 소비자 코어로 이동 (**M5 MESI 트래픽**).
- 300ns tick-to-trade를 4코어 4단계로 쪼개면 → 캐시 라인 전송 3번 + 핸드오프 3번 추가, 절약 0 — **단계들이 어차피 순차** (book 갱신이 끝나야 전략이 시작). **순차 작업을 코어로 나누면 레이턴시가 더해지지 숨겨지지 않는다.**
- 최저 레이턴시 설계는 반대: tick-to-trade 체인 전체를 **가능한 적은 코어**, 흔히 **핀닝·격리된 코어 하나**에서 receive → parse → book 갱신 → 결정 → 리스크 체크 → send를 **inline**, 핸드오프 0. 코어는 블록·스위치·폴트 없이 hot path를 절대 떠나지 않음 = **M6 교훈 전부를 한 스레드에 조립.**
- **그래서 ring의 진짜 역할 = hot path 쪼개기가 아니라 cold 일을 hot 코어 밖으로:** 로깅, 모니터링, 느린 분석, **gap recovery**를 ring으로 다른 코어에 push → tick-to-trade 체인에서 사이클·캐시 라인을 절대 안 뺏음. M4 producer-consumer 분리는 **hot과 cold를 가르지, hot과 hot을 가르지 않는다.**

### 단계가 자기 코어를 얻는 경우 (의도적 거래, 기본값 아님)
- 진짜로 뒤처질 만큼 느릴 때: µs 동안 생각하는 무거운 전략 → 자기 코어에서 돌려 피드 핸들러를 막지 않게 (핸드오프 비용을 내고 decoupling을 삼) = M7 backpressure 설계.
- **장애 격리**가 가치 있을 때: 공유 메모리 ring으로 연결된 **별도 프로세스** → 하나가 크래시해도 나머지 무사 (M6 공유 메모리 셋업 비용).

### 피드 핸들러 하나, 거래소 여럿 — 정규화
- 회사는 여러 거래소에서 거래, 거래소마다 wire format이 다름 → 그 다양성이 전략까지 오면 모든 전략이 모든 거래소를 이해해야 함.
- 피드 핸들러가 흡수: 각 거래소 포맷을 파싱해 **어디서 왔든 똑같이 생긴 정규화 내부 업데이트** 하나를 내보냄 → 전략은 내부 포맷 대상으로 **한 번만** 작성, **거래소 불문**. 피드 핸들러 = 번역 계층 → book의 자연스러운 집.

### Colocation
- 이 모든 것도 내 머신 ↔ 거래소 사이 **물리적** 레이턴시(거리로 결정)는 못 바꿈 → **colocation**: 서버를 거래소 자체 데이터센터에 두어 신호가 마일이 아니라 미터를 이동.
- **Colocation = 외부 레이턴시를 삼, 이 레슨의 아키텍처 = 설계하는 내부 레이턴시. 둘이 합쳐 tick-to-trade.**

- ✅ 체크포인트: 피드 핸들러·전략·게이트웨이를 lock-free ring으로 연결된 코어 3개에 → 결함: 단계가 순차라 3코어가 병렬로 돌지 않음, ring 핸드오프 2번 + 코어 간 캐시 라인 전송 2번만 추가. hot chain은 **한 코어 inline**, 진짜 뒤처질 만큼 느린 단계나 **로깅 같은 cold 일 격리**에만 분리. (mutex가 ring보다 빠르다거나, 전력만의 문제 ❌)

---

## 6. C++ HFT Pattern Map

**핵심:** 면접관은 결국 코스가 이름 붙이지 않은 걸 묻는다 (throttling 레이어, position 서비스, smart order router, 본 적 없는 book). **본 적 없어도 된다** — 전부 이미 가진 몇 개의 패턴으로 만들어지고, 어느 패턴인지 찾는 신뢰할 수 있는 방법이 있다. = **코스를 방법으로 접은 것.**

### 5가지 질문 → 어떤 프롬프트든 체크리스트로
| # | 질문 | 모듈 |
|---|---|---|
| 1 | **Hot path가 뭔가?** 매 tick 도는 코드(비용이 중요한 유일한 코드)를 찾고, 시작 시/하루 한 번 도는 cold path와 분리 | M1 cost model |
| 2 | **자료구조는? 접근 패턴·캐시에 맞나?** 컨테이너를 고르기 전에 데이터를 어떻게 읽고 쓰는지 말하고, Big-O가 아니라 **locality**로 선택 | M2, M3, M5 |
| 3 | **동시성 모델은?** 누가 쓰고 누가 읽고 어떻게 넘기나. **single writer**(락 불필요) 선호, lock-free ring 핸드오프, 독립 데이터를 코어에 **샤딩**해 스케일 | M4 (+ M8 scaling test) |
| 4 | **없앨 수 있는 경계 횡단은?** 모든 할당·복사·커널 횡단·네트워크 hop = 때로 설계로 없앨 수 있는 레이턴시 | M6, M7 |
| 5 | **어떻게 측정하나?** 뭘 바꾸기 전에 옳음을 증명할 숫자를 정하고, **평균이 아닌 percentile**로 | M8 |

### 패턴 맵 (조회표) — 컴포넌트 → 꺼낼 패턴
> (원문 도식을 레슨 내용 기반으로 재구성)

| 면접관이 말하는 것 | 꺼낼 패턴 |
|---|---|
| **Order book** | 가격 인덱스 평평한 배열 / 정렬 레벨 + FIFO 레벨 + id→위치 해시(stable handle), 정수 가격, 연속 메모리 |
| **Matching engine** | single-writer hot loop, 락 없음, 심볼 샤딩, 할당 없는 walk, `vector<Fill>` 사전 reserve |
| **Order gateway / risk** | cold path에서 계산한 한도, 정수 비교, 예측 가능한 분기, rate limit, kill switch, TCP 직렬화(피드 핸들러의 역) |
| **Feed handler** | busy-poll non-blocking recv / kernel bypass, 고정 크기 framing, zero-copy parse, seq gap 감지, 정규화 |
| **Handoff / threading** | 한 코어 inline, SPSC ring은 cold 일 오프로드용, release/acquire, `alignas(64)` |
| **Position / 카운터** | 스레드별 상태 + 병합, 정수 수량, single writer |
| **메모리** | 시작 시 pool/arena, pre-fault, `mlock`, huge page, `reserve` |
| **측정** | 단계별 HDR histogram, p99.9, 트리거 캡처, `perf stat`/`c2c` |
- 이 맵이 보여주는 것: **트레이딩 도메인은 어휘를 더했지 새 엔지니어링을 더하지 않았다.** AAPL 오더북 = 금융 라벨 붙은 컨테이너 선택, 매칭 엔진 = single-writer hot loop, 게이트웨이 = 미리 계산한 한도 + 예측 가능한 분기. 8개 모듈 동안 엔지니어링을 정확히 배운 이유 = 도메인이 거기로 환원되도록.

### 강한 답의 모양 (코스가 두 번 만든 것: M1 checkpoint, M8 디버깅 스토리)
1. **Big-O로 시작** (공용어, 문제 프레이밍) → 그리고 **여기선 Big-O가 답이 아니라고** 분명히 말함
2. **cost-model 체크리스트를 소리 내어:** 데이터가 어디 사나, 뭐가 할당하나, 뭐가 복사하나, 분기·호출 비용, 뭐가 커널을 건너나 → 각 비용 + 첫 처방
3. **측정으로:** percentile로 (이 도메인에선 **꼬리가 곧 제품**, 평균은 그걸 숨김)
4. 변경 제안 시: **하나만** 바꾸고, 불만이었던 숫자로 검증하고, **변경의 비용**을 명명
- = 면접관이 "어떻게 생각하는지 보고 싶다"고 할 때 의미하는 것. M1의 hot loop든 화이트보드에서 발명 중인 smart order router든 같은 호.

- ✅ 체크포인트: 처음 보는 프롬프트 — "500개 심볼의 상위 5개 가격 레벨을 매 tick 최저 레이턴시로 갱신하는 서비스" → 강한 답: hot path = tick당 book 갱신 → 할당 없게. 심볼마다 **가격 인덱스 레벨 배열** book → 상위 5개 O(1) 읽기 (M3, M5). **book당 single writer, 심볼로 코어 샤딩** → 500 book 락 없이 병렬 (M4, M8). 갱신에서 커널·복사 회피 (M6, M7). **tick당 갱신 레이턴시 p99.9**로 증명, 평균 아님 (M8). (전역 `std::map` + mutex 하나 + 평균 보고 ❌ / `std::list` + 공유 락 ❌)
- 🎤 "본 적 없는 시스템에 어떻게 접근?" → hot path 찾기 → 접근 패턴·캐시에 맞는 자료구조 → 가능하면 single writer 동시성 모델 → 할당·커널 횡단 제거 → 뭘 바꾸기 전에 증명할 percentile 결정.
- 🎤 "가장 중요한 습관 하나?" → **최적화 전에 측정하고, 꼬리로 시작한다.** 이 도메인에서 p99.9가 제품이고 평균은 문제를 숨기는 숫자.

---

## 용어 사전

| 용어 | 한 줄 정의 |
|---|---|
| Order book | 한 종목의 모든 대기 매수·매도 주문, 가격순 |
| Bid / Ask(offer) | 매수 / 매도 주문 |
| BBO | best bid and offer (최고 매수가 + 최저 매도가) |
| Spread / depth | best ask − best bid / 레벨(들)의 총 대기 수량 |
| Price level | 같은 가격 주문들의 FIFO 큐 |
| Price-time priority | 더 좋은 가격 먼저, 같은 가격이면 먼저 온 순 |
| Resting order | book에서 대기 중인 미체결 주문 |
| Limit / market order | 최악 가격 명시(대기 가능) / 가격 없이 즉시 |
| Marketable | 반대편과 즉시 체결 가능한 가격의 주문 |
| Passive(maker) / aggressive(taker) | 유동성 공급 / 소비 |
| Fill (execution) | 특정 수량이 특정 가격에 체결된 보고 |
| Ack | 주문 접수 확인 |
| Position | 보유 순 수량 |
| Price improvement | aggressor가 자기 limit보다 좋은 가격에 체결 |
| Slippage | 최우선가와 실현 평균가의 차이 |
| IOC / FOK | 즉시 가능한 만큼 후 취소 / 전부 아니면 전무 |
| Matching engine | 거래소의 매수·매도 짝짓기 프로그램 (authoritative book 보유) |
| Order gateway | 회사의 outbound 검문소, pre-trade risk check |
| Market access rule | 미국 SEC 규정: pre-trade 리스크 통제 의무 |
| Fat-finger / price collar | 비정상 크기 주문 / 시장가 대비 가격 범위 제한 |
| Kill switch / cancel-on-disconnect | 전체 정지+일괄 취소 / 연결 끊기면 대기 주문 취소 |
| Tick-to-trade | 촉발 tick 수신 → 주문이 회사를 떠날 때까지 |
| Normalization | 거래소별 포맷 → 단일 내부 업데이트 형식 |
| Colocation | 서버를 거래소 데이터센터에 배치 |
| Clearing & settlement | 체결 후 주식·돈의 실제 이전 (hot path 밖) |
