---
id: DS-A03
time: 110분
updated: 2026-11-08
---
# DS-A03 Bounded MPMC 큐: Vyukov 시퀀스 방식

## 학습 목표

- Dmitry Vyukov 의 bounded MPMC 큐(1024cores "Bounded MPMC queue")를 C11 `<stdatomic.h>` 로 직접 구현한다: 슬롯마다 `sequence`, `enqueue_pos` / `dequeue_pos` 를 CAS 로 예약, `dif = seq - 기대값` 의 세 경우(내 차례 / 가득·빔 / 낡은 위치).
- **왜 슬롯마다 sequence 가 필요한지** 를 말로, 그리고 강제 스케줄 데모로 보인다: head/tail 한 쌍으로는 "예약됨" 과 "쓰기 끝남" 을 구분할 수 없다.
- load/store/CAS 하나하나에 **왜 그 memory_order 인지** 설명한다 (seq = acquire/release, 위치 CAS = relaxed). relaxed 로 바꾸면 이 맥에서 실제로 메시지가 중복·누락되는 것을 본다.
- 4×4 스트레스로 **모든 메시지가 정확히 한 번**, **생산자별 순서 유지**, **최종 카운트 정확** 을 검증하고, 생산자별 순서가 왜 항상 성립하는지 증명한다.
- "Vyukov 큐는 엄밀한 의미의 lock-free 가 아니다" 를 강제 일시정지 훅으로 결정적으로 보이고, lock-free / wait-free / obstruction-free 를 정확히 정의한다. 뮤텍스 큐 대비 처리량을 실측하고 언제 이기고 언제 지는지 숫자로 말한다.

## 1. 문제

"생산자도 여럿, 소비자도 여럿인 고정 크기 큐를 락 없이 짜 보세요." — 고급 면접의 단골 문제이고, 실제로는 멀티코어 MCU(듀얼 코어 STM32H7, ESP32, 멀티 클러스터 SoC)의 작업 분배, 여러 ISR 과 태스크가 동시에 올리는 이벤트·로그 큐, 스레드 풀의 작업 큐가 전부 이 모양이다. 면접관이 진짜 듣고 싶은 건 코드보다 **"슬롯마다 시퀀스 번호를 두는 이유"** 와 **"이게 정말 lock-free 인가"** 다.

SPSC 링(DS-A01)은 생산자가 하나라서 `tail` 을 혼자 쓰고 release store 하나로 게시하면 끝났다. 생산자가 둘이 되면 두 가지가 동시에 깨진다. (1) 두 생산자가 같은 `tail` 을 읽고 같은 칸에 쓴다 → `tail` 을 CAS 로 "예약" 해야 한다. (2) 예약은 했는데 아직 데이터를 안 쓴 칸이 생긴다. 소비자는 `head < tail` 만 보고 그 칸을 읽어 버린다. 카운터 한 쌍은 "몇 번째까지 **예약** 됐나" 만 알고 "몇 번째까지 **써졌나**" 는 모른다. Vyukov 의 해법: **상태를 칸마다 하나씩** 둔다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">cap = 8, 슬롯 i 의 seq 가 "이 칸에서 다음에 무슨 일이 일어날 수 있나" 를 말한다</text>
<rect x="20" y="40" width="190" height="34" rx="4" class="f-acc" stroke="currentColor"/>
<text x="30" y="62" fill="currentColor">enq_pos = 13 (128 B 라인 혼자)</text>
<rect x="520" y="40" width="200" height="34" rx="4" class="f-acc" stroke="currentColor"/>
<text x="530" y="62" fill="currentColor">deq_pos = 9 (128 B 라인 혼자)</text>
<g stroke="currentColor">
<rect x="20" y="110" width="80" height="60" class="f-mut"/><rect x="100" y="110" width="80" height="60" class="f-ok"/>
<rect x="180" y="110" width="80" height="60" class="f-ok"/><rect x="260" y="110" width="80" height="60" class="f-ok"/>
<rect x="340" y="110" width="80" height="60" class="f-warn"/><rect x="420" y="110" width="80" height="60" class="f-mut"/>
<rect x="500" y="110" width="80" height="60" class="f-mut"/><rect x="580" y="110" width="80" height="60" class="f-mut"/>
</g>
<g fill="currentColor">
<text x="30" y="132">slot 0</text><text x="30" y="156">seq 16</text>
<text x="110" y="132">slot 1</text><text x="110" y="156">seq 10</text>
<text x="190" y="132">slot 2</text><text x="190" y="156">seq 11</text>
<text x="270" y="132">slot 3</text><text x="270" y="156">seq 12</text>
<text x="350" y="132">slot 4</text><text x="350" y="156">seq 12</text>
<text x="430" y="132">slot 5</text><text x="430" y="156">seq 13</text>
<text x="510" y="132">slot 6</text><text x="510" y="156">seq 14</text>
<text x="590" y="132">slot 7</text><text x="590" y="156">seq 15</text>
</g>
<path d="M140 76 L140 106" stroke="currentColor" fill="none"/><text x="146" y="96" fill="currentColor">소비자: pos 9 → slot 1, seq == 9+1 → 내 차례</text>
<path d="M380 180 L380 206" stroke="currentColor" fill="none"/>
<text x="20" y="222" fill="currentColor">slot 4: pos 12 생산자가 CAS 로 예약만 하고 아직 안 씀 (seq 12 그대로)</text>
<text x="20" y="240" fill="currentColor">→ 소비자는 pos 12 에서 seq 12 ≠ 13 → "아직 없음". head/tail 만으론 모른다</text>
<text x="20" y="270" fill="currentColor">초록 = 써짐 (seq = pos+1)   회색 = 비었음 (seq = 다음 바퀴 pos)   주황 = 예약, 쓰는 중</text>
<text x="20" y="290" fill="currentColor">다음 생산자: pos 13 → slot 5, seq 13 == pos → 빈 칸, 내 차례 (pos 5 의 소비자가 5+8 = 13 을 남겼다)</text>
</svg>
```

(같은 그림에서 아주 낡은 pos 5 를 들고 온 생산자는 slot 5 에서 seq 13 을 본다: dif = +8 → "내 pos 는 이미 다른 생산자가 가져갔다" → `enq_pos` 다시 읽기. 세 경우를 표로:)

| 누가 | 기대값 | `dif = seq - 기대값` = 0 | < 0 | > 0 |
|---|---|---|---|---|
| 생산자 (pos) | `pos` | 빈 칸, 내 차례 → `enq_pos` CAS | 한 바퀴 전 소비자가 아직 안 비움 = **가득** → false | 다른 생산자가 이미 가져감 → `enq_pos` 다시 읽기 |
| 소비자 (pos) | `pos + 1` | 써진 칸, 내 차례 → `deq_pos` CAS | 아직 안 써짐 = **빔** (또는 예약 후 쓰는 중) → false | 다른 소비자가 이미 가져감 → `deq_pos` 다시 읽기 |

### Part A — Vyukov 큐 (`solution.c` A1–A4)

- `slot_case(seq, expect)` — `(intptr_t)(seq - expect)` 의 부호로 세 경우. `SIZE_MAX` 근처에서 wrap 해도 맞아야 한다.
- `mpmc_init(q, cells, cap)` — cap 은 2 의 거듭제곱이고 **≥ 2** (왜 1 은 안 되나 → 5. 풀이). 슬롯 i 의 seq = i.
- `mpmc_try_enqueue` / `mpmc_try_dequeue` — 한 번 시도, 가득/빔이면 false. CAS 성공 직후 테스트 훅 `after_claim(pos)` 을 부른 뒤 데이터를 쓴다.

### Part B — 검증과 "lock-free 가 아니다"

- `order_step` (B1): 한 소비자가 본 메시지 `(p << 32) | i` 가 생산자 p 에서 이전보다 큰 번호인지. `exactly_once` (B2): 메시지별 수신 횟수 배열 → 누락/중복 개수. `mpmc_dequeue_bounded` (B3): 최대 N 번 시도하는 "기다리는" dequeue.
- 스트레스(주어짐): 4×4 cap 1024, 4×4 cap 2, 6×2 cap 16, 1×6 cap 64, 각 생산자 50만 개. 정확히 한 번 · 소비자마다 생산자별 순서 증가 · 합계 · 끝난 뒤 빔 · `enq_pos == deq_pos == 총 개수`.
- **강제 일시정지**(주어짐): 생산자 A 가 슬롯 0 을 CAS 로 예약한 직후 훅에서 멈춘다. 다른 생산자는 계속 넣을 수 있지만, 소비자는 1000 번 시도해도, 10만 번 기다려도 아무것도 못 받는다. A 를 풀면 위치 순서대로 다 나온다.
- 데모: `demo_broken.c` (head/tail 만 쓴 큐, cap 1, relaxed seq), `demo_asm.c` (weak/strong CAS, LSE/LL·SC/Cortex-M), `demo_bench.c` (Vyukov vs 뮤텍스 vs condvar, 1×1–6×6).

### Part C — 펌웨어: 여러 ISR + main 의 이벤트 큐

- `evq_post` (C1): 어디서든(ISR 포함) 부르는 게시. **try 한 번**, 가득이면 `drops++` 하고 false. `evq_drain` (C2): main 루프에서 비우기.
- 시나리오(주어짐, 단일 코어 선점 = 중첩 호출로 결정적 재현): main 이 슬롯을 예약한 순간 ISR 이 들어와 이벤트를 올리고 drain 도 시도한다. ISR 의 게시는 성공하고, ISR 의 drain 은 **기다리지 않고 0** 을 받아 나와야 한다. ISR 이 10 개를 폭주시키면 정확히 7 개 성공, 3 개 drop.

## 2. Problem (English)

> Implement Dmitry Vyukov's bounded multi-producer multi-consumer queue in C11:
>
> ```c
> typedef struct { atomic_size_t seq; uint64_t data; } cell_t;
> typedef struct { cell_t *buf; size_t mask; _Alignas(128) atomic_size_t enq_pos; _Alignas(128) atomic_size_t deq_pos; } mpmc_t;
> bool mpmc_init(mpmc_t *q, cell_t *cells, size_t cap);   // cap = power of two, >= 2
> bool mpmc_try_enqueue(mpmc_t *q, uint64_t v);           // false if full
> bool mpmc_try_dequeue(mpmc_t *q, uint64_t *out);        // false if empty
> ```
>
> No locks; each slot carries a sequence number. State the memory order of every atomic access and why. Then prove with a 4-producer × 4-consumer stress test that every message is consumed exactly once, that each consumer sees each producer's messages in increasing order, and that the final counts are exact. Benchmark against a `pthread_mutex_t`-protected ring for 1×1, 2×2, 4×4, 6×6 threads.
>
> Follow-ups: Why can't a single head/tail pair work for MPMC? Is this queue lock-free? What if a producer is preempted between its CAS and its sequence store? Weak or strong CAS? What does it compile to on arm64 and on a Cortex-M4 / M0+? Can an ISR use it?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, 0.1 초 만에 끝남 — 앞의 체크가 하나라도 실패하면 멀티스레드 단계는 건너뛰므로 멈추지 않는다):

```text
== Part A: slot_case / init / FIFO ==
FAIL starter.c:332  slot_case(5, 5) == SLOT_MINE && slot_case(4, 5) == SLOT_NOT_READY && slot_case(13, 5) == SLOT_STALE
…
FAIL starter.c:344  mpmc_try_enqueue(&q, 10) && mpmc_try_enqueue(&q, 11) && mpmc_try_enqueue(&q, 12) && mpmc_try_enqueue(&q, 13)
…
SKIP 5 forced-schedule checks: earlier checks failed
SKIP 4 stress checks: earlier checks failed
…
6/35 passed
```

TODO 는 A1–A4 (큐), B1–B3 (검증기, bounded dequeue), C1–C2 (이벤트 큐). 스텁은 잘못된 값을 돌려줄 뿐 돌지 않는다. 스트레스 크기는 `DS_A03_MSGS=<생산자당 개수>` 로 바꿀 수 있고, ASan/TSan 빌드에서는 자동으로 1/10 (`__has_feature` 로 판단).

## 4. 힌트

<details><summary>힌트 1 — dif 는 왜 부호 있는 차이인가</summary>

seq 와 pos 는 계속 커지다가 언젠가 `SIZE_MAX → 0` 으로 넘어간다. `seq < pos` 처럼 크기 비교를 하면 wrap 순간 틀린다. **부호 없는 뺄셈** `seq - pos` 는 wrap 과 상관없이 "몇 칸 차이인지" 를 2^64 모듈로 정확히 준다. 이걸 `intptr_t` 로 해석하면 ±2^63 안의 차이는 부호까지 맞다. (범위 밖 변환은 구현 정의 — C11 6.3.1.3p3. clang/gcc 는 2의 보수 모듈러.)
</details>

<details><summary>힌트 2 — enqueue 의 골격</summary>

`pos = load(enq_pos, relaxed)` → 루프: `c = &buf[pos & mask]`, `seq = load(c->seq, acquire)`. 0 이면 `CAS_weak(&enq_pos, &pos, pos+1)` — 성공하면 루프 탈출, 실패하면 `pos` 에 현재 값이 들어오니 그대로 다시. 음수면 false. 양수면 `pos = load(enq_pos)`. 탈출 후: (훅) → `c->data = v` → `store(c->seq, pos+1, release)`.
</details>

<details><summary>힌트 3 — dequeue 가 seq 에 쓰는 값</summary>

소비자는 기대값이 `pos + 1` 이다. 다 읽은 뒤에는 이 칸을 **다음 바퀴 생산자** 에게 넘긴다. 다음 바퀴에서 이 칸을 쓸 생산자의 pos 는 `pos + cap` 이고, 생산자는 `seq == pos` 를 기다리므로 `store(c->seq, pos + mask + 1, release)`.
</details>

<details><summary>힌트 4 — 생산자별 순서 검사</summary>

`last1[p]` 에 "마지막으로 본 번호 + 1" 을 저장하면 0 을 "아직 없음" 으로 쓸 수 있다. 새 번호 i 는 `i + 1 > last1[p]` 여야 한다 (같으면 중복, 작으면 역순). 덧셈은 `uint64_t` 로 해서 `i = UINT32_MAX` 에서도 넘치지 않게.
</details>

<details><summary>힌트 5 — ISR 은 왜 try 한 번만</summary>

단일 코어에서 ISR 은 main 을 선점하고, **끝날 때까지 main 은 한 명령도 못 돈다**. main 이 슬롯을 예약만 해 둔 순간 ISR 이 그 슬롯을 기다리며 돌면, 기다리는 대상(main)이 영원히 못 돌아온다. 그러니 ISR 에서는 실패하면 바로 포기하고 셈만 한다.
</details>

## 5. 풀이

### Part A — 슬롯 seq 의 일생

슬롯 i (cap = N) 의 seq 는 정해진 순서로만 커진다. 각 단계의 store 를 하는 쪽은 정확히 한 스레드뿐이다.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">슬롯 i 의 seq: i → i+1 → i+N → i+N+1 → i+2N → …  (store 는 매번 그 순간의 "주인" 한 명만)</text>
<g stroke="currentColor">
<rect x="20" y="50" width="130" height="50" rx="6" class="f-mut"/>
<rect x="210" y="50" width="130" height="50" rx="6" class="f-warn"/>
<rect x="400" y="50" width="130" height="50" rx="6" class="f-ok"/>
<rect x="590" y="50" width="130" height="50" rx="6" class="f-mut"/>
</g>
<g fill="currentColor">
<text x="30" y="72">seq = i</text><text x="30" y="90">pos i 생산자 차례</text>
<text x="220" y="72">seq = i (그대로)</text><text x="220" y="90">CAS 로 예약, 쓰는 중</text>
<text x="410" y="72">seq = i+1</text><text x="410" y="90">pos i 소비자 차례</text>
<text x="600" y="72">seq = i+N</text><text x="600" y="90">pos i+N 생산자 차례</text>
</g>
<g stroke="currentColor" fill="none" stroke-width="1.5">
<path d="M150 75 L206 75"/><path d="M200 70 L206 75 L200 80"/>
<path d="M340 75 L396 75"/><path d="M390 70 L396 75 L390 80"/>
<path d="M530 75 L586 75"/><path d="M580 70 L586 75 L580 80"/>
</g>
<g fill="currentColor">
<text x="150" y="122">enq_pos CAS i→i+1</text><text x="150" y="138">(relaxed)</text>
<text x="335" y="122">data 쓰기 →</text><text x="335" y="138">store i+1 (release)</text>
<text x="525" y="122">CAS deq_pos, data 읽기</text><text x="525" y="138">→ store i+N (release)</text>
</g>
<line x1="10" y1="160" x2="730" y2="160" stroke="currentColor" stroke-dasharray="3 3"/>
<text x="10" y="182" fill="currentColor">① 생산자 data 쓰기 →(sb) seq=i+1 release →(sw) 소비자의 acquire load (seq==i+1)</text>
<text x="40" y="200" fill="currentColor">→(sb) 소비자 data 읽기 : 새 값을 본다</text>
<text x="10" y="222" fill="currentColor">② 소비자 data 읽기 →(sb) seq=i+N release →(sw) 다음 바퀴 생산자의 acquire load</text>
<text x="40" y="240" fill="currentColor">(seq==i+N) →(sb) data 덮어쓰기 : 읽기가 끝난 뒤에만 덮어쓴다</text>
<text x="10" y="266" fill="currentColor">sb = 프로그램 순서, sw = synchronizes-with (C11 5.1.2.4). 위치 CAS 는 사슬 밖 → relaxed</text>
</svg>
```

이 그림에서 정확성이 거의 다 나온다.

- **위치는 유일하다.** `enq_pos` 의 값 p 를 p+1 로 바꾸는 CAS 는 수정 순서(modification order)에서 딱 한 번만 성공한다 (RMW 는 수정 순서의 마지막 값을 읽는다, C11 7.17.3). 그래서 pos p 를 가진 생산자는 정확히 한 명, 소비자도 마찬가지.
- **pos p 의 소비자는 pos p 생산자의 값을 읽는다.** 소비자는 `seq == p+1` 을 봐야 읽는다. 그 값을 쓰는 건 pos p 생산자뿐이다. 생산자 p+N 은 `seq == p+N` 을 기다리는데, 그 값은 소비자 p 가 다 읽은 뒤에야 써진다. → 위치 ↔ 메시지가 일대일 = **정확히 한 번**.
- **덮어쓰기 없음 / 쓰레기 읽기 없음** 은 위 두 줄의 happens-before 사슬. `data` 는 일반 변수인데도 데이터 레이스가 아니다 (두 접근이 항상 happens-before 로 이어짐).

### Part A — 각 접근의 memory_order 와 이유

| 위치 | order | 이유 (빼면 무엇이 깨지나) |
|---|---|---|
| `enq_pos`/`deq_pos` 첫 load, 재 load | relaxed | "아마 이쯤" 이라는 힌트일 뿐. 낡은 값이면 seq 비교나 CAS 가 걸러낸다 |
| 슬롯 `seq` load (생산자) | **acquire** | 이전 바퀴 소비자의 `data` 읽기가 내 덮어쓰기보다 먼저임을 보장 (WAR) |
| 위치 CAS (성공·실패 둘 다) | relaxed | 같은 역할끼리 번호표만 나눈다. 데이터는 seq 로 넘어가므로 순서를 실을 필요가 없다 |
| `data` 쓰기/읽기 | 일반 접근 | seq 의 release/acquire 가 보호 |
| `seq = pos+1` store (생산자) | **release** | `data` 쓰기가 seq 보다 먼저 보이게 — 빠지면 소비자가 옛 값을 읽는다 (아래 실측) |
| 슬롯 `seq` load (소비자) | **acquire** | `seq == pos+1` 을 봤으면 그 뒤의 `data` 읽기는 새 값 |
| `seq = pos+N` store (소비자) | **release** | 내 `data` 읽기가 끝난 뒤에야 다음 생산자가 쓰게 |

C-A03 의 1칸 채널(release 로 게시, acquire 로 받기)을 칸마다, 양방향으로 한 것이다. 새로운 건 "번호표(위치)" 와 "칸의 상태(seq)" 를 **분리** 했다는 점뿐이다. 낡은 pos 로 seq 를 읽고 CAS 가 성공하는 경우도 안전하다: CAS 가 p 에서 성공했다는 건 그 순간 `enq_pos == p` 였다는 뜻이고, 슬롯 p 의 seq 를 p 에서 바꿀 수 있는 건 pos p 의 주인(나)뿐이므로 내가 읽은 `seq == p` 는 CAS 시점에도 그대로다.

### Part A — 왜 head/tail 한 쌍으로는 안 되나 (`demo_broken.c naive`)

<!-- code: demo_broken.c -->

`tail` 을 CAS 로 예약하고 나서 쓰는 "자연스러운" MPMC 를 만들고, 훅으로 멈출 지점을 강제했다. 실제 출력 (매번 같음):

```text
$ cc -std=c11 -Wall -Wextra -O2 -pthread demo_broken.c -o db && ./db naive
[naive 1] producer paused after tail CAS: consumer dequeue ok=1 value=0xDEAD (expected 100 = 0x64)
          after producer resumes: dequeue ok=0 -> message 100 is LOST (written into a slot already consumed)
[naive 2] consumer paused after head CAS: producer enqueue(99) ok=1, consumer then read 99 (expected 10)
```

(1) "예약 ≠ 쓰기 끝": 소비자가 `head < tail` 만 보고 아직 안 쓴 칸(0xDEAD)을 읽었고, 진짜 메시지 100 은 이미 소비된 칸에 써져 사라졌다. (2) 거꾸로 "head 증가 ≠ 읽기 끝": 소비자가 head 를 올린 뒤 읽기 전에 멈추자, 생산자가 "자리 있음" 으로 판단해 그 칸을 덮어썼다. 둘 다 **칸 하나하나의 상태** 가 없어서 생긴다. 전역 카운터를 두 쌍(예약용 + 완료용)으로 늘리는 방법도 있지만(DPDK `rte_ring` 의 head/tail 두 단계 — Q8), 그러면 완료 카운터를 순서대로 올려야 해서 앞 생산자를 기다리게 된다.

### Part A — cap = 1 은 왜 안 되나 (`./db cap1`)

seq 의 의미는 "pos+1 = 써짐" 과 "pos+N = 비었음(다음 바퀴)" 이다. N = 1 이면 **두 값이 같다.**

```text
$ ./db cap1
[cap1] enq 111 -> 1, enq 222 -> 1 (cap 1 must refuse the 2nd), slot data=222 seq=2 enq=2 deq=0
       consumer at pos 0 expects seq 1, sees 2: dif=+1 -> 'stale', reloads deq=0, forever (livelock)
```

두 번째 enqueue 가 "빈 칸" 으로 착각해 111 을 덮어썼고, 소비자는 pos 0 에서 영원히 "낡았다 → 다시 읽기" 를 돈다. 그래서 `mpmc_init` 은 cap ≥ 2 를 요구한다 (Vyukov 원본도 `buffer_size >= 2` 를 assert 한다). 2 의 거듭제곱은 `pos & mask` 로 칸을 찾고, `size_t` 가 wrap 할 때도 `pos % N` 이 끊기지 않게 하기 위해서다 (2^64 가 N 의 배수).

### Part B — 검증기와 4×4 스트레스

`order_step` 은 6 줄이다: `p = v >> 32`, `i = (uint32_t)v`, `p >= nprod` 이면 false, `i + 1 <= last1[p]` 이면 false, 아니면 `last1[p] = i + 1`. `exactly_once` 는 0 인 칸(누락)과 2 이상인 칸(중복)을 센다. 검증기 자체를 먼저 손으로 만든 입력(중복, 역순, 범위 밖)으로 테스트하는 게 요점이다 — 검증기가 틀리면 스트레스가 아무것도 증명하지 못한다.

실제 출력 (`./solution`, 6 회 반복 모두 35/35):

```text
== Part B: stress (exactly once, per-producer order, exact counts) ==
  4 prod x 4 cons, cap 1024, 500000 msgs/prod: 0.19 s, lost 0 dup 0
  4 prod x 4 cons, cap    2, 500000 msgs/prod: 0.34 s, lost 0 dup 0
  6 prod x 2 cons, cap   16, 500000 msgs/prod: 0.33 s, lost 0 dup 0
  1 prod x 6 cons, cap   64, 500000 msgs/prod: 0.05 s, lost 0 dup 0
```

cap 2 는 거의 매 연산이 가득/빔 경계에서 일어나고 seq 가 50만 바퀴를 돈다 — 경계 버그를 가장 잘 잡는 설정이다.

### Part B — "소비자마다 생산자별 순서 증가" 는 정말 항상 성립하나?

가정하지 말고 증명하자. 생산자 P 가 m1 을 넣고(위치 p1) 그다음 m2 를 넣었다(위치 p2). P 의 두 번째 CAS 는 첫 번째 CAS 보다 프로그램 순서상 뒤이고, 같은 원자 변수에 대한 RMW 는 수정 순서를 따른다 → **p2 > p1** (C11 5.1.2.4 의 write-write coherence: happens-before 로 앞선 수정은 수정 순서에서도 앞). 소비자 C 도 마찬가지로 자기가 차례로 받은 위치가 d1 < d2 < … 로 증가한다. 위에서 본 "위치 d 의 소비자는 위치 d 의 메시지를 받는다" 를 합치면: C 가 m2 를 먼저(d = p2), m1 을 나중에(d = p1) 받으려면 p1 > p2 여야 해서 모순이다. 그래서 **같은 소비자 안에서는** 생산자별 순서가 항상 유지된다 → CHECK 로 넣어도 된다.

성립하지 **않는** 것도 적어 둔다: 서로 다른 소비자 사이에는 순서가 없다 (C1 이 m2 를 받아 처리를 먼저 끝낼 수 있다). 또 "P 의 m1 enqueue 완료가 Q 의 m3 enqueue 시작보다 happens-before 로 앞서면 m1 이 더 앞 위치를 받는다" 는 성립한다 (write-write coherence 로 Q 의 CAS 가 수정 순서에서 뒤). 그러나 dequeue 가 false("빔")를 답하는 경우는 선형화(linearizable)되지 않는다 — 바로 다음 절.

### Part B — 강제 일시정지: Vyukov 큐는 엄밀한 의미의 lock-free 가 아니다

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">solution.c 의 강제 스케줄 (cap 4) — 훅이 A 를 CAS 와 seq store 사이에 묶어 둔다</text>
<text x="10" y="58" fill="currentColor">생산자 A</text>
<rect x="110" y="40" width="120" height="28" rx="4" class="f-acc" stroke="currentColor"/><text x="118" y="59" fill="currentColor">CAS 0→1 성공</text>
<rect x="240" y="40" width="330" height="28" rx="4" class="f-warn" stroke="currentColor"/><text x="250" y="59" fill="currentColor">훅에서 멈춤 (선점·페이지 폴트·디버거라고 생각)</text>
<rect x="580" y="40" width="140" height="28" rx="4" class="f-ok" stroke="currentColor"/><text x="588" y="59" fill="currentColor">data, seq=1</text>
<text x="10" y="108" fill="currentColor">생산자 B</text>
<rect x="250" y="90" width="200" height="28" rx="4" class="f-ok" stroke="currentColor"/><text x="258" y="109" fill="currentColor">201, 202, 203 → true</text>
<rect x="460" y="90" width="100" height="28" rx="4" class="f-mut" stroke="currentColor"/><text x="468" y="109" fill="currentColor">204 → 가득</text>
<text x="10" y="158" fill="currentColor">소비자</text>
<rect x="250" y="140" width="310" height="28" rx="4" class="f-warn" stroke="currentColor"/><text x="258" y="159" fill="currentColor">try ×1000 → 전부 false, bounded 10만 → false</text>
<rect x="580" y="140" width="140" height="28" rx="4" class="f-ok" stroke="currentColor"/><text x="588" y="159" fill="currentColor">100,201,202,203</text>
<g fill="currentColor">
<text x="10" y="200">슬롯:  [0] seq 0 (A 예약, 안 씀)   [1] seq 2 (201)   [2] seq 3 (202)   [3] seq 4 (203)   deq_pos = 0</text>
<text x="10" y="222">B 의 enqueue 는 이미 "완료" 했는데 dequeue 는 "비었음" → FIFO 명세로 설명할 수 없는 답</text>
<text x="10" y="244">A 가 영원히 안 돌아오면: 소비자는 영원히 아무것도 못 받고, 생산자도 한 바퀴(cap-1 개) 뒤에 막힌다</text>
</g>
</svg>
```

`solution.c` 의 5 개 체크가 정확히 이 그림이다 (실제 출력):

```text
== Part B: forced pause between CAS and seq store ==
PASS claimed && gate_pos == 0u && atomic_load(&q4.enq_pos) == 1u && atomic_load(&c4[0].seq) == 0u
PASS b_ok && !mpmc_try_enqueue(&q4, 204)
PASS empty_answers == 1000u && atomic_load(&q4.deq_pos) == 0u
PASS !mpmc_dequeue_bounded(&q4, &t, 100000u)
PASS a.ok && all && o[0] == 100u && o[1] == 201u && o[2] == 202u && o[3] == 203u
```

정의를 정확히 하자 (Herlihy & Shavit, *The Art of Multiprocessor Programming* 3장 기준):

| 진행 보장 | 정의 | 멈춘 스레드가 남을 막을 수 있나 |
|---|---|---|
| wait-free | **모든** 연산이 다른 스레드와 상관없이 유한(보통 상한 있는) 단계 안에 끝난다 | 아니오 (나도 굶지 않음) |
| lock-free | 스레드들이 계속 단계를 밟는 한, **누군가의** 연산은 유한 단계 안에 끝난다 (시스템 전체 진행). 개별 스레드는 굶을 수 있다 | 아니오 |
| obstruction-free | 다른 스레드가 모두 멈춘 상태에서 **혼자** 돌면 유한 단계 안에 끝난다 | 아니오 (하지만 경합 중 livelock 가능) |
| blocking (락 등) | 위 어느 것도 아님 — 한 스레드가 멈추면 다른 스레드가 영원히 못 끝낼 수 있다 | **예** |

Vyukov 큐의 `try_` 함수들은 코드만 보면 늘 유한 단계 안에 **반환** 한다 (CAS 가 실패했다면 다른 누군가가 성공했다). 그러나 반환값 "비었음" 은 위 그림처럼 큐에 완료된 원소가 있는데도 나온다 — 선형화 가능한 FIFO 의 답이 아니다. FIFO 로서 의미 있는 연산 "원소 하나 꺼내기" 는, 멈춘 A 하나 때문에 **모든 소비자가 영원히** 끝내지 못한다. 그래서 정의상 blocking 이다. A 가 막는 범위는 슬롯 하나(와 한 바퀴 뒤의 생산자)라서 실제로는 락보다 훨씬 덜 아프다 — 뮤텍스를 쥔 채 멈춘 스레드는 **모든** 생산자와 소비자를 막는다. 1024cores 페이지 설명도 "공식적인 의미의 lock-free 는 아니고, 뮤텍스 없이 원자 RMW 로 구현했을 뿐" 이라는 취지로 알려져 있다 (원문 문구는 미확인). 면접 답: "**락이 없는(lock-less) 큐지만 lock-free 는 아니다. CAS 와 seq store 사이에서 선점된 생산자가 그 슬롯의 소비자를 막는다.**"

### Part B — relaxed 로 바꾸면? (`./db relaxed`, 이 맥에서 실제로 깨짐)

seq 의 acquire/release 를 전부 relaxed 로 바꾼 같은 큐를 4×4, cap 4, 생산자당 100만 개로 돌렸다 (load avg 9.6):

```text
$ ./db relaxed 1000000 4 3
[relaxed] run 0: 4000000 msgs, cap 4, 0.53 s: lost 19, dup 19, garbage 0
[relaxed] run 1: 4000000 msgs, cap 4, 0.54 s: lost 16, dup 16, garbage 0
[relaxed] run 2: 4000000 msgs, cap 4, 0.52 s: lost 8, dup 8, garbage 0
$ ./db acqrel 1000000 4 2
[acq/rel] run 0: 4000000 msgs, cap 4, 0.66 s: lost 0, dup 0, garbage 0
[acq/rel] run 1: 4000000 msgs, cap 4, 0.61 s: lost 0, dup 0, garbage 0
```

400만 개 중 8–19 개(약 1/25만)가 깨졌다. lost 와 dup 이 항상 같다: 소비자가 seq 는 새 값으로 봤는데 `data` 는 **한 바퀴 전 값** 으로 읽었다 → 옛 메시지 중복 + 새 메시지 누락. 컴파일러 탓인지 확인하려고 relaxed 경로의 asm 을 봤다 (`cc -O2 -S demo_broken.c`): 생산자는 `str x9, [x11, #8]` (data) 다음 `str x13, [x11]` (seq) 로 **프로그램 순서 그대로** 였다. acquire/release 경로는 같은 자리에 `ldapr` / `stlr` 가 들어간다. 즉 이 깨짐은 **하드웨어 재배치**(ARMv8 은 약한 순서) 다. x86(TSO)에서는 store-store, load-load 재배치가 없어서 같은 코드가 우연히 맞을 수 있다 — 그게 "x86 에서 테스트 통과" 가 의미 없는 이유다. TSan 은 실행이 깨지든 말든 바로 잡는다 (`-fno-inline` 으로 프레임을 살린 실제 보고, 앞부분):

```text
$ cc -std=c11 -g -O1 -fno-inline -fsanitize=thread -pthread demo_broken.c -o db_tsan && ./db_tsan relaxed 20000 4 1
WARNING: ThreadSanitizer: data race (pid=3113)
  Write of size 8 at 0x000109700db8 by thread T1:
    #0 vq_enq demo_broken.c:118 (db_tsan2:arm64+0x100001400)
    #1 r_prod demo_broken.c:168 (db_tsan2:arm64+0x100001530)

  Previous write of size 8 at 0x000109700db8 by thread T3:
    #0 vq_enq demo_broken.c:118 (db_tsan2:arm64+0x100001400)
  …
  Location is heap block of size 64 at 0x000109700d80 allocated by main thread:
```

118 행은 `c->data = v`. 두 생산자(다른 바퀴)의 쓰기 사이에 happens-before 가 없다고 정확히 짚는다 (exit 134). 정답 코드는 TSan 경고 0.

### Part B — CAS: weak 냐 strong 이냐, 어떤 명령이 되나 (`demo_asm.c`)

<!-- code: demo_asm.c -->

같은 파일을 네 가지로 컴파일한 실제 결과 (명령은 파일 머리 주석):

| 대상 | `compare_exchange_weak` (relaxed) | `_strong` | seq acquire load / release store |
|---|---|---|---|
| arm64 기본 (`-mcpu` 기본 apple-m1) | `cas x8, x2, [x0]` 한 줄 | **같은** `cas` 한 줄 | `ldapr` / `stlr` |
| arm64 `-mcpu=apple-m1+nolse` | `ldxr` … `stxr` 한 번 (루프 없음, 실패 = 가짜 실패) | `ldxr`…`stxr` + `cbnz w10` 로 **재시도 루프** | `ldapr` / `stlr` |
| Cortex-M4 (`thumbv7em`) | `ldrex` … `strex` 한 번 | `strex` 실패 시 `bne` 재시도 루프 | `ldr` + `dmb sy` / `dmb sy` + `str` |
| Cortex-M0+ (`thumbv6m`) | `bl __atomic_compare_exchange_4` | 같은 libcall | `bl __atomic_load_4` / `bl __atomic_store_4` |

- LSE 에서는 `cas` 가 원래 가짜 실패가 없어서 weak = strong 이다. LL/SC 에서는 weak 가 루프 하나를 덜 갖는다. Vyukov 의 CAS 는 **이미 재시도 루프 안** 에 있으므로 weak 가 맞다: 가짜 실패하면 seq 를 다시 읽고 다시 시도할 뿐이다 (C-A03 Q5 의 규칙 "루프면 weak").
- 비용: ARM-M04 가 이 맥에서 "`casal` CAS 루프는 경합 시 LL/SC CAS 보다 2–4배 느리다" 를 쟀다. 큐 수준에서도 그런지 `demo_bench` 를 `-mcpu=apple-m1+nolse` 로 다시 빌드해 비교했다: 4×4 에서 LSE 10.9 vs LL/SC 11.0 Mops, 6×6 11.8 vs 10.1 (W=0, 5회 중앙값) — **큐에서는 차이가 오차 수준** 이었다. CAS 한 줄만 두드리는 마이크로벤치와 달리, 큐 연산은 위치 라인 + 슬롯 라인 두 개를 옮기는 비용이 지배하기 때문으로 보인다 (추정). 경합 없는 1 스레드 enq+deq 한 쌍은 LSE 2.6 ns vs LL/SC 6.3 ns 로 LSE 가 빨랐다 (ARM-M04 의 비경합 결과와 같은 방향).
- M0+ 는 원자 load 조차 libcall 이다 (`-Watomic-alignment`: "exceeds the max lock-free size (0 bytes)"). ARMv6-M 에는 LDREX/STREX 가 없으니(CPP-A04), 그 libcall 은 보통 인터럽트를 끄고 구현된다 → 더 이상 lock-free 가 아니다. M0+ 에서는 처음부터 짧은 critical section 큐(C-M14)가 정직하다.

### Part B — 처리량: 뮤텍스보다 빠른가? (`demo_bench.c`)

<!-- code: demo_bench.c -->

기계: Apple M6 (`sysctl hw.perflevel0/1/2.physicalcpu` = Super 2 + Performance 4 + Efficiency 6), Apple clang 21.0.0, 코어 고정 불가. 다른 레슨 작성 에이전트들의 벤치마크와 **동시에** 돌았다 (load average 7.0–9.3) — 절대값이 아니라 **비율** 을 볼 것. 메시지 200만 개, cap 1024, 5 회 중앙값 Mops/s. `W` = 메시지마다 생산자·소비자가 하는 가짜 일(빈 루프 200 회 ≈ 50 ns, 따로 잼).

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o bench
./bench all 2000000 5 0 && ./bench all 2000000 5 200
```

| W | 큐 | 1×1 | 2×2 | 4×4 | 6×6 |
|---|---|---|---|---|---|
| 0 | Vyukov | 44.4 | 18.6 | 10.9 | 11.8 |
| 0 | Vyukov, 슬롯 128 B 패딩 | 61.2 | 21.4 | 10.8 | 11.4 |
| 0 | pthread_mutex 링 (try + 재시도) | 29.1 | 23.4 | **18.1** | **15.0** |
| 0 | mutex + condvar (잠듦) | 34.2 | 21.0 | 11.9 | 10.0 |
| 200 | Vyukov | **14.8** | **13.5** | **11.1** | **10.9** |
| 200 | Vyukov, 패딩 | 16.6 | 16.4 | 11.7 | 11.6 |
| 200 | pthread_mutex 링 | 5.2 | 4.3 | 3.6 | 3.8 |
| 200 | mutex + condvar | 5.1 | 3.5 | 3.2 | 3.1 |

load 가 4.3–5.5 로 내려갔을 때 다시 돌린 값 (5회 중앙값): W=0 4×4 Vyukov 10.7 / mutex 16.2 / condvar 13.1, 6×6 11.1 / 15.7 / 11.4. W=200 4×4 10.9 / 3.1 / 3.0. 4×4·6×6 의 순위와 배수는 같았다. **1×1 은 실행마다 크게 흔들렸다** (mutex 24–29, condvar 34–52) — 두 스레드가 어느 코어(Super/P/E)에 놓이느냐에 따라 달라지는 것으로 보이고 코어 고정이 안 되니 1×1 끼리의 비교는 믿지 말 것.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">처리량 Mops/s (중앙값, 5회, 메시지 200만, cap 1024) — load avg 7.0–9.3, 비율만 볼 것</text>
<text x="50" y="42" fill="currentColor" font-weight="bold">W=0: 큐만 두드림 (0–50)</text>
<line x1="50" y1="270" x2="360" y2="270" stroke="currentColor"/>
<line x1="50" y1="270" x2="50" y2="70.0" stroke="currentColor"/>
<text x="44" y="274" fill="currentColor" text-anchor="end">0</text>
<text x="44" y="234" fill="currentColor" text-anchor="end">10</text>
<line x1="50" y1="230" x2="360" y2="230" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/>
<text x="44" y="194" fill="currentColor" text-anchor="end">20</text>
<line x1="50" y1="190" x2="360" y2="190" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/>
<text x="44" y="154" fill="currentColor" text-anchor="end">30</text>
<line x1="50" y1="150" x2="360" y2="150" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/>
<text x="44" y="114" fill="currentColor" text-anchor="end">40</text>
<line x1="50" y1="110" x2="360" y2="110" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/>
<text x="44" y="74" fill="currentColor" text-anchor="end">50</text>
<line x1="50" y1="70" x2="360" y2="70" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/>
<rect x="62" y="92.4" width="18" height="177.6" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
<text x="71" y="89.4" fill="currentColor" text-anchor="middle" font-size="12">44</text>
<rect x="82" y="153.6" width="18" height="116.4" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
<text x="91" y="150.6" fill="currentColor" text-anchor="middle" font-size="12">29</text>
<rect x="102" y="133.2" width="18" height="136.8" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
<text x="111" y="130.2" fill="currentColor" text-anchor="middle" font-size="12">34</text>
<text x="91" y="286" fill="currentColor" text-anchor="middle">1×1</text>
<rect x="138" y="195.6" width="18" height="74.4" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
<text x="147" y="192.6" fill="currentColor" text-anchor="middle" font-size="12">19</text>
<rect x="158" y="176.4" width="18" height="93.6" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
<text x="167" y="173.4" fill="currentColor" text-anchor="middle" font-size="12">23</text>
<rect x="178" y="186.0" width="18" height="84.0" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
<text x="187" y="183.0" fill="currentColor" text-anchor="middle" font-size="12">21</text>
<text x="167" y="286" fill="currentColor" text-anchor="middle">2×2</text>
<rect x="214" y="226.4" width="18" height="43.6" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
<text x="223" y="223.4" fill="currentColor" text-anchor="middle" font-size="12">11</text>
<rect x="234" y="197.6" width="18" height="72.4" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
<text x="243" y="194.6" fill="currentColor" text-anchor="middle" font-size="12">18</text>
<rect x="254" y="222.4" width="18" height="47.6" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
<text x="263" y="219.4" fill="currentColor" text-anchor="middle" font-size="12">12</text>
<text x="243" y="286" fill="currentColor" text-anchor="middle">4×4</text>
<rect x="290" y="222.8" width="18" height="47.2" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
<text x="299" y="219.8" fill="currentColor" text-anchor="middle" font-size="12">12</text>
<rect x="310" y="210.0" width="18" height="60.0" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
<text x="319" y="207.0" fill="currentColor" text-anchor="middle" font-size="12">15</text>
<rect x="330" y="230.0" width="18" height="40.0" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
<text x="339" y="227.0" fill="currentColor" text-anchor="middle" font-size="12">10</text>
<text x="319" y="286" fill="currentColor" text-anchor="middle">6×6</text>
<text x="410" y="42" fill="currentColor" font-weight="bold">W=200: 메시지당 일 ≈50 ns (0–20)</text>
<line x1="410" y1="270" x2="720" y2="270" stroke="currentColor"/>
<line x1="410" y1="270" x2="410" y2="70.0" stroke="currentColor"/>
<text x="404" y="274" fill="currentColor" text-anchor="end">0</text>
<text x="404" y="224" fill="currentColor" text-anchor="end">5</text>
<line x1="410" y1="220" x2="720" y2="220" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/>
<text x="404" y="174" fill="currentColor" text-anchor="end">10</text>
<line x1="410" y1="170" x2="720" y2="170" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/>
<text x="404" y="124" fill="currentColor" text-anchor="end">15</text>
<line x1="410" y1="120" x2="720" y2="120" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/>
<text x="404" y="74" fill="currentColor" text-anchor="end">20</text>
<line x1="410" y1="70" x2="720" y2="70" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/>
<rect x="422" y="122.0" width="18" height="148.0" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
<text x="431" y="119.0" fill="currentColor" text-anchor="middle" font-size="12">15</text>
<rect x="442" y="218.0" width="18" height="52.0" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
<text x="451" y="215.0" fill="currentColor" text-anchor="middle" font-size="12">5</text>
<rect x="462" y="219.0" width="18" height="51.0" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
<text x="471" y="216.0" fill="currentColor" text-anchor="middle" font-size="12">5</text>
<text x="451" y="286" fill="currentColor" text-anchor="middle">1×1</text>
<rect x="498" y="135.0" width="18" height="135.0" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
<text x="507" y="132.0" fill="currentColor" text-anchor="middle" font-size="12">14</text>
<rect x="518" y="227.0" width="18" height="43.0" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
<text x="527" y="224.0" fill="currentColor" text-anchor="middle" font-size="12">4</text>
<rect x="538" y="235.0" width="18" height="35.0" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
<text x="547" y="232.0" fill="currentColor" text-anchor="middle" font-size="12">4</text>
<text x="527" y="286" fill="currentColor" text-anchor="middle">2×2</text>
<rect x="574" y="159.0" width="18" height="111.0" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
<text x="583" y="156.0" fill="currentColor" text-anchor="middle" font-size="12">11</text>
<rect x="594" y="234.0" width="18" height="36.0" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
<text x="603" y="231.0" fill="currentColor" text-anchor="middle" font-size="12">4</text>
<rect x="614" y="238.0" width="18" height="32.0" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
<text x="623" y="235.0" fill="currentColor" text-anchor="middle" font-size="12">3</text>
<text x="603" y="286" fill="currentColor" text-anchor="middle">4×4</text>
<rect x="650" y="161.0" width="18" height="109.0" class="f-acc" stroke="currentColor" stroke-width="0.5"/>
<text x="659" y="158.0" fill="currentColor" text-anchor="middle" font-size="12">11</text>
<rect x="670" y="232.0" width="18" height="38.0" class="f-mut" stroke="currentColor" stroke-width="0.5"/>
<text x="679" y="229.0" fill="currentColor" text-anchor="middle" font-size="12">4</text>
<rect x="690" y="239.0" width="18" height="31.0" class="f-warn" stroke="currentColor" stroke-width="0.5"/>
<text x="699" y="236.0" fill="currentColor" text-anchor="middle" font-size="12">3</text>
<text x="679" y="286" fill="currentColor" text-anchor="middle">6×6</text>
<rect x="60" y="298" width="14" height="12" class="f-acc" stroke="currentColor"/>
<text x="80" y="309" fill="currentColor">Vyukov MPMC</text>
<rect x="270" y="298" width="14" height="12" class="f-mut" stroke="currentColor"/>
<text x="290" y="309" fill="currentColor">pthread_mutex 링 (try)</text>
<rect x="480" y="298" width="14" height="12" class="f-warn" stroke="currentColor"/>
<text x="500" y="309" fill="currentColor">mutex+condvar (잠)</text>
</svg>
```

읽는 법 — 결과는 예상과 달랐고, 그게 이 절의 요점이다.

1. **큐만 두드리면(W=0) 4×4·6×6 에서 뮤텍스가 1.3–1.7배 빨랐다.** `time` 으로 보면 뮤텍스 판은 user 1.2 s + **sys 3.6 s**, Vyukov 는 user 11.7 s + sys 0.15 s (실시간 1.75 s 로 비슷). 뮤텍스는 경합 때 기다리는 스레드를 커널에서 재우므로, 깨어 있는 소수 스레드가 락을 연달아 잡으며 큐 라인을 자기 캐시에 붙잡아 둔다 (추정: "재우기 = 자연스러운 배치"). Vyukov 는 12 스레드가 전부 깨어서 `enq_pos`/`deq_pos` 두 라인에 CAS 를 퍼붓는다 — 연산마다 코어 간 라인 이동 2 번 이상. 처리량 상한이 "라인 하나가 코어 사이를 오가는 속도" 로 묶여 ~11 Mops 에서 평평하다.
2. **메시지마다 일을 조금이라도 하면(W=200) Vyukov 가 3–4배 빨랐다.** 뮤텍스 판은 sys 16.6 s 로 커널 왕복이 폭증했다: 락 주인이 일하는 동안 락을 놓으니 매번 자는 스레드를 깨워 넘겨야 한다. Vyukov 는 아무도 자지 않으므로 큐 연산이 일과 겹친다. 실제 시스템은 W=0 이 아니다.
3. **대가: Vyukov 는 CPU 를 태운다.** 기다릴 때 돌기 때문에 W=0 에서 1.75 초 동안 코어 6.7 개분의 user 시간을 썼다. 배터리 기기·공유 서버에서는 그 자체가 비용이고, 비어 있을 때 잠들 방법(이벤트/세마포어를 느린 경로에만)이 따로 필요하다.
4. 슬롯 패딩(128 B)은 1×1 에서만 1.4배 — 생산자와 소비자가 이웃 슬롯(한 라인에 16 B 슬롯 8 개)을 동시에 만지는 false sharing 이 사라진다. 4×4 부터는 위치 CAS 가 지배해서 차이 없음. 메모리는 8 배. 패딩·배치의 정밀한 지연 측정은 SPSC 에서 LLAT-M01 이 맡는다.

결론: "lock-free 니까 빠르다" 는 틀린 말이다. 이점은 **처리량이 아니라 진행 성질**(멈춘 스레드가 막는 범위가 슬롯 하나) 과 **커널을 부르지 않는다는 것**(ISR·실시간 스레드에서 쓸 수 있음) 이다. 처리량은 측정하고 고른다.

### Part C — ISR 이벤트 큐: 기다리지 않는 쪽만 쓴다

`evq_post` 는 try 한 번 + 통계, `evq_drain` 은 실패할 때까지 꺼내기. 핵심은 시나리오다. 단일 코어 MCU 에서 ISR 이 main 을 선점하는 것은 "main 코드 한가운데서 ISR 함수가 **호출** 되는 것" 과 같다 — 그래서 `after_claim` 훅 안에서 `isr_on_claim()` 을 직접 부르면 스레드 없이 결정적으로 재현된다.

- main 이 pos 1 을 예약한 순간 ISR 진입 → ISR 은 pos 2, 3, 4 에 게시 성공 (생산자끼리는 서로 안 막는다).
- ISR 이 drain 을 시도 → pos 1 의 seq 가 아직 1 (≠ 2) → `NOT_READY` → **0 개, 즉시 반환**. 만약 여기서 "올 때까지 기다리기" 를 했다면 main 은 ISR 이 끝나야 돌 수 있으므로 **영원히** 돈다. 멀티코어의 "선점된 생산자가 소비자를 막는다" 가 단일 코어에서는 "막는다" 가 아니라 **교착** 이 된다. RTOS 에서 높은 우선순위 태스크가 돌며 기다리는 것도 같다 (우선순위 역전, see:RTOS-M03).
- ISR 이 끝나고 main 이 seq store → drain 하면 위치 순서대로 main 의 42, ISR 의 1000, 1001, 1002. ISR 의 이벤트가 먼저 "완료" 됐어도 main 것이 먼저 나온다.
- 8 칸 큐에서 main 이 1 칸 예약 중 ISR 폭주 10 개 → 7 개 성공 + **정확히 3 drop**. drop 카운터는 relaxed `fetch_add` (통계일 뿐 다른 데이터를 넘기지 않는다).

```text
== Part C: ISR event queue (nested preemption model) ==
PASS main_ok && g_isr_fired && g_isr_drained == 0u
PASS n == 4u && ev_src(out[0]) == 0u && (uint32_t)out[0] == 42u && (uint32_t)out[3] == 1002u
PASS main_ok && atomic_load(&g_ev.drops) - drops0 == 3u && n == 8u && (uint32_t)out[0] == 43u
```

펌웨어 현실 점검 (hedge 포함):

- **단일 코어 Cortex-M4** 라면 이 큐의 이점은 "ISR 끼리, ISR 과 main 이 인터럽트를 끄지 않고 동시에 게시" 하는 것 하나다. 그런데 인터럽트를 10 사이클 끄는 critical section 큐(C-M14)가 더 단순하고 최악 시간이 확실하다. M4 asm 에서 본 `dmb sy` 는 C11 의미를 지키려고 컴파일러가 넣은 것이고, 같은 코어 안의 ISR↔main 만이라면 하드웨어적으로는 필요 없다 (코어는 자기 접근을 프로그램 순서로 본다 — C-M14).
- **멀티코어** 가 진짜 무대다: 듀얼 Cortex-M7/M4 (STM32H7 dual-core — 공유 SRAM 은 M7 D-cache 때문에 non-cacheable MPU 영역 또는 캐시 유지가 필요하다고 알려져 있음, 세부 미검증 · DS-A01 / C-M14), Cortex-A + M 혼합 SoC, ESP32(Xtensa `S32C1I` CAS — 미검증). RP2040 은 M0+ 두 개라 CAS 가 없다 → SIO 하드웨어 스핀락/FIFO(OS-M07).
- **ISR 에서 쓰는 규칙**: try 한 번, 실패하면 drop 카운트(또는 ISR 전용 SPSC 로 분리). ISR 에서 소비는 하지 않는 게 기본이고, 해야 한다면 역시 try 한 번.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution        # 35/35 passed (6 회 반복, 회당 ~0.6 초)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o so_san && ./so_san   # 35/35, 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_tsan && ./so_tsan                                    # 35/35, 경고 0
DS_A03_MSGS=200000 ./so_tsan                                                                                         # 35/35, 경고 0 (4배 큰 스트레스)
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| head/tail 만 두고 `tail` 을 CAS 로 예약 | "예약됨" 과 "써짐" 을 구분 못 함 → 빈 칸 읽기, 메시지 분실 (`./db naive` 실측) | 슬롯마다 `seq`, 소비자는 `seq == pos+1` 일 때만 |
| `if (seq < pos)` 처럼 크기 비교 | wrap 순간 부호가 뒤집힘 | `(intptr_t)(seq - pos)` 의 부호 |
| dequeue 끝에 `seq = pos + 1` 또는 `pos + 2` | 다음 바퀴 생산자는 `seq == pos + cap` 을 기다림 → 영원히 가득 | `store(seq, pos + mask + 1, release)` |
| seq load/store 를 relaxed 로 | 이 맥에서 400만 개 중 8–19 개 중복·누락 (하드웨어 재배치), TSan 보고 | 생산·소비 모두 seq 는 acquire load / release store |
| 위치 CAS 를 `seq_cst`/`acq_rel` 로 "안전하게" | 틀리진 않지만 의미 없는 순서 비용. 데이터는 seq 로 넘어간다 | `memory_order_relaxed` 두 개 |
| CAS 실패 후 `pos` 를 다시 load | 실패한 CAS 가 이미 현재 값을 `pos` 에 넣었다. 불필요한 로드 | `CAS_weak(&enq_pos, &pos, pos+1, …)` 를 루프에서 그대로 |
| cap = 1 허용, 또는 2 의 거듭제곱 아님 | 1: "써짐" 과 "비었음" 이 같은 seq → 덮어쓰기 + livelock (`./db cap1`). 비 2^k: `pos & mask` 가 틀리고 wrap 에서 `%` 가 끊김 | `cap >= 2 && (cap & (cap-1)) == 0` |
| "Vyukov = lock-free" 라고 말하기 | CAS 와 seq store 사이에서 멈춘 생산자가 소비자를 막는다 (강제 스케줄 체크) | "lock-less, blocking at one slot" 이라고 정확히 |
| ISR 에서 큐가 빌/찰 때까지 돌며 기다리기 | 단일 코어에서는 기다리는 대상이 ISR 아래에 있어 영원히 못 옴 | try 한 번 + drop 카운터 |
| 스트레스 결과를 검증기 테스트 없이 믿기 | 검증기가 중복을 못 잡으면 "0 dup" 은 아무 의미 없다 | 손으로 만든 중복/역순 입력으로 검증기 먼저 체크 |

## 8. 면접 Q&A

<details><summary>Q1. MPMC 에서 Vyukov 방식은 왜 슬롯마다 시퀀스 번호를 두나요?</summary>

생산자가 여럿이면 위치 예약(CAS)과 데이터 쓰기가 두 단계로 나뉜다. 전역 head/tail 은 "몇 번까지 예약됐나" 만 알고 "그 칸이 다 써졌나" 는 모른다. 그래서 소비자가 예약만 된 빈 칸을 읽고, 반대쪽으로는 생산자가 아직 읽히는 중인 칸을 덮어쓴다 — 강제 스케줄로 둘 다 재현했다(0xDEAD 읽음, 100 분실, 10 대신 99). 슬롯마다 seq 를 두면 칸의 상태(생산 차례 pos / 소비 차례 pos+1 / 다음 바퀴 pos+N)를 칸이 직접 말한다. 위치 카운터는 번호표 분배만, seq 는 데이터 인계(release/acquire)만 맡는다. 꼬리: "seq 대신 칸마다 bool full 플래그면?" → 바퀴를 구분 못 해서, 낡은 pos 를 든 스레드가 다음 바퀴 칸을 자기 것으로 착각할 수 있다. seq 는 "몇 번째 바퀴의 누구 차례" 까지 담는다.
</details>

<details><summary>Q2. dif = seq − pos 의 세 경우를 설명하고, wrap 에서도 맞는 이유는?</summary>

생산자 기준: 0 = 빈 칸이고 내 차례 → CAS. 음수 = 한 바퀴 전 원소를 소비자가 아직 안 비움 = 가득 → false. 양수 = 다른 생산자가 이 pos 를 이미 가져가서 칸이 앞서 있다 → 내 pos 가 낡음 → 다시 읽기. 소비자는 기대값을 pos+1 로 해서 0 = 써짐, 음수 = 빔(또는 예약 후 쓰는 중), 양수 = 다른 소비자가 가져감. wrap: 부호 없는 뺄셈은 2^64 모듈로 정확하고, 실제 차이는 항상 ±N 정도라서 `intptr_t` 로 해석한 부호가 맞다 (`slot_case(2, SIZE_MAX-1) == STALE` 체크). 꼬리: "intptr_t 변환은 UB 아닌가요?" → 범위 밖 정수 변환은 UB 가 아니라 구현 정의(6.3.1.3p3), clang/gcc 는 모듈러로 정의. 엄격히 하려면 `seq - pos < SIZE_MAX/2` 로 비교해도 된다.
</details>

<details><summary>Q3. 각 원자 접근의 memory_order 를 말해 보세요. 위치 CAS 가 relaxed 여도 되는 이유는?</summary>

seq load = acquire (생산자: 전 바퀴 소비자의 읽기가 내 쓰기보다 먼저 / 소비자: 생산자의 쓰기를 봄), seq store = release (생산자: data → seq, 소비자: data 읽기 → seq). 위치 load·CAS = relaxed. 위치는 "같은 역할끼리 번호를 겹치지 않게 나누는 것" 이고, 겹치지 않음은 같은 원자 변수의 수정 순서만으로 보장된다 (RMW 는 마지막 값을 읽는다). 데이터의 가시성은 위치가 아니라 seq 의 release→acquire 로 넘어간다. relaxed 로 바꿔 깨지는 건 seq 쪽이다 — 이 맥에서 400만 개 중 8–19 개가 실제로 중복·누락됐고 asm 은 프로그램 순서 그대로였다(하드웨어 재배치). 꼬리: "acquire 대신 consume?" → 실질적으로 모든 컴파일러가 acquire 로 올린다.
</details>

<details><summary>Q4. lock-free 와 wait-free 의 차이는? Vyukov 큐는 lock-free 인가요?</summary>

wait-free: 모든 스레드의 모든 연산이 남과 상관없이 유한 단계에 끝남. lock-free: 계속 실행되는 한 시스템 전체로 누군가는 진행(개별 굶주림 허용). obstruction-free: 혼자 돌면 끝남. 핵심 시험: "아무 스레드나 아무 지점에서 무기한 멈췄을 때 다른 스레드가 여전히 연산을 끝낼 수 있나?" Vyukov 큐는 아니다: 생산자가 CAS 와 seq store 사이에서 멈추면 그 슬롯의 소비자는 영원히 못 받고, `try_dequeue` 는 다른 완료된 원소가 있어도 "비었음" 을 돌려준다 (선형화 불가). 강제 훅으로 결정적으로 보였다. 다만 막는 범위가 슬롯 하나라 뮤텍스(전체를 막음)보다 훨씬 낫다. 꼬리: "진짜 lock-free bounded 큐는?" → 예약과 게시를 원자 연산 하나로 하거나(CRQ: FAA + 128비트 CAS 슬롯 — Morrison & Afek 2013, 이걸 연결한 LCRQ 는 unbounded), 남의 미완료 연산을 도와주는(helping) 설계가 필요하다. Michael–Scott 큐는 lock-free 지만 unbounded 이고 메모리 회수(LLAT-M03 hazard pointer)가 필요하다.
</details>

<details><summary>Q5. 이 큐는 FIFO 인가요? 생산자별 순서가 보장되나요?</summary>

위치 순서 FIFO 다: 위치 p 의 메시지는 위치 p 의 소비자에게 간다. 한 생산자의 연속 enqueue 는 증가하는 위치를 얻고(같은 변수 RMW 의 수정 순서 + 프로그램 순서), 한 소비자의 연속 dequeue 도 증가하는 위치를 얻으므로 **같은 소비자 안에서** 생산자별 순서는 항상 유지된다 — 증명했고 4 개 설정 × 6 회 스트레스 CHECK 로도 0 위반. 소비자들 사이의 처리 순서는 보장 없음. 그리고 "빔" 이라는 답은 선형화되지 않는다(Q4). 꼬리: "전역 순서가 필요하면?" → 소비자 하나(MPSC)로 하거나, 메시지에 시퀀스를 달아 재정렬.
</details>

<details><summary>Q6. compare_exchange_weak 와 strong 중 무엇을, 왜? arm64 에선 무슨 명령이 나오나요?</summary>

이 CAS 는 재시도 루프 안이라 weak 가 맞다 — 가짜 실패해도 seq 를 다시 보고 다시 시도하면 된다. 실제 asm: Apple 기본(LSE)에서는 weak/strong 모두 `cas` 한 명령으로 같다. `-mcpu=apple-m1+nolse` 에서는 weak = `ldxr/stxr` 한 번, strong = `stxr` 실패 시 `cbnz` 루프. Cortex-M4 도 같은 구조(`ldrex/strex`), seq acquire/release 는 `ldr`+`dmb sy` / `dmb sy`+`str`. M0+ 는 전부 `__atomic_*_4` libcall. 성능: ARM-M04 실측으로 경합 CAS 루프는 LSE `casal` 이 LL/SC 보다 2–4배 느렸지만, 큐 전체로는 4×4 에서 10.9 vs 11.0 Mops 로 차이 없음을 확인했다. 꼬리: "strong 이 필요한 곳은?" → 루프 없이 한 번 시도하고 결과로 분기할 때 (예: 연습 3 의 ticket 판은 CAS 대신 fetch_add 를 쓴다).
</details>

<details><summary>Q7. 그래서 뮤텍스 큐보다 빠른가요?</summary>

이 맥 실측(load 7–9, 비율만): 큐만 두드리는 벤치(W=0)에서는 4×4·6×6 에서 pthread_mutex 링이 18.1/15.0 Mops 로 Vyukov(10.9/11.8)보다 빨랐다. 메시지마다 ~50 ns 일을 하면(W=200) Vyukov 11 Mops vs 뮤텍스 3.6 — 3배 역전. 뮤텍스는 sys 시간이 폭증(16.6 s)했다: 기다리는 스레드를 재우고 깨우는 커널 왕복. Vyukov 는 커널을 안 부르지만 기다리는 동안 CPU 를 태운다(user 11.7 s / 실시간 1.75 s). 그러니 답은 "워크로드에 따라 다르고, lock-free 의 진짜 이점은 진행 성질·커널 미사용·ISR 사용 가능성" 이다. 꼬리: "Vyukov 처리량이 ~11 Mops 에서 평평한 이유?" → 모든 생산자가 `enq_pos` 한 라인, 모든 소비자가 `deq_pos` 한 라인에 CAS → 라인 이동 속도가 상한 (추정). 개선: 샤딩(큐 여러 개), 배치 dequeue, 생산자별 SPSC + 소비자 폴링.
</details>

<details><summary>Q8. 슬롯 seq 말고 다른 MPMC 설계는요? DPDK rte_ring 은 어떻게 하죠?</summary>

DPDK `rte_ring` (알려진 설계, 세부 미검증): 생산자 쪽에 `prod.head`(예약)와 `prod.tail`(완료) 두 카운터. 예약은 head CAS, 쓰기 후 **자기 앞 생산자가 tail 을 올릴 때까지 기다렸다가** tail 을 순서대로 올린다. 슬롯 상태 없이 "완료" 를 표현하지만, 앞 생산자가 멈추면 뒤 생산자도 멈춘다(역시 blocking), 대신 bulk enqueue 에 강하다. rigtorp/MPMCQueue: 위치를 fetch_add 로 받고 슬롯 turn 을 기다림(연습 3) — CAS 재시도는 없지만 try 가 안 된다. Michael–Scott 연결 리스트 큐: lock-free, unbounded, 메모리 회수 필요. LCRQ: FAA + 128비트 CAS 로 만든 링(CRQ)을 리스트로 이어 붙인 lock-free 큐 (unbounded). Rust crossbeam `ArrayQueue` 는 Vyukov 방식 기반으로 알려져 있다 (미검증).
</details>

<details><summary>Q9. ISR 이나 RTOS 에서 이 큐를 써도 되나요?</summary>

게시(try enqueue)는 된다 — 락이 없으니 ISR 이 main 을 선점해도 교착하지 않고, 생산자끼리는 서로 막지 않는다 (Part C: ISR 이 main 의 예약 창 안에서 3 개 성공). 금지: ISR 이나 높은 우선순위 태스크에서 "빌/찰 때까지 돌며 기다리기". 기다리는 대상(선점된 낮은 쪽)이 그동안 못 돌아서 단일 코어에서는 영원히 끝나지 않는다 (Part C 의 drain 은 기다리지 않고 0 을 받았다). 가득 차면 drop 카운트. 하드웨어 제약: CAS 가 필요하므로 ARMv7-M 이상(M3/M4/M7/M33). M0/M0+ 는 libcall(→ 보통 IRQ 끄기)이라 의미가 없다. 단일 코어라면 짧은 IRQ-off 큐(C-M14)가 더 단순하고 최악 시간이 확실하다.
</details>

<details><summary>Q10. cap 은 왜 2 의 거듭제곱이고 1 은 왜 안 되나요?</summary>

2^k 이면 `pos & mask` 가 칸 번호이고, 2^64 가 cap 의 배수라서 `size_t` 가 wrap 해도 "pos 와 칸" 의 대응이 끊기지 않는다. cap = 1 이면 "pos 의 생산자가 씀" 을 뜻하는 pos+1 과 "다음 바퀴 생산자 차례" 를 뜻하는 pos+cap 이 같은 숫자라, 두 번째 enqueue 가 아직 안 읽힌 칸을 덮어쓰고 소비자는 dif = +1 을 영원히 본다 (`./db cap1` 실제 출력). 그래서 cap ≥ 2.
</details>

## 9. 추가 연습

### 연습 1 — 대략 크기 `mpmc_size_approx`

모니터링용으로 "지금 몇 개 들어 있나" 를 락 없이 돌려준다. 결과가 항상 `[0, cap]` 안이어야 한다.

<details><summary>답</summary>

`d = load(deq_pos)` 를 먼저, `e = load(enq_pos)` 를 나중에 relaxed 로 읽고 `(intptr_t)(e - d)` 를 0 과 cap 으로 자른다. 두 로드 사이에 다른 스레드가 움직이므로 원시 차이는 범위 밖이 될 수 있다 (두 로드 사이에 생산자·소비자가 모두 움직이면 cap 초과, relaxed 로드가 낡은 enq 를 주면 음수. 순서를 거꾸로 enq → deq 로 읽으면 음수가 흔해진다) — 4×4 cap 16 스트레스 중 생산자가 매번 재 보니 원시 차이가 범위 밖인 경우가 실제로 10 번 나왔다 (관찰값, 실행마다 다름). 자른 결과는 0 번 범위 밖. 이 값은 "그 무렵의 크기" 일 뿐이라 흐름 제어(로드 셰딩) 힌트로만 쓰고, 정확성 판단(비었나?)에는 쓰지 않는다 — 그건 try_dequeue 의 몫.
</details>

### 연습 2 — 임의 시작 위치 `mpmc_init_at` (wrap 테스트)

64비트 위치는 현실에서 wrap 하지 않으므로 wrap 코드는 테스트되지 않은 채 남기 쉽다. 위치를 `SIZE_MAX - 5` 에서 시작하게 만들어 5 바퀴(20 위치) 동안 `SIZE_MAX → 0` 을 통과시키고, `SIZE_MAX - 1000` 에서 4×4 스트레스도 돌린다.

<details><summary>답</summary>

슬롯 i 의 첫 seq = "start 이상에서 처음으로 i 에 대응하는 위치" = `start + ((i - start) & mask)`. 부호 없는 산술이라 `i - start` 가 wrap 해도 맞다. 체크: `enq_pos == start + 20 == 14` (2^64 모듈로), 바퀴마다 가득·빔 경계 정확, 스트레스 정확히 한 번 + 위치 = start + n. 32비트 MCU 에서는 `size_t` 가 32비트라 4G 연산 뒤 실제로 wrap 한다 — 초당 100만 이벤트면 약 72 분이므로 이 테스트가 필수다.
</details>

### 연습 3 — ticket 판: CAS 대신 fetch_add (rigtorp/MPMCQueue 의 push/pop 모양)

`tq_push`: `pos = fetch_add(&enq_pos, 1)` 로 위치를 **무조건** 받고, 그 슬롯의 seq 가 pos 가 될 때까지 기다렸다가 쓰고 `seq = pos+1`. `tq_pop` 대칭. 4×4 cap 1024 와 cap 2 에서 정확히 한 번.

<details><summary>답</summary>

CAS 재시도가 없다(경합 시 `fetch_add` = LSE `ldadd` 한 번). 대신 번호표를 뽑은 순간 그 칸 차례를 **반드시** 기다려야 하므로 try 가 불가능하고, 가득/빔이면 블록된다. 진행 성질은 Vyukov 와 같다 — 앞 바퀴의 주인이 멈추면 기다린다. 장점은 대기 순서가 공정(FIFO 번호표, OS-M07 의 ticket lock 과 같은 생각), 단점은 소비자가 원소가 없는 위치까지 미리 예약해 버린다는 것 (종료 시 깨워 줄 방법 필요 — 테스트는 소비자마다 정확한 개수만 pop). rigtorp 구현의 `try_push` 는 이 문제 때문에 다시 CAS 를 쓴다 (알려진 구조, 미검증). 위치 순서 FIFO 증명은 본문과 같아서 생산자별 순서 체크도 그대로 통과.
</details>

### 연습 4 — 32 B 레코드 페이로드 (비원자 memcpy)

`uint64_t` 대신 `{producer, seqno, ts, payload[3], checksum}` 32 B 구조체를 `memcpy` 로 넣고 꺼낸다. 4×4 스트레스에서 체크섬 불일치(찢어진 레코드) 0, TSan 경고 0 이어야 한다.

<details><summary>답</summary>

슬롯의 데이터는 원자일 필요가 없다 — seq 의 release/acquire 가 "쓰기 전체 → 읽기 전체" 를 happens-before 로 묶기 때문이다. 그래서 C-A03 의 seqlock 류와 달리 재시도 없이 아무 크기나 복사할 수 있다 (대가: 레코드만큼 슬롯이 커짐). TSan 이 경고 0 인 것은 이 순서를 TSan 이 모델링하는 acquire/release 로 표현했기 때문이다 (독립 fence 로 했다면 Apple TSan 이 오탐 — MCPP-M13). 체크: torn 0, 순서 위반 0, 누락·중복 0.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o pr_san && ./pr_san
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pr_tsan && ./pr_tsan
```

실제 출력: `14/14 passed` (세 빌드 모두, TSan 경고 0, 일반 빌드 0.55 초).

## 10. 핵심 정리

- MPMC 의 어려움: 위치 예약(CAS)과 데이터 쓰기가 분리된다. head/tail 한 쌍은 "예약됨" 과 "써짐" 을 구분 못 한다 → 슬롯마다 seq (생산 차례 pos / 소비 차례 pos+1 / 다음 바퀴 pos+cap).
- `dif = (intptr_t)(seq - 기대값)`: 0 = 내 차례 → CAS, 음수 = 가득/빔 → false, 양수 = 낡은 위치 → 다시 읽기. 생산자 기대값 pos, 소비자 pos+1. 부호 없는 뺄셈이라 wrap 안전.
- seq = acquire load / release store (양쪽 모두), 위치 load·CAS = relaxed, data 는 일반 변수. relaxed seq 는 이 맥에서 400만 개 중 8–19 개 실제 손상, asm 은 순서 그대로 → 하드웨어 재배치.
- 정확히 한 번 = 위치 유일(수정 순서) + 위치 p 소비자 ↔ 위치 p 생산자. 소비자마다 생산자별 순서 증가는 증명 가능하고 CHECK 가능. "빔" 답은 선형화 불가.
- 엄밀한 lock-free 아님: CAS 와 seq store 사이에서 멈춘 생산자가 그 슬롯 소비자를 무기한 막는다 (강제 훅으로 결정적 재현). 막는 범위는 슬롯 하나.
- cap 은 2^k 이고 ≥ 2 (cap 1 = 덮어쓰기 + livelock). CAS 는 루프 안이라 weak — LSE 에선 weak = strong = `cas`, LL/SC 에선 strong 만 루프.
- 처리량: 큐만 두드리면 뮤텍스가 1.3–1.7배 빠름(재우기 = 배치), 메시지당 ~50 ns 일이 있으면 Vyukov 가 3배 빠름. Vyukov 는 기다리며 CPU 를 태운다. 비율로 읽고 측정해서 고른다.
- 펌웨어: ISR 은 try 한 번 + drop 카운트, 절대 기다리지 않는다 (단일 코어에서는 교착). CAS 필요 → M3 이상, M0+ 는 libcall. 단일 코어면 IRQ-off 큐가 더 단순할 수 있다.

## 참고자료

- D. Vyukov, "Bounded MPMC queue", 1024cores.net — 원 알고리즘 (C++, `sequence_`, `enqueue_pos_`, `dequeue_pos_`, `buffer_size >= 2` assert): <https://www.1024cores.net/home/lock-free-algorithms/queues/bounded-mpmc-queue>
- M. Herlihy, N. Shavit, *The Art of Multiprocessor Programming* — 3장 선형화 가능성·진행 조건(wait-free, lock-free, obstruction-free), 10장 큐
- M. Herlihy, "Wait-free synchronization", ACM TOPLAS 13(1), 1991 / M. Herlihy, V. Luchangco, M. Moir, "Obstruction-free synchronization", ICDCS 2003
- M. M. Michael, M. L. Scott, "Simple, Fast, and Practical Non-Blocking and Blocking Concurrent Queue Algorithms", PODC 1996
- A. Morrison, Y. Afek, "Fast Concurrent Queues for x86 Processors" (LCRQ), PPoPP 2013
- E. Rigtorp, MPMCQueue (ticket/turn 방식 bounded MPMC): <https://github.com/rigtorp/MPMCQueue>
- DPDK Programmer's Guide, "Ring Library" (head/tail 두 단계 생산자·소비자): <https://doc.dpdk.org/guides/prog_guide/ring_lib.html>
- C11 (N1570): 5.1.2.4 (happens-before, synchronizes-with, 데이터 레이스, coherence), 6.3.1.3 (정수 변환 — 범위 밖은 구현 정의), 7.17.3 (memory_order, RMW 는 수정 순서의 마지막 값), 7.17.7.4 (compare_exchange weak/strong, 가짜 실패): <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- Arm Architecture Reference Manual for A-profile (DDI 0487) — `CAS`(FEAT_LSE), `LDXR/STXR`, `LDAPR`(FEAT_LRCPC), `STLR`; ARMv7-M ARM (DDI 0403) — `LDREX/STREX`, `DMB`; ARMv6-M ARM (DDI 0419) — 배타 접근 명령 없음
- 관련 레슨: DS-A01 (SPSC 링, acquire/release 인덱스), C-A03 (1칸 채널, CAS weak/strong), ARM-M04 (LL/SC vs LSE, `casal` 경합 실측), OS-M07 (스핀락, ticket), OS-B06 (condvar bounded buffer), C-M14 (ISR 공유 변수, IRQ-off), CPP-A04 (M0+ 에 LDREX 없음), MCPP-M13 (fence 와 TSan 오탐), LLAT-M01 (SPSC 지연 튜닝: 패딩·배치), LLAT-M03 (hazard pointer), DS-A04 (ABA, Treiber 스택), OS-A05 (Chase–Lev deque), RTOS-M03 (우선순위 역전)
