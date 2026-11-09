---
id: OS-A05
time: 115분
updated: 2026-11-08
---
# OS-A05 work-stealing 스레드 풀: Chase-Lev deque

## 학습 목표

- Chase–Lev deque 를 C11 형태(Lê, Pop, Cohen, Zappa Nardelli, PPoPP 2013)로 짠다. 주인(owner)은 bottom 쪽에서 `push`/`take`, 도둑(thief)은 top 쪽에서 `steal`(CAS) 한다. 원자 연산 **하나하나의 memory_order 를 이유와 함께** 말할 수 있다. 특히 `take` 와 `steal` 의 `seq_cst` 펜스가 막는 것이 SB(store buffering) 모양이라는 것을 안다.
- 마지막 한 개를 두고 `take` 와 `steal` 이 겨루는 경우를 **강제 스케줄**(hook)로 결정적으로 재현한다. 주인이 이기는 경우, 도둑이 이기는 경우, 펜스가 빠진 경우를 각각 만들고, 일부러 고장 낸 `take` 가 같은 스케줄에서 항목을 **두 번** 내준다는 것까지 CHECK 로 확인한다.
- 이 맥(Apple M6)에서 펜스를 뺀 `take` 가 **실제 하드웨어에서도** 중복을 내는지 직접 잰다. 1000만 판에 11 / 40 / 23 회 나왔고, 펜스 판과 seq_cst 연산 판은 0 이었다.
- 워커마다 deque 를 하나씩 둔 스레드 풀을 만든다. spawn/join(기다리는 동안 남의 일을 돕는 join), 무작위 희생자 steal, pending 카운터로 종료를 판정한다. 재귀 병렬 합과 fib 에서 결과가 정확하고 모든 태스크가 정확히 한 번 실행되는지 검증한다.
- 1~12 워커에서 **전역 뮤텍스 큐** 와 확장성을 비교한다. "전역 카운터 하나가 lock-free deque 의 이득을 다 잡아먹는다" 는 것도 숫자로 확인한다 (잘게 쪼갠 fib 에서 12 워커 0.54× → pending 카운터를 없애면 7.0×).

## 1. 문제

"스레드 풀을 짜 보세요. 태스크가 다른 태스크를 낳는 재귀 작업이라면 어떻게 나눠 주죠?" 이 질문 뒤에는 거의 항상 **work-stealing 과 Chase–Lev deque** 가 나온다. Cilk, Intel TBB, Java ForkJoinPool, Rust Rayon, Go 런타임, Tokio 가 모두 이 아이디어를 쓴다(세부는 5. 풀이 끝의 표). 임베디드에서는 Jetson·i.MX8 같은 Linux SoC 의 인식(perception) 파이프라인과 병렬 라이브러리 밑단에서 만난다. lock-free 자료구조 가운데 **SPSC(DS-A01) 다음으로 면접에 많이 나오는 것** 이 이 구조다.

동시성 가이드 5층의 레슨이다. 재료는 이미 다 있다. C-A03 은 release/acquire 게시와 CAS 를, MCPP-M13 은 펜스 동기화 규칙을, ARM-M05 는 이 칩에서 SB 재배치가 실제로 보인다는 것을(`str ; ldr` 2000만 회 중 최대 1717 회) 다뤘다. DS-A01(SPSC)·DS-A03(Vyukov MPMC)은 같은 층에서 따로 작성된다. 이 레슨의 새 내용은 **"한쪽 끝은 주인 혼자, 다른 쪽 끝은 여러 도둑"** 이라는 비대칭 구조다. 주인 쪽 연산은 거의 공짜(RMW 없음)이고, 비싼 CAS 는 도둑과 "마지막 한 개" 에만 남긴다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">워커마다 deque 하나 — 주인은 아래(bottom), 도둑은 위(top)</text>
<rect x="30" y="40" width="200" height="200" rx="6" class="f-mut" stroke="currentColor"/>
<text x="40" y="60" fill="currentColor" font-weight="bold">워커 0 의 deque</text>
<rect x="60" y="72" width="140" height="28" class="f-ok" stroke="currentColor"/><text x="70" y="91" fill="currentColor">[t] 가장 오래됨 (큰 일)</text>
<rect x="60" y="100" width="140" height="28" class="f-ok" stroke="currentColor"/><text x="70" y="119" fill="currentColor">[t+1]</text>
<rect x="60" y="128" width="140" height="28" class="f-ok" stroke="currentColor"/><text x="70" y="147" fill="currentColor">[t+2]</text>
<rect x="60" y="156" width="140" height="28" class="f-acc" stroke="currentColor"/><text x="70" y="175" fill="currentColor">[b-1] 가장 최근 (작은 일)</text>
<rect x="60" y="184" width="140" height="28" fill="none" stroke="currentColor" stroke-dasharray="4 3"/><text x="70" y="203" fill="currentColor">[b] 다음 push 자리</text>
<text x="210" y="91" fill="currentColor">← top</text>
<text x="210" y="203" fill="currentColor">← bottom</text>
<line x1="300" y1="86" x2="240" y2="86" stroke="currentColor" stroke-width="2"/>
<text x="300" y="80" fill="currentColor">steal: 도둑 여럿, top 에 CAS</text>
<text x="300" y="98" fill="currentColor">(FIFO 쪽 = 쪼개지지 않은 큰 덩어리를 가져감)</text>
<line x1="300" y1="190" x2="240" y2="190" stroke="currentColor" stroke-width="2"/>
<text x="300" y="186" fill="currentColor">push / take: 주인 한 명, bottom 은 주인만 씀</text>
<text x="300" y="204" fill="currentColor">(LIFO 쪽 = 방금 만든 일, 캐시가 따뜻함, RMW 없음)</text>
<text x="300" y="140" fill="currentColor">마지막 한 개(t == b-1)일 때만 주인도 top 에 CAS → 결판</text>
<rect x="30" y="262" width="80" height="36" rx="5" class="f-acc" stroke="currentColor"/><text x="42" y="285" fill="currentColor">워커 0</text>
<rect x="130" y="262" width="80" height="36" rx="5" class="f-ok" stroke="currentColor"/><text x="142" y="285" fill="currentColor">워커 1</text>
<rect x="230" y="262" width="80" height="36" rx="5" class="f-ok" stroke="currentColor"/><text x="242" y="285" fill="currentColor">워커 2</text>
<rect x="330" y="262" width="80" height="36" rx="5" class="f-warn" stroke="currentColor"/><text x="340" y="285" fill="currentColor">워커 3 (빔)</text>
<path d="M370 262 C 340 225, 120 225, 80 258" fill="none" stroke="currentColor" stroke-dasharray="5 3"/>
<text x="430" y="275" fill="currentColor">비면 무작위 희생자에게서 steal.</text>
<text x="430" y="293" fill="currentColor">일이 저절로 노는 쪽으로 흐른다 (P/E 혼합도).</text>
<text x="10" y="322" fill="currentColor">전역 큐 하나 + 뮤텍스: 모든 spawn·꺼내기가 같은 락과 같은 캐시 라인을 지나간다 → 5. Part C 의 0.07×.</text>
</svg>
```

| 연산 | 누가 | 어느 끝 | 핵심 원자 연산 | 비용 (arm64 실제 명령, `demo_asm.c`) |
|---|---|---|---|---|
| `push` | 주인 | bottom | top `acquire` 읽기, 슬롯 relaxed 쓰기, bottom `release` 쓰기 | `ldapr` + `str` + `stlr` (RMW 없음) |
| `take` | 주인 | bottom | bottom 감소 쓰기 → **`seq_cst` 펜스** → top 읽기, 마지막 하나면 CAS | `str` + `dmb ish` + `ldr` (+ `casal`) |
| `steal` | 아무나 | top | top `acquire` → **`seq_cst` 펜스** → bottom `acquire`, 슬롯 읽기, top CAS | `ldapr` + `dmb ish` + `ldapr` + `casal` |

### Part A — Chase–Lev deque (고정 크기 배열)

- `deque_t` 는 `top`, `bottom`(부호 있는 64비트, 각각 다른 128 B 라인), `_Atomic(task_t *) buf[256]` 로 이뤄진다. 슬롯도 atomic(relaxed)로 둔다. 그래야 TSan 이 슬롯 재사용을 레이스로 잘못 보고하지 않는다.
- `dq_push` (A1, 가득 차면 false), `dq_take` (A2, 비면 `DQ_EMPTY`), `dq_steal` (A3, `DQ_EMPTY` / `DQ_ABORT` / 태스크). 강제 스케줄용 `HOOK` 은 정해진 두 자리에 둔다. 하나는 take 에서 top 을 읽은 직후, 다른 하나는 steal 에서 CAS 바로 앞이다.
- 단위 테스트: 주인은 LIFO, 도둑은 FIFO 로 꺼낸다. 빈 take 는 bottom 을 되돌린다. 가득 참과 그 복구를 확인하고, 인덱스가 배열을 10 바퀴 돌아도 순서가 맞는지 본다.

### Part B — 마지막 한 개: 강제 스케줄과 고장 낸 take

- **S1 (주인 승)**: 도둑이 top/bottom/슬롯을 읽고 CAS 직전에 멈춘다. 그 사이 주인이 take 를 끝까지 한다. 결과는 주인 X, 도둑 `ABORT`.
- **S2 (도둑 승, 실제 스레드 2 개)**: 도둑이 CAS 직전에 멈춘다. 주인은 top 을 읽은 뒤 멈춘다. 이 상태에서 도둑이 CAS 하고, 그다음 주인이 CAS 한다. 결과는 도둑 X, 주인 `EMPTY`.
- **S3 (펜스가 지키는 것)**: 항목 A, B 가 있다. 주인이 top 을 읽은 직후 도둑이 steal 을 두 번 한다. 결과는 도둑 A, 주인 B 이고, 도둑의 두 번째 steal 은 `EMPTY` 다.
- **이빨 확인**: 주어진 `dq_take_nocas`(마지막 하나에서 CAS 생략)는 S1 에서, `dq_take_reordered`(펜스가 없을 때 하드웨어가 허용하는 순서를 소스에 그대로 적은 판)는 S3 에서 같은 항목을 두 번 내줘야 한다.
- 실제 스레드 스트레스: 주인 1 + 도둑 3, 항목 20만 개. 모두 정확히 한 번 소비돼야 한다.
- 데모: `demo_sb.c`(펜스를 뺐을 때 실제 하드웨어 중복 횟수), `demo_tsan.c`(release/acquire 를 빼면 TSan 이 무엇을 잡나), `demo_asm.c`(명령 표).

### Part C — 스레드 풀: spawn / join / steal / 종료

- `task_spawn` (C1): pending 을 올리고 push 한다. 가득 차면 그 자리에서 직접 실행한다. `find_work` (C2): 먼저 내 deque, 그다음 무작위 희생자(나 제외)에게서 최대 n−1 번 steal. `task_join` (C3): 자식의 `done` 이 보일 때까지 남의 일을 돕는다.
- 워크로드: `fib_task` (C4, cutoff 이하면 직렬, fib(n−1) 은 spawn 하고 fib(n−2) 는 직접 실행), `sum_task` (C5, 2^20 개 원소, grain 4096, 원소별 방문 카운터).
- CHECK: 1 / 8 워커에서 fib 결과 정확, spawned == executed, 모든 자식 `runs == 1`, 끝난 뒤 pending == 0. 1 / 6 워커에서 psum 결과 정확, 모든 원소 방문 1 회. 자식 600 개를 한꺼번에 spawn 하는 경우 1 워커에서 넘침이 정확히 344 = 600 − 256 이어야 한다.
- 데모: `demo_bench.c`. 스케줄러 3 종(Chase–Lev / 워커별 뮤텍스 deque / 전역 뮤텍스 큐) × 1~12 워커 × 워크로드 3 종, 종료 판정 2 종.

## 2. Problem (English)

> Implement a Chase–Lev work-stealing deque in C11 (`<stdatomic.h>`), fixed capacity 256, following Lê et al., PPoPP 2013:
>
> - `bool dq_push(deque_t *q, task_t *x)` — owner only; returns false when full.
> - `task_t *dq_take(deque_t *q)` — owner only; LIFO end; returns `DQ_EMPTY` if empty.
> - `task_t *dq_steal(deque_t *q)` — any thread; FIFO end; returns a task, `DQ_EMPTY`, or `DQ_ABORT` (lost a race).
>
> Justify every memory order. Show the race on the last element between `take` and `steal` with deterministic forced schedules (owner wins / thief wins), and show that removing the `seq_cst` fence in `take` (or skipping the CAS on the last element) yields a duplicated task under a schedule of your choice.
>
> Then build a work-stealing thread pool: per-worker deques, random victim selection, `spawn`/`join` where a joining worker keeps executing other tasks, and termination detection. Verify with recursive parallel sum and fib with a cutoff: exact results, every task executed exactly once, no task left behind.
>
> Follow-ups: scalability for 1..12 workers versus a single mutex-protected global queue; what does each operation compile to on arm64 and Cortex-M4; is the deque lock-free or wait-free; what changes for a growable array?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행하면 아래처럼 나온다 (실제 출력 일부, 0.1 초). Part A 단위 테스트가 실패하면 스트레스와 풀은 건너뛰므로 멈추지 않는다. 강제 스케줄의 대기에는 2 초, 풀 실행에는 2 초 마감이 있다.

```text
PASS dq_take(q) == DQ_EMPTY && dq_steal(q) == DQ_EMPTY
PASS atomic_load(&q->bottom) == 0 && atomic_load(&q->top) == 0
FAIL starter.c:465  dq_push(q, A) && dq_push(q, B) && dq_push(q, X) && dq_size(q) == 3
FAIL starter.c:466  dq_take(q) == X
…
FAIL starter.c:500  rs[1] == X && rs[0] == DQ_ABORT && count_of(X, rs, 2) == 1
…
SKIP 1 stress check (Part B): Part A unit checks failed
SKIP 9 pool checks (Part C): Part A unit checks failed
3/19 passed
```

TODO 는 A1–A3(deque), C1–C5(spawn, find_work, join, fib, psum) 다. 고장 낸 take 두 개, gate, 워커 루프, `run_task`, `pool_run` 은 주어진다. Part A 만 풀고 C 를 비워 두면 풀 테스트 6 개가 각각 2 초 마감 뒤 FAIL 한다 (멈추지 않는다, 총 약 12 초).

## 4. 힌트

<details><summary>힌트 1 — 누가 무엇을 쓰나</summary>

`bottom` 은 주인만 쓴다. 그래서 주인이 자기 bottom 을 읽을 때는 relaxed 면 된다(내가 마지막에 쓴 값). `top` 은 도둑들이 CAS 로, 마지막 하나일 때는 주인도 CAS 로 올린다. top 은 **줄어들지 않는다**. 크기는 `bottom - top` 이다.
</details>

<details><summary>힌트 2 — take 는 "먼저 예약, 그다음 확인"</summary>

take 는 bottom 을 먼저 1 내려서 "이 칸은 내가 가져간다" 고 알린다. 그런 다음 top 을 읽는다. 도둑은 반대로 top 을 먼저 읽고 bottom 을 나중에 읽는다. 양쪽 모두 "내 쓰기 → 상대 변수 읽기" 순서이고, 이것은 store→load 다. ARM 뿐 아니라 x86(TSO)에서도 이 순서는 지켜지지 않는다. 그래서 둘 사이에 `atomic_thread_fence(memory_order_seq_cst)` 가 들어간다.
</details>

<details><summary>힌트 3 — take 의 세 경우</summary>

`t < b`: 둘 이상 남았으니 그냥 내 것이다. `t == b`: 마지막 하나라서 도둑과 top 을 두고 CAS 로 결판낸다. 이기든 지든 bottom 을 `b + 1` 로 돌려 빈 상태(`top == bottom`)를 만든다. `t > b`: 원래 비어 있었으니 bottom 을 되돌린다. 인덱스가 부호 있는 정수여야 `b = -1` 이 `t = 0` 보다 작다.
</details>

<details><summary>힌트 4 — steal 은 CAS 전에 슬롯을 읽는다</summary>

CAS 에 성공한 순간 그 칸은 주인이 다시 쓸 수 있다. 그러므로 값은 **먼저** 읽어 두고, CAS 가 실패하면 그 값을 버린다(`DQ_ABORT`). CAS 실패는 "다른 누군가가 성공했다" 는 뜻이다.
</details>

<details><summary>힌트 5 — join 이 놀면 안 되는 이유와 종료</summary>

join 하는 워커가 그냥 돌며 기다리면, 워커 수만큼 부모가 동시에 기다리는 순간 모두 멈춘다. 자식이 남의 deque 에 있을 수 있기 때문이다. 기다리는 동안 `find_work` 로 아무 일이나 실행해야 한다. 종료는 `pending` 으로 판정한다. spawn 할 때 +1, 실행을 마칠 때 −1 이다. 부모는 자식을 spawn 한 **다음에** 자기 −1 을 하므로, 일이 남아 있는 동안 pending 이 잠깐이라도 0 이 될 수 없다.
</details>

## 5. 풀이

### Part A — 연산마다 memory_order 와 그 이유

| 연산 · 줄 | order | 왜 (빼면 무엇이 깨지나) |
|---|---|---|
| push: `bottom` 읽기 | relaxed | 주인만 쓰는 변수다. 내가 마지막에 쓴 값을 읽는다 (coherence). |
| push: `top` 읽기 | **acquire** | 슬롯 재사용(WAR) 때문이다. 도둑은 슬롯 t 를 **읽은 뒤** CAS 로 top 을 t+1 로 만든다. 주인이 top = t+1 을 보고 슬롯 (t+CAP)&MASK 를 덮어쓰려면, 그 읽기가 덮어쓰기보다 happens-before 여야 한다. relaxed 면 LB(load buffering) 모양이 허용된다. 도둑이 **새** 값을 읽고 CAS 에 성공하면, 옛 태스크는 사라지고 새 태스크는 두 번 실행된다. |
| push: 슬롯 쓰기 | relaxed | 게시는 다음 줄의 release 가 맡는다. 슬롯을 atomic 으로 둔 이유는 TSan 오탐을 막는 것뿐이다. |
| push: `bottom = b+1` | **release** | 도둑이 bottom 을 acquire 로 읽으면 슬롯 내용과 **태스크 구조체의 일반 필드** 가 보인다. 빼면 `demo_tsan.c` 처럼 태스크 필드에서 데이터 레이스가 난다. |
| take: `bottom = b-1` | relaxed | 순서는 다음 줄의 펜스가 만든다. |
| take: **펜스** | **seq_cst** | SB 를 금지한다. 아래 그림의 S3 를 보라. |
| take: `top` 읽기 | relaxed | 펜스 뒤에 있으니 충분하다. |
| take/steal: top CAS | seq_cst / 실패 relaxed | 마지막 하나를 두 쪽 중 하나만 갖게 한다. top 의 수정 순서가 하나라서 둘 중 정확히 하나만 성공한다. |
| steal: `top` 읽기 | acquire | 논문 형태를 따랐다. 고정 배열 판에서 이것이 꼭 필요한지는 이 레슨에서 증명하지 않았다. arm64 에서는 `ldapr` 로 나오고 비용은 작다. |
| steal: **펜스** | **seq_cst** | 도둑 쪽 SB 를 막는다. 앞선 CAS(top 쓰기)와 bottom 읽기 사이의 순서다. |
| steal: `bottom` 읽기 | acquire | push 의 release 와 짝을 이룬다. |

**push 의 top acquire 가 막는 것 — 슬롯 재사용 (LB 모양).** CAP = 4 로 줄여서 보면 이렇다.

```text
상태: top = 5, bottom = 9 (가득 참), 슬롯 5&3 = 1 번 칸에 태스크 P
도둑                                   주인
 t = 5, b = 9                           (가득이라 push 를 못 하고 기다리는 중)
 x = buf[1]            ← ①  읽기
 CAS top 5→6 성공      ← ②  쓰기         t = top        ← ③ 읽기 (6 을 봄)
                                         buf[9&3 = 1] = Q ← ④ 쓰기 (P 자리에 Q)
```

①→② 와 ③→④ 는 각자 "읽고 나서 쓰기" 다. 둘 다 relaxed 면 C11 은 ① 이 ④ 의 Q 를 읽고 ③ 이 ② 의 6 을 읽는 실행(LB, load buffering)을 금지하지 않는다. 그러면 도둑은 Q 를 가져가고, P 는 영영 실행되지 않으며, Q 는 나중에 주인도 실행한다. ③ 을 acquire 로 하면 ②(seq_cst CAS 의 release 부분)와 synchronizes-with 가 생긴다. 그러면 ① happens-before ④ 이고, ① 은 ④ 의 값을 읽을 수 없다 (5.1.2.4 의 read-write coherence). ARMv8 하드웨어에서 LB 는 아키텍처상 허용되지만 실제로는 거의 관찰되지 않는다. 이 버그는 테스트로 찾기 어렵고 **모델로 따져야** 하는 종류다.

**release 저장 vs 논문의 펜스.** 논문의 push 는 `fence(release); store(bottom, relaxed)` 다. 여기서는 `store(bottom, release)` 로 썼다. 이유는 Apple TSan 이 독립 펜스를 모델링하지 않기 때문이다 (MCPP-M13: 독립 fence = TSan 오탐). 미묘한 점이 하나 있다. 도둑이 읽는 bottom 값은 **take 의 relaxed store** 가 쓴 값일 수도 있다(감소나 복구). C11(N1570 5.1.2.4p10)의 release sequence 에는 "같은 스레드의 뒤따르는 store" 가 들어 있으므로, push 의 release 로 시작한 sequence 에 take 의 store 도 포함된다. C++20 은 이 조항을 지웠다(P0982R1). C++ 로 옮길 때는 논문처럼 release **펜스** 형태로 두는 편이 안전하다. 펜스는 뒤따르는 모든 store 를 덮는다. solution 은 TSan 에서 경고 0 이었다.

**왜 부호 있는 64비트 인덱스인가.** 논문 그림은 `size_t` 를 쓴다. 새 deque(bottom = 0)에서 take 하면 `b = 0 - 1 = SIZE_MAX` 가 되고 `t <= b` 가 참이 된다. 그러면 옛 슬롯 값이 반환된다. 그 순간 도둑이 steal 하면 `t < b` 도 참이라 쓰레기를 훔치고 top 을 올린다. 연습 1 이 두 경우를 모두 재현한다. 고치는 방법은 둘이다. 부호 있는 인덱스를 쓰거나, 비교를 `(ptrdiff_t)(b - t)` 로 한다. 64비트라 wrap 까지는 2^63 번이 필요하다. 또 top 이 단조 증가하므로 **top CAS 에는 ABA 가 없다** (DS-A04 의 Treiber 스택과 대조된다). 32비트 MCU 판에서는 2^32 번 steal 동안 멈춰 있던 도둑만 ABA 를 만들 수 있다. 이론상의 이야기다.

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">S2: 마지막 한 개 X — 도둑 승 (실제 스레드 2 개, gate 로 순서 고정)</text>
<text x="20" y="44" fill="currentColor">도둑</text><text x="380" y="44" fill="currentColor">주인</text>
<rect x="20" y="52" width="320" height="24" class="f-ok" stroke="currentColor"/><text x="28" y="69" fill="currentColor">① t=0, (펜스), b=1, x=buf[0]=X  → HOOK 대기</text>
<rect x="380" y="82" width="340" height="24" class="f-mut" stroke="currentColor"/><text x="388" y="99" fill="currentColor">② bottom=0, 펜스, top=0 → HOOK: 도둑 깨움</text>
<rect x="20" y="112" width="320" height="24" class="f-acc" stroke="currentColor"/><text x="28" y="129" fill="currentColor">③ CAS top 0→1 성공 → X 를 가져감</text>
<rect x="380" y="142" width="340" height="24" class="f-warn" stroke="currentColor"/><text x="388" y="159" fill="currentColor">④ t==b → CAS 0→1 실패 → EMPTY, bottom=1</text>
<text x="20" y="188" fill="currentColor">결과: 도둑 X, 주인 EMPTY, top=bottom=1. CAS 를 빼면(nocas) ④ 에서 주인도 X → 중복 (S1 에서 확인).</text>
<line x1="10" y1="202" x2="730" y2="202" stroke="currentColor" stroke-dasharray="3 3"/>
<text x="10" y="224" fill="currentColor" font-weight="bold">S3: 항목 A, B — 펜스가 막는 SB 모양</text>
<text x="20" y="246" fill="currentColor">주인 (take): W bottom=1  →  R top</text>
<text x="380" y="246" fill="currentColor">도둑 (2 번째 steal): W top=1 → R bottom</text>
<rect x="20" y="256" width="330" height="24" class="f-ok" stroke="currentColor"/><text x="28" y="273" fill="currentColor">펜스 있음: 최소 한쪽은 상대의 새 값을 본다</text>
<text x="28" y="298" fill="currentColor">도둑이 bottom=1 을 봄 → 2 번째 steal EMPTY → A 도둑, B 주인</text>
<text x="28" y="316" fill="currentColor">또는 주인이 top=1 을 봄 → t==b → CAS 로 결판</text>
<rect x="380" y="256" width="340" height="24" class="f-warn" stroke="currentColor"/><text x="388" y="273" fill="currentColor">펜스 없음: 둘 다 옛 값을 읽을 수 있다 (SB)</text>
<text x="388" y="298" fill="currentColor">주인 t=0 &lt; b=1 → CAS 없이 B</text>
<text x="388" y="316" fill="currentColor">도둑 b=2 &gt; t=1 → B 를 훔침 → B 두 번</text>
<text x="10" y="346" fill="currentColor">dq_take_reordered = R top 을 W bottom 보다 먼저 → S3 에서 결정적으로 B 중복.</text>
<text x="10" y="366" fill="currentColor">실제 하드웨어 (demo_sb.c): 펜스 없는 판 1000만 판에 11 / 40 / 23 회 중복, 나머지 0.</text>
<text x="10" y="386" fill="currentColor">x86 도 store→load 는 재배치한다 → x86 에서도 이 펜스(mfence 또는 lock 접두 명령)가 필요하다.</text>
</svg>
```

### Part A — 실제로 나온 명령 (`demo_asm.c`)

```bash
cc -std=c11 -O2 -S demo_asm.c -o - | grep -v '^\s*[;.]'                               # arm64
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O2 -S demo_asm.c -o -          # 64비트 인덱스
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O2 -S -DIDX32 demo_asm.c -o -  # 32비트 인덱스
```

<!-- code: demo_asm.c -->

arm64 `dq_take` 의 앞부분(실제 출력)은 아래와 같다. 펜스는 `dmb ish`, 마지막 하나 CAS 는 LSE `casal` 로 나왔다. `t <= b-1` 비교는 컴파일러가 `t < bottom` 으로 바꿨다(`cmp x11, x9; b.ge`).

```text
_dq_take:
	ldr	x9, [x0, #64]        ; bottom
	sub	x10, x9, #1
	str	x10, [x0, #64]       ; bottom = b-1 (relaxed → 그냥 str)
	dmb	ish                  ; seq_cst fence
	ldr	x11, [x0]            ; top (relaxed)
	cmp	x11, x9
	b.ge	LBB1_3
	…
	casal	x11, x9, [x0]        ; 마지막 하나: seq_cst CAS
```

| 연산 | arm64 (Apple clang 21, 기본 플래그) | Cortex-M4, 64비트 인덱스 | Cortex-M4, 32비트 인덱스 |
|---|---|---|---|
| push | `ldr` bottom, `ldapr` top, `str` 슬롯, `stlr` bottom | `bl __atomic_load_8` ×2, `bl __atomic_store_8` (+ `-Watomic-alignment` 경고 12 개) | `ldr` top + `dmb sy` (acquire), `dmb sy` + `str` (release) |
| take | `str` + **`dmb ish`** + `ldr`, `casal` | `__atomic_store_8` 다음 `dmb sy`, `__atomic_compare_exchange_8` | `str` + `dmb sy` + `ldr`, `ldrex`/`strex` 루프 + `dmb sy` |
| steal | `ldapr` + **`dmb ish`** + `ldapr`, `ldr` 슬롯, `casal` | libcall + `dmb sy` | `ldr`, `dmb sy` ×2, `ldr`, `dmb sy`, `ldrex`/`strex` |
| `atomic_is_lock_free(&top)` | `mov w0, #1` (상수 참) | `bl __atomic_is_lock_free` (런타임) | `movs r0, #1` |

배울 점은 셋이다. ① arm64 에서 주인 쪽 push 는 RMW 없이 `stlr` 하나로 끝난다. take 의 비용은 사실상 `dmb ish` 하나다. ② Cortex-M4 에는 64비트 원자 연산이 없다(LDREXD 없음, CPP-A04). 그래서 인덱스는 32비트여야 하고, 그 대신 wrap 비교를 `(int32_t)(b - t)` 로 해야 한다. ③ M4 에서는 acquire 도 release 도 전부 `dmb sy` 다 (단일 코어 M4 에 work-stealing 을 쓸 일은 거의 없다. 펌웨어 이야기는 Part C 끝에서).

### Part B — 강제 스케줄 세 개와 이빨 확인

스케줄은 두 가지 방법으로 고정했다. **같은 스레드에서 끼워 넣기**(S1, S3)는 훅 함수 안에서 상대편 연산을 끝까지 호출하는 방식이다. 이것은 연산 단위 순차 일관(SC) 인터리빙 하나를 그대로 실행한 것과 같다. **실제 스레드 + gate**(S2)는 두 연산이 동시에 "중간에 멈춰 있어야" 할 때 쓴다. 단계 번호와 `pthread_cond_timedwait` 2 초 마감으로 순서를 강제한다.

훅의 모양은 이렇다 (solution.c 에서 발췌). 평소에는 `g_hook == NULL` 이라 분기 하나만 남는다. 훅 포인터는 스레드를 만들기 전에만 바꾸므로 레이스가 아니다.

```c
#define HOOK(p, q) do { if (g_hook) g_hook((p), (q)); } while (0)
/* S1: 도둑이 CAS 직전 → 그 자리에서 주인 take 를 끝까지 */
static void s1_hook(hook_pt p, deque_t *q) {
    if (p == HK_STEAL_BEFORE_CAS && !g_fired) { g_fired = 1; g_r1 = g_take(q); }
}
/* S2: 실제 스레드 — 단계 번호로 줄 세우기, 대기는 2 초 마감 */
static void s2_hook(hook_pt p, deque_t *q) {
    if (p == HK_STEAL_BEFORE_CAS) { stage_set(1); if (!stage_wait(2)) g_s2_timeout = true; }
    else if (p == HK_TAKE_AFTER_TOP) { stage_set(2); if (!stage_wait(3)) g_s2_timeout = true; }
}
```

| 시나리오 | 상태 | 끼워 넣는 순서 | 정상 take 결과 | 고장 낸 take 결과 |
|---|---|---|---|---|
| S1 | X 하나 | 도둑: 읽기 끝, CAS 직전 → **주인 take 전체** → 도둑 CAS | 주인 X, 도둑 ABORT, top=bottom=1 | `nocas`: 주인 X(CAS 없음), 도둑 CAS 성공 X → **X 두 번**, top 1 &gt; bottom 0 |
| S2 | X 하나 | 도둑 CAS 직전 / 주인 top 읽은 뒤 → 도둑 CAS → 주인 CAS | 도둑 X, 주인 EMPTY | — |
| S3 | A, B | 주인 top 읽은 직후 → **도둑 steal ×2** | 도둑 A, EMPTY / 주인 B | `reordered`: 도둑 A, B / 주인 B → **B 두 번** |

S3 의 정상 판이 왜 맞는지 단계로 보자. 주인은 이미 bottom = 1 을 썼다. 그래서 도둑의 두 번째 steal 은 `t = 1, b = 1` 을 보고 비었다고 판단한다. 주인은 펜스 **전에** 읽은 것이 아니라 펜스 **뒤에** top = 0 을 읽었다. `0 < 1` 이므로 CAS 없이 B 를 가진다. 정상이다. 도둑이 A 를 가져간 것은 주인의 판단(B 하나만 내 것)과 충돌하지 않는다. 고장 판은 top 을 bottom 쓰기보다 먼저 읽는다. 그래서 도둑은 아직 b = 2 를 보고 B 까지 훔친다.

**실제 하드웨어에서는?** — `demo_sb.c`

<!-- code: demo_sb.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_sb.c -o demo_sb && ./demo_sb 10000000 1023
```

판마다 주인이 A, B 를 넣고 `go` 를 올린다. 도둑은 즉시 steal 을 두 번 하고, 주인은 0~1023 회 빈 루프만큼 쉰 뒤 take 한다. 쉬는 범위는 255 / 1023 / 4095 로 바꿔 가며, 도둑의 두 번째 steal 과 주인의 take 가 겹치는 창을 찾았다. 1023 에서 B 를 가져가는 쪽이 대략 4 : 6 으로 섞였다. 결과는 1000만 판 × 3 회이고, 측정 중 load average 는 7.2 → 9.7 이었다.

| take 판 | 명령 (`-S` 실제) | 중복 (B 두 번) |
|---|---|---|
| fence (논문) | `str` · `dmb ish` · `ldr` | 0, 0, 0 |
| none (펜스 삭제) | `str` · `ldr` | **11, 40, 23** |
| scops (seq_cst store + seq_cst load) | `stlr` · `ldar` | 0, 0, 0 |

펜스를 빼면 **이 칩에서 실제로 같은 태스크가 두 번 나간다**. 빈도는 대략 1/40만이다. 테스트 몇 번으로는 거의 안 보이고, 운영 중에는 "가끔 한 번 결과가 틀리는" 버그가 된다. `stlr` → `ldar` 쌍은 ARMv8 에서 RCsc 라서 store→load 를 막는다. 그래서 펜스 대신 쓸 수 있다 (ARM-M05 의 SB 표에서 `stlr ; ldar` 0 과 같은 이유). `stlr ; ldapr`(release store + acquire load)로는 안 된다. 0 회는 "보장" 이 아니라 "이 실행에서 안 보였다" 는 뜻이다. 정확성 근거는 모델(펜스 규칙)에서 와야 한다.

**release/acquire 를 빼면 TSan 은?** — `demo_tsan.c`

<!-- code: demo_tsan.c -->

push 의 bottom 쓰기와 steal 의 bottom 읽기를 둘 다 relaxed 로 바꾸고, 태스크 내용(`lo`, `hi` 일반 필드)을 도둑이 읽게 했다. 실제 TSan 출력은 아래와 같다 (exit 134).

```text
WARNING: ThreadSanitizer: data race (pid=7496)
  Read of size 8 at 0x00010459c008 by thread T1:
    #0 thief demo_tsan.c:60 (tsan:arm64+0x100000828)

  Previous write of size 8 at 0x00010459c008 by main thread:
    #0 main demo_tsan.c:69 (tsan:arm64+0x1000006e8)

  Location is global 'jobs' at 0x00010459c000 (tsan+0x100008008)
…
SUMMARY: ThreadSanitizer: data race demo_tsan.c:60 in thief
checksum 60000 (want 60000)
ThreadSanitizer: reported 2 warnings
```

60 행은 도둑의 `j->hi - j->lo` 이고, 69 행은 주인의 `jobs[i].lo = …` 다. 결과(checksum)는 맞았지만 TSan 은 모델 위반을 잡는다. 보고 위치는 "태스크 내용" 이고, 이것이 진짜 버그 위치다. 슬롯 자체는 atomic 이라 보고되지 않는다. `-DFIXED`(release/acquire)는 경고 0 이었다. 펜스를 뺀 SB 버그는 TSan 이 **잡지 못한다**. 데이터 레이스가 아니라 원자 변수끼리의 순서 문제라서 그렇다. 그래서 강제 스케줄 테스트와 하드웨어 스트레스가 따로 필요하다.

**실제 스레드 스트레스** (solution 의 Part B 마지막 CHECK): 주인이 20만 개를 push 하며 1/4 확률로 take 하고, 도둑 3 명이 계속 steal 한다. 한 번 실행에서 주인 92292, 도둑 107708, steal ABORT 169126 이었다. 모든 항목이 정확히 1 회 소비됐다 (20 회 반복 모두 PASS). ABORT 가 성공보다 많다. 도둑 셋이 같은 top 을 두고 싸우기 때문이다. 이것이 무작위 희생자 선택이 필요한 이유다.

### Part C — spawn / join / 종료 판정

```text
spawn(c):  pending += 1 (relaxed)            ← 먼저 올린다
           push(c) 실패 → 그 자리에서 run_task(c)      (넘침 = 고정 크기 deque 의 정책)
run_task:  c->fn(...); runs += 1; done = 1 (release); pending -= 1 (release)
           ※ done 을 올린 뒤에는 c 를 건드리지 않는다 — c 는 부모의 스택에 있다
join(c):   while (!done (acquire))  { x = find_work(); x ? run_task(x) : idle_wait(); }
worker:    while (pending (acquire) != 0) { 같은 루프 }
```

- **join 이 남의 일을 돕는 이유.** 부모가 그냥 돌며 기다리면, 워커 수만큼 부모가 동시에 기다릴 때 일을 집어 갈 사람이 아무도 없다. 돕는 join 은 부모 스택 위에 남의 태스크를 쌓는다. 그래서 워커 스택을 8 MB 로 잡았다 (macOS 보조 스레드 기본 512 KB). Cilk 는 부모의 **연속(continuation)** 을 훔치게 해서 이 깊이를 원천적으로 막는다. 여기서는 Rayon/TBB 처럼 **자식을 훔치는** 방식이다.
- **fib 에서 자식 하나는 직접 실행.** fib(n−1) 만 spawn 하고 fib(n−2) 는 같은 스레드에서 바로 호출한다. 그러면 deque 를 지나가는 횟수가 절반으로 줄고, 도둑은 deque 의 top, 곧 **가장 큰** 조각을 가져간다.
- **pending 이 0 이 되는 순간이 왜 진짜 끝인가.** 부모의 −1 은 자식들의 +1 보다 프로그램 순서상 뒤에 있다. 같은 원자 변수의 RMW 들은 수정 순서 하나에 놓인다. 그래서 "일이 남았는데 0" 인 순간은 없다. 마지막 −1 은 release 이고 워커의 확인은 acquire 이므로, pending == 0 을 본 main 은 모든 결과를 본다.
- **CHECK 결과** (solution, 20 회 반복 모두 29/29). fib(25) cutoff 12 = 태스크 610 개이고, 8 워커에서 steal 22 회(실행마다 다름). spawned == executed. 자식 `runs == 1` 위반 0. pending 은 끝나고 0. psum 은 2^20 원소가 모두 1 회 방문됐다. 1 워커의 wide 태스크는 넘침 **정확히 344** 였다. 4 워커에서는 넘침 수가 실행마다 달라서(103, 178 …) 정확성만 본다.

### Part C — find_work: 희생자 고르기와 쉬기

| 선택 | 이 레슨 | 다른 선택지와 이유 |
|---|---|---|
| 내 deque 먼저 | `dq_take` (LIFO) | 캐시가 따뜻한 최근 일, RMW 없음 |
| 희생자 | 무작위(xorshift, 나 제외), 한 번에 최대 n−1 번 | 순회(round-robin)는 도둑들이 같은 순서로 같은 희생자에 몰린다. 무작위가 Blumofe–Leiserson 상한의 전제다 |
| 훔치는 양 | 1 개 (top) | 재귀 분할이면 top 하나가 가장 큰 조각이라 충분하다. Go/Tokio 는 독립 태스크라 절반을 훔친다 |
| ABORT | 다음 희생자로 | 같은 희생자에서 바로 다시 시도하면 같은 top 을 두고 계속 싸운다 |
| 일이 없을 때 | `yield` 스핀, 64 번마다 `sched_yield`, 4096 번마다 마감 확인 | 실제 런타임은 일정 횟수 뒤 잠든다(futex/condvar). 잠들기 직전의 "lost wakeup" 처리는 OS-A01 · MCPP-A03 의 주제다 |

워커 구조체(deque 포함)는 128 B 로 정렬했다. 그래서 워커 i 의 bottom 과 워커 i+1 의 top 이 한 라인에 들어가지 않는다. 각 워커의 통계 카운터(spawned, executed …)는 주인만 쓰고 풀이 끝난 뒤에만 읽으므로 일반 변수로 충분하다 (스레드 join 이 happens-before 를 준다).

### Part C — 확장성: `demo_bench.c`

<!-- code: demo_bench.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o demo_bench
./demo_bench fib 38 18 ; ./demo_bench fib 34 6 ; ./demo_bench psum
TERM_MODE=root ./demo_bench fib 34 6           # 종료 판정을 "루트 done" 으로 (pending 카운터 없음)
```

조건: Apple M6 (`sysctl -n machdep.cpu.brand_string`), `hw.perflevel0/1/2.physicalcpu` = 2 / 4 / 6 (Super 2 + Performance 4 + Efficiency 6 = 12 코어), `hw.cachelinesize` 128, Apple clang version 21.0.0 (clang-2100.0.123.102). 칸은 3 회 중 중앙값이고, 결과는 매번 직렬 값과 같았다(OK). **다른 에이전트들의 벤치마크가 같은 맥에서 동시에 돌았다.** 측정 중 `uptime` load average 는 6.8 → 9.9 였다(이 세션 앞부분에는 16 까지 올라갔다). 절대 ms 보다 **배수** 를 보라. macOS 는 코어 고정이 안 되므로 6 워커가 넘으면 E 코어에도 올라간다. 직렬 `fib_serial` 은 fib(34) 8.1 ms, fib(38) 53.8 ms 였다.

**잘게 쪼갠 fib(34) cutoff 6** — 태스크 83만 개, 태스크당 약 15 ns. 배수는 Chase–Lev 1 워커 · 루트 종료(12.28 ms) 대비다.

| 워커 | CL (루트 종료) | CL + pending 카운터 | 워커별 뮤텍스 deque | 전역 뮤텍스 큐 |
|---|---|---|---|---|
| 1 | 1.00 (12.28 ms) | 0.74 (16.63) | 0.66 (18.70) | 0.74 (16.50) |
| 2 | 1.96 | 0.58 | 1.30 | 0.37 |
| 4 | 3.43 | 0.51 | 2.51 | 0.16 |
| 6 | 4.67 | 0.54 | 3.59 | 0.13 |
| 8 | 5.03 | 0.57 | 4.01 | 0.11 |
| 12 | **7.04** (1.74 ms) | **0.54** (22.71) | 4.59 | **0.07** (182.89) |

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">fib(34) cutoff 6 (태스크 83만 개) — 배수 vs 워커 수 (실측, load 7.7–9.9)</text>
<line x1="70" y1="300" x2="640" y2="300" stroke="currentColor"/>
<line x1="70" y1="300" x2="70" y2="36" stroke="currentColor"/>
<text x="40" y="304" fill="currentColor">0</text><text x="40" y="240" fill="currentColor">2</text><text x="40" y="176" fill="currentColor">4</text><text x="40" y="112" fill="currentColor">6</text><text x="40" y="48" fill="currentColor">8</text>
<line x1="70" y1="268" x2="640" y2="268" stroke="currentColor" stroke-dasharray="1 4"/><text x="645" y="272" fill="currentColor">1.0</text>
<text x="66" y="318" fill="currentColor">1</text><text x="116" y="318" fill="currentColor">2</text><text x="216" y="318" fill="currentColor">4</text><text x="316" y="318" fill="currentColor">6</text><text x="416" y="318" fill="currentColor">8</text><text x="512" y="318" fill="currentColor">10</text><text x="612" y="318" fill="currentColor">12</text>
<text x="300" y="338" fill="currentColor">워커 수 (6 넘으면 E 코어 포함)</text>
<polyline points="70,268 120,236 220,172 320,108 420,44" fill="none" stroke="currentColor" stroke-dasharray="2 3"/>
<text x="428" y="48" fill="currentColor">이상적 (선형)</text>
<polyline points="70,268 120,237.3 220,190.2 320,150.6 420,139 520,109 620,74.7" fill="none" stroke="currentColor" stroke-width="2.5"/>
<circle cx="620" cy="74.7" r="4" class="f-acc" stroke="currentColor"/>
<text x="560" y="66" fill="currentColor">Chase–Lev 7.0×</text>
<polyline points="70,278.9 120,258.4 220,219.7 320,185.1 420,171.7 520,157.3 620,153.1" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="7 4"/>
<circle cx="620" cy="153.1" r="4" class="f-ok" stroke="currentColor"/>
<text x="540" y="146" fill="currentColor">워커별 뮤텍스 4.6×</text>
<polyline points="70,276.3 120,281.4 220,283.7 320,282.7 420,281.8 520,282.1 620,282.7" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="3 2"/>
<circle cx="620" cy="282.7" r="4" class="f-warn" stroke="currentColor"/>
<text x="440" y="262" fill="currentColor">CL + 전역 pending 카운터 0.54×</text>
<polyline points="70,276.3 120,288.2 220,294.9 320,295.8 420,296.5 520,297.1 620,297.8" fill="none" stroke="currentColor" stroke-width="1.5"/>
<circle cx="620" cy="297.8" r="4" class="f-mut" stroke="currentColor"/>
<text x="440" y="294" fill="currentColor">전역 뮤텍스 큐 0.07×</text>
</svg>
```

**굵게 쪼갠 fib(38) cutoff 18** — 태스크 1.8만 개, 태스크당 약 3 µs. 종료 판정은 pending, load 는 6.8 이다. Chase–Lev 는 2 / 4 / 8 / 12 워커에서 2.01 / 3.70 / 5.51 / **7.78×**, 워커별 뮤텍스는 2.05 / 3.99 / 5.41 / 5.78×, 전역 큐는 1.75 / 1.48 / 1.50 / **0.99×** (2 워커 이후 평평하고, 3 회 사이 흔들림이 컸다). **psum**(2^24 원소, 원소마다 해시 16 회, 잎 4096 개 × 약 15 µs, 루트 종료, load 9.9)은 12 워커에서 CL 8.47×, 워커별 뮤텍스 7.79×, 전역 큐 5.08× 였다.

**1 워커 오버헤드.** 같은 fib(34) cutoff 6 을 직렬로 돌리면 8.1 ms, Chase–Lev 풀 1 워커는 12.3 ms 다. 태스크 83만 개에 약 4.2 ms, 곧 태스크당 약 5 ns 가 spawn(push) + take + `done` release store 의 값이다. 1 워커 판에 펜스(`dmb ish`)가 들어 있어도 이 정도다. 뮤텍스 deque 는 같은 자리에서 18.7 ms 였다.

읽는 법:

1. **태스크가 굵으면 무엇을 써도 비슷하다.** 그러나 전역 큐는 굵은 fib 에서도 2 워커 이후 늘지 않았다. 모든 spawn 이 같은 락을 지나기 때문이다. 태스크가 잘수록 "스케줄러 비용 / 태스크 일" 비가 커진다. 15 ns 짜리 태스크에서 전역 큐는 12 워커가 1 워커보다 **11 배 느렸다**.
2. **분산이 먼저, lock-free 는 그다음.** 워커별 deque 에 뮤텍스를 씌워도 4.6× 까지 간다. 전역 큐(0.07×)와의 차이 대부분은 "공유 지점을 없앤 것" 에서 나온다. Chase–Lev 는 거기에 주인 쪽 연산의 RMW·락을 없애 7.0× 로 올린다. 1 워커만 봐도 18.70 → 12.28 ms 다. 뮤텍스 lock/unlock 두 번이 태스크마다 사라진 효과다.
3. **숨은 전역 지점: pending 카운터.** 같은 Chase–Lev 에 "spawn 마다 +1, 끝날 때마다 −1" 하는 전역 atomic 하나를 넣으면 0.54× 로 **직렬보다 느려진다**. 태스크 83만 개 × RMW 2 회가 한 캐시 라인을 두고 코어 사이를 오간다(LLAT-B06 의 경합 비용, OS-M07 의 라인 핑퐁). solution 은 분리(detached) 태스크도 지원하려고 pending 을 쓴다. fork-join 만 할 거라면 "루트 done" 만 보면 된다. 모든 태스크가 루트의 자손이고 다 join 되기 때문이다. 둘 다 필요하면 워커별 카운터 + 2 단계 확인(분산 종료 탐지)을 쓴다. 굵은 태스크(1.8만 개)에서는 pending 비용이 보이지 않았다 (7.78× vs 5.63×, 루트 판이 오히려 낮았는데 load 흔들림이다).
4. **P/E 코어.** 12 워커일 때 워커별 실행 태스크 수의 min/max 는 37732/105638 (fib 잘게), 0/2246 (fib 굵게, pending 판), 0/499 (psum) 였다. 0 은 그 스레드가 늦게 시작했거나 E 코어에서 거의 기회를 못 얻었다는 뜻이다. 정적 분할(코어마다 1/12)이었다면 가장 느린 코어가 전체 시간을 정했을 것이다. work-stealing 은 느린 코어가 적게 가져가도 결과가 맞고, 빠른 코어가 나머지를 훔쳐 간다. 그래서 이 이기종 칩에서 7× 가 나온다. (HPC-M02 가 P/E 스케줄링을 따로 다룬다.)
5. False sharing(워커 구조체 · deque 의 128 B 정렬을 뺐을 때 비용) 측정은 이 레슨에서 하지 않았다 → see:ARCH-M08 · MEM 트랙 · LPERF-A03.

**누가 이 구조를 쓰나** (세부는 문서 기준이고 직접 확인하지 않았다)

| 시스템 | 알려진 구조 |
|---|---|
| Cilk-5 (Frigo, Leiserson, Randall, PLDI 1998) | work-first, 연속을 훔침, THE 프로토콜 (충돌 경로에만 락) |
| Intel TBB / oneTBB | 스레드별 태스크 deque + 무작위 steal (arena) |
| Java ForkJoinPool (Doug Lea) | 워커별 deque, 도둑은 base 쪽 |
| Rust Rayon | crossbeam-deque (Chase–Lev 계열) 의 Worker/Stealer |
| Go 런타임 | P 마다 고정 256 칸 로컬 실행 큐 + 전역 큐, 비면 다른 P 에서 절반을 훔침 |
| Tokio (멀티스레드 런타임) | 워커별 고정 크기 로컬 큐 + LIFO 슬롯 + 절반 훔치기 + 전역 inject 큐 |

**펌웨어 · 로보틱스.** MCU 에서는 거의 쓰지 않는다. 단일 코어면 훔칠 상대가 없고, 2 코어 MCU(RP2040, ESP32)는 보통 코어마다 역할을 정적으로 나눈다. Linux SoC(Jetson, i.MX8)의 인식 · 계획 코드에서 TBB/OpenMP(OpenCV 의 parallel 백엔드 등) 밑에 들어 있다. ROS 2 의 기본 `MultiThreadedExecutor` 는 스레드들이 하나의 executor 에서 준비된 콜백을 가져가는 구조로 알고 있다. work-stealing deque 는 아니다(미검증). 실시간 제어 루프에는 work-stealing 을 피한다. 어느 코어에서 언제 돌지 정해지지 않아 최악 지연을 분석하기 어렵기 때문이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution     # 29/29 passed (20 회 반복)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o so_san && ./so_san   # 29/29, 경고 0
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_tsan && ./so_tsan                                     # 29/29, TSan 경고 0 (5 회)
```

새니타이저 빌드는 `__has_feature(thread_sanitizer / address_sanitizer)` 로 감지해 반복 수를 1/4 로 줄인다(`SCALE`). TSan 판은 0.5 초 걸렸다.

## 7. 자주 하는 실수

1. **인덱스를 `size_t` 로 두고 논문 비교를 그대로 쓴다.** 새 deque 에서 `bottom - 1 = SIZE_MAX` 가 되어 take 와 steal 이 둘 다 옛 슬롯 값을 내준다 (연습 1 이 재현). → `int64_t` 를 쓰거나 `(ptrdiff_t)(b - t)` 로 비교한다.
2. **take 에서 펜스를 빼거나, top 을 먼저 읽는다.** S3 와 `demo_sb.c` 에서 1000만 판에 11~40 회 중복이 났다. TSan 은 이것을 못 잡는다. → `store(bottom); fence(seq_cst); load(top)`. 아니면 `stlr`/`ldar` 쌍(seq_cst store + seq_cst load)을 쓴다. `memory_order_release` store + `acquire` load 는 **충분하지 않다** (RCpc 면 store→load 가 풀린다, ARM-M05).
3. **"주인은 bottom 쪽이니 도둑과 안 부딪친다" 며 마지막 하나에서 CAS 를 생략한다.** S1 에서 X 가 두 번 나간다. → `t == b` 일 때만 CAS 하고, 결과와 상관없이 `bottom = b + 1` 로 되돌린다.
4. **steal 에서 CAS 를 먼저 하고 슬롯을 나중에 읽는다.** CAS 가 성공한 순간 주인이 그 칸을 다시 쓸 수 있다. → 슬롯을 먼저 읽고, CAS 가 실패하면 버린다 (`DQ_ABORT`).
5. **push 의 top 읽기를 relaxed 로 한다.** 슬롯 재사용 때 도둑이 새 값을 읽을 수 있다(LB 모양). → `acquire`. 이 실수는 TSan 으로도, 이 맥의 스트레스로도 보이지 않았다. 모델로 따져야 한다.
6. **`run_task` 에서 `done` 을 올린 뒤 태스크를 만진다** (예: `pending -= 1` 을 `t->pool` 같은 필드로 접근). 부모가 이미 반환해서 스택이 사라졌을 수 있다. → 필요한 포인터는 먼저 지역 변수에 꺼내 두고, `done` store 를 태스크에 대한 마지막 접근으로 한다.
7. **join 에서 그냥 돌며 기다린다.** 워커 수만큼 부모가 기다리면 진행이 멈춘다. → 기다리는 동안 `find_work` 로 다른 일을 실행한다.
8. **종료 판정을 "내 deque 가 비었다" 로 한다.** 다른 워커가 실행 중인 태스크가 새 자식을 만들 수 있다. → pending == 0 (또는 fork-join 이면 루트 done). 그리고 그 카운터의 비용을 잰다 (Part C 3번).
9. **너무 짧은 워크로드로 확장성을 잰다.** `demo_bench.c` 의 첫 psum 판(원소를 그냥 더함)은 1 워커 0.8 ms 였다. 12 스레드 생성·join 비용과 메모리 대역폭이 결과를 정해서 스케줄러 비교가 의미 없었다. → 한 칸이 수십 ms 이상이 되게 일을 늘리고(원소마다 해시 16 회), 결과를 직렬 값과 비교하고, 중앙값과 load average 를 같이 적는다.

## 8. 면접 Q&A

<details><summary>Q1. Chase–Lev deque 의 연산별 memory_order 를 설명해 보세요.</summary>

push 는 top 을 acquire 로 읽고(슬롯 재사용 전에 도둑의 읽기가 끝났음을 본다), 슬롯은 relaxed 로 쓰고, bottom 은 release 로 쓴다(내용 게시). take 는 bottom 을 b−1 로 relaxed store 한 뒤 **seq_cst 펜스**, top 을 relaxed load 한다. 마지막 하나(t == b)면 top 에 seq_cst CAS 를 하고 bottom 을 되돌린다. steal 은 top acquire → **seq_cst 펜스** → bottom acquire 로 읽은 뒤, 슬롯을 먼저 읽고 top CAS 를 한다. 꼬리: "펜스 두 개가 왜 다 필요하죠?" 양쪽이 "내 쓰기 → 상대 변수 읽기" 를 하는 SB 모양이라서 그렇다. 한쪽만 막으면 다른 쪽의 store→load 재배치로 둘 다 옛 값을 읽을 수 있다. 이 칩에서 take 쪽 펜스를 빼자 1000만 판에 11~40 회 중복이 났다.
</details>

<details><summary>Q2. 마지막 한 개를 두고 take 와 steal 이 겹치면?</summary>

둘 다 top 에 같은 기대값 t 로 CAS 한다. 수정 순서는 하나이므로 정확히 하나만 성공한다. 주인이 지면 EMPTY 를 받고, 도둑이 지면 ABORT 를 받는다. 어느 쪽이든 주인은 bottom 을 b+1 로 돌려 `top == bottom` 빈 상태로 만든다. 이 레슨은 이것을 hook 으로 강제했다. 도둑이 CAS 직전에 멈춘 사이 주인 take 전체가 돌면 주인이 이긴다(S1). 도둑 CAS 가 주인의 top 읽기와 CAS 사이에 끼면 도둑이 이긴다(S2, 실제 스레드 2 개 + gate). 꼬리: "둘 이상 남았을 때는 왜 CAS 가 없어도 되나?" 펜스 뒤에 top 을 읽어서 `t < b` 였다면, 그 순간 도둑들은 이미 줄어든 bottom 을 본다. 그래서 b 칸까지 오지 못한다. 도둑은 top 쪽에서 하나씩만, 그것도 CAS 로 가져간다.
</details>

<details><summary>Q3. 이 deque 는 lock-free 인가요, wait-free 인가요?</summary>

push 와 take 는 루프가 없다. CAS 는 마지막 하나에서 한 번뿐이다. 그래서 주인 쪽은 **wait-free** 다(고정 배열이면. 성장은 malloc 에 달렸다). steal 은 한 번 시도하고 실패하면 ABORT 를 돌려준다. ABORT 는 다른 누군가의 CAS 가 성공했다는 뜻이므로, 재시도하는 도둑 집합은 **lock-free**(시스템 전체로는 항상 진행)다. 그러나 특정 도둑이 계속 지는 기아는 가능하다. 정의: lock-free = 어떤 스레드가 멈춰도 다른 스레드들이 유한 단계 안에 진행한다. wait-free = 모든 스레드가 각자 유한 단계 안에 끝낸다. obstruction-free = 혼자 돌 때만 유한 단계를 보장한다. 대조: DS-A03 의 Vyukov MPMC 는 슬롯 시퀀스 store 전에 선점된 생산자가 그 슬롯의 소비자를 막는다. 엄밀히는 lock-free 가 아니다.
</details>

<details><summary>Q4. 왜 주인은 LIFO, 도둑은 FIFO 쪽인가요?</summary>

주인이 방금 만든 일(bottom)은 캐시가 따뜻하고 작다. 깊이 우선으로 처리하면 deque 가 깊이 × 분기 수 정도로 작게 유지된다. 재귀 분할에서 top 쪽은 가장 먼저 쪼개진, 곧 **가장 큰** 조각이다. 도둑이 한 번 훔치면 오래 바빠지므로 steal 횟수가 적다. 실측으로 83만 태스크에 steal 219 회였다. 경쟁도 양 끝으로 갈라져서 마지막 하나일 때만 만난다. 꼬리: "Go 와 Tokio 는 왜 절반을 훔치나?" 그 태스크들은 재귀 분할이 아니라 독립된 고루틴/퓨처다. 그래서 "큰 조각" 이 없고, 한 번에 많이 옮겨 steal 횟수를 줄인다 (세부는 미검증).
</details>

<details><summary>Q5. 배열이 가득 차면 어떻게 하나요?</summary>

① 고정 크기 + 넘치면 그 자리에서 실행한다. 이 레슨의 방법이고, 정확성은 그대로다. 실측으로 1 워커에서 600 자식 중 344 개가 직접 실행됐다. ② 2 배 배열을 만들어 [t, b) 를 복사하고 배열 포인터를 release 로 게시한다(연습 2). 도둑은 bottom 을 읽은 **다음** 배열 포인터를 acquire 로 읽는다(논문은 consume). bottom acquire 가 게시를 보장하므로 새 항목을 옛 배열에서 읽는 일은 없다. 옛 배열은 늦은 도둑이 아직 읽고 있을 수 있어서 바로 free 할 수 없다. 이것은 메모리 회수 문제다(hazard pointer = LLAT-M03, epoch = OS-A03). 연습 2 는 끝날 때 한꺼번에 해제한다. ③ Go 처럼 고정 링 + 넘치면 절반을 전역 큐로 옮긴다.
</details>

<details><summary>Q6. 스레드 풀의 종료는 어떻게 판정하나요? 비용은?</summary>

pending 카운터를 쓴다. spawn 할 때 +1(relaxed), 완료 시 −1(release), 워커는 0 을 acquire 로 보면 나간다. 부모의 −1 이 자식들의 +1 보다 뒤에 있으므로 일이 남은 채 0 이 될 수 없다. 이 방법은 join 없는 분리 태스크에도 맞다(연습 3). 비용: 태스크 83만 개짜리 fib 에서 이 카운터 하나 때문에 12 워커가 0.54× 가 됐다. 없애면 7.04× 다. fork-join 이면 루트의 done 만 보면 된다. 일반적으로는 워커별 카운터와 분산 종료 탐지(모두 idle + 합계 일치를 두 번 확인)를 쓴다. 꼬리: "idle 워커는 어떻게 쉬나?" 이 레슨은 `yield` 스핀, 64 번마다 `sched_yield` 다. 실제 런타임은 일정 횟수 실패하면 futex/조건 변수로 잠들고, spawn 이 하나를 깨운다 (OS-A01, MCPP-A03).
</details>

<details><summary>Q7. 왜 전역 뮤텍스 큐보다 빠른가요? 정말 lock-free 덕분인가요?</summary>

대부분은 **분산** 덕분이다. 워커별 deque 에 뮤텍스를 씌운 판도 12 워커에서 4.6× 였고, 전역 큐는 0.07× 였다. 전역 큐는 모든 spawn·꺼내기가 같은 락 라인과 같은 큐 라인을 오간다. lock-free 는 그 위에서 주인 쪽의 lock/unlock 을 없앤다. 1 워커 18.7 → 12.3 ms, 12 워커 4.6 → 7.0× 다. 꼬리: "steal 이 몇 번이나 일어났나?" 83만 태스크에 219 회였다. 주인 쪽 경로가 압도적으로 많으므로 거기를 싸게 만든 설계가 이긴다.
</details>

<details><summary>Q8. 이 코드를 어떻게 테스트했나요? TSan 이면 충분한가요?</summary>

세 겹으로 했다. ① 결정적 강제 스케줄: hook 지점에서 상대 연산을 끼워 넣거나(같은 스레드) gate 로 두 스레드를 멈춰 세운다. 정상 판은 정확히 한 번씩 나오는지, **고장 판은 중복이 나는지** 를 둘 다 CHECK 해서 테스트가 실제로 잡는다는 것을 확인한다. ② 실제 스레드 스트레스: 모든 항목 정확히 1 회 소비(카운터 배열), 풀에서는 spawned == executed, 자식 runs == 1, 결과 정확. ③ TSan / ASan. TSan 은 release/acquire 누락처럼 **일반 데이터의 레이스** 가 되는 버그를 잡는다(`demo_tsan.c`). 펜스 누락(SB)은 원자 변수끼리의 순서 문제라 TSan 이 못 잡는다. 독립 펜스는 오히려 오탐을 낸다. 그래서 펜스 판은 강제 스케줄과 하드웨어 스트레스(`demo_sb.c`)로 확인했다.
</details>

<details><summary>Q9. Cortex-M 이나 RTOS 에서는?</summary>

Cortex-M4 에서는 64비트 인덱스가 lock-free 가 아니다(`__atomic_load_8` libcall). 32비트 인덱스와 wrap 비교를 써야 하고, 펜스·acquire·release 가 전부 `dmb sy` 다(실제 `-S`). M0/M0+ 에는 CAS 자체가 없다(LDREX 없음, CPP-A04). 단일 코어 MCU 에는 훔칠 상대가 없다. 듀얼 코어 MCU 는 코어별 역할을 정적으로 나누는 것이 보통이다. work-stealing 이 펌웨어 면접에 나오는 맥락은 Linux SoC 의 병렬 처리(TBB/OpenMP), 또는 "실시간 제어에 왜 안 쓰나(최악 지연을 분석할 수 없다)" 다. 꼬리: "ISR 에서 spawn 할 수 있나?" push 는 주인만 해야 한다. ISR 이 같은 코어의 태스크 deque 에 push 하면 주인이 둘이 된다. ISR → 태스크 전달은 SPSC 큐(DS-A01, RUST-M07)로 한다.
</details>

<details><summary>Q10. 자식 훔치기(child stealing)와 연속 훔치기(continuation stealing)의 차이는?</summary>

이 레슨과 TBB·Rayon 은 spawn 한 **자식** 을 deque 에 넣고 부모는 계속 간다. 도둑은 자식을 가져간다. 구현이 쉽다(그냥 함수 포인터 + 인자). 그 대신 join 에서 기다리며 남의 일을 실행하면 스택이 깊어지고, deque 에 자식이 여러 개 쌓일 수 있다. Cilk 는 부모의 **연속**(spawn 지점 이후의 나머지)을 deque 에 넣고, 워커는 자식으로 바로 들어간다. 도둑은 연속을 가져간다. 그래서 직렬 실행과 같은 순서·같은 스택 깊이가 유지된다(공간 상한 = P × 직렬 스택). 그 대신 컴파일러가 연속을 따로 떼어 낼 수 있어야 한다(프레임을 힙에 두거나 스택을 바꾼다). 꼬리: "fib 에서 자식 하나를 직접 실행한 건 어느 쪽에 가깝나?" 여전히 자식 훔치기다. spawn 수만 절반으로 줄였다.
</details>

<details><summary>Q11. deque 256 칸이면 충분한가요? 얼마나 차나요?</summary>

깊이 우선(LIFO)으로 돌면 주인 deque 에는 대략 "현재 재귀 깊이 × 단계당 spawn 수" 만큼만 남는다. fib(34) cutoff 6 은 단계당 1 개, 깊이 28 정도라 수십 칸이다. 넘치는 것은 한 태스크가 자식을 한꺼번에 많이 spawn 할 때다(이 레슨의 wide 태스크, parallel_for 의 평평한 루프). 그때를 대비해 넘침 = 직접 실행으로 둔다. 정확성은 그대로이고 병렬성만 줄어든다. 이것은 1 워커에서 넘침 344 회로 확인했다. 성장 판(연습 2)은 메모리 회수 문제를 끌고 온다. Go 처럼 넘침의 절반을 전역 큐로 보내는 방법도 있다.
</details>

## 9. 추가 연습

### 연습 1 — 논문 그림 그대로의 `size_t` 인덱스

논문 그림 1 의 비교(`t <= b`, `t < b`)를 `size_t` 로 그대로 옮긴 `udq_take_paper` / `udq_steal_paper` 가 어떤 입력에서 깨지는지 보이고, 부호 없는 인덱스는 유지한 채 고친 판을 써라. 인덱스가 `SIZE_MAX → 0` 을 넘어가도 맞아야 한다.

<details><summary>답</summary>

새 deque(top = bottom = 0)에서 take 하면 `b = SIZE_MAX` 가 되고 `0 <= SIZE_MAX` 라서 옛 슬롯 값(표지 `g_sentinel`)을 돌려준다. 같은 순간(bottom = SIZE_MAX) 도둑의 steal 도 `0 < SIZE_MAX` 라서 쓰레기를 훔치고 top 을 1 로 올린다. 고친 판은 비교를 `(ptrdiff_t)(b - t) >= 0` / `<= 0` 으로 바꾼다. 부호 없는 뺄셈은 wrap 해도 정의돼 있고(C11 6.2.5p9), 차이가 2^63 미만이면 부호가 맞다. 테스트: 시작 인덱스 `SIZE_MAX - 2` 에서 6 개 push, steal 2 (FIFO), take 4 (LIFO). 마지막 CAS 가 top 을 `SIZE_MAX → 0` 으로 넘기고 top = bottom = 0 으로 끝난다.
</details>

### 연습 2 — 늘어나는 deque

가득 차면 2 배 배열에 [t, b) 를 복사하고 `arr` 를 release 로 게시하는 `gdq_*` 를 써라. 도둑은 bottom 을 읽은 다음 `arr` 를 acquire 로 읽는다. 옛 배열은 `prev` 사슬로 붙잡아 두었다가 끝에서 해제한다. 4 칸에서 5000 개 push 하면 몇 번 커지나? 도둑 3 명 스트레스에서 모든 항목이 정확히 한 번 나와야 한다.

<details><summary>답</summary>

4 → 8192 로 11 번 커진다 (4·2^11 = 8192 ≥ 5000). 순서가 중요하다. 주인은 복사를 마친 뒤 `arr` 를 release 로 쓰고, 그 뒤 push 의 bottom release 가 따라온다. 도둑이 새 bottom 을 acquire 로 봤다면 `arr` store 가 happens-before 이므로 새 배열을 본다. 옛 bottom 을 봤다면 그 인덱스는 옛 배열에도 그대로 있다(주인은 커진 뒤 옛 배열을 쓰지 않는다). 그래서 어느 배열을 읽어도 맞다. 바로 free 하면 늦은 도둑이 해제된 메모리를 읽는다 → 회수는 LLAT-M03 / OS-A03. 스트레스 실측: 10만 개, 15 번 커져 최종 65536 칸(도둑이 느려 쌓였다), 모두 1 회.
</details>

### 연습 3 — 분리 태스크 parallel_for

`for_task` 는 구간을 반으로 나눠 둘 다 spawn 하고 **join 없이** 바로 끝난다 (잎은 원소마다 hit +1). 태스크는 스택이 아니라 아레나에서 꺼낸다. 풀이 끝났을 때 모든 원소의 hit 가 1 이고 pending 이 0 이어야 한다.

<details><summary>답</summary>

join 이 없으니 루트의 done 은 금방 1 이 된다. 끝을 아는 방법은 pending 뿐이다. 이것이 solution 이 pending 을 쓰는 이유다. 부모가 자식 둘의 +1 을 한 **뒤에** 자기 −1 을 하므로 중간에 0 이 되지 않는다. 태스크를 스택에 두면 부모가 반환한 뒤 자식이 사라진 프레임을 가리킨다. 그래서 아레나(원자 인덱스 할당)를 쓴다. 검증: 2^18 원소, 6 워커, hit 모두 1, pending 0, spawned == executed == 1 + 아레나 할당 수.
</details>

### 연습 4 — 병렬 병합 정렬

`sort_task` 는 2048 개 이하면 `qsort`, 아니면 오른쪽 반을 spawn, 왼쪽은 직접, join, 병합한다. 8 워커로 2^20 개 정수를 정렬해 qsort 결과와 바이트 단위로 같아야 한다.

<details><summary>답</summary>

병합은 두 반쪽이 모두 끝난 뒤에만 한다. 오른쪽을 다른 워커가 정렬했어도 join 의 `done` acquire 가 그 쓰기들을 보여 준다(run_task 의 release 와 짝). 왼쪽과 오른쪽은 겹치지 않는 구간이라 레이스가 없다(TSan 0). `g_tmp` 도 구간별로 나눠 쓰므로 공유하지 않는다. 검증: `memcmp(ref, arr) == 0`, 자식 runs 위반 0, spawned == executed, pending 0.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o pr_san && ./pr_san
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pr_tsan && ./pr_tsan
```

실제 출력은 세 빌드 모두 `12/12 passed`, TSan 경고 0 이었다 (일반 빌드 5 회 반복).

## 10. 핵심 정리

- Chase–Lev: 주인은 bottom 쪽 push/take(LIFO, RMW 없음), 도둑은 top 쪽 steal(FIFO, CAS)이다. 둘은 마지막 한 개(t == b)에서만 top CAS 로 결판낸다. top 은 단조 증가하므로 ABA 가 없다.
- 순서: push = top acquire · 슬롯 relaxed · bottom **release**. take = bottom store → **seq_cst 펜스** → top load. steal = top acquire → **seq_cst 펜스** → bottom acquire → 슬롯 먼저 읽고 CAS.
- 두 펜스는 SB(store→load)를 막는다. x86 에서도 필요하다. 이 칩에서 take 펜스를 빼자 1000만 판에 11 / 40 / 23 회 중복이 났다. `stlr`+`ldar`(seq_cst 연산)로 대신할 수 있고, release+acquire 로는 안 된다.
- 인덱스는 부호 있는 64비트를 쓴다(size_t 면 bottom 0 에서 깨짐). Cortex-M4 는 64비트 atomic 이 libcall 이라 32비트 + wrap 비교, 펜스는 `dmb sy` 다.
- 테스트 = hook 강제 스케줄(정상은 1 회, **고장 판은 중복** 까지 CHECK) + 정확히 한 번 스트레스 + TSan. TSan 은 release/acquire 누락은 잡지만 펜스 누락은 못 잡는다.
- 풀: spawn = pending+1 후 push(넘치면 직접 실행), join = 기다리며 남의 일 하기, 종료 = pending == 0. `done` store 가 태스크에 대한 마지막 접근이어야 한다.
- 실측 (12 코어 P/E 혼합, load 7–10): 잘게 쪼갠 fib 에서 Chase–Lev 7.0×, 워커별 뮤텍스 4.6×, 전역 뮤텍스 큐 0.07×, 전역 pending 카운터를 붙인 Chase–Lev 0.54×. 공유 지점 하나가 전부를 망친다.
- 성질: 주인 = wait-free, steal = lock-free(ABORT = 남이 성공), Vyukov MPMC 는 엄밀히 lock-free 가 아니다(DS-A03).

## 참고자료

- N. M. Lê, A. Pop, A. Cohen, F. Zappa Nardelli, "Correct and Efficient Work-Stealing for Weak Memory Models", PPoPP 2013 — 그림 1 C11 코드, ARMv7/POWER 판과 증명: <https://fzn.fr/readings/ppopp13.pdf>
- D. Chase, Y. Lev, "Dynamic Circular Work-Stealing Deque", SPAA 2005 — 원래 알고리즘, 배열 성장
- N. S. Arora, R. D. Blumofe, C. G. Plaxton, "Thread Scheduling for Multiprogrammed Multiprocessors", SPAA 1998 — ABP deque (Chase–Lev 의 전신)
- M. Frigo, C. E. Leiserson, K. H. Randall, "The Implementation of the Cilk-5 Multithreaded Language", PLDI 1998 — work-first 원칙, THE 프로토콜
- R. D. Blumofe, C. E. Leiserson, "Scheduling Multithreaded Computations by Work Stealing", JACM 46(5), 1999 — 무작위 steal 의 시간·공간 상한
- C11 (N1570): 5.1.2.4 (happens-before, release sequence p10, data race), 7.17.4 fences, 7.17.7.4 compare_exchange, 6.2.5p9 부호 없는 wrap — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>; C++20 release sequence 변경 P0982R1
- M. Herlihy, N. Shavit, *The Art of Multiprocessor Programming*, 16장 Futures, Scheduling, and Work Distribution (work-stealing deque, lock-free/wait-free 정의는 3장)
- Arm Architecture Reference Manual (DDI 0487) — `DMB`, `LDAPR`/`LDAR`/`STLR`, LSE `CAS`; ARMv7-M ARM (DDI 0403) — `LDREX`/`STREX`, `DMB`
- crossbeam-deque 문서 (Rust, Chase–Lev 계열 Worker/Stealer): <https://docs.rs/crossbeam-deque>; Go `runtime/proc.go` (`runqput`/`runqsteal`); Tokio 스케줄러 블로그 "Making the Tokio scheduler 10x faster" (2019)
- 관련 레슨: C-A03 (release/acquire, CAS), MCPP-M13 (펜스 규칙, TSan 과 독립 펜스), ARM-M05 (SB 리트머스, RCsc vs RCpc), ARM-M04 (LL/SC vs LSE), LLAT-B06 (원자 연산·경합 비용), OS-M07 (스핀락, 라인 핑퐁), CPP-A04 (Cortex-M lock-free 여부), DS-A01 (SPSC), DS-A03 (Vyukov MPMC, 진행 성질), DS-A04 (ABA), LLAT-M03 (hazard pointer), OS-A03 (epoch 회수), ARCH-M08 · LPERF-A03 (false sharing), HPC-M02 (P/E 코어 스케줄링), HPC-A05 (분산 work stealing), GAME-M03 (잡 시스템), OS-A01 · MCPP-A03 (잠드는 대기)
