---
id: C-A03
time: 110분
updated: 2026-11-08
---
# C-A03 C11 Atomics: 플래그·카운터·acquire/release

## 학습 목표

- `atomic_flag` 로 스핀락(spinlock)을, `atomic_fetch_add` 로 카운터·티켓을, `atomic_compare_exchange_weak` 로 CAS 루프(최대값 워터마크)를 만들고 **각 연산에 왜 그 memory_order 인지** 한 문장으로 말할 수 있다.
- release-store / acquire-load 의 **synchronizes-with → happens-before** 관계로 "일반 데이터를 다른 코어에 안전하게 넘기는" 코드를 쓰고, **반납 방향(소비자 → 생산자)에도 release/acquire 가 필요한 이유** 를 설명한다.
- relaxed 오용을 ThreadSanitizer 로 잡아내고, 같은 버그가 실제 실행에서는 **안 보일 수도, 컴파일러가 루프 밖으로 빼서 절반을 잃을 수도, CPU 재배치로 35% 를 잃을 수도** 있다는 것을 실측으로 보인다. TSan 이 **못** 잡는 것(원자 load+store 의 lost update)도 구분한다.
- release/acquire 로 막을 수 없는 재배치(store → load, SB 리트머스)를 Apple Silicon 에서 관찰하고 `ldapr` vs `ldar` 로 설명한다.
- 듀얼 코어 MCU 의 센서 드라이버에 **트리플 버퍼(atomic_exchange acq_rel) + 스핀락 설정 + relaxed 통계** 를 묶어 넣고, Cortex-M4 에서 각 연산이 `LDREX/STREX` + `DMB` 로 어떻게 내려가는지 읽는다.

## 1. 문제

"lock-free 로 코어 간 데이터를 넘겨 보세요. 왜 그 memory_order 죠?" — 시니어 펌웨어 면접에서 C-M14(ISR ↔ main, 단일 코어) 다음 단계로 나오는 질문이다. 듀얼 코어 MCU(STM32H7 M7+M4, nRF5340, ESP32, RP2040)와 Cortex-A/R SoC 가 흔해지면서 "인터럽트를 끄면 된다" 는 답이 더는 통하지 않는다 (C-M14 Q8).

C-M14 에서 쓴 도구는 **단일 코어** 전제였다: ISR 은 main 을 선점하고 끝까지 실행되므로, main 입장에서 ISR 의 쓰기는 "통째로 일어나거나 안 일어나거나" 였다. 코어가 둘이면 두 흐름이 **진짜 동시에** 돈다. 그리고 ARMv8 (Apple Silicon 포함)·ARMv7-A/R·Cortex-M7 같은 **약한 순서(weakly ordered)** 메모리 시스템에서는 한 코어가 쓴 순서대로 다른 코어가 보지 않을 수 있다. 컴파일러도 원자 연산이 아닌 일반 접근을 마음대로 옮긴다. C11 은 이걸 한 문장으로 정리한다:

> 서로 다른 스레드의 충돌하는 두 접근(최소 하나는 쓰기) 중 **하나라도 원자 연산이 아니고**, 어느 쪽도 다른 쪽보다 **먼저 일어나지(happens before) 않으면** 데이터 레이스(data race)이고, 동작은 정의되지 않는다(UB). — C11 5.1.2.4p25 요지

그러니 "일반 데이터(구조체·버퍼)를 넘기는" 모든 코드는 어딘가에서 **happens-before** 를 만들어야 한다. 그 도구가 release / acquire 다.

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">1칸 채널: 데이터는 일반 변수, "소유권" 은 full 플래그의 release/acquire 로 넘긴다</text>
  <text x="10" y="70" fill="currentColor">생산자</text>
  <text x="10" y="86" fill="currentColor">(Core1)</text>
  <text x="10" y="200" fill="currentColor">소비자</text>
  <text x="10" y="216" fill="currentColor">(Core0)</text>
  <rect x="80" y="55" width="150" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="90" y="77" fill="currentColor" font-family="monospace">slot = msg;</text>
  <rect x="240" y="55" width="170" height="34" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="248" y="77" fill="currentColor" font-family="monospace">store(full,1,rel)</text>
  <rect x="250" y="185" width="170" height="34" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="258" y="207" fill="currentColor" font-family="monospace">load(full,acq)==1</text>
  <rect x="430" y="185" width="140" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="440" y="207" fill="currentColor" font-family="monospace">out = slot;</text>
  <rect x="580" y="185" width="150" height="34" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="586" y="207" fill="currentColor" font-family="monospace">store(full,0,rel)</text>
  <rect x="560" y="55" width="170" height="34" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="566" y="77" fill="currentColor" font-family="monospace">load(full,acq)==0</text>
  <path d="M325 89 L335 185" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M329 175 L335 185 L341 174" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="340" y="140" fill="currentColor">① synchronizes-with</text>
  <path d="M655 185 L645 89" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M639 100 L645 89 L651 99" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="490" y="125" fill="currentColor">② synchronizes-with</text>
  <text x="490" y="145" fill="currentColor">→ 이어서 다음 slot 쓰기</text>
  <g fill="currentColor">
    <text x="10" y="252">① slot 쓰기 → release 저장 →(값 1 을 읽은)acquire 읽기 → slot 읽기 : 소비자는 온전한 msg 를 본다.</text>
    <text x="10" y="274">② slot 읽기 → release 반납 → acquire 확인 → 다음 slot 쓰기 : 생산자가 읽는 중인 slot 을 덮지 않는다.</text>
    <text x="10" y="296">둘 중 하나라도 relaxed 면 그 방향의 happens-before 가 사라진다 → 데이터 레이스 (TSan 이 잡는다, 5. 풀이).</text>
  </g>
</svg>
```

호스트(맥, Apple M6, 12코어)에서는 **pthread 하나 = 코어 하나** 로 본다. Apple Silicon 은 ARMv8 이라 순서가 약하므로, 여기서 맞는 코드는 Cortex-A/R·M7 에서도 맞는 코드가 된다(그 반대는 아님 — x86 은 순서가 강해서 relaxed 버그를 숨긴다).

| 부품 | C11 | 펌웨어 예 | 이 레슨 |
|---|---|---|---|
| 상호 배제 | `atomic_flag` test_and_set / clear | 코어 간 공유 설정, SMP RTOS 커널 락 | Part A, C |
| 개수 세기 / 번호 발급 | `atomic_fetch_add` | 통계, 시퀀스 번호, 버퍼 인덱스 예약 | Part A, C |
| 조건부 갱신 | `atomic_compare_exchange_weak` | 최대 큐 깊이, 최악 지연 워터마크, 크레딧 | Part A, 연습 3 |
| 데이터 게시 | release store / acquire load | 메일박스, DMA 완료 후 프레임 넘기기 | Part B |
| 버퍼 교환 | `atomic_exchange` (acq_rel) | 트리플 버퍼(카메라·디스플레이·센서 융합) | Part C |

### Part A — 원자 부품 3종

- `spin_lock / spin_trylock / spin_unlock` — `atomic_flag` 하나로. 4 스레드 × 20만 번, 락 안에서 **일반 변수** `a += 1; b += 3;` → 최종 `a == 800000`, `b == 3a`.
- `ticket_next(t)` — 0, 1, 2, … 고유 번호. 4 스레드 × 10만 장에서 **모든 번호가 정확히 한 번**.
- `stat_max(p, v)` — `*p = max(*p, v)` 를 CAS 루프로, 갱신 직전 값을 반환. 4 스레드 × 10만 개 값의 최대값이 단일 스레드 계산과 같아야 한다.

### Part B — 데이터 게시: 양방향 release/acquire

- `chan_try_send(c, m)` / `chan_try_recv(c, out)` — 위 그림의 1칸 채널 (`msg_t` 32 B, CRC 포함). 비어 있지 않으면 send 실패, 차 있지 않으면 recv 실패 (블로킹 없음).
- 생산자 1 + 소비자 1 로 20만 개 메시지: **순서대로, 하나도 깨지지 않고** 도착.
- `demo_relaxed.c` 로 게시(send) 쪽 relaxed, 반납(recv) 쪽 relaxed, 락 relaxed 세 가지를 TSan 이 각각 잡는지 확인하고, 실제 실행 결과와 비교한다.

### Part C — 듀얼 코어 센서 드라이버: 트리플 버퍼

Core1 의 "프레임 완료 ISR"(DMA 완료 처리)이 프레임을 계속 만들고, Core0 의 main 은 **가장 최신 프레임만** 가져간다. 생산자는 절대 기다리지 않는다.

- `tb_publish(t)` — 다 채운 `back` 을 `mid` 와 교환하며 DIRTY 표시. 안 읽힌 프레임을 덮었으면 true (drop).
- `tb_latest(t)` — DIRTY 면 `front` 와 `mid` 를 교환해 새 프레임을, 아니면 NULL.
- `sensor_frame_done(d, seq)` — 스핀락으로 보호된 설정(gain, offset)을 읽어 프레임을 채우고 게시, `produced` / `dropped` 를 relaxed 로 센다.
- 스트레스: 20만 프레임 동안 main 이 64 프레임마다 gain 을 바꾼다. 받은 프레임은 전부 온전(체크섬, `offset == gain*10`), seq 는 증가만, **마지막 프레임은 반드시 받고**, `consumed + dropped == produced == 200000`.

## 2. Problem (English)

> **C11 atomics for a dual-core MCU: lock, counters, publish/consume, triple buffer.**
>
> Threads model cores. Shared plain data may only be accessed under a happens-before relation established by your atomics.
>
> 1. `spin_lock/spin_trylock/spin_unlock` on an `atomic_flag`; `uint32_t ticket_next(atomic_uint *)` (unique, gap-free); `uint32_t stat_max(atomic_uint *p, uint32_t v)` (lock-free max, returns the value seen before the update). 4 threads: 800 000 locked increments of a plain pair must be exact; 400 000 tickets each issued exactly once.
> 2. A single-slot SPSC channel `{atomic_uint full; msg_t slot;}`: `bool chan_try_send(chan_t*, const msg_t*)`, `bool chan_try_recv(chan_t*, msg_t*)`. 200 000 messages must arrive in order and intact. Choose the weakest correct memory orders and justify both directions.
> 3. A triple buffer `{frame_t buf[3]; atomic_uint mid; unsigned back, front;}` where `mid` = index | DIRTY. `bool tb_publish(tribuf_t*)` (returns true if an unread frame was overwritten), `const frame_t *tb_latest(tribuf_t*)` (NULL if nothing new). Then `void sensor_frame_done(sensor_t*, uint32_t seq)` using a spinlock-protected config and relaxed statistics. The consumer must always get the newest frame and never a torn one; `consumed + dropped == produced`.
>
> Follow-ups: Why is `memory_order_relaxed` fine for the ticket counter but not for the channel flag? What does ThreadSanitizer miss? When is release/acquire not enough (hint: Dekker/Peterson)? What changes on a Cortex-M0+ or across two cores with a D-cache?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력):

```text
FAIL starter.c:266  spin_trylock(&l)
FAIL starter.c:269  spin_trylock(&l)
FAIL starter.c:275  ticket_next(&t) == 5u && ticket_next(&t) == 6u && atomic_load(&t) == 7u
FAIL starter.c:277  stat_max(&m, 5u) == 0u && atomic_load(&m) == 5u
FAIL starter.c:278  stat_max(&m, 3u) == 5u && atomic_load(&m) == 5u
FAIL starter.c:279  stat_max(&m, 0xFFFFFFFFu) == 5u && atomic_load(&m) == 0xFFFFFFFFu
SKIP 5 stress checks (Part A): unit checks failed
FAIL starter.c:308  chan_try_send(&g_ch, &a)
FAIL starter.c:310  chan_try_recv(&g_ch, &out) && out.seq == 1u && msg_ok(&out)
FAIL starter.c:312  chan_try_send(&g_ch, &b) && chan_try_recv(&g_ch, &out) && out.seq == 2u
SKIP 2 stress checks (Part B): unit checks failed
FAIL starter.c:342  fr != NULL && fr->seq == 100u && frame_ok(fr) && fr->gain == 1u
FAIL starter.c:348  fr != NULL && fr->seq == 102u && fr->gain == 7u && frame_ok(fr)
FAIL starter.c:349  atomic_load(&g_dev.produced) == 3u && atomic_load(&g_dev.dropped) == 1u
SKIP 3 stress checks (Part C): unit checks failed
10/32 passed
```

하네스 설계: 각 Part 의 **멀티스레드 스트레스는 단일 스레드 단위 테스트가 모두 통과해야** 돈다. 그래서 시작 코드는 멈추지도(빈 `chan_try_recv` 를 기다리며 무한 루프) 레이스를 일으키지도 않는다. 스트레스 CHECK 는 "올바른 구현이면 매 실행 항상 성립" 하는 것만 본다 — 최종 카운트, 온전한 데이터, `consumed + dropped == produced`. 몇 개가 drop 되는지 같은 타이밍 의존 값은 출력만 한다.

관찰용 파일 (하네스와 별개):

```bash
cc -std=c11 -g -O1 -fsanitize=thread -pthread [-DPUB=memory_order_relaxed | -DRET=memory_order_relaxed | -DLOCK_MO=memory_order_relaxed] demo_relaxed.c -o dr && ./dr   # relaxed 오용 3종 (기본 빌드는 보고 없음)
cc -std=c11 -O2 -pthread -DN=200000u [-D...] [-DVOL] demo_relaxed.c -o dr_n && ./dr_n     # 실제 실행 스트레스
cc -std=c11 -O2 -pthread demo_lost_update.c -o dlu && ./dlu                               # load+store vs fetch_add
cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_lost_update.c -o dlu_t && ./dlu_t       # TSan 은 침묵
cc -std=c11 -O2 -pthread -DMO=0 demo_sb_litmus.c -o sb && ./sb                            # 0 relaxed, 1 acq/rel, 2 seq_cst
cc -std=c11 -O2 -S demo_asm.c -o -                                                        # arm64 명령
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_asm.c -o -          # Cortex-M4 명령
```

## 4. 힌트

<details>
<summary>힌트 1 — 스핀락: test_and_set 은 "이전 값" 을 돌려준다</summary>

`atomic_flag_test_and_set` 은 플래그를 true 로 만들고 **그 전 값** 을 돌려준다. 전 값이 false 였으면 내가 잡은 것. 잡는 쪽은 그 뒤의 접근이 위로 올라가면 안 되고(acquire), 푸는 쪽은 그 앞의 접근이 아래로 내려가면 안 된다(release). trylock 은 루프 없는 한 번.

</details>

<details>
<summary>힌트 2 — 카운터·티켓에 순서가 필요한가?</summary>

티켓 번호 자체가 "유일하고 빠짐없음" 만 보장하면 되면, 원자 RMW 는 수정 순서(modification order)의 마지막 값을 읽으므로 memory_order 와 상관없이 중복이 생기지 않는다. 다른 데이터를 이 값으로 "넘기지" 않는다면 relaxed 로 충분하다. CAS 루프에서는 실패 시 `cur` 가 **현재 값으로 갱신** 된다는 점을 이용한다.

</details>

<details>
<summary>힌트 3 — 채널: 상태 하나, 방향 둘</summary>

`full` 은 "slot 의 소유자" 다. 0 이면 생산자, 1 이면 소비자. 소유권을 넘기는 저장(0→1, 1→0)은 둘 다 release, 소유권을 확인하는 읽기는 둘 다 acquire. "데이터 먼저, 플래그 나중" 은 양쪽 모두에 적용된다 — 소비자는 slot 을 **다 읽은 뒤** 반납한다.

</details>

<details>
<summary>힌트 4 — 트리플 버퍼: 세 인덱스는 항상 {0,1,2}</summary>

`back`(생산자 전용), `mid`(공유), `front`(소비자 전용) 는 항상 서로 다른 버퍼를 가리킨다. 교환은 `atomic_exchange` 한 번이면 인덱스 순열이 유지된다. 생산자는 `back|DIRTY` 를 넣고 옛 mid 를 새 back 으로, 소비자는 `front`(DIRTY 없이)를 넣고 옛 mid 를 새 front 로. 두 교환 모두 "내가 다 쓴/읽은 버퍼를 넘기고(release), 상대가 넘긴 버퍼를 받는(acquire)" 일이다.

</details>

## 5. 풀이

### 0. C11 메모리 모델 — 이 레슨에 필요한 만큼만

| 용어 | 뜻 (C11 5.1.2.4) |
|---|---|
| 수정 순서 (modification order) | 원자 객체 **하나** 에 대한 모든 쓰기의 전체 순서. 모든 스레드가 이 순서에 동의한다. RMW 는 항상 그 순서의 **마지막 값** 을 읽는다 (7.17.3) |
| synchronizes-with | 객체 M 에 대한 release 연산 A 가 쓴 값(또는 A 로 시작하는 release sequence 의 값)을 acquire 연산 B 가 **읽으면**, A 는 B 와 동기화한다 |
| happens-before | 같은 스레드 안의 순서(sequenced-before) + synchronizes-with 를 이어 붙인 것 |
| 데이터 레이스 | 서로 다른 스레드의 충돌 접근, 하나 이상 비원자, happens-before 없음 → UB (p25) |

memory_order 별로 **막는 재배치** (7.17.3, "roach motel" 비유):

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">release 는 위쪽 접근이 아래로 못 내려가게, acquire 는 아래쪽 접근이 위로 못 올라가게</text>
  <rect x="20" y="40" width="330" height="40" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="30" y="65" fill="currentColor">앞의 읽기/쓰기 (slot = msg 등)</text>
  <rect x="20" y="100" width="330" height="34" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="30" y="122" fill="currentColor" font-family="monospace">store(flag, 1, release)</text>
  <rect x="20" y="154" width="330" height="40" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="30" y="179" fill="currentColor">뒤의 접근: 위로 올라와도 됨</text>
  <path d="M300 80 L300 98" stroke="currentColor" stroke-width="2"/>
  <text x="250" y="96" fill="currentColor" font-size="12">✕ 아래로</text>
  <rect x="390" y="40" width="330" height="40" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="400" y="65" fill="currentColor">앞의 접근: 아래로 내려가도 됨</text>
  <rect x="390" y="100" width="330" height="34" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="400" y="122" fill="currentColor" font-family="monospace">load(flag, acquire)</text>
  <rect x="390" y="154" width="330" height="40" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="400" y="179" fill="currentColor">뒤의 읽기/쓰기 (out = slot 등)</text>
  <path d="M670 154 L670 136" stroke="currentColor" stroke-width="2"/>
  <text x="620" y="150" fill="currentColor" font-size="12">✕ 위로</text>
  <text x="10" y="222" fill="currentColor">그래서 release store → (다른 변수) acquire load 는 서로 지나칠 수 있다 → SB 리트머스.</text>
  <text x="10" y="242" fill="currentColor">seq_cst 는 여기에 "모든 seq_cst 연산의 단일 전체 순서" 를 더해 store → load 도 막는다.</text>
</svg>
```

| memory_order | 순서 보장 | 쓰는 곳 |
|---|---|---|
| `relaxed` | 그 객체 하나의 원자성 + 수정 순서만. 다른 변수와의 순서 **없음** | 통계, 티켓, 워터마크 |
| `acquire` (load, RMW) | 이후 접근이 앞으로 못 옴 | 플래그 확인, 락 획득 |
| `release` (store, RMW) | 이전 접근이 뒤로 못 감 | 게시, 락 해제, 반납 |
| `acq_rel` (RMW) | 둘 다 | 교환(버퍼 넘기고 받기), 참조 카운트 |
| `seq_cst` (기본값) | 둘 다 + 전체 순서 | store→load 순서가 필요한 알고리즘(Dekker/Peterson), 헷갈릴 때 |
| `consume` | (의존성 순서) | 쓰지 말 것 — 컴파일러가 acquire 로 올려 처리 |

### Part A — 스핀락: acquire 로 잡고 release 로 푼다

```c
static void spin_lock(spinlock_t *l) {
    while (atomic_flag_test_and_set_explicit(&l->f, memory_order_acquire))
        cpu_relax();                         /* arm64 YIELD / Cortex-M __YIELD() 또는 __WFE() */
}
static void spin_unlock(spinlock_t *l) {
    atomic_flag_clear_explicit(&l->f, memory_order_release);
}
```

- **왜 acquire/release 인가:** 이전 소유자의 임계 구역 쓰기 → 그의 `clear(release)` → 내 `test_and_set(acquire)` 가 그 값(false)을 읽음 → 내 임계 구역. 이 사슬이 `g_shared.a` 같은 **일반 변수** 의 데이터 레이스를 없앤다. 테스트의 `b == 3a` 불변식도 이 덕분에 성립한다.
- **`ATOMIC_FLAG_INIT` 필수:** C11 7.17.8 — 이 매크로로 초기화하지 않은 `atomic_flag` 의 초기 상태는 불확정(indeterminate)이다. 그래서 `sensor_t g_dev = { .cfg = { SPINLOCK_INIT, 1u, 10u } }` 처럼 선언 시 초기화로 넣는다. 실행 중 "다시 풀린 상태로" 만들 때는 대입이 아니라 `atomic_flag_clear` 를 쓴다. 다른 원자 타입은 정적 저장 기간의 0 초기화가 유효한 상태이고, 자동 변수는 `atomic_init` 을 쓴다. (`ATOMIC_VAR_INIT` 은 C17 부터 폐지 예정이라 쓸 필요 없다.)
- **`atomic_flag` 만이 lock-free 가 보장된 타입** 이다 (7.17.8). 그 대신 C11 에는 읽기만 하는 연산이 없다 — TTAS(test-and-test-and-set) 처럼 "읽으며 기다리기" 를 하려면 `atomic_bool` 을 쓴다 (TAS/TTAS/ticket lock 성능 비교는 OS-M07).
- **펌웨어 경고:** 스핀락은 **다른 코어** 를 막는 도구다. 단일 코어에서 main 이 락을 잡은 순간 ISR 이 같은 락을 돌면 ISR 은 영원히 돈다(main 은 ISR 이 끝나야 진행). 그래서 Linux 는 `spin_lock_irqsave` = "내 코어 인터럽트 끄기(C-M14 Part B) + 다른 코어용 스핀락" 이다. 이 레슨의 Part C 는 생산자가 **다른 코어** 이므로 안전하다.

**relaxed 로 잡으면?** `demo_relaxed.c -DLOCK_MO=memory_order_relaxed` — 올바른 버전과 relaxed 버전의 `-O2` arm64 asm (실제 출력에서 지시어·주소 계산·`@PAGEOFF` 를 줄이고 나란히 놓음):

```text
올바른 (acquire/release)                    relaxed
LBB2_1: swpab  w9, w13, [x12]  ; 잡기+acq      ldr   x10, [x8, _counter]   ; ← 루프 전에 한 번 읽음
        tbnz   w13, #0, LBB2_1                 LBB2_1: swpb w11, w15, [x14]  ; 잡기 (순서 없음)
        ldr    x13, [x10, _counter]                    tbnz w15, #0, LBB2_1
        add    x13, x13, #1                            strb wzr, [x12, _lk]   ; 풀기 (순서 없음)
        str    x13, [x10, _counter]                    add  w9, w9, #1 ... b.ne LBB2_1
        stlrb  wzr, [x12]              ; 풀기+rel      add  x9, x10, #24, lsl #12  ; +98304
        ...                                            add  x9, x9, #1696          ; +1696 = 100000
                                                       str  x9, [x8, _counter]   ; ← 루프 끝나고 한 번 씀
```

relaxed 원자 연산은 일반 접근에 대해 **컴파일러에게도 아무 제약이 아니다.** clang 은 `counter++` 10만 번을 "루프 전에 읽고, 끝나고 +100000 해서 쓰기" 로 합쳐 임계 구역 밖으로 빼 버렸다. 두 스레드가 각각 그렇게 하니 결과는 정확히 절반:

```text
$ ./dr_n   (-O2 -DN=200000u -DLOCK_MO=memory_order_relaxed, 3회)
lock: counter=10000000 (expect 20000000)
lock: counter=10000000 (expect 20000000)
lock: counter=10000000 (expect 20000000)
```

`-DVOL` 로 카운터를 `volatile` 로 만들면 컴파일러는 `swpb / ldr / add / str / strb` 를 **프로그램 순서 그대로** 낸다 (asm 확인). 그래도:

```text
$ ./dr_n   (-O2 -DN=200000u -DVOL -DLOCK_MO=memory_order_relaxed, 3회)
lock: counter=12849076 (expect 20000000)
lock: counter=12882584 (expect 20000000)
lock: counter=13415066 (expect 20000000)
$ ./dr_n   (-O2 -DN=200000u -DVOL, 올바른 순서, 3회)
lock: counter=20000000 (expect 20000000)   (3회 모두)
```

명령 순서는 맞는데 약 35% 를 잃었다 — **CPU(ARMv8) 가** 락 바깥으로 접근을 재배치/지연 전파한 결과다. acquire 가 없는 `swpb` 뒤의 `ldr counter` 는 swap 이 끝나기 전에 수행될 수 있고, release 가 없는 `strb` 는 앞선 `str counter` 보다 먼저 다른 코어에 보일 수 있다. 같은 실험을 x86 에서 하면 TSO 라 거의 안 보일 수 있다 — 그래서 "우리 PC 에선 잘 돈다" 는 증거가 아니다.

### Part A — 카운터와 티켓: relaxed 로 충분한 이유

```c
static uint32_t ticket_next(atomic_uint *t) {
    return atomic_fetch_add_explicit(t, 1u, memory_order_relaxed);
}
```

원자 RMW 는 수정 순서의 마지막 값을 읽고 그 바로 뒤에 쓴다 (7.17.3). 그래서 어떤 memory_order 든 두 스레드가 같은 번호를 받을 수 없다. relaxed 가 포기하는 것은 **다른 변수와의 순서** 뿐이다. 통계 카운터(`produced`, `dropped`)는 "대략 지금 몇 개" 만 보여 주면 되므로 relaxed 가 맞다. 반대로 카운터 값을 보고 **다른 데이터를 읽는다면**(예: `n = load(count); use(buf[n-1])`) 그건 게시이므로 release/acquire 가 필요하다.

**원자 부품 두 개 ≠ 원자 RMW** (C-M14 의 핵심 문장 재확인) — `demo_lost_update.c`, 4 스레드 × 100만 (실제 출력):

```text
expect 4000000 : load+store=1813434  fetch_add=4000000
expect 4000000 : load+store=1571513  fetch_add=4000000
expect 4000000 : load+store=1627223  fetch_add=4000000
```

같은 파일을 TSan 으로 돌리면:

```text
$ ./dlu_t
expect 4000000 : load+store=1846158  fetch_add=4000000
$ echo $?
0
```

**보고 0건.** 모든 접근이 원자적이므로 C11 의미의 데이터 레이스가 아니기 때문이다. TSan 은 **데이터 레이스** 를 잡지, "읽고-계산하고-쓰기 사이에 끼어드는" **원자성 위반(race condition)** 은 못 잡는다. 이건 코드 리뷰와 설계(RMW 하나로 만들기)로 막아야 한다.

### Part A — CAS 루프: 최대값 워터마크

```c
static uint32_t stat_max(atomic_uint *p, uint32_t v) {
    unsigned cur = atomic_load_explicit(p, memory_order_relaxed);
    while (cur < v &&
           !atomic_compare_exchange_weak_explicit(p, &cur, v,
                                                  memory_order_relaxed, memory_order_relaxed))
        ;
    return cur;
}
```

| 포인트 | 설명 |
|---|---|
| 실패하면 `cur` 가 갱신된다 | 7.17.7.4: 비교가 실패하면 현재 값이 `*expected` 에 써진다. 루프 안에서 다시 load 할 필요 없음 |
| `cur < v` 를 매번 다시 검사 | 다른 스레드가 더 큰 값을 넣었으면 그냥 끝 — 불필요한 쓰기(캐시 라인 독점) 없음 |
| `_weak` | **가짜 실패(spurious failure)** 허용 (7.17.7.4). LL/SC 기계(ARMv7 `LDREX/STREX`)에서 루프 하나로 구현 가능해 더 싸다. 어차피 루프면 weak, 한 번만 시도할 거면 strong |
| relaxed | 값 자체만 중요 (통계). 이 값을 보고 다른 데이터를 읽는다면 success 순서를 acquire/release 로 |
| fetch_add 로는 안 되나 | "조건부" 갱신은 RMW 한 방에 안 된다. ARMv8.1 에는 `LDUMAX` 같은 원자 max 명령이 있지만 C11 에는 해당 함수가 없다 |

### Part B — 데이터 게시: "데이터 먼저, 플래그 나중" 을 양쪽에

```c
static bool chan_try_send(chan_t *c, const msg_t *m) {
    if (atomic_load_explicit(&c->full, memory_order_acquire) != 0u) return false;
    c->slot = *m;                                               /* 1) 데이터 */
    atomic_store_explicit(&c->full, 1u, memory_order_release);  /* 2) 게시 */
    return true;
}
static bool chan_try_recv(chan_t *c, msg_t *out) {
    if (atomic_load_explicit(&c->full, memory_order_acquire) != 1u) return false;
    *out = c->slot;                                             /* 1) 다 읽고 */
    atomic_store_explicit(&c->full, 0u, memory_order_release);  /* 2) 반납 */
    return true;
}
```

네 개의 순서 각각이 지키는 것:

| 연산 | 빼면(relaxed) 무엇이 깨지나 |
|---|---|
| send 의 `store(1, release)` | 소비자가 FULL 을 보고도 **덜 쓴 slot** 을 읽을 수 있다 (게시 방향 레이스) |
| recv 의 `load(acquire)` | 소비자의 slot 읽기가 FULL 확인보다 **앞당겨질** 수 있다 |
| recv 의 `store(0, release)` | 생산자가 EMPTY 를 보고 **소비자가 아직 읽는 중인 slot** 을 덮을 수 있다 (반납 방향 레이스) |
| send 의 `load(acquire)` | 생산자의 slot 쓰기가 EMPTY 확인보다 앞당겨질 수 있다 |

면접에서 흔히 놓치는 건 **반납 방향** 이다. 링 버퍼 SPSC 큐(DS-A01)의 `tail`(소비자가 갱신) 도 같은 이유로 release 다.

**TSan 이 relaxed 오용을 잡는가** — `demo_relaxed.c` (`-g -O1 -fsanitize=thread`, 실제 출력에서 스레드 생성 스택 등은 생략):

```text
# 기본 (release/acquire)
chan: messages=2000 bad=0
lock: counter=200000 (expect 200000)
(exit 0, 보고 없음)

# -DPUB=memory_order_relaxed   (게시 쪽)
WARNING: ThreadSanitizer: data race (pid=81694)
  Read of size 4 at 0x0001041d400c by main thread:
    #0 main demo_relaxed.c:80 (C-A03_dr:arm64+0x1000006e4)
  Previous write of size 4 at 0x0001041d400c by thread T1:
    #0 producer demo_relaxed.c:43 (C-A03_dr:arm64+0x100000820)
  Location is global 'slot' at 0x0001041d400c (C-A03_dr+0x10000800c)
SUMMARY: ThreadSanitizer: data race demo_relaxed.c:80 in main
... (slot.data[] 쪽 두 번째 보고, 43 → 44행)
chan: messages=2000 bad=0
ThreadSanitizer: reported 2 warnings

# -DRET=memory_order_relaxed   (반납 쪽) — 이번엔 "생산자의 다음 쓰기 vs 소비자의 이전 읽기"
WARNING: ThreadSanitizer: data race (pid=81703)
  Write of size 4 at 0x000100070028 by thread T1:
    #0 producer demo_relaxed.c:44 (C-A03_dr:arm64+0x100000838)
  Previous read of size 4 at 0x000100070028 by main thread:
    #0 main demo_relaxed.c:80 (C-A03_dr:arm64+0x1000006f8)
  Location is global 'slot' at 0x00010007000c (C-A03_dr+0x100008028)
SUMMARY: ThreadSanitizer: data race demo_relaxed.c:44 in producer
... (slot.seq 쪽 두 번째 보고, 43행)
chan: messages=2000 bad=0
ThreadSanitizer: reported 2 warnings

# -DLOCK_MO=memory_order_relaxed
WARNING: ThreadSanitizer: data race (pid=81720)
  Write of size 8 at 0x000104320000 by thread T2:
    #0 locker demo_relaxed.c:71 (C-A03_dr:arm64+0x100000904)
  Previous read of size 8 at 0x000104320000 by thread T3:
    #0 locker demo_relaxed.c (C-A03_dr:arm64+0x1000008ac)
  Location is global 'counter' at 0x000104320000 (C-A03_dr+0x100008000)
SUMMARY: ThreadSanitizer: data race demo_relaxed.c:71 in locker
lock: counter=100000 (expect 200000)
ThreadSanitizer: reported 1 warnings
```

읽는 법:

- `main demo_relaxed.c:80` 은 `consume_all()` 이 `main` 으로 인라인된 **호출 위치** 다 (`-O1`). 실제 읽기는 `msg_t m = slot;`.
- 락 보고의 "Previous read … `demo_relaxed.c`" 에 행 번호가 없는 건 그 읽기가 **루프 밖으로 끌어올려진** `ldr` 이라서다 — 앞의 asm 그대로. `counter=100000` 도 정확히 절반.
- 이 맥의 TSan 은 경고가 있으면 종료 코드가 134 였다 (SIGABRT). CI 에서는 종료 코드나 `TSAN_OPTIONS=halt_on_error=1` 로 실패 처리한다.

**그럼 실제 실행에서는?** `-O2 -DN=200000u`, 3회씩:

| 빌드 | chan bad | lock counter (기대 20000000) |
|---|---|---|
| 기본 | 0, 0, 0 | 20000000 ×3 |
| `-DPUB=relaxed` | 0, 0, 0 | — |
| `-DRET=relaxed` | 0, 0, 0 | — |
| `-DLOCK_MO=relaxed` | — | 10000000 ×3 (컴파일러가 합침) |
| `-DLOCK_MO=relaxed -DVOL` | — | 12849076 / 12882584 / 13415066 (CPU 재배치) |

채널의 relaxed 버전은 이 기계에서 60만 개 메시지 동안 **한 번도 깨지지 않았다.** arm64 에서 relaxed store 는 일반 `str` 인데, 이 패턴에서는 소비자가 FULL 을 보기까지 걸리는 시간 동안 slot 쓰기도 이미 보였을 가능성이 크다. 그래서 테스트는 통과하고 버그는 숨어 있다가, 컴파일러 버전이 바뀌거나(재배치 허용), 캐시가 있는 다른 코어(M7)로 옮기면 터진다. **TSan 은 하드웨어가 재현해 주지 않아도 C11 규칙 위반을 잡는다** — 이게 이 레슨의 TSan 실습 요점이다.

**release/acquire 로 부족한 경우 — store → load (SB 리트머스)** `demo_sb_litmus.c`: A: `x=i; r1=y`, B: `y=i; r2=x`. 순차 일관성(SC)이라면 `r1 < i && r2 < i` 는 불가능하다. 500만 회 × 8번 실행 (실제 출력 요약):

| 순서 | 8번 실행의 SC 위반 횟수 |
|---|---|
| relaxed | 91, 87, 0, 5436, 0, 137, 0, 315 |
| release store / acquire load | 0, 18, 0, 0, 0, 0, 0, 0 |
| seq_cst | 0 ×8 |

acquire/release 버전도 **한 번(18회)** 관찰됐다. 이유는 컴파일러가 낸 명령에 있다 (`-O2 -S`, `ta` 함수):

```text
-DMO=1 (release/acquire):  stlr w8, [x9]   ...  ldapr w13, [x12]
-DMO=2 (seq_cst):          stlr w8, [x9]   ...  ldar  w13, [x12]
```

ARMv8 의 `ldar` 는 앞선 `stlr` 를 앞지르지 못하지만(RCsc), ARMv8.3 의 `ldapr`(RCpc) 은 앞선 `stlr` 와 다른 주소면 앞지를 수 있다. C11 acquire 는 그것까지만 요구하므로 clang 은 더 싼 `ldapr` 을 고른다. 결론: **"내 쓰기가 보인 다음에 상대 것을 읽는다" 가 필요한 알고리즘(Dekker, Peterson, 일부 이벤트 카운트/웨이크업 로직)은 seq_cst** — 연습 4. 관찰 빈도가 낮고 실행마다 크게 다르다는 점도 중요하다: 8번 중 1번이었다. 테스트로 "안 보였다" 는 증명이 아니다.

### Part C — 트리플 버퍼

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">세 버퍼 = 세 소유자. 공유하는 건 mid(인덱스|DIRTY) 하나, 교환은 atomic_exchange 한 번</text>
  <rect x="20" y="50" width="200" height="60" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="32" y="74" fill="currentColor">back (Core1 전용)</text>
  <text x="32" y="96" fill="currentColor">프레임 채우는 중</text>
  <rect x="270" y="50" width="200" height="60" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="282" y="74" fill="currentColor">mid (공유, atomic)</text>
  <text x="282" y="96" fill="currentColor" font-family="monospace">idx | DIRTY</text>
  <rect x="520" y="50" width="200" height="60" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="532" y="74" fill="currentColor">front (Core0 전용)</text>
  <text x="532" y="96" fill="currentColor">최신 프레임 읽는 중</text>
  <path d="M120 112 C 140 160, 330 160, 360 112" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M350 120 L360 112 L362 125" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="40" y="180" fill="currentColor" font-family="monospace">tb_publish: old = xchg(mid, back|DIRTY, acq_rel)</text>
  <text x="40" y="200" fill="currentColor" font-family="monospace">            back = old&amp;3; drop = old&amp;DIRTY</text>
  <path d="M620 112 C 600 160, 420 160, 390 112" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M388 126 L390 112 L400 121" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="400" y="230" fill="currentColor" font-family="monospace">tb_latest: if (mid&amp;DIRTY)</text>
  <text x="400" y="250" fill="currentColor" font-family="monospace">  old = xchg(mid, front, acq_rel)</text>
  <text x="400" y="270" fill="currentColor" font-family="monospace">  front = old&amp;3</text>
  <text x="20" y="250" fill="currentColor">release: 내가 다 쓴/읽은 버퍼를 넘긴다</text>
  <text x="20" y="270" fill="currentColor">acquire: 상대가 넘긴 버퍼의 내용을 본다</text>
  <text x="20" y="292" fill="currentColor">생산자는 기다리지 않는다. 소비자는 늘 최신 완성 프레임. 덮인 프레임은 drop 으로 센다.</text>
</svg>
```

```c
static bool tb_publish(tribuf_t *t) {
    unsigned old = atomic_exchange_explicit(&t->mid, t->back | TB_DIRTY, memory_order_acq_rel);
    t->back = old & TB_IDX;
    return (old & TB_DIRTY) != 0u;
}
static const frame_t *tb_latest(tribuf_t *t) {
    if ((atomic_load_explicit(&t->mid, memory_order_relaxed) & TB_DIRTY) == 0u) return NULL;
    unsigned old = atomic_exchange_explicit(&t->mid, t->front, memory_order_acq_rel);
    t->front = old & TB_IDX;
    return &t->buf[t->front];
}
```

정확성 논증:

1. **인덱스 순열:** 처음 `{back, mid, front} = {0, 1, 2}`. 교환은 "내 인덱스를 넣고 mid 의 인덱스를 가져오기" 라 순열이 유지된다 (단위 테스트가 `1<<back | 1<<mid | 1<<front == 7` 확인). 따라서 생산자가 쓰는 버퍼와 소비자가 읽는 버퍼는 **절대 같지 않다.**
2. **왜 둘 다 acq_rel:** 생산자 교환의 release 는 프레임 내용을 게시하고, acquire 는 "소비자가 다 읽고 넘긴 버퍼" 를 받는다 — 이게 없으면 생산자가 다음에 그 버퍼에 쓰는 것과 소비자의 이전 읽기가 레이스다(Part B 의 반납 방향과 같은 구조). 소비자 교환도 똑같이 대칭.
3. **빠른 경로의 relaxed load:** 단지 "교환할 가치가 있나" 를 보는 힌트다. DIRTY 는 소비자만 지우므로 본 뒤 사라지지 않고, 실제 동기화는 뒤의 교환이 한다. 대부분의 폴링이 새 프레임 없이 끝나므로 RMW(캐시 라인 독점)를 아낀다.
4. **`consumed + dropped == produced`:** 프레임은 생산자 교환으로 DIRTY 와 함께 mid 에 들어가고, (a) 다음 생산자 교환이 DIRTY 인 채로 꺼내면 drop, (b) 소비자 교환이 꺼내면 consumed. 정확히 한 번씩. 마지막 프레임은 다음 생산이 없으니 소비자가 반드시 가져간다.

실제 출력 (solution, 3회 — drop 수는 타이밍에 따라 다르고, 체크는 합계만 본다):

```text
Part C stress: produced=200000 consumed=192768 dropped=7232
Part C stress: produced=200000 consumed=187805 dropped=12195
Part C stress: produced=200000 consumed=196248 dropped=3752
```

설정(`gain`, `offset = gain*10`)은 두 필드가 **함께** 바뀌어야 하므로 원자 변수 하나로는 안 된다 → 스핀락. 프레임마다 `frame_ok()` 가 `offset == gain*10` 을 확인하므로, 락이 틀리면 "gain 은 새 값, offset 은 옛 값" 인 프레임이 잡힌다. (쓰기가 드물고 읽기가 잦으면 seqlock — C-M14 Part C, LLAT-M02 — 이 더 낫다. 리더가 writer 를 막지 않는다.)

**실제 듀얼 코어 MCU 로 옮길 때** 체크리스트:

| 항목 | 내용 |
|---|---|
| 원자 명령 | Cortex-M3/M4/M7/M33 은 `LDREX/STREX` (아래 asm). **Cortex-M0/M0+ 는 없음** → RP2040 은 SIO 하드웨어 스핀락, STM32H7 은 HSEM 같은 하드웨어 세마포어 |
| 코어 간 배타 접근 | 다른 코어와의 `LDREX/STREX` 는 그 메모리 영역이 **global exclusive monitor** 를 지원해야 의미가 있다 (SOCX-B03). 칩 레퍼런스 매뉴얼 확인 |
| D-cache | M7 의 캐시는 M4 와 코히어런트하지 않다. 공유 영역을 MPU 로 non-cacheable/shareable 로 두거나 clean/invalidate (C-B09 Q6). **원자 연산은 캐시 일관성을 대신하지 않는다** |
| DMA | DMA 엔진은 C11 "스레드" 가 아니다. CPU ↔ DMA 순서는 `DMB`/`DSB` 와 캐시 유지보수 (C-A02) |
| 같은 코어의 ISR | 스핀락 금지 (교착). 인터럽트 마스킹 또는 lock-free 구조 (C-M14) |

### Cortex-M4 와 arm64 에서 실제로 나온 명령 — `demo_asm.c`

| C11 연산 | arm64 (`cc -O2 -S`) | Cortex-M4 (`--target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2`) |
|---|---|---|
| `test_and_set(acquire)` | `swpab` | `ldrexb / strexb / cmp / bne` 뒤에 `dmb sy` |
| `clear(release)` | `stlrb wzr` | `dmb sy` 다음 `strb` |
| `fetch_add(relaxed)` | `ldadd` | `ldrex / adds / strex / cmp / bne` (dmb 없음) |
| `fetch_add(seq_cst)` | `ldaddal` | `dmb sy` + 위 루프 + `dmb sy` |
| `store(release)` | `stlr` | `dmb sy; str` |
| `load(acquire)` | `ldapr` | `ldr; dmb sy` |
| `store(seq_cst)` | `stlr` | `dmb sy; str; dmb sy` |
| `load(seq_cst)` | `ldar` | `ldr; dmb sy` |
| `exchange(acq_rel)` | `swpal` | `dmb sy` + `ldrex/strex` 루프 + `dmb sy` |
| CAS weak (relaxed) | `cas` | `ldrex / cmp / strex`, 불일치면 `clrex` |

M4 출력의 실제 조각 (lock 루프는 clang 이 4번 펼쳤다 — 그중 첫 번째, unlock, publish):

```text
lock:                         unlock:                    publish:
	ldrexb	r2, [r0]              	movs	r1, #0             	str	r0, [r1]      @ payload
	strexb	r3, r1, [r0]          	dmb	sy                 	...
	cmp	r3, #0                	strb	r1, [r0]           	movs	r1, #1
	bne	.LBB0_1               	bx	lr                 	dmb	sy
	lsls	r2, r2, #31                                        	str	r1, [r0]      @ ready
	dmb	sy                                                 	bx	lr
	beq	.LBB0_9
```

읽는 법:

- **arm64 (LSE)** 는 순서를 명령 자체에 싣는다: 접미사 `a` = acquire, `l` = release, `al` = 둘 다. 별도 배리어가 없다. relaxed `ldadd` 와 seq_cst `ldaddal` 은 명령 하나 차이.
- **ARMv7-M 에는 acquire/release 명령이 없어서** (`LDA/STL` 은 ARMv8-M 부터) 컴파일러가 `DMB` 로 감싼다: release = 앞에 `dmb`, acquire = 뒤에 `dmb`. relaxed RMW 에는 `dmb` 가 없다.
- 단일 코어 Cortex-M4 구현은 일반 메모리 접근 순서를 실제로 거의 바꾸지 않아 이 `dmb` 들이 대부분 "보험" 이다 (ARM AN321 의 배리어 사용 지침). 그러나 아키텍처(ARMv7-M)는 재배치를 허용하고, M7(슈퍼스칼라, AXI, 캐시)이나 다른 버스 마스터가 끼면 실제로 필요하다. 컴파일러가 C11 규칙대로 넣는 것을 지우려 하지 말 것.
- M4 의 CAS 는 비교 실패 시 `clrex` 로 배타 모니터를 정리한다. ISR 이 사이에 끼면 예외 진입/복귀가 모니터를 지워 `strex` 가 실패하고 재시도한다 (C-M14 Q5, ARM-M11).

### 정리: 어떤 순서를 언제

| 상황 | 순서 | 이 레슨 |
|---|---|---|
| 개수·번호·워터마크 (값 자체만 중요) | relaxed | `ticket_next`, `stat_max`, `produced/dropped` |
| 일반 데이터를 넘긴다 | 쓰는 쪽 release, 받는 쪽 acquire | 채널 양방향 |
| 넘기면서 받는다 (교환, 참조 카운트 감소) | acq_rel | 트리플 버퍼, 연습 1 |
| 락 | 획득 acquire, 해제 release | 스핀락 |
| store 다음 다른 변수 load 의 순서가 필요 | seq_cst | 연습 4, SB 리트머스 |
| 모르겠다 | seq_cst 로 맞게 만든 뒤, 근거가 있을 때만 약화 + TSan | — |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution            # 32/32 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o sol_asan && ./sol_asan   # 32/32, 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o sol_tsan && ./sol_tsan              # 32/32, 보고 없음
```

실제 출력 (TSan 빌드):

```text
Part C stress: produced=200000 consumed=198378 dropped=1622
32/32 passed
```

TSan 은 ASan 과 같이 쓸 수 없어서 두 번 빌드한다. TSan 은 **실행된 경로만** 본다 — 스트레스로 각 경로를 여러 번 지나가게 한 이유다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 코어 간 플래그를 `volatile int` 로 | volatile 은 원자성도, 스레드 간 순서도 주지 않는다. C11 상 데이터 레이스 = UB (C-B09) | `atomic_uint` + release/acquire |
| 게시 플래그를 relaxed 로 ("x86 에서 잘 됨") | 데이터 쓰기가 플래그 뒤로 밀릴 수 있다. TSan 이 즉시 보고 (`-DPUB`) | `store(..., release)` / `load(..., acquire)` |
| 반납(EMPTY/tail) 쪽만 relaxed | 생산자가 소비자가 읽는 중인 버퍼를 덮는다 (`-DRET` 보고) | 반납도 release, 확인도 acquire |
| 스핀락 test_and_set/clear 를 relaxed | 컴파일러가 임계 구역을 밖으로 뺀다(절반 유실), CPU 도 재배치(35% 유실) | acquire / release |
| `n = load(c); store(c, n+1);` 로 카운터 | 각각은 원자적이어도 사이가 틈. TSan 도 못 잡음 | `atomic_fetch_add` |
| CAS 실패 후 `cur` 를 다시 load | 불필요 — 실패한 CAS 가 이미 현재 값을 넣어 줬다. 다시 load 하면 오히려 ABA 창이 넓어진다 | `while (!cas_weak(p, &cur, f(cur)))` |
| `compare_exchange_weak` 를 루프 없이 한 번 | 가짜 실패로 값이 같아도 false 가 나올 수 있다 | 루프 안에서 weak, 한 번이면 strong |
| `atomic_flag` 를 0 초기화에 맡김 | 7.17.8: `ATOMIC_FLAG_INIT` 없으면 상태 불확정 | `= ATOMIC_FLAG_INIT` (정적 초기화) |
| 같은 코어의 ISR 에서 스핀락 | main 이 잡고 있으면 ISR 이 영원히 돈다 | 인터럽트 마스킹(C-M14) 또는 lock-free 구조 |
| Peterson/Dekker 를 acquire/release 로 | store → load 재배치 허용. 실측 SB 위반 관찰 | seq_cst |
| `_Atomic` 큰 구조체로 "통째로 원자" | lock-free 가 아니면 내부 락 → ISR 교착, M0+/M4 에선 라이브러리 호출 | 인덱스·포인터만 원자로 (트리플 버퍼처럼) |
| 원자 연산이면 M7 ↔ M4 공유 SRAM 도 안전하다고 가정 | 캐시 일관성은 별개 | non-cacheable MPU 영역 또는 캐시 유지보수 |

## 8. 면접 Q&A

<details>
<summary>Q1. volatile 과 _Atomic 은 무엇이 다른가요? 둘 다 붙여야 하나요?</summary>

volatile 은 "이 접근을 지우거나 합치지 말라" 는 **컴파일러** 지시다 — MMIO 레지스터(C-M01)와 단일 코어 ISR 플래그용. 원자성(찢어짐 방지, RMW)도, 다른 변수와의 순서도, 다른 코어에 보이는 순서도 주지 않는다. `_Atomic` 은 C11 메모리 모델에 들어가 원자성 + memory_order 에 따른 순서를 준다. 스레드/코어 간 공유에는 `_Atomic` 만으로 충분하고, MMIO 레지스터에는 `_Atomic` 을 쓰면 안 된다(장치 레지스터에 `LDREX/STREX` 나 libcall 이 나갈 수 있고, 접근 폭도 보장되지 않는다). 둘 다 필요한 경우는 드물다 — 예: 시그널 핸들러와 공유하는 lock-free 원자 객체(C-M14). 꼬리: "`volatile` 카운터 `++` 는?" → ldr/add/str 이라 lost update (C-B09, `demo_lost_update.c` 의 load+store 판이 같은 결과).

</details>

<details>
<summary>Q2. 스핀락에서 test_and_set 은 acquire, clear 는 release 인 이유는? relaxed 로 하면 무슨 일이 나죠?</summary>

해제의 release 는 "임계 구역 안의 쓰기가 해제보다 먼저 보이게", 획득의 acquire 는 "임계 구역 안의 읽기가 획득보다 먼저 일어나지 않게" 한다. 그 결과 이전 소유자의 쓰기 → 다음 소유자의 읽기에 happens-before 가 생긴다. relaxed 실측: clang `-O2` 가 `counter++` 를 루프 밖으로 끌어내 정확히 절반(10000000/20000000)을 잃었고, `volatile` 로 컴파일러 이동을 막아도 Apple M6 에서 약 35% 를 잃었다(CPU 재배치). TSan 은 두 경우 모두 데이터 레이스로 보고한다. 꼬리: "seq_cst 로 하면?" → 맞지만 arm64 에선 거의 같은 비용, M4 에선 `dmb` 가 하나 더 붙는다. 꼬리 2: "TAS 루프가 버스를 두드리는 문제?" → TTAS(`atomic_bool` 로 읽으며 대기) + 백오프, 공정성은 ticket lock (OS-M07).

</details>

<details>
<summary>Q3. 통계 카운터에 relaxed fetch_add 를 써도 되나요? 언제 안 되나요?</summary>

된다. RMW 는 수정 순서의 마지막 값을 읽으므로 memory_order 와 무관하게 증가를 잃지 않는다 — 4 스레드 × 100만에서 `fetch_add=4000000`. relaxed 가 포기하는 것은 **다른 메모리와의 순서** 뿐이다. 안 되는 경우: 카운터 값이 "데이터가 준비됐다" 는 신호로 쓰일 때. 예: 생산자가 `buf[n] = x; fetch_add(&count, 1)` 하고 소비자가 `count` 를 보고 `buf[count-1]` 을 읽는다면 생산자는 release, 소비자는 acquire 가 필요하다. 꼬리: "relaxed 로 읽은 통계가 줄어들 수도 있나?" → 같은 원자 객체를 한 스레드가 연속으로 읽으면 수정 순서상 뒤로 가지 않는다(read-read coherence). 다른 카운터 두 개 사이의 관계(`produced >= consumed`)는 relaxed 로 읽으면 보장되지 않는다.

</details>

<details>
<summary>Q4. acquire/release 와 seq_cst 의 차이를 예로 설명해 보세요.</summary>

acquire/release 는 "짝을 이룬 두 스레드" 사이의 순서만 만든다. release store 뒤의 acquire load(다른 변수)는 서로 지나칠 수 있다 — Store Buffering. `demo_sb_litmus.c` 에서 `x=i; r1=y || y=i; r2=x` 를 500만 회 × 8번 돌렸더니 relaxed 는 8번 중 5번, acquire/release 도 8번 중 1번(18회) SC 위반이 나왔고 seq_cst 는 0 이었다. 명령으로 보면 acquire load 는 `ldapr`(RCpc, 앞선 `stlr` 를 앞지를 수 있음), seq_cst load 는 `ldar`. Dekker/Peterson 처럼 "내 깃발을 세운 뒤 상대 깃발을 본다" 는 알고리즘은 seq_cst 가 필요하다. 꼬리: "seq_cst 비용은?" → arm64 에선 load 가 `ldar` 로 바뀌는 정도, ARMv7 에선 store 뒤에 `dmb` 가 하나 더(M4 실측 `dmb sy; str; dmb sy`), x86 에선 seq_cst store 가 `xchg`(또는 `mov`+`mfence`)라 가장 비싸다.

</details>

<details>
<summary>Q5. compare_exchange_weak 와 strong 은 언제 각각 쓰나요? 실패하면 expected 는 어떻게 되죠?</summary>

weak 는 값이 같아도 실패할 수 있다(spurious failure, 7.17.7.4). LL/SC 기계(ARMv7 `LDREX/STREX`, RISC-V `LR/SC`)에서 `STREX` 가 인터럽트나 다른 코어 때문에 실패하는 걸 그대로 돌려주는 것이다. 어차피 재시도 루프라면 weak 가 더 싸다(M4 에서 루프 하나). 한 번만 시도하고 결과로 분기한다면 strong (내부에서 가짜 실패를 재시도). 실패하면 현재 값이 `*expected` 에 써지므로 루프에서 다시 load 하지 않는다. arm64 LSE 에서는 `cas` 한 명령이라 둘의 차이가 없다(실측 `max_cas`). 꼬리: "ABA 는?" → 값이 A→B→A 로 돌아오면 CAS 가 속는다. 포인터 스택에서 문제 (DS-A04: 세대 태그).

</details>

<details>
<summary>Q6. ThreadSanitizer 는 무엇을 잡고 무엇을 못 잡나요?</summary>

잡는 것: **실행 중 실제로 일어난** 접근 쌍 가운데, 하나 이상 비원자이고 happens-before 가 없는 충돌 = C11 데이터 레이스. memory_order 를 모델링하므로 하드웨어가 문제를 재현하지 않아도 relaxed 오용을 잡는다 (채널 relaxed 버전: 실기 60만 메시지 무결, TSan 은 즉시 보고). 못 잡는 것: ① 원자 연산만으로 된 **원자성 위반**(load+store 카운터: 60% 유실인데 보고 0건) ② 실행되지 않은 경로 ③ seq_cst 가 필요한데 acq/rel 로 쓴 것 같은 일부 순서 버그(모든 접근이 원자적이면 레이스가 아니다) ④ 교착(락 순서 역전은 별도 보고 기능이 있지만 atomic_flag 로 만든 스핀락은 TSan 이 락으로 인식하지 못한다). 또 ASan 과 같이 못 쓰고, 5~15배 느리다. 꼬리: "MCU 에서는?" → TSan 이 없으니 호스트에서 같은 코드를 pthread 로 돌리는 이 레슨의 구조가 실무 방법이다.

</details>

<details>
<summary>Q7. atomic 변수가 lock-free 가 아니면 무슨 문제가 있나요? 어떻게 확인하죠?</summary>

lock-free 가 아니면 구현이 내부 락(또는 인터럽트 끄기)으로 원자성을 흉내 낸다. 그 락을 main 이 잡은 순간 같은 코어의 ISR/시그널 핸들러가 같은 객체를 건드리면 교착이다 (C11 7.14.1.1 이 핸들러에 lock-free 원자만 허용하는 이유). 확인: `ATOMIC_INT_LOCK_FREE` 등 매크로(0 절대 아님, 1 때때로, 2 항상)를 `_Static_assert` 로 — 이 레슨 첫 줄. 개별 객체는 `atomic_is_lock_free()`. 이 맥에서 한 줄 실험: `_Atomic struct { uint32_t a[8]; }` → 0, 16바이트 구조체·`uint64_t` → 1, `ATOMIC_INT_LOCK_FREE` = 2. Cortex-M4 에서 `_Atomic uint64_t` 는 `__atomic_load` 라이브러리 호출, M0+ 는 32비트 RMW 도 라이브러리 호출이다 (C-M14 실측). 그래서 큰 데이터는 원자로 만들지 말고 **인덱스·포인터만 원자로** 바꿔 끼운다 — 트리플 버퍼가 그 예.

</details>

<details>
<summary>Q8. 이 트리플 버퍼를 STM32H7 (M7 + M4) 공유 SRAM 에 그대로 올리면 동작하나요?</summary>

C 코드는 맞지만 그것만으로는 부족하다. ① M7 D-cache: M4 가 쓴 프레임이 M7 캐시의 옛 줄에 가려질 수 있고, M7 이 쓴 것도 캐시에만 있을 수 있다. 공유 버퍼와 `mid` 를 MPU 로 non-cacheable(또는 shareable)로 두거나, 게시 전 clean / 소비 전 invalidate (캐시 라인 정렬 — C-A05). ② `mid` 의 `LDREX/STREX` 가 두 코어 사이에서 배타적이려면 그 영역이 global exclusive monitor 를 지원해야 한다 — 지원 안 되면 HSEM(하드웨어 세마포어)으로 교환을 보호. ③ 프레임을 DMA 가 직접 채운다면 DMA 완료 → CPU 게시 사이 순서는 `DSB` + 캐시 무효화 (C-A02). ④ 스핀락으로 보호한 설정을 같은 코어의 ISR 에서 읽으면 교착 — 그 경우는 인터럽트 마스킹 + 스핀락(`spin_lock_irqsave` 구조). 꼬리: "RP2040 은?" → M0+ 라 `LDREX/STREX` 자체가 없다. SIO 스핀락(32개 하드웨어 락)으로 교환을 감싼다.

</details>

<details>
<summary>Q9. 단일 코어 MCU 에서 ISR 과 main 사이라면 acquire/release 가 필요한가요?</summary>

같은 코어에서는 CPU 가 자기 접근 순서를 자기에게 지켜 주므로 하드웨어 배리어는 보통 필요 없고, **컴파일러** 재배치만 막으면 된다. C11 에서 그 도구가 `atomic_signal_fence` 다 (같은 스레드와 그 시그널 핸들러 사이의 순서만, 7.17.4). 그래도 원자 RMW 가 필요하면 `_Atomic` (M3/M4 는 `LDREX/STREX`, 예외가 모니터를 지워 재시도), 64비트 이상은 임계 구역 (C-M14). 단, 버스 마스터(DMA)나 다른 코어가 끼는 순간 다시 `DMB` 가 필요하다. 펜스 종류와 DMA 디스크립터 OWN 비트 순서 비교는 C-A02.

</details>

## 9. 추가 연습

### 연습 1 — DMA 버퍼 참조 카운트

여러 소비자(USB 송신, 로깅, 네트워크)가 같은 DMA 버퍼를 읽는다. `buf_get` / `buf_put` 을 쓰고, **마지막** put 만 true 를 돌려받아 버퍼를 풀에 반납(재사용)하게 하라. 마지막 스레드의 재사용 쓰기가 다른 스레드의 이전 읽기와 레이스가 되지 않아야 한다.

<details>
<summary>답 보기</summary>

```c
static void buf_get(dmabuf_t *b) { atomic_fetch_add_explicit(&b->ref, 1u, memory_order_relaxed); }
static bool buf_put(dmabuf_t *b) {
    if (atomic_fetch_sub_explicit(&b->ref, 1u, memory_order_release) != 1u) return false;
    atomic_thread_fence(memory_order_acquire);
    return true;
}
```

get 이 relaxed 인 이유: 새 참조는 **이미 참조를 가진** 쪽이 만들므로 버퍼가 사라질 수 없고, 넘겨줄 데이터도 없다. put 의 release 는 "내 읽기는 다 끝났다" 를 게시한다. 여러 스레드의 release fetch_sub 는 RMW 로 이어지므로 하나의 **release sequence** 를 이룬다 (5.1.2.4). 마지막 스레드의 acquire 펜스가 그 전부와 동기화해 모든 읽기 → 재사용 쓰기의 happens-before 가 생긴다. acquire 를 0 이 된 경우에만 펜스로 붙이는 건 Linux `refcount`/Boost 의 관용구이고, 그냥 `memory_order_acq_rel` 로 fetch_sub 해도 맞다 (매번 acquire 비용). 테스트: 4 스레드 × 2만 get/put, "마지막" 은 정확히 1번, 읽기 오류 0, TSan 깨끗.

</details>

### 연습 2 — 이벤트 플래그 그룹 (FreeRTOS EventGroup / Zephyr k_event 의 핵심)

여러 생산자가 각자 슬롯을 채운 뒤 자기 비트를 올린다. 소비자는 쌓인 비트를 **한 번에 가져가고** 비트에 해당하는 슬롯을 읽는다. `ev_post(ev, bits)` / `ev_take(ev)` 를 작성하라.

<details>
<summary>답 보기</summary>

`ev_post` = `atomic_fetch_or_explicit(ev, bits, memory_order_release)`, `ev_take` = `atomic_exchange_explicit(ev, 0u, memory_order_acquire)`. RMW 라 서로 다른 생산자의 비트가 섞여 사라지지 않고(`|=` 를 load/store 로 하면 lost update), 가져가기와 비우기가 한 연산이라 그 사이에 온 비트도 잃지 않는다 (C-M14 의 `atomic_exchange` 카운터와 같은 원리). 여러 생산자의 release fetch_or 는 RMW 로 이어지므로, 소비자의 acquire exchange 가 읽은 값에 포함된 모든 생산자와 동기화된다. 플래그는 C-M14 처럼 같은 비트가 **합쳐진다** — 개수가 중요하면 카운터를 쓴다. 테스트: 8 스레드가 각자 슬롯 → 비트, 소비자는 모든 비트를 볼 때까지 take, 본 비트의 슬롯 값이 전부 맞아야 한다.

</details>

### 연습 3 — 크레딧 (세마포어 try_take)

DMA 채널 디스크립터가 1000개 있다. `credit_take(c)` 는 크레딧이 있으면 하나 줄이고 true, 0 이면 **줄이지 않고** false. 4 스레드가 600번씩 시도하면 정확히 1000번만 성공해야 한다.

<details>
<summary>답 보기</summary>

`fetch_sub` 는 무조건 빼므로 0 아래로 내려갔다가(언더플로) 되돌리는 동안 다른 스레드가 잘못된 값을 본다. 조건부 감소는 CAS 루프:

```c
unsigned cur = atomic_load_explicit(c, memory_order_relaxed);
do { if (cur == 0u) return false; }
while (!atomic_compare_exchange_weak_explicit(c, &cur, cur - 1u,
                                              memory_order_acquire, memory_order_relaxed));
```

성공 순서 acquire, `credit_give` 는 release fetch_add — 크레딧과 함께 "반납된 디스크립터의 내용" 을 넘기는 구조라서다. 실패 순서는 relaxed (아무것도 얻지 못했으니). 결과: 2400번 시도 중 정확히 1000번 성공, 최종 0.

</details>

### 연습 4 — Peterson 락: seq_cst 가 필요한 곳

스레드 2개용 Peterson 락을 C11 원자로 쓰고 각 20만 번 락 안에서 일반 카운터를 증가시켜라. 왜 acquire/release 로는 안 되는지 SB 리트머스 결과와 연결하라.

<details>
<summary>답 보기</summary>

```c
atomic_store(&p_want[me], true);          /* seq_cst */
atomic_store(&p_turn, other);
while (atomic_load(&p_want[other]) && atomic_load(&p_turn) == other) cpu_relax();
```

정확성은 "내 `want` 쓰기가 상대의 `want` 읽기보다 먼저 전체 순서에 들어간다" 에 달려 있다 — store 다음 다른 변수 load 의 순서(SB). acquire/release 는 이걸 금지하지 않아 두 스레드가 모두 상대의 옛 `want=false` 를 읽고 동시에 들어갈 수 있다. `demo_sb_litmus.c` 에서 release/acquire 버전이 실제로 SB 위반을 보였다(8번 중 1번 실행에서 18회, `stlr` 뒤의 `ldapr`). 해제는 release 로 충분하다(일반 락 해제와 같은 역할). 실무에서는 Peterson 대신 하드웨어 원자 명령(스핀락)을 쓰지만, "seq_cst 가 꼭 필요한 예를 드세요" 의 모범 답이다. 테스트: 400000 정확.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice   # 10/10 passed
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o practice_t && ./practice_t   # 10/10, 보고 없음
```

## 10. 핵심 정리

- 일반 데이터를 스레드/코어 사이로 넘기면 어딘가에 **happens-before** 가 있어야 한다. 없으면 데이터 레이스 = UB (5.1.2.4p25). volatile 은 이걸 만들지 못한다.
- **release 저장 → 그 값을 읽은 acquire 로드** 가 synchronizes-with. "데이터 먼저, 플래그 나중" 을 **게시와 반납 양방향** 에 적용한다.
- 스핀락 = `test_and_set(acquire)` 루프 + `clear(release)`, `ATOMIC_FLAG_INIT` 필수. 같은 코어의 ISR 과는 쓰지 않는다 (irqsave).
- 카운터·티켓·워터마크는 relaxed RMW 로 충분 — RMW 는 증가를 잃지 않는다. 값으로 다른 데이터를 넘기면 그때부터 게시다. 조건부 갱신은 `compare_exchange_weak` 루프 (실패 시 expected 갱신, 가짜 실패).
- 교환으로 넘기고 받는 구조(트리플 버퍼, 참조 카운트)는 acq_rel. 공유하는 건 큰 데이터가 아니라 **인덱스 하나**.
- relaxed 오용은 실행 결과로는 안 보일 수 있다(채널 60만 개 무결). TSan 은 잡는다. 하지만 TSan 은 원자 load+store 의 lost update 와 seq_cst 부족은 못 잡는다.
- store → load 순서는 seq_cst 만 막는다 (SB: acq/rel 에서도 관찰, `ldapr` vs `ldar`). arm64 는 `swpa/stlr/ldapr/ldadd`, Cortex-M4 는 `LDREX/STREX` + `DMB`, M0+ 는 라이브러리 호출.

## 참고자료

- C11 (N1570): 5.1.2.4 Multi-threaded executions and data races (release sequence, synchronizes-with, happens-before, p25 데이터 레이스), 7.17.1 lock-free 매크로, 7.17.2 초기화(`atomic_init`), 7.17.3 memory_order, 7.17.4 펜스, 7.17.5 `atomic_is_lock_free`, 7.17.7 원자 연산(7.17.7.3 exchange, 7.17.7.4 compare_exchange, 7.17.7.5 fetch_*), 7.17.8 `atomic_flag` — <https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf>
- cppreference — Memory order (C): <https://en.cppreference.com/w/c/atomic/memory_order>
- Clang ThreadSanitizer 문서: <https://clang.llvm.org/docs/ThreadSanitizer.html>
- Arm Architecture Reference Manual for A-profile — LDAR/STLR, LDAPR(RCpc, FEAT_LRCPC), LSE 원자 명령(SWP/LDADD/CAS): <https://developer.arm.com/documentation/ddi0487/latest/>
- ARMv7-M Architecture Reference Manual (DDI0403) — A3.4 배타적 접근(LDREX/STREX, local/global monitor), A3.7 메모리 배리어(DMB/DSB/ISB): <https://developer.arm.com/documentation/ddi0403/latest/>
- ARM Application Note 321 — ARM Cortex-M Programming Guide to Memory Barrier Instructions: <https://developer.arm.com/documentation/dai0321/latest/>
- C/C++11 mappings to processors (Cambridge, Sewell 그룹): <https://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html>
- Paul E. McKenney, *Is Parallel Programming Hard, And, If So, What Can You Do About It?* — 메모리 순서 장: <https://mirrors.edge.kernel.org/pub/linux/kernel/people/paulmck/perfbook/perfbook.html>
- Linux kernel — `Documentation/atomic_t.txt`, `refcount_t` (release + acquire-on-zero): <https://docs.kernel.org/core-api/refcount-vs-atomic.html>
- 관련 레슨: C-M14 (ISR ↔ main, 단일 코어), C-B09 (volatile 한계), C-A02 (컴파일러 배리어·펜스·DMA 디스크립터), C-A05 (캐시 라인 정렬 DMA 버퍼), C-A11 (코드 리뷰), OS-M07 (TAS/TTAS/ticket lock 성능), OS-M08 (메시지 패싱·DCLP 스트레스), DS-A01 (lock-free SPSC 링 큐), DS-A04 (ABA), ARCH-A05 (리트머스), ARM-M05 (DMB/LDAR/STLR), ARM-M11 (Cortex-M LDREX/STREX), SOCX-B03 (global exclusive monitor), LLAT-M02 (seqlock), TOOL-M09 (TSan)
