# 04. IPC & 자원 예산 분배 (IPC & Budget Arbitration) — Q1–Q6 📡

> Neros "Senior Firmware Engineer, **Platform**" 의 핵심 주제.
> JD 문장: *공통 런타임(logging / telemetry / **IPC** / configuration)을 소유하고,
> **서브시스템이 같은 예산(compute·memory·bandwidth)을 두고 경쟁할 때 트레이드오프를 주도**한다.*
> 즉 이 파일은 "자료구조 문제"가 아니라 **플랫폼 정책을 코드로 쓰는 문제**다.
>
> 빌드: `cc -std=c11 -Wall -Wextra -O0 -g solutions/04_ipc_budget.c -o /tmp/n04 && /tmp/n04`
> (또는 `make prob N=04_ipc_budget`)

---

## 왜 이 주제가 Neros에서 중요한가

- FPV 드론의 무선 링크는 **좁고, 재밍(jamming)당하고, 거리에 따라 붕괴**한다. 비행 제어,
  자율주행, 텔레메트리, 영상, 로깅이 **같은 파이프**를 두고 싸운다.
- 각 팀이 자기 코드에 `if (link_bad) skip()` 을 흩뿌리면 정책이 코드베이스 전체에 흩어진다.
  → 플랫폼 런타임이 **하나의 중재자(arbiter)** 로 min 보장 / 우선순위 분배 / 열화 정책을 들고 있어야 한다.
- MCU 펌웨어는 **malloc 없이** 돈다. 메시지도, 버퍼도 전부 **고정 크기 풀 + 고정 용량 큐**.
- 면접에서 이 주제는 "구현할 줄 아나"보다 **"왜 이 정책인가, 실패하면 무엇이 먼저 죽나"** 를 묻는다.

---

## 개념 정리

### 1) malloc 을 왜 안 쓰나

| 문제 | 설명 |
|------|------|
| **파편화(fragmentation)** | 오래 도는 시스템에서 free 된 조각이 흩어져, 총 여유는 충분한데 큰 블록 할당이 실패한다. 비행 중 실패 = 사고. |
| **비결정적 지연** | `malloc` 은 free list 탐색/병합 때문에 호출마다 시간이 다르다 → 제어 루프 **WCET** 보장 불가. |
| **실패 경로 폭증** | 모든 호출이 NULL 을 돌려줄 수 있어, 검사 안 한 곳이 곧 크래시. |
| **가시성 없음** | "지금 힙을 누가 얼마나 쓰나"를 런타임이 답할 수 없다. 풀은 `used/nblocks` 로 즉답. |

→ 대안: **고정 크기 block pool**. 크기가 한 종류라 파편화가 원리적으로 없고, alloc/free 가
**O(1) 상수 시간**, 최대 사용량이 **컴파일 타임에 확정**된다 (링커 맵에서 바로 보인다).

### 2) pool vs heap

| | heap (`malloc`) | fixed block pool |
|---|---|---|
| 블록 크기 | 임의 | 한 종류 (또는 size class 별 풀 여러 개) |
| 시간 복잡도 | 가변 | **O(1)** alloc / O(1) free |
| 파편화 | 있음 | **없음** (외부 파편화 0, 내부 파편화만) |
| 실패 | 예측 불가 시점 | **고갈 시점만**, 즉시 NULL |
| 진단 | 어려움 | `used` 하이워터마크로 예산 관리 |

### 3) intrusive free list — 메타데이터 0 바이트

빈 블록은 **어차피 내용이 무의미**하므로, 그 앞 `sizeof(void*)` 바이트에 "다음 빈 블록 주소"를
써 둔다. 별도 비트맵/링크 배열이 필요 없다.

```c
// alloc: head pop
void *blk = p->free_head;
memcpy(&p->free_head, blk, sizeof(void *));   // free_head = blk->next
// free: head push
memcpy(ptr, &p->free_head, sizeof(void *));   // ptr->next = free_head
p->free_head = ptr;
```

- `memcpy` 로 읽고 쓰는 이유: 블록이 `void*` 정렬을 만족하지 않을 수도 있고, 바이트 배열을
  포인터로 직접 캐스팅하면 **strict aliasing / 정렬 UB** 가 된다.
- 전제: `block_size >= sizeof(void*)`. 그리고 저장소는 `_Alignas` 로 최소 포인터 정렬.
- **소유권 검사**가 핵심 안전장치: `[base, base + block*n)` 밖이거나 `(ptr-base) % block != 0`
  이면 free list 에 넣으면 **안 된다**. 넣는 순간 풀 전체가 조용히 오염되고, 나중에 엉뚱한
  주소를 alloc 이 돌려준다 (디버깅 지옥). 거부하고 **카운터만 올린다**.

### 4) SPSC 큐가 락이 필요 없는 이유

- 생산자만 `head` 를 **쓰고**, 소비자만 `tail` 을 **쓴다**. 상대 인덱스는 **읽기만** 한다
  → 같은 변수에 대한 **write-write 경합이 존재하지 않는다** → 뮤텍스 불필요.
- `count` 필드를 두면 양쪽이 모두 쓰게 되어 경합이 생긴다 → 그래서 `count = head - tail`
  (부호없는 뺄셈, 랩어라운드 안전) 로 **계산**한다.
- 순서 규칙: **데이터 먼저 쓰고 → 인덱스를 나중에 공개**. pop 은 반대로 인덱스를 먼저 읽고
  → 데이터를 읽는다.
- 이식성: 싱글코어 ISR↔main 은 `volatile` + 위 순서로 전통적으로 충분. **멀티코어/공격적
  최적화 환경**에서는 `head/tail` 을 `_Atomic` 으로 두고 **release store / acquire load** 가
  필요하다 (그래야 "데이터가 인덱스보다 먼저 보인다"가 보장된다).
- 생산자·소비자가 여럿이면 SPSC 전제가 깨진다 → mutex, 또는 CAS 기반 MPMC 링(훨씬 어렵다).

### 5) zero-copy vs copy

| | copy (이 파일의 방식) | zero-copy (포인터 전달) |
|---|---|---|
| 비용 | 메시지당 `memcpy` (여기선 18바이트, 무시할 수준) | 복사 0 |
| 소유권 | **단순**: push 하는 순간 송신자 버퍼는 자유 | 수신자가 다 쓸 때까지 **해제 금지** |
| 수명 | 문제 없음 | use-after-free / 이중 free 위험 |
| 큰 payload | 나쁨 (영상 프레임을 복사할 순 없다) | **필수** |

실무 절충: **작은 제어 메시지는 값 복사**(단순·안전), **큰 버퍼는 pool 블록 포인터를 큐로
전달**(zero-copy) 하고 소비자가 `pool_free` 로 반납 — 소유권이 "큐를 따라 이동"한다고 문서화.

### 6) token bucket vs leaky bucket

| | token bucket | leaky bucket |
|---|---|---|
| 모델 | 토큰이 rate 로 쌓이고, 전송 시 소비 | 큐에서 **고정 rate 로 유출** |
| burst | **허용** (최대 burst 만큼 몰아 보내기 가능) | 허용 안 함 (출력이 항상 평탄) |
| 용도 | 로그/텔레메트리 (가끔 몰리는 게 정상) | 링크 shaping (출력 지터 최소화) |

정수 구현의 핵심: **토큰을 1000배(milli-token)로 저장**한다.
`rate [tokens/s] == rate [milli-tokens/ms]` 이므로 나눗셈 없이 `tokens += elapsed_ms * rate`.
이렇게 하면 1ms 간격으로 호출해도 **소수 토큰이 버려지지 않는다**(`elapsed*rate/1000` 로
매번 정수 나눗셈하면 잔여분이 전부 증발해 영원히 충전이 안 되는 고전 버그).

```c
uint32_t elapsed = now_ms - tb->last_ms;     // unsigned -> tick wraparound 안전
tb->tokens_milli += (uint64_t)elapsed * tb->rate;
if (tb->tokens_milli > cap) tb->tokens_milli = cap;   // burst 상한
```

### 7) priority starvation 과 min 보장

- 순수 strict priority 로 나누면 **상위 스트림이 want 를 다 먹고 하위는 0** → 기아(starvation).
  텔레메트리가 완전히 끊기면 지상국은 드론이 죽었는지 알 수 없다.
- 그래서 **2단계 정책**: ① prio 순으로 **min_bps 먼저 예약**(생존선 보장) → ② 남은 것을
  다시 prio 순으로 **want 까지** 배분.
- 예산이 모든 min 을 못 채우면? **컷오프**한다: 못 채우는 지점 이후(더 낮은 우선순위)는 전부 0.
  "싼 하위 스트림이 비어 있는 틈에 끼어드는" **우선순위 역전**을 막기 위함이다.
- 정렬은 **stable**(같은 prio면 원래 순서 유지) → 같은 입력에 항상 같은 출력. 결정성은
  비행 로그 재현/인증에서 필수.

### 8) graceful degradation

- 링크가 공칭의 10% 로 붕괴하면 "전부 조금씩"이 아니라 **"중요한 것만 온전히"** 가 정답이다.
  절반짜리 영상 + 절반짜리 제어 = 둘 다 쓸모없음.
- 죽는 순서를 **미리 정해 둔다**: verbose log → 영상 → 고빈도 텔레메트리 → 저빈도 하트비트 →
  비행 제어(절대 불가침).
- Q6 은 Q5 와 정책이 일부러 다르다: Q5 는 컷오프, Q6 은 **skip 후 greedy 계속**. 링크가 죽은
  상황에선 남는 대역에 싼 스트림이라도 태우는 편이 낫기 때문 — **"왜 다르게 했나"를 말로
  설명할 수 있는 것이 이 문제의 진짜 포인트**다.

---

## 각 Q의 핵심 포인트 (한 줄씩)

- **Q1 block pool** — 빈 블록 안에 next 를 숨기는 intrusive free list로 alloc/free 둘 다 O(1);
  **풀 밖/오정렬 포인터 free 는 거부하고 카운트**(free list 오염 방지)가 채점 포인트.
- **Q2 msgq (SPSC ring)** — 고정 크기 메시지 값 복사 + `count = head - tail`;
  **인덱스마다 writer 가 한 명뿐이라 락이 필요 없다**는 근거를 말할 수 있어야 한다.
- **Q3 topic filter** — 구독을 32비트 마스크로 표현해 **할당 0회 pub/sub**;
  거른 메시지는 큐 용량을 아예 소모하지 않고 `filtered` 로 관측 가능하게 만든다.
- **Q4 token bucket** — milli-token 정수 스케일링으로 **잔여분 손실 없이** 충전, burst 상한 클램프,
  `now - last` 는 부호없는 뺄셈이라 tick 랩어라운드에 안전.
- **Q5 budget_alloc** — ① min 보장(기아 방지) → ② 잔여를 prio 순 want 까지;
  min 을 못 채우면 그 아래는 전부 0(우선순위 역전 차단), stable 정렬로 결정적.
- **Q6 select_to_send** — min_bps 기준 greedy 선택으로 **뭘 포기할지**를 결정;
  재밍 10% 상황에서 비행 제어·텔레메트리는 살고 verbose 로깅은 죽는다.

---

## 흔한 함정 체크리스트

- [ ] **ABA 문제** — free list head 를 CAS 로 lock-free 하게 만들면, A→B→A 사이에 head 가 바뀌어도
      CAS 가 성공해 리스트가 깨진다. 방어: 태그 카운터 붙인 포인터(tagged pointer), 또는 애초에
      **ISR 에서는 풀을 만지지 않고** 인터럽트 잠깐 막기(critical section)로 단순화.
- [ ] **정렬(alignment)** — 바이트 배열을 그냥 `(void**)blk` 로 캐스팅 → 일부 MCU 에서 hard fault.
      `_Alignas` + `memcpy` 로 해결. `block_size >= sizeof(void*)` 검사도 필수.
- [ ] **정수 오버플로** — `elapsed_ms * rate` 를 32비트로 하면 오래 idle 후 곱셈이 넘친다 → `uint64_t`.
      대역폭도 bps 를 32비트에 담을 땐 합산 순서 주의 (min 합계가 total 을 넘는지 먼저 확인).
- [ ] **tick wraparound** — `if (now_ms > last_ms + period)` 는 랩되면 **영원히 거짓**.
      항상 `(uint32_t)(now - last) >= period` 형태로 **차이를 부호없이** 계산.
- [ ] `count` 를 공유 필드로 두어 SPSC 무락 전제를 깨뜨림.
- [ ] 큐가 가득 찼을 때 **덮어쓰기**(오래된 제어 명령 유실) vs **거부**(최신 유실) — 정책을 토픽별로
      정해야 한다. 로그는 덮어쓰기, 명령은 거부가 보통.
- [ ] 드롭/필터 카운터 없음 → "왜 메시지가 안 왔지"를 **현장에서 진단 불가**. 카운터는 공짜다.
- [ ] 우선순위만으로 분배해 하위 스트림이 영구 기아 → 하트비트조차 못 보내 기체 상태 불명.

---

## 면접 꼬리질문 (답 요지 포함)

1. **"왜 malloc 을 안 쓰나? 풀도 결국 고갈되는데?"**
   → 고갈 시점이 **결정적**이고 지역적이기 때문. 풀은 `used` 하이워터마크로 **사전에** 크기를 증명할
   수 있고, 실패해도 그 서브시스템에만 국한된다. 반면 힙은 파편화로 **언제 실패할지 모르고**,
   실패가 시스템 전역으로 번지며, 할당 지연 자체가 제어 루프 WCET 를 깨뜨린다.

2. **"SPSC 큐가 정말 락 없이 안전한가? 멀티코어에서는?"**
   → 인덱스마다 writer 가 정확히 한 명이라 write-write 경합이 없다는 게 근거. 다만 **가시성/순서**는
   별개 문제라 멀티코어에서는 `_Atomic` + release store(head) / acquire load(tail) 가 필요하다.
   `volatile` 은 순서·원자성을 보장하지 않으므로 그것만으로는 부족. 생산자가 둘이 되는 순간 전제가
   깨지므로, 그땐 뮤텍스나 per-producer 큐 + 병합으로 간다.

3. **"큐가 가득 차면 새 걸 버리나, 오래된 걸 버리나?"**
   → **토픽별 정책**. 센서 최신값/로그는 오래된 걸 덮어쓰는 게 맞고(최신성이 가치),
   명령·이벤트는 순서와 누락이 중요하니 새 걸 거부하고 `dropped` 를 올린 뒤 상위가 재시도한다.
   어느 쪽이든 **조용히 버리지 않는다** — 카운터 + 레이트 리밋된 경고 로그.

4. **"텔레메트리와 영상이 같은 링크를 쓴다. 어떻게 나눌 건가?"**
   → 각 스트림에 `(min, want, prio)` 를 선언하게 하고 런타임이 중재한다. ① prio 순 min 보장으로
   기아를 막고 ② 잔여를 prio 순 want 까지. 예산이 모자라면 **하위부터 0** 으로 떨어뜨린다.
   결과는 stable 정렬로 **결정적**이라 비행 로그로 재현·감사 가능. 정책은 코드가 아니라
   **설정(config)** 으로 빼서 팀별 재컴파일 없이 바꾼다.

5. **"재밍으로 대역이 10% 가 됐다. 무엇을 먼저 끄나?"**
   → 미리 정의한 열화 사다리대로: verbose 로그 → 영상 → 고빈도 텔레메트리 → 저빈도 하트비트.
   비행 제어와 링크 상태 하트비트는 **절대 불가침**. "모두 조금씩"은 전부 쓸모없게 만드는 최악의 선택.
   복귀는 히스테리시스를 둬서(좋아졌다 나빠졌다 진동 방지) 단계적으로 올린다.

6. **"token bucket 을 float 없이 어떻게? 1ms 마다 호출되면?"**
   → 토큰을 1000배 정수로 들고 `tokens += elapsed_ms * rate` (rate tokens/s = rate milli-tokens/ms).
   나눗셈이 없으니 잔여분이 증발하지 않는다. `elapsed` 는 부호없는 뺄셈으로 랩어라운드 안전,
   곱셈은 64비트로 올려 오버플로 방지, 마지막에 `burst*1000` 으로 클램프.
