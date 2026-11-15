---
id: CPP-A07
time: 120분
updated: 2026-11-11
---
# CPP-A07 정적 할당 액티브 오브젝트 프레임워크

## 학습 목표

- 액티브 오브젝트(active object, AO) = **자기 이벤트 큐 + 상태 머신 + run-to-completion(RTC)** 를 힙 없이 C++ 로 만든다: 고정 블록 이벤트 풀(참조 카운트), AO별 고정 큐, 멤버 함수 포인터 상태 핸들러(entry/exit), 우선순위 협조 스케줄러, 틱 기반 타임 이벤트.
- 이벤트 **수명**을 설명하고 테스트로 증명한다: publish 로 여러 AO 에 같은 블록을 나눠 줘도, 큐가 넘쳐 버려도, 구독자가 0명이어도 시나리오 끝에 풀 블록이 전부 돌아온다.
- 큐 넘침 정책을 **이벤트 종류별로** 고르고 근거를 댄다: "보장 post → fault" 와 "try_post → 버림 + 카운터".
- 이벤트 드리븐 코드의 진짜 버그 두 개 — **낡은 타임아웃(stale timeout)** 과 **늦게 읽은 핀** — 을 가상 시간 트레이스로 재현하고 고친다.
- Cortex-M4 객체/ELF 에서 RAM 을 객체별로 세고(`Show<...>`, `nm -S`), `operator new`·`malloc`·`__cxa_*`·`.init_array` 가 0 임을 실측으로 보인다. 멤버 함수 포인터 디스패치의 실제 명령 비용도.

## 1. 문제

"RTOS 없이(또는 RTOS 위에서도) 이벤트 드리븐 펌웨어 구조를 설계해 보세요. 힙은 금지입니다." — 시니어 펌웨어 면접에서 C-M07(표 기반 FSM)의 꼬리질문으로 자주 이어지는 설계 문제다. 답의 뼈대는 Miro Samek 이 QP 프레임워크로 널리 알린 **액티브 오브젝트** 패턴이다: 각 AO 는 자기 스레드(여기선 우선순위)·자기 큐·자기 상태를 갖고, 서로 **공유 메모리 대신 이벤트만** 주고받는다. 락이 없어지고, ISR 은 "이벤트를 만들어 큐에 넣기" 만 한다.

QP/C++ 는 GPL/상용 이중 라이선스라 코드는 쓰지 않는다. 아이디어만 빌려 **직접** 구현하고, QP 문서에서 확인한 동작만 비교로 인용한다(5. 풀이 Part B). 이미 다룬 것은 다시 하지 않는다: 표 FSM 과 RTC 큐의 원리(C-M07), 링 버퍼(DS-B03), 고정 크기 풀의 free list(DS-M02), 저장/복원형 임계 구역(CPP-B06), ISR 공유 변수 정책(CPP-A04), 힙 없는 콜백 2워드(CPP-M12), 고정 용량 컨테이너(CPP-M10), 정적 초기화와 `.init_array`(CPP-B09). RTOS 쪽 이웃은 이벤트 그룹(RTOS-B13)과 훅 기반 트레이싱(RTOS-A06).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">정적 AO 프레임워크 한 장 (이 레슨의 Board: 버튼 → LED, 통계)</text>
  <rect x="10" y="40" width="150" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="62" fill="currentColor">EXTI ISR (핀 엣지)</text>
  <text x="20" y="82" fill="currentColor" font-size="12">new_evt + try_post</text>
  <rect x="10" y="110" width="150" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="132" fill="currentColor">SysTick ISR</text>
  <text x="20" y="152" fill="currentColor" font-size="12">tick(): 타임 이벤트 post</text>
  <rect x="10" y="190" width="150" height="110" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="212" fill="currentColor">이벤트 풀 (8 × 16 B)</text>
  <text x="20" y="232" fill="currentColor" font-size="12">free list, ref 카운트</text>
  <text x="20" y="252" fill="currentColor" font-size="12">마지막 gc → 반납</text>
  <text x="20" y="272" fill="currentColor" font-size="12">최저 수위 n_min</text>
  <path d="M160 68 L230 68" stroke="currentColor" stroke-width="1.5"/>
  <path d="M160 138 L230 138" stroke="currentColor" stroke-width="1.5"/>
  <rect x="230" y="40" width="200" height="40" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="240" y="65" fill="currentColor">btn 큐 [4]  prio 3</text>
  <rect x="230" y="90" width="200" height="40" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="240" y="115" fill="currentColor">led 큐 [4]  prio 2</text>
  <rect x="230" y="140" width="200" height="40" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="240" y="165" fill="currentColor">log 큐 [4]  prio 1</text>
  <text x="240" y="205" fill="currentColor" font-size="12">ready 비트마스크: 0b1110</text>
  <rect x="460" y="40" width="270" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="470" y="62" fill="currentColor">run_one(): 가장 높은 ready 비트</text>
  <text x="470" y="82" fill="currentColor" font-size="12">31 - clz(ready) → get → dispatch</text>
  <text x="470" y="100" fill="currentColor" font-size="12">→ gc(e). 끝까지 처리(RTC), 선점 없음</text>
  <path d="M430 85 L460 85" stroke="currentColor" stroke-width="1.5"/>
  <rect x="460" y="130" width="270" height="110" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="470" y="152" fill="currentColor">AO = Active&lt;D, N&gt;</text>
  <text x="470" y="172" fill="currentColor" font-size="12" font-family="monospace">state_ : Status (D::*)(e)</text>
  <text x="470" y="192" fill="currentColor" font-size="12">Tran → exit(현재) → entry(새)</text>
  <text x="470" y="212" fill="currentColor" font-size="12">핸들러 안의 post 는 큐로만</text>
  <text x="470" y="232" fill="currentColor" font-size="12">publish → 구독자 전원 (zero-copy)</text>
  <path d="M595 110 L595 130" stroke="currentColor" stroke-width="1.5"/>
  <path d="M460 230 C 400 290, 300 260, 330 182" stroke="currentColor" stroke-width="1.5" fill="none"/>
  <text x="300" y="300" fill="currentColor" font-size="12">publish(PRESSED) → led, log 큐에 같은 포인터</text>
</svg>
```

### Part A — 프레임워크 코어

- `EventPool::alloc/release` — free list 를 블록 안에 침투형으로(DS-M02), ISR 에서도 부르므로 `CritSec` 안에서. 실패 카운터와 최저 수위(low watermark).
- `Active<D,N>::dispatch` — 현재 상태 핸들러(멤버 함수 포인터)를 부르고, `Tran` 이면 exit → 상태 교체 → entry. 자기 전이도 exit/entry 를 한다. entry 안의 전이는 fault.
- `Framework::publish / gc` — 구독자 비트마스크, 참조 카운트, **임시 소유** 로 구독자 0명·도중 회수 방지.
- `Framework::run_one` — 가장 높은 우선순위 ready AO 에서 1개 꺼내 RTC. 핸들러가 다른 AO 에 post 해도 그 AO 는 지금 걸음이 끝난 뒤에 돈다.

### Part B — 가장자리: 넘침, ISR, 시간

- `post_impl` — 성공이면 ref +1, ready 비트. 실패면 `post` 는 fault, `try_post` 는 버림 + `n_drop`. **아무도 안 들고 있는** 풀 이벤트는 그 자리에서 회수.
- main 이 큐에서 꺼내는 도중 ISR 이 같은 AO 에 post → 유실·ready 비트 꺼짐 없이.
- `disarm / tick` — 불변식 "`ctr_ == 0` ⇔ 무장 목록에 없음". 만료 후 disarm 은 false(타임아웃이 이미 큐에 있을 수 있음).

### Part C — 버튼(디바운스) → LED 블링크, 가상 시간으로

- `Button::settling` — 마지막 엣지 후 20 ms 조용하면 핀을 읽어 확정, 바뀌었으면 `PressEvt` publish. 낡은 타임아웃과 "타임아웃 뒤에 온 엣지" 를 둘 다 거른다.
- 주어진 LED AO(누를 때마다 3번 깜빡, 최대 9번)와 통계 AO 와 함께, 튀는 누름/뗌/두 번째 누름의 **정확한 이벤트·액션 트레이스**, EMI 버스트, xorshift 무작위 40회(타이머 만료와 겹치는 엣지·글리치 포함, ISR 순서 두 가지)를 통과.
- RAM 계산(호스트 + Cortex-M4)과 `operator new` 0회.

## 2. Problem (English)

> **Implement a heap-free active-object framework and a button→LED application on top of it.**
>
> 1. `EventPool`: fixed-size blocks, intrusive free list, `alloc()` returns `nullptr` when empty (counted), ISR-safe. `Framework::new_evt<T>(sig)` is constrained to trivially destructible `Event`-derived `T` that fits a block.
> 2. `Active<D, N>`: an AO with an `N`-slot queue and a flat state machine whose states are member functions `Status (D::*)(const Event&)`. A handler returns `Handled`, `Ignored`, or `tran(&D::target)`. On `Tran`: run exit of the current state, switch, run entry of the target (also for self-transitions). A transition from an entry action is a fault.
> 3. `Framework`: `post` (must succeed; overflow is a fault), `try_post` (may drop; counted), `subscribe/publish` (zero-copy multicast with reference counting), `gc`, `run_one()` (highest-priority ready AO, one event, run-to-completion), `arm/disarm/tick` for time events owned by AOs. Every event block must return to the pool at the end of every scenario.
> 4. `Button`: debounce with a 20 ms window restarted by every edge; publish `PRESSED`/`RELEASED` with a sequence number. Must not produce a phantom press when a timeout and an edge land in the same tick, in either ISR order.
>
> Constraints: `-std=c++20 -fno-exceptions -fno-rtti`, no heap (global `operator new` is replaced and counted), all framework objects `constinit`-able. Time is virtual (1 tick = 1 ms); ISRs are simulated with a host PRIMASK model.
>
> Follow-ups: Why a member-function pointer and what does it cost on Cortex-M? Why does `publish` take a temporary reference? Which events may be dropped? What does `disarm()` returning `false` mean? How big is the whole application in RAM and flash? How do you enter sleep without missing an event?

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, 61개 FAIL 중):

```text
FAIL starter.cpp:589  pool.n_free() == 4 && pool.n_total() == 4 && pool.block_size() == kBlock
FAIL starter.cpp:607  std::strcmp(g_log, "p:s1/A p:s1/EXIT p:s2/ENTRY ") == 0 && p.in_state(&Probe::s2)
FAIL starter.cpp:628  std::strcmp(g_log, "hi:s1/A hi:s1/EXIT hi:s2/ENTRY ") == 0
FAIL starter.cpp:664  e != nullptr && !fw.post(p, e) && g_fault.count == 1 && g_fault.last == FAULT_QUEUE_FULL
FAIL starter.cpp:692  std::strcmp(g_log, "p:s1/C [C-done] p:s1/C [C-done] ") == 0
FAIL starter.cpp:703  p.queue().size() == 1 && !fw.is_armed(p.te)
FAIL starter.cpp:827  b.stats.presses == 40 && b.stats.releases == 40 && b.btn.presses() == 40
8/69 passed
```

하네스 설계: 스텁은 "틀리지만 안전" 하다 — `alloc` 은 `nullptr`, `run_one` 은 `false` 라 시뮬레이터가 멈추지 않는다. 트레이스 비교가 실패하면 기대값과 실제값을 둘 다 찍는다. 처음부터 PASS 인 8개는 크기·`operator new` 0회 같은 구조 검사다.

관찰용 파일 (하네스와 별개, 결과는 5. 풀이에 실제 출력으로):

```bash
bash target.sh            # demo_target.cpp(= solution.cpp + constinit Board) → thumbv7em .o: nm -S, nm -u, 섹션 합계
bash target.sh sizes      # Cortex-M4 에서의 sizeof (Show<...> 에러로 값 출력)
bash target.sh asm        # run_one / dispatch_thunk asm
bash target.sh mfp        # demo_mfp.cpp: 멤버 함수 포인터 vs 함수 포인터 vs 가상 함수 호출 (M4 + arm64)
for n in 1 2 3 4; do c++ -std=c++20 -fno-exceptions -fno-rtti -fsyntax-only -DSHOW_ERROR=$n demo_errors.cpp; done
# Lima VM elinux (arm-none-eabi-g++ 14.2.1): 실제 링크 + 힙 심볼 검사 (exit 1 이면 힙 심볼 있음)
limactl shell elinux -- bash -lc 'mkdir -p /tmp/CPP-A07 && cp <lesson dir>/{solution.cpp,demo_target.cpp,link.sh} /tmp/CPP-A07 && cd /tmp/CPP-A07 && bash link.sh'
```

`demo_target.cpp` 는 `#define AO_NO_HARNESS` 후 `solution.cpp` 를 그대로 include 한다 — 호스트에서 테스트한 **바로 그 코드** 가 Cortex-M 으로 간다. 포트 층은 `__ARM_ARCH_PROFILE == 'M'` 이면 진짜 `mrs/cpsid/msr` 를, 아니면 호스트 PRIMASK 모델을 쓴다 (clang `-dM -E` 로 확인: thumbv7em = `'M'`, Apple arm64 = `'A'`).

## 4. 힌트

<details>
<summary>힌트 1 — 풀: 빈 블록 자체가 링크다</summary>

빈 블록의 첫 워드에 "다음 빈 블록" 포인터를 둔다(그래서 블록 ≥ `sizeof(void*)`, `static_assert`). alloc = 머리 떼기, release = 머리에 붙이기, 둘 다 O(1). ISR 과 main 이 같이 부르므로 `CritSec cs;` 한 줄이 먼저. 반납할 때 그 자리에 `FreeBlk{head_}` 를 placement new 로 만들면 이벤트 객체의 수명은 저장소 재사용으로 끝난다([basic.life] — 소멸자가 trivial 이라 부를 필요 없음).

</details>

<details>
<summary>힌트 2 — 디스패치: 같은 핸들러를 세 번 부른다</summary>

`(self.*state_)(e)` 가 `Tran` 이면 `next_` 에 목표가 들어 있다. 순서: 현재 상태에 `kExitEvt` → `state_ = next_` → 새 상태에 `kEntryEvt`. entry/exit 은 "신호 값이 ENTRY/EXIT 인 정적 이벤트" 라 핸들러 하나가 모든 일을 `switch` 로 받는다.

</details>

<details>
<summary>힌트 3 — publish 의 +1 / gc 의 -1</summary>

post 가 성공할 때마다 ref +1, 처리 후 gc 가 -1, 0 이 되면 풀로. publish 는 시작할 때 **자기도 하나 들고**(+1) 끝날 때 gc 한다. 구독자가 0명이면 그 gc 가 회수하고, 선점형 커널에서 높은 우선순위 구독자가 즉시 처리해 버려도 두 번째 구독자에게 보내기 전에 블록이 사라지지 않는다.

</details>

<details>
<summary>힌트 4 — run_one 의 임계 구역 범위</summary>

`get()` 과 "큐가 비었으면 ready 비트 끄기" 를 **한** 임계 구역에. 사이에 ISR 이 post 하면, 비트를 끈 뒤에 이벤트가 들어와 아무도 깨우지 않는 "잃어버린 깨움" 이 된다. 디스패치는 임계 구역 **밖** — 핸들러 길이만큼 인터럽트를 막으면 안 된다.

</details>

<details>
<summary>힌트 5 — 디바운스 타임아웃이 왔다고 믿지 마라</summary>

`disarm()` 이 false 면 타임아웃이 **이미 큐에 있다**. 그 상태에서 다시 arm 하면 큐의 옛 타임아웃은 "새 창이 끝났다" 로 오해된다 → `is_armed()` 면 무시. 반대로 타임아웃이 먼저 큐에 들어가고 그 뒤에 엣지가 왔다면? 핀은 이미 바뀌었는데 엣지 이벤트는 아직 처리 전이다. 마지막으로 **처리한** 엣지의 레벨(ISR 이 샘플한 값)과 지금 핀을 비교하라.

</details>

## 5. 풀이

### 설계 한 장 — 왜 이렇게 골랐나

| 결정 | 고른 것 | 대안과 이유 |
|---|---|---|
| 이벤트 저장 | 고정 블록 풀 + 포인터만 큐에 | 값 복사 큐는 큰 이벤트일수록 느리고, publish 가 구독자 수만큼 복사. 풀은 zero-copy 지만 **수명 관리**(ref)가 숙제 |
| 상태 표현 | 멤버 함수 포인터 `Status (D::*)(const Event&)` | 핸들러가 AO 의 private 멤버를 바로 쓴다. 비용: 2워드 + 가상 비트 검사 (아래 실측). C 스타일 함수 포인터는 1워드·`ldr; bx` 2명령 |
| 디스패치 | 비템플릿 `ActiveObject` + 정적 썽크 포인터 | 가상 함수 없음 → vtable 0 (`!is_polymorphic_v`), 스케줄러·큐 코드는 AO 종류와 무관하게 한 벌(CPP-A05 의 thin template) |
| 생성 | 전부 `constexpr` 생성자, 전역은 `constinit` | 동적 초기화 0 → `.init_array` 0, 정적 초기화 순서 문제 없음(CPP-B09). 대가는 `.data`(아래) |
| 스케줄러 | 우선순위 협조형, `31 - clz(ready)` | 스택 하나, 락 없음. 선점이 필요하면 RTOS 태스크 하나당 AO 하나 (RTOS-B13 쪽) |
| 시간 | AO 멤버인 정적 `TimeEvent` + 무장 목록 | 풀에서 할당하지 않는다 — 주기 타이머가 풀을 고갈시키지 않게 (QP 도 time event 는 풀 할당 불가라고 문서화) |
| 넘침 | 이벤트 종류별 정책 | 아래 Part B 표 |

### Part A — 풀, 디스패치, publish, 스케줄러

**풀.** `StaticPool<16, 8>` 은 `alignas(std::max_align_t) std::byte buf_[128]{}` 하나다. `init()` 이 뒤에서부터 release 해서 0번 블록이 머리에 온다. 테스트: 4블록 풀에서 5번 alloc → 4개 서로 다르고 정확히 16바이트 간격, 다섯 번째는 `nullptr` + `n_fail == 1`, 전부 반납 후 `n_min == 0` 이 남는다(수위는 리셋하지 않는다 — 현장 로그용). LIFO free list 라 마지막에 반납한 블록이 다음 alloc 으로 나온다(테스트가 확인).

**디스패치와 entry/exit.** 시험용 `Probe` 의 로그(실제 CHECK 문자열):

```text
start           p:s1/ENTRY
A  (s1→s2)      p:s1/A p:s1/EXIT p:s2/ENTRY
B  (s2→s2 자기) p:s2/B p:s2/EXIT p:s2/ENTRY      ← 외부 자기 전이: exit/entry 를 한다
C  (무시)       p:s2/C                           ← Ignored: exit/entry 없음
```

자기 전이의 exit/entry 는 Part C 에서 **기능** 이 된다: 버튼의 `settling` 은 엣지마다 `tran(&Button::settling)` 하고, exit(disarm) → entry(arm) 가 디바운스 창을 다시 연다. entry 에서 `tran` 을 돌려주면 `FAULT_TRAN_IN_ENTRY` — UML 상태 머신에서 entry 액션은 전이를 일으키지 않는다. 평면 FSM 이라 초기 전이(initial transition)도 "초기 상태의 entry" 로 단순화했다. 계층(HSM)은 C-M07 Q&A 가 다룬 대로 이 레슨 범위 밖이다.

**RTC 와 우선순위.** `lo`(prio 1)가 C 를 처리하면서 `hi`(prio 5)와 자기에게 B 를 post 한다. 로그는 `lo:s1/C [C-done] hi:s1/B lo:s1/B` — `hi` 가 더 높아도 `lo` 의 걸음이 **끝난 뒤** 에 돈다(협조형). 그다음 `post(lo, A); post(lo, C); post(hi, A)` 후 `ready == 0b100010`, 첫 `run_one` 은 늦게 온 `hi` 를 먼저 처리한다. 같은 AO 안에서는 FIFO.

**참조 카운트.** 아래 그림이 Part A4 테스트 그대로다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">PressEvt 블록 하나의 일생 (구독자 a=prio1, c=prio3) — 테스트 A4</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="55">시점</text><text x="10" y="85">ref</text><text x="10" y="115">풀 빈 칸</text>
  </g>
  <rect x="80" y="40" width="120" height="90" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="90" y="55" fill="currentColor">new_evt</text><text x="90" y="85" fill="currentColor">0</text><text x="90" y="115" fill="currentColor">3 / 4</text>
  <rect x="210" y="40" width="120" height="90" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="220" y="55" fill="currentColor">publish 시작</text><text x="220" y="85" fill="currentColor">1 (임시)</text><text x="220" y="115" fill="currentColor">3 / 4</text>
  <rect x="340" y="40" width="120" height="90" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="350" y="55" fill="currentColor">c, a 에 post</text><text x="350" y="85" fill="currentColor">3</text><text x="350" y="115" fill="currentColor">3 / 4</text>
  <rect x="470" y="40" width="120" height="90" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="480" y="55" fill="currentColor">publish 끝 gc</text><text x="480" y="85" fill="currentColor">2</text><text x="480" y="115" fill="currentColor">3 / 4</text>
  <rect x="600" y="40" width="130" height="90" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="610" y="55" fill="currentColor">c 처리, a 처리</text><text x="610" y="85" fill="currentColor">1 → 0</text><text x="610" y="115" fill="currentColor">3 → 4 / 4</text>
  <text x="10" y="165" fill="currentColor">구독자 0명: 1(임시) → gc → 0 → 즉시 반납.  임시 +1 이 없으면 publish 가 반납할 기회가 없다 (누수).</text>
  <text x="10" y="190" fill="currentColor">try_post 실패: ref 를 올리지 않았고 아무도 안 들고 있으면(ref == 0) post_impl 이 그 자리에서 반납.</text>
  <text x="10" y="215" fill="currentColor">publish 중 한 구독자 큐가 꽉 참: 임시 ref 가 있으니 반납하지 않는다 → 다른 구독자는 정상 수신.</text>
  <text x="10" y="240" fill="currentColor">정적 이벤트(pool = 0: ENTRY/EXIT, 타임 이벤트)는 gc 가 아무것도 하지 않는다.</text>
</svg>
```

`ref` 는 `mutable uint8_t` 다. 큐에는 `const Event*` 가 흐르므로(핸들러가 이벤트를 고치면 다른 구독자가 바뀐 값을 본다 — 그래서 const) 프레임워크만 카운트를 바꿀 길이 필요하다. `mutable` 은 const 객체 안에서도 그 멤버를 바꿀 수 있게 한다([dcl.stc]). 이 선택에는 **실측된 대가** 가 있다 — 아래 Cortex-M 절의 `.data.kEntryEvt`.

**스케줄러 asm.** `bash target.sh asm` 의 `run_one` (clang 21, thumbv7em, -O2) 첫 부분:

```text
	mrs	r12, primask
	cpsid i
	ldr	r4, [r0, #88]        @ ready_
	cbz	r4, .LBB14_3         @ 0 이면 idle
	clz	lr, r4               @ 가장 높은 비트 = 명령 1개
	...                      @ 큐 get (링 버퍼 인덱스, n_ 감소)
	msr	primask, r12
	ldr	r2, [r0, #20]        @ ao->dispatch_ (썽크 포인터)
	mov	r1, r6
	blx	r2                   @ 간접 호출 1번 → Active<D,N>::dispatch_thunk
```

인터럽트를 막는 구간은 `cpsid i` 부터 `msr primask` 까지 큐가 비지 않는 경로에서 22명령, 비는 경로(ready 비트 끄기)에서 약 26명령이었다(실측 asm 에서 셈). 이 숫자가 이 프레임워크가 인터럽트 지연에 더하는 몫이다. 디스패치는 그 밖이다.

### Part B — 넘침 정책, ISR, 시간

**넘침: 이벤트 종류마다 다르게.**

| 생산자 → 이벤트 | 정책 | 근거 |
|---|---|---|
| EXTI ISR → `EDGE` | `try_post`: 버림 + `n_drop` | 엣지는 **레벨 정보의 사본** 이다. 디바운스가 끝에 핀을 다시 읽으므로 몇 개 잃어도 결과가 같다. ISR 에서 assert/리셋하면 EMI 버스트 하나로 기기가 재부팅된다 |
| SysTick → 타임 이벤트 | `post`: 실패 = fault | 잃으면 LED 가 영원히 켜져 있거나 디바운스가 끝나지 않는다. 큐 크기로 **보장** 한다: 타임 이벤트 1개 + 외부 이벤트 최대치 |
| 버튼 → `PRESSED/RELEASED` (publish) | `post`: 실패 = fault | 사용자 입력 유실은 기능 버그. 구독자 큐는 설계에서 계산한다 |
| 테스트 하네스의 `on_fault` | 세고 계속 | 칩에서는 로그(리셋 원인 영역) 후 BKPT/리셋. 호스트에서는 테스트가 확인해야 하니 멈추지 않는다 |

CPP-M10 이 정한 원칙의 연장이다: **런타임에 일어날 수 있는 일**(버스트)은 정책으로 처리하고 세며, **설계가 막았어야 할 일**(보장 큐 넘침)은 fault 로 보낸다. 실패한 post 는 `bool` 을 돌려주고, 그 이벤트는 이미 회수됐을 수 있으니 호출자는 더 만지지 않는다.

QP 문서와 비교 (state-machine.com QP/C++ 레퍼런스에서 확인한 것만): `post_()` 는 margin 을 `QF::NO_MARGIN` 으로 `postx_()` 에 넘기고, NO_MARGIN 은 "posting fails 시 assertion" 이다. margin 을 준 `postx_()` 는 "post 후 남아야 할 빈 칸 수" 를 요구하고 실패면 `false` 를 돌려준다. 이 레슨의 `post`/`try_post` 는 그 두 모드를 margin 없이 단순화한 것이다. publish 는 "reference-counting inside the published event (zero-copy event multicasting)". 실패한 post 의 이벤트를 QP 가 바로 재활용하는지는 그 페이지에 없었다 — **미확인**.

**ISR 이 main 의 `get()` 도중에 들어오면.** 주어진 큐의 `get()` 은 `n_` 을 지역 변수로 읽고 `isr_window()` 뒤에 `n - 1` 을 쓴다 — 컴파일된 `ldrh n; ...; strh n-1` 사이에 인터럽트가 오는 상황이다. 테스트 B3 는 거기서 같은 AO 에 post 하는 ISR 을 장전한다. 정답(`get` + ready 정리를 한 `CritSec` 안에): ISR 은 pending 으로 밀렸다가 복원 순간 실행되고, 큐에 1개·ready 비트 켜짐·두 이벤트 모두 처리. `run_one` 에서 `CritSec` 을 뺀 변이(아래 버그 주입 표)는 ISR 의 `++n_` 이 main 의 `n_ = n - 1` 에 덮여 **이벤트가 큐 안에 있는데 개수는 0** 이 됐다(3개 FAIL).

**타임 이벤트.** 불변식 하나로 코드가 짧아진다: `ctr_ == 0` ⇔ 무장 목록에 없음. `arm` 은 목록에 없을 때만 넣고(재무장은 카운터만 갱신), `tick` 은 0 이 된 한 번짜리를 목록에서 뺀다. 테스트: 3틱째 정확히 1개, 주기 (2, 5) 는 17틱 동안 2·7·12·17 의 4번, 만료 전 disarm = true, 만료 후 disarm = **false + 타임아웃은 이미 큐에**, 같은 이벤트를 두 번 arm 해도 목록에 한 번. 한 번짜리를 만료 후 목록에서 빼지 않는 변이는 다음 `arm` 이 같은 노드를 다시 넣어 **목록이 고리가 되고 `tick()` 이 끝나지 않았다** (실측: 테스트가 멈춤 — 120초 타임아웃으로 종료). 침투형 목록의 불변식이 깨지면 증상은 "assert" 가 아니라 "멈춤" 이다.

QP 문서의 `QTimeEvt::disarm()` 설명이 같은 계약이다: 반환 false 는 "not running" 이고, 한 번짜리가 만료돼 스스로 해제된 경우 "the time event has already been posted and should be expected in the active object's event queue".

### Part C — 버튼 → LED, 가상 시간 트레이스

시뮬레이터는 1 ms 마다 ① 핀 변화 → EXTI ISR ② SysTick ISR ③ main 루프(idle 까지 `run_one`) 를 돈다. 트레이스는 QS 같은 이진 기록(`{t, kind, a, b}` 8바이트)이고 문자열은 호스트에서만 만든다. 같은 시각의 기록을 한 줄로 찍은 **C1 의 기대 트레이스 전체**(CHECK 에 들어 있는 그대로):

```text
10: btn<EDGE                    ← 누름, 튐 3번 (10, 11, 13)
11: btn<EDGE
13: btn<EDGE
33: btn<DEB_TO pub:PRESSED led<PRESSED LED=1 log<PRESSED     ← 13 + 20. btn(3) → led(2) → log(1) 순
133: led<BLINK_TO LED=0
200: btn<EDGE                   ← 뗌, 튐
201: btn<EDGE
202: btn<EDGE
222: btn<DEB_TO pub:RELEASED log<RELEASED                    ← led 는 RELEASED 를 구독하지 않는다
233: led<BLINK_TO LED=1
300: btn<EDGE
320: btn<DEB_TO pub:PRESSED led<PRESSED log<PRESSED          ← 깜빡이는 도중: 남은 횟수 +3, 전이 없음
333: led<BLINK_TO LED=0
400: btn<EDGE
420: btn<DEB_TO pub:RELEASED log<RELEASED
433: led<BLINK_TO LED=1
  ... 533 / 633 / 733 / 833 / 933 / 1033 / 1133 (LED 1·0 교대)
1233: led<BLINK_TO              ← 남은 0 → off. off 의 entry set(0) 은 이미 0 이라 기록 없음
```

LED 켜짐 6번(첫 누름 3 + 두 번째 누름 3), 통계 `last_hold_ms == 100`, `max_hold_ms == 189` (33 → 222), 풀 8/8, fault 0. `on` 상태의 exit 이 LED 를 끄므로 어느 경로로 `on` 을 떠나도 LED 가 켜진 채 남지 않는다 — entry/exit 의 존재 이유.

**함정 ① 낡은 타임아웃 (C2).** 핀 `5:1, 11:0, 31:1, 32:0` — 11 ms 에 연 창(만료 31)과 같은 31 ms 에 엣지가 온다. EXTI 가 먼저면 큐는 `[EDGE, DEB_TO]`. EDGE 의 자기 전이가 disarm(false: 이미 만료) → arm(51) 하고, 이어 옛 `DEB_TO` 가 도착한다. 정답 트레이스는 `31: btn<EDGE btn<DEB_TO(ign)` → `52: btn<DEB_TO` (변화 없음, 누름 0). `is_armed` 검사를 지운 변이의 **실제 출력**:

```text
31: btn<EDGE btn<DEB_TO pub:PRESSED led<PRESSED LED=1 log<PRESSED
32: btn<EDGE
52: btn<DEB_TO pub:RELEASED log<RELEASED
131: led<BLINK_TO LED=0
```

1 ms 짜리 글리치가 디바운스 없이 "누름" 으로 확정됐다(유령 누름). 옛 타임아웃이 "새 창이 끝났다" 로 해석됐기 때문이다.

**함정 ② 타임아웃 뒤에 온 엣지 (C2b).** 같은 핀, 이번엔 SysTick 이 먼저라 큐는 `[DEB_TO, EDGE]`. 타임아웃은 진짜다(무장 해제 상태). 하지만 핸들러가 도는 시점의 핀은 **이미 31 ms 엣지로 바뀌어** 있다. 첫 판 정답(검사 ①만)에 이 순서를 넣은 실제 출력:

```text
31: btn<DEB_TO pub:PRESSED btn<EDGE led<PRESSED LED=1 log<PRESSED
32: btn<EDGE
52: btn<DEB_TO pub:RELEASED log<RELEASED
```

검사 ①로는 못 막는다. 이 레슨을 쓰다가 시뮬레이터 순서를 바꿔 보고 발견한 두 번째 버그다. 고친 방법: EXTI ISR 이 엣지 이벤트에 **그 순간의 레벨** 을 싣고, 버튼은 마지막으로 처리한 엣지 레벨 `last_edge_` 를 기억한다. 타임아웃에서 `*pin_ != last_edge_` 면 "아직 처리 안 된(또는 버려진) 엣지가 있다" 는 뜻이니 창을 다시 연다. 고친 뒤 트레이스: `31: btn<DEB_TO btn<EDGE` → `52: btn<DEB_TO`. 버려진 엣지에도 같은 규칙이 맞다 — C3(같은 ms 에 엣지 13개, 큐 4칸 → 9개 버림)에서 받은 마지막 엣지는 0 인데 핀은 1 이라 창이 한 번 더 열려 누름이 50 + 20 + 20 = 90 ms 에 확정된다(테스트가 `press_t_ == 90` 확인). 잃은 엣지 때문에 **늦어질** 수는 있어도 **틀리지는** 않는다.

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">31 ms: 타이머 만료와 엣지가 같은 틱 — 큐 순서 두 가지</text>
  <path d="M40 60 L700 60" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="40" y="52">5</text><text x="120" y="52">11 (창 열림, 만료 31)</text><text x="400" y="52">31</text><text x="470" y="52">32</text><text x="640" y="52">52</text>
  </g>
  <rect x="40" y="75" width="320" height="60" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="50" y="97" fill="currentColor">EXTI 먼저: [EDGE, DEB_TO]</text>
  <text x="50" y="120" fill="currentColor" font-size="12">EDGE 재무장 → 옛 DEB_TO 는 ① is_armed 로 무시</text>
  <rect x="380" y="75" width="350" height="60" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="390" y="97" fill="currentColor">SysTick 먼저: [DEB_TO, EDGE]</text>
  <text x="390" y="120" fill="currentColor" font-size="12">DEB_TO 진짜, 핀은 이미 바뀜 → ② 핀 ≠ last_edge_</text>
  <rect x="40" y="150" width="320" height="60" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="50" y="172" fill="currentColor">검사 없음: 31 에 PRESSED, 52 에 RELEASED</text>
  <text x="50" y="195" fill="currentColor" font-size="12">= 유령 누름 (실측 트레이스)</text>
  <rect x="380" y="150" width="350" height="60" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="390" y="172" fill="currentColor">①+②: 창 재시작 → 52 에 확정, 변화 없음</text>
  <text x="390" y="195" fill="currentColor" font-size="12">두 순서 모두 누름 0 (C2, C2b)</text>
</svg>
```

**무작위 (C4).** xorshift32(seed `0xA07`) 로 40번의 누름/뗌을 만든다: 튐 1~9번(홀수 → 레벨이 바뀜), 간격 1~4 ms, 1/8 확률로 간격을 **정확히 20 ms**(타이머 만료와 같은 틱), 사이클의 절반에는 C2 와 같은 4-엣지 글리치. 실제 출력:

```text
   C4(exti first): 444 edges (34 at timer expiry, 17 glitches), pool min free 7/8, queue max btn 2 led 1 log 1
   C4(tick first): 444 edges (34 at timer expiry, 17 glitches), pool min free 7/8, queue max btn 2 led 1 log 1
```

두 순서 모두 누름 40 / 뗌 40 / LED 120 / 풀 8/8 / fault 0 / 버림 0. 고백: 처음 판(간격 1~4 ms 만, 글리치 없음)은 검사 ①이나 ②를 지운 변이도 **통과** 시켰다 — 레벨이 바뀌는 튐에서는 이른 확정도 방향이 맞아 개수가 같기 때문이다. 글리치를 넣은 지금은 두 변이 모두 C4 에서 FAIL 한다. 무작위 테스트는 무엇을 **세는지** 가 전부다.

### Cortex-M4 실측 — RAM, 섹션, 힙

`bash target.sh sizes` (clang 21, thumbv7em-none-eabihf, arm-none-eabi 14.2.1 헤더) 와 호스트 하네스 출력:

| 객체 | Cortex-M4 [B] | 호스트 arm64 [B] | 구성 (M4) |
|---|---|---|---|
| `Event` | 4 | 4 | sig 2 + pool 1 + ref 1 |
| `PressEvt` / 풀 블록 | 12 / 16 | 12 / 16 | |
| `TimeEvent` | 20 | 32 | Event 4 + target·next 8 + ctr·interval 8 |
| `Button::State` (멤버 함수 포인터) | **8** | 16 | {ptr, adj} 2워드 |
| `EventQueue` | 16 | 24 | buf 포인터 + 5 × u16 |
| `ActiveObject` | 32 | 56 | prio + 큐 + 썽크 2 + drop |
| `Button` / `Led` / `Stats` | 96 / 96 / 80 | 176 / 176 / 136 | + state_·next_ 16 + 큐 칸 4 × 4 + 자기 멤버 |
| `Framework` | 100 | 152 | pool 4 + aos_[9] 36 + subs_[12] 48 + ready·armed·count 12 |
| `StaticPool<16, 8>` | 144 | 152 | 128 + 관리 16 |
| **`Board` 전체** | **528** | 800 | |

AO 하나당 상태 포인터 2개(`state_`, `next_`)가 16바이트다. 일반 함수 포인터였다면 8바이트 — AO 3개에서 24바이트 차이. 이게 멤버 함수 포인터의 RAM 값이다.

`bash target.sh` (객체 파일) 실제 출력:

```text
== 정적 객체 크기 (llvm-nm -C -S --size-sort, 데이터만)
00000000 00000004 b g_now
00000004 00000004 b g_fault (.0)
00000000 00000210 D g_board
== 외부 참조 (llvm-nm -u)
  (끝)
== 섹션 종류별 합계 (llvm-size -A)
  .ARM.exidx     176
  .bss             8
  .data          528
  .data.kEntryEvt     4
  .data.kExitEvt     4
  .text         2694
  .init_array 섹션 수: 0
```

VM 에서 실제 링크 (`bash link.sh`, arm-none-eabi-g++ 14.2.1, `-O2 -ffunction-sections -fdata-sections -Wl,--gc-sections`, exit 0):

```text
   text	   data	    bss	    dec	    hex	filename
   2052	    536	     12	   2600	    a28	ao.elf
00008228 00000258 W Button::settling(Event const&)
00018808 00000210 D g_board
0000861c 000001e0 T app_main()
...
== 힙/런타임 심볼 검사
  없음 (operator new / malloc / free / __cxa_* / _sbrk)
```

읽는 법:

1. **외부 참조 0개, `.init_array` 0개.** placement new 는 `<new>` 의 인라인 함수라 심볼이 없다. 동적 초기화가 없어서 `__cxa_guard_*`/`__aeabi_atexit` 도 없다(그 표는 CPP-A03). `constinit Board g_board;` 가 컴파일된다는 것 자체가 증명이고, 생성자에서 레지스터를 읽는 AO 를 `constinit` 으로 두면 컴파일 에러다(아래 demo_errors 1).
2. **`g_board` 는 `.bss` 가 아니라 `.data`(D) 528바이트.** 상수 초기화지만 값이 0 이 아니다 — 우선순위, 자기 큐 버퍼를 가리키는 포인터, 상태 핸들러 주소. 그래서 RAM 528 + **플래시에 초기값 이미지 528** (startup 이 복사). 전부 0 으로 두고 `start()` 에서 채우면 `.bss` 로 가지만 "main 전에 이미 유효" 라는 보장과 맞바꾼다. 8 KB RAM MCU 에서는 이 판단이 실제로 갈린다.
3. **`kEntryEvt`/`kExitEvt` 가 `.rodata` 가 아니라 `.data`.** `Event::ref` 가 `mutable` 이라 `constexpr` 객체라도 컴파일러가 쓰기 가능한 곳에 둔다. 확인용 미니 파일에서 `mutable` 판은 `.data.kM`, 아닌 판은 `.rodata.kC` 였다(실측). 대안: `mutable` 을 빼고 프레임워크가 풀 이벤트에만 `const_cast` 로 쓴다 — 풀 블록은 원래 const 객체가 아니므로 정의된 동작이다([dcl.type.cv]: UB 는 **const 객체** 를 고칠 때). 그러면 정적 이벤트는 플래시로 가고, 실수로 정적 이벤트의 ref 를 건드리면 플래시 쓰기 → 버스 폴트로 바로 드러난다.
4. **`.ARM.exidx` 176바이트** 는 clang 객체에만 있었다(-fno-exceptions 인데도 함수마다 언와인드 인덱스 항목). GCC 링크 결과의 섹션 목록(`size -A`)에는 `.ARM.exidx` 가 없었다. 컴파일러마다 다르니 크기 예산은 **링크된 ELF** 로 잰다.
5. 링크 크기 text 2052 B 중 가장 큰 함수는 `Button::settling` 600 B(인라인된 publish·new_evt 포함), 그다음 `app_main` 480 B(`start()` 와 `run_one` 이 인라인됨).

**멤버 함수 포인터의 명령 비용** — `bash target.sh mfp` (`demo_mfp.cpp`, 같은 일을 세 방식으로):

```text
@ Cortex-M4 (thumbv7em)                @ arm64 (호스트)
_Z8call_mfpR1ARK5Event:                __Z8call_mfpR1ARK5Event:
	ldr	r3, [r0, #4]     @ adj            	ldp	x2, x8, [x0]
	ldr.w	r12, [r0]    @ ptr            	add	x0, x0, x8, asr #1
	lsls	r2, r3, #31  @ adj 의 0번 비트    	tbz	w8, #0, LBB0_2
	add.w	r0, r0, r3, asr #1  @ this += adj>>1	ldr	x8, [x0]
	beq	.LBB0_2                         	ldr	x2, [x8, w2, uxtw]
	ldr	r2, [r0]         @ (가상이면) vptr  LBB0_2:
	ldr.w	r12, [r2, r12]                  	br	x2
.LBB0_2:
	bx	r12
_Z7call_fpR1BRK5Event:                 __Z7call_fpR1BRK5Event:
	ldr	r2, [r0]                        	ldr	x2, [x0]
	bx	r2                              	br	x2
```

| 방식 | 저장 크기 (M4) | 호출 명령 (M4) | 분기 |
|---|---|---|---|
| 멤버 함수 포인터 | 8 B | 8 (가상 아님 경로 6) | 가상 비트 검사 1 |
| C 스타일 함수 포인터 `Status(*)(B&, const Event&)` | 4 B | 2 | 없음 |
| 상태 객체의 가상 함수 | 4 B (포인터) | 8 (`ldr` 3번 + 인자 정리) | 없음, 대신 vtable 상수 |

관찰 포인트: 가상 여부를 나타내는 비트가 `ptr` 이 아니라 **`adj` 의 0번 비트** 에 있고 `adj` 는 1 비트 밀려 저장된다(`lsls ..., #31` 검사, `asr #1` 보정). Itanium C++ ABI 의 일반 규칙은 `ptr` 의 0번 비트인데, Thumb 함수 주소는 0번 비트가 1 이라 그 자리를 쓸 수 없다 — 그래서 ARM 용 C++ ABI 가 바꾼 표현이다. Apple arm64 도 같은 표현을 쓰는 것이 asm 에서 보였다(`asr #1`, `tbz w8, #0`). 우리 핸들러는 가상이 아니지만 컴파일러는 포인터 **값** 을 모르니 검사를 매번 넣는다. 디스패치 썽크(`target.sh asm`)에서는 핸들러 호출 3번(이벤트·exit·entry)마다 이 검사가 반복됐다. 이벤트 하나에 수십 사이클 대비 몇 명령이라 이 레슨은 가독성(핸들러가 멤버를 직접 씀)을 택했고, 1 kHz 이상의 핫 경로라면 C 스타일 함수 포인터가 정답이다.

**컴파일 단계에서 막는 실수** (`demo_errors.cpp`, Apple clang 21, 실제 출력 요약):

```text
-DSHOW_ERROR=1  constinit Slow g_slow;   // 생성자에서 read_reset_reason()
  error: variable does not have a constant initializer
  note: required by 'constinit' specifier here
  note: non-constexpr constructor 'Slow' cannot be used in a constant expression
-DSHOW_ERROR=2  g_board.fw.new_evt<NonTrivialEvt>(SIG_A);
  error: no matching member function for call to 'new_evt'
  note: because 'NonTrivialEvt' does not satisfy 'PoolEvent'
  note: because 'std::is_trivially_destructible_v<NonTrivialEvt>' evaluated to false
-DSHOW_ERROR=3  Button copy = g_board.btn;
  error: call to implicitly-deleted copy constructor of 'Button'
  note: ... because base class 'ActiveObject' has a deleted copy constructor
-DSHOW_ERROR=4  return tran(&Other::idle);   // Mine 의 핸들러 안에서
  error: cannot initialize a parameter of type 'State' (aka 'Status (Mine::*)(const Event &)')
         with an rvalue of type 'Status (Other::*)(const Event &)': different classes ('Mine' vs 'Other')
```

하네스에도 같은 것을 `requires`/traits 로 둔다: `!can_new_evt<NonTrivialEvt>`, `!can_new_evt<BigEvt>`(블록보다 큼), `!is_copy_constructible_v<Button>`, `!is_convertible_v<Status (Led::*)(const Event&), Button::State>`, `!is_polymorphic_v<ActiveObject>`, `is_trivially_destructible_v<Board>`(전역이어도 소멸자 등록 없음). AO 복사 금지가 중요한 이유: `ActiveObject` 의 큐는 **자기 멤버** `qbuf_` 를 가리킨다. 복사본은 원본의 버퍼를 가리키는 큐를 갖게 된다.

### 버그 주입 결과

정답의 사본에 버그를 하나씩 넣고 돌린 결과 (레슨 폴더에는 남기지 않음):

| 주입한 버그 | 결과 | 잡은 테스트 |
|---|---|---|
| publish 의 임시 ref +1 / 끝의 gc 제거 | 66/69 | A4 구독자 0명 누수, B2 |
| 버튼의 `is_armed` 검사(①) 제거 | 65/69 | C2 트레이스(유령 누름), C4 |
| 버튼의 `pin != last_edge_` 검사(②) 제거 | 64/69 | C2b, C3 (`press_t_`), C4 |
| `run_one` 의 `CritSec` 제거 | 66/69 | B3 (ISR 의 post 유실) |
| gc 가 ref 를 무시하고 바로 반납 | 50/69 | A4, B, C1·C3·C4 |
| dispatch 에서 exit 호출 제거 | 65/69 | A2, A3, C1 |
| 실패한 post 에서 회수 안 함 | 64/69 | B1, C3 |
| 스케줄러가 가장 **낮은** 우선순위부터 | 66/69 | A3 (RTC 순서, 높은 것 먼저), C1 트레이스 |
| 만료된 한 번짜리를 목록에서 안 뺌 | **멈춤** (목록 고리) | 120초 타임아웃 |

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution   # 69/69 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer solution.cpp -o solution_san && ./solution_san   # 69/69, 보고 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 큐 넘침을 모두 `assert` | EXTI 버스트 하나로 리셋. `NDEBUG` 빌드에서는 검사가 사라져 메모리를 덮어씀 (CPP-M10) | 버려도 되는 이벤트는 `try_post` + 카운터, 보장 이벤트는 큐 크기 계산 + fault |
| 실패한 post 의 이벤트를 그냥 둠 | ref 0 블록이 영원히 풀 밖 — 몇 시간 뒤 `new_evt` 가 `nullptr` | `post_impl` 실패 경로에서 `ref == 0` 이면 회수 |
| publish 를 "구독자마다 post" 로만 | 구독자 0명이면 누수, 선점형 커널에서는 첫 구독자가 처리·회수한 블록을 두 번째에게 보냄 | 임시 ref +1 … 끝에 gc |
| 핸들러 안에서 다른 AO 의 핸들러를 직접 호출 | RTC 깨짐: 상대가 전이 도중일 수 있고 스택이 쌓임 (C-M07 의 재진입 실측) | 항상 post/publish, 처리는 스케줄러가 |
| `disarm()` 의 반환값 무시 | false = 타임아웃이 이미 큐에 — 재무장 후 옛 타임아웃을 새것으로 처리 | 타임아웃 핸들러에서 `is_armed` 면 무시 (또는 세대 번호) |
| 타임아웃 핸들러에서 핀을 "지금" 읽고 믿음 | 큐에 처리 안 된 엣지가 있을 수 있다 (C2b 실측 유령 누름) | ISR 이 엣지에 레벨을 싣고 `pin != last_edge_` 면 창 재시작 |
| `run_one` 에서 `get` 과 ready 정리를 따로 보호하거나 안 보호 | ISR 의 post 가 유실되거나 ready 비트가 꺼진 채 이벤트가 남음 | 한 `CritSec` 안에 둘 다, 디스패치는 밖 |
| 신호와 이벤트 타입이 안 맞는 정적 이벤트 | `SIG_A` 핸들러가 `static_cast<const ReqEvt&>` 하는데 `Event` 로 만들면 객체 밖을 읽음 (practice.cpp 를 쓰다 ASan 이 `global-buffer-overflow` 로 실측) | 신호 ↔ 타입 표를 한곳에. 생성은 `new_evt<T>` 나 `T` 타입 상수로만 |
| 이벤트에 소멸자 있는 멤버 (`std::string` 류) | gc 는 소멸자를 부르지 않는다 → 누수/UB | `PoolEvent` 개념: trivially destructible 만 |
| AO 를 값으로 복사·이동 | 큐 버퍼 포인터가 원본을 가리킴 | `ActiveObject` 복사 `= delete` |
| `TimeEvent` 를 풀에서 할당 | 주기 타이머가 블록을 영구 점유, 풀 고갈 계산이 꼬임 | AO 멤버(정적 이벤트, `pool = 0`) |
| 침투형 목록 불변식 위반 (`ctr_ == 0` 인데 목록에 남김) | 다음 `arm` 이 같은 노드를 다시 넣어 고리 → `tick()` 무한 루프 (주입 실측) | 불변식을 한 곳(`tick`/`disarm`)에서만 바꿈 |

## 8. 면접 Q&A

<details>
<summary>Q1. 액티브 오브젝트가 "공유 데이터 + 뮤텍스" 보다 나은 점과 나쁜 점은?</summary>

좋은 점: 각 AO 의 데이터는 그 AO 의 핸들러만 만진다 → 락이 없고 우선순위 역전·교착이 구조적으로 없다. ISR 은 이벤트를 만들어 넣기만 하니 짧다. RTC 라 핸들러 안에서는 상태가 일관된다. 테스트가 쉽다 — 이벤트 시퀀스를 넣고 트레이스를 비교하면 된다(이 레슨의 C1). 나쁜 점: 메모리(큐·풀을 최악으로 잡아야 함), 지연(협조형이면 가장 긴 핸들러가 다른 AO 의 응답 시간 상한), 요청-응답이 비동기라 코드가 상태로 쪼개진다. 꼬리: "긴 계산이 필요하면?" → 잘게 나눠 자기에게 이벤트를 post 하며 이어 가거나, 선점형(RTOS 태스크당 AO)으로.

</details>

<details>
<summary>Q2. 이벤트를 큐에 값으로 복사하지 않고 풀 + 포인터로 하는 이유와 대가는?</summary>

publish 가 구독자 수와 무관하게 포인터 하나만 넣는다(zero-copy). 큐 칸이 4바이트라 큐 RAM 이 작고, 큰 이벤트도 같은 비용. 대가는 **수명 관리**: 누가 마지막으로 쓰는지 몰라서 참조 카운트가 필요하고, 실패 경로마다 회수를 빠뜨리면 느린 누수가 된다. 그래서 테스트마다 "풀 8/8" 을 확인한다. 이벤트가 작고 구독자가 하나뿐인 시스템(대부분의 소형 MCU)은 값 복사 큐가 더 단순하고 안전할 수 있다 — FreeRTOS 큐가 그 방식(값 복사). 꼬리: "ref 를 8비트로 해도 되나?" → 구독자 수 + 연기 큐 수가 255 를 넘지 않으면. 넘을 수 있으면 `static_assert` 로 막는다.

</details>

<details>
<summary>Q3. 상태를 멤버 함수 포인터로 저장하면 Cortex-M 에서 무엇을 내나요?</summary>

크기 8바이트(ptr + adj, 실측 `Show<..., 8, ...>`), 호출마다 가상 여부 검사 분기: `ldr adj; ldr ptr; lsls #31; add this, adj asr #1; beq; (가상이면 vptr 로드 2개); bx` — 8명령, 가상 아닌 경로로는 6명령 (실측). ARM C++ ABI 는 Thumb 주소의 0번 비트가 1 이라 가상 표시를 `adj` 의 0번 비트로 옮겼다(asm 의 `lsls ..., #31`). C 스타일 `Status(*)(D&, const Event&)` 는 4바이트, `ldr; bx` 2명령. 이 레슨은 핸들러가 private 멤버를 바로 쓰는 가독성을 택했고, 디스패치 썽크에서 이 검사가 이벤트당 최대 3번(이벤트, exit, entry) 반복되는 것을 확인했다. 꼬리: "가상 함수 State 패턴은?" → 상태마다 객체·vtable 이 필요하고 호출 8명령(실측). 장점은 상태별 데이터를 상태 객체에 둘 수 있다는 것.

</details>

<details>
<summary>Q4. publish 에서 왜 자기 참조를 하나 더 잡나요? 협조형 스케줄러에서도 필요한가요?</summary>

두 가지. ① 구독자 0명: 아무도 post 받지 않으면 ref 가 0 인 채로 끝나 반납할 주체가 없다 → 임시 +1 후 끝의 gc 가 회수한다(테스트 "lonely"). ② 선점형: 첫 구독자가 더 높은 우선순위면 post 하는 순간 실행돼 처리·gc 까지 끝낼 수 있다. 그때 ref 가 0 이 되면 블록이 풀로 가고, 두 번째 구독자에게는 해제된 블록 포인터가 간다. 협조형에서는 ②가 일어나지 않지만 ①은 여전하고, 나중에 선점형으로 옮길 때 같은 코드가 맞도록 둔다. 임시 +1 을 지운 변이는 A4 의 구독자 0명 테스트에서 블록 1개가 돌아오지 않았다.

</details>

<details>
<summary>Q5. 큐가 넘치면 어떻게 하나요? assert 인가요, 버리나요?</summary>

이벤트 종류마다 다르다. 버튼 엣지처럼 **상태의 사본** 인 이벤트는 버리고 센다 — 디바운스가 핀을 다시 읽으니 결과가 같고(C3: 13개 중 9개 버려도 누름 1번, 20 ms 늦을 뿐), ISR 에서 리셋하는 것보다 낫다. 타임아웃·사용자 명령처럼 **잃으면 기능이 깨지는** 이벤트는 보장 post 로 두고, 넘침은 설계 버그로 fault(로그 + 리셋)한다. 그 보장은 큐 크기 계산으로 만든다: 생산자별 최대 미처리 이벤트 합. 실측 수위(`max_used`)를 현장 로그로 남겨 계산이 맞았는지 확인한다(C4: btn 2, led 1). QP 도 같은 두 모드를 갖는다 — margin 없는 post 는 실패 시 assertion, margin 을 준 post 는 `false` (문서 확인). 꼬리: "버린 이벤트의 블록은?" → post 가 그 자리에서 회수한다. 호출자는 실패 후 이벤트를 만지면 안 된다.

</details>

<details>
<summary>Q6. 디바운스 타이머의 disarm 이 false 를 돌려줬습니다. 무슨 뜻이고 어떻게 처리하나요?</summary>

타이머가 이미 만료돼 타임아웃 이벤트가 **큐에 들어가 있다**(또는 애초에 무장 안 됨). 그 상태에서 재무장하면 큐의 옛 타임아웃이 새 창의 만료로 오해된다 → 유령 누름(C2 의 실측 트레이스: 31 ms 에 PRESSED, 52 ms 에 RELEASED). 처리: 타임아웃 핸들러에서 `is_armed()` 면 낡은 것으로 무시. 일반화하면 타임 이벤트에 세대 번호를 싣고 AO 가 현재 세대만 받는다. 꼬리: "그것만으로 충분한가?" → 아니다. 순서가 반대(타임아웃 먼저, 엣지 나중)면 타임아웃은 진짜인데 핀은 이미 바뀌었다(C2b 실측). ISR 이 엣지에 레벨을 싣고 "핀 ≠ 마지막 처리한 엣지 레벨" 이면 창을 다시 연다.

</details>

<details>
<summary>Q7. 이 프레임워크가 RAM 과 플래시를 얼마나 쓰나요? 힙을 안 쓴다는 걸 어떻게 증명하나요?</summary>

Cortex-M4 실측: `Board`(풀 144 + 프레임워크 100 + AO 3개 272 + 기타) 528 B, 링크된 ELF text 2052 / data 536 / bss 12 (gc-sections, -O2). 힙 증명은 세 겹: ① 호스트 하네스가 전역 `operator new` 를 바꿔 세고 마지막에 0 확인, ② 객체 파일의 `llvm-nm -u` 가 비어 있음(`_Znwj`, `malloc` 없음), ③ 링크된 ELF 를 `nm` 으로 검사해 `operator new|malloc|free|__cxa_|_sbrk` 가 하나라도 있으면 `link.sh` 가 exit 1. 꼬리: "`g_board` 가 왜 `.data` 죠?" → constinit 이지만 0 이 아닌 상수(우선순위, 자기 포인터, 핸들러 주소)로 초기화돼 플래시에 초기값 528 B 가 따로 든다. 전부 0 으로 두고 런타임에 채우면 `.bss` 로 간다 — 보장과 플래시의 맞교환.

</details>

<details>
<summary>Q8. 이벤트가 없을 때 저전력으로 자려면 idle 루프를 어떻게 쓰나요?</summary>

`if (!run_one()) wfi();` 는 틀렸다: 검사와 WFI 사이에 인터럽트가 와서 이벤트를 넣으면, 그 ISR 은 이미 끝났으니 WFI 는 **다음** 인터럽트(최악 1틱)까지 잔다. 정답: `cpsid i` → ready 다시 확인 → 비었으면 `wfi` → `cpsie i`. PRIMASK=1 이어도 pending 인터럽트는 WFI 를 깨우고, `cpsie i` 순간 ISR 이 실행된다(ARMv7-M ARM 의 WFI 깨움 조건 — DDI 0403). 연습 4 가 호스트 모델로 두 판을 비교한다: 순진한 판은 일이 있는데 `sleeps == 1`, 안전한 판은 0. 꼬리: "tickless 는?" → 다음 만료까지 틱을 건너뛰고 저전력 타이머로 깨운다(연습 3: 200틱 동안 깨어남 200 → 13, 만료 시각은 같음).

</details>

<details>
<summary>Q9. 이걸 테스트 가능하게 만든 핵심은 뭔가요?</summary>

시간과 인터럽트를 **입력** 으로 만든 것. 하드웨어 의존은 포트 층 세 함수(`irq_save/irq_restore/isr_window`)와 핀 하나뿐이고, 시뮬레이터가 1 ms 마다 EXTI → SysTick → main 을 결정적으로 돌린다. 그래서 "31 ms 에 만료와 엣지가 겹친다" 같은 현장 재현이 어려운 경합을 한 줄짜리 핀 스크립트로 만들고, 두 ISR 순서를 모두 시험한다. 트레이스를 이진 기록으로 남기는 건 QP 의 QS, RTOS-A06 의 훅 트레이스와 같은 발상이고, 같은 기록을 실기에서 UART/RTT 로 빼면 현장 로그가 된다. 그리고 호스트에서 테스트한 `solution.cpp` 를 `demo_target.cpp` 가 그대로 include 해 Cortex-M 으로 컴파일한다 — 테스트한 코드와 굽는 코드가 같다.

</details>

## 9. 추가 연습

### 연습 1 — 이벤트 연기 (defer / recall)

`Worker` AO 는 일 하나에 50틱 걸린다(`busy`). 바쁠 때 온 요청은 2칸짜리 연기 큐에 넣고(넘치면 `lost++`), `idle` 로 돌아올 때 하나씩 꺼내 자기 큐에 다시 넣는다. 요청 4개를 한꺼번에 넣으면 처리 순서 1, 2, 3 / 잃음 1 / 끝에 풀 8/8 이어야 한다.

<details>
<summary>답 보기</summary>

```cpp
bool defer(EventQueue& dq, const Event* e) {
    CritSec cs;
    if (!dq.put(e)) return false;          // 넘침: ref 를 안 올렸으니 RTC 끝 gc 가 회수
    if (e->pool != 0) ++e->ref;            // 연기 큐도 "소유자" → RTC 끝의 gc 가 반납하지 않는다
    return true;
}
bool recall(ActiveObject& ao, EventQueue& dq) {
    const Event* e = nullptr;
    { CritSec cs; e = dq.get(); }
    if (e == nullptr) return false;
    (void)post(ao, e);                     // 큐가 +1
    gc(e);                                 // 연기 큐의 몫 -1
    return true;
}
```

핵심은 참조 카운트의 의미를 "이 블록을 들고 있는 **곳** 의 수" 로 일반화하는 것이다. 연기는 RTC 가 끝나도 블록이 살아 있어야 하니 +1, recall 은 소유를 큐로 넘기니 post(+1) 후 gc(-1). QP 문서도 defer 가 "another outstanding reference" 를 잡아 RTC 끝에 재활용되지 않게 한다고 설명한다. 차이: QP 의 recall 은 자기 큐에 **LIFO** 로 넣어 다음에 바로 처리되게 한다(문서 확인). 이 연습은 FIFO 큐만 있어서 뒤에 넣었다 — 그사이 다른 요청이 오면 순서가 바뀔 수 있다는 점이 LIFO 를 쓰는 이유다.

</details>

### 연습 2 — 크기별 이벤트 풀 2개

블록 8 B 풀과 32 B 풀을 두고, `new_evt<T>` 는 `T` 가 들어가는 **가장 작은** 풀에서 할당한다. 작은 풀이 비면 큰 풀로 넘어가지 **않는다**. gc 는 이벤트의 `pool` 번호로 제 풀에 돌려준다.

<details>
<summary>답 보기</summary>

```cpp
for (uint8_t i = 0; i < 2; ++i) {
    EventPool* p = pools_[i];
    if (p == nullptr || sizeof(T) > p->block_size()) continue;
    void* blk = p->alloc();
    if (blk == nullptr) return nullptr;    // 넘어가지 않는다: 결정적, 큰 풀은 큰 이벤트 몫으로 보존
    T* e = ::new (blk) T{};
    e->sig = sig; e->pool = static_cast<uint8_t>(i + 1); e->ref = 0;
    return e;
}
return nullptr;
// gc / post 실패: pools_[e->pool - 1]->release(...)
```

넘어가기(fallback)를 허용하면 작은 이벤트 폭주가 큰 이벤트 블록까지 먹어 **다른 종류의** 기능이 실패한다 — 풀을 나눈 이유가 사라진다. `ReqEvt`(6 B)는 1번 풀, `BigEvt`(28 B)는 2번 풀, 작은 풀 4개를 다 쓰면 다섯 번째는 `nullptr` 이고 큰 풀은 그대로(테스트). 이벤트 헤더의 `pool` 바이트가 "어디로 돌려줄지" 를 담으니 gc 는 크기를 몰라도 된다.

</details>

### 연습 3 — tickless idle: `next_expiry()` / `skip(n)`

무장된 타임 이벤트 중 가장 빠른 만료까지 남은 틱을 돌려주는 `next_expiry()` 와, 아무것도 만료되지 않는 범위에서 n 틱을 한 번에 빼는 `skip(n)` 을 써라. 주기 타이머 A(7, 30)·B(12, 45)를 200틱 돌려 매 틱 방식과 tickless 방식의 (시각, 신호) 기록이 같고, 깨어난 횟수가 200 → 13 이 되는지 확인하라.

<details>
<summary>답 보기</summary>

```cpp
uint32_t next_expiry() const {
    CritSec cs;
    uint32_t m = 0;
    for (const TimeEvent* t = armed_; t != nullptr; t = t->next_) if (m == 0 || t->ctr_ < m) m = t->ctr_;
    return m;
}
void skip(uint32_t n) { CritSec cs; for (TimeEvent* t = armed_; t; t = t->next_) t->ctr_ -= n; }   // 전제: n < next_expiry()
// idle: k = next_expiry(); skip(k - 1); g_now += k - 1;  → 저전력 타이머로 k 틱 뒤 깨어나 tick() 한 번
```

만료는 A 7번(7, 37, …, 187) + B 5번(12, 57, …, 192) = 12번, 깨어남은 그 12번 + 마지막 200 = 13번(실측 출력 `wakeups: every tick 200, tickless 13`). `skip(k - 1)` 후 마지막 1틱은 보통의 `tick()` 으로 처리하므로 만료·재장전 코드는 하나뿐이다. 실제 칩에서는 SysTick 을 멈추고 LPTIM/RTC 로 k 틱 뒤에 깨우며, **일찍** 깨어났다면(다른 인터럽트) 실제 지난 틱만큼만 skip 한다 — 그 보정이 tickless 구현의 어려운 부분이다(FreeRTOS `configUSE_TICKLESS_IDLE` 이 같은 문제를 푼다, RTOS 트랙 참고).

</details>

### 연습 4 — idle 진입 경쟁: WFI 와 PRIMASK

`idle_naive`: `if (!run_one()) { /* 인터럽트 */ wfi(); }`. `idle_safe`: PRIMASK 를 잡고 ready 를 다시 본 뒤 WFI. 호스트 모델의 `wfi_model()` 은 pending 인터럽트가 있으면 바로 깨고 없으면 `g_sleeps++`. 두 판에서 "검사 직후 인터럽트가 이벤트를 넣는" 상황을 만들어 비교하라.

<details>
<summary>답 보기</summary>

```cpp
static void idle_safe(Framework& fw) {
    if (fw.run_one()) return;
    const uint32_t pm = irq_save();      // cpsid i
    if (fw.ready() == 0) {
        isr_window();                    // 여기 온 인터럽트는 pending
        wfi_model();                     // pending 이 있으면 WFI 는 바로 깬다 (PRIMASK=1 이어도)
    }
    irq_restore(pm);                     // cpsie i → ISR 실행 → ready
}
```

순진한 판: ISR 이 검사와 WFI 사이에 **실행 완료** 되므로 WFI 시점엔 pending 이 없다 → `g_sleeps == 1` 인데 `ready != 0` (일이 있는데 잤다). 안전한 판: 같은 인터럽트가 pending 으로 남아 WFI 가 즉시 반환, 복원 순간 ISR 실행 → `g_sleeps == 0`, 바로 처리(테스트). `demo_target.cpp` 의 `app_main` 은 간단히 순진한 판으로 써 두었다 — 제품에서는 이 연습의 판으로 바꿔야 한다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 17/17 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer practice.cpp -o practice_san && ./practice_san   # 17/17, 보고 없음
```

## 10. 핵심 정리

- AO = 자기 큐 + 상태 머신 + RTC. AO 끼리는 이벤트만 주고받고, ISR 은 이벤트를 만들어 넣기만 한다 → 락 없음.
- 이벤트 풀 + 포인터 큐 = zero-copy, 대가는 수명: post 성공 +1, 처리 후 gc -1, publish 는 임시 +1. 실패 경로에서 ref 0 이면 그 자리에서 회수. 모든 시나리오 끝에 "풀 8/8" 을 테스트한다.
- 넘침 정책은 이벤트별: 상태의 사본(엣지)은 버리고 세며, 잃으면 기능이 깨지는 것(타임아웃, 명령)은 큐 크기로 보장하고 넘침은 fault.
- `run_one`: `get` + ready 정리는 한 임계 구역(실측 22~26명령), 디스패치는 밖. 우선순위 선택은 `clz` 1명령.
- 타임 이벤트는 AO 멤버(정적), 불변식 `ctr_ == 0` ⇔ 목록 밖. `disarm() == false` 면 타임아웃이 이미 큐에 있다 → `is_armed` 로 낡은 것 무시. 그리고 타임아웃 뒤에 온 엣지는 "핀 ≠ 마지막 엣지 레벨" 로 잡는다.
- Cortex-M4 실측: Board 528 B(`.data`, 플래시 이미지 528 B 별도), ELF text 2052 B, `operator new`/`malloc`/`__cxa_*`/`.init_array` 0. `mutable` 멤버 때문에 constexpr 정적 이벤트가 `.rodata` 가 아니라 `.data` 로 간다.
- 멤버 함수 포인터 = 8 B + 8명령(가상 비트 검사, ARM ABI 는 `adj` 에 표시) vs 함수 포인터 4 B + 2명령. 가독성과 사이클의 맞교환.

## 참고자료

- C++ 표준 (N4861 C++20 초안): [dcl.constinit] (동적 초기화면 ill-formed), [basic.start.static] (상수 초기화), [dcl.stc] (`mutable`), [dcl.type.cv] (const 객체 수정만 UB), [basic.life] (저장소 재사용으로 수명 종료), [expr.mptr.oper] (`.*` 호출), [conv.mem], [expr.static.cast] (파생 클래스로의 static_cast — 실제 타입이 아니면 UB), [bit.count] (`std::countl_zero`), [temp.constr] / [temp.names] (requires 제약) — <https://isocpp.org/files/papers/N4861.pdf>
- Itanium C++ ABI — 멤버 함수 포인터 표현 (ptr, adj): <https://itanium-cxx-abi.github.io/cxx-abi/abi.html>
- C++ ABI for the Arm Architecture (IHI 0041) — 멤버 함수 포인터의 가상 비트를 `adj` 로 옮긴 변경 (이 레슨에서는 asm 으로 관찰): <https://github.com/ARM-software/abi-aa/releases>
- ARMv7-M Architecture Reference Manual (DDI 0403) — WFI 와 깨움 조건, PRIMASK, CLZ: <https://developer.arm.com/documentation/ddi0403/latest/>
- Miro Samek, *Practical UML Statecharts in C/C++*, 2nd ed. (2008) — active object, RTC, 이벤트 풀과 참조 카운트의 원전 설명 (아이디어 참고만).
- QP/C++ 레퍼런스 — `QActive::post_/postx_/publish_/defer/recall`: <https://www.state-machine.com/qpcpp/class_q_p_1_1_q_active.html>, `QTimeEvt::disarm/rearm`: <https://www.state-machine.com/qpcpp/class_q_p_1_1_q_time_evt.html> (QP 는 GPL/상용 이중 라이선스 — 코드는 쓰지 않았다)
- UML 2.5.1 Specification — State Machines 장의 run-to-completion 의미론: <https://www.omg.org/spec/UML/2.5.1/>
- 관련 레슨: C-M07 (표 FSM, RTC 큐, 재진입), DS-B03 (링 버퍼), DS-M02 (고정 크기 풀), CPP-B06 (CriticalSection), CPP-B09 (정적 초기화, .init_array), CPP-B14 (operator new 금지), CPP-M06 (vtable 비용), CPP-M10 (static_vector, 넘침 정책), CPP-M12 (delegate 2워드), CPP-A02 (ISR 트램펄린 — `extern "C"` 핸들러 → 멤버), CPP-A03 (`__cxa_*` 런타임 의존), CPP-A04 (ISR 공유, PRIMASK 모델), CPP-A05 (thin template), RTOS-B13 (이벤트 그룹), RTOS-A06 (트레이스 훅)
