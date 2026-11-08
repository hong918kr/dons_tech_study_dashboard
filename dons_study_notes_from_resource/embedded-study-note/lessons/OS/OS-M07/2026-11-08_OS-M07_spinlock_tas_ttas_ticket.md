---
id: OS-M07
time: 95분
updated: 2026-11-08
---
# OS-M07 스핀락 구현: TAS, TTAS, ticket lock

## 학습 목표

- C11 atomics 로 스핀락 세 가지를 직접 만든다: **TAS**(test-and-set, `atomic_exchange`), **TTAS**(test-and-test-and-set, 먼저 읽고 풀렸을 때만 exchange) + **상한 있는 지수 백오프**(bounded exponential backoff), **ticket lock**(`next`/`serving`, `fetch_add`). 각 연산의 memory_order 를 한 문장으로 설명할 수 있다.
- 스레드 1~12 개에서 **처리량** 과 **공정성**(스레드별 획득 횟수의 min/max/표준편차, Jain 지수)을 실측하고, "TAS → TTAS → ticket 으로 가면 무엇이 좋아지고 무엇을 잃나" 를 숫자로 말한다.
- Apple clang 이 이 맥(arm64-apple-macos)에서 세 락을 **어떤 명령으로** 내리는지 `-S` 로 확인한다 (기본 = LSE `SWPA`/`LDADD`/`CASA`, `-mcpu=apple-m1+nolse` = `LDAXR`/`STXR`). 그리고 그 차이가 TAS 락에서 처리량을 몇 배 바꾸는지 다시 잰다 (ARM-M04 결과의 재확인).
- ticket lock 의 약점(모든 대기자가 한 줄을 읽음, 다음 차례가 선점되면 **convoy**)을 코어보다 많은 스레드로 재현하고, MCS/CLH 큐 락·Linux qspinlock 이 왜 나왔는지 말한다.
- 펌웨어 쪽: RP2040 SIO 하드웨어 스핀락(Cortex-M0+ 에 LDREX 가 없어서), `spin_lock_irqsave` 의 순서, 단일 코어 MCU 에서는 스핀이 아니라 인터럽트 금지라는 것을 코드로 확인한다.

## 1. 문제

"스핀락을 짜 보세요. 그걸로 16 코어에서 돌리면 어떻게 되죠? 공정한가요?" — SMP RTOS 커널 락(FreeRTOS SMP, Zephyr SMP), 멀티코어 MCU(RP2040, STM32H7 듀얼 코어, ESP32), Linux 드라이버의 `spin_lock_irqsave`, 하이퍼바이저·부트로더의 코어 간 락이 전부 이 문제다. 면접관은 코드 자체보다 **캐시 일관성(cache coherence) 트래픽** 과 **공정성** 을 묻는다.

동시성 가이드 4층의 첫 레슨이다. 재료는 3층에 이미 있다: C-A03 이 `atomic_flag` 스핀락과 acquire/release 의 의미를, ARM-M04 가 같은 연산을 `LDAXR/STLXR` 와 LSE 로 손으로 썼고 "`SWPA` 락은 LL/SC 락보다 경합 때 2배 이상 느리다" 를 실측했다. LLAT-B06 은 "먼저 읽고 필요할 때만 RMW" 가 85배 차이를 낸다는 것을 보였다. 이 레슨은 그 조각들을 **세 가지 락 설계** 로 조립하고, 같은 벤치마크 위에서 서로 비교한다. 운영체제 쪽 이론은 OSTEP 28장(Locks)과 같다.

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">락이 들어 있는 캐시 라인 하나를 코어들이 어떻게 다루나 (M = 독점 수정, S = 공유 읽기)</text>
<text x="10" y="58" fill="currentColor" font-weight="bold">TAS</text>
<rect x="70" y="40" width="70" height="30" rx="4" class="f-warn" stroke="currentColor"/><text x="80" y="60" fill="currentColor">C1: swp</text>
<rect x="150" y="40" width="70" height="30" rx="4" class="f-warn" stroke="currentColor"/><text x="160" y="60" fill="currentColor">C2: swp</text>
<rect x="230" y="40" width="70" height="30" rx="4" class="f-warn" stroke="currentColor"/><text x="240" y="60" fill="currentColor">C3: swp</text>
<rect x="330" y="40" width="90" height="30" rx="4" class="f-acc" stroke="currentColor"/><text x="340" y="60" fill="currentColor">lock 라인 M</text>
<path d="M300 55 L328 55" stroke="currentColor" fill="none" stroke-width="2"/>
<text x="440" y="52" fill="currentColor">대기자 모두가 매번 "쓰기" → 라인이 코어 사이를</text>
<text x="440" y="68" fill="currentColor">계속 옮겨 다닌다 (핑퐁). 주인의 해제 store 도 줄을 선다</text>
<text x="10" y="148" fill="currentColor" font-weight="bold">TTAS</text>
<rect x="70" y="100" width="70" height="30" rx="4" class="f-ok" stroke="currentColor"/><text x="80" y="120" fill="currentColor">C1: ldr S</text>
<rect x="150" y="100" width="70" height="30" rx="4" class="f-ok" stroke="currentColor"/><text x="160" y="120" fill="currentColor">C2: ldr S</text>
<rect x="230" y="100" width="70" height="30" rx="4" class="f-ok" stroke="currentColor"/><text x="240" y="120" fill="currentColor">C3: ldr S</text>
<text x="330" y="120" fill="currentColor">잠겨 있는 동안: 각자 L1 의 S 사본만 읽음 (조용)</text>
<rect x="70" y="145" width="230" height="30" rx="4" class="f-warn" stroke="currentColor"/><text x="80" y="165" fill="currentColor">해제 순간: 모두 0 을 보고 동시에 swp</text>
<text x="330" y="165" fill="currentColor">해제 store = 모든 S 사본 무효화 → 한 명만 이기고</text>
<text x="330" y="181" fill="currentColor">나머지는 헛친 RMW (thundering herd) → 백오프가 이걸 흩는다</text>
<text x="10" y="248" fill="currentColor" font-weight="bold">ticket</text>
<rect x="70" y="210" width="230" height="30" rx="4" class="f-mut" stroke="currentColor"/><text x="80" y="230" fill="currentColor">들어올 때 딱 한 번 ldadd next</text>
<rect x="70" y="250" width="70" height="30" rx="4" class="f-ok" stroke="currentColor"/><text x="78" y="270" fill="currentColor">#6 대기</text>
<rect x="150" y="250" width="70" height="30" rx="4" class="f-ok" stroke="currentColor"/><text x="158" y="270" fill="currentColor">#7 대기</text>
<rect x="230" y="250" width="70" height="30" rx="4" class="f-ok" stroke="currentColor"/><text x="238" y="270" fill="currentColor">#8 대기</text>
<text x="330" y="230" fill="currentColor">헛친 RMW 0 번. 모두 serving 을 읽으며 기다림</text>
<text x="330" y="250" fill="currentColor">해제 = serving+1 (store 하나) → 번호 순서대로 = FIFO</text>
<text x="330" y="270" fill="currentColor">약점: 해제마다 모든 대기자의 사본이 무효화 (O(N) 트래픽),</text>
<text x="330" y="286" fill="currentColor">다음 번호가 선점되면 락이 비어도 아무도 못 들어감 (convoy)</text>
<line x1="10" y1="310" x2="730" y2="310" stroke="currentColor" stroke-dasharray="3 3"/>
<text x="10" y="334" fill="currentColor">한 줄 요약: TAS = 쓰며 기다림 / TTAS = 읽으며 기다림 / ticket = 번호표 + 읽으며 기다림.</text>
<text x="10" y="354" fill="currentColor">더 가려면 "각자 자기 라인에서" 기다리는 큐 락 (MCS, CLH, Linux qspinlock) — 이 레슨은 언급만.</text>
<text x="10" y="380" fill="currentColor">측정은 5. 풀이 Part B: 처리량은 TTAS+백오프, 공정성은 ticket, 코어보다 스레드가 많으면 뮤텍스.</text>
</svg>
```

| 설계 | 잠그기 | 풀기 | 공정성 | 경합 때 트래픽 |
|---|---|---|---|---|
| TAS | `while (exchange(&v, 1)) ;` | `store(&v, 0)` release | 보장 없음 (가까운/빠른 코어가 이김) | 기다리는 내내 RMW |
| TTAS + 백오프 | `while (load(&v)) pause; if (!exchange) return; backoff` | 같음 | 보장 없음 | 기다릴 땐 읽기, 해제 순간 한 번 몰림 |
| ticket | `my = fetch_add(&next, 1); while (load(&serving) != my) pause;` | `serving + 1` release | **FIFO** | 획득당 RMW 1 번, 해제마다 모든 대기자 재읽기 |

### Part A — 세 가지 락

- `tas_trylock` / `tas_lock` / `spin_unlock` — `atomic_uint` 하나와 `atomic_exchange_explicit`. `tas_lock` 은 실패한 exchange 횟수를 돌려준다 (헛친 RMW = 데모의 트래픽 지표).
- `backoff_next(cur, max)` — 0 → 1 → 2 → 4 … → `max` 에서 멈추는 순수 함수. `ttas_lock(l, backoff_max)` — 읽기로 기다리고, exchange 에 지면 백오프.
- `ticket_lock` (내 번호표를 돌려줌) / `ticket_unlock` / `ticket_trylock` (기다리는 사람이 없을 때만 번호표를 뽑는다). 번호표가 `0xFFFFFFFF → 0` 으로 넘어가도 동작해야 한다.

### Part B — 정확성은 CHECK, 성능·공정성은 데모

- `fair_stats(cnt, n)` — 스레드별 획득 횟수 → min / max / mean / 모표준편차 / Jain 지수 `(Σx)² / (n·Σx²)` (1 = 완전 공평, 1/n = 한 명이 독차지). 고정 입력으로 검증.
- 상호 배제 스트레스: 락 4종(TAS, TTAS, TTAS+백오프, ticket) × 6 스레드 × 4만 번. 임계 구역 안 인원의 최댓값 == 1, 일반 변수 두 개의 합계가 정확해야 한다.
- **강제 스케줄 FIFO**: main 이 ticket lock 을 쥔 채 대기자를 1 명씩 출발시키고, 그 대기자가 번호표를 뽑은 것(`next` 증가)을 확인한 뒤 다음을 출발시킨다. 해제 후 입장 순서 == 도착 순서여야 한다. (TAS/TTAS 는 같은 스케줄에서 FIFO 가 아니다 → `demo_fifo.c` 로 관찰만.)
- 데모: `demo_bench.c` (1~12 스레드 처리량 + 공정성, LSE 판/LL·SC 판), `demo_convoy.c` (스레드 > 코어), `demo_fifo.c`, `demo_asm.c` (명령 표).

### Part C — 펌웨어: 하드웨어 스핀락과 irqsave

- RP2040 SIO 스핀락 레지스터 모델(주어짐): `SPINLOCKn` 을 **읽으면** 풀려 있던 경우 잡고 0 이 아닌 값, 이미 잡혀 있으면 0. **쓰면** 풀림. 그 위에 `hw_spin_lock` / `hw_spin_trylock` / `hw_spin_unlock` 을 쓰고, 두 "코어" 가 레코드 4 워드를 로그 링에 섞이지 않게 쓰는지 검증한다.
- `spin_lock_irqsave(cpu)` / `spin_unlock_irqrestore(cpu, flags)` — 같은 코어의 ISR 이 같은 락을 잡으려 할 때 자기 교착(self-deadlock)이 나지 않는 순서. 중첩(바깥에서 이미 IRQ 를 끈 경우)도 지킨다.

## 2. Problem (English)

> Implement three spinlocks with C11 `<stdatomic.h>`:
>
> 1. **TAS**: `bool tas_trylock(spin_t *)`, `unsigned tas_lock(spin_t *)` (returns the number of failed exchanges), `void spin_unlock(spin_t *)`.
> 2. **TTAS with bounded exponential backoff**: `unsigned backoff_next(unsigned cur, unsigned max)` (0→1, doubles, saturates at `max`), `unsigned ttas_lock(spin_t *, unsigned backoff_max)` — spin on a plain load, attempt the exchange only when the lock looks free, back off after losing.
> 3. **Ticket lock**: `unsigned ticket_lock(ticket_t *)` (returns the ticket), `void ticket_unlock(ticket_t *)`, `bool ticket_trylock(ticket_t *)` (succeeds only if nobody holds or waits). Must survive counter wrap-around.
>
> Then: `fair_stats()` (min/max/mean/stddev/Jain index of per-thread acquisition counts); prove mutual exclusion (never more than one thread inside, exact totals) and FIFO order of the ticket lock under a forced schedule. Firmware part: a lock on top of an RP2040-style hardware spinlock register (read = try-claim, write = release), and `spin_lock_irqsave` / `spin_unlock_irqrestore` that cannot self-deadlock with an ISR on the same core.
>
> Follow-ups: benchmark throughput and fairness for 1..12 threads; which instructions does the compiler emit (LL/SC vs LSE) and does it matter; what happens with more threads than cores?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, 0.09 초 만에 끝남 — 단위 테스트가 실패하면 스트레스는 건너뛰므로 멈추지 않는다):

```text
FAIL starter.c:253  tas_trylock(&s) == true
PASS tas_trylock(&s) == false
PASS atomic_load(&s.v) == 0u
FAIL starter.c:257  tas_lock(&s) == 0u && atomic_load(&s.v) == 1u
…
FAIL starter.c:272  ticket_lock(&t) == 0u && atomic_load(&t.next) == 1u && atomic_load(&t.serving) == 0u
…
SKIP 5 stress checks (Part B): unit checks failed
PASS sio_spinlock_read(&sio, 0u) == 1u && sio_spinlock_read(&sio, 0u) == 0u
FAIL starter.c:338  hw_spin_trylock(&sio, 3u) == true && atomic_load(&sio.state) == 0x8u
…
SKIP 1 stress check (Part C): unit checks failed
8/38 passed
```

TODO 는 A1–A7 (락), B1 (`fair_stats`), C1 (하드웨어 스핀락 락), C2 (irqsave). 스텁은 잘못된 값을 돌려줄 뿐 돌지 않는다.

## 4. 힌트

<details><summary>힌트 1 — TAS 의 memory_order</summary>

`exchange` 는 옛 값을 돌려준다. 옛 값이 0 이었으면 내가 잡은 것. 잡는 쪽은 acquire(임계 구역의 접근이 위로 못 올라감), 푸는 쪽은 release store(임계 구역의 쓰기가 해제보다 먼저 보임). 이유는 C-A03 에서 다 했다 — 여기서는 그대로 쓴다.
</details>

<details><summary>힌트 2 — TTAS 의 바깥 루프</summary>

"읽어서 0 이 보였다" 는 잡았다는 뜻이 아니다. 그 사이 다른 코어가 먼저 exchange 할 수 있다. 그래서 `for (;;) { while (load) pause; if (exchange == 0) return; backoff; }` 처럼 exchange 결과를 다시 확인하고, 지면 처음으로 돌아간다. 기다리는 load 는 relaxed 로 충분하다 — 동기화는 성공한 exchange 의 acquire 가 만든다.
</details>

<details><summary>힌트 3 — 백오프의 상한</summary>

`cur * 2` 를 그대로 두면 언젠가 넘치고(overflow), 너무 오래 쉬면 락이 비어 있는데도 아무도 안 들어간다. `cur >= max / 2 ? max : cur * 2` 처럼 상한에서 멈춘다. `cur == 0` 은 시작값 → 1.
</details>

<details><summary>힌트 4 — ticket lock 의 해제와 trylock</summary>

`serving` 은 락 주인만 쓴다. 그러니 해제는 RMW 가 필요 없다: relaxed load → +1 → release store. trylock 은 "지금 `next == serving`" 일 때만 `next` 를 `serving + 1` 로 바꾸는 CAS 한 번이다. CAS 가 `next` 의 값을 직접 비교하므로, 그 사이 누가 번호표를 뽑았으면 실패한다.
</details>

<details><summary>힌트 5 — irqsave 의 순서</summary>

잡을 때: IRQ 먼저 끄고 → 락. (락 먼저면 그 틈에 ISR 이 들어와 같은 락을 돈다.) 풀 때: 락 먼저 풀고 → IRQ 복원. (복원 먼저면 펜딩된 ISR 이 아직 잡힌 락을 돈다.) 복원은 "켜기" 가 아니라 **이전 상태로 되돌리기** 다.
</details>

## 5. 풀이

### Part A — TAS: exchange 한 줄

```c
static bool tas_trylock(spin_t *l) {
    return atomic_exchange_explicit(&l->v, 1u, memory_order_acquire) == 0u;
}
static unsigned tas_lock(spin_t *l) {
    unsigned fails = 0;
    while (atomic_exchange_explicit(&l->v, 1u, memory_order_acquire) != 0u) fails++;
    return fails;
}
static void spin_unlock(spin_t *l) { atomic_store_explicit(&l->v, 0u, memory_order_release); }
```

- **왜 `atomic_flag` 가 아니라 `atomic_uint` 인가:** C11 7.17.8 의 `atomic_flag` 는 lock-free 가 보장된 유일한 타입이지만 **읽기만 하는 연산이 없다**. TTAS 는 "읽으며 기다리기" 가 핵심이라 `atomic_uint` 로 통일했다 (C-A03 의 `atomic_flag` 판과 같은 명령이 나오는 것을 아래 asm 표에서 확인).
- `spin_t` 는 `_Alignas(128)` — `sysctl hw.cachelinesize` 가 128 이다. 락 라인과 보호 대상 데이터가 같은 라인이면 대기자들의 RMW 가 주인의 데이터 접근까지 방해한다 (false sharing 자체는 ARCH-M08).
- 반환값 `fails` 는 성능용 관측값이다. 테스트는 "경합이 없으면 0" 만 본다.

### Part A — TTAS + 상한 있는 지수 백오프

```c
static unsigned backoff_next(unsigned cur, unsigned max) {
    if (cur == 0u) return 1u <= max ? 1u : max;
    return cur >= max / 2u ? max : cur * 2u;
}
static unsigned ttas_lock(spin_t *l, unsigned backoff_max) {
    unsigned fails = 0, delay = 0;
    for (;;) {
        while (atomic_load_explicit(&l->v, memory_order_relaxed) != 0u) cpu_relax();
        if (atomic_exchange_explicit(&l->v, 1u, memory_order_acquire) == 0u) return fails;
        fails++;
        if (backoff_max) {
            delay = backoff_next(delay, backoff_max);
            for (unsigned i = 0; i < delay; i++) cpu_relax();
        }
    }
}
```

단계별로 보면:

| 단계 | 하는 일 | 캐시 라인 |
|---|---|---|
| 1. 잠겨 있음 | `ldr` 로 1 을 보며 `cpu_relax()` | 각 코어가 S(공유) 사본을 L1 에 두고 읽기만 → 버스 조용 |
| 2. 해제 | 주인의 `stlr` 0 | 모든 S 사본 무효화 |
| 3. 몰림 | 모두 0 을 보고 `swpa` | 한 명만 0 을 받음. 나머지는 1 을 받음 = 헛친 RMW |
| 4. 진 사람 | `delay` 만큼 쉼, 다음엔 2 배 | 다음 해제 때 몰리는 인원이 줄어든다 |

- **백오프는 "진 다음" 에만** 넣었다. 처음부터 쉬면 경합이 없을 때도 느려진다. 상한(`max`)이 없으면 오래 진 스레드가 락이 비어도 잠만 잔다 — 지연이 커지고 불공평이 더 심해진다.
- `cpu_relax()` = 스핀 루프 힌트. 이 레슨 코드는 `yield`(arm64) / `pause`(x86) 를 쓴다. 그런데 이 맥에서 **`yield` 는 사실상 아무 일도 안 한다** — `demo_bench.c` 첫 줄 실측: `yield 0.22 ns, isb 7.84 ns` (1e7 회 루프, 루프 자체 비용 포함). 그래서 벤치에서는 백오프의 쉬는 명령을 `isb` 로 바꾼 판(`TTAS+bo(isb)`)도 쟀고, 그게 가장 빨랐다 (아래). Rust 표준 라이브러리의 `spin_loop()` 가 aarch64 에서 `isb` 를 쓰도록 바뀐 것도 같은 이유로 알려져 있다 (미검증).

### Part A — ticket lock

```c
static unsigned ticket_lock(ticket_t *t) {
    unsigned my = atomic_fetch_add_explicit(&t->next, 1u, memory_order_relaxed);
    while (atomic_load_explicit(&t->serving, memory_order_acquire) != my) cpu_relax();
    return my;
}
static void ticket_unlock(ticket_t *t) {
    unsigned s = atomic_load_explicit(&t->serving, memory_order_relaxed);   /* 소유자만 쓴다 */
    atomic_store_explicit(&t->serving, s + 1u, memory_order_release);
}
static bool ticket_trylock(ticket_t *t) {
    unsigned s = atomic_load_explicit(&t->serving, memory_order_relaxed);
    unsigned n = s;
    return atomic_compare_exchange_strong_explicit(&t->next, &n, s + 1u,
                                                   memory_order_acquire, memory_order_relaxed);
}
```

- **`fetch_add` 는 relaxed 로 충분:** 번호표가 겹치지 않는 것은 RMW 의 원자성(같은 객체의 수정 순서는 하나) 덕이다. 임계 구역 진입의 acquire 는 `serving` 을 읽는 load 가 준다 — 이전 주인의 release store 와 짝을 이룬다.
- **해제는 RMW 가 아니다:** `serving` 을 쓰는 것은 주인 한 명뿐이라 load + store 로 된다. `fetch_add(&serving, 1, release)` 도 맞지만 쓸데없는 RMW 다.
- **wrap-around:** 둘 다 `unsigned` 이고 `==` 로만 비교하므로 `0xFFFFFFFF → 0` 을 넘어도 맞다 (부호 없는 산술은 2^32 로 감싼다, C11 6.2.5p9). 대기자가 2^32 명을 넘지 않는 한. Linux arm32 처럼 16 비트 반쪽을 쓰면 한계는 65535 명 (추가 연습 3).
- **trylock 이 맞는 이유:** `serving == s` 를 읽은 뒤 CAS 가 `next == s` 를 확인하고 성공했다면, 그 순간 발급된 번호표가 `s` 개뿐이고 `serving ≤ next` 이며 `serving` 은 줄지 않으므로 `serving == s` — 아무도 쥐지도 기다리지도 않았다.
- **FIFO:** 입장 순서 = 번호표 순서 = `fetch_add` 가 원자적으로 정한 순서. 이것이 TAS/TTAS 와의 본질적인 차이다.

### 실제로 나온 명령 — `demo_asm.c`

<!-- code: demo_asm.c -->

```bash
cc -std=c11 -O2 -S demo_asm.c -o -                         # 기본: -target-cpu apple-m1 (cc -### 로 확인)
cc -std=c11 -O2 -mno-outline-atomics -S demo_asm.c -o -     # 기본과 같음
cc -std=c11 -O2 -march=armv8-a -S demo_asm.c -o -           # 기본과 같음 (LSE 가 꺼지지 않는다!)
cc -std=c11 -O2 -march=armv8-a+nolse -S demo_asm.c -o -     # 역시 swpa — 꺼지지 않음
cc -std=c11 -O2 -mcpu=apple-m1+nolse -S demo_asm.c -o -     # LL/SC (ldapr 은 유지)
cc -std=c11 -O2 -mcpu=generic -S demo_asm.c -o -            # LL/SC + ldar (cortex-a53 도 같음)
cc -std=c11 -O2 -mcpu=generic+lse -S demo_asm.c -o -        # swpa / ldadd / casa 로 돌아오지만 acquire load 는 ldar
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -std=c11 -O2 -S demo_asm.c -o -
```

| 함수 | Apple 기본 (`cc -O2`, `-mno-outline-atomics`, `-march=armv8-a`, `-mcpu=cortex-a55`) | `-mcpu=apple-m1+nolse` | `-mcpu=generic` / `cortex-a53` | Cortex-M4 |
|---|---|---|---|---|
| `tas_lock` | `swpa w8, w9, [x0]` / `cbnz w9` | `ldaxr` / `stxr` / `orr` / `cbnz` | `ldaxr w9,[x0]` / `stxr w10,w8,[x0]` / `orr w9,w10,w9` / `cbnz w9` | `ldrex` / `strex` / `cmp` / `bne` / `dmb sy` (4번 펼침) |
| `flag_lock` (`atomic_flag`) | `swpab` / `tbnz #0` | `ldaxrb` / `stxrb` | `ldaxrb` / `stxrb` / `cbnz` / `tbnz` | `ldrexb` / `strexb` / `dmb sy` |
| `spin_unlock` | `stlr wzr, [x0]` | `stlr` | `stlr wzr, [x0]` | `dmb sy` / `str` |
| `ttas_lock` 대기 | `ldr` / `cbnz` / `yield` → `swpa` | `ldr` / `yield` → `ldaxr`/`stxr` | 같음 | `yield` / `ldr` → `ldrex`/`strex` / `dmb sy` |
| `ttas_lock_isb` 대기 | `ldr` / `cbnz` / `isb` | `isb` | `isb` | (M 에는 `isb` 힌트를 안 넣음) |
| `ticket_lock` | `ldadd w8, w8, [x0]` → `ldapr w9` / `cmp` / `yield` | `ldxr`/`add`/`stxr` → `ldapr` | `ldxr` / `add` / `stxr` / `cbnz` → `ldar w9` 루프 | `ldrex`/`adds`/`strex` → `ldr` / `dmb sy` |
| `ticket_unlock` | `ldr` / `add` / `stlr` | 같음 | `ldr` / `add` / `stlr` | `ldr` / `dmb sy` / `adds` / `str` |
| `ticket_trylock` | `casa w10, w9, [x0]` / `cset` | `ldaxr` / `cmp` / `b.ne` → `clrex` / `stxr` | `ldaxr` / `cmp` / `b.ne` / `stxr`, 실패 경로 `clrex` | `ldrex` / `cmp` / `bne` / `strex`, 실패 `clrex`, 성공 `dmb sy` |

이 표에서 읽을 것:

1. **Apple clang 의 기본은 LSE 인라인이다.** `cc -### -c` 에 `"-target-cpu" "apple-m1"` 가 보이고, exchange → `SWPA`, relaxed `fetch_add` → `LDADD`(A/L 없음), acquire CAS → `CASA`. `-mno-outline-atomics` 는 아무것도 바꾸지 않았다 (outline atomics 는 Linux 배포판용 — ARM-M04). `-march=armv8-a` 나 `-march=armv8-a+nolse` 도 LSE 를 끄지 못했다. 끄는 방법은 **CPU 쪽에서 `-mcpu=apple-m1+nolse`** 또는 LSE 없는 CPU(`generic`, `cortex-a53`)를 고르는 것이었다.
2. **acquire load 는 `LDAPR`** (FEAT_LRCPC 의 RCpc acquire) 이고, `-mcpu=generic` 과 `-mcpu=generic+lse` 에서는 `LDAR` (LSE 는 켜도 LRCPC 는 없는 CPU). C11 acquire 는 RCpc 로 충분하다 (ARM-M05).
3. **LL/SC 판 TAS 는 `ldaxr` 바로 뒤에 무조건 `stxr`** — 값이 1 이어도 1 을 다시 쓴다. exchange 의 의미가 원래 그렇다. TTAS 의 "먼저 `ldr`" 이 이걸 막는다.
4. **Cortex-M4** 는 같은 C 가 `ldrex/strex` 루프 + `dmb sy` 다 (C-A03 표와 같음). M0/M0+ 에는 이 명령이 없다 → Part C 의 RP2040.

### Part B — 처리량: `demo_bench.c`

<!-- code: demo_bench.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o bench_lse                       # Apple 기본 (LSE)
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread -mcpu=apple-m1+nolse demo_bench.c -o bench_llsc  # LL/SC
./bench_lse; ./bench_llsc            # 기본: 칸당 80 ms × 3 회, NCS=32 → 각 13 초
xcrun llvm-objdump -d --no-show-raw-insn bench_lse | awk '/<_worker>:/,/^$/' | grep -c swpa   # 4
```

`worker` 함수 역어셈에서 센 명령: LSE 판 `swpa ×4, ldadd ×1, ldapr ×3, stlr ×2, yield ×4, isb ×2`, LL/SC 판 `ldaxr ×4, stxr ×5, ldxr ×1, ldapr ×2, stlr ×2` — 의도대로 두 판이 갈렸다.

조건: Apple M6 (`sysctl -n machdep.cpu.brand_string`), `hw.perflevel0/1/2.physicalcpu` = 2 / 4 / 6 (Super 2 + Performance 4 + Efficiency 6 = 12 코어), `hw.cachelinesize` 128, Apple clang version 21.0.0 (clang-2100.0.123.102). 임계 구역 = 공유 라인 하나에 일반 RMW 2 개, 바깥 = 64비트 LCG 32 번 (스레드끼리 독립). 칸 = 80 ms 동안 모두 돌린 획득 수 / 시간, 3 회 중 중앙값. **같은 맥에서 다른 에이전트들의 벤치마크가 동시에 돌았다** (`uptime` load average 6.3 → 8.6 → 8.8, 측정 중). 절대값보다 **비율** 을 보라. macOS 에는 코어 고정이 없어 6 스레드 이상은 E 코어에도 올라간다.

LSE 판 (Apple 기본, load 6.3–8.6), Mops/s:

| 락 \ 스레드 | 1 | 2 | 4 | 6 | 8 | 10 | 12 |
|---|---|---|---|---|---|---|---|
| TAS (`swpa`) | 34.7 | 34.9 | 25.7 | 17.7 | 8.3 | 5.2 | **3.1** |
| TTAS (`yield`) | 34.3 | 23.9 | **7.9** | 6.3 | 9.4 | 8.0 | 7.1 |
| TTAS + 백오프 (`yield`) | 34.7 | 33.8 | 8.4 | 7.0 | 9.8 | 9.4 | 8.8 |
| TTAS + 백오프 (`isb`) | 34.5 | 35.0 | **33.8** | 16.3 | 12.5 | 11.4 | **11.3** |
| ticket | 34.5 | 13.7 | 9.4 | 9.1 | 6.7 | 4.8 | **0.7** |
| pthread_mutex | 34.2 | 17.7 | 13.2 | 13.5 | 14.0 | 14.5 | 14.3 |
| os_unfair_lock | 35.4 | 7.7 | 17.4 | 15.7 | 15.9 | 17.0 | 16.2 |

LL/SC 판 (`-mcpu=apple-m1+nolse`, load 8.8), 스핀락 줄만:

| 락 \ 스레드 | 1 | 2 | 4 | 6 | 8 | 10 | 12 |
|---|---|---|---|---|---|---|---|
| TAS (`ldaxr/stxr`) | 35.2 | 19.3 | 25.4 | 22.6 | 18.3 | 20.2 | **19.0** |
| TTAS (`yield`) | 35.2 | 32.9 | 7.1 | 17.3 | 3.8 | 15.0 | 12.5 |
| TTAS + 백오프 (`isb`) | 35.2 | 33.0 | 33.2 | 22.2 | 18.9 | 20.0 | 19.9 |
| ticket | 35.2 | 13.0 | 11.7 | 8.7 | 6.8 | 3.5 | 3.5 |

같은 실행 파일을 몇 분 전(load 4.1–6.3)에도 돌렸다. 12 스레드 칸: LSE TAS 2.8, TTAS+bo(isb) 11.4, ticket 3.3, mutex 14.8 / LL/SC TAS 19.8, ticket 1.9. 숫자는 흔들리지만 순서는 같았다.

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">처리량 Mops/s (높을수록 좋음) — Apple M6 실측 중앙값, load 6–9, 비율만 볼 것</text>
<g stroke="currentColor" stroke-width="0.5" stroke-dasharray="1 4">
<line x1="60" y1="240" x2="560" y2="240"/><line x1="60" y1="180" x2="560" y2="180"/><line x1="60" y1="120" x2="560" y2="120"/><line x1="60" y1="60" x2="560" y2="60"/></g>
<path d="M60 50 L60 300 L560 300" stroke="currentColor" fill="none"/>
<g fill="currentColor"><text x="44" y="304">0</text><text x="38" y="244">10</text><text x="38" y="184">20</text><text x="38" y="124">30</text><text x="38" y="64">40</text>
<text x="56" y="316">1</text><text x="101" y="316">2</text><text x="192" y="316">4</text><text x="283" y="316">6</text><text x="374" y="316">8</text><text x="462" y="316">10</text><text x="553" y="316">12</text>
<text x="270" y="334">스레드 수</text></g>
<polyline points="60.0,91.9 105.5,90.8 196.4,145.6 287.2,193.6 378.2,250.1 469.1,268.9 560.0,281.6" fill="none" stroke="currentColor" stroke-width="2.5"/>
<polyline points="60.0,94.0 105.5,156.4 196.4,252.7 287.2,262.5 378.2,243.6 469.1,251.9 560.0,257.7" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="2 3"/>
<polyline points="60.0,93.3 105.5,90.2 196.4,97.1 287.2,202.5 378.2,225.3 469.1,231.4 560.0,232.5" fill="none" stroke="currentColor" stroke-width="2.5" stroke-dasharray="10 4"/>
<polyline points="60.0,92.9 105.5,218.0 196.4,243.9 287.2,245.5 378.2,259.7 469.1,271.4 560.0,295.7" fill="none" stroke="currentColor" stroke-width="1.5"/>
<polyline points="60.0,95.0 105.5,193.7 196.4,220.8 287.2,219.1 378.2,215.8 469.1,213.1 560.0,214.3" fill="none" stroke="currentColor" stroke-width="1" stroke-dasharray="6 2 1 2"/>
<polyline points="60.0,89.0 105.5,184.1 196.4,147.7 287.2,164.6 378.2,190.5 469.1,178.6 560.0,186.2" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="4 4"/>
<g class="f-warn" stroke="currentColor"><circle cx="560" cy="281.6" r="4"/><circle cx="560" cy="295.7" r="4"/></g>
<g class="f-ok" stroke="currentColor"><circle cx="196.4" cy="97.1" r="4"/><circle cx="560" cy="186.2" r="4"/></g>
<g fill="currentColor">
<text x="568" y="190">TAS, LL/SC 판 (19.0)</text>
<text x="568" y="218">pthread_mutex (14.3)</text>
<text x="568" y="236">TTAS+백오프 isb (11.3)</text>
<text x="568" y="261">TTAS yield (7.1)</text>
<text x="568" y="281">TAS, LSE swpa (3.1)</text>
<text x="568" y="299">ticket (0.7)</text>
<text x="205" y="92">TTAS+백오프 isb: 4 스레드까지 평평</text></g>
</svg>
```

읽는 법 (측정 = 위 표, 원인 = 추정 — Apple 은 일관성 프로토콜을 공개하지 않는다):

1. **1 스레드 칸은 모두 같다** (34–35 Mops ≈ 29 ns/회). 바깥 작업(LCG 32 번)이 시간을 거의 다 쓰고, 경합 없는 락 한 쌍은 몇 ns 다. "경합이 없으면 아무 락이나 빠르다."
2. **LSE TAS 는 스레드가 늘수록 무너진다** (12 스레드 3.1). `demo_bench` 의 "failed exchanges per acquire" 가 12 스레드에서 **50** — 한 번 잡을 때마다 다른 코어들이 50 번 헛쓰기를 했다. 쓰기마다 라인 독점권이 이동하고, 주인의 해제 `stlr` 도 그 줄에 선다.
3. **같은 TAS 가 LL/SC 로 내리면 6배 빠르다** (19.0 vs 3.1, 앞 실행 19.8 vs 2.8). ARM-M04 의 "`SWPA` 락은 LL/SC 락보다 2배 이상 느리다" 를 C11 TAS 로 다시 확인한 셈이고, 이 벤치에서는 차이가 더 컸다. 추정: LL/SC 대기자는 `stxr` 가 실패하면 쓰기 없이 다시 `ldaxr` 로 돌아가지만, `swpa` 는 매번 완전한 쓰기 트랜잭션이다. (LL/SC 판의 "failed exchanges" 가 0.1 근처인 것은 `stxr` 실패 재시도가 exchange 안쪽 루프라 세지 않기 때문 — 비교 불가.)
4. **TTAS(`yield`) 는 기대만큼 좋지 않다** — 4 스레드에서 7.9 로 TAS(25.7)보다도 나빴다. `yield` 가 0.22 ns(사실상 nop)라서 대기자들이 `ldr` 를 쉬지 않고 두드리고, 해제 때마다 전원이 동시에 `swpa` 로 몰린다. 지수 백오프를 넣어도 쉬는 명령이 `yield` 면 별 차이가 없다(8.4).
5. **쉬는 명령을 `isb` 로 바꾸면 결과가 바뀐다:** TTAS+백오프(`isb`)는 4 스레드까지 33.8 로 평평하고, 12 스레드에서도 스핀락 중 최고(11.3, LL/SC 판 19.9). 헛친 exchange 도 획득당 1.2 이하. **"TTAS + 백오프" 의 효과는 백오프가 실제로 시간을 쓸 때만 나온다** — 어떤 명령을 냈는지 확인해야 하는 이유다.
6. **ticket 은 처리량이 가장 나쁘다** (2 스레드부터 13.7, 12 스레드 0.7). 매 해제가 정확히 "다음 번호" 에게만 넘어가므로, 그 스레드가 느린 E 코어에 있거나 다른 프로세스에 밀려 CPU 를 잃었으면 모두가 기다린다 (아래 convoy). 반대로 TAS 계열은 "지금 가장 가까이 있는 코어" 가 가져가 처리량이 좋다 — 그 대가가 불공정이다.
7. **pthread_mutex / os_unfair_lock** 은 2 스레드 이후 13–17 로 평평하다. 경합이 심하면 잠들어서(커널 대기) 헛도는 코어가 없어진다. 스핀락이 이길 수 있는 곳은 "임계 구역이 매우 짧고, 스레드 수 ≤ 코어 수, 선점이 없는" 경우 — 즉 커널·RTOS·코어당 스레드 1 개인 펌웨어다. 뮤텍스의 내부(경합 시에만 커널)는 OS-A01 (futex), MCPP-A03 (atomic wait).

### Part B — 공정성: 스레드별 획득 횟수

같은 실행의 공정성 출력 (CoV = 스레드별 획득 횟수의 표준편차/평균, 0 = 완전 공평):

| 락 | CoV 4T | CoV 8T | CoV 12T | 12T min / max | max/min | Jain 12T |
|---|---|---|---|---|---|---|
| TAS (LSE) | 0.184 | 0.355 | 0.327 | 7 813 / 31 670 | 4.1 | 0.903 |
| TTAS + 백오프 (`isb`) | 0.085 | 0.146 | 0.454 | 29 252 / 124 362 | 4.3 | 0.829 |
| ticket | 0.000 | 0.000 | 0.029 | 4 698 / 5 031 | 1.1 | 0.999 |
| pthread_mutex | 0.042 | 0.058 | 0.054 | 93 662 / 111 159 | 1.2 | 0.997 |
| os_unfair_lock | 0.020 | 0.152 | 0.267 | 53 979 / 158 749 | 2.9 | 0.933 |
| TAS (LL/SC 판) | 0.528 | 0.736 | 0.671 | 42 628 / 346 144 | 8.1 | 0.689 |

바깥 작업을 없애면(`./bench_llsc 60 1 0`, NCS=0 — 해제하자마자 다시 잡으러 감) 불공정이 극단으로 간다. 실제 출력:

```text
min/max per-thread acquires and Jain index at 12 threads
TAS             min         1  max   1131758  max/min 1131758.0  jain 0.433
TTAS            min      5401  max    305498  max/min    56.6  jain 0.456
TTAS+bo         min     27042  max    269184  max/min    10.0  jain 0.800
ticket          min     17819  max     20557  max/min     1.2  jain 0.996
pthread_mutex   min    210408  max    233908  max/min     1.1  jain 0.999
```

60 ms 동안 한 스레드는 **1 번**, 다른 스레드는 113만 번 잡았다 — 굶주림(starvation). 해제한 코어는 라인을 아직 독점(M)으로 갖고 있어서, 바로 다음 exchange 가 다른 코어의 요청보다 거의 항상 먼저 끝난다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">공정성: 12 스레드에서 스레드별 획득 횟수의 CoV (낮을수록 공평) — 실측</text>
<path d="M200 40 L200 270" stroke="currentColor" fill="none"/>
<g fill="currentColor"><text x="196" y="286" text-anchor="middle">0</text><text x="396" y="286" text-anchor="middle">0.25</text><text x="596" y="286" text-anchor="middle">0.5</text></g>
<g stroke="currentColor" stroke-width="0.5" stroke-dasharray="1 4"><line x1="400" y1="40" x2="400" y2="270"/><line x1="600" y1="40" x2="600" y2="270"/></g>
<g fill="currentColor">
<text x="10" y="60">TAS (LL/SC 판)</text><text x="10" y="90">TTAS+백오프 isb</text><text x="10" y="120">TAS (LSE swpa)</text>
<text x="10" y="150">os_unfair_lock</text><text x="10" y="180">pthread_mutex</text><text x="10" y="210">ticket</text></g>
<rect x="200" y="46" width="537" height="20" class="f-warn" stroke="currentColor"/>
<rect x="200" y="76" width="363" height="20" class="f-warn" stroke="currentColor"/>
<rect x="200" y="106" width="262" height="20" class="f-warn" stroke="currentColor"/>
<rect x="200" y="136" width="214" height="20" class="f-mut" stroke="currentColor"/>
<rect x="200" y="166" width="43" height="20" class="f-ok" stroke="currentColor"/>
<rect x="200" y="196" width="23" height="20" class="f-ok" stroke="currentColor"/>
<g fill="currentColor"><text x="590" y="61">0.671</text><text x="570" y="91">0.454</text><text x="468" y="121">0.327</text>
<text x="420" y="151">0.267</text><text x="250" y="181">0.054</text><text x="230" y="211">0.029</text>
<text x="10" y="246">ticket 은 P 코어·E 코어 섞여도 거의 같은 횟수 (번호표가 한 바퀴에 한 번씩 돈다).</text>
<text x="10" y="264">TAS 계열은 빠른 코어 · 방금 푼 코어가 계속 이긴다.</text></g>
</svg>
```

- ticket 이 E 코어가 섞인 12 스레드에서도 CoV ≈ 0 인 이유: 모두가 항상 줄에 서 있으면 한 바퀴에 한 번씩 차례가 온다. 대신 **가장 느린 코어의 속도로** 바퀴가 돈다 — 처리량이 낮은 이유와 같은 동전의 양면이다.
- Jain 지수는 "몇 명이 공평하게 나눠 가진 것과 같은가 / n". 0.433 × 12 ≈ 5.2 명분.
- 공정성 수치는 **실행마다 크게 흔들린다** (같은 칸이 0.2~0.7). 테스트로 CHECK 하지 않고, CHECK 하는 것은 아래의 결정적인 FIFO 성질이다.

### Part B — FIFO 를 결정적으로 검증하기

"ticket 은 FIFO" 를 타이밍에 기대지 않고 확인하려면 **도착 순서를 강제** 해야 한다. 번호표를 뽑은 순간이 바깥에서 보이므로(`next` 증가) 가능하다:

```c
(void)ticket_lock(&ft);                                   /* main = 번호표 0 */
for (int i = 0; i < FIFO_W; i++) {
    pthread_create(&th[i], NULL, fifo_waiter, &fa[i]);
    if (!wait_next(&ft, (unsigned)i + 2u, 2.0)) started = false;   /* i 번째가 번호표를 뽑을 때까지 */
}
ticket_unlock(&ft);
```

`wait_next` 는 2 초 타임아웃이 있어서, 번호표를 뽑지 않는 시작 코드는 FAIL 로 끝난다(멈추지 않는다). TAS/TTAS 는 "번호표를 뽑는" 관측 가능한 순간이 없어 같은 방식의 CHECK 가 불가능하다 — 관찰만 한다 (`demo_fifo.c`, 대기자 6 명을 3 ms 간격으로 출발, 20 회):

<!-- code: demo_fifo.c -->

```text
TAS     FIFO in  0/20 runs   (run 0 order: 1 3 5 4 6 2 )
TTAS    FIFO in  0/20 runs   (run 0 order: 6 5 2 1 3 4 )
ticket  FIFO in 20/20 runs   (run 0 order: 1 2 3 4 5 6 )
```

두 번째 실행: TAS 0/20 (`4 1 3 5 2 6`), TTAS 0/20 (`1 3 5 2 4 6`), ticket 20/20. 가장 먼저 온 1 번이 다섯 번째로 들어가기도 한다.

상호 배제 스트레스는 임계 구역 안에서 원자 카운터 `inside` 를 올렸다 내리고 최댓값을 기록한다 (`max_inside == 1`). 이 카운터는 relaxed atomic 이라 그 자체로 레이스가 아니고, 락이 틀리면 2 이상이 관측된다. 일반 변수 `plain += 1; plain2 += 3` 은 락이 틀리면 합계가 모자란다. 정답 코드의 실제 출력:

```text
  TAS           max_inside=1 plain=240000
  TTAS          max_inside=1 plain=240000
  TTAS+backoff  max_inside=1 plain=240000
  ticket        max_inside=1 plain=240000
```

TSan 은 사용자가 만든 스핀락을 "락" 으로 알지는 못하지만, `exchange`(acquire) ↔ `store`(release) 의 happens-before 는 C11 모델대로 추적한다. 그래서 `plain` 접근에 레이스 보고가 없다 (정답·추가 연습 모두 TSan 보고 0). 반대로 TSan 은 스핀락으로 인한 교착을 보고하지 못한다 (TOOL-M09).

### Part B — ticket 의 약점: 코어보다 스레드가 많으면 convoy

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="20" fill="currentColor" font-weight="bold" font-size="13">convoy: 다음 번호(#6)가 선점되면 락이 비어 있어도 아무도 못 들어간다</text>
<text x="10" y="58" fill="currentColor">코어 A</text><text x="10" y="98" fill="currentColor">코어 B</text><text x="10" y="138" fill="currentColor">코어 C</text>
<rect x="70" y="40" width="90" height="26" class="f-acc" stroke="currentColor"/><text x="78" y="58" fill="currentColor">#5 임계 구역</text>
<rect x="160" y="40" width="40" height="26" class="f-mut" stroke="currentColor"/><text x="164" y="58" fill="currentColor">해제</text>
<rect x="70" y="80" width="60" height="26" class="f-ok" stroke="currentColor"/><text x="76" y="98" fill="currentColor">#6 대기</text>
<rect x="130" y="80" width="300" height="26" class="f-warn" stroke="currentColor"/><text x="140" y="98" fill="currentColor">#6 선점됨 — 다른 스레드가 CPU 사용 (수 ms)</text>
<rect x="430" y="80" width="90" height="26" class="f-acc" stroke="currentColor"/><text x="436" y="98" fill="currentColor">#6 임계 구역</text>
<rect x="70" y="120" width="450" height="26" class="f-warn" stroke="currentColor"/><text x="80" y="138" fill="currentColor">#7, #8 … serving 을 읽으며 헛돈다 (CPU 를 태우며 #6 의 복귀를 늦춘다)</text>
<path d="M200 170 L430 170" stroke="currentColor" fill="none" stroke-width="2"/><text x="240" y="188" fill="currentColor">락은 비어 있는데 아무도 못 들어가는 구간</text>
<text x="10" y="214" fill="currentColor">TAS/TTAS 는 아무나 들어가므로 이 구간이 없다. 뮤텍스는 대기자가 잠들어 CPU 를 #6 에게 돌려준다.</text>
</svg>
```

<!-- code: demo_convoy.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_convoy.c -o convoy && ./convoy    # 11 초
```

실제 출력 (`sysctl -n hw.ncpu` = 12, 측정 전 load 5.7 → 후 10.2; 칸 = Mops/s, 괄호 = 획득 한 번의 최대 대기 ms):

```text
W=150 ms, reps=3; cell = Mops/s (max single-acquire wait ms)
lock\threads                    6                12                24                48
TTAS+bo(isb)        6.37 (  1.07)     4.65 ( 30.48)     2.94 ( 77.38)     0.93 (113.47)
ticket              5.22 (  2.73)     0.24 ( 30.47)     0.03 ( 61.65)     0.07 (507.19)
ticket+yield        7.05 (  2.62)     2.70 (  0.15)     0.10 (  0.59)     0.02 (  4.75)
pthread_mutex       6.89 (  0.10)     8.35 (  0.58)     8.65 (  0.69)     8.57 (  8.47)
os_unfair_lock      9.67 (  0.28)     8.67 (  5.87)     6.93 ( 24.53)     7.66 ( 54.93)
```

- **ticket: 6 → 24 스레드에서 5.22 → 0.03 Mops (약 170배 감소).** 12 스레드에서 이미 0.24 — 다른 프로세스들이 코어를 쓰고 있어(load 5–10) 12 스레드도 사실상 과다 구독이다. 48 스레드에서 한 번 잡는 데 최대 0.5 초.
- TTAS 도 줄지만(0.93) 수십 배 덜하다. 아무나 들어갈 수 있기 때문이다.
- `ticket+yield`(64 번 돌 때마다 `sched_yield()`)는 최대 대기를 0.15–4.75 ms 로 줄였지만 처리량은 여전히 낮다. macOS 의 `sched_yield` 가 "선점된 바로 그 스레드" 에게 CPU 를 준다는 보장은 없다.
- 뮤텍스는 48 스레드에서도 8.6 Mops. **결론: 사용자 공간에서 선점될 수 있는 스레드끼리는 순수 스핀락(특히 ticket)을 쓰지 않는다.** 커널은 스핀락을 잡는 동안 선점을 끄므로(`preempt_disable`) 이 문제가 없다 — 그래서 스핀락은 커널/RTOS 의 도구다.

**큐 락 (언급만).** ticket 의 또 다른 약점은 해제마다 **모든** 대기자의 `serving` 사본이 무효화되는 O(N) 트래픽이다. MCS 락(Mellor-Crummey & Scott, 1991)과 CLH 락(Craig; Magnusson·Landin·Hagersten)은 대기자마다 **자기 노드(자기 캐시 라인)** 에서 기다리게 해서 해제가 다음 사람 한 명의 라인만 건드린다. FIFO 는 유지한다. Linux 는 x86(4.2 무렵)과 arm64(4.19 무렵, 이전은 ticket)의 `spinlock_t` 를 **qspinlock** 으로 바꿨다 — 32비트 한 워드(locked 바이트 + pending 비트 + 꼬리 CPU 번호)에 넣은 MCS 변형이고, 경합이 없으면 CAS 한 번, 두 번째 대기자까지는 pending 비트로 큐 없이 처리한다 (버전·세부는 문서 기준, 미검증). 이 커리큘럼 트랙에는 MCS/CLH 전용 아이템이 없다 (트랙 JSON 검색). lock-free 쪽은 5층 DS-A01/A03/A04.

### Part C — RP2040 SIO 하드웨어 스핀락

RP2040 의 두 코어는 Cortex-M0+(ARMv6-M)다. ARMv6-M 에는 **LDREX/STREX 가 없다** (CPP-A04: M0+ 에서는 32비트 `fetch_add` 도 libcall). 그래서 칩이 SIO 블록에 32 개의 하드웨어 스핀락을 둔다. 데이터시트 기준 동작(미검증 세부는 hedge):

| 접근 | 동작 |
|---|---|
| `SPINLOCKn` 읽기 | 풀려 있었으면 **잡고** 0 이 아닌 값 반환, 이미 잡혀 있으면 0 |
| `SPINLOCKn` 쓰기 (아무 값) | 풀기 |
| `SPINLOCK_ST` 읽기 | 32 개 상태 비트맵 (디버그용) |

"읽기 한 번 = test-and-set" 이 버스 쪽에서 원자적으로 일어난다. 즉 **하드웨어 TAS** 다. 모델과 락:

```c
static uint32_t sio_spinlock_read(sio_t *sio, unsigned n) {          /* 주어진 "레지스터" */
    uint32_t bit = 1u << n;
    uint32_t old = atomic_fetch_or_explicit(&sio->state, bit, memory_order_acquire);
    return (old & bit) ? 0u : bit;
}
static void hw_spin_lock(sio_t *sio, unsigned n) {
    while (sio_spinlock_read(sio, n) == 0u) cpu_relax();
}
static void hw_spin_unlock(sio_t *sio, unsigned n) { sio_spinlock_write(sio, n); }
```

- pico-sdk 의 `spin_lock_blocking()` 은 대략 "IRQ 저장·끄기 → `while (!*lock);` → 메모리 배리어" 이고, `spin_unlock()` 은 "배리어 → `*lock = 0` → IRQ 복원" 이다 (pico-sdk `hardware_sync`, 세부 미검증). 하드웨어 레지스터는 버스 순서를 보장하지 않으므로 **보호 데이터에 대한 순서는 소프트웨어 배리어**(M0+ 의 `dmb`)가 맡는다 — 이 모델에서 acquire/release 가 하는 역할이다.
- 실리콘에는 캐시 라인 핑퐁이 없다(SIO 는 코어 옆의 단일 사이클 I/O 블록). 그래서 TAS 의 성능 문제가 여기서는 거의 없고, 2 코어뿐이라 공정성도 덜 문제다. 대신 **개수가 32 개로 한정** 되어 SDK 가 번호를 나눠 쓴다(`spin_lock_claim_unused`).
- RP2350(Cortex-M33)은 LDAEX/STLEX 가 있고, 하드웨어 스핀락에 erratum(RP2350-E2)이 있어 pico-sdk 가 기본으로 소프트웨어 스핀락을 쓴다고 알려져 있다 (`PICO_USE_SW_SPIN_LOCKS`, 미검증).
- 2 코어 로그 스트레스: 레코드 4 워드가 항상 같은 값, 코어별 순서 유지, 8000 레코드 모두 존재 → PASS.

### Part C — `spin_lock_irqsave`: 같은 코어의 ISR 과 교착하지 않기

멀티코어라도 **같은 코어의 ISR** 이 같은 락을 잡으려 하면 문제다. ISR 은 락 주인(자기가 끼어든 태스크)이 끝날 때까지 기다리는데, 주인은 ISR 이 끝나야 다시 돈다 — 영원히 스핀. 모델은 ISR 이 `ticket_trylock` 에 실패하면 "deadlock" 으로 센다:

| 순서 | 결과 (실측 CHECK) |
|---|---|
| IRQ 켠 채 `ticket_lock` → 인터럽트 도착 | `isr_runs == 1 && isr_deadlocks == 1` (주어진 잘못된 예) |
| `irq_save` → `ticket_lock` → 인터럽트 → `ticket_unlock` → `irq_restore` | 인터럽트는 펜딩, 복원 순간 ISR 실행, `deadlocks == 0`, `shared_log == 101` |
| 바깥에서 이미 IRQ 끔 → 안쪽 irqsave/irqrestore | 안쪽 restore 가 IRQ 를 **켜지 않음** (`f2 == 0`), 바깥 restore 에서 ISR 실행 |

```c
static unsigned spin_lock_irqsave(cpu_t *c) {
    unsigned f = irq_save(c);        /* cpsid i 에 해당, 이전 PRIMASK 반환 */
    (void)ticket_lock(c->lk);
    return f;
}
static void spin_unlock_irqrestore(cpu_t *c, unsigned flags) {
    ticket_unlock(c->lk);            /* 락 먼저 */
    irq_restore(c, flags);           /* 그다음 이전 상태로 */
}
```

- Linux 의 `spin_lock_irqsave(&lock, flags)` 가 정확히 이 모양이다(+ 선점 금지). ISR 과 공유하지 않는 락은 `spin_lock`, 하위 반쪽(softirq)과 공유하면 `spin_lock_bh`.
- **단일 코어 MCU** 에서는 다른 코어가 없으므로 "기다리면 누군가 풀어 준다" 가 성립하지 않는다. 그래서 단일 코어의 임계 구역은 스핀락이 아니라 **인터럽트 금지(PRIMASK/BASEPRI)** 다 (C-M14). Linux 도 `CONFIG_SMP=n` 이면 스핀락 몸통이 사라지고 선점/IRQ 금지만 남는다. RTOS 태스크끼리는 뮤텍스(우선순위 상속, RTOS-M03/M04)를 쓴다 — 스핀은 선점·우선순위 역전과 상극이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution        # 38/38 passed (6 회 반복)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o so_san && ./so_san   # 38/38, 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_tsan && ./so_tsan                                    # 38/38, 경고 0, 2.4 초
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| TTAS 에서 "0 을 읽었으니 내 것" 하고 exchange 를 안 함 | 읽기와 쓰기 사이에 다른 코어가 잡는다 → 두 명 입장 | `if (exchange(&v,1,acquire) == 0) return;` 아니면 다시 대기 |
| 기다리는 load 를 `acquire`, 잡는 exchange 를 `relaxed` 로 | 동기화는 "성공한 획득" 에 있어야 한다. relaxed exchange 뒤의 임계 구역 접근이 위로 올라갈 수 있다 | 대기 load = relaxed, exchange = acquire |
| 해제를 relaxed store 로 | 임계 구역 안의 쓰기가 해제 뒤에 보일 수 있다 (다음 주인이 옛 값을 읽음) | `atomic_store_explicit(&v, 0, memory_order_release)` |
| 백오프 상한 없이 `delay *= 2` | 넘침(overflow) 후 0 → 백오프 사라짐, 또는 너무 긴 대기 | `cur >= max/2 ? max : cur*2` |
| `cpu_relax()` 가 시간을 쓴다고 가정 | 이 맥에서 `yield` = 0.22 ns. TTAS+백오프(yield) 8.4 vs (isb) 33.8 Mops (4T) | 플랫폼의 실제 비용을 재고 명령을 고른다 (`isb`, x86 `pause`, `wfe`) |
| ticket 해제를 `fetch_add(&next, …)` 로, 또는 `serving` 대신 `next` 를 비교 | 번호표와 차례 번호가 섞임 → 영원히 대기 또는 두 명 입장 | 해제는 `serving` 만, 주인 혼자 load + release store |
| ticket trylock 을 `if (next == serving) fetch_add(&next)` 로 | 비교와 증가 사이에 다른 사람이 번호표를 뽑으면, trylock 이 "성공" 해 놓고 차례가 아님 | `CAS(&next, serving, serving+1)` 한 번 |
| 스핀락 잡은 채 `printf`/`malloc`/sleep, 또는 선점 가능한 스레드끼리 ticket | 주인이 오래 쉬거나 선점되면 모두가 CPU 를 태움 (convoy 실측 170배 감소) | 짧은 구간만, 커널은 선점 금지, 사용자 공간은 뮤텍스 |
| ISR 과 공유하는 락을 IRQ 켠 채 잡기 / irqrestore 먼저 | 같은 코어 ISR 이 그 락을 돌면 자기 교착 | `irqsave → lock`, `unlock → irqrestore` |

## 8. 면접 Q&A

<details><summary>Q1. TAS → TTAS → ticket lock 으로 가면 무엇이 좋아지나요?</summary>

TAS 는 기다리는 동안 매번 쓰기(RMW)를 해서 락 라인이 코어 사이를 계속 옮겨 다닌다. 실측 12 스레드에서 획득 한 번당 헛친 exchange 50 번, 처리량 3.1 Mops. TTAS 는 기다릴 때 읽기만 해서 각 코어의 공유 사본에서 끝나고, 풀렸을 때만 쓴다. 해제 순간의 몰림은 지수 백오프로 흩는다 — 쉬는 명령을 `isb` 로 했을 때 4 스레드 33.8 Mops, 12 스레드 11.3. ticket 은 획득당 RMW 1 번(`ldadd`)에 FIFO 공정성까지 준다: CoV 0.03, max/min 1.1 (TAS 계열 0.3~0.7, 바깥 작업 0 이면 한 스레드가 60 ms 동안 1 번만 잡는 굶주림). 대가는 처리량 — 반드시 "다음 번호" 에게 넘겨야 해서 느린 코어·선점된 스레드를 기다린다. 꼬리: "그다음은?" → 각자 자기 라인에서 기다리는 큐 락(MCS/CLH), Linux qspinlock.
</details>

<details><summary>Q2. 이 맥에서 컴파일러는 어떤 명령을 냈나요? LSE 가 더 빠르지 않나요?</summary>

Apple clang 21 기본(`-target-cpu apple-m1`)은 LSE 인라인이다: exchange → `swpa`, relaxed `fetch_add` → `ldadd`, acquire CAS → `casa`, acquire load → `ldapr`, release store → `stlr`. `-mno-outline-atomics` 나 `-march=armv8-a` 로는 안 바뀌고, `-mcpu=apple-m1+nolse` 에서 `ldaxr/stxr` 가 나왔다. 그 두 빌드로 같은 TAS 를 재면 12 스레드에서 LL/SC 19.0 vs LSE 3.1 Mops — **LSE `swpa` TAS 가 6배 느렸다** (ARM-M04 의 `swpa` 락 결과와 같은 방향). 실패 없는 RMW(`ldadd` 카운터)는 LSE 가 이기지만, "쓰며 기다리는" 루프는 아니다. 실무 답: 락은 TTAS 로 짜서 RMW 횟수 자체를 줄이면 어느 명령이든 상관이 줄어든다 (TTAS+bo(isb): LSE 11.3, LL/SC 19.9). 원인은 추정이라고 덧붙인다.
</details>

<details><summary>Q3. 백오프는 왜 지수적이고 왜 상한이 있나요? 어떤 명령으로 쉬나요?</summary>

경합 인원을 모르니 진 횟수만큼 대기를 두 배로 늘려 스스로 맞춘다 (이더넷 CSMA/CD 의 binary exponential backoff 와 같은 생각, Anderson 1990). 상한이 없으면 오래 진 스레드가 락이 비어도 잠만 자서 지연·불공정이 커지고, 정수 넘침 위험도 있다. 쉬는 명령: x86 `pause`, arm64 `yield` 가 관례지만 이 맥에서 `yield` 는 0.22 ns(사실상 nop), `isb` 는 7.8 ns 로 실측됐다. TTAS+백오프가 `yield` 로는 8.4, `isb` 로는 33.8 Mops(4 스레드). Linux arm64 는 대기 루프(`smp_cond_load_acquire`)에서 배타 load 로 모니터를 건 뒤 `wfe`(이벤트 대기)로 잠드는 방식을 쓰는 것으로 알려져 있다 — 해제 store 가 모니터를 깨워 준다 (세부 미검증, WFE 실습은 ARM-M04). 꼬리: "펌웨어에서는?" → 전력 때문에 WFE, 시간 상한(워치독) 때문에 bounded spin (추가 연습 2).
</details>

<details><summary>Q4. ticket lock 의 약점은? 코어보다 스레드가 많으면?</summary>

① 모든 대기자가 같은 `serving` 라인을 읽으므로 해제마다 N 개 사본이 무효화된다 (O(N) 트래픽). ② 차례가 정해져 있어 **다음 번호가 선점되면 락이 비어도 아무도 못 들어간다** — convoy. 실측: 6 → 24 스레드(12 코어)에서 5.22 → 0.03 Mops, 최대 대기 0.5 초. TTAS 는 0.93 Mops 로 덜 무너지고, 뮤텍스는 8.6 Mops 로 평평. 해결: 커널은 스핀락 동안 선점 금지, 사용자 공간은 잠드는 락, 트래픽은 큐 락(MCS/qspinlock) 또는 비례 백오프(내 앞 인원 × base, 추가 연습 1).
</details>

<details><summary>Q5. ticket lock 각 연산의 memory_order 를 설명해 보세요.</summary>

`fetch_add(&next, 1, relaxed)`: 번호의 유일성은 RMW 원자성(수정 순서가 하나)으로 충분하고, 동기화는 여기서 할 필요가 없다. `load(&serving, acquire)`: 이전 주인의 `store(&serving, s+1, release)` 와 synchronizes-with 를 만들어 임계 구역 데이터를 본다 (C11 5.1.2.4). 해제의 `load(&serving, relaxed)` 는 주인 자신이 마지막으로 쓴 값(또는 그 이후 값이 없음)을 읽으므로 relaxed 로 된다. trylock 의 CAS 는 성공 시 acquire, 실패 시 relaxed. 꼬리: "fetch_add 가 relaxed 면 번호 뽑기가 임계 구역보다 늦게 보일 수 있지 않나?" → 상관없다. 번호는 순서만 정하고, 입장은 `serving` acquire 로 결정된다. asm 에서도 `ldadd`(A/L 없음) + `ldapr` 로 나왔다.
</details>

<details><summary>Q6. 스핀락과 뮤텍스는 언제 각각 쓰나요?</summary>

스핀락: 임계 구역이 아주 짧고(수십~수백 ns), 기다리는 쪽이 잠들 수 없거나(ISR, 스케줄러 내부) 잠들기·깨우기 비용(µs)이 더 클 때, 그리고 주인이 선점되지 않을 때(커널은 `preempt_disable`). 뮤텍스: 그 외 전부, 특히 사용자 공간. 현대 뮤텍스는 "잠깐 스핀 → 커널 대기" 의 혼합이라 경합 없을 때는 스핀락과 같은 비용이다 (os_unfair_lock 1 스레드 35.4 Mops ≈ TAS 34.7; 내부는 OS-A01 futex, MCPP-A03 atomic wait). 꼬리: "RTOS 에서 스핀락으로 태스크끼리 동기화하면?" → 낮은 우선순위가 쥐고 높은 우선순위가 돌면 단일 코어에서는 영원히 못 끝난다 (우선순위 역전의 극단, RTOS-M03/M04).
</details>

<details><summary>Q7. 단일 코어 Cortex-M 에서 스핀락은? RP2040 은 왜 하드웨어 스핀락이 있나요?</summary>

단일 코어에서 스핀은 "다른 코어가 풀어 준다" 는 전제가 없으므로 의미가 없거나(태스크끼리) 교착이다(ISR 이 태스크가 쥔 락을 기다림). 임계 구역은 PRIMASK/BASEPRI 로 인터럽트를 막는다 (C-M14). 멀티코어 MCU 에서는 스핀락 + 같은 코어 ISR 대비 irqsave 가 필요하다. RP2040 은 Cortex-M0+(ARMv6-M) 두 개라 LDREX/STREX 가 없어서 SIO 에 32 개의 하드웨어 스핀락을 둔다 — 레지스터 읽기 = test-and-set, 쓰기 = 해제. 순서(배리어)는 여전히 소프트웨어 몫이다. 꼬리: "irqsave 의 순서?" → 끄고 잡고, 풀고 복원. 복원은 '켜기' 가 아니라 이전 값으로 (중첩 대비).
</details>

<details><summary>Q8. 직접 만든 락을 어떻게 테스트하나요?</summary>

매 실행 성립하는 것만 CHECK: ① 상호 배제 — 임계 구역 안 인원 카운터의 최댓값 == 1, ② 일반 변수 합계 정확(N×K), ③ FIFO — 도착 순서를 강제하는 스케줄(번호표가 뽑혔음을 `next` 로 확인한 뒤 다음 스레드 출발) 아래 입장 순서, ④ 단위: trylock 실패/성공, wrap-around(`0xFFFFFFFF → 0`), 백오프 수열. 처리량·공정성 분산은 실행마다 흔들리므로 데모 출력. TSan 은 atomics 의 acquire/release 로 happens-before 를 추적해 보호 데이터 레이스를 잡지만, 사용자 스핀락의 교착이나 "락 의미" 는 모른다. 시작 코드가 멈추지 않도록 대기에는 타임아웃(2 초)을 두었다. 꼬리: "TAS 의 비-FIFO 는 왜 CHECK 안 하나?" → 보장이 아니라 경향이라서 (우연히 FIFO 일 수 있다). 관찰 결과 0/20.
</details>

<details><summary>Q9. 공정성은 어떻게 숫자로 표현하나요?</summary>

같은 시간 창에서 스레드별 획득 횟수를 세어 min/max(굶주림 여부), CoV = 표준편차/평균(분산), Jain 지수 (Σx)²/(n·Σx²) (1 = 공평, 1/n = 독차지)를 본다. 실측 12 스레드: ticket Jain 0.999, TAS(LSE) 0.903, 바깥 작업 0 인 LL/SC TAS 0.433 (min 1, max 113만). 주의: P/E 코어가 섞인 기계에서 "공평한 횟수" 가 꼭 좋은 것은 아니다 — ticket 은 가장 느린 코어의 속도로 바퀴를 돌린다. 지연 관점의 공정성(최대 대기)도 같이 본다 (convoy 표의 괄호).
</details>

## 9. 추가 연습

### 연습 1 — 비례 백오프 ticket lock

대기자가 `serving` 을 쉬지 않고 읽지 않도록, **내 앞에 남은 인원 × base** 만큼 쉬었다가 다시 본다 (Mellor-Crummey & Scott 1991 이 비교한 기법). `prop_delay(my, serving, base)` 는 wrap-around 에 안전해야 한다.

<details><summary>답</summary>

`(my - serving) * base` — 부호 없는 뺄셈이라 `my = 1, serving = 0xFFFFFFFF` 도 2 명으로 계산된다. 락: `my = fetch_add(next)` 후 `for (;;) { s = load(serving, acquire); if (s == my) return; spin(prop_delay(my, s, base)); }`. 강제 스케줄 FIFO 와 4 스레드 스트레스(정확한 합계, 동시 입장 0)로 검증. 앞사람이 적을수록 자주 보므로 차례가 왔을 때 늦게 알아채는 손해가 작다.
</details>

### 연습 2 — 시간 제한 TTAS (워치독이 있는 펌웨어)

`bool ttas_lock_bounded(spin_t *, unsigned max_spins)` — 최대 `max_spins` 번만 돌고 못 잡으면 false. 실패해도 락 상태를 바꾸면 안 된다.

<details><summary>답</summary>

대기 루프와 exchange 실패 양쪽에서 `++spins >= max_spins` 이면 false. exchange 가 1 을 돌려준 경우 1 을 1 로 바꾼 것이므로 상태는 그대로다. 테스트: 남이 쥔 락에서 1000 번 후 false, `v == 1` 유지, 1 초 안에 반환. 펌웨어에서는 실패 시 에러 카운터 + 재시도 정책(또는 워치독 리셋 전에 진단 기록)을 둔다. 스핀락을 "영원히" 기다리는 코드는 하드웨어 고장(다른 코어 정지) 때 시스템 전체를 멈춘다.
</details>

### 연습 3 — 한 워드에 next·owner (Linux arm32 `arch_spinlock_t` 모양)

32비트 한 워드에 상위 16 = next, 하위 16 = owner. 잠그기는 `fetch_add(1 << 16)` 하나, trylock 은 "next == owner" 확인과 번호표 뽑기를 **CAS 한 번** 에. 해제 때 owner `0xFFFF → 0` 의 올림(carry)이 next 로 넘어가면 안 된다.

<details><summary>답</summary>

해제를 `fetch_add(1)` 로 하면 owner 가 0xFFFF 일 때 carry 가 next 를 1 올려 버린다. C11 atomic 은 반쪽만 쓸 수 없으므로 CAS 루프로 하위 16 비트만 바꾼다: `nw = (old & 0xFFFF0000) | ((old + 1) & 0xFFFF)`. (Linux arm32 는 owner 반쪽에 16비트 store 를 쓰는 것으로 알려져 있다 — 미검증.) next 의 올림은 bit 31 밖으로 버려지므로 문제없다. 테스트: `0xFFFFFFFF` 에서 lock → `0x0000FFFF`, unlock → `0x00000000`, `0xFFFDFFFD` 에서 강제 스케줄 FIFO 5 명 → owner `0x0003`. 이점: trylock 의 비교가 두 필드를 한 번에 본다. 한계: 대기자 65535 명.
</details>

### 연습 4 — 디버그 스핀락: 재진입과 남의 해제 잡기

락에 소유 코어 번호를 기록해 ① 같은 코어가 다시 잡으려 함(실제라면 영원히 스핀) → `DBG_RECURSION`, ② 주인이 아닌 코어의 해제 → `DBG_NOT_OWNER` 를 돌려준다 (Linux `CONFIG_DEBUG_SPINLOCK` 의 recursion / wrong owner 검사와 같은 생각, 세부 미검증).

<details><summary>답</summary>

`owner` 는 atomic_int(relaxed). 잡기 전에 `owner == cpu` 이면 재진입. 이 읽기가 안전한 이유: 다른 코어가 쓴 값은 내 번호일 수 없고, 내 번호는 나만 쓰고 지우므로 나는 항상 내 최신 값을 본다. 잡은 뒤 `owner = cpu`, 해제는 `owner` 확인 → `NO_OWNER` → release store. 테스트: 재진입 탐지, 남의 해제 거부(락 상태 유지), 4 스레드 스트레스.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o pr_san && ./pr_san
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pr_tsan && ./pr_tsan
```

실제 출력: `23/23 passed` (세 빌드 모두, TSan 경고 0).

## 10. 핵심 정리

- TAS = 쓰며 기다림(라인 핑퐁, 실측 12T 헛친 exchange 50/획득), TTAS = 읽으며 기다리고 풀렸을 때만 exchange, ticket = `fetch_add` 번호표 + `serving` 읽기 → FIFO.
- 기다리는 load 는 relaxed, 획득 RMW/load 는 acquire, 해제 store 는 release. ticket 의 `fetch_add` 는 relaxed, 해제는 RMW 없이 load + release store.
- 백오프는 "진 다음에", 지수적으로, 상한 있게. 쉬는 명령을 확인할 것: 이 맥에서 `yield` 0.22 ns(nop), `isb` 7.8 ns → TTAS+백오프 4T 8.4 vs 33.8 Mops.
- Apple clang 기본 = LSE 인라인 (`swpa`/`ldadd`/`casa`/`ldapr`/`stlr`). `-mno-outline-atomics`·`-march=armv8-a` 로는 안 바뀌고 `-mcpu=apple-m1+nolse` 로 LL/SC. TAS 는 LL/SC 판이 6배 빨랐다.
- 공정성: ticket CoV ≈ 0 / Jain 0.999, TAS 계열 CoV 0.3–0.7, 극단엔 한 스레드 1 회 vs 113만 회. 공정 = 가장 느린 코어 속도로 돈다.
- ticket 의 약점: 해제마다 O(N) 무효화 + 다음 번호 선점 시 convoy (6→24 스레드 170배 감소). → 큐 락(MCS/CLH/qspinlock), 커널은 선점 금지, 사용자 공간은 뮤텍스.
- 펌웨어: 단일 코어 = 인터럽트 금지(C-M14), 멀티코어 = 스핀락 + irqsave(끄고 잡고 / 풀고 복원), RP2040 = M0+ 에 LDREX 가 없어서 SIO 하드웨어 스핀락(읽기 = TAS).
- 테스트: 동시 입장 0, 정확한 합계, 강제 스케줄 FIFO 만 CHECK. 처리량·분산은 데모, load average 와 함께 비율로 읽는다.

## 참고자료

- OSTEP (Arpaci-Dusseau) 28장 *Locks* — test-and-set, compare-and-swap, LL/SC, fetch-and-add ticket lock, yield, 2단계 락: <https://pages.cs.wisc.edu/~remzi/OSTEP/threads-locks.pdf>
- T. E. Anderson, "The Performance of Spin Lock Alternatives for Shared-Memory Multiprocessors", IEEE TPDS 1(1), 1990 — TTAS, 백오프, 배열 큐 락
- J. M. Mellor-Crummey, M. L. Scott, "Algorithms for Scalable Synchronization on Shared-Memory Multiprocessors", ACM TOCS 9(1), 1991 — ticket + 비례 백오프, MCS 락: <https://www.cs.rochester.edu/u/scott/papers/1991_TOCS_synch.pdf>
- M. Herlihy, N. Shavit, *The Art of Multiprocessor Programming*, 7장 Spin Locks and Contention (TAS/TTAS/백오프/Anderson/CLH/MCS)
- C11 (N1570): 5.1.2.4 (synchronizes-with, data race), 6.2.5p9 (부호 없는 산술 wrap), 7.17.7.3 `atomic_exchange`, 7.17.7.4 compare_exchange, 7.17.7.5 `atomic_fetch_add`, 7.17.8 `atomic_flag` — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- Arm Architecture Reference Manual for A-profile (DDI 0487) — LDAXR/STXR(배타 접근), LSE `SWP`/`LDADD`/`CAS`, `LDAPR`(FEAT_LRCPC), `YIELD`/`WFE`/`ISB`: <https://developer.arm.com/documentation/ddi0487/latest/>
- Raspberry Pi, *RP2040 Datasheet* — 2.3.1.3 SIO Hardware Spinlocks (절 번호 미검증), pico-sdk `hardware_sync` (`spin_lock_blocking`, `spin_lock_claim_unused`): <https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf>
- Linux kernel: `include/linux/spinlock.h` (`spin_lock_irqsave`), `kernel/locking/qspinlock.c`, `Documentation/locking/spinlocks.rst`; LWN "MCS locks and qspinlocks" (2014): <https://lwn.net/Articles/590243/>
- Apple `os/lock.h` (`os_unfair_lock` — 공정성 보장 없음, 대기자는 커널에서 잔다)
- 관련 레슨: C-A03 (`atomic_flag` 스핀락, memory_order), ARM-M04 (LDAXR/STLXR·LSE 직접, `swpa` 락 실측, WFE), ARM-M05 (LDAR vs LDAPR), LLAT-B06 (원자 연산 비용, 먼저 읽기 ×85), C-M14 (단일 코어 = 인터럽트 금지), CPP-A04 (M0+ 에 LDREX 없음), OS-B05 (뮤텍스 처리량), OS-M08 (패턴), TOOL-M09 (TSan), RTOS-M03/M04 (우선순위 역전·상속), OS-A01 (futex 뮤텍스), MCPP-A03 (atomic wait/notify), LLAT-M05 (코어 간 핑퐁 지연), ARCH-M08 (false sharing), DS-A01/A03/A04 (lock-free)
